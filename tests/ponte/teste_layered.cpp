// Ritmo do Present num swapchain flip (waitable, uso SHADER_INPUT, copia no backbuffer) em janela VISIVEL:
// (a) nao layered; (b) layered antes de criar; (c) layered tirado na criacao e devolvido depois.
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
static double Ms() { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return 1000.0 * c.QuadPart / f.QuadPart; }
static void Caso(const char* nome, int modo) {
    IDXGIAdapter1* ad = Achar(L"NVIDIA");
    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D11CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, lv, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    WNDCLASSEXW wc{ sizeof(wc) }; wc.lpfnWndProc = Wp; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"TesteLayWnd"; RegisterClassExW(&wc);
    DWORD ex = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | ((modo >= 1) ? WS_EX_LAYERED : 0);
    HWND hw = CreateWindowExW(ex, wc.lpszClassName, L"teste", WS_POPUP, 100, 100, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
    if (modo >= 1) SetLayeredWindowAttributes(hw, 0, 255, LWA_ALPHA);
    ShowWindow(hw, SW_SHOWNOACTIVATE);
    if (modo == 2) SetWindowLongPtrW(hw, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    IDXGIFactory2* f2 = nullptr; ad->GetParent(__uuidof(IDXGIFactory2), (void**)&f2);
    DXGI_SWAP_CHAIN_DESC1 d1{}; d1.Width = 1280; d1.Height = 720; d1.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d1.SampleDesc.Count = 1;
    d1.BufferUsage = DXGI_USAGE_SHADER_INPUT; d1.BufferCount = 2; d1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; d1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    d1.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    IDXGISwapChain1* sc = nullptr; HRESULT hr = f2->CreateSwapChainForHwnd(dev, hw, &d1, nullptr, nullptr, &sc);
    if (modo == 2) { SetWindowLongPtrW(hw, GWL_EXSTYLE, ex); SetLayeredWindowAttributes(hw, 0, 255, LWA_ALPHA); }
    IDXGISwapChain2* sc2 = nullptr; HANDLE lat = nullptr;
    if (sc && SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&sc2))) { sc2->SetMaximumFrameLatency(1); lat = sc2->GetFrameLatencyWaitableObject(); }
    D3D11_TEXTURE2D_DESC td{}; td.Width = 1280; td.Height = 720; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* t = nullptr; dev->CreateTexture2D(&td, nullptr, &t);
    int n = 0; double t0 = Ms(); double esp = 0;
    while (Ms() - t0 < 2000 && sc) {
        double e0 = Ms(); if (lat) WaitForSingleObjectEx(lat, 100, FALSE); esp += Ms() - e0;
        ID3D11Texture2D* bb = nullptr; sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
        ctx->CopyResource(bb, t); bb->Release(); sc->Present(0, 0); n++;
        MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    printf("[%s] swapchain hr=0x%08lX: %d presents em 2 s = %.0f/s, espera media no waitable %.2f ms\n", nome, hr, n, n / 2.0, n ? esp / n : 0);
    if (lat) CloseHandle(lat); if (sc2) sc2->Release(); if (sc) sc->Release(); t->Release(); f2->Release(); DestroyWindow(hw); ctx->Release(); dev->Release(); ad->Release();
}
int wmain() { setvbuf(stdout, nullptr, _IONBF, 0); Caso("nao layered", 0); Caso("layered antes", 1); Caso("layered tirado e devolvido", 2); return 0; }
