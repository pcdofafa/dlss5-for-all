// Teste do ponte_seh.h (I10): provoca violacao de acesso e instrucao ilegal DENTRO da
// guarda e confere que o controle volta com o codigo certo e o endereco preenchido; que uma
// excecao C++ NAO e roubada; que o caminho normal nao paga nada; que a guarda aninhada
// devolve ao nivel certo; e que a guarda e desarmada ao sair.
//
// Nao ha como validar a rede de seguranca rodando o jogo (e justamente o que nao se pode
// derrubar), entao este teste e a prova de que o mecanismo funciona. O que ele NAO cobre:
// ficar inerte de verdade, que depende do estado da DLL dentro do processo do jogo.
//
//   g++ -std=c++17 -O2 ponte_seh_test.cpp -o ponte_seh_test.exe && ./ponte_seh_test.exe
#include <cstdio>
#include <stdexcept>
#include "../ponte_seh.h"

static int g_sink = 0;
__attribute__((noinline)) static void Boom(volatile int* p) { g_sink = *p; }

struct Resultado { DWORD code; void* addr; };

static Resultado Guardado(void (*fn)())
{
    PtGuarda g;
    if (PT_GUARDA_ARMAR(g)) return Resultado{ g.code, g.addr };
    fn();
    PtGuardaDesarmar(&g);
    return Resultado{ 0, nullptr };
}

static void FazAv()    { Boom(reinterpret_cast<volatile int*>(0x10)); }
static void FazUd2()   { __builtin_trap(); }
static void FazThrow() { throw std::runtime_error("cpp"); }
static void FazNada()  { g_sink++; }

// Guarda dentro de guarda: o estouro tem de voltar para a guarda de DENTRO, e a de fora
// tem de continuar armada e util depois disso.
static int Aninhado()
{
    int passos = 0;
    PtGuarda fora;
    if (PT_GUARDA_ARMAR(fora)) return -1;   // nao deve cair aqui
    {
        PtGuarda dentro;
        if (PT_GUARDA_ARMAR(dentro)) passos = dentro.code == EXCEPTION_ACCESS_VIOLATION ? 1 : -2;
        else { FazAv(); passos = -3; }
    }
    if (passos == 1)   // a de fora ainda vale: provoca de novo e confere
    {
        PtGuarda dentro2;
        if (PT_GUARDA_ARMAR(dentro2)) passos = dentro2.code == EXCEPTION_ILLEGAL_INSTRUCTION ? 2 : -4;
        else { FazUd2(); passos = -5; }
    }
    PtGuardaDesarmar(&fora);
    return passos;
}

int main()
{
    int falhas = 0;
    Resultado r = Guardado(FazAv);
    printf("AV        -> 0x%08lX addr=%p %s\n", (unsigned long)r.code, r.addr,
           (r.code == EXCEPTION_ACCESS_VIOLATION && r.addr != nullptr) ? "ok" : "FALHOU");
    falhas += !(r.code == EXCEPTION_ACCESS_VIOLATION && r.addr != nullptr);

    r = Guardado(FazUd2);
    printf("UD2       -> 0x%08lX %s\n", (unsigned long)r.code,
           r.code == EXCEPTION_ILLEGAL_INSTRUCTION ? "ok" : "FALHOU");
    falhas += r.code != EXCEPTION_ILLEGAL_INSTRUCTION;

    r = Guardado(FazNada);
    printf("normal    -> 0x%08lX %s\n", (unsigned long)r.code, r.code == 0 ? "ok" : "FALHOU");
    falhas += r.code != 0;

    bool pegou = false;
    try { Guardado(FazThrow); } catch (const std::exception&) { pegou = true; }
    printf("throw C++ -> %s\n", pegou ? "chegou ao catch (ok)" : "FALHOU");
    falhas += !pegou;

    const int an = Aninhado();
    printf("aninhado  -> %d %s\n", an, an == 2 ? "ok" : "FALHOU");
    falhas += an != 2;

    printf("desarmada -> %s\n", t_ptGuarda == nullptr ? "ok" : "FALHOU");
    falhas += t_ptGuarda != nullptr;

    printf("%s\n", falhas == 0 ? "TUDO OK" : "HOUVE FALHA");
    return falhas != 0;
}
