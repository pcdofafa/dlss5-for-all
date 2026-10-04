// =====================================================================================
//  DLSS5 For All - modulo de captura de tela (janela / monitor)
//
//  Mesma ideia da captura de camera, so que a "fonte de video" e uma janela de jogo
//  (ou um monitor inteiro). Usa Windows Graphics Capture (WGC, o mesmo backend do
//  "Captura de Janela (Windows 10)" do OBS):
//   - pega o frame ja composto pelo DWM, direto em textura da GPU (sem readback pra CPU)
//   - funciona com janela ocluida / atras de outra, e com borderless fullscreen
//   - a textura vem no MESMO device D3D11 do renderer, entao o caminho e
//     WGC -> CopySubresourceRegion -> SRV -> nosso draw -> Present -> ReShade/DLSS5
//
//  1 frame do jogo = 1 SetEvent = 1 Present. E o que o Feeder quer pra estimar
//  motion vector: nada de present repetido com imagem identica.
//
//  Escrito na ABI "crua" do WinRT (nada de C++/WinRT) pra continuar compilando com
//  o mesmo MinGW-w64 que gera o exe.
//
//  Este header depende de uma funcao Log(const char*, ...) definida no main.cpp antes
//  do #include - e la que sai o relatorio de latencia (LAT[...] no dlss5forall.log).
// =====================================================================================
#pragma once

#include "saida_rapida.h"   // Saida::Voltas: a espera do handler encolhe no encerramento
#include "idioma.h"           // T(pt, en): as mensagens daqui viram caixa de erro na tela
#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include "hdr_control.h"   // so para perguntar "o monitor do alvo esta em HDR?"
#include <string>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstdlib>

// =====================================================================================
//  Medicao de latencia (QueryPerformanceCounter)
//
//  Instantes marcados no caminho de UM frame:
//    t0  SystemRelativeTime do frame  - carimbo do proprio DWM (base QPC, unidade 100ns)
//    t1  FrameArrived (Invoke)        - quando o WGC avisou que tem frame novo
//    t2  Tick()                       - drenar a fila + recortar + CopySubresourceRegion
//    t3  DrawAndPresent()             - nosso draw + Present + toda a cadeia ReShade/DLSS
//    t4  vblank real                  - via DXGI_FRAME_STATISTICS.SyncQPCTime
//
//  O numero que mais importa e t3->t4 ("PRESENT->TELA"): e ali que mora a fila do
//  driver/DWM, que e a unica parte grande da latencia que o nosso codigo controla.
//  "intervalo entre FrameArrived" diz o ritmo REAL que o WGC entrega - se ele nao bater
//  com a taxa do monitor, a latencia extra e do sistema, nao nossa.
//
//  Sai no log a cada 300 frames. Custo: ~4 QueryPerformanceCounter por frame.
// =====================================================================================
static void Log(const char* fmt, ...);

// A OUTRA PORTA DE ENTRADA (O3, 20/09/2026). O WGC pula composicoes; a Desktop Duplication
// carimba cada quadro com o present que o gerou. Os dois entregam pelo MESMO caminho daqui
// para a frente (recorte em `tex` + SetEvent no `evt`), entao nada acima da captura precisa
// saber quem esta servindo. Ver captura_dxgi.h.
#include "captura_dxgi.h"
// O ponteiro desenhado por nos sobre a imagem capturada (os dois backends usam o mesmo).
#include "cursor_pelicula.h"
// "A tela esta em taxa variavel agora?" - quem pergunta e o apresentador; quem mede e a
// captura, que ja tem o monitor da fonte na mao.
#include "tela_vrr.h"

/* X-GERADO (20/09/2026): o CARIMBO do ultimo quadro copiado do WGC, em ms na base do QPC (o mesmo
   relogio do apresentador). E o `SystemRelativeTime` do proprio quadro - posto pelo DWM ANTES da
   fila de entrega -, e nao a hora em que o WGC nos acordou. Medido no alvo64 a 30 fps cravados
   (FG-PROVA §2): o ritmo do FrameArrived vai de p05 30,3 a p95 46,5 ms, e era esse tremor da
   ENTREGA que o apresentador estava lendo como tremor da FONTE (`j = 21 ms`). Ver
   ApresentadorGpu::Acordar. 0 enquanto nao houver captura (modo injetado). */
inline std::atomic<double> g_wgcCarimboMs{ 0.0 };
/* RODADA5 (23/09/2026, OCLUSAO-2309 sec. 12): o MESMO carimbo, mas CRU - sem a grade do V2. O V2
   regulariza o carimbo (absorve os 3/4/5 vblanks da fonte) e isso e o contrario do que a cadencia
   por carimbo quer: a tela tem de reproduzir o espacamento DA FONTE, irregular ou nao. Quem le:
   o apresentador (cadencia sem FG) e a linha `quadros:` do log. 0 = sem captura. */
inline std::atomic<double> g_wgcCarimboCruMs{ 0.0 };
/* RODADA5: quadros que chegaram da captura e voltaram ao pool SEM virar imagem porque a cadeia nao
   os alcancou (o dreno "so o mais novo" devolveu o antigo). E o `perdidos_na_captura` da linha
   `quadros:`. Acumulado da sessao; quem le calcula o delta. O que o proprio WGC deixa de entregar
   com o pool cheio nao passa por aqui (nao e observavel deste lado). */
inline std::atomic<uint64_t> g_capPerdidosTot{ 0 };

/* DEGRAU (23/09/2026, OCLUSAO-2309 sec. 21): a guarda com ritmo ADMITE a fonte em passo INTEIRO N sobre a grade de
   composicao do DWM, em vez de um intervalo continuo (240/108 = 2,2 fazia a linha de 60 do testufo cair em 3/5).
   COMO, SEM DESCARTAR NADA: na FILA do WGC o pool tem optPoolBuf buffers. Com o degrau ativo, o quadro processado NAO e
   devolvido ao pool logo depois da copia: a captura o SEGURA. Com todos os buffers conosco o DWM nao tem onde entregar
   e simplesmente nao entrega (o SO nao entrega os outros - como hoje com a fila cheia). A esteira devolve o mais velho
   `lead` ms antes do carimbo da composicao-alvo (idx % N na grade, contando do ultimo quadro NOVO): o buffer livre recebe
   exatamente a composicao-alvo, que e processada NA CHEGADA (sem fila). Todo quadro que chega e processado.
   `lead` se calibra pelo que chega: cedo (idx < alvo) e inequivoco -> lead -= |d| P; tarde com a fonte densa -> +P/2.
   FASE: quadro-alvo copia exata (dedup) -> o buffer e devolvido na hora e o proximo NOVO conta (a grade passa a contar
   dele: a fase casa com o conteudo). Estado mexido so pela thread do laco (Tick, EsperarRitmo, Relatorio). */
struct ScreenCapture;
namespace Degrau {
inline int            g_N = 0;             // passo pedido pela esteira (< 2 = desligado: cada quadro volta ao pool na hora)
inline ScreenCapture* g_cap = nullptr;     // a captura em FILA do WGC que segura os buffers
inline double         g_capHz = 0.0;       // refresh do monitor da captura (EnumDisplaySettings), para conferir a grade do DWM
inline double         g_capTickMs = 0.0;   // ultimo Tick da FILA (o despertador do laco so vale com a captura andando)
inline double g_P = 0.0, g_vb = 0.0; inline long long g_base = 0; inline ULONGLONG g_ancMs = 0;   // grade do DWM (QPC em ms)
inline double g_phiX = 1.0, g_phiY = 0.0, g_phi = 0.0;   // fase dos carimbos na grade (media circular, em P)
inline double g_lead = -1.0;               // ms antes do carimbo-alvo em que o buffer volta ao DWM (< 0 = recomeca em (N-1) P)
inline const long long kNada = -(1LL << 62);
inline long long g_idxNovo = kNada, g_idxUlt = kNada, g_alvo = 0;
inline bool   g_naMao = false;             // o Tick tirou um quadro; o laco ainda nao disse se era novo ou copia
inline bool   g_copiaPend = false;         // o ultimo quadro foi copia: devolver ja
inline bool   g_libAberta = false, g_libEmDia = false;   // a ultima devolucao espera o quadro dela / foi no prazo
inline double g_libMs = 0.0;
inline uint32_t g_hist = 0;                // ultimos 8 resultados no prazo (bit 1 = acerto)
inline int    g_histN = 0;
// janela de 1 s (a linha `esteira 1s`)
inline uint32_t g_jLib = 0, g_jEmDia = 0, g_jAcerto = 0, g_jCedo = 0, g_jTarde = 0, g_jCopias = 0, g_jEspN = 0, g_jEspOutro = 0, g_jEspOutroMax = 0;
inline double Agora() { static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f); LARGE_INTEGER c; QueryPerformanceCounter(&c); return 1000.0 * (double)c.QuadPart / (double)f.QuadPart; }
// A grade do DWM (periodo e um vblank), relida 1x/s; o indice absoluto continua o mesmo entre releituras.
inline bool Ancorar() {
    const ULONGLONG ag = GetTickCount64();
    if (g_P > 0.0 && ag - g_ancMs < 1000) return true;
    g_ancMs = ag;
    DWM_TIMING_INFO ti{}; ti.cbSize = sizeof ti;
    LARGE_INTEGER fq{}; QueryPerformanceFrequency(&fq);
    if (FAILED(DwmGetCompositionTimingInfo(nullptr, &ti)) || !ti.qpcRefreshPeriod || !ti.qpcVBlank || !fq.QuadPart) return g_P > 0.0;
    const double P = (double)ti.qpcRefreshPeriod * 1000.0 / (double)fq.QuadPart, vb = (double)ti.qpcVBlank * 1000.0 / (double)fq.QuadPart;
    if (!(P > 1.0 && P < 60.0)) return g_P > 0.0;
    if (g_P > 0.0 && g_vb > 0.0) g_base += (long long)floor((vb - g_vb) / g_P + 0.5); else g_base = 0;
    g_P = P; g_vb = vb;
    return true;
}
inline double G(double t) { return (t - g_vb) / g_P + (double)g_base; }
inline long long Idx(double carMs) { return (long long)floor(G(carMs) - g_phi + 0.5); }
inline double Tempo(long long idx) { return g_vb + ((double)(idx - g_base) + g_phi) * g_P; }   // o carimbo da composicao idx
inline double Hz() { return g_P > 0.0 ? 1000.0 / g_P : 0.0; }
// A grade do DWM e a do monitor da captura? (DwmGetCompositionTimingInfo(nullptr) e a do monitor principal)
inline bool GradeConfere() { const double h = Hz(); return h > 1.0 && (g_capHz <= 1.0 || fabs(h - g_capHz) <= 0.02 * g_capHz); }
inline void Rearmar() { g_idxNovo = g_idxUlt = kNada; g_naMao = g_copiaPend = g_libAberta = false; g_hist = 0; g_histN = 0; }
// O Tick (FILA) chama para cada quadro tirado do pool, com o carimbo CRU (ms).
inline void Chegou(double carMs) {
    if (g_N < 2 || carMs <= 0.0 || !Ancorar()) return;
    const double gc = G(carMs), r = gc - floor(gc);
    g_phiX = 0.94 * g_phiX + 0.06 * cos(2.0 * 3.14159265358979 * r);
    g_phiY = 0.94 * g_phiY + 0.06 * sin(2.0 * 3.14159265358979 * r);
    g_phi = atan2(g_phiY, g_phiX) / (2.0 * 3.14159265358979);
    const long long idx = Idx(carMs);
    g_idxUlt = idx; g_naMao = true;
    if (!g_libAberta) return;   // chegou sem devolucao nossa pendente (fluxo natural: pool com buffer livre)
    g_libAberta = false;
    if (!g_libEmDia) return;    // devolucao atrasada (o laco chegou depois do prazo) ou por copia: nao calibra
    const long long d = idx - g_alvo;
    const double tetoLead = ((double)g_N - 0.5) * g_P;
    int bit = 0;
    if (d == 0) { g_jAcerto++; bit = 1; }
    else if (d < 0) { g_jCedo++; g_lead += (double)d * g_P; if (g_lead < 0.0) g_lead = 0.0; }   // um buffer livre antes do alvo: inequivoco
    else {
        g_jTarde++;
        // tarde e ambiguo (lead curto OU composicao-alvo sem nada novo); so corrige com a fonte DENSA (>= 6 de 8 acertos)
        int ac = 0; for (int i = 0; i < 8; i++) ac += (g_hist >> i) & 1u;
        if (g_histN >= 8 && ac >= 6) { g_lead += 0.5 * g_P; if (g_lead > tetoLead) g_lead = tetoLead; }
    }
    g_hist = (g_hist << 1) | (uint32_t)bit; if (g_histN < 8) g_histN++;
}
// O laco chama depois do dedup: o quadro que o Tick tirou era novo (foi a rede) ou copia exata?
inline void Classificar(bool novo) {
    if (!g_naMao) return;
    g_naMao = false;
    if (g_N < 2) return;
    if (novo) {
        if (g_idxNovo != kNada) {
            const long long e = g_idxUlt - g_idxNovo;
            if (e == g_N) g_jEspN++; else if (e > 0) { g_jEspOutro++; if ((uint32_t)e > g_jEspOutroMax) g_jEspOutroMax = (uint32_t)e; }
        }
        g_idxNovo = g_idxUlt; g_copiaPend = false;
    } else { g_copiaPend = true; g_jCopias++; }
}
}  // namespace Degrau

struct LatSeries {
    static const int CAP = 1024;
    double v[CAP]; int n = 0;
    void add(double x) { if (n < CAP) v[n++] = x; }
    void reset() { n = 0; }
    double avg() const { if (!n) return 0.0; double s = 0; for (int i = 0; i < n; i++) s += v[i]; return s / n; }
    double pct(double q) const {
        if (!n) return 0.0;
        std::vector<double> t(v, v + n); std::sort(t.begin(), t.end());
        int i = (int)(t.size() * q); if (i >= (int)t.size()) i = (int)t.size() - 1;
        return t[i];
    }
    double p95() const { return pct(0.95); }
    double mn() const { if (!n) return 0.0; double m = v[0]; for (int i = 1; i < n; i++) if (v[i] < m) m = v[i]; return m; }
    double mx() const { double m = 0; for (int i = 0; i < n; i++) if (v[i] > m) m = v[i]; return m; }
};

struct LatProbe {
    LARGE_INTEGER freq{};
    int       every = 300;
    UINT32    n = 0;
    bool      fedByPresent = false;   // o loop principal esta chamando Present()?

    LatSeries srt;      // t0 -> t1 (carimbo do DWM x recebimento; negativo = t0 e no futuro)
    LatSeries wake;     // t1 -> t2 (evento -> loop acordar)
    LatSeries tick;     // duracao do Tick
    LatSeries pres;     // duracao do DrawAndPresent (inclui a cadeia ReShade/DLSS)
    LatSeries slot;     // espera pelo slot de apresentacao (contrapressao da placa)
    LatSeries disp;     // fim do Present -> vblank em que o frame apareceu
    // gap/nArrive/lastArr sao escritos pelo Invoke do WGC, que roda numa THREAD DO SISTEMA,
    // e lidos/zerados pelo laco principal em Dump()/Reset(). Sem sincronizacao isso e data
    // race: nao corrompe memoria (o indice da serie e checado), mas pode congelar a serie
    // de "gap" no relatorio LAT, que e justamente o numero que diz o ritmo real do WGC.
    // Um SRWLOCK basta - a secao critica sao tres somas, e o caminho e chamado ~100x/s.
    SRWLOCK   lkGap = SRWLOCK_INIT;
    LatSeries gap;      // intervalo entre dois FrameArrived (protegido por lkGap)
    UINT32    nArrive = 0, nTick = 0, nPres = 0;

    LONGLONG  presQpc[256]{};
    UINT      lastPC  = 0;
    LONGLONG  lastArr = 0;
    double    ultGapMs = 0.0;   // RODADA4: intervalo do ULTIMO FrameArrived ao anterior (sob lkGap)
    LONGLONG  curSrt  = 0;
    LONGLONG  t0dump  = 0;

    LatProbe() { QueryPerformanceFrequency(&freq); t0dump = Now(); }

    LONGLONG Now() const { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
    double   Ms(LONGLONG d) const { return freq.QuadPart ? (double)d * 1000.0 / (double)freq.QuadPart : 0.0; }
    LONGLONG QpcTo100ns(LONGLONG q) const { return freq.QuadPart ? (LONGLONG)((double)q * 10000000.0 / (double)freq.QuadPart) : 0; }

    void Reset() {
        srt.reset(); wake.reset(); tick.reset(); pres.reset(); disp.reset(); slot.reset();
        AcquireSRWLockExclusive(&lkGap);
        gap.reset(); nArrive = 0; lastArr = 0;
        ReleaseSRWLockExclusive(&lkGap);
        n = 0; nTick = 0; nPres = 0; t0dump = Now();
    }
    // Chamado do Invoke (thread do sistema): a unica escrita que vem de fora do laco.
    void ChegouFrame(LONGLONG q) {
        AcquireSRWLockExclusive(&lkGap);
        if (lastArr) { const double g = Ms(q - lastArr); gap.add(g); ultGapMs = g; }
        lastArr = q;
        nArrive++;
        ReleaseSRWLockExclusive(&lkGap);
    }

    // ESPERA PELO SLOT DE APRESENTACAO. Chamado pelo laco principal em volta do
    // WaitForPresentSlot, que fica no TOPO do ciclo, antes de pegar o quadro.
    //
    // Por que este numero faltava e por que ele decide a leitura do orcamento de quadro:
    // "present" mede a CHAMADA de DrawAndPresent, e o Present do D3D11 volta assim que
    // enfileira, sem esperar a placa. Entao, quando a placa e o gargalo, o tempo NAO
    // aparece ali - ele aparece aqui, na volta seguinte, quando o objeto de espera segura
    // o laco ate haver vaga. Sem esta serie, esse tempo nao estava em lugar nenhum e o
    // orcamento de quadro tinha um buraco.
    //
    // A leitura: espera pequena aqui significa apresentacao desacoplada, e a latencia de
    // fila NAO entra na vazao. Espera grande significa contrapressao, e ai a fila volta a
    // entrar na vazao - a interpretacao do custo por quadro muda inteira. Custo: duas
    // leituras de relogio por quadro.
    void Slot(LONGLONG s0, LONGLONG s1) { slot.add(Ms(s1 - s0)); ultSlotMs = Ms(s1 - s0); }
    double ultSlotMs = 0.0;   // RODADA4: a espera de slot desta volta (so a thread do laco escreve)
    // RODADA4: o retrato do WGC para o log do buraco: ha quanto tempo chegou o ultimo quadro e
    // quanto ele demorou em relacao ao anterior.
    void UltimaChegada(double* idadeMs, double* gapMs) {
        AcquireSRWLockShared(&lkGap);
        const LONGLONG la = lastArr; const double g = ultGapMs;
        ReleaseSRWLockShared(&lkGap);
        *idadeMs = la ? Ms(Now() - la) : -1.0; *gapMs = g;
    }

    // Chamado pelo loop principal logo depois do DrawAndPresent.
    void Present(LONGLONG p0, LONGLONG p1, IDXGISwapChain* sc) {
        fedByPresent = true;
        pres.add(Ms(p1 - p0));
        nPres++;
        if (sc) {
            UINT pc = 0;
            if (SUCCEEDED(sc->GetLastPresentCount(&pc))) presQpc[pc & 255] = p1;
            DXGI_FRAME_STATISTICS fs{};
            if (SUCCEEDED(sc->GetFrameStatistics(&fs)) && fs.PresentCount && fs.PresentCount != lastPC) {
                LONGLONG q = presQpc[fs.PresentCount & 255];
                if (q) { double d = Ms(fs.SyncQPCTime.QuadPart - q); if (d > -100.0 && d < 500.0) disp.add(d); }
                lastPC = fs.PresentCount;
            }
        }
        curSrt = 0;
        if (++n >= (UINT32)every) { Dump(); Reset(); }
    }

    void Dump() {
        double s = Ms(Now() - t0dump) / 1000.0; if (s <= 0.0) s = 1.0;
        // nArrive tambem e escrito pelo Invoke do WGC: le sob o mesmo lock do gap, senao
        // sobra exatamente a corrida que este lock foi criado para fechar.
        AcquireSRWLockShared(&lkGap);
        const UINT32 nArr = nArrive;
        ReleaseSRWLockShared(&lkGap);
        // 'slot' entra ao lado de 'acordar' porque as duas sao espera, e juntas com tick e
        // present formam a parte SERIAL do ciclo - a unica que soma no custo por quadro.
        // PRESENT->TELA continua no fim e continua sendo LATENCIA: quem somar tudo numa
        // conta de vazao vai contar duas vezes o que a fila ja esta escondendo.
        Log("LAT %.0fs | fps chegada=%.1f copiado=%.1f present=%.1f | srt %.2f/%.2f | acordar %.2f/%.2f"
            " | slot %.2f/%.2f | tick %.2f/%.2f | present %.2f/%.2f | serial %.2f"
            " | PRESENT->TELA %.2f/%.2f (n=%d)  [ms media/p95]"
            // RODADA4 (23/09/2026): os MAXIMOS, para dizer de quem e um STALL "fora do feed" de
            // 50-260 ms: srt/acordar grandes = o laco demorou a pegar o quadro que ja tinha chegado;
            // slot/present grandes = a nossa placa/DXGI segurou; tudo pequeno com o `max` do ritmo
            // do WGC grande = o quadro nao chegou (DWM/WGC), nao fomos nos.
            " | max srt %.1f acordar %.1f slot %.1f tick %.1f present %.1f",
            s, nArr / s, nTick / s, nPres / s,
            srt.avg(), srt.p95(), wake.avg(), wake.p95(), slot.avg(), slot.p95(),
            tick.avg(), tick.p95(), pres.avg(), pres.p95(),
            wake.avg() + slot.avg() + tick.avg() + pres.avg(),
            disp.avg(), disp.p95(), disp.n,
            srt.mx(), wake.mx(), slot.mx(), tick.mx(), pres.mx());
        AcquireSRWLockShared(&lkGap);
        const double gmn = gap.mn(), g05 = gap.pct(0.05), gav = gap.avg(), g95 = gap.p95(), gmx = gap.mx();
        ReleaseSRWLockShared(&lkGap);
        Log("LAT ritmo do WGC (intervalo entre FrameArrived): min %.2f p05 %.2f media %.2f p95 %.2f max %.2f ms",
            gmn, g05, gav, g95, gmx);
    }
};

static LatProbe g_lat;

namespace WGC {
namespace WF   = ABI::Windows::Foundation;
namespace WG   = ABI::Windows::Graphics;
namespace WGCa = ABI::Windows::Graphics::Capture;
namespace WGDX = ABI::Windows::Graphics::DirectX;
namespace WGD3 = ABI::Windows::Graphics::DirectX::Direct3D11;
}

// =====================================================================================
//  IGraphicsCaptureSession5 - a causa do teto de 50 fps
//
//  O header do MinGW nao declara essa interface, mas ela EXISTE no Windows 11 (conferido
//  em C:\Windows\System32\WinMetadata\Windows.Graphics.winmd, build 26200). Ela expoe
//  MinUpdateInterval: o intervalo MINIMO entre dois frames que o WGC entrega.
//
//  O padrao do sistema e 16,0 ms - um teto de ~60 fps pensado pra economizar bateria.
//  So que o teto nao e 60: o WGC entrega no vblank do DWM. Num monitor de 100 Hz o DWM
//  compoe a cada 10 ms, e "no minimo 16 ms" arredonda pra cima ate o proximo vblank
//  valido, ou seja DOIS vblanks: 20,00 ms = 50,0 fps cravados. Foi exatamente isso que
//  medimos. (Num monitor de 60 Hz o mesmo 16 ms cabe em 1 vblank de 16,67 ms e ninguem
//  percebe o problema - por isso quase ninguem reclama disso.)
//
//  Medido aqui, mesmo alvo, mesma sessao, so mudando esse campo:
//     MinUpdateInterval = 16 ms (padrao) -> 49,93 fps, gap 20,00 ms (p05 19,4 / p95 20,6)
//     MinUpdateInterval =  0 ms          -> 99,83 fps, gap 10,00 ms (p05  9,3 / p95 10,7)
//  Desktop Duplication no mesmo monitor, como controle: 100,25 fps, gap 10,00 ms.
//
//  Precisa ser ajustado ANTES do StartCapture.
// =====================================================================================
static const GUID IID_WgcSession5 =
    { 0x67c0ea62, 0x1f85, 0x5061, { 0x92, 0x5a, 0x23, 0x9b, 0xe0, 0xac, 0x09, 0xcb } };

struct IWgcSession5 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_MinUpdateInterval(WGC::WF::TimeSpan* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MinUpdateInterval(WGC::WF::TimeSpan value) = 0;
};

// ------------------------------------------------------------------ alvos enumeraveis
enum SourceKind { SRC_CAMERA = 0, SRC_WINDOW = 1, SRC_MONITOR = 2 };

struct WinTarget {
    HWND         hwnd = nullptr;
    std::wstring title;    // titulo da janela
    std::wstring proc;     // nome do exe
    std::wstring label;    // "titulo  [exe.exe]  1920x1080"
    int          w = 0, h = 0;
};

struct MonTarget {
    HMONITOR     mon = nullptr;
    std::wstring label;    // "\\.\DISPLAY1 (principal)  2560x1440"
    int          w = 0, h = 0;
};

static bool WgcWindowIsCloaked(HWND h) {
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) return cloaked != 0;
    return false;
}

static std::wstring WgcProcessName(HWND h) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (!pid) return L"";
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return L"";
    wchar_t path[MAX_PATH] = L""; DWORD n = MAX_PATH;
    std::wstring out;
    if (QueryFullProcessImageNameW(p, 0, path, &n)) {
        out = path; size_t k = out.find_last_of(L"\\/");
        if (k != std::wstring::npos) out = out.substr(k + 1);
    }
    CloseHandle(p);
    return out;
}

struct WgcEnumCtx { std::vector<WinTarget>* v; HWND self; };

static BOOL CALLBACK WgcEnumProc(HWND h, LPARAM lp) {
    WgcEnumCtx* c = (WgcEnumCtx*)lp;
    if (h == c->self) return TRUE;
    if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
    if (GetAncestor(h, GA_ROOT) != h) return TRUE;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    if (WgcWindowIsCloaked(h)) return TRUE;
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return TRUE;
    std::wstring t(len + 1, L'\0');
    GetWindowTextW(h, &t[0], len + 1); t.resize(len);
    if (t == L"Program Manager" || t == L"Configuracoes" || t == L"Settings") return TRUE;
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(h, &r);
    int w = r.right - r.left, hh = r.bottom - r.top;
    if (w < 64 || hh < 64) return TRUE;
    WinTarget wt; wt.hwnd = h; wt.title = t; wt.proc = WgcProcessName(h); wt.w = w; wt.h = hh;
    // O titulo de uma janela de jogo passa de 512 caracteres com folga (navegadores e
    // launchers sao os campeoes). _snwprintf trunca SEM por o terminador, e o std::wstring
    // logo abaixo leria alem do buffer.
    wchar_t buf[512];
    _snwprintf(buf, 512, L"%ls  [%ls]  %dx%d", t.c_str(), wt.proc.empty() ? L"?" : wt.proc.c_str(), w, hh);
    buf[511] = 0;
    wt.label = buf;
    c->v->push_back(wt);
    return TRUE;
}

static std::vector<WinTarget> EnumCaptureWindows(HWND self) {
    std::vector<WinTarget> v; WgcEnumCtx c{ &v, self };
    EnumWindows(WgcEnumProc, (LPARAM)&c);
    return v;
}

static BOOL CALLBACK WgcMonProc(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    auto* v = (std::vector<MonTarget>*)lp;
    MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(m, (MONITORINFO*)&mi)) return TRUE;
    MonTarget t; t.mon = m;
    t.w = mi.rcMonitor.right - mi.rcMonitor.left;
    t.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    wchar_t buf[256];
    _snwprintf(buf, 256, L"%ls%ls  %dx%d", mi.szDevice, (mi.dwFlags & MONITORINFOF_PRIMARY) ? T(L" (principal)", L" (primary)") : L"", t.w, t.h);
    buf[255] = 0;
    t.label = buf;
    v->push_back(t);
    return TRUE;
}

static std::vector<MonTarget> EnumCaptureMonitors() {
    std::vector<MonTarget> v;
    EnumDisplayMonitors(nullptr, nullptr, WgcMonProc, (LPARAM)&v);
    return v;
}

// ------------------------------------------------------------------ handler FrameArrived
// O frame pool e "free threaded": o Invoke cai numa thread do sistema. Ele nao faz nada
// alem de carimbar a hora e acordar o loop principal, que e quem toca no device D3D11.
typedef WGC::WF::ITypedEventHandler<WGC::WGCa::Direct3D11CaptureFramePool*, IInspectable*> WgcFrameArrivedBase;

// RITMO2 (22/09/2026): A CHEGADA DEIXOU DE ESPERAR A VOLTA DO LACO.
//
// O QUE ACONTECIA (STUTTER-TDR-2209 §2.1/§2.2, medido em 22/09): com a rede ligada a
// captura caia de 238/s para 63-74/s e o tremor da chegada subia de 4,2 para 22-31 ms.
// Nao era o WGC: era NOSSO. Quem devolvia os buffers do pool era o Tick(), na thread do
// render, DEPOIS do Present -- e o Present leva a cadeia inteira do ReShade (RenoDX +
// feed) dentro dele, ~16-17 ms. Com um pool de 2 buffers e o unico consumidor preso atras
// do Present, o WGC so podia produzir 1-2 quadros por volta do laco. Sobre um video de
// 60 fps numa tela de 240 Hz isso trocava 4 refreshes fixos por 3/4/5 ao acaso, que e a
// definicao de judder.
//
// O QUE PASSOU A ACONTECER: o proprio FrameArrived -- que ja roda numa thread do sistema,
// fora do nosso laco -- drena o pool, GUARDA O MAIS NOVO num slot e devolve todos os
// outros na hora. Os buffers voltam ao WGC no ritmo da CAPTURA, nao no do Present. O
// Tick() continua fazendo exatamente o que fazia (pega o mais novo, recorta, copia), so
// que agora pega do slot em vez de pegar do pool.
//
// POR QUE ISSO NAO E O MESMO QUE "copiar na thread do WGC": a copia (CopyFrame) continua
// inteira na thread do render, com o mesmo contexto imediato de sempre. O handler nao
// toca em device, contexto nem textura nossa -- ele so pede e devolve frames ao pool, que
// e free-threaded por construcao (CreateFreeThreaded). E o minimo de trabalho que resolve
// o acoplamento sem abrir um segundo dono do contexto D3D11.
struct ScreenCapture;
static void WgcDrenarParaSlot(ScreenCapture* s);

// Os campos sao atomicos e existe um contador de chamadas em voo porque
// remove_FrameArrived NAO espera o Invoke que ja comecou: sem isso, um SetEvent
// atrasado escreveria num HANDLE que o Stop() acabou de fechar (e que o Windows pode
// ja ter reciclado para outro objeto). Desarmar + esperar o voo acabar e o que torna
// Stop()+Start() - a recriacao da captura - seguro.
struct WgcFrameHandler : WgcFrameArrivedBase {
    LONG   rc = 1;
    std::atomic<HANDLE>                 evt{ nullptr };
    std::atomic<std::atomic<UINT32>*>   counter{ nullptr };
    std::atomic<std::atomic<LONGLONG>*> arriveQpc{ nullptr };   // QPC do ultimo FrameArrived
    std::atomic<LONG>                   inFlight{ 0 };
    // RITMO2: a captura que este handler serve. Nulo = nao drenar (comportamento antigo,
    // ou desarmado). Desarmar() zera isto ANTES de esperar o inFlight, exatamente como ja
    // fazia com o evt e os contadores - e por isso o Teardown pode soltar o pool depois.
    std::atomic<ScreenCapture*>         dono{ nullptr };

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** o) override {
        if (!o) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IAgileObject) ||
            IsEqualIID(riid, __uuidof(WgcFrameArrivedBase))) {
            *o = static_cast<WgcFrameArrivedBase*>(this);
            InterlockedIncrement(&rc);
            return S_OK;
        }
        *o = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&rc); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&rc);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    virtual ~WgcFrameHandler() {}   // 'delete this' acima em classe polimorfica pede destrutor virtual
    HRESULT STDMETHODCALLTYPE Invoke(WGC::WGCa::IDirect3D11CaptureFramePool*, IInspectable*) override {
        inFlight.fetch_add(1, std::memory_order_acquire);
        LONGLONG q = g_lat.Now();
        if (auto* a = arriveQpc.load(std::memory_order_relaxed)) a->store(q, std::memory_order_relaxed);
        g_lat.ChegouFrame(q);
        if (auto* c = counter.load(std::memory_order_relaxed)) (*c)++;
        // RITMO2: drenar ANTES do SetEvent, para o laco acordar com o quadro ja no slot -
        // acordar primeiro e drenar depois deixaria o Tick pegar um slot vazio e perder a
        // volta inteira. O custo aqui e um TryGetNextFrame e um Close por quadro: microssegundos.
        if (auto* d = dono.load(std::memory_order_relaxed)) WgcDrenarParaSlot(d);
        if (HANDLE e = evt.load(std::memory_order_relaxed)) SetEvent(e);
        inFlight.fetch_sub(1, std::memory_order_release);
        return S_OK;
    }

    // Corta o handler de tudo que o Stop() vai destruir e espera quem ja estava dentro
    // do Invoke sair. O teto de espera existe para o encerramento nunca travar por causa
    // de uma thread do sistema presa.
    void Desarmar() {
        evt.store(nullptr, std::memory_order_relaxed);
        counter.store(nullptr, std::memory_order_relaxed);
        arriveQpc.store(nullptr, std::memory_order_relaxed);
        dono.store(nullptr, std::memory_order_relaxed);   // RITMO2: para de drenar antes da espera abaixo
        for (int i = 0; i < Saida::Voltas(500) && inFlight.load(std::memory_order_acquire) > 0; i++) Sleep(1);   // SAIDA RAPIDA: 200 voltas no encerramento
    }
};

// ------------------------------------------------------------------ handler Closed
// O WGC encerra a sessao sozinho quando o alvo some do jeito dele: o jogo troca de
// resolucao (recria o swapchain), vai pra tela cheia exclusiva, ou o item simplesmente
// deixa de existir. A JANELA CONTINUA VIVA nesses casos - IsWindow() diz que esta tudo
// bem enquanto nao chega mais nenhum frame.
//
// Sem este handler, o campo 'closed' so virava true quando a janela morria, e a condicao
// de recriacao do main.cpp (!Alive() && IsWindow(alvo)) era logicamente impossivel:
// codigo morto que parecia conserto. Este e o unico aviso que o sistema da.
typedef WGC::WF::ITypedEventHandler<WGC::WGCa::GraphicsCaptureItem*, IInspectable*> WgcClosedBase;

struct WgcClosedHandler : WgcClosedBase {
    LONG                             rc = 1;
    std::atomic<std::atomic<bool>*>  flag{ nullptr };   // vira true quando a sessao cai
    std::atomic<HANDLE>              evt{ nullptr };    // acorda o loop principal na hora
    std::atomic<LONG>                inFlight{ 0 };

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** o) override {
        if (!o) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IAgileObject) ||
            IsEqualIID(riid, __uuidof(WgcClosedBase))) {
            *o = static_cast<WgcClosedBase*>(this);
            InterlockedIncrement(&rc);
            return S_OK;
        }
        *o = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&rc); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&rc);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    virtual ~WgcClosedHandler() {}   // 'delete this' acima em classe polimorfica pede destrutor virtual
    HRESULT STDMETHODCALLTYPE Invoke(WGC::WGCa::IGraphicsCaptureItem*, IInspectable*) override {
        inFlight.fetch_add(1, std::memory_order_acquire);
        if (auto* f = flag.load(std::memory_order_relaxed)) f->store(true, std::memory_order_relaxed);
        if (HANDLE e = evt.load(std::memory_order_relaxed)) SetEvent(e);   // sem isto o loop so acordaria no timeout de 100 ms
        inFlight.fetch_sub(1, std::memory_order_release);
        return S_OK;
    }

    void Desarmar() {
        flag.store(nullptr, std::memory_order_relaxed);
        evt.store(nullptr, std::memory_order_relaxed);
        for (int i = 0; i < Saida::Voltas(500) && inFlight.load(std::memory_order_acquire) > 0; i++) Sleep(1);   // SAIDA RAPIDA: 200 voltas no encerramento
    }
};

// ------------------------------------------------------------------ a captura em si
struct ScreenCapture {
    // alvo
    HWND      targetWin = nullptr;
    HMONITOR  targetMon = nullptr;
    bool      clientOnly = true;    // so a area do cliente (tira barra de titulo/bordas)

    // OCLUSAO (23/09/2026): MONITOR SERVIDO POR UMA JANELA. Diferente de zero quando a
    // captura esta por HWND (targetWin) mas quem a pessoa escolheu foi o MONITOR que este
    // campo guarda. Existe porque a fonte monitor sobreposta tapa a propria fonte: a
    // pelicula cobre 100% do monitor, sai da captura (WDA_EXCLUDEFROMCAPTURE), a composicao
    // daquele monitor deixa de mudar e o WGC para de disparar FrameArrived (ver o bloco
    // "A CAMADA QUE TAPA A PROPRIA FONTE" no main.cpp e o texto da ConferirProvaDaDuplicacao
    // logo abaixo, que descreve a MESMA parada do lado da duplicacao). A captura por JANELA
    // nao tem esse problema - o WGC por HWND entrega o conteudo da janela mesmo com ela
    // inteiramente coberta -, entao no modo monitor o app troca o ITEM da captura por baixo
    // e continua se comportando como modo monitor em todo o resto.
    //
    // Quem le isto NAO pode tratar a captura como "fonte janela": o FollowTarget nao
    // esconde a pelicula quando o jogo perde o foco, a pelicula continua colada no monitor,
    // a duplicacao de desktop nao entra (ela duplica o monitor, e o monitor e justamente o
    // que esta parado) e o vigia de "o monitor foi desligado" continua olhando este handle.
    HMONITOR  monSubstituido = nullptr;

    // OCLUSAO (23/09/2026): objeto de PROVA (ver o bloco no Start). Nunca serve imagem a
    // ninguem: existe so para responder "esta janela entrega quadro?" sem parar a captura
    // que esta servindo. Marcado ANTES do Start.
    bool      provaSemCopia = false;

    // WinRT
    WGC::WGD3::IDirect3DDevice*              rtDev = nullptr;
    WGC::WGCa::IGraphicsCaptureItem*         item = nullptr;
    WGC::WGCa::IDirect3D11CaptureFramePool*  pool = nullptr;
    WGC::WGCa::IGraphicsCaptureSession*      session = nullptr;
    WgcFrameHandler*                         handler = nullptr;
    EventRegistrationToken                   token{};
    WgcClosedHandler*                        closedHandler = nullptr;
    EventRegistrationToken                   closedToken{};

    // D3D
    ID3D11Device*             dev = nullptr;
    ID3D11DeviceContext*      ictx = nullptr;  // contexto imediato cacheado (1x, nao por frame)
    ID3D11Texture2D*          tex = nullptr;   // recorte final, tamanho = w x h
    ID3D11ShaderResourceView* srv = nullptr;
    UINT w = 0, h = 0;
    UINT poolW = 0, poolH = 0;
    DXGI_FORMAT texFmt = DXGI_FORMAT_UNKNOWN;   // formato com que 'tex' foi criada

    // ---- formato da captura: 8 bits ou FP16
    // Com o HDR do Windows ligado, um frame pool BGRA8 nao recebe o buffer HDR da janela:
    // o compositor converte pra SDR com o tone map generico dele e entrega o resultado ja
    // lavado, ANTES de o ReShade existir no caminho. Pedindo o pool em
    // R16G16B16A16Float o Windows entrega scRGB LINEAR (BT.709, 1.0 = 80 nits) sem tone
    // map nenhum, e a conversao passa a ser nossa - feita no PS do app, com o brilho de
    // papel real do monitor. Ver docs\hdr.md.
    //
    // So em HDR. Com o desktop em SDR nao ha nada a ganhar e ha uma incerteza (o que o WGC
    // entrega num pool FP16 fora de HDR nao foi verificado), entao nem tentamos.
    bool  hdrFp16 = false;         // formato ATIVO agora (o main.cpp le isto)
    int   hdrFp16Mode = -1;        // -1 = automatico, 0 = forca 8 bits, 1 = forca FP16
    float hdrSdrWhiteNits = 80.f;  // paper white do monitor, pro shader normalizar

    WGC::WGDX::DirectXPixelFormat PoolFormat() const {
        return hdrFp16 ? WGC::WGDX::DirectXPixelFormat_R16G16B16A16Float
                       : WGC::WGDX::DirectXPixelFormat_B8G8R8A8UIntNormalized;
    }
    DXGI_FORMAT TexFormat() const {
        return hdrFp16 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    }

    HANDLE                 evt = nullptr;
    std::atomic<UINT32>    frames{ 0 };
    std::atomic<UINT32>    arrived{ 0 };
    std::atomic<LONGLONG>  arriveQpc{ 0 };

    // V1-CAPTURA (21/09/2026): CONTADORES DE 5 s DO CAMINHO DO QUADRO, para achar onde a fonte
    // acima de ~50 fps se perde. `arrived` = FrameArrived do WGC; `pool` = quadros que saíram
    // do pool (TryGetNextFrame); `copiados` = os que viraram recorte (= os que a cadeia vê);
    // `ticks` = voltas do laço que chegaram aqui. O histograma "por tick" diz quantos quadros
    // estavam na fila a cada volta (2+ = a volta demorou mais que um período da fonte e o
    // "só o mais novo" descartou); o de carimbos diz o intervalo entre quadros CONSECUTIVOS
    // DO POOL (todos, inclusive os descartados) — se ele já vem aos pares (< 2 ms), é o DWM.
    UINT32    v1Pool = 0, v1Cop = 0, v1Ticks = 0, v1ArrBase = 0;
    UINT32    v1PorTick[4]{};        // 0, 1, 2, 3+ quadros drenados numa volta
    UINT32    v1Dt[6]{};             // carimbo->carimbo: <2 | 2-6 | 6-10 | 10-14 | 14-20 | >20 ms
    UINT32    v1SrtIgual = 0;        // carimbo repetido (mesma composição entregue duas vezes)
    double    v1DtMax = 0.0;         // RODADA4: maior carimbo->carimbo da janela de 5 s (ms)
    LONGLONG  v1UltSrt = 0;
    ULONGLONG v1T0 = 0;

    // ---- RITMO2 (22/09/2026): o slot da chegada solta, e os contadores dele.
    //
    // `soltaNovo` e o quadro mais novo que o FrameArrived ja tirou do pool e que o Tick
    // ainda nao consumiu. Tudo o que for mais velho que ele ja foi devolvido ao WGC.
    //
    // `soltaAtropelado` e O CONTADOR QUE O DONO PEDIU: quantos quadros chegaram e foram
    // devolvidos SEM a cadeia ver, porque ela ainda estava no quadro anterior. E a medida
    // direta de "quantos quadros a rede nao alcancou" - com a fonte a 240/s e a cadeia a
    // 60/s ele fica perto de 180/s, e cair nele e o sinal de que a cadeia encostou na fonte.
    // `soltaRepetiu` e o outro lado: voltas do laco em que NAO havia quadro novo no slot e
    // a pelicula mostrou de novo o que ja estava (a rede adiantada em relacao a fonte).
    CRITICAL_SECTION soltaCs{};
    bool             soltaCsOk = false;
    WGC::WGCa::IDirect3D11CaptureFrame* soltaNovo = nullptr;   // sob soltaCs
    UINT32           soltaDesdeTick = 0;                       // sob soltaCs: chegadas desde o ultimo Tick
    UINT32           soltaAtropelado = 0, soltaRepetiu = 0, soltaEntregue = 0;   // sob soltaCs (os dois primeiros) / thread do render
    void V1Carimbo(LONGLONG srt) {
        if (v1UltSrt && srt) {
            const double d = (double)(srt - v1UltSrt) / 10000.0;
            if (d <= 0.0) v1SrtIgual++;
            else v1Dt[d < 2.0 ? 0 : d < 6.0 ? 1 : d < 10.0 ? 2 : d < 14.0 ? 3 : d < 20.0 ? 4 : 5]++;
            if (d > v1DtMax) v1DtMax = d;   // RODADA4: o maior buraco entre carimbos do pool
        }
        if (srt) v1UltSrt = srt;
    }
    // RITMO2: com a chegada solta, o `v1Pool` e o `carimbo dt` passam a ser escritos pela
    // thread do FrameArrived. A leitura de 5 s toma o MESMO cadeado da drenagem para tirar
    // o retrato - senao a soma sairia de um instante e a reposicao de outro, e a contagem
    // de 5 s do log (que o dono compara janela a janela) ficaria com furos.
    void V1Relatar() {
        const ULONGLONG ag = GetTickCount64();
        if (!v1T0) { v1T0 = ag; v1ArrBase = arrived.load(std::memory_order_relaxed); return; }
        if (ag - v1T0 < 5000) return;
        const double s = (ag - v1T0) / 1000.0;
        const UINT32 arr = arrived.load(std::memory_order_relaxed) - v1ArrBase;
        UINT32 pool_, dt_[6], igual_, atrop_, rep_, entr_;
        double dtMax_;
        if (soltaCsOk) EnterCriticalSection(&soltaCs);
        pool_ = v1Pool; igual_ = v1SrtIgual; memcpy(dt_, v1Dt, sizeof dt_); dtMax_ = v1DtMax;
        atrop_ = soltaAtropelado; rep_ = soltaRepetiu; entr_ = soltaEntregue;
        v1Pool = v1SrtIgual = 0; memset(v1Dt, 0, sizeof v1Dt); v1DtMax = 0.0;
        soltaAtropelado = soltaRepetiu = soltaEntregue = 0;
        if (soltaCsOk) LeaveCriticalSection(&soltaCs);
        Log("captura V1: arrived %.1f/s | pool %.1f/s | copiados %.1f/s | ticks %.1f/s | por tick 0:%u 1:%u 2:%u 3+:%u"
            " | carimbo dt <2:%u 2-6:%u 6-10:%u 10-14:%u 14-20:%u >20:%u igual:%u max %.1f ms | backend %s buffers %d",
            arr / s, pool_ / s, v1Cop / s, v1Ticks / s, v1PorTick[0], v1PorTick[1], v1PorTick[2], v1PorTick[3],
            dt_[0], dt_[1], dt_[2], dt_[3], dt_[4], dt_[5], igual_, dtMax_, backend == BK_DXGI ? "DXGI" : "WGC", optPoolBuf);
        // A LINHA NOVA: quanto da fonte a cadeia nao alcancou. Sai no WGC nos DOIS desenhos
        // (com e sem a chegada solta) - e justamente comparando os dois que ela serve.
        if (backend != BK_DXGI)
            Log("captura RITMO2: chegada %s | a cadeia NAO ALCANCOU %.1f/s (chegaram e voltaram ao pool sem "
                "virar imagem) | a pelicula REPETIU %.1f/s (volta sem quadro novo) | entregues ao laco %.1f/s "
                "| buffers %d", optSolta ? "solta" : "presa ao laco (padrao)",
                atrop_ / s, rep_ / s, entr_ / s, optPoolBuf);
        if (optV2 > 0) { Log("captura V2: grade T %.2f ms | descartados %u | ressincronizados %u", v2T100ns / 10000.0, v2Descartados, v2Ressinc); v2Descartados = v2Ressinc = 0; }
        // RODADA8: a fila em ordem - entregues, pendentes, teto, idade na fila e excedentes (para o painel/limitador do Agente D)
        if (backend != BK_DXGI && optDrain && optOrdem && !optSolta && !optNoCopy) {
            double p50 = 0.0, p95 = 0.0, mx = 0.0;
            if (ordIdadeN > 0) {
                std::sort(ordIdade, ordIdade + ordIdadeN);
                p50 = ordIdade[ordIdadeN / 2]; p95 = ordIdade[std::min(ordIdadeN - 1, (int)(ordIdadeN * 0.95))]; mx = ordIdade[ordIdadeN - 1];
            }
            const double entr = ordEntregues / s, exc = ordExced / s;
            Log("captura FILA: em ordem (sem descarte) | entregues %.1f/s | pendentes max %u (teto %d) | voltas com a fila no teto %u | idade do quadro na fila p50 %.2f / p95 %.2f / max %.2f ms | excedentes estimados %.0f (%.1f/s; %.0f na sessao) | intervalo da fonte fora do teto %.2f ms",
                entr, ordPendMax, optPoolBuf, ordTeto, p50, p95, mx, ordExced, exc, ordExcedTot, ordIntLivre);
            const bool avisoFila = (ordTeto > 0 && ordExced >= 1.0) || (ordTeto * 10 > (UINT32)(ordEntregues ? ordEntregues : 1));
            filaEntregues.store((float)entr); filaExced.store((float)exc); filaP50.store((float)p50); filaP95.store((float)p95);
            filaAviso.store(avisoFila); filaMs.store(ag);
            if (avisoFila) {
                Log("captura FILA: AVISO - a fonte passou da capacidade da rede: ~%.1f quadros/s excedentes (o DWM ficou sem buffer e nao entregou), fila no teto em %u voltas, idade na fila p95 %.1f ms. "
                    "Taxa SUSTENTAVEL ~%.1f fps (entregues com a fila no teto). Nenhum quadro que chegou foi descartado por nos; o remedio e limitar a fonte (limitador do jogo).",
                    exc, ordTeto, p95, entr);
            }
            ordTeto = 0; ordPendMax = 0; ordEntregues = 0; ordIdadeN = 0; ordExced = 0.0;
        }
        v1T0 = ag; v1ArrBase = arrived.load(std::memory_order_relaxed);
        v1Cop = v1Ticks = 0;
        memset(v1PorTick, 0, sizeof v1PorTick);
    }
    std::atomic<bool>      closed{ false };   // escrito pelo handler de Closed, em outra thread
    bool                   started = false;

    // geometria do recorte, cacheada: DwmGetWindowAttribute e uma chamada ao processo
    // do DWM, e ela estava sendo feita a cada frame no caminho critico.
    RECT     lastWinRect{}, cacheWant{}, cacheOrg{};
    bool     cacheOk = false;
    LONGLONG lastSrt100ns = 0;   // SystemRelativeTime do ultimo frame copiado

    // Chaves de diagnostico (variaveis de ambiente). O padrao ja e o modo rapido;
    // servem pra reproduzir o comportamento antigo e comparar numeros no log.
    //   DLSS5_DRAIN=0      volta a copiar TODOS os frames da fila (antigo)
    //   DLSS5_RECTCACHE=0  volta a perguntar a geometria ao DWM a cada frame (antigo)
    //   DLSS5_POOLBUF=N    numero de buffers do frame pool (padrao 2)
    //   DLSS5_NOCOPY=1     dreno e descarto sem copiar - mede so o ritmo bruto do WGC
    //   DLSS5_MINUPD=X     MinUpdateInterval em ms (padrao 0 = sem teto).
    //                      Negativo = nao mexe, volta ao padrao do Windows (16 ms = 50 fps
    //                      num monitor de 100 Hz). So serve pra comparar no log.
    int    optDrain     = 1;
    int    optRectCache = 1;
    // RITMO2 (22/09/2026): O NUMERO DE BUFFERS DO POOL VIROU CHAVE, E O PADRAO CONTINUA 2.
    //
    // O §2.2 original do STUTTER-TDR-2209 culpava o pool de 2 buffers pela queda da chegada
    // (238/s -> 63-74/s com a rede ligada). As medidas de 17:23-17:29 (§2.6) mudaram a
    // leitura: o gargalo e o CUSTO DO QUADRO (mediana 16,6-16,9 ms contra um orcamento de
    // 16,67; p99 travado em 21,2-21,8 = 5 refreshes de 240 Hz), e o acoplamento so PROPAGA
    // esse custo. O proprio relatorio diz, no §5.0c: "se o quadro custa 17,3 ms, tres
    // buffers nao devolvem a cadencia - so deixam a chegada oscilar mais. Vale como teste
    // barato DE DIAGNOSTICO, nao como correcao."
    //
    // Por isso a chave existe e o padrao NAO muda: 3 e o que o dono liga para responder a
    // pergunta em aberto do §5.6 ("se o pool de 2 buffers limita alguma coisa: continua nao
    // medido"). Custo de um buffer a mais: ~8 MB de VRAM a 1920x1080 BGRA8.
    // `[ultima] captura_buffers` no FafaDLSS5.ini; DLSS5_POOLBUF manda na frente dela.
    int    optPoolBuf   = 2;
    // RITMO2: a CHEGADA SOLTA - o FrameArrived drena o pool e guarda o mais novo num slot,
    // em vez de o Tick drenar depois do Present. PADRAO 0 (desligada) pela mesma razao
    // acima: com o quadro custando ~1 ms a mais que o orcamento, soltar a chegada faz o
    // `arrived` subir mas NAO devolve a cadencia por si so - e uma medida, nao uma correcao.
    // Fica atras de `[ultima] captura_solta=1` (ou DLSS5_SOLTA=1) como o experimento que
    // responde, com numero, se o pool/acoplamento limita alguma coisa. Ver RITMO2-2209 §3.
    int    optSolta     = 0;
    int    optNoCopy    = 0;
    // RODADA8 (PLANO-FLUIDEZ, Agente C, item 4): a captura sai EM ORDEM, sem o dreno "so o mais novo" (que descartava o que a
    // rede nao alcancava, ~105/s com o jogo a 240). Teto de pendentes = optPoolBuf; acima disso quem espera e a fonte (o DWM
    // sem buffer livre). DLSS5_CAP_ORDEM=0 volta ao dreno antigo (so bancada).
    int    optOrdem     = 1;
    WGC::WGCa::IDirect3D11CaptureFrame* ordSegura = nullptr;   // o mais velho ja tirado do pool, para a proxima volta
    // DEGRAU (sec. 21): quadros JA processados (copiados) que a captura segura para o DWM nao ter buffer livre entre
    // duas composicoes-alvo. Em ordem de chegada; a esteira devolve o mais velho na hora certa (DegrauLiberarUm).
    WGC::WGCa::IDirect3D11CaptureFrame* dgSeg[8] = {};
    int    dgSegN = 0;
    double dgUltLibMs = 0.0;
    void DegrauSegurar(WGC::WGCa::IDirect3D11CaptureFrame* f) {
        if (dgSegN >= 8) { CloseFrame(f); return; }
        dgSeg[dgSegN++] = f;
    }
    void DegrauLiberarUm() {
        if (dgSegN <= 0) return;
        CloseFrame(dgSeg[0]);
        for (int i = 1; i < dgSegN; i++) dgSeg[i - 1] = dgSeg[i];
        dgSeg[--dgSegN] = nullptr;
        dgUltLibMs = Degrau::Agora();
    }
    void DegrauSoltarTudo() { while (dgSegN > 0) DegrauLiberarUm(); }
    // true = todos os buffers do pool estao conosco: sem devolucao nossa, nada mais chega
    bool DegrauCheio() const { return dgSegN > 0 && dgSegN >= optPoolBuf && !ordSegura; }
    int  DegrauSegurados() const { return dgSegN; }
    UINT32 ordDeq = 0, ordTeto = 0, ordPendMax = 0, ordEntregues = 0;
    double ordIdade[1024] = {}; int ordIdadeN = 0;
    double ordIntLivre = 0.0, ordExced = 0.0, ordExcedTot = 0.0; LONGLONG ordUltSrt = 0; ULONGLONG ordAvisoMs = 0;
    // AGENTE D (PLANO-FLUIDEZ item 4): o retrato da ultima janela de 5 s da fila, para a politica "quando nao cabe"
    // (politica_cabe.h) ler do laco de 1 s. Escrito so no V1Relatar; `filaMs` = GetTickCount64 da janela (0 = nenhuma ainda).
    std::atomic<float> filaEntregues{ 0.f }, filaExced{ 0.f }, filaP50{ 0.f }, filaP95{ 0.f };
    std::atomic<bool>  filaAviso{ false };
    std::atomic<ULONGLONG> filaMs{ 0 };
    double optMinUpdMs  = 1.0;   // RODADA5 (PESQ-CAPTURA-2309 1.4): padrao 1000 us, como o Magpie - com < 1000 us (inclusive 0) o WGC do 24H2+ pode prender a entrega em ~50/s (robmikh #82); a 240 Hz (4,17 ms) 1 ms nao limita nada. Antes: 0.0
    // V2-CAPTURA (21/09/2026; LIGADO por padrao desde 21/09 16:40, ok do dono: "se der defeito na imagem eu falo"): AMOSTRAGEM REGULAR. Medido (V1-CAPTURA.md):
    // o DWM entrega um jogo que apresenta com sync 0 e teto de fps (COD a 80, alvo64 --fps N)
    // em ATE ~62-65 quadros/s, e com intervalos de 3, 4, 5 ou 8 vblanks para uma fonte
    // perfeitamente regular (alvo a 51: p05 12,5 / p50 16,7 / p95 33,3 ms). Nao e a captura:
    // a TELA mostra os mesmos ~60/s (sonda GetFrameStatistics no alvo: 80 apresentados -> 60,3
    // exibidos). O que se pode fazer daqui e entregar ao apresentador um carimbo REGULAR:
    //   DLSS5_V2=1   trava uma grade de periodo T (taxa medida em janela de ate 2 s, nao a
    //                mediana de 16, que le 60 onde a fonte e 51) na fase da chegada; cada
    //                quadro sai carimbado no no da grade. Ressincroniza num vao > 1,5 T.
    //   DLSS5_V2=k   (k >= 2) alem da grade, fica UM quadro por janela de k vblanks (os outros
    //                sao devolvidos sem copia): 240 Hz com k=4 = 60/s cravados.
    int      optV2 = 1;           // padrao 1 = grade na taxa medida (DLSS5_V2=0 desliga; k>=2 e o pulo real, so em ultimo caso)
    double   v2T100ns = 0.0;        // periodo da grade (100 ns)
    LONGLONG v2Grade = 0;           // ultimo no publicado
    LONGLONG v2Janela[128]{};       // carimbos brutos recentes (para a taxa)
    int      v2JanN = 0, v2JanPos = 0;
    LONGLONG v2UltSlot = -1;        // decimacao: ultima janela de k vblanks que rendeu quadro
    UINT32   v2Descartados = 0, v2Ressinc = 0;
    // Devolve o carimbo a publicar (100 ns) para um quadro bruto `srt`.
    LONGLONG V2Carimbo(LONGLONG srt, double vblank100ns) {
        if (optV2 <= 0 || srt <= 0) return srt;
        v2Janela[v2JanPos] = srt; v2JanPos = (v2JanPos + 1) % 128; if (v2JanN < 128) v2JanN++;
        // taxa = quadros / tempo na janela, lida da janela inteira (ate 128 quadros ou ~2 s)
        int n = v2JanN;
        while (n > 8) {   // encolhe ate caber em 2 s
            const LONGLONG mais = v2Janela[(v2JanPos - n + 128) % 128];
            if ((double)(srt - mais) <= 20000000.0) break;
            n--;
        }
        const LONGLONG mais = v2Janela[(v2JanPos - n + 128) % 128];
        double T = (n >= 8 && srt > mais) ? (double)(srt - mais) / (double)(n - 1) : 0.0;
        if (T <= 0.0) { v2Grade = srt; v2T100ns = 0.0; return srt; }
        if (optV2 >= 2 && vblank100ns > 0.0) T = vblank100ns * optV2;   // decimado: a grade E a janela
        if (v2T100ns <= 0.0 || !v2Grade) { v2T100ns = T; v2Grade = srt; return srt; }
        v2T100ns += 0.05 * (T - v2T100ns);
        LONGLONG prox = v2Grade + (LONGLONG)v2T100ns;
        const double erro = (double)(srt - prox);
        if (fabs(erro) > 1.5 * v2T100ns) { v2Ressinc++; v2Grade = srt; return srt; }   // vao ou pulo: recomeca na chegada
        // a grade segue a chegada devagar (1/16 do erro): absorve os 3/4/5 vblanks sem oscilar
        prox += (LONGLONG)(erro / 16.0);
        v2Grade = prox;
        return prox;
    }
    // Periodo do vblank do monitor da fonte, em 100 ns (0 = nao sei). Lido uma vez por segundo.
    double    v2Vb100ns = 0.0; ULONGLONG v2VbLido = 0;
    double V2Vblank100ns() {
        const ULONGLONG ag = GetTickCount64();
        if (ag - v2VbLido > 1000) {
            v2VbLido = ag;
            HMONITOR m = targetMon ? targetMon : (targetWin ? MonitorFromWindow(targetWin, MONITOR_DEFAULTTONEAREST) : nullptr);
            MONITORINFOEXW mi{}; mi.cbSize = sizeof mi;
            if (m && GetMonitorInfoW(m, &mi)) {
                DEVMODEW dm{}; dm.dmSize = sizeof dm;
                if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
                    v2Vb100ns = 10000000.0 / (double)dm.dmDisplayFrequency;
            }
        }
        return v2Vb100ns;
    }

    // =================================================================================
    //  QUEM ESTA SERVINDO (O3, 20/09/2026)
    //
    //  Duas portas de entrada, uma saida so: os dois backends terminam no mesmo `tex`/`srv`
    //  e no mesmo `evt`, entao NADA acima da captura sabe qual deles esta no ar. A troca em
    //  pleno voo (automatica) so mexe na parte WinRT; `tex`, `srv`, `ictx` e `evt` ficam de
    //  pe, e por isso o `R.extSrv` do laco continua valendo e ninguem pisca.
    //
    //  O CRITERIO DO AUTOMATICO nao e "o WGC e pior". E "o tremor que ele entrega nao cabe
    //  num periodo da tela": com `j` acima de um periodo o apresentador nao consegue mais
    //  por o quadro na composicao certa, e ai o engasgo aparece para a pessoa. Abaixo disso
    //  trocar seria mexer no que funciona - a duplicacao nao ve janela ocluida, entao ela e
    //  um remedio, nao um upgrade.
    // =================================================================================
    enum { BK_WGC = 0, BK_DXGI = 1 };
    enum { MODO_AUTO = 0, MODO_WGC = 1, MODO_DXGI = 2 };
    int          modo = MODO_AUTO;      // o que a pessoa escolheu no painel
    int          backend = BK_WGC;      // quem esta servindo AGORA
    CapturaDxgi  dupl;
    bool         duplRecusada = false;  // ja tentamos e nao deu: nao insistir a cada segundo
    std::wstring duplPorque;            // o motivo, para o painel e para o log
    ULONGLONG    obsDesde = 0;          // quando o WGC comecou a servir (relogio da observacao)
    double       obsJMs = -1.0;         // ultimo `j` do apresentador
    double       obsTelaHz = 0.0;       // ultimo periodo de tela conhecido
    bool         cursorSalvo = false, borderSalvo = false;   // o que o ultimo Start pediu
    // "A duplicacao nao serve mais": quem remonta a sessao WGC e o laco (ele e o dono da
    // textura de saida e do despertador), entao aqui so fica o pedido.
    bool         pedeVoltarWgc = false;

    // O TREMOR DA CHEGADA, MEDIDO AQUI. O apresentador ja calcula o mesmo numero (`j`), mas
    // ele o guarda para si (JMs() e privado) e nem sempre esta no ar - no modo sem
    // apresentador a captura ficaria sem criterio nenhum para decidir a troca. Entao ela
    // mede o proprio: sobre os ultimos 128 intervalos ENTRE CARIMBOS (SystemRelativeTime no
    // WGC, LastPresentTime na duplicacao), o p95 do desvio em relacao a mediana. Fonte
    // perfeita a 33,33 ms da ~0; o WGC pulando composicao da os 12-17 ms que medimos.
    double   tremorDt[128]{};
    int      tremorN = 0, tremorPos = 0;
    LONGLONG tremorUltCarimbo = 0;

    void TremorAmostra(LONGLONG carimbo100ns) {
        if (!carimbo100ns) return;
        if (tremorUltCarimbo && carimbo100ns > tremorUltCarimbo) {
            const double dt = (double)(carimbo100ns - tremorUltCarimbo) / 10000.0;
            if (dt > 0.0 && dt < 1000.0) {
                tremorDt[tremorPos] = dt;
                tremorPos = (tremorPos + 1) % (int)(sizeof(tremorDt) / sizeof(tremorDt[0]));
                if (tremorN < (int)(sizeof(tremorDt) / sizeof(tremorDt[0]))) tremorN++;
            }
        }
        tremorUltCarimbo = carimbo100ns;
    }

    double TremorMs() const {
        if (tremorN < 32) return -1.0;   // amostra curta demais para julgar nada
        double v[128];
        for (int i = 0; i < tremorN; i++) v[i] = tremorDt[i];
        std::sort(v, v + tremorN);
        const double mediana = v[tremorN / 2];
        double d[128];
        for (int i = 0; i < tremorN; i++) d[i] = (v[i] > mediana) ? (v[i] - mediana) : (mediana - v[i]);
        std::sort(d, d + tremorN);
        return d[(int)(tremorN * 0.95)];
    }

    const char* NomeBackend() const { return backend == BK_DXGI ? "DXGI" : "WGC"; }

    // O Log() e em UTF-8 e os motivos nascem em wchar_t. Conversao curta e local - o U8()
    // do main.cpp so existe depois deste header.
    static std::string U8Dbg(const std::wstring& s) {
        if (s.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string o((size_t)(n > 0 ? n : 0), '\0');
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &o[0], n, nullptr, nullptr);
        return o;
    }

    // A duplicacao NAO entra em HDR de proposito: em HDR o output entrega FP16 com outra
    // colorimetria e o caminho de conversao que temos (paper white, scRGB linear) esta
    // escrito para o pool do WGC. Ver o bloco do hdrFp16 mais acima.
    static bool MonitorEmHdr(HMONITOR m) {
        if (!m) return false;
        Hdr::DxgiInfo d = Hdr::QueryDxgi(m);
        return d.ok && d.hdr;
    }

    // O monitor que interessa: o da JANELA da fonte (ou o monitor alvo, quando a fonte e
    // um monitor). E o mesmo criterio da pelicula - ver FollowTarget no main.cpp.
    HMONITOR MonitorDaFonte() const {
        if (targetMon) return targetMon;
        if (targetWin && IsWindow(targetWin)) return MonitorFromWindow(targetWin, MONITOR_DEFAULTTONEAREST);
        return nullptr;
    }

    // ---- helpers WinRT
    static HRESULT Factory(const wchar_t* cls, REFIID iid, void** out) {
        HSTRING_HEADER hdr; HSTRING hs = nullptr;
        HRESULT hr = WindowsCreateStringReference(cls, (UINT32)wcslen(cls), &hdr, &hs);
        if (FAILED(hr)) return hr;
        return RoGetActivationFactory(hs, iid, out);
    }

    // Tamanho "de conteudo" que queremos entregar pro renderer (o recorte).
    bool TargetRect(RECT* out, RECT* frameOrigin, bool* usouMoldura = nullptr) {
        if (usouMoldura) *usouMoldura = false;
        if (targetMon) {
            MONITORINFO mi{}; mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(targetMon, &mi)) return false;
            *frameOrigin = mi.rcMonitor;
            *out = mi.rcMonitor;
            return true;
        }
        if (!targetWin || !IsWindow(targetWin)) return false;
        RECT fb{};
        if (FAILED(DwmGetWindowAttribute(targetWin, DWMWA_EXTENDED_FRAME_BOUNDS, &fb, sizeof(fb)))) {
            if (!GetWindowRect(targetWin, &fb)) return false;
        }
        *frameOrigin = fb;
        if (!clientOnly) { *out = fb; return true; }
        // DAQUI PARA BAIXO, cair no `fb` e um ACIDENTE, nao uma escolha: quer dizer que a
        // area util nao pode ser medida NESTE instante (janela ainda assentando, no meio de
        // um resize). Quem chama precisa saber, senao guarda a moldura no cache e captura a
        // barra de titulo pelo resto da sessao - foi o que aconteceu em 14/09/2026 04:33
        // (`fonte mudou ... para 1286x808`, que e a moldura DWM, com a area util em 1280x720).
        RECT cr{};
        if (!GetClientRect(targetWin, &cr)) { *out = fb; if (usouMoldura) *usouMoldura = true; return true; }
        POINT tl{ cr.left, cr.top }, br{ cr.right, cr.bottom };
        ClientToScreen(targetWin, &tl); ClientToScreen(targetWin, &br);
        RECT c{ tl.x, tl.y, br.x, br.y };
        if (c.right - c.left < 16 || c.bottom - c.top < 16) { *out = fb; if (usouMoldura) *usouMoldura = true; return true; }
        *out = c;
        // OS TRES RETANGULOS, NO LOG, sempre que algum muda. Em 14/09/2026 o app dizia estar
        // capturando 1276x776 enquanto uma medicao por fora dava janela 1306x791 e area util
        // 1270x688 - e nao havia como saber se era a nossa conta que estava errada, se era
        // outro instante, ou se o `clientOnly` nem estava valendo. Tres numeros numa linha
        // respondem isso na proxima abertura, sem depender de ninguem medir por fora.
        // (O processo e PER_MONITOR_AWARE_V2, entao tudo aqui e pixel de verdade.)
        {
            RECT wr{}; GetWindowRect(targetWin, &wr);
            static RECT ultC{}, ultW{}, ultF{};
            if (memcmp(&ultC, &c, sizeof(RECT)) || memcmp(&ultW, &wr, sizeof(RECT)) || memcmp(&ultF, &fb, sizeof(RECT))) {
                ultC = c; ultW = wr; ultF = fb;
                Log("captura: client %ldx%ld em %ld,%ld | janela %ldx%ld | moldura DWM %ldx%ld | recorte=%s",
                    (long)(c.right - c.left), (long)(c.bottom - c.top), (long)c.left, (long)c.top,
                    (long)(wr.right - wr.left), (long)(wr.bottom - wr.top),
                    (long)(fb.right - fb.left), (long)(fb.bottom - fb.top),
                    clientOnly ? "area util" : "JANELA INTEIRA (a moldura entra na imagem)");
            }
        }
        return true;
    }

    // Mesma coisa, mas so paga o DwmGetWindowAttribute/ClientToScreen quando a janela
    // realmente se mexeu. GetWindowRect e local (nao vai ao DWM), entao e barato.
    bool TargetRectCached(RECT* out, RECT* frameOrigin) {
        if (!optRectCache) return TargetRect(out, frameOrigin);
        if (targetMon) {
            if (!cacheOk) { if (!TargetRect(&cacheWant, &cacheOrg)) return false; cacheOk = true; }
            *out = cacheWant; *frameOrigin = cacheOrg; return true;
        }
        RECT wr{};
        if (!targetWin || !GetWindowRect(targetWin, &wr)) return false;
        if (!cacheOk || memcmp(&wr, &lastWinRect, sizeof(RECT)) != 0) {
            bool moldura = false;
            if (!TargetRect(&cacheWant, &cacheOrg, &moldura)) return false;
            lastWinRect = wr;
            // SO GUARDA O QUE VALE. Se a area util nao pode ser medida agora, o retangulo
            // da moldura serve para ESTE quadro e mais nada: sem isto, o cache so seria
            // refeito quando a JANELA mudasse de tamanho - e uma janela parada nunca muda,
            // entao a moldura ficava capturada para sempre.
            cacheOk = !moldura;
        }
        *out = cacheWant; *frameOrigin = cacheOrg;
        return true;
    }

    bool EnsureTex(UINT nw, UINT nh) {
        // O formato entra na comparacao porque hdrFp16 pode mudar entre dois Start()
        // (o monitor do alvo entrou ou saiu de HDR). Com so o tamanho na conta, a
        // textura antiga sobreviveria em BGRA8 enquanto o pool ja entrega FP16, e o
        // CopySubresourceRegion falharia calado - imagem parada e nenhum erro.
        const DXGI_FORMAT want = TexFormat();
        if (tex && nw == w && nh == h && texFmt == want) return true;
        if (srv) { srv->Release(); srv = nullptr; }
        if (tex) { tex->Release(); tex = nullptr; }
        texFmt = DXGI_FORMAT_UNKNOWN;
        w = nw; h = nh;
        if (!w || !h) return false;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = want; td.SampleDesc.Count = 1;
        // RENDER_TARGET junto com SHADER_RESOURCE: e o que permite desenhar o ponteiro
        // nesta mesma textura, depois do recorte (ver cursor_pelicula.h). Nao muda nada
        // para quem so le - o custo de pedir o bind a mais e zero.
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &tex))) return false;
        if (FAILED(dev->CreateShaderResourceView(tex, nullptr, &srv))) {
            tex->Release(); tex = nullptr;   // sem SRV a textura nao serve pra nada
            return false;
        }
        texFmt = want;
        return true;
    }

    bool RecreatePool(UINT pw, UINT ph) {
        if (!pool) return false;
        WGC::WG::SizeInt32 sz{ (INT32)pw, (INT32)ph };
        // RITMO2: o Recreate troca os buffers por baixo do pool, e agora ha uma segunda
        // thread (a da chegada) pedindo frames a ele. O cadeado da drenagem serve tambem
        // aqui - e no mesmo movimento o quadro guardado no slot e devolvido, porque ele e
        // do tamanho VELHO e copia-lo depois da troca daria recorte errado por uma volta.
        const bool trava = soltaCsOk && optSolta;
        if (trava) {
            EnterCriticalSection(&soltaCs);
            if (soltaNovo) { CloseFrame(soltaNovo); soltaNovo = nullptr; }
            soltaDesdeTick = 0;
        }
        if (ordSegura) { CloseFrame(ordSegura); ordSegura = nullptr; } DegrauSoltarTudo(); if (Degrau::g_cap == this) Degrau::g_cap = nullptr;   // RODADA8: o da mao e do tamanho velho
        const HRESULT hr = pool->Recreate(rtDev, PoolFormat(), optPoolBuf, sz);
        if (trava) LeaveCriticalSection(&soltaCs);
        if (FAILED(hr)) return false;
        poolW = pw; poolH = ph;
        cacheOk = false;   // o alvo mudou de tamanho: a geometria cacheada morreu
        return true;
    }

    // ---- ciclo de vida
    bool Start(ID3D11Device* d3d, HWND win, HMONITOR mon, bool clientArea, bool cursor,
               bool border, std::wstring* err) {
        dev = d3d; targetWin = win; targetMon = mon; clientOnly = clientArea;
        // OCLUSAO (23/09/2026): um Start de MONITOR DE VERDADE desfaz a substituicao. Sem
        // esta linha, qualquer caminho de recriacao que passasse C.mon (a retomada da
        // queda, a volta da duplicacao, o desligar do modo integrado) deixaria o campo
        // sujo e o resto do app continuaria acreditando numa janela que ja nao e o item.
        if (mon) monSubstituido = nullptr;

        // Recomeco limpo - e isto que faz o Stop()+Start() da recriacao funcionar. Sem
        // zerar 'closed' a captura recriada ja nasce marcada como morta e o laco de
        // retomada do main.cpp queima as 10 tentativas sem nunca sair do lugar. Se
        // sobrou alguma coisa de uma sessao anterior (um Start sem Stop antes), ela sai
        // aqui; o 'evt' fica, porque o loop principal ja pode estar esperando nele.
        if (started || pool || session || item || rtDev) Teardown(true);
        closed.store(false, std::memory_order_relaxed);
        frames.store(0, std::memory_order_relaxed);
        arrived.store(0, std::memory_order_relaxed);
        ordDeq = 0; ordUltSrt = 0; ordIntLivre = 0.0;   // RODADA8: a fila em ordem recomeca com a sessao
        arriveQpc.store(0, std::memory_order_relaxed);
        // DRIVER1 (23/09/2026): a janela de 5 s guarda a BASE de `arrived` (`v1ArrBase`) para
        // fazer a diferenca. O Start zerava `arrived` e deixava a base com o valor da sessao
        // anterior, entao a primeira subtracao sem sinal dava a volta e o log saia com
        // `captura V1: arrived 531686111.0/s`. Zerar a janela junto: com `v1T0 = 0` a proxima
        // V1Relatar so recarimba a base e comeca uma janela nova.
        v1T0 = 0; v1ArrBase = 0; v1Pool = v1Cop = v1Ticks = 0; v1SrtIgual = 0; v1UltSrt = 0;
        memset(v1PorTick, 0, sizeof v1PorTick);
        memset(v1Dt, 0, sizeof v1Dt); v1DtMax = 0.0;
        cacheOk = false; poolW = poolH = 0; lastSrt100ns = 0;
        lastWinRect = RECT{};

        // O evento e criado ANTES de qualquer caminho de erro. Se o Start falhar la na
        // frente (aconteceu: alvo em tela cheia exclusiva), o main.cpp continua com um
        // handle valido para esperar; com evt nulo ou ja fechado, o
        // MsgWaitForMultipleObjects devolve WAIT_FAILED na hora e o loop vira busy-loop
        // de 100% de um nucleo.
        if (!evt) evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        // Daqui pra frente, qualquer 'return false' desfaz o que ja foi criado (menos o
        // 'evt'). Antes, cada tentativa de recriacao vazava pool, handler e device WinRT
        // e deixava a struct montada pela metade para o Tick seguinte tropecar.
        struct Desfaz {
            ScreenCapture* s;
            bool armado = true;
            ~Desfaz() { if (armado) s->Teardown(true); }
        } desfaz{ this, true };

        {   char b[16];
            if (GetEnvironmentVariableA("DLSS5_DRAIN",     b, 16)) optDrain     = atoi(b);
            if (GetEnvironmentVariableA("DLSS5_RECTCACHE", b, 16)) optRectCache = atoi(b);
            if (GetEnvironmentVariableA("DLSS5_NOCOPY",    b, 16)) optNoCopy    = atoi(b);
            if (GetEnvironmentVariableA("DLSS5_CAP_ORDEM", b, 16)) optOrdem     = atoi(b);
            if (GetEnvironmentVariableA("DLSS5_POOLBUF",   b, 16)) { optPoolBuf = atoi(b); if (optPoolBuf < 1) optPoolBuf = 1; }
            // RITMO2: DLSS5_SOLTA=0 devolve a drenagem para a thread do render (o desenho
            // de ate 21/09). Existe para a bancada comparar os dois sem trocar de exe.
            if (GetEnvironmentVariableA("DLSS5_SOLTA",     b, 16)) optSolta     = atoi(b);
            if (GetEnvironmentVariableA("DLSS5_MINUPD",    b, 16)) optMinUpdMs  = atof(b);
            if (GetEnvironmentVariableA("DLSS5_V2",        b, 16)) optV2        = atoi(b);
            if (GetEnvironmentVariableA("DLSS5_FP16",      b, 16)) hdrFp16Mode  = atoi(b);
            // DLSS5_CAPTURA=0 automatico, 1 so WGC, 2 so duplicacao. Manda na frente do painel:
            // e a chave de bancada para comparar os dois backends no mesmo alvo.
            if (GetEnvironmentVariableA("DLSS5_CAPTURA",   b, 16)) {
                const int v = atoi(b);
                if (v >= MODO_AUTO && v <= MODO_DXGI) modo = v;
            }
            // OCLUSAO (23/09/2026): A SESSAO DE PROVA NAO TOCA NO D3D, E ISSO E OBRIGATORIO.
            // Ela sobe ao lado da sessao que esta servindo, no MESMO device - e a chegada
            // solta (`optSolta`, o padrao) copia o quadro na thread da chegada usando o
            // contexto IMEDIATO desse device. Cada objeto de captura tem o cadeado dele, o
            // que protege um objeto de si mesmo e nao protege NADA entre dois: duas threads
            // de chegada copiando no mesmo ID3D11DeviceContext seria corrida de verdade, do
            // tipo que nao aparece na bancada e aparece na maquina de quem esta jogando.
            // A prova so precisa saber se FrameArrived dispara - e `arrived` e contado pelo
            // handler, antes de qualquer copia. Entao aqui ela fica sem copia e sem solta, e
            // quem a esvazia e o Drenar() do laco (TryGetNextFrame + Close, zero D3D).
            // As chaves de bancada nao mandam nisto: e correcao, nao preferencia.
            if (provaSemCopia) { optSolta = 0; optNoCopy = 1; }
        }

        // ---- formato da captura, decidido ANTES de criar o pool (nao da pra trocar depois)
        {
            HMONITOR m = targetMon ? targetMon
                       : (targetWin ? MonitorFromWindow(targetWin, MONITOR_DEFAULTTONEAREST) : nullptr);
            Hdr::DxgiInfo d = Hdr::QueryDxgi(m);
            const bool hdrOn = d.ok && d.hdr;
            hdrFp16 = (hdrFp16Mode < 0) ? hdrOn : (hdrFp16Mode == 1);

            // O paper white real do monitor e o que o shader usa para normalizar. Sem ele
            // a conversao fica 3x errada nesta maquina, onde o usuario poe o brilho SDR
            // em 240 nits em vez dos 80 de referencia.
            auto ts = Hdr::TargetsForMonitor(m);
            if (!ts.empty()) {
                Hdr::Info i = Hdr::Query(ts[0]);
                if (i.ok && i.sdrWhiteNits > 0.f) hdrSdrWhiteNits = i.sdrWhiteNits;
            }
            Log("captura: monitor %s -> pool %s (paper white %.0f nits)%s",
                hdrOn ? "em HDR" : "em SDR",
                hdrFp16 ? "R16G16B16A16_FLOAT (scRGB linear)" : "B8G8R8A8_UNORM",
                hdrSdrWhiteNits,
                hdrFp16Mode < 0 ? "" : " [forcado por DLSS5_FP16]");
        }

        HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
            if (err) *err = T(L"RoInitialize falhou (WinRT indisponivel).", L"RoInitialize failed (WinRT unavailable).");
            return false;
        }

        // device WinRT em cima do nosso device D3D11 -> textura sem copia entre devices
        IDXGIDevice* dxgi = nullptr;
        if (FAILED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi))) {
            if (err) *err = T(L"QueryInterface(IDXGIDevice) falhou.", L"QueryInterface(IDXGIDevice) failed.");
            return false;
        }
        IInspectable* insp = nullptr;
        hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi, &insp);
        dxgi->Release();
        if (FAILED(hr) || !insp) { if (err) *err = T(L"CreateDirect3D11DeviceFromDXGIDevice falhou.", L"CreateDirect3D11DeviceFromDXGIDevice failed."); return false; }
        hr = insp->QueryInterface(__uuidof(WGC::WGD3::IDirect3DDevice), (void**)&rtDev);
        insp->Release();
        if (FAILED(hr)) { if (err) *err = T(L"IDirect3DDevice indisponivel.", L"IDirect3DDevice unavailable."); return false; }

        // item: janela ou monitor
        IGraphicsCaptureItemInterop* interop = nullptr;
        hr = Factory(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem,
                     __uuidof(IGraphicsCaptureItemInterop), (void**)&interop);
        if (FAILED(hr) || !interop) {
            if (err) *err = T(L"Windows Graphics Capture indisponivel nesse Windows (precisa 10 1903+).", L"Windows Graphics Capture is unavailable on this Windows (needs 10 1903+).");
            return false;
        }
        if (targetWin) hr = interop->CreateForWindow(targetWin, __uuidof(WGC::WGCa::IGraphicsCaptureItem), (void**)&item);
        else           hr = interop->CreateForMonitor(targetMon, __uuidof(WGC::WGCa::IGraphicsCaptureItem), (void**)&item);
        interop->Release();
        if (FAILED(hr) || !item) {
            if (err) *err = targetWin ? T(L"Nao consegui capturar essa janela (ela fechou? e protegida?).", L"Could not capture that window (did it close? is it protected?).")
                                      : T(L"Nao consegui capturar esse monitor.", L"Could not capture that monitor.");
            return false;
        }

        // pool no tamanho do item
        WGC::WG::SizeInt32 isz{};
        item->get_Size(&isz);
        if (isz.Width <= 0 || isz.Height <= 0) { if (err) *err = T(L"Tamanho do alvo invalido.", L"Invalid target size."); return false; }

        WGC::WGCa::IDirect3D11CaptureFramePoolStatics2* st = nullptr;
        hr = Factory(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool,
                     __uuidof(WGC::WGCa::IDirect3D11CaptureFramePoolStatics2), (void**)&st);
        if (FAILED(hr) || !st) { if (err) *err = T(L"Direct3D11CaptureFramePool indisponivel.", L"Direct3D11CaptureFramePool unavailable."); return false; }
        hr = st->CreateFreeThreaded(rtDev, PoolFormat(), optPoolBuf, isz, &pool);
        st->Release();
        if (FAILED(hr) || !pool) { if (err) *err = T(L"CreateFreeThreaded falhou.", L"CreateFreeThreaded failed."); return false; }
        poolW = (UINT)isz.Width; poolH = (UINT)isz.Height;

        // contexto imediato pego uma vez so: GetImmediateContext por frame custa um
        // AddRef/Release e um lock interno no caminho critico, sem motivo nenhum.
        dev->GetImmediateContext(&ictx);
        if (!ictx) { if (err) *err = T(L"GetImmediateContext falhou.", L"GetImmediateContext failed."); return false; }

        // RITMO2: o diagnostico `DLSS5_NOCOPY=1` mede o ritmo bruto do WGC drenando na
        // thread do render; com a chegada solta haveria dois donos do pool. Um exclui o outro.
        if (optNoCopy && optSolta) { optSolta = 0; Log("captura: DLSS5_NOCOPY=1 - chegada solta desligada nesta sessao (um dono do pool de cada vez)"); }
        // O cadeado da chegada solta nasce UMA vez por objeto e nao e destruido no Stop:
        // uma thread do sistema ainda dentro do Invoke pode toca-lo, e um Stop()+Start()
        // (a recriacao da captura) acontece com frequencia. Um CRITICAL_SECTION vazado por
        // objeto de captura e mais barato do que a corrida que a destruicao abriria.
        // O cadeado sobe sempre (os contadores RITMO2 valem nos dois desenhos); so a
        // DRENAGEM na thread da chegada e que depende do optSolta.
        if (!soltaCsOk) { InitializeCriticalSection(&soltaCs); soltaCsOk = true; }

        handler = new WgcFrameHandler();
        handler->evt = evt; handler->counter = &arrived; handler->arriveQpc = &arriveQpc;
        if (optSolta) handler->dono.store(this, std::memory_order_relaxed);
        hr = pool->add_FrameArrived(handler, &token);
        if (FAILED(hr)) { if (err) *err = T(L"add_FrameArrived falhou.", L"add_FrameArrived failed."); return false; }

        // Aviso de queda da sessao. Nao e fatal se falhar: o app so perde a deteccao
        // rapida e volta a depender da janela morrer.
        closedHandler = new WgcClosedHandler();
        closedHandler->flag = &closed; closedHandler->evt = evt;
        if (FAILED(item->add_Closed(closedHandler, &closedToken))) {
            Log("captura: add_Closed falhou - a queda da sessao so sera vista se a janela fechar");
            closedHandler->Release(); closedHandler = nullptr;
        }

        hr = pool->CreateCaptureSession(item, &session);
        if (FAILED(hr) || !session) { if (err) *err = T(L"CreateCaptureSession falhou.", L"CreateCaptureSession failed."); return false; }

        // cursor e a borda amarela de "esta sendo capturado" (Win11) sao opcionais
        WGC::WGCa::IGraphicsCaptureSession2* s2 = nullptr;
        if (SUCCEEDED(session->QueryInterface(__uuidof(WGC::WGCa::IGraphicsCaptureSession2), (void**)&s2)) && s2) {
            s2->put_IsCursorCaptureEnabled(cursor ? 1 : 0); s2->Release();
        }
        WGC::WGCa::IGraphicsCaptureSession3* s3 = nullptr;
        if (SUCCEEDED(session->QueryInterface(__uuidof(WGC::WGCa::IGraphicsCaptureSession3), (void**)&s3)) && s3) {
            s3->put_IsBorderRequired(border ? 1 : 0); s3->Release();
        }

        // Tira o teto de atualizacao do WGC (ver o comentario da IWgcSession5 la em cima).
        // Sem isso o Windows entrega 1 frame a cada 16 ms no minimo, o que num monitor de
        // 100 Hz vira 1 a cada 2 vblanks = 50,0 fps, independente do fps do jogo.
        {
            IWgcSession5* s5 = nullptr;
            if (SUCCEEDED(session->QueryInterface(IID_WgcSession5, (void**)&s5)) && s5) {
                WGC::WF::TimeSpan cur{};
                s5->get_MinUpdateInterval(&cur);
                if (optMinUpdMs >= 0.0) {
                    WGC::WF::TimeSpan nv{ (INT64)(optMinUpdMs * 10000.0) };
                    HRESULT hr5 = s5->put_MinUpdateInterval(nv);
                    WGC::WF::TimeSpan got{};
                    s5->get_MinUpdateInterval(&got);
                    // DWM-SONDA (23/09/2026): a linha em MICROSSEGUNDOS e a que a bancada grepa,
                    // porque a chave do ini e em us. 0 = o pedido de sempre (sem teto).
                    const int usPed = (int)(optMinUpdMs * 1000.0 + 0.5);
                    if (usPed > 0) Log("captura: MinUpdateInterval=%d us (chave captura_min_update_us)", usPed);
                    else           Log("captura: MinUpdateInterval=0 (padrao do sistema)");
                    Log("captura: MinUpdateInterval %.2f ms -> %.2f ms (pedido %.2f, hr=0x%08lX)",
                        (double)cur.Duration / 10000.0, (double)got.Duration / 10000.0,
                        optMinUpdMs, (unsigned long)hr5);
                } else {
                    Log("captura: MinUpdateInterval mantido no padrao do Windows (%.2f ms) - DLSS5_MINUPD<0",
                        (double)cur.Duration / 10000.0);
                }
                s5->Release();
            } else {
                Log("captura: sem IGraphicsCaptureSession5 neste Windows - o sistema impoe"
                    " no minimo 16 ms entre frames (teto de 50 fps em monitor de 100 Hz)");
            }
        }

        // primeira geometria (pode mudar a qualquer momento se a janela for redimensionada)
        RECT want{}, org{};
        if (!TargetRect(&want, &org)) { if (err) *err = T(L"Nao consegui medir o alvo.", L"Could not measure the target."); return false; }
        if (!EnsureTex((UINT)(want.right - want.left), (UINT)(want.bottom - want.top))) {
            if (err) *err = T(L"Falha ao criar a textura do recorte.", L"Failed to create the crop texture.");
            return false;
        }

        hr = session->StartCapture();
        if (FAILED(hr)) {
            if (err) *err = TS(L"StartCapture falhou ", L"StartCapture failed ") + T(L"(a captura de tela esta bloqueada por politica?)", L"(is screen capture blocked by policy?)");
            return false;
        }
        Log("captura: buffers=%d so-o-frame-mais-novo=%d geometria-cacheada=%d | V2 amostragem regular=%s"
            " | RITMO2 chegada solta=%s (%s) | RODADA8 fila em ordem=%d (teto %d pendentes, sem descarte)",
            optPoolBuf, (optDrain && !(optOrdem && !optSolta)) ? 1 : 0, optRectCache, optV2 <= 0 ? "desligada" : optV2 == 1 ? "grade na taxa medida" : "grade + 1 quadro por k vblanks",
            optSolta ? "ligada" : "desligada",
            optSolta ? "o FrameArrived devolve os buffers ao WGC no ritmo da captura, sem esperar o Present"
                     : "o Tick devolve os buffers depois do Present (desenho ate 21/09/2026)",
            (optDrain && optOrdem && !optSolta) ? 1 : 0, optPoolBuf);
        started = true;
        desfaz.armado = false;   // subiu inteiro: nada a desfazer

        // A ESCOLHA DE BACKEND ACONTECE DEPOIS DE O WGC SUBIR, e nao no lugar dele. O
        // caminho do WGC e o testado; se a duplicacao nao puder servir (outra placa, HDR,
        // monitor girado, outro gravador ja duplicando) a captura ja esta no ar e ninguem
        // fica sem imagem. O preco e meio segundo de WGC antes da troca manual.
        cursorSalvo = cursor; borderSalvo = border;
        backend = BK_WGC;
        obsDesde = GetTickCount64();
        // `duplRecusada` SOBREVIVE ao Start de proposito: quando a duplicacao deixou de
        // servir (o monitor entrou em HDR, a fonte foi para outra placa), este Start e
        // justamente a volta para o WGC - tentar a duplicacao de novo aqui seria um
        // vai-e-volta a cada segundo. Quem limpa a marca e a pessoa, no painel.
        if (modo == MODO_DXGI && !duplRecusada) TrocarParaDuplicacao("escolha da pessoa (modo DXGI)");
        return true;
    }

    // Sessao e pool do WGC sao IClosable: o Release sozinho nao promete que a captura
    // parou agora, o Close promete. Importa no Stop()+Start() da recriacao, para a
    // sessao velha nao ficar entregando frames enquanto a nova sobe.
    template <class T> static void CloseRelease(T*& p) {
        if (!p) return;
        WGC::WF::IClosable* cl = nullptr;
        if (SUCCEEDED(p->QueryInterface(__uuidof(WGC::WF::IClosable), (void**)&cl)) && cl) {
            cl->Close(); cl->Release();
        }
        p->Release(); p = nullptr;
    }

    // O RECORTE, SOZINHO - e a unica parte que os dois backends compartilham, e por isso
    // ela saiu de dentro do CopyFrame. `orgL/orgT` e o canto superior esquerdo, EM
    // COORDENADAS DE TELA, do que a textura de origem representa: a moldura da janela no
    // WGC, o canto do monitor na duplicacao. `csW/csH` limitam a leitura ao conteudo valido
    // (o frame pool do WGC pode ser maior que o conteudo; na duplicacao e a textura inteira).
    bool RecortarDe(ID3D11Texture2D* src, LONG orgL, LONG orgT, INT32 csW, INT32 csH) {
        if (!src || !ictx) return false;
        D3D11_TEXTURE2D_DESC sd{}; src->GetDesc(&sd);
        // NS-2309 item 5(b) (conferencia vinda do NeuralScreen v1.14: em tela de 10 bits o DDA
        // deles alternava FP16 <-> BGRA8 e reconstruia 1955 vezes em 133 s - ideia, codigo proprio).
        // O nosso formato e FIXO por sessao (PoolFormat() no pool do WGC; DuplicateOutput legado,
        // sempre BGRA8, na duplicacao), entao isto NUNCA deve acontecer. Se acontecer, a copia
        // entre formatos diferentes falharia calada: o quadro e recusado e o log conta.
        const DXGI_FORMAT fmtEsperado = TexFormat();
        if (sd.Format != fmtEsperado) {
            static unsigned s_fmtTroca = 0;
            ++s_fmtTroca;
            if (s_fmtTroca <= 3 || (s_fmtTroca % 500) == 0)
                Log("captura: CONFERENCIA formato - o quadro chegou em DXGI %d e o pool/recorte e %d "
                    "(%u quadros assim nesta sessao; quadro recusado, nada copiado)",
                    (int)sd.Format, (int)fmtEsperado, s_fmtTroca);
            return false;
        }
        RECT want{}, org{};
        if (!TargetRectCached(&want, &org)) return false;
        LONG ox = want.left - orgL, oy = want.top - orgT;
        LONG cw = want.right - want.left, chh = want.bottom - want.top;
        LONG maxW = (LONG)((csW > 0 && (UINT)csW <= sd.Width)  ? csW : (INT32)sd.Width);
        LONG maxH = (LONG)((csH > 0 && (UINT)csH <= sd.Height) ? csH : (INT32)sd.Height);
        if (ox < 0) { cw += ox; ox = 0; }
        if (oy < 0) { chh += oy; oy = 0; }
        if (ox + cw > maxW) cw = maxW - ox;
        if (oy + chh > maxH) chh = maxH - oy;
        if (cw <= 0 || chh <= 0 || !EnsureTex((UINT)cw, (UINT)chh)) return false;
        D3D11_BOX box{ (UINT)ox, (UINT)oy, 0, (UINT)(ox + cw), (UINT)(oy + chh), 1 };
        ictx->CopySubresourceRegion(tex, 0, 0, 0, 0, src, 0, &box);
        return true;
    }

    static void CloseFrame(WGC::WGCa::IDirect3D11CaptureFrame* f) {
        if (!f) return;
        WGC::WF::IClosable* cl = nullptr;
        if (SUCCEEDED(f->QueryInterface(__uuidof(WGC::WF::IClosable), (void**)&cl)) && cl) { cl->Close(); cl->Release(); }
        f->Release();
    }

    // Recorta UM frame pra tex. Devolve true se copiou.
    bool CopyFrame(WGC::WGCa::IDirect3D11CaptureFrame* f) {
        WGC::WF::TimeSpan ts{};
        lastSrt100ns = SUCCEEDED(f->get_SystemRelativeTime(&ts)) ? ts.Duration : 0;
        if (lastSrt100ns) g_wgcCarimboCruMs.store((double)lastSrt100ns / 10000.0, std::memory_order_relaxed);   // RODADA5: antes do V2
        // V2: o carimbo regularizado (ou o bruto, com o V2 desligado). No modo k o quadro fora
        // da janela ja foi devolvido antes de chegar aqui (ver Tick).
        if (optV2 > 0 && lastSrt100ns) { const LONGLONG c = V2Carimbo(lastSrt100ns, V2Vblank100ns()); if (c > 0) lastSrt100ns = c; }
        // X-GERADO: o carimbo do quadro vai para o apresentador (ritmo da chegada). 100ns -> ms.
        if (lastSrt100ns) g_wgcCarimboMs.store((double)lastSrt100ns / 10000.0, std::memory_order_relaxed);

        WGC::WG::SizeInt32 cs{}; f->get_ContentSize(&cs);
        WGC::WGD3::IDirect3DSurface* surf = nullptr;
        ID3D11Texture2D* src = nullptr;
        if (SUCCEEDED(f->get_Surface(&surf)) && surf) {
            Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess* acc = nullptr;
            if (SUCCEEDED(surf->QueryInterface(
                    __uuidof(Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess), (void**)&acc)) && acc) {
                acc->GetInterface(__uuidof(ID3D11Texture2D), (void**)&src);
                acc->Release();
            }
            surf->Release();
        }
        if (!src) return false;

        RECT want{}, org{};
        bool copied = TargetRectCached(&want, &org) &&
                      RecortarDe(src, org.left, org.top, cs.Width, cs.Height);
        src->Release();

        // alvo mudou de tamanho -> o pool precisa nascer de novo no tamanho novo
        if (cs.Width > 0 && cs.Height > 0 && ((UINT)cs.Width != poolW || (UINT)cs.Height != poolH))
            RecreatePool((UINT)cs.Width, (UINT)cs.Height);
        return copied;
    }

    // NATIVO + PELICULA (20/09/2026): no Nativo a IMAGEM vem da ponte, mas a sessao da captura
    // fica de pe assim mesmo - e dela que saem o Alive() e o TargetRectCached() que colam a
    // pelicula na janela do jogo. Como ninguem vai ler o quadro, ele e devolvido SEM COPIA: o
    // dreno so tira os frames da fila do WGC para ela nao segurar buffer. Uma volta do laco,
    // nenhum trabalho de GPU nosso (o mesmo que o modo de diagnostico optNoCopy ja fazia).
    void Drenar() {
        DegrauSoltarTudo(); if (Degrau::g_cap == this) Degrau::g_cap = nullptr;   // DEGRAU: nada segurado no Nativo
        if (!pool || !started) return;
        for (;;) {
            WGC::WGCa::IDirect3D11CaptureFrame* f = nullptr;
            if (FAILED(pool->TryGetNextFrame(&f)) || !f) break;
            CloseFrame(f);
        }
    }

    // Desmonta SO a parte WinRT (sessao, pool, item, device WinRT e handlers) e deixa de
    // pe o que a cadeia enxerga: `tex`, `srv`, `ictx` e `evt`. E o que permite trocar de
    // backend em pleno voo sem um unico quadro preto.
    //
    // A ORDEM IMPORTA: os handlers sao DESARMADOS ANTES do Close. Fechando primeiro, o
    // Closed do item dispararia, marcaria `closed` e o laco do main.cpp veria a captura
    // como morta - e tentaria recriar a sessao que nos acabamos de aposentar de proposito.
    void PararWgcMantendoSaida() {
        if (handler)       handler->Desarmar();
        if (closedHandler) closedHandler->Desarmar();
        if (item && closedHandler) item->remove_Closed(closedToken);
        if (session) CloseRelease(session);
        if (pool) {
            if (handler) pool->remove_FrameArrived(token);
            CloseRelease(pool);
        }
        if (handler)       { handler->Release();       handler = nullptr; }
        if (closedHandler) { closedHandler->Release(); closedHandler = nullptr; }
        // RITMO2: o WGC sai de cena (a duplicacao assumiu) - o que sobrou no slot volta.
        if (soltaCsOk) {
            EnterCriticalSection(&soltaCs);
            if (soltaNovo) { CloseFrame(soltaNovo); soltaNovo = nullptr; }
            soltaDesdeTick = 0;
            LeaveCriticalSection(&soltaCs);
        }
        if (ordSegura) { CloseFrame(ordSegura); ordSegura = nullptr; } DegrauSoltarTudo(); if (Degrau::g_cap == this) Degrau::g_cap = nullptr;   // RODADA8
        token = EventRegistrationToken{};
        closedToken = EventRegistrationToken{};
        if (item)  { item->Release();  item = nullptr; }
        if (rtDev) { rtDev->Release(); rtDev = nullptr; }
        closed.store(false, std::memory_order_relaxed);
    }

    // A PROVA ANTES DA TROCA (O3, 20/09/2026 - medido na bancada).
    //
    // `DuplicateOutput` pode ter SUCESSO e mesmo assim `AcquireNextFrame` devolver
    // WAIT_TIMEOUT para sempre: foi o que esta maquina fez (NVIDIA 4080 + Radeon iGPU,
    // monitor listado sob a NVIDIA). Conferido fora do app, com um programa de 60 linhas
    // que so duplica e conta - zero quadros em 5 s, com uma janela sendo arrastada pela
    // tela. Ou seja: nao da para acreditar no `hr` da montagem.
    //
    // Entao a troca nao aposenta o WGC de cara. A duplicacao sobe AO LADO dele e fica em
    // prova; o WGC continua servindo. So quando o PRIMEIRO quadro da duplicacao chega e
    // que o WGC e aposentado. Se em 1,5 s nao chegou nada, a duplicacao e desmontada e
    // marcada como recusada - e a pessoa nunca ve a imagem congelar.
    bool      duplEmProva = false;
    ULONGLONG duplProvaAte = 0;
    ULONGLONG duplProvaInicio = 0;

    // RITMO SUSTENTADO, E NAO "chegou quadro". Medido em 20/09/2026: nesta maquina a
    // duplicacao entrega uma rajada curta no arranque e para - com prova de 1 quadro ela
    // passava, e com 5 tambem; a fonte entao ficava ~1 s parada ate o vigia desfazer. A
    // prova agora pede 20 quadros AO LONGO DE pelo menos 1 s, e como o WGC continua
    // servindo durante ela, prova longa nao custa nada a quem esta assistindo.
    enum { kProvaQuadros = 20, kProvaMinMs = 1000, kSemQuadroMs = 400 };

    void ConferirProvaDaDuplicacao() {
        if (!duplEmProva) return;
        const ULONGLONG agora = GetTickCount64();
        if (dupl.seq.load(std::memory_order_acquire) >= (UINT32)kProvaQuadros &&
            agora - duplProvaInicio >= (ULONGLONG)kProvaMinMs) {
            duplEmProva = false;
            PararWgcMantendoSaida();
            backend = BK_DXGI;
            duplUltQuadroMs = agora;
            Log("captura: WGC -> DXGI (%s) - %d quadros da duplicacao em %llu ms",
                duplMotivo.c_str(), (int)kProvaQuadros, (unsigned long long)(agora - duplProvaInicio));
            return;
        }
        if (agora < duplProvaAte) return;
        duplEmProva = false;
        const UINT32 veio = dupl.seq.load(std::memory_order_acquire);
        dupl.Parar();
        duplRecusada = true;
        duplPorque = L"a duplicacao subiu mas nao entregou quadro em ritmo";
        Log("captura: a duplicacao entregou so %u quadro(s) em %llu ms - desmontada, o WGC continua "
            "servindo. (Acontece com a pelicula cobrindo o monitor: ela e topmost e esta fora da "
            "captura, entao a composicao do desktop para de mudar e a duplicacao nao tem o que "
            "entregar. Tambem acontece em maquina hibrida, com o output listado numa placa e "
            "servido por outra.)", (unsigned)veio, (unsigned long long)(agora - duplProvaInicio));
    }

    // O VIGIA, enquanto a duplicacao serve. Ela pode parar de entregar DEPOIS de ter
    // provado - e nesta bancada foi o que aconteceu TODAS as vezes: a duplicacao entrega
    // em ritmo enquanto a sessao do WGC existe e SECA em menos de 1 s depois de ela ser
    // aposentada (medido 3x em 20/09/2026; com a pelicula topmost cobrindo o monitor e o
    // WGC fora do caminho, a composicao do desktop para e nao ha o que duplicar).
    // 400 ms = 12 quadros de uma fonte a 30 fps: nao dispara por atraso, so por parada.
    // O preco de uma tentativa que nao vinga e ~0,6 s de fonte parada, UMA VEZ por sessao
    // (`duplRecusada` nao deixa tentar de novo).
    ULONGLONG duplUltQuadroMs = 0;
    void ConferirSilencioDaDuplicacao() {
        if (backend != BK_DXGI || duplEmProva) return;
        const ULONGLONG agora = GetTickCount64();
        if (!duplUltQuadroMs) { duplUltQuadroMs = agora; return; }
        if (agora - duplUltQuadroMs < (ULONGLONG)kSemQuadroMs) return;
        duplRecusada = true;
        duplPorque = L"a duplicacao parou de entregar";
        pedeVoltarWgc = true;
        Log("captura: a duplicacao ficou %llu ms sem entregar quadro - voltando para o WGC",
            (unsigned long long)(agora - duplUltQuadroMs));
        duplUltQuadroMs = agora;
    }

    std::string duplMotivo;   // por que a troca foi pedida (entra no log quando ela vinga)

    // Troca WGC -> duplicacao sem parar a cadeia. `porque` entra no log: uma troca de
    // backend silenciosa seria a pior coisa possivel para quem estiver medindo.
    bool TrocarParaDuplicacao(const char* porque) {
        if (backend == BK_DXGI || duplEmProva) return true;
        if (!dev || !evt) return false;
        // OCLUSAO (23/09/2026): com o monitor servido por uma janela a duplicacao esta fora
        // de questao - ela duplica o MONITOR, e o monitor e exatamente a coisa que parou de
        // ser composta (e o motivo de termos trocado para a janela). Deixar a troca
        // acontecer aqui seria desfazer o conserto em silencio, com a imagem congelando de
        // novo alguns segundos depois de ela "provar" no arranque.
        if (monSubstituido) {
            duplRecusada = true;
            duplPorque = L"o monitor esta sendo servido por uma janela (a duplicacao nao teria o que duplicar)";
            Log("captura: duplicacao fora do caminho - o monitor esta sendo servido pela janela sob a pelicula");
            return false;
        }
        HMONITOR m = MonitorDaFonte();
        std::wstring pq;
        const bool hdr = MonitorEmHdr(m);
        if (!CapturaDxgi::Disponivel(dev, m, hdr, &pq)) {
            duplRecusada = true; duplPorque = pq;
            Log("captura: duplicacao indisponivel (%s) - seguindo no WGC", U8Dbg(pq).c_str());
            return false;
        }
        {   RECT want{}, org{};   // o recorte ja na largada: a prova de ritmo depende do dedup certo
            if (TargetRectCached(&want, &org)) dupl.DefinirRecorte(want); }
        std::wstring err;
        if (!dupl.Iniciar(dev, m, evt, &err)) {
            duplRecusada = true; duplPorque = err;
            Log("captura: duplicacao nao subiu (%s) - seguindo no WGC", U8Dbg(err).c_str());
            return false;
        }
        // NAO aposenta o WGC agora: ele continua servindo ate a duplicacao PROVAR que
        // entrega (ver ConferirProvaDaDuplicacao, logo acima).
        duplMotivo = porque ? porque : "pedido";
        duplEmProva = true;
        duplProvaInicio = GetTickCount64();
        duplProvaAte = duplProvaInicio + 2500;
        Log("captura: duplicacao no ar em prova (%s) - o WGC continua servindo ate o primeiro quadro dela chegar",
            duplMotivo.c_str());
        return true;
    }

    // O apresentador e quem mede o tremor da CHEGADA (`j`) e o periodo da tela; o main.cpp
    // repassa os dois aqui uma vez por segundo. Esta funcao e a unica dona da decisao.
    // `jMs <= 0` quer dizer "o apresentador nao tem numero para dar" - ai vale o tremor que
    // a propria captura mede. `telaHz <= 1` idem: o periodo sai do DWM (tela_vrr.h).
    void RelatarRitmo(double jMs, double telaHz) {
        if (!(jMs > 0.0)) jMs = TremorMs();
        obsJMs = jMs; obsTelaHz = telaHz;

        // A FONTE PODE TER MUDADO DE MONITOR (ou o monitor pode ter entrado em HDR) COM A
        // DUPLICACAO NO AR. A duplicacao e de UM output: sem esta conferencia ela seguiria
        // entregando o monitor antigo, e a pelicula mostraria a area de trabalho do outro
        // lado da mesa. Conferido aqui porque isto ja roda uma vez por segundo.
        if (backend == BK_DXGI && dupl.Vivo()) {
            HMONITOR m = MonitorDaFonte();
            if (m && MonitorEmHdr(m)) {
                Log("captura: o monitor entrou em HDR - a duplicacao nao serve aqui, voltando para o WGC");
                pedeVoltarWgc = true;
            } else if (m && m != dupl.mon) {
                std::wstring e;
                if (dupl.Iniciar(dev, m, evt, &e)) {
                    Log("captura DXGI: a fonte mudou de monitor - duplicacao refeita no monitor novo");
                } else {
                    Log("captura: a fonte mudou de monitor e a duplicacao nao subiu la (%s) - voltando para o WGC",
                        U8Dbg(e).c_str());
                    pedeVoltarWgc = true;
                }
            }
        }
        if (!started || backend != BK_WGC) return;
        if (modo == MODO_WGC) return;
        const ULONGLONG agora = GetTickCount64();
        if (!obsDesde) obsDesde = agora;
        if (modo == MODO_DXGI) { if (!duplRecusada) TrocarParaDuplicacao("escolha no painel"); return; }
        // O AUTOMATICO NAO TROCA MAIS (21/09/2026, medido). A duplicacao entrega COMPOSICOES
        // do DWM, nao presents da fonte: com o alvo a 30 fps o carimbo dela treme tanto
        // quanto o do WGC (chegada p95-p50 = 12,1 ms pela duplicacao, 12,5 ms pelo WGC;
        // `j` 11,7 contra 12,5). Nao ha o que ganhar trocando sozinho, e ha o que perder
        // (monitor inteiro, a nossa saida fora da captura, uma prova de 1 s). Fica so o
        // registro, uma vez por sessao: a pessoa que quiser a duplicacao escolhe no painel.
        if (duplRecusada) return;
        if (agora - obsDesde < 10000) return;      // 10 s observando antes de falar
        if (!(jMs > 0.0)) return;                  // ninguem sabe o tremor ainda
        double periodo = (telaHz > 1.0) ? 1000.0 / telaHz : TelaVrr::PeriodoMs();
        if (!(periodo > 1.0)) periodo = 16.67;
        if (!(jMs > periodo)) return;              // cabe no periodo: nada a dizer
        duplRecusada = true;                       // (reusa a marca: "nao volta a falar")
        Log("captura: tremor da chegada j %.1f ms > 1 periodo da tela (%.1f ms) - o automatico NAO troca para a "
            "duplicacao (ela entrega composicoes do DWM, com o mesmo tremor; medido em 21/09/2026). "
            "Quem quiser experimenta-la escolhe DXGI no painel.", jMs, periodo);
    }

    // O PONTEIRO POR CIMA DO RECORTE, chamado logo depois de cada copia. O recorte e o
    // mesmo `want` que a captura ja usa, entao a seta cai no pixel certo tanto na pelicula
    // que cobre o monitor quanto na camada colada numa janelinha.
    void DesenharPonteiro() {
        if (!tex || !w || !h || !dev || !ictx) return;
        if (Ponteiro::g_modo == Ponteiro::MODO_ESCONDER) return;
        RECT want{}, org{};
        if (!TargetRectCached(&want, &org)) return;
        Ponteiro::DesenharNaCaptura(dev, ictx, tex, w, h, want.left, want.top, hdrFp16, hdrSdrWhiteNits);
    }

    // Roda na thread do render, lado da duplicacao: pega o quadro publicado pela thread
    // dela (se houver um novo) e recorta para `tex`. A origem aqui e o CANTO DO MONITOR,
    // porque a duplicacao entrega o output inteiro.
    bool TickDuplicacao() {
        if (!ictx || !dupl.Vivo()) return false;
        const LONGLONG t2 = g_lat.Now();
        // O RECORTE VAI PARA A THREAD DA DUPLICACAO a cada volta (quatro atomicos, de graca):
        // e sobre ele que o dedup por conteudo decide se a composicao sobe ou nao.
        {   RECT want{}, org{};
            if (TargetRectCached(&want, &org)) dupl.DefinirRecorte(want); }
        ID3D11Texture2D* src = dupl.Travar();
        if (!src) return false;
        const bool copied = RecortarDe(src, dupl.monRect.left, dupl.monRect.top,
                                       (INT32)dupl.texW, (INT32)dupl.texH);
        dupl.Destravar();
        if (!copied) return false;
        DesenharPonteiro();   // a duplicacao entrega o desktop SEM cursor: ele e sempre nosso

        // CARIMBO = LastPresentTime, nao "a hora em que eu acordei". E o instante do present
        // que gerou a imagem, convertido para a mesma unidade do SystemRelativeTime do WGC,
        // para o apresentador ler os dois do mesmo jeito.
        duplUltQuadroMs = GetTickCount64();
        lastSrt100ns = dupl.Carimbo100ns();
        if (lastSrt100ns) g_wgcCarimboMs.store((double)lastSrt100ns / 10000.0, std::memory_order_relaxed);
        if (lastSrt100ns) g_wgcCarimboCruMs.store((double)lastSrt100ns / 10000.0, std::memory_order_relaxed);   // RODADA5
        frames++;
        TremorAmostra(lastSrt100ns);
        g_lat.nTick++;
        g_lat.curSrt = lastSrt100ns;
        const LONGLONG t1 = dupl.publicadoQpc.load(std::memory_order_relaxed);
        if (t1 && lastSrt100ns) g_lat.srt.add((double)(g_lat.QpcTo100ns(t1) - lastSrt100ns) / 10000.0);
        if (t1) g_lat.wake.add(g_lat.Ms(t2 - t1));
        g_lat.tick.add(g_lat.Ms(g_lat.Now() - t2));
        if (!g_lat.fedByPresent && ++g_lat.n >= (UINT32)g_lat.every) { g_lat.Dump(); g_lat.Reset(); }
        return true;
    }

    // RITMO2 (22/09/2026): roda na THREAD DO FrameArrived, nunca na do render. Tira do pool
    // tudo o que estiver la, devolve os velhos na hora e guarda so o mais novo no slot.
    // NAO toca em device, contexto, textura nem em nada que o laco esteja usando: o pool e
    // free-threaded e os contadores ficam sob o mesmo cadeado que o relatorio de 5 s toma.
    // O `started` NAO entra na condicao de proposito. Ele so vira true depois do
    // StartCapture, e um quadro pode chegar nesse intervalo: com o teste aqui, esses
    // primeiros quadros ficariam na fila do pool sem ninguem para tira-los (o Tick com a
    // chegada solta le o slot, nunca o pool) e, se enchessem os 3 buffers, o WGC pararia de
    // produzir e a captura nascia morta. Quem autoriza a drenagem e o `dono` do handler,
    // que so e posto no Start e e retirado no Desarmar.
    void DrenarSlot() {
        if (!optSolta || !pool || !soltaCsOk) return;
        EnterCriticalSection(&soltaCs);
        for (;;) {
            WGC::WGCa::IDirect3D11CaptureFrame* f = nullptr;
            if (FAILED(pool->TryGetNextFrame(&f)) || !f) break;
            { WGC::WF::TimeSpan ts{}; if (SUCCEEDED(f->get_SystemRelativeTime(&ts))) V1Carimbo(ts.Duration); }
            v1Pool++; soltaDesdeTick++;
            // Ja havia um no slot? Entao a cadeia nao alcancou este: ele volta ao pool sem
            // nunca ter virado imagem, e e ISSO que o contador do log de 5 s conta.
            if (soltaNovo) { CloseFrame(soltaNovo); soltaAtropelado++; g_capPerdidosTot.fetch_add(1, std::memory_order_relaxed); }
            soltaNovo = f;
        }
        LeaveCriticalSection(&soltaCs);
    }

    // Roda na thread do render: pega o frame mais novo (se houver) e recorta pra tex.
    // Devolve true quando copiou algo novo. Atualiza w/h quando o alvo muda de tamanho.
    bool Tick() {
        ConferirProvaDaDuplicacao();     // barato: so faz alguma coisa com uma troca em curso
        ConferirSilencioDaDuplicacao();  // idem: so olha o relogio quando a duplicacao serve
        // DEGRAU (sec. 21): so a FILA do WGC segura buffers. Qualquer outro caminho (ou o degrau desligado) devolve tudo.
        {
            const bool filaWgc = backend != BK_DXGI && !optNoCopy && !optSolta && optDrain && optOrdem && optV2 < 2;
            if (!filaWgc || Degrau::g_N < 2) {
                DegrauSoltarTudo();
                if (!filaWgc && Degrau::g_cap == this) Degrau::g_cap = nullptr;
            }
            if (filaWgc && pool && started) {
                Degrau::g_cap = this; Degrau::g_capTickMs = Degrau::Agora();
                const double vb = V2Vblank100ns(); Degrau::g_capHz = vb > 0.0 ? 1.0e7 / vb : 0.0;
                // vigia: ninguem devolveu nada por um passo + 100 ms (o laco nao passou pela esteira) -> devolve um
                if (DegrauCheio() && Degrau::g_P > 0.0 && Degrau::Agora() - dgUltLibMs > (double)Degrau::g_N * Degrau::g_P + 100.0) {
                    DegrauLiberarUm(); Degrau::g_libAberta = false;
                }
            }
        }
        if (backend == BK_DXGI) return TickDuplicacao();
        if (!pool || !started || !ictx) return false;
        LONGLONG t2 = g_lat.Now();
        bool copied = false;
        v1Ticks++; V1Relatar();
        UINT32 v1Nesta = 0;

        if (optNoCopy) {   // diagnostico: mede o ritmo do WGC sem nenhum trabalho nosso
            for (;;) {
                WGC::WGCa::IDirect3D11CaptureFrame* f = nullptr;
                if (FAILED(pool->TryGetNextFrame(&f)) || !f) break;
                CloseFrame(f);
            }
            return false;
        }

        if (optSolta) {
            // RITMO2: o pool ja foi drenado pela thread da chegada. Aqui so se pega o que
            // ela guardou. O resto do caminho (V2 modo k, CopyFrame, CloseFrame, contadores)
            // e byte por byte o mesmo de antes - de proposito: o que muda e QUEM devolve os
            // buffers ao WGC, nao o que a cadeia ve.
            WGC::WGCa::IDirect3D11CaptureFrame* keep = nullptr;
            if (soltaCsOk) {
                EnterCriticalSection(&soltaCs);
                keep = soltaNovo; soltaNovo = nullptr;
                v1Nesta = soltaDesdeTick; soltaDesdeTick = 0;
                if (keep) soltaEntregue++; else soltaRepetiu++;
                LeaveCriticalSection(&soltaCs);
            }
            if (keep && optV2 >= 2) {   // V2 modo k: um quadro por janela de k vblanks
                WGC::WF::TimeSpan ts{}; const double vb = V2Vblank100ns();
                if (SUCCEEDED(keep->get_SystemRelativeTime(&ts)) && vb > 0.0) {
                    const LONGLONG slot = (LONGLONG)((double)ts.Duration / (vb * optV2));
                    if (slot == v2UltSlot) { v2Descartados++; CloseFrame(keep); keep = nullptr; }
                    else v2UltSlot = slot;
                }
            }
            if (keep) { copied = CopyFrame(keep); CloseFrame(keep); if (copied) v1Cop++; }
        } else if (optDrain && optOrdem) {
            // RODADA8 (PLANO-FLUIDEZ, Agente C, item 4): FILA EM ORDEM, sem descarte. O pool do WGC e FIFO e tem optPoolBuf
            // buffers (padrao 2) = o TETO de pendentes. Cada volta tira SO o mais velho, copia e devolve; o que sobrou fica no
            // pool para a volta seguinte (o evento e re-sinalizado para o laco nao dormir com quadro esperando). Com o pool no
            // teto o DWM nao tem buffer livre e nao entrega mais nada - e o PRODUTOR que espera (como no anel da ponte), nunca
            // nos descartando um quadro que ja chegou. O custo disso aparece como LATENCIA DE FILA (idade do quadro ao ser
            // copiado) crescendo e como EXCEDENTES estimados (a fonte passou da capacidade da rede) - linha `captura FILA`.
            const UINT32 arrAgora = arrived.load(std::memory_order_relaxed);
            // pendentes = no pool (FrameArrived - tirados) + o que ja esta na mao (contagem do WGC; diagnostico e teto)
            const UINT32 pendAntes = (UINT32)((int)(arrAgora - ordDeq) < 0 ? 0 : (arrAgora - ordDeq)) + (ordSegura ? 1u : 0u);
            WGC::WGCa::IDirect3D11CaptureFrame* f = ordSegura;   // o que a volta anterior ja tirou do pool (o mais velho)
            ordSegura = nullptr;
            if (!f) { if (SUCCEEDED(pool->TryGetNextFrame(&f)) && f) ordDeq++; else f = nullptr; }
            if (f) {
                LONGLONG srt = 0;
                { WGC::WF::TimeSpan ts{}; if (SUCCEEDED(f->get_SystemRelativeTime(&ts))) { srt = ts.Duration; V1Carimbo(srt); } }
                if (srt) Degrau::Chegou((double)srt / 10000.0);   // DEGRAU: indice na grade + calibracao do lead
                v1Pool++; v1Nesta = 1;
                if (pendAntes > ordPendMax) ordPendMax = pendAntes;
                const bool noTeto = pendAntes >= (UINT32)optPoolBuf;
                if (noTeto) ordTeto++;
                // idade do quadro na fila: agora - carimbo (o carimbo do WGC e a hora de exibicao da composicao)
                if (srt) {
                    const double idade = (double)(g_lat.QpcTo100ns(g_lat.Now()) - srt) / 10000.0;
                    if (ordIdadeN < 1024) ordIdade[ordIdadeN++] = idade;
                    // EXCEDENTES (estimativa): com a fila no teto, um intervalo de carimbo maior que o intervalo tipico da fonte
                    // FORA do teto quer dizer composicoes que o DWM nao entregou por falta de buffer
                    if (ordUltSrt) {
                        const double dt = (double)(srt - ordUltSrt) / 10000.0;
                        if (dt > 0.0 && dt < 250.0) {
                            if (!noTeto) ordIntLivre = ordIntLivre > 0.0 ? 0.9 * ordIntLivre + 0.1 * dt : dt;
                            else if (ordIntLivre > 0.0) { const double k = floor(dt / ordIntLivre + 0.5) - 1.0; if (k > 0.0) { ordExced += k; ordExcedTot += k; } }
                        }
                    }
                    ordUltSrt = srt;
                }
                if (optV2 >= 2) {   // V2 modo k (so bancada, explicito): um quadro por janela de k vblanks
                    WGC::WF::TimeSpan ts{}; const double vb = V2Vblank100ns();
                    if (SUCCEEDED(f->get_SystemRelativeTime(&ts)) && vb > 0.0) {
                        const LONGLONG slot = (LONGLONG)((double)ts.Duration / (vb * optV2));
                        if (slot == v2UltSlot) { v2Descartados++; CloseFrame(f); f = nullptr; }
                        else v2UltSlot = slot;
                    }
                }
                if (f) {
                    copied = CopyFrame(f);
                    // DEGRAU: o quadro ja copiado fica conosco (o buffer so volta ao DWM quando a esteira devolver)
                    if (Degrau::g_N >= 2 && Degrau::g_cap == this) DegrauSegurar(f); else CloseFrame(f);
                    if (copied) { v1Cop++; ordEntregues++; }
                }
                soltaEntregue++;
                // ainda ha quadro no pool? Ele e tirado AGORA (continua o mais velho, e continua ocupando um buffer - o teto
                // nao muda) e fica na mao para a volta seguinte, que e acordada ja: sem isto, com o pool cheio o FrameArrived
                // nao dispara de novo e o quadro esperaria o timeout do laco.
                WGC::WGCa::IDirect3D11CaptureFrame* g = nullptr;
                if (SUCCEEDED(pool->TryGetNextFrame(&g)) && g) { ordDeq++; ordSegura = g; if (evt) SetEvent(evt); }
            } else soltaRepetiu++;
        } else if (optDrain) {
            // Drena a fila mas so COPIA o frame mais novo. Como o Present e 1 por Tick,
            // copiar os antigos e banda de GPU jogada fora e ainda atrasa o Present -
            // e o unico que aparece na tela e o ultimo mesmo.
            WGC::WGCa::IDirect3D11CaptureFrame* keep = nullptr;
            for (;;) {
                WGC::WGCa::IDirect3D11CaptureFrame* f = nullptr;
                if (FAILED(pool->TryGetNextFrame(&f)) || !f) break;
                { WGC::WF::TimeSpan ts{}; if (SUCCEEDED(f->get_SystemRelativeTime(&ts))) V1Carimbo(ts.Duration); }
                v1Pool++; v1Nesta++;
                // RITMO2: este quadro chegou e vai voltar ao pool sem virar imagem - a
                // cadeia nao o alcancou. O contador vale NOS DOIS desenhos (com e sem a
                // chegada solta), porque e ele que o dono compara antes/depois.
                if (keep) { CloseFrame(keep); soltaAtropelado++; g_capPerdidosTot.fetch_add(1, std::memory_order_relaxed); }   // devolve o antigo sem copiar (RODADA5: perdidos_na_captura)
                keep = f;
            }
            if (keep) soltaEntregue++; else soltaRepetiu++;
            if (keep && optV2 >= 2) {   // V2 modo k: um quadro por janela de k vblanks; fora dela, devolve sem copiar
                WGC::WF::TimeSpan ts{}; const double vb = V2Vblank100ns();
                if (SUCCEEDED(keep->get_SystemRelativeTime(&ts)) && vb > 0.0) {
                    const LONGLONG slot = (LONGLONG)((double)ts.Duration / (vb * optV2));
                    if (slot == v2UltSlot) { v2Descartados++; CloseFrame(keep); keep = nullptr; }
                    else v2UltSlot = slot;
                }
            }
            if (keep) { copied = CopyFrame(keep); CloseFrame(keep); if (copied) v1Cop++; }
        } else {
            for (;;) {
                WGC::WGCa::IDirect3D11CaptureFrame* f = nullptr;
                if (FAILED(pool->TryGetNextFrame(&f)) || !f) break;
                { WGC::WF::TimeSpan ts{}; if (SUCCEEDED(f->get_SystemRelativeTime(&ts))) V1Carimbo(ts.Duration); }
                v1Pool++; v1Nesta++;
                if (CopyFrame(f)) { copied = true; v1Cop++; }
                CloseFrame(f);
            }
        }
        v1PorTick[v1Nesta > 3 ? 3 : v1Nesta]++;

        if (copied) {
            DesenharPonteiro();   // por cima do recorte, com a posicao de AGORA
            frames++;
            TremorAmostra(lastSrt100ns);
            g_lat.nTick++;
            LONGLONG t1 = arriveQpc.load(std::memory_order_relaxed);
            g_lat.curSrt = lastSrt100ns;
            if (t1 && lastSrt100ns) g_lat.srt.add((double)(g_lat.QpcTo100ns(t1) - lastSrt100ns) / 10000.0);
            if (t1) g_lat.wake.add(g_lat.Ms(t2 - t1));
            g_lat.tick.add(g_lat.Ms(g_lat.Now() - t2));
            // se o loop principal nao estiver alimentando o probe pelo Present,
            // o relatorio sai por aqui mesmo (so sem os tempos de apresentacao)
            if (!g_lat.fedByPresent && ++g_lat.n >= (UINT32)g_lat.every) { g_lat.Dump(); g_lat.Reset(); }
        }
        return copied;
    }

    bool Alive() const {
        // Na duplicacao quem responde e a thread dela: o `closed` do WGC nao vale mais (a
        // sessao foi aposentada de proposito) e a janela alvo continua sendo o outro criterio.
        if (backend == BK_DXGI) return dupl.Vivo() && (!targetWin || IsWindow(targetWin));
        return !closed.load(std::memory_order_relaxed) && (!targetWin || IsWindow(targetWin));
    }
    // true = foi o WGC que encerrou a sessao (o item avisou pelo Closed), nao a janela
    // que morreu. Os dois derrubam a captura, mas so o primeiro tem conserto: a condicao
    // de recriacao certa e SessionClosed() && IsWindow(alvo).
    bool SessaoCaiu() const { return closed.load(std::memory_order_relaxed); }
    bool SessionClosed() const { return SessaoCaiu(); }   // mesmo predicado, nome em ingles
    // Só para teste (DLSS5_TESTA_QUEDA): finge que o WGC encerrou a sessao com a janela
    // viva. E o unico jeito de exercitar o caminho de recriacao sem um jogo de verdade
    // trocando de resolucao no meio da medicao.
    void FingirQueda() { closed.store(true, std::memory_order_relaxed); }

    // Desmonta tudo o que o Start montou, em ordem: primeiro cala a fonte de eventos
    // (Close da sessao e do pool + remove dos handlers), depois desarma os handlers e
    // espera quem esta dentro do Invoke sair, e so entao solta os objetos e o evento.
    //
    // manterEvt=true e o modo usado quando o Start falha no meio: o main.cpp espera
    // nesse HANDLE e, com ele fechado, MsgWaitForMultipleObjects devolve WAIT_FAILED
    // na hora - o loop principal vira busy-wait de 100% de um nucleo.
    void Teardown(bool manterEvt) {
        started = false;
        // A thread da duplicacao usa o `evt` e a textura compartilhada: ela para PRIMEIRO,
        // antes de qualquer coisa daqui ser solta.
        dupl.Parar();
        backend = BK_WGC;
        duplEmProva = false;
        // RITMO2 (22/09/2026): DESARMAR ANTES DE SOLTAR O POOL, e nao depois. Ate hoje o
        // Teardown fechava o pool e SO ENTAO desarmava o handler - dava certo porque o
        // Invoke antigo nao tocava no pool, so carimbava a hora e dava SetEvent. Agora ele
        // DRENA o pool: um Invoke em voo depois do CloseRelease(pool) chamaria
        // TryGetNextFrame num objeto ja solto. O `Desarmar()` zera o `dono` e espera o voo
        // acabar, que e exatamente a garantia que falta aqui. O PararWgcMantendoSaida (a
        // troca para a duplicacao) sempre fez nesta ordem; agora os dois combinam.
        if (handler)       handler->Desarmar();
        if (closedHandler) closedHandler->Desarmar();
        if (session) { CloseRelease(session); }
        if (pool) {
            if (handler) pool->remove_FrameArrived(token);
            CloseRelease(pool);
        }
        if (item && closedHandler) item->remove_Closed(closedToken);
        // O quadro que tiver sobrado no slot volta ao WGC agora: com o pool ja fechado ele
        // e so um Release, mas deixa-lo pendurado vazaria a textura da sessao antiga a cada
        // Stop()+Start() da recriacao.
        if (soltaCsOk) {
            EnterCriticalSection(&soltaCs);
            if (soltaNovo) { CloseFrame(soltaNovo); soltaNovo = nullptr; }
            soltaDesdeTick = 0;
            LeaveCriticalSection(&soltaCs);
        }
        if (ordSegura) { CloseFrame(ordSegura); ordSegura = nullptr; } DegrauSoltarTudo(); if (Degrau::g_cap == this) Degrau::g_cap = nullptr;   // RODADA8
        if (handler)       { handler->Release();       handler = nullptr; }
        if (closedHandler) { closedHandler->Release(); closedHandler = nullptr; }
        token = EventRegistrationToken{};
        closedToken = EventRegistrationToken{};
        if (item)  { item->Release();  item = nullptr; }
        if (rtDev) { rtDev->Release(); rtDev = nullptr; }
        if (ictx)  { ictx->Release();  ictx = nullptr; }
        if (srv)   { srv->Release();   srv = nullptr; }
        if (tex)   { tex->Release();   tex = nullptr; }
        texFmt = DXGI_FORMAT_UNKNOWN;
        w = h = 0; poolW = poolH = 0;
        cacheOk = false;
        if (!manterEvt && evt) { CloseHandle(evt); evt = nullptr; }
    }

    void Stop() { Teardown(false); }
};

// RITMO2: a ponte entre o handler (declarado antes da struct, porque e ele que o pool
// recebe) e a captura. Uma linha, para nao duplicar o desenho da drenagem em dois lugares.
static void WgcDrenarParaSlot(ScreenCapture* s) { if (s) s->DrenarSlot(); }
