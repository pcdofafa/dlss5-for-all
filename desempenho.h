// =====================================================================================
//  desempenho.h - "OTIMIZAR DESEMPENHO": quando o jogo usa a placa toda, a pelicula
//  (a nossa camada) fica lenta. Este modulo da ao app tres alavancas e uma escada
//  automatica que as usa na ordem certa, medindo em cada degrau.
//
//  Header-only, no estilo do seletor_hud.h e do atualizacao.h. NAO depende de nada do
//  main.cpp: o que ele precisa do app (log, cartaz, gravar work_resolution, ler o fps da
//  camada) chega por ponteiros de funcao/callbacks no Contexto. Por isso ele compila
//  sozinho num teste (scratchpad\otimizar-desempenho\medidor.cpp) contra um jogo de teste.
//
//  AS ALAVANCAS, da mais barata para a mais cara (o efeito de cada uma foi MEDIDO, ver
//  docs\pesquisa-2026-09-06\RELATORIO-DESEMPENHO-JOGO.md):
//    1. PRIORIDADE DE GPU DO NOSSO PROCESSO. D3DKMTSetProcessSchedulingPriorityClass
//       (gdi32.dll, carregada dinamicamente) em REALTIME, com HIGH de reserva. So no nosso
//       processo, reversivel, sem privilegio. E a UNICA alavanca que pega ao vivo e
//       resolve sozinha: pelicula de 0,9 para 80 fps com o jogo saturando a placa.
//    2. LIMITE DE FPS DO JOGO PELO DRIVER (NVIDIA). NvAPI DRS, nvapi64.dll carregada
//       dinamicamente por nvapi_QueryInterface com os IDs publicos das funcoes (os mesmos
//       usados pelo nvapi-open-source-sdk e pelo Special K); sem SDK, sem .lib. Cria/acha
//       o perfil do EXE do alvo e grava FRL_FPS_ID (0x10835002) = 60 ou 30. O valor
//       ORIGINAL vai para o dlss5forall.ini ([desempenho]) antes de qualquer alteracao,
//       para restaurar mesmo depois de uma queda do app. SO VALE QUANDO O JOGO E REABERTO
//       (medido: o driver le o perfil na criacao do device; a restauracao idem). AMD: nao
//       implementado (ADLX/ADL FRTC) - a interface avisa "nao disponivel ainda".
//    3. A CAMADA SE ENCOLHE: work_resolution do Feeder (dlss5-feed.cfg) 100 -> 80 -> 67.
//       Quem grava o cfg e o app (GravarWorkResolution ja existe no main.cpp); este
//       modulo so decide QUANDO. O Feeder rele o cfg e reconstroi ao vivo (medido).
//
//  A ESCADA (Otimizador): mede 5 s em cada degrau e SO desce se a camada continuar
//  abaixo do alvo (30 fps; 60 quando ha uma camada so). Ordem: prioridade de GPU ->
//  resolucao do DLSS 80 -> 67 -> limite do jogo (este por ultimo, porque so vale na
//  reabertura; a escada termina em "aplicado, reabra o jogo"). Degrau que piorou e
//  desfeito. Cada degrau solta um cartaz curto; no fim, um resumo em uma linha.
//
//  "JOGO: IGUAL A PELICULA" (regra do usuario): quando a pelicula tem teto (o 3x trava em
//  30; 1x/2x podem ter 60 pelo painel), o jogo e travado no MESMO teto pelo driver -
//  quadros acima disso so roubam placa da pelicula. Padrao com o 3x ligado; a escolha
//  no painel (Livre / 60 / 30 / Igual) fica no ini. Ver Sincronizar().
//
//  Tudo com log e sem excecao nao tratada; nvapi64.dll ausente = falha silenciosa e a
//  alavanca 2 some da interface.
// =====================================================================================
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <mutex>

namespace Desempenho {

// ------------------------------------------------------------------ log e utilidades
// O host liga o log aqui (main.cpp: Desempenho::g_logFn = [](const char* s){ Log("%s", s); }).
// Sem host (teste isolado) as linhas vao para stderr.
static void (*g_logFn)(const char*) = nullptr;
static void Logf(const char* fmt, ...) {
    char b[1024]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (g_logFn) g_logFn(b); else fprintf(stderr, "[desempenho] %s\n", b);
}
static std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring Minusculas(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
// GetProcAddress sem o aviso -Wcast-function-type do g++ (FARPROC -> assinatura real)
template <class T> static T Proc(HMODULE m, const char* nome) { return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(m, nome))); }

// Nome do exe (so o nome, "re8.exe") do processo dono de uma janela. Vazio se nao der.
// PROCESS_QUERY_LIMITED_INFORMATION e o menor direito possivel: funciona ate em processo
// protegido por anticheat, que so recusa direitos de leitura/escrita de memoria.
static std::wstring ExeDaJanela(HWND h, DWORD* pidOut = nullptr) {
    if (!h) return L"";
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pidOut) *pidOut = pid;
    if (!pid) return L"";
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return L"";
    wchar_t buf[MAX_PATH * 2]; DWORD n = MAX_PATH * 2;
    std::wstring r;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) { r = buf; r = r.substr(r.find_last_of(L"\\/") + 1); }
    CloseHandle(p);
    return r;
}

// ------------------------------------------------------------------ 1. prioridade de GPU
// D3DKMT_SCHEDULINGPRIORITYCLASS: IDLE=0 BELOW_NORMAL=1 NORMAL=2 ABOVE_NORMAL=3 HIGH=4
// REALTIME=5 (REALTIME exige SeIncreaseBasePriorityPrivilege; nao usamos).
enum PrioGpu { PRIO_GPU_IDLE = 0, PRIO_GPU_BELOW = 1, PRIO_GPU_NORMAL = 2, PRIO_GPU_ABOVE = 3, PRIO_GPU_HIGH = 4, PRIO_GPU_REALTIME = 5 };
typedef LONG (WINAPI* PFN_D3DKMTSetProcPrio)(HANDLE, int);
typedef LONG (WINAPI* PFN_D3DKMTGetProcPrio)(HANDLE, int*);
static PFN_D3DKMTSetProcPrio g_kmtSet = nullptr;
static PFN_D3DKMTGetProcPrio g_kmtGet = nullptr;
static bool g_kmtTentado = false;
static bool CarregarKmt() {
    if (g_kmtTentado) return g_kmtSet && g_kmtGet;
    g_kmtTentado = true;
    HMODULE gdi = LoadLibraryW(L"gdi32.dll");
    if (!gdi) return false;
    g_kmtSet = Proc<PFN_D3DKMTSetProcPrio>(gdi, "D3DKMTSetProcessSchedulingPriorityClass");
    g_kmtGet = Proc<PFN_D3DKMTGetProcPrio>(gdi, "D3DKMTGetProcessSchedulingPriorityClass");
    if (!g_kmtSet || !g_kmtGet) Logf("prioridade de gpu: gdi32 sem D3DKMT*ProcessSchedulingPriorityClass (Windows antigo)");
    return g_kmtSet && g_kmtGet;
}
// -1 = nao deu para ler
static int PrioridadeGpuAtual(HANDLE proc = GetCurrentProcess()) {
    if (!CarregarKmt()) return -1;
    int p = -1; LONG st = g_kmtGet(proc, &p);
    return st >= 0 ? p : -1;
}
// Devolve true se aplicou. `anterior` recebe o valor que havia (para desfazer).
static bool PrioridadeGpu(int prio, int* anterior = nullptr, HANDLE proc = GetCurrentProcess()) {
    if (!CarregarKmt()) return false;
    const int antes = PrioridadeGpuAtual(proc);
    if (anterior) *anterior = antes;
    LONG st = g_kmtSet(proc, prio);
    if (st < 0) { Logf("prioridade de gpu: %d -> %d recusado (NTSTATUS 0x%08lX)", antes, prio, (unsigned long)st); return false; }
    Logf("prioridade de gpu: %d -> %d", antes, prio);
    return true;
}
// A MAIOR prioridade que o sistema aceitar para nos: REALTIME primeiro, HIGH se recusar.
// MEDIDO (RELATORIO-DESEMPENHO-JOGO.md): com o jogo saturando a placa, HIGH nao mudou nada
// (pelicula < 1 fps, igual a NORMAL); REALTIME levou a pelicula de 0,9 para 80 fps. E
// REALTIME foi aceito sem elevacao e ate com token de usuario basico (Windows 11 26200,
// HAGS ligado) - a exigencia de privilegio que a documentacao antiga cita nao se
// confirmou aqui; se algum Windows recusar, o fallback HIGH entra sozinho.
// O DWM (o compositor do Windows) roda nessa mesma classe: a pelicula E o caminho da
// imagem ate a tela, faz sentido ela ter a vez do compositor. O jogo nao para: perde a
// fatia que a pelicula usa, e com o teto de fps da pelicula (30/60) essa fatia e pequena.
// RODADA7 (23/09/2026, PLANO-FLUIDEZ item 2 / PESQ-GPU-CONVIVENCIA §0.7): o PADRAO agora e HIGH.
// REALTIME com HAGS deu STALLs de 50-140 ms no jogo (rodada 4) e a OBS abandonou o REALTIME
// com HAGS pelo mesmo motivo. REALTIME so por escolha explicita: `[ultima] prio_gpu=realtime`
// ou DLSS5_PRIO_GPU=realtime/5. NAO ha escalada automatica: a escada do otimizador chama esta
// funcao e recebe a MESMA classe escolhida (se HIGH ja esta, o degrau e pulado).
static bool g_prioRealtimePedido = false;   // definido pelo host ANTES da primeira chamada
static const char* NomePrioGpu(int p) {
    switch (p) { case 0: return "IDLE"; case 1: return "BELOW_NORMAL"; case 2: return "NORMAL";
                 case 3: return "ABOVE_NORMAL"; case 4: return "HIGH"; case 5: return "REALTIME"; }
    return "?";
}
static bool PrioridadeGpuMaxima(int* anterior = nullptr) {
    bool ok = PrioridadeGpu(g_prioRealtimePedido ? PRIO_GPU_REALTIME : PRIO_GPU_HIGH, anterior);
    if (!ok && g_prioRealtimePedido) ok = PrioridadeGpu(PRIO_GPU_HIGH, anterior);   // recusou REALTIME: HIGH (rebaixa, nunca sobe)
    const int ef = PrioridadeGpuAtual();
    Logf("prioridade de gpu EFETIVA: %s (%d)%s", NomePrioGpu(ef), ef,
         g_prioRealtimePedido ? " - REALTIME pedido pela chave prio_gpu/DLSS5_PRIO_GPU" : " - padrao HIGH (REALTIME so pela chave prio_gpu=realtime)");
    return ok;
}

// HAGS ("Agendamento de GPU acelerado por hardware"): SO LEITURA, via D3DKMTQueryAdapterInfo
// WDDM_2_7_CAPS no adaptador do desktop. 1 ligado, 0 desligado, -1 nao soube.
// Nao mudamos isso nunca: e configuracao do sistema da pessoa e exige reiniciar.
static int HagsLigado() {
    struct OpenFromName { WCHAR deviceName[32]; UINT hAdapter; LUID luid; UINT vidPnSourceId; };   // D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME
    struct QueryInfo { UINT hAdapter; int type; void* data; UINT size; };                          // D3DKMT_QUERYADAPTERINFO
    struct CloseAd { UINT hAdapter; };                                                             // D3DKMT_CLOSEADAPTER
    typedef LONG (WINAPI* PFN_Open)(OpenFromName*);
    typedef LONG (WINAPI* PFN_Query)(QueryInfo*);
    typedef LONG (WINAPI* PFN_Close)(CloseAd*);
    HMODULE gdi = LoadLibraryW(L"gdi32.dll");
    if (!gdi) return -1;
    auto open = Proc<PFN_Open>(gdi, "D3DKMTOpenAdapterFromGdiDisplayName");
    auto query = Proc<PFN_Query>(gdi, "D3DKMTQueryAdapterInfo");
    auto close = Proc<PFN_Close>(gdi, "D3DKMTCloseAdapter");
    if (!open || !query || !close) return -1;
    // o monitor principal (\\.\DISPLAYn) leva ao adaptador que desenha o desktop
    OpenFromName o{};
    DISPLAY_DEVICEW dd{}; dd.cb = sizeof dd;
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); i++) {
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) { wcsncpy(o.deviceName, dd.DeviceName, 31); break; }
    }
    if (!o.deviceName[0]) wcscpy(o.deviceName, L"\\\\.\\DISPLAY1");
    int r = -1;
    if (open(&o) >= 0) {
        UINT caps = 0;                                   // D3DKMT_WDDM_2_7_CAPS: bit0 suportado, bit1 ligado, bit2 ligado por padrao
        QueryInfo q{ o.hAdapter, 70 /* KMTQAITYPE_WDDM_2_7_CAPS */, &caps, sizeof caps };
        const LONG st = query(&q);
        if (st >= 0) r = (caps & 2) ? 1 : 0;
        else Logf("hags: D3DKMTQueryAdapterInfo(WDDM_2_7_CAPS) -> 0x%08lX", (unsigned long)st);
        CloseAd c{ o.hAdapter }; close(&c);
    } else Logf("hags: D3DKMTOpenAdapterFromGdiDisplayName(%s) falhou", U8(o.deviceName).c_str());
    return r;
}

// ------------------------------------------------------------------ 2. NvAPI DRS (limite do jogo)
// Declaracoes minimas do que usamos, copiadas da forma publica do nvapi.h (os structs sao
// de layout fixo; a versao e sizeof | (versao << 16), como o MAKE_NVAPI_VERSION oficial).
namespace nv {
typedef int      Status;          // NVAPI_OK = 0
typedef unsigned U32; typedef unsigned short U16; typedef unsigned char Byte;
typedef void*    Sessao; typedef void* Perfil;
enum { UNICODE_MAX = 2048, BIN_MAX = 4096 };
typedef U16 UStr[UNICODE_MAX];
struct Bin { U32 size; Byte data[BIN_MAX]; };
enum { TIPO_DWORD = 0 };
enum { LOCAL_PERFIL_ATUAL = 0, LOCAL_GLOBAL = 1, LOCAL_BASE = 2, LOCAL_PADRAO = 3 };
struct Setting {                  // NVDRS_SETTING_V1
    U32 version; UStr settingName; U32 settingId; U32 settingType; U32 settingLocation;
    U32 isCurrentPredefined; U32 isPredefinedValid;
    union { U32 u32Predefined; Bin binPredefined; UStr wszPredefined; };
    union { U32 u32Current;    Bin binCurrent;    UStr wszCurrent; };
};
struct Profile {                  // NVDRS_PROFILE_V1
    U32 version; UStr profileName; U32 gpuSupport; U32 isPredefined; U32 numOfApps; U32 numOfSettings;
};
struct AppV1 {                    // NVDRS_APPLICATION_V1
    U32 version; U32 isPredefined; UStr appName; UStr userFriendlyName; UStr launcher;
};
struct AppV4 {                    // NVDRS_APPLICATION_V4 (drivers atuais devolvem este)
    U32 version; U32 isPredefined; UStr appName; UStr userFriendlyName; UStr launcher; UStr fileInFolder;
    U32 flags; UStr commandLine;
};
static const U32 SETTING_VER1 = (U32)sizeof(Setting) | (1u << 16);
static const U32 PROFILE_VER1 = (U32)sizeof(Profile) | (1u << 16);
static const U32 APP_VER1     = (U32)sizeof(AppV1)   | (1u << 16);
static const U32 APP_VER4     = (U32)sizeof(AppV4)   | (4u << 16);
static const U32 FRL_FPS_ID   = 0x10835002u;   // "Max Frame Rate" do painel da NVIDIA; 0 = desligado, 1..1023 fps
enum { OK = 0, ERR_NOT_FOUND = -5, SETTING_NOT_FOUND = -166, PROFILE_NOT_FOUND = -163,
       EXECUTABLE_NOT_FOUND = -165, INCOMPATIBLE_STRUCT_VERSION = -9 };

// IDs publicos das funcoes (tabela do nvapi-open-source-sdk / Special K). Se um ID nao
// existir no driver, nvapi_QueryInterface devolve nulo e o modulo se declara indisponivel.
typedef void* (__cdecl* PFN_Query)(unsigned id);
typedef Status (__cdecl* PFN_Init)();
typedef Status (__cdecl* PFN_Unload)();
typedef Status (__cdecl* PFN_ErrMsg)(Status, char msg[64]);
typedef Status (__cdecl* PFN_CreateSession)(Sessao*);
typedef Status (__cdecl* PFN_DestroySession)(Sessao);
typedef Status (__cdecl* PFN_LoadSettings)(Sessao);
typedef Status (__cdecl* PFN_SaveSettings)(Sessao);
typedef Status (__cdecl* PFN_CreateProfile)(Sessao, Profile*, Perfil*);
typedef Status (__cdecl* PFN_DeleteProfile)(Sessao, Perfil);
typedef Status (__cdecl* PFN_FindProfileByName)(Sessao, UStr, Perfil*);
typedef Status (__cdecl* PFN_GetProfileInfo)(Sessao, Perfil, Profile*);
typedef Status (__cdecl* PFN_FindAppByName)(Sessao, UStr, Perfil*, void* app);
typedef Status (__cdecl* PFN_CreateApp)(Sessao, Perfil, void* app);
typedef Status (__cdecl* PFN_GetSetting)(Sessao, Perfil, U32 id, Setting*);
typedef Status (__cdecl* PFN_SetSetting)(Sessao, Perfil, Setting*);
typedef Status (__cdecl* PFN_DeleteSetting)(Sessao, Perfil, U32 id);
typedef Status (__cdecl* PFN_GetBaseProfile)(Sessao, Perfil*);

struct Api {
    HMODULE dll = nullptr; bool ok = false;
    PFN_Init Init = nullptr; PFN_Unload Unload = nullptr; PFN_ErrMsg ErrMsg = nullptr;
    PFN_CreateSession CreateSession = nullptr; PFN_DestroySession DestroySession = nullptr;
    PFN_LoadSettings LoadSettings = nullptr; PFN_SaveSettings SaveSettings = nullptr;
    PFN_CreateProfile CreateProfile = nullptr; PFN_DeleteProfile DeleteProfile = nullptr;
    PFN_FindProfileByName FindProfileByName = nullptr; PFN_GetProfileInfo GetProfileInfo = nullptr;
    PFN_FindAppByName FindAppByName = nullptr; PFN_CreateApp CreateApp = nullptr;
    PFN_GetSetting GetSetting = nullptr; PFN_SetSetting SetSetting = nullptr;
    PFN_DeleteSetting DeleteSetting = nullptr; PFN_GetBaseProfile GetBaseProfile = nullptr;

    // B8 (17/09/2026): o laco (painel) e a thread da conferencia podem pedir a primeira carga ao
    // mesmo tempo. Antes o `tentado` virava true antes de terminar e o segundo lia ok=false ("NvAPI
    // indisponivel" uma vez); agora quem chega junto espera a carga acabar.
    std::once_flag umaVez;
    bool Carregar() { std::call_once(umaVez, [this] { CarregarAgora(); }); return ok; }
    bool CarregarAgora() {
        dll = LoadLibraryW(L"nvapi64.dll");
        if (!dll) { Logf("nvapi: nvapi64.dll nao existe (sem driver NVIDIA) - limite do jogo indisponivel"); return false; }
        auto q = Proc<PFN_Query>(dll, "nvapi_QueryInterface");
        if (!q) { Logf("nvapi: sem nvapi_QueryInterface"); return false; }
        struct { const char* nome; unsigned id; void** dest; } tab[] = {
            { "Initialize",             0x0150E828u, (void**)&Init },
            { "Unload",                 0xD22BDD7Eu, (void**)&Unload },
            { "GetErrorMessage",        0x6C2D048Cu, (void**)&ErrMsg },
            { "DRS_CreateSession",      0x0694D52Eu, (void**)&CreateSession },
            { "DRS_DestroySession",     0xDAD9CFF8u, (void**)&DestroySession },
            { "DRS_LoadSettings",       0x375DBD6Bu, (void**)&LoadSettings },
            { "DRS_SaveSettings",       0xFCBC7E14u, (void**)&SaveSettings },
            { "DRS_CreateProfile",      0xCC176068u, (void**)&CreateProfile },
            { "DRS_DeleteProfile",      0x17093206u, (void**)&DeleteProfile },
            { "DRS_FindProfileByName",  0x7E4A9A0Bu, (void**)&FindProfileByName },
            { "DRS_GetProfileInfo",     0x61CD6FD6u, (void**)&GetProfileInfo },
            { "DRS_FindApplicationByName", 0xEEE566B2u, (void**)&FindAppByName },
            { "DRS_CreateApplication",  0x4347A9DEu, (void**)&CreateApp },
            { "DRS_GetSetting",         0x73BF8338u, (void**)&GetSetting },
            { "DRS_SetSetting",         0x577DD202u, (void**)&SetSetting },
            { "DRS_DeleteProfileSetting", 0xE4A26362u, (void**)&DeleteSetting },
            { "DRS_GetBaseProfile",     0xDA8466A0u, (void**)&GetBaseProfile },
        };
        for (auto& t : tab) {
            *t.dest = q(t.id);
            if (!*t.dest) { Logf("nvapi: funcao %s (0x%08X) nao existe neste driver - limite do jogo indisponivel", t.nome, t.id); return false; }
        }
        Status st = Init();
        if (st != OK) { Logf("nvapi: NvAPI_Initialize -> %d (sem placa NVIDIA ativa?)", st); return false; }
        ok = true;
        Logf("nvapi: carregada (17 funcoes DRS resolvidas)");
        return true;
    }
    std::string Erro(Status st) { char m[64] = {}; if (ErrMsg && ErrMsg(st, m) == OK && m[0]) return m; char b[32]; snprintf(b, sizeof b, "erro %d", st); return b; }
};
static Api g_api;

static void ParaUStr(UStr dest, const std::wstring& s) {
    size_t n = s.size() < UNICODE_MAX - 1 ? s.size() : UNICODE_MAX - 1;
    for (size_t i = 0; i < n; i++) dest[i] = (U16)s[i];
    dest[n] = 0;
}
static std::wstring DeUStr(const U16* s) { std::wstring r; for (size_t i = 0; i < UNICODE_MAX && s[i]; i++) r += (wchar_t)s[i]; return r; }

// Uma sessao DRS aberta e carregada. Vive so o tempo de uma operacao: a base de
// perfis muda por fora (painel da NVIDIA, Profile Inspector) e uma sessao velha estaria
// desatualizada.
// B2 (16/09/2026): a conferencia do perfil roda numa thread (Desempenho::g_conf); B5: a gravacao
// do limite tambem. A trava (recursiva) impede duas sessoes DRS ao mesmo tempo e serializa quem
// mexe em g_lim/g_jan (LimiteJogoAplicar/Restaurar a seguram do comeco ao fim).
struct TravaDrs { CRITICAL_SECTION cs; TravaDrs() { InitializeCriticalSection(&cs); } };
static TravaDrs g_travaDrs;
// B5: segura uma CRITICAL_SECTION pelo escopo.
struct Segura { CRITICAL_SECTION* c; explicit Segura(CRITICAL_SECTION& x) : c(&x) { EnterCriticalSection(c); } ~Segura() { LeaveCriticalSection(c); } };
struct Sessao_ {
    Sessao s = nullptr; bool ok = false;
    Sessao_() {
        EnterCriticalSection(&g_travaDrs.cs);
        if (!g_api.Carregar()) return;
        Status st = g_api.CreateSession(&s);
        if (st != OK) { Logf("nvapi: CreateSession -> %s", g_api.Erro(st).c_str()); return; }
        st = g_api.LoadSettings(s);
        if (st != OK) { Logf("nvapi: LoadSettings -> %s", g_api.Erro(st).c_str()); return; }
        ok = true;
    }
    ~Sessao_() { if (s) g_api.DestroySession(s); LeaveCriticalSection(&g_travaDrs.cs); }
    bool Salvar() {
        Status st = g_api.SaveSettings(s);
        if (st != OK) Logf("nvapi: SaveSettings -> %s", g_api.Erro(st).c_str());
        return st == OK;
    }
    // O perfil que o driver usa para este exe (o que o painel da NVIDIA mostra em
    // "Configuracoes de programa"). criado=true quando tivemos de criar um.
    Perfil PerfilDoExe(const std::wstring& exe, bool criarSeFaltar, bool* criado, std::wstring* nomePerfil) {
        if (criado) *criado = false;
        UStr nome; ParaUStr(nome, exe);
        Perfil p = nullptr;
        AppV4 a4{}; a4.version = APP_VER4;
        Status st = g_api.FindAppByName(s, nome, &p, &a4);
        if (st == INCOMPATIBLE_STRUCT_VERSION) { AppV1 a1{}; a1.version = APP_VER1; st = g_api.FindAppByName(s, nome, &p, &a1); }
        if (st == OK && p) {
            if (nomePerfil) { Profile pi{}; pi.version = PROFILE_VER1; if (g_api.GetProfileInfo(s, p, &pi) == OK) *nomePerfil = DeUStr(pi.profileName); }
            return p;
        }
        if (st != EXECUTABLE_NOT_FOUND && st != ERR_NOT_FOUND && st != PROFILE_NOT_FOUND)
            Logf("nvapi: FindApplicationByName(%s) -> %s", U8(exe).c_str(), g_api.Erro(st).c_str());
        if (!criarSeFaltar) return nullptr;
        // Sem perfil: cria um com o nome do exe (igual ao que o painel da NVIDIA faz ao
        // "Adicionar" um programa) e amarra o exe a ele.
        const std::wstring nomeP = L"DLSS5ForAll - " + exe;
        Profile pi{}; pi.version = PROFILE_VER1; ParaUStr(pi.profileName, nomeP);
        st = g_api.CreateProfile(s, &pi, &p);
        if (st != OK) {
            // pode ja existir de uma sessao anterior interrompida: reaproveita
            UStr np; ParaUStr(np, nomeP);
            if (g_api.FindProfileByName(s, np, &p) != OK) { Logf("nvapi: CreateProfile(%s) -> %s", U8(nomeP).c_str(), g_api.Erro(st).c_str()); return nullptr; }
        }
        AppV1 a1{}; a1.version = APP_VER1; ParaUStr(a1.appName, exe);
        st = g_api.CreateApp(s, p, &a1);
        if (st == INCOMPATIBLE_STRUCT_VERSION) { a4 = AppV4{}; a4.version = APP_VER4; ParaUStr(a4.appName, exe); st = g_api.CreateApp(s, p, &a4); }
        if (st != OK) { Logf("nvapi: CreateApplication(%s) -> %s", U8(exe).c_str(), g_api.Erro(st).c_str()); return nullptr; }
        if (criado) *criado = true;
        if (nomePerfil) *nomePerfil = nomeP;
        Logf("nvapi: perfil '%s' criado para %s", U8(nomeP).c_str(), U8(exe).c_str());
        return p;
    }
    // Limite gravado NESTE perfil: -1 = nao ha (herda do global), senao 0..1023.
    int LerFrl(Perfil p) {
        Setting st{}; st.version = SETTING_VER1;
        Status r = g_api.GetSetting(s, p, FRL_FPS_ID, &st);
        if (r != OK) return -1;
        if (st.settingLocation != LOCAL_PERFIL_ATUAL) return -1;   // veio herdado, nao e deste perfil
        return (int)st.u32Current;
    }
    bool GravarFrl(Perfil p, int fps) {
        Setting st{}; st.version = SETTING_VER1; st.settingId = FRL_FPS_ID; st.settingType = TIPO_DWORD;
        st.u32Current = (U32)fps;
        Status r = g_api.SetSetting(s, p, &st);
        if (r != OK) Logf("nvapi: SetSetting(FRL=%d) -> %s", fps, g_api.Erro(r).c_str());
        return r == OK;
    }
    bool ApagarFrl(Perfil p) {
        Status r = g_api.DeleteSetting(s, p, FRL_FPS_ID);
        if (r != OK && r != SETTING_NOT_FOUND) { Logf("nvapi: DeleteProfileSetting(FRL) -> %s", g_api.Erro(r).c_str()); return false; }
        return true;
    }
};
} // namespace nv

// O que este modulo guarda no dlss5forall.ini, secao [desempenho], ENQUANTO um limite
// nosso esta aplicado. Vazio = nada a restaurar. Escrito ANTES de tocar no driver.
//   exe=re8.exe            o exe cujo perfil alteramos
//   original=-1|N          -1: o perfil nao tinha FRL (restaurar = apagar a chave)
//   perfil_criado=0|1      1: o perfil inteiro e nosso (restaurar = apagar o perfil)
//   aplicado=60|30         o que gravamos
static std::wstring g_iniPath;
static void IniSetD(const wchar_t* k, const std::wstring& v) { if (!g_iniPath.empty()) WritePrivateProfileStringW(L"desempenho", k, v.empty() ? nullptr : v.c_str(), g_iniPath.c_str()); }
static std::wstring IniGetD(const wchar_t* k) { wchar_t b[512] = {}; if (!g_iniPath.empty()) GetPrivateProfileStringW(L"desempenho", k, L"", b, 512, g_iniPath.c_str()); return b; }
// =====================================================================================
//  PARTE B (Rodada 21, 24/09/2026): "DESEMPENHO MAXIMO" - chave nova no submenu Avancado
//  do painel (main.cpp, E_DESEMPMAX), ligada por padrao, valendo ENQUANTO A PELICULA
//  ESTIVER ATIVA. O main chama Max::PeliculaLigou() uma vez, ao validar a fonte e comecar a
//  processar de verdade (o mesmo lugar de sempre de Desempenho::Iniciar(), e so na CAMADA 1 -
//  MESMA REGRA do limite do jogo: duas camadas escrevendo no mesmo perfil do driver desfariam
//  uma a gravacao da outra). O desligamento e o Restaurar() chamado por Desempenho::Encerrar()
//  (saida normal) e EmergenciaRestaurar() (crash) - nao ha um hook por "a pelicula parou no
//  meio da sessao", que teria de cobrir todo caminho de duas placas/uma placa/Nativo e nao
//  valeria o risco de esquecer algum. Duas alavancas INDEPENDENTES - uma pode falhar sem afetar a outra:
//
//    1. GPU: NVAPI DRS, SO no perfil do NOSSO exe (nunca o do jogo nem o perfil global) -
//       reusa a mesma sessao/perfil de PerfilDoExe(), so que o exe pedido e sempre o nosso
//       proprio. Grava PREFERRED_PSTATE_ID (0x1057EB71, conferido no NvApiDriverSettings.h
//       oficial da NVIDIA, github.com/NVIDIA/nvapi) = PREFERRED_PSTATE_PREFER_MAX (1)
//       ("Preferir desempenho maximo" no Painel da NVIDIA). Desligar apaga so essa chave do
//       nosso perfil. Sem NVIDIA (nvapi64.dll ausente), falha silenciosa - so a CPU muda.
//       Se GRAVAR pedir administrador (perfil novo, num Windows que restringe a escrita do
//       DRS), o app NAO se eleva: fica no log e numa linha discreta no painel (PrecisaAdmin()).
//    2. CPU: SetProcessInformation(ProcessPowerThrottling) tira o EcoQoS/throttling do
//       NOSSO processo (sem admin) e timeBeginPeriod(1) (winmm carregado em tempo de
//       execucao, como o despertador do apresentador em apresentador_gpu.h - winmm nao
//       entra no link). Os dois desfazem sozinhos com o processo, mas sao desfeitos
//       explicitamente ao desligar a chave ou ao a pelicula parar, para nao segurar o
//       Windows num estado "sem economia de energia" o tempo todo, so enquanto vale.
// =====================================================================================
namespace Max {
    static const nv::U32 kPreferredPstateId = 0x1057EB71u;
    enum { kPreferMax = 1 };

    static bool g_gpuAplicado = false;   // true = o NOSSO perfil tem PREFERRED_PSTATE=PREFER_MAX gravado agora
    static bool g_cpuAplicado = false;   // true = throttling desligado + timeBeginPeriod(1) pedidos nesta sessao
    static bool g_pelicula    = false;   // a pelicula esta de pe agora? (g_apres.Iniciar/Parar)
    // PRECISA DE ADMIN PARA GRAVAR NO DRIVER? So um bool aqui (sem texto pronto): quem monta a
    // frase bilingue e o painel (main.cpp, que tem T()/TS()) - este modulo e header-only e
    // compila sozinho sem idioma.h (ver o comentario grande no topo do arquivo), entao nao
    // pode chamar TS() aqui dentro.
    static bool g_precisaAdmin = false;

    static HMODULE g_winmm = nullptr;
    typedef UINT (WINAPI *PfnPeriodo)(UINT);
    static PfnPeriodo g_timeBeginPeriod = nullptr, g_timeEndPeriod = nullptr;

    static std::wstring NomeDoNossoExe() {
        wchar_t buf[MAX_PATH]{};
        const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (!n) return std::wstring();
        std::wstring caminho(buf, n < MAX_PATH ? n : MAX_PATH - 1);
        const size_t b = caminho.find_last_of(L"\\/");
        return (b == std::wstring::npos) ? caminho : caminho.substr(b + 1);
    }

    // Grava (ligar=true) ou apaga (ligar=false) PREFERRED_PSTATE no perfil do NOSSO exe.
    static bool GpuAplicar(bool ligar) {
        const std::wstring exe = NomeDoNossoExe();
        if (exe.empty()) { Logf("desempenho maximo: nao consegui o nome do proprio exe - GPU nao ajustada"); return false; }
        nv::Segura trava(nv::g_travaDrs.cs);   // a mesma trava do limite do jogo: uma sessao DRS por vez (recursiva, mesmo thread)
        nv::Sessao_ s;
        if (!s.ok) { if (ligar) Logf("desempenho maximo: nvapi indisponivel (sem placa NVIDIA?) - so a CPU foi ajustada"); return false; }
        bool criado = false; std::wstring nomePerfil;
        nv::Perfil p = s.PerfilDoExe(exe, /*criarSeFaltar=*/ligar, &criado, &nomePerfil);
        if (!p) {
            if (ligar) Logf("desempenho maximo: sem perfil de driver para %s - nada gravado", U8(exe).c_str());
            return false;
        }
        bool ok;
        if (ligar) {
            nv::Setting st{}; st.version = nv::SETTING_VER1; st.settingId = kPreferredPstateId;
            st.settingType = nv::TIPO_DWORD; st.u32Current = (nv::U32)kPreferMax;
            const nv::Status r = nv::g_api.SetSetting(s.s, p, &st);
            ok = (r == nv::OK);
            if (!ok) Logf("desempenho maximo: SetSetting(PREFERRED_PSTATE) -> %s", nv::g_api.Erro(r).c_str());
        } else {
            const nv::Status r = nv::g_api.DeleteSetting(s.s, p, kPreferredPstateId);
            ok = (r == nv::OK || r == nv::SETTING_NOT_FOUND);
        }
        const bool salvou = s.Salvar();   // Sessao_::Salvar ja loga o erro do SaveSettings, se houver
        if (!salvou) {
            // Medido: SaveSettings pede administrador quando o perfil e CRIADO agora e o
            // Windows restringe a escrita do DRS - nao elevamos o app inteiro por isto.
            g_precisaAdmin = true;
            Logf("desempenho maximo: gravar no driver pediu mais privilegio do que o app tem - "
                 "ative \"Preferir desempenho maximo\" para %s no Painel de Controle NVIDIA", U8(exe).c_str());
            return false;
        }
        if (ligar && ok) g_precisaAdmin = false;
        Logf("desempenho maximo: GPU (perfil '%s' de %s) PREFERRED_PSTATE %s", U8(nomePerfil.empty() ? exe : nomePerfil).c_str(),
             U8(exe).c_str(), ligar ? "= preferir desempenho maximo" : "removido");
        return ok;
    }

    static void CpuAplicar(bool ligar) {
        PROCESS_POWER_THROTTLING_STATE pt{};
        pt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        pt.StateMask = ligar ? 0ul : pt.ControlMask;   // StateMask 0 nas duas alavancas = throttling DESLIGADO
        const BOOL ok = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof(pt));
        if (!ok) Logf("desempenho maximo: SetProcessInformation(ProcessPowerThrottling) falhou (erro %lu)", (unsigned long)GetLastError());
        if (ligar) {
            if (!g_winmm) g_winmm = LoadLibraryW(L"winmm.dll");
            if (g_winmm && !g_timeBeginPeriod) g_timeBeginPeriod = (PfnPeriodo)(void*)GetProcAddress(g_winmm, "timeBeginPeriod");
            if (g_winmm && !g_timeEndPeriod)   g_timeEndPeriod   = (PfnPeriodo)(void*)GetProcAddress(g_winmm, "timeEndPeriod");
            if (g_timeBeginPeriod) g_timeBeginPeriod(1);
        } else if (g_timeEndPeriod) g_timeEndPeriod(1);
        Logf("desempenho maximo: CPU power throttling %s%s", ligar ? "DESLIGADO" : "religado ao padrao do Windows",
             ligar ? (g_timeBeginPeriod ? " + timeBeginPeriod(1)" : " (sem winmm: timeBeginPeriod indisponivel)") : "");
    }

    static bool ChaveLigada() { return IniGetD(L"desempenho_maximo") != L"0"; }   // ausente = ligado (padrao)
    // Para o painel montar a linha discreta (bilingue, com T()/TS()): so dados, sem texto pronto.
    static bool PrecisaAdmin() { return g_precisaAdmin; }
    static std::wstring NossoExe() { return NomeDoNossoExe(); }
    static bool GpuAtiva() { return g_gpuAplicado; }
    static bool CpuAtiva() { return g_cpuAplicado; }

    // Aplica ou desfaz as duas alavancas conforme "a pelicula esta de pe" x "a chave esta ligada".
    static void Sincronizar() {
        const bool quer = g_pelicula && ChaveLigada();
        if (quer != g_cpuAplicado) { CpuAplicar(quer); g_cpuAplicado = quer; }
        if (quer != g_gpuAplicado) g_gpuAplicado = GpuAplicar(quer) ? quer : g_gpuAplicado;
    }
    static void PeliculaLigou()    { g_pelicula = true;  Sincronizar(); }
    // O painel: a pessoa ligou/desligou a chave "Desempenho maximo" (E_DESEMPMAX).
    static void ChaveMudou(bool ligar) { IniSetD(L"desempenho_maximo", ligar ? L"1" : L"0"); Sincronizar(); }
    // Encerrar/crash: garante que nada fica gravado no driver nem no processo depois que o app sai.
    static void Restaurar() {
        if (g_cpuAplicado) { CpuAplicar(false); g_cpuAplicado = false; }
        if (g_gpuAplicado) { GpuAplicar(false); g_gpuAplicado = false; }
    }
} // namespace Max

struct LimiteJogo {
    std::wstring exe;            // vazio = nenhum limite nosso aplicado
    int original = -1;           // -1 = nao havia FRL no perfil
    bool perfilCriado = false;
    int aplicado = 0;
    // QUAL PROCESSO DO JOGO ESTAVA ABERTO QUANDO APLICAMOS. E o que permite saber se o limite
    // ja esta valendo: o driver le o perfil quando o jogo cria o device, entao o limite so vale
    // a partir do PROXIMO processo. Mesmo exe com pid diferente = o jogo foi reaberto = valendo.
    // Vai para o ini junto com o resto, e sobrevive a uma queda do app: se ao reabrir o pid
    // gravado nao for mais o do alvo, o jogo foi reaberto no meio tempo e o limite pegou.
    // 0 = nao sabemos qual era (limite antigo, de antes desta versao) - nesse caso nao
    // afirmamos nada, ver EstadoDoLimite().
    unsigned long pidAoAplicar = 0;
};
static LimiteJogo g_lim;
// B5: g_lim/g_jan sao escritos pela thread da conferencia (sob a trava DRS) e lidos pelo painel no
// laco sem ela. Esta trava so cobre a troca dos campos (microssegundos), nunca uma chamada ao driver:
// quem escreve a segura so na atribuicao; o laco a segura para copiar (EstadoDoLimite).
struct TravaDados { CRITICAL_SECTION cs; TravaDados() { InitializeCriticalSection(&cs); } };
static TravaDados g_travaDados;

// Em que pe esta o limite que aplicamos. A funcao EstadoDoLimite() fica logo depois de
// kLimiteAoVivo, mais abaixo, porque depende dela.
enum EstadoLimite {
    LIM_NENHUM = 0,     // nao ha limite nosso
    LIM_PENDENTE,       // aplicado, mas o jogo ainda e o MESMO processo: so vale ao reabrir
    LIM_VALENDO,        // o jogo foi reaberto depois de aplicarmos: o driver ja o esta limitando
    LIM_INDEFINIDO,     // ha limite, mas nao da para afirmar em que pe (sem pid de um dos lados)
    LIM_HERDADO,        // NAO ha limite nosso agora, mas o processo vivo do jogo NASCEU com a chave:
                        // o driver o segura ate ele ser reaberto. "Livre" aqui seria mentira (14/09).
};

static void LimiteGravarIni() {
    IniSetD(L"exe", g_lim.exe);
    IniSetD(L"original", g_lim.exe.empty() ? L"" : std::to_wstring(g_lim.original));
    IniSetD(L"perfil_criado", g_lim.exe.empty() ? L"" : (g_lim.perfilCriado ? L"1" : L"0"));
    IniSetD(L"aplicado", g_lim.exe.empty() ? L"" : std::to_wstring(g_lim.aplicado));
    IniSetD(L"pid_ao_aplicar", g_lim.exe.empty() ? L"" : std::to_wstring(g_lim.pidAoAplicar));
}
static void LimiteLerIni() {
    g_lim = LimiteJogo{};
    g_lim.exe = IniGetD(L"exe");
    if (g_lim.exe.empty()) return;
    g_lim.original = _wtoi(IniGetD(L"original").c_str());
    g_lim.perfilCriado = IniGetD(L"perfil_criado") == L"1";
    g_lim.aplicado = _wtoi(IniGetD(L"aplicado").c_str());
    g_lim.pidAoAplicar = (unsigned long)_wcstoui64(IniGetD(L"pid_ao_aplicar").c_str(), nullptr, 10);
}

// A JANELA EM QUE A CHAVE EXISTIU no perfil do driver - e SOBREVIVE a restauracao (14/09/2026).
// Motivo: a chave so pega/solta quando o jogo abre; um jogo aberto enquanto ela existia nasce
// limitado pela vida inteira do processo, mesmo depois de apagarmos a chave. Sem esta memoria o
// app zerava g_lim ao restaurar e dizia "livre" com o driver ainda em 30 (pid 7584, 14/09).
// [desempenho] ultimo_exe / ultimo_fps / ultimo_de / ultimo_ate (FILETIME em decimal;
// ultimo_ate=0 = a chave ainda esta la) / ultimo_pid (o pid vivo quando aplicamos: esse NAO
// nasceu com a chave). `aplicado_em` no ini e o mesmo ultimo_de, gravado ANTES do driver.
struct JanelaLimite { std::wstring exe; int fps = 0; unsigned long long de = 0, ate = 0; unsigned long pid = 0; };
static JanelaLimite g_jan;
static unsigned long long AgoraFt() { FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime; return u.QuadPart; }
static std::string HoraLocal(unsigned long long ft) {
    if (!ft) return "?";
    ULARGE_INTEGER u; u.QuadPart = ft; FILETIME f; f.dwLowDateTime = u.LowPart; f.dwHighDateTime = u.HighPart;
    FILETIME l; SYSTEMTIME st; if (!FileTimeToLocalFileTime(&f, &l) || !FileTimeToSystemTime(&l, &st)) return "?";
    char b[16]; snprintf(b, sizeof b, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond); return b;
}
static void JanelaGravarIni() {
    IniSetD(L"ultimo_exe", g_jan.exe);
    IniSetD(L"ultimo_fps", g_jan.exe.empty() ? L"" : std::to_wstring(g_jan.fps));
    IniSetD(L"ultimo_de",  g_jan.exe.empty() ? L"" : std::to_wstring(g_jan.de));
    IniSetD(L"ultimo_ate", g_jan.exe.empty() ? L"" : std::to_wstring(g_jan.ate));
    IniSetD(L"ultimo_pid", g_jan.exe.empty() ? L"" : std::to_wstring(g_jan.pid));
    IniSetD(L"aplicado_em", g_lim.exe.empty() ? L"" : std::to_wstring(g_jan.de));
}
static void JanelaLerIni() {
    g_jan = JanelaLimite{};
    g_jan.exe = IniGetD(L"ultimo_exe");
    if (g_jan.exe.empty()) return;
    g_jan.fps = _wtoi(IniGetD(L"ultimo_fps").c_str());
    g_jan.de  = _wcstoui64(IniGetD(L"ultimo_de").c_str(), nullptr, 10);
    g_jan.ate = _wcstoui64(IniGetD(L"ultimo_ate").c_str(), nullptr, 10);
    g_jan.pid = (unsigned long)_wcstoui64(IniGetD(L"ultimo_pid").c_str(), nullptr, 10);
}
// Hora de criacao do processo (FILETIME). 0 = nao deu para ler.
static unsigned long long HoraCriacao(unsigned long pid) {
    if (!pid) return 0;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return 0;
    FILETIME c{}, e{}, k{}, u{}; unsigned long long r = 0;
    if (GetProcessTimes(p, &c, &e, &k, &u)) { ULARGE_INTEGER x; x.LowPart = c.dwLowDateTime; x.HighPart = c.dwHighDateTime; r = x.QuadPart; }
    CloseHandle(p);
    return r;
}
static std::wstring ExeDoPid(unsigned long pid) {
    HANDLE p = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
    if (!p) return L"";
    wchar_t buf[MAX_PATH * 2]; DWORD n = MAX_PATH * 2; std::wstring r;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) { r = buf; r = r.substr(r.find_last_of(L"\\/") + 1); }
    CloseHandle(p);
    return r;
}
// O processo VIVO do jogo nasceu enquanto a chave existia no perfil? (resposta cacheada por pid:
// e chamada a cada repintura do painel). O pid que ja estava aberto quando aplicamos nao conta.
// B5: recebe uma copia de g_jan (tirada sob g_travaDados), nao le o global.
static bool NasceuLimitado(unsigned long pid, const JanelaLimite& jan) {
    static unsigned long cPid = 0; static bool cRes = false; static unsigned long long cAte = 0;
    if (pid == cPid && cAte == jan.ate) return cRes;
    cPid = pid; cAte = jan.ate; cRes = false;
    if (!pid || jan.exe.empty() || !jan.de || pid == jan.pid) return false;
    if (Minusculas(ExeDoPid(pid)) != Minusculas(jan.exe)) return false;
    const unsigned long long c = HoraCriacao(pid);
    if (!c) return false;
    cRes = c >= jan.de && (jan.ate == 0 || c <= jan.ate);
    return cRes;
}

// Ha driver NVIDIA com NvAPI funcional? (decide se os chips "Jogo: 60/30" aparecem)
static bool LimiteJogoDisponivel() { return nv::g_api.Carregar(); }
// AMD: FRTC/Chill via ADLX ainda nao implementado. A interface mostra "nao disponivel
// ainda" para vendor 0x1002. (Ver RELATORIO-DESEMPENHO-JOGO.md, secao AMD.)
static bool LimiteJogoDisponivelAmd() { return false; }

// Le o limite que o driver aplica HOJE ao exe (o do perfil dele; -1 = nenhum no perfil).
static int LimiteJogoAtual(const std::wstring& exe) {
    nv::Sessao_ s; if (!s.ok) return -1;
    nv::Perfil p = s.PerfilDoExe(exe, false, nullptr, nullptr);
    return p ? s.LerFrl(p) : -1;
}

// Restaura o que havia antes de nos (idempotente; usa o que esta no ini, entao funciona
// tambem na abertura seguinte a uma queda). true = nada pendente ou restaurado.
static bool LimiteJogoRestaurar() {
    nv::Segura trava(nv::g_travaDrs.cs);   // B5: a thread da conferencia (chip, sincDesempenho, e desde o B6 o otimizador) e o laco (saida) chegam aqui
    if (g_lim.exe.empty()) return true;
    nv::Sessao_ s; if (!s.ok) { Logf("limite do jogo: nvapi indisponivel - %s continua com %d fps no driver", U8(g_lim.exe).c_str(), g_lim.aplicado); return false; }
    nv::Perfil p = s.PerfilDoExe(g_lim.exe, false, nullptr, nullptr);
    bool ok = true;
    if (!p) {
        Logf("limite do jogo: o perfil de %s ja nao existe - nada a restaurar", U8(g_lim.exe).c_str());
    } else if (g_lim.perfilCriado) {
        nv::Status r = nv::g_api.DeleteProfile(s.s, p);
        ok = (r == nv::OK);
        if (!ok) Logf("nvapi: DeleteProfile -> %s", nv::g_api.Erro(r).c_str());
    } else if (g_lim.original < 0) {
        ok = s.ApagarFrl(p);
    } else {
        ok = s.GravarFrl(p, g_lim.original);
    }
    if (ok) ok = s.Salvar();
    if (ok) {
        Logf("limite do jogo: %s restaurado (%s)", U8(g_lim.exe).c_str(),
             g_lim.perfilCriado ? "perfil nosso apagado" : g_lim.original < 0 ? "chave apagada" : "valor original de volta");
        {
            nv::Segura dados(g_travaDados.cs);
            g_lim = LimiteJogo{};
            // A janela fecha AGORA, mas a memoria dela fica: quem nasceu dentro dela segue limitado.
            if (!g_jan.exe.empty() && g_jan.ate == 0) g_jan.ate = AgoraFt();
        }
        LimiteGravarIni();
        JanelaGravarIni();
    }
    return ok;
}

// O GUARDIAO (14/09/2026): uma copia minima deste exe ("--guardiao <pid> <ini>") que so espera o
// app morrer e, se o ini ainda disser que ha chave nossa no driver, restaura. Cobre Fatal(),
// crash, taskkill e Gerenciador de Tarefas - todos os caminhos em que Encerrar() nao roda. Na
// saida normal o app restaura antes, o ini fica vazio e o guardiao sai calado. Lancado uma vez
// por vida do app, na primeira gravacao. Desligamento do Windows mata os dois: fica para a
// varredura do arranque seguinte (Iniciar). Ver Guardiao(), mais abaixo.
static void LancarGuardiao() {
    static bool lancado = false;
    if (lancado || g_iniPath.empty()) return;
    lancado = true;
    wchar_t exe[MAX_PATH] = {}; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --guardiao " + std::to_wstring((unsigned long)GetCurrentProcessId()) + L" \"" + g_iniPath + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        Logf("limite do jogo: guardiao lancado (pid %lu) - restaura o perfil se este app morrer sem fechar direito", (unsigned long)pi.dwProcessId);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    } else {
        Logf("limite do jogo: nao deu para lancar o guardiao (erro %lu) - so a proxima abertura do app restaura se este morrer", (unsigned long)GetLastError());
    }
}

// Aplica fps (60/30; 0 = "livre" = restaurar) ao exe. Guarda o original na primeira vez.
static bool LimiteJogoAplicar(const std::wstring& exe, int fps, std::wstring* erro = nullptr,
                              unsigned long pidAlvo = 0) {
    if (fps <= 0) return LimiteJogoRestaurar();
    if (exe.empty()) { if (erro) *erro = L"sem exe do alvo"; return false; }
    nv::Segura trava(nv::g_travaDrs.cs);   // B5: ver LimiteJogoRestaurar
    // Trocou de jogo com um limite nosso pendurado no anterior? Devolve o anterior primeiro.
    if (!g_lim.exe.empty() && Minusculas(g_lim.exe) != Minusculas(exe)) LimiteJogoRestaurar();
    nv::Sessao_ s;
    if (!s.ok) { if (erro) *erro = L"NvAPI indisponivel"; return false; }
    bool criado = false; std::wstring nomePerfil;
    nv::Perfil p = s.PerfilDoExe(exe, true, &criado, &nomePerfil);
    if (!p) { if (erro) *erro = L"nao achei nem criei o perfil do jogo no driver"; return false; }
    if (g_lim.exe.empty()) {
        const int original = criado ? -1 : s.LerFrl(p);
        {
            nv::Segura dados(g_travaDados.cs);
            g_lim.exe = exe; g_lim.perfilCriado = criado; g_lim.original = original;
            g_lim.aplicado = fps;
            g_jan = JanelaLimite{}; g_jan.exe = exe; g_jan.fps = fps; g_jan.de = AgoraFt(); g_jan.pid = pidAlvo;
        }
        LimiteGravarIni();          // ANTES de gravar no driver: se cairmos agora, a proxima abertura desfaz
        JanelaGravarIni();
        LancarGuardiao();           // ...e se cairmos de vez, o guardiao desfaz
    }
    // O pid vale para ESTA aplicacao, mesmo trocando so o valor (60 -> 30) sem soltar o limite:
    // o novo valor tambem so pega na proxima abertura, entao a pendencia recomeca daqui.
    { nv::Segura dados(g_travaDados.cs); g_lim.pidAoAplicar = pidAlvo; }
    if (!s.GravarFrl(p, fps)) { if (erro) *erro = L"o driver recusou o limite"; return false; }
    if (!s.Salvar()) { if (erro) *erro = L"o driver nao salvou o perfil"; return false; }
    { nv::Segura dados(g_travaDados.cs); g_lim.aplicado = fps; g_jan.fps = fps; }
    LimiteGravarIni();
    JanelaGravarIni();
    Logf("limite do jogo: %s -> %d fps no perfil '%s' (original %d%s)", U8(exe).c_str(), fps, U8(nomePerfil).c_str(),
         g_lim.original, g_lim.perfilCriado ? ", perfil criado por nos" : "");
    return true;
}

// ------------------------------------------------------------------ o que foi MEDIDO (parte A)
// Medido em 2026-09-05/06 nesta maquina (RTX 4080, driver 596.49, HAGS ligado) com um jogo
// D3D11 de teste; detalhes em docs\pesquisa-2026-09-06\RELATORIO-DESEMPENHO-JOGO.md.
//   - O FRL do perfil NAO pega num jogo ja aberto: o driver le o perfil quando o jogo cria o
//     device. Vale na PROXIMA abertura. A restauracao idem: o jogo continua limitado ate
//     ser reaberto. Por isso toda mensagem sobre o limite diz "quando o jogo for reaberto".
//   - O Feeder rele o dlss5-feed.cfg com a camada aberta e reconstroi a feature quando
//     work_resolution muda: esse degrau vale AO VIVO.
static const bool kLimiteAoVivo  = false;
static const bool kWorkResAoVivo = true;

// Em que pe esta o limite que aplicamos. `pidAlvo` e o pid do jogo AGORA (0 = nao sabemos).
// Mora aqui, e nao junto do struct LimiteJogo, porque depende do kLimiteAoVivo acima.
// B5: chamada pelo laco enquanto a thread da conferencia pode estar gravando: copia sob g_travaDados.
static int EstadoDoLimite(unsigned long pidAlvo) {
    bool semLimite; unsigned long pidAoAplicar; JanelaLimite jan;
    {
        nv::Segura dados(g_travaDados.cs);
        semLimite = g_lim.exe.empty() || g_lim.aplicado <= 0;
        pidAoAplicar = g_lim.pidAoAplicar;
        if (semLimite) jan = g_jan;
    }
    if (semLimite) return NasceuLimitado(pidAlvo, jan) ? LIM_HERDADO : LIM_NENHUM;
    if (kLimiteAoVivo) return LIM_VALENDO;                 // se um dia pegar ao vivo, nao ha pendencia
    if (!pidAoAplicar || !pidAlvo) return LIM_INDEFINIDO;
    return (pidAlvo == pidAoAplicar) ? LIM_PENDENTE : LIM_VALENDO;
}

// Navegadores: o video no Edge/Chrome/Firefox e composto pelo processo de GPU do
// navegador, que e compartilhado por todas as abas e ja anda travado no refresh do
// monitor pelo DWM. Nao aplicamos limite do driver a eles (mexeria no perfil do navegador
// da pessoa por causa de uma aba) e o painel diz "sem limite disponivel".
static bool EhNavegador(const std::wstring& exe) {
    const std::wstring e = Minusculas(exe);
    for (const wchar_t* n : { L"msedge.exe", L"chrome.exe", L"firefox.exe", L"brave.exe", L"opera.exe", L"vivaldi.exe", L"msedgewebview2.exe" })
        if (e == n) return true;
    return false;
}

// ------------------------------------------------------------------ o modo escolhido no painel
// Os chips "Jogo: Livre / 60 / 30 / Igual a pelicula". IGUAL segue o teto de fps da camada
// (o 3x trava a camada em 30; 2x/1x podem ter 60 pelo painel) - quadros do jogo acima do
// teto so roubam placa da pelicula. E o padrao quando o 3x esta ligado; a escolha da pessoa
// (qualquer chip) fica no ini e vale dali em diante.
// UM CONTROLE, DOIS MOTORES. A pessoa escolhe o NUMERO, que e o que ela quer; o programa
// escolhe o meio (a peca dentro do jogo, que vale na hora, ou o perfil do driver, que vale na
// reabertura), que e o que ela nao tem como saber. Ver o bloco "FPS DO JOGO" no main.cpp.
//
// JOGO_IGUAL saiu dos chips em 06/09/2026 e continua no enum so para ler ini antigo. Medido:
// igualar o jogo ao teto do filtro e o PIOR ajuste possivel - o filtro caiu de 25,0 para 19,9
// fps, porque ele nunca apresenta mais do que captura, e as duas taxas iguais batem cabeca.
// O valor certo fica ACIMA do que o filtro consome, nunca igual.
enum ModoJogo { JOGO_LIVRE = 0, JOGO_30 = 30, JOGO_45 = 45, JOGO_60 = 60, JOGO_120 = 120,
                JOGO_IGUAL = -1 };
static int  g_modo = JOGO_LIVRE;
static bool g_modoEscolhido = false;    // a pessoa clicou num chip alguma vez (senao vale o padrao por camadas)
static void ModoGravarIni() { IniSetD(L"modo", !g_modoEscolhido ? L"" : g_modo == JOGO_IGUAL ? L"igual" : g_modo == 60 ? L"60" : g_modo == 30 ? L"30" : L"livre"); }
static void ModoLerIni() {
    const std::wstring m = IniGetD(L"modo");
    g_modoEscolhido = !m.empty();
    g_modo = (m == L"igual") ? JOGO_IGUAL : (m == L"60") ? JOGO_60 : (m == L"30") ? JOGO_30 : JOGO_LIVRE;
}
// PADRAO = LIVRE SEMPRE (14/09/2026, decisao do usuario): o limite do driver so entra por escolha
// explicita no painel. Antes o 3x ligava JOGO_IGUAL sozinho e gravava no perfil do jogo sem
// ninguem pedir; um jogo aberto naquela janela ficava a 30 mesmo depois do app "soltar".
static int ModoPadrao(int) { return JOGO_LIVRE; }
// A BASE DA FLUIDEZ INTERPOLADA (X-FG-QUALIDADE, 19/09/2026; ESTUDO-LS §5.2). A regra numero um
// do Lossless Scaling e "limite o jogo a 1/2, 1/3 ou 1/4 do refresh": sem isso a captura do
// monitor chega em rajadas (COD do dono: 33 fps com p95 de 46 ms num painel de 240 Hz livre) e
// nenhum ritmo do apresentador conserta. Quando a Interpolada liga, o app pede uma base
// (refresh / divisor, o divisor comecando no multiplicador e subindo enquanto a chegada nao
// alcanca) POR ESTE CAMPO - e ela vale como um chip automatico: so enquanto a pessoa NAO
// escolheu chip nenhum (g_modoEscolhido), e so nesta sessao (nao vai ao ini). Passa pelos dois
// motores de sempre (a peca no jogo na hora; o perfil do driver na reabertura) e e desfeita
// pelos mesmos caminhos (Interpolada desligada -> 0 -> LimiteJogoRestaurar; Encerrar; guardiao).
// AVISO: isto e uma excecao a "PADRAO = LIVRE SEMPRE" (14/09) - o driver e gravado sem clique,
// so quando a Interpolada esta ligada; [fluidez] base_auto=0 no ini desliga (ver main.cpp).
static int  g_baseAuto = 0;
static void BaseAutomatica(int fps) {
    const int v = fps > 0 ? fps : 0;
    if (v == g_baseAuto) return;
    g_baseAuto = v;
    Logf("limite do jogo: base da fluidez interpolada = %d fps (%s)", v, v ? (g_modoEscolhido ? "a pessoa escolheu um chip: o chip vale, a base nao" : "vale como chip automatico enquanto nenhum chip for escolhido") : "desligada");
}
// O modo em vigor: o escolhido, ou a base da Interpolada, ou o padrao pelas camadas.
static int ModoAtual(int camadas) { return g_modoEscolhido ? g_modo : (g_baseAuto > 0 ? g_baseAuto : ModoPadrao(camadas)); }
// O limite que o modo pede para o jogo dado o teto da camada (0 = nenhum).
static int LimitePedido(int modo, int tetoCamada) { return modo == JOGO_IGUAL ? (tetoCamada > 0 ? tetoCamada : 0) : modo > 0 ? modo : 0; }

// ------------------------------------------------------------------ o que o painel mostra
struct Estado {
    int  jogoLimite = 0;         // 0 livre, 60, 30 (o que esta gravado no driver agora)
    bool prioAlta = false;       // alavanca 1 aplicada
    bool otimizando = false;     // a escada esta rodando
    std::wstring linha;          // uma linha de status para o painel (vazia = nada a dizer)
    bool linhaAviso = false;     // pintar a linha em ambar
};
static Estado g_est;

// Textos da interface (regra da casa: curto, sem jargao, so efeito garantido).
namespace Txt {
// DUAS TROCAS AQUI, as duas de 06/09/2026.
//
// 1. "pelicula" saiu, "filtro" entrou. A interface do app diz "filtro" 24 vezes e "pelicula"
//    ZERO. A equipe conversava em "pelicula" e a documentacao usa "pelicula", mas quem abre o
//    programa nunca ve essa palavra. Uma legenda que fala de "pelicula" nao ajuda ninguem a
//    entender que se trata da nossa imagem.
//
// 2. "60 ou 30" virou "60". O usuario, usando de verdade, achou que uma camada funciona bem
//    com o jogo travado em 60, nem mais nem menos. Oferecer 30 no mesmo folego dava a entender
//    que quanto menor melhor, e e o contrario: travar BAIXO DEMAIS estrangula a captura e o
//    filtro nunca apresenta mais do que captura.
//
// O TEXTO NAO PROMETE UM NUMERO, DE PROPOSITO, e isso e uma decisao e nao uma omissao.
//
// O QUE ESTA FIRME e pode ser dito: travar o jogo numa taxa FIXA deixa o filtro nitidamente
// mais regular do que deixar solto. Solto foi o pior de cinco configuracoes medidas, por
// margem larga. Isso vale como orientacao hoje, qualquer que seja o mecanismo.
//
// O QUE NAO ESTA FIRME e por isso nao entra: o numero 60. Quatro explicacoes eram candidatas e
// DUAS JA CAIRAM com o bloco do ultrawide (100 Hz):
//
//   (a) e o numero 60                                        -> ainda de pe
//   (b) e a taxa da tela                                     -> CAIU
//   (c) e a razao inteira entre a taxa do jogo e a da tela    -> CAIU
//   (d) e CAPACIDADE: nao e o filtro que satura, e o jogo que deixa de segurar a propria trava
//       quando divide a placa - e ai o valor certo e o maior que a maquina sustenta, e muda por
//       maquina e por cena                                    -> ainda de pe, e a frente
//
// O QUE DERRUBOU (b) E (c): travar em 60 foi o ponto mais regular NOS DOIS monitores, o de 60 Hz
// e o de 100 Hz. No ultrawide, as celulas sincronizadas com a tela ficaram PIOR que a de 60, que
// ali esta deliberadamente fora de sincronia. Os dois criterios de leitura deram a mesma ordem,
// o que nao acontecia no primario. Entao o valor recomendado NAO vira conta por monitor.
//
// Escrever "trave em 60" continua sendo criar um teto onde talvez nao haja: se o mecanismo for
// (d), numa maquina mais forte o ideal e mais alto. Por isso a frase diz que travar ajuda, que
// 60 e um bom ponto de PARTIDA, e que o melhor valor e o maior que a maquina segura. Esse texto
// ja e o texto certo se (d) ganhar, e continua verdadeiro se (a) ganhar.
// Explicacao(), PontoDePartida(), NaoMexeNoFiltro(), RotuloSecao(), ChipLivre(),
// SemLimiteNavegador(), Botao(), BotaoSub(), BotaoParar(), SemNvidia() e BotaoSubPendente()
// (mais abaixo) SAIRAM em 24/09/2026 (item 7, Rodada 21): eram texto so da secao "FPS DO
// JOGO" removida do menu End. ChipIgual() ja estava sem uso desde antes (chip que saiu dos
// controles em 06/09/2026) e continua so por historico/migracao de ini antigo.
static const wchar_t* ChipIgual(bool en)   { return en ? L"Same as layer" : L"Igual à película"; }
static const wchar_t* ProximaAbertura(bool en) { return en ? L"takes effect when the game is reopened" : L"vale quando o jogo for reaberto"; }
// O OPOSTO DELA, para quando a peca esta no jogo e respondendo. Existe porque o contrario -
// nao dizer nada - deixa a pessoa sem saber se precisa reabrir: ela acabou de ler essa
// exigencia em outras telas do programa, e silencio aqui ela le como "entao tambem preciso".
static const wchar_t* JaValendo(bool en) { return en ? L"already in effect — no need to reopen the game" : L"já está valendo — não precisa reabrir o jogo"; }
// TarjaPendente(), TarjaHerdado() e BotaoSubPendente() SAIRAM em 24/09/2026 junto com a secao
// "FPS DO JOGO" (item 7, Rodada 21) - eram as tarjas que aquele painel mostrava.
static std::wstring AgoraValendo(bool en, int fps) {
    return (en ? L"The game cap is now in effect: " : L"O limite do jogo está valendo agora: ") +
           std::to_wstring(fps) + L" fps";
}
}

// ------------------------------------------------------------------ 3+4. a escada
// O host entrega o que a escada precisa. Tudo opcional menos fpsCamada.
struct Contexto {
    std::wstring exeAlvo;                                   // exe do jogo (ExeDaJanela(scap.targetWin))
    bool nvidia = true;                                     // ha NvAPI (LimiteJogoDisponivel())
    int  alvoFps = 30;                                      // 60 com uma camada so, 30 com 2x/3x
    bool en = false;                                        // idioma
    bool limiteAoVivo = true;                               // medido na parte A: o FRL do driver pega no jogo aberto?
    bool workResAoVivo = false;                             // o Feeder aceita mudar work_resolution sem reabrir?
    int  workResAtual = 100;
    std::function<void(const std::wstring&, UINT)> toast;   // cartaz
    std::function<bool(int)> aplicarWorkRes;                // grava work_resolution no cfg; true se gravou
    std::function<void()> repintar;                         // Hud::Refresh
};

enum Degrau { D_NADA = 0, D_BASE, D_PRIO, D_WORK80, D_WORK67, D_JOGO60, D_JOGO30, D_FIM };
static const int kAmostrasPorDegrau = 5;   // 5 s de medicao por degrau
static const int kDescartar = 1;           // o 1o segundo depois de mexer e descartado

// B6 (16/09/2026): os degraus do limite do jogo gravam no driver (~100 ms) pela thread da conferencia,
// como o chip (B5). Pedir devolve o numero do pedido; 0 = sem thread, rodou aqui e *ok/*erro ja valem.
// fps 0 = restaurar. Definidas mais abaixo, junto da thread.
static unsigned PedirLimiteOtimizador(const std::wstring& exe, int fps, bool* ok, std::wstring* erro);
static bool RespostaLimiteOtimizador(unsigned seq, bool* ok, std::wstring* erro);

struct Otimizador {
    Contexto ctx;
    int    degrau = D_NADA;
    int    idx = -1;              // posicao na ordem de degraus
    std::vector<int> ordem;       // os degraus desta rodada, na ordem
    int    amostras = 0;          // amostras aceitas neste degrau
    int    descartadas = 0;
    double soma = 0.0;
    double mediaAnterior = 0.0;   // media do degrau anterior (para detectar piora)
    double mediaBase = 0.0;
    int    workResAntes = 100;    // como estava quando a escada comecou (para um cancelamento geral)
    int    workResPrevio = 100;   // como estava antes DESTE degrau (e o que "desfazer" devolve)
    bool   prioAntesOk = false; int prioAntes = -1;
    std::wstring feito;           // resumo do que ficou aplicado (separado por virgula)
    std::wstring feitoAntes;      // o resumo como estava antes DESTE degrau (desfazer volta a ele)
    unsigned esperaSeq = 0;       // B6: pedido ao driver cuja resposta este degrau espera (0 = nenhum)
    int    esperaFps = 0;
    double esperaMedia = 0.0;     // a media com que Proximo foi chamado, para seguir depois da resposta

    bool Ativo() const { return degrau != D_NADA && degrau != D_FIM; }
    void Toast(const std::wstring& s, UINT ms = 2500) { if (ctx.toast) ctx.toast(s, ms); }
    std::wstring Fps(double v) { wchar_t b[32]; _snwprintf(b, 32, L"%.0f", v); return b; }
    void Feito(const std::wstring& s) { if (!feito.empty()) feito += L", "; feito += s; }
    // Degraus da mesma familia (DLSS 80 depois 67) contam como UM item no resumo: quem le
    // quer saber onde parou, nao o caminho. Troca o item que comeca com `prefixo`.
    void FeitoTrocar(const std::wstring& prefixo, const std::wstring& s) {
        size_t p = feito.find(prefixo);
        if (p == std::wstring::npos) { Feito(s); return; }
        size_t fim = feito.find(L", ", p);
        feito = feito.substr(0, p) + s + (fim == std::wstring::npos ? L"" : feito.substr(fim));
    }

    void Iniciar(const Contexto& c) {
        ctx = c;
        degrau = D_BASE; idx = -1; amostras = 0; descartadas = 0; soma = 0.0; mediaAnterior = 0.0;
        feito.clear(); feitoAntes.clear(); esperaSeq = 0;
        workResAntes = workResPrevio = ctx.workResAtual;
        // A ORDEM DEPENDE DO QUE PEGA AO VIVO. Medir so faz sentido em degrau que muda algo
        // agora; o limite do jogo, quando so vale na reabertura, fica por ultimo e encerra
        // a escada como "aplicado, reabra o jogo".
        ordem.clear();
        ordem.push_back(D_PRIO);
        if (ctx.limiteAoVivo) { ordem.push_back(D_JOGO60); ordem.push_back(D_JOGO30); }
        ordem.push_back(D_WORK80); ordem.push_back(D_WORK67);
        // SEM opt-in a escada NAO grava no perfil do driver (14/09/2026): quando o limite so vale
        // na reabertura, o degrau D_JOGO* deixava uma chave no perfil do jogo que a pessoa nao
        // pediu e que segurava o jogo depois do app "soltar". So entra por escolha no chip.
        g_est.otimizando = true; g_est.linha = ctx.en ? L"measuring…" : L"medindo…"; g_est.linhaAviso = false;
        Logf("otimizar: inicio (alvo %d fps, exe %s, nvidia=%d, limite ao vivo=%d, work_res ao vivo=%d, work_res atual=%d)",
             ctx.alvoFps, U8(ctx.exeAlvo).c_str(), ctx.nvidia ? 1 : 0, ctx.limiteAoVivo ? 1 : 0, ctx.workResAoVivo ? 1 : 0, ctx.workResAtual);
        Toast(ctx.en ? L"Optimizing… measuring the layer" : L"Otimizando… medindo a película", 2500);
        if (ctx.repintar) ctx.repintar();
    }
    void Cancelar() {
        if (!Ativo()) return;
        Logf("otimizar: cancelado pela pessoa no degrau %d", degrau);
        degrau = D_FIM; g_est.otimizando = false;
        g_est.linha = feito.empty() ? (ctx.en ? L"stopped" : L"parado") : (ctx.en ? L"stopped; kept: " : L"parado; ficou: ") + feito;
        g_est.linhaAviso = false;
        if (ctx.repintar) ctx.repintar();
    }

    // Chamar UMA VEZ POR SEGUNDO com o fps apresentado pela camada (o pfps do laco).
    void Amostra1s(double fpsCamada) {
        if (!Ativo()) return;
        if (esperaSeq) return;                     // B6: o driver ainda nao respondeu o degrau: nada a medir
        if (descartadas < kDescartar) { descartadas++; return; }
        soma += fpsCamada; amostras++;
        if (amostras < kAmostrasPorDegrau) return;
        const double media = soma / amostras;
        Logf("otimizar: degrau %d -> pelicula %.1f fps (alvo %d)", degrau, media, ctx.alvoFps);
        if (degrau == D_BASE) mediaBase = media;
        // Piorou de verdade em relacao ao degrau anterior? Desfaz este e segue.
        // `valida` e o fps que a pessoa VAI VER daqui em diante: depois de desfazer, o do
        // degrau anterior (o que continua de pe), nao o do degrau ruim.
        double valida = media;
        if (degrau != D_BASE && mediaAnterior > 0 && media < mediaAnterior * 0.9) {
            Logf("otimizar: degrau %d piorou (%.1f < %.1f) - desfeito", degrau, media, mediaAnterior);
            valida = mediaAnterior;
            Desfazer(degrau);
        } else {
            mediaAnterior = media;
        }
        if (valida >= ctx.alvoFps) { Terminar(true, valida); return; }
        Proximo(valida);
    }

    void Desfazer(int d) {
        switch (d) {
        case D_PRIO:   if (prioAntesOk) PrioridadeGpu(prioAntes < 0 ? PRIO_GPU_NORMAL : prioAntes); g_est.prioAlta = false; break;
        // B6: a restauracao vai para a thread e ninguem espera (o retorno ja era ignorado). A fila
        // garante que ela roda antes do degrau seguinte, se ele tambem gravar.
        case D_JOGO60: case D_JOGO30: { bool ok = false; std::wstring e; PedirLimiteOtimizador(ctx.exeAlvo, 0, &ok, &e); g_est.jogoLimite = 0; break; }
        // volta ao valor do DEGRAU ANTERIOR, nao ao do inicio da escada: se 80 ajudou e 67
        // piorou, quem fica e o 80.
        case D_WORK80: case D_WORK67: if (ctx.aplicarWorkRes) { ctx.aplicarWorkRes(workResPrevio); ctx.workResAtual = workResPrevio; } break;
        }
        feito = feitoAntes;       // o resumo volta a ser o do degrau anterior
        mediaAnterior = 0.0;      // e o degrau desfeito nao serve de referencia para o proximo
    }

    void Proximo(double mediaAtual) {
        for (;;) {
            if (++idx >= (int)ordem.size()) { Terminar(false, mediaAtual); return; }
            degrau = ordem[idx];
            amostras = 0; descartadas = 0; soma = 0.0;
            // fotografia do estado ANTES deste degrau, para o caso de ele piorar
            feitoAntes = feito; workResPrevio = ctx.workResAtual;
            switch (degrau) {
            case D_PRIO: {
                if (g_est.prioAlta) continue;                       // ja esta (a pessoa clicou duas vezes)
                prioAntesOk = PrioridadeGpuMaxima(&prioAntes);
                if (!prioAntesOk) continue;
                g_est.prioAlta = true;
                Feito(ctx.en ? L"layer first in line" : L"película com prioridade");
                Toast(ctx.en ? L"Optimizing… layer gets GPU priority" : L"Otimizando… película com prioridade na placa");
                return;
            }
            case D_WORK80: case D_WORK67: {
                const int wr = (degrau == D_WORK80) ? 80 : 67;
                if (!ctx.aplicarWorkRes || ctx.workResAtual <= wr) continue;
                if (!ctx.aplicarWorkRes(wr)) continue;
                ctx.workResAtual = wr;
                FeitoTrocar(ctx.en ? L"DLSS at " : L"DLSS a ", (ctx.en ? L"DLSS at " : L"DLSS a ") + std::to_wstring(wr) + L"%");
                if (!ctx.workResAoVivo) {
                    Toast((ctx.en ? L"DLSS resolution " : L"Resolução do DLSS ") + std::to_wstring(wr) + L"%\n" +
                          (ctx.en ? L"takes effect the next time the app opens" : L"vale na próxima abertura do app"), 5000);
                    Terminar(false, mediaAtual, true);
                    return;
                }
                Toast((ctx.en ? L"Optimizing… DLSS resolution " : L"Otimizando… resolução do DLSS ") + std::to_wstring(wr) + L"%");
                // a reconstrucao da feature leva alguns quadros: um segundo a mais de descarte
                descartadas = -1;
                return;
            }
            case D_JOGO60: case D_JOGO30: {
                const int fps = (degrau == D_JOGO60) ? 60 : 30;
                if (!ctx.nvidia || ctx.exeAlvo.empty() || EhNavegador(ctx.exeAlvo)) continue;
                if (fps < ctx.alvoFps) continue;                    // jogo a 30 nunca leva a pelicula a 60 (ela apresenta no maximo o que captura)
                if (!ctx.limiteAoVivo && degrau == D_JOGO60 && ctx.alvoFps <= 30) continue;   // sem medir, vai direto ao que a pelicula precisa
                std::wstring erro; bool ok = false;
                const unsigned seq = PedirLimiteOtimizador(ctx.exeAlvo, fps, &ok, &erro);
                // B6: com a thread, a escada fica parada neste degrau ate o driver responder (Pulso)
                if (seq) { esperaSeq = seq; esperaFps = fps; esperaMedia = mediaAtual; return; }
                if (!DepoisDoLimite(fps, ok, erro, mediaAtual)) continue;
                return;
            }
            default:
                continue;
            }
        }
    }

    // O resto do degrau D_JOGO* depois que o driver respondeu. false = nao aplicou (o degrau e pulado).
    bool DepoisDoLimite(int fps, bool ok, const std::wstring& erro, double mediaAtual) {
        if (!ok) { Logf("otimizar: limite %d nao aplicado: %s", fps, U8(erro).c_str()); return false; }
        g_est.jogoLimite = fps;
        Feito((ctx.en ? L"game at " : L"jogo a ") + std::to_wstring(fps) + L" fps");
        if (!ctx.limiteAoVivo) {
            // O driver so aplica na proxima abertura do jogo: nao adianta medir.
            Terminar(false, mediaAtual, true);
            return true;
        }
        Toast((ctx.en ? L"Optimizing… game capped at " : L"Otimizando… jogo limitado a ") + std::to_wstring(fps) + L" fps");
        return true;
    }

    // B6: chamar a cada volta do laco (a thread o acorda quando o driver responde).
    void Pulso() {
        if (!esperaSeq) return;
        bool ok = false; std::wstring erro;
        if (!RespostaLimiteOtimizador(esperaSeq, &ok, &erro)) return;
        const int fps = esperaFps; esperaSeq = 0;
        if (!Ativo()) {
            // parada enquanto o driver gravava: o limite ficou, o painel mostra
            Logf("otimizar: limite %d %s depois da parada", fps, ok ? "gravado" : "nao aplicado");
            if (ok) g_est.jogoLimite = fps;
        } else if (!DepoisDoLimite(fps, ok, erro, esperaMedia)) {
            Proximo(esperaMedia);
        }
        if (ctx.repintar) ctx.repintar();
    }

    void Terminar(bool chegou, double media, bool pendente = false) {
        degrau = D_FIM; g_est.otimizando = false;
        std::wstring r;
        if (chegou) {
            r = (ctx.en ? L"Optimized: " : L"Otimizado: ") + (feito.empty() ? (ctx.en ? L"nothing needed" : L"nada foi preciso") : feito) +
                (ctx.en ? L", layer at " : L", película a ") + Fps(media) + L" fps";
            g_est.linhaAviso = false;
        } else if (pendente) {
            r = (ctx.en ? L"Applied: " : L"Aplicado: ") + feito + L" — " +
                (ctx.en ? L"reopen the game to see the effect" : L"reabra o jogo para ver o efeito");
            g_est.linhaAviso = true;
        } else {
            r = (ctx.en ? L"Could not improve: layer at " : L"Não deu para melhorar: película a ") + Fps(media) + L" fps" +
                (feito.empty() ? L"" : L" (" + feito + L")") +
                (ctx.en ? L". Try fewer layers." : L". Tente menos camadas.");
            g_est.linhaAviso = true;
        }
        g_est.linha = r;
        Logf("otimizar: fim - %s (base %.1f fps)", U8(r).c_str(), mediaBase);
        Toast(r, 5000);
        if (ctx.repintar) ctx.repintar();
    }
};
static Otimizador g_otim;

// ------------------------------------------------------------------ o modo e o teto da camada
// Poe o driver de acordo com o modo em vigor. Chamar: na abertura (depois que o alvo e
// conhecido), quando o teto da camada muda (CMD_FPS_*, Teto3x), quando as camadas mudam
// (1x/2x/3x), quando a pessoa clica num chip "Jogo:" e quando o alvo muda. Idempotente:
// so toca no driver se o limite pedido for diferente do que esta gravado. Devolve o texto
// do cartaz quando mudou algo (vazio = nada a dizer).
//
// aoVivo: A PECA ESTA NO JOGO E RESPONDENDO (MOTOR_AO_VIVO no host). Quando esta true, o
// cartaz NAO pode dizer "vale quando o jogo for reaberto": a peca ja aplicou na hora, e a
// frase mandaria a pessoa reabrir o jogo a toa. O driver continua sendo gravado do mesmo
// jeito - ele e a rede para quando a peca sair -, so a frase muda. O host sabe disso e este
// arquivo nao, por isso vem por parametro. Ver MotorAtual() no main.cpp.
//
// B5 (16/09/2026): a gravacao no driver (~100 ms) saiu do laco. Sincronizar() e Manual() so postam
// o pedido para a thread da conferencia (g_conf); ela roda SincronizarAgora() sob a trava DRS e
// devolve o cartaz, que o laco pega em RespostaSincronizar() e mostra. Pedidos seguidos antes de a
// thread acordar viram um so (vale o ultimo; o cartaz de "nada mudou" do chip nao se perde).
static volatile LONG g_reconferir = 0;   // B2: o limite mudou - a proxima Situacao confere o perfil de novo
struct RespostaSinc { std::wstring msg, erro; int jogoLimite = 0; bool falhou = false; };
static RespostaSinc SincronizarAgora(const std::wstring& exe, int modo, int tetoCamada, bool en,
                                     unsigned long pidAlvo, bool aoVivo, bool manual) {
    RespostaSinc r;
    int pedido = LimitePedido(modo, tetoCamada);
    if (exe.empty() || EhNavegador(exe) || !LimiteJogoDisponivel()) pedido = 0;
    const int atual = (!g_lim.exe.empty() && Minusculas(g_lim.exe) == Minusculas(exe)) ? g_lim.aplicado : 0;
    if (pedido == atual) {
        r.jogoLimite = atual;
        // Chip manual "Jogo: Livre / 60 / 30 / Igual a pelicula" sem mudanca: diz por que.
        if (manual) {
            if (EhNavegador(exe)) r.msg = en ? L"Video in the browser: no cap available" : L"Vídeo no navegador: sem limite disponível";
            else if (modo == JOGO_IGUAL && tetoCamada == 0) r.msg = en ? L"Game follows the layer: no cap while the layer is free" : L"Jogo igual à película: sem limite enquanto a película estiver livre";
            else r.msg = en ? L"Game: no change" : L"Jogo: nada mudou";
        }
        return r;
    }
    InterlockedExchange(&g_reconferir, 1);
    std::wstring erro;
    if (pedido == 0) {
        LimiteJogoRestaurar(); r.jogoLimite = 0;
        if (aoVivo) r.msg = en ? L"Game: free again" : L"Jogo: livre de novo";
        else r.msg = en ? L"Game: free again\nit stays capped until it is reopened" : L"Jogo: livre de novo\ncontinua limitado até ser reaberto";
        return r;
    }
    if (!LimiteJogoAplicar(exe, pedido, &erro, pidAlvo)) {
        r.falhou = true;     // o painel continua mostrando o limite que ja estava
        r.erro = erro;
        r.msg = (en ? L"Could not cap the game: " : L"Não deu para limitar o jogo: ") + erro;
        return r;
    }
    r.jogoLimite = pedido;
    r.msg = (en ? L"Game capped at " : L"Jogo limitado a ") + std::to_wstring(pedido) + L" fps";
    if (modo == JOGO_IGUAL) r.msg += en ? L" (same as the layer)" : L" (igual à película)";
    if (!kLimiteAoVivo && !aoVivo) r.msg += std::wstring(L"\n") + Txt::ProximaAbertura(en);
    if (aoVivo)         r.msg += std::wstring(L"\n") + Txt::JaValendo(en);
    return r;
}

// ------------------------------------------------------------------ ciclo de vida
// Na abertura: liga o ini, le o modo e desfaz qualquer limite que tenha ficado de uma queda.
// RESTAURAR EM TODO CAMINHO DE SAIDA (14/09/2026). Fatal() -> ExitProcess, crash (0xC0000005),
// WM_ENDSESSION e o kill pelo Gerenciador de Tarefas nao passam pelo Encerrar() do fim do
// wWinMain. Este e o caminho de emergencia: idempotente, sem toast, sem repintura; so devolve
// o perfil se houver chave nossa. Chamado por Fatal(), pelo filtro de excecao e por WM_ENDSESSION.
static void EmergenciaRestaurar(const char* de) {
    static volatile LONG dentro = 0;
    if (InterlockedCompareExchange(&dentro, 1, 0) != 0) return;   // reentrada (crash dentro do proprio restaurar)
    {
        nv::Segura trava(nv::g_travaDrs.cs);   // B5: a thread da conferencia pode estar gravando o limite agora
        if (!g_lim.exe.empty()) {
            Logf("limite do jogo: saida de emergencia (%s) com %s a %d fps no driver - restaurando", de, U8(g_lim.exe).c_str(), g_lim.aplicado);
            LimiteJogoRestaurar();
        }
    }
    Max::Restaurar();   // Parte B: um crash com "Desempenho maximo" ligado nao pode deixar PREFERRED_PSTATE gravado
    InterlockedExchange(&dentro, 0);
}
static LPTOP_LEVEL_EXCEPTION_FILTER g_filtroAnterior = nullptr;
static LONG WINAPI FiltroCrash(EXCEPTION_POINTERS* ep) {
    EmergenciaRestaurar("crash");
    return g_filtroAnterior ? g_filtroAnterior(ep) : EXCEPTION_CONTINUE_SEARCH;
}

// VARREDURA DO PERFIL (14/09/2026): le o FRL que o driver TEM HOJE para o exe e decide. Uma vez
// por exe por sessao (Situacao chama). Chave nossa aplicada agora: nada a fazer. Perfil com o
// nosso nome ("DLSS5ForAll - exe") ou valor igual ao ultimo que gravamos sem registro de
// restauracao: orfa -> apaga e loga. Qualquer outro valor: NAO mexe (pode ser do painel da
// NVIDIA) - loga e avisa no painel.
// B2: roda na thread da conferencia (ver g_conf); o aviso do painel volta pelo laco.
struct Conferencia {
    CRITICAL_SECTION cs;              // protege o pedido e o aviso
    HANDLE ev = nullptr, th = nullptr;
    volatile LONG parar = 0;
    std::wstring exe, aviso;
    unsigned long pid = 0; int est = 0, hz = 0;
    bool varrer = false, pedido = false;
    // B5: o pedido de gravacao do limite (ver Sincronizar) e a resposta para o laco
    struct Sinc { bool tem = false, manual = false, en = false, aoVivo = false; std::wstring exe; int modo = 0, teto = 0; unsigned long pid = 0; DWORD tidLaco = 0; };
    Sinc sinc;
    RespostaSinc resposta; bool temResposta = false;
    // B6: os pedidos do botao "Otimizar", em FILA (restaurar e aplicar em seguida nao podem virar um so),
    // e a resposta do ultimo que rodou, com o numero dele
    struct PedidoOtim { unsigned seq = 0; std::wstring exe; int fps = 0; DWORD tidLaco = 0; };
    std::vector<PedidoOtim> otim;
    unsigned otimSeq = 0, otimRespSeq = 0; bool otimRespOk = false; std::wstring otimRespErro;
    Conferencia() { InitializeCriticalSection(&cs); }
};
static Conferencia g_conf;
static void VarrerPerfil(const std::wstring& exe) {
    if (exe.empty() || EhNavegador(exe) || !LimiteJogoDisponivel()) return;
    nv::Sessao_ s; if (!s.ok) return;
    std::wstring nomePerfil;
    nv::Perfil p = s.PerfilDoExe(exe, false, nullptr, &nomePerfil);
    const int frl = p ? s.LerFrl(p) : -1;
    if (frl < 0) { Logf("varredura: perfil de %s sem FRL - nenhuma chave orfa", U8(exe).c_str()); return; }
    if (!g_lim.exe.empty() && Minusculas(g_lim.exe) == Minusculas(exe)) { Logf("varredura: perfil de %s com FRL=%d (nosso, aplicado nesta sessao)", U8(exe).c_str(), frl); return; }
    const bool perfilNosso = nomePerfil.rfind(L"DLSS5ForAll - ", 0) == 0;
    const bool valorNosso  = !g_jan.exe.empty() && Minusculas(g_jan.exe) == Minusculas(exe) && g_jan.fps == frl && g_jan.ate == 0;
    if (perfilNosso || valorNosso) {
        bool ok = perfilNosso ? (nv::g_api.DeleteProfile(s.s, p) == nv::OK) : s.ApagarFrl(p);
        if (ok) ok = s.Salvar();
        Logf("limite do jogo: chave orfa apagada - perfil '%s' de %s tinha FRL=%d sem registro de restauracao (%s)%s",
             U8(nomePerfil).c_str(), U8(exe).c_str(), frl, perfilNosso ? "perfil criado por nos" : "valor igual ao ultimo que gravamos", ok ? "" : " - FALHOU");
        if (ok && g_jan.ate == 0 && !g_jan.exe.empty()) { { nv::Segura dados(g_travaDados.cs); g_jan.ate = AgoraFt(); } JanelaGravarIni(); }
        return;
    }
    Logf("varredura: o perfil '%s' de %s ja tem FRL=%d SEM registro nosso - nao mexi (painel da NVIDIA?)", U8(nomePerfil).c_str(), U8(exe).c_str(), frl);
    EnterCriticalSection(&g_conf.cs);
    g_conf.aviso = L"O driver já limita este jogo a " + std::to_wstring(frl) + L" fps (perfil NVIDIA, não é nosso)";
    LeaveCriticalSection(&g_conf.cs);
}

// A LINHA DE PROVA (14/09/2026): o que o driver tem no perfil AGORA, quando o pid nasceu, em que
// pe o limite esta e o refresh medido. E o que separa "o jogo esta a 30 pelo driver" de "a captura
// esta a 30".
// B2 (16/09/2026): era a cada 10 s NO LACO DE RENDER, e cada Sessao_ (CreateSession + LoadSettings
// da base DRS inteira) levava ~100 ms: 6 quadros da ponte perdidos e um STALL de 120 ms no feed a
// cada 10 s, nos dois modos. Agora a thread abaixo faz a varredura e a leitura, e so quando o pid
// do jogo muda ou o limite e mexido (g_reconferir). O laco so posta o pedido e le o aviso.
// B5: roda um pedido de Sincronizar e guarda o cartaz. A checagem de `parar` fica DENTRO da trava
// DRS: se o Encerrar ja pediu a parada, a restauracao dele vem depois e nada novo e gravado.
static void SincronizarNaThread(const Conferencia::Sinc& p) {
    RespostaSinc r; bool rodou = false;
    {
        nv::Segura trava(nv::g_travaDrs.cs);
        if (!g_conf.parar) { r = SincronizarAgora(p.exe, p.modo, p.teto, p.en, p.pid, p.aoVivo, p.manual); rodou = true; }
    }
    if (!rodou) return;
    EnterCriticalSection(&g_conf.cs);
    // Resposta ainda nao lida pelo laco: junta. Cartaz vazio nao apaga o anterior; o erro fica ate
    // ser lido; o limite mostrado e o da ultima gravacao que deu certo.
    RespostaSinc& a = g_conf.resposta;
    if (!g_conf.temResposta) a = RespostaSinc{};
    if (!r.msg.empty()) a.msg = r.msg;
    if (r.falhou) a.erro = r.erro;
    else { a.jogoLimite = r.jogoLimite; a.falhou = false; }
    if (!g_conf.temResposta && r.falhou) a.falhou = true;   // so falha: o painel nao mexe no limite
    g_conf.temResposta = true;
    LeaveCriticalSection(&g_conf.cs);
    if (p.tidLaco) PostThreadMessageW(p.tidLaco, WM_NULL, 0, 0);   // acorda o laco (MsgWaitForMultipleObjects)
}
// B6: um degrau do "Otimizar" (fps 0 = restaurar), como o Otimizador fazia no laco antes.
static bool LimiteOtimizadorAgora(const std::wstring& exe, int fps, std::wstring* erro) {
    if (fps <= 0) return LimiteJogoRestaurar();
    return LimiteJogoAplicar(exe, fps, erro);
}
// B6: roda a fila do "Otimizar" em ordem. Mesma regra do SincronizarNaThread: `parar` e conferido
// dentro da trava DRS, entao depois do Encerrar nada novo e gravado.
static void OtimizadorNaThread(const std::vector<Conferencia::PedidoOtim>& fila) {
    for (const auto& p : fila) {
        bool ok = false, rodou = false; std::wstring erro;
        {
            nv::Segura trava(nv::g_travaDrs.cs);
            if (!g_conf.parar) { ok = LimiteOtimizadorAgora(p.exe, p.fps, &erro); rodou = true; }
        }
        if (!rodou) return;
        EnterCriticalSection(&g_conf.cs);
        g_conf.otimRespSeq = p.seq; g_conf.otimRespOk = ok; g_conf.otimRespErro = erro;
        LeaveCriticalSection(&g_conf.cs);
        if (p.tidLaco) PostThreadMessageW(p.tidLaco, WM_NULL, 0, 0);
    }
}
static DWORD WINAPI ConferenciaThread(LPVOID) {
    LimiteJogoDisponivel();   // B8: a primeira carga da nvapi sai aqui, nunca no laco (com o Iniciar ja feita, nao custa nada)
    for (;;) {
        WaitForSingleObject(g_conf.ev, INFINITE);
        if (g_conf.parar) return 0;
        EnterCriticalSection(&g_conf.cs);
        const bool tem = g_conf.pedido;
        const std::wstring exe = g_conf.exe;
        const unsigned long pid = g_conf.pid; const int est = g_conf.est, hz = g_conf.hz; const bool varrer = g_conf.varrer;
        g_conf.pedido = g_conf.varrer = false;
        const Conferencia::Sinc sinc = g_conf.sinc;
        g_conf.sinc = Conferencia::Sinc{};
        std::vector<Conferencia::PedidoOtim> otim; otim.swap(g_conf.otim);
        LeaveCriticalSection(&g_conf.cs);
        if (sinc.tem) SincronizarNaThread(sinc);
        if (!otim.empty()) OtimizadorNaThread(otim);
        if (g_conf.parar) return 0;
        if (!tem) continue;
        if (varrer) VarrerPerfil(exe);
        if (g_conf.parar) return 0;
        const int frl = LimiteJogoAtual(exe);
        const char* nome = est == LIM_PENDENTE ? "pendente" : est == LIM_VALENDO ? "valendo" : est == LIM_HERDADO ? "herdado" : est == LIM_INDEFINIDO ? "indefinido" : "livre";
        char frlTxt[16]; if (frl < 0) snprintf(frlTxt, sizeof frlTxt, "ausente"); else snprintf(frlTxt, sizeof frlTxt, "%d", frl);
        Logf("jogo: %s pid %lu nasceu %s | FRL no perfil agora=%s | estado=%s herdado=%s | refresh %d Hz",
             U8(exe).c_str(), pid, HoraLocal(HoraCriacao(pid)).c_str(), frlTxt, nome, est == LIM_HERDADO ? "sim" : "nao", hz);
    }
}
static bool GarantirThreadConferencia() {
    if (!g_conf.th) {
        if (!g_conf.ev) g_conf.ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_conf.th = g_conf.ev ? CreateThread(nullptr, 0, ConferenciaThread, nullptr, 0, nullptr) : nullptr;
    }
    return g_conf.th != nullptr;
}
// B8: onde o Iniciar nao roda (camada de cima, host da ponte do add-on), a primeira carga da nvapi
// saia no laco, no primeiro desenho do painel. Chamar no arranque: a thread da conferencia nasce
// e carrega. Sem thread, fica como antes (carga na primeira chamada).
static void CarregarApiForaDoLaco() { GarantirThreadConferencia(); }
// Chamar a cada 1 s do laco principal com o exe/pid do alvo. Nao bloqueia: sem mudanca, so compara.
static void Situacao(const std::wstring& exe, unsigned long pid, int (*refreshHz)()) {
    static std::wstring exeVarrido; static unsigned long pidVisto = 0;
    if (exe.empty()) return;
    if (TryEnterCriticalSection(&g_conf.cs)) {
        if (!g_conf.aviso.empty()) { g_est.linha = g_conf.aviso; g_est.linhaAviso = true; g_conf.aviso.clear(); }
        LeaveCriticalSection(&g_conf.cs);
    }
    const bool novoExe = Minusculas(exe) != exeVarrido;
    if (!novoExe && pid == pidVisto && !g_reconferir) return;
    if (!GarantirThreadConferencia()) return;   // sem thread nao confere: nunca no laco
    exeVarrido = Minusculas(exe); pidVisto = pid; InterlockedExchange(&g_reconferir, 0);
    const int est = EstadoDoLimite(pid);
    const int hz = refreshHz ? refreshHz() : 0;
    EnterCriticalSection(&g_conf.cs);
    g_conf.exe = exe; g_conf.pid = pid; g_conf.est = est; g_conf.hz = hz;
    g_conf.varrer = g_conf.varrer || novoExe; g_conf.pedido = true;
    LeaveCriticalSection(&g_conf.cs);
    SetEvent(g_conf.ev);
}

// B5: poe o driver de acordo com o modo em vigor (ver o comentario de SincronizarAgora). Nao
// bloqueia: o cartaz chega depois por RespostaSincronizar. Sem thread, roda aqui como antes.
static void Sincronizar(const std::wstring& exe, int camadas, int tetoCamada, bool en,
                        unsigned long pidAlvo = 0, bool aoVivo = false, bool manual = false) {
    Conferencia::Sinc p;
    p.tem = true; p.manual = manual; p.en = en; p.aoVivo = aoVivo;
    p.exe = exe; p.modo = ModoAtual(camadas); p.teto = tetoCamada; p.pid = pidAlvo; p.tidLaco = GetCurrentThreadId();
    if (!GarantirThreadConferencia()) { p.tidLaco = 0; SincronizarNaThread(p); return; }
    EnterCriticalSection(&g_conf.cs);
    p.manual = p.manual || (g_conf.sinc.tem && g_conf.sinc.manual);   // o chip clicado ainda merece o cartaz dele
    g_conf.sinc = p;
    LeaveCriticalSection(&g_conf.cs);
    SetEvent(g_conf.ev);
}

// B6: ver a declaracao, antes do Otimizador.
static unsigned PedirLimiteOtimizador(const std::wstring& exe, int fps, bool* ok, std::wstring* erro) {
    if (!GarantirThreadConferencia()) { *ok = LimiteOtimizadorAgora(exe, fps, erro); return 0; }
    Conferencia::PedidoOtim p;
    p.exe = exe; p.fps = fps; p.tidLaco = GetCurrentThreadId();
    EnterCriticalSection(&g_conf.cs);
    if (++g_conf.otimSeq == 0) ++g_conf.otimSeq;   // 0 quer dizer "rodou aqui"
    p.seq = g_conf.otimSeq;
    g_conf.otim.push_back(p);
    LeaveCriticalSection(&g_conf.cs);
    SetEvent(g_conf.ev);
    return p.seq;
}
// B6: true = o pedido `seq` ja rodou (*ok/*erro dele). Nao bloqueia.
static bool RespostaLimiteOtimizador(unsigned seq, bool* ok, std::wstring* erro) {
    if (!TryEnterCriticalSection(&g_conf.cs)) return false;
    const bool tem = g_conf.otimRespSeq == seq;
    if (tem) { *ok = g_conf.otimRespOk; *erro = g_conf.otimRespErro; }
    LeaveCriticalSection(&g_conf.cs);
    return tem;
}

// Chip manual "Jogo: Livre / 60 / 30 / Igual a pelicula". Grava a escolha e sincroniza.
static void Manual(const std::wstring& exe, int modo, int camadas, int tetoCamada, bool en,
                   unsigned long pidAlvo = 0, bool aoVivo = false) {
    g_modo = modo; g_modoEscolhido = true; ModoGravarIni();
    Logf("limite do jogo: modo %s escolhido no painel", modo == JOGO_IGUAL ? "igual a pelicula" : modo == 60 ? "60" : modo == 30 ? "30" : "livre");
    Sincronizar(exe, camadas, tetoCamada, en, pidAlvo, aoVivo, true);
}

// B5: o laco chama a cada volta. true = chegou resposta; *msg = cartaz (vazio = nada a dizer).
static bool RespostaSincronizar(std::wstring* msg) {
    if (!TryEnterCriticalSection(&g_conf.cs)) return false;
    const bool tem = g_conf.temResposta;
    RespostaSinc r;
    if (tem) { r = g_conf.resposta; g_conf.resposta = RespostaSinc{}; g_conf.temResposta = false; }
    LeaveCriticalSection(&g_conf.cs);
    if (!tem) return false;
    if (!r.falhou) g_est.jogoLimite = r.jogoLimite;
    if (!r.erro.empty()) { g_est.linha = r.erro; g_est.linhaAviso = true; }
    if (msg) *msg = r.msg;
    return true;
}

// O GUARDIAO (ver LancarGuardiao). Chamar na PRIMEIRA linha do wWinMain, antes do mutex de
// instancia unica: true = esta copia era o guardiao e ja fez o trabalho; o host retorna 0.
// Log proprio (dlss5forall.guardiao.log, ao lado do ini) porque o log do app esta aberto em "w"
// pelo processo vivo; o resultado tambem vai para o ini (guardiao_restaurou) e o arranque
// seguinte o repete no log principal.
static FILE* g_logGuardiao = nullptr;
static bool Guardiao() {
    int n = 0; LPWSTR* av = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!av) return false;
    DWORD pid = 0; std::wstring ini;
    for (int i = 1; i + 2 < n; i++) if (std::wstring(av[i]) == L"--guardiao") { pid = (DWORD)_wtoi(av[i + 1]); ini = av[i + 2]; break; }
    LocalFree(av);
    if (!pid || ini.empty()) return false;
    g_iniPath = ini;
    const std::wstring dir = ini.substr(0, ini.find_last_of(L"\\/") + 1);
    g_logGuardiao = _wfopen((dir + L"dlss5forall.guardiao.log").c_str(), L"a");
    g_logFn = [](const char* s) {
        if (!g_logGuardiao) return;
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(g_logGuardiao, "[%02d/%02d %02d:%02d:%02d] guardiao: %s\n", st.wDay, st.wMonth, st.wHour, st.wMinute, st.wSecond, s);
        fflush(g_logGuardiao);
    };
    HANDLE hp = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (hp) { WaitForSingleObject(hp, INFINITE); CloseHandle(hp); }
    else Logf("nao consegui abrir o app pid %lu (erro %lu) - ja morreu? seguindo", (unsigned long)pid, (unsigned long)GetLastError());
    LimiteLerIni(); JanelaLerIni();
    if (g_lim.exe.empty()) { Logf("app pid %lu saiu limpo (ini sem chave) - nada a fazer", (unsigned long)pid); }
    else {
        Logf("app pid %lu morreu com %s a %d fps no driver - restaurando", (unsigned long)pid, U8(g_lim.exe).c_str(), g_lim.aplicado);
        const std::wstring exe = g_lim.exe; const int fps = g_lim.aplicado;
        const bool ok = LimiteJogoRestaurar();
        Logf("%s: %s", U8(exe).c_str(), ok ? "restaurado" : "FALHOU (fica para a proxima abertura do app)");
        const std::string h = HoraLocal(AgoraFt());
        if (ok) IniSetD(L"guardiao_restaurou", exe + L" " + std::to_wstring(fps) + L" fps as " + std::wstring(h.begin(), h.end()));
    }
    if (nv::g_api.ok && nv::g_api.Unload) nv::g_api.Unload();
    if (g_logGuardiao) fclose(g_logGuardiao);
    return true;
}

static void Iniciar(const std::wstring& iniPath) {
    g_iniPath = iniPath;
    LimiteLerIni();
    JanelaLerIni();
    ModoLerIni();
    // MIGRACAO (24/09/2026, Rodada 21, item 7 do dono): os chips "FPS do jogo" sairam do menu
    // End porque limitar o fps do jogo pelo driver causava problemas ao ligar. Quem tinha
    // escolhido um chip (chave [desempenho] modo= gravada por sessoes anteriores) continuaria
    // tendo o limite reaplicado sozinho pelo Sincronizar() do laco, sem UI nenhuma para ver ou
    // desligar isso - por isso a escolha e desfeita aqui, uma vez, no arranque.
    if (g_modoEscolhido) {
        Logf("limite do jogo: a opcao 'FPS do jogo' saiu do menu - desfazendo a escolha anterior (modo=%d) do ini",
             g_modo);
        g_modoEscolhido = false; g_modo = JOGO_LIVRE;
        ModoGravarIni();   // apaga a chave [desempenho] modo= (ModoGravarIni grava "" quando !g_modoEscolhido)
    }
    const std::wstring gr = IniGetD(L"guardiao_restaurou");
    if (!gr.empty()) { Logf("limite do jogo: o guardiao restaurou %s depois que o app morreu sem fechar direito (ver dlss5forall.guardiao.log)", U8(gr).c_str()); IniSetD(L"guardiao_restaurou", L""); }
    if (!g_lim.exe.empty()) {
        Logf("limite do jogo: sobrou %s a %d fps de uma sessao anterior (aplicado as %s) - chave orfa, apagando",
             U8(g_lim.exe).c_str(), g_lim.aplicado, HoraLocal(g_jan.de).c_str());
        if (LimiteJogoRestaurar()) Logf("limite do jogo: chave orfa apagada (%s)", U8(g_jan.exe).c_str());
    }
    g_filtroAnterior = SetUnhandledExceptionFilter(FiltroCrash);
    const int hags = HagsLigado();
    Logf("desempenho: nvapi=%d hags=%s prioridade de gpu atual=%d modo=%s%s", LimiteJogoDisponivel() ? 1 : 0,
         hags < 0 ? "?" : hags ? "ligado" : "desligado", PrioridadeGpuAtual(),
         g_modo == JOGO_IGUAL ? "igual" : g_modo == 60 ? "60" : g_modo == 30 ? "30" : "livre", g_modoEscolhido ? " (escolhido)" : " (padrao)");
}
// Ao sair (caminho normal): devolve o driver como estava. A prioridade de GPU morre com o processo.
static void Encerrar() {
    if (g_otim.Ativo()) g_otim.Cancelar();
    if (g_conf.th) {   // B2: a conferencia termina antes da restauracao (a trava DRS garante a ordem mesmo se estourar; B5: um limite na fila nao e mais gravado)
        InterlockedExchange(&g_conf.parar, 1); SetEvent(g_conf.ev);
        WaitForSingleObject(g_conf.th, 3000);
        CloseHandle(g_conf.th); g_conf.th = nullptr;
    }
    LimiteJogoRestaurar();
    Max::Restaurar();
    if (nv::g_api.ok && nv::g_api.Unload) nv::g_api.Unload();
}

} // namespace Desempenho
