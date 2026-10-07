// =====================================================================================
//  reinicio_auto.h - REINICIO AUTOMATICO DO PROCESSO DEPOIS DO RESET DA PLACA
//  (OCLUSAO-2309 sec. 22; PESQ-RESET-DRIVER-2309 sec. 3.4/3.5)
//
//  O FATO QUE OBRIGA ISTO: no D3D12 o device e singleton por adaptador POR PROCESSO. Depois de
//  um DEVICE_HUNG, enquanto QUALQUER referencia ao device removido viver no processo (feed,
//  NGX, ReShade, ponte), todo D3D12CreateDevice na mesma placa devolve 0x887A0006. O feed
//  segura a dele de proposito (sem Shutdown1 do core, nunca) e o ReShade segura pela cadeia.
//  Entao nao ha recuperacao DENTRO do processo: o caminho e sair e nascer de novo.
//
//  AS TRES PECAS DESTE ARQUIVO (o disparo no processo que caiu mora no main.cpp, junto de
//  ReabrirApp, porque precisa de ArgsDeReabertura e do apresentador):
//
//   1. A SONDA (`--sonda-d3d12 <luid> <arquivo>`): D3D12CreateDevice + fila + fence com um
//      Signal trivial, num processo novo. O corpo mora em sonda.h e, desde 07/10/2026, quem a
//      RODA e o launcher da raiz (..\DLSS5ForAll.exe): na pasta do app o dxgi.dll e o ReShade,
//      e um device criado ali carregaria o ReShade e os add-ons (o feed montaria rede em cima
//      da sonda). Na raiz, o dxgi.dll e o do Windows. (Ate a 2.1: uma COPIA deste exe no %TEMP%.)
//   2. O LANCADOR (`--relancar <pid> <ini>`): processo leve, sem device nenhum. Espera o
//      processo que caiu morrer, espera mais 2,5 s, roda a sonda (ate 3 vezes, de 2 em 2 s);
//      passou, abre o app com o estado salvo no ini; nao passou, explica e oferece o
//      "Reiniciar video do Windows".
//   3. O "REINICIAR VIDEO DO WINDOWS": Win+Ctrl+Shift+B por SendInput. HONESTO: o atalho
//      recria as superficies do DWM; reiniciar o driver e efeito colateral, sem garantia
//      (PESQ-RESET-DRIVER sec. 2). Nunca automatico; so com clique e confirmacao, e nunca com
//      jogo em tela cheia aberto (o atalho pode derrubar o jogo).
//
//  CHAVES ([ultima] do dlss5forall.ini):
//    reinicio_auto        1 (padrao) | 0 = o comportamento antigo (passagem segura, cartaz)
//    reinicio_auto_em     segundos desde 1601 da ultima reabertura automatica (a cota: 1 a cada 3 min)
//    reinicio_auto_n      quantas reaberturas automaticas ja houve (so conta)
//    reinicio_auto_ult    o que aconteceu na ultima: disparado | reaberto | sonda-falhou | cota | lancador-falhou
//    reinicio_auto_args   a linha de comando da copia nova (ArgsDeReabertura + --reinicio-auto)
//    reinicio_auto_luid   a placa a sondar (hex alto:baixo)
//    video_reiniciado_em  segundos desde 1601 do ultimo "Reiniciar video do Windows" (so registro)
// =====================================================================================
#pragma once
// 07/10/2026: o CORPO das sondas (SondaMain / SondaNgxMain) mora em sonda.h e so o LAUNCHER o compila;
// este arquivo so lanca o launcher com --sonda-*, espera e le o resultado (RodarSonda / RodarSondaNgx).

namespace ReinicioAuto {

constexpr ULONGLONG kCotaSeg = 180;   // 1 reabertura automatica a cada 3 min

static ULONGLONG Agora1601() {
    FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
    return (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000ull;
}
static std::wstring IniLer(const std::wstring& ini, const wchar_t* k) {
    wchar_t b[2048]{}; GetPrivateProfileStringW(L"ultima", k, L"", b, 2048, ini.c_str()); return b;
}
static void IniGravar(const std::wstring& ini, const wchar_t* k, const std::wstring& v) {
    WritePrivateProfileStringW(L"ultima", k, v.c_str(), ini.c_str());
}
static std::wstring MeuExe() {
    wchar_t p[MAX_PATH]{}; const DWORD n = GetModuleFileNameW(nullptr, p, MAX_PATH);
    return (n && n < MAX_PATH) ? std::wstring(p) : std::wstring();
}
static std::wstring PastaDe(const std::wstring& caminho) {
    const size_t b = caminho.find_last_of(L"\\/");
    return b == std::wstring::npos ? std::wstring() : caminho.substr(0, b + 1);
}
// 07/10/2026: QUEM RODA AS SONDAS E O LAUNCHER DA RAIZ (sonda.h). Ate a 2.1 este exe se copiava
// para %TEMP%\DLSS5ForAll-sonda\ (CopiarSondaSeMudou) e rodava a copia oculta; isso saiu.
//
// Os dois caminhos sao o MESMO arquivo? (volume + indice: um link rigido da camada conta como igual)
static bool MesmoArquivoSonda(const std::wstring& a, const std::wstring& b) {
    auto abrir = [](const std::wstring& p) {
        return CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    };
    HANDLE ha = abrir(a);
    if (ha == INVALID_HANDLE_VALUE) return false;
    HANDLE hb = abrir(b);
    if (hb == INVALID_HANDLE_VALUE) { CloseHandle(ha); return false; }
    BY_HANDLE_FILE_INFORMATION ia{}, ib{};
    const bool igual = GetFileInformationByHandle(ha, &ia) && GetFileInformationByHandle(hb, &ib) &&
                       ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
                       ia.nFileIndexHigh == ib.nFileIndexHigh && ia.nFileIndexLow == ib.nFileIndexLow;
    CloseHandle(ha); CloseHandle(hb);
    return igual;
}
// O launcher que roda as sondas: o DLSS5ForAll.exe da pasta de cima (bin\ -> raiz). Na camada
// (<bin>\camada<k>\, cujo "de cima" e o proprio app por link rigido) sobe mais um nivel. Nunca este
// mesmo exe. No build de desenvolvimento (app na bancada src\) vale o portable-launcher\ ao lado.
// Vazio = nao achei (a sonda devolve 16, "nao rodou", e o app abre assim mesmo).
static std::wstring LauncherDaSonda() {
    const std::wstring eu = MeuExe();
    std::wstring d = PastaDe(eu);
    for (int nivel = 0; nivel < 2 && !d.empty(); nivel++) {
        std::wstring sem = d;
        while (!sem.empty() && (sem.back() == L'\\' || sem.back() == L'/')) sem.pop_back();
        d = PastaDe(sem);
        if (d.empty()) break;
        const std::wstring c = d + L"DLSS5ForAll.exe";
        if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES && !MesmoArquivoSonda(c, eu)) return c;
    }
#ifdef DLSS5_DEV
    const std::wstring dev = PastaDe(eu) + L"portable-launcher\\DLSS5ForAll.exe";
    if (GetFileAttributesW(dev.c_str()) != INVALID_FILE_ATTRIBUTES) return dev;
#endif
    return L"";
}
// A PASTA DE DADOS da sonda (o arquivo de resultado e os arquivos do NGX dela): a subpasta "sonda" da pasta
// de dados do app - g_dadosDir (bin\, ou %LOCALAPPDATA%\DLSS5ForAll quando o bin\ nao aceita escrita; ver
// EscolherPastaDeDados) ou, no processo leve --relancar, a pasta do ini que ele recebeu. Nunca o %TEMP%.
// Termina em barra; vazio so se a pasta nao puder ser criada.
static std::wstring g_lIni;   // o ini do --relancar (ver RelancarMain)
static std::wstring PastaDadosSonda() {
    std::wstring base = !g_dadosDir.empty() ? g_dadosDir : !g_lIni.empty() ? PastaDe(g_lIni) : PastaDe(MeuExe());
    if (base.empty()) return L"";
    if (base.back() != L'\\' && base.back() != L'/') base += L'\\';
    const std::wstring s = base + L"sonda\\";
    CreateDirectoryW(s.c_str(), nullptr);
    return GetFileAttributesW(s.c_str()) != INVALID_FILE_ATTRIBUTES ? s : std::wstring();
}

// ---------------------------------------------------------------- 0. LANCAR LIMPO (MGR-2309 item 7)
// Licao observada no DLSS5 Autopilot (MIT, v2.0.3 #287: o jogo aberto pela ferramenta herdou o
// diretorio de DLLs do pai) - codigo proprio. Todo processo que o app lanca (sonda, lancador,
// o futuro lancador de jogos) nasce:
//   - com o diretorio de trabalho = pasta do ALVO (nunca a nossa `bin\`, onde o dxgi.dll e o
//     ReShade: um jogo que achasse o nosso dxgi.dll carregaria o ReShade e os add-ons);
//   - sem o diretorio de DLLs deste processo: se alguem aqui dentro (ReShade, NGX, um add-on)
//     tiver chamado SetDllDirectory, o CreateProcess repassa esse caminho ao filho. Ele e
//     zerado durante a criacao e devolvido logo depois (com log quando havia algum);
//   - com o PATH sem a pasta do app, e - com `semVarsNossas` - sem as variaveis DLSS5_*.
// Devolve o BOOL do CreateProcessW (GetLastError preservado).
static std::vector<wchar_t> AmbienteLimpo(const std::wstring& pastaApp, bool semVarsNossas) {
    std::vector<wchar_t> out;
    std::wstring app = pastaApp;
    while (!app.empty() && (app.back() == L'\\' || app.back() == L'/')) app.pop_back();
    auto igualPasta = [&](std::wstring p) {
        while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
        return !app.empty() && _wcsicmp(p.c_str(), app.c_str()) == 0;
    };
    wchar_t* bloco = GetEnvironmentStringsW();
    if (!bloco) return out;
    for (const wchar_t* e = bloco; *e; e += wcslen(e) + 1) {
        std::wstring v = e;
        if (semVarsNossas && _wcsnicmp(v.c_str(), L"DLSS5_", 6) == 0) continue;
        if (_wcsnicmp(v.c_str(), L"PATH=", 5) == 0) {
            std::wstring novo = L"PATH=", resto = v.substr(5);
            size_t i = 0;
            bool primeiro = true;
            while (i <= resto.size()) {
                const size_t f = resto.find(L';', i);
                const std::wstring item = resto.substr(i, f == std::wstring::npos ? std::wstring::npos : f - i);
                if (!item.empty() && !igualPasta(item)) { if (!primeiro) novo += L';'; novo += item; primeiro = false; }
                if (f == std::wstring::npos) break;
                i = f + 1;
            }
            v = novo;
        }
        out.insert(out.end(), v.begin(), v.end());
        out.push_back(0);
    }
    FreeEnvironmentStringsW(bloco);
    out.push_back(0);
    return out;
}
static BOOL CriarProcessoLimpo(const std::wstring& exe, const std::wstring& cmd, DWORD flags,
                               bool semVarsNossas, PROCESS_INFORMATION* pi, const std::wstring& pastaApp) {
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    std::vector<wchar_t> env = AmbienteLimpo(pastaApp, semVarsNossas);
    const std::wstring cwd = PastaDe(exe);
    wchar_t dllDir[MAX_PATH]{};
    const DWORD nDll = GetDllDirectoryW(MAX_PATH, dllDir);
    if (nDll) SetDllDirectoryW(nullptr);
    STARTUPINFOW si{}; si.cb = sizeof(si);
    const BOOL ok = CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE,
                                   flags | (env.size() > 1 ? CREATE_UNICODE_ENVIRONMENT : 0),
                                   env.size() > 1 ? env.data() : nullptr,
                                   cwd.empty() ? nullptr : cwd.c_str(), &si, pi);
    const DWORD err = GetLastError();
    if (nDll && nDll < MAX_PATH) SetDllDirectoryW(dllDir);
    if (nDll) {
        // Sem Log aqui (este arquivo roda tambem nos processos leves): quem chama decide. O
        // arquivo de rastro fica no %TEMP% para quem investigar - uma linha por lancamento.
        wchar_t tmp[MAX_PATH]{};
        if (GetTempPathW(MAX_PATH, tmp)) {
            if (FILE* f = _wfopen((std::wstring(tmp) + L"DLSS5ForAll-lancar.txt").c_str(), L"a")) {
                fwprintf(f, L"lancar limpo: o processo tinha SetDllDirectory(\"%ls\") - zerado para %ls\n", dllDir, exe.c_str());
                fclose(f);
            }
        }
    }
    SetLastError(err);
    return ok;
}

// O launcher e a pasta de dados da sonda; false (com `det` e o codigo 16, "nao rodou") se faltar um dos dois.
static bool PrepararSonda(const wchar_t* nomeRes, std::wstring* launcher, std::wstring* res, std::string* det) {
    *launcher = LauncherDaSonda();
    if (launcher->empty()) { if (det) *det = "sem o launcher (..\\DLSS5ForAll.exe) para rodar a sonda"; return false; }
    const std::wstring dados = PastaDadosSonda();
    if (dados.empty()) { if (det) *det = "sem pasta de dados com escrita para o resultado da sonda"; return false; }
    *res = dados + nomeRes;
    DeleteFileW(res->c_str());
    return true;
}

// Roda a sonda num processo novo, pelo launcher da raiz (fora da pasta do app). Devolve o codigo;
// `det` recebe a linha que a sonda escreveu.
static int RodarSonda(const std::wstring& luid, std::string* det) {
    if (det) det->clear();
    const std::wstring exe = MeuExe();
    std::wstring launcher, res;
    if (exe.empty() || !PrepararSonda(L"sonda-resultado.txt", &launcher, &res, det)) { if (det && det->empty()) *det = "sem caminho do exe"; return 16; }
    std::wstring cmd = L"\"" + launcher + L"\" --sonda-d3d12 " + (luid.empty() ? std::wstring(L"-") : luid) + L" \"" + res + L"\"";
    PROCESS_INFORMATION pi{};
    // MGR-2309 item 7: cwd = pasta do launcher (a raiz), sem o nosso diretorio de DLLs, PATH sem a pasta do app,
    // sem DLSS5_*. Sem CREATE_NO_WINDOW: o launcher e -mwindows (nao abre console) e a sonda nao cria janela.
    if (!CriarProcessoLimpo(launcher, cmd, 0, true, &pi, PastaDe(exe))) {
        if (det) { char b[80]; snprintf(b, sizeof b, "CreateProcess da sonda falhou (erro %lu)", (unsigned long)GetLastError()); *det = b; }
        return 15;
    }
    DWORD cod = 14;
    if (WaitForSingleObject(pi.hProcess, 8000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &cod);
    else { TerminateProcess(pi.hProcess, 14); if (det) *det = "a sonda nao voltou em 8 s"; }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    if (det && det->empty()) {
        if (FILE* f = _wfopen(res.c_str(), L"r")) { char b[256]{}; if (fgets(b, sizeof b, f)) *det = b; fclose(f); }
        while (!det->empty() && (det->back() == '\n' || det->back() == '\r')) det->pop_back();
    }
    return (int)cod;
}

// Roda a sonda da rede e ESPERA no maximo `tetoMs` (60 s de fabrica). Mesmo launcher/pasta da sonda D3D12.
// v2.1: `aCada` (opcional) e chamado a cada ~100 ms da espera com o tempo decorrido - e por onde o app
// mostra o aviso "Preparando a rede do DLSS 5..." e mantem as mensagens andando; com ele a espera e um
// MsgWaitForMultipleObjects, para a fila de mensagens desta thread nao parar por um minuto.
static int RodarSondaNgx(const std::wstring& pastaApp, DWORD tetoMs, std::string* det, void (*aCada)(DWORD msDecorridos) = nullptr) {
    if (det) det->clear();
    const std::wstring exe = MeuExe();
    std::wstring launcher, res;
    if (exe.empty() || !PrepararSonda(L"sonda-resultado-ngx.txt", &launcher, &res, det)) { if (det && det->empty()) *det = "sem caminho do exe"; return 16; }
    std::wstring pa = pastaApp; while (!pa.empty() && pa.back() == L'\\') pa.pop_back();
    std::wstring cmd = L"\"" + launcher + L"\" --sonda-ngx \"" + pa + L"\" \"" + res + L"\"";
    PROCESS_INFORMATION pi{};
    // MGR-2309 item 7: cwd = pasta do launcher (a raiz), sem o nosso diretorio de DLLs, PATH sem a pasta do app,
    // sem DLSS5_*. Sem CREATE_NO_WINDOW: o launcher e -mwindows (nao abre console) e a sonda nao cria janela.
    if (!CriarProcessoLimpo(launcher, cmd, 0, true, &pi, PastaDe(exe))) {
        if (det) { char b[80]; snprintf(b, sizeof b, "CreateProcess da sonda falhou (erro %lu)", (unsigned long)GetLastError()); *det = b; }
        return 15;
    }
    DWORD cod = 14;
    bool voltou = false;
    if (!aCada) {
        voltou = WaitForSingleObject(pi.hProcess, tetoMs) == WAIT_OBJECT_0;
    } else {
        const ULONGLONG t0 = GetTickCount64();
        for (;;) {
            const ULONGLONG dec = GetTickCount64() - t0;
            if (dec >= tetoMs) break;
            const DWORD fatia = (DWORD)((tetoMs - dec) < 100 ? (tetoMs - dec) : 100);
            if (MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, fatia, QS_ALLINPUT) == WAIT_OBJECT_0) { voltou = true; break; }
            aCada((DWORD)(GetTickCount64() - t0));
        }
    }
    if (voltou) GetExitCodeProcess(pi.hProcess, &cod);
    else { TerminateProcess(pi.hProcess, 14); WaitForSingleObject(pi.hProcess, 2000); if (det) { char b[64]; snprintf(b, sizeof b, "a sonda da rede nao voltou em %lu ms", (unsigned long)tetoMs); *det = b; } }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    if (det && det->empty()) {
        if (FILE* f = _wfopen(res.c_str(), L"r")) { char b[256]{}; if (fgets(b, sizeof b, f)) *det = b; fclose(f); }
        while (!det->empty() && (det->back() == '\n' || det->back() == '\r')) det->pop_back();
        if (det->empty() && cod != 0) { char b[64]; snprintf(b, sizeof b, "a sonda caiu (codigo 0x%08lX)", (unsigned long)cod); *det = b; }
    }
    return (int)cod;
}

// ---------------------------------------------------------------- 3. Win+Ctrl+Shift+B
// Devolve quantos eventos o SendInput aceitou (8 = todos). O UIPI bloqueia se a janela em foco
// for de um processo elevado - ai devolve 0 e quem chamou diz para a pessoa apertar.
static UINT DispararAtalhoVideo() {
    INPUT in[8]{};
    const WORD vk[4] = { VK_LWIN, VK_LCONTROL, VK_LSHIFT, 'B' };
    for (int i = 0; i < 4; i++) { in[i].type = INPUT_KEYBOARD; in[i].ki.wVk = vk[i]; }
    for (int i = 0; i < 4; i++) { in[4 + i].type = INPUT_KEYBOARD; in[4 + i].ki.wVk = vk[3 - i]; in[4 + i].ki.dwFlags = KEYEVENTF_KEYUP; }
    return SendInput(8, in, sizeof(INPUT));
}

// Ha janela de OUTRO processo cobrindo um monitor inteiro (jogo/app 3D em tela cheia)? Olha a janela em
// primeiro plano e, se ela for nossa (o clique no botao a faz nossa), a de cima de tudo que nao e nossa,
// na ordem Z. Fundo da area de trabalho e barra de tarefas nao contam.
//
// Rodada 22 (24/09/2026): BUG DO DONO — este botao se recusou dizendo "ha um jogo em tela
// cheia" quando a janela era o "Shell Handwriting Canvas" (TextInputHost.exe, o canvas de
// tinta/escrita a mao do Windows): cobre a tela inteira, tem titulo e nao estava cloaked -
// passava pelas checagens de entao. `JanelaPodeSerJogo` (main.cpp) e agora o criterio unico,
// compartilhado com `JogoEmTelaCheia` (seletor_hud.h) e `JanelaSobAPelicula` (a oclusao).
static HWND JanelaTelaCheiaAlheia(std::wstring* titulo) {
    struct Ctx { HWND achou; } c{ nullptr };
    auto cobre = [](HWND w) -> bool {
        if (!JanelaPodeSerJogo(w)) return false;
        RECT r{}; if (!GetWindowRect(w, &r)) return false;
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi)) return false;
        return r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom;
    };
    HWND fg = GetForegroundWindow();
    if (cobre(fg)) c.achou = fg;
    else {
        struct L { static BOOL CALLBACK Cb(HWND w, LPARAM lp) {
            Ctx* x = (Ctx*)lp;
            // a de cima que nao e nossa: so ela conta (uma janela comum por cima do jogo tira o "tela cheia")
            RECT r{}; if (!GetWindowRect(w, &r) || r.right - r.left < 64 || r.bottom - r.top < 64) return TRUE;
            if (!JanelaPodeSerJogo(w)) return TRUE;
            x->achou = w;
            return FALSE;
        } };
        EnumWindows(&L::Cb, (LPARAM)&c);
        if (c.achou && !cobre(c.achou)) c.achou = nullptr;
    }
    if (c.achou && titulo) {
        // NOMEIA O PROCESSO (pedido do dono, depois do bug do canvas): "Forza Horizon 5
        // (ForzaHorizon5.exe)", nao so o titulo - ajuda a pessoa a reconhecer o que e.
        wchar_t t[128]{}; GetWindowTextW(c.achou, t, 128);
        const std::wstring proc = WgcProcessName(c.achou);
        *titulo = proc.empty() ? std::wstring(t) : (std::wstring(t) + L" (" + proc + L")");
    }
    return c.achou;
}

// ---------------------------------------------------------------- SO BANCADA: o teste A (PESQ-RESET-DRIVER sec. 6, T-A)
// `DLSS5_REINICIO_TESTE=N` no ambiente: N s depois de o laco comecar, pede o device D3D12 da placa do filtro NESTE
// processo (singleton por adaptador: e o MESMO objeto do feed) e chama ID3D12Device5::RemoveDevice(). Marca so o nosso
// device como removido, sem reset de placa nem TDR - reproduz o 0x887A0006 da passagem segura e o singleton morto.
// Sem a variavel, nada disto existe.
static bool TesteRemoverDevice(const std::wstring& luidTxt, std::string* det) {
    LUID alvo{}; bool temAlvo = false;
    const size_t dp = luidTxt.find(L':');
    if (dp != std::wstring::npos) { alvo.HighPart = (LONG)wcstoul(luidTxt.c_str(), nullptr, 16); alvo.LowPart = (DWORD)wcstoul(luidTxt.c_str() + dp + 1, nullptr, 16); temAlvo = true; }
    IDXGIFactory1* fab = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fab)) || !fab) { *det = "CreateDXGIFactory1 falhou"; return false; }
    IDXGIAdapter1* ad = nullptr;
    for (UINT i = 0; ; i++) {
        IDXGIAdapter1* a = nullptr;
        if (fab->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
        const bool casa = temAlvo ? (d.AdapterLuid.HighPart == alvo.HighPart && d.AdapterLuid.LowPart == alvo.LowPart) : !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE);
        if (casa && !ad) ad = a; else a->Release();
    }
    fab->Release();
    if (!ad) { *det = "placa nao encontrada"; return false; }
    ID3D12Device* dev = nullptr;
    const HRESULT hr = D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev);
    ad->Release();
    if (FAILED(hr) || !dev) { char b[80]; snprintf(b, sizeof b, "D3D12CreateDevice 0x%08X (ja removido?)", (unsigned)hr); *det = b; return false; }
    ID3D12Device5* d5 = nullptr;
    bool ok = SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device5), (void**)&d5)) && d5;
    if (ok) { d5->RemoveDevice(); d5->Release(); *det = "RemoveDevice chamado no device singleton da placa"; }
    else *det = "sem ID3D12Device5";
    dev->Release();
    return ok;
}

// ---------------------------------------------------------------- 2. o lancador
// (g_lIni, o ini deste processo, esta declarado la em cima, junto de PastaDadosSonda)
static void LLog(const char* fmt, ...) {
    const std::wstring lp = PastaDe(g_lIni) + L"reinicio-auto.log";
    FILE* f = _wfopen(lp.c_str(), L"a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}
static bool AbrirApp(const std::wstring& args) {
    const std::wstring exe = MeuExe();
    // v2.0: espera o "conferido" da copia nova (lancamento.h) - este lancador sai logo depois, e
    // sem a espera a copia nova acharia o pai ja morto e cairia na porta do launcher.
    const HINSTANCE r = (HINSTANCE)Lancamento::AbrirEEsperarConferido(exe.c_str(), args.c_str(), PastaDe(exe).c_str());
    LLog("abrindo o app: %s %s -> %d", U8(exe).c_str(), U8(args).c_str(), (int)(INT_PTR)r);
    return (INT_PTR)r > 32;
}
// A sonda ate `vezes` vezes, de 2 em 2 s. true = a placa aceitou um device novo.
static bool SondarComRepeticao(const std::wstring& luid, int vezes, std::string* ult) {
    for (int i = 1; i <= vezes; i++) {
        const ULONGLONG t0 = GetTickCount64();
        const int cod = RodarSonda(luid, ult);
        LLog("sonda %d de %d: codigo %d (%s) em %llu ms", i, vezes, cod, ult ? ult->c_str() : "", (unsigned long long)(GetTickCount64() - t0));
        if (cod == 0) return true;
        if (cod == 15 || cod == 16) return false;   // nao e a placa: nao adianta repetir
        if (i < vezes) Sleep(2000);
    }
    return false;
}
// "Reiniciar video do Windows" com as travas do item 3 (jogo em tela cheia, confirmacao), a
// partir de uma janela sem dono (o lancador). true = disparou.
static bool VideoComTravas(HWND dono) {
    std::wstring jogo;
    if (JanelaTelaCheiaAlheia(&jogo)) {
        LLog("reiniciar video: ha uma janela em tela cheia (\"%s\") - nao disparo", U8(jogo).c_str());
        MessageBoxW(dono, (TS(L"Feche o jogo antes.\n\n\"", L"Close the game first.\n\n\"") + jogo +
                           TS(L"\" está em tela cheia, e o atalho do Windows pode fechá-lo.",
                              L"\" is in full screen, and the Windows shortcut may close it.")).c_str(),
                    L"DLSS5 For All", MB_ICONWARNING | MB_OK | MB_TOPMOST | MB_SETFOREGROUND);
        return false;
    }
    const int r = MessageBoxW(dono,
        T(L"Reiniciar o vídeo do Windows?\n\nA tela pisca e pode ficar preta por 1–2 s, e um jogo aberto pode fechar.\n\n"
          L"Aviso honesto: este atalho do Windows (Win+Ctrl+Shift+B) recria a imagem da área de trabalho; reiniciar o driver da placa "
          L"é efeito colateral e nem sempre acontece. Se o desempenho não voltar, reinicie o PC.",
          L"Restart Windows video?\n\nThe screen blinks and may go black for 1–2 s, and an open game may close.\n\n"
          L"Honest note: this Windows shortcut (Win+Ctrl+Shift+B) rebuilds the desktop image; restarting the graphics driver "
          L"is a side effect and does not always happen. If performance does not come back, restart the PC."),
        L"DLSS5 For All", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND);
    if (r != IDYES) { LLog("reiniciar video: a pessoa cancelou na confirmacao"); return false; }
    const UINT n = DispararAtalhoVideo();
    LLog("reiniciar video: Win+Ctrl+Shift+B enviado por SendInput (%u de 8 eventos aceitos)", n);
    if (!g_lIni.empty()) IniGravar(g_lIni, L"video_reiniciado_em", std::to_wstring(Agora1601()));
    if (n < 8)
        MessageBoxW(dono, T(L"O Windows não aceitou o atalho enviado pelo programa.\nAperte você mesmo Win+Ctrl+Shift+B e clique OK.",
                            L"Windows did not accept the shortcut sent by the program.\nPress Win+Ctrl+Shift+B yourself and click OK."),
                    L"DLSS5 For All", MB_ICONINFORMATION | MB_OK | MB_TOPMOST | MB_SETFOREGROUND);
    return true;
}

// `DLSS5ForAll.exe --relancar <pid> <ini>`: nasce do processo que viu a placa reiniciar, pouco antes de ele se encerrar.
static int RelancarMain(DWORD pid, const std::wstring& ini) {
    g_lIni = ini;
    CarregarIdioma(ini);
    LLog("lancador: o app (pid %lu) viu a placa reiniciar e esta saindo - esperando ele sair", (unsigned long)pid);
    if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
        const DWORD w = WaitForSingleObject(h, 15000);
        CloseHandle(h);
        if (w != WAIT_OBJECT_0) {
            LLog("lancador: o app nao saiu em 15 s - desisto (abrir outro agora bateria no mutex e no device preso)");
            IniGravar(ini, L"reinicio_auto_ult", L"lancador-falhou");
            MessageBoxW(nullptr, T(L"A placa de vídeo reiniciou e o programa não conseguiu fechar sozinho.\nFeche o DLSS5 For All pelo Gerenciador de Tarefas e abra de novo.",
                                   L"The graphics card reset and the program could not close by itself.\nClose DLSS5 For All in Task Manager and open it again."),
                        L"DLSS5 For All", MB_ICONWARNING | MB_OK | MB_TOPMOST | MB_SETFOREGROUND);
            return 1;
        }
    }
    Sleep(2500);   // o driver termina a recuperacao; nada de device novo no mesmo segundo do reset
    const std::wstring luid = IniLer(ini, L"reinicio_auto_luid");
    const std::wstring args = IniLer(ini, L"reinicio_auto_args");
    std::string det;
    if (SondarComRepeticao(luid, 3, &det)) {
        IniGravar(ini, L"reinicio_auto_ult", L"reaberto");
        if (AbrirApp(args)) return 0;
        IniGravar(ini, L"reinicio_auto_ult", L"lancador-falhou");
        MessageBoxW(nullptr, T(L"A placa voltou, mas o programa não conseguiu se reabrir.\nAbra o DLSS5 For All do jeito de sempre.",
                               L"The graphics card is back, but the program could not reopen itself.\nOpen DLSS5 For All the usual way."),
                    L"DLSS5 For All", MB_ICONWARNING | MB_OK | MB_TOPMOST | MB_SETFOREGROUND);
        return 1;
    }
    IniGravar(ini, L"reinicio_auto_ult", L"sonda-falhou");
    LLog("lancador: a placa ainda nao aceita um device novo (%s) - explicando e oferecendo o Reiniciar video do Windows", det.c_str());
    for (;;) {
        const int r = MessageBoxW(nullptr,
            T(L"A placa de vídeo reiniciou e ainda não aceita o programa.\n\nFeche jogos abertos e tente \"Reiniciar vídeo do Windows\" "
              L"(recupera o desempenho após a placa resetar; pode fechar um jogo aberto).\n\n"
              L"Sim = Reiniciar vídeo do Windows\nNão = tentar abrir de novo\nCancelar = fechar",
              L"The graphics card reset and does not accept the program yet.\n\nClose open games and try \"Restart Windows video\" "
              L"(recovers performance after the card resets; may close an open game).\n\n"
              L"Yes = Restart Windows video\nNo = try to open again\nCancel = close"),
            L"DLSS5 For All", MB_ICONWARNING | MB_YESNOCANCEL | MB_TOPMOST | MB_SETFOREGROUND);
        if (r == IDCANCEL) { LLog("lancador: a pessoa fechou - nada reaberto"); return 1; }
        if (r == IDYES) {
            if (!VideoComTravas(nullptr)) continue;
            Sleep(3000);
        }
        if (SondarComRepeticao(luid, r == IDYES ? 2 : 1, &det)) {
            IniGravar(ini, L"reinicio_auto_ult", L"reaberto");
            return AbrirApp(args) ? 0 : 1;
        }
        MessageBoxW(nullptr, (TS(L"A placa ainda não aceita (", L"The card still does not accept it (") + std::wstring(det.begin(), det.end()) +
                              TS(L").\nSe continuar assim, reinicie o PC — só isso (ou reiniciar o driver como administrador) garante.",
                                 L").\nIf it stays like this, restart the PC — only that (or restarting the driver as administrator) is guaranteed.")).c_str(),
                    L"DLSS5 For All", MB_ICONWARNING | MB_OK | MB_TOPMOST | MB_SETFOREGROUND);
    }
}

}  // namespace ReinicioAuto
