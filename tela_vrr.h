// =====================================================================================
//  DLSS5 For All - a tela esta em taxa VARIAVEL (G-Sync / FreeSync) agora?
//
//  POR QUE ISTO IMPORTA: com VRR ativo o monitor nao tem mais "hora certa". Quantizar a
//  apresentacao para o proximo vblank - que e a coisa CERTA a fazer numa tela de taxa
//  fixa - vira um atraso inventado por nos: a tela teria mostrado o quadro no instante em
//  que ele ficou pronto. Quem le esta resposta e o apresentador, para desligar a
//  quantizacao e apresentar imediato.
//
//  NAO EXISTE API QUE RESPONDA ISSO. Vale registrar o que foi conferido, para ninguem
//  refazer a busca:
//    - DXGI_FEATURE_PRESENT_ALLOW_TEARING (IDXGIFactory5) responde "esta maquina PERMITE
//      present sem esperar o vblank". E pre-requisito de VRR, e nada mais: uma tela de 60 Hz
//      fixa numa placa moderna responde `sim` do mesmo jeito.
//    - IDXGIOutput6::GetDesc1 entrega colorimetria, luminancia e bits - nada de taxa variavel.
//    - DwmGetCompositionTimingInfo entrega `rateRefresh`, que e a taxa NOMINAL (o teto do
//      range em VRR), e `qpcRefreshPeriod`, o periodo correspondente. Tambem nao responde.
//
//  ENTAO A RESPOSTA E MEDIDA, e o metodo e este:
//    O DWM publica em `qpcVBlank` o instante do ultimo vblank. Numa tela de taxa FIXA os
//    vblanks caem numa grade: o intervalo entre dois deles e sempre um MULTIPLO INTEIRO do
//    periodo nominal (1x quando ninguem atrasa, 2x quando uma composicao foi pulada, etc).
//    Com VRR ativo o scanout segue o PRESENT: o intervalo passa a cair onde o quadro ficou
//    pronto, e o resto da divisao pelo periodo nominal se espalha por toda a faixa.
//    Medimos o resto normalizado de cada intervalo (0 = em cima da grade, 0,5 = no meio do
//    caminho entre dois vblanks nominais). Se mais de 35% das amostras cairem LONGE da grade
//    (resto entre 0,20 e 0,80), a tela nao esta respeitando a grade: e VRR.
//
//  DUAS TRAVAS contra falso positivo, porque dizer "VRR" sem ser e desligar a quantizacao
//  de quem precisa dela:
//    1. Sem ALLOW_TEARING, a resposta e NAO, sempre - VRR sem isso nao existe.
//    2. Minimo de 120 amostras por janela, e a decisao so muda depois de DUAS janelas
//       seguidas dizendo o contrario do que esta valendo (histerese).
//
//  CUSTO: uma chamada ao DWM por volta do laco enquanto nao ha decisao; depois disso, uma
//  reconferencia a cada 5 s. Em regime e ~0.
//
//  Depende de Log(const char*, ...).
// =====================================================================================
#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <dxgi1_5.h>
#include <algorithm>

static void Log(const char* fmt, ...);

namespace TelaVrr {

// A maquina permite present sem esperar o vblank? Pre-requisito, nao resposta.
inline bool SuportaTearing() {
    static int cache = -1;
    if (cache >= 0) return cache != 0;
    cache = 0;
    IDXGIFactory5* f5 = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory5), (void**)&f5)) && f5) {
        BOOL ok = FALSE;
        if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &ok, sizeof(ok))))
            cache = ok ? 1 : 0;
        f5->Release();
    }
    return cache != 0;
}

struct Estado {
    // janela de medida
    LONGLONG ultVblank = 0;
    double   restos[256]{};
    int      n = 0;
    double   somaIntervalo = 0.0;
    int      nIntervalo = 0;
    // decisao
    bool      vrr = false;
    bool      decidiu = false;
    int       contraSeguidas = 0;
    double    intervaloMs = 0.0;    // intervalo medio observado entre vblanks
    double    periodoMs = 0.0;      // periodo nominal declarado pelo DWM
    ULONGLONG ultDecisao = 0;
};
inline Estado g_vrr;

// Chamar uma vez por volta do laco. Barato por construcao: depois de decidir, so volta a
// medir de 5 em 5 segundos.
inline void Amostrar() {
    const ULONGLONG agora = GetTickCount64();
    if (g_vrr.decidiu && agora - g_vrr.ultDecisao < 5000) return;

    DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti);
    if (FAILED(DwmGetCompositionTimingInfo(nullptr, &ti)) || !ti.qpcRefreshPeriod) return;
    LARGE_INTEGER fq{}; QueryPerformanceFrequency(&fq);
    if (!fq.QuadPart) return;

    const double periodo = (double)ti.qpcRefreshPeriod;
    g_vrr.periodoMs = periodo * 1000.0 / (double)fq.QuadPart;

    const LONGLONG vb = (LONGLONG)ti.qpcVBlank;
    if (g_vrr.ultVblank && vb > g_vrr.ultVblank) {
        const double d = (double)(vb - g_vrr.ultVblank);
        g_vrr.somaIntervalo += d * 1000.0 / (double)fq.QuadPart;
        g_vrr.nIntervalo++;
        // resto normalizado: 0 = em cima da grade nominal, 0,5 = no meio de dois vblanks
        double r = d / periodo;
        r = r - (double)(long long)r;
        if (r < 0.0) r += 1.0;
        if (g_vrr.n < (int)(sizeof(g_vrr.restos) / sizeof(g_vrr.restos[0]))) g_vrr.restos[g_vrr.n++] = r;
    }
    if (vb > g_vrr.ultVblank) g_vrr.ultVblank = vb;

    if (g_vrr.n < 120) return;

    int fora = 0;
    for (int i = 0; i < g_vrr.n; i++) if (g_vrr.restos[i] > 0.20 && g_vrr.restos[i] < 0.80) fora++;
    const double frac = (double)fora / (double)g_vrr.n;
    const bool querVrr = SuportaTearing() && frac > 0.35;

    g_vrr.intervaloMs = g_vrr.nIntervalo ? g_vrr.somaIntervalo / (double)g_vrr.nIntervalo : 0.0;

    if (!g_vrr.decidiu) {
        g_vrr.vrr = querVrr;
        g_vrr.decidiu = true;
        g_vrr.contraSeguidas = 0;
        Log("tela: VRR %s (fora da grade %.0f%% de %d amostras, intervalo observado %.2f ms, "
            "periodo nominal %.2f ms, allow-tearing %d)",
            g_vrr.vrr ? "sim" : "nao", frac * 100.0, g_vrr.n, g_vrr.intervaloMs, g_vrr.periodoMs,
            (int)SuportaTearing());
    } else if (querVrr != g_vrr.vrr) {
        // HISTERESE: uma janela discordando pode ser so uma rajada de composicoes pulada.
        // Duas seguidas ja e a tela tendo mudado de comportamento (a pessoa ligou o G-Sync
        // no painel do driver, o jogo entrou em tela cheia).
        if (++g_vrr.contraSeguidas >= 2) {
            g_vrr.vrr = querVrr;
            g_vrr.contraSeguidas = 0;
            Log("tela: VRR %s (mudou - fora da grade %.0f%% de %d amostras, intervalo %.2f ms)",
                g_vrr.vrr ? "sim" : "nao", frac * 100.0, g_vrr.n, g_vrr.intervaloMs);
        }
    } else {
        g_vrr.contraSeguidas = 0;
    }

    g_vrr.n = 0; g_vrr.somaIntervalo = 0.0; g_vrr.nIntervalo = 0;
    g_vrr.ultDecisao = agora;
}

// O que o apresentador pergunta.
inline bool   Ativo()       { return g_vrr.decidiu && g_vrr.vrr; }
inline double IntervaloMs() { return g_vrr.intervaloMs; }
inline double PeriodoMs()   { return g_vrr.periodoMs; }

// Um monitor mudou de lugar/modo: a medida anterior nao vale mais.
inline void Reiniciar() {
    g_vrr.n = 0; g_vrr.somaIntervalo = 0.0; g_vrr.nIntervalo = 0;
    g_vrr.decidiu = false; g_vrr.contraSeguidas = 0; g_vrr.ultVblank = 0;
}

} // namespace TelaVrr

// O NOME CURTO QUE O APRESENTADOR VAI CHAMAR (o modulo dele e de outra pessoa; este e o
// ponto de entrada combinado). `inline` e nao `static` de proposito: sem isso o compilador
// avisa "defined but not used" enquanto o apresentador ainda nao chama.
inline bool TelaVRR()           { return TelaVrr::Ativo(); }
inline double TelaVrrIntervalo() { return TelaVrr::IntervaloMs(); }
