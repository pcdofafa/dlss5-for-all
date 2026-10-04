// =====================================================================================
//  flip_prova.h  -  "o quadro foi DIRETO para a tela, ou o DWM compos?"
//
//  POR QUE ESTE ARQUIVO EXISTE. O `RegistrarComposicao` (main.cpp) responde "as CONDICOES
//  estao satisfeitas?" e diz, com todas as letras, que nao pode afirmar qual caminho
//  engatou - "confirmar com PresentMon qual plano engatou". Isso deixou o projeto inteiro
//  argumentando sobre o `p` (present -> tela): 20-28 ms no O1, atribuidos a "a pelicula e
//  uma janela LAYERED". **Ela nao e** (ver main.cpp, bloco do NOREDIRECTIONBITMAP: em tela
//  cheia o LAYERED nao entra desde a M6). Ou seja: a explicacao que esta escrita em tres
//  arquivos nao bate com o codigo, e ninguem tinha como decidir de dentro do processo.
//
//  TEM COMO. `IDXGISwapChainMedia::GetFrameStatisticsMedia()` devolve `CompositionMode`:
//    OVERLAY  = o quadro foi para um PLANO DE HARDWARE - caminho direto, sem composicao
//    COMPOSED = o DWM compos aquele quadro
//    NONE / COMPOSITION_FAILURE = nao ha resposta para aquele quadro
//  Nao precisa de privilegio, de registro nem de ferramenta de fora, e existe em qualquer
//  swapchain de flip model (Windows 8.1+). O diagnostico e do PROPRIO DXGI, nao uma
//  inferencia nossa a partir de um tempo. Ver meiorefresh.h, que descreve o mesmo
//  instrumento em detalhe (este arquivo e a versao enxuta, ligada de verdade).
//
//  O SEGUNDO INSTRUMENTO, independente: `IDXGISwapChain::GetFrameStatistics`. Numa janela
//  composta ele costuma devolver DXGI_ERROR_FRAME_STATISTICS_DISJOINT ou nao andar; no
//  caminho direto ele responde e os contadores andam. Dois instrumentos que precisam
//  CONCORDAR - e a regra desta arvore para nao transformar um numero em veredito.
//
//  E O QUE ISTO **NAO** FAZ: nao muda nada. E so medida. Quem mexe em estilo de janela,
//  swapchain ou ordem Z e o main.cpp - e agora passa a ter como saber se adiantou.
// =====================================================================================
#pragma once

#include <windows.h>
#include <dxgi1_3.h>   // IDXGISwapChainMedia, DXGI_FRAME_STATISTICS_MEDIA
#include <dxgi1_6.h>   // RODADA6: IDXGIOutput6 (HDR do monitor na linha pelicula:)
#include <dwmapi.h>
#include <string>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

namespace FlipProva {

// Le a chave de MPO do Windows (SO LEITURA), so para constar no log.
// RODADA6 (23/09/2026, OCLUSAO-2309 §13): esta chave NAO EXISTE MAIS nesta maquina (Windows
// 11 25H2, dxdiag de 23/09) e, no 24H2+, nem vale quando existe (guia do Lossless Scaling).
// Antes a linha concluia "MPO DESLIGADO NESTA MAQUINA" por ela - e isso era falso. Nenhum
// veredito sai daqui: quem diz se a pelicula esta em plano de hardware e o DXGI
// (CompositionMode, ver Iflip abaixo). E o DirectFlip no plano PRIMARIO nem depende de MPO.
inline DWORD OverlayTestMode(bool* achou) {
    HKEY k = nullptr; if (achou) *achou = false;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\Dwm", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return 0;
    DWORD v = 0, cb = sizeof(v), tipo = 0;
    const LONG r = RegQueryValueExW(k, L"OverlayTestMode", nullptr, &tipo, (LPBYTE)&v, &cb);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || tipo != REG_DWORD) return 0;
    if (achou) *achou = true;
    return v;
}

class Sonda {
public:
    // `janela` e a nossa janela visivel; `bbW/bbH` o tamanho do backbuffer; `rasgo` se o
    // swapchain nasceu com ALLOW_TEARING. `aCada` presents fecham uma janela de medida
    // (300 casa com o LAT do screen_capture.h: as duas linhas falam do mesmo pedaco).
    // FASE4 (22/09/2026): a sonda passou a GUARDAR A CLASSE da janela que recebeu, e a linha
    // do log passou a dizer qual e e se ela esta visivel. Motivo, medido: o
    // `RITMO-ABERTO-2209 (c)` leu `flip [pelicula]: ... SEM-TOPMOST(!)` e concluiu que a
    // pelicula perdia o estilo em regime. Nao perde. Quando a ponte de placas esta de pe, o
    // swapchain NAO vive na pelicula: o `main.cpp:26782` passa
    // `PontePlacas::JanelaDaRede(hwnd)` ao `R.Init`, que devolve a JANELA OCULTA DA REDE
    // (`FafaDLSS5RedeWnd`, criada em `main.cpp:23232` com so `WS_EX_TOOLWINDOW |
    // WS_EX_NOACTIVATE`) - sem TOPMOST, sem NOREDIR, sem TRANSPARENT, exatamente o que o log
    // imprimia. A prova estava na propria linha seguinte: o `PorCima` listava
    // `FafaDLSS5CamWnd` (a PELICULA) acima "da nossa janela".
    // Sem o nome da classe no log, a linha e ambigua e ja custou uma investigacao inteira.
    void Preparar(HWND janela, UINT bbW, UINT bbH, bool rasgo, int aCada = 300) {
        Soltar();
        m_janela = janela; m_bbW = bbW; m_bbH = bbH; m_rasgo = rasgo;
        m_classe[0] = 0;
        if (janela && IsWindow(janela)) {
            wchar_t cls[64]{}; GetClassNameW(janela, cls, 64);
            _snprintf(m_classe, sizeof(m_classe), "%ls", cls);
            m_classe[sizeof(m_classe) - 1] = 0;
        }
        m_aCada = aCada > 8 ? aCada : 300;
        m_n = m_overlay = m_composto = m_outro = 0;
        m_mediaRespondeu = m_statsRespondeu = false;
        m_statsErro = S_OK; m_presentCount = m_refreshCount = 0;
    }

    // Uma vez por Present. Barata: o GetFrameStatisticsMedia e amostrado de kAmostra em
    // kAmostra, pelo mesmo motivo do MedirP (cada chamada e uma ida ao DXGI/compositor).
    // Devolve true quando a janela FECHOU e ha linha nova para o log.
    bool Observar(IDXGISwapChain* sc) {
        if (!sc) return false;
        if (++m_amostra >= kAmostra) {
            m_amostra = 0;
            if (!m_media && !m_semMedia) {
                if (FAILED(sc->QueryInterface(__uuidof(IDXGISwapChainMedia), (void**)&m_media)) || !m_media) {
                    m_media = nullptr; m_semMedia = true;   // nao insiste a cada quadro
                }
            }
            if (m_media) {
                DXGI_FRAME_STATISTICS_MEDIA fs{};
                if (SUCCEEDED(m_media->GetFrameStatisticsMedia(&fs))) {
                    m_mediaRespondeu = true;
                    switch (fs.CompositionMode) {
                    case DXGI_FRAME_PRESENTATION_MODE_OVERLAY:  m_overlay++;  break;
                    case DXGI_FRAME_PRESENTATION_MODE_COMPOSED: m_composto++; break;
                    default: m_outro++; break;   // NONE / COMPOSITION_FAILURE
                    }
                }
            }
            // O segundo instrumento, e ele e o criterio que o time pediu: "o
            // GetFrameStatistics passa a responder". Guardamos o erro EXATO - dizer so
            // "nao respondeu" nao distingue DISJOINT (houve corte na sequencia) de
            // NOT_CURRENTLY_AVAILABLE (a janela nao esta num caminho que os produza).
            DXGI_FRAME_STATISTICS st{};
            const HRESULT hr = sc->GetFrameStatistics(&st);
            if (SUCCEEDED(hr)) {
                m_statsRespondeu = true;
                m_presentCount = st.PresentCount; m_refreshCount = st.PresentRefreshCount;
            }
            m_statsErro = hr;
        }
        return (++m_n >= m_aCada);
    }

    // A linha do log. `pMs` e o present->tela que o chamador ja mede (negativo = nao tem).
    // Monta tudo o que decide o caminho direto, para nao ser preciso cruzar cinco linhas.
    std::string Linha(const char* quem, double pMs) {
        char b[1600];   // FASE4: +classe da janela e +sufixo do TOPMOST
        // o periodo real da tela, nunca chutado em 60 Hz
        double periodo = 0.0;
        DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti);
        LARGE_INTEGER f{}; QueryPerformanceFrequency(&f);
        const bool dwmOk = SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.qpcRefreshPeriod > 0 && f.QuadPart;
        if (dwmOk) periodo = (double)ti.qpcRefreshPeriod * 1000.0 / (double)f.QuadPart;
        // O `p` nao existe em todo caminho (o Renderer da pelicula nao o mede; o apresentador
        // mede). Escrever "0,00 ms" onde nao houve medida seria o melhor numero possivel
        // saindo de lugar nenhum - o tipo de linha que este projeto ja aprendeu a nao escrever.
        char pTxt[96];
        if (pMs > 0.0 && periodo > 0.0)
            _snprintf(pTxt, sizeof(pTxt), "p %.2f ms = %.2f refresh", pMs, pMs / periodo);
        else if (pMs > 0.0)
            _snprintf(pTxt, sizeof(pTxt), "p %.2f ms (sem periodo da tela)", pMs);
        else
            _snprintf(pTxt, sizeof(pTxt), "p nao medido neste caminho");
        pTxt[sizeof(pTxt) - 1] = 0;

        RECT rc{}, rw{}; UINT cw = 0, ch = 0;
        LONG_PTR ex = 0;
        char mon[64] = "?";
        // FASE4 (22/09/2026): visivel ou nao muda o SENTIDO de metade desta linha. Numa janela
        // oculta (a da rede, na ponte de placas) TOPMOST e NOREDIRECTIONBITMAP nao querem dizer
        // nada - o DWM nem a compoe -, entao o "(!)" de alarme nao pode aparecer ali.
        const bool visivel = m_janela && IsWindow(m_janela) && IsWindowVisible(m_janela);
        if (m_janela && IsWindow(m_janela)) {
            GetClientRect(m_janela, &rc); GetWindowRect(m_janela, &rw);
            cw = (UINT)(rc.right - rc.left); ch = (UINT)(rc.bottom - rc.top);
            ex = GetWindowLongPtrW(m_janela, GWL_EXSTYLE);
            MONITORINFO mi{}; mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(MonitorFromWindow(m_janela, MONITOR_DEFAULTTONEAREST), &mi)) {
                const bool igual = (rw.left == mi.rcMonitor.left && rw.top == mi.rcMonitor.top &&
                                    rw.right == mi.rcMonitor.right && rw.bottom == mi.rcMonitor.bottom);
                _snprintf(mon, sizeof(mon), igual ? "= monitor" : "!= monitor %ldx%ld",
                          (long)(mi.rcMonitor.right - mi.rcMonitor.left),
                          (long)(mi.rcMonitor.bottom - mi.rcMonitor.top));
                mon[sizeof(mon) - 1] = 0;
            }
        }
        bool temMpo = false; const DWORD mpo = OverlayTestMode(&temMpo);

        // O VEREDITO, e ele e conservador: so "DIRETO" com o DXGI dizendo OVERLAY na
        // maioria dos quadros. Sem resposta do DXGI nao ha veredito nenhum - um tempo
        // pequeno tambem sai de uma fila vazia, e ja enganou este projeto antes.
        const char* ver = !m_mediaRespondeu ? "SEM VEREDITO (o DXGI nao respondeu CompositionMode)"
                        : (m_overlay > m_composto) ? "DIRETO (plano de hardware)"
                        : (m_composto > 0)         ? "COMPOSTO pelo DWM"
                                                   : "SEM VEREDITO (nenhum quadro classificado)";
        _snprintf(b, sizeof(b),
            "flip [%s @ %s %s]: %s - DXGI overlay=%u composto=%u outros=%u | GetFrameStatistics %s"
            " (0x%08lX, present=%u refresh=%u) | %s (tela %.0f Hz%s)"
            " | janela %ldx%ld %s, cliente %ux%u %s backbuffer %ux%u | estilo%s%s%s%s%s"
            " | rasgo=%d | MPO: OverlayTestMode=%s%s",
            quem, m_classe[0] ? m_classe : "?", visivel ? "VISIVEL" : "OCULTA",
            ver, m_overlay, m_composto, m_outro,
            m_statsRespondeu ? "RESPONDE" : "nao responde",
            (unsigned long)m_statsErro, (unsigned)m_presentCount, (unsigned)m_refreshCount,
            pTxt, periodo > 0.0 ? 1000.0 / periodo : 0.0,
            dwmOk ? "" : ", DWM nao respondeu",
            (long)(rw.right - rw.left), (long)(rw.bottom - rw.top), mon,
            cw, ch, (cw == m_bbW && ch == m_bbH) ? "=" : "!=", m_bbW, m_bbH,
            (ex & WS_EX_LAYERED)             ? (visivel ? " LAYERED(!)" : " LAYERED") : " sem-LAYERED",
            (ex & WS_EX_NOREDIRECTIONBITMAP) ? " NOREDIR"    : "",
            // FASE4 (22/09/2026): so a janela VISIVEL precisa de TOPMOST. A da rede e oculta
            // de proposito e o "(!)" ali era alarme falso - ver o comentario do Preparar.
            (ex & WS_EX_TOPMOST)             ? " TOPMOST"
                                             : (visivel ? " SEM-TOPMOST(!)" : " sem-TOPMOST(ok, janela oculta)"),
            (ex & WS_EX_TRANSPARENT)         ? " TRANSPARENT": "",
            (ex & WS_EX_NOACTIVATE)          ? " NOACTIVATE" : "",
            (int)m_rasgo,
            temMpo ? (mpo == 5 ? "5" : "outro") : "ausente",
            // RODADA6: sem veredito pela chave (ver OverlayTestMode). O veredito e o do DXGI, acima.
            temMpo ? " (a chave existe, mas no Windows 11 24H2+ ela nao decide nada - vale o DXGI)"
                   : " (a chave nao existe mais neste Windows - vale o DXGI)");
        b[sizeof(b) - 1] = 0;
        m_n = m_overlay = m_composto = m_outro = 0;   // proxima janela
        return std::string(b);
    }

    // Quem esta POR CIMA da nossa janela e visivel derruba o caminho direto - o DWM passa a
    // ter de compor os dois. Isto lista os nossos (painel, cartaz, guia, selo): sao os
    // unicos que temos como fechar. Vazio = nada nosso atrapalhando.
    std::string PorCima() const {
        if (!m_janela || !IsWindow(m_janela)) return std::string();
        // FASE4 (22/09/2026): numa janela OCULTA a ordem Z nao diz nada - todo mundo esta "por
        // cima" dela, inclusive a nossa propria pelicula. Era assim que a linha antiga
        // listava `FafaDLSS5CamWnd` como se algo nosso estivesse atrapalhando.
        if (!IsWindowVisible(m_janela))
            return std::string("(janela oculta - ordem Z nao se aplica; quem apresenta na tela e o apresentador)");
        RECT nosso{}; GetWindowRect(m_janela, &nosso);
        const DWORD eu = GetCurrentProcessId();
        std::string fora;
        for (HWND w = GetWindow(m_janela, GW_HWNDPREV); w; w = GetWindow(w, GW_HWNDPREV)) {
            if (!IsWindowVisible(w) || IsIconic(w)) continue;
            RECT r{}, ix{};
            if (!GetWindowRect(w, &r) || !IntersectRect(&ix, &r, &nosso)) continue;
            DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
            wchar_t cls[64]{}; GetClassNameW(w, cls, 64);
            char c8[96]; _snprintf(c8, sizeof(c8), "%ls%s", cls, pid == eu ? "" : "(de terceiro)");
            c8[sizeof(c8) - 1] = 0;
            if (!fora.empty()) fora += ", ";
            fora += c8;
        }
        return fora;
    }

    void Soltar() { if (m_media) { m_media->Release(); m_media = nullptr; } m_semMedia = false; }
    ~Sonda() { Soltar(); }

    // (a Regua, la embaixo, le o MESMO GetFrameStatistics - mas a cada Present, e nao de 8 em
    //  8 como aqui. Sao dois instrumentos com cadencias diferentes lendo a mesma fonte; a
    //  Sonda fica com a leitura dela para o veredito dela nao depender do estado da Regua.)

private:
    static const int kAmostra = 8;
    HWND m_janela = nullptr;
    char m_classe[64] = {0};   // FASE4: a classe da janela do swapchain, no log
    UINT m_bbW = 0, m_bbH = 0;
    bool m_rasgo = false;
    int  m_aCada = 300, m_n = 0, m_amostra = 0;
    IDXGISwapChainMedia* m_media = nullptr;
    bool m_semMedia = false, m_mediaRespondeu = false, m_statsRespondeu = false;
    unsigned m_overlay = 0, m_composto = 0, m_outro = 0;
    HRESULT  m_statsErro = S_OK;
    UINT     m_presentCount = 0, m_refreshCount = 0;
};

// =====================================================================================
//  A REGUA DE FLUIDEZ NATIVA  (RITMO/OCLUSAO, 23/09/2026)
//
//  POR QUE ELA EXISTE. O criterio do dono mudou: nao basta o app MARCAR 60 fps, a tela tem
//  de MOSTRAR 60 com espacamento perfeito. Ate aqui o numero que a bancada olhava era o
//  `p99` da linha `regularidade:` - e ele mede a VOLTA DO LACO (o intervalo entre dois
//  quadros nossos), nao a tela. Um laco perfeitamente regular que entrega o quadro 1 ms
//  depois do vblank produz a MESMA linha de um laco que entrega 1 ms antes, e os dois
//  aparecem na tela com espacamentos completamente diferentes. Nenhuma leva era
//  verificavel sob o criterio novo.
//
//  O INSTRUMENTO JA ESTAVA AQUI. `IDXGISwapChain::GetFrameStatistics` devolve, entre
//  outros, dois contadores cumulativos:
//     PresentCount        - quantas imagens ja foram APRESENTADAS na tela
//     PresentRefreshCount - em qual v-blank a ultima delas foi apresentada
//  Entao, entre dois quadros EXIBIDOS consecutivos (PresentCount avancou exatamente 1), o
//  delta de PresentRefreshCount e literalmente "quantos refreshes este quadro ficou na
//  tela". E o equivalente, medido de dentro do processo, do `MsBetweenDisplayChange` do
//  PresentMon - e nao precisa de ETW, de privilegio nem de ferramenta de fora.
//
//  O QUE A LINHA DIZ. O histograma desse delta ao longo de 10 s. Se TODO quadro fica no
//  mesmo numero de refreshes, o espacamento e nativo: a 100 Hz, `2 refreshes` em 100% dos
//  quadros e 50,0 fps perfeitamente espacados - o que a pessoa ve como um jogo nativo.
//  Misturar 2 e 3 refreshes e o judder: mesma media de fps, cadencia irregular, e e
//  exatamente isso que o `p99` do laco nao consegue enxergar.
//
//  "PERDIDOS" e a conta honesta e nao e necessariamente defeito: sao os Present() que
//  fizemos e que o DXGI nunca contou como exibidos - o flip model descarta o quadro que for
//  substituido antes do vblank. Com syncInterval 0 (taxa livre) isso e o comportamento
//  normal e o numero fica alto de proposito; com V-Sync ou com o N do RitmoDxgi ele tem de
//  ser perto de zero. A conta tem a defasagem da fila de apresentacao (1 a 3 quadros) no
//  fechamento da janela, o que e ruido em cima de centenas.
//
//  E ELA NAO MUDA NADA. Uma chamada ao DXGI por Present, um histograma de 16 posicoes e uma
//  linha de log a cada 10 s.
// =====================================================================================
class Regua {
public:
    static const int kMaxRefresh = 15;   // baldes 0..15; 15 e "15 ou mais"
    enum { kJanelaMs = 10000, kMinAmostras = 60, kTentativasAteDesistir = 240 };

    // Uma vez por Present, ANTES de qualquer outra leitura de estatistica (a Sonda aproveita
    // esta). Devolve true quando a janela de 10 s fechou e ha linha nova para o log.
    bool Amostrar(IDXGISwapChain* sc) {
        m_ultimoHr = E_FAIL;
        if (!sc) return false;
        // OCLUSAO-2309 §8: a regua do RELOGIO roda sempre - inclusive depois que o DXGI desistiu,
        // que e justamente o caso da janela composta.
        AmostrarRelogio(sc);
        if (m_desistiu) return Fechou();
        if (!m_inicio) m_inicio = GetTickCount64();
        m_presents++;
        DXGI_FRAME_STATISTICS st{};
        const HRESULT hr = sc->GetFrameStatistics(&st);
        m_ultimoHr = hr;
        if (FAILED(hr)) {
            // DISJOINT nao e "nao responde": e "houve um corte na sequencia, os contadores
            // nao sao comparaveis com os de antes". Quem ja respondeu uma vez so
            // RESSINCRONIZA. Desistir de vez e so para a janela que NUNCA respondeu - a
            // janela composta, onde estes contadores nao existem.
            m_temAnt = false;
            if (!m_jaRespondeu && ++m_falhasSeguidas >= (unsigned)kTentativasAteDesistir) {
                m_desistiu = true;
                m_hrDesistiu = hr;
                return true;   // o chamador loga a linha de indisponibilidade uma vez
            }
            return Fechou();
        }
        m_falhasSeguidas = 0;
        m_jaRespondeu = true;
        m_presentCount = st.PresentCount; m_refreshCount = st.PresentRefreshCount;
        if (!m_temAnt) { m_antP = st.PresentCount; m_antR = st.PresentRefreshCount; m_temAnt = true; return Fechou(); }
        if (st.PresentCount != m_antP) {
            const UINT dP = st.PresentCount - m_antP;
            const UINT dR = st.PresentRefreshCount - m_antR;
            m_antP = st.PresentCount; m_antR = st.PresentRefreshCount;
            m_exibidos += dP;
            if (dP == 1) {
                m_hist[dR > (UINT)kMaxRefresh ? kMaxRefresh : (int)dR]++;
                m_noHist++;
            } else {
                // Dois ou mais quadros foram exibidos entre duas leituras nossas: a leitura
                // e por Present, entao isto quer dizer que a tela andou mais que a gente. Nao
                // da para repartir o dR entre eles sem inventar, e inventar aqui seria
                // fabricar a propria metrica - ficam contados a parte.
                m_agrupados += dP;
            }
        }
        return Fechou();
    }

    bool Desistiu() const { return m_desistiu; }
    HRESULT HrDesistiu() const { return m_hrDesistiu; }
    HRESULT UltimoHr() const { return m_ultimoHr; }
    UINT PresentCountAtual() const { return m_presentCount; }
    UINT RefreshCountAtual() const { return m_refreshCount; }

    // Para o painel/status: a moda e o quanto dela, da ULTIMA janela fechada (a janela em
    // curso nao serve - um numero que muda a cada quadro nao e leitura, e piscar).
    int    Moda()    const { return m_ultModa; }
    double ModaPct() const { return m_ultModaPct; }
    bool   Nativo()  const { return m_ultNativo; }

    // A linha, e o zera-zera da proxima janela. So chamar quando Amostrar devolveu true.
    // `via` diz de QUAL swapchain ela saiu ("pelicula" ou "apresentador"): nesta maquina a
    // ponte de placas e forcada por precaucao mesmo com uma placa so, e ai quem apresenta na
    // tela e o apresentador - o swapchain da pelicula vai para uma janela OCULTA. Duas
    // reguas, uma em cada, e a linha diz qual delas viu a tela.
    std::string Linha(const char* via) {
        char b[640];
        if (m_desistiu) {
            _snprintf(b, sizeof(b),
                "tela: GetFrameStatistics nao responde neste caminho (0x%08lX) - regua do DXGI "
                "indisponivel (janela composta pelo DWM). Nao tento de novo nesta sessao. | via %s",
                (unsigned long)m_hrDesistiu, via);
            b[sizeof(b) - 1] = 0;
            std::string s(b);
            s += LinhaRelogio(true);
            m_inicio = GetTickCount64();
            return s;
        }
        // moda e os dois seguintes
        int i1 = -1, i2 = -1, i3 = -1;
        for (int k = 0; k <= kMaxRefresh; k++) {
            if (!m_hist[k]) continue;
            if (i1 < 0 || m_hist[k] > m_hist[i1]) { i3 = i2; i2 = i1; i1 = k; }
            else if (i2 < 0 || m_hist[k] > m_hist[i2]) { i3 = i2; i2 = k; }
            else if (i3 < 0 || m_hist[k] > m_hist[i3]) { i3 = k; }
        }
        const unsigned total = m_noHist;
        const double pct1 = total ? 100.0 * m_hist[i1 < 0 ? 0 : i1] / total : 0.0;
        // PERDIDOS: os Present que nunca viraram exibicao (ver o cabecalho). Piso em zero
        // porque a fila de apresentacao deixa 1 a 3 quadros pendentes no fechamento.
        const long perdidos = (long)m_presents - (long)m_exibidos;
        m_ultModa = (i1 < 0) ? -1 : i1;
        m_ultModaPct = pct1;
        m_ultEstimada = false;
        // AGRUPADOS ENTRAM NO VEREDITO. Sao os quadros que a tela exibiu entre duas leituras
        // nossas (PresentCount andou mais de 1 de uma vez): deles nao se sabe o espacamento
        // individual, e declarar "nativo" com um monte deles de fora seria dar o veredito com
        // base na parte da amostra que por acaso coube. Ate 1% e ruido de amostragem.
        const bool amostraLimpa = (m_exibidos == 0) || (100.0 * m_agrupados / (double)m_exibidos <= 1.0);
        m_ultNativo = (total >= (unsigned)kMinAmostras) && (pct1 >= 99.0) && amostraLimpa;
        char outros[160] = "";
        {   int n = 0; char* p = outros; size_t resta = sizeof(outros);
            const int cand[2] = { i2, i3 };
            for (int j = 0; j < 2; j++) {
                if (cand[j] < 0 || !m_hist[cand[j]] || !total) continue;
                const int esc = _snprintf(p, resta, "%s%d=%.1f%%", n ? " " : "",
                                          cand[j], 100.0 * m_hist[cand[j]] / total);
                if (esc <= 0 || (size_t)esc >= resta) break;
                p += esc; resta -= (size_t)esc; n++;
            }
        }
        if (!total) {
            // O CASO MEDIDO EM 23/09/2026, e ele tem de aparecer com todas as letras: com a
            // ponte de placas de pe o swapchain NAO vive na pelicula, vive na janela OCULTA
            // da rede (FafaDLSS5RedeWnd) - o GetFrameStatistics responde S_OK e os contadores
            // ficam em ZERO para sempre, porque aquela janela nunca vai a tela (quem
            // apresenta e o apresentador da outra placa). "0 quadros exibidos" sem essa
            // explicacao seria lido como "a tela travou", que e o contrario do que aconteceu.
            _snprintf(b, sizeof(b),
                "tela: 0 quadros exibidos em %llu s - nenhum delta limpo de PresentRefreshCount "
                "(agrupados %u, presents %u, PresentCount do DXGI %u). Sem veredito de espacamento%s | via %s",
                (unsigned long long)((GetTickCount64() - m_inicio) / 1000),
                m_agrupados, m_presents, (unsigned)m_presentCount,
                m_presentCount == 0
                    ? " - o PresentCount nao saiu de zero: este swapchain nao chega a tela "
                      "(janela oculta / quem apresenta e o apresentador externo)." : ".", via);
        } else {
            _snprintf(b, sizeof(b),
                "tela: %u quadros exibidos | espacamento %d refreshes: %.1f%% | outros: %s | "
                "perdidos %ld | nativo=%s%s%s | via %s",
                total, m_ultModa, pct1, outros[0] ? outros : "nenhum",
                perdidos > 0 ? perdidos : 0,
                m_ultNativo ? "sim" : "nao",
                (total < (unsigned)kMinAmostras) ? " (amostra curta)" : "",
                m_agrupados ? (amostraLimpa ? " (poucos agrupados)" : " (AMOSTRA COM AGRUPADOS - leitura mais lenta que a tela)") : "",
                via);
        }
        b[sizeof(b) - 1] = 0;
        std::string s(b);
        // OCLUSAO-2309 §8: sem nenhum quadro exibido pelo DXGI (janela composta: PresentCount
        // parado em zero), a regua do relogio assume o veredito - moda/pct/nativo vem dela.
        s += LinhaRelogio(total == 0);
        Zerar();
        return s;
    }
    bool Estimada() const { return m_ultEstimada; }

    // =================================================================================
    //  OCLUSAO-2309 §8: A REGUA DO RELOGIO (para a janela COMPOSTA)
    //
    //  POR QUE. Nesta maquina, com o DWM compondo (outros=37/38, sem iflip/MPO), o
    //  GetFrameStatistics responde S_OK e o PresentCount fica em ZERO para sempre - tanto na
    //  pelicula oculta quanto no apresentador visivel. A regua do DXGI nao mede nada ali.
    //
    //  ALTERNATIVAS PESADAS (e por que nao): ETW/PresentMon (Microsoft-Windows-DxgKrnl) da o
    //  MsBetweenDisplayChange de verdade, mas pede sessao ETW (admin ou "Performance Log
    //  Users") e uma thread consumidora; D3DKMT nao expoe o flip por janela composta;
    //  IDXGISwapChainMedia::GetFrameStatisticsMedia usa os MESMOS contadores (zerados).
    //
    //  O QUE ESTA FAZ. O DWM entrega a GRADE (DwmGetCompositionTimingInfo(NULL): qpcVBlank e
    //  qpcRefreshPeriod, que funcionam - o proprio log "dwm: compoe 240 Hz" le deles). Cada
    //  Present e carimbado com o QPC logo depois de voltar e jogado na grade: o indice do
    //  proximo vblank. Entre dois presents seguidos, o delta de indices e "em quantos
    //  refreshes este quadro ficaria na tela" se o DWM o compusesse no primeiro vblank
    //  depois do Present - a mesma pergunta da regua do DXGI, respondida pelo lado de quem
    //  apresenta. Delta 0 = dois presents no mesmo intervalo de vblank: o de tras SUBSTITUI o
    //  da frente antes de o DWM compor (quadro que ninguem viu). E ESTIMATIVA: nao ve a GPU
    //  atrasar o quadro depois do Present, nem o DWM perder uma composicao - quem mede isso e
    //  o `late/dropped/missed` da linha `dwm:`. So vale para janela VISIVEL (a oculta nunca
    //  vai a tela; ali a linha diz isso e nao inventa numero).
    // =================================================================================
    void AmostrarRelogio(IDXGISwapChain* sc) {
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        const ULONGLONG agora = GetTickCount64();
        if (!m_rInicio) m_rInicio = agora;
        if (!m_rGradeMs || agora - m_rGradeMs >= 100) {
            m_rGradeMs = agora;
            DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti);
            if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.qpcRefreshPeriod > 0 && ti.qpcVBlank > 0) {
                m_rVb = (long long)ti.qpcVBlank; m_rPer = (long long)ti.qpcRefreshPeriod;
                if (!m_rOrigem) m_rOrigem = m_rVb;
            }
            HWND hw = nullptr; DXGI_SWAP_CHAIN_DESC d{};
            if (SUCCEEDED(sc->GetDesc(&d))) hw = d.OutputWindow;
            m_rVisivel = hw && IsWindowVisible(hw) && !IsIconic(hw);
        }
        if (!m_rPer || !m_rOrigem) return;
        if (!m_rVisivel) { m_rOcultos++; m_rTemAnt = false; return; }
        m_rPresents++;
        // indice absoluto do proximo vblank depois deste Present, na grade que nasceu em m_rOrigem
        const long long base = (m_rVb - m_rOrigem + m_rPer / 2) / m_rPer;       // vblanks entre a origem e o vb lido
        long long dq = (long long)q.QuadPart - m_rVb;
        long long k = (dq >= 0) ? dq / m_rPer : -((-dq + m_rPer - 1) / m_rPer); // floor
        const long long idx = base + k + 1;
        if (m_rTemAnt) {
            const long long d = idx - m_rAntIdx;
            if (d <= 0) m_rMesmo++;
            else { m_rHist[d > kMaxRefresh ? kMaxRefresh : (int)d]++; m_rN++; }
        }
        m_rAntIdx = idx; m_rTemAnt = true;
    }
    std::string LinhaRelogio(bool assumeVeredito) {
        char b[420];
        const unsigned long long seg = m_rInicio ? (GetTickCount64() - m_rInicio) / 1000 : 0;
        if (!m_rPer) {
            _snprintf(b, sizeof(b), " || relogio: sem grade do DWM (DwmGetCompositionTimingInfo nao respondeu)");
        } else if (!m_rPresents) {
            _snprintf(b, sizeof(b), " || relogio: %u presents em %llu s numa janela OCULTA/minimizada - nao vai a tela, sem estimativa",
                      m_rOcultos, seg);
        } else {
            int i1 = -1, i2 = -1;
            for (int k = 0; k <= kMaxRefresh; k++) {
                if (!m_rHist[k]) continue;
                if (i1 < 0 || m_rHist[k] > m_rHist[i1]) { i2 = i1; i1 = k; }
                else if (i2 < 0 || m_rHist[k] > m_rHist[i2]) i2 = k;
            }
            const unsigned tot = m_rN;
            const double p1 = (tot && i1 >= 0) ? 100.0 * m_rHist[i1] / tot : 0.0;
            const double p2 = (tot && i2 >= 0) ? 100.0 * m_rHist[i2] / tot : 0.0;
            const double perMs = [&]{ LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart ? 1000.0 * (double)m_rPer / (double)f.QuadPart : 0.0; }();
            const bool nativo = tot >= (unsigned)kMinAmostras && p1 >= 99.0 && m_rMesmo * 100 <= m_rPresents;
            _snprintf(b, sizeof(b),
                " || relogio (ESTIMADA: QPC do Present na grade de vblank do DWM, %.2f ms): %u presents em %llu s, "
                "espacamento %d refreshes: %.1f%%%s%s | mesmo vblank (substituidos antes de compor) %u | nativo=%s",
                perMs, m_rPresents, seg, i1, p1,
                i2 >= 0 ? " | depois " : "", i2 >= 0 ? (std::to_string(i2) + "=" + std::to_string((int)(p2 + 0.5)) + "%").c_str() : "",
                m_rMesmo, nativo ? "sim" : "nao");
            if (assumeVeredito && tot) {
                m_ultModa = i1; m_ultModaPct = p1; m_ultNativo = nativo; m_ultEstimada = true;
            }
        }
        b[sizeof(b) - 1] = 0;
        for (int k = 0; k <= kMaxRefresh; k++) m_rHist[k] = 0;
        m_rN = 0; m_rMesmo = 0; m_rPresents = 0; m_rOcultos = 0; m_rInicio = GetTickCount64();
        return std::string(b);
    }

    void Zerar() {
        for (int k = 0; k <= kMaxRefresh; k++) m_hist[k] = 0;
        m_noHist = 0; m_presents = 0; m_exibidos = 0; m_agrupados = 0;
        m_inicio = GetTickCount64();
    }

private:
    bool Fechou() {
        if (!m_inicio) { m_inicio = GetTickCount64(); return false; }
        return GetTickCount64() - m_inicio >= (ULONGLONG)kJanelaMs;
    }
    unsigned  m_hist[kMaxRefresh + 1] = {0};
    unsigned  m_noHist = 0, m_presents = 0, m_exibidos = 0, m_agrupados = 0;
    ULONGLONG m_inicio = 0;
    UINT      m_antP = 0, m_antR = 0;
    bool      m_temAnt = false, m_jaRespondeu = false, m_desistiu = false;
    unsigned  m_falhasSeguidas = 0;
    HRESULT   m_hrDesistiu = S_OK, m_ultimoHr = S_OK;
    UINT      m_presentCount = 0, m_refreshCount = 0;
    int       m_ultModa = -1;
    double    m_ultModaPct = 0.0;
    bool      m_ultNativo = false;
    bool      m_ultEstimada = false;   // OCLUSAO-2309 §8: o veredito da ultima janela veio do relogio
    // regua do relogio (OCLUSAO-2309 §8)
    unsigned  m_rHist[kMaxRefresh + 1] = {0};
    unsigned  m_rN = 0, m_rMesmo = 0, m_rPresents = 0, m_rOcultos = 0;
    long long m_rVb = 0, m_rPer = 0, m_rOrigem = 0, m_rAntIdx = 0;
    bool      m_rTemAnt = false, m_rVisivel = false;
    ULONGLONG m_rGradeMs = 0, m_rInicio = 0;
};

// =====================================================================================
//  RODADA6 (23/09/2026, OCLUSAO-2309 §13): "A PELICULA ESTA EM FLIP INDEPENDENTE?", 1x/s
//
//  CALIBRACAO (bancada/regua-dda/sonda-iflip.exe, mesma maquina, 25G64 1920x1080@240 sem MPO):
//   - GetFrameStatisticsMedia().CompositionMode e o UNICO instrumento de dentro do processo que
//     separa os dois caminhos: OVERLAY 60/60 com a janela no tamanho exato e nada desenhando por
//     cima; COMPOSED 60/60 com o backbuffer 1 px menor, com uma janela de terceiro desenhando por
//     cima (o indicador do cursor de texto do Windows sobre o caret do terminal) e com uma
//     Desktop Duplication ativa. No LG (com MPO) deu OVERLAY mesmo com o indicador por cima.
//   - NAO servem: o PresentCount do GetFrameStatistics anda tambem composto (60/s, e ate 1000/s
//     com tearing); os contadores do DwmGetCompositionTimingInfo(NULL) andam 1 por CHAMADA
//     (cFrame, cRefresh...) e o qpcCompose e sempre ~6 ms no futuro - nao dizem se o DWM
//     dormiu; a Desktop Duplication diz, mas ela MESMA tira a janela do flip independente.
//  Por isso a linha "dwm:" (rateCompose) nao entra no veredito: ela e nominal.
//
//  A LINHA: `pelicula: iflip=sim|nao motivo=... | overlay X composto Y outro Z de N presents |
//  estilo ... | por cima: ...`. Com iflip=nao, o motivo lista o que o checklist da
//  PESQ-MONITOR-VIRTUAL §2.2 acha na hora: janela oculta/minimizada, janela != monitor,
//  cliente != backbuffer, LAYERED com alpha < 255, sem NOREDIRECTIONBITMAP, janela VISIVEL de
//  outro processo por cima, ponteiro do mouse visivel sobre a pelicula (com captura WGC ativa o
//  DWM compoe para desenha-lo - medido), captura de MONITOR ativa (so contexto), HDR do Windows
//  ligado com pelicula SDR. Nenhum encontrado = "causa nao identificada" (dito assim, sem chute).
//  ATENCAO: o OSD de volume do Windows derruba o iFlip por ~3 s e NAO aparece na lista (nao e
//  janela enumeravel na ordem Z) - ali a linha pode apontar um candidato inocente.
// =====================================================================================
inline volatile LONG g_capturaMonitor = 0;   // main.cpp: 1 enquanto a fonte e a captura do MONITOR
inline volatile LONG g_estiloPelicula = 'B'; // main.cpp: 'A' ou 'B' (chave pelicula_estilo)
inline volatile LONG g_cursorTrocado = 0;    // RODADA8: main.cpp: 1 enquanto os cursores de sistema estao trocados pelo vazio
inline volatile LONGLONG g_compostoDesdeMs = 0; // RODADA8: GetTickCount64 do inicio da sequencia ATUAL de presents COMPOSTOS da janela visivel (0 = OVERLAY)

class Iflip {
public:
    void Preparar(HWND janela, UINT bbW, UINT bbH, IDXGISwapChain* sc) {
        Soltar();
        m_janela = janela; m_bbW = bbW; m_bbH = bbH;
        m_n = m_ov = m_co = m_ou = 0; m_ini = GetTickCount64(); m_ultimo = -1; m_trocas = 0;
        m_hdr = false;
        if (sc) {
            IDXGIOutput* o = nullptr;
            if (SUCCEEDED(sc->GetContainingOutput(&o)) && o) {
                IDXGIOutput6* o6 = nullptr;
                if (SUCCEEDED(o->QueryInterface(__uuidof(IDXGIOutput6), (void**)&o6)) && o6) {
                    DXGI_OUTPUT_DESC1 d{};
                    if (SUCCEEDED(o6->GetDesc1(&d))) m_hdr = (d.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
                    o6->Release();
                }
                o->Release();
            }
        }
    }
    // Uma vez por Present (a chamada custa ~0 ms, medido na sonda). true = fechou 1 s.
    bool Observar(IDXGISwapChain* sc) {
        if (!sc) return false;
        if (!m_media && !m_semMedia) {
            if (FAILED(sc->QueryInterface(__uuidof(IDXGISwapChainMedia), (void**)&m_media)) || !m_media) { m_media = nullptr; m_semMedia = true; }
        }
        m_n++;
        // PRESENT -> TELA (RODADA6): o id deste Present (GetLastPresentCount) com o QPC de agora; quando o
        // GetFrameStatistics devolve esse id como exibido, SyncQPCTime - QPC = quanto o quadro levou do
        // Present ate a varredura. Composto = +1 composicao do DWM; em flip independente, sem ela.
        {
            IDXGISwapChain1* s1 = nullptr;
            UINT id = 0; LARGE_INTEGER q; QueryPerformanceCounter(&q);
            if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&s1)) && s1) {
                if (SUCCEEDED(s1->GetLastPresentCount(&id))) { m_rId[id % kR] = id; m_rQpc[id % kR] = q.QuadPart; }
                s1->Release();
            }
            DXGI_FRAME_STATISTICS st{};
            if (SUCCEEDED(sc->GetFrameStatistics(&st)) && st.PresentCount && st.PresentCount != m_ultExib &&
                m_rId[st.PresentCount % kR] == st.PresentCount && st.SyncQPCTime.QuadPart > m_rQpc[st.PresentCount % kR]) {
                m_ultExib = st.PresentCount;
                LARGE_INTEGER f; QueryPerformanceFrequency(&f);
                const double ms = 1000.0 * (double)(st.SyncQPCTime.QuadPart - m_rQpc[st.PresentCount % kR]) / (double)f.QuadPart;
                if (ms < 200.0 && m_nLat < kL) m_lat[m_nLat++] = ms;
            }
        }
        if (m_media) {
            DXGI_FRAME_STATISTICS_MEDIA fs{};
            if (SUCCEEDED(m_media->GetFrameStatisticsMedia(&fs))) {
                int modo = 2;
                if (fs.CompositionMode == DXGI_FRAME_PRESENTATION_MODE_OVERLAY) { m_ov++; modo = 1; }
                else if (fs.CompositionMode == DXGI_FRAME_PRESENTATION_MODE_COMPOSED) { m_co++; modo = 0; }
                else m_ou++;
                if (modo != 2) { if (m_ultimo >= 0 && modo != m_ultimo) m_trocas++; m_ultimo = modo; }
                // RODADA8: o cursor proprio (main.cpp) le isto para saber se o DWM voltou a compor com o ponteiro escondido
                if (modo == 0) { if (!g_compostoDesdeMs) g_compostoDesdeMs = (LONGLONG)GetTickCount64(); }
                else if (modo == 1) g_compostoDesdeMs = 0;
            } else m_ou++;
        } else m_ou++;
        return GetTickCount64() - m_ini >= 1000;
    }
    bool EmIflip() const { return m_emIflip; }
    // RODADA7: present->tela da ultima janela de 1 s (0 = sem medida). A cadencia usa em iFlip.
    double UltP50() const { return m_ultP50; }
    double UltP95() const { return m_ultP95; }
    unsigned UltN() const { return m_ultN; }
    // A linha, e zera a janela de 1 s.
    std::string Linha() {
        const unsigned cls = m_ov + m_co;
        m_emIflip = cls > 0 && m_ov * 10 >= cls * 9;   // >= 90% dos quadros classificados em OVERLAY
        std::string motivo;
        char b[1400];
        // estado da janela (o checklist)
        RECT rw{}, rc{}; LONG_PTR ex = 0; BYTE alfa = 255; DWORD lwaFl = 0; bool temAlfa = false;
        bool vis = false, min = false, igualMon = false; UINT cw = 0, ch = 0;
        std::string cima;
        if (m_janela && IsWindow(m_janela)) {
            vis = IsWindowVisible(m_janela) != 0; min = IsIconic(m_janela) != 0;
            GetWindowRect(m_janela, &rw); GetClientRect(m_janela, &rc);
            cw = (UINT)(rc.right - rc.left); ch = (UINT)(rc.bottom - rc.top);
            ex = GetWindowLongPtrW(m_janela, GWL_EXSTYLE);
            if (ex & WS_EX_LAYERED) { COLORREF k = 0; temAlfa = GetLayeredWindowAttributes(m_janela, &k, &alfa, &lwaFl) != 0; }
            MONITORINFO mi{}; mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(MonitorFromWindow(m_janela, MONITOR_DEFAULTTONEAREST), &mi))
                igualMon = !memcmp(&rw, &mi.rcMonitor, sizeof(RECT));
            cima = PorCimaVisiveis();
        }
        auto soma = [&](const char* s) { if (!motivo.empty()) motivo += "; "; motivo += s; };
        if (m_emIflip) {
            motivo = "o DXGI diz OVERLAY (plano de hardware: o DWM nao compoe a pelicula)";
        } else {
            if (!cls)                                   soma(m_semMedia ? "sem IDXGISwapChainMedia" : "o DXGI nao classificou nenhum quadro");
            else                                        soma("o DXGI diz COMPOSTO");
            if (!vis || min)                            soma("janela oculta/minimizada");
            if (vis && !igualMon)                       soma("janela != monitor");
            if (cw != m_bbW || ch != m_bbH)             soma("cliente != backbuffer (sem MPO/panel fitter no 25G64: tem de ser exato)");
            if (!(ex & WS_EX_NOREDIRECTIONBITMAP))      soma("sem WS_EX_NOREDIRECTIONBITMAP");
            if ((ex & WS_EX_LAYERED) && (!temAlfa || !(lwaFl & LWA_ALPHA) || alfa != 255 || (lwaFl & LWA_COLORKEY)))
                                                        soma("LAYERED sem alpha 255 opaco");
            if (!cima.empty())                          { std::string s = "janela visivel por cima: " + cima; soma(s.c_str()); }
            // RODADA6, controle negativo (monitor_por_janela=0, estilo B): a captura de MONITOR do proprio app,
            // com a pelicula EXCLUIDA da captura (WDA), NAO tirou a pelicula do iFlip (25 de 27 s em OVERLAY,
            // captura 60/s sem realimentacao). Uma captura de monitor que INCLUI a pelicula (regua externa com
            // DLSS5_CAPTURAVEL=1) tira. Entao aqui ela e so contexto, nao causa.
            // RODADA6, medido com a sonda-iflip + sonda-wgcjanela: com QUALQUER captura WGC ativa (inclusive a
            // por janela) e o ponteiro do mouse VISIVEL neste monitor, o DWM compoe (desenha o ponteiro) e a
            // janela de cima sai do flip independente. Ponteiro escondido pelo jogo, ou noutro monitor = iFlip.
            {
                CURSORINFO ci{}; ci.cbSize = sizeof(ci);
                // RODADA8: com o cursor proprio (main.cpp) os cursores de sistema estao trocados pelo vazio - o flag SHOWING
                // continua ligado, mas nao ha ponteiro desenhado; so um cursor PROPRIO do jogo (fora da tabela OCR_*) aparece.
                if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) && PtInRect(&rw, ci.ptScreenPos))
                    soma(g_cursorTrocado ? "ponteiro sobre a pelicula com os cursores de sistema TROCADOS pelo vazio (so um cursor proprio do jogo apareceria)"
                                         : "ponteiro do mouse VISIVEL sobre a pelicula com a captura ativa");
            }
            if (m_hdr)                                  soma("HDR do Windows ligado e pelicula SDR 8 bits");
            if (cls && motivo.find(';') == std::string::npos) soma("causa nao identificada");
            if (g_capturaMonitor)                       soma("(contexto) captura de MONITOR ativa - com a pelicula excluida ela nao derruba o iFlip");
        }
        _snprintf(b, sizeof(b),
            "pelicula: iflip=%s motivo=%s | overlay %u composto %u outro %u de %u presents, %u trocas | %s | estilo %c:%s%s%s%s%s alpha=%s | janela %ldx%ld%s, cliente %ux%u %s backbuffer %ux%u | por cima: %s",
            m_emIflip ? "sim" : "nao", motivo.c_str(), m_ov, m_co, m_ou, m_n, m_trocas, LatTxt().c_str(),
            (char)g_estiloPelicula,
            (ex & WS_EX_LAYERED) ? " LAYERED" : " sem-LAYERED",
            (ex & WS_EX_NOREDIRECTIONBITMAP) ? " NOREDIR" : " (sem NOREDIR)",
            (ex & WS_EX_TRANSPARENT) ? " TRANSPARENT" : " (sem TRANSPARENT: o clique nao atravessa)",
            (ex & WS_EX_TOPMOST) ? " TOPMOST" : " (sem TOPMOST)",
            GetPropW(m_janela, L"TreatAsDesktopFullscreen") ? " TreatAsDesktopFullscreen" : "",
            (ex & WS_EX_LAYERED) ? (temAlfa ? std::to_string((int)alfa).c_str() : "?") : "n/a",
            (long)(rw.right - rw.left), (long)(rw.bottom - rw.top), igualMon ? " = monitor" : " != monitor",
            cw, ch, (cw == m_bbW && ch == m_bbH) ? "=" : "!=", m_bbW, m_bbH,
            cima.empty() ? "ninguem" : cima.c_str());
        b[sizeof(b) - 1] = 0;
        m_n = m_ov = m_co = m_ou = 0; m_trocas = 0; m_nLat = 0; m_ini = GetTickCount64();
        return std::string(b);
    }
    void Soltar() { if (m_media) { m_media->Release(); m_media = nullptr; } m_semMedia = false; }
    ~Iflip() { Soltar(); }

private:
    // Janelas VISIVEIS (e nao cloaked) de cima da nossa na ordem Z que cruzam o retangulo dela.
    // As nossas saem marcadas como "(nossa)"; as de terceiro com o pid.
    std::string PorCimaVisiveis() const {
        RECT nosso{}; GetWindowRect(m_janela, &nosso);
        const DWORD eu = GetCurrentProcessId();
        std::string s; int n = 0;
        for (HWND w = GetWindow(m_janela, GW_HWNDPREV); w && n < 6; w = GetWindow(w, GW_HWNDPREV)) {
            if (!IsWindowVisible(w) || IsIconic(w)) continue;
            BOOL cloaked = FALSE; DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            if (cloaked) continue;
            RECT r{}, ix{};
            if (!GetWindowRect(w, &r) || !IntersectRect(&ix, &r, &nosso)) continue;
            DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
            wchar_t cls[64]{}; GetClassNameW(w, cls, 64);
            char c8[160];
            // {NOREDIR}: janela sem superficie de redirecao - se nao tiver swapchain/visual, nao desenha
            // nada e NAO derruba o iFlip (medido: o ShellHandwritingCanvas do TabTip, tela inteira).
            _snprintf(c8, sizeof(c8), "%ls%s%lu [%ldx%ld]%s", cls, pid == eu ? "(nossa) pid " : "(de terceiro) pid ",
                      (unsigned long)pid, (long)(r.right - r.left), (long)(r.bottom - r.top),
                      (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) ? "{NOREDIR}" : "");
            c8[sizeof(c8) - 1] = 0;
            if (!s.empty()) s += ", ";
            s += c8; n++;
        }
        return s;
    }
    HWND m_janela = nullptr;
    UINT m_bbW = 0, m_bbH = 0;
    IDXGISwapChainMedia* m_media = nullptr;
    bool m_semMedia = false, m_emIflip = false, m_hdr = false;
    unsigned m_n = 0, m_ov = 0, m_co = 0, m_ou = 0, m_trocas = 0;
    int m_ultimo = -1;
    ULONGLONG m_ini = 0;
    static const unsigned kR = 64, kL = 512;
    UINT      m_rId[kR] = {0};
    long long m_rQpc[kR] = {0};
    UINT      m_ultExib = 0;
    double    m_lat[kL] = {0};
    unsigned  m_nLat = 0;
    double    m_ultP50 = 0.0, m_ultP95 = 0.0; unsigned m_ultN = 0;   // RODADA7
    std::string LatTxt() {
        if (!m_nLat) { m_ultN = 0; m_ultP50 = m_ultP95 = 0.0; return std::string("present->tela n/d"); }
        std::vector<double> v(m_lat, m_lat + m_nLat); std::sort(v.begin(), v.end());
        m_ultP50 = v[v.size() / 2]; m_ultP95 = v[(v.size() * 95) / 100 < v.size() ? (v.size() * 95) / 100 : v.size() - 1]; m_ultN = m_nLat;   // RODADA7: a cadencia le
        char b[96]; _snprintf(b, sizeof(b), "present->tela p50 %.2f / p95 %.2f ms (n=%u)", m_ultP50, m_ultP95, m_nLat);
        b[sizeof(b) - 1] = 0; return std::string(b);
    }
};

} // namespace FlipProva
