/* =====================================================================
   ponte\integrar.cpp -- bin\dlss5-integrar.exe, o AUXILIAR do modo Nativo.

   POR QUE UM EXE SEPARADO (reestruturacao de 07/10/2026, pedido do dono):
   o modo Nativo/Integrado carrega a nossa DLL (bin\dlss5-ponte64.dll) no
   processo do jogo por LoadLibraryW remoto. Ate a 2.1 isso morava dentro do
   bin\DLSS5ForAll.exe (ponte\injetor.h), e o programa principal - que so
   captura, filtra e mostra - carregava junto as funcoes de escrever em outro
   processo. Agora elas moram SO aqui: um exe pequeno, que o app executa
   apenas quando a pessoa usa o modo Nativo, faz a integracao e sai. Nada foi
   escondido: as funcoes sao as mesmas, importadas normalmente, so que no
   lugar onde sao usadas.

   USO:  dlss5-integrar.exe <pid do jogo> "<caminho absoluto da dll>"
   SAIDA: codigo de retorno (0 = a dll carregou no jogo) e, na saida padrao
   (um pipe que o app passa), UMA linha com o motivo da falha - o mesmo
   texto que o injetor.h de antes devolvia, para o log do app nao mudar.
     0 ok | 2 argumentos | 3 dll ausente | 4 OpenProcess | 5 alvo de 32 bits
     6 VirtualAllocEx | 7 WriteProcessMemory | 8 sem LoadLibraryW
     9 CreateRemoteThread | 10 o LoadLibraryW remoto nao terminou em 10 s
     11 LoadLibraryW no alvo devolveu 0

   Referencia: crosire/reshade tools\injector.cpp (BSD-3), a variante SIMPLES
   (RESHADE_LOADING_THREAD_FUNC=0): o CreateRemoteThread chama direto
   LoadLibraryW com o caminho da DLL como argumento -- nao ha codigo copiado
   para o processo do jogo, so a string do caminho. Serve porque a nossa DLL
   nao faz trabalho no DllMain (o PROCESS_ATTACH so sobe uma thread).
   LoadLibraryW mora no mesmo endereco em todo processo de 64 bits (kernel32
   na mesma base), entao o endereco lido AQUI vale LA.

   SO 64 BITS: um alvo WOW64 e recusado (uma DLL de 64 nao carrega num
   processo de 32). O app ja confere isso antes (Injetor::Bits64); aqui e a
   mesma checagem como rede de seguranca, como era antes.

   Compilado pelo build-mingw.sh (-municode -mwindows: sem console).
   ===================================================================== */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <cstdio>
#include <cwchar>

/* Traducao curta de um erro win32 para a linha de resultado. */
static std::string ComErro(const char* onde, DWORD e)
{
    char b[192];
    _snprintf(b, sizeof(b), "%s (win32=%lu)", onde, (unsigned long)e);
    b[sizeof(b) - 1] = 0;
    return std::string(b);
}

/* Escreve a linha de resultado na saida padrao (o pipe do app). Sem pipe
   (alguem rodou o exe a mao), nao ha para onde escrever: so o codigo vale. */
static int Responder(int codigo, const std::string& texto)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE && !texto.empty()) {
        DWORD n = 0;
        WriteFile(out, texto.data(), (DWORD)texto.size(), &n, nullptr);
    }
    return codigo;
}

/* Injeta `dll` (caminho absoluto) no processo `pid` por LoadLibraryW remoto.
   Em qualquer falha NADA fica pendurado no processo do jogo (a memoria
   alocada e liberada em todo caminho de saida). Mesmo codigo e mesmos
   textos do antigo Injetor::Injetar. */
static int Injetar(DWORD pid, const wchar_t* dll)
{
    if (dll == nullptr || dll[0] == 0) return Responder(2, "caminho da dll vazio");
    if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES)
        return Responder(3, "a dll nao existe no caminho esperado (bin\\dlss5-ponte64.dll)");

    HANDLE proc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_READ |
                              PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (proc == nullptr) return Responder(4, ComErro("OpenProcess (injecao) recusado", GetLastError()));

    /* So 64 bits: uma dll de 64 nao carrega num processo WOW64. */
    BOOL wow = FALSE;
    if (IsWow64Process(proc, &wow) && wow) { CloseHandle(proc); return Responder(5, "o processo alvo e de 32 bits (WOW64)"); }

    const SIZE_T bytes = (wcslen(dll) + 1) * sizeof(wchar_t);
    void* remoto = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remoto == nullptr) { const DWORD e = GetLastError(); CloseHandle(proc); return Responder(6, ComErro("VirtualAllocEx falhou", e)); }

    if (!WriteProcessMemory(proc, remoto, dll, bytes, nullptr))
    { const DWORD e = GetLastError(); VirtualFreeEx(proc, remoto, 0, MEM_RELEASE); CloseHandle(proc); return Responder(7, ComErro("WriteProcessMemory falhou", e)); }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE load = k32 != nullptr
        ? reinterpret_cast<LPTHREAD_START_ROUTINE>(reinterpret_cast<void*>(GetProcAddress(k32, "LoadLibraryW")))
        : nullptr;
    if (load == nullptr) { VirtualFreeEx(proc, remoto, 0, MEM_RELEASE); CloseHandle(proc); return Responder(8, "nao achei LoadLibraryW no kernel32"); }

    HANDLE th = CreateRemoteThread(proc, nullptr, 0, load, remoto, 0, nullptr);
    if (th == nullptr) { const DWORD e = GetLastError(); VirtualFreeEx(proc, remoto, 0, MEM_RELEASE); CloseHandle(proc); return Responder(9, ComErro("CreateRemoteThread falhou", e)); }

    /* Espera o LoadLibraryW terminar. 10 s e folga larga: a DLL nao trabalha
       no DllMain (so sobe uma thread), entao o LoadLibraryW volta rapido. */
    const DWORD w = WaitForSingleObject(th, 10000);
    DWORD saida = 0;
    GetExitCodeThread(th, &saida);
    VirtualFreeEx(proc, remoto, 0, MEM_RELEASE);
    CloseHandle(th);
    CloseHandle(proc);

    if (w != WAIT_OBJECT_0) return Responder(10, "o LoadLibraryW remoto nao terminou em 10 s");
    /* O codigo de saida da thread e o HMODULE truncado a 32 bits: 0 = a dll
       nao carregou. Nao-zero = carregou (a base baixa raramente e 0). */
    if (saida == 0) return Responder(11, "LoadLibraryW no processo alvo devolveu 0 (a dll nao carregou)");
    return Responder(0, "ok");
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr || argc != 3) {
        if (argv) LocalFree(argv);
        return Responder(2, "uso: dlss5-integrar.exe <pid> <caminho da dll>");
    }
    wchar_t* fim = nullptr;
    const unsigned long pid = wcstoul(argv[1], &fim, 10);
    if (fim == nullptr || *fim != L'\0' || pid == 0) {
        LocalFree(argv);
        return Responder(2, "pid invalido na linha de comando");
    }
    const std::wstring dll = argv[2];
    LocalFree(argv);
    return Injetar((DWORD)pid, dll.c_str());
}
