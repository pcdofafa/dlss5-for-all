/* ------------------------------------------------------------------
   dlss_sr_app.h - a feature DLSS Super Resolution do NGX criada NO APP
   (D3D11), para a sessao `.up` 0x200 do degrau 2 (Q2, 16/09/2026).

   O QUE E. Num jogo com FSR 2/3.1 ou XeSS (e sem DLSS) a DLL intercepta o
   dispatch do upscaler e nos manda a cor pequena, os vetores de movimento,
   a profundidade e a exposicao (ponte_feeder_up.h, sessao 0x200). Aqui o
   app resolve o `_nvngx.dll` do driver pelo registro (a mesma chave
   NGXCore\FullPath que a lib estatica do SDK consulta; e o que o
   bancada3\alvo-ngx\alvo64-ngx.cpp faz em D3D12), inicializa o NGX no
   device D3D11 do app, cria uma feature DLSS-SR com os dados do jogo
   (escala de MV, jitter, exposicao, reset, dt, flags) e avalia; o Output
   sai no tamanho de SAIDA do jogo e volta pela `PonteUp::Entregar`.

   O snippet `nvngx_dlss.dll` PRECISA estar ao lado do exe do app (o
   DriverStore nao o tem; `bancada2\bin-*\` ja o carrega): sem ele o Init
   passa e `SuperSampling.Available` volta 0.

   O objeto de parametros e a vtable na ordem do MSVC vem de
   ponte\cliente\ngx\ngx_contrato.h (G1): o mesmo contrato, um so lugar.

   Preset e qualidade vem do card "DLSS do jogo" (Q1): `Configurar()`.
   "jogo" = automatico (preset default do driver; qualidade pela razao
   interno/saida). Mudar recria a feature no proximo Avaliar.
   ------------------------------------------------------------------ */
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "ponte/cliente/ngx/ngx_contrato.h"

namespace DlssSrApp {

    typedef void (*LogFn)(const char* fmt, ...);
    static LogFn g_log = nullptr;
    static inline void Logf(const char* fmt, ...) {
        if (!g_log) return;
        char b[1024]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
        g_log("%s", b);
    }

    struct Entrada {
        ID3D11Texture2D* cor = nullptr;     // cor pequena (a saida da rede, ou a do jogo)
        ID3D11Texture2D* mv = nullptr;      // R16G16_FLOAT (interno ou saida), ou nullptr
        ID3D11Texture2D* depth = nullptr;   // R32_FLOAT, ou nullptr
        ID3D11Texture2D* expo = nullptr;    // 1x1 R32_FLOAT, ou nullptr (auto-exposicao)
        UINT rw = 0, rh = 0;                // renderSize deste quadro (subrect)
        UINT maxW = 0, maxH = 0;            // tamanho dos slots (o maximo da feature)
        UINT ow = 0, oh = 0;                // tamanho de saida
        DXGI_FORMAT outFmt = DXGI_FORMAT_UNKNOWN;
        float jx = 0, jy = 0, msx = 1, msy = 1, pre = 1, escExpo = 1, dt = 0, sharp = 0;
        bool reset = false, sharpen = false;
        int flagsNgx = 0;                   // DLSS.Feature.Create.Flags propostos pela DLL (HDR/MVLowRes/MVJittered/DepthInverted/AutoExposure)
        int depthInv = -1;                  // -1 = decidir por heuristica
    };

    // ---------------------------------------------------------------- estado
    typedef Ngx::Result (*PfnInit11)(unsigned long long, const wchar_t*, ID3D11Device*, unsigned);
    typedef Ngx::Result (*PfnShutdown11)(ID3D11Device*);
    typedef Ngx::Result (*PfnParams)(Ngx::Parameter**);
    typedef Ngx::Result (*PfnDestroyParams)(Ngx::Parameter*);
    typedef Ngx::Result (*PfnCreate11)(ID3D11DeviceContext*, unsigned, Ngx::Parameter*, Ngx::Handle**);
    typedef Ngx::Result (*PfnEvaluate11)(ID3D11DeviceContext*, const Ngx::Handle*, const Ngx::Parameter*, Ngx::ProgressCallback);
    typedef Ngx::Result (*PfnRelease11)(Ngx::Handle*);

    static ID3D11Device*        g_dev = nullptr;
    static ID3D11DeviceContext* g_ctx = nullptr;
    static HMODULE g_nvngx = nullptr;
    static PfnInit11 g_init = nullptr; static PfnShutdown11 g_shutdown = nullptr;
    static PfnParams g_capParams = nullptr, g_allocParams = nullptr; static PfnDestroyParams g_destroyParams = nullptr;
    static PfnCreate11 g_create = nullptr; static PfnEvaluate11 g_evaluate = nullptr; static PfnRelease11 g_release = nullptr;
    static bool g_ngxPronto = false, g_ngxFalhou = false;
    static char g_motivo[200] = "";

    static Ngx::Parameter* g_par = nullptr;
    static Ngx::Handle*    g_feature = nullptr;
    static ID3D11Texture2D* g_saida = nullptr;
    static ID3D11Texture2D* g_expoPadrao = nullptr;   // 1x1 = 1.0 quando o jogo nao manda exposicao e a auto-exposicao esta fora
    // o que a feature viva foi criada com
    static UINT g_fW = 0, g_fH = 0, g_fOW = 0, g_fOH = 0; static DXGI_FORMAT g_fOutFmt = DXGI_FORMAT_UNKNOWN;
    static int g_fFlags = -1; static unsigned g_fPreset = 0, g_fPqv = 0;
    // a escolha do card
    static std::wstring g_preset = L"jogo", g_qualidade = L"jogo";
    static unsigned g_presetValor = 0; static int g_pqvPedido = -1;
    // heuristica da depth
    static int g_depthInvDecidido = -1;
    // estatisticas
    static uint64_t g_avaliacoes = 0, g_falhas = 0; static double g_somaMs = 0.0; static unsigned g_criacoes = 0;
    static int64_t g_qpcFreq = 1;
    static inline int64_t Agora() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

    // ---------------------------------------------------------------- NGX
    static inline std::wstring CaminhoNvngx() {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return L"";
        wchar_t buf[1024]; DWORD tam = sizeof(buf), tipo = 0;
        const LONG r = RegQueryValueExW(k, L"FullPath", nullptr, &tipo, (LPBYTE)buf, &tam);
        RegCloseKey(k);
        if (r != ERROR_SUCCESS || (tipo != REG_SZ && tipo != REG_EXPAND_SZ)) return L"";
        std::wstring p(buf);
        if (!p.empty() && p.back() != L'\\') p += L'\\';
        return p + L"_nvngx.dll";
    }

    static inline bool NgxGarantir() {
        if (g_ngxPronto) return true;
        if (g_ngxFalhou || !g_dev) return false;
        g_ngxFalhou = true;   // qualquer saida antes do fim e falha definitiva (com motivo)
        g_nvngx = GetModuleHandleW(L"_nvngx.dll");
        if (!g_nvngx) {
            const std::wstring caminho = CaminhoNvngx();
            if (caminho.empty()) { snprintf(g_motivo, sizeof g_motivo, "a chave NGXCore\\FullPath nao existe (driver NVIDIA sem NGX?)"); Logf("sr-app: %s", g_motivo); return false; }
            g_nvngx = LoadLibraryW(caminho.c_str());
            if (!g_nvngx) { snprintf(g_motivo, sizeof g_motivo, "LoadLibraryW(_nvngx.dll) falhou %lu", GetLastError()); Logf("sr-app: %s", g_motivo); return false; }
            Logf("sr-app: _nvngx.dll carregado de %ls", caminho.c_str());
        } else Logf("sr-app: _nvngx.dll ja estava no processo");
        struct { const char* nome; void** destino; } tabela[] = {
            { "NVSDK_NGX_D3D11_Init",                    (void**)&g_init },
            { "NVSDK_NGX_D3D11_Shutdown1",               (void**)&g_shutdown },
            { "NVSDK_NGX_D3D11_GetCapabilityParameters", (void**)&g_capParams },
            { "NVSDK_NGX_D3D11_AllocateParameters",      (void**)&g_allocParams },
            { "NVSDK_NGX_D3D11_DestroyParameters",       (void**)&g_destroyParams },
            { "NVSDK_NGX_D3D11_CreateFeature",           (void**)&g_create },
            { "NVSDK_NGX_D3D11_EvaluateFeature",         (void**)&g_evaluate },
            { "NVSDK_NGX_D3D11_ReleaseFeature",          (void**)&g_release },
        };
        for (auto& e : tabela) {
            *e.destino = (void*)GetProcAddress(g_nvngx, e.nome);
            if (!*e.destino) { snprintf(g_motivo, sizeof g_motivo, "o _nvngx.dll nao exporta %s", e.nome); Logf("sr-app: %s", g_motivo); return false; }
        }
        wchar_t pasta[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, pasta, MAX_PATH);
        if (wchar_t* b = wcsrchr(pasta, L'\\')) *(b + 1) = 0;
        // 0x1000000: o AppId do host de referencia do Feeder e do alvo-ngx. Versao da API 0x15 (SDK 1.5).
        const Ngx::Result r = g_init(0x1000000ULL, pasta, g_dev, 0x15u);
        if (Ngx::Falhou(r)) { snprintf(g_motivo, sizeof g_motivo, "NVSDK_NGX_D3D11_Init 0x%08X (%s)", r, Ngx::NomeResultado(r)); Logf("sr-app: %s", g_motivo); return false; }
        Ngx::Parameter* cap = nullptr;
        if (Ngx::Falhou(g_capParams(&cap)) || !cap) { snprintf(g_motivo, sizeof g_motivo, "GetCapabilityParameters falhou"); Logf("sr-app: %s", g_motivo); return false; }
        int disponivel = 0, precisaDriver = 0, initResult = 0;
        Ngx::LerI(cap, "SuperSampling.Available", &disponivel);
        Ngx::LerI(cap, "SuperSampling.NeedsUpdatedDriver", &precisaDriver);
        Ngx::LerI(cap, "SuperSampling.FeatureInitResult", &initResult);
        Logf("sr-app: NGX D3D11 pronto: SuperSampling.Available=%d NeedsUpdatedDriver=%d FeatureInitResult=0x%08X (pasta %ls)", disponivel, precisaDriver, (unsigned)initResult, pasta);
        if (!disponivel) { snprintf(g_motivo, sizeof g_motivo, "DLSS SR indisponivel no driver (nvngx_dlss.dll esta ao lado do exe?)"); Logf("sr-app: %s", g_motivo); return false; }
        if (Ngx::Falhou(g_allocParams(&g_par)) || !g_par) { snprintf(g_motivo, sizeof g_motivo, "AllocateParameters falhou"); Logf("sr-app: %s", g_motivo); return false; }
        g_ngxFalhou = false; g_ngxPronto = true;
        return true;
    }

    // ---------------------------------------------------------------- interface
    static inline bool Ligar(ID3D11Device* dev, ID3D11DeviceContext* ctx, LogFn log) {
        if (!dev || !ctx) return false;
        if (g_dev) return true;
        g_log = log;
        g_dev = dev; g_dev->AddRef();
        g_ctx = ctx; g_ctx->AddRef();
        { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpcFreq = f.QuadPart > 0 ? f.QuadPart : 1; }
        return true;
    }

    static inline unsigned PresetDaLetra(const std::wstring& p) {
        if (p.size() != 1) return 0;
        const wchar_t c = (wchar_t)towupper(p[0]);
        if (c >= L'A' && c <= L'O') return (unsigned)(c - L'A' + 1);   // A=1 .. F=6, J=10, K=11, L=12
        return 0;
    }
    static inline int PqvDoTexto(const std::wstring& q) {
        if (q == L"quality") return Ngx::PQ_MAX_QUALITY;
        if (q == L"balanced") return Ngx::PQ_BALANCED;
        if (q == L"performance") return Ngx::PQ_MAX_PERF;
        if (q == L"ultra-performance") return Ngx::PQ_ULTRA_PERF;
        if (q == L"ultra-quality") return Ngx::PQ_ULTRA_QUALITY;
        if (q == L"dlaa") return Ngx::PQ_DLAA;
        return -1;
    }
    // Do card "DLSS do jogo". "jogo" = automatico. Vale no proximo Avaliar (recria a feature).
    static inline void Configurar(const std::wstring& preset, const std::wstring& qualidade) {
        g_preset = preset; g_qualidade = qualidade;
        g_presetValor = PresetDaLetra(preset);
        g_pqvPedido = PqvDoTexto(qualidade);
    }
    static inline unsigned PqvAutomatico(UINT rw, UINT rh, UINT ow, UINT oh) {
        const double r = (rw && rh) ? ((double)ow / rw + (double)oh / rh) * 0.5 : 1.0;
        if (r <= 1.05) return Ngx::PQ_DLAA;
        if (r <= 1.6)  return Ngx::PQ_MAX_QUALITY;
        if (r <= 1.85) return Ngx::PQ_BALANCED;
        if (r <= 2.5)  return Ngx::PQ_MAX_PERF;
        return Ngx::PQ_ULTRA_PERF;
    }
    static inline bool Ativa() { return g_feature != nullptr; }
    static inline const char* Motivo() { return g_motivo; }
    static inline char PresetEmUso() { return g_feature ? (g_fPreset ? Ngx::LetraPreset(g_fPreset) : '-') : '-'; }
    static inline const char* QualidadeEmUso() { return g_feature ? Ngx::NomePerfQuality(g_fPqv) : "-"; }

    // Heuristica da profundidade invertida: mediana < 0,5 -> reversed-Z (o longe e 0, e a maior
    // parte de uma cena e longe). Readback uma vez por sessao; so quando a DLL nao sabe.
    static inline int DecidirDepthInvertida(ID3D11Texture2D* depth, UINT rw, UINT rh) {
        if (!depth || !g_dev || !g_ctx) return 0;
        D3D11_TEXTURE2D_DESC td; depth->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.MiscFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* st = nullptr;
        if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &st)) || !st) return 0;
        g_ctx->CopyResource(st, depth);
        int resultado = 0;
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            uint64_t baixos = 0, total = 0, zeros = 0;
            const UINT w = rw ? (rw < td.Width ? rw : td.Width) : td.Width, h = rh ? (rh < td.Height ? rh : td.Height) : td.Height;
            for (UINT y = 0; y < h; y += 4) {
                const float* lin = reinterpret_cast<const float*>(static_cast<const BYTE*>(m.pData) + (size_t)y * m.RowPitch);
                for (UINT x = 0; x < w; x += 4) { const float v = lin[x]; if (v < 0.5f) ++baixos; if (v == 0.f) ++zeros; ++total; }
            }
            g_ctx->Unmap(st, 0);
            const double fracBaixos = total ? (double)baixos / (double)total : 0.0;
            resultado = fracBaixos > 0.5 ? 1 : 0;
            Logf("sr-app: depth invertida=%d (heuristica: %.0f%% dos texels abaixo de 0,5; %.0f%% iguais a 0; %ux%u)", resultado, 100.0 * fracBaixos, total ? 100.0 * (double)zeros / (double)total : 0.0, w, h);
        }
        st->Release();
        return resultado;
    }

    static inline void LiberarFeature() {
        if (g_feature && g_release) g_release(g_feature);
        g_feature = nullptr;
    }

    static inline bool SaidaGarantir(UINT ow, UINT oh, DXGI_FORMAT fmt) {
        if (g_saida) { D3D11_TEXTURE2D_DESC d; g_saida->GetDesc(&d); if (d.Width == ow && d.Height == oh && d.Format == fmt) return true; g_saida->Release(); g_saida = nullptr; }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = ow; td.Height = oh; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        HRESULT hr = g_dev->CreateTexture2D(&td, nullptr, &g_saida);
        if (FAILED(hr)) { td.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE; hr = g_dev->CreateTexture2D(&td, nullptr, &g_saida); }
        if (FAILED(hr) || !g_saida) { snprintf(g_motivo, sizeof g_motivo, "a textura de saida %ux%u fmt=%u com UAV nao nasceu 0x%08X", ow, oh, (unsigned)fmt, (unsigned)hr); Logf("sr-app: %s", g_motivo); g_saida = nullptr; return false; }
        return true;
    }

    static inline bool ExpoPadraoGarantir() {
        if (g_expoPadrao) return true;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_FLOAT; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const float um = 1.f; D3D11_SUBRESOURCE_DATA sd{ &um, 4, 4 };
        return SUCCEEDED(g_dev->CreateTexture2D(&td, &sd, &g_expoPadrao)) && g_expoPadrao;
    }

    // Cria (ou recria) a feature para esta entrada. Com o contexto imediato.
    static inline bool FeatureGarantir(const Entrada& e, int flags) {
        const unsigned pqv = g_pqvPedido >= 0 ? (unsigned)g_pqvPedido : PqvAutomatico(e.maxW, e.maxH, e.ow, e.oh);
        if (g_feature && g_fW == e.maxW && g_fH == e.maxH && g_fOW == e.ow && g_fOH == e.oh && g_fOutFmt == e.outFmt && g_fFlags == flags && g_fPreset == g_presetValor && g_fPqv == pqv) return true;
        LiberarFeature();
        Ngx::Parameter* p = g_par;
        Ngx::Vt(p)->Reset(p);
        Ngx::PorUI(p, Ngx::kWidth, e.maxW); Ngx::PorUI(p, Ngx::kHeight, e.maxH);
        Ngx::PorUI(p, Ngx::kOutWidth, e.ow); Ngx::PorUI(p, Ngx::kOutHeight, e.oh);
        Ngx::PorUI(p, Ngx::kPerfQuality, pqv);
        Ngx::PorI(p, Ngx::kCreateFlags, flags);
        Ngx::PorUI(p, "CreationNodeMask", 1u); Ngx::PorUI(p, "VisibilityNodeMask", 1u);
        Ngx::PorUI(p, Ngx::kPresetDLAA, g_presetValor); Ngx::PorUI(p, Ngx::kPresetQuality, g_presetValor); Ngx::PorUI(p, Ngx::kPresetBalanced, g_presetValor);
        Ngx::PorUI(p, Ngx::kPresetPerformance, g_presetValor); Ngx::PorUI(p, Ngx::kPresetUltraPerf, g_presetValor); Ngx::PorUI(p, Ngx::kPresetUltraQuality, g_presetValor);
        Ngx::PorUI(p, Ngx::kEnableOutputSubrects, 0u);
        const int64_t t0 = Agora();
        Ngx::Handle* h = nullptr;
        const Ngx::Result r = g_create(g_ctx, Ngx::FEATURE_SUPER_SAMPLING, p, &h);
        if (Ngx::Falhou(r) || !h) {
            snprintf(g_motivo, sizeof g_motivo, "CreateFeature(DLSS-SR) 0x%08X (%s) %ux%u->%ux%u flags=0x%X pqv=%u", r, Ngx::NomeResultado(r), e.maxW, e.maxH, e.ow, e.oh, (unsigned)flags, pqv);
            Logf("sr-app: %s", g_motivo);
            return false;
        }
        g_feature = h; ++g_criacoes;
        g_fW = e.maxW; g_fH = e.maxH; g_fOW = e.ow; g_fOH = e.oh; g_fOutFmt = e.outFmt; g_fFlags = flags; g_fPreset = g_presetValor; g_fPqv = pqv;
        Logf("sr-app: feature criada %ux%u->%ux%u preset=%c qualidade=%s flags=0x%X (%s%s%s%s%s) saida fmt=%u em %.0f ms (criacao %u)",
             e.maxW, e.maxH, e.ow, e.oh, g_presetValor ? Ngx::LetraPreset(g_presetValor) : '-', Ngx::NomePerfQuality(pqv), (unsigned)flags,
             (flags & Ngx::CF_HDR) ? "hdr " : "", (flags & Ngx::CF_MV_LOW_RES) ? "mv-lowres " : "mv-tela ", (flags & Ngx::CF_MV_JITTERED) ? "mv-jittered " : "",
             (flags & Ngx::CF_DEPTH_INVERTED) ? "depth-inv " : "", (flags & Ngx::CF_AUTO_EXPOSURE) ? "auto-expo" : "expo-tex",
             (unsigned)e.outFmt, 1000.0 * double(Agora() - t0) / double(g_qpcFreq), g_criacoes);
        return true;
    }

    // Avalia. *saida = a nossa textura (tamanho de saida, e.outFmt), valida ate o proximo Avaliar.
    static inline bool Avaliar(const Entrada& e, ID3D11Texture2D** saida, double* ms) {
        if (saida) *saida = nullptr;
        if (ms) *ms = 0.0;
        if (!e.cor || !e.ow || !e.oh || !e.rw || !e.rh) return false;
        if (!NgxGarantir()) return false;
        const int64_t t0 = Agora();
        int flags = e.flagsNgx & (Ngx::CF_HDR | Ngx::CF_MV_LOW_RES | Ngx::CF_MV_JITTERED | Ngx::CF_DEPTH_INVERTED | Ngx::CF_AUTO_EXPOSURE);
        if (e.depthInv > 0) flags |= Ngx::CF_DEPTH_INVERTED;
        else if (e.depthInv < 0) {
            if (g_depthInvDecidido < 0) g_depthInvDecidido = DecidirDepthInvertida(e.depth, e.rw, e.rh);
            if (g_depthInvDecidido > 0) flags |= Ngx::CF_DEPTH_INVERTED; else flags &= ~Ngx::CF_DEPTH_INVERTED;
        } else flags &= ~Ngx::CF_DEPTH_INVERTED;
        if (!e.expo) flags |= Ngx::CF_AUTO_EXPOSURE; else flags &= ~Ngx::CF_AUTO_EXPOSURE;
        if (!SaidaGarantir(e.ow, e.oh, e.outFmt)) return false;
        if (!FeatureGarantir(e, flags)) { ++g_falhas; return false; }

        Ngx::Parameter* p = g_par;
        Ngx::PorD3D11(p, Ngx::kColor, e.cor);
        Ngx::PorD3D11(p, Ngx::kOutput, g_saida);
        Ngx::PorD3D11(p, Ngx::kMotionVectors, e.mv);
        Ngx::PorD3D11(p, Ngx::kDepth, e.depth);
        if (e.expo) Ngx::PorD3D11(p, Ngx::kExposureTexture, e.expo);
        else if (ExpoPadraoGarantir()) Ngx::PorD3D11(p, Ngx::kExposureTexture, g_expoPadrao);
        Ngx::PorF(p, Ngx::kJitterX, e.jx); Ngx::PorF(p, Ngx::kJitterY, e.jy);
        Ngx::PorF(p, Ngx::kMvScaleX, e.msx != 0.f ? e.msx : 1.f); Ngx::PorF(p, Ngx::kMvScaleY, e.msy != 0.f ? e.msy : 1.f);
        Ngx::PorUI(p, Ngx::kRenderW, e.rw); Ngx::PorUI(p, Ngx::kRenderH, e.rh);
        Ngx::PorUI(p, Ngx::kColorBaseX, 0); Ngx::PorUI(p, Ngx::kColorBaseY, 0);
        Ngx::PorUI(p, Ngx::kDepthBaseX, 0); Ngx::PorUI(p, Ngx::kDepthBaseY, 0);
        Ngx::PorUI(p, Ngx::kMvBaseX, 0); Ngx::PorUI(p, Ngx::kMvBaseY, 0);
        Ngx::PorUI(p, Ngx::kOutBaseX, 0); Ngx::PorUI(p, Ngx::kOutBaseY, 0);
        Ngx::PorI(p, Ngx::kReset, e.reset ? 1 : 0);
        Ngx::PorF(p, Ngx::kSharpness, e.sharpen ? e.sharp : 0.f);
        Ngx::PorF(p, Ngx::kPreExposure, e.pre > 0.f ? e.pre : 1.f);
        Ngx::PorF(p, Ngx::kExposureScale, e.escExpo > 0.f ? e.escExpo : 1.f);
        Ngx::PorF(p, Ngx::kFrameTimeDelta, e.dt);
        const Ngx::Result r = g_evaluate(g_ctx, g_feature, p, nullptr);
        const double gasto = 1000.0 * double(Agora() - t0) / double(g_qpcFreq);
        if (Ngx::Falhou(r)) {
            if (++g_falhas <= 5) Logf("sr-app: EvaluateFeature 0x%08X (%s) no quadro %llu (falha %llu)", r, Ngx::NomeResultado(r), (unsigned long long)g_avaliacoes, (unsigned long long)g_falhas);
            snprintf(g_motivo, sizeof g_motivo, "EvaluateFeature 0x%08X (%s)", r, Ngx::NomeResultado(r));
            return false;
        }
        ++g_avaliacoes; g_somaMs += gasto;
        if ((g_avaliacoes % 300) == 0) { Logf("sr-app: %llu avaliacoes | sr %.2f ms (CPU, gravacao) | falhas %llu | criacoes %u", (unsigned long long)g_avaliacoes, g_somaMs / 300.0, (unsigned long long)g_falhas, g_criacoes); g_somaMs = 0.0; }
        if (saida) *saida = g_saida;
        if (ms) *ms = gasto;
        return true;
    }

    static inline uint64_t Falhas() { return g_falhas; }
    static inline uint64_t Avaliacoes() { return g_avaliacoes; }
    static inline void NovaSessao() { g_depthInvDecidido = -1; }

    static inline void Parar() {
        if (!g_dev) return;
        LiberarFeature();
        if (g_par && g_destroyParams) g_destroyParams(g_par);
        g_par = nullptr;
        if (g_saida) { g_saida->Release(); g_saida = nullptr; }
        if (g_expoPadrao) { g_expoPadrao->Release(); g_expoPadrao = nullptr; }
        if (g_ngxPronto && g_shutdown) g_shutdown(g_dev);
        g_ngxPronto = false; g_ngxFalhou = false;
        if (g_avaliacoes) Logf("sr-app: encerrado - %llu avaliacoes, %llu falhas, %u criacoes", (unsigned long long)g_avaliacoes, (unsigned long long)g_falhas, g_criacoes);
        g_ctx->Release(); g_ctx = nullptr;
        g_dev->Release(); g_dev = nullptr;
        g_fW = g_fH = g_fOW = g_fOH = 0; g_fFlags = -1;
    }

} // namespace DlssSrApp
