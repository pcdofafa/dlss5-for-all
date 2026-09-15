// teste2_present.cpp — swapchain flip (FLIP_DISCARD, 2 buffers, waitable, latência 1)
// numa janela 1920x1080 na GPU A (NVIDIA): mede a taxa do laço
// Wait(waitable) -> Clear -> Present(0,0) em quatro estados da janela:
// visível, cloaked (DWMWA_CLOAK), SW_HIDE e movida para fora do desktop.
//
// Compilar:
//   g++ -std=c++17 -O2 -municode teste2_present.cpp -o teste2_present.exe
//       -ld3d11 -ldxgi -ldxguid -ldwmapi -static
#include <windows.h>
#include <dwmapi.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;

static double g_qpf = 0;
static double agora_ms() { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart * 1000.0 / g_qpf; }
static void logf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n"); fflush(stdout);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE) return 0;   // nunca fecha sozinha
    return DefWindowProcW(h, m, w, l);
}
static void bombear() {
    MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}

// Acha o adaptador NVIDIA (A).
static ComPtr<IDXGIAdapter1> adaptador_nvidia(IDXGIFactory6* f) {
    for (UINT i = 0;; i++) {
        ComPtr<IDXGIAdapter1> ad;
        if (f->EnumAdapters1(i, &ad) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d; ad->GetDesc1(&d);
        if (d.VendorId == 0x10DE) { logf("adaptador A: %ls", d.Description); return ad; }
    }
    return nullptr;
}

struct Cena {
    HWND hwnd = nullptr;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGISwapChain2> sc;
    HANDLE waitable = nullptr;

    bool criar() {
        WNDCLASSW wc = {}; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"Teste2Present"; RegisterClassW(&wc);
        RECT r = { 0, 0, 1920, 1080 }; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd = CreateWindowExW(0, wc.lpszClassName, L"Teste 2 - Present", WS_OVERLAPPEDWINDOW,
                               100, 100, r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd) { logf("FALHOU CreateWindowExW"); return false; }

        ComPtr<IDXGIFactory6> f;
        HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&f));
        if (FAILED(hr)) { logf("FALHOU CreateDXGIFactory2 hr=0x%08lX", (unsigned long)hr); return false; }
        ComPtr<IDXGIAdapter1> ad = adaptador_nvidia(f.Get());
        if (!ad) { logf("FALHOU: sem NVIDIA"); return false; }
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        hr = D3D11CreateDevice(ad.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                               &fl, 1, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
        if (FAILED(hr)) { logf("FALHOU D3D11CreateDevice hr=0x%08lX", (unsigned long)hr); return false; }

        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = 1920; sd.Height = 1080; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        ComPtr<IDXGISwapChain1> sc1;
        hr = f->CreateSwapChainForHwnd(dev.Get(), hwnd, &sd, nullptr, nullptr, &sc1);
        if (FAILED(hr)) { logf("FALHOU CreateSwapChainForHwnd hr=0x%08lX", (unsigned long)hr); return false; }
        sc1.As(&sc);
        sc->SetMaximumFrameLatency(1);
        waitable = sc->GetFrameLatencyWaitableObject();
        f->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
        logf("swapchain FLIP_DISCARD 2 buffers, waitable, latência 1: ok");
        return true;
    }

    // Laço de `ms` milissegundos; devolve quadros/s e conta o que Present devolveu.
    void medir(const char* estado, double ms) {
        ComPtr<ID3D11Texture2D> bb; ComPtr<ID3D11RenderTargetView> rtv;
        sc->GetBuffer(0, IID_PPV_ARGS(&bb));
        dev->CreateRenderTargetView(bb.Get(), nullptr, &rtv);
        float cor[4] = { 0.1f, 0.3f, 0.6f, 1.0f };
        int quadros = 0, naoSOk = 0, timeouts = 0; HRESULT ultimoDiferente = S_OK;
        double esperaTotal = 0;
        double t0 = agora_ms();
        while (agora_ms() - t0 < ms) {
            bombear();
            double te = agora_ms();
            DWORD w = WaitForSingleObject(waitable, 1000);
            esperaTotal += agora_ms() - te;
            if (w == WAIT_TIMEOUT) timeouts++;
            cor[0] = (quadros % 2) ? 0.9f : 0.1f;
            ctx->ClearRenderTargetView(rtv.Get(), cor);
            HRESULT hr = sc->Present(0, 0);
            if (hr != S_OK) { naoSOk++; ultimoDiferente = hr; }
            quadros++;
        }
        double dt = agora_ms() - t0;
        logf("%-34s: %6.0f quadros/s  (%d quadros em %.0f ms; espera média no waitable %.3f ms; "
             "timeouts=%d; Present != S_OK: %d vezes, último=0x%08lX%s)",
             estado, quadros * 1000.0 / dt, quadros, dt, esperaTotal / quadros, timeouts, naoSOk,
             (unsigned long)ultimoDiferente,
             ultimoDiferente == DXGI_STATUS_OCCLUDED ? " (DXGI_STATUS_OCCLUDED)" : "");
    }
};

int wmain() {
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpf = (double)f.QuadPart;
    logf("=== TESTE 2: Present em swapchain flip com janela invisível ===");
    Cena c; if (!c.criar()) return 1;

    // (i) visível, uma vez só, 2 s.
    ShowWindow(c.hwnd, SW_SHOWNORMAL); bombear(); Sleep(300); bombear();
    c.medir("(i) visível", 2000);

    // (ii) cloaked pelo DWM (continua "visível" para o Win32, invisível na tela).
    BOOL cloak = TRUE;
    HRESULT hr = DwmSetWindowAttribute(c.hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
    logf("DwmSetWindowAttribute(DWMWA_CLOAK, TRUE): hr=0x%08lX", (unsigned long)hr);
    bombear(); Sleep(300); bombear();
    c.medir("(ii) cloaked", 2000);

    // (iii) SW_HIDE (descloaka antes para isolar o efeito).
    cloak = FALSE; DwmSetWindowAttribute(c.hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
    ShowWindow(c.hwnd, SW_HIDE); bombear(); Sleep(300); bombear();
    c.medir("(iii) SW_HIDE", 2000);

    // (iv) visível mas fora do desktop (x = -10000).
    SetWindowPos(c.hwnd, nullptr, -10000, -10000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(c.hwnd, SW_SHOWNOACTIVATE); bombear(); Sleep(300); bombear();
    c.medir("(iv) fora do desktop (x=-10000)", 2000);

    // Extra: cloaked + fora do desktop, que é o que uma "ponte" real usaria.
    cloak = TRUE; DwmSetWindowAttribute(c.hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak));
    bombear(); Sleep(300); bombear();
    c.medir("(v) cloaked + fora do desktop", 2000);

    ShowWindow(c.hwnd, SW_HIDE); bombear();
    DestroyWindow(c.hwnd);
    return 0;
}
