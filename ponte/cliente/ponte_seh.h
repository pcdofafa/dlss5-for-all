// ponte_seh.h -- a rede de seguranca dos nossos hooks dentro do processo do JOGO (I10).
//
// O QUE ESTE ARQUIVO EXISTE PARA IMPEDIR, com nome e data: em 16/09/2026 13:21 e 13:47 e em
// 17/09/2026 23:38 e 23:41 o `Resident Evil 2.exe` foi fechado pelo Windows com 0xC0000005
// dentro da `dlss5-ponte64.dll` (WER: modulo dlss5-ponte64.dll, deslocamentos 0x3b5a, 0x703e
// e 0x3696), sempre depois de o jogador mudar a resolucao com o Nativo ligado. Um defeito
// nosso fechou o jogo de alguem. Consertar a causa e obrigatorio e esta sendo feito em
// separado; isto aqui e a garantia de que o PROXIMO defeito - o que ninguem previu - custe
// o nosso recurso e nao a partida do jogador.
//
// A REGRA DE OURO: se algo explodir dentro de um hook nosso, o jogo continua. A DLL fica
// INERTE na hora (todos os hooks passam a so chamar o original, como se nunca tivessemos
// enganchado nada), registra o estouro e nao tenta de novo naquela sessao. O app vai ver o
// Nativo morrer pelo vigia de saude e cair para o Universal com cartaz (A8/A9).
//
// POR QUE NAO `__try`/`__except`: o g++ do MinGW nao aceita SEH estruturado, e a DLL e
// compilada com ele. A ideia e a mesma que a variante AMD do Feeder ja usa em
// `feeder-amd/src-amd/feed_seh_shim.h` - Vectored Exception Handler + longjmp -, reescrita
// aqui com nomes proprios para nao acoplar os dois modulos.
//
//   PtGuarda g;
//   if (PT_GUARDA_ARMAR(g)) { /* estourou: g.code e g.addr dizem o que e onde */ }
//   else { /* corpo protegido */ PtGuardaDesarmar(&g); }
//
// DIFERENCAS EM RELACAO AO __except(EXCEPTION_EXECUTE_HANDLER), todas de proposito:
//
//  - so intercepta falha de hardware DE VERDADE (violacao de acesso, instrucao ilegal,
//    divisao por zero...). Excecao C++ (0xE06D7363), nome de thread (0x406D1388),
//    breakpoint e o resto do RaiseException seguem o caminho normal: o VEH roda ANTES de
//    qualquer handler de frame, e roubar uma excecao que um try/catch do proprio jogo (ou
//    do NGX, ou do ReShade) resolveria sozinho seria trocar um defeito nosso por outro.
//
//  - NAO desenrola frames. O longjmp usa _setjmp com frame NULL, entao nao passa pelo
//    RtlUnwindEx: nenhum destrutor roda entre a falha e a volta. Isso e requisito, nao
//    limitacao - o frame que falhou pode ser codigo de terceiros sem unwind info
//    confiavel. A consequencia tem de ser respeitada por quem usa: o corpo protegido
//    pode deixar uma trava presa.
//
//    E POR ISSO QUE "FICAR INERTE" E PARTE DO CONTRATO, e nao um extra. Depois de um
//    estouro, nenhum caminho nosso na thread do jogo entra mais no corpo dos hooks (o
//    teste de `inerte` vem antes da guarda), portanto ninguem tenta pegar de novo uma
//    trava que ficou presa. O jogo nao pode travar; uma thread nossa parada num mutex
//    orfao e aceitavel, porque a DLL ja nao tem mais nada a fazer.
//
//  - estouro de pilha (EXCEPTION_STACK_OVERFLOW) NAO e interceptado: nao sobra pilha para
//    o longjmp voltar.
//
// CUSTO POR QUADRO: armar e um push numa variavel thread_local e um _setjmp (salvar os
// registradores nao-volateis); desarmar e um store. Nenhuma chamada de sistema, nenhuma
// alocacao, nenhuma trava. O handler global e registrado uma unica vez, na primeira vez
// que alguem arma.
//
// J4 (18/09/2026): tudo aqui e `inline` (as variaveis inclusive, C++17), e nao `static`, porque
// o header passou a ser incluido por MAIS DE UMA unidade de traducao da mesma DLL
// (ponte_cliente.cpp e fg/fg_hook.cpp, que guarda a thread de ritmo). Com `static`, cada uma
// teria a sua propria `t_ptGuarda` e registraria o seu proprio handler: uma guarda armada num
// arquivo seria invisivel ao handler do outro.
#pragma once
#include <windows.h>
#include <setjmp.h>

struct PtGuarda;
inline thread_local PtGuarda* t_ptGuarda = nullptr;
inline PVOID                  g_ptVeh    = nullptr;
inline void PtGuardaDesarmar(PtGuarda* g);

struct PtGuarda
{
    jmp_buf   jb;
    PtGuarda* prev;
    DWORD     code;
    void*     addr;
    PtGuarda() : prev(nullptr), code(0), addr(nullptr) {}
    // Desarma sozinho ao sair do escopo: cobre o caminho normal, o caminho do longjmp (ja
    // desarmado pelo handler, vira no-op) e uma excecao C++ que atravesse o corpo - sem
    // isto a guarda ficaria apontando para um frame morto.
    ~PtGuarda() { PtGuardaDesarmar(this); }
};

inline bool PtGuardaEhFatal(DWORD code)
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

inline LONG CALLBACK PtGuardaVeh(EXCEPTION_POINTERS* ep)
{
    PtGuarda* g = t_ptGuarda;
    if (g == nullptr || ep == nullptr || ep->ExceptionRecord == nullptr) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (!PtGuardaEhFatal(code)) return EXCEPTION_CONTINUE_SEARCH;
    g->code = code;
    g->addr = ep->ExceptionRecord->ExceptionAddress;
    t_ptGuarda = g->prev;   // a guarda dispara uma vez so
    longjmp(g->jb, 1);      // volta para o PT_GUARDA_ARMAR; nada e desenrolado
}

inline void PtGuardaPush(PtGuarda* g)
{
    if (g_ptVeh == nullptr)
    {
        // first = 1: a falha tem de chegar aqui antes do handler de frame do jogo, que a
        // trataria como fatal. Corrida benigna: dois registros custam um handler a mais
        // que nunca dispara para a mesma guarda.
        PVOID h = AddVectoredExceptionHandler(1, PtGuardaVeh);
        if (InterlockedCompareExchangePointer(&g_ptVeh, h, nullptr) != nullptr && h != nullptr)
            RemoveVectoredExceptionHandler(h);
    }
    g->code = 0;
    g->addr = nullptr;
    g->prev = t_ptGuarda;
    t_ptGuarda = g;
}

inline void PtGuardaDesarmar(PtGuarda* g)
{
    if (t_ptGuarda == g) t_ptGuarda = g->prev;
}

// _setjmp com frame NULL: o longjmp correspondente NAO chama RtlUnwindEx, so restaura os
// registradores - ver a nota sobre destrutores no cabecalho.
#define PT_GUARDA_ARMAR(g) (PtGuardaPush(&(g)), _setjmp((g).jb, NULL) != 0)
