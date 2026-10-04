// =====================================================================================
//  fonte_ponte.h -- A2 (15/09/2026): liga o PonteHost (A1, ponte_feeder_host.h) as costuras
//  que ja existem no main.cpp. Header-only, no estilo da arvore (funcoes static, ASCII).
//
//  O QUE ESTE ARQUIVO FAZ
//   - ENTRADA:  Puxar() pega o quadro novo do jogo (PonteHost::ObterQuadro) e o expoe como
//               ID3D11ShaderResourceView - o mesmo papel do scap.srv da captura WGC, que o
//               laco poe em R.extSrv. O Renderer nao sabe de onde a textura veio.
//   - COLHEITA: ColherSaida() pega o quadro DEPOIS da cadeia (ReShade + Feeder + NR). Esse
//               quadro so existe DENTRO do Present (o ReShade roda no hook dele) e, em flip
//               model, o backbuffer fica indefinido depois do Present (ver o cabecalho do
//               addon\shared_frame.h). O unico lugar de onde ele sai e a publicacao que o
//               add-on faz no reshade_finish_effects (DLSS5Bridge_SetSharedOutput), lida pelo
//               shared_source.h. E EXATAMENTE o caminho que o Sintetiza (main.cpp) ja usa com
//               produtor e consumidor no mesmo processo - nada novo no add-on.
//   - SAIDA:    Devolver() entrega o resultado por PonteHost::EntregarSaida. O backbuffer do
//               app e SEMPRE B8G8R8A8_UNORM (Renderer::InitWaitable) e o FEED_OUTPUT do jogo e
//               o formato do swapchain DELE (R8G8B8A8 e o mais comum); CopyResource entre os
//               dois e um no-op silencioso. Por isso, quando formato ou tamanho nao batem, o
//               resultado passa por um blit (fullscreen triangle) para uma textura no formato
//               e tamanho do FEED_OUTPUT antes de ir para o A1.
//   - NUNCA DEIXA O JOGO PENDURADO: no modo pipelined o jogo enfileira Wait(out_fence, n) na
//               fila de GPU dele. Se um quadro puxado nao for devolvido, o jogo TRAVA. Por isso
//               Devolver(nullptr, n) entrega a propria cor (passagem direta) e FecharQuadro()
//               e a rede de seguranca no fim de cada volta do laco.
//
//  O QUE ESTE ARQUIVO NAO FAZ (e do main.cpp / A5): criar a janela, o device, decidir o
//  ritmo do laco, desligar captura/overlay/seletor. Ver relatorios\A2-integracao.md.
//
//  USO (o laco do main.cpp, resumido):
//      PonteHost::Preparar(GetCommandLineW(), &sessao, 30000);     // A5: no comeco do wWinMain, sem device
//      bbW = sessao.largura; bbH = sessao.altura;                  // o backbuffer nasce no tamanho do quadro
//      FontePonte::g_log = &Log; FontePonte::g_formatoSaida = sessao.formatoSaida;   // uma vez
//      FontePonte::Ligar(R.dev, R.ctx);                            // depois do R.Init (fase 2 do A1)
//      ...
//      got = FontePonte::Puxar(&R.extSrv, &w, &h, &n);             // no lugar do scap.Tick()
//      if (got) { R.DrawAndPresent(); ID3D11Texture2D* s = nullptr;
//                 FontePonte::Devolver(FontePonte::ColherSaida(&s) ? s : nullptr, n); }
//      FontePonte::FecharQuadro();                                 // fim da volta, sempre
//      ...
//      FontePonte::Desligar();                                     // antes do R.Shutdown
// =====================================================================================
#pragma once

#include <windows.h>
#include <shellapi.h>     // CommandLineToArgvW (MedirJanelaDoJogo)
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <string>

#include "ponte_feeder_host.h"   // A1: PonteHost::{Quadro, Iniciar, Ativa, ObterQuadro, EntregarSaida, Parar}
#include "ponte_feeder_up.h"     // G2 (fase 3): PonteUp::{Abrir, Ligar, ObterQuadro, Entregar} - a sessao upstream
#include "dlss_sr_app.h"         // Q2 (degrau 2): a feature DLSS SR do NGX criada no app, para a sessao 0x200
#include "shared_source.h"       // SharedSource::Reader (lado leitor da saida publicada pelo add-on)

#ifndef DLSS5_BRIDGE_MODULE_NAMEW
#define DLSS5_BRIDGE_MODULE_NAMEW L"dlss5-bridge.addon64"   // igual ao reshade_bridge.h
#endif

namespace FontePonte {

    // ------------------------------------------------------------------ log
    // O Log() do main.cpp e definido DEPOIS dos includes do topo; por isso o ponteiro. A5
    // faz `FontePonte::g_log = &Log;` uma vez. Sem ponteiro, vai para o OutputDebugString.
    static void (*g_log)(const char* fmt, ...) = nullptr;
    static inline void Logf(const char* fmt, ...) {
        char b[512];
        va_list ap; va_start(ap, fmt);
        vsnprintf(b, sizeof(b), fmt, ap);
        va_end(ap);
        b[sizeof(b) - 1] = 0;
        if (g_log) g_log("%s", b); else { OutputDebugStringA(b); OutputDebugStringA("\n"); }
    }

    // ------------------------------------------------------------------ estado
    static ID3D11Device*             g_dev = nullptr;
    static ID3D11DeviceContext*      g_ctx = nullptr;
    static bool                      g_ligada = false;

    // o quadro em maos (o ultimo que ObterQuadro devolveu)
    static PonteHost::Quadro         g_q{};
    static ID3D11Texture2D*          g_cor = nullptr;      // g_q.cor com AddRef (o SRV aponta para ela)
    static ID3D11ShaderResourceView* g_srv = nullptr;
    static bool                      g_pendente = false;   // Puxar devolveu e Devolver ainda nao rodou
    static uint64_t                  g_puxados = 0, g_devolvidos = 0, g_passagens = 0, g_adaptados = 0;
    static bool                      g_avisouHdr = false, g_avisouFmt = false, g_avisouAdapt = false;
    // P1 (16/09/2026): TETO DO NATIVO. No RE2 a rede rodava em cada quadro do jogo (1 passe
    // ~220 fps no jogo, 2 passes 60-100, 4 passes ~40), e acima da taxa do monitor os quadros
    // nem aparecem. O main.cpp poe aqui a taxa do monitor do jogo (0 = livre); quadro fora da
    // hora e devolvido SEM trabalho de GPU (so o Signal da out_fence): o Output guarda a
    // ultima saida processada e a DLL a mostra de novo. Com VRR o teto e a taxa maxima.
    static double                    g_tetoHz = 0.0;
    static double                    g_ritmoProximoMs = 0.0;   // prazo (QPC ms) do proximo quadro a processar
    static bool                      g_saidaValida = false;    // o Output desta textura ja recebeu uma entrega
    static uint64_t                  g_reaproveitados = 0;
    static uint64_t                  g_janPux = 0, g_janReap = 0;
    static int64_t                   g_janIni = 0;

    // saida: formato/tamanho do FEED_OUTPUT. UNKNOWN = deduzir do formato da cor (regra do
    // Feeder, FeedFmtOutputFor em feed_fmt.h). A5 pode fixar aqui o que o A1 souber de fato.
    static DXGI_FORMAT               g_formatoSaida = DXGI_FORMAT_UNKNOWN;
    // F0 (15/09/2026): o formato do backbuffer do app (Renderer::bbFmt), so para o log dizer
    // se a cadeia esta processando na faixa da cor (16F) ou em 8 bits. O main.cpp preenche.
    static DXGI_FORMAT               g_formatoBackbuffer = DXGI_FORMAT_UNKNOWN;
    static ID3D11Texture2D*          g_scratch = nullptr;  // resultado adaptado (formato/tamanho do OUTPUT)
    static ID3D11RenderTargetView*   g_scratchRtv = nullptr;
    static UINT                      g_scratchW = 0, g_scratchH = 0;
    static DXGI_FORMAT               g_scratchFmt = DXGI_FORMAT_UNKNOWN;
    static ID3D11VertexShader*       g_blitVs = nullptr;
    static ID3D11PixelShader*        g_blitPs = nullptr;
    static ID3D11SamplerState*       g_blitSamp = nullptr;
    static bool                      g_blitFalhou = false;

    // colheita: a saida da cadeia publicada pelo add-on no reshade_finish_effects
    typedef int (*SetSharedFn)(const wchar_t*);
    static SharedSource::Reader      g_leitor;
    static ID3D11Texture2D*          g_destino = nullptr;  // copia do quadro processado (formato do backbuffer)
    static uint32_t                  g_geracaoDestino = 0;
    static bool                      g_pediu = false, g_colheitaDesistiu = false;
    static ULONGLONG                 g_pediuMs = 0;
    static uint64_t                  g_colhidos = 0;

    template <class T> static inline void Soltar(T*& p) { if (p) { p->Release(); p = nullptr; } }

    static inline std::wstring NomeTransporte() {
        return L"dlss5-ponte-" + std::to_wstring((unsigned long)GetCurrentProcessId());
    }

    // ------------------------------------------------------------------ formatos
    // O SRV le a textura no formato em que o Feeder a criou: FeedFmtTypedColor() ja tipa o
    // backbuffer do jogo (sem _SRGB e sem TYPELESS), entao um desc nulo basta. Os TYPELESS
    // ficam cobertos por precaucao - um cliente futuro pode compartilhar a textura crua.
    static inline DXGI_FORMAT FormatoDeVista(DXGI_FORMAT f) {
        switch (f) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:     return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:     return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:     return DXGI_FORMAT_B8G8R8X8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:  return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return f;
        }
    }
    // Espelho de FeedFmtOutputFor (external\DLSS5-Feeder\src\feed_fmt.h): o FEED_OUTPUT que o
    // cliente pede para um dado formato de cor. Vale quando g_formatoSaida == UNKNOWN.
    static inline DXGI_FORMAT FormatoSaidaPara(DXGI_FORMAT cor) {
        switch (FormatoDeVista(cor)) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R11G11B10_FLOAT:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R10G10B10A2_UNORM:  return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM:     return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_UNORM:     return DXGI_FORMAT_B8G8R8A8_UNORM;
        default:                             return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
    }
    static inline bool EhHdr(DXGI_FORMAT f) {
        f = FormatoDeVista(f);
        return f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R11G11B10_FLOAT ||
               f == DXGI_FORMAT_R10G10B10A2_UNORM;
    }

    // ------------------------------------------------------------------ blit de adaptacao
    // Fullscreen triangle + sample. Existe so para o caso em que o resultado (backbuffer
    // B8G8R8A8 do tamanho do backbuffer) nao e byte a byte o que o FEED_OUTPUT aceita.
    static const char* const kBlitHlsl =
        "Texture2D tex : register(t0); SamplerState smp : register(s0);\n"
        "struct VSO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VSO VS(uint id : SV_VertexID) { VSO o; float2 uv = float2((id << 1) & 2, id & 2);\n"
        "  o.uv = uv; o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
        "float4 PS(VSO i) : SV_Target { return tex.Sample(smp, i.uv); }\n";

    static inline bool BlitGarantir() {
        if (g_blitVs && g_blitPs && g_blitSamp) return true;
        if (g_blitFalhou || !g_dev) return false;
        typedef HRESULT (WINAPI* PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                              LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
        HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
        PFN_Compile compile = dc ? (PFN_Compile)(void*)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!compile) { g_blitFalhou = true; Logf("fonte_ponte: d3dcompiler_47.dll indisponivel - sem adaptacao de formato na saida"); return false; }
        ID3DBlob *vsb = nullptr, *psb = nullptr, *eb = nullptr;
        HRESULT hr = compile(kBlitHlsl, strlen(kBlitHlsl), "fonte_ponte", nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsb, &eb);
        if (FAILED(hr)) { g_blitFalhou = true; Logf("fonte_ponte: VS do blit falhou 0x%08X", (unsigned)hr); Soltar(eb); return false; }
        Soltar(eb);
        hr = compile(kBlitHlsl, strlen(kBlitHlsl), "fonte_ponte", nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psb, &eb);
        if (FAILED(hr)) { g_blitFalhou = true; Logf("fonte_ponte: PS do blit falhou 0x%08X", (unsigned)hr); Soltar(eb); Soltar(vsb); return false; }
        Soltar(eb);
        g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_blitVs);
        g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_blitPs);
        vsb->Release(); psb->Release();
        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev->CreateSamplerState(&smp, &g_blitSamp);
        if (!g_blitVs || !g_blitPs || !g_blitSamp) { g_blitFalhou = true; Logf("fonte_ponte: shaders do blit nao nasceram"); return false; }
        return true;
    }

    static inline bool ScratchGarantir(UINT w, UINT h, DXGI_FORMAT fmt) {
        if (g_scratch && g_scratchW == w && g_scratchH == h && g_scratchFmt == fmt) return true;
        Soltar(g_scratchRtv); Soltar(g_scratch);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr = g_dev->CreateTexture2D(&td, nullptr, &g_scratch);
        if (FAILED(hr) || !g_scratch) { Logf("fonte_ponte: scratch %ux%u fmt=%u falhou 0x%08X", w, h, (unsigned)fmt, (unsigned)hr); return false; }
        hr = g_dev->CreateRenderTargetView(g_scratch, nullptr, &g_scratchRtv);
        if (FAILED(hr) || !g_scratchRtv) { Soltar(g_scratch); Logf("fonte_ponte: RTV do scratch falhou 0x%08X", (unsigned)hr); return false; }
        g_scratchW = w; g_scratchH = h; g_scratchFmt = fmt;
        return true;
    }

    // Desenha `fonte` inteira no scratch (w x h, fmt). O Renderer refaz todo o estado no
    // proximo DrawAndPresent, entao so se desamarra o que poderia atrapalhar (RT e SRV).
    static inline ID3D11Texture2D* Adaptar(ID3D11Texture2D* fonte, UINT w, UINT h, DXGI_FORMAT fmt) {
        if (!BlitGarantir() || !ScratchGarantir(w, h, fmt)) return nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        if (FAILED(g_dev->CreateShaderResourceView(fonte, nullptr, &srv)) || !srv) return nullptr;
        ID3D11RenderTargetView* nulos[1] = { nullptr };
        g_ctx->OMSetRenderTargets(1, &g_scratchRtv, nullptr);
        D3D11_VIEWPORT vp{ 0.f, 0.f, (float)w, (float)h, 0.f, 1.f };
        g_ctx->RSSetViewports(1, &vp);
        g_ctx->RSSetState(nullptr);
        g_ctx->OMSetDepthStencilState(nullptr, 0);
        g_ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        g_ctx->IASetInputLayout(nullptr);
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_ctx->VSSetShader(g_blitVs, nullptr, 0);
        g_ctx->GSSetShader(nullptr, nullptr, 0);
        g_ctx->PSSetShader(g_blitPs, nullptr, 0);
        g_ctx->PSSetShaderResources(0, 1, &srv);
        g_ctx->PSSetSamplers(0, 1, &g_blitSamp);
        g_ctx->Draw(3, 0);
        ID3D11ShaderResourceView* n = nullptr;
        g_ctx->PSSetShaderResources(0, 1, &n);
        g_ctx->OMSetRenderTargets(1, nulos, nullptr);
        srv->Release();
        return g_scratch;
    }

    // ------------------------------------------------------------------ interface (CONTRATO)
    // FASE 2 do PonteHost (A1, revisao 2): abre fences e texturas do jogo no device da rede.
    // A FASE 1 (PonteHost::Preparar - pipe, Hello, Build, tamanho/formato do quadro) e do
    // main.cpp (A5), no comeco do wWinMain, ANTES do device: o add-on so espera 15 s pelo pipe.
    // AJUSTE DO A5 (15/09): chamava PonteHost::Iniciar(GetCommandLineW(), dev, ctx), a forma de
    // 3 argumentos do CONTRATO. Com o Preparar ja feito ela cai na fase 2 do mesmo jeito - mas
    // se a sessao tivesse MORRIDO entre o Preparar e este ponto (jogo fechou durante o arranque),
    // g_ativa estaria false e a forma de 3 argumentos faria um Preparar NOVO: pipe novo, thread
    // nova, 20 s esperando um jogo que nao vai conectar. A fase 2 direta devolve false com
    // "ponte: Iniciar(dev, ctx) sem Preparar antes (ou a sessao ja encerrou)", que e a verdade.
    // false = a ponte nao subiu (o motivo esta no log). Chamar DEPOIS do R.Init.
    static inline bool Ligar(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
        if (g_ligada) return true;
        if (!dev || !ctx) return false;
        if (!PonteHost::Iniciar(dev, ctx)) return false;
        g_dev = dev; g_dev->AddRef();
        g_ctx = ctx; g_ctx->AddRef();
        g_ligada = true;
        g_ritmoProximoMs = 0.0; g_saidaValida = false; g_janPux = g_janReap = 0; g_janIni = 0;   // P1
        Logf("fonte_ponte: ligada (transporte da saida: %ls)", NomeTransporte().c_str());
        return true;
    }

    static inline bool Ativa() { return g_ligada && PonteHost::Ativa(); }

    // NO LACO, ANTES DE DESENHAR. true = ha quadro novo: *srvSaida aponta para a cor dele,
    // *w/*h e *n dizem tamanho e numero. false = nada novo; *srvSaida NAO e tocado (o laco
    // pode continuar apresentando o quadro anterior, como faz com o WGC parado).
    static inline bool Puxar(ID3D11ShaderResourceView** srvSaida, UINT* w, UINT* h, uint64_t* n) {
        if (!Ativa()) return false;
        if (g_pendente) {
            // O laco puxou de novo sem devolver: fecha o anterior por passagem direta em
            // vez de deixar o jogo esperando por um n que nunca vai ser sinalizado.
            Logf("fonte_ponte: quadro %llu puxado e nao devolvido - passagem direta", (unsigned long long)g_q.n);
            PonteHost::EntregarSaida(g_cor ? g_cor : g_q.cor, g_q.n);
            g_pendente = false; g_passagens++;
        }
        PonteHost::Quadro q{};
        if (!PonteHost::ObterQuadro(&q)) return false;
        if (!q.cor || !q.largura || !q.altura) {
            // quadro sem cor nao tem o que processar: sinaliza para o jogo nao travar
            PonteHost::EntregarSaida(q.cor, q.n);
            return false;
        }
        // O SRV so e refeito quando a textura muda (sessao nova, resize renegociado). A
        // comparacao e pelo ponteiro E pela descricao: um ponteiro reaproveitado pelo
        // alocador depois de uma renegociacao nao pode passar por "a mesma textura".
        const bool mesma = (g_cor == q.cor) && g_srv &&
                           g_q.largura == q.largura && g_q.altura == q.altura && g_q.formato == q.formato;
        if (!mesma) {
            Soltar(g_srv); Soltar(g_cor);
            D3D11_TEXTURE2D_DESC td{}; q.cor->GetDesc(&td);
            D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = FormatoDeVista(td.Format);
            sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MipLevels = 1;
            HRESULT hr = g_dev->CreateShaderResourceView(q.cor, &sd, &g_srv);
            if (FAILED(hr) || !g_srv) {
                if (!g_avisouFmt) { g_avisouFmt = true;
                    Logf("fonte_ponte: CreateShaderResourceView da cor falhou 0x%08X (fmt=%u %ux%u) - passagem direta",
                         (unsigned)hr, (unsigned)td.Format, td.Width, td.Height); }
                PonteHost::EntregarSaida(q.cor, q.n);
                return false;
            }
            g_cor = q.cor; g_cor->AddRef();
            g_saidaValida = false;   // P1: textura nova (sessao/resize) - o Output dela ainda nao tem saida
            Logf("fonte_ponte: quadro do jogo %ux%u fmt=%u (vista fmt=%u) -> SRV pronto; saida esperada fmt=%u",
                 q.largura, q.altura, (unsigned)td.Format, (unsigned)sd.Format,
                 (unsigned)(g_formatoSaida != DXGI_FORMAT_UNKNOWN ? g_formatoSaida : FormatoSaidaPara(td.Format)));
            if (EhHdr(td.Format) && !g_avisouHdr) { g_avisouHdr = true;
                // F0: a cor HDR/10 bits e processada na faixa dela quando o backbuffer do app e 16F
                // (decidido no arranque: Renderer::bbFmt) e o Output da sessao e o formato da cor.
                const DXGI_FORMAT saida = (g_formatoSaida != DXGI_FORMAT_UNKNOWN) ? g_formatoSaida : FormatoSaidaPara(td.Format);
                const bool bb16 = FormatoDeVista(g_formatoBackbuffer) == DXGI_FORMAT_R16G16B16A16_FLOAT;
                if (EhHdr(saida) && bb16)
                    Logf("fonte_ponte: a cor do jogo e HDR/10 bits (fmt=%u): cadeia em 16F (backbuffer fmt=%u) e saida fmt=%u - sem tone mapping, sem clipping",
                         (unsigned)td.Format, (unsigned)g_formatoBackbuffer, (unsigned)saida);
                else
                    Logf("fonte_ponte: a cor do jogo e HDR/10 bits (fmt=%u) mas a cadeia esta em %s (backbuffer fmt=%u, saida fmt=%u): a faixa acima do "
                         "branco de referencia sera CORTADA - reabra o app com [ultima] ponte_backbuffer=16f (ou modo Nativo num monitor HDR)",
                         (unsigned)td.Format, bb16 ? "16F" : "8 bits", (unsigned)g_formatoBackbuffer, (unsigned)saida); }
        }
        g_q = q;
        g_pendente = true;
        g_puxados++;
        if (srvSaida) *srvSaida = g_srv;
        if (w) *w = q.largura;
        if (h) *h = q.altura;
        if (n) *n = q.n;
        return true;
    }

    // DEPOIS que a cadeia terminou o quadro n. `resultado` e a saida processada (o que
    // ColherSaida devolveu). nullptr = nao houve saida (cadeia ainda subindo, Present pulado):
    // entrega a propria cor, para o jogo nunca ficar esperando a fence.
    // Formato/tamanho diferentes do FEED_OUTPUT passam pelo blit de adaptacao.
    static inline void Devolver(ID3D11Texture2D* resultado, uint64_t n) {
        if (!g_ligada) return;
        if (!g_pendente || n != g_q.n) {
            Logf("fonte_ponte: Devolver(%llu) sem quadro pendente (pendente=%d atual=%llu) - ignorado",
                 (unsigned long long)n, (int)g_pendente, (unsigned long long)g_q.n);
            return;
        }
        ID3D11Texture2D* fonte = resultado ? resultado : (g_cor ? g_cor : g_q.cor);
        if (!resultado) g_passagens++;
        const DXGI_FORMAT fmtSaida = (g_formatoSaida != DXGI_FORMAT_UNKNOWN) ? g_formatoSaida : FormatoSaidaPara(g_q.formato);
        D3D11_TEXTURE2D_DESC fd{}; if (fonte) fonte->GetDesc(&fd);
        ID3D11Texture2D* entrega = fonte;
        if (fonte && (fd.Format != fmtSaida || fd.Width != g_q.largura || fd.Height != g_q.altura)) {
            ID3D11Texture2D* a = Adaptar(fonte, g_q.largura, g_q.altura, fmtSaida);
            if (a) {
                entrega = a; g_adaptados++;
                if (!g_avisouAdapt) { g_avisouAdapt = true;
                    Logf("fonte_ponte: saida adaptada por blit: %ux%u fmt=%u -> %ux%u fmt=%u (CopyResource nao serviria)",
                         fd.Width, fd.Height, (unsigned)fd.Format, g_q.largura, g_q.altura, (unsigned)fmtSaida); }
            } else if (!g_avisouAdapt) { g_avisouAdapt = true;
                Logf("fonte_ponte: blit de adaptacao indisponivel - entregando %ux%u fmt=%u como esta (o A1 precisa converter)",
                     fd.Width, fd.Height, (unsigned)fd.Format); }
        }
        PonteHost::EntregarSaida(entrega, n);
        g_pendente = false;
        g_saidaValida = (entrega != nullptr);   // P1
        g_devolvidos++;
        if ((g_devolvidos % 600) == 0)
            Logf("fonte_ponte: %llu puxados, %llu devolvidos (%llu colhidos da cadeia, %llu passagens diretas, %llu adaptados)",
                 (unsigned long long)g_puxados, (unsigned long long)g_devolvidos, (unsigned long long)g_colhidos,
                 (unsigned long long)g_passagens, (unsigned long long)g_adaptados);
    }

    // P1: LOGO DEPOIS de um Puxar que deu true. true = o quadro n NAO e hora (acima do teto):
    // ja foi devolvido sem trabalho de GPU (so o Signal), a DLL mostra de novo a ultima saida,
    // e o laco segue como se nada tivesse chegado. false = processar como sempre. A grade de
    // prazos anda de periodo em periodo com meia folga: jogo abaixo do teto, com jitter, passa
    // inteiro; acima, a media sai no teto. Contagem de fences intacta (1 Signal por quadro).
    static inline bool PularEste(uint64_t n) {
        if (!g_ligada || !g_pendente || n != g_q.n) return false;
        LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f);
        const double agora = 1000.0 * (double)t.QuadPart / (double)f.QuadPart;
        bool pular = false;
        if (g_tetoHz > 0.0 && g_saidaValida) {
            const double periodo = 1000.0 / g_tetoHz;
            if (g_ritmoProximoMs > 0.0 && agora + periodo * 0.5 < g_ritmoProximoMs) pular = true;
            else g_ritmoProximoMs = (g_ritmoProximoMs <= 0.0 || agora - g_ritmoProximoMs > periodo) ? agora + periodo : g_ritmoProximoMs + periodo;
        } else {
            g_ritmoProximoMs = 0.0;
        }
        if (pular) {
            PonteHost::EntregarSaida(nullptr, n);   // sem copia: o Output fica com a ultima saida
            g_pendente = false;
            g_devolvidos++; g_reaproveitados++; g_janReap++;
        }
        if (g_janIni == 0) g_janIni = t.QuadPart;
        if (++g_janPux >= 300) {
            const double seg = (double)(t.QuadPart - g_janIni) / (double)f.QuadPart;
            if (seg > 0.0)
                Logf("ponte: ritmo jogo %.1f fps | processados %.1f fps | reaproveitados %llu | teto %s",
                     (double)g_janPux / seg, (double)(g_janPux - g_janReap) / seg, (unsigned long long)g_janReap,
                     g_tetoHz > 0.0 ? (std::to_string((int)(g_tetoHz + 0.5)) + " Hz").c_str() : "livre");
            g_janPux = g_janReap = 0; g_janIni = t.QuadPart;
        }
        return pular;
    }

    static inline void Desligar() {
        if (!g_ligada) return;
        if (g_pendente) { PonteHost::EntregarSaida(g_cor ? g_cor : g_q.cor, g_q.n); g_pendente = false; }
        PonteHost::Parar();
        // a publicacao no add-on e desligada (nome nulo), como o Sintetiza faz ao sair
        HMODULE mod = GetModuleHandleW(DLSS5_BRIDGE_MODULE_NAMEW);
        SetSharedFn f = mod ? reinterpret_cast<SetSharedFn>(reinterpret_cast<void*>(GetProcAddress(mod, "DLSS5Bridge_SetSharedOutput"))) : nullptr;
        if (f && g_pediu) f(nullptr);
        g_leitor.Fechar();
        Soltar(g_destino); g_geracaoDestino = 0;
        Soltar(g_srv); Soltar(g_cor);
        Soltar(g_scratchRtv); Soltar(g_scratch); g_scratchW = g_scratchH = 0; g_scratchFmt = DXGI_FORMAT_UNKNOWN;
        Soltar(g_blitVs); Soltar(g_blitPs); Soltar(g_blitSamp);
        Logf("fonte_ponte: desligada - %llu puxados, %llu devolvidos, %llu colhidos, %llu passagens diretas, %llu adaptados, %llu reaproveitados",
             (unsigned long long)g_puxados, (unsigned long long)g_devolvidos, (unsigned long long)g_colhidos,
             (unsigned long long)g_passagens, (unsigned long long)g_adaptados, (unsigned long long)g_reaproveitados);
        Soltar(g_ctx); Soltar(g_dev);
        g_ligada = false; g_pediu = false; g_colheitaDesistiu = false;
        g_q = PonteHost::Quadro{};
    }

    // ------------------------------------------------------------------ extras (fora do contrato, so ajudam o A5)

    // A SAIDA DA CADEIA. Chamar logo DEPOIS do R.DrawAndPresent() do quadro puxado. true = o
    // add-on publicou um quadro novo e ele foi copiado para *out (textura nossa, no formato e
    // tamanho do backbuffer). false = ainda nao ha publicacao (efeitos compilando, add-on
    // antigo, Present nao rodou a cadeia): devolva nullptr ao Devolver.
    // Mesmo desenho do Sintetiza::Preparar/GuardarProcessado: pedir uma vez, abrir sem
    // esperar (esperar aqui travaria o laco que produz o quadro), desistir com prazo.
    static inline bool ColherSaida(ID3D11Texture2D** out) {
        if (out) *out = nullptr;
        if (!g_ligada || g_colheitaDesistiu || !g_dev) return false;
        HMODULE mod = GetModuleHandleW(DLSS5_BRIDGE_MODULE_NAMEW);
        SetSharedFn setShared = mod ? reinterpret_cast<SetSharedFn>(reinterpret_cast<void*>(GetProcAddress(mod, "DLSS5Bridge_SetSharedOutput"))) : nullptr;
        if (!setShared) {
            if (mod) { g_colheitaDesistiu = true; Logf("fonte_ponte: o dlss5-bridge.addon64 nao tem DLSS5Bridge_SetSharedOutput - a saida vai por passagem direta (SEM efeitos)"); }
            return false;   // o ReShade ainda pode nao ter carregado o add-on: tenta na proxima volta
        }
        if (!g_pediu) {
            g_pediu = true; g_pediuMs = GetTickCount64();
            setShared(NomeTransporte().c_str());
            Logf("fonte_ponte: pedi ao add-on a publicacao da saida (%ls); abrindo quando ela nascer", NomeTransporte().c_str());
            return false;   // o mapeamento so existe depois do proximo finish_effects
        }
        std::wstring erro;
        if (!g_leitor.Aberto()) {
            if (!g_leitor.Abrir(NomeTransporte(), g_dev, &erro)) {
                // 30 s: o aquecimento da cadeia tem teto de 30 s e a publicacao so comeca
                // quando os efeitos compilam. Passar disso e add-on mudo, nao demora.
                if (GetTickCount64() - g_pediuMs > 30000) {
                    g_colheitaDesistiu = true;
                    Logf("fonte_ponte: o add-on nao publicou a saida em 30 s (%ls) - passagem direta daqui em diante", erro.c_str());
                    setShared(nullptr);
                }
                return false;
            }
        }
        // destino no tamanho/formato que o produtor publicou; refeito a cada geracao
        // (mudanca de resolucao) - CopyResource entre tamanhos diferentes e no-op silencioso.
        const uint32_t ger = g_leitor.Geracao();
        if (ger == 0) return false;
        if (!g_destino || ger != g_geracaoDestino) {
            Soltar(g_destino);
            if (!g_leitor.CriarDestino(&g_destino, &erro)) {
                Logf("fonte_ponte: destino da colheita falhou (%ls)", erro.c_str());
                return false;
            }
            g_geracaoDestino = ger;
            Logf("fonte_ponte: colheita pronta - transporte %s %ux%u fmt=%u geracao %u",
                 g_leitor.ModoTexto(), g_leitor.Largura(), g_leitor.Altura(), (unsigned)g_leitor.Formato(), (unsigned)ger);
        }
        const int r = g_leitor.Ler(g_destino, &erro);
        if (r == 1) { g_colhidos++; if (out) *out = g_destino; return true; }
        if (r < 0) {
            // caiu (o add-on parou de publicar por 500 ms): reabre na proxima volta, sem
            // desistir - a cadeia pode so ter engasgado.
            Logf("fonte_ponte: a publicacao da saida caiu (%ls) - reabrindo", erro.c_str());
            g_leitor.Fechar();
            Soltar(g_destino); g_geracaoDestino = 0;
        }
        return false;
    }

    // REDE DE SEGURANCA, no fim de cada volta do laco: se um quadro foi puxado e ninguem o
    // devolveu (Present pulado por teto/pulo/repouso, excecao no meio), entrega a cor crua.
    static inline void FecharQuadro() {
        if (g_ligada && g_pendente) Devolver(nullptr, g_q.n);
    }

    static inline bool        QuadroPendente() { return g_pendente; }
    static inline uint64_t    NumeroQuadro()   { return g_q.n; }
    static inline UINT        LarguraQuadro()  { return g_q.largura; }
    static inline UINT        AlturaQuadro()   { return g_q.altura; }
    static inline DXGI_FORMAT FormatoQuadro()  { return g_q.formato; }
    static inline int64_t     QpcChegada()     { return g_q.qpcChegada; }

    // J5 (18/09/2026): DEPTH E MV REAIS DO JOGO NESTE QUADRO, para o frame generation da pelicula
    // (fg_ffx.h, PublicarDadosJogo). Vale SO entre Puxar e Devolver: os slots sao do jogo e ele os
    // reescreve depois da out_fence deste quadro - quem usar tem de COPIAR (no contexto imediato,
    // antes do Devolver, que e o que enfileira o Signal). false = quadro sem 'D' (jogo sem
    // upscaler enganchado, "antes do upscaling" desligado, DLL anterior ao J5): fluxo optico.
    static inline bool DadosDoQuadro(PonteHost::DadosJogo* d, ID3D11Texture2D** depth, ID3D11Texture2D** mv) {
        if (!g_ligada || !g_pendente || !g_q.dados.valido || !g_q.depth || !g_q.mv) return false;
        if (d) *d = g_q.dados;
        if (depth) *depth = g_q.depth;
        if (mv) *mv = g_q.mv;
        return true;
    }

    // O prazo da espera do laco (MsgWaitForMultipleObjects) quando a ponte esta ativa. NAO E
    // MAIS USADO pelo main.cpp (ajuste do A5, 15/09): o A1 expos PonteHost::EventoQuadro(), um
    // evento auto-reset que dispara quando ha quadro novo, e o laco espera NELE (waitEvt) em
    // vez de consultar a cada 1 ms. Fica aqui para quem nao tiver o evento.
    static inline DWORD EsperaMs(DWORD padrao) { return Ativa() ? 1 : padrao; }
    static inline HANDLE EventoQuadro() { return PonteHost::EventoQuadro(); }

    // ------------------------------------------------------------------ janela do jogo
    // DESDE A REVISAO 2 DO A1 O TAMANHO DO QUADRO VEM DO FeedBuild (PonteHost::Preparar,
    // Sessao::largura/altura), negociado ANTES de o backbuffer nascer: o main.cpp cria o
    // backbuffer nesse tamanho e nao ha mais "fonte mudou" no primeiro quadro. O que sobrou
    // aqui e so achar o HWND do jogo pelo PID, para o titulo, o painel e os avisos - o
    // tamanho medido (w/h) e informativo. (O texto antigo descrevia a medida da janela como
    // origem do backbuffer; deixou de ser.)
    static inline DWORD PidDaLinhaDeComando() {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        DWORD pid = 0;
        if (argv) {
            for (int i = 1; i < argc && !pid; i++) {
                const wchar_t* a = argv[i];
                if (!a || !a[0] || a[0] == L'-') continue;
                wchar_t* fim = nullptr;
                const unsigned long v = wcstoul(a, &fim, 10);
                if (fim && *fim == L'\0' && v != 0) pid = (DWORD)v;
            }
            LocalFree(argv);
        }
        return pid;
    }
    struct AchaJanela { DWORD pid; HWND melhor; long area; };
    static inline BOOL CALLBACK AchaJanelaProc(HWND h, LPARAM lp) {
        AchaJanela* a = reinterpret_cast<AchaJanela*>(lp);
        DWORD p = 0; GetWindowThreadProcessId(h, &p);
        if (p != a->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER) != nullptr) return TRUE;
        RECT r{}; if (!GetClientRect(h, &r)) return TRUE;
        const long area = (long)(r.right - r.left) * (long)(r.bottom - r.top);
        if (area > a->area) { a->area = area; a->melhor = h; }
        return TRUE;
    }
    // true com *w/*h = area util da maior janela visivel do processo `pid` (0 = pega o PID
    // da linha de comando). false = sem PID ou sem janela: o chamador escolhe o tamanho.
    static inline bool MedirJanelaDoJogo(DWORD pid, UINT* w, UINT* h, HWND* janela = nullptr) {
        if (!pid) pid = PidDaLinhaDeComando();
        if (!pid) return false;
        AchaJanela a{ pid, nullptr, 0 };
        EnumWindows(&AchaJanelaProc, reinterpret_cast<LPARAM>(&a));
        if (!a.melhor || a.area <= 0) return false;
        RECT r{}; GetClientRect(a.melhor, &r);
        if (w) *w = (UINT)(r.right - r.left);
        if (h) *h = (UINT)(r.bottom - r.top);
        if (janela) *janela = a.melhor;
        return true;
    }

} // namespace FontePonte

/* ==================================================================
   FonteUpstream - o lado do app da sessao "upstream" (fase 3, G2).

   O QUE MUDA EM RELACAO A FontePonte. Na fase 2 o quadro que chega e o
   FINAL do jogo, do tamanho do backbuffer, e a cadeia inteira roda em
   cima dele. Aqui o que chega e a COR PEQUENA do `EvaluateFeature` do
   DLSS - a imagem na resolucao INTERNA, antes do super resolution -, e a
   regra e uma so: **nada pode reamostrar essa cor**. Se a cor pequena
   fosse esticada ate o backbuffer para a cadeia rodar, o app estaria
   fazendo o super resolution que e justamente o trabalho do DLSS do jogo,
   e duas vezes seguidas - o contrario do que a fase 3 existe para provar.

   COMO A REDE RODA NA RESOLUCAO INTERNA SEM MEXER NO BACKBUFFER. A rede
   deste app nao e uma funcao que da para chamar: ela e avaliada pelo
   add-on DENTRO do Present (`R.DrawAndPresent` -> ReShade -> add-on ->
   saida publicada, que o `FontePonte::ColherSaida` le). Ou seja, quem
   decide em que resolucao a rede ve os pixels e o TAMANHO DA FONTE que o
   Renderer desenha, e o backbuffer nao pode mudar de tamanho em voo
   (`Renderer::FonteMudou`, e o F0 mediu o preco de tentar). A saida e
   geometrica e nao custa nada: a cor pequena e copiada **1:1, no canto
   superior esquerdo** de uma moldura do tamanho do backbuffer (o resto
   fica preto), a cadeia roda nessa moldura, e no fim recortamos de volta
   o mesmo retangulo do canto. A rede ve os pixels da cor interna na
   densidade em que eles existem - nenhum filtro toca neles no caminho de
   ida nem no de volta. O preco e a cadeia processar uma moldura maior do
   que o necessario; e por isso que existe o orcamento de 4 ms abaixo, e
   e ele quem diz se o upstream vale a pena nesta maquina.

   A CURVA REVERSIVEL DO F0. Quando a cor e R16G16B16A16_FLOAT, a rede
   comprime tudo o que esta acima do branco de referencia para ~1,17 (o F0
   mediu). A saida e a mesma do F0: entrar na curva `y = srgb(x/(1+x))`
   antes da rede e sair por `x = r/(1-r)`, `r = srgb^-1(y)` limitado a
   0,98, depois dela. As duas passagens ja existiam aqui de graca: a curva
   anda junto com a copia para a moldura e com o recorte de volta, sem um
   unico dispatch a mais.

   O ORCAMENTO. 4 ms por avaliacao, contados de `Puxar` a `Devolver` (a
   volta inteira do laco, Present incluido - e o que o jogo espera de
   fato). Estourou: o resultado e jogado fora e o quadro volta como
   passa-through (Color->Output, cor intocada), e conta como atraso.
   Acima de 5 % de atrasos numa janela de 10 s o upstream se desliga
   sozinho, com cartaz curto, e a fase 2 segue como antes.
   ================================================================== */
namespace FonteUpstream {

    using FontePonte::Logf;
    using FontePonte::Soltar;
    using FontePonte::EhHdr;
    using FontePonte::FormatoDeVista;

    static ID3D11Device*        g_dev = nullptr;
    static ID3D11DeviceContext* g_ctx = nullptr;
    static bool g_ligada = false;

    // O tamanho e o formato do backbuffer do app, preenchidos pelo main antes de Ligar.
    // `g_srcW/g_srcH` sao o que o Renderer espera como FONTE: quando os dois pares nao
    // batem, desenhar a moldura faria o Renderer reamostrar e a regra de cima cairia.
    static UINT        g_bbW = 0, g_bbH = 0;
    static UINT        g_srcW = 0, g_srcH = 0;
    static DXGI_FORMAT g_bbFmt = DXGI_FORMAT_UNKNOWN;
    // Cartaz curto quando o upstream se desliga sozinho (o main aponta para o ShowToast).
    static void (*g_cartaz)(const wchar_t* texto) = nullptr;

    // shaders: o mesmo triangulo de tela cheia, com uma escala de uv e um modo de curva
    static ID3D11VertexShader* g_vs = nullptr;
    static ID3D11PixelShader*  g_ps = nullptr;
    static ID3D11SamplerState* g_samp = nullptr;
    static ID3D11Buffer*       g_cb = nullptr;
    static bool g_shadersFalharam = false;

    static ID3D11Texture2D*          g_moldura = nullptr;   // do tamanho do backbuffer: entra na cadeia
    static ID3D11RenderTargetView*   g_molduraRtv = nullptr;
    static ID3D11ShaderResourceView* g_molduraSrv = nullptr;
    static ID3D11Texture2D*          g_recorte = nullptr;   // do tamanho interno: volta para a DLL
    static ID3D11RenderTargetView*   g_recorteRtv = nullptr;

    static PonteUp::QuadroUp g_q{};
    static bool     g_pendente = false;
    static int64_t  g_qpcPuxar = 0, g_qpcFreq = 1;
    // A CONTA DA VOLTA, em pedaços. A primeira fumaça mediu 117 avaliações/s (8,5 ms por volta)
    // com a rede em 1,3 ms, e não havia como saber onde estavam os outros 7 ms sem isto. Cada
    // pedaço tem dono diferente: `espera` é o laço demorando a notar o quadro, `entrada` é a
    // nossa cópia para a moldura, `cadeia` é o Present inteiro (onde a rede roda), `saida` é o
    // recorte e a entrega, e `periodo` é o intervalo entre duas voltas - a soma dos quatro
    // contra o período diz se o que falta é trabalho nosso ou espera pelo jogo/GPU.
    static int64_t  g_qpcEntrou = 0, g_qpcAnterior = 0;
    static double   g_somaEspera = 0.0, g_somaEntrada = 0.0, g_somaCadeia = 0.0;
    static double   g_somaSaida = 0.0, g_somaPeriodo = 0.0;
    static uint64_t g_avaliados = 0, g_atrasados = 0;
    // janela de 10 s do desligamento automatico
    static ULONGLONG g_janelaMs = 0;
    static uint64_t  g_janelaQuadros = 0, g_janelaAtrasos = 0;
    static bool g_avisouMoldura = false, g_avisouTamanho = false;
    static const double kOrcamentoMs = 4.0;
    // Q2 (degrau 2): a sessao 0x200. O jogo usa FSR/XeSS; a DLL manda a cor pequena e o app faz o
    // DLSS SR (dlss_sr_app.h) e devolve no tamanho de SAIDA. `g_redeNoSr` = [ultima] nativo_upstream:
    // com 1 a rede roda ANTES do SR na cor pequena (pela cadeia, como no upstream de sempre); com 0
    // o SR roda direto na cor do jogo, sem a cadeia (o main poe o valor em UpstreamAbrir).
    static bool     g_redeNoSr = true;
    static uint64_t g_srAvaliadas = 0, g_srFalhas = 0; static unsigned g_srFalhasSeguidas = 0;
    static double   g_srSomaMs = 0.0, g_srSomaTotal = 0.0;
    static bool     g_srLogouDireto = false, g_srLogouFmt = false;

    static inline int64_t Agora() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
    static inline double  Ms(int64_t de, int64_t ate) { return 1000.0 * double(ate - de) / double(g_qpcFreq); }

    // modo 0 = copia crua, 1 = entra na curva do F0, 2 = sai dela.
    static const char* const kUpHlsl =
        "cbuffer Cb : register(b0) { float2 gUv; uint gModo; float gPad; };\n"
        "Texture2D tex : register(t0); SamplerState smp : register(s0);\n"
        "struct VSO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
        "VSO VS(uint id : SV_VertexID) { VSO o; float2 uv = float2((id << 1) & 2, id & 2);\n"
        "  o.uv = uv * gUv; o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
        "float3 Srgb(float3 c) { c = max(c, 0.0);\n"
        "  return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(max(c, 1e-6), 1.0 / 2.4) - 0.055; }\n"
        "float3 SrgbInv(float3 c) { c = saturate(c);\n"
        "  return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }\n"
        "float4 PS(VSO i) : SV_Target {\n"
        "  float4 c = tex.Sample(smp, i.uv);\n"
        "  if (gModo == 1) { float3 x = max(c.rgb, 0.0); c.rgb = Srgb(x / (1.0 + x)); }\n"
        "  else if (gModo == 2) { float3 r = min(SrgbInv(c.rgb), 0.98); c.rgb = r / (1.0 - r); }\n"
        "  return c; }\n";

    static inline bool ShadersGarantir() {
        if (g_vs && g_ps && g_samp && g_cb) return true;
        if (g_shadersFalharam || !g_dev) return false;
        typedef HRESULT (WINAPI* PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
                                              LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
        HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
        PFN_Compile compile = dc ? (PFN_Compile)(void*)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!compile) { g_shadersFalharam = true; Logf("upstream: d3dcompiler_47.dll indisponivel"); return false; }
        ID3DBlob *vsb = nullptr, *psb = nullptr, *eb = nullptr;
        HRESULT hr = compile(kUpHlsl, strlen(kUpHlsl), "upstream", nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsb, &eb);
        if (FAILED(hr)) { g_shadersFalharam = true; Logf("upstream: VS falhou 0x%08X", (unsigned)hr); Soltar(eb); return false; }
        Soltar(eb);
        hr = compile(kUpHlsl, strlen(kUpHlsl), "upstream", nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psb, &eb);
        if (FAILED(hr)) { g_shadersFalharam = true; Logf("upstream: PS falhou 0x%08X", (unsigned)hr); Soltar(eb); Soltar(vsb); return false; }
        Soltar(eb);
        g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_vs);
        g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps);
        vsb->Release(); psb->Release();
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;   // 1:1: ponto, para nenhum texel se misturar com o vizinho
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        g_dev->CreateSamplerState(&sd, &g_samp);
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_dev->CreateBuffer(&bd, nullptr, &g_cb);
        if (!g_vs || !g_ps || !g_samp || !g_cb) { g_shadersFalharam = true; Logf("upstream: os shaders da moldura nao nasceram"); return false; }
        return true;
    }

    static inline bool AlvoGarantir(ID3D11Texture2D** tex, ID3D11RenderTargetView** rtv,
                                   ID3D11ShaderResourceView** srv, UINT w, UINT h, DXGI_FORMAT fmt) {
        if (*tex) {
            D3D11_TEXTURE2D_DESC d{}; (*tex)->GetDesc(&d);
            if (d.Width == w && d.Height == h && d.Format == fmt) return true;
            if (srv) Soltar(*srv);
            Soltar(*rtv); Soltar(*tex);
        }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_dev->CreateTexture2D(&td, nullptr, tex)) || !*tex) {
            Logf("upstream: textura %ux%u fmt=%u falhou", w, h, (unsigned)fmt); return false; }
        if (FAILED(g_dev->CreateRenderTargetView(*tex, nullptr, rtv)) || !*rtv) { Soltar(*tex); return false; }
        if (srv && (FAILED(g_dev->CreateShaderResourceView(*tex, nullptr, srv)) || !*srv)) { Soltar(*rtv); Soltar(*tex); return false; }
        return true;
    }

    // Uma passagem: desenha `fonte` no `rtv`, num retangulo w x h do canto, com a escala de
    // uv e o modo de curva pedidos. O Renderer refaz o estado inteiro no proximo
    // DrawAndPresent, entao so se desamarra o que atrapalharia (RT e SRV).
    static inline void Passagem(ID3D11ShaderResourceView* fonte, ID3D11RenderTargetView* rtv,
                                UINT w, UINT h, float uvX, float uvY, unsigned modo) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(g_ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            float* f = (float*)m.pData;
            f[0] = uvX; f[1] = uvY;
            ((unsigned*)m.pData)[2] = modo;
            f[3] = 0.f;
            g_ctx->Unmap(g_cb, 0);
        }
        ID3D11RenderTargetView* nulos[1] = { nullptr };
        g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT vp{ 0.f, 0.f, (float)w, (float)h, 0.f, 1.f };
        g_ctx->RSSetViewports(1, &vp);
        g_ctx->RSSetState(nullptr);
        g_ctx->OMSetDepthStencilState(nullptr, 0);
        g_ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        g_ctx->IASetInputLayout(nullptr);
        g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_ctx->VSSetShader(g_vs, nullptr, 0);
        g_ctx->GSSetShader(nullptr, nullptr, 0);
        g_ctx->PSSetShader(g_ps, nullptr, 0);
        g_ctx->VSSetConstantBuffers(0, 1, &g_cb);
        g_ctx->PSSetConstantBuffers(0, 1, &g_cb);
        g_ctx->PSSetShaderResources(0, 1, &fonte);
        g_ctx->PSSetSamplers(0, 1, &g_samp);
        g_ctx->Draw(3, 0);
        ID3D11ShaderResourceView* n = nullptr;
        g_ctx->PSSetShaderResources(0, 1, &n);
        g_ctx->OMSetRenderTargets(1, nulos, nullptr);
    }

    // ------------------------------------------------------------------ interface
    static inline bool Ligar(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
        if (g_ligada) return true;
        if (!dev || !ctx) return false;
        if (!PonteUp::Ligar(dev, ctx)) return false;
        g_dev = dev; g_dev->AddRef();
        g_ctx = ctx; g_ctx->AddRef();
        { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpcFreq = f.QuadPart > 0 ? f.QuadPart : 1; }
        g_ligada = true;
        DlssSrApp::Ligar(dev, ctx, FontePonte::g_log);   // Q2: o NGX so e inicializado no primeiro quadro 0x200
        g_srAvaliadas = g_srFalhas = 0; g_srFalhasSeguidas = 0; g_srSomaMs = g_srSomaTotal = 0.0; g_srLogouDireto = g_srLogouFmt = false;
        DlssSrApp::NovaSessao();
        return true;
    }

    static inline bool Ativa() { return g_ligada && PonteUp::Ativa(); }
    static inline void Desligar(const char* motivo);   // definida mais abaixo (o SrRodar a usa)

    // Q2: a sessao negociada e a 0x200 (DLSS SR feito pelo app)?
    static inline bool SessaoSr(UINT* ow = nullptr, UINT* oh = nullptr) {
        if (!Ativa()) return false;
        PonteUp::SessaoUp s{};
        if (!PonteUp::ObterSessao(&s) || !s.sr || s.largura == 0) return false;
        if (ow) *ow = s.saidaW;
        if (oh) *oh = s.saidaH;
        return true;
    }
    // A cadeia (rede) entra nos quadros desta sessao? Nao entra no SR sem rede (nativo_upstream=0)
    // nem quando a cor do jogo esta num formato que a cadeia nao aceita (ex.: R11G11B10).
    static inline bool FormatoCadeia(DXGI_FORMAT f) {
        f = FormatoDeVista(f);
        return f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_R10G10B10A2_UNORM || f == DXGI_FORMAT_R16G16B16A16_FLOAT;
    }
    static inline bool CadeiaNoUpstream() {
        PonteUp::SessaoUp s{};
        if (!Ativa() || !PonteUp::ObterSessao(&s) || s.largura == 0) return false;
        if (!s.sr) return true;
        return g_redeNoSr && FormatoCadeia(s.formatoCor);
    }

    // Q2: roda o DLSS SR do app sobre `cor` (a saida da rede, ou a cor do jogo) com os guias do
    // quadro `q`; *saida = textura no tamanho de saida. Falhas seguidas demais desligam a sessao
    // com motivo (a DLL volta a chamar o upscaler original do jogo).
    static inline bool SrRodar(ID3D11Texture2D* cor, const PonteUp::QuadroUp& q, ID3D11Texture2D** saida, double* ms) {
        PonteUp::SessaoUp s{};
        if (!PonteUp::ObterSessao(&s) || !s.sr) return false;
        DlssSrApp::Entrada e;
        e.cor = cor; e.mv = q.mv; e.depth = q.depth; e.expo = q.expo;
        e.maxW = q.largura; e.maxH = q.altura;
        e.rw = (q.srMsg.render_w && q.srMsg.render_w <= q.largura) ? q.srMsg.render_w : q.largura;
        e.rh = (q.srMsg.render_h && q.srMsg.render_h <= q.altura) ? q.srMsg.render_h : q.altura;
        e.ow = s.saidaW; e.oh = s.saidaH; e.outFmt = s.formatoSaida;
        e.jx = q.extra.jitter_x; e.jy = q.extra.jitter_y; e.msx = q.extra.mv_scale_x; e.msy = q.extra.mv_scale_y;
        e.pre = q.extra.pre_exposure; e.escExpo = q.extra.exposure_scale; e.dt = q.extra.frame_dt_ms;
        e.sharp = q.srMsg.sharpness; e.sharpen = (q.srMsg.flags & 1u) != 0;
        e.reset = (q.extra.flags & PonteUp::kExtraReset) != 0;
        e.flagsNgx = s.flagsNgx; e.depthInv = s.depthInv3;
        const bool ok = DlssSrApp::Avaliar(e, saida, ms);
        if (!ok) {
            ++g_srFalhas;
            if (++g_srFalhasSeguidas >= 30) {
                char m[256]; snprintf(m, sizeof m, "o DLSS SR no app falhou 30 vezes seguidas: %s", DlssSrApp::Motivo());
                Logf("sr-app: %s", m);
                if (g_cartaz) g_cartaz(T(L"O DLSS não conseguiu ampliar a imagem: o jogo voltou ao upscaling dele",
                                         L"DLSS could not upscale the image: the game went back to its own upscaling"));   // H6
                Desligar(m);
            }
            return false;
        }
        g_srFalhasSeguidas = 0;
        return true;
    }
    static inline void SrContar(double srMs, double totalMs) {
        ++g_srAvaliadas; g_srSomaMs += srMs; g_srSomaTotal += totalMs;
        if ((g_srAvaliadas % 300) == 0) {
            Logf("sr-app: %llu avaliacoes | sr %.2f ms | total %.2f ms | falhas %llu | rede %s",
                 (unsigned long long)g_srAvaliadas, g_srSomaMs / 300.0, g_srSomaTotal / 300.0, (unsigned long long)g_srFalhas,
                 CadeiaNoUpstream() ? "antes do SR" : "fora");
            g_srSomaMs = g_srSomaTotal = 0.0;
        }
    }

    // true = a DLL ja negociou o Build da sessao upstream (o tamanho interno e conhecido).
    // Enquanto isto e verdade, o laco NAO passa o quadro final da fase 2 pela cadeia: a fonte
    // da cadeia e a cor interna e ficar alternando os dois tamanhos refaria a cadeia a cada
    // quadro. O quadro da fase 2 volta intocado - e o DLSS do jogo ja o fez em cima do nosso
    // upstream, entao nao ha nada que a cadeia pudesse acrescentar ali sem desfazer o ganho.
    static inline bool SessaoNegociada(UINT* w = nullptr, UINT* h = nullptr, bool* hdr = nullptr) {
        if (!Ativa()) return false;
        PonteUp::SessaoUp s{};
        if (!PonteUp::ObterSessao(&s) || s.largura == 0 || s.altura == 0) return false;
        if (w) *w = s.largura;
        if (h) *h = s.altura;
        if (hdr) *hdr = EhHdr(s.formatoCor);
        return true;
    }
    // O backbuffer do app ja e do tamanho interno? Sendo, a cadeia roda 1:1 na cor inteira e
    // nao existe moldura nenhuma. Nao sendo, vale o recorte no canto (o caminho de reserva).
    static inline bool UmPorUm() {
        UINT w = 0, h = 0;
        return SessaoNegociada(&w, &h) && w == g_bbW && h == g_bbH;
    }

    static inline void Desligar(const char* motivo) {
        if (PonteUp::Ativa()) PonteUp::Desligar(motivo);
    }

    // O despertador do laco. O evento da fase 2 NAO serve para o upstream: os dois quadros
    // chegam por pipes diferentes e em instantes diferentes, e dormir no evento do outro
    // adiciona meia espera a cada quadro - no modo sincrono isso vira latencia do jogo.
    static inline HANDLE EventoQuadro() { return Ativa() ? PonteUp::EventoQuadro() : nullptr; }

    // X-CONSOLE / I11 (18/09/2026): O RETANGULO DA COR INTERNA DENTRO DA MOLDURA, para o
    // comparador do add-on (ele copia bit a bit e nao tem como saber que a imagem ocupa so
    // o canto). true = a cadeia esta rodando pela moldura e a cor e MENOR que o backbuffer:
    // *w/*h e o tamanho dela (1:1 no canto superior esquerdo). false = sem moldura nesta
    // sessao, ou a cor e do tamanho do backbuffer (cadeia 1:1 inteira).
    static inline bool MolduraRetangulo(UINT* w, UINT* h) {
        if (!Ativa() || g_moldura == nullptr || g_q.largura == 0 || g_q.altura == 0) return false;
        if (g_q.largura >= g_bbW && g_q.altura >= g_bbH) return false;
        if (w) *w = g_q.largura;
        if (h) *h = g_q.altura;
        return true;
    }

    // NO LACO, ANTES do Puxar da fase 2. true = ha um quadro upstream: *srv aponta para a
    // MOLDURA (do tamanho do backbuffer, com a cor interna 1:1 no canto), que e o que o
    // Renderer deve desenhar nesta volta. false = nada; nada e tocado.
    static inline bool Puxar(ID3D11ShaderResourceView** srvSaida, UINT* w, UINT* h, uint64_t* n) {
        if (!Ativa() || !g_dev) return false;
        if (g_bbW == 0 || g_bbH == 0 || g_srcW == 0 || g_srcH == 0) return false;
        if (g_pendente) {   // ninguem devolveu a volta passada: passa-through para o jogo nao esperar
            PonteUp::Entregar(nullptr, g_q.n, 0.0, true);
            g_pendente = false;
        }
        PonteUp::QuadroUp q{};
        if (!PonteUp::ObterQuadro(&q)) return false;
        if (!q.cor || !q.largura || !q.altura) { PonteUp::Entregar(nullptr, q.n, 0.0, false); return false; }
        const int64_t qPuxar0 = Agora();   // o instante em que o laco pegou este quadro
        // Q2: sessao 0x200 SEM a rede (nativo_upstream=0, ou cor num formato que a cadeia nao
        // aceita): o DLSS SR roda aqui mesmo, direto na cor do jogo, e o quadro nem passa pela
        // cadeia. A volta do laco segue como "sem quadro upstream".
        if (q.sr && !CadeiaNoUpstream()) {
            if (!g_srLogouDireto) {
                g_srLogouDireto = true;
                UINT ow = 0, oh = 0; SessaoSr(&ow, &oh);
                Logf("sr-app: DLSS SR direto na cor do jogo %ux%u fmt=%u -> %ux%u (%s)", q.largura, q.altura, (unsigned)q.formato,
                     ow, oh, g_redeNoSr ? "a cadeia nao aceita este formato de cor" : "nativo_upstream=0: sem rede antes do SR");
            }
            ID3D11Texture2D* saida = nullptr; double ms = 0.0;
            const bool ok = SrRodar(q.cor, q, &saida, &ms);
            const double gasto = Ms(qPuxar0, Agora());
            PonteUp::Entregar(ok ? saida : nullptr, q.n, gasto, gasto > kOrcamentoMs);
            if (ok) SrContar(ms, Ms(q.qpcChegada, Agora()));
            return false;
        }
        // A cadeia so pode rodar sem reamostrar se a FONTE que o Renderer desenha for do
        // tamanho do backbuffer. Quem acerta isso e o laco (`R.FonteMudou` com o tamanho que
        // devolvemos em *w/*h); aqui so se registra o estado, uma vez, para o log nao esconder
        // uma volta em que a fonte ainda nao tinha trocado.
        if ((g_srcW != g_bbW || g_srcH != g_bbH) && !g_avisouMoldura) {
            g_avisouMoldura = true;
            Logf("upstream: a fonte do app e %ux%u e a moldura da cadeia e %ux%u; o laco troca a fonte na proxima volta",
                 g_srcW, g_srcH, g_bbW, g_bbH);
        }
        if (q.largura > g_bbW || q.altura > g_bbH) {
            if (!g_avisouTamanho) { g_avisouTamanho = true;
                Logf("upstream: a cor interna e %ux%u e nao cabe no backbuffer %ux%u", q.largura, q.altura, g_bbW, g_bbH); }
            PonteUp::Entregar(nullptr, q.n, 0.0, false);
            Desligar("a cor interna nao cabe no backbuffer do app");
            return false;
        }
        if (!ShadersGarantir() ||
            !AlvoGarantir(&g_moldura, &g_molduraRtv, &g_molduraSrv, g_bbW, g_bbH, g_bbFmt)) {
            PonteUp::Entregar(nullptr, q.n, 0.0, false);
            Desligar("a moldura da cadeia nao montou");
            return false;
        }
        ID3D11ShaderResourceView* corSrv = nullptr;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = FormatoDeVista(q.formato);
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        if (FAILED(g_dev->CreateShaderResourceView(q.cor, &sd, &corSrv)) || !corSrv) {
            PonteUp::Entregar(nullptr, q.n, 0.0, false); return false; }

        const float preto[4] = { 0.f, 0.f, 0.f, 1.f };
        g_ctx->ClearRenderTargetView(g_molduraRtv, preto);
        // 1:1 no canto: o retangulo do viewport tem exatamente o tamanho da cor, e a uv
        // varre a textura inteira. Modo 1 = entra na curva do F0 quando a cor e 16F.
        const unsigned modo = (FormatoDeVista(q.formato) == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 1u : 0u;
        Passagem(corSrv, g_molduraRtv, q.largura, q.altura, 1.f, 1.f, modo);
        corSrv->Release();

        g_q = q;
        g_pendente = true;
        g_qpcEntrou = Agora();            // fim da nossa copia para a moldura
        g_somaEspera  += Ms(q.qpcChegada, qPuxar0);
        g_somaEntrada += Ms(qPuxar0, g_qpcEntrou);
        g_somaPeriodo += (g_qpcAnterior != 0) ? Ms(g_qpcAnterior, qPuxar0) : 0.0;
        g_qpcAnterior = qPuxar0;
        g_qpcPuxar = qPuxar0;             // o relogio do orcamento comeca quando o quadro e nosso
        if (srvSaida) *srvSaida = g_molduraSrv;
        if (w) *w = g_bbW;
        if (h) *h = g_bbH;
        if (n) *n = q.n;
        if (g_avaliados == 0) {
            const bool umPorUm = (q.largura == g_bbW && q.altura == g_bbH);
            Logf("upstream: rede antes do super resolution: cor interna %ux%u fmt=%u %s %ux%u fmt=%u%s",
                 q.largura, q.altura, (unsigned)q.formato,
                 umPorUm ? "1:1 na cadeia inteira (backbuffer no tamanho interno)" : "1:1 no canto de uma moldura",
                 g_bbW, g_bbH, (unsigned)g_bbFmt,
                 modo == 1 ? " (dentro da curva reversivel do F0)" : "");
        }
        return true;
    }

    // DEPOIS do Present do quadro puxado. `resultado` = o que ColherSaida devolveu (a saida
    // da cadeia, do tamanho do backbuffer) ou nullptr.
    static inline void Devolver(ID3D11Texture2D* resultado, uint64_t n) {
        if (!g_ligada || !g_pendente || n != g_q.n) return;
        const int64_t qDev0 = Agora();     // a volta ja passou pelo Present: aqui comeca a saida
        const double gastoMs = Ms(g_qpcPuxar, qDev0);
        const bool atrasado = gastoMs > kOrcamentoMs;
        g_somaCadeia += Ms(g_qpcEntrou, qDev0);
        ID3D11Texture2D* entrega = nullptr;

        // Estourou o orcamento: o resultado nao serve mais (a DLL ja esta esperando ha mais
        // tempo do que o combinado). Volta a cor intocada e conta o atraso.
        if (resultado != nullptr && !atrasado) {
            // O Output tem o formato que o Build negociou (a regra do F0: o mesmo da cor).
            // Q2: no SR o recorte e a ENTRADA do DLSS, entao fica no formato da cor do jogo.
            PonteUp::SessaoUp s{};
            const DXGI_FORMAT fmtSaida = (PonteUp::ObterSessao(&s) && s.formatoSaida != DXGI_FORMAT_UNKNOWN && !s.sr)
                                       ? s.formatoSaida : g_q.formato;
            if (AlvoGarantir(&g_recorte, &g_recorteRtv, nullptr, g_q.largura, g_q.altura, fmtSaida)) {
                D3D11_TEXTURE2D_DESC rd{}; resultado->GetDesc(&rd);
                ID3D11ShaderResourceView* srv = nullptr;
                D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                sd.Format = FormatoDeVista(rd.Format);
                sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                sd.Texture2D.MipLevels = 1;
                if (SUCCEEDED(g_dev->CreateShaderResourceView(resultado, &sd, &srv)) && srv) {
                    // Recorta o MESMO retangulo do canto e sai da curva EXATAMENTE quando
                    // entrou nela (a cor era 16F). Vale mesmo com a cadeia em 8 bits: e a
                    // curva que faz a faixa acima do branco atravessar uma cadeia SDR - foi
                    // para isso que o F0 a montou.
                    const unsigned modo = (FormatoDeVista(g_q.formato) == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 2u : 0u;
                    Passagem(srv, g_recorteRtv, g_q.largura, g_q.altura,
                             float(g_q.largura) / float(rd.Width), float(g_q.altura) / float(rd.Height), modo);
                    srv->Release();
                    entrega = g_recorte;
                }
            }
        }
        // Q2: sessao 0x200 - a rede ja passou pela cor pequena (o recorte); agora o DLSS SR do
        // app amplia para o tamanho de saida. Sem recorte (cadeia ainda subindo, atraso), o SR
        // roda na cor crua do jogo: o jogo nunca fica sem quadro ampliado.
        double srMs = 0.0; bool srOk = false;
        if (g_q.sr) {
            ID3D11Texture2D* saidaSr = nullptr;
            srOk = SrRodar(entrega ? entrega : g_q.cor, g_q, &saidaSr, &srMs);
            entrega = srOk ? saidaSr : nullptr;
        }
        PonteUp::Entregar(entrega, n, gastoMs, atrasado);
        if (srOk) SrContar(srMs, Ms(g_q.qpcChegada, Agora()));
        g_pendente = false;
        ++g_avaliados;
        if (atrasado) ++g_atrasados;
        g_somaSaida += Ms(qDev0, Agora());
        if ((g_avaliados % 300) == 0) {
            Logf("upstream: volta %llu | espera %.2f | entrada %.2f | cadeia %.2f | saida %.2f | periodo %.2f ms",
                 (unsigned long long)g_avaliados, g_somaEspera / 300.0, g_somaEntrada / 300.0,
                 g_somaCadeia / 300.0, g_somaSaida / 300.0, g_somaPeriodo / 300.0);
            g_somaEspera = g_somaEntrada = g_somaCadeia = g_somaSaida = g_somaPeriodo = 0.0;
        }

        // Orcamento na janela de 10 s. Um app que entrega tarde em mais de 5 % dos quadros
        // esta atrapalhando o jogo, e o certo e sair de cena dizendo por que.
        const ULONGLONG agora = GetTickCount64();
        if (g_janelaMs == 0) g_janelaMs = agora;
        ++g_janelaQuadros;
        if (atrasado) ++g_janelaAtrasos;
        if (agora - g_janelaMs >= 10000) {
            const double pct = g_janelaQuadros ? 100.0 * double(g_janelaAtrasos) / double(g_janelaQuadros) : 0.0;
            if (g_janelaAtrasos != 0)
                Logf("upstream: atrasado %llu vezes (%.1f %%)", (unsigned long long)g_janelaAtrasos, pct);
            if (pct > 5.0 && g_janelaQuadros >= 30) {
                Logf("upstream: %.1f %% dos quadros acima de %.0f ms em 10 s", pct, kOrcamentoMs);
                if (g_cartaz) g_cartaz(T(L"DLSS 5 Upstream desligado: a placa não deu conta",
                                         L"DLSS 5 Upstream turned off: the graphics card could not keep up"));   // H6
                Desligar("o app nao devolveu dentro de 4 ms em mais de 5% dos quadros");
            }
            g_janelaMs = agora; g_janelaQuadros = 0; g_janelaAtrasos = 0;
        }
    }

    // Rede de seguranca no fim da volta, como o FontePonte::FecharQuadro.
    static inline void FecharQuadro() {
        if (g_ligada && g_pendente) Devolver(nullptr, g_q.n);
    }

    static inline void Parar() {
        if (!g_ligada) return;
        if (g_pendente) { PonteUp::Entregar(nullptr, g_q.n, 0.0, false); g_pendente = false; }
        PonteUp::Parar();
        DlssSrApp::Parar();   // Q2: a feature DLSS SR do app sai junto com a sessao
        Soltar(g_molduraSrv); Soltar(g_molduraRtv); Soltar(g_moldura);
        Soltar(g_recorteRtv); Soltar(g_recorte);
        Soltar(g_vs); Soltar(g_ps); Soltar(g_samp); Soltar(g_cb);
        Logf("upstream: encerrado - %llu avaliacoes, %llu atrasos", (unsigned long long)g_avaliados, (unsigned long long)g_atrasados);
        Soltar(g_ctx); Soltar(g_dev);
        g_ligada = false;
    }

} // namespace FonteUpstream
