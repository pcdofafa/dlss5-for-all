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
// =====================================================================================
#pragma once
#include <windows.h>
#include <stdint.h>

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

    // Chamar UMA VEZ POR QUADRO, imediatamente antes de deixar o quadro seguir. `fps` 0 = sem
    // limite: sai na primeira linha, sem tocar em relogio nenhum.
    // Devolve true se de fato segurou o quadro (para o contador de prova do painel).
    bool Esperar(uint32_t fps) {
        if (fps == 0) { m_alvoQpc = 0; m_fpsAtual = 0; return false; }

        const double periodoMs = 1000.0 / (double)fps;

        // Trocou de valor no painel, ou primeiro quadro depois de ficar livre: comeca limpo.
        // Sem isto, mudar de 30 para 120 herdaria um alvo calculado com o periodo antigo e o
        // primeiro quadro sairia na hora errada.
        if (fps != m_fpsAtual || m_alvoQpc == 0) {
            m_fpsAtual = fps;
            m_alvoQpc = Agora() + MsParaQpc(periodoMs);
            return false;
        }

        const int64_t agora = Agora();
        const int64_t periodoQpc = MsParaQpc(periodoMs);

        // FREIO DO ACUMULADOR: atrasado demais, esquece o passado em vez de cobrar a divida.
        if (agora - m_alvoQpc > (int64_t)(kMaxAtrasoPeriodos * (double)periodoQpc)) {
            m_alvoQpc = agora + periodoQpc;
            return false;
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
        m_alvoQpc += periodoQpc;   // acumulado, nao "agora + periodo"
        return segurou;
    }

private:
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
};

} // namespace FpsLimit
