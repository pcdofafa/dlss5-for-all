// Teste da ponte_gpu.h: manda uma textura de uma placa para outra, confere os pixels e
// mede quadros/segundo sustentados em 4K e 1440p, nas duas direcoes.
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <string>
#include <vector>
#include "ponte_gpu.h"

static IDXGIAdapter1* Achar(const wchar_t* parte) {
    IDXGIFactory1* f = nullptr; CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f);
    IDXGIAdapter1* achado = nullptr;
    for (UINT i = 0; !achado; i++) {
        IDXGIAdapter1* a = nullptr;
        if (f->EnumAdapters1(i, &a) != S_OK || !a) break;
        DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
        if (wcsstr(d.Description, parte) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) achado = a; else a->Release();
    }
    f->Release();
    return achado;
}
static ID3D11Device* Dev11(IDXGIAdapter1* a) {
    ID3D11Device* d = nullptr; ID3D11DeviceContext* c = nullptr;
    D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(a, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, lv, 1, D3D11_SDK_VERSION, &d, nullptr, &c);
    if (FAILED(hr)) { printf("D3D11CreateDevice falhou 0x%08lX\n", hr); return nullptr; }
    c->Release();
    return d;
}
static ID3D11Texture2D* Tex(ID3D11Device* d, UINT w, UINT h, UINT cpu, D3D11_USAGE uso) {
    D3D11_TEXTURE2D_DESC td{}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = uso;
    td.BindFlags = (uso == D3D11_USAGE_STAGING) ? 0 : D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = cpu;
    ID3D11Texture2D* t = nullptr; d->CreateTexture2D(&td, nullptr, &t); return t;
}
static double Ms() { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return 1000.0 * c.QuadPart / f.QuadPart; }

static int Rodar(const wchar_t* nomeA, const wchar_t* nomeB, UINT w, UINT h, bool conferir) {
    IDXGIAdapter1* aA = Achar(nomeA); IDXGIAdapter1* aB = Achar(nomeB);
    if (!aA || !aB) { printf("adaptador nao achado\n"); return 1; }
    ID3D11Device* dA = Dev11(aA); ID3D11Device* dB = Dev11(aB);
    if (!dA || !dB) return 1;
    ID3D11DeviceContext* cA = nullptr; dA->GetImmediateContext(&cA);
    ID3D11DeviceContext* cB = nullptr; dB->GetImmediateContext(&cB);

    PonteGpu::Ponte ponte; std::wstring erro;
    double t0 = Ms();
    if (!ponte.Iniciar(dA, dB, w, h, DXGI_FORMAT_B8G8R8A8_UNORM, &erro)) { printf("Iniciar falhou: %ls\n", erro.c_str()); return 2; }
    printf("[%ls -> %ls] %ux%u ponte de pe em %.1f ms (filas de copia=%d, %.1f MB/quadro)\n", nomeA, nomeB, w, h, Ms() - t0,
           (int)ponte.FilasDeCopia(), ponte.BytesPorQuadro() / 1048576.0);

    // textura de origem com padrao f(x,y)
    ID3D11Texture2D* src = Tex(dA, w, h, 0, D3D11_USAGE_DEFAULT);
    std::vector<uint32_t> pix((size_t)w * h);
    for (UINT y = 0; y < h; y++) for (UINT x = 0; x < w; x++) pix[(size_t)y * w + x] = (x * 7u) ^ (y * 131u) ^ 0xA5000000u;
    cA->UpdateSubresource(src, 0, nullptr, pix.data(), w * 4, 0);

    ID3D11Texture2D* stg = Tex(dB, w, h, D3D11_CPU_ACCESS_READ, D3D11_USAGE_STAGING);

    if (conferir) {
        if (!ponte.Enviar(src)) { printf("Enviar falhou\n"); return 3; }
        uint64_t n = 0; ID3D11Texture2D* got = nullptr;
        for (int k = 0; k < 100 && !got; k++) { got = ponte.Receber(&n); if (!got) Sleep(1); }
        if (!got) { printf("Receber nao devolveu nada\n"); return 4; }
        cB->CopyResource(stg, got);
        ponte.Liberar(n);
        D3D11_MAPPED_SUBRESOURCE m{};
        HRESULT hr = cB->Map(stg, 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) { printf("Map falhou 0x%08lX\n", hr); return 5; }
        size_t erros = 0;
        for (UINT y = 0; y < h; y++) {
            const uint32_t* row = (const uint32_t*)((const char*)m.pData + (size_t)y * m.RowPitch);
            for (UINT x = 0; x < w; x++) if (row[x] != pix[(size_t)y * w + x]) erros++;
        }
        cB->Unmap(stg, 0);
        printf("  verificacao pixel a pixel: %zu erros de %zu (quadro %llu)\n", erros, pix.size(), (unsigned long long)n);
        if (erros) return 6;
    }

    // laco sustentado: envia o mais rapido que o anel deixa; o destino consome tudo
    const int N = 300;
    int enviados = 0, descart = 0, recebidos = 0;
    ID3D11Texture2D* alvo = Tex(dB, w, h, 0, D3D11_USAGE_DEFAULT);
    t0 = Ms();
    uint64_t ultimo = 0;
    while (enviados < N) {
        if (ponte.Enviar(src)) enviados++; else { descart++; }
        uint64_t n = 0; ID3D11Texture2D* got = ponte.Receber(&n);
        if (got) { cB->CopyResource(alvo, got); ponte.Liberar(n); cB->Flush(); recebidos++; ultimo = n; }
        if (!got && descart) Sleep(0);
    }
    // espera o ultimo atravessar
    for (int k = 0; k < 2000 && ponte.Atravessados() < (uint64_t)enviados; k++) Sleep(1);
    { uint64_t n = 0; ID3D11Texture2D* got = ponte.Receber(&n); if (got) { cB->CopyResource(alvo, got); ponte.Liberar(n); cB->Flush(); recebidos++; } }
    const double dt = Ms() - t0;
    printf("  %d quadros em %.0f ms = %.1f quadros/s (%.2f ms por quadro); descartes por anel cheio: %d; recebidos %d; atravessados %llu\n",
           enviados, dt, enviados * 1000.0 / dt, dt / enviados, descart, recebidos, (unsigned long long)ponte.Atravessados());
    (void)ultimo;
    ponte.Parar();
    alvo->Release(); stg->Release(); src->Release();
    cA->Release(); cB->Release(); dA->Release(); dB->Release(); aA->Release(); aB->Release();
    return 0;
}

int wmain() {
    int r = 0;
    r |= Rodar(L"NVIDIA", L"Radeon", 3840, 2160, true);
    r |= Rodar(L"Radeon", L"NVIDIA", 3840, 2160, true);
    r |= Rodar(L"NVIDIA", L"Radeon", 2560, 1440, false);
    r |= Rodar(L"Radeon", L"NVIDIA", 2560, 1440, false);
    printf("resultado: %d\n", r);
    return r;
}
