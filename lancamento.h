// =====================================================================================
//  lancamento.h - O BILHETE DO LAUNCHER (v2.0, 02/10/2026).
//
//  O pacote abre SEMPRE pela tela do canal: o DLSS5ForAll.exe da raiz (o launcher) e a
//  porta, e o bin\DLSS5ForAll.exe (o app) confere, antes de qualquer janela ou device,
//  se foi aberto por ela. Este arquivo e o contrato entre os dois lados, incluido pelos
//  dois (launcher.cpp com DLSS5_LAUNCHER, main.cpp sem).
//
//  O MECANISMO
//    1. O launcher cria o app SUSPENSO (ja sabe o PID do filho antes de ele rodar uma
//       linha) e publica, com o nome amarrado a esse PID:
//         Local\DLSS5_Lancado_<pid>        evento manual, apagado: "o app conferiu"
//         Local\DLSS5_Lancado_<pid>_b      memoria compartilhada com o Bilhete abaixo:
//                                          marca, PID do launcher e 128 bits aleatorios
//                                          (BCryptGenRandom)
//       e so entao solta a thread. Espera ate 10 s o evento acender e sai - fica vivo
//       durante a conferencia, entao o app sempre encontra o pai no ar.
//    2. O app (ConferirLancamento, no topo do wWinMain) aceita o bilhete so se o PID do
//       launcher escrito nele e o do PROPRIO pai e se o pai e o launcher da raiz
//       (<pasta do exe>\..\DLSS5ForAll.exe). Aceito (ou liberado por ser filho do proprio app), acende o
//       evento do proprio PID: e o "conferido" que o launcher espera.
//    3. Os relancamentos do proprio app (reabrir, reinicio automatico, troca bin<->bin-amd)
//       usam a mesma porta pelo outro lado: o pai e o nosso exe, e quem lancou espera o
//       "conferido" (EsperarConferido) antes de seguir - sem isso o lancador do --relancar
//       saia antes de o filho olhar quem era o pai, e o filho via um pai morto.
//
//  O QUE ISTO NAO E: protecao contra quem cria processos suspensos de proposito. E a
//  garantia de que um atalho, um duplo clique no bin\ ou um "fixar na barra" passam pela
//  tela do canal - que e o que o dono pediu.
// =====================================================================================
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <string>
#include <cstdint>
#ifdef DLSS5_LAUNCHER
#include <bcrypt.h>
#endif

namespace Lancamento {

static const uint32_t kMarca = 0x354C4644u;   // "DFL5"

struct Bilhete {
    uint32_t marca;        // kMarca
    uint32_t pidLauncher;  // quem lancou: o app confere que e o PROPRIO pai
    uint8_t  token[16];    // 128 bits aleatorios; zerado = bilhete invalido
};

inline std::wstring NomeEvento(DWORD pid)  { return L"Local\\DLSS5_Lancado_" + std::to_wstring((unsigned long)pid); }
inline std::wstring NomeBilhete(DWORD pid) { return NomeEvento(pid) + L"_b"; }

// v2.1: O DACL PADRAO DE UM TOKEN ELEVADO SO DA ACESSO A ADMINISTRATORS E SYSTEM. Launcher e app com niveis
// diferentes (um "executar como administrador" so num deles) viam OpenEventW/OpenFileMappingW falhar com
// ACCESS_DENIED - o bilhete "invalido" e o app reabrindo o launcher: "cliquei em Iniciar e nada aconteceu".
// A ACL explicita: SYSTEM/Administrators/dono = tudo; qualquer usuario autenticado = so ESPERAR o evento
// (SYNCHRONIZE) ou so LER a memoria (SECTION_MAP_READ). Ninguem alem do dono escreve o bilhete, e o app
// continua exigindo pidLauncher == PID do proprio pai + o pai ser o launcher da raiz: a trava nao muda.
struct AclAberta {
    SECURITY_ATTRIBUTES sa{}; PSECURITY_DESCRIPTOR sd = nullptr;
    explicit AclAberta(const wchar_t* sddl) {
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) {
            sa.nLength = sizeof(sa); sa.lpSecurityDescriptor = sd; sa.bInheritHandle = FALSE;
        }
    }
    ~AclAberta() { if (sd) LocalFree(sd); }
    SECURITY_ATTRIBUTES* get() { return sd ? &sa : nullptr; }   // nullptr = o DACL padrao, como na 2.0
};
inline const wchar_t* SddlEvento()  { return L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)(A;;0x00100000;;;AU)"; }
inline const wchar_t* SddlMemoria() { return L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;OW)(A;;0x0004;;;AU)"; }

// O lado de quem lancou (launcher e relancamentos do app): espera o filho dizer "conferido"
// - o evento do PID dele aceso - ou morrer, ate `tetoMs`. Os dois casos liberam quem espera;
// estourar o prazo tambem (o filho segue sozinho). Devolve true so quando o filho conferiu.
inline bool EsperarConferido(HANDLE filho, DWORD pid, DWORD tetoMs) {
    const ULONGLONG t0 = GetTickCount64();
    HANDLE ev = nullptr;
    while (GetTickCount64() - t0 < tetoMs) {
        if (!ev) ev = OpenEventW(SYNCHRONIZE, FALSE, NomeEvento(pid).c_str());
        if (ev && WaitForSingleObject(ev, 0) == WAIT_OBJECT_0) { CloseHandle(ev); return true; }
        if (filho && WaitForSingleObject(filho, 15) == WAIT_OBJECT_0) break;
        if (!filho) Sleep(15);
    }
    if (ev) CloseHandle(ev);
    return false;
}

// ShellExecuteW com a espera do "conferido": o caminho dos relancamentos do proprio app. Mesmo
// contrato do ShellExecuteW (valor > 32 = abriu), para quem chama nao mudar o resto.
inline INT_PTR AbrirEEsperarConferido(const wchar_t* exe, const wchar_t* args, const wchar_t* pasta, DWORD tetoMs = 5000) {
    SHELLEXECUTEINFOW si{}; si.cbSize = sizeof(si);
    si.fMask = SEE_MASK_NOCLOSEPROCESS;
    si.lpVerb = L"open"; si.lpFile = exe; si.lpParameters = args; si.lpDirectory = pasta;
    si.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&si)) return (INT_PTR)si.hInstApp > 32 ? 0 : (INT_PTR)si.hInstApp;
    if (si.hProcess) {
        EsperarConferido(si.hProcess, GetProcessId(si.hProcess), tetoMs);
        CloseHandle(si.hProcess);
    }
    return (INT_PTR)si.hInstApp > 32 ? (INT_PTR)si.hInstApp : 33;
}

#ifdef DLSS5_LAUNCHER
// O lado do launcher: publica evento + bilhete para o filho SUSPENSO `pidFilho`. Os dois
// handles ficam com quem chamou ate o fim da espera (fechar antes apagaria os objetos).
// `erro` (opcional) recebe o GetLastError() do primeiro passo que falhou, e `etapa` o nome dele, para o log.
inline bool PublicarBilhete(DWORD pidFilho, HANDLE* ev, HANDLE* mapa, DWORD* erro = nullptr, const char** etapa = nullptr) {
    auto falha = [&](const char* e) { if (erro) *erro = GetLastError(); if (etapa) *etapa = e; return false; };
    AclAberta aclEv(SddlEvento()), aclMapa(SddlMemoria());
    *ev = CreateEventW(aclEv.get(), TRUE, FALSE, NomeEvento(pidFilho).c_str());
    if (!*ev) return falha("CreateEvent");
    *mapa = CreateFileMappingW(INVALID_HANDLE_VALUE, aclMapa.get(), PAGE_READWRITE, 0, sizeof(Bilhete), NomeBilhete(pidFilho).c_str());
    if (!*mapa) return falha("CreateFileMapping");
    Bilhete* b = (Bilhete*)MapViewOfFile(*mapa, FILE_MAP_WRITE, 0, 0, sizeof(Bilhete));
    if (!b) return falha("MapViewOfFile");
    Bilhete n{};
    n.marca = kMarca;
    n.pidLauncher = GetCurrentProcessId();
    const bool ok = BCRYPT_SUCCESS(BCryptGenRandom(nullptr, n.token, sizeof(n.token), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    if (ok) *b = n;
    UnmapViewOfFile(b);
    if (!ok) { if (erro) *erro = 0; if (etapa) *etapa = "BCryptGenRandom"; }
    return ok;
}
#endif

}  // namespace Lancamento
