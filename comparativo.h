// comparativo.h -- A LINHA ARRASTAVEL do modo comparativo (Ctrl+F6).
//
// Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).
//
// PEDIDO DO DONO (25/09/2026): com o modo comparativo ligado aparece uma linha divisoria
// que a pessoa ARRASTA com o mouse para escolher onde fica a divisao; a posicao fica
// salva no ini ([comparar] divisor, 0-100, com fracao).
//
// QUEM DESENHA O QUE:
//   - a LINHA, a ALCA e os rotulos "DLSS 5" / "Original" sao desenhados DENTRO do quadro,
//     pelo add-on (src/addon/comparar_lado_a_lado.h), no mesmo finish_effects que faz a
//     divisao - por isso aparecem tambem em quem grava a tela;
//   - ESTE arquivo so PEGA O MOUSE. A pelicula deixa o clique atravessar para o jogo
//     (WS_EX_TRANSPARENT), entao ninguem nela recebe o arraste. Aqui nasce uma janela FINA
//     e ALTA, topmost, SEM click-through, parada em cima da linha e andando junto com ela.
//     Ela e quase invisivel (layered com alfa 1 - alfa 0 deixaria o clique atravessar) e
//     fica fora da captura (WDA_EXCLUDEFROMCAPTURE): a captura por monitor nunca a ve.
//   - a posicao vai ao add-on AO VIVO pelo export DLSS5Bridge_CompararAoVivo (mesmo
//     processo; o add-on aplica no proximo Present) e ao ini quando a pessoa SOLTA. Com um
//     add-on antigo, sem o export, cai para o ini a cada 120 ms durante o arraste (o add-on
//     le o ini em ate 250 ms: anda aos saltos, mas anda).
//
// POR QUE UMA JANELA E NAO UM HOOK DE MOUSE: o WH_MOUSE_LL entra na cadeia por onde passa
// todo o mouse do sistema - e o mesmo motivo que tirou o WH_KEYBOARD_LL do caminho reserva
// dos atalhos (antitrapaca le como macro). A janela so recebe o que cai EM CIMA dela.
//
// O PRECO, escrito para ninguem estranhar no log: com uma janela nossa por cima da pelicula
// o DWM compoe (a pelicula sai do iFlip) - so enquanto o modo comparativo esta ligado, que e
// ferramenta de gravacao e nao de jogo.
//
// USO (4 linhas no main.cpp - ver o relatorio):
//   #include "comparativo.h"
//   Comparativo::Iniciar(IniPath(), hwndDaPelicula, [](const char* s) { Log("%s", s); });
//   Comparativo::Ligar(g_hud.comparar);      // no handler do CMD_COMPARAR
//   Comparativo::Parar();                     // na saida
// Tudo o mais roda numa thread propria, com o proprio laco de mensagens: o arraste nao
// espera Present travado nem o laco principal.
#pragma once

#include <windows.h>
#include <string>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace Comparativo {

typedef void (*FnLog)(const char*);
typedef int (*FnAoVivo)(int ligado, float divisorPct);

namespace detalhe {

enum { kLargura = 24,        // px de "pega" em volta da linha (ela tem 4 px no quadro)
       kTimerMs = 33,        // reposiciona/mostra/esconde ~30x/s
       kIniVivoMs = 120,     // add-on sem o export: ritmo do ini durante o arraste
       kGeoMs = 1000 };      // releitura da geometria da fonte no ini

static std::wstring        g_ini;
static FnLog               g_log = nullptr;
static std::atomic<HWND>   g_tela{ nullptr };
static std::atomic<bool>   g_ligado{ false };
static std::atomic<float>  g_div{ 50.f };
static HANDLE              g_th = nullptr;
static DWORD               g_tid = 0;
static HWND                g_jan = nullptr;
static bool                g_arrastando = false;
static int                 g_pegaDx = 0;          // cursor.x - linha.x no clique (o salto zero)
static ULONGLONG           g_iniVivoEm = 0;
static bool                g_avisouSemExport = false;
static const wchar_t* const kClasse = L"DLSS5ForAllLinhaComparar";

// A GEOMETRIA DA FONTE, a mesma que o add-on usa (main.cpp, ComparadorGravarRetangulo, grava
// em [comparar]): onde a IMAGEM esta dentro do back buffer. O divisor e medido dentro dela.
struct Geo { int bbW = 0, bbH = 0, fx = 0, fy = 0, fw = 0, fh = 0, cw = 0, ch = 0; };
static Geo       g_geo;
static ULONGLONG g_geoEm = 0;

static void Logf(const char* fmt, ...) {
    if (!g_log) return;
    char b[400];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    b[sizeof(b) - 1] = 0;
    g_log(b);
}

static void LerGeo(bool forcar) {
    const ULONGLONG agora = GetTickCount64();
    if (!forcar && agora - g_geoEm < (ULONGLONG)kGeoMs) return;
    g_geoEm = agora;
    const wchar_t* i = g_ini.c_str();
    auto le = [&](const wchar_t* k) { return (int)GetPrivateProfileIntW(L"comparar", k, 0, i); };
    g_geo.bbW = le(L"bb_w");     g_geo.bbH = le(L"bb_h");
    g_geo.fx  = le(L"fonte_x");  g_geo.fy  = le(L"fonte_y");
    g_geo.fw  = le(L"fonte_w");  g_geo.fh  = le(L"fonte_h");
    g_geo.cw  = le(L"caixa_w");  g_geo.ch  = le(L"caixa_h");
}

// O RETANGULO DA IMAGEM NA TELA (coordenadas de tela, pixels fisicos). O quadro do add-on e
// apresentado esticado no cliente da pelicula; dentro dele a imagem ocupa a mesma fracao que
// ocupa no back buffer. A conta repete a do add-on (comparar_lado_a_lado.h, "A GEOMETRIA DA
// FONTE"): no espaco do finish a imagem e fonte * (back buffer / caixa).
static bool RetanguloImagem(RECT* out) {
    HWND t = g_tela.load();
    if (!t || !IsWindow(t) || !IsWindowVisible(t) || IsIconic(t)) return false;
    RECT c{};
    if (!GetClientRect(t, &c) || c.right <= 0 || c.bottom <= 0) return false;
    POINT o{ 0, 0 };
    ClientToScreen(t, &o);
    const double cliW = (double)c.right, cliH = (double)c.bottom;
    double x0 = 0, y0 = 0, x1 = 1, y1 = 1;   // fracoes do back buffer
    const Geo& g = g_geo;
    if (g.bbW > 0 && g.bbH > 0 && g.fw > 0 && g.fh > 0 && g.cw > 0 && g.ch > 0 &&
        g.cw <= g.bbW && g.ch <= g.bbH && g.fx + g.fw <= g.cw && g.fy + g.fh <= g.ch) {
        x0 = (double)g.fx / g.cw;             x1 = (double)(g.fx + g.fw) / g.cw;
        y0 = (double)g.fy / g.ch;             y1 = (double)(g.fy + g.fh) / g.ch;
    }
    out->left   = o.x + (LONG)std::lround(x0 * cliW);
    out->right  = o.x + (LONG)std::lround(x1 * cliW);
    out->top    = o.y + (LONG)std::lround(y0 * cliH);
    out->bottom = o.y + (LONG)std::lround(y1 * cliH);
    return out->right - out->left > 8 && out->bottom - out->top > 8;
}

// *carregado = o add-on esta no processo (com ou sem o export novo). Resolvido a cada uso:
// o ReShade pode descarregar e recarregar os add-ons (troca de runtime).
static FnAoVivo AoVivo(bool* carregado = nullptr) {
    HMODULE m = GetModuleHandleW(L"dlss5-bridge.addon64");
    if (carregado) *carregado = (m != nullptr);
    return m ? (FnAoVivo)(void*)GetProcAddress(m, "DLSS5Bridge_CompararAoVivo") : nullptr;
}

static void GravarDivisorIni(bool flush) {
    wchar_t b[32];
    _snwprintf(b, 32, L"%.1f", (double)g_div.load());
    b[31] = 0;
    WritePrivateProfileStringW(L"comparar", L"divisor", b, g_ini.c_str());
    if (flush) WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_ini.c_str());
}

// Manda a posicao (e, se pedido, o ligado) ao add-on. true = foi pelo export.
static bool Enviar(int ligado) {
    bool carregado = false;
    if (FnAoVivo f = AoVivo(&carregado)) { f(ligado, g_div.load()); return true; }
    // add-on ainda nao carregado (arranque): o ini ja tem o estado e ele le ao subir
    if (carregado && !g_avisouSemExport) {
        g_avisouSemExport = true;
        Logf("comparativo: o add-on nao tem DLSS5Bridge_CompararAoVivo (dlss5-bridge.addon64 antigo) - "
             "a linha anda pelo ini, aos saltos de ate 250 ms");
    }
    return false;
}

// Onde a janela de pega fica para a posicao atual; esconde quando nao ha o que mostrar.
static void Reposicionar() {
    if (!g_jan) return;
    RECT img{};
    const bool quer = g_ligado.load() && RetanguloImagem(&img);
    if (!quer) {
        if (IsWindowVisible(g_jan)) ShowWindow(g_jan, SW_HIDE);
        return;
    }
    const int w = img.right - img.left;
    int lx = img.left + (int)std::lround((double)w * g_div.load() / 100.0);
    int x = lx - kLargura / 2;
    if (x < img.left) x = img.left;
    if (x + kLargura > img.right) x = img.right - kLargura;
    RECT atual{};
    GetWindowRect(g_jan, &atual);
    const bool mover = atual.left != x || atual.top != img.top ||
                       atual.right - atual.left != kLargura || atual.bottom - atual.top != img.bottom - img.top;
    // A PELICULA TAMBEM E TOPMOST e pode ter subido por cima de nos (ela se reafirma).
    // Anda pela ordem Z acima de nos: achou a pelicula = reafirmar. Custa poucas janelas
    // (so as topmost ficam acima), e so reafirma quando precisa.
    bool reafirmar = !IsWindowVisible(g_jan);
    if (!reafirmar) {
        HWND t = g_tela.load();
        for (HWND h = GetWindow(g_jan, GW_HWNDPREV); h; h = GetWindow(h, GW_HWNDPREV))
            if (h == t) { reafirmar = true; break; }
    }
    if (mover || reafirmar)
        SetWindowPos(g_jan, HWND_TOPMOST, x, img.top, kLargura, img.bottom - img.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW | (mover ? 0 : (SWP_NOMOVE | SWP_NOSIZE)));
}

static void Arrastar(int cursorX) {
    RECT img{};
    if (!RetanguloImagem(&img)) return;
    const double w = (double)(img.right - img.left);
    double p = ((double)(cursorX - g_pegaDx) - (double)img.left) / w * 100.0;
    if (p < 0.0) p = 0.0;
    if (p > 100.0) p = 100.0;
    p = std::round(p * 10.0) / 10.0;   // decimo de ponto: e o que o ini guarda
    if ((float)p == g_div.load()) return;
    g_div.store((float)p);
    Reposicionar();
    if (!Enviar(-1)) {
        const ULONGLONG agora = GetTickCount64();
        if (agora - g_iniVivoEm >= (ULONGLONG)kIniVivoMs) { g_iniVivoEm = agora; GravarDivisorIni(true); }
    }
}

static void Soltar(const char* como) {
    if (!g_arrastando) return;
    g_arrastando = false;
    GravarDivisorIni(true);
    Logf("comparativo: linha em %.1f%% (%s) - gravada em [comparar] divisor", (double)g_div.load(), como);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:    return HTCLIENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // o jogo continua com o foco
    case WM_SETCURSOR:    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE;
    case WM_LBUTTONDOWN: {
        POINT c; GetCursorPos(&c);
        RECT img{};
        if (RetanguloImagem(&img)) {
            const int lx = img.left + (int)std::lround((double)(img.right - img.left) * g_div.load() / 100.0);
            g_pegaDx = c.x - lx;   // pegar a linha fora do centro nao a faz pular
            if (g_pegaDx > kLargura || g_pegaDx < -kLargura) g_pegaDx = 0;
        } else g_pegaDx = 0;
        g_arrastando = true;
        SetCapture(h);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_arrastando) { POINT c; GetCursorPos(&c); Arrastar(c.x); }
        return 0;
    case WM_LBUTTONUP:
        if (g_arrastando) { POINT c; GetCursorPos(&c); Arrastar(c.x); ReleaseCapture(); Soltar("arrastada"); }
        return 0;
    case WM_CAPTURECHANGED:
        Soltar("captura do mouse perdida");
        return 0;
    case WM_LBUTTONDBLCLK:
        // dois cliques: volta ao meio
        g_div.store(50.f);
        Reposicionar();
        Enviar(-1);
        g_arrastando = true; Soltar("dois cliques: de volta ao meio");
        return 0;
    case WM_TIMER:
        if (!g_arrastando) LerGeo(false);
        Reposicionar();
        return 0;
    default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static DWORD WINAPI Thread(void*) {
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_SIZEWE);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = kClasse;
    RegisterClassExW(&wc);
    g_jan = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                            kClasse, L"DLSS5 compare line", WS_POPUP,
                            0, 0, kLargura, 100, nullptr, nullptr, hi, nullptr);
    if (!g_jan) { Logf("comparativo: a janela da linha nao nasceu (erro %lu) - sem arraste", GetLastError()); return 0; }
    // alfa 1, e nao 0: janela layered com alfa zero deixa o clique atravessar
    SetLayeredWindowAttributes(g_jan, 0, 1, LWA_ALPHA);
    // fora da captura (Windows 10 2004+; antes disso a chamada falha e o alfa 1 ja a esconde)
    SetWindowDisplayAffinity(g_jan, 0x00000011 /*WDA_EXCLUDEFROMCAPTURE*/);
    SetTimer(g_jan, 1, kTimerMs, nullptr);
    LerGeo(true);
    Reposicionar();
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    KillTimer(g_jan, 1);
    DestroyWindow(g_jan);
    g_jan = nullptr;
    UnregisterClassW(kClasse, hi);
    return 0;
}

} // namespace detalhe

// Uma vez, com a pelicula ja criada. `ini` = IniPath(); `tela` = a janela VISIVEL onde o
// quadro com a divisao aparece (a pelicula). Le [comparar] ligado e divisor.
inline void Iniciar(const std::wstring& ini, HWND tela, FnLog log) {
    using namespace detalhe;
    if (g_th) { g_tela.store(tela); return; }
    g_ini = ini;
    g_log = log;
    g_tela.store(tela);
    g_ligado.store(GetPrivateProfileIntW(L"comparar", L"ligado", 0, ini.c_str()) != 0);
    wchar_t d[32]{};
    GetPrivateProfileStringW(L"comparar", L"divisor", L"50", d, 32, ini.c_str());
    float v = (float)_wtof(d);
    if (!(v >= 0.f && v <= 100.f)) v = 50.f;
    g_div.store(v);
    g_th = CreateThread(nullptr, 0, &Thread, nullptr, 0, &g_tid);
    Logf("comparativo: linha arrastavel pronta (divisor %.1f%%, modo %s)", (double)v,
         g_ligado.load() ? "LIGADO" : "desligado");
    if (g_ligado.load()) Enviar(1);
}

// Troca a janela visivel (a pelicula foi refeita, F11 etc.). Opcional.
inline void Tela(HWND tela) { detalhe::g_tela.store(tela); }

// No handler do CMD_COMPARAR, DEPOIS de gravar [comparar] ligado no ini: o add-on troca no
// proximo Present (sem esperar os 250 ms da releitura) e a janela da linha aparece/some.
inline void Ligar(bool ligado) {
    using namespace detalhe;
    g_ligado.store(ligado);
    if (ligado) LerGeo(true);
    Enviar(ligado ? 1 : 0);
    if (g_jan) PostMessageW(g_jan, WM_TIMER, 1, 0);
}

inline float Divisor() { return detalhe::g_div.load(); }

// Na saida do app.
inline void Parar() {
    using namespace detalhe;
    if (!g_th) return;
    PostThreadMessageW(g_tid, WM_QUIT, 0, 0);
    if (WaitForSingleObject(g_th, 2000) != WAIT_OBJECT_0) Logf("comparativo: a thread da linha nao saiu em 2 s");
    CloseHandle(g_th);
    g_th = nullptr; g_tid = 0;
}

} // namespace Comparativo
