// teste3_reshade.cpp — com o dxgi.dll do ReShade ao lado do exe (sem add-ons),
// cria quatro "devices" diferentes e observa no ReShade.log quais ganham
// runtime/proxy:
//   (a) D3D11CreateDevice + CreateSwapChainForHwnd
//   (b) D3D11CoreCreateDevice (GetProcAddress em d3d11.dll) + CreateSwapChainForHwnd
//   (c) D3D12CreateDevice + fila + CreateSwapChainForHwnd
//   (d) IDCompositionDevice (DCompositionCreateDevice2 a partir do device de (a))
//       + IDCompositionSurface 256x256 BeginDraw/EndDraw/Commit, sem swapchain.
// 30 Presents/Commits em cada caso. Janelas sempre ocultas.
//
// Compilar:
//   g++ -std=c++17 -O2 -municode teste3_reshade.cpp -o teste3_reshade.exe
//       -ld3d11 -ld3d12 -ldxgi -ldxguid -lole32 -static
#include <windows.h>
#include <d3d11_1.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <cstdio>

using Microsoft::WRL::ComPtr;

static void logf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n"); fflush(stdout);
}
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static void bombear() {
    MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}
static HWND janela_oculta(const wchar_t* titulo) {
    static bool registrada = false;
    if (!registrada) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"Teste3ReShade"; RegisterClassW(&wc); registrada = true;
    }
    // Criada sem WS_VISIBLE: nunca aparece.
    return CreateWindowExW(0, L"Teste3ReShade", titulo, WS_OVERLAPPEDWINDOW, 0, 0, 640, 480,
                           nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

// Assinatura real no Win11 26200 (deduzida do desmonte do d3d11.dll e confirmada em probe_core.cpp):
// o 1º parâmetro NÃO é IDXGIFactory — é um ponteiro opcional para uma struct interna (null funciona);
// o SDKVersion não é validado.
typedef HRESULT (WINAPI* PFN_D3D11CoreCreateDevice)(const void* pArgsInternos, IDXGIAdapter*, D3D_DRIVER_TYPE,
                                                     HMODULE, UINT Flags, const D3D_FEATURE_LEVEL*, UINT,
                                                     UINT SDKVersion, ID3D11Device**, D3D_FEATURE_LEVEL*);
typedef HRESULT (WINAPI* PFN_DCompositionCreateDevice2)(IUnknown*, REFIID, void**);

static ComPtr<IDXGIFactory2> g_factory;

static ComPtr<IDXGISwapChain1> swapchain(IUnknown* devOuFila, HWND hwnd, const char* caso) {
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = 640; sd.Height = 480; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> sc;
    HRESULT hr = g_factory->CreateSwapChainForHwnd(devOuFila, hwnd, &sd, nullptr, nullptr, &sc);
    logf("  [%s] CreateSwapChainForHwnd: hr=0x%08lX", caso, (unsigned long)hr);
    return sc;
}
static void presents(IDXGISwapChain1* sc, const char* caso) {
    int ruins = 0; HRESULT ultimo = S_OK;
    for (int i = 0; i < 30; i++) { bombear(); HRESULT hr = sc->Present(0, 0); if (hr != S_OK) { ruins++; ultimo = hr; } }
    logf("  [%s] 30 Presents: %d != S_OK (último 0x%08lX)", caso, ruins, (unsigned long)ultimo);
}

// (a) caminho normal.
static ComPtr<ID3D11Device> caso_a() {
    logf("(a) D3D11CreateDevice + swapchain");
    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                   &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    logf("  [a] D3D11CreateDevice: hr=0x%08lX", (unsigned long)hr);
    if (FAILED(hr)) return nullptr;
    HWND h = janela_oculta(L"Teste3 a");
    ComPtr<IDXGISwapChain1> sc = swapchain(dev.Get(), h, "a");
    if (sc) presents(sc.Get(), "a");
    sc.Reset(); DestroyWindow(h);
    return dev;
}

// (b) D3D11CoreCreateDevice, o export interno usado pelo D3D11On12.
static void caso_b() {
    logf("(b) D3D11CoreCreateDevice + swapchain");
    HMODULE m = LoadLibraryW(L"d3d11.dll");
    auto fn = (PFN_D3D11CoreCreateDevice)GetProcAddress(m, "D3D11CoreCreateDevice");
    if (!fn) { logf("  [b] D3D11CoreCreateDevice não exportado"); return; }
    ComPtr<IDXGIAdapter1> ad; g_factory->EnumAdapters1(0, &ad);   // adaptador 0 = NVIDIA (ver Teste 1)
    ComPtr<ID3D11Device> dev;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, obtido = (D3D_FEATURE_LEVEL)0;
    HRESULT hr = fn(nullptr, ad.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                    &fl, 1, D3D11_SDK_VERSION, &dev, &obtido);
    logf("  [b] D3D11CoreCreateDevice: hr=0x%08lX", (unsigned long)hr);
    if (FAILED(hr)) return;
    HWND h = janela_oculta(L"Teste3 b");
    ComPtr<IDXGISwapChain1> sc = swapchain(dev.Get(), h, "b");
    if (sc) presents(sc.Get(), "b");
    sc.Reset(); DestroyWindow(h);
}

// (c) D3D12 com fila DIRECT.
static void caso_c() {
    logf("(c) D3D12CreateDevice + fila + swapchain");
    ComPtr<ID3D12Device> dev;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev));
    logf("  [c] D3D12CreateDevice: hr=0x%08lX", (unsigned long)hr);
    if (FAILED(hr)) return;
    D3D12_COMMAND_QUEUE_DESC q = {}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> fila;
    hr = dev->CreateCommandQueue(&q, IID_PPV_ARGS(&fila));
    logf("  [c] CreateCommandQueue: hr=0x%08lX", (unsigned long)hr);
    HWND h = janela_oculta(L"Teste3 c");
    ComPtr<IDXGISwapChain1> sc = swapchain(fila.Get(), h, "c");
    if (sc) presents(sc.Get(), "c");
    sc.Reset(); DestroyWindow(h);
}

// (d) DirectComposition sem swapchain: superfície 256x256 desenhada pelo D3D11.
static void caso_d(ID3D11Device* dev11) {
    logf("(d) DCompositionCreateDevice2 + IDCompositionSurface 256x256 (sem swapchain)");
    HMODULE m = LoadLibraryW(L"dcomp.dll");
    auto fn = (PFN_DCompositionCreateDevice2)GetProcAddress(m, "DCompositionCreateDevice2");
    if (!fn) { logf("  [d] DCompositionCreateDevice2 não exportado"); return; }
    ComPtr<IDCompositionDevice> dc;
    HRESULT hr = fn(dev11, __uuidof(IDCompositionDevice), (void**)dc.GetAddressOf());
    logf("  [d] DCompositionCreateDevice2: hr=0x%08lX", (unsigned long)hr);
    if (FAILED(hr)) return;
    ComPtr<IDCompositionSurface> sup;
    hr = dc->CreateSurface(256, 256, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &sup);
    logf("  [d] CreateSurface: hr=0x%08lX", (unsigned long)hr);
    if (FAILED(hr)) return;
    ComPtr<ID3D11DeviceContext> ctx; dev11->GetImmediateContext(&ctx);
    int ruins = 0;
    for (int i = 0; i < 30; i++) {
        ComPtr<ID3D11Texture2D> tex; POINT off = {};
        hr = sup->BeginDraw(nullptr, IID_PPV_ARGS(&tex), &off);
        if (FAILED(hr)) { ruins++; logf("  [d] BeginDraw: hr=0x%08lX", (unsigned long)hr); break; }
        ComPtr<ID3D11RenderTargetView> rtv;
        dev11->CreateRenderTargetView(tex.Get(), nullptr, &rtv);
        float cor[4] = { 0.2f, 0.4f, 0.8f, 1.0f };
        if (rtv) ctx->ClearRenderTargetView(rtv.Get(), cor);
        sup->EndDraw();
        hr = dc->Commit();
        if (hr != S_OK) ruins++;
    }
    logf("  [d] 30 BeginDraw/EndDraw/Commit: %d problemas", ruins);
}

int wmain() {
    logf("=== TESTE 3: ReShade (dxgi.dll ao lado, sem add-ons) e os quatro tipos de device ===");
    wchar_t caminho[MAX_PATH] = L"";
    HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
    if (dxgi) GetModuleFileNameW(dxgi, caminho, MAX_PATH);
    logf("dxgi.dll carregado de: %ls", caminho);
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&g_factory));
    logf("CreateDXGIFactory2: hr=0x%08lX", (unsigned long)hr);
    if (FAILED(hr)) return 1;

    ComPtr<ID3D11Device> devA = caso_a();
    caso_b();
    caso_c();
    if (devA) caso_d(devA.Get());
    logf("fim");
    return 0;
}
