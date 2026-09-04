// =====================================================================================
//  splash_ofertas.h - convite pro grupo de ofertas do PC do Fafa
//
//  Uma janela sem borda, topmost e layered (mesmo padrao do ShowToast do main.cpp),
//  na identidade do PC do Fafa: preto profundo, acento ciano, gradiente ciano->azul
//  no botao, titulo em caixa alta. QR grande a esquerda com o endereco escrito
//  embaixo; marca, gancho, motivo e botao a direita.
//
//  ELA E O LAUNCHER DO APP, e nao um aviso a ser fechado. Sao TRES saidas, todas
//  botoes de verdade e sempre na mesma posicao:
//    QUERO AS OFERTAS   (ciano, primario)  -> abre o grupo no navegador
//    APOIAR O CANAL     (vazado)           -> abre o canal; so existe com `canal` no ini
//    INICIAR PROGRAMA   (verde)            -> fecha esta janela e o app segue
//
//  O verde substituiu um "Agora nao" cinza. Era a MESMA acao - fechar e seguir -, mas
//  escrita como desistencia, o que fazia o convite parecer um pedagio: a pessoa achava
//  que o normal era fechar no X. Agora o caminho de quem so quer usar o programa e um
//  botao claro, e ninguem precisa fechar nada para comecar.
//
//  A ordem dos botoes NAO muda entre aberturas. Embaralhar para colher o clique de quem
//  nao le foi considerado e recusado: quem clica errado nao vira inscrito, vira gente
//  irritada - e quem abre este app todo dia e justamente o publico do canal.
//
//  Clicavel SO o que e obviamente clicavel: QR, endereco e os tres botoes; o X fecha; o
//  resto da janela e area morta de proposito.
//
//  Ela NAO fecha sozinha. Nao ha contagem regressiva, prazo nem timer; a chave `segundos`
//  do ofertas.ini e o parametro de mesmo nome continuam aceitos so pra nao quebrar nada,
//  e sao ignorados.
//
//  Ela sai da captura (SetWindowDisplayAffinity/EXCLUDEFROMCAPTURE), entao nao entra
//  no video capturado nem no filtro.
//
//  Nao coleta nada, nao manda nada pra lugar nenhum: so desenha e, se a pessoa
//  clicar, chama o navegador.
//
//  A TELA e A DECISAO sao coisas separadas:
//    ShowOfferSplash(...)          -> desenha agora, sem estado nenhum. Serve tanto pra
//                                    abertura do app quanto pro item fixo do painel.
//    ofertas::DeveMostrarNaAbertura() -> aplica a politica do ofertas.ini (sempre /
//                                    primeira_vez / nunca) e marca que ja mostrou.
//  Quem decide e quem chama.
//
//  Configuracao: ofertas.ini ao lado do exe (editado a mao, nunca reescrito pelo app).
//  Estado: dlss5forall.ini, secao [ofertas], chave ja_mostrado (escrito pelo app).
//  So GDI: nada de GDI+, nada de imagem - a marca aparece em texto.
// =====================================================================================
#pragma once
#include "idioma.h"   // T(pt, en): a janela do convite tem texto de tela
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cmath>
#include "qr.h"

// windowsx.h pode nao estar incluido em quem usa o header
#ifndef GET_X_LPARAM_SAFE
#define GET_X_LPARAM_SAFE(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM_SAFE(lp) ((int)(short)HIWORD(lp))
#endif

namespace ofertas {

// Com que frequencia o convite aparece SOZINHO na abertura do app.
enum Politica { POL_SEMPRE = 0, POL_PRIMEIRA_VEZ = 1, POL_NUNCA = 2 };

// O destino de fabrica. Fica separado porque e para ele que a validacao volta quando o
// ofertas.ini traz um endereco que nao e http(s).
inline const wchar_t* UrlPadrao() { return L"https://grupo.pcdofafa.com.br/pg"; }

// ShellExecuteW("open", s) NAO abre so paginas: com um caminho de arquivo, um UNC de rede
// ou um verbo de protocolo registrado ele EXECUTA o que estiver ali. O ofertas.ini fica
// ao lado do exe e e editavel por qualquer um, entao o unico endereco que aceitamos e
// http:// ou https://, sem espaco na frente.
//
// Alem do esquema, recusamos o que ShellExecuteW poderia reinterpretar: espaco e
// caracteres de controle (que serviriam pra esconder o resto da linha), barra invertida
// (caminho de arquivo / UNC disfarcado de URL) e enderecos absurdamente longos.
//
// A aspa dupla e o caso serio: o handler do protocolo e uma linha de comando com %1
// dentro de aspas ("chrome.exe" -- "%1"), entao uma aspa no meio do endereco fecha o
// argumento e o resto vira parametro do navegador (--gpu-launcher=calc.exe e afins).
// Junto dela vao < > | ^, que nao aparecem em endereco de verdade.
//
// `&` FICA PERMITIDO de proposito: sem a aspa, ele nao sai de dentro do "%1" - e e o
// separador de query string, ou seja, e o que faz ?utm_source=app&utm_medium=qr
// funcionar. Rastrear de onde vem quem entra no grupo e o motivo do /pg existir.
constexpr size_t kUrlMax = 2048;

inline bool UrlEhWeb(const std::wstring& u) {
    const bool esquema = (u.size() > 8 && _wcsnicmp(u.c_str(), L"https://", 8) == 0) ||
                         (u.size() > 7 && _wcsnicmp(u.c_str(), L"http://",  7) == 0);
    if (!esquema || u.size() > kUrlMax) return false;
    for (wchar_t c : u)
        if (c <= L' ' || c == 0x7F || c == L'\\' || c == L'"' ||
            c == L'<' || c == L'>' || c == L'|' || c == L'^') return false;
    return true;
}

// A recusa vale a pena aparecer: quem editou o ini errado precisa de um sinal, e quem
// tentou trocar o destino por um executavel deixa rastro no DebugView/depurador.
inline void LogUrlRecusada(const wchar_t* onde, const std::wstring& u) {
    std::wstring m = L"[ofertas] ";
    m += onde;
    m += L": endereco recusado (so http:// ou https://, sem espaco, controle ou \\): ";
    m += u.substr(0, 200);
    m += L"\n";
    OutputDebugStringW(m.c_str());
}

// Ponto unico de "so pode ir pra frente o que e web": QR, texto na tela e navegador.
inline std::wstring UrlSegura(const std::wstring& u) {
    return UrlEhWeb(u) ? u : std::wstring(UrlPadrao());
}

struct Config {
    std::wstring url      = UrlPadrao();
    // Canal do criador. Vazio de proposito: sem endereco no ofertas.ini o botao "APOIE O
    // CRIADOR" nem aparece no painel - prometer um link que nao existe e pior do que nao
    // ter o botao.
    std::wstring canal;
    std::wstring marca    = L"PC DO FAFÁ";
    std::wstring submarca = TS(L"PROMOÇÕES", L"DEALS");
    std::wstring titulo   = TS(L"Peça de PC pelo menor preço", L"PC parts at the lowest price");
    std::wstring mensagem = TS(L"A gente mostra onde comprar barato no Brasil e como importar pagando menos. O app é grátis - o grupo é como você retribui.",
                               L"We show you where to buy cheap in Brazil and how to import for less. The app is free - the group is how you pay it back.");
    std::wstring microcta = TS(L"Aponta a câmera no código ou clica no botão",
                               L"Point your camera or click the button");
    std::wstring botao    = TS(L"QUERO AS OFERTAS", L"SHOW ME THE DEALS");
    // Esta tela e o LAUNCHER do app: o botao secundario nao e desistir, e comecar. Ele
    // sempre fez isso (fechava a janela e o app seguia), mas dizia "Agora nao" - texto de
    // recusa para a acao que a maioria vem fazer.
    std::wstring botao2   = TS(L"INICIAR PROGRAMA", L"START THE PROGRAM");
    std::wstring rodape   = TS(L"aponte a câmera do celular", L"point your phone camera here");
    Politica politica = POL_SEMPRE;
    bool     forcar   = false;      // forcar_splash=1: reexibe mesmo com a politica fechada
};

namespace detail {

inline std::wstring ExeDir() {
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    size_t k = s.find_last_of(L"\\/");
    return (k == std::wstring::npos) ? std::wstring() : s.substr(0, k + 1);
}

// Le o ini na mao (em vez de GetPrivateProfileString) pra aceitar tanto UTF-8 quanto
// UTF-16 - assim o arquivo sobrevive a uma edicao no Bloco de Notas com acento.
inline std::wstring ReadTextFile(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    if (sz.QuadPart <= 0 || sz.QuadPart > (1 << 20)) { CloseHandle(f); return L""; }
    std::vector<unsigned char> buf((size_t)sz.QuadPart);
    DWORD rd = 0;
    ReadFile(f, buf.data(), (DWORD)buf.size(), &rd, nullptr);
    CloseHandle(f);
    buf.resize(rd);
    if (buf.size() >= 2 && buf[0] == 0xFF && buf[1] == 0xFE)          // UTF-16 LE
        return std::wstring((const wchar_t*)(buf.data() + 2), (buf.size() - 2) / 2);
    size_t off = (buf.size() >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) ? 3 : 0;
    if (buf.size() <= off) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char*)buf.data() + off, (int)(buf.size() - off), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, (const char*)buf.data() + off, (int)(buf.size() - off), &w[0], n);
    return w;
}

inline std::wstring Trim(std::wstring s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}
// "\n" escrito literalmente no ini vira quebra de linha de verdade
inline std::wstring Unescape(const std::wstring& s) {
    std::wstring o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == L'\\' && i + 1 < s.size() && (s[i + 1] == L'n' || s[i + 1] == L'N')) { o += L'\n'; i++; }
        else o += s[i];
    }
    return o;
}
// Sufixo "_en" nas chaves de TEXTO do ofertas.ini: "titulo=" e a versao em portugues,
// "titulo_en=" a em ingles. Uma chave so vale quando bate com o idioma em uso - senao o
// ini em portugues sobrescreveria a traducao embutida e a janela sairia meio traduzida.
inline bool TerminaEmEn(const std::wstring& k) {
    return k.size() > 3 && _wcsicmp(k.c_str() + k.size() - 3, L"_en") == 0;
}
inline bool ChaveDoIdioma(const std::wstring& k) { return TerminaEmEn(k) == EmIngles(); }
inline std::wstring SemSufixoEn(const std::wstring& k) {
    return TerminaEmEn(k) ? k.substr(0, k.size() - 3) : k;
}

inline bool Falso(const std::wstring& v) {
    return v == L"0" || _wcsicmp(v.c_str(), L"nao") == 0 || _wcsicmp(v.c_str(), L"não") == 0
        || _wcsicmp(v.c_str(), L"false") == 0 || _wcsicmp(v.c_str(), L"nunca") == 0;
}
inline std::wstring Maiuscula(std::wstring s) {   // o papel do Oswald: titulo em caixa alta
    if (!s.empty()) CharUpperBuffW(&s[0], (DWORD)s.size());
    return s;
}

// Teto por campo. Um ini com uma linha de 200 mil caracteres nao pode virar uma chamada
// de DrawTextW de 200 mil caracteres a cada repintura - o texto que cabe na janela e
// muito menor que isso, e o que passa daqui so serviria pra travar o desenho.
constexpr size_t kValorMax = 512;    // titulo, mensagem, botoes, rodape...
constexpr size_t kLinhaMax = 8192;   // linha inteira do ini (chave = valor)

// Caracteres de controle nao desenham: viram espaco, menos o \n que o Unescape criou de
// proposito. Sem isso um \r ou um \x01 perdido no ini vira retangulo na tela.
inline std::wstring Sanitiza(std::wstring s, bool permiteQuebra) {
    if (s.size() > kValorMax) s.resize(kValorMax);
    for (wchar_t& c : s)
        if (c < L' ' && !(permiteQuebra && c == L'\n')) c = L' ';
    return s;
}

} // namespace detail

// Endereco como a pessoa le e digita: sem esquema, sem www, sem barra sobrando.
// A barra do meio FICA - o caminho faz parte do destino.
inline std::wstring UrlVisivel(const std::wstring& url) {
    std::wstring s = url;
    for (const wchar_t* p : { L"https://", L"http://" })
        if (s.size() > wcslen(p) && _wcsnicmp(s.c_str(), p, wcslen(p)) == 0) { s = s.substr(wcslen(p)); break; }
    if (s.size() > 4 && _wcsnicmp(s.c_str(), L"www.", 4) == 0) s = s.substr(4);
    while (!s.empty() && s.back() == L'/') s.pop_back();
    return s;
}

// Le ofertas.ini (secao [ofertas]) ao lado do exe. Chaves: url, mostrar, forcar_splash,
// titulo, mensagem, cta, botao, botao_fechar, rodape, marca, submarca.
// `segundos` ainda e aceita e descartada: a janela nao fecha mais sozinha.
// O que faltar fica com o valor de `base`. O app NUNCA reescreve este arquivo.
//
// Este arquivo e editado a mao por quem baixou o app: nenhum valor daqui pode derrubar
// nada. Linha gigante, lixo binario, chave desconhecida, valor fora de faixa - tudo isso
// e ignorado e o padrao embutido continua valendo.
inline Config LoadConfig(Config base = Config()) {
    std::wstring txt = detail::ReadTextFile(detail::ExeDir() + L"ofertas.ini");
    if (txt.empty()) { base.url = UrlSegura(base.url); return base; }

    bool inSection = false;
    size_t pos = 0;
    while (pos <= txt.size()) {
        size_t nl  = txt.find(L'\n', pos);
        size_t fim = (nl == std::wstring::npos) ? txt.size() : nl;
        size_t len = (fim > pos) ? fim - pos : 0;
        size_t ini = pos;
        pos = (nl == std::wstring::npos) ? txt.size() + 1 : nl + 1;
        if (len == 0 || len > detail::kLinhaMax) continue;   // linha absurda: nem copia

        std::wstring line = detail::Trim(txt.substr(ini, len));
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[') {
            size_t e = line.find(L']');
            inSection = (e != std::wstring::npos) &&
                        (_wcsicmp(detail::Trim(line.substr(1, e - 1)).c_str(), L"ofertas") == 0);
            continue;
        }
        if (!inSection) continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos || eq == 0) continue;
        std::wstring k = detail::Trim(line.substr(0, eq)), v = detail::Trim(line.substr(eq + 1));
        if (k.empty() || v.empty()) continue;

        auto texto = [&](const std::wstring& s, bool quebra) {
            return detail::Sanitiza(quebra ? detail::Unescape(s) : s, quebra);
        };
        if (_wcsicmp(k.c_str(), L"url") == 0) {
            if (UrlEhWeb(v)) base.url = v;
            else             LogUrlRecusada(L"ofertas.ini", v);
        }
        else if (_wcsicmp(k.c_str(), L"canal") == 0) {
            if (UrlEhWeb(v)) base.canal = v;
            else             LogUrlRecusada(L"ofertas.ini (canal)", v);
        }
        // AS CHAVES DE TEXTO TEM IDIOMA, e as outras nao.
        //
        // O ofertas.ini que acompanha o app esta em portugues, e ele sobrescreve os textos
        // embutidos. Sem esta regra, quem abrisse o app em ingles leria a janela do convite
        // em portugues - o ini venceria a traducao em todas as linhas.
        //
        // Entao: "titulo=" vale quando o app esta em portugues, "titulo_en=" quando esta em
        // ingles. Sem a chave do idioma em uso, fica o padrao embutido, que ja e traduzido.
        // url, canal, mostrar e forcar_splash nao sao texto: valem sempre, sem sufixo.
        else if (_wcsicmp(k.c_str(), L"segundos") == 0)     { /* aceita e descarta: sem contagem */ }
        else if (_wcsicmp(k.c_str(), L"forcar_splash") == 0) base.forcar = !detail::Falso(v);
        else if (detail::ChaveDoIdioma(k)) {
            const std::wstring kb = detail::SemSufixoEn(k);
            if      (_wcsicmp(kb.c_str(), L"titulo") == 0)       base.titulo   = texto(v, true);
            else if (_wcsicmp(kb.c_str(), L"mensagem") == 0)     base.mensagem = texto(v, true);
            else if (_wcsicmp(kb.c_str(), L"cta") == 0)          base.microcta = texto(v, true);
            else if (_wcsicmp(kb.c_str(), L"botao") == 0)        base.botao    = texto(v, false);
            else if (_wcsicmp(kb.c_str(), L"botao_fechar") == 0) base.botao2   = texto(v, false);
            else if (_wcsicmp(kb.c_str(), L"rodape") == 0)       base.rodape   = texto(v, false);
            else if (_wcsicmp(kb.c_str(), L"marca") == 0)        base.marca    = texto(v, false);
            else if (_wcsicmp(kb.c_str(), L"submarca") == 0)     base.submarca = texto(v, false);
        }
        else if (_wcsicmp(k.c_str(), L"mostrar") == 0) {
            // Qualquer coisa que nao seja "primeira*" nem uma negacao conhecida cai no
            // padrao: sempre. Valor escrito errado nunca deve calar o convite.
            if (detail::Falso(v))                               base.politica = POL_NUNCA;
            else if (_wcsnicmp(v.c_str(), L"primeira", 8) == 0) base.politica = POL_PRIMEIRA_VEZ;
            else                                                base.politica = POL_SEMPRE;
        }
    }
    base.url = UrlSegura(base.url);   // rede final, valha o ini ou o argumento de quem chamou
    return base;
}

// --------------------------------------------------------------- estado (dlss5forall.ini)
inline bool JaMostrou() {
    wchar_t b[16]{};
    GetPrivateProfileStringW(L"ofertas", L"ja_mostrado", L"0", b, 16,
                             (detail::ExeDir() + L"dlss5forall.ini").c_str());
    return b[0] == L'1';
}
inline void MarcarQueMostrou() {
    WritePrivateProfileStringW(L"ofertas", L"ja_mostrado", L"1",
                               (detail::ExeDir() + L"dlss5forall.ini").c_str());
}

// -------------------------------------------------------------------------------------
//  A DECISAO: o convite deve abrir sozinho agora, nesta execucao?
//  Chame no ponto de integracao, logo antes de ShowOfferSplash. Quando devolve true ja
//  grava a marca de "mostrado" (na hora de exibir, nao na saida do app - se travar, a
//  pessoa nao ve de novo).
//  Politica em ofertas.ini: mostrar=sempre (padrao) | primeira_vez | 0/nao/nunca.
//  forcar_splash=1 passa por cima da politica (a chave e sua, o app nao a apaga).
// -------------------------------------------------------------------------------------
inline bool DeveMostrarNaAbertura() {
    Config c = LoadConfig();
    bool mostra;
    if (c.forcar)                          mostra = true;
    else if (c.politica == POL_NUNCA)      mostra = false;
    else if (c.politica == POL_SEMPRE)     mostra = true;
    else                                   mostra = !JaMostrou();
    if (mostra) MarcarQueMostrou();
    return mostra;
}

// --------------------------------------------------------------------- visual
namespace detail {

// Paleta do site grupo.pcdofafa.com.br (ver marca/PALETA.md)
static const COLORREF C_BG     = RGB(11, 15, 22);     // #0b0f16
static const COLORREF C_SURF   = RGB(23, 33, 43);     // #17212b
static const COLORREF C_CARD   = RGB(31, 41, 55);     // #1f2937
static const COLORREF C_CY     = RGB(6, 182, 212);    // #06b6d4
static const COLORREF C_CY_L   = RGB(34, 211, 238);   // #22d3ee
static const COLORREF C_CY_D   = RGB(8, 145, 178);    // #0891b2
static const COLORREF C_BLUE   = RGB(37, 99, 235);    // #2563eb
static const COLORREF C_BLUE_L = RGB(59, 130, 246);   // #3b82f6
static const COLORREF C_GREEN  = RGB(22, 163, 74);    // #16a34a  botao INICIAR PROGRAMA
static const COLORREF C_GREEN_L= RGB(34, 197, 94);    // #22c55e  o mesmo, com o mouse em cima
static const COLORREF C_GREEN_D= RGB(21, 128, 61);    // #15803d  pressionado
static const COLORREF C_TEXT   = RGB(255, 255, 255);
static const COLORREF C_BODY   = RGB(209, 213, 219);  // #d1d5db
static const COLORREF C_SUP    = RGB(156, 163, 175);  // #9ca3af
static const COLORREF C_MUTE   = RGB(107, 114, 128);  // #6b7280

// TAMANHO DA JANELA EM UNIDADES LOGICAS (96 dpi). Todo numero deste arquivo - layout,
// alturas de fonte, raios de canto, margens - esta nesta unidade. O unico lugar onde
// eles viram pixel de verdade e a funcao Esc() abaixo, chamada com o DPI do monitor
// onde a janela esta. Num monitor a 100% o resultado e identico ao de antes.
static const int SPW = 880, SPH = 452;

// 96 dpi -> pixels desta tela. Mesmo idioma do painel (main.cpp:1865).
inline int Esc(int v, UINT dpi) { return MulDiv(v, (int)(dpi ? dpi : 96), 96); }

struct Fonts {
    HFONT marca = nullptr, sub = nullptr, title = nullptr, msg = nullptr, cta = nullptr,
          micro = nullptr, addr = nullptr, small_ = nullptr, tiny = nullptr,
          close_ = nullptr;
    UINT dpi = 96;                      // com que DPI estas fontes foram criadas
    // As alturas sao negativas: altura de caractere em PIXEL, ja em 96 dpi - por isso a
    // conversao e MulDiv(v, dpi, 96) e nao a formula de ponto tipografico (dpi/72).
    void make(UINT d = 96) {
        free();
        dpi = d ? d : 96;
        auto f = [this](int h, int w) {
            return CreateFontW(-Esc(-h, dpi), 0, 0, 0, w, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, L"Segoe UI");
        };
        marca   = f(-17, FW_BOLD);
        sub     = f(-11, FW_SEMIBOLD);
        title   = f(-28, FW_BOLD);      // faz o papel do Oswald: bold, caixa alta
        msg     = f(-16, FW_NORMAL);
        cta     = f(-18, FW_BOLD);
        micro   = f(-13, FW_SEMIBOLD);
        addr    = f(-19, FW_SEMIBOLD);
        small_  = f(-14, FW_NORMAL);
        tiny    = f(-12, FW_NORMAL);
        close_  = f(-15, FW_NORMAL);
    }
    void free() {
        for (HFONT* p : { &marca, &sub, &title, &msg, &cta, &micro, &addr, &small_, &tiny, &close_ })
            if (*p) { DeleteObject(*p); *p = nullptr; }
    }
};

// Sem contagem regressiva: a janela nao guarda tempo nenhum, so o estado do mouse.
struct View {
    const Config* cfg = nullptr;
    const qr::Code* code = nullptr;
    Fonts* fonts = nullptr;
    UINT dpi = 96;            // o DPI do monitor onde esta janela esta agora
    bool hoverCta = false, hoverCanal = false, hoverSec = false, hoverClose = false,
         hoverLink = false, pressed = false;
};

// Retangulos calculados num lugar so, pra desenho e clique nunca discordarem. Entram em
// unidades logicas e saem em PIXEL: quem desenha e quem testa o clique usam a mesma
// funcao com o mesmo dpi, entao nao ha como um escalar e o outro nao.
struct Rects { RECT moldura, card, addr, cta, canal, sec, close_; };
inline Rects Layout(int w, int h, UINT dpi) {
    auto E = [dpi](int v) { return Esc(v, dpi); };
    auto R4 = [&](int l, int t, int r, int b) { return RECT{ E(l), E(t), E(r), E(b) }; };
    Rects r{};
    r.moldura = R4(24, 24, 372, 372);                // cartao #1f2937 em volta do QR
    r.card    = R4(32, 32, 364, 364);                // area branca: 332 unidades de lado
    r.addr    = R4(24, 380, 372, 408);               // o endereco escrito, tambem clicavel
    // TRES DESTINOS, TRES BOTOES, POSICAO FIXA. Esta tela virou o launcher do app: a
    // pessoa escolhe entre entrar no grupo, apoiar o canal ou comecar a usar - em vez de
    // ter de FECHAR uma janela para o programa abrir, que era o desenho antigo e fazia o
    // convite parecer obstaculo.
    //
    // A ordem NAO muda de uma abertura para outra. Houve a ideia de embaralhar os botoes
    // para colher o clique de quem nao le; foi recusada. Quem clica sem querer nao vira
    // inscrito, vira gente irritada - e quem abre este app todo dia e exatamente o publico
    // do canal. O que da destaque aqui e HIERARQUIA VISUAL: os dois convites sao grandes e
    // coloridos, o iniciar e discreto. Continua obvio, e sempre no mesmo lugar.
    r.cta     = R4(396, 252, 696, 298);              // grupo de ofertas (primario)
    r.canal   = R4(396, 306, 696, 350);              // apoiar o canal (secundario forte)
    r.sec     = R4(396, 358, 696, 406);              // INICIAR PROGRAMA (verde)
    r.close_  = { w - E(38), E(14), w - E(14), E(38) };   // ancorado na borda direita real
    (void)h;
    return r;
}
// So estes tres levam ao navegador. O resto da janela nao faz nada: clique sem querer
// que joga a pessoa pro browser gera raiva, e raiva nao converte.
// ---------------------------------------------------------------- DPI
// O layout inteiro (Rects, fontes, SPW/SPH) e escrito em unidades de 96 dpi. Num processo
// PER_MONITOR_AWARE_V2 o Windows nao escala nada por nos, entao num 4K a 200% este cartao
// sairia com metade do tamanho aparente - justamente a peca que paga o app.
//
// O QUE FAZEMOS: escala explicita. Cada medida passa por Esc(v, dpi) na hora de virar
// pixel - no tamanho da janela, no Layout(), nas alturas de fonte, nos raios de canto e
// em cada numero solto do Paint. O DIB do degrade ja nasce no tamanho fisico e as regioes
// de recorte tambem, porque tudo que chega neles ja e pixel. O hit-test usa o MESMO
// Layout(w, h, dpi) do desenho, com o ponto do mouse como ele vem (fisico): nao ha
// conversao de ida e volta pra desalinhar.
//
// TENTATIVA QUE NAO FUNCIONOU, registrada para ninguem repetir: SetGraphicsMode(GM_ADVANCED)
// + SetWorldTransform no DC de memoria, desenhando em unidades logicas. A janela saiu do
// tamanho certo (medido: 1100x565 a 125%, e o clique no X acertou), mas o CONTEUDO
// continuou em 880x452 dentro dela - desenho ocupando ate x=875 de 1100. O motivo e do
// GDI: BitBlt NAO participa da transformada de mundo (so enxerga translacao) e todo o
// fundo em degrade passa por um BitBlt de DIBSection; as REGIOES de recorte
// (CreateRoundRectRgn + SelectClipRgn, que fazem os cantos arredondados) tambem sao em
// coordenadas de DISPOSITIVO e ignoram a transformada.
inline UINT DpiDaJanela(HWND h) {
    typedef UINT(WINAPI* PFN)(HWND);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    PFN f = u ? (PFN)GetProcAddress(u, "GetDpiForWindow") : nullptr;
    UINT dpi = f ? f(h) : 96;
    return dpi < 96 ? 96 : dpi;
}
// O DPI do monitor onde a janela VAI nascer (antes de existir HWND).
inline UINT DpiDoPonto(POINT p) {
    typedef HRESULT(WINAPI* PFN)(HMONITOR, int, UINT*, UINT*);
    HMODULE sh = LoadLibraryW(L"shcore.dll");
    PFN f = sh ? (PFN)GetProcAddress(sh, "GetDpiForMonitor") : nullptr;
    UINT dx = 96, dy = 96;
    if (f) f(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), 0, &dx, &dy);   // 0 = MDT_EFFECTIVE
    if (sh) FreeLibrary(sh);
    return dx < 96 ? 96 : dx;
}
// DLSS5_SPLASH_DPI=<numero> força o DPI desta janela, para conferir 125/150/200% numa
// maquina que nao tem aquele monitor. Como ela MUDA o comportamento (o tamanho da
// janela), so vale junto com DLSS5_DIAG=1 - a mesma regra do DLSS5_TESTA_QUEDA e do
// DLSS5_TESTA_POKE no main.cpp (ver o comentario do DiagLigado(), main.cpp:2180).
inline UINT DpiForcado() {
    wchar_t d[8]{};
    if (!GetEnvironmentVariableW(L"DLSS5_DIAG", d, 8) || d[0] != L'1') return 0;
    wchar_t b[16]{};
    if (!GetEnvironmentVariableW(L"DLSS5_SPLASH_DPI", b, 16)) return 0;
    int v = _wtoi(b);
    return (v >= 96 && v <= 960) ? (UINT)v : 0;
}
inline UINT DpiEfetivoJanela(HWND h) { UINT f = DpiForcado(); return f ? f : DpiDaJanela(h); }
inline UINT DpiEfetivoPonto(POINT p) { UINT f = DpiForcado(); return f ? f : DpiDoPonto(p); }

inline bool PontoAbreLink(const Rects& R, POINT p) {
    return PtInRect(&R.moldura, p) || PtInRect(&R.cta, p) || PtInRect(&R.addr, p);
}
inline bool PontoFecha(const Rects& R, POINT p) {
    return PtInRect(&R.sec, p) || PtInRect(&R.close_, p);
}

inline void FillRound(HDC dc, RECT rc, int rad, COLORREF fill, COLORREF edge, bool hasEdge) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN   p = hasEdge ? CreatePen(PS_SOLID, 1, edge) : (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, rad, rad);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b);
    if (hasEdge) DeleteObject(p);
}
inline void StrokeRound(HDC dc, RECT rc, int rad, COLORREF edge) {
    HPEN p = CreatePen(PS_SOLID, 1, edge);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH)), op = SelectObject(dc, p);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, rad, rad);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(p);
}

// Gradiente da marca (135 graus, canto superior esquerdo -> inferior direito) pintado
// dentro do recorte que ja estiver valendo no DC.
inline void GradienteNoRecorte(HDC dc, RECT box, COLORREF a, COLORREF b) {
    int w = box.right - box.left, h = box.bottom - box.top;
    if (w <= 0 || h <= 0) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC tmp = CreateCompatibleDC(dc);
    HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bm) { DeleteDC(tmp); return; }
    HGDIOBJ ob = SelectObject(tmp, bm);
    unsigned char* px = (unsigned char*)bits;
    double den = (double)(w + h - 2); if (den < 1) den = 1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double t = (x + y) / den;
            size_t i = ((size_t)y * w + x) * 4;
            px[i + 0] = (unsigned char)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t);
            px[i + 1] = (unsigned char)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t);
            px[i + 2] = (unsigned char)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t);
            px[i + 3] = 255;
        }
    BitBlt(dc, box.left, box.top, w, h, tmp, 0, 0, SRCCOPY);
    SelectObject(tmp, ob); DeleteObject(bm); DeleteDC(tmp);
}
inline void FillRoundGradient(HDC dc, RECT rc, int rad, COLORREF a, COLORREF b) {
    HRGN rg = CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, rad, rad);
    SelectClipRgn(dc, rg);
    GradienteNoRecorte(dc, rc, a, b);
    SelectClipRgn(dc, nullptr);
    DeleteObject(rg);
}

inline void DrawText1(HDC dc, HFONT f, COLORREF col, RECT rc, const wchar_t* s, UINT fmt, int spacing = 0) {
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, col);
    SetTextCharacterExtra(dc, spacing);
    DrawTextW(dc, s, -1, &rc, fmt | DT_NOPREFIX);
    SetTextCharacterExtra(dc, 0);
    SelectObject(dc, of);
}
// Texto que quebra linha dentro de uma caixa FIXA. Se nao couber na fonte de projeto,
// desce de um em um ate caber (ou ate o piso). Duas coisas fazem isso ser necessario:
//  - o ofertas.ini e editavel, e uma frase mais longa transbordaria por cima do que vem
//    embaixo (a mensagem invadiria o selo, o titulo invadiria a mensagem);
//  - o GDI nao escala fonte linearmente: a MESMA frase que cabe em duas linhas a 100%
//    pede tres a 200%, porque o arredondamento de avanco por glifo nao dobra junto.
// Custa uma medicao por repintura, e a repintura so acontece quando o mouse muda de area.
inline void DrawTextCaixa(HDC dc, HFONT base, const wchar_t* s, RECT box, COLORREF col,
                          UINT fmt, int alturaLogica, int minimoLogico, int peso,
                          UINT dpi, int spacing = 0) {
    auto cabe = [&](HFONT f) {
        RECT calc{ 0, 0, box.right - box.left, 0 };
        HGDIOBJ of = SelectObject(dc, f);
        SetTextCharacterExtra(dc, spacing);
        DrawTextW(dc, s, -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SetTextCharacterExtra(dc, 0);
        SelectObject(dc, of);
        return (calc.bottom - calc.top) <= (box.bottom - box.top);
    };
    if (cabe(base)) { DrawText1(dc, base, col, box, s, fmt, spacing); return; }
    HFONT f = nullptr;
    for (int a = alturaLogica - 1; a >= minimoLogico; a--) {
        if (f) DeleteObject(f);
        f = CreateFontW(-Esc(a, dpi), 0, 0, 0, peso, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                        CLEARTYPE_QUALITY, 0, L"Segoe UI");
        if (cabe(f) || a == minimoLogico) break;
    }
    DrawText1(dc, f ? f : base, col, box, s, fmt, spacing);
    if (f) DeleteObject(f);
}

inline SIZE MedeTexto(HDC dc, HFONT f, const wchar_t* s, int spacing = 0) {
    HGDIOBJ of = SelectObject(dc, f);
    SetTextCharacterExtra(dc, spacing);
    RECT r{ 0, 0, 4000, 0 };
    DrawTextW(dc, s, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    SetTextCharacterExtra(dc, 0);
    SelectObject(dc, of);
    return SIZE{ r.right - r.left, r.bottom - r.top };
}

// Desenha o QR dentro do cartao branco, com modulos em tamanho inteiro de pixel e
// quiet zone de 4 modulos - se isso escorregar, leitor nenhum le.
inline void DrawQr(HDC dc, RECT card, const qr::Code& c, UINT dpi) {
    FillRound(dc, card, Esc(12, dpi), RGB(255, 255, 255), 0, false);
    if (c.size <= 0) return;
    const int quiet = 4;
    int total = c.size + quiet * 2;
    int box = (card.right - card.left) - Esc(14, dpi);       // margem branca extra do cartao
    int ms = box / total;
    if (ms < 1) ms = 1;
    int side = ms * total;
    int ox = card.left + ((card.right - card.left) - side) / 2;
    int oy = card.top + ((card.bottom - card.top) - side) / 2;
    HBRUSH dark = CreateSolidBrush(RGB(10, 10, 12));
    for (int y = 0; y < c.size; y++)
        for (int x = 0; x < c.size; x++)
            if (c.at(x, y)) {
                RECT m{ ox + (quiet + x) * ms, oy + (quiet + y) * ms,
                        ox + (quiet + x + 1) * ms, oy + (quiet + y + 1) * ms };
                FillRect(dc, &m, dark);
            }
    DeleteObject(dark);
}

// Todo o desenho da janela num lugar so - WM_PAINT e a ferramenta de screenshot usam
// exatamente esta funcao, entao o PNG de conferencia e o que a tela mostra.
inline void Paint(HDC dc, int w, int h, const View& v) {
    const UINT dpi = v.dpi ? v.dpi : 96;
    auto E = [dpi](int x) { return Esc(x, dpi); };   // toda medida solta daqui pra baixo
    Rects R = Layout(w, h, dpi);
    const Config& C = *v.cfg;
    Fonts& F = *v.fonts;

    RECT all{ 0, 0, w, h };
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    HBRUSH edge = CreateSolidBrush(C_CARD);
    FrameRect(dc, &all, edge);
    DeleteObject(edge);

    // faixa de marca no topo (o eco da barra azul do site)
    RECT topo{ 0, 0, w, E(3) };
    GradienteNoRecorte(dc, topo, C_CY, C_BLUE);

    // ---- coluna do QR: cartao escuro, codigo grande e o endereco escrito
    FillRound(dc, R.moldura, E(18), C_CARD, 0, false);
    DrawQr(dc, R.card, *v.code, dpi);
    // O que a pessoa le tem de ser o mesmo destino que o clique abre: passa pela mesma
    // validacao do OpenLink, senao o texto na tela prometeria uma coisa e o botao outra.
    DrawText1(dc, F.addr, v.hoverLink ? C_CY_L : C_TEXT, R.addr,
              UrlVisivel(UrlSegura(C.url)).c_str(), DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    RECT cap{ R.moldura.left, E(408), R.moldura.right, E(428) };
    DrawText1(dc, F.tiny, C_MUTE, cap, C.rodape.c_str(), DT_CENTER | DT_SINGLELINE);

    // ---- coluna do texto
    const int X = E(396), RW = w - X - E(32);

    RECT nm{ X, E(34), X + RW, E(58) };
    DrawText1(dc, F.marca, C_CY_L, nm, C.marca.c_str(), DT_LEFT | DT_SINGLELINE, E(1));
    RECT sb{ X, E(58), X + RW, E(74) };
    DrawText1(dc, F.sub, C_MUTE, sb, C.submarca.c_str(), DT_LEFT | DT_SINGLELINE, E(3));

    RECT ti{ X, E(92), X + RW - E(70), E(186) };
    DrawTextCaixa(dc, F.title, Maiuscula(C.titulo).c_str(), ti, C_TEXT,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS, 28, 18, FW_BOLD, dpi, E(1));

    RECT ms{ X, E(194), X + RW, E(254) };
    DrawTextCaixa(dc, F.msg, C.mensagem.c_str(), ms, C_BODY,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS, 16, 11, FW_NORMAL, dpi);

    // selo com bordinha ciano, no espirito dos badges do site
    SIZE mt = MedeTexto(dc, F.micro, C.microcta.c_str());   // ja medido na fonte escalada
    RECT bd{ X, E(262), X + (std::min)((LONG)(mt.cx + E(26)), (LONG)RW), E(262 + 28) };
    StrokeRound(dc, bd, E(14), C_CY_D);
    DrawText1(dc, F.micro, C_CY_L, bd, C.microcta.c_str(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    // botao primario no gradiente da marca
    RECT bt = R.cta;
    if (v.pressed) OffsetRect(&bt, 0, E(1));
    if (v.pressed)        FillRoundGradient(dc, bt, E(12), RGB(6, 110, 138), RGB(30, 79, 191));
    else if (v.hoverCta)  FillRoundGradient(dc, bt, E(12), C_CY, C_BLUE_L);
    else                  FillRoundGradient(dc, bt, E(12), C_CY_D, C_BLUE);
    DrawText1(dc, F.cta, C_TEXT, bt, detail::Maiuscula(C.botao).c_str(), DT_CENTER | DT_SINGLELINE | DT_VCENTER, E(1));

    // APOIAR O CANAL: so existe se o ofertas.ini trouxer a chave `canal`. Botao vazado
    // (contorno, sem preenchimento) de proposito - ele e um convite forte, mas nao pode
    // competir de igual para igual com o primario, senao os dois viram ruido.
    if (!C.canal.empty()) {
        RECT bc = R.canal;
        if (v.hoverCanal) FillRound(dc, bc, E(11), C_SURF, 0, false);
        StrokeRound(dc, bc, E(11), v.hoverCanal ? C_CY : C_CY_D);
        DrawText1(dc, F.cta, v.hoverCanal ? C_TEXT : C_CY_L, bc,
                  T(L"APOIAR O CANAL", L"SUPPORT THE CHANNEL"), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    // INICIAR PROGRAMA: botao verde, do tamanho dos outros dois.
    //
    // Aqui estava o "Agora nao" - um texto cinza, discreto, escrito como desistencia. Era
    // a MESMA acao de hoje (fecha esta janela e o app segue), mas o desenho dizia que o
    // caminho normal era fechar a janela no X e que clicar ali era abrir mao de algo. Com
    // a tela virando launcher, isto passou a ser o destino principal: quem so quer usar o
    // programa clica aqui e entra. Verde porque e o "seguir em frente" e porque nao
    // disputa com o ciano da marca, que e dos dois convites.
    {
        RECT bs = R.sec;
        if (v.pressed && v.hoverSec) OffsetRect(&bs, 0, E(1));
        const COLORREF c = (v.pressed && v.hoverSec) ? C_GREEN_D : (v.hoverSec ? C_GREEN_L : C_GREEN);
        FillRound(dc, bs, E(12), c, 0, false);
        DrawText1(dc, F.cta, C_TEXT, bs, detail::Maiuscula(C.botao2).c_str(),
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    // X de fechar
    if (v.hoverClose) FillRound(dc, R.close_, E(6), C_SURF, 0, false);
    DrawText1(dc, F.close_, v.hoverClose ? C_TEXT : C_MUTE, R.close_, L"✕", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    // A assinatura do app, discreta, no pe da coluna de texto. Quem abriu o DLSS5ForAll.exe
    // precisa reconhecer de quem e esta janela sem ter de clicar em nada.
    // Ela ficava no pe da coluna da ESQUERDA e caia em cima do endereco (que ocupa
    // 380-408, logo abaixo do cartao do QR); aqui embaixo do "Agora nao" nao esbarra em
    // nada - e o unico espaco vazio que sobra na janela.
    RECT assin{ X, E(404), X + RW, E(424) };
    if (assin.right > assin.left + E(40))
        DrawText1(dc, F.tiny, C_MUTE, assin, T(L"DLSS5 For All · abrindo…", L"DLSS5 For All · opening…"),
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    // faixa de marca no rodape, fechando o quadro (era a barra de tempo, que saiu junto
    // com a contagem regressiva)
    RECT base{ 0, h - E(3), w, h };
    GradienteNoRecorte(dc, base, C_CY, C_BLUE);
}

// --------------------------------------------------------------------- janela
struct Runtime {
    Config cfg;
    qr::Code code;
    Fonts fonts;
    ULONGLONG t0 = 0;         // so pra entrada suave; nao existe mais prazo de fechamento
    UINT dpi = 96;            // do monitor onde a janela esta; muda em WM_DPICHANGED
    BYTE alpha = 0;
    bool hoverCta = false, hoverCanal = false, hoverSec = false, hoverClose = false,
         hoverLink = false, pressed = false, hover = false;
};

inline View MakeView(Runtime& rt) {
    View v;
    v.cfg = &rt.cfg; v.code = &rt.code; v.fonts = &rt.fonts;
    v.dpi = rt.dpi;
    v.hoverCta = rt.hoverCta; v.hoverSec = rt.hoverSec; v.hoverClose = rt.hoverClose;
    v.hoverCanal = rt.hoverCanal;
    v.hoverLink = rt.hoverLink;
    v.pressed = rt.pressed;
    return v;
}

inline bool HideFromCaptureLocal(HWND h) {
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
    typedef BOOL(WINAPI* PFN)(HWND, DWORD);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    PFN f = u ? (PFN)GetProcAddress(u, "SetWindowDisplayAffinity") : nullptr;
    return f && f(h, WDA_EXCLUDEFROMCAPTURE);
}

inline void RoundCorners(HWND h, UINT dpi) {
    // Win11 arredonda de verdade; nos mais antigos cai pra regiao recortada.
    HMODULE d = LoadLibraryW(L"dwmapi.dll");
    bool ok = false;
    if (d) {
        typedef HRESULT(WINAPI* PFN)(HWND, DWORD, LPCVOID, DWORD);
        PFN f = (PFN)GetProcAddress(d, "DwmSetWindowAttribute");
        DWORD pref = 2;   // DWMWCP_ROUND
        if (f) ok = SUCCEEDED(f(h, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &pref, sizeof(pref)));
        FreeLibrary(d);
    }
    if (!ok) {
        RECT r; GetWindowRect(h, &r);
        const int rad = Esc(16, dpi);
        HRGN rg = CreateRoundRectRgn(0, 0, r.right - r.left + 1, r.bottom - r.top + 1, rad, rad);
        SetWindowRgn(h, rg, TRUE);
    }
}

inline void OpenLink(const std::wstring& url) {
    // Segunda barreira (a primeira e no LoadConfig): so http/https chega ao ShellExecuteW.
    // Qualquer outra coisa vira o endereco de fabrica, porque um botao chamado "QUERO AS
    // OFERTAS" que executa um programa e a diferenca entre um convite e um vetor.
    const std::wstring alvo = UrlSegura(url);
    if (alvo != url) LogUrlRecusada(L"OpenLink", url);
    ShellExecuteW(nullptr, L"open", alvo.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

inline LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Runtime* rt = (Runtime*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int w = rc.right, hh = rc.bottom;
        HDC mem = CreateCompatibleDC(dc);                 // buffer duplo: nada de piscar
        HBITMAP bm = CreateCompatibleBitmap(dc, w, hh);
        HGDIOBJ ob = SelectObject(mem, bm);
        if (rt) { View v = MakeView(*rt); Paint(mem, w, hh, v); }
        BitBlt(dc, 0, 0, w, hh, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bm); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    // O unico trabalho do timer e a entrada suave (~180 ms). Depois dela ele morre: a
    // janela nao tem mais nada que mude sozinho e fica parada ate a pessoa fechar.
    case WM_TIMER: {
        if (!rt) { KillTimer(h, 1); return 0; }
        ULONGLONG now = GetTickCount64();
        // O fade termina em 255, opaco. Parava em 250: 2% de transparencia que ninguem
        // pediu e que aparece como o desktop atravessando o cartao inteiro - inclusive o
        // branco atras do QR, que e onde a camera do celular precisa do contraste.
        int a = (int)((now - rt->t0) * 255 / 180);
        rt->alpha = (BYTE)(a > 255 ? 255 : a);
        SetLayeredWindowAttributes(h, 0, rt->alpha, LWA_ALPHA);
        if (rt->alpha >= 255) KillTimer(h, 1);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!rt) return 0;
        POINT p{ GET_X_LPARAM_SAFE(lp), GET_Y_LPARAM_SAFE(lp) };   // ja vem em pixel
        RECT rc; GetClientRect(h, &rc);
        Rects R = Layout(rc.right, rc.bottom, rt->dpi);
        // O botao do canal so responde quando existe endereco: sem a chave `canal` no
        // ofertas.ini ele nao e desenhado, e area que nao aparece nao pode ser clicavel.
        bool hc = PtInRect(&R.cta, p) != 0, hs = PtInRect(&R.sec, p) != 0,
             hn = !rt->cfg.canal.empty() && PtInRect(&R.canal, p) != 0,
             hx = PtInRect(&R.close_, p) != 0,
             hl = (PtInRect(&R.moldura, p) || PtInRect(&R.addr, p)) != 0;
        if (hc != rt->hoverCta || hs != rt->hoverSec || hn != rt->hoverCanal ||
            hx != rt->hoverClose || hl != rt->hoverLink || !rt->hover) {
            rt->hoverCta = hc; rt->hoverSec = hs; rt->hoverCanal = hn;
            rt->hoverClose = hx; rt->hoverLink = hl; rt->hover = true;
            InvalidateRect(h, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (rt) {
            rt->hover = rt->hoverCta = rt->hoverCanal = rt->hoverSec = rt->hoverClose = rt->hoverLink = rt->pressed = false;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_SETCURSOR: {
        // maozinha so onde realmente clica; no resto o cursor continua seta
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        RECT rc; GetClientRect(h, &rc);
        Rects R = Layout(rc.right, rc.bottom, rt ? rt->dpi : DpiEfetivoJanela(h));
        bool clicavel = PontoAbreLink(R, p) || PontoFecha(R, p);
        SetCursor(LoadCursor(nullptr, clicavel ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        if (!rt) return 0;
        POINT p{ GET_X_LPARAM_SAFE(lp), GET_Y_LPARAM_SAFE(lp) };
        RECT rc; GetClientRect(h, &rc);
        Rects R = Layout(rc.right, rc.bottom, rt->dpi);
        rt->pressed = PtInRect(&R.cta, p) != 0;
        if (rt->pressed) { SetCapture(h); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!rt) return 0;
        if (GetCapture() == h) ReleaseCapture();
        rt->pressed = false;
        POINT p{ GET_X_LPARAM_SAFE(lp), GET_Y_LPARAM_SAFE(lp) };
        RECT rc; GetClientRect(h, &rc);
        Rects R = Layout(rc.right, rc.bottom, rt->dpi);
        if (PontoAbreLink(R, p)) { OpenLink(rt->cfg.url); DestroyWindow(h); return 0; }
        // O canal abre no navegador E FECHA a janela, igual ao grupo: a pessoa ja escolheu
        // para onde ir, e deixar o launcher aberto atras do navegador so daria trabalho.
        if (!rt->cfg.canal.empty() && PtInRect(&R.canal, p)) {
            OpenLink(rt->cfg.canal); DestroyWindow(h); return 0;
        }
        if (PontoFecha(R, p))    { DestroyWindow(h); return 0; }
        InvalidateRect(h, nullptr, FALSE);       // area morta: clique ali nao faz nada
        return 0;
    }
    // Arrastar a janela para um monitor com outra escala (ou mudar a escala com ela
    // aberta). O Windows manda o retangulo ja calculado em lParam: obedecemos a ele,
    // refazemos as fontes no DPI novo e repintamos. Sem isto a janela mudaria de tamanho
    // e o conteudo continuaria do tamanho do monitor antigo.
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
    case WM_DPICHANGED: {
        if (!rt) return 0;
        UINT novo = HIWORD(wp);
        if (UINT f = DpiForcado()) novo = f;      // no teste o DPI e o que mandamos
        if (novo < 96) novo = 96;
        rt->dpi = novo;
        rt->fonts.make(novo);
        const RECT* s = (const RECT*)lp;
        if (s) SetWindowPos(h, nullptr, s->left, s->top, s->right - s->left, s->bottom - s->top,
                            SWP_NOZORDER | SWP_NOACTIVATE);
        RoundCorners(h, novo);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
        if (wp == VK_RETURN || wp == VK_SPACE) { if (rt) OpenLink(rt->cfg.url); DestroyWindow(h); return 0; }
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

} // namespace detail
} // namespace ofertas

// -------------------------------------------------------------------------------------
//  A TELA: mostra o convite agora e so volta quando a janela fecha. Nao consulta politica
//  nenhuma - quem decide e quem chama, entao serve tanto pra abertura do app quanto pro
//  item fixo do painel.
//  Conteudo (url/titulo/msg): pode passar nullptr - o ofertas.ini vence, depois o
//  argumento, depois o padrao embutido.
//  `segundos` e IGNORADO (parametro mantido so pra nao quebrar quem ja chamava): a janela
//  nao fecha sozinha. Ela aparece, fica e sai quando a pessoa manda - por "Agora nao",
//  pelo X, por Esc, ou abrindo o link no botao/QR/endereco.
// -------------------------------------------------------------------------------------
// Opcoes de exibicao. Existem por dois pedidos do usuario:
//  - a tela do grupo DEVE sair na captura (e propaganda do canal, tem de aparecer no
//    video gravado/transmitido). So na fonte MONITOR sobreposto ela precisa continuar
//    escondida, senao a nossa propria captura a filma e ela entra no filtro.
//  - ela deve abrir no monitor onde esta o alvo, e dentro do alvo quando couber.
struct SplashOpcoes {
    bool esconderDaCaptura = true;   // comportamento antigo, preservado por padrao
    RECT area{ 0, 0, 0, 0 };         // onde centralizar; vazio = area de trabalho do primario
};

inline void ShowOfferSplash(HINSTANCE hi, const wchar_t* url, const wchar_t* titulo,
                            const wchar_t* msg, int segundos, const SplashOpcoes& op) {
    using namespace ofertas;
    using namespace ofertas::detail;

    (void)segundos;   // sem contagem regressiva; ver o comentario acima

    Config base;
    if (url && *url) {
        if (UrlEhWeb(url)) base.url = url;      // o argumento tambem passa pela peneira
        else               LogUrlRecusada(L"ShowOfferSplash", url);
    }
    if (titulo && *titulo) base.titulo   = Sanitiza(titulo, true);
    if (msg && *msg)       base.mensagem = Sanitiza(msg, true);

    Runtime rt;
    rt.cfg = LoadConfig(base);                  // conteudo: o ini vence o argumento
    if (!qr::EncodeText(rt.cfg.url.c_str(), qr::ECC_M, rt.code)) rt.code = qr::Code();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"DLSS5ForAllOfertas";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);   // WM_SETCURSOR troca pra maozinha onde clica
    RegisterClassExW(&wc);

    // Onde a janela nasce E em que escala. SPW/SPH sao unidades logicas: num monitor a
    // 150% a janela tem 1320x678 pixels, entao o "cabe na area do alvo?" so pode ser
    // decidido depois de saber o DPI do monitor daquela area.
    RECT wa = op.area;
    UINT dpi = 96;
    int W = SPW, H = SPH;
    for (int tentativa = 0; tentativa < 2; tentativa++) {
        if (wa.right - wa.left <= 0 || wa.bottom - wa.top <= 0)
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        POINT centro{ (wa.left + wa.right) / 2, (wa.top + wa.bottom) / 2 };
        dpi = DpiEfetivoPonto(centro);
        W = Esc(SPW, dpi); H = Esc(SPH, dpi);
        if (wa.right - wa.left >= W && wa.bottom - wa.top >= H) break;
        wa = RECT{ 0, 0, 0, 0 };      // nao coube na area do alvo: tenta a area de trabalho
    }
    int x = wa.left + (wa.right - wa.left - W) / 2;
    int y = wa.top + (wa.bottom - wa.top - H) / 2;
    if (x < wa.left) x = wa.left;     // tela menor que o cartao: prefere cortar embaixo/direita
    if (y < wa.top)  y = wa.top;

    // TITULO, nao string vazia. Esta e a PRIMEIRA coisa que aparece quando alguem da
    // duplo clique no exe: um cartao topmost com QR e um botao "QUERO AS OFERTAS", sem
    // nome, sem entrada na barra de tarefas. E a assinatura visual de um adware. O nome do
    // app no titulo (e no rodape do cartao, logo abaixo) e o que separa uma coisa da outra.
    HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                             wc.lpszClassName, L"DLSS5 For All", WS_POPUP, x, y, W, H,
                             nullptr, nullptr, hi, nullptr);
    if (!h) return;

    // O monitor onde a janela realmente nasceu pode nao ser o que calculamos (o Windows
    // pode ter mexido na posicao). Vale o DPI dela, nao o da nossa conta.
    UINT real = DpiEfetivoJanela(h);
    if (real != dpi) {
        dpi = real; W = Esc(SPW, dpi); H = Esc(SPH, dpi);
        SetWindowPos(h, nullptr, 0, 0, W, H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    rt.dpi = dpi;
    rt.fonts.make(dpi);
    rt.t0 = GetTickCount64();
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)&rt);
    SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
    if (op.esconderDaCaptura) HideFromCaptureLocal(h);   // ver SplashOpcoes
    RoundCorners(h, dpi);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(h);
    UpdateWindow(h);
    // O timer so faz a entrada suave. Se ele nao nascer (fila de timers do processo
    // cheia), o alpha ficaria em 0 pra sempre: uma janela invisivel, topmost, clicavel e
    // sem como fechar. Sem fade, entao, mas visivel.
    if (!SetTimer(h, 1, 40, nullptr))
        SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }

    KillTimer(h, 1);
    rt.fonts.free();
    UnregisterClassW(wc.lpszClassName, hi);
}

// assinatura antiga, mantida para nao quebrar quem ja chamava
inline void ShowOfferSplash(HINSTANCE hi, const wchar_t* url, const wchar_t* titulo,
                            const wchar_t* msg, int segundos) {
    ShowOfferSplash(hi, url, titulo, msg, segundos, SplashOpcoes{});
}
