/* =====================================================================
   ponte\injetor.h -- injecao de DLL a partir do app (lado do DLSS5 For All).

   Modo Integrado, Fase 2 (CONTRATO-FASE2.md, secao B2, 15/09/2026): o app
   ja esta rodando e capturando um jogo; o usuario manda "Integrar" e o app
   injeta a nossa DLL de 64 bits (bin\dlss5-ponte64.dll) NO PROCESSO DO JOGO
   ja em execucao. A DLL engancha o Present, compartilha o backbuffer com o
   host (o nosso exe, PonteHost) e devolve o resultado antes do Present real.
   NADA e gravado na pasta do jogo e o jogo NAO reinicia.

   Referencia: crosire/reshade tools\injector.cpp (BSD-3). Usamos a variante
   SIMPLES de la (RESHADE_LOADING_THREAD_FUNC=0): o CreateRemoteThread chama
   direto LoadLibraryW com o caminho da DLL como argumento -- nao ha codigo
   copiado para o processo do jogo, so a string do caminho. Serve porque a
   nossa DLL nao faz trabalho no DllMain: o PROCESS_ATTACH so sobe uma thread
   (contrato do B1). LoadLibraryW mora no mesmo endereco em todo processo de
   64 bits (kernel32 na mesma base), entao o endereco lido AQUI vale LA.

   SO 64 BITS NESTA FASE. Bits64() diz se o processo e nativo de 64 bits; um
   jogo de 32 bits (WOW64) recebe um cartaz "ainda nao" no main.cpp e segue no
   caminho do add-on do Feeder. Injetar() tambem recusa um alvo WOW64, por
   seguranca (uma DLL de 64 bits nao carrega num processo de 32).

   O QUE ESTE HEADER NAO FAZ: nao atualiza a DACL da DLL para
   ALL_APPLICATION_PACKAGES (jogos UWP/da Store). O alvo real desta fase
   (Resident Evil 2, Win64 comum) nao precisa; um jogo UWP precisaria e isso
   fica anotado no relatorio como trabalho futuro (exige advapi32).
   ===================================================================== */
#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <string>
#include <cstdio>

namespace Injetor {

/* Traducao curta de um erro win32 para a string do chamador. */
static inline std::string ComErro(const char* onde, DWORD e)
{
    char b[192];
    _snprintf(b, sizeof(b), "%s (win32=%lu)", onde, (unsigned long)e);
    b[sizeof(b) - 1] = 0;
    return std::string(b);
}

/* Abre o processo com o MENOR direito que responde a arquitetura. */
static inline HANDLE AbrirParaConsulta(DWORD pid)
{
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (p == nullptr) p = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    return p;
}

/* true = o processo `pid` e NATIVO de 64 bits. Como o app (e o SO onde ele
   roda) e de 64 bits, um processo nao-WOW64 e de 64 bits e um WOW64 e de 32.
   false quando e de 32 bits OU quando nao deu para saber (sem direito de
   consulta): o main.cpp trata os dois como "nao da para injetar de 64 bits"
   e mostra o cartaz de 32 bits (o caso comum). */
static inline bool Bits64(DWORD pid)
{
    HANDLE p = AbrirParaConsulta(pid);
    if (p == nullptr) return false;
    BOOL wow = FALSE;
    const BOOL ok = IsWow64Process(p, &wow);
    CloseHandle(p);
    if (!ok) return false;
    return wow == FALSE;
}

/* Injeta `dll` (caminho absoluto) no processo `pid` por LoadLibraryW remoto.
   Devolve false com *erro preenchido em qualquer falha; nesse caso NADA fica
   pendurado no processo do jogo (a memoria alocada e liberada em todo
   caminho de saida). So 64 bits: um alvo WOW64 e recusado. */
static inline bool Injetar(DWORD pid, const wchar_t* dll, std::string* erro)
{
    auto falha = [&](const std::string& m) -> bool { if (erro) *erro = m; return false; };

    if (dll == nullptr || dll[0] == 0) return falha("caminho da dll vazio");
    if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES)
        return falha("a dll nao existe no caminho esperado (bin\\dlss5-ponte64.dll)");

    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ |
                              PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (proc == nullptr) return falha(ComErro("OpenProcess (injecao) recusado", GetLastError()));

    /* So 64 bits: uma dll de 64 nao carrega num processo WOW64. */
    BOOL wow = FALSE;
    if (IsWow64Process(proc, &wow) && wow) { CloseHandle(proc); return falha("o processo alvo e de 32 bits (WOW64)"); }

    const SIZE_T bytes = (wcslen(dll) + 1) * sizeof(wchar_t);
    void* remoto = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remoto == nullptr) { const DWORD e = GetLastError(); CloseHandle(proc); return falha(ComErro("VirtualAllocEx falhou", e)); }

    if (!WriteProcessMemory(proc, remoto, dll, bytes, nullptr))
    { const DWORD e = GetLastError(); VirtualFreeEx(proc, remoto, 0, MEM_RELEASE); CloseHandle(proc); return falha(ComErro("WriteProcessMemory falhou", e)); }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE load = k32 != nullptr
        ? reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<void*>(GetProcAddress(k32, "LoadLibraryW")))
        : nullptr;
    if (load == nullptr) { VirtualFreeEx(proc, remoto, 0, MEM_RELEASE); CloseHandle(proc); return falha("nao achei LoadLibraryW no kernel32"); }

    HANDLE th = CreateRemoteThread(proc, nullptr, 0, load, remoto, 0, nullptr);
    if (th == nullptr) { const DWORD e = GetLastError(); VirtualFreeEx(proc, remoto, 0, MEM_RELEASE); CloseHandle(proc); return falha(ComErro("CreateRemoteThread falhou", e)); }

    /* Espera o LoadLibraryW terminar. 10 s e folga larga: a DLL nao trabalha
       no DllMain (so sobe uma thread), entao o LoadLibraryW volta rapido. */
    const DWORD w = WaitForSingleObject(th, 10000);
    DWORD saida = 0;
    GetExitCodeThread(th, &saida);
    VirtualFreeEx(proc, remoto, 0, MEM_RELEASE);
    CloseHandle(th);
    CloseHandle(proc);

    if (w != WAIT_OBJECT_0) return falha("o LoadLibraryW remoto nao terminou em 10 s");
    /* O codigo de saida da thread e o HMODULE truncado a 32 bits: 0 = a dll
       nao carregou. Nao-zero = carregou (a base baixa raramente e 0). */
    if (saida == 0) return falha("LoadLibraryW no processo alvo devolveu 0 (a dll nao carregou)");
    return true;
}

} // namespace Injetor
