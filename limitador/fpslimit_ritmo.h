// =====================================================================================
//  fpslimit_ritmo.h - a espera. E so isto que o limitador faz: segurar o quadro ate a hora.
//
//  Roda no CAMINHO QUENTE do jogo, uma vez por quadro. As regras aqui sao severas por isso:
//  nao aloca, nao chama nada que possa bloquear em disco ou em rede, nao lanca excecao, e o
//  caminho de "sem limite" custa uma comparacao e sai.
//
//  DUAS DECISOES QUE DEFINEM A QUALIDADE, e as duas ja mordem quem escreve limitador:
//
//  1. O PRAZO E ACUMULADO, NAO RECALCULADO A CADA QUADRO. Se o alvo do proximo quadro fosse
//     "agora + periodo", todo atraso de um quadro empurraria o proximo, e a taxa media ficaria
//     sempre abaixo do pedido. Acumulando (alvo += periodo) o ritmo se mantem exato ao longo do
//     tempo. E o mesmo motivo pelo qual relogio de parede nao se acerta somando atrasos.
//
//  2. O ACUMULADOR PRECISA DE FREIO. Com acumulacao pura, uma pausa longa - a pessoa abriu o
//     menu, o jogo travou carregando, a janela foi minimizada - deixa o alvo muito no passado, e
//     ao voltar o limitador libera uma rajada de quadros seguidos tentando "recuperar o
//     atraso". Isso aparece como um solavanco feio exatamente no pior momento. Quando o atraso
//     passa de kMaxAtrasoPeriodos, o alvo e RESSINCRONIZADO para agora e o passado e esquecido:
//     o limitador nao tem divida a cobrar.
//
//  COMO ESPERA. Temporizador de alta resolucao do Windows para a maior parte do tempo, e uma
//  espera curta e ocupada so no finalzinho. Espera ocupada pura daria precisao melhor ao custo
//  de queimar um nucleo inteiro, o que num jogo que ja esta com a placa cheia e justamente o
//  que nao se quer. A margem de giro e pequena de proposito.
//
//  3. (N1, 20/09/2026) O ALVO PODE SER PRESO A REDE DO VBLANK. O acumulador acima mantem a
//     TAXA exata, mas nao a FASE: o alvo nasce onde o primeiro quadro calhou de cair e fica ali,
//     livre em relacao ao relogio que de fato poe imagem na tela. Do outro lado da nossa cadeia
//     o apresentador ancora o tique dele no vblank do DWM, e a diferenca entre os dois relogios
//     aparece como tremor da chegada (`j` na equacao do K-RITMO) - que infla a folga da fila e
//     a latencia junto. Com a ancora ligada o quadro e solto em t_k = vblank_0 + k*T + fase, o
//     mesmo relogio dos dois lados. E o que o Reflex faz pela DLSS-G, e a razao de ele ser
//     obrigatorio la. Ver fpslimit_shared.h, campos `ancora` e `fase_us`.
// =====================================================================================
#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <stdint.h>
#include <math.h>

namespace FpsLimit {

// Depois de quantos periodos de atraso o limitador desiste de recuperar e ressincroniza.
// Dois periodos e pouco (um engasgo normal ja passa disso); vinte seria meio segundo a 40 fps,
// tempo demais devendo. Quatro cobre engasgo comum e ressincroniza em qualquer pausa de
// verdade.
static const double kMaxAtrasoPeriodos = 4.0;

// Quanto tempo antes do prazo o temporizador devolve o controle, para o acerto finca ser feito
// girando. 0,4 ms cobre a granularidade tipica do temporizador de alta resolucao sem gastar
// nucleo: a 60 fps sao 2,4% de um nucleo, e a 120 fps 4,8%.
static const double kMargemGiroMs = 0.4;

// N1 - quanto o alvo pode DESLIZAR por quadro para entrar em fase com a rede do vblank. E o
// mesmo teto do apresentador (K-RITMO C1): 3% de meio intervalo. O ponto nao e chegar rapido,
// e nunca pular: um salto de fase aparece como um solavanco, que e justamente o que se conserta.
static const double kFracaoDeslize = 1.0 / 32.0;

// N1 - o periodo do limitador so se prende ao vblank quando cabe num numero INTEIRO deles com
// erro abaixo disto. 120 fps em 240 Hz = 2 vblanks exatos e entra; 45 em 100 Hz = 2,22 e nao
// entra - ali a quantizacao pioraria o que deveria consertar. Mesma regra do C2 do K-RITMO.
static const double kTolComensuravel = 0.05;

// N1 - de quanto em quanto tempo o vblank e relido do compositor. Um segundo, pelo mesmo motivo
// do apresentador: trocar de monitor ou de refresh e um gesto humano, nao um evento de quadro.
static const double kReleituraVblankMs = 1000.0;

class Ritmo {
public:
    void Iniciar() {
        QueryPerformanceFrequency(&m_freq);
        m_alvoQpc = 0;
        m_fpsAtual = 0;
        // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION existe do Windows 10 1803 em diante. Sem ele o
        // temporizador comum tem passo de ~15 ms, grosso demais para qualquer teto util, e a
        // espera cai inteira no giro - que funciona, mas custa nucleo. Por isso tentamos o bom
        // primeiro e guardamos se conseguimos.
        m_timer = CreateWaitableTimerExW(nullptr, nullptr,
                                         0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                         TIMER_ALL_ACCESS);
        m_timerFino = (m_timer != nullptr);
        if (!m_timer) m_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    }

    void Encerrar() {
        if (m_timer) { CloseHandle(m_timer); m_timer = nullptr; }
    }

    bool TemporizadorFino() const { return m_timerFino; }
    // N1 - a ancora de fato pegou? (so para o registro; a peca escreve isto uma vez no log)
    bool     Ancorada() const  { return m_ancoraOk; }
    unsigned VblanksPorQuadro() const { return m_nVblanks; }

    // Chamar UMA VEZ POR QUADRO, imediatamente antes de deixar o quadro seguir. `fps` 0 = sem
    // limite: sai na primeira linha, sem tocar em relogio nenhum.
    // `ancora`/`faseMs` sao o PEDIDO do app (N1): prender o alvo a rede do vblank, deslocada de
    // `faseMs`. Ignorados quando o periodo nao cabe num numero inteiro de vblanks.
    // Devolve true se de fato segurou o quadro (para o contador de prova do painel).
    bool Esperar(uint32_t fps, bool ancora = false, double faseMs = 0.0) {
        if (fps == 0) { m_alvoQpc = 0; m_fpsAtual = 0; m_ancoraOk = false; return false; }

        const double periodoMs = 1000.0 / (double)fps;

        // Trocou de valor no painel, ou primeiro quadro depois de ficar livre: comeca limpo.
        // Sem isto, mudar de 30 para 120 herdaria um alvo calculado com o periodo antigo e o
        // primeiro quadro sairia na hora errada.
        if (fps != m_fpsAtual || m_alvoQpc == 0) {
            m_fpsAtual = fps;
            m_alvoQpc = Agora() + MsParaQpc(periodoMs);
            m_ancoraOk = false; m_vblankQpc = 0; m_lidoQpc = 0;   // a rede nasce de novo com o periodo novo
            return false;
        }

        const int64_t agora = Agora();
        const int64_t periodoQpc = MsParaQpc(periodoMs);

        // FREIO DO ACUMULADOR: atrasado demais, esquece o passado em vez de cobrar a divida.
        if (agora - m_alvoQpc > (int64_t)(kMaxAtrasoPeriodos * (double)periodoQpc)) {
            m_alvoQpc = agora + periodoQpc;
            return false;
        }

        // ---- N1: a rede do vblank. Atualiza (no maximo 1x/s) e desliza o alvo para cima dela.
        AtualizarAncora(ancora, faseMs, periodoQpc);
        if (m_ancoraOk) {
            const int64_t T = m_periodoQpc;
            const int64_t base = m_vblankQpc + m_faseQpc;
            const double  k = (double)(m_alvoQpc - base) / (double)T;
            const int64_t alvoRede = base + (int64_t)llround(k) * T;
            int64_t erro = alvoRede - m_alvoQpc;
            const int64_t teto = (int64_t)(kFracaoDeslize * (double)T);
            if (erro >  teto) erro =  teto;
            if (erro < -teto) erro = -teto;
            m_alvoQpc += erro;
        }

        bool segurou = false;
        if (agora < m_alvoQpc) {
            segurou = true;
            const double faltaMs = QpcParaMs(m_alvoQpc - agora);
            if (faltaMs > kMargemGiroMs) DormirMs(faltaMs - kMargemGiroMs);
            // acerto fino girando. YieldProcessor avisa o processador de que este giro e uma
            // espera, o que deixa o outro fio logico do mesmo nucleo respirar.
            while (Agora() < m_alvoQpc) YieldProcessor();
        }
        // acumulado, nao "agora + periodo". Com a ancora, o passo e o da REDE (N vblanks
        // exatos): usar 1/fps aqui faria os dois relogios brigarem, e o deslize acima passaria
        // a vida corrigindo uma diferenca que ele mesmo reintroduz a cada quadro.
        m_alvoQpc += m_ancoraOk ? m_periodoQpc : periodoQpc;
        return segurou;
    }

private:
    /* ---- N1: a rede do vblank, lida do MESMO compositor que o apresentador le.
       Por que o DWM e nao um campo novo na memoria compartilhada: o QPC e o mesmo relogio nos
       dois processos e o DwmGetCompositionTimingInfo responde pelo compositor da sessao - ou
       seja, os dois lados leem a MESMA ancora sem nada atravessar. O app so precisa mandar o
       phi, que e a unica coisa que ele sabe e a peca nao (a folga `D` da fila). */
    typedef HRESULT (WINAPI* PFN_Timing)(HWND, DWM_TIMING_INFO*);
    void AtualizarAncora(bool pedida, double faseMs, int64_t periodoQpc) {
        if (!pedida) { m_ancoraOk = false; return; }
        m_faseQpc = MsParaQpc(faseMs);
        const int64_t agora = Agora();
        if (m_lidoQpc != 0 && agora - m_lidoQpc < MsParaQpc(kReleituraVblankMs)) return;
        m_lidoQpc = agora;
        if (!m_dwm) {
            // Em jogo de D3D a dwmapi.dll ja esta carregada; GetModuleHandle resolve sem tocar no
            // carregador. LoadLibrary so como reserva, e ele nao roda no DllMain (ver o dll.cpp).
            HMODULE h = GetModuleHandleW(L"dwmapi.dll");
            if (!h) h = LoadLibraryW(L"dwmapi.dll");
            if (h) m_dwm = (PFN_Timing)(void*)GetProcAddress(h, "DwmGetCompositionTimingInfo");
            if (!m_dwm) { m_ancoraOk = false; return; }
        }
        DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti);
        if (FAILED(m_dwm(nullptr, &ti)) || ti.qpcRefreshPeriod <= 0) { m_ancoraOk = false; return; }
        const int64_t vbl = (int64_t)ti.qpcRefreshPeriod;
        const int64_t nv = (int64_t)((double)periodoQpc / (double)vbl + 0.5);
        if (nv < 1) { m_ancoraOk = false; return; }
        const int64_t T = nv * vbl;
        if (fabs((double)T - (double)periodoQpc) > kTolComensuravel * (double)periodoQpc) {
            m_ancoraOk = false; return;   // nao e comensuravel: o acumulador livre de sempre
        }
        const int64_t novo = (int64_t)ti.qpcVBlank;
        if (!m_ancoraOk || m_vblankQpc == 0 || m_periodoQpc != T) {
            m_vblankQpc = novo;   // primeira ancora, ou a tela mudou: a rede nasce aqui
        } else {
            /* SEGUIR A DERIVA SEM TROCAR DE VBLANK. O relogio do compositor e o QPC nao andam
               exatamente juntos, e sem correcao a rede escorrega. Mas pular para OUTRO vblank do
               grupo de `nv` mudaria a folga com que o quadro chega ao apresentador em ate
               (nv-1) vblanks de uma vez - um salto, exatamente o que nao se quer. Entao so a
               parte FRACIONARIA do desvio (dentro de um vblank) e corrigida. */
            const int64_t d = novo - m_vblankQpc;
            m_vblankQpc += d - (int64_t)llround((double)d / (double)vbl) * vbl;
        }
        m_periodoQpc = T; m_nVblanks = (unsigned)nv; m_ancoraOk = true;
    }

    int64_t Agora() const { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart; }
    int64_t MsParaQpc(double ms) const { return (int64_t)(ms * (double)m_freq.QuadPart / 1000.0); }
    double  QpcParaMs(int64_t t) const { return (double)t * 1000.0 / (double)m_freq.QuadPart; }

    void DormirMs(double ms) {
        if (!m_timer) { Sleep((DWORD)(ms > 1.0 ? ms : 1.0)); return; }
        // Tempo relativo vai NEGATIVO, em unidades de 100 ns, que e como a API espera.
        LARGE_INTEGER devido; devido.QuadPart = -(LONGLONG)(ms * 10000.0);
        if (devido.QuadPart == 0) return;
        if (SetWaitableTimer(m_timer, &devido, 0, nullptr, nullptr, FALSE))
            WaitForSingleObject(m_timer, (DWORD)(ms + 50.0));   // teto, para nunca ficar preso aqui
    }

    LARGE_INTEGER m_freq{};
    int64_t       m_alvoQpc   = 0;
    uint32_t      m_fpsAtual  = 0;
    HANDLE        m_timer     = nullptr;
    bool          m_timerFino = false;
    // N1 - a rede do vblank
    PFN_Timing    m_dwm        = nullptr;
    int64_t       m_vblankQpc  = 0;   // vblank_0: onde a rede comeca
    int64_t       m_periodoQpc = 0;   // T = nv vblanks exatos
    int64_t       m_faseQpc    = 0;   // phi, pedido pelo app
    int64_t       m_lidoQpc    = 0;   // quando o compositor foi lido pela ultima vez
    unsigned      m_nVblanks   = 0;
    bool          m_ancoraOk   = false;
};

} // namespace FpsLimit
