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
//      Signal trivial, num processo novo. Roda de uma COPIA do exe fora da pasta do app
//      (%TEMP%\DLSS5ForAll-sonda\dlss5-sonda.exe): na pasta do app o dxgi.dll e o ReShade,
//      e um device criado ali carregaria o ReShade e os add-ons (o feed montaria rede em cima
//      da sonda). Fora dela, o dxgi.dll e o do Windows.
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
#include "ponte/cliente/ngx/ngx_contrato.h"   // NS-2309 item 4: a sonda da rede usa o mesmo contrato NGX do app

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

// ---------------------------------------------------------------- 1. a sonda
// Codigos de saida: 0 ok | 10 sem placa | 11 D3D12CreateDevice falhou | 12 fila/fence falhou |
// 13 o Signal nao completou em 3 s | 14 a sonda nao voltou (o lancador matou) | 15 nao consegui
// lancar a sonda | 16 nao consegui copiar o exe da sonda.
static const wchar_t* g_sondaArq = nullptr;
static int SondaDizer(int cod, const char* fmt, ...) {
    if (g_sondaArq && *g_sondaArq) {
        if (FILE* f = _wfopen(g_sondaArq, L"w")) {
            va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
            fclose(f);
        }
    }
    return cod;
}
#define dizer SondaDizer
static int SondaMain(const wchar_t* luidTxt, const wchar_t* arqRes) {
    g_sondaArq = arqRes;
    LUID alvo{}; bool temAlvo = false;
    if (luidTxt && *luidTxt) {
        const wchar_t* dp = wcschr(luidTxt, L':');
        if (dp) { alvo.HighPart = (LONG)wcstoul(luidTxt, nullptr, 16); alvo.LowPart = (DWORD)wcstoul(dp + 1, nullptr, 16); temAlvo = true; }
    }
    IDXGIFactory1* fab = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fab)) || !fab) return dizer(10, "CreateDXGIFactory1 falhou");
    IDXGIAdapter1* ad = nullptr;
    wchar_t nome[128] = L"?";
    for (UINT i = 0; ; i++) {
        IDXGIAdapter1* a = nullptr;
        if (fab->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
        const bool sw = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        const bool casa = temAlvo ? (d.AdapterLuid.HighPart == alvo.HighPart && d.AdapterLuid.LowPart == alvo.LowPart) : !sw;
        if (casa && !ad) { ad = a; wcsncpy(nome, d.Description, 127); nome[127] = 0; } else a->Release();
    }
    fab->Release();
    if (!ad) return dizer(10, "nenhuma placa %s", temAlvo ? "com o LUID pedido" : "de hardware");
    ID3D12Device* dev = nullptr;
    const HRESULT hr = D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev);
    ad->Release();
    if (FAILED(hr) || !dev) return dizer(11, "D3D12CreateDevice falhou 0x%08X (%ls)", (unsigned)hr, nome);
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* q = nullptr; ID3D12Fence* fe = nullptr;
    int cod = 0; char det[200] = "ok";
    if (FAILED(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q)) || !q ||
        FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fe)) || !fe) {
        cod = 12; snprintf(det, sizeof det, "fila ou fence falhou (removido 0x%08X)", (unsigned)dev->GetDeviceRemovedReason());
    } else {
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        q->Signal(fe, 1);
        if (fe->GetCompletedValue() < 1 && ev) {
            fe->SetEventOnCompletion(1, ev);
            if (WaitForSingleObject(ev, 3000) != WAIT_OBJECT_0) { cod = 13; snprintf(det, sizeof det, "o Signal nao completou em 3 s (removido 0x%08X)", (unsigned)dev->GetDeviceRemovedReason()); }
        }
        if (ev) CloseHandle(ev);
    }
    if (fe) fe->Release();
    if (q) q->Release();
    dev->Release();
    return dizer(cod, "%s (%ls)", det, nome);
}
#undef dizer

// Roda a sonda num processo novo, da copia fora da pasta do app. Devolve o codigo; `det` recebe
// a linha que a sonda escreveu.
static int RodarSonda(const std::wstring& luid, std::string* det) {
    if (det) det->clear();
    const std::wstring exe = MeuExe();
    wchar_t tmp[MAX_PATH]{};
    if (exe.empty() || !GetTempPathW(MAX_PATH, tmp)) { if (det) *det = "sem caminho do exe/temp"; return 16; }
    const std::wstring dir = std::wstring(tmp) + L"DLSS5ForAll-sonda\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring copia = dir + L"dlss5-sonda.exe";
    const std::wstring res = dir + L"resultado.txt";
    DeleteFileW(res.c_str());
    if (!CopyFileW(exe.c_str(), copia.c_str(), FALSE) && GetFileAttributesW(copia.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (det) { char b[80]; snprintf(b, sizeof b, "nao consegui copiar o exe da sonda (erro %lu)", (unsigned long)GetLastError()); *det = b; }
        return 16;
    }
    std::wstring cmd = L"\"" + copia + L"\" --sonda-d3d12 " + (luid.empty() ? std::wstring(L"-") : luid) + L" \"" + res + L"\"";
    PROCESS_INFORMATION pi{};
    // MGR-2309 item 7: cwd = pasta da copia, sem o nosso diretorio de DLLs, PATH sem a pasta do app, sem DLSS5_*
    if (!CriarProcessoLimpo(copia, cmd, CREATE_NO_WINDOW, true, &pi, PastaDe(exe))) {
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

// ---------------------------------------------------------------- 1b. a SONDA DA REDE antes de abrir (NS-2309 item 4)
// `--sonda-ngx <pasta do app> <arquivo>`: o mesmo exe copiado para %TEMP% (sem o ReShade), num processo DESCARTAVEL:
// device D3D12 na placa NVIDIA -> Init do NGX do driver -> Init_Ext do nvngx_dlssnr.dll da pasta do app -> UMA feature
// da rede (Reserved18) a 640x360 -> UM evaluate com quadro preto e vetores zero -> tudo solto. Se o driver ou a rede
// estiverem quebrados (TDR, crash, device removido, create recusado), quem cai e ESTE processo, nao o app no meio do
// jogo. Ideia observada no NeuralScreen (pre-voo/gate de compatibilidade, fronteira MIT/PolyForm) - codigo proprio,
// escrito a partir do NOSSO feed (NrEnsure / NrAttachSnippet / NrInstallCallerSpoof), nada copiado de la.
// Codigos: 0 ok | 20 sem placa NVIDIA | 21 D3D12CreateDevice | 22 fila/lista/fence/texturas | 23 sem _nvngx.dll do driver
// | 24 Init do core | 25 sem nvngx_dlssnr.dll / export | 26 Init_Ext | 27 AllocateParameters | 28 CreateFeature |
// 29 Evaluate | 30 a GPU nao terminou (fence) / device removido | 14 nao voltou no prazo (o lancador matou) | 15/16 lancar/copiar.
namespace SondaNgx {
typedef Ngx::Result (*FnCoreInit)(unsigned long long, const wchar_t*, ID3D12Device*, const void*, unsigned);
typedef Ngx::Result (*FnAlloc)(Ngx::Parameter**);
typedef Ngx::Result (*FnDestroy)(Ngx::Parameter*);
typedef Ngx::Result (*FnShutdown)(ID3D12Device*);
typedef Ngx::Result (*FnInitExt)(unsigned long long, const wchar_t*, ID3D12Device*, unsigned, const Ngx::Parameter*);
typedef DWORD (WINAPI *FnGmfnw)(HMODULE, LPWSTR, DWORD);
static HMODULE g_core = nullptr;
static FnGmfnw g_gmfnwReal = nullptr;
// A rede so aceita quem a chama de dentro de um modulo cujo caminho contem "nvngx.dll" (ver NrInstallCallerSpoof no
// feed): so a pergunta "qual o nome do modulo X?" com X = este exe e respondida com o caminho do _nvngx.dll do driver.
static DWORD WINAPI GmfnwThunk(HMODULE h, LPWSTR b, DWORD n) {
    if (h == GetModuleHandleW(nullptr) && g_core) { const DWORD w = g_gmfnwReal(g_core, b, n); if (w && w < n) return w; }
    return g_gmfnwReal ? g_gmfnwReal(h, b, n) : GetModuleFileNameW(h, b, n);
}
static bool TrocarIat(HMODULE mod) {
    BYTE* base = (BYTE*)mod;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    for (const IMAGE_IMPORT_DESCRIPTOR* imp = (const IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (!imp->OriginalFirstThunk) continue;
        const IMAGE_THUNK_DATA* oft = (const IMAGE_THUNK_DATA*)(base + imp->OriginalFirstThunk);
        IMAGE_THUNK_DATA* ft = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; oft->u1.AddressOfData; ++oft, ++ft) {
            if (oft->u1.Ordinal & IMAGE_ORDINAL_FLAG64) continue;
            const IMAGE_IMPORT_BY_NAME* ibn = (const IMAGE_IMPORT_BY_NAME*)(base + oft->u1.AddressOfData);
            if (strcmp((const char*)ibn->Name, "GetModuleFileNameW") != 0) continue;
            void** slot = (void**)&ft->u1.Function;
            DWORD prot = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &prot)) return false;
            g_gmfnwReal = (FnGmfnw)*slot; *slot = (void*)&GmfnwThunk;
            VirtualProtect(slot, sizeof(void*), prot, &prot);
            return true;
        }
    }
    return false;
}
static Ngx::Result RazaoCb(Ngx::Parameter* p) { if (!p) return 0xBAD00005u; Ngx::PorF(p, "DLSSNR.ScalingRatio", 1.0f); return Ngx::kOk; }
static ID3D12Resource* Tex(ID3D12Device* dev, UINT w, UINT h, DXGI_FORMAT f, bool uav) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = h;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.Format = f; rd.SampleDesc.Count = 1;
    rd.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    ID3D12Resource* t = nullptr;
    // COMMON: o que o feed entrega a rede tambem nao vem com barreira nossa para ela (a rede faz as dela)
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&t))) return nullptr;
    return t;
}
} // namespace SondaNgx

#define dizer SondaDizer
static int SondaNgxMain(const wchar_t* pastaApp, const wchar_t* arqRes) {
    using namespace SondaNgx;
    g_sondaArq = arqRes;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);   // crash = codigo de saida, sem caixa do WER
    IDXGIFactory1* fab = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fab)) || !fab) return dizer(20, "CreateDXGIFactory1 falhou");
    IDXGIAdapter1* ad = nullptr; wchar_t nome[128] = L"?";
    for (UINT i = 0; ; i++) {
        IDXGIAdapter1* a = nullptr;
        if (fab->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
        if (!ad && d.VendorId == 0x10DE && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { ad = a; wcsncpy(nome, d.Description, 127); nome[127] = 0; }
        else a->Release();
    }
    fab->Release();
    if (!ad) return dizer(20, "nenhuma placa NVIDIA (a rede so roda nelas)");
    ID3D12Device* dev = nullptr;
    HRESULT hr = D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev);
    ad->Release();
    if (FAILED(hr) || !dev) return dizer(21, "D3D12CreateDevice falhou 0x%08X (%ls)", (unsigned)hr, nome);
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* q = nullptr; ID3D12CommandAllocator* al = nullptr; ID3D12GraphicsCommandList* li = nullptr; ID3D12Fence* fe = nullptr;
    const UINT W = 640, H = 360;
    ID3D12Resource *cor = nullptr, *sai = nullptr, *mv = nullptr, *prof = nullptr;
    if (FAILED(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q)) || !q ||
        FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al)) || !al ||
        FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&li)) || !li ||
        FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fe)) || !fe ||
        !(cor = Tex(dev, W, H, DXGI_FORMAT_B8G8R8A8_UNORM, false)) || !(sai = Tex(dev, W, H, DXGI_FORMAT_B8G8R8A8_UNORM, true)) ||
        !(mv = Tex(dev, W, H, DXGI_FORMAT_R16G16_FLOAT, false)) || !(prof = Tex(dev, W, H, DXGI_FORMAT_R32_FLOAT, false)))
        return dizer(22, "fila/lista/fence/texturas falharam (removido 0x%08X, %ls)", (unsigned)dev->GetDeviceRemovedReason(), nome);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT64 fv = 0;
    auto Enviar = [&](DWORD tetoMs) -> bool {   // fecha, executa e espera; so a fence decide
        li->Close(); ID3D12CommandList* ls[] = { li }; q->ExecuteCommandLists(1, ls);
        q->Signal(fe, ++fv);
        if (fe->GetCompletedValue() < fv && ev) { ResetEvent(ev); fe->SetEventOnCompletion(fv, ev); WaitForSingleObject(ev, tetoMs); }
        const bool ok = fe->GetCompletedValue() >= fv && SUCCEEDED(dev->GetDeviceRemovedReason());
        if (ok) { al->Reset(); li->Reset(al, nullptr); }
        return ok;
    };
    // o NGX do driver (_nvngx.dll pelo registro, como a lib do SDK e o ngx_forward do feed)
    for (const wchar_t* v : { L"FullPath", L"NGXPath" }) {
        wchar_t d[MAX_PATH]{}; DWORD sz = sizeof(d);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", v, RRF_RT_REG_SZ, nullptr, d, &sz) != ERROR_SUCCESS || !d[0]) continue;
        std::wstring p = d; if (p.back() != L'\\') p += L'\\'; p += L"_nvngx.dll";
        if ((g_core = LoadLibraryW(p.c_str())) != nullptr) break;
    }
    if (!g_core) return dizer(23, "o _nvngx.dll do driver nao carregou (%ls)", nome);
    const FnCoreInit coreInit = (FnCoreInit)(void*)GetProcAddress(g_core, "NVSDK_NGX_D3D12_Init");
    const FnAlloc alloc = (FnAlloc)(void*)GetProcAddress(g_core, "NVSDK_NGX_D3D12_AllocateParameters");
    const FnDestroy destroy = (FnDestroy)(void*)GetProcAddress(g_core, "NVSDK_NGX_D3D12_DestroyParameters");
    const FnShutdown coreShut = (FnShutdown)(void*)GetProcAddress(g_core, "NVSDK_NGX_D3D12_Shutdown1");
    if (!coreInit || !alloc || !destroy) return dizer(23, "o _nvngx.dll nao exporta Init/AllocateParameters (%ls)", nome);
    wchar_t tmp[MAX_PATH]{}; GetTempPathW(MAX_PATH, tmp);
    std::wstring dados = std::wstring(tmp) + L"DLSS5ForAll-sonda";
    Ngx::Result r = coreInit(0x1000000ULL, dados.c_str(), dev, nullptr, 0x15u);
    if (Ngx::Falhou(r)) return dizer(24, "Init do NGX do driver 0x%08X (%s) (%ls)", r, Ngx::NomeResultado(r), nome);
    // a rede da pasta do app
    std::wstring pasta = pastaApp ? pastaApp : L"";
    while (!pasta.empty() && (pasta.back() == L'\\' || pasta.back() == L'/')) pasta.pop_back();
    const std::wstring dll = pasta + L"\\nvngx_dlssnr.dll";
    HMODULE rede = LoadLibraryW(dll.c_str());
    if (!rede) return dizer(25, "nvngx_dlssnr.dll nao carregou da pasta do app (erro %lu)", (unsigned long)GetLastError());
    const FnInitExt initExt = (FnInitExt)(void*)GetProcAddress(rede, "NVSDK_NGX_D3D12_Init_Ext");
    const Ngx::FnD3D12CreateFeature criar = (Ngx::FnD3D12CreateFeature)(void*)GetProcAddress(rede, "NVSDK_NGX_D3D12_CreateFeature");
    const Ngx::FnD3D12EvaluateFeature avaliar = (Ngx::FnD3D12EvaluateFeature)(void*)GetProcAddress(rede, "NVSDK_NGX_D3D12_EvaluateFeature");
    const Ngx::FnD3D12ReleaseFeature soltar = (Ngx::FnD3D12ReleaseFeature)(void*)GetProcAddress(rede, "NVSDK_NGX_D3D12_ReleaseFeature");
    const FnShutdown redeShut = (FnShutdown)(void*)GetProcAddress(rede, "NVSDK_NGX_D3D12_Shutdown1");
    if (!initExt || !criar || !avaliar || !soltar) return dizer(25, "nvngx_dlssnr.dll sem um export do ciclo de vida");
    TrocarIat(rede);
    r = initExt(0x0876232CULL, pasta.c_str(), dev, 0x15u, nullptr);
    if (Ngx::Falhou(r)) return dizer(26, "Init_Ext da rede 0x%08X (%s) (%ls)", r, Ngx::NomeResultado(r), nome);
    Ngx::Parameter* p = nullptr;
    r = alloc(&p);
    if (Ngx::Falhou(r) || !p) return dizer(27, "AllocateParameters 0x%08X (%s)", r, Ngx::NomeResultado(r));
    Ngx::PorUI(p, "CreationNodeMask", 1u); Ngx::PorUI(p, "VisibilityNodeMask", 1u);
    Ngx::PorUI(p, Ngx::kWidth, W); Ngx::PorUI(p, Ngx::kHeight, H); Ngx::PorUI(p, Ngx::kOutWidth, W); Ngx::PorUI(p, Ngx::kOutHeight, H);
    Ngx::PorUI(p, "DLSSNR.Width", W); Ngx::PorUI(p, "DLSSNR.Height", H);
    Ngx::PorUI(p, "DLSSNR.Hint.Render.Preset", 1u);
    Ngx::PorI(p, Ngx::kPerfQuality, Ngx::PQ_DLAA);
    Ngx::PorVoid(p, "DLSSNRComputeScalingRatioCallback", (void*)&RazaoCb);
    Ngx::PorF(p, "DLSSNR.ScalingRatio", 1.0f);
    Ngx::Handle* feat = nullptr;
    r = criar(li, 18u /* Reserved18 */, p, &feat);
    if (Ngx::Falhou(r) || !feat) { li->Close(); return dizer(28, "CreateFeature da rede 0x%08X (%s) a %ux%u (%ls)", r, Ngx::NomeResultado(r), W, H, nome); }
    if (!Enviar(2000)) return dizer(30, "a GPU nao terminou a criacao (removido 0x%08X, %ls)", (unsigned)dev->GetDeviceRemovedReason(), nome);
    // um passe: quadro preto, vetores zero, profundidade zero (os mesmos nomes que o feed escreve)
    Ngx::PorI(p, "DLSSNR.Enabled", 1); Ngx::PorI(p, "DLSSNR.UICorrection", 0);
    Ngx::PorUI(p, "DLSSNR.Style", 0u); Ngx::PorF(p, "DLSSNR.Intensity", 1.0f);
    Ngx::PorF(p, "DLSSNR.LocalToneStrength", 1.0f); Ngx::PorF(p, "DLSSNR.LocalStructureStrength", 1.0f); Ngx::PorF(p, "DLSSNR.SkinStructureStrength", 1.0f);
    Ngx::PorI(p, "DLSSNR.UseAutoMask", 0);
    Ngx::PorF(p, "DLSSNR.MVecScaleX", 1.0f); Ngx::PorF(p, "DLSSNR.MVecScaleY", 1.0f); Ngx::PorI(p, "DLSSNR.DepthInverted", 0);
    Ngx::PorD3D12(p, "DLSSNR.MVec", mv); Ngx::PorD3D12(p, "DLSSNR.Depth", prof);
    Ngx::PorD3D12(p, "DLSSNR.Color", cor); Ngx::PorD3D12(p, "DLSSNR.Output", sai);
    Ngx::PorI(p, "DLSSNR.Reset", 1);
    r = avaliar(li, feat, p, nullptr);
    if (Ngx::Falhou(r)) { li->Close(); return dizer(29, "Evaluate da rede 0x%08X (%s) (%ls)", r, Ngx::NomeResultado(r), nome); }
    if (!Enviar(2000)) return dizer(30, "a GPU nao terminou o passe (removido 0x%08X, %ls)", (unsigned)dev->GetDeviceRemovedReason(), nome);
    // soltar pelo MESMO modulo que criou (a rede), como o feed faz (NrReleaseGuarded)
    soltar(feat); destroy(p);
    if (redeShut) redeShut(dev);
    if (coreShut) coreShut(dev);
    return dizer(0, "ok: device + NGX + 1 passe da rede a %ux%u (%ls)", W, H, nome);
    // O resto (texturas, fila, device) sai com o processo: ele e descartavel de proposito.
}
#undef dizer

// Roda a sonda da rede e ESPERA no maximo `tetoMs` (3 s). Mesma copia/pasta da sonda D3D12.
static int RodarSondaNgx(const std::wstring& pastaApp, DWORD tetoMs, std::string* det) {
    if (det) det->clear();
    const std::wstring exe = MeuExe();
    wchar_t tmp[MAX_PATH]{};
    if (exe.empty() || !GetTempPathW(MAX_PATH, tmp)) { if (det) *det = "sem caminho do exe/temp"; return 16; }
    const std::wstring dir = std::wstring(tmp) + L"DLSS5ForAll-sonda\\";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring copia = dir + L"dlss5-sonda.exe";
    const std::wstring res = dir + L"resultado-ngx.txt";
    DeleteFileW(res.c_str());
    if (!CopyFileW(exe.c_str(), copia.c_str(), FALSE) && GetFileAttributesW(copia.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (det) { char b[80]; snprintf(b, sizeof b, "nao consegui copiar o exe da sonda (erro %lu)", (unsigned long)GetLastError()); *det = b; }
        return 16;
    }
    std::wstring pa = pastaApp; while (!pa.empty() && pa.back() == L'\\') pa.pop_back();
    std::wstring cmd = L"\"" + copia + L"\" --sonda-ngx \"" + pa + L"\" \"" + res + L"\"";
    PROCESS_INFORMATION pi{};
    // MGR-2309 item 7: cwd = pasta da copia, sem o nosso diretorio de DLLs, PATH sem a pasta do app, sem DLSS5_*
    if (!CriarProcessoLimpo(copia, cmd, CREATE_NO_WINDOW, true, &pi, PastaDe(exe))) {
        if (det) { char b[80]; snprintf(b, sizeof b, "CreateProcess da sonda falhou (erro %lu)", (unsigned long)GetLastError()); *det = b; }
        return 15;
    }
    DWORD cod = 14;
    if (WaitForSingleObject(pi.hProcess, tetoMs) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &cod);
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
static std::wstring g_lIni;
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
