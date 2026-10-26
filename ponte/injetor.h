/* =====================================================================
   ponte\injetor.h -- o que sobrou do lado do APP do antigo modo Nativo.

   2.2.3 (decisao do dono, 26/10/2026): o MODO NATIVO FOI REMOVIDO DO
   PROGRAMA. Ele carregava a nossa DLL (bin\dlss5-ponte64.dll) dentro do
   jogo ja aberto pelo auxiliar bin\dlss5-integrar.exe (OpenProcess +
   VirtualAllocEx + WriteProcessMemory + CreateRemoteThread). Nao era
   malware, mas e a tecnica classica de malware, e o Windows Defender, o
   Google Drive e varios antivirus passaram a acusar o pacote por causa
   dele. O auxiliar saiu do build e do pacote; o lancamento dele
   (Injetor::Injetar) saiu daqui. O DLSS 5 roda sempre pelo Universal.

   Sobra so a CONSULTA de arquitetura (Bits64), que o seletor usa para
   descrever a janela escolhida no log. Ela so le (IsWow64Process com o
   direito minimo de consulta); nao escreve nada em outro processo.
   ===================================================================== */
#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>

namespace Injetor {

/* Abre o processo com o MENOR direito que responde a arquitetura. */
static inline HANDLE AbrirParaConsulta(DWORD pid)
{
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (p == nullptr) p = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    return p;
}

/* true = o processo `pid` e NATIVO de 64 bits (nao-WOW64). false quando e de
   32 bits OU quando nao deu para saber (sem direito de consulta). */
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

} // namespace Injetor
