/* =====================================================================
   ponte_comum.h -- o cliente injetado dlss5-ponte64.dll (lado "jogo" do fio
   feed_ipc v9): includes, protocolo v9, log, utilidades e o remendo de vtable.

   Fase 2 do Modo Integrado (CONTRATO-FASE2.md, 15/09/2026). B1.

   A DLL entra no processo do jogo por LoadLibraryW (o injetor do B2 a coloca).
   Ela nao exporta nada. Fala o MESMO protocolo v9 que o add-on de 32 bits do
   DLSS5-Feeder fala (external\DLSS5-Feeder\src\feed_ipc.h); a diferenca e que
   nao usamos ReShade: os hooks sao por vtable do IDXGISwapChain e da
   ID3D12CommandQueue, achados com device/swapchain descartaveis (tecnica do
   kiero, MIT, usada so como referencia dos indices).

   O host que recebe o que mandamos e o nosso proprio app, no papel do
   dlss5-feed-host64.exe (src\ponte_feeder_host.h). Ele e o SERVIDOR do pipe e
   ja esta escutando quando a DLL entra; nos conectamos (CreateFile).

   REGRAS DURAS (CONTRATO-FASE2, secao B1):
   - Nada de trabalho no DllMain: PROCESS_ATTACH so sobe uma thread.
   - Nunca deixar o jogo cair: todo erro chama Desligar() e passa ao Present
     original; nunca throw; nunca bloquear a CPU do jogo mais que 1 quadro.
   - Nada gravado na pasta do jogo: o log vai em %TEMP%.
   ===================================================================== */
#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00   /* Windows 10: ID3D11Device5, IDXGISwapChain3 */
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <mutex>

namespace Ponte {

/* ------------------------------------------------------------------ */
/* Protocolo feed_ipc v9 (copiado de feed_ipc.h, byte a byte). O add-on   */
/* do Feeder e o nosso host usam exatamente estes tamanhos; os            */
/* static_assert sao a garantia de que a copia bate.                      */
/* ------------------------------------------------------------------ */
static const uint32_t kMagic  = 0x35534C44u;  /* 'DLS5' */
static const uint32_t kVersao = 9u;
static const uint32_t kHelloV1 = 3u * sizeof(uint32_t);

/* FeedBuild::client_flags */
static const uint32_t kBuildHostCria  = 1u;   /* FEED_BUILD_HOST_CREATES  */
static const uint32_t kBuildSemUav    = 2u;   /* FEED_BUILD_OUTPUT_NO_UAV */
static const uint32_t kBuildAsyncHome = 4u;   /* FEED_BUILD_ASYNC_HOME    */
/* FeedBuildAck::flags */
static const uint32_t kAckSrAtivo        = 1u;
static const uint32_t kAckSrIndisponivel = 2u;

enum Slot { SLOT_COLOR = 0, SLOT_OUTPUT, SLOT_DEPTH, SLOT_MV, SLOTS };
enum Cliente { CLIENTE_D3D11 = 0, CLIENTE_GL = 1, CLIENTE_VULKAN = 2 };

#pragma pack(push, 1)
struct FeedHello    { uint32_t magic, version, pid, client_kind; uint64_t self_process; };
struct FeedHelloAck { uint32_t magic, version, panel_width, panel_height; };
struct FeedBuild
{
    uint32_t width, height, color_fmt, output_fmt;
    int32_t  hdr, depth_inverted, flags_override, transport;
    float    mv_scale_x, mv_scale_y;
    uint64_t tex[SLOTS];
    uint32_t client_flags;
    uint32_t target_width, target_height;
    uint64_t panel_tex;
};
struct FeedBuildAck
{
    int32_t  ok;
    uint32_t ngx_result;
    uint64_t fence_in, fence_out;
    uint64_t tex[SLOTS];
    uint64_t tex_size[SLOTS];
    uint32_t output_fmt, flags, sr_quality;
    uint64_t panel_tex, panel_size;
};
struct FeedWindowMsg { uint32_t width, height; };
struct FeedFrameMsg  { uint64_t n; uint32_t reset; float jitter_x, jitter_y; };
/* J5 (18/09/2026): 'D' + FeedDadosMsg, ANTES do 'F' do mesmo quadro, na sessao da fase 2. Diz
   que os slots Depth/MV deste quadro tem dado REAL do upscaler do jogo (copiado do balcao da
   sessao .up para os slots da fase 2, na resolucao de render, canto superior esquerdo) e com que
   convencao. So e mandado quando o BuildAck trouxe kAckDados (0x400): um app antigo, que nao
   conhece a tag, nunca a recebe. Quadro sem 'D' = slots sem dado valido (fluxo optico no app). */
struct FeedDadosMsg
{
    uint64_t n;
    uint32_t render_w, render_h;   /* tamanho de Depth neste quadro (MV: mv_w x mv_h) */
    uint32_t mv_w, mv_h;
    float    jitter_x, jitter_y, mv_scale_x, mv_scale_y, frame_dt_ms;
    float    cam_near, cam_far, cam_fov, view_to_m;   /* 0 = o jogo nao informou */
    uint32_t flags;                /* kDados* */
    uint32_t origem;               /* 1 dlss, 2 fsr31, 3 fsr2, 4 xess */
    int64_t  qpc_dispatch;         /* QPC do dispatch do upscaler (mesmo relogio nas duas pontas): a idade do dado */
};
#pragma pack(pop)
static_assert(sizeof(FeedHello)     == 24,  "FeedHello v9 = 24 bytes");
static_assert(sizeof(FeedHelloAck)  == 16,  "FeedHelloAck v9 = 16 bytes");
static_assert(sizeof(FeedBuild)     == 92,  "FeedBuild v9 = 92 bytes (93 com a tag)");
static_assert(sizeof(FeedBuildAck)  == 116, "FeedBuildAck v9 = 116 bytes");
static_assert(sizeof(FeedFrameMsg)  == 20,  "FeedFrameMsg v9 = 20 bytes (21 com a tag)");
static_assert(sizeof(FeedWindowMsg) == 8,   "FeedWindowMsg v9 = 8 bytes");
static_assert(sizeof(FeedDadosMsg)  == 76,  "FeedDadosMsg (J5) = 76 bytes (77 com a tag)");
/* J5: FeedBuildAck::flags - o host entende 'D' na fase 2 (0x100/0x200 sao da sessao .up). */
static const uint32_t kAckDados = 0x400u;
/* J5: FeedDadosMsg::flags */
static const uint32_t kDadosReset      = 1u;    /* corte de cena declarado pelo jogo (o mesmo bit do 'F') */
static const uint32_t kDadosDepthInv   = 2u;    /* profundidade invertida [1..0] */
static const uint32_t kDadosDepthIncog = 4u;    /* invertida ou nao: desconhecido (contexto criado antes do gancho) */
static const uint32_t kDadosMvTela     = 8u;    /* MV em resolucao de tela, nao de render */
static const uint32_t kDadosMvJitter   = 16u;   /* o jitter esta embutido no MV */
static const uint32_t kDadosMvNdc      = 32u;   /* MV em NDC (XeSS): convencao nao resolvida - o app recusa */

/* ------------------------------------------------------------------ */
/* Log em %TEMP%\dlss5-ponte.<pid>.log (NUNCA na pasta do jogo).        */
/* Thread-safe: e chamado da thread do jogo (Present) e da thread da    */
/* ponte. As linhas fixas que o B4 le estao no CONTRATO-FASE2 B1.       */
/* ------------------------------------------------------------------ */
static FILE*      g_logFile = nullptr;
static std::mutex g_logMutex;
static int64_t    g_qpcFreq = 1;

static void LogAbrir()
{
    wchar_t tmp[MAX_PATH] = L"";
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) { tmp[0] = L'.'; tmp[1] = L'\\'; tmp[2] = 0; }
    wchar_t caminho[MAX_PATH];
    _snwprintf(caminho, MAX_PATH, L"%sdlss5-ponte.%lu.log", tmp, GetCurrentProcessId());
    /* C13: a copia que assume a sessao depois do espelho da descarga continua o log da anterior */
    wchar_t eu[MAX_PATH] = L"";
    HMODULE m = nullptr;
    const bool copia = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                          reinterpret_cast<LPCWSTR>(&g_logFile), &m) &&
                       GetModuleFileNameW(m, eu, MAX_PATH) != 0 && wcsstr(eu, L".copia") != nullptr;
    /* C16: as duas escrevem ao mesmo tempo na entrega do espelho: sempre "a" (grava no fim) */
    if (!copia) { FILE* novo = _wfopen(caminho, L"w"); if (novo != nullptr) fclose(novo); }
    g_logFile = _wfopen(caminho, L"a");
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpcFreq = f.QuadPart > 0 ? f.QuadPart : 1;
}

static void Log(const char* fmt, ...)
{
    std::lock_guard<std::mutex> l(g_logMutex);
    if (g_logFile == nullptr) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(g_logFile, fmt, ap);
    va_end(ap);
    fputc('\n', g_logFile);
    fflush(g_logFile);
}

static void LogFechar()
{
    std::lock_guard<std::mutex> l(g_logMutex);
    if (g_logFile != nullptr) { fclose(g_logFile); g_logFile = nullptr; }
}

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
template <class T> static void Soltar(T*& p) { if (p) { p->Release(); p = nullptr; } }
static void FecharHandle(HANDLE& h) { if (h != nullptr && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = nullptr; }
static HANDLE   ComoHandle(uint64_t v) { return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(v)); }
static uint64_t ComoValor(HANDLE h)    { return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h)); }

static int64_t Agora() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double  Ms(int64_t de, int64_t ate) { return 1000.0 * double(ate - de) / double(g_qpcFreq); }

static const char* NomeSlot(int s)
{
    return s == SLOT_COLOR ? "Color" : s == SLOT_OUTPUT ? "Output" : s == SLOT_DEPTH ? "Depth" : "MV";
}

/* O formato tipado de um formato tipeless/sRGB do backbuffer -- o que vai no
   FeedBuild::color_fmt (o host aceita 28, 87, 24 = R10G10B10A2 e 10 = 16F). */
static DXGI_FORMAT FormatoTipado(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}
/* O host aceita exatamente estes quatro no color_fmt. */
static bool FormatoCorSuportado(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return true;
    default: return false;
    }
}
static bool MesmaFamilia(DXGI_FORMAT a, DXGI_FORMAT b) { return FormatoTipado(a) == FormatoTipado(b); }

/* ------------------------------------------------------------------ */
/* Remendo de vtable (restauravel). Um metodo COM e o ponteiro no slot   */
/* [indice] da vtable do objeto; trocamos por um nosso e guardamos o     */
/* original (o "trampolim"). VirtualProtect porque a vtable mora em      */
/* pagina de so-leitura. TODAS as instancias que compartilham a vtable   */
/* passam a chamar o nosso hook -- e o que queremos (o swapchain real do */
/* jogo tem a mesma vtable do descartavel).                              */
/* ------------------------------------------------------------------ */
static void** VtableDe(void* obj) { return *reinterpret_cast<void***>(obj); }

static bool PatchSlot(void** vt, int idx, void* novo, void** origOut)
{
    if (vt == nullptr) return false;
    DWORD velho = 0;
    if (!VirtualProtect(&vt[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &velho)) return false;
    if (origOut != nullptr) *origOut = vt[idx];
    vt[idx] = novo;
    VirtualProtect(&vt[idx], sizeof(void*), velho, &velho);
    return true;
}
/* Restaura so se a vtable ainda aponta para o NOSSO hook (nao pisar em cima
   de outro hook instalado por cima do nosso depois). */
static void RestaurarSlot(void** vt, int idx, void* nosso, void* orig)
{
    if (vt == nullptr || orig == nullptr) return;
    if (vt[idx] != nosso) return;   /* alguem enganchou por cima; nao mexer */
    DWORD velho = 0;
    if (!VirtualProtect(&vt[idx], sizeof(void*), PAGE_EXECUTE_READWRITE, &velho)) return;
    vt[idx] = orig;
    VirtualProtect(&vt[idx], sizeof(void*), velho, &velho);
}

/* Indices de vtable (referencia: kiero, e a doc da Microsoft). IUnknown
   ocupa 0..2 em toda interface COM. */
enum { VT_PRESENT = 8, VT_RESIZEBUFFERS = 13, VT_RESIZETARGET = 14, VT_PRESENT1 = 22 };
enum { VT_EXECUTECOMMANDLISTS = 10 };

} // namespace Ponte
