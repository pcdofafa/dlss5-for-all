// feed_seh_shim.h -- substituto do __try/__except para o build MinGW da variante AMD.
//
// O g++ nao aceita SEH estruturado (__try/__except). O Feeder usa cinco desses blocos
// para transformar um access violation dentro do NGX (ou dentro do add-on que o
// detoura) num "disable" limpo em vez de derrubar o jogo. Aqui isso e emulado com um
// Vectored Exception Handler + longjmp:
//
//   FeedGuard guard;
//   if (FEED_GUARD_ARM(guard)) { /* caminho da excecao: guard.code tem o codigo */ }
//   else { /* codigo protegido */ FeedGuardDisarm(&guard); }
//
// Diferencas em relacao ao __except(EXCEPTION_EXECUTE_HANDLER) do MSVC, de proposito:
//  - so intercepta falhas de hardware "de verdade" (access violation, instrucao ilegal,
//    divisao por zero...). Excecoes C++ (0xE06D7363), nomes de thread (0x406D1388),
//    breakpoints e outros RaiseException continuam seguindo o caminho normal, porque o
//    VEH roda ANTES de qualquer handler de frame e nao pode roubar o que um try/catch
//    interno do NGX resolveria sozinho;
//  - nao desenrola frames (longjmp sem unwind, _setjmp com frame NULL): nenhum destrutor
//    roda entre a falha e o ponto de retorno. Os wrappers do Feeder ja sao escritos sem
//    objetos C++ por causa da regra equivalente do /EHsc, entao nada muda;
//  - stack overflow nao e interceptado (nao ha pilha para o longjmp).
//
// O handler e por processo, mas a guarda e por thread (thread_local): uma falha em
// outra thread nao e tocada.
#pragma once
#include <windows.h>
#include <setjmp.h>

struct FeedGuard;
static thread_local FeedGuard *t_feed_guard = nullptr;
static PVOID                   g_feed_veh   = nullptr;
static void FeedGuardDisarm(FeedGuard *g);

struct FeedGuard
{
    jmp_buf    jb;
    FeedGuard *prev;
    DWORD      code;
    void      *addr;
    FeedGuard() : prev(nullptr), code(0), addr(nullptr) {}
    // Desarma sozinho ao sair do escopo: cobre o caminho normal, o caminho do longjmp
    // (ja desarmado, vira no-op) e uma excecao C++ que atravesse o bloco protegido --
    // sem isso a guarda ficaria apontando para um frame morto.
    ~FeedGuard() { FeedGuardDisarm(this); }
};

static bool FeedGuardIsFatal(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_INVALID_HANDLE:
        return true;
    default:
        return false;
    }
}

static LONG CALLBACK FeedGuardVeh(EXCEPTION_POINTERS *ep)
{
    FeedGuard *g = t_feed_guard;
    if (g == nullptr || ep == nullptr || ep->ExceptionRecord == nullptr) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (!FeedGuardIsFatal(code)) return EXCEPTION_CONTINUE_SEARCH;
    g->code = code;
    g->addr = ep->ExceptionRecord->ExceptionAddress;
    t_feed_guard = g->prev;   // a guarda dispara uma vez so
    longjmp(g->jb, 1);        // volta para o FEED_GUARD_ARM; nada e desenrolado
}

static void FeedGuardPush(FeedGuard *g)
{
    if (g_feed_veh == nullptr)
    {
        // Primeiro (first=1): precisa ver a falha antes do handler de frame do proprio
        // NGX/ReShade, que trataria como fatal. Corrida benigna: dois registros so
        // custam um handler a mais que nunca dispara para a mesma guarda.
        PVOID h = AddVectoredExceptionHandler(1, FeedGuardVeh);
        if (InterlockedCompareExchangePointer(&g_feed_veh, h, nullptr) != nullptr && h != nullptr)
            RemoveVectoredExceptionHandler(h);
    }
    g->code = 0;
    g->addr = nullptr;
    g->prev = t_feed_guard;
    t_feed_guard = g;
}

static void FeedGuardDisarm(FeedGuard *g)
{
    if (t_feed_guard == g) t_feed_guard = g->prev;
}

// _setjmp com frame NULL: o longjmp correspondente NAO chama RtlUnwindEx, so restaura os
// registradores. E o que queremos: o frame que falhou pode ser codigo estranho (NGX,
// add-on) sem unwind info confiavel para o unwinder do Windows.
#define FEED_GUARD_ARM(g) (FeedGuardPush(&(g)), _setjmp((g).jb, NULL) != 0)
