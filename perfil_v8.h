// =====================================================================================
//  perfil_v8.h - RODADA 26 (v8, 25/09/2026): "Ajustes da rede neural" no visual aprovado
// =====================================================================================
//  INCLUIDO DENTRO DO namespace Perfil do main.cpp, logo depois do Render antigo (que continua
//  valendo com [ultima] menu_v7=1). A janela se chamava "Perfil da imagem"; o dono pediu o nome
//  "Ajustes da rede neural" e nenhum "Perfil da imagem" na interface (item 9).
//
//  TODA A LOGICA E A DE SEMPRE: a encenacao (nada vale antes de Aplicar), o Aplicar em ordem
//  (parametros primeiro, a resolucao por passe por ultimo), a memoria de resets, o "Herdar",
//  o alvo por passe (X-PERFIL). Mudou o desenho (design-gemini/mockup-consistente.html): o alvo
//  em chips "Todos · Passe 1 · Passe 2…" (pontinho no passe com ajuste proprio), a resolucao por
//  passe em barras nos degraus reais, a barra "Rede neural" ao vivo, os seis parametros - Modelo
//  A/B/C em segmentado, os outros cinco em barra - com a explicacao de uma linha, e o rodape
//  Descartar / Aplicar (aceso so com pendencia) + "Nada vale antes de Aplicar".
// =====================================================================================
static const int CW8P = 360;
static int LarguraJanela() { return Hud::MenuV8() ? CW8P : CW; }
// "PELE E ROSTOS" -> "Pele e rostos": os rotulos de kCtrlNr sao os da janela antiga (caixa alta).
static std::wstring Frase8(const wchar_t* s) {
    std::wstring t = s ? s : L"";
    if (t.empty()) return t;
    CharLowerBuffW(&t[0], (DWORD)t.size());
    CharUpperBuffW(&t[0], 1);
    return t;
}

// A barra v8 com os estados do perfil: ambar = encenado (ou arrastando), apagada = herdada de Todos.
static void DesenharBarra8(Graphics* g, const RectF& r, int a, float v, bool arrastando, bool herdado, bool hot) {
    const REAL yc = r.Y + r.Height / 2.f;
    FillRound(g, RectF(r.X, yc - 3.f, r.Width, 6.f), 3.f, Hud::K8_BORDA);
    const bool temValor = (v >= 0.f);
    const REAL f = temValor ? Fracao(a, v) : 0.f;
    const bool pend = Pendente(a) || arrastando;
    const Color cheio = pend ? C_PEND : (herdado ? Hud::Alfa8(Hud::K8_ACC, 110) : Hud::K8_ACC);
    if (temValor && f > 0.f) FillRound(g, RectF(r.X, yc - 3.f, r.Width * f > 6.f ? r.Width * f : 6.f, 6.f), 3.f, cheio);
    if (Discreto(a)) {
        const int n = Marcas(a);
        SolidBrush marca(Hud::Alfa8(Hud::K8_SEC, 150));
        for (int i = 1; i < n - 1; i++) g->FillRectangle(&marca, r.X + r.Width * FracaoDaMarca(a, i) - 0.5f, yc - 5.f, 1.f, 10.f);
    }
    const REAL kx = r.X + r.Width * f;
    if (temValor) {
        SolidBrush kb(pend ? C_PEND : herdado ? Hud::Alfa8(Hud::K8_ACC, 170) : (hot ? Hud::K8_ACCH : Hud::K8_ACC));
        g->FillEllipse(&kb, kx - 7.f, yc - 7.f, 14.f, 14.f);
        Pen kp(Color(255, 12, 12, 12), 2.f);
        g->DrawEllipse(&kp, kx - 7.f, yc - 7.f, 14.f, 14.f);
    } else {
        Pen p(Hud::K8_SEC, 1.5f);   // nunca lido: o punho vazio diz "sem valor" em vez de fingir um zero
        g->DrawEllipse(&p, kx - 6.f, yc - 6.f, 12.f, 12.f);
    }
    if (arrastando && temValor) {
        const std::wstring t = TextoValor(a, v);
        RectF b(kx - 24.f, r.Y - 26.f, 48.f, 22.f);
        if (b.X < r.X - 4.f) b.X = r.X - 4.f;
        if (b.X + b.Width > r.X + r.Width + 4.f) b.X = r.X + r.Width + 4.f - b.Width;
        FillRound(g, b, 4.f, pend ? C_PEND : Hud::K8_ACC);
        Txt(g, t.c_str(), Hud::f8CtrlB, Hud::K8_PRETO, b, 1);
    }
}

static int RenderV8(Graphics* g) {
    Hud::EnsureFonts();
    Hud::EnsureFonts8();
    g_it.clear();
    if (g_passeSel >= PassesMostrados() || g_moduloAmd || !FeederTemRedeReduzida()) g_passeSel = -1;
    const REAL x0 = (REAL)SH, w = (REAL)CW8P, top = (REAL)SH;
    const REAL L = x0 + 20.f, IW = w - 40.f, R = L + IW;
    RectF rX;
    const int ix = (int)g_it.size();
    const REAL hc = Hud::V8CabecalhoJanela(g, x0, top, w, T(L"Ajustes da rede neural", L"Neural network settings"), g_hot == ix, &rX);
    g_it.push_back({ rX, K_X, 0, 0, true, TS(L"Fechar\nO que não foi aplicado é descartado (só o Aplicar grava).", L"Close\nPending changes are discarded (only Apply saves them).") });
    REAL cy = top + hc + 12.f;
    const bool porPasse = !g_moduloAmd && FeederTemRedeReduzida();
    const int n = PassesMostrados();

    // ---- o ALVO: Todos · Passe 1 · Passe 2 … (o pontinho = o passe tem ajuste proprio; ambar = encenado)
    if (porPasse) {
        const REAL h = 28.f, gap = 6.f;
        REAL x = L;
        for (int i = -1; i < n; i++) {
            wchar_t nt[32];
            if (i < 0) wcscpy(nt, T(L"Todos", L"All")); else _snwprintf(nt, 32, T(L"Passe %d", L"Pass %d"), i + 1);
            nt[31] = 0;
            const REAL wc = Hud::Larg8(nt, Hud::f8CtrlB) + (i >= 0 ? 24.f : 18.f);
            if (x + wc > R + 0.5f && x > L + 0.5f) { x = L; cy += h + gap; }
            const RectF r(x, cy, wc, h);
            const int idx = (int)g_it.size();
            const bool sel = (g_passeSel == i);
            const bool proprio = (i >= 0) && PasseTemProprio(i);
            std::wstring dn = nt; dn += L"\n";
            if (i < 0) dn += TS(L"Os parâmetros valem para todos os passes (o passe sem ajuste próprio herda daqui).",
                                L"Parameters apply to every pass (passes without their own settings inherit from here).");
            else {
                if (proprio) dn += TS(L"Este passe tem ajuste próprio. ", L"This pass has its own settings. ");
                dn += TS(L"Os parâmetros abaixo passam a valer só para ele.", L"The parameters below will apply to this pass only.");
            }
            g_it.push_back({ r, i < 0 ? K_TODOS : K_SELPASSE, i < 0 ? 0 : i, 0, true, dn });
            if (g) {
                const bool hot = (g_hot == idx);
                FillRound(g, r, 4.f, sel ? Hud::K8_ACC : (hot ? Hud::K8_CHIPH : Hud::K8_CHIP));
                Txt(g, nt, sel ? Hud::f8CtrlB : Hud::f8Ctrl, sel ? Hud::K8_PRETO : (hot ? Hud::K8_TXT : Hud::K8_SEC), r, 1);
                if (proprio) {
                    bool pendAqui = false;
                    for (int ci = 0; ci < kNCtrlNr; ci++) if (PendPasse(ci, i) != kSemPend) pendAqui = true;
                    SolidBrush pt(pendAqui ? C_PEND : (sel ? Hud::K8_PRETO : Hud::K8_ACC));
                    g->FillEllipse(&pt, r.X + r.Width - 9.f, r.Y + 4.f, 5.f, 5.f);
                }
            }
            x += wc + gap;
        }
        cy += h + 12.f;
        Hud::V8Div(g, L, IW, cy - 6.f);
    }

    // ---- RESOLUCAO por passe (barras nos degraus reais: 50..200 no passe 1, o minimo medido nos extras)
    if (porPasse) {
        const REAL bh = 22.f;
        const RectF bMais(R - bh, cy - 2.f, bh, bh), bMenos(R - bh - 6.f - bh, cy - 2.f, bh, bh);
        const int iMenos = (int)g_it.size();
        g_it.push_back({ bMenos, K_MENOSPASSE, 0, 0, n > 1, TS(L"Um passe a menos", L"Remove a pass") });
        const int iMais = (int)g_it.size();
        g_it.push_back({ bMais, K_MAISPASSE, 0, 0, n < kMaxPasses,
                         TS(L"Mais um passe\nCada passe custa uma rede inteira na sua resolução.", L"Add a pass\nEach pass costs a full network run at its own resolution.") });
        {
            REAL cyT = cy;
            Hud::V8Secao(g, L, IW, cyT, T(L"RESOLUÇÃO", L"RESOLUTION"));
            Hud::V8Botao(g, bMenos, L"−", Hud::B8_CONTORNO, n > 1, g_hot == iMenos);
            Hud::V8Botao(g, bMais, L"+", Hud::B8_CONTORNO, n < kMaxPasses, g_hot == iMais);
            cy = cyT;
        }
        const std::wstring minTxt = std::to_wstring(g_nrEscalaMin);
        const std::wstring dicaLinha = TS(L"Resolução do passe\nResolução em que este passe roda. O passe 1 é a base (desce até 50%); os seguintes acrescentam cada vez menos "
                                          L"e dá para rodá-los menores sem perder o detalhe. ",
                                          L"Pass resolution\nThe resolution this pass runs at. Pass 1 is the base (down to 50%); later passes add less and less, "
                                          L"so they can run smaller without losing detail. ")
                                     + (g_nrEscalaMin > 0 ? TS(L"Menor tamanho que a rede aceita nesta placa: ", L"Smallest size the network accepts on this GPU: ") + minTxt + L"%."
                                                          : TS(L"Os extras descem até 25% enquanto o feed não mede o mínimo desta placa.", L"Extra passes go down to 25% until the feeder measures this GPU's minimum."));
        for (int i = 0; i < n; i++) {
            if (g_passeSel >= 0 && i != g_passeSel) continue;
            const REAL h = 32.f, wRot = 70.f, wVal = 44.f;
            const int a = kEscalaIdx0 + i;
            const int idx = (int)g_it.size();
            const bool viva = (g_barra == idx);
            const float v = viva ? g_barraValor : ValorDe(a);
            const RectF r(L + wRot, cy + 5.f, IW - wRot - wVal - 10.f, h - 10.f);
            g_it.push_back({ r, K_BARRA, a, 0, true, dicaLinha });
            if (g) {
                wchar_t nt[32]; _snwprintf(nt, 32, T(L"Passe %d", L"Pass %d"), i + 1); nt[31] = 0;
                Txt(g, nt, Hud::f8Ctrl, Pendente(a) ? C_PEND : Hud::K8_TXT, RectF(L, cy, wRot, h), 0, true);
                DesenharBarra8(g, r, a, v, viva, false, g_hot == idx);
                Txt(g, TextoValor(a, v).c_str(), Hud::f8CtrlB, Pendente(a) ? C_PEND : Hud::K8_TXT, RectF(R - wVal, cy, wVal, h), 2);
            }
            cy += h;
        }
        cy += 6.f;
        // ---- a MESMA barra "Rede neural" do painel, com a previa do que esta encenado (antes do Aplicar)
        {
            std::vector<int> ep;
            int mostra[kMaxPasses];
            for (int i = 0; i < n && i < kMaxPasses; i++) { mostra[i] = (int)(ValorDe(kEscalaIdx0 + i) + 0.5f); ep.push_back(mostra[i]); }
            const BarraRede br = CalcularBarraRede(&ep);
            std::wstring dica = Hud::DicaBarraRede(br);
            wchar_t cst[160];
            if (g_hud.nMsPasse > 0) {
                double tot = g_hud.msTotal;
                if (tot <= 0.0) { tot = 0.0; for (int i = 0; i < g_hud.nMsPasse; i++) tot += g_hud.msPasse[i]; }
                _snwprintf(cst, 160, T(L"\nMedido agora: %.1f ms de rede por quadro.", L"\nMeasured: %.1f ms of network time per frame."), tot);
            } else {
                _snwprintf(cst, 160, T(L"\nEstimativa por área: ≈ %.1f× o custo de um passe inteiro.", L"\nArea estimate: ≈ %.1f× the cost of one full pass."), CustoEstimadoPasses(mostra, n));
            }
            cst[159] = 0;
            dica += cst;
            const REAL hb = Hud::DesenharBarraRede(g, L, cy, IW, br, Hud::f8Sub, Hud::K8_SEC, Hud::K8_SEC, Hud::K8_BORDA);
            g_it.push_back({ RectF(L, cy, IW, hb), K_NADA, 0, 0, false, dica });
            cy += hb + 12.f;
        }
    } else if (!g_moduloAmd) {
        // feed antigo (sem a rede reduzida): a barra de passes de antes, 1..10
        Hud::V8Secao(g, L, IW, cy, T(L"PASSES", L"PASSES"));
        const REAL h = 34.f, wVal = 44.f;
        const int idx = (int)g_it.size();
        const bool viva = (g_barra == idx);
        const float v = viva ? g_barraValor : ValorDe(kPassesIdx);
        const RectF r(L + 70.f, cy + 6.f, IW - 70.f - wVal - 10.f, h - 12.f);
        g_it.push_back({ r, K_BARRA, kPassesIdx, 0, true,
                         TS(L"Passes\nQuantas vezes a rede roda sobre o MESMO quadro — mais passes, mais rede e mais custo.",
                            L"Passes\nHow many times the network runs over the SAME frame — more passes, more cost.") });
        if (g) {
            Txt(g, T(L"Passes", L"Passes"), Hud::f8Ctrl, Pendente(kPassesIdx) ? C_PEND : Hud::K8_TXT, RectF(L, cy, 70.f, h), 0);
            DesenharBarra8(g, r, kPassesIdx, v, viva, false, g_hot == idx);
            Txt(g, TextoValor(kPassesIdx, v).c_str(), Hud::f8CtrlB, Hud::K8_TXT, RectF(R - wVal, cy, wVal, h), 2);
        }
        cy += h + 10.f;
    }

    // ---- PARAMETROS (os seis controles reais; "Herdar de Todos" quando o passe escolhido tem ajuste proprio)
    {
        REAL cyT = cy;
        Hud::V8Secao(g, L, IW, cyT, T(L"PARÂMETROS", L"PARAMETERS"));
        if (g_passeSel >= 0 && PasseTemProprio(g_passeSel)) {
            const std::wstring rot = T(L"Herdar de Todos", L"Inherit from All");
            const REAL bw = Hud::V8BotaoW(rot) - 10.f;
            const RectF b(R - bw, cy - 2.f, bw, 22.f);
            const int iH = (int)g_it.size();
            wchar_t d[180];
            _snwprintf(d, 180, T(L"Herdar de Todos\nO passe %d volta a seguir o ajuste de Todos, nos seis controles.",
                                 L"Inherit from All\nPass %d goes back to using the All settings for all six controls."), g_passeSel + 1);
            d[179] = 0;
            g_it.push_back({ b, K_HERDAR, 0, 0, true, d });
            Hud::V8Botao(g, b, rot, Hud::B8_CONTORNO, true, g_hot == iH);
        }
        cy = cyT;
    }
    bool primeiro = true;
    for (int ci = 0; ci < kNCtrlNr; ci++) {
        const CtrlNr& c = kCtrlNr[ci];
        if (!ControleValeAqui(c.chaveNossa)) continue;   // modulo AMD: controle sem chave no mod nao aparece
        if (!primeiro) Hud::V8Div(g, L, IW, cy);
        primeiro = false;
        const bool seg = c.inteiro;   // o Modelo (A/B/C): segmentado; os outros cinco: barra
        const REAL wCtrl = seg ? 3.f * 38.f : 120.f + 8.f + 40.f;
        const REAL wLab = IW - wCtrl - 12.f;
        REAL hs = Hud::AlturaTexto(T(c.dicaPt, c.dicaEn), Hud::f8Sub, wLab);
        if (hs < 15.f) hs = 15.f;
        const REAL h = (8.f + 19.f + hs + 8.f) > 40.f ? (8.f + 19.f + hs + 8.f) : 40.f;
        const int idx0 = (int)g_it.size();
        const bool viva = (g_barra == idx0) && !seg;
        const float v = viva ? g_barraValor : ValorDe(ci);
        const bool herd = Herdando(ci) && !viva;
        const bool pend = Pendente(ci);
        const Color corT = pend ? C_PEND : (herd ? Hud::K8_SEC : Hud::K8_TXT);
        const std::wstring rot = Frase8(T(c.rotPt, c.rotEn));
        std::wstring tip = rot + L"  —  " + ((v >= 0.f) ? TextoValor(ci, v) : std::wstring(L"—")) + L"\n" + T(c.dicaPt, c.dicaEn);
        if (herd) tip += TS(L"\nHerdado de Todos: mexer aqui dá a este passe um ajuste próprio.", L"\nInherited from All: changing it here gives this pass its own setting.");
        if (seg) {
            const REAL hh = 28.f, x0s = R - wCtrl, ws = wCtrl / (REAL)c.n;
            const int sel = (v >= 0.f) ? NrDegrauMaisPerto(c, v) : -1;
            const RectF caixa(x0s, cy + (h - hh) / 2.f, wCtrl, hh);
            if (g) FillRound(g, caixa, 4.f, Hud::K8_INPUT);
            for (int k = 0; k < c.n; k++) {
                const RectF r(x0s + k * ws, caixa.Y, ws, hh);
                const int idx = (int)g_it.size();
                g_it.push_back({ r, K_SEG, ci, k, true, tip });
                if (g) {
                    const bool s = (k == sel), hot = (g_hot == idx);
                    if (s) FillRound(g, RectF(r.X + 1.f, r.Y + 1.f, r.Width - 2.f, r.Height - 2.f), 3.f, pend ? C_PEND : (herd ? Hud::Alfa8(Hud::K8_ACC, 130) : Hud::K8_ACC));
                    else if (hot) FillRound(g, RectF(r.X + 1.f, r.Y + 1.f, r.Width - 2.f, r.Height - 2.f), 3.f, Hud::K8_CHIPH);
                    Txt(g, c.nomes[k], s ? Hud::f8CtrlB : Hud::f8Ctrl, s ? Hud::K8_PRETO : (hot ? Hud::K8_TXT : Hud::K8_SEC), r, 1);
                }
            }
            if (g) StrokeRound(g, caixa, 4.f, Hud::K8_BORDA, 1.f);
        } else {
            const RectF r(R - wCtrl, cy + (h - 20.f) / 2.f, 120.f, 20.f);
            g_it.push_back({ r, K_BARRA, ci, 0, true, tip });
            if (g) {
                DesenharBarra8(g, r, ci, v, viva, herd, g_hot == idx0);
                Txt(g, (v >= 0.f) ? TextoValor(ci, v).c_str() : L"—", Hud::f8CtrlB, pend ? C_PEND : (herd ? Hud::K8_SEC : Hud::K8_TXT),
                    RectF(R - 40.f, cy, 40.f, h), 2);
            }
        }
        if (g) {
            Txt(g, rot.c_str(), Hud::f8Rot, corT, RectF(L, cy + 8.f, wLab, 19.f), 0, true);
            Hud::TxtWrap(g, T(c.dicaPt, c.dicaEn), Hud::f8Sub, Hud::K8_SEC, RectF(L, cy + 27.f, wLab, hs + 2.f));
        }
        cy += h;
    }
    // a faixa na esquerda diz de quem os controles sao agora (o passe escolhido)
    if (g && g_passeSel >= 0) { SolidBrush faixa(Hud::K8_ACC); g->FillRectangle(&faixa, x0 + 6.f, top + hc + 8.f, 3.f, cy - (top + hc + 8.f)); }

    // ---- rodape: Descartar (contorno) / Aplicar (ciano cheio, aceso so com pendencia) + a regra
    cy += 14.f;
    {
        const bool pend = HaPendencia();
        const REAL bh = 34.f, gap = 10.f, bw = (IW - gap) / 2.f;
        const RectF bD(L, cy, bw, bh), bA(L + bw + gap, cy, bw, bh);
        const int iD = (int)g_it.size();
        g_it.push_back({ bD, K_DESFAZER, 0, 0, pend, TS(L"Descartar\nVolta aos valores em uso (Esc faz o mesmo).", L"Discard\nReverts to the current values (Esc does the same).") });
        const int iA = (int)g_it.size();
        g_it.push_back({ bA, K_APLICAR, 0, 0, pend, TS(L"Aplicar\nAplica todas as mudanças de uma vez (a rede reinicia uma só vez). Enter faz o mesmo.",
                                                      L"Apply\nApplies all changes at once (the network restarts only once). Enter does the same.") });
        Hud::V8Botao(g, bD, T(L"Descartar", L"Discard"), Hud::B8_CONTORNO, pend, g_hot == iD);
        Hud::V8Botao(g, bA, T(L"Aplicar", L"Apply"), Hud::B8_CHEIO, pend, g_hot == iA);
        cy += bh + 8.f;
        std::wstring dica = TS(L"Nada vale antes de Aplicar", L"Nothing applies until you press Apply");
        if (!MotorProprio() && IniGet(L"perfil_reabre") != L"0") dica += TS(L"\nO app reabre em ~3 s", L"\nThe app restarts in ~3 s");
        // quebra em duas linhas quando o texto (o inglês é mais longo) não cabe na largura
        const REAL hd = Hud::AlturaTexto(dica.c_str(), Hud::f8Sub, IW);
        if (g) Hud::TxtWrap(g, dica.c_str(), Hud::f8Sub, pend ? C_PEND : Hud::K8_SEC, RectF(L, cy, IW, hd + 2.f));
        cy += hd + 14.f;
    }
    return (int)(cy - top);
}
