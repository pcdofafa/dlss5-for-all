// teste-ritmo.cpp - prova que o limitador entrega a taxa pedida, SEM placa e SEM jogo.
//
// O Ritmo nao sabe o que e um quadro: ele so segura quem o chama ate a hora certa. Entao da
// para testar o limitador inteiro num laco vazio, e e o que este arquivo faz. Mede a taxa que
// saiu, a dispersao dos intervalos e o comportamento do freio do acumulador.
//
// Compilar:  g++ -O2 -std=c++17 teste-ritmo.cpp -o teste-ritmo.exe
#include "fpslimit_ritmo.h"
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace FpsLimit;

static double QpcMs() {
    static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
}

struct Resultado { double fpsMedido, tipicoMs, p99Ms, piorMs, cpuPct; int segurados, total; };

static Resultado Rodar(Ritmo& r, uint32_t fps, double segundos, double trabalhoMs = 0.0) {
    std::vector<double> intervalos;
    intervalos.reserve(4096);
    FILETIME c0{}, e0{}, k0{}, u0{}, c1{}, e1{}, k1{}, u1{};
    GetProcessTimes(GetCurrentProcess(), &c0, &e0, &k0, &u0);

    const double t0 = QpcMs();
    double anterior = t0;
    int segurados = 0, total = 0;
    while (QpcMs() - t0 < segundos * 1000.0) {
        // simula o custo do quadro do jogo, se pedido
        if (trabalhoMs > 0.0) { const double a = QpcMs(); while (QpcMs() - a < trabalhoMs) {} }
        if (r.Esperar(fps)) segurados++;
        const double agora = QpcMs();
        if (total > 0) intervalos.push_back(agora - anterior);
        anterior = agora; total++;
    }
    const double dur = QpcMs() - t0;
    GetProcessTimes(GetCurrentProcess(), &c1, &e1, &k1, &u1);
    auto ft = [](const FILETIME& f) { ULARGE_INTEGER u; u.LowPart = f.dwLowDateTime; u.HighPart = f.dwHighDateTime; return (double)u.QuadPart / 10000.0; };
    const double cpuMs = (ft(k1) - ft(k0)) + (ft(u1) - ft(u0));

    std::sort(intervalos.begin(), intervalos.end());
    Resultado R{};
    R.fpsMedido = total * 1000.0 / dur;
    R.tipicoMs  = intervalos.empty() ? 0 : intervalos[intervalos.size() / 2];
    R.p99Ms     = intervalos.empty() ? 0 : intervalos[(size_t)(intervalos.size() * 0.99)];
    R.piorMs    = intervalos.empty() ? 0 : intervalos.back();
    R.cpuPct    = cpuMs * 100.0 / dur;
    R.segurados = segurados; R.total = total;
    return R;
}

int main() {
    Ritmo r; r.Iniciar();
    printf("temporizador de alta resolucao: %s\n\n", r.TemporizadorFino() ? "SIM" : "NAO (cai no giro, custa mais nucleo)");

    printf("%-34s %9s %9s %9s %9s %8s\n", "condicao", "fps saiu", "tipico", "p99", "pior", "cpu");
    printf("%-34s %9s %9s %9s %9s %8s\n", "--------", "--------", "------", "---", "----", "---");

    struct Caso { const char* nome; uint32_t fps; double trabalho; };
    const Caso casos[] = {
        { "livre (sem limite)",              0,   0.5 },
        { "teto 30, quadro barato",          30,  0.5 },
        { "teto 45, quadro barato",          45,  0.5 },
        { "teto 60, quadro barato",          60,  0.5 },
        { "teto 120, quadro barato",         120, 0.5 },
        { "teto 60, quadro de 10 ms",        60,  10.0 },
        { "teto 120, quadro de 10 ms (nao da)", 120, 10.0 },
    };
    for (const auto& c : casos) {
        const Resultado R = Rodar(r, c.fps, 3.0, c.trabalho);
        printf("%-34s %9.1f %7.2fms %7.2fms %7.2fms %7.1f%%\n",
               c.nome, R.fpsMedido, R.tipicoMs, R.p99Ms, R.piorMs, R.cpuPct);
    }

    // FREIO DO ACUMULADOR: pausa longa no meio nao pode virar rajada de recuperacao.
    printf("\nfreio do acumulador (teto 60, pausa de 600 ms no meio):\n");
    {
        std::vector<double> iv; double anterior = QpcMs();
        const double t0 = QpcMs();
        int rajada = 0;
        while (QpcMs() - t0 < 2000.0) {
            const double t = QpcMs() - t0;
            if (t > 900.0 && t < 1500.0) { Sleep(600); }   // a pausa
            r.Esperar(60);
            const double agora = QpcMs();
            const double d = agora - anterior;
            if (anterior != t0) iv.push_back(d);
            if (d < 4.0) rajada++;                          // quadro liberado bem antes do periodo
            anterior = agora;
        }
        printf("  quadros liberados a menos de 4 ms um do outro: %d\n", rajada);
        printf("  (com freio funcionando isto fica baixo; sem freio, a pausa vira uma rajada)\n");
    }

    r.Encerrar();
    return 0;
}
