// =====================================================================================
//  ESTEIRA (23/09/2026, OCLUSAO-2309 sec. 18): o caminho de UM quadro no modo monitor,
//  carimbado etapa por etapa, e a linha `esteira 1s` com p50/p95 de cada etapa.
//
//  POR QUE EXISTE. Com o neural ligado a entrada sustentava ~143 quadros/s (1 passe) com
//  um pacote de GPU de ~5,7 ms: sobrava ~1,3-3 ms por quadro que NAO se sobrepunha a rede,
//  e nenhuma linha do log dizia onde. As linhas que ja existiam medem pedacos soltos (LAT
//  a cada 300 quadros, `apresentador MEDIDA` a cada 60, o feed a cada 600), cada uma com a
//  sua janela, e nenhuma olha a GPU do lado do app.
//
//  O QUE MEDE (tudo no mesmo quadro, pelo mesmo id):
//    CPU (thread do laco, QPC):
//      carimbo  - SystemRelativeTime do quadro (o DWM compos a fonte)       [origem]
//      slot0/1  - a espera no objeto do swapchain oculto (contrapressao)
//      tick     - captura: drenar a fila + copiar para a textura do laco
//      dedup    - comparacao exata com o ultimo quadro que foi a rede
//      desenho  - o resto da volta + o nosso desenho no backbuffer (ate o Present)
//      present  - o Present do swapchain oculto: ReShade + feed (rede enfileirada) + publicacao
//      ler      - AposPresent: copia do anel publicado pelo add-on
//      enviar   - AposPresent: copia para o anel da ponte + fence (+ espera de vaga)
//    GPU (queries TIMESTAMP no contexto da rede, lidas sem esperar 1-16 quadros depois):
//      A = inicio do quadro (antes da copia da captura)
//      B = antes do Present (nosso desenho pronto na fila)
//      C = depois do Present (a fila D3D11 so passa daqui quando a rede terminou: Wait(fence11))
//      D = depois do AposPresent (copias da ponte)
//      pre = B-A, rede = C-B (rede + espera entre filas + publicacao), pos = D-C,
//      vao = A(n+1)-D(n) (a placa SEM trabalho nosso esperando a CPU), quadro = A(n+1)-A(n)
//    Apresentador (thread dele): enviado -> recebido -> Present, e carimbo -> Present.
//    ORDEM: o carimbo de cada quadro que vai a TELA tem de ser maior que o do anterior; o
//    mesmo para o que vai a REDE. Qualquer inversao conta (e as primeiras sao logadas).
//
//  "SERIAL" = pre + pos + vao: o que, na GPU, nao se sobrepoe a rede deste quadro. Com um
//  quadro em voo (latencia 1) o vao e a CPU da volta seguinte (slot liberado -> Present).
//
//  CUSTO: ~5 queries e ~12 leituras de QPC por quadro; a linha sai 1x/s. `[ultima]
//  esteira_medir=0` / DLSS5_ESTEIRA_MEDIR=0 desliga (nada e emitido, nem query).
//
//  A OUTRA METADE: `esteira_voo` (DLSS5_ESTEIRA_VOO) = quantos quadros a cadeia oculta pode
//  ter em voo quando o apresentador e quem vai a tela (1 = o de antes). Ver Renderer::AplicarVooOculto.
// =====================================================================================
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include <cstring>

namespace Esteira {

inline double Ms() {
    static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
}

// BUG 3: quem sabe o ESTADO do app (neural/filtros/FG/F6/modo) e o main.cpp; a linha `esteira 1s` ganha um
// sufixo " | estado: ..." para o log dizer o que estava ligado quando os tempos foram medidos.
inline void (*g_estadoFn)(char* buf, size_t n) = nullptr;
inline int  g_medir = -1;       // -1 = ainda nao lido (Ligado() le a chave na primeira vez)
constexpr int kVooPadrao = 1;   // decidido pela medida (sec. 18): ver o valor e o porque no relatorio
inline int  g_voo = 1;           // quadros em voo pedidos para a cadeia oculta (1..3)
inline bool g_vooAplicado = false;

enum Etapa { E_SLOT0 = 0, E_SLOT1, E_TICK, E_DEDUP, E_DESENHO, E_PRESENT, E_LER, E_ENVIAR, E_ACORDAR, E_APOS, E_N };

struct Quadro {
    double t[E_N];
    double carimbo = 0.0;
    uint64_t ponteN = 0;
    int gpuSlot = -1;
    bool ativo = false;
};
inline Quadro g_q;

// ------------------------------------------------------------------ GPU
constexpr int kR = 16;
struct GpuSlot { ID3D11Query* dj = nullptr; ID3D11Query* ts[4] = {}; bool emVoo = false; bool valido = false; uint64_t id = 0; };
inline GpuSlot g_gs[kR];
inline ID3D11Device* g_gdev = nullptr;
inline bool g_gpuFalhou = false;
inline uint64_t g_id = 0;                 // id do proximo quadro
inline uint64_t g_resolverDe = 1;         // o proximo id a resolver (ordem estrita)
inline uint64_t g_antId = 0; inline UINT64 g_antA = 0, g_antD = 0;

// ------------------------------------------------------------------ amostras da janela de 1 s
struct Serie {
    std::vector<double> v;
    void add(double x) { if (v.size() < 4096) v.push_back(x); }
    void reset() { v.clear(); }
    double pct(double q) const {
        if (v.empty()) return 0.0;
        std::vector<double> t(v); std::sort(t.begin(), t.end());
        size_t i = (size_t)(q * (double)(t.size() - 1) + 0.5); if (i >= t.size()) i = t.size() - 1;
        return t[i];
    }
};
enum S { S_IDADE = 0, S_SLOT, S_TICK, S_DEDUP, S_DESENHO, S_PRESENT, S_LER, S_ENVIAR, S_ACORDAR, S_APOS, S_VOLTA, S_INTERV,
         S_GPRE, S_GREDE, S_GPOS, S_GVAO, S_GQUADRO, S_GSERIAL,
         S_TFILA, S_TCAD, S_TCARIMBO, S_N };
inline Serie g_s[S_N];
inline SRWLOCK g_lkTela = SRWLOCK_INIT;     // as tres series da tela vem da thread do apresentador
inline double g_relIni = 0.0, g_ultSlot0 = 0.0;
inline uint64_t g_nJanela = 0;
inline uint64_t g_gpuResolvidos = 0, g_gpuPerdidos = 0;

// ------------------------------------------------------------------ ORDEM (a prova da inversao)
constexpr int kE = 1024;
inline std::atomic<double> g_envMs[kE];        // quando o laco entregou o quadro n da ponte (QPC ms)
inline std::atomic<uint64_t> g_envN[kE];
inline double g_ultCarRede = 0.0;               // thread do laco
inline uint64_t g_regRede = 0, g_regRedeTot = 0;
inline double g_ultCarTela = 0.0; inline uint64_t g_ultNTela = 0;   // thread do apresentador
inline std::atomic<uint64_t> g_regTela{ 0 }, g_regTelaTot{ 0 }, g_regNTot{ 0 }, g_telaN{ 0 }, g_telaNTot{ 0 }, g_telaSemCar{ 0 };
inline std::atomic<uint64_t> g_repTelaTot{ 0 };   // o mesmo carimbo foi a tela duas vezes (nao e inversao; so conta)

// ------------------------------------------------------------------ OCUPACAO DA GPU e GUARDA DE FOLGA
// Nesta maquina GPU saturada = nvlddmkm 153 (OCLUSAO-2309 sec. 16.1(iii) e sec. 18, rodada E2: 2 pacotes nossos na
// placa a 95-100%). Entao a FOLGA e requisito, e ela e medida 1x/s:
//   ocupacao = quadros x (custo do pacote do feed [gpu_pacote_ms do .status] + TRABALHO de pre+pos nossos [p05, teto 1,5 ms; sec. 20]) / tempo real
//   (o FG, quando ligado, nao entra: a esteira mede o caminho sem FG - o custo dele aparece no `gpu` do nvidia-smi)
//   ocupacao_janela = soma de (D - A) / tempo real: teto (inclui as esperas entre filas dentro do quadro)
// GUARDA (padrao 75%, chave esteira_guarda_pct): ocupacao > kGuardaLim por 2 s seguidos = GUARDA ATIVA: a fila da cadeia oculta volta/fica em 1 pacote
// (nunca mais que 1 em voo), AVISO no log com a taxa sustentavel COM folga. A captura espera (o DWM nao entrega
// o que nao cabe); nada e descartado, nenhum passe e cortado. Solta com < kGuardaSolta por 3 s.
// `nr_escala_auto` (padrao 0 = inerte): com 1, a guarda ativa por 10 s grava o degrau de resolucao interna seguinte
// (100 -> 75 -> 50) no [ultima] para a PROXIMA abertura - nada muda nesta sessao. Decisao do dono.
inline double kGuardaLim = 0.75, kGuardaSolta = 0.70;   // padrao 75% (coordenador, 23/09 15:05); [ultima] esteira_guarda_pct=N (50..95) ajusta; solta 5 pontos abaixo
inline double g_pacoteMs = 0.0;          // o main copia do .status do feed a cada 2 s
inline bool   g_guarda = false;          // ativa agora
inline int    g_guardaAcima = 0, g_guardaAbaixo = 0, g_guardaSegAtiva = 0;
inline uint64_t g_guardaVezes = 0;
inline bool   g_pedeVoo1 = false;        // o main aplica (latencia 1 na cadeia oculta)
inline int    g_escalaAuto = 0;          // [ultima] nr_escala_auto
inline int    g_escalaSugerida = 0;      // o main grava no ini (so com nr_escala_auto=1)
inline double g_somaAD = 0.0, g_somaPrePos = 0.0;
inline double g_ultOcup = -1.0, g_ultOcupJan = 0.0, g_ultQps = 0.0, g_ultRelMs = 0.0;   // sec. 21: -1 = sem medida (a guarda do FG e o painel leem)

// GUARDA COM RITMO (sec. 20, `[ultima] esteira_guarda_ritmo`, padrao 1; 0 = so o aviso, como na sec. 18).
// Com a fonte acima da capacidade (testufo a 240/s) a guarda so avisava e a GPU ficava em 89-92% - o regime do 153.
// Com o ritmo, a guarda ATIVA passa a ADMITIR a captura em ritmo: entre dois quadros aceitos passa no minimo
//   intervalo = custo de GPU do quadro (pacote do feed + pre/pos nossos) / limiar      (75% -> ocupacao ~75%)
// O laco ESPERA antes de pedir o proximo quadro (EsperarRitmo, antes da espera do slot e da marca A na GPU, para a
// espera nao entrar no pre/pos medido). Nada e descartado por nos: com a fila da captura cheia o SO deixa de entregar
// (o que o dreno devolver continua em `perdidos_na_captura`). SOLTA quando a fonte (aceitos + perdidos na captura) x
// custo cair abaixo de kRitmoSolta (65%) por 3 s - com a fonte acima, os aceitos sozinhos ja dao ~75%.
inline int    g_ritmoChave = 1;          // [ultima] esteira_guarda_ritmo
inline bool   g_ritmo = false;           // admitindo em ritmo agora
inline double kRitmoSolta = 0.65;
inline double g_ritmoIntervMs = 0.0;     // intervalo minimo entre quadros aceitos
inline double g_ritmoCustoMs = 0.0;      // custo por quadro suavizado (EMA de 1 s)
inline double g_ultAceitoMs = 0.0;       // SLOT0 do ultimo quadro que foi a rede
inline int    g_ritmoAbaixo = 0, g_ritmoSeg = 0;
inline uint64_t g_ritmoPerdAnt = 0;
inline double g_ritmoEsperaSoma = 0.0; inline uint64_t g_ritmoEsperas = 0;   // janela de 1 s (log)
inline HANDLE g_ritmoTimer = nullptr; inline bool g_ritmoTimerTentou = false;
// PLACA INTEIRA (RESET-2109, sec. 21). No reset de 23/09 21:26 a guarda regulava 75% da NOSSA fatia com a placa em
// 97-104% (Edge + DWM a 240 Hz + apresentador): a premissa "75% nossos = fora do regime do 153" caiu. A guarda passa a
// olhar tambem a utilizacao da PLACA INTEIRA (NVML, a mesma do nvidia-smi, todos os processos; thread propria 1x/s) com
// teto kPlacaLim (padrao 85%, `[ultima] esteira_guarda_placa_pct`, 0 = so a nossa parte). Sem NVML (placa nao NVIDIA),
// fica so a nossa parte, como antes.
// sec. 22: o FREIO DE EMERGENCIA (ver Freio, abaixo; sec. 24: o STALL agora e classificado - ver O CONTROLADOR DE CARGA)
inline double kFreioMs = 60.0, kFreioPlaca = 0.85;   // [ultima] esteira_freio_ms (0 = sem freio)
inline double g_freioAteMs = 0.0, g_freioUltMs = 0.0, g_freioIvMs = 0.0;
inline uint64_t g_freioVezes = 0;
inline double kPlacaLim = 0.95;   // sec. 23: teto da placa inteira = REDE DE SEGURANCA aprendida (95% sem reset; cai por placa depois de um reset)
inline std::atomic<int> g_placaHist[8]; inline std::atomic<double> g_placaHistMs[8]; inline std::atomic<unsigned> g_placaHistI{ 0 };
inline std::atomic<int>    g_placaPct{ -1 };
inline std::atomic<int> g_clockMhz{ 0 }, g_clockMaxMhz{ 0 };   // sec. 24: clock grafico (MHz) agora e o maior desta sessao (NVML)
inline std::atomic<double> g_placaMs{ 0.0 };
inline HANDLE g_placaThr = nullptr;
struct NvmlUtil { unsigned gpu, memory; };
typedef int (*NvmlInit_t)();
typedef int (*NvmlCount_t)(unsigned*);
typedef int (*NvmlHandle_t)(unsigned, void**);
typedef int (*NvmlUtil_t)(void*, NvmlUtil*);
typedef int (*NvmlClock_t)(void*, int, unsigned*);   // sec. 24: nvmlDeviceGetClockInfo(dev, NVML_CLOCK_GRAPHICS = 0, &mhz)
inline DWORD WINAPI PlacaThread(void*) {
    HMODULE m = LoadLibraryExW(L"nvml.dll", nullptr, 0x00000800 /*LOAD_LIBRARY_SEARCH_SYSTEM32*/);
    if (!m) m = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    if (!m) { ::Log("esteira: placa inteira - nvml.dll nao encontrada: a guarda olha so a nossa parte"); return 0; }
    NvmlInit_t init = (NvmlInit_t)(void*)GetProcAddress(m, "nvmlInit_v2");
    NvmlCount_t cnt = (NvmlCount_t)(void*)GetProcAddress(m, "nvmlDeviceGetCount_v2");
    NvmlHandle_t hnd = (NvmlHandle_t)(void*)GetProcAddress(m, "nvmlDeviceGetHandleByIndex_v2");
    NvmlUtil_t utl = (NvmlUtil_t)(void*)GetProcAddress(m, "nvmlDeviceGetUtilizationRates");
    NvmlClock_t clk = (NvmlClock_t)(void*)GetProcAddress(m, "nvmlDeviceGetClockInfo");
    unsigned n = 0;
    if (!init || !cnt || !hnd || !utl || init() != 0 || cnt(&n) != 0 || n == 0) { ::Log("esteira: placa inteira - NVML nao iniciou: a guarda olha so a nossa parte"); return 0; }
    void* h[4] = {}; unsigned nh = 0;
    for (unsigned i = 0; i < n && nh < 4; i++) if (hnd(i, &h[nh]) == 0) nh++;
    ::Log("esteira: placa inteira - NVML de pe (%u placa(s) NVIDIA); teto da placa %.0f%% (sec. 24: o NVML mede o TEMPO COM ALGUM KERNEL, amostra de 1/6 a 1 s - nao e carga; e so a REDE DE SEGURANCA; clock grafico %s para a previsao)",
          nh, 100.0 * kPlacaLim, clk ? "lido" : "indisponivel");
    for (;;) {
        int mx = -1; unsigned iMx = 0;
        for (unsigned i = 0; i < nh; i++) { NvmlUtil u{}; if (utl(h[i], &u) == 0 && (int)u.gpu > mx) { mx = (int)u.gpu; iMx = i; } }
        if (clk && nh) { unsigned mhz = 0; if (clk(h[iMx], 0, &mhz) == 0 && mhz > 0) { g_clockMhz.store((int)mhz); if ((int)mhz > g_clockMaxMhz.load()) g_clockMaxMhz.store((int)mhz); } }
        g_placaPct.store(mx); g_placaMs.store(Ms());
        { const unsigned k = g_placaHistI.fetch_add(1) % 8; g_placaHist[k].store(mx); g_placaHistMs[k].store(Ms()); }   // sec. 23: o teto aprendido olha os ultimos segundos antes do reset
        Sleep(1000);
    }
}
inline void IniciarPlaca() { if ((kPlacaLim > 0.0 || kFreioMs > 0.0) && !g_placaThr) g_placaThr = CreateThread(nullptr, 0, PlacaThread, nullptr, 0, nullptr); }
// sec. 23: a maior leitura da placa inteira nos ultimos `ms` (o reset registra X = isto)
inline int PlacaMaxRecente(double ms) { int mx = -1; const double ag = Ms(); for (int i = 0; i < 8; i++) if (ag - g_placaHistMs[i].load() <= ms && g_placaHist[i].load() > mx) mx = g_placaHist[i].load(); return mx; }
inline double Placa() { const int p = g_placaPct.load(); return (kPlacaLim > 0.0 && p >= 0 && Ms() - g_placaMs.load() <= 3000.0) ? p / 100.0 : -1.0; }

// GUARDA NO PAINEL (sec. 21, pedido do dono): `[ultima] esteira_guarda`. 0 = sem ritmo nem degrau: a guarda
// so mede e avisa (o comportamento da sec. 18). Muda em voo pelo painel (End). 2.1: DESMARCADA sempre que a chave
// faltar (com ou sem ini); so `esteira_guarda=1` liga - quem le e o main (Configurar). 0 aqui = o que vale antes do Configurar.
inline int    g_guardaChave = 0;

// RITMO EM DEGRAU (sec. 21, `[ultima] esteira_guarda_degrau`, padrao 1; 0 = o ritmo continuo da sec. 20). Com a guarda
// ativa e a captura em FILA do WGC, a fonte e admitida em passo INTEIRO N sobre a grade de composicao do DWM (ver o
// bloco DEGRAU no screen_capture.h: a captura segura os buffers ja processados e a esteira devolve um na hora certa).
// N = o menor passo VALIDO com refresh/N <= sustentavel com folga (limiar / custo). VALIDO: com o refresh multiplo de 60
// (M = refresh/60), N divide M ou e multiplo de M - a 240 Hz: 1, 2, 4, 8, 12... (3 daria 80/s e a linha de 60 do testufo
// cairia em 3/6 refreshes). Outros refreshes: qualquer inteiro. Histerese: sobe na hora; desce um degrau so depois de
// g_dgEspera s (5 s) com o degrau de baixo cabendo (com o custo medido nele, se ja esteve la); recaida em <= 5 s dobra a
// espera (ate 80 s). SOLTA quando a fonte estimada cabe (< 65% por g_dgSoltaEspera s).
inline int    g_degrauChave = 1;
inline int    g_dgN = 0;                          // passo em vigor (0 = sem degrau)
inline double g_dgCustoEm[33] = {}; inline double g_dgCustoEmMs[33] = {};
inline int    g_dgFolgaSeg = 0, g_dgAcimaSeg = 0, g_dgSeg = 0, g_dgSoltaSeg = 0;
inline double g_dgEspera = 5.0, g_dgSoltaEspera = 3.0, g_dgDesceuMs = 0.0, g_dgSoltouMs = 0.0;
inline HANDLE g_dgTimer = nullptr; inline bool g_dgTimerTentou = false;
inline bool DegrauValido(int n, double hz) {
    if (n < 1) return false;
    const double m = hz / 60.0, mr = floor(m + 0.5);
    if (mr >= 1.0 && fabs(m - mr) <= 0.01 * m) { const int M = (int)mr; return (M % n == 0) || (n % M == 0); }
    return true;
}
inline int DegrauPara(double hz, double sust) {   // o menor N valido com hz/N <= sust (0 = nem 32 cabe)
    for (int n = 1; n <= 32; n++) if (DegrauValido(n, hz) && hz / (double)n <= sust + 1e-6) return n;
    return 0;
}
inline bool DegrauAtivo() { return g_ritmo && g_dgN >= 2 && Degrau::g_N >= 2 && Degrau::g_cap != nullptr; }
inline double RitmoFps() {
    if (DegrauAtivo() && Degrau::Hz() > 0.0) return Degrau::Hz() / (double)g_dgN;
    return g_ritmoIntervMs > 0.0 ? 1000.0 / g_ritmoIntervMs : 0.0;
}
// FG (sec. 21, guarda do FG como ULTIMO recurso): true quando a guarda nao tem mais como baixar a carga - desligada no
// painel/ini, ou o ritmo ja no piso (20/s), ou o degrau no maior passo valido acima de 20/s.
inline bool LimitadorEsgotado() {
    if (!g_guardaChave || !g_ritmoChave) return true;
    if (!g_ritmo) return false;   // a guarda ainda nem entrou: ela vem antes
    if (DegrauAtivo()) {
        const double hz = Degrau::Hz();
        for (int n = g_dgN + 1; n <= 32; n++) if (DegrauValido(n, hz) && hz / (double)n >= 20.0) return false;
        return true;
    }
    return g_ritmoIntervMs >= 50.0 - 1e-6;
}
// RESET-2109 (sec. 21): a rede saiu do caminho (PASSAGEM SEGURA): nao ha pacote nosso para guardar. Solta guarda, ritmo e
// degrau na hora (na sessao do reset o ritmo seguiu a 31/s por 1 min sem rede, com o custo congelado de 23 ms).
inline void SoltarGuarda(const char* motivo) {
    if (!g_guarda && !g_ritmo && g_dgN == 0) return;
    ::Log("esteira: guarda de folga, ritmo e degrau SOLTOS - %s", motivo);
    g_guarda = false; g_ritmo = false; g_dgN = 0; Degrau::g_N = 0; g_freioAteMs = 0.0; g_freioIvMs = 0.0;
    g_guardaAcima = g_guardaAbaixo = 0; g_ritmoCustoMs = 0.0; g_ultOcup = -1.0;
}
// FREIO DE EMERGENCIA (sec. 22; PESQ-RESET-DRIVER sec. 4.1): em todos os resets de 23/09 vieram STALLs de 75-120 ms
// 1-2 s ANTES do nvlddmkm 153. Um quadro nosso com a volta (slot -> AposPresent) acima de kFreioMs (60 ms,
// `[ultima] esteira_freio_ms`, 0 = sem freio) com a PLACA INTEIRA acima de kFreioPlaca (85%) desce UM degrau NA HORA,
// sem as janelas de 2 s da guarda: degrau N -> o proximo N valido (metade da taxa ou menos); sem degrau, o ritmo
// continuo passa a 2/3 da taxa (piso 20/s). Log `guarda: freio`. VOLTA pela mesma histerese do degrau (desce so depois
// de g_dgEspera s com o degrau de baixo cabendo; recaida em <= 5 s dobra a espera); enquanto o freio segura
// (g_dgEspera s) o ritmo/degrau nao solta nem desce.
inline double PlacaCrua() { const int p = g_placaPct.load(); return (p >= 0 && Ms() - g_placaMs.load() <= 3000.0) ? p / 100.0 : -1.0; }
inline bool FreioSegura() { return g_freioAteMs > 0.0 && Ms() < g_freioAteMs; }
// =====================================================================================
// sec. 24 (PESQ-CONTROLE-CARGA-2309 sec. 3.8): O CONTROLADOR DE CARGA - substitui o bloco "guarda por sintomas" da sec. 23
// =====================================================================================
// Regras do dono (inalteradas): a UNICA alavanca e o DEGRAU DE TAXA (divisores do refresh; sem degrau, o ritmo continuo em
// passos de 2/3 a partir da fonte livre, piso 20/s) - nunca a resolucao; nenhum quadro descartado por nos (a captura so
// espera); sem popups (so a barra, que nao mudou); aprendizado por placa/config ([taxa_estavel]); teto de sessao so com
// falhas ESPALHADAS; depois da cena pesada SOBE de volta.
//
// SINAIS (janela de 1 s, a mesma da linha `esteira 1s`):
//   C. PRINCIPAL = o TEMPO DE GPU do nosso quadro (pacote do feed + pre/pos nossos, p90 das ultimas 5 janelas) contra o
//      ORCAMENTO do degrau (1000/taxa ms). PREVISAO de ocupacao num degrau de taxa R = custo(R) x R / 1000 + outros
//      (outros = placa inteira NVML - a nossa parte, 0..0,5; `guarda_outros=0` tira). Ela decide o DESTINO da descida
//      (o maior degrau com previsao <= guarda_alvo_descida) e o PORTAO da subida (<= guarda_portao) - e NUNCA dispara uma
//      descida sozinha (veto do dono a "desce por ocupacao fixa").
//   A/B. PANICO = o que a pessoa ve: atrasados (> guarda_atr_pct % e >= guarda_atr_min quadros, com >= 20 na tela) e a
//      fila da captura (idade p50 > guarda_fila_quadros x o quadro da fonte, ou subindo guarda_fila_subindo janelas
//      seguidas). Uma janela e RUIM com A ou B (ou a nossa parte > esteira_guarda_pct, se ligada).
//   D. STALL (volta do quadro > esteira_freio_ms) - reage no proprio quadro (Freio): isolado = so log (+ sem sonda por
//      guarda_stall_bloq_s); 2 em guarda_stall_par_s ou com a GPU alta (p90 > guarda_gpu_alta x a mediana de 30 s, ou
//      previsao no degrau atual > 0,95) = desce 1; >= guarda_stall_grave_ms = desce 1 e guarda_stall_hold_s sem subir.
//   E. REDE NVML (placa inteira) > teto aprendido em k de n janelas = desce 1 (so rede de seguranca; o NVML e "tempo com
//      algum kernel", nao carga).
// DESCIDA: k de n janelas ruins (guarda_k=2 de guarda_n=3; carga intermitente ruim/boa/ruim conta) -> DIRETO ao degrau com
//   previsao <= 0,85 (sem medida de GPU: 1 degrau); depois HOLD de guarda_hold_s (5 s) sem subir, drenando.
// SUBIDA: so com a janela limpa, fora do HOLD, >= guarda_troca_min_s (2 s) desde a ultima troca e >= guarda_sonda1_s (3 s)
//   limpos; portao: previsao no degrau de cima <= 0,80; com <= 0,60 PULA para o mais alto que passa no portao. Cada subida
//   e uma SONDA com prova de guarda_prova_s (10 s): 1 janela ruim (ou STALL que desceria) volta NA HORA e conta FALHA
//   daquele degrau -> espera daquele degrau 15/30/60/120 s (dobra; guarda_backoff_s/_max_s), zerada depois de
//   guarda_backoff_zera_s (60 s) limpos nele.
// TETO DA SESSAO: guarda_teto_falhas (3) falhas de SONDA no mesmo degrau, contadas so com >= guarda_teto_entre_s (2 min)
//   entre si E >= guarda_teto_espalho_s (6 min) da 1a a ultima. Mesmo com teto, 1 sonda a cada guarda_teto_sonda_s (10 min);
//   se ela passa, o teto sai.
// CUSTO INFLADO NO DEGRAU BAIXO (o clock cai com a GPU ociosa): a previsao para um degrau MAIS ALTO usa, nesta ordem:
//   (1) o custo MEDIDO naquele degrau (ou no mais proximo acima dele) nos ultimos guarda_custo_mem_s (10 min) - o que ja
//       aconteceu nesta placa, com o clock que ela tinha la;
//   (2) sem essa medida, o custo de agora NORMALIZADO pelo clock grafico (NVML): custo x clock_agora / clock_ref, com
//       clock_ref = o maior clock visto nesta sessao (fator limitado a 0,5..1; `guarda_clock=0` desliga);
//   (3) sem NVML, min(custo de agora, calibracao da placa: rede + fixo, calibracao.h).
//   A escolha: (1) e medida real e nao depende de o custo ser linear no clock; (2) cobre o caso "nunca estive la nesta
//   sessao" com a fisica certa (tempo de GPU ~ 1/clock) e a prova de 10 s pega o erro; (3) e a reserva de sempre.
// LOG: `guarda: estado ...` (fase, degrau, previsao, motivo) no maximo 1x/s, so quando fase/degrau/motivo mudam.
struct ChavesCtl {
    int    k = 2, n = 3;                                     // guarda_k / guarda_n: janelas ruins para descer
    double atrPct = 1.0; int atrMin = 2;                     // guarda_atr_pct / guarda_atr_min
    double filaQuadros = 1.0; int filaSubindo = 3;          // guarda_fila_quadros / guarda_fila_subindo
    double alvoDescida = 0.85;                               // guarda_alvo_descida
    double holdS = 5.0;                                      // guarda_hold_s
    double portao = 0.80, salto = 0.60;                      // guarda_portao / guarda_salto
    double sonda1S = 3.0, provaS = 10.0;                     // guarda_sonda1_s / guarda_prova_s
    double backoffS = 15.0, backoffMaxS = 120.0, backoffZeraS = 60.0;   // guarda_backoff_s / _max_s / _zera_s
    double trocaMinS = 2.0;                                  // guarda_troca_min_s
    double stallGraveMs = 250.0, stallParS = 5.0, stallHoldS = 30.0, stallBloqS = 10.0, gpuAlta = 1.2, prevPico = 0.95;
    int    tetoFalhas = 3; double tetoEntreS = 120.0, tetoEspalhoS = 360.0, tetoSondaS = 600.0;
    double memAlfa = 0.3, memMinS = 60.0, memPosDescidaS = 30.0, memIniS = 10.0;   // guarda_mem_*
    int    placaVoltaPts = 2; double placaVoltaMin = 30.0; int placaVoltaMax = 95; // guarda_placa_volta_pts / _min
    double custoMemS = 600.0;                                // guarda_custo_mem_s
    int    clockNorm = 1, outros = 1;                        // guarda_clock / guarda_outros
};
inline ChavesCtl g_ctl;
template <class F> inline void LerChavesCtl(F get) {
    auto d = [&](const wchar_t* k, double& v, double lo, double hi) { const std::wstring s = get(k); if (!s.empty()) { const double x = NumDecW(s.c_str()); v = x < lo ? lo : x > hi ? hi : x; } };
    auto i = [&](const wchar_t* k, int& v, int lo, int hi) { const std::wstring s = get(k); if (!s.empty()) { const int x = _wtoi(s.c_str()); v = x < lo ? lo : x > hi ? hi : x; } };
    ChavesCtl& c = g_ctl;
    i(L"guarda_k", c.k, 1, 10); i(L"guarda_n", c.n, 1, 10); if (c.k > c.n) c.k = c.n;
    d(L"guarda_atr_pct", c.atrPct, 0.1, 50.0); i(L"guarda_atr_min", c.atrMin, 1, 100);
    d(L"guarda_fila_quadros", c.filaQuadros, 0.25, 10.0); i(L"guarda_fila_subindo", c.filaSubindo, 0, 10);
    d(L"guarda_alvo_descida", c.alvoDescida, 0.3, 1.0); d(L"guarda_hold_s", c.holdS, 0.0, 60.0);
    d(L"guarda_portao", c.portao, 0.2, 1.0); d(L"guarda_salto", c.salto, 0.0, 1.0); if (c.salto > c.portao) c.salto = c.portao;
    d(L"guarda_sonda1_s", c.sonda1S, 1.0, 120.0); d(L"guarda_prova_s", c.provaS, 1.0, 120.0);
    d(L"guarda_backoff_s", c.backoffS, 1.0, 600.0); d(L"guarda_backoff_max_s", c.backoffMaxS, 1.0, 3600.0); d(L"guarda_backoff_zera_s", c.backoffZeraS, 5.0, 3600.0);
    if (c.backoffMaxS < c.backoffS) c.backoffMaxS = c.backoffS;
    d(L"guarda_troca_min_s", c.trocaMinS, 0.0, 30.0);
    d(L"guarda_stall_grave_ms", c.stallGraveMs, 60.0, 5000.0); d(L"guarda_stall_par_s", c.stallParS, 0.0, 60.0);
    d(L"guarda_stall_hold_s", c.stallHoldS, 0.0, 600.0); d(L"guarda_stall_bloq_s", c.stallBloqS, 0.0, 120.0);
    d(L"guarda_gpu_alta", c.gpuAlta, 1.0, 5.0); d(L"guarda_prev_pico", c.prevPico, 0.5, 2.0);
    i(L"guarda_teto_falhas", c.tetoFalhas, 1, 20); d(L"guarda_teto_entre_s", c.tetoEntreS, 0.0, 3600.0);
    d(L"guarda_teto_espalho_s", c.tetoEspalhoS, 0.0, 7200.0); d(L"guarda_teto_sonda_s", c.tetoSondaS, 30.0, 7200.0);
    d(L"guarda_mem_alfa", c.memAlfa, 0.01, 1.0); d(L"guarda_mem_min_s", c.memMinS, 0.0, 3600.0);
    d(L"guarda_mem_pos_descida_s", c.memPosDescidaS, 0.0, 600.0); d(L"guarda_mem_ini_s", c.memIniS, 0.0, 600.0);
    i(L"guarda_placa_volta_pts", c.placaVoltaPts, 0, 20); d(L"guarda_placa_volta_min", c.placaVoltaMin, 1.0, 1440.0);
    d(L"guarda_custo_mem_s", c.custoMemS, 10.0, 7200.0); i(L"guarda_clock", c.clockNorm, 0, 1); i(L"guarda_outros", c.outros, 0, 1);
    ::Log("guarda: controlador sec. 24 - descida %d de %d janelas (atrasados > %.1f%% e >= %d; fila > %.2f quadro ou subindo %d janelas) -> degrau com previsao <= %.2f, HOLD %.0f s |"
          " subida: portao %.2f, salto %.2f, 1a sonda %.0f s limpos, prova %.0f s, backoff %.0f..%.0f s (zera com %.0f s limpos), troca min %.0f s |"
          " STALL: isolado = log (+%.0f s sem sonda), 2 em %.0f s ou GPU alta (%.2fx mediana / previsao > %.2f) = -1, >= %.0f ms = -1 + %.0f s sem subir |"
          " teto da sessao: %d falhas, >= %.0f s entre si e >= %.0f s 1a->ultima, sonda a cada %.0f s | memoria: alfa %.2f, >= %.0f s estaveis (fora %.0f s pos-descida e %.0f s iniciais) |"
          " rede NVML: +%d pts por sessao limpa >= %.0f min ate %d%% | custo por degrau vale %.0f s; clock NVML %s; outros %s",
          c.k, c.n, c.atrPct, c.atrMin, c.filaQuadros, c.filaSubindo, c.alvoDescida, c.holdS,
          c.portao, c.salto, c.sonda1S, c.provaS, c.backoffS, c.backoffMaxS, c.backoffZeraS, c.trocaMinS,
          c.stallBloqS, c.stallParS, c.gpuAlta, c.prevPico, c.stallGraveMs, c.stallHoldS,
          c.tetoFalhas, c.tetoEntreS, c.tetoEspalhoS, c.tetoSondaS, c.memAlfa, c.memMinS, c.memPosDescidaS, c.memIniS,
          c.placaVoltaPts, c.placaVoltaMin, c.placaVoltaMax, c.custoMemS, c.clockNorm ? "normaliza" : "desligado", c.outros ? "na previsao" : "fora");
}

inline std::atomic<uint64_t> g_extAtrasados{ 0 }, g_extTelaNovos{ 0 };   // o main copia do apresentador (acumulados)
inline uint64_t g_sAtrAnt = 0, g_sTelaAnt = 0;
inline double g_jVoltaMax = 0.0;
inline double g_pedeTaxa = 0.0;          // o main pede: comecar nesta taxa (memoria da configuracao)
inline double g_taxaAgora = 0.0;         // 1x/s: a taxa em vigor (0 = livre, sem guarda)
inline double g_taxaMin = 0.0;           // a menor taxa desta sessao (so log)
inline double g_tetoSessao = 0.0;        // 0 = sem teto de sessao
inline double g_fonteLivreFps = 0.0;     // a fonte medida sem guarda (a chave da memoria usa)
inline bool   g_estavelAgora = false;    // 1x/s: este segundo conta como ESTAVEL para a memoria (fora HOLD/prova/pos-descida)
inline double g_iniCtlMs = 0.0;          // sec. 27: o 1o Relatorio (arranque) ou a ultima troca - STALL nos 15 s seguintes nao conta
inline double g_custoCalibMs = 0.0;      // o main poe 1x/s: rede + fixo pela calibracao da placa (reserva da previsao)

// ---- estado do controlador (thread do laco: Relatorio e Freio)
enum FaseCtl { F_LIVRE = 0, F_NORMAL, F_HOLD, F_PROVA };
inline int    g_fase = F_LIVRE;
inline double g_holdAteMs = 0.0, g_semSubirAteMs = 0.0, g_semSondaAteMs = 0.0, g_ultTrocaMs = 0.0, g_ultDescidaMs = 0.0;
inline double g_provaAteMs = 0.0, g_provaDe = 0.0, g_provaAlvo = 0.0;   // taxa de onde subiu (0 = livre) e para onde
inline int    g_limpoSeg = 0, g_limpoNivelSeg = 0;
inline bool   g_jRuim[10] = {}, g_jPlaca[10] = {}; inline int g_jI = 0;
inline double g_idadeHist[12] = {}; inline int g_idadeN = 0;
inline double g_stallMs[4] = {}; inline int g_stallI = 0;
inline double g_ultTetoSondaMs = 0.0;
inline char   g_motivo[200] = "livre"; inline char g_estadoLog[240] = "";
// custo por quadro das ultimas janelas (p90 de 5; mediana de 30)
inline double g_custoHist[30] = {}; inline int g_custoN = 0, g_custoI = 0;
// custo MEDIDO por degrau (sec. 24: a previsao para um degrau alto usa o que foi medido LA)
struct CustoNivel { double fps = 0.0, ms = 0.0, quandoMs = 0.0; };
inline CustoNivel g_custoNivel[24]; inline int g_nCustoNivel = 0;
// falhas de SONDA por degrau (backoff e teto de sessao)
struct NivelFalha { double fps = 0.0, esperaS = 0.0, proxMs = 0.0, primMs = 0.0, ultMs = 0.0; int nFalhas = 0; };
inline NivelFalha g_nf[24]; inline int g_nNf = 0;
inline NivelFalha* AchaNf(double fps, bool cria) {
    for (int i = 0; i < g_nNf; i++) if (fabs(g_nf[i].fps - fps) < 1.0) return &g_nf[i];
    if (!cria || g_nNf >= 24) return nullptr;
    g_nf[g_nNf] = NivelFalha{}; g_nf[g_nNf].fps = fps; return &g_nf[g_nNf++];
}
inline void ResetarSessaoTaxa(const char* motivo) {   // trocou configuracao/fonte: teto de sessao, backoffs e custos por degrau recomecam
    if (g_tetoSessao > 0.0 || g_nNf) ::Log("guarda: teto da sessao, backoffs e falhas de sonda ZERADOS - %s", motivo);
    g_nNf = 0; g_tetoSessao = 0.0; g_taxaMin = 0.0; g_nCustoNivel = 0; g_custoN = g_custoI = 0; g_ultTetoSondaMs = 0.0;
    g_iniCtlMs = Ms();   // sec. 27: a remontagem depois da troca tem a mesma carencia do arranque
}
inline bool DgPode() { return g_degrauChave && Degrau::g_cap != nullptr && Degrau::Hz() > 0.0 && Degrau::Ancorar() && Degrau::GradeConfere(); }

// Os degraus possiveis, do mais alto (0 = livre, a fonte inteira) ao piso de 20/s.
inline int Niveis(double* v, int max, bool dp, double hz) {
    int n = 0;
    const double livre = g_fonteLivreFps > 1.0 ? g_fonteLivreFps : (hz > 0.0 ? hz : 60.0);
    v[n++] = livre;
    if (dp) {
        for (int N = 2; N <= 32 && n < max; N++) {
            if (!DegrauValido(N, hz)) continue;
            const double r = hz / (double)N;
            if (r < 20.0 - 1e-6) break;
            if (r < livre * 0.98) v[n++] = r;
        }
    } else {
        double r = livre;
        while (n < max && r > 20.0 + 0.5) { r = std::max(20.0, r * 2.0 / 3.0); v[n++] = r; }
    }
    return n;
}
inline int NivelAtual(const double* v, int n) {
    if (!g_ritmo) return 0;
    const double r = RitmoFps(); int m = 1; double best = 1e9;
    for (int i = 1; i < n; i++) if (fabs(v[i] - r) < best) { best = fabs(v[i] - r); m = i; }
    return m;
}
// custo por quadro de agora: p90 das ultimas 5 janelas (0 = sem medida)
inline double CustoP90() {
    const int m = std::min(5, g_custoN); if (m <= 0) return 0.0;
    double t[5]; for (int i = 0; i < m; i++) t[i] = g_custoHist[(g_custoI - 1 - i + 30) % 30];
    std::sort(t, t + m); size_t k = (size_t)(0.9 * (double)(m - 1) + 0.5); return t[k];
}
inline double CustoMediana() {
    if (g_custoN < 5) return 0.0;
    double t[30]; for (int i = 0; i < g_custoN; i++) t[i] = g_custoHist[i];
    std::sort(t, t + g_custoN); return t[g_custoN / 2];
}
inline void GuardarCustoNivel(double fps, double ms) {
    if (fps <= 1.0 || ms <= 0.0) return;
    CustoNivel* c = nullptr;
    for (int i = 0; i < g_nCustoNivel; i++) if (fabs(g_custoNivel[i].fps - fps) < 1.0) c = &g_custoNivel[i];
    if (!c) { if (g_nCustoNivel >= 24) return; c = &g_custoNivel[g_nCustoNivel++]; c->fps = fps; c->ms = ms; }
    else c->ms = 0.7 * c->ms + 0.3 * ms;
    c->quandoMs = Ms();
}
// "outros" (placa inteira - a nossa parte), 0..0,5
inline double g_outrosAgora = 0.0;
// PREVISAO de ocupacao no degrau de taxa `fps`. `org` (opcional) recebe de onde veio o custo. < 0 = sem medida de GPU.
inline double Previsao(double fps, double fpsAgora, const char** org = nullptr) {
    const double agoraCusto = CustoP90();
    if (agoraCusto <= 0.0 || fps <= 0.0) { if (org) *org = "sem medida"; return -1.0; }
    double c = agoraCusto; const char* o = "medido agora";
    if (fps > fpsAgora + 0.5) {   // degrau MAIS ALTO: o custo de agora pode estar inflado pelo clock baixo
        // sec. 27 (v3): o MENOR dos candidatos, nao o primeiro. No teste do dono (24/09 00:36) o degrau baixo derrubou o clock
        // (315-1065 de 2895 MHz), o custo "medido" subiu de 7 para 21 ms e o "medido naquele degrau" gravou o numero velho do
        // .status na prova (1,60 a 60/s): 1 passe a 1080p ficou preso em 30/s. Subir com a estimativa otimista e seguro - a
        // sonda de 10 s prova; descer continua usando o custo de agora (conservador).
        const double ag = Ms(); const CustoNivel* best = nullptr;
        for (int i = 0; i < g_nCustoNivel; i++) {
            const CustoNivel& k = g_custoNivel[i];
            if (k.fps >= fps - 0.5 && ag - k.quandoMs <= 1000.0 * g_ctl.custoMemS && (!best || k.fps < best->fps)) best = &k;
        }
        if (best && best->ms < c) { c = best->ms; o = "medido naquele degrau"; }
        const int ck = g_clockMhz.load(), cr = g_clockMaxMhz.load();
        if (g_ctl.clockNorm && ck > 0 && cr > 0 && ck < cr) {
            double f = (double)ck / (double)cr; if (f < 0.15) f = 0.15;
            if (agoraCusto * f < c) { c = agoraCusto * f; o = "normalizado pelo clock"; }
        }
        if (g_custoCalibMs > 0.0 && g_custoCalibMs < c) { c = g_custoCalibMs; o = "calibracao da placa"; }
    }
    if (org) *org = o;
    return c * fps / 1000.0 + (g_ctl.outros ? g_outrosAgora : 0.0);
}
// sec. 27 (v3): CARGA REAL. A guarda so rebaixa com prova de que a GPU e o gargalo: a placa inteira (NVML) >= kFreioPlaca,
// ou a previsao no degrau atual com o custo OTIMISTA (clock cheio / calibracao) acima de guarda_prev_pico. Fonte que cabe
// no orcamento NUNCA e rebaixada: atrasados/fila/STALL com a placa folgada sao da CPU, do arranque (montagem da rede, 1,5 s
// no log do dono) ou da fonte esparsa - rebaixar so derruba o clock e infla o custo (o laco que prendeu o video em 30/s).
inline double PrevisaoOtimista(double fps) {
    const double agoraCusto = CustoP90();
    if (agoraCusto <= 0.0 || fps <= 0.0) return -1.0;
    double c = agoraCusto;
    const int ck = g_clockMhz.load(), cr = g_clockMaxMhz.load();
    if (g_ctl.clockNorm && ck > 0 && cr > 0 && ck < cr) { double f = (double)ck / (double)cr; if (f < 0.15) f = 0.15; c *= f; }
    if (g_custoCalibMs > 0.0 && g_custoCalibMs < c) c = g_custoCalibMs;
    return c * fps / 1000.0 + (g_ctl.outros ? g_outrosAgora : 0.0);
}
inline bool CargaReal(double fpsAgora, double placa) {
    if (placa >= 0.0 && placa >= kFreioPlaca) return true;
    const double p = PrevisaoOtimista(fpsAgora);
    return p > g_ctl.prevPico;
}
inline char g_motivoDet[200] = "";
inline void Estado(const char* cod, const char* det = "") { snprintf(g_motivo, sizeof g_motivo, "%s", cod); snprintf(g_motivoDet, sizeof g_motivoDet, "%s", det); }

// Poe a admissao em <= R quadros/s (degrau: o menor N valido com hz/N <= R; senao, ritmo continuo).
inline void AplicarTaxa(double R, bool dp, double hz) {
    if (!g_ritmo) { g_ritmo = true; g_ritmoAbaixo = 0; g_ritmoSeg = 0; g_dgSeg = 0; }
    if (!g_guarda) { g_guarda = true; g_guardaVezes++; g_guardaSegAtiva = 0; }
    if (dp) {
        int n = DegrauPara(hz, R); if (n < 2) n = 2;
        g_dgN = n; Degrau::g_N = n; Degrau::Rearmar();
        g_ritmoIntervMs = 1000.0 * (double)n / hz;
    } else {
        g_dgN = 0; Degrau::g_N = 0;
        double iv = 1000.0 / (R > 1.0 ? R : 1.0);
        g_ritmoIntervMs = iv < 1000.0 / 360.0 ? 1000.0 / 360.0 : iv > 50.0 ? 50.0 : iv;
    }
    if (g_taxaMin <= 0.0 || RitmoFps() < g_taxaMin - 0.5) g_taxaMin = RitmoFps();
}
inline void SoltarPorFolga(const char* motivo) {
    g_ritmo = false; g_guarda = false; g_dgN = 0; Degrau::g_N = 0; g_guardaAcima = g_guardaAbaixo = 0;
    ::Log("guarda: SOLTA - %s", motivo);
}
// Vai ao degrau i da lista (0 = livre).
inline void AplicarNivel(const double* v, int i, bool dp, double hz, const char* motivo) {
    if (i <= 0) SoltarPorFolga(motivo);
    else AplicarTaxa(v[i], dp, hz);
    g_ultTrocaMs = Ms();
}
// Uma falha de SONDA no degrau `fps`: backoff dobra; teto da sessao so com falhas espalhadas.
inline void FalhaSonda(double fps, const char* motivo) {
    const double ag = Ms();
    NivelFalha* f = AchaNf(fps, true); if (!f) return;
    f->esperaS = f->esperaS <= 0.0 ? g_ctl.backoffS : std::min(g_ctl.backoffMaxS, f->esperaS * 2.0);
    f->proxMs = ag + 1000.0 * f->esperaS;
    bool contou = false;
    if (f->nFalhas == 0 || ag - f->ultMs >= 1000.0 * g_ctl.tetoEntreS) { if (f->nFalhas == 0) f->primMs = ag; f->nFalhas++; f->ultMs = ag; contou = true; }
    ::Log("guarda: SONDA FALHOU em %.0f/s (%s) - espera deste degrau %.0f s%s (falhas espalhadas %d, 1a->ultima %.0f s)",
          fps, motivo, f->esperaS, contou ? "" : " (a < 2 min da ultima contada: a mesma fase pesada, nao conta para o teto)", f->nFalhas, (f->ultMs - f->primMs) / 1000.0);
    if (f->nFalhas >= g_ctl.tetoFalhas && f->ultMs - f->primMs >= 1000.0 * g_ctl.tetoEspalhoS && (g_tetoSessao <= 0.0 || fps < g_tetoSessao - 0.5)) {
        g_tetoSessao = fps; g_ultTetoSondaMs = ag;
        ::Log("guarda: teto da sessao = %.0f/s (%d sondas falharam nele, >= %.0f s entre si e %.0f s da 1a a ultima); 1 sonda a cada %.0f s mesmo assim; zera ao trocar de configuracao/fonte",
              fps, f->nFalhas, g_ctl.tetoEntreS, (f->ultMs - f->primMs) / 1000.0, g_ctl.tetoSondaS);
    }
}
// DESCE ao degrau `dest` (> atual). `hold` = segundos sem subir depois. Na prova, conta a falha da sonda.
inline bool DescerPara(const double* v, int n, int atual, int dest, bool dp, double hz, double holdS, const char* motivo) {
    if (dest >= n) dest = n - 1;
    if (dest <= atual) {
        static double avisou = 0.0; const double ag = Ms();
        if (ag - avisou > 10000.0) { avisou = ag; ::Log("guarda: SINTOMA (%s) mas a taxa ja esta no piso (%.0f/s) - nada mais a descer", motivo, v[atual]); }
        return false;
    }
    const double ag = Ms();
    const double de = g_ritmo ? RitmoFps() : v[0];
    if (g_fase == F_PROVA) FalhaSonda(g_provaAlvo, motivo);
    AplicarNivel(v, dest, dp, hz, "");
    g_fase = F_HOLD; g_holdAteMs = ag + 1000.0 * holdS; g_ultDescidaMs = ag; g_limpoSeg = 0; g_limpoNivelSeg = 0;
    for (int i = 0; i < 10; i++) { g_jRuim[i] = false; g_jPlaca[i] = false; }
    if (g_voo > 1 || g_vooAplicado) g_pedeVoo1 = true;
    ::Log("guarda: SINTOMA (%s) - desce %.0f/s -> %.0f/s%s, HOLD %.0f s (menor desta sessao %.0f/s)", motivo, de, RitmoFps(),
          DegrauAtivo() ? " (degrau)" : " (ritmo continuo)", holdS, g_taxaMin);
    return true;
}
inline int IndiceDe(const double* v, int n, double fps) {
    if (fps <= 0.0) return 0;
    int m = 0; double best = 1e9;
    for (int i = 0; i < n; i++) if (fabs(v[i] - fps) < best) { best = fabs(v[i] - fps); m = i; }
    return m;
}
// GPU alta = o p90 das ultimas 5 janelas passa de guarda_gpu_alta x a mediana de 30 s, ou a previsao no degrau atual > 0,95.
inline bool GpuAlta(double fpsAgora) {
    const double p90 = CustoP90(), med = CustoMediana();
    if (p90 > 0.0 && med > 0.0 && p90 > g_ctl.gpuAlta * med) return true;
    return Previsao(fpsAgora, fpsAgora) > g_ctl.prevPico;
}
// STALL (sec. 24, PESQ 3.3): a volta de um quadro passou de kFreioMs. Classifica pelo tempo de GPU (Intel: parede >> GPU =
// pico do SO/DWM) e pela repeticao (Unreal: 2 seguidos). No maximo UMA acao por segundo.
inline void Freio(double voltaMs, double placa) {
    const double agora = Ms();
    if (voltaMs > g_jVoltaMax) g_jVoltaMax = voltaMs;
    const double anterior = g_stallI > 0 ? g_stallMs[(g_stallI - 1) % 4] : 0.0;
    g_stallMs[g_stallI % 4] = agora; g_stallI++;
    if (g_freioUltMs > 0.0 && agora - g_freioUltMs < 1000.0) return;
    if (!g_ritmoChave || !g_guardaChave) {
        static bool avisou = false;
        if (!avisou) { avisou = true; ::Log("guarda: STALL %.0f ms (placa %.0f%%), mas a folga de GPU esta desligada (painel/ini): SEM efeito, so aviso", voltaMs, 100.0 * placa); }
        return;
    }
    const bool dp = DgPode(); const double hz = Degrau::Hz();
    double v[40]; const int n = Niveis(v, 40, dp, hz); const int at = NivelAtual(v, n);
    const double fpsAgora = g_ritmo ? RitmoFps() : ((agora - g_ultRelMs <= 2500.0 && g_ultQps > 1.0) ? g_ultQps : v[0]);
    // sec. 27 (v3): sem CARGA REAL (placa < kFreioPlaca e previsao otimista <= guarda_prev_pico) ou no arranque (15 s), o
    // STALL e so log: o do teste do dono (1518 ms, placa 1%) era a montagem da rede e derrubou o video de 60 para 40/s.
    const bool carga = CargaReal(fpsAgora, placa) && (g_iniCtlMs > 0.0 && agora - g_iniCtlMs >= 15000.0);
    const bool grave = carga && voltaMs >= g_ctl.stallGraveMs;
    const bool par = carga && anterior > 0.0 && agora - anterior <= 1000.0 * g_ctl.stallParS;
    const bool alta = carga && GpuAlta(fpsAgora);
    if (!grave && !par && !alta) {
        if (carga) g_semSondaAteMs = std::max(g_semSondaAteMs, agora + 1000.0 * g_ctl.stallBloqS);   // sec. 27: sem carga, nao atrasa a subida
        static double avisou = 0.0;
        if (agora - avisou > 5000.0) {
            avisou = agora;
            ::Log("guarda: STALL isolado %.0f ms (placa %.0f%%; GPU normal: p90 %.2f ms, mediana %.2f ms%s) - so log; sem sonda de subida por %.0f s",
                  voltaMs, placa >= 0.0 ? 100.0 * placa : -1.0, CustoP90(), CustoMediana(), carga ? "" : "; SEM carga real de GPU ou arranque: nao rebaixa", carga ? g_ctl.stallBloqS : 0.0);
        }
        return;
    }
    g_freioUltMs = agora;
    char m[220];
    snprintf(m, sizeof m, "STALL %.0f ms%s%s%s, placa %.0f%%", voltaMs, grave ? " (grave: desce na hora)" : "", par ? " (2o em poucos s)" : "",
             alta ? " (com a GPU alta)" : "", placa >= 0.0 ? 100.0 * placa : -1.0);
    int dest = at + 1;
    if (g_fase == F_PROVA) { const int d = IndiceDe(v, n, g_provaDe); if (d > at) dest = d; }   // na prova: volta de onde subiu
    const double hold = grave ? std::max(g_ctl.holdS, g_ctl.stallHoldS) : g_ctl.holdS;
    if (DescerPara(v, n, at, dest, dp, hz, hold, m)) {
        g_freioVezes++;
        if (grave) g_semSubirAteMs = agora + 1000.0 * g_ctl.stallHoldS;
    }
}
// SUBIDA (sec. 24, PESQ 3.4): chamada 1x/s com a janela limpa e a fase NORMAL.
inline void TentarSubir(const double* v, int n, int at, bool dp, double hz, double fpsAgora) {
    (void)n;
    const double ag = Ms(); char det[200];
    if (!g_ritmo || at <= 0) { Estado("livre"); return; }
    if (ag < g_semSubirAteMs) { snprintf(det, sizeof det, "faltam %.0f s", (g_semSubirAteMs - ag) / 1000.0); Estado("STALL grave recente: sem subir", det); return; }
    if (ag < g_semSondaAteMs) { snprintf(det, sizeof det, "faltam %.0f s", (g_semSondaAteMs - ag) / 1000.0); Estado("STALL isolado recente: sem sonda", det); return; }
    if (ag - g_ultTrocaMs < 1000.0 * g_ctl.trocaMinS) { Estado("tempo minimo entre trocas"); return; }
    if (g_limpoSeg < (int)(g_ctl.sonda1S + 0.5)) { snprintf(det, sizeof det, "%d de %.0f s", g_limpoSeg, g_ctl.sonda1S); Estado("aguardando segundos limpos", det); return; }
    auto bloqueado = [&](int j, char* why, size_t wn) -> bool {
        if (NivelFalha* f = AchaNf(v[j], false)) if (f->proxMs > ag) { snprintf(why, wn, "backoff de %.0f/s: espera %.0f s, faltam %.0f s", v[j], f->esperaS, (f->proxMs - ag) / 1000.0); return true; }
        if (g_tetoSessao > 0.0 && v[j] >= g_tetoSessao - 0.5 && ag - g_ultTetoSondaMs < 1000.0 * g_ctl.tetoSondaS) {
            snprintf(why, wn, "teto da sessao %.0f/s: proxima sonda em %.0f s", g_tetoSessao, (1000.0 * g_ctl.tetoSondaS - (ag - g_ultTetoSondaMs)) / 1000.0); return true;
        }
        return false;
    };
    const char* org = "sem medida";
    const double p1 = Previsao(v[at - 1], fpsAgora, &org);
    int alvo = at - 1;
    if (bloqueado(alvo, det, sizeof det)) { Estado(strstr(det, "teto") ? "teto da sessao" : "backoff do degrau de cima", det); return; }
    if (p1 >= 0.0) {
        if (p1 > g_ctl.portao) {
            snprintf(det, sizeof det, "previsao em %.0f/s = %.2f > %.2f (%s)", v[at - 1], p1, g_ctl.portao, org);
            Estado("portao: nao cabe subir", det); return;
        }
        if (p1 <= g_ctl.salto)
            for (int j = at - 2; j >= 0; j--) {
                char w[160]; if (bloqueado(j, w, sizeof w)) break;
                const double pj = Previsao(v[j], fpsAgora);
                if (pj < 0.0 || pj > g_ctl.portao) break;
                alvo = j;
            }
    }
    const double de = RitmoFps(), pa = Previsao(v[alvo], fpsAgora, &org);
    if (g_tetoSessao > 0.0 && v[alvo] >= g_tetoSessao - 0.5) g_ultTetoSondaMs = ag;
    g_provaDe = de; g_provaAlvo = v[alvo];
    char m[200]; snprintf(m, sizeof m, "sonda: sobe a fonte inteira (~%.0f/s)", v[0]);
    AplicarNivel(v, alvo, dp, hz, m);
    g_fase = F_PROVA; g_provaAteMs = ag + 1000.0 * g_ctl.provaS; g_limpoNivelSeg = 0;
    if (pa >= 0.0)
        ::Log("guarda: SOBE (sonda) %.0f/s -> %.0f/s%s: previsao %.2f (%s; portao %.2f, salto %.2f) - prova de %.0f s",
              de, v[alvo], at - alvo > 1 ? " (SALTO: a cena pesada acabou)" : "", pa, org, g_ctl.portao, g_ctl.salto, g_ctl.provaS);
    else
        ::Log("guarda: SOBE (sonda) %.0f/s -> %.0f/s: sem medida de GPU, um degrau - prova de %.0f s", de, v[alvo], g_ctl.provaS);
    Estado("prova da sonda");
}
// Espera de alta resolucao ate `alvo` (ms QPC): timer + giro final de <= 0,25 ms.
inline void EsperarAteMs(double alvo) {
    double agora = Ms();
    if (!g_ritmoTimerTentou) {
        g_ritmoTimerTentou = true;
        g_ritmoTimer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
        if (!g_ritmoTimer) g_ritmoTimer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    }
    const double dorme = alvo - agora - 0.25;
    if (dorme > 0.05) {
        if (g_ritmoTimer) {
            LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(dorme * 10000.0);
            if (SetWaitableTimer(g_ritmoTimer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(g_ritmoTimer, (DWORD)dorme + 20);
            else Sleep((DWORD)dorme);
        } else Sleep((DWORD)dorme);
    }
    while (Ms() < alvo) YieldProcessor();
}
// A proxima devolucao: alvo = ultimo NOVO + N (na grade), na hora carimbo(alvo) - lead. false = nada a mirar ainda.
inline bool DegrauProxima(long long& alvo, double& R) {
    if (!Degrau::Ancorar()) return false;
    const int N = Degrau::g_N;
    const long long base = Degrau::g_idxNovo != Degrau::kNada ? Degrau::g_idxNovo : Degrau::g_idxUlt;
    if (base == Degrau::kNada) return false;
    alvo = base + N;
    if (Degrau::g_idxUlt != Degrau::kNada && alvo <= Degrau::g_idxUlt) alvo = Degrau::g_idxUlt + 1;
    const double tetoLead = ((double)N - 0.5) * Degrau::g_P;
    if (Degrau::g_lead < 0.0) Degrau::g_lead = ((double)N - 1.0) * Degrau::g_P;   // comeca CEDO: o erro cedo e o inequivoco
    if (Degrau::g_lead > tetoLead) Degrau::g_lead = tetoLead;
    R = Degrau::Tempo(alvo) - Degrau::g_lead;
    return true;
}
// O laco poe isto na espera dele (MsgWaitForMultipleObjects): com todos os buffers conosco, nada acordaria o laco na
// hora da devolucao. Timer de alta resolucao 1 ms antes; nullptr = nada pendente.
inline HANDLE DegrauDespertador() {
    if (!DegrauAtivo() || !Degrau::g_cap->DegrauCheio() || Ms() - Degrau::g_capTickMs > 200.0) return nullptr;   // captura parada (repouso etc.): nada de laco girando
    if (!g_dgTimerTentou) {
        g_dgTimerTentou = true;
        g_dgTimer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*HIGH_RESOLUTION, auto-reset*/, TIMER_ALL_ACCESS);
        if (!g_dgTimer) g_dgTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    if (!g_dgTimer) return nullptr;
    long long alvo = 0; double R = 0.0;
    double falta = 0.0;
    if (!Degrau::g_copiaPend && DegrauProxima(alvo, R)) falta = R - 1.0 - Ms();
    LARGE_INTEGER due; due.QuadPart = falta > 0.01 ? -(LONGLONG)(falta * 10000.0) : -1;
    if (!SetWaitableTimer(g_dgTimer, &due, 0, nullptr, nullptr, FALSE)) return nullptr;
    return g_dgTimer;
}
// O laco chama ANTES da espera do slot (via EsperarRitmo). Com todos os buffers conosco, espera a hora e devolve um.
inline void EsperarDegrau() {
    ScreenCapture* cap = Degrau::g_cap;
    if (!cap || !cap->DegrauCheio()) return;   // ha buffer livre (ou quadro esperando): a chegada vem sozinha
    const double t0 = Ms();
    long long alvo = 0; double R = t0;
    bool emDia = false;
    if (Degrau::g_copiaPend) { Degrau::g_copiaPend = false; }   // quadro-alvo copia: o proximo novo dentro do passo conta
    else if (DegrauProxima(alvo, R)) {
        if (R - t0 > (double)Degrau::g_N * Degrau::g_P + 2.0) R = t0;   // relogio estranho: nao espera
        if (R > t0) { EsperarAteMs(R); emDia = true; }
    }
    cap->DegrauLiberarUm();
    Degrau::g_libMs = Ms(); Degrau::g_alvo = alvo; Degrau::g_libAberta = true; Degrau::g_libEmDia = emDia;
    Degrau::g_jLib++; if (emDia) Degrau::g_jEmDia++;
    const double esp = Degrau::g_libMs - t0;
    if (esp > 0.0) { g_ritmoEsperaSoma += esp; g_ritmoEsperas++; }
}

// O laco chama ANTES da espera do slot, so com fonte de captura. Espera ate o ultimo aceito + intervalo.
inline void EsperarRitmo() {
    if (DegrauAtivo()) { EsperarDegrau(); return; }
    if (!g_ritmo || g_ritmoIntervMs <= 0.0 || g_ultAceitoMs <= 0.0) return;
    const double alvo = g_ultAceitoMs + g_ritmoIntervMs;
    double agora = Ms();
    if (alvo <= agora || alvo - agora > g_ritmoIntervMs + 1.0) return;   // ja passou / relogio estranho: nao espera
    const double t0 = agora;
    if (!g_ritmoTimerTentou) {
        g_ritmoTimerTentou = true;
        g_ritmoTimer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
        if (!g_ritmoTimer) g_ritmoTimer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    }
    const double dorme = alvo - agora - 0.25;   // o resto (<= 0,25 ms) em giro curto
    if (dorme > 0.05) {
        if (g_ritmoTimer) {
            LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(dorme * 10000.0);
            if (SetWaitableTimer(g_ritmoTimer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(g_ritmoTimer, (DWORD)dorme + 20);
            else Sleep((DWORD)dorme);
        } else Sleep((DWORD)dorme);
    }
    while ((agora = Ms()) < alvo) YieldProcessor();
    g_ritmoEsperaSoma += agora - t0; g_ritmoEsperas++;
}

// Log: o do main (static, declarado no screen_capture.h, que vem antes deste include)

inline bool Ligado() { return g_medir == 1; }

// Le as chaves (chamado uma vez pelo main, que tem o IniGet).
inline void Configurar(bool medir, int voo, int escalaAuto = 0, int guardaPct = 75, int ritmo = 1, int degrau = 1, int guarda = 1) {
    g_degrauChave = degrau ? 1 : 0; g_guardaChave = guarda ? 1 : 0;
    g_ritmoChave = ritmo ? 1 : 0;
    // sec. 23: a nossa parte deixou de ser gatilho - 0 (padrao) = desligada; > 0 conta como mais um sintoma
    guardaPct = guardaPct <= 0 ? 0 : guardaPct < 50 ? 50 : guardaPct > 95 ? 95 : guardaPct;
    kGuardaLim = guardaPct / 100.0; kGuardaSolta = kGuardaLim > 0.0 ? kGuardaLim - 0.05 : 0.0;
    g_escalaAuto = escalaAuto ? 1 : 0;
    g_medir = medir ? 1 : 0;
    g_voo = voo < 1 ? 1 : voo > 3 ? 3 : voo;
    for (int i = 0; i < kE; i++) { g_envMs[i].store(0.0); g_envN[i].store(0); }
}

inline void SoltarGpu() {
    for (auto& s : g_gs) { if (s.dj) { s.dj->Release(); s.dj = nullptr; } for (auto*& q : s.ts) if (q) { q->Release(); q = nullptr; } s.emVoo = s.valido = false; }
    g_gdev = nullptr;
}
inline bool MontarGpu(ID3D11Device* dev) {
    if (g_gpuFalhou || !dev) return false;
    if (g_gdev == dev) return true;
    SoltarGpu();
    D3D11_QUERY_DESC q{};
    for (auto& s : g_gs) {
        q.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; if (FAILED(dev->CreateQuery(&q, &s.dj))) { g_gpuFalhou = true; SoltarGpu(); return false; }
        q.Query = D3D11_QUERY_TIMESTAMP;
        for (auto*& t : s.ts) if (FAILED(dev->CreateQuery(&q, &t))) { g_gpuFalhou = true; SoltarGpu(); return false; }
    }
    g_gdev = dev; g_resolverDe = g_id + 1; g_antId = 0;
    return true;
}

// Resolve em ORDEM os quadros cujas queries ja voltaram (sem esperar: DONOTFLUSH).
inline void Resolver(ID3D11DeviceContext* c) {
    if (!g_gdev || !c) return;
    for (int guarda = 0; guarda < kR && g_resolverDe <= g_id; guarda++) {
        GpuSlot& s = g_gs[g_resolverDe % kR];
        if (!s.emVoo || s.id != g_resolverDe) { g_resolverDe++; continue; }   // ja reciclado/cancelado
        if (!s.valido) { s.emVoo = false; g_resolverDe++; continue; }
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{}; UINT64 t[4] = {};
        if (c->GetData(s.dj, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;   // ainda na fila: tenta na proxima volta
        bool ok = !dj.Disjoint && dj.Frequency;
        for (int k = 0; k < 4 && ok; k++) ok = c->GetData(s.ts[k], &t[k], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
        s.emVoo = false; g_resolverDe++;
        if (!ok || t[1] < t[0] || t[2] < t[1] || t[3] < t[2]) { g_gpuPerdidos++; g_antId = 0; continue; }
        const double k = 1000.0 / (double)dj.Frequency;
        g_s[S_GPRE].add((double)(t[1] - t[0]) * k);
        g_s[S_GREDE].add((double)(t[2] - t[1]) * k);
        g_s[S_GPOS].add((double)(t[3] - t[2]) * k);
        g_somaAD += (double)(t[3] - t[0]) * k; g_somaPrePos += (double)(t[1] - t[0]) * k + (double)(t[3] - t[2]) * k;
        if (g_antId && g_antId + 1 == s.id && t[0] >= g_antD) {
            const double vao = (double)(t[0] - g_antD) * k;
            g_s[S_GVAO].add(vao);
            g_s[S_GQUADRO].add((double)(t[0] - g_antA) * k);
            // serial do quadro anterior: o que nao se sobrepos a rede dele = pre + pos + o vao que veio depois
            g_s[S_GSERIAL].add(vao + (double)(t[1] - t[0]) * k + (double)(t[3] - t[2]) * k);
        }
        g_antId = s.id; g_antA = t[0]; g_antD = t[3];
        g_gpuResolvidos++;
    }
}

// ------------------------------------------------------------------ o laco chama, na ordem
inline void Cancelar(ID3D11DeviceContext* c);
inline void SlotIni(ID3D11DeviceContext* c) { if (!Ligado()) return; if (g_q.ativo) Cancelar(c); g_q = Quadro{}; for (double& x : g_q.t) x = 0.0; g_q.t[E_SLOT0] = Ms(); }
// Logo depois da espera do slot: marca A na GPU (inicio do quadro).
inline void SlotFim(ID3D11Device* dev, ID3D11DeviceContext* c) {
    if (!Ligado()) return;
    g_q.t[E_SLOT1] = Ms();
    if (!MontarGpu(dev)) return;
    Resolver(c);
    const uint64_t id = ++g_id;
    GpuSlot& s = g_gs[id % kR];
    if (s.emVoo) { g_gpuPerdidos++; s.emVoo = false; if (g_resolverDe <= s.id) g_resolverDe = s.id + 1; }
    s.id = id; s.valido = true; s.emVoo = true;
    c->Begin(s.dj); c->End(s.ts[0]);
    g_q.gpuSlot = (int)(id % kR); g_q.ativo = true;
}
inline void Marca(Etapa e) { if (Ligado() && g_q.ativo) g_q.t[e] = Ms(); }
// B (antes do Present) / C (depois) / D (depois do AposPresent)
inline void MarcaGpu(ID3D11DeviceContext* c, int k) {
    if (!Ligado() || !g_q.ativo || g_q.gpuSlot < 0 || !c) return;
    GpuSlot& s = g_gs[g_q.gpuSlot];
    if (s.emVoo && s.ts[k]) c->End(s.ts[k]);
}
inline void Ponte(uint64_t n) {
    if (!Ligado() || !n) return;
    const int i = (int)(n % kE);
    g_envN[i].store(0, std::memory_order_relaxed);
    g_envMs[i].store(Ms(), std::memory_order_relaxed);
    g_envN[i].store(n, std::memory_order_release);
    if (g_q.ativo) g_q.ponteN = n;
}

inline void Relatorio();

// A volta nao apresentou quadro novo: fecha as queries (o par Begin/End tem de fechar) e esquece.
inline void Cancelar(ID3D11DeviceContext* c) {
    if (!Ligado() || !g_q.ativo) return;
    if (g_q.gpuSlot >= 0 && c) { GpuSlot& s = g_gs[g_q.gpuSlot]; if (s.emVoo) { for (int k = 1; k < 4; k++) c->End(s.ts[k]); c->End(s.dj); s.valido = false; } }
    g_q.ativo = false;
}
// Fim do quadro que foi a rede e a ponte: `carimbo` = o carimbo CRU dele.
inline void FimQuadro(ID3D11DeviceContext* c, double carimbo) {
    if (!Ligado() || !g_q.ativo) return;
    if (g_q.gpuSlot >= 0 && c) { GpuSlot& s = g_gs[g_q.gpuSlot]; if (s.emVoo) c->End(s.dj); }
    Quadro& q = g_q; q.ativo = false; q.carimbo = carimbo;
    auto d = [&](Etapa a, Etapa b) -> double { return (q.t[a] > 0.0 && q.t[b] > 0.0 && q.t[b] >= q.t[a]) ? q.t[b] - q.t[a] : -1.0; };
    auto put = [&](S s, double v) { if (v >= 0.0) g_s[s].add(v); };
    if (carimbo > 0.0 && q.t[E_TICK] > 0.0) put(S_IDADE, q.t[E_TICK] - carimbo);
    put(S_SLOT, d(E_SLOT0, E_SLOT1));
    put(S_TICK, d(E_SLOT1, E_TICK));
    put(S_DEDUP, d(E_TICK, E_DEDUP));
    put(S_DESENHO, d(q.t[E_DEDUP] > 0.0 ? E_DEDUP : E_TICK, E_DESENHO));
    put(S_PRESENT, d(E_DESENHO, E_PRESENT));
    put(S_LER, d(E_PRESENT, E_LER));
    put(S_ENVIAR, d(E_LER, E_ENVIAR));
    put(S_ACORDAR, d(E_ENVIAR, E_ACORDAR));
    put(S_APOS, d(q.t[E_ACORDAR] > 0.0 ? E_ACORDAR : q.t[E_ENVIAR] > 0.0 ? E_ENVIAR : E_PRESENT, E_APOS));
    put(S_VOLTA, d(E_SLOT0, E_APOS));
    // sec. 22: FREIO DE EMERGENCIA - a volta deste quadro travou (STALL) com a placa inteira quase cheia
    if (kFreioMs > 0.0) { const double v = d(E_SLOT0, E_APOS); if (v > kFreioMs) Freio(v, PlacaCrua()); }   // sec. 23: STALL e sintoma por si
    g_ultAceitoMs = q.t[E_SLOT0];   // RITMO: o proximo quadro so e pedido intervalo depois deste
    if (g_ultSlot0 > 0.0 && q.t[E_SLOT0] > g_ultSlot0) put(S_INTERV, q.t[E_SLOT0] - g_ultSlot0);
    g_ultSlot0 = q.t[E_SLOT0];
    // ORDEM na entrada da rede
    if (carimbo > 0.0) {
        if (g_ultCarRede > 0.0 && carimbo < g_ultCarRede - 0.001) {
            g_regRede++; g_regRedeTot++;
            if (g_regRedeTot <= 20) ::Log("esteira: INVERSAO NA REDE - quadro com carimbo %.3f ms depois de um com %.3f ms (%.3f ms para tras)", carimbo, g_ultCarRede, g_ultCarRede - carimbo);
        }
        g_ultCarRede = carimbo;
    }
    g_nJanela++;
    Relatorio();
}

// ------------------------------------------------------------------ a thread do apresentador chama, logo depois do Present
inline void Tela(uint64_t n, double carimbo, double recebidoMs, double presentMs) {
    if (!Ligado() || !n) return;
    g_telaN.fetch_add(1, std::memory_order_relaxed); g_telaNTot.fetch_add(1, std::memory_order_relaxed);
    if (g_ultNTela && n < g_ultNTela) {
        g_regNTot.fetch_add(1, std::memory_order_relaxed);
        if (g_regNTot.load() <= 20) ::Log("esteira: INVERSAO NA TELA - quadro %llu da ponte depois do %llu", (unsigned long long)n, (unsigned long long)g_ultNTela);
    }
    if (carimbo > 0.0) {
        if (g_ultCarTela > 0.0 && carimbo < g_ultCarTela - 0.001) {
            g_regTela.fetch_add(1, std::memory_order_relaxed); g_regTelaTot.fetch_add(1, std::memory_order_relaxed);
            if (g_regTelaTot.load() <= 20) ::Log("esteira: INVERSAO NA TELA - quadro %llu com carimbo %.3f ms depois de um com %.3f ms (%.3f ms para tras)", (unsigned long long)n, carimbo, g_ultCarTela, g_ultCarTela - carimbo);
        } else if (g_ultCarTela > 0.0 && fabs(carimbo - g_ultCarTela) <= 0.001) g_repTelaTot.fetch_add(1, std::memory_order_relaxed);
        g_ultCarTela = carimbo;
    } else g_telaSemCar.fetch_add(1, std::memory_order_relaxed);
    g_ultNTela = n;
    const int i = (int)(n % kE);
    double env = 0.0;
    if (g_envN[i].load(std::memory_order_acquire) == n) env = g_envMs[i].load(std::memory_order_relaxed);
    AcquireSRWLockExclusive(&g_lkTela);
    if (env > 0.0 && recebidoMs >= env) g_s[S_TFILA].add(recebidoMs - env);
    if (presentMs >= recebidoMs) g_s[S_TCAD].add(presentMs - recebidoMs);
    if (carimbo > 0.0 && presentMs > carimbo) g_s[S_TCARIMBO].add(presentMs - carimbo);
    ReleaseSRWLockExclusive(&g_lkTela);
}

inline void Relatorio() {
    const double agora = Ms();
    if (g_relIni <= 0.0) { g_relIni = agora; if (g_iniCtlMs <= 0.0) g_iniCtlMs = agora; return; }
    const double dur = agora - g_relIni;
    if (dur < 1000.0) return;
    auto pp = [](const Serie& s) { char b[40]; if (s.v.empty()) snprintf(b, sizeof b, "-"); else snprintf(b, sizeof b, "%.2f/%.2f", s.pct(0.5), s.pct(0.95)); return std::string(b); };
    AcquireSRWLockExclusive(&g_lkTela);
    const std::string tf = pp(g_s[S_TFILA]), tc = pp(g_s[S_TCAD]), tcar = pp(g_s[S_TCARIMBO]);
    const size_t nTela = g_s[S_TCAD].v.size();
    g_s[S_TFILA].reset(); g_s[S_TCAD].reset(); g_s[S_TCARIMBO].reset();
    ReleaseSRWLockExclusive(&g_lkTela);
    const double qps = 1000.0 * (double)g_nJanela / dur;
    const double gq50 = g_s[S_GQUADRO].pct(0.5), gr50 = g_s[S_GREDE].pct(0.5);
    // OCUPACAO + GUARDA DE FOLGA (ver o bloco da guarda, acima)
    // sec. 20: pre/pos entram pelo TRABALHO, nao pelo tempo de parede. Com a fonte a 60/s (YouTube) o `pos` (C->D) mediu
    // 10-16 ms por quadro com a placa a 20-40% no nvidia-smi: a fila D3D11 fica parada esperando (ponte/apresentador/outros
    // contextos), e a soma de pre+pos dava "ocupacao" de 93-96% a 60 q/s - a guarda disparava e o ritmo derrubava o video
    // para 47/s. Trabalho = p05 de pre + p05 de pos na janela (o quadro que nao esperou), com teto de kPrePosTeto: em
    // saturacao (testufo, sec. 18) o trabalho real fora do pacote foi 0,55 ms; acima de 1,5 ms e espera, nao trabalho.
    constexpr double kPrePosTeto = 1.5;
    const double prePosMedio = g_gpuResolvidos ? g_somaPrePos / (double)g_gpuResolvidos : 0.0;   // parede (com esperas), so para o log
    double prePosTrab = (g_s[S_GPRE].v.empty() ? 0.0 : g_s[S_GPRE].pct(0.05)) + (g_s[S_GPOS].v.empty() ? 0.0 : g_s[S_GPOS].pct(0.05));
    if (prePosTrab > kPrePosTeto) prePosTrab = kPrePosTeto;
    const double ocup = (g_pacoteMs > 0.0) ? (double)g_nJanela * (g_pacoteMs + prePosTrab) / dur : -1.0;
    const double ocupParede = (g_pacoteMs > 0.0) ? ((double)g_nJanela * g_pacoteMs + g_somaPrePos) / dur : -1.0;   // a medida da sec. 18
    const double ocupJan = g_somaAD / dur;
    g_ultOcup = ocup; g_ultOcupJan = ocupJan; g_ultQps = qps; g_ultRelMs = agora;
    const double custoQ = g_pacoteMs + prePosTrab;
    const double placa = Placa();   // RESET-2109 (sec. 21): a placa inteira (NVML), -1 = sem
    const double qpsP = 1000.0 * (double)g_nJanela / dur;
    const double sustPlaca = (placa > 0.0 && qpsP > 1.0) ? qpsP * kPlacaLim / placa : 1e9;   // a mesma taxa escalada ao teto da placa (conservador: trata tudo como nosso)
    const double sustFolga = std::min(custoQ > 0.0 ? kGuardaLim * 1000.0 / custoQ : 0.0, sustPlaca);
    // (sec. 23: a entrada da guarda pela OCUPACAO saiu daqui - ver A GUARDA POR SINTOMAS, logo abaixo)
    // GUARDA COM RITMO (ver o bloco do ritmo, acima). O custo por quadro e suavizado; o intervalo e refeito 1x/s.
    const uint64_t perdTot = g_capPerdidosTot.load(std::memory_order_relaxed);
    const double perdS = 1000.0 * (double)(perdTot - g_ritmoPerdAnt) / dur; g_ritmoPerdAnt = perdTot;
    if (custoQ > 0.0) g_ritmoCustoMs = g_ritmoCustoMs > 0.0 ? 0.7 * g_ritmoCustoMs + 0.3 * custoQ : custoQ;
    const double fonteOcup = g_ritmoCustoMs > 0.0 ? (qps + perdS) * g_ritmoCustoMs / 1000.0 : -1.0;
    const double espMed = g_ritmoEsperas ? g_ritmoEsperaSoma / (double)g_ritmoEsperas : 0.0;
    const uint64_t nEsp = g_ritmoEsperas; g_ritmoEsperaSoma = 0.0; g_ritmoEsperas = 0;
    // DEGRAU (sec. 21): a janela de 1 s do que a captura viu (antes de qualquer decisao)
    const uint32_t dgLib = Degrau::g_jLib, dgEmDia = Degrau::g_jEmDia, dgAc = Degrau::g_jAcerto, dgCedo = Degrau::g_jCedo, dgTarde = Degrau::g_jTarde,
                   dgCop = Degrau::g_jCopias, dgEspN = Degrau::g_jEspN, dgEspOut = Degrau::g_jEspOutro, dgEspMax = Degrau::g_jEspOutroMax; (void)dgEspMax;
    Degrau::g_jLib = Degrau::g_jEmDia = Degrau::g_jAcerto = Degrau::g_jCedo = Degrau::g_jTarde = Degrau::g_jCopias = Degrau::g_jEspN = Degrau::g_jEspOutro = Degrau::g_jEspOutroMax = 0;
    const bool dgPode = g_degrauChave && Degrau::g_cap != nullptr && Degrau::Ancorar() && Degrau::GradeConfere();
    const double dgHz = Degrau::Hz();
    const bool ritmoPode = g_ritmoChave && g_guardaChave;
    // sec. 24: O CONTROLADOR DE CARGA (ver o bloco "O CONTROLADOR DE CARGA", acima). A ocupacao acima e medida e vai ao log;
    // o custo por quadro (custoQ) alimenta a PREVISAO por degrau (destino da descida e portao da subida), nunca uma descida.
    {
        const double ag = agora;
        const uint64_t atr = g_extAtrasados.load(std::memory_order_relaxed), tel = g_extTelaNovos.load(std::memory_order_relaxed);
        uint64_t dAtr = 0, dTel = 0;
        if (atr >= g_sAtrAnt && tel >= g_sTelaAnt) { dAtr = atr - g_sAtrAnt; dTel = tel - g_sTelaAnt; }
        g_sAtrAnt = atr; g_sTelaAnt = tel;
        // sec. 27: fonte esparsa (desktop parado: "quadro da fonte 587 ms" no log do dono) nao gera sintoma de atrasados
        const bool sAtr = dTel >= 20 && qps >= 15.0 && dAtr >= (uint64_t)g_ctl.atrMin && (double)dAtr > g_ctl.atrPct / 100.0 * (double)dTel;
        const double idade50 = g_s[S_IDADE].v.empty() ? 0.0 : g_s[S_IDADE].pct(0.5);
        const double fonteAgora = qps + perdS;
        const double ivFonte = DegrauAtivo() ? (qps > 1.0 ? 1000.0 / qps : 0.0) : (fonteAgora > 1.0 ? 1000.0 / fonteAgora : 0.0);
        const bool filaVale = ivFonte > 0.0 && (!g_ritmo || DegrauAtivo());
        // fila: acima de guarda_fila_quadros x o quadro da fonte, OU subindo guarda_fila_subindo janelas seguidas (GCC: a fila
        // avisa antes da perda) - cada passo > 5% do quadro da fonte e ja acima de meio quadro
        g_idadeHist[g_idadeN % 12] = idade50; g_idadeN++;
        bool subindo = false;
        if (filaVale && g_ctl.filaSubindo > 0 && g_idadeN > g_ctl.filaSubindo && idade50 > 0.5 * ivFonte) {
            subindo = true;
            for (int i = 0; i < g_ctl.filaSubindo && subindo; i++)
                if (!(g_idadeHist[(g_idadeN - 1 - i) % 12] > g_idadeHist[(g_idadeN - 2 - i) % 12] + 0.05 * ivFonte)) subindo = false;
        }
        const bool sFila = filaVale && (idade50 > g_ctl.filaQuadros * ivFonte || subindo);
        const bool sPlaca = placa >= 0.0 && kPlacaLim > 0.0 && placa > kPlacaLim;
        const bool sNossa = kGuardaLim > 0.0 && ocup >= 0.0 && ocup > kGuardaLim;
        const bool sStall = kFreioMs > 0.0 && g_jVoltaMax > kFreioMs;
        g_jVoltaMax = 0.0;
        if (!g_ritmo && qps > 1.0) g_fonteLivreFps = fonteAgora;
        if (g_ritmo && g_dgN >= 2 && !dgPode) {
            ::Log("guarda: degrau desligado nesta hora (captura fora da FILA do WGC ou grade do DWM %.2f Hz != monitor da captura %.2f Hz) - segue em ritmo continuo", dgHz, Degrau::g_capHz);
            g_dgN = 0;
        }
        double v[40]; const int n = Niveis(v, 40, dgPode, dgHz); const int at = NivelAtual(v, n);
        const double fpsAgora = g_ritmo ? RitmoFps() : (qps > 1.0 ? qps : v[0]);
        // o tempo de GPU desta janela: historico (p90/mediana) e o custo MEDIDO neste degrau (so depois de assentar)
        if (custoQ > 0.0) { g_custoHist[g_custoI] = custoQ; g_custoI = (g_custoI + 1) % 30; if (g_custoN < 30) g_custoN++; }
        if (custoQ > 0.0 && ag - g_ultTrocaMs >= 1000.0 * (g_ctl.trocaMinS + 1.0)) GuardarCustoNivel(g_ritmo ? RitmoFps() : v[0], custoQ);
        g_outrosAgora = (placa >= 0.0 && ocup >= 0.0) ? std::min(0.5, std::max(0.0, placa - ocup)) : 0.0;
        const double prevAqui = Previsao(fpsAgora, fpsAgora);
        const bool carga = CargaReal(fpsAgora, placa) || sPlaca;   // sec. 27: prova de que a GPU e o gargalo
        const bool ruim = sAtr || sFila || sNossa || (sStall && prevAqui > g_ctl.prevPico);
        g_jRuim[g_jI % 10] = ruim; g_jPlaca[g_jI % 10] = sPlaca; g_jI++;
        int nR = 0, nP = 0;
        for (int i = 0; i < g_ctl.n && i < g_jI && i < 10; i++) { const int k = (g_jI - 1 - i) % 10; nR += g_jRuim[k] ? 1 : 0; nP += g_jPlaca[k] ? 1 : 0; }
        if (!g_ritmo && g_fase != F_PROVA) g_fase = F_LIVRE;
        if (g_ritmo && g_fase == F_LIVRE) g_fase = F_NORMAL;
        if (g_fase == F_HOLD && ag >= g_holdAteMs) g_fase = g_ritmo ? F_NORMAL : F_LIVRE;
        Estado(g_ritmo ? "estavel" : "livre");
        char m2[400];
        auto motivoSint = [&](const char* pre) {
            snprintf(m2, sizeof m2, "%s%s%s%s%satrasados %llu de %llu, idade p50 %.1f ms (quadro da fonte %.1f ms), GPU p90 %.2f ms, previsao aqui %.2f, placa %.0f%% (teto %.0f%%)",
                     pre, sAtr ? "atrasados; " : "", sFila ? (subindo ? "fila da captura subindo; " : "fila da captura > 1 quadro; ") : "",
                     sNossa ? "nossa parte acima de esteira_guarda_pct; " : "", sPlaca ? "placa inteira acima do teto; " : "",
                     (unsigned long long)dAtr, (unsigned long long)dTel, idade50, ivFonte, CustoP90(), prevAqui, placa >= 0.0 ? 100.0 * placa : -1.0, 100.0 * kPlacaLim);
        };
        if (ritmoPode) {
            if (g_pedeTaxa > 0.0) {
                const double R = g_pedeTaxa; g_pedeTaxa = 0.0;
                // sec. 27: a memoria so vale se a fonte NAO cabe (previsao otimista da fonte inteira acima do portao); sem medida
                // ainda, espera a 1a medida em vez de comecar rebaixado
                const double pl = PrevisaoOtimista(v[0]);
                if (pl < 0.0 && ag - g_iniCtlMs < 10000.0) g_pedeTaxa = R;
                else if (pl >= 0.0 && pl <= g_ctl.portao) ::Log("guarda: memoria da configuracao (%.0f/s) IGNORADA - a fonte inteira cabe (previsao %.2f <= portao %.2f)", R, pl, g_ctl.portao);
                else if (!g_ritmo && (g_fonteLivreFps <= 0.0 || R < g_fonteLivreFps * 0.98)) {
                    AplicarTaxa(R, dgPode, dgHz); g_ultTrocaMs = ag; g_fase = F_NORMAL; g_limpoSeg = 0; g_limpoNivelSeg = 0;
                    ::Log("guarda: memoria da configuracao - comeca em %.0f/s (a moda dos segundos ESTAVEIS das sessoes iguais); a regra de subida vale desde ja (%.0f s limpos + portao %.2f)",
                          RitmoFps(), g_ctl.sonda1S, g_ctl.portao);
                }
            } else if (g_fase == F_PROVA && ruim && carga && ag - g_ultTrocaMs >= 2000.0) {   // sec. 27: a janela da troca e a sem carga nao reprovam
                motivoSint("prova da sonda: janela ruim com carga real de GPU - volta na hora; ");
                int dest = IndiceDe(v, n, g_provaDe); if (dest <= at) dest = at + 1;
                DescerPara(v, n, at, dest, dgPode, dgHz, g_ctl.holdS, m2);
            } else if (nR >= g_ctl.k && !carga) {
                // sec. 27: FONTE QUE CABE NUNCA E REBAIXADA - sintoma sem carga de GPU e da CPU/fonte, nao da placa
                static double avisou = 0.0;
                if (ag - avisou > 10000.0) {
                    avisou = ag;
                    motivoSint("sintoma SEM carga real de GPU (placa abaixo do freio e previsao otimista <= guarda_prev_pico) - NAO rebaixa; ");
                    ::Log("guarda: %s", m2);
                }
                g_limpoSeg = 0;
                Estado("sintoma sem carga de GPU: nao rebaixa");
            } else if (nR >= g_ctl.k) {
                if (ag - g_ultTrocaMs >= 1000.0 * g_ctl.trocaMinS) {
                    int dest = at + 1;
                    if (prevAqui >= 0.0) { dest = n - 1; for (int j = at + 1; j < n; j++) if (Previsao(v[j], fpsAgora) <= g_ctl.alvoDescida) { dest = j; break; } }
                    char pre[120]; snprintf(pre, sizeof pre, "%d de %d janelas ruins -> degrau com previsao <= %.2f; ", nR, g_ctl.n, g_ctl.alvoDescida);
                    motivoSint(pre);
                    DescerPara(v, n, at, dest, dgPode, dgHz, g_ctl.holdS, m2);
                } else Estado("sintoma: esperando o tempo minimo entre trocas");
            } else if (nP >= g_ctl.k) {
                if (ag - g_ultTrocaMs >= 1000.0 * g_ctl.trocaMinS) {
                    char pre[160]; snprintf(pre, sizeof pre, "REDE DE SEGURANCA: placa inteira (NVML, tempo com algum kernel) acima do teto aprendido em %d de %d janelas; ", nP, g_ctl.n);
                    motivoSint(pre);
                    DescerPara(v, n, at, at + 1, dgPode, dgHz, g_ctl.holdS, m2);
                } else Estado("rede NVML: esperando o tempo minimo entre trocas");
            } else if (ruim || sPlaca || sStall) {
                g_limpoSeg = 0; g_limpoNivelSeg = 0;
                Estado(ruim ? "janela ruim: conta no k de n" : sPlaca ? "placa inteira acima do teto: conta no k de n" : "STALL na janela");
            } else {
                g_limpoSeg++; g_limpoNivelSeg++;
                if (g_fase == F_PROVA && ag >= g_provaAteMs) {
                    ::Log("guarda: sonda PASSOU - %.0f s limpos em %.0f/s", g_ctl.provaS, g_provaAlvo);
                    if (g_tetoSessao > 0.0 && g_provaAlvo >= g_tetoSessao - 0.5) {
                        ::Log("guarda: teto da sessao %.0f/s RETIRADO - a sonda periodica passou (a fase pesada acabou)", g_tetoSessao);
                        g_tetoSessao = 0.0; if (NivelFalha* f = AchaNf(g_provaAlvo, false)) f->nFalhas = 0;
                    }
                    g_fase = g_ritmo ? F_NORMAL : F_LIVRE; g_limpoNivelSeg = 0;
                }
                if (g_fase == F_NORMAL && g_limpoNivelSeg >= (int)(g_ctl.backoffZeraS + 0.5))
                    if (NivelFalha* f = AchaNf(v[at], false)) if (f->esperaS > 0.0) {
                        ::Log("guarda: %.0f s limpos em %.0f/s - backoff deste degrau ZERADO (era %.0f s)", g_ctl.backoffZeraS, v[at], f->esperaS);
                        f->esperaS = 0.0; f->proxMs = 0.0;
                    }
                if (g_fase == F_PROVA) Estado("prova da sonda");
                else if (g_fase == F_HOLD) Estado("HOLD depois de descer");
                else if (g_ritmo && g_fase == F_NORMAL) TentarSubir(v, n, at, dgPode, dgHz, fpsAgora);
            }
            if (g_fase == F_HOLD && !strstr(g_motivo, "sintoma")) Estado("HOLD depois de descer");
        } else if (g_ritmo) SoltarPorFolga("folga de GPU desligada (painel/ini): sem ritmo nem degrau, so medida");
        if (!g_ritmo && g_fase != F_PROVA) g_fase = F_LIVRE;
        g_estavelAgora = ritmoPode && !ruim && !sStall && g_fase != F_HOLD && g_fase != F_PROVA &&
                         (g_ultDescidaMs <= 0.0 || ag - g_ultDescidaMs >= 1000.0 * g_ctl.memPosDescidaS);
        if (g_ritmo) g_guardaSegAtiva++;
        g_taxaAgora = g_ritmo ? RitmoFps() : 0.0;
        // LOG DE ESTADO: 1x/s no maximo, so quando fase/degrau/motivo mudam (a barra nao muda)
        {
            static const char* kFase[] = { "livre", "normal", "HOLD", "PROVA" };
            char e[240]; snprintf(e, sizeof e, "%s|%.0f|%s", kFase[g_fase], g_taxaAgora, g_motivo);
            if (strcmp(e, g_estadoLog) != 0) {
                snprintf(g_estadoLog, sizeof g_estadoLog, "%s", e);
                double va[40]; const int nn = Niveis(va, 40, dgPode, dgHz); const int aa = NivelAtual(va, nn);
                const double fa = g_ritmo ? RitmoFps() : fpsAgora;
                const char* org = "sem medida";
                const double pAcima = aa > 0 ? Previsao(va[aa - 1], fa, &org) : -1.0;
                ::Log("guarda: estado %s | degrau %s%.0f/s%s | previsao aqui %.2f, degrau acima %s%.0f/s = %.2f (%s) | GPU p90 %.2f ms, outros %.0f%%, clock %d/%d MHz | motivo: %s%s%s%s",
                      kFase[g_fase], g_ritmo ? "" : "livre ~", fa, DegrauAtivo() ? " (degrau)" : g_ritmo ? " (ritmo continuo)" : "",
                      Previsao(fa, fa), aa == 1 ? "livre ~" : "", aa > 0 ? va[aa - 1] : 0.0, pAcima, org,
                      CustoP90(), 100.0 * g_outrosAgora, g_clockMhz.load(), g_clockMaxMhz.load(), g_motivo, g_motivoDet[0] ? " (" : "", g_motivoDet, g_motivoDet[0] ? ")" : "");
            }
        }
    }
    if (!g_ritmo) g_dgN = 0;
    Degrau::g_N = (g_ritmo && g_dgN >= 2) ? g_dgN : 0;
    char estadoTxt[220] = "(sem informacao)";
    if (g_estadoFn) g_estadoFn(estadoTxt, sizeof estadoTxt);
    ::Log("esteira 1s | %.1f quadros/s (tela %.1f/s, voo %d) | CPU p50/p95 ms: idade(carimbo->tick) %s | slot %s | tick %s | dedup %s | laco+desenho %s | Present %s | ler %s | enviar %s | acordar %s | resto %s | volta %s | intervalo %s"
        " | GPU p50/p95 ms: pre %s | rede %s | pos %s | vao %s | quadro %s | SERIAL(pre+pos+vao) %s | teto pela rede %.0f/s"
        " | tela p50/p95 ms: ponte->apresentador %s | ->Present %s | carimbo->Present %s"
        " | ordem: inversoes rede %llu tela %llu (sessao rede %llu tela %llu, n %llu, carimbo repetido %llu, sem carimbo %llu) | gpu %llu resolvidos, %llu perdidos"
        " | ocupacao GPU %.0f%% (pacote %.2f ms x quadros + copias; trabalho pre+pos %.2f ms, parede %.2f ms = %.0f%%; janela A-D %.0f%%) | guarda %s (sustentavel com folga ~%.0f/s) | ritmo %s %.0f/s (esperas %llu, media %.2f ms, fonte %.0f%%)"
        " | placa inteira %.0f%% (NVML, teto %.0f%%) | degrau %s N=%d (devolucoes %u, no prazo %u: acerto %u cedo %u tarde %u; copias %u; espacamento N %u outros %u; lead %.2f ms; segurados %d) | estado: %s",
        qps, 1000.0 * (double)nTela / dur, g_voo,
        pp(g_s[S_IDADE]).c_str(), pp(g_s[S_SLOT]).c_str(), pp(g_s[S_TICK]).c_str(), pp(g_s[S_DEDUP]).c_str(), pp(g_s[S_DESENHO]).c_str(),
        pp(g_s[S_PRESENT]).c_str(), pp(g_s[S_LER]).c_str(), pp(g_s[S_ENVIAR]).c_str(), pp(g_s[S_ACORDAR]).c_str(), pp(g_s[S_APOS]).c_str(), pp(g_s[S_VOLTA]).c_str(), pp(g_s[S_INTERV]).c_str(),
        pp(g_s[S_GPRE]).c_str(), pp(g_s[S_GREDE]).c_str(), pp(g_s[S_GPOS]).c_str(), pp(g_s[S_GVAO]).c_str(), pp(g_s[S_GQUADRO]).c_str(), pp(g_s[S_GSERIAL]).c_str(),
        gr50 > 0.0 ? 1000.0 / gr50 : 0.0,
        tf.c_str(), tc.c_str(), tcar.c_str(),
        (unsigned long long)g_regRede, (unsigned long long)g_regTela.exchange(0),
        (unsigned long long)g_regRedeTot, (unsigned long long)g_regTelaTot.load(), (unsigned long long)g_regNTot.load(),
        (unsigned long long)g_repTelaTot.load(), (unsigned long long)g_telaSemCar.load(),
        (unsigned long long)g_gpuResolvidos, (unsigned long long)g_gpuPerdidos,
        ocup >= 0.0 ? 100.0 * ocup : -1.0, g_pacoteMs, prePosTrab, prePosMedio, ocupParede >= 0.0 ? 100.0 * ocupParede : -1.0, 100.0 * ocupJan, g_guarda ? "ATIVA" : "solta", sustFolga,
        !ritmoPode ? "desligado" : g_ritmo ? "ATIVO" : "solto", g_ritmo ? RitmoFps() : 0.0, (unsigned long long)nEsp, espMed, fonteOcup >= 0.0 ? 100.0 * fonteOcup : -1.0,
        placa >= 0.0 ? 100.0 * placa : -1.0, 100.0 * kPlacaLim, !g_degrauChave ? "desligado" : DegrauAtivo() ? "ATIVO" : "solto", g_dgN, dgLib, dgEmDia, dgAc, dgCedo, dgTarde, dgCop, dgEspN, dgEspOut, Degrau::g_lead, Degrau::g_cap ? Degrau::g_cap->DegrauSegurados() : 0, estadoTxt);
    g_somaAD = g_somaPrePos = 0.0;
    (void)gq50;
    for (int s = 0; s < S_N; s++) if (s != S_TFILA && s != S_TCAD && s != S_TCARIMBO) g_s[s].reset();
    g_regRede = 0; g_nJanela = 0; g_gpuResolvidos = 0; g_gpuPerdidos = 0; g_telaN.store(0);
    g_relIni = agora;
}

}  // namespace Esteira
