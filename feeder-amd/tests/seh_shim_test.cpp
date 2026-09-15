// Teste do feed_seh_shim.h: provoca um access violation e um illegal instruction dentro
// da guarda e confere que o controle volta com o codigo certo; depois confere que uma
// excecao C++ NAO e roubada pela guarda e que codigo sem guarda nao e afetado.
//   g++ -std=c++20 -O2 -I../src-amd seh_shim_test.cpp -o seh_shim_test.exe && ./seh_shim_test.exe
#include <cstdio>
#include <stdexcept>
#include "feed_seh_shim.h"

static int g_sink = 0;
__attribute__((noinline)) static void Boom(volatile int *p) { g_sink = *p; }

static DWORD Guarded(void (*fn)())
{
    FeedGuard guard;
    if (FEED_GUARD_ARM(guard)) return guard.code;
    fn();
    FeedGuardDisarm(&guard);
    return 0;
}

static void DoAv()      { Boom(reinterpret_cast<volatile int *>(0x10)); }
static void DoUd2()     { __builtin_trap(); }
static void DoThrow()   { throw std::runtime_error("cpp"); }
static void DoNothing() { g_sink++; }

int main()
{
    int fails = 0;
    DWORD c = Guarded(DoAv);
    printf("AV     -> 0x%08lX %s\n", c, c == EXCEPTION_ACCESS_VIOLATION ? "ok" : "FALHOU"); fails += c != EXCEPTION_ACCESS_VIOLATION;
    c = Guarded(DoUd2);
    printf("UD2    -> 0x%08lX %s\n", c, c == EXCEPTION_ILLEGAL_INSTRUCTION ? "ok" : "FALHOU"); fails += c != EXCEPTION_ILLEGAL_INSTRUCTION;
    c = Guarded(DoNothing);
    printf("normal -> 0x%08lX %s\n", c, c == 0 ? "ok" : "FALHOU"); fails += c != 0;
    bool caught = false;
    try { Guarded(DoThrow); } catch (const std::exception &) { caught = true; }
    printf("throw  -> %s\n", caught ? "chegou ao catch (ok)" : "FALHOU");
    fails += !caught;
    // guarda desarmada: uma segunda AV fora da guarda tem de continuar fatal (nao testamos
    // aqui, mataria o processo), mas conferimos que t_feed_guard voltou a nulo.
    printf("guarda residual: %s\n", t_feed_guard == nullptr ? "nenhuma (ok)" : "FALHOU");
    fails += t_feed_guard != nullptr;
    // repeticao: 1000 AVs seguidas para pegar vazamento de estado
    for (int i = 0; i < 1000; ++i) if (Guarded(DoAv) != EXCEPTION_ACCESS_VIOLATION) { fails++; break; }
    printf("1000x AV: %s\n", fails == 0 ? "ok" : "FALHOU");
    printf(fails == 0 ? "TUDO OK\n" : "%d FALHAS\n", fails);
    return fails;
}
