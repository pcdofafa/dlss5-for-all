// Isola a queda: device por D3D11CoreCreateDevice + swapchain flip, variando placa, janela layered e flag waitable.
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <cstdio>
typedef HRESULT (WINAPI *PFN_Core)(const void*, IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*);
static IDXGIAdapter1* Achar(const wchar_t* parte) {
    IDXGIFactory1* f = nullptr; CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f);
    IDXGIAdapter1* achado = nullptr;
    for (UINT i = 0; !achado; i++) { IDXGIAdapter1* a = nullptr; if (f->EnumAdapters1(i, &a) != S_OK || !a) break;
        DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d); if (wcsstr(d.Description, parte) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) achado = a; else a->Release(); }
    f->Release(); return achado;
}
static LRESULT CALLBACK Wp(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static int Caso(const wchar_t* placa, bool cru, bool layered, bool waitable) {
    printf("[%ls cru=%d layered=%d waitable=%d] ", placa, cru, layered, waitable); fflush(stdout);
    IDXGIAdapter1* ad = Achar(placa); if (!ad) { printf("sem adaptador\n"); return 1; }
    ID3D11Device* dev = nullptr; D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0 }; D3D_FEATURE_LEVEL got{}; HRESULT hr;
    if (cru) {
        PFN_Core core = (PFN_Core)(void*)GetProcAddress(LoadLibraryW(L"d3d11.dll"), "D3D11CoreCreateDevice");
        hr = core(nullptr, ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, lv, 1, D3D11_SDK_VERSION, &dev, &got);
    } else {
        ID3D11DeviceContext* c = nullptr;
        hr = D3D11CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, lv, 1, D3D11_SDK_VERSION, &dev, &got, &c);
        if (c) c->Release();
    }
    printf("device hr=0x%08lX ", hr); fflush(stdout);
    if (FAILED(hr)) { ad->Release(); printf("\n"); return 2; }
    WNDCLASSEXW wc{ sizeof(wc) }; wc.lpfnWndProc = Wp; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"TesteCruWnd"; RegisterClassExW(&wc);
    HWND hw = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | (layered ? WS_EX_LAYERED : 0), wc.lpszClassName, L"teste", WS_POPUP, -5000, -5000, 640, 360, nullptr, nullptr, wc.hInstance, nullptr);
    if (layered) SetLayeredWindowAttributes(hw, 0, 255, LWA_ALPHA);
    ShowWindow(hw, SW_SHOWNOACTIVATE);
    IDXGIFactory2* f2 = nullptr; ad->GetParent(__uuidof(IDXGIFactory2), (void**)&f2);
    DXGI_SWAP_CHAIN_DESC1 d1{}; d1.Width = 640; d1.Height = 360; d1.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d1.SampleDesc.Count = 1;
    d1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d1.BufferCount = 2; d1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; d1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    d1.Flags = waitable ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0;
    IDXGISwapChain1* sc = nullptr;
    hr = f2->CreateSwapChainForHwnd(dev, hw, &d1, nullptr, nullptr, &sc);
    printf("swapchain hr=0x%08lX ", hr); fflush(stdout);
    if (sc) { for (int i = 0; i < 10; i++) sc->Present(0, 0); printf("10 presents ok"); sc->Release(); }
    printf("\n");
    f2->Release(); DestroyWindow(hw); dev->Release(); ad->Release();
    return 0;
}
int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const wchar_t* placa = argc > 1 ? argv[1] : L"Radeon";
    Caso(placa, false, false, false);
    Caso(placa, true,  false, false);
    Caso(placa, true,  false, true);
    Caso(placa, true,  true,  false);
    Caso(placa, true,  true,  true);
    Caso(placa, false, true,  true);
    return 0;
}
