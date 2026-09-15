// =====================================================================================
//  meiorefresh.h  -  "a imagem demora duas atualizacoes da tela para aparecer"
//
//  MODULO ISOLADO. Nao inclui nada do app e nao e incluido por ninguem ainda.
//  Dono: agente "um quadro a cada dois". Integracao: ver ESPEC-LIGACAO.md.
//
//  -------------------------------------------------------------------------------
//  O QUE ESTE ARQUIVO RESOLVE, E O QUE ELE **NAO** RESOLVE
//  -------------------------------------------------------------------------------
//  Existem DOIS defeitos diferentes que produzem o mesmo numero (~30 ms num monitor
//  de 60 Hz) e que a sessao de 14/09 confundiu num so. Eles NAO sao a mesma coisa:
//
//   (A) "MEIO REFRESH" DE VERDADE - a pelicula recebe UM flip a cada DOIS refreshes.
//       Assinatura: `slot` ~= 1 refresh (30-31 ms) E `chegada`/`pelicula` caem para
//       METADE da taxa da tela (30,0 fps num monitor de 60 Hz).
//       Causa ja provada e JA CORRIGIDA (main.cpp, bloco da pelicula em tela cheia):
//       janela WS_EX_LAYERED e SEMPRE redirecionada, o DWM compoe cada quadro dela e
//       ela nunca entra em Independent Flip. Hoje a janela nasce com
//       WS_EX_NOREDIRECTIONBITMAP e sem LAYERED. **Metade dos quadros voltou.**
//       Este modulo NAO conserta (A): ele VIGIA para (A) nao voltar calado.
//
//   (B) ATRASO DE DUAS ATUALIZACOES - todos os quadros aparecem (60 de 60), mas cada
//       um demora ~1,8 refreshes entre o nosso Present e o vblank em que ele e
//       varrido. Assinatura: `PRESENT->TELA` 30,2-30,7 ms COM `present=60.0` e
//       `slot` pequeno. Isto e LATENCIA, nao vazao - quem somar os 30 ms num
//       orcamento de quadro conta duas vezes o que a fila ja esta escondendo.
//       **E (B) que esta vivo no log de 14/09 06:30.**
//
//  Este modulo faz tres coisas, nesta ordem de valor:
//    1. MEDE qual dos dois esta acontecendo, de dentro do proprio app, sem registro,
//       sem administrador e sem ferramenta externa (Detector).
//    2. Oferece as alavancas que NAO precisam de privilegio nenhum (Planejar /
//       SyncInterval / FlagsDePresent).
//    3. So no fim, e so se a medida pedir, encosta na chave de MPO do Windows - com
//       backup, com desfazer e dizendo que precisa reiniciar (namespace Mpo).
//
//  -------------------------------------------------------------------------------
//  COMO O DIAGNOSTICO E FEITO (e por que ele e confiavel)
//  -------------------------------------------------------------------------------
//  Dois instrumentos independentes, que precisam CONCORDAR para virar veredito:
//
//   (i)  IDXGISwapChainMedia::GetFrameStatisticsMedia() -> CompositionMode.
//        E a resposta DIRETA do proprio DXGI: OVERLAY = o quadro foi para um plano de
//        hardware (caminho direto, sem composicao por quadro); COMPOSED = o DWM
//        compos. Nao precisa de privilegio, nao precisa do registro, nao precisa de
//        PresentMon. Disponivel em qualquer swapchain de flip model (Windows 8.1+).
//
//   (ii) O atraso medido em UNIDADES DE REFRESH, nao em ms.
//        `PRESENT->TELA / periodo do refresh`. O periodo vem do
//        DwmGetCompositionTimingInfo (qpcRefreshPeriod), nunca chutado em 60 Hz -
//        senao o diagnostico mente em 144 Hz e em monitor variavel.
//        <= 1,35 refreshes = caminho direto. >= 1,60 = uma atualizacao a mais no meio.
//
//  ATENCAO A UMA REFUTACAO QUE **NAO VALE AQUI**: docs\latencia-nr.md §4.4 declarou a
//  hipotese de Independent Flip "descartada por medicao" porque `dRefresh =
//  SyncRefreshCount - PresentRefreshCount` deu 0,00 em todas as condicoes. Esse teste
//  NAO separa composicao de caminho direto - ele so detecta quadro PERDIDO/atrasado, e
//  a composicao do DWM nao perde quadro nenhum, so atrasa todos igualmente. Alem
//  disso a bancada de la era uma janela de 1280x720 num monitor de 3440x1440, que o
//  proprio autor registra como "nunca foi elegivel a iFlip". A pelicula em tela cheia
//  de hoje cobre o monitor inteiro e E elegivel. => hipotese REABERTA, nao refutada.
//
//  -------------------------------------------------------------------------------
//  A ALAVANCA PRINCIPAL, E POR QUE ELA E TAMBEM O TESTE QUE DECIDE
//  -------------------------------------------------------------------------------
//  Hoje o app chama `Present(0, 0)`. Em FLIP model isso **NAO rasga e NAO pula o
//  vblank** - ele so deixa de estrangular o laco. O quadro continua esperando uma
//  varredura. Para pular a fila e preciso o par COMPLETO:
//      criacao:  DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
//      Present:  Present(0, DXGI_PRESENT_ALLOW_TEARING)
//  docs\latencia-nr.md §4.4 (R7) testou "ALLOW_TEARING so na criacao" e concluiu "sem
//  efeito". **Esse teste nao vale como refutacao**: a flag de criacao sozinha nao faz
//  nada, e o proprio autor anota "o Present continua sem a flag". Fica REABERTO.
//
//  E aqui esta a parte bonita: ALLOW_TEARING **so age quando a janela ja esta no
//  caminho direto**. Se o DWM estiver compondo, a flag e inerte por construcao. Entao
//  o mesmo botao e, ao mesmo tempo, o conserto candidato E o experimento que separa as
//  duas hipoteses:
//      ligou e PRESENT->TELA caiu de ~30 para ~2-8 ms  => estavamos no caminho direto,
//                                                         o refresh extra era FILA. Fim.
//                                                         Nenhum registro necessario.
//      ligou e PRESENT->TELA nao mexeu                 => o DWM esta compondo. So
//                                                         **ai** a conversa de MPO
//                                                         comeca a fazer sentido.
//  O Detector confirma pelo outro lado (CompositionMode). Dois instrumentos, um
//  veredito.
//
//  -------------------------------------------------------------------------------
//  NUMEROS QUE ESTE ARQUIVO NAO PODE CONTRARIAR (medidos, com fonte)
//  -------------------------------------------------------------------------------
//   - SetMaximumFrameLatency(2) foi medido e e MUITO PIOR: 57-59 ms contra ~19, e o
//     fps cai para 50 (docs\latencia-nr.md §4.4 (f)). => a latencia maxima fica em 1,
//     SEMPRE. Este modulo nunca a levanta. Mexer em BufferCount e outra coisa e nunca
//     foi testado nesta arvore.
//   - Com a fonte abaixo da taxa da tela sobra folga e o PRESENT->TELA desabou para
//     6,53-6,93 ms; com a fonte na taxa da tela a fila enche e ele sobe (§4.2/§4.3).
//     Hoje chegada=60,0 num monitor de 60 Hz: a fila esta SATURADA por construcao.
// =====================================================================================
#pragma once

#include <windows.h>
#include <dxgi.h>
#include <dxgi1_3.h>   // IDXGISwapChainMedia, DXGI_FRAME_STATISTICS_MEDIA, CompositionMode
#include <dxgi1_5.h>   // IDXGIFactory5, DXGI_FEATURE_PRESENT_ALLOW_TEARING
#include <dwmapi.h>
#include <string>
#include <cstdio>

// O app ja tem um Log(const char*, ...) estatico (screen_capture.h faz a mesma
// declaracao adiantada). Mantido igual para o modulo entrar sem cerimonia.
static void Log(const char* fmt, ...);

namespace MeioRefresh {

// =====================================================================================
//  1. A ESCOLHA DA PESSOA
// =====================================================================================
//
// UM controle so, tres posicoes, sem sigla no rotulo. O texto vive no fim do arquivo.
enum class Entrega : int {
    Esperando  = 0,   // padrao de hoje: o quadro espera a tela virar. Nunca rasga.
    NaHora     = 1,   // ALLOW_TEARING: manda assim que fica pronto. Pode rasgar.
    Automatico = 2,   // o app mede e escolhe: so vai para NaHora se o atraso aparecer.
};

inline const char* NomeInterno(Entrega e) {
    switch (e) {
        case Entrega::Esperando:  return "esperando";
        case Entrega::NaHora:     return "nahora";
        case Entrega::Automatico: return "auto";
    }
    return "esperando";
}

inline Entrega DoTexto(const wchar_t* s, Entrega padrao = Entrega::Esperando) {
    if (!s || !s[0]) return padrao;
    if (!lstrcmpiW(s, L"nahora") || !lstrcmpiW(s, L"na_hora") || !lstrcmpiW(s, L"1")) return Entrega::NaHora;
    if (!lstrcmpiW(s, L"auto")   || !lstrcmpiW(s, L"automatico") || !lstrcmpiW(s, L"2")) return Entrega::Automatico;
    if (!lstrcmpiW(s, L"esperando") || !lstrcmpiW(s, L"0")) return Entrega::Esperando;
    return padrao;
}

// =====================================================================================
//  2. O QUE PERGUNTAR ANTES DE CRIAR O SWAPCHAIN
// =====================================================================================
//
// Chamado UMA vez, antes do CreateSwapChainForHwnd. Nao cria nada, nao guarda estado.
struct PlanoDeCriacao {
    UINT bufferCount      = 2;
    UINT flags            = 0;      // os que o chamador ja tinha, mais os nossos
    UINT maxFrameLatency  = 1;      // NUNCA sobe. Ver o cabecalho (§4.4 (f)).
    bool tearingDisponivel = false; // o Windows/driver aceitam ALLOW_TEARING?
    bool tearingLigado     = false; // ...e nos vamos usar?
};

// O `tearing` do sistema tem de ser perguntado ao IDXGIFactory5. Se a fabrica nao for
// dessa geracao (ou o driver disser que nao), a flag NAO pode entrar na criacao: o
// CreateSwapChain devolve E_INVALIDARG e a pelicula nao abre. E por isso que isto e uma
// consulta, e nao um `#define`.
inline bool SuportaTearing(IDXGIFactory* fabrica) {
    if (!fabrica) return false;
    IDXGIFactory5* f5 = nullptr;
    if (FAILED(fabrica->QueryInterface(__uuidof(IDXGIFactory5), (void**)&f5)) || !f5) return false;
    BOOL ok = FALSE;
    const HRESULT hr = f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &ok, sizeof(ok));
    f5->Release();
    return SUCCEEDED(hr) && ok;
}

// `ritmoSolto` = o modo de bancada que o app ja tem (buffers 4 + latencia 3). Quando
// ele estiver ligado NAO mexemos em nada: e uma configuracao com dono e com medida.
//
// `buffersExtra`: 2 -> 3. Por que 3 e nao 4, e por que isto NAO e "latencia 2":
//   com 2 buffers um esta na tela e o outro e o nosso alvo de desenho; assim que o
//   Present enfileira o nosso, o laco fica sem para onde desenhar e para no objeto de
//   espera. E exatamente o que o log mostra: `slot` medio 0,88 ms mas **p95 10,2 ms**,
//   e crescendo ao longo da sessao (0,03 -> 0,93). Com 3 buffers sobra um livre e essa
//   parada some. A LATENCIA MAXIMA CONTINUA 1 - e ela, nao o numero de buffers, que
//   governa a profundidade da fila de apresentacao. Sao coisas diferentes e a §4.4 (f)
//   so mediu a segunda.
inline PlanoDeCriacao Planejar(IDXGIFactory* fabrica, Entrega modo, bool ritmoSolto, bool buffersExtra) {
    PlanoDeCriacao p;
    if (ritmoSolto) { p.bufferCount = 4; p.maxFrameLatency = 3; return p; }
    p.bufferCount     = buffersExtra ? 3u : 2u;
    p.maxFrameLatency = 1;
    p.tearingDisponivel = SuportaTearing(fabrica);

    // A FLAG DE CRIACAO ENTRA SEMPRE QUE O SISTEMA ACEITA - INCLUSIVE NO MODO "Certinha".
    //
    // Parece errado e nao e, e a prova esta nesta propria arvore. Quem rasga a imagem e a
    // flag do **Present**, nao a da criacao; sozinha, a de criacao nao muda comportamento
    // nenhum. docs\latencia-nr.md §4.4 (R7) mediu exatamente esta condicao - "ALLOW_TEARING
    // so na criacao" - e registrou "sem efeito (21,1 sob 63% de carga). E sem tearing: o
    // Present continua sem a flag".
    //
    // E por que isso importa tanto: o chip vizinho de "ritmo da pelicula" e obrigado a
    // dizer "vale ao reabrir", porque BufferCount e latencia maxima nascem com o swapchain
    // e trocar isso em voo pediria ResizeBuffers com a cadeia NGX viva - medido duas vezes
    // como CreateFeature 0xC0000005 (armadilha 7b). Pondo a flag no nascimento, a NOSSA
    // opcao nao herda esse defeito: trocar entre "Certinha" e "Na hora" passa a ser so um
    // par de argumentos diferente no proximo Present, **valendo no quadro seguinte**, sem
    // reabrir nada e sem tocar no swapchain. Era esse o pedido - um botao que resolve.
    //
    // (Nesta arvore nao existe nenhum ResizeBuffers - Renderer::Rebuild foi removido -,
    //  entao a flag vive o swapchain inteiro e nao ha ponto onde ela se perca.)
    if (p.tearingDisponivel) p.flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    p.tearingLigado = p.tearingDisponivel && (modo == Entrega::NaHora || modo == Entrega::Automatico);
    return p;
}

// =====================================================================================
//  3. O QUE PASSAR NO Present()
// =====================================================================================
//
// Substitui, no ponto do Present, o par (syncInterval, flags) de hoje.
// Hoje o app faz:  sc->Present((vsync && !esperaExterna) ? 1 : 0, 0);
// Passa a fazer:   sc->Present(MeioRefresh::SyncInterval(...), MeioRefresh::FlagsDePresent(...));
//
// REGRA DURA DO DXGI, e a razao de as duas funcoes andarem juntas: DXGI_PRESENT_ALLOW_TEARING
// **so** e valido com syncInterval == 0 e **so** numa swapchain criada com a flag. Fora
// disso o Present devolve DXGI_ERROR_INVALID_CALL e a pelicula congela. Por isso o
// `agora` (a decisao do modo Automatico) entra nas duas.
inline UINT SyncInterval(bool vsyncPedido, bool tearingAgora) {
    if (tearingAgora) return 0;          // obrigatorio
    return vsyncPedido ? 1u : 0u;        // o comportamento de hoje, intacto
}

inline UINT FlagsDePresent(bool tearingCriado, bool tearingAgora) {
    return (tearingCriado && tearingAgora) ? (UINT)DXGI_PRESENT_ALLOW_TEARING : 0u;
}

// =====================================================================================
//  4. O DIAGNOSTICO
// =====================================================================================

enum class Veredito : int {
    Desconhecido = 0,  // ainda medindo, ou o DXGI nao respondeu
    Direto,            // o quadro vai direto para a tela - nada a fazer
    Composto,          // uma atualizacao a mais no meio do caminho (o caso (B))
    MeioRefresh,       // METADE dos quadros nao aparece (o caso (A) - regressao grave)
};

struct Estado {
    Veredito veredito       = Veredito::Desconhecido;
    double   atrasoMs       = 0.0;   // PRESENT->TELA medio da janela
    double   atrasoRefresh  = 0.0;   // o mesmo, em unidades de refresh  <- o que importa
    double   periodoMs      = 0.0;   // periodo real do monitor
    double   hz             = 0.0;
    double   fpsPresent     = 0.0;
    int      nComposto      = 0;     // quadros que o DXGI disse COMPOSED
    int      nOverlay       = 0;     // ...e OVERLAY (plano de hardware)
    bool     dxgiRespondeu  = false; // o CompositionMode veio? (se nao, so temos (ii))
    bool     tearingAtivo   = false;
    bool     mpoDesligado   = false; // a chave do Windows esta la?
    bool     precisaReiniciar = false; // ...e mudou desde o boot?
};

// Le a chave de MPO do Windows. SO LEITURA, HKLM, sem privilegio nenhum - abrir uma
// chave de HKLM para leitura e permitido a qualquer usuario.
//   ausente ou 0 -> MPO no padrao do Windows (ligado)
//   5            -> MPO DESLIGADO na maquina inteira
inline bool MpoDesligadoNoRegistro(DWORD* valorFora = nullptr) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\Dwm", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    DWORD v = 0, cb = sizeof(v), tipo = 0;
    const LONG r = RegQueryValueExW(k, L"OverlayTestMode", nullptr, &tipo, (LPBYTE)&v, &cb);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || tipo != REG_DWORD) return false;
    if (valorFora) *valorFora = v;
    return v == 5;
}

class Detector {
public:
    // `todaJanela` = de quantos quadros em quantos o veredito e reavaliado. 300 casa com
    // o LAT do screen_capture.h, entao as duas linhas do log falam da mesma janela.
    explicit Detector(int todaJanela = 300) : m_janela(todaJanela) {
        QueryPerformanceFrequency(&m_freq);
        LerPeriodoDoMonitor();
    }

    void DefinirTearingAtivo(bool v) { m_est.tearingAtivo = v; }

    // Chamado uma vez por quadro, LOGO DEPOIS do Present, no mesmo lugar onde o
    // LatProbe::Present ja e chamado. `atrasoMs` e exatamente o numero que o LatProbe
    // ja calcula (SyncQPCTime - qpc do fim do Present); passe-o pronto para nao medir
    // duas vezes a mesma coisa.
    //
    // `atrasoMs < 0` = nao ha amostra neste quadro (o PresentCount nao andou). E normal
    // e nao deve poluir a media.
    void Observar(IDXGISwapChain* sc, double atrasoMs, double fpsPresent) {
        m_est.fpsPresent = fpsPresent;
        if (atrasoMs >= 0.0) { m_somaAtraso += atrasoMs; m_nAtraso++; }

        // (i) a resposta direta do DXGI, quando ela existe
        if (sc && !m_semMedia) {
            if (!m_media) {
                if (FAILED(sc->QueryInterface(__uuidof(IDXGISwapChainMedia), (void**)&m_media)) || !m_media) {
                    m_media = nullptr; m_semMedia = true;   // nao insiste a cada quadro
                }
            }
            if (m_media) {
                DXGI_FRAME_STATISTICS_MEDIA fs{};
                if (SUCCEEDED(m_media->GetFrameStatisticsMedia(&fs))) {
                    m_est.dxgiRespondeu = true;
                    switch (fs.CompositionMode) {
                        case DXGI_FRAME_PRESENTATION_MODE_OVERLAY:  m_nOverlay++;  break;
                        case DXGI_FRAME_PRESENTATION_MODE_COMPOSED: m_nComposto++; break;
                        default: break;   // NONE / COMPOSITION_FAILURE: nao conta para nenhum lado
                    }
                }
            }
        }

        if (++m_n >= m_janela) Fechar();
    }

    Estado Ler() const { return m_est; }

    // Solta a interface. Chamar antes de o swapchain morrer.
    void Soltar() { if (m_media) { m_media->Release(); m_media = nullptr; } m_semMedia = false; }
    ~Detector() { Soltar(); }

    // Decisao do modo Automatico: "devo mandar o quadro na hora?".
    // Conservador de proposito - so liga o tearing depois de o veredito Composto se
    // sustentar por DUAS janelas seguidas (~10 s). Ligar por causa de um pico seria
    // exatamente o falso positivo que este projeto proibe.
    bool DeveMandarNaHora(Entrega modo) const {
        if (modo == Entrega::NaHora)    return true;
        if (modo != Entrega::Automatico) return false;
        return m_composto2x;
    }

private:
    void LerPeriodoDoMonitor() {
        DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti);
        if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.qpcRefreshPeriod > 0 && m_freq.QuadPart) {
            m_est.periodoMs = (double)ti.qpcRefreshPeriod * 1000.0 / (double)m_freq.QuadPart;
        }
        // Reserva: o modo de video do monitor primario. Vale quando o DWM nao responde
        // (sessao remota, por exemplo). Nunca fica em "60 chutado".
        if (m_est.periodoMs <= 0.0) {
            DEVMODEW dm{}; dm.dmSize = sizeof(dm);
            if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
                m_est.periodoMs = 1000.0 / (double)dm.dmDisplayFrequency;
        }
        m_est.hz = m_est.periodoMs > 0.0 ? 1000.0 / m_est.periodoMs : 0.0;
    }

    void Fechar() {
        LerPeriodoDoMonitor();           // pode ter mudado (monitor variavel, troca de modo)
        m_est.atrasoMs      = m_nAtraso ? (m_somaAtraso / m_nAtraso) : 0.0;
        m_est.atrasoRefresh = m_est.periodoMs > 0.0 ? (m_est.atrasoMs / m_est.periodoMs) : 0.0;
        m_est.nComposto     = m_nComposto;
        m_est.nOverlay      = m_nOverlay;
        DWORD vmpo = 0;
        m_est.mpoDesligado  = MpoDesligadoNoRegistro(&vmpo);

        const Veredito ant = m_est.veredito;
        m_est.veredito = Julgar();
        m_composto2x = (m_est.veredito == Veredito::Composto && ant == Veredito::Composto);

        Log("MEIO-REFRESH: %s | atraso %.2f ms = %.2f atualizacoes (tela %.1f Hz, %.2f ms)"
            " | present %.1f fps | DXGI: %s%d direto / %d compondo | na hora=%s | MPO no registro=%s",
            Rotulo(m_est.veredito), m_est.atrasoMs, m_est.atrasoRefresh, m_est.hz, m_est.periodoMs,
            m_est.fpsPresent,
            m_est.dxgiRespondeu ? "" : "(nao respondeu) ", m_est.nOverlay, m_est.nComposto,
            m_est.tearingAtivo ? "sim" : "nao",
            m_est.mpoDesligado ? "DESLIGADO (chave=5)" : "no padrao");

        m_n = 0; m_nAtraso = 0; m_somaAtraso = 0.0; m_nOverlay = 0; m_nComposto = 0;
    }

    Veredito Julgar() const {
        // (A) primeiro, porque e o grave: metade dos quadros sumindo. So se pronuncia
        // quando ha taxa de tela conhecida E o present esta cravado na metade dela.
        if (m_est.hz > 1.0 && m_est.fpsPresent > 1.0) {
            const double razao = m_est.fpsPresent / m_est.hz;
            if (razao > 0.42 && razao < 0.58 && m_est.atrasoRefresh > 1.5)
                return Veredito::MeioRefresh;
        }
        // (i) a resposta do DXGI manda, quando ela veio e e clara (>=80% de um lado).
        const int tot = m_nOverlay + m_nComposto;
        if (m_est.dxgiRespondeu && tot >= 8) {
            if (m_nOverlay  >= (tot * 4) / 5) return Veredito::Direto;
            if (m_nComposto >= (tot * 4) / 5) return Veredito::Composto;
        }
        // (ii) na falta dela, o atraso em unidades de refresh. A faixa 1,35-1,60 fica de
        // proposito SEM veredito: e onde os dois caminhos se encostam e cravar ali seria
        // inventar. O painel mostra "Desconhecido" e nao avisa nada.
        if (m_nAtraso >= 8 && m_est.periodoMs > 0.0) {
            if (m_est.atrasoRefresh >= 1.60) return Veredito::Composto;
            if (m_est.atrasoRefresh <= 1.35) return Veredito::Direto;
        }
        return Veredito::Desconhecido;
    }

    static const char* Rotulo(Veredito v) {
        switch (v) {
            case Veredito::Direto:      return "caminho direto";
            case Veredito::Composto:    return "UMA ATUALIZACAO A MAIS (o Windows esta compondo)";
            case Veredito::MeioRefresh: return "METADE DOS QUADROS NAO APARECE - regressao grave";
            default:                    return "ainda medindo";
        }
    }

    LARGE_INTEGER        m_freq{};
    int                  m_janela = 300;
    int                  m_n = 0, m_nAtraso = 0, m_nOverlay = 0, m_nComposto = 0;
    double               m_somaAtraso = 0.0;
    bool                 m_composto2x = false;
    bool                 m_semMedia = false;
    IDXGISwapChainMedia* m_media = nullptr;
    Estado               m_est;
};

// =====================================================================================
//  5. O TEXTO DO PAINEL
// =====================================================================================
//
// Regra do usuario, nas palavras dele: "explica de um jeito que o usuario entende
// carai". Entao: no rotulo e nas opcoes, ZERO sigla. "MPO", "Independent Flip",
// "ALLOW_TEARING", "DWM" e "vblank" so podem aparecer na Dica - que e onde quem sabe o
// nome da coisa vai procurar.
//
// A frase que nao pode aparecer: "desativa o MPO". Nao e isso que o botao faz.

inline const wchar_t* Rotulo(bool en) {
    return en ? L"HOW FAST THE PICTURE REACHES THE SCREEN"
              : L"QUANTO A IMAGEM DEMORA PARA APARECER";
}

inline const wchar_t* NomeOpcao(Entrega e, bool en) {
    switch (e) {
        case Entrega::Esperando:  return en ? L"Certinha"     : L"Certinha";
        case Entrega::NaHora:     return en ? L"Na hora"      : L"Na hora";
        case Entrega::Automatico: return en ? L"Automatic"    : L"Automatica";
    }
    return L"";
}

inline const wchar_t* SubOpcao(Entrega e, bool en) {
    switch (e) {
        case Entrega::Esperando:
            return en ? L"waits for the screen to turn - never tears"
                      : L"espera a tela virar - a imagem nunca parte ao meio";
        case Entrega::NaHora:
            return en ? L"shows up sooner; a horizontal seam may appear when you swing the camera"
                      : L"aparece antes; pode surgir um risco no meio da tela ao virar a camera rapido";
        case Entrega::Automatico:
            return en ? L"the app measures and only speeds up if the delay is really there"
                      : L"o app mede e so acelera se o atraso estiver mesmo acontecendo";
    }
    return L"";
}

inline const wchar_t* Dica(bool en) {
    return en
        ? L"Windows can hand our picture straight to the display, or route it through the desktop "
          L"compositor first (Independent Flip / MPO). Through the compositor every frame waits one "
          L"extra screen refresh - 16,7 ms on a 60 Hz screen, which is where the ~30 ms comes from. "
          L"\"Na hora\" uses ALLOW_TEARING to skip that queue; it only has any effect when the direct "
          L"path is already available, so if the number does not move, the compositor is in the way."
        : L"O Windows pode entregar a nossa imagem direto para o monitor ou passar antes pelo "
          L"compositor da area de trabalho (Independent Flip / MPO). Pelo compositor, cada imagem "
          L"espera UMA atualizacao a mais da tela - 16,7 ms num monitor de 60 Hz, que e de onde saem "
          L"os ~30 ms. \"Na hora\" usa ALLOW_TEARING para pular essa fila; ela so faz efeito quando o "
          L"caminho direto ja existe, entao, se o numero nao mexer, quem esta no meio e o compositor.";
}

// A LINHA VIVA DO PAINEL. Vazia = nao ha nada a dizer, e ai NAO desenhe linha nenhuma
// (o painel ja tem historico de linha cortada por medir a menos - armadilha 21).
inline std::wstring LinhaDeAviso(const Estado& s, bool en) {
    wchar_t buf[384];
    switch (s.veredito) {
        case Veredito::MeioRefresh:
            _snwprintf(buf, 384,
                en ? L"only half of the frames are reaching the screen (%.0f of %.0f per second). This is a bug, not a setting - please report it."
                   : L"so metade dos quadros esta chegando na tela (%.0f de %.0f por segundo). Isso e defeito, nao ajuste - avise a gente.",
                s.fpsPresent, s.hz);
            buf[383] = 0; return buf;
        case Veredito::Composto:
            _snwprintf(buf, 384,
                en ? L"your picture is taking %.1f screen refreshes (%.0f ms) to show up. \"Na hora\" removes one of them."
                   : L"a sua imagem esta levando %.1f atualizacoes da tela (%.0f ms) para aparecer. \"Na hora\" tira uma delas.",
                s.atrasoRefresh, s.atrasoMs);
            buf[383] = 0; return buf;
        default:
            return L"";
    }
}

// =====================================================================================
//  6. MPO - O ULTIMO RECURSO, E SO SE A MEDIDA PEDIR
// =====================================================================================
//
// LEIA ANTES DE CHAMAR QUALQUER COISA DAQUI.
//
// Esta chave NAO e do nosso app: e da MAQUINA INTEIRA, exige ADMINISTRADOR para
// escrever e **so vale depois de reiniciar o Windows**. Ela nao tem "modo so para o
// nosso jogo". Por isso:
//
//   - nada aqui e chamado sozinho, nunca, em hipotese nenhuma;
//   - nada aqui e chamado sem a pessoa ter confirmado numa caixa que diz o preco;
//   - `Ler()` grava o valor anterior em disco ANTES de qualquer escrita, e `Desfazer()`
//     devolve exatamente aquele estado (inclusive "a chave nao existia", que e
//     diferente de "a chave existia valendo 0");
//   - a tela TEM de dizer que precisa reiniciar. Sem reiniciar, nada muda, e a pessoa
//     vai medir de novo, ver o mesmo 30 ms e achar que o app mentiu.
//
// O DANO COLATERAL, escrito sem suavizar (e por isso o padrao e NUNCA mexer):
//   desligar os planos de sobreposicao tira o caminho barato de TODO o sistema, nao so
//   do nosso app. O que piora:
//     * video: YouTube, Netflix e qualquer player passam a ser compostos em vez de irem
//       para um plano proprio -> mais GPU ligada, mais consumo;
//     * notebook: e o caso pior. O plano de sobreposicao e justamente o que deixa a GPU
//       dormir enquanto so ha video na tela. Sem ele, a bateria cai visivelmente;
//     * multi-monitor e taxas diferentes entre telas costumam ficar PIORES sem MPO;
//     * em compensacao, ha uma classe conhecida de defeito (piscadas e engasgos em
//       algumas combinacoes de driver) que some desligando - e por isso que a receita
//       circula na internet.
//   Resumo honesto para a tela: "isto conserta um problema e pode criar outro, na
//   maquina inteira, e so vale depois de reiniciar".
//
// E O AVISO MAIS IMPORTANTE DE TODOS: nesta maquina a medida de 14/09 06:30 foi feita
// com a chave JA valendo 5 (MPO desligado) e o atraso deu 30 ms assim mesmo. Ou seja
// **nao ha evidencia de que mexer nela ajude**; ha evidencia de que ela nao explica
// sozinha o numero. Nao ofereca este botao antes de o Detector dizer "Composto" COM o
// MPO no padrao do Windows e depois de um reinicio.

namespace Mpo {

struct Backup {
    bool  existia = false;
    DWORD valor   = 0;
    bool  valido  = false;
};

inline Backup Ler() {
    Backup b;
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\Dwm", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        DWORD cb = sizeof(b.valor), tipo = 0;
        if (RegQueryValueExW(k, L"OverlayTestMode", nullptr, &tipo, (LPBYTE)&b.valor, &cb) == ERROR_SUCCESS && tipo == REG_DWORD)
            b.existia = true;
        RegCloseKey(k);
        b.valido = true;
    }
    return b;
}

// Grava o backup em texto, ao lado do ini do app. Em texto de proposito: se o app nunca
// mais abrir, a pessoa (ou o suporte) ainda consegue desfazer com uma linha de `reg`.
inline bool SalvarBackup(const wchar_t* caminho, const Backup& b) {
    if (!caminho || !b.valido) return false;
    FILE* f = _wfopen(caminho, L"w, ccs=UTF-8");
    if (!f) return false;
    if (b.existia)
        fwprintf(f, L"antes: HKLM\\SOFTWARE\\Microsoft\\Windows\\Dwm\\OverlayTestMode = %lu (REG_DWORD)\n"
                    L"desfazer: reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\Dwm\" /v OverlayTestMode /t REG_DWORD /d %lu /f\n",
                 (unsigned long)b.valor, (unsigned long)b.valor);
    else
        fwprintf(f, L"antes: a chave NAO EXISTIA (padrao do Windows)\n"
                    L"desfazer: reg delete \"HKLM\\SOFTWARE\\Microsoft\\Windows\\Dwm\" /v OverlayTestMode /f\n");
    fclose(f);
    return true;
}

// Escreve pedindo elevacao. NAO escreve direto: o app nao e (e nao deve ser) elevado.
// Dispara um `reg.exe` com "runas", que faz o Windows mostrar a caixa do UAC - a pessoa
// ve o pedido do sistema, alem do nosso aviso. `desligar=false` REMOVE a chave.
//
// Devolve false se a pessoa recusou o UAC. Recusar e um caminho normal, nao um erro.
inline bool Aplicar(bool desligar, const wchar_t* caminhoBackup) {
    const Backup b = Ler();
    if (caminhoBackup) SalvarBackup(caminhoBackup, b);

    wchar_t args[512];
    if (desligar)
        lstrcpyW(args, L"add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\Dwm\" /v OverlayTestMode /t REG_DWORD /d 5 /f");
    else
        lstrcpyW(args, L"delete \"HKLM\\SOFTWARE\\Microsoft\\Windows\\Dwm\" /v OverlayTestMode /f");

    SHELLEXECUTEINFOW si{}; si.cbSize = sizeof(si);
    si.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    si.lpVerb = L"runas";            // <- o UAC aparece aqui
    si.lpFile = L"reg.exe";
    si.lpParameters = args;
    si.nShow = SW_HIDE;
    if (!ShellExecuteExW(&si) || !si.hProcess) {
        Log("MPO: a pessoa recusou a elevacao (ou o sistema negou) - NADA foi alterado");
        return false;
    }
    WaitForSingleObject(si.hProcess, 10000);
    DWORD rc = 1; GetExitCodeProcess(si.hProcess, &rc); CloseHandle(si.hProcess);
    Log("MPO: %s a chave OverlayTestMode - reg.exe devolveu %lu. **So vale depois de REINICIAR o Windows.**",
        desligar ? "gravei" : "removi", (unsigned long)rc);
    return rc == 0;
}

// Desfaz usando o backup em memoria (o mesmo que foi para o disco).
inline bool Desfazer(const Backup& b) {
    if (!b.valido) return false;
    return Aplicar(b.existia && b.valor == 5, nullptr);
}

inline const wchar_t* TextoDoAviso(bool en) {
    return en
        ? L"This changes a WINDOWS setting, not an app setting: it affects the whole machine, needs "
          L"administrator, and only takes effect AFTER YOU RESTART. It can make video playback use "
          L"more power and, on a laptop, noticeably shorten battery life. We save the previous value "
          L"and the Undo button puts it back."
        : L"Isto muda uma configuracao DO WINDOWS, nao do app: vale para a maquina inteira, precisa de "
          L"administrador e SO FUNCIONA DEPOIS DE REINICIAR. Pode fazer video (YouTube, Netflix) gastar "
          L"mais energia e, em notebook, encurtar a bateria de forma perceptivel. Guardamos o valor "
          L"anterior e o botao Desfazer devolve ele.";
}

} // namespace Mpo
} // namespace MeioRefresh
