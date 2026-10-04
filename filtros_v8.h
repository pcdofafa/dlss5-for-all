// =====================================================================================
//  filtros_v8.h - RODADA 26 (v8, 25/09/2026): a janela de Filtros no visual aprovado
// =====================================================================================
//  INCLUIDO DENTRO DO namespace Filtros do main.cpp, logo depois do Render antigo (que continua
//  valendo com [ultima] menu_v7=1). Mesma logica de sempre - os mesmos Kind/It/Clique, a mesma
//  gravacao, o mesmo envio ao ReShade; mudou o desenho (design-gemini/mockup-consistente.html):
//  uma coluna so de 340 no canto superior direito, cada filtro com o interruptor pilula e, quando
//  ligado, as barras com o valor a direita; Exposicao adaptativa ON/OFF; presets 1·2·3 com
//  Salvar/Carregar; Padrao de fabrica; o X no canto; aviso rapido no mesmo estilo do painel.
//  OS TREZE FILTROS CONTINUAM TODOS AQUI (o mockup mostra so os quatro do visual da rede - sao os
//  que nascem ligados; os outros aparecem como linha desligada, prontos para ligar).
// =====================================================================================
static const int CW8F = 340;
static int  g_presetSelF = 1;              // o chip de preset escolhido na janela (1..3)
static std::wstring g_avisoF;              // o aviso rapido desta janela
static ULONGLONG    g_avisoFAte = 0;
enum { kTimerAvisoF = 98 };
static int LarguraJanela() { return Hud::MenuV8() ? CW8F : CW; }
static void Aviso(const std::wstring& t) {
    g_avisoF = t;
    g_avisoFAte = GetTickCount64() + 2600;
    if (g_wnd) SetTimer(g_wnd, kTimerAvisoF, 2700, nullptr);
}

// A barra no desenho v8: trilho de 6, preenchimento ciano (do meio, quando bipolar), punho de 14 e,
// enquanto arrasta, a bolha com o numero.
static void DesenharBarra8(Graphics* g, const RectF& r, const Barra& b, int v, bool arrastando, bool hot) {
    const REAL yc = r.Y + r.Height / 2.f;
    const RectF tr(r.X, yc - 3.f, r.Width, 6.f);
    FillRound(g, tr, 3.f, Hud::K8_BORDA);
    const REAL f = Fracao(b, v);
    if (Bipolar(b)) {
        const REAL f0 = Fracao(b, 0);
        const REAL a = r.X + r.Width * (f < f0 ? f : f0), w = r.Width * (f > f0 ? f - f0 : f0 - f);
        if (w > 0.5f) FillRound(g, RectF(a, tr.Y, w, 6.f), 3.f, Hud::K8_ACC);
        SolidBrush marca(Hud::K8_SEC);
        g->FillRectangle(&marca, r.X + r.Width * f0 - 0.75f, yc - 5.f, 1.5f, 10.f);
    } else if (f > 0.f) {
        FillRound(g, RectF(tr.X, tr.Y, r.Width * f > 6.f ? r.Width * f : 6.f, 6.f), 3.f, Hud::K8_ACC);
    }
    const REAL kx = r.X + r.Width * f;
    SolidBrush kb((hot || arrastando) ? Hud::K8_ACCH : Hud::K8_ACC);
    g->FillEllipse(&kb, kx - 7.f, yc - 7.f, 14.f, 14.f);
    Pen kp(Color(255, 12, 12, 12), 2.f);
    g->DrawEllipse(&kp, kx - 7.f, yc - 7.f, 14.f, 14.f);
    if (arrastando) {
        const std::wstring t = TextoValor(b, v);
        RectF bb(kx - 24.f, r.Y - 26.f, 48.f, 22.f);
        if (bb.X < r.X - 4.f) bb.X = r.X - 4.f;
        if (bb.X + bb.Width > r.X + r.Width + 4.f) bb.X = r.X + r.Width + 4.f - bb.Width;
        FillRound(g, bb, 4.f, Hud::K8_ACC);
        Txt(g, t.c_str(), Hud::f8CtrlB, Hud::K8_PRETO, bb, 1);
    }
}

static int RenderV8(Graphics* g) {
    Hud::EnsureFonts();
    Hud::EnsureFonts8();
    Garantir();
    g_it.clear();
    const REAL x0 = (REAL)SH, w = (REAL)CW8F, top = (REAL)SH;
    const REAL L = x0 + 20.f, IW = w - 40.f, R = L + IW;
    RectF rX;
    const int ix = (int)g_it.size();
    const REAL hc = Hud::V8CabecalhoJanela(g, x0, top, w, T(L"Filtros", L"Filters"), g_hot == ix, &rX);
    g_it.push_back({ rX, K_X, 0, 0, true });
    REAL cy = top + hc + 10.f;

    // ---- os treze filtros, em duas secoes (imagem | estilo), uma coluna so
    for (int col = 0; col < 2; col++) {
        Hud::V8Secao(g, L, IW, cy, col == 0 ? T(L"IMAGEM", L"IMAGE") : T(L"ESTILO", L"STYLE"));
        bool primeiro = true;
        for (int i = 0; i < N; i++) {
            if (g_f[i].coluna != col) continue;
            if (!primeiro) Hud::V8Div(g, L, IW, cy);
            primeiro = false;
            const Filtro& f = g_f[i];
            const REAL h = 32.f;
            const int isw = (int)g_it.size();
            g_it.push_back({ RectF(L, cy, IW, h), K_SWITCH, i, 0, true });
            if (g) {
                Txt(g, T(f.nome, f.nomeEn), Hud::f8Rot, f.on ? Hud::K8_TXT : Hud::K8_SEC, RectF(L, cy, IW - Hud::kChave8 - 12.f, h), 0, true);
                Hud::V8Chave(g, R - Hud::kChave8, cy + h / 2.f, f.on, true, g_hot == isw);
            }
            cy += h;
            if (!f.on) continue;
            for (int b = 0; b < f.nBarras; b++) {
                const REAL hb = 26.f, wr = 96.f, wv = 44.f;
                const RectF br(L + wr, cy + 4.f, IW - wr - wv - 10.f, hb - 8.f);
                const int idx = (int)g_it.size();
                const bool viva = (g_barra == idx);
                const int v = viva ? g_barraValor : f.v[b];
                g_it.push_back({ br, K_BARRA, i, b, true });
                if (g) {
                    Txt(g, T(f.barras[b].rotPt, f.barras[b].rotEn), Hud::f8Sub, Hud::K8_SEC, RectF(L + 12.f, cy, wr - 16.f, hb), 0, true);
                    DesenharBarra8(g, br, f.barras[b], v, viva, g_hot == idx);
                    Txt(g, TextoValor(f.barras[b], v).c_str(), Hud::f8CtrlB, Hud::K8_TXT, RectF(R - wv, cy, wv, hb), 2);
                }
                cy += hb;
            }
            cy += 2.f;
        }
        cy += 8.f;
    }

    // ---- Exposicao adaptativa (dlss5-feed.cfg exposicao_adaptativa): ON/OFF
    {
        if (Hud::g_expoAdapt < 0)
            Hud::g_expoAdapt = LerChaveCfgInt(g_exeDir + L"dlss5-feed.cfg", "exposicao_adaptativa", 0) != 0 ? 1 : 0;
        const bool on = Hud::g_expoAdapt == 1;
        Hud::V8Div(g, L, IW, cy - 6.f);
        const REAL h = 36.f;
        const int idx = (int)g_it.size();
        g_it.push_back({ RectF(L, cy, IW, h), K_EXPOADAPT, 0, 0, true });
        if (g) {
            Txt(g, T(L"Exposição adaptativa", L"Adaptive exposure"), Hud::f8Rot, Hud::K8_TXT, RectF(L, cy, IW - Hud::kChave8 - 12.f, h), 0, true);
            Hud::V8Chave(g, R - Hud::kChave8, cy + h / 2.f, on, true, g_hot == idx);
        }
        cy += h;
    }
    // ---- presets 1·2·3 + Salvar + Carregar (os 13 filtros numa linha so: [ultima] filtro_presetN)
    {
        Hud::V8Div(g, L, IW, cy);
        cy += 8.f;
        const REAL h = 30.f, wChip = 34.f, gap = 6.f;
        const std::wstring rotS = T(L"Salvar", L"Save"), rotC = T(L"Carregar", L"Load");
        const REAL bwS = Hud::V8BotaoW(rotS) - 8.f, bwC = Hud::V8BotaoW(rotC) - 8.f;
        REAL x = L;
        for (int p = 1; p <= 3; p++) {
            const bool tem = !IniGet((L"filtro_preset" + std::to_wstring(p)).c_str()).empty();
            const RectF r(x, cy, wChip, h);
            const int idx = (int)g_it.size();
            g_it.push_back({ r, K_PRESET_SEL, p, 0, true });
            if (g) {
                const bool sel = (g_presetSelF == p), hot = (g_hot == idx);
                const BYTE a = tem ? 255 : 115;
                FillRound(g, r, 4.f, Hud::Alfa8(sel ? Hud::K8_ACC : (hot ? Hud::K8_CHIPH : Hud::K8_CHIP), a));
                Txt(g, std::to_wstring(p).c_str(), sel ? Hud::f8CtrlB : Hud::f8Ctrl, Hud::Alfa8(sel ? Hud::K8_PRETO : (hot ? Hud::K8_TXT : Hud::K8_SEC), a), r, 1);
            }
            x += wChip + gap;
        }
        const bool temSel = !IniGet((L"filtro_preset" + std::to_wstring(g_presetSelF)).c_str()).empty();
        const RectF rS(R - bwC - 8.f - bwS, cy, bwS, h), rC(R - bwC, cy, bwC, h);
        const int iS = (int)g_it.size(); g_it.push_back({ rS, K_PRESET_SALVAR, g_presetSelF, 0, true });
        const int iC = (int)g_it.size(); g_it.push_back({ rC, K_PRESET_CARREGAR, g_presetSelF, 0, temSel });
        Hud::V8Botao(g, rS, rotS, Hud::B8_CONTORNO, true, g_hot == iS);
        Hud::V8Botao(g, rC, rotC, Hud::B8_CONTORNO, temSel, g_hot == iC);
        cy += h + 12.f;
    }
    // ---- Padrao de fabrica (contorno, largura inteira)
    {
        const RectF r(L, cy, IW, 32.f);
        const int idx = (int)g_it.size();
        g_it.push_back({ r, K_PADRAO, 0, 0, true });
        Hud::V8Botao(g, r, T(L"Padrão de fábrica", L"Restore factory defaults"), Hud::B8_CONTORNO, true, g_hot == idx);
        cy += 32.f + 10.f;
    }
    if (g_tudoDesligado) {
        const std::wstring t = TS(L"DLSS 5 desabilitado (F6): os filtros voltam quando você religar", L"DLSS 5 is off (F6): filters come back when you turn it on");
        const REAL th = Hud::AlturaTexto(t.c_str(), Hud::f8Sub, IW);
        if (g) Hud::TxtWrap(g, t.c_str(), Hud::f8Sub, Hud::K8_AVISO, RectF(L, cy, IW, th + 2.f));
        cy += th + 8.f;
    }
    cy += 6.f;
    if (g && !g_avisoF.empty() && GetTickCount64() < g_avisoFAte) Hud::V8Toast(g, x0, w, cy - 6.f, g_avisoF);
    return (int)(cy - top);
}
