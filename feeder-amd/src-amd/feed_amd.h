// feed_amd.h -- variante AMD do DLSS5-Feeder: "modo portador FSR" + "swapchain-isca".
//
// Incluido de dlss5-feed.cpp sob FEED_AMD_VARIANT, logo depois de Barrier(): precisa de
// g, g_cfg, Log, Warn, g_self, FormatName, OutputFormatFor, ResolveOutputFormat,
// TypedColorFormat, DrainGpu e dos SLOT_*. Nada aqui e chamado quando o portador esta
// desligado, fora as guardas `if (g_amd.active)` espalhadas pelo fonte principal.
//
// Ideia (PLANO-AMD, secoes 1 a 3): numa Radeon o NGX nao inicializa. O mod DLSS-NR-on-AMD
// hooka ffxCreateContext/ffxDispatch da amd_fidelityfx_dx12.dll e aplica a rede do DLSS 5
// sobre o resultado do FSR, usando cor/depth/MV do dispatch como guias. Entao, em vez de
// NGX Init/Create/Evaluate, o Feeder carrega a DLL do FSR 3.1 (SDK 1.1.4, MIT), cria um
// contexto de upscaler 1:1 (NativeAA, jitter zero) no seu device D3D12 privado e despacha
// por frame com as MESMAS texturas compartilhadas que ja alimentavam o DLSS. O copy-back
// para o backbuffer D3D11 nao muda.
//
// Chaves novas do dlss5-feed.cfg (todas lidas em CfgReload):
//   amd_carrier         -1 auto (VendorId 0x1002), 0 nunca, 1 forcar (teste numa RTX)
//   amd_color_fmt       formato DXGI do buffer de cor/saida do portador: 0 = o do backbuffer,
//                       87 = B8G8R8A8_UNORM (padrao), 28 = R8G8B8A8_UNORM, 10 = R16G16B16A16_FLOAT
//                       (o unico comprovado nos logs do mod). Diferente do backbuffer => a
//                       cor passa pelo blit de reamostragem em vez de CopyResource.
//   amd_srgb            -1 auto (liga em SDR), 0/1: flags NON_LINEAR_COLORSPACE do FSR
//   amd_decoy_swapchain 0/1: swapchain D3D12 numa janela oculta, na fila do Feeder,
//                       apresentado antes do primeiro Present do jogo e depois ~30x/s numa
//                       thread propria, para o mod adotar o device do Feeder (secao 3.3)
#pragma once

#include <dxgi1_4.h>
#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_upscale.h"
#include "ffx_api/dx12/ffx_api_dx12.h"

extern "C" const char *NgxForwardDescribe();
extern "C" bool        NgxForwardAvailable();
static bool InitSession(ID3D11Device *dev11, ID3D11DeviceContext *ctx);   // definida mais abaixo no fonte principal
static const char *FormatName(DXGI_FORMAT f);                              // idem

static const wchar_t *kAmdFfxDll = L"amd_fidelityfx_dx12.dll";

struct AmdState
{
    bool     active;          // a sessao atual roda o portador FSR (decidido em InitSession)
    HMODULE  dll;
    PfnFfxCreateContext  CreateContext;
    PfnFfxDestroyContext DestroyContext;
    PfnFfxDispatch       Dispatch;
    PfnFfxQuery          Query;
    PfnFfxConfigure      Configure;
    ffxContext ctx;
    UINT       ctx_w, ctx_h;
    uint32_t   ctx_flags;
    bool       srgb;           // flags NON_LINEAR ligadas neste contexto
    bool       color_convert;  // o buffer de cor do portador nao tem o formato do backbuffer
    UINT64     dispatches;
    UINT64     dispatch_fails;
    LONGLONG   last_dispatch_qpc;
    char       status[192];    // linha do overlay

    // swapchain-isca
    reshade::api::effect_runtime *decoy_rt;  // o runtime que o ReShade cria sobre a isca (ignorado pelo feed)
    bool       decoy_creating;
    bool       decoy_failed;
    HWND       decoy_hwnd;
    DWORD      decoy_owner_thread;
    IDXGISwapChain1 *decoy_sc;
    HANDLE     decoy_thread, decoy_stop;
    volatile LONG decoy_presents;
};
static AmdState g_amd;

static void AmdStatus(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(g_amd.status, sizeof(g_amd.status), _TRUNCATE, fmt, ap);
    va_end(ap);
}

static bool AmdCarrierWanted(UINT vendor_id)
{
    if (g_cfg.amd_carrier == 0) return false;
    if (g_cfg.amd_carrier == 1) return true;
    return vendor_id == 0x1002;
}

static void AmdFfxMessage(uint32_t type, const wchar_t *message)
{
    Log("[amd] ffx %s: %ls", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", message ? message : L"");
}

// Carrega a DLL do FSR de dentro da pasta do add-on (caminho completo: nunca a de um jogo
// ou do System32). O mod procura os modulos ja carregados quando arma seus hooks, entao a
// DLL entra no processo aqui, na abertura da sessao, e nunca sai (FreeLibrary poderia
// deixar um detour do mod apontando para memoria liberada).
static bool AmdLoadFfx()
{
    if (g_amd.dll != nullptr) return true;
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_self, path, MAX_PATH);
    if (wchar_t *s = wcsrchr(path, L'\\')) *(s + 1) = L'\0';
    wcscat_s(path, MAX_PATH, kAmdFfxDll);

    HMODULE m = GetModuleHandleW(kAmdFfxDll);
    const bool already = m != nullptr;
    if (m == nullptr) m = LoadLibraryW(path);
    if (m == nullptr)
    {
        Log("[amd] carrier: could not load %ls (error %lu) -- put amd_fidelityfx_dx12.dll (FidelityFX SDK 1.1.4) next to this add-on", path, GetLastError());
        AmdStatus("FSR DLL missing (%ls)", kAmdFfxDll);
        return false;
    }
    g_amd.CreateContext  = reinterpret_cast<PfnFfxCreateContext >(reinterpret_cast<void *>(GetProcAddress(m, "ffxCreateContext")));
    g_amd.DestroyContext = reinterpret_cast<PfnFfxDestroyContext>(reinterpret_cast<void *>(GetProcAddress(m, "ffxDestroyContext")));
    g_amd.Dispatch       = reinterpret_cast<PfnFfxDispatch      >(reinterpret_cast<void *>(GetProcAddress(m, "ffxDispatch")));
    g_amd.Query          = reinterpret_cast<PfnFfxQuery         >(reinterpret_cast<void *>(GetProcAddress(m, "ffxQuery")));
    g_amd.Configure      = reinterpret_cast<PfnFfxConfigure     >(reinterpret_cast<void *>(GetProcAddress(m, "ffxConfigure")));
    if (g_amd.CreateContext == nullptr || g_amd.DestroyContext == nullptr || g_amd.Dispatch == nullptr || g_amd.Query == nullptr)
    {
        Log("[amd] carrier: %ls has no ffx-api exports (ffxCreateContext/ffxDispatch/ffxDestroyContext/ffxQuery) -- wrong DLL", kAmdFfxDll);
        AmdStatus("FSR DLL has no ffx-api exports");
        return false;
    }
    g_amd.dll = m;
    wchar_t got[MAX_PATH] = {};
    GetModuleFileNameW(m, got, MAX_PATH);
    Log("[amd] carrier: %ls %s: %ls (ffxCreateContext=%p ffxDispatch=%p)", kAmdFfxDll, already ? "was already loaded" : "loaded",
        got, (void *)g_amd.CreateContext, (void *)g_amd.Dispatch);
    return true;
}

// Chamado em InitSession no lugar do bloco NGX. Decide e liga o portador.
static bool AmdCarrierStart(UINT vendor_id)
{
    g_amd.active = false;
    Log("[amd] carrier: amd_carrier=%d vendor=0x%04X -> %s", g_cfg.amd_carrier, vendor_id,
        AmdCarrierWanted(vendor_id) ? "FSR carrier (no NGX on this session)" : "NGX path");
    if (!AmdCarrierWanted(vendor_id)) return false;
    if (!AmdLoadFfx()) return false;
    g_amd.active = true;
    g_amd.dispatches = 0;
    g_amd.dispatch_fails = 0;
    AmdStatus("FSR DLL loaded, context not built yet");
    return true;
}

static DXGI_FORMAT AmdColorFormatFromCfg()
{
    switch (g_cfg.amd_color_fmt)
    {
    case 10: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case 28: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case 87: return DXGI_FORMAT_B8G8R8A8_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;   // 0 (ou valor desconhecido) = manter o do backbuffer
    }
}

// BuildResources, depois de g.color_fmt/g.output_fmt/g.hdr: troca o formato do buffer de
// cor do portador quando amd_color_fmt pede outro. g.hdr ja foi decidido pelo formato do
// backbuffer, entao um portador RGBA16F com conteudo SDR continua SDR para o FSR.
static void AmdApplyColorFormat(DXGI_FORMAT bb_fmt)
{
    g_amd.color_convert = false;
    if (!g_amd.active) return;
    const DXGI_FORMAT want = AmdColorFormatFromCfg();
    if (want == DXGI_FORMAT_UNKNOWN || want == g.color_fmt) return;
    const DXGI_FORMAT before = g.color_fmt;
    g.color_fmt   = want;
    g.output_fmt  = ResolveOutputFormat(OutputFormatFor(want), g.dev12);
    g_amd.color_convert = true;
    Log("[amd] carrier: amd_color_fmt=%d: colour buffer %s (backbuffer %s / %s) -> output %s; colour goes through the resample blit",
        g_cfg.amd_color_fmt, FormatName(want), FormatName(bb_fmt), FormatName(before), FormatName(g.output_fmt));
}

static void AmdDestroyContext()
{
    if (g_amd.ctx == nullptr) return;
    Breadcrumb("destroying the FSR carrier context");
    const ffxReturnCode_t rc = g_amd.DestroyContext(&g_amd.ctx, nullptr);
    if (rc != FFX_API_RETURN_OK) Log("[amd] carrier: ffxDestroyContext -> %u", rc);
    else Log("[amd] carrier: context destroyed (%llu dispatches)", (unsigned long long)g_amd.dispatches);
    g_amd.ctx = nullptr;
    g_amd.ctx_w = g_amd.ctx_h = 0;
}

// CreateDlssFeature do portador: contexto FSR 1:1 (render == display, NativeAA). Sem lista
// de comandos: a ffx-api cria seus recursos internos fora de qualquer command list.
static bool AmdCreateContext(UINT w, UINT h, bool inverted, bool *crashed)
{
    if (crashed != nullptr) *crashed = false;
    AmdDestroyContext();
    if (g_amd.dll == nullptr && !AmdLoadFfx()) return false;

    const UINT out_w = g.output_width  != 0 ? g.output_width  : w;
    const UINT out_h = g.output_height != 0 ? g.output_height : h;
    g_amd.srgb = g_cfg.amd_srgb >= 0 ? g_cfg.amd_srgb != 0 : !g.hdr;

    uint32_t flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    if (inverted)   flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
    if (g.hdr)      flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
    if (g_amd.srgb) flags |= FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
    if (g_cfg.flags >= 0) flags = static_cast<uint32_t>(g_cfg.flags);   // mesma chave "flags" do cfg, agora com o significado do FSR

    ffxCreateBackendDX12Desc be = {};
    be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    be.device      = g.dev12;

    ffxCreateContextDescUpscale cd = {};
    cd.header.type    = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    cd.header.pNext   = &be.header;
    cd.flags          = flags;
    cd.maxRenderSize  = { w, h };
    cd.maxUpscaleSize = { out_w, out_h };
    cd.fpMessage      = AmdFfxMessage;

    Breadcrumb("creating the FSR carrier context");
    LARGE_INTEGER a, b, f;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    const ffxReturnCode_t rc = g_amd.CreateContext(&g_amd.ctx, &cd.header, nullptr);
    QueryPerformanceCounter(&b);
    const double ms = (b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
    if (rc != FFX_API_RETURN_OK || g_amd.ctx == nullptr)
    {
        Log("[amd] carrier: erro: ffxCreateContext -> %u (%s) for %ux%u -> %ux%u flags=0x%X", rc,
            rc == FFX_API_RETURN_ERROR_UNKNOWN_DESCTYPE ? "unknown desc type" :
            rc == FFX_API_RETURN_ERROR_RUNTIME_ERROR ? "runtime (D3D12) error" :
            rc == FFX_API_RETURN_NO_PROVIDER ? "no provider" :
            rc == FFX_API_RETURN_ERROR_MEMORY ? "out of memory" :
            rc == FFX_API_RETURN_ERROR_PARAMETER ? "bad parameter" : "error", w, h, out_w, out_h, flags);
        AmdStatus("ffxCreateContext failed (%u)", rc);
        g_amd.ctx = nullptr;
        return false;
    }
    g_amd.ctx_w = w;
    g_amd.ctx_h = h;
    g_amd.ctx_flags = flags;
    g.create_flags = static_cast<int>(flags);
    Log("[amd] carrier: context ok %ux%u -> %ux%u (NativeAA 1:1) flags=0x%X (%s%s%s%s) in %.1f ms; colour %s -> output %s, depth R32_FLOAT%s, mv R16G16_FLOAT, reactive=%s",
        w, h, out_w, out_h, flags,
        (flags & FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE) ? "HDR " : "SDR ",
        (flags & FFX_UPSCALE_ENABLE_DEPTH_INVERTED) ? "DepthInverted " : "",
        (flags & FFX_UPSCALE_ENABLE_AUTO_EXPOSURE) ? "AutoExposure " : "",
        (flags & FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE) ? "NonLinearSRGB" : "",
        ms, FormatName(g.color_fmt), FormatName(g.output_fmt), inverted ? " (reversed)" : "",
        g.mask_ok ? "DLSS5_Mask" : "none");
    AmdStatus("context %ux%u ok, flags 0x%X, %llu dispatches", w, h, flags, (unsigned long long)g_amd.dispatches);
    g.need_reset        = true;
    g.frame_ready       = true;
    g.create_fail_count = 0;
    g_amd.last_dispatch_qpc = 0;
    return true;
}

// O evaluate do portador. Chamado de FeedFrame11 com as barreiras ja aplicadas (COLOR/
// DEPTH/MV/MASK em NON_PIXEL_SHADER_RESOURCE, OUTPUT em UNORDERED_ACCESS) e com g.list
// aberto; o backend DX12 da ffx-api devolve cada recurso ao estado informado aqui, entao
// as barreiras de volta que o chamador ja tem continuam validas.
static bool AmdDispatch(int reset)
{
    if (g_amd.ctx == nullptr) { Log("[amd] carrier: dispatch without a context"); return false; }

    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    float dt_ms = 16.667f;
    if (g_amd.last_dispatch_qpc != 0)
    {
        dt_ms = static_cast<float>((now.QuadPart - g_amd.last_dispatch_qpc) * 1000.0 / (double)f.QuadPart);
        if (dt_ms < 1.0f) dt_ms = 1.0f;
        if (dt_ms > 200.0f) dt_ms = 200.0f;
    }
    g_amd.last_dispatch_qpc = now.QuadPart;

    ffxDispatchDescUpscale dd = {};
    dd.header.type   = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    dd.commandList   = g.list;
    dd.color         = ffxApiGetResourceDX12(g.tex12[SLOT_COLOR],  FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dd.depth         = ffxApiGetResourceDX12(g.tex12[SLOT_DEPTH],  FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dd.motionVectors = ffxApiGetResourceDX12(g.tex12[SLOT_MV],     FFX_API_RESOURCE_STATE_COMPUTE_READ);
    if (g.mask_ok)   // a mascara "MV falhou na validacao" do DLSS5_Feed.fx = reativo do FSR (favorece o frame atual)
        dd.reactive  = ffxApiGetResourceDX12(g.tex12[SLOT_MASK],   FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dd.output        = ffxApiGetResourceDX12(g.tex12[SLOT_OUTPUT], FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dd.jitterOffset      = { 0.0f, 0.0f };
    dd.motionVectorScale = { g_cfg.mv_scale_x, g_cfg.mv_scale_y };   // o FX ja entrega pixels
    dd.renderSize        = { g.width, g.height };
    dd.upscaleSize       = { g.output_width != 0 ? g.output_width : g.width, g.output_height != 0 ? g.output_height : g.height };
    dd.enableSharpening  = false;
    dd.sharpness         = 0.0f;
    dd.frameTimeDelta    = dt_ms;
    dd.preExposure       = 1.0f;
    dd.reset             = reset != 0;
    dd.cameraNear        = 0.1f;
    dd.cameraFar         = 1000.0f;   // RESHADE_DEPTH_LINEARIZATION_FAR_PLANE do preset
    dd.cameraFovAngleVertical = 1.0f;
    dd.viewSpaceToMetersFactor = 1.0f;
    dd.flags             = g_amd.srgb ? FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB : 0;

    const ffxReturnCode_t rc = g_amd.Dispatch(&g_amd.ctx, &dd.header);
    const unsigned long long n = ++g_amd.dispatches;
    if (rc != FFX_API_RETURN_OK)
    {
        ++g_amd.dispatch_fails;
        if (g_amd.dispatch_fails <= 5 || (g_amd.dispatch_fails % 600) == 0)
            Log("[amd] carrier: erro: ffxDispatch #%llu -> %u (failures so far %llu)", n, rc, (unsigned long long)g_amd.dispatch_fails);
        AmdStatus("dispatch #%llu FAILED (%u)", n, rc);
        return false;
    }
    if (n <= static_cast<unsigned long long>(g_cfg.log_frames) || (n % 1800) == 0)
        Log("[amd] carrier: dispatch #%llu ok (%ux%u, reset=%d, dt=%.1f ms, mv scale %.2f,%.2f)", n, g.width, g.height, reset, dt_ms,
            g_cfg.mv_scale_x, g_cfg.mv_scale_y);
    if ((n % 60) == 1)
        AmdStatus("context %ux%u ok, flags 0x%X, %llu dispatches%s", g_amd.ctx_w, g_amd.ctx_h, g_amd.ctx_flags, n,
                  g_amd.decoy_sc ? " + decoy swapchain" : "");
    return true;
}

// ---------------------------------------------------------------------------
// Swapchain-isca (amd_decoy_swapchain=1)
// ---------------------------------------------------------------------------
// O mod adota "o device do primeiro swapchain apresentado" e trata um swapchain D3D11 como
// "unsupported". O do jogo e D3D11 e apresenta antes de o Feeder ter device. A isca e um
// swapchain D3D12 (32x32, FLIP_DISCARD) numa janela oculta, criado na fila do Feeder e
// apresentado uma vez AQUI, dentro do init do effect runtime do jogo -- que o ReShade
// dispara de dentro do CreateSwapChain do jogo, antes do primeiro Present -- e depois
// ~30x/s numa thread propria. O ReShade tambem ve esse swapchain e cria um segundo effect
// runtime sobre ele (a fila e um proxy do ReShade); esse runtime e reconhecido pela flag
// decoy_creating e ignorado em OnInitEffectRuntime/OnRenderTechnique/OnDestroyEffectRuntime.

static LRESULT CALLBACK AmdDecoyWndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static DWORD WINAPI AmdDecoyThread(LPVOID)
{
    while (WaitForSingleObject(g_amd.decoy_stop, 33) == WAIT_TIMEOUT)
    {
        if (g_amd.decoy_sc == nullptr) break;
        g_amd.decoy_sc->Present(0, 0);   // DXGI_STATUS_OCCLUDED numa janela oculta e normal
        InterlockedIncrement(&g_amd.decoy_presents);
    }
    return 0;
}

static bool AmdDecoyCreate()
{
    if (g_amd.decoy_sc != nullptr) return true;
    if (g.queue == nullptr) { Log("[amd] decoy: no D3D12 queue yet"); return false; }

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = AmdDecoyWndProc;
    wc.hInstance     = g_self;
    wc.lpszClassName = L"DLSS5FeedAmdDecoy";
    RegisterClassW(&wc);   // ERROR_CLASS_ALREADY_EXISTS numa segunda sessao e inofensivo
    g_amd.decoy_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"dlss5-feed AMD decoy swapchain",
                                       WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, g_self, nullptr);
    if (g_amd.decoy_hwnd == nullptr) { Log("[amd] decoy: CreateWindowExW failed (%lu)", GetLastError()); g_amd.decoy_failed = true; return false; }
    g_amd.decoy_owner_thread = GetCurrentThreadId();

    IDXGIFactory2 *factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void **>(&factory));
    if (FAILED(hr) || factory == nullptr)
    {
        Log("[amd] decoy: CreateDXGIFactory1 failed 0x%08X", hr);
        DestroyWindow(g_amd.decoy_hwnd); g_amd.decoy_hwnd = nullptr; g_amd.decoy_failed = true;
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width       = 32;
    sd.Height      = 32;
    sd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling     = DXGI_SCALING_STRETCH;
    sd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;

    g_amd.decoy_creating = true;   // o init_effect_runtime aninhado que o ReShade dispara e o da isca
    hr = factory->CreateSwapChainForHwnd(g.queue, g_amd.decoy_hwnd, &sd, nullptr, nullptr, &g_amd.decoy_sc);
    g_amd.decoy_creating = false;
    if (SUCCEEDED(hr)) factory->MakeWindowAssociation(g_amd.decoy_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    factory->Release();
    if (FAILED(hr) || g_amd.decoy_sc == nullptr)
    {
        Log("[amd] decoy: CreateSwapChainForHwnd on the feed queue failed 0x%08X", hr);
        DestroyWindow(g_amd.decoy_hwnd); g_amd.decoy_hwnd = nullptr; g_amd.decoy_failed = true; g_amd.decoy_sc = nullptr;
        return false;
    }

    const HRESULT ph = g_amd.decoy_sc->Present(0, 0);   // o "primeiro swapchain apresentado" do processo
    InterlockedIncrement(&g_amd.decoy_presents);
    g_amd.decoy_stop   = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_amd.decoy_thread = CreateThread(nullptr, 0, AmdDecoyThread, nullptr, 0, nullptr);
    Log("[amd] decoy: D3D12 swapchain %p (32x32 FLIP_DISCARD) on queue %p, window %p; first Present -> 0x%08X; ReShade runtime on it: %p; presenter thread %s",
        (void *)g_amd.decoy_sc, (void *)g.queue, (void *)g_amd.decoy_hwnd, ph, (void *)g_amd.decoy_rt,
        g_amd.decoy_thread ? "started (~30 Hz)" : "FAILED");
    return true;
}

static void AmdDecoyDestroy()
{
    if (g_amd.decoy_sc == nullptr && g_amd.decoy_hwnd == nullptr) return;
    if (g_amd.decoy_stop != nullptr) SetEvent(g_amd.decoy_stop);
    if (g_amd.decoy_thread != nullptr)
    {
        if (WaitForSingleObject(g_amd.decoy_thread, 3000) != WAIT_OBJECT_0) Log("[amd] decoy: presenter thread did not stop in 3 s");
        CloseHandle(g_amd.decoy_thread); g_amd.decoy_thread = nullptr;
    }
    if (g_amd.decoy_stop != nullptr) { CloseHandle(g_amd.decoy_stop); g_amd.decoy_stop = nullptr; }
    const LONG presents = g_amd.decoy_presents;
    if (g_amd.decoy_sc != nullptr) { g_amd.decoy_sc->Release(); g_amd.decoy_sc = nullptr; }   // o ReShade destroi o runtime da isca aqui
    if (g_amd.decoy_hwnd != nullptr)
    {
        if (GetCurrentThreadId() == g_amd.decoy_owner_thread) DestroyWindow(g_amd.decoy_hwnd);
        else PostMessageW(g_amd.decoy_hwnd, WM_CLOSE, 0, 0);   // janela de outra thread: deixa o loop dela fechar
        g_amd.decoy_hwnd = nullptr;
    }
    g_amd.decoy_rt = nullptr;
    Log("[amd] decoy: released after %ld presents", presents);
}

// OnInitEffectRuntime do jogo (D3D11), no fim: abre a sessao D3D12 agora (normalmente ela
// so nasce no primeiro frame de efeito) e cria a isca. So quando o portador vai ser usado.
static void AmdDecoyOnRuntimeInit(reshade::api::effect_runtime *rt)
{
    if (g_cfg.amd_decoy_swapchain != 1 || rt != g.runtime || g_amd.decoy_sc != nullptr || g_amd.decoy_failed) return;
    if (!g_cfg.enabled || g_cfg.mode == 0 || g.disabled) return;
    reshade::api::device *dev = rt->get_device();
    if (dev == nullptr || dev->get_api() != reshade::api::device_api::d3d11) return;
    auto *dev11 = reinterpret_cast<ID3D11Device *>(dev->get_native());
    if (dev11 == nullptr) return;

    UINT vendor = 0;
    IDXGIDevice *dxgi_dev = nullptr;
    if (SUCCEEDED(dev11->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&dxgi_dev))) && dxgi_dev != nullptr)
    {
        IDXGIAdapter *adapter = nullptr;
        if (SUCCEEDED(dxgi_dev->GetAdapter(&adapter)) && adapter != nullptr)
        {
            DXGI_ADAPTER_DESC ad = {};
            adapter->GetDesc(&ad);
            vendor = ad.VendorId;
            adapter->Release();
        }
        dxgi_dev->Release();
    }
    if (!AmdCarrierWanted(vendor)) { Log("[amd] decoy: carrier not wanted on vendor 0x%04X; no decoy", vendor); return; }

    if (!g.session_ready)
    {
        ID3D11DeviceContext *ctx = nullptr;
        dev11->GetImmediateContext(&ctx);
        if (ctx == nullptr) return;
        Log("[amd] decoy: opening the D3D12 session early (from the effect runtime init) so the decoy can be presented before the game's first Present");
        InitSession(dev11, ctx);
        ctx->Release();
    }
    if (!g.session_ready || !g_amd.active) return;
    AmdDecoyCreate();
}

// Chamado no DllMain: nada aqui pode dar LoadLibrary (loader lock), por isso o estado do
// forwarder NGX so e consultado e logado em InitSession.
static void AmdLogVariant()
{
    Log("[amd] variant: DLSS5-Feeder AMD build (MinGW, FEED_AMD_VARIANT): amd_carrier=%d amd_color_fmt=%d amd_srgb=%d amd_decoy_swapchain=%d",
        g_cfg.amd_carrier, g_cfg.amd_color_fmt, g_cfg.amd_srgb, g_cfg.amd_decoy_swapchain);
    // Bases dos modulos, para transformar um endereco de crash em RVA sem depurador.
    Log("[amd] modules: this add-on %p, dxgi.dll (ReShade) %p", (void *)g_self, (void *)GetModuleHandleW(L"dxgi.dll"));
}
