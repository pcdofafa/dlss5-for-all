// =====================================================================================
//  menu_v8.h - RODADA 26 (v8, 25/09/2026): O MENU END NO VISUAL APROVADO PELO DONO
// =====================================================================================
//  INCLUIDO DENTRO DO namespace Hud do main.cpp, logo depois do RenderV7. Usa os mesmos
//  Item/g_items/g_hot/Activate e os mesmos comandos do laco principal: o que mudou foi o
//  DESENHO e a ORGANIZACAO (mais alguns E_* novos no fim do enum, pela regra da casa).
//
//  REFERENCIA EXATA: design-gemini/mockup-consistente.html (layout, textos, ordem,
//  comportamento) e design-gemini/DESIGN-SPEC.md (paleta, tipografia). Regras de controle,
//  sem excecao: liga/desliga = interruptor pilula com ON/OFF a esquerda; 2-4 opcoes curtas =
//  segmentado; 5+ opcoes ou nomes longos = chips; valor continuo = barra com o valor a
//  direita; acao = botao contorno ciano; principal = ciano cheio; destrutivo (Sair) =
//  contorno vermelho. Nenhum dropdown. Secoes com titulo pequeno em maiusculas e linhas com
//  divisoria fina: rotulo a esquerda, controle alinhado a direita.
//
//  LARGURA FIXA de 480 unidades (96 dpi, o DPI escala como o resto) e ALTURA = A DA ABA MAIS
//  CHEIA, igual nas quatro: o Paint mede as quatro (AlturaV8) e o rodape (presets do jogo +
//  Voltar ao padrao/Sair) fica sempre no mesmo lugar. As medidas sao mais apertadas que as
//  da especificacao (linha de 38 em vez de 40, controles de 28-30) DE PROPOSITO: o dono pediu
//  que caiba em 1080p a 100% e a 125% sem rolar, e a 125% a area util (~1020 px) da ~763
//  unidades de cartao. A rolagem da Rodada 24 continua como rede de seguranca (tarjas de aviso
//  e a janelinha podem passar disso).
//
//  O CAMINHO DE VOLTA: [ultima] menu_v7=1 devolve o menu da Rodada 25 (RenderV7), inteiro.
// =====================================================================================

static REAL LarguraTexto(const wchar_t* s, Font* f);   // definida mais abaixo, junto da dica

// ---------------------------------------------------------------- paleta (DESIGN-SPEC.md)
static const Color K8_CARTAO (250,  30,  30,  30);   // superficie #1E1E1E (quase solida)
static const Color K8_BORDA  (255,  51,  51,  51);   // #333333 - bordas e divisorias
static const Color K8_TXT    (255, 255, 255, 255);   // texto primario
static const Color K8_SEC    (255, 160, 160, 160);   // #A0A0A0 - texto secundario
static const Color K8_ACC    (255,   0, 229, 255);   // #00E5FF - destaque
static const Color K8_ACCH   (255,  51, 234, 255);   // #33EAFF - destaque sob o mouse
static const Color K8_AVISO  (255, 255, 171,   0);   // #FFAB00
static const Color K8_PERIGO (255, 255,  61,   0);   // #FF3D00 - Sair
static const Color K8_CHIP   (255,  42,  42,  42);   // #2A2A2A
static const Color K8_CHIPH  (255,  58,  58,  58);   // #3A3A3A
static const Color K8_INPUT  (255,  26,  26,  26);   // #1A1A1A - fundo do segmentado
static const Color K8_PRETO  (255,   0,   0,   0);
static const Color K8_BOLINHA(255, 136, 136, 136);   // o punho do interruptor desligado
static Color Alfa8(const Color& c, BYTE a) { return Color((BYTE)((int)c.GetA() * (int)a / 255), c.GetR(), c.GetG(), c.GetB()); }

// ---------------------------------------------------------------- medidas
static const REAL kHdr8 = 88.f, kAbas8 = 40.f, kLinha8 = 36.f, kLinhaSub8 = 44.f, kCtrl8 = 28.f;
static const REAL kRodPreset8 = 46.f, kRodBotoes8 = 50.f, kRod8 = kRodPreset8 + kRodBotoes8;
static const REAL kChave8 = 74.f;   // "ON"/"OFF" (26) + respiro (8) + trilho (40)

// ---------------------------------------------------------------- fontes (Segoe UI Variable, com reserva)
static Font *f8Titulo = nullptr, *f8Gpu = nullptr, *f8Jogo = nullptr, *f8Placar = nullptr, *f8PlacarUn = nullptr,
            *f8Aba = nullptr, *f8Secao = nullptr, *f8Rot = nullptr, *f8Ctrl = nullptr, *f8CtrlB = nullptr,
            *f8Sub = nullptr, *f8OnOff = nullptr, *f8Btn = nullptr, *f8TitJan = nullptr;
static Font* MkFont8(const wchar_t* fam, const wchar_t* reserva, REAL px, INT style) {
    Font* f = new Font(fam, px, style, UnitPixel);
    if (f->GetLastStatus() != Ok) { delete f; f = new Font(reserva, px, style, UnitPixel); }
    if (f->GetLastStatus() != Ok) { delete f; f = new Font(L"Segoe UI", px, style, UnitPixel); }
    return f;
}
static void EnsureFonts8() {
    if (f8Titulo) return;
    f8Titulo   = MkFont8(L"Segoe UI Variable Display",       L"Segoe UI",          24.f, FontStyleBold);
    f8TitJan   = MkFont8(L"Segoe UI Variable Display",       L"Segoe UI",          18.f, FontStyleBold);
    f8Placar   = MkFont8(L"Segoe UI Variable Display Semibold", L"Segoe UI Semibold", 20.f, FontStyleRegular);
    f8PlacarUn = MkFont8(L"Segoe UI Variable Text",          L"Segoe UI",          14.f, FontStyleRegular);
    f8Gpu      = MkFont8(L"Segoe UI Variable Text",          L"Segoe UI",          15.f, FontStyleRegular);
    f8Jogo     = MkFont8(L"Segoe UI Variable Text",          L"Segoe UI",          12.f, FontStyleRegular);
    f8Aba      = MkFont8(L"Segoe UI Variable Text Semibold", L"Segoe UI Semibold", 14.f, FontStyleRegular);
    f8Secao    = MkFont8(L"Segoe UI Variable Text",          L"Segoe UI",          12.f, FontStyleBold);
    f8Rot      = MkFont8(L"Segoe UI Variable Text",          L"Segoe UI",          15.f, FontStyleRegular);
    f8Ctrl     = MkFont8(L"Segoe UI Variable Text",          L"Segoe UI",          14.f, FontStyleRegular);
    f8CtrlB    = MkFont8(L"Segoe UI Variable Text Semibold", L"Segoe UI Semibold", 14.f, FontStyleRegular);
    f8Sub      = MkFont8(L"Segoe UI Variable Text",          L"Segoe UI",          12.f, FontStyleRegular);
    f8OnOff    = MkFont8(L"Segoe UI Variable Text Semibold", L"Segoe UI Semibold", 12.f, FontStyleRegular);
    f8Btn      = MkFont8(L"Segoe UI Variable Text Semibold", L"Segoe UI Semibold", 14.f, FontStyleRegular);
}
// A LOGO (a capivara) do cabecalho: o recurso de icone 1 do proprio exe (IconeApp), pedido no
// tamanho FISICO certo (o .ico tem nove tamanhos: o Windows escolhe a imagem, nao estica) e
// convertido para ARGB com o alfa de verdade - o Bitmap::FromHICON do GDI+ perde o canal alfa dos
// icones de 32 bits. Nenhum arquivo solto na pasta. Guardado ate o tamanho mudar (DPI).
static Bitmap* g_logo8 = nullptr;
static int     g_logo8Px = 0;
static Bitmap* LogoApp8(int px) {
    if (g_logo8 && g_logo8Px == px) return g_logo8;
    delete g_logo8; g_logo8 = nullptr; g_logo8Px = px;
    HICON ic = IconeApp(px);
    if (!ic) return nullptr;
    ICONINFO ii{};
    if (!GetIconInfo(ic, &ii)) return nullptr;
    BITMAP bm{};
    if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
        const int w = bm.bmWidth, h = bm.bmHeight;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        std::vector<BYTE> px4((size_t)w * h * 4);
        HDC dc = GetDC(nullptr);
        const int linhas = GetDIBits(dc, ii.hbmColor, 0, (UINT)h, px4.data(), &bi, DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        if (linhas == h) {
            bool temAlfa = false;
            for (size_t i = 3; i < px4.size(); i += 4) if (px4[i]) { temAlfa = true; break; }
            if (!temAlfa) for (size_t i = 3; i < px4.size(); i += 4) px4[i] = 255;   // icone sem alfa: opaco
            g_logo8 = new Bitmap(w, h, PixelFormat32bppARGB);
            BitmapData bd{};
            Rect rr(0, 0, w, h);
            if (g_logo8->LockBits(&rr, ImageLockModeWrite, PixelFormat32bppARGB, &bd) == Ok) {
                for (int y = 0; y < h; y++) memcpy((BYTE*)bd.Scan0 + (size_t)y * bd.Stride, px4.data() + (size_t)y * w * 4, (size_t)w * 4);
                g_logo8->UnlockBits(&bd);
            }
        }
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return g_logo8;
}
static void DesenharLogo8(Graphics* g, REAL x, REAL y, REAL tam) {
    if (!g) return;
    int fis = (int)(tam * g_scale + 0.5f);
    if (fis < 16) fis = 16;
    if (fis > 256) fis = 256;
    Bitmap* b = LogoApp8(fis);
    const RectF r(x, y, tam, tam);
    GraphicsPath p; RoundPath(p, r, 8.f);
    if (!b) { FillRound(g, r, 8.f, K8_CHIP); return; }
    g->SetClip(&p);
    const InterpolationMode im = g->GetInterpolationMode();
    g->SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g->DrawImage(b, r);
    g->SetInterpolationMode(im);
    g->ResetClip();
}
static void SoltarFonts8() {
    delete g_logo8; g_logo8 = nullptr; g_logo8Px = 0;
    Font* fs[] = { f8Titulo, f8Gpu, f8Jogo, f8Placar, f8PlacarUn, f8Aba, f8Secao, f8Rot, f8Ctrl, f8CtrlB, f8Sub, f8OnOff, f8Btn, f8TitJan };
    for (Font* f : fs) delete f;
    f8Titulo = f8Gpu = f8Jogo = f8Placar = f8PlacarUn = f8Aba = f8Secao = f8Rot = f8Ctrl = f8CtrlB = f8Sub = f8OnOff = f8Btn = f8TitJan = nullptr;
}
// Largura de texto com memoria: o menu mede as quatro abas a cada mudanca visivel (AlturaV8) e
// quase todo texto e o mesmo de uma vez para a outra.
static REAL Larg8(const std::wstring& s, Font* f) {
    static std::map<std::pair<const void*, std::wstring>, REAL> cache;
    const auto k = std::make_pair((const void*)f, s);
    const auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    const REAL w = LarguraTexto(s.c_str(), f);
    if (cache.size() > 4000) cache.clear();
    cache[k] = w;
    return w;
}

// ---------------------------------------------------------------- o que o menu le de arquivo, com memoria
// O Paint do v8 mede as QUATRO abas a cada mudanca visivel, e cada IniGet e uma leitura do ini (o
// de uma bancada tinha 1,7 MB). Estas chaves so mudam por clique - e todo clique termina num
// Refresh(), que zera a memoria -, entao uma leitura por pintura basta.
struct Ini8 {
    bool fgJogo = false, transporte = false, reshade = false, perfilReabre = true, motorProprio = false;
    bool recursosExp = false, renoDxNovo = false, feederRedeReduzida = false, desempMax = true;
    std::wstring fluidezModo;
    bool ok = false;
};
static Ini8 g_ini8;
static void IniV8Esquecer() { g_ini8.ok = false; }
static const Ini8& IniV8() {
    if (!g_ini8.ok) {
        g_ini8.fgJogo       = MostrarFgDoJogo();
        g_ini8.transporte   = IniGet(L"mostrar_transporte") == L"1";
        g_ini8.reshade      = MostrarReShade();
        g_ini8.perfilReabre = IniGet(L"perfil_reabre") != L"0";
        g_ini8.motorProprio = MotorProprio();
        g_ini8.recursosExp  = RecursosExperimentais();
        g_ini8.renoDxNovo   = RenoDxNovo();
        g_ini8.feederRedeReduzida = FeederTemRedeReduzida();
        g_ini8.desempMax    = Desempenho::Max::ChaveLigada();
        g_ini8.fluidezModo  = IniGet(L"fluidez_modo");
        g_ini8.ok = true;
    }
    return g_ini8;
}

// ---------------------------------------------------------------- estado do v8
static int  g_mainNat8 = 0;      // altura natural do cartao principal NA aba desenhada agora
static int  g_mainH8   = 0;      // altura do cartao principal = a da aba mais cheia (AlturaV8)
static int  g_popH8    = 0;      // altura da janelinha ao lado (0 = fechada)
static int  g_presetSel = 0;     // o chip de preset escolhido no rodape (1..3; 0 = ainda nao iniciado)
static std::wstring g_aviso8;    // o aviso rapido (toast) do painel, mesmo estilo em todo o app
static ULONGLONG    g_aviso8Ate = 0;
static int  g_tetoArrasto = -1;  // o valor vivo da barra "fps escolhido" enquanto arrasta (-1 = parado)
enum { kTimerAviso8 = 97 };

// O valor que o ponto do mouse representa na barra do "fps escolhido": 10..240, de 5 em 5 (o
// mesmo intervalo e o mesmo passo do [-]/[+] antigo). Mesma regra de escala do ValorDaBarra.
static int ValorDoTeto8(const RectF& r, LONG xFisico, REAL escala) {
    const REAL x = (REAL)xFisico / (escala > 0.f ? escala : 1.f);
    REAL f = (r.Width > 1.f) ? (x - r.X) / r.Width : 0.f;
    if (f < 0.f) f = 0.f;
    if (f > 1.f) f = 1.f;
    int v = kTetoMin + (int)(f * (REAL)(240 - kTetoMin) + 0.5f);
    v = ((v + 2) / 5) * 5;
    return v < kTetoMin ? kTetoMin : (v > 240 ? 240 : v);
}
// Uma opcao de segmentado ou de chip.
struct Opc8 {
    std::wstring rot; int kind; int val; bool sel; bool en; std::wstring tip; bool ponto; bool apagado;
    Opc8(const std::wstring& r, int k, int v, bool s, bool e, const std::wstring& t = std::wstring(),
         bool p = false, bool a = false)
        : rot(r), kind(k), val(v), sel(s), en(e), tip(t), ponto(p), apagado(a) {}
};

// ---------------------------------------------------------------- pecas de desenho
// Titulo de secao: pequeno, em maiusculas, com +1 de espaco entre as letras (o letter-spacing do
// mockup) e a divisoria embaixo. Letra por letra com a medida TIPOGRAFICA: o MeasureString comum
// soma um respiro dos dois lados de cada letra, e com ele o titulo saia "N E U R A L".
static void V8Espacado(Graphics* g, const wchar_t* s, Font* f, const Color& c, REAL x, REAL y, REAL h, REAL extra) {
    StringFormat sf(StringFormat::GenericTypographic());
    sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces | StringFormatFlagsNoWrap);
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush b(c);
    REAL cx = x;
    for (const wchar_t* p = s; *p; p++) {
        g->DrawString(p, 1, f, RectF(cx, y, 60.f, h), &sf, &b);
        RectF med{};
        g->MeasureString(p, 1, f, PointF(0.f, 0.f), &sf, &med);
        cx += med.Width + extra;
    }
}
static void V8Secao(Graphics* g, REAL L, REAL IW, REAL& cy, const wchar_t* t) {
    if (g) {
        V8Espacado(g, t, f8Secao, K8_SEC, L, cy, 18.f, 1.f);
        Pen p(K8_BORDA, 1.f); g->DrawLine(&p, L, cy + 24.f, L + IW, cy + 24.f);
    }
    cy += 25.f;
}
static void V8Div(Graphics* g, REAL L, REAL IW, REAL y) {
    if (g) { Pen p(K8_BORDA, 1.f); g->DrawLine(&p, L, y, L + IW, y); }
}
// Rotulo da linha: titulo (14) e, se houver, a linha secundaria (12) embaixo.
static void V8Rotulo(Graphics* g, REAL x, REAL y, REAL w, REAL h, const std::wstring& tit, const std::wstring& sub,
                     bool en = true, const Color* corSub = nullptr) {
    if (!g) return;
    const Color ct = en ? K8_TXT : Alfa8(K8_SEC, 170);
    if (sub.empty()) { Txt(g, tit.c_str(), f8Rot, ct, RectF(x, y, w, h), 0, true); return; }
    const REAL y0 = y + (h - 36.f) / 2.f;
    Txt(g, tit.c_str(), f8Rot, ct, RectF(x, y0, w, 19.f), 0, true);
    Txt(g, sub.c_str(), f8Sub, corSub ? *corSub : K8_SEC, RectF(x, y0 + 19.f, w, 17.f), 0, true);
}
// Interruptor pilula: "ON"/"OFF" a esquerda, trilho 40x20, punho 16. x = inicio do bloco de 74.
static void V8Chave(Graphics* g, REAL x, REAL yMeio, bool on, bool en, bool hot) {
    if (!g) return;
    const BYTE a = en ? 255 : 110;
    Txt(g, on ? L"ON" : L"OFF", f8OnOff, Alfa8(on ? K8_ACC : K8_SEC, a), RectF(x, yMeio - 9.f, 26.f, 18.f), 2);
    const RectF tr(x + 34.f, yMeio - 10.f, 40.f, 20.f);
    FillRound(g, tr, 10.f, Alfa8(on ? K8_ACC : K8_BORDA, a));
    const REAL bx = on ? tr.X + 22.f : tr.X + 2.f;
    SolidBrush b(Alfa8(on ? K8_TXT : K8_BOLINHA, a));
    g->FillEllipse(&b, bx, tr.Y + 2.f, 16.f, 16.f);
    if (hot && en) { Pen p(Color(200, 255, 255, 255), 1.f); g->DrawEllipse(&p, bx, tr.Y + 2.f, 16.f, 16.f); }
}
enum { B8_CONTORNO = 0, B8_CHEIO, B8_PERIGO };
static REAL V8BotaoW(const std::wstring& t) { return Larg8(t, f8Btn) + 28.f; }
static void V8Botao(Graphics* g, const RectF& r, const std::wstring& t, int estilo, bool en, bool hot) {
    if (!g) return;
    const BYTE a = en ? 255 : 128;
    if (estilo == B8_CHEIO) {
        if (en) FillRound(g, r, 4.f, hot ? K8_ACCH : K8_ACC);
        else    FillRound(g, r, 4.f, K8_CHIP);
        Txt(g, t.c_str(), f8Btn, en ? K8_PRETO : K8_SEC, r, 1, true);
        return;
    }
    const Color base = (estilo == B8_PERIGO) ? K8_PERIGO : K8_ACC;
    if (hot && en) FillRound(g, r, 4.f, Alfa8(base, 26));
    StrokeRound(g, RectF(r.X + .5f, r.Y + .5f, r.Width - 1.f, r.Height - 1.f), 4.f, Alfa8(base, a), 1.f);
    Txt(g, t.c_str(), f8Btn, Alfa8(base, a), r, 1, true);
}
// Um botao que empilha o proprio Item (o jeito comum de usar V8Botao).
static void V8BotaoItem(Graphics* g, const RectF& r, const std::wstring& t, int estilo, int kind, int val, bool en,
                        const std::wstring& tip = std::wstring()) {
    const int idx = (int)g_items.size();
    g_items.push_back(Item{ r, kind, val, en, false, tip });
    V8Botao(g, r, t, estilo, en, g_hot == idx);
}
// Barra de valor continuo: trilho 6, preenchimento ciano, punho 14. r = a faixa do trilho.
static void V8Barra(Graphics* g, const RectF& r, REAL f, bool en, bool hot) {
    if (!g) return;
    if (f < 0.f) f = 0.f;
    if (f > 1.f) f = 1.f;
    const BYTE a = en ? 255 : 110;
    const REAL yc = r.Y + r.Height / 2.f;
    const RectF tr(r.X, yc - 3.f, r.Width, 6.f);
    FillRound(g, tr, 3.f, Alfa8(K8_BORDA, a));
    if (f > 0.f) FillRound(g, RectF(tr.X, tr.Y, tr.Width * f > 6.f ? tr.Width * f : 6.f, 6.f), 3.f, Alfa8(K8_ACC, a));
    const REAL kx = r.X + r.Width * f;
    SolidBrush kb(Alfa8(hot && en ? K8_ACCH : K8_ACC, a));
    g->FillEllipse(&kb, kx - 7.f, yc - 7.f, 14.f, 14.f);
    Pen kp(Color(a, 12, 12, 12), 2.f);
    g->DrawEllipse(&kp, kx - 7.f, yc - 7.f, 14.f, 14.f);
}
// SEGMENTADO (2-4 opcoes curtas): uma caixa so, com divisorias; a escolhida em ciano cheio.
static REAL V8SegLargura(const std::vector<Opc8>& o) {
    REAL w = 0.f;
    for (const Opc8& x : o) w += Larg8(x.rot, f8CtrlB) + 22.f;
    return w;
}
static void V8Seg(Graphics* g, REAL x, REAL y, REAL w, REAL h, const std::vector<Opc8>& o) {
    const int n = (int)o.size();
    if (n <= 0) return;
    const REAL natural = V8SegLargura(o);
    const REAL extra = (w > natural) ? (w - natural) / (REAL)n : 0.f;
    const RectF caixa(x, y, w, h);
    GraphicsPath clip; RoundPath(clip, caixa, 4.f);
    if (g) { FillRound(g, caixa, 4.f, K8_INPUT); g->SetClip(&clip); }
    REAL cx = x;
    for (int i = 0; i < n; i++) {
        const REAL wi = (i == n - 1) ? (x + w - cx) : (Larg8(o[i].rot, f8CtrlB) + 22.f + extra);
        const RectF r(cx, y, wi, h);
        const int idx = (int)g_items.size();
        g_items.push_back(Item{ r, o[i].kind, o[i].val, o[i].en, o[i].sel, o[i].tip });
        if (g) {
            const bool hot = (g_hot == idx) && o[i].en;
            if (o[i].sel) { SolidBrush b(o[i].en ? K8_ACC : Alfa8(K8_ACC, 130)); g->FillRectangle(&b, r); }
            else if (hot) { SolidBrush b(K8_CHIPH); g->FillRectangle(&b, r); }
            const Color ct = o[i].sel ? K8_PRETO : (hot ? K8_TXT : (o[i].en ? K8_SEC : Alfa8(K8_SEC, 100)));
            Txt(g, o[i].rot.c_str(), o[i].sel ? f8CtrlB : f8Ctrl, ct, r, 1, true);
            if (i > 0 && !o[i].sel && !o[i - 1].sel) { Pen p(K8_BORDA, 1.f); g->DrawLine(&p, cx, y, cx, y + h); }
        }
        cx += wi;
    }
    if (g) { g->ResetClip(); StrokeRound(g, caixa, 4.f, K8_BORDA, 1.f); }
}
// CHIPS (5+ opcoes ou nomes longos), com quebra de linha. wFixo > 0 = todos da mesma largura;
// `direita` alinha a fila a direita quando ela cabe numa linha so. Devolve a altura usada.
static REAL V8ChipsLargura(const std::vector<Opc8>& o, REAL wFixo, REAL pad = 16.f) {
    REAL t = 0.f;
    for (size_t i = 0; i < o.size(); i++) {
        REAL w = wFixo > 0.f ? wFixo : Larg8(o[i].rot, f8CtrlB) + pad;
        if (w < 34.f) w = 34.f;
        t += w + (i ? 6.f : 0.f);
    }
    return t;
}
static REAL V8Chips(Graphics* g, REAL x, REAL y, REAL wMax, REAL h, const std::vector<Opc8>& o,
                    REAL wFixo = 0.f, bool direita = false, REAL pad = 16.f) {
    const REAL gap = 6.f;
    const REAL total = V8ChipsLargura(o, wFixo, pad);
    REAL cx = (direita && total <= wMax) ? x + wMax - total : x, cy = y;
    for (size_t i = 0; i < o.size(); i++) {
        REAL w = wFixo > 0.f ? wFixo : Larg8(o[i].rot, f8CtrlB) + pad;
        if (w < 34.f) w = 34.f;
        if (cx + w > x + wMax + 0.5f && cx > x + 0.5f) { cx = x; cy += h + gap; }
        const RectF r(cx, cy, w, h);
        const int idx = (int)g_items.size();
        g_items.push_back(Item{ r, o[i].kind, o[i].val, o[i].en, o[i].sel, o[i].tip });
        if (g) {
            const bool hot = (g_hot == idx) && o[i].en;
            const BYTE a = (!o[i].en || o[i].apagado) ? 115 : 255;
            FillRound(g, r, 4.f, Alfa8(o[i].sel ? K8_ACC : (hot ? K8_CHIPH : K8_CHIP), a));
            Txt(g, o[i].rot.c_str(), o[i].sel ? f8CtrlB : f8Ctrl, Alfa8(o[i].sel ? K8_PRETO : (hot ? K8_TXT : K8_SEC), a), r, 1, true);
            if (o[i].ponto) {
                SolidBrush pb(o[i].sel ? K8_PRETO : K8_ACC);
                g->FillEllipse(&pb, r.X + r.Width - 9.f, r.Y + 4.f, 5.f, 5.f);
            }
        }
        cx += w + gap;
    }
    return cy + h - y;
}
// Tarja de aviso (fundo ambar translucido, borda, "!" e o texto), em qualquer aba.
static REAL V8Tarja(Graphics* g, REAL L, REAL IW, REAL y, const std::wstring& texto, const Color& cor,
                    const std::wstring& tip, int kindBotao = E_NONE, const wchar_t* rotBotao = nullptr) {
    const REAL bw = (kindBotao != E_NONE && rotBotao) ? V8BotaoW(rotBotao) : 0.f;
    const REAL wTxt = IW - 34.f - (bw > 0.f ? bw + 8.f : 0.f);
    REAL th = AlturaTexto(texto.c_str(), f8Sub, wTxt);
    if (th < 16.f) th = 16.f;
    const REAL h = (th + 14.f < 32.f) ? 32.f : th + 14.f;
    const RectF r(L, y, IW, h);
    g_items.push_back(Item{ r, E_NONE, 0, false, false, tip });
    if (g) {
        FillRound(g, r, 4.f, Alfa8(cor, 51));
        StrokeRound(g, RectF(r.X + .5f, r.Y + .5f, r.Width - 1.f, r.Height - 1.f), 4.f, cor, 1.f);
        Txt(g, L"!", f8CtrlB, cor, RectF(r.X + 8.f, r.Y, 16.f, h), 1);
        TxtWrap(g, texto.c_str(), f8Sub, cor, RectF(r.X + 26.f, r.Y + (h - th) / 2.f, wTxt, th + 2.f));
    }
    if (bw > 0.f) V8BotaoItem(g, RectF(r.X + r.Width - bw - 6.f, r.Y + (h - 26.f) / 2.f, bw, 26.f), rotBotao, B8_CONTORNO, kindBotao, 0, true);
    return h;
}
// O cartao (e a janelinha) com sombra: o mesmo desenho nas tres janelas v8.
static void MolduraSimples8(Graphics& g, REAL x, REAL y, REAL w, REAL h) {
    for (int i = SH; i >= 1; i--) {
        RectF r(x - (REAL)i, y - (REAL)i + 5.f, w + 2.f * i, h + 2.f * i);
        FillRound(&g, r, 8.f + (REAL)i, Color((BYTE)(2 + (SH - i)), 0, 0, 0));
    }
    const RectF card(x, y, w, h);
    FillRound(&g, card, 8.f, K8_CARTAO);
    StrokeRound(&g, card, 8.f, K8_BORDA, 1.f);
}
// Cabecalho das janelas pequenas (Filtros, Ajustes da rede neural, a janelinha do painel):
// titulo a esquerda, X de fechar a direita, divisoria embaixo. Devolve a altura.
static REAL V8CabecalhoJanela(Graphics* g, REAL x, REAL y, REAL w, const wchar_t* titulo, bool hotX, RectF* rX) {
    const REAL h = 56.f;
    const RectF bx(x + w - 20.f - 28.f, y + 14.f, 28.f, 28.f);
    if (rX) *rX = bx;
    if (g) {
        Txt(g, titulo, f8TitJan, K8_TXT, RectF(x + 20.f, y + 12.f, w - 80.f, 32.f), 0, true);
        if (hotX) FillRound(g, bx, 4.f, K8_CHIPH);
        StrokeRound(g, RectF(bx.X + .5f, bx.Y + .5f, bx.Width - 1.f, bx.Height - 1.f), 4.f, hotX ? K8_ACC : K8_BORDA, 1.f);
        Pen p(hotX ? K8_TXT : K8_SEC, 1.6f);
        g->DrawLine(&p, bx.X + 10.f, bx.Y + 10.f, bx.X + 18.f, bx.Y + 18.f);
        g->DrawLine(&p, bx.X + 18.f, bx.Y + 10.f, bx.X + 10.f, bx.Y + 18.f);
        Pen d(K8_BORDA, 1.f); g->DrawLine(&d, x, y + h, x + w, y + h);
    }
    return h;
}
// O aviso rapido (toast), centrado em [x, x+w], com o pe em yPe. Mesmo estilo em todo o app.
static void V8Toast(Graphics* g, REAL x, REAL w, REAL yPe, const std::wstring& t) {
    if (!g || t.empty()) return;
    REAL tw = Larg8(t, f8Ctrl) + 36.f;
    if (tw > w - 24.f) tw = w - 24.f;
    const RectF r(x + (w - tw) / 2.f, yPe - 38.f, tw, 38.f);
    for (int i = 6; i >= 1; i--) FillRound(g, RectF(r.X - i, r.Y - i + 4.f, r.Width + 2.f * i, r.Height + 2.f * i), 6.f + i, Color((BYTE)12, 0, 0, 0));
    FillRound(g, r, 6.f, K8_CHIP);
    StrokeRound(g, RectF(r.X + .5f, r.Y + .5f, r.Width - 1.f, r.Height - 1.f), 6.f, K8_BORDA, 1.f);
    Txt(g, t.c_str(), f8Ctrl, K8_TXT, r, 1, true);
}

// O resumo curto do "Passes": "Modelo B · Intensidade 100%" (os passes ja estao nos chips).
static std::wstring PerfilResumoCurto() {
    if (g_moduloAmd) return PerfilResumo();
    const int deg0 = (g_hud.nrDegrau[0] >= 0) ? g_hud.nrDegrau[0] : 0;
    std::wstring s = TS(L"Modelo ", L"Model ") + kCtrlNr[0].nomes[deg0];
    if (g_hud.nrValor[1] >= 0.f) {
        wchar_t pv[24]; _snwprintf(pv, 24, L"%d%%", (int)(g_hud.nrValor[1] * 100.f + 0.5f)); pv[23] = 0;
        s += TS(L" · Intensidade ", L" · Intensity ") + pv;
    } else {
        const int deg1 = (g_hud.nrDegrau[1] >= 0) ? g_hud.nrDegrau[1] : 0;
        s += TS(L" · Intensidade ", L" · Intensity ") + kCtrlNr[1].nomes[deg1];
    }
    return s;
}

// A explicacao do modo escolhido (textos do mockup, que vieram das dicas do card Modo).
static const wchar_t* ExplicaModo8(int m) {
    switch (m) {
    case MODO_UPSTREAM:  return T(L"Aplica o DLSS 5 antes de o jogo aumentar a resolução: mais leve e sem mexer na interface do jogo. Só em jogos com DLSS, FSR ou XeSS ligados. Mais chance de incompatibilidade.",
                                  L"Applies DLSS 5 before the game upscales: lighter on the GPU, and the game's HUD is left untouched. Only in games with DLSS, FSR or XeSS enabled. More likely to be incompatible.");
    case MODO_NATIVO:    return T(L"Integra-se ao jogo e aplica o DLSS 5 na imagem final, direto na placa. Imagem melhor, um pouco mais pesado. Jogos 64 bits DX11/DX12 sem anti-cheat.",
                                  L"Hooks into the game and applies DLSS 5 to the final image, directly on the GPU. Better image quality, slightly heavier. 64-bit DX11/DX12 games without anti-cheat.");
    case MODO_UNIVERSAL: return T(L"Funciona com qualquer coisa na tela: captura por fora e aplica o DLSS 5 como um filtro. Indicado para jogos com anti-cheat.",
                                  L"Works with anything on screen: captures from outside and applies DLSS 5 as a filter. Recommended for games with anti-cheat.");
    default:             return T(L"Usa o melhor modo que funcionar neste jogo: Upstream, senão Nativo, senão Universal.",
                                  L"Uses the best mode that works in this game: Upstream first, then Native, then Universal.");
    }
}

// ---------------------------------------------------------------- a janelinha ao lado (popover)
// "Resolução por etapa" (Avançado) e "Atalhos" (Geral). Mora NA MESMA janela layered do painel,
// a direita do cartao (o LarguraCartao cresce), e por isso e clicavel de graca: o "mudar" de
// cada atalho continua sendo o mesmo Item de sempre (E_ATALHO). So uma aberta por vez.
static int V8Popover(Graphics* g, REAL top) {
    const REAL x0 = (REAL)(SH + CW8 + POPGAP8), w = (REAL)POPW8;
    const REAL L = x0 + 20.f, IW = w - 40.f, R = L + IW;
    const bool atalhos = g_hud.atalhosAbertos;
    RectF rX;
    const int idxX = (int)g_items.size();
    const REAL hc = V8CabecalhoJanela(g, x0, top, w, atalhos ? T(L"Atalhos", L"Keyboard shortcuts") : T(L"Resolução por etapa", L"Resolution by stage"),
                                      g_hot == idxX, &rX);
    g_items.push_back(Item{ rX, atalhos ? E_ATALHOSABRIR : E_TELEMETRIA, g_hud.telemetria ? 1 : 0, true, false,
                            TS(L"Fechar", L"Close") });
    REAL cy = top + hc + 8.f;
    auto linhaInfo = [&](const std::wstring& rot, const std::wstring& val, const Color& cor, bool divisoria = true) {
        const REAL h = 30.f;
        if (g) {
            const REAL wv = Larg8(val, f8CtrlB) + 4.f;
            const REAL wr = IW - (wv < IW * 0.6f ? wv : IW * 0.6f) - 8.f;
            Txt(g, rot.c_str(), f8Ctrl, K8_SEC, RectF(L, cy, wr, h), 0, true);
            Txt(g, val.c_str(), f8CtrlB, cor, RectF(L + wr + 8.f, cy, IW - wr - 8.f, h), 2, true);
        }
        cy += h;
        if (divisoria) V8Div(g, L, IW, cy);
    };
    auto frase = [&](const std::wstring& t, const Color& cor) {
        REAL th = AlturaTexto(t.c_str(), f8Sub, IW);
        if (th < 16.f) th = 16.f;
        if (g) TxtWrap(g, t.c_str(), f8Sub, cor, RectF(L, cy + 4.f, IW, th + 2.f));
        cy += th + 8.f;
    };
    if (atalhos) {
        // ---- os atalhos: uma linha por acao, a tecla e o "mudar" (a linha inteira e o Item E_ATALHO)
        for (int i = 0; i < AT_COUNT; i++) {
            if (i == AT_INTERATIVO && !IniV8().reshade) continue;   // Rodada 23: ReShade escondido
            const REAL h = 34.f;
            const RectF r(L - 6.f, cy + 2.f, IW + 12.f, h - 4.f);
            const int idx = (int)g_items.size();
            const bool capt = (g_hud.capturandoAtalho == i);
            g_items.push_back(Item{ r, E_ATALHO, i, true, capt,
                                    std::wstring(RotuloAtalho(i)) + TS(L"\nClique para trocar a tecla: aperte a nova (Ctrl/Alt/Shift valem; Esc cancela).",
                                                                       L"\nClick to rebind: press the new key (Ctrl/Alt/Shift supported; Esc cancels).") });
            if (g) {
                const bool hot = (g_hot == idx);
                if (capt) { FillRound(g, r, 4.f, Alfa8(K8_ACC, 30)); StrokeRound(g, r, 4.f, K8_ACC, 1.f); }
                else if (hot) FillRound(g, r, 4.f, K8_CHIP);
                const bool recusado = g_atalhos[i].vk && !g_atalhos[i].registrado;
                const std::wstring tecla = capt ? TS(L"Aperte uma tecla…", L"Press a key…")
                                         : g_atalhos[i].vk ? TextoAtalho((AcaoAtalho)i) : TS(L"definir", L"Not set");
                const REAL wb = Larg8(tecla, f8CtrlB) + 16.f;
                const REAL wm = 52.f;
                Txt(g, RotuloAtalho(i), f8Ctrl, K8_TXT, RectF(L, cy, IW - wb - wm - 16.f, h), 0, true);
                const RectF bd(R - wm - 8.f - wb, cy + 5.f, wb, h - 10.f);
                FillRound(g, bd, 4.f, K8_INPUT);
                Txt(g, tecla.c_str(), f8CtrlB, recusado ? K8_AVISO : (g_atalhos[i].vk ? K8_ACC : K8_SEC), bd, 1, true);
                const RectF bm(R - wm, cy + 5.f, wm, h - 10.f);
                StrokeRound(g, RectF(bm.X + .5f, bm.Y + .5f, bm.Width - 1.f, bm.Height - 1.f), 4.f, hot ? K8_ACC : K8_BORDA, 1.f);
                Txt(g, T(L"mudar", L"Change"), f8Sub, hot ? K8_ACC : K8_SEC, bm, 1, true);
            }
            cy += h;
            V8Div(g, L, IW, cy);
        }
        // ---- o ReShade (so informativo) e o modo do mouse de agora
        if (IniV8().reshade) {
            UINT vkRs = 0, modsRs = 0;
            linhaInfo(TS(L"ReShade (segundo menu)", L"ReShade (second menu)"), TeclaOverlayReShade(&vkRs, &modsRs), K8_TXT);
        }
        linhaInfo(TS(L"Mouse agora", L"Mouse mode"), g_mouseModoPonteiro ? TS(L"Ponteiro (menus)", L"Pointer (menus)") : TS(L"Câmera (padrão)", L"Camera (default)"),
                  g_mouseModoPonteiro ? K8_ACC : K8_SEC, false);
        if (g_hud.capturandoAtalho >= 0) frase(TS(L"Aperte a tecla — Ctrl/Alt/Shift valem · Esc cancela", L"Press a key — Ctrl/Alt/Shift supported · Esc cancels"), K8_ACC);
        else if (!g_hud.avisoAtalho.empty()) frase(g_hud.avisoAtalho, K8_AVISO);
        cy += 6.f;
        {   // restaurar padroes / guia de teclas
            const REAL bh = 30.f, gap = 8.f, bw = (IW - gap) / 2.f;
            V8BotaoItem(g, RectF(L, cy, bw, bh), TS(L"Restaurar padrões", L"Restore defaults"), B8_CONTORNO, E_ATALHO_PADRAO, 0, true,
                        TS(L"Restaurar padrões\nVolta todas as teclas às de fábrica.", L"Restore defaults\nResets every shortcut to its default key."));
            V8BotaoItem(g, RectF(L + bw + gap, cy, bw, bh), TS(L"Guia de teclas", L"Key guide"), B8_CONTORNO, E_GUIA, 0, true,
                        TS(L"Guia de teclas\nO cartaz com as teclas, de novo.", L"Key guide\nShows the shortcut overlay again."));
            cy += bh + 10.f;
        }
        frase(g_hkPanel + TS(L" abre e fecha este painel · Esc ou clique fora fecha", L" toggles the panel · Esc or click outside to close"), K8_SEC);
    } else {
        // ---- RESOLUCAO POR ETAPA: medida, nao configurada (ver o bloco de mesmo nome no RenderV7)
        auto par = [&](const wchar_t* rot, UINT a, UINT b, const std::wstring& extra) {
            wchar_t v[96];
            if (a && b) _snwprintf(v, 96, L"%u × %u", a, b); else wcscpy(v, L"—");
            v[95] = 0;
            std::wstring val = v;
            if (!extra.empty()) val += L"  " + extra;
            linhaInfo(rot, val, (a && b) ? K8_TXT : K8_SEC);
        };
        par(T(L"Jogo", L"Game"), g_hud.etJogoW, g_hud.etJogoH, L"");
        par(T(L"Captura", L"Capture"), g_hud.etCapW, g_hud.etCapH, L"");
        par(T(L"Entrada da rede", L"Network input"), g_hud.etTrabW, g_hud.etTrabH, g_hud.etModo);
        par(T(L"Neural Rendering", L"Neural Rendering"), g_hud.etNrW, g_hud.etNrH, g_hud.etNrNota);
        par(T(L"Saída da rede", L"Network output"), g_hud.etSaiW, g_hud.etSaiH, L"");
        par(T(L"Cadeia (buffer)", L"Chain buffer"), g_hud.etBbW, g_hud.etBbH, L"");
        par(T(L"Tela", L"Screen"), g_hud.etCliW, g_hud.etCliH, L"");
        if (g_hud.etTemFeed) {
            wchar_t ln[240];
            _snwprintf(ln, 240, T(L"rede: %.1f passe(s) por quadro medidos (pedido %d) · %.2f ms de CPU do feeder por quadro",
                                  L"Network: %.1f passes per frame measured (%d requested) · %.2f ms of feeder CPU time per frame"),
                       g_hud.etNrPassesQuadro, g_hud.etNrPasses, g_hud.etNrMsQuadro);
            ln[239] = 0;
            frase(ln, K8_SEC);
        } else {
            frase(TS(L"o feeder ainda não disse o que construiu — as linhas da rede aparecem depois de ~600 quadros.",
                     L"The feeder hasn't reported its setup yet — network rows appear after ~600 frames."), K8_SEC);
        }
        // a topologia (quem desenha, quem processa, quem exibe) - mesma conta do RenderV7
        if (!g_hud.topoRede.empty() || !g_hud.topoMonitor.empty()) {
            cy += 4.f;
            V8Secao(g, L, IW, cy, T(L"TOPOLOGIA", L"TOPOLOGY"));
            linhaInfo(TS(L"Desenha o jogo", L"Renders the game"), g_hud.topoOutra.empty() ? g_hud.topoRede : g_hud.topoOutra + TS(L" (provável)", L" (probably)"), K8_TXT);
            linhaInfo(TS(L"Processa (rede)", L"Runs the network"), g_hud.topoRede, K8_TXT);
            linhaInfo(TS(L"Exibe (monitor)", L"Drives the monitor"), g_hud.topoMonitor.empty() ? TS(L"não identificada", L"Not identified") : g_hud.topoMonitor, K8_TXT);
            linhaInfo(TS(L"Travessias por quadro", L"PCIe crossings per frame"), std::to_wstring(g_hud.topoTravessias) + TS(L"  (meta: 1)", L"  (target: 1)"),
                      g_hud.topoTravessias <= 1 ? K8_TXT : K8_AVISO, false);
            frase(g_hud.topoRecomendado ? TS(L"Modo recomendado: a saída está na placa que processa — a película não atravessa nada.",
                                             L"Recommended setup: the monitor is on the GPU that runs the network — nothing crosses PCIe.")
                                        : TS(L"Saída na placa do jogo: a nossa ponte atravessa DUAS vezes por quadro. Se puder, ligue o monitor na placa que processa.",
                                             L"The monitor is on the game's GPU: our bridge crosses PCIe TWICE per frame. If you can, plug the monitor into the GPU that runs the network."),
                  g_hud.topoRecomendado ? K8_SEC : K8_AVISO);
            if (g_hud.fonteJanela && IniV8().renoDxNovo && !g_moduloAmd) {
                const bool jaEsta = (IniGet(L"saida_monitor") == L"1" && IniGet(L"super_resolucao") == L"sr" &&
                                     g_hud.motorProprio && g_hud.nrEscala > 0 && g_hud.nrEscala < 100 && g_hud.nrPasses == 1);
                V8BotaoItem(g, RectF(L, cy + 2.f, IW, 30.f), jaEsta ? TS(L"Receita aplicada: janela pequena → 4K", L"Recipe applied: small window → 4K")
                                                                    : TS(L"Aplicar \"janela pequena → 4K pela rede\"", L"Apply \"small window → 4K via the network\""),
                            B8_CONTORNO, E_RECEITAJANELA, 0, !jaEsta,
                            TS(L"Receita \"janela pequena → 4K pela rede\"\nDeixe o jogo numa janela pequena (720p ou 1080p) e a rede reconstrói até a resolução do monitor. "
                               L"Escreve de uma vez: saída no monitor, Super Resolution = DLSS SR, rede em 65% e 1 passe. O programa reabre uma vez. "
                               L"A metade do transporte está medida (janela = 110 fps contra 25 em tela cheia 4K); a da película, não.",
                               L"Recipe \"small window → 4K via the network\"\nKeep the game in a small window (720p or 1080p) and the network rebuilds it up to the monitor's resolution. "
                               L"Sets in one go: output at monitor resolution, Super Resolution = DLSS SR, network at 65% and 1 pass. The app restarts once. "
                               L"The transport half is measured (window = 110 fps vs 25 in 4K fullscreen); the layer half is not."));
                cy += 36.f;
            }
        }
    }
    cy += 12.f;
    if (g && cy < top + 80.f) cy = top + 80.f;
    return (int)(cy - top + 0.5f);
}

// ---------------------------------------------------------------- o menu
static int RenderV8(Graphics* g) {
    EnsureFonts();
    EnsureFonts8();
    CarregarAba();
    g_items.clear();
    const REAL L = (REAL)(SH + PAD8), IW = (REAL)(CW8 - PAD8 * 2), R = L + IW;
    const REAL top = (REAL)SH, xCard = (REAL)SH;
    if (g_presetSel < 1 || g_presetSel > 3) { const int a = PresetJogo::Ativo(); g_presetSel = a > 0 ? a : 1; }
    const bool nativo = g_hud.modoIntegrado && !g_hud.integrando;

    // ================================================================ cabecalho (igual em todas as abas)
    {
        const std::wstring titulo = L"DLSS5ForAll";
        const REAL LT = L + 52.f;   // o bloco titulo/placa/jogo comeca depois da logo (40 + 12 de respiro)
        const REAL wt = Larg8(titulo, f8Titulo) + 6.f;
        g_items.push_back(Item{ RectF(L, top + 12.f, wt + 52.f, 69.f), E_NONE, 0, false, false,
                                std::wstring(L"DLSS5ForAll ") + APP_VERSION + TS(L"\nFiltro neural por cima de qualquer jogo, vídeo ou captura.",
                                                                                  L"\nA neural filter for any game, video or capture.") });
        // o placar: "chegada → apresentado fps", a direita
        const double chegada = ChegadaFluidez();
        const double apres = g_hud.fgAtivo ? g_hud.fgApresFps : g_hud.fpsPresent;
        const bool semFonte = g_hud.src.empty();
        wchar_t a[16], b[16];
        if (semFonte) { wcscpy(a, L"—"); wcscpy(b, L"—"); }
        else { _snwprintf(a, 16, L"%.0f", chegada); _snwprintf(b, 16, L"%.0f", apres); }
        a[15] = 0; b[15] = 0;
        const REAL wUn = Larg8(L"fps", f8PlacarUn), wB = Larg8(b, f8Placar), wSeta = Larg8(L"→", f8Placar), wA = Larg8(a, f8Placar);
        const REAL yP = top + 28.f, hP = 32.f;
        REAL x = R - wUn;
        const REAL xPlacar = x - 6.f - wB - 6.f - wSeta - 6.f - wA;
        g_items.push_back(Item{ RectF(xPlacar, yP, R - xPlacar, hP), E_NONE, 0, false, false,
                                TS(L"Quadros por segundo\nÀ esquerda os que chegam da fonte (o jogo, o vídeo); à direita os que vão para a tela, já com a rede e a geração de quadros.",
                                   L"Frames per second\nLeft: frames arriving from the source (game or video). Right: frames reaching the screen, after the network and Frame Generation.") });
        if (g) {
            DesenharLogo8(g, L, top + 26.f, 40.f);   // a capivara, alinhada ao meio do bloco titulo/placa/jogo
            Txt(g, titulo.c_str(), f8Titulo, K8_TXT, RectF(LT, top + 12.f, xPlacar - LT - 8.f, 32.f), 0, true);
            const std::wstring gpu = g_gpu.lido && !g_gpu.nome.empty() ? g_gpu.nome : TS(L"placa não identificada", L"GPU not identified");
            Txt(g, gpu.c_str(), f8Gpu, K8_SEC, RectF(LT, top + 44.f, xPlacar - LT - 8.f, 20.f), 0, true);
            const int ativo = PresetJogo::Ativo();
            std::wstring jl = PresetJogo::NomeDoDono() + L" · ";
            jl += ativo > 0 ? TS(L"preset ", L"preset ") + std::to_wstring(ativo) : TS(L"padrão", L"default");
            Txt(g, jl.c_str(), f8Jogo, K8_SEC, RectF(LT, top + 64.f, R - LT, 17.f), 0, true);
            Txt(g, L"fps", f8PlacarUn, K8_SEC, RectF(x, yP + 3.f, wUn + 2.f, hP), 0);
            x -= 6.f + wB;
            Txt(g, b, f8Placar, K8_ACC, RectF(x, yP, wB + 2.f, hP), 0);
            x -= 6.f + wSeta;
            Txt(g, L"→", f8Placar, K8_ACC, RectF(x, yP, wSeta + 2.f, hP), 0);
            x -= 6.f + wA;
            Txt(g, a, f8Placar, K8_SEC, RectF(x, yP, wA + 2.f, hP), 0);
            Pen p(K8_BORDA, 1.f); g->DrawLine(&p, xCard, top + kHdr8, xCard + (REAL)CW8, top + kHdr8);
        }
    }
    // ================================================================ abas
    {
        const wchar_t* nomes[ABA_COUNT] = { T(L"IMAGEM", L"IMAGE"), T(L"FLUIDEZ", L"SMOOTHNESS"), T(L"AVANÇADO", L"ADVANCED"), T(L"GERAL", L"GENERAL") };
        const REAL wAba = (REAL)CW8 / (REAL)ABA_COUNT, y = top + kHdr8 + 1.f;
        for (int i = 0; i < ABA_COUNT; i++) {
            const RectF r(xCard + i * wAba, y, wAba, kAbas8);
            const int idx = (int)g_items.size();
            const bool sel = (g_abaAtual == i);
            g_items.push_back(Item{ r, E_ABA, i, true, sel, std::wstring() });
            if (g) {
                Txt(g, nomes[i], f8Aba, (sel || g_hot == idx) ? K8_TXT : K8_SEC, r, 1);
                if (sel) { SolidBrush s(K8_ACC); g->FillRectangle(&s, RectF(r.X, r.Y + kAbas8 - 2.f, r.Width, 2.f)); }
            }
        }
        if (g) { Pen p(K8_BORDA, 1.f); g->DrawLine(&p, xCard, y + kAbas8, xCard + (REAL)CW8, y + kAbas8); }
    }
    REAL cy = top + kHdr8 + kAbas8 + 12.f;

    // ================================================================ tarjas (em qualquer aba: sao estado, nao opcao)
    if (g_hud.shadersFaltando) {
        cy += V8Tarja(g, L, IW, cy, TS(L"Faltam os shaders — a imagem está saindo sem filtro", L"Shaders are missing — the image is unfiltered"),
                      K8_AVISO, std::wstring()) + 8.f;
    }
    if (!g_hud.diagCurto.empty()) cy += V8Tarja(g, L, IW, cy, g_hud.diagCurto, K8_AVISO, g_hud.diagDica) + 8.f;
    if (!g_hud.cabeAviso.empty()) cy += V8Tarja(g, L, IW, cy, g_hud.cabeAviso, g_hud.cabeLimitando ? K8_ACC : K8_AVISO, g_hud.cabeDica) + 8.f;
    if (g_hud.fonteMudou || !g_hud.pendenteReabrir.empty()) {
        const std::wstring t = g_hud.fonteMudou ? TS(L"O alvo mudou de tamanho: reabra o app na resolução nova.", L"The target window changed size: restart the app at the new resolution.")
                                                : TS(L"Só vale quando reabrir: ", L"Applies after a restart: ") + PendenteReabrirEmLinha();
        cy += V8Tarja(g, L, IW, cy, t, K8_AVISO, std::wstring(), E_REABRIR, T(L"Reabrir", L"Restart")) + 8.f;
    }

    // ================================================================ IMAGEM
    if (g_abaAtual == ABA_IMAGEM) {
        V8Secao(g, L, IW, cy, T(L"NEURAL RENDERING", L"NEURAL RENDERING"));
        // ---- Neural Rendering (DLSS 5): o card mestre (E_FILTER, so a rede; o F6 e o programa inteiro)
        {
            const bool neuralOk = (g_hud.neural == NR_ATIVO), subindo = (g_hud.neural == NR_INICIANDO);
            const bool temAddon = (g_hud.neural != NR_SEM_ADDON && g_hud.neural != NR_SEM_RTX);
            const bool camadaOn = g_hud.overlay ? !g_hud.bypass : true;
            const bool ligado   = camadaOn && (neuralOk || subindo);   // v2.1: sem rede (SEM_RTX/SEM_ADDON) o botao fica APAGADO, nunca com cara de ligado
            const ULONGLONG ag = GetTickCount64();
            const int seg = (g_hud.ativarEm > ag) ? (int)((g_hud.ativarEm - ag + 999) / 1000) : 0;
            const bool en = g_hud.overlay && temAddon && !g_hud.neuralPendente;
            std::wstring sub = TemAtalho(AT_TUDO) ? TextoAtalho(AT_TUDO) + L" · " : std::wstring();
            if (seg > 0)                                 sub += TS(L"ativando… ", L"Activating… ") + std::to_wstring(seg);
            else if (g_hud.neuralPendente)               sub += TS(L"confirmando com a rede…", L"Waiting for the network to confirm…");
            else if (g_hud.neural == NR_SEM_RTX)         sub += TS(L"não roda nesta placa", L"Not supported on this GPU");
            else if (g_hud.neural == NR_SEM_ADDON)       sub += TS(L"não está instalado", L"Not installed");
            else if (!camadaOn)                          sub += TS(L"imagem original, sem filtro", L"Original image, no filter");
            else if (!neuralOk && !subindo)              sub += TS(L"a camada está na frente, mas a rede está desligada", L"Overlay is on, but the network is off");
            else if (subindo)                            sub += TS(L"esperando o primeiro quadro", L"Waiting for the first frame");
            else                                         sub += TS(L"ativo — a rede refaz a imagem", L"Active — the network is rebuilding the image");
            const bool alerta = ligado && !neuralOk && !subindo && !g_hud.neuralPendente && temAddon;
            const REAL h = kLinhaSub8;
            const int idx = (int)g_items.size();
            g_items.push_back(Item{ RectF(L, cy, IW, h), E_FILTER, 0, en, ligado,
                                    TS(L"Neural Rendering (DLSS 5)\nLiga ou desliga SÓ a rede neural: a película continua na tela (captura, ampliação e filtros), "
                                       L"para comparar com e sem a rede. Ligar tem contagem de 3 s; desligar devolve o desempenho na hora. "
                                       L"A tecla do programa inteiro é a do atalho \"tudo\".",
                                       L"Neural Rendering (DLSS 5)\nTurns ONLY the neural network on or off; the overlay stays on screen (capture, upscaling and filters), "
                                       L"so you can compare with and without it. Turning it on has a 3 s countdown; turning it off frees up performance instantly. "
                                       L"To toggle the whole app, use the \"Enable/disable DLSS 5\" shortcut.") });
            if (g) {
                V8Rotulo(g, L, cy, IW - kChave8 - 12.f, h, TS(L"Neural Rendering (DLSS 5)", L"Neural Rendering (DLSS 5)"), sub, true, alerta ? &K8_AVISO : nullptr);
                V8Chave(g, R - kChave8, cy + h / 2.f, ligado, en, g_hot == idx);
            }
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Filtros: "F7 · N ativos", interruptor mestre (E_FILTROSLIGA) e Configurar (E_EFEITOS)
        {
            const bool ligado = Filtros::Ligado();
            const int nAt = Filtros::QuantosLigados();
            std::wstring sub = TemAtalho(AT_FILTROSLIGA) ? TextoAtalho(AT_FILTROSLIGA) + L" · " : std::wstring();
            if (!ligado) sub += TS(L"desligados — os valores continuam salvos", L"Off — your settings are kept");
            else if (g_hud.srEfetivo == 2) sub += TS(L"sem efeito enquanto a IA amplia", L"No effect while the AI is upscaling");
            else sub += std::to_wstring(nAt) + (EmIngles() ? L" active" : (nAt == 1 ? L" ativo" : L" ativos"));
            const REAL h = kLinhaSub8;
            const std::wstring rotCfg = T(L"Configurar", L"Configure");
            const REAL bw = V8BotaoW(rotCfg);
            const int idx = (int)g_items.size();
            g_items.push_back(Item{ RectF(L, cy, IW - bw - 8.f, h), E_FILTROSLIGA, 0, true, ligado,
                                    TS(L"Filtros\nLiga ou desliga todos os filtros de uma vez, preservando o que você escolheu em cada um. Independente do Neural Rendering.\nResumo: ",
                                       L"Filters\nTurns all filters on or off at once, keeping what you chose for each one. Independent of Neural Rendering.\nSummary: ") + Filtros::Resumo() });
            if (g) {
                V8Rotulo(g, L, cy, IW - bw - kChave8 - 26.f, h, TS(L"Filtros", L"Filters"), sub, true);
                V8Chave(g, R - bw - 10.f - kChave8, cy + h / 2.f, ligado, true, g_hot == idx);
            }
            V8BotaoItem(g, RectF(R - bw, cy + (h - kCtrl8) / 2.f, bw, kCtrl8), rotCfg, B8_CONTORNO, E_EFEITOS, 0, true,
                        TS(L"Configurar os filtros\nAbre a janela de filtros no canto (nitidez, cor, brilho, vinheta…).", L"Configure filters\nOpens the Filters window (sharpness, color, brightness, vignette…).")
                        + (TemAtalho(AT_EFEITOS) ? TS(L"\nAtalho: ", L"\nShortcut: ") + TextoAtalho(AT_EFEITOS) : std::wstring()));
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Passes: "Passes" + resumo + [Ajustar] na mesma linha, chips 1..10 embaixo, e a barra da rede
        {
            const bool pode = g_hud.nrPode;
            const bool temPasses = pode && !g_moduloAmd;
            const bool smRuim = (g_dlssnr.resultado == DLSSNR_INCOMPATIVEL);
            const std::wstring rotAj = T(L"Ajustar", L"Adjust");
            const REAL bw = V8BotaoW(rotAj), h1 = 40.f;
            const std::wstring resumo = !pode ? TS(L"o add-on da rede não tem estes ajustes", L"Not available with this network add-on")
                                              : PerfilResumoCurto();
            if (g) V8Rotulo(g, L, cy + 2.f, IW - bw - 12.f, h1 - 2.f, TS(L"Passes", L"Passes"), resumo, pode, smRuim ? &K8_AVISO : nullptr);
            V8BotaoItem(g, RectF(R - bw, cy + (h1 - kCtrl8) / 2.f + 1.f, bw, kCtrl8), rotAj, B8_CONTORNO, E_PERFIL, 0, pode,
                        smRuim ? DlssnrDicaFaixa()
                               : TS(L"Ajustes da rede neural\nModelo, intensidade, estrutura, pele e rostos, tom local e global, e a resolução de cada passe — nada vale antes de Aplicar.",
                                    L"Neural network settings\nModel, intensity, structure, skin and faces, local and global tone, and each pass's resolution — nothing applies until you press Apply."));
            cy += h1;
            int idxChip0 = -1;
            if (temPasses) {
                std::vector<Opc8> o;
                idxChip0 = (int)g_items.size();
                for (int i = 0; i < 10; i++) {
                    const std::vector<int> ei = EscalasParaPasses(kNrPasses[i]);
                    wchar_t tip[420];
                    _snwprintf(tip, 420,
                        T(L"%d passe(s)\nCada passe é outra rodada da rede sobre o MESMO quadro: mais rede, mais custo. ~%ls ms de rede por quadro (%ls).%ls\n%ls",
                          L"%d pass(es)\nEach pass runs the network again over the SAME frame: more work, more cost. ~%ls ms of network time per frame (%ls).%ls\n%ls"),
                        kNrPasses[i], MsTxt(Calib::RedeMs(ei)).c_str(), Calib::Rotulo().c_str(),
                        kNrPasses[i] > 1 ? T(L" Acima de 1 a rede roda sobre a própria saída; o padrão de fábrica é 1.", L" Above 1, the network runs on its own output; the default is 1.") : L"",
                        IniV8().motorProprio ? T(L"Vale agora.", L"Applies now.") : T(L"Ao escolher, o programa reinicia em 3 s para aplicar.", L"The app restarts in 3 s to apply it."));
                    tip[419] = 0;
                    o.emplace_back(std::to_wstring(kNrPasses[i]), E_NRPASSES, i, g_hud.nrPasses == kNrPasses[i], true, std::wstring(tip));
                }
                const REAL wChip = (IW - 9.f * 6.f) / 10.f;
                cy += V8Chips(g, L, cy, IW, kCtrl8, o, wChip) + 8.f;
                // "Rede neural: X ms de Y ms" (a do menu End, aprovada): com o mouse num chip, a previa fantasma
                int nPrev = 0;
                if (idxChip0 >= 0 && g_hot >= idxChip0 && g_hot < idxChip0 + 10) nPrev = kNrPasses[g_hot - idxChip0];
                const std::vector<int> ep = nPrev > 0 ? EscalasParaPasses(nPrev) : std::vector<int>();
                const BarraRede br = CalcularBarraRede(nPrev > 0 ? &ep : nullptr);
                const REAL hb = DesenharBarraRede(g, L, cy, IW, br, f8Sub, K8_SEC, K8_SEC, K8_BORDA);
                g_items.push_back(Item{ RectF(L, cy, IW, hb), E_NONE, 0, false, false, DicaBarraRede(br) });
                cy += hb + 8.f;
            } else cy += 2.f;
            V8Div(g, L, IW, cy);
        }
        // ---- Resolucao do DLSS (chips): o unico controle que mexe no CUSTO da rede
        {
            const REAL h = kLinha8;
            std::vector<Opc8> o;
            int ef = g_hud.workRes;
            const double mp = (double)g_hud.w * (double)g_hud.h / 1e6;
            if (!ef) ef = (mp <= 2.4) ? 100 : (mp <= 4.0) ? 77 : (mp <= 6.0) ? 67 : 58;
            for (int i = 0; i < kWorkResN; i++) {
                std::wstring tip = WorkResNomeLinha(i) + TS(L"\nResolução em que a rede trabalha: menor = filtro mais leve e mais fps; Auto escolhe pelo tamanho da tela. "
                                                             L"É o único controle que mexe no CUSTO da rede. Vale na próxima abertura da cadeia.",
                                                             L"\nThe resolution the network runs at: lower = lighter and more FPS; Auto picks based on screen size. "
                                                             L"This is the only control that changes the network's COST. Applies the next time the pipeline starts.");
                const int pct = kWorkRes[i] > 0 ? kWorkRes[i] : ef;
                const double f = pct / 100.0, cheio = 0.142 + 0.193 * mp, novo = 0.142 + 0.193 * mp * f * f;
                const int menos = (cheio > 0.0) ? (int)(100.0 * (1.0 - novo / cheio) + 0.5) : 0;
                if (menos > 0 && mp > 0.0) {
                    wchar_t est[140]; _snwprintf(est, 140, T(L"\nA %d%% a rede processa %d%% da área — cerca de %d%% menos custo.", L"\nAt %d%%, the network processes %d%% of the area — roughly %d%% less cost."),
                                                 pct, (pct * pct) / 100, menos); est[139] = 0;
                    tip += est;
                }
                o.emplace_back(kWorkRes[i] == 0 ? std::wstring(T(L"Auto", L"Auto")) : std::to_wstring(kWorkRes[i]) + L"%", E_WORKRES, i, g_hud.workRes == kWorkRes[i], true, tip);
            }
            const REAL wc = V8ChipsLargura(o, 0.f, 10.f);
            const REAL wl = IW - wc - 10.f;
            if (g) V8Rotulo(g, L, cy, wl, h, TS(L"Resolução do DLSS", L"DLSS resolution"), std::wstring());
            V8Chips(g, L + wl + 10.f, cy + (h - kCtrl8) / 2.f, wc, kCtrl8, o, 0.f, false, 10.f);
            cy += h;
            if (!g_hud.workResAviso.empty()) {
                const REAL th = AlturaTexto(g_hud.workResAviso.c_str(), f8Sub, IW);
                if (g) TxtWrap(g, g_hud.workResAviso.c_str(), f8Sub, K8_AVISO, RectF(L, cy - 4.f, IW, th + 2.f));
                cy += th + 2.f;
            }
            V8Div(g, L, IW, cy);
        }
        // ---- Upscaling pela rede (segmentado): quem amplia a imagem ate a tela, do lado da rede
        {
            const REAL h = kLinha8;
            std::vector<Opc8> o;
            const wchar_t* dica = T(L"Upscaling pela rede\nA rede trabalha na resolução de cima e alguém leva o resultado até a tela. Não: a rede entrega do mesmo tamanho "
                                    L"(DLAA) e um blit simples estica. FSR 1: o mesmo, mas quem estica é o filtro espacial da AMD. DLSS: o DLSS reconstrói até a tela "
                                    L"(exige resolução abaixo de 100% e custa o mesmo que 100%). Vale assim que a rede se refizer.",
                                    L"Network upscaling\nThe network runs at the resolution above, and something has to bring the result up to screen size. Off: the network "
                                    L"outputs at the same size (DLAA) and a simple blit stretches it. FSR 1: same, but AMD's spatial filter does the stretching. DLSS: DLSS itself "
                                    L"reconstructs up to screen resolution (needs a resolution below 100% and costs the same as 100%). Applies once the network rebuilds.");
            o.emplace_back(T(L"Não", L"Off"), E_SRMODO, 0, g_hud.srPedido == 0, true, dica);
            o.emplace_back(L"FSR 1", E_SRMODO, 1, g_hud.srPedido == 1, true, dica);
            o.emplace_back(L"DLSS", E_SRMODO, 2, g_hud.srPedido == 2, true, dica);
            const REAL ws = V8SegLargura(o);
            if (g) V8Rotulo(g, L, cy, IW - ws - 12.f, h, TS(L"Upscaling pela rede", L"Network upscaling"), std::wstring());
            V8Seg(g, R - ws, cy + (h - kCtrl8) / 2.f, ws, kCtrl8, o);
            cy += h;
            if (!g_hud.srAviso.empty()) {
                const std::wstring av = TS(L"não está valendo: ", L"Not in effect: ") + g_hud.srAviso;
                const REAL th = AlturaTexto(av.c_str(), f8Sub, IW);
                if (g) TxtWrap(g, av.c_str(), f8Sub, K8_AVISO, RectF(L, cy - 4.f, IW, th + 2.f));
                cy += th + 2.f;
            }
        }
        // ---- "Quem faz o quê": estado, nao explicacao (os numeros medidos ficam na dica)
        {
            const bool neuralLigado = (g_hud.neural == NR_ATIVO || g_hud.neural == NR_INICIANDO);
            const bool redeAmplia = (g_hud.srEfetivo == 2);
            const bool temEstica = (g_hud.etCapW && g_hud.etCliW && g_hud.etCapW != g_hud.etCliW);
            const REAL h = 24.f;
            wchar_t ln[320];
            _snwprintf(ln, 320, T(L"Quem faz o quê\nNeural: %ls · Ampliação: %ls (%ux%u → %ux%u) · Quadros gerados: %ls\nOs três são independentes — dá para ligar um sem o outro.",
                                  L"Pipeline\nNeural: %ls · Upscaling: %ls (%ux%u → %ux%u) · Generated frames: %ls\nAll three are independent — you can use any one without the others."),
                       neuralLigado ? T(L"ligado", L"on") : T(L"desligado", L"off"),
                       redeAmplia ? T(L"a REDE (DLSS SR)", L"the NETWORK (DLSS SR)") : temEstica ? T(L"filtro do app", L"app filter") : T(L"nenhuma (1:1)", L"none (1:1)"),
                       g_hud.etCapW, g_hud.etCapH, g_hud.etCliW, g_hud.etCliH, g_hud.fgAtivo ? T(L"ligados", L"on") : T(L"desligados", L"off"));
            ln[319] = 0;
            g_items.push_back(Item{ RectF(L, cy, IW, h), E_NONE, 0, false, false, ln });
            if (g) {
                // "Quem faz o quê: Neural · Ampliação · Quadros gerados" - o que esta ligado vai em branco/negrito
                const std::wstring p0 = TS(L"Quem faz o quê: ", L"Pipeline: ");
                const std::wstring ps[3] = { TS(L"Neural", L"Neural"), redeAmplia ? TS(L"Ampliação pela rede", L"Network upscaling") : TS(L"Ampliação", L"Upscaling"),
                                             TS(L"Quadros gerados", L"Generated frames") };
                const bool on[3] = { neuralLigado, redeAmplia || temEstica, g_hud.fgAtivo };
                REAL x = L;
                Txt(g, p0.c_str(), f8Sub, K8_SEC, RectF(x, cy, IW, h), 0);
                x += Larg8(p0, f8Sub);
                for (int i = 0; i < 3; i++) {
                    if (i) { Txt(g, L" · ", f8Sub, K8_SEC, RectF(x, cy, 30.f, h), 0); x += Larg8(L" · ", f8Sub); }
                    Font* f = on[i] ? f8CtrlB : f8Sub;
                    Txt(g, ps[i].c_str(), f, on[i] ? K8_TXT : K8_SEC, RectF(x, cy, R - x, h), 0, true);
                    x += Larg8(ps[i], f);
                }
            }
            cy += h + 8.f;
        }
        V8Secao(g, L, IW, cy, T(L"RECONSTRUÇÃO", L"RECONSTRUCTION"));
        // ---- Como a IA reconstroi (chips): o preset do DLSS (nao custa fps; nao existe no modulo AMD)
        {
            const REAL h = kLinha8;
            static const wchar_t* kDicaPt[] = {
                L"Padrão\nO modelo que a NVIDIA escolhe sozinha. Não custa FPS — preset é só comportamento.",
                L"Modelo E — rede antiga\nInventa menos detalhe e acumula menos entre quadros. Sem os dados de movimento do jogo, tende a ficar mais estável.",
                L"Modelo F — rede antiga\nComo o 1, com um pouco mais de detalhe. Foi o melhor aqui quando o salto de resolução é grande.",
                L"Modelo J — rede nova (transformer)\nReconstrói mais detalhe; em salto grande sofre, porque conta com os vetores de movimento do jogo — que a captura não tem.",
                L"Modelo K — rede nova (transformer)\nO mais recente. Melhor em resolução mais alta; é o que mais depende dos dados que faltam no nosso caminho.",
            };
            static const wchar_t* kDicaEn[] = {
                L"Default\nThe model NVIDIA picks automatically. Costs no FPS — a preset only changes behavior.",
                L"Model E — older (CNN)\nInvents less detail and accumulates less across frames. Without the game's motion data, it tends to be more stable.",
                L"Model F — older (CNN)\nLike 1, with a bit more detail. Looked best here with large resolution jumps.",
                L"Model J — newer (transformer)\nRebuilds more detail, but relies on the game's motion vectors — which screen capture doesn't have.",
                L"Model K — newer (transformer)\nThe newest, and the one that relies most on the data our capture path lacks.",
            };
            std::vector<Opc8> o;
            for (int i = 0; i < kNPreset; i++) {
                std::wstring tip = T(kDicaPt[i], kDicaEn[i]);
                if (g_moduloAmd) tip += TS(L"\nNo modo AMD não há preset do DLSS: quem amplia é o FSR 3.1 do portador.", L"\nIn AMD mode there is no DLSS preset: the carrier's FSR 3.1 does the upscaling.");
                o.emplace_back(i == 0 ? std::wstring(T(L"Padrão", L"Default")) : std::wstring(kPresetNome[i]), E_PRESET, i, kPreset[i] == g_hud.presetIa, !g_moduloAmd, tip);
            }
            const REAL wc = V8ChipsLargura(o, 0.f);
            if (g) V8Rotulo(g, L, cy, IW - wc - 12.f, h, TS(L"Como a IA reconstrói", L"Reconstruction preset"), std::wstring(), !g_moduloAmd);
            V8Chips(g, R - wc, cy + (h - kCtrl8) / 2.f, wc, kCtrl8, o);
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Nitidez (barra): o RCAS do feeder, depois de a IA ampliar (E_NITIDEZBARRA)
        {
            const REAL h = kLinha8, wv = 44.f, wb = 170.f;
            const bool age = (g_hud.srEfetivo != 0);
            const RectF rb(R - wv - 10.f - wb, cy + 4.f, wb, h - 8.f);
            const int idx = (int)g_items.size();
            g_items.push_back(Item{ rb, E_NITIDEZBARRA, 0, age, false,
                                    TS(L"Nitidez\nUm realce aplicado DEPOIS de a IA ampliar — é o que os jogos chamam de \"nitidez do DLSS\". Não cria detalhe: realça o contorno "
                                       L"do que já existe; demais deixa halo. 0 desliga o passe · 88 é a referência da AMD. Não custa FPS e vale na hora — arraste e olhe.",
                                       L"Sharpness\nA sharpening pass applied AFTER the AI upscales — what games call \"DLSS sharpness\". It creates no detail: it enhances the "
                                       L"edges of what is already there; too much leaves halos. 0 turns it off · 88 is AMD's reference. Costs no FPS and applies instantly — drag and watch.")
                                    + (age ? std::wstring() : TS(L"\nSó age com o upscaling ligado (Upscaling pela rede ≠ Não).", L"\nOnly works with upscaling on (Network upscaling ≠ Off).")) });
            if (g) {
                V8Rotulo(g, L, cy, rb.X - L - 12.f, h, TS(L"Nitidez", L"Sharpness"), std::wstring(), age);
                V8Barra(g, rb, (REAL)g_hud.nitidezIa / 100.f, age, g_hot == idx || g_barra == idx);
                wchar_t v[16]; _snwprintf(v, 16, L"%d%%", g_hud.nitidezIa); v[15] = 0;
                Txt(g, v, f8CtrlB, age ? K8_TXT : K8_SEC, RectF(R - wv, cy, wv, h), 2);
            }
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Ampliador (segmentado): quem leva a CAPTURA ate a tela, do lado do app
        {
            const REAL h = kLinha8;
            const wchar_t* dica = T(L"Ampliador\nQuem leva a captura até os pixels do monitor quando ela é menor que a tela. Padrão: o ampliador direcional do programa "
                                    L"(não é DLSS — quem faz o DLSS ampliar é o \"Upscaling pela rede\"). FSR 1: o filtro espacial da AMD, ~0,5 ms e zero VRAM. "
                                    L"FSR 3.1: a parte espacial do upscaler da fluidez; custa mais e ocupa ~300 MB. Vale na hora.",
                                    L"Upscaler\nScales the capture up to the monitor's resolution when it is smaller than the screen. Default: the app's directional upscaler "
                                    L"(not DLSS — DLSS upscaling is \"Network upscaling\"). FSR 1: AMD's spatial filter, ~0.5 ms, no extra VRAM. "
                                    L"FSR 3.1: the spatial part of the Frame Generation upscaler; heavier, uses ~300 MB. Applies instantly.");
            std::wstring d = dica;
            if (!g_hud.upMotivo.empty()) d += TS(L"\nAgora: caiu para o caminho de sempre — ", L"\nCurrently: fell back to the default path — ") + g_hud.upMotivo;
            std::vector<Opc8> o;
            o.emplace_back(T(L"Padrão", L"Default"), E_UPSCALER, 0, g_hud.upPedido == 0, true, d);
            o.emplace_back(L"FSR 1", E_UPSCALER, 1, g_hud.upPedido == 1, true, d);
            o.emplace_back(L"FSR 3.1", E_UPSCALER, 2, g_hud.upPedido == 2, true, d);
            const REAL ws = V8SegLargura(o);
            if (g) V8Rotulo(g, L, cy, IW - ws - 12.f, h, TS(L"Ampliador", L"Upscaler"), std::wstring());
            V8Seg(g, R - ws, cy + (h - kCtrl8) / 2.f, ws, kCtrl8, o);
            cy += h;
        }
    }

    // ================================================================ FLUIDEZ
    if (g_abaAtual == ABA_FLUIDEZ) {
        V8Secao(g, L, IW, cy, T(L"GERAÇÃO DE QUADROS", L"FRAME GENERATION"));
        // ---- Geracao de quadros: Desligado · 2x · 4x (E_FLUIDEZMODO 0 / E_FLUIDEZMULT 0 e 2)
        {
            const REAL h = kLinha8;
            const std::wstring modoIni = IniV8().fluidezModo;
            const int selModo = g_fluidezRegraEmulador ? 0 : (modoIni == L"interpolada") ? 2 : (modoIni == L"projetada") ? 1 : 0;
            const double chegadaFm = ChegadaFluidez();
            const bool podeInterp = (chegadaFm >= 10.0) || (selModo == 2);
            const int multSel = FluidezMultFg();
            const double hzT = g_hud.fgTelaHz > 1.0 ? g_hud.fgTelaHz : (double)RefreshDoMonitor();
            auto dicaN = [&](int mult) {
                wchar_t dq[400];
                const double real = chegadaFm >= 1.0 ? chegadaFm : 0.0;
                const double sai = real > 0.0 ? std::min((double)mult * real, hzT > 1.0 ? hzT : (double)mult * real) : 0.0;
                if (!podeInterp)
                    _snwprintf(dq, 400, T(L"%d×\nLiga quando houver medida (≥ 10 quadros reais por segundo). Chegada agora: %.0f fps.",
                                          L"%d×\nAvailable once the input reaches 10 real frames per second. Current input: %.0f fps."), mult, chegadaFm);
                else if (real > 0.0)
                    _snwprintf(dq, 400, T(L"%d×\n%d quadro(s) gerado(s) para cada real: %.0f reais → ~%.0f na tela (limite do monitor: %.0f Hz). Latência: +1 quadro real.",
                                          L"%d×\n%d generated frame(s) per real frame: %.0f real → ~%.0f on screen (monitor limit: %.0f Hz). Adds one real frame of latency."), mult, mult - 1, real, sai, hzT);
                else
                    _snwprintf(dq, 400, T(L"%d×\n%d quadro(s) gerado(s) para cada quadro real, limitado à taxa do monitor.",
                                          L"%d×\n%d generated frame(s) per real frame, capped at the monitor's rate."), mult, mult - 1);
                dq[399] = 0;
                return std::wstring(dq);
            };
            std::vector<Opc8> o;
            o.emplace_back(T(L"Desligado", L"Off"), E_FLUIDEZMODO, 0, selModo != 2, true,
                           TS(L"Desligado\nSem geração de quadros: a tela mostra só o que a rede entrega.", L"Off\nNo Frame Generation: the screen shows only what the network delivers."));
            o.emplace_back(L"2×", E_FLUIDEZMULT, 0, selModo == 2 && multSel == 2, podeInterp, dicaN(2));
            o.emplace_back(L"4×", E_FLUIDEZMULT, 2, selModo == 2 && multSel == 4, podeInterp, dicaN(4));
            const REAL ws = V8SegLargura(o);
            if (g) V8Rotulo(g, L, cy, IW - ws - 12.f, h, TS(L"Geração de quadros", L"Frame Generation"), std::wstring());
            V8Seg(g, R - ws, cy + (h - kCtrl8) / 2.f, ws, kCtrl8, o);
            cy += h;
            // a linha viva (uma so): o que a geracao de quadros esta fazendo agora
            wchar_t lv[200];
            Color cor = K8_SEC;
            if (g_hud.fgAtivo && g_hud.fgFonteParada)
                _snwprintf(lv, 200, T(L"à espera · o jogo está parado, nada a inventar", L"Waiting · the game is paused, nothing to generate"));
            else if (g_hud.fgAtivo && g_hud.fgPausado)
                { _snwprintf(lv, 200, T(L"em pausa · chegada %.0f fps", L"Paused · input %.0f fps"), chegadaFm); cor = K8_AVISO; }
            // BUG 9: a geracao esta LIGADA mas o monitor nao tem vblank livre para os quadros gerados: em 2x
            // cada gerado precisa de um vblank ENTRE dois reais (apresentador_gpu.h, "1 vblank entre os reais:
            // nao ha vaga"), entao com o jogo acima de ~metade do refresh quase todos sao descartados; em 4x
            // a meta (4 x jogo) passa do refresh e o apresentador cai para "sem gerados" abaixo de R = 1,25.
            // Sem este aviso a linha dizia "ativa" e a pessoa via "60 -> 60" sem saber por que nao mudou.
            else if (g_hud.fgAtivo && !g_hud.fgPausado && hzT > 1.0 && chegadaFm > (multSel == 2 ? 0.55 : 0.8) * hzT)
                { _snwprintf(lv, 200, T(L"ativa · %.0f → %.0f na tela · o jogo (%.0f) já enche o monitor (%.0f Hz): quase não há vaga p/ gerados",
                                        L"Active · %.0f → %.0f on screen · the game (%.0f) already fills the monitor (%.0f Hz): little room for generated frames"),
                             chegadaFm, g_hud.fgApresFps, chegadaFm, hzT);
                  cor = K8_AVISO; }
            else if (g_hud.fgAtivo)
                { _snwprintf(lv, 200, T(L"ativa · %.0f → %.0f na tela · latência ≈ %.0f ms", L"Active · %.0f → %.0f fps · latency ≈ %.0f ms"), chegadaFm, g_hud.fgApresFps, g_hud.fgLatMs);
                  cor = (g_hud.fgApresFps >= 0.85 * (g_hud.fgMultEf >= 2 ? g_hud.fgMultEf : 2) * chegadaFm) ? K8_ACC : K8_AVISO; }
            else
                _snwprintf(lv, 200, T(L"chegada %.0f fps · a geração precisa de ≥ 10", L"Input %.0f fps · needs at least 10 fps"), chegadaFm);
            lv[199] = 0;
            g_items.push_back(Item{ RectF(L, cy - 6.f, IW, 20.f), E_NONE, 0, false, false,
                                    TS(L"Geração de quadros\nO programa inventa quadros entre dois reais da rede (FSR 3.1 Frame Interpolation). Em verde, em ritmo; em âmbar, abaixo do esperado.",
                                       L"Frame Generation\nThe app generates frames between two real ones from the network (FSR 3.1 Frame Interpolation). Green means on pace; amber, below target.") });
            if (g) Txt(g, lv, f8Sub, cor, RectF(L, cy - 6.f, IW, 18.f), 0, true);
            cy += 16.f;
        }
        // ---- Frame Gen DO JOGO (Q4): FORA DO MENU por decisao do dono (item 5). O codigo fica; volta com
        //      [ultima] mostrar_fg_jogo=1 (e o g_fg do ini so vale com a chave - ver o arranque).
        if (IniV8().fgJogo && g_hud.podeIntegrar && CardFgVisivel()) {
            V8Div(g, L, IW, cy);
            const REAL h = kLinha8;
            const bool ativo = nativo;
            std::vector<Opc8> o;
            for (int i = 0; i < kNFg; i++)
                o.emplace_back(T(kFgRotPt[i], kFgRotEn[i]), E_FG, i, g_fg == kFgValor[i], ativo,
                               TS(L"Geração de quadros do jogo (modo Nativo)\nEntra quando o jogo reabre a imagem (Alt+Enter). Em troca, os comandos respondem uns milésimos mais tarde.",
                                  L"In-game Frame Generation (Native mode)\nKicks in when the game recreates its swap chain (Alt+Enter). In exchange, input responds a few milliseconds later."));
            const REAL ws = V8SegLargura(o);
            if (g) V8Rotulo(g, L, cy, IW - ws - 12.f, h, TS(L"Frame Gen do jogo", L"In-game Frame Generation"), std::wstring(), ativo);
            V8Seg(g, R - ws, cy + (h - kCtrl8) / 2.f, ws, kCtrl8, o);
            cy += h;
        }
        cy += 10.f;
        V8Secao(g, L, IW, cy, T(L"SINCRONIA E DESEMPENHO", L"SYNC & PERFORMANCE"));
        // ---- V-Sync (E_VSYNC)
        {
            const REAL h = kLinha8;
            const int idx = (int)g_items.size();
            g_items.push_back(Item{ RectF(L, cy, IW, h), E_VSYNC, 0, true, g_hud.vsync,
                                    TS(L"V-Sync (vale para tudo)\nLigado: espera o vblank — teto no refresh do monitor, mais latência. Desligado: não espera, e chega antes na tela. "
                                       L"Vale para a apresentação normal e para a geração de quadros.",
                                       L"V-Sync (applies everywhere)\nOn: waits for vblank — capped at the refresh rate, more latency. Off: no waiting, lower latency. "
                                       L"Applies to normal presentation and to Frame Generation.") });
            if (g) { V8Rotulo(g, L, cy, IW - kChave8 - 12.f, h, L"V-Sync", std::wstring()); V8Chave(g, R - kChave8, cy + h / 2.f, g_hud.vsync, true, g_hot == idx); }
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Folga de GPU (E_FOLGAGPU), com o estado embaixo
        {
            const bool on = Esteira::g_guardaChave != 0;
            const std::wstring sub = !g_hud.folgaEstado.empty() ? g_hud.folgaEstado
                                   : on ? TS(L"inativa: a fonte cabe", L"Idle — the source fits") : TS(L"desligada: só aviso", L"Off — warnings only");
            const REAL h = kLinhaSub8;
            const int idx = (int)g_items.size();
            g_items.push_back(Item{ RectF(L, cy, IW, h), E_FOLGAGPU, 0, true, on,
                                    TS(L"Folga de GPU\nCom a fonte acima do que a rede processa com folga, o app admite a captura num passo inteiro da tela para a placa não passar de ~75% — "
                                       L"é o que evita os reinícios da placa (nvlddmkm 153). Desligada: o app processa tudo o que couber e só avisa.",
                                       L"GPU headroom\nWhen the source delivers more than the network can process with headroom, the app takes captures in whole-refresh steps to keep the GPU under ~75% — "
                                       L"this is what prevents GPU resets (nvlddmkm 153). Off: the app processes everything it can and only warns.") });
            if (g) { V8Rotulo(g, L, cy, IW - kChave8 - 12.f, h, TS(L"Folga de GPU", L"GPU headroom"), sub); V8Chave(g, R - kChave8, cy + h / 2.f, on, true, g_hot == idx); }
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Mostrar FPS na tela (E_FPSTELA)
        {
            const REAL h = kLinha8;
            const int idx = (int)g_items.size();
            const std::wstring onde = nativo ? TS(L"contador no canto do jogo", L"Counter in the game's corner")
                                   : g_hud.fpsApres ? TS(L"contador no canto: fps do jogo → fps na tela", L"Corner counter: game FPS → screen FPS")
                                   : TS(L"o contador do ReShade no canto — pode sumir só ao reabrir", L"ReShade's corner counter — may only change after a restart");
            g_items.push_back(Item{ RectF(L, cy, IW, h), E_FPSTELA, 0, true, g_hud.fpsTela, TS(L"Mostrar FPS na tela\n", L"Show FPS counter\n") + onde });
            if (g) { V8Rotulo(g, L, cy, IW - kChave8 - 12.f, h, TS(L"Mostrar FPS na tela", L"Show FPS counter"), std::wstring()); V8Chave(g, R - kChave8, cy + h / 2.f, g_hud.fpsTela, true, g_hot == idx); }
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Limite de fps: Monitor · Livre · Escolhido (E_RITMO); no Nativo o ritmo e sempre livre
        {
            const REAL h = kLinha8;
            std::wstring d = TS(L"Limite de fps da película\nMonitor (padrão): uma volta por atualização de tela, sempre com o quadro mais novo — latência mínima, fps preso no refresh. "
                                L"Livre: nada espera nada; o fps que você lê é o teto real da cadeia. Escolhido: só o seu número segura. Vale na hora.",
                                L"FPS limit\nRefresh rate (default): one cycle per screen refresh, always with the newest frame — lowest latency, FPS capped at the refresh rate. "
                                L"Uncapped: nothing waits; the FPS you see is the pipeline's real ceiling. Custom: only your limit applies. Applies instantly.");
            if (nativo) d += TS(L"\nNo modo Nativo o ritmo é sempre livre: quem apresenta é o jogo.", L"\nIn Native mode pacing is always uncapped: the game presents the frames.");
            std::vector<Opc8> o;
            o.emplace_back(T(L"Monitor", L"Refresh rate"), E_RITMO, 0, g_hud.ritmoModo == 0, !nativo, d);
            o.emplace_back(T(L"Livre", L"Uncapped"), E_RITMO, 1, g_hud.ritmoModo == 1, !nativo, d);
            o.emplace_back(T(L"Escolhido", L"Custom"), E_RITMO, 2, g_hud.ritmoModo == 2, !nativo, d);
            const REAL ws = V8SegLargura(o);
            if (g) V8Rotulo(g, L, cy, IW - ws - 12.f, h, TS(L"Limite de fps", L"FPS limit"), std::wstring(), !nativo);
            V8Seg(g, R - ws, cy + (h - kCtrl8) / 2.f, ws, kCtrl8, o);
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- o numero do "Escolhido" (barra, 10..240 de 5 em 5) - aparece com ele escolhido, ou com um teto
        //      gravado de antes (o [-]/[+] antigo valia em qualquer ritmo: um teto ativo nao pode ficar escondido)
        if ((g_hud.ritmoModo == 2 || g_hud.tetoFps > 0) && !nativo) {
            const REAL h = kLinha8, wv = 60.f, wb = 170.f;
            const RectF rb(R - wv - 10.f - wb, cy + 4.f, wb, h - 8.f);
            const int idx = (int)g_items.size();
            const int v = g_tetoArrasto > 0 ? g_tetoArrasto : (g_hud.tetoFps > 0 ? g_hud.tetoFps : 60);
            g_items.push_back(Item{ rb, E_TETOBARRA, 0, true, false,
                                    TS(L"Fps escolhido\nO teto de fps da película (fps_teto_pelicula). Acima dele a película nem processa: serve para economizar placa de propósito. "
                                       L"Arraste e solte para gravar; \"Livre\" tira o teto.",
                                       L"Custom limit\nThe overlay's FPS cap (fps_teto_pelicula). Above it the overlay doesn't even process frames — useful to save GPU power on purpose. "
                                       L"Drag and release to save; \"Uncapped\" removes the cap.") });
            if (g) {
                V8Rotulo(g, L, cy, rb.X - L - 12.f, h, TS(L"Fps escolhido", L"Custom limit"), std::wstring());
                V8Barra(g, rb, (REAL)(v - kTetoMin) / (REAL)(240 - kTetoMin), true, g_hot == idx || g_barra == idx);
                wchar_t t[16]; _snwprintf(t, 16, L"%d fps", v); t[15] = 0;
                Txt(g, t, f8CtrlB, K8_TXT, RectF(R - wv, cy, wv, h), 2);
            }
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Resolucao do jogo: Original · 720p · 1080p · 1440p (E_JOGORES; so com a fonte Janela)
        {
            const REAL h = kLinha8;
            const bool vale = g_hud.fonteJanela && !g_hud.jogoDesistiu;
            std::wstring d = TS(L"Resolução do jogo\nMuda o tamanho da JANELA do jogo — o tamanho em que ele desenha. Só funciona com o jogo em janela COM BORDAS "
                                L"(a borda não aparece: a película cobre a tela). O tamanho original volta quando você sai. Vale na hora.",
                                L"Game resolution\nChanges the size of the game's WINDOW — the size it renders at. Only works with the game in a BORDERED window "
                                L"(the border doesn't show: the overlay covers the screen). The original size is restored when you quit. Applies instantly.");
            if (!g_hud.fonteJanela) d += TS(L"\nSó com a fonte Janela.", L"\nOnly with a Window source.");
            if (g_hud.jogoDesistiu) d += TS(L"\nO jogo devolveu o tamanho dele duas vezes: mude dentro do próprio jogo.", L"\nThe game reverted its size twice — change it in the game's own settings.");
            if (!g_hud.jogoAviso.empty()) d += L"\n" + g_hud.jogoAviso;
            static const int kIdx[4] = { 0, 1, 3, 4 };   // Original, 720p, 1080p, 1440p (900p e 2160p ficam no seletor)
            bool algum = false;
            for (int k = 1; k < 4; k++) if (g_hud.jogoAlturaAtual == kJogoRes[kIdx[k]]) algum = true;
            std::vector<Opc8> o;
            for (int k = 0; k < 4; k++) {
                const int i = kIdx[k];
                const bool sel = vale && (kJogoRes[i] > 0 ? g_hud.jogoAlturaAtual == kJogoRes[i] : !algum);
                o.emplace_back(kJogoRes[i] == 0 ? std::wstring(T(L"Original", L"Original")) : std::to_wstring(kJogoRes[i]) + L"p", E_JOGORES, i, sel, vale, d);
            }
            const REAL ws = V8SegLargura(o);
            if (g) V8Rotulo(g, L, cy, IW - ws - 12.f, h, TS(L"Resolução do jogo", L"Game resolution"), std::wstring(), vale);
            V8Seg(g, R - ws, cy + (h - kCtrl8) / 2.f, ws, kCtrl8, o);
            cy += h;
        }
        cy += 10.f;
        V8Secao(g, L, IW, cy, T(L"DESEMPENHO", L"PERFORMANCE"));
        // ---- DLSS Frame Skipping (chips): Todos · 1/2 · 1/3 · 1/4 (E_ALTERNA; nao existe no Nativo)
        {
            const REAL h = kLinha8;
            const bool pode = SkipQuadrosPode();
            const int razao = (g_hud.alternaN > g_hud.puloN) ? g_hud.alternaN : g_hud.puloN;
            std::wstring d = TS(L"DLSS Frame Skipping\nSó um quadro de cada grupo é nosso; nos outros a camada some e aparece o jogo ao vivo. Alivia a placa, e a tela continua andando no ritmo cheio.",
                                L"DLSS Frame Skipping\nOnly one frame per group is processed; on the others the overlay steps aside and the live game shows through. Lightens the GPU load while the screen keeps its full frame rate.");
            if (!g_hud.sintetiza) d += TS(L"\nPode tremular: um quadro tem DLSS, o seguinte é o jogo cru.", L"\nMay flicker: one frame has DLSS, the next is the raw game.");
            if (!pode) d += TS(L"\nNo modo Nativo não existe: o quadro volta ao jogo 1:1.", L"\nNot available in Native mode: frames go back to the game 1:1.");
            if (g_hud.alternaN > 1 && g_hud.puloN > 1) d += TS(L"\nUma chave antiga do ini ainda pula quadro por baixo: os dois se multiplicam.", L"\nAn old ini setting also skips frames underneath: the two multiply.");
            static const wchar_t* kRot[4] = { L"", L"1/2", L"1/3", L"1/4" };
            std::vector<Opc8> o;
            const int nAlt = (int)(sizeof(kAlterna) / sizeof(kAlterna[0]));
            for (int i = 0; i < nAlt && i < 4; i++)
                o.emplace_back(i == 0 ? std::wstring(T(L"Todos", L"All")) : std::wstring(kRot[i]), E_ALTERNA, i, razao == kAlterna[i], pode, d);
            const REAL wc = V8ChipsLargura(o, 0.f);
            if (g) V8Rotulo(g, L, cy, IW - wc - 12.f, h, TS(L"DLSS Frame Skipping", L"DLSS Frame Skipping"), std::wstring(), pode);
            V8Chips(g, R - wc, cy + (h - kCtrl8) / 2.f, wc, kCtrl8, o);
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Desempenho maximo (E_DESEMPMAX), com o estado embaixo
        {
            const bool ligado = IniV8().desempMax;
            const bool precisaAdmin = Desempenho::Max::PrecisaAdmin();
            const bool gpuOn = Desempenho::Max::GpuAtiva(), cpuOn = Desempenho::Max::CpuAtiva();
            const std::wstring sub = precisaAdmin ? TS(L"ative \"Preferir desempenho máximo\" no Painel NVIDIA", L"Set \"Prefer maximum performance\" in NVIDIA Control Panel")
                                   : !ligado ? TS(L"desligado", L"Off")
                                   : (gpuOn && cpuOn) ? TS(L"aplicado agora (GPU e CPU)", L"Active (GPU + CPU)")
                                   : cpuOn ? TS(L"aplicado agora (só CPU)", L"Active (CPU only)")
                                   : TS(L"vale quando a película ficar ativa", L"Applies when the overlay is active");
            const REAL h = kLinhaSub8;
            const int idx = (int)g_items.size();
            g_items.push_back(Item{ RectF(L, cy, IW, h), E_DESEMPMAX, 0, true, ligado,
                                    TS(L"Desempenho máximo\nEnquanto a película estiver ativa: pede ao driver da NVIDIA para preferir desempenho máximo (só no perfil deste programa) "
                                       L"e tira este processo do power throttling do Windows. Sem placa NVIDIA, só a parte da CPU vale. Ligado por padrão.",
                                       L"Maximum performance\nWhile the overlay is active: asks the NVIDIA driver to prefer maximum performance (in this app's own profile only) "
                                       L"and exempts this process from Windows power throttling. Without an NVIDIA GPU, only the CPU part applies. On by default.")
                                    + (precisaAdmin ? TS(L"\nO perfil do driver pede administrador: ative à mão para ", L"\nChanging the driver profile needs admin rights — enable it manually for ") + Desempenho::Max::NossoExe() : std::wstring()) });
            if (g) { V8Rotulo(g, L, cy, IW - kChave8 - 12.f, h, TS(L"Desempenho máximo", L"Maximum performance"), sub, true, precisaAdmin ? &K8_AVISO : nullptr);
                     V8Chave(g, R - kChave8, cy + h / 2.f, ligado, true, g_hot == idx); }
            cy += h;
        }
    }

    // ================================================================ AVANCADO
    if (g_abaAtual == ABA_AVANCADO) {
        V8Secao(g, L, IW, cy, T(L"PROCESSAMENTO", L"PROCESSING"));
        // ---- Modo: Automatico · Upstream · Nativo · Universal (sempre visiveis; esmaecidos onde nao valem)
        {
            const bool pode = g_hud.podeIntegrar;
            const bool en = pode && !g_hud.integrando;
            const bool nativoOk = (g_nativoVeto == 0) && !AntiCheatAtivo();
            const bool upstreamOk = nativoOk && (g_modoEscolha == MODO_UPSTREAM || (g_upstreamFalhas < 2 && UpstreamNoJogo()));
            const bool upAtivo = g_hud.modoIntegrado && FonteUpstream::Ativa() && FonteUpstream::SessaoNegociada() && !g_upstreamPausado;
            const bool upEsperando = g_hud.modoIntegrado && !g_hud.integrando && g_nativoUpstream && PonteUp::Ativa() && !FonteUpstream::SessaoNegociada() && !g_upstreamRodou;
            const wchar_t* emUso = g_hud.integrando ? T(L"LIGANDO…", L"TURNING ON…")
                                 : (upAtivo && FonteUpstream::SessaoSr() && FonteUpstream::CadeiaNoUpstream()) ? L"UPSTREAM"
                                 : (g_hud.modoIntegrado && FonteUpstream::SessaoSr() && !g_upstreamPausado) ? T(L"NATIVO · DLSS", L"NATIVE · DLSS")
                                 : upAtivo ? L"UPSTREAM"
                                 : (g_hud.modoIntegrado && g_upstreamPausado) ? T(L"NATIVO · UPSTREAM EM PAUSA", L"NATIVE · UPSTREAM PAUSED")
                                 : upEsperando ? T(L"NATIVO · ESPERANDO", L"NATIVE · WAITING")
                                 : g_hud.modoIntegrado ? T(L"NATIVO", L"NATIVE") : L"UNIVERSAL";
            std::wstring dicaSelo = TS(L"Modo em uso\nO transporte que está valendo agora (os botões dizem a ESCOLHA).", L"Mode in use\nThe mode actually running right now (the buttons show your CHOICE).");
            if (!pode) dicaSelo += TS(L"\nOs modos valem quando a fonte é a janela de um jogo; esta fonte usa sempre o Universal.", L"\nModes only apply when the source is a game window; this source always uses Universal.");
            else if (!nativoOk) dicaSelo += L"\n" + MotivoNativo();
            else if (g_vn.dica > 0) dicaSelo += L"\n" + VnDica(g_vn.dica);
            if (g_upstreamPausado || upEsperando) dicaSelo += TS(L"\nAtive o DLSS, o FSR ou o XeSS nas opções de imagem do jogo para usar o Upstream.", L"\nTurn on DLSS, FSR or XeSS in the game's display options to use Upstream.");
            // linha 1: "Modo" + o selo do modo EM USO a direita
            const REAL wSelo = Larg8(emUso, f8Secao) + 18.f;
            const RectF rSelo(R - wSelo, cy + 8.f, wSelo, 20.f);
            g_items.push_back(Item{ rSelo, E_NONE, 0, false, false, dicaSelo });
            if (g) {
                V8Rotulo(g, L, cy + 4.f, IW - wSelo - 12.f, 28.f, TS(L"Modo", L"Mode"), std::wstring());
                StrokeRound(g, RectF(rSelo.X + .5f, rSelo.Y + .5f, rSelo.Width - 1.f, rSelo.Height - 1.f), 4.f, g_hud.integrando ? K8_ACCH : K8_ACC, 1.f);
                Txt(g, emUso, f8Secao, g_hud.integrando ? K8_ACCH : K8_ACC, rSelo, 1, true);
            }
            cy += 34.f;
            // linha 2: o segmentado, largura inteira
            auto motivo = [&](int m) -> std::wstring {
                if (!pode) return TS(L"Indisponível: a fonte não é a janela de um jogo.", L"Unavailable: the source is not a game window.");
                if (m == MODO_NATIVO && !nativoOk) return MotivoNativo();
                if (m == MODO_UPSTREAM && !upstreamOk)
                    return !nativoOk ? MotivoNativo()
                         : g_upstreamFalhas >= 2 ? TS(L"Este jogo não aceitou o DLSS 5 Upstream duas vezes. Escolher aqui tenta de novo.", L"DLSS 5 Upstream failed twice in this game. Select it to try again.")
                         : TS(L"Indisponível: este jogo não tem DLSS, FSR ou XeSS que o DLSS 5 consiga usar (ou o upscaling está desligado no jogo).",
                              L"Unavailable: this game has no DLSS, FSR or XeSS that DLSS 5 can hook into (or upscaling is off in the game).");
                return std::wstring();
            };
            static const int ordem[3] = { MODO_UPSTREAM, MODO_NATIVO, MODO_UNIVERSAL };   // v2.0: sem Automatico
            std::vector<Opc8> o;
            for (int k = 0; k < 3; k++) {
                const int m = ordem[k];
                const bool vale = en && (m == MODO_NATIVO ? nativoOk : m == MODO_UPSTREAM ? upstreamOk : true);
                std::wstring tip = std::wstring(m == MODO_AUTO ? T(L"Automático", L"Auto") : m == MODO_NATIVO ? T(L"Nativo", L"Native") : m == MODO_UPSTREAM ? L"Upstream" : L"Universal")
                                   + L"\n" + ExplicaModo8(m);
                const std::wstring mo = motivo(m);
                if (!mo.empty()) tip += L"\n" + mo;
                if (m == MODO_NATIVO && g_vn.falhasPerfil > 0 && g_vn.dica == 0) tip += TS(L"\nO modo Nativo teve problemas neste jogo da última vez.", L"\nNative mode had problems in this game last time.");
                o.emplace_back(m == MODO_AUTO ? T(L"Automático", L"Auto") : m == MODO_NATIVO ? T(L"Nativo", L"Native") : m == MODO_UPSTREAM ? L"Upstream" : L"Universal",
                               E_MODOPROC, m, g_modoEscolha == m, vale, tip);
            }
            V8Seg(g, L, cy, IW, kCtrl8, o);
            cy += kCtrl8 + 6.f;
            // linha 3: a explicacao do modo escolhido (altura reservada pela mais longa: a aba nao pula)
            {
                REAL hMax = 0.f;
                for (int m = 0; m < 4; m++) { const REAL t = AlturaTexto(ExplicaModo8(m), f8Sub, IW); if (t > hMax) hMax = t; }
                if (g) TxtWrap(g, ExplicaModo8(g_modoEscolha), f8Sub, K8_SEC, RectF(L, cy, IW, hMax + 2.f));
                cy += hMax + 8.f;
            }
            V8Div(g, L, IW, cy);
        }
        // ---- DLSS do jogo: preset (chips Auto · A..L) e qualidade (chips) - pela DLL do modo Nativo
        {
            const bool ponte = nativo;
            const bool srApp = ponte && FonteUpstream::SessaoSr();
            const bool ativo = ponte && CardDlssJogoVisivel() && ((g_dlssJogo.lido && g_dlssJogo.ngx) || srApp);
            std::wstring motivo;
            if (!g_hud.podeIntegrar)   motivo = TS(L"Indisponível: só no modo Nativo, com a janela de um jogo.", L"Unavailable: Native mode only, with a game window.");
            else if (!ponte)           motivo = TS(L"Indisponível: a ponte não está ativa (modo Nativo).", L"Unavailable: the bridge is not active (Native mode).");
            else if (srApp)            motivo = TS(L"O upscaling FSR/XeSS do jogo foi trocado pelo DLSS: estas opções valem para ele.", L"The game's FSR/XeSS upscaling was replaced by DLSS: these options apply to it.");
            else if (!g_dlssJogo.lido) motivo = TS(L"Aguardando a DLL dentro do jogo…", L"Waiting for the DLL inside the game…");
            else if (!g_dlssJogo.ngx)  motivo = TS(L"Indisponível: este jogo não carregou o NGX (sem DLSS).", L"Unavailable: this game has not loaded NGX (no DLSS).");
            else if (!g_dlssJogo.sr)   motivo = TS(L"O NGX está no jogo, mas ainda não há DLSS Super Resolution em uso.", L"NGX is in the game, but no DLSS Super Resolution is in use yet.");
            // o que esta EM USO (segundo a DLL, ou a nossa feature na sessao 0x200)
            std::wstring selo;
            if (srApp) {
                const char p = DlssSrApp::PresetEmUso();
                selo = DlssSrApp::Ativa() ? std::wstring(1, (wchar_t)(p == '-' ? L'?' : p)) : std::wstring(L"DLSS");
            } else if (ativo && g_dlssJogo.sr) {
                selo = g_dlssJogo.preset.empty() || g_dlssJogo.preset == L"-" ? std::wstring(L"?") : g_dlssJogo.preset;
                for (int q = 0; q < kNDlssQual; q++) if (g_dlssJogo.qualidade == kDlssQual[q].chave) selo += std::wstring(L" · ") + T(kDlssQual[q].pt, kDlssQual[q].en);
            }
            for (auto& ch : selo) ch = (wchar_t)towupper(ch);
            // o rotulo em cima dos chips; o que esta EM USO, a direita, na mesma linha
            const std::wstring emUsoJ = selo.empty() ? std::wstring() : TS(L"em uso: ", L"in use: ") + selo;
            if (g) {
                V8Rotulo(g, L, cy + 2.f, IW * 0.6f, 28.f, TS(L"DLSS do jogo", L"In-game DLSS"), std::wstring(), ativo);
                if (!emUsoJ.empty()) Txt(g, emUsoJ.c_str(), f8Sub, K8_ACC, RectF(L + IW * 0.4f, cy + 2.f, IW * 0.6f, 28.f), 2, true);
            }
            g_items.push_back(Item{ RectF(L, cy + 2.f, IW, 28.f), E_NONE, 0, false, false,
                                    TS(L"DLSS do jogo\nPreset e qualidade do DLSS do PRÓPRIO jogo, pela DLL do modo Nativo: escolher recria a feature ao vivo.\n",
                                       L"In-game DLSS\nPreset and quality of the game's OWN DLSS, via the Native mode DLL: changing them recreates the feature live.\n") + motivo });
            cy += 32.f;
            std::vector<Opc8> o;
            for (int i = 0; i < kNDlssPresets; i++) {
                std::wstring tip = i == 0 ? TS(L"Preset: automático\nO jogo (ou o driver) escolhe o preset, como sempre.", L"Preset: auto\nThe game (or the driver) picks the preset, as usual.")
                                          : TS(L"Preset ", L"Preset ") + kDlssPresets[i] + TS(L"\nTroca o preset do DLSS do jogo ao vivo; a build do nvngx_dlss.dll decide quais existem.",
                                                                                              L"\nSwitches the game's DLSS preset live; the nvngx_dlss.dll build determines which ones exist.");
                if (!ativo && !motivo.empty()) tip += L"\n" + motivo;
                o.emplace_back(i == 0 ? std::wstring(T(L"Auto", L"Auto")) : std::wstring(kDlssPresets[i]), E_DLSSPRESET, i, g_dlssPreset == kDlssPresets[i], ativo, tip);
            }
            cy += V8Chips(g, L, cy, IW, kCtrl8, o, (IW - 6.f * (kNDlssPresets - 1)) / (REAL)kNDlssPresets) + 8.f;
            V8Div(g, L, IW, cy);
            // qualidade: "Jogo · Qualidade · Equilibrado · Desempenho · Ultra · DLAA"
            {
                const REAL h = kLinha8;
                std::vector<Opc8> q;
                for (int i = 0; i < kNDlssQual; i++) {
                    std::wstring tip = std::wstring(T(kDlssQual[i].pt, kDlssQual[i].en)) +
                        (i == 0 ? TS(L"\nO DLSS roda no modo escolhido dentro do jogo.", L"\nDLSS uses the mode selected in the game.")
                         : i == 5 ? TS(L"\nSó anti-aliasing: a rede roda na resolução de saída.", L"\nAnti-aliasing only: the network runs at output resolution.")
                                  : TS(L"\nRecria a feature do DLSS ao vivo com este modo; a resolução em que o jogo desenha não muda.", L"\nRecreates the DLSS feature live with this mode; the game's render resolution does not change."));
                    if (!ativo && !motivo.empty()) tip += L"\n" + motivo;
                    const std::wstring rot = i == 4 ? std::wstring(T(L"Ultra", L"Ultra")) : std::wstring(T(kDlssQual[i].pt, kDlssQual[i].en));
                    q.emplace_back(rot, E_DLSSQUAL, i, g_dlssQualidade == kDlssQual[i].chave, ativo, tip);
                }
                const REAL wq = V8ChipsLargura(q, 0.f, 10.f);
                if (wq <= IW - 90.f) {
                    if (g) V8Rotulo(g, L, cy, IW - wq - 12.f, h, TS(L"Qualidade", L"Quality"), std::wstring(), ativo);
                    V8Chips(g, R - wq, cy + (h - kCtrl8) / 2.f, wq, kCtrl8, q, 0.f, false, 10.f);
                    cy += h;
                } else {   // os seis nomes nao cabem ao lado do rotulo: rotulo em cima, chips numa fila embaixo
                    if (g) V8Rotulo(g, L, cy + 2.f, IW, 26.f, TS(L"Qualidade", L"Quality"), std::wstring(), ativo);
                    cy += 30.f;
                    cy += V8Chips(g, L, cy, IW, kCtrl8, q, 0.f, false, 10.f) + 8.f;
                }
            }
            V8Div(g, L, IW, cy);
        }
        // ---- Motor da rede: RenoDX · Proprio (no modulo AMD: o mod, e o orcamento de espera dele)
        if (g_moduloAmd) {
            const REAL h = kLinhaSub8;
            const bool temIni = ArquivoExiste(IniModAmd());
            g_items.push_back(Item{ RectF(L, cy, IW, h), E_NONE, 0, false, false,
                                    TS(L"Motor da rede no modo AMD\nQuem faz o Neural Rendering aqui é o mod DLSS-NR-on-AMD, de terceiro. Tom local, estrutura e pele vão para o dlssnr_on_amd.ini "
                                       L"e valem quando o programa reabre; modelo, intensidade, passes e o preset do DLSS não existem neste caminho.",
                                       L"Network engine in AMD mode\nThe Neural Rendering here is done by the third-party DLSS-NR-on-AMD mod. Local tone, structure and skin go to dlssnr_on_amd.ini "
                                       L"and apply after a restart; model, intensity, passes and the DLSS preset don't exist on this path.") });
            if (g) V8Rotulo(g, L, cy, IW, h, TS(L"Motor da rede", L"Network engine"),
                            temIni ? TS(L"mod DLSS-NR-on-AMD (terceiro)", L"DLSS-NR-on-AMD mod (third party)") : TS(L"o dlssnr_on_amd.ini não está nesta pasta", L"dlssnr_on_amd.ini is not in this folder"),
                            true, temIni ? nullptr : &K8_AVISO);
            cy += h; V8Div(g, L, IW, cy);
            // orcamento de espera (InlineWaitMs): passo de 5 ms - o unico controle de "passo" que sobrou, e so no modo AMD
            {
                const REAL hh = kLinha8, bw = 30.f, wn = 64.f;
                const bool pode = g_hud.amdEsperaPode;
                const std::wstring d = TS(L"Orçamento de espera por quadro\nQuanto o quadro espera pela rede antes de mostrar o anterior. Menor = mais fluido, com resíduo; maior = mais fiel, menos fps. "
                                          L"25 ms é o valor comprovado numa RX 9060 XT. Vale quando o programa reabre.",
                                          L"Per-frame wait budget\nHow long each frame waits for the network before the previous one is shown. Lower = smoother, with some ghosting; higher = more accurate, lower FPS. "
                                          L"25 ms is the tested value on an RX 9060 XT. Applies after a restart.");
                const REAL x0 = R - bw - 6.f - wn - 6.f - bw;
                if (g) V8Rotulo(g, L, cy, x0 - L - 12.f, hh, TS(L"Espera por quadro", L"Per-frame wait"), std::wstring(), pode);
                V8BotaoItem(g, RectF(x0, cy + (hh - kCtrl8) / 2.f, bw, kCtrl8), L"−", B8_CONTORNO, E_AMDESPERA, 0, pode && g_hud.amdEsperaMs > kEsperaMin, d);
                if (g) { wchar_t b[24]; _snwprintf(b, 24, pode ? L"%d ms" : L"— ms", g_hud.amdEsperaMs); b[23] = 0;
                         Txt(g, b, f8CtrlB, pode ? K8_TXT : K8_SEC, RectF(x0 + bw + 6.f, cy, wn, hh), 1); }
                V8BotaoItem(g, RectF(R - bw, cy + (hh - kCtrl8) / 2.f, bw, kCtrl8), L"+", B8_CONTORNO, E_AMDESPERA, 1, pode && g_hud.amdEsperaMs < kEsperaMax, d);
                cy += hh;
            }
        } else {
            const bool mostra = IniV8().renoDxNovo && (IniV8().recursosExp || (g_fonteCaptura && IniV8().feederRedeReduzida));
            std::wstring st;
            if (g_hud.stLido) {
                st = (g_hud.stMotor == L"proprio" ? TS(L"rodando: próprio", L"running: built-in") : TS(L"rodando: RenoDX", L"running: RenoDX"));
                if (g_hud.stPasses > 0) {
                    st += L" · " + std::to_wstring(g_hud.stPasses) + (g_hud.stPasses == 1 ? TS(L" passe", L" pass") : TS(L" passes", L" passes"));
                    if (g_hud.stPassesPedidos > g_hud.stPasses) st += TS(L" de ", L" of ") + std::to_wstring(g_hud.stPassesPedidos);
                }
                if (g_hud.stRemontPendente) st += TS(L" · nova escala ao reabrir o jogo", L" · new scale after the game restarts");
            }
            std::wstring d = TS(L"Motor da rede\nDuas implementações da MESMA rede. RenoDX: o padrão em jogo; o perfil vale depois que o programa se reabre. "
                                L"Próprio: o custo é o mesmo, mas dá CONTROLE dele (resolução de cada passe, recorte, IA antes de ampliar), borra menos com muitos passes "
                                L"e o perfil vale na hora. Trocar reabre o programa uma vez.",
                                L"Network engine\nTwo implementations of the SAME network. RenoDX: the default for games; settings apply after a restart. "
                                L"Built-in: same cost, but gives you CONTROL over it (per-pass resolution, cropping, AI before upscaling), blurs less with many passes, "
                                L"and settings apply instantly. Switching restarts the app once.");
            if (!mostra) d += TS(L"\nNesta sessão só o RenoDX está disponível (o próprio aparece em fonte de captura com o feeder novo, ou com recursos experimentais).",
                                 L"\nOnly RenoDX is available in this session (Built-in appears with a capture source and the new feeder, or with experimental features on).");
            if (!g_hud.iaAviso.empty()) d += TS(L"\nAgora: ", L"\nRight now: ") + g_hud.iaAviso;
            if (g_hud.stLido && !g_hud.stMotivo.empty()) d += L"\n" + g_hud.stMotivo;
            std::vector<Opc8> o;
            o.emplace_back(L"RenoDX", E_MOTOR, 0, !g_hud.motorProprio, mostra, d);
            o.emplace_back(T(L"Próprio", L"Built-in"), E_MOTOR, 1, g_hud.motorProprio, mostra, d);
            const REAL ws = V8SegLargura(o);
            const REAL h = st.empty() ? kLinha8 : kLinhaSub8;
            if (g) V8Rotulo(g, L, cy, IW - ws - 12.f, h, TS(L"Motor da rede", L"Network engine"), st, true, g_hud.stPassthrough ? &K8_AVISO : nullptr);
            V8Seg(g, R - ws, cy + (h - kCtrl8) / 2.f, ws, kCtrl8, o);
            cy += h;
        }
        cy += 10.f;
        V8Secao(g, L, IW, cy, T(L"SISTEMA", L"SYSTEM"));
        // ---- Capturar imagem processada [Capturar] (E_CAPTURAIMG)
        {
            const std::wstring st = CapImgStatusLer();
            const REAL h = kLinhaSub8;
            const std::wstring rot = T(L"Capturar", L"Capture");
            const REAL bw = V8BotaoW(rot);
            if (g) V8Rotulo(g, L, cy, IW - bw - 12.f, h, TS(L"Capturar imagem processada", L"Save processed screenshot"),
                            st.empty() ? TS(L"um PNG do que a película mostra", L"A PNG of what the overlay shows") : st);
            V8BotaoItem(g, RectF(R - bw, cy + (h - kCtrl8) / 2.f, bw, kCtrl8), rot, B8_CONTORNO, E_CAPTURAIMG, 0, true,
                        TS(L"Capturar imagem processada\nSalva em PNG o que a película mostra (a saída da rede e dos filtros), que o print do Windows não pega. "
                           L"Pasta: Imagens\\DLSS5ForAll (ou [ultima] captura_img_pasta).",
                           L"Save processed screenshot\nSaves a PNG of what the overlay shows (network and filter output), which Windows screenshots can't capture. "
                           L"Folder: Pictures\\DLSS5ForAll (or [ultima] captura_img_pasta)."));
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Reiniciar video do Windows [Reiniciar] (E_REINICIARVIDEO) - a mesma acao do seletor, com a mesma trava
        {
            const REAL h = kLinha8;
            const std::wstring rot = T(L"Reiniciar", L"Reset");
            const REAL bw = V8BotaoW(rot);
            if (g) V8Rotulo(g, L, cy, IW - bw - 12.f, h, TS(L"Reiniciar vídeo do Windows", L"Reset Windows graphics"), std::wstring());
            V8BotaoItem(g, RectF(R - bw, cy + (h - kCtrl8) / 2.f, bw, kCtrl8), rot, B8_CONTORNO, E_REINICIARVIDEO, 0, true,
                        TS(L"Reiniciar vídeo do Windows\nManda o atalho do Windows Win+Ctrl+Shift+B, que recria a imagem da área de trabalho (reiniciar o driver é efeito colateral e nem sempre acontece). "
                           L"A tela pisca e pode ficar preta por 1–2 s. Recusa se houver um jogo em tela cheia, e pede confirmação.",
                           L"Reset Windows graphics\nSends the Windows shortcut Win+Ctrl+Shift+B, which rebuilds the desktop image (a driver restart is a side effect and doesn't always happen). "
                           L"The screen flickers and may go black for 1–2 s. Refused while a game is in fullscreen, and asks for confirmation."));
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Resolucao por etapa [Mostrar] (E_TELEMETRIA): abre a janelinha ao lado
        {
            const REAL h = kLinha8;
            const std::wstring rot = g_hud.telemetria ? T(L"Ocultar", L"Hide") : T(L"Mostrar", L"Show");
            const REAL bw = V8BotaoW(T(L"Mostrar", L"Show")) > V8BotaoW(rot) ? V8BotaoW(T(L"Mostrar", L"Show")) : V8BotaoW(rot);
            if (g) V8Rotulo(g, L, cy, IW - bw - 12.f, h, TS(L"Resolução por etapa", L"Resolution by stage"), std::wstring());
            V8BotaoItem(g, RectF(R - bw, cy + (h - kCtrl8) / 2.f, bw, kCtrl8), rot, B8_CONTORNO, E_TELEMETRIA, g_hud.telemetria ? 1 : 0, true,
                        TS(L"Resolução por etapa\nO tamanho MEDIDO da imagem em cada etapa (jogo, captura, rede, tela) e quem desenha, processa e exibe — nome de opção não é medida.",
                           L"Resolution by stage\nThe MEASURED image size at each stage (game, capture, network, screen) and which GPU renders, processes and displays — a setting's name is not a measurement."));
            cy += h;
        }
        // ---- topologia fora do recomendado: o aviso fica a vista (o detalhe e a janelinha)
        if (!g_hud.topoRecomendado && (!g_hud.topoRede.empty() || !g_hud.topoMonitor.empty())) {
            V8Div(g, L, IW, cy);
            const REAL h = kLinhaSub8;
            g_items.push_back(Item{ RectF(L, cy, IW, h), E_NONE, 0, false, false,
                                    TS(L"Topologia\nA saída está na placa do jogo: a nossa ponte atravessa DUAS vezes por quadro (entrada e saída). Se puder, ligue o monitor na placa que processa.",
                                       L"Topology\nThe monitor is on the game's GPU: our bridge crosses PCIe TWICE per frame (in and out). If you can, plug the monitor into the GPU that runs the network.") });
            if (g) V8Rotulo(g, L, cy, IW, h, TS(L"Topologia", L"Topology"), TS(L"saída na placa do jogo — ligue o monitor na placa que processa", L"Monitor on the game's GPU — move it to the GPU that runs the network"), true, &K8_AVISO);
            cy += h;
        }
        // ---- "Como as camadas trocam imagem": FORA DO MENU por decisao do dono (item 6); volta com
        //      [ultima] mostrar_transporte=1. O E_TRANSPORTE e o CMD continuam inteiros.
        if (IniV8().transporte) {
            V8Div(g, L, IW, cy);
            const REAL h = kLinha8;
            const std::wstring esc = TransporteEscolhido();
            std::vector<Opc8> o;
            const wchar_t* nomes[3] = { T(L"Padrão", L"Default"), T(L"Experimental", L"Experimental"), T(L"Windows", L"Windows") };
            for (int i = 0; i < 3; i++)
                o.emplace_back(nomes[i], E_TRANSPORTE, i, esc == kTransporteVals[i], true,
                               TS(L"Como as camadas trocam imagem\nSó vale com o 2x (duas camadas). Padrão: o testado; Experimental: no desenho do OBS; Windows: o caminho antigo, pelo compositor.",
                                  L"Layer transport\nOnly with 2x (two layers). Default: the tested path; Experimental: OBS-style; Windows: the legacy path, through the compositor."));
            const REAL ws = V8SegLargura(o);
            if (g) V8Rotulo(g, L, cy, IW - ws - 12.f, h, TS(L"Camadas trocam imagem", L"Layer transport"), std::wstring());
            V8Seg(g, R - ws, cy + (h - kCtrl8) / 2.f, ws, kCtrl8, o);
            cy += h;
        }
    }

    // ================================================================ GERAL
    if (g_abaAtual == ABA_GERAL) {
        V8Secao(g, L, IW, cy, T(L"EXIBIÇÃO", L"DISPLAY"));
        // ---- Rodada 27 (v9): Tamanho da interface - [-] 100% [+] [Auto] (E_TAMANHOUI: val 0 = Auto,
        //      1 = "-", 2 = "+"). De 70% a 160% de 5 em 5; o "+" para no teto desta tela (a janela
        //      nunca passa da area util do monitor). Vale na hora, sem reabrir.
        {
            const REAL h = kLinhaSub8;
            const int p = TamanhoUi();
            const REAL usado = g_fatorUiUsado > 0.f ? g_fatorUiUsado : (p > 0 ? (REAL)p / 100.f : 1.f);
            const REAL teto  = g_fatorUiMax > 0.f ? g_fatorUiMax : (REAL)kTamUiMax / 100.f;
            const int pUsado = (int)(usado * 100.f + 0.5f), pTeto = (int)(teto * 100.f + 0.5f);
            const bool noTeto = pUsado >= pTeto || pUsado >= kTamUiMax, noPiso = pUsado <= kTamUiMin;
            const std::wstring d = TS(L"Tamanho da interface\nAumenta ou diminui juntos a letra e as janelas do app: este painel, a tela inicial, Filtros e Ajustes da rede neural. "
                                      L"De 70% a 160%; nenhuma janela passa da área útil do monitor em que está. Auto escolhe o maior que cabe (até 100% num 1080p). Vale na hora.",
                                      L"Interface size\nScales the app's text and windows together: this panel, the start screen, Filters and Neural network settings. "
                                      L"From 70% to 160%; no window ever grows beyond the usable area of its monitor. Auto picks the largest size that fits (up to 100% on a 1080p screen). Applies instantly.");
            std::wstring sub;
            if (p == 0) sub = TS(L"Auto: o maior que cabe nesta tela", L"Auto: the largest that fits this screen");
            else if (p > pUsado) { wchar_t b[80]; _snwprintf(b, 80, T(L"Limitado a %d%% nesta tela", L"Limited to %d%% on this screen"), pUsado); b[79] = 0; sub = b; }
            const REAL bq = kCtrl8, wVal = Larg8(L"160%", f8CtrlB) + 14.f, wAuto = V8BotaoW(L"Auto");
            const REAL wTot = bq + 4.f + wVal + 4.f + bq + 8.f + wAuto;
            REAL x = R - wTot;
            const REAL yb = cy + (h - kCtrl8) / 2.f;
            const std::wstring tipMax = TS(L"Máximo para esta tela\nMaior que isto, a janela passaria da área útil do monitor.",
                                           L"Maximum for this screen\nAny larger and the window would extend past the monitor's usable area.");
            const int iRot = (int)g_items.size();
            g_items.push_back(Item{ RectF(L, cy, x - L - 8.f, h), E_NONE, 0, false, false, d });
            (void)iRot;
            if (g) V8Rotulo(g, L, cy, x - L - 12.f, h, TS(L"Tamanho da interface", L"Interface size"), sub, true);
            V8BotaoItem(g, RectF(x, yb, bq, kCtrl8), L"−", B8_CONTORNO, E_TAMANHOUI, 1, !noPiso,
                        TS(L"Diminuir\n5% menor (mínimo 70%).", L"Decrease\n5% smaller (minimum 70%)."));
            x += bq + 4.f;
            if (g) { wchar_t v[16]; _snwprintf(v, 16, L"%d%%", pUsado); v[15] = 0; Txt(g, v, f8CtrlB, K8_TXT, RectF(x, yb, wVal, kCtrl8), 1, true); }
            x += wVal + 4.f;
            V8BotaoItem(g, RectF(x, yb, bq, kCtrl8), L"+", B8_CONTORNO, E_TAMANHOUI, 2, !noTeto,
                        noTeto && pUsado < kTamUiMax ? tipMax : TS(L"Aumentar\n5% maior (até o máximo que cabe nesta tela).", L"Increase\n5% larger (up to the maximum that fits this screen)."));
            x += bq + 8.f;
            V8BotaoItem(g, RectF(x, yb, wAuto, kCtrl8), L"Auto", p == 0 ? B8_CHEIO : B8_CONTORNO, E_TAMANHOUI, 0, true,
                        TS(L"Auto\nO maior tamanho que cabe nesta tela, sem passar de 100% num 1080p. Apertar − ou + sai do Auto.",
                           L"Auto\nThe largest size that fits this screen, capped at 100% on a 1080p display. Pressing − or + turns Auto off."));
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Exibicao: Janela · Sem bordas · Tela cheia (E_EXIB; so no Nativo com a janela do jogo)
        {
            const REAL h = kLinha8;
            const bool vis = g_hud.podeIntegrar && CardExibicaoVisivel();
            const int atual = vis ? ExibicaoPainel() : -1;
            std::wstring d = TS(L"Exibição do jogo\nJanela e Sem bordas mudam o estilo da janela do jogo; Tela cheia é a exclusiva do jogo, pelo mesmo caminho do Alt+Enter.",
                                L"Game display\nWindowed and Borderless change the game window's style; Fullscreen is the game's exclusive mode, the same as Alt+Enter.");
            if (!vis) d += TS(L"\nSó no modo Nativo, com a janela do jogo.", L"\nNative mode only, with the game window.");
            else if (!g_dlssJogo.telaCheiaOk) d += TS(L"\nTela cheia: este jogo não aceita pelo caminho do Alt+Enter.", L"\nFullscreen: this game doesn't support switching via Alt+Enter.");
            std::vector<Opc8> o;
            o.emplace_back(T(L"Janela", L"Windowed"), E_EXIB, 0, atual == 0, vis, d);
            o.emplace_back(T(L"Sem bordas", L"Borderless"), E_EXIB, 1, atual == 1, vis, d);
            o.emplace_back(T(L"Tela cheia", L"Fullscreen"), E_EXIB, 2, atual == 2, vis && g_dlssJogo.telaCheiaOk, d);
            const REAL ws = V8SegLargura(o);
            if (g) V8Rotulo(g, L, cy, IW - ws - 12.f, h, TS(L"Exibição", L"Display"), std::wstring(), vis);
            V8Seg(g, R - ws, cy + (h - kCtrl8) / 2.f, ws, kCtrl8, o);
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Modo comparativo (E_COMPARAR), com a tecla embaixo
        {
            const REAL h = kLinhaSub8;
            const int idx = (int)g_items.size();
            g_items.push_back(Item{ RectF(L, cy, IW, h), E_COMPARAR, 0, true, g_hud.comparar,
                                    TS(L"Modo comparativo\nA tela dividida: à esquerda com DLSS 5, à direita a imagem original — arraste a linha para mudar a divisão. Bom para gravar a comparação.",
                                       L"Comparison mode\nSplits the screen: left with DLSS 5, right the original image — drag the line to move the split. Great for recording comparisons.") });
            if (g) { V8Rotulo(g, L, cy, IW - kChave8 - 12.f, h, TS(L"Modo comparativo", L"Comparison mode"),
                              TemAtalho(AT_COMPARAR) ? TextoAtalho(AT_COMPARAR) : TS(L"metade da tela sem DLSS 5", L"half the screen without DLSS 5"));
                     V8Chave(g, R - kChave8, cy + h / 2.f, g_hud.comparar, true, g_hot == idx); }
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Captura: Auto · WGC · DXGI (E_CAPTURA), com quem serve agora embaixo
        {
            const REAL h = kLinhaSub8;
            const std::wstring d = TS(L"De onde vêm os quadros\nAuto (padrão): WGC, que enxerga janela coberta e tela cheia sem bordas. DXGI: duplicação do monitor, com o carimbo do desenho; "
                                      L"não serve em HDR, girado, em outra placa, nem com a janela coberta — nesses casos quem serve continua sendo o WGC.",
                                      L"Where frames come from\nAuto (default): WGC, which sees occluded windows and borderless fullscreen. DXGI: monitor duplication, timestamped at draw time; "
                                      L"doesn't work with HDR, rotated displays, another GPU or an occluded window — then WGC keeps serving.");
            std::vector<Opc8> o;
            o.emplace_back(T(L"Auto", L"Auto"), E_CAPTURA, 0, g_hud.capturaModo == 0, true, d);
            o.emplace_back(L"WGC", E_CAPTURA, 1, g_hud.capturaModo == 1, true, d);
            o.emplace_back(L"DXGI", E_CAPTURA, 2, g_hud.capturaModo == 2, true, d);
            const REAL ws = V8SegLargura(o);
            if (g) V8Rotulo(g, L, cy, IW - ws - 12.f, h, TS(L"Captura", L"Capture"), TS(L"servindo agora: ", L"Active: ") + (g_hud.capturaAtiva == 1 ? L"DXGI" : L"WGC"));
            V8Seg(g, R - ws, cy + (h - kCtrl8) / 2.f, ws, kCtrl8, o);
            cy += h; V8Div(g, L, IW, cy);
        }
        // ---- Ponteiro: Esconder · Desenhar · Prender (E_PONTEIRO)
        {
            const REAL h = kLinha8;
            const std::wstring d = TS(L"O mouse na película\nEsconder: sem ponteiro (para gravar). Desenhar (padrão): o ponteiro do sistema, desenhado por nós na posição de AGORA. "
                                      L"Prender: o de cima, e o mouse não sai da janela do jogo enquanto a película estiver na frente. Vale na hora.",
                                      L"Cursor on the overlay\nHide: no cursor (for recording). Draw (default): the system cursor, drawn by the app at its CURRENT position. "
                                      L"Lock: same, plus the mouse can't leave the game window while the overlay is in front. Applies instantly.");
            std::vector<Opc8> o;
            o.emplace_back(T(L"Esconder", L"Hide"), E_PONTEIRO, 0, g_hud.ponteiroModo == 0, true, d);
            o.emplace_back(T(L"Desenhar", L"Draw"), E_PONTEIRO, 1, g_hud.ponteiroModo == 1, true, d);
            o.emplace_back(T(L"Prender", L"Lock"), E_PONTEIRO, 2, g_hud.ponteiroModo == 2, true, d);
            const REAL ws = V8SegLargura(o);
            if (g) V8Rotulo(g, L, cy, IW - ws - 12.f, h, TS(L"Ponteiro", L"Cursor"), std::wstring());
            V8Seg(g, R - ws, cy + (h - kCtrl8) / 2.f, ws, kCtrl8, o);
            cy += h;
        }
        cy += 10.f;
        V8Secao(g, L, IW, cy, T(L"CONTA E SISTEMA", L"ACCOUNT & SYSTEM"));
        auto linhaBotao = [&](const std::wstring& rot, const std::wstring& botao, int kind, bool en, const std::wstring& tip, bool div) {
            const REAL h = kLinha8;
            const REAL bw = V8BotaoW(botao);
            if (g) V8Rotulo(g, L, cy, IW - bw - 12.f, h, rot, std::wstring(), en);
            V8BotaoItem(g, RectF(R - bw, cy + (h - kCtrl8) / 2.f, bw, kCtrl8), botao, B8_CONTORNO, kind, 0, en, tip);
            cy += h;
            if (div) V8Div(g, L, IW, cy);
        };
        linhaBotao(TS(L"Atalhos", L"Keyboard shortcuts"), g_hud.atalhosAbertos ? TS(L"Fechar", L"Close") : TS(L"Ver todos", L"View all"), E_ATALHOSABRIR, true,
                   TS(L"Atalhos\nAs teclas do programa, numa janelinha ao lado: clique numa linha para trocar a tecla.", L"Keyboard shortcuts\nAll the app's keys, in a panel alongside: click a row to rebind it."), true);
        if (IniV8().reshade)   // Rodada 23: escondido por padrao ([ultima] mostrar_reshade=1 devolve)
            linhaBotao(TS(L"ReShade", L"ReShade"), g_hud.interativo ? TS(L"Fechar", L"Close") : TS(L"Abrir", L"Open"), E_INTERATIVO, g_hud.overlay,
                       TS(L"ReShade\nO segundo menu: presets, shaders e add-ons. Só no modo sobreposto.", L"ReShade\nThe second menu: presets, shaders and add-ons. Overlay mode only."), true);
        linhaBotao(TS(L"Trocar fonte ou placa", L"Change source or GPU"), TS(L"Trocar", L"Change"), E_SELETOR, true,
                   TS(L"Trocar fonte ou placa\nVolta para a tela de escolha, com a fonte, a placa e o idioma de agora já marcados.",
                      L"Change source or GPU\nGoes back to the selection screen, with the current source, GPU and language already selected."), true);
        linhaBotao(TS(L"Grupo de ofertas", L"Deals group"), TS(L"Abrir", L"Open"), E_OFERTAS, true,
                   TS(L"Grupo de ofertas — PC do Fafa\nO app é grátis; o grupo ajuda a comprar peças e importar mais barato.",
                      L"Deals group — PC do Fafa\nThe app is free; joining the group is how you support it."), !g_urlCanal.empty());
        if (!g_urlCanal.empty()) {
            cy += 8.f;
            V8BotaoItem(g, RectF(L, cy, IW, 36.f), TS(L"Apoie o criador", L"Support the creator"), B8_CHEIO, E_CANAL, 0, true,
                        TS(L"Apoie o criador do programa\nAbre o canal no YouTube — é de graça e ajuda o app a continuar.",
                           L"Support the creator\nOpens the YouTube channel — it's free and helps keep the app going."));
            cy += 36.f;
        }
    }

    // ================================================================ altura natural e rodape fixo
    cy += 14.f;
    g_mainNat8 = (int)((cy - top) + kRod8 + 0.5f);
    const REAL mainH = (REAL)(g_mainH8 > g_mainNat8 ? g_mainH8 : g_mainNat8);
    const REAL yRod = top + mainH - kRod8;
    {
        if (g) { Pen p(K8_BORDA, 1.f); g->DrawLine(&p, xCard, yRod, xCard + (REAL)CW8, yRod); }
        // ---- presets DESTE jogo/programa: nome + 1·2·3 + Salvar + Carregar
        const REAL y = yRod + (kRodPreset8 - 32.f) / 2.f, h = 32.f;
        const int ativo = PresetJogo::Ativo();
        const std::wstring rotS = T(L"Salvar", L"Save"), rotC = T(L"Carregar", L"Load");
        const REAL bwS = V8BotaoW(rotS) - 8.f, bwC = V8BotaoW(rotC) - 8.f;
        const REAL xC = R - bwC, xS = xC - 8.f - bwS;
        std::vector<Opc8> o;
        for (int n = 1; n <= 3; n++) {
            std::wstring tip = TS(L"Preset ", L"Preset ") + std::to_wstring(n) + L" — " + PresetJogo::NomeDoDono() + L"\n";
            tip += PresetJogo::Tem(n) ? TS(L"Guarda a configuração inteira do app (rede, passes, escalas, filtros, fluidez, limite de fps) só para este jogo/programa. ",
                                          L"Stores the app's entire configuration (network, passes, scales, filters, Frame Generation, FPS limit) for this game/app only. ")
                                      : TS(L"Vazio: Salvar guarda aqui a configuração de agora. ", L"Empty — Save stores the current configuration here. ");
            if (n == ativo) tip += TS(L"É o ATIVO: carrega sozinho quando este jogo abre.", L"This is the ACTIVE preset: it loads automatically when this game starts.");
            o.emplace_back(std::to_wstring(n), E_PRESETSLOT, n, g_presetSel == n, true, tip, n == ativo, !PresetJogo::Tem(n));
        }
        const REAL wChips = V8ChipsLargura(o, 34.f);
        const REAL xChips = xS - 10.f - wChips;
        const std::wstring nome = PresetJogo::NomeDoDono();
        g_items.push_back(Item{ RectF(L, y, xChips - L - 8.f, h), E_NONE, 0, false, false,
                                TS(L"Presets deste jogo/programa\nCada jogo (pelo exe) tem os seus 3. Salvar guarda tudo no slot escolhido e o torna o ativo; "
                                   L"Carregar devolve o slot (o que não for filtro vale depois que o app reabre). O ativo carrega sozinho quando o jogo abre.",
                                   L"Presets for this game/app\nEach game (by its exe) gets 3. Save stores everything in the selected slot and makes it active; "
                                   L"Load restores it (anything other than filters applies after the app restarts). The active preset loads automatically when the game starts.") });
        if (g) Txt(g, nome.c_str(), f8Ctrl, K8_SEC, RectF(L, y, xChips - L - 8.f, h), 0, true);
        V8Chips(g, xChips, y, wChips, h, o, 34.f);
        V8BotaoItem(g, RectF(xS, y, bwS, h), rotS, B8_CONTORNO, E_PRESETSALVAR, g_presetSel, true,
                    TS(L"Salvar no preset ", L"Save to preset ") + std::to_wstring(g_presetSel) + TS(L"\nGuarda a configuração de agora neste slot (substitui o que havia) e o torna o ativo deste jogo.",
                                                                                                     L"\nStores the current configuration in this slot (overwriting it) and makes it the active preset for this game."));
        V8BotaoItem(g, RectF(xC, y, bwC, h), rotC, B8_CONTORNO, E_PRESETCARREGAR, g_presetSel, PresetJogo::Tem(g_presetSel),
                    PresetJogo::Tem(g_presetSel) ? TS(L"Carregar o preset ", L"Load preset ") + std::to_wstring(g_presetSel) +
                                                   TS(L"\nDevolve este slot. Filtros valem na hora; o resto, depois que o app se reabre sozinho (~3 s).",
                                                      L"\nRestores this slot. Filters apply instantly; everything else after the app restarts itself (~3 s).")
                                                 : TS(L"Preset vazio\nSalve alguma coisa nele primeiro.", L"Empty preset\nSave to it first."));
        if (g) { Pen p(K8_BORDA, 1.f); g->DrawLine(&p, xCard, yRod + kRodPreset8, xCard + (REAL)CW8, yRod + kRodPreset8); }
        // ---- Voltar a tela anterior (contorno) e Sair (vermelho)
        const REAL yb = yRod + kRodPreset8 + (kRodBotoes8 - 34.f) / 2.f, hb = 34.f;
        const std::wstring rotSair = g_hud.confirmandoSaida ? T(L"Clique de novo para sair", L"Click again to quit") : T(L"Sair", L"Quit");
        const REAL bwSair = V8BotaoW(rotSair) + 16.f;
        V8BotaoItem(g, RectF(R - bwSair, yb, bwSair, hb), rotSair, B8_PERIGO, E_SAIR, 0, true,
                    TS(L"Sair do DLSS5 For All\nDevolve tudo como estava. Pede um segundo clique.", L"Quit DLSS5 For All\nRestores everything as it was. Click twice to confirm.")
                    + (TemAtalho(AT_SAIR) ? L"\n" + TextoAtalho(AT_SAIR) + TS(L" duas vezes também sai.", L" twice also quits.") : std::wstring()));
        V8BotaoItem(g, RectF(L, yb, R - bwSair - 10.f - L, hb),
                    TS(L"Voltar à tela anterior", L"Back to previous screen"),
                    B8_CONTORNO, E_SELETOR, 0, true,   // v2.1: era "Voltar ao padrao" (E_PADRAOAPP); agora o MESMO caminho do "Trocar fonte ou placa" (CMD_SELETOR)
                    TS(L"Voltar à tela anterior\nVolta para a tela de escolha da fonte, com a fonte, o modo e a placa de agora já marcados. "
                       L"Troque o que quiser e clique em INICIAR de novo.",
                       L"Back to previous screen\nGoes back to the source selection screen, with the current source, mode and GPU already selected. "
                       L"Change whatever you want and click START again."));
    }
    // ---- o aviso rapido (toast), por cima do conteudo, logo acima do rodape
    if (g && !g_aviso8.empty() && GetTickCount64() < g_aviso8Ate) V8Toast(g, xCard, (REAL)CW8, yRod - 14.f, g_aviso8);

    // ================================================================ a janelinha ao lado
    g_popH8 = PopoverAberto8() ? V8Popover(g, top) : 0;
    return g_mainNat8 > g_popH8 ? g_mainNat8 : g_popH8;
}

// A altura do cartao = a da aba MAIS CHEIA (igual nas quatro, decisao do dono). Mede as quatro
// sem desenhar e deixa g_mainH8 pronto para o desenho da aba de agora.
static int AlturaV8() {
    const int antes = g_abaAtual;
    int maxMain = 0;
    for (int a = 0; a < ABA_COUNT; a++) {
        g_abaAtual = a;
        RenderV8(nullptr);
        if (g_mainNat8 > maxMain) maxMain = g_mainNat8;
    }
    g_abaAtual = antes;
    g_mainH8 = maxMain;
    RenderV8(nullptr);   // deixa g_items/g_popH8 da aba de agora
    return maxMain > g_popH8 ? maxMain : g_popH8;
}

// A moldura v8: o cartao (altura da aba mais cheia) e, se aberta, a janelinha ao lado.
static void DesenharMolduraV8(Graphics& g) {
    MolduraSimples8(g, (REAL)SH, (REAL)SH, (REAL)CW8, (REAL)g_mainH8);
    if (g_popH8 > 0) MolduraSimples8(g, (REAL)(SH + CW8 + POPGAP8), (REAL)SH, (REAL)POPW8, (REAL)g_popH8);
}
