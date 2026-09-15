// Swapchain flip sem DXGI_USAGE_RENDER_TARGET_OUTPUT: o DXGI aceita? e o ReShade pula?
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <cstdio>
static IDXGIAdapter1* Achar(const wchar_t* parte) {
    IDXGIFactory1* f = nullptr; CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f);
    IDXGIAdapter1* achado = nullptr;
    for (UINT i = 0; !achado; i++) { IDXGIAdapter1* a = nullptr; if (f->EnumAdapters1(i, &a) != S_OK || !a) break;
        DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d); if (wcsstr(d.Description, parte) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) achado = a; else a->Release(); }
    f->Release(); return achado;
}
static LRESULT CALLBACK Wp(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static void Caso(const wchar_t* placa, UINT uso, bool waitable) {
    printf("[%ls uso=0x%X waitable=%d] ", placa, uso, waitable);
    IDXGIAdapter1* ad = Achar(placa);
    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, lv, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    if (FAILED(hr)) { printf("device 0x%08lX\n", hr); return; }
    WNDCLASSEXW wc{ sizeof(wc) }; wc.lpfnWndProc = Wp; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"TesteUsoWnd"; RegisterClassExW(&wc);
    HWND hw = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName, L"teste", WS_POPUP, -5000, -5000, 640, 360, nullptr, nullptr, wc.hInstance, nullptr);
    SetLayeredWindowAttributes(hw, 0, 255, LWA_ALPHA); ShowWindow(hw, SW_SHOWNOACTIVATE);
    IDXGIFactory2* f2 = nullptr; ad->GetParent(__uuidof(IDXGIFactory2), (void**)&f2);
    DXGI_SWAP_CHAIN_DESC1 d1{}; d1.Width = 640; d1.Height = 360; d1.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d1.SampleDesc.Count = 1;
    d1.BufferUsage = uso; d1.BufferCount = 2; d1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; d1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    d1.Flags = waitable ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0;
    IDXGISwapChain1* sc = nullptr;
    hr = f2->CreateSwapChainForHwnd(dev, hw, &d1, nullptr, nullptr, &sc);
    printf("swapchain hr=0x%08lX ", hr);
    if (sc) {
        // copia de uma textura para o backbuffer (o que o apresentador faz)
        D3D11_TEXTURE2D_DESC td{}; td.Width = 640; td.Height = 360; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* t = nullptr; dev->CreateTexture2D(&td, nullptr, &t);
        ID3D11Texture2D* bb = nullptr; sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
        for (int i = 0; i < 10; i++) { ctx->CopyResource(bb, t); sc->Present(0, 0); }
        printf("10 copias+presents ok"); bb->Release(); t->Release(); sc->Release();
    }
    printf("\n"); f2->Release(); DestroyWindow(hw); ctx->Release(); dev->Release(); ad->Release();
}
int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const wchar_t* placa = argc > 1 ? argv[1] : L"Radeon";
    Caso(placa, DXGI_USAGE_RENDER_TARGET_OUTPUT, true);
    Caso(placa, DXGI_USAGE_SHADER_INPUT, true);
    Caso(placa, DXGI_USAGE_BACK_BUFFER, true);
    Caso(placa, 0, true);
    return 0;
}
