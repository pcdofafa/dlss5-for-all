// teste1_ponte.cpp — prova de conceito: textura escrita em D3D11 na GPU A (NVIDIA)
// chega numa textura D3D11 na GPU B (AMD iGPU) sem passar pela CPU, via
// heap cross-adapter do D3D12 e fences compartilhadas (padrão do sample
// D3D12HeterogeneousMultiadapter da Microsoft).
//
// Compilar:
//   g++ -std=c++17 -O2 -municode teste1_ponte.cpp -o teste1_ponte.exe
//       -ld3d11 -ld3d12 -ldxgi -ldxguid -lole32 -static
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------- utilidades
static double g_qpf = 0;
static double agora_ms() {
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return c.QuadPart * 1000.0 / g_qpf;
}
static void logf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n"); fflush(stdout);
}
#define CHK(hr, oque) do { HRESULT _h = (hr); \
    if (FAILED(_h)) { logf("  FALHOU %s: hr=0x%08lX", oque, (unsigned long)_h); return false; } \
    else logf("  ok     %s", oque); } while (0)

// Padrão verificável: pixel = f(x, y) em BGRA.
static uint32_t padrao(uint32_t x, uint32_t y) {
    uint8_t b = (uint8_t)x, g = (uint8_t)y, r = (uint8_t)((x >> 8) ^ (y >> 8) ^ (x * 7 + y * 13));
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// Espera uma fence D3D12 chegar ao valor (spin curto, resolução de QPC).
static void esperar_fence(ID3D12Fence* f, UINT64 v) {
    while (f->GetCompletedValue() < v) YieldProcessor();
}

// -------------------------------------------------------------- um adaptador
// Tudo que um lado (A ou B) precisa: devices 11 e 12, contexto, fila de cópia.
struct Lado {
    const char* nome = "";
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D11Device5> dev11;
    ComPtr<ID3D11DeviceContext4> ctx11;
    ComPtr<ID3D12Device> dev12;
    ComPtr<ID3D12CommandQueue> fila;     // COPY (ou DIRECT se COPY falhar)
    D3D12_COMMAND_LIST_TYPE tipoFila = D3D12_COMMAND_LIST_TYPE_COPY;
    BOOL rowMajorCross = FALSE;

    bool criar() {
        logf("[%s] criando devices", nome);
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        ComPtr<ID3D11Device> d; ComPtr<ID3D11DeviceContext> c;
        CHK(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                              &fl, 1, D3D11_SDK_VERSION, &d, nullptr, &c), "D3D11CreateDevice");
        CHK(d.As(&dev11), "QI ID3D11Device5");
        CHK(c.As(&ctx11), "QI ID3D11DeviceContext4");
        CHK(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev12)),
            "D3D12CreateDevice FL11_0");
        D3D12_FEATURE_DATA_D3D12_OPTIONS op = {};
        CHK(dev12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &op, sizeof(op)),
            "CheckFeatureSupport D3D12_OPTIONS");
        rowMajorCross = op.CrossAdapterRowMajorTextureSupported;
        logf("  CrossAdapterRowMajorTextureSupported=%d  ResourceHeapTier=%d",
             rowMajorCross, (int)op.ResourceHeapTier);
        return criar_fila(D3D12_COMMAND_LIST_TYPE_COPY);
    }
    bool criar_fila(D3D12_COMMAND_LIST_TYPE t) {
        tipoFila = t;
        D3D12_COMMAND_QUEUE_DESC q = {}; q.Type = t;
        fila.Reset();
        CHK(dev12->CreateCommandQueue(&q, IID_PPV_ARGS(&fila)),
            t == D3D12_COMMAND_LIST_TYPE_COPY ? "CreateCommandQueue COPY" : "CreateCommandQueue DIRECT");
        return true;
    }
};

// Lista de comandos gravada uma vez (conteúdo fixo) e reexecutada.
struct ListaFixa {
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> lista;
    bool iniciar(ID3D12Device* d, D3D12_COMMAND_LIST_TYPE t) {
        if (FAILED(d->CreateCommandAllocator(t, IID_PPV_ARGS(&alloc)))) return false;
        if (FAILED(d->CreateCommandList(0, t, alloc.Get(), nullptr, IID_PPV_ARGS(&lista)))) return false;
        return true;
    }
    void executar(ID3D12CommandQueue* q) {
        ID3D12CommandList* l[] = { lista.Get() };
        q->ExecuteCommandLists(1, l);
    }
};

// ---------------------------------------------------------- recursos da ponte
enum Caminho { CAMINHO_TEXTURA_ROW_MAJOR = 0, CAMINHO_BUFFER = 1 };
static const int ANEL = 3;   // recursos cross-adapter em voo

struct Ponte {
    Lado *A, *B;
    UINT W, H;
    Caminho caminho;

    // Lado A
    ComPtr<ID3D11Texture2D> texA11, srcA11;     // srcA11 = "quadro do jogo", texA11 = compartilhada
    ComPtr<ID3D12Resource> texA12;
    ComPtr<ID3D12Heap> heapA;
    ComPtr<ID3D12Resource> crossA[ANEL];
    ComPtr<ID3D12Fence> fA12;   // A11 -> A12 (SHARED)
    ComPtr<ID3D11Fence> fA11;
    ComPtr<ID3D12Fence> fX12A;  // A12 -> B12 (SHARED | CROSS_ADAPTER)
    ListaFixa cmdA[ANEL];

    // Lado B
    ComPtr<ID3D12Heap> heapB;
    ComPtr<ID3D12Resource> crossB[ANEL];
    ComPtr<ID3D12Fence> fX12B;
    ComPtr<ID3D12Resource> texB12;
    ComPtr<ID3D11Texture2D> texB11, stagingB11;
    ComPtr<ID3D12Fence> fB12;   // B12 -> B11 (SHARED)
    ComPtr<ID3D11Fence> fB11;
    ListaFixa cmdB[ANEL];

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 tamFootprint = 0, passoAnel = 0;
    UINT64 n = 0;   // número do quadro corrente (valor das fences)

    // ---- A11: textura fonte + textura compartilhada -----------------------
    bool criar_lado_A11() {
        logf("[A11] texturas %ux%u BGRA8", W, H);
        std::vector<uint32_t> px((size_t)W * H);
        for (UINT y = 0; y < H; y++) for (UINT x = 0; x < W; x++) px[(size_t)y * W + x] = padrao(x, y);
        D3D11_SUBRESOURCE_DATA sd = { px.data(), W * 4, 0 };

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        CHK(A->dev11->CreateTexture2D(&td, &sd, &srcA11), "CreateTexture2D srcA11 (com padrão)");

        // Tenta as combinações de MiscFlags que permitem abrir no D3D12.
        struct { UINT misc; const char* nome; } tent[] = {
            { D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED, "NTHANDLE|SHARED" },
            { D3D11_RESOURCE_MISC_SHARED_NTHANDLE, "NTHANDLE" },
            { D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX, "NTHANDLE|KEYEDMUTEX" },
        };
        for (auto& t : tent) {
            td.MiscFlags = t.misc;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            HRESULT hr = A->dev11->CreateTexture2D(&td, nullptr, &texA11);
            logf("  CreateTexture2D texA11 MiscFlags=%s: hr=0x%08lX", t.nome, (unsigned long)hr);
            if (SUCCEEDED(hr)) break;
        }
        if (!texA11) return false;
        // Primeiro quadro: padrão via UpdateSubresource, como pedido.
        A->ctx11->UpdateSubresource(texA11.Get(), 0, nullptr, px.data(), W * 4, 0);
        A->ctx11->Flush();

        ComPtr<IDXGIResource1> r1; CHK(texA11.As(&r1), "QI IDXGIResource1 texA11");
        HANDLE h = nullptr;
        CHK(r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h),
            "CreateSharedHandle texA11");
        HRESULT hr = A->dev12->OpenSharedHandle(h, IID_PPV_ARGS(&texA12));
        CloseHandle(h);
        CHK(hr, "A12 OpenSharedHandle texA11 -> ID3D12Resource");
        return true;
    }

    // ---- heap cross-adapter em A, aberta em B -----------------------------
    D3D12_RESOURCE_DESC desc_cross() {
        D3D12_RESOURCE_DESC rd = {};
        if (caminho == CAMINHO_TEXTURA_ROW_MAJOR) {
            rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            rd.Width = W; rd.Height = H; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
            rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
        } else {
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = tamFootprint; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
            rd.Format = DXGI_FORMAT_UNKNOWN; rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
        }
        return rd;
    }
    bool criar_heap_cross() {
        // Footprint da textura (usado no caminho buffer e para conferir o pitch).
        D3D12_RESOURCE_DESC tdesc = texA12->GetDesc();
        UINT nlin = 0; UINT64 tamLin = 0;
        A->dev12->GetCopyableFootprints(&tdesc, 0, 1, 0, &footprint, &nlin, &tamLin, &tamFootprint);
        logf("[A12] footprint: RowPitch=%u total=%llu bytes", footprint.Footprint.RowPitch,
             (unsigned long long)tamFootprint);

        D3D12_RESOURCE_DESC rd = desc_cross();
        D3D12_RESOURCE_ALLOCATION_INFO ai = A->dev12->GetResourceAllocationInfo(0, 1, &rd);
        if (ai.SizeInBytes == UINT64(-1)) { logf("  FALHOU GetResourceAllocationInfo (desc inválida)"); return false; }
        passoAnel = (ai.SizeInBytes + 0xFFFF) & ~UINT64(0xFFFF);
        logf("[A12] alocação cross: %llu bytes, alinhamento %llu, anel de %d -> heap de %llu bytes",
             (unsigned long long)ai.SizeInBytes, (unsigned long long)ai.Alignment, ANEL,
             (unsigned long long)(passoAnel * ANEL));

        D3D12_HEAP_DESC hd = {};
        hd.SizeInBytes = passoAnel * ANEL;
        hd.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        hd.Flags = D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
        HRESULT hr = A->dev12->CreateHeap(&hd, IID_PPV_ARGS(&heapA));
        logf("  CreateHeap SHARED|SHARED_CROSS_ADAPTER (sem deny): hr=0x%08lX", (unsigned long)hr);
        if (FAILED(hr)) {
            hd.Flags |= (caminho == CAMINHO_BUFFER) ? D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS
                                                    : D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
            CHK(A->dev12->CreateHeap(&hd, IID_PPV_ARGS(&heapA)), "CreateHeap com ALLOW_ONLY_*");
        }
        for (int i = 0; i < ANEL; i++)
            CHK(A->dev12->CreatePlacedResource(heapA.Get(), passoAnel * i, &rd, D3D12_RESOURCE_STATE_COMMON,
                                               nullptr, IID_PPV_ARGS(&crossA[i])), "A12 CreatePlacedResource cross");

        HANDLE h = nullptr;
        CHK(A->dev12->CreateSharedHandle(heapA.Get(), nullptr, GENERIC_ALL, nullptr, &h), "A12 CreateSharedHandle heap");
        hr = B->dev12->OpenSharedHandle(h, IID_PPV_ARGS(&heapB));
        CloseHandle(h);
        CHK(hr, "B12 OpenSharedHandle heap");
        for (int i = 0; i < ANEL; i++)
            CHK(B->dev12->CreatePlacedResource(heapB.Get(), passoAnel * i, &rd, D3D12_RESOURCE_STATE_COMMON,
                                               nullptr, IID_PPV_ARGS(&crossB[i])), "B12 CreatePlacedResource cross");
        return true;
    }

    // ---- fences -----------------------------------------------------------
    bool criar_fences() {
        logf("[fences]");
        HANDLE h = nullptr; HRESULT hr;
        CHK(A->dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fA12)), "A12 CreateFence fA SHARED");
        CHK(A->dev12->CreateSharedHandle(fA12.Get(), nullptr, GENERIC_ALL, nullptr, &h), "A12 CreateSharedHandle fA");
        hr = A->dev11->OpenSharedFence(h, IID_PPV_ARGS(&fA11)); CloseHandle(h);
        CHK(hr, "A11 OpenSharedFence fA -> ID3D11Fence");

        CHK(A->dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED | D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER,
                                  IID_PPV_ARGS(&fX12A)), "A12 CreateFence fX SHARED|CROSS_ADAPTER");
        CHK(A->dev12->CreateSharedHandle(fX12A.Get(), nullptr, GENERIC_ALL, nullptr, &h), "A12 CreateSharedHandle fX");
        hr = B->dev12->OpenSharedHandle(h, IID_PPV_ARGS(&fX12B)); CloseHandle(h);
        CHK(hr, "B12 OpenSharedHandle fX");

        CHK(B->dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fB12)), "B12 CreateFence fB SHARED");
        CHK(B->dev12->CreateSharedHandle(fB12.Get(), nullptr, GENERIC_ALL, nullptr, &h), "B12 CreateSharedHandle fB");
        hr = B->dev11->OpenSharedFence(h, IID_PPV_ARGS(&fB11)); CloseHandle(h);
        CHK(hr, "B11 OpenSharedFence fB -> ID3D11Fence");
        return true;
    }

    // ---- textura destino em B (B12 <-> B11) ---------------------------------
    bool criar_lado_B() {
        logf("[B] textura destino %ux%u", W, H);
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = W; rd.Height = H; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        // Rota 1: criar no B12 (HEAP_FLAG_SHARED) e abrir no B11.
        struct { D3D12_RESOURCE_FLAGS f; const char* nome; } tent[] = {
            { D3D12_RESOURCE_FLAG_NONE, "NONE" },
            { D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, "ALLOW_SIMULTANEOUS_ACCESS" },
            { D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, "ALLOW_RENDER_TARGET" },
        };
        for (auto& t : tent) {
            rd.Flags = t.f;
            HRESULT hr = B->dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texB12));
            logf("  B12 CreateCommittedResource SHARED flags=%s: hr=0x%08lX", t.nome, (unsigned long)hr);
            if (FAILED(hr)) continue;
            HANDLE h = nullptr;
            hr = B->dev12->CreateSharedHandle(texB12.Get(), nullptr, GENERIC_ALL, nullptr, &h);
            logf("  B12 CreateSharedHandle texB12: hr=0x%08lX", (unsigned long)hr);
            if (FAILED(hr)) { texB12.Reset(); continue; }
            ComPtr<ID3D11Device1> d1; B->dev11.As(&d1);
            hr = d1->OpenSharedResource1(h, IID_PPV_ARGS(&texB11)); CloseHandle(h);
            logf("  B11 OpenSharedResource1 texB12 -> ID3D11Texture2D: hr=0x%08lX", (unsigned long)hr);
            if (SUCCEEDED(hr)) { logf("  rota B12->B11 OK (flags=%s)", t.nome); break; }
            texB12.Reset();
        }
        // Rota 2: criar no B11 (NTHANDLE) e abrir no B12.
        if (!texB11) {
            logf("  tentando rota B11 (NTHANDLE) -> B12");
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
            CHK(B->dev11->CreateTexture2D(&td, nullptr, &texB11), "B11 CreateTexture2D NTHANDLE|SHARED");
            ComPtr<IDXGIResource1> r1; texB11.As(&r1);
            HANDLE h = nullptr;
            CHK(r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h),
                "B11 CreateSharedHandle");
            HRESULT hr = B->dev12->OpenSharedHandle(h, IID_PPV_ARGS(&texB12)); CloseHandle(h);
            CHK(hr, "B12 OpenSharedHandle -> ID3D12Resource");
        }
        D3D11_TEXTURE2D_DESC st = {};
        st.Width = W; st.Height = H; st.MipLevels = 1; st.ArraySize = 1;
        st.Format = DXGI_FORMAT_B8G8R8A8_UNORM; st.SampleDesc.Count = 1;
        st.Usage = D3D11_USAGE_STAGING; st.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        CHK(B->dev11->CreateTexture2D(&st, nullptr, &stagingB11), "B11 CreateTexture2D staging");
        return true;
    }

    // ---- listas de cópia fixas (uma por posição do anel) -------------------
    void gravar_copia(ID3D12GraphicsCommandList* l, ID3D12Resource* tex, ID3D12Resource* cross, bool texParaCross) {
        D3D12_TEXTURE_COPY_LOCATION lt = {}, lc = {};
        lt.pResource = tex; lt.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; lt.SubresourceIndex = 0;
        lc.pResource = cross;
        if (caminho == CAMINHO_TEXTURA_ROW_MAJOR) {
            lc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; lc.SubresourceIndex = 0;
        } else {
            lc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; lc.PlacedFootprint = footprint;
        }
        if (texParaCross) l->CopyTextureRegion(&lc, 0, 0, 0, &lt, nullptr);
        else              l->CopyTextureRegion(&lt, 0, 0, 0, &lc, nullptr);
        l->Close();
    }
    bool gravar_listas() {
        for (int i = 0; i < ANEL; i++) {
            if (!cmdA[i].iniciar(A->dev12.Get(), A->tipoFila)) { logf("  FALHOU lista A%d", i); return false; }
            gravar_copia(cmdA[i].lista.Get(), texA12.Get(), crossA[i].Get(), true);
            if (!cmdB[i].iniciar(B->dev12.Get(), B->tipoFila)) { logf("  FALHOU lista B%d", i); return false; }
            gravar_copia(cmdB[i].lista.Get(), texB12.Get(), crossB[i].Get(), false);
        }
        logf("  ok     listas de cópia gravadas (%d por lado)", ANEL);
        return true;
    }

    bool criar() {
        return criar_lado_A11() && criar_heap_cross() && criar_fences() && criar_lado_B() && gravar_listas();
    }

    // ---- um quadro da cadeia (sem esperar na CPU) -------------------------
    // A11 escreve -> Signal(fA) -> A12 Wait(fA) copia tex->cross Signal(fX)
    // -> B12 Wait(fX) copia cross->texB Signal(fB) -> B11 Wait(fB).
    void quadro(bool escreverA) {
        n++;
        int i = (int)((n - 1) % ANEL);
        if (escreverA) A->ctx11->CopyResource(texA11.Get(), srcA11.Get());  // "o jogo desenhou"
        A->ctx11->Signal(fA11.Get(), n);
        A->ctx11->Flush();
        A->fila->Wait(fA12.Get(), n);
        cmdA[i].executar(A->fila.Get());
        A->fila->Signal(fX12A.Get(), n);
        B->fila->Wait(fX12B.Get(), n);
        cmdB[i].executar(B->fila.Get());
        B->fila->Signal(fB12.Get(), n);
        B->ctx11->Wait(fB11.Get(), n);
    }

    // ---- verificação pixel a pixel no B11 --------------------------------
    bool verificar() {
        B->ctx11->CopyResource(stagingB11.Get(), texB11.Get());
        D3D11_MAPPED_SUBRESOURCE m = {};
        HRESULT hr = B->ctx11->Map(stagingB11.Get(), 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) { logf("  FALHOU Map staging B11: hr=0x%08lX", (unsigned long)hr); return false; }
        size_t erros = 0; uint32_t primeiroX = 0, primeiroY = 0, visto = 0, esperado = 0;
        for (UINT y = 0; y < H; y++) {
            const uint32_t* lin = (const uint32_t*)((const uint8_t*)m.pData + (size_t)y * m.RowPitch);
            for (UINT x = 0; x < W; x++) {
                uint32_t e = padrao(x, y);
                if (lin[x] != e) { if (!erros) { primeiroX = x; primeiroY = y; visto = lin[x]; esperado = e; } erros++; }
            }
        }
        B->ctx11->Unmap(stagingB11.Get(), 0);
        if (erros) logf("  VERIFICAÇÃO FALHOU: %zu pixels errados; primeiro em (%u,%u) visto=%08X esperado=%08X",
                        erros, primeiroX, primeiroY, visto, esperado);
        else logf("  VERIFICAÇÃO OK: %u pixels iguais ao padrão", W * H);
        return erros == 0;
    }

    // ---- medições ---------------------------------------------------------
    // Etapas isoladas: espera cada fence na CPU e cronometra.
    void medir_etapas(int quadros) {
        double sA = 0, sX = 0, sB = 0;
        for (int k = 0; k < quadros; k++) {
            double t0 = agora_ms();
            quadro(true);
            esperar_fence(fA12.Get(), n);  double t1 = agora_ms();
            esperar_fence(fX12A.Get(), n); double t2 = agora_ms();
            esperar_fence(fB12.Get(), n);  double t3 = agora_ms();
            if (k >= 5) { sA += t1 - t0; sX += t2 - t1; sB += t3 - t2; }  // descarta aquecimento
        }
        double d = quadros - 5;
        logf("  etapas (média de %d quadros, ms): A11 escreve+signal %.3f | A12 cópia tex->cross %.3f | "
             "B12 cópia cross->texB %.3f | total %.3f", quadros - 5, sA / d, sX / d, sB / d, (sA + sX + sB) / d);
    }
    // Laço encadeado com anel: só espera a posição do anel ficar livre.
    double medir_pipeline(int quadros) {
        double t0 = agora_ms();
        for (int k = 0; k < quadros; k++) {
            if (n >= ANEL) esperar_fence(fB12.Get(), n + 1 - ANEL);
            quadro(true);
        }
        esperar_fence(fB12.Get(), n);
        double dt = agora_ms() - t0;
        double fps = quadros * 1000.0 / dt;
        logf("  pipeline: %d quadros em %.1f ms -> %.1f quadros/s (%.3f ms/quadro)", quadros, dt, fps, dt / quadros);
        return fps;
    }
};

// -------------------------------------------- alternativa CPU (só comparação)
static double medir_cpu(Lado& A, Lado& B, UINT W, UINT H, int quadros) {
    logf("[CPU] A11 staging -> Map -> memcpy -> B11 DYNAMIC (WRITE_DISCARD) -> CopyResource, %ux%u", W, H);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> srcA, dstB, dynB, stgA;
    A.dev11->CreateTexture2D(&td, nullptr, &srcA);
    B.dev11->CreateTexture2D(&td, nullptr, &dstB);
    td.Usage = D3D11_USAGE_DYNAMIC; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    B.dev11->CreateTexture2D(&td, nullptr, &dynB);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    A.dev11->CreateTexture2D(&td, nullptr, &stgA);
    if (!srcA || !dstB || !dynB || !stgA) { logf("  FALHOU criar texturas da via CPU"); return 0; }

    double t0 = agora_ms();
    for (int k = 0; k < quadros; k++) {
        A.ctx11->CopyResource(stgA.Get(), srcA.Get());
        D3D11_MAPPED_SUBRESOURCE ma = {}, mb = {};
        if (FAILED(A.ctx11->Map(stgA.Get(), 0, D3D11_MAP_READ, 0, &ma))) { logf("  FALHOU Map A"); return 0; }
        if (FAILED(B.ctx11->Map(dynB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mb))) { logf("  FALHOU Map B"); return 0; }
        for (UINT y = 0; y < H; y++)
            memcpy((uint8_t*)mb.pData + (size_t)y * mb.RowPitch, (uint8_t*)ma.pData + (size_t)y * ma.RowPitch, W * 4);
        B.ctx11->Unmap(dynB.Get(), 0);
        A.ctx11->Unmap(stgA.Get(), 0);
        B.ctx11->CopyResource(dstB.Get(), dynB.Get());
        B.ctx11->Flush();
    }
    // Espera o B terminar de verdade.
    B.ctx11->CopyResource(dynB.Get(), dstB.Get());
    double dt = agora_ms() - t0;
    logf("  via CPU: %d quadros em %.1f ms -> %.1f quadros/s (%.3f ms/quadro)", quadros, dt,
         quadros * 1000.0 / dt, dt / quadros);
    return quadros * 1000.0 / dt;
}

// ------------------------------------------------------------- enumeração
static bool enumerar(Lado& A, Lado& B) {
    ComPtr<IDXGIFactory6> f;
    CHK(CreateDXGIFactory2(0, IID_PPV_ARGS(&f)), "CreateDXGIFactory2 -> IDXGIFactory6");
    for (UINT i = 0;; i++) {
        ComPtr<IDXGIAdapter1> ad;
        if (f->EnumAdapters1(i, &ad) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d; ad->GetDesc1(&d);
        logf("  adaptador %u: %ls  vendor=0x%04X  LUID=%08lX:%08lX  VRAM=%llu MB%s", i, d.Description, d.VendorId,
             (unsigned long)d.AdapterLuid.HighPart, (unsigned long)d.AdapterLuid.LowPart,
             (unsigned long long)(d.DedicatedVideoMemory >> 20),
             (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " (software)" : "");
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (d.VendorId == 0x10DE && !A.adapter) A.adapter = ad;
        if (d.VendorId == 0x1002 && !B.adapter) B.adapter = ad;
    }
    if (!A.adapter || !B.adapter) { logf("  FALHOU: não achei NVIDIA (A) e AMD (B)"); return false; }
    return true;
}

struct Resultado { UINT W, H; const char* caminho; bool ok; double fps; };

static void rodar_caminho(Lado& A, Lado& B, UINT W, UINT H, Caminho c, std::vector<Resultado>& res) {
    const char* nome = c == CAMINHO_TEXTURA_ROW_MAJOR ? "(a) textura ROW_MAJOR cross-adapter" : "(b) buffer + PLACED_FOOTPRINT";
    logf("");
    logf("=============== %ux%u — caminho %s ===============", W, H, nome);
    Ponte p; p.A = &A; p.B = &B; p.W = W; p.H = H; p.caminho = c;
    Resultado r = { W, H, nome, false, 0 };
    if (p.criar()) {
        logf("[quadro 1] cadeia completa + verificação");
        p.quadro(false);                         // usa o conteúdo do UpdateSubresource
        esperar_fence(p.fB12.Get(), p.n);
        bool ok1 = p.verificar();
        p.quadro(true);                          // agora via CopyResource (GPU) do srcA11
        esperar_fence(p.fB12.Get(), p.n);
        bool ok2 = p.verificar();
        r.ok = ok1 && ok2;
        p.medir_etapas(40);
        r.fps = p.medir_pipeline(300);
        // Confere que o pipeline não corrompeu nada.
        p.verificar();
        // Garante que as GPUs terminaram antes de destruir recursos.
        esperar_fence(p.fB12.Get(), p.n);
        A.ctx11->Flush(); B.ctx11->Flush();
    }
    res.push_back(r);
}

int wmain(int argc, wchar_t** argv) {
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpf = (double)f.QuadPart;
    logf("=== TESTE 1: ponte GPU->GPU sem CPU (NVIDIA -> AMD) ===");
    Lado A, B; A.nome = "A/NVIDIA"; B.nome = "B/AMD";
    if (!enumerar(A, B)) return 1;
    if (!A.criar() || !B.criar()) return 1;
    // "teste1_ponte.exe W H direct" força filas DIRECT em vez de COPY.
    if (argc > 3 && wcscmp(argv[3], L"direct") == 0) {
        if (!A.criar_fila(D3D12_COMMAND_LIST_TYPE_DIRECT) || !B.criar_fila(D3D12_COMMAND_LIST_TYPE_DIRECT)) return 1;
    }

    bool rowMajor = A.rowMajorCross && B.rowMajorCross;
    logf("Caminho (a) textura row-major cross-adapter: %s", rowMajor ? "suportado nos dois" : "NÃO suportado em pelo menos um");

    std::vector<Resultado> res;
    std::vector<std::pair<UINT, UINT>> tam = { {3840, 2160}, {2560, 1440} };
    if (argc > 2) tam = { { (UINT)_wtoi(argv[1]), (UINT)_wtoi(argv[2]) } };
    std::vector<double> fpsCpu;
    for (auto& t : tam) {
        if (rowMajor) rodar_caminho(A, B, t.first, t.second, CAMINHO_TEXTURA_ROW_MAJOR, res);
        rodar_caminho(A, B, t.first, t.second, CAMINHO_BUFFER, res);
        logf("");
        fpsCpu.push_back(medir_cpu(A, B, t.first, t.second, 100));
    }

    logf("");
    logf("=============== RESUMO ===============");
    logf("%-10s | %-38s | %-10s | %s", "resolução", "caminho", "verifica", "quadros/s sustentados");
    for (auto& r : res)
        logf("%4ux%-5u | %-38s | %-10s | %.1f", r.W, r.H, r.caminho, r.ok ? "OK" : "FALHOU", r.fps);
    for (size_t i = 0; i < tam.size(); i++)
        logf("%4ux%-5u | %-38s | %-10s | %.1f", tam[i].first, tam[i].second, "via CPU (staging+memcpy+dynamic)", "-", fpsCpu[i]);
    logf("fila A: %s  fila B: %s", A.tipoFila == D3D12_COMMAND_LIST_TYPE_COPY ? "COPY" : "DIRECT",
         B.tipoFila == D3D12_COMMAND_LIST_TYPE_COPY ? "COPY" : "DIRECT");
    return 0;
}
