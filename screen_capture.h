// =====================================================================================
//  DLSS5 For All - modulo de captura de tela (janela / monitor)
//
//  Mesma ideia da captura de camera, so que a "fonte de video" e uma janela de jogo
//  (ou um monitor inteiro). Usa Windows Graphics Capture (WGC, o mesmo backend do
//  "Captura de Janela (Windows 10)" do OBS):
//   - pega o frame ja composto pelo DWM, direto em textura da GPU (sem readback pra CPU)
//   - funciona com janela ocluida / atras de outra, e com borderless fullscreen
//   - a textura vem no MESMO device D3D11 do renderer, entao o caminho e
//     WGC -> CopySubresourceRegion -> SRV -> nosso draw -> Present -> ReShade/DLSS5
//
//  1 frame do jogo = 1 SetEvent = 1 Present. E o que o Feeder quer pra estimar
//  motion vector: nada de present repetido com imagem identica.
//
//  Escrito na ABI "crua" do WinRT (nada de C++/WinRT) pra continuar compilando com
//  o mesmo MinGW-w64 que gera o exe.
//
//  Este header depende de uma funcao Log(const char*, ...) definida no main.cpp antes
//  do #include - e la que sai o relatorio de latencia (LAT[...] no dlss5forall.log).
// =====================================================================================
#pragma once

#include "idioma.h"           // T(pt, en): as mensagens daqui viram caixa de erro na tela
#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include "hdr_control.h"   // so para perguntar "o monitor do alvo esta em HDR?"
#include <string>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstdlib>

// =====================================================================================
//  Medicao de latencia (QueryPerformanceCounter)
//
//  Instantes marcados no caminho de UM frame:
//    t0  SystemRelativeTime do frame  - carimbo do proprio DWM (base QPC, unidade 100ns)
//    t1  FrameArrived (Invoke)        - quando o WGC avisou que tem frame novo
//    t2  Tick()                       - drenar a fila + recortar + CopySubresourceRegion
//    t3  DrawAndPresent()             - nosso draw + Present + toda a cadeia ReShade/DLSS
//    t4  vblank real                  - via DXGI_FRAME_STATISTICS.SyncQPCTime
//
//  O numero que mais importa e t3->t4 ("PRESENT->TELA"): e ali que mora a fila do
//  driver/DWM, que e a unica parte grande da latencia que o nosso codigo controla.
//  "intervalo entre FrameArrived" diz o ritmo REAL que o WGC entrega - se ele nao bater
//  com a taxa do monitor, a latencia extra e do sistema, nao nossa.
//
//  Sai no log a cada 300 frames. Custo: ~4 QueryPerformanceCounter por frame.
// =====================================================================================
static void Log(const char* fmt, ...);

struct LatSeries {
    static const int CAP = 1024;
    double v[CAP]; int n = 0;
    void add(double x) { if (n < CAP) v[n++] = x; }
    void reset() { n = 0; }
    double avg() const { if (!n) return 0.0; double s = 0; for (int i = 0; i < n; i++) s += v[i]; return s / n; }
    double pct(double q) const {
        if (!n) return 0.0;
        std::vector<double> t(v, v + n); std::sort(t.begin(), t.end());
        int i = (int)(t.size() * q); if (i >= (int)t.size()) i = (int)t.size() - 1;
        return t[i];
    }
    double p95() const { return pct(0.95); }
    double mn() const { if (!n) return 0.0; double m = v[0]; for (int i = 1; i < n; i++) if (v[i] < m) m = v[i]; return m; }
    double mx() const { double m = 0; for (int i = 0; i < n; i++) if (v[i] > m) m = v[i]; return m; }
};

struct LatProbe {
    LARGE_INTEGER freq{};
    int       every = 300;
    UINT32    n = 0;
    bool      fedByPresent = false;   // o loop principal esta chamando Present()?

    LatSeries srt;      // t0 -> t1 (carimbo do DWM x recebimento; negativo = t0 e no futuro)
    LatSeries wake;     // t1 -> t2 (evento -> loop acordar)
    LatSeries tick;     // duracao do Tick
    LatSeries pres;     // duracao do DrawAndPresent (inclui a cadeia ReShade/DLSS)
    LatSeries disp;     // fim do Present -> vblank em que o frame apareceu
    // gap/nArrive/lastArr sao escritos pelo Invoke do WGC, que roda numa THREAD DO SISTEMA,
    // e lidos/zerados pelo laco principal em Dump()/Reset(). Sem sincronizacao isso e data
    // race: nao corrompe memoria (o indice da serie e checado), mas pode congelar a serie
    // de "gap" no relatorio LAT, que e justamente o numero que diz o ritmo real do WGC.
    // Um SRWLOCK basta - a secao critica sao tres somas, e o caminho e chamado ~100x/s.
    SRWLOCK   lkGap = SRWLOCK_INIT;
    LatSeries gap;      // intervalo entre dois FrameArrived (protegido por lkGap)
    UINT32    nArrive = 0, nTick = 0, nPres = 0;

    LONGLONG  presQpc[256]{};
    UINT      lastPC  = 0;
    LONGLONG  lastArr = 0;
    LONGLONG  curSrt  = 0;
    LONGLONG  t0dump  = 0;

    LatProbe() { QueryPerformanceFrequency(&freq); t0dump = Now(); }

    LONGLONG Now() const { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
    double   Ms(LONGLONG d) const { return freq.QuadPart ? (double)d * 1000.0 / (double)freq.QuadPart : 0.0; }
    LONGLONG QpcTo100ns(LONGLONG q) const { return freq.QuadPart ? (LONGLONG)((double)q * 10000000.0 / (double)freq.QuadPart) : 0; }

    void Reset() {
        srt.reset(); wake.reset(); tick.reset(); pres.reset(); disp.reset();
        AcquireSRWLockExclusive(&lkGap);
        gap.reset(); nArrive = 0; lastArr = 0;
        ReleaseSRWLockExclusive(&lkGap);
        n = 0; nTick = 0; nPres = 0; t0dump = Now();
    }
    // Chamado do Invoke (thread do sistema): a unica escrita que vem de fora do laco.
    void ChegouFrame(LONGLONG q) {
        AcquireSRWLockExclusive(&lkGap);
        if (lastArr) gap.add(Ms(q - lastArr));
        lastArr = q;
        nArrive++;
        ReleaseSRWLockExclusive(&lkGap);
    }

    // Chamado pelo loop principal logo depois do DrawAndPresent.
    void Present(LONGLONG p0, LONGLONG p1, IDXGISwapChain* sc) {
        fedByPresent = true;
        pres.add(Ms(p1 - p0));
        nPres++;
        if (sc) {
            UINT pc = 0;
            if (SUCCEEDED(sc->GetLastPresentCount(&pc))) presQpc[pc & 255] = p1;
            DXGI_FRAME_STATISTICS fs{};
            if (SUCCEEDED(sc->GetFrameStatistics(&fs)) && fs.PresentCount && fs.PresentCount != lastPC) {
                LONGLONG q = presQpc[fs.PresentCount & 255];
                if (q) { double d = Ms(fs.SyncQPCTime.QuadPart - q); if (d > -100.0 && d < 500.0) disp.add(d); }
                lastPC = fs.PresentCount;
            }
        }
        curSrt = 0;
        if (++n >= (UINT32)every) { Dump(); Reset(); }
    }

    void Dump() {
        double s = Ms(Now() - t0dump) / 1000.0; if (s <= 0.0) s = 1.0;
        // nArrive tambem e escrito pelo Invoke do WGC: le sob o mesmo lock do gap, senao
        // sobra exatamente a corrida que este lock foi criado para fechar.
        AcquireSRWLockShared(&lkGap);
        const UINT32 nArr = nArrive;
        ReleaseSRWLockShared(&lkGap);
        Log("LAT %.0fs | fps chegada=%.1f copiado=%.1f present=%.1f | srt %.2f/%.2f | acordar %.2f/%.2f"
            " | tick %.2f/%.2f | present %.2f/%.2f | PRESENT->TELA %.2f/%.2f (n=%d)  [ms media/p95]",
            s, nArr / s, nTick / s, nPres / s,
            srt.avg(), srt.p95(), wake.avg(), wake.p95(), tick.avg(), tick.p95(),
            pres.avg(), pres.p95(), disp.avg(), disp.p95(), disp.n);
        AcquireSRWLockShared(&lkGap);
        const double gmn = gap.mn(), g05 = gap.pct(0.05), gav = gap.avg(), g95 = gap.p95(), gmx = gap.mx();
        ReleaseSRWLockShared(&lkGap);
        Log("LAT ritmo do WGC (intervalo entre FrameArrived): min %.2f p05 %.2f media %.2f p95 %.2f max %.2f ms",
            gmn, g05, gav, g95, gmx);
    }
};

static LatProbe g_lat;

namespace WGC {
namespace WF   = ABI::Windows::Foundation;
namespace WG   = ABI::Windows::Graphics;
namespace WGCa = ABI::Windows::Graphics::Capture;
namespace WGDX = ABI::Windows::Graphics::DirectX;
namespace WGD3 = ABI::Windows::Graphics::DirectX::Direct3D11;
}

// =====================================================================================
//  IGraphicsCaptureSession5 - a causa do teto de 50 fps
//
//  O header do MinGW nao declara essa interface, mas ela EXISTE no Windows 11 (conferido
//  em C:\Windows\System32\WinMetadata\Windows.Graphics.winmd, build 26200). Ela expoe
//  MinUpdateInterval: o intervalo MINIMO entre dois frames que o WGC entrega.
//
//  O padrao do sistema e 16,0 ms - um teto de ~60 fps pensado pra economizar bateria.
//  So que o teto nao e 60: o WGC entrega no vblank do DWM. Num monitor de 100 Hz o DWM
//  compoe a cada 10 ms, e "no minimo 16 ms" arredonda pra cima ate o proximo vblank
//  valido, ou seja DOIS vblanks: 20,00 ms = 50,0 fps cravados. Foi exatamente isso que
//  medimos. (Num monitor de 60 Hz o mesmo 16 ms cabe em 1 vblank de 16,67 ms e ninguem
//  percebe o problema - por isso quase ninguem reclama disso.)
//
//  Medido aqui, mesmo alvo, mesma sessao, so mudando esse campo:
//     MinUpdateInterval = 16 ms (padrao) -> 49,93 fps, gap 20,00 ms (p05 19,4 / p95 20,6)
//     MinUpdateInterval =  0 ms          -> 99,83 fps, gap 10,00 ms (p05  9,3 / p95 10,7)
//  Desktop Duplication no mesmo monitor, como controle: 100,25 fps, gap 10,00 ms.
//
//  Precisa ser ajustado ANTES do StartCapture.
// =====================================================================================
static const GUID IID_WgcSession5 =
    { 0x67c0ea62, 0x1f85, 0x5061, { 0x92, 0x5a, 0x23, 0x9b, 0xe0, 0xac, 0x09, 0xcb } };

struct IWgcSession5 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_MinUpdateInterval(WGC::WF::TimeSpan* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MinUpdateInterval(WGC::WF::TimeSpan value) = 0;
};

// ------------------------------------------------------------------ alvos enumeraveis
enum SourceKind { SRC_CAMERA = 0, SRC_WINDOW = 1, SRC_MONITOR = 2 };

struct WinTarget {
    HWND         hwnd = nullptr;
    std::wstring title;    // titulo da janela
    std::wstring proc;     // nome do exe
    std::wstring label;    // "titulo  [exe.exe]  1920x1080"
    int          w = 0, h = 0;
};

struct MonTarget {
    HMONITOR     mon = nullptr;
    std::wstring label;    // "\\.\DISPLAY1 (principal)  2560x1440"
    int          w = 0, h = 0;
};

static bool WgcWindowIsCloaked(HWND h) {
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) return cloaked != 0;
    return false;
}

static std::wstring WgcProcessName(HWND h) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (!pid) return L"";
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return L"";
    wchar_t path[MAX_PATH] = L""; DWORD n = MAX_PATH;
    std::wstring out;
    if (QueryFullProcessImageNameW(p, 0, path, &n)) {
        out = path; size_t k = out.find_last_of(L"\\/");
        if (k != std::wstring::npos) out = out.substr(k + 1);
    }
    CloseHandle(p);
    return out;
}

struct WgcEnumCtx { std::vector<WinTarget>* v; HWND self; };

static BOOL CALLBACK WgcEnumProc(HWND h, LPARAM lp) {
    WgcEnumCtx* c = (WgcEnumCtx*)lp;
    if (h == c->self) return TRUE;
    if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
    if (GetAncestor(h, GA_ROOT) != h) return TRUE;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    if (WgcWindowIsCloaked(h)) return TRUE;
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return TRUE;
    std::wstring t(len + 1, L'\0');
    GetWindowTextW(h, &t[0], len + 1); t.resize(len);
    if (t == L"Program Manager" || t == L"Configuracoes" || t == L"Settings") return TRUE;
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(h, &r);
    int w = r.right - r.left, hh = r.bottom - r.top;
    if (w < 64 || hh < 64) return TRUE;
    WinTarget wt; wt.hwnd = h; wt.title = t; wt.proc = WgcProcessName(h); wt.w = w; wt.h = hh;
    // O titulo de uma janela de jogo passa de 512 caracteres com folga (navegadores e
    // launchers sao os campeoes). _snwprintf trunca SEM por o terminador, e o std::wstring
    // logo abaixo leria alem do buffer.
    wchar_t buf[512];
    _snwprintf(buf, 512, L"%ls  [%ls]  %dx%d", t.c_str(), wt.proc.empty() ? L"?" : wt.proc.c_str(), w, hh);
    buf[511] = 0;
    wt.label = buf;
    c->v->push_back(wt);
    return TRUE;
}

static std::vector<WinTarget> EnumCaptureWindows(HWND self) {
    std::vector<WinTarget> v; WgcEnumCtx c{ &v, self };
    EnumWindows(WgcEnumProc, (LPARAM)&c);
    return v;
}

static BOOL CALLBACK WgcMonProc(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    auto* v = (std::vector<MonTarget>*)lp;
    MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(m, (MONITORINFO*)&mi)) return TRUE;
    MonTarget t; t.mon = m;
    t.w = mi.rcMonitor.right - mi.rcMonitor.left;
    t.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    wchar_t buf[256];
    _snwprintf(buf, 256, L"%ls%ls  %dx%d", mi.szDevice, (mi.dwFlags & MONITORINFOF_PRIMARY) ? T(L" (principal)", L" (primary)") : L"", t.w, t.h);
    buf[255] = 0;
    t.label = buf;
    v->push_back(t);
    return TRUE;
}

static std::vector<MonTarget> EnumCaptureMonitors() {
    std::vector<MonTarget> v;
    EnumDisplayMonitors(nullptr, nullptr, WgcMonProc, (LPARAM)&v);
    return v;
}

// ------------------------------------------------------------------ handler FrameArrived
// O frame pool e "free threaded": o Invoke cai numa thread do sistema. Ele nao faz nada
// alem de carimbar a hora e acordar o loop principal, que e quem toca no device D3D11.
typedef WGC::WF::ITypedEventHandler<WGC::WGCa::Direct3D11CaptureFramePool*, IInspectable*> WgcFrameArrivedBase;

// Os campos sao atomicos e existe um contador de chamadas em voo porque
// remove_FrameArrived NAO espera o Invoke que ja comecou: sem isso, um SetEvent
// atrasado escreveria num HANDLE que o Stop() acabou de fechar (e que o Windows pode
// ja ter reciclado para outro objeto). Desarmar + esperar o voo acabar e o que torna
// Stop()+Start() - a recriacao da captura - seguro.
struct WgcFrameHandler : WgcFrameArrivedBase {
    LONG   rc = 1;
    std::atomic<HANDLE>                 evt{ nullptr };
    std::atomic<std::atomic<UINT32>*>   counter{ nullptr };
    std::atomic<std::atomic<LONGLONG>*> arriveQpc{ nullptr };   // QPC do ultimo FrameArrived
    std::atomic<LONG>                   inFlight{ 0 };

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** o) override {
        if (!o) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IAgileObject) ||
            IsEqualIID(riid, __uuidof(WgcFrameArrivedBase))) {
            *o = static_cast<WgcFrameArrivedBase*>(this);
            InterlockedIncrement(&rc);
            return S_OK;
        }
        *o = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&rc); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&rc);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    virtual ~WgcFrameHandler() {}   // 'delete this' acima em classe polimorfica pede destrutor virtual
    HRESULT STDMETHODCALLTYPE Invoke(WGC::WGCa::IDirect3D11CaptureFramePool*, IInspectable*) override {
        inFlight.fetch_add(1, std::memory_order_acquire);
        LONGLONG q = g_lat.Now();
        if (auto* a = arriveQpc.load(std::memory_order_relaxed)) a->store(q, std::memory_order_relaxed);
        g_lat.ChegouFrame(q);
        if (auto* c = counter.load(std::memory_order_relaxed)) (*c)++;
        if (HANDLE e = evt.load(std::memory_order_relaxed)) SetEvent(e);
        inFlight.fetch_sub(1, std::memory_order_release);
        return S_OK;
    }

    // Corta o handler de tudo que o Stop() vai destruir e espera quem ja estava dentro
    // do Invoke sair. O teto de espera existe para o encerramento nunca travar por causa
    // de uma thread do sistema presa.
    void Desarmar() {
        evt.store(nullptr, std::memory_order_relaxed);
        counter.store(nullptr, std::memory_order_relaxed);
        arriveQpc.store(nullptr, std::memory_order_relaxed);
        for (int i = 0; i < 500 && inFlight.load(std::memory_order_acquire) > 0; i++) Sleep(1);
    }
};

// ------------------------------------------------------------------ handler Closed
// O WGC encerra a sessao sozinho quando o alvo some do jeito dele: o jogo troca de
// resolucao (recria o swapchain), vai pra tela cheia exclusiva, ou o item simplesmente
// deixa de existir. A JANELA CONTINUA VIVA nesses casos - IsWindow() diz que esta tudo
// bem enquanto nao chega mais nenhum frame.
//
// Sem este handler, o campo 'closed' so virava true quando a janela morria, e a condicao
// de recriacao do main.cpp (!Alive() && IsWindow(alvo)) era logicamente impossivel:
// codigo morto que parecia conserto. Este e o unico aviso que o sistema da.
typedef WGC::WF::ITypedEventHandler<WGC::WGCa::GraphicsCaptureItem*, IInspectable*> WgcClosedBase;

struct WgcClosedHandler : WgcClosedBase {
    LONG                             rc = 1;
    std::atomic<std::atomic<bool>*>  flag{ nullptr };   // vira true quando a sessao cai
    std::atomic<HANDLE>              evt{ nullptr };    // acorda o loop principal na hora
    std::atomic<LONG>                inFlight{ 0 };

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** o) override {
        if (!o) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IAgileObject) ||
            IsEqualIID(riid, __uuidof(WgcClosedBase))) {
            *o = static_cast<WgcClosedBase*>(this);
            InterlockedIncrement(&rc);
            return S_OK;
        }
        *o = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&rc); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&rc);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    virtual ~WgcClosedHandler() {}   // 'delete this' acima em classe polimorfica pede destrutor virtual
    HRESULT STDMETHODCALLTYPE Invoke(WGC::WGCa::IGraphicsCaptureItem*, IInspectable*) override {
        inFlight.fetch_add(1, std::memory_order_acquire);
        if (auto* f = flag.load(std::memory_order_relaxed)) f->store(true, std::memory_order_relaxed);
        if (HANDLE e = evt.load(std::memory_order_relaxed)) SetEvent(e);   // sem isto o loop so acordaria no timeout de 100 ms
        inFlight.fetch_sub(1, std::memory_order_release);
        return S_OK;
    }

    void Desarmar() {
        flag.store(nullptr, std::memory_order_relaxed);
        evt.store(nullptr, std::memory_order_relaxed);
        for (int i = 0; i < 500 && inFlight.load(std::memory_order_acquire) > 0; i++) Sleep(1);
    }
};

// ------------------------------------------------------------------ a captura em si
struct ScreenCapture {
    // alvo
    HWND      targetWin = nullptr;
    HMONITOR  targetMon = nullptr;
    bool      clientOnly = true;    // so a area do cliente (tira barra de titulo/bordas)

    // WinRT
    WGC::WGD3::IDirect3DDevice*              rtDev = nullptr;
    WGC::WGCa::IGraphicsCaptureItem*         item = nullptr;
    WGC::WGCa::IDirect3D11CaptureFramePool*  pool = nullptr;
    WGC::WGCa::IGraphicsCaptureSession*      session = nullptr;
    WgcFrameHandler*                         handler = nullptr;
    EventRegistrationToken                   token{};
    WgcClosedHandler*                        closedHandler = nullptr;
    EventRegistrationToken                   closedToken{};

    // D3D
    ID3D11Device*             dev = nullptr;
    ID3D11DeviceContext*      ictx = nullptr;  // contexto imediato cacheado (1x, nao por frame)
    ID3D11Texture2D*          tex = nullptr;   // recorte final, tamanho = w x h
    ID3D11ShaderResourceView* srv = nullptr;
    UINT w = 0, h = 0;
    UINT poolW = 0, poolH = 0;
    DXGI_FORMAT texFmt = DXGI_FORMAT_UNKNOWN;   // formato com que 'tex' foi criada

    // ---- formato da captura: 8 bits ou FP16
    // Com o HDR do Windows ligado, um frame pool BGRA8 nao recebe o buffer HDR da janela:
    // o compositor converte pra SDR com o tone map generico dele e entrega o resultado ja
    // lavado, ANTES de o ReShade existir no caminho. Pedindo o pool em
    // R16G16B16A16Float o Windows entrega scRGB LINEAR (BT.709, 1.0 = 80 nits) sem tone
    // map nenhum, e a conversao passa a ser nossa - feita no PS do app, com o brilho de
    // papel real do monitor. Ver docs\hdr.md.
    //
    // So em HDR. Com o desktop em SDR nao ha nada a ganhar e ha uma incerteza (o que o WGC
    // entrega num pool FP16 fora de HDR nao foi verificado), entao nem tentamos.
    bool  hdrFp16 = false;         // formato ATIVO agora (o main.cpp le isto)
    int   hdrFp16Mode = -1;        // -1 = automatico, 0 = forca 8 bits, 1 = forca FP16
    float hdrSdrWhiteNits = 80.f;  // paper white do monitor, pro shader normalizar

    WGC::WGDX::DirectXPixelFormat PoolFormat() const {
        return hdrFp16 ? WGC::WGDX::DirectXPixelFormat_R16G16B16A16Float
                       : WGC::WGDX::DirectXPixelFormat_B8G8R8A8UIntNormalized;
    }
    DXGI_FORMAT TexFormat() const {
        return hdrFp16 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    }

    HANDLE                 evt = nullptr;
    std::atomic<UINT32>    frames{ 0 };
    std::atomic<UINT32>    arrived{ 0 };
    std::atomic<LONGLONG>  arriveQpc{ 0 };
    std::atomic<bool>      closed{ false };   // escrito pelo handler de Closed, em outra thread
    bool                   started = false;

    // geometria do recorte, cacheada: DwmGetWindowAttribute e uma chamada ao processo
    // do DWM, e ela estava sendo feita a cada frame no caminho critico.
    RECT     lastWinRect{}, cacheWant{}, cacheOrg{};
    bool     cacheOk = false;
    LONGLONG lastSrt100ns = 0;   // SystemRelativeTime do ultimo frame copiado

    // Chaves de diagnostico (variaveis de ambiente). O padrao ja e o modo rapido;
    // servem pra reproduzir o comportamento antigo e comparar numeros no log.
    //   DLSS5_DRAIN=0      volta a copiar TODOS os frames da fila (antigo)
    //   DLSS5_RECTCACHE=0  volta a perguntar a geometria ao DWM a cada frame (antigo)
    //   DLSS5_POOLBUF=N    numero de buffers do frame pool (padrao 2)
    //   DLSS5_NOCOPY=1     dreno e descarto sem copiar - mede so o ritmo bruto do WGC
    //   DLSS5_MINUPD=X     MinUpdateInterval em ms (padrao 0 = sem teto).
    //                      Negativo = nao mexe, volta ao padrao do Windows (16 ms = 50 fps
    //                      num monitor de 100 Hz). So serve pra comparar no log.
    int    optDrain     = 1;
    int    optRectCache = 1;
    int    optPoolBuf   = 2;
    int    optNoCopy    = 0;
    double optMinUpdMs  = 0.0;

    // ---- helpers WinRT
    static HRESULT Factory(const wchar_t* cls, REFIID iid, void** out) {
        HSTRING_HEADER hdr; HSTRING hs = nullptr;
        HRESULT hr = WindowsCreateStringReference(cls, (UINT32)wcslen(cls), &hdr, &hs);
        if (FAILED(hr)) return hr;
        return RoGetActivationFactory(hs, iid, out);
    }

    // Tamanho "de conteudo" que queremos entregar pro renderer (o recorte).
    bool TargetRect(RECT* out, RECT* frameOrigin) {
        if (targetMon) {
            MONITORINFO mi{}; mi.cbSize = sizeof(mi);
            if (!GetMonitorInfoW(targetMon, &mi)) return false;
            *frameOrigin = mi.rcMonitor;
            *out = mi.rcMonitor;
            return true;
        }
        if (!targetWin || !IsWindow(targetWin)) return false;
        RECT fb{};
        if (FAILED(DwmGetWindowAttribute(targetWin, DWMWA_EXTENDED_FRAME_BOUNDS, &fb, sizeof(fb)))) {
            if (!GetWindowRect(targetWin, &fb)) return false;
        }
        *frameOrigin = fb;
        if (!clientOnly) { *out = fb; return true; }
        RECT cr{};
        if (!GetClientRect(targetWin, &cr)) { *out = fb; return true; }
        POINT tl{ cr.left, cr.top }, br{ cr.right, cr.bottom };
        ClientToScreen(targetWin, &tl); ClientToScreen(targetWin, &br);
        RECT c{ tl.x, tl.y, br.x, br.y };
        if (c.right - c.left < 16 || c.bottom - c.top < 16) { *out = fb; return true; }
        *out = c;
        return true;
    }

    // Mesma coisa, mas so paga o DwmGetWindowAttribute/ClientToScreen quando a janela
    // realmente se mexeu. GetWindowRect e local (nao vai ao DWM), entao e barato.
    bool TargetRectCached(RECT* out, RECT* frameOrigin) {
        if (!optRectCache) return TargetRect(out, frameOrigin);
        if (targetMon) {
            if (!cacheOk) { if (!TargetRect(&cacheWant, &cacheOrg)) return false; cacheOk = true; }
            *out = cacheWant; *frameOrigin = cacheOrg; return true;
        }
        RECT wr{};
        if (!targetWin || !GetWindowRect(targetWin, &wr)) return false;
        if (!cacheOk || memcmp(&wr, &lastWinRect, sizeof(RECT)) != 0) {
            if (!TargetRect(&cacheWant, &cacheOrg)) return false;
            lastWinRect = wr; cacheOk = true;
        }
        *out = cacheWant; *frameOrigin = cacheOrg;
        return true;
    }

    bool EnsureTex(UINT nw, UINT nh) {
        // O formato entra na comparacao porque hdrFp16 pode mudar entre dois Start()
        // (o monitor do alvo entrou ou saiu de HDR). Com so o tamanho na conta, a
        // textura antiga sobreviveria em BGRA8 enquanto o pool ja entrega FP16, e o
        // CopySubresourceRegion falharia calado - imagem parada e nenhum erro.
        const DXGI_FORMAT want = TexFormat();
        if (tex && nw == w && nh == h && texFmt == want) return true;
        if (srv) { srv->Release(); srv = nullptr; }
        if (tex) { tex->Release(); tex = nullptr; }
        texFmt = DXGI_FORMAT_UNKNOWN;
        w = nw; h = nh;
        if (!w || !h) return false;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = want; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &tex))) return false;
        if (FAILED(dev->CreateShaderResourceView(tex, nullptr, &srv))) {
            tex->Release(); tex = nullptr;   // sem SRV a textura nao serve pra nada
            return false;
        }
        texFmt = want;
        return true;
    }

    bool RecreatePool(UINT pw, UINT ph) {
        if (!pool) return false;
        WGC::WG::SizeInt32 sz{ (INT32)pw, (INT32)ph };
        if (FAILED(pool->Recreate(rtDev, PoolFormat(), optPoolBuf, sz))) return false;
        poolW = pw; poolH = ph;
        cacheOk = false;   // o alvo mudou de tamanho: a geometria cacheada morreu
        return true;
    }

    // ---- ciclo de vida
    bool Start(ID3D11Device* d3d, HWND win, HMONITOR mon, bool clientArea, bool cursor,
               bool border, std::wstring* err) {
        dev = d3d; targetWin = win; targetMon = mon; clientOnly = clientArea;

        // Recomeco limpo - e isto que faz o Stop()+Start() da recriacao funcionar. Sem
        // zerar 'closed' a captura recriada ja nasce marcada como morta e o laco de
        // retomada do main.cpp queima as 10 tentativas sem nunca sair do lugar. Se
        // sobrou alguma coisa de uma sessao anterior (um Start sem Stop antes), ela sai
        // aqui; o 'evt' fica, porque o loop principal ja pode estar esperando nele.
        if (started || pool || session || item || rtDev) Teardown(true);
        closed.store(false, std::memory_order_relaxed);
        frames.store(0, std::memory_order_relaxed);
        arrived.store(0, std::memory_order_relaxed);
        arriveQpc.store(0, std::memory_order_relaxed);
        cacheOk = false; poolW = poolH = 0; lastSrt100ns = 0;
        lastWinRect = RECT{};

        // O evento e criado ANTES de qualquer caminho de erro. Se o Start falhar la na
        // frente (aconteceu: alvo em tela cheia exclusiva), o main.cpp continua com um
        // handle valido para esperar; com evt nulo ou ja fechado, o
        // MsgWaitForMultipleObjects devolve WAIT_FAILED na hora e o loop vira busy-loop
        // de 100% de um nucleo.
        if (!evt) evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        // Daqui pra frente, qualquer 'return false' desfaz o que ja foi criado (menos o
        // 'evt'). Antes, cada tentativa de recriacao vazava pool, handler e device WinRT
        // e deixava a struct montada pela metade para o Tick seguinte tropecar.
        struct Desfaz {
            ScreenCapture* s;
            bool armado = true;
            ~Desfaz() { if (armado) s->Teardown(true); }
        } desfaz{ this, true };

        {   char b[16];
            if (GetEnvironmentVariableA("DLSS5_DRAIN",     b, 16)) optDrain     = atoi(b);
            if (GetEnvironmentVariableA("DLSS5_RECTCACHE", b, 16)) optRectCache = atoi(b);
            if (GetEnvironmentVariableA("DLSS5_NOCOPY",    b, 16)) optNoCopy    = atoi(b);
            if (GetEnvironmentVariableA("DLSS5_POOLBUF",   b, 16)) { optPoolBuf = atoi(b); if (optPoolBuf < 1) optPoolBuf = 1; }
            if (GetEnvironmentVariableA("DLSS5_MINUPD",    b, 16)) optMinUpdMs  = atof(b);
            if (GetEnvironmentVariableA("DLSS5_FP16",      b, 16)) hdrFp16Mode  = atoi(b);
        }

        // ---- formato da captura, decidido ANTES de criar o pool (nao da pra trocar depois)
        {
            HMONITOR m = targetMon ? targetMon
                       : (targetWin ? MonitorFromWindow(targetWin, MONITOR_DEFAULTTONEAREST) : nullptr);
            Hdr::DxgiInfo d = Hdr::QueryDxgi(m);
            const bool hdrOn = d.ok && d.hdr;
            hdrFp16 = (hdrFp16Mode < 0) ? hdrOn : (hdrFp16Mode == 1);

            // O paper white real do monitor e o que o shader usa para normalizar. Sem ele
            // a conversao fica 3x errada nesta maquina, onde o usuario poe o brilho SDR
            // em 240 nits em vez dos 80 de referencia.
            auto ts = Hdr::TargetsForMonitor(m);
            if (!ts.empty()) {
                Hdr::Info i = Hdr::Query(ts[0]);
                if (i.ok && i.sdrWhiteNits > 0.f) hdrSdrWhiteNits = i.sdrWhiteNits;
            }
            Log("captura: monitor %s -> pool %s (paper white %.0f nits)%s",
                hdrOn ? "em HDR" : "em SDR",
                hdrFp16 ? "R16G16B16A16_FLOAT (scRGB linear)" : "B8G8R8A8_UNORM",
                hdrSdrWhiteNits,
                hdrFp16Mode < 0 ? "" : " [forcado por DLSS5_FP16]");
        }

        HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
            if (err) *err = T(L"RoInitialize falhou (WinRT indisponivel).", L"RoInitialize failed (WinRT unavailable).");
            return false;
        }

        // device WinRT em cima do nosso device D3D11 -> textura sem copia entre devices
        IDXGIDevice* dxgi = nullptr;
        if (FAILED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi))) {
            if (err) *err = T(L"QueryInterface(IDXGIDevice) falhou.", L"QueryInterface(IDXGIDevice) failed.");
            return false;
        }
        IInspectable* insp = nullptr;
        hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi, &insp);
        dxgi->Release();
        if (FAILED(hr) || !insp) { if (err) *err = T(L"CreateDirect3D11DeviceFromDXGIDevice falhou.", L"CreateDirect3D11DeviceFromDXGIDevice failed."); return false; }
        hr = insp->QueryInterface(__uuidof(WGC::WGD3::IDirect3DDevice), (void**)&rtDev);
        insp->Release();
        if (FAILED(hr)) { if (err) *err = T(L"IDirect3DDevice indisponivel.", L"IDirect3DDevice unavailable."); return false; }

        // item: janela ou monitor
        IGraphicsCaptureItemInterop* interop = nullptr;
        hr = Factory(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem,
                     __uuidof(IGraphicsCaptureItemInterop), (void**)&interop);
        if (FAILED(hr) || !interop) {
            if (err) *err = T(L"Windows Graphics Capture indisponivel nesse Windows (precisa 10 1903+).", L"Windows Graphics Capture is unavailable on this Windows (needs 10 1903+).");
            return false;
        }
        if (targetWin) hr = interop->CreateForWindow(targetWin, __uuidof(WGC::WGCa::IGraphicsCaptureItem), (void**)&item);
        else           hr = interop->CreateForMonitor(targetMon, __uuidof(WGC::WGCa::IGraphicsCaptureItem), (void**)&item);
        interop->Release();
        if (FAILED(hr) || !item) {
            if (err) *err = targetWin ? T(L"Nao consegui capturar essa janela (ela fechou? e protegida?).", L"Could not capture that window (did it close? is it protected?).")
                                      : T(L"Nao consegui capturar esse monitor.", L"Could not capture that monitor.");
            return false;
        }

        // pool no tamanho do item
        WGC::WG::SizeInt32 isz{};
        item->get_Size(&isz);
        if (isz.Width <= 0 || isz.Height <= 0) { if (err) *err = T(L"Tamanho do alvo invalido.", L"Invalid target size."); return false; }

        WGC::WGCa::IDirect3D11CaptureFramePoolStatics2* st = nullptr;
        hr = Factory(RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool,
                     __uuidof(WGC::WGCa::IDirect3D11CaptureFramePoolStatics2), (void**)&st);
        if (FAILED(hr) || !st) { if (err) *err = T(L"Direct3D11CaptureFramePool indisponivel.", L"Direct3D11CaptureFramePool unavailable."); return false; }
        hr = st->CreateFreeThreaded(rtDev, PoolFormat(), optPoolBuf, isz, &pool);
        st->Release();
        if (FAILED(hr) || !pool) { if (err) *err = T(L"CreateFreeThreaded falhou.", L"CreateFreeThreaded failed."); return false; }
        poolW = (UINT)isz.Width; poolH = (UINT)isz.Height;

        // contexto imediato pego uma vez so: GetImmediateContext por frame custa um
        // AddRef/Release e um lock interno no caminho critico, sem motivo nenhum.
        dev->GetImmediateContext(&ictx);
        if (!ictx) { if (err) *err = T(L"GetImmediateContext falhou.", L"GetImmediateContext failed."); return false; }

        handler = new WgcFrameHandler();
        handler->evt = evt; handler->counter = &arrived; handler->arriveQpc = &arriveQpc;
        hr = pool->add_FrameArrived(handler, &token);
        if (FAILED(hr)) { if (err) *err = T(L"add_FrameArrived falhou.", L"add_FrameArrived failed."); return false; }

        // Aviso de queda da sessao. Nao e fatal se falhar: o app so perde a deteccao
        // rapida e volta a depender da janela morrer.
        closedHandler = new WgcClosedHandler();
        closedHandler->flag = &closed; closedHandler->evt = evt;
        if (FAILED(item->add_Closed(closedHandler, &closedToken))) {
            Log("captura: add_Closed falhou - a queda da sessao so sera vista se a janela fechar");
            closedHandler->Release(); closedHandler = nullptr;
        }

        hr = pool->CreateCaptureSession(item, &session);
        if (FAILED(hr) || !session) { if (err) *err = T(L"CreateCaptureSession falhou.", L"CreateCaptureSession failed."); return false; }

        // cursor e a borda amarela de "esta sendo capturado" (Win11) sao opcionais
        WGC::WGCa::IGraphicsCaptureSession2* s2 = nullptr;
        if (SUCCEEDED(session->QueryInterface(__uuidof(WGC::WGCa::IGraphicsCaptureSession2), (void**)&s2)) && s2) {
            s2->put_IsCursorCaptureEnabled(cursor ? 1 : 0); s2->Release();
        }
        WGC::WGCa::IGraphicsCaptureSession3* s3 = nullptr;
        if (SUCCEEDED(session->QueryInterface(__uuidof(WGC::WGCa::IGraphicsCaptureSession3), (void**)&s3)) && s3) {
            s3->put_IsBorderRequired(border ? 1 : 0); s3->Release();
        }

        // Tira o teto de atualizacao do WGC (ver o comentario da IWgcSession5 la em cima).
        // Sem isso o Windows entrega 1 frame a cada 16 ms no minimo, o que num monitor de
        // 100 Hz vira 1 a cada 2 vblanks = 50,0 fps, independente do fps do jogo.
        {
            IWgcSession5* s5 = nullptr;
            if (SUCCEEDED(session->QueryInterface(IID_WgcSession5, (void**)&s5)) && s5) {
                WGC::WF::TimeSpan cur{};
                s5->get_MinUpdateInterval(&cur);
                if (optMinUpdMs >= 0.0) {
                    WGC::WF::TimeSpan nv{ (INT64)(optMinUpdMs * 10000.0) };
                    HRESULT hr5 = s5->put_MinUpdateInterval(nv);
                    WGC::WF::TimeSpan got{};
                    s5->get_MinUpdateInterval(&got);
                    Log("captura: MinUpdateInterval %.2f ms -> %.2f ms (pedido %.2f, hr=0x%08lX)",
                        (double)cur.Duration / 10000.0, (double)got.Duration / 10000.0,
                        optMinUpdMs, (unsigned long)hr5);
                } else {
                    Log("captura: MinUpdateInterval mantido no padrao do Windows (%.2f ms) - DLSS5_MINUPD<0",
                        (double)cur.Duration / 10000.0);
                }
                s5->Release();
            } else {
                Log("captura: sem IGraphicsCaptureSession5 neste Windows - o sistema impoe"
                    " no minimo 16 ms entre frames (teto de 50 fps em monitor de 100 Hz)");
            }
        }

        // primeira geometria (pode mudar a qualquer momento se a janela for redimensionada)
        RECT want{}, org{};
        if (!TargetRect(&want, &org)) { if (err) *err = T(L"Nao consegui medir o alvo.", L"Could not measure the target."); return false; }
        if (!EnsureTex((UINT)(want.right - want.left), (UINT)(want.bottom - want.top))) {
            if (err) *err = T(L"Falha ao criar a textura do recorte.", L"Failed to create the crop texture.");
            return false;
        }

        hr = session->StartCapture();
        if (FAILED(hr)) {
            if (err) *err = TS(L"StartCapture falhou ", L"StartCapture failed ") + T(L"(a captura de tela esta bloqueada por politica?)", L"(is screen capture blocked by policy?)");
            return false;
        }
        Log("captura: buffers=%d so-o-frame-mais-novo=%d geometria-cacheada=%d",
            optPoolBuf, optDrain, optRectCache);
        started = true;
        desfaz.armado = false;   // subiu inteiro: nada a desfazer
        return true;
    }

    // Sessao e pool do WGC sao IClosable: o Release sozinho nao promete que a captura
    // parou agora, o Close promete. Importa no Stop()+Start() da recriacao, para a
    // sessao velha nao ficar entregando frames enquanto a nova sobe.
    template <class T> static void CloseRelease(T*& p) {
        if (!p) return;
        WGC::WF::IClosable* cl = nullptr;
        if (SUCCEEDED(p->QueryInterface(__uuidof(WGC::WF::IClosable), (void**)&cl)) && cl) {
            cl->Close(); cl->Release();
        }
        p->Release(); p = nullptr;
    }

    static void CloseFrame(WGC::WGCa::IDirect3D11CaptureFrame* f) {
        if (!f) return;
        WGC::WF::IClosable* cl = nullptr;
        if (SUCCEEDED(f->QueryInterface(__uuidof(WGC::WF::IClosable), (void**)&cl)) && cl) { cl->Close(); cl->Release(); }
        f->Release();
    }

    // Recorta UM frame pra tex. Devolve true se copiou.
    bool CopyFrame(WGC::WGCa::IDirect3D11CaptureFrame* f) {
        WGC::WF::TimeSpan ts{};
        lastSrt100ns = SUCCEEDED(f->get_SystemRelativeTime(&ts)) ? ts.Duration : 0;

        WGC::WG::SizeInt32 cs{}; f->get_ContentSize(&cs);
        WGC::WGD3::IDirect3DSurface* surf = nullptr;
        ID3D11Texture2D* src = nullptr;
        if (SUCCEEDED(f->get_Surface(&surf)) && surf) {
            Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess* acc = nullptr;
            if (SUCCEEDED(surf->QueryInterface(
                    __uuidof(Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess), (void**)&acc)) && acc) {
                acc->GetInterface(__uuidof(ID3D11Texture2D), (void**)&src);
                acc->Release();
            }
            surf->Release();
        }
        if (!src) return false;

        bool copied = false;
        D3D11_TEXTURE2D_DESC sd{}; src->GetDesc(&sd);
        RECT want{}, org{};
        if (TargetRectCached(&want, &org)) {
            // offset do recorte dentro do frame entregue pelo WGC
            LONG ox = want.left - org.left, oy = want.top - org.top;
            LONG cw = want.right - want.left, chh = want.bottom - want.top;
            LONG maxW = (LONG)((cs.Width > 0 && (UINT)cs.Width <= sd.Width) ? cs.Width : (INT32)sd.Width);
            LONG maxH = (LONG)((cs.Height > 0 && (UINT)cs.Height <= sd.Height) ? cs.Height : (INT32)sd.Height);
            if (ox < 0) { cw += ox; ox = 0; }
            if (oy < 0) { chh += oy; oy = 0; }
            if (ox + cw > maxW) cw = maxW - ox;
            if (oy + chh > maxH) chh = maxH - oy;
            if (cw > 0 && chh > 0 && EnsureTex((UINT)cw, (UINT)chh)) {
                D3D11_BOX box{ (UINT)ox, (UINT)oy, 0, (UINT)(ox + cw), (UINT)(oy + chh), 1 };
                ictx->CopySubresourceRegion(tex, 0, 0, 0, 0, src, 0, &box);
                copied = true;
            }
        }
        src->Release();

        // alvo mudou de tamanho -> o pool precisa nascer de novo no tamanho novo
        if (cs.Width > 0 && cs.Height > 0 && ((UINT)cs.Width != poolW || (UINT)cs.Height != poolH))
            RecreatePool((UINT)cs.Width, (UINT)cs.Height);
        return copied;
    }

    // Roda na thread do render: pega o frame mais novo (se houver) e recorta pra tex.
    // Devolve true quando copiou algo novo. Atualiza w/h quando o alvo muda de tamanho.
    bool Tick() {
        if (!pool || !started || !ictx) return false;
        LONGLONG t2 = g_lat.Now();
        bool copied = false;

        if (optNoCopy) {   // diagnostico: mede o ritmo do WGC sem nenhum trabalho nosso
            for (;;) {
                WGC::WGCa::IDirect3D11CaptureFrame* f = nullptr;
                if (FAILED(pool->TryGetNextFrame(&f)) || !f) break;
                CloseFrame(f);
            }
            return false;
        }

        if (optDrain) {
            // Drena a fila mas so COPIA o frame mais novo. Como o Present e 1 por Tick,
            // copiar os antigos e banda de GPU jogada fora e ainda atrasa o Present -
            // e o unico que aparece na tela e o ultimo mesmo.
            WGC::WGCa::IDirect3D11CaptureFrame* keep = nullptr;
            for (;;) {
                WGC::WGCa::IDirect3D11CaptureFrame* f = nullptr;
                if (FAILED(pool->TryGetNextFrame(&f)) || !f) break;
                if (keep) CloseFrame(keep);   // devolve o antigo sem copiar
                keep = f;
            }
            if (keep) { copied = CopyFrame(keep); CloseFrame(keep); }
        } else {
            for (;;) {
                WGC::WGCa::IDirect3D11CaptureFrame* f = nullptr;
                if (FAILED(pool->TryGetNextFrame(&f)) || !f) break;
                if (CopyFrame(f)) copied = true;
                CloseFrame(f);
            }
        }

        if (copied) {
            frames++;
            g_lat.nTick++;
            LONGLONG t1 = arriveQpc.load(std::memory_order_relaxed);
            g_lat.curSrt = lastSrt100ns;
            if (t1 && lastSrt100ns) g_lat.srt.add((double)(g_lat.QpcTo100ns(t1) - lastSrt100ns) / 10000.0);
            if (t1) g_lat.wake.add(g_lat.Ms(t2 - t1));
            g_lat.tick.add(g_lat.Ms(g_lat.Now() - t2));
            // se o loop principal nao estiver alimentando o probe pelo Present,
            // o relatorio sai por aqui mesmo (so sem os tempos de apresentacao)
            if (!g_lat.fedByPresent && ++g_lat.n >= (UINT32)g_lat.every) { g_lat.Dump(); g_lat.Reset(); }
        }
        return copied;
    }

    bool Alive() const {
        return !closed.load(std::memory_order_relaxed) && (!targetWin || IsWindow(targetWin));
    }
    // true = foi o WGC que encerrou a sessao (o item avisou pelo Closed), nao a janela
    // que morreu. Os dois derrubam a captura, mas so o primeiro tem conserto: a condicao
    // de recriacao certa e SessionClosed() && IsWindow(alvo).
    bool SessaoCaiu() const { return closed.load(std::memory_order_relaxed); }
    bool SessionClosed() const { return SessaoCaiu(); }   // mesmo predicado, nome em ingles
    // Só para teste (DLSS5_TESTA_QUEDA): finge que o WGC encerrou a sessao com a janela
    // viva. E o unico jeito de exercitar o caminho de recriacao sem um jogo de verdade
    // trocando de resolucao no meio da medicao.
    void FingirQueda() { closed.store(true, std::memory_order_relaxed); }

    // Desmonta tudo o que o Start montou, em ordem: primeiro cala a fonte de eventos
    // (Close da sessao e do pool + remove dos handlers), depois desarma os handlers e
    // espera quem esta dentro do Invoke sair, e so entao solta os objetos e o evento.
    //
    // manterEvt=true e o modo usado quando o Start falha no meio: o main.cpp espera
    // nesse HANDLE e, com ele fechado, MsgWaitForMultipleObjects devolve WAIT_FAILED
    // na hora - o loop principal vira busy-wait de 100% de um nucleo.
    void Teardown(bool manterEvt) {
        started = false;
        if (session) { CloseRelease(session); }
        if (pool) {
            if (handler) pool->remove_FrameArrived(token);
            CloseRelease(pool);
        }
        if (item && closedHandler) item->remove_Closed(closedToken);
        if (handler)       { handler->Desarmar();       handler->Release();       handler = nullptr; }
        if (closedHandler) { closedHandler->Desarmar(); closedHandler->Release(); closedHandler = nullptr; }
        token = EventRegistrationToken{};
        closedToken = EventRegistrationToken{};
        if (item)  { item->Release();  item = nullptr; }
        if (rtDev) { rtDev->Release(); rtDev = nullptr; }
        if (ictx)  { ictx->Release();  ictx = nullptr; }
        if (srv)   { srv->Release();   srv = nullptr; }
        if (tex)   { tex->Release();   tex = nullptr; }
        texFmt = DXGI_FORMAT_UNKNOWN;
        w = h = 0; poolW = poolH = 0;
        cacheOk = false;
        if (!manterEvt && evt) { CloseHandle(evt); evt = nullptr; }
    }

    void Stop() { Teardown(false); }
};
