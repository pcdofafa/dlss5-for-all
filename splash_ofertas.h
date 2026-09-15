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
//
//  MODO LAUNCHER (versao portable): a mesma tela com os botoes NVIDIA/AMD no lugar do
//  verde (LauncherInfo, ShowLauncherSplash) e, quando ha versao nova, um BANNER no topo
//  ("NOVA VERSAO x.y DISPONIVEL", botao BAIXAR E ATUALIZAR, caixa "atualizar
//  automaticamente") - ver BannerAtualizacao. Quem descobre a versao e o atualizacao.h;
//  aqui so se desenha e se devolve ESC_ATUALIZAR. No alto da coluna do QR ela tem tambem os
//  dois chips do IDIOMA ("Português" / "English"): e a primeira janela do pacote portable, e
//  quem nao le portugues precisa poder trocar antes de qualquer outra coisa. O clique troca
//  o texto da janela inteira na hora (ver TrocarIdioma) e avisa quem chamou pelo
//  LauncherInfo::aoTrocarIdioma, que e quem grava a escolha no ini.
//
//  VITRINE (so no launcher): a tela pode ter DUAS COLUNAS - a esquerda, a vitrine de ofertas
//  (uma pagina web hospedada por vitrine.h no retangulo que esta tela reserva); a direita,
//  tudo o que sempre existiu, desenhado com a origem do DC deslocada. Ver VitrineHost, VW,
//  RectVitrine, PaintColunaVitrine. Sem vitrine nada muda: colunaX = 0.
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
#include <atomic>       // o aviso de atualizacao chega de outra thread (ver BannerAtualizacao)
#include <functional>
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
// (convite direto do grupo no Telegram, definido pelo usuario em 2026-09-05; antes era
// grupo.pcdofafa.com.br/pg)
inline const wchar_t* UrlPadrao() { return L"https://t.me/+Tg-va1EvXX1lYTJh"; }

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
    std::wstring marca    = L"PC DO FAFA";
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
    std::wstring rodape   = TS(L"aponte a câmera do celular", L"point your phone camera");
    // A LINHA DE VALOR DO PRODUTO, no alto do launcher, logo abaixo do nome. Ela e do
    // PROGRAMA, nao do grupo - por isso tem padrao embutido e nao some nunca. Chave
    // slogan= / slogan_en= em [ofertas].
    // "on any game", e nao "in any game": o DLSS 5 e aplicado POR CIMA do que esta na tela,
    // e nao instalado dentro do jogo - "in" diria justamente o contrario do que o app faz.
    std::wstring slogan   = TS(L"DLSS 5 em qualquer jogo ou vídeo", L"DLSS 5 on any game or video");
    // "o que mudou": endereco das novidades desta versao. Vazio (padrao) = o link nem
    // aparece - prometer um link que nao existe e pior do que nao ter o link.
    std::wstring urlNovidades;
    // O que aparece ESCRITO embaixo do QR: o nome do grupo, nao o endereco do convite
    // (t.me/+Tg-va1Ev..., que e feio de mostrar e nao diz nada a quem le). O QR, o botao e a
    // vitrine ja levam ao destino; o endereco nao precisa aparecer em lugar nenhum da tela.
    std::wstring nomeGrupo = L"PC do Fafa Promoções";
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

// Le ofertas.ini (secao [ofertas]) ao lado do exe. Chaves: url, canal, url_novidades,
// mostrar, forcar_splash, titulo, mensagem, cta, botao, botao_fechar, rodape, slogan,
// nome_grupo, marca, submarca.
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
        else if (_wcsicmp(k.c_str(), L"url_novidades") == 0) {
            if (UrlEhWeb(v)) base.urlNovidades = v;
            else             LogUrlRecusada(L"ofertas.ini (url_novidades)", v);
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
        // "mostrar" ANTES do ramo de idioma. Ele ficava depois, e em portugues ChaveDoIdioma()
        // responde true para qualquer chave sem sufixo _en - "mostrar" entrava no ramo de texto,
        // nao casava com nada e era descartada em silencio: mostrar=0 so funcionava com o app
        // em ingles. (O comentario acima ja dizia que ela vale sempre; agora o codigo tambem.)
        else if (_wcsicmp(k.c_str(), L"mostrar") == 0) {
            // Qualquer coisa que nao seja "primeira*" nem uma negacao conhecida cai no
            // padrao: sempre. Valor escrito errado nunca deve calar o convite.
            if (detail::Falso(v))                               base.politica = POL_NUNCA;
            else if (_wcsnicmp(v.c_str(), L"primeira", 8) == 0) base.politica = POL_PRIMEIRA_VEZ;
            else                                                base.politica = POL_SEMPRE;
        }
        else if (detail::ChaveDoIdioma(k)) {
            const std::wstring kb = detail::SemSufixoEn(k);
            if      (_wcsicmp(kb.c_str(), L"titulo") == 0)       base.titulo   = texto(v, true);
            else if (_wcsicmp(kb.c_str(), L"mensagem") == 0)     base.mensagem = texto(v, true);
            else if (_wcsicmp(kb.c_str(), L"cta") == 0)          base.microcta = texto(v, true);
            else if (_wcsicmp(kb.c_str(), L"botao") == 0)        base.botao    = texto(v, false);
            else if (_wcsicmp(kb.c_str(), L"botao_fechar") == 0) base.botao2   = texto(v, false);
            else if (_wcsicmp(kb.c_str(), L"rodape") == 0)       base.rodape   = texto(v, false);
            else if (_wcsicmp(kb.c_str(), L"slogan") == 0)       base.slogan   = texto(v, false);
            else if (_wcsicmp(kb.c_str(), L"nome_grupo") == 0)   base.nomeGrupo = texto(v, false);
            else if (_wcsicmp(kb.c_str(), L"marca") == 0)        base.marca    = texto(v, false);
            else if (_wcsicmp(kb.c_str(), L"submarca") == 0)     base.submarca = texto(v, false);
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
// O ÂMBAR e a cor do BANNER DO CANAL, e so dele. A tela tem duas cores de destaque com
// papeis separados que nunca se cruzam: CIANO = fazer (o INICIAR, a unica acao do
// programa), AMBAR = oferta (o convite do grupo). Pintar o banner de ciano o faria
// disputar o olho com o botao de usar o app - que e exatamente o problema que este
// redesenho veio corrigir. Texto ESCURO por cima: #0b0f16 sobre #f59e0b da 8,6:1 de
// contraste (WCAG AAA); branco sobre o mesmo ambar daria 2,1:1 e reprovaria.
static const COLORREF C_AMB    = RGB(245, 158, 11);   // #f59e0b  o botao do banner
static const COLORREF C_AMB_L  = RGB(251, 191, 36);   // #fbbf24  com o mouse em cima
static const COLORREF C_AMB_D  = RGB(180, 83, 9);     // #b45309  a borda e o filete
static const COLORREF C_TEXT   = RGB(255, 255, 255);
static const COLORREF C_BODY   = RGB(209, 213, 219);  // #d1d5db
static const COLORREF C_SUP    = RGB(156, 163, 175);  // #9ca3af
static const COLORREF C_MUTE   = RGB(107, 114, 128);  // #6b7280

// TAMANHO DA JANELA EM UNIDADES LOGICAS (96 dpi). Todo numero deste arquivo - layout,
// alturas de fonte, raios de canto, margens - esta nesta unidade. O unico lugar onde
// eles viram pixel de verdade e a funcao Esc() abaixo, chamada com o DPI do monitor
// onde a janela esta. Num monitor a 100% o resultado e identico ao de antes.
static const int SPW = 880, SPH = 452;

// ---------------------------------------------------------------- modo LAUNCHER
// A mesma tela, quando e o launcher da versao portable que a abre. Em vez do botao verde
// "INICIAR PROGRAMA" ha DOIS: usar o DLSS 5 em placa NVIDIA (verde) e em placa AMD
// (vermelho). O que nao serve nesta maquina fica cinza, com o motivo escrito no proprio
// botao. Com os dois cinzas aparece um terceiro caminho, "abrir sem DLSS 5", com a
// explicacao do que o programa ainda faz. Quem decide o que e compativel e quem chama
// (o launcher enumera as placas); aqui so se desenha e se devolve a escolha.
enum Escolha { ESC_FECHOU = 0, ESC_NVIDIA, ESC_AMD, ESC_SEM_DLSS, ESC_LINK };

// ---------------------------------------------------------------- conteudo remoto
// A PUBLI DA TELA PODE VIR DA WEB (ofertas.json, ver atualizacao.h): o autor troca titulo,
// mensagem, chamada, botao, link do grupo/canal e um CUPOM sem lancar versao nova. Os
// campos presentes SOBREPOEM o ofertas.ini local ao montar a Config (o ini continua sendo
// a base; o padrao embutido, a ultima linha). Quem preenche isto (atualizacao.h) ja
// escolheu o idioma, saneou os textos (Sanitiza) e validou os enderecos (UrlEhWeb); a tela
// so aplica. Campo vazio = nao sobrepoe.
//
// Com `cupom`, o selo "aponta a camera..." vira "CUPOM XXX - texto" (mesma bordinha ciano) e
// passa a ser clicavel: copia o cupom para a area de transferencia e abre a url.
// O TIPO DO ITEM. O grupo publica oferta de peca, mas tambem cupom e recado que nao e nem uma
// coisa nem outra - e "a mais recente" pode ser qualquer um dos tres. A faixa muda o rotulo e o
// texto de apoio conforme o tipo, senao ela diria "o preco muda rapido" embaixo de um aviso que
// nao tem preco nenhum.
enum TipoItem { ITEM_OFERTA = 0, ITEM_CUPOM = 1, ITEM_AVISO = 2 };
struct OfertaRemota {
    std::wstring titulo, mensagem, cta, botao, url, canal, cupom, cupomTexto;
    // `tipo` vem do ofertas.json quando o servidor manda; quando nao manda, e DEDUZIDO (ver
    // TipoDeduzido). `publicado` e o instante em que o item foi ao ar, em segundos unix - so
    // com ele a tela escreve "ha N min"; sem ele, ela cala a boca sobre tempo em vez de
    // inventar um.
    int tipo = -1;
    long long publicado = 0;
    bool Vazio() const { return titulo.empty() && mensagem.empty() && cta.empty() && botao.empty() && url.empty() && canal.empty() && cupom.empty(); }
    // Sem o campo `tipo`: tem codigo de cupom -> cupom; o titulo fala em dinheiro -> oferta;
    // senao -> aviso. E uma heuristica, e por isso o campo explicito ganha dela sempre.
    int TipoEfetivo() const {
        if (tipo >= ITEM_OFERTA && tipo <= ITEM_AVISO) return tipo;
        if (!cupom.empty()) return ITEM_CUPOM;
        // "R$", "US$" ou "%" seguidos de digito em algum ponto do titulo
        for (size_t i = 0; i + 1 < titulo.size(); i++)
            if ((titulo[i] == L'$' || titulo[i] == L'%') && iswdigit(titulo[i + 1])) return ITEM_OFERTA;
        for (size_t i = 0; i + 2 < titulo.size(); i++)
            if (titulo[i] == L'$' && titulo[i + 1] == L' ' && iswdigit(titulo[i + 2])) return ITEM_OFERTA;
        return ITEM_AVISO;
    }
};
// O RESUMO DE USO (so na copia do autor: [atualizacao] relatorio_url= no ofertas.ini).
// Numeros que o servidor soma; a tela so desenha o cartao. Ver atualizacao::LerResumo.
struct ResumoUso {
    bool ok = false;
    std::wstring gerado;
    long long hojeAb = 0, hojeUs = 0, d7Ab = 0, d7Us = 0, d30Ab = 0, d30Us = 0;
    std::vector<std::pair<std::wstring, long long>> porDia;      // data -> usuarios (ate 14, do mais antigo ao mais novo)
    std::vector<std::pair<std::wstring, long long>> porDiaAb;    // data -> aberturas
    std::vector<std::pair<std::wstring, long long>> porVersao, porModulo, porBotaoHoje, porBotao7;
    std::wstring ultimoRegistro;   // do relatorio-uso.txt, quando a rede falhou
};
// Como a rede responde DEPOIS de a janela poder estar aberta, o combinado (para o conteudo
// remoto e para o resumo) e: quem busca escreve o resultado, poe *pronto=1 (ou 2 = nada) e
// manda WM_OFERTAS_ATUALIZA para a janela publicada em g_janelaLauncher; a janela, ao
// nascer, publica o HWND e depois consulta `pronto`. Com atomicos seq_cst um dos dois lados
// sempre ve o outro. Nao usar FindWindow: a classe e a mesma da tela do grupo aberta pelo
// app, e a mensagem poderia cair na janela de OUTRO processo.
// ---------------------------------------------------------------- a VITRINE (coluna da esquerda)
// No launcher a tela pode ganhar uma SEGUNDA COLUNA, a esquerda da de sempre: a vitrine de
// ofertas, que e uma pagina web ({url_base}/vitrine) mostrada dentro da janela por um motor
// web (WebView2, ver vitrine.h). Esta tela NAO sabe nada de web: ela reserva o retangulo,
// desenha a faixa de titulo ("OFERTAS DE AGORA" + selo "atualiza sozinho") e o aviso de
// "carregando..." / "sem conexao", e avisa o motor quando a janela nasce, muda de DPI e
// morre. Quem decide SE a coluna existe e quem chama (launcher.cpp: url_base, chave
// vitrine=, runtime presente, rede respondeu); a unica decisao daqui e a do MONITOR
// ESTREITO: se as duas colunas nao cabem na area de trabalho, a coluna sai e a tela e a de
// uma coluna, identica a de antes (motivoOff diz isso para o log).
//
// Por que a vitrine desliga o fade de entrada: a janela deixa de ser WS_EX_LAYERED quando
// tem a coluna, porque um motor web hospedado como janela filha nao compõe direito dentro
// de uma janela em camadas (fica preto ou nao aparece). Sem camada nao ha alpha, entao a
// tela nasce opaca de uma vez. O resto (topmost, cantos, hit-test, teclas) e igual.
struct VitrineHost {
    virtual ~VitrineHost() {}
    virtual void Criar(HWND pai, RECT area, double zoom) = 0;   // a janela nasceu: hospede a pagina aqui
    virtual void Mover(RECT area, double zoom) = 0;             // WM_DPICHANGED: novo retangulo/escala
    virtual void Fechar() = 0;                                  // a janela vai morrer
    // A pagina da vitrine recebe o idioma na QUERY (l=pt|en), montada uma vez na abertura.
    // Sem isto, quem trocasse o idioma nos chips via o launcher inteiro mudar e a coluna da
    // vitrine continuar na lingua anterior - o problema que o seletor existe para resolver,
    // sobrevivendo dentro dele. Renavegar e o unico caminho: nao ha como pedir a uma pagina
    // de terceiro que troque de lingua. O padrao NAO faz nada, para que um host sem vitrine
    // (ou uma implementacao futura) nao seja obrigado a implementar.
    virtual void TrocarIdioma(const wchar_t* /*codigo pt|en*/) {}
    // A GAVETA FECHOU / ABRIU. A pagina continua carregada (recriar o motor a cada clique
    // custa segundos - ver o relatorio da vitrine: duas aberturas seguidas da mesma pasta de
    // cache demoram 4-6 s); o que muda e so a visibilidade da janela filha. O padrao nao faz
    // nada, para um host sem motor nao ser obrigado a implementar.
    virtual void Mostrar(bool /*visivel*/) {}
    std::atomic<int> estado{ 0 };   // 0 carregando, 1 pagina na tela, 2 sem conexao (aviso no lugar)
    std::wstring motivoOff;         // preenchido por quem desligou a coluna (esta tela: monitor estreito)
};
// largura logica (96 dpi) da coluna da vitrine, separador incluso: a pagina ocupa 16..436
// (420 de largura, a medida para a qual a pagina foi desenhada) e a coluna de sempre comeca em 452
static const int VW = 452;

struct LauncherInfo {
    bool nvidiaOk = false, amdOk = false;
    std::wstring nvidiaNota, amdNota;   // por que esta cinza (vazio quando esta aceso)
    // A PLACA FOI ENCONTRADA NESTA MAQUINA? E diferente de "serve": uma Radeon RX 9060 com
    // driver velho esta DETECTADA e nao esta OK - ela merece aparecer no seletor, desligada,
    // com o motivo do lado ("atualize o driver"). Uma placa que nao existe aqui nao aparece:
    // o seletor mostra o que a deteccao liberou, e quem tem uma opcao so nem ve o seletor.
    bool nvidiaDetectada = false, amdDetectada = false;
    // A LINHA DE ESTADO DO HARDWARE, pronta (quem detecta e o launcher; aqui so se desenha):
    // "RTX 4080 detectada · pronta", "Radeon RX 9060 XT · módulo AMD (beta)", "nenhuma placa
    // compatível — o app abre sem DLSS 5". `hwSinal`: 1 verde (pronta), 2 amarelo (beta/com
    // ressalva), 3 cinza (nenhuma) - e so a bolinha na frente do texto.
    std::wstring estadoHardware;
    int hwSinal = 0;
    std::wstring versaoApp;             // "1.2": aparece ao lado do nome, no alto
    // a vitrine (coluna da esquerda). Nulo = tela de uma coluna, a de sempre.
    VitrineHost* vitrine = nullptr;
    // A GAVETA: ela nasce aberta? (primeiro uso, ou a batida de N aberturas, ou a escolha
    // guardada - quem decide isso e o launcher, que le e grava o ini da raiz.) `aoTrocarGaveta`
    // recebe o novo estado a cada clique na aba, para ser gravado.
    bool gavetaAberta = false;
    std::atomic<int>* vitrineNovas = nullptr;   // quantas ofertas novas (0/-1 = so "Ofertas")
    std::function<void(bool)> aoTrocarGaveta;
    // A FAIXA DO CANAL: a pessoa a deixou visivel? (o launcher le e grava o ini da raiz).
    // `aoTrocarFaixa` recebe o novo estado a cada clique em "ocultar" / "reativar".
    bool faixaVisivel = true;
    std::function<void(bool)> aoTrocarFaixa;
    // versao nova ja verificada: a pilula discreta ao lado da versao e a linha de status. A
    // caixa de perguntar continua sendo do launcher, e SO depois do INICIAR.
    std::atomic<int>* versaoPronta = nullptr;   // 0 verificando, 1 ha versao nova, 2 nada
    const std::wstring* versaoNova = nullptr;   // so vale quando *versaoPronta == 1
    // A FAIXA QUE SE ATUALIZA SOZINHA: quem chama roda um polling e, toda vez que o item mais
    // recente do grupo MUDA, escreve o novo em `remoto`, incrementa `geracaoRemoto` e posta
    // WM_OFERTAS_ATUALIZA. A tela compara a geracao com a que desenhou e anima a troca. Nulo =
    // sem polling: a publi vale como veio na abertura, que e o comportamento de sempre.
    std::atomic<int>* geracaoRemoto = nullptr;
    // conteudo remoto: o cache (aplicado ao nascer, sincrono) e o fresco (quando chegar)
    const OfertaRemota* remotoCache = nullptr;
    std::atomic<int>* remotoPronto = nullptr;   // 0 buscando, 1 ha conteudo em *remoto, 2 nada
    const OfertaRemota* remoto = nullptr;
    // cartao "Relatorio de uso" (so quando relatorio_url existe)
    bool cartao = false;
    std::atomic<int>* resumoPronto = nullptr;   // 0 buscando, 1 ok em *resumo, 2 indisponivel
    const ResumoUso* resumo = nullptr;
    std::wstring notaVersao;            // assinatura no pe da tela ("DLSS5 For All 1.2 · atualizado agora")
    // A pessoa trocou o idioma nos chips do alto. A tela ja trocou o texto dela; isto e para
    // quem chama GRAVAR a escolha onde o app a le depois (o launcher escreve nos
    // dlss5forall.ini dos modulos). Nulo = ninguem grava, a troca vale so nesta tela.
    // Esta tela nao conhece caminho de ini nenhum de proposito: ela e desenho, e o launcher
    // e quem sabe onde estao as pastas bin.
    std::function<void(Lingua)> aoTrocarIdioma;
};
inline std::atomic<HWND> g_janelaLauncher{ nullptr };
constexpr UINT WM_OFERTAS_ATUALIZA = WM_APP + 0x41;
// o cartao do relatorio acrescenta esta altura EMBAIXO (so na copia do autor)
inline int AlturaCartao() { return 190; }

// 96 dpi -> pixels desta tela. Mesmo idioma do painel (main.cpp:1865).
inline int Esc(int v, UINT dpi) { return MulDiv(v, (int)(dpi ? dpi : 96), 96); }
// o retangulo (em PIXEL) onde a pagina da vitrine entra: dentro dos cantos arredondados e
// abaixo da faixa de titulo (ver VitrineHost / VW)
inline RECT RectVitrine(UINT dpi, int hPx) { return RECT{ Esc(16, dpi), Esc(84, dpi), Esc(VW - 16, dpi), hPx - Esc(16, dpi) }; }

struct Fonts {
    HFONT marca = nullptr, sub = nullptr, title = nullptr, msg = nullptr, cta = nullptr,
          micro = nullptr, addr = nullptr, small_ = nullptr, tiny = nullptr,
          close_ = nullptr, btn = nullptr, nota = nullptr, produto = nullptr;
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
        produto = f(-34, FW_BOLD);      // "DLSS 5 FOR ALL" no alto do launcher: o nome manda
        btn     = f(-14, FW_BOLD);      // os botoes de modo do launcher (texto mais longo)
        nota    = f(-11, FW_NORMAL);    // o motivo dentro do botao cinza
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
        for (HFONT* p : { &marca, &sub, &title, &msg, &cta, &micro, &addr, &small_, &tiny, &close_, &btn, &nota, &produto })
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
    const LauncherInfo* li = nullptr;   // nulo = tela classica (botao verde unico)
    bool hoverLingPt = false, hoverLingEn = false;   // os chips do idioma
    // ---- launcher (ver LayoutL): o que esta sob o mouse e o que a pessoa escolheu
    bool hoverIniciar = false, hoverGrupo = false, hoverCanalRod = false,
         hoverAba = false, hoverNovidades = false, hoverPopBotao = false, hoverPopFechar = false,
         hoverPopQr = false;
    bool hoverBanner = false, hoverBanBotao = false, hoverBanOcultar = false;   // a faixa do canal
    bool remoto = false;          // o texto da faixa veio do servidor (publi de hoje)?
    int  tipoItem = ITEM_OFERTA;  // oferta / cupom / aviso: manda no rotulo e no texto de apoio
    long long publicado = 0;      // quando o item foi ao ar (0 = a tela nao fala de tempo)
    double animFaixa = 0.0;       // 1 -> 0 durante a troca de oferta (ver AnimarFaixa)
    HICON icone = nullptr;        // o icone do app no cabecalho (nulo = nao desenha)
    int  selecao = 0;             // indice na lista de opcoes da FormaL (nao o codigo da opcao)
    bool gaveta = false;          // a gaveta da vitrine esta aberta
    bool popover = false;         // o cartao do QR esta por cima da tela
    int  versaoEstado = 2;        // 0 verificando, 1 ha versao nova, 2 e a mais nova
    std::wstring versaoNova;
    int  vitrineNovas = -1;       // quantas ofertas novas na aba (-1/0 = so "Ofertas")
    // conteudo remoto e cartao do relatorio (so no launcher)
    std::wstring cupom, cupomTexto;     // vazio = selo normal ("aponta a camera")
    bool hoverCupom = false;
    bool cartao = false;                // desenha o cartao "Relatorio de uso" embaixo
    int  resumoEstado = 0;              // 0 aguardando, 1 ok (resumo valido), 2 indisponivel
    const ResumoUso* resumo = nullptr;
    // a vitrine: onde a coluna de sempre comeca (0 = sem vitrine) e o estado da pagina
    int colunaX = 0;                    // em PIXEL
    int vitrineEstado = 0;              // ver VitrineHost::estado
};
// Retangulos calculados num lugar so, pra desenho e clique nunca discordarem. Entram em
// unidades logicas e saem em PIXEL: quem desenha e quem testa o clique usam a mesma
// funcao com o mesmo dpi, entao nao ha como um escalar e o outro nao.
//
// ISTO E A TELA CLASSICA (o convite do grupo que o main.cpp abre pelo painel). O LAUNCHER
// tem o layout dele, logo abaixo (RectsL / LayoutL) - eram o mesmo desenho e deixaram de
// ser no dia em que o launcher passou a ser a tela do PRODUTO e nao a da publi.
struct Rects {
    RECT moldura, card, addr, cta, canal, sec, close_, rodapeQr, assin, selo;
};
inline Rects Layout(int w, int h, UINT dpi) {
    auto E = [dpi](int v) { return Esc(v, dpi); };
    auto R4 = [&](int l, int t, int r, int b) { return RECT{ E(l), E(t), E(r), E(b) }; };
    Rects r{};
    r.moldura = R4(24, 24, 372, 372);      // cartao #1f2937 em volta do QR
    r.card    = R4(32, 32, 364, 364);      // area branca: 332 unidades de lado
    r.addr    = R4(24, 380, 372, 408);     // o nome do grupo escrito, tambem clicavel
    r.rodapeQr = R4(24, 408, 372, 428);
    // TRES DESTINOS, TRES BOTOES, POSICAO FIXA. A ordem NAO muda de uma abertura para
    // outra. Houve a ideia de embaralhar os botoes para colher o clique de quem nao le; foi
    // recusada. Quem clica sem querer nao vira inscrito, vira gente irritada - e quem abre
    // este app todo dia e exatamente o publico do canal.
    r.selo  = R4(396, 262, 696, 290);      // o selo "aponta a camera..." (largura real vem do texto)
    r.cta   = R4(396, 252, 696, 298);      // grupo de ofertas (primario)
    r.canal = R4(396, 306, 696, 350);      // apoiar o canal (secundario forte)
    r.sec   = R4(396, 358, 696, 406);      // INICIAR PROGRAMA (verde)
    r.assin = R4(396, 404, w - 32, 424);
    r.close_ = { w - E(38), E(14), w - E(14), E(38) };   // ancorado na borda direita real
    (void)h;
    return r;
}

// =====================================================================================
//  O LAUNCHER: a tela do PRODUTO
//
//  Antes, o launcher era a tela do convite com dois botoes de placa enfiados embaixo: quem
//  baixava o "DLSS 5 For All" via primeiro uma manchete sobre peca de PC barata, um QR do
//  tamanho de meia janela e os botoes "QUERO AS OFERTAS" / "APOIAR O CANAL" ACIMA dos
//  botoes de usar o programa. O nome do produto aparecia numa assinatura de 12 px no rodape.
//
//  A HIERARQUIA AGORA E A DO PRODUTO, e o apoio ao canal e um rodape elegante:
//    1. no alto, "DLSS 5 FOR ALL", a versao, a linha de valor e o estado da placa detectada;
//    2. UMA acao principal, o botao INICIAR em ciano - a escolha de placa virou um seletor
//       discreto embaixo dele, que so aparece para quem tem mais de uma opcao;
//    3. "continuar de onde parou", com a ultima sessao e um "Repetir";
//    4. uma linha de status (versao/novidades);
//    5. o rodape: "Feito pelo PC do Fafa · Entrar no grupo · Apoiar o canal". O QR e o texto
//       de venda do ofertas.ini (titulo/mensagem/cta) sairam da tela principal e vivem no
//       POPOVER que o "Entrar no grupo" abre;
//    6. a vitrine virou GAVETA: fechada, e uma aba na lateral; aberta, a janela cresce para a
//       esquerda e a pagina aparece no mesmo retangulo de sempre.
//  Uma cor de destaque so (o ciano do INICIAR). A placa aparece como uma bolinha no chip do
//  seletor, e nao como um botao inteiro verde ou vermelho.
// =====================================================================================
// Largura logica da coluna do produto (a janela inteira, com a gaveta fechada). 620 e a
// medida em que o RODAPE cabe numa linha so com tudo o que ele pode ter: a assinatura, os
// dois botoes e ainda a pilula do cupom quando a publi remota traz um.
static const int SPWL = 620;
static const int PADL = 32;    // respiro lateral

// ------------------------------------------------------------------ AS TRES VARIACOES
// O launcher tem TRES desenhos possiveis, e eles diferem em UMA coisa so: quanto espaco o
// convite do grupo ocupa na tela principal. O resto - o nome do produto no alto, o INICIAR
// como unica acao, o seletor de placa, o "continuar de onde parou", os chips do idioma - e
// igual nos tres, porque e a parte que ja esta decidida.
//
//   0  GAVETA      o convite cabe no rodape (uma linha) e numa aba lateral; o QR e o texto
//                  de venda vivem no popover. A tela e 100% produto.
//   1  FAIXA       entra uma FAIXA larga do canal entre o produto e o rodape: rotulo de
//                  secao, QR pequeno, titulo, mensagem e botao ambar. E o banner de
//                  destaque no estilo GOG/Steam, dentro de uma tela que continua sendo do
//                  programa - o INICIAR permanece o maior objeto e a unica coisa ciano.
//   2  VITRINE     a faixa do item 1 MAIS a coluna de ofertas sempre aberta ao lado (sem
//                  gaveta, sem aba). Sem rede ou sem WebView2 cai sozinha na variacao 1.
//
// A escolha e do USUARIO do projeto, nao do codigo: enquanto ele nao escolher, o padrao e a
// 0 e a variavel DLSS5_LAUNCHER_LAYOUT (com DLSS5_DIAG=1, como toda chave de teste desta
// base) troca a variacao para as fotos de comparacao.
// O ICONE DO APP no cabecalho do launcher, em unidades logicas. E a capivara meio pixel-art
// meio realista com a moldura azul neon (marca\DLSS5ForAll.ico, recurso 1 do exe): a propria
// imagem conta o que o programa faz - de um lado a imagem crua, do outro a imagem que o DLSS
// devolve -, entao ela vale mais no alto da tela do que na barra de tarefas sozinha. 72 e o
// tamanho que cabe na altura do bloco de cabecalho (nome + linha de valor + estado da placa)
// sem empurrar nada e sem roubar da linha do nome a largura de que a pilula de atualizacao
// precisa. A moldura neon do desenho e a mesma familia do ciano da tela (#22d3ee), que ja
// era a paleta do site - nao houve o que trocar de cor.
static const int ICONE_L = 72;
enum LayoutLauncher { LAY_GAVETA = 0, LAY_FAIXA = 1, LAY_VITRINE = 2 };
// A ESCOLHA FOI FEITA em 11/09/2026: o padrao e a FAIXA. As outras duas continuam no codigo
// porque sao o material de comparacao do relatorio - e porque a VITRINE cai sozinha na FAIXA
// quando nao ha rede, entao ela nunca foi um caminho separado de verdade.
static const int LAY_PADRAO = LAY_FAIXA;
inline int LayoutEscolhido() {
    wchar_t d[8]{}, b[8]{};
    if (!GetEnvironmentVariableW(L"DLSS5_DIAG", d, 8) || d[0] != L'1') return LAY_PADRAO;
    if (!GetEnvironmentVariableW(L"DLSS5_LAUNCHER_LAYOUT", b, 8)) return LAY_PADRAO;
    const int v = _wtoi(b);
    return (v >= LAY_GAVETA && v <= LAY_VITRINE) ? v : LAY_PADRAO;
}

// Que caminho esta maquina tem, e o que mais a tela precisa mostrar.
//
// O LAUNCHER NAO PERGUNTA MAIS QUAL PLACA USAR (11/09/2026). Ele decide sozinho, em ordem
// fixa: NVIDIA pronta -> NVIDIA; senao AMD pronta -> modulo AMD; senao sem DLSS 5, que passou
// a ser uma CONSEQUENCIA e nunca uma opcao oferecida. A escolha manual existe - e sempre
// existiu - na janela seguinte, o seletor do proprio app; pedir a mesma decisao duas vezes,
// sendo que aqui ela e feita antes de a pessoa ver qualquer coisa, era so um degrau a mais
// antes do INICIAR. O que fica no lugar e a LINHA DE ESTADO, que informa sem pedir nada
// ("RTX 4080 detectada · pronta"): ela responde "o meu computador serve?", que e a duvida
// real de quem abre o programa pela primeira vez.
//
// `opc`/`n` continuam existindo porque e deles que sai QUAL modulo o INICIAR abre; o que
// morreu foi o desenho dos chips.
struct FormaL {
    int  opc[3] = { -1, -1, -1 };   // 0 nvidia, 1 amd, 2 sem DLSS 5
    int  n = 0;
    bool nota = false, explica = false, relatorio = false;
    bool notaModo = false;          // "Modo AMD em desenvolvimento: funciona, mas ainda pesa mais."
    bool faixa = false;             // a faixa do canal, aberta (variacoes FAIXA e VITRINE)
    bool faixaOculta = false;       // a faixa existe mas a pessoa a escondeu: fica o "reativar"
    bool ofertas = true;            // ver OfertasValem(): em ingles a faixa chama o canal
    bool icone = false;             // ha icone do app para o cabecalho? (ver ICONE_L)
    int  layout = LAY_GAVETA;
};
// AS OFERTAS SO VALEM EM PORTUGUES. O grupo e brasileiro: os precos sao em real, as lojas
// entregam no Brasil e os cupons so funcionam aqui. Mostrar isso a quem trocou a tela para
// ingles seria oferecer uma coisa que a pessoa nao tem como comprar - e ocupar com ela o
// espaco de algo que ela PODE usar. Em ingles a faixa vira o convite para o canal do YouTube,
// e a coluna de ofertas e a aba lateral nem aparecem.
inline bool OfertasValem() { return g_lingua == LINGUA_PT; }

inline FormaL FormaDe(const LauncherInfo* li) {
    FormaL f{};
    f.layout = LayoutEscolhido();
    f.faixa  = f.layout != LAY_GAVETA;
    f.ofertas = OfertasValem();
    if (!li) return f;
    // A PESSOA PODE DESLIGAR A FAIXA, e desligada ela fica. E o que separa um convite de um
    // anuncio: a pesquisa (NN/g, padroes enganosos) chama de "Nagging" justamente o convite
    // que volta depois de recusado, e Steam e Battle.net sao os dois launchers grandes que
    // documentam uma chave dessas. No lugar da faixa fica uma linha discreta com "reativar" -
    // esconder nao pode virar um caminho sem volta.
    if (f.faixa && !li->faixaVisivel) { f.faixa = false; f.faixaOculta = true; }
    // A ORDEM DA LISTA E A ORDEM DA DECISAO: a primeira acesa e a que o INICIAR abre.
    auto add = [&](int o) { if (f.n < 3) f.opc[f.n++] = o; };
    if (li->nvidiaDetectada) add(0);
    if (li->amdDetectada)    add(1);
    if (!li->nvidiaOk && !li->amdOk) add(2);
    // A ajuda do modo AMD aparece quando o caminho AMD e o que vai valer - ou seja, quando a
    // NVIDIA nao esta pronta e a AMD esta. Ninguem precisa ser avisado de que o modo AMD pesa
    // mais numa maquina que vai rodar pela NVIDIA.
    f.notaModo = !li->nvidiaOk && li->amdOk;
    f.explica = !li->nvidiaOk && !li->amdOk;
    f.relatorio = li->cartao;
    return f;
}
inline const wchar_t* NomeOpcao(int o) {
    return o == 0 ? L"NVIDIA" : o == 1 ? L"AMD" : T(L"Sem DLSS 5", L"No DLSS 5");
}
inline bool OpcaoAcesa(const LauncherInfo* li, int o) {
    return o == 0 ? li->nvidiaOk : o == 1 ? li->amdOk : true;
}
// ------------------------------------------------------------------ O TEXTO DA FAIXA
// Tudo o que a faixa escreve sai DAQUI, num lugar so: o rotulo, o titulo, a linha de apoio e o
// botao. Sao quatro casos - tres em portugues (oferta, cupom, aviso) e um em ingles (o convite
// para o canal) -, e junta-los numa funcao e o que garante que o rotulo nunca prometa uma coisa
// que o texto de baixo desminta.
//
// A REGRA DE OURO e nao inventar: o titulo e o que o servidor mandou, sem moldura nossa em
// volta; o "ha N min" so aparece quando existe carimbo de publicacao; e nenhuma frase fixa fala
// em preco, frete ou peca, porque o item mais recente do grupo pode ser um cupom de mercado ou
// um recado que nao tem preco nenhum.
struct TextoFaixa {
    std::wstring rotulo, titulo, apoio, botao;
    bool ehCanal = false;          // ingles: a faixa chama o canal, nao o grupo
};
// PARA ONDE A FAIXA LEVA: o grupo em portugues, o canal em ingles. Sem `canal=` no ofertas.ini
// nao ha para onde mandar quem esta em ingles, e a faixa volta a apontar para o grupo - melhor
// um destino em portugues do que um botao que nao faz nada.
inline const std::wstring& AlvoDaFaixa(const Config& C) {
    return (!OfertasValem() && !C.canal.empty()) ? C.canal : C.url;
}
// "ha 3 min", "ha 2 h", "ha 4 dias" - ou vazio quando nao ha carimbo. Vazio e uma resposta
// legitima: melhor nao falar de tempo do que chutar um.
inline std::wstring HaQuantoTempo(long long publicado) {
    if (publicado <= 0) return L"";
    // segundos unix a partir do relogio do sistema, sem <ctime>: o FILETIME conta em 100 ns
    // desde 1601, e 11644473600 e a distancia entre 1601 e 1970 em segundos
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{}; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    const long long agora = (long long)(u.QuadPart / 10000000ULL) - 11644473600LL;
    long long s = agora - publicado;
    if (s < 0) s = 0;                                  // relogio da maquina atrasado
    if (s < 120)    return T(L"agora", L"just now");
    if (s < 3600)   return TS(L"há ", L"") + std::to_wstring(s / 60) + TS(L" min", L" min ago");
    if (s < 86400)  return TS(L"há ", L"") + std::to_wstring(s / 3600) + TS(L" h", L" h ago");
    const long long d = s / 86400;
    if (d == 1)     return T(L"ontem", L"yesterday");
    return TS(L"há ", L"") + std::to_wstring(d) + TS(L" dias", L" days ago");
}
inline TextoFaixa TextoDaFaixa(const Config& C, int tipo, const std::wstring& cupom,
                               long long publicado) {
    TextoFaixa t{};
    if (!OfertasValem()) {
        // SEM OFERTA, CUPOM OU PRECO - quem esta fora do Brasil nao compra nas lojas do
        // grupo. O que sobra e o que atravessa a fronteira: o canal.
        // 14/09/2026: o texto era o MESMO ingles nos dois lados do T() - quem escolhe
        // portugues fora do Brasil via ingles aqui mesmo assim. Corrigido: PT de verdade
        // dos dois lados agora, como em qualquer outro texto do app.
        t.ehCanal = true;
        t.rotulo  = T(L"DO CRIADOR", L"FROM THE CREATOR");
        t.titulo  = L"PC do Fafa on YouTube";
        t.apoio   = T(L"Testes, builds e DLSS 5 na prática — inscreva-se para acompanhar o projeto.",
                      L"Tests, builds and DLSS 5 in practice — subscribe to follow the project.");
        t.botao   = T(L"Abrir o canal", L"Open the channel");
        return t;
    }
    const std::wstring quando = HaQuantoTempo(publicado);
    t.titulo = C.titulo;
    t.botao  = C.botao;
    if (tipo == ITEM_CUPOM) {
        t.rotulo = T(L"CUPOM NOVO", L"NEW COUPON");
        t.apoio  = T(L"Cupom novo no grupo: vale enquanto durar.",
                     L"New coupon in the group: good while it lasts.");
    } else if (tipo == ITEM_AVISO) {
        t.rotulo = T(L"NO GRUPO AGORA", L"IN THE GROUP NOW");
        t.apoio  = quando.empty() ? std::wstring(T(L"Novidade no grupo.", L"Something new in the group."))
                                  : (TS(L"Novidade no grupo ", L"Something new in the group ") + quando + L".");
    } else {
        t.rotulo = T(L"OFERTA DE HOJE", L"TODAY'S DEAL");
        const std::wstring fixo = T(L"O preço muda rápido — o grupo recebe primeiro.",
                                    L"Prices move fast — the group hears first.");
        t.apoio  = quando.empty() ? fixo
                                  : (TS(L"Publicada ", L"Posted ") + quando + TS(L" no grupo. ", L" in the group. ") + fixo);
    }
    return t;
}

// O rotulo do botao do grupo no rodape. Curto quando a pilula do cupom divide a linha com
// ele - ver a nota no rodape de LayoutL.
inline const wchar_t* RotuloGrupo(bool curto) {
    return curto ? T(L"Entrar no grupo", L"Join the group")
                 : T(L"Entrar no grupo de ofertas", L"Join the deals group");
}
inline std::wstring NotaOpcao(const LauncherInfo* li, int o) {
    return o == 0 ? li->nvidiaNota : o == 1 ? li->amdNota : std::wstring();
}

struct RectsL {
    RECT lingPt{}, lingEn{}, close_{};
    RECT icone{};                  // o icone do app no cabecalho (vazio quando nao ha)
    RECT titulo{}, versao{}, pilula{}, slogan{}, hardware{};
    RECT notaModo{};               // a ajuda curta do modo AMD, abaixo da linha de estado
    RECT iniciar{};
    RECT explica{};
    RECT status{}, novidades{};
    RECT relatorio{};
    RECT sep{}, marca{}, grupo{}, canal{}, cupom{};
    RECT aba{};
    // A FAIXA DO CANAL (variacoes 1 e 2). `banner` e o bloco inteiro - clicar em qualquer
    // ponto dele abre o grupo, que e o alvo grande da lei de Fitts; `bannerBotao` e so o
    // botao ambar. Nas variacoes sem faixa todos estes ficam vazios, e PtInRect num
    // retangulo vazio e sempre falso: nao ha um `if` a proteger em lugar nenhum.
    RECT banner{}, banRotulo{}, banQr{}, banTitulo{}, banMsg{}, bannerBotao{}, banCupom{};
    // "ocultar" ao lado do rotulo e, quando a faixa esta oculta, o "reativar" que fica no
    // lugar dela. So um dos dois existe de cada vez.
    RECT banOcultar{}, banReativar{};
    // o popover do QR (so quando esta aberto; fora dele fica tudo vazio e PtInRect e falso)
    RECT popFundo{}, popCard{}, popTitulo{}, popQr{}, popNome{}, popRodape{}, popMsg{}, popBotao{}, popFechar{};
    int  opc[3] = { -1, -1, -1 };
    int  nOpc = 0;                 // quantos caminhos a maquina tem (o INICIAR abre o 1o aceso)
    int  alturaPx = 0;             // altura da janela inteira, em PIXEL (o layout ja e em pixel)
};

// Mede um texto numa fonte e devolve a LARGURA EM PIXEL. Com dc nulo (nao ha DC a mao)
// devolve uma estimativa grosseira - ela so decide largura de botao do rodape, e o desenho
// e o clique usam SEMPRE o mesmo numero, entao no maximo a pilula fica folgada.
inline SIZE MedeTexto(HDC dc, HFONT f, const wchar_t* s, int spacing);
inline int LarguraTexto(HDC dc, HFONT f, const wchar_t* s, int alturaLogica, UINT dpi) {
    if (dc && f) return MedeTexto(dc, f, s, 0).cx;
    return Esc((int)wcslen(s) * alturaLogica * 6 / 10, dpi);
}

// `w` e a largura da COLUNA DO PRODUTO em pixel (a janela sem a gaveta); `oxPx` e onde ela
// comeca (0 com a gaveta fechada, Esc(VW) com ela aberta). Os retangulos saem ja deslocados.
// `dc`/`F` servem so para medir texto (botoes do rodape, chips); podem ser nulos.
// `botaoFaixa` e o rotulo do botao ambar da faixa do canal, que vem do ofertas.ini e por isso
// nao pode ser adivinhado aqui (a largura do botao sai do texto). Nulo = o padrao embutido.
inline RectsL LayoutL(int w, UINT dpi, const LauncherInfo* li, const FormaL& f,
                      int oxPx, bool popover, bool temCupom, HDC dc, Fonts* F,
                      const wchar_t* botaoFaixa = nullptr) {
    auto E = [dpi](int v) { return Esc(v, dpi); };
    const int P = E(PADL);
    RectsL r{};
    for (int i = 0; i < 3; i++) r.opc[i] = f.opc[i];
    r.nOpc = f.n;

    // cabecalho: os chips do idioma a esquerda, o X a direita. Esta e a primeira janela do
    // pacote portable e quem nao le portugues precisa trocar ANTES de qualquer coisa. Eles
    // sao do tamanho do proprio nome ("Português" e "English" nao tem a mesma largura) para
    // nao virarem dois blocos grandes acima do nome do produto.
    {
        const int lp = LarguraTexto(dc, F ? F->micro : nullptr, NomeLingua(LINGUA_PT), 13, dpi);
        const int le = LarguraTexto(dc, F ? F->micro : nullptr, NomeLingua(LINGUA_EN), 13, dpi);
        r.lingPt = { P, E(18), P + lp + E(22), E(42) };
        r.lingEn = { r.lingPt.right + E(6), E(18), r.lingPt.right + E(6) + le + E(22), E(42) };
    }
    r.close_ = { w - E(38), E(14), w - E(14), E(38) };

    // O ICONE fica a esquerda do bloco de cabecalho, centrado na altura das tres linhas
    // (nome, linha de valor, estado da placa), e empurra as tres para a direita. Sem icone
    // (recurso ausente, ou o app rodando sem ele) o bloco volta a comecar na margem: a tela
    // nao reserva buraco para uma imagem que nao veio.
    const int tx0 = f.icone ? P + E(ICONE_L) + E(16) : P;
    r.titulo   = { tx0, E(56), w - P, E(102) };
    r.versao   = { tx0, E(72), w - P, E(98) };    // a linha da versao/pilula; o x real sai do texto do titulo
    r.pilula   = r.versao;
    r.slogan   = { tx0, E(106), w - P, E(132) };
    r.hardware = { tx0, E(138), w - P, E(162) };
    // A ajuda do modo AMD e uma linha a mais DENTRO do cabecalho: ela pertence a linha de
    // estado (explica o "AMD experimental" que esta escrito ali), nao ao corpo da tela.
    int yc = E(162);
    if (f.notaModo) { r.notaModo = { tx0, yc + E(2), w - P, yc + E(22) }; yc += E(24); }
    // O icone fica centrado na altura REAL do cabecalho, que cresce com essa linha.
    if (f.icone) {
        const int meio = (E(56) + yc) / 2, lado = E(ICONE_L);
        r.icone = { P, meio - lado / 2, P + lado, meio - lado / 2 + lado };
    }
    r.iniciar  = { P, yc + E(16), w - P, yc + E(16) + E(60) };

    int y = r.iniciar.bottom + E(6);

    if (f.explica) {
        r.explica = { P, y, w - P, y + E(58) };
        y += E(58) + E(8);
    }
    // STATUS/NOVIDADES: uma linha pequena. O "o que mudou" fica encostado a direita (o texto
    // do status muda quando a rede responde, e um link que anda de lugar nao se acha).
    r.status = { P, y + E(2), w - P, y + E(24) };
    {
        const int ln = LarguraTexto(dc, F ? F->tiny : nullptr, T(L"o que mudou", L"what changed"), 12, dpi);
        r.novidades = { w - P - ln - E(4), r.status.top, w - P, r.status.bottom };
    }
    y += E(28);

    // o cartao "Relatorio de uso" (so na copia do autor: relatorio_url= no ini)
    if (f.relatorio) {
        r.relatorio = { P, y, w - P, y + E(AlturaCartao() - 30) };
        y += E(AlturaCartao() - 30) + E(10);
    }

    // ------ A FAIXA DO CANAL (variacoes FAIXA e VITRINE). Ela vem DEPOIS de tudo o que e do
    // programa e ANTES do rodape: quem abriu para usar o app ja leu o que precisava e ja
    // passou pelo INICIAR, e quem rola o olho ate o fim encontra a oferta num bloco largo,
    // com ar, que nao se confunde com a tela. O rotulo "DO CANAL" em cima e o que a separa
    // de um anuncio de terceiro - a faixa nao finge ser parte do programa nem finge nao
    // existir. Altura de 8 em 8: 112 = 14 modulos.
    // A faixa oculta deixa UMA linha no lugar dela: "Ofertas ocultas · reativar". Ela custa
    // 26 unidades contra as 158 da faixa inteira, e e o caminho de volta.
    if (f.faixaOculta) {
        const int lr = LarguraTexto(dc, F ? F->tiny : nullptr, T(L"reativar", L"turn back on"), 12, dpi);
        r.banReativar = { w - P - lr - E(4), y + E(6), w - P, y + E(26) };
        r.banRotulo   = { P, y + E(6), r.banReativar.left - E(8), y + E(26) };
        y += E(30);
    }
    if (f.faixa) {
        const int hb2 = E(128);       // 16 modulos de 8
        const int lo = LarguraTexto(dc, F ? F->tiny : nullptr, T(L"ocultar", L"hide"), 12, dpi);
        r.banOcultar = { w - P - lo - E(4), y + E(8), w - P, y + E(26) };
        r.banRotulo = { P, y + E(8), r.banOcultar.left - E(8), y + E(26) };
        r.banner    = { P, y + E(30), w - P, y + E(30) + hb2 };
        const int qi = r.banner.left + E(16), qt = r.banner.top + E(16);
        const int ql = hb2 - E(32);                       // o QR ocupa a altura util da faixa
        r.banQr     = { qi, qt, qi + ql, qt + ql };
        const int tx = r.banQr.right + E(18), tr = r.banner.right - E(16);
        r.banTitulo = { tx, r.banner.top + E(14), tr, r.banner.top + E(44) };
        r.banMsg    = { tx, r.banner.top + E(46), tr, r.banner.top + E(80) };
        // o mesmo par do Config::botao - duas traducoes diferentes para a mesma frase em
        // portugues seria a tela se contradizendo em ingles
        const wchar_t* rot = (botaoFaixa && *botaoFaixa) ? botaoFaixa : T(L"QUERO AS OFERTAS", L"SHOW ME THE DEALS");
        const int lb = LarguraTexto(dc, F ? F->micro : nullptr, rot, 13, dpi);
        r.bannerBotao = { tx, r.banner.bottom - E(44), tx + lb + E(36), r.banner.bottom - E(12) };
        if (temCupom) r.banCupom = { r.bannerBotao.right + E(10), r.bannerBotao.top + E(3),
                                     r.bannerBotao.right + E(10) + E(130), r.bannerBotao.bottom - E(3) };
        y = r.banner.bottom + E(6);
    }

    // ------ RODAPE: a unica hora em que o canal aparece na tela principal, e numa linha so.
    r.sep   = { P, y + E(10), w - P, y + E(11) };
    const int yb = y + E(22), hb = E(30);
    r.marca = { P, yb, P + LarguraTexto(dc, F ? F->tiny : nullptr, T(L"Feito pelo PC do Fafa", L"Made by PC do Fafa"), 12, dpi), yb + hb };
    {
        int x = r.marca.right + E(14);
        // COM A FAIXA o grupo ja tem um bloco inteiro logo acima: repetir aqui um botao
        // "Entrar no grupo de ofertas" seria pedir a mesma coisa duas vezes na mesma tela,
        // e duas chamadas para a mesma acao diluem as duas. Sobra o apoio ao canal, que e
        // outro destino.
        // COM CUPOM O ROTULO DO GRUPO ENCOLHE. Medido na tela: com a publi remota trazendo um
        // cupom, a linha "Feito pelo PC do Fafa · Entrar no grupo de ofertas · Apoiar o canal
        // · CUPOM FAFA10" passava da largura por poucos pixels, e quem sobrava de fora era
        // justamente a pilula do cupom - a peca que paga o app. Em vez de deixar a sobra
        // decidir o que desaparece, o rotulo mais longo e o que cede.
        if (!f.faixa) {
            const int lg = LarguraTexto(dc, F ? F->micro : nullptr, RotuloGrupo(temCupom), 13, dpi);
            r.grupo = { x, yb, x + lg + E(26), yb + hb };
            x = r.grupo.right + E(8);
        }
        const int lc = LarguraTexto(dc, F ? F->micro : nullptr, T(L"Apoiar o canal", L"Support the channel"), 13, dpi);
        r.canal = { x, yb, x + lc + E(26), yb + hb };
        // O SELO DO CUPOM (publi remota) e uma pilula pequena encostada a direita: quando ha
        // cupom ele aparece, quando nao ha o rodape nao muda de forma. Com a faixa ele ja
        // esta la em cima, ao lado do botao ambar, e nao se repete aqui.
        if (temCupom && !f.faixa) r.cupom = { w - P - E(150), yb + E(4), w - P, yb + hb - E(4) };
    }
    const int hPx = yb + hb + E(16);
    r.alturaPx = hPx;

    // ------ A ABA DA GAVETA, colada na borda esquerda da coluna do produto. Na variacao
    // VITRINE a coluna de ofertas nao abre nem fecha - ela esta sempre la -, entao uma aba
    // de abrir/fechar nao teria o que fazer. Em ingles nao ha aba nenhuma: nao ha ofertas.
    if (li && li->vitrine && f.ofertas && f.layout != LAY_VITRINE) {
        const int meio = hPx / 2;
        r.aba = { 0, meio - E(60), E(22), meio + E(60) };
    }

    // ------ O POPOVER DO QR (por cima de tudo, so quando aberto).
    if (popover) {
        r.popFundo = { -oxPx, 0, w, hPx };                       // a janela inteira (inclui a gaveta)
        const int cw = (std::min)(w - E(120), E(400));
        const int cx = (w - cw) / 2;
        r.popCard = { cx, E(18), cx + cw, hPx - E(18) };
        const int ix = r.popCard.left + E(24), ir = r.popCard.right - E(24);
        r.popFechar = { r.popCard.right - E(36), r.popCard.top + E(10), r.popCard.right - E(12), r.popCard.top + E(34) };
        r.popTitulo = { ix, r.popCard.top + E(20), ir - E(26), r.popCard.top + E(76) };
        r.popBotao  = { ix, r.popCard.bottom - E(60), ir, r.popCard.bottom - E(16) };
        r.popRodape = { ix, r.popBotao.top - E(26), ir, r.popBotao.top - E(6) };
        r.popNome   = { ix, r.popRodape.top - E(28), ir, r.popRodape.top };
        r.popMsg    = { ix, r.popTitulo.bottom, ir, r.popTitulo.bottom + E(44) };
        // o QR fica com o que sobra no meio, sempre quadrado
        const int t = r.popMsg.bottom + E(10), b = r.popNome.top - E(10);
        const int lado = (std::min)(b - t, ir - ix);
        const int qx = (r.popCard.left + r.popCard.right) / 2 - lado / 2;
        r.popQr = { qx, t, qx + lado, t + (lado < 0 ? 0 : lado) };
    }

    if (oxPx)
        for (RECT* p : { &r.lingPt, &r.lingEn, &r.close_, &r.icone, &r.titulo, &r.versao, &r.pilula, &r.slogan,
                         &r.hardware, &r.notaModo, &r.iniciar,
                         &r.explica, &r.status, &r.novidades,
                         &r.relatorio, &r.sep, &r.marca, &r.grupo, &r.canal, &r.cupom, &r.aba,
                         &r.banner, &r.banRotulo, &r.banQr, &r.banTitulo, &r.banMsg,
                         &r.bannerBotao, &r.banCupom, &r.banOcultar, &r.banReativar,
                         &r.popFundo, &r.popCard, &r.popTitulo, &r.popQr, &r.popNome, &r.popRodape,
                         &r.popMsg, &r.popBotao, &r.popFechar })
            OffsetRect(p, oxPx, 0);
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
// os chips do idioma, no cabecalho do launcher (PtInRect num retangulo vazio e sempre
// falso, entao nao ha o que proteger com um `if` a mais)
inline bool PontoIdioma(const RectsL& R, POINT p) {
    return PtInRect(&R.lingPt, p) || PtInRect(&R.lingEn, p);
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
    // Regioes de recorte sao em coordenadas de DISPOSITIVO: quando a origem do DC esta
    // deslocada (a coluna de sempre desenhada a direita da vitrine), o retangulo logico tem
    // de ser convertido, senao o recorte cai no lugar errado e o botao sai quadrado/cortado.
    POINT d[2] = { { rc.left, rc.top }, { rc.right + 1, rc.bottom + 1 } };
    LPtoDP(dc, d, 2);
    HRGN rg = CreateRoundRectRgn(d[0].x, d[0].y, d[1].x, d[1].y, rad, rad);
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
        int larg = box.right - box.left;
        // O DT_CALCRECT NAO soma o espacamento extra entre letras que o desenho soma (ver a
        // nota em MedeTexto). Sem corrigir isso, uma frase que "cabe" na conta pede uma linha
        // a mais na tela e essa linha desaparece cortada - foi o que aconteceu com o titulo
        // da publi remota no popover, que saiu "RTX 4070 SUPER POR R$ 3.4". A correcao e
        // medir numa caixa proporcionalmente mais estreita: o que o spacing acrescenta por
        // linha e ele vezes o numero de letras que cabem nela.
        if (spacing > 0) {
            const SIZE u = MedeTexto(dc, f, L"n", 0);
            larg -= spacing * (larg / (u.cx > 0 ? u.cx : 1));
            if (larg < Esc(40, dpi)) larg = Esc(40, dpi);
        }
        RECT calc{ 0, 0, larg, 0 };
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
    // O DT_CALCRECT NAO soma o espacamento extra entre letras que o DESENHO soma: medido na
    // tela, "DLSS 5 FOR ALL" com spacing=1 saia 26 px mais largo do que a medida dizia, e a
    // versao ao lado do nome encostava nele. Como o extra entra depois de cada caractere,
    // o que falta e exatamente spacing x numero de caracteres.
    return SIZE{ r.right - r.left + (spacing > 0 ? spacing * (int)wcslen(s) : 0), r.bottom - r.top };
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

// A coluna da VITRINE (so no launcher com vitrine): a faixa de titulo na mesma linha da
// marca da coluna ao lado, o painel onde a pagina entra (com o aviso enquanto ela nao esta
// la - a pagina, quando existe, e uma janela filha que cobre o painel) e o separador.
inline void PaintColunaVitrine(HDC dc, int ox, int h, const View& v) {
    const UINT dpi = v.dpi ? v.dpi : 96;
    auto E = [dpi](int x) { return Esc(x, dpi); };
    Fonts& F = *v.fonts;
    RECT ti{ E(16), E(34), ox - E(16), E(58) };
    DrawText1(dc, F.marca, C_CY_L, ti, T(L"OFERTAS DE AGORA", L"DEALS RIGHT NOW"), DT_LEFT | DT_SINGLELINE | DT_VCENTER, E(1));
    {   // o selo "atualiza sozinho", encostado a direita da faixa
        const wchar_t* selo = T(L"atualiza sozinho", L"updates on its own");
        SIZE mt = MedeTexto(dc, F.tiny, selo);
        RECT pill{ ox - E(16) - mt.cx - E(18), E(36), ox - E(16), E(56) };
        StrokeRound(dc, pill, E(10), C_CY_D);
        DrawText1(dc, F.tiny, C_CY_L, pill, selo, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    RECT sb{ E(16), E(58), ox - E(16), E(74) };
    DrawText1(dc, F.sub, C_MUTE, sb, T(L"DO GRUPO PC DO FAFA", L"FROM THE PC DO FAFA GROUP"), DT_LEFT | DT_SINGLELINE, E(3));
    RECT web = RectVitrine(dpi, h);
    FillRound(dc, web, E(14), C_SURF, C_CARD, true);
    if (v.vitrineEstado != 1) {
        RECT t1 = web; t1.bottom = (web.top + web.bottom) / 2 + E(2);
        RECT t2 = web; t2.top = t1.bottom;
        DrawText1(dc, F.small_, C_SUP, t1,
                  v.vitrineEstado == 0 ? T(L"carregando ofertas…", L"loading deals…")
                                       : T(L"sem conexão com as ofertas", L"no connection to the deals"),
                  DT_CENTER | DT_SINGLELINE | DT_BOTTOM);
        if (v.vitrineEstado == 2)
            DrawText1(dc, F.tiny, C_MUTE, t2, T(L"o botão QUERO AS OFERTAS ao lado continua funcionando",
                                                L"the SHOW ME THE DEALS button next to this still works"),
                      DT_CENTER | DT_SINGLELINE | DT_TOP);
    }
    RECT sep{ ox - 1, E(3), ox, h - E(3) };
    HBRUSH b = CreateSolidBrush(C_CARD);
    FillRect(dc, &sep, b);
    DeleteObject(b);
}

// Todo o desenho da janela num lugar so - WM_PAINT e a ferramenta de screenshot usam
// exatamente esta funcao, entao o PNG de conferencia e o que a tela mostra.
// Com a gaveta aberta a janela e "vitrine | coluna do produto": o fundo, a moldura e as
// faixas de marca cobrem a janela inteira, e a coluna do produto e desenhada com os
// retangulos JA deslocados por LayoutL (nada de mexer na origem do DC: as regioes de
// recorte dos cantos arredondados sao em coordenadas de dispositivo e nao acompanhariam).
inline void PaintClassica(HDC dc, int w, int h, const View& v);
inline void PaintLauncher(HDC dc, int w, int h, const View& v);
inline void Paint(HDC dc, int w, int h, const View& v) {
    const UINT dpi = v.dpi ? v.dpi : 96;
    auto E = [dpi](int x) { return Esc(x, dpi); };
    RECT all{ 0, 0, w, h };
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    HBRUSH edge = CreateSolidBrush(C_CARD);
    FrameRect(dc, &all, edge);
    DeleteObject(edge);
    // faixa de marca no topo (o eco da barra azul do site) e no rodape, fechando o quadro
    // (era a barra de tempo, que saiu junto com a contagem regressiva)
    RECT topo{ 0, 0, w, E(3) };
    GradienteNoRecorte(dc, topo, C_CY, C_BLUE);
    RECT base{ 0, h - E(3), w, h };
    GradienteNoRecorte(dc, base, C_CY, C_BLUE);
    if (v.li) {
        if (v.colunaX > 0) PaintColunaVitrine(dc, v.colunaX, h, v);
        PaintLauncher(dc, w, h, v);
    } else {
        PaintClassica(dc, w, h, v);
    }
}
inline void PaintClassica(HDC dc, int w, int h, const View& v) {
    const UINT dpi = v.dpi ? v.dpi : 96;
    auto E = [dpi](int x) { return Esc(x, dpi); };   // toda medida solta daqui pra baixo
    Rects R = Layout(w, h, dpi);
    const Config& C = *v.cfg;
    Fonts& F = *v.fonts;
    const int TY = 0;

    // ---- coluna do QR: cartao escuro, codigo grande e o endereco escrito
    FillRound(dc, R.moldura, E(18), C_CARD, 0, false);
    DrawQr(dc, R.card, *v.code, dpi);
    // Embaixo do QR vai o NOME do grupo (nome_grupo= no ofertas.ini), nao o endereco: quem le
    // reconhece o grupo, e o destino real continua sendo o do QR/botao (UrlSegura). A linha
    // continua clicavel, como sempre foi.
    DrawText1(dc, F.addr, v.hoverLink ? C_CY_L : C_TEXT, R.addr,
              C.nomeGrupo.c_str(), DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    DrawText1(dc, F.tiny, C_MUTE, R.rodapeQr, C.rodape.c_str(), DT_CENTER | DT_SINGLELINE);

    // ---- coluna do texto
    const int X = E(396), RW = w - X - E(32);

    RECT nm{ X, TY + E(34), X + RW, TY + E(58) };
    DrawText1(dc, F.marca, C_CY_L, nm, C.marca.c_str(), DT_LEFT | DT_SINGLELINE, E(1));
    RECT sb{ X, TY + E(58), X + RW, TY + E(74) };
    DrawText1(dc, F.sub, C_MUTE, sb, C.submarca.c_str(), DT_LEFT | DT_SINGLELINE, E(3));

    RECT ti{ X, TY + E(92), X + RW - E(70), TY + E(186) };
    DrawTextCaixa(dc, F.title, Maiuscula(C.titulo).c_str(), ti, C_TEXT,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS, 28, 18, FW_BOLD, dpi, E(1));

    RECT ms{ X, TY + E(194), X + RW, TY + E(254) };
    DrawTextCaixa(dc, F.msg, C.mensagem.c_str(), ms, C_BODY,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS, 16, 11, FW_NORMAL, dpi);

    // selo com bordinha ciano, no espirito dos badges do site. Com CUPOM (conteudo remoto),
    // o selo vira "CUPOM XXX - texto" e e clicavel (copia o cupom e abre o link).
    {
        const std::wstring seloTxt = v.cupom.empty() ? C.microcta
            : (TS(L"CUPOM ", L"COUPON ") + v.cupom + (v.cupomTexto.empty() ? L"" : (L" — " + v.cupomTexto)));
        SIZE mt = MedeTexto(dc, F.micro, seloTxt.c_str());   // ja medido na fonte escalada
        RECT bd{ X, R.selo.top, X + (std::min)((LONG)(mt.cx + E(26)), (LONG)RW), R.selo.bottom };
        if (!v.cupom.empty() && v.hoverCupom) FillRound(dc, bd, E(14), C_SURF, 0, false);
        StrokeRound(dc, bd, E(14), (!v.cupom.empty() && v.hoverCupom) ? C_CY : C_CY_D);
        DrawText1(dc, F.micro, (!v.cupom.empty() && v.hoverCupom) ? C_TEXT : C_CY_L, bd, seloTxt.c_str(),
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }

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
    RECT assin = R.assin;
    if (assin.right > assin.left + E(40))
        DrawText1(dc, F.tiny, C_MUTE, assin, T(L"DLSS5 For All · abrindo…", L"DLSS5 For All · opening…"),
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    (void)h;   // as faixas de marca (topo e rodape) sao da janela inteira: ver Paint
}

// ---- CARTAO "RELATORIO DE USO" (so na copia do autor, e so no launcher). Numeros do
// servidor; um mini grafico de barras dos ultimos 14 dias (usuarios distintos por dia) e os
// cliques por botao. Sem rede: "indisponivel" e o ultimo registro do relatorio-uso.txt.
inline void PaintCartaoRelatorio(HDC dc, RECT cr, const View& v) {
    const UINT dpi = v.dpi ? v.dpi : 96;
    auto E = [dpi](int x) { return Esc(x, dpi); };
    Fonts& F = *v.fonts;
    {
        FillRound(dc, cr, E(14), C_SURF, C_CARD, true);
        const int cx = cr.left + E(18), cw = cr.right - cr.left - E(36);
        RECT t1{ cx, cr.top + E(10), cx + cw, cr.top + E(30) };
        auto fmtN = [](long long n) {   // 4870 -> "4.870" (pt) / "4,870" (en)
            std::wstring s = std::to_wstring(n < 0 ? 0 : n), o;
            int k = 0; for (int i = (int)s.size() - 1; i >= 0; i--) { o.insert(o.begin(), s[i]); if (++k % 3 == 0 && i > 0) o.insert(o.begin(), EmIngles() ? L',' : L'.'); }
            return o;
        };
        if (v.resumoEstado == 1 && v.resumo) {
            const ResumoUso& S = *v.resumo;
            DrawText1(dc, F.sub, C_CY_L, t1, (TS(L"RELATÓRIO DE USO", L"USAGE REPORT") + (S.gerado.empty() ? L"" : (L"  ·  " + S.gerado))).c_str(),
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER, E(2));
            // linha-resumo: hoje / 7 / 30 dias / versao dominante / nvidia x amd
            long long totalV = 0, totalM = 0; std::wstring topV; long long topVn = -1;
            for (auto& p : S.porVersao) { totalV += p.second; if (p.second > topVn) { topVn = p.second; topV = p.first; } }
            long long nv = 0, am = 0, sem = 0;
            for (auto& p : S.porModulo) { totalM += p.second; if (p.first == L"nvidia") nv = p.second; else if (p.first == L"amd") am = p.second; else sem += p.second; }
            std::wstring linha = TS(L"Hoje ", L"Today ") + fmtN(S.hojeUs) + TS(L" pessoas", L" people") +
                                 L"  ·  7 " + TS(L"dias ", L"days ") + fmtN(S.d7Us) + L"  ·  30 " + TS(L"dias ", L"days ") + fmtN(S.d30Us);
            if (totalV > 0 && !topV.empty()) linha += L"  ·  v" + topV + L" " + std::to_wstring(topVn * 100 / totalV) + L"%";
            if (totalM > 0) linha += L"  ·  NVIDIA " + std::to_wstring(nv * 100 / totalM) + L"% / AMD " + std::to_wstring(am * 100 / totalM) + L"%" + (sem ? (TS(L" / sem ", L" / none ") + std::to_wstring(sem * 100 / totalM) + L"%") : L"");
            RECT t2{ cx, cr.top + E(32), cx + cw, cr.top + E(52) };
            DrawText1(dc, F.small_, C_TEXT, t2, linha.c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            // barras: 14 dias, na metade esquerda
            const int gx = cx, gw = cw * 46 / 100, gy0 = cr.top + E(60), gy1 = cr.top + E(140);
            const size_t n = S.porDia.size();
            long long mx = 1; for (auto& p : S.porDia) if (p.second > mx) mx = p.second;
            if (n) {
                const int passo = gw / (int)n, larg = (std::max)(E(3), passo - E(3));
                HBRUSH bb = CreateSolidBrush(C_CY_D), bh = CreateSolidBrush(C_CY);
                for (size_t i = 0; i < n; i++) {
                    const int hh = (int)((long long)(gy1 - gy0 - E(4)) * S.porDia[i].second / mx);
                    RECT b{ gx + (int)i * passo, gy1 - hh, gx + (int)i * passo + larg, gy1 };
                    FillRect(dc, &b, i + 1 == n ? bh : bb);
                }
                DeleteObject(bb); DeleteObject(bh);
                // eixo e rotulos: data mais antiga, mais nova e o maior valor
                HPEN eixo = CreatePen(PS_SOLID, 1, C_CARD); HGDIOBJ op = SelectObject(dc, eixo);
                MoveToEx(dc, gx, gy1, nullptr); LineTo(dc, gx + gw, gy1); SelectObject(dc, op); DeleteObject(eixo);
                auto dm = [](const std::wstring& d) { return d.size() >= 10 ? d.substr(8, 2) + L"/" + d.substr(5, 2) : d; };
                RECT l1{ gx, gy1 + E(2), gx + gw / 2, gy1 + E(16) }, l2{ gx + gw / 2, gy1 + E(2), gx + gw, gy1 + E(16) };
                DrawText1(dc, F.tiny, C_MUTE, l1, dm(S.porDia.front().first).c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                DrawText1(dc, F.tiny, C_MUTE, l2, (dm(S.porDia.back().first) + L" · " + fmtN(S.porDia.back().second) + TS(L" pessoas", L" people")).c_str(), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
                RECT l3{ gx, gy0 - E(14), gx + gw, gy0 };
                DrawText1(dc, F.tiny, C_MUTE, l3, (TS(L"pessoas por dia · máx. ", L"people per day · max ") + fmtN(mx)).c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
            // cliques por botao (hoje e 7 dias), na metade direita, com nomes legiveis
            auto nomeBotao = [](const std::wstring& e) -> std::wstring {
                if (e == L"clique_nvidia")   return TS(L"Botão NVIDIA", L"NVIDIA button");
                if (e == L"clique_amd")      return TS(L"Botão AMD", L"AMD button");
                if (e == L"clique_sem_dlss") return TS(L"Abrir sem DLSS 5", L"Open without DLSS 5");
                if (e == L"clique_grupo")    return TS(L"Quero as ofertas", L"Show me the deals");
                if (e == L"clique_canal")    return TS(L"Apoiar o canal", L"Support the channel");
                if (e == L"clique_cupom")    return TS(L"Cupom", L"Coupon");
                if (e == L"clique_atualizar") return TS(L"Atualizar", L"Update");
                if (e == L"fechou_sem_escolher") return TS(L"Fechou sem escolher", L"Closed without choosing");
                if (e == L"atualizacao_ok")  return TS(L"Atualização ok", L"Update ok");
                if (e == L"atualizacao_falhou") return TS(L"Atualização falhou", L"Update failed");
                return e;
            };
            auto listaBotoes = [&](const std::vector<std::pair<std::wstring, long long>>& m) {
                std::wstring s;
                for (auto& p : m) { if (!s.empty()) s += L"  ·  "; s += nomeBotao(p.first) + L" " + fmtN(p.second); }
                return s.empty() ? TS(L"(sem cliques)", L"(no clicks)") : s;
            };
            const int bx = gx + gw + E(18), bw = cr.right - E(18) - bx;
            RECT h1{ bx, gy0 - E(14), bx + bw, gy0 };
            DrawText1(dc, F.tiny, C_MUTE, h1, T(L"cliques hoje", L"clicks today"), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            RECT c1{ bx, gy0, bx + bw, gy0 + E(38) };
            DrawTextCaixa(dc, F.tiny, listaBotoes(S.porBotaoHoje).c_str(), c1, C_BODY, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS, 12, 10, FW_NORMAL, dpi);
            RECT h2{ bx, gy0 + E(42), bx + bw, gy0 + E(56) };
            DrawText1(dc, F.tiny, C_MUTE, h2, T(L"cliques em 7 dias", L"clicks in 7 days"), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            RECT c2{ bx, gy0 + E(56), bx + bw, gy1 + E(16) };
            DrawTextCaixa(dc, F.tiny, listaBotoes(S.porBotao7).c_str(), c2, C_BODY, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS, 12, 10, FW_NORMAL, dpi);
        } else {
            DrawText1(dc, F.sub, C_CY_L, t1, T(L"RELATÓRIO DE USO", L"USAGE REPORT"), DT_LEFT | DT_SINGLELINE | DT_VCENTER, E(2));
            RECT t2{ cx, cr.top + E(32), cx + cw, cr.top + E(52) };
            DrawText1(dc, F.small_, C_BODY, t2, v.resumoEstado == 0 ? T(L"buscando…", L"loading…")
                                                                     : T(L"relatório indisponível (sem rede)", L"report unavailable (no network)"),
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            if (v.resumo && !v.resumo->ultimoRegistro.empty()) {
                RECT t3{ cx, cr.top + E(54), cx + cw, cr.top + E(74) };
                DrawText1(dc, F.tiny, C_MUTE, t3, (TS(L"último registro guardado: ", L"last saved record: ") + v.resumo->ultimoRegistro).c_str(),
                          DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            }
        }
    }
}

// Uma bolinha cheia (o sinal de plataforma nos chips e no estado do hardware). Pequena de
// proposito: o sinal e sutil, o botao inteiro nao muda de cor por causa do fabricante.
// Uma cor entre duas, com `t` de 0 (a primeira) a 1 (a segunda). Serve ao fade da troca de
// oferta e ao pisca do rotulo - GDI nao tem transparencia de texto, entao o que se faz e
// calcular a cor intermediaria e desenhar opaco. Como o fundo do bloco e liso, o resultado na
// tela e o mesmo de um fade de verdade.
inline COLORREF MisturaCor(COLORREF a, COLORREF b, double t) {
    if (t <= 0.0) return a;
    if (t >= 1.0) return b;
    auto m = [t](int x, int y) { return (int)(x + (y - x) * t + 0.5); };
    return RGB(m(GetRValue(a), GetRValue(b)), m(GetGValue(a), GetGValue(b)), m(GetBValue(a), GetBValue(b)));
}
inline void Bolinha(HDC dc, int cx, int cy, int raio, COLORREF cor) {
    HBRUSH b = CreateSolidBrush(cor);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, cx - raio, cy - raio, cx + raio + 1, cy + raio + 1);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b);
}
// A cor do fabricante, so para a bolinha.
inline COLORREF CorOpcao(int o) {
    return o == 0 ? RGB(118, 185, 0) : o == 1 ? RGB(237, 28, 36) : C_MUTE;
}
// Uma pilula vazada com texto no meio; devolve a largura que ela ocupou.
inline int PilulaTexto(HDC dc, RECT& rc, HFONT f, const wchar_t* s, COLORREF borda, COLORREF texto,
                       UINT dpi, bool preenche) {
    SIZE m = MedeTexto(dc, f, s);
    rc.right = rc.left + m.cx + Esc(20, dpi);
    if (preenche) FillRound(dc, rc, Esc(11, dpi), C_SURF, 0, false);
    StrokeRound(dc, rc, Esc(11, dpi), borda);
    DrawText1(dc, f, texto, rc, s, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    return rc.right - rc.left;
}

// =====================================================================================
//  A TELA DO LAUNCHER. Ver o bloco de comentario de LayoutL para o porque do desenho.
// =====================================================================================
inline void PaintLauncher(HDC dc, int w, int h, const View& v) {
    const UINT dpi = v.dpi ? v.dpi : 96;
    auto E = [dpi](int x) { return Esc(x, dpi); };
    Fonts& F = *v.fonts;
    const Config& C = *v.cfg;
    const LauncherInfo* li = v.li;
    FormaL f = FormaDe(li);
    // A FormaL sai de FormaDe, que so conhece o LauncherInfo; se ha icone e coisa da JANELA
    // (o recurso carregou neste DPI?), e quem sabe disso e a View. Sem esta linha o desenho
    // usaria um layout sem icone e o hit-test um layout com icone - as duas coisas que esta
    // base junta de proposito num lugar so.
    f.icone = v.icone != nullptr;
    const int cw = w - v.colunaX;                  // a coluna do produto
    const std::wstring rotBotao = Maiuscula(C.botao);
    const RectsL R = LayoutL(cw, dpi, li, f, v.colunaX, v.popover, !v.cupom.empty(), dc, v.fonts,
                             rotBotao.c_str());

    // ---- cabecalho: idioma a esquerda, X a direita
    auto chip = [&](RECT rc, Lingua l, bool hover) {
        const bool sel = (g_lingua == l);
        if (sel)         FillRound(dc, rc, E(9), C_CY_D, 0, false);
        else if (hover)  FillRound(dc, rc, E(9), C_SURF, C_CY_D, true);
        else             StrokeRound(dc, rc, E(9), C_CARD);
        DrawText1(dc, F.small_, sel ? C_BG : (hover ? C_TEXT : C_BODY), rc, NomeLingua(l),
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    };
    chip(R.lingPt, LINGUA_PT, v.hoverLingPt);
    chip(R.lingEn, LINGUA_EN, v.hoverLingEn);
    if (v.hoverClose) FillRound(dc, R.close_, E(6), C_SURF, 0, false);
    DrawText1(dc, F.close_, v.hoverClose ? C_TEXT : C_MUTE, R.close_, L"✕", DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    // ---- O ICONE DO APP. DrawIconEx e de user32 e faz a mistura com o fundo por conta
    // propria, inclusive o canal alfa das bordas do desenho - nao ha AlphaBlend nem a
    // msimg32 no caminho (ver a nota do popover sobre por que esta base nao linka msimg32).
    if (v.icone && !IsRectEmpty(&R.icone))
        DrawIconEx(dc, R.icone.left, R.icone.top, v.icone,
                   R.icone.right - R.icone.left, R.icone.bottom - R.icone.top, 0, nullptr, DI_NORMAL);

    // ---- O PRODUTO. O nome grande e a primeira coisa da tela; era uma assinatura de 12 px
    // no rodape enquanto a manchete da publi ocupava este lugar.
    const wchar_t* kNome = L"DLSS 5 FOR ALL";
    DrawText1(dc, F.produto, C_TEXT, R.titulo, kNome, DT_LEFT | DT_BOTTOM | DT_SINGLELINE, E(1));
    {
        SIZE mn = MedeTexto(dc, F.produto, kNome, E(1));
        int x = R.titulo.left + mn.cx + E(20);
        if (li && !li->versaoApp.empty()) {
            const std::wstring vs = L"v" + li->versaoApp;
            RECT rv{ x, R.versao.top, R.versao.right, R.versao.bottom };
            DrawText1(dc, F.small_, C_SUP, rv, vs.c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            x += MedeTexto(dc, F.small_, vs.c_str()).cx + E(12);
        }
        // A PILULA "atualizacao disponivel" e so um aviso: a caixa que pergunta continua
        // aparecendo SO depois do INICIAR (e do launcher, nao desta tela).
        if (v.versaoEstado == 1) {
            const std::wstring p = T(L"atualização disponível", L"update available") +
                                   (v.versaoNova.empty() ? std::wstring() : (L" " + v.versaoNova));
            RECT rp{ x, R.pilula.top + E(1), R.pilula.right, R.pilula.bottom - E(1) };
            if (rp.left + E(60) < rp.right) PilulaTexto(dc, rp, F.tiny, p.c_str(), C_CY_D, C_CY_L, dpi, false);
        }
    }
    DrawText1(dc, F.msg, C_BODY, R.slogan, C.slogan.c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    if (li && !li->estadoHardware.empty()) {
        const COLORREF cs = li->hwSinal == 1 ? RGB(34, 197, 94) : li->hwSinal == 2 ? RGB(234, 179, 8) : C_MUTE;
        const int cy = (R.hardware.top + R.hardware.bottom) / 2;
        Bolinha(dc, R.hardware.left + E(4), cy, E(4), cs);
        RECT rh = R.hardware; rh.left += E(16);
        DrawText1(dc, F.small_, C_SUP, rh, li->estadoHardware.c_str(),
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
    // A ajuda curta do modo AMD, alinhada com o texto do estado (e nao com a bolinha): ela
    // explica o "AMD experimental" que esta escrito na linha de cima.
    if (f.notaModo && !IsRectEmpty(&R.notaModo)) {
        RECT rn = R.notaModo; rn.left += E(16);
        DrawText1(dc, F.tiny, C_MUTE, rn,
                  T(L"Modo AMD em desenvolvimento: funciona, mas ainda pesa mais.",
                    L"AMD mode is still in development: it works, but it is heavier."),
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }

    // ---- A ACAO PRINCIPAL. Um botao, uma cor de destaque na tela inteira.
    {
        RECT b = R.iniciar;
        if (v.pressed && v.hoverIniciar) OffsetRect(&b, 0, E(1));
        if (v.pressed && v.hoverIniciar) FillRoundGradient(dc, b, E(14), RGB(6, 110, 138), RGB(30, 79, 191));
        else if (v.hoverIniciar)         FillRoundGradient(dc, b, E(14), C_CY, C_BLUE_L);
        else                             FillRoundGradient(dc, b, E(14), C_CY_D, C_BLUE);
        DrawText1(dc, F.title, C_TEXT, b, T(L"INICIAR", L"START"), DT_CENTER | DT_SINGLELINE | DT_VCENTER, E(3));
    }


    // ---- Nenhuma placa serve: o programa continua util, e esta e a hora de dizer isso.
    if (f.explica)
        DrawTextCaixa(dc, F.tiny,
            T(L"Sem uma placa compatível o DLSS 5 não liga, mas o programa continua útil: ele aplica os "
              L"efeitos do ReShade em qualquer coisa que apareça na tela — jogo, emulador, vídeo, navegador "
              L"ou placa de captura — sem instalar nem injetar nada no jogo.",
              L"Without a compatible card DLSS 5 stays off, but the program is still useful: it applies "
              L"ReShade effects to anything on your screen — a game, an emulator, a video, a browser or a "
              L"capture card — without installing or injecting anything into the game."),
            R.explica, C_BODY, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS, 12, 10, FW_NORMAL, dpi);

    // ---- STATUS/NOVIDADES: uma linha pequena, sem peso nenhum.
    {
        const wchar_t* s = v.versaoEstado == 1 ? T(L"atualização disponível", L"update available")
                         : v.versaoEstado == 0 ? T(L"verificando a versão…", L"checking for updates…")
                                               : T(L"versão mais nova instalada", L"you have the newest version");
        DrawText1(dc, F.tiny, C_MUTE, R.status, s, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        if (!C.urlNovidades.empty())
            DrawText1(dc, F.tiny, v.hoverNovidades ? C_CY_L : C_CY_D, R.novidades,
                      T(L"o que mudou", L"what changed"), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }

    if (f.relatorio) PaintCartaoRelatorio(dc, R.relatorio, v);

    // ---- A FAIXA DO CANAL (variacoes FAIXA e VITRINE). Um bloco largo com rotulo de secao,
    // QR, titulo, mensagem e um botao ambar - o banner de destaque que a GOG e a Steam usam,
    // no tamanho que cabe numa janela de 620. Tres coisas o impedem de virar anuncio:
    //   1. o rotulo "DO CANAL" em cima, que assume de quem e a mensagem (house ad rotulado);
    //   2. a cor propria (ambar), que nunca e a cor do INICIAR;
    //   3. o lugar - depois de tudo o que e do programa, nunca acima da acao principal.
    // O bloco INTEIRO e clicavel (alvo grande, lei de Fitts), e o hover acende a borda toda
    // para que isso fique obvio antes do clique.
    // ---- A FAIXA OCULTA: uma linha, e o caminho de volta.
    if (f.faixaOculta) {
        DrawText1(dc, F.tiny, C_MUTE, R.banRotulo, T(L"Ofertas do canal ocultas", L"Channel deals hidden"),
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawText1(dc, F.tiny, v.hoverBanOcultar ? C_CY_L : C_CY_D, R.banReativar,
                  T(L"reativar", L"turn back on"), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }

    if (f.faixa) {
        // TUDO O QUE A FAIXA ESCREVE VEM DE TextoDaFaixa - rotulo, titulo, apoio e botao -,
        // porque os quatro precisam concordar entre si: o rotulo nao pode dizer "oferta de
        // hoje" com um aviso embaixo, nem "cupom novo" sem codigo de cupom nenhum.
        const TextoFaixa TX = TextoDaFaixa(C, v.tipoItem, v.cupom, v.publicado);
        // Sem publi do servidor, o rotulo e o generico: o texto que esta na faixa e o convite
        // fixo do ofertas.ini, e prometer "oferta de hoje" com uma manchete de sempre seria a
        // unica forma de a faixa mentir.
        const std::wstring rot = (v.remoto || TX.ehCanal) ? TX.rotulo
                                                          : std::wstring(T(L"DO CANAL", L"FROM THE CHANNEL"));
        // o rotulo pisca uma vez em ciano quando a oferta acaba de trocar (ver v.animFaixa)
        const COLORREF corRot = v.animFaixa > 0.0 ? MisturaCor(C_MUTE, C_CY_L, v.animFaixa) : C_MUTE;
        DrawText1(dc, F.sub, corRot, R.banRotulo, rot.c_str(),
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER, E(2));
        // "ocultar": discreto de proposito. Ele existe para quem incomoda, nao para chamar
        // atencao de quem nao pensou no assunto.
        DrawText1(dc, F.tiny, v.hoverBanOcultar ? C_BODY : C_MUTE, R.banOcultar,
                  T(L"ocultar", L"hide"), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        // O hover levanta a faixa INTEIRA (fundo mais claro e borda ambar), e nao so o botao:
        // o bloco todo e clicavel, e um realce que so acende no botao ensinaria o contrario.
        FillRound(dc, R.banner, E(14), v.hoverBanner ? C_CARD : C_SURF,
                  v.hoverBanner ? C_AMB : C_CARD, true);
        // o filete de cor na lateral esquerda: a "arte" do banner numa tela sem bitmap
        {
            RECT fi{ R.banner.left + E(1), R.banner.top + E(10), R.banner.left + E(5), R.banner.bottom - E(10) };
            HBRUSH b = CreateSolidBrush(C_AMB);
            FillRect(dc, &fi, b);
            DeleteObject(b);
        }
        // O QR fica dentro da faixa: quem tem o celular na mao entra sem clicar em nada, e
        // ele e o que da ao bloco a cara de banner e nao de aviso de texto.
        if (R.banQr.right > R.banQr.left) DrawQr(dc, R.banQr, *v.code, dpi);
        // A TROCA DE OFERTA. Enquanto a animacao corre, o conteudo do bloco (titulo, apoio,
        // botao, cupom) entra deslizando de baixo e clareando; a moldura, o QR e o rotulo
        // ficam parados, para a faixa nao "pular" na tela. `animFaixa` vai de 1 (comecou) a 0
        // (acabou); com a animacao do sistema desligada ela ja nasce em 0 e nada se move.
        const int desliza = (int)(E(12) * v.animFaixa);
        const COLORREF corTit = MisturaCor(C_TEXT, C_SURF, v.animFaixa);
        const COLORREF corApo = MisturaCor(C_SUP, C_SURF, v.animFaixa);
        RECT rt1 = R.banTitulo; OffsetRect(&rt1, 0, desliza);
        RECT rm1 = R.banMsg;    OffsetRect(&rm1, 0, desliza);
        DrawText1(dc, F.addr, corTit, rt1, TX.titulo.c_str(),
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        DrawTextCaixa(dc, F.tiny, TX.apoio.c_str(), rm1, corApo,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS, 12, 10, FW_NORMAL, dpi);
        RECT bb = R.bannerBotao; OffsetRect(&bb, 0, desliza);
        FillRound(dc, bb, E(10), MisturaCor(v.hoverBanBotao || v.hoverBanner ? C_AMB_L : C_AMB, C_SURF, v.animFaixa), 0, false);
        DrawText1(dc, F.micro, MisturaCor(C_BG, C_SURF, v.animFaixa), bb,
                  Maiuscula(TX.botao).c_str(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        // O CUPOM so aparece no caso em que ele existe de verdade - nunca uma pilula vazia.
        // O TITULO CORTADO TEM COMO SER LIDO. O nome do produto vem do servidor e pode ser
        // longo demais para a largura da faixa; o DT_END_ELLIPSIS corta com reticencias, e
        // quando isso acontece o mouse em cima do bloco mostra o texto inteiro numa caixinha.
        // Sem isso a pessoa ficaria sabendo que existe um resto e sem como ver qual.
        if (v.hoverBanner && !TX.titulo.empty()) {
            const SIZE mt = MedeTexto(dc, F.addr, TX.titulo.c_str());
            const int larg = R.banTitulo.right - R.banTitulo.left;
            if (mt.cx > larg) {
                const int lt = (std::min)(mt.cx, R.banner.right - R.banner.left - E(32));
                RECT tip{ R.banTitulo.left, R.banTitulo.top - E(34), R.banTitulo.left + lt + E(20), R.banTitulo.top - E(4) };
                FillRound(dc, tip, E(8), C_BG, C_AMB_D, true);
                RECT ti2 = tip; ti2.left += E(10); ti2.right -= E(10);
                DrawTextCaixa(dc, F.tiny, TX.titulo.c_str(), ti2, C_TEXT,
                              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS, 12, 9, FW_NORMAL, dpi);
            }
        }
        if (!v.cupom.empty() && !IsRectEmpty(&R.banCupom)) {
            const std::wstring s = TS(L"cupom ", L"coupon ") + v.cupom;
            SIZE m = MedeTexto(dc, F.tiny, s.c_str());
            RECT rc{ R.banCupom.left, R.banCupom.top, R.banCupom.left + m.cx + E(20), R.banCupom.bottom };
            if (rc.right < R.banner.right - E(12)) {
                StrokeRound(dc, rc, E(9), v.hoverCupom ? C_AMB : C_AMB_D);
                DrawText1(dc, F.tiny, v.hoverCupom ? C_TEXT : C_AMB_L, rc, s.c_str(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            }
        }
    }

    // ---- O RODAPE. O apoio ao canal cabe numa linha, com botoes neutros: quem veio usar o
    // programa nao tropeca neles, e quem quer retribuir sabe onde estao.
    {
        HBRUSH b = CreateSolidBrush(C_CARD);
        RECT sp = R.sep; FillRect(dc, &sp, b);
        DeleteObject(b);
        DrawText1(dc, F.tiny, C_MUTE, R.marca, T(L"Feito pelo PC do Fafa", L"Made by PC do Fafa"),
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        auto botaoRodape = [&](RECT rc, bool hover, const wchar_t* s) {
            if (hover) FillRound(dc, rc, E(9), C_SURF, 0, false);
            StrokeRound(dc, rc, E(9), hover ? C_CY_D : C_CARD);
            DrawText1(dc, F.micro, hover ? C_TEXT : C_BODY, rc, s, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        };
        if (!IsRectEmpty(&R.grupo))
            botaoRodape(R.grupo, v.hoverGrupo, RotuloGrupo(!v.cupom.empty()));
        if (!C.canal.empty()) botaoRodape(R.canal, v.hoverCanalRod, T(L"Apoiar o canal", L"Support the channel"));
        // o selo do cupom (publi remota): uma pilula pequena, encostada a direita
        if (!v.cupom.empty() && !IsRectEmpty(&R.cupom)) {
            const std::wstring s = TS(L"CUPOM ", L"COUPON ") + v.cupom;
            SIZE m = MedeTexto(dc, F.tiny, s.c_str());
            RECT rc{ R.cupom.right - m.cx - E(20), R.cupom.top, R.cupom.right, R.cupom.bottom };
            if (rc.left > R.canal.right + E(8)) {
                if (v.hoverCupom) FillRound(dc, rc, E(10), C_SURF, 0, false);
                StrokeRound(dc, rc, E(10), v.hoverCupom ? C_CY : C_CY_D);
                DrawText1(dc, F.tiny, v.hoverCupom ? C_TEXT : C_CY_L, rc, s.c_str(), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            }
        }
    }

    // ---- A ABA DA GAVETA, na lateral. Fechada, e a unica coisa que a vitrine ocupa na tela.
    if (!IsRectEmpty(&R.aba)) {
        RECT a = R.aba;
        FillRound(dc, a, E(8), v.hoverAba ? C_CARD : C_SURF, C_CARD, true);
        std::wstring rot = v.gaveta ? T(L"FECHAR", L"CLOSE") : T(L"OFERTAS", L"DEALS");
        if (!v.gaveta && v.vitrineNovas > 0)
            rot += L" · " + std::to_wstring(v.vitrineNovas) + T(L" novas", L" new");
        HFONT fr = CreateFontW(-E(11), 0, 900, 900, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, L"Segoe UI");
        HGDIOBJ of = SelectObject(dc, fr);
        SetTextColor(dc, v.hoverAba ? C_TEXT : C_CY_L);
        SIZE m{};
        GetTextExtentPoint32W(dc, rot.c_str(), (int)rot.size(), &m);
        TextOutW(dc, (a.left + a.right) / 2 - m.cy / 2, (a.top + a.bottom) / 2 + m.cx / 2,
                 rot.c_str(), (int)rot.size());
        SelectObject(dc, of);
        DeleteObject(fr);
    }

    // ---- O POPOVER DO QR. O convite do grupo (titulo, mensagem, chamada, QR e botao do
    // ofertas.ini) mora AQUI, e nao mais na tela principal: quem quiser entrar clica em
    // "Entrar no grupo de ofertas" e ve tudo; quem veio usar o programa nunca esbarra nele.
    if (v.popover) {
        // Escurece o que esta atras. Uma trama de xadrez de 1 px na cor do fundo, e nao um
        // AlphaBlend: AlphaBlend mora na msimg32, e este header tambem e compilado dentro do
        // main.cpp - nao vale obrigar o app inteiro a linkar uma biblioteca a mais por um
        // efeito de fundo. O xadrez cobre metade dos pixels e da o mesmo recado.
        {
            WORD trama[8] = { 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555, 0xAAAA, 0x5555 };
            HBITMAP bm = CreateBitmap(8, 8, 1, 1, trama);
            if (bm) {
                HBRUSH br = CreatePatternBrush(bm);
                if (br) {
                    const COLORREF ot = SetTextColor(dc, C_BG), ob2 = SetBkColor(dc, C_BG);
                    const int om = SetBkMode(dc, TRANSPARENT);
                    HGDIOBJ ob = SelectObject(dc, br);
                    PatBlt(dc, R.popFundo.left, R.popFundo.top + E(3),
                           R.popFundo.right - R.popFundo.left,
                           R.popFundo.bottom - R.popFundo.top - E(6), PATCOPY);
                    SelectObject(dc, ob);
                    SetTextColor(dc, ot); SetBkColor(dc, ob2); SetBkMode(dc, om);
                    DeleteObject(br);
                }
                DeleteObject(bm);
            }
        }

        FillRound(dc, R.popCard, E(16), C_SURF, C_CY_D, true);
        if (v.hoverPopFechar) FillRound(dc, R.popFechar, E(6), C_CARD, 0, false);
        DrawText1(dc, F.close_, v.hoverPopFechar ? C_TEXT : C_MUTE, R.popFechar, L"✕",
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        // O CARTAO E A FAIXA CONTAM A MESMA HISTORIA, em tamanhos diferentes: os dois saem de
        // TextoDaFaixa, entao em ingles o cartao tambem fala do canal, e o QR embaixo aponta
        // para o mesmo lugar que o botao. Um cartao que dissesse "grupo de ofertas" com um QR
        // do YouTube seria pior do que nao ter cartao nenhum.
        const TextoFaixa PX = TextoDaFaixa(C, v.tipoItem, v.cupom, v.publicado);
        DrawTextCaixa(dc, F.title, Maiuscula(PX.titulo).c_str(), R.popTitulo, C_TEXT,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS, 24, 15, FW_BOLD, dpi, E(1));
        DrawTextCaixa(dc, F.small_, PX.ehCanal ? PX.apoio.c_str() : C.mensagem.c_str(), R.popMsg, C_BODY,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS, 14, 10, FW_NORMAL, dpi);
        if (R.popQr.right > R.popQr.left) DrawQr(dc, R.popQr, *v.code, dpi);
        DrawText1(dc, F.addr, v.hoverPopQr ? C_CY_L : C_TEXT, R.popNome,
                  PX.ehCanal ? L"PC do Fafa" : C.nomeGrupo.c_str(),
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        DrawText1(dc, F.tiny, C_MUTE, R.popRodape, C.microcta.c_str(),
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        RECT pb = R.popBotao;
        if (v.hoverPopBotao) FillRoundGradient(dc, pb, E(12), C_CY, C_BLUE_L);
        else                 FillRoundGradient(dc, pb, E(12), C_CY_D, C_BLUE);
        DrawText1(dc, F.cta, C_TEXT, pb, Maiuscula(PX.botao).c_str(), DT_CENTER | DT_SINGLELINE | DT_VCENTER, E(1));
    }
    (void)h;
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
    const LauncherInfo* li = nullptr;   // modo launcher (ver LauncherInfo)
    bool hoverLingPt = false, hoverLingEn = false;   // os chips do idioma
    // ---- launcher: o que esta sob o mouse, o que esta escolhido e o que esta aberto
    bool hoverIniciar = false, hoverGrupo = false, hoverCanalRod = false,
         hoverAba = false, hoverNovidades = false, hoverPopBotao = false, hoverPopFechar = false,
         hoverPopQr = false, hoverBanner = false, hoverBanBotao = false, hoverBanOcultar = false;
    // O ICONE DO APP, no tamanho FISICO deste DPI. O .ico traz varias resolucoes e o
    // LoadImage escolhe/reduz a mais proxima, entao ele e recarregado quando o DPI muda -
    // esticar um icone de 96 px para 144 borraria justamente o lado pixel-art do desenho.
    HINSTANCE hinst = nullptr;
    HICON icone = nullptr;
    void CarregarIcone(UINT d) {
        if (icone) { DestroyIcon(icone); icone = nullptr; }
        if (!hinst) return;
        const int lado = Esc(ICONE_L, d);
        icone = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(1), IMAGE_ICON, lado, lado, LR_DEFAULTCOLOR);
        forma.icone = icone != nullptr;   // sem recurso de icone, o cabecalho volta a margem
    }
    FormaL forma{};
    int  selecao = 0;                   // indice na lista da FormaL
    bool gaveta = false, gavetaUsada = false, popover = false;
    bool vitrineCriada = false;         // o motor web ja foi montado (so na primeira abertura)
    Escolha escolha = ESC_FECHOU;       // o que a pessoa escolheu, lido depois do laco
    int fechadoPor = 0;                 // diagnostico: 1 X, 2 Esc, 3 Enter, 4 grupo, 5 canal, 6 nv, 7 amd, 8 sem, 9 WM_CLOSE, 12 cupom, 13 repetir
    // conteudo remoto (ver OfertaRemota) e cartao do relatorio (ver ResumoUso)
    bool remotoAplicado = false;        // o conteudo FRESCO ja entrou (o cache entra ao nascer)
    std::wstring cupom, cupomTexto;
    // o tipo do item que esta na faixa e quando ele foi publicado (ver OfertaRemota)
    int  tipoItem = ITEM_OFERTA;
    long long publicado = 0;
    // A TROCA DE OFERTA, animada. `animFaixa` cai de 1 a 0 em ANIM_MS; `animT0` e o instante
    // em que ela comecou. Com a animacao do sistema desligada (SPI_GETCLIENTAREAANIMATION) a
    // troca e seca: o conteudo novo aparece e pronto.
    double    animFaixa = 0.0;
    ULONGLONG animT0 = 0;
    int       geracaoVista = 0;   // qual oferta esta desenhada (ver LauncherInfo::geracaoRemoto)
    int       versaoVista = -1;   // o estado da verificacao de versao que a tela ja desenhou
    bool hoverCupom = false;
    bool cartao = false;
    int  resumoEstado = 0;              // 0 aguardando, 1 ok, 2 indisponivel
    const ResumoUso* resumo = nullptr;
    // a vitrine (ver VitrineHost): nula = uma coluna. colunaX = Esc(VW, dpi) quando ha.
    VitrineHost* vitrine = nullptr;
    int  colunaX = 0;
    bool layered = true;                // false com a vitrine: sem WS_EX_LAYERED, sem fade
};
inline int g_ultimoFechadoPor = 0;      // copia do fechadoPor da ultima tela (para o log de quem chama)
inline std::wstring g_ultimoCupom;      // o cupom que a pessoa clicou (para o log de quem chama)
// A gaveta da vitrine chegou a ficar aberta nesta abertura? E o que decide o evento
// abriu_vitrine (que antes valia por "a coluna esteve na tela") e o que vai para o log.
inline bool g_ultimaGavetaAberta = false;

inline View MakeView(Runtime& rt) {
    View v;
    v.cfg = &rt.cfg; v.code = &rt.code; v.fonts = &rt.fonts;
    v.dpi = rt.dpi;
    v.hoverCta = rt.hoverCta; v.hoverSec = rt.hoverSec; v.hoverClose = rt.hoverClose;
    v.hoverCanal = rt.hoverCanal;
    v.hoverLink = rt.hoverLink;
    v.pressed = rt.pressed;
    v.li = rt.li;
    v.hoverLingPt = rt.hoverLingPt; v.hoverLingEn = rt.hoverLingEn;
    v.cupom = rt.cupom; v.cupomTexto = rt.cupomTexto; v.hoverCupom = rt.hoverCupom;
    v.cartao = rt.cartao; v.resumoEstado = rt.resumoEstado; v.resumo = rt.resumo;
    v.colunaX = rt.colunaX;
    v.vitrineEstado = rt.vitrine ? rt.vitrine->estado.load() : 0;
    v.hoverIniciar = rt.hoverIniciar;
    v.hoverGrupo = rt.hoverGrupo; v.hoverCanalRod = rt.hoverCanalRod;
    v.hoverAba = rt.hoverAba; v.hoverNovidades = rt.hoverNovidades;
    v.hoverPopBotao = rt.hoverPopBotao; v.hoverPopFechar = rt.hoverPopFechar; v.hoverPopQr = rt.hoverPopQr;
    v.hoverBanner = rt.hoverBanner; v.hoverBanBotao = rt.hoverBanBotao;
    v.hoverBanOcultar = rt.hoverBanOcultar;
    v.remoto = rt.remotoAplicado;
    v.tipoItem = rt.tipoItem; v.publicado = rt.publicado;
    v.animFaixa = rt.animFaixa;
    v.icone = rt.icone;
    v.selecao = rt.selecao;
    v.gaveta = rt.gaveta; v.popover = rt.popover;
    if (rt.li) {
        v.versaoEstado = rt.li->versaoPronta ? rt.li->versaoPronta->load() : 2;
        if (v.versaoEstado == 1 && rt.li->versaoNova) v.versaoNova = *rt.li->versaoNova;
        v.vitrineNovas = rt.li->vitrineNovas ? rt.li->vitrineNovas->load() : -1;
    }
    return v;
}
// O layout do LAUNCHER desta janela: um lugar so, para o desenho, o hover e o clique nunca
// discordarem sobre onde as coisas estao. `w` e a largura da janela inteira; a coluna do
// produto comeca em rt->colunaX. O DC serve so para medir texto (largura dos botoes do
// rodape e dos chips) - e por isso e o MESMO numero no desenho e no clique.
inline RectsL LayoutLDe(HWND h, Runtime* rt) {
    RECT rc{}; GetClientRect(h, &rc);
    HDC dc = GetDC(h);
    const std::wstring rotBotao = Maiuscula(rt->cfg.botao);
    const RectsL R = LayoutL(rc.right - rt->colunaX, rt->dpi, rt->li, rt->forma, rt->colunaX,
                             rt->popover, !rt->cupom.empty(), dc, &rt->fonts, rotBotao.c_str());
    if (dc) ReleaseDC(h, dc);
    return R;
}
// A largura logica da janela do launcher com e sem a gaveta.
inline int LarguraLauncher(bool gaveta) { return SPWL + (gaveta ? VW : 0); }

// A ALTURA DA JANELA SAI DO CONTEUDO, entao toda vez que o conteudo muda de tamanho - a faixa
// foi oculta, o idioma mudou e as ofertas sumiram - a janela precisa ser remedida. A largura
// segue a gaveta (aberta ou fechada). Um lugar so para isso, porque errar aqui deixa a tela com
// um pedaco cortado ou uma tira preta embaixo.
inline void RedimensionarLauncher(HWND h, Runtime* rt) {
    if (!rt || !rt->li) return;
    HDC dcm = GetDC(h);
    const std::wstring rot = Maiuscula(rt->cfg.botao);
    const int larguraPx = Esc(LarguraLauncher(rt->gaveta), rt->dpi);
    const RectsL R = LayoutL(larguraPx - rt->colunaX, rt->dpi, rt->li, rt->forma, rt->colunaX,
                             rt->popover, !rt->cupom.empty(), dcm, &rt->fonts, rot.c_str());
    if (dcm) ReleaseDC(h, dcm);
    SetWindowPos(h, nullptr, 0, 0, larguraPx, R.alturaPx, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (rt->vitrine && rt->gaveta) rt->vitrine->Mover(RectVitrine(rt->dpi, R.alturaPx), 1.0);
}

// SO PARA TESTE (DLSS5_DIAG=1 + DLSS5_LOG_ITENS=1, a mesma dupla do painel no main.cpp):
// despeja num arquivo ao lado do exe uma linha por area clicavel da tela, com o retangulo
// em PIXEL e o centro. E o que permite conferir um clique pelo ponto que a tela RECEBEU, em
// vez de julgar pelo efeito - ver a nota de testar-botoes-sem-roubar-o-mouse. Nao muda nada
// do comportamento: so escreve.
inline void DespejarItensLauncher(HWND h, Runtime* rt) {
    wchar_t d[8]{}, e[8]{};
    if (!GetEnvironmentVariableW(L"DLSS5_DIAG", d, 8) || d[0] != L'1') return;
    if (!GetEnvironmentVariableW(L"DLSS5_LOG_ITENS", e, 8) || e[0] != L'1') return;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring p = exe;
    const size_t b = p.find_last_of(L'\\');
    p = (b == std::wstring::npos ? std::wstring() : p.substr(0, b + 1)) + L"itens-launcher.txt";
    FILE* f = _wfopen(p.c_str(), L"w, ccs=UTF-8");
    if (!f) return;
    const RectsL R = LayoutLDe(h, rt);
    RECT wr{}; GetWindowRect(h, &wr);
    fwprintf(f, L"janela tela=%ld,%ld %ldx%ld dpi=%u gaveta=%d popover=%d\n",
             wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, rt->dpi,
             (int)rt->gaveta, (int)rt->popover);
    auto linha = [&](const wchar_t* nome, const RECT& r) {
        if (IsRectEmpty(&r)) return;
        fwprintf(f, L"%-12ls %5ld,%5ld,%5ld,%5ld  centro=%ld,%ld\n", nome,
                 r.left, r.top, r.right, r.bottom, (r.left + r.right) / 2, (r.top + r.bottom) / 2);
    };
    linha(L"lingPt", R.lingPt);   linha(L"lingEn", R.lingEn);   linha(L"fechar", R.close_);
    linha(L"iniciar", R.iniciar); linha(L"novidades", R.novidades);
    linha(L"grupo", R.grupo);     linha(L"canal", R.canal);     linha(L"cupom", R.cupom);
    linha(L"aba", R.aba);         linha(L"popBotao", R.popBotao); linha(L"popFechar", R.popFechar);
    linha(L"popQr", R.popQr);     linha(L"banner", R.banner);   linha(L"bannerBotao", R.bannerBotao);
    linha(L"banOcultar", R.banOcultar); linha(L"banReativar", R.banReativar); linha(L"icone", R.icone);
    fclose(f);
}

// Poe o conteudo remoto por cima da Config ja montada (ini + padrao) e refaz o QR se o
// endereco mudou. Tudo o que chega aqui ja foi saneado e validado por quem preencheu.
// O QR desenha o endereco para onde a faixa leva AGORA - o grupo em portugues, o canal em
// ingles. Refeito a cada troca de idioma e a cada publi nova: um QR que aponta para outro
// lugar que o botao ao lado dele seria uma armadilha, nao um atalho.
inline void AtualizarQr(Runtime& rt) {
    if (!qr::EncodeText(AlvoDaFaixa(rt.cfg).c_str(), qr::ECC_M, rt.code)) rt.code = qr::Code();
}
inline void AplicarRemoto(Runtime& rt, const OfertaRemota& o) {
    if (!o.titulo.empty())   rt.cfg.titulo   = o.titulo;
    if (!o.mensagem.empty()) rt.cfg.mensagem = o.mensagem;
    if (!o.cta.empty())      rt.cfg.microcta = o.cta;
    if (!o.botao.empty())    rt.cfg.botao    = o.botao;
    if (!o.canal.empty() && UrlEhWeb(o.canal)) rt.cfg.canal = o.canal;
    if (!o.url.empty() && UrlEhWeb(o.url) && o.url != rt.cfg.url) {
        rt.cfg.url = o.url;
        AtualizarQr(rt);
    }
    rt.cupom = o.cupom; rt.cupomTexto = o.cupomTexto;
    rt.tipoItem = o.TipoEfetivo();
    rt.publicado = o.publicado;
}

// A pessoa clicou no outro idioma. Vale NA HORA: todo texto desta janela passa por T() no
// ponto de uso, entao basta trocar a global e repintar.
//
// A Config e a excecao, e por isso ela e remontada aqui: os padroes embutidos sao TS() lidos
// na CONSTRUCAO do struct, e as chaves de texto do ofertas.ini so valem no idioma em uso
// (ver ChaveDoIdioma) - se ficasse a Config antiga, o titulo, a mensagem e o botao seriam os
// unicos textos da tela a nao acompanhar a troca.
//
// O que veio da WEB nao volta a ser buscado: o ofertas.json ja chegou com o idioma escolhido
// no parse (atualizacao.h), entao o remoto e reaplicado como esta - ele continua valendo pelo
// endereco, pelo cupom e pela validade, e no maximo o texto dele fica na lingua da abertura.
// Rebuscar seria uma ida a rede no meio de um clique, e a tela nao espera rede por nada.
//
// TROCAR O IDIOMA MUDA A FORMA DA TELA, e nao so o texto: em ingles nao ha ofertas (ver
// OfertasValem), entao somem a coluna da vitrine, a aba lateral e o conteudo de oferta da
// faixa - e a janela inteira precisa ser remedida. Por isso este ponto refaz a FormaL, fecha
// a gaveta quando ela deixa de fazer sentido e redimensiona a janela, em vez de so repintar.
inline void RedimensionarLauncher(HWND h, Runtime* rt);
inline void TrocarIdioma(HWND h, Runtime* rt, Lingua nova) {
    if (!rt || nova == g_lingua) return;
    g_lingua = nova;
    rt->cfg = LoadConfig(Config());
    if (rt->li) {
        if (rt->li->remotoCache && !rt->li->remotoCache->Vazio()) AplicarRemoto(*rt, *rt->li->remotoCache);
        if (rt->remotoAplicado && rt->li->remoto)                 AplicarRemoto(*rt, *rt->li->remoto);
        if (rt->li->aoTrocarIdioma) rt->li->aoTrocarIdioma(nova);   // quem chama grava a escolha
        rt->forma = FormaDe(rt->li);
        rt->forma.icone = rt->icone != nullptr;
        AtualizarQr(*rt);            // o alvo mudou: o grupo em portugues, o canal em ingles
        // a coluna de ofertas nao existe em ingles: se estava aberta, fecha (e o motor web
        // some junto, sem ser destruido - reabrir na volta ao portugues nao custa segundos)
        if (!rt->forma.ofertas && rt->gaveta) {
            rt->gaveta = false;
            rt->colunaX = 0;
            if (rt->vitrine) rt->vitrine->Mostrar(false);
        }
        RedimensionarLauncher(h, rt);
    }
    InvalidateRect(h, nullptr, TRUE);
}

// Copia texto para a area de transferencia (o cupom, no clique do selo).
inline bool CopiarTexto(HWND h, const std::wstring& s) {
    if (!OpenClipboard(h)) return false;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    bool ok = false;
    if (g) {
        void* p = GlobalLock(g);
        if (p) { memcpy(p, s.c_str(), (s.size() + 1) * sizeof(wchar_t)); GlobalUnlock(g); ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr; }
        if (!ok) GlobalFree(g);
    }
    CloseClipboard();
    return ok;
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

// ============================================================== A FAIXA QUE SE ATUALIZA
// A faixa mostra sempre o item MAIS RECENTE do grupo, e ela se atualiza sozinha enquanto a
// janela esta aberta. Quem vai a rede e uma thread de quem chama (o launcher), pelo mesmo
// caminho do ofertas.json de sempre; esta tela so recebe o aviso e troca o que esta escrito.
//
// TRES REGRAS que valem a pena estar escritas:
//  - SO TROCA QUANDO MUDA. Quem avisa compara o conteudo (ver `assinatura` no launcher.cpp) e
//    so manda a mensagem quando ha algo diferente. Repintar a cada minuto uma faixa igual
//    seria um piscar sem motivo no canto da tela de quem esta lendo outra coisa.
//  - COM A FAIXA OCULTA, NINGUEM VAI A REDE. Quem desligou a faixa nao deve pagar tráfego por
//    ela; a aba lateral continua contando "N novas" porque esse numero vem de outro lugar.
//  - A ANIMACAO RESPEITA O SISTEMA. Com "mostrar animacoes no Windows" desligado
//    (SPI_GETCLIENTAREAANIMATION), a troca e seca. Quem desliga isso costuma ter um motivo -
//    enjoo de movimento, ou uma maquina que engasga - e nao e a nossa faixa que vai discutir.
static const UINT ANIM_MS = 350;    // a troca inteira; ~12 px de deslize e um fade
static const UINT TIMER_ANIM = 2;   // 1 e o fade de entrada da janela
inline bool AnimacoesLigadas() {
    BOOL b = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &b, 0)) return true;
    return b != FALSE;
}
// Chamada quando chega conteudo NOVO para a faixa: comeca a animacao (ou nao, se o sistema
// pediu para nao animar) e manda repintar.
inline void AnimarFaixa(HWND h, Runtime* rt) {
    if (!rt) return;
    if (!AnimacoesLigadas()) {
        rt->animFaixa = 0.0;
        InvalidateRect(h, nullptr, FALSE);
        return;
    }
    rt->animFaixa = 1.0;
    rt->animT0 = GetTickCount64();
    SetTimer(h, TIMER_ANIM, 16, nullptr);     // ~60 quadros por segundo
    InvalidateRect(h, nullptr, FALSE);
}

// A GAVETA DA VITRINE abre e fecha COM A JANELA ABERTA. A janela cresce (ou encolhe) PARA A
// ESQUERDA: a coluna do produto fica onde estava, debaixo do mouse e dos olhos de quem
// clicou. O motor web e montado na PRIMEIRA abertura e depois so troca de visibilidade -
// recriar o controle a cada clique custa segundos (duas aberturas seguidas na mesma pasta de
// cache levam 4-6 s, medido no teste da vitrine).
inline void AlternarGaveta(HWND h, Runtime* rt, bool abrir) {
    if (!rt || !rt->vitrine || rt->gaveta == abrir) return;
    RECT w{}; GetWindowRect(h, &w);
    const int novoOx = abrir ? Esc(VW, rt->dpi) : 0;
    const int larg = (w.right - w.left) - rt->colunaX + novoOx;
    int x = w.right - larg;
    // ... mas nunca para fora da area util do monitor onde ela esta
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) {
        if (x + larg > mi.rcWork.right) x = mi.rcWork.right - larg;
        if (x < mi.rcWork.left) x = mi.rcWork.left;
    }
    rt->gaveta = abrir;
    rt->colunaX = novoOx;
    SetWindowPos(h, nullptr, x, w.top, larg, w.bottom - w.top, SWP_NOZORDER | SWP_NOACTIVATE);
    RoundCorners(h, rt->dpi);
    RECT rc{}; GetClientRect(h, &rc);
    if (abrir) {
        rt->gavetaUsada = true;
        const double z = (double)rt->dpi / DpiDaJanela(h);
        if (!rt->vitrineCriada) { rt->vitrineCriada = true; rt->vitrine->Criar(h, RectVitrine(rt->dpi, rc.bottom), z); }
        else { rt->vitrine->Mover(RectVitrine(rt->dpi, rc.bottom), z); rt->vitrine->Mostrar(true); }
    } else {
        rt->vitrine->Mostrar(false);
    }
    if (rt->li && rt->li->aoTrocarGaveta) rt->li->aoTrocarGaveta(abrir);
    InvalidateRect(h, nullptr, TRUE);
}

// A rede respondeu (ou ja tinha respondido quando a janela nasceu): aplica o conteudo
// remoto fresco e/ou o resumo de uso, e repinta. Idempotente.
inline void AplicarAtualizacoes(HWND h, Runtime* rt) {
    if (!rt || !rt->li) return;
    bool mudou = false;
    // O CONTEUDO DA FAIXA CHEGOU, ou trocou. A primeira vez vem da abertura; as seguintes, do
    // polling (quem chama so avisa quando o conteudo e DIFERENTE - ver a nota em AnimarFaixa),
    // e cada uma delas anima a troca. `geracaoRemoto` e o que separa uma da outra: quem chama
    // o incrementa a cada oferta nova, e esta tela compara com a que ja desenhou.
    const int ger = rt->li->geracaoRemoto ? rt->li->geracaoRemoto->load() : 0;
    if (ger > rt->geracaoVista && rt->li->remoto && !rt->li->remoto->Vazio()) {
        AplicarRemoto(*rt, *rt->li->remoto);
        rt->geracaoVista = ger;
        rt->remotoAplicado = true;
        AnimarFaixa(h, rt);       // ela ja manda repintar
        return;
    }
    if (!rt->remotoAplicado && rt->li->remotoPronto && rt->li->remotoPronto->load() == 1 && rt->li->remoto) {
        AplicarRemoto(*rt, *rt->li->remoto);
        rt->remotoAplicado = true; mudou = true;
        AnimarFaixa(h, rt);
    }
    // A VERSAO NOVA PODE APARECER COM A JANELA JA ABERTA (o autor publica enquanto o launcher
    // esta na tela): a pilula ao lado do nome e a linha de status leem `versaoPronta` a cada
    // pintura, entao basta perceber que o sinal mudou e repintar. A caixa que PERGUNTA continua
    // sendo so depois do INICIAR - nada pula na frente de quem esta fazendo outra coisa.
    if (rt->li->versaoPronta) {
        const int e = rt->li->versaoPronta->load();
        if (e != rt->versaoVista) { rt->versaoVista = e; mudou = true; }
    }
    if (rt->cartao && rt->resumoEstado == 0 && rt->li->resumoPronto) {
        const int e = rt->li->resumoPronto->load();
        if (e != 0) { rt->resumoEstado = e; rt->resumo = rt->li->resumo; mudou = true; }
    }
    if (mudou) InvalidateRect(h, nullptr, FALSE);
}

// Um clique na tela do LAUNCHER. Cada caminho daqui mantem o evento anonimo que ja existia:
// o INICIAR (e o Repetir) viram clique_nvidia / clique_amd / clique_sem_dlss conforme a placa
// escolhida, o botao dentro do popover vira clique_grupo, o do rodape clique_canal e o selo
// clique_cupom - e quem chama continua lendo isso pelo par (escolha, fechadoPor).
inline void ClickLauncher(HWND h, Runtime* rt, POINT p) {
    const RectsL R = LayoutLDe(h, rt);
    const LauncherInfo* li = rt->li;
    auto abrirCom = [&](int opcao, int por) {
        rt->escolha = opcao == 0 ? ESC_NVIDIA : opcao == 1 ? ESC_AMD : ESC_SEM_DLSS;
        rt->fechadoPor = por;
        DestroyWindow(h);
    };
    // PARA ONDE A FAIXA LEVA depende do idioma: o grupo em portugues, o canal em ingles (ver
    // AlvoDaFaixa). O evento acompanha - clique no canal e `clique_canal`, no grupo e
    // `clique_grupo` -, senao a contagem do servidor diria que o grupo recebeu a visita de
    // quem na verdade foi para o YouTube.
    const bool faixaVaiAoCanal = !OfertasValem() && !rt->cfg.canal.empty();
    // ---- O POPOVER e modal: enquanto esta aberto, so ele responde.
    if (rt->popover) {
        if (PtInRect(&R.popBotao, p) || PtInRect(&R.popQr, p) || PtInRect(&R.popNome, p)) {
            // o mesmo destino da faixa que o abriu: grupo em portugues, canal em ingles
            OpenLink(AlvoDaFaixa(rt->cfg));
            rt->escolha = ESC_LINK; rt->fechadoPor = faixaVaiAoCanal ? 5 : 4;
            DestroyWindow(h); return;
        }
        // o X do cartao, ou um clique fora dele: volta para a tela (a janela NAO fecha - quem
        // so foi espiar o grupo nao perde o launcher)
        if (PtInRect(&R.popFechar, p) || !PtInRect(&R.popCard, p)) {
            rt->popover = false;
            InvalidateRect(h, nullptr, TRUE);
        }
        return;
    }
    // ---- A ACAO PRINCIPAL
    if (PtInRect(&R.iniciar, p)) {
        const int o = rt->forma.opc[rt->selecao];
        abrirCom(o, o == 0 ? 6 : o == 1 ? 7 : 8);
        return;
    }
    // ---- A GAVETA
    if (PtInRect(&R.aba, p)) { AlternarGaveta(h, rt, !rt->gaveta); return; }
    // ---- OCULTAR / REATIVAR A FAIXA. A tela fica mais baixa (ou mais alta) na hora, e quem
    // chamou grava a escolha - na proxima abertura ela continua valendo.
    if (PtInRect(&R.banOcultar, p) || PtInRect(&R.banReativar, p)) {
        LauncherInfo* mut = const_cast<LauncherInfo*>(li);
        mut->faixaVisivel = !li->faixaVisivel;
        rt->forma = FormaDe(li);
        rt->forma.icone = rt->icone != nullptr;
        if (li->aoTrocarFaixa) li->aoTrocarFaixa(li->faixaVisivel);
        RedimensionarLauncher(h, rt);   // a altura sai do conteudo: sem a faixa ela encolhe
        InvalidateRect(h, nullptr, TRUE);
        return;
    }
    // ---- A FAIXA, e a diferenca entre os seus dois alvos (ver faixaVaiAoCanal, no alto).
    //
    // O BOTAO AMBAR e um pedido explicito: abre o grupo no navegador e, como todo ESC_LINK,
    // fecha a janela (o evento enviado e clique_grupo, o mesmo do botao do rodape - ver o
    // switch de `e` no launcher.cpp).
    //
    // O RESTO DA FAIXA abre o POPOVER, e nao o navegador. A faixa inteira e clicavel porque
    // um alvo grande e facil de acertar (lei de Fitts) - e exatamente por isso ela e facil
    // de acertar SEM QUERER, e um clique sem querer que fecha o launcher, abre o navegador e
    // ainda inicia o programa e o tipo de coisa que faz a pessoa desinstalar o app. Entao o
    // alvo grande custa o de sempre: nada fecha, e o cartao com o QR grande aparece por cima
    // para quem quiser continuar.
    if (PtInRect(&R.bannerBotao, p)) {
        OpenLink(AlvoDaFaixa(rt->cfg));
        rt->escolha = ESC_LINK; rt->fechadoPor = faixaVaiAoCanal ? 5 : 4;
        DestroyWindow(h); return;
    }
    // A PILULA DO CUPOM copia o codigo e abre o grupo, igual a do rodape (fechadoPor 12 =
    // clique_cupom). Ela so existe quando ha cupom de verdade, entao nao ha o que proteger.
    if (!rt->cupom.empty() && PtInRect(&R.banCupom, p)) {
        CopiarTexto(h, rt->cupom);
        g_ultimoCupom = rt->cupom;
        OpenLink(rt->cfg.url); rt->escolha = ESC_LINK; rt->fechadoPor = 12; DestroyWindow(h); return;
    }
    if (PtInRect(&R.banner, p)) { rt->popover = true; InvalidateRect(h, nullptr, TRUE); return; }
    // ---- O RODAPE
    if (PtInRect(&R.grupo, p)) { rt->popover = true; InvalidateRect(h, nullptr, TRUE); return; }
    if (!rt->cfg.canal.empty() && PtInRect(&R.canal, p)) {
        OpenLink(rt->cfg.canal); rt->escolha = ESC_LINK; rt->fechadoPor = 5; DestroyWindow(h); return;
    }
    if (!rt->cupom.empty() && PtInRect(&R.cupom, p)) {
        CopiarTexto(h, rt->cupom);
        g_ultimoCupom = rt->cupom;
        OpenLink(rt->cfg.url); rt->escolha = ESC_LINK; rt->fechadoPor = 12; DestroyWindow(h); return;
    }
    if (!rt->cfg.urlNovidades.empty() && PtInRect(&R.novidades, p)) { OpenLink(rt->cfg.urlNovidades); return; }
    // ---- cabecalho: idioma (a janela continua aberta) e o X
    if (PontoIdioma(R, p)) { TrocarIdioma(h, rt, PtInRect(&R.lingPt, p) ? LINGUA_PT : LINGUA_EN); return; }
    if (PtInRect(&R.close_, p)) { rt->fechadoPor = 1; DestroyWindow(h); return; }
    InvalidateRect(h, nullptr, FALSE);   // area morta: clique ali nao faz nada
}

inline LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Runtime* rt = (Runtime*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_OFERTAS_ATUALIZA:
        AplicarAtualizacoes(h, rt);
        if (rt && rt->vitrine) InvalidateRect(h, nullptr, FALSE);   // o motor web tambem acorda por aqui (estado da pagina)
        return 0;
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
        // O TIMER DA TROCA DE OFERTA: `animFaixa` cai de 1 a 0 em ANIM_MS e o timer morre no
        // fim. Ele nao depende de a janela ser em camadas (o fade da faixa e desenhado, nao
        // e transparencia de janela), por isso vem ANTES da checagem do timer 1.
        if (wp == TIMER_ANIM) {
            if (!rt) { KillTimer(h, TIMER_ANIM); return 0; }
            const ULONGLONG dt = GetTickCount64() - rt->animT0;
            rt->animFaixa = dt >= ANIM_MS ? 0.0 : (1.0 - (double)dt / ANIM_MS);
            // desacelera no fim (o mesmo "ease-out" de qualquer animacao de interface: o
            // movimento chega devagar em vez de parar de repente)
            rt->animFaixa *= rt->animFaixa;
            if (rt->animFaixa <= 0.0) { rt->animFaixa = 0.0; KillTimer(h, TIMER_ANIM); }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (!rt || !rt->layered) { KillTimer(h, 1); return 0; }   // sem camada (vitrine) nao ha fade
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
        if (rt->li) {
            const RectsL R = LayoutLDe(h, rt);
            // o estado de antes, so para saber se vale repintar
            const bool a0 = rt->hoverIniciar, a2 = rt->hoverGrupo,
                       a3 = rt->hoverCanalRod, a4 = rt->hoverAba, a5 = rt->hoverNovidades,
                       a6 = rt->hoverCupom, a7 = rt->hoverClose, a8 = rt->hoverLingPt,
                       a9 = rt->hoverLingEn, a10 = rt->hoverPopBotao, a11 = rt->hoverPopFechar,
                       a12 = rt->hoverPopQr, a14 = rt->hoverBanner, a15 = rt->hoverBanBotao,
                       a16 = rt->hoverBanOcultar;
            // COM O POPOVER ABERTO nada atras dele responde ao mouse: um cartao modal que
            // deixa o que esta embaixo acender e um cartao que parece quebrado.
            const bool pop = rt->popover;
            rt->hoverPopBotao  = pop && PtInRect(&R.popBotao, p);
            rt->hoverPopFechar = pop && PtInRect(&R.popFechar, p);
            rt->hoverPopQr     = pop && (PtInRect(&R.popQr, p) || PtInRect(&R.popNome, p));
            rt->hoverIniciar = !pop && PtInRect(&R.iniciar, p);
            rt->hoverGrupo   = !pop && PtInRect(&R.grupo, p);
            rt->hoverCanalRod = !pop && !rt->cfg.canal.empty() && PtInRect(&R.canal, p);
            rt->hoverAba     = !pop && PtInRect(&R.aba, p);
            rt->hoverBanner  = !pop && PtInRect(&R.banner, p);
            rt->hoverBanBotao = rt->hoverBanner && PtInRect(&R.bannerBotao, p);
            rt->hoverBanOcultar = !pop && (PtInRect(&R.banOcultar, p) || PtInRect(&R.banReativar, p));
            rt->hoverNovidades = !pop && !rt->cfg.urlNovidades.empty() && PtInRect(&R.novidades, p);
            rt->hoverCupom   = !pop && !rt->cupom.empty() &&
                               (PtInRect(&R.cupom, p) || PtInRect(&R.banCupom, p));
            rt->hoverClose   = !pop && PtInRect(&R.close_, p);
            rt->hoverLingPt  = !pop && g_lingua != LINGUA_PT && PtInRect(&R.lingPt, p);
            rt->hoverLingEn  = !pop && g_lingua != LINGUA_EN && PtInRect(&R.lingEn, p);
            if (!rt->hover || a0 != rt->hoverIniciar ||
                a2 != rt->hoverGrupo || a3 != rt->hoverCanalRod || a4 != rt->hoverAba ||
                a5 != rt->hoverNovidades || a6 != rt->hoverCupom || a7 != rt->hoverClose ||
                a8 != rt->hoverLingPt || a9 != rt->hoverLingEn ||
                a10 != rt->hoverPopBotao || a11 != rt->hoverPopFechar || a12 != rt->hoverPopQr ||
                a14 != rt->hoverBanner || a15 != rt->hoverBanBotao || a16 != rt->hoverBanOcultar) {
                rt->hover = true;
                InvalidateRect(h, nullptr, FALSE);
            }
        } else {
            Rects R = Layout(rc.right, rc.bottom, rt->dpi);
            // O botao do canal so responde quando existe endereco: sem a chave `canal` no
            // ofertas.ini ele nao e desenhado, e area que nao aparece nao pode ser clicavel.
            bool hc = PtInRect(&R.cta, p) != 0, hs = PtInRect(&R.sec, p) != 0,
                 hn = !rt->cfg.canal.empty() && PtInRect(&R.canal, p) != 0,
                 hx = PtInRect(&R.close_, p) != 0,
                 hl = (PtInRect(&R.moldura, p) || PtInRect(&R.addr, p)) != 0;
            bool hk = !rt->cupom.empty() && PtInRect(&R.selo, p);
            if (hc != rt->hoverCta || hs != rt->hoverSec || hn != rt->hoverCanal ||
                hx != rt->hoverClose || hl != rt->hoverLink || !rt->hover || hk != rt->hoverCupom) {
                rt->hoverCta = hc; rt->hoverSec = hs; rt->hoverCanal = hn;
                rt->hoverClose = hx; rt->hoverLink = hl; rt->hover = true; rt->hoverCupom = hk;
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        // SO PARA TESTE (DLSS5_DIAG=1 + DLSS5_SEGURA_HOVER=1): nao apaga o realce. Fotografar
        // um estado de hover sem mover o cursor DE VERDADE e impossivel de outro jeito - o
        // WM_MOUSEMOVE postado acende o realce e o TrackMouseEvent manda o WM_MOUSELEAVE no
        // mesmo instante, porque o cursor real esta noutro lugar. E o cursor real e do usuario,
        // que esta jogando enquanto estas fotos sao tiradas.
        {
            wchar_t d[8]{}, b[8]{};
            if (GetEnvironmentVariableW(L"DLSS5_DIAG", d, 8) && d[0] == L'1' &&
                GetEnvironmentVariableW(L"DLSS5_SEGURA_HOVER", b, 8) && b[0] == L'1') return 0;
        }
        if (rt) {
            rt->hover = rt->hoverCta = rt->hoverCanal = rt->hoverSec = rt->hoverClose = rt->hoverLink = rt->pressed = false;
            rt->hoverCupom = rt->hoverLingPt = rt->hoverLingEn = false;
            rt->hoverIniciar = rt->hoverGrupo = rt->hoverCanalRod = false;
            rt->hoverAba = rt->hoverNovidades = rt->hoverPopBotao = rt->hoverPopFechar = rt->hoverPopQr = false;
            rt->hoverBanner = rt->hoverBanBotao = rt->hoverBanOcultar = false;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_SETCURSOR: {
        // maozinha so onde realmente clica; no resto o cursor continua seta
        if (!rt) { SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE; }
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        RECT rc; GetClientRect(h, &rc);
        bool clicavel = false;
        if (rt->li) {
            const RectsL R = LayoutLDe(h, rt);
            if (rt->popover) {
                clicavel = PtInRect(&R.popBotao, p) || PtInRect(&R.popFechar, p) ||
                           PtInRect(&R.popQr, p) || PtInRect(&R.popNome, p);
            } else {
                clicavel = PtInRect(&R.iniciar, p) || PtInRect(&R.close_, p) ||
                           PtInRect(&R.grupo, p) || PtInRect(&R.aba, p) || PtInRect(&R.banner, p) ||
                           PtInRect(&R.banOcultar, p) || PtInRect(&R.banReativar, p) ||
                           (!rt->cfg.canal.empty() && PtInRect(&R.canal, p)) ||
                           (!rt->cfg.urlNovidades.empty() && PtInRect(&R.novidades, p)) ||
                           (!rt->cupom.empty() && PtInRect(&R.cupom, p));
                // os chips da placa: so o que esta aceso; e o do idioma que ja vale nao clica
                if (PontoIdioma(R, p) && !(g_lingua == LINGUA_PT ? PtInRect(&R.lingPt, p) : PtInRect(&R.lingEn, p)))
                    clicavel = true;
            }
        } else {
            Rects R = Layout(rc.right, rc.bottom, rt->dpi);
            clicavel = PontoAbreLink(R, p) || PontoFecha(R, p) ||
                       (!rt->cupom.empty() && PtInRect(&R.selo, p));
        }
        SetCursor(LoadCursor(nullptr, clicavel ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        if (!rt) return 0;
        POINT p{ GET_X_LPARAM_SAFE(lp), GET_Y_LPARAM_SAFE(lp) };
        RECT rc; GetClientRect(h, &rc);
        if (rt->li) {
            const RectsL R = LayoutLDe(h, rt);
            rt->pressed = !rt->popover && PtInRect(&R.iniciar, p) != 0;
        } else {
            Rects R = Layout(rc.right, rc.bottom, rt->dpi);
            rt->pressed = PtInRect(&R.cta, p) != 0;
        }
        if (rt->pressed) { SetCapture(h); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!rt) return 0;
        if (GetCapture() == h) ReleaseCapture();
        rt->pressed = false;
        POINT p{ GET_X_LPARAM_SAFE(lp), GET_Y_LPARAM_SAFE(lp) };
        RECT rc; GetClientRect(h, &rc);
        if (rt->li) { ClickLauncher(h, rt, p); return 0; }
        Rects R = Layout(rc.right, rc.bottom, rt->dpi);
        // o selo do CUPOM: copia o codigo para a area de transferencia e abre o link do
        // grupo (a pessoa chega la com o cupom pronto para colar); fecha como o botao
        if (!rt->cupom.empty() && PtInRect(&R.selo, p)) {
            CopiarTexto(h, rt->cupom);
            g_ultimoCupom = rt->cupom;
            OpenLink(rt->cfg.url); rt->escolha = ESC_LINK; rt->fechadoPor = 12; DestroyWindow(h); return 0;
        }
        if (PontoAbreLink(R, p)) { OpenLink(rt->cfg.url); rt->escolha = ESC_LINK; rt->fechadoPor = 4; DestroyWindow(h); return 0; }
        // O canal abre no navegador E FECHA a janela, igual ao grupo: a pessoa ja escolheu
        // para onde ir, e deixar a tela aberta atras do navegador so daria trabalho.
        if (!rt->cfg.canal.empty() && PtInRect(&R.canal, p)) {
            OpenLink(rt->cfg.canal); rt->escolha = ESC_LINK; rt->fechadoPor = 5; DestroyWindow(h); return 0;
        }
        if (PontoFecha(R, p))    { rt->fechadoPor = 1; DestroyWindow(h); return 0; }
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
        if (rt->li) rt->CarregarIcone(novo);   // o icone e recarregado, nunca esticado
        rt->colunaX = (rt->vitrine && rt->gaveta) ? Esc(VW, novo) : 0;
        const RECT* s = (const RECT*)lp;
        if (rt->li) {
            // O layout do launcher nao e proporcional (a altura sai do conteudo, e o conteudo
            // e medido em texto): o retangulo sugerido pelo Windows serve para a POSICAO, o
            // tamanho vem do layout refeito no DPI novo.
            HDC dcm = GetDC(h);
            const std::wstring rotBotao = Maiuscula(rt->cfg.botao);
            const RectsL R = LayoutL(Esc(SPWL, novo), novo, rt->li, rt->forma, rt->colunaX,
                                     rt->popover, !rt->cupom.empty(), dcm, &rt->fonts, rotBotao.c_str());
            if (dcm) ReleaseDC(h, dcm);
            RECT w0{}; GetWindowRect(h, &w0);
            SetWindowPos(h, nullptr, s ? s->left : w0.left, s ? s->top : w0.top,
                         Esc(LarguraLauncher(rt->gaveta), novo), R.alturaPx,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        } else if (s) {
            SetWindowPos(h, nullptr, s->left, s->top, s->right - s->left, s->bottom - s->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        RoundCorners(h, novo);
        if (rt->vitrine && rt->gaveta) {
            RECT rc; GetClientRect(h, &rc);
            rt->vitrine->Mover(RectVitrine(novo, rc.bottom), (double)novo / DpiDaJanela(h));
        }
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            // Com o popover aberto, Esc fecha SO o popover: quem foi espiar o grupo nao
            // perde o launcher por apertar a tecla de voltar.
            if (rt && rt->popover) { rt->popover = false; InvalidateRect(h, nullptr, TRUE); return 0; }
            if (rt) rt->fechadoPor = 2;
            DestroyWindow(h); return 0;
        }
        if (wp == VK_RETURN || wp == VK_SPACE) {
            if (rt) rt->fechadoPor = 3;
            if (rt && rt->popover) { OpenLink(AlvoDaFaixa(rt->cfg)); rt->escolha = ESC_LINK; rt->fechadoPor = 4; DestroyWindow(h); return 0; }
            // No launcher, Enter e o INICIAR: abre com a placa que esta no seletor.
            if (rt && rt->li) {
                const int o = rt->forma.opc[rt->selecao];
                rt->escolha = o == 0 ? ESC_NVIDIA : o == 1 ? ESC_AMD : ESC_SEM_DLSS;
                DestroyWindow(h); return 0;
            }
            if (rt) { OpenLink(rt->cfg.url); rt->escolha = ESC_LINK; }
            DestroyWindow(h); return 0;
        }
        return 0;
    case WM_CLOSE: if (rt) rt->fechadoPor = 9; DestroyWindow(h); return 0;
    case WM_DESTROY:
        if (rt && rt->vitrine) rt->vitrine->Fechar();   // o motor web solta a janela filha antes de a mae sumir
        PostQuitMessage(0);
        return 0;
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

// O miolo comum da tela classica e do launcher: `li` nulo = classica.
inline ofertas::detail::Escolha ShowSplashImpl(HINSTANCE hi, ofertas::Config base, const SplashOpcoes& op,
                                               const ofertas::detail::LauncherInfo* li) {
    using namespace ofertas;
    using namespace ofertas::detail;

    Runtime rt;
    rt.hinst = hi;                  // de onde sai o icone do app (recurso 1 do exe)
    rt.li = li;
    rt.cartao = li && li->cartao;               // o cartao do relatorio (copia do autor) ja nasce com a tela
    rt.cfg = LoadConfig(base);                  // conteudo: o ini vence o argumento
    if (li) {
        rt.forma = FormaDe(li);
        // A PLACA QUE JA VEM ESCOLHIDA: a da ultima vez, se ainda servir; senao a primeira
        // que estiver acesa. Ninguem precisa escolher nada para o INICIAR funcionar.
        rt.selecao = 0;
        for (int i = 0; i < rt.forma.n; i++)
            if (OpcaoAcesa(li, rt.forma.opc[i])) { rt.selecao = i; break; }
        // NADA DE "a ultima escolha manda" AQUI. A ordem e fixa (NVIDIA pronta, senao AMD
        // pronta, senao sem DLSS 5) porque o launcher deixou de perguntar: se ele abrisse pelo
        // AMD so porque a sessao passada foi AMD, a pessoa nao teria onde ver isso nem como
        // desfazer - nem o seletor nem o cartao "continuar de onde parou" existem mais.
    }
    AtualizarQr(rt);
    // conteudo remoto: o cache do ultimo ofertas.json valido entra ja (sem esperar a rede);
    // o fresco entra quando chegar (AplicarAtualizacoes), por cima
    if (li && li->remotoCache && !li->remotoCache->Vazio()) AplicarRemoto(rt, *li->remotoCache);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"DLSS5ForAllOfertas";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);   // WM_SETCURSOR troca pra maozinha onde clica
    RegisterClassExW(&wc);

    // A vitrine (coluna da esquerda) so no launcher, e so se couber. DLSS5_VITRINE_ESTREITO=1
    // finge um monitor estreito, para conferir a queda para uma coluna sem trocar de monitor.
    VitrineHost* vh = li ? li->vitrine : nullptr;
    {
        wchar_t e[4]{};
        if (vh && GetEnvironmentVariableW(L"DLSS5_VITRINE_ESTREITO", e, 4) && e[0] == L'1') { vh->motivoOff = L"monitor_estreito(forcado)"; vh = nullptr; }
    }

    // A GAVETA NASCE ABERTA? Quem decide e quem chama (primeiro uso, a batida de N
    // aberturas, ou a escolha guardada no ini da raiz); aqui so se obedece - e so se houver
    // vitrine e ela couber na tela.
    // Na variacao VITRINE a coluna de ofertas nao e uma gaveta: ela faz parte da tela e nasce
    // sempre aberta (e sem aba). Sem motor web - sem rede, sem WebView2, monitor estreito -
    // `vh` e nulo e a tela cai sozinha na variacao FAIXA, que e a mesma coluna do produto.
    bool gaveta0 = vh && (rt.forma.layout == LAY_VITRINE || (li && li->gavetaAberta));

    // Onde a janela nasce E em que escala. As medidas sao unidades logicas: num monitor a
    // 150% a janela tem metade a mais de pixel, entao o "cabe na area do alvo?" so pode ser
    // decidido depois de saber o DPI do monitor daquela area. A altura do launcher nao e
    // uma constante: ela sai do LayoutL, que conta o que a tela tem (seletor, cartao da
    // ultima sessao, explicacao).
    RECT wa{};
    UINT dpi = 96;
    int W = SPW, H = SPH;
    auto alturaLauncher = [&](UINT d) {
        HDC dcm = GetDC(nullptr);
        Fonts f; f.make(d);
        HGDIOBJ oo = dcm ? SelectObject(dcm, f.tiny) : nullptr;
        const std::wstring rotBotao = Maiuscula(rt.cfg.botao);
        const RectsL R = LayoutL(Esc(SPWL, d), d, li, rt.forma, 0, false, false, dcm, &f,
                                 rotBotao.c_str());
        if (dcm) { if (oo) SelectObject(dcm, oo); ReleaseDC(nullptr, dcm); }
        f.free();
        return R.alturaPx;
    };
    auto medir = [&](RECT area, bool comVitrine) {   // deixa wa/dpi/W/H prontos; diz se cabe
        wa = area;
        if (wa.right - wa.left <= 0 || wa.bottom - wa.top <= 0)
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        POINT centro{ (wa.left + wa.right) / 2, (wa.top + wa.bottom) / 2 };
        dpi = DpiEfetivoPonto(centro);
        if (li) { W = Esc(LarguraLauncher(comVitrine), dpi); H = alturaLauncher(dpi); }
        else    { W = Esc(SPW, dpi);                         H = Esc(SPH, dpi); }
        return wa.right - wa.left >= W && wa.bottom - wa.top >= H;
    };
    // Como sempre: primeiro a area pedida, depois a area de trabalho. Se nem a area de
    // trabalho comporta a janela com a gaveta aberta, a gaveta nasce FECHADA (a aba continua
    // la; abrir depois passa pelo mesmo limite do monitor, em AlternarGaveta).
    bool cabe = medir(op.area, gaveta0) || medir(RECT{}, gaveta0);
    if (!cabe && gaveta0) {
        gaveta0 = false;
        cabe = medir(op.area, false) || medir(RECT{}, false);
    }
    // Sem espaco nem para a coluna do produto sozinha, a vitrine sai de vez (a aba ocuparia
    // um espaco que nao existe) - e o motivo vai para o log de quem chama.
    if (!cabe && vh) { vh->motivoOff = L"monitor_estreito"; vh = nullptr; gaveta0 = false; }
    rt.vitrine = vh;
    rt.gaveta = gaveta0;
    rt.gavetaUsada = gaveta0;
    rt.layered = !vh;                 // ver VitrineHost: janela filha web nao vive em janela em camadas
    int x = wa.left + (wa.right - wa.left - W) / 2;
    int y = wa.top + (wa.bottom - wa.top - H) / 2;
    if (x < wa.left) x = wa.left;     // tela menor que o cartao: prefere cortar embaixo/direita
    if (y < wa.top)  y = wa.top;

    // TITULO, nao string vazia. Esta e a PRIMEIRA coisa que aparece quando alguem da
    // duplo clique no exe: um cartao topmost com QR e um botao "QUERO AS OFERTAS", sem
    // nome, sem entrada na barra de tarefas. E a assinatura visual de um adware. O nome do
    // app no titulo (e no rodape do cartao, logo abaixo) e o que separa uma coisa da outra.
    // WS_CLIPCHILDREN: com a vitrine ha uma janela filha (o motor web) e o nosso BitBlt nao
    // pode pintar por cima dela; sem filha o estilo nao muda nada.
    HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | (rt.layered ? WS_EX_LAYERED : 0),
                             wc.lpszClassName, L"DLSS5 For All", WS_POPUP | WS_CLIPCHILDREN, x, y, W, H,
                             nullptr, nullptr, hi, nullptr);
    if (!h) return ESC_FECHOU;

    // O monitor onde a janela realmente nasceu pode nao ser o que calculamos (o Windows
    // pode ter mexido na posicao). Vale o DPI dela, nao o da nossa conta.
    UINT real = DpiEfetivoJanela(h);
    if (real != dpi) {
        dpi = real;
        if (li) { W = Esc(LarguraLauncher(gaveta0), dpi); H = alturaLauncher(dpi); }
        else    { W = Esc(SPW, dpi);                      H = Esc(SPH, dpi); }
        SetWindowPos(h, nullptr, 0, 0, W, H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    rt.dpi = dpi;
    rt.colunaX = gaveta0 ? Esc(VW, dpi) : 0;
    rt.fonts.make(dpi);
    if (li) rt.CarregarIcone(dpi);   // so o launcher tem cabecalho; a tela classica nao
    rt.t0 = GetTickCount64();
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)&rt);
    if (rt.layered) SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
    if (op.esconderDaCaptura) HideFromCaptureLocal(h);   // ver SplashOpcoes
    RoundCorners(h, dpi);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(h);
    UpdateWindow(h);
    if (li) DespejarItensLauncher(h, &rt);   // so com DLSS5_DIAG=1 + DLSS5_LOG_ITENS=1
    // O timer so faz a entrada suave. Se ele nao nascer (fila de timers do processo
    // cheia), o alpha ficaria em 0 pra sempre: uma janela invisivel, topmost, clicavel e
    // sem como fechar. Sem fade, entao, mas visivel. (Sem camada nao ha fade nenhum.)
    if (rt.layered && !SetTimer(h, 1, 40, nullptr))
        SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);
    if (li) {
        // publica a janela para as threads da rede e SO ENTAO consulta o que ja ficou
        // pronto no meio do caminho (a ordem e o que fecha a corrida; ver LauncherInfo)
        g_janelaLauncher.store(h);
        AplicarAtualizacoes(h, &rt);
    }
    // A pagina da vitrine entra por ultimo, com a janela ja na tela: o motor web e
    // assincrono e avisa (WM_OFERTAS_ATUALIZA) quando carregou ou falhou. O zoom corrige o
    // caso em que o DPI desta janela nao e o que o processo enxerga (DLSS5_SPLASH_DPI de
    // teste, ou processo sem consciencia de DPI): a pagina acompanha o resto da tela.
    // Com a GAVETA FECHADA o motor nem e montado: quem nunca abrir a gaveta nunca paga o
    // preco de subir o WebView2 (e a pasta cache\webview2 nem chega a ser criada).
    if (vh && gaveta0) {
        rt.vitrineCriada = true;
        vh->Criar(h, RectVitrine(dpi, H), (double)dpi / DpiDaJanela(h));
    }

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }

    g_janelaLauncher.store(nullptr);
    KillTimer(h, 1);
    if (rt.icone) { DestroyIcon(rt.icone); rt.icone = nullptr; }
    rt.fonts.free();
    UnregisterClassW(wc.lpszClassName, hi);
    g_ultimoFechadoPor = rt.fechadoPor;
    g_ultimaGavetaAberta = rt.gavetaUsada;
    return rt.escolha;
}

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
    ShowSplashImpl(hi, base, op, nullptr);
}

// A MESMA tela como launcher: devolve o que a pessoa escolheu (ver Escolha). ESC_LINK
// quer dizer que ela abriu o grupo ou o canal - quem chama decide se o app abre assim
// mesmo (abre: e o comportamento de sempre da tela classica).
inline ofertas::detail::Escolha ShowLauncherSplash(HINSTANCE hi, const SplashOpcoes& op,
                                                   const ofertas::detail::LauncherInfo& info) {
    return ShowSplashImpl(hi, ofertas::Config(), op, &info);
}

// assinatura antiga, mantida para nao quebrar quem ja chamava
inline void ShowOfferSplash(HINSTANCE hi, const wchar_t* url, const wchar_t* titulo,
                            const wchar_t* msg, int segundos) {
    ShowOfferSplash(hi, url, titulo, msg, segundos, SplashOpcoes{});
}
