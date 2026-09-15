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
static bool PrioridadeGpuMaxima(int* anterior = nullptr) {
    if (PrioridadeGpu(PRIO_GPU_REALTIME, anterior)) return true;
    return PrioridadeGpu(PRIO_GPU_HIGH, anterior);
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
    HMODULE dll = nullptr; bool tentado = false, ok = false;
    PFN_Init Init = nullptr; PFN_Unload Unload = nullptr; PFN_ErrMsg ErrMsg = nullptr;
    PFN_CreateSession CreateSession = nullptr; PFN_DestroySession DestroySession = nullptr;
    PFN_LoadSettings LoadSettings = nullptr; PFN_SaveSettings SaveSettings = nullptr;
    PFN_CreateProfile CreateProfile = nullptr; PFN_DeleteProfile DeleteProfile = nullptr;
    PFN_FindProfileByName FindProfileByName = nullptr; PFN_GetProfileInfo GetProfileInfo = nullptr;
    PFN_FindAppByName FindAppByName = nullptr; PFN_CreateApp CreateApp = nullptr;
    PFN_GetSetting GetSetting = nullptr; PFN_SetSetting SetSetting = nullptr;
    PFN_DeleteSetting DeleteSetting = nullptr; PFN_GetBaseProfile GetBaseProfile = nullptr;

    bool Carregar() {
        if (tentado) return ok;
        tentado = true;
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
struct Sessao_ {
    Sessao s = nullptr; bool ok = false;
    Sessao_() {
        if (!g_api.Carregar()) return;
        Status st = g_api.CreateSession(&s);
        if (st != OK) { Logf("nvapi: CreateSession -> %s", g_api.Erro(st).c_str()); return; }
        st = g_api.LoadSettings(s);
        if (st != OK) { Logf("nvapi: LoadSettings -> %s", g_api.Erro(st).c_str()); return; }
        ok = true;
    }
    ~Sessao_() { if (s) g_api.DestroySession(s); }
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
static bool NasceuLimitado(unsigned long pid) {
    static unsigned long cPid = 0; static bool cRes = false; static unsigned long long cAte = 0;
    if (pid == cPid && cAte == g_jan.ate) return cRes;
    cPid = pid; cAte = g_jan.ate; cRes = false;
    if (!pid || g_jan.exe.empty() || !g_jan.de || pid == g_jan.pid) return false;
    if (Minusculas(ExeDoPid(pid)) != Minusculas(g_jan.exe)) return false;
    const unsigned long long c = HoraCriacao(pid);
    if (!c) return false;
    cRes = c >= g_jan.de && (g_jan.ate == 0 || c <= g_jan.ate);
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
        g_lim = LimiteJogo{};
        LimiteGravarIni();
        // A janela fecha AGORA, mas a memoria dela fica: quem nasceu dentro dela segue limitado.
        if (!g_jan.exe.empty() && g_jan.ate == 0) g_jan.ate = AgoraFt();
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
    // Trocou de jogo com um limite nosso pendurado no anterior? Devolve o anterior primeiro.
    if (!g_lim.exe.empty() && Minusculas(g_lim.exe) != Minusculas(exe)) LimiteJogoRestaurar();
    nv::Sessao_ s;
    if (!s.ok) { if (erro) *erro = L"NvAPI indisponivel"; return false; }
    bool criado = false; std::wstring nomePerfil;
    nv::Perfil p = s.PerfilDoExe(exe, true, &criado, &nomePerfil);
    if (!p) { if (erro) *erro = L"nao achei nem criei o perfil do jogo no driver"; return false; }
    if (g_lim.exe.empty()) {
        g_lim.exe = exe; g_lim.perfilCriado = criado; g_lim.original = criado ? -1 : s.LerFrl(p);
        g_lim.aplicado = fps;
        g_jan = JanelaLimite{}; g_jan.exe = exe; g_jan.fps = fps; g_jan.de = AgoraFt(); g_jan.pid = pidAlvo;
        LimiteGravarIni();          // ANTES de gravar no driver: se cairmos agora, a proxima abertura desfaz
        JanelaGravarIni();
        LancarGuardiao();           // ...e se cairmos de vez, o guardiao desfaz
    }
    // O pid vale para ESTA aplicacao, mesmo trocando so o valor (60 -> 30) sem soltar o limite:
    // o novo valor tambem so pega na proxima abertura, entao a pendencia recomeca daqui.
    g_lim.pidAoAplicar = pidAlvo;
    if (!s.GravarFrl(p, fps)) { if (erro) *erro = L"o driver recusou o limite"; return false; }
    if (!s.Salvar()) { if (erro) *erro = L"o driver nao salvou o perfil"; return false; }
    g_lim.aplicado = fps; LimiteGravarIni();
    g_jan.fps = fps; JanelaGravarIni();
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
static int EstadoDoLimite(unsigned long pidAlvo) {
    if (g_lim.exe.empty() || g_lim.aplicado <= 0) return NasceuLimitado(pidAlvo) ? LIM_HERDADO : LIM_NENHUM;
    if (kLimiteAoVivo) return LIM_VALENDO;                 // se um dia pegar ao vivo, nao ha pendencia
    if (!g_lim.pidAoAplicar || !pidAlvo) return LIM_INDEFINIDO;
    return (pidAlvo == g_lim.pidAoAplicar) ? LIM_PENDENTE : LIM_VALENDO;
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
// O modo em vigor: o escolhido, ou o padrao pelas camadas.
static int ModoAtual(int camadas) { return g_modoEscolhido ? g_modo : ModoPadrao(camadas); }
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
static const wchar_t* Explicacao(bool en) {
    // Segunda frase e o AVISO HONESTO (14/09/2026): a trava e gravada no perfil NVIDIA do jogo,
    // so vale quando o jogo abre, e o app a devolve ao sair (um guardiao cobre queda/kill).
    return en ? L"Capping the game at a fixed rate makes the filter steadier. Written to the game's NVIDIA profile, applies when the game opens, undone when this app closes."
              : L"Travar o jogo numa taxa fixa deixa o filtro mais regular. Grava no perfil NVIDIA do jogo, vale quando o jogo abre e é desfeito quando este app fecha.";
}
// A orientacao de valor, em linha propria. "Comeco" e nao "ideal": ver o bloco acima.
static const wchar_t* PontoDePartida(bool en) {
    return en ? L"60 is a good start — the best value is the highest your machine holds"
              : L"60 é um bom começo — o melhor é o maior que a sua máquina segura";
}
// A negativa, desenhada numa linha propria pelo painel. Mesmo motivo da negativa do teto do
// filtro: quem chega aqui procurando outra coisa le a legenda como se falasse dessa outra
// coisa, e dizer o que o controle NAO faz custa uma linha.
static const wchar_t* NaoMexeNoFiltro(bool en) {
    return en ? L"it does not change the filter's fps" : L"não mexe no fps do filtro";
}
static const wchar_t* RotuloSecao(bool en) { return en ? L"GAME FPS" : L"FPS DO JOGO"; }
static const wchar_t* ChipLivre(bool en)   { return en ? L"Free" : L"Livre"; }
static const wchar_t* ChipIgual(bool en)   { return en ? L"Same as layer" : L"Igual à película"; }
static const wchar_t* SemLimiteNavegador(bool en) { return en ? L"video in the browser: no cap available" : L"vídeo no navegador: sem limite disponível"; }
static const wchar_t* Botao(bool en)       { return en ? L"OPTIMIZE PERFORMANCE" : L"OTIMIZAR DESEMPENHO"; }
static const wchar_t* BotaoSub(bool en)    { return en ? L"measures 5 s per step and only goes as far as needed" : L"mede 5 s por degrau e só vai até onde precisa"; }
static const wchar_t* BotaoParar(bool en)  { return en ? L"Optimizing… click to stop" : L"Otimizando… clique para parar"; }
static const wchar_t* SemNvidia(bool en)   { return en ? L"not available on this card yet" : L"não disponível nesta placa ainda"; }
static const wchar_t* ProximaAbertura(bool en) { return en ? L"takes effect when the game is reopened" : L"vale quando o jogo for reaberto"; }
// O OPOSTO DELA, para quando a peca esta no jogo e respondendo. Existe porque o contrario -
// nao dizer nada - deixa a pessoa sem saber se precisa reabrir: ela acabou de ler essa
// exigencia em outras telas do programa, e silencio aqui ela le como "entao tambem preciso".
static const wchar_t* JaValendo(bool en) { return en ? L"already in effect — no need to reopen the game" : L"já está valendo — não precisa reabrir o jogo"; }
// A TARJA, que fica no painel enquanto o limite esperar a reabertura. O cartaz de 4 s ja
// existia e ja dizia a coisa certa, mas some: a pessoa le, volta para o jogo, olha o contador,
// nao ve mudanca, e a explicacao ja saiu da tela. Aviso que some e aviso que nao existiu no
// momento em que fez falta. Por isso a tarja e persistente, e so sai quando o limite pega.
static std::wstring TarjaPendente(bool en, int fps) {
    return (en ? L"Game capped at " : L"Jogo limitado a ") + std::to_wstring(fps) +
           (en ? L" fps — waiting for the game to be reopened" : L" fps — esperando você reabrir o jogo");
}
// E o cartaz do momento em que passa a valer, que fecha o ciclo: a pessoa fica sabendo sem ter
// de descobrir por tentativa.
// A TARJA DO HERDADO (14/09/2026): nao ha limite nosso AGORA, mas o processo vivo do jogo nasceu
// com a chave e o driver o segura ate ele ser reaberto. O painel dizia "Livre" aqui - e o
// contador do jogo mostrava 30. Fica ate o jogo ser reaberto (o pid muda e a tarja sai sozinha).
static std::wstring TarjaHerdado(bool en, int fps) {
    return (en ? L"The game opened with the driver cap of " : L"O jogo abriu com o limite de ") + std::to_wstring(fps) +
           (en ? L" fps — reopen the game to free it" : L" fps do driver — reabra o jogo para soltar");
}
static std::wstring AgoraValendo(bool en, int fps) {
    return (en ? L"The game cap is now in effect: " : L"O limite do jogo está valendo agora: ") +
           std::to_wstring(fps) + L" fps";
}
static const wchar_t* BotaoSubPendente(bool en) { return en ? L"the game cap only applies when the game is reopened" : L"o limite do jogo só vale quando o jogo for reaberto"; }
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
        feito.clear(); feitoAntes.clear();
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
        case D_JOGO60: case D_JOGO30: LimiteJogoRestaurar(); g_est.jogoLimite = 0; break;
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
                std::wstring erro;
                if (!LimiteJogoAplicar(ctx.exeAlvo, fps, &erro)) { Logf("otimizar: limite %d nao aplicado: %s", fps, U8(erro).c_str()); continue; }
                g_est.jogoLimite = fps;
                Feito((ctx.en ? L"game at " : L"jogo a ") + std::to_wstring(fps) + L" fps");
                if (!ctx.limiteAoVivo) {
                    // O driver so aplica na proxima abertura do jogo: nao adianta medir.
                    Terminar(false, mediaAtual, true);
                    return;
                }
                Toast((ctx.en ? L"Optimizing… game capped at " : L"Otimizando… jogo limitado a ") + std::to_wstring(fps) + L" fps");
                return;
            }
            default:
                continue;
            }
        }
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
static std::wstring Sincronizar(const std::wstring& exe, int camadas, int tetoCamada, bool en,
                                unsigned long pidAlvo = 0, bool aoVivo = false) {
    const int modo = ModoAtual(camadas);
    int pedido = LimitePedido(modo, tetoCamada);
    if (exe.empty() || EhNavegador(exe) || !LimiteJogoDisponivel()) pedido = 0;
    const int atual = (!g_lim.exe.empty() && Minusculas(g_lim.exe) == Minusculas(exe)) ? g_lim.aplicado : 0;
    if (pedido == atual) { g_est.jogoLimite = atual; return L""; }
    std::wstring erro;
    if (pedido == 0) {
        LimiteJogoRestaurar(); g_est.jogoLimite = 0;
        if (aoVivo) return en ? L"Game: free again" : L"Jogo: livre de novo";
        return en ? L"Game: free again\nit stays capped until it is reopened" : L"Jogo: livre de novo\ncontinua limitado até ser reaberto";
    }
    if (!LimiteJogoAplicar(exe, pedido, &erro, pidAlvo)) {
        g_est.linha = erro; g_est.linhaAviso = true;
        return (en ? L"Could not cap the game: " : L"Não deu para limitar o jogo: ") + erro;
    }
    g_est.jogoLimite = pedido;
    std::wstring r = (en ? L"Game capped at " : L"Jogo limitado a ") + std::to_wstring(pedido) + L" fps";
    if (modo == JOGO_IGUAL) r += en ? L" (same as the layer)" : L" (igual à película)";
    if (!kLimiteAoVivo && !aoVivo) r += std::wstring(L"\n") + Txt::ProximaAbertura(en);
    if (aoVivo)         r += std::wstring(L"\n") + Txt::JaValendo(en);
    return r;
}

// Chip manual "Jogo: Livre / 60 / 30 / Igual a pelicula". Grava a escolha e sincroniza.
static std::wstring Manual(const std::wstring& exe, int modo, int camadas, int tetoCamada, bool en,
                           unsigned long pidAlvo = 0, bool aoVivo = false) {
    g_modo = modo; g_modoEscolhido = true; ModoGravarIni();
    Logf("limite do jogo: modo %s escolhido no painel", modo == JOGO_IGUAL ? "igual a pelicula" : modo == 60 ? "60" : modo == 30 ? "30" : "livre");
    std::wstring r = Sincronizar(exe, camadas, tetoCamada, en, pidAlvo, aoVivo);
    if (r.empty()) {
        if (EhNavegador(exe)) return en ? L"Video in the browser: no cap available" : L"Vídeo no navegador: sem limite disponível";
        if (modo == JOGO_IGUAL && tetoCamada == 0) return en ? L"Game follows the layer: no cap while the layer is free" : L"Jogo igual à película: sem limite enquanto a película estiver livre";
        return en ? L"Game: no change" : L"Jogo: nada mudou";
    }
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
    if (!g_lim.exe.empty()) {
        Logf("limite do jogo: saida de emergencia (%s) com %s a %d fps no driver - restaurando", de, U8(g_lim.exe).c_str(), g_lim.aplicado);
        LimiteJogoRestaurar();
    }
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
        if (ok && g_jan.ate == 0 && !g_jan.exe.empty()) { g_jan.ate = AgoraFt(); JanelaGravarIni(); }
        return;
    }
    Logf("varredura: o perfil '%s' de %s ja tem FRL=%d SEM registro nosso - nao mexi (painel da NVIDIA?)", U8(nomePerfil).c_str(), U8(exe).c_str(), frl);
    g_est.linha = L"O driver já limita este jogo a " + std::to_wstring(frl) + L" fps (perfil NVIDIA, não é nosso)";
    g_est.linhaAviso = true;
}

// A LINHA DE PROVA, a cada 10 s (14/09/2026), junto do LAT 10s do screen_capture.h: o que o driver
// tem no perfil AGORA (1 GetSetting/10 s), quando o pid nasceu, em que pe o limite esta e o
// refresh medido. E o que separa "o jogo esta a 30 pelo driver" de "a captura esta a 30".
// Chamar a cada 1 s do laco principal com o exe/pid do alvo; ela mesma se limita a 10 s.
static void Situacao(const std::wstring& exe, unsigned long pid, int (*refreshHz)()) {
    static ULONGLONG ultimo = 0; static std::wstring exeVarrido;
    if (exe.empty()) return;
    if (Minusculas(exe) != exeVarrido) { exeVarrido = Minusculas(exe); VarrerPerfil(exe); ultimo = 0; }
    const ULONGLONG agora = GetTickCount64();
    if (ultimo && agora - ultimo < 10000) return;
    ultimo = agora;
    const int frl = LimiteJogoAtual(exe);
    const int est = EstadoDoLimite(pid);
    const char* nome = est == LIM_PENDENTE ? "pendente" : est == LIM_VALENDO ? "valendo" : est == LIM_HERDADO ? "herdado" : est == LIM_INDEFINIDO ? "indefinido" : "livre";
    char frlTxt[16]; if (frl < 0) snprintf(frlTxt, sizeof frlTxt, "ausente"); else snprintf(frlTxt, sizeof frlTxt, "%d", frl);
    Logf("jogo: %s pid %lu nasceu %s | FRL no perfil agora=%s | estado=%s herdado=%s | refresh %d Hz",
         U8(exe).c_str(), pid, HoraLocal(HoraCriacao(pid)).c_str(), frlTxt, nome, est == LIM_HERDADO ? "sim" : "nao", refreshHz ? refreshHz() : 0);
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
    LimiteJogoRestaurar();
    if (nv::g_api.ok && nv::g_api.Unload) nv::g_api.Unload();
}

} // namespace Desempenho
