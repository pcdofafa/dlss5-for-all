// =====================================================================================
//  hdr_control.h  -  deteccao e controle do HDR do Windows, por monitor.
//
//  Por que isso existe:
//    A captura (Windows Graphics Capture) pede o frame pool em B8G8R8A8UIntNormalized,
//    que e SDR de 8 bits. Com o HDR do Windows LIGADO, o compositor nao entrega o buffer
//    HDR da janela: ele aplica o tone map SDR generico do sistema e entrega o resultado.
//    Isso acontece ANTES do ReShade, do LumeniteFX e do DLSS verem qualquer coisa - e o
//    resultado e a imagem lavada/chapada que o usuario ve. Nenhum slider do RenoDX
//    desfaz isso (ver docs\brilho-witcher3.md).
//
//  O que este modulo faz:
//    1. DETECTA, por monitor, se o HDR esta ligado. Duas fontes independentes:
//         - DisplayConfig (DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO / _2)
//         - DXGI (IDXGIOutput6::GetDesc1 -> ColorSpace, luminancias, primarias)
//    2. DESLIGA o HDR so do monitor relevante, se (e somente se) o usuario pediu.
//    3. RESTAURA sempre: destrutor (RAII), atexit, filtro de excecao nao tratada e -
//       para o caso feio de o processo morrer sem rodar nada disso - um registro em ini
//       que o proximo inicio le e desfaz.
//
//  API usada:
//    Win10 1803+ : DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO (9)
//                  DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE (10)
//    Win11 24H2+ : DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2 (15)
//                  DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE (16)
//    O caminho novo e preferido quando existe, porque no 24H2 o "advanced color" legado
//    passou a englobar HDR *e* WCG/ACM: desligar pelo caminho antigo derruba os dois.
//    O SET_HDR_STATE mexe so no HDR. A escolha e por tentativa (o GET_..._2 responde
//    ERROR_GEN_FAILURE / ERROR_INVALID_PARAMETER em Windows que nao o conhece), nao por
//    numero de build.
//
//  As structs dessas duas chamadas novas so aparecem no wingdi.h sob
//  NTDDI_VERSION >= NTDDI_WIN11_GA, e o projeto compila com _WIN32_WINNT 0x0A00 (Win10).
//  Entao sao declaradas aqui a mao, com os campos de bits achatados em um UINT32 - mesmo
//  layout, sem depender de como o compilador empacota bitfields.
//
//  Nao depende de nada do projeto. Linka com user32 (ja implicito) e dxgi (ja no build).
//  Licenca MIT, igual ao resto do app.
// =====================================================================================
#pragma once
#include "idioma.h"   // T(pt, en): este modulo tem texto de tela (rotulo, tooltip, avisos)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <dxgi1_6.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

namespace Hdr {

// ------------------------------------------------------------------ log opcional
// O app tem um Log(fmt, ...) proprio; aqui so guardamos um ponteiro pra ele.
typedef void (*LogFn)(const char*);
inline LogFn g_logSink = nullptr;

inline void Log(const char* fmt, ...) {
    if (!g_logSink) return;
    char b[600];
    va_list ap; va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    b[sizeof(b) - 1] = 0;
    g_logSink(b);
}

// ------------------------------------------------------------------ constantes da API
enum : UINT32 {
    DEVINFO_GET_SOURCE_NAME      = 1,
    DEVINFO_GET_TARGET_NAME      = 2,
    DEVINFO_GET_ADV_COLOR_INFO   = 9,    // Win10 1803+
    DEVINFO_SET_ADV_COLOR_STATE  = 10,   // Win10 1803+
    DEVINFO_GET_SDR_WHITE_LEVEL  = 11,
    DEVINFO_GET_ADV_COLOR_INFO_2 = 15,   // Win11 24H2+
    DEVINFO_SET_HDR_STATE        = 16,   // Win11 24H2+
};

// GET_ADVANCED_COLOR_INFO (9) - bits do campo value
enum : UINT32 {
    ACI1_SUPPORTED      = 1u << 0,
    ACI1_ENABLED        = 1u << 1,
    ACI1_WIDE_ENFORCED  = 1u << 2,
    ACI1_FORCE_DISABLED = 1u << 3,
};
// GET_ADVANCED_COLOR_INFO_2 (15) - bits do campo value
enum : UINT32 {
    ACI2_AC_SUPPORTED        = 1u << 0,
    ACI2_AC_ACTIVE           = 1u << 1,
    ACI2_LIMITED_BY_POLICY   = 1u << 3,
    ACI2_HDR_SUPPORTED       = 1u << 4,
    ACI2_HDR_USER_ENABLED    = 1u << 5,
    ACI2_WIDE_SUPPORTED      = 1u << 6,
    ACI2_WIDE_USER_ENABLED   = 1u << 7,
};
// activeColorMode do _2
enum : UINT32 { ACM_SDR = 0, ACM_WCG = 1, ACM_HDR = 2 };

#pragma pack(push, 1)
// Nao ha padding a esconder (tudo alinhado em 4/8), mas o pack deixa explicito que o
// layout e o do sistema e nao o que o compilador quiser inventar.
struct PktAdvColorInfo1 {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 value;
    UINT32 colorEncoding;
    UINT32 bitsPerColorChannel;
};
struct PktAdvColorInfo2 {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 value;
    UINT32 colorEncoding;
    UINT32 bitsPerColorChannel;
    UINT32 activeColorMode;
};
struct PktSetAdvColorState {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 value;                        // bit 0 = enableAdvancedColor
};
struct PktSetHdrState {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 value;                        // bit 0 = enableHdr
};
struct PktSdrWhiteLevel {
    DISPLAYCONFIG_DEVICE_INFO_HEADER header;
    UINT32 SDRWhiteLevel;                // 1000 = 80 nits
};
#pragma pack(pop)

// ------------------------------------------------------------------ alvo de display
// Um "target" e uma saida fisica (o monitor no fim do cabo). O par (adapterId, targetId)
// e o endereco que a API DisplayConfig usa - mas o adapterId e um LUID que muda a cada
// boot, entao para persistir em ini guardamos o devicePath, que e estavel.
struct Target {
    LUID         adapter{};
    UINT32       targetId = 0;
    UINT32       sourceId = 0;
    std::wstring gdiName;      // "\\.\DISPLAY1" - casa com MONITORINFOEXW.szDevice
    std::wstring devicePath;   // "\\?\DISPLAY#GSM5B09#5&...#{e6f07b5f-...}" - estavel
    std::wstring friendly;     // nome amigavel do EDID, quando ha
};

inline bool SameLuid(const LUID& a, const LUID& b) {
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

// Todos os targets ativos, na ordem em que o sistema os lista.
inline std::vector<Target> EnumTargets() {
    std::vector<Target> out;
    UINT32 nPath = 0, nMode = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPath, &nMode) != ERROR_SUCCESS) return out;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPath);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(nMode);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPath, paths.data(), &nMode, modes.data(), nullptr) != ERROR_SUCCESS)
        return out;
    paths.resize(nPath);

    for (auto& p : paths) {
        Target t;
        t.adapter  = p.targetInfo.adapterId;
        t.targetId = p.targetInfo.id;
        t.sourceId = p.sourceInfo.id;

        DISPLAYCONFIG_SOURCE_DEVICE_NAME sn{};
        sn.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)DEVINFO_GET_SOURCE_NAME;
        sn.header.size = sizeof(sn);
        sn.header.adapterId = p.sourceInfo.adapterId;
        sn.header.id = p.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&sn.header) == ERROR_SUCCESS) t.gdiName = sn.viewGdiDeviceName;

        DISPLAYCONFIG_TARGET_DEVICE_NAME tn{};
        tn.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)DEVINFO_GET_TARGET_NAME;
        tn.header.size = sizeof(tn);
        tn.header.adapterId = p.targetInfo.adapterId;
        tn.header.id = p.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&tn.header) == ERROR_SUCCESS) {
            t.devicePath = tn.monitorDevicePath;
            t.friendly   = tn.monitorFriendlyDeviceName;
        }
        if (t.friendly.empty()) t.friendly = t.gdiName;
        out.push_back(t);
    }
    return out;
}

// Os targets ligados a um HMONITOR. Normalmente 1; em clone, mais de um dividem o mesmo
// source - por isso devolve vetor: desligar o HDR "do monitor" tem de valer para todos.
inline std::vector<Target> TargetsForMonitor(HMONITOR mon) {
    std::vector<Target> out;
    if (!mon) return out;
    MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return out;
    for (auto& t : EnumTargets())
        if (_wcsicmp(t.gdiName.c_str(), mi.szDevice) == 0) out.push_back(t);
    return out;
}

inline HMONITOR MonitorOfWindow(HWND h) {
    return h ? MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST) : nullptr;
}

// ------------------------------------------------------------------ leitura de estado
struct Info {
    bool  ok            = false;   // a consulta respondeu
    bool  newApi        = false;   // veio do GET_ADVANCED_COLOR_INFO_2 (Win11 24H2+)
    bool  supported     = false;   // o monitor/caminho aceita HDR
    bool  enabled       = false;   // HDR ligado AGORA
    bool  wideGamut     = false;   // WCG/ACM ativo (so o caminho novo distingue)
    bool  limitedByPolicy = false; // o sistema esta segurando (driver, MPO, politica)
    UINT32 bitsPerChannel = 0;
    UINT32 colorEncoding  = 0;
    UINT32 activeMode     = ACM_SDR;
    float  sdrWhiteNits   = 0.f;   // paper white do SDR dentro do HDR (padrao 80 nits)
};

inline Info Query(const Target& t) {
    Info in;

    PktAdvColorInfo2 g2{};
    g2.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)DEVINFO_GET_ADV_COLOR_INFO_2;
    g2.header.size = sizeof(g2);
    g2.header.adapterId = t.adapter;
    g2.header.id = t.targetId;
    if (DisplayConfigGetDeviceInfo(&g2.header) == ERROR_SUCCESS) {
        in.ok = in.newApi   = true;
        in.supported        = (g2.value & ACI2_HDR_SUPPORTED) != 0;
        in.enabled          = (g2.activeColorMode == ACM_HDR);
        in.wideGamut        = (g2.activeColorMode == ACM_WCG);
        in.limitedByPolicy  = (g2.value & ACI2_LIMITED_BY_POLICY) != 0;
        in.bitsPerChannel   = g2.bitsPerColorChannel;
        in.colorEncoding    = g2.colorEncoding;
        in.activeMode       = g2.activeColorMode;
    } else {
        PktAdvColorInfo1 g1{};
        g1.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)DEVINFO_GET_ADV_COLOR_INFO;
        g1.header.size = sizeof(g1);
        g1.header.adapterId = t.adapter;
        g1.header.id = t.targetId;
        if (DisplayConfigGetDeviceInfo(&g1.header) != ERROR_SUCCESS) return in;
        in.ok             = true;
        in.supported      = (g1.value & ACI1_SUPPORTED) != 0 && !(g1.value & ACI1_FORCE_DISABLED);
        // No caminho legado "advanced color enabled" + "wide color enforced" quer dizer
        // WCG sem HDR; HDR de verdade e enabled sem enforced.
        in.wideGamut      = (g1.value & ACI1_WIDE_ENFORCED) != 0;
        in.enabled        = (g1.value & ACI1_ENABLED) != 0 && !in.wideGamut;
        in.bitsPerChannel = g1.bitsPerColorChannel;
        in.colorEncoding  = g1.colorEncoding;
        in.activeMode     = in.enabled ? ACM_HDR : (in.wideGamut ? ACM_WCG : ACM_SDR);
    }

    PktSdrWhiteLevel wl{};
    wl.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)DEVINFO_GET_SDR_WHITE_LEVEL;
    wl.header.size = sizeof(wl);
    wl.header.adapterId = t.adapter;
    wl.header.id = t.targetId;
    if (DisplayConfigGetDeviceInfo(&wl.header) == ERROR_SUCCESS)
        in.sdrWhiteNits = (float)wl.SDRWhiteLevel / 1000.0f * 80.0f;

    return in;
}

// ------------------------------------------------------------------ leitura via DXGI
// Confirma o estado por um caminho totalmente independente e traz o que a DisplayConfig
// nao da: luminancias e primarias reais do painel. E o que o pipeline de render enxerga.
struct DxgiInfo {
    bool  ok   = false;
    bool  hdr  = false;                 // ColorSpace == G2084/P2020
    DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    UINT  bitsPerColor = 0;
    float minLum = 0.f, maxLum = 0.f, maxFullFrameLum = 0.f;
    float rx = 0, ry = 0, gx = 0, gy = 0, bx = 0, by = 0, wx = 0, wy = 0;
    std::wstring deviceName;
    RECT  desktop{};
};

inline bool IsHdrColorSpace(DXGI_COLOR_SPACE_TYPE cs) {
    // O desktop em HDR sempre reporta PQ / BT.2020. As variantes scRGB aparecem em
    // swapchain, nao em saida de display, mas custam nada de aceitar.
    return cs == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
           cs == DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020 ||
           cs == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
}

inline DxgiInfo QueryDxgi(HMONITOR mon) {
    DxgiInfo out;
    if (!mon) return out;
    IDXGIFactory1* fac = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&fac)) || !fac) return out;
    for (UINT a = 0; !out.ok; a++) {
        IDXGIAdapter1* ad = nullptr;
        // Qualquer FALHA encerra o laco, nao so NOT_FOUND. Com DXGI_ERROR_INVALID_CALL ou
        // DEVICE_REMOVED (driver caindo, sessao remota) o antigo devolvia erro != NOT_FOUND,
        // 'ad' ficava nulo e o for incrementava para sempre: laco infinito na abertura, com
        // o app parado antes de qualquer janela aparecer.
        if (FAILED(fac->EnumAdapters1(a, &ad)) || !ad) break;
        for (UINT o = 0; ad && !out.ok; o++) {
            IDXGIOutput* op = nullptr;
            if (FAILED(ad->EnumOutputs(o, &op)) || !op) break;
            IDXGIOutput6* o6 = nullptr;
            if (op && SUCCEEDED(op->QueryInterface(__uuidof(IDXGIOutput6), (void**)&o6)) && o6) {
                DXGI_OUTPUT_DESC1 d{};
                if (SUCCEEDED(o6->GetDesc1(&d)) && d.Monitor == mon) {
                    out.ok = true;
                    out.colorSpace   = d.ColorSpace;
                    out.hdr          = IsHdrColorSpace(d.ColorSpace);
                    out.bitsPerColor = d.BitsPerColor;
                    out.minLum = d.MinLuminance; out.maxLum = d.MaxLuminance;
                    out.maxFullFrameLum = d.MaxFullFrameLuminance;
                    out.rx = d.RedPrimary[0];   out.ry = d.RedPrimary[1];
                    out.gx = d.GreenPrimary[0]; out.gy = d.GreenPrimary[1];
                    out.bx = d.BluePrimary[0];  out.by = d.BluePrimary[1];
                    out.wx = d.WhitePoint[0];   out.wy = d.WhitePoint[1];
                    out.deviceName = d.DeviceName;
                    out.desktop = d.DesktopCoordinates;
                }
                o6->Release();
            }
            if (op) op->Release();
        }
        if (ad) ad->Release();
    }
    fac->Release();
    return out;
}

// ------------------------------------------------------------------ escrita de estado
// Devolve true se o estado FINAL e o pedido (inclusive quando ja estava assim).
// msOut recebe o tempo da chamada de sistema, em milissegundos.
inline bool SetHdrEnabled(const Target& t, bool on, double* msOut = nullptr, LONG* errOut = nullptr) {
    Info before = Query(t);
    if (msOut) *msOut = 0.0;
    if (errOut) *errOut = ERROR_SUCCESS;
    if (!before.ok) return false;
    if (before.enabled == on) return true;
    if (on && !before.supported) return false;

    LARGE_INTEGER f{}, a{}, b{};
    QueryPerformanceFrequency(&f);
    LONG r = ERROR_GEN_FAILURE;

    QueryPerformanceCounter(&a);
    if (before.newApi) {
        PktSetHdrState s{};
        s.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)DEVINFO_SET_HDR_STATE;
        s.header.size = sizeof(s);
        s.header.adapterId = t.adapter;
        s.header.id = t.targetId;
        s.value = on ? 1u : 0u;
        r = DisplayConfigSetDeviceInfo(&s.header);
    }
    if (r != ERROR_SUCCESS) {
        // Caminho legado. Tambem serve de rede se o SET_HDR_STATE existir mas recusar.
        PktSetAdvColorState s{};
        s.header.type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)DEVINFO_SET_ADV_COLOR_STATE;
        s.header.size = sizeof(s);
        s.header.adapterId = t.adapter;
        s.header.id = t.targetId;
        s.value = on ? 1u : 0u;
        LONG r2 = DisplayConfigSetDeviceInfo(&s.header);
        if (r == ERROR_SUCCESS || r2 == ERROR_SUCCESS) r = ERROR_SUCCESS; else r = r2;
    }
    QueryPerformanceCounter(&b);
    if (msOut && f.QuadPart) *msOut = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
    if (errOut) *errOut = r;

    Info after = Query(t);
    return after.ok && after.enabled == on;
}

// ------------------------------------------------------------------ a caixinha
// Uma opcao so, do usuario: "Tratar HDR automaticamente". Padrao DESMARCADA - detectar
// e de graca e nao mexe em nada, mas agir sem pedir seria invasivo.
// A acao certa depende da fonte, e quem resolve isso e o Treatment la embaixo.
inline bool TreatmentEnabledFromIni(const std::wstring& iniPath) {
    return GetPrivateProfileIntW(L"hdr", L"tratar", 0, iniPath.c_str()) != 0;
}
inline void SetTreatmentEnabledInIni(const std::wstring& iniPath, bool on) {
    WritePrivateProfileStringW(L"hdr", L"tratar", on ? L"1" : L"0", iniPath.c_str());
}

// ------------------------------------------------------------------ Manager
// Um por processo. Guarda o que desligou, restaura no destrutor e - se o processo morrer
// antes disso - deixa no ini a pista para o proximo inicio desfazer.
//
// Chave do ini:  [hdr] restaurar = <devicePath>|<devicePath>|...
// Escrita ANTES de desligar, apagada DEPOIS de restaurar. Se ela sobrou, o app morreu
// no meio. O devicePath e usado (e nao o LUID) porque LUID nao sobrevive a um reboot.
class Manager {
public:
    // O destrutor tambem retira o objeto das redes de seguranca: um atexit disparando
    // sobre memoria ja liberada seria pior que nao restaurar.
    ~Manager() { Restore(); if (Self() == this) Self() = nullptr; }

    void Init(const std::wstring& iniPath) { ini_ = iniPath; }

    // Restaura o que uma execucao anterior deixou desligado. Chamar cedo, antes de
    // qualquer coisa que dependa do estado do display. Devolve quantos religou.
    int RecoverStale() {
        std::wstring list = IniGet(L"restaurar");
        if (list.empty()) return 0;
        Log("HDR: a sessao anterior nao restaurou (%s) - religando",
            Utf8(list).c_str());
        int done = 0;
        auto all = EnumTargets();
        size_t pos = 0;
        while (pos <= list.size()) {
            size_t sep = list.find(L'|', pos);
            std::wstring path = list.substr(pos, sep == std::wstring::npos ? std::wstring::npos : sep - pos);
            pos = (sep == std::wstring::npos) ? list.size() + 1 : sep + 1;
            if (path.empty()) continue;
            for (auto& t : all) {
                if (_wcsicmp(t.devicePath.c_str(), path.c_str()) != 0) continue;
                double ms = 0; LONG err = 0;
                bool jaEstava = Query(t).enabled;
                if (SetHdrEnabled(t, true, &ms, &err)) {
                    if (jaEstava) Log("HDR: '%s' ja estava ligado - nada a fazer", Utf8(t.friendly).c_str());
                    else { Log("HDR: religado em '%s' (%.0f ms)", Utf8(t.friendly).c_str(), ms); done++; }
                } else {
                    Log("HDR: NAO consegui religar em '%s' (erro %ld) - use Win+Alt+B",
                        Utf8(t.friendly).c_str(), err);
                }
                break;
            }
        }
        IniSet(L"restaurar", L"");
        return done;
    }

    // Estado do monitor onde esta a janela/monitor alvo. mon tem prioridade sobre win.
    // Preenche 'info' (DisplayConfig) e 'dxgi' (DXGI) do primeiro target do monitor.
    bool DetectFor(HWND win, HMONITOR mon, Info* info, DxgiInfo* dxgi, std::wstring* nameOut) {
        HMONITOR m = mon ? mon : MonitorOfWindow(win);
        if (!m) return false;
        lastMon_ = m;
        auto ts = TargetsForMonitor(m);
        if (ts.empty()) return false;
        Info i = Query(ts[0]);
        DxgiInfo d = QueryDxgi(m);
        if (info) *info = i;
        if (dxgi) *dxgi = d;
        if (nameOut) *nameOut = ts[0].friendly;
        return i.ok || d.ok;
    }

    // Atalho: "o monitor onde esta a janela alvo esta em HDR?"
    // Concorda com as duas fontes; se so uma responder, vale a que respondeu.
    static bool IsWindowOnHdrMonitor(HWND win, HMONITOR mon = nullptr) {
        HMONITOR m = mon ? mon : MonitorOfWindow(win);
        if (!m) return false;
        DxgiInfo d = QueryDxgi(m);
        if (d.ok) return d.hdr;
        auto ts = TargetsForMonitor(m);
        if (ts.empty()) return false;
        Info i = Query(ts[0]);
        return i.ok && i.enabled;
    }

    // Por que DisableFor nao fez nada. "Nao precisou" e "falhou" sao coisas diferentes,
    // e confundir as duas foi exatamente o bug do relato de campo.
    enum Outcome { OUT_NONE = 0, OUT_DID = 1, OUT_NOT_NEEDED = 2, OUT_FAILED = 3 };
    Outcome lastOutcome = OUT_NONE;

    // Desliga o HDR do monitor do alvo (todos os targets dele, por causa de clone).
    // Nao faz nada se ja estiver desligado. Devolve true se algo foi desligado.
    bool DisableFor(HWND win, HMONITOR mon) {
        lastOutcome = OUT_NONE;
        // O HMONITOR pode ter envelhecido: um modeset (inclusive o NOSSO, do ciclo
        // anterior) invalida handles. Se o cacheado nao responde mais, re-resolve pela
        // janela, que e sempre atual.
        HMONITOR m = mon ? mon : MonitorOfWindow(win);
        {
            MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
            if (m && !GetMonitorInfoW(m, &mi)) {
                Log("HDR: o HMONITOR cacheado nao responde mais (modeset?) - re-resolvendo");
                m = MonitorOfWindow(win);
            }
        }
        if (!m) { Log("HDR: nao identifiquei o monitor do alvo"); lastOutcome = OUT_FAILED; return false; }
        auto ts = TargetsForMonitor(m);
        if (ts.empty()) {
            Log("HDR: monitor do alvo fora da lista do DisplayConfig");
            lastOutcome = OUT_FAILED; return false;
        }

        // O DXGI e o DisplayConfig podem discordar por um instante logo depois de um
        // modeset. A decisao de agir vem do DXGI (e o que o pipeline ve); a acao vai
        // pelo DisplayConfig. Se qualquer um dos dois disser "ligado", tentamos -
        // desligar o que ja esta desligado e um no-op barato, perder a janela nao e.
        const bool dxgiDizLigado = QueryDxgi(m).hdr;
        std::vector<Target> toOff;
        for (auto& t : ts) {
            Info i = Query(t);
            if (!i.ok) { Log("HDR: nao consegui ler o estado de '%s'", Utf8(t.friendly).c_str()); continue; }
            if (i.enabled || (dxgiDizLigado && i.supported)) toOff.push_back(t);
        }
        if (toOff.empty()) {
            Log("HDR: nada a desligar em '%s' (ja esta em SDR)", Utf8(ts[0].friendly).c_str());
            lastOutcome = OUT_NOT_NEEDED;
            return false;
        }

        // ini PRIMEIRO: se o processo morrer no meio da troca, a pista ja esta gravada.
        std::wstring list;
        for (auto& t : toOff) { if (!list.empty()) list += L'|'; list += t.devicePath; }
        std::wstring prev = IniGet(L"restaurar");
        if (!prev.empty()) list = prev + L"|" + list;
        IniSet(L"restaurar", list);

        InstallSafetyNets();

        bool any = false;
        for (auto& t : toOff) {
            double ms = 0; LONG err = 0;
            if (SetHdrEnabled(t, false, &ms, &err)) {
                Log("HDR: desligado em '%s' em %.0f ms (a tela pisca uma vez)",
                    Utf8(t.friendly).c_str(), ms);
                off_.push_back(t);
                any = true;
            } else {
                Log("HDR: falhei ao desligar em '%s' (erro %ld) - siga sem mexer",
                    Utf8(t.friendly).c_str(), err);
            }
        }
        if (!any) IniSet(L"restaurar", prev);   // nao mexi em nada: nao deixa lixo
        lastOutcome = any ? OUT_DID : OUT_FAILED;
        return any;
    }

    // Religa tudo o que este processo desligou. Idempotente; seguro de chamar N vezes.
    void Restore() {
        if (off_.empty()) return;
        std::vector<Target> pend;
        pend.swap(off_);
        std::wstring falharam;
        for (auto& t : pend) {
            double ms = 0; LONG err = 0;
            // O LUID pode ter mudado (troca de modo, hotplug): revalida pelo devicePath.
            Target cur = t;
            for (auto& n : EnumTargets())
                if (_wcsicmp(n.devicePath.c_str(), t.devicePath.c_str()) == 0) { cur = n; break; }
            if (SetHdrEnabled(cur, true, &ms, &err)) {
                Log("HDR: restaurado em '%s' (%.0f ms)", Utf8(cur.friendly).c_str(), ms);
            } else {
                Log("HDR: NAO consegui restaurar em '%s' (erro %ld) - Win+Alt+B religa",
                    Utf8(cur.friendly).c_str(), err);
                if (!falharam.empty()) falharam += L'|';
                falharam += t.devicePath;   // fica no ini: o proximo inicio tenta de novo
            }
        }
        IniSet(L"restaurar", falharam);
        lastOutcome = OUT_NONE;
    }

    bool Changed() const { return !off_.empty(); }

    // Para caminhos de saida que NAO rodam destrutor nem atexit - no app, o Fatal(),
    // que termina em ExitProcess(). Chamar de qualquer lugar, a qualquer hora.
    static void RestoreActive() { if (Self()) Self()->Restore(); }

private:
    std::wstring ini_;
    std::vector<Target> off_;
    HMONITOR lastMon_ = nullptr;

    std::wstring IniGet(const wchar_t* k) const {
        if (ini_.empty()) return L"";
        wchar_t b[2048];
        GetPrivateProfileStringW(L"hdr", k, L"", b, 2048, ini_.c_str());
        return b;
    }
    void IniSet(const wchar_t* k, const std::wstring& v) const {
        if (ini_.empty()) return;
        WritePrivateProfileStringW(L"hdr", k, v.empty() ? nullptr : v.c_str(), ini_.c_str());
    }

    static std::string Utf8(const std::wstring& w) {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s((size_t)n, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
    }

    // ---- redes de seguranca alem do destrutor
    // atexit pega ExitProcess()/return do main; o filtro de excecao pega crash.
    // Nenhum dos dois pega TerminateProcess/queda de energia - para esses existe o ini.
    static Manager*& Self() { static Manager* p = nullptr; return p; }
    static void AtExitHook() { if (Self()) Self()->Restore(); }
    static LONG WINAPI CrashHook(EXCEPTION_POINTERS* ep) {
        if (Self()) Self()->Restore();
        return prevFilter_ ? prevFilter_(ep) : EXCEPTION_CONTINUE_SEARCH;
    }
    static inline LPTOP_LEVEL_EXCEPTION_FILTER prevFilter_ = nullptr;

    void InstallSafetyNets() {
        // Self() aponta sempre para quem desligou algo por ultimo - com um `static bool`
        // aqui, um segundo Manager no mesmo processo nunca se registrava e ficava sem
        // rede de seguranca. Os ganchos globais e que sao instalados uma vez so.
        Self() = this;
        static bool hooks = false;
        if (hooks) return;
        hooks = true;
        atexit(&AtExitHook);
        prevFilter_ = SetUnhandledExceptionFilter(&CrashHook);
    }
};

// =====================================================================================
//  FONTE DE VIDEO (placa de captura / console / camera) - Media Foundation
//
//  Aqui o HDR esta do outro lado do cabo HDMI e nao ha o que desligar: o console manda
//  HDR10 (PQ, BT.2020) e a placa entrega isso como se fosse video comum. Se a cadeia
//  tratar os codigos PQ como se fossem gamma SDR, o resultado e exatamente o sintoma:
//  o preto de 0.1 nit vira 0.062 e o branco de referencia de 203 nits vira 0.581 -
//  a imagem inteira espremida entre 6% e 58%. Cinza, chapada, sem preto e sem branco.
//  O tratamento e nosso: converter PQ/BT.2020 -> SDR BT.709 antes do ReShade ver.
// =====================================================================================

// Valores do enum MFVideoTransferFunction / MFVideoPrimaries / MFVideoTransferMatrix,
// repetidos aqui para o header nao exigir que o Media Foundation tenha sido incluido.
enum : UINT32 {
    VTF_UNKNOWN = 0, VTF_G22 = 4, VTF_709 = 5, VTF_SRGB = 7,
    VTF_2020 = 13, VTF_PQ = 15, VTF_HLG = 16,
};
enum : UINT32 { VPR_UNKNOWN = 0, VPR_709 = 2, VPR_BT2020 = 9 };
enum : UINT32 { VMX_UNKNOWN = 0, VMX_709 = 1, VMX_601 = 2, VMX_2020_10 = 4, VMX_2020_12 = 5 };
enum : UINT32 { VRG_UNKNOWN = 0, VRG_FULL = 1, VRG_LIMITED = 2 };

struct VideoColor {
    bool   declared = false;   // o media type trouxe algum atributo de cor
    bool   hasTf = false, hasPrim = false, hasMtx = false, hasRange = false;
    UINT32 tf = 0, primaries = 0, matrix = 0, range = 0;
    bool   hdrDeclared = false;  // PQ, HLG ou BT.2020 declarados explicitamente
    bool   trustworthy = false;  // vale acreditar? ver o comentario abaixo
};

// Ninguem marca PQ/HLG/BT.2020 por acidente: quando aparece, e verdade.
// O contrario NAO vale. `BT.709 + matriz 709 + 16-235` e o preenchimento padrao que
// praticamente toda placa de captura USB despeja em TODOS os modos, tenha ou nao ideia
// do que esta chegando pelo HDMI. Por isso "nao declarou HDR" nunca prova SDR.
inline VideoColor VideoColorFromAttrs(bool hasTf, UINT32 tf, bool hasPrim, UINT32 prim,
                                      bool hasMtx, UINT32 mtx, bool hasRange, UINT32 range) {
    VideoColor v;
    v.hasTf = hasTf; v.hasPrim = hasPrim; v.hasMtx = hasMtx; v.hasRange = hasRange;
    v.tf = tf; v.primaries = prim; v.matrix = mtx; v.range = range;
    v.declared = hasTf || hasPrim || hasMtx || hasRange;
    const bool pq  = hasTf && (tf == VTF_PQ || tf == VTF_HLG);
    const bool wcg = (hasPrim && prim == VPR_BT2020) ||
                     (hasMtx && (mtx == VMX_2020_10 || mtx == VMX_2020_12));
    v.hdrDeclared = pq || wcg;
    v.trustworthy = pq;          // so a funcao de transferencia decide sozinha
    return v;
}

#ifdef __IMFMediaType_INTERFACE_DEFINED__
// Atalho para quem ja incluiu o Media Foundation. Atencao ao nome do atributo: e
// MF_MT_TRANSFER_FUNCTION, nao MF_MT_VIDEO_TRANSFER_FUNCTION (esse nao existe).
inline VideoColor VideoColorFromMediaType(IMFMediaType* t) {
    if (!t) return VideoColor{};
    UINT32 tf = 0, pr = 0, mx = 0, rg = 0;
    const bool a = SUCCEEDED(t->GetUINT32(MF_MT_TRANSFER_FUNCTION,   &tf));
    const bool b = SUCCEEDED(t->GetUINT32(MF_MT_VIDEO_PRIMARIES,     &pr));
    const bool c = SUCCEEDED(t->GetUINT32(MF_MT_YUV_MATRIX,          &mx));
    const bool d = SUCCEEDED(t->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, &rg));
    return VideoColorFromAttrs(a, tf, b, pr, c, mx, d, rg);
}
#endif

// ------------------------------------------------------------------ heuristica de conteudo
// Quando os metadados mentem (o caso comum), sobra olhar o pixel. Um sinal PQ lido como
// SDR tem uma assinatura muito especifica, calculada da propria curva ST 2084:
//     0.1 nit -> codigo 0.062      100 nits -> 0.508
//     203 nits (branco de referencia HDR) -> 0.581      1000 nits -> 0.752
// Ou seja: o histograma nao alcanca o branco e nao alcanca o preto. O discriminante
// forte e o percentil alto - qualquer jogo tem HUD, menu ou ceu que encostam em 1.0
// quando o sinal e SDR de verdade.
struct LumaStats {
    bool   ok = false;
    double p01 = 0, p50 = 0, p99 = 0, mean = 0;
    UINT32 samples = 0;
};

// Amostra um BGRA top-down (o formato que o Capture do app entrega). `step` e o passo
// em pixels; 7 da ~40k amostras em 1080p e custa dezenas de microssegundos.
inline LumaStats MeasureBgra(const BYTE* p, UINT32 w, UINT32 h, LONG stride, UINT32 step = 7) {
    LumaStats s;
    if (!p || !w || !h) return s;
    if (!step) step = 1;
    const LONG pitch = stride ? (stride < 0 ? -stride : stride) : (LONG)w * 4;
    UINT32 hist[256] = { 0 };
    UINT64 sum = 0; UINT32 n = 0;
    for (UINT32 y = 0; y < h; y += step) {
        const BYTE* row = p + (size_t)y * (size_t)pitch;
        for (UINT32 x = 0; x < w; x += step) {
            const BYTE* q = row + (size_t)x * 4;
            // BGRA; luma BT.709 sobre os codigos, sem linearizar (e uma heuristica)
            const UINT32 l = (54u * q[2] + 183u * q[1] + 18u * q[0]) >> 8;
            hist[l < 256 ? l : 255]++;
            sum += l; n++;
        }
    }
    if (!n) return s;
    s.ok = true; s.samples = n; s.mean = (double)sum / n / 255.0;
    const UINT32 t01 = (UINT32)(n * 0.01), t50 = (UINT32)(n * 0.50), t99 = (UINT32)(n * 0.99);
    UINT32 acc = 0; int g01 = -1, g50 = -1, g99 = -1;
    for (int i = 0; i < 256; i++) {
        acc += hist[i];
        if (g01 < 0 && acc >= t01) g01 = i;
        if (g50 < 0 && acc >= t50) g50 = i;
        if (g99 < 0 && acc >= t99) { g99 = i; break; }
    }
    s.p01 = (g01 < 0 ? 0 : g01) / 255.0;
    s.p50 = (g50 < 0 ? 0 : g50) / 255.0;
    s.p99 = (g99 < 0 ? 255 : g99) / 255.0;
    return s;
}

struct PqGuess {
    bool         suspect = false;
    double       confidence = 0.0;    // 0..1, honestamente aproximada
    const wchar_t* why = L"";
};

// Le uma medicao e opina. Nao decide nada sozinha - serve para ACENDER O AVISO, e o
// usuario confirma marcando a caixa e vendo a imagem mudar.
inline PqGuess GuessPq(const LumaStats& s) {
    PqGuess g;
    if (!s.ok) { g.why = L"sem medicao"; return g; }
    // 1000 nits -> 0.752; se o topo do histograma passa disso, quase certamente e SDR
    if (s.p99 > 0.80) { g.why = L"o branco chega ao topo: parece SDR normal"; return g; }
    // faixa 16-235 nao expandida da p99 ~0.92 e preto ~0.063 - o p99 ja excluiu isso
    if (s.p99 < 0.35) { g.why = L"cena escura demais para decidir"; return g; }
    g.suspect = true;
    // quanto mais o topo se aproxima de 0.58 (203 nits) e o preto de 0.062, mais suspeito
    const double topo  = (0.80 - s.p99) / (0.80 - 0.55);          // 0 em 0.80, 1 em 0.55
    const double preto = (s.p01 - 0.005) / (0.06 - 0.005);        // 0 em ~0, 1 em 0.06
    double c = 0.65 * (topo < 0 ? 0 : topo > 1 ? 1 : topo)
             + 0.35 * (preto < 0 ? 0 : preto > 1 ? 1 : preto);
    g.confidence = c < 0 ? 0 : c > 1 ? 1 : c;
    g.why = L"o branco nao chega ao topo e o preto nao chega ao fundo: assinatura de PQ lido como SDR";
    return g;
}

// ------------------------------------------------------------------ parametros do shader
// VM_SCRGB e o caminho da captura de tela em HDR: quando o monitor do alvo esta em HDR,
// o frame pool do WGC e criado em R16G16B16A16Float e o Windows entrega scRGB LINEAR -
// primarias BT.709, 1.0 = 80 nits, sem tone map do compositor. Nao ha o que "detectar"
// nem o que confirmar: nos escolhemos o formato, entao sabemos o que esta chegando.
// Por isso essa conversao e automatica, e nao depende da caixinha.
enum VideoMode : int { VM_NONE = 0, VM_PQ_2020 = 1, VM_HLG_2020 = 2, VM_SCRGB = 3 };

// Vira um cbuffer de 4 floats + 2 ints no PS do app. Com mode == VM_NONE o shader pula
// tudo e o pixel passa intacto - e por isso que ligar/desligar em runtime e de graca.
struct ShaderParams {
    int   mode        = VM_NONE;
    int   fixMatrix   = 1;      // desfazer a matriz YCbCr BT.709 aplicada a um sinal BT.2020
    float refWhiteNits = 203.f; // BT.2408: branco grafico de referencia em HDR
    float peakNits     = 1000.f;// pico assumido do conteudo, para o roll-off
    float knee         = 0.75f; // acima disto comeca a compressao (em unidades de branco)
    float _pad         = 0.f;
};

// ------------------------------------------------------------------ texto pro usuario
// Uma linha para o log / toast, descrevendo o estado detectado.
inline std::wstring DescribeW(const Info& i, const DxgiInfo& d, const std::wstring& name) {
    std::wstring s = name.empty() ? TS(L"monitor do alvo", L"the target monitor") : name;
    if (!i.ok && !d.ok) return s + T(L": nao consegui ler o estado de cor", L": could not read the colour state");
    bool on = d.ok ? d.hdr : i.enabled;
    s += T(on ? L": HDR LIGADO" : L": HDR desligado", on ? L": HDR ON" : L": HDR off");
    wchar_t b[256];
    if (d.ok) {
        _snwprintf(b, 256, T(L"  (%u bits, pico %.0f nits, tela cheia %.0f nits)",
                             L"  (%u bits, peak %.0f nits, full screen %.0f nits)"),
                   d.bitsPerColor, d.maxLum, d.maxFullFrameLum);
        s += b;
    }
    if (on && i.ok && i.sdrWhiteNits > 0.f) {
        _snwprintf(b, 256, T(L"  branco SDR %.0f nits", L"  SDR white %.0f nits"), i.sdrWhiteNits);
        s += b;
    }
    if (!on && i.ok && i.wideGamut) s += T(L"  (gama largo/ACM ativo)", L"  (wide gamut/ACM active)");
    return s;
}

// ------------------------------------------------------------------ textos da interface
// Escritos aqui de proposito: a semantica e desta camada, e assim o texto nao se perde
// quando alguem mexer na tela.
namespace Txt {

// rotulo da caixinha.
// ATENCAO A SEMANTICA: a conversao de HDR e AUTOMATICA e nao passa por aqui. Quando o
// monitor do alvo esta em HDR, a captura ja abre em FP16 e o shader ja converte, marcado
// ou nao - e seguro e nao tem efeito colateral. Esta caixa e so o PLANO B: desligar o HDR
// do monitor, para o caso raro em que a conversao nao resolve (tipicamente um jogo que
// escolheu HDR ao iniciar e nao gosta do que ve).
inline const wchar_t* Caixa() {
    return T(L"Desligar o HDR do monitor enquanto captura (plano B)",
             L"Turn the monitor\x27s HDR off while capturing (plan B)");
}

// tooltip curto - o do seletor
inline const wchar_t* Tooltip() {
    return T(L"NAO precisa marcar isto para o HDR funcionar: quando o monitor esta em HDR o app\n"
             L"ja captura em alta precisao e converte a imagem sozinho, sem mexer em nada seu.\n"
             L"\n"
             L"Esta caixa e o plano B: alem de converter, desliga o HDR do monitor onde esta o\n"
             L"alvo enquanto o app roda, e religa sozinho ao sair. Serve quando a imagem ainda\n"
             L"sai errada - por exemplo um jogo que decidiu usar HDR e nao gostou do resultado.\n"
             L"Custa um piscar de tela ao ligar e outro ao sair.\n"
             L"\n"
             L"Em placa de captura ou console isto nao desliga nada (o HDR esta do outro lado do\n"
             L"cabo): ali a caixa autoriza a conversao de PQ/BT.2020, que e um palpite e por isso\n"
             L"pede sua confirmacao.",

             L"You do NOT need to check this for HDR to work: when the monitor is in HDR the app\n"
             L"already captures at high precision and converts the image on its own, touching\n"
             L"nothing of yours.\n"
             L"\n"
             L"This box is plan B: on top of converting, it turns HDR off on the monitor the\n"
             L"target is on while the app runs, and turns it back on when it quits. Use it when\n"
             L"the image still comes out wrong - a game that chose HDR and did not like the\n"
             L"result, for instance. It costs one screen blink going in and another going out.\n"
             L"\n"
             L"On a capture card or console this turns nothing off (the HDR is on the other side\n"
             L"of the cable): there the box authorises the PQ/BT.2020 conversion, which is a\n"
             L"guess and therefore asks for your confirmation.");
}

// texto longo - hint do seletor / rodape do painel
inline const wchar_t* Explicacao() {
    return T(L"Imagem lavada, cinzenta, sem preto e sem branco quase sempre e HDR no caminho.\n"
             L"O app trata isso sozinho, de um jeito diferente para cada fonte:\n\n"
             L"  - jogo / janela / monitor: se o monitor esta em HDR, a captura abre em 16 bits\n"
             L"    por canal e o app converte a imagem para SDR na hora, com o brilho de papel\n"
             L"    real do seu monitor. E automatico, nao mexe no seu display e nao tem ajuste\n"
             L"    para fazer. E tambem o caminho mais fiel: a cor chega sem passar pelo\n"
             L"    conversor generico do Windows.\n\n"
             L"  - placa de captura / console / camera: o HDR vem do console pelo HDMI e nao ha\n"
             L"    o que desligar aqui. O app reconhece pelos dados da placa ou pela cara da\n"
             L"    imagem e converte (PQ / BT.2020 para SDR) - mas como ai e palpite, ele pede\n"
             L"    sua confirmacao na caixa. Pode ligar e desligar com o jogo rodando.\n\n"
             L"A caixa \"Desligar o HDR do monitor\" e o plano B, para quando a conversao nao\n"
             L"basta. Ela desliga o HDR so do monitor onde esta o alvo, enquanto o app roda, e\n"
             L"religa sozinho ao fechar.\n\n"
             L"Duas ressalvas honestas:\n"
             L"  - jogo que ja esta aberto costuma decidir se usa HDR so na inicializacao. Se\n"
             L"    voce marcar a caixa com o jogo aberto, pode ser preciso reiniciar o jogo.\n"
             L"  - no console, poe a saida em SDR se puder: a placa de captura e de 8 bits de\n"
             L"    qualquer jeito, entao HDR nela e perda sem ganho nenhum.",

             L"A washed-out, grey image with no real black and no real white is almost always HDR\n"
             L"in the way. The app handles it on its own, differently for each source:\n\n"
             L"  - game / window / monitor: if the monitor is in HDR, the capture opens at 16 bits\n"
             L"    per channel and the app converts the image to SDR on the fly, using your\n"
             L"    monitor\x27s real paper-white level. It is automatic, it does not touch your\n"
             L"    display and there is nothing to tune. It is also the most faithful path: the\n"
             L"    colour arrives without going through the generic Windows converter.\n\n"
             L"  - capture card / console / camera: the HDR comes from the console over HDMI and\n"
             L"    there is nothing to turn off here. The app spots it from the card data or from\n"
             L"    the look of the image and converts it (PQ / BT.2020 to SDR) - but since that\n"
             L"    is a guess, it asks for your confirmation in the box. You can toggle it with\n"
             L"    the game running.\n\n"
             L"The \"Turn the monitor HDR off\" box is plan B, for when converting is not enough.\n"
             L"It turns HDR off only on the monitor the target is on, while the app runs, and\n"
             L"turns it back on when it closes.\n\n"
             L"Two honest caveats:\n"
             L"  - a game that is already open usually decides about HDR only at startup. If you\n"
             L"    check the box with the game open, you may have to restart the game.\n"
             L"  - on a console, set the output to SDR if you can: the capture card is 8-bit\n"
             L"    anyway, so HDR into it is all loss and no gain.");
}

// aviso quando ha HDR e a caixa esta DESMARCADA.
// Na fonte de tela isso NAO e mais um problema - a conversao ja aconteceu -, entao o
// texto informa em vez de pedir acao. Use Aviso() so quando a conversao nao esta ativa.
inline std::wstring Aviso(bool fonteDeTela) {
    if (fonteDeTela)
        return T(L"Esse monitor esta em HDR e a captura nao conseguiu abrir em alta precisao,\n"
                 L"entao a imagem chega ja convertida pelo Windows - lavada.\n"
                 L"Marque \"Desligar o HDR do monitor\" para o app resolver,\n"
                 L"ou aperte Win+Alt+B para desligar o HDR na mao.\n"
                 L"O jogo ja aberto pode precisar reiniciar para perceber.",
                 L"This monitor is in HDR and the capture could not open at high precision,\n"
                 L"so the image arrives already converted by Windows - washed out.\n"
                 L"Check \"Turn the monitor HDR off\" and let the app sort it out,\n"
                 L"or press Win+Alt+B to turn HDR off by hand.\n"
                 L"A game that is already open may need a restart to notice.");
    return T(L"Parece que a placa esta recebendo HDR do console - a imagem chega lavada.\n"
             L"Marque a caixa de HDR para o app converter,\n"
             L"ou ponha a saida do console em SDR (e o melhor: a placa e de 8 bits).",
             L"Looks like the card is getting HDR from the console - the image arrives washed out.\n"
             L"Check the HDR box to let the app convert it,\n"
             L"or set the console output to SDR (that is best: the card is 8-bit anyway).");
}

// informativo, quando a conversao automatica ESTA ativa (fonte de tela em FP16)
inline const wchar_t* ConversaoAtiva() {
    return T(L"Monitor em HDR: capturando em 16 bits e convertendo na hora.\n"
             L"Nao precisa fazer nada - e automatico e nao mexe no seu display.",
             L"Monitor in HDR: capturing at 16 bits and converting on the fly.\n"
             L"Nothing to do - it is automatic and it does not touch your display.");
}

} // namespace Txt

// Compatibilidade com o texto antigo (fonte de tela).
inline std::wstring WarnText() { return Txt::Aviso(true); }

// =====================================================================================
//  Treatment - a semantica da caixinha, uma classe so para as duas fontes.
//
//  Contrato com a interface (o agente do main.cpp chama exatamente isto):
//      Hdr::Treatment tr;
//      tr.Init(iniPath);              // uma vez, no inicio
//      tr.RecoverStale();             // desfaz o que uma sessao morta deixou
//      tr.SetSourceScreen(hwnd, mon); //  ..ou..
//      tr.SetSourceVideo(cor);        // quando a fonte fica conhecida
//      tr.Detect();                   // sempre, independente da caixa; barato
//      tr.SetEnabled(marcada);        // idempotente, pode ser chamada a qualquer hora
//      tr.Shader();                   // cbuffer do PS (VM_NONE quando nao ha conversao)
//      tr.StatusLine(); tr.WarnLine();
//
//  Tudo e idempotente e seguro em runtime. SetEnabled(true) duas vezes nao desliga o
//  HDR duas vezes; SetEnabled(false) sem nada ligado nao faz nada.
// =====================================================================================
class Treatment {
public:
    enum Source { SRC_NONE = 0, SRC_SCREEN = 1, SRC_VIDEO = 2 };

    void Init(const std::wstring& iniPath) {
        ini_ = iniPath;
        mgr_.Init(iniPath);
        enabled_ = TreatmentEnabledFromIni(iniPath);
    }
    int RecoverStale() { return mgr_.RecoverStale(); }

    void SetSourceScreen(HWND win, HMONITOR mon) {
        src_ = SRC_SCREEN; win_ = win; mon_ = mon;
    }
    void SetSourceVideo(const VideoColor& c) {
        src_ = SRC_VIDEO; vcol_ = c;
    }
    // Medicao opcional de conteudo, para a fonte de video. Chame a cada ~30 frames.
    void FeedFrameStats(const LumaStats& s) { stats_ = s; guess_ = GuessPq(s); }

    // Detecta. Nao mexe em nada. Pode ser chamada quantas vezes quiser.
    void Detect() {
        if (src_ == SRC_SCREEN) {
            mgr_.DetectFor(win_, mon_, &info_, &dxgi_, &monName_);
            hdrFound_ = dxgi_.ok ? dxgi_.hdr : info_.enabled;
            if (info_.ok && info_.sdrWhiteNits > 0.f) sdrWhite_ = info_.sdrWhiteNits;
        } else if (src_ == SRC_VIDEO) {
            hdrFound_ = vcol_.hdrDeclared || (guess_.suspect && guess_.confidence >= 0.5);
        }
    }

    bool IsEnabled() const { return enabled_; }
    bool HdrFound() const { return hdrFound_; }
    bool Acting()   const { return src_ == SRC_SCREEN ? mgr_.Changed() : (enabled_ && hdrFound_); }
    Source Src()    const { return src_; }
    const LumaStats& Stats() const { return stats_; }
    const PqGuess&   Guess() const { return guess_; }
    const DxgiInfo&  Display() const { return dxgi_; }

    // A caixinha. Idempotente e sem ordem obrigatoria: redetecta antes de agir, entao
    // pode ser chamada a qualquer momento, inclusive sem um Detect() recente.
    bool SetEnabled(bool on) {
        enabled_ = on;
        SetTreatmentEnabledInIni(ini_, on);
        Detect();                       // o estado do display pode ter mudado desde a
                                        // ultima deteccao - inclusive por nossa causa
        if (src_ == SRC_SCREEN) {
            if (on) {
                if (mgr_.Changed()) {
                    tentativa_ = TENT_OK;          // ja estamos agindo: nada a fazer
                } else if (hdrFound_) {
                    mgr_.DisableFor(win_, mon_);
                    tentativa_ = (mgr_.lastOutcome == Manager::OUT_DID)        ? TENT_OK
                               : (mgr_.lastOutcome == Manager::OUT_NOT_NEEDED) ? TENT_NAO_PRECISOU
                                                                               : TENT_FALHOU;
                } else {
                    tentativa_ = TENT_NAO_PRECISOU;
                }
            } else {
                mgr_.Restore();
                tentativa_ = TENT_NENHUMA;
            }
        }
        // fonte de video: nao ha nada a "aplicar" - Shader() ja responde diferente
        return enabled_;
    }

    // Desfaz tudo o que este processo fez. Chamar no encerramento (o destrutor do
    // Manager tambem chama, mas explicito e melhor).
    void Restore() { mgr_.Restore(); }

    // A captura de tela avisa aqui qual formato ela conseguiu abrir. Com FP16 ativo o
    // frame chega em scRGB linear e a conversao no shader e obrigatoria - sem ela a
    // imagem sai errada, porque o dado nao e mais display-referred.
    void SetScreenFp16(bool on, float sdrWhiteNits) {
        fp16_ = on;
        if (sdrWhiteNits > 0.f) sdrWhite_ = sdrWhiteNits;
        // Pico do PAINEL, nao um 1000 chutado: e o teto do que o compositor pode ter
        // entregue. So importa se alguem baixar o joelho (ver ScreenKnee()).
        if (dxgi_.ok && dxgi_.maxLum > sdrWhite_) screenPeak_ = dxgi_.maxLum;
    }
    bool ScreenFp16() const { return fp16_; }

    // O cbuffer do pixel shader. VM_NONE quando nao ha conversao a fazer.
    ShaderParams Shader() const {
        ShaderParams p;
        // Fonte de tela em FP16: automatico. Nos pedimos scRGB, entao temos de
        // interpretar scRGB - independente da caixinha, que trata de outra coisa.
        if (src_ == SRC_SCREEN) {
            if (!fp16_) return p;                       // VM_NONE: BGRA8, caminho de sempre
            p.mode = VM_SCRGB;
            p.fixMatrix = 0;                            // scRGB ja e BT.709
            p.refWhiteNits = sdrWhite_;                 // o branco de papel REAL do monitor
            p.peakNits = screenPeak_;                   // pico do painel, medido
            // Joelho 1.0 = identidade ate o branco de papel. Aqui fidelidade vale mais que
            // highlight: o caso comum e um jogo SDR num desktop HDR, e ele TEM de voltar
            // identico. Quem captura conteudo HDR com muito realce baixa isto (0.75 e um
            // bom comeco) por DLSS5_HDR_KNEE.
            p.knee = screenKnee_;
            return p;
        }
        if (src_ != SRC_VIDEO || !enabled_ || !hdrFound_) return p;    // VM_NONE
        p.mode = (vcol_.hasTf && vcol_.tf == VTF_HLG) ? VM_HLG_2020 : VM_PQ_2020;
        // A placa etiquetou matriz BT.709 (o padrao dela) num sinal que e BT.2020: o
        // Video Processor desmatriciou errado e precisamos desfazer. Se ela declarou
        // matriz 2020, a conversao ja saiu certa e nao ha o que corrigir.
        p.fixMatrix = (vcol_.hasMtx && (vcol_.matrix == VMX_2020_10 || vcol_.matrix == VMX_2020_12)) ? 0 : 1;
        p.refWhiteNits = refWhite_;
        p.peakNits     = peak_;
        p.knee         = knee_;
        return p;
    }
    // Ajustes finos, se a interface quiser expor (nao precisa).
    void SetRefWhite(float nits) { refWhite_ = nits; }
    void SetPeak(float nits)     { peak_ = nits; }
    void SetKnee(float k)        { knee_ = k; }

    // Uma linha para o painel.
    // Cuidado que custou um relato de campo: "a caixa esta marcada" NAO quer dizer "ja
    // tentei desligar". Quem sabe se houve tentativa, e como ela terminou, e o
    // tentativa_ - a caixa vem do ini e pode estar marcada antes de qualquer acao.
    std::wstring StatusLine() const {
        if (src_ == SRC_SCREEN) {
            if (mgr_.Changed())
                return T(fp16_ ? L"convertendo em 16 bits · e HDR do monitor desligado pelo app"
                               : L"HDR desligado pelo app · volta sozinho ao sair",
                         fp16_ ? L"converting at 16 bits · and monitor HDR turned off by the app"
                               : L"HDR turned off by the app · it comes back when the app quits");
            if (fp16_) return T(L"monitor em HDR · capturando em 16 bits e convertendo (automático)",
                                L"monitor in HDR · capturing at 16 bits and converting (automatic)");
            if (tentativa_ == TENT_FALHOU)
                return T(L"HDR ligado · nao consegui desligar (Win+Alt+B desliga na mao)",
                         L"HDR on · could not turn it off (Win+Alt+B does it by hand)");
            if (hdrFound_)      return T(enabled_ ? L"HDR ligado · vou desligar ao iniciar a captura"
                                                  : L"HDR ligado · a captura nao abriu em 16 bits, imagem lavada",
                                         enabled_ ? L"HDR on · I will turn it off when the capture starts"
                                                  : L"HDR on · the capture did not open at 16 bits, washed-out image");
            return T(L"sem HDR neste monitor · nada a fazer", L"no HDR on this monitor · nothing to do");
        }
        if (src_ == SRC_VIDEO) {
            if (vcol_.hdrDeclared && vcol_.trustworthy)
                return T(enabled_ ? L"PQ/HDR declarado pela placa · convertendo"
                                  : L"PQ/HDR declarado pela placa · marque para converter",
                         enabled_ ? L"PQ/HDR declared by the card · converting"
                                  : L"PQ/HDR declared by the card · check the box to convert");
            if (guess_.suspect)
                return T(enabled_ ? L"imagem com cara de HDR · convertendo"
                                  : L"imagem com cara de HDR · marque para converter",
                         enabled_ ? L"the image looks like HDR · converting"
                                  : L"the image looks like HDR · check the box to convert");
            return T(L"a placa nao indica HDR · nada a fazer", L"the card reports no HDR · nothing to do");
        }
        return T(L"fonte indefinida", L"undefined source");
    }

    // O aviso, quando ha HDR e ninguem esta tratando. Vazio quando nao ha o que dizer -
    // e com a conversao automatica ativa nao ha: o problema ja esta resolvido.
    std::wstring WarnLine() const {
        if (!hdrFound_) return L"";
        if (src_ == SRC_SCREEN && fp16_) return L"";   // ja convertendo, nada a avisar
        if (enabled_) return L"";
        return Txt::Aviso(src_ == SRC_SCREEN);
    }

    // Como terminou a ultima tentativa de agir. Serve para o texto do painel nao
    // confundir "marcada, ainda nao agi" com "tentei e falhei".
    enum Tentativa { TENT_NENHUMA = 0, TENT_OK, TENT_NAO_PRECISOU, TENT_FALHOU };
    Tentativa LastAttempt() const { return tentativa_; }

private:
    std::wstring ini_;
    Manager      mgr_;
    Source       src_ = SRC_NONE;
    bool         enabled_ = false;
    bool         hdrFound_ = false;
    Tentativa    tentativa_ = TENT_NENHUMA;

    HWND         win_ = nullptr;
    HMONITOR     mon_ = nullptr;
    Info         info_;
    DxgiInfo     dxgi_;
    std::wstring monName_;
    float        sdrWhite_ = 80.f;

    bool         fp16_ = false;      // a captura de tela esta em R16G16B16A16Float?
    float        screenPeak_ = 1000.f;
    float        screenKnee_ = KneeFromEnv();

    // Joelho do roll-off da captura de tela. 1.0 (padrao) = sem roll-off.
    static float KneeFromEnv() {
        char b[32];
        if (GetEnvironmentVariableA("DLSS5_HDR_KNEE", b, 32)) {
            double v = atof(b);
            if (v > 0.05 && v <= 1.0) return (float)v;
        }
        return 1.0f;
    }

    VideoColor   vcol_;
    LumaStats    stats_;
    PqGuess      guess_;
    float        refWhite_ = 203.f, peak_ = 1000.f, knee_ = 0.75f;
};

} // namespace Hdr
