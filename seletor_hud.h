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
using Hud::FillRound; using Hud::StrokeRound; using Hud::Txt; using Hud::TxtSpaced;
// Rodada 27 (v9): A PALETA DO MOCKUP (design-gemini\mockup-consistente.html / DESIGN-SPEC), a
// mesma do painel v8 - superficie #1E1E1E, cartoes #2A2A2A, borda #333, destaque #00E5FF, texto
// branco / #A0A0A0. Os nomes C_* continuam os de antes (o desenho da tela inteira os usa); so o
// valor mudou. Antes vinham do Hud (a paleta azul-ardosia do painel antigo).
static const Color C_CARD (250,  30,  30,  30);   // #1E1E1E
static const Color C_EDGE (255,  51,  51,  51);   // #333333
static const Color C_TXT  (255, 255, 255, 255);
static const Color C_BODY (255, 232, 232, 232);
static const Color C_DIM  (255, 160, 160, 160);   // #A0A0A0
static const Color C_FAINT(255, 160, 160, 160);
static const Color C_ON   (255,   0, 229, 255);   // #00E5FF
static const Color C_ACC  (255,   0, 229, 255);
static const Color C_ACC2 (255,  51, 234, 255);   // #33EAFF
static const Color C_ROW  (255,  42,  42,  42);   // #2A2A2A
static const Color C_ROWH (255,  58,  58,  58);   // #3A3A3A
static const Color C_OFF  (255,  51,  51,  51);
static const Color C_DARK (255,   0,   0,   0);   // texto sobre o ciano
static const Color C_WARN (255, 255, 171,   0);   // #FFAB00
static const Color C_SEP  (255,  51,  51,  51);
using Hud::fTitle; using Hud::fVer; using Hud::fSub; using Hud::fLbl; using Hud::fItem;
using Hud::fItemSub; using Hud::fChip; using Hud::fBadge;
// o interruptor do mockup (pilula 40x20, ciano ligado, punho #888 desligado)
static void Switch(Graphics* g, const RectF& r, bool on, bool en) {
    const BYTE a = en ? 255 : 110;
    const RectF tr(r.X + r.Width - 40.f, r.Y + (r.Height - 20.f) / 2.f, 40.f, 20.f);
    FillRound(g, tr, 10.f, on ? Color(a, 0, 229, 255) : Color(a, 51, 51, 51));
    SolidBrush b(on ? Color(a, 255, 255, 255) : Color(a, 136, 136, 136));
    g->FillEllipse(&b, on ? tr.X + 22.f : tr.X + 2.f, tr.Y + 2.f, 16.f, 16.f);
}

static const int  SH  = 20;     // margem da sombra (igual ao painel)
static const int  CW  = 860;    // largura do cartao: as listas de janela precisam de espaco (Rodada 27: 720 -> 860, a do mockup; o cabecalho ganhou o tamanho da interface)
static const int  PAD = 24;     // respiro interno
static const REAL TOPO_H = 64.f;   // a barra de titulo desenhada: e por ela que se arrasta (Rodada 27: 52 -> 64, o titulo de 26 do mockup)
static const int  kLinhasAlvo = 6; // linhas visiveis da lista do alvo
static const int  kLinhasModo = 5; // linhas visiveis da lista de modos (camera)
static const REAL kLinhaH = 32.f;  // altura de cada linha das listas
static const REAL kColRot = 210.f; // Rodada 27 (v9): a coluna do rotulo nas linhas "rotulo + chips" (como no mockup)
static const REAL kTipMaxW = 440.f;
static const UINT kTimerTip = 1;
static const UINT kTipMs = 250;    // o mouse para em cima e a dica aparece depois disto
// I3 (18/09/2026): com um jogo em tela cheia/sem bordas ja aberto, esta tela sobe POR CIMA
// dele (topmost + primeiro plano) e reafirma o topo de tempos em tempos - jogo sem bordas
// costuma ser topmost tambem. Sem jogo aberto, ela e uma janela comum, como sempre foi.
static const UINT kTimerTopo = 2;
static const UINT kTopoMs = 500;
// sec. 22: o "Reiniciar video do Windows" - 3 s depois do atalho, a sonda D3D12 num processo novo
static const UINT kTimerVideo = 3;
static std::wstring g_videoMsg;       // o resultado (ou o "feche o jogo antes") logo abaixo do botao
static bool g_videoTestando = false;

// sec. 23: EscalasDoCfg() e ListaTxt() (os passes/escalas que a sessao vai abrir, lidos do cfg
// do feed) SAIRAM em 24/09/2026 (item 8 do dono) - so a BarraDaTela() desta tela de escolha
// (removida junto) e o ComboDaTela() da barra usavam isto antes de a sessao comecar. A barra
// "Rede neural: X ms de Y ms" do menu End, dentro da sessao, ficou.

// cores so desta tela: o fundo das listas (mais escuro que o cartao, para "afundar") e
// o ambar do aviso de arquivos, no mesmo espirito da barra amarela do Windows mas em
// versao escura - chama atencao sem gritar "erro": nada esta quebrado, esta faltando.
static const Color C_LISTA (255,  26,  26,  26);   // #1A1A1A (v9)
static const Color C_AMB_BG(255,  52,  43,  24);   // ambar translucido sobre #1E1E1E (v9)
static const Color C_AMB_TX(255, 255, 196,  92);
static const Color C_TIP_BG(252,  30,  30,  30);

static REAL g_scale = 1.f;

// os tipos de area clicavel
enum Kind { K_NADA = 0, K_X, K_LANG, K_FONTE, K_ALVO, K_MODO, K_RECORTE, K_SW, K_GPU,
            K_BAIXAR, K_PASTA, K_ATUALIZAR, K_ATALHOS, K_INICIAR, K_INFO,
            // o que se monta ANTES de comecar (ver o bloco no Estado)
            K_JOGORES, K_RECONST, K_RITMO, K_CURTO, K_IAORDEM, K_NITIDEZ,
            // E1-E4 (16/09/2026): modo, exibicao do jogo e o upscaler do proprio jogo
            // I6 (18/09/2026): a chavinha "DLSS 5 antes do upscaling" (K_UPSTREAM, H1) virou o
            // chip Upstream do card Modo (K_MODOPROC com MODO_UPSTREAM)
            K_MODOPROC, K_EXIBICAO, K_DLSSQUAL, K_DLSSPRESET,
            // sec. 22: "Reiniciar video do Windows" (ultimo recurso depois de a placa resetar)
            K_VIDEO,
            // Rodada 27 (v9): o tamanho da interface (val = Hud::TamUi)
            K_TAMUI,
            K_AVANCADO };   // o titulo do bloco "Avancado" das opcoes (expande/recolhe)
// as chavinhas, na ordem em que aparecem
enum Sw { SW_SOBREPOR = 0, SW_TELACHEIA, SW_CURSOR, SW_ESCONDER, SW_VSYNC, SW_HDR, SW_FPS, SW_ANTICHEAT, SW_N };
// E2: a qualidade do upscaler do jogo no Nativo (as chaves sao as do card "DLSS do jogo")
struct QualJogo { const wchar_t* chave; const wchar_t* pt; const wchar_t* en; };
static const QualJogo kQualJogo[] = {
    { L"jogo",              L"Do jogo",             L"Game's own" },
    { L"dlaa",              L"DLAA 100%",           L"DLAA 100%" },
    { L"quality",           L"Qualidade 67%",       L"Quality 67%" },
    { L"balanced",          L"Equilibrado 58%",     L"Balanced 58%" },
    { L"performance",       L"Desempenho 50%",      L"Performance 50%" },
    { L"ultra-performance", L"Ultradesempenho 33%", L"Ultra performance 33%" },
};
static const int kNQualJogo = (int)(sizeof(kQualJogo) / sizeof(kQualJogo[0]));

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
// Rodada 27 (v9): as do mockup - titulo 26 negrito, titulo de secao 13 negrito, e a letra
// pequena do controle compacto de tamanho da interface.
static Font *fTit = nullptr, *fSec = nullptr, *fChipP = nullptr;
static REAL g_selFatorUsado = 1.f, g_selFatorMax = 1.6f;   // o tamanho da interface desta tela (ultima pintura)
static const UINT kTimerRepeteSel = 5;   // Rodada 27 (v10): segurar o "-"/"+" do tamanho repete
static It   g_repIt{ RectF(), 0, 0, false, std::wstring() };
static bool g_repAtivo = false;
static int  g_repVezes = 0;
static bool g_selAvancado = false;   // o bloco "Avancado" das opcoes: recolhido por padrao (Rodada 27)
static void EnsureFonts() {
    Hud::EnsureFonts();
    Hud::EnsureFonts8();
    if (!fTit) {
        fTit   = Hud::MkFont8(L"Segoe UI Variable Display", L"Segoe UI", 26.f, FontStyleBold);
        fSec   = Hud::MkFont8(L"Segoe UI Variable Text", L"Segoe UI", 13.f, FontStyleBold);
        fChipP = Hud::MkFont8(L"Segoe UI Variable Text Semibold", L"Segoe UI Semibold", 13.f, FontStyleRegular);
    }
    if (fLinha) return;
    fLinha = Hud::MkFont(L"Segoe UI",          16.f, FontStyleRegular);
    fTip   = Hud::MkFont(L"Segoe UI",          13.f, FontStyleRegular);
    fTipT  = Hud::MkFont(L"Segoe UI Semibold", 13.f, FontStyleRegular);
    fBotao = Hud::MkFont(L"Segoe UI Semibold", 16.f, FontStyleRegular);
}
static void SoltarFontes() {
    delete fLinha; delete fTip; delete fTipT; delete fBotao;
    fLinha = fTip = fTipT = fBotao = nullptr;
    delete fTit; delete fSec; delete fChipP;
    fTit = fSec = fChipP = nullptr;
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
    // MODO ANTI-CHEAT (C1): a chavinha SW_ANTICHEAT grava [ultima] anticheat na hora do clique;
    // quando a janela escolhida tem anti-cheat na pasta do exe, ela aparece ligada e esmaecida
    // (ligada sozinha, sem gravar). A varredura roda uma vez por janela escolhida.
    bool antiCheatDetectado = false; std::wstring antiCheatProduto; HWND antiCheatHwnd = nullptr;
    // E1-E4: medidos uma vez por janela escolhida (MedirNativo)
    HWND nativoHwnd = nullptr;
    bool podeNativo = false;           // 64 bits, D3D11/12 carregado, sem anti-cheat na pasta
    bool temUpscaler = false;          // DLSS, FSR 2+ ou XeSS na pasta ou carregado no jogo
    const wchar_t* gancho = nullptr;   // H3: "DLSS" | "FSR" | "XeSS" que a DLL engancha (ngx / sr_hook): o chip Upstream vale
    std::wstring ganchoOnde;           // H7: a pasta do Unreal onde a busca achou (log)
    int  upstreamFalhas = 0;           // I7: [perfil:<exe>] upstream_falhas; com 2 o chip Upstream some (o Automatico nem tenta)
    std::wstring apiJogo, perfilExe;   // "D3D12"/"D3D11"; exe em minusculas ([perfil:<exe>])
    int  modo = MODO_UNIVERSAL;             // a escolha: MODO_AUTO / MODO_UPSTREAM / MODO_NATIVO / MODO_UNIVERSAL
    std::wstring dlssQualidade = L"jogo", dlssPreset = L"jogo";
    int  exibicao = -1;                // exibicao do jogo lida agora: 0 janela, 1 sem bordas, -1 nao sei
    int  exibPedida = -1;              // o ultimo pedido (o jogo pode desfazer; nao insistimos)
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
// Tecla do painel conforme o modo (End; Insert no modulo AMD; ou a que a pessoa escolheu): "{P}" nos textos.
static std::wstring SubPainel(std::wstring s) {
    for (size_t p; (p = s.find(L"{P}")) != std::wstring::npos; ) s.replace(p, 3, g_hkPanel);
    return s;
}
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
    return SubPainel(TS(
        L"PLACA DE CAPTURA / CÂMERA / CONSOLE\n"
        L"Para jogar PS5, Xbox, Switch ou outro PC pela placa de captura — ou usar uma câmera. "
        L"Escolha o aparelho na lista e depois a resolução, o FPS e o formato que ele entrega.\n"
        L"Dica: 1920x1080 a 60 fps é o ponto ideal — a placa de vídeo faz 4 vezes menos conta que em 4K "
        L"e a imagem passa pelo DLSS 5 do mesmo jeito. Se a lista tiver YUY2 ou NV12, prefira: "
        L"são os formatos sem compressão, com menos atraso.\n"
        L"O som do console sai pelo programa também (chave no painel {P}).\n"
        L"Depois de abrir: ",
        L"CAPTURE CARD / CAMERA / CONSOLE\n"
        L"To play PS5, Xbox, Switch or another PC through a capture card — or to use a camera. "
        L"Pick the device in the list, then the resolution, FPS and format it delivers.\n"
        L"Tip: 1920x1080 at 60 fps is the sweet spot — the GPU does 4 times less work than at 4K "
        L"and the image still goes through DLSS 5 the same way. If the list has YUY2 or NV12, prefer them: "
        L"they are the uncompressed formats, with less delay.\n"
        L"The console's sound comes through the program too (switch in the {P} panel).\n"
        L"Once it opens: ") + RodapeAtalhos() + T(L", F11 tela cheia.", L", F11 for fullscreen."));
}
static const wchar_t* NomeSw(int i) {
    switch (i) {
    case SW_SOBREPOR:  return T(L"SOBREPOR",   L"OVERLAY");
    case SW_TELACHEIA: return T(L"Tela cheia", L"Fullscreen");
    case SW_CURSOR:    return T(L"Cursor",     L"Cursor");
    case SW_ESCONDER:  return T(L"Esconder",   L"Hide");
    case SW_VSYNC:     return L"V-Sync";
    case SW_HDR:       return L"HDR";
    case SW_FPS:       return T(L"FPS na tela", L"FPS on screen");
    default:           return L"Anti-cheat";   // E5: "Modo anti-cheat" cortava na grade
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
    case SW_ANTICHEAT: return TS(
        L"MODO ANTI-CHEAT\n"
        L"Recomendado para jogos online. Mantém apenas a captura de tela, sem qualquer interação com o jogo, como um "
        L"gravador de gameplay. Aumenta a chance de não ser detectado por sistemas anti-cheat, mas não garante que isso "
        L"não possa ocorrer.",
        L"ANTI-CHEAT MODE\n"
        L"Recommended for online games. Keeps only screen capture, with no interaction with the game, like a gameplay "
        L"recorder. Increases the likelihood of not being flagged by anti-cheat systems, but does not guarantee it.");
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
// MODO ANTI-CHEAT: a mesma varredura por arquivo da abertura (AntitrapacaNaPasta), no exe da
// janela escolhida. So le nomes de arquivo, e so quando a selecao muda de janela.
static void AntiCheatNoAlvo(Estado& s) {
    HWND h = (s.fonte == SRC_WINDOW && s.selWin >= 0 && s.selWin < (int)s.wins.size()) ? s.wins[s.selWin].hwnd : nullptr;
    if (h == s.antiCheatHwnd) return;
    s.antiCheatHwnd = h; s.antiCheatDetectado = false; s.antiCheatProduto.clear();
    if (!h) return;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    const std::wstring exe = pid ? CaminhoDoProcesso(pid) : L"";
    std::wstring evid;
    const std::wstring produto = exe.empty() ? L"" : AntitrapacaNaPasta(exe, &evid);
    if (!produto.empty()) {
        s.antiCheatDetectado = true; s.antiCheatProduto = produto;
        Log("seletor: anti-cheat na pasta da janela escolhida (%s, evidencia: %s) - o Modo anti-cheat aparece ligado", U8(produto).c_str(), U8(evid).c_str());
    }
}
// E1: valor da chave no perfil do jogo escolhido; sem perfil ou sem a chave, o [ultima]
// (soPerfil: vazio em vez do [ultima]). Ausente e vazio se separam pelo sentinela \x01,
// como no PerfilCarregar.
static std::wstring LerDoJogo(const Estado& s, const wchar_t* k, bool soPerfil = false) {
    if (!s.perfilExe.empty()) {
        wchar_t b[512];
        GetPrivateProfileStringW((L"perfil:" + s.perfilExe).c_str(), k, L"\x01", b, 512, IniPath().c_str());
        if (!(b[0] == L'\x01' && b[1] == 0)) return b;
    }
    return soPerfil ? std::wstring() : IniGet(k);
}
// As escolhas de ANTES DE COMECAR: ao abrir (sem jogo: [ultima]) e a cada janela escolhida
// (o perfil dela por cima). A fonte da verdade continua sendo o ini.
static void LerEscolhas(Estado& s) {
    s.jogoRes = _wtoi(LerDoJogo(s, L"jogo_resolucao").c_str());
    // kJogoRes vem de main.cpp - fonte unica, para os degraus nunca divergirem entre a
    // validacao aqui e os chips desenhados mais abaixo.
    bool ok = false;
    for (int i = 0; i < kNJogoRes; i++) if (s.jogoRes == kJogoRes[i]) ok = true;
    if (!ok) s.jogoRes = 0;

    std::wstring r = LerDoJogo(s, L"reconstrucao");
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
    // X-CONSOLE (18/09/2026): com o motor RenoDX a "Rapida" e INERTE (o add-on avalia sempre
    // depois de ampliar - main.cpp, "a ordem fica INERTE"). O chip marcado mostra o que VALE:
    // Maxima. Clicar em Rapida continua trocando o motor junto (K_IAORDEM), e ai passa a valer.
    // 2.1: "motor" vazio (instalacao limpa) conta como o padrao que o arranque vai usar - a
    // mesma regra (MotorProprioEfetivo) - senao a tela marcava "Maxima" e o app abria no proprio.
    s.iaOrdem = (IniGet(L"ia_ordem") == L"depois"
                 || !MotorProprioEfetivo(s.modo != MODO_NATIVO && s.modo != MODO_UPSTREAM)) ? 1 : 0;
    {
        const std::wstring vn = LerDoJogo(s, L"nitidez_ia");
        int k = vn.empty() ? 80 : _wtoi(vn.c_str());
        if (k < 0 || k > 100) k = 80;
        s.nitidez = k;
    }
    // E1: o modo vem SO do perfil do jogo; sem perfil, Automatico
    {
        std::wstring mp = LerDoJogo(s, L"modo_processamento", true);
        for (auto& c : mp) c = (wchar_t)towlower(c);
        // v2.0 (dono): sem Automatico - sem perfil, ou perfil com "auto", abre em Universal; Nativo/Upstream so a mao
        // 2.2.3: Nativo e Upstream foram removidos - perfil antigo com "nativo"/"upstream" abre em Universal
        s.modo = MODO_UNIVERSAL;
        // I7: quantas vezes o Upstream ja caiu neste jogo (so do perfil; sem perfil, zero)
        s.upstreamFalhas = _wtoi(LerDoJogo(s, L"upstream_falhas", true).c_str());
    }
    // E2: qualidade e preset do upscaler do jogo (as mesmas chaves do card; fora da lista = "jogo")
    {
        std::wstring p = LerDoJogo(s, L"dlss_preset"), q = LerDoJogo(s, L"dlss_qualidade");
        for (auto& c : p) c = (wchar_t)towupper(c);
        for (auto& c : q) c = (wchar_t)towlower(c);
        s.dlssPreset = L"jogo"; s.dlssQualidade = L"jogo";
        for (int i = 1; i < kNDlssPresets; i++) if (p == kDlssPresets[i]) s.dlssPreset = kDlssPresets[i];
        for (int i = 1; i < kNQualJogo; i++) if (q == kQualJogo[i].chave) s.dlssQualidade = kQualJogo[i].chave;
    }
}
// E1 (16/09/2026): A JANELA ESCOLHIDA PODE USAR O NATIVO? As condicoes do ligarNativo, lidas de
// fora e sem injetar nada: 64 bits, D3D11/12 entre os modulos carregados, sem anti-cheat na
// pasta. E o jogo tem upscaler proprio (DLSS, FSR 2+ ou XeSS) na pasta do exe ou carregado?
// So roda quando a janela escolhida muda. Na troca, as escolhas passam a ser as do perfil do
// jogo e o g_perfilExe aponta para ele: o IniSet desta tela espelha no [perfil:<exe>], senao o
// PerfilCarregar do arranque devolveria o valor antigo do perfil por cima do clique.
// H3: os upscalers que a DLL engancha - nvngx_dlss.dll (ngx_upstream) e os modulos da tabela do
// sr_hook.cpp (FSR 3.1 dx12/dx11, FSR do SDK 2.x, FSR 2 x64, XeSS). Em todos o modo Upstream (I6)
// decide se a rede roda antes do upscaling. Os outros nomes do NomeDeUpscaler (ex.: amd_fidelityfx_vk.dll)
// contam como upscaler do jogo, mas sem gancho o chip Upstream nao faria nada (some).
static const wchar_t* NomeDeGancho(std::wstring n) {
    for (auto& c : n) c = (wchar_t)towlower(c);
    if (n == L"nvngx_dlss.dll") return L"DLSS";
    if (n == L"amd_fidelityfx_dx12.dll" || n == L"amd_fidelityfx_dx11.dll" || n == L"ffx_fsr2_api_x64.dll") return L"FSR";
    // H8: FidelityFX SDK 2.x (ex.: RE requiem) - o loader e quem o jogo chama; o modulo do
    // upscaler aparece sozinho quando o jogo o carrega sem loader. Os dois estao no sr_hook.
    if (n == L"amd_fidelityfx_loader_dx12.dll" || n == L"amd_fidelityfx_upscaler_dx12.dll") return L"FSR";
    if (n == L"libxess.dll") return L"XeSS";
    return nullptr;
}
static bool NomeDeUpscaler(std::wstring n) {
    for (auto& c : n) c = (wchar_t)towlower(c);
    const bool dll = n.size() > 4 && n.compare(n.size() - 4, 4, L".dll") == 0;
    return n == L"nvngx_dlss.dll" || n == L"libxess.dll" ||
           (dll && (n.rfind(L"amd_fidelityfx_", 0) == 0 || n.rfind(L"ffx_fsr2_api", 0) == 0));
}
// I6 (18/09/2026): o gancho de um PROCESSO ja capturado (o painel End decide se o chip Upstream
// aparece): modulos carregados fora da pasta do app, e depois a pasta do exe. Mesma regra do
// MedirNativo, sem o resto da medicao. nullptr = nada que a DLL engancha.
static const wchar_t* GanchoDoProcesso(DWORD pid) {
    if (!pid) return nullptr;
    const wchar_t* gancho = nullptr;
    HANDLE snap = INVALID_HANDLE_VALUE;
    for (int t = 0; t < 3 && snap == INVALID_HANDLE_VALUE; t++) {
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (snap == INVALID_HANDLE_VALUE && GetLastError() != ERROR_BAD_LENGTH) break;
    }
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{}; me.dwSize = sizeof(me);
        for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
            if (_wcsnicmp(me.szExePath, g_exeDir.c_str(), g_exeDir.size()) == 0) continue;   // o nosso FG nao conta
            const wchar_t* gk = NomeDeGancho(me.szModule);
            if (gk && (!gancho || !wcscmp(gk, L"DLSS"))) gancho = gk;
        }
        CloseHandle(snap);
    }
    if (gancho) return gancho;
    const std::wstring exe = CaminhoDoProcesso(pid);
    const size_t barra = exe.find_last_of(L"\\/");
    if (barra == std::wstring::npos) return nullptr;
    WIN32_FIND_DATAW fd{};
    HANDLE f = FindFirstFileW((exe.substr(0, barra + 1) + L"*.dll").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return nullptr;
    do { if ((gancho = NomeDeGancho(fd.cFileName)) != nullptr) break; } while (FindNextFileW(f, &fd));
    FindClose(f);
    return gancho;
}
// H7: JOGO UNREAL guarda o upscaler fora da pasta do exe (<raiz>\<Projeto>\Binaries\Win64\x.exe e
// o DLL em <raiz>\Engine\Plugins\...\Binaries\ThirdParty\Win64 ou <raiz>\<Projeto>\Plugins\...).
// Busca so de leitura (nomes de arquivo), a partir da raiz do jogo, com teto de profundidade, de
// pastas e de tempo, numa thread: a resposta chega a janela por WM_SEL_UNREAL e so vale se a janela
// escolhida ainda for a mesma. Pula Content/Saved (paks e saves: grandes e sem DLL) e juncoes.
static const UINT kWmSelUnreal = WM_APP + 0x51;
struct AchadoUnreal {
    HWND alvo = nullptr;
    bool upscaler = false; const wchar_t* gancho = nullptr;
    std::wstring raiz, onde;
    unsigned pastas = 0; DWORD ms = 0; bool cortou = false;
};
// "" quando o exe nao esta em ...\Binaries\Win64 (ou WinGDK) ou a raiz nao tem a pasta Engine
static std::wstring RaizUnreal(const std::wstring& exe) {
    std::wstring low = exe;
    for (auto& c : low) c = (wchar_t)towlower(c);
    size_t p = low.rfind(L"\\binaries\\win64\\");
    if (p == std::wstring::npos) p = low.rfind(L"\\binaries\\wingdk\\");
    if (p == std::wstring::npos || p == 0) return L"";
    const size_t b = exe.find_last_of(L"\\/", p - 1);   // a barra antes do <Projeto>
    if (b == std::wstring::npos || b < 3) return L"";    // nunca a raiz do disco
    const std::wstring raiz = exe.substr(0, b + 1);
    const DWORD a = GetFileAttributesW((raiz + L"Engine").c_str());
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) ? raiz : L"";
}
static void BuscarUnreal(AchadoUnreal& a) {
    static const unsigned kMaxPastas = 4000, kMaxProf = 8;
    static const DWORD kMaxMs = 2000;
    const ULONGLONG t0 = GetTickCount64();
    std::vector<std::pair<std::wstring, unsigned>> pilha{ { a.raiz, 0u } };
    while (!pilha.empty()) {
        if (a.pastas >= kMaxPastas || GetTickCount64() - t0 > kMaxMs) { a.cortou = true; break; }
        const std::wstring dir = pilha.back().first; const unsigned prof = pilha.back().second;
        pilha.pop_back(); ++a.pastas;
        WIN32_FIND_DATAW fd{};
        HANDLE f = FindFirstFileExW((dir + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (f == INVALID_HANDLE_VALUE) continue;
        do {
            const wchar_t* n = fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!wcscmp(n, L".") || !wcscmp(n, L"..") || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
                if (!_wcsicmp(n, L"Content") || !_wcsicmp(n, L"Saved")) continue;
                if (prof + 1 < kMaxProf) pilha.push_back({ dir + n + L"\\", prof + 1 });
            } else if (NomeDeUpscaler(n)) {
                a.upscaler = true;
                if (const wchar_t* gk = NomeDeGancho(n)) { a.gancho = gk; a.onde = dir; break; }
            }
        } while (FindNextFileW(f, &fd));
        FindClose(f);
        if (a.gancho) break;
    }
    a.ms = (DWORD)(GetTickCount64() - t0);
}
// H7: o resultado da busca, na thread da tela. Dono do `a`. true = mudou o que a tela mostra.
static bool AplicarUnreal(Estado& s, AchadoUnreal* a) {
    const bool vale = (a->alvo == s.nativoHwnd);   // senao a pessoa ja escolheu outra janela
    if (vale) {
        const std::string achou = a->gancho ? "achou " + U8(a->gancho) + " em " + U8(a->onde)
                                : a->upscaler ? std::string("so upscaler sem gancho da DLL")
                                : std::string("nenhum (o upscaler pode estar dentro do exe)");
        Log("seletor: busca Unreal em %s - %u pastas, %lu ms%s: %s", U8(a->raiz).c_str(), a->pastas, (unsigned long)a->ms,
            a->cortou ? " (parou no teto)" : "", achou.c_str());
        if (a->upscaler) s.temUpscaler = true;
        if (a->gancho) { s.gancho = a->gancho; s.ganchoOnde = a->onde; }
    }
    const bool mudou = vale && a->upscaler;
    delete a;
    return mudou;
}
static void MedirNativo(Estado& s) {
    AntiCheatNoAlvo(s);
    HWND h = (s.fonte == SRC_WINDOW && s.selWin >= 0 && s.selWin < (int)s.wins.size()) ? s.wins[s.selWin].hwnd : nullptr;
    if (h == s.nativoHwnd) return;
    s.nativoHwnd = h; s.podeNativo = false; s.temUpscaler = false; s.gancho = nullptr; s.ganchoOnde.clear(); s.apiJogo.clear(); s.exibPedida = -1;
    s.perfilExe = h ? Desempenho::ExeDaJanela(h) : std::wstring();
    for (auto& c : s.perfilExe) c = (wchar_t)towlower(c);
    g_perfilExe = s.perfilExe;
    LerEscolhas(s);
    if (!h) return;
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    const bool b64 = pid && Injetor::Bits64(pid);
    bool d3d11 = false, d3d12 = false;
    if (b64) {
        HANDLE snap = INVALID_HANDLE_VALUE;
        for (int t = 0; t < 3 && snap == INVALID_HANDLE_VALUE; t++) {   // ERROR_BAD_LENGTH: o jogo carregava um modulo
            snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
            if (snap == INVALID_HANDLE_VALUE && GetLastError() != ERROR_BAD_LENGTH) break;
        }
        if (snap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W me{}; me.dwSize = sizeof(me);
            for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
                std::wstring n = me.szModule;
                for (auto& c : n) c = (wchar_t)towlower(c);
                if (n == L"d3d12.dll") d3d12 = true;
                else if (n == L"d3d11.dll") d3d11 = true;
                // o nosso amd_fidelityfx_dx12.dll (FG) mora na pasta do app: nao conta
                else if (NomeDeUpscaler(n) && _wcsnicmp(me.szExePath, g_exeDir.c_str(), g_exeDir.size()) != 0) {
                    s.temUpscaler = true;
                    const wchar_t* gk = NomeDeGancho(n);
                    if (gk && (!s.gancho || !wcscmp(gk, L"DLSS"))) s.gancho = gk;   // com os dois, o DLSS
                }
            }
            CloseHandle(snap);
        }
    }
    const std::wstring exe = pid ? CaminhoDoProcesso(pid) : L"";
    const size_t barra = exe.find_last_of(L"\\/");
    if (!s.gancho && barra != std::wstring::npos) {   // H1/H3: segue ate achar um upscaler com gancho, nao so o primeiro
        WIN32_FIND_DATAW fd{};
        HANDLE f = FindFirstFileW((exe.substr(0, barra + 1) + L"*.dll").c_str(), &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                if (NomeDeUpscaler(fd.cFileName)) s.temUpscaler = true;
                if ((s.gancho = NomeDeGancho(fd.cFileName)) != nullptr) break;
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    s.apiJogo = d3d12 ? L"D3D12" : d3d11 ? L"D3D11" : L"";
    s.podeNativo = b64 && (d3d11 || d3d12) && !s.antiCheatDetectado;
    Log("seletor: janela escolhida %s - 64 bits=%d api=%s anti-cheat=%d upscaler do jogo=%d gancho=%s -> Nativo %s (modo do perfil: %s, upstream_falhas=%d)",
        U8(s.perfilExe).c_str(), (int)b64, s.apiJogo.empty() ? "?" : U8(s.apiJogo).c_str(), (int)s.antiCheatDetectado,
        (int)s.temUpscaler, s.gancho ? U8(s.gancho).c_str() : "nenhum", s.podeNativo ? "disponivel" : "indisponivel", NomeModoProc(s.modo), s.upstreamFalhas);
    // H7: Unreal sem gancho carregado nem na pasta do exe: a busca na raiz do jogo, fora da thread da tela
    const std::wstring raiz = (!s.gancho && s.podeNativo) ? RaizUnreal(exe) : std::wstring();
    if (!raiz.empty()) {
        auto* a = new AchadoUnreal; a->alvo = h; a->raiz = raiz;
        if (s.wnd) {
            Log("seletor: jogo Unreal - procurando DLSS/FSR/XeSS a partir de %s (em segundo plano)", U8(raiz).c_str());
            const HWND wnd = s.wnd;
            std::thread([wnd, a] {
                BuscarUnreal(*a);
                if (!PostMessageW(wnd, kWmSelUnreal, 0, (LPARAM)a)) delete a;   // a tela ja fechou
            }).detach();
        } else {   // sem janela (nao acontece na tela de verdade): na hora
            BuscarUnreal(*a);
            AplicarUnreal(s, a);
        }
    }
}
// a escolha de modo so aparece para janela que pode usar o Nativo; Modo anti-cheat ligado forca o Universal
// 2.2.3: o modo Nativo (injecao no jogo) foi removido - nao ha mais escolha de modo: sempre Universal.
static bool EscolhaDeModo(const Estado& s) { (void)s; return false; }
// true = a tela mostra os campos do Nativo. Automatico: com upscaler no jogo; sem, os do Universal.
// I6: o Upstream e o Nativo com a rede antes do upscaling - mesmos campos.
static bool CamposNativo(const Estado& s) {
    if (!EscolhaDeModo(s)) return false;
    return s.modo == MODO_NATIVO || s.modo == MODO_UPSTREAM || (s.modo == MODO_AUTO && s.temUpscaler);
}
// I6/I7: o chip Upstream aparece so onde vale - jogo com DLSS/FSR/XeSS que a DLL engancha e
// que nao caiu duas vezes ali (regra do que nao funciona nao aparece). Escolhido a mao, fica:
// e o jeito de forcar de novo depois de um patch do jogo.
static bool ChipUpstream(const Estado& s) {
    if (s.modo == MODO_UPSTREAM) return true;
    return s.gancho != nullptr && s.upstreamFalhas < 2;
}
// Automatico sem upscaler no jogo: o upscaling escolhido so acontece pela pelicula (Universal).
// "Amplia" = a reconstrucao passa da altura em que o jogo vai desenhar.
static bool AmpliaPelaPelicula(const Estado& s) {
    if (s.reconstrucao == 0 || s.selWin < 0 || s.selWin >= (int)s.wins.size()) return false;
    const WinTarget& w = s.wins[s.selWin];
    const int hJogo = s.jogoRes > 0 ? s.jogoRes : w.h;
    int hAlvo = s.reconstrucao;
    if (hAlvo < 0) {
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        HMONITOR m = w.hwnd ? MonitorFromWindow(w.hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
        hAlvo = (m && GetMonitorInfoW(m, &mi)) ? (int)(mi.rcMonitor.bottom - mi.rcMonitor.top) : 0;
    }
    return hAlvo > hJogo;
}
// 2.1: o monitor da fonte escolhida (a tela ou a tela onde esta a janela) fica em OUTRA placa que nao a `p`? Entao cada
// quadro atravessa entre as duas (travessias por quadro = 2) e pode ficar lento: o seletor avisa ao lado da placa.
// Cache por HMONITOR: o DXGI nao e coisa para enumerar a cada Paint. Camera/captura: sem monitor, sem aviso.
static bool MonitorEmOutraPlacaQue(const Estado& s, const PlacaOpcao& p) {
    HMONITOR m = nullptr;
    if (s.fonte == SRC_MONITOR && s.selMon >= 0 && s.selMon < (int)s.mons.size()) m = s.mons[s.selMon].mon;
    else if (s.fonte == SRC_WINDOW && s.selWin >= 0 && s.selWin < (int)s.wins.size() && s.wins[s.selWin].hwnd)
        m = MonitorFromWindow(s.wins[s.selWin].hwnd, MONITOR_DEFAULTTONEAREST);
    if (!m) return false;
    static HMONITOR cm = nullptr; static LUID cl{}; static bool cok = false;
    if (m != cm) { cm = m; cok = PonteGpu::PlacaDoMonitor(m, &cl, nullptr); }
    return cok && !(cl.LowPart == p.luid.LowPart && cl.HighPart == p.luid.HighPart);
}

// E4: a exibicao do jogo, lida de fora pelo estilo e pelo retangulo (JogoRes::Estilo).
// Tela cheia exclusiva nao se distingue de sem bordas daqui: as duas marcam "sem bordas".
static int ExibicaoAtual(HWND h) {
    const int e = JogoRes::Estilo(h);
    return e == 0 ? 0 : (e == 1 || e == 2) ? 1 : -1;
}
// E4: Janela (WS_OVERLAPPEDWINDOW) ou Janela sem bordas (WS_POPUP), SEMPRE com tamanho e posicao
// dentro do monitor em que o jogo esta (o RE2 invadiu o ultrawide ao sair do sem bordas).
// Uma tentativa por clique: se o jogo desfizer, a tela mostra o modo dele e a dica explica.
static bool AplicarExibicao(HWND h, int modo) {
    if (!h || !IsWindow(h)) return false;
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    HMONITOR m = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    RECT rc{};
    if (!m || !GetMonitorInfoW(m, &mi) || !GetClientRect(h, &rc)) return false;
    DWORD st = (DWORD)GetWindowLongPtrW(h, GWL_STYLE);
    const DWORD ex = (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE);
    RECT alvo{};
    if (modo == 1) {
        st = (st & ~(DWORD)WS_OVERLAPPEDWINDOW) | (DWORD)WS_POPUP;
        alvo = mi.rcMonitor;
    } else {
        st = (st & ~(DWORD)WS_POPUP) | (DWORD)WS_OVERLAPPEDWINDOW;
        const RECT& wa = mi.rcWork;
        const LONG mw = wa.right - wa.left, mh = wa.bottom - wa.top;
        LONG cw = rc.right - rc.left, chh = rc.bottom - rc.top;
        if (cw < 640 || chh < 360) { cw = mw * 3 / 4; chh = mh * 3 / 4; }
        RECT fr{ 0, 0, cw, chh };
        AdjustWindowRectEx(&fr, st, FALSE, ex);
        const LONG margemW = (fr.right - fr.left) - cw, margemH = (fr.bottom - fr.top) - chh;
        if (cw + margemW > mw || chh + margemH > mh) {
            const double kx = (double)(mw - margemW) / (double)cw, ky = (double)(mh - margemH) / (double)chh;
            const double k = kx < ky ? kx : ky;
            cw = (LONG)(cw * k) & ~1L; chh = (LONG)(chh * k) & ~1L;
        }
        const LONG fw = cw + margemW, fh = chh + margemH;
        alvo.left = wa.left + (mw - fw) / 2; alvo.top = wa.top + (mh - fh) / 2;
        alvo.right = alvo.left + fw; alvo.bottom = alvo.top + fh;
    }
    SetLastError(0);
    if (!SetWindowLongW(h, GWL_STYLE, (LONG)st) && GetLastError() != 0) {
        Log("seletor: exibicao do jogo -> %s RECUSADA (SetWindowLong erro %lu)", modo == 1 ? "sem bordas" : "janela", (unsigned long)GetLastError());
        return false;
    }
    const BOOL ok = SetWindowPos(h, nullptr, alvo.left, alvo.top, alvo.right - alvo.left, alvo.bottom - alvo.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    Log("seletor: exibicao do jogo -> %s: janela %ldx%ld em %ld,%ld (monitor %ldx%ld em %ld,%ld) ok=%d",
        modo == 1 ? "sem bordas" : "janela", (long)(alvo.right - alvo.left), (long)(alvo.bottom - alvo.top),
        (long)alvo.left, (long)alvo.top, (long)(mi.rcMonitor.right - mi.rcMonitor.left),
        (long)(mi.rcMonitor.bottom - mi.rcMonitor.top), (long)mi.rcMonitor.left, (long)mi.rcMonitor.top, (int)ok);
    return ok != FALSE;
}
// Ha uma janela de OUTRO processo cobrindo um monitor inteiro (jogo em tela cheia ou sem
// bordas)? E o caso em que a pessoa abriu o app com o jogo ja rodando e, sem isto, teria
// de dar Alt+Tab para achar a tela de escolha.
static HWND JogoEmTelaCheia(const Estado& s) {
    // a lista de janelas so e carregada com a fonte Janela; aqui vale para qualquer fonte
    const std::vector<WinTarget> wins = (s.fonte == SRC_WINDOW) ? s.wins : EnumCaptureWindows(s.wnd);
    for (const WinTarget& w : wins) {
        // Rodada 22 (24/09/2026): JanelaPodeSerJogo (main.cpp) e o criterio unico e
        // compartilhado (cloaked, WS_EX_TOOLWINDOW/TRANSPARENT/NOACTIVATE, layered
        // transparente, sem titulo, processo do shell) - antes esta funcao so conferia
        // visivel/minimizada/pid, e um canvas do sistema (Shell Handwriting Canvas,
        // TextInputHost.exe) passava por aqui como se fosse um jogo.
        if (!JanelaPodeSerJogo(w.hwnd)) continue;
        RECT r{}; if (!GetWindowRect(w.hwnd, &r)) continue;
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(MonitorFromWindow(w.hwnd, MONITOR_DEFAULTTONEAREST), &mi)) continue;
        if (r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top &&
            r.right >= mi.rcMonitor.right && r.bottom >= mi.rcMonitor.bottom) return w.hwnd;
    }
    return nullptr;
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
        Log("seletor: aviso do DLSS 5 %s (addon=%d dlssnr=%d dlss=%d reshade=%d sm-ruim=%d mod-amd faltando=%u diag=%d)",
            f.AlgumaCoisa() ? "mostrado" : "escondido", (int)f.addon, (int)f.dll, (int)f.dll2,
            (int)f.reshade, (int)f.smRuim, (unsigned)f.mod.size(), Diag::Atual().regra);
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
    if (sel)      FillRound(g, r, 4.f, C_ACC);
    else if (hot) FillRound(g, r, 4.f, C_ROWH);
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
    FillRound(g, r, 4.f, sel ? C_ACC : (hot && en) ? C_ROWH : C_ROW);
    Txt(g, t, fChip, sel ? C_DARK : en ? C_BODY : C_FAINT, r, 1, true);
}

static int Render(Graphics* g) {
    EnsureFonts();
    g_it.clear();
    Estado& s = *g_s;
    const REAL L = (REAL)(SH + PAD), IW = (REAL)(CW - PAD * 2), R = L + IW;
    const REAL top = (REAL)SH;
    REAL cy = top;

    // -------- barra de titulo (desenhada): marca, titulo, tamanho da interface, idioma e o X
    // Rodada 27 (v9): o cabecalho do mockup - capivara + "DLSS5ForAll" (26, negrito) e, a direita,
    // "Aa − 100% + Auto", os dois idiomas e o X. O subtitulo "— escolha a fonte" so entra se sobrar
    // espaco entre o titulo e os controles.
    {
        const REAL yc = cy + TOPO_H / 2.f;   // o centro vertical da barra
        const REAL marca = 40.f;
        if (g) Hud::DesenharLogo8(g, L, yc - marca / 2.f, marca);
        const REAL xT = L + marca + 12.f;
        const REAL wT = LarguraTexto(L"DLSS5ForAll", fTit);
        if (g) Txt(g, L"DLSS5ForAll", fTit, C_TXT, RectF(xT, yc - 18.f, wT + 4.f, 36.f), 0);
        REAL x = xT + wT + 6.f;
        const REAL wV = LarguraTexto(APP_VERSION, fVer) + 4.f;
        if (g) Txt(g, APP_VERSION, fVer, C_DIM, RectF(x, yc - 3.f, wV, 16.f), 0);
        x += wV + 10.f;
        const REAL xSub = x;
        // o X (30x30, contorno, como o do mockup)
        RectF rx(R - 30.f, yc - 15.f, 30.f, 30.f);
        int idx = Add(rx, K_X, 0, true, TS(L"Fechar\nFecha sem abrir nada (Esc).", L"Close\nCloses without opening anything (Esc)."));
        if (g) {
            if (g_hot == idx) FillRound(g, rx, 4.f, C_ROWH);
            StrokeRound(g, RectF(rx.X + .5f, rx.Y + .5f, rx.Width - 1.f, rx.Height - 1.f), 4.f, C_EDGE, 1.f);
            Txt(g, L"✕", fChip, C_DIM, rx, 1);
        }
        // idioma: dois chips, cada um na propria lingua
        const std::wstring tipL = TS(L"Idioma / Language\nA tela reabre no idioma escolhido.", L"Language / Idioma\nThe screen reopens in the chosen language.");
        REAL cx = rx.X - 12.f;
        const Lingua ls[2] = { LINGUA_EN, LINGUA_PT };
        for (Lingua l : ls) {
            const REAL w = LarguraTexto(NomeLingua(l), fChip) + 22;
            RectF rc(cx - w, yc - 15.f, w, 30.f);
            const bool sel = (g_lingua == l);
            int i2 = Add(rc, K_LANG, (int)l, true, tipL);
            if (g) Chip(g, rc, NomeLingua(l), sel, g_hot == i2);
            cx = rc.X - 6;
        }
        // Rodada 27 (v9): TAMANHO DA INTERFACE, compacto - "Aa − 100% + Auto". O mesmo
        // [ultima] tamanho_ui do painel (Geral > Exibicao). − e + saem do Auto; o + para no
        // teto desta tela (a janela nunca passa da area util do monitor). Vale na hora.
        {
            cx -= 10.f;
            const int p = Hud::TamanhoUi();
            const int pUsado = (int)(g_selFatorUsado * 100.f + 0.5f), pTeto = (int)(g_selFatorMax * 100.f + 0.5f);
            const bool noTeto = pUsado >= pTeto || pUsado >= Hud::kTamUiMax, noPiso = pUsado <= Hud::kTamUiMin;
            const std::wstring base = TS(L"Tamanho da interface\nAumenta ou diminui juntos a letra e as janelas do app (esta tela, o painel, Filtros e Ajustes da rede neural), de 70% a 160%. "
                                         L"Nenhuma janela passa da área útil do monitor. Auto escolhe o maior que cabe (até 100% num 1080p).",
                                         L"Interface size\nScales the app's text and windows together (this screen, the panel, Filters and Neural network settings), from 70% to 160%. "
                                         L"No window ever grows beyond the monitor's usable area. Auto picks the largest size that fits (up to 100% on a 1080p screen).");
            const REAL hb = 26.f, yb = yc - hb / 2.f;
            // Auto
            const REAL wA = LarguraTexto(L"Auto", fChipP) + 18.f;
            RectF ra(cx - wA, yb, wA, hb);
            int iA = Add(ra, K_TAMUI, 0, true, TS(L"Auto\nO maior tamanho que cabe nesta tela, sem passar de 100% num 1080p. Apertar − ou + sai do Auto.",
                                                  L"Auto\nThe largest size that fits this screen, capped at 100% on a 1080p display. Pressing − or + turns Auto off."));
            if (g) {
                if (p == 0) FillRound(g, ra, 4.f, g_hot == iA ? C_ACC2 : C_ACC);
                else { if (g_hot == iA) FillRound(g, ra, 4.f, C_ROWH); StrokeRound(g, RectF(ra.X + .5f, ra.Y + .5f, ra.Width - 1.f, ra.Height - 1.f), 4.f, C_EDGE, 1.f); }
                Txt(g, L"Auto", fChipP, p == 0 ? C_DARK : C_BODY, ra, 1);
            }
            cx = ra.X - 6.f;
            auto passo = [&](const wchar_t* t, int val, bool en, const std::wstring& tip) {
                RectF r(cx - hb, yb, hb, hb);
                int i = Add(r, K_TAMUI, val, en, tip);
                if (g) {
                    if (g_hot == i && en) FillRound(g, r, 4.f, C_ROWH);
                    StrokeRound(g, RectF(r.X + .5f, r.Y + .5f, r.Width - 1.f, r.Height - 1.f), 4.f, C_EDGE, 1.f);
                    Txt(g, t, fChip, en ? C_TXT : Color(110, 160, 160, 160), r, 1);
                }
                cx = r.X - 4.f;
            };
            passo(L"+", 2, !noTeto, noTeto && pUsado < Hud::kTamUiMax
                ? TS(L"Máximo para esta tela\nMaior que isto, a janela passaria da área útil do monitor.",
                     L"Maximum for this screen\nAny larger and the window would extend past the monitor's usable area.")
                : TS(L"Aumentar\n5% maior (até o máximo que cabe nesta tela).", L"Increase\n5% larger (up to the maximum that fits this screen)."));
            const REAL wVal = LarguraTexto(L"160%", fChipP) + 8.f;
            RectF rv(cx - wVal, yb, wVal, hb);
            Add(rv, K_NADA, 0, true, base);
            if (g) { wchar_t v[16]; _snwprintf(v, 16, L"%d%%", pUsado); v[15] = 0; Txt(g, v, fChipP, C_TXT, rv, 1); }
            cx = rv.X - 4.f;
            passo(L"−", 1, !noPiso, TS(L"Diminuir\n5% menor (mínimo 70%).", L"Decrease\n5% smaller (minimum 70%)."));
            const REAL wAa = LarguraTexto(L"Aa", fChip) + 6.f;
            RectF raa(cx - wAa, yb, wAa, hb);
            Add(raa, K_NADA, 0, true, base);
            if (g) Txt(g, L"Aa", fChip, C_DIM, raa, 1);
            cx = raa.X - 8.f;
        }
        if (g && cx - xSub > 60.f) Txt(g, T(L"— escolha a fonte", L"— pick the source"), fSub, C_DIM, RectF(xSub, yc - 11.f, cx - xSub, 22.f), 0, true);
        cy += TOPO_H;
        if (g) { Pen p(C_SEP, 1.f); g->DrawLine(&p, (REAL)SH, cy, (REAL)(SH + CW), cy); }
        cy += 16;
    }

    // -------- FONTE: tres cartoes, um selecionado
    {
        if (g) Txt(g, T(L"FONTE", L"SOURCE"), fSec, C_DIM, RectF(L, cy, IW, 16), 0);
        cy += 22;
        const REAL gap = 10, bw = (IW - gap * 2) / 3, bh = 64;
        const SourceKind ks[3] = { SRC_WINDOW, SRC_MONITOR, SRC_CAMERA };
        // J8: quem joga console pela placa procura "placa de captura", nao "camera" (cartao de
        // 217 px: o nome inteiro "Placa de captura / Camera / Console" vai em duas linhas)
        const wchar_t* nome[3] = { T(L"Janela", L"Window"), L"Monitor", T(L"Placa de captura", L"Capture card") };
        const wchar_t* sub[3]  = { T(L"recomendado · mais leve", L"recommended · lighter"),
                                   T(L"a tela inteira", L"the whole screen"),
                                   T(L"Câmera / Console", L"Camera / Console") };
        for (int i = 0; i < 3; i++) {
            RectF r(L + i * (bw + gap), cy, bw, bh);
            const bool sel = (s.fonte == ks[i]);
            int idx = Add(r, K_FONTE, (int)ks[i], true, TipFonte(ks[i]));
            if (g) {
                FillRound(g, r, 6.f, (g_hot == idx && !sel) ? C_ROWH : C_ROW);
                if (sel) { StrokeRound(g, r, 6.f, C_ACC, 1.f); FillRound(g, RectF(r.X, r.Y + 12, 4.f, r.Height - 24), 2.f, C_ON); }
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
                                                    : T(L"QUAL PLACA, CÂMERA OU CONSOLE", L"WHICH CARD, CAMERA OR CONSOLE");
        if (g) {
            Txt(g, lbl, fSec, C_DIM, RectF(L, cy, IW, 16), 0);
            wchar_t c[64]; _snwprintf(c, 64, T(L"%d encontrada%s", L"%d found"), n, n == 1 ? L"" : L"s");
            if (n > 0) Txt(g, c, fSec, C_DIM, RectF(L, cy, IW, 16), 2);
        }
        cy += 22;
        const int vis = n < kLinhasAlvo ? n : kLinhasAlvo;
        int& sel = SelAlvo(s);
        if (n > 0 && s.rolAlvo > n - vis) s.rolAlvo = n - vis;
        if (s.rolAlvo < 0) s.rolAlvo = 0;
        const REAL boxH = (n == 0) ? (s.fonte == SRC_CAMERA ? 92.f : 56.f) : vis * kLinhaH + 8;
        RectF box(L, cy, IW, boxH);
        g_rcListaAlvo = box;
        if (g) { FillRound(g, box, 8.f, C_LISTA); StrokeRound(g, box, 8.f, C_EDGE, 1.f); }
        if (n == 0) {
            if (g) {
                const wchar_t* vazio = s.fonte == SRC_WINDOW
                    ? T(L"Nenhuma janela encontrada. Abra o jogo e clique em \"Atualizar lista\".",
                        L"No window found. Open the game and click \"Refresh list\".")
                    : s.fonte == SRC_MONITOR
                    ? T(L"Nenhum monitor encontrado. Clique em \"Atualizar lista\".", L"No monitor found. Click \"Refresh list\".")
                    : T(L"Nenhuma placa de captura ou câmera foi encontrada neste PC.\n"
                        L"Se o console está ligado na placa agora, confira o cabo USB e clique em \"Atualizar lista\".\n"
                        L"Para um jogo que roda neste PC, escolha \"Janela\" ali em cima.",
                        L"No capture card or camera was found on this PC.\n"
                        L"If the console is plugged into the card right now, check the USB cable and click \"Refresh list\".\n"
                        L"For a game running on this PC, pick \"Window\" up there.");
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

    // -------- E1: o modo, so quando a janela escolhida pode usar o Nativo
    MedirNativo(s);   // barato: so mede de novo quando a janela escolhida muda
    const bool nativo = CamposNativo(s);
    if (EscolhaDeModo(s)) {
        if (g) Txt(g, T(L"Modo", L"Mode"), fItemSub, C_FAINT, RectF(L, cy, 120, 30), 0);
        // I6 (18/09/2026): quatro chips, na ordem do mais leve ao mais compativel. O indice e o
        // MODO_* (o valor do clique); a ordem na tela e a de `ordem`. O Upstream some onde nao
        // vale (ChipUpstream); o Automatico escolhe entre os tres.
        const bool upstreamAqui = ChipUpstream(s);
        const wchar_t* nomes[4] = { T(L"Automático", L"Auto"), T(L"Nativo", L"Native"), L"Universal", L"Upstream" };
        // os textos aprovados do card "Modo" do painel (C1/I6), mais o que vale neste jogo
        std::wstring tips[4] = {
            TS(L"AUTOMÁTICO\nUsa o melhor modo que funcionar neste jogo: Upstream, senão Nativo, senão Universal.",
               L"AUTO\nUses the best mode that works in this game: Upstream, else Native, else Universal.") +
            (upstreamAqui && s.gancho ? TS(L"\nNeste jogo: Upstream, com o upscaling do próprio jogo ligado.",
                                           L"\nIn this game: Upstream, with the game's own upscaling on.")
             : s.temUpscaler ? TS(L"\nNeste jogo: Nativo, com o upscaling do próprio jogo.",
                                  L"\nIn this game: Native, with the game's own upscaling.")
                             : TS(L"\nNeste jogo: Nativo sem upscaling; com upscaling, Universal.",
                                  L"\nIn this game: Native without upscaling; with upscaling, Universal.")),
            TS(L"NATIVO\nIntegra-se ao jogo e aplica o DLSS 5 na imagem final, direto na placa de vídeo. "
               L"Imagem melhor, um pouco mais pesado. Funciona na maioria dos jogos de 64 bits em DirectX 11 e 12, sem anti-cheat.",
               L"NATIVE\nIntegrates with the game and applies DLSS 5 to the final image, right on the graphics card. "
               L"Better image, a bit heavier. Works in most 64-bit games on DirectX 11 and 12, without anti-cheat."),
            TS(L"UNIVERSAL\nFunciona com qualquer coisa na tela. Não interage com o jogo: "
               L"captura a imagem por fora e aplica o DLSS 5 sobre ela, como um filtro. Indicado para jogos com anti-cheat.",
               L"UNIVERSAL\nWorks with anything on screen. Does not interact with the game: "
               L"captures the image from outside and applies DLSS 5 on top of it, like a filter. Recommended for games with anti-cheat."),
            TS(L"UPSTREAM\nAplica o DLSS 5 antes de o jogo aumentar a resolução: mais leve para a placa e sem mexer na interface do jogo. "
               L"Só em jogos com DLSS, FSR ou XeSS, e precisa do upscaling ligado nas opções de imagem do jogo. Mais chance de incompatibilidade.\n"
               L"Se o jogo desligar o upscaling, o DLSS 5 segue na imagem final e volta sozinho quando religar.",
               L"UPSTREAM\nApplies DLSS 5 before the game raises the resolution: lighter on the graphics card and leaves the game's interface untouched. "
               L"Only in games with DLSS, FSR or XeSS, and needs upscaling turned on in the game's display options. Higher chance of incompatibility.\n"
               L"If the game turns upscaling off, DLSS 5 stays on the final image and comes back by itself when it is turned on again.") };
        if (s.upstreamFalhas >= 2)
            tips[MODO_UPSTREAM] += TS(L"\nEste jogo não aceitou o DLSS 5 Upstream duas vezes. Escolher aqui tenta de novo.",
                                                                 L"\nThis game did not accept DLSS 5 Upstream twice. Choosing it here tries again.");
        static const int ordem[3] = { MODO_UPSTREAM, MODO_NATIVO, MODO_UNIVERSAL };   // v2.0: sem Automatico
        REAL x = L + 70, cyChips = cy;
        for (int k = 0; k < 3; k++) {
            const int i = ordem[k];
            if (i == MODO_UPSTREAM && !upstreamAqui) continue;
            const REAL w = LarguraTexto(nomes[i], fChip) + 28;
            ProxLinhaDeChip(x, cyChips, w, L + 70, IW - 70);
            RectF r(x, cyChips, w, 30);
            int idx = Add(r, K_MODOPROC, i, true, tips[i]);
            if (g) Chip(g, r, nomes[i], s.modo == i, g_hot == idx);
            x += w + 8;
        }
        cy = cyChips + 30 + 8;
    }

    // -------- E4: no Nativo, a exibicao do jogo no lugar do recorte
    if (nativo) {
        s.exibicao = ExibicaoAtual(s.wins[s.selWin].hwnd);   // CamposNativo garante a janela
        if (g) Txt(g, T(L"Exibição", L"Display"), fItemSub, C_FAINT, RectF(L, cy, 120, 30), 0);
        const wchar_t* nomes[2] = { T(L"Janela", L"Windowed"), T(L"Janela sem bordas", L"Borderless") };
        std::wstring tips[2] = {
            TS(L"JANELA\nO jogo numa janela com bordas, dentro do monitor em que ele está.",
               L"WINDOWED\nThe game in a bordered window, inside the monitor it is on."),
            TS(L"JANELA SEM BORDAS\nO jogo ocupa o monitor em que ele está, sem bordas.",
               L"BORDERLESS\nThe game fills the monitor it is on, without borders.") };
        for (auto& t : tips) {
            t += TS(L"\nTela cheia: use a opção do próprio jogo.", L"\nFullscreen: use the game's own option.");
            if (s.exibPedida >= 0 && s.exibicao != s.exibPedida)
                t += TS(L"\nO jogo voltou ao modo dele: mude pelo menu do jogo.",
                        L"\nThe game went back to its own mode: change it in the game's menu.");
        }
        REAL x = L + 70, cyChips = cy;
        for (int i = 0; i < 2; i++) {
            const REAL w = LarguraTexto(nomes[i], fChip) + 28;
            ProxLinhaDeChip(x, cyChips, w, L + 70, IW - 70);
            RectF r(x, cyChips, w, 30);
            int idx = Add(r, K_EXIBICAO, i, true, tips[i]);
            if (g) Chip(g, r, nomes[i], s.exibicao == i, g_hot == idx);
            x += w + 8;
        }
        cy = cyChips + 30 + 12;
    }

    // -------- janela: o recorte, dois chips numa linha so (E3: nao no Nativo)
    if (s.fonte == SRC_WINDOW && !nativo) {
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
            TxtWrap(g, SubPainel(T(L"o jogo fica na janela dele e a película cobre a tela inteira — o teclado e o mouse "
                     L"continuam indo para o jogo. No painel ({P}) dá para escolher até onde a rede reconstrói "
                     L"e mudar a resolução da janela do jogo.",
                     L"the game stays in its window and the layer covers the whole screen — keyboard and mouse "
                     L"still go to the game. In the panel ({P}) you can choose how far the network rebuilds and "
                     L"change the game window's resolution.")).c_str(),
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
        if (g) Txt(g, T(L"ANTES DE COMEÇAR", L"BEFORE YOU START"), fSec, C_DIM, RectF(L, cy, IW, 16), 0);
        cy += 22;

        // ---- resolucao do jogo (so com fonte JANELA: e a janela dele que se redimensiona;
        // E2: nao no Nativo - a janela do jogo nao e mexida com a ponte, o upscaler do jogo manda)
        if (s.fonte == SRC_WINDOW && !nativo) {
            // Rodada 27 (v9): rotulo e chips na MESMA linha, como no mockup (o "o jogo desenha nesta
            // resolucao (menos = mais FPS)" ja esta na dica de cada chip).
            if (g) Txt(g, T(L"Resolução de renderização", L"Render resolution"), fLinha, C_BODY, RectF(L, cy, kColRot - 8, 30), 0, true);
            // kJogoRes/kNJogoRes vem de main.cpp (fonte unica - 2160/4K acrescentado
            // 14/09/2026, pedido do usuario: quem tem monitor 4K pode querer o jogo nativo
            // em 4K e usar o programa so pela rede neural, sem ampliar nada).
            REAL x = L + kColRot, cyChips = cy;
            for (int i = 0; i < kNJogoRes; i++) {
                wchar_t b[16];
                if (kJogoRes[i] == 0) wcscpy(b, L"Original"); else _snwprintf(b, 16, L"%dp", kJogoRes[i]);   // E5: "Nativo" conflitava com o modo
                b[15] = 0;
                const REAL w = LarguraTexto(b, fChip) + 28;
                ProxLinhaDeChip(x, cyChips, w, L + kColRot, IW - kColRot);
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
        if (!nativo) {   // E2: no Nativo o upscaling e o do proprio jogo (bloco logo abaixo)
        // Rodada 27 (v9): rotulo (com a linha de explicacao embaixo) na coluna da esquerda e os
        // chips na da direita, como no mockup - a linha so do rotulo saiu.
        if (g) {
            Txt(g, L"Upscaling", fLinha, C_BODY, RectF(L, cy, kColRot - 8, 30), 0, true);
            TxtWrap(g, T(L"até onde a IA amplia a imagem", L"how far the AI upscales the image"), fItemSub, C_DIM, RectF(L, cy + 28, kColRot - 16, 36));
        }
        const REAL cyUpsIni = cy;
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
                RectF r(L + kColRot, cy, w, 30);
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
                if (g) Txt(g, sub, fItemSub, C_FAINT, RectF(L + kColRot, cy, IW - kColRot, 16), 0, true);
                cy += 18;
            }
            cy += 4;
            // OS DEGRAUS FIXOS (Sem upscaling, 1080..4320p): fileira separada, quebra
            // sozinha se nao couber numa linha so (ProxLinhaDeChip, definido no topo do
            // arquivo) - era exatamente aqui que 7 chips (3240/6K e 4320/8K novos) estouravam
            // a tela a 300% de escala.
            REAL x = L + kColRot, cyChips = cy;
            for (int i = 0; i < kNReconst; i++) {
                const int alvo = kReconstAlt[i];
                if (alvo < 0) continue;   // "Resolução do Windows" ja foi desenhada acima, sozinha
                wchar_t b[32];
                if (alvo == 0) wcscpy(b, T(L"Sem upscaling", L"No upscaling"));
                else _snwprintf(b, 32, L"%dp", alvo);
                b[31] = 0;
                const REAL w = LarguraTexto(b, fChip) + 28;
                ProxLinhaDeChip(x, cyChips, w, L + kColRot, IW - kColRot);
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
            if (cy < cyUpsIni + 66.f) cy = cyUpsIni + 66.f;   // o rotulo da esquerda (2 linhas) cabe inteiro
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
        } else if (s.temUpscaler) {
            // ---- E2: NO NATIVO, A QUALIDADE DO UPSCALER DO PROPRIO JOGO (DLSS, FSR 2+ ou XeSS).
            // Mesmas chaves do card "DLSS do jogo" (dlss_qualidade / dlss_preset, no perfil do
            // jogo): o arranque as manda para a DLL quando a ponte liga. Jogo sem upscaler
            // proprio (RE2) nao chega aqui: a secao some.
            if (g) Txt(g, T(L"Upscaling do jogo", L"Game upscaling"), fItemSub, C_BODY, RectF(L, cy, IW, 18), 0);
            cy += 22;
            const std::wstring tipQ = TS(
                L"UPSCALING DO JOGO\nEscolhe a qualidade do DLSS, FSR ou XeSS do próprio jogo: a porcentagem é a resolução "
                L"em que o jogo desenha antes de ampliar. Menos = mais FPS e menos detalhe.\n\"Do jogo\" mantém o que está no "
                L"menu do jogo.\nSe a imagem não mudar, troque a opção de upscaling no menu do jogo uma vez.",
                L"GAME UPSCALING\nPicks the quality of the game's own DLSS, FSR or XeSS: the percentage is the resolution "
                L"the game draws at before upscaling. Lower = more FPS and less detail.\n\"Game's own\" keeps what is set in "
                L"the game's menu.\nIf the image does not change, toggle the upscaling option in the game's menu once.");
            REAL x = L, cyChips = cy;
            for (int i = 0; i < kNQualJogo; i++) {
                const wchar_t* b = T(kQualJogo[i].pt, kQualJogo[i].en);
                const REAL w = LarguraTexto(b, fChip) + 28;
                ProxLinhaDeChip(x, cyChips, w, L, IW);
                RectF r(x, cyChips, w, 30);
                const int idx = Add(r, K_DLSSQUAL, i, true, tipQ);
                if (g) Chip(g, r, b, s.dlssQualidade == kQualJogo[i].chave, g_hot == idx);
                x += w + 8;
            }
            cy = cyChips + 30 + 8;
            // preset: campo secundario, na mesma linha do rotulo curto
            if (g) Txt(g, L"Preset", fItemSub, C_FAINT, RectF(L, cy, 70, 30), 0);
            const std::wstring tipP = TS(
                L"PRESET\nO modelo de IA do DLSS. Auto deixa o jogo e o driver escolherem. Só vale para o DLSS.",
                L"PRESET\nThe DLSS AI model. Auto lets the game and the driver choose. DLSS only.");
            x = L + 70; cyChips = cy;
            for (int i = 0; i < kNDlssPresets; i++) {
                const wchar_t* b = (i == 0) ? L"Auto" : kDlssPresets[i];
                const REAL w = LarguraTexto(b, fChip) + 20;
                ProxLinhaDeChip(x, cyChips, w, L + 70, IW - 70);
                RectF r(x, cyChips, w, 30);
                const int idx = Add(r, K_DLSSPRESET, i, true, tipP);
                if (g) Chip(g, r, b, s.dlssPreset == kDlssPresets[i], g_hot == idx);
                x += w + 6;
            }
            cy = cyChips + 30 + 12;
            // I6 (18/09/2026): a chavinha "DLSS 5 antes do upscaling" (H1/H3) saiu daqui - virou o
            // chip Upstream do card Modo, acima.
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
        //     E9 (16/09): errado - o RCAS exige work_upscale 1 ou 2 (dlss5-feed.cpp:6277). Ver o gate da barra.
        //   - "Limite de FPS" e "Resolução de renderização": independem do upscaling (ritmo
        //     de apresentacao e tamanho da janela do JOGO, nao da rede) -> FICAM.
        //
        // NENHUM VALOR MUDA AO ESCONDER: e so o Render() que pula o bloco (nem o texto, nem
        // os chips, nem o espaco que eles ocupavam) - s.curto/s.iaOrdem e as chaves do ini
        // continuam exatamente como estavam, e reaparecem do jeito que a pessoa deixou se
        // ela ligar o upscaling de novo. O ReShade (F7) e os efeitos que a pessoa tenha posto
        // la CONTINUAM valendo com upscaling desligado - isto aqui nao mexe nisso.
        const bool upscalingLigado = (s.reconstrucao != 0) && !nativo;   // E2: a pelicula nao amplia no Nativo
        const bool srForcadoNoIni = (IniGet(L"super_resolucao") == L"sr");   // escolha explicita, persiste entre telas

        // ---- caminho curto do SR (so com upscaling ligado - ver o gate acima)
        if (upscalingLigado) {
            const wchar_t* nomes[2] = { T(L"Normal", L"Normal"), T(L"Máxima (recomendado)", L"Maximum (recommended)") };
            if (g) Txt(g, T(L"Qualidade do upscaling", L"Upscaling quality"), fLinha, C_BODY, RectF(L, cy, kColRot - 8, 30), 0, true);   // Rodada 27: mesma linha
            REAL x = L + kColRot, cyChips = cy;
            for (int i = 0; i < 2; i++) {
                const REAL w = LarguraTexto(nomes[i], fChip) + 28;
                ProxLinhaDeChip(x, cyChips, w, L + kColRot, IW - kColRot);
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


        // ---- em que MOMENTO a IA trabalha (antes ou depois de ampliar) - so com upscaling
        // ligado, OU se o Super Resolution tiver sido escolhido a mao noutra tela (ver o
        // gate grande, acima de "caminho curto do SR").
        // O CONTROLE QUE DECIDE O CUSTO. A rede roda no tamanho da imagem que RECEBE: se
        // trabalhar ANTES de ampliar ela ve 720p (~11 ms pela lei medida); se trabalhar
        // DEPOIS ve 4K (~43 ms). Sao 4x - e foi isso que derrubou a pelicula para 23 fps no
        // teste de 14/09/2026, quando o caminho curto passou a entregar 4K para a rede.
        // O rotulo fala de QUALIDADE e CUSTO, que e o que o jogador decide; a ordem das
        // etapas fica no tooltip.
        if ((upscalingLigado || (srForcadoNoIni && !nativo)) && !g_moduloAmd && !FeederTemNrPreSr()) {
            // 2.1: este feeder nao le `nr_pre_sr` - a escolha nao tem efeito nenhum. Linha DESABILITADA
            // (rotulo e nota em C_FAINT, sem chips), com a verdade em vez de um chip "pesa ~4x".
            if (g) {
                Txt(g, T(L"Qualidade da IA", L"AI quality"), fLinha, C_FAINT, RectF(L, cy, kColRot - 8, 30), 0, true);
                Txt(g, T(L"nesta versão a IA já trabalha no tamanho de trabalho — não há o que escolher aqui",
                         L"in this version the AI already works at the working size — there is nothing to choose here"),
                    fItemSub, C_FAINT, RectF(L + kColRot, cy, IW - kColRot, 30), 0, true);
            }
            cy += 30 + 8;
        } else if (upscalingLigado || (srForcadoNoIni && !nativo)) {
            const wchar_t* nomes[2] = { T(L"Rápida (recomendado)", L"Fast (recommended)"),
                                        T(L"Máxima (pesa ~4x)", L"Maximum (~4x heavier)") };
            if (g) Txt(g, T(L"Qualidade da IA", L"AI quality"), fLinha, C_BODY, RectF(L, cy, kColRot - 8, 30), 0, true);   // Rodada 27: mesma linha
            REAL x = L + kColRot, cyChips = cy;
            for (int i = 0; i < 2; i++) {
                const REAL w = LarguraTexto(nomes[i], fChip) + 28;
                ProxLinhaDeChip(x, cyChips, w, L + kColRot, IW - kColRot);
                RectF r(x, cyChips, w, 30);
                const int idx = Add(r, K_IAORDEM, i, true,
                    TS(L"QUALIDADE DA IA\nDecide em que momento a rede neural trabalha."
                       L"Rapida: a IA melhora a imagem PEQUENA e so depois o upscaling amplia. A rede ve "
                       L"720p em vez de 4K e custa cerca de um quarto - e o que mantem o FPS alto.\n"
                       L"Maxima: o upscaling amplia primeiro e a IA trabalha na imagem ja grande. Pode "
                       L"recuperar detalhe que nao existia na imagem pequena, mas pesa cerca de 4 vezes.\n"
                       L"A Rapida so existe no motor proprio da rede: escolher aqui liga esse motor junto. "
                       L"Com o motor RenoDX (o padrao) vale sempre a Maxima.",
                       L"AI QUALITY\nDecides when the neural network works."
                       L"Fast: the AI improves the SMALL image and only then upscaling enlarges it. The "
                       L"network sees 720p instead of 4K and costs about a quarter - this is what keeps FPS high.\n"
                       L"Maximum: upscaling runs first and the AI works on the already-large image. It may "
                       L"recover detail that was not in the small image, but costs about 4x.\n"
                       L"Fast only exists in the built-in network engine: picking it here turns that engine on too. "
                       L"With the RenoDX engine (the default) Maximum always applies."));
                if (g) Chip(g, r, nomes[i], s.iaOrdem == i, g_hot == idx);
                x += w + 8;
            }
            cy = cyChips + 30 + 8;
        }
        // RODAPE GERAL (fora do bloco acima de proposito: ele vale para "Upscaling" e
        // "Limite de FPS" tambem, que continuam visiveis mesmo com "Qualidade da IA"
        // escondida - antes este texto vivia preso dentro do bloco da IA e sumia junto,
        // o que deixava as OUTRAS reguas desta secao sem o aviso).
        // Rodada 27 (v9): o Limite de FPS e a Nitidez sairam desta tela (ficam so no menu End; os
        // valores continuam os mesmos do ini) - o rodape fala so do upscaling.
        if (g) Txt(g, T(L"o upscaling vale quando o programa abre — por isso fica aqui",
                        L"upscaling applies when the program opens — that's why it's here"),
                   fItemSub, C_FAINT, RectF(L, cy, IW, 18), 0, true);
        cy += 24;
    }

    // -------- camera: a lista de modos
    if (s.fonte == SRC_CAMERA) {
        const auto* ms = Modos(s);
        const int n = ms ? (int)ms->size() : 0;
        // J8: o nome que o jogador de console entende (era "MODO")
        if (g) Txt(g, T(L"RESOLUÇÃO / FPS / FORMATO", L"RESOLUTION / FPS / FORMAT"), fSec, C_DIM, RectF(L, cy, IW, 16), 0);
        cy += 22;
        const int vis = n < kLinhasModo ? n : kLinhasModo;
        if (n > 0 && s.rolModo > n - vis) s.rolModo = n - vis;
        if (s.rolModo < 0) s.rolModo = 0;
        const REAL boxH = (n == 0) ? 56.f : vis * kLinhaH + 8;
        RectF box(L, cy, IW, boxH);
        g_rcListaModo = box;
        if (g) { FillRound(g, box, 8.f, C_LISTA); StrokeRound(g, box, 8.f, C_EDGE, 1.f); }
        if (n == 0) {
            if (g && s.selDev >= 0)
                TxtWrap(g, T(L"Esse aparelho não ofereceu nenhuma resolução. Outro programa pode estar usando a placa: feche OBS, Discord ou o navegador e clique em \"Atualizar lista\".",
                             L"This device offered no resolution at all. Another program may be using the card: close OBS, Discord or your browser and click \"Refresh list\"."),
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
    // Rodada 27 (v9, aprovado pelo dono): a chave HDR SAIU (o HDR ja e automatico - o valor do ini
    // continua valendo); SOBREPOR, Esconder e Cursor foram para um bloco "Avancado" RECOLHIDO por
    // padrao (clicar no titulo expande/recolhe). Ficam a vista: Tela cheia, V-Sync, FPS na tela e
    // Anti-cheat (no Nativo so FPS na tela e Anti-cheat, como antes).
    {
        if (g) Txt(g, T(L"OPÇÕES", L"OPTIONS"), fSec, C_DIM, RectF(L, cy, IW, 16), 0);
        cy += 22;
        const REAL gap = 10, bw = (IW - gap * 3) / 4, bh = 40;
        const bool tela = Tela(s);
        auto celula = [&](int i, const RectF& r) {
            // MODO ANTI-CHEAT detectado na janela escolhida: ligada sozinha, esmaecida, nao clicavel
            const bool detect = (i == SW_ANTICHEAT) && s.antiCheatDetectado;
            const bool en = detect ? false : (i == SW_SOBREPOR || i == SW_CURSOR || i == SW_ESCONDER) ? tela : true;
            std::wstring tip = TipSw(i, s);
            if (i == SW_FPS && nativo)
                tip = TS(L"FPS NA TELA\nContador pequeno no canto do jogo: fps do jogo e, com frame generation, fps na tela.",
                         L"FPS ON SCREEN\nSmall counter in the game's corner: game fps and, with frame generation, on-screen fps.");
            if (!en && !detect) tip += T(L"\n(não se aplica à placa de captura)", L"\n(does not apply to the capture card)");
            if (i == SW_ANTICHEAT && s.podeNativo) tip += T(L"\nLigado: só o modo Universal.", L"\nOn: Universal mode only.");
            if (detect) tip += TS(L"\nLigado sozinho: anti-cheat detectado nesta fonte (",
                                  L"\nTurned on by itself: anti-cheat detected in this source (") + s.antiCheatProduto + L").";
            int idx = Add(r, K_SW, i, en, tip);
            if (g) {
                FillRound(g, r, 6.f, (g_hot == idx && en) ? C_ROWH : C_ROW);
                Txt(g, NomeSw(i), i == SW_SOBREPOR ? fBadge : fChip, en ? C_TXT : C_FAINT,
                    RectF(r.X + 12, r.Y, r.Width - 12 - 52, r.Height), 0, true);
                Switch(g, RectF(r.X + r.Width - 52, r.Y, 40, r.Height), detect || (s.sw[i] && en), en || detect);
            }
        };
        int vis[4]; int nVis = 0;
        const int principais[4] = { SW_TELACHEIA, SW_VSYNC, SW_FPS, SW_ANTICHEAT };
        for (int i : principais) if (!nativo || i == SW_ANTICHEAT || i == SW_FPS) vis[nVis++] = i;
        for (int k = 0; k < nVis; k++) celula(vis[k], RectF(L + k * (bw + gap), cy, bw, bh));
        cy += bh + 10;
        // o bloco Avancado (so fora do Nativo: as tres sao da pelicula)
        if (!nativo) {
            const wchar_t* tAv = T(L"Avançado", L"Advanced");
            const REAL wAv = LarguraTexto(tAv, fChip) + 34.f;
            RectF rAv(L, cy, wAv, 28.f);
            const int iAv = Add(rAv, K_AVANCADO, 0, true,
                TS(L"Avançado\nSobrepor, Esconder e Cursor — o jeito como a película fica na tela. Clique para mostrar ou esconder.",
                   L"Advanced\nOverlay, Hide and Cursor — how the overlay sits on screen. Click to show or hide."));
            if (g) {
                if (g_hot == iAv) FillRound(g, rAv, 4.f, C_ROWH);
                Txt(g, g_selAvancado ? L"▾" : L"▸", fChip, C_DIM, RectF(rAv.X + 4, rAv.Y, 16, rAv.Height), 1);
                Txt(g, tAv, fChip, C_BODY, RectF(rAv.X + 22, rAv.Y, rAv.Width - 22, rAv.Height), 0);
            }
            cy += 28.f + 6.f;
            if (g_selAvancado) {
                const int avancadas[3] = { SW_SOBREPOR, SW_ESCONDER, SW_CURSOR };
                for (int k = 0; k < 3; k++) celula(avancadas[k], RectF(L + k * (bw + gap), cy, bw, bh));
                cy += bh + 10;
            }
        }
        cy += 6;
    }

    // -------- a placa que roda o filtro: informa com uma, pergunta com duas ou mais
    // (E3: nao no Nativo - a ponte exige a mesma placa do jogo)
    if (!nativo) {
        if (g) Txt(g, T(L"PLACA DE VÍDEO QUE RODA O FILTRO", L"GRAPHICS CARD THAT RUNS THE FILTER"), fSec, C_DIM, RectF(L, cy, IW, 16), 0);
        cy += 22;
        const std::wstring tipG = TS(
            L"PLACA QUE RODA O FILTRO\nO modelo neural roda nesta placa. Com uma só placa na máquina não há o que "
            L"escolher; com duas ou mais, dá para rodar o DLSS 5 numa e o jogo na outra. A escolha fica gravada e "
            L"vale nas próximas aberturas também.",
            L"CARD THAT RUNS THE FILTER\nThe neural model runs on this card. With a single card there is nothing to "
            L"choose; with two or more you can run DLSS 5 on one and the game on the other. The choice is saved and "
            L"also applies to the next launches.");
        const int n = (int)s.placasIdx.size();
        // BUG 6: Radeon de uma serie do mod (kSeriesRadeonMod) sem o mod instalado nao e "sem DLSS 5" por
        // falta de placa: falta o mod em bin\mod-amd (v2.1, pasta unica). O rotulo diz o que falta.
        auto semDlss = [&](const PlacaOpcao& p) -> std::wstring {
            if (!g_moduloAmd && SerieRadeonDoMod(p.vendor, p.nome))
                return TS(L"mod AMD não instalado", L"AMD mod not installed");
            return TS(L"sem DLSS 5", L"no DLSS 5");
        };
        auto rotulo = [&](int i) {
            const PlacaOpcao& p = g_placas[s.placasIdx[i]];
            return p.nome + (p.dlss ? TS(L"  ·  DLSS 5 ok", L"  ·  DLSS 5 ok") : (L"  ·  " + semDlss(p)));
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
                // 2.1: monitor na outra placa = cada quadro atravessa entre as duas. Dica em todo chip afetado; a frase curta
                // entra na segunda linha do chip escolhido (so texto, o layout nao muda).
                const bool outraPlaca = MonitorEmOutraPlacaQue(s, g_placas[s.placasIdx[i]]);
                const std::wstring notaOutra = TS(L"o monitor está na outra placa: cada quadro atravessa entre as duas, pode ficar lento",
                                                  L"the monitor is on the other card: every frame crosses between the two, it may be slow");
                int idx = Add(r, K_GPU, i, true, outraPlaca ? tipG + L"\n\n" + notaOutra : tipG);
                if (g) {
                    FillRound(g, r, 6.f, sel ? C_ACC : (g_hot == idx) ? C_ROWH : C_ROW);
                    const PlacaOpcao& p = g_placas[s.placasIdx[i]];
                    Txt(g, p.nome.c_str(), fBadge, sel ? C_DARK : C_BODY, RectF(r.X + 12, r.Y + 3, r.Width - 24, 18), 0, true);
                    const std::wstring l2 = (p.dlss ? std::wstring(L"DLSS 5 ok") : semDlss(p)) +
                                            ((outraPlaca && sel) ? TS(L"  ·  monitor na outra placa: pode ficar lento", L"  ·  monitor on the other card: may be slow") : L"");
                    Txt(g, l2.c_str(), fItemSub, sel ? C_DARK : C_FAINT,
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
            FillRound(g, r, 8.f, C_AMB_BG);
            StrokeRound(g, r, 8.f, C_WARN, 1.f);
            FillRound(g, RectF(r.X, r.Y + 14, 5.f, r.Height - 28), 2.f, C_WARN);
            TxtWrap(g, s.faltaTxt.c_str(), fChip, C_AMB_TX, RectF(r.X + 20, r.Y + 12, IW - 40, th + 4));
        }
        // QUANDO O AVISO E "A BUILD NAO COBRE ESTA PLACA", o texto da faixa e curto de
        // proposito e o detalhe - quais sm ela tem e qual baixar - fica aqui, na dica. Area
        // so do texto: os dois botoes abaixo tem as dicas deles e nao podem ser cobertos.
        {
            const std::wstring tipSm = DlssnrDicaFaixa();
            if (!tipSm.empty()) Add(RectF(r.X + 20, r.Y + 12, IW - 40, th + 4), K_INFO, 0, false, tipSm);
        }
        const wchar_t* b1 = g_moduloAmd ? T(L"Baixar o mod", L"Get the mod") : T(L"Baixar os arquivos", L"Get the files");
        const wchar_t* b2 = T(L"Abrir a pasta", L"Open the folder");
        const REAL by = r.Y + 12 + th + 10;
        RectF r1(r.X + 20, by, LarguraTexto(b1, fBadge) + 30, 30);
        RectF r2(r1.X + r1.Width + 8, by, LarguraTexto(b2, fBadge) + 30, 30);
        int i1 = Add(r1, K_BAIXAR, 0, true, TS(L"Abre no navegador a página de onde baixar.", L"Opens the download page in your browser."));
        int i2 = Add(r2, K_PASTA, 0, true, TS(L"Abre a pasta do app — é aqui que os arquivos têm de ficar.", L"Opens the app folder — this is where the files must go."));
        if (g) {
            FillRound(g, r1, 4.f, g_hot == i1 ? C_ACC2 : C_WARN); Txt(g, b1, fBadge, C_DARK, r1, 1);
            FillRound(g, r2, 4.f, g_hot == i2 ? C_ROWH : C_OFF);  Txt(g, b2, fBadge, C_BODY, r2, 1);
        }
        cy += bh + 14;
    }

    // -------- a faixa do DIAGNOSTICO (N3, 16/09/2026)
    //
    // MESMA ROUPA DA FAIXA DE CIMA, e de proposito: quem ja viu o aviso dos arquivos que
    // faltam reconhece esta na hora. A diferenca e que ela nao tem botao - nao ha um arquivo
    // para baixar nem uma pasta para abrir, ha uma frase e o que fazer, e o que fazer fica
    // na dica. So aparece com veredito; sem evidencia, silencio (ver o bloco do Diag).
    //
    // SO ENTRA SE A OUTRA NAO ESTIVER FALANDO DO MESMO ASSUNTO: com a build do dlssnr
    // errada, a faixa de cima ja diz isso e com mais detalhe. Duas tarjas ambar seguidas
    // dizendo a mesma coisa e o tipo de tela que ninguem le ate o fim.
    if (!Diag::FaixaTexto().empty() &&
        !(Diag::Atual().regra == Diag::V_DLSSNR_PLACA && s.falta.smRuim)) {
        const std::wstring& txt = Diag::FaixaTexto();
        const REAL th = AlturaTexto(txt.c_str(), fChip, IW - 40);
        RectF r(L, cy, IW, 12 + th + 12);
        if (g) {
            FillRound(g, r, 8.f, C_AMB_BG);
            StrokeRound(g, r, 8.f, C_WARN, 1.f);
            FillRound(g, RectF(r.X, r.Y + 12, 5.f, r.Height - 24), 2.f, C_WARN);
            TxtWrap(g, txt.c_str(), fChip, C_AMB_TX, RectF(r.X + 20, r.Y + 12, IW - 40, th + 4));
        }
        Add(RectF(r.X + 20, r.Y + 12, IW - 40, th + 4), K_INFO, 0, false, Diag::FaixaDica());
        cy += r.Height + 14;
    }

    // -------- sec. 23: a barra "Rede neural: X ms de Y ms" nesta tela SAIU em 24/09/2026
    // (item 8 do dono): o dono achou a barra do MENU END (dentro da sessao, com numeros
    // medidos de verdade) realista e util, e a desta tela de escolha (so estimativa, antes
    // de comecar) redundante. BarraDaTela() (a versao so-estimativa) foi removida junto.


    // -------- sec. 22: "REINICIAR VIDEO DO WINDOWS" (pedido do dono, 23/09 noite)
    //
    // Ultimo recurso MANUAL depois de a placa resetar: Win+Ctrl+Shift+B por SendInput, com confirmacao, nunca com jogo em
    // tela cheia aberto, e 3 s depois a sonda D3D12 num processo novo diz se a placa aceita um dispositivo novo. REALCADO
    // (ambar) so quando o ini tem reset registrado depois do ultimo boot; sem isso fica discreto, mas continua clicavel.
    // O aviso honesto vai no texto e na dica: o atalho recria a imagem do DWM, e reiniciar o driver e efeito colateral.
    {
        const ULONGLONG desde = GpuReset::SegundosDesdeUltimo();
        const bool resetNoBoot = desde != ~0ull && desde < GetTickCount64() / 1000ull;
        const wchar_t* tB = T(L"Reiniciar vídeo do Windows", L"Restart Windows video");
        const wchar_t* tD = T(L"— recupera o desempenho após a placa resetar. Pode fechar um jogo aberto.",
                              L"— recovers performance after the card resets. May close an open game.");
        const REAL bw = LarguraTexto(tB, fBadge) + 30, bh = 30;
        const REAL dW = IW - bw - 12;
        const REAL dh = AlturaTexto(tD, fItemSub, dW);
        const REAL rowH = dh + 4 > bh ? dh + 4 : bh;
        RectF rb(L, cy + (rowH - bh) / 2, bw, bh);
        const int ib = Add(rb, K_VIDEO, 0, !g_videoTestando,
            TS(L"REINICIAR VÍDEO DO WINDOWS\nEnvia Win+Ctrl+Shift+B, o atalho do Windows que recria a imagem da tela. Serve para recuperar o "
               L"desempenho depois de a placa de vídeo resetar (o app registra quando isso acontece; aí o botão fica em destaque).\n\n"
               L"A tela pisca e pode ficar preta por 1–2 s, e um jogo aberto pode fechar — por isso ele não dispara com jogo em tela cheia.\n\n"
               L"Aviso honesto: reiniciar o driver da placa é efeito colateral desse atalho e nem sempre acontece. 3 s depois o app testa "
               L"a placa num processo novo e diz o resultado; se não resolver, reinicie o PC.",
               L"RESTART WINDOWS VIDEO\nSends Win+Ctrl+Shift+B, the Windows shortcut that rebuilds the screen image. It is for recovering "
               L"performance after the graphics card resets (the app records when that happens; then the button is highlighted).\n\n"
               L"The screen blinks and may go black for 1–2 s, and an open game may close — so it does not fire with a full-screen game open.\n\n"
               L"Honest note: restarting the graphics driver is a side effect of that shortcut and does not always happen. 3 s later the app "
               L"tests the card in a new process and tells you the result; if it does not help, restart the PC."));
        if (g) {
            FillRound(g, rb, 4.f, g_videoTestando ? C_OFF : resetNoBoot ? (g_hot == ib ? C_ACC2 : C_WARN) : (g_hot == ib ? C_ROWH : C_OFF));
            Txt(g, tB, fBadge, g_videoTestando ? C_FAINT : resetNoBoot ? C_DARK : C_BODY, rb, 1);
            TxtWrap(g, tD, fItemSub, resetNoBoot ? C_AMB_TX : C_FAINT, RectF(rb.X + bw + 12, cy + (rowH - dh) / 2, dW, dh + 4));
        }
        cy += rowH;
        if (!g_videoMsg.empty()) {
            const REAL mh = AlturaTexto(g_videoMsg.c_str(), fItemSub, IW);
            if (g) TxtWrap(g, g_videoMsg.c_str(), fItemSub, C_WARN, RectF(L, cy + 6, IW, mh + 4));
            cy += 6 + mh;
        }
        cy += 14;
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
        int ia = Add(ra, K_ATUALIZAR, 0, true, TS(L"ATUALIZAR LISTA\nProcura de novo as janelas, monitores, placas de captura e câmeras. Use depois de abrir o jogo ou ligar a placa.",
                                                  L"REFRESH LIST\nLooks for windows, monitors, capture cards and cameras again. Use it after opening the game or plugging the card in."));
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
    // Rodada 27 (v9): a moldura do painel v8 (sombra, cartao #1E1E1E, borda #333, cantos de 8)
    Hud::MolduraSimples8(g, (REAL)SH, (REAL)SH, (REAL)CW, (REAL)cardH);
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
    // Rodada 27 (v9): o TAMANHO DA INTERFACE (o mesmo [ultima] tamanho_ui do painel) - DPI x fator,
    // limitado ao teto desta tela: a janela cabe INTEIRA na area util do monitor em que esta.
    {
        const REAL dpi = g_scale;
        g_scale = Hud::EscalaComTeto(g_s->wnd, dpi, 1.f, (REAL)(CW + SH * 2), (REAL)(cardH + SH * 2), &g_selFatorMax);
        g_selFatorUsado = g_scale / dpi;
    }
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
    // E1: o modo vai ao arranque como --modo. Janela que nao pode usar o Nativo (ou Modo anti-cheat
    // ligado) fica em Universal; Automatico sem upscaler no jogo e com upscaling escolhido tambem,
    // porque so a pelicula amplia. As chaves escondidas no Nativo nao mudam (voltam no Universal).
    s.ch.modo.clear(); s.ch.camposNativo = false;
    if (s.fonte == SRC_WINDOW) {
        if (!EscolhaDeModo(s)) s.ch.modo = L"universal";
        else {
            IniSet(L"modo_processamento", NomeModoProcW(s.modo));
            s.ch.camposNativo = CamposNativo(s);
            // I6: o Upstream e o Nativo (transporte) com a rede antes do upscaling; o arranque
            // le a escolha no [ultima] modo_processamento e decide o resto (UpstreamAbrir)
            if (s.modo == MODO_NATIVO || s.modo == MODO_UPSTREAM) s.ch.modo = L"nativo";
            else if (s.modo == MODO_UNIVERSAL || (!s.temUpscaler && AmpliaPelaPelicula(s))) s.ch.modo = L"universal";
        }
        Log("seletor: INICIAR - modo escolhido %s, abre com %ls%s%s%ls", EscolhaDeModo(s) ? NomeModoProc(s.modo) : "(sem escolha)",
            s.ch.modo.empty() ? L"automatico" : s.ch.modo.c_str(), s.ch.camposNativo ? " (campos do Nativo)" : "",
            !EscolhaDeModo(s) ? "" : !s.gancho ? " - sem chip Upstream (jogo sem DLSS/FSR/XeSS com gancho)"
                              : !ChipUpstream(s) ? " - sem chip Upstream (caiu duas vezes neste jogo), gancho " : " - gancho ",
            (EscolhaDeModo(s) && s.gancho) ? s.gancho : L"");
    }
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
    // sec. 23 (dono: sem popup): a memoria de resets e so o texto sob a barra "Rede neural" desta tela
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
    // Rodada 27 (v9): o tamanho da interface - 0 Auto, 1 "-", 2 "+" (o passo parte do fator que
    // esta tela esta USANDO e o "+" para no teto dela). O break repinta: o Paint le o tamanho novo
    // e redimensiona a janela na hora.
    case K_TAMUI:
        Hud::DefinirTamanhoUi(it.val == 0 ? 0 : Hud::PassoTamanhoUi(g_selFatorUsado, g_selFatorMax, it.val == 1 ? -1 : +1));
        break;
    case K_AVANCADO: g_selAvancado = !g_selAvancado; break;   // Rodada 27: expande/recolhe o bloco "Avancado"
    case K_FONTE: TrocarFonte((SourceKind)it.val); break;
    case K_ALVO: {
        int& sel = SelAlvo(s);
        if (sel != it.val) { sel = it.val; if (s.fonte == SRC_CAMERA) CarregarModos(s); }
        break;
    }
    case K_MODO: s.selModo = it.val; break;
    case K_RECORTE: s.clientOnly = (it.val == 0); break;
    // E1: grava no perfil do jogo na hora (o g_perfilExe aponta para ele, ver MedirNativo)
    case K_MODOPROC:
        // 2.2.3: Nativo e Upstream foram removidos - so o Universal vale (e nem a janela dos modos abre)
        if (it.val != MODO_UNIVERSAL) { Log("seletor: modo %s pedido - removido na 2.2.3, fica em Universal", NomeModoProc(it.val)); break; }
        // v2.0 (dono): para Nativo/Upstream, a janela dos modos antes (uma vez por sessao; ver namespace Risco).
        // Cancelar = fica no que estava (o Universal, de fabrica).
        if (s.modo != it.val && !Risco::PerguntarModal(s.wnd, s.modo, it.val, s.antiCheatDetectado, s.perfilExe)) {
            Log("seletor: modo %s cancelado na janela dos modos", NomeModoProc(it.val));
            break;
        }
        if (s.modo != it.val) {
            // I7 ("so a mao do jogador muda"): a troca a mao zera as marcas de falha do jogo,
            // para ele ter uma chance limpa - Upstream e Nativo (cadeia A8/A9)
            ModoZerarFalhas("seletor");
            s.upstreamFalhas = 0;
        }
        s.modo = it.val;
        IniSet(L"modo_processamento", NomeModoProcW(it.val));
        Log("seletor: modo -> %s", NomeModoProc(it.val));
        break;
    // E4: uma tentativa por clique, sem laco; o Render le de novo o que o jogo aceitou
    case K_EXIBICAO:
        if (s.selWin >= 0 && s.selWin < (int)s.wins.size()) {
            WinTarget& w = s.wins[s.selWin];
            s.exibPedida = it.val;
            AplicarExibicao(w.hwnd, it.val);
            RECT rc{};
            if (GetClientRect(w.hwnd, &rc)) { w.w = rc.right - rc.left; w.h = rc.bottom - rc.top; }
        }
        break;
    // E2: as chaves do card "DLSS do jogo"; o arranque manda para a DLL quando a ponte liga
    case K_DLSSQUAL:
        s.dlssQualidade = kQualJogo[it.val].chave;
        IniSet(L"dlss_qualidade", s.dlssQualidade);
        break;
    case K_DLSSPRESET:
        s.dlssPreset = kDlssPresets[it.val];
        IniSet(L"dlss_preset", s.dlssPreset);
        break;
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
        // MODO ANTI-CHEAT: persistente, chave propria, gravada na hora (nao e opcao de sessao).
        if (it.val == SW_ANTICHEAT) { IniSet(L"anticheat", v ? L"1" : L"0"); Log("seletor: modo anti-cheat %s ([ultima] anticheat)", v ? "LIGADO" : "desligado"); break; }
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
    case K_VIDEO: {   // sec. 22
        EsconderTip();
        if (g_videoTestando) return;
        std::wstring jogo;
        if (ReinicioAuto::JanelaTelaCheiaAlheia(&jogo)) {
            Log("seletor: Reiniciar video do Windows - ha uma janela em tela cheia (\"%s\"): NAO disparo, peco para fechar o jogo antes", U8(jogo).c_str());
            g_videoMsg = TS(L"Feche o jogo antes: \"", L"Close the game first: \"") + jogo +
                         TS(L"\" está em tela cheia, e o atalho do Windows pode fechá-lo.", L"\" is in full screen, and the Windows shortcut may close it.");
            break;
        }
        const int r = MessageBoxW(s.wnd,
            T(L"Reiniciar o vídeo do Windows?\n\nA tela pisca e pode ficar preta por 1–2 s, e um jogo aberto pode fechar.\n\n"
              L"Aviso honesto: este atalho do Windows (Win+Ctrl+Shift+B) recria a imagem da área de trabalho; reiniciar o driver da placa "
              L"é efeito colateral e nem sempre acontece. 3 s depois o app testa a placa e diz o resultado.",
              L"Restart Windows video?\n\nThe screen blinks and may go black for 1–2 s, and an open game may close.\n\n"
              L"Honest note: this Windows shortcut (Win+Ctrl+Shift+B) rebuilds the desktop image; restarting the graphics driver "
              L"is a side effect and does not always happen. 3 s later the app tests the card and tells you the result."),
            APP_TITLE, MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2);
        if (r != IDYES) { Log("seletor: Reiniciar video do Windows - cancelado na confirmacao"); return; }
        const UINT n = ReinicioAuto::DispararAtalhoVideo();
        IniSet(L"video_reiniciado_em", std::to_wstring(ReinicioAuto::Agora1601()));
        Log("seletor: Reiniciar video do Windows - Win+Ctrl+Shift+B enviado por SendInput (%u de 8 eventos aceitos); reset registrado no ini: %s; sonda em 3 s",
            n, IniGet(L"gpu_reset_em").empty() ? "nao" : "sim");
        g_videoTestando = true;
        g_videoMsg = n == 8 ? TS(L"Atalho enviado — a tela pisca. Testando a placa em 3 s…", L"Shortcut sent — the screen blinks. Testing the card in 3 s…")
                            : TS(L"O Windows não aceitou o atalho enviado pelo programa: aperte você mesmo Win+Ctrl+Shift+B. Testando a placa em 3 s…",
                                 L"Windows did not accept the shortcut sent by the program: press Win+Ctrl+Shift+B yourself. Testing the card in 3 s…");
        SetTimer(s.wnd, kTimerVideo, 3000, nullptr);
        break;
    }
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
        if (hit >= 0 && g_it[hit].enabled) {
            // Rodada 27 (v10): o "-"/"+" do tamanho da interface repete enquanto o botao fica
            // apertado (o mesmo ritmo do painel: 420 ms para comecar, um passo a cada 110 ms)
            if (g_it[hit].kind == K_TAMUI && g_it[hit].val >= 1) {
                g_repIt = g_it[hit]; g_repAtivo = true; g_repVezes = 0;
                SetTimer(h, kTimerRepeteSel, 420, nullptr);
            }
            return 0;   // e um controle: o LBUTTONUP resolve
        }
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
        if (wp == kTimerRepeteSel) {   // Rodada 27 (v10): segurar o "-"/"+" repete
            if (!g_repAtivo || !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) { KillTimer(h, kTimerRepeteSel); g_repAtivo = false; return 0; }
            g_repVezes++;
            Ativar(g_repIt);
            SetTimer(h, kTimerRepeteSel, 110, nullptr);
            return 0;
        }
        if (wp == kTimerTopo) {   // I3: continua por cima do jogo
            SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return 0;
        }
        if (wp == kTimerVideo) {   // sec. 22: 3 s depois do Win+Ctrl+Shift+B, a sonda num processo novo
            KillTimer(h, kTimerVideo);
            std::wstring luid;
            if (g_s && g_s->selPlaca >= 0 && g_s->selPlaca < (int)g_s->placasIdx.size()) {
                const int pi = g_s->placasIdx[g_s->selPlaca];
                if (pi >= 0 && pi < (int)g_placas.size()) {
                    wchar_t b[40]; _snwprintf(b, 40, L"%08lX:%08lX", (unsigned long)g_placas[pi].luid.HighPart, (unsigned long)g_placas[pi].luid.LowPart); b[39] = 0; luid = b;
                }
            }
            std::string det;
            const int cod = ReinicioAuto::RodarSonda(luid, &det);
            Log("seletor: Reiniciar video do Windows - sonda D3D12 em processo novo: codigo %d (%s)", cod, det.c_str());
            g_videoTestando = false;
            g_videoMsg = cod == 0
                ? TS(L"Teste num processo novo: a placa aceitou um dispositivo novo. Se o desempenho não voltar, reinicie o PC — o atalho nem sempre reinicia o driver.",
                     L"Test in a new process: the card accepted a new device. If performance does not come back, restart the PC — the shortcut does not always restart the driver.")
                : TS(L"Teste num processo novo: a placa ainda NÃO aceita (", L"Test in a new process: the card still does NOT accept it (") +
                  std::wstring(det.begin(), det.end()) +
                  TS(L"). Reinicie o PC — o atalho nem sempre reinicia o driver.", L"). Restart the PC — the shortcut does not always restart the driver.");
            Paint();
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
        // soltar depois de um repique nao e mais um passo (o botao ja acionou enquanto segurado)
        if (g_repAtivo) { KillTimer(h, kTimerRepeteSel); g_repAtivo = false; if (g_repVezes > 0) return 0; }
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
    // H7: a busca do upscaler na raiz do jogo Unreal terminou (thread do MedirNativo)
    case kWmSelUnreal:
        if (g_s && AplicarUnreal(*g_s, reinterpret_cast<AchadoUnreal*>(lp))) Paint();
        else if (!g_s) delete reinterpret_cast<AchadoUnreal*>(lp);
        return 0;
    case WM_CLOSE: Cancelar(); return 0;
    case WM_DESTROY:
        KillTimer(h, kTimerTip);
        KillTimer(h, kTimerTopo);
        KillTimer(h, kTimerVideo);   // sec. 22
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
    LerEscolhas(s);   // E1: a cada janela escolhida o perfil dela vem por cima (MedirNativo)
    s.sw[SW_TELACHEIA] = wantFs;
    s.sw[SW_VSYNC] = wantVsync;
    s.sw[SW_HDR] = wantHdr;
    // A chavinha do FPS le o ReShade.ini: a escolha salva em [ultima] fps_tela ja foi
    // aplicada la na abertura (ver wWinMain). Chave ausente = padrao do ReShade, ligado.
    s.sw[SW_FPS] = (FpsNaTelaNoReShadeIni() != 0);
    s.sw[SW_ANTICHEAT] = (IniGet(L"anticheat") == L"1");   // MODO ANTI-CHEAT: persistente, chave propria
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
    // I3: com um jogo em tela cheia ja aberto, esta tela sobe por cima dele - primeiro plano
    // de verdade (o jogo le o mouse e o Windows pode recusar o SetForegroundWindow simples),
    // topmost, e o timer reafirma o topo enquanto a tela viver.
    if (HWND jogo = JogoEmTelaCheia(st)) {
        wchar_t cls[64]{}; GetClassNameW(jogo, cls, 64);
        const bool ok = Hud::ForcarPrimeiroPlano(st.wnd);
        SetWindowPos(st.wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetTimer(st.wnd, kTimerTopo, kTopoMs, nullptr);
        Log("seletor: ha um jogo em tela cheia aberto (classe %s) - a tela sobe por cima dele (primeiro plano: %s)",
            U8(cls).c_str(), ok ? "sim" : "o Windows recusou; fica no topo mesmo assim");
    }
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

    // I3 (18/09/2026, pedido do dono): "subir a janela mesmo antes de selecionar a fonte com a
    // tecla End". Enquanto esta tela vive, a tecla do painel (End; Insert no AMD) e uma hotkey
    // global: apertada com um jogo na frente, a tela de selecao vem para o primeiro plano e para
    // o topo, sem Alt+Tab. Id fora da faixa HK_ACAO_BASE para nao colidir com o painel, que so
    // registra as dele depois que a fonte foi escolhida.
    const int kHkSeletor = 0x5E1E, kHkSeletorEnd = 0x5E1F;   // o End tambem, quando a tecla do painel for outra (AMD = Insert)
    // 2.1: o RegistrarTodosAtalhos (arranque) JA registrou a tecla do painel (e o End extra) nesta
    // mesma thread. Registrar de novo a MESMA tecla aqui da erro 1409 (ja registrada) - entao o seletor
    // usa as do arranque (ids HK_ACAO_BASE+AT_PAINEL e HK_PAINEL_END_EXTRA, tratados no laco abaixo) e
    // so registra as suas quando o arranque nao as registrou (camada 2, ponte, tecla recusada).
    const bool painelDoArranque = g_atalhos[AT_PAINEL].registrado;
    const bool hkProprio = !painelDoArranque;
    const bool hkOk = painelDoArranque || RegisterHotKey(nullptr, kHkSeletor, g_atalhos[AT_PAINEL].mods | MOD_NOREPEAT, g_atalhos[AT_PAINEL].vk) != 0;
    const bool hkEnd = hkProprio && (g_atalhos[AT_PAINEL].vk != VK_END || g_atalhos[AT_PAINEL].mods != 0) && RegisterHotKey(nullptr, kHkSeletorEnd, MOD_NOREPEAT, VK_END) != 0;
    if (!hkOk) Log("seletor: a tecla do painel nao pode ser registrada agora (erro %lu) - a tela nao sobe por ela", (unsigned long)GetLastError());
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY && ((int)msg.wParam == kHkSeletor || (int)msg.wParam == kHkSeletorEnd
                                                  || (painelDoArranque && ((int)msg.wParam == HK_ACAO_BASE + AT_PAINEL || (int)msg.wParam == HK_PAINEL_END_EXTRA))) && st.wnd) {
            // 2.1 (pedido do dono): seletor MINIMIZADO tambem volta - sem restaurar, o primeiro plano
            // ia para uma janela minimizada e nada aparecia. Ja visivel/em foco: segue como antes.
            if (IsIconic(st.wnd)) ShowWindow(st.wnd, SW_RESTORE);
            const bool ok = Hud::ForcarPrimeiroPlano(st.wnd);
            SetWindowPos(st.wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetFocus(st.wnd);
            Log("seletor: tecla do painel - a tela de selecao sobe por cima (primeiro plano: %s)", ok ? "sim" : "o Windows recusou; fica no topo");
            continue;
        }
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (hkOk && hkProprio) UnregisterHotKey(nullptr, kHkSeletor);
    if (hkEnd) UnregisterHotKey(nullptr, kHkSeletorEnd);
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
    // v2.0: DLSS5_SELETOR_SHOT_MODO=1 mostra a linha "Modo" (a janela de amostra nao tem processo para
    // medir; finge um jogo de 64 bits que aceita o Nativo). O modo marcado e o de fabrica: Universal.
    { wchar_t mo[4]{}; if (GetEnvironmentVariableW(L"DLSS5_SELETOR_SHOT_MODO", mo, 4) > 0 && mo[0] == L'1') st.podeNativo = true; }
    g_s = &st; g_hot = -1;
    // A faixa so existe depois de alguem PERGUNTAR o que falta; o caminho da foto ia
    // direto para o Render e o aviso nunca aparecia, mesmo com a chave ligada.
    if (!st.ignorarFalta) AtualizarFalta(st);
    // o PNG sai em 96 dpi, no TAMANHO DA INTERFACE escolhido (Rodada 27; Automatico = Medio)
    const REAL sFoto = Hud::FatorUiFoto();
    g_scale = sFoto;
    g_selFatorUsado = sFoto; g_selFatorMax = (REAL)Hud::kTamUiMax / 100.f;   // o valor mostrado no "Aa - N% +"
    { wchar_t av[4]{}; g_selAvancado = GetEnvironmentVariableW(L"DLSS5_SELETOR_SHOT_AVANCADO", av, 4) > 0 && av[0] == L'1'; }   // foto com o bloco Avancado aberto
    int cardH = Render(nullptr);
    int tipItem = -1;
    if (comTooltip)
        for (size_t i = 0; i < g_it.size(); i++) if (g_it[i].kind == K_SW && g_it[i].val == SW_SOBREPOR) { g_hot = (int)i; tipItem = (int)i; }
    const int W = (int)((CW + SH * 2) * sFoto + 0.5f), H = (int)((cardH + SH * 2) * sFoto + 0.5f);
    Bitmap bmp(W, H, PixelFormat32bppARGB);
    Graphics g(&bmp);
    {
        LinearGradientBrush bg(Point(0, 0), Point(W, H), Color(255, 58, 64, 78), Color(255, 26, 28, 36));
        g.FillRectangle(&bg, 0, 0, W, H);
    }
    g.ScaleTransform(sFoto, sFoto);
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
