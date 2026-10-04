#pragma once
// =====================================================================================
//  saida_rapida.h -- o teto de espera de TODA espera de GPU enquanto o app esta fechando.
//
//  POR QUE EXISTE (20/09/2026, COD Vanguard do dono). O dono escolheu 2x no card e o app
//  se reabriu para aplicar. O processo velho levou **22 s** para morrer (log 19:32:34.947
//  "Encerrando", copia nova so as 19:32:57.6) e, nesse intervalo, o `nvlddmkm` resetou
//  SETE vezes (eventos 153, 19:32:37 a 19:32:53) e o jogo perdeu o device. Ou seja: o
//  fechamento do app com a placa cheia pelo jogo trava a GPU alem dos 2 s do TDR.
//
//  A soma era previsivel lendo o codigo: cada peca que morre espera a GPU com um teto
//  PROPRIO e generoso, pensado para o caso normal - 2 s na ponte, 2 s no ampliador, 2 s no
//  VME, 2 s no FI, 5 s na travessia entre placas, 500 ms por laco na captura, duas vezes.
//  Com a placa em TDR nenhuma dessas esperas volta cedo: elas se somam.
//
//  A REGRA, entao: enquanto `Saida::Saindo()`, nenhuma espera de GPU passa de 200 ms. Se
//  a placa nao respondeu em 200 ms no meio do encerramento, ela nao vai responder - e o
//  SO recolhe tudo (texturas, filas, devices) quando o processo sai, que e exatamente o
//  que ele faz de qualquer forma. Esperar mais nao protege ninguem: so segura a GPU.
//
//  Quem liga isto e o encerramento do main (`Saida::Comecou()`), uma linha antes do
//  "Encerrando". Nada muda em voo: fora do encerramento `Teto()` devolve o valor de
//  sempre, e o sinal e so lido, nunca escrito, pelas pecas.
// =====================================================================================
#include <windows.h>
#include <atomic>

namespace Saida {

    // Teto unico de qualquer espera de GPU durante o encerramento. 200 ms = o mesmo
    // tamanho de ordem do teto do Present do X-TDR (50 ms) vezes 4: da para a fila
    // esvaziar num quadro ruim, e nao chega perto dos 2 s do TDR do Windows.
    constexpr DWORD kTetoMs = 200;

    inline std::atomic<bool> g_saindo{ false };
    // Alguma thread foi ABANDONADA (nao saiu no prazo e o processo vai morrer com ela viva).
    // A partir daqui nada mais pode ser liberado: a thread orfa ainda le texturas e devices.
    // Quem ve isto e o porteiro do encerramento, que sai na hora em vez de continuar soltando.
    inline std::atomic<bool> g_abandonou{ false };

    inline bool  Saindo() { return g_saindo.load(std::memory_order_relaxed); }
    inline void  Comecou() { g_saindo.store(true, std::memory_order_relaxed); }
    inline bool  Abandonou() { return g_abandonou.load(std::memory_order_relaxed); }
    inline void  Abandonei() { g_abandonou.store(true, std::memory_order_relaxed); }

    // O teto que a peca deve usar: o dela, ou 200 ms se ja estamos fechando.
    inline DWORD Teto(DWORD normal) { return Saindo() ? (normal < kTetoMs ? normal : kTetoMs) : normal; }

    // Para as esperas escritas como "N voltas de Sleep(1)" (a captura): quantas voltas.
    inline int   Voltas(int normais) { return Saindo() ? (normais < (int)kTetoMs ? normais : (int)kTetoMs) : normais; }

}
