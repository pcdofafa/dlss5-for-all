// probe_core.cpp — D3D11CoreCreateDevice no Win11 26200: 10 parâmetros, o 1º NÃO é IDXGIFactory
// (é um ponteiro opcional para uma struct interna; null funciona).
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <cstdio>
typedef HRESULT (WINAPI* PFN10)(const void*, IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                const D3D_FEATURE_LEVEL*, UINT, UINT, ID3D11Device**, D3D_FEATURE_LEVEL*);
int wmain() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    IDXGIFactory2* f2 = nullptr; CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), (void**)&f2);
    IDXGIAdapter1* ad = nullptr; f2->EnumAdapters1(0, &ad);
    PFN10 fn = (PFN10)GetProcAddress(LoadLibraryW(L"d3d11.dll"), "D3D11CoreCreateDevice");
    D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    UINT sdks[] = { D3D11_SDK_VERSION, 0, 12345 };
    for (UINT sdk : sdks) {
        printf("-> pArgs=null, adapter, UNKNOWN, SDK=%u: ", sdk);
        ID3D11Device* d = nullptr; D3D_FEATURE_LEVEL got = (D3D_FEATURE_LEVEL)0;
        HRESULT hr = fn(nullptr, ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0x20, fl, 2, sdk, &d, &got);
        printf("hr=0x%08lX %s FL=%04X\n", (unsigned long)hr, d ? "(device criado)" : "", got);
        if (d) {
            IDXGIDevice* dd = nullptr; d->QueryInterface(__uuidof(IDXGIDevice), (void**)&dd);
            IDXGIAdapter* pa = nullptr; if (dd) dd->GetAdapter(&pa);
            DXGI_ADAPTER_DESC desc = {}; if (pa) pa->GetDesc(&desc);
            ID3D11DeviceContext* ctx = nullptr; d->GetImmediateContext(&ctx);
            printf("   adaptador do device: %ls; contexto imediato=%p\n", desc.Description, (void*)ctx);
            if (ctx) ctx->Release(); if (pa) pa->Release(); if (dd) dd->Release(); d->Release();
        }
    }
    return 0;
}
