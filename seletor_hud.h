// =====================================================================================
//  seletor_hud.h - A PRIMEIRA TELA DO APP (escolha da fonte), no estilo do painel
//
//  Incluido pelo main.cpp DEPOIS do namespace Hud, de proposito: a tela e desenhada com
//  os mesmos helpers (FillRound, Txt, Switch, cores C_*) e as mesmas fontes do painel do
//  End. Antes era um dialogo Win32 classico - cinza, com combos e caixas de marcar - e
//  parecia de outro programa ao lado do painel. Agora e uma janela layered GDI+ igual a
//  dele: cartoes escuros, acento ciano, chips e chavinhas.
//
//  REGRAS DA INTERFACE (memoria do projeto): poucas decisoes por tela, nenhuma palavra
//  tecnica onde a pessoa decide, nada que polua. Por isso a tela mostra so NOMES CURTOS;
//  toda explicacao longa (os textos que antes ficavam nos rotulos e na dica de cinco
//  linhas) virou TOOLTIP, que aparece ao parar o mouse em cima do item.
//
//  O CONTRATO COM O RESTO DO APP NAO MUDOU: mesma assinatura de ShowSelector, mesmos
//  campos de Choice, mesmo retorno (true = INICIAR, false = fechou), a troca de idioma
//  continua fechando e reabrindo a tela (g_selTrocouIdioma), a faixa de arquivos que
//  faltam continua existindo (agora um cartao ambar no mesmo estilo) e o "Atalhos..."
//  abre o mesmo dialogo de sempre.
//
//  Uma funcao so (Render) faz layout E desenho: com g == nullptr ela apenas mede e
//  preenche a lista de areas clicaveis; com g != nullptr ela desenha. E o mesmo padrao do
//  painel, e pela mesma razao: o retangulo do clique e o do desenho nunca se desencontram.
//  O --seletor-shot passa por este mesmo Render, entao o PNG e a tela sao a mesma coisa.
// =====================================================================================
#pragma once

namespace Sel {
using namespace Gdiplus;
using Hud::FillRound; using Hud::StrokeRound; using Hud::Txt; using Hud::Switch; using Hud::TxtSpaced;
using Hud::C_CARD; using Hud::C_EDGE; using Hud::C_TXT; using Hud::C_BODY; using Hud::C_DIM;
using Hud::C_FAINT; using Hud::C_ON; using Hud::C_ACC; using Hud::C_ACC2; using Hud::C_ROW;
using Hud::C_ROWH; using Hud::C_OFF; using Hud::C_DARK; using Hud::C_WARN; using Hud::C_SEP;
using Hud::fTitle; using Hud::fVer; using Hud::fSub; using Hud::fLbl; using Hud::fItem;
using Hud::fItemSub; using Hud::fChip; using Hud::fBadge;

static const int  SH  = 20;     // margem da sombra (igual ao painel)
static const int  CW  = 720;    // largura do cartao: as listas de janela precisam de espaco
static const int  PAD = 24;     // respiro interno
static const REAL TOPO_H = 52.f;   // a barra de titulo desenhada: e por ela que se arrasta
static const int  kLinhasAlvo = 6; // linhas visiveis da lista do alvo
static const int  kLinhasModo = 5; // linhas visiveis da lista de modos (camera)
static const REAL kLinhaH = 32.f;  // altura de cada linha das listas
static const REAL kTipMaxW = 440.f;
static const UINT kTimerTip = 1;
static const UINT kTipMs = 250;    // o mouse para em cima e a dica aparece depois disto

// cores so desta tela: o fundo das listas (mais escuro que o cartao, para "afundar") e
// o ambar do aviso de arquivos, no mesmo espirito da barra amarela do Windows mas em
// versao escura - chama atencao sem gritar "erro": nada esta quebrado, esta faltando.
static const Color C_LISTA (255,  11,  17,  30);
static const Color C_AMB_BG(255,  64,  44,  16);
static const Color C_AMB_TX(255, 254, 215, 170);
static const Color C_TIP_BG(252,  17,  24,  39);

static REAL g_scale = 1.f;

// os tipos de area clicavel
enum Kind { K_NADA = 0, K_X, K_LANG, K_FONTE, K_ALVO, K_MODO, K_RECORTE, K_SW, K_GPU,
            K_BAIXAR, K_PASTA, K_ATUALIZAR, K_ATALHOS, K_INICIAR, K_INFO,
            // o que se monta ANTES de comecar (ver o bloco no Estado)
            K_JOGORES, K_RECONST, K_RITMO, K_CURTO, K_IAORDEM, K_NITIDEZ };
// as chavinhas, na ordem em que aparecem
enum Sw { SW_SOBREPOR = 0, SW_TELACHEIA, SW_CURSOR, SW_ESCONDER, SW_VSYNC, SW_HDR, SW_FPS, SW_N };

struct It { RectF r; int kind; int val; bool enabled; std::wstring tip; };
static std::vector<It> g_it;
static int  g_hot = -1;
static int  g_winW = 0, g_winH = 0;
static RectF g_rcListaAlvo, g_rcListaModo;   // para a roda do mouse achar a lista certa

// a janela pode ser arrastada pelo topo (ela nao tem barra de titulo do Windows)
static POINT g_pos{ 0, 0 };
static bool  g_posValida = false;
static bool  g_arrastando = false;
static POINT g_arrastoOff{ 0, 0 };

// o tooltip: uma janelinha layered a parte, para poder sair da area do cartao
static HWND g_tip = nullptr;
static int  g_tipItem = -1;

// fontes so desta tela (as outras vem do painel)
static Font *fLinha = nullptr, *fTip = nullptr, *fTipT = nullptr, *fBotao = nullptr;
static void EnsureFonts() {
    Hud::EnsureFonts();
    if (fLinha) return;
    fLinha = Hud::MkFont(L"Segoe UI",          14.f, FontStyleRegular);
    fTip   = Hud::MkFont(L"Segoe UI",          13.f, FontStyleRegular);
    fTipT  = Hud::MkFont(L"Segoe UI Semibold", 13.f, FontStyleRegular);
    fBotao = Hud::MkFont(L"Segoe UI Semibold", 15.f, FontStyleRegular);
}
static void SoltarFontes() {
    delete fLinha; delete fTip; delete fTipT; delete fBotao;
    fLinha = fTip = fTipT = fBotao = nullptr;
}

// Um Graphics so para MEDIR texto (o Render(nullptr) nao tem superficie). As medidas
// saem em unidades logicas, que e o que o layout usa.
static Graphics* Medidor() {
    static Bitmap* b = nullptr; static Graphics* g = nullptr;
    if (!g) { b = new Bitmap(4, 4, PixelFormat32bppARGB); g = new Graphics(b); g->SetTextRenderingHint(TextRenderingHintAntiAlias); }
    return g;
}
static REAL AlturaTexto(const wchar_t* s, Font* f, REAL w) {
    StringFormat sf; RectF lay(0, 0, w, 4000.f), out;
    Medidor()->MeasureString(s, -1, f, lay, &sf, &out);
    return out.Height + 2.f;
}
static REAL LarguraTexto(const wchar_t* s, Font* f) {
    StringFormat sf; sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    RectF lay(0, 0, 4000.f, 200.f), out;
    Medidor()->MeasureString(s, -1, f, lay, &sf, &out);
    return out.Width;
}
// texto que QUEBRA linha dentro da caixa (o Txt do painel nao quebra)
static void TxtWrap(Graphics* g, const wchar_t* s, Font* f, const Color& c, const RectF& box) {
    StringFormat sf; SolidBrush b(c);
    g->DrawString(s, -1, f, box, &sf, &b);
}
// FILEIRA DE CHIPS QUE QUEBRA SOZINHA (14/09/2026). Antes disto cada fileira desta tela
// media a largura de cada chip com LarguraTexto (certo) mas nunca verificava se a SOMA
// cabia na largura do painel (errado) - so ia empilhando "x += w + 8" para a direita. Com
// 7 chips de reconstrucao (3240/6K e 4320/8K novos) e "Resolução do Windows"/"Windows
// resolution" no meio, isso estourou a tela pra fora a 300% de escala. Chamar ANTES de
// montar o RectF de cada chip: quebra para a proxima linha se o chip nao couber na atual,
// nunca espreme. `x` e `cy` sao passados por referencia e ficam prontos para o RectF do
// chip logo em seguida; quem chama soma `cy += 30+<espaco depois da fileira>` so DEPOIS
// do laco, porque `cy` aqui ja avança conforme o numero de linhas realmente usadas.
static void ProxLinhaDeChip(REAL& x, REAL& cy, REAL w, REAL Lborda, REAL IWdisp) {
    if (x > Lborda && x + w > Lborda + IWdisp) { x = Lborda; cy += 30.f + 8.f; }
}

// ------------------------------------------------------------------ o estado da tela
struct Estado {
    std::vector<Device>*   devs = nullptr;
    std::vector<WinTarget> wins;
    std::vector<MonTarget> mons;
    SourceKind fonte = SRC_WINDOW;
    int selWin = -1, selMon = -1, selDev = -1, selModo = -1;
    int rolAlvo = 0, rolModo = 0;      // primeira linha visivel de cada lista
    bool clientOnly = true;            // janela: so a area util
    bool sw[SW_N]{};
    std::vector<int> placasIdx;        // chip -> indice em g_placas
    int  selPlaca = 0;
    FaltaDlss falta; std::wstring faltaTxt;
    bool ignorarFalta = false;         // o --seletor-shot nao mostra a faixa
    std::wstring wantDev, wantMode, wantWin, wantMon;   // ultimas escolhas (ini)
    bool primeiraVez = false;
    // ============ O QUE SE ESCOLHE ANTES DE COMECAR (14/09/2026) ============
    // Pedido do usuario, e a razao e boa: estes tres SO VALEM AO REABRIR, e um controle que
    // so vale ao reabrir nao pode morar no painel de jogo - clicar nele durante a partida e
    // nao ver efeito foi a queixa que ele repetiu por dois dias. Aqui ele esta parado, com
    // tempo de ler, antes de a sessao comecar. (A resolucao do jogo tambem fica no painel,
    // porque aquela vale NA HORA - e as duas telas leem a mesma chave do ini.)
    int  jogoRes = 0;        // altura da janela do jogo: 0 = nativo, senao 720/900/1080/1440
    int  reconstrucao = -1;  // 0 = captura, -1 = monitor, senao a altura (1080/1440/2160)
    int  ritmo = 0;          // 0 = taxa do monitor, 1 = livre, 2 = limite escolhido
    bool curto = false;      // caminho curto do SR (captura 1:1 no canto do backbuffer)
    int  iaOrdem = 0;        // 0 = a IA trabalha ANTES de ampliar (rapida), 1 = DEPOIS (pesada)
    int  nitidez = 80;       // work_sharpness x100: o realce aplicado DEPOIS de a IA ampliar
    Choice ch; bool ok = false;
    HWND wnd = nullptr; HINSTANCE inst = nullptr;
};
static Estado* g_s = nullptr;   // a tela aberta (uma so por vez)

static bool Tela(const Estado& s) { return s.fonte != SRC_CAMERA; }
static int  NumAlvos(const Estado& s) {
    return s.fonte == SRC_WINDOW ? (int)s.wins.size() : s.fonte == SRC_MONITOR ? (int)s.mons.size()
                                 : (s.devs ? (int)s.devs->size() : 0);
}
static int& SelAlvo(Estado& s) { return s.fonte == SRC_WINDOW ? s.selWin : s.fonte == SRC_MONITOR ? s.selMon : s.selDev; }
static const std::vector<Mode>* Modos(const Estado& s) {
    if (s.fonte != SRC_CAMERA || !s.devs || s.selDev < 0 || s.selDev >= (int)s.devs->size()) return nullptr;
    return &(*s.devs)[s.selDev].modes;
}
// INICIAR so acende com um alvo valido (e, na camera, um modo)
static bool AlvoValido(const Estado& s) {
    const int n = NumAlvos(s), a = s.fonte == SRC_WINDOW ? s.selWin : s.fonte == SRC_MONITOR ? s.selMon : s.selDev;
    if (a < 0 || a >= n) return false;
    if (s.fonte == SRC_CAMERA) { const auto* m = Modos(s); return m && s.selModo >= 0 && s.selModo < (int)m->size(); }
    return true;
}

// ------------------------------------------------------------------ textos das dicas
// Tudo que antes era rotulo comprido ou a dica de cinco linhas mora aqui. Primeira linha
// = titulo (sai em negrito), o resto = corpo. Em pt/en pelo T() de sempre.
static std::wstring TipFonte(SourceKind k) {
    if (k == SRC_WINDOW) return TS(
        L"JANELA — recomendado\n"
        L"Mais leve que o monitor: a rede processa só a área da janela. Um jogo em 2560x1080 são 2,8 milhões "
        L"de pixels por quadro contra 5,0 de uma tela 3440x1440 — metade da conta.\n"
        L"Com SOBREPOR ligado, a nossa janela fica colada em cima do jogo, sem borda; mouse e teclado passam "
        L"direto para ele — parece um filtro dentro do jogo. Desligado, abre uma janela separada.\n"
        L"Nos dois casos o OBS grava a nossa janela (Captura de Janela). Só para jogos em janela / borderless.",
        L"WINDOW — recommended\n"
        L"Lighter than the monitor: the network only processes the window's area. A game at 2560x1080 is 2.8 "
        L"million pixels per frame against 5.0 on a 3440x1440 screen — about half the work.\n"
        L"With OVERLAY on, our window sits glued on top of the game, borderless; mouse and keyboard go straight "
        L"through to it — it looks like a filter inside the game. Off, it opens as a separate window.\n"
        L"Either way OBS records our window (Window Capture). Windowed / borderless games only.");
    if (k == SRC_MONITOR) return TS(
        L"MONITOR\n"
        L"A tela inteira passa pelo DLSS 5.\n"
        L"SOBREPOR ligado: a camada cobre o monitor e TUDO nele é filtrado; mouse e teclado continuam indo para o "
        L"que está embaixo. Se nada mudar na tela por muito tempo, a imagem pode parar (a camada tapa a própria "
        L"fonte) — o app percebe e devolve o original.\n"
        L"SOBREPOR desligado: uma janela separada com a tela inteira dentro, filtrada. Para gravar no OBS, "
        L"desligue \"Esconder\" e use Captura de Janela (Windows 10 1903+).",
        L"MONITOR\n"
        L"The whole screen goes through DLSS 5.\n"
        L"OVERLAY on: the layer covers the monitor and EVERYTHING on it is filtered; mouse and keyboard still reach "
        L"whatever is underneath. If nothing changes on screen for a while the image can freeze (the layer covers "
        L"its own source) — the app notices and backs off.\n"
        L"OVERLAY off: a separate window with the whole screen inside it, filtered. To record in OBS, turn "
        L"\"Hide\" off and use Window Capture (Windows 10 1903+).");
    return TS(
        L"CÂMERA / PLACA DE CAPTURA\n"
        L"A imagem que chega pelo cabo: console, outro PC, câmera. Escolha o aparelho e depois o modo "
        L"(resolução, fps e formato).\n"
        L"Dica: 1920x1080 é o ponto ideal — o Feeder roda em DLAA na resolução da captura, então 4K = 4x a carga. "
        L"Prefira formato não comprimido (YUY2/NV12) quando houver.\n"
        L"Depois de abrir: ",
        L"CAMERA / CAPTURE CARD\n"
        L"The image coming through the cable: console, another PC, a camera. Pick the device, then the mode "
        L"(resolution, fps and format).\n"
        L"Tip: 1920x1080 is the sweet spot — the Feeder runs DLAA at the capture resolution, so 4K = 4x the load. "
        L"Prefer an uncompressed format (YUY2/NV12) when there is one.\n"
        L"Once it opens: ") + RodapeAtalhos() + T(L", F11 tela cheia.", L", F11 for fullscreen.");
}
static const wchar_t* NomeSw(int i) {
    switch (i) {
    case SW_SOBREPOR:  return T(L"SOBREPOR",   L"OVERLAY");
    case SW_TELACHEIA: return T(L"Tela cheia", L"Fullscreen");
    case SW_CURSOR:    return T(L"Cursor",     L"Cursor");
    case SW_ESCONDER:  return T(L"Esconder",   L"Hide");
    case SW_VSYNC:     return L"V-Sync";
    case SW_HDR:       return L"HDR";
    default:           return T(L"FPS na tela", L"FPS on screen");
    }
}
static std::wstring TipSw(int i, const Estado& s) {
    switch (i) {
    case SW_SOBREPOR: return TS(
        L"SOBREPOR\n"
        L"Ligado: o filtro fica COLADO POR CIMA do alvo, sem borda, como se fosse parte do jogo. Mouse e teclado "
        L"continuam indo para o jogo; some da barra de tarefas e do Alt+Tab. Controle só por atalho (o app mostra "
        L"quais ao abrir). OBS: Captura de Janela apontando para o DLSS5 For All.\n"
        L"Desligado: o alvo aparece numa JANELA SEPARADA, como a imagem de uma placa de captura. Move, "
        L"redimensiona, F11 tela cheia. É o modo certo para o Lossless Scaling por cima e para gravar no OBS.",
        L"OVERLAY\n"
        L"On: the filter sits GLUED ON TOP of the target, borderless, as if it were part of the game. Mouse and "
        L"keyboard still go to the game; it leaves the taskbar and Alt+Tab. Control is by shortcut only (the app "
        L"shows which ones when it opens). In OBS: Window Capture pointed at DLSS5 For All.\n"
        L"Off: the target shows up in a SEPARATE WINDOW, just like a capture card feed. Move it, resize it, F11 "
        L"for fullscreen. This is the right mode for Lossless Scaling on top and for recording in OBS.");
    case SW_TELACHEIA: return TS(
        L"TELA CHEIA\n"
        L"Abre a janela do filtro já em tela cheia. F11 alterna depois, a qualquer momento.\n"
        L"Com SOBREPOR ligado não faz diferença: a camada já cobre o alvo.",
        L"FULLSCREEN\n"
        L"Opens the filter window already in fullscreen. F11 toggles it later, at any time.\n"
        L"With OVERLAY on it makes no difference: the layer already covers the target.");
    case SW_CURSOR: return TS(
        L"CURSOR\n"
        L"Desenha o cursor do mouse dentro da imagem capturada. Útil para gravar tutoriais; num jogo, em geral, "
        L"fica melhor desligado.\nSó vale para Janela e Monitor.",
        L"CURSOR\n"
        L"Draws the mouse cursor inside the captured image. Handy for recording tutorials; in a game it is usually "
        L"better off.\nWindow and Monitor only.");
    case SW_ESCONDER: return TS(
        L"ESCONDER DA CAPTURA\n"
        L"Tira a nossa janela de gravadores e capturas de tela. No monitor sobreposto é obrigatório — sem isso a "
        L"camada capturaria a si mesma e viraria um espelho infinito.\n"
        L"Desligue para gravar a nossa janela no OBS (Captura de Janela). Só vale para Janela e Monitor.",
        L"HIDE FROM CAPTURE\n"
        L"Keeps our window out of recorders and screenshots. When overlaying a monitor it is required — without it "
        L"the layer would capture itself and turn into an infinite mirror.\n"
        L"Turn it off to record our window in OBS (Window Capture). Window and Monitor only.");
    case SW_VSYNC: return TS(
        L"V-SYNC\n"
        L"Ligado, cada quadro é mostrado exatamente num vblank do monitor: cadência estável, e o Lossless Scaling "
        L"(frame generation) gosta dessa regularidade.\n"
        L"Em troca, mais latência (o app espera o monitor) e a taxa fica presa ao refresh do monitor onde a janela "
        L"está (60 Hz = 60 fps, mesmo capturando 100).\n"
        L"Desligado (padrão) NÃO rasga a imagem: em flip model ele só não espera o vblank. Dá para mudar depois no "
        L"painel, sem fechar o app.",
        L"V-SYNC\n"
        L"On, every frame is presented exactly on a monitor vblank: steady pacing, and Lossless Scaling (frame "
        L"generation) likes that regularity.\n"
        L"In exchange, more latency (the app waits for the monitor) and the rate is capped by the refresh of the "
        L"monitor the window is on (60 Hz = 60 fps, even capturing 100).\n"
        L"Off (the default) does NOT tear: on the flip model it simply does not wait for the vblank. You can change "
        L"it later from the panel, without closing the app.");
    case SW_HDR:
#ifdef TEM_HDR_CONTROL
        return TS(L"HDR — plano B\n", L"HDR — plan B\n") + Hdr::Txt::Tooltip();
#else
        return TS(L"HDR — plano B\nTratar o HDR automaticamente (desligar no monitor / converter PQ).",
                  L"HDR — plan B\nHandle HDR automatically (turn it off on the monitor / convert PQ).");
#endif
    default: return TS(
        L"FPS NA TELA\n"
        L"Mostra o contador de FPS no canto da imagem. É o contador do ReShade, não nosso: a escolha vai para o "
        L"ReShade.ini e vale já nesta abertura.",
        L"FPS ON SCREEN\n"
        L"Shows the FPS counter in the corner of the image. It is ReShade's counter, not ours: the choice goes into "
        L"ReShade.ini and is in effect this session already.");
    }
    (void)s;
}

// ------------------------------------------------------------------ dados
// Recarrega a lista do alvo para a fonte atual e aplica os padroes que dependem da
// fonte. E o antigo FillTargets: chamado ao abrir, ao trocar de fonte e no "Atualizar
// lista". SOBREPOR volta LIGADO em toda passagem por aqui, para janela e monitor -
// decisao do usuario ("ja e a segunda vez que abro e tenho que marcar"); desligar e uma
// escolha da sessao, com aviso, e nao fica gravada.
static void CarregarModos(Estado& s) {
    s.selModo = -1; s.rolModo = 0;
    if (s.fonte != SRC_CAMERA || !s.devs || s.selDev < 0 || s.selDev >= (int)s.devs->size()) return;
    Device& d = (*s.devs)[s.selDev];
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    LoadModes(d);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    // Sem o sufixo "(comprimido)" / "(compressed)": e a unica parte do rotulo que muda
    // com o idioma, e a escolha salva no ini nao pode se perder por causa disso.
    auto semSufixo = [](const std::wstring& t) { const size_t k = t.find(L"  ("); return (k == std::wstring::npos) ? t : t.substr(0, k); };
    int sel = -1;
    for (size_t i = 0; i < d.modes.size(); i++)
        if (!s.wantMode.empty() && semSufixo(d.modes[i].label) == semSufixo(s.wantMode)) sel = (int)i;
    if (sel < 0) sel = BestMode(d.modes);
    s.selModo = sel;
    if (s.selModo >= kLinhasModo) s.rolModo = s.selModo - kLinhasModo + 1;
}
static void Recarregar(Estado& s) {
    s.rolAlvo = 0;
    if (s.fonte == SRC_WINDOW) {
        s.wins = EnumCaptureWindows(s.wnd);
        s.selWin = s.wins.empty() ? -1 : 0;
        for (size_t i = 0; i < s.wins.size(); i++) if (!s.wantWin.empty() && s.wins[i].title == s.wantWin) s.selWin = (int)i;
    } else if (s.fonte == SRC_MONITOR) {
        s.mons = EnumCaptureMonitors();
        s.selMon = s.mons.empty() ? -1 : 0;
        for (size_t i = 0; i < s.mons.size(); i++) if (!s.wantMon.empty() && MesmoMonitor(s.mons[i].label, s.wantMon)) s.selMon = (int)i;
    } else {
        const int n = s.devs ? (int)s.devs->size() : 0;
        s.selDev = n ? 0 : -1;
        for (int i = 0; i < n; i++) if (!s.wantDev.empty() && (*s.devs)[i].name == s.wantDev) s.selDev = i;
        CarregarModos(s);
    }
    const int a = SelAlvo(s);
    if (a >= kLinhasAlvo) s.rolAlvo = a - kLinhasAlvo + 1;
    const bool tela = Tela(s);
    s.sw[SW_SOBREPOR] = tela;                       // camera nao tem o que sobrepor
    if (s.fonte == SRC_MONITOR) s.sw[SW_ESCONDER] = true;
    if (s.fonte == SRC_WINDOW)  s.sw[SW_ESCONDER] = false;
}
// A faixa de arquivos: reavaliada ao abrir e toda vez que a tela recupera o foco (a
// pessoa pode ter acabado de instalar o RHI em outra janela). Devolve true se mudou.
static bool AtualizarFalta(Estado& s) {
    if (s.ignorarFalta) return false;
    const FaltaDlss f = VerFaltaDlss();
    const std::wstring t = f.AlgumaCoisa() ? TextoFaltaDlss(f) : L"";
    const bool mudou = (t != s.faltaTxt) || (f.AlgumaCoisa() != s.falta.AlgumaCoisa());
    if (mudou) {
        Log("seletor: aviso do DLSS 5 %s (addon=%d dlssnr=%d dlss=%d reshade=%d mod-amd faltando=%u)",
            f.AlgumaCoisa() ? "mostrado" : "escondido", (int)f.addon, (int)f.dll, (int)f.dll2,
            (int)f.reshade, (unsigned)f.mod.size());
    }
    s.falta = f; s.faltaTxt = t;
    return mudou;
}

// ------------------------------------------------------- "RESOLUÇÃO DO WINDOWS" (14/09/2026)
//
// O chip de reconstrucao com valor -1 ("Resolução do Windows") acompanha a area de
// trabalho - mas isso so faz sentido POR MONITOR, e com dois monitores "a area de
// trabalho" sem dizer qual e um numero que engana. Segue o monitor em que a NOSSA janela
// esta (MonitorFromWindow), NUNCA o principal e NUNCA a area virtual somada
// (SM_CXVIRTUALSCREEN soma os dois monitores num retangulo que nao existe em tela
// nenhuma) - e usa a area FISICA do monitor (rcMonitor), a MESMA conta que o arranque faz
// para reconstrucao=-1 (main.cpp, bloco "RESOLUCAO DE RECONSTRUCAO (ate onde a rede vai)").
//
// AMORTECIMENTO: a mesma ideia do FonteEstabilizou (main.cpp:14063-14098, 250 ms) - so que
// aqui nao ha cadeia viva para reconfigurar (o backbuffer so nasce depois do "INICIAR"; ver
// M11 no dossie: redimensionar a cadeia NGX em voo MATA o processo), entao nao ha nada
// caro a proteger, so o ROTULO e o LOG. E esta tela ja e "amortecida" por construcao: o
// arrasto (WM_MOUSEMOVE com g_arrastando) so move a janela via SetWindowPos e NUNCA chama
// Paint()/Render() no meio do arrasto - o unico ponto em que isto e reavaliado e em
// eventos discretos (hover, clique, fim do arrasto, WM_DISPLAYCHANGE), nunca a cada
// posicao intermediaria da enxurrada de WM_MOUSEMOVE. Por isso um temporizador seria
// redundante aqui; o log so escreve quando o monitor OU a resolucao realmente mudam.
static HMONITOR     g_winResMon = nullptr;
static UINT         g_winResW = 0, g_winResH = 0;
static std::wstring g_winResDev;   // "\\.\DISPLAY2", so para o log
static void AplicarResolucaoWindows(HMONITOR m) {
    if (!m) return;
    MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(m, (MONITORINFO*)&mi)) return;
    const UINT w = (UINT)(mi.rcMonitor.right - mi.rcMonitor.left);
    const UINT h = (UINT)(mi.rcMonitor.bottom - mi.rcMonitor.top);
    if (!w || !h) return;
    const bool mudou = (m != g_winResMon) || (w != g_winResW) || (h != g_winResH);
    g_winResMon = m; g_winResW = w; g_winResH = h; g_winResDev = mi.szDevice;
    if (mudou)
        Log("seletor: \"Resolução do Windows\" (upscaling) segue %ls%ls - agora %ux%u",
            g_winResDev.c_str(),
            (mi.dwFlags & MONITORINFOF_PRIMARY) ? T(L" (principal)", L" (primary)") : L"", w, h);
}
// Chamada barata (duas chamadas Win32 locais - o mesmo espirito de ConferirUmPorUm em
// main.cpp): pode entrar em todo Render() sem custar nada de GPU nem I/O.
static void VerificarResolucaoWindows() {
    if (!g_s || !g_s->wnd) return;
    HMONITOR m = MonitorFromWindow(g_s->wnd, MONITOR_DEFAULTTONEAREST);
    if (m) AplicarResolucaoWindows(m);
}

// ------------------------------------------------------------------ desenho
// uma linha de lista (janela / monitor / camera / modo)
static void Linha(Graphics* g, const RectF& r, bool sel, bool hot, const wchar_t* txt, const wchar_t* dir) {
    if (sel)      FillRound(g, r, 8.f, C_ACC);
    else if (hot) FillRound(g, r, 8.f, C_ROWH);
    const Color ct = sel ? C_DARK : C_BODY, cd = sel ? C_DARK : C_FAINT;
    // LARGURA DA COLUNA DA DIREITA, MEDIDA - nao mais fixa (14/09/2026, 8a rodada, achado de
    // D:\claude-scratch\validacao\RELATORIO.md item 3: a linha "Hollow Knight" saia como
    // "1920x10..." cortada). 190 px fixos cortava "proc.exe  ·  LxA" toda vez que o nome do
    // processo era mais comprido que a media - jogos com nome de processo curto cabiam por
    // acaso, nao por calculo certo. Agora mede o texto de verdade (LarguraTexto, a mesma
    // funcao usada nos chips) e so limita a no MAXIMO 45% da linha, pra sempre sobrar espaco
    // de verdade pro titulo - que continua com reticencias proprias se precisar, mas o
    // titulo e o dado principal da linha, nao a resolucao.
    REAL wDir = 0.f;
    if (dir && *dir) {
        wDir = LarguraTexto(dir, fItemSub) + 4.f;
        const REAL wDirMax = r.Width * 0.45f;
        if (wDir > wDirMax) wDir = wDirMax;
    }
    Txt(g, txt, fLinha, ct, RectF(r.X + 12, r.Y, r.Width - 24 - wDir, r.Height), 0, true);
    if (wDir > 0) Txt(g, dir, fItemSub, cd, RectF(r.X + r.Width - 12 - wDir, r.Y, wDir, r.Height), 2, true);
}
// a barrinha de rolagem, so quando a lista nao cabe
static void Rolagem(Graphics* g, const RectF& box, int n, int vis, int rol) {
    if (n <= vis) return;
    const RectF trilho(box.X + box.Width - 8, box.Y + 6, 4.f, box.Height - 12);
    FillRound(g, trilho, 2.f, C_OFF);
    const REAL h = trilho.Height * (REAL)vis / (REAL)n;
    const REAL y = trilho.Y + (trilho.Height - h) * (REAL)rol / (REAL)(n - vis);
    FillRound(g, RectF(trilho.X, y, 4.f, h < 16 ? 16 : h), 2.f, C_FAINT);
}
static int Add(const RectF& r, int kind, int val, bool en, const std::wstring& tip = L"") {
    g_it.push_back(It{ r, kind, val, en, tip });
    return (int)g_it.size() - 1;
}
static void Chip(Graphics* g, const RectF& r, const wchar_t* t, bool sel, bool hot, bool en = true) {
    FillRound(g, r, 9.f, sel ? C_ACC : (hot && en) ? C_ROWH : C_ROW);
    Txt(g, t, fChip, sel ? C_DARK : en ? C_BODY : C_FAINT, r, 1, true);
}

static int Render(Graphics* g) {
    EnsureFonts();
    g_it.clear();
    Estado& s = *g_s;
    const REAL L = (REAL)(SH + PAD), IW = (REAL)(CW - PAD * 2), R = L + IW;
    const REAL top = (REAL)SH;
    REAL cy = top;

    // -------- barra de titulo (desenhada): titulo, idioma e o X
    {
        const REAL ty = cy + 12;
        // A CAPIVARA ABRE O CABECALHO. Esta e a primeira janela que a pessoa ve, e ate aqui
        // era a unica das nossas telas sem a marca. Vai ANTES do titulo, no mesmo canto que
        // ela ocupa no painel e no Perfil, e o titulo comeca depois dela: nada de sobrepor.
        // Se o recurso faltar (exe sem icone embutido), DesenharMarca nao desenha nada e o
        // titulo continua onde esta - o layout nao depende da imagem.
        const REAL marca = 28.f, apos = marca + 10.f;
        if (g) Hud::DesenharMarca(g, L, ty + 1, (int)marca);
        if (g) TxtSpaced(g, L"DLSS5 FOR ALL", fTitle, C_TXT, L + apos, ty, 30.f, 1.6f);
        REAL x = L + apos;
        for (const wchar_t* p = L"DLSS5 FOR ALL"; *p; p++) { wchar_t um[2] = { *p, 0 }; x += LarguraTexto(um, fTitle) + 1.6f; }
        if (g) Txt(g, APP_VERSION, fVer, C_FAINT, RectF(x + 2, ty + 8, 40, 18), 0);
        x += 30;
        if (g) Txt(g, T(L"—  escolha a fonte", L"—  pick the source"), fSub, C_DIM, RectF(x, ty, 260, 30), 0, true);
        // o X
        RectF rx(R - 28, ty + 2, 26, 26);
        int idx = Add(rx, K_X, 0, true, TS(L"Fechar\nFecha sem abrir nada (Esc).", L"Close\nCloses without opening anything (Esc)."));
        if (g) {
            FillRound(g, rx, 13.f, g_hot == idx ? C_ROWH : C_ROW);
            Txt(g, L"✕", fChip, C_BODY, rx, 1);
        }
        // idioma: dois chips, cada um na propria lingua
        const std::wstring tipL = TS(L"Idioma / Language\nA tela reabre no idioma escolhido.", L"Language / Idioma\nThe screen reopens in the chosen language.");
        REAL cx = rx.X - 10;
        const Lingua ls[2] = { LINGUA_EN, LINGUA_PT };
        for (Lingua l : ls) {
            const REAL w = LarguraTexto(NomeLingua(l), fChip) + 22;
            RectF rc(cx - w, ty + 2, w, 26);
            const bool sel = (g_lingua == l);
            int i2 = Add(rc, K_LANG, (int)l, true, tipL);
            if (g) Chip(g, rc, NomeLingua(l), sel, g_hot == i2);
            cx = rc.X - 6;
        }
        cy += TOPO_H;
        if (g) { Pen p(C_SEP, 1.f); g->DrawLine(&p, L, cy, R, cy); }
        cy += 16;
    }

    // -------- FONTE: tres cartoes, um selecionado
    {
        if (g) Txt(g, T(L"FONTE", L"SOURCE"), fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;
        const REAL gap = 10, bw = (IW - gap * 2) / 3, bh = 64;
        const SourceKind ks[3] = { SRC_WINDOW, SRC_MONITOR, SRC_CAMERA };
        const wchar_t* nome[3] = { T(L"Janela", L"Window"), L"Monitor", T(L"Câmera", L"Camera") };
        const wchar_t* sub[3]  = { T(L"recomendado · mais leve", L"recommended · lighter"),
                                   T(L"a tela inteira", L"the whole screen"),
                                   T(L"placa de captura · console", L"capture card · console") };
        for (int i = 0; i < 3; i++) {
            RectF r(L + i * (bw + gap), cy, bw, bh);
            const bool sel = (s.fonte == ks[i]);
            int idx = Add(r, K_FONTE, (int)ks[i], true, TipFonte(ks[i]));
            if (g) {
                FillRound(g, r, 12.f, (g_hot == idx && !sel) ? C_ROWH : C_ROW);
                if (sel) { StrokeRound(g, r, 12.f, C_ACC, 1.5f); FillRound(g, RectF(r.X, r.Y + 14, 5.f, r.Height - 28), 2.f, C_ON); }
                Txt(g, nome[i], fItem, sel ? C_TXT : C_BODY, RectF(r.X + 18, r.Y + 10, r.Width - 30, 22), 0, true);
                Txt(g, sub[i], fItemSub, sel ? C_ACC2 : C_FAINT, RectF(r.X + 18, r.Y + 34, r.Width - 30, 18), 0, true);
            }
        }
        cy += bh + 16;
    }

    // -------- a lista do alvo
    {
        const int n = NumAlvos(s);
        const wchar_t* lbl = s.fonte == SRC_WINDOW ? T(L"QUAL JANELA", L"WHICH WINDOW")
                           : s.fonte == SRC_MONITOR ? T(L"QUAL MONITOR", L"WHICH MONITOR")
                                                    : T(L"QUAL CÂMERA OU PLACA", L"WHICH CAMERA OR CARD");
        if (g) {
            Txt(g, lbl, fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
            wchar_t c[64]; _snwprintf(c, 64, T(L"%d encontrada%s", L"%d found"), n, n == 1 ? L"" : L"s");
            if (n > 0) Txt(g, c, fLbl, C_FAINT, RectF(L, cy, IW, 16), 2);
        }
        cy += 22;
        const int vis = n < kLinhasAlvo ? n : kLinhasAlvo;
        int& sel = SelAlvo(s);
        if (n > 0 && s.rolAlvo > n - vis) s.rolAlvo = n - vis;
        if (s.rolAlvo < 0) s.rolAlvo = 0;
        const REAL boxH = (n == 0) ? (s.fonte == SRC_CAMERA ? 92.f : 56.f) : vis * kLinhaH + 8;
        RectF box(L, cy, IW, boxH);
        g_rcListaAlvo = box;
        if (g) { FillRound(g, box, 12.f, C_LISTA); StrokeRound(g, box, 12.f, C_EDGE, 1.f); }
        if (n == 0) {
            if (g) {
                const wchar_t* vazio = s.fonte == SRC_WINDOW
                    ? T(L"Nenhuma janela encontrada. Abra o jogo e clique em \"Atualizar lista\".",
                        L"No window found. Open the game and click \"Refresh list\".")
                    : s.fonte == SRC_MONITOR
                    ? T(L"Nenhum monitor encontrado. Clique em \"Atualizar lista\".", L"No monitor found. Click \"Refresh list\".")
                    : T(L"Nenhuma placa de captura ou câmera foi encontrada neste PC.\n"
                        L"Se ela está ligada agora, clique em \"Atualizar lista\".\n"
                        L"Para filtrar um jogo que roda nesta máquina, escolha \"Janela\" ali em cima.",
                        L"No capture card or camera was found on this PC.\n"
                        L"If it is plugged in right now, click \"Refresh list\".\n"
                        L"To filter a game running on this machine, pick \"Window\" up there.");
                TxtWrap(g, vazio, fLinha, C_DIM, RectF(box.X + 16, box.Y + 12, box.Width - 32, box.Height - 20));
            }
        } else {
            const REAL wRol = (n > vis) ? 12.f : 0.f;
            for (int i = 0; i < vis; i++) {
                const int k = s.rolAlvo + i;
                if (k >= n) break;
                RectF r(box.X + 4, box.Y + 4 + i * kLinhaH, box.Width - 8 - wRol, kLinhaH);
                int idx = Add(r, K_ALVO, k, true);
                if (g) {
                    if (s.fonte == SRC_WINDOW) {
                        const WinTarget& w = s.wins[k];
                        wchar_t d[128]; _snwprintf(d, 128, L"%s  ·  %dx%d", w.proc.c_str(), w.w, w.h);
                        Linha(g, r, sel == k, g_hot == idx, w.title.c_str(), d);
                    } else if (s.fonte == SRC_MONITOR) {
                        Linha(g, r, sel == k, g_hot == idx, s.mons[k].label.c_str(), nullptr);
                    } else {
                        Linha(g, r, sel == k, g_hot == idx, (*s.devs)[k].name.c_str(), nullptr);
                    }
                }
            }
            if (g) Rolagem(g, box, n, vis, s.rolAlvo);
        }
        cy += boxH + 10;
    }

    // -------- janela: o recorte, dois chips numa linha so
    if (s.fonte == SRC_WINDOW) {
        if (g) Txt(g, T(L"Recorte", L"Crop"), fItemSub, C_FAINT, RectF(L, cy, 120, 30), 0);
        const wchar_t* nomes[2] = { T(L"Área útil", L"Client area"), T(L"Janela inteira", L"Whole window") };
        const std::wstring tips[2] = {
            TS(L"ÁREA ÚTIL\nSó o conteúdo da janela, sem barra de título e bordas. É o que se quer quase sempre.",
               L"CLIENT AREA\nOnly the window's content, without title bar and borders. Almost always what you want."),
            TS(L"JANELA INTEIRA\nCom a moldura do Windows (barra de título e bordas) dentro da imagem.",
               L"WHOLE WINDOW\nWith the Windows frame (title bar and borders) inside the image.") };
        REAL x = L + 70, cyChips = cy;
        for (int i = 0; i < 2; i++) {
            const REAL w = LarguraTexto(nomes[i], fChip) + 28;
            ProxLinhaDeChip(x, cyChips, w, L + 70, IW - 70);
            RectF r(x, cyChips, w, 30);
            const bool sel = (s.clientOnly == (i == 0));
            int idx = Add(r, K_RECORTE, i, true, tips[i]);
            if (g) Chip(g, r, nomes[i], sel, g_hot == idx);
            x += w + 8;
        }
        cy = cyChips + 30 + 4;
        // ------- O MODO "JOGO EM JANELA, PELICULA NA TELA" (14/09/2026)
        // Nao e uma chavinha nova, e por isso esta linha existe: com a fonte JANELA o
        // programa ja abre assim sozinho - a pelicula cobre o monitor, o jogo fica na janela
        // dele por tras, com o foco, e o clique atravessa. Sem esta linha a pessoa nao tem
        // como saber que e assim que vai ficar (foi o relato de 14/09: ele escolheu janela,
        // a pelicula cobriu a tela e ele nao entendeu para onde tinha ido o teclado).
        // O tamanho ate onde a rede reconstroi e a resolucao da janela do jogo ficam no
        // painel (tecla End), e o texto diz isso para nao virar procura.
        if (g) {
            TxtWrap(g, T(L"o jogo fica na janela dele e a película cobre a tela inteira — o teclado e o mouse "
                     L"continuam indo para o jogo. No painel (End) dá para escolher até onde a rede reconstrói "
                     L"e mudar a resolução da janela do jogo.",
                     L"the game stays in its window and the layer covers the whole screen — keyboard and mouse "
                     L"still go to the game. In the panel (End) you can choose how far the network rebuilds and "
                     L"change the game window's resolution."),
                fItemSub, C_FAINT, RectF(L, cy, IW, 40));
        }
        cy += 44;
    }

    // ============ O QUE SE MONTA ANTES DE COMECAR (14/09/2026) ============
    //
    // POR QUE ESTES QUATRO ESTAO AQUI E NAO NO PAINEL: os tres primeiros so valem AO
    // REABRIR (o tamanho do backbuffer, o numero de buffers do swapchain e a janela do jogo
    // nascem no arranque), e um controle que so vale ao reabrir, colocado no painel de jogo,
    // ensina a pessoa que clicar nao adianta. Foi a queixa que o usuario repetiu por dois
    // dias. Aqui ele esta parado, com espaco e tempo de ler.
    // A RESOLUCAO DO JOGO tambem continua no painel, porque aquela vale NA HORA - as duas
    // telas leem e escrevem a MESMA chave do ini, entao o estado e um so.
    // VALE PARA A PLACA DE CAPTURA TAMBEM (14/09/2026). O caminho depois da fonte e o MESMO -
    // captura -> backbuffer -> feeder -> DLSS -> apresentacao -, e o modo camera e ate o mais
    // favoravel: sinal de tamanho fixo, sem janela mudando, sem DPI, sem o jogo trocando de
    // resolucao por dentro, e sem ponte cross-adapter (a amostra ja nasce na placa escolhida).
    // So a "resolucao de renderizacao" fica de fora, e por um motivo simples: nao ha janela de
    // jogo para redimensionar - quem manda ali e o modo da placa de captura.
    {
        if (g) Txt(g, T(L"ANTES DE COMEÇAR", L"BEFORE YOU START"), fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;

        // ---- resolucao do jogo (so com fonte JANELA: e a janela dele que se redimensiona)
        if (s.fonte == SRC_WINDOW) {
            if (g) Txt(g, T(L"Resolução de renderização  ·  o jogo desenha nesta resolução (menos = mais FPS)",
                            L"Render resolution  ·  the game draws at this resolution (lower = more FPS)"),
                       fItemSub, C_BODY, RectF(L, cy, IW, 18), 0);
            cy += 22;
            // kJogoRes/kNJogoRes vem de main.cpp (fonte unica - 2160/4K acrescentado
            // 14/09/2026, pedido do usuario: quem tem monitor 4K pode querer o jogo nativo
            // em 4K e usar o programa so pela rede neural, sem ampliar nada).
            REAL x = L, cyChips = cy;
            for (int i = 0; i < kNJogoRes; i++) {
                wchar_t b[16];
                if (kJogoRes[i] == 0) wcscpy(b, T(L"Nativo", L"Native")); else _snwprintf(b, 16, L"%dp", kJogoRes[i]);
                b[15] = 0;
                const REAL w = LarguraTexto(b, fChip) + 28;
                ProxLinhaDeChip(x, cyChips, w, L, IW);
                RectF r(x, cyChips, w, 30);
                const int idx = Add(r, K_JOGORES, kJogoRes[i], true,
                    TS(L"RESOLUÇÃO DO JOGO\nRedimensiona a JANELA do jogo, para ele desenhar menos — e o que "
                       L"economiza placa de verdade. A borda não aparece: a película captura só a área útil e "
                       L"cobre a tela inteira.\nSó funciona com o jogo em janela COM BORDAS.",
                       L"GAME RESOLUTION\nResizes the game's WINDOW so it draws less — that is what really saves "
                       L"GPU. The border does not show: the layer captures only the client area and covers the "
                       L"whole screen.\nOnly works with the game in a BORDERED window."));
                if (g) Chip(g, r, b, s.jogoRes == kJogoRes[i], g_hot == idx);
                x += w + 8;
            }
            cy = cyChips + 30 + 12;
        }

        // ---- resolucao de reconstrucao (upscaling): ate onde a rede amplia a imagem
        //
        // A LISTA E SEMPRE A MESMA, COMPLETA, com o 4K incluso - independente da resolucao
        // ATUAL da area de trabalho ou do monitor. Às vezes a tela está reduzida, e é
        // justamente aí que reconstruir para uma resolução maior faz mais sentido (palavra
        // do usuario, 14/09/2026): se a lista seguisse a tela ela ofereceria MENOS opções
        // exatamente no caso em que o programa tem MAIS a oferecer. NÃO FILTRAR pela
        // resolução atual — alguém vai querer "otimizar" isto de volta; não otimize.
        if (g) Txt(g, T(L"Upscaling  ·  até onde a IA amplia a imagem",
                        L"Upscaling  ·  how far the AI scales the image"),
                   fItemSub, C_BODY, RectF(L, cy, IW, 18), 0);
        cy += 22;
        {
            VerificarResolucaoWindows();
            // kReconstAlt/kReconstChave/kNReconst vem de main.cpp - fonte unica (3240/6K e
            // 4320/8K acrescentados 14/09/2026, pedido do usuario: "por que nao colocar mais
            // opcoes como 6K e 8K tambem?"). SEM TETO TECNICO encontrado: D3D11/12 aceitam
            // textura/backbuffer ate 16384 px por lado, 8K sobra com folga; o CreateFeature
            // do NGX ja e protegido por SEH no feeder e ja degrada com log em vez de travar
            // se recusar um tamanho. 8K tambem e o proprio teto que a NVIDIA documenta para
            // o DLSS Super Resolution - por isso a lista para exatamente ai.
            wchar_t tipMon[64] = L"";
            if (g_winResW && g_winResH) {
                _snwprintf(tipMon, 64, T(L" (agora %ux%u)", L" (now %ux%u)"), g_winResW, g_winResH);
                tipMon[63] = 0;
            }
            // "RESOLUÇÃO DO WINDOWS" GANHA LINHA PROPRIA, ACIMA DOS DEGRAUS FIXOS
            // (14/09/2026, 8a rodada - conserto do estouro a 300%). Por que separar em vez
            // de so deixar a fileira quebrar: (1) e o UNICO item automatico ("segue o
            // monitor") contra uma serie de degraus FIXOS (numeros que nao mudam sozinhos) -
            // e conceitualmente diferente, nao so mais comprido; (2) e de longe o rotulo mais
            // comprido da fileira ("Resolução do Windows"/"Windows resolution"), entao
            // deixa-lo dentro da mesma fileira dos numeros obrigaria quase sempre a quebrar
            // ANTES dele mesmo, o que parece aleatorio; (3) a linha "agora = LxA" (logo
            // abaixo) fica associada ao chip DELE sem ambiguidade, em vez de vir depois de
            // uma fileira inteira que pode ja ter quebrado em duas linhas.
            {
                const int alvo = -1;
                wchar_t b[32];
                wcscpy(b, T(L"Resolução do Windows", L"Windows resolution"));
                b[31] = 0;
                const REAL w = LarguraTexto(b, fChip) + 28;
                RectF r(L, cy, w, 30);
                const std::wstring tip =
                    TS(L"RESOLUÇÃO DO WINDOWS — o padrão\nAcompanha a área de trabalho do monitor em que ESTA "
                       L"tela está agora", L"WINDOWS RESOLUTION — the default\nFollows the desktop area of the "
                       L"monitor this screen is on right now") + tipMon +
                    TS(L".\nCom dois monitores, segue o que estiver mostrando esta janela — nunca o monitor "
                       L"principal.\nDaí até a tela quem estica é a apresentação — é de graça, mas não inventa "
                       L"detalhe.\nNunca menor que a captura.",
                       L".\nWith two monitors, it follows whichever one is showing this window — never the main "
                       L"monitor.\nFrom there to the screen the presentation stretches it — free, but it invents "
                       L"no detail.\nNever smaller than the capture.");
                const int idx = Add(r, K_RECONST, alvo, true, tip);
                if (g) Chip(g, r, b, s.reconstrucao == alvo, g_hot == idx);
            }
            cy += 30 + 4;
            // O NUMERO VISIVEL, sem precisar parar o mouse: com dois monitores a pessoa nao
            // tem como adivinhar qual resolucao "Resolução do Windows" vai usar sem isto.
            if (g_winResW && g_winResH) {
                wchar_t sub[128];
                _snwprintf(sub, 128, T(L"“Resolução do Windows” agora = %ux%u",
                                       L"“Windows resolution” now = %ux%u"), g_winResW, g_winResH);
                sub[127] = 0;
                if (g) Txt(g, sub, fItemSub, C_FAINT, RectF(L, cy, IW, 16), 0, true);
                cy += 18;
            }
            cy += 4;
            // OS DEGRAUS FIXOS (Sem upscaling, 1080..4320p): fileira separada, quebra
            // sozinha se nao couber numa linha so (ProxLinhaDeChip, definido no topo do
            // arquivo) - era exatamente aqui que 7 chips (3240/6K e 4320/8K novos) estouravam
            // a tela a 300% de escala.
            REAL x = L, cyChips = cy;
            for (int i = 0; i < kNReconst; i++) {
                const int alvo = kReconstAlt[i];
                if (alvo < 0) continue;   // "Resolução do Windows" ja foi desenhada acima, sozinha
                wchar_t b[32];
                if (alvo == 0) wcscpy(b, T(L"Sem upscaling", L"No upscaling"));
                else _snwprintf(b, 32, L"%dp", alvo);
                b[31] = 0;
                const REAL w = LarguraTexto(b, fChip) + 28;
                ProxLinhaDeChip(x, cyChips, w, L, IW);
                RectF r(x, cyChips, w, 30);
                std::wstring tip =
                    TS(L"RECONSTRUÇÃO\nAté onde a rede neural reconstrói a imagem. Daí até a tela quem estica é a "
                       L"apresentação — é de graça, mas não inventa detalhe.\nSepara o custo da rede da resolução "
                       L"do seu monitor: jogo em 1080p, monitor 4K e reconstrução em 1440p é escolha legítima.\n"
                       L"Nunca menor que a captura. A lista mostra sempre TODAS as opções, mesmo que o monitor "
                       L"esteja numa resolução menor agora.",
                       L"RECONSTRUCTION\nHow far the neural network rebuilds the image. From there to the screen the "
                       L"presentation stretches it — free, but it invents no detail.\nIt separates the network's cost "
                       L"from your monitor's resolution.\nNever smaller than the capture. The list always shows ALL "
                       L"options, even if the monitor is at a lower resolution right now.");
                // AVISO DE CUSTO NOS DEGRAUS NOVOS (6K/8K): a lei de custo da rede (dossie:
                // ms = 6,66 + 4,41 x Mpx) FOI MEDIDA - mas so em resoluções menores. 6K/8K em
                // si nunca foram medidos nesta rede; o numero aqui e ARITMETICA sobre a lei
                // medida, nao uma medida direta - por isso "estimativa", nunca "medido".
                // Avisa, nao proibe (regra do usuario: "legal ele avisar mas tb n proibir").
                if (alvo >= 3240) tip += TS(
                    L"\n\n⚠ Acima de 4K o custo da rede cresce muito rápido e a maioria dos PCs não aguenta isso "
                    L"em tempo real — pode cair para poucos quadros por segundo (estimativa: ninguém mediu 6K/8K "
                    L"nesta rede ainda, a conta vem da lei medida em tamanhos menores). Mesmo assim, a escolha é "
                    L"sua.",
                    L"\n\n⚠ Above 4K the network's cost grows very fast and most PCs won't keep up in real time — "
                    L"it can drop to just a few frames per second (an estimate: nobody has measured 6K/8K on this "
                    L"network yet, this comes from the law measured at smaller sizes). Still your call, though.");
                const int idx = Add(r, K_RECONST, alvo, true, tip);
                if (g) Chip(g, r, b, s.reconstrucao == alvo, g_hot == idx);
                x += w + 8;
            }
            cy = cyChips + 30 + 4;
            // AVISAR, NUNCA PROIBIR (regra do usuario): reconstruir acima da tela onde a
            // pelicula vai aparecer nao e ilegitimo - so nao inventa pixel visivel nenhum a
            // mais e custa GPU a toa. A pessoa continua livre para escolher assim mesmo.
            const bool avisoGrande = s.reconstrucao > 0 && g_winResH > 0 && (UINT)s.reconstrucao > g_winResH;
            if (avisoGrande) {
                wchar_t av[220];
                _snwprintf(av, 220,
                    T(L"maior que a tela (%ux%u): os pixels a mais não aparecem e custam desempenho à toa — "
                      L"mesmo assim, é sua escolha.",
                      L"larger than the screen (%ux%u): the extra pixels don't show and cost performance for "
                      L"nothing — still your call."),
                    g_winResW, g_winResH);
                av[219] = 0;
                if (g) TxtWrap(g, av, fItemSub, C_WARN, RectF(L, cy, IW, 34));
                cy += 36;
            }
            // O MESMO AVISO DA DICA (6K/8K), TAMBEM COMO LINHA VISIVEL: quem ja escolheu um
            // degrau pesado pode nao ter parado o mouse para ler o tooltip. Linguagem de
            // jogador, sem formula - e marcado como estimativa porque e aritmetica sobre a
            // lei medida (dossie), nao uma medida direta de 6K/8K.
            if (s.reconstrucao >= 3240) {
                const wchar_t* av2 = T(
                    L"acima de 4K o custo da rede cresce muito rápido — a maioria dos PCs não aguenta isso em "
                    L"tempo real (estimativa; ninguém mediu a rede nesse tamanho ainda).",
                    L"above 4K the network's cost grows very fast — most PCs can't keep up in real time "
                    L"(an estimate; nobody has measured the network at this size yet).");
                if (g) TxtWrap(g, av2, fItemSub, C_WARN, RectF(L, cy, IW, 34));
                cy += 36;
            }
            cy += 8;
        }

        // ---- ritmo / limite de fps
        if (g) Txt(g, T(L"Limite de FPS",
                        L"FPS limit"),
                   fItemSub, C_BODY, RectF(L, cy, IW, 18), 0);
        cy += 22;
        {
            const wchar_t* nomes[3] = { T(L"Taxa do monitor", L"Monitor rate"),
                                        T(L"Livre (bancada)", L"Free (bench)"),
                                        T(L"Limite escolhido", L"Chosen cap") };
            REAL x = L, cyChips = cy;
            for (int i = 0; i < 3; i++) {
                const REAL w = LarguraTexto(nomes[i], fChip) + 28;
                ProxLinhaDeChip(x, cyChips, w, L, IW);
                RectF r(x, cyChips, w, 30);
                const int idx = Add(r, K_RITMO, i, true,
                    TS(L"RITMO\nDe quanto em quanto tempo a película dá uma volta (capturar → rede → apresentar).\n"
                       L"Taxa do monitor: uma volta por atualização de tela, sempre com o quadro mais novo — "
                       L"latência mínima, e o fps não passa do refresh.\nLivre: nada espera nada. É o único modo em "
                       L"que o fps que você lê é o teto REAL da cadeia.\nLimite escolhido: só o seu teto de fps segura.",
                       L"PACING\nHow often the layer runs one cycle (capture → network → present).\n"
                       L"Monitor rate: one cycle per screen refresh, always the newest frame — minimum latency, and "
                       L"fps will not exceed the refresh.\nFree: nothing waits. The only mode where the fps you read "
                       L"is the chain's REAL ceiling.\nChosen cap: only your fps cap holds."));
                if (g) Chip(g, r, nomes[i], s.ritmo == i, g_hot == idx);
                x += w + 8;
            }
            cy = cyChips + 30 + 12;
        }

        // ============ CONTROLES QUE SO VALEM COM UPSCALING LIGADO (14/09/2026) ============
        //
        // Pedido do usuario: "quando eu marco a opção sem upscaling ... a opção qualidade do
        // upscaling tem que sumir e os relacionados". A REGRA DELE E EXPLICITA: nao
        // adivinhar quais sao "os relacionados" - CONFERIR NO CODIGO, caso a caso, porque
        // esconder algo que ainda funciona e tao ruim quanto mostrar algo que nao funciona.
        // Conferido em feeder-amd/src-feeder/src/dlss5-feed.cpp:
        //
        //   - "Qualidade do upscaling" (K_CURTO, caminho curto): GravarCaminhoCurtoNoCfg
        //     (main.cpp) so grava short_w/short_h != 0 quando alvW < bbW || alvH < bbH - e
        //     com reconstrucao=0 o backbuffer NASCE do tamanho da captura (bbW=cw, bbH=ch),
        //     entao essa condicao e SEMPRE falsa. 100% inerte, sem excecao. -> SOME.
        //   - "Qualidade da IA" (K_IAORDEM, nr_pre_sr): NrAlvo() (dlss5-feed.cpp) so olha a
        //     escolha quando `g.sr_active` e true, e sr_active exige work_upscale==2 (Super
        //     Resolution). Com reconstrucao=0 o SR so liga sozinho quando a reconstrucao e
        //     MAIOR que a captura (main.cpp, bloco "SUPER RESOLUTION") - o que nao e o nosso
        //     caso -, MAS uma escolha explicita feita noutra tela ([ultima]
        //     super_resolucao=sr) sobrevive entre aberturas e forca o SR mesmo aqui. Por
        //     isso o gate le o ini, nao so o chip desta tela. -> SOME, SALVO ESSE CASO.
        //   - "Nitidez" (K_NITIDEZ, work_sharpness/RCAS): CONFERIDO E MANTIDO DE PROPOSITO -
        //     dlss5-feed.cpp:5447-5450, `rcas_aqui = ... && (easu_aqui || !escalando)`: o
        //     RCAS roda TAMBEM sem upscaling nenhum, sempre que a saida nao muda de tamanho
        //     (o caso exato de "Sem upscaling"). Ainda funciona -> FICA VISIVEL.
        //   - "Limite de FPS" e "Resolução de renderização": independem do upscaling (ritmo
        //     de apresentacao e tamanho da janela do JOGO, nao da rede) -> FICAM.
        //
        // NENHUM VALOR MUDA AO ESCONDER: e so o Render() que pula o bloco (nem o texto, nem
        // os chips, nem o espaco que eles ocupavam) - s.curto/s.iaOrdem e as chaves do ini
        // continuam exatamente como estavam, e reaparecem do jeito que a pessoa deixou se
        // ela ligar o upscaling de novo. O ReShade (F7) e os efeitos que a pessoa tenha posto
        // la CONTINUAM valendo com upscaling desligado - isto aqui nao mexe nisso.
        const bool upscalingLigado = (s.reconstrucao != 0);
        const bool srForcadoNoIni = (IniGet(L"super_resolucao") == L"sr");   // escolha explicita, persiste entre telas

        // ---- caminho curto do SR (so com upscaling ligado - ver o gate acima)
        if (upscalingLigado) {
            const wchar_t* nomes[2] = { T(L"Normal", L"Normal"), T(L"Máxima (recomendado)", L"Maximum (recommended)") };
            if (g) Txt(g, T(L"Qualidade do upscaling",
                            L"Upscaling quality"),
                       fItemSub, C_BODY, RectF(L, cy, IW, 18), 0);
            cy += 22;
            REAL x = L, cyChips = cy;
            for (int i = 0; i < 2; i++) {
                const REAL w = LarguraTexto(nomes[i], fChip) + 28;
                ProxLinhaDeChip(x, cyChips, w, L, IW);
                RectF r(x, cyChips, w, 30);
                const int idx = Add(r, K_CURTO, i, true,
                    TS(L"COMO A REDE RECEBE A IMAGEM\nNormal: o programa amplia a captura até o tamanho da "
                       L"reconstrução e a rede reduz de volta antes de olhar — duas reamostragens antes da "
                       L"primeira conta útil.\nMáxima (recomendado): a captura vai 1:1 e a rede recebe o render nativo do "
                       L"jogo. Medido FORA do app: +4,87 dB e 4,05 dB mais nítido.\nAinda não foi medido dentro do "
                       L"app nesta máquina — por isso não é o padrão. Ligue e compare.",
                       L"HOW THE NETWORK RECEIVES THE IMAGE\nNormal: the program upscales the capture to the "
                       L"reconstruction size and the network shrinks it back before looking — two resamples before "
                       L"the first useful computation.\nMaximum (recommended): the capture goes 1:1 and the network gets the "
                       L"game's native render. Measured OUTSIDE the app: +4.87 dB and 4.05 dB sharper.\nNot yet "
                       L"measured inside the app on this machine — that is why it is not the default."));
                if (g) Chip(g, r, nomes[i], s.curto == (i == 1), g_hot == idx);
                x += w + 8;
            }
            cy = cyChips + 30 + 12;
        }

        // ---- NITIDEZ: a barra, tambem aqui (SEMPRE VISIVEL - ver o gate acima: o RCAS
        // roda tambem sem upscaling, sempre que a saida nao muda de tamanho)
        // CLIQUE DIRETO NA BARRA, e nao arrasto: esta tela nao tem captura de mouse para
        // arrastar, e inventar uma so para isto seria mexer no que ja funciona. No painel
        // (tecla End) a mesma nitidez tem barra de ARRASTAR, que e onde ela importa - la a
        // pessoa esta vendo a imagem mudar. Aqui ela so escolhe o ponto de partida.
        {
            wchar_t rot[80];
            _snwprintf(rot, 80, T(L"Nitidez  -  %.2f", L"Sharpness  -  %.2f"), s.nitidez / 100.0);
            rot[79] = 0;
            if (g) Txt(g, rot, fItemSub, C_BODY, RectF(L, cy, IW, 18), 0);
            cy += 22;
            const REAL bh = 22, bw = IW;
            // Onze paradas de 0,00 a 1,00: cada uma e um item clicavel do tamanho de um
            // decimo da barra, entao clicar em qualquer ponto cai no valor mais proximo.
            for (int i = 0; i <= 10; i++) {
                RectF r(L + (bw / 11.f) * i, cy, bw / 11.f, bh);
                Add(r, K_NITIDEZ, i * 10, true,
                    TS(L"NITIDEZ - um realce aplicado DEPOIS de a IA ampliar; e o que os jogos chamam de nitidez do DLSS. O DLSS em si nao tem nitidez propria desde a versao 2.5.1. Nao cria detalhe: realca o contorno do que ja existe, e demais deixa halo. 0,00 desliga o passe; 0,88 e a referencia da propria AMD. No painel (End) esta mesma nitidez tem barra de ARRASTAR, para ajustar olhando a imagem.",
                       L"SHARPNESS - a sharpening pass applied AFTER the AI upscales; what games call DLSS sharpness. DLSS itself has had no sharpening since 2.5.1. It creates no detail: it enhances existing edges, and too much leaves halos. 0.00 turns the pass off; 0.88 is AMD's own reference. In the panel (End) the same sharpness has a DRAG bar, to tune it while watching the image."));
            }
            if (g) {
                RectF r(L, cy, bw, bh);
                FillRound(g, r, bh / 2, C_ROW);
                const REAL f = (REAL)s.nitidez / 100.f;
                if (f > 0.f) FillRound(g, RectF(r.X, r.Y, r.Width * f, bh), bh / 2, C_ACC);
                SolidBrush marca(C_FAINT);
                g->FillRectangle(&marca, r.X + r.Width * 0.88f, r.Y + 4.f, 1.5f, bh - 8.f);
                SolidBrush knob(Color(255, 255, 255, 255));
                g->FillEllipse(&knob, r.X + r.Width * f - bh / 2 + 2.f, r.Y + 2.f, bh - 4.f, bh - 4.f);
            }
            cy += bh + 12;
        }

        // ---- em que MOMENTO a IA trabalha (antes ou depois de ampliar) - so com upscaling
        // ligado, OU se o Super Resolution tiver sido escolhido a mao noutra tela (ver o
        // gate grande, acima de "caminho curto do SR").
        // O CONTROLE QUE DECIDE O CUSTO. A rede roda no tamanho da imagem que RECEBE: se
        // trabalhar ANTES de ampliar ela ve 720p (~11 ms pela lei medida); se trabalhar
        // DEPOIS ve 4K (~43 ms). Sao 4x - e foi isso que derrubou a pelicula para 23 fps no
        // teste de 14/09/2026, quando o caminho curto passou a entregar 4K para a rede.
        // O rotulo fala de QUALIDADE e CUSTO, que e o que o jogador decide; a ordem das
        // etapas fica no tooltip.
        if (upscalingLigado || srForcadoNoIni) {
            const wchar_t* nomes[2] = { T(L"Rapida (recomendado)", L"Fast (recommended)"),
                                        T(L"Maxima (pesa ~4x)", L"Maximum (~4x heavier)") };
            if (g) Txt(g, T(L"Qualidade da IA", L"AI quality"),
                       fItemSub, C_BODY, RectF(L, cy, IW, 18), 0);
            cy += 22;
            REAL x = L, cyChips = cy;
            for (int i = 0; i < 2; i++) {
                const REAL w = LarguraTexto(nomes[i], fChip) + 28;
                ProxLinhaDeChip(x, cyChips, w, L, IW);
                RectF r(x, cyChips, w, 30);
                const int idx = Add(r, K_IAORDEM, i, true,
                    TS(L"QUALIDADE DA IA\nDecide em que momento a rede neural trabalha."
                       L"Rapida: a IA melhora a imagem PEQUENA e so depois o upscaling amplia. A rede ve "
                       L"720p em vez de 4K e custa cerca de um quarto - e o que mantem o FPS alto.\n"
                       L"Maxima: o upscaling amplia primeiro e a IA trabalha na imagem ja grande. Pode "
                       L"recuperar detalhe que nao existia na imagem pequena, mas pesa cerca de 4 vezes.",
                       L"AI QUALITY\nDecides when the neural network works."
                       L"Fast: the AI improves the SMALL image and only then upscaling enlarges it. The "
                       L"network sees 720p instead of 4K and costs about a quarter - this is what keeps FPS high.\n"
                       L"Maximum: upscaling runs first and the AI works on the already-large image. It may "
                       L"recover detail that was not in the small image, but costs about 4x."));
                if (g) Chip(g, r, nomes[i], s.iaOrdem == i, g_hot == idx);
                x += w + 8;
            }
            cy = cyChips + 30 + 8;
        }
        // RODAPE GERAL (fora do bloco acima de proposito: ele vale para "Upscaling" e
        // "Limite de FPS" tambem, que continuam visiveis mesmo com "Qualidade da IA"
        // escondida - antes este texto vivia preso dentro do bloco da IA e sumia junto,
        // o que deixava as OUTRAS reguas desta secao sem o aviso).
        if (g) Txt(g, T(L"upscaling e limite de FPS valem quando o programa abre — por isso ficam aqui",
                        L"upscaling and FPS limit apply when the program opens — that is why they are here"),
                   fItemSub, C_FAINT, RectF(L, cy, IW, 18), 0, true);
        cy += 24;
    }

    // -------- camera: a lista de modos
    if (s.fonte == SRC_CAMERA) {
        const auto* ms = Modos(s);
        const int n = ms ? (int)ms->size() : 0;
        if (g) Txt(g, T(L"MODO  ·  resolução, fps e formato", L"MODE  ·  resolution, fps and format"), fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;
        const int vis = n < kLinhasModo ? n : kLinhasModo;
        if (n > 0 && s.rolModo > n - vis) s.rolModo = n - vis;
        if (s.rolModo < 0) s.rolModo = 0;
        const REAL boxH = (n == 0) ? 56.f : vis * kLinhaH + 8;
        RectF box(L, cy, IW, boxH);
        g_rcListaModo = box;
        if (g) { FillRound(g, box, 12.f, C_LISTA); StrokeRound(g, box, 12.f, C_EDGE, 1.f); }
        if (n == 0) {
            if (g && s.selDev >= 0)
                TxtWrap(g, T(L"Esse dispositivo não expôs nenhum modo de vídeo (em uso por outro app? Feche OBS/Discord/navegador e clique em \"Atualizar lista\").",
                             L"This device exposed no video mode at all (in use by another app? Close OBS/Discord/your browser and click \"Refresh list\")."),
                        fItemSub, C_DIM, RectF(box.X + 16, box.Y + 10, box.Width - 32, box.Height - 16));
        } else {
            const REAL wRol = (n > vis) ? 12.f : 0.f;
            for (int i = 0; i < vis; i++) {
                const int k = s.rolModo + i;
                if (k >= n) break;
                RectF r(box.X + 4, box.Y + 4 + i * kLinhaH, box.Width - 8 - wRol, kLinhaH);
                int idx = Add(r, K_MODO, k, true);
                if (g) Linha(g, r, s.selModo == k, g_hot == idx, (*ms)[k].label.c_str(), nullptr);
            }
            if (g) Rolagem(g, box, n, vis, s.rolModo);
        }
        cy += boxH + 14;
    }

    // -------- as chavinhas: so o nome curto; a explicacao e o tooltip
    {
        if (g) Txt(g, T(L"OPÇÕES", L"OPTIONS"), fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;
        const REAL gap = 10, bw = (IW - gap * 3) / 4, bh = 42;
        const bool tela = Tela(s);
        for (int i = 0; i < SW_N; i++) {
            const int col = i % 4, lin = i / 4;
            RectF r(L + col * (bw + gap), cy + lin * (bh + gap), bw, bh);
            const bool en = (i == SW_SOBREPOR || i == SW_CURSOR || i == SW_ESCONDER) ? tela : true;
            std::wstring tip = TipSw(i, s);
            if (!en) tip += T(L"\n(não se aplica à câmera)", L"\n(does not apply to the camera)");
            int idx = Add(r, K_SW, i, en, tip);
            if (g) {
                FillRound(g, r, 10.f, (g_hot == idx && en) ? C_ROWH : C_ROW);
                Txt(g, NomeSw(i), i == SW_SOBREPOR ? fBadge : fChip, en ? C_BODY : C_FAINT,
                    RectF(r.X + 12, r.Y, r.Width - 12 - 52, r.Height), 0, true);
                Switch(g, RectF(r.X + r.Width - 48, r.Y + (bh - 22) / 2, 40, 22), s.sw[i] && en, en);
            }
        }
        cy += bh * 2 + gap + 16;
    }

    // -------- a placa que roda o filtro: informa com uma, pergunta com duas ou mais
    {
        if (g) Txt(g, T(L"PLACA DE VÍDEO QUE RODA O FILTRO", L"GRAPHICS CARD THAT RUNS THE FILTER"), fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;
        const std::wstring tipG = TS(
            L"PLACA QUE RODA O FILTRO\nO modelo neural roda nesta placa. Com uma só placa na máquina não há o que "
            L"escolher; com duas ou mais, dá para rodar o DLSS 5 numa e o jogo na outra. A escolha fica gravada e "
            L"vale nas próximas aberturas também.",
            L"CARD THAT RUNS THE FILTER\nThe neural model runs on this card. With a single card there is nothing to "
            L"choose; with two or more you can run DLSS 5 on one and the game on the other. The choice is saved and "
            L"also applies to the next launches.");
        const int n = (int)s.placasIdx.size();
        auto rotulo = [&](int i) {
            const PlacaOpcao& p = g_placas[s.placasIdx[i]];
            return p.nome + (p.dlss ? TS(L"  ·  DLSS 5 ok", L"  ·  DLSS 5 ok") : TS(L"  ·  sem DLSS 5", L"  ·  no DLSS 5"));
        };
        if (n <= 1) {
            RectF r(L, cy, IW, 24);
            Add(r, K_INFO, 0, false, tipG);
            if (g) Txt(g, n == 0 ? T(L"(nenhuma placa encontrada)", L"(no graphics card found)") : rotulo(0).c_str(),
                       fLinha, n == 0 ? C_FAINT : C_BODY, r, 0, true);
            cy += 24 + 14;
        } else {
            const REAL gap = 10, bw = (IW - gap * (n - 1)) / n, bh = 40;
            for (int i = 0; i < n; i++) {
                RectF r(L + i * (bw + gap), cy, bw, bh);
                const bool sel = (s.selPlaca == i);
                int idx = Add(r, K_GPU, i, true, tipG);
                if (g) {
                    FillRound(g, r, 10.f, sel ? C_ACC : (g_hot == idx) ? C_ROWH : C_ROW);
                    const PlacaOpcao& p = g_placas[s.placasIdx[i]];
                    Txt(g, p.nome.c_str(), fBadge, sel ? C_DARK : C_BODY, RectF(r.X + 12, r.Y + 3, r.Width - 24, 18), 0, true);
                    Txt(g, p.dlss ? L"DLSS 5 ok" : T(L"sem DLSS 5", L"no DLSS 5"), fItemSub, sel ? C_DARK : C_FAINT,
                        RectF(r.X + 12, r.Y + 20, r.Width - 24, 16), 0, true);
                }
            }
            cy += bh + 14;
        }
    }

    // -------- a faixa dos arquivos que faltam (ver a explicacao junto de FaltaDlss)
    if (!s.faltaTxt.empty()) {
        const REAL th = AlturaTexto(s.faltaTxt.c_str(), fChip, IW - 40);
        const REAL bh = 12 + th + 10 + 30 + 12;
        RectF r(L, cy, IW, bh);
        if (g) {
            FillRound(g, r, 12.f, C_AMB_BG);
            StrokeRound(g, r, 12.f, Color(160, 251, 146, 60), 1.f);
            FillRound(g, RectF(r.X, r.Y + 14, 5.f, r.Height - 28), 2.f, C_WARN);
            TxtWrap(g, s.faltaTxt.c_str(), fChip, C_AMB_TX, RectF(r.X + 20, r.Y + 12, IW - 40, th + 4));
        }
        const wchar_t* b1 = g_moduloAmd ? T(L"Baixar o mod", L"Get the mod") : T(L"Baixar os arquivos", L"Get the files");
        const wchar_t* b2 = T(L"Abrir a pasta", L"Open the folder");
        const REAL by = r.Y + 12 + th + 10;
        RectF r1(r.X + 20, by, LarguraTexto(b1, fBadge) + 30, 30);
        RectF r2(r1.X + r1.Width + 8, by, LarguraTexto(b2, fBadge) + 30, 30);
        int i1 = Add(r1, K_BAIXAR, 0, true, TS(L"Abre no navegador a página de onde baixar.", L"Opens the download page in your browser."));
        int i2 = Add(r2, K_PASTA, 0, true, TS(L"Abre a pasta do app — é aqui que os arquivos têm de ficar.", L"Opens the app folder — this is where the files must go."));
        if (g) {
            FillRound(g, r1, 9.f, g_hot == i1 ? C_ACC2 : C_WARN); Txt(g, b1, fBadge, C_DARK, r1, 1);
            FillRound(g, r2, 9.f, g_hot == i2 ? C_ROWH : C_OFF);  Txt(g, b2, fBadge, C_BODY, r2, 1);
        }
        cy += bh + 14;
    }

    // -------- rodape: atualizar, atalhos, o resumo das teclas e o INICIAR
    if (g) { Pen p(C_SEP, 1.f); g->DrawLine(&p, L, cy, R, cy); }
    cy += 14;
    {
        const REAL bh = 40;
        const wchar_t* tA = T(L"Atualizar lista", L"Refresh list");
        const wchar_t* tK = T(L"Atalhos...", L"Shortcuts...");
        RectF ra(L, cy, LarguraTexto(tA, fChip) + 32, bh);
        RectF rk(ra.X + ra.Width + 8, cy, LarguraTexto(tK, fChip) + 32, bh);
        int ia = Add(ra, K_ATUALIZAR, 0, true, TS(L"ATUALIZAR LISTA\nProcura de novo as janelas, monitores ou câmeras. Use depois de abrir o jogo ou ligar um aparelho.",
                                                  L"REFRESH LIST\nLooks for windows, monitors or cameras again. Use it after opening the game or plugging a device in."));
        int ik = Add(rk, K_ATALHOS, 0, true, TS(L"ATALHOS\nVer e mudar as teclas do app (painel, liga/desliga) e a tecla que abre o ReShade.",
                                                L"SHORTCUTS\nSee and change the app's keys (panel, on/off) and the key that opens ReShade."));
        const bool pode = AlvoValido(s);
        const wchar_t* tI = T(L"INICIAR", L"START");
        RectF ri(R - 150, cy, 150, bh);
        std::wstring tipI = pode
            ? TS(L"INICIAR\nAbre o filtro com o que está escolhido acima. Enter também inicia.\nDepois de aberto: ",
                 L"START\nOpens the filter with what is chosen above. Enter also starts it.\nOnce it is open: ") + RodapeAtalhos()
            : TS(L"INICIAR\nEscolha um alvo na lista primeiro.", L"START\nPick a target in the list first.");
        int ii = Add(ri, K_INICIAR, 0, pode, tipI);
        if (g) {
            Chip(g, ra, tA, false, g_hot == ia);
            Chip(g, rk, tK, false, g_hot == ik);
            // o resumo das teclas, no meio: a pessoa ve como vai controlar o app antes de comecar
            Txt(g, RodapeAtalhos().c_str(), fItemSub, C_FAINT, RectF(rk.X + rk.Width + 10, cy, ri.X - (rk.X + rk.Width) - 20, bh), 1, true);
            FillRound(g, ri, bh / 2, pode ? ((g_hot == ii) ? C_ACC2 : C_ON) : C_OFF);
            Txt(g, tI, fBotao, pode ? C_DARK : C_FAINT, ri, 1);
        }
        cy += bh;
    }
    cy += PAD;
    return (int)(cy - top);
}

// a moldura: sombra macia e o cartao (igual a do painel, na largura desta tela)
static void Moldura(Graphics& g, int cardH) {
    for (int i = SH; i >= 1; i--) {
        RectF r((REAL)(SH - i), (REAL)(SH - i + 5), (REAL)(CW + 2 * i), (REAL)(cardH + 2 * i));
        FillRound(&g, r, 16.f + i, Color((BYTE)(3 + (SH - i)), 0, 0, 0));
    }
    RectF card((REAL)SH, (REAL)SH, (REAL)CW, (REAL)cardH);
    FillRound(&g, card, 16.f, C_CARD);
    StrokeRound(&g, card, 16.f, C_EDGE, 1.f);
}

// ------------------------------------------------------------------ o tooltip
// Cartao pequeno e escuro, titulo em negrito e corpo que quebra linha. Mede e desenha
// com a mesma funcao, para o PNG e a janelinha real sairem iguais.
static const REAL kTipPad = 12.f, kTipSH = 10.f;
struct TipMedida { std::wstring tit, corpo; REAL w = 0, h = 0, titH = 0; };
static TipMedida MedirTip(const std::wstring& t) {
    EnsureFonts();
    TipMedida m;
    const size_t q = t.find(L'\n');
    if (q == std::wstring::npos) { m.corpo = t; } else { m.tit = t.substr(0, q); m.corpo = t.substr(q + 1); }
    const REAL maxW = kTipMaxW - kTipPad * 2;
    REAL w = 0;
    if (!m.tit.empty()) { w = LarguraTexto(m.tit.c_str(), fTipT); m.titH = 20.f; }
    // a largura natural do corpo (a maior linha), limitada ao maximo; depois a altura
    // com quebra nessa largura
    REAL wc = 0;
    { size_t a = 0; while (a <= m.corpo.size()) { size_t b = m.corpo.find(L'\n', a); if (b == std::wstring::npos) b = m.corpo.size();
        const REAL lw = LarguraTexto(m.corpo.substr(a, b - a).c_str(), fTip); if (lw > wc) wc = lw; a = b + 1; } }
    if (wc > maxW) wc = maxW;
    if (wc > w) w = wc;
    if (w < 80) w = 80;
    const REAL ch = m.corpo.empty() ? 0.f : AlturaTexto(m.corpo.c_str(), fTip, w + 2);
    m.w = w + kTipPad * 2;
    m.h = kTipPad + m.titH + (m.titH > 0 && ch > 0 ? 4 : 0) + ch + kTipPad;
    return m;
}
static void DesenharTip(Graphics* g, const TipMedida& m, REAL x, REAL y) {
    RectF r(x, y, m.w, m.h);
    for (int i = 4; i >= 1; i--) FillRound(g, RectF(x - i, y - i + 2, m.w + 2 * i, m.h + 2 * i), 10.f + i, Color((BYTE)(10), 0, 0, 0));
    FillRound(g, r, 10.f, C_TIP_BG);
    StrokeRound(g, r, 10.f, Color(200, 6, 182, 212), 1.f);
    REAL ty = y + kTipPad;
    if (!m.tit.empty()) { Txt(g, m.tit.c_str(), fTipT, C_TXT, RectF(x + kTipPad, ty, m.w - kTipPad * 2, m.titH), 0, true); ty += m.titH + 4; }
    if (!m.corpo.empty()) TxtWrap(g, m.corpo.c_str(), fTip, C_BODY, RectF(x + kTipPad, ty, m.w - kTipPad * 2 + 2, m.h));
}
static LRESULT CALLBACK TipProc(HWND h, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcW(h, m, wp, lp); }
static void EsconderTip() {
    g_tipItem = -1;
    if (g_tip) ShowWindow(g_tip, SW_HIDE);
}
// posiciona a dica embaixo do item (ou em cima, se nao couber) e a desenha numa
// janela layered propria, ja com a escala da tela
static void MostrarTip(int item) {
    if (!g_s || !g_s->wnd || item < 0 || item >= (int)g_it.size() || g_it[item].tip.empty()) return;
    if (!g_tip) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = TipProc; wc.hInstance = g_s->inst; wc.lpszClassName = L"DLSS5ForAllSeletorDica";
        RegisterClassExW(&wc);
        g_tip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED,
                                wc.lpszClassName, L"", WS_POPUP, 0, 0, 10, 10, g_s->wnd, nullptr, g_s->inst, nullptr);
        if (!g_tip) return;
    }
    const TipMedida m = MedirTip(g_it[item].tip);
    const int W = (int)((m.w + kTipSH * 2) * g_scale + 0.5f), H = (int)((m.h + kTipSH * 2) * g_scale + 0.5f);
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
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.ScaleTransform(g_scale, g_scale);
        DesenharTip(&g, m, kTipSH, kTipSH);
    }
    // onde: embaixo do item, alinhado a esquerda dele, dentro do monitor
    RECT wr{}; GetWindowRect(g_s->wnd, &wr);
    const RectF& r = g_it[item].r;
    POINT pos{ wr.left + (LONG)((r.X - kTipSH) * g_scale), wr.top + (LONG)((r.Y + r.Height + 4 - kTipSH) * g_scale) };
    MONITORINFO mi{ sizeof(mi) };
    if (GetMonitorInfoW(MonitorFromWindow(g_s->wnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        if (pos.x + W > mi.rcWork.right) pos.x = mi.rcWork.right - W;
        if (pos.x < mi.rcWork.left) pos.x = mi.rcWork.left;
        if (pos.y + H > mi.rcWork.bottom) pos.y = wr.top + (LONG)((r.Y - 4 + kTipSH) * g_scale) - H;
        if (pos.y < mi.rcWork.top) pos.y = mi.rcWork.top;
    }
    SIZE sz{ W, H }; POINT srcpt{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_tip, screen, &pos, &sz, mem, &srcpt, 0, &bf, ULW_ALPHA);
    SelectObject(mem, oldb);
    DeleteObject(dib); DeleteDC(mem); ReleaseDC(nullptr, screen);
    SetWindowPos(g_tip, HWND_TOPMOST, pos.x, pos.y, W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    g_tipItem = item;
}

// ------------------------------------------------------------------ pintura da tela
static void Paint() {
    if (!g_s || !g_s->wnd) return;
    Hud::UpdateScale(g_s->wnd);
    g_scale = Hud::g_scale;
    const int cardH = Render(nullptr);
    int W = (int)((CW + SH * 2) * g_scale + 0.5f), H = (int)((cardH + SH * 2) * g_scale + 0.5f);
    // se nao couber no monitor (150% numa tela 1080p), encolhe ate caber - nunca abaixo
    // de 70%, senao vira ilegivel
    MONITORINFO mi{ sizeof(mi) };
    const bool temMon = GetMonitorInfoW(MonitorFromWindow(g_s->wnd, MONITOR_DEFAULTTOPRIMARY), &mi) != 0;
    if (temMon) {
        const REAL disp = (REAL)(mi.rcWork.bottom - mi.rcWork.top) - 16.f;
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
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.ScaleTransform(g_scale, g_scale);
        Moldura(g, cardH);
        Render(&g);
    }
    // centrada no monitor primario na primeira vez; depois fica onde a pessoa deixou
    // (a janela cresce para baixo quando a lista ou a faixa aparecem)
    POINT pos = g_pos;
    if (!g_posValida && temMon) {
        pos.x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - W) / 2;
        pos.y = mi.rcWork.top  + ((mi.rcWork.bottom - mi.rcWork.top) - H) / 2;
        g_pos = pos; g_posValida = true;
    }
    Hud::ClamparNaTela(pos, W, H); g_pos = pos;
    SIZE sz{ W, H }; POINT srcpt{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (!UpdateLayeredWindow(g_s->wnd, screen, &pos, &sz, mem, &srcpt, 0, &bf, ULW_ALPHA))
        Log("seletor: UpdateLayeredWindow falhou (erro %lu)", GetLastError());
    g_winW = W; g_winH = H;
    SelectObject(mem, oldb);
    DeleteObject(dib); DeleteDC(mem); ReleaseDC(nullptr, screen);
    // o tooltip aberto pode ter ficado sobre um item que mudou de lugar
    if (g_tipItem >= 0) { const int it = g_tipItem; if (it < (int)g_it.size() && it == g_hot) MostrarTip(it); else EsconderTip(); }
}

// p em pixels de cliente; o layout vive em unidades logicas
static int HitTest(POINT p) {
    const REAL x = p.x / g_scale, y = p.y / g_scale;
    for (size_t i = 0; i < g_it.size(); i++) {
        const RectF& r = g_it[i].r;
        if (x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height) return (int)i;
    }
    return -1;
}
static bool NoTopo(POINT p) {
    const REAL x = p.x / g_scale, y = p.y / g_scale;
    return x >= SH && x < SH + CW && y >= SH && y < SH + TOPO_H;
}

// ------------------------------------------------------------------ acoes
static void Cancelar() { if (g_s) { g_s->ok = false; DestroyWindow(g_s->wnd); } }
static void Iniciar() {
    Estado& s = *g_s;
    if (!AlvoValido(s)) return;
    s.ch.kind = s.fonte;
    s.ch.fs = s.sw[SW_TELACHEIA];
    s.ch.cursor = s.sw[SW_CURSOR];
    s.ch.hideSelf = s.sw[SW_ESCONDER];
    s.ch.vsync = s.sw[SW_VSYNC];
    s.ch.hdrTreat = s.sw[SW_HDR];
    s.ch.fpsTela = s.sw[SW_FPS];
    s.ch.placa = (s.selPlaca >= 0 && s.selPlaca < (int)s.placasIdx.size()) ? s.placasIdx[s.selPlaca] : -1;
    if (s.fonte == SRC_CAMERA) {
        s.ch.dev = s.selDev; s.ch.mode = s.selModo;
    } else if (s.fonte == SRC_WINDOW) {
        s.ch.win = s.wins[s.selWin].hwnd; s.ch.winTitle = s.wins[s.selWin].title;
        s.ch.clientOnly = s.clientOnly;
        s.ch.overlay = s.sw[SW_SOBREPOR];
    } else {
        s.ch.mon = s.mons[s.selMon].mon; s.ch.monLabel = s.mons[s.selMon].label;
        s.ch.overlay = s.sw[SW_SOBREPOR];
        if (s.ch.overlay) s.ch.hideSelf = true;   // sem isso e espelho infinito
    }
    s.ok = true;
    DestroyWindow(s.wnd);
}
static void TrocarFonte(SourceKind k) {
    Estado& s = *g_s;
    if (s.fonte == k) return;
    s.fonte = k;
    Log("seletor: fonte -> %s", k == SRC_WINDOW ? "janela" : k == SRC_MONITOR ? "monitor" : "camera");
    Recarregar(s);
}
static void MoverSelecao(int delta) {
    Estado& s = *g_s;
    const int n = NumAlvos(s);
    if (n == 0) return;
    int& sel = SelAlvo(s);
    int novo = sel + delta;
    if (novo < 0) novo = 0;
    if (novo >= n) novo = n - 1;
    if (novo == sel) return;
    sel = novo;
    const int vis = n < kLinhasAlvo ? n : kLinhasAlvo;
    if (sel < s.rolAlvo) s.rolAlvo = sel;
    if (sel >= s.rolAlvo + vis) s.rolAlvo = sel - vis + 1;
    if (s.fonte == SRC_CAMERA) CarregarModos(s);
}
static void MoverModo(int delta) {
    Estado& s = *g_s;
    const auto* ms = Modos(s);
    const int n = ms ? (int)ms->size() : 0;
    if (n == 0) return;
    int novo = s.selModo + delta;
    if (novo < 0) novo = 0;
    if (novo >= n) novo = n - 1;
    s.selModo = novo;
    const int vis = n < kLinhasModo ? n : kLinhasModo;
    if (s.selModo < s.rolModo) s.rolModo = s.selModo;
    if (s.selModo >= s.rolModo + vis) s.rolModo = s.selModo - vis + 1;
}
// POR VALOR, de proposito: o Paint() la embaixo refaz g_it, e uma referencia para dentro
// do vector ficaria pendurada no meio da acao.
static void Ativar(const It it) {
    Estado& s = *g_s;
    switch (it.kind) {
    case K_X: Cancelar(); return;
    case K_LANG: {
        const Lingua nova = (Lingua)it.val;
        if (nova == g_lingua) return;
        GravarIdioma(IniPath(), nova);
        Log("idioma: trocado para %s no seletor", U8(CodigoLingua(nova)).c_str());
        g_selTrocouIdioma = true;
        DestroyWindow(s.wnd);   // o wWinMain reabre o seletor ja no idioma novo
        return;
    }
    case K_FONTE: TrocarFonte((SourceKind)it.val); break;
    case K_ALVO: {
        int& sel = SelAlvo(s);
        if (sel != it.val) { sel = it.val; if (s.fonte == SRC_CAMERA) CarregarModos(s); }
        break;
    }
    case K_MODO: s.selModo = it.val; break;
    case K_RECORTE: s.clientOnly = (it.val == 0); break;
    // ANTES DE COMECAR: os quatro gravam no ini na hora do clique (e nao so no Iniciar), para
    // a escolha sobreviver a um "Cancelar" ou a um fechamento - a pessoa ja decidiu.
    case K_JOGORES:
        s.jogoRes = it.val;
        IniSet(L"jogo_resolucao", std::to_wstring(it.val));
        break;
    case K_RECONST:
        s.reconstrucao = it.val;
        IniSet(L"reconstrucao", it.val == 0 ? L"captura" : it.val < 0 ? L"monitor" : std::to_wstring(it.val));
        // A chave antiga acompanha: outras partes do programa ainda a leem, e duas chaves
        // para o mesmo efeito so se sustentam se uma mandar na outra.
        IniSet(L"saida_monitor", it.val != 0 ? L"1" : L"0");
        break;
    case K_RITMO:
        s.ritmo = it.val;
        IniSet(L"ritmo", it.val == 1 ? L"livre" : it.val == 2 ? L"limite" : L"monitor");
        break;
    case K_CURTO:
        s.curto = (it.val == 1);
        IniSet(L"caminho_curto", s.curto ? L"1" : L"0");
        break;
    case K_NITIDEZ:
        // A barra da nitidez tambem aqui, porque o usuario pediu para configurar tudo ANTES de
        // comecar. As duas telas escrevem a MESMA chave ([ultima] nitidez_ia) e o arranque a
        // leva para o work_sharpness do feeder - estado unico, sem copia divergente.
        s.nitidez = it.val;
        IniSet(L"nitidez_ia", std::to_wstring(it.val));
        break;
    case K_IAORDEM:
        s.iaOrdem = it.val;
        IniSet(L"ia_ordem", it.val == 1 ? L"depois" : L"antes");
        // A ORDEM "ANTES" SO EXISTE NO MOTOR PROPRIO, e por isso ela vem junto.
        // O add-on do RenoDX engancha no BACKBUFFER, ja ampliado: todos os caminhos de
        // avaliacao dele sao pos-upscale e o SLOT_COLOR (a imagem pequena) e invisivel la.
        // Gravar so a ordem deixaria o botao inerte - o feeder ate loga "INERTE nesta
        // configuracao", mas botao que aceita o clique e nao faz nada e o defeito que este
        // programa passou dois dias consertando.
        // ESCOLHA EXPLICITA, CONSEQUENCIA EXPLICITA: quem clica aqui esta na tela inicial,
        // antes de comecar, e o motor so troca porque ELE pediu. Os dois motores foram
        // medidos indistinguiveis em imagem e custo (dossie 3.3j: 43,14 x 43,14 ms,
        // PSNR 37,34 / SSIM 0,9982), entao a troca nao custa qualidade.
        if (it.val == 0) { IniSet(L"motor", L"proprio"); IniSet(L"motor_auto", L"1"); }
        break;
    case K_SW: {
        bool& v = s.sw[it.val];
        v = !v;
        // DESLIGAR O SOBREPOR PEDE CONFIRMACAO. Sem sobrepor o filtro nao aparece em cima do
        // jogo, e quem desliga sem querer conclui que "o programa nao faz nada". O aviso diz
        // o que muda e para que serve; "Nao" devolve a chavinha ligada.
        if (it.val == SW_SOBREPOR && !v) {
            EsconderTip();
            const int r = MessageBoxW(s.wnd,
                T(L"Sem SOBREPOR, o filtro NÃO aparece em cima do jogo: ele abre numa janela separada, "
                  L"com a imagem filtrada dentro dela.\n\nIsso serve para gravar no OBS ou comparar lado a lado. "
                  L"Para jogar com o DLSS 5 por cima do jogo, deixe ligado.\n\nDesligar mesmo assim?",
                  L"Without OVERLAY the filter does NOT show on top of the game: it opens in a separate window "
                  L"with the filtered image inside it.\n\nThat is for recording in OBS or comparing side by side. "
                  L"To play with DLSS 5 over the game, keep it on.\n\nTurn it off anyway?"),
                APP_TITLE, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
            if (r != IDYES) v = true;
        }
        break;
    }
    case K_GPU: s.selPlaca = it.val; break;
    case K_BAIXAR:
        // No modulo AMD o que falta e o mod DLSS-NR-on-AMD, nao o RHI.
        Log("seletor: aviso do DLSS 5 - abrindo %s", g_moduloAmd ? "a pagina do mod DLSS-NR-on-AMD" : "as releases do RHI");
        ShellExecuteW(nullptr, L"open", g_moduloAmd ? kUrlModAmd : kUrlRhi, nullptr, nullptr, SW_SHOWNORMAL);
        return;
    case K_PASTA:
        // A pasta do EXE, e nao a de dados: e ali que o ReShade procura os arquivos.
        Log("seletor: aviso do DLSS 5 - abrindo a pasta do app");
        ShellExecuteW(nullptr, L"open", g_exeDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    case K_ATUALIZAR:
        if (s.fonte == SRC_CAMERA) {
            if (s.devs && s.selDev >= 0 && s.selDev < (int)s.devs->size()) { (*s.devs)[s.selDev].modesLoaded = false; (*s.devs)[s.selDev].modes.clear(); }
            CarregarModos(s);
        } else {
            // a escolha atual vira a "ultima": recarregar nao pode perder o alvo ja escolhido
            if (s.fonte == SRC_WINDOW && s.selWin >= 0 && s.selWin < (int)s.wins.size()) s.wantWin = s.wins[s.selWin].title;
            if (s.fonte == SRC_MONITOR && s.selMon >= 0 && s.selMon < (int)s.mons.size()) s.wantMon = s.mons[s.selMon].label;
            Recarregar(s);
        }
        break;
    case K_ATALHOS:
        EsconderTip();
        ShowAtalhosDialog(s.wnd, s.inst);
        break;
    case K_INICIAR: Iniciar(); return;
    default: return;
    }
    if (g_s && IsWindow(g_s->wnd)) Paint();
}

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
        if (LOWORD(lp) == HTCLIENT) {
            POINT p{}; GetCursorPos(&p); ScreenToClient(h, &p);
            const int hit = HitTest(p);
            SetCursor(LoadCursorW(nullptr, (hit >= 0 && g_it[hit].enabled) ? IDC_HAND : NoTopo(p) ? IDC_SIZEALL : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        SetFocus(h);
        const int hit = HitTest(p);
        if (hit >= 0 && g_it[hit].enabled) return 0;   // e um controle: o LBUTTONUP resolve
        if (!NoTopo(p)) return 0;
        RECT wr{}; GetWindowRect(h, &wr);
        POINT cur{}; GetCursorPos(&cur);
        g_arrastando = true;
        g_arrastoOff.x = cur.x - wr.left;
        g_arrastoOff.y = cur.y - wr.top;
        EsconderTip();
        SetCapture(h);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (g_arrastando) {
            POINT cur{}; GetCursorPos(&cur);
            g_pos.x = cur.x - g_arrastoOff.x;
            g_pos.y = cur.y - g_arrastoOff.y;
            SetWindowPos(h, nullptr, g_pos.x, g_pos.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        const int hit = HitTest(p);
        if (hit != g_hot) {
            g_hot = hit;
            KillTimer(h, kTimerTip);
            if (g_tipItem >= 0 && g_tipItem != hit) EsconderTip();
            if (hit >= 0 && !g_it[hit].tip.empty()) SetTimer(h, kTimerTip, kTipMs, nullptr);
            Paint();
        }
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_TIMER:
        if (wp == kTimerTip) {
            KillTimer(h, kTimerTip);
            // sem exigir foco: o mouse chega aqui antes do clique, e a dica e para isso
            if (g_hot >= 0 && g_hot < (int)g_it.size()) MostrarTip(g_hot);
            return 0;
        }
        break;
    case WM_MOUSELEAVE:
        KillTimer(h, kTimerTip);
        EsconderTip();
        if (g_hot != -1) { g_hot = -1; Paint(); }
        return 0;
    case WM_CAPTURECHANGED:
        g_arrastando = false;
        return 0;
    case WM_LBUTTONUP: {
        if (g_arrastando) {
            g_arrastando = false;
            ReleaseCapture();
            Hud::ClamparNaTela(g_pos, g_winW, g_winH);
            SetWindowPos(h, nullptr, g_pos.x, g_pos.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            // FIM DO ARRASTO: o unico momento em que isto muda de verdade e quando o
            // arrasto PARA (nunca a cada posicao intermediaria - ver o comentario grande em
            // VerificarResolucaoWindows). Se a tela cruzou para outro monitor, "Resolução do
            // Windows" precisa refletir isso sem esperar o proximo hover.
            VerificarResolucaoWindows();
            Paint();
            return 0;
        }
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        const int hit = HitTest(p);
        if (hit >= 0 && g_it[hit].enabled) { KillTimer(h, kTimerTip); EsconderTip(); Ativar(g_it[hit]); }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        // a roda rola a lista que esta embaixo do cursor
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &p);
        const REAL x = p.x / g_scale, y = p.y / g_scale;
        const int passos = -GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        auto dentro = [&](const RectF& r) { return x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height; };
        if (g_s && dentro(g_rcListaAlvo))      { g_s->rolAlvo += passos; }
        else if (g_s && g_s->fonte == SRC_CAMERA && dentro(g_rcListaModo)) { g_s->rolModo += passos; }
        else return 0;
        EsconderTip();
        Paint();   // o Render corrige o rol para dentro dos limites
        return 0;
    }
    case WM_KEYDOWN:
        if (!g_s) break;
        switch (wp) {
        case VK_ESCAPE: Cancelar(); return 0;
        case VK_RETURN: Iniciar(); return 0;
        case VK_UP:    MoverSelecao(-1); Paint(); return 0;
        case VK_DOWN:  MoverSelecao(+1); Paint(); return 0;
        case VK_PRIOR: MoverModo(-1); Paint(); return 0;   // PageUp / PageDown: a lista de modos da camera
        case VK_NEXT:  MoverModo(+1); Paint(); return 0;
        case VK_LEFT:  TrocarFonte(g_s->fonte == SRC_WINDOW ? SRC_CAMERA : g_s->fonte == SRC_MONITOR ? SRC_WINDOW : SRC_MONITOR); Paint(); return 0;
        case VK_RIGHT: TrocarFonte(g_s->fonte == SRC_WINDOW ? SRC_MONITOR : g_s->fonte == SRC_MONITOR ? SRC_CAMERA : SRC_WINDOW); Paint(); return 0;
        case VK_F5:    if (!g_it.empty()) for (const It& it : g_it) if (it.kind == K_ATUALIZAR) { Ativar(it); break; } return 0;
        }
        break;
    // A TELA VOLTOU PARA A FRENTE: a pessoa pode ter acabado de instalar os arquivos em
    // outra janela. Reavaliar aqui e o que faz o aviso sumir sozinho.
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) { KillTimer(h, kTimerTip); EsconderTip(); }
        else if (g_s && AtualizarFalta(*g_s)) Paint();
        return 0;
    // A resolucao do Windows pode mudar com esta tela aberta (a pessoa foi em
    // Configuracoes, ou plugou/desligou um monitor) - "Resolução do Windows" precisa
    // acompanhar sem exigir que a pessoa mexa o mouse primeiro.
    case WM_DISPLAYCHANGE:
        VerificarResolucaoWindows();
        Paint();
        return 0;
    case WM_CLOSE: Cancelar(); return 0;
    case WM_DESTROY:
        KillTimer(h, kTimerTip);
        if (g_tip) { DestroyWindow(g_tip); g_tip = nullptr; }
        g_tipItem = -1;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ------------------------------------------------------------------ a entrada
static void MontarPlacas(Estado& s) {
    s.placasIdx.clear(); s.selPlaca = 0;
    for (size_t i = 0; i < g_placas.size(); i++) {
        if (g_placas[i].software) continue;
        if ((int)i == g_placaIdx) s.selPlaca = (int)s.placasIdx.size();
        s.placasIdx.push_back((int)i);
    }
}
static void PrepararEstado(Estado& s, const std::wstring& wantKind, bool wantFs, bool wantVsync, bool wantHdr) {
    // JANELA E O PADRAO DA PRIMEIRA VEZ (recomendado: a rede processa area, e a janela e
    // menor que o monitor). Quem ja escolheu antes manda; camera sem camera vira janela.
    s.fonte = (wantKind == L"monitor") ? SRC_MONITOR : (wantKind == L"camera") ? SRC_CAMERA : SRC_WINDOW;
    if (s.primeiraVez) s.fonte = SRC_WINDOW;
    if (s.fonte == SRC_CAMERA && (!s.devs || s.devs->empty())) s.fonte = SRC_WINDOW;
    for (int i = 0; i < SW_N; i++) s.sw[i] = false;
    // ANTES DE COMECAR: le do ini o que ja estava escolhido. A fonte da verdade e o ini -
    // as duas telas (seletor e painel) leem e escrevem as MESMAS chaves, entao o estado e um
    // so e o que se muda num lugar aparece no outro.
    {
        s.jogoRes = _wtoi(IniGet(L"jogo_resolucao").c_str());
        // kJogoRes vem de main.cpp - fonte unica, para os degraus nunca divergirem entre a
        // validacao aqui e os chips desenhados mais abaixo.
        bool ok = false;
        for (int i = 0; i < kNJogoRes; i++) if (s.jogoRes == kJogoRes[i]) ok = true;
        if (!ok) s.jogoRes = 0;

        std::wstring r = IniGet(L"reconstrucao");
        for (auto& c : r) c = (wchar_t)towlower(c);
        if (r.empty()) r = L"monitor";   // o mesmo padrao do arranque
        s.reconstrucao = (r == L"monitor") ? -1 : (r == L"captura" || r == L"nativo") ? 0 : _wtoi(r.c_str());
        // kReconstAlt vem de main.cpp - mesma razao (0 e -1 sao sempre validos; os demais
        // sao os degraus fixos 1080/1440/2160/3240/4320).
        bool okr = (s.reconstrucao == 0 || s.reconstrucao == -1);
        for (int i = 0; i < kNReconst; i++) if (s.reconstrucao == kReconstAlt[i]) okr = true;
        if (!okr) s.reconstrucao = -1;

        std::wstring t = IniGet(L"ritmo");
        for (auto& c : t) c = (wchar_t)towlower(c);
        s.ritmo = (t == L"livre" || t == L"solto") ? 1 : (t == L"limite" || t == L"personalizado") ? 2 : 0;

        s.curto = (IniGet(L"caminho_curto") == L"1");
        s.iaOrdem = (IniGet(L"ia_ordem") == L"depois") ? 1 : 0;
        {
            const std::wstring vn = IniGet(L"nitidez_ia");
            int k = vn.empty() ? 80 : _wtoi(vn.c_str());
            if (k < 0 || k > 100) k = 80;
            s.nitidez = k;
        }
    }
    s.sw[SW_TELACHEIA] = wantFs;
    s.sw[SW_VSYNC] = wantVsync;
    s.sw[SW_HDR] = wantHdr;
    // A chavinha do FPS le o ReShade.ini: a escolha salva em [ultima] fps_tela ja foi
    // aplicada la na abertura (ver wWinMain). Chave ausente = padrao do ReShade, ligado.
    s.sw[SW_FPS] = (FpsNaTelaNoReShadeIni() != 0);
    MontarPlacas(s);
}
} // namespace Sel

// A mesma assinatura de sempre: o wWinMain nao sabe que a tela mudou de roupa.
static bool ShowSelector(HINSTANCE hi, std::vector<Device>& devs, Choice& out,
                         const std::wstring& wantKind, const std::wstring& wantDev, const std::wstring& wantMode,
                         const std::wstring& wantWin, const std::wstring& wantMon, bool wantFs,
                         bool wantVsync, bool wantHdr, bool primeiraVez) {
    using namespace Sel;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = Proc; wc.hInstance = hi; wc.lpszClassName = L"DLSS5ForAllSeletor";
    wc.hCursor = nullptr;   // o WM_SETCURSOR decide (mao, mover ou seta)
    // hIconSm separado: e ele que aparece na barra de tarefas e no Alt+Tab pequeno.
    wc.hIcon   = IconeApp(GetSystemMetrics(SM_CXICON));
    wc.hIconSm = IconeApp(GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&wc);

    Estado st; st.devs = &devs; st.inst = hi;
    st.wantDev = wantDev; st.wantMode = wantMode; st.wantWin = wantWin; st.wantMon = wantMon;
    st.primeiraVez = primeiraVez;
    PrepararEstado(st, wantKind, wantFs, wantVsync, wantHdr);
    g_s = &st; g_hot = -1; g_tipItem = -1; g_arrastando = false;

    const std::wstring tituloSel = std::wstring(APP_TITLE L" " APP_VERSION) +
                                   T(L" — escolha a fonte", L" — pick the source");
    // WS_POPUP + layered: a barra de titulo e desenhada por nos. WS_EX_APPWINDOW mantem a
    // tela na barra de tarefas e no Alt+Tab, como toda primeira janela de um programa.
    st.wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, wc.lpszClassName, tituloSel.c_str(), WS_POPUP,
                             0, 0, 10, 10, nullptr, nullptr, hi, nullptr);
    if (!st.wnd) { Log("seletor: CreateWindowExW falhou (erro %lu)", (unsigned long)GetLastError()); g_s = nullptr; return false; }
    Recarregar(st);
    AtualizarFalta(st);
    Paint();
    ShowWindow(st.wnd, SW_SHOW);
    SetForegroundWindow(st.wnd);
    SetFocus(st.wnd);
    {
        RECT wr{}; GetWindowRect(st.wnd, &wr);
        Log("seletor aberto em %ld,%ld  %ldx%ld  escala=%.2f  fonte=%s  alvos=%d  itens=%u",
            wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, (double)g_scale,
            st.fonte == SRC_WINDOW ? "janela" : st.fonte == SRC_MONITOR ? "monitor" : "camera",
            NumAlvos(st), (unsigned)g_it.size());
        // DLSS5_LOG_ITENS=1 despeja as areas clicaveis em coordenadas de tela, como o painel
        // faz: e o que permite testar a tela de fora (clicar numa linha, parar o mouse numa
        // chavinha) sem adivinhar posicoes.
        wchar_t env[8]{};
        if (GetEnvironmentVariableW(L"DLSS5_LOG_ITENS", env, 8) && env[0] == L'1') {
            for (size_t i = 0; i < g_it.size(); i++) {
                const It& it = g_it[i];
                std::wstring nome;
                if (it.kind == K_ALVO) nome = st.fonte == SRC_WINDOW ? st.wins[it.val].title : st.fonte == SRC_MONITOR ? st.mons[it.val].label : (*st.devs)[it.val].name;
                Log("  item[%u] kind=%d val=%d ativo=%d  centro-na-tela=%ld,%ld  %s", (unsigned)i,
                    it.kind, it.val, (int)it.enabled,
                    wr.left + (LONG)((it.r.X + it.r.Width / 2) * g_scale),
                    wr.top  + (LONG)((it.r.Y + it.r.Height / 2) * g_scale), U8(nome).c_str());
            }
        }
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    g_s = nullptr;
    g_it.clear();
    SoltarFontes();
    out = st.ch;
    return st.ok;
}

// --------------------------------------------------------------- --seletor-shot
// Grava a tela num PNG, pelo MESMO Render da janela, com dados de exemplo: tres janelas,
// dois monitores, uma camera com dois modos e a faixa de arquivos escondida. Com
// comTooltip, desenha a dica aberta sobre a chavinha SOBREPOR. DLSS5_SELETOR_SHOT_FONTE
// (janela | monitor | camera) escolhe qual fonte aparece selecionada.
namespace Sel {
static bool SaveShot(const wchar_t* path, bool comTooltip) {
    EnsureFonts();
    static std::vector<Device> devsAmostra;
    devsAmostra.clear();
    {
        Device d; d.name = L"Elgato HD60 X"; d.modesLoaded = true;
        Mode m1; m1.w = 1920; m1.h = 1080; m1.fn = 60; m1.fd = 1; m1.label = L"1920x1080  60 fps  NV12"; d.modes.push_back(m1);
        Mode m2; m2.w = 3840; m2.h = 2160; m2.fn = 30; m2.fd = 1; m2.compressed = true; m2.label = L"3840x2160  30 fps  MJPG  (comprimido)"; d.modes.push_back(m2);
        devsAmostra.push_back(d);
    }
    // A FAIXA DE ARQUIVOS FICA DE FORA DA FOTO por padrao: a foto e de divulgacao e a
    // maquina que a tira quase sempre tem tudo no lugar, entao a faixa nunca apareceria -
    // e quando aparecesse, mostraria o estado da pasta de quem fotografou, nao do produto.
    // DLSS5_SELETOR_SHOT_FALTA=1 liga a faixa, e existe por um motivo concreto: e o unico
    // jeito de conferir numa imagem o texto que a pessoa vai ler quando faltar um arquivo.
    Estado st; st.devs = &devsAmostra; st.primeiraVez = true;
    {
        wchar_t ff[8]{};
        st.ignorarFalta = !(GetEnvironmentVariableW(L"DLSS5_SELETOR_SHOT_FALTA", ff, 8) && ff[0] == L'1');
    }
    PrepararEstado(st, L"", false, false, false);
    st.sw[SW_FPS] = true;
    WinTarget w1; w1.title = L"ELDEN RING™"; w1.proc = L"eldenring.exe"; w1.w = 2560; w1.h = 1080; st.wins.push_back(w1);
    WinTarget w2; w2.title = L"Hollow Knight"; w2.proc = L"hollow_knight.exe"; w2.w = 1920; w2.h = 1080; st.wins.push_back(w2);
    WinTarget w3; w3.title = L"RetroArch 1.19.1"; w3.proc = L"retroarch.exe"; w3.w = 1280; w3.h = 960; st.wins.push_back(w3);
    MonTarget mo1; mo1.label = L"\\\\.\\DISPLAY1  (principal)  3440x1440"; mo1.w = 3440; mo1.h = 1440; st.mons.push_back(mo1);
    MonTarget mo2; mo2.label = L"\\\\.\\DISPLAY2  1920x1080"; mo2.w = 1920; mo2.h = 1080; st.mons.push_back(mo2);
    st.selWin = 0; st.selMon = 0; st.selDev = 0; st.selModo = 0;
    wchar_t env[16]{};
    if (GetEnvironmentVariableW(L"DLSS5_SELETOR_SHOT_FONTE", env, 16)) {
        const std::wstring f = env;
        st.fonte = (f == L"monitor") ? SRC_MONITOR : (f == L"camera") ? SRC_CAMERA : SRC_WINDOW;
    }
    st.sw[SW_SOBREPOR] = Tela(st);
    st.sw[SW_ESCONDER] = (st.fonte == SRC_MONITOR);
    g_s = &st; g_hot = -1;
    // A faixa so existe depois de alguem PERGUNTAR o que falta; o caminho da foto ia
    // direto para o Render e o aviso nunca aparecia, mesmo com a chave ligada.
    if (!st.ignorarFalta) AtualizarFalta(st);
    g_scale = 1.f;   // o PNG sai sempre em 96 dpi, do tamanho de projeto
    int cardH = Render(nullptr);
    int tipItem = -1;
    if (comTooltip)
        for (size_t i = 0; i < g_it.size(); i++) if (g_it[i].kind == K_SW && g_it[i].val == SW_SOBREPOR) { g_hot = (int)i; tipItem = (int)i; }
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
    Moldura(g, cardH);
    Render(&g);
    if (tipItem >= 0) {
        const TipMedida m = MedirTip(g_it[tipItem].tip);
        const RectF& r = g_it[tipItem].r;
        REAL x = r.X, y = r.Y + r.Height + 4;
        if (x + m.w > SH + CW - 8) x = SH + CW - 8 - m.w;
        if (y + m.h > SH + cardH - 8) y = r.Y - 4 - m.h;
        DesenharTip(&g, m, x, y);
    }
    g_s = nullptr; g_it.clear();
    CLSID png{};
    if (Hud::EncoderClsid(L"image/png", &png) < 0) return false;
    const bool ok = bmp.Save(path, &png, nullptr) == Ok;
    SoltarFontes();
    return ok;
}
} // namespace Sel
