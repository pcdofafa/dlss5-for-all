// =====================================================================================
//  sonda.h - AS SONDAS DA PLACA, compartilhadas pelo app e pelo LAUNCHER (07/10/2026)
//
//  `--sonda-d3d12 <luid> <arquivo>`: D3D12CreateDevice + fila + fence com um Signal trivial.
//  `--sonda-ngx <pasta do app> <arquivo>`: device D3D12 + NGX do driver + a rede + 1 passe.
//  Os dois rodam num processo DESCARTAVEL que nao pode ter o ReShade: na pasta do app (bin\) o
//  dxgi.dll e o do ReShade, e um device criado ali carregaria o ReShade e os add-ons (o feed
//  montaria rede em cima da sonda).
//
//  ATE A 2.1 o app copiava o PROPRIO exe para %TEMP%\DLSS5ForAll-sonda\dlss5-sonda.exe e rodava
//  a copia, oculta. Agora QUEM RODA A SONDA E O LAUNCHER DA RAIZ (..\DLSS5ForAll.exe): ele ja
//  mora fora do bin\ (ao lado dele nao ha dxgi.dll nenhum), ja importa dxgi e passou a importar
//  d3d12. Ao ver `--sonda-*` na linha de comando ele executa a sonda e sai, sem tela e sem
//  bilhete (launcher.cpp, topo do wWinMain). O app nao copia mais executavel nenhum.
//
//  Este arquivo so tem o CORPO das sondas (o que roda dentro do processo descartavel). Quem as
//  lanca, espera e le o resultado e o reinicio_auto.h (RodarSonda / RodarSondaNgx), so no app.
//  O arquivo de resultado (e a pasta de dados do NGX da sonda) fica na pasta de dados do
//  programa (bin\ ou %LOCALAPPDATA%\DLSS5ForAll\), nunca no %TEMP%.
// =====================================================================================
#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <dxgi.h>
#include <d3d12.h>
#include <string>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include "ponte/cliente/ngx/ngx_contrato.h"   // NS-2309 item 4: a sonda da rede usa o mesmo contrato NGX do app

namespace ReinicioAuto {
// ---------------------------------------------------------------- 1. a sonda
// Codigos de saida: 0 ok | 10 sem placa | 11 D3D12CreateDevice falhou | 12 fila/fence falhou |
// 13 o Signal nao completou em 3 s | 14 a sonda nao voltou (o lancador matou) | 15 nao consegui
// lancar a sonda | 16 a sonda nao rodou (sem o launcher da raiz ou sem pasta de dados).
static const wchar_t* g_sondaArq = nullptr;
static int SondaDizer(int cod, const char* fmt, ...) {
    if (g_sondaArq && *g_sondaArq) {
        if (FILE* f = _wfopen(g_sondaArq, L"w")) {
            va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
            fclose(f);
        }
    }
    return cod;
}
#define dizer SondaDizer
static int SondaMain(const wchar_t* luidTxt, const wchar_t* arqRes) {
    g_sondaArq = arqRes;
    LUID alvo{}; bool temAlvo = false;
    if (luidTxt && *luidTxt) {
        const wchar_t* dp = wcschr(luidTxt, L':');
        if (dp) { alvo.HighPart = (LONG)wcstoul(luidTxt, nullptr, 16); alvo.LowPart = (DWORD)wcstoul(dp + 1, nullptr, 16); temAlvo = true; }
    }
    IDXGIFactory1* fab = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fab)) || !fab) return dizer(10, "CreateDXGIFactory1 falhou");
    IDXGIAdapter1* ad = nullptr;
    wchar_t nome[128] = L"?";
    for (UINT i = 0; ; i++) {
        IDXGIAdapter1* a = nullptr;
        if (fab->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
        const bool sw = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        const bool casa = temAlvo ? (d.AdapterLuid.HighPart == alvo.HighPart && d.AdapterLuid.LowPart == alvo.LowPart) : !sw;
        if (casa && !ad) { ad = a; wcsncpy(nome, d.Description, 127); nome[127] = 0; } else a->Release();
    }
    fab->Release();
    if (!ad) return dizer(10, "nenhuma placa %s", temAlvo ? "com o LUID pedido" : "de hardware");
    ID3D12Device* dev = nullptr;
    const HRESULT hr = D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev);
    ad->Release();
    if (FAILED(hr) || !dev) return dizer(11, "D3D12CreateDevice falhou 0x%08X (%ls)", (unsigned)hr, nome);
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* q = nullptr; ID3D12Fence* fe = nullptr;
    int cod = 0; char det[200] = "ok";
    if (FAILED(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q)) || !q ||
        FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fe)) || !fe) {
        cod = 12; snprintf(det, sizeof det, "fila ou fence falhou (removido 0x%08X)", (unsigned)dev->GetDeviceRemovedReason());
    } else {
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        q->Signal(fe, 1);
        if (fe->GetCompletedValue() < 1 && ev) {
            fe->SetEventOnCompletion(1, ev);
            if (WaitForSingleObject(ev, 3000) != WAIT_OBJECT_0) { cod = 13; snprintf(det, sizeof det, "o Signal nao completou em 3 s (removido 0x%08X)", (unsigned)dev->GetDeviceRemovedReason()); }
        }
        if (ev) CloseHandle(ev);
    }
    if (fe) fe->Release();
    if (q) q->Release();
    dev->Release();
    return dizer(cod, "%s (%ls)", det, nome);
}
#undef dizer

// ---------------------------------------------------------------- 1b. a SONDA DA REDE antes de abrir (NS-2309 item 4)
// `--sonda-ngx <pasta do app> <arquivo>`: o launcher da raiz (sem o ReShade ao lado), num processo DESCARTAVEL:
// device D3D12 na placa NVIDIA -> Init do NGX do driver -> Init_Ext do nvngx_dlssnr.dll da pasta do app -> UMA feature
// da rede (Reserved18) a 640x360 -> UM evaluate com quadro preto e vetores zero -> tudo solto. Se o driver ou a rede
// estiverem quebrados (TDR, crash, device removido, create recusado), quem cai e ESTE processo, nao o app no meio do
// jogo. Ideia observada no NeuralScreen (pre-voo/gate de compatibilidade, fronteira MIT/PolyForm) - codigo proprio,
// escrito a partir do NOSSO feed (NrEnsure / NrAttachSnippet / NrInstallCallerSpoof), nada copiado de la.
// Codigos: 0 ok | 20 sem placa NVIDIA | 21 D3D12CreateDevice | 22 fila/lista/fence/texturas | 23 sem _nvngx.dll do driver
// | 24 Init do core | 25 sem nvngx_dlssnr.dll / export | 26 Init_Ext | 27 AllocateParameters | 28 CreateFeature |
// 29 Evaluate | 30 a GPU nao terminou (fence) / device removido | 14 nao voltou no prazo (o lancador matou) | 15/16 lancar / nao rodou.
namespace SondaNgx {
typedef Ngx::Result (*FnCoreInit)(unsigned long long, const wchar_t*, ID3D12Device*, const void*, unsigned);
typedef Ngx::Result (*FnAlloc)(Ngx::Parameter**);
typedef Ngx::Result (*FnDestroy)(Ngx::Parameter*);
typedef Ngx::Result (*FnShutdown)(ID3D12Device*);
typedef Ngx::Result (*FnInitExt)(unsigned long long, const wchar_t*, ID3D12Device*, unsigned, const Ngx::Parameter*);
typedef DWORD (WINAPI *FnGmfnw)(HMODULE, LPWSTR, DWORD);
static HMODULE g_core = nullptr;
static FnGmfnw g_gmfnwReal = nullptr;
// A rede so aceita quem a chama de dentro de um modulo cujo caminho contem "nvngx.dll" (ver NrInstallCallerSpoof no
// feed): so a pergunta "qual o nome do modulo X?" com X = este exe e respondida com o caminho do _nvngx.dll do driver.
static DWORD WINAPI GmfnwThunk(HMODULE h, LPWSTR b, DWORD n) {
    if (h == GetModuleHandleW(nullptr) && g_core) { const DWORD w = g_gmfnwReal(g_core, b, n); if (w && w < n) return w; }
    return g_gmfnwReal ? g_gmfnwReal(h, b, n) : GetModuleFileNameW(h, b, n);
}
static bool TrocarIat(HMODULE mod) {
    BYTE* base = (BYTE*)mod;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    for (const IMAGE_IMPORT_DESCRIPTOR* imp = (const IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (!imp->OriginalFirstThunk) continue;
        const IMAGE_THUNK_DATA* oft = (const IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA* ft = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; oft->u1.AddressOfData; ++oft, ++ft) {
            if (oft->u1.Ordinal & IMAGE_ORDINAL_FLAG64) continue;
            const IMAGE_IMPORT_BY_NAME* ibn = (const IMAGE_IMPORT_BY_NAME*)(base + oft->u1.AddressOfData);
            if (strcmp((const char*)ibn->Name, "GetModuleFileNameW") != 0) continue;
            void** slot = (void**)&ft->u1.Function;
            DWORD prot = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &prot)) return false;
            g_gmfnwReal = (FnGmfnw)*slot; *slot = (void*)&GmfnwThunk;
            VirtualProtect(slot, sizeof(void*), prot, &prot);
            return true;
        }
    }
    return false;
}
static Ngx::Result RazaoCb(Ngx::Parameter* p) { if (!p) return 0xBAD00005u; Ngx::PorF(p, "DLSSNR.ScalingRatio", 1.0f); return Ngx::kOk; }
static ID3D12Resource* Tex(ID3D12Device* dev, UINT w, UINT h, DXGI_FORMAT f, bool uav) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = h;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = f; rd.SampleDesc.Count = 1;
    rd.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ID3D12Resource* t = nullptr;
    // COMMON: o que o feed entrega a rede tambem nao vem com barreira nossa para ela (a rede faz as dela)
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&t))) return nullptr;
    return t;
}
} // namespace SondaNgx

#define dizer SondaDizer
static int SondaNgxMain(const wchar_t* pastaApp, const wchar_t* arqRes) {
    using namespace SondaNgx;
    g_sondaArq = arqRes;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);   // crash = codigo de saida, sem caixa do WER
    IDXGIFactory1* fab = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fab)) || !fab) return dizer(20, "CreateDXGIFactory1 falhou");
    IDXGIAdapter1* ad = nullptr; wchar_t nome[128] = L"?";
    for (UINT i = 0; ; i++) {
        IDXGIAdapter1* a = nullptr;
        if (fab->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
        if (!ad && d.VendorId == 0x10DE && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { ad = a; wcsncpy(nome, d.Description, 127); nome[127] = 0; }
        else a->Release();
    }
    fab->Release();
    if (!ad) return dizer(20, "nenhuma placa NVIDIA (a rede so roda nelas)");
    ID3D12Device* dev = nullptr;
    HRESULT hr = D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev);
    ad->Release();
    if (FAILED(hr) || !dev) return dizer(21, "D3D12CreateDevice falhou 0x%08X (%ls)", (unsigned)hr, nome);
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* q = nullptr; ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList* li = nullptr; ID3D12Fence* fe = nullptr;
    const UINT W = 640, H = 360;
    ID3D12Resource *cor = nullptr, *sai = nullptr, *mv = nullptr, *prof = nullptr;
    if (FAILED(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q)) || !q ||
        FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al)) || !al ||
        FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&li)) || !li ||
        FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fe)) || !fe ||
        !(cor = Tex(dev, W, H, DXGI_FORMAT_B8G8R8A8_UNORM, false)) || !(sai = Tex(dev, W, H, DXGI_FORMAT_B8G8R8A8_UNORM, true)) ||
        !(mv = Tex(dev, W, H, DXGI_FORMAT_R16G16_FLOAT, false)) || !(prof = Tex(dev, W, H, DXGI_FORMAT_R32_FLOAT, false)))
        return dizer(22, "fila/lista/fence/texturas falharam (removido 0x%08X, %ls)", (unsigned)dev->GetDeviceRemovedReason(), nome);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT64 fv = 0;
    // v2.1: LENTO NAO E QUEBRADO. A 2.0 esperava 2 s pela fence e chamava de falha (30) tanto a placa
    // so lenta quanto o device removido. Agora a espera dura enquanto o device estiver vivo: quem
    // decide "demorou demais" e o TETO do pai (sonda_antes_ms, 60 s de fabrica), que mata este
    // processo e conta como estouro (14). Daqui so sai falso com o device removido de verdade
    // (ou na salvaguarda de 10 min, para um pai que morreu sem matar a sonda).
    auto Enviar = [&]() -> bool {   // fecha, executa e espera; so a fence decide
        li->Close(); ID3D12CommandList* ls[] = { li }; q->ExecuteCommandLists(1, ls);
        q->Signal(fe, ++fv);
        const ULONGLONG iniEnvio = GetTickCount64();
        while (fe->GetCompletedValue() < fv && SUCCEEDED(dev->GetDeviceRemovedReason()) &&
               GetTickCount64() - iniEnvio < 600000) {
            if (ev) { ResetEvent(ev); fe->SetEventOnCompletion(fv, ev); WaitForSingleObject(ev, 500); }
            else Sleep(10);
        }
        const bool ok = fe->GetCompletedValue() >= fv && SUCCEEDED(dev->GetDeviceRemovedReason());
        if (ok) { al->Reset(); li->Reset(al, nullptr); }
        return ok;
    };
    // o NGX do driver (_nvngx.dll pelo registro, como a lib do SDK e o ngx_forward do feed)
    for (const wchar_t* v : { L"FullPath", L"NGXPath" }) {
        wchar_t d[MAX_PATH]{}; DWORD sz = sizeof(d);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", v, RRF_RT_REG_SZ, nullptr, d, &sz) != ERROR_SUCCESS || !d[0]) continue;
        std::wstring p = d; if (p.back() != L'\\') p += L'\\'; p += L"_nvngx.dll";
        if ((g_core = LoadLibraryW(p.c_str())) != nullptr) break;
    }
    if (!g_core) return dizer(23, "o _nvngx.dll do driver nao carregou (%ls)", nome);
    const FnCoreInit coreInit = (FnCoreInit)(void*)GetProcAddress(g_core, "NVSDK_NGX_D3D12_Init");
    const FnAlloc alloc = (FnAlloc)(void*)GetProcAddress(g_core, "NVSDK_NGX_D3D12_AllocateParameters");
    const FnDestroy destroy = (FnDestroy)(void*)GetProcAddress(g_core, "NVSDK_NGX_D3D12_DestroyParameters");
    const FnShutdown coreShut = (FnShutdown)(void*)GetProcAddress(g_core, "NVSDK_NGX_D3D12_Shutdown1");
    if (!coreInit || !alloc || !destroy) return dizer(23, "o _nvngx.dll nao exporta Init/AllocateParameters (%ls)", nome);
    // os arquivos do NGX desta sonda: na pasta do arquivo de resultado (a pasta de dados do programa), nunca no %TEMP%
    std::wstring dados = arqRes ? std::wstring(arqRes) : std::wstring();
    { const size_t b = dados.find_last_of(L"\\/"); dados = b == std::wstring::npos ? std::wstring(L".") : dados.substr(0, b); }
    Ngx::Result r = coreInit(0x1000000ULL, dados.c_str(), dev, nullptr, 0x15u);
    if (Ngx::Falhou(r)) return dizer(24, "Init do NGX do driver 0x%08X (%s) (%ls)", r, Ngx::NomeResultado(r), nome);
    // a rede da pasta do app
    std::wstring pasta = pastaApp ? pastaApp : L"";
    while (!pasta.empty() && (pasta.back() == L'\\' || pasta.back() == L'/')) pasta.pop_back();
    const std::wstring dll = pasta + L"\\nvngx_dlssnr.dll";
    HMODULE rede = LoadLibraryW(dll.c_str());
    if (!rede) return dizer(25, "nvngx_dlssnr.dll nao carregou da pasta do app (erro %lu)", (unsigned long)GetLastError());
    const FnInitExt initExt = (FnInitExt)(void*)GetProcAddress(rede, "NVSDK_NGX_D3D12_Init_Ext");
    const Ngx::FnD3D12CreateFeature criar = (Ngx::FnD3D12CreateFeature)(void*)GetProcAddress(rede, "NVSDK_NGX_D3D12_CreateFeature");
    const Ngx::FnD3D12EvaluateFeature avaliar = (Ngx::FnD3D12EvaluateFeature)(void*)GetProcAddress(rede, "NVSDK_NGX_D3D12_EvaluateFeature");
    const Ngx::FnD3D12ReleaseFeature soltar = (Ngx::FnD3D12ReleaseFeature)(void*)GetProcAddress(rede, "NVSDK_NGX_D3D12_ReleaseFeature");
    const FnShutdown redeShut = (FnShutdown)(void*)GetProcAddress(rede, "NVSDK_NGX_D3D12_Shutdown1");
    if (!initExt || !criar || !avaliar || !soltar) return dizer(25, "nvngx_dlssnr.dll sem um export do ciclo de vida");
    TrocarIat(rede);
    r = initExt(0x0876232CULL, pasta.c_str(), dev, 0x15u, nullptr);
    if (Ngx::Falhou(r)) return dizer(26, "Init_Ext da rede 0x%08X (%s) (%ls)", r, Ngx::NomeResultado(r), nome);
    Ngx::Parameter* p = nullptr;
    r = alloc(&p);
    if (Ngx::Falhou(r) || !p) return dizer(27, "AllocateParameters 0x%08X (%s)", r, Ngx::NomeResultado(r));
    Ngx::PorUI(p, "CreationNodeMask", 1u); Ngx::PorUI(p, "VisibilityNodeMask", 1u);
    Ngx::PorUI(p, Ngx::kWidth, W); Ngx::PorUI(p, Ngx::kHeight, H); Ngx::PorUI(p, Ngx::kOutWidth, W); Ngx::PorUI(p, Ngx::kOutHeight, H);
    Ngx::PorUI(p, "DLSSNR.Width", W); Ngx::PorUI(p, "DLSSNR.Height", H);
    Ngx::PorUI(p, "DLSSNR.Hint.Render.Preset", 1u);
    Ngx::PorI(p, Ngx::kPerfQuality, Ngx::PQ_DLAA);
    Ngx::PorVoid(p, "DLSSNRComputeScalingRatioCallback", (void*)&RazaoCb);
    Ngx::PorF(p, "DLSSNR.ScalingRatio", 1.0f);
    Ngx::Handle* feat = nullptr;
    r = criar(li, 18u /* Reserved18 */, p, &feat);
    if (Ngx::Falhou(r) || !feat) { li->Close(); return dizer(28, "CreateFeature da rede 0x%08X (%s) a %ux%u (%ls)", r, Ngx::NomeResultado(r), W, H, nome); }
    if (!Enviar()) return dizer(30, "o device caiu durante a criacao (removido 0x%08X, %ls)", (unsigned)dev->GetDeviceRemovedReason(), nome);
    // um passe: quadro preto, vetores zero, profundidade zero (os mesmos nomes que o feed escreve)
    Ngx::PorI(p, "DLSSNR.Enabled", 1); Ngx::PorI(p, "DLSSNR.UICorrection", 0);
    Ngx::PorUI(p, "DLSSNR.Style", 0u); Ngx::PorF(p, "DLSSNR.Intensity", 1.0f);
    Ngx::PorF(p, "DLSSNR.LocalToneStrength", 1.0f); Ngx::PorF(p, "DLSSNR.LocalStructureStrength", 1.0f); Ngx::PorF(p, "DLSSNR.SkinStructureStrength", 1.0f);
    Ngx::PorI(p, "DLSSNR.UseAutoMask", 0);
    Ngx::PorF(p, "DLSSNR.MVecScaleX", 1.0f); Ngx::PorF(p, "DLSSNR.MVecScaleY", 1.0f); Ngx::PorI(p, "DLSSNR.DepthInverted", 0);
    Ngx::PorD3D12(p, "DLSSNR.MVec", mv); Ngx::PorD3D12(p, "DLSSNR.Depth", prof);
    Ngx::PorD3D12(p, "DLSSNR.Color", cor); Ngx::PorD3D12(p, "DLSSNR.Output", sai);
    Ngx::PorI(p, "DLSSNR.Reset", 1);
    r = avaliar(li, feat, p, nullptr);
    if (Ngx::Falhou(r)) { li->Close(); return dizer(29, "Evaluate da rede 0x%08X (%s) (%ls)", r, Ngx::NomeResultado(r), nome); }
    if (!Enviar()) return dizer(30, "o device caiu durante o passe (removido 0x%08X, %ls)", (unsigned)dev->GetDeviceRemovedReason(), nome);
    // soltar pelo MESMO modulo que criou (a rede), como o feed faz (NrReleaseGuarded)
    soltar(feat); destroy(p);
    if (redeShut) redeShut(dev);
    if (coreShut) coreShut(dev);
    return dizer(0, "ok: device + NGX + 1 passe da rede a %ux%u (%ls)", W, H, nome);
    // O resto (texturas, fila, device) sai com o processo: ele e descartavel de proposito.
}
#undef dizer

}  // namespace ReinicioAuto
