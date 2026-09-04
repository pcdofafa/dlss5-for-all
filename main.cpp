// =====================================================================================
//  DLSS5 For All (ex-DLSS5 For All)
//  Janela Direct3D 11 minima que mostra uma placa de captura / webcam.
//  Existe por UM motivo: ser um alvo limpo e previsivel pro ReShade (add-on build)
//  + DLSS5-Feeder + LumeniteFX + renodx-dlss5, que enganchamos por cima dela.
//
//  O que ela faz de proposito:
//   - 1 swapchain so (nada de preview + projetor + docks como no OBS)
//   - backbuffer SEMPRE na resolucao da captura (o Feeder roda em DLAA: render = output)
//   - desenha o frame com draw call real, com depth buffer real (z = 0.5) pra
//     o Generic Depth do ReShade e o LumeniteFX Kernel terem depth valido
//   - 1 Present por frame capturado (motion vectors estimados gostam disso)
//
//  Teclas: End abre o painel | F6 liga/desliga tudo | F11 tela cheia sem borda.
//  Esc NAO fecha o app (em jogo aperta-se Esc o tempo todo); sair e pelo painel.
//  Linha de comando: --device N --mode M --fullscreen --list
//  Log: dlss5forall.log ao lado do exe. Ultima escolha salva em dlss5forall.ini.
// =====================================================================================
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>         // so para descobrir o PID da outra instancia (ver InstanciaUnica)
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <deque>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cstring>
// GDI+ (painel na tela: cantos redondos, antialias, texto bonito).
// NOMINMAX tira min/max do windows.h, mas os headers do GDI+ usam os dois -
// entao trazemos as versoes da STL pro escopo global antes de incluir.
#include <objidl.h>
using std::min;
using std::max;
#include <gdiplus.h>
#include "idioma.h"           // T(pt, en): portugues / ingles no ponto de uso
#include "screen_capture.h"   // modulo de captura de janela / monitor (WGC)
#include "reshade_bridge.h"   // ponte com o add-on: troca LUT / uniforms do DLSS5_Grade
#include "motion_estimate.h"  // motion vectors pelo bloco de video da GPU (D3D12 VME)
// Modulos de outros agentes: entram por __has_include para o build fechar sem eles.
// Precisam vir aqui em cima porque o seletor (bem antes do painel) ja usa os textos.
#if defined(__has_include)
#  if __has_include("hdr_control.h")
#    define TEM_HDR_CONTROL 1
#  endif
#  if __has_include("splash_ofertas.h")
#    define TEM_SPLASH_OFERTAS 1
#  endif
#endif
#ifdef TEM_HDR_CONTROL
#include "hdr_control.h"      // deteccao e tratamento de HDR (display e placa de captura)
static Hdr::Treatment g_hdrTr;
#endif
#ifdef TEM_SPLASH_OFERTAS
#include "splash_ofertas.h"   // tela do convite do grupo (QR + botao)
#endif

#define APP_TITLE   L"DLSS5 For All"
#define APP_VERSION L"1.1"
static const int GRID_X = 32;   // grade de quads desenhada (da vertices de sobra pro Generic Depth)
static const int GRID_Y = 18;

// ------------------------------------------------------------------ icone do app
// O icone vem do recurso 1 (app.rc, compilado por windres no build-mingw.sh) e tem NOVE
// tamanhos desenhados um a um (16 a 256, ver marca\gerar-icone.ps1).
//
// LoadIcon() nao serve aqui, e essa e a razao de existir esta funcao: ela devolve sempre
// o tamanho "grande" do sistema (tipicamente 32px) e deixa o Windows encolher na hora de
// desenhar. Numa bandeja de 16px isso borra o desenho de 32 em vez de usar o de 16, que
// esta ali do lado dentro do mesmo .ico. LoadImageW com largura e altura explicitas
// escolhe a IMAGEM certa - que e o motivo de ter gerado nove.
//
// LR_SHARED: o Windows cuida do tempo de vida do handle de recurso, e chamar de novo com
// o mesmo tamanho devolve o mesmo handle. Nao se destroi um icone carregado assim.
static HICON IconeApp(int px) {
    return (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1),
                             IMAGE_ICON, px, px, LR_DEFAULTCOLOR | LR_SHARED);
}

template <class T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// ------------------------------------------------------------------ log / erros
static FILE* g_log = nullptr;
static std::wstring g_exeDir;
// Onde NOS gravamos: dlss5forall.ini e dlss5forall.log. Normalmente e o proprio g_exeDir.
// Quando a pasta do exe nao aceita escrita, vira %LOCALAPPDATA%\DLSS5ForAll\ - ver
// EscolherPastaDeDados(). Tudo o que e do ReShade (ReShade.ini, presets, dlss5-feed.log,
// os .addon64) continua saindo de g_exeDir, porque quem le esses arquivos e ele.
static std::wstring g_dadosDir;
static bool g_pastaDoExeSemEscrita = false;

static std::string U8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}
static void RestoreHdrOnExit();   // definido junto do bloco de HDR
// A janela da bandeja, para o Fatal poder tirar o icone antes de matar o processo.
static HWND g_trayWnd = nullptr;
static void TrayRemove(HWND h);

static void Fatal(const std::wstring& msg) {
    // A UNICA linha de log que sai no idioma da interface, e de proposito: aqui o log e a
    // copia exata do que a pessoa leu na caixa antes de o app morrer, e e isso que serve
    // ao suporte. Todo o resto do dlss5forall.log continua em portugues, sempre.
    Log("FATAL: %s", U8(msg).c_str());
    RestoreHdrOnExit();   // ExitProcess nao roda destrutor nem atexit
    // ...e tambem nao tira o icone da bandeja: ele fica de fantasma ao lado do relogio ate
    // alguem passar o mouse por cima. Um Fatal dentro do laco (falha ao recriar a textura
    // da camera, por exemplo) deixava exatamente esse rastro.
    if (g_trayWnd) { TrayRemove(g_trayWnd); g_trayWnd = nullptr; }
    MessageBoxW(nullptr, msg.c_str(),
                (std::wstring(APP_TITLE) + T(L" - erro", L" - error")).c_str(), MB_ICONERROR | MB_OK);
    ExitProcess(1);
}
static std::wstring Hex(HRESULT hr) { wchar_t b[24]; _snwprintf(b, 24, L"0x%08X", (unsigned)hr); return b; }

// os argumentos da linha de comando SEM o nome do exe - para relancar o app igualzinho
static std::wstring GetCommandLineArgsOnly() {
    const wchar_t* c = GetCommandLineW();
    if (!c) return L"";
    std::wstring s = c;
    size_t i = 0;
    if (!s.empty() && s[0] == L'"') { i = s.find(L'"', 1); i = (i == std::wstring::npos) ? s.size() : i + 1; }
    else { i = s.find(L' '); if (i == std::wstring::npos) i = s.size(); }
    while (i < s.size() && s[i] == L' ') i++;
    return s.substr(i);
}

// ------------------------------------------------------------------ Media Foundation
struct Mode {
    UINT32 w = 0, h = 0, fn = 0, fd = 1;
    GUID   sub{};
    bool   compressed = false;
    DWORD  nativeIndex = 0;
    std::wstring label;
    double fps() const { return fd ? (double)fn / fd : 0.0; }
};
struct Device {
    std::wstring name;
    IMFActivate* act = nullptr;
    std::vector<Mode> modes;   // preenchido sob demanda
    bool modesLoaded = false;
};

static std::wstring SubtypeName(const GUID& g, bool* compressed) {
    *compressed = false;
    if (g == MFVideoFormat_MJPG)   { *compressed = true; return L"MJPG"; }
    if (g == MFVideoFormat_H264)   { *compressed = true; return L"H264"; }
    if (g == MFVideoFormat_RGB32)  return L"RGB32";
    if (g == MFVideoFormat_ARGB32) return L"ARGB32";
    if (g == MFVideoFormat_RGB24)  return L"RGB24";
    wchar_t fcc[5]; DWORD d = g.Data1; bool printable = true;
    for (int i = 0; i < 4; i++) { wchar_t c = (wchar_t)((d >> (8 * i)) & 0xFF); if (c < 32 || c > 126) printable = false; fcc[i] = c; }
    fcc[4] = 0;
    if (printable) return fcc;
    wchar_t b[16]; _snwprintf(b, 16, L"%08X", (unsigned)d); return b;
}

static std::vector<Device> EnumDevices() {
    std::vector<Device> out;
    IMFAttributes* attr = nullptr;
    if (FAILED(MFCreateAttributes(&attr, 1))) return out;
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** acts = nullptr; UINT32 n = 0;
    HRESULT hr = MFEnumDeviceSources(attr, &acts, &n);
    Log("MFEnumDeviceSources: hr=0x%08X, %u dispositivo(s)", (unsigned)hr, n);
    if (SUCCEEDED(hr)) {
        for (UINT32 i = 0; i < n; i++) {
            WCHAR* name = nullptr; UINT32 len = 0;
            acts[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len);
            Device d; d.name = name ? name : T(L"(sem nome)", L"(unnamed)"); d.act = acts[i];
            Log("  [%u] %s", i, U8(d.name).c_str());
            out.push_back(d);
            if (name) CoTaskMemFree(name);
        }
        CoTaskMemFree(acts);
    }
    attr->Release();
    return out;
}

static IMFSourceReader* OpenReader(IMFActivate* act, IMFMediaSource** src, std::wstring* err) {
    *src = nullptr;
    HRESULT hr = act->ActivateObject(IID_IMFMediaSource, (void**)src);
    if (FAILED(hr)) { if (err) *err = TS(L"ActivateObject falhou ", L"ActivateObject failed ") + Hex(hr) + T(L" (dispositivo em uso por outro programa?)", L" (device in use by another program?)"); Log("ActivateObject hr=0x%08X", (unsigned)hr); return nullptr; }
    IMFAttributes* a = nullptr; MFCreateAttributes(&a, 2);
    // deixa o Source Reader inserir decoder (MJPEG) + video processor (YUY2/NV12 -> RGB32)
    a->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    IMFSourceReader* r = nullptr;
    hr = MFCreateSourceReaderFromMediaSource(*src, a, &r);
    SafeRelease(a);
    if (FAILED(hr)) { if (err) *err = TS(L"MFCreateSourceReaderFromMediaSource falhou ", L"MFCreateSourceReaderFromMediaSource failed ") + Hex(hr); Log("CreateSourceReader hr=0x%08X", (unsigned)hr); (*src)->Shutdown(); SafeRelease(*src); return nullptr; }
    return r;
}

static std::vector<Mode> EnumModes(IMFSourceReader* r) {
    std::vector<Mode> v;
    for (DWORD i = 0;; i++) {
        IMFMediaType* t = nullptr;
        if (FAILED(r->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &t))) break;
        Mode m; m.nativeIndex = i;
        t->GetGUID(MF_MT_SUBTYPE, &m.sub);
        MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &m.w, &m.h);
        MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &m.fn, &m.fd);
        if (!m.fd) m.fd = 1;
        std::wstring sn = SubtypeName(m.sub, &m.compressed);
        wchar_t b[160];
        _snwprintf(b, 160, L"%ux%u @ %.2f fps   %ls%ls", m.w, m.h, m.fps(), sn.c_str(),
                   m.compressed ? T(L"  (comprimido)", L"  (compressed)") : L"");
        b[159] = 0;   // _snwprintf nao termina a string quando trunca
        m.label = b;
        v.push_back(m);
        t->Release();
    }
    return v;
}

// melhor modo: maior resolucao > maior fps > nao comprimido
static int BestMode(const std::vector<Mode>& v) {
    int best = -1;
    for (size_t i = 0; i < v.size(); i++) {
        if (best < 0) { best = (int)i; continue; }
        const Mode& a = v[i]; const Mode& b = v[best];
        unsigned long long pa = (unsigned long long)a.w * a.h, pb = (unsigned long long)b.w * b.h;
        if (pa != pb) { if (pa > pb) best = (int)i; continue; }
        if (std::abs(a.fps() - b.fps()) > 0.01) { if (a.fps() > b.fps()) best = (int)i; continue; }
        if (!a.compressed && b.compressed) best = (int)i;
    }
    return best;
}

static bool LoadModes(Device& d) {
    if (d.modesLoaded) return !d.modes.empty();
    std::wstring err; IMFMediaSource* src = nullptr;
    IMFSourceReader* r = OpenReader(d.act, &src, &err);
    if (r) {
        d.modes = EnumModes(r);
        r->Release();
    }
    if (src) { src->Shutdown(); src->Release(); }
    d.act->ShutdownObject();
    d.modesLoaded = true;
    Log("Modos de '%s': %u", U8(d.name).c_str(), (unsigned)d.modes.size());
    for (auto& m : d.modes) Log("    [%u] %s", m.nativeIndex, U8(m.label).c_str());
    return !d.modes.empty();
}

// ------------------------------------------------------------------ captura (thread)
struct Capture {
    IMFMediaSource*  src = nullptr;
    IMFSourceReader* reader = nullptr;
    std::thread      th;
    std::atomic<bool> running{ false };
    HANDLE           evt = nullptr;

    std::mutex        mtx;
    std::vector<BYTE> buf;          // BGRA top-down, w*4*h
    UINT32            w = 0, h = 0;
    LONG              stride = 0;   // MF_MT_DEFAULT_STRIDE (pode ser negativo = bottom-up)
    bool              fresh = false;
    std::atomic<UINT32> frames{ 0 };
    std::atomic<UINT32> errors{ 0 };

    bool QueryFormat(std::wstring* err) {
        IMFMediaType* cur = nullptr;
        HRESULT hr = reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur);
        if (FAILED(hr)) { if (err) *err = TS(L"GetCurrentMediaType falhou ", L"GetCurrentMediaType failed ") + Hex(hr); return false; }
        UINT32 nw = 0, nh = 0; MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &nw, &nh);
        UINT32 st = 0; LONG s = 0;
        if (SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &st))) s = (LONG)(INT32)st;
        GUID sub{}; cur->GetGUID(MF_MT_SUBTYPE, &sub);
        cur->Release();
        bool c; std::wstring sn = SubtypeName(sub, &c);
        Log("Formato de saida: %ux%u %s stride=%ld", nw, nh, U8(sn).c_str(), (long)s);
        std::lock_guard<std::mutex> lk(mtx);
        w = nw; h = nh; stride = s;
        buf.assign((size_t)w * h * 4, 0);
        fresh = false;
        return w > 0 && h > 0;
    }

    bool Start(IMFActivate* act, DWORD nativeIndex, std::wstring* err) {
        reader = OpenReader(act, &src, err);
        if (!reader) return false;
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

        IMFMediaType* nat = nullptr;
        HRESULT hr = reader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nativeIndex, &nat);
        if (SUCCEEDED(hr)) { hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, nat); nat->Release(); }
        if (FAILED(hr)) Log("SetCurrentMediaType(nativo %u) hr=0x%08X (seguindo com o padrao do dispositivo)", nativeIndex, (unsigned)hr);

        // pede RGB32 (BGRA) -- o reader monta decoder + conversor sozinho
        IMFMediaType* out = nullptr; MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, out);
        if (FAILED(hr)) {
            Log("RGB32 recusado (0x%08X), tentando ARGB32", (unsigned)hr);
            out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32);
            hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, out);
        }
        out->Release();
        if (FAILED(hr)) { if (err) *err = TS(L"O dispositivo nao aceitou saida RGB32/ARGB32 (",
                                             L"The device would not accept RGB32/ARGB32 output (") + Hex(hr) + L")."; return false; }
        if (!QueryFormat(err)) return false;

        evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        running = true;
        th = std::thread(&Capture::Loop, this);
        return true;
    }

    void Loop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Log("Thread de captura iniciada");
        UINT32 lastLogged = 0;
        while (running) {
            DWORD si = 0, fl = 0; LONGLONG ts = 0; IMFSample* s = nullptr;
            HRESULT hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &si, &fl, &ts, &s);
            if (FAILED(hr)) { Log("ReadSample hr=0x%08X", (unsigned)hr); errors++; if (!running) break; Sleep(50); if (errors > 200) break; continue; }
            if (fl & MF_SOURCE_READERF_ENDOFSTREAM) { Log("Fim de stream"); break; }
            if (fl & MF_SOURCE_READERF_ERROR) { Log("MF_SOURCE_READERF_ERROR"); errors++; }
            if (fl & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) { Log("Tipo de midia mudou (fonte trocou resolucao?)"); QueryFormat(nullptr); }
            if (!s) continue;

            IMFMediaBuffer* b = nullptr; bool copied = false;
            if (SUCCEEDED(s->ConvertToContiguousBuffer(&b))) {
                std::lock_guard<std::mutex> lk(mtx);
                const size_t rowBytes = (size_t)w * 4;
                if (buf.size() == rowBytes * h && h > 0) {
                    IMF2DBuffer* b2 = nullptr;
                    if (SUCCEEDED(b->QueryInterface(IID_IMF2DBuffer, (void**)&b2))) {
                        BYTE* sl0 = nullptr; LONG pitch = 0;
                        if (SUCCEEDED(b2->Lock2D(&sl0, &pitch))) {
                            // pitch negativo = bottom-up; sl0 sempre aponta pra linha de CIMA
                            for (UINT32 y = 0; y < h; y++) memcpy(&buf[y * rowBytes], sl0 + (LONG)y * pitch, rowBytes);
                            b2->Unlock2D();
                            fresh = true; copied = true;
                        }
                        b2->Release();
                    } else {
                        BYTE* d = nullptr; DWORD mx = 0, cur = 0;
                        if (SUCCEEDED(b->Lock(&d, &mx, &cur))) {
                            LONG st = stride ? stride : (LONG)rowBytes;
                            LONG ast = st < 0 ? -st : st;
                            if ((size_t)cur >= (size_t)ast * h) {
                                if (st < 0) for (UINT32 y = 0; y < h; y++) memcpy(&buf[y * rowBytes], d + (size_t)(h - 1 - y) * ast, rowBytes);
                                else        for (UINT32 y = 0; y < h; y++) memcpy(&buf[y * rowBytes], d + (size_t)y * ast, rowBytes);
                                fresh = true; copied = true;
                            }
                            b->Unlock();
                        }
                    }
                }
                b->Release();
            }
            s->Release();
            if (copied) { frames++; SetEvent(evt); }
            if (frames - lastLogged >= 600) { lastLogged = frames; Log("frames capturados: %u", (unsigned)frames); }
        }
        Log("Thread de captura encerrada (frames=%u erros=%u)", (unsigned)frames, (unsigned)errors);
        CoUninitialize();
    }

    // copia o ultimo frame se houver novo; devolve false se nada novo
    bool Grab(std::vector<BYTE>& out, UINT32& ow, UINT32& oh) {
        std::lock_guard<std::mutex> lk(mtx);
        if (!fresh) return false;
        out = buf; ow = w; oh = h; fresh = false;
        return true;
    }
    void Size(UINT32& ow, UINT32& oh) { std::lock_guard<std::mutex> lk(mtx); ow = w; oh = h; }

    bool threadViva = false;   // o Stop desistiu de esperar: ninguem pode destruir isto
    void Stop() {
        running = false;
        if (src) src->Shutdown();   // destrava um ReadSample pendente
        bool viva = false;          // a thread continua rodando depois do prazo?
        if (th.joinable()) {
            HANDLE hth = (HANDLE)th.native_handle();
            if (WaitForSingleObject(hth, 2000) == WAIT_OBJECT_0) th.join();
            else { th.detach(); viva = true; }
        }
        threadViva = viva;
        if (viva) {
            // A thread ficou presa dentro do ReadSample. Acontece de verdade com cabo ou
            // placa de captura ruim, e o Shutdown() acima nem sempre a destrava. Ela AINDA
            // usa reader, src e evt (SetEvent no fim de cada frame): soltar os tres aqui e
            // use-after-free na saida do app - crash na cara do usuario justamente quando
            // o hardware ja estava dando problema. Abandonamos os tres de proposito: o
            // processo esta encerrando, e um vazamento que dura milissegundos e
            // infinitamente melhor do que uma tela de erro.
            Log("captura: a thread nao terminou em 2 s (ReadSample travado) - reader/src/evt ficam com ela");
            reader = nullptr; src = nullptr; evt = nullptr;
            return;
        }
        SafeRelease(reader);
        SafeRelease(src);
        if (evt) { CloseHandle(evt); evt = nullptr; }
    }
};

// ------------------------------------------------------------------ D3D11
static const char* kShader = R"HLSL(
Texture2D    tex : register(t0);
SamplerState smp : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
// grade GRID_X x GRID_Y de quads gerada so pelo SV_VertexID (sem vertex buffer).
// z = 0.5 -> depth buffer plano, valido pro ReShade (Generic Depth) e pro LumeniteFX.
VSOut VS(uint id : SV_VertexID)
{
    uint quad = id / 6u;  uint v = id % 6u;
    uint qx = quad % GRIDX; uint qy = quad / GRIDX;
    float2 c = float2((v == 1u || v == 3u || v == 4u) ? 1.0 : 0.0, (v >= 2u && v != 3u) ? 1.0 : 0.0);
    float2 uv = (float2(qx, qy) + c) / float2(GRIDX, GRIDY);
    VSOut o; o.uv = uv;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0);
    return o;
}
// Conversao de HDR da fonte (placa de captura / console). Preenchido pelo app a partir
// do Hdr::ShaderParams. Com HdrMode == 0 o shader nao faz nada - e o caminho de sempre,
// entao quem nao usa HDR nao paga nada por isto.
cbuffer HdrCB : register(b0)
{
    int   HdrMode;      // 0 = nada, 1 = PQ/BT.2020 (HDR10), 2 = HLG/BT.2020, 3 = scRGB linear
    int   HdrFixMatrix; // 1 = desfazer a matriz YCbCr BT.709 aplicada a um sinal BT.2020
    float HdrRefWhite;  // nits que viram 1.0 na saida (BT.2408: 203)
    float HdrPeak;      // pico assumido do conteudo, para o roll-off (ex.: 1000)
    float HdrKnee;      // acima disto comeca a compressao (>= 1.0 = sem roll-off)
    float3 _hdrPad;     // (aqui morou um aviso desenhado na imagem - ver o fim do PS)
};

// Quando a fonte e o backbuffer tem tamanhos diferentes, este PS deixa de ser uma copia
// 1:1 e vira uma AMPLIACAO (fonte menor) ou uma REDUCAO (fonte maior). Acontece depois de
// um FonteMudou: o alvo trocou de tamanho e o backbuffer nao pode ser recriado com a
// cadeia NGX viva, entao a conta fica aqui.
//
// O sampler linear do D3D serve para 1:1 mas borra na ampliacao, e e justamente a nitidez
// que a cadeia (Kernel -> Feed -> DLAA -> Neural) vai usar como entrada. Catmull-Rom
// resolve: e interpolante (passa exatamente pelos texels da fonte, entao 1:1 continua
// identico ao de sempre) e tem lobo negativo, que devolve o contraste de borda que o
// bilinear come.
cbuffer ScaleCB : register(b1)
{
    float2 SrcSize;     // tamanho da textura de origem, em texels
    float2 SrcSizeInv;  // 1 / SrcSize
    float2 Ratio;       // SrcSize / tamanho do backbuffer (>1 quando reduz)
    int    UpFilter;    // 0 = 1:1  1 = Catmull-Rom  2 = direcional+realce  3 = area
    float  Sharp;       // realce do modo 2 (0 = so a ampliacao)
};

// Catmull-Rom em funcao da distancia (usado pelo ampliador direcional, que amostra em
// posicoes que nao caem na grade separavel).
float CubicKernel(float x)
{
    x = abs(x);
    if (x < 1.0) return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    if (x < 2.0) return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    return 0.0;
}

// Catmull-Rom separavel em 9 amostras: as 4x4 posicoes viram 3x3 buscas bilineares
// deslocadas, aproveitando o proprio filtro do sampler para somar dois texels por
// tap. Peso negativo nas pontas -> pode passar do intervalo, o clamp em 0 fica por
// conta de quem chama.
float3 SampleCatmullRom(float2 uv)
{
    float2 p  = uv * SrcSize;
    float2 c1 = floor(p - 0.5) + 0.5;      // centro do texel de baixo do par central
    float2 f  = p - c1;                    // fracao dentro do intervalo [c1, c1+1]

    // pesos cubicos de Catmull-Rom (B = 0, C = 1/2)
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);

    float2 w12 = w1 + w2;
    float2 o12 = w2 / max(w12, 1e-5);      // onde amostrar para o sampler somar w1 e w2

    float2 t0  = (c1 - 1.0) * SrcSizeInv;
    float2 t3  = (c1 + 2.0) * SrcSizeInv;
    float2 t12 = (c1 + o12) * SrcSizeInv;

    float3 r = 0.0;
    r += tex.SampleLevel(smp, float2(t0.x,  t0.y ), 0).rgb * (w0.x  * w0.y );
    r += tex.SampleLevel(smp, float2(t12.x, t0.y ), 0).rgb * (w12.x * w0.y );
    r += tex.SampleLevel(smp, float2(t3.x,  t0.y ), 0).rgb * (w3.x  * w0.y );
    r += tex.SampleLevel(smp, float2(t0.x,  t12.y), 0).rgb * (w0.x  * w12.y);
    r += tex.SampleLevel(smp, float2(t12.x, t12.y), 0).rgb * (w12.x * w12.y);
    r += tex.SampleLevel(smp, float2(t3.x,  t12.y), 0).rgb * (w3.x  * w12.y);
    r += tex.SampleLevel(smp, float2(t0.x,  t3.y ), 0).rgb * (w0.x  * w3.y );
    r += tex.SampleLevel(smp, float2(t12.x, t3.y ), 0).rgb * (w12.x * w3.y );
    r += tex.SampleLevel(smp, float2(t3.x,  t3.y ), 0).rgb * (w3.x  * w3.y );
    return r;
}

// ---------------------------------------------------------------- ampliacao direcional
// Ampliador com kernel anisotropico, no espirito do EASU do FSR mas escrito aqui (MIT).
// A ideia: numa borda, borrar AO LONGO dela nao custa nitidez (a borda continua reta) e
// mata o degrau; borrar ATRAVES dela e que estraga. Entao o kernel vira uma elipse
// alinhada com a borda - largo na direcao da borda, estreito na perpendicular. Em area
// lisa a elipse vira circulo e o filtro cai num cubico comum.
//
// 16 taps do bloco 4x4. Fecha com clamp no min/max do 2x2 central, que e o que segura o
// ringing do lobo negativo (mesma trava do EASU).
float3 SampleDirecional(float2 uv)
{
    float2 p  = uv * SrcSize;
    float2 c1 = floor(p - 0.5) + 0.5;
    float2 f  = p - c1;

    float3 s[4][4];
    float  l[4][4];
    [unroll] for (int j = 0; j < 4; j++)
    [unroll] for (int i = 0; i < 4; i++)
    {
        s[j][i] = tex.SampleLevel(smp, (c1 + float2(i - 1, j - 1)) * SrcSizeInv, 0).rgb;
        l[j][i] = dot(s[j][i], float3(0.2126, 0.7152, 0.0722));
    }

    // gradiente por Sobel sobre o 3x3 que envolve o ponto (indices 0..2 do bloco)
    float gx = (l[0][2] + 2.0 * l[1][2] + l[2][2]) - (l[0][0] + 2.0 * l[1][0] + l[2][0]);
    float gy = (l[2][0] + 2.0 * l[2][1] + l[2][2]) - (l[0][0] + 2.0 * l[0][1] + l[0][2]);
    float  g = sqrt(gx * gx + gy * gy);

    // dir = ao longo da borda (perpendicular ao gradiente)
    float2 dir = (g > 1e-4) ? float2(-gy, gx) / g : float2(1.0, 0.0);
    // forca da borda: 0 em area lisa, 1 em borda dura. O 8.0 e o ponto em que uma
    // transicao de ~1/8 da faixa ja conta como borda.
    float e = saturate(g * 8.0);

    // elipse: 'ao longo' pode esticar ate 2x, 'atraves' aperta ate ~0.55x
    float aLong  = 1.0 / (1.0 + 1.0 * e);
    float aCross = 1.0 + 0.8 * e;

    float3 acc = 0.0; float wsum = 0.0;
    [unroll] for (int jj = 0; jj < 4; jj++)
    [unroll] for (int ii = 0; ii < 4; ii++)
    {
        float2 o = float2(ii - 1, jj - 1) - f;              // offset em texels
        float  a = dot(o, dir) * aLong;                      // ao longo da borda
        float  b = dot(o, float2(-dir.y, dir.x)) * aCross;   // atraves
        float  r = sqrt(a * a + b * b);
        float  w = CubicKernel(r);
        acc += s[jj][ii] * w; wsum += w;
    }
    float3 col = acc / max(wsum, 1e-5);

    // trava de ringing no 2x2 central
    float3 mn = min(min(s[1][1], s[1][2]), min(s[2][1], s[2][2]));
    float3 mx = max(max(s[1][1], s[1][2]), max(s[2][1], s[2][2]));

    // Realce limitado por contraste, no espirito do RCAS: nao e o RCAS do FSR (que roda
    // num passe proprio em espaco de saida), e um unsharp barato reaproveitando os taps
    // que ja estao em registrador. O limitador e o mesmo em essencia - o realce nunca
    // passa do min/max da vizinhanca, entao nao cria halo nem estoura branco.
    if (Sharp > 0.0)
    {
        float3 vizinho = (s[1][2] + s[2][1] + s[1][1] + s[2][2]) * 0.25;
        col += Sharp * (col - vizinho) * (1.0 - e * 0.5);   // menos realce em borda dura
    }
    return clamp(col, mn, mx);
}

// ---------------------------------------------------------------- reducao por area
// Fonte MAIOR que o backbuffer (o alvo cresceu depois que a cadeia subiu): o PS reduz. O sampler
// linear so olha 2x2 - reduzindo 4x ele pula 3 de cada 4 texels e o resultado serrilha.
// Uma grade de taps bilineares cobrindo a pegada inteira resolve: cada tap ja soma 2x2,
// entao NxN taps cobrem 2N x 2N texels.
float3 SampleArea(float2 uv)
{
    // n taps espacados de Ratio/n texels: como cada busca bilinear ja integra ~1 texel,
    // isso cobre exatamente a pegada de Ratio texels, sem sobrar nem faltar.
    // Com Ratio < 1.5 um tap so ja cobre tudo - e ai a busca bilinear crua e melhor que
    // qualquer grade (MEDIDO: em 75% a grade larga demais perdia 12% do detalhe do texto
    // e mais da metade do contraste de linha de 1 px para o bilinear puro).
    float r = max(Ratio.x, Ratio.y);
    int n = (int)clamp(round(r), 1.0, 4.0);
    float2 passo = Ratio * SrcSizeInv / (float)n;   // distancia entre taps, em UV
    float3 acc = 0.0; float wsum = 0.0;
    [loop] for (int j = 0; j < n; j++)
    {
        [loop] for (int i = 0; i < n; i++)
        {
            float2 o = float2(i, j) - (n - 1) * 0.5;   // -1.5 .. 1.5 em passos
            acc += tex.SampleLevel(smp, uv + o * passo, 0).rgb;   // caixa: o filtro certo aqui
            wsum += 1.0;
        }
    }
    return acc / max(wsum, 1e-5);
}

// ---- EOTF SMPTE ST 2084 (PQ): codigo 0..1 -> luz absoluta em nits (0..10000)
float3 PqToNits(float3 e)
{
    const float m1 = 0.1593017578125, m2 = 78.84375;
    const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    float3 p = pow(max(e, 0.0), 1.0 / m2);
    return 10000.0 * pow(max(p - c1, 0.0) / max(c2 - c3 * p, 1e-6), 1.0 / m1);
}

// ---- OETF inversa da ARIB STD-B67 (HLG) -> cena normalizada 0..1
float3 HlgToScene(float3 e)
{
    const float a = 0.17883277, b = 0.28466892, c = 0.55991073;
    return (e <= 0.5) ? (e * e) / 3.0 : (exp((e - c) / a) + b) / 12.0;
}

float4 PS(VSOut i) : SV_Target
{
    // UpFilter == 0 e o caminho de sempre (100%): amostra direta, bit a bit igual ao que
    // o app fazia antes de existir resolucao interna.
    float3 col;
    if      (UpFilter == 1) col = max(SampleCatmullRom(i.uv), 0.0);
    else if (UpFilter == 2) col = max(SampleDirecional(i.uv), 0.0);
    else if (UpFilter == 3) col = SampleArea(i.uv);
    else                    col = tex.Sample(smp, i.uv).rgb;

    if (HdrMode != 0)
    {
        // Todo caminho termina aqui com 'lin' = luz LINEAR em nits, primarias BT.709.
        float3 lin;

        if (HdrMode == 3)
        {
            // scRGB, que e o que o WGC entrega num frame pool R16G16B16A16Float: ja
            // linear, ja BT.709, com 1.0 = 80 nits por definicao. Nao ha curva para
            // desfazer nem matriz para corrigir - so a escala.
            lin = max(col, 0.0) * 80.0;
        }
        else
        {
            // 1. A placa etiquetou BT.709 e o conversor desmatriciou o YCbCr com os
            //    coeficientes errados. Desfazer, ainda no dominio NAO-linear.
            if (HdrFixMatrix != 0)
            {
                col = float3(
                    dot(col, float3( 0.949900,  0.045506,  0.004594)),
                    dot(col, float3(-0.054223,  1.038105,  0.016118)),
                    dot(col, float3(-0.002956, -0.009944,  1.012900)));
            }

            // 2. Curva de transferencia -> luz linear, em nits.
            float3 nits;
            if (HdrMode == 1) nits = PqToNits(col);
            else              nits = HlgToScene(max(col, 0.0)) * 1000.0;   // HLG: 1.0 = pico nominal

            // 3. Primarias BT.2020 -> BT.709, em luz LINEAR.
            lin = float3(
                dot(nits, float3( 1.660491, -0.587641, -0.072850)),
                dot(nits, float3(-0.124550,  1.132900, -0.008349)),
                dot(nits, float3(-0.018151, -0.100579,  1.118730)));
        }
        lin = max(lin, 0.0);     // cores fora do gamut BT.709 viram negativas

        // 4. Branco de referencia -> 1.0.
        float3 v = lin / max(HdrRefWhite, 1.0);

        // 5. Roll-off nas altas, so na luma, para nao girar matiz.
        //    Reinhard estendida: f(0)=0, f(W)=1 exato, monotonica e limitada por
        //    construcao. (Uma Hermite C1 seria mais suave no joelho, mas em dominio
        //    LINEAR com muito headroom ela estoura acima de 1.0 antes de chegar ao pico -
        //    medido: com joelho 0.75 e pico 604 nits, 500 nits saturava. O BT.2390 so
        //    escapa disso por trabalhar em dominio PQ, que aqui nao temos.)
        //
        //    HdrKnee >= 1.0 desliga o roll-off: identidade ate o branco de papel e clamp
        //    acima. E o PADRAO da captura de tela, e a razao e geometria pura - em SDR
        //    tudo acima do branco de papel tem de caber entre ele e 1.0, entao ou o
        //    branco sai em 1.0 (fiel, sem realce) ou sai abaixo (com realce, ~10% mais
        //    escuro). Para um jogo SDR num desktop HDR - o caso comum - fidelidade ganha.
        float Lw = max(HdrPeak / max(HdrRefWhite, 1.0), 1.0);
        float y  = dot(v, float3(0.2126, 0.7152, 0.0722));
        if (HdrKnee < 1.0 && y > HdrKnee)
        {
            float W = max(Lw - HdrKnee, 1e-3);
            float t = y - HdrKnee;
            float s = saturate(t * (1.0 + t / (W * W)) / (1.0 + t));
            float ny = HdrKnee + (1.0 - HdrKnee) * s;
            v *= ny / max(y, 1e-6);
        }
        v = saturate(v);

        // 6. De volta a display-referred sRGB - que e o que o ReShade, o LumeniteFX,
        //    o DLSS e o Neural Rendering esperam ver.
        col = (v <= 0.0031308) ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
    }

    // NADA DE AVISO DESENHADO NA IMAGEM, e a razao esta registrada para nao ser tentada
    // de novo: houve uma versao que deixava a tela em preto e branco enquanto o overlay do
    // ReShade estava com o mouse, e outra que acendia uma moldura nas bordas. As duas
    // foram descartadas pelo mesmo motivo - quem abre o ReShade abre para MEXER no filtro
    // e julgar o resultado na hora. Qualquer coisa que o app pinte por cima disputa com
    // exatamente aquilo que a pessoa foi ali avaliar. O aviso do modo interativo e o
    // cartaz, que some sozinho e nao mora na imagem.
    return float4(col, 1.0);
}
)HLSL";

typedef HRESULT(WINAPI* PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

struct Renderer {
    ID3D11Device*            dev = nullptr;
    ID3D11DeviceContext*     ctx = nullptr;
    IDXGISwapChain*          sc = nullptr;
    ID3D11RenderTargetView*  rtv = nullptr;
    ID3D11Texture2D*         depthTex = nullptr;
    ID3D11DepthStencilView*  dsv = nullptr;
    ID3D11Texture2D*         frameTex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11SamplerState*      samp = nullptr;
    ID3D11VertexShader*      vs = nullptr;
    ID3D11PixelShader*       ps = nullptr;
    ID3D11DepthStencilState* dss = nullptr;
    ID3D11RasterizerState*   rs = nullptr;
    UINT w = 0, h = 0;

    // --------- fonte x backbuffer: dois tamanhos que PODEM divergir
    // 'w/h' e SO o backbuffer; 'srcW/srcH' e a resolucao da FONTE (captura). Os dois
    // nascem iguais e ficam iguais na imensa maioria das sessoes.
    //
    // Eles divergem num caso, e o caso importa: quando o alvo muda de tamanho com a
    // cadeia NGX viva. Ai o backbuffer NAO pode ser recriado (ver FonteMudou abaixo:
    // ResizeBuffers com a cadeia de pe mata o processo), entao a fonte nova passa a ser
    // desenhada num backbuffer de outro tamanho - e e o PS que faz a conta, com
    // SampleDirecional quando a fonte e menor e SampleArea quando e maior, mais o
    // letterbox no viewport quando a proporcao muda. Sem isso o caminho cairia no
    // bilinear do sampler, que e visivelmente pior em texto de HUD.
    //
    // (A "Resolucao interna" - backbuffer k vezes a fonte, de proposito - existiu aqui e
    // foi retirada por decisao do usuario: nao melhorava o neural e o custo era do jogo.
    // Os numeros estao em docs/supersampling.md e a decisao em docs/painel.md.)
    UINT  srcW = 0, srcH = 0;

    struct ScaleCBData { float srcW, srcH, invW, invH, ratX, ratY; int upFilter; float sharp; };
    float sharp = 0.35f;   // realce do ampliador direcional (0 = so a ampliacao)
    ID3D11Buffer* scaleCB = nullptr;
    bool          scaleDirty = true;
    bool          deviceMorreu = false;   // Present devolveu DEVICE_REMOVED/RESET
    bool          fonteMudou = false;   // o alvo mudou de tamanho depois que a cadeia subiu

    // Camera: o frame vem da CPU e sobe pra frameTex (DYNAMIC).
    // Janela/monitor: o frame ja e uma textura na GPU; extSrv aponta pra ela e
    // frameTex nem existe (zero copia CPU no caminho).
    bool cpuUpload = true;
    ID3D11ShaderResourceView* extSrv = nullptr;

    // --------- latencia de apresentacao
    // Por padrao o DXGI deixa o driver enfileirar 3 frames na nossa frente. Com a cadeia
    // DLSS pesando no Present isso vira ~110 ms de atraso puro de fila. O swapchain
    // "waitable" com SetMaximumFrameLatency(1) troca isso por uma espera explicita, que a
    // gente faz no TOPO do ciclo - antes de pegar o frame do WGC. Assim o frame desenhado
    // e sempre o mais novo que existia no instante em que a GPU ficou livre.
    IDXGISwapChain2* sc2  = nullptr;    // so existe no caminho waitable
    HANDLE  latWait       = nullptr;    // GetFrameLatencyWaitableObject
    UINT    scFlags       = 0;          // precisa ser repassado no ResizeBuffers
    bool    slotWaited    = false;      // exatamente 1 espera por Present
    bool    vsync         = false;      // padrao: sem esperar o vblank (ver DrawAndPresent)

    // --- HDR da fonte de video: cbuffer b0 do PS. Espelha o Hdr::ShaderParams.
    // 32 bytes, multiplo de 16, casando campo a campo com o cbuffer do HLSL.
    struct HdrCBData { int mode, fixMatrix; float refWhite, peak, knee, pad0, pad1, pad2; };
    ID3D11Buffer* hdrCB = nullptr;
    HdrCBData     hdrData{ 0, 1, 203.f, 1000.f, 0.75f, 0.f, 0.f, 0.f };
    bool          hdrDirty = true;
    void SetHdrParams(int mode, int fixMatrix, float refWhite, float peak, float knee) {
        if (hdrData.mode == mode && hdrData.fixMatrix == fixMatrix &&
            hdrData.refWhite == refWhite && hdrData.peak == peak && hdrData.knee == knee) return;
        hdrData.mode = mode; hdrData.fixMatrix = fixMatrix;
        hdrData.refWhite = refWhite; hdrData.peak = peak; hdrData.knee = knee;
        hdrDirty = true;
    }

    void WaitForPresentSlot() {
        // Com V-Sync ligado o Present(1,0) JA bloqueia ate o vblank. Somar o objeto de
        // espera da uma segunda espera por frame - quem faz o ritmo com vsync e o Present.
        if (vsync) { slotWaited = true; return; }
        if (latWait && !slotWaited) { WaitForSingleObjectEx(latWait, 100, FALSE); slotWaited = true; }
    }

    bool CreateTargets(std::wstring* err) {
        ID3D11Texture2D* bb = nullptr;
        HRESULT hr = sc->GetBuffer(0, IID_ID3D11Texture2D, (void**)&bb);
        if (FAILED(hr)) { *err = L"GetBuffer " + Hex(hr); return false; }
        hr = dev->CreateRenderTargetView(bb, nullptr, &rtv);
        bb->Release();
        if (FAILED(hr)) { *err = L"CreateRenderTargetView " + Hex(hr); return false; }

        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R24G8_TYPELESS; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        hr = dev->CreateTexture2D(&td, nullptr, &depthTex);
        if (FAILED(hr)) { *err = L"CreateTexture2D(depth) " + Hex(hr); return false; }
        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        hr = dev->CreateDepthStencilView(depthTex, &dd, &dsv);
        if (FAILED(hr)) { *err = L"CreateDepthStencilView " + Hex(hr); return false; }

        if (!cpuUpload) return true;   // fonte de tela entrega textura pronta

        D3D11_TEXTURE2D_DESC ft{};
        ft.Width = srcW; ft.Height = srcH; ft.MipLevels = 1; ft.ArraySize = 1;
        ft.Format = DXGI_FORMAT_B8G8R8A8_UNORM; ft.SampleDesc.Count = 1;
        ft.Usage = D3D11_USAGE_DYNAMIC; ft.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ft.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = dev->CreateTexture2D(&ft, nullptr, &frameTex);
        if (FAILED(hr)) { *err = L"CreateTexture2D(frame) " + Hex(hr); return false; }
        hr = dev->CreateShaderResourceView(frameTex, nullptr, &srv);
        if (FAILED(hr)) { *err = L"CreateShaderResourceView " + Hex(hr); return false; }
        return true;
    }
    void ReleaseTargets() {
        if (ctx) { ctx->OMSetRenderTargets(0, nullptr, nullptr); ID3D11ShaderResourceView* n = nullptr; ctx->PSSetShaderResources(0, 1, &n); }
        SafeRelease(srv); SafeRelease(frameTex); SafeRelease(dsv); SafeRelease(depthTex); SafeRelease(rtv);
    }

    bool Init(HWND hwnd, UINT cw, UINT ch, std::wstring* err) {
        srcW = cw; srcH = ch;
        w = cw; h = ch;
        if (!InitWaitable(hwnd) && !InitLegacy(hwnd, err)) return false;
        if (!latWait) {
            // sem objeto de espera: pelo menos corta a fila do driver de 3 pra 1
            IDXGIDevice1* d1 = nullptr;
            if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice1), (void**)&d1)) && d1) {
                d1->SetMaximumFrameLatency(1); d1->Release();
            }
        }
        Log("apresentacao: waitable=%d (objeto=%p) flags=0x%X", latWait ? 1 : 0, (void*)latWait, scFlags);
        return InitRest(err);
    }

    // Caminho preferido: FLIP_DISCARD + FRAME_LATENCY_WAITABLE_OBJECT + max latency 1.
    // Precisa nascer pelo IDXGIFactory2::CreateSwapChainForHwnd (o
    // D3D11CreateDeviceAndSwapChain nao aceita a flag). Se qualquer passo falhar,
    // devolve false com dev/ctx/sc zerados e o InitLegacy assume.
    bool InitWaitable(HWND hwnd) {
        D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0 }; D3D_FEATURE_LEVEL got{};
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, lv, 1,
                                       D3D11_SDK_VERSION, &dev, &got, &ctx);
        if (FAILED(hr)) { Log("D3D11CreateDevice (waitable) falhou 0x%08X", (unsigned)hr); SafeRelease(ctx); SafeRelease(dev); return false; }

        IDXGIDevice* dxdev = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory2* f2 = nullptr;
        dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev);
        if (dxdev) { dxdev->GetAdapter(&ad); dxdev->Release(); }
        if (ad) { ad->GetParent(__uuidof(IDXGIFactory2), (void**)&f2); ad->Release(); }
        if (!f2) { Log("IDXGIFactory2 indisponivel - swapchain classico"); SafeRelease(ctx); SafeRelease(dev); return false; }

        DXGI_SWAP_CHAIN_DESC1 d1{};
        d1.Width = w; d1.Height = h; d1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d1.SampleDesc.Count = 1; d1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d1.BufferCount = 2; d1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        d1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        d1.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        IDXGISwapChain1* s1 = nullptr;
        hr = f2->CreateSwapChainForHwnd(dev, hwnd, &d1, nullptr, nullptr, &s1);
        f2->Release();
        if (FAILED(hr) || !s1) {
            Log("CreateSwapChainForHwnd(waitable) falhou 0x%08X - swapchain classico", (unsigned)hr);
            SafeRelease(ctx); SafeRelease(dev); return false;
        }
        sc = s1; scFlags = d1.Flags;
        if (SUCCEEDED(s1->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&sc2)) && sc2) {
            sc2->SetMaximumFrameLatency(1);
            latWait = sc2->GetFrameLatencyWaitableObject();
        }
        Log("D3D11 ok, swapchain waitable %ux%u FLIP_DISCARD", w, h);
        return true;
    }

    bool InitLegacy(HWND hwnd, std::wstring* err) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = w; sd.BufferDesc.Height = h;
        sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 0; sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0 }; D3D_FEATURE_LEVEL got{};
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, lv, 1, D3D11_SDK_VERSION, &sd, &sc, &dev, &got, &ctx);
        if (FAILED(hr)) {
            Log("FLIP_DISCARD falhou (0x%08X), tentando DISCARD classico", (unsigned)hr);
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; sd.BufferCount = 1;
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, lv, 1, D3D11_SDK_VERSION, &sd, &sc, &dev, &got, &ctx);
        }
        if (FAILED(hr)) { *err = TS(L"D3D11CreateDeviceAndSwapChain falhou ", L"D3D11CreateDeviceAndSwapChain failed ") + Hex(hr); return false; }
        Log("D3D11 ok, swapchain %ux%u, swapeffect=%d", w, h, (int)sd.SwapEffect);
        return true;
    }

    bool InitRest(std::wstring* err) {
        HRESULT hr = S_OK;
        // shaders (d3dcompiler_47.dll vem com o Windows 10/11)
        HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
        PFN_Compile compile = dc ? (PFN_Compile)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!compile) { *err = T(L"d3dcompiler_47.dll nao encontrado.", L"d3dcompiler_47.dll not found."); return false; }
        char gx[16], gy[16]; snprintf(gx, 16, "%du", GRID_X); snprintf(gy, 16, "%du", GRID_Y);
        D3D_SHADER_MACRO macros[] = { {"GRIDX", gx}, {"GRIDY", gy}, {nullptr, nullptr} };
        ID3DBlob *vsb = nullptr, *psb = nullptr, *eb = nullptr;
        hr = compile(kShader, strlen(kShader), "fafacam", macros, nullptr, "VS", "vs_4_0", 0, 0, &vsb, &eb);
        if (FAILED(hr)) { *err = TS(L"VS compile falhou: ", L"VS compile failed: "); if (eb) { std::string e((char*)eb->GetBufferPointer(), eb->GetBufferSize()); *err += std::wstring(e.begin(), e.end()); } return false; }
        SafeRelease(eb);
        hr = compile(kShader, strlen(kShader), "fafacam", macros, nullptr, "PS", "ps_4_0", 0, 0, &psb, &eb);
        if (FAILED(hr)) { *err = TS(L"PS compile falhou: ", L"PS compile failed: "); if (eb) { std::string e((char*)eb->GetBufferPointer(), eb->GetBufferSize()); *err += std::wstring(e.begin(), e.end()); } return false; }
        SafeRelease(eb);
        dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
        dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
        vsb->Release(); psb->Release();
        if (!vs || !ps) { *err = T(L"CreateVertex/PixelShader falhou", L"CreateVertex/PixelShader failed"); return false; }

        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.MaxLOD = D3D11_FLOAT32_MAX;
        dev->CreateSamplerState(&smp, &samp);

        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(HdrCBData);
        cb.Usage = D3D11_USAGE_DYNAMIC; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&cb, nullptr, &hdrCB);   // se falhar, o PS ve zeros = HdrMode 0

        cb.ByteWidth = sizeof(ScaleCBData);
        dev->CreateBuffer(&cb, nullptr, &scaleCB); // se falhar, zeros = UpFilter 0 = 1x

        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = TRUE; ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        dev->CreateDepthStencilState(&ds, &dss);

        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
        dev->CreateRasterizerState(&rd, &rs);

        return CreateTargets(err);
    }

    // Renderer::Rebuild foi REMOVIDO. Ele era o unico ResizeBuffers do arquivo e nao tinha
    // mais chamador desde o FonteMudou - ou seja, era a funcao proibida pela regra da §4.6
    // esperando alguem chamar por engano. Se um dia o backbuffer precisar mudar de tamanho,
    // leia aquela secao antes de escrever a primeira linha.

    // A FONTE mudou de tamanho (o jogo redimensionou a janela, a camera trocou de modo).
    //
    // Aqui NAO se mexe no backbuffer, e esta e a regra mais importante deste arquivo:
    // ResizeBuffers com a cadeia NGX viva mata o processo. Foi medido duas vezes, por
    // caminhos diferentes - trocando a resolucao interna pelo painel e com um jogo que so
    // redimensionou a propria janela. O ReShade recria o runtime de efeitos inteiro quando
    // o backbuffer muda de tamanho, e a feature do DLSS morre no meio
    // (CreateFeature raised exception 0xC0000005).
    //
    // Entao o backbuffer fica do tamanho em que nasceu e a fonte nova e desenhada nele,
    // com letterbox quando a proporcao muda (ver o viewport em DrawAndPresent). Quem
    // quiser o filtro rodando na resolucao nova reabre o app - e o painel oferece isso.
    bool FonteMudou(UINT nw, UINT nh, std::wstring* err) {
        (void)err;
        if (!nw || !nh || (nw == srcW && nh == srcH)) return true;
        Log("fonte mudou de %ux%u para %ux%u - backbuffer segue em %ux%u (sem recriar a cadeia)",
            srcW, srcH, nw, nh, w, h);
        srcW = nw; srcH = nh;
        scaleDirty = true;
        fonteMudou = true;
        if (cpuUpload) {                    // camera: a textura de upload acompanha a fonte
            if (ctx) { ID3D11ShaderResourceView* n = nullptr; ctx->PSSetShaderResources(0, 1, &n); }
            SafeRelease(srv); SafeRelease(frameTex);
            D3D11_TEXTURE2D_DESC ft{};
            ft.Width = srcW; ft.Height = srcH; ft.MipLevels = 1; ft.ArraySize = 1;
            ft.Format = DXGI_FORMAT_B8G8R8A8_UNORM; ft.SampleDesc.Count = 1;
            ft.Usage = D3D11_USAGE_DYNAMIC; ft.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            ft.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (SUCCEEDED(dev->CreateTexture2D(&ft, nullptr, &frameTex)))
                dev->CreateShaderResourceView(frameTex, nullptr, &srv);
        }
        return true;
    }
    // nome antigo, para os chamadores que ja existiam
    bool Resize(UINT nw, UINT nh, std::wstring* err) { return FonteMudou(nw, nh, err); }

    void Upload(const BYTE* src, UINT sw, UINT sh) {
        if (!frameTex || sw != srcW || sh != srcH) return;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(frameTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        const size_t row = (size_t)srcW * 4;
        for (UINT y = 0; y < srcH; y++) memcpy((BYTE*)m.pData + (size_t)y * m.RowPitch, src + (size_t)y * row, row);
        ctx->Unmap(frameTex, 0);
    }

    void DrawAndPresent() {
        if (!rtv) return;
        ID3D11ShaderResourceView* use = extSrv ? extSrv : srv;
        if (!use) return;
        const float clear[4] = { 0.f, 0.f, 0.f, 1.f };
        ctx->OMSetRenderTargets(1, &rtv, dsv);
        ctx->ClearRenderTargetView(rtv, clear);
        ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        // Viewport com letterbox: se a fonte mudou de proporcao depois que a cadeia subiu,
        // o backbuffer continua no tamanho de nascimento (ver FonteMudou), entao a imagem
        // e desenhada na maior area de mesma proporcao e o resto fica preto - melhor uma
        // tarja do que a imagem esticada.
        float vw = (float)w, vh = (float)h, vx = 0.f, vy = 0.f;
        if (srcW && srcH) {
            const float arSrc = (float)srcW / (float)srcH;
            const float arBB  = (float)w / (float)h;
            if (arSrc > arBB)      { vh = (float)w / arSrc; vy = ((float)h - vh) * 0.5f; }
            else if (arSrc < arBB) { vw = (float)h * arSrc; vx = ((float)w - vw) * 0.5f; }
        }
        D3D11_VIEWPORT vp{ vx, vy, vw, vh, 0.f, 1.f };
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(rs);
        ctx->OMSetDepthStencilState(dss, 0);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        if (hdrCB) {
            if (hdrDirty) {
                D3D11_MAPPED_SUBRESOURCE m{};
                if (SUCCEEDED(ctx->Map(hdrCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                    memcpy(m.pData, &hdrData, sizeof(hdrData));
                    ctx->Unmap(hdrCB, 0);
                    hdrDirty = false;
                }
            }
            ctx->PSSetConstantBuffers(0, 1, &hdrCB);
        }
        if (scaleCB) {
            if (scaleDirty) {
                // 0 = 1:1 (nada a fazer), 2 = direcional quando amplia, 3 = area quando reduz.
                // Isto NAO e resto da resolucao interna removida: e o caminho de quando o
                // alvo muda de tamanho e o backbuffer fica no que era. Sem ele, esse caso
                // cai no bilinear do sampler, que borra texto de HUD.
                const int filtro = (w == srcW && h == srcH) ? 0 : ((w > srcW) ? 2 : 3);
                ScaleCBData s{ (float)srcW, (float)srcH,
                               srcW ? 1.f / (float)srcW : 0.f, srcH ? 1.f / (float)srcH : 0.f,
                               w ? (float)srcW / (float)w : 1.f, h ? (float)srcH / (float)h : 1.f,
                               filtro, (filtro == 2) ? sharp : 0.f };
                D3D11_MAPPED_SUBRESOURCE m{};
                if (SUCCEEDED(ctx->Map(scaleCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                    memcpy(m.pData, &s, sizeof(s));
                    ctx->Unmap(scaleCB, 0);
                    scaleDirty = false;
                }
            }
            ctx->PSSetConstantBuffers(1, 1, &scaleCB);
        }
        ctx->PSSetShaderResources(0, 1, &use);
        ctx->PSSetSamplers(0, 1, &samp);
        ctx->Draw(GRID_X * GRID_Y * 6, 0);
        ID3D11ShaderResourceView* n = nullptr; ctx->PSSetShaderResources(0, 1, &n);
        // V-Sync: com a captura destravada (WGC a ~100 fps) o Present(1,0) virou o
        // limitador - ele trava no refresh do monitor onde a janela esta. Present(0,0)
        // em flip model NAO rasga a imagem (isso exigiria ALLOW_TEARING, que nao usamos):
        // ele so deixa de esperar o vblank.
        const HRESULT hrP = sc->Present(vsync ? 1 : 0, 0);
        // DEVICE_REMOVED / DEVICE_RESET: TDR do driver, ou uma atualizacao de driver com o
        // app aberto (acontece muito em publico). Sem isto o app seguia apresentando num
        // device morto: janela preta, laco a 100% de um nucleo e nenhuma mensagem. Nao da
        // para recuperar aqui - recriar o device recriaria o backbuffer, e a cadeia NGX nao
        // sobrevive a isso (ver FonteMudou) -, entao a resposta honesta e avisar e sair
        // limpo, que e o que devolve o HDR do monitor.
        if (hrP == DXGI_ERROR_DEVICE_REMOVED || hrP == DXGI_ERROR_DEVICE_RESET) {
            const HRESULT motivo = dev ? dev->GetDeviceRemovedReason() : hrP;
            Log("Present devolveu 0x%08X (device removed; motivo 0x%08X) - encerrando",
                (unsigned)hrP, (unsigned)motivo);
            deviceMorreu = true;
        }
        slotWaited = false;
    }

    void Shutdown() {
        ReleaseTargets();
        if (latWait) { CloseHandle(latWait); latWait = nullptr; }   // veio do GetFrameLatencyWaitableObject
        SafeRelease(hdrCB); SafeRelease(scaleCB);
        SafeRelease(rs); SafeRelease(dss); SafeRelease(samp); SafeRelease(ps); SafeRelease(vs);
        if (ctx) ctx->ClearState();
        SafeRelease(sc2); SafeRelease(sc); SafeRelease(ctx); SafeRelease(dev);
    }

    // Grava o backbuffer (a saida REAL da cadeia, ja com o PS aplicado) num PNG.
    // Existe para diagnostico de cor: um print de tela num desktop HDR passa pelo tone
    // map do DWM e nao serve para medir nada. Ligado por DLSS5_DUMP=<caminho.png>.
    bool SaveBackbufferPng(const wchar_t* path) {
        if (!sc || !dev || !ctx) return false;
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(sc->GetBuffer(0, IID_ID3D11Texture2D, (void**)&bb)) || !bb) return false;
        D3D11_TEXTURE2D_DESC d{}; bb->GetDesc(&d);
        D3D11_TEXTURE2D_DESC sd = d;
        sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
        ID3D11Texture2D* stg = nullptr;
        HRESULT hr = dev->CreateTexture2D(&sd, nullptr, &stg);
        if (SUCCEEDED(hr) && stg) ctx->CopyResource(stg, bb);
        bb->Release();
        if (FAILED(hr) || !stg) return false;

        bool ok = false;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m))) {
            Gdiplus::Bitmap bmp((INT)d.Width, (INT)d.Height, PixelFormat32bppARGB);
            Gdiplus::Rect rc(0, 0, (INT)d.Width, (INT)d.Height);
            Gdiplus::BitmapData bd{};
            if (bmp.LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
                for (UINT y = 0; y < d.Height; y++) {
                    const BYTE* s = (const BYTE*)m.pData + (size_t)y * m.RowPitch;
                    BYTE* dst = (BYTE*)bd.Scan0 + (size_t)y * bd.Stride;
                    for (UINT x = 0; x < d.Width; x++) {   // BGRA -> BGRA, alfa opaco
                        dst[x * 4 + 0] = s[x * 4 + 0]; dst[x * 4 + 1] = s[x * 4 + 1];
                        dst[x * 4 + 2] = s[x * 4 + 2]; dst[x * 4 + 3] = 255;
                    }
                }
                bmp.UnlockBits(&bd);
                UINT num = 0, sz = 0;
                Gdiplus::GetImageEncodersSize(&num, &sz);
                if (sz) {
                    Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)malloc(sz);
                    if (info) {
                        Gdiplus::GetImageEncoders(num, sz, info);
                        for (UINT i = 0; i < num; i++)
                            if (wcscmp(info[i].MimeType, L"image/png") == 0) {
                                ok = (bmp.Save(path, &info[i].Clsid, nullptr) == Gdiplus::Ok);
                                break;
                            }
                        free(info);
                    }
                }
            }
            ctx->Unmap(stg, 0);
        }
        stg->Release();
        return ok;
    }
};

// ------------------------------------------------------------------ janela de render
static std::atomic<bool> g_quit{ false };
static void ToggleFullscreen(HWND h) {
    static WINDOWPLACEMENT wp{ sizeof(wp) };
    static bool emTelaCheia = false;   // FOMOS nos que tiramos a moldura desta janela?
    DWORD st = (DWORD)GetWindowLongPtrW(h, GWL_STYLE);
    // No modo sobreposto a janela e um WS_POPUP colado no alvo, sem moldura NENHUMA. Sem
    // esta guarda o ramo "senao" la embaixo DAVA moldura a ela: barra de titulo e borda
    // por cima do jogo, e sem volta, porque o proximo F11 acharia WS_OVERLAPPEDWINDOW e
    // entraria em "tela cheia". A ausencia do estilo, sozinha, nao distingue os dois
    // casos - por isso a marca explicita de quem tirou.
    if (!(st & WS_OVERLAPPEDWINDOW) && !emTelaCheia) {
        Log("F11/Alt+Enter ignorado: janela sem moldura (modo sobreposto)");
        return;
    }
    if (st & WS_OVERLAPPEDWINDOW) {
        MONITORINFO mi{ sizeof(mi) };
        if (GetWindowPlacement(h, &wp) && GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLongPtrW(h, GWL_STYLE, st & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            emTelaCheia = true;
        }
    } else {
        SetWindowLongPtrW(h, GWL_STYLE, st | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(h, &wp);
        SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        emTelaCheia = false;
    }
}
static std::atomic<bool> g_clickThrough{ false };   // modo sobreposto: cliques atravessam pro jogo
static void MarcarUsoInterativo(bool nossaJanela, UINT msg);   // definido junto do modo interativo

// Este WndProc roda na thread principal, a mesma do render (ver o bloco "POR QUE A JANELA
// NAO VIVE NUMA THREAD SO DELA"). Com WS_EX_LAYERED o sistema nem pergunta pelo hit-test,
// entao o custo do Present nao prende mais o mouse de quem esta embaixo da camada. Sem o
// layered, isso volta a ser um problema serio: no modo monitor a camada e a tela inteira.
static LRESULT CALLBACK RenderWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    // O prazo do modo interativo e renovado aqui, e nao no laco principal: assim ele
    // continua certo independente de quem despacha as mensagens desta janela.
    if ((m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || (m >= WM_KEYFIRST && m <= WM_KEYLAST))
        MarcarUsoInterativo(true, m);
    switch (m) {
    case WM_NCHITTEST:
        if (g_clickThrough) return HTTRANSPARENT;
        break;
    case WM_MOUSEACTIVATE:
        if (g_clickThrough) return MA_NOACTIVATE;
        break;
    case WM_KEYDOWN:
        // Esc NAO fecha o app. Em jogo a pessoa aperta Esc o tempo todo, e perder o
        // filtro (ou o app inteiro) por causa disso e inaceitavel. Esc so fecha o que e
        // nosso e flutuante - painel, cartaz, tela do grupo -, cada um na sua janela.
        // Sair e pelo atalho da acao "sair", pela bandeja, pelo painel ou pelo X.
        if (wp == VK_F11) { ToggleFullscreen(h); return 0; }
        break;
    case WM_SYSKEYDOWN:   // Alt+Enter
        if (wp == VK_RETURN) { ToggleFullscreen(h); return 0; }
        break;
    case WM_DESTROY: g_quit = true; PostQuitMessage(0); return 0;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// =====================================================================================
//  POR QUE A JANELA NAO VIVE NUMA THREAD SO DELA (foi tentado, e foi desfeito)
//
//  O hit-test do Windows e SINCRONO: quando ele PRECISA perguntar "o clique e seu?", manda
//  WM_NCHITTEST para a thread dona da janela e ESPERA a resposta antes de entregar o
//  clique a quem quer que seja. Com a camada cobrindo a tela inteira e a mesma thread
//  fazendo a fila de mensagens e o DrawAndPresent (onde o ReShade compila efeitos e o DLSS
//  NR roda), o mouse do sistema INTEIRO ficava pendurado: medido, ate 1864 ms de espera,
//  nos mesmos instantes em que o log registra "present travou 841 ms".
//
//  A janela chegou a ser movida para uma thread propria por causa disso, e resolvia (0,4 ms
//  no pior caso). So que a CAUSA REAL do clique nao passar era outra - faltava
//  WS_EX_LAYERED, ver o bloco "O CLIQUE SO ATRAVESSA COM WS_EX_LAYERED" - e, com o layered
//  no lugar, o sistema descarta a camada no hit-test SEM PERGUNTAR NADA a nossa thread. A
//  espera some pela raiz, e a thread extra passou a ser so risco:
//
//    - o ReShade vive DENTRO deste processo e se pendura na janela/swapchain; o PokeReShade
//      (o F6 e o botao "Abrir o ReShade") posta a tecla na janela e conta com o Present da
//      MESMA thread para ela ser lida. Com a janela em outra thread esse acordo quebra - o
//      usuario sentiu na hora: "o F6 demora a responder, fica na fila".
//    - AttachThreadInput/SetForegroundWindow dentro do PokeReShade passavam a thread errada.
//
//  Fica registrado para nao ser reinventado: se um dia o layered tiver de sair, a thread
//  propria volta a ser necessaria - e o PokeReShade tem de ser adaptado junto.

// ------------------------------------------------------------------ escolha da fonte
struct Choice {
    SourceKind   kind = SRC_CAMERA;
    bool overlay = false;      // janela colada POR CIMA do alvo (modo filtro)
    bool clickThrough = true;  // overlay deixa o mouse passar pro jogo embaixo
    int  fpsCap = 0;           // teto de present/s (0 = 1 por frame capturado)
    bool vsync = false;        // esperar o vblank no Present (padrao: nao)
    bool hdrTreat = false;     // tratar o HDR (desligar no monitor / converter PQ)
    int          dev = -1, mode = -1;      // camera
    HWND         win = nullptr;            // janela
    std::wstring winTitle;
    HMONITOR     mon = nullptr;            // monitor
    std::wstring monLabel;
    bool clientOnly = true;   // janela: so a area util (sem barra de titulo / bordas)
    bool fpsTela = true;      // contador de FPS do overlay do ReShade ([OVERLAY] ShowFPS)
    bool cursor = false;      // desenhar o cursor do mouse no frame
    bool hideSelf = false;    // tirar a nossa janela da captura (evita espelho infinito)
    bool border = false;      // borda amarela do Windows no alvo capturado
    bool fs = false;
};

// ------------------------------------------------------------------ dialogo de selecao
enum { HK_QUIT = 1, HK_INTERACT = 2, HK_BYPASS = 3,
       HK_QUIT2 = 4, HK_INTERACT2 = 5, HK_BYPASS2 = 6,
       HK_PANEL = 7, HK_PANEL2 = 8 };

// =====================================================================================
//  MAPA DE ATALHOS
//
//  O esquema antigo eram quatro combinacoes com modificador que ainda mudavam de maquina
//  pra maquina - o usuario nao aprendia nunca. Agora sao DUAS teclas, sem modificador:
//  End abre o painel, F6 liga e desliga tudo. O resto se faz no painel, com o mouse.
//  Quem quiser tecla pras outras acoes define; por isso isto e um mapa, e nao constantes.
//  Uma fonte de verdade so, gravada em [atalhos] do dlss5forall.ini, lida pelo painel,
//  pelo seletor e pelos cartazes - nunca escreva o nome de uma tecla direto num texto.
// =====================================================================================
enum AcaoAtalho { AT_PAINEL = 0, AT_TUDO, AT_FILTRO, AT_INTERATIVO, AT_SAIR, AT_COUNT };
enum { HK_ACAO_BASE = 20 };   // id do RegisterHotKey = HK_ACAO_BASE + acao

struct Atalho {
    const wchar_t* chave;    // nome no ini - NAO se traduz: e formato de arquivo
    const wchar_t* rotulo;   // como a interface chama a acao (pt)
    const wchar_t* rotuloEn; // ...e em ingles. Ler sempre por RotuloAtalho(), nunca direto:
                             // um `const wchar_t*` gravado aqui congelaria no idioma de
                             // quando a tabela foi construida, que e antes de ler o ini.
    UINT mods = 0, vk = 0;   // 0 = sem tecla
    bool registrado = false;
};
static Atalho g_atalhos[AT_COUNT] = {
    { L"painel",     L"Abrir o painel",                  L"Open the panel" },
    { L"tudo",       L"Habilitar/desabilitar o DLSS 5",  L"Enable/disable DLSS 5" },
    { L"filtro",     L"Só a camada, sem mexer no neural", L"Overlay only, leave the neural alone" },
    { L"interativo", L"Abrir/fechar o ReShade",          L"Open/close ReShade" },
    { L"sair",       L"Sair do app",                     L"Quit the app" },
};

// nome legivel de uma tecla, do jeito que o usuario a conhece
//
// DUAS FUNCOES, E A DIFERENCA E O QUE VAI PARA O DISCO. Esta aqui e a CANONICA: e ela que
// GravarAtalho escreve em [atalhos] do ini e TeclaDeNome le de volta, e por isso ela nao
// muda com o idioma. Se o texto gravado mudasse junto com a lingua, trocar de idioma
// apagaria os atalhos de quem ja usa o app (o parser nao reconheceria a linha) - e o
// sintoma seria "meus atalhos sumiram sozinhos".
// Quem vai para a tela e NomeTecla(), logo abaixo.
static std::wstring NomeTeclaCanonica(UINT mods, UINT vk) {
    if (!vk) return L"sem tecla";
    std::wstring s;
    if (mods & MOD_CONTROL) s += L"Ctrl+";
    if (mods & MOD_ALT)     s += L"Alt+";
    if (mods & MOD_SHIFT)   s += L"Shift+";
    if (vk >= VK_F1 && vk <= VK_F24) { wchar_t b[8]; _snwprintf(b, 8, L"F%u", vk - VK_F1 + 1); return s + b; }
    switch (vk) {
    case VK_INSERT: return s + L"Insert";
    case VK_DELETE: return s + L"Delete";
    case VK_HOME:   return s + L"Home";
    case VK_END:    return s + L"End";
    case VK_PRIOR:  return s + L"Page Up";
    case VK_NEXT:   return s + L"Page Down";
    case VK_PAUSE:  return s + L"Pause";
    case VK_SCROLL: return s + L"Scroll Lock";
    case VK_ESCAPE: return s + L"Esc";
    case VK_SPACE:  return s + L"Espaço";
    case VK_RETURN: return s + L"Enter";
    case VK_TAB:    return s + L"Tab";
    case VK_OEM_3:  return s + L"`";
    default: break;
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) { wchar_t b[4] = { (wchar_t)vk, 0 }; return s + b; }
    // "tecla 4294967295" tem 16 caracteres + terminador: com b[16] o _snwprintf trunca
    // SEM terminar e o std::wstring seguinte le alem do buffer. O vk vem do ReShade.ini,
    // que e texto editavel - nao da para supor que cabe em dois digitos.
    wchar_t b[24]; _snwprintf(b, 24, T(L"tecla %u", L"key %u"), vk); b[23] = 0;
    return s + b;
}
// O nome que a pessoa LE. So duas palavras do canonico sao texto de verdade - o resto
// (Ctrl, Alt, Home, End, F6...) e o que esta escrito na tecla, igual nos dois idiomas.
static std::wstring NomeTecla(UINT mods, UINT vk) {
    if (!EmIngles()) return NomeTeclaCanonica(mods, vk);
    if (!vk) return L"no key";
    std::wstring s = NomeTeclaCanonica(mods, vk);
    const size_t p = s.rfind(L"Espaço");
    if (p != std::wstring::npos) s.replace(p, 6, L"Space");
    return s;
}
// o inverso, para ler do ini. Aceita tambem os nomes em ingles: um ini escrito a mao
// (ou por uma versao futura) nao pode virar "atalho sem tecla" em silencio.
static bool TeclaDeNome(const std::wstring& txt, UINT* mods, UINT* vk) {
    *mods = 0; *vk = 0;
    if (txt.empty() || txt == L"sem tecla" || txt == L"no key" || txt == L"-") return false;
    std::wstring t = txt;
    for (;;) {
        size_t p = t.find(L'+');
        if (p == std::wstring::npos || p + 1 >= t.size()) break;
        std::wstring m = t.substr(0, p);
        if      (m == L"Ctrl")  *mods |= MOD_CONTROL;
        else if (m == L"Alt")   *mods |= MOD_ALT;
        else if (m == L"Shift") *mods |= MOD_SHIFT;
        else break;
        t = t.substr(p + 1);
    }
    if (t.size() >= 2 && (t[0] == L'F' || t[0] == L'f') && iswdigit(t[1])) {
        int n = _wtoi(t.c_str() + 1);
        if (n >= 1 && n <= 24) { *vk = VK_F1 + n - 1; return true; }
    }
    if (t == L"Insert") *vk = VK_INSERT;
    else if (t == L"Delete") *vk = VK_DELETE;
    else if (t == L"Home") *vk = VK_HOME;
    else if (t == L"End") *vk = VK_END;
    else if (t == L"Page Up") *vk = VK_PRIOR;
    else if (t == L"Page Down") *vk = VK_NEXT;
    else if (t == L"Pause") *vk = VK_PAUSE;
    else if (t == L"Scroll Lock") *vk = VK_SCROLL;
    else if (t == L"Esc") *vk = VK_ESCAPE;
    else if (t == L"Espaço" || t == L"Space") *vk = VK_SPACE;
    else if (t == L"Enter") *vk = VK_RETURN;
    else if (t == L"Tab") *vk = VK_TAB;
    else if (t == L"`") *vk = VK_OEM_3;
    else if (t.size() == 1) *vk = (UINT)towupper(t[0]);
    return *vk != 0;
}
static std::wstring TextoAtalho(AcaoAtalho a) { return NomeTecla(g_atalhos[a].mods, g_atalhos[a].vk); }
static bool TemAtalho(AcaoAtalho a) { return g_atalhos[a].vk != 0 && g_atalhos[a].registrado; }
// o rotulo da acao no idioma atual (nunca leia g_atalhos[i].rotulo direto)
static const wchar_t* RotuloAtalho(int a) { return T(g_atalhos[a].rotulo, g_atalhos[a].rotuloEn); }

// O rodape do seletor, curto o bastante para caber em 196 px e honesto sobre o que
// realmente foi registrado. Sem tecla nenhuma, diz o caminho que sempre existe.
static std::wstring TextoAtalho(AcaoAtalho a);
static std::wstring RodapeAtalhos() {
    const bool p = TemAtalho(AT_PAINEL), u = TemAtalho(AT_TUDO);
    // Sem os "=" e com o separador estreito: com as duas teclas, "End = painel   ·   F6 =
    // liga/desliga" nao cabia nos 196 px do campo do seletor e saia cortado no meio da
    // ultima palavra ("F6 = liga/des"). Esta forma cabe e diz a mesma coisa.
    const std::wstring painel = TS(L" painel", L" panel");
    const std::wstring tudo   = TS(L" liga/desliga", L" on/off");
    if (p && u) return TextoAtalho(AT_PAINEL) + painel + L" · " + TextoAtalho(AT_TUDO) + tudo;
    if (p)      return TextoAtalho(AT_PAINEL) + painel;
    if (u)      return TextoAtalho(AT_TUDO) + tudo;
    return T(L"atalhos em uso por outro app", L"another app holds the shortcut keys");
}

// ------------------------------------------------------------------ atalhos globais
// Alt+F9/F10/F12 sao a primeira escolha, mas outro programa pode ja ter registrado
// (foi o que aconteceu aqui). Entao cada comando tem uma lista de alternativas e o
// app usa a primeira que o Windows aceitar - e mostra qual ficou valendo.
struct HkOpt { UINT mods; UINT vk; const wchar_t* name; };
static std::wstring g_hkBypass = L"-", g_hkInteract = L"-", g_hkQuit = L"-", g_hkPanel = L"-";
static std::wstring IniPath();   // definido depois do seletor, mas o mapa precisa antes

// Le/grava o mapa em [atalhos]. Padrao: so as duas teclas que o usuario precisa saber.
static void CarregarAtalhos() {
    struct { AcaoAtalho a; UINT mods, vk; } padrao[] = {
        { AT_PAINEL, 0, VK_END },   // End abre o painel
        { AT_TUDO,   0, VK_F6 },    // F6 liga e desliga tudo
        { AT_FILTRO, 0, 0 },        // sem tecla: faz-se pelo painel
        { AT_INTERATIVO, 0, 0 },
        { AT_SAIR, MOD_ALT, VK_ESCAPE },
    };
    for (auto& p : padrao) { g_atalhos[p.a].mods = p.mods; g_atalhos[p.a].vk = p.vk; }
    for (int i = 0; i < AT_COUNT; i++) {
        wchar_t b[64]{};
        GetPrivateProfileStringW(L"atalhos", g_atalhos[i].chave, L"", b, 64, IniPath().c_str());
        std::wstring v = b;
        if (v.empty()) continue;
        UINT m = 0, k = 0;
        if (v == L"sem tecla") { g_atalhos[i].mods = 0; g_atalhos[i].vk = 0; }
        else if (TeclaDeNome(v, &m, &k)) { g_atalhos[i].mods = m; g_atalhos[i].vk = k; }
    }
}
// apaga a secao inteira do ini (usada pelo "restaurar padroes")
static void DeletePrivateProfileSection(const wchar_t* secao) {
    WritePrivateProfileStringW(secao, nullptr, nullptr, IniPath().c_str());
}
static void GravarAtalho(AcaoAtalho a) {
    // NomeTeclaCanonica, e nao TextoAtalho: o que vai para o ini nao pode depender do
    // idioma da sessao - ver a nota em cima de NomeTeclaCanonica.
    WritePrivateProfileStringW(L"atalhos", g_atalhos[a].chave,
                               NomeTeclaCanonica(g_atalhos[a].mods, g_atalhos[a].vk).c_str(),
                               IniPath().c_str());
}

// Cascata de reserva. Tres acoes tem: painel, "tudo" e "sair".
//
// O painel sempre teve, pelo motivo obvio: sem ele nao ha porta de entrada nenhuma. As
// outras duas ganharam depois, e o motivo e o mesmo por outro caminho. O "tudo" e o
// antes-e-depois - a demonstracao que faz a pessoa entender para que serve o app - e o
// "sair" e a unica saida visivel no modo sobreposto, que nao tem barra de titulo, nem
// entrada na barra de tarefas, nem Alt+Tab. Perder qualquer uma das duas em silencio
// porque um gravador de tela residente pegou o F6 e caro demais.
//
// Os ajustes finos continuam sem cascata de proposito: quem os quer abre o painel, e o
// painel faz tudo no mouse.
struct TeclaReserva { UINT mods, vk; };
static const TeclaReserva kCascataPainel[] = {
    { 0, VK_END }, { 0, VK_INSERT }, { 0, VK_PAUSE }, { 0, VK_SCROLL },
    { MOD_CONTROL | MOD_ALT, 'D' }, { MOD_CONTROL | MOD_ALT, 'P' },
};
static const TeclaReserva kCascataTudo[] = {
    { 0, VK_F6 }, { 0, VK_F7 }, { MOD_ALT, VK_F6 }, { MOD_CONTROL | MOD_ALT, 'T' },
};
// Alt+Esc primeiro por decisao do usuario. Ele E atalho do Windows (alterna para a proxima
// janela), e enquanto o app roda o sistema perde essa funcao - por isso a dupla pressao
// continua valendo e o guia avisa. Ctrl+Alt+Q entra logo atras, para quem preferir.
static const TeclaReserva kCascataSair[] = {
    { MOD_ALT, VK_ESCAPE }, { MOD_CONTROL | MOD_ALT, 'Q' },
    { MOD_CONTROL | MOD_ALT, VK_F12 }, { MOD_CONTROL | MOD_SHIFT, 'Q' },
};
static bool RegistrarAtalho(AcaoAtalho a) {
    UnregisterHotKey(nullptr, HK_ACAO_BASE + a);
    g_atalhos[a].registrado = false;
    if (!g_atalhos[a].vk) return false;
    if (RegisterHotKey(nullptr, HK_ACAO_BASE + a, g_atalhos[a].mods | MOD_NOREPEAT, g_atalhos[a].vk)) {
        g_atalhos[a].registrado = true;
        return true;
    }
    return false;
}
// true quando alguma acao ficou SEM tecla porque o Windows recusou (nao porque a pessoa
// escolheu "sem tecla"). Vira cartaz na abertura - ver o uso em wWinMain.
static bool g_algumAtalhoRecusado = false;

static void RegistrarTodosAtalhos() {
    for (int i = 0; i < AT_COUNT; i++) RegistrarAtalho((AcaoAtalho)i);
    struct { AcaoAtalho acao; const TeclaReserva* lista; size_t n; } reservas[] = {
        { AT_PAINEL, kCascataPainel, sizeof(kCascataPainel) / sizeof(kCascataPainel[0]) },
        { AT_TUDO,   kCascataTudo,   sizeof(kCascataTudo)   / sizeof(kCascataTudo[0])   },
        { AT_SAIR,   kCascataSair,   sizeof(kCascataSair)   / sizeof(kCascataSair[0])   },
    };
    for (const auto& r : reservas) {
        if (g_atalhos[r.acao].registrado || !g_atalhos[r.acao].vk) continue;
        const UINT mAntes = g_atalhos[r.acao].mods, vAntes = g_atalhos[r.acao].vk;
        for (size_t i = 0; i < r.n; i++) {
            g_atalhos[r.acao].mods = r.lista[i].mods; g_atalhos[r.acao].vk = r.lista[i].vk;
            if (RegistrarAtalho(r.acao)) break;
        }
        // Nenhuma da lista pegou: devolve a escolha original, para o painel mostrar a tecla
        // que a pessoa pediu (marcada como recusada) em vez da ultima que tentamos.
        if (!g_atalhos[r.acao].registrado) {
            g_atalhos[r.acao].mods = mAntes; g_atalhos[r.acao].vk = vAntes;
        }
    }
    g_hkPanel = TextoAtalho(AT_PAINEL);
    g_algumAtalhoRecusado = false;
    for (int i = 0; i < AT_COUNT; i++) {
        if (g_atalhos[i].vk && !g_atalhos[i].registrado) g_algumAtalhoRecusado = true;
        Log("atalho %s = %s%s", U8(g_atalhos[i].chave).c_str(),
            U8(NomeTeclaCanonica(g_atalhos[i].mods, g_atalhos[i].vk)).c_str(),
            (g_atalhos[i].vk && !g_atalhos[i].registrado) ? "  (RECUSADO - ja em uso)" : "");
    }
}

// A tecla que abre o overlay do ReShade, lida do ReShade.ini ([INPUT] KeyOverlay=vk,ctrl,shift,alt).
// Nunca escrever "End" ou "Home" fixo: quem manda nisso e a instalacao do ReShade.
// As nossas hotkeys globais interceptariam a propria tecla que vamos sintetizar (ou
// capturar na configuracao): sem suspender, o SendInput dispararia WM_HOTKEY e a acao
// chamaria a si mesma, e o dialogo de atalhos nunca veria a tecla.
static void SuspenderAtalhos() {
    for (int i = 0; i < AT_COUNT; i++)
        if (g_atalhos[i].registrado) UnregisterHotKey(nullptr, HK_ACAO_BASE + i);
}
static void RetomarAtalhos() {
    // O resultado do RegisterHotKey volta para a flag: durante a suspensao outro programa
    // pode ter tomado a tecla, e ai ela nao volta a ser nossa. Sem isto o painel continuava
    // anunciando a tecla como valida e ela nao fazia nada - o pior tipo de defeito de
    // interface, o que mente sobre o proprio estado.
    for (int i = 0; i < AT_COUNT; i++) {
        if (!g_atalhos[i].registrado) continue;
        g_atalhos[i].registrado =
            RegisterHotKey(nullptr, HK_ACAO_BASE + i, g_atalhos[i].mods | MOD_NOREPEAT, g_atalhos[i].vk) != 0;
        if (!g_atalhos[i].registrado)
            Log("atalho '%s' nao voltou: outro programa tomou %s durante a suspensao",
                U8(g_atalhos[i].chave).c_str(), U8(NomeTeclaCanonica(g_atalhos[i].mods, g_atalhos[i].vk)).c_str());
    }
}

// modsOut sai em MOD_CONTROL/MOD_SHIFT/MOD_ALT. Quem manda a tecla PRECISA olhar isto:
// o PokeReShade sintetiza a tecla NUA, entao uma combinacao ("Ctrl+Home") chegaria do
// outro lado como "Home" e faria outra coisa - ou coisa nenhuma.
static std::wstring TeclaOverlayReShade(UINT* vkOut = nullptr, UINT* modsOut = nullptr) {
    wchar_t b[64]{};
    GetPrivateProfileStringW(L"INPUT", L"KeyOverlay", L"", b, 64, (g_exeDir + L"ReShade.ini").c_str());
    UINT vk = 0, ctrl = 0, shift = 0, alt = 0;
    // vk e um byte: qualquer coisa acima de 0xFF no ini nao e tecla, e uma linha
    // corrompida ou de outra versao do ReShade. Recusar aqui evita propagar o lixo.
    if (swscanf(b, L"%u,%u,%u,%u", &vk, &ctrl, &shift, &alt) < 1 || !vk || vk > 0xFF) {
        if (vkOut) *vkOut = 0;
        if (modsOut) *modsOut = 0;
        return T(L"(não configurada)", L"(not set)");
    }
    if (vkOut) *vkOut = vk;
    UINT m = (ctrl ? MOD_CONTROL : 0) | (shift ? MOD_SHIFT : 0) | (alt ? MOD_ALT : 0);
    if (modsOut) *modsOut = m;
    return NomeTecla(m, vk);
}

// ------------------------------------------------- gravar a tecla do overlay do ReShade
// A tecla do ReShade NAO e hotkey nossa: mora no ReShade.ini, e quem a le e o ReShade.
// Poder escolhe-la aqui e o pedido do usuario ("configurar com qual botao queremos abrir o
// ReShade base tambem"), e o lugar certo e o mesmo dialogo das outras teclas - a pessoa nao
// tem por que saber que uma delas mora noutro arquivo.
//
// QUANDO O RESHADE LE ISSO, medido no ReShade.log desta maquina e nao suposto:
//   12:40:20:404  Initializing crosire's ReShade ... loaded from 'dxgi.dll'   <- antes de nos
//   12:40:20:454  (o nosso log) "DLSS5 For All 1.1 iniciando"                 <- o seletor e aqui
//   12:40:22:284  Recreated runtime environment on runtime ... ('ReShade.ini')
// A ultima linha cita o proprio ReShade.ini no instante em que o effect_runtime sobe, que e
// o primeiro swapchain - e o swapchain so e criado DEPOIS de a pessoa fechar o seletor.
// Entao a gravacao feita aqui entra a tempo e vale ja nesta abertura. O cartaz do botao
// "Abrir o ReShade" ainda carrega uma linha de escape enquanto a tecla foi trocada nesta
// sessao (g_teclaOverlayMudada): se a conclusao acima estiver errada em alguma versao do
// ReShade, a pessoa le o que fazer em vez de ficar apertando uma tecla morta.
//
// O ReShade.ini nao tem BOM (conferido: comeca em "[ADDON]"), entao o
// WritePrivateProfileStringW o enxerga e reescreve APENAS esta chave, preservando o resto -
// que e o motivo de nao usarmos escrita de arquivo inteiro aqui.
static bool g_teclaOverlayMudada = false;

static std::wstring ReShadeIniPath() { return g_exeDir + L"ReShade.ini"; }

// Uma copia por sessao, antes da primeira gravacao. O ReShade.ini e do usuario e tem
// presets, tecnicas e a configuracao inteira do ReShade dentro: mexer nele sem rede seria
// abusar da confianca de quem so queria trocar uma tecla.
static void BackupReShadeIniUmaVez() {
    static bool feito = false;
    if (feito) return;
    feito = true;
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t suf[64];
    _snwprintf(suf, 64, L".bak-keyoverlay-%04u%02u%02u-%02u%02u",
               t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    const std::wstring destino = ReShadeIniPath() + suf;
    // TRUE = nao sobrescreve: se ja ha um backup deste minuto, ele e tao bom quanto
    const BOOL ok = CopyFileW(ReShadeIniPath().c_str(), destino.c_str(), TRUE);
    Log("ReShade.ini: backup %s (%s)", U8(destino).c_str(),
        ok ? "criado" : "ja existia ou falhou - a gravacao segue");
}

// Grava [INPUT] KeyOverlay no formato do ReShade: "vk,ctrl,shift,alt", 0/1 em cada flag.
static bool GravarTeclaOverlayReShade(UINT vk, UINT mods) {
    if (!vk || vk > 0xFF) return false;
    BackupReShadeIniUmaVez();
    wchar_t v[64];
    _snwprintf(v, 64, L"%u,%u,%u,%u", vk,
               (mods & MOD_CONTROL) ? 1u : 0u,
               (mods & MOD_SHIFT)   ? 1u : 0u,
               (mods & MOD_ALT)     ? 1u : 0u);
    const BOOL ok = WritePrivateProfileStringW(L"INPUT", L"KeyOverlay", v, ReShadeIniPath().c_str());
    Log("ReShade.ini: KeyOverlay=%s (%s)", U8(v).c_str(), ok ? "gravado" : "FALHOU");
    if (ok) g_teclaOverlayMudada = true;
    return ok != 0;
}

// ---------------------------------------- o contador de FPS que aparece no canto da tela
// ELE NAO E NOSSO. E o contador do overlay do ReShade ([OVERLAY] ShowFPS), que aparece
// junto com o filtro e por isso todo mundo acha que e do app. O usuario pediu para poder
// desligar, e o lugar de mexer e o mesmo do KeyOverlay: o ReShade.ini, com o mesmo backup.
static const wchar_t* kSecOverlayRs = L"OVERLAY";
static const wchar_t* kChaveFpsRs   = L"ShowFPS";

// -1 = a chave nao existe (nunca foi gravada); 0/1 = o que esta la agora.
// O padrao de fabrica do ReShade e LIGADO, e e o que fica valendo quando ninguem mexeu -
// mudar o comportamento de quem nao pediu nada seria a pior forma de atender ao pedido.
static int FpsNaTelaNoReShadeIni() {
    wchar_t b[16]{};
    GetPrivateProfileStringW(kSecOverlayRs, kChaveFpsRs, L"", b, 16, ReShadeIniPath().c_str());
    if (!b[0]) return -1;
    return (b[0] == L'0') ? 0 : 1;
}

// O RESHADE EXPORTA A PROPRIA API DE CONFIGURACAO, e isso resolve um problema que a
// escrita no arquivo sozinha nao resolve: o ReShade mantem o ini em cache e o regrava
// sozinho (AutoSave). Escrever so no arquivo corre o risco de ser desfeito pelo proximo
// AutoSave, com o valor velho que ele tem em memoria.
//
// Conferido nos exports do dxgi.dll desta instalacao (objdump -p): ReShadeSetConfigValue
// esta la, ao lado de ReShadeGetConfigValue e ReShadeRegisterAddon. A assinatura vem de
// addon/include/reshade/reshade.hpp:
//     void ReShadeSetConfigValue(void *module, effect_runtime *runtime,
//                                const char *section, const char *key, const char *value)
// runtime = nullptr significa "o ReShade.ini global", que e exatamente o nosso caso.
//
// O 'dxgi.dll' do SISTEMA tambem se chama dxgi.dll; se o ReShade nao estiver instalado, o
// GetModuleHandleW acha o do sistema, o GetProcAddress devolve nulo e caimos fora sem
// estrago. d3d11.dll entra na busca porque o README manda renomear para ele quando o
// dxgi.dll nao carrega.
typedef void (*PFN_RsSetConfig)(void*, void*, const char*, const char*, const char*);
static bool PedirConfigAoReShade(const char* secao, const char* chave, const char* valor) {
    static const wchar_t* kModulos[] = { L"dxgi.dll", L"d3d11.dll" };
    for (const wchar_t* nome : kModulos) {
        HMODULE m = GetModuleHandleW(nome);
        if (!m) continue;
        PFN_RsSetConfig f = reinterpret_cast<PFN_RsSetConfig>(
            reinterpret_cast<void*>(GetProcAddress(m, "ReShadeSetConfigValue")));
        if (!f) continue;
        f(nullptr, nullptr, secao, chave, valor);
        Log("ReShade: SetConfigValue [%s] %s = %s (via %s)", secao, chave, valor, U8(nome).c_str());
        return true;
    }
    return false;
}

// Grava nos DOIS caminhos, de proposito e nesta ordem: primeiro o arquivo (que vale mesmo
// que o ReShade nao esteja carregado, e e o que a proxima abertura vai ler), depois a API
// (que deixa o cache em memoria do ReShade coerente, para o AutoSave dele nao desfazer o
// que acabamos de escrever). Nao devolve "o contador sumiu da tela": quem redesenha o
// overlay e o ReShade, e ele so releu esse valor com certeza na proxima abertura.
static bool DefinirFpsNaTela(bool ligado) {
    BackupReShadeIniUmaVez();
    const wchar_t* v = ligado ? L"1" : L"0";
    const BOOL ok = WritePrivateProfileStringW(kSecOverlayRs, kChaveFpsRs, v, ReShadeIniPath().c_str());
    PedirConfigAoReShade("OVERLAY", "ShowFPS", ligado ? "1" : "0");
    Log("FPS na tela: %s (ReShade.ini %s)", ligado ? "ligado" : "desligado",
        ok ? "gravado" : "FALHOU");
    return ok != 0;
}

enum { ID_KIND = 100, ID_DEV = 101, ID_MODE = 102, ID_FS = 103, ID_START = 104,
       ID_REFRESH = 105, ID_CURSOR = 106, ID_HIDE = 107, ID_OVERLAY = 108,
       ID_VSYNC = 109, ID_HDR = 110, ID_ATALHOS = 111, ID_FPSTELA = 112,
       ID_LANG_PT = 113, ID_LANG_EN = 114,
       ID_DLSS_BAIXAR = 115, ID_DLSS_PASTA = 116 };

// =====================================================================================
//  A FAIXA DE "FALTAM OS ARQUIVOS DO DLSS 5"
//
//  O relato que a criou: quem instala e nao tem os dois arquivos abre o programa, ve uma
//  imagem identica a original e conclui que o app nao funciona. Ele funciona - so que sem
//  o neural, que e justamente a razao de ele existir -, e ate aqui NADA na tela dizia
//  isso. O silencio e o defeito.
//
//  Os dois arquivos nao vem no pacote de proposito (ver CREDITOS.md): sao codigo da
//  NVIDIA/RenoDX que este projeto nao redistribui. O que faltava era a pessoa DESCOBRIR
//  isso, e e so o que a faixa faz - ela informa, nao impede: o INICIAR continua valendo.
// =====================================================================================
struct FaltaDlss {
    bool addon = false;     // renodx-dlss5.addon64
    bool dll   = false;     // nvngx_dlssnr.dll
    bool reshade = false;   // dxgi.dll: sem ele a cadeia inteira nao sobe, e a causa e outra
    bool AlgumaCoisa() const { return addon || dll || reshade; }
};
static bool ArquivoExiste(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}
// Sempre ao lado do EXE, e nao em g_dadosDir: quem le esses arquivos e o ReShade, que so
// olha para a pasta do proprio processo.
static FaltaDlss VerFaltaDlss() {
    FaltaDlss f;
    f.addon   = !ArquivoExiste(g_exeDir + L"renodx-dlss5.addon64");
    f.dll     = !ArquivoExiste(g_exeDir + L"nvngx_dlssnr.dll");
    // O dxgi.dll pode ter sido instalado com outro nome (d3d11.dll e a alternativa que o
    // proprio README ensina quando o dxgi nao carrega). Aceitar os dois evita acusar
    // instalacao correta.
    f.reshade = !ArquivoExiste(g_exeDir + L"dxgi.dll") && !ArquivoExiste(g_exeDir + L"d3d11.dll");
    return f;
}
static const wchar_t* kUrlRhi = L"https://github.com/RankFTW/RHI/releases";
// Ambar de aviso, no espirito da barra amarela do proprio Windows: chama atencao sem
// gritar "erro" - nada aqui esta quebrado, esta faltando.
static const COLORREF kCorFaixaFundo = RGB(255, 244, 214);
static const COLORREF kCorFaixaTexto = RGB( 74,  53,   0);

// Trocar o idioma no seletor RECRIA o seletor, e essa foi a escolha barata e certa: sao
// duas dezenas de controles Win32 com texto proprio (rotulos, itens de combo, a dica de
// cinco linhas, o titulo da janela), e re-setar um por um deixaria alguns para tras a cada
// controle novo que alguem acrescentasse. Fechar e reabrir nao perde nada - a janela ainda
// nem escolheu a fonte - e nao ha estado a preservar alem do que ja mora no ini.
static bool g_selTrocouIdioma = false;

struct SelState {
    std::vector<Device>*   devs = nullptr;
    std::vector<WinTarget> wins;
    std::vector<MonTarget> mons;
    HWND hKind = nullptr, hDev = nullptr, hMode = nullptr, hFs = nullptr;
    HWND hCursor = nullptr, hHide = nullptr, hOverlay = nullptr, hStart = nullptr, hHint = nullptr;
    HWND hLblDev = nullptr, hLblMode = nullptr, hVsync = nullptr, hHdr = nullptr, hAtalhos = nullptr;
    HWND hFps = nullptr;   // "Mostrar FPS na tela" - vai para o ReShade.ini, nao para o nosso
    HWND hLangPt = nullptr, hLangEn = nullptr;   // idioma da interface, os dois a mostra
    // a faixa de "faltam os arquivos do DLSS 5" e a linha de botoes que ela empurra
    HWND hAvisoTxt = nullptr, hAvisoBaixar = nullptr, hAvisoPasta = nullptr;
    HWND hRefresh = nullptr, hBtAtalhos = nullptr;
    bool avisoVisivel = false;   // o que esta na tela agora (para so mexer quando muda)
    UINT dpi = 96;               // o layout e escrito em 96 dpi; a escala entra na criacao
    HBRUSH hbrAviso = nullptr;   // o ambar da faixa, pintado no WM_CTLCOLORSTATIC
    Choice ch; bool ok = false;
    std::wstring wantDev, wantMode, wantWin, wantMon;   // ultimas escolhas (ini)
    bool primeiraVez = false;   // sem nada salvo no ini: o padrao novo vale
};

static SourceKind SelKind(SelState* s) {
    int k = (int)SendMessageW(s->hKind, CB_GETCURSEL, 0, 0);
    // 0 = Monitor (padrao), 1 = Janela, 2 = Placa/camera. Ver o CB_ADDSTRING.
    return (k == 1) ? SRC_WINDOW : (k == 2) ? SRC_CAMERA : SRC_MONITOR;
}

static void FillModes(SelState* s) {
    SendMessageW(s->hMode, CB_RESETCONTENT, 0, 0);
    SourceKind k = SelKind(s);
    int di = (int)SendMessageW(s->hDev, CB_GETCURSEL, 0, 0);

    if (k == SRC_WINDOW) {
        SetWindowTextW(s->hLblMode, T(L"Recorte:", L"Crop:"));
        SendMessageW(s->hMode, CB_ADDSTRING, 0, (LPARAM)T(L"Área útil da janela (sem barra de título / bordas)",
                                                          L"Window client area (no title bar / borders)"));
        SendMessageW(s->hMode, CB_ADDSTRING, 0, (LPARAM)T(L"Janela inteira (com moldura)",
                                                          L"Whole window (frame included)"));
        SendMessageW(s->hMode, CB_SETCURSEL, 0, 0);
        EnableWindow(s->hMode, TRUE);
        EnableWindow(s->hStart, di >= 0 && di < (int)s->wins.size());
        // CINCO linhas. O campo tem 100 px e Segoe UI 15 cabe cinco: a sexta linha do texto
        // antigo - justamente a que responde "posso gravar isso?" - nunca aparecia na tela.
        //
        // A PRIMEIRA linha e a que faltava quando esta fonte saiu do seletor: sem ela, a
        // pessoa nao tem como saber que escolher a janela DEIXA O FILTRO MAIS LEVE, e a
        // opcao parece so um jeito mais trabalhoso de fazer a mesma coisa.
        SetWindowTextW(s->hHint, T(
            L"MAIS LEVE QUE O MONITOR: a rede processa só a área da janela. Um jogo em 2560x1080\n"
            L"são 2,8 milhões de pixels por quadro contra 5,0 de uma tela 3440x1440 — metade da conta.\n"
            L"SOBREPOR marcado = a nossa janela, sem borda, colada em cima do jogo; mouse e teclado\n"
            L"passam direto pra ele — parece filtro dentro do jogo. Desmarcado = janela separada.\n"
            L"Nos dois casos o OBS grava a nossa janela (Captura de Janela). Só janela / borderless.",
            L"LIGHTER THAN THE MONITOR: the network only processes the window's area. A game at 2560x1080\n"
            L"is 2.8 million pixels per frame against 5.0 on a 3440x1440 screen — about half the work.\n"
            L"OVERLAY checked = our window, borderless, glued on top of the game; mouse and keyboard go\n"
            L"straight through to it — it looks like a filter inside the game. Unchecked = separate window.\n"
            L"Either way OBS records our window (Window Capture). Windowed / borderless games only."));
        return;
    }
    if (k == SRC_MONITOR) {
        SetWindowTextW(s->hLblMode, T(L"Recorte:", L"Crop:"));
        SendMessageW(s->hMode, CB_ADDSTRING, 0, (LPARAM)T(L"Monitor inteiro", L"Whole monitor"));
        SendMessageW(s->hMode, CB_SETCURSEL, 0, 0);
        EnableWindow(s->hMode, FALSE);
        EnableWindow(s->hStart, di >= 0 && di < (int)s->mons.size());
        // CINCO linhas, pelo mesmo motivo do ramo de cima: a sexta nao aparece na tela.
        SetWindowTextW(s->hHint, T(
            L"SOBREPOR desmarcado = JANELA SEPARADA com a tela inteira dentro dela, filtrada.\n"
            L"O desktop continua normal e o OBS grava essa janela sem problema.\n"
            L"SOBREPOR marcado = a camada cobre o monitor e TUDO nele passa pelo DLSS 5; o mouse e o\n"
            L"teclado continuam indo pro que está embaixo. Se nada mudar na tela por muito tempo, a\n"
            L"imagem pode parar (a camada tapa a fonte) — o app percebe e devolve o original.",
            L"OVERLAY unchecked = a SEPARATE WINDOW with the whole screen inside it, filtered.\n"
            L"Your desktop stays normal and OBS records that window with no trouble.\n"
            L"OVERLAY checked = the layer covers the monitor and EVERYTHING on it goes through DLSS 5;\n"
            L"mouse and keyboard still reach whatever is underneath. If nothing changes on screen for a\n"
            L"while the image can freeze (the layer covers its own source) — the app notices and backs off."));
        return;
    }

    SetWindowTextW(s->hLblMode, T(L"Modo (resolução / fps / formato):", L"Mode (resolution / fps / format):"));
    EnableWindow(s->hMode, TRUE);
    if (di < 0 || di >= (int)s->devs->size()) {
        // O return saia antes de qualquer SetWindowTextW(hHint): quem so tem teclado e
        // monitor escolhia "Placa de captura", o combo ficava vazio, o INICIAR apagava e a
        // dica na tela continuava sendo a do modo ANTERIOR, falando de SOBREPOR e de janela.
        // O app parecia travado, sem uma palavra sobre o que fazer.
        EnableWindow(s->hStart, FALSE);
        SetWindowTextW(s->hHint, T(
            L"Nenhuma placa de captura ou câmera foi encontrada neste PC.\n"
            L"Se ela está ligada agora, clique em \"Atualizar lista\".\n"
            L"Para filtrar um jogo que roda nesta máquina, troque a Fonte para \"Janela\" ali em cima.",
            L"No capture card or camera was found on this PC.\n"
            L"If it is plugged in right now, click \"Refresh list\".\n"
            L"To filter a game running on this machine, switch Source to \"Window\" up there."));
        return;
    }
    Device& d = (*s->devs)[di];
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    LoadModes(d);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    int sel = -1, best = BestMode(d.modes);
    for (size_t i = 0; i < d.modes.size(); i++) {
        SendMessageW(s->hMode, CB_ADDSTRING, 0, (LPARAM)d.modes[i].label.c_str());
        // Sem o sufixo "(comprimido)" / "(compressed)", pelo mesmo motivo do MesmoMonitor:
        // ele e a unica parte do rotulo que muda com o idioma, e a escolha salva no ini
        // nao pode se perder so porque a pessoa trocou de lingua.
        auto semSufixo = [](const std::wstring& s) {
            const size_t k = s.find(L"  (");
            return (k == std::wstring::npos) ? s : s.substr(0, k);
        };
        if (!s->wantMode.empty() && semSufixo(d.modes[i].label) == semSufixo(s->wantMode)) sel = (int)i;
    }
    if (sel < 0) sel = best;
    if (sel >= 0) SendMessageW(s->hMode, CB_SETCURSEL, sel, 0);
    EnableWindow(s->hStart, !d.modes.empty());
    const std::wstring dica = d.modes.empty()
        ? TS(L"Esse dispositivo não expôs nenhum modo de vídeo (em uso por outro app? "
             L"Feche OBS/Discord/navegador e clique em \"Atualizar lista\").",
             L"This device exposed no video mode at all (in use by another app? "
             L"Close OBS/Discord/your browser and click \"Refresh list\").")
        : (TS(L"Dica: 1920x1080 é o ponto ideal na 4080 — o Feeder roda em DLAA na resolução da captura, então 4K = 4x a carga.\n"
              L"Prefira formato não comprimido (YUY2/NV12) se tiver. Depois de abrir: ",
              L"Tip: 1920x1080 is the sweet spot on a 4080 — the Feeder runs DLAA at the capture resolution, so 4K = 4x the load.\n"
              L"Prefer an uncompressed format (YUY2/NV12) when there is one. Once it opens: ")
           + RodapeAtalhos() + T(L", F11 tela cheia.", L", F11 for fullscreen."));
    SetWindowTextW(s->hHint, dica.c_str());
}

// O rotulo do monitor e "\\.\DISPLAY2  (principal)  1920x1080", e o que fica no ini e ele
// inteiro. So que a palavra do meio muda com o idioma - entao comparar a linha crua faria
// quem trocasse de portugues para ingles perder o monitor escolhido, sem explicacao. O
// nome do device (ate o primeiro espaco) e a parte que identifica de verdade.
static bool MesmoMonitor(const std::wstring& a, const std::wstring& b) {
    if (a == b) return true;
    const size_t ka = a.find(L' '), kb = b.find(L' ');
    if (ka == std::wstring::npos || kb == std::wstring::npos) return false;
    return ka == kb && a.compare(0, ka, b, 0, kb) == 0;
}

// ------------------------------------------------------- o texto da faixa do DLSS 5
//
// DIZER QUAL FALTA, e nao "faltam arquivos": meio caminho andado (um dos dois no lugar) e
// o caso mais confuso de diagnosticar, e e justamente o que acontece com quem copiou so o
// que achou. Os nomes vao exatos, para a pessoa poder procurar por eles.
// CADA LINHA TEM DE CABER EM UMA SO. O STATIC quebra sozinho o que passa dos 604 px, e
// cada quebra a mais empurra a ultima frase para fora da faixa - foi o que aconteceu na
// primeira versao, e a frase perdida era justamente a que diz onde baixar. Medido na tela:
// cabem cerca de 80 caracteres por linha, e sao quatro linhas no pior caso (sem o ReShade
// E sem os dois arquivos).
static std::wstring TextoFaltaDlss(const FaltaDlss& f) {
    const bool falta2 = (f.addon && f.dll);
    std::wstring t;
    if (f.reshade) {
        // Sem o ReShade a cadeia inteira nao sobe, e a causa e outra: nao adianta mandar
        // baixar os arquivos do neural antes de resolver isto.
        t = T(L"O ReShade não está nesta pasta: rode o setup-dlss5.ps1 antes — sem ele nada sobe.",
              L"ReShade is not in this folder: run setup-dlss5.ps1 first — nothing runs without it.");
        if (f.addon || f.dll) {
            t += T(L"\nTambém falta aqui: ", L"\nAlso missing here: ");
            t += falta2 ? (L"renodx-dlss5.addon64 " + TS(L"e", L"and") + L" nvngx_dlssnr.dll")
                        : (f.addon ? L"renodx-dlss5.addon64" : L"nvngx_dlssnr.dll");
            t += L".";
        }
    } else {
        t = TS(falta2 ? L"O DLSS 5 não vai ligar: faltam " : L"O DLSS 5 não vai ligar: falta ",
               L"DLSS 5 will not turn on: ");
        t += falta2 ? (L"renodx-dlss5.addon64 " + TS(L"e", L"and") + L" nvngx_dlssnr.dll")
                    : (f.addon ? L"renodx-dlss5.addon64" : L"nvngx_dlssnr.dll");
        t += T(L" aqui.", falta2 ? L" are missing." : L" is missing.");
    }
    t += T(L"\nO app abre e mostra a imagem do mesmo jeito — só não faz o realce do DLSS 5.",
           L"\nThe app still opens and shows the image — it just will not do the DLSS 5 uplift.");
    if (f.addon || f.dll)
        t += TS(falta2 ? L"\nNão vêm no pacote: são da comunidade RenoDX, e o instalador RHI baixa tudo."
                       : L"\nNão vem no pacote: é da comunidade RenoDX, e o instalador RHI baixa tudo.",
                falta2 ? L"\nNot in the package: from the RenoDX community — the RHI installer gets them for you."
                       : L"\nNot in the package: from the RenoDX community — the RHI installer gets it for you.");
    return t;
}

// ------------------------------------------------------- layout do seletor com a faixa
// Todo o seletor esta escrito em unidades de 96 dpi; estas tres sao as unicas coordenadas
// que a faixa precisa compartilhar com quem cria os controles.
static const int kSelW = 660, kSelH = 526;   // janela sem a faixa
static const int kBotoesY = 462;             // a linha "Atualizar lista ... INICIAR"
// A faixa: um retangulo so, e as coordenadas de dentro dela derivam destas duas. O
// WM_ERASEBKGND pinta exatamente este retangulo - se divergirem, sobra ambar por fora.
static const int kFaixaY = 458, kFaixaH = 122;
static const int kAvisoDy = kFaixaH + 8;     // quanto a faixa empurra a linha de botoes

// Mostra, esconde e reescreve a faixa conforme o que existe no disco AGORA. Chamada na
// criacao e a cada vez que o seletor recupera o foco - e o que faz o aviso sumir sozinho
// depois de a pessoa instalar o RHI, sem reabrir o programa.
//
// Os controles da faixa existem SEMPRE (criados escondidos quando nao ha o que dizer):
// assim isto funciona nos dois sentidos, e um arquivo que suma com o seletor aberto
// tambem aparece. Reposicionar e mais trabalho do que recriar a janela, mas recriar
// perderia o alvo que a pessoa ja tivesse escolhido no combo.
static void AplicarAvisoDlss(SelState* s, HWND h) {
    if (!s || !s->hAvisoTxt) return;
    const FaltaDlss f = VerFaltaDlss();
    const bool mostrar = f.AlgumaCoisa();
    if (mostrar) SetWindowTextW(s->hAvisoTxt, TextoFaltaDlss(f).c_str());
    if (mostrar == s->avisoVisivel) return;   // nada mudou: nao mexe na janela a toa
    s->avisoVisivel = mostrar;
    Log("seletor: aviso do DLSS 5 %s (addon=%d dll=%d reshade=%d)",
        mostrar ? "mostrado" : "escondido", (int)f.addon, (int)f.dll, (int)f.reshade);

    auto E = [s](int v) { return MulDiv(v, (int)s->dpi, 96); };
    const int cmd = mostrar ? SW_SHOW : SW_HIDE;
    for (HWND c : { s->hAvisoTxt, s->hAvisoBaixar, s->hAvisoPasta })
        if (c) ShowWindow(c, cmd);

    const int y = kBotoesY + (mostrar ? kAvisoDy : 0);
    const struct { HWND c; int x; } linha[] = {
        { s->hRefresh, 16 }, { s->hBtAtalhos, 174 }, { s->hAtalhos, 302 }, { s->hStart, 504 },
    };
    for (const auto& b : linha)
        if (b.c) SetWindowPos(b.c, nullptr, E(b.x), E(y), 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    RECT r{ 0, 0, E(kSelW), E(kSelH + (mostrar ? kAvisoDy : 0)) };
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    SetWindowPos(h, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
    // RDW_ALLCHILDREN, e nao InvalidateRect: invalidar so o pai apaga o fundo por cima dos
    // controles e nao pede repintura a eles - a janela fica com os botoes sumidos ate algo
    // mais tocar neles.
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// -1 = nada salvo, 0 = desmarcou da ultima vez, 1 = marcou. Lido do ini antes do seletor.
static int g_wantOverlay = -1;
static void FillTargets(SelState* s, HWND self) {
    SendMessageW(s->hDev, CB_RESETCONTENT, 0, 0);
    SourceKind k = SelKind(s);
    int sel = 0;
    if (k == SRC_CAMERA) {
        SetWindowTextW(s->hLblDev, T(L"Placa de captura / câmera / console:", L"Capture card / camera / console:"));
        for (size_t i = 0; i < s->devs->size(); i++) {
            SendMessageW(s->hDev, CB_ADDSTRING, 0, (LPARAM)(*s->devs)[i].name.c_str());
            if (!s->wantDev.empty() && (*s->devs)[i].name == s->wantDev) sel = (int)i;
        }
    } else if (k == SRC_WINDOW) {
        SetWindowTextW(s->hLblDev, T(L"Janela (jogo, emulador, player...):", L"Window (game, emulator, player...):"));
        s->wins = EnumCaptureWindows(self);
        for (size_t i = 0; i < s->wins.size(); i++) {
            SendMessageW(s->hDev, CB_ADDSTRING, 0, (LPARAM)s->wins[i].label.c_str());
            if (!s->wantWin.empty() && s->wins[i].title == s->wantWin) sel = (int)i;
        }
    } else {
        SetWindowTextW(s->hLblDev, L"Monitor:");   // igual nos dois idiomas
        s->mons = EnumCaptureMonitors();
        for (size_t i = 0; i < s->mons.size(); i++) {
            SendMessageW(s->hDev, CB_ADDSTRING, 0, (LPARAM)s->mons[i].label.c_str());
            if (!s->wantMon.empty() && MesmoMonitor(s->mons[i].label, s->wantMon)) sel = (int)i;
        }
    }
    SendMessageW(s->hDev, CB_SETCURSEL, sel, 0);

    bool screen = (k != SRC_CAMERA);
    EnableWindow(s->hCursor, screen);
    EnableWindow(s->hHide, screen);
    EnableWindow(s->hOverlay, screen);   // sobrepor vale pra janela e pra monitor
    if (!screen) SendMessageW(s->hOverlay, BM_SETCHECK, BST_UNCHECKED, 0);
    if (k == SRC_MONITOR) SendMessageW(s->hHide, BM_SETCHECK, BST_CHECKED, 0);
    if (k == SRC_WINDOW)  SendMessageW(s->hHide, BM_SETCHECK, BST_UNCHECKED, 0);
    // SOBREPOR vem MARCADO por padrao pra janela: so fica desmarcado se a pessoa desmarcou
    // da ultima vez (ini sobrepor=0). No monitor o padrao e desmarcado (sobrepor la forca
    // esconder a janela das capturas e o OBS deixa de gravar), a menos que tenha salvo =1.
    if (k == SRC_WINDOW)  SendMessageW(s->hOverlay, BM_SETCHECK, (g_wantOverlay != 0) ? BST_CHECKED : BST_UNCHECKED, 0);
    if (k == SRC_MONITOR) SendMessageW(s->hOverlay, BM_SETCHECK, (g_wantOverlay == 1) ? BST_CHECKED : BST_UNCHECKED, 0);
    FillModes(s);
}

// =====================================================================================
//  Diálogo de atalhos do seletor
//
//  A configuracao de teclas precisa existir ANTES de escolher a fonte - a pessoa abre o
//  app e ja quer saber (e mudar) como vai controla-lo. Mesmo mapa e mesmo ini da secao
//  ATALHOS do painel: uma fonte de verdade, dois lugares para editar.
//  Win32 puro de proposito: o seletor tambem e, e aqui vale funcionar e ficar claro.
// =====================================================================================
enum { IDD_AT_MUDAR = 300, IDD_AT_PADRAO = 320, IDD_AT_FECHAR = 321, IDD_AT_RESHADE = 322 };
// A tecla do ReShade e a (AT_COUNT + 1)-esima linha do dialogo, mas NAO e uma acao nossa:
// ela mora no ReShade.ini. CAP_RESHADE e o valor de 'capturando' que a representa - uma
// linha a mais na tela, um arquivo a mais no fundo.
enum { CAP_RESHADE = AT_COUNT };
// Tecla de fabrica do overlay do ReShade: Home (VK_HOME = 36), sem modificadores. E o
// padrao do proprio ReShade e o que o "Restaurar padroes" devolve.
static const UINT kVkOverlayPadrao = VK_HOME;

struct AtalhoDlg {
    HWND lbl[AT_COUNT]{}, bt[AT_COUNT]{};
    HWND lblRs = nullptr, btRs = nullptr;   // a linha do ReShade
    HWND aviso = nullptr;
    int  capturando = -1;
};
static void AtalhoDlgAtualiza(AtalhoDlg* d) {
    for (int i = 0; i < AT_COUNT; i++) {
        std::wstring t = std::wstring(RotuloAtalho(i)) + L":   " +
                         (d->capturando == i ? TS(L"aperte a tecla...  (Esc cancela)",
                                                  L"press the key...  (Esc cancels)")
                                             : TextoAtalho((AcaoAtalho)i));
        if (g_atalhos[i].vk && !g_atalhos[i].registrado && d->capturando != i)
            t += T(L"   (em uso por outro programa)", L"   (taken by another program)");
        SetWindowTextW(d->lbl[i], t.c_str());
        EnableWindow(d->bt[i], d->capturando < 0);
    }
    if (d->lblRs) {
        std::wstring t = TS(L"Abrir o ReShade (overlay):   ", L"Open ReShade (overlay):   ") +
                         (d->capturando == CAP_RESHADE
                              ? TS(L"aperte a tecla...  (Esc cancela)", L"press the key...  (Esc cancels)")
                              : TeclaOverlayReShade());
        SetWindowTextW(d->lblRs, t.c_str());
        EnableWindow(d->btRs, d->capturando < 0);
    }
}
static LRESULT CALLBACK AtalhoDlgProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    AtalhoDlg* d = (AtalhoDlg*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (!d) break;
        if (id >= IDD_AT_MUDAR && id < IDD_AT_MUDAR + AT_COUNT) {
            d->capturando = id - IDD_AT_MUDAR;
            SuspenderAtalhos();          // a tecla tem de chegar aqui, nao virar hotkey
            SetWindowTextW(d->aviso, T(L"Aperte a combinacao desejada (Ctrl/Alt/Shift + tecla). Esc cancela.",
                                       L"Press the combination you want (Ctrl/Alt/Shift + key). Esc cancels."));
            AtalhoDlgAtualiza(d);
            SetFocus(h);
            return 0;
        }
        if (id == IDD_AT_RESHADE) {
            d->capturando = CAP_RESHADE;
            SuspenderAtalhos();          // a tecla tem de chegar aqui, nao virar hotkey
            SetWindowTextW(d->aviso, T(L"Aperte a tecla que vai abrir o ReShade (Ctrl/Alt/Shift + tecla). Esc cancela.",
                                       L"Press the key that will open ReShade (Ctrl/Alt/Shift + key). Esc cancels."));
            AtalhoDlgAtualiza(d);
            SetFocus(h);
            return 0;
        }
        if (id == IDD_AT_PADRAO) {
            WritePrivateProfileStringW(L"atalhos", nullptr, nullptr, IniPath().c_str());
            CarregarAtalhos();
            RegistrarTodosAtalhos();
            // O padrao de fabrica do ReShade tambem volta: a linha esta no mesmo dialogo, e
            // "restaurar padroes" que deixasse uma delas como estava seria meia verdade.
            GravarTeclaOverlayReShade(kVkOverlayPadrao, 0);
            SetWindowTextW(d->aviso, T(L"Atalhos de fábrica restaurados (o ReShade voltou para Home).",
                                       L"Default shortcuts restored (ReShade is back on Home)."));
            AtalhoDlgAtualiza(d);
            return 0;
        }
        if (id == IDD_AT_FECHAR) { DestroyWindow(h); return 0; }
        break;
    }
    case WM_KEYDOWN: {
        if (!d || d->capturando < 0) break;
        if (wp == VK_ESCAPE) {
            d->capturando = -1; RetomarAtalhos();
            SetWindowTextW(d->aviso, L"");
            AtalhoDlgAtualiza(d);
            return 0;
        }
        if (wp == VK_CONTROL || wp == VK_MENU || wp == VK_SHIFT) return 0;
        UINT mods = 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
        if (GetKeyState(VK_MENU)    & 0x8000) mods |= MOD_ALT;
        if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= MOD_SHIFT;
        const int acao = d->capturando;
        d->capturando = -1;
        // ---- A TECLA DO RESHADE, que vai para outro arquivo e nao vira hotkey nossa.
        // A recusa por colisao acontece AQUI, e nao como aviso no log depois de aberto: o
        // padrao de fabrica e End para o painel e Home para o ReShade, e quem escolhesse End
        // aqui ficaria com duas coisas na mesma tecla brigando em silencio - o painel abre e
        // o overlay do ReShade tambem, na mesma tecla, cada um comendo o clique do outro.
        if (acao == CAP_RESHADE) {
            int colide = -1;
            for (int i = 0; i < AT_COUNT; i++)
                if (g_atalhos[i].vk == (UINT)wp && g_atalhos[i].mods == mods) colide = i;
            if (colide >= 0) {
                SetWindowTextW(d->aviso, (NomeTecla(mods, (UINT)wp) +
                                          TS(L" já é de \"", L" already belongs to \"") +
                                          RotuloAtalho(colide) +
                                          TS(L"\" aqui no app — escolha outra para o ReShade.",
                                             L"\" here in the app — pick another one for ReShade.")).c_str());
            } else if (GravarTeclaOverlayReShade((UINT)wp, mods)) {
                SetWindowTextW(d->aviso,
                    (TS(L"O ReShade passa a abrir com ", L"ReShade now opens with ") + NomeTecla(mods, (UINT)wp) +
                     TS(L" (vale já nesta abertura).", L" (already in effect this session).")).c_str());
            } else {
                SetWindowTextW(d->aviso,
                    T(L"Não consegui gravar no ReShade.ini — a pasta do app aceita escrita?",
                      L"Could not write to ReShade.ini — is the app folder writable?"));
            }
            RetomarAtalhos();
            AtalhoDlgAtualiza(d);
            return 0;
        }
        // A recusa vale nos DOIS sentidos: escolher para o painel a tecla que ja abre o
        // ReShade da na mesma briga que o contrario, e quem esta na tela agora e a unica
        // pessoa capaz de decidir qual das duas muda.
        {
            UINT vkRs = 0, modsRs = 0;
            TeclaOverlayReShade(&vkRs, &modsRs);
            if (vkRs && vkRs == (UINT)wp && modsRs == mods) {
                SetWindowTextW(d->aviso, (NomeTecla(mods, (UINT)wp) +
                    TS(L" já abre o ReShade — escolha outra, ou mude a tecla do ReShade abaixo.",
                       L" already opens ReShade — pick another one, or change ReShade's key below.")).c_str());
                RetomarAtalhos();
                AtalhoDlgAtualiza(d);
                return 0;
            }
        }
        int conflito = -1;
        for (int i = 0; i < AT_COUNT; i++)
            if (i != acao && g_atalhos[i].vk == (UINT)wp && g_atalhos[i].mods == mods) conflito = i;
        if (conflito >= 0) {
            SetWindowTextW(d->aviso, (NomeTecla(mods, (UINT)wp) + TS(L" ja e de \"", L" already belongs to \"") +
                                      RotuloAtalho(conflito) + TS(L"\" - escolha outra.", L"\" - pick another one.")).c_str());
        } else {
            const UINT mA = g_atalhos[acao].mods, vA = g_atalhos[acao].vk;
            g_atalhos[acao].mods = mods; g_atalhos[acao].vk = (UINT)wp;
            if (RegistrarAtalho((AcaoAtalho)acao)) {
                GravarAtalho((AcaoAtalho)acao);
                if (acao == AT_PAINEL) g_hkPanel = TextoAtalho(AT_PAINEL);
                SetWindowTextW(d->aviso, L"");
            } else {
                g_atalhos[acao].mods = mA; g_atalhos[acao].vk = vA;
                RegistrarAtalho((AcaoAtalho)acao);
                SetWindowTextW(d->aviso, (NomeTecla(mods, (UINT)wp) +
                                          TS(L" esta em uso por outro programa.",
                                             L" is taken by another program.")).c_str());
            }
        }
        RetomarAtalhos();
        AtalhoDlgAtualiza(d);
        return 0;
    }
    case WM_CLOSE:   DestroyWindow(h); return 0;
    case WM_DESTROY: if (d) { d->capturando = -1; RetomarAtalhos(); } PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void ShowAtalhosDialog(HWND pai, HINSTANCE hi) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = AtalhoDlgProc; wc.hInstance = hi;
    wc.lpszClassName = L"DLSS5ForAllAtalhos";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);

    // LINHAS, e nao AT_COUNT: a tecla do ReShade ocupa uma linha a mais, com um respiro
    // antes dela (kGapRs) porque ela e de outra natureza - vai para o ReShade.ini.
    const int LINHAS = AT_COUNT + 1, kGapRs = 34;
    const int W = 560, H = 96 + LINHAS * 34 + kGapRs + 64;
    RECT r{ 0, 0, W, H };
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    RECT pr{}; GetWindowRect(pai, &pr);
    HWND h = CreateWindowExW(0, wc.lpszClassName, T(L"Atalhos do teclado", L"Keyboard shortcuts"),
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                             pr.left + 40, pr.top + 60, r.right - r.left, r.bottom - r.top,
                             pai, nullptr, hi, nullptr);
    if (!h) return;
    AtalhoDlg d;
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)&d);
    HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Segoe UI");
    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int w, int hh, int id) {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, hh, h,
                                 (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        return c;
    };
    mk(L"STATIC", T(L"Estas teclas funcionam com o jogo em primeiro plano. O padrão são três;\n"
                    L"as outras ações ficam sem tecla e são feitas pelo painel.",
                    // Duas linhas, e a primeira TEM de caber em 520 px: o STATIC quebra
                    // sozinho o que nao cabe e a terceira linha sai fora da caixa.
                    L"These keys work with the game in front. Three come set by default;\n"
                    L"the other actions start with no key and are done from the panel."), 0, 16, 12, 520, 40, 0);
    for (int i = 0; i < AT_COUNT; i++) {
        d.lbl[i] = mk(L"STATIC", L"", 0, 16, 62 + i * 34 + 4, 420, 22, 0);
        d.bt[i]  = mk(L"BUTTON", T(L"Mudar", L"Change"), WS_TABSTOP, 446, 62 + i * 34, 90, 26, IDD_AT_MUDAR + i);
    }
    {   // a tecla do ReShade, separada por um respiro e com a origem dita na tela
        mk(L"STATIC", T(L"Esta última é do ReShade, não do app — ela vai para o ReShade.ini:",
                        L"This last one belongs to ReShade, not to the app — it goes into ReShade.ini:"),
           0, 16, 62 + AT_COUNT * 34 + 6, 520, 22, 0);
        const int y = 62 + AT_COUNT * 34 + kGapRs;
        d.lblRs = mk(L"STATIC", L"", 0, 16, y + 4, 420, 22, 0);
        d.btRs  = mk(L"BUTTON", T(L"Mudar", L"Change"), WS_TABSTOP, 446, y, 90, 26, IDD_AT_RESHADE);
    }
    d.aviso = mk(L"STATIC", L"", 0, 16, 68 + LINHAS * 34 + kGapRs, 520, 22, 0);
    mk(L"BUTTON", T(L"Restaurar padrões", L"Restore defaults"), WS_TABSTOP, 16, 96 + LINHAS * 34 + kGapRs, 170, 30, IDD_AT_PADRAO);
    mk(L"BUTTON", T(L"Fechar", L"Close"), BS_DEFPUSHBUTTON | WS_TABSTOP, 436, 96 + LINHAS * 34 + kGapRs, 100, 30, IDD_AT_FECHAR);
    AtalhoDlgAtualiza(&d);

    EnableWindow(pai, FALSE);
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // IsDialogMessage engoliria o WM_KEYDOWN que a captura precisa ver
        if (d.capturando >= 0 || !IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(pai, TRUE);
    SetForegroundWindow(pai);
    DeleteObject(font);
    UnregisterClassW(wc.lpszClassName, hi);
}

static LRESULT CALLBACK SelWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    SelState* s = (SelState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if ((id == ID_LANG_PT || id == ID_LANG_EN) && code == BN_CLICKED) {
            const Lingua nova = (id == ID_LANG_EN) ? LINGUA_EN : LINGUA_PT;
            if (nova == g_lingua) {
                // Clicar no que ja esta valendo nao pode "desmarcar" o par: sao dois
                // botoes de estado, e um par sem nenhum marcado nao existe.
                SendMessageW(s->hLangPt, BM_SETCHECK, EmIngles() ? BST_UNCHECKED : BST_CHECKED, 0);
                SendMessageW(s->hLangEn, BM_SETCHECK, EmIngles() ? BST_CHECKED : BST_UNCHECKED, 0);
                return 0;
            }
            GravarIdioma(IniPath(), nova);
            Log("idioma: trocado para %s no seletor", U8(CodigoLingua(nova)).c_str());
            g_selTrocouIdioma = true;
            DestroyWindow(h);   // o wWinMain reabre o seletor ja no idioma novo
            return 0;
        }
        if (id == ID_KIND && code == CBN_SELCHANGE) { FillTargets(s, h); return 0; }
        if (id == ID_DEV && code == CBN_SELCHANGE) { FillModes(s); return 0; }
        if (id == ID_VSYNC && code == BN_CLICKED) {
            // vem desmarcado; ao marcar, explica o que melhora e o que piora, com OK
            if (SendMessageW((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED)
                MessageBoxW(h, T(
                    L"V-SYNC LIGADO\n\n"
                    L"O que melhora:\n"
                    L"  - cadencia estavel, cada frame apresentado exatamente num vblank do monitor;\n"
                    L"  - o Lossless Scaling (frame generation) gosta dessa regularidade.\n\n"
                    L"O que piora:\n"
                    L"  - mais latencia: o app espera o monitor antes de mostrar cada frame;\n"
                    L"  - a taxa fica presa ao refresh do monitor onde a janela esta (60 Hz = 60 fps, mesmo capturando 100).\n\n"
                    L"Desligado (padrão) NÃO rasga a imagem: em flip model ele só não espera o vblank.\n"
                    // Nao citar tecla aqui: esta caixa aparece ANTES de o app registrar as
                    // hotkeys, entao ainda nao se sabe qual vai valer nesta maquina.
                    L"Dá pra mudar depois no painel, sem fechar o app.",

                    L"V-SYNC ON\n\n"
                    L"What gets better:\n"
                    L"  - steady pacing, every frame presented exactly on a monitor vblank;\n"
                    L"  - Lossless Scaling (frame generation) likes that regularity.\n\n"
                    L"What gets worse:\n"
                    L"  - more latency: the app waits for the monitor before showing each frame;\n"
                    L"  - the rate is capped by the refresh of the monitor the window is on (60 Hz = 60 fps, even capturing 100).\n\n"
                    L"Off (the default) does NOT tear: on the flip model it simply does not wait for the vblank.\n"
                    L"You can change this later from the panel, without closing the app."),
                    L"DLSS5 For All - V-Sync", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (id == ID_OVERLAY && code == BN_CLICKED) {
            // a pessoa acabou de mudar o modo: explica na hora o que vai acontecer
            bool on = SendMessageW(s->hOverlay, BM_GETCHECK, 0, 0) == BST_CHECKED;
            SetWindowTextW(s->hHint, on
                ? T(L"SOBREPOR LIGADO: o filtro fica COLADO POR CIMA do alvo, sem borda, como se fosse parte do jogo.\n"
                    L"Mouse e teclado continuam indo pro jogo; some da barra de tarefas e do Alt+Tab. Controle so por\n"
                    L"atalho (o app mostra quais ao abrir). OBS: Captura de Janela apontando pro DLSS5 For All.",
                    L"OVERLAY ON: the filter sits GLUED ON TOP of the target, borderless, as if it were part of the game.\n"
                    L"Mouse and keyboard still go to the game; it leaves the taskbar and Alt+Tab. Control is by shortcut\n"
                    L"only (the app shows which ones when it opens). In OBS: Window Capture pointed at DLSS5 For All.")
                : T(L"SOBREPOR DESLIGADO: o alvo aparece numa JANELA SEPARADA, igual a imagem de uma placa de captura.\n"
                    L"Move, redimensiona, F11 tela cheia. End abre o painel. O jogo fica visivel do lado.\n"
                    L"E o modo certo pro Lossless Scaling por cima e pra gravar no OBS. Marque de novo pra colar.",
                    L"OVERLAY OFF: the target shows up in a SEPARATE WINDOW, just like a capture card feed.\n"
                    L"Move it, resize it, F11 for fullscreen. End opens the panel. The game stays visible beside it.\n"
                    L"This is the right mode for Lossless Scaling on top and for recording in OBS. Check it again to glue."));
            return 0;
        }
        if (id == ID_DLSS_BAIXAR && code == BN_CLICKED) {
            Log("seletor: aviso do DLSS 5 - abrindo as releases do RHI");
            ShellExecuteW(nullptr, L"open", kUrlRhi, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == ID_DLSS_PASTA && code == BN_CLICKED) {
            // A pasta do EXE, e nao a de dados: e ali que o ReShade procura os arquivos.
            Log("seletor: aviso do DLSS 5 - abrindo a pasta do app");
            ShellExecuteW(nullptr, L"open", g_exeDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == ID_ATALHOS && code == BN_CLICKED) {
            ShowAtalhosDialog(h, (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE));
            if (s->hAtalhos)
                SetWindowTextW(s->hAtalhos, RodapeAtalhos().c_str());
            return 0;
        }
        if (id == ID_REFRESH && code == BN_CLICKED) {
            if (SelKind(s) == SRC_CAMERA) {
                int di = (int)SendMessageW(s->hDev, CB_GETCURSEL, 0, 0);
                if (di >= 0 && di < (int)s->devs->size()) { (*s->devs)[di].modesLoaded = false; (*s->devs)[di].modes.clear(); }
                FillModes(s);
            } else {
                FillTargets(s, h);
            }
            return 0;
        }
        if (id == ID_START && code == BN_CLICKED) {
            SourceKind k = SelKind(s);
            int di = (int)SendMessageW(s->hDev, CB_GETCURSEL, 0, 0);
            int mi = (int)SendMessageW(s->hMode, CB_GETCURSEL, 0, 0);
            s->ch.kind = k;
            s->ch.fs = SendMessageW(s->hFs, BM_GETCHECK, 0, 0) == BST_CHECKED;
            s->ch.cursor = SendMessageW(s->hCursor, BM_GETCHECK, 0, 0) == BST_CHECKED;
            s->ch.hideSelf = SendMessageW(s->hHide, BM_GETCHECK, 0, 0) == BST_CHECKED;
            s->ch.vsync = SendMessageW(s->hVsync, BM_GETCHECK, 0, 0) == BST_CHECKED;
            s->ch.hdrTreat = SendMessageW(s->hHdr, BM_GETCHECK, 0, 0) == BST_CHECKED;
            s->ch.fpsTela = SendMessageW(s->hFps, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (k == SRC_CAMERA) {
                if (di < 0 || mi < 0) return 0;
                s->ch.dev = di; s->ch.mode = mi;
            } else if (k == SRC_WINDOW) {
                if (di < 0 || di >= (int)s->wins.size()) return 0;
                s->ch.win = s->wins[di].hwnd; s->ch.winTitle = s->wins[di].title;
                s->ch.clientOnly = (mi != 1);
                s->ch.overlay = SendMessageW(s->hOverlay, BM_GETCHECK, 0, 0) == BST_CHECKED;
            } else {
                if (di < 0 || di >= (int)s->mons.size()) return 0;
                s->ch.mon = s->mons[di].mon; s->ch.monLabel = s->mons[di].label;
                s->ch.overlay = SendMessageW(s->hOverlay, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (s->ch.overlay) s->ch.hideSelf = true;   // sem isso e espelho infinito
            }
            s->ok = true; DestroyWindow(h);
            return 0;
        }
        break;
    }
    // O AMBAR DA FAIXA, pintado pelo pai: um retangulo so, atras de tudo, que nenhum
    // controle irmao pode apagar.
    case WM_ERASEBKGND: {
        const HDC dc = (HDC)wp;
        RECT rc{}; GetClientRect(h, &rc);
        FillRect(dc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        if (s && s->avisoVisivel && s->hbrAviso) {
            auto E = [s](int v) { return MulDiv(v, (int)s->dpi, 96); };
            RECT f{ E(16), E(kFaixaY), E(16 + 628), E(kFaixaY + kFaixaH) };
            FillRect(dc, &f, s->hbrAviso);
        }
        return 1;
    }
    // ...e o texto por cima dele, com o mesmo fundo: sem isto o STATIC sairia numa ilha
    // cinza dentro da faixa.
    case WM_CTLCOLORSTATIC: {
        if (!s || !s->hbrAviso || (HWND)lp != s->hAvisoTxt) break;
        SetBkColor((HDC)wp, kCorFaixaFundo);
        SetTextColor((HDC)wp, kCorFaixaTexto);
        return (LRESULT)s->hbrAviso;
    }
    // O SELETOR VOLTOU PARA A FRENTE: a pessoa pode ter acabado de instalar o RHI numa
    // outra janela. Reavaliar aqui e o que faz o aviso sumir sozinho - sem isto ela
    // instalaria, voltaria e continuaria vendo a acusacao, sem saber se deu certo.
    case WM_ACTIVATE:
        if (s && LOWORD(wp) != WA_INACTIVE) AplicarAvisoDlss(s, h);
        break;
    case DM_GETDEFID: return MAKELONG(ID_START, DC_HASDEFID);
    case DM_SETDEFID: return TRUE;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static bool ShowSelector(HINSTANCE hi, std::vector<Device>& devs, Choice& out,
                         const std::wstring& wantKind, const std::wstring& wantDev, const std::wstring& wantMode,
                         const std::wstring& wantWin, const std::wstring& wantMon, bool wantFs,
                         bool wantVsync, bool wantHdr, bool primeiraVez) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = SelWndProc; wc.hInstance = hi; wc.lpszClassName = L"FafaDLSS5CamSel";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    // hIconSm separado: e ele que aparece na barra de titulo e no Alt+Tab pequeno.
    wc.hIcon   = IconeApp(GetSystemMetrics(SM_CXICON));
    wc.hIconSm = IconeApp(GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&wc);

    SelState st; st.devs = &devs;
    st.wantDev = wantDev; st.wantMode = wantMode; st.wantWin = wantWin; st.wantMon = wantMon;
    st.primeiraVez = primeiraVez;
    // O processo e PER_MONITOR_AWARE_V2: o Windows NAO escala nada por nos. O painel ja
    // tratava isso (Hud::g_scale); o seletor nao, e por isso ele saia 20% menor que uma
    // janela normal a 125% e pela metade num 4K a 200% - "de outra epoca" ao lado do
    // painel. Todo o layout continua escrito em unidades de 96 dpi; a escala entra aqui,
    // num lugar so, na criacao de cada controle.
    UINT dpi = 96;
    {
        typedef UINT(WINAPI* PFNDPI)(HMONITOR, int, UINT*, UINT*);
        HMODULE sh = LoadLibraryW(L"shcore.dll");
        PFNDPI f = sh ? (PFNDPI)GetProcAddress(sh, "GetDpiForMonitor") : nullptr;
        UINT dx = 96, dy = 96;
        if (f && SUCCEEDED(f(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), 0, &dx, &dy))) dpi = dx;
        if (sh) FreeLibrary(sh);
        if (dpi < 96) dpi = 96;
    }
    auto E = [dpi](int v) { return MulDiv(v, (int)dpi, 96); };   // 96 dpi -> pixels desta tela
    st.dpi = dpi;   // a faixa do DLSS 5 reposiciona controles depois, e precisa da mesma escala

    // A janela ja NASCE alta quando falta arquivo: reposicionar tudo logo depois de
    // aparecer daria um pulo na tela. AplicarAvisoDlss, mais abaixo, so confirma o estado.
    const bool avisoNaAbertura = VerFaltaDlss().AlgumaCoisa();
    const int W = E(kSelW), H = E(kSelH + (avisoNaAbertura ? kAvisoDy : 0));
    RECT r{ 0, 0, W, H }; AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    int sx = (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2, sy = (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2;
    const std::wstring tituloSel = std::wstring(APP_TITLE L" " APP_VERSION) +
                                   T(L" — escolha a fonte de vídeo", L" — pick the video source");
    HWND h = CreateWindowExW(0, wc.lpszClassName, tituloSel.c_str(),
                             // WS_CLIPCHILDREN: o apagamento de fundo do PAI nao pode
                             // passar por cima dos controles. Ate aqui nao fazia falta
                             // porque nada invalidava esta janela depois de criada; a
                             // faixa do DLSS 5, que aparece e some com a janela aberta,
                             // passou a invalidar - e a repintura do fundo alcanca a area
                             // dos filhos, que so voltam quando algo os invalida um a um.
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                             sx, sy, r.right - r.left, r.bottom - r.top, nullptr, nullptr, hi, nullptr);
    if (!h) { Log("seletor: CreateWindowExW falhou (erro %lu)", (unsigned long)GetLastError()); return false; }
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)&st);
    HFONT font = CreateFontW(-E(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int hh, int id) {
        HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                 E(x), E(y), E(w), E(hh), h, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE); return c;
    };
    // O IDIOMA FICA NA PRIMEIRA LINHA DESTA TELA, e essa foi uma decisao do usuario:
    // esta e a primeira janela do app, e quem nao le portugues precisa achar o idioma sem
    // entender mais nada do resto - sem clicar em "Atalhos...", sem esperar o painel.
    //
    // DOIS BOTOES, e nao uma lista: uma lista fechada mostra so o idioma em uso e esconde
    // que existe outro atras de um clique. Com os dois na tela a opcao se ve na primeira
    // olhada, e trocar custa UM clique. Cada nome vem na propria lingua ("Português",
    // "English") porque e assim que se reconhece o proprio idioma numa tela estrangeira; o
    // rotulo traz as duas palavras pelo mesmo motivo.
    //
    // BOTAO DE OPCAO, e nao caixa BS_PUSHLIKE: medido na tela desta maquina, o "pushlike"
    // marcado no tema do Windows 11 fica so com a borda um pouco mais escura que o outro -
    // quem olha rapido nao sabe qual esta valendo. A bolinha cheia contra a vazia nao tem
    // essa duvida, e e o controle que o Windows inteiro usa para "escolha uma".
    mk(L"STATIC", L"Idioma / Language:", SS_RIGHT, 330, 14, 132, 20, 0);
    st.hLangPt = mk(L"BUTTON", NomeLingua(LINGUA_PT),
                    BS_AUTORADIOBUTTON | WS_TABSTOP | WS_GROUP, 468, 13, 94, 22, ID_LANG_PT);
    st.hLangEn = mk(L"BUTTON", NomeLingua(LINGUA_EN),
                    BS_AUTORADIOBUTTON | WS_TABSTOP, 566, 13, 78, 22, ID_LANG_EN);
    SendMessageW(st.hLangPt, BM_SETCHECK, EmIngles() ? BST_UNCHECKED : BST_CHECKED, 0);
    SendMessageW(st.hLangEn, BM_SETCHECK, EmIngles() ? BST_CHECKED : BST_UNCHECKED, 0);

    // WS_GROUP aqui fecha o grupo dos dois botoes de idioma: sem isto as setas do teclado
    // sairiam deles direto para os controles de baixo.
    mk(L"STATIC", T(L"Fonte:", L"Source:"), WS_GROUP, 16, 14, 300, 20, 0);
    st.hKind    = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 16, 34, 628, 200, ID_KIND);
    st.hLblDev  = mk(L"STATIC", T(L"Placa de captura / câmera / console:", L"Capture card / camera / console:"), 0, 16, 70, 600, 20, 0);
    st.hDev     = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 16, 90, 628, 320, ID_DEV);
    st.hLblMode = mk(L"STATIC", T(L"Modo (resolução / fps / formato):", L"Mode (resolution / fps / format):"), 0, 16, 126, 600, 20, 0);
    st.hMode    = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 16, 146, 628, 400, ID_MODE);
    st.hFs      = mk(L"BUTTON", T(L"Abrir em tela cheia (F11 alterna)", L"Open in fullscreen (F11 toggles)"),
                     BS_AUTOCHECKBOX | WS_TABSTOP, 16, 186, 340, 22, ID_FS);
    st.hCursor  = mk(L"BUTTON", T(L"Mostrar o cursor do mouse", L"Show the mouse cursor"),
                     BS_AUTOCHECKBOX | WS_TABSTOP, 366, 186, 258, 22, ID_CURSOR);
    st.hHide    = mk(L"BUTTON", T(L"Esconder esta janela de gravadores/capturas (obrigatório no monitor sobreposto)",
                                  L"Hide this window from recorders/capture (required when overlaying a monitor)"),
                     BS_AUTOCHECKBOX | WS_TABSTOP, 16, 212, 628, 22, ID_HIDE);
    st.hOverlay = mk(L"BUTTON", T(L"SOBREPOR  —  marcado: filtro colado em cima do alvo  |  desmarcado: janela separada",
                                  L"OVERLAY  —  checked: filter glued on top of the target  |  unchecked: separate window"),
                     BS_AUTOCHECKBOX | WS_TABSTOP, 16, 238, 628, 22, ID_OVERLAY);
    st.hVsync   = mk(L"BUTTON", T(L"V-Sync — sincroniza com o monitor (desligado = menos espera, sem rasgar)",
                                  L"V-Sync — sync with the monitor (off = less waiting, still no tearing)"),
                     BS_AUTOCHECKBOX | WS_TABSTOP, 16, 264, 628, 22, ID_VSYNC);
    // o rotulo vem do proprio modulo de HDR, para os textos nao divergirem
#ifdef TEM_HDR_CONTROL
    st.hHdr     = mk(L"BUTTON", Hdr::Txt::Caixa(), BS_AUTOCHECKBOX | WS_TABSTOP, 16, 290, 628, 22, ID_HDR);
#else
    st.hHdr     = mk(L"BUTTON", T(L"Tratar HDR automaticamente", L"Handle HDR automatically"),
                     BS_AUTOCHECKBOX | WS_TABSTOP, 16, 290, 628, 22, ID_HDR);
#endif
    // O FPS do canto e do overlay do ReShade, nao nosso - por isso o rotulo diz de onde ele
    // vem: quem quer tirar o numero da tela procura por "FPS", nao por "ReShade".
    st.hFps     = mk(L"BUTTON", T(L"Mostrar FPS na tela — o contador do ReShade, no canto da imagem",
                                  L"Show FPS on screen — the ReShade counter, in the corner of the image"),
                     BS_AUTOCHECKBOX | WS_TABSTOP, 16, 316, 628, 22, ID_FPSTELA);
    // 344, e nao 318: a caixa acima empurrou tudo 26 px, e a janela cresceu os mesmos 26
    // (E(526)). A altura do hint continua 110 porque ele TEM cinco linhas - encolher aqui ja
    // cortou a quinta linha uma vez, justamente a que responde "posso gravar isso?".
    st.hHint    = mk(L"STATIC", L"", 0, 16, 344, 628, 110, 0);

    // A FAIXA DO DLSS 5, entre a dica e a linha de botoes. Criada sempre e escondida
    // quando nao ha o que dizer (ver AplicarAvisoDlss).
    //
    // O AMBAR E PINTADO PELO PAI, no WM_ERASEBKGND, e nao por um STATIC de fundo atras dos
    // outros controles. Com o STATIC funcionava na primeira pintura e ia se desfazendo: sem
    // WS_CLIPSIBLINGS um irmao pinta o retangulo dele inteiro, e o fundo apagava o texto e
    // os dois botoes toda vez que a janela era reativada. Sintoma bonito de depurar - a
    // faixa perdia uma linha por vez.
    // 78 de altura = as quatro linhas do pior caso (sem o ReShade E sem os dois arquivos),
    // com um respiro antes dos botoes - encostado, a ultima linha parecia cortada.
    st.hAvisoTxt = mk(L"STATIC", L"", 0, 28, kFaixaY + 6, 604, 78, 0);
    st.hAvisoBaixar = mk(L"BUTTON", T(L"Baixar os arquivos", L"Get the files"),
                         WS_TABSTOP, 28, kFaixaY + 90, 176, 28, ID_DLSS_BAIXAR);
    st.hAvisoPasta  = mk(L"BUTTON", T(L"Abrir a pasta", L"Open the folder"),
                         WS_TABSTOP, 212, kFaixaY + 90, 150, 28, ID_DLSS_PASTA);

    st.hRefresh    = mk(L"BUTTON", T(L"Atualizar lista", L"Refresh list"), WS_TABSTOP, 16, kBotoesY, 150, 34, ID_REFRESH);
    st.hBtAtalhos  = mk(L"BUTTON", T(L"Atalhos...", L"Shortcuts..."), WS_TABSTOP, 174, kBotoesY, 120, 34, ID_ATALHOS);
    // 196 px de largura e SS_CENTERIMAGE: com 200x34 em y=437 o texto quebrava em duas
    // linhas e a segunda era CORTADA pela borda de baixo da janela. Primeira janela do app.
    st.hAtalhos = mk(L"STATIC", L"", SS_CENTERIMAGE, 302, kBotoesY, 196, 34, 0);
    st.hStart = mk(L"BUTTON", T(L"INICIAR", L"START"), BS_DEFPUSHBUTTON | WS_TABSTOP, 504, kBotoesY, 140, 34, ID_START);

    // MONITOR PRIMEIRO (e o padrao de quem abre o app sem ler nada), MAS A JANELA FICA.
    //
    // A fonte Janela chegou a sair daqui por ser mais trabalhosa de usar - escolher o jogo
    // numa lista de titulos, e a camada some quando o alvo perde o foco. Voltou por um
    // motivo que nao e de gosto, e que so aparece quando se mede: A REDE PROCESSA AREA.
    //
    // Um jogo em 2560x1080 dentro de uma tela de 3440x1440 sao 2,8 megapixels por quadro
    // contra 5,0 do monitor inteiro - quase metade da conta. No modo monitor nao ha como
    // fugir disso: a captura e a tela toda, mesmo que o jogo ocupe um pedaco dela. Tirar a
    // opcao tirava, junto, a unica forma de baixar o custo do filtro sem baixar a
    // qualidade da rede. O texto de ajuda do seletor diz isso, que era o que faltava.
    SendMessageW(st.hKind, CB_ADDSTRING, 0, (LPARAM)T(L"Monitor inteiro", L"Whole monitor"));
    SendMessageW(st.hKind, CB_ADDSTRING, 0, (LPARAM)T(L"Janela (jogo / programa) — mais leve",
                                                      L"Window (game / program) — lighter"));
    SendMessageW(st.hKind, CB_ADDSTRING, 0, (LPARAM)T(L"Placa de captura / câmera / console",
                                                      L"Capture card / camera / console"));
    int k0 = (wantKind == L"janela") ? 1 : (wantKind == L"camera") ? 2 : 0;
    if (primeiraVez || devs.empty()) k0 = 0;
    SendMessageW(st.hKind, CB_SETCURSEL, k0, 0);
    if (wantFs)     SendMessageW(st.hFs, BM_SETCHECK, BST_CHECKED, 0);
    if (wantVsync)  SendMessageW(st.hVsync, BM_SETCHECK, BST_CHECKED, 0);
    if (wantHdr)    SendMessageW(st.hHdr, BM_SETCHECK, BST_CHECKED, 0);
    // A caixa do FPS nao precisa de parametro proprio: a escolha salva em [ultima] fps_tela
    // ja foi aplicada ao ReShade.ini na abertura (ver wWinMain), entao ler o ReShade.ini
    // aqui devolve a mesma coisa por um caminho so. Chave ausente (-1) = padrao do ReShade,
    // que e ligado.
    if (FpsNaTelaNoReShadeIni() != 0) SendMessageW(st.hFps, BM_SETCHECK, BST_CHECKED, 0);
    FillTargets(&st, h);
    // a pessoa ve como vai controlar o app antes mesmo de escolher a fonte
    SetWindowTextW(st.hAtalhos, RodapeAtalhos().c_str());
    // O estado inicial da faixa passa pela MESMA funcao que a atualiza depois: comeca no
    // valor oposto ao real para ela ver "mudou" e aplicar tudo de uma vez (texto, mostrar
    // ou esconder, mover a linha de botoes). Uma logica de layout so, e nao duas.
    st.hbrAviso = CreateSolidBrush(kCorFaixaFundo);
    st.avisoVisivel = !avisoNaAbertura;
    AplicarAvisoDlss(&st, h);
    ShowWindow(h, SW_SHOW); UpdateWindow(h); SetFocus(st.hStart);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    DeleteObject(font);
    if (st.hbrAviso) DeleteObject(st.hbrAviso);
    out = st.ch;
    return st.ok;
}

// ------------------------------------------------------------------ ini (ultima escolha)
static std::wstring IniPath() { return g_dadosDir + L"dlss5forall.ini"; }
static std::wstring IniGet(const wchar_t* key) {
    wchar_t b[512]; GetPrivateProfileStringW(L"ultima", key, L"", b, 512, IniPath().c_str()); return b;
}
static void IniSet(const wchar_t* key, const std::wstring& v) { WritePrivateProfileStringW(L"ultima", key, v.c_str(), IniPath().c_str()); }

// Escreve work_resolution no dlss5-feed.cfg. O arquivo e "chave=valor" por linha, sem
// secoes, e NAO e nosso: quem o le e o add-on do feed. Por isso a gravacao troca so a
// linha que interessa e preserva o resto byte a byte - mexer num arquivo de outro
// componente exige devolve-lo do jeito que veio, menos o que se veio mudar.
static void GravarWorkResolution(int valor) {
    const std::wstring caminho = g_exeDir + L"dlss5-feed.cfg";
    FILE* f = _wfopen(caminho.c_str(), L"rb");
    if (!f) { Log("cadeia: dlss5-feed.cfg nao encontrado - work_resolution nao aplicado"); return; }
    std::string txt; char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) txt.append(buf, n);
    fclose(f);

    char linhaNova[64];
    snprintf(linhaNova, sizeof(linhaNova), "work_resolution=%d", valor);
    const std::string chave = "work_resolution=";
    size_t p = txt.find(chave);
    // A chave pode nao existir (cfg de uma versao antiga do feed): ai ela e acrescentada.
    if (p == std::string::npos) {
        if (!txt.empty() && txt.back() != '\n') txt += "\n";
        txt += linhaNova; txt += "\n";
    } else {
        size_t fim = txt.find('\n', p);
        if (fim == std::string::npos) fim = txt.size();
        // \r\n: nao engolir o \r, senao o arquivo passa a misturar finais de linha
        size_t corte = fim;
        if (corte > p && txt[corte - 1] == '\r') corte--;
        if (txt.compare(p, corte - p, linhaNova) == 0) return;   // ja esta assim
        txt.replace(p, corte - p, linhaNova);
    }
    f = _wfopen(caminho.c_str(), L"wb");
    if (!f) { Log("cadeia: dlss5-feed.cfg sem permissao de escrita - work_resolution nao aplicado"); return; }
    fwrite(txt.data(), 1, txt.size(), f);
    fclose(f);
}

// O BOM que faz o app perder TODAS as escolhas em silencio.
//
// GetPrivateProfileString e a API de .ini do Windows, e ela nao conhece BOM UTF-8: com os
// tres bytes EF BB BF na frente, a primeira secao passa a se chamar "\uFEFF[ultima]" e
// NENHUMA chave e encontrada. O app nao da erro - ele simplesmente volta a todos os
// padroes, como se fosse a primeira execucao. Perdi uma rodada inteira de teste nisso
// antes de entender, e para o usuario seria pior: as escolhas dele evaporam sem aviso.
//
// Quem poe o BOM e qualquer editor comum (o Bloco de Notas ao "Salvar como UTF-8", o
// Set-Content -Encoding UTF8 do PowerShell 5.1). Nos nunca pomos: WritePrivateProfileString
// escreve sem BOM.
//
// ESCOPO, de proposito estreito: SO o dlss5forall.ini, que e nosso e so nos escrevemos.
//   - ofertas.ini NAO: e editado a mao, cheio de comentarios e blocos alternativos, e o
//     splash_ofertas.h ja le BOM UTF-8/UTF-16 sozinho;
//   - ReShade.ini NAO: e do ReShade;
//   - nada de varrer a pasta.
// So reescreve quando o BOM esta de fato la, guarda o original em .bak-bom e registra no
// log - reescrever arquivo alheio em silencio e pior do que o problema.
static void CorrigirBomDoIni() {
    const std::wstring p = IniPath();
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;              // nao existe ainda: nada a fazer
    LARGE_INTEGER tam{};
    if (!GetFileSizeEx(f, &tam) || tam.QuadPart < 3 || tam.QuadPart > 1024 * 1024) {
        // Vazio, ou grande demais para ser o nosso ini (que tem dezenas de bytes). Nos dois
        // casos a resposta certa e nao encostar no arquivo.
        CloseHandle(f);
        return;
    }
    std::string todo((size_t)tam.QuadPart, 0);
    DWORD lidos = 0;
    const bool ok = ReadFile(f, &todo[0], (DWORD)todo.size(), &lidos, nullptr) != 0;
    CloseHandle(f);
    if (!ok || lidos != todo.size()) return;
    if ((unsigned char)todo[0] != 0xEF || (unsigned char)todo[1] != 0xBB ||
        (unsigned char)todo[2] != 0xBF) return;         // sem BOM: o caso normal, sai daqui

    const std::wstring bak = p + L".bak-bom";
    const std::wstring tmp = p + L".tmp";
    CopyFileW(p.c_str(), bak.c_str(), FALSE);   // o original fica guardado, para suporte

    // ESCRITA ATOMICA: grava num .tmp e troca com MoveFileEx. Reescrever o ini por cima
    // deixaria um estado intermediario no disco - um CREATE_ALWAYS trunca antes de
    // escrever, e uma queda no meio (ou um WriteFile parcial por disco cheio) transformaria
    // "arquivo com BOM que o Windows nao le" em "arquivo truncado", que e estritamente pior.
    // Com o .tmp, ou o ini antigo continua inteiro, ou o novo aparece pronto.
    const DWORD n = (DWORD)(todo.size() - 3);
    HANDLE g = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g == INVALID_HANDLE_VALUE) {
        Log("ini com BOM: nao consegui criar o temporario (pasta sem permissao de escrita?) - "
            "o arquivo continua como estava e as escolhas nao vao ser lidas");
        return;
    }
    DWORD escritos = 0;
    const bool gravou = WriteFile(g, todo.data() + 3, n, &escritos, nullptr) != 0;
    FlushFileBuffers(g);
    CloseHandle(g);
    if (!gravou || escritos != n) {
        DeleteFileW(tmp.c_str());
        Log("ini com BOM: escrita incompleta (%lu de %lu bytes) - o arquivo NAO foi trocado "
            "e continua como estava; a copia em dlss5forall.ini.bak-bom tambem esta intacta",
            (unsigned long)escritos, (unsigned long)n);
        return;
    }
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        Log("ini com BOM: nao consegui trocar o arquivo (erro %lu) - o original ficou intacto",
            (unsigned long)GetLastError());
        return;
    }
    Log("dlss5forall.ini estava com BOM UTF-8 (o Windows nao acharia nenhuma chave) - "
        "reescrito sem BOM (%lu bytes); o original ficou em dlss5forall.ini.bak-bom", (unsigned long)n);
}

// ------------------------------------------------------------------ util de janela
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
static bool HideFromCapture(HWND h) {
    typedef BOOL(WINAPI* PFN)(HWND, DWORD);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    PFN f = u ? (PFN)GetProcAddress(u, "SetWindowDisplayAffinity") : nullptr;
    return f && f(h, WDA_EXCLUDEFROMCAPTURE);
}
// mantem a area util da janela igual a resolucao da fonte (quando cabe na tela)
static void ResizeClientTo(HWND h, UINT w, UINT hh) {
    DWORD st = (DWORD)GetWindowLongPtrW(h, GWL_STYLE);
    if (!(st & WS_OVERLAPPEDWINDOW)) return;      // em tela cheia nao mexe
    RECT wr{ 0, 0, (LONG)w, (LONG)hh }; AdjustWindowRect(&wr, st, FALSE);
    // A area util e a do monitor ONDE A JANELA ESTA, nao a do primario. SPI_GETWORKAREA
    // responde sempre pelo primario - e o mesmo erro que a criacao da janela ja tinha
    // corrigido: com o alvo num monitor maior que o primario, a janela era limitada pelo
    // tamanho errado toda vez que a fonte mudava.
    MONITORINFO mi{ sizeof(mi) };
    RECT wa{};
    if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) wa = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int ww = std::min<int>(wr.right - wr.left, wa.right - wa.left);
    int wh = std::min<int>(wr.bottom - wr.top, wa.bottom - wa.top);
    SetWindowPos(h, nullptr, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
// ------------------------------------------------------------------ modo sobreposto
// A janela vira um POPUP sem borda grudado no retangulo capturado do alvo, sempre
// por cima dele e (por padrao) transparente pro mouse: quem recebe clique e teclado
// continua sendo o jogo embaixo. O que voce ve e o nosso frame, ja passado pela cadeia.

static bool RegisterFirstFree(int id, const HkOpt* opts, int n, std::wstring& out) {
    for (int i = 0; i < n; i++) {
        if (RegisterHotKey(nullptr, id, opts[i].mods, opts[i].vk)) { out = opts[i].name; return true; }
    }
    out = L"(nenhum livre)";
    return false;
}

// ------------------------------------------------------------------ onde abrir as janelas
// "Se a janela que estamos capturando esta na tela 2, abre na tela 2. Se a janela for
// grande o suficiente, abre dentro da janela." Vale pro painel, pro guia de atalhos, pro
// cartaz e pra tela do grupo: a pessoa esta olhando pro jogo, e e la que a informacao
// tem de aparecer - nao no monitor primario, onde ela nem esta olhando.
static HWND     g_alvoWin = nullptr;    // janela capturada (nulo na fonte monitor/camera)
static HMONITOR g_alvoMon = nullptr;    // monitor capturado, quando a fonte e monitor

// Retangulo de referencia para centralizar algo de w x h: o proprio alvo quando ele
// comporta a janela com folga, senao a area util do monitor onde o alvo esta.
static RECT AreaParaJanela(int w, int h) {
    RECT alvo{};
    const bool temAlvo = g_alvoWin && IsWindow(g_alvoWin) && !IsIconic(g_alvoWin) &&
                         GetWindowRect(g_alvoWin, &alvo);
    if (temAlvo && (alvo.right - alvo.left) >= w + 48 && (alvo.bottom - alvo.top) >= h + 48)
        return alvo;

    HMONITOR mon = g_alvoMon;
    if (!mon && temAlvo) mon = MonitorFromWindow(g_alvoWin, MONITOR_DEFAULTTONEAREST);
    if (!mon) mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    return mi.rcWork;
}
static POINT CentroDe(const RECT& r, int w, int h) {
    return POINT{ r.left + ((r.right - r.left) - w) / 2, r.top + ((r.bottom - r.top) - h) / 2 };
}

// Centraliza W x H na area preferida E garante que a janela nao fique meio fora da tela.
// O clamp existe porque a area preferida pode ser um alvo maior que o monitor (janela de
// jogo com bordas fora da tela, por exemplo): centralizar nela jogaria metade do painel
// pra fora. Tudo aqui e em pixels fisicos.
static POINT PosicaoParaJanela(int W, int H) {
    const RECT ref = AreaParaJanela(W, H);
    POINT p = CentroDe(ref, W, H);
    const POINT centro{ ref.left + (ref.right - ref.left) / 2, ref.top + (ref.bottom - ref.top) / 2 };
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(centro, MONITOR_DEFAULTTONEAREST), &mi);
    if (p.x + W > mi.rcWork.right)  p.x = mi.rcWork.right - W;
    if (p.y + H > mi.rcWork.bottom) p.y = mi.rcWork.bottom - H;
    if (p.x < mi.rcWork.left) p.x = mi.rcWork.left;
    if (p.y < mi.rcWork.top)  p.y = mi.rcWork.top;
    return p;
}
// Altura disponivel, em pixels fisicos, onde uma janela de w x h vai aparecer. Serve pra
// decidir a escala: a comparacao tem de ser feita na MESMA unidade do tamanho final.
static int AlturaDisponivel(int w, int h) {
    const RECT ref = AreaParaJanela(w, h);
    const POINT centro{ ref.left + (ref.right - ref.left) / 2, ref.top + (ref.bottom - ref.top) / 2 };
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(centro, MONITOR_DEFAULTTONEAREST), &mi);
    const int hRef = ref.bottom - ref.top, hMon = mi.rcWork.bottom - mi.rcWork.top;
    return hRef < hMon ? hRef : hMon;   // nunca prometer mais espaco do que a tela tem
}

// ------------------------------------------------------------------ aviso na tela
// Feedback visual: a janela sobreposta nao tem barra nem texto, entao cada comando
// pisca um cartaz por ~2 s. Ele tambem sai da captura, senao entraria no proprio filtro.
static HWND  g_toast = nullptr;
static HFONT g_toastFont = nullptr;
static std::wstring g_toastText;
static ULONGLONG g_toastUntil = 0;
static bool HideFromCapture(HWND h);   // definido mais abaixo

static LRESULT CALLBACK ToastWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT r; GetClientRect(h, &r);
        // mesma paleta do painel (marca/PALETA.md): superficie #111827, barra ciano #06b6d4
        HBRUSH bg = CreateSolidBrush(RGB(17, 24, 39));
        FillRect(dc, &r, bg); DeleteObject(bg);
        HBRUSH edge = CreateSolidBrush(RGB(6, 182, 212));
        RECT bar{ 0, 0, 4, r.bottom };
        FillRect(dc, &bar, edge); DeleteObject(edge);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(233, 236, 242));
        HGDIOBJ old = SelectObject(dc, g_toastFont);
        RECT t{ 18, 10, r.right - 12, r.bottom - 8 };
        DrawTextW(dc, g_toastText.c_str(), -1, &t, DT_LEFT | DT_TOP | DT_NOPREFIX);
        SelectObject(dc, old);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void ShowToast(HINSTANCE hi, HMONITOR mon, const std::wstring& text, UINT ms) {
    if (!g_toastFont)
        g_toastFont = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    if (!g_toast) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = ToastWndProc; wc.hInstance = hi; wc.lpszClassName = L"DLSS5ForAllToast";
        RegisterClassExW(&wc);
        g_toast = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
                                  wc.lpszClassName, L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hi, nullptr);
        if (!g_toast) return;
        SetLayeredWindowAttributes(g_toast, 0, 235, LWA_ALPHA);
        HideFromCapture(g_toast);   // o cartaz nao entra no video capturado
    }
    g_toastText = text;

    // mede o texto pra caixa caber
    HDC dc = GetDC(g_toast); HGDIOBJ old = SelectObject(dc, g_toastFont);
    RECT calc{ 0, 0, 700, 0 };
    DrawTextW(dc, g_toastText.c_str(), -1, &calc, DT_CALCRECT | DT_LEFT | DT_TOP | DT_NOPREFIX);
    SelectObject(dc, old); ReleaseDC(g_toast, dc);
    int w = (calc.right - calc.left) + 34, h = (calc.bottom - calc.top) + 20;

    // canto inferior direito de onde a pessoa esta olhando: dentro do alvo se couber,
    // senao no monitor do alvo (o parametro mon, quando vem, ainda manda)
    RECT ref;
    if (mon) { MONITORINFO mi{ sizeof(mi) }; GetMonitorInfoW(mon, &mi); ref = mi.rcWork; }
    else     { ref = AreaParaJanela(w, h); }
    int x = ref.right - w - 24, y = ref.bottom - h - 24;

    SetWindowPos(g_toast, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_toast, nullptr, TRUE);
    UpdateWindow(g_toast);
    g_toastUntil = GetTickCount64() + ms;
}

// ainda ha cartaz na tela? (o encerramento por device morto espera a pessoa ler)
static bool ToastVisivel() { return g_toast && g_toastUntil != 0; }

// As chaves que MUDAM o comportamento do app (derrubar a captura de proposito, disparar o
// cartaz de "pesado") so valem com DLSS5_DIAG=1 junto. Sao inofensivas por serem obscuras,
// mas apareciam no log do usuario e confundiriam qualquer suporte que o lesse. As chaves
// que so LEEM ou LOGAM (DLSS5_LOG_ITENS, DLSS5_DRAIN, DLSS5_MINUPD...) continuam soltas.
static bool DiagLigado() {
    static const bool v = []{
        wchar_t e[8]{}; return GetEnvironmentVariableW(L"DLSS5_DIAG", e, 8) && e[0] == L'1';
    }();
    return v;
}

static void ToastTick() {
    if (g_toast && g_toastUntil && GetTickCount64() > g_toastUntil) {
        ShowWindow(g_toast, SW_HIDE);
        g_toastUntil = 0;
    }
}

// ------------------------------------------------------------------ icone na bandeja
// No modo sobreposto a janela nao tem barra nem entra no Alt+Tab, entao o ponto de
// controle e a bandeja: menu com os atalhos e os comandos.
#define WM_TRAY (WM_APP + 1)
enum { TRAY_BYPASS = 200, TRAY_INTERACT = 201, TRAY_QUIT = 202, TRAY_PANEL = 203,
       TRAY_OFERTAS = 204, TRAY_GUIA = 205 };
// FILA de comandos pendentes, nao um inteiro.
//
// Era um `std::atomic<int>`: o segundo clique antes de o laco consumir o primeiro
// SUBSTITUIA o primeiro, em silencio. E o laco fica fora de circulacao por bastante tempo
// - o PokeReShade sozinho apresenta seis quadros com a tecla segurada, o que passa dos
// 300 ms. Dois cliques no painel dentro desse intervalo perdiam o primeiro.
//
// A interface mantida de proposito (`= cmd` e `.exchange(0)`) para nao espalhar a mudanca
// pelos ~30 pontos que enfileiram comandos.
struct FilaComandos {
    void operator=(int c) {
        if (!c) return;
        std::lock_guard<std::mutex> lk(mtx);
        if (fila.size() < 32) fila.push_back(c);   // teto: clique repetido nao vira memoria
    }
    int exchange(int) {
        std::lock_guard<std::mutex> lk(mtx);
        if (fila.empty()) return 0;
        const int c = fila.front(); fila.pop_front();
        return c;
    }
    std::mutex mtx;
    std::deque<int> fila;
};
static FilaComandos g_trayCmd;    // consumida pelo loop principal, um comando por volta
static bool g_trayBypass = false, g_trayInteract = false;

static LRESULT CALLBACK TrayWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_TRAY && (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP)) {
        POINT p; GetCursorPos(&p);
        HMENU mu = CreatePopupMenu();
        AppendMenuW(mu, MF_STRING | MF_GRAYED, 0, APP_TITLE L" " APP_VERSION);
        AppendMenuW(mu, MF_SEPARATOR, 0, nullptr);
        std::wstring mp = TS(L"Abrir o painel\t", L"Open the panel\t") + g_hkPanel;
        AppendMenuW(mu, MF_STRING, TRAY_PANEL, mp.c_str());
        AppendMenuW(mu, MF_STRING, TRAY_GUIA, T(L"Ver os atalhos (guia)", L"See the shortcuts (guide)"));
        AppendMenuW(mu, MF_SEPARATOR, 0, nullptr);
        std::wstring mb = TS(g_trayBypass ? L"Ligar o filtro" : L"Desligar o filtro (ver o original)",
                             g_trayBypass ? L"Turn the filter on" : L"Turn the filter off (see the original)")
                          + L"\t" + g_hkBypass;
        // O item faz o ciclo inteiro (abre o ReShade e entrega o mouse; de novo, fecha e
        // devolve), entao o texto diz isso - "Teclado pro ReShade" descrevia so a metade.
        std::wstring mi = TS(g_trayInteract ? L"Fechar o ReShade e devolver o mouse"
                                            : L"Abrir o ReShade (o mouse vai pra ele)",
                             g_trayInteract ? L"Close ReShade and give the mouse back"
                                            : L"Open ReShade (the mouse goes to it)")
                          + L"\t" + g_hkInteract;
        std::wstring mq = TS(L"Sair\t", L"Quit\t") + TextoAtalho(AT_SAIR);
        AppendMenuW(mu, MF_STRING, TRAY_BYPASS,   mb.c_str());
        AppendMenuW(mu, MF_STRING, TRAY_INTERACT, mi.c_str());
        AppendMenuW(mu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(mu, MF_STRING | MF_GRAYED, 0, T(L"tudo se faz pelo painel", L"everything else is in the panel"));
        AppendMenuW(mu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(mu, MF_STRING, TRAY_OFERTAS, T(L"Grupo de ofertas - PC do Fafa", L"Deals group - PC do Fafa"));
        AppendMenuW(mu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(mu, MF_STRING, TRAY_QUIT, mq.c_str());
        SetForegroundWindow(h);
        int cmd = (int)TrackPopupMenu(mu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, h, nullptr);
        DestroyMenu(mu);
        PostMessageW(h, WM_NULL, 0, 0);
        if (cmd) g_trayCmd = cmd;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static HWND MakeTrayWindow(HINSTANCE hi) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = TrayWndProc; wc.hInstance = hi; wc.lpszClassName = L"DLSS5ForAllTray";
    RegisterClassExW(&wc);
    return CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, hi, nullptr);
}

static void TrayAdd(HWND h, const std::wstring& tip) {
    NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid); nid.hWnd = h; nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    nid.uCallbackMessage = WM_TRAY;
    // SM_CXSMICON e o tamanho que a bandeja pede, e ele muda com o DPI da maquina:
    // 16 a 100%, 20 a 125%, 24 a 150%. Os tres estao no .ico, desenhados um a um.
    nid.hIcon = IconeApp(GetSystemMetrics(SM_CXSMICON));
    wcsncpy(nid.szTip, tip.c_str(), 127);           nid.szTip[127] = 0;
    {
        const std::wstring it = std::wstring(APP_TITLE) + T(L" ligado", L" is running");
        wcsncpy(nid.szInfoTitle, it.c_str(), 63); nid.szInfoTitle[63] = 0;
    }
    // As teclas sao configuraveis desde a versao dos atalhos: cravar "Alt+F9" aqui
    // mostrava uma tecla que pode nem estar registrada nesta maquina (a cascata troca
    // quando outro programa ja tomou a combinacao).
    {
        std::wstring b = g_hkBypass   + TS(L" liga/desliga o filtro\n", L" toggles the filter\n") +
                         g_hkInteract + TS(L" abre/fecha o ReShade\n",  L" opens/closes ReShade\n") +
                         TextoAtalho(AT_PAINEL) + TS(L" abre o painel\n", L" opens the panel\n") +
                         TextoAtalho(AT_SAIR)   + TS(L" sai", L" quits");
        wcsncpy(nid.szInfo, b.c_str(), 255);
        nid.szInfo[255] = 0;   // wcsncpy nao termina quando enche o buffer
    }
    Shell_NotifyIconW(NIM_ADD, &nid);
}
static void TrayRemove(HWND h) {
    NOTIFYICONDATAW nid{}; nid.cbSize = sizeof(nid); nid.hWnd = h; nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

static void SetClickThrough(HWND h, bool on) {
    g_clickThrough = on;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    // LAYERED entra e sai junto com TRANSPARENT: e a dupla que faz o clique atravessar
    // para outros processos (ver o bloco "O CLIQUE SO ATRAVESSA COM WS_EX_LAYERED").
    // Sozinho, o TRANSPARENT so muda ordem de pintura e o HTTRANSPARENT so vale entre
    // janelas da nossa propria thread.
    if (on) {
        ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        wchar_t env[8]{};
        if (!(GetEnvironmentVariableW(L"DLSS5_SEM_LAYERED", env, 8) && env[0] == L'1'))
            ex |= WS_EX_LAYERED;
    } else {
        ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
    }
    SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
    if (on && (ex & WS_EX_LAYERED)) SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);
    SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    if (!on) { SetForegroundWindow(h); SetFocus(h); }
}

// ------------------------------------------------- o modo interativo tem prazo de volta
// FORA DO CLICK-THROUGH A NOSSA CAMADA RECEBE TODO CLIQUE E TODA TECLA. No modo monitor
// ela cobre a TELA INTEIRA: deixar isso ligado e esquecer nao e um detalhe de interface,
// e "o Windows parou de responder ao mouse" - foi exatamente a queixa recebida. Antes
// nada devolvia sozinho: o botao "Abrir o ReShade" entrava aqui e ficava para sempre.
//
// Entao todo caminho que sai do click-through arma uma volta automatica:
//   - 20 s sem NENHUMA entrada dirigida a nossa janela  -> volta (abriu e foi embora);
//   - kInterTotalMs no total                            -> volta (a saida de escape).
// So a TECLA estica o prazo total, o mouse nao: quem esta de fato usando o overlay do
// ReShade digita nele; quem esta preso numa tela morta clica - e clicar renovando o prazo
// total seria justamente a armadilha se perpetuando. Quem foi devolvido no meio de um uso
// legitimo aperta a tecla de novo e ganha outro periodo inteiro.
//
// Sem NENHUMA tecla capaz de devolver o mouse (ver g_saidaInterativo) o prazo total
// encurta: ali a volta automatica e a unica saida que existe.
static const ULONGLONG kInterOcioMs        = 20000;
static const ULONGLONG kInterTotalMs       = 45000;
static const ULONGLONG kInterTotalSemTecla = 25000;
static std::atomic<ULONGLONG> g_interAte{ 0 };     // 0 = nao ha volta armada (nao estamos interativos)
                                       // atomico: o WndProc da camada roda em outra thread
static std::atomic<ULONGLONG> g_interEntrada{ 0 };  // ultima entrada que chegou na nossa janela
static ULONGLONG g_interCartaz = 0;    // proximo lembrete na tela (fonte monitor)
// A tecla que devolve o mouse, do jeito que a pessoa a ve. Vazia = nao existe nenhuma, e
// ai os cartazes nao podem prometer uma. Preenchida no registro das hotkeys.
static std::wstring g_saidaInterativo;

static void ArmarVoltaInterativo() {
    const ULONGLONG agora = GetTickCount64();
    g_interAte = agora + (g_saidaInterativo.empty() ? kInterTotalSemTecla : kInterTotalMs);
    g_interEntrada = agora;
    g_interCartaz = 0;   // o primeiro lembrete sai no proximo giro do laco
    Log("interativo: volta automatica armada (%llu s)",
        (unsigned long long)((g_interAte - agora) / 1000));
}
static void DesarmarVoltaInterativo() { g_interAte = 0; g_interCartaz = 0; }
// chamada de dentro do laco de mensagens, para cada mensagem que passa
static void MarcarUsoInterativo(bool nossaJanela, UINT msg) {
    if (!g_interAte || !nossaJanela) return;
    const bool mouse = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST);
    const bool tecla = (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);
    if (!mouse && !tecla) return;
    const ULONGLONG agora = GetTickCount64();
    g_interEntrada = agora;
    if (tecla && !g_saidaInterativo.empty()) g_interAte = agora + kInterTotalMs;
}

// =====================================================================================
//  PAINEL NA TELA (HUD)
//
//  Um atalho unico abre e fecha este painel. Ele NAO e desenhado dentro do nosso
//  pipeline D3D11 de proposito: se fosse, passaria pelo filtro DLSS e entraria na
//  captura (o OBS gravaria o painel junto com o jogo). Entao ele e uma janela propria -
//  popup layered, topmost, fora da captura - igual ao cartaz de aviso, com uma diferenca
//  importante: esta recebe mouse e teclado (nada de WS_EX_TRANSPARENT/NOACTIVATE aqui).
//
//  Desenho: UpdateLayeredWindow com um DIB de 32 bits premultiplicado, pintado pelo
//  GDI+. E o unico caminho que da alpha por pixel de verdade - cantos redondos com
//  antialias, sombra suave e fundo translucido sem franja preta.
// =====================================================================================

// O loop principal registra o renderer aqui. O painel precisa dele por um motivo so:
// o ReShade le o teclado uma vez por Present, entao mandar uma tecla pra ele exige
// apresentar um frame com a tecla ainda pressionada (veja PokeReShade).
static Renderer* g_renderer = nullptr;
static void PresentNow(int n) {
    for (int i = 0; i < n && g_renderer; i++) {
        g_renderer->WaitForPresentSlot();
        g_renderer->DrawAndPresent();
    }
}

// comandos que o painel enfileira pro loop principal (mesma fila do menu da bandeja)
//
// CADA FAIXA MORA NO SEU PROPRIO MILHAR, e isso nao e capricho: numa versao antiga um
// controle com faixa larga comecava em 350 e somava a porcentagem, entao o chip "50%"
// gerava 400 - que era exatamente CMD_ATALHO_BASE + AT_PAINEL. O bloco de atalhos e
// avaliado antes, entao aquele clique entrava em "aperte a tecla..." e suspendia as
// hotkeys ate o fim da sessao. Os static_assert abaixo travam a reincidencia na compilacao.
enum { CMD_PANEL_TOGGLE = 300, /* 301 foi CMD_RESHADE: o botao "Abrir o ReShade" passou a
                                  usar TRAY_INTERACT, que abre E fecha. Numero deixado vago
                                  de proposito - reaproveitar id de comando morto e receita
                                  de ini antigo virar clique errado. */ CMD_NEURAL = 302,
       CMD_VSYNC = 303, CMD_HDR = 304, CMD_OFERTAS = 305, CMD_GUIA = 306, CMD_CANAL = 311,
       CMD_TUDO = 307, CMD_SAIR = 308, CMD_SAIR_PAINEL = 309, CMD_FPSTELA = 310,

       CMD_FPS_BASE      = 400,  // + indice em kFpsCaps        -> 400..415
       CMD_WORKRES_BASE  = 500,  // + indice em kWorkRes        -> 500..515
       CMD_IDIOMA_BASE   = 600,  // + indice em kLinguas        -> 600..601
       // (1000, 1100 e 1300 eram a LUT, a intensidade da LUT e a nitidez, retiradas por
       //  decisao do usuario junto com os controles. CUIDADO com a versao antiga desta
       //  nota, que dizia "o preset nao carrega nenhum dos dois": vale para o
       //  DLSS5_Grade, mas o DLSS5_Sharpen ESTA no Techniques= do FafaDLSS5.ini, com
       //  Sharpness=0.35 - conferido no arquivo, nao na memoria.
       //  2000 e 2500 eram a resolucao interna, retirada antes disso. A
       //  faixa fica VAGA de proposito: reaproveitar o numero de um comando morto e a
       //  receita de um ini antigo virar clique errado.)
       CMD_ATALHO_BASE   = 3000, // + acao: entra em modo de captura
       CMD_ATALHO_PADRAO = 3100, // restaura os atalhos de fabrica
       CMD_REABRIR       = 4000, // relanca o app (alvo mudou de tamanho)
       CMD_ATALHO_SET_BASE = 100000 }; // + mods*256 + vk: a tecla capturada

// As faixas TEM de ser disjuntas. A largura usada em cada checagem e o maior valor que o
// codigo consegue somar na base - com folga, de proposito.
static_assert(CMD_FPSTELA       +   1 < CMD_FPS_BASE,        "avulsos invadem o teto de fps");
static_assert(CMD_FPS_BASE      +  16 < CMD_WORKRES_BASE,    "teto de fps invade a resolucao do DLSS");
static_assert(CMD_WORKRES_BASE  +  16 < CMD_IDIOMA_BASE,     "resolucao do DLSS invade o idioma");
static_assert(CMD_IDIOMA_BASE   +   8 < CMD_ATALHO_BASE,     "idioma invade os atalhos");
static_assert(CMD_ATALHO_BASE   +  64 < CMD_ATALHO_PADRAO,   "atalhos invadem o padrao de fabrica");
static_assert(CMD_ATALHO_PADRAO +   1 < CMD_REABRIR,         "padrao de fabrica invade o reabrir");
static_assert(CMD_REABRIR       +   1 < CMD_ATALHO_SET_BASE, "reabrir invade a tecla capturada");

// A "Resolucao interna" (backbuffer 75/100/150/200% da fonte) morava aqui. Foi medida
// (docs/supersampling.md) e RETIRADA por decisao do usuario: o neural nao melhora e o
// custo sai do fps do jogo. O app roda sempre no tamanho da captura.
static const int      kFpsCaps[]  = { 0, 60, 45, 30, 20 };
static const wchar_t* kFpsNames[] = { L"Máx", L"60", L"45", L"30", L"20" };
// So o primeiro chip e palavra; 60/45/30/20 sao numeros e nao mudam de idioma. Uma funcao
// so, para o chip do painel e o cartaz do clique nao divergirem.
static const wchar_t* NomeFpsCap(int i) { return (i == 0) ? T(L"Máx", L"Max") : kFpsNames[i]; }
static const int      kWorkRes[]   = { 0, 100, 77, 67, 58 };
static const wchar_t* kWorkResNames[] = { L"Auto", L"100%", L"77%", L"67%", L"58%" };
// os dois idiomas, na ordem em que aparecem no painel e no seletor
static const Lingua   kLinguas[] = { LINGUA_PT, LINGUA_EN };
// --------------------------------------------------------------- cor e nitidez: FORA
// O painel tinha chips de LUT (LUT_SELECT do DLSS5_Grade.fx), de intensidade da LUT e de
// nitidez (DLSS5_Sharpen.fx), todos escritos pela ponte do dlss5-bridge.addon64. Saíram
// por decisão do usuário: o preset em uso (FafaDLSS5.ini) carrega só Lumenite_Kernel e
// DLSS5_Feed, então nenhum dos dois efeitos estava no caminho e os chips só produziam
// "recusado" no log. Menos código por frame, menos controle morto na tela.
//
// A ponte NAO foi tocada: rsbridge::SetLut/SetLutStrength/SetSharpen* continuam no
// reshade_bridge.h, sem chamador, e o protocolo binario do add-on segue o mesmo - o
// dlss5-bridge.addon64 instalado continua compatível com este exe. Quem quiser o grading
// de volta reabilita pelo overlay do ReShade, com os shaders que continuam no disco.

// O estado real do neural, na fonte que o add-on escreve. Nao inventamos flag nossa:
// o usuario pode ter ligado o neural pelo overlay do ReShade, sem passar por nos.
static int NeuralNoIni() {
    wchar_t b[16]{};
    GetPrivateProfileStringW(L"RenoDX.DLSS5", L"NeuralUplift", L"", b, 16, (g_exeDir + L"ReShade.ini").c_str());
    if (!b[0]) return -1;                 // nao sei
    return (b[0] == L'1') ? 1 : 0;
}

// --------------------------------------------------------------- estado do neural
// O app inteiro existe por causa do Neural Rendering, entao o painel nao pode adivinhar
// se ele esta rodando: le da fonte que o add-on escreve. Sao duas, e nenhuma sozinha
// responde - o ini diz o que foi PEDIDO, o log diz o que esta ACONTECENDO.
// NR_SEM_RTX e o "nao da, e nao e culpa sua": a maquina nao tem placa capaz, ou o driver
// recusou o DLSS. Estado separado do NR_SEM_ADDON porque a resposta e outra - nenhum
// arquivo copiado resolve, e o painel nao pode oferecer um botao de ligar.
enum NeuralEstado { NR_DESLIGADO = 0, NR_INICIANDO, NR_ATIVO, NR_SEM_ADDON, NR_SEM_RTX };

// ------------------------------------------------------------------ que placa e esta?
// O DLSS roda em Tensor Core, e Tensor Core so existe em GeForce RTX (serie 20 pra cima).
// Sem isso o NGX_Init falha la no fundo do add-on e o painel dizia so "não subiu — confira
// o DLSS 5 Feed no ReShade": um conselho inutil para quem tem Radeon, Arc ou GTX, porque
// nao ha nada a conferir. A placa e a primeira coisa a saber e da para saber antes de tudo.
struct GpuInfo {
    std::wstring nome;       // a placa que o app usa
    std::wstring outraRtx;   // uma RTX que existe na maquina mas NAO e a que o app usa
    UINT vendor = 0;
    bool rtx  = false;   // NVIDIA E com Tensor Core (o nome traz "RTX")
    bool lido = false;   // o DXGI respondeu? sem resposta nao se acusa ninguem
};
static GpuInfo g_gpu;

// 0x10DE = NVIDIA, mas o fabricante sozinho NAO responde: a GTX 16xx e NVIDIA e nao tem
// Tensor Core nenhum. O criterio e o "RTX" no nome, que a propria NVIDIA so usa nas placas
// que trazem os nucleos de que o DLSS precisa.
static bool PlacaComTensorCore(UINT vendor, const std::wstring& nome) {
    if (vendor != 0x10DEu) return false;
    std::wstring up = nome;
    for (auto& c : up) c = (wchar_t)towupper(c);
    return up.find(L"RTX") != std::wstring::npos;
}

static void DetectarGpu() {
    // O ADAPTADOR 0 E O QUE O APP USA. O Renderer chama D3D11CreateDevice com
    // pAdapter=nullptr e D3D_DRIVER_TYPE_HARDWARE, e nao ha selecao de adaptador no app.
    // Medido nesta maquina (RTX 4080 + iGPU Radeon + Basic Render): o adaptador 0 e a
    // 4080 e o device do D3D11 sai nela mesma.
    //
    // Mas a lista inteira importa, e por um motivo pratico: em notebook hibrido o 0 pode
    // ser a iGPU e a RTX ficar no 1. Ai a resposta certa nao e "esta maquina nao tem RTX"
    // (mentira que faz a pessoa desistir do app) e sim "tem, mas o app nao esta usando" -
    // que tem conserto em Configuracoes > Tela > Gráficos.
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f)) || !f) {
        Log("gpu: CreateDXGIFactory1 falhou - seguindo sem saber que placa e");
        return;
    }
    for (UINT i = 0; ; i++) {
        IDXGIAdapter1* a = nullptr;
        if (f->EnumAdapters1(i, &a) != S_OK || !a) break;
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(a->GetDesc1(&d))) {
            const std::wstring nome = d.Description;
            const bool software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            const bool temTensor = !software && PlacaComTensorCore(d.VendorId, nome);
            if (i == 0) {
                g_gpu.nome = nome; g_gpu.vendor = d.VendorId;
                g_gpu.rtx = temTensor; g_gpu.lido = true;
            } else if (temTensor && g_gpu.outraRtx.empty()) {
                g_gpu.outraRtx = nome;
            }
            Log("gpu: adaptador %u: %s vendor=0x%04X %s%s", i, U8(nome).c_str(),
                (unsigned)d.VendorId, software ? "(software) " : "", temTensor ? "RTX" : "");
        }
        a->Release();
    }
    f->Release();
    Log("gpu: %s vendor=0x%04X rtx=%d%s",
        g_gpu.lido ? U8(g_gpu.nome).c_str() : "(desconhecida)",
        (unsigned)g_gpu.vendor, (int)g_gpu.rtx,
        g_gpu.outraRtx.empty() ? "" : (" (ha uma " + U8(g_gpu.outraRtx) + " na maquina, nao usada)").c_str());
}

// le o fim de um arquivo de texto (o ReShade.log cresce muito para ser lido inteiro)
//
// CACHE DE UMA ENTRADA, e o motivo e o lugar de onde isto e chamado: uma vez por segundo
// (as vezes duas na mesma volta - o LerEstadoNeural e o NeuralPeloLog leem o MESMO
// ReShade.log) de dentro do laco que apresenta os quadros. Sao 96 KB do disco para um
// arquivo que, na esmagadora maioria das voltas, nao mudou um byte. Tamanho e horario de
// escrita iguais = devolve o que ja esta na mao.
//
// TAMANHO MENOR TAMBEM INVALIDA. So perguntar "cresceu?" seria a armadilha: o ReShade
// rotaciona o log na abertura (ReShade.log vira .log1 e um novo comeca do zero), e o app
// passaria o resto da execucao respondendo com o conteudo da sessao passada.
//
// Uma entrada so, nao um mapa: os dois chamadores pedem o mesmo arquivo com o mesmo
// tamanho. Um caminho diferente simplesmente invalida - correto, so nao e otimo.
// Chamado sempre da thread da interface; sem trava, de proposito.
static std::string TailArquivo(const std::wstring& caminho, size_t bytes) {
    // DUAS ENTRADAS, e nao uma: numa mesma volta o app le o ReShade.log (duas vezes) e o
    // dlss5-feed.log. Com uma entrada so, os dois arquivos se expulsariam do cache a cada
    // chamada e a leitura de disco voltaria inteira - um cache que nunca acerta e so
    // trabalho a mais. Sao os dois unicos arquivos que passam por aqui.
    struct Entrada {
        std::wstring caminho;
        size_t       bytes = 0;
        LONGLONG     tam = -1;      // -1 = entrada vazia ou invalidada
        FILETIME     hora{};
        std::string  buf;
    };
    static Entrada cache[2];
    Entrada* e = nullptr;
    for (auto& c : cache) if (c.tam >= 0 && c.bytes == bytes && c.caminho == caminho) { e = &c; break; }
    if (!e) {   // sem entrada para este arquivo: usa uma vaga, ou atropela a primeira
        for (auto& c : cache) if (c.tam < 0) { e = &c; break; }
        if (!e) e = &cache[0];
        e->tam = -1;
    }

    WIN32_FILE_ATTRIBUTE_DATA fa{};
    bool anotar = false;
    if (GetFileAttributesExW(caminho.c_str(), GetFileExInfoStandard, &fa)) {
        const LONGLONG tamAgora = ((LONGLONG)fa.nFileSizeHigh << 32) | (LONGLONG)fa.nFileSizeLow;
        if (e->tam >= 0 && tamAgora == e->tam &&
            fa.ftLastWriteTime.dwLowDateTime  == e->hora.dwLowDateTime &&
            fa.ftLastWriteTime.dwHighDateTime == e->hora.dwHighDateTime)
            return e->buf;
        e->hora = fa.ftLastWriteTime;
        anotar = true;
    }

    HANDLE h = CreateFileW(caminho.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { e->tam = -1; e->buf.clear(); return {}; }
    LARGE_INTEGER tam{};
    GetFileSizeEx(h, &tam);
    LONGLONG ini = tam.QuadPart > (LONGLONG)bytes ? tam.QuadPart - (LONGLONG)bytes : 0;
    LARGE_INTEGER pos; pos.QuadPart = ini;
    SetFilePointerEx(h, pos, nullptr, FILE_BEGIN);
    std::string buf((size_t)(tam.QuadPart - ini), '\0');
    DWORD lidos = 0;
    if (!buf.empty()) ReadFile(h, &buf[0], (DWORD)buf.size(), &lidos, nullptr);
    CloseHandle(h);
    buf.resize(lidos);
    // So guarda o que a leitura confirmou. Se o GetFileAttributesExW nao respondeu (arquivo
    // trocado embaixo de nos, permissao negada) a entrada fica invalidada, nao mentindo.
    if (anotar) { e->caminho = caminho; e->bytes = bytes; e->tam = tam.QuadPart; e->buf = buf; }
    else        { e->tam = -1; e->buf.clear(); }
    return buf;
}

// definido junto do AlternarNeural, la embaixo; aqui ele e a FONTE PRINCIPAL do estado
static int NeuralPeloLog();

// A PROVA DEFINITIVA de que esta maquina roda DLSS nao e o nome da placa: e o NGX dizer
// que sim. Quem pergunta e o dlss5-feed, e a resposta esta no dlss5-feed.log:
//
//   [feed] NVSDK_NGX_D3D12_Init -> 0x00000001 (Success)
//   [feed] NGX capabilities: SuperSampling.Available=1 NeedsUpdatedDriver=0 MinDriver=470.0
//
// Devolve 1 (o NGX aceitou), 0 (recusou) ou -1 (ainda nao perguntou - o feed so abre a
// sessao D3D12 depois de o ReShade carregar e os shaders compilarem, entao -1 e o normal
// nos primeiros segundos e nao acusa nada). Quando recusa, 'porque' sai preenchido.
static int NgxPeloFeed(std::wstring* porque) {
    const std::string t = TailArquivo(g_exeDir + L"dlss5-feed.log", 32 * 1024);
    if (t.empty()) return -1;
    const size_t cap = t.rfind("NGX capabilities:");
    if (cap != std::string::npos) {
        // So a ULTIMA linha de capabilities vale; as anteriores sao de tentativas antigas.
        // E so ELA: cortar no fim de linha impede que um "NeedsUpdatedDriver=1" da linha
        // seguinte (de outra sessao, de outro contexto) responda no lugar desta.
        const size_t fim = t.find('\n', cap);
        const std::string linha = t.substr(cap, (fim == std::string::npos ? t.size() : fim) - cap);
        const bool disponivel = linha.find("SuperSampling.Available=1") != std::string::npos;
        const bool driverVelho = linha.find("NeedsUpdatedDriver=1") != std::string::npos;
        if (disponivel && !driverVelho) return 1;
        if (porque) *porque = driverVelho
            ? T(L"o driver da NVIDIA é antigo demais para o DLSS — atualize (mínimo 470)", L"the NVIDIA driver is too old for DLSS — update it (470 minimum)")
            : T(L"o driver recusou o DLSS nesta placa — atualize o driver NVIDIA (mínimo 470)", L"the driver refused DLSS on this GPU — update the NVIDIA driver (470 minimum)");
        return 0;
    }
    // Sem a linha de capabilities, um Init que terminou sem "(Success)" ja e resposta.
    const size_t ini = t.rfind("NVSDK_NGX_D3D12_Init ->");
    if (ini != std::string::npos && t.find("(Success)", ini) == std::string::npos) {
        if (porque) *porque = T(L"o NGX não inicializou nesta placa — atualize o driver NVIDIA (mínimo 470)", L"NGX did not initialise on this GPU — update the NVIDIA driver (470 minimum)");
        return 0;
    }
    return -1;
}

static NeuralEstado LerEstadoNeural(std::wstring* detalhe) {
    // 0. A PLACA AGUENTA? Antes de qualquer arquivo: nenhum deles resolve a falta de
    // Tensor Core, e mandar a pessoa conferir o add-on seria fazer perder tempo com o que
    // nao tem conserto. So acusa quando o DXGI de fato respondeu (g_gpu.lido).
    if (g_gpu.lido && !g_gpu.rtx) {
        if (detalhe) *detalhe = g_gpu.outraRtx.empty()
            ? (TS(L"o DLSS precisa de uma GeForce RTX (série 20 ou mais nova); esta máquina tem ",
                  L"DLSS needs a GeForce RTX (20 series or newer); this machine has ") + g_gpu.nome)
            : (TS(L"esta máquina tem uma ", L"this machine has a ") + g_gpu.outraRtx +
               TS(L", mas o app está rodando na ", L", but the app is running on the ") + g_gpu.nome +
               T(L" — em Configurações › Sistema › Tela › Gráficos, escolha "
                 L"\"Alto desempenho\" para o DLSS5ForAll.exe e reabra",
                 L" — under Settings › System › Display › Graphics, set DLSS5ForAll.exe to "
                 L"\"High performance\" and reopen it"));
        return NR_SEM_RTX;
    }
    // 1. os arquivos existem? sem eles nao ha o que ligar
    const std::wstring addon = g_exeDir + L"renodx-dlss5.addon64";
    const std::wstring dll   = g_exeDir + L"nvngx_dlssnr.dll";
    if (GetFileAttributesW(addon.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (detalhe) *detalhe = T(L"falta o renodx-dlss5.addon64 ao lado do app", L"renodx-dlss5.addon64 is missing next to the app");
        return NR_SEM_ADDON;
    }
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (detalhe) *detalhe = T(L"falta o nvngx_dlssnr.dll ao lado do app", L"nvngx_dlssnr.dll is missing next to the app");
        return NR_SEM_ADDON;
    }
    // 2. LIGADO OU DESLIGADO - E O LOG QUE MANDA, O INI E RESERVA.
    // O ini diz o que foi PEDIDO, e o RenoDX so o grava quando o AutoSave do ReShade
    // resolve, com segundos de atraso (as vezes nunca, se o app fecha antes). O
    // "NR toggled ON/OFF" sai no instante do toque. Enquanto os dois discordarem, quem
    // esta certo e o log - era daqui que vinha o card grudado no estado velho logo depois
    // de clicar, que fazia a pessoa clicar de novo e desfazer o proprio pedido.
    //
    // O relogio do INICIANDO (la embaixo) e rearmado aqui e no caminho de ATIVO: sem isso
    // ele contava desde a PRIMEIRA vez que o app passou por aqui, e quem ligasse o neural
    // depois de 25 s de app lia "não subiu" por um segundo antes de a cadeia responder.
    static ULONGLONG t0Iniciando = 0;
    const int pelaLinha = NeuralPeloLog();                        // -1 = o log nao fala disso
    const int pedido = (pelaLinha >= 0) ? pelaLinha : NeuralNoIni();
    if (pedido == 0) {
        t0Iniciando = 0;   // desligado: o proximo "ligado" comeca a contar do zero
        if (detalhe) *detalhe = T(L"o add-on está carregado, mas o neural está desligado", L"the add-on is loaded, but the neural is off");
        return NR_DESLIGADO;
    }
    // 2.5. O RESHADE CHEGOU A CARREGAR?
    // Sem ele, o passo 3 nunca acha nada no log e o estado ficava em INICIANDO para
    // SEMPRE, com o texto "esperando o primeiro frame passar pela rede" - uma promessa que
    // nao ia se cumprir nunca. Acontece com quem copiou os arquivos mas nao rodou o setup,
    // e com quem teve o dxgi.dll levado pelo antivirus (comum com add-on de ReShade).
    // Duas provas independentes: o modulo carregado no processo e o arquivo ao lado do exe.
    if (!GetModuleHandleW(L"dxgi.dll") &&
        GetFileAttributesW((g_exeDir + L"dxgi.dll").c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (detalhe) *detalhe = T(L"o ReShade não carregou — rode o setup-dlss5.ps1", L"ReShade did not load — run setup-dlss5.ps1");
        return NR_SEM_ADDON;
    }

    // 2.6. A PLACA E RTX, MAS O DRIVER ACEITOU?
    // O nome da placa e um bom palpite; o NGX e a resposta. Quando o feed ja perguntou e
    // levou nao, o neural nao vai subir por mais que se aperte o botao - e "atualize o
    // driver" e a unica frase util aqui. So depois do 2.5: sem o ReShade o feed nem chega
    // a perguntar, e o -1 dele nao pode virar acusacao.
    {
        std::wstring porqueNgx;
        if (NgxPeloFeed(&porqueNgx) == 0) {
            if (detalhe) *detalhe = porqueNgx;
            return NR_SEM_RTX;
        }
    }

    // 3. o que esta acontecendo: o add-on so escreve estas linhas quando avalia de verdade
    //
    // POR QUE ESTA BUSCA CONTINUA SENDO NO TAIL INTEIRO, e nao so no que veio depois do
    // ultimo toque. Medido no ReShade.log desta maquina: a linha de avaliacao aparece DUAS
    // vezes por sessao, em count=1 e count=60, e nunca mais - ela e marco de "a cadeia
    // rodou", nao batimento. Exigir que ela viesse depois do ultimo "NR toggled" deixaria
    // o card em INICIANDO ate o prazo e depois em DESLIGADO para quem simplesmente
    // desligou e religou o neural: trocar um estado grudado em ATIVO por um grudado em
    // DESLIGADO nao e conserto.
    //
    // O que conserta o grude e o passo 2, que agora le o ON/OFF do log em vez do ini: com
    // o ultimo toque em OFF nem se chega aqui. O que sobra e o caso do religamento sem
    // linha nova - e ai "a cadeia deste processo ja avaliou e o neural esta ligado agora" e
    // a verdade mais forte que existe para dizer.
    const std::string tail = TailArquivo(g_exeDir + L"ReShade.log", 96 * 1024);
    const bool avaliando = tail.find("inline feature 18 evaluation succeeded") != std::string::npos ||
                           tail.find("first NGX evaluate intercepted") != std::string::npos ||
                           tail.find("NGX feature create intercepted: feature=1") != std::string::npos;
    if (avaliando) {
        t0Iniciando = 0;   // subiu: se cair depois, a contagem recomeca
        if (detalhe) *detalhe = T(L"a rede neural está refazendo a imagem", L"the neural network is rebuilding the image");
        return NR_ATIVO;
    }
    // INICIANDO tem prazo. Passados ~25 s sem uma linha de avaliacao no log, a cadeia nao
    // vai subir sozinha - insistir em "ja vai" e mentira. O relogio comeca na primeira vez
    // que passamos por aqui nesta execucao, nao na abertura do app: o ReShade so compila
    // os shaders quando o primeiro Present acontece.
    if (!t0Iniciando) t0Iniciando = GetTickCount64();
    if (GetTickCount64() - t0Iniciando > 25000) {
        if (detalhe) *detalhe = T(L"não subiu — confira \"DLSS 5 Feed\" no ReShade, ou reabra o app", L"it did not come up — check \"DLSS 5 Feed\" in ReShade, or reopen the app");
        return NR_DESLIGADO;
    }
    if (detalhe) *detalhe = T(L"ligado, esperando o primeiro frame passar pela rede", L"on, waiting for the first frame through the network");
    return NR_INICIANDO;
}

// ------------------------------------------------- a cadeia ja esta ENTREGANDO imagem?
// Pergunta diferente de "o neural esta ligado": aqui e "o que a camada mostra ja passou
// pela cadeia, ou ainda e um quadro parado?". Serve ao aquecimento em bypass (ver o laco
// principal) e por isso aceita TRES respostas independentes - a cadeia pode estar
// entregando com o neural desligado, e nesse caso o feed e quem responde:
//   1. o neural ATIVO (a resposta mais forte: a rede refez pelo menos um quadro);
//   2. "frame N delivered" no dlss5-feed.log (o feed entregou, com ou sem neural);
//   3. "evaluation succeeded" no ReShade.log (a avaliacao NGX rodou).
// Sem leitura nova de disco: as duas fontes ja estao no cache do TailArquivo.
static bool CadeiaEntregando() {
    if (LerEstadoNeural(nullptr) == NR_ATIVO) return true;
    const std::string feed = TailArquivo(g_exeDir + L"dlss5-feed.log", 32 * 1024);
    if (feed.find(" delivered (") != std::string::npos) return true;
    const std::string rs = TailArquivo(g_exeDir + L"ReShade.log", 96 * 1024);
    return rs.find("evaluation succeeded") != std::string::npos;
}

// --------------------------------------------------------------- liga/desliga TUDO
// F6 e o antes-e-depois do app inteiro: um toque desliga o que o painel configurou
// (o neural e, no modo sobreposto, a propria camada), outro devolve exatamente o que
// estava. Nao e "desligar o neural" - e "ver a imagem original".
struct EstadoFiltro {
    bool guardado = false;
    bool neural = false;      // o neural estava ligado?
    bool neuralNos = false;   // fomos NOS que o desligamos (ver AlternarTudo)
    bool camada = false;      // a camada estava aparecendo (sem bypass)?
};
static EstadoFiltro g_antes;
static bool g_tudoDesligado = false;

struct HudState {
    HINSTANCE inst   = nullptr;
    HWND      render = nullptr;    // a nossa janela D3D11
    HWND      wnd    = nullptr;    // a janela do painel
    HWND      prevFg = nullptr;    // quem estava em foco quando o painel abriu
    bool      open   = false;
    std::wstring src;              // nome da fonte capturada
    UINT      w = 0, h = 0;
    double    fpsSrc = 0.0, fpsPresent = 0.0;
    bool      overlay = false;     // modo sobreposto (o bypass so faz sentido nele)
    bool      bypass  = false;
    int       fpsCap  = 0;
    int       workRes = 0;         // resolucao de trabalho da rede (0 = automatico pela area)
    bool      vsync   = false;
    bool      hdrTreat = false;    // a caixinha "tratar HDR"
    bool      fpsTela = true;      // contador de FPS do overlay do ReShade ([OVERLAY] ShowFPS)
    bool      hdrFound = false;    // o app detectou HDR no caminho
    bool      hdrActing = false;   // o app esta de fato tratando agora
    std::wstring hdrLine;          // uma linha do hdr_control pro painel
    std::wstring hdrAviso;         // so quando ha algo a fazer (vazio = tudo certo)
    bool      camera  = false;     // fonte camera/placa: o texto muda ("filtro", nao "jogo")
    bool      tudoDesligado = false;  // o F6 desligou o conjunto inteiro
    bool      interativo = false;     // o teclado esta com o filtro
    bool      confirmandoSaida = false;  // primeiro clique no botao de sair
    NeuralEstado neural = NR_DESLIGADO;  // estado real, lido do add-on
    std::wstring neuralDetalhe;          // uma linha explicando esse estado
    bool      neuralPendente = false;    // ha um toque armado esperando confirmacao
    bool      fonteMudou = false;        // o alvo mudou de tamanho depois de abrir
    int       capturandoAtalho = -1;     // qual acao esta esperando uma tecla (-1 = nenhuma)
    std::wstring avisoAtalho;            // "essa tecla ja e de outra acao", etc.
};
static HudState g_hud;

// PONTO UNICO DE SAIDA do modo "aperte a tecla...". Entrar nesse modo chama
// SuspenderAtalhos() (senao a tecla vira WM_HOTKEY e nunca chega ao painel); QUALQUER
// saida tem de chamar RetomarAtalhos(), inclusive as desistencias. Faltava exatamente
// isso no Esc e no clique fora: End, F6 e Alt+Esc ficavam desregistrados o resto da
// sessao, sem nenhum aviso na tela. Devolve true se havia captura em andamento.
static bool CancelarCapturaAtalho() {
    if (g_hud.capturandoAtalho < 0) return false;
    g_hud.capturandoAtalho = -1;
    g_hud.avisoAtalho.clear();
    RetomarAtalhos();
    Log("captura de atalho cancelada - hotkeys de volta no ar");
    return true;
}

// ------------------------------------------------------------------ HDR (outro modulo)
// O hdr_control.h e do agente de HDR: ele detecta HDR por monitor e, se autorizado,
// desliga o HDR do Windows enquanto o app roda (fonte de tela) ou converte PQ/BT.2020
// (fonte placa de captura). Aqui ficam so os quatro pontos de contato da interface.
// Sem o header o app compila e roda igual, com a caixinha sempre desmarcada.
// (o include e a instancia estao no topo do arquivo, junto dos demais)
static void HdrRefreshLine() {
#ifdef TEM_HDR_CONTROL
    g_hdrTr.Detect();
    g_hud.hdrFound = g_hdrTr.HdrFound();
    g_hud.hdrActing = g_hdrTr.Acting();
    g_hud.hdrLine  = g_hdrTr.StatusLine();
    g_hud.hdrAviso = g_hdrTr.WarnLine();   // vazio quando a conversao automatica da conta
#else
    g_hud.hdrFound = false;
    g_hud.hdrActing = false;
    g_hud.hdrLine.clear();
    g_hud.hdrAviso.clear();
#endif
}
// Empurra pro pixel shader o que o modulo decidiu. Com mode 0 (o normal) o PS e
// passthrough - o custo pra quem nao tem HDR e zero.
static void HdrFeedShader(Renderer& R) {
#ifdef TEM_HDR_CONTROL
    Hdr::ShaderParams p = g_hdrTr.Shader();
    R.SetHdrParams(p.mode, p.fixMatrix, p.refWhiteNits, p.peakNits, p.knee);
#else
    (void)R;
#endif
}
static bool HdrSetTreat(bool on) {
#ifdef TEM_HDR_CONTROL
    return g_hdrTr.SetEnabled(on);
#else
    (void)on; return false;
#endif
}
static void HdrShutdown() {
#ifdef TEM_HDR_CONTROL
    g_hdrTr.Restore();
#endif
}
// O aviso pro cartaz, escrito pelo modulo (o texto muda entre fonte de tela e placa).
// Chamado tambem pelo Fatal(), que sai por ExitProcess e nao roda destrutor nem atexit:
// um erro fatal nao pode deixar o monitor do usuario com o HDR desligado.
static void RestoreHdrOnExit() {
#ifdef TEM_HDR_CONTROL
    Hdr::Manager::RestoreActive();
#endif
}

// ------------------------------------------------------------------ grupo de ofertas
// O app e de graca; quem paga a conta e o canal do usuario. A tela do convite (QR +
// botao) e do splash_ofertas.h - aqui so existe o ponto de chamada. Sem o header, o
// item do painel abre o link no navegador, que resolve igual.
// (o include esta no topo do arquivo, junto dos demais)
static const wchar_t* kUrlGrupo = L"https://grupo.pcdofafa.com.br/pg";
// O canal do criador, lido do ofertas.ini (chave `canal`) na abertura. Vazio = o app nao
// tem canal para divulgar, e ai o item "Apoie o criador" nem aparece no painel nem no
// launcher. Prometer um link que nao existe e pior do que nao ter o botao.
static std::wstring g_urlCanal;
static std::atomic<bool> g_ofertasAberto{ false };

// A area onde a tela do grupo deve nascer, no tamanho FISICO dela.
//
// A splash agora escala por DPI (880x452 sao unidades logicas: 1100x565 a 125%, 1320x678 a
// 150%). Perguntar "cabe?" ao AreaParaJanela com os numeros logicos respondia sim para uma
// janela de jogo que NAO cabe o cartao de verdade - e o splash_ofertas.h, ao descobrir isso,
// cai para a area de trabalho do primario, abrindo a tela no monitor errado.
//
// A ordem tem de ser esta: uma area provisoria diz em QUE monitor estamos, o monitor diz o
// DPI, e so entao da para perguntar de novo com o tamanho certo. O DPI sai do proprio
// header, para respeitar o DLSS5_SPLASH_DPI e nao existirem duas contas diferentes.
static RECT AreaDaTelaDeOfertas() {
#ifdef TEM_SPLASH_OFERTAS
    const RECT provisoria = AreaParaJanela(880, 452);
    const POINT centro{ (provisoria.left + provisoria.right) / 2,
                        (provisoria.top + provisoria.bottom) / 2 };
    const UINT dpi = ofertas::detail::DpiEfetivoPonto(centro);
    return AreaParaJanela(ofertas::detail::Esc(880, dpi), ofertas::detail::Esc(452, dpi));
#else
    return AreaParaJanela(880, 452);
#endif
}

// Roda numa thread propria: a tela do convite tem loop de mensagens e contagem
// regressiva, e travar o loop principal congelaria o filtro na cara do usuario.
static bool g_ofertasNaCaptura = true;   // decidido pela fonte; ver ConfigurarOfertas()
static std::thread g_thOfertas;          // a tela do grupo aberta pelo painel/bandeja
static DWORD g_threadPrincipal = 0;      // quem recebe as hotkeys globais (RegisterHotKey(nullptr,...))

static void AbrirGrupoOfertas(HINSTANCE hi) {
    if (g_ofertasAberto.exchange(true)) return;
#ifdef TEM_SPLASH_OFERTAS
    // A tela do grupo E propaganda do canal: ela deve sair no video gravado/transmitido,
    // entao NAO fica fora da captura - com uma excecao. Na fonte monitor sobreposto a
    // nossa propria captura filmaria a splash e ela entraria no filtro, virando eco.
    SplashOpcoes op;
    op.esconderDaCaptura = !g_ofertasNaCaptura;
    op.area = AreaDaTelaDeOfertas();      // no tamanho FISICO, ver AreaDaTelaDeOfertas
    Log("grupo: area %ld,%ld-%ld,%ld  na captura=%d (alvo=%p mon=%p)",
        op.area.left, op.area.top, op.area.right, op.area.bottom,
        (int)g_ofertasNaCaptura, (void*)g_alvoWin, (void*)g_alvoMon);
    // COM PROPRIA: esta thread chama ShellExecuteW (o botao do grupo abre o navegador), e o
    // shell exige COM inicializado NA THREAD que chama. Sem isto o clique podia nao abrir
    // nada, em silencio - o pior desfecho para o unico botao que o app pede que a pessoa
    // aperte. APARTMENTTHREADED e o que o shell espera.
    //
    // E guardada, nao detach: no encerramento esperamos ela sair (ver g_thOfertas), senao o
    // processo pode terminar com a janela dela ainda viva.
    if (g_thOfertas.joinable()) g_thOfertas.join();   // so ha uma por vez (g_ofertasAberto)
    g_thOfertas = std::thread([hi, op]() {
        const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ShowOfferSplash(hi, kUrlGrupo, nullptr, nullptr, 0, op);
        if (SUCCEEDED(hrCo)) CoUninitialize();
        g_ofertasAberto = false;
    });
#else
    ShellExecuteW(nullptr, L"open", kUrlGrupo, nullptr, nullptr, SW_SHOWNORMAL);
    g_ofertasAberto = false;
#endif
    Log("grupo de ofertas aberto pelo painel");
}

// A MESMA tela, na abertura do app, antes do seletor - e aqui ela e bloqueante.
//
// Por que na abertura e nao junto com a captura: e o momento em que a pessoa esta parada,
// decidindo, e nao no meio de um jogo. Por que bloqueante: o seletor so deve aparecer
// quando ela fechar (Agora nao / X / Esc / o botao do grupo). Nao ha contagem regressiva
// em lugar nenhum - a tela fica ate a pessoa agir, no modelo do WinRAR.
//
// Aqui a splash NAO se esconde da captura: o alvo ainda nem foi escolhido, nao existe
// captura nossa para ela ecoar, e ela e propaganda do canal - deve sair em quem estiver
// gravando a tela.
// Devolve false quando a pessoa pediu para SAIR pelo atalho enquanto a tela estava aberta.
static bool OfertasNaAbertura(HINSTANCE hi) {
#ifdef TEM_SPLASH_OFERTAS
    if (!ofertas::DeveMostrarNaAbertura()) return true;
    SplashOpcoes op;
    op.esconderDaCaptura = false;
    op.area = AreaDaTelaDeOfertas();      // sem alvo ainda: cai na area util do primario
    Log("grupo na abertura: area %ld,%ld-%ld,%ld (antes do seletor, sem prazo)",
        op.area.left, op.area.top, op.area.right, op.area.bottom);
    g_ofertasAberto = true;

    // A tela roda em thread PROPRIA e o laco de mensagens fica aqui, em vez de simplesmente
    // chamar ShowOfferSplash na thread principal.
    //
    // O motivo e o atalho de sair. WM_HOTKEY e mensagem de THREAD (hwnd nulo): o laco
    // interno da splash a recebe e o DispatchMessage a joga fora, entao Alt+Esc ficava
    // inerte enquanto a propaganda estivesse aberta - e ela nao fecha mais sozinha. Com o
    // laco aqui, a tecla e vista e respeitada.
    //
    // Sem dupla pressao neste ponto, de proposito: a confirmacao existe porque a
    // combinacao pode ser apertada sem querer NO MEIO DE UM JOGO. Aqui nao ha jogo - a
    // pessoa esta olhando para um convite e apertou "sair" de propria vontade. Pedir
    // confirmacao seria transformar a propaganda em obstaculo, que e exatamente o que nao
    // pode acontecer.
    std::atomic<bool> fechou{ false };
    bool pediuSair = false;
    std::thread t([hi, op, &fechou]() {
        ShowOfferSplash(hi, kUrlGrupo, nullptr, nullptr, 0, op);
        fechou = true;
        PostThreadMessageW(g_threadPrincipal, WM_NULL, 0, 0);   // acorda o laco de baixo
    });
    while (!fechou) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_HOTKEY && (int)m.wParam == HK_ACAO_BASE + AT_SAIR) {
                pediuSair = true;
                if (HWND w = FindWindowW(L"DLSS5ForAllOfertas", nullptr)) PostMessageW(w, WM_CLOSE, 0, 0);
                Log("grupo na abertura: %s pedido - encerrando", U8(NomeTeclaCanonica(g_atalhos[AT_SAIR].mods, g_atalhos[AT_SAIR].vk)).c_str());
            }
            // As outras hotkeys (painel, tudo) nao tem o que fazer antes da captura subir:
            // sao consumidas aqui para nao se acumularem na fila ate o laco principal.
            TranslateMessage(&m); DispatchMessageW(&m);
        }
    }
    t.join();
    g_ofertasAberto = false;
    Log("grupo na abertura: fechado, %s", pediuSair ? "a pedido de sair" : "seguindo para o seletor");
    return !pediuSair;
#else
    (void)hi; return true;
#endif
}

namespace Hud {
using namespace Gdiplus;

static const int SH  = 20;    // margem reservada pra sombra
static const int CW  = 524;   // largura do cartao
static const int PAD = 26;    // respiro interno

// O processo e PER_MONITOR_AWARE_V2 (a janela de video tem que ser 1:1 com a captura),
// entao ninguem escala nada por nos. O painel, ao contrario do video, e interface: ele
// tem que crescer junto com a escala do Windows, senao num monitor a 125% ou 150% sai
// um cartaozinho de letra miuda. Todo o layout e escrito em unidades logicas (96 dpi) e
// a escala entra de uma vez so, no Graphics.
static REAL g_scale = 1.f;
static void UpdateScale(HWND ref) {
    UINT dpi = 96;
    typedef UINT(WINAPI* PFN)(HWND);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    PFN f = u ? (PFN)GetProcAddress(u, "GetDpiForWindow") : nullptr;
    if (f && ref) dpi = f(ref);
    if (!dpi) dpi = 96;
    g_scale = (REAL)dpi / 96.f;
}

enum EKind { E_NONE = 0, E_FILTER, E_NEURAL, E_RESHADE, E_FPS, E_WORKRES,
             E_VSYNC, E_HDR, E_OFERTAS, E_CANAL, E_GUIA, E_INTERATIVO, E_SAIR,
             E_ATALHO, E_ATALHO_PADRAO, E_REABRIR, E_FPSTELA, E_IDIOMA };
struct Item { RectF r; int kind; int val; bool enabled; bool on; };
static std::vector<Item> g_items;
static int g_hot = -1;
static int g_winW = 0, g_winH = 0;

// ------------------------------------------------------- o painel pode ser arrastado
//
// Ele nasce centralizado, e ate aqui essa era a unica posicao possivel: o Paint chamava
// PosicaoParaJanela a cada pintura e devolvia o cartao para o meio da tela. Num monitor
// ultrawide o meio e justamente onde esta o que a pessoa quer olhar enquanto mexe no
// filtro. Agora ele se move com o mouse e a posicao fica gravada no ini.
//
// So arrasta quem pega o cartao FORA de um controle - clicar num chip continua clicando
// no chip. E o arrasto usa GetCursorPos, nao as coordenadas do WM_MOUSEMOVE: com a janela
// se movendo debaixo do cursor, as coordenadas de cliente mudam de referencia a cada
// mensagem e o cartao "foge" da mao.
static bool  g_posValida = false;     // false = o painel se posiciona sozinho, como antes
static POINT g_pos{ 0, 0 };
static bool  g_arrastando = false;
static POINT g_arrastoOff{ 0, 0 };

// Mantem o cartao dentro da area util do monitor onde ele esta. Vale para a posicao
// gravada tambem: um ini de dois monitores aberto com um so nao pode esconder o painel.
static void ClamparNaTela(POINT& p, int W, int H) {
    const POINT centro{ p.x + W / 2, p.y + H / 2 };
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromPoint(centro, MONITOR_DEFAULTTONEAREST), &mi)) return;
    if (p.x + W > mi.rcWork.right)  p.x = mi.rcWork.right - W;
    if (p.y + H > mi.rcWork.bottom) p.y = mi.rcWork.bottom - H;
    if (p.x < mi.rcWork.left) p.x = mi.rcWork.left;
    if (p.y < mi.rcWork.top)  p.y = mi.rcWork.top;
}
static bool g_closing = false;

// Paleta da marca (marca/PALETA.md, tirada do grupo.pcdofafa.com.br): preto profundo,
// superficies azul-ardosia, acento ciano indo pro azul. Mesmo DNA do site, mas isto
// aqui e ferramenta de trabalho - o ciano marca estado, nao decora.
static const Color C_CARD (249,  17,  24,  39);   // #111827
static const Color C_EDGE (170,  31,  41,  55);   // #1f2937
static const Color C_TXT  (255, 255, 255, 255);   // titulos
static const Color C_BODY (255, 209, 213, 219);   // #d1d5db
static const Color C_DIM  (255, 156, 163, 175);   // #9ca3af
static const Color C_FAINT(255, 107, 114, 128);   // #6b7280
static const Color C_ON   (255,   6, 182, 212);   // #06b6d4 ciano principal
static const Color C_ACC  (255,   6, 182, 212);   // selecao = mesmo ciano
static const Color C_ACC2 (255,  34, 211, 238);   // #22d3ee claro/hover
static const Color C_BLUE (255,  37,  99, 235);   // #2563eb
static const Color C_ROW  (255,  31,  41,  55);   // #1f2937 cards
static const Color C_ROWH (255,  45,  58,  76);   // hover
static const Color C_OFF  (255,  55,  65,  81);
static const Color C_DARK (255,   3,  14,  22);   // texto sobre o ciano
static const Color C_WARN (255, 251, 146,  60);   // #fb923c
static const Color C_SEP  ( 46, 255, 255, 255);

static Font *fTitle = nullptr, *fVer = nullptr, *fSub = nullptr, *fLbl = nullptr,
            *fItem = nullptr, *fItemSub = nullptr, *fChip = nullptr, *fBadge = nullptr,
            *fStat = nullptr, *fStatLbl = nullptr;

static Font* MkFont(const wchar_t* fam, REAL px, INT style) {
    Font* f = new Font(fam, px, style, UnitPixel);
    if (f->GetLastStatus() != Ok) { delete f; f = new Font(L"Segoe UI", px, style, UnitPixel); }
    return f;
}
static void EnsureFonts() {
    if (fTitle) return;
    fTitle   = MkFont(L"Segoe UI Semibold", 21.f, FontStyleBold);
    fVer     = MkFont(L"Segoe UI",          12.f, FontStyleRegular);
    fSub     = MkFont(L"Segoe UI",          14.f, FontStyleRegular);
    fLbl     = MkFont(L"Segoe UI Semibold", 11.f, FontStyleRegular);
    fItem    = MkFont(L"Segoe UI Semibold", 16.f, FontStyleRegular);
    fItemSub = MkFont(L"Segoe UI",          12.f, FontStyleRegular);
    fChip    = MkFont(L"Segoe UI",          13.f, FontStyleRegular);
    fBadge   = MkFont(L"Segoe UI Semibold", 13.f, FontStyleRegular);
    fStat    = MkFont(L"Segoe UI Semibold", 18.f, FontStyleRegular);
    fStatLbl = MkFont(L"Segoe UI",          10.f, FontStyleRegular);
}

static void RoundPath(GraphicsPath& p, const RectF& r, REAL rad) {
    REAL d = rad * 2;
    p.AddArc(r.X, r.Y, d, d, 180.f, 90.f);
    p.AddArc(r.X + r.Width - d, r.Y, d, d, 270.f, 90.f);
    p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.f, 90.f);
    p.AddArc(r.X, r.Y + r.Height - d, d, d, 90.f, 90.f);
    p.CloseFigure();
}
static void FillRound(Graphics* g, const RectF& r, REAL rad, const Color& c) {
    GraphicsPath p; RoundPath(p, r, rad); SolidBrush b(c); g->FillPath(&b, &p);
}
static void StrokeRound(Graphics* g, const RectF& r, REAL rad, const Color& c, REAL w) {
    GraphicsPath p; RoundPath(p, r, rad); Pen pen(c, w); g->DrawPath(&pen, &p);
}
// align: 0 esquerda, 1 centro, 2 direita. A caixa centraliza o texto na vertical.
static void Txt(Graphics* g, const wchar_t* s, Font* f, const Color& c, const RectF& box,
                int align, bool ellipsis = false) {
    StringFormat sf;
    sf.SetAlignment(align == 0 ? StringAlignmentNear : align == 1 ? StringAlignmentCenter : StringAlignmentFar);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    if (ellipsis) sf.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    g->DrawString(s, -1, f, box, &sf, &b);
}
// Titulo em caixa alta com espaco entre as letras, no espirito do Oswald do site.
// O GDI+ nao tem letter-spacing, entao vai letra por letra.
static void TxtSpaced(Graphics* g, const wchar_t* s, Font* f, const Color& c,
                      REAL x, REAL y, REAL h, REAL extra) {
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    SolidBrush b(c);
    REAL cx = x;
    for (const wchar_t* p = s; *p; p++) {
        wchar_t one[2] = { *p, 0 };
        RectF box(cx, y, 200.f, h);
        g->DrawString(one, 1, f, box, &sf, &b);
        RectF med{};
        g->MeasureString(one, 1, f, box, &sf, &med);
        cx += med.Width + extra;
    }
}
// Chavinha liga/desliga: um trilho arredondado com a bolinha de um lado ou do outro.
static void Switch(Graphics* g, const RectF& r, bool on, bool enabled) {
    FillRound(g, r, r.Height / 2, on && enabled ? C_ON : C_OFF);
    REAL d = r.Height - 6;
    REAL cx = on ? (r.X + r.Width - d - 3) : (r.X + 3);
    SolidBrush knob(enabled ? Color(255, 255, 255, 255) : Color(255, 170, 176, 188));
    g->FillEllipse(&knob, cx, r.Y + 3, d, d);
}

// Uma funcao so faz o layout e o desenho: com g == nullptr ela apenas mede (e preenche
// a lista de areas clicaveis), com g != nullptr ela desenha. Assim nao existe a chance
// classica de o retangulo do clique e o do desenho sairem do lugar um do outro.
static int Render(Graphics* g) {
    EnsureFonts();
    g_items.clear();
    const REAL L = (REAL)(SH + PAD), IW = (REAL)(CW - PAD * 2), R = L + IW;
    const REAL top = (REAL)SH;
    REAL cy = top + 22;
    // UMA leitura por layout, nao duas: TeclaOverlayReShade() faz GetPrivateProfileStringW
    // no ReShade.ini, e ela era chamada no card do ReShade E no rodape. Como o Paint roda
    // o layout duas vezes, isso dava QUATRO leituras de ini por repintura do painel - I/O
    // de disco no caminho de desenho, uma vez por segundo.
    const std::wstring keyReShade = TeclaOverlayReShade();

    // -------- cabecalho
    if (g) {
        TxtSpaced(g, L"DLSS5 FOR ALL", fTitle, C_TXT, L, cy, 30.f, 1.6f);
        Txt(g, APP_VERSION, fVer, C_FAINT, RectF(R - 50, cy + 8, 50, 18), 2);
    }
    cy += 32;
    {
        std::wstring sub = TS(L"filtrando  ·  ", L"filtering  ·  ") +
                           (g_hud.src.empty() ? TS(L"(sem fonte)", L"(no source)") : g_hud.src);
        if (g) Txt(g, sub.c_str(), fSub, C_DIM, RectF(L, cy, IW, 20), 0, true);
    }
    cy += 30;

    // -------- placar. A chegada e o present divergem de proposito desde que a captura
    // destravou (WGC a ~100 fps): a diferenca entre os dois numeros e o custo do filtro.
    {
        const REAL gap = 8, bw = (IW - gap * 3) / 4, bh = 54;
        wchar_t a[64], b[64], c[64];
        _snwprintf(a, 64, L"%u×%u", g_hud.w, g_hud.h);
        _snwprintf(b, 64, L"%.0f fps", g_hud.fpsSrc);
        _snwprintf(c, 64, L"%.0f fps", g_hud.fpsPresent);
        const wchar_t* nrv = g_hud.neural == NR_ATIVO      ? T(L"ATIVO",     L"ACTIVE")
                           : g_hud.neural == NR_INICIANDO  ? T(L"subindo",   L"starting")
                           : g_hud.neural == NR_SEM_RTX    ? T(L"sem RTX",   L"no RTX")
                           : g_hud.neural == NR_SEM_ADDON  ? T(L"ausente",   L"missing")
                                                           : T(L"desligado", L"off");
        const wchar_t* lbl[4] = { T(L"RESOLUÇÃO", L"RESOLUTION"), T(L"CHEGADA", L"INCOMING"),
                                  L"PRESENT", L"NEURAL" };
        const wchar_t* val[4] = { a, b, c, nrv };
        // present bem abaixo da chegada = o filtro esta segurando; pinta de laranja
        const bool apertado = g_hud.fpsSrc >= 20.0 && g_hud.fpsPresent < g_hud.fpsSrc * 0.6 && g_hud.fpsCap == 0;
        for (int i = 0; i < 4; i++) {
            RectF r(L + i * (bw + gap), cy, bw, bh);
            if (g) {
                FillRound(g, r, 10.f, C_ROW);
                Txt(g, lbl[i], fStatLbl, C_FAINT, RectF(r.X, r.Y + 9, r.Width, 14), 1);
                Color cor = C_TXT;
                if (i == 2 && apertado) cor = C_WARN;
                if (i == 3) cor = (g_hud.neural == NR_ATIVO) ? C_ON
                                : (g_hud.neural == NR_INICIANDO) ? C_ACC2 : C_WARN;
                Txt(g, val[i], (i == 3) ? fBadge : fStat, cor,
                    RectF(r.X + 4, r.Y + 26, r.Width - 8, 22), 1, true);
            }
        }
        cy += bh + 18;
    }
    if (g) { Pen p(C_SEP, 1.f); g->DrawLine(&p, L, cy, R, cy); }
    cy += 18;

    // -------- DLSS 5: UM CARD SO, e essa foi uma decisao do usuario, nao de arquitetura.
    //
    // Antes eram dois: "NEURAL RENDERING" (o efeito caro, dentro da cadeia) e "Filtro
    // DLSS 5" (a camada por cima do alvo). Tecnicamente sao coisas diferentes - a camada
    // funciona sem o neural, e e assim que se mede o custo dele -, mas para quem usa os
    // dois nomes diziam a mesma coisa e um dependia do outro para aparecer. A ordem foi
    // clara: "ou e tudo junto ou nao e". Entao o botao e um so, chamado DLSS 5, e ele
    // HABILITA ou DESABILITA a coisa inteira (mesma acao do F6).
    //
    // O estado do neural continua na tela, mas como INFORMACAO, nao como controle: ele e o
    // motivo do app existir e o unico jeito de perceber que a cadeia nao subiu. Um app que
    // esconde isso deixa a pessoa concluindo sozinha que "o filtro nao faz nada".
    {
        RectF r(L, cy, IW, 96);
        const int idx = (int)g_items.size();
        const bool neuralOk = (g_hud.neural == NR_ATIVO);
        const bool subindo  = (g_hud.neural == NR_INICIANDO);
        // NR_SEM_RTX e NR_SEM_ADDON: nao existe nada para ligar, e um botao que nao pode
        // dar certo e pior do que botao nenhum.
        const bool temAddon = (g_hud.neural != NR_SEM_ADDON && g_hud.neural != NR_SEM_RTX);
        // HABILITADO SO COM AS DUAS PARTES DE PE. Antes esta linha olhava so a camada, e o
        // card anunciava HABILITADO com o neural desligado no ReShade.ini - a pessoa
        // apertava, lia "habilitado" e a imagem nao mudava nada.
        const bool camadaOn = g_hud.overlay ? !g_hud.bypass : true;
        const bool ligado   = camadaOn && (neuralOk || subindo || !temAddon);
        // Enquanto um toque espera confirmacao o card RECUSA cliques: o toque e um PULSO
        // que o RenoDX le no proximo frame dele, e dois pulsos armados antes da resposta
        // se cancelam - a pessoa voltaria ao estado inicial achando que "as vezes nao liga".
        const bool en = g_hud.overlay && temAddon && !g_hud.neuralPendente;
        Item it{ r, E_FILTER, 0, en, ligado }; g_items.push_back(it);
        if (g) {
            FillRound(g, r, 12.f, (g_hot == idx && en) ? C_ROWH : C_ROW);
            FillRound(g, RectF(r.X, r.Y + 12, 5.f, r.Height - 24), 2.f,
                      !ligado ? C_OFF : (neuralOk ? C_ON : (subindo ? C_ACC2 : C_WARN)));
            Txt(g, L"DLSS 5", fLbl, C_FAINT, RectF(r.X + 20, r.Y + 10, r.Width - 40, 16), 0);
            Txt(g, (!g_hud.overlay || ligado) ? T(L"HABILITADO", L"ENABLED") : T(L"DESABILITADO", L"DISABLED"),
                fTitle, ligado ? (neuralOk || subindo ? C_ON : C_WARN) : C_BODY,
                RectF(r.X + 20, r.Y + 28, r.Width - 150, 30), 0, true);
            // A LINHA DE BAIXO E O DIAGNOSTICO, e ela muda com o que importa saber agora.
            const wchar_t* sub =
                  g_hud.neuralPendente             ? T(L"confirmando com o add-on…",
                                                       L"confirming with the add-on…")
                : g_hud.neural == NR_SEM_RTX       ? T(L"o Neural Rendering não roda nesta placa",
                                                       L"Neural Rendering does not run on this GPU")
                : g_hud.neural == NR_SEM_ADDON     ? T(L"o Neural Rendering não está instalado",
                                                       L"Neural Rendering is not installed")
                // As duas linhas abaixo separam os dois jeitos de estar DESABILITADO, e a
                // diferenca importa: sem a camada e escolha da pessoa; com a camada na
                // frente e o neural desligado e o estado que fazia o app mentir.
                : (!camadaOn)                      ? T(L"imagem original, sem filtro — o jogo roda como sempre",
                                                       L"original image, no filter — the game runs as always")
                : (!neuralOk && !subindo)          ? T(L"a camada está na frente, mas o neural está desligado",
                                                       L"the overlay is up front, but the neural is off")
                : subindo                          ? T(L"neural ligado, esperando o primeiro quadro passar pela rede",
                                                       L"neural on, waiting for the first frame through the network")
                                                   : T(L"neural ativo — a rede refaz a imagem",
                                                       L"neural active — the network is rebuilding the image");
            Txt(g, sub, fItemSub, (ligado && !neuralOk && !subindo && !g_hud.neuralPendente) ? C_WARN : C_BODY,
                RectF(r.X + 20, r.Y + 60, r.Width - 40, 18), 0, true);
            Txt(g, ligado ? T(L"desligar devolve o desempenho na hora",
                              L"turning it off gives the performance back instantly") : L"",
                fItemSub, C_FAINT, RectF(r.X + 20, r.Y + 76, r.Width - 150, 16), 0, true);
            // a tecla desta acao, ao lado do estado: o painel e o controle e a legenda
            RectF bd(r.X + r.Width - 130 - 62, r.Y + 34, 56, 26);
            FillRound(g, bd, 8.f, C_OFF);
            // Nunca a tecla que o Windows recusou: o badge e uma promessa.
            Txt(g, TemAtalho(AT_TUDO) ? TextoAtalho(AT_TUDO).c_str() : L"—",
                fBadge, C_BODY, bd, 1, true);
            RectF pl(r.X + r.Width - 130, r.Y + 30, 112, 34);
            FillRound(g, pl, 17.f, ligado ? C_ON : C_OFF);
            Txt(g, !en ? L"—" : g_hud.neuralPendente ? T(L"aguarde…", L"hold on…")
                              : ligado ? T(L"desabilitar", L"disable") : T(L"habilitar", L"enable"),
                fBadge, ligado && en && !g_hud.neuralPendente ? C_DARK : C_BODY, pl, 1);
        }
        cy += 96 + 10;
    }

    cy += 6;   // (aqui moravam os chips de COR/LUT e de nitidez - ver "cor e nitidez: FORA")

    // -------- teto de fps do filtro
    {
        if (g) Txt(g, T(L"TETO DE FPS DO FILTRO", L"FILTER FPS CAP"), fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;
        const REAL gap = 8, bw = (IW - gap * 4) / 5, bh = 34;
        for (int i = 0; i < 5; i++) {
            RectF r(L + i * (bw + gap), cy, bw, bh);
            const int idx = (int)g_items.size();
            const bool sel = (g_hud.fpsCap == kFpsCaps[i]);
            Item it{ r, E_FPS, i, true, sel }; g_items.push_back(it);
            if (g) {
                FillRound(g, r, 9.f, sel ? C_ACC : (g_hot == idx ? C_ROWH : C_ROW));
                // so o primeiro chip e palavra ("Máx"); 60/45/30/20 sao numeros
                Txt(g, NomeFpsCap(i), fChip, sel ? C_DARK : C_BODY, r, 1);
            }
        }
        cy += bh + 7;
        // NAO prometa latencia aqui: medido com a GPU livre, o teto piorou o
        // PRESENT->TELA (17 ms com teto 60 contra 6,8 sem). O teto serve para consumo,
        // temperatura e ruido de cooler. Ver docs\latencia-nr.md §4.2-4.3, que tambem
        // registra a quantizacao que eu medi (fonte a 78-80 fps, teto 60 entrega 50).
        if (g) Txt(g, T(L"segura consumo, temperatura e ruído do cooler — não reduz a latência",
                        L"holds down power draw, temperature and fan noise — it does not cut latency"),
                   fItemSub, C_FAINT, RectF(L, cy, IW, 18), 0, true);
        cy += 26;
    }

    // -------- RESOLUÇÃO DO DLSS: a alavanca de desempenho que faltava na tela
    //
    // Este e o unico controle do painel que mexe no CUSTO da rede, e nao no que sobra
    // para o jogo. A rede processa AREA: em 3440x1440 sao 5,0 megapixels por quadro, e o
    // custo dela nao muda se o jogo estiver no ultra ou no baixo - foi medido aqui, com o
    // filtro travado em ~30 ms por quadro (33 fps) por mais que os graficos baixassem.
    // Baixar a porcentagem faz a rede trabalhar em menos pixels e reconstruir: e o que o
    // DLSS existe para fazer, e a unica coisa que levanta esse teto.
    //
    // Vale na PROXIMA ABERTURA, e o cartaz diz isso: quem le o dlss5-feed.cfg e o add-on
    // do feed, uma vez so, quando o ReShade sobe junto com a swapchain.
    {
        if (g) Txt(g, T(L"RESOLUÇÃO DO DLSS", L"DLSS RESOLUTION"), fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;
        const REAL gap = 8, bw = (IW - gap * 4) / 5, bh = 34;
        for (int i = 0; i < 5; i++) {
            RectF r(L + i * (bw + gap), cy, bw, bh);
            const int idx = (int)g_items.size();
            const bool sel = (g_hud.workRes == kWorkRes[i]);
            Item it{ r, E_WORKRES, i, true, sel }; g_items.push_back(it);
            if (g) {
                FillRound(g, r, 9.f, sel ? C_ACC : (g_hot == idx ? C_ROWH : C_ROW));
                Txt(g, kWorkResNames[i], fChip, sel ? C_DARK : C_BODY, r, 1);
            }
        }
        cy += bh + 7;
        if (g) Txt(g, T(L"menor = filtro mais leve e mais fps · Auto escolhe pelo tamanho da tela"
                        L" · vale ao reabrir",
                        L"lower = lighter filter, more fps · Auto picks by screen size"
                        L" · applies on reopen"),
                   fItemSub, C_FAINT, RectF(L, cy, IW, 18), 0, true);
        cy += 26;
    }

    // -------- ajustes: V-Sync, HDR e o FPS na tela, tres chavinhas
    {
        if (g) Txt(g, T(L"AJUSTES", L"SETTINGS"), fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;
        struct Sw { int kind; const wchar_t* nome; std::wstring sub; bool on; bool en; };
        // o rotulo da caixa e do proprio modulo de HDR: a semantica ja mudou uma vez
        // (de "tratar" para "plano B") e o painel nao pode ficar contando historia velha
#ifdef TEM_HDR_CONTROL
        // o texto integral do modulo cabe no seletor (628 px), mas trunca aqui: no painel
        // vale a versao curta, com a mesma semantica de "plano B"
        const wchar_t* kRotuloHdr = T(L"Desligar o HDR do monitor (plano B)",
                                      L"Turn the monitor's HDR off (plan B)");
#else
        const wchar_t* kRotuloHdr = T(L"Desligar o HDR do monitor enquanto captura",
                                      L"Turn the monitor's HDR off while capturing");
#endif
        // O subtexto do switch tem ~378 px; a linha longa do modulo de HDR ("monitor em
        // HDR · capturando em 16 bits e convertendo (automático)") sai truncada no meio da
        // palavra. A versao curta cabe e diz a mesma coisa; a longa continua no seletor,
        // que tem 628 px.
        std::wstring hdrSub = g_hud.hdrLine.empty()
            ? TS(L"desliga o HDR do monitor enquanto o app roda; religa ao sair",
                 L"turns the monitor's HDR off while the app runs; turns it back on when it quits")
            : (g_hud.hdrLine.size() > 52 && g_hud.hdrFound
                   ? TS(g_hud.hdrActing ? L"monitor em HDR · convertendo automaticamente"
                                        : L"monitor em HDR · sem tratamento",
                        g_hud.hdrActing ? L"monitor in HDR · converting automatically"
                                        : L"monitor in HDR · not being handled")
                   : g_hud.hdrLine);
        // O contador de FPS e do overlay do ReShade, e o subtexto diz isso: sem essa frase a
        // pessoa procura o numero no nosso codigo e nao acha. E diz tambem o que ela precisa
        // saber para nao achar que o clique falhou - quem redesenha o overlay e o ReShade, e
        // so na proxima abertura ele com certeza releu esse valor.
        Sw linhas[3] = {
            { E_VSYNC, L"V-Sync",
              TS(g_hud.vsync ? L"espera o vblank — mais estável, teto no refresh do monitor"
                             : L"sem esperar o vblank — não rasga a imagem, só não espera",
                 g_hud.vsync ? L"waits for the vblank — steadier, capped at the monitor's refresh"
                             : L"no waiting for the vblank — it does not tear, it just does not wait"),
              g_hud.vsync, true },
            { E_HDR, kRotuloHdr, hdrSub, g_hud.hdrTreat, true },
            { E_FPSTELA, T(L"Mostrar FPS na tela", L"Show FPS on screen"),
              TS(L"o contador do ReShade no canto — se não sumir agora, some na próxima abertura",
                 L"ReShade's counter in the corner — applies on the next open"),
              g_hud.fpsTela, true },
        };
        for (int i = 0; i < 3; i++) {
            RectF r(L, cy, IW, 52);
            const int idx = (int)g_items.size();
            Item it{ r, linhas[i].kind, 0, linhas[i].en, linhas[i].on }; g_items.push_back(it);
            if (g) {
                FillRound(g, r, 12.f, (g_hot == idx) ? C_ROWH : C_ROW);
                // "caixa marcada" nao quer dizer "o app agiu" - quem responde isso e
                // Acting(). O alerta e para HDR detectado e ninguem tratando.
                const bool alerta = (linhas[i].kind == E_HDR && !g_hud.hdrAviso.empty());
                Txt(g, linhas[i].nome, fItem, C_TXT, RectF(r.X + 18, r.Y + 8, r.Width - 96, 22), 0);
                Txt(g, linhas[i].sub.c_str(), fItemSub, alerta ? C_WARN : C_BODY,
                    RectF(r.X + 18, r.Y + 29, r.Width - 96, 18), 0, true);
                Switch(g, RectF(r.X + r.Width - 66, r.Y + 13, 48, 26), linhas[i].on, linhas[i].en);
            }
            cy += 52 + 8;
        }
        cy += 8;
    }

    // -------- IDIOMA: o segundo lugar onde se troca (o primeiro e o seletor de fonte)
    //
    // Dois chips, iguais aos do teto de fps, e cada nome na propria lingua - quem abriu o
    // app no idioma errado precisa reconhecer "English" ou "Português" sem ler mais nada.
    // O rotulo da secao traz as duas palavras pelo mesmo motivo.
    // Vale NA HORA: o painel inteiro e redesenhado a cada Paint e le os textos no ponto de
    // uso, entao nao ha nada a re-setar aqui.
    {
        if (g) Txt(g, L"IDIOMA / LANGUAGE", fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;
        const REAL gap = 8, bw = (IW - gap) / 2, bh = 34;
        for (int i = 0; i < 2; i++) {
            RectF r(L + i * (bw + gap), cy, bw, bh);
            const int idx = (int)g_items.size();
            const bool sel = (g_lingua == kLinguas[i]);
            Item it{ r, E_IDIOMA, i, true, sel }; g_items.push_back(it);
            if (g) {
                FillRound(g, r, 9.f, sel ? C_ACC : (g_hot == idx ? C_ROWH : C_ROW));
                Txt(g, NomeLingua(kLinguas[i]), fChip, sel ? C_DARK : C_BODY, r, 1);
            }
        }
        cy += bh + 7;
        if (g) Txt(g, T(L"vale na hora, em todo o app · fica lembrado para as próximas vezes",
                        L"applies immediately, everywhere in the app · remembered for next time"),
                   fItemSub, C_FAINT, RectF(L, cy, IW, 18), 0, true);
        cy += 26;
    }

    // -------- ver os atalhos de novo
    {
        RectF r(L, cy, IW, 46);
        const int idx = (int)g_items.size();
        Item it{ r, E_GUIA, 0, true, false }; g_items.push_back(it);
        if (g) {
            FillRound(g, r, 12.f, (g_hot == idx) ? C_ROWH : C_ROW);
            Txt(g, T(L"Ver os atalhos", L"See the shortcuts"), fItem, C_TXT, RectF(r.X + 18, r.Y + 3, r.Width - 130, 22), 0);
            Txt(g, T(L"o guia de teclas, de novo", L"the key guide, once more"), fItemSub, C_BODY, RectF(r.X + 18, r.Y + 24, r.Width - 130, 18), 0);
            // O badge aqui mostrava g_hkPanel (End). Lido em voz alta, "Ver os atalhos ...
            // End" promete que End abre o guia - e End abre o PAINEL. O guia nao tem tecla
            // propria e nao precisa ter: o badge vira o que o clique faz.
            RectF bd(r.X + r.Width - 108, r.Y + 11, 90, 24);
            FillRound(g, bd, 8.f, C_OFF);
            Txt(g, T(L"abrir", L"open"), fBadge, C_BODY, bd, 1, true);
        }
        cy += 46 + 10;
    }

    // -------- abrir o ReShade
    {
        RectF r(L, cy, IW, 56);
        const int idx = (int)g_items.size();
        Item it{ r, E_RESHADE, 0, true, false }; g_items.push_back(it);
        if (g) {
            FillRound(g, r, 12.f, (g_hot == idx) ? C_ROWH : C_ROW);
            StrokeRound(g, r, 12.f, Color(90, 6, 182, 212), 1.f);
            Txt(g, T(L"Abrir o ReShade", L"Open ReShade"), fItem, C_TXT, RectF(r.X + 18, r.Y + 10, r.Width - 110, 22), 0);
            Txt(g, T(L"presets, shaders e add-ons — o mouse vai pra ele",
                     L"presets, shaders and add-ons — the mouse goes to it"),
                fItemSub, C_BODY, RectF(r.X + 18, r.Y + 31, r.Width - 110, 18), 0);
            // a tecla do overlay vem do ReShade.ini: se a pessoa mudar la, o painel segue
            RectF bd(r.X + r.Width - 96, r.Y + 15, 78, 26);
            FillRound(g, bd, 8.f, C_OFF);
            Txt(g, keyReShade.c_str(), fBadge, C_BODY, bd, 1, true);
        }
        cy += 56 + 12;
    }

    // -------- ATALHOS: a pessoa define as proprias teclas
    // Uma linha por acao, com a tecla atual e um botao "mudar" que escuta a proxima
    // tecla. As acoes que nascem sem tecla ficam com "definir" - o padrao continua sendo
    // duas teclas so, e quem quiser mais, configura.
    {
        if (g) Txt(g, T(L"ATALHOS", L"SHORTCUTS"), fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;
        for (int i = 0; i < AT_COUNT; i++) {
            RectF r(L, cy, IW, 34);
            const int idx = (int)g_items.size();
            const bool capturando = (g_hud.capturandoAtalho == i);
            Item it{ r, E_ATALHO, i, true, capturando }; g_items.push_back(it);
            if (g) {
                FillRound(g, r, 9.f, capturando ? Color(255, 30, 58, 76) : (g_hot == idx ? C_ROWH : C_ROW));
                if (capturando) StrokeRound(g, r, 9.f, C_ACC, 1.f);
                Txt(g, RotuloAtalho(i), fChip, C_BODY, RectF(r.X + 14, r.Y, r.Width - 190, r.Height), 0, true);
                RectF bd(r.X + r.Width - 164, r.Y + 4, 104, 26);
                const bool recusado = g_atalhos[i].vk && !g_atalhos[i].registrado;
                FillRound(g, bd, 7.f, C_OFF);
                Txt(g, capturando ? T(L"aperte…", L"press…")
                                  : g_atalhos[i].vk ? TextoAtalho((AcaoAtalho)i).c_str() : T(L"definir", L"set"),
                    fBadge, recusado ? C_WARN : (g_atalhos[i].vk ? C_TXT : C_FAINT), bd, 1, true);
                RectF mb(r.X + r.Width - 54, r.Y + 4, 44, 26);
                FillRound(g, mb, 7.f, (g_hot == idx) ? C_ACC : C_ROW);
                Txt(g, T(L"mudar", L"change"), fItemSub, (g_hot == idx) ? C_DARK : C_DIM, mb, 1, true);
            }
            cy += 34 + 5;
        }
        {   // restaurar padroes
            RectF r(L, cy, 200, 30);
            const int idx = (int)g_items.size();
            Item it{ r, E_ATALHO_PADRAO, 0, true, false }; g_items.push_back(it);
            if (g) {
                FillRound(g, r, 8.f, (g_hot == idx) ? C_ROWH : C_ROW);
                Txt(g, T(L"restaurar padrões", L"restore defaults"), fItemSub, C_BODY, r, 1);
            }
            if (g && g_hud.capturandoAtalho >= 0)
                Txt(g, T(L"aperte a tecla (com Ctrl/Alt/Shift se quiser) · Esc cancela",
                         L"press the key (with Ctrl/Alt/Shift if you like) · Esc cancels"),
                    fItemSub, C_ACC2, RectF(L + 212, cy, IW - 212, 30), 0, true);
            else if (g && !g_hud.avisoAtalho.empty())
                Txt(g, g_hud.avisoAtalho.c_str(), fItemSub, C_WARN, RectF(L + 212, cy, IW - 212, 30), 0, true);
            cy += 30 + 16;
        }
    }

    // -------- teclado pro ReShade e sair: o que antes so existia como atalho
    {
        const REAL gap = 10, bw = (IW - gap) / 2, bh = 46;
        RectF r1(L, cy, bw, bh), r2(L + bw + gap, cy, bw, bh);
        const int i1 = (int)g_items.size();
        Item a{ r1, E_INTERATIVO, 0, g_hud.overlay, false }; g_items.push_back(a);
        const int i2 = (int)g_items.size();
        Item b{ r2, E_SAIR, 0, true, false }; g_items.push_back(b);
        if (g) {
            FillRound(g, r1, 12.f, (g_hot == i1) ? C_ROWH : C_ROW);
            Txt(g, g_hud.interativo ? T(L"Fechar o ReShade e devolver o mouse", L"Close ReShade and give the mouse back")
                                    : T(L"Abrir o ReShade", L"Open ReShade"),
                fItem, g_hud.overlay ? C_TXT : C_FAINT, RectF(r1.X + 16, r1.Y + 3, r1.Width - 32, 22), 0, true);
            Txt(g, g_hud.overlay ? T(L"o mouse vai pra ele; aperte de novo pra voltar",
                                     L"the mouse goes to it; press again to come back")
                                 : T(L"só no modo sobreposto", L"overlay mode only"),
                fItemSub, C_BODY, RectF(r1.X + 16, r1.Y + 24, r1.Width - 32, 18), 0, true);

            FillRound(g, r2, 12.f, (g_hot == i2) ? Color(255, 92, 40, 46) : C_ROW);
            Txt(g, g_hud.confirmandoSaida ? T(L"Clique de novo para sair", L"Click again to quit")
                                          : T(L"Sair do DLSS5 For All", L"Quit DLSS5 For All"),
                fItem, g_hud.confirmandoSaida ? C_WARN : C_TXT, RectF(r2.X + 16, r2.Y + 3, r2.Width - 32, 22), 0, true);
            Txt(g, TemAtalho(AT_SAIR) ? (TextoAtalho(AT_SAIR) + TS(L" duas vezes também sai",
                                                                   L" twice also quits")).c_str()
                                      : T(L"devolve tudo como estava", L"puts everything back the way it was"),
                fItemSub, C_BODY, RectF(r2.X + 16, r2.Y + 24, r2.Width - 32, 18), 0, true);
        }
        cy += bh + 12;
    }

    // -------- reabrir: aparece só quando o alvo mudou de tamanho e o filtro ficou preso
    // na resolução em que nasceu (ver FonteMudou)
    if (g_hud.fonteMudou) {
        RectF r(L, cy, IW, 52);
        const int idx = (int)g_items.size();
        Item it{ r, E_REABRIR, 0, true, false }; g_items.push_back(it);
        if (g) {
            FillRound(g, r, 12.f, (g_hot == idx) ? C_ROWH : C_ROW);
            StrokeRound(g, r, 12.f, C_WARN, 1.f);
            Txt(g, T(L"Reabrir o app na resolução nova", L"Reopen the app at the new resolution"), fItem, C_TXT,
                RectF(r.X + 18, r.Y + 8, r.Width - 130, 22), 0, true);
            Txt(g, T(L"o alvo mudou de tamanho; o filtro continua na resolução de quando abriu",
                     L"the target changed size; the filter is still at the resolution it opened with"),
                fItemSub, C_BODY, RectF(r.X + 18, r.Y + 29, r.Width - 130, 18), 0, true);
            RectF bd(r.X + r.Width - 108, r.Y + 13, 90, 26);
            FillRound(g, bd, 8.f, C_WARN);
            Txt(g, T(L"reabrir", L"reopen"), fBadge, C_DARK, bd, 1);
        }
        cy += 52 + 12;
    }

    // -------- grupo de ofertas: item de menu, com a logo do canal. Presenca de marca,
    // nao propaganda - uma linha, no fim, do tamanho dos outros itens.
    {
        RectF r(L, cy, IW, 58);
        const int idx = (int)g_items.size();
        Item it{ r, E_OFERTAS, 0, true, false }; g_items.push_back(it);
        if (g) {
            FillRound(g, r, 12.f, (g_hot == idx) ? C_ROWH : C_ROW);
            // uma barrinha ciano no lugar da logo: marca sem carregar imagem nenhuma
            FillRound(g, RectF(r.X, r.Y + 14, 4.f, r.Height - 28), 2.f, C_ACC);
            const REAL tx = r.X + 18;
            Txt(g, T(L"Grupo de ofertas — PC do Fafá", L"Deals group — PC do Fafá"), fItem, C_ACC2,
                RectF(tx, r.Y + 10, r.Width - 36, 22), 0, true);
            Txt(g, T(L"o app é grátis; o grupo ajuda a comprar peças e importar mais barato",
                     L"the app is free; the group is how you pay it back"),
                fItemSub, C_BODY, RectF(tx, r.Y + 31, r.Width - 36, 18), 0, true);
        }
        cy += 58 + 12;
    }

    // -------- apoie o criador: so aparece com endereco no ofertas.ini (chave `canal`).
    // Sem a chave o item nem existe - um botao que abre um link inventado e pior do que
    // botao nenhum, e quem empacota o app pode nao ter canal para divulgar.
    if (!g_urlCanal.empty()) {
        RectF r(L, cy, IW, 58);
        const int idx = (int)g_items.size();
        Item it{ r, E_CANAL, 0, true, false }; g_items.push_back(it);
        if (g) {
            FillRound(g, r, 12.f, (g_hot == idx) ? C_ROWH : C_ROW);
            FillRound(g, RectF(r.X, r.Y + 14, 4.f, r.Height - 28), 2.f, C_WARN);
            const REAL tx = r.X + 18;
            Txt(g, T(L"Apoie o criador do programa", L"Support the app's creator"), fItem, C_TXT,
                RectF(tx, r.Y + 10, r.Width - 36, 22), 0, true);
            Txt(g, T(L"abre o canal no YouTube — é de graça e ajuda o app a continuar",
                     L"opens the YouTube channel — it is free and it keeps the app going"),
                fItemSub, C_BODY, RectF(tx, r.Y + 31, r.Width - 36, 18), 0, true);
        }
        cy += 58 + 12;
    }

    // -------- rodape com os atalhos que realmente foram registrados
    if (g) { Pen p(C_SEP, 1.f); g->DrawLine(&p, L, cy, R, cy); }
    cy += 14;
    {
        std::wstring l1 = g_hkPanel + TS(L"  abre e fecha este painel   ·   Esc ou clique fora fecha",
                                         L"  opens and closes this panel   ·   Esc or a click outside closes it");
        const std::wstring tudo = TS(L"  liga e desliga tudo   ·   ", L"  turns everything on and off   ·   ");
        std::wstring l2 = g_hud.overlay
            ? (TextoAtalho(AT_TUDO) + tudo + keyReShade + TS(L"  abre o ReShade", L"  opens ReShade"))
            : (TextoAtalho(AT_TUDO) + tudo + TS(L"F11 tela cheia   ·   Esc nao fecha o app",
                                                L"F11 fullscreen   ·   Esc does not close the app"));
        if (g) {
            Txt(g, l1.c_str(), fItemSub, C_DIM,   RectF(L, cy, IW, 18), 0, true);
            Txt(g, l2.c_str(), fItemSub, C_FAINT, RectF(L, cy + 19, IW, 18), 0, true);
        }
        cy += 40;
        // uma linha de estado do HDR, quando ha o que dizer
        if (!g_hud.hdrLine.empty() && (g_hud.hdrFound || g_hud.hdrActing)) {
            if (g) Txt(g, (L"HDR · " + g_hud.hdrLine).c_str(), fItemSub,
                       g_hud.hdrAviso.empty() ? C_DIM : C_WARN, RectF(L, cy, IW, 18), 0, true);
            cy += 22;
        }
    }

    cy += 8;
    return (int)(cy - top);
}

// so a moldura: a sombra (SH camadas) e o cartao. Separada do conteudo de proposito -
// ela depende SO do tamanho, entao da pra guardar pronta (ver DesenharFundo).
static void DesenharMoldura(Graphics& g, int cardH) {
    // sombra: varias camadas fracas, cada uma um pouco menor - blur de pobre, mas
    // com alpha por pixel de verdade o resultado e uma sombra macia mesmo.
    for (int i = SH; i >= 1; i--) {
        RectF r((REAL)(SH - i), (REAL)(SH - i + 5), (REAL)(CW + 2 * i), (REAL)(cardH + 2 * i));
        FillRound(&g, r, 16.f + i, Color((BYTE)(3 + (SH - i)), 0, 0, 0));
    }
    RectF card((REAL)SH, (REAL)SH, (REAL)CW, (REAL)cardH);
    FillRound(&g, card, 16.f, C_CARD);
    StrokeRound(&g, card, 16.f, C_EDGE, 1.f);
}

// sombra + cartao + conteudo, na ordem (caminho sem cache: --panel-shot)
static void DrawAll(Graphics& g, int cardH) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    DesenharMoldura(g, cardH);
    Render(&g);
}

// --------------------------------------------------------- repintura sem trabalho a toa
// O painel era redesenhado por inteiro uma vez por segundo, tivesse mudado alguma coisa
// ou nao: o tick do laco principal chama Refresh() sem perguntar. Cada repintura custava
// DOIS layouts completos (um so para medir a altura), SH=20 retangulos arredondados
// antialiasados de sombra sobre a area inteira, e algumas dezenas de DrawString do GDI+ -
// quase sempre para produzir exatamente os mesmos pixels de antes.
//
// Tres economias, da mais barata para a mais cara:
//   1. AssinaturaVisivel() resume TUDO que vira pixel. Igual a da ultima vez => o Paint
//      sai na hora, sem tocar em GDI nenhum.
//   2. AssinaturaAltura() resume o pouco que muda a ALTURA do cartao. Igual => reaproveita
//      o cardH ja medido e pula o layout de medicao (o Render(nullptr)).
//   3. A moldura (sombra + cartao) vai para um Bitmap e so e redesenhada quando o tamanho
//      muda; nas outras vezes e um blit.
//
// REGRA PARA QUEM MEXER NO Render(): qualquer coisa nova que ele leia PRECISA entrar na
// AssinaturaVisivel, senao o painel congela mostrando o valor velho e o defeito aparece
// como "o painel nao atualiza" - dificil de ligar a esta funcao. Na duvida, inclua.
static void SigMix(uint64_t& h, uint64_t v) { h ^= v; h *= 1099511628211ull; }
static void SigStr(uint64_t& h, const std::wstring& s) {
    for (wchar_t c : s) SigMix(h, (uint64_t)c);
    SigMix(h, s.size() * 131 + 7);
}

static uint64_t AssinaturaVisivel() {
    uint64_t h = 1469598103934665603ull;
    SigStr(h, g_hud.src);
    SigMix(h, ((uint64_t)g_hud.w << 32) | (uint64_t)g_hud.h);
    // os fps saem do painel com "%.0f": so o inteiro muda pixel. Sem este arredondamento
    // a assinatura mudaria a cada tick e a economia nao existiria. (Nao sao negativos, e
    // o unico desacordo possivel com o %.0f e o empate exato em ,5 - que nao muda nada:
    // o que importa e a assinatura mudar no MESMO ponto em que o texto muda.)
    SigMix(h, (uint64_t)(int64_t)(g_hud.fpsSrc + 0.5));
    SigMix(h, (uint64_t)(int64_t)(g_hud.fpsPresent + 0.5));
    // ...mas o "apertado" (que pinta o PRESENT de laranja) le os valores CRUS, entao o
    // que entra aqui e o booleano dele, nao os doubles.
    SigMix(h, (g_hud.fpsSrc >= 20.0 && g_hud.fpsPresent < g_hud.fpsSrc * 0.6 && g_hud.fpsCap == 0) ? 1u : 0u);
    SigMix(h, (uint64_t)g_hud.fpsCap);
    SigMix(h, (uint64_t)g_hud.neural);
    SigStr(h, g_hud.neuralDetalhe);
    SigStr(h, g_hud.hdrLine);
    SigStr(h, g_hud.hdrAviso);
    SigStr(h, g_hud.avisoAtalho);
    SigStr(h, g_hkPanel);
    SigStr(h, TeclaOverlayReShade());   // a pessoa pode trocar a tecla no ReShade
    uint64_t bits = 0; int b = 0;
    auto bit = [&](bool v) { if (v) bits |= (1ull << b); b++; };
    bit(g_hud.overlay); bit(g_hud.bypass); bit(g_hud.vsync); bit(g_hud.hdrTreat);
    bit(g_hud.hdrFound); bit(g_hud.hdrActing); bit(g_hud.camera); bit(g_hud.tudoDesligado);
    bit(g_hud.interativo); bit(g_hud.confirmandoSaida); bit(g_hud.neuralPendente);
    bit(g_hud.fonteMudou); bit(g_hud.fpsTela);
    SigMix(h, bits);
    SigMix(h, (uint64_t)(int64_t)g_hud.capturandoAtalho);
    SigMix(h, (uint64_t)(int64_t)g_hot);                    // o realce sob o mouse
    for (int i = 0; i < AT_COUNT; i++) {
        SigMix(h, ((uint64_t)g_atalhos[i].mods << 16) | (uint64_t)g_atalhos[i].vk);
        SigMix(h, g_atalhos[i].registrado ? 1u : 0u);
    }
    SigMix(h, (uint64_t)g_lingua);                          // trocar de idioma muda TODO texto
    SigMix(h, (uint64_t)(int64_t)(g_scale * 1000.f));       // DPI muda o bitmap inteiro
    // O Paint nao decide so o CONTEUDO: ele tambem posiciona a janela (PosicaoParaJanela)
    // e encolhe a escala quando o painel nao cabe na tela (AlturaDisponivel). Se essas
    // duas entradas ficassem de fora, arrastar o jogo para outro canto ou trocar a
    // resolucao com o painel aberto deixaria o painel parado no lugar velho ate que algum
    // outro dado mudasse. Sao duas chamadas baratas, e compram a correcao inteira.
    if (g_hud.render) {
        RECT rr{}; GetWindowRect(g_hud.render, &rr);
        SigMix(h, ((uint64_t)(uint32_t)rr.left << 32) ^ (uint32_t)rr.top);
        SigMix(h, ((uint64_t)(uint32_t)rr.right << 32) ^ (uint32_t)rr.bottom);
    }
    if (g_hud.wnd) {
        MONITORINFO mi{ sizeof(mi), {}, {}, 0 };
        if (GetMonitorInfoW(MonitorFromWindow(g_hud.wnd, MONITOR_DEFAULTTONEAREST), &mi))
            SigMix(h, ((uint64_t)(uint32_t)(mi.rcWork.right - mi.rcWork.left) << 32) ^
                       (uint32_t)(mi.rcWork.bottom - mi.rcWork.top));
    }
    return h;
}

// A altura do cartao e uma soma de literais em cy. So DUAS coisas a mudam: o card
// "Reabrir" (aparece com fonteMudou) e a linha de HDR do rodape. Se nenhuma mudou, o
// cardH da ultima medicao continua valendo ao pixel.
static uint64_t AssinaturaAltura() {
    uint64_t h = 1469598103934665603ull;
    SigMix(h, g_hud.fonteMudou ? 1u : 0u);
    SigMix(h, (!g_hud.hdrLine.empty() && (g_hud.hdrFound || g_hud.hdrActing)) ? 1u : 0u);
    return h;
}

static Bitmap* g_fundo = nullptr;      // moldura pronta, em pixels FISICOS
static int g_fundoW = 0, g_fundoH = 0;
static REAL g_fundoScale = 0.f;

// o que o ultimo Paint deixou na tela (a janela layered guarda o bitmap mesmo escondida)
static uint64_t g_sigPintada = 0;
static bool     g_jaPintou   = false;
static uint64_t g_sigAltura  = 0;
static int      g_cardHCache = 0;

// "esqueca o que esta na tela": a proxima repintura acontece de qualquer jeito. Chamado
// ao abrir o painel - reabrir e barato, e vale mais que economizar um desenho e correr o
// risco de mostrar o estado de quando ele fechou.
static void InvalidarPintura() { g_jaPintou = false; }

static void SoltarFundo() { delete g_fundo; g_fundo = nullptr; g_fundoW = g_fundoH = 0; }

// Poe a moldura no destino. Guardada em pixels fisicos e copiada com a transformacao
// IDENTIDADE e CompositingModeSourceCopy: assim e um blit 1:1, sem reamostrar (escalar um
// bitmap ja pronto borraria a borda da sombra) e sem blend com o que havia embaixo - o
// que dispensa tambem o Clear da superficie inteira.
static void DesenharFundo(Graphics& g, int W, int H, int cardH) {
    if (!g_fundo || g_fundoW != W || g_fundoH != H || g_fundoScale != g_scale) {
        SoltarFundo();
        g_fundo = new Bitmap(W, H, PixelFormat32bppPARGB);
        Graphics gc(g_fundo);
        gc.Clear(Color(0, 0, 0, 0));
        gc.SetSmoothingMode(SmoothingModeAntiAlias);
        gc.SetPixelOffsetMode(PixelOffsetModeHalf);
        gc.ScaleTransform(g_scale, g_scale);
        DesenharMoldura(gc, cardH);
        g_fundoW = W; g_fundoH = H; g_fundoScale = g_scale;
    }
    g.SetCompositingMode(CompositingModeSourceCopy);
    g.DrawImage(g_fundo, 0, 0, W, H);
    g.SetCompositingMode(CompositingModeSourceOver);
}

// relogio fino local: o QpcMs() do laco principal so aparece bem depois deste namespace
static double RelogioMs() {
    static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
}

// medicao do custo de repintura: so com DLSS5_DIAG=1, e resumida a cada 30 desenhos
// (uma linha por repintura enterraria o log, que e o unico diagnostico que o usuario tem)
static void ContaPaint(double ms, bool pulou) {
    if (!DiagLigado()) return;
    static double soma = 0.0, pior = 0.0;
    static int n = 0, pulos = 0;
    if (pulou) { pulos++; return; }
    soma += ms; n++;
    if (ms > pior) pior = ms;
    if (n >= 30) {
        Log("painel: %d repinturas, %.2f ms media, %.2f ms pior; %d pulos (nada mudou)",
            n, soma / n, pior, pulos);
        soma = 0.0; pior = 0.0; n = 0; pulos = 0;
    }
}

static void Paint() {
    if (!g_hud.wnd) return;
    const double t0 = RelogioMs();
    UpdateScale(g_hud.render ? g_hud.render : g_hud.wnd);
    // (o estado da ponte era relido aqui a cada desenho, só para os chips de LUT/nitidez;
    //  sem eles ninguém no painel depende do rsbridge::GetStatus)

    // 1. mudou alguma coisa que vira pixel? A UpdateScale vem antes de proposito: o DPI
    //    entra na assinatura, e trocar o painel de monitor tem de repintar.
    const uint64_t sigAgora = AssinaturaVisivel();
    if (g_jaPintou && sigAgora == g_sigPintada) { ContaPaint(0.0, true); return; }

    // 2. a altura pode ter mudado? Se nao, o cardH da ultima vez vale e o layout de
    //    medicao (que roda o Render inteiro so pra somar cy) nao precisa acontecer.
    //    O g_items nao se perde: quem o preenche de verdade e o Render(&g) la embaixo.
    const uint64_t sigAlt = AssinaturaAltura();
    if (!g_jaPintou || sigAlt != g_sigAltura || g_cardHCache <= 0) {
        g_cardHCache = Render(nullptr);
        g_sigAltura  = sigAlt;
    }
    const int cardH = g_cardHCache;

    // O painel cresceu; em tela 1080p sem escala ele encostaria nas bordas. Se nao couber
    // onde vai aparecer, encolhe ate caber (nunca abaixo de 70%, senao vira ilegivel).
    // Tudo comparado em pixels FISICOS - misturar com as unidades logicas do layout foi
    // exatamente o que ja jogou o painel meio metro pra fora da tela uma vez.
    int W = (int)((CW + SH * 2) * g_scale + 0.5f), H = (int)((cardH + SH * 2) * g_scale + 0.5f);
    {
        const REAL disp = (REAL)AlturaDisponivel(W, H) - 16.f;
        const REAL cabe = disp / (REAL)(cardH + SH * 2);
        if (cabe < g_scale) {
            g_scale = (cabe < 0.70f) ? 0.70f : cabe;
            W = (int)((CW + SH * 2) * g_scale + 0.5f);
            H = (int)((cardH + SH * 2) * g_scale + 0.5f);
        }
    }

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;   // top-down
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) { DeleteDC(mem); ReleaseDC(nullptr, screen); return; }
    HGDIOBJ oldb = SelectObject(mem, dib);
    {
        Bitmap bmp(W, H, W * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        Graphics g(&bmp);
        // a moldura entra pronta e em escala 1:1 (ela ja nasceu com o g_scale aplicado),
        // por isso o DesenharFundo vem ANTES do ScaleTransform. O SourceCopy dele tambem
        // dispensa o Clear que havia aqui.
        DesenharFundo(g, W, H, cardH);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.ScaleTransform(g_scale, g_scale);
        Render(&g);
    }

    // no monitor do alvo, e dentro do proprio alvo quando ele comporta o painel - a menos
    // que a pessoa tenha arrastado o cartao, e ai a escolha dela manda. O Paint roda a cada
    // segundo: sem esta condicao, o painel voltaria sozinho para o meio da tela um instante
    // depois de ser solto.
    POINT pos = PosicaoParaJanela(W, H);
    if (g_posValida) { pos = g_pos; ClamparNaTela(pos, W, H); g_pos = pos; }
    SIZE sz{ W, H }; POINT srcpt{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    BOOL ok = UpdateLayeredWindow(g_hud.wnd, screen, &pos, &sz, mem, &srcpt, 0, &bf, ULW_ALPHA);
    if (!ok) Log("painel: UpdateLayeredWindow falhou (erro %lu)", GetLastError());
    g_winW = W; g_winH = H;

    SelectObject(mem, oldb);
    DeleteObject(dib); DeleteDC(mem); ReleaseDC(nullptr, screen);

    // So aqui: se algum caminho acima tiver desistido no meio, a assinatura NAO pode ser
    // dada por pintada - senao a proxima volta pularia por achar que a tela ja esta certa.
    g_sigPintada = sigAgora;
    g_jaPintou   = true;
    ContaPaint(RelogioMs() - t0, false);
}

// --------------------------------------------------------------- --panel-shot
// Grava o painel num PNG, com o MESMO codigo de desenho da tela. Serve pra conferir
// o visual sem precisar fotografar o monitor - e a janela de verdade fica de fora das
// capturas de tela de proposito (SetWindowDisplayAffinity), entao um print comum sairia
// sem ela. O fundo cinza-azulado so simula uma cena atras, pra mostrar a translucidez.
static int EncoderClsid(const WCHAR* mime, CLSID* out) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (!size) return -1;
    ImageCodecInfo* info = (ImageCodecInfo*)malloc(size);
    if (!info) return -1;
    GetImageEncoders(num, size, info);
    int found = -1;
    for (UINT i = 0; i < num; i++)
        if (wcscmp(info[i].MimeType, mime) == 0) { *out = info[i].Clsid; found = (int)i; break; }
    free(info);
    return found;
}
static bool SaveShot(const wchar_t* path) {
    EnsureFonts();
    g_scale = 1.f;                 // o PNG sai sempre em 96 dpi, do tamanho de projeto
    int cardH = Render(nullptr);
    // hover no botao do ReShade, pra mostrar como fica o realce
    for (size_t i = 0; i < g_items.size(); i++) if (g_items[i].kind == E_RESHADE) g_hot = (int)i;
    const int W = CW + SH * 2, H = cardH + SH * 2;
    Bitmap bmp(W, H, PixelFormat32bppARGB);
    Graphics g(&bmp);
    {
        LinearGradientBrush bg(Point(0, 0), Point(W, H), Color(255, 58, 64, 78), Color(255, 26, 28, 36));
        g.FillRectangle(&bg, 0, 0, W, H);
    }
    DrawAll(g, cardH);
    CLSID png{};
    if (EncoderClsid(L"image/png", &png) < 0) return false;
    return bmp.Save(path, &png, nullptr) == Ok;
}

// p vem em pixels de tela (cliente); o layout vive em unidades logicas
static int HitTest(POINT p) {
    const REAL x = p.x / g_scale, y = p.y / g_scale;
    for (size_t i = 0; i < g_items.size(); i++) {
        const RectF& r = g_items[i].r;
        if (x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height) return (int)i;
    }
    return -1;
}
static void Close();
static void Activate(const Item& it) {
    switch (it.kind) {
    // CMD_TUDO, e nao TRAY_BYPASS: o card "DLSS 5" liga e desliga a coisa inteira -
    // a camada E o neural -, que e a mesma acao do F6. TRAY_BYPASS continua existindo
    // para quem so mexe na camada por dentro (aquecimento, deteccao de tela parada).
    case E_FILTER:  g_trayCmd = CMD_TUDO; break;
    // O MESMO COMANDO DA TECLA, de proposito. Este botao tinha caminho proprio
    // (CMD_RESHADE): abria o overlay e prendia o mouse, mas nao sabia fechar - so a volta
    // automatica ou outro atalho devolviam. Dois caminhos para a mesma acao, com finais
    // diferentes, e o tipo de coisa que faz a pessoa achar que o app e imprevisivel.
    case E_RESHADE: g_trayCmd = TRAY_INTERACT; break;
    case E_FPS:     g_trayCmd = CMD_FPS_BASE + it.val; break;
    case E_WORKRES: g_trayCmd = CMD_WORKRES_BASE + it.val; break;
    case E_IDIOMA:  g_trayCmd = CMD_IDIOMA_BASE + it.val; break;
    case E_VSYNC:   g_trayCmd = CMD_VSYNC; break;
    case E_HDR:     g_trayCmd = CMD_HDR; break;
    case E_FPSTELA: g_trayCmd = CMD_FPSTELA; break;
    case E_OFERTAS: g_trayCmd = CMD_OFERTAS; break;
    case E_CANAL:   g_trayCmd = CMD_CANAL; break;
    case E_GUIA:    g_trayCmd = CMD_GUIA; break;
    case E_INTERATIVO: g_trayCmd = TRAY_INTERACT; break;
    case E_SAIR:    g_trayCmd = CMD_SAIR_PAINEL; break;
    case E_ATALHO:  g_trayCmd = CMD_ATALHO_BASE + it.val; break;
    case E_ATALHO_PADRAO: g_trayCmd = CMD_ATALHO_PADRAO; break;
    case E_REABRIR: g_trayCmd = CMD_REABRIR; break;
    default: break;
    }
}

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_NCHITTEST: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &p);
        // a margem da sombra e transparente: o clique la e "clique fora"
        const int m = (int)(SH * g_scale);
        if (p.x < m || p.y < m || p.x >= g_winW - m || p.y >= g_winH - m) return HTTRANSPARENT;
        return HTCLIENT;
    }
        // Fora dos controles o cursor vira a cruz de mover: e o unico aviso de que o cartao
        // pode ser arrastado - ele nao tem barra de titulo para sugerir isso.
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, g_hot >= 0 ? IDC_HAND : IDC_SIZEALL)); return TRUE; }
        break;
    case WM_LBUTTONDOWN: {
        // Pegar o cartao pelo VAZIO arrasta; pegar por um controle continua sendo clique.
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        const int hit = HitTest(p);
        if (hit >= 0 && g_items[hit].enabled) return 0;   // e um chip: o LBUTTONUP resolve
        RECT wr{}; GetWindowRect(h, &wr);
        POINT cur{}; GetCursorPos(&cur);
        g_arrastando = true;
        g_arrastoOff.x = cur.x - wr.left;
        g_arrastoOff.y = cur.y - wr.top;
        SetCapture(h);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (g_arrastando) {
            // GetCursorPos, e nao o lParam: a janela anda junto com o cursor, entao as
            // coordenadas de cliente mudam de referencia a cada mensagem e o cartao fugiria.
            POINT cur{}; GetCursorPos(&cur);
            g_pos.x = cur.x - g_arrastoOff.x;
            g_pos.y = cur.y - g_arrastoOff.y;
            g_posValida = true;
            SetWindowPos(h, nullptr, g_pos.x, g_pos.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hit = HitTest(p);
        if (hit >= 0 && !g_items[hit].enabled) hit = -1;
        if (hit != g_hot) { g_hot = hit; Paint(); }
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hot != -1) { g_hot = -1; Paint(); }
        return 0;
    case WM_CAPTURECHANGED:   // alguem tomou o mouse no meio do arrasto
        g_arrastando = false;
        return 0;
    case WM_LBUTTONUP: {
        if (g_arrastando) {
            g_arrastando = false;
            ReleaseCapture();
            ClamparNaTela(g_pos, g_winW, g_winH);
            SetWindowPos(h, nullptr, g_pos.x, g_pos.y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            IniSet(L"painel_x", std::to_wstring(g_pos.x));
            IniSet(L"painel_y", std::to_wstring(g_pos.y));
            Log("painel: arrastado para %ld,%ld (gravado no ini)", g_pos.x, g_pos.y);
            return 0;   // soltar depois de arrastar NAO e clique
        }
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hit = HitTest(p);
        Log("painel: clique em %ld,%ld -> item %d%s", p.x, p.y, hit,
            (hit >= 0 && !g_items[hit].enabled) ? " (desativado)" : "");
        if (hit >= 0 && g_items[hit].enabled) Activate(g_items[hit]);
        return 0;
    }
    case WM_KEYDOWN:
        // Em modo de captura o painel engole o teclado: a proxima tecla vira o atalho.
        // Modificador sozinho nao conta - a pessoa ainda esta montando a combinacao.
        if (g_hud.capturandoAtalho >= 0) {
            if (wp == VK_ESCAPE) {
                CancelarCapturaAtalho();
                Paint();
                return 0;
            }
            if (wp == VK_CONTROL || wp == VK_MENU || wp == VK_SHIFT ||
                wp == VK_LCONTROL || wp == VK_RCONTROL || wp == VK_LMENU ||
                wp == VK_RMENU || wp == VK_LSHIFT || wp == VK_RSHIFT) return 0;
            UINT mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetKeyState(VK_MENU)    & 0x8000) mods |= MOD_ALT;
            if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= MOD_SHIFT;
            g_trayCmd = CMD_ATALHO_SET_BASE + (int)(mods & 7) * 256 + (int)(wp & 0xFF);
            return 0;
        }
        if (wp == VK_ESCAPE) { Close(); return 0; }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) Close();   // clicou fora / o jogo voltou
        return 0;
    case WM_CLOSE:
        Close();
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void FecharGuia();   // definido junto do namespace Guia

static void Open() {
    // O cartaz de boas-vindas fica 30 s na tela; se a pessoa abriu o painel, ela ja foi
    // para a acao e o cartaz vira estorvo - alem de ficar POR CIMA, porque os dois sao
    // topmost e ele nasceu depois.
    FecharGuia();
    if (!g_hud.wnd) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = Proc; wc.hInstance = g_hud.inst;
        wc.lpszClassName = L"DLSS5ForAllPanel"; wc.hCursor = nullptr;
        RegisterClassExW(&wc);
        g_hud.wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                                    wc.lpszClassName, L"", WS_POPUP,
                                    0, 0, 10, 10, nullptr, nullptr, g_hud.inst, nullptr);
        if (!g_hud.wnd) { Log("painel: CreateWindow falhou"); return; }
        // o painel fica de fora de gravacoes e prints - senao entraria no proprio filtro.
        // DLSS5_PANEL_NA_CAPTURA=1 desliga isso (util pra gravar um tutorial do painel).
        wchar_t env[8]{};
        if (GetEnvironmentVariableW(L"DLSS5_PANEL_NA_CAPTURA", env, 8) && env[0] == L'1')
            Log("painel: aparece nas capturas (DLSS5_PANEL_NA_CAPTURA=1)");
        else
            HideFromCapture(g_hud.wnd);
    }
    // A posicao arrastada numa sessao anterior, lida uma vez so. Sem as duas chaves o
    // painel continua se posicionando sozinho, como sempre fez.
    static bool leuPosIni = false;
    if (!leuPosIni) {
        leuPosIni = true;
        const std::wstring sx = IniGet(L"painel_x"), sy = IniGet(L"painel_y");
        if (!sx.empty() && !sy.empty()) {
            g_pos.x = (LONG)_wtoi(sx.c_str());
            g_pos.y = (LONG)_wtoi(sy.c_str());
            g_posValida = true;
            Log("painel: posicao gravada %ld,%ld", g_pos.x, g_pos.y);
        }
    }
    g_hot = -1;
    HWND fg = GetForegroundWindow();
    if (fg != g_hud.wnd) g_hud.prevFg = fg;
    g_hud.open = true;              // antes do Show: o WM_ACTIVATE chega na hora
    InvalidarPintura();             // abrir sempre desenha, custe o desenho que custar
    Paint();
    ShowWindow(g_hud.wnd, SW_SHOWNA);
    SetForegroundWindow(g_hud.wnd);
    SetActiveWindow(g_hud.wnd);
    SetFocus(g_hud.wnd);
    RECT wr{}; GetWindowRect(g_hud.wnd, &wr);
    Log("painel aberto em %ld,%ld  %ldx%ld  visivel=%d  itens=%u  escala=%.2f",
        wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top,
        (int)IsWindowVisible(g_hud.wnd), (unsigned)g_items.size(), (double)g_scale);
    // DLSS5_LOG_ITENS=1 despeja as areas clicaveis em coordenadas de tela. Serve pra
    // testar o painel de fora (ele nao sai em print nenhum, por causa do EXCLUDEFROMCAPTURE).
    wchar_t env[8]{};
    if (GetEnvironmentVariableW(L"DLSS5_LOG_ITENS", env, 8) && env[0] == L'1') {
        for (size_t i = 0; i < g_items.size(); i++) {
            const Item& it = g_items[i];
            Log("  item[%u] kind=%d val=%d ativo=%d  centro-na-tela=%ld,%ld", (unsigned)i,
                it.kind, it.val, (int)it.enabled,
                wr.left + (LONG)((it.r.X + it.r.Width / 2) * g_scale),
                wr.top  + (LONG)((it.r.Y + it.r.Height / 2) * g_scale));
        }
    }
}

static void Close() {
    if (!g_hud.open || g_closing) return;
    g_closing = true;
    // O painel pode estar em "aperte a tecla..." quando o usuario clica fora ou aperta
    // Esc: fechar sem retomar deixaria o app sem nenhuma hotkey registrada.
    CancelarCapturaAtalho();
    g_hud.open = false; g_hot = -1;
    if (g_hud.wnd) ShowWindow(g_hud.wnd, SW_HIDE);
    if (g_hud.prevFg && IsWindow(g_hud.prevFg)) SetForegroundWindow(g_hud.prevFg);
    g_closing = false;
    Log("painel fechado");
}
static void Toggle() { if (g_hud.open) Close(); else Open(); }
static void Refresh() { if (g_hud.open) Paint(); }

// o jogo (e a nossa propria overlay) reafirmam topmost o tempo todo; o painel
// precisa reafirmar tambem, senao some por tras da camada do filtro.
// sobe o painel na hora (chamado logo depois que a camada do filtro reafirma o topo)
static void RaiseNow() {
    if (g_hud.open && g_hud.wnd) SetWindowPos(g_hud.wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void Tick() {
    if (!g_hud.open || !g_hud.wnd) return;
    static ULONGLONG last = 0;
    ULONGLONG now = GetTickCount64();
    if (now - last < 250) return;
    last = now;
    SetWindowPos(g_hud.wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void Shutdown() {
    if (g_hud.wnd) { DestroyWindow(g_hud.wnd); g_hud.wnd = nullptr; }
    delete fTitle; delete fVer; delete fSub; delete fLbl; delete fItem;
    delete fItemSub; delete fChip; delete fBadge; delete fStat; delete fStatLbl;
    fTitle = nullptr;
    SoltarFundo();
}
} // namespace Hud

// =====================================================================================
//  GUIA DE ATALHOS
//
//  O cartaz de aviso pisca por alguns segundos e some - serve pra confirmar uma acao,
//  nao pra ensinar. Quem abre o app pela primeira vez precisa de outra coisa: uma tela
//  que fica parada, escrita grande, dizendo o que cada tecla faz em portugues de gente.
//  E isto. Aparece na abertura, some sozinha em 30 s, tem "nao mostrar de novo" (que
//  fica gravado no ini) e pode ser reaberta pelo painel ou pela bandeja.
//
//  Mesmo desenho do painel (janela layered + GDI+), com duas diferencas de proposito:
//  a fonte e bem maior, porque isto e pra ser lido de longe com o jogo atras; e ela
//  reafirma o topo com mais frequencia, porque nasce junto com a camada do filtro,
//  que tambem esta brigando pelo topmost nos primeiros segundos.
// =====================================================================================
namespace Guia {
using namespace Gdiplus;
using Hud::FillRound;
using Hud::StrokeRound;
using Hud::Txt;
using Hud::TxtSpaced;
using Hud::C_CARD; using Hud::C_EDGE; using Hud::C_TXT; using Hud::C_BODY;
using Hud::C_DIM;  using Hud::C_FAINT; using Hud::C_ACC; using Hud::C_ACC2;
using Hud::C_ROW;  using Hud::C_ROWH; using Hud::C_OFF; using Hud::C_DARK;
using Hud::C_SEP;  using Hud::C_WARN; using Hud::C_ON;

static const int SH  = 20;    // margem da sombra
static const int CW  = 690;   // mais largo que o painel: aqui o texto e grande
static const int PAD = 30;

enum GKind { G_NADA = 0, G_ENTENDI, G_NUNCA_MAIS };
struct GItem { RectF r; int kind; };
static std::vector<GItem> g_it;
static int  g_hot = -1;
static HWND g_wnd = nullptr;
static bool g_open = false, g_closing = false;
static ULONGLONG g_ate = 0;          // fecha sozinha nesta hora (0 = so no clique)
static REAL g_scale = 1.f;
static int  g_winW = 0, g_winH = 0;

static Font *fTit = nullptr, *fSub = nullptr, *fTecla = nullptr, *fNome = nullptr,
            *fDesc = nullptr, *fBotao = nullptr, *fPe = nullptr;

static void EnsureFonts() {
    if (fTit) return;
    fTit   = Hud::MkFont(L"Segoe UI Semibold", 26.f, FontStyleBold);
    fSub   = Hud::MkFont(L"Segoe UI",          16.f, FontStyleRegular);
    fTecla = Hud::MkFont(L"Segoe UI Semibold", 17.f, FontStyleRegular);
    fNome  = Hud::MkFont(L"Segoe UI Semibold", 19.f, FontStyleRegular);
    fDesc  = Hud::MkFont(L"Segoe UI",          15.f, FontStyleRegular);
    fBotao = Hud::MkFont(L"Segoe UI Semibold", 16.f, FontStyleRegular);
    fPe    = Hud::MkFont(L"Segoe UI",          14.f, FontStyleRegular);
}

// uma linha do guia: a tecla num badge, o que ela faz, e por que voce ia querer isso
struct Linha { std::wstring tecla, nome, desc; bool destaque; };

// Duas teclas e o resto no painel. Tudo lido do mapa configuravel - nunca texto fixo.
//
// E nunca uma tecla que o Windows RECUSOU. Ensinar "F6 liga e desliga tudo" quando o
// RegisterHotKey falhou (outra instancia, um gravador de tela residente) e pior do que nao
// ensinar nada: a pessoa aperta, nao acontece nada, e conclui que o app esta quebrado.
// TemAtalho() ja existia para isso e so nao estava sendo usado aqui.
static const wchar_t* SemTecla() {
    return T(L"outro programa está com essa tecla — mude no painel, em ATALHOS",
             L"another program holds this key — change it in the panel, under SHORTCUTS");
}

static std::vector<Linha> Lista() {
    std::vector<Linha> v;
    const std::wstring semTecla = TS(L"sem tecla", L"no key");
    v.push_back({ TemAtalho(AT_PAINEL) ? TextoAtalho(AT_PAINEL) : semTecla,
                  T(L"Abrir o painel", L"Open the panel"),
                  TemAtalho(AT_PAINEL) ? T(L"é por aqui que se faz tudo: filtro, neural, fps, HDR",
                                           L"this is where everything happens: filter, neural, fps, HDR")
                                       : SemTecla(), true });
    v.push_back({ TemAtalho(AT_TUDO) ? TextoAtalho(AT_TUDO) : semTecla,
                  T(L"Habilitar e desabilitar o DLSS 5", L"Enable and disable DLSS 5"),
                  TemAtalho(AT_TUDO) ? T(L"o antes e depois: desliga o filtro inteiro e devolve o jogo cru",
                                         L"the before and after: drops the whole filter, raw game back")
                                     : SemTecla(), true });
    // A TECLA AQUI E A DO APP, NAO A DO RESHADE. O guia mostrava a tecla do overlay
    // (Home), que ainda abre - mas nao fecha, e nao entrega o mouse. Quem faz o ciclo
    // inteiro e o atalho do interativo, e prometer o outro deixava a pessoa com o overlay
    // aberto sem saber como sair dele.
    v.push_back({ TemAtalho(AT_INTERATIVO) ? TextoAtalho(AT_INTERATIVO) : TeclaOverlayReShade(),
                  T(L"Abrir e fechar o ReShade", L"Open and close ReShade"),
                  TemAtalho(AT_INTERATIVO)
                      ? T(L"o mouse vai pra ele; aperte de novo pra fechar e devolver",
                          L"the mouse goes to it; press again to close and give it back")
                      : T(L"presets e shaders — para mexer a fundo na cadeia de efeitos",
                          L"presets and shaders — for digging into the effect chain"), false });
    if (TemAtalho(AT_SAIR))
        v.push_back({ TextoAtalho(AT_SAIR), T(L"Sair", L"Quit"),
                      T(L"aperte duas vezes seguidas para confirmar", L"press it twice in a row to confirm"), false });
    else
        v.push_back({ semTecla, T(L"Sair", L"Quit"),
                      T(L"pelo botão \"Sair\" no painel, ou pelo ícone na bandeja",
                        L"use the \"Quit\" button in the panel, or the tray icon"), false });
    return v;
}

static int Render(Graphics* g) {
    EnsureFonts();
    g_it.clear();
    const REAL L = (REAL)(SH + PAD), IW = (REAL)(CW - PAD * 2), R = L + IW;
    const REAL top = (REAL)SH;
    REAL cy = top + 26;

    if (g) {
        TxtSpaced(g, T(L"COMO USAR", L"HOW TO USE"), fTit, C_TXT, L, cy, 34.f, 2.0f);
        Txt(g, APP_TITLE, fPe, C_FAINT, RectF(R - 190, cy + 10, 150, 20), 2);
    }
    {   // X de fechar, no canto - tem de haver um jeito obvio de sair antes dos 30 s
        RectF x(R - 30, cy - 2, 32, 32);
        const int ix = (int)g_it.size(); g_it.push_back({ x, G_ENTENDI });
        if (g) {
            if (g_hot == ix) FillRound(g, x, 8.f, C_ROWH);
            Pen p(g_hot == ix ? C_TXT : C_DIM, 2.f);
            g->DrawLine(&p, x.X + 10, x.Y + 10, x.X + 22, x.Y + 22);
            g->DrawLine(&p, x.X + 22, x.Y + 10, x.X + 10, x.Y + 22);
        }
    }
    cy += 38;
    if (g) Txt(g, T(L"guarde estas teclas — elas funcionam com o jogo em primeiro plano",
                    L"keep these keys in mind — they work with the game in the foreground"),
               fSub, C_DIM, RectF(L, cy, IW, 22), 0, true);
    cy += 30;
    if (g) { Pen p(C_SEP, 1.f); g->DrawLine(&p, L, cy, R, cy); }
    cy += 16;

    for (const Linha& a : Lista()) {
        const REAL hh = 68;
        RectF r(L, cy, IW, hh);
        if (g) {
            FillRound(g, r, 12.f, C_ROW);
            if (a.destaque) FillRound(g, RectF(r.X, r.Y + 12, 4.f, r.Height - 24), 2.f, C_ACC);
            // badge da tecla
            RectF bd(r.X + 18, r.Y + 17, 132, 34);
            FillRound(g, bd, 9.f, a.destaque ? C_ACC : C_OFF);
            Txt(g, a.tecla.c_str(), fTecla, a.destaque ? C_DARK : C_TXT, bd, 1, true);
            Txt(g, a.nome.c_str(), fNome, C_TXT, RectF(r.X + 166, r.Y + 12, r.Width - 184, 24), 0, true);
            Txt(g, a.desc.c_str(), fDesc, C_BODY, RectF(r.X + 166, r.Y + 37, r.Width - 184, 20), 0, true);
        }
        cy += hh + 10;
    }

    cy += 6;
    // o estado do neural no cartaz: e o motivo do app existir, tem de estar visivel
    {
        const wchar_t* e = g_hud.neural == NR_ATIVO     ? T(L"Neural Rendering: ATIVO",
                                                            L"Neural Rendering: ACTIVE")
                         : g_hud.neural == NR_INICIANDO ? T(L"Neural Rendering: ligado, iniciando",
                                                            L"Neural Rendering: on, starting up")
                         : g_hud.neural == NR_SEM_RTX   ? T(L"Neural Rendering: INDISPONÍVEL nesta placa",
                                                            L"Neural Rendering: UNAVAILABLE on this GPU")
                         : g_hud.neural == NR_SEM_ADDON ? T(L"Neural Rendering: NÃO INSTALADO",
                                                            L"Neural Rendering: NOT INSTALLED")
                                                        : T(L"Neural Rendering: DESLIGADO — ligue no painel",
                                                            L"Neural Rendering: OFF — turn it on in the panel");
        const Color c = g_hud.neural == NR_ATIVO ? C_ON
                      : g_hud.neural == NR_INICIANDO ? C_ACC2 : C_WARN;
        if (g) Txt(g, e, fDesc, c, RectF(L, cy, IW, 20), 0, true);
        cy += 26;
    }
    // A bandeja passou a existir nos dois modos (ver MakeTrayWindow no wWinMain), mas o
    // texto continua condicional porque no modo janela separada a barra de titulo e a
    // referencia mais util - e onde estao a fonte, o tamanho e os fps.
    if (g) Txt(g, g_hud.overlay
                  ? T(L"o ícone na bandeja, ao lado do relógio, tem os mesmos comandos",
                      L"the tray icon, next to the clock, has the same commands")
                  : T(L"a barra de título mostra a fonte, o tamanho e os fps · F11 põe em tela cheia",
                      L"the title bar shows the source, the size and the fps · F11 goes fullscreen"),
               fPe, C_FAINT, RectF(L, cy, IW, 20), 0, true);
    cy += 26;
    // O texto de HDR vem do proprio modulo: hoje a conversao e automatica, entao a linha
    // costuma ser informativa ("convertendo em 16 bits") e so vira aviso laranja quando
    // sobrou algo pra pessoa fazer.
    if (g_hud.hdrFound && !g_hud.hdrLine.empty()) {
        const bool alerta = !g_hud.hdrAviso.empty();
        if (g) Txt(g, (L"HDR · " + g_hud.hdrLine).c_str(), fPe, alerta ? C_WARN : C_DIM,
                   RectF(L, cy, IW, 20), 0, true);
        cy += 26;
    }
    cy += 6;

    // botoes
    {
        const REAL bw = 176, bh = 44, gap = 12;
        RectF b1(R - bw, cy, bw, bh);
        RectF b2(R - bw - gap - 232, cy, 232, bh);
        int i1 = (int)g_it.size(); g_it.push_back({ b1, G_ENTENDI });
        int i2 = (int)g_it.size(); g_it.push_back({ b2, G_NUNCA_MAIS });
        if (g) {
            FillRound(g, b1, 10.f, (g_hot == i1) ? C_ACC2 : C_ACC);
            Txt(g, T(L"Entendi", L"Got it"), fBotao, C_DARK, b1, 1);
            FillRound(g, b2, 10.f, (g_hot == i2) ? C_ROWH : C_ROW);
            StrokeRound(g, b2, 10.f, C_EDGE, 1.f);
            Txt(g, T(L"Não mostrar de novo", L"Don't show this again"), fBotao, C_BODY, b2, 1);
        }
        cy += bh + 10;
    }
    cy += 12;
    return (int)(cy - top);
}

static void Paint() {
    if (!g_wnd) return;
    EnsureFonts();
    Hud::UpdateScale(g_hud.render ? g_hud.render : g_wnd);
    g_scale = Hud::g_scale;
    const int cardH = Render(nullptr);
    int W = (int)((CW + SH * 2) * g_scale + 0.5f), H = (int)((cardH + SH * 2) * g_scale + 0.5f);
    {   // encolhe se nao couber onde vai aparecer (em pixels fisicos, como no painel)
        const REAL disp = (REAL)AlturaDisponivel(W, H) - 16.f;
        const REAL cabe = disp / (REAL)(cardH + SH * 2);
        if (cabe < g_scale) {
            g_scale = (cabe < 0.70f) ? 0.70f : cabe;
            W = (int)((CW + SH * 2) * g_scale + 0.5f);
            H = (int)((cardH + SH * 2) * g_scale + 0.5f);
        }
    }

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) { DeleteDC(mem); ReleaseDC(nullptr, screen); return; }
    HGDIOBJ oldb = SelectObject(mem, dib);
    {
        Bitmap bmp(W, H, W * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        Graphics g(&bmp);
        g.Clear(Color(0, 0, 0, 0));
        g.ScaleTransform(g_scale, g_scale);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        for (int i = SH; i >= 1; i--) {
            RectF r((REAL)(SH - i), (REAL)(SH - i + 5), (REAL)(CW + 2 * i), (REAL)(cardH + 2 * i));
            FillRound(&g, r, 16.f + i, Color((BYTE)(3 + (SH - i)), 0, 0, 0));
        }
        RectF card((REAL)SH, (REAL)SH, (REAL)CW, (REAL)cardH);
        FillRound(&g, card, 16.f, C_CARD);
        StrokeRound(&g, card, 16.f, C_ACC, 1.f);   // borda ciano: e um convite, nao um alerta
        Render(&g);
    }
    POINT pos = PosicaoParaJanela(W, H);
    SIZE sz{ W, H }; POINT src{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_wnd, screen, &pos, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    g_winW = W; g_winH = H;
    SelectObject(mem, oldb);
    DeleteObject(dib); DeleteDC(mem); ReleaseDC(nullptr, screen);
}

// mesmo truque do painel: ele nao sai em print (EXCLUDEFROMCAPTURE), entao o PNG e
// gerado pelo proprio codigo de desenho
static bool SaveShot(const wchar_t* path) {
    EnsureFonts();
    g_scale = 1.f;
    int cardH = Render(nullptr);
    const int W = CW + SH * 2, H = cardH + SH * 2;
    Bitmap bmp(W, H, PixelFormat32bppARGB);
    Graphics g(&bmp);
    {
        LinearGradientBrush bg(Point(0, 0), Point(W, H), Color(255, 58, 64, 78), Color(255, 26, 28, 36));
        g.FillRectangle(&bg, 0, 0, W, H);
    }
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    for (int i = SH; i >= 1; i--) {
        RectF r((REAL)(SH - i), (REAL)(SH - i + 5), (REAL)(CW + 2 * i), (REAL)(cardH + 2 * i));
        FillRound(&g, r, 16.f + i, Color((BYTE)(3 + (SH - i)), 0, 0, 0));
    }
    RectF card((REAL)SH, (REAL)SH, (REAL)CW, (REAL)cardH);
    FillRound(&g, card, 16.f, C_CARD);
    StrokeRound(&g, card, 16.f, C_ACC, 1.f);
    Render(&g);
    CLSID png{};
    if (Hud::EncoderClsid(L"image/png", &png) < 0) return false;
    return bmp.Save(path, &png, nullptr) == Ok;
}

static int HitTest(POINT p) {
    const REAL x = p.x / g_scale, y = p.y / g_scale;
    for (size_t i = 0; i < g_it.size(); i++) {
        const RectF& r = g_it[i].r;
        if (x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height) return (int)i;
    }
    return -1;
}
static void Close();

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_NCHITTEST: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &p);
        const int mg = (int)(SH * g_scale);
        if (p.x < mg || p.y < mg || p.x >= g_winW - mg || p.y >= g_winH - mg) return HTTRANSPARENT;
        return HTCLIENT;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, g_hot >= 0 ? IDC_HAND : IDC_ARROW)); return TRUE; }
        break;
    case WM_MOUSEMOVE: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hit = HitTest(p);
        if (hit != g_hot) { g_hot = hit; Paint(); }
        // mexeu o mouse = a pessoa esta lendo: para de contar o tempo
        g_ate = 0;
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_hot != -1) { g_hot = -1; Paint(); }
        return 0;
    case WM_LBUTTONUP: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hit = HitTest(p);
        if (hit >= 0) {
            if (g_it[hit].kind == G_NUNCA_MAIS) {
                IniSet(L"guia_visto", L"1");
                Log("guia: 'nao mostrar de novo' marcado");
            }
            Close();
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_RETURN || wp == VK_SPACE) { Close(); return 0; }
        break;
    case WM_CLOSE: Close(); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// segundos = 0 fica ate a pessoa fechar
static void Open(int segundos) {
    if (!g_wnd) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = Proc; wc.hInstance = g_hud.inst;
        wc.lpszClassName = L"DLSS5ForAllGuia"; wc.hCursor = nullptr;
        RegisterClassExW(&wc);
        g_wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                                wc.lpszClassName, L"", WS_POPUP,
                                0, 0, 10, 10, nullptr, nullptr, g_hud.inst, nullptr);
        if (!g_wnd) { Log("guia: CreateWindow falhou"); return; }
        HideFromCapture(g_wnd);   // e instrucao pra quem usa, nao conteudo pra gravar
    }
    g_hot = -1;
    g_open = true;
    Paint();
    ShowWindow(g_wnd, SW_SHOWNA);
    SetWindowPos(g_wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    g_ate = segundos > 0 ? GetTickCount64() + (ULONGLONG)segundos * 1000 : 0;
    RECT wr{}; GetWindowRect(g_wnd, &wr);
    Log("guia aberto em %ld,%ld %ldx%ld (%d s)", wr.left, wr.top,
        wr.right - wr.left, wr.bottom - wr.top, segundos);
}

static void Close() {
    if (!g_open || g_closing) return;
    g_closing = true;
    g_open = false; g_hot = -1; g_ate = 0;
    if (g_wnd) ShowWindow(g_wnd, SW_HIDE);
    g_closing = false;
    Log("guia fechado");
}

// O guia nasce junto com a camada do filtro, que reafirma topmost o tempo todo nos
// primeiros segundos: aqui a reafirmacao e mais frequente que a do painel.
static void Tick() {
    if (!g_open || !g_wnd) return;
    if (g_ate && GetTickCount64() > g_ate) { Close(); return; }
    static ULONGLONG last = 0;
    ULONGLONG now = GetTickCount64();
    if (now - last < 120) return;
    last = now;
    SetWindowPos(g_wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
static void RaiseNow() {
    if (g_open && g_wnd) SetWindowPos(g_wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void Shutdown() {
    if (g_wnd) { DestroyWindow(g_wnd); g_wnd = nullptr; }
    delete fTit; delete fSub; delete fTecla; delete fNome; delete fDesc; delete fBotao; delete fPe;
    fTit = nullptr;
}
} // namespace Guia

// o painel chama isto ao abrir (declarado la em cima, no namespace Hud)
namespace Hud { static void FecharGuia() { Guia::Close(); } }

// ------------------------------------------------------------------ teclas pro ReShade
static void HandleHotkeyMsg(WPARAM k);

// Bombeia a fila por alguns milissegundos sem sair do lugar. Existe por causa do
// SendInput: a tecla so vira WM_KEYDOWN quando ALGUEM despacha a mensagem, e o
// ReShade le o teclado justamente pela wndproc da janela onde ele esta injetado.
static void PumpMessages(DWORD ms) {
    ULONGLONG end = GetTickCount64() + ms;
    MSG m;
    for (;;) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) { g_quit = true; return; }
            if (m.message == WM_HOTKEY) HandleHotkeyMsg(m.wParam);
            TranslateMessage(&m); DispatchMessageW(&m);
        }
        if (GetTickCount64() >= end) break;
        Sleep(4);
    }
}

// Manda uma tecla pro ReShade, que esta injetado na NOSSA janela (dxgi.dll).
// Ele so ve teclado quando a janela esta em foco - e no modo sobreposto ela e
// click-through + NOACTIVATE de proposito. Entao a sequencia e: fecha o painel (que
// esta com o foco), sai do click-through, ativa a janela, sintetiza a tecla, deixa as
// mensagens serem despachadas e so entao decide se volta pro estado de antes.
//   stayInteractive = true  -> fica interativo (End: sem isso ninguem mexe no overlay)
//   stayInteractive = false -> volta pro click-through (F6: e um aperta-e-pronto)
//
// O "via" existe porque do outro lado ha DOIS leitores diferentes de teclado, e isto foi
// medido, nao chutado:
//   VIA_MENSAGEM - o ReShade le a fila de mensagens da janela. Com o overlay dele aberto
//     ele engole o teclado do aplicativo e a tecla sintetizada se perde: so com SendInput
//     o End abria o overlay e nunca conseguia fechar. A mensagem postada chega nos dois
//     estados.
//   VIA_TECLADO - o add-on RenoDX (F6) olha o estado real do teclado, que so o SendInput
//     mexe: so com PostMessage o F6 nunca alternava o Neural Rendering.
// Mandar pelos dois caminhos ao mesmo tempo NAO resolve: as duas transicoes podem cair em
// Presents diferentes e o ReShade alterna duas vezes, voltando ao estado de origem.
enum TeclaVia { VIA_MENSAGEM, VIA_TECLADO };
// As nossas hotkeys globais interceptariam a propria tecla que vamos sintetizar (o F6 do
// RenoDX e tambem o nosso atalho de "liga/desliga tudo"): sem suspender, o SendInput
// dispararia WM_HOTKEY e a acao chamaria a si mesma.

static bool PokeReShade(UINT vk, bool stayInteractive, TeclaVia via) {
    HWND h = g_hud.render;
    if (!h) return false;
    Hud::Close();
    SuspenderAtalhos();
    const bool wasCT = g_clickThrough;
    if (wasCT) SetClickThrough(h, false);
    // TRAZER O FOCO DE VERDADE, e nao so pedir.
    //
    // SetForegroundWindow e recusado quando o processo que chama nao "merece" o foreground
    // pelas regras do Windows. Quando o F6 vem de uma hotkey global de verdade, a gente
    // acabou de receber entrada e o pedido passa. Quando vem de qualquer outro caminho -
    // clique no painel, menu da bandeja, ou um teste que enfileira WM_HOTKEY -, ele e
    // recusado em silencio, e ai o guard logo abaixo abortaria o poke por um motivo que
    // nao e o que ele existe para pegar.
    //
    // AttachThreadInput com a thread que esta em primeiro plano empresta a fila de entrada
    // dela por um instante e o pedido passa a valer. E a tecnica padrao para isto; o
    // desanexar no fim e obrigatorio, senao as duas filas ficam presas uma na outra.
    for (int tentativa = 0; tentativa < 2 && GetForegroundWindow() != h; tentativa++) {
        if (tentativa == 0) {
            SetForegroundWindow(h);
        } else {
            const HWND fg = GetForegroundWindow();
            const DWORD tidFg = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
            const DWORD tidEu = GetCurrentThreadId();
            if (tidFg && tidFg != tidEu && AttachThreadInput(tidEu, tidFg, TRUE)) {
                SetForegroundWindow(h);
                BringWindowToTop(h);
                AttachThreadInput(tidEu, tidFg, FALSE);
            }
        }
        SetActiveWindow(h);
        SetFocus(h);
        PumpMessages(70);
    }
    PumpMessages(70);

    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    DWORD ext = (vk == VK_END || vk == VK_HOME || vk == VK_INSERT || vk == VK_DELETE ||
                 vk == VK_PRIOR || vk == VK_NEXT || (vk >= VK_LEFT && vk <= VK_DOWN))
                ? KEYEVENTF_EXTENDEDKEY : 0;
    INPUT in[2]{};
    in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = (WORD)vk; in[0].ki.wScan = (WORD)sc; in[0].ki.dwFlags = ext;
    in[1] = in[0]; in[1].ki.dwFlags = ext | KEYEVENTF_KEYUP;
    // O pulo do gato: o ReShade decide "essa tecla foi apertada agora" olhando o estado
    // do teclado UMA VEZ por Present. Se o down e o up couberem entre dois Presents
    // (e cabem: aqui o alvo parado apresenta a cada 100 ms) ele nunca ve tecla apertada.
    // Entao a gente solta a tecla so DEPOIS de apresentar um par de frames com ela em pe.
    const LPARAM lpDown = 1 | ((LPARAM)sc << 16) | (ext ? (LPARAM)1 << 24 : 0);
    const LPARAM lpUp   = lpDown | ((LPARAM)1 << 30) | ((LPARAM)1 << 31);
    // SendInput e teclado GLOBAL: ele cai em QUEM ESTIVER EM FOCO. Se o
    // SetForegroundWindow acima nao pegou (o Windows recusa quando outro processo tem a
    // entrada, e e comum com jogo em tela cheia), o F6 vai para dentro do jogo - onde F6
    // costuma ser quicksave, console ou trocar de arma. Abortar e a unica resposta certa;
    // o PostMessage nao tem esse risco, porque vai para a nossa janela por handle.
    // DLSS5_TESTA_POKE=abortar (com DLSS5_DIAG=1) finge que o foco nao voltou. E o unico
    // jeito de exercitar este caminho sem um jogo em tela cheia recusando o foreground.
    static const bool fingeAbortar = []{
        if (!DiagLigado()) return false;
        wchar_t e[16]{};
        return GetEnvironmentVariableW(L"DLSS5_TESTA_POKE", e, 16) && wcscmp(e, L"abortar") == 0;
    }();
    if (via == VIA_TECLADO && (fingeAbortar || GetForegroundWindow() != h)) {
        Log("PokeReShade abortado: o foco ficou em %p, nao na nossa janela (%p) - "
            "SendInput cairia dentro do jogo", (void*)GetForegroundWindow(), (void*)h);
        // SEMPRE devolve o click-through aqui, mesmo com stayInteractive: ficar interativo
        // so se justifica quando a tecla foi entregue e ha um overlay para mexer. Sem
        // entrega, o unico efeito de ficar era prender o mouse na camada sem nada na tela.
        if (wasCT) SetClickThrough(h, true);
        RetomarAtalhos();
        ShowToast(g_hud.inst, nullptr,
            T(L"NÃO CONSEGUI FALAR COM O RESHADE\n"
              L"o jogo não devolveu o foco. Clique na janela do filtro e tente de novo.",
              L"COULD NOT TALK TO RESHADE\n"
              L"the game would not give focus back. Click the filter window and try again."), 5000);
        return false;
    }
    UINT sent = 0;
    if (via == VIA_MENSAGEM) sent = PostMessageW(h, WM_KEYDOWN, vk, lpDown) ? 1 : 0;
    else                     sent = SendInput(1, &in[0], sizeof(INPUT));
    // o ReShade (e os add-ons) olham o teclado uma vez por Present: a tecla precisa ficar
    // em pe por alguns frames apresentados, senao o aperto passa despercebido
    for (int i = 0; i < 6; i++) { PumpMessages(45); PresentNow(1); }
    if (via == VIA_MENSAGEM) sent += PostMessageW(h, WM_KEYUP, vk, lpUp) ? 1 : 0;
    else                     sent += SendInput(1, &in[1], sizeof(INPUT));
    PumpMessages(80);
    PresentNow(2);

    // Mesma regra do aborto: sem os dois eventos entregues nao ha overlay nenhum para
    // mexer, entao nao ha por que segurar o mouse. E quando FICA interativo, arma a volta
    // automatica na mesma linha em que o click-through sai - o unico jeito de garantir que
    // toda saida do click-through tenha prazo.
    if (wasCT && (!stayInteractive || sent < 2)) SetClickThrough(h, true);
    else if (g_hud.overlay && stayInteractive && sent >= 2) ArmarVoltaInterativo();
    RetomarAtalhos();
    Log("PokeReShade vk=0x%02X enviados=%u/2 (era click-through=%d, agora=%d, foco=%p janela=%p)",
        vk, sent, (int)wasCT, (int)g_clickThrough, (void*)GetForegroundWindow(), (void*)h);
    // O outro caminho de falha: o foco veio, mas o SendInput/PostMessage nao entregou os
    // dois eventos (fila de entrada bloqueada, sessao trancando). Quase inalcancavel, mas
    // sem cartaz o F6 desfazia tudo em silencio - e "nao aconteceu nada e ninguem disse por
    // que" e o pior desfecho de qualquer botao.
    if (sent < 2)
        ShowToast(g_hud.inst, nullptr,
            T(L"O RESHADE NÃO RECEBEU A TECLA\n"
              L"nada foi alterado. Tente de novo em um instante.",
              L"RESHADE DID NOT GET THE KEY\n"
              L"nothing was changed. Try again in a moment."), 4000);
    return sent >= 2;
}

// O ultimo "NR toggled ON/OFF" do log do add-on: -1 nao sei, 0 desligado, 1 ligado.
// E a fonte IMEDIATA (o ini demora, o AutoSave do ReShade grava quando quer), e por isso
// e ela que confere se o neural ficou como pedimos.
// Verdadeiro quando o add-on SOLTOU o Neural Rendering depois do ultimo toque - e isso
// nao e teoria, e a linha que aparece no ReShade.log quando o jogo mexe nos graficos:
//
//   [DLSS 5 Neural Rendering] DLSS5 Generic: tracked D3D12 device is being destroyed;
//   releasing NR state for re-arm
//
// O device D3D12 que o add-on rastreava morre, ele solta o estado do NR, e a partir dali
// o efeito NAO esta mais sendo aplicado - por mais que o ultimo "NR toggled ON" do log
// continue dizendo que sim. Era exatamente esse o buraco: a pessoa mexia nos graficos do
// jogo, o filtro parava de valer, e o app seguia anunciando que estava tudo ligado. So
// fechar e reabrir resolvia.
static bool g_neuralSolto = false;

static int NeuralPeloLog() {
    const std::string t = TailArquivo(g_exeDir + L"ReShade.log", 96 * 1024);
    const size_t on   = t.rfind("NR toggled ON");
    size_t       off  = t.rfind("NR toggled OFF");
    const size_t solto = t.rfind("releasing NR state");
    // O "solto" conta como um OFF, e mais recente que ele quando vier depois: o que
    // interessa e o ULTIMO evento, nao o ultimo toque.
    g_neuralSolto = (solto != std::string::npos) &&
                    (on == std::string::npos || solto > on) &&
                    (off == std::string::npos || solto > off);
    if (g_neuralSolto) off = solto;
    if (on == std::string::npos && off == std::string::npos) return -1;
    if (on  == std::string::npos) return 0;
    if (off == std::string::npos) return 1;
    return on > off ? 1 : 0;
}

// F6: o antes-e-depois. Desliga guardando o que estava; liga devolvendo igual.
// bypass e da camada (so existe no modo sobreposto); o resto vai pela ponte.
// --------------------------------------------------------------- alternar o neural
// A PONTE PRIMEIRO, o teclado como reserva.
//
// O caminho antigo (SetForegroundWindow + SendInput do F6) nunca funcionou nesta maquina, e
// o agente da ponte descobriu por que: enquanto a GameInputServiceWindow - de integridade
// mais alta - tem o foreground, o UIPI DESCARTA o SendInput sem sinalizar erro (ele ainda
// devolve 1). Nao era falta de foco: era o Windows jogando a tecla fora em silencio. Foi o
// que eu vinha medindo como "NR toggled = zero no ReShade.log" sem saber a causa.
//
// A ponte nao tem esse problema porque nao ha tecla nenhuma no meio: o add-on serve o
// GetAsyncKeyState direto para o RenoDX, dentro do processo. Ver docs/neural-sem-teclado.md.
//
// O contrato para o AlternarTudo (§14.10) e o mesmo de antes: false quando NENHUM dos dois
// caminhos funcionou. So ai o cartaz aparece e a troca inteira e desfeita.
static bool AlternarNeural() {
    if (rsbridge::ToggleNeural()) {
        // "armado", nao "alternado": quem entrega e o proximo frame do RenoDX. A conferencia
        // de 2,5 s pelo ReShade.log continua valendo, e e ela que fecha o ciclo.
        Log("neural: toque armado pela ponte (sem foco, sem SendInput)");
        return true;
    }
    Log("neural: a ponte nao pode alternar agora - caindo no caminho do teclado");
    return PokeReShade(VK_F6, false, VIA_TECLADO);
}

static std::wstring AlternarTudo(bool* bypass, bool overlay) {
    // "LIGADO" E A COISA INTEIRA DE PE. Esta funcao decidia pelo g_tudoDesligado, uma flag
    // NOSSA - e por isso o app chegou a anunciar "HABILITADO" com o Neural Rendering
    // desligado: o ReShade.ini tinha NeuralUplift=0 de uma sessao anterior, a camada
    // estava na frente, e ninguem conferiu a outra metade. Quem apertava via o cartaz
    // dizer que habilitou e a imagem nao mudar nada.
    //
    // Agora a decisao vem do ESTADO REAL das duas partes. Falta uma? Entao nao esta
    // ligado, e o proximo toque LIGA - que e exatamente o que "ou e tudo junto ou nao e"
    // quer dizer.
    const bool temAddon = (g_hud.neural != NR_SEM_ADDON && g_hud.neural != NR_SEM_RTX);
    const bool neuralOn = (g_hud.neural == NR_ATIVO || g_hud.neural == NR_INICIANDO);
    const bool camadaOn = overlay ? !(*bypass) : true;
    const bool tudoOn   = camadaOn && (neuralOn || !temAddon);

    if (tudoOn) {
        g_antes = EstadoFiltro{};
        g_antes.guardado = true;
        g_antes.neural = neuralOn;
        g_antes.camada = camadaOn;
        // A CAMADA SAI DA FRENTE PRIMEIRO. Ela e a unica parte que a pessoa VE, e antes
        // era a ultima da fila: saia depois do toque no neural, que no caminho de reserva
        // (PokeReShade por teclado) custa uns 700 ms. O resultado era "apertei e demorou".
        if (overlay) *bypass = true;
        if (neuralOn) {
            g_antes.neuralNos = AlternarNeural();   // ponte primeiro, teclado como reserva
            if (!g_antes.neuralNos) {
                // O poke abortou: desfaz o que ja aparecia na tela e nao marca nada. Sem
                // isso o app ficaria dizendo que desligou o neural que continua ligado.
                if (overlay) *bypass = !camadaOn;
                g_antes = EstadoFiltro{};
                Log("DLSS 5: o toque para desligar o neural abortou - desfeito, nada mudou");
                return std::wstring();
            }
        }
        g_tudoDesligado = true;
        Log("DLSS 5 DESABILITADO (estava: neural=%d camada=%d)", (int)neuralOn, (int)camadaOn);
        return T(L"DLSS 5 DESABILITADO  -  imagem original", L"DLSS 5 DISABLED  -  original image");
    }

    // LIGAR: poe a camada de volta e GARANTE o neural, em vez de confiar numa marca de
    // que fomos nos que o desligamos. O ini pode ter vindo com NeuralUplift=0 de outra
    // sessao, e nesse caso nunca houve marca nenhuma para restaurar.
    if (overlay) *bypass = false;
    if (temAddon && !neuralOn) {
        if (!AlternarNeural()) {
            if (overlay) *bypass = !camadaOn;
            Log("DLSS 5: o toque para ligar o neural abortou - continua desligado");
            return std::wstring();
        }
    }
    g_antes.neuralNos = false;
    g_tudoDesligado = false;
    Log("DLSS 5 HABILITADO (neural estava %s)", neuralOn ? "ligado" : "desligado");
    return T(L"DLSS 5 HABILITADO", L"DLSS 5 ENABLED");
}
// Quando a tecla CHEGOU, para o laco poder dizer quanto tempo ela esperou na fila. O
// atalho e processado no topo do laco; se o ciclo estiver dentro do DrawAndPresent, ela
// espera ele terminar. Sem este carimbo, "o F6 demora" e palpite - com ele o proprio log
// da sessao da pessoa mostra o numero.
static std::atomic<ULONGLONG> g_hotkeyTick{ 0 };

// Um lugar so pra traduzir WM_HOTKEY em comando - o loop principal e o PumpMessages
// usam o mesmo, senao uma tecla apertada durante uma espera do PumpMessages sumiria.
static void HandleHotkeyMsg(WPARAM k) {
    g_hotkeyTick = GetTickCount64();
    // mapa novo
    if (k >= HK_ACAO_BASE && k < HK_ACAO_BASE + AT_COUNT) {
        switch ((AcaoAtalho)(k - HK_ACAO_BASE)) {
        case AT_PAINEL:     g_trayCmd = CMD_PANEL_TOGGLE; break;
        case AT_TUDO:       g_trayCmd = CMD_TUDO; break;
        case AT_FILTRO:     g_trayCmd = TRAY_BYPASS; break;
        case AT_INTERATIVO: g_trayCmd = TRAY_INTERACT; break;
        case AT_SAIR:       g_trayCmd = CMD_SAIR; break;
        default: break;
        }
        return;
    }
    // reservas antigas, mantidas registradas mas fora de todo texto
    if      (k == HK_QUIT     || k == HK_QUIT2)     g_trayCmd = CMD_SAIR;
    else if (k == HK_INTERACT || k == HK_INTERACT2) g_trayCmd = TRAY_INTERACT;
    else if (k == HK_BYPASS   || k == HK_BYPASS2)   g_trayCmd = TRAY_BYPASS;
    else if (k == HK_PANEL    || k == HK_PANEL2)    g_trayCmd = CMD_PANEL_TOGGLE;
}

// A CAMADA NUNCA PODE FICAR COM O TECLADO.
//
// Ela e transparente ao MOUSE (HTTRANSPARENT), mas foco e outra coisa: se o Windows a
// coloca em primeiro plano - e ele faz isso sozinho, por exemplo quando o seletor, que era
// do nosso processo, e destruido e o sistema procura outra janela nossa para ativar -,
// TODA tecla passa a ser entregue a ela. E ela ignora tudo. Do lado de quem esta usando o
// PC: "o teclado morreu", sem aviso e sem saida pelo mouse, porque o clique atravessa a
// camada e nao devolve o foco para ninguem.
static void DevolverForegroundSeForNossaCamada(HWND camada) {
    if (!g_clickThrough) return;          // no modo interativo o foco e nosso de proposito
    if (GetForegroundWindow() != camada) return;
    const DWORD nos = GetCurrentProcessId();
    for (HWND w = GetWindow(camada, GW_HWNDNEXT); w; w = GetWindow(w, GW_HWNDNEXT)) {
        if (!IsWindowVisible(w) || IsIconic(w)) continue;
        DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
        if (pid == nos) continue;         // nossas proprias janelas nao resolvem nada
        if (GetWindowLongPtrW(w, GWL_EXSTYLE) & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT)) continue;
        if (GetWindowTextLengthW(w) <= 0) continue;
        SetForegroundWindow(w);
        static int logs = 0;
        if (logs < 3) { logs++; Log("foco: a camada estava com o teclado - devolvido para a janela de baixo"); }
        return;
    }
}

// segue o alvo: posicao, tamanho, some quando o jogo nao esta em primeiro plano
// keepAlive: quando a camada precisa sair da vista (jogo fora de foco, bypass) ela e
// jogada pra fora da tela em vez de ser escondida. Assim o OBS nao perde a fonte -
// uma janela oculta some da lista de Captura de Janela, uma janela fora da tela nao.
static void FollowTarget(HWND h, ScreenCapture& scap, bool& visible, bool bypass, bool keepAlive) {
    RECT want{}, org{};
    // TargetRectCached, nao TargetRect: o segundo faz DwmGetWindowAttribute, que e uma
    // chamada ao processo do DWM, e isto roda a cada volta do laco - o caminho critico.
    // O cache foi criado exatamente para isso e a captura ja o usa.
    bool show = !bypass && scap.Alive() && scap.TargetRectCached(&want, &org);
    if (show && scap.targetWin) {
        // sobre uma janela: so aparece quando o jogo esta em primeiro plano
        if (IsIconic(scap.targetWin)) show = false;
        else {
            HWND fg = GetForegroundWindow();
            // O PID do alvo e o nosso nao mudam enquanto a janela for a mesma, e isto roda
            // a cada volta do laco: consultar so quando o alvo troca. A checagem de foco em
            // si continua por volta, de proposito - atrasa-la faria a camada demorar a sair
            // da frente no Alt+Tab, que e justamente o que ela nao pode fazer.
            static HWND  alvoAnterior = nullptr;
            static DWORD pidT = 0;
            static const DWORD pidNos = GetCurrentProcessId();
            if (scap.targetWin != alvoAnterior) {
                alvoAnterior = scap.targetWin; pidT = 0;
                GetWindowThreadProcessId(scap.targetWin, &pidT);
            }
            DWORD pidF = 0;
            if (fg) GetWindowThreadProcessId(fg, &pidF);
            // QUALQUER janela do NOSSO PROCESSO conta como "nossa": painel, cartaz-guia,
            // toast e a tela do grupo. Listar uma a uma ja falhou - a tela do grupo faz
            // SetForegroundWindow e nao estava na lista, entao a camada ia para -32000
            // enquanto ela estivesse aberta; desde que ela deixou de fechar sozinha, isso
            // seria por tempo indeterminado.
            show = (fg == scap.targetWin || fg == h ||
                    (pidF && (pidF == pidT || pidF == pidNos)));
        }
    }
    // sobre um monitor: fica sempre por cima - o filtro vale pra tela inteira
    if (!show) {
        if (visible) {
            if (keepAlive) {
                RECT cur{}; GetWindowRect(h, &cur);
                SetWindowPos(h, HWND_TOPMOST, -32000, -32000, cur.right - cur.left, cur.bottom - cur.top,
                             SWP_NOACTIVATE | SWP_NOSENDCHANGING);
            } else {
                ShowWindow(h, SW_HIDE);
            }
            visible = false;
            Log("overlay fora de vista (alvo fora de foco / minimizado / bypass)");
        }
        return;
    }
    RECT cur{}; GetWindowRect(h, &cur);
    if (cur.left != want.left || cur.top != want.top || cur.right != want.right || cur.bottom != want.bottom)
        SetWindowPos(h, HWND_TOPMOST, want.left, want.top, want.right - want.left, want.bottom - want.top,
                     SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    if (!visible) {
        ShowWindow(h, SW_SHOWNOACTIVATE); visible = true;
        Log("overlay colada em %ld,%ld %ldx%ld", (long)want.left, (long)want.top,
            (long)(want.right - want.left), (long)(want.bottom - want.top));
    }
    // jogo borderless costuma ser topmost tambem: reafirma o topo de tempos em tempos
    static ULONGLONG lastTop = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastTop >= 1000) {
        lastTop = now;
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Hud::RaiseNow();   // o painel tem que continuar acima da camada do filtro
        Guia::RaiseNow();  // o guia idem - ele nasce junto com a camada
    }
}
// relogio de alta resolucao (GetTickCount64 tem passo de ~15 ms, grosso demais pro teto de fps)
static double QpcMs() {
    static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
}

// ------------------------------------------------- motion vectors por hardware (VME)
//
// Estima motion vectors no BLOCO DE VIDEO da GPU (D3D12 Video Motion Estimation) e
// entrega o resultado a cadeia ReShade pelo add-on ponte, que o publica na semantica
// DLSS5_EXTMV. O DLSS5_MV_Hardware.fx la e escreve em Kernel::tFlow, que e o que o
// DLSS5_Feed.fx ja consome com DLSS5_MV_PROVIDER=3.
//
// POR QUE ISTO EXISTE: o app captura a imagem final do jogo; nao ha motion vector do
// jogo para pegar. Sem MV o DLAA nao reprojeta o historico e tudo que se move borra.
// O caminho antigo (Lumenite_Kernel) estima em pixel shader e custa a fila 3D, que a
// 3440x1440 ja esta saturada; o bloco de video nao disputa com o render.
//
// TUDO AQUI E OPCIONAL. Se o VME nao existir, se o driver recusar, se o add-on for
// antigo ou se o usuario desligar em [captura] mv_hardware=0, o app volta sozinho ao
// Lumenite_Kernel e nao muda mais nada. O usuario nunca fica sem imagem por isto.
namespace MvHw {

    dlss5::MotionEstimator g_est;
    bool     g_want    = true;    // [captura] mv_hardware
    bool     g_active  = false;   // o estimador subiu e esta entregando
    bool     g_tried   = false;   // ja tentamos nesta resolucao (nao insistir todo frame)
    UINT     g_w = 0, g_h = 0;    // resolucao com que o estimador foi criado
    unsigned g_frames  = 0;
    double   g_cpuAcc  = 0.0;     // soma do custo de CPU do Update, para a media
    unsigned g_cpuFrom = 0;       // quadro em que a janela de media do CPU comecou

    // O export e NOVO no add-on: um .addon64 antigo nao o tem. Resolvemos por
    // GetProcAddress e toleramos a ausencia -- nesse caso simplesmente nao ha MV de
    // hardware, e nada mais no app muda.
    typedef int (*SetExternalMVFn)(void *);
    SetExternalMVFn g_setMv = nullptr;
    bool g_setMvTried = false;

    inline SetExternalMVFn Bridge() {
        if (!g_setMvTried) {
            // O add-on e carregado pelo ReShade DEPOIS de nos: enquanto o modulo nao
            // existe, nem tentamos e nem desistimos. So paramos de procurar quando ele
            // ja esta no processo e mesmo assim nao tem o export (add-on antigo).
            if (HMODULE mod = GetModuleHandleW(DLSS5_BRIDGE_MODULE_NAMEW)) {
                g_setMv = reinterpret_cast<SetExternalMVFn>(
                    reinterpret_cast<void *>(GetProcAddress(mod, "DLSS5Bridge_SetExternalMV")));
                g_setMvTried = true;
                if (!g_setMv)
                    Log("mv-hw: o add-on da ponte nao tem DLSS5Bridge_SetExternalMV "
                        "(dlss5-bridge.addon64 antigo) - sem MV de hardware");
            }
        }
        return g_setMv;
    }

    // Sobe (ou re-sobe) o estimador para a resolucao atual da fonte. Idempotente.
    inline void Ensure(UINT w, UINT h, ID3D11Device *dev) {
        static bool lido = false;
        if (!lido) {
            lido = true;
            // PADRAO 0 -- o Lumenite_Kernel e quem roda de fabrica.
            //
            // Foi decisao medida, e contra o meu proprio caminho. Com o alvo de
            // deslocamento conhecido, a 45 px por quadro o Kernel le 44,668 px (-0,7%)
            // e o VME le 42,2 px (-6%) na melhor configuracao dele; com o bloco de 8
            // que era o padrao anterior, 31,9 px (-29%). O Kernel trabalha em resolucao
            // plena com piramide multi-escala e nao tem a ambiguidade de casamento que
            // aparece no VME entre ~15 e ~70 px. Como a prioridade declarada do usuario
            // e melhoria grafica acima de latencia, precisao ganha de custo.
            //
            // O VME continua inteiro e disponivel em mv_hardware=1: ele custa ~1 ms na
            // fila de VIDEO, que nao disputa com o render, contra 15 passes de tela
            // cheia do Kernel na fila 3D. Quem estiver com a GPU no limite troca
            // precisao por quadros nessa chave.
            g_want = GetPrivateProfileIntW(L"captura", L"mv_hardware", 0, IniPath().c_str()) != 0;
            if (!g_want)
                Log("mv-hw: modo preciso (padrao) - motion vectors pelo Lumenite_Kernel. "
                    "Para o modo leve (~1 ms na fila de video, menos preciso entre 20 e 60 px "
                    "por quadro), ponha mv_hardware=1 em [captura] no dlss5forall.ini");
            else
                Log("mv-hw: modo leve pedido em [captura] mv_hardware=1 - estimativa no bloco "
                    "de video (~1 ms fora da fila 3D, menos preciso entre 20 e 60 px por quadro)");
        }
        if (!g_want || !dev || !w || !h) return;
        if (g_active && w == g_w && h == g_h) return;
        if (g_tried && w == g_w && h == g_h) return;   // ja falhou nesta resolucao

        // ORDEM IMPORTA. O add-on guarda o ponteiro do SRV e o republica em todo
        // begin_effects; o Shutdown abaixo destroi esse SRV. Numa troca de resolucao
        // (o jogo muda de modo, o FonteMudou dispara) isso deixaria a ponte publicando
        // memoria morta ate o proximo Frame bem-sucedido -- e, se o Init novo falhar,
        // para sempre, porque o Frame sai cedo quando nao esta ativo. Zerar a ponte
        // primeiro custa uma chamada e fecha a janela inteira.
        if (SetExternalMVFn fn = Bridge()) fn(nullptr);
        g_est.Shutdown();
        g_active = false;
        g_w = w; g_h = h; g_tried = true;

        // Divisor 2 + bloco 16. O bloco 16 substituiu o 8 por medicao: com o alvo de
        // deslocamento conhecido, o bloco maior corta o erro do campo pela metade na
        // faixa onde o VME e fraco, e ELIMINA o pior sintoma dele -- ler movimento
        // MAIOR que o real. A 15 px por quadro o bloco 8 lia 16,6 px (+11%); o 16 le
        // 15,3 px (+2%). A 45 px, 40,4 px (-10%) contra 42,2 px (-6%).
        //
        // O preco e densidade de grade (1/32 do buffer em vez de 1/16), e ela e barata
        // aqui: a medicao de erro na borda de movimento mostrou que dobrar a densidade
        // nao reduz o erro onde ele nasce (0,790 contra 0,817 px -- empate).
        const UINT div = (UINT)GetPrivateProfileIntW(L"captura", L"mv_divisor", 2, IniPath().c_str());
        const UINT blk = (UINT)GetPrivateProfileIntW(L"captura", L"mv_bloco",  16, IniPath().c_str());
        if (g_est.Init(dev, w, h, div ? div : 2, blk)) {
            g_active = true;
            Log("mv-hw: VME ok, grade %ux%u, divisor %u, bloco %u (fonte %ux%u)",
                g_est.MvWidth(), g_est.MvHeight(), div ? div : 2, g_est.BlockSize(), w, h);
        } else {
            Log("mv-hw: indisponivel (%s) -> seguindo com o Lumenite_Kernel",
                g_est.LastError() ? g_est.LastError() : "motivo desconhecido");
        }
    }

    // Uma vez por quadro, logo depois do frame capturado chegar. Cedo de proposito:
    // o Update faz a fila 3D esperar a fila de video (na GPU, nao na CPU), e todo o
    // trabalho que o app faz entre aqui e o Present esconde essa espera.
    inline void Frame(ID3D11DeviceContext *ctx, ID3D11ShaderResourceView *frameSrv) {
        if (!g_active || !ctx || !frameSrv) return;

        const double t0 = QpcMs();
        const bool ok = g_est.Update(ctx, frameSrv);
        g_cpuAcc += QpcMs() - t0;
        ++g_frames;

        if (ok) {
            if (SetExternalMVFn fn = Bridge()) fn(g_est.MotionSRV());
        }

        // Prova no log real. A sonda AMOSTRA a cada 30 quadros e so reporta quando
        // junta 20 amostras -- ou seja, uma linha a cada 600 quadros, como antes, mas
        // agregando 20 medicoes em vez de fotografar uma.
        //
        // A diferenca nao e cosmetica. A sonda do dlss5-feed mede um quadro so, e no
        // run 13:26 isso produziu "0,000 px, 0% nao-zero" a 40 px de deslocamento --
        // enquanto a reproducao offline do mesmo cenario da 40,000 px com 100% dos
        // blocos. O log do mesmo run explica: "ritmo do WGC: min 0.01 ... max 747.06
        // ms". Ha entrega duplicada e buraco de quase um segundo, e um quadro repetido
        // produz campo exatamente zero. Um instantaneo que caia num repetido mente
        // sobre todo o resto da sessao.
        //
        // Por isso a coluna 'campo zerado': ela conta as amostras que vieram com o
        // campo inteiro nulo. Com conteudo em movimento, e a taxa de repeticao do WGC
        // -- e diz de imediato se vale confiar na media da linha.
        if ((g_frames % 30) == 0) {
            dlss5::MotionEstimator::ProbeStats st;
            if (g_est.ProbeSample(ctx, 20, &st)) {
                const unsigned janela = (g_frames > g_cpuFrom) ? (g_frames - g_cpuFrom) : 1u;
                const double cpu = g_cpuAcc / janela;
                g_cpuAcc = 0.0; g_cpuFrom = g_frames;
                // 'descartado' e a fracao de blocos que o estimador jogou fora por
                // deslocamento acima do alcance do hardware. Se subir de forma
                // consistente, o conteudo se move mais rapido do que este divisor
                // alcanca, e a saida e [captura] mv_divisor=4.
                Log("mv-hw: quadro %u | Update %.3f ms de CPU | %u amostras: |mv| media %.3f px, "
                    "mediana %.3f px, max %.2f px | %.0f%% nao-zero, %.0f%% descartado, "
                    "%.0f%% campo zerado",
                    g_frames, cpu, st.samples, st.meanPx, st.medianPx, st.maxPx,
                    st.pctNonZero, st.pctRejected, st.pctFieldZero);
            }
        }
    }

    // Antes de soltar o device. O add-on guarda o ponteiro do SRV: precisa ser avisado
    // ANTES de o SRV morrer, senao ele publica um ponteiro solto no proximo frame.
    inline void Stop() {
        if (SetExternalMVFn fn = Bridge()) fn(nullptr);
        g_est.Shutdown();
        g_active = false; g_tried = false; g_w = g_h = 0;
        g_frames = 0; g_cpuAcc = 0.0; g_cpuFrom = 0;
    }
}



static bool IStrHas(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return true;
    std::wstring a = hay, b = needle;
    std::transform(a.begin(), a.end(), a.begin(), ::towlower);
    std::transform(b.begin(), b.end(), b.begin(), ::towlower);
    return a.find(b) != std::wstring::npos;
}

// ------------------------------------------------------------------ main
// ==================================================================== INSTANCIA UNICA
// DUAS COPIAS DO APP SOBRE A MESMA TELA E O PIOR ESTADO POSSIVEL, e aconteceu de verdade.
// Na sessao das 15:27/15:43 o usuario ficou com duas camadas topmost empilhadas no mesmo
// monitor, duas cadeias NGX disputando a GPU e - o pior - sem tecla para fechar a segunda:
// a primeira ja tinha End/F6/Alt+Esc, a segunda caiu na cascata de reserva e o log dela
// registrou "atalho sair = (nenhum livre)" e "Modo sobreposto: (nenhum livre) = sair". Uma
// janela cobrindo a tela inteira, sem atalho de saida e com a barra de tarefas atras dela,
// e uma armadilha - e e a origem provavel do "nada clicavel" relatado depois.
//
// Local\ e nao Global\: o escopo e a sessao de logon. Dois usuarios na mesma maquina (troca
// rapida de usuario) tem cada um a sua tela e o seu app; um bloquear o outro seria errado.
static HANDLE g_mutexInstancia = nullptr;
#define DLSS5_MUTEX_INSTANCIA L"Local\\DLSS5ForAll.instancia"

// Os modos que NAO abrem camada nenhuma passam direto. Nao e conveniencia para os scripts
// de medicao: e que o perigo aqui e especificamente a janela sobreposta em cima da tela do
// usuario, e nenhum destes a cria. Bloquea-los quebraria a bancada sem evitar nada.
// --outra-instancia e a porta explicita para o resto.
static bool LinhaDeComandoIgnoraInstancia() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool ignora = false;
    for (int i = 1; i < argc && !ignora; i++) {
        const std::wstring a = argv[i];
        ignora = (a == L"--outra-instancia" || a == L"--panel-shot" || a == L"--guia-shot" ||
                  a == L"--list" || a == L"--list-windows");
    }
    LocalFree(argv);
    return ignora;
}

// O PID da outra copia, so para o log. O mutex ja e a prova de que ela existe (o SO o
// libera quando o processo morre, entao nao ha orfao); isto e para o diagnostico ficar
// ACIONAVEL - com o PID da para encerrar a copia certa no Gerenciador de Tarefas.
static DWORD PidDaOutraCopia() {
    wchar_t meu[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, meu, MAX_PATH)) return 0;
    const wchar_t* nome = wcsrchr(meu, L'\\');
    nome = nome ? nome + 1 : meu;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W e{}; e.dwSize = sizeof(e);
    DWORD achado = 0;
    const DWORD eu = GetCurrentProcessId();
    if (Process32FirstW(snap, &e)) {
        do {
            if (e.th32ProcessID != eu && _wcsicmp(e.szExeFile, nome) == 0) {
                achado = e.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return achado;
}

// A hora em que a outra copia comecou, tirada da PRIMEIRA linha do log dela
// ("[HH:MM:SS.mmm] DLSS5 For All ... iniciando"). Vazia quando o log nao esta la ou nao tem
// a cara esperada - e ai o aviso nao cita hora nenhuma, em vez de inventar uma.
static std::wstring HoraDaOutraCopia() {
    HANDLE h = CreateFileW((g_exeDir + L"dlss5forall.log").c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    char buf[32]{};
    DWORD lidos = 0;
    const BOOL ok = ReadFile(h, buf, 16, &lidos, nullptr);
    CloseHandle(h);
    if (!ok || lidos < 6 || buf[0] != '[') return {};
    // "[HH:MM" - so os cinco caracteres da hora, e so se forem digitos onde tem de ser
    if (!isdigit((unsigned char)buf[1]) || !isdigit((unsigned char)buf[2]) || buf[3] != ':' ||
        !isdigit((unsigned char)buf[4]) || !isdigit((unsigned char)buf[5])) return {};
    return std::wstring{ (wchar_t)buf[1], (wchar_t)buf[2], L':', (wchar_t)buf[4], (wchar_t)buf[5] };
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    // DPI: janela 1:1 com a captura mesmo em monitor com escala
    {
        typedef BOOL(WINAPI* PFN)(HANDLE);
        HMODULE u = GetModuleHandleW(L"user32.dll");
        PFN f = u ? (PFN)GetProcAddress(u, "SetProcessDpiAwarenessContext") : nullptr;
        if (f) f((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */); else SetProcessDPIAware();
    }
    // O MUTEX VEM ANTES DO LOG, e a ordem e o ponto: o log e aberto em "w" e TRUNCA. Uma
    // segunda copia bloqueada apagaria justamente o log da copia viva - a unica prova do
    // que estava acontecendo. Sabendo aqui que ha outra, o log abre em "a" e a linha do
    // bloqueio entra no fim, sem destruir nada.
    const bool ignoraInstancia = LinhaDeComandoIgnoraInstancia();
    g_mutexInstancia = CreateMutexW(nullptr, FALSE, DLSS5_MUTEX_INSTANCIA);
    const bool outraInstancia = (g_mutexInstancia != nullptr && GetLastError() == ERROR_ALREADY_EXISTS);

    {
        wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
        g_exeDir = p; size_t k = g_exeDir.find_last_of(L"\\/"); g_exeDir = (k == std::wstring::npos) ? L"" : g_exeDir.substr(0, k + 1);
        const wchar_t* modoLog = outraInstancia ? L"a" : L"w";

        // ONDE GRAVAR. O README pede para nao instalar em Program Files, e o publico
        // instala. Neste processo (64 bits) NAO existe a virtualizacao do VirtualStore -
        // ela so vale para processos de 32 bits -, entao a escrita simplesmente FALHA com
        // acesso negado. O sintoma seria mudo e caro: a escolha de fonte nunca lembrada,
        // os atalhos configurados perdidos a cada abertura, e - o pior - a pista
        // [hdr] restaurar nao gravada antes de desligar o HDR do monitor, o que num
        // TerminateProcess deixaria a tela em SDR sem ninguem para religar.
        //
        // O teste e o proprio log: se ele abre na pasta do exe, a pasta aceita escrita.
        g_dadosDir = g_exeDir;
        g_log = _wfopen((g_exeDir + L"dlss5forall.log").c_str(), modoLog);
        if (!g_log) {
            g_pastaDoExeSemEscrita = true;
            wchar_t base[MAX_PATH]{};
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) && base[0]) {
                std::wstring d = std::wstring(base) + L"\\DLSS5ForAll\\";
                CreateDirectoryW(d.c_str(), nullptr);      // ja existir e sucesso para nos
                FILE* f = _wfopen((d + L"dlss5forall.log").c_str(), modoLog);
                if (f) { g_log = f; g_dadosDir = d; }
            }
            // Primeira vez fora da pasta do exe: traz o ini que ja existia ao lado dele,
            // para a pessoa nao perder as escolhas de antes. O TRUE no ultimo argumento e
            // "falhe se ja existir": se migramos numa sessao anterior, a copia nao acontece
            // e o arquivo de la (que e o atual) fica intacto.
            if (!g_dadosDir.empty() && g_dadosDir != g_exeDir)
                CopyFileW((g_exeDir + L"dlss5forall.ini").c_str(),
                          (g_dadosDir + L"dlss5forall.ini").c_str(), TRUE);
        }
    }
    // O IDIOMA VEM ANTES DE QUALQUER TEXTO NA TELA, e depois do g_dadosDir porque e de la
    // que sai o ini. A primeira coisa que a pessoa pode ver ja e traduzida: a caixa de "ja
    // esta aberto", logo abaixo. Sem chave no ini, o Windows decide (ver IdiomaDoSistema).
    {
        const bool escolhido = CarregarIdioma(IniPath());
        Log("idioma: %s (%s)", U8(CodigoLingua(g_lingua)).c_str(),
            escolhido ? "escolhido pela pessoa, no ini" : "deduzido do idioma do Windows");
    }
    // ---- ja ha uma copia aberta? Entao esta nao abre NADA.
    if (outraInstancia && !ignoraInstancia) {
        const DWORD pid = PidDaOutraCopia();
        const std::wstring hora = HoraDaOutraCopia();
        Log("instancia: ja existe outra (pid %lu%s), saindo",
            (unsigned long)pid, pid ? "" : " - desconhecido");
        // MessageBox, e nao cartaz: o nosso cartaz e uma janela topmost que sai da captura e
        // some sozinha - errado para a unica coisa que esta copia tem a dizer. E a mensagem
        // precisa dizer COMO fechar a que esta aberta, senao ela e so uma recusa.
        // As teclas vem do ini, que e o que a copia ABERTA carregou - ela abriu primeiro,
        // entao ficou com as primeiras opcoes da cascata. Aqui nao se pode usar TemAtalho:
        // ele exige 'registrado', e nesta copia nada foi registrado (nem vai ser).
        CarregarAtalhos();
        const std::wstring tPainel = g_atalhos[AT_PAINEL].vk ? TextoAtalho(AT_PAINEL)
                                                             : TS(L"o ícone na bandeja", L"the tray icon");
        const std::wstring tSair   = g_atalhos[AT_SAIR].vk   ? TextoAtalho(AT_SAIR)
                                                             : std::wstring(L"Alt+Esc");
        std::wstring t = TS(L"O DLSS5 For All já está aberto", L"DLSS5 For All is already running");
        if (!hora.empty()) t += TS(L" (desde ", L" (since ") + hora + L")";
        t += TS(L".\n\nFeche-o primeiro: abra o painel com ",
                L".\n\nClose it first: open the panel with ") + tPainel
             + TS(L" e use \"Sair do DLSS5 For All\", ou aperte ",
                  L" and use \"Quit DLSS5 For All\", or press ") + tSair
             + T(L" duas vezes.\n\nDuas cópias juntas empilham duas camadas na mesma tela e a "
                 L"segunda fica sem tecla de saída.",
                 L" twice.\n\nTwo copies at once stack two layers on the same screen, and the "
                 L"second one ends up with no key to quit.");
        // Trazer a copia existente para frente quando isso ADIANTA: se ela ainda esta no
        // seletor, ou e uma janela separada normal, a pessoa provavelmente clicou duas vezes
        // no atalho porque nao a viu. No modo sobreposto NAO se mexe: aquela janela e
        // topmost e click-through (WS_EX_TRANSPARENT), e chamar SetForegroundWindow nela nao
        // traz nada para lugar nenhum - so mexe no foco de quem estava trabalhando.
        for (const wchar_t* cls : { L"FafaDLSS5CamSel", L"FafaDLSS5CamWnd" }) {
            HWND outra = FindWindowW(cls, nullptr);
            if (!outra) continue;
            if (GetWindowLongPtrW(outra, GWL_EXSTYLE) & WS_EX_TRANSPARENT) break;
            if (IsIconic(outra)) ShowWindow(outra, SW_RESTORE);
            SetForegroundWindow(outra);
            Log("instancia: a copia existente (classe %s) foi trazida para frente", U8(cls).c_str());
            break;
        }
        MessageBoxW(nullptr, t.c_str(),
                    (std::wstring(APP_TITLE) + T(L" - já está aberto", L" - already running")).c_str(),
                    MB_OK | MB_ICONINFORMATION);
        if (g_log) { fclose(g_log); g_log = nullptr; }
        // O mutex e da OUTRA copia: fechar o nosso handle nao o destroi, e nao pode destruir.
        if (g_mutexInstancia) { CloseHandle(g_mutexInstancia); g_mutexInstancia = nullptr; }
        return 0;
    }
    Log("DLSS5 For All %s iniciando", U8(APP_VERSION).c_str());
    if (outraInstancia && ignoraInstancia)
        Log("instancia: ja existe outra, mas a linha de comando pediu para seguir assim mesmo");
    // A placa, antes de tudo: e o unico requisito do app que nenhum arquivo copiado
    // resolve, e todo o texto do neural depende de saber a resposta. Nao bloqueia nada -
    // sem RTX o app segue como espelho e captura, so o neural fica indisponivel.
    DetectarGpu();
    // O canal do criador vem do ofertas.ini, e a validacao http(s) e a mesma do resto do
    // modulo - o ini fica ao lado do exe e e editavel por qualquer um.
#ifdef TEM_SPLASH_OFERTAS
    {
        const ofertas::Config oc = ofertas::LoadConfig();
        g_urlCanal = oc.canal;
        if (!g_urlCanal.empty()) Log("ofertas.ini: canal do criador configurado");
    }
#endif
    // A ESCOLHA DA PESSOA SOBRE O FPS NA TELA MANDA, e tem de ser reaplicada aqui.
    // O ReShade regrava o ReShade.ini sozinho (AutoSave) e pode ter devolvido o ShowFPS
    // para o valor dele. Reaplicar agora, antes do seletor e MUITO antes do primeiro
    // swapchain (que e quando o ReShade le o ini - ver GravarTeclaOverlayReShade), faz a
    // escolha valer ja nesta abertura. Chave ausente = ninguem pediu nada: nao se toca em
    // configuracao alheia sem pedido.
    {
        const std::wstring v = IniGet(L"fps_tela");
        if (!v.empty()) DefinirFpsNaTela(v == L"1");
    }
    if (g_pastaDoExeSemEscrita)
        Log("a pasta do exe nao aceita escrita (%s) - ini e log vao para %s",
            U8(g_exeDir).c_str(), g_dadosDir.empty() ? "lugar nenhum" : U8(g_dadosDir).c_str());
    // O CASO SEM SAIDA: nem a pasta do exe nem o %LOCALAPPDATA% aceitaram escrita. Aqui nao
    // ha log para registrar nada e o cartaz da tela so aparece muito depois, se aparecer -
    // e nada do que a pessoa configurar vai sobreviver ao fechamento. E o unico ponto do
    // app em que uma MessageBox de bloqueio se justifica: ela e a unica coisa que a pessoa
    // vai ver a respeito.
    if (g_pastaDoExeSemEscrita && !g_log)
        MessageBoxW(nullptr,
            T(L"Não consigo gravar nem na pasta do app nem na pasta de dados do usuário.\n\n"
              L"O app funciona, mas NADA vai ser lembrado: fonte, atalhos, V-Sync, HDR e o "
              L"\"não mostrar de novo\" voltam ao padrão toda vez.\n\n"
              L"Mova o app para uma pasta sua (Documentos, Área de Trabalho).",
              L"I cannot write to the app folder or to the user data folder.\n\n"
              L"The app works, but NOTHING will be remembered: source, shortcuts, V-Sync, HDR and "
              L"\"don't show this again\" go back to the defaults every time.\n\n"
              L"Move the app to a folder of your own (Documents, Desktop)."),
            (std::wstring(APP_TITLE) + T(L" - sem permissão de escrita", L" - no write permission")).c_str(),
            MB_OK | MB_ICONWARNING);

    // o modulo de HDR loga pelo nosso log, para tudo sair no mesmo arquivo
#ifdef TEM_HDR_CONTROL
    Hdr::g_logSink = [](const char* s) { Log("%s", s); };
#endif

    Gdiplus::GdiplusStartupInput gdipIn;
    ULONG_PTR gdipTok = 0;
    if (Gdiplus::GdiplusStartup(&gdipTok, &gdipIn, nullptr) != Gdiplus::Ok)
        Log("GdiplusStartup falhou - o painel na tela nao vai desenhar");

    // ANTES de qualquer IniGet: um BOM no nosso ini faz o Windows nao achar chave nenhuma
    // e o app volta a todos os padroes em silencio. Ver CorrigirBomDoIni.
    CorrigirBomDoIni();

    // args
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    // CommandLineToArgvW aloca com LocalAlloc; o unique_ptr devolve em qualquer saida,
    // inclusive nas antecipadas (--list, seletor cancelado).
    std::unique_ptr<void, decltype(&LocalFree)> argvGuard(argv, &LocalFree);
    int argDev = -1, argMode = -1, argMon = -1;
    bool argFs = false, argList = false, argListWin = false;
    bool argCursor = false, argFullWin = false, argHide = false, argNoHide = false, argBorder = false;
    bool argOverlay = false, argNoClick = false;
    int  argFps = 0;
    int  argVsync = -1, argHdr = -1;   // -1 = nao veio na linha de comando
    std::wstring argWin, argSource, argShot;
    bool argShotGuia = false;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--panel-shot" && i + 1 < argc) { argShot = argv[++i]; continue; }
        if (a == L"--guia-shot" && i + 1 < argc) { argShot = argv[++i]; argShotGuia = true; continue; }
        if (a == L"--device" && i + 1 < argc) argDev = _wtoi(argv[++i]);
        else if (a == L"--mode" && i + 1 < argc) argMode = _wtoi(argv[++i]);
        else if (a == L"--source" && i + 1 < argc) argSource = argv[++i];
        else if (a == L"--window" && i + 1 < argc) { argWin = argv[++i]; if (argSource.empty()) argSource = L"window"; }
        else if (a == L"--monitor" && i + 1 < argc) { argMon = _wtoi(argv[++i]); if (argSource.empty()) argSource = L"monitor"; }
        else if (a == L"--overlay") { argOverlay = true; if (argSource.empty()) argSource = L"window"; }
        else if (a == L"--no-clickthrough") argNoClick = true;
        else if (a == L"--fps" && i + 1 < argc) argFps = _wtoi(argv[++i]);
        else if (a == L"--cursor") argCursor = true;
        else if (a == L"--full-window") argFullWin = true;
        else if (a == L"--hide-self") argHide = true;
        else if (a == L"--no-hide-self") argNoHide = true;
        else if (a == L"--capture-border") argBorder = true;
        else if (a == L"--vsync") argVsync = 1;
        else if (a == L"--no-vsync") argVsync = 0;
        else if (a == L"--hdr") argHdr = 1;
        else if (a == L"--no-hdr") argHdr = 0;
        else if (a == L"--fullscreen") argFs = true;
        else if (a == L"--list") argList = true;
        else if (a == L"--list-windows") argListWin = true;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!argShot.empty()) {
        std::wstring alvo = IniGet(L"janela");
        g_hud.src = alvo.empty() ? TS(L"janela do jogo", L"the game window") : alvo;
        g_hud.w = 1920; g_hud.h = 1080;
        g_hud.fpsSrc = 59.8; g_hud.fpsPresent = 58.4;
        g_hud.overlay = true; g_hud.bypass = false; g_hud.fpsCap = 0;
        g_hud.vsync = false; g_hud.hdrTreat = false; g_hud.hdrFound = true;
        g_hud.hdrLine = TS(L"monitor em HDR · capturando em 16 bits e convertendo (automático)",
                           L"monitor in HDR · capturing at 16 bits and converting (automatic)");
        g_hud.hdrAviso.clear();   // o caso de hoje: automatico, nada a fazer
        g_hud.neural = LerEstadoNeural(&g_hud.neuralDetalhe);
        // os atalhos que sobraram nesta maquina (Alt+F1/F9/F10 estao tomados por outro app)
        CarregarAtalhos(); g_hkPanel = TextoAtalho(AT_PAINEL);
        for (int i = 0; i < AT_COUNT; i++) g_atalhos[i].registrado = (g_atalhos[i].vk != 0);
        bool ok = argShotGuia ? Guia::SaveShot(argShot.c_str()) : Hud::SaveShot(argShot.c_str());
        Log("--%s '%s' -> %s", argShotGuia ? "guia-shot" : "panel-shot",
            U8(argShot).c_str(), ok ? "ok" : "falhou");
        Guia::Shutdown();
        Hud::Shutdown();
        if (gdipTok) Gdiplus::GdiplusShutdown(gdipTok);
        CoUninitialize();
        if (g_log) { fclose(g_log); g_log = nullptr; }
        return ok ? 0 : 1;
    }

    if (argListWin) {
        auto ws = EnumCaptureWindows(nullptr);
        Log("--- janelas capturaveis (%u) ---", (unsigned)ws.size());
        for (size_t i = 0; i < ws.size(); i++) Log("[%u] %s", (unsigned)i, U8(ws[i].label).c_str());
        auto ms = EnumCaptureMonitors();
        Log("--- monitores (%u) ---", (unsigned)ms.size());
        for (size_t i = 0; i < ms.size(); i++) Log("[%u] %s", (unsigned)i, U8(ms[i].label).c_str());
        MessageBoxW(nullptr, T(L"Lista de janelas e monitores gravada em dlss5forall.log", L"List of windows and monitors written to dlss5forall.log"), APP_TITLE, MB_OK);
        return 0;
    }

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    bool mfOk = SUCCEEDED(hr);   // zerado no fim se a thread de captura ficar presa dentro do MF
    if (!mfOk) Log("MFStartup falhou 0x%08X - so as fontes de tela ficam disponiveis", (unsigned)hr);

    std::vector<Device> devs;
    if (mfOk) devs = EnumDevices();

    if (argList) {
        if (!mfOk) Fatal(TS(L"MFStartup falhou ", L"MFStartup failed ") + Hex(hr) + T(L".\nWindows N? Instala o Media Feature Pack.", L".\nWindows N? Install the Media Feature Pack."));
        for (auto& d : devs) LoadModes(d);
        MessageBoxW(nullptr, T(L"Lista de dispositivos e modos gravada em dlss5forall.log", L"List of devices and modes written to dlss5forall.log"), APP_TITLE, MB_OK);
        return 0;
    }

    Choice C;
    bool fromCli = false;

    if (argSource == L"window" || argSource == L"janela") {
        // Sem --window nao ha o que casar: IStrHas com string vazia da true para TODAS as
        // janelas, e o app abria filtrando a primeira da lista - tipicamente o navegador ou
        // o proprio terminal de quem escreveu o atalho.
        if (argWin.empty())
            Fatal(TS(L"--source window precisa de --window <parte do título ou do nome do processo>.",
                     L"--source window needs --window <part of the title or of the process name>.") +
                  std::wstring(1, L'\n') +
                  T(L"Use --list-windows para ver as janelas disponíveis.",
                    L"Use --list-windows to see which windows are available."));
        auto ws = EnumCaptureWindows(nullptr);
        int found = -1;
        for (size_t i = 0; i < ws.size(); i++)
            if (IStrHas(ws[i].title, argWin) || IStrHas(ws[i].proc, argWin)) { found = (int)i; break; }
        if (found < 0) {
            for (size_t i = 0; i < ws.size(); i++) Log("janela [%u] %s", (unsigned)i, U8(ws[i].label).c_str());
            Fatal(TS(L"Nenhuma janela visivel casa com \"", L"No visible window matches \"") + argWin + T(L"\".\nA lista das janelas visiveis foi pro dlss5forall.log (--list-windows).", L"\".\nThe list of visible windows went to dlss5forall.log (--list-windows)."));
        }
        C.kind = SRC_WINDOW; C.win = ws[found].hwnd; C.winTitle = ws[found].title;
        C.clientOnly = !argFullWin; C.cursor = argCursor; C.hideSelf = argHide; C.border = argBorder;
        C.overlay = argOverlay; C.clickThrough = !argNoClick; C.fpsCap = argFps;
        if (argVsync >= 0) C.vsync = (argVsync == 1);
        if (argHdr >= 0) C.hdrTreat = (argHdr == 1);
        C.fs = argFs && !argOverlay; fromCli = true;
    } else if (argSource == L"monitor" || argSource == L"screen" || argSource == L"tela") {
        auto ms = EnumCaptureMonitors();
        if (ms.empty()) Fatal(T(L"Nenhum monitor enumerado.", L"No monitor was enumerated."));
        int idx = (argMon >= 0 && argMon < (int)ms.size()) ? argMon : 0;
        C.kind = SRC_MONITOR; C.mon = ms[idx].mon; C.monLabel = ms[idx].label;
        C.cursor = argCursor; C.hideSelf = !argNoHide; C.border = argBorder;
        C.overlay = argOverlay; C.clickThrough = !argNoClick; C.fpsCap = argFps;
        C.fs = argFs && !argOverlay; fromCli = true;
        if (argVsync >= 0) C.vsync = (argVsync == 1);
        if (argHdr >= 0) C.hdrTreat = (argHdr == 1);
    } else if (argDev >= 0 && argDev < (int)devs.size()) {
        C.kind = SRC_CAMERA; C.dev = argDev; LoadModes(devs[argDev]);
        C.mode = (argMode >= 0 && argMode < (int)devs[argDev].modes.size()) ? argMode : BestMode(devs[argDev].modes);
        if (C.mode < 0) Fatal(T(L"O dispositivo escolhido nao tem modos de video.", L"The device you picked has no video modes."));
        C.fpsCap = argFps;
        if (argVsync >= 0) C.vsync = (argVsync == 1);
        if (argHdr >= 0) C.hdrTreat = (argHdr == 1);
        C.fs = argFs; fromCli = true;
    }

    // O mapa de atalhos e carregado E registrado antes do seletor: o dialogo "Atalhos..."
    // precisa saber quais teclas realmente pegaram, senao mostraria "em uso por outro
    // programa" para todas - o registro ainda nem teria sido tentado.
    CarregarAtalhos();
    RegistrarTodosAtalhos();

    g_threadPrincipal = GetCurrentThreadId();
    // A tela do grupo vem ANTES do seletor, e SO nesse caminho.
    //
    // Quem abre por linha de comando (--source/--window/--monitor/--device) criou um atalho
    // "jogo + filtro" e vai usar isso todo dia: fazer essa pessoa fechar propaganda a mao a
    // cada partida transforma o convite em estorvo. A propaganda e para quem abre o app na
    // mao, que ja esta parado escolhendo a fonte. O item do painel e o da bandeja continuam
    // abrindo a mesma tela sob demanda, nos dois modos.
    if (!fromCli && !OfertasNaAbertura(hi)) {
        Log("encerrado pelo atalho de sair, na tela do grupo");
        if (gdipTok) Gdiplus::GdiplusShutdown(gdipTok);
        CoUninitialize();
        if (g_log) { fclose(g_log); g_log = nullptr; }
        return 0;
    }

    if (!fromCli) {
        if (devs.empty() && !mfOk) Log("sem camera/placa: o seletor abre direto em Janela");
        std::wstring wk = IniGet(L"fonte"), wd = IniGet(L"dispositivo"), wm = IniGet(L"modo");
        std::wstring wj = IniGet(L"janela"), wn = IniGet(L"monitor");
        bool wf = IniGet(L"telacheia") == L"1";
        bool wv = IniGet(L"vsync") == L"1";
        // a caixa do HDR mora no ini do proprio modulo ([hdr] tratar), fonte unica
#ifdef TEM_HDR_CONTROL
        bool wh = Hdr::TreatmentEnabledFromIni(IniPath());
#else
        bool wh = false;
#endif
        // "primeira vez" = nao ha escolha de fonte salva. So ai valem os padroes novos
        // (Janela + SOBREPOR); quem ja escolheu antes manda.
        const bool primeiraVez = wk.empty();
        { std::wstring so = IniGet(L"sobrepor"); g_wantOverlay = so.empty() ? -1 : (so == L"1" ? 1 : 0); }
        // O laco existe por causa da troca de idioma: ela fecha o seletor e ele reabre
        // inteiro no idioma novo (ver g_selTrocouIdioma). Sem escolher a fonte, a saida do
        // laco continua sendo a mesma de sempre - INICIAR (true) ou fechar (false).
        bool okSel = false;
        do {
            g_selTrocouIdioma = false;
            okSel = ShowSelector(hi, devs, C, wk, wd, wm, wj, wn, wf, wv, wh, primeiraVez);
        } while (!okSel && g_selTrocouIdioma);
        if (!okSel) { Log("Cancelado no seletor"); return 0; }
        IniSet(L"fonte", C.kind == SRC_WINDOW ? L"janela" : C.kind == SRC_MONITOR ? L"monitor" : L"camera");
        IniSet(L"telacheia", C.fs ? L"1" : L"0");
        IniSet(L"vsync", C.vsync ? L"1" : L"0");
        // O FPS na tela mora em DOIS lugares e isso e de proposito: o ReShade.ini e quem o
        // ReShade le, e o nosso ini e quem lembra a escolha. Sao dois porque o ReShade
        // regrava o ini dele sozinho (AutoSave) e pode devolver o valor antigo; na proxima
        // abertura o nosso ini reaplica a escolha da pessoa, que e quem manda.
        IniSet(L"fps_tela", C.fpsTela ? L"1" : L"0");
        DefinirFpsNaTela(C.fpsTela);
        if (C.kind != SRC_CAMERA) IniSet(L"sobrepor", C.overlay ? L"1" : L"0");

        if (C.kind == SRC_CAMERA) { IniSet(L"dispositivo", devs[C.dev].name); IniSet(L"modo", devs[C.dev].modes[C.mode].label); }
        else if (C.kind == SRC_WINDOW) {
            // Titulo de janela pode conter CR/LF (raro, mas existe - alguns players poem).
            // WritePrivateProfileString grava cru e a linha seguinte do ini vira lixo, o que
            // derruba TODAS as chaves depois dela.
            std::wstring lim = C.winTitle;
            for (wchar_t& c : lim) if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
            IniSet(L"janela", lim);
        }
        else IniSet(L"monitor", C.monLabel);
    }

    // ---------------------------------------------------------------- abre a fonte
    // NO HEAP, de proposito. O Stop() abandona a thread quando o ReadSample trava (cabo ou
    // placa ruim): ela continua usando mtx, buf e evt DEPOIS que o Stop volta. Com o objeto
    // na pilha do wWinMain, o retorno da funcao destruiria o mutex e o vector debaixo dela -
    // use-after-free na saida, exatamente no caso em que o hardware ja estava com problema.
    // No heap, quando a thread sobrevive nos simplesmente NAO liberamos: o processo esta
    // encerrando e um vazamento de alguns KB e melhor do que uma tela de erro.
    std::unique_ptr<Capture> capPtr(new Capture());
    Capture&     cap = *capPtr;   // camera / placa
    ScreenCapture scap;       // janela / monitor
    std::wstring err, srcName;
    UINT32 cw = 0, ch = 0;

    if (C.kind == SRC_CAMERA) {
        Device& D = devs[C.dev]; const Mode M = D.modes[C.mode];
        Log("Fonte: camera '%s' modo [%u] %s", U8(D.name).c_str(), M.nativeIndex, U8(M.label).c_str());
        if (!cap.Start(D.act, M.nativeIndex, &err)) Fatal(TS(L"Nao consegui abrir a captura:\n", L"Could not start the capture:\n") + err);
        cap.Size(cw, ch);
        srcName = D.name;
    } else {
        scap.targetWin = C.win; scap.targetMon = C.mon; scap.clientOnly = C.clientOnly;
        RECT want{}, org{};
        if (!scap.TargetRect(&want, &org))
            Fatal(C.kind == SRC_WINDOW ? T(L"A janela escolhida sumiu antes de comecar.", L"The window you picked disappeared before we started.") : T(L"Nao consegui medir o monitor.", L"Could not measure the monitor."));
        cw = (UINT32)(want.right - want.left); ch = (UINT32)(want.bottom - want.top);
        srcName = (C.kind == SRC_WINDOW) ? C.winTitle : C.monLabel;
        Log("Fonte: %s '%s' %ux%u (recorte=%s cursor=%d esconder=%d)",
            C.kind == SRC_WINDOW ? "janela" : "monitor", U8(srcName).c_str(), cw, ch,
            C.clientOnly ? "area util" : "inteira", (int)C.cursor, (int)C.hideSelf);
    }
    if (!cw || !ch) Fatal(T(L"A fonte reportou resolucao invalida (0x0).", L"The source reported an invalid resolution (0x0)."));

    // ---------------------------------------------------------------- janela de render
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = RenderWndProc; wc.hInstance = hi; wc.lpszClassName = L"FafaDLSS5CamWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon   = IconeApp(GetSystemMetrics(SM_CXICON));
    wc.hIconSm = IconeApp(GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&wc);
    if (C.overlay && C.kind == SRC_CAMERA) { C.overlay = false; Log("modo sobreposto nao vale pra camera - caindo pro espelho"); }
    if (C.overlay && C.kind == SRC_MONITOR) C.hideSelf = true;
    if (C.overlay) C.fs = false;

    std::wstring title = std::wstring(APP_TITLE) + L" - " + srcName;
    HWND hwnd = nullptr;
    if (C.overlay) {
        // popup sem borda, do tamanho exato do alvo, colado em cima dele.
        // Sem WS_EX_TOOLWINDOW ela continua sendo uma janela "de verdade" pro sistema:
        // o OBS a lista em Captura de Janela e grava o resultado ja filtrado.
        // So quando ela precisa sumir das capturas (fonte monitor) vira toolwindow.
        RECT want{}, org{}; scap.TargetRect(&want, &org);
        DWORD ex = WS_EX_TOPMOST | (C.hideSelf ? WS_EX_TOOLWINDOW : 0);
        if (C.clickThrough) ex |= (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        g_clickThrough = C.clickThrough;
        hwnd = CreateWindowExW(ex, wc.lpszClassName, title.c_str(), WS_POPUP,
                               want.left, want.top, (int)cw, (int)ch, nullptr, nullptr, hi, nullptr);
    } else {
        RECT wr{ 0, 0, (LONG)cw, (LONG)ch }; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        // a area util do monitor ONDE O ALVO ESTA - o SPI_GETWORKAREA responde sempre
        // pelo primario, e numa maquina de dois monitores isso abria a janela do tamanho
        // errado (e, com o alvo num monitor maior, do tamanho de nada)
        RECT wa = AreaParaJanela(wr.right - wr.left, wr.bottom - wr.top);
        {   // AreaParaJanela pode devolver o retangulo do proprio alvo; aqui queremos a tela
            MONITORINFO mi{ sizeof(mi) };
            POINT c{ wa.left + (wa.right - wa.left) / 2, wa.top + (wa.bottom - wa.top) / 2 };
            GetMonitorInfoW(MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST), &mi);
            wa = mi.rcWork;
        }
        int ww = std::min<int>(wr.right - wr.left, wa.right - wa.left), wh = std::min<int>(wr.bottom - wr.top, wa.bottom - wa.top);
        Log("janela separada: pedido %dx%d, area util %ldx%ld -> %dx%d",
            (int)(wr.right - wr.left), (int)(wr.bottom - wr.top),
            wa.right - wa.left, wa.bottom - wa.top, ww, wh);
        hwnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                               wa.left + (wa.right - wa.left - ww) / 2, wa.top + (wa.bottom - wa.top - wh) / 2,
                               ww, wh, nullptr, nullptr, hi, nullptr);
    }
    if (!hwnd) Fatal(T(L"CreateWindow falhou", L"CreateWindow failed"));

    // ------------------------------------------- a resolucao de trabalho da rede neural
    //
    // O DLSS pode rodar na resolucao inteira (DLAA, 100%) ou trabalhar menor e reconstruir.
    // Isso muda TUDO no custo, porque o que a rede processa e AREA: 3440x1440 sao 5,0
    // megapixels por quadro contra 2,1 de um 1080p - mais que o dobro de conta.
    //
    // Medido nesta maquina, num ultrawide com work_resolution=100: 30 ms por quadro, dos
    // quais o feed usava 1,1 ms (4%). Os outros 96% eram a rede - e o app inteiro andava
    // a 20-24 fps por causa disso, sem que baixar os graficos do jogo mudasse nada (o
    // gargalo nunca foi o jogo).
    //
    // Um numero fixo aqui seria errado para alguem: 67 salva o ultrawide e joga qualidade
    // fora num 1080p, que aguenta DLAA folgado. Entao o app escolhe pela area real da
    // fonte, nos degraus que o proprio DLSS usa (100 DLAA, 77, 67 Quality, 58 Balanced).
    // Quem quiser mandar no valor poe um numero em [cadeia] work_resolution no
    // dlss5forall.ini; "auto" (ou vazio) e o padrao.
    //
    // ANTES do R.Init de proposito: quem le este arquivo e o dlss5-feed, e ele le uma vez
    // so, quando o ReShade sobe junto com a swapchain - que e o que o R.Init faz.
    if (C.kind != SRC_CAMERA || cw) {
        const double mp = (double)cw * (double)ch / 1e6;
        int wr = (mp <= 2.4) ? 100 : (mp <= 4.0) ? 77 : (mp <= 6.0) ? 67 : 58;
        const std::wstring manual = IniGet(L"work_resolution");
        if (!manual.empty() && manual != L"auto") {
            const int v = _wtoi(manual.c_str());
            if (v >= 33 && v <= 100) {
                wr = v; g_hud.workRes = v;
                Log("cadeia: work_resolution=%d (escolha do ini)", wr);
            }
        } else {
            g_hud.workRes = 0;   // automatico: o chip "Auto" fica marcado no painel
            Log("cadeia: work_resolution=%d (automatico para %ux%u, %.1f megapixels)", wr, cw, ch, mp);
        }
        GravarWorkResolution(wr);
    }

    Renderer R;
    // O backbuffer nasce SEMPRE no tamanho da captura. A "Resolucao interna" que ficava
    // aqui (com marca de pendente no ini, watchdog e teste de sobrevivencia) foi medida e
    // retirada por decisao do usuario - ver docs/painel.md. Uma chave [ultima] escala= de
    // uma instalacao antiga e simplesmente ignorada, sem cartaz e sem reversao.
    R.cpuUpload = (C.kind == SRC_CAMERA);
    if (!R.Init(hwnd, cw, ch, &err)) Fatal(TS(L"Direct3D 11 falhou:\n", L"Direct3D 11 failed:\n") + err);
    g_renderer = &R;

    // =================================================================================
    //  O CLIQUE SO ATRAVESSA COM WS_EX_LAYERED. Isto nao e superstic ao: e o que a
    //  documentacao do WM_NCHITTEST diz, palavra por palavra, sobre HTTRANSPARENT:
    //
    //    "In a window currently covered by another window IN THE SAME THREAD (the message
    //     will be sent to underlying windows IN THE SAME THREAD until one of them returns
    //     a code that is not HTTRANSPARENT)."
    //
    //  Ou seja: responder HTTRANSPARENT so empurra o clique para janelas NOSSAS. Para o
    //  Explorer, para o desktop, para o jogo - qualquer coisa de outro processo - ele nao
    //  faz nada, e o clique morre na nossa camada. Foi exatamente o que a medicao mostrou:
    //  hit-test respondendo -1 em 0,2 ms e o clique sem chegar em ninguem.
    //
    //  Quem atravessa processos e a combinacao LAYERED + TRANSPARENT, e isso tambem esta
    //  documentado, na pagina de layered windows: "If the layered window has the
    //  WS_EX_TRANSPARENT extended window style, the shape of the layered window will be
    //  ignored and the mouse events will be passed to other windows underneath".
    //
    //  DEPOIS do R.Init, de proposito: a swapchain nasce numa janela comum e so entao a
    //  janela vira layered. Criar layered ANTES arrisca o CreateSwapChainForHwnd em flip
    //  model, que e o caminho rapido que o app inteiro depende.
    //
    //  DLSS5_SEM_LAYERED=1 volta ao comportamento antigo - a saida se o layered brigar com
    //  o flip model nesta maquina (imagem preta ou Present falhando).
    if (C.overlay && C.clickThrough) {
        wchar_t env[8]{};
        const bool desligado = GetEnvironmentVariableW(L"DLSS5_SEM_LAYERED", env, 8) && env[0] == L'1';
        if (desligado) {
            Log("camada: WS_EX_LAYERED desligado por DLSS5_SEM_LAYERED=1 - o clique NAO vai atravessar");
        } else {
            LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
            const bool ok = SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA) != 0;
            Log("camada: WS_EX_LAYERED ligado (alpha 255, ok=%d) - sem ele o clique nao atravessa"
                " para outros processos", (int)ok);
        }
    }

    if (C.kind != SRC_CAMERA) {
        if (C.hideSelf && !HideFromCapture(hwnd))
            Log("SetWindowDisplayAffinity(EXCLUDEFROMCAPTURE) falhou - Windows antigo? cuidado com o espelho");
        if (!scap.Start(R.dev, C.win, C.mon, C.clientOnly, C.cursor, C.border, &err))
            Fatal(TS(L"Nao consegui abrir a captura de tela:\n", L"Could not start the screen capture:\n") + err);
        R.extSrv = scap.srv;
        if (scap.w != R.srcW || scap.h != R.srcH) {
            if (!R.Resize(scap.w, scap.h, &err)) Fatal(TS(L"Falha ao redimensionar:\n", L"Resize failed:\n") + err);
            ResizeClientTo(hwnd, R.srcW, R.srcH);
        }
    }

    // Estimador de motion vector no bloco de video da GPU. Depois da captura estar de
    // pe, porque so aqui a resolucao da fonte esta decidida. Falhar aqui nao e fatal:
    // o Ensure loga o motivo e o app segue no Lumenite_Kernel.
    MvHw::Ensure(R.srcW, R.srcH, R.dev);

    bool overlayVisible = true, bypass = false;
    HWND trayWnd = nullptr;

    // ------------------------------------------------------- AQUECIMENTO EM BYPASS
    // A CAMADA NAO PODE NASCER NA FRENTE ANTES DE TER O QUE MOSTRAR.
    //
    // Cronologia medida numa sessao real (monitor 3440x1440, logs das 15:27):
    //   15:27:46.06  swapchain pronto - a camada topmost ja cobre a tela inteira
    //   15:27:47.48  DLSS5_Feed.fx compilado
    //   15:27:48.34  sessao D3D12 do feed aberta
    //   15:27:49.99  build da feature
    //   15:27:50.36  feature 18 criada + primeira avaliacao
    // Sao ~4 s em que a camada esta na frente e o nosso Present fica preso atras da
    // criacao das features NGX na mesma GPU: a camada exibe um quadro PARADO enquanto o
    // desktop de verdade continua vivo por baixo. Os cliques atravessam (o click-through
    // esta certo desde a criacao da janela), mas a pessoa nao ve efeito nenhum e conclui
    // o obvio: "nada na tela estava clicavel". Num monitor inteiro isso e a tela toda.
    //
    // A correcao e nao mostrar o que ainda nao existe: a camada nasce em BYPASS - o mesmo
    // caminho que ja a tira de vista - e so aparece quando a cadeia entrega de verdade
    // (CadeiaEntregando). Vale para janela sobreposta tanto quanto para monitor.
    bool aquecendo = false;
    ULONGLONG aquecimentoInicio = 0, aquecimentoAte = 0, proximaSondagem = 0, proximoCartazAquec = 0;
    ULONGLONG meuCartazAquecAte = 0;   // ate quando o cartaz na tela e o do aquecimento
    if (C.overlay) {
        aquecendo = true;
        bypass = true;
        aquecimentoInicio = GetTickCount64();
        // Teto de seguranca. Sem ele, uma cadeia que nunca sobe deixaria a camada
        // escondida para sempre e o app pareceria nao ter feito nada - trocar um defeito
        // visivel por um invisivel nao e conserto.
        aquecimentoAte = GetTickCount64() + 30000;
        Log("aquecimento: a camada comeca em bypass ate a cadeia entregar (teto 30 s)");
    }
    g_trayBypass = bypass;

    // ---------------------------------------------------------------- painel na tela
    g_hud.inst = hi; g_hud.render = hwnd; g_hud.src = srcName;
    g_hud.bypass = bypass;   // aquecendo, o painel tem de dizer que o filtro ainda nao aparece
    g_hud.w = R.srcW; g_hud.h = R.srcH; g_hud.overlay = C.overlay; g_hud.fpsCap = C.fpsCap;
    g_hud.vsync = C.vsync; g_hud.hdrTreat = C.hdrTreat;
    // O painel mostra o que esta VALENDO, e quem sabe isso e o ReShade.ini (o seletor ja
    // gravou ali). Chave ausente = padrao do ReShade, que e ligado.
    g_hud.fpsTela = C.fpsTela = (FpsNaTelaNoReShadeIni() != 0);
    g_hud.camera = (C.kind == SRC_CAMERA);
    g_hud.neural = LerEstadoNeural(&g_hud.neuralDetalhe);   // antes do primeiro cartaz
    Log("neural na abertura: estado=%d (%s)", (int)g_hud.neural, U8(g_hud.neuralDetalhe).c_str());
    // onde as nossas janelas devem aparecer: no monitor do alvo, dentro dele se couber
    g_alvoWin = C.win;
    g_alvoMon = C.mon ? C.mon
              : (C.win ? MonitorFromWindow(C.win, MONITOR_DEFAULTTONEAREST) : nullptr);
    // a tela do grupo sai na captura (e propaganda do canal), menos no monitor
    // sobreposto - la a nossa propria captura a filmaria e ela entraria no filtro
    g_ofertasNaCaptura = !(C.overlay && C.kind == SRC_MONITOR);
    R.vsync = C.vsync;

    // ---------------------------------------------------------------- HDR
    // Duas coisas diferentes, e vale nao confundir:
    //  - a CONVERSAO e automatica. Se o monitor do alvo esta em HDR, a captura ja abriu
    //    o pool em R16G16B16A16Float (scRGB linear) e o PS converte na hora. Nao mexe no
    //    display de ninguem, nao tem efeito colateral e nao passa pela caixinha.
    //  - a CAIXA e o plano B: desligar o HDR do monitor enquanto o app roda.
    // Na fonte de placa/console nao ha FP16 que resolva - ali a caixa autoriza a
    // conversao de PQ/BT.2020, que e palpite e por isso pede confirmacao.
#ifdef TEM_HDR_CONTROL
    g_hdrTr.Init(IniPath());
    g_hdrTr.RecoverStale();          // desfaz o que uma sessao morta tenha deixado
    if (C.kind == SRC_CAMERA) {
        IMFMediaType* cur = nullptr;
        if (cap.reader && SUCCEEDED(cap.reader->GetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur)) && cur) {
            g_hdrTr.SetSourceVideo(Hdr::VideoColorFromMediaType(cur));
            cur->Release();
        }
    } else {
        HMONITOR alvoMon = C.mon ? C.mon
                         : (C.win ? MonitorFromWindow(C.win, MONITOR_DEFAULTTONEAREST)
                                  : MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
        g_hdrTr.SetSourceScreen(hwnd, alvoMon);
        // qual formato a captura conseguiu abrir - e o que decide a conversao no PS
        // Depois de um Detect(): o modulo so guarda o formato da tela quando ja sabe em
        // que monitor esta, e o Detect e quem descobre isso.
        g_hdrTr.Detect();
        g_hdrTr.SetScreenFp16(scap.hdrFp16, scap.hdrSdrWhiteNits);
    }
    HdrRefreshLine();
    // SEMPRE, nao so quando marcado. Quem desmarcou a caixa no seletor com [hdr] tratar=1
    // no ini deixava o modulo com enabled_ = true; o WarnLine() devolve vazio nesse estado,
    // entao o aviso "a imagem vai chegar lavada" sumia justamente para quem desmarcou.
    g_hud.hdrTreat = C.hdrTreat = HdrSetTreat(C.hdrTreat);
    HdrRefreshLine();   // a linha tem de contar o que a acao fez, nao o que havia antes
    Log("HDR: detectado=%d conversao=%s caixa=%d agindo=%d (%s)",
        (int)g_hud.hdrFound,
        g_hdrTr.Shader().mode == 3 ? "fp16" : g_hdrTr.Shader().mode ? "pq" : "nenhuma",
        (int)g_hud.hdrTreat, (int)g_hud.hdrActing, U8(g_hud.hdrLine).c_str());
#endif
    // O atalho do painel entra ANTES dos outros: assim ele fica com a primeira opcao
    // livre e os demais caem nas alternativas deles, sem brigar entre si.
    // ja carregado e registrado antes do seletor; aqui so revalida (idempotente)
    RegistrarTodosAtalhos();
    // Se a tecla do painel for a mesma que abre o overlay do ReShade, as duas brigam em
    // silencio - melhor dizer.
    {
        UINT vkRs = 0; TeclaOverlayReShade(&vkRs);
        if (vkRs && vkRs == g_atalhos[AT_PAINEL].vk && g_atalhos[AT_PAINEL].mods == 0)
            Log("ATENCAO: %s abre o painel E o overlay do ReShade - mude uma das duas",
                U8(NomeTeclaCanonica(g_atalhos[AT_PAINEL].mods, g_atalhos[AT_PAINEL].vk)).c_str());
    }

    if (C.overlay) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);   // ver o comentario do outro ramo
        // teclado nao chega numa janela click-through: os atalhos viram hotkeys globais
        static const HkOpt kBypass[] = {
            { MOD_ALT, VK_F9, L"Alt+F9" }, { MOD_ALT, VK_F8, L"Alt+F8" }, { MOD_ALT, VK_F7, L"Alt+F7" },
            { MOD_ALT, VK_F6, L"Alt+F6" }, { MOD_ALT, VK_F5, L"Alt+F5" }, { MOD_ALT, VK_F2, L"Alt+F2" },
            { MOD_CONTROL | MOD_ALT, VK_F9, L"Ctrl+Alt+F9" }, { MOD_CONTROL | MOD_ALT, 'O', L"Ctrl+Alt+O" },
        };
        static const HkOpt kInteract[] = {
            { MOD_ALT, VK_F10, L"Alt+F10" }, { MOD_ALT, VK_F11, L"Alt+F11" }, { MOD_ALT, VK_F3, L"Alt+F3" },
            { MOD_ALT, VK_F1, L"Alt+F1" },
            { MOD_CONTROL | MOD_ALT, VK_F10, L"Ctrl+Alt+F10" }, { MOD_CONTROL | MOD_ALT, 'I', L"Ctrl+Alt+I" },
        };
        static const HkOpt kQuit[] = {
            { MOD_ALT, VK_F12, L"Alt+F12" }, { MOD_ALT, VK_F11, L"Alt+F11" },
            { MOD_CONTROL | MOD_ALT, VK_F12, L"Ctrl+Alt+F12" }, { MOD_CONTROL | MOD_ALT, 'Q', L"Ctrl+Alt+Q" },
        };
        RegisterFirstFree(HK_BYPASS,   kBypass,   (int)(sizeof(kBypass) / sizeof(kBypass[0])),     g_hkBypass);
        // A CASCATA DO INTERATIVO SO EXISTE COMO RESERVA, e agora ela sabe disso.
        //
        // Desde que a tecla do interativo faz o ciclo inteiro - abre o overlay do ReShade E
        // entrega o mouse pra ele; aperta de novo, fecha e devolve -, registrar Alt+F10,
        // Alt+F11 e Ctrl+Alt+I por cima e tomar tres combinacoes do sistema (e dos jogos)
        // para repetir o que uma tecla ja faz. Com atalho proprio configurado, a cascata
        // nao entra. Sem ele, ela volta a valer: sem NENHUMA tecla, o modo interativo vira
        // uma armadilha, porque so a volta automatica devolveria o mouse.
        const bool temInterativoProprio = TemAtalho(AT_INTERATIVO);
        bool okInteract = false, okInteract2 = false;
        if (!temInterativoProprio) {
            okInteract = RegisterFirstFree(HK_INTERACT, kInteract,
                                           (int)(sizeof(kInteract) / sizeof(kInteract[0])), g_hkInteract);
            okInteract2 = RegisterHotKey(nullptr, HK_INTERACT2, MOD_CONTROL | MOD_ALT, 'I') != 0;
        } else {
            g_hkInteract = TextoAtalho(AT_INTERATIVO);
            Log("atalhos: a cascata do interativo (Alt+F10/Alt+F11/Ctrl+Alt+I) nao foi registrada -"
                " %s ja faz o ciclo inteiro", U8(g_hkInteract).c_str());
        }
        RegisterFirstFree(HK_QUIT,     kQuit,     (int)(sizeof(kQuit) / sizeof(kQuit[0])),         g_hkQuit);
        // reservas que sempre tentam existir, alem das escolhidas acima
        RegisterHotKey(nullptr, HK_BYPASS2,   MOD_CONTROL | MOD_ALT, 'O');
        RegisterHotKey(nullptr, HK_QUIT2,     MOD_CONTROL | MOD_ALT, 'Q');
        // A SAIDA DE EMERGENCIA DO MODO INTERATIVO, decidida aqui e uma vez so.
        // Fora do click-through a camada fica com o mouse (a tela inteira, na fonte
        // monitor); prometer uma tecla que o Windows recusou seria a armadilha completa.
        // Ordem: o atalho que a pessoa escolheu, a cascata do sobreposto, a reserva fixa.
        // Vazia = nao existe nenhuma, e ai so a volta automatica devolve - por isso ela
        // encurta nesse caso (ver kInterTotalSemTecla).
        if (TemAtalho(AT_INTERATIVO))  g_saidaInterativo = TextoAtalho(AT_INTERATIVO);
        else if (okInteract)           g_saidaInterativo = g_hkInteract;
        else if (okInteract2)          g_saidaInterativo = L"Ctrl+Alt+I";
        Log("Modo sobreposto: %s = tira/poe o filtro | %s = abre/fecha o ReShade (e o mouse) | %s = sair"
            " | volta automatica do interativo: %s",
            U8(g_hkBypass).c_str(), U8(g_hkInteract).c_str(), U8(g_hkQuit).c_str(),
            g_saidaInterativo.empty() ? "NENHUMA TECLA (so a volta automatica)"
                                      : U8(g_saidaInterativo).c_str());

    } else {
        // SW_SHOWNORMAL + RESTORE de proposito: a PRIMEIRA chamada de ShowWindow de um
        // processo obedece ao wShowWindow que veio do processo pai. Aberto por um atalho
        // marcado como "minimizado" (ou por um script), o app nascia com a janela em
        // -32000 e o usuario nao via nada.
        ShowWindow(hwnd, SW_SHOWNORMAL);
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        // o mesmo cartaz-guia do modo sobreposto: as duas teclas e nada mais
    }
    // A BANDEJA VALE NOS DOIS MODOS. Ela so existia no sobreposto, e o cartaz-guia
    // prometia "o icone na bandeja tem os mesmos comandos" para todo mundo - inclusive
    // para quem esta no modo janela separada, que e o modo que o README recomenda para
    // gravar no OBS. A pessoa procurava um icone que nao estava la.
    trayWnd = MakeTrayWindow(hi);
    g_trayWnd = trayWnd;               // o Fatal precisa saber para tirar o icone
    if (trayWnd)
        TrayAdd(trayWnd, std::wstring(APP_TITLE) + L" - " + srcName +
                         (TemAtalho(AT_PAINEL) ? (L"  (" + TextoAtalho(AT_PAINEL) +
                                                  TS(L" abre o painel)", L" opens the panel)"))
                                               : TS(L"  (clique aqui para o painel)",
                                                    L"  (click here for the panel)")));

    UpdateWindow(hwnd);
    if (C.fs) ToggleFullscreen(hwnd);

    // ALGUMA tecla foi recusada pelo Windows (outra instancia nossa, um gravador residente
    // com F6 global). O painel ja pintava a tecla recusada de laranja na secao ATALHOS, mas
    // so quem abrisse o painel veria - e o painel pode ser justamente o que ficou sem tecla.
    if (g_algumAtalhoRecusado) {
        std::wstring t = TS(L"Alguma tecla já estava em uso\n", L"Some key was already taken\n");
        t += TemAtalho(AT_PAINEL) ? (TS(L"o painel abre com ", L"the panel opens with ") +
                                     TextoAtalho(AT_PAINEL) + L"; ")
                                  : TS(L"use o ícone na bandeja; ", L"use the tray icon; ");
        t += T(L"as outras estão em ATALHOS, no painel", L"the rest are under SHORTCUTS, in the panel");
        ShowToast(hi, C.mon, t, 8000);
    }

    // A tela do grupo NAO aparece aqui: ela ja saiu na abertura, antes do seletor (ver
    // OfertasNaAbertura). Mostrar de novo ao iniciar a captura seria a segunda propaganda
    // da mesma execucao. A porta permanente continua sendo o item do painel e da bandeja.
    //
    // Isto tambem resolve um empilhamento: com a splash sem prazo, ela e o cartaz-guia
    // nasciam juntos, os dois topmost e centralizados na mesma area, um por cima do outro
    // ate a pessoa fechar os dois. Agora a splash ja fechou quando o guia abre.
    //
    // A pasta do exe nao aceita escrita (tipicamente Program Files). Nao e fatal - as
    // escolhas passam a morar no LOCALAPPDATA -, mas a pessoa precisa saber, porque o
    // ReShade grava na pasta dele e vai ter o mesmo problema com presets e cache.
    if (g_pastaDoExeSemEscrita) {
        ShowToast(hi, C.mon, g_dadosDir == g_exeDir
            ? T(L"NAO CONSIGO GRAVAR NA PASTA DO APP\nsuas escolhas nao vao ser lembradas.\n"
                L"Mova o app para uma pasta sua (Documentos, Area de Trabalho).",
                L"I CANNOT WRITE TO THE APP FOLDER\nyour choices will not be remembered.\n"
                L"Move the app to a folder of your own (Documents, Desktop).")
            : T(L"A PASTA DO APP E SOMENTE LEITURA\nsuas escolhas vao para a pasta de dados do usuario.\n"
                L"O ReShade tambem grava aqui - mova o app para uma pasta sua.",
                L"THE APP FOLDER IS READ-ONLY\nyour choices are going to your user data folder.\n"
                L"ReShade writes here too - move the app to a folder of your own."), 9000);
    }

    // O cartaz de boas-vindas E o guia: uma tela so, grande, com as teclas que valem.
    // Fica 30 s por padrao ([cartaz] segundos no ini) e some antes se a pessoa fechar.
    if (IniGet(L"guia_visto") != L"1") {
        int seg = (int)GetPrivateProfileIntW(L"cartaz", L"segundos", 30, IniPath().c_str());
        Guia::Open(seg < 3 ? 3 : (seg > 300 ? 300 : seg));
    }

    // loop: 1 present por frame novo; se a fonte parar, re-apresenta a cada 100 ms
    std::vector<BYTE> frame; UINT32 fw = 0, fh = 0;
    UINT32 presented = 0, lastFrames = 0, lastPresented = 0;
    wchar_t dumpPath[MAX_PATH] = { 0 };
    UINT32  dumpFrame = 120;
    if (GetEnvironmentVariableW(L"DLSS5_DUMP", dumpPath, MAX_PATH)) {
        wchar_t b[16];
        if (GetEnvironmentVariableW(L"DLSS5_DUMP_FRAME", b, 16)) dumpFrame = (UINT32)_wtoi(b);
        if (!dumpFrame) dumpFrame = 1;
        Log("DUMP armado: '%s' no frame %u", U8(dumpPath).c_str(), dumpFrame);
    }
    ULONGLONG lastTitle = GetTickCount64(), lastPresent = 0;
    ULONGLONG ultimaLeituraNeural = 0;   // ver o espacamento no tique de um segundo
    ULONGLONG presentAnterior = GetTickCount64();   // diagnostico de present travado
    int       travasLogadas = 0;                    // teto de 10 linhas por sessao
    int  segundosPesado = 0; bool avisouPesado = false;   // aviso de "esta pesado", uma vez so
    ULONGLONG pediuSair = 0;   // primeira pressao do atalho de sair (confirma em 2 s)
    ULONGLONG confirmaAte = 0; // ate quando o botao "Sair" do painel fica em confirmacao
    bool avisouNeural = false; // aviso de "o neural nao esta rodando", uma vez so
    ULONGLONG verificaNeuralAte = 0; int neuralAlvo = 0;   // confere o neural depois do F6
    uint32_t  neuralEntregasAntes = 0;   // NeuralDelivered() de quando o toque foi armado
    bool      neuralAnuncia = false;     // o cartaz do resultado e do botao do card
    bool      neuralCorrigiu = false;    // ja corrigimos uma vez; nao entrar em pingue-pongue
    ULONGLONG ultimaRetomada = 0; int retomadas = 0;   // recriar a captura quando ela cai
    bool avisouQueda = false;                          // um log por queda, nao um por frame
    bool avisouDesistiu = false;                       // cartaz de "caiu e nao voltou", uma vez so
    int  monSumido = 0; bool avisouMonitor = false;    // fonte monitor: o monitor foi desligado?
    int  religouPorSolto = 0;   // quantas vezes ja religamos o neural depois de um re-arm
    // ------------------------------------------------ A CAMADA QUE TAPA A PROPRIA FONTE
    //
    // MEDIDO NESTA MAQUINA (3 execucoes, DISPLAY1 3440x1440, 19:11-19:20): com a fonte
    // MONITOR em modo sobreposto, no instante em que a camada e colada por cima do monitor
    // a captura para de entregar frames - "0.0 fps  (alvo parado)" no titulo - MESMO com
    // uma janela pintando cor nova por baixo dela a cada 700 ms.
    //
    // Nao e o click-through: o WM_NCHITTEST da camada responde HTTRANSPARENT em 0 ms
    // durante todo o congelamento (medido com SendMessageTimeout). Mouse e teclado passam
    // para o Windows normalmente o tempo todo.
    //
    // E a fonte que morre. A camada cobre 100% do monitor e sai da captura
    // (WDA_EXCLUDEFROMCAPTURE); com tudo o que esta embaixo dela totalmente ocluido, a
    // composicao daquele monitor deixa de mudar e o WGC nao dispara mais FrameArrived. A
    // camada tapa exatamente aquilo que ela precisa capturar.
    //
    // Para quem esta na frente do PC isso e indistinguivel de "o computador travou": a
    // tela e uma FOTO, o cursor continua andando por cima (o ponteiro e desenhado pelo
    // hardware, nunca congela) e todo clique e toda tecla chegam ao Windows sem que nada
    // apareca. Foi essa a queixa: "nao consigo usar o que esta na tela".
    //
    // A deteccao nao pode ser so "0 fps": um monitor genuinamente parado tambem da 0. Por
    // isso a SONDA - a camada se recolhe por 1 s e a pergunta e respondida por medida:
    // se os frames voltam sem ela, era ela; se nao voltam, a tela estava parada mesmo e
    // ninguem e incomodado. O que autoriza a sonda e a PESSOA TER MEXIDO no meio do
    // congelamento (GetLastInputInfo) - mexeu e a tela nao mudou, alguma coisa esta errada
    // -, com o pico de fps de antes da camada colar como segundo caminho. Sem esse porteiro
    // a sonda piscaria a tela de quem apenas parou de mexer no computador.
    int       segSemFrame = 0;            // segundos seguidos sem frame novo, camada colada
    ULONGLONG sondaAte = 0;               // != 0: a camada esta recolhida, medindo
    UINT32    framesNaSonda = 0;          // scap.frames quando a sonda comecou
    ULONGLONG proximaSonda = 0;           // nao sondar de novo antes disso
    ULONGLONG intervaloSonda = 20000;     // dobra a cada sonda que nao confirma (teto 5 min)
    double    fpsAntesDaCamada = 0.0;     // pico recente medido com a camada FORA da frente
    ULONGLONG ultimoInputTick = 0;        // GetLastInputInfo: a pessoa mexeu neste segundo?
    bool      recriouNoCongelamento = false;  // ja tentamos o conserto barato neste congelamento
    int       segComFrame = 0;            // segundos seguidos COM frame (a fonte voltou mesmo?)
    bool      avisouAutoTapa = false;     // o cartaz longo sai uma vez por sessao
    const ULONGLONG inicioApp = GetTickCount64();
    bool fingiuQueda = false;
    ULONGLONG ultimaQuedaFalsa = GetTickCount64();
    const int segTestaQueda = []{
        if (!DiagLigado()) return 0;
        wchar_t e[16]{};
        return GetEnvironmentVariableW(L"DLSS5_TESTA_QUEDA", e, 16) ? _wtoi(e) : 0;
    }();
    const ULONGLONG inicioMs = GetTickCount64();
    double lastPresentMs = 0.0;   // teto de fps usa relogio fino
    HANDLE waitEvt = (C.kind == SRC_CAMERA) ? cap.evt : scap.evt;
    bool lostSource = false;

    while (!g_quit) {
        // Handle nulo (a captura caiu e ainda nao voltou) tem de virar espera SEM handle:
        // MsgWaitForMultipleObjects com um handle invalido devolve WAIT_FAILED na hora e o
        // loop passa a girar em 100% de um nucleo sem esperar nada.
        if (waitEvt) MsgWaitForMultipleObjects(1, &waitEvt, FALSE, 100, QS_ALLINPUT);
        else         MsgWaitForMultipleObjects(0, nullptr, FALSE, 100, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_quit = true;
            if (msg.message == WM_HOTKEY) HandleHotkeyMsg(msg.wParam);
            // O prazo do modo interativo e renovado no RenderWndProc, que agora roda na
            // thread da propria janela - as mensagens dela nao passam mais por aqui.
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (g_quit) break;

        int cmd = g_trayCmd.exchange(0);
        if (cmd) {
            // Quanto a tecla esperou entre ser apertada e ser atendida. So loga o que
            // incomoda: abaixo de 50 ms ninguem percebe, e o log nao e lugar de ruido.
            const ULONGLONG t0 = g_hotkeyTick.exchange(0);
            if (t0) {
                const ULONGLONG esperou = GetTickCount64() - t0;
                if (esperou >= 50) Log("atalho: cmd=%d esperou %llu ms na fila do laco",
                                       cmd, (unsigned long long)esperou);
            }
        }
        if (cmd == TRAY_QUIT) { g_quit = true; break; }
        if (cmd == CMD_SAIR) {
            // Sair por tecla pede confirmacao: a combinacao pode ser apertada sem querer
            // no meio do jogo, e perder o filtro por acidente irrita mais do que ajuda.
            const ULONGLONG agora = GetTickCount64();
            if (pediuSair && agora - pediuSair <= 2000) { g_quit = true; break; }
            pediuSair = agora;
            Log("pedido de sair: primeira pressao (confirma em 2 s)");
            ShowToast(hi, C.mon, TextoAtalho(AT_SAIR) + TS(L" de novo para sair\n(ou use o botao no painel)", L" again to quit\n(or use the button in the panel)"), 2000);
            cmd = 0;
        }
        // ---- configuracao de atalhos
        if (cmd >= CMD_ATALHO_BASE && cmd < CMD_ATALHO_BASE + AT_COUNT) {
            g_hud.capturandoAtalho = cmd - CMD_ATALHO_BASE;
            g_hud.avisoAtalho.clear();
            SuspenderAtalhos();          // senao a tecla vira hotkey em vez de chegar aqui
            Log("captura de atalho iniciada para '%s' - hotkeys suspensas",
                U8(g_atalhos[g_hud.capturandoAtalho].chave).c_str());
            Hud::Refresh();
            cmd = 0;
        } else if (cmd == CMD_ATALHO_PADRAO) {
            g_hud.capturandoAtalho = -1;      // RegistrarTodosAtalhos abaixo ja re-registra tudo
            DeletePrivateProfileSection(L"atalhos");
            CarregarAtalhos();
            RegistrarTodosAtalhos();
            g_hud.avisoAtalho = T(L"atalhos de fábrica restaurados", L"default shortcuts restored");
            Hud::Refresh();
            cmd = 0;
        } else if (cmd >= CMD_ATALHO_SET_BASE) {
            const int bruto = cmd - CMD_ATALHO_SET_BASE;
            const UINT mods = (UINT)(bruto / 256) & 7;
            const UINT vk   = (UINT)(bruto % 256);
            const int acao  = g_hud.capturandoAtalho;
            g_hud.capturandoAtalho = -1;
            if (acao >= 0 && acao < AT_COUNT) {
                // ja e de outra acao nossa? avisa e nao aplica - dois donos para a mesma
                // tecla e pior do que nao ter tecla
                int conflito = -1;
                for (int i = 0; i < AT_COUNT; i++)
                    if (i != acao && g_atalhos[i].vk == vk && g_atalhos[i].mods == mods) conflito = i;
                if (conflito >= 0) {
                    g_hud.avisoAtalho = NomeTecla(mods, vk) + TS(L" já é de \"", L" already belongs to \"") + RotuloAtalho(conflito) + L"\"";
                } else {
                    const UINT mAntes = g_atalhos[acao].mods, vAntes = g_atalhos[acao].vk;
                    g_atalhos[acao].mods = mods; g_atalhos[acao].vk = vk;
                    if (RegistrarAtalho((AcaoAtalho)acao)) {
                        GravarAtalho((AcaoAtalho)acao);
                        if (acao == AT_PAINEL) g_hkPanel = TextoAtalho(AT_PAINEL);
                        g_hud.avisoAtalho.clear();
                        Log("atalho '%s' virou %s", U8(g_atalhos[acao].chave).c_str(),
                            U8(NomeTeclaCanonica(g_atalhos[acao].mods, g_atalhos[acao].vk)).c_str());
                    } else {
                        g_atalhos[acao].mods = mAntes; g_atalhos[acao].vk = vAntes;
                        RegistrarAtalho((AcaoAtalho)acao);
                        g_hud.avisoAtalho = NomeTecla(mods, vk) + TS(L" está em uso por outro programa", L" is taken by another program");
                    }
                }
            }
            RetomarAtalhos();
            Hud::Refresh();
            cmd = 0;
        }
        if (cmd == CMD_REABRIR) {
            // Relanca com a MESMA linha de comando (o ini guarda o resto das escolhas) e
            // com o NOSSO proprio caminho: o nome do exe nao e fixo - o usuario renomeia,
            // e os testes rodam como DLSS5ForAll-hud.exe. Com o nome cravado, "Reabrir"
            // fechava o app e nao abria nada.
            wchar_t meuExe[MAX_PATH]{};
            const DWORD n = GetModuleFileNameW(nullptr, meuExe, MAX_PATH);
            if (!n || n >= MAX_PATH) {
                Log("reabrir: GetModuleFileNameW falhou (n=%lu) - o app continua aberto", (unsigned long)n);
                ShowToast(hi, C.mon, T(L"NAO CONSEGUI REABRIR O APP\nfeche e abra na mao, do jeito de sempre", L"COULD NOT REOPEN THE APP\nclose it and open it by hand, the usual way"), 5000);
                cmd = 0;
            } else {
                Log("reabrindo o app a pedido do painel: %s", U8(meuExe).c_str());
                Hud::Close();
                // ShellExecuteW devolve <= 32 quando falhou (antivirus, permissao, disco).
                // Sair mesmo assim deixaria a pessoa sem app nenhum na tela.
                const HINSTANCE r = ShellExecuteW(nullptr, L"open", meuExe,
                                                  GetCommandLineArgsOnly().c_str(),
                                                  g_exeDir.c_str(), SW_SHOWNORMAL);
                if ((INT_PTR)r <= 32) {
                    Log("reabrir: ShellExecuteW devolveu %d - o app continua aberto", (int)(INT_PTR)r);
                    ShowToast(hi, C.mon, T(L"NAO CONSEGUI REABRIR O APP\nele segue aberto - feche e abra na mao", L"COULD NOT REOPEN THE APP\nit is still running - close it and open it by hand"), 5000);
                    cmd = 0;
                } else {
                    g_quit = true;
                    break;
                }
            }
        }
        if (cmd == CMD_SAIR_PAINEL) {
            // dois cliques no botao, pelo mesmo motivo da dupla pressao da tecla
            if (g_hud.confirmandoSaida) { g_quit = true; break; }
            g_hud.confirmandoSaida = true;
            confirmaAte = GetTickCount64() + 4000;
            Hud::Refresh();
            cmd = 0;
        }
        if (cmd == CMD_TUDO && verificaNeuralAte) {
            // UM PEDIDO DE CADA VEZ. O toque do neural e um pulso que o RenoDX le no
            // proximo frame dele; dois pulsos armados antes da confirmacao se cancelam e a
            // pessoa volta ao estado inicial. Vale para o F6 tanto quanto para o card.
            ShowToast(hi, C.mon, T(L"UM INSTANTE\no pedido anterior do neural ainda está sendo confirmado", L"ONE MOMENT\nthe previous neural request is still being confirmed"), 1800);
            cmd = 0;
        }
        if (cmd == CMD_TUDO) {
            // Mesmo motivo do TRAY_BYPASS: o F6 e uma decisao da pessoa sobre a camada, e o
            // aquecimento nao pode ficar mexendo no bypass por baixo dela depois disso.
            //
            // Sair do bypass ANTES de chamar o AlternarTudo nao e detalhe: ele guarda
            // "a camada estava aparecendo?" em g_antes.camada e devolve isso no toque
            // seguinte. Chamado com a camada ainda em bypass de aquecimento, ele guardaria
            // "estava escondida" e o proximo F6 devolveria o app para o bypass - de onde
            // nao sairia mais. O estado que o F6 tem de ver e o normal: filtro aparecendo.
            if (aquecendo) {
                aquecendo = false;
                bypass = false; g_trayBypass = false; g_hud.bypass = false;
                Log("aquecimento: desarmado pelo F6 (camada liberada antes de alternar)");
            }
            // antes de agir, para saber depois se o RenoDX chegou a LER o toque
            const uint32_t entregasAntes = rsbridge::NeuralDelivered();
            std::wstring t = AlternarTudo(&bypass, C.overlay);
            g_trayBypass = bypass; g_hud.bypass = bypass; g_hud.tudoDesligado = g_tudoDesligado;
            Hud::Refresh();
            // Texto vazio = a troca foi desfeita (o poke abortou) e o PokeReShade ja
            // explicou por que. Um segundo cartaz aqui seria a contradicao do R2.1.
            if (!t.empty()) ShowToast(hi, C.mon, t, 2000);
            // Confere daqui a pouco se o neural ficou como pedimos. Precisa conferir
            // porque a tecla fisica chega ao RenoDX TAMBEM por fora da nossa hotkey (ele
            // le o estado real do teclado), entao em algumas situacoes ele alterna duas
            // vezes e o resultado sai invertido - o pior desfecho possivel num app cujo
            // produto e o neural.
            if (g_antes.neural && !t.empty()) {
                neuralAlvo = g_tudoDesligado ? 0 : 1;
                verificaNeuralAte = GetTickCount64() + 2500;
                neuralEntregasAntes = entregasAntes;
                neuralAnuncia = false;   // o cartaz do "FILTRO LIGADO/DESLIGADO" ja saiu
                neuralCorrigiu = false;
                g_hud.neuralPendente = true; Hud::Refresh();
            }
            cmd = 0;
        }
        if (cmd == TRAY_INTERACT) {
            // UM SO ATALHO FAZ AS DUAS COISAS, porque elas nao servem separadas: o overlay
            // do ReShade so e util com o mouse na camada, e o mouse na camada so faz
            // sentido com o overlay aberto. Aperta: abre o ReShade E prende o mouse nele.
            // Aperta de novo: fecha o ReShade E devolve o mouse pro Windows.
            //
            // A ORDEM IMPORTA nos dois sentidos, e e a razao deste bloco existir:
            //   ao ENTRAR, o click-through sai primeiro - a tecla so chega no ReShade se a
            //     nossa janela ja estiver recebendo teclado;
            //   ao SAIR, a tecla vai primeiro - depois de devolver o mouse, nao haveria
            //     mais como fechar o overlay, e ele ficaria aberto por cima de tudo com o
            //     mouse ja pertencendo ao Windows: o pior dos dois mundos.
            const bool entrando = C.clickThrough;   // estamos SAINDO do click-through
            UINT vkRs = 0, modsRs = 0;
            const std::wstring nomeRs = TeclaOverlayReShade(&vkRs, &modsRs);
            // Com modificador (Ctrl+Home e afins) o poke nao consegue: ele sintetiza a
            // tecla nua. Ai o app faz a metade que sabe fazer e diz o que apertar.
            const bool podePoke = (vkRs != 0 && modsRs == 0);

            if (!entrando && podePoke) PokeReShade(vkRs, false, VIA_MENSAGEM);   // fecha o overlay

            C.clickThrough = !C.clickThrough;
            g_trayInteract = !C.clickThrough;
            g_hud.interativo = g_trayInteract; Hud::Refresh();
            SetClickThrough(hwnd, C.clickThrough);
            Log("sobreposto: click-through=%d (reshade: %s)", (int)C.clickThrough,
                !podePoke ? "sem tecla utilizavel no ini" : (entrando ? "abrindo" : "fechando"));

            if (entrando && podePoke) PokeReShade(vkRs, true, VIA_MENSAGEM);     // abre o overlay

            // Toda entrada no modo interativo arma a volta automatica; toda saida desarma.
            if (C.clickThrough) DesarmarVoltaInterativo(); else ArmarVoltaInterativo();
            // A tecla do overlay vem do ReShade.ini: escrever "End" aqui era mentira nesta
            // instalacao (a dela e Home) e vira mentira em qualquer uma que mude a tecla.
            const std::wstring volta = g_saidaInterativo.empty()
                ? TS(L"O botão no painel", L"The button in the panel") : g_saidaInterativo;
            std::wstring ti;
            if (podePoke) {
                ti = C.clickThrough
                    ? TS(L"RESHADE FECHADO  -  mouse e teclado voltaram pro Windows\n",
                         L"RESHADE CLOSED  -  mouse and keyboard are back with Windows\n")
                      + volta + TS(L" abre de novo", L" opens it again")
                    : TS(L"RESHADE ABERTO  -  mouse e teclado estão nele\n",
                         L"RESHADE OPEN  -  mouse and keyboard are on it\n")
                      + volta + TS(L" fecha e devolve o mouse", L" closes it and gives the mouse back");
            } else {
                // Sem tecla utilizavel no ReShade.ini o app so consegue entregar o teclado;
                // prometer que abriu seria mentira, entao ele diz o que a pessoa tem de apertar.
                ti = C.clickThrough
                    ? TS(L"MOUSE E TECLADO VOLTAM PRO JOGO\n", L"MOUSE AND KEYBOARD GO BACK TO THE GAME\n")
                      + volta + TS(L" pra mexer no ReShade de novo", L" to work on ReShade again")
                    : TS(L"TECLADO LIBERADO PRO FILTRO\n", L"KEYBOARD HANDED TO THE FILTER\n")
                      + (nomeRs.empty()
                          ? TS(L"defina a tecla do overlay no ReShade (Settings, \"Overlay key\")",
                               L"set the overlay key in ReShade (Settings, \"Overlay key\")")
                          : (TS(L"aperte ", L"press ") + nomeRs + TS(L" para abrir o ReShade", L" to open ReShade")));
            }
            // No monitor sobreposto a camada cobre a TELA INTEIRA. Fora do click-through
            // ela passa a receber todo clique, e os botoes que aparecem embaixo dela (barra
            // de tarefas, outras janelas) deixam de responder - a pessoa acha que travou.
            if (!C.clickThrough && C.kind == SRC_MONITOR)
                ti += T(L"\nA camada cobre a tela toda: enquanto isto estiver ligado, os cliques\n"
                        L"vão pro filtro e não pros botões do Windows.",
                        L"\nThe layer covers the whole screen: while this is on, clicks go to the\n"
                        L"filter and not to Windows' buttons.");
            if (!C.clickThrough)
                ti += TS(L"\nVolta sozinho se ninguém usar", L"\nIt comes back on its own if nobody uses it")
                      + (g_saidaInterativo.empty()
                        ? TS(L" — nenhuma tecla ficou livre pra devolver na mão",
                             L" — no key was left free to bring it back by hand")
                        : (L"; " + g_saidaInterativo + TS(L" devolve na hora", L" brings it back right away")));
            ShowToast(hi, C.mon, ti, C.clickThrough ? 2500 : 4500);
        } else if (cmd == TRAY_BYPASS) {
            // A ESCOLHA DA PESSOA MANDA. Se ela mexeu no filtro durante o aquecimento, o
            // automatico sai de cena: continuar decidindo por cima de quem acabou de
            // decidir e o jeito mais rapido de um recurso util virar sabotagem.
            if (aquecendo) {
                aquecendo = false;
                Log("aquecimento: desarmado - a pessoa mexeu no filtro na mao");
            }
            bypass = !bypass; g_trayBypass = bypass; g_hud.bypass = bypass;
            Log("sobreposto: bypass=%d (mostrando o %s)", (int)bypass, bypass ? "original" : "filtro");
            Hud::Refresh();
            ShowToast(hi, C.mon, T(bypass ? L"FILTRO DESLIGADO  -  imagem original" : L"FILTRO LIGADO  -  DLSS 5",
                                    bypass ? L"FILTER OFF  -  original image"        : L"FILTER ON  -  DLSS 5"), 1800);
        } else if (cmd == CMD_GUIA || cmd == TRAY_GUIA) {
            Hud::Close();          // o guia e maior: nao faz sentido os dois na tela
            Guia::Open(0);         // aberto pelo usuario: fica ate ele fechar
        } else if (cmd == TRAY_OFERTAS) {
            AbrirGrupoOfertas(hi);
        } else if (cmd == TRAY_PANEL || cmd == CMD_PANEL_TOGGLE) {
            Hud::Toggle();
        // (aqui morava o CMD_NEURAL, o botao separado do neural. O card virou um so,
        //  "DLSS 5", que liga e desliga tudo pelo CMD_TUDO - decisao do usuario.)
        } else if (cmd >= CMD_FPS_BASE && cmd < CMD_FPS_BASE + (int)(sizeof(kFpsCaps) / sizeof(kFpsCaps[0]))) {
            int i = cmd - CMD_FPS_BASE;
            C.fpsCap = kFpsCaps[i]; g_hud.fpsCap = C.fpsCap;
            lastPresentMs = 0.0;
            Log("teto de fps do filtro: %d", C.fpsCap);
            Hud::Refresh();
            ShowToast(hi, C.mon,
                      C.fpsCap ? (TS(L"Teto do filtro: ", L"Filter cap: ") + NomeFpsCap(i) + L" fps")
                               : TS(L"Teto do filtro: sem limite", L"Filter cap: no limit"), 1600);
        } else if (cmd >= CMD_WORKRES_BASE && cmd < CMD_WORKRES_BASE + (int)(sizeof(kWorkRes) / sizeof(kWorkRes[0]))) {
            const int i = cmd - CMD_WORKRES_BASE;
            g_hud.workRes = kWorkRes[i];
            IniSet(L"work_resolution", g_hud.workRes ? std::to_wstring(g_hud.workRes) : L"auto");
            // Grava o cfg JA, e nao so na proxima abertura: se o app for fechado de
            // qualquer jeito (X, bandeja, queda), a escolha continua valendo.
            int efetivo = g_hud.workRes;
            if (!efetivo) {
                const double mp = (double)R.srcW * (double)R.srcH / 1e6;
                efetivo = (mp <= 2.4) ? 100 : (mp <= 4.0) ? 77 : (mp <= 6.0) ? 67 : 58;
            }
            GravarWorkResolution(efetivo);
            Log("resolucao do DLSS: %s (efetivo %d%%)",
                g_hud.workRes ? "escolha da pessoa" : "automatico", efetivo);
            Hud::Refresh();
            // O CARTAZ TEM DE DIZER QUE NAO VALE AGORA. Quem le o dlss5-feed.cfg e o
            // add-on do feed, uma vez so, quando o ReShade sobe. Sem esta frase a pessoa
            // clica, nao ve diferenca nenhuma e conclui que o controle nao funciona.
            ShowToast(hi, C.mon,
                (g_hud.workRes ? TS(L"RESOLUÇÃO DO DLSS: ", L"DLSS RESOLUTION: ") + kWorkResNames[i]
                               : TS(L"RESOLUÇÃO DO DLSS: automática (", L"DLSS RESOLUTION: automatic (")
                                 + std::to_wstring(efetivo) + L"%)")
                + T(L"\nvale ao reabrir o app — a cadeia lê isso quando sobe",
                    L"\napplies next time you open the app — the chain reads this as it starts"), 4200);
        } else if (cmd >= CMD_IDIOMA_BASE && cmd < CMD_IDIOMA_BASE + (int)(sizeof(kLinguas) / sizeof(kLinguas[0]))) {
            // Trocar de idioma AQUI vale na hora: o painel, o guia, os cartazes, o menu da
            // bandeja e a barra de titulo montam o texto no momento em que desenham. Nada a
            // re-setar - so avisar o painel que a pintura de agora nao vale mais.
            const Lingua nova = kLinguas[cmd - CMD_IDIOMA_BASE];
            if (nova != g_lingua) {
                GravarIdioma(IniPath(), nova);
                Log("idioma: trocado para %s no painel", U8(CodigoLingua(nova)).c_str());
                Hud::InvalidarPintura();
                Hud::Refresh();
                // a barra de titulo e o menu da bandeja se remontam sozinhos: um no tique
                // de um segundo, o outro a cada abertura do menu
                ShowToast(hi, C.mon, T(L"IDIOMA: PORTUGUÊS", L"LANGUAGE: ENGLISH"), 1600);
            }
        } else if (cmd == CMD_VSYNC) {
            C.vsync = !C.vsync; R.vsync = C.vsync; g_hud.vsync = C.vsync;
            IniSet(L"vsync", C.vsync ? L"1" : L"0");
            Log("V-Sync: %d", (int)C.vsync);
            Hud::Refresh();
            ShowToast(hi, C.mon, T(C.vsync
                ? L"V-SYNC LIGADO  -  espera o vblank; teto no refresh do monitor"
                : L"V-SYNC DESLIGADO  -  sem esperar o vblank (nao rasga a imagem)",
                C.vsync
                ? L"V-SYNC ON  -  waits for the vblank; capped at the monitor refresh"
                : L"V-SYNC OFF  -  no waiting for the vblank (it does not tear)"), 2200);
        } else if (cmd == CMD_HDR) {
            C.hdrTreat = !C.hdrTreat;
            g_hud.hdrTreat = HdrSetTreat(C.hdrTreat);
            C.hdrTreat = g_hud.hdrTreat;

            HdrRefreshLine();
            Hud::Refresh();
            ShowToast(hi, C.mon, T(C.hdrTreat ? L"TRATAR HDR: LIGADO" : L"TRATAR HDR: DESLIGADO",
                                    C.hdrTreat ? L"HANDLE HDR: ON"    : L"HANDLE HDR: OFF"), 2000);
        } else if (cmd == CMD_FPSTELA) {
            C.fpsTela = !C.fpsTela;
            g_hud.fpsTela = C.fpsTela;
            IniSet(L"fps_tela", C.fpsTela ? L"1" : L"0");   // a escolha da pessoa, lembrada
            DefinirFpsNaTela(C.fpsTela);
            Hud::Refresh();
            // NAO ANUNCIAR "sumiu": quem desenha o contador e o overlay do ReShade, e o
            // unico momento em que ele COM CERTEZA releu o ShowFPS e a proxima abertura
            // (ver GravarTeclaOverlayReShade). O pedido foi feito pelos dois caminhos - o
            // arquivo e a API do proprio ReShade -, e o cartaz conta exatamente isso.
            ShowToast(hi, C.mon, T(C.fpsTela
                ? L"FPS NA TELA: LIGADO\no contador é do ReShade; se não aparecer agora, aparece na próxima abertura"
                : L"FPS NA TELA: DESLIGADO\no contador é do ReShade; se não sumir agora, some na próxima abertura",
                C.fpsTela
                ? L"FPS ON SCREEN: ON\nthe counter belongs to ReShade; if it does not show now, it shows next time you open"
                : L"FPS ON SCREEN: OFF\nthe counter belongs to ReShade; if it does not go now, it goes next time you open"), 3200);
        } else if (cmd == CMD_OFERTAS) {
            Hud::Close();
            AbrirGrupoOfertas(hi);
        } else if (cmd == CMD_CANAL) {
            // O painel fecha antes: o navegador vem para a frente e um painel topmost
            // sobrando por cima dele so atrapalharia.
            Hud::Close();
            if (!g_urlCanal.empty()) {
                Log("apoie o criador: abrindo o canal");
                ofertas::detail::OpenLink(g_urlCanal);   // valida http(s) antes de chamar o shell
            }
        }
        // FIM DA SONDA (ver "A CAMADA QUE TAPA A PROPRIA FONTE"). Tem de vir ANTES do
        // FollowTarget: quem devolve a camada para a frente e ele, na mesma volta.
        if (sondaAte && GetTickCount64() >= sondaAte) {
            const UINT32 chegou = (UINT32)scap.frames - framesNaSonda;
            const ULONGLONG agoraSonda = GetTickCount64();
            sondaAte = 0; segSemFrame = 0; recriouNoCongelamento = false;
            // DOIS, e nao um: recolher a camada JA e uma mudanca na composicao daquele
            // monitor, e so isso rende UM frame - medido, sempre exatamente um nas telas
            // paradas. Contar "chegou pelo menos um" acusaria auto-tapamento em toda sonda.
            //
            // E a sonda dura 1 s, nao 300 ms, por um motivo medido aqui: o DWM compoe um
            // monitor secundario ocioso a ~2 fps, entao meio segundo de fonte VIVA cabia
            // dentro de um unico frame e a medida dava empate com a tela morta. Com 1 s a
            // tela parada continua em 1 e qualquer fonte viva passa de 2.
            if (chegou >= 2) {
                // A fonte so voltou a andar com a camada fora da frente: era ela.
                Log("monitor sobreposto: a camada tapava a propria fonte (%u frames chegaram com ela"
                    " recolhida) - devolvendo a imagem original", (unsigned)chegou);
                bypass = true; g_trayBypass = true; g_hud.bypass = true; Hud::Refresh();
                // Cartaz longo na primeira vez (explica e ensina a saida); curto depois,
                // porque quem religa o filtro de proposito ja leu o motivo.
                ShowToast(hi, C.mon, avisouAutoTapa
                    ? T(L"A IMAGEM CONGELOU DE NOVO - filtro fora\n"
                        L"a camada tapa a fonte dela. Para filtrar um jogo, escolha Janela no seletor.",
                        L"THE IMAGE FROZE AGAIN - filter dropped\n"
                        L"the layer covers its own source. To filter a game, pick Window in the selector.")
                    : T(L"O FILTRO NÃO CONSEGUE COBRIR O MONITOR INTEIRO\n"
                        L"a camada tapa exatamente o que ela captura: o Windows para de mandar imagem\n"
                        L"nova e a tela vira uma foto. O mouse e o teclado NUNCA travaram - só a\n"
                        L"imagem parou. Devolvi o original.\n"
                        L"Para filtrar um jogo, feche e escolha \"Janela\" no seletor; a fonte Monitor\n"
                        L"funciona sem a caixa \"Sobrepor\" (janela espelho).",
                        L"THE FILTER CANNOT COVER THE WHOLE MONITOR\n"
                        L"the layer covers exactly what it captures: Windows stops sending new frames\n"
                        L"and the screen becomes a photo. Mouse and keyboard NEVER froze - only the\n"
                        L"image stopped. I put the original back.\n"
                        L"To filter a game, close this and pick \"Window\" in the selector; the Monitor\n"
                        L"source works fine without the \"Overlay\" box (mirror window)."),
                    avisouAutoTapa ? 5000 : 11000);
                avisouAutoTapa = true;
            } else {
                // Ninguem estava tapando nada: a tela esta parada de verdade. Nao incomoda
                // e espaca a proxima sonda, para nao piscar de 20 em 20 s numa tela quieta.
                proximaSonda = agoraSonda + intervaloSonda;
                if (intervaloSonda < 300000) intervaloSonda *= 2;
                Log("monitor sobreposto: sem frame novo mesmo com a camada recolhida (%u) - a tela"
                    " esta parada de verdade; proxima sonda em %llu s",
                    (unsigned)chegou, (unsigned long long)(intervaloSonda / 1000));
            }
        }
        if (C.overlay) {
            FollowTarget(hwnd, scap, overlayVisible, bypass || sondaAte != 0, !C.hideSelf);
            DevolverForegroundSeForNossaCamada(hwnd);
        }
        Hud::Tick();
        Guia::Tick();
        if (confirmaAte && GetTickCount64() > confirmaAte) { confirmaAte = 0; g_hud.confirmandoSaida = false; Hud::Refresh(); }

        // ---- aquecimento: a camada so aparece quando a cadeia entrega (ver a declaracao)
        if (aquecendo) {
            const ULONGLONG agora = GetTickCount64();
            if (agora >= proximaSondagem) {
                proximaSondagem = agora + 250;   // 4 sondagens por segundo, todas em cache
                const bool pronta = CadeiaEntregando();
                const bool estourou = agora > aquecimentoAte;
                if (pronta || estourou) {
                    aquecendo = false;
                    bypass = false; g_trayBypass = false; g_hud.bypass = false; Hud::Refresh();
                    Log("aquecimento: camada liberada apos %llu ms (%s)",
                        (unsigned long long)(agora - aquecimentoInicio),
                        pronta ? "a cadeia entregou" : "TETO DE 30 s - a cadeia nao respondeu");
                    // O cartaz do estouro nao pode parecer sucesso: a pessoa precisa saber
                    // que o que ela vai ver pode nao ter passado pela rede.
                    ShowToast(hi, C.mon, T(pronta
                        ? L"FILTRO LIGADO"
                        : L"A REDE NÃO RESPONDEU\nmostrando o filtro como está — confira o \"DLSS 5 Feed\" no ReShade",
                        pronta
                        ? L"FILTER ON"
                        : L"THE NETWORK DID NOT ANSWER\nshowing the filter as it is — check \"DLSS 5 Feed\" in ReShade"),
                        pronta ? 1800 : 6000);
                }
            }
            // Enquanto aquece, o cartaz se renova: ele tem de durar o aquecimento inteiro,
            // que nao tem prazo conhecido. A frase entre parenteses e a que responde a
            // queixa - a pessoa precisa saber que o desktop dela continua funcionando.
            // ...mas NAO por cima de um cartaz de outra pessoa. Os avisos da abertura
            // ("alguma tecla ja estava em uso", "a pasta do app e somente leitura") duram
            // 8-9 s e sao justamente os que a pessoa precisa ler; o aquecimento e o caso
            // comum e pode esperar. Depois que o meu esta na tela, ele se renova sozinho -
            // e por isso a condicao aceita "o cartaz visivel e o meu".
            if (aquecendo && agora >= proximoCartazAquec && (!ToastVisivel() || agora < meuCartazAquecAte)) {
                proximoCartazAquec = agora + 2000;
                meuCartazAquecAte  = agora + 2400;
                ShowToast(hi, C.mon,
                    T(L"INICIANDO A REDE NEURAL…\n(a tela continua sua enquanto isso)",
                      L"STARTING THE NEURAL NETWORK…\n(your screen keeps working meanwhile)"), 2400);
            }
        }

        // ---- o neural ficou como pedimos?
        if (verificaNeuralAte && GetTickCount64() > verificaNeuralAte) {
            verificaNeuralAte = 0;
            // Duas perguntas diferentes, e as duas importam:
            //   entregue - o RenoDX chegou a LER o toque? (o contador da ponte, imediato)
            //   real     - em que estado ele ficou? (o "NR toggled" do log, de ponta a ponta)
            const bool entregue = rsbridge::NeuralDelivered() > neuralEntregasAntes;
            const int  real = NeuralPeloLog();
            if (real >= 0 && real != neuralAlvo && !neuralCorrigiu) {
                // O RenoDX le a tecla fisica TAMBEM por fora da nossa hotkey, entao em
                // algumas situacoes ele alterna duas vezes e o resultado sai invertido.
                // Corrige UMA vez e volta a conferir - antes daqui saia um AlternarNeural
                // seguido de leitura imediata, que e o mesmo erro de cedo demais.
                Log("neural saiu invertido (queria %d, esta %d, entregue=%d) - corrigindo uma vez",
                    neuralAlvo, real, (int)entregue);
                neuralCorrigiu = true;
                neuralEntregasAntes = rsbridge::NeuralDelivered();
                if (AlternarNeural()) verificaNeuralAte = GetTickCount64() + 2500;
                else                  g_hud.neuralPendente = false;
            } else {
                g_hud.neuralPendente = false;
                g_hud.neural = LerEstadoNeural(&g_hud.neuralDetalhe);
                Hud::Refresh();
                const bool ligado = (g_hud.neural == NR_ATIVO || g_hud.neural == NR_INICIANDO);
                const bool bateu = (real < 0) ? (ligado == (neuralAlvo == 1)) : (real == neuralAlvo);
                Log("neural: confirmacao alvo=%d log=%d card=%d entregue=%d",
                    neuralAlvo, real, (int)g_hud.neural, (int)entregue);
                // SO AQUI SE ANUNCIA ESTADO. O cartaz do clique dizia "LIGANDO..."; este e
                // o que tem o direito de dizer "LIGADO", porque houve confirmacao.
                if (neuralAnuncia) {
                    std::wstring t = bateu
                        ? (neuralAlvo ? TS(L"NEURAL RENDERING LIGADO", L"NEURAL RENDERING ON")
                                      : TS(L"NEURAL RENDERING DESLIGADO\nresposta mais rápida, sem o realce do DLSS 5",
                                           L"NEURAL RENDERING OFF\nsnappier response, without the DLSS 5 uplift"))
                        : (entregue
                            ? TS(L"O NEURAL NÃO FICOU COMO PEDI\no add-on recebeu o toque mas o estado não mudou — abra o ReShade e confira o DLSS 5",
                                 L"THE NEURAL DID NOT END UP AS ASKED\nthe add-on got the nudge but the state did not change — open ReShade and check DLSS 5")
                            : TS(L"O ADD-ON NÃO RESPONDEU\no toque não chegou ao Neural Rendering — abra o ReShade e confira o DLSS 5",
                                 L"THE ADD-ON DID NOT ANSWER\nthe nudge never reached Neural Rendering — open ReShade and check DLSS 5"));
                    ShowToast(hi, C.mon, t, 3000);
                } else if (!bateu) {
                    // veio do F6: o cartaz do "FILTRO LIGADO/DESLIGADO" ja saiu e estaria
                    // errado quanto ao neural - so por isso este segundo aparece.
                    ShowToast(hi, C.mon, T(L"ATENÇÃO: O NEURAL NÃO ACOMPANHOU\nabra o ReShade e confira o DLSS 5", L"HEADS UP: THE NEURAL DID NOT FOLLOW\nopen ReShade and check DLSS 5"), 3500);
                }
                neuralAnuncia = false;
            }
        }

        // ---- o modo interativo tem prazo (ver ArmarVoltaInterativo)
        // A camada com o mouse cobre a tela inteira na fonte monitor: e a diferenca entre
        // um app aberto e um Windows que parou de responder. Aqui ele sempre volta sozinho.
        if (g_interAte && C.overlay && !g_clickThrough) {
            const ULONGLONG agora = GetTickCount64();
            const bool ocioso = (agora - g_interEntrada) > kInterOcioMs;
            if (ocioso || agora > g_interAte) {
                // FECHA O OVERLAY ANTES DE DEVOLVER O MOUSE, pelo mesmo motivo da saida
                // pelo atalho: depois de devolvido nao ha mais como fechar, e o overlay do
                // ReShade ficaria aberto por cima de tudo sem receber clique nenhum.
                UINT vkRs = 0, modsRs = 0;
                TeclaOverlayReShade(&vkRs, &modsRs);
                if (vkRs && !modsRs) PokeReShade(vkRs, false, VIA_MENSAGEM);
                C.clickThrough = true;
                g_trayInteract = false; g_hud.interativo = false; Hud::Refresh();
                SetClickThrough(hwnd, true);
                DesarmarVoltaInterativo();
                Log("interativo: click-through devolvido sozinho (%s)", ocioso ? "ocio" : "prazo total");
                const std::wstring volta = g_saidaInterativo.empty()
                    ? TS(L"O botão no painel", L"The button in the panel") : g_saidaInterativo;
                ShowToast(hi, C.mon,
                    TS(L"MOUSE E TECLADO VOLTARAM PRO JOGO\n"
                       L"a camada estava com eles e ninguém usava. ",
                       L"MOUSE AND KEYBOARD ARE BACK WITH THE GAME\n"
                       L"the layer had them and nobody was using it. ")
                    + volta + TS(L" devolve pro filtro.", L" hands them back to the filter."), 3500);
            } else if (C.kind == SRC_MONITOR && agora > g_interCartaz) {
                // Na fonte monitor o cartaz nao pode ser um piscar de 4 s: enquanto a tela
                // inteira estiver presa, o aviso fica na tela, renovado, com a conta
                // regressiva. E o indicador permanente que o toast sozinho nao da.
                g_interCartaz = agora + 3000;
                const unsigned seg = (unsigned)((g_interAte - agora) / 1000);
                wchar_t cb[320];
                const std::wstring comoVoltar = g_saidaInterativo.empty()
                    ? TS(L"nenhuma tecla livre pra devolver na mão", L"no key was free to bring it back by hand")
                    : (g_saidaInterativo + TS(L" devolve na hora", L" brings it back right away"));
                _snwprintf(cb, 320,
                    T(L"A CAMADA ESTÁ COM O MOUSE E O TECLADO\n"
                      L"a tela inteira é do filtro; os cliques não chegam ao Windows\n"
                      L"%s  ·  volta sozinho em %u s",
                      L"THE LAYER HAS THE MOUSE AND THE KEYBOARD\n"
                      L"the whole screen belongs to the filter; clicks do not reach Windows\n"
                      L"%s  ·  it comes back on its own in %u s"),
                    comoVoltar.c_str(), seg);
                ShowToast(hi, C.mon, cb, 3400);
            }
        }

        ToastTick();

        // Fonte de video: a heuristica de "isso tem cara de HDR" precisa ver o frame.
        // A cada ~30 apresentacoes; custa dezenas de microssegundos de CPU.
#ifdef TEM_HDR_CONTROL
        if (C.kind == SRC_CAMERA && (presented % 30) == 0) {
            std::lock_guard<std::mutex> lk(cap.mtx);
            if (!cap.buf.empty())
                g_hdrTr.FeedFrameStats(Hdr::MeasureBgra(cap.buf.data(), cap.w, cap.h, cap.stride));
        }
#endif
        HdrFeedShader(R);

        // Espera a GPU/DXGI aceitarem frame novo ANTES de pegar o frame da fonte.
        // Esperar depois (dentro do Present) faz o frame envelhecer na nossa mao.
        R.WaitForPresentSlot();

        bool got = false;
        if (C.kind == SRC_CAMERA) {
            got = cap.Grab(frame, fw, fh);
            if (got) {
                if (fw != R.srcW || fh != R.srcH) { if (!R.Resize(fw, fh, &err)) Fatal(TS(L"Falha ao redimensionar:\n", L"Resize failed:\n") + err); }
                R.Upload(frame.data(), fw, fh);
            }
        } else {
            got = scap.Tick();
            if (got) {
                if (scap.w != R.srcW || scap.h != R.srcH) {
                    R.FonteMudou(scap.w, scap.h, &err);
                    ResizeClientTo(hwnd, R.srcW, R.srcH);
                    if (!g_hud.fonteMudou) {
                        g_hud.fonteMudou = true;
                        Hud::Refresh();
                        ShowToast(hi, C.mon,
                            TS(L"O ALVO MUDOU DE TAMANHO\n"
                               L"o filtro continua na resolucao de quando o app abriu.\n"
                               L"Para ajustar, abra o painel (",
                               L"THE TARGET CHANGED SIZE\n"
                               L"the filter is still at the resolution the app opened with.\n"
                               L"To adjust it, open the panel (") + TextoAtalho(AT_PAINEL) +
                            TS(L") e clique em \"Reabrir o app\".", L") and click \"Reopen the app\"."), 7000);
                    }
                }
            }
            // Fora do 'if (got)' de proposito: quando o EnsureTex falha numa troca de
            // tamanho, o scap.srv de dentro ja foi solto e a atribuicao condicional
            // deixava R.extSrv pendurado no ponteiro morto ate o proximo frame bom.
            R.extSrv = scap.srv;
            // O WGC encerra a sessao quando o alvo recria o swapchain dele (e o que um
            // jogo faz ao mudar de resolucao). A janela continua viva, entao nao e "o
            // alvo fechou": e a nossa captura que caiu, e da para levantar de novo. Isso
            // NAO toca no backbuffer - o filtro segue no tamanho em que nasceu.
            // DLSS5_TESTA_QUEDA=<segundos>: derruba a sessao de proposito, para conferir a
            // recriacao da captura sem precisar de um jogo trocando de resolucao.
            //   > 0  uma vez, aos N segundos
            //   < 0  a cada |N| segundos, sem parar - e assim que se chega ao cartaz de
            //        "caiu e nao voltou": as retomadas se acumulam mais rapido do que os
            //        10 s de estabilidade que zeram a cota.
            // Desligado quando a variavel nao existe.
            if (segTestaQueda != 0) {
                const ULONGLONG periodo = (ULONGLONG)(segTestaQueda < 0 ? -segTestaQueda : segTestaQueda) * 1000;
                if ((segTestaQueda > 0 && !fingiuQueda && GetTickCount64() - inicioApp > periodo) ||
                    (segTestaQueda < 0 && GetTickCount64() - ultimaQuedaFalsa > periodo)) {
                    fingiuQueda = true;
                    ultimaQuedaFalsa = GetTickCount64();
                    Log("DLSS5_TESTA_QUEDA: derrubando a sessao de proposito");
                    scap.FingirQueda();
                }
            }
            if (!scap.Alive()) {
                if (!avisouQueda) {
                    avisouQueda = true;
                    Log("captura caiu: Closed do WGC=%d, janela ainda viva=%d",
                        (int)scap.SessionClosed(), (int)(C.win && IsWindow(C.win)));
                }
                // Recriar so faz sentido quando foi a SESSAO que caiu e a janela continua
                // de pe. Janela morta nao tem conserto, e tentar Start numa janela que nao
                // existe mais so gera erro a cada 1,5 s.
                // O alvo ainda existe? Numa fonte JANELA e o IsWindow; numa fonte MONITOR
                // nao ha janela nenhuma, e a condicao antiga (C.win && ...) era sempre falsa
                // - a fonte monitor simplesmente nao tinha caminho de recriacao.
                const bool alvoVivo = C.win ? (IsWindow(C.win) && !IsIconic(C.win)) : (C.mon != nullptr);
                if (scap.SessionClosed() && alvoVivo &&
                    GetTickCount64() - ultimaRetomada > 1500 && retomadas < 10) {
                    ultimaRetomada = GetTickCount64(); retomadas++;
                    // O Stop() solta o SRV e fecha o evento: os dois ponteiros que o
                    // resto do laco usa tem de sair de circulacao AGORA, nao depois de
                    // saber se o Start deu certo.
                    scap.Stop();
                    waitEvt = nullptr;
                    R.extSrv = nullptr;
                    std::wstring e2;
                    const bool ok2 = scap.Start(R.dev, C.win, C.mon, C.clientOnly, C.cursor, C.border, &e2);
                    // As duas linhas abaixo valem para os DOIS desfechos. O Start cria o
                    // evento antes de qualquer caminho de erro e deixa a struct zerada
                    // quando falha, entao: handle sempre valido (deixar o antigo era
                    // busy-loop de 100% de um nucleo) e extSrv nulo em vez de apontar
                    // para o SRV que o Stop ja liberou - o DrawAndPresent trata nulo.
                    waitEvt = scap.evt;
                    R.extSrv = scap.srv;
                    if (ok2) {
                        R.FonteMudou(scap.w, scap.h, &e2);
                        lostSource = false;
                        avisouQueda = false;
                        Log("captura recriada em %ux%u (tentativa %d)", scap.w, scap.h, retomadas);
                    } else {
                        Log("nao consegui recriar a captura: %s", U8(e2).c_str());
                    }
                } else if (!lostSource && !alvoVivo && C.win) {
                    // So aqui e "fechou de verdade". Com o handler de Closed no ar esta
                    // condicao passou a ser alcancada tambem por sessao caida com a janela
                    // viva (jogo minimizado, ou as 10 tentativas gastas) - dizer "fechada"
                    // nesses casos seria mentira na barra de titulo.
                    lostSource = true; Log("A janela capturada foi fechada");
                } else if (!avisouDesistiu && retomadas >= 10 && alvoVivo) {
                    // Gastamos as dez tentativas e o alvo continua vivo: a imagem congelou e
                    // nao volta mais sozinha. Sem cartaz, isso e uma tela parada sem
                    // explicacao - a pessoa acha que o filtro travou o jogo. O caminho de
                    // saida existe e e o mesmo do redimensionamento: reabrir o app.
                    avisouDesistiu = true;
                    g_hud.fonteMudou = true;      // faz aparecer o botao "Reabrir" no painel
                    Hud::Refresh();
                    Log("captura caiu e nao voltou depois de %d tentativas - avisando o usuario", retomadas);
                    ShowToast(hi, C.mon,
                        TS(C.win ? L"A CAPTURA DO JOGO CAIU E NÃO VOLTOU\n"
                                  : L"A CAPTURA DO MONITOR CAIU E NÃO VOLTOU\n",
                           C.win ? L"THE GAME CAPTURE DIED AND DID NOT COME BACK\n"
                                 : L"THE MONITOR CAPTURE DIED AND DID NOT COME BACK\n") +
                        TS(L"a imagem parou no último quadro. Abra o painel (",
                           L"the image stopped on the last frame. Open the panel (") + TextoAtalho(AT_PAINEL) +
                        TS(L")\ne clique em \"Reabrir o app\".", L")\nand click \"Reopen the app\"."), 9000);
                }
            } else if (retomadas && GetTickCount64() - ultimaRetomada > 10000) {
                // Ficou 10 s de pe depois de voltar: a cota de tentativas se renova. Sem
                // isto, dez trocas de resolucao ao longo de uma sessao longa gastavam o
                // orcamento e a captura nunca mais voltava.
                Log("captura estavel de novo - cota de retomadas zerada (%d usadas)", retomadas);
                retomadas = 0;
                avisouDesistiu = false;
            }
        }

        // O device do D3D11 morreu (TDR / troca de driver). Nao ha recuperacao possivel
        // aqui, entao: um cartaz que a pessoa consegue ler e uma saida limpa - o
        // encerramento normal e quem religa o HDR do monitor.
        if (R.deviceMorreu) {
            ShowToast(hi, C.mon,
                T(L"A PLACA DE VÍDEO REINICIOU\n"
                  L"o driver caiu ou foi atualizado com o app aberto.\n"
                  L"Abra o app de novo.",
                  L"THE GRAPHICS CARD RESET\n"
                  L"the driver crashed or was updated while the app was open.\n"
                  L"Open the app again."), 6000);
            for (int i = 0; i < 140 && ToastVisivel(); i++) {
                MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
                Sleep(50); ToastTick();
            }
            g_quit = true;
            break;
        }

        // Motion vectors do quadro que acabou de chegar, no bloco de video da GPU.
        // Aqui, e nao junto do Present: o Update faz a fila 3D esperar a fila de video
        // (na GPU, nao na CPU), e tudo que o app faz daqui ate o DrawAndPresent esconde
        // parte dessa espera. Depois do teste de deviceMorreu de proposito - nao vale
        // tocar num device que ja morreu.
        if (got) {
            MvHw::Ensure(R.srcW, R.srcH, R.dev);          // pega troca de resolucao
            MvHw::Frame(R.ctx, R.extSrv ? R.extSrv : R.srv);
        }

        ULONGLONG now = GetTickCount64();
        // overlay escondida: nao desenha a cada frame, mas mantem um Present lento -
        // o ReShade so compila/roda efeito em Present, e sem isso a cadeia nunca acorda
        bool hidden = C.overlay && !overlayVisible;
        // AQUECENDO NAO CONTA COMO ESCONDIDA, e este detalhe decide se o recurso funciona.
        // A camada esta fora de vista (bypass), mas o Present tem de continuar rapido: e o
        // Present que faz o ReShade compilar os efeitos e a cadeia subir - o comentario
        // abaixo diz isso. Tratada como escondida, o gap viraria 500 ms, e o aquecimento,
        // que existe para ENCURTAR a espera da pessoa, passaria a alonga-la para dezenas de
        // segundos ate estourar o teto de 30 s.
        if (aquecendo) hidden = false;
        ULONGLONG gap = hidden ? 500 : 100;
        // Teto de taxa: cada Present nosso roda Kernel + Feed + DLSS NR na GPU. Limitar os
        // nossos frames segura consumo, temperatura e ruido de cooler. NAO reduz latencia -
        // medido com a GPU livre, o teto de 60 piorou o PRESENT->TELA (17 ms contra 6,8 sem
        // teto). Ver docs\latencia-nr.md §4.2-4.3. 0 = sem teto, 1 present por frame.
        // LIMITACAO CONHECIDA (medida, nao resolvida): o teto so pode agir quando chega
        // frame novo, entao ele quantiza na taxa com que o loop consome frames. Com a
        // fonte a ~92 fps, pedir 60 entrega ~46. Tentei escolher "o frame mais proximo do
        // prazo" em vez de "o primeiro depois do prazo"; em bancada o resultado ficou
        // PIOR (teto 30 entregou 18,8 contra ~28 desta versao) e nao consegui explicar o
        // numero - o custo do Present domina o ritmo do loop e mascara o efeito. Revertido
        // de proposito: nao vale trocar um comportamento previsivel por um que eu nao sei
        // demonstrar. Para resolver de verdade e preciso medir com o Present barato.
        bool capOk = true;
        if (C.fpsCap > 0) {
            double tnow = QpcMs();
            capOk = (tnow - lastPresentMs) >= (1000.0 / (double)C.fpsCap) - 0.5;
            if (capOk && got) lastPresentMs = tnow;
        }
        if ((got && !hidden && capOk) || now - lastPresent >= gap) {
            LONGLONG p0 = g_lat.Now();
            const double tPresent0 = QpcMs();
            R.DrawAndPresent();
            const double durPresent = QpcMs() - tPresent0;
            if (got) g_lat.Present(p0, g_lat.Now(), R.sc);   // relatorio LAT no dlss5forall.log
            presented++; lastPresent = now;

            // ---- DIAGNOSTICO: o Present travou?
            // A hipotese sobre os primeiros segundos (ver AQUECIMENTO EM BYPASS) e que o
            // nosso Present fica preso atras da criacao das features NGX na mesma GPU. Esta
            // linha e o que confirma ou refuta isso na proxima sessao do usuario, sem
            // precisar de bancada: se as travas aparecerem agrupadas nos primeiros segundos,
            // a hipotese esta certa; se nao aparecerem, a causa e outra e o aquecimento e
            // paliativo.
            //
            // Mede a DURACAO do DrawAndPresent, nao o intervalo entre dois Presents: o
            // intervalo inclui espera proposital (o gap de 100/500 ms e o teto de fps) e
            // encheria o log de travas que nao existem. O intervalo entra na linha como
            // informacao, nao como criterio.
            if (durPresent > 250.0 && travasLogadas < 10) {
                travasLogadas++;
                Log("present travou %.0f ms (t+%.1f s, intervalo %llu ms)%s",
                    durPresent, (double)(now - inicioMs) / 1000.0,
                    (unsigned long long)(now - presentAnterior),
                    travasLogadas == 10 ? "  [ultima: 10 por sessao]" : "");
            }
            presentAnterior = now;

            // Diagnostico de cor: DLSS5_DUMP=<arquivo.png> grava a saida da cadeia no
            // frame DLSS5_DUMP_FRAME (padrao 120) e encerra. E o unico jeito honesto de
            // medir cor num desktop HDR - um print de tela passaria pelo tone map do DWM.
            if (dumpPath[0] && presented == dumpFrame) {
                bool ok = R.SaveBackbufferPng(dumpPath);
                Log("DUMP: frame %u -> '%s' %s", presented, U8(dumpPath).c_str(), ok ? "ok" : "FALHOU");
                g_quit = true;
            }
        }

        if (now - lastTitle >= 1000) {
            UINT32 f = (C.kind == SRC_CAMERA) ? (UINT32)cap.frames : (UINT32)scap.frames;
            // RECRIAR A CAPTURA ZERA O CONTADOR. Sem esta guarda, (f - lastFrames) da a
            // volta em UINT32 e o segundo seguinte a cada retomada aparece como
            // "4294967287.0 fps" no titulo e no painel. Pior do que feio: todo teste de
            // "esta chegando frame novo?" passa a responder que sim, justamente no
            // instante em que a resposta importa.
            if (f < lastFrames) lastFrames = 0;
            double fps = (f - lastFrames) * 1000.0 / (double)(now - lastTitle);
            double pfps = (presented - lastPresented) * 1000.0 / (double)(now - lastTitle);
            lastFrames = f; lastPresented = presented;
            // A CAMADA TAPOU A PROPRIA FONTE? (ver o bloco de comentario da declaracao)
            // So na fonte monitor sobreposta, so com a camada realmente na frente, e so
            // quando a fonte estava VIVA antes de ela colar - sem essa ultima condicao a
            // sonda piscaria a tela de quem simplesmente parou de mexer no computador.
            // O SINAL QUE MANDA E A PESSOA MEXENDO. "0 fps" sozinho nao acusa nada: um
            // desktop quieto tambem da 0, e ali a foto congelada e igual ao que deveria
            // aparecer. Agora, se ela mexeu no mouse ou no teclado e AINDA ASSIM nenhum
            // frame novo chegou, a tela deveria ter mudado e nao mudou - e isso e
            // exatamente a queixa que trouxe este codigo aqui. GetLastInputInfo ve a
            // entrada do sistema inteiro, que e o que interessa: os cliques dela estao
            // passando pela camada (HTTRANSPARENT) e chegando no Windows.
            LASTINPUTINFO li{ sizeof(li), 0 };
            const bool mexeu = GetLastInputInfo(&li) && (ULONGLONG)li.dwTime != ultimoInputTick;
            if (GetLastInputInfo(&li)) ultimoInputTick = li.dwTime;
            if (C.kind == SRC_MONITOR && C.overlay) {
                if (!overlayVisible || bypass || aquecendo) {
                    // camada fora da frente: guarda o PICO recente como referencia (um
                    // segundo quieto no meio nao pode apagar a memoria de que ela andava)
                    fpsAntesDaCamada = (fps > fpsAntesDaCamada) ? fps : fpsAntesDaCamada * 0.7;
                    segSemFrame = 0;
                } else if (!sondaAte && now >= proximaSonda) {
                    segSemFrame = (fps < 0.5) ? segSemFrame + 1 : 0;
                    // "A fonte voltou" sao TRES segundos seguidos chegando frame, nao um.
                    // A propria recriacao entrega um frame ao subir: com o teste de um
                    // segundo so, cada retomada zerava este estado e o app recriava a
                    // captura em circulo, sem nunca chegar a sonda (visto no log).
                    if (fps >= 0.5) { if (++segComFrame >= 3) { recriouNoCongelamento = false; segComFrame = 0; } }
                    else segComFrame = 0;
                    if (segSemFrame >= 5 && (mexeu || fpsAntesDaCamada >= 0.5)) {
                        if (!recriouNoCongelamento) {
                            // PRIMEIRO O CONSERTO BARATO. A sessao do WGC continua "viva"
                            // (Alive() true) mas parou de entregar, entao o caminho de
                            // recriacao que ja existe neste laco nunca dispara sozinho -
                            // ele so olha para SessionClosed. FingirQueda marca a sessao
                            // como fechada e entrega o caso a ele, com todo o tratamento
                            // de erro que ja esta escrito e testado. Se era estado ruim da
                            // captura, a imagem volta E O FILTRO CONTINUA - que e o
                            // desfecho que a pessoa quer.
                            recriouNoCongelamento = true;
                            segSemFrame = 0;
                            Log("monitor sobreposto: %d s sem frame novo (mexeu=%d, a fonte vinha a %.1f fps"
                                " antes da camada colar) - recriando a captura antes de culpar a camada",
                                5, (int)mexeu, fpsAntesDaCamada);
                            scap.FingirQueda();
                        } else {
                            // Recriar nao resolveu: agora sim, medir se e a camada.
                            framesNaSonda = (UINT32)scap.frames;
                            sondaAte = now + 1000;
                            Log("monitor sobreposto: continua sem frame novo depois de recriar a captura"
                                " - recolhendo a camada por 1 s para medir quem parou");
                        }
                    }
                }
            }
            // o painel vive fora do pipeline, entao ele so sabe do mundo o que a gente conta
            g_hud.fpsSrc = fps; g_hud.fpsPresent = pfps;
            g_hud.w = R.srcW; g_hud.h = R.srcH;
            // A LEITURA DO NEURAL NAO PRECISA SER DE SEGUNDO EM SEGUNDO COM O PAINEL
            // FECHADO. Ela mexe em arquivo (ReShade.log, ReShade.ini, dlss5-feed.log) e o
            // Hud::Refresh logo abaixo nao desenha nada quando o painel esta fechado -
            // era trabalho para ninguem ver. De cinco em cinco segundos basta.
            //
            // TRES EXCECOES, e as tres importam:
            //   painel aberto        - o card mostra o estado, tem de acompanhar;
            //   verificacao pendente - e a janela pos-toggle, que vive de leitura rapida;
            //   INICIANDO            - o prazo de 25 s e contado DENTRO do LerEstadoNeural,
            //                          e so avanca quando alguem a chama. Espacar ali faria
            //                          o "não subiu" aparecer com 30 s, ou pior.
            const bool neuralRapido = g_hud.open || verificaNeuralAte ||
                                      g_hud.neural == NR_INICIANDO;
            if (neuralRapido || now - ultimaLeituraNeural >= 5000) {
                ultimaLeituraNeural = now;
                g_hud.neural = LerEstadoNeural(&g_hud.neuralDetalhe);
                // O JOGO MEXEU NOS GRAFICOS E DERRUBOU O NEURAL: religa sozinho.
                //
                // Quando o device D3D12 rastreado morre - e e o que acontece ao trocar
                // configuracao grafica no jogo -, o add-on solta o estado do NR "for
                // re-arm" e o efeito para de ser aplicado. Ate aqui a unica saida era
                // fechar e reabrir o app. Religar e um toque na ponte, custa nada, e e
                // obviamente o que a pessoa quer: ela nao desligou nada.
                //
                // Teto de tres por sessao: se o re-arm nao pega, o problema e outro e
                // insistir viraria um pisca-pisca no lugar de um aviso.
                if (g_neuralSolto && !bypass && !verificaNeuralAte && religouPorSolto < 3) {
                    religouPorSolto++;
                    Log("neural: o add-on soltou o NR (o jogo mexeu nos graficos?) - religando sozinho (%d/3)",
                        religouPorSolto);
                    if (AlternarNeural()) {
                        neuralAlvo = 1;
                        verificaNeuralAte = GetTickCount64() + 2500;
                        neuralEntregasAntes = rsbridge::NeuralDelivered();
                        neuralAnuncia = false;
                        neuralCorrigiu = false;
                        g_hud.neuralPendente = true;
                        ShowToast(hi, C.mon,
                            T(L"O NEURAL CAIU E VOLTOU\n"
                              L"mexer nos gráficos do jogo derruba a cadeia; religado sozinho",
                              L"THE NEURAL DROPPED AND CAME BACK\n"
                              L"changing the game\x27s graphics knocks the chain down; turned back on by itself"), 3000);
                    }
                } else if (g_hud.neural == NR_ATIVO) {
                    religouPorSolto = 0;   // voltou a valer: a cota se renova
                }
            }
            Hud::Refresh();
            lastTitle = now;

            // FONTE MONITOR: nao existe janela para morrer, entao Alive() e sempre true e
            // desligar (ou desconectar) o monitor capturado congelava a imagem sem uma
            // linha de aviso - a pessoa ve o ultimo quadro parado e acha que o app travou.
            // O sinal e o GetMonitorInfoW passar a falhar: o HMONITOR deixa de ser valido.
            // Tres segundos seguidos antes de acusar, porque trocar de modo de video derruba
            // o handle por um instante e voltar dele e normal.
            if (C.kind == SRC_MONITOR && scap.targetMon) {
                MONITORINFO mi{ sizeof(mi) };
                if (GetMonitorInfoW(scap.targetMon, &mi)) { monSumido = 0; }
                else if (++monSumido >= 3 && !avisouMonitor) {
                    avisouMonitor = true;
                    lostSource = true;
                    Log("fonte monitor: o HMONITOR deixou de ser valido ha %d s - monitor desligado?", monSumido);
                    ShowToast(hi, C.mon,
                        T(L"O MONITOR CAPTURADO SUMIU\n"
                          L"ele foi desligado ou desconectado, e a imagem parou.\n"
                          L"Reabra o app e escolha a fonte de novo.",
                          L"THE CAPTURED MONITOR IS GONE\n"
                          L"it was turned off or unplugged, and the image stopped.\n"
                          L"Reopen the app and pick the source again."), 9000);
                }
            }

            // O app existe pelo neural: se ele nao subiu, a pessoa precisa saber - e uma
            // vez so, com a saida na mao. Espera 6 s para nao acusar durante a partida.
            if (!avisouNeural && now - inicioMs > 6000 &&
                (g_hud.neural == NR_DESLIGADO || g_hud.neural == NR_SEM_ADDON ||
                 g_hud.neural == NR_SEM_RTX)) {
                avisouNeural = true;
                // Caixa alta sem acento tem cara de erro de sistema, nao de recado - e
                // este e exatamente o momento em que a pessoa precisa ler com calma.
                // O caso SEM_RTX nao manda ninguem "ligar no painel": nao ha o que ligar, e
                // o app continua servindo como espelho e captura. Isso precisa estar dito.
                std::wstring t = (g_hud.neural == NR_SEM_RTX)
                    ? (TS(L"O Neural Rendering não roda nesta máquina\n",
                          L"Neural Rendering does not run on this machine\n") + g_hud.neuralDetalhe +
                       T(L"\no resto do app continua funcionando: espelho, captura e filtro",
                         L"\nthe rest of the app still works: mirror, capture and filter"))
                    : (g_hud.neural == NR_SEM_ADDON)
                    ? (TS(L"Neural Rendering não está instalado\n",
                          L"Neural Rendering is not installed\n") + g_hud.neuralDetalhe)
                    : (TS(L"O Neural Rendering está desligado\n"
                          L"é ele o motivo deste app existir — ligue no painel",
                          L"Neural Rendering is off\n"
                          L"it is the whole reason this app exists — turn it on in the panel") +
                       (TemAtalho(AT_PAINEL) ? (L" (" + TextoAtalho(AT_PAINEL) + L")") : std::wstring()));
                Log("aviso: neural nao esta ativo (%s)", U8(g_hud.neuralDetalhe).c_str());
                ShowToast(hi, C.mon, t, 7000);
            }

            // ---- "o filtro esta pesado": avisa uma vez, sozinho.
            // Vale pra qualquer fonte - a cadeia custa o mesmo venha o frame de onde vier
            // (no monitor inteiro custa mais, porque a resolucao e maior). O criterio e o
            // present ficando abaixo da metade da chegada por alguns segundos seguidos.
            // Nao conta quando ha teto de fps (ai o present esta baixo porque foi pedido),
            // nem com o filtro desligado, nem com a fonte parada.
            // DLSS5_TESTA_AVISO=1 dispara o cartaz no primeiro segundo, pra conferir o
            // texto sem precisar de um PC lento de verdade.
            static const bool forcaAviso = []{
                if (!DiagLigado()) return false;
                wchar_t e[8]{}; return GetEnvironmentVariableW(L"DLSS5_TESTA_AVISO", e, 8) && e[0] == L'1';
            }();
            // Com a camada fora de vista (jogo em segundo plano) o present cai para 2/s DE
            // PROPOSITO - so o bastante para a cadeia do ReShade nao dormir. A chegada
            // continua em 60/s, entao "present < metade da chegada" fica verdadeiro na hora
            // e o cartaz de FILTRO PESADO aparecia sozinho 5 s depois de um alt-tab,
            // acusando um problema que nao existe. Observado em runtime.
            if (hidden) segundosPesado = 0;
            if (!avisouPesado && !hidden &&
                (forcaAviso || (!bypass && C.fpsCap == 0 && fps >= 20.0 && pfps < fps * 0.5))) {
                segundosPesado++;
                if (forcaAviso || segundosPesado >= 5) {
                    avisouPesado = true;
                    // O texto antigo mandava "abra o painel e desligue o filtro" no modo
                    // janela separada - onde o card "Filtro DLSS 5" nasce DESABILITADO
                    // ("so no modo sobreposto"). A pessoa abria o painel, achava o item
                    // que o cartaz mandou procurar, e ele estava cinza - no pior momento
                    // possivel, com o jogo travando. O que funciona nos DOIS modos e o
                    // "tudo" (AlternarTudo), e so se ele tiver tecla de verdade.
                    std::wstring t = TS(L"O filtro está pesado nesta configuração\n",
                                        L"The filter is heavy in this setup\n");
                    if (C.overlay)               t += g_hkBypass + TS(L" tira o filtro na hora", L" drops the filter instantly");
                    else if (TemAtalho(AT_TUDO)) t += TextoAtalho(AT_TUDO) + TS(L" desliga o filtro na hora", L" turns the filter off instantly");
                    else                         t += TS(L"desligue o Neural Rendering no painel", L"turn Neural Rendering off in the panel");
                    t += TS(L"\nou baixe o teto de fps no painel (", L"\nor lower the fps cap in the panel (") + g_hkPanel + L")";
                    t += TS(g_hud.camera ? L"\nsem o filtro, a imagem continua chegando normal"
                                         : L"\nsem o filtro, o jogo volta ao desempenho de sempre",
                            g_hud.camera ? L"\nwithout the filter, the image keeps coming in normally"
                                         : L"\nwithout the filter, the game goes back to its usual performance");
                    Log("aviso de desempenho: chegada=%.1f present=%.1f", fps, pfps);
                    ShowToast(hi, C.mon, t, 7000);
                }
            } else if (pfps >= fps * 0.7) {
                segundosPesado = 0;
            }
            // numa fonte de tela, 0 fps so quer dizer "o alvo nao redesenhou nada" - nao e falha
            const wchar_t* warn = lostSource ? T(L"  (FONTE FECHADA)", L"  (SOURCE CLOSED)")
                                : (fps >= 1.0) ? L""
                                : (C.kind == SRC_CAMERA) ? T(L"  (SEM SINAL?)", L"  (NO SIGNAL?)")
                                                         : T(L"  (alvo parado)", L"  (target idle)");
            // _snwprintf NAO poe o terminador quando o texto nao cabe, e o titulo de uma
            // janela de jogo passa dos 512 com folga. Sem o zero na mao, SetWindowTextW le
            // alem do buffer.
            wchar_t t[512];
            _snwprintf(t, 512, T(L"%ls - %ls - %ux%u - %.1f fps%ls   |  %ls: painel   %ls: liga/desliga tudo   F11: tela cheia",
                                 L"%ls - %ls - %ux%u - %.1f fps%ls   |  %ls: panel   %ls: everything on/off   F11: fullscreen"),
                       APP_TITLE, srcName.c_str(), R.srcW, R.srcH, fps, warn,
                       TextoAtalho(AT_PAINEL).c_str(), TextoAtalho(AT_TUDO).c_str());
            t[511] = 0;
            SetWindowTextW(hwnd, t);
        }
    }

    // O F6 e uma COMPARACAO, nao uma configuracao.
    //
    // "Desliga tudo" existe para a pessoa ver o antes-e-depois; sair no meio dessa
    // comparacao nao pode deixar o Neural Rendering desligado para sempre. Ate hoje isso
    // nao aparecia porque o poke do F6 nunca funcionava de verdade (§14.11); com a ponte
    // funcionando, sair com tudo desligado grava NeuralUplift=0 no ReShade.ini e a proxima
    // abertura sobe sem neural, com o card dizendo DESLIGADO - a pessoa concluiria que o
    // app quebrou. Descoberto no primeiro teste em que o F6 realmente alternou.
    //
    // Os frames apresentados depois do toque nao sao supersticao: quem le o toque e o
    // RenoDX, no Present seguinte, e ele vive DENTRO deste processo. Sem apresentar, a
    // gente morre antes de a tecla ser lida e o religamento se perde.
    if (g_tudoDesligado && g_antes.neuralNos) {
        Log("encerrando com o neural desligado por nos - religando antes de sair");
        const uint32_t entreguesAntes = rsbridge::NeuralDelivered();
        // O laco de espera so faz sentido quando quem armou o toque foi a PONTE. Sem hook,
        // NeuralDelivered() fica em 0 para sempre e os 12 quadros viram espera pura - mais
        // de um segundo a mais para fechar o app, em cima dos quadros que o PokeReShade ja
        // apresentou por conta propria. Fechar devagar por causa de um contador que nunca
        // vai mudar e o tipo de lentidao que ninguem consegue explicar depois.
        const bool pelaPonte = rsbridge::NeuralToggleAvailable();
        if (AlternarNeural() && pelaPonte) {
            // Apresenta ATE o RenoDX ler o toque, em vez de chutar um numero de quadros.
            // NeuralDelivered() so sobe quando ele efetivamente leu, e a entrega sai no
            // primeiro Present seguinte (medido). O teto de 12 e so para o caso patologico
            // de o Present parar de acontecer - dispositivo perdido no meio do encerramento.
            int q = 0;
            for (; q < 12 && rsbridge::NeuralDelivered() == entreguesAntes; q++) PresentNow(1);
            Log("religamento: %u->%u toques entregues em %d quadro(s)",
                (unsigned)entreguesAntes, (unsigned)rsbridge::NeuralDelivered(), q);
        }
        g_antes.neuralNos = false;
    }
    Log("Encerrando (apresentados=%u)", presented);
    Guia::Close();
    Guia::Shutdown();
    Hud::Close();
    Hud::Shutdown();
    for (int id : { HK_PANEL, HK_PANEL2 }) UnregisterHotKey(nullptr, id);
    // A bandeja existe nos dois modos agora, entao ela sai daqui de fora do if.
    if (trayWnd) { TrayRemove(trayWnd); DestroyWindow(trayWnd); trayWnd = g_trayWnd = nullptr; }
    if (C.overlay)
        for (int id : { HK_QUIT, HK_INTERACT, HK_BYPASS, HK_QUIT2, HK_INTERACT2, HK_BYPASS2 }) UnregisterHotKey(nullptr, id);
    // O cartaz e usado nos DOIS modos desde os avisos novos (tecla recusada, pasta sem
    // escrita, captura perdida): destruir so no sobreposto vazava janela e fonte.
    if (g_toast) { DestroyWindow(g_toast); g_toast = nullptr; }
    if (g_toastFont) { DeleteObject(g_toastFont); g_toastFont = nullptr; }
    // A tela do grupo pode estar aberta (o painel a abre em thread propria). Fecha e espera:
    // sair com ela viva deixaria uma janela topmost orfa por um instante.
    if (g_thOfertas.joinable()) {
        if (HWND w = FindWindowW(L"DLSS5ForAllOfertas", nullptr)) PostMessageW(w, WM_CLOSE, 0, 0);
        g_thOfertas.join();
    }
    HdrShutdown();   // religa o HDR do monitor, se fomos nos que desligamos
    if (C.kind == SRC_CAMERA) {
        cap.Stop();
        // A thread continuou viva (ReadSample travado): ela ainda usa o objeto E ainda
        // esta dentro do Media Foundation. Nao destruir o Capture, e nao chamar
        // MFShutdown - as duas coisas puxariam o chao debaixo dela.
        if (cap.threadViva) { (void)capPtr.release(); mfOk = false;
            Log("captura: a thread ficou presa no MF - nao vou destruir o objeto nem chamar MFShutdown"); }
    } else scap.Stop();
    // ANTES do R.Shutdown(): o add-on guarda o ponteiro do nosso SRV e o publica em
    // todo begin_effects. Solta-lo sem avisar deixaria a ponte republicando memoria
    // morta ate o ReShade descer. O Stop() zera a ponte primeiro e so entao libera.
    MvHw::Stop();
    R.extSrv = nullptr;
    R.Shutdown();
    for (auto& d : devs) SafeRelease(d.act);
    if (mfOk) MFShutdown();
    if (gdipTok) Gdiplus::GdiplusShutdown(gdipTok);
    CoUninitialize();
    // A instancia sai do ar aqui. No encerramento anormal (Fatal, TerminateProcess, queda de
    // energia) nao ha o que fazer e nem precisa: o Windows libera o mutex junto com o
    // processo, entao nao existe o caso "o app nao abre mais porque ficou um mutex preso".
    if (g_mutexInstancia) { CloseHandle(g_mutexInstancia); g_mutexInstancia = nullptr; }
    if (g_log) { fclose(g_log); g_log = nullptr; }   // qualquer Log() depois disto seria UAF
    return 0;
}
