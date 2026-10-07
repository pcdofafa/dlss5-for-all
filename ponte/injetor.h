/* =====================================================================
   ponte\injetor.h -- o lado do APP do modo Integrado/Nativo.

   Modo Integrado, Fase 2 (CONTRATO-FASE2.md, secao B2, 15/09/2026): o app
   ja esta rodando e capturando um jogo; o usuario manda "Integrar" e a nossa
   DLL de 64 bits (bin\dlss5-ponte64.dll) e carregada NO PROCESSO DO JOGO ja
   em execucao. A DLL engancha o Present, compartilha o backbuffer com o host
   (o nosso exe, PonteHost) e devolve o resultado antes do Present real.
   NADA e gravado na pasta do jogo e o jogo NAO reinicia.

   07/10/2026: QUEM CARREGA A DLL NO JOGO E O AUXILIAR bin\dlss5-integrar.exe
   (ponte\integrar.cpp), nao mais este processo. O app principal so captura,
   filtra e mostra; as funcoes de escrever em outro processo (VirtualAllocEx,
   WriteProcessMemory, CreateRemoteThread) ficaram so no auxiliar, que o app
   executa apenas quando a pessoa usa o modo Nativo. Mesmo alvo, mesma DLL,
   mesmas checagens e os MESMOS textos de erro no log (o auxiliar os devolve
   por um pipe); aqui so sobram a consulta de arquitetura e o lancamento.

   SO 64 BITS NESTA FASE. Bits64() diz se o processo e nativo de 64 bits; um
   jogo de 32 bits (WOW64) recebe um cartaz "ainda nao" no main.cpp e segue no
   caminho do add-on do Feeder. O auxiliar tambem recusa um alvo WOW64.

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
#include <vector>
#include <cstdio>

namespace Injetor {

static constexpr const wchar_t* kAuxiliar = L"dlss5-integrar.exe";

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

/* O texto de cada codigo do auxiliar, para quando a linha dele nao chegar
   (ele caiu antes de escrever). Os codigos estao em ponte\integrar.cpp. */
static inline std::string TextoDoCodigo(DWORD c)
{
    switch (c) {
    case 2:  return "o auxiliar recusou os argumentos";
    case 3:  return "a dll nao existe no caminho esperado (bin\\dlss5-ponte64.dll)";
    case 4:  return "OpenProcess (injecao) recusado";
    case 5:  return "o processo alvo e de 32 bits (WOW64)";
    case 6:  return "VirtualAllocEx falhou";
    case 7:  return "WriteProcessMemory falhou";
    case 8:  return "nao achei LoadLibraryW no kernel32";
    case 9:  return "CreateRemoteThread falhou";
    case 10: return "o LoadLibraryW remoto nao terminou em 10 s";
    case 11: return "LoadLibraryW no processo alvo devolveu 0 (a dll nao carregou)";
    default: { char b[96]; _snprintf(b, sizeof(b), "o auxiliar saiu com o codigo 0x%08lX", (unsigned long)c); b[sizeof(b) - 1] = 0; return b; }
    }
}

/* Carrega `dll` (caminho absoluto) no processo `pid` pelo auxiliar `auxiliar`
   (o caminho completo do dlss5-integrar.exe). Espera ele sair (ate 20 s: o
   proprio auxiliar desiste do LoadLibraryW remoto em 10 s) e devolve false com
   *erro preenchido em qualquer falha - o texto que o auxiliar escreveu no pipe
   (o mesmo de antes), ou o do codigo de saida. */
static inline bool Injetar(const std::wstring& auxiliar, DWORD pid, const wchar_t* dll, std::string* erro)
{
    auto falha = [&](const std::string& m) -> bool { if (erro) *erro = m; return false; };
    if (dll == nullptr || dll[0] == 0) return falha("caminho da dll vazio");
    if (GetFileAttributesW(auxiliar.c_str()) == INVALID_FILE_ATTRIBUTES)
        return falha("o auxiliar do modo Nativo nao existe (bin\\dlss5-integrar.exe)");

    /* O pipe da saida padrao do auxiliar: SO a ponta de escrita e herdavel, e
       so ela e repassada (lista explicita de herdados - nenhum outro handle
       deste processo vai junto). */
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE ler = nullptr, escrever = nullptr;
    if (!CreatePipe(&ler, &escrever, &sa, 0)) return falha(ComErro("CreatePipe para o auxiliar falhou", GetLastError()));
    SetHandleInformation(ler, HANDLE_FLAG_INHERIT, 0);

    SIZE_T tam = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &tam);
    std::vector<unsigned char> lista(tam ? tam : 64);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(lista.data());
    const bool comLista = InitializeProcThreadAttributeList(attrs, 1, 0, &tam) &&
                          UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &escrever, sizeof(escrever), nullptr, nullptr);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nullptr;
    si.StartupInfo.hStdOutput = escrever;
    si.StartupInfo.hStdError = escrever;
    si.lpAttributeList = comLista ? attrs : nullptr;

    std::wstring linha = L"\"" + auxiliar + L"\" " + std::to_wstring((unsigned long)pid) + L" \"" + dll + L"\"";
    std::wstring pasta = auxiliar.substr(0, auxiliar.find_last_of(L"\\/") + 1);
    PROCESS_INFORMATION pi{};
    const BOOL criou = CreateProcessW(auxiliar.c_str(), &linha[0], nullptr, nullptr, TRUE,
                                      comLista ? EXTENDED_STARTUPINFO_PRESENT : 0, nullptr,
                                      pasta.empty() ? nullptr : pasta.c_str(), &si.StartupInfo, &pi);
    const DWORD erroCriar = GetLastError();
    if (comLista) DeleteProcThreadAttributeList(attrs);
    CloseHandle(escrever);   /* a nossa copia: sem ela o ReadFile abaixo ve o fim quando o auxiliar sair */
    if (!criou) { CloseHandle(ler); return falha(ComErro("nao consegui abrir o auxiliar dlss5-integrar.exe", erroCriar)); }
    CloseHandle(pi.hThread);

    const DWORD w = WaitForSingleObject(pi.hProcess, 20000);
    DWORD codigo = (DWORD)-1;
    if (w != WAIT_OBJECT_0) { TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 2000); }
    else GetExitCodeProcess(pi.hProcess, &codigo);
    CloseHandle(pi.hProcess);

    /* A linha do auxiliar (poucos bytes: cabe no buffer do pipe, entao ler
       depois de ele sair nao trava). */
    std::string texto;
    char b[512];
    DWORD n = 0;
    while (texto.size() < 2048 && ReadFile(ler, b, sizeof(b), &n, nullptr) && n > 0) texto.append(b, n);
    CloseHandle(ler);
    while (!texto.empty() && (texto.back() == '\n' || texto.back() == '\r')) texto.pop_back();

    if (w != WAIT_OBJECT_0) return falha("o auxiliar dlss5-integrar.exe nao voltou em 20 s");
    if (codigo == 0) return true;
    return falha(texto.empty() ? TextoDoCodigo(codigo) : texto);
}

} // namespace Injetor
