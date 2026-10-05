// =====================================================================================
//  cartaz.h - O CARTAZ: o unico jeito de o app falar na tela por cima do jogo (I8, 18/09/2026)
//
//  ATE AQUI havia um cartaz de GDI puro (fonte fixa de 19 px, sem DPI, uma janela so, canto
//  inferior direito), e cada aviso novo escolhia o proprio tamanho. O dono, no teste de
//  18/09: "padroniza o tamanho das janelas das mensagens e o local aonde aparecem; isso esta
//  meio solto no app", "as mensagens podem ter a fonte maior", "serem mais legiveis".
//
//  O QUE ESTE COMPONENTE FIXA, para todo mundo que chama ShowToast:
//   - a FORMA: mesma largura maxima (kLarguraMax), altura pelo numero de linhas, mesma
//     margem, mesmo fundo (a superficie do painel, quase opaca), a barra ciano da marca e a
//     capivara; primeira linha em destaque (o que aconteceu), o resto em corpo (a acao);
//   - a LETRA: maior que a do painel (o cartaz e lido de longe, com o jogo em movimento e
//     muitas vezes na TV), peso semibold, entrelinha folgada, e um contorno escuro leve para
//     o texto nao sumir em cena clara;
//   - o LUGAR: canto superior direito por padrao ([ultima] cartaz_canto=inferior poe embaixo),
//     dentro da area do jogo quando ele comporta, senao no monitor dele; NUNCA no centro;
//   - a ESCALA: pelo DPI do monitor E pela resolucao (em 4K a 100% o texto nao encolhe);
//     [ultima] cartaz_zoom = 0.8..3.0 multiplica, como o painel_zoom;
//   - o TEMPO: o que o chamador pediu, nunca menos que da para ler sem pressa (TempoMinimo);
//   - o EMPILHAMENTO: ate kMax cartazes ao mesmo tempo, um abaixo do outro a partir do canto;
//     o mesmo texto repetido so renova o tempo; passando de kMax, o mais velho sai.
//
//  Cada cartaz e uma janela layered propria, desenhada por GDI+ com alfa por pixel (cantos
//  redondos), fora da captura de tela (o cartaz nao entra no video nem no filtro),
//  WS_EX_NOACTIVATE + WS_EX_TRANSPARENT: nunca rouba foco nem clique do jogo.
//
//  Quem chama continua sendo ShowToast(hi, mon, texto, ms) - a assinatura de sempre; ela
//  virou uma casca sobre Cartaz::Mostrar. Os TEXTOS ficam onde sempre ficaram (T(pt, en) no
//  ponto de uso, ver idioma.h) - e os que chegam de FORA do app (motivos do feeder, em
//  ingles tecnico) passam pelo catalogo de mensagens.h antes de subir aqui.
//
//  O RODAPE (X-CODIGOS-ERRO, 18/09/2026): todo cartaz de erro, queda ou limitacao traz o
//  CODIGO do catalogo e o id da ocorrencia (`D5-NAT-205 · 7K3M9Q`) pequeno e esmaecido no
//  canto inferior direito da caixa - nunca protagonista (Microsoft: "Error code:" em
//  segundo plano; NN/g: o codigo nao substitui a frase). E o que o jogador dita e o que
//  casa com a mesma linha no log. Cartaz de estado (sem codigo) nao tem rodape.
// =====================================================================================
#pragma once

namespace Cartaz {
using namespace Gdiplus;

enum Canto { CANTO_SUP_DIR = 0, CANTO_INF_DIR = 1 };
enum { kMax = 3 };
// unidades logicas (x escala): largura, margens e letras
static const REAL kLarguraMax = 470.f, kPad = 16.f, kBarra = 4.f, kIcone = 30.f, kGapIcone = 12.f;
static const REAL kMargem = 22.f, kGap = 10.f, kRaio = 12.f, kEntreTitulo = 5.f;
static const REAL kTituloPx = 21.f, kCorpoPx = 17.f, kRodapePx = 12.f, kEntreRodape = 6.f;
static const Color C_FUNDO (240,  17,  24,  39);   // #111827, quase opaco
static const Color C_BORDA (200,  55,  65,  81);
static const Color C_CIANO (255,   6, 182, 212);   // #06b6d4
static const Color C_TITULO(255, 255, 255, 255);
static const Color C_CORPO (255, 229, 231, 235);   // #e5e7eb
static const Color C_RODAPE(255, 156, 163, 175);   // #9ca3af: o codigo, esmaecido (~60 % do corpo)
static const Color C_CONTORNO(150,   0,   0,   0);

struct Um {
    HWND         wnd = nullptr;
    std::wstring texto;
    std::wstring rodape;        // o codigo do catalogo + id da ocorrencia; vazio = sem rodape
    ULONGLONG    ate = 0;       // tick em que some
    ULONGLONG    id = 0;
    HMONITOR     mon = nullptr; // o monitor pedido (ou nulo = a area do jogo)
    int          w = 0, h = 0;
    bool         status = false; // cartaz de estado: o proximo o substitui no lugar
};
static std::vector<Um> g_lista;   // do mais velho ao mais novo
static ULONGLONG g_serial = 0;
static ULONGLONG g_ultimoTopo = 0;
static Font *g_fTitulo = nullptr, *g_fCorpo = nullptr, *g_fRodape = nullptr;
static bool g_classe = false;
static HINSTANCE g_hi = nullptr;

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, m, wp, lp);
}

static void GarantirFontes() {
    if (g_fTitulo) return;
    g_fTitulo = new Font(L"Segoe UI Semibold", kTituloPx, FontStyleBold, UnitPixel);
    if (g_fTitulo->GetLastStatus() != Ok) { delete g_fTitulo; g_fTitulo = new Font(L"Segoe UI", kTituloPx, FontStyleBold, UnitPixel); }
    g_fCorpo = new Font(L"Segoe UI Semibold", kCorpoPx, FontStyleRegular, UnitPixel);
    if (g_fCorpo->GetLastStatus() != Ok) { delete g_fCorpo; g_fCorpo = new Font(L"Segoe UI", kCorpoPx, FontStyleBold, UnitPixel); }
    // o rodape e o codigo: letra pequena, regular, para ser lido so por quem procura
    g_fRodape = new Font(L"Segoe UI", kRodapePx, FontStyleRegular, UnitPixel);
    if (g_fRodape->GetLastStatus() != Ok) { delete g_fRodape; g_fRodape = new Font(FontFamily::GenericSansSerif(), kRodapePx, FontStyleRegular, UnitPixel); }
}

// Onde o cartaz mora: a area do jogo quando ele comporta a caixa, senao o monitor dele
// (ou o monitor pedido). Mesma regra de todas as nossas janelas (AreaParaJanela).
static RECT AreaDe(HMONITOR mon, int w, int h) {
    if (mon) { MONITORINFO mi{}; mi.cbSize = sizeof(mi); if (GetMonitorInfoW(mon, &mi)) return mi.rcWork; }
    return AreaParaJanela(w, h);
}
static HMONITOR MonitorDe(HMONITOR mon, const RECT& area) {
    if (mon) return mon;
    const POINT c{ area.left + (area.right - area.left) / 2, area.top + (area.bottom - area.top) / 2 };
    return MonitorFromPoint(c, MONITOR_DEFAULTTONEAREST);
}
// A ESCALA: o DPI do monitor (o Windows), a resolucao (4K a 100% nao pode encolher a letra)
// e o zoom da pessoa. Nunca abaixo de 1.
static REAL EscalaEm(HMONITOR mon) {
    REAL dpi = 1.f;
    {
        typedef HRESULT(WINAPI* PFN)(HMONITOR, int, UINT*, UINT*);
        HMODULE sh = GetModuleHandleW(L"shcore.dll");
        if (!sh) sh = LoadLibraryW(L"shcore.dll");
        PFN f = sh ? reinterpret_cast<PFN>(reinterpret_cast<void*>(GetProcAddress(sh, "GetDpiForMonitor"))) : nullptr;
        UINT dx = 96, dy = 96;
        if (f && mon && SUCCEEDED(f(mon, 0 /*MDT_EFFECTIVE_DPI*/, &dx, &dy)) && dx) dpi = (REAL)dx / 96.f;
        else { HDC dc = GetDC(nullptr); if (dc) { dpi = (REAL)GetDeviceCaps(dc, LOGPIXELSX) / 96.f; ReleaseDC(nullptr, dc); } }
    }
    REAL res = 1.f;
    {
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoW(mon, &mi)) res = (REAL)(mi.rcMonitor.bottom - mi.rcMonitor.top) / 1200.f;
    }
    REAL e = dpi > res ? dpi : res;
    const std::wstring z = IniGet(L"cartaz_zoom");
    if (!z.empty()) { const REAL k = (REAL)NumDecW(z.c_str()); if (k >= 0.8f && k <= 3.0f) e *= k; }
    if (e < 1.f) e = 1.f;
    if (e > 2.6f) e = 2.6f;
    return e;
}
static Canto CantoEscolhido() {
    static int c = -1;
    if (c < 0) c = (IniGet(L"cartaz_canto") == L"inferior") ? CANTO_INF_DIR : CANTO_SUP_DIR;
    return (Canto)c;
}
// O tempo minimo para ler sem pressa: 2 s mais 35 ms por letra, ate 9 s. O chamador manda
// quando pede mais.
static UINT TempoMinimo(const std::wstring& t, UINT ms) {
    UINT minimo = 2000 + (UINT)t.size() * 35;
    if (minimo > 9000) minimo = 9000;
    return ms > minimo ? ms : minimo;
}

// Titulo = primeira linha; corpo = o resto; rodape = o codigo (opcional, alinhado a direita).
// Mede e desenha com a MESMA funcao (g nulo mede), para a caixa e o texto nunca sairem do
// lugar um do outro. Devolve a altura logica.
static REAL Desenhar(Graphics* g, REAL escala, const std::wstring& texto, REAL larguraTexto, REAL* larguraUsada,
                     const std::wstring& rodape = L"") {
    GarantirFontes();
    std::wstring titulo = texto, corpo;
    const size_t nl = texto.find(L'\n');
    if (nl != std::wstring::npos) { titulo = texto.substr(0, nl); corpo = texto.substr(nl + 1); }
    // Medindo (g nulo) usa-se um Graphics com a MESMA escala do desenho: a quebra de linha
    // tem de sair igual nas duas passadas, senao a caixa fica curta para o texto.
    Bitmap medidor(4, 4, PixelFormat32bppPARGB);
    Graphics gm(&medidor);
    gm.ScaleTransform(escala, escala);
    Graphics* gg = g ? g : &gm;
    gg->SetTextRenderingHint(TextRenderingHintAntiAlias);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentNear);
    sf.SetTrimming(StringTrimmingWord);
    sf.SetFormatFlags(StringFormatFlagsLineLimit);
    RectF caixaT(0, 0, larguraTexto, 4000.f), medT{};
    gg->MeasureString(titulo.c_str(), -1, g_fTitulo, caixaT, &sf, &medT);
    RectF caixaC(0, 0, larguraTexto, 4000.f), medC{};
    if (!corpo.empty()) gg->MeasureString(corpo.c_str(), -1, g_fCorpo, caixaC, &sf, &medC);
    // o rodape (o codigo): uma linha so, nunca quebra
    RectF caixaR(0, 0, larguraTexto, 200.f), medR{};
    if (!rodape.empty()) gg->MeasureString(rodape.c_str(), -1, g_fRodape, caixaR, &sf, &medR);
    // entrelinha folgada: 1,25x do que o GDI+ mede
    const REAL hT = medT.Height * 1.12f, hC = corpo.empty() ? 0.f : medC.Height * 1.18f + kEntreTitulo;
    const REAL hR = rodape.empty() ? 0.f : medR.Height + kEntreRodape;
    REAL usada = medT.Width > medC.Width ? medT.Width : medC.Width;
    if (medR.Width > usada) usada = medR.Width;
    if (usada > larguraTexto) usada = larguraTexto;
    if (larguraUsada) *larguraUsada = usada;
    if (g) {
        // o contorno leve: o mesmo texto quatro vezes, deslocado 1 px, em preto translucido
        SolidBrush contorno(C_CONTORNO), bT(C_TITULO), bC(C_CORPO), bR(C_RODAPE);
        const REAL dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
        for (int i = 0; i < 4; i++) {
            g->DrawString(titulo.c_str(), -1, g_fTitulo, RectF(dx[i], dy[i], larguraTexto, hT + 2), &sf, &contorno);
            if (!corpo.empty())
                g->DrawString(corpo.c_str(), -1, g_fCorpo, RectF(dx[i], hT + kEntreTitulo + dy[i], larguraTexto, hC + 2), &sf, &contorno);
        }
        g->DrawString(titulo.c_str(), -1, g_fTitulo, RectF(0, 0, larguraTexto, hT + 2), &sf, &bT);
        if (!corpo.empty())
            g->DrawString(corpo.c_str(), -1, g_fCorpo, RectF(0, hT + kEntreTitulo, larguraTexto, hC + 2), &sf, &bC);
        if (!rodape.empty()) {
            // alinhado a direita da largura USADA pelo texto (a caixa encolhe ate ela)
            StringFormat sfR;
            sfR.SetAlignment(StringAlignmentFar);
            sfR.SetLineAlignment(StringAlignmentNear);
            sfR.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsLineLimit);
            g->DrawString(rodape.c_str(), -1, g_fRodape, RectF(0, hT + hC + kEntreRodape, usada, medR.Height + 2), &sfR, &bR);
        }
    }
    return hT + hC + hR;
}

// Pinta um cartaz inteiro (fundo, barra, capivara, texto) num DIB e devolve o tamanho em
// pixels fisicos. `bits` recebe o DIB; quem chama faz o UpdateLayeredWindow ou grava o PNG.
static HBITMAP Pintar(const std::wstring& texto, REAL escala, HDC screen, int* outW, int* outH, void** bits,
                      const std::wstring& rodape = L"") {
    const bool temMarca = IconeApp((int)(kIcone * escala + 0.5f)) != nullptr;
    const REAL esqTexto = kBarra + kPad + (temMarca ? kIcone + kGapIcone : 0.f);
    const REAL larguraTexto = kLarguraMax - esqTexto - kPad;
    REAL usada = 0.f;
    const REAL hTexto = Desenhar(nullptr, escala, texto, larguraTexto, &usada, rodape);
    REAL hL = hTexto + kPad * 2;
    if (temMarca && hL < kIcone + kPad * 2) hL = kIcone + kPad * 2;
    const REAL wL = esqTexto + usada + kPad;
    const int W = (int)(wL * escala + 0.5f), H = (int)(hL * escala + 0.5f);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
    if (!dib) return nullptr;
    {
        Bitmap bmp(W, H, W * 4, PixelFormat32bppPARGB, (BYTE*)*bits);
        Graphics g(&bmp);
        g.Clear(Color(0, 0, 0, 0));
        g.ScaleTransform(escala, escala);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        RectF caixa(0.5f, 0.5f, wL - 1.f, hL - 1.f);
        GraphicsPath p;
        {
            const REAL r = kRaio, d = r * 2;
            p.AddArc(caixa.X, caixa.Y, d, d, 180, 90);
            p.AddArc(caixa.X + caixa.Width - d, caixa.Y, d, d, 270, 90);
            p.AddArc(caixa.X + caixa.Width - d, caixa.Y + caixa.Height - d, d, d, 0, 90);
            p.AddArc(caixa.X, caixa.Y + caixa.Height - d, d, d, 90, 90);
            p.CloseFigure();
        }
        SolidBrush fundo(C_FUNDO); g.FillPath(&fundo, &p);
        Pen borda(C_BORDA, 1.f); g.DrawPath(&borda, &p);
        // a barra ciano, com o canto arredondado acompanhando a caixa
        {
            Region clip(&p); g.SetClip(&clip);
            SolidBrush ciano(C_CIANO);
            g.FillRectangle(&ciano, 0.f, 0.f, kBarra + 1.f, hL);
            g.ResetClip();
        }
        if (temMarca) {
            // A capivara como no painel (DesenharMarca): o .ico no tamanho FISICO pedido ao
            // Windows, desenhado nas unidades logicas - so reduz, nunca estica.
            HICON marca = IconeApp((int)(kIcone * escala + 0.5f));
            Bitmap* b = marca ? Bitmap::FromHICON(marca) : nullptr;
            if (b) {
                if (b->GetLastStatus() == Ok) g.DrawImage(b, RectF(kBarra + kPad, (hL - kIcone) / 2, kIcone, kIcone));
                delete b;
            }
        }
        g.TranslateTransform(esqTexto, kPad);
        Desenhar(&g, escala, texto, larguraTexto, nullptr, rodape);
    }
    if (outW) *outW = W;
    if (outH) *outH = H;
    return dib;
}

// Poe cada cartaz no lugar dele: a partir do canto, um abaixo (ou acima) do outro.
static void Arrumar() {
    const Canto canto = CantoEscolhido();
    // todos no mesmo monitor de referencia (o do mais novo com pedido, senao a area do jogo)
    HMONITOR mon = nullptr;
    int maiorW = 0, maiorH = 0;
    for (const Um& u : g_lista) { if (u.mon) mon = u.mon; if (u.w > maiorW) maiorW = u.w; if (u.h > maiorH) maiorH = u.h; }
    RECT ref = AreaDe(mon, maiorW, maiorH);
    const REAL escala = EscalaEm(MonitorDe(mon, ref));
    const int margem = (int)(kMargem * escala + 0.5f), gap = (int)(kGap * escala + 0.5f);
    int y = (canto == CANTO_SUP_DIR) ? ref.top + margem : ref.bottom - margem;
    for (Um& u : g_lista) {
        if (!u.wnd) continue;
        const int x = ref.right - margem - u.w;
        const int yy = (canto == CANTO_SUP_DIR) ? y : y - u.h;
        SetWindowPos(u.wnd, HWND_TOPMOST, x, yy, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        y = (canto == CANTO_SUP_DIR) ? yy + u.h + gap : yy - gap;
    }
}

static void Fechar(size_t i) {
    if (i >= g_lista.size()) return;
    if (g_lista[i].wnd) DestroyWindow(g_lista[i].wnd);
    g_lista.erase(g_lista.begin() + i);
}

// Pinta (ou repinta) a janela de um cartaz com o texto dele.
static void Renderizar(Um& u) {
    RECT ref = AreaDe(u.mon, 0, 0);
    const REAL escala = EscalaEm(MonitorDe(u.mon, ref));
    HDC screen = GetDC(nullptr);
    void* bits = nullptr;
    int W = 0, H = 0;
    HBITMAP dib = Pintar(u.texto, escala, screen, &W, &H, &bits, u.rodape);
    if (dib) {
        HDC mem = CreateCompatibleDC(screen);
        HGDIOBJ old = SelectObject(mem, dib);
        POINT pos{ 0, 0 }; SIZE sz{ W, H }; POINT src{ 0, 0 };
        BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(u.wnd, screen, &pos, &sz, mem, &src, 0, &bf, ULW_ALPHA);
        SelectObject(mem, old); DeleteObject(dib); DeleteDC(mem);
        u.w = W; u.h = H;
    }
    ReleaseDC(nullptr, screen);
}

// O ponto de entrada. Devolve o id do cartaz (para retira-lo antes da hora, se preciso).
//
// DOIS TIPOS, pelo tempo que o chamador pede. Ate kStatusMs e um cartaz de ESTADO - a
// contagem "ATIVANDO… 3, 2, 1", o "aquecendo… 7 s", o chip que muda a cada toque: ele
// ocupa UMA vaga e o seguinte o substitui no lugar, com o tempo pedido (o chamador esta
// sequenciando). Acima disso e um AVISO: empilha, e nunca fica menos que o tempo de ler.
static const UINT kStatusMs = 2000;
static ULONGLONG Mostrar(HINSTANCE hi, HMONITOR mon, const std::wstring& texto, UINT ms, const std::wstring& rodape = L"") {
    g_hi = hi;
    const bool status = (ms <= kStatusMs);
    if (!status) ms = TempoMinimo(texto, ms);
    // o mesmo texto ja na tela: so renova o tempo
    for (Um& u : g_lista) if (u.texto == texto) { u.ate = GetTickCount64() + ms; if (u.rodape != rodape) { u.rodape = rodape; Renderizar(u); Arrumar(); } return u.id; }
    if (status) {
        for (Um& u : g_lista) if (u.status) {
            u.texto = texto; u.rodape = rodape; u.mon = mon; u.ate = GetTickCount64() + ms; u.id = ++g_serial;
            Renderizar(u);
            Arrumar();
            return u.id;
        }
    }
    if (!g_classe) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = Proc; wc.hInstance = hi; wc.lpszClassName = L"DLSS5ForAllCartaz";
        RegisterClassExW(&wc);
        g_classe = true;
    }
    while (g_lista.size() >= (size_t)kMax) Fechar(0);   // o mais velho sai
    Um u;
    u.wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
                            L"DLSS5ForAllCartaz", L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hi, nullptr);
    if (!u.wnd) return 0;
    HideFromCapture(u.wnd);   // o cartaz nao entra no video capturado
    u.texto = texto; u.rodape = rodape; u.mon = mon; u.id = ++g_serial; u.status = status;
    u.ate = GetTickCount64() + ms;
    Renderizar(u);
    g_lista.push_back(u);
    Arrumar();
    return u.id;
}

// BUG 11: onde as outras janelas topmost do app (menu End, Guia, Filtros, Perfil) se inserem para
// NAO brigar com os cartazes. Cada uma reafirmava HWND_TOPMOST a cada 120-250 ms e o cartaz a cada
// 500 ms: quem chamava por ultimo ficava por cima, e o aviso e o menu se alternavam na frente um do
// outro, piscando. Ordem fixa agora: o cartaz mais novo no topo, as outras logo abaixo. Sem cartaz
// na tela devolve HWND_TOPMOST (o comportamento de antes).
static HWND AbaixoDosAvisos() {
    for (size_t i = g_lista.size(); i-- > 0;) if (g_lista[i].wnd) return g_lista[i].wnd;
    return HWND_TOPMOST;
}
static bool Visivel() { return !g_lista.empty(); }
static ULONGLONG AteQuando(ULONGLONG id) {
    for (const Um& u : g_lista) if (u.id == id) return u.ate;
    return 0;
}
static void Retirar(ULONGLONG id) {
    for (size_t i = 0; i < g_lista.size(); i++) if (g_lista[i].id == id) { Fechar(i); Arrumar(); return; }
}
// Retira o cartaz que ainda mostra ESTE texto (outro fica). E o que o I7 usa para sumir com a
// janelinha "ative o DLSS, o FSR ou o XeSS" quando o jogo chama o upscaler.
[[maybe_unused]] static void RetirarTexto(const std::wstring& texto) {   // (usada pelo I7 na branch principal)
    for (size_t i = 0; i < g_lista.size(); i++) if (g_lista[i].texto == texto) { Fechar(i); Arrumar(); return; }
}
static void RetirarTodos() { while (!g_lista.empty()) Fechar(0); }

// Chamado no laco (1x por volta): some quem venceu o tempo e reafirma o topo de vez em quando.
static void Tick() {
    if (g_lista.empty()) return;
    const ULONGLONG agora = GetTickCount64();
    bool mudou = false;
    for (size_t i = 0; i < g_lista.size();) {
        if (agora > g_lista[i].ate) { Fechar(i); mudou = true; }
        else i++;
    }
    if (mudou) Arrumar();
    else if (agora - g_ultimoTopo > 500) {
        g_ultimoTopo = agora;
        for (const Um& u : g_lista)
            if (u.wnd) SetWindowPos(u.wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// --cartaz-shot: o PNG pelo MESMO desenho que vai para a tela (o cartaz sai da captura de
// tela de proposito, entao um print nunca o mostra).
static bool SalvarShot(const wchar_t* caminho, const std::wstring& texto, const std::wstring& rodape = L"") {
    HDC screen = GetDC(nullptr);
    void* bits = nullptr;
    int W = 0, H = 0;
    HBITMAP dib = Pintar(texto, 1.f, screen, &W, &H, &bits, rodape);
    bool ok = false;
    if (dib) {
        GdiFlush();
        Bitmap b(W, H, W * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        CLSID png{};
        if (Hud::EncoderClsid(L"image/png", &png) >= 0) ok = (b.Save(caminho, &png, nullptr) == Ok);
        DeleteObject(dib);
    }
    ReleaseDC(nullptr, screen);
    return ok;
}

static void Shutdown() {
    RetirarTodos();
    delete g_fTitulo; g_fTitulo = nullptr;
    delete g_fCorpo;  g_fCorpo  = nullptr;
    delete g_fRodape; g_fRodape = nullptr;
}
} // namespace Cartaz
