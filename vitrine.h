// =====================================================================================
//  vitrine.h - a VITRINE DE OFERTAS dentro da tela do launcher: uma pagina web
//  (https://grupo.pcdofafa.com.br/app-ofertas, uma rota da landing do grupo; ver
//  kUrlVitrinePadrao) hospedada na coluna da esquerda pelo WebView2 (o motor do Edge,
//  "Evergreen Runtime", que ja vem no Windows 11 e na maioria dos Windows 10).
//
//  O que este arquivo faz, e so isso:
//    - descobre se o runtime existe (WebView2Loader.dll ao lado do launcher, carregada com
//      LoadLibrary: sem lib de import, entao o launcher continua abrindo onde nao ha runtime);
//    - cria o ambiente (pasta de dados em <raiz>\cache\webview2\, nunca ao lado de um exe do
//      sistema) e o controle DENTRO da janela da tela, no retangulo que ela reservou;
//    - deixa a pagina "so pagina": sem menu de contexto, sem DevTools, sem zoom por
//      Ctrl+roda, sem barra de status, sem atalhos do navegador, sem pagina de erro do Edge;
//    - REDUZ a pagina ao feed de ofertas (ver ScriptSoOfertas abaixo): a coluna e o feed do
//      Telegram e o cabecalho do grupo, mais nada;
//    - links target=_blank e QUALQUER navegacao de topo que nao seja a propria rota da
//      vitrine (inclusive /join?... no mesmo dominio, que e o redirecionamento para o
//      Telegram) abrem no navegador padrao (ShellExecute), nunca dentro da coluna;
//    - Esc dentro da pagina fecha a tela, como Esc fora dela;
//    - avisa a tela (WM_OFERTAS_ATUALIZA) quando a pagina carregou ou falhou (estado).
//
//  Quem decide SE a coluna existe e o launcher.cpp (url_base, vitrine=1, runtime, rede em
//  ate 1,5 s); quem decide se CABE e a tela (monitor estreito). Este arquivo so hospeda.
//
//  COM sem ATL/WRL: os "handlers" do WebView2 sao objetos COM com um unico metodo Invoke;
//  Cb<> abaixo implementa IUnknown + Invoke por cima de uma lambda. Os IIDs vem escritos
//  aqui porque o MinGW nao tem __uuidof para as interfaces do SDK (o WebView2.h e gerado
//  pelo MIDL para o MSVC); sao os GUIDs do proprio WebView2.h, que nao mudam entre versoes.
//
//  Teste: DLSS5_SEM_WEBVIEW=1 finge que nao ha runtime; DLSS5_VITRINE_LOG_LINKS=1 faz o
//  "abrir no navegador" virar uma linha no launcher.log (para testar sem abrir o navegador
//  de quem esta na maquina).
//
//  Comentarios sem acento, como o resto do projeto. Textos de tela via T()/TS().
// =====================================================================================
#pragma once
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <string>
#include <atomic>
#include <functional>
#include "WebView2.h"          // SDK Microsoft.Web.WebView2 (portable-launcher\webview2-sdk\include)
#include "idioma.h"
#include "splash_ofertas.h"
#include "atualizacao.h"       // Anotar, CriarPastas, Existe

namespace vitrine {

// ----------------------------------------------------------------- IIDs (do WebView2.h)
inline IID IidDe(const wchar_t* s) { IID i{}; IIDFromString(s, &i); return i; }
inline const IID& IID_EnvCompleted()  { static const IID i = IidDe(L"{4E8A3389-C9D8-4BD2-B6B5-124FEE6CC14D}"); return i; }
inline const IID& IID_CtlCompleted()  { static const IID i = IidDe(L"{6C4819F3-C9B7-4260-8127-C9F5BDE7F68C}"); return i; }
inline const IID& IID_NavStarting()   { static const IID i = IidDe(L"{9ADBE429-F36D-432B-9DDC-F8881FBD76E3}"); return i; }
inline const IID& IID_NavCompleted()  { static const IID i = IidDe(L"{D33A35BF-1C49-4F98-93AB-006E0533FE1C}"); return i; }
inline const IID& IID_NewWindow()     { static const IID i = IidDe(L"{D4C185FE-C81C-4989-97AF-2D3FA7AB5651}"); return i; }
inline const IID& IID_AccelKey()      { static const IID i = IidDe(L"{B29C7E28-FA79-41A8-8E44-65811C76DCB2}"); return i; }
inline const IID& IID_Controller2()   { static const IID i = IidDe(L"{C979903E-D4CA-4228-92EB-47EE3FA96EAB}"); return i; }
inline const IID& IID_Settings3()     { static const IID i = IidDe(L"{FDB5AB74-AF33-4854-84F0-0A631DEB5EBA}"); return i; }
inline const IID& IID_NavCompletedArgs2() { static const IID i = IidDe(L"{FDF8B738-EE1E-4DB2-A329-8D7D7B74D792}"); return i; }
inline const IID& IID_ScriptNoDocumento() { static const IID i = IidDe(L"{B99369F3-9B11-47B5-BC6F-8E7895FCEA17}"); return i; }

// ----------------------------------------------------------------- um handler COM por lambda
template <class I, class A, class B>
struct Cb final : I {
    std::function<HRESULT(A, B)> f;
    const IID& iid;
    ULONG n = 1;
    Cb(const IID& i, std::function<HRESULT(A, B)> fn) : f(std::move(fn)), iid(i) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** o) override {
        if (!o) return E_POINTER;
        if (IsEqualIID(r, IID_IUnknown) || IsEqualIID(r, iid)) { *o = static_cast<I*>(this); AddRef(); return S_OK; }
        *o = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++n; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG k = --n; if (!k) delete this; return k; }
    HRESULT STDMETHODCALLTYPE Invoke(A a, B b) override { return f(a, b); }
};
template <class I, class A, class B>
inline I* Handler(const IID& iid, std::function<HRESULT(A, B)> f) { return new Cb<I, A, B>(iid, std::move(f)); }

// ----------------------------------------------------------------- utilidades
// "https://host:porta/caminho" (minusculo, sem query, sem # e sem barra final) de um
// endereco. E o que define "dentro" e "fora" da vitrine: SO a propria rota da vitrine navega
// na coluna (a pagina pode recarregar-se com outra query); qualquer outra navegacao de topo -
// inclusive /join?... no mesmo dominio, que e o redirecionamento para o Telegram, e os links
// das lojas - e cancelada e vai ao navegador padrao. Sub-recursos (imagens de cdn*.telesco.pe,
// fontes, o JSON) nao passam por NavigationStarting, entao nao sao afetados.
inline std::wstring Rota(const std::wstring& u) {
    const size_t p = u.find(L"://");
    if (p == std::wstring::npos) return L"";
    size_t fim = u.find_first_of(L"?#", p + 3);
    std::wstring o = u.substr(0, fim == std::wstring::npos ? u.size() : fim);
    while (o.size() > p + 3 && o.back() == L'/') o.pop_back();
    for (auto& c : o) c = (wchar_t)towlower(c);
    return o;
}
inline std::wstring DeCoTaskMem(LPWSTR s) { std::wstring r = s ? s : L""; if (s) CoTaskMemFree(s); return r; }

// ----------------------------------------------------------------- a coluna e SO o feed
// O endereco da vitrine (kUrlVitrinePadrao) e uma rota da landing do grupo, e HOJE o
// servidor devolve a landing INTEIRA para qualquer caminho: /app-ofertas, /app-ofertas.json
// e /qualquer-coisa responderam 200 com o mesmo corpo, byte a byte. Ou seja, a coluna
// recebia a pagina de vendas completa - barra rolante, chamada de topo, numeros, parceiros,
// cartoes de "por que confiar", perguntas frequentes, rodape - com o feed no meio de tudo.
//
// Este script roda ANTES dos scripts da pagina (AddScriptToExecuteOnDocumentCreated) e a
// reduz ao que a coluna existe para mostrar: o cabecalho do grupo (logotipo, nome, selo e o
// botao de entrar no Telegram) e os cartoes de oferta do feed. Como roda no inicio, a
// landing nao chega a pintar: nao ha o piscar da pagina cheia antes do corte.
//
// Ele tambem encolhe o cartao para a coluna de 420 px SEM cortar conteudo: fonte e respiro
// menores, imagem com altura maxima e object-fit:contain (encolhe inteira, nao recorta), e
// nenhum line-clamp ou reticencia - o texto da oferta continua inteiro na tela. E tira o
// teaser .feed-wall/.feed-fade da landing, um gradiente de 200 px que apaga de proposito os
// ultimos cartoes; numa coluna estreita ele comia justamente o que a pessoa quer ler.
//
// O acento: a pagina servida esta em UTF-8 e chega correta (nao ha mojibake nela), mas o
// nome esta escrito "PC do Fafa", sem acento, no texto-fonte do site - 14 vezes, no titulo,
// no logotipo, no alt da imagem e no rodape. O conserto de raiz e no site, que nao mora
// neste repositorio; aqui a gente corrige o que a coluna MOSTRA. O "@FafaPromobot" que
// aparece no texto das ofertas e o nome real do bot e NAO pode ser trocado - dai o
// (?![0-9A-Za-z_]) na expressao.
//
// Se a pagina mudar de forma e o feed nao for encontrado, o script desiste e deixa a pagina
// como esta: coluna cheia demais e ruim, coluna em branco e pior.
//
// O unico caractere fora do ASCII neste arquivo e o "a" com acento dessa troca, dentro do
// literal cru LR"JS(...)JS". O arquivo e UTF-8 sem BOM e so com LF, como splash_ofertas.h,
// que ja tem "PC DO FAFA" acentuado desde sempre; o .gitattributes tem "* -text", entao o
// git guarda e devolve byte a byte. Se um dia este arquivo aparecer salvo em outra
// codificacao, e ESTE caractere que denuncia - confira antes de compilar.
inline const wchar_t* ScriptSoOfertas() {
    return LR"JS(
(function () {
  'use strict';

  // O WebView2 roda este script em TODO documento criado, e a landing traz quadros de fora
  // (o do video e o do pixel do Facebook). Dentro deles nao ha feed nenhum para reduzir, e
  // esconder o corpo de um quadro alheio so pode dar errado: aqui e a pagina de cima ou nada.
  try { if (window.top !== window) return; } catch (e) { return; }

  // TODA regra de aparencia vive sob "html.v-reduz", classe que so entra depois de a pagina
  // ser reconhecida como a landing. Fora isso o script nao pinta nada: se um dia a rota
  // /app-ofertas de verdade subir, ela vem com layout proprio ja feito para a coluna, e este
  // arquivo nao pode discutir com ela. A unica regra solta e a que esconde o corpo enquanto
  // a decisao nao saiu - e ela e desfeita pela .v-pronta nos DOIS caminhos.
  var R = 'html.v-reduz ';
  var CSS =
    'body{visibility:hidden}' +
    'html.v-pronta body{visibility:visible}' +
    R + '{background:#0b0f16!important}' +
    R + 'body{background:#0b0f16!important;margin:0!important;padding:0!important;' +
      'overflow-x:hidden!important}' +
    R + '#vitrine-raiz{padding:0 0 14px 0}' +
    R + 'nav#nav{position:static!important;background:transparent!important;border:0!important;' +
      'box-shadow:none!important;backdrop-filter:none!important}' +
    R + '#nav .nav-container{max-width:none!important;padding:12px 14px!important;gap:10px}' +
    R + '#nav .logo{gap:10px!important}' +
    R + '#nav .logo-img{width:38px!important;height:38px!important;border-radius:8px!important}' +
    R + '#nav .logo-text{font-size:17px!important;margin-bottom:2px!important}' +
    R + '#nav .logo-badge{font-size:10px!important}' +
    R + '#nav .btn-primary{padding:8px 12px!important;font-size:12px!important;' +
      'border-radius:7px!important;white-space:nowrap}' +
    R + '.telegram-feed{max-width:none!important;margin:0!important;padding:0 12px!important;' +
      'gap:8px!important}' +
    R + '.offer-card{border-radius:10px!important}' +
    R + '.offer-card-image{max-height:118px!important;object-fit:contain!important;' +
      'background:#0e1621!important}' +
    R + '.offer-card-body{padding:8px 10px 10px!important}' +
    R + '.offer-card-meta{margin-bottom:5px!important}' +
    R + '.offer-card-tag{font-size:10px!important;padding:2px 6px!important}' +
    R + '.offer-card-date{font-size:10px!important}' +
    R + '.offer-card-text{font-size:12px!important;line-height:1.38!important;' +
      'margin-bottom:8px!important;overflow-wrap:anywhere}' +
    R + '.offer-card-btn{font-size:12px!important;padding:7px!important;border-radius:8px!important}' +
    R + '::-webkit-scrollbar{width:10px}' +
    R + '::-webkit-scrollbar-track{background:#0b0f16}' +
    R + '::-webkit-scrollbar-thumb{background:#243244;border-radius:5px}' +
    R + '::-webkit-scrollbar-thumb:hover{background:#31445c}';

  // No comeco do documento o <html> ainda nem existe (o script roda antes de a pagina ser
  // lida), entao document.head E document.documentElement podem ser nulos: sem esta guarda
  // o script morre aqui e a landing inteira vai para a coluna. Devolve false enquanto nao
  // houver onde pendurar o estilo.
  function estilo() {
    if (document.getElementById('vitrine-estilo')) return true;
    var alvo = document.head || document.documentElement;
    if (!alvo) return false;
    var s = document.createElement('style');
    s.id = 'vitrine-estilo';
    s.textContent = CSS;
    alvo.appendChild(s);
    return true;
  }

  function marcar(c) {
    var e = document.documentElement;
    if (e && (' ' + e.className + ' ').indexOf(' ' + c + ' ') < 0) e.className += ' ' + c;
  }
  function mostrar() { marcar('v-pronta'); }

  // A ASSINATURA DA LANDING, e o motivo dela existir.
  // A rota /app-ofertas ja esta escrita no worker do grupo (handleAppOfertas / buildAppOfertas,
  // no bridge-page) e um dia sobe. Essa pagina TAMBEM tem .telegram-feed e .offer-card, mas ela
  // ja e a coluna: cabecalho proprio, layout proprio para 420 px, sem pixel de terceiro. Se este
  // script agisse la, jogaria fora o cabecalho e a chamada do fim - eu quebraria a pagina nova
  // consertando a antiga.
  // A landing se distingue por duas coisas que a pagina da vitrine nao tem: o <nav id="nav"> e
  // os <section>. Exigir as DUAS e melhor do que combinar uma marca com o outro repositorio,
  // porque funciona sem ninguem coordenar nada e vale tambem para um launcher antigo que caia
  // na pagina nova. Sem a assinatura, este script nao mexe e nao pinta: a pagina vai inteira.
  function ehLanding() {
    try {
      return !!document.getElementById('nav') && document.querySelectorAll('section').length > 0;
    } catch (e) { return false; }
  }

  // AQUI HAVIA UMA FUNCAO QUE ACENTUAVA "Fafa". Ela saiu em 11/09/2026: o nome da marca e
  // "PC do Fafa", SEM acento, e era esta funcao que reescrevia a pagina do grupo com a grafia
  // acentuada toda vez que a vitrine carregava - a causa de a forma errada voltar sozinha
  // mesmo depois de corrigida nos arquivos. Nao e preciso substituir por nada: o texto que vem
  // do servidor ja esta certo, e uma tela nao deve corrigir a ortografia de uma marca por
  // conta propria.

  function reduzir() {
    if (!document.body) return false;
    if (!ehLanding()) return 'nao-e-landing';   // pagina propria da vitrine: sai sem tocar
    var feed = document.querySelector('.telegram-feed');
    if (!feed || !feed.querySelector('.offer-card')) return false;
    var nav = document.getElementById('nav');
    var raiz = document.createElement('div');
    raiz.id = 'vitrine-raiz';
    if (nav) raiz.appendChild(nav);
    raiz.appendChild(feed);
    while (document.body.firstChild) document.body.removeChild(document.body.firstChild);
    document.body.appendChild(raiz);
    marcar('v-reduz');            // so agora o estilo compacto passa a valer
    // a mais recente e a de cima: a coluna comeca no topo do feed, nao onde a pagina parou.
    // Cada um no seu try: uma falha aqui nao pode apagar uma reducao que ja deu certo.
    try { window.scrollTo(0, 0); } catch (e) {}
    return true;
  }

  // Tres desfechos, e os tres terminam com a coluna visivel:
  //   true             reduziu; acabou
  //   'nao-e-landing'  e a pagina propria da vitrine; acabou sem tocar em nada
  //   false            ainda nao deu (feed nao chegou); tenta de novo, ate 12 vezes
  var tentativas = 0;
  function tentar() {
    estilo();
    var r = false;
    try { r = reduzir(); } catch (e) { r = false; }
    if (r || ++tentativas >= 12) { mostrar(); return; }
    setTimeout(tentar, 250);
  }

  // O estilo tem de entrar o quanto antes: e ele que segura o corpo escondido ate a pagina
  // estar reduzida, e e por isso que a landing nao pisca na coluna. Enquanto o <html> nao
  // existir, tenta de novo no proximo ciclo; com teto, para nunca virar laco eterno.
  var cedo = 0;
  function primeiroEstilo() {
    var feito = false;
    try { feito = estilo(); } catch (e) { feito = false; }
    if (!feito && ++cedo < 400) setTimeout(primeiroEstilo, 0);
  }
  primeiroEstilo();

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', tentar);
  else tentar();
  setTimeout(mostrar, 6000);
})();
)JS";
}

typedef HRESULT (STDAPICALLTYPE* PFN_VersaoRuntime)(PCWSTR, LPWSTR*);
typedef HRESULT (STDAPICALLTYPE* PFN_CriarAmbiente)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                                    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

// ----------------------------------------------------------------- o motor
struct Motor final : ofertas::detail::VitrineHost {
    // preenchidos por quem decide (launcher.cpp) antes de a tela abrir
    std::wstring url, pastaDados, log;
    bool soLogarLinks = false;          // DLSS5_VITRINE_LOG_LINKS=1
    std::atomic<int> linksFora{ 0 };    // quantos links foram para o navegador (log)
    std::wstring versaoRuntime;         // o que o loader disse (log)
    // estado interno
    HMODULE loader = nullptr;
    PFN_CriarAmbiente criarAmbiente = nullptr;
    ICoreWebView2Environment* env = nullptr;
    ICoreWebView2Controller* ctl = nullptr;
    ICoreWebView2* wv = nullptr;
    HWND pai = nullptr;
    RECT area{};
    double zoom = 1.0;
    std::wstring rota;                  // Rota(url): o unico destino de topo que fica na coluna
    bool fechado = false;

    ~Motor() override { Fechar(); }

    // O runtime esta na maquina? Carrega o loader (ao lado do launcher) e pergunta a versao.
    // Nao lanca nada: sem loader, sem runtime ou com DLSS5_SEM_WEBVIEW=1 devolve false e o motivo.
    bool RuntimePresente(const std::wstring& pastaExe, std::wstring& motivo) {
        wchar_t e[4]{};
        if (GetEnvironmentVariableW(L"DLSS5_SEM_WEBVIEW", e, 4) && e[0] == L'1') { motivo = L"teste_sem_webview"; return false; }
        const std::wstring dll = pastaExe + L"WebView2Loader.dll";
        if (!atualizacao::Existe(dll)) { motivo = L"sem_loader"; return false; }
        loader = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!loader) { motivo = L"loader_nao_carrega(" + std::to_wstring(GetLastError()) + L")"; return false; }
        auto versao = (PFN_VersaoRuntime)GetProcAddress(loader, "GetAvailableCoreWebView2BrowserVersionString");
        criarAmbiente = (PFN_CriarAmbiente)GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions");
        if (!versao || !criarAmbiente) { motivo = L"loader_sem_funcoes"; return false; }
        LPWSTR v = nullptr;
        const HRESULT hr = versao(nullptr, &v);
        versaoRuntime = DeCoTaskMem(v);
        if (FAILED(hr) || versaoRuntime.empty()) { motivo = L"sem_runtime"; return false; }
        return true;
    }

    void Acordar() { if (pai) PostMessageW(pai, ofertas::detail::WM_OFERTAS_ATUALIZA, 0, 0); }
    void Falhou(const wchar_t* onde, HRESULT hr) {
        atualizacao::Anotar(log, L"vitrine: %ls (hr=0x%08lX) - fica o aviso 'sem conexao'", onde, (unsigned long)hr);
        motivoOff = onde;
        estado.store(2);
        Acordar();
    }
    // Um endereco que a pagina quis abrir fora: so http(s) passa (a pagina e conteudo remoto;
    // um javascript: ou file: nao pode chegar ao ShellExecute), e em teste vira so log.
    void AbrirFora(const std::wstring& u, const wchar_t* de) {
        if (!ofertas::UrlEhWeb(u)) { atualizacao::Anotar(log, L"vitrine: %ls recusado (nao e http/https): %ls", de, u.substr(0, 200).c_str()); return; }
        linksFora.fetch_add(1);
        if (soLogarLinks) { atualizacao::Anotar(log, L"vitrine: %ls -> navegador padrao (TESTE, nao aberto): %ls", de, u.c_str()); return; }
        atualizacao::Anotar(log, L"vitrine: %ls -> navegador padrao: %ls", de, u.substr(0, 200).c_str());
        ShellExecuteW(nullptr, L"open", u.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    bool DentroDaVitrine(const std::wstring& u) const { return Rota(u) == rota; }

    // ---- VitrineHost
    void Criar(HWND janela, RECT r, double z) override {
        pai = janela; area = r; zoom = z;
        rota = Rota(url);
        atualizacao::CriarPastas(pastaDados);
        if (!criarAmbiente) { Falhou(L"falha_ambiente(loader)", E_FAIL); return; }
        auto h = Handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
            IID_EnvCompleted(), [this](HRESULT hr, ICoreWebView2Environment* e) -> HRESULT {
                if (fechado) return S_OK;
                if (FAILED(hr) || !e) { Falhou(L"falha_ambiente", hr); return S_OK; }
                env = e; env->AddRef();
                auto hc = Handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
                    IID_CtlCompleted(), [this](HRESULT hr2, ICoreWebView2Controller* c) -> HRESULT { ControlePronto(hr2, c); return S_OK; });
                const HRESULT h2 = env->CreateCoreWebView2Controller(pai, hc);
                hc->Release();
                if (FAILED(h2)) Falhou(L"falha_controle(pedido)", h2);
                return S_OK;
            });
        const HRESULT hr = criarAmbiente(nullptr, pastaDados.c_str(), nullptr, h);
        h->Release();
        if (FAILED(hr)) Falhou(L"falha_ambiente(pedido)", hr);
    }
    void ControlePronto(HRESULT hr, ICoreWebView2Controller* c) {
        if (fechado) { if (c) c->Close(); return; }
        if (FAILED(hr) || !c) { Falhou(L"falha_controle", hr); return; }
        ctl = c; ctl->AddRef();
        if (FAILED(ctl->get_CoreWebView2(&wv)) || !wv) { Falhou(L"falha_controle(webview)", E_FAIL); return; }
        // a pagina e SO pagina: nada do que faz um navegador parecer navegador
        ICoreWebView2Settings* s = nullptr;
        if (SUCCEEDED(wv->get_Settings(&s)) && s) {
            s->put_AreDefaultContextMenusEnabled(FALSE);
            s->put_AreDevToolsEnabled(FALSE);
            s->put_IsZoomControlEnabled(FALSE);          // Ctrl+roda e Ctrl+/-
            s->put_IsStatusBarEnabled(FALSE);
            s->put_IsBuiltInErrorPageEnabled(FALSE);     // a falha e nossa: "sem conexao com as ofertas"
            s->put_AreHostObjectsAllowed(FALSE);
            s->put_IsWebMessageEnabled(FALSE);
            ICoreWebView2Settings3* s3 = nullptr;
            if (SUCCEEDED(s->QueryInterface(IID_Settings3(), (void**)&s3)) && s3) { s3->put_AreBrowserAcceleratorKeysEnabled(FALSE); s3->Release(); }
            s->Release();
        }
        // fundo da cor da tela enquanto a pagina nao pintou (sem o clarao branco)
        ICoreWebView2Controller2* c2 = nullptr;
        if (SUCCEEDED(ctl->QueryInterface(IID_Controller2(), (void**)&c2)) && c2) {
            COREWEBVIEW2_COLOR cor{ 255, 11, 15, 22 };   // C_BG #0b0f16
            c2->put_DefaultBackgroundColor(cor);
            c2->Release();
        }
        ctl->put_Bounds(area);
        ctl->put_ZoomFactor(zoom);
        EventRegistrationToken t{};
        // qualquer navegacao de topo que nao seja a propria rota da vitrine: cancela e manda ao navegador
        wv->add_NavigationStarting(Handler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
            IID_NavStarting(), [this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* a) -> HRESULT {
                LPWSTR u = nullptr; a->get_Uri(&u);
                const std::wstring uri = DeCoTaskMem(u);
                if (!DentroDaVitrine(uri)) { a->put_Cancel(TRUE); AbrirFora(uri, L"navegacao para fora"); }
                return S_OK;
            }), &t);
        // carregou ou nao: e o que a coluna mostra (a pagina, ou o aviso no lugar dela)
        wv->add_NavigationCompleted(Handler<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
            IID_NavCompleted(), [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* a) -> HRESULT {
                BOOL ok = FALSE; a->get_IsSuccess(&ok);
                COREWEBVIEW2_WEB_ERROR_STATUS st = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN; a->get_WebErrorStatus(&st);
                int http = 0;
                ICoreWebView2NavigationCompletedEventArgs2* a2 = nullptr;
                if (SUCCEEDED(a->QueryInterface(IID_NavCompletedArgs2(), (void**)&a2)) && a2) { a2->get_HttpStatusCode(&http); a2->Release(); }
                const bool deu = ok && (http == 0 || http < 400);
                atualizacao::Anotar(log, L"vitrine: pagina %ls (erro=%d http=%d)", deu ? L"carregada" : L"falhou", (int)st, http);
                if (ctl) ctl->put_IsVisible(deu ? TRUE : FALSE);
                estado.store(deu ? 1 : 2);
                Acordar();
                return S_OK;
            }), &t);
        // target=_blank / window.open: nunca uma janela nova, sempre o navegador da pessoa
        wv->add_NewWindowRequested(Handler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
            IID_NewWindow(), [this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* a) -> HRESULT {
                LPWSTR u = nullptr; a->get_Uri(&u);
                a->put_Handled(TRUE);
                AbrirFora(DeCoTaskMem(u), L"nova janela");
                return S_OK;
            }), &t);
        // Esc com o foco dentro da pagina fecha a tela, igual a Esc fora dela
        ctl->add_AcceleratorKeyPressed(Handler<ICoreWebView2AcceleratorKeyPressedEventHandler, ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs*>(
            IID_AccelKey(), [this](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* a) -> HRESULT {
                COREWEBVIEW2_KEY_EVENT_KIND k = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN; a->get_KeyEventKind(&k);
                UINT vk = 0; a->get_VirtualKey(&vk);
                if ((k == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || k == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) && vk == VK_ESCAPE) {
                    a->put_Handled(TRUE);
                    PostMessageW(pai, WM_KEYDOWN, VK_ESCAPE, 0);
                }
                return S_OK;
            }), &t);
        // a coluna e SO o feed de ofertas: ScriptSoOfertas roda ANTES dos scripts da pagina, em
        // toda navegacao, entao a landing inteira nunca chega a pintar. Se o script nao entrar,
        // a pagina vem como o servidor mandou - fica feio, nao fica quebrado; a linha no log e
        // o que diz qual dos dois aconteceu.
        auto hs = Handler<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler, HRESULT, LPCWSTR>(
            IID_ScriptNoDocumento(), [this](HRESULT hr2, LPCWSTR) -> HRESULT {
                if (FAILED(hr2))
                    atualizacao::Anotar(log, L"vitrine: script do feed recusado (hr=0x%08lX) - a pagina vai inteira", (unsigned long)hr2);
                return S_OK;
            });
        const HRESULT hs1 = wv->AddScriptToExecuteOnDocumentCreated(ScriptSoOfertas(), hs);
        hs->Release();
        if (FAILED(hs1))
            atualizacao::Anotar(log, L"vitrine: script do feed nao pode ser pedido (hr=0x%08lX) - a pagina vai inteira", (unsigned long)hs1);
        const HRESULT hn = wv->Navigate(url.c_str());
        if (FAILED(hn)) Falhou(L"falha_navegar", hn);
        else atualizacao::Anotar(log, L"vitrine: runtime %ls, pagina pedida: %ls", versaoRuntime.c_str(), url.c_str());
    }
    // A pessoa trocou o idioma nos chips da primeira tela. A pagina ja esta carregada com a
    // lingua antiga na query, e nao ha como pedir a uma pagina de terceiro que troque: o
    // caminho e renavegar.
    //
    // TROCA SO O VALOR DE l=, em vez de remontar a URL. Remontar exigiria guardar aqui a
    // Verificacao que o Preparar recebeu - uma referencia a um objeto que vive no escopo do
    // launcher, que e o tipo de coisa que vira ponteiro solto quando alguem mexe na ordem
    // das declaracoes. Trocando o valor no lugar, o id da instalacao e o resto da query
    // seguem exatamente como estavam.
    void TrocarIdioma(const wchar_t* codigo) override {
        if (!wv || fechado || url.empty() || !codigo || !*codigo) return;
        const size_t q = url.find(L'?');
        if (q == std::wstring::npos) return;
        // "l=" tem de ser um parametro inteiro, e nao o fim do nome de outro ("&url=")
        size_t p = url.find(L"l=", q + 1);
        while (p != std::wstring::npos && url[p - 1] != L'?' && url[p - 1] != L'&')
            p = url.find(L"l=", p + 2);
        if (p == std::wstring::npos) return;
        const size_t ini = p + 2;
        size_t fim = url.find(L'&', ini);
        if (fim == std::wstring::npos) fim = url.size();
        if (url.compare(ini, fim - ini, codigo) == 0) return;   // ja esta nessa lingua
        url = url.substr(0, ini) + codigo + url.substr(fim);
        rota = Rota(url);
        const HRESULT hn = wv->Navigate(url.c_str());
        atualizacao::Anotar(log, L"vitrine: idioma -> %ls, renavegando (hr=0x%08lX)", codigo,
                            (unsigned long)hn);
    }

    void Mover(RECT r, double z) override {
        area = r; zoom = z;
        if (ctl) { ctl->put_Bounds(area); ctl->put_ZoomFactor(zoom); }
    }
    // A GAVETA FECHOU/ABRIU: so a visibilidade da janela filha muda - a pagina continua
    // carregada. Fechada, ela nao pode ficar visivel por cima da coluna do produto; aberta,
    // so aparece se a pagina realmente carregou (estado 1), senao quem manda e o aviso
    // "sem conexao com as ofertas" que a tela desenha no lugar dela.
    void Mostrar(bool visivel) override {
        if (ctl) ctl->put_IsVisible(visivel && estado.load() == 1 ? TRUE : FALSE);
    }
    void Fechar() override {
        if (fechado) return;
        fechado = true;
        if (ctl) { ctl->Close(); }
        if (wv)  { wv->Release();  wv = nullptr; }
        if (ctl) { ctl->Release(); ctl = nullptr; }
        if (env) { env->Release(); env = nullptr; }
        // o loader fica carregado ate o processo sair: descarrega-lo com o runtime ainda
        // desmontando objetos e pedir problema, e o launcher sai logo depois de qualquer jeito
    }
    const wchar_t* Carga() const { const int e = estado.load(); return e == 1 ? L"ok" : e == 2 ? L"falhou" : L"carregando"; }
};

// A decisao que nao depende da tela: chave vitrine=, endereco, runtime. Devolve true com o
// motor preenchido (url, pasta de dados, log); false com o motivo para o log. A espera pela
// rede (1,5 s) e do launcher.cpp, porque e ele que sabe quando a busca comecou. (A vitrine
// nao depende de url_base: o endereco dela e fixo, kUrlVitrinePadrao, ou vitrine_url=.)
inline bool Preparar(Motor& m, const atualizacao::Verificacao& verif, const std::wstring& raiz, std::wstring& motivo) {
    m.log = atualizacao::ArquivoLogLauncher(raiz);
    if (!verif.cfg.vitrine)        { motivo = L"ini_vitrine=0"; return false; }
    m.url = atualizacao::UrlVitrine(verif);
    if (m.url.empty())             { motivo = L"sem_url"; return false; }
    if (!m.RuntimePresente(raiz, motivo)) return false;
    m.pastaDados = raiz + L"cache\\webview2";
    wchar_t e[4]{};
    m.soLogarLinks = GetEnvironmentVariableW(L"DLSS5_VITRINE_LOG_LINKS", e, 4) && e[0] == L'1';
    return true;
}

} // namespace vitrine
