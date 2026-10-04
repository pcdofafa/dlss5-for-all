/* =====================================================================
   apresentador_gpu.h -- mostra na tela, PELA PLACA DONA DO MONITOR, o que
   a ponte_gpu.h trouxe da placa que processa.

   Com duas placas, o swapchain do DLSS 5 (o da GeForce, onde o ReShade e
   a rede rodam) passa a viver numa janela oculta; a janela que a pessoa
   ve ganha ESTE swapchain, criado num device D3D11 da placa do monitor.
   Assim o DWM compoe uma janela nativa da placa dele - nada de janela
   estrangeira, nada de copia por quadro dentro do compositor.

   Thread propria: espera "tem quadro novo", pega o mais novo que a ponte
   entregou, copia para o backbuffer e apresenta. Latencia 1 pelo objeto
   de espera do swapchain, como o Renderer principal. Quem produz (o laco
   principal) e avisado por evento a cada quadro consumido, e e nisso que
   ele espera no lugar do objeto de espera do swapchain oculto.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <dwmapi.h>       // K-RITMO C3: qpcVBlank/qpcRefreshPeriod - o relogio da tela
#include <thread>
#include <atomic>
#include <string>
#include <cstdio>
#include <vector>
#include "ponte_gpu.h"
#include "fg_ffx.h"       // fluidez INTERPOLADA (experimental): G entre A e B, apresentado com pacing
#include "flip_prova.h"   // N6-FLIP: OVERLAY (caminho direto) x COMPOSED, dito pelo proprio DXGI
#include "cursor_pelicula.h"   // RESET-CURSOR: o ponteiro desenhado na APRESENTACAO (cursor_desenho=apresentacao)
#include <algorithm>

// sec. 27 (v3): VIGIA DA PELICULA. Instante (GetTickCount64) em que uma chamada que pode ficar presa na GPU comecou
// (0 = fora dela): o Present do apresentador e o DrawAndPresent do laco. O vigia (main.cpp, VigiaPelicula) ve uma das
// duas presa > 1 s e deixa a pelicula TRANSPARENTE (e devolve o ponteiro do sistema) ate ela sair: a tela nunca fica
// congelada atras de uma pelicula parada.
inline std::atomic<unsigned long long> g_vigiaApresDesde{ 0 }, g_vigiaLacoDesde{ 0 };
inline std::atomic<bool> g_ponteiroEntre{ true };   // sec. 27: [ultima] ponteiro_entre_quadros (a seta entre dois quadros reais)
struct VigiaMarca {
    std::atomic<unsigned long long>& a;
    explicit VigiaMarca(std::atomic<unsigned long long>& x) : a(x) { a.store(GetTickCount64(), std::memory_order_relaxed); }
    ~VigiaMarca() { a.store(0, std::memory_order_relaxed); }
};

namespace PonteGpu {

/* Cria um device D3D11 na placa dada. `cru`=true tenta o D3D11CoreCreateDevice exportado
   pelo d3d11.dll - o ponto de entrada que o D3D11CreateDevice usa por dentro. O ReShade so
   intercepta os exports publicos, entao um device criado por aqui nasce SEM o proxy dele,
   e o swapchain que vier depois nao ganha runtime do ReShade (o log dele diz "Skipping swap
   chain because it was created without a proxy Direct3D device" - provado em 12/09/2026
   com o ReShade 6.8). E o que queremos para a janela visivel: os add-ons so devem existir
   no swapchain da placa que processa. Se nao houver o export, cai no D3D11CreateDevice.

   A ASSINATURA NAO E A "CLASSICA" de seis parametros que circula por ai: no d3d11.dll do
   Windows 10/11 atual ela tem DEZ, e a de seis devolve E_INVALIDARG (o 1o parametro NAO e
   a IDXGIFactory - e um bloco interno, que pode ir nulo). Tirada do desmonte e conferida
   em teste (scratchpad/ponte/reshade). Por ser interna, so e usada em Windows 10 1703+
   (o mesmo piso das fences compartilhadas) e com `cru` ligado. */
static inline ID3D11Device* CriarDevice11(IDXGIAdapter1* placa, bool cru, bool* saiuCru, std::wstring* erro) {
    if (saiuCru) *saiuCru = false;
    D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0 };
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    ID3D11Device* dev = nullptr;
    if (cru) {
        typedef HRESULT (WINAPI *PFN_Core)(const void* argsInternos, IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT flags,
                                           const D3D_FEATURE_LEVEL*, UINT nFl, UINT sdk, ID3D11Device**, D3D_FEATURE_LEVEL*);
        HMODULE m = GetModuleHandleW(L"d3d11.dll");
        if (!m) m = LoadLibraryW(L"d3d11.dll");
        PFN_Core core = m ? (PFN_Core)(void*)GetProcAddress(m, "D3D11CoreCreateDevice") : nullptr;
        if (core && placa) {
            D3D_FEATURE_LEVEL got{};
            const HRESULT hr = core(nullptr, placa, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, lv, 1, D3D11_SDK_VERSION, &dev, &got);
            if (SUCCEEDED(hr) && dev) { if (saiuCru) *saiuCru = true; }
            else dev = nullptr;
        }
    }
    if (!dev) {
        ID3D11DeviceContext* c = nullptr; D3D_FEATURE_LEVEL got{};
        const HRESULT hr = D3D11CreateDevice(placa, placa ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                             flags, lv, 1, D3D11_SDK_VERSION, &dev, &got, &c);
        if (c) c->Release();
        if (FAILED(hr) || !dev) {
            if (erro) { wchar_t b[96]; _snwprintf(b, 96, L"D3D11CreateDevice (placa do monitor) falhou 0x%08lX", (unsigned long)hr); b[95] = 0; *erro = b; }
            return nullptr;
        }
    }
    return dev;
}

class Apresentador {
public:
    /* `janela` e a janela visivel (a de sempre). `placa` e a placa do monitor. w x h e o
       tamanho do backbuffer - o mesmo das texturas que a ponte entrega. */
    bool Iniciar(HWND janela, IDXGIAdapter1* placa, UINT w, UINT h, bool tentarCru,
                 void (*log)(const char*, ...), std::wstring* erro) {
        Parar();
        m_log = log; m_janela = janela; m_w = w; m_h = h;
        if (m_log) m_log("apresentador: criando device na placa do monitor (%s)", tentarCru ? "D3D11CoreCreateDevice" : "D3D11CreateDevice");
        m_dev = CriarDevice11(placa, tentarCru, &m_deviceCru, erro);
        if (!m_dev) return false;
        m_dev->GetImmediateContext(&m_ctx);
        if (m_log) m_log("apresentador: device %p (cru=%d), contexto %p; criando o swapchain %ux%u", (void*)m_dev, (int)m_deviceCru, (void*)m_ctx, w, h);

        IDXGIDevice* dxdev = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory2* f2 = nullptr;
        m_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev);
        if (dxdev) { dxdev->GetAdapter(&ad); dxdev->Release(); }
        if (ad) { ad->GetParent(__uuidof(IDXGIFactory2), (void**)&f2); ad->Release(); }
        if (!f2) { if (erro) *erro = L"apresentador: sem IDXGIFactory2"; Parar(); return false; }

        // Igual ao InitWaitable do Renderer: FLIP_DISCARD, 2 buffers, objeto de espera,
        // latencia 1. Scaling no padrao (STRETCH) pelos mesmos motivos escritos la.
        //
        // SEM DXGI_USAGE_RENDER_TARGET_OUTPUT, DE PROPOSITO. O ReShade cria um runtime (com
        // TODOS os add-ons: RenoDX, feeder) em todo swapchain que nasce no processo - e um
        // segundo runtime numa placa sem NGX derrubou o driver da Radeon (12/09/2026,
        // amdxx64.dll, divisao por zero). Sem esse uso o ReShade pula o swapchain ("Skipping
        // swap chain due to missing 'DXGI_USAGE_RENDER_TARGET_OUTPUT' flag"), e o DXGI aceita
        // normalmente: o backbuffer so precisa ser DESTINO DE COPIA, e e so isso que fazemos
        // nele (medido: 10 copias+presents ok em flip waitable, com e sem ReShade).
        DXGI_SWAP_CHAIN_DESC1 d1{};
        d1.Width = w; d1.Height = h; d1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d1.SampleDesc.Count = 1; d1.BufferUsage = DXGI_USAGE_SHADER_INPUT;
        // MAGPIE (AdaptivePresenter): 4 buffers em vez de 2. Com poucos buffers e latencia 1,
        // o Present bloqueia na contrapressao do compositor (~40 ms medidos = ~24 fps de teto).
        // Mais buffers + latencia maior deixam o apresentador enfileirar e nunca bloquear no
        // Present, subindo o que a pessoa ve. Receber() sempre pega o mais novo, entao isto
        // NAO acumula atraso de conteudo velho.
        d1.BufferCount = 4; d1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        d1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        d1.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        /* ===== O1 (20/09/2026): SINCRONIZACAO - o swapchain nasce PODENDO rasgar =====
           `DXGI_PRESENT_ALLOW_TEARING` so e aceito num swapchain criado com
           `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING`, e o flag nao pode ser posto depois. Como a
           escolha (VSync / Sem VSync / VRR) e do card e muda em voo, o flag entra SEMPRE que a
           maquina o suporta - ele nao muda nada por si: quem rasga e o `Present`, e so no modo
           "Sem VSync". Sem suporte (Windows antigo, driver sem DXGI 1.5) o flag fica de fora e
           o modo "Sem VSync" vira o mesmo que hoje, dito no log uma vez.
           O QUE ELE NAO FAZ *SE O DWM ESTIVER COMPONDO*: uma janela composta nunca rasga, tenha
           o flag ou nao, e ai o ganho de "Sem VSync" e so a LATENCIA (fila em 1).
           CORRECAO N6-FLIP (20/09/2026): a frase que estava aqui - "no modo Universal a pelicula
           e uma janela LAYERED sobre o jogo" - NAO e verdade desde a M6 (ela nasce sem LAYERED,
           com NOREDIRECTIONBITMAP, cobrindo o monitor). O `p` = 21-25 ms do FG-GERADO §F e um
           FATO; a explicacao dele era um chute. Quem decide agora e o `m_flip` logo abaixo, que
           pergunta ao DXGI se o quadro foi OVERLAY (plano de hardware) ou COMPOSED. */
        {
            IDXGIFactory5* f5 = nullptr;
            if (SUCCEEDED(f2->QueryInterface(__uuidof(IDXGIFactory5), (void**)&f5)) && f5) {
                BOOL ok = FALSE;
                if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &ok, sizeof(ok))) && ok) {
                    m_tearingOk = true;
                    d1.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
                }
                f5->Release();
            }
        }
        // A JANELA JA E LAYERED a esta altura (clique atravessando / intermitencia), e um
        // swapchain flip criado numa janela layered cai num caminho lento do compositor - e
        // o motivo de o Renderer principal so ligar o WS_EX_LAYERED DEPOIS do device (ver o
        // bloco do clique no main.cpp). Aqui a ordem e a inversa por natureza (o apresentador
        // nasce tarde, quando o add-on publica), entao o estilo sai por um instante e volta
        // igual, com o alpha 255 que estava.
        const LONG_PTR exAntes = GetWindowLongPtrW(janela, GWL_EXSTYLE);
        const bool eraLayered = (exAntes & WS_EX_LAYERED) != 0;
        if (eraLayered) SetWindowLongPtrW(janela, GWL_EXSTYLE, exAntes & ~(LONG_PTR)WS_EX_LAYERED);
        HRESULT hr = f2->CreateSwapChainForHwnd(m_dev, janela, &d1, nullptr, nullptr, &m_sc);
        if (FAILED(hr) || !m_sc) {
            d1.Flags = 0; m_tearingOk = false;   // O1: sem flags nao ha ALLOW_TEARING nem objeto de espera
            hr = f2->CreateSwapChainForHwnd(m_dev, janela, &d1, nullptr, nullptr, &m_sc);
        }
        if (eraLayered) {
            SetWindowLongPtrW(janela, GWL_EXSTYLE, exAntes);
            SetLayeredWindowAttributes(janela, 0, 255, LWA_ALPHA);
            if (m_log) m_log("apresentador: WS_EX_LAYERED tirado durante a criacao do swapchain e devolvido (alpha 255)");
        }
        f2->Release();
        if (FAILED(hr) || !m_sc) {
            if (erro) { wchar_t b[96]; _snwprintf(b, 96, L"apresentador: CreateSwapChainForHwnd falhou 0x%08lX", (unsigned long)hr); b[95] = 0; *erro = b; }
            Parar(); return false;
        }
        if (d1.Flags && SUCCEEDED(m_sc->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&m_sc2)) && m_sc2) {
            // RODADA6: com a receita do Magpie (pelicula_estilo=B) a fila e 2 (PESQ-MONITOR-VIRTUAL §2.3: em
            // flip independente da para descer a 1-2 quadros); a cadencia por carimbo ainda a desce a 1.
            m_sc2->SetMaximumFrameLatency(FlipProva::g_estiloPelicula == 'B' ? 2 : 3);   // MAGPIE: BufferCount-1, deixa o present enfileirar
            m_latWait = m_sc2->GetFrameLatencyWaitableObject();
        }
        if (!m_latWait) {
            IDXGIDevice1* dd1 = nullptr;
            if (SUCCEEDED(m_dev->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dd1)) && dd1) { dd1->SetMaximumFrameLatency(3); dd1->Release(); }
        }
        if (m_log) m_log("apresentador: swapchain %p (waitable=%d, rasgo=%d); compilando o desenho", (void*)m_sc, m_latWait ? 1 : 0, m_tearingOk ? 1 : 0);
        /* N6-FLIP: a prova de qual caminho o quadro toma ate a tela. O comentario do MedirP
           afirma "a nossa janela e LAYERED, e o GetFrameStatistics nao responde por ela" - e
           isso NAO vale na pelicula em tela cheia, que nao e layered desde a M6. Em vez de
           discutir a afirmacao, o DXGI passa a ser perguntado a cada janela de medida. */
        m_flip.Preparar(janela, w, h, m_tearingOk);
        m_iflip.Preparar(janela, w, h, m_sc);   // RODADA6
        if (!CriarDesenho(erro)) { Parar(); return false; }

        m_evtNovo     = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_evtConsumiu = CreateEventW(nullptr, FALSE, TRUE,  nullptr);   // nasce sinalizado: o primeiro quadro nao espera
        m_sair.store(false);
        m_apresentados.store(0);
        m_deviceMorreu.store(false);
        m_ok = true;
        Ponteiro::g_apresVivo.store(true);   // sec. 27: a captura nunca mais desenha a seta enquanto este apresentador existir
        return true;
    }

    void LigarPonte(Ponte* p) { m_ponte.store(p); }
    void Vsync(bool v) { m_vsync.store(v); }

    /* ---------------- TROCA COORDENADA DE NIVEL (guarda de runtime do Codec) ----------------
       O laco do apresentador le a ponte NESTA thread. Refazer a ponte por baixo dele (que e o
       que a guarda precisa fazer para cair de Codec para Alto em voo) seria device removed na
       certa - por isso ate 12/09/2026 a SAIDA so trocava de nivel ao reabrir o app.
       O handshake: quem quer trocar pede a pausa e ESPERA o laco confirmar que parou ENTRE dois
       quadros - nada em voo, nada segurado, nenhuma view presa a uma textura da ponte. So entao
       a ponte pode ser parada e remontada; Retomar() solta o laco de novo. */
    bool PausarEEsperar(DWORD limiteMs) {
        if (!m_ok || !m_th.joinable()) return false;
        m_pausar.store(true);
        const ULONGLONG t0 = GetTickCount64();
        while (!m_pausado.load()) {
            if (m_sair.load()) { m_pausar.store(false); return false; }
            if (m_evtNovo) SetEvent(m_evtNovo);
            if (GetTickCount64() - t0 > limiteMs) { m_pausar.store(false); return false; }
            Sleep(1);
        }
        return true;
    }
    void Retomar() { m_pausar.store(false); if (m_evtNovo) SetEvent(m_evtNovo); }
    bool Pausado() const { return m_pausado.load(); }
    bool TemInterpolador() const { return m_fg.load() != nullptr; }
    /* RESET-DRENO (23/09/2026): ESPERA A PLACA TERMINAR TUDO O QUE ESTE DEVICE JA MANDOU (copias da ponte,
       desenho, Present). So pode ser chamado com o laco PAUSADO (PausarEEsperar): o contexto imediato nao e
       de duas threads ao mesmo tempo, e pausado ninguem mais encosta nele. Devolve os ms que levou; -1 = o
       prazo estourou (a placa nao devolveu em `tetoMs`), e quem chama loga e segue - nunca espera para sempre. */
    double DrenarGpu(DWORD tetoMs) {
        if (!m_ok || !m_dev || !m_ctx || !m_pausado.load()) return 0.0;
        ID3D11Query* q = nullptr;
        D3D11_QUERY_DESC qd{}; qd.Query = D3D11_QUERY_EVENT;
        if (FAILED(m_dev->CreateQuery(&qd, &q)) || !q) return 0.0;
        const ULONGLONG t0 = GetTickCount64();
        LARGE_INTEGER a, b, f; QueryPerformanceCounter(&a); QueryPerformanceFrequency(&f);
        m_ctx->End(q); m_ctx->Flush();
        bool ok = false;
        while (GetTickCount64() - t0 <= tetoMs) {
            const HRESULT hr = m_ctx->GetData(q, nullptr, 0, 0);
            if (hr == S_OK) { ok = true; break; }
            if (FAILED(hr)) break;   // device removido: nao ha o que esperar
            Sleep(1);
        }
        q->Release();
        QueryPerformanceCounter(&b);
        return ok ? 1000.0 * (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart : -1.0;
    }

    /* ---------------- SEGURAR UM QUADRO DE FOLGA (so no nivel Codec) ----------------
       O decodificador da placa do monitor entrega em 9-13 ms de media, mas com p95 de 10-19 ms
       (medido 12/09/2026, scratchpad\codec\real\tabela_real.txt). Sem folga, cada pico desses e
       um quadro que nao chega a tempo - judder. Com a folga o laco apresenta SEMPRE o quadro
       anterior e guarda o novo, entao o pico e absorvido pelo que ja estava na mao. Custa
       exatamente um quadro de latencia, por isso so vale no Codec (que ja cobra ~15 ms); em
       Max e Alto o laco continua byte a byte o que sempre foi. */
    void SegurarFolga(bool s) { m_segurar.store(s); }
    bool SegurandoFolga() const { return m_segurar.load(); }
    /* FLUIDEZ INTERPOLADA: com um interpolador ligado, cada quadro B que chega vira G (entre o
       anterior e B) mostrado AGORA e B mostrado meio intervalo depois (mediana filtrada das
       ultimas 16 chegadas). Sem interpolador o laco e o de sempre, byte a byte. */
    void LigarInterpolador(FrameGen::Interpolador* fg) { m_fg.store(fg); }
    /* FONTE DE TAXA FIXA (placa de captura / camera a N fps). O gate economico compara os
       apresentados/s com "o que a cadeia entregaria sem FG" = 1000/(intervalo - custo do FG).
       Numa fonte de taxa fixa a cadeia nunca entrega mais do que a fonte manda, entao essa
       referencia estoura (a 4K com o FI em ~8 ms: 1000/(16,7-8) = 115 "sem FG" contra 120
       apresentados = ganho 0,96 => PAUSA por "nao compensa", com o FG dobrando a entrega).
       Com a taxa declarada a referencia fica presa a ela. 0 = fonte livre (jogo), como sempre. */
    void TaxaFonteFixa(double fps) { m_fonteFps = fps > 0.0 ? fps : 0.0; }
    /* FILA DE UM QUADRO COM CADENCIA FIXA (X-FG-QUALIDADE, 19/09/2026; ESTUDO-LS §3 "Queue Target 1"
       + "Adaptive"). 0 = o de antes: o G sai ancorado em `ultimo present + meio intervalo` e o B meio
       intervalo depois - o jitter POSITIVO da chegada (a captura em rajada, a cadeia oculta) vai
       para a tela. 1 = cada par (G..., B) entra numa fila e sai num TIQUE fixo = intervalo mediano
       entre quadros distintos / K, continuando do ultimo prazo agendado e nao da chegada; com a
       fila cheia demais na chegada (2 reais pendentes) o tique encurta 1% por quadro, vazia (o
       ultimo prazo ja passou) alonga 1% - o `queue_draining_momentum = 0,01` do LS, nunca um
       descarte de uma vez. Custa ~um intervalo de chegada de latencia (o LS paga o mesmo no
       padrao). Ver LacoInterpolado. `[fluidez] fila=0/1` no ini, DLSS5_FG_FILA na bancada. */
    /* NS-2309 item 5(c) (conferencia vinda do NeuralScreen frame_generation.inl, que pula o quadro
       em DXGI_STATUS_MODE_CHANGED - ideia, codigo proprio). O nosso Present e de janela em flip model;
       MODE_CHANGED (0x087A0007) e codigo de SUCESSO e so aparece quando o modo da tela mudou por baixo
       (troca de resolucao/HDR com a pelicula aberta). O quadro JA foi entregue - nao ha o que pular
       nesta volta -, mas o proximo sairia numa superficie de tamanho/formato velho: conta, avisa no log
       (3 primeiros, depois 1 a cada 100) e deixa o caminho de sempre (FonteMudou / reabertura) agir.
       OCCLUDED (0x087A0001) e benigno e so conta. */
    void ConferirPresent(HRESULT hr, const char* onde) {
        if (hr == DXGI_STATUS_MODE_CHANGED) {
            ++m_presModoMudou;
            if (m_log && (m_presModoMudou <= 3 || (m_presModoMudou % 100) == 0))
                m_log("apresentador: CONFERENCIA Present(%s) devolveu DXGI_STATUS_MODE_CHANGED (%u nesta sessao) - "
                      "o modo da tela mudou por baixo da pelicula", onde, m_presModoMudou);
        } else if (hr == DXGI_STATUS_OCCLUDED) {
            ++m_presOcluido;
        }
    }
    unsigned m_presModoMudou = 0, m_presOcluido = 0;
    void FilaQuadros(int n) { const int v = n <= 0 ? 0 : 1; if (v != m_fila) { m_fila = v; if (m_log) m_log("apresentador: fila de quadros no modo interpolado = %d (%s)", v, v ? "um quadro de folga, cadencia fixa no tique" : "sem folga: G ancorado na chegada"); } }
    int  FilaQuadros() const { return m_fila; }
    /* A BASE que o app pediu ao jogo (fps; 0 = nenhuma). Quando a chegada medida esta a menos de 8%
       dela, o tique da fila se prende a 1000/base/K exato (cadencia comensuravel com a tela). */
    void BaseFps(double fps) { m_baseFps = fps > 0.0 ? fps : 0.0; }
    double BaseFps() const { return m_baseFps; }
    double TiqueMs() const { return m_fTique; }
    /* N1 - `D`, a folga da fila desta maquina (T/2 + c + p + j, com piso e teto; ver AS MEDIDAS
       DA MAQUINA mais abaixo). Publica porque quem limita a FONTE - a peca dentro do jogo -
       precisa dela para escolher a fase: o quadro real tem de CHEGAR aqui `D` antes do prazo em
       que ele sai. Ver main.cpp, g_conversa.Fase. */
    double DMs() const { return m_dMs; }
    /* O3: o mesmo `j` do log de ritmo (p95 do erro de chegada), aqui em cima porque o JMs()
       da area privada e do uso interno. Quem le e a CAPTURA: tremor maior que um periodo da
       tela e o sinal de que o WGC esta pulando composicao - ver screen_capture.h. */
    double TremorChegadaMs() const { return m_jMs; }
    /* K-RITMO C2 (K3): o refresh da tela onde a JANELA VISIVEL esta, medido pelo proprio
       apresentador. 0 = ainda nao leu (a base entao fica com o RefreshDoMonitor() do alvo). */
    double TelaHz() const { return m_telaHz; }
    /* X-FG-6 (3x/4x): quantos quadros por chegada (2, 3 ou 4). O que VALE pode ser menor: a tela
       (taxa do monitor) e a placa (Gate) derrubam para 2x; a queda avisa o main uma vez
       (MultCaiuUmaVez) e dura a sessao - escolher de novo no card e um pedido novo. */
    void Multiplicador(int m) {
        m = m < 2 ? 2 : m > 4 ? 4 : m;
        if (m == m_mult) return;
        m_mult = m; m_multEf = m; m_multQueda = false; m_multCustoDesde = 0.0; m_multLogTela = false;
        if (FrameGen::Interpolador* fg = m_fg.load()) fg->Multiplicador(m);
        if (m_log) m_log("apresentador: multiplicador pedido %dx", m);
    }
    int  MultiplicadorEfetivo() const { return m_multEf; }
    bool MultCaiuUmaVez() { return m_multCaiu.exchange(false); }
    void RefreshHz(double hz) { m_refreshHz = hz > 0.0 ? hz : 0.0; }
    /* O custo do FG MEDIDO na prova ao vivo (a diferenca de intervalo na pausa); 0 = ainda nao
       houve pausa nesta sessao e quem quiser o numero se vira com o tempo de GPU do FI. */
    double CustoFgMedidoMs() const { return m_custoFgMs; }
    /* O tamanho da saida, para quem precisa decidir por resolucao (o Desempenho automatico). */
    UINT Largura() const { return m_w; }
    UINT Altura()  const { return m_h; }

    /* ======================= O1-TELA (20/09/2026): O MODO "TELA" =======================
       O PROBLEMA, com os numeros do monitor do dono (COD, 100 Hz fixo, sem VRR). Com N inteiro
       a cadencia so fecha quando `refresh / fps` e inteiro: a 40 fps, 2x da 80 presents/s numa
       tela que mostra 100 - os presents caem em 1, 1 e 2 vblanks e a tela ve 10/10/20 ms. A
       51 fps, 2x pede 102/s numa tela de 100: um a mais por segundo, que vira uma imagem
       comida. Nenhum N inteiro resolve, porque o problema nao e o N - e exigir que ele seja
       inteiro.

       O QUE ESTE MODO FAZ. A cadencia passa a ser a TELA: **um present por vblank**, sempre, e
       o que varia e QUANTOS quadros saem de cada intervalo da fonte. A razao
       `R = periodo_da_fonte / periodo_da_tela` (vblanks por intervalo: 2,5 a 40 fps em 100 Hz;
       1,96 a 51; 3,33 a 30) nao precisa ser inteira - um acumulador reparte 2, 3, 2, 3... e a
       MEDIA e R exata. Entao os apresentados/s sao o refresh, qualquer que seja o fps do jogo.

       A FRACAO DE CADA QUADRO, E O LIMITE HONESTO. Cada vblank dentro do intervalo queria o
       quadro na fracao `k/R` entre os dois reais. O provedor FFX **nao aceita fracao
       arbitraria**: ele so escreve no ponto medio (X-FG-VIDEO §3 mediu que `numGeneratedFrames`
       e ignorado, e nao ha campo de fase no `ffxDispatchDescFrameGeneration`). O que existe sao
       as fracoes DIADICAS das chamadas encadeadas - {1/4, 1/2, 3/4} com os dois contextos de
       hoje. Entao cada vblank recebe o gerado de fracao mais PROXIMA da que queria, e o erro de
       fracao fica em no maximo 1/8 do intervalo (3,1 ms a 40 fps). O relogio fica exato; o
       instante retratado fica a 1/8. E o certo a trocar: o judder de 10/10/20 ms e visivel, o
       erro de 1/8 na posicao de um objeto nao e.
       TETO: com 3 gerados o modo cobre R ate 4. Acima disso (240 Hz com fonte a 30 = R 8) ele
       entrega 4 por intervalo e nao o refresh inteiro - o log diz `razao` e `apresentados`, e a
       conta fecha sozinha. Passar disso pede um terceiro contexto do FI (fracoes de 1/8).
       R < 1,5 (jogo mais rapido que a tela): nao ha o que gerar. O quadro real sai no vblank e
       a ADMISSAO descarta o excesso - o mesmo caminho que ja existe. */
    void ModoTela(bool lig) { Meta(lig ? -1.0 : 0.0); }
    /* ===== META (21/09/2026): a fluidez por META DE FPS, no lugar de 2x/3x/4x =====
       Decisao do dono (teste no COD, 21/09): o modo Tela com o fps do jogo travado foi o que
       funcionou; 2x/3x/4x "limitou demais o fps base e teve resultado estranho". Entao a pessoa
       diz o fps que quer ver (60, 100, 120, 144, 165, 240 ou "Tela" = o refresh medido) e o modo
       Tela vira o caso geral: `per` = 1000/meta no lugar do periodo do vblank, `R = meta / fonte`
       (EMA + histerese como antes), `R - 1` gerados por intervalo repartidos pelo acumulador.
       TETO: 7 gerados por intervalo (8x, 3o contexto do FI: fracoes de 1/8) - e o CUSTO, nao um
       numero fixo: o par de k custa `passes + cg x (k - 1)` e tem de caber em 0,8 x intervalo da
       fonte (MetaCusto). O que nao cabe e gerado a menos, o log e a dica dizem quanto
       ("entregando"), e o jogo NUNCA e limitado. `fps` > 0 = a meta; < 0 = Tela; 0 = desligado. */
    void Meta(double fps) {
        const double v = fps < 0.0 ? -1.0 : fps;
        if (v == m_metaFps) return;
        m_metaFps = v;
        const bool lig = v != 0.0;
        if (lig != m_modoTela) { m_modoTela = lig; m_telaAcc = 0.0; m_telaErrF = 0.0; m_telaErrN = 0; }
        m_metaCapK = 8; m_metaCapLog = 0;   // meta nova: o teto pelo custo e re-medido
        m_metaDecidiu = false; m_metaGerDesde = 0.0;   // e a decisao gerar/nao gerar e refeita na hora
        m_metaInicioMs = 0.0; m_metaMultMs = 0.0; m_metaCapMs = 0.0;   // e o arranque de 3 s recomeca
        if (m_log) {
            if (v < 0.0)       m_log("apresentador: meta = TELA (o refresh medido da tela da janela) - um quadro por vblank, R = refresh / fonte");
            else if (v > 0.0)  m_log("apresentador: meta = %.0f fps - R = %.0f / fonte, %s gerados por intervalo repartidos no acumulador", v, v, "R - 1");
            else               m_log("apresentador: meta desligada - N quadros por intervalo da fonte");
        }
    }
    bool   ModoTela() const { return m_modoTela; }
    double TelaRazao() const { return m_telaR; }
    /* o alvo em fps (Tela = o refresh medido), o que a placa de fato entrega (R efetivo x fonte;
       igual ao alvo quando cabe) e o teto de N que o custo permite (2, 4 ou 8) */
    double MetaFps() const { return m_metaFps < 0.0 ? m_telaHz : m_metaFps; }
    double MetaEntregueFps() const { return m_metaEntregue; }
    int    MetaCapK() const { return m_metaCapK; }
    /* META (dono, 21/09): com a meta abaixo de 1,25 x a fonte nada e gerado ("liga 60 com o jogo a 60
       e nao acontece nada") - e isso tem de ficar visivel: o card apaga os degraus, o contador
       mostra 60 -> 60. `MetaGerando()` e o estado em vigor (com histerese de 2 s: se o jogo cair
       de 60 para 40 depois, os gerados entram sozinhos; se voltar a 60, saem). */
    bool   MetaGerando() const { return m_metaGerando; }
    static constexpr double kMetaRMin = 1.25;   // abaixo disto (meta / fonte) nao ha gerado
    /* Quem mexeu na rede (preset/qualidade do DLSS do jogo, perfil) recomeca a medida da fonte:
       a EMA nao pode ficar presa ao fps de antes do rebuild. (A propria EMA tambem salta sozinha
       quando a chegada muda mais de 20% - ver a chegada.) */
    void MetaRemedir() { m_telaFonteMs = 0.0; m_metaGerDesde = 0.0; }

    enum Sync { SYNC_VSYNC = 0, SYNC_SEM = 1, SYNC_VRR = 2 };
    /* SINCRONIZACAO. Tres modos, e o que cada um de fato faz nesta arquitetura:
       - VSYNC (padrao, o de hoje): o prazo de cada present e puxado ao vblank mais proximo
         (`AoVblank`), menos `q`. E a quantizacao que faz as 16,67 ms caberem em 4 vblanks.
       - SEM VSYNC: o present pede `ALLOW_TEARING` e a fila do swapchain cai para 1 (latencia
         minima). A quantizacao ao vblank FICA: sem ela, numa tela composta, os presents caem em
         fase solta e dois deles viram UMA imagem (o `imagens distintas` do FG-GERADO §D).
       - VRR: a tela nao tem vblank fixo para ancorar, entao a quantizacao SAI e o quadro e
         apresentado no instante calculado - que e exatamente o que o painel VRR espera receber.
         So vale quando a tela de fato esta em VRR: enquanto `TelaVrr()` for falso, VRR se
         comporta como VSYNC (o gancho esta em `TelaVrr(bool)`, posto pelo main). */
    void Sincronizacao(int s) {
        const int v = s < 0 ? 0 : s > 2 ? 2 : s;
        if (v == m_sync.load()) return;
        m_sync.store(v);
        m_latAplicada = -1;   // o modo manda na fila do swapchain: reaplica no proximo present
        if (m_log) m_log("apresentador: sincronizacao = %s%s", v == SYNC_SEM ? "sem VSync" : v == SYNC_VRR ? "VRR" : "VSync",
                         v == SYNC_SEM && !m_tearingOk ? " (a maquina nao suporta rasgo: so a latencia muda)" : "");
    }
    int  Sincronizacao() const { return m_sync.load(); }
    /* O GANCHO DO VRR. Quem sabe se a tela esta em taxa variavel e a captura (o o3-captura
       expoe isso); aqui so se guarda a resposta. Ausente, fica falso - e VRR trata-se como
       VSync, que e o comportamento de sempre. */
    /* RODADA3 (23/09/2026, OCLUSAO-2309 sec. 10): TETO DE APRESENTACAO imposto de fora (main.cpp, so
       enquanto a realimentacao pelo relogio do DWM estiver DETECTADA). Com a fonte monitor sob a nossa
       janela visivel, cada Present daqui e uma composicao e cada composicao e uma captura: segurar so a
       pelicula (a rede) nao soltava o laco, porque o apresentador seguia no ritmo dele. Com o teto, o
       intervalo minimo entre presents e 1000/teto ms: o GERADO que chega cedo demais e descartado (nao
       e conteudo do jogo), o REAL espera o resto do intervalo (no maximo esse intervalo) e vai a tela -
       nenhum quadro do jogo e perdido enquanto o teto ficar acima do fps dele. 0 = sem teto (o
       comportamento de sempre; fora da realimentacao nada muda). */
    void TetoApresentacao(double fps) {
        const double v = fps > 0.0 ? fps : 0.0;
        if (v == m_tetoApres.load(std::memory_order_relaxed)) return;
        m_tetoApres.store(v, std::memory_order_relaxed);
        if (m_log) {
            if (v > 0.0) m_log("apresentador: teto de apresentacao = %.0f fps (realimentacao) - gerado adiantado descartado, real espera o intervalo", v);
            else         m_log("apresentador: teto de apresentacao retirado (%llu gerados descartados, %llu reais adiados enquanto durou)",
                               (unsigned long long)m_tetoDescartados.load(), (unsigned long long)m_tetoAdiados.load());
        }
        if (v <= 0.0) { m_tetoDescartados.store(0); m_tetoAdiados.store(0); }
    }
    /* ======================= RODADA5 (23/09/2026, OCLUSAO-2309 sec. 12): CADENCIA POR CARIMBO =======================
       O PROBLEMA (sec. 11.5). Sem interpolador, este laco fazia Receber -> Mostrar -> Present(0,0) NA HORA em que o
       quadro chegava. O vblank em que ele aparecia era o primeiro depois de `carimbo + captura + rede + ponte +
       acordar`, e esse caminho varia alguns ms de um quadro para o outro: com o grao de 4,17 ms (240 Hz), ~1/3 dos
       quadros atravessava a fronteira do vblank - 60 fps viravam 3/4/5 refreshes ao acaso, e nao os da fonte.
       O QUE ESTE MODO FAZ. Cada quadro da ponte traz o CARIMBO CRU da captura (g_wgcCarimboCruMs, sem a grade do
       V2; o main poe com CarimboQuadro(n, ...) logo depois do Enviar). O alvo do quadro e o vblank MAIS PROXIMO de
       `carimbo + D`, e o Present(1,0) e solto MEIO REFRESH antes dele (despertador de alta resolucao). Assim a
       distancia entre dois quadros na tela e a distancia entre os carimbos, arredondada ao refresh - a tela
       reproduz a fonte, com os 3/4/5 dela e nao com os nossos.
       D (automatico) = o menor multiplo inteiro do refresh que cobre o p90 do caminho captura->pronto (+ o meio
       refresh da soltura), medido nos ultimos 2 s; sobe na hora, desce um refresh por vez depois de 5 s sobrando;
       piso 1 refresh, TETO `CadenciaTeto` refreshes (padrao 2). Quem fica pronto depois da hora de soltar sai NA
       HORA (conta em `atrasados`) - nada e pulado, nada e segurado mais que 1 refresh alem do alvo.
       Por que Present(1,0) e nao SyncInterval=N (RITMO-ABERTO / RitmoDxgi): N segura TODO quadro N refreshes, o
       que so reproduz uma fonte perfeitamente regular - a do jogo falso ja e 71% 4 / 20% 3 / 7% 5 refreshes - e
       enfileira latencia quando a fonte atrasa. Com a soltura no prazo, o `1` so garante que o quadro fica ao menos
       um vblank na tela (nunca e substituido antes de o DWM compor).
       Desligado (`[ultima] cadencia_carimbo=0` / DLSS5_CADENCIA=0), o laco e byte a byte o de antes. */
    void Cadencia(bool ligada, int tetoRefreshes) {
        const int teto = tetoRefreshes < 1 ? 1 : tetoRefreshes > 8 ? 8 : tetoRefreshes;
        const bool mudou = (ligada != m_cadOn.load()) || teto != m_cadTeto.load();
        m_cadOn.store(ligada); m_cadTeto.store(teto);
        if (mudou && m_log) m_log("apresentador: cadencia por carimbo %s (sem FG: alvo = vblank mais proximo de carimbo + D, Present(1,0) solto meio refresh antes; D = multiplo do refresh que cobre o p90 captura->pronto, teto %d refreshes)",
                                  ligada ? "LIGADA" : "desligada", teto);
    }
    bool Cadencia() const { return m_cadOn.load(); }
    /* RODADA7 (PLANO-FLUIDEZ item 3): modo da cadencia. 0 = desligada (apresenta no PRIMEIRO vblank depois de
       pronto: Present(1,0), em ordem), 1 = forcada (segura ate o alvo carimbo + K), 2 = AUTOMATICA (padrao):
       liga so quando refresh/fonte NAO e razao inteira (tolerancia 1%, medida a cada 1 s sobre os carimbos
       dos ultimos 4 s, histerese de 3 decisoes iguais). Em razao inteira a fonte ja cai sempre no mesmo
       numero de refreshes e segurar so custa latencia. Chave: `[ultima] cadencia_carimbo=0|1|auto`. */
    void CadenciaModo(int modo, int tetoRefreshes) {
        const int m = modo < 0 ? 2 : modo > 2 ? 2 : modo;
        m_cadModo.store(m);
        Cadencia(m != 0, tetoRefreshes);
        if (m_log) m_log("apresentador: cadencia modo %s (0=desligada/1=forcada/auto); sem FG os quadros sao lidos EM ORDEM e apresentados com Present(1,0) nos dois casos",
                         m == 0 ? "DESLIGADA" : m == 1 ? "FORCADA" : "AUTO");
    }
    int CadenciaModo() const { return m_cadModo.load(); }
    uint64_t SemLugarEsperas() const { return m_semLugarEsperas.load(std::memory_order_relaxed); }
    uint64_t SemLugarLongas() const { return m_semLugarLongas.load(std::memory_order_relaxed); }
    /* O carimbo CRU (ms, base QPC) do quadro `n` da ponte. Chamado pelo laco principal logo depois do Enviar. */
    void CarimboQuadro(uint64_t n, double carimboCruMs) {
        if (!n) return;
        const int i = (int)(n % kCarQ);
        m_carQn[i].store(0, std::memory_order_relaxed);
        m_carQv[i].store(carimboCruMs, std::memory_order_relaxed);
        m_carQn[i].store(n, std::memory_order_release);
    }
    /* Contadores acumulados da sessao (a linha `quadros:` do main tira o delta por segundo). */
    uint64_t TelaNovos()      const { return m_cadTelaNovos.load(std::memory_order_relaxed); }
    uint64_t TelaCopias()     const { return m_cadTelaCopias.load(std::memory_order_relaxed); }
    uint64_t CadAtrasados()   const { return m_cadAtrasados.load(std::memory_order_relaxed); }
    uint64_t CadPulados()     const { return m_cadPulados.load(std::memory_order_relaxed); }      // ponte reescreveu antes de ler (nao deveria)
    uint64_t CadSemLugar()    const { return m_cadSemLugar.load(std::memory_order_relaxed); }     // swapchain sem lugar por > 300 ms: descartado com AVISO
    uint64_t CadFilaAvisos()  const { return m_cadFilaAvisos.load(std::memory_order_relaxed); }   // chegou com 2+ quadros esperando atras
    double   CadDMs()         const { return m_cadDMsAt.load(std::memory_order_relaxed); }
    double   TetoApresentacaoFps() const { return m_tetoApres.load(std::memory_order_relaxed); }
    uint64_t TetoDescartados() const { return m_tetoDescartados.load(std::memory_order_relaxed); }
    uint64_t TetoAdiados() const { return m_tetoAdiados.load(std::memory_order_relaxed); }
    void TelaVrr(bool v) { if (v != m_telaVrr.load()) { m_telaVrr.store(v); if (m_log) m_log("apresentador: a tela %s em taxa variavel (VRR)", v ? "ESTA" : "nao esta"); } }
    bool TelaVrr() const { return m_telaVrr.load(); }
    /* LATENCIA MAXIMA: quantos quadros o swapchain deixa enfileirar (1 a 3). Cada um na fila e
       ~um intervalo de present a mais entre o Present e a tela. 0 = o padrao do modo. */
    void LatenciaMax(int n) { const int v = n < 0 ? 0 : n > 3 ? 3 : n; if (v != m_latQuer) { m_latQuer = v; m_latAplicada = -1; } }
    int  LatenciaMax() const { return m_latQuer; }
    uint64_t Gerados() const { return m_gerados; }
    uint64_t Cortes()  const { return m_cortes; }
    /* ======================= RODADA8 (PLANO-FLUIDEZ item 3, Agente C): FG COM SAIDA FIXA 2x =======================
       `[ultima] fg_saida=2x|meta` (DLSS5_FG_SAIDA), padrao 2x. O FSR3 FI so gera o PONTO MEDIO (FASE-ARBITRARIA-2309), entao
       a saida honesta e 2 x a fonte, travada ao vblank: cada quadro REAL vai no vblank do carimbo dele + K (a mesma grade
       absoluta da cadencia por carimbo) e o GERADO vai no vblank do MEIO entre o real anterior e o atual. Quando 2 x fonte
       divide o refresh (60 -> 120 em 240 Hz) cada quadro fica 2 refreshes; quando o intervalo em vblanks e impar, o meio
       alterna piso/teto de forma deterministica (nada de "moeda" pelo relogio). NENHUM real e descartado: atrasado sai no
       primeiro vblank possivel; sem lugar no swapchain, espera. O GERADO que nao tem vblank livre (intervalo de 1 vblank,
       ou ficou pronto tarde demais para caber antes do real) e descartado e contado A PARTE - gerado nao e quadro do jogo.
       Neste modo nao ha admissao, nem descarte por amostragem (o dedup exato fica antes da rede, main.cpp Quadros5), nem o
       teto de apresentacao, nem meta: a "meta" e derivada (2 x fonte, limitada ao refresh). `meta` volta ao caminho de antes
       (LacoInterpolado), byte a byte. */
    void FgSaida2x(bool v) {
        if (m_f2Pedido.exchange(v) != v && m_log)
            m_log("apresentador: fg_saida = %s", v ? "2x (FG travado ao vblank: real em carimbo+K, gerado no vblank do meio; nenhum real descartado)"
                                                    : "meta (o caminho interpolado de antes: meta/admissao/fila)");
    }
    bool     FgSaida2x() const { return m_f2Pedido.load(); }
    uint64_t F2GeradosTela() const { return m_f2GerTelaTot.load(std::memory_order_relaxed); }
    uint64_t F2GeradosDescartados() const { return m_f2GerDescTot.load(std::memory_order_relaxed); }
    uint64_t F2RealAtrasados() const { return m_f2RealAtrTot.load(std::memory_order_relaxed); }
    int      F2K() const { return m_f2K; }

    /* ======================= I21: O CONTADOR DE FPS NA JANELA VISIVEL (19/09/2026) =======================
       O contador "de sempre" e o do ReShade ([OVERLAY] ShowFPS), desenhado no swapchain da CADEIA. Com
       este apresentador de pe a cadeia vive numa janela OCULTA - o ReShade desenha o contador numa
       janela que ninguem ve (dono, 19/09, modo monitor + Interpolada: "medidor de fps nao ta aparecendo
       na tela pra eu ver"). Entao, sempre que o apresentador existe e a chave "FPS na tela" esta ligada,
       ELE desenha o proprio contador, com os numeros do card do FG (J1): "jogo 72 -> tela 141" = chegada
       real da cadeia (mediana de Acordar()) e presents/s desta thread; FG pausado, fonte parada ou sem
       interpolador, so "tela 60". Quando a cadeia E a janela visivel o apresentador nao existe e o do
       ReShade segue sozinho - nunca ha dois contadores.
       COMO: o texto vira uma textura pequena feita por GDI (Segoe UI semibold, contorno escuro - regra
       do I8: letra maior que a do painel, legivel de longe e na TV), refeita SO quando o texto muda
       (conferido 4x/s; os numeros mudam 1x/s). Por quadro: o backbuffer nao e render target (ver o
       BufferUsage, de proposito), entao o recorte sob o contador e copiado para uma textura-alvo
       pequena, o texto e desenhado por cima (blend premultiplicado) e o recorte volta - duas copias de
       ~400x50 px e um Draw de 3 vertices; nada que espere por nada. Canto e escala vem do main (o
       mesmo canto dos cartazes por padrao; [ultima] fps_canto muda). */
    enum FpsCantoTela { FPS_SUP_DIR = 0, FPS_INF_DIR = 1, FPS_SUP_ESQ = 2, FPS_INF_ESQ = 3 };
    void FpsNaTela(bool ligado) {
        if (m_fpsLigado.exchange(ligado) != ligado && m_log && m_ok)
            m_log("apresentador: contador de fps na janela visivel %s (a cadeia esta numa janela oculta: o do ReShade nao aparece)", ligado ? "LIGADO" : "desligado");
    }
    void FpsCanto(int canto, float escala) { m_fpsCanto.store(canto < 0 || canto > 3 ? 0 : canto); m_fpsEscala.store(escala); }

    /* Sobe a thread. Separado do Iniciar para a ponte ja existir quando ela acordar. */
    void Rodar() {
        if (!m_ok || m_th.joinable()) return;
        m_th = std::thread([this] { Laco(); });
    }

    /* O laco principal chama depois de cada Present do swapchain oculto: ha quadro novo.
       X-HUD-PISCA (19/09/2026): E AQUI que o intervalo de chegada e medido, NAO no Receber().
       Ate hoje a mediana era feita dos instantes em que ESTE laco pegava o quadro - e no modo
       interpolado ele so pega o proximo depois de esperar o G (ancorado em `ultimo present +
       meio intervalo`) e o B (meio intervalo depois): uma volta dura exatamente UM intervalo
       da mediana, e a mediana era feita dessas voltas. Circulo fechado: qualquer soluco que
       alongasse a mediana ficava travado nela e cada volta somava o proprio custo - no COD do
       dono (19/09, 12:10) a cadeia entregava 90-180 fps (linhas `LAT`) e a "chegada" do
       apresentador desceu SOZINHA de 33,6 para 12,2 fps em dois minutos, sem nunca voltar, com
       25 presents/s numa tela de 100 Hz e "custo do FG 81 ms MEDIDO" (era a propria volta). O
       instante certo e o da cadeia: cada quadro que ela le (mesmo o que a ponte descarta por
       anel cheio) passa por aqui, entao esta mediana e a taxa REAL da fonte, e a espera do
       apresentador nao entra nela. E o que o FSR e o LSFG fazem (PESQ-FG §3.1: a media movel
       e dos Presents do jogo, nao da thread de ritmo). */
    /* X-GERADO (20/09/2026): O CARIMBO DO PROPRIO QUADRO, quando a fonte e a captura.
       Ate hoje o instante da chegada era o RELOGIO DE RECEPCAO (o Ms() desta chamada). No modo
       Universal isso mede o WGC, nao o jogo: medido no alvo64 a 30 fps CRAVADOS (FG-PROVA §2), o
       `LAT ritmo do WGC` deu `p05 30,3 / p95 46,5 ms` - a captura atrasa ~14 ms em 1 de cada 20
       quadros. O apresentador lia esse tremor como tremor DA FONTE: `j = 21 ms` com uma fonte que
       nao treme nada, e `D` (a folga do prazo do G) inflado na mesma medida.
       O WGC entrega junto o SystemRelativeTime do quadro - o carimbo do proprio DWM, na base do
       QPC, feito ANTES da fila de entrega. Ele e o instante certo do quadro. `carimboMs` = esse
       numero em ms (0 = nao ha captura: modo injetado, ponte do jogo -> vale o relogio de sempre).
       O relogio de recepcao CONTINUA valendo para a fila e a latencia (m_ultChegada, o vao da
       fonte parada, o prazo do present): ali o que importa e quando o quadro chegou AQUI. */
    void Acordar(double carimboMs = 0.0) {
        const double t = carimboMs > 0.0 ? carimboMs : Ms();
        if (carimboMs > 0.0 && !m_carimboFonte.load(std::memory_order_relaxed)) m_carimboFonte.store(true, std::memory_order_relaxed);
        AcquireSRWLockExclusive(&m_srcLock);
        // carimbo repetido = a cadeia re-apresentou a MESMA entrega da captura: nao e chegada nova
        if (!(carimboMs > 0.0 && carimboMs == m_srcUlt)) {
            if (m_srcUlt > 0.0) {
                const double dt = t - m_srcUlt;
                if (dt > 1.0 && dt < 250.0) {
                    m_srcDts[m_srcN++ % 16] = dt;   // vaos (menu/pausa) ficam fora, como sempre
                    // X-GERADO: a DISTRIBUICAO dos intervalos, para separar de quem e o tremor -
                    // do carimbo (a fonte/compositor entregou fora de hora) ou da recepcao.
                    if (m_carN < (int)(sizeof m_carDt / sizeof m_carDt[0])) m_carDt[m_carN++] = dt;
                    /* `j` (o tremor da chegada) nasce AQUI quando ha carimbo: o desvio do intervalo
                       do QUADRO em relacao a mediana. A fila do laco drena isto em MedirJ. */
                    if (carimboMs > 0.0 && m_srcN > 8) {
                        double v[16]; int n = (int)std::min(m_srcN, 16);
                        for (int i = 0; i < n; i++) v[i] = m_srcDts[i];
                        std::sort(v, v + n);
                        const double medi = (n & 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
                        if (m_jFilaN < (int)(sizeof m_jFila / sizeof m_jFila[0])) m_jFila[m_jFilaN++] = fabs(dt - medi);
                    }
                }
            }
            m_srcUlt = t;
        }
        ReleaseSRWLockExclusive(&m_srcLock);
        if (m_evtNovo) SetEvent(m_evtNovo);
    }
    /* Nisto o laco principal espera no lugar do objeto de espera do swapchain oculto. */
    HANDLE EventoConsumiu() const { return m_evtConsumiu; }

    bool     Ativo() const { return m_ok; }
    bool     DeviceCru() const { return m_deviceCru; }
    bool     DeviceMorreu() const { return m_deviceMorreu.load(); }
    uint64_t Apresentados() const { return m_apresentados.load(); }
    ID3D11Device* Device() const { return m_dev; }
    IDXGISwapChain1* SwapChain() const { return m_sc; }
    // OCLUSAO (23/09/2026): a REGUA DE FLUIDEZ NATIVA tambem aqui (ver FlipProva::Regua). Com
    // a ponte de placas de pe - e nesta maquina ela e forcada mesmo com uma placa so - quem vai
    // a tela e ESTE swapchain; o da pelicula vive na janela oculta da rede e o PresentCount
    // dele nunca sai de zero. A leitura e feita na thread do Present (o delta so vale entre
    // Presents consecutivos); o laco principal so copia estes tres numeros 1x/s para o painel.
    // Corrida benigna: sao int/double/bool de diagnostico que mudam de 10 em 10 s.
    int    ReguaModa()    const { return m_regua.Moda(); }
    double ReguaModaPct() const { return m_regua.ModaPct(); }
    bool   ReguaNativo()  const { return m_regua.Nativo(); }

    void Parar() {
        Ponteiro::g_apresVivo.store(false);   // sec. 27: sem apresentador, a captura volta a desenhar a seta
        m_sair.store(true);
        m_pausar.store(false);          // senao o laco fica na pausa e nunca ve o m_sair
        if (m_evtNovo) SetEvent(m_evtNovo);
        // SAIDA RAPIDA: no fechamento a thread tem 200 ms para sair. Presa num Present que a
        // placa nao devolve (TDR do jogo por baixo), esperar mais so segura a GPU - e foi
        // exatamente assim que o COD perdeu o device. Entao ela e ABANDONADA: nada daqui para
        // baixo e liberado (soltar textura debaixo de uma thread viva trocaria uma espera por
        // uma queda) e o SO recolhe tudo quando o processo sair, uns milissegundos depois.
        if (m_th.joinable()) {
            if (Saida::Saindo() &&
                WaitForSingleObject((HANDLE)m_th.native_handle(), Saida::kTetoMs) != WAIT_OBJECT_0) {
                if (m_log) m_log("apresentador: a thread nao saiu em %u ms no fechamento - abandonada (o SO recolhe)", (unsigned)Saida::kTetoMs);
                m_th.detach();
                m_ok = false;
                Saida::Abandonei();   // ninguem mais solta nada: ela continua lendo a ponte
                return;
            }
            m_th.join();
        }
        m_pausado.store(false);
        m_folgaTex = nullptr; m_folgaN = 0;
        PendLimpar(); m_novoPendente = false; m_ultAlvo = 0.0; m_tiqueAj = 1.0;
        m_erroF = 0.0; m_faseForaN = 0; m_telaLidoMs = 0.0; m_tiqueComens = false;
        m_cMs = m_pMs = m_jMs = m_qMs = m_dMs = m_tMs = 0.0;
        m_pEsperando = false; m_pConta = 0; m_jPos = 0; m_jCalcMs = 0.0;
        // RODADA5: a cadencia recomeca (o despertador sai logo abaixo, em SoltarDespertador)
        m_cadPrep = false; m_cadTemAnt = false; m_cadUltCar = 0.0; m_cadTelaTemAnt = false; m_cadDk = 0; m_cadDkOk = false; m_cadDkMed = false; m_cadGradeMs = 0.0; m_cadOrigem = 0.0;
        // RODADA8: o FG 2x recomeca (as texturas pendentes sao do FI/ponte, que o dono solta; aqui so se esquece)
        m_f2N = 0; m_f2Prep = false; m_f2Dedup = false; m_f2TemAnt = false; m_f2TemTela = false; m_f2TemTelaReal = false; m_f2KOk = false; m_f2K = 0; m_f2GapN = 0;
        m_f2LogMs = m_f2SegMs = m_f2IniMs = 0.0; m_f2KDesceuMs = 0.0; m_f2KEsperaMs = 10000.0; m_f2SubirK = false; m_cadLeadBaixaDesde = 0.0; m_cadLeadDesceuMs = 0.0; m_cadLeadEsperaMs = 10000.0;
        m_flip.Soltar();   // a interface do DXGI sai antes do swapchain
        m_iflip.Soltar();  // RODADA6
        // X-FONTE-SOLTA: a admissao recomeca do zero (as contas dela sao da sessao do laco)
        m_admChegadas = m_admAceitas = m_admEmVoo = m_admOrc = m_admCedo = m_admVigia = m_admRajada = 0;
        m_admLogMs = m_admVigiaLogMs = m_ultDescarteMs = m_admUltAceitaMs = 0.0;
        for (int i = 0; i < kJN; i++) { m_jAmostra[i] = 0.0; m_jQuando[i] = 0.0; }
        m_ok = false;
        if (m_latWait) { CloseHandle(m_latWait); m_latWait = nullptr; }
        if (m_evtNovo) { CloseHandle(m_evtNovo); m_evtNovo = nullptr; }
        if (m_evtConsumiu) { CloseHandle(m_evtConsumiu); m_evtConsumiu = nullptr; }
        SoltarDespertador();
        Soltar(m_rtv); Soltar(m_inter); Soltar(m_srv); Soltar(m_srvTex);
        Soltar(m_vs); Soltar(m_ps); Soltar(m_samp);
        Soltar(m_fpsTextoSrv); Soltar(m_fpsTexto); Soltar(m_fpsAlvoRtv); Soltar(m_fpsAlvo); Soltar(m_fpsBlend);
        m_ponteiro.Soltar();   // RESET-CURSOR: o desenho do ponteiro na apresentacao vive neste device
        Soltar(m_ultimo); Soltar(m_comp); m_ultimoOk = false;
        m_fpsTextoAtual[0] = 0; m_fpsTW = m_fpsTH = m_fpsAW = m_fpsAH = 0; m_fpsCnt = 0; m_fpsCntT0 = 0.0; m_fpsUltConf = 0.0; m_fpsTelaFps = 0.0;
        Soltar(m_sc2); Soltar(m_sc);
        if (m_ctx) { m_ctx->ClearState(); m_ctx->Flush(); }
        Soltar(m_ctx); Soltar(m_dev);
    }
    ~Apresentador() { Parar(); }

private:
    void Laco() {
        SetThreadDescription(GetCurrentThread(), L"DLSS5 apresentador (placa do monitor)");
        while (!m_sair.load()) {
            /* A ESPERA. Sem interpolador: o evento de quadro novo, 50 ms de teto (como sempre).
               Com interpolador e um B pendente: ate o PRAZO dele, pelo despertador de alta
               resolucao (ver EsperarAte). Ate 18/09/2026 esta espera era um WaitForSingleObject
               com timeout em ms inteiros e SEM resolucao de timer pedida - num processo que
               nunca chamou timeBeginPeriod o timeout de "8 ms" acorda em ate ~15,6 ms, e o B
               saia colado no G seguinte (pares de presents e um vao: e o judder que o grafico
               de fps nao mostra). Um quadro novo que chegar durante a espera do B fica
               anotado (m_novoPendente) e e atendido logo depois de B sair - B nunca mais e
               ADIANTADO por causa da chegada. */
            // RODADA8: com o FG em saida fixa 2x (fg_saida=2x, padrao) a espera e do proprio F2Volta (pendentes travados
            // ao vblank + quadro novo); aqui nao se espera nada.
            const bool f2 = m_fg.load() && m_f2Pedido.load(std::memory_order_relaxed);
            if (f2) { /* ver F2Volta */ }
            else if (m_fg.load() && PendProximo()) { m_rWake += EsperarAte(PendProximo()->alvo, true, m_fila > 0); m_rWakeN++; }
            else if (m_novoPendente) m_novoPendente = false;
            else if (PonteiroSoVivo() || PonteiroEntrePossivel()) {
                // sec. 27: a seta entre quadros - acorda a cada meio refresh pelo timer de ALTA resolucao (o timeout em ms
                // de um WaitForSingleObject acordaria em ate ~15,6 ms), ou antes, com quadro novo
                if (!m_timer) CriarDespertador();
                const double meio = m_telaPeriodoMs > 0.5 ? 0.5 * m_telaPeriodoMs : 2.0;
                HANDLE hs[2] = { m_evtNovo, m_timer };
                DWORD nh = 1, tmo = 4;
                if (m_timer) { LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(meio * 10000.0); if (SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE)) { nh = 2; tmo = 60; } }
                WaitForMultipleObjects(nh, hs, FALSE, tmo);
            }
            else WaitForSingleObject(m_evtNovo, 50);   // RESET-CURSOR: com a tela parada, acorda por refresh para o ponteiro
            if (m_sair.load()) break;
            Ponte* p = m_ponte.load();
            // PAUSA COORDENADA: solta o que estiver na mao ANTES de confirmar, porque quem
            // pediu vai destruir esta ponte - um slot preso aqui travaria o Parar() dela, e uma
            // SRV apontando para uma textura dela ficaria pendurada num recurso morto.
            if (m_pausar.load()) {
                if (m_folgaN && p && p->Ativa()) p->Liberar(m_folgaN);
                m_folgaTex = nullptr; m_folgaN = 0;
                // o B pendente e textura do INTERPOLADOR (nao da ponte): sai agora, um pouco cedo,
                // em vez de ficar preso - e sem ele a espera de cima volta a ser a de 50 ms
                while (const Pend* pd = PendProximo()) { ApresentarTex(pd->t, pd->gerado); m_pendI++; }
                PendLimpar();
                F2Esvaziar("pausa");   // RODADA8: os reais pendentes saem agora; os gerados sao descartados (contados)
                Soltar(m_srv); Soltar(m_srvTex);
                m_pausado.store(true);
                SetEvent(m_evtConsumiu);   // o laco principal nao pode ficar preso na espera
                continue;
            }
            m_pausado.store(false);
            if (!p || !p->Ativa()) continue;
            if (FrameGen::Interpolador* fg = m_fg.load()) {
                if (f2) { F2Volta(p, fg); continue; }
                if (m_f2N > 0) F2Esvaziar("fg_saida trocada para meta");
                if (m_f2Dedup) { fg->DedupPermitir(true); m_f2Dedup = false; }
                LacoInterpolado(p, fg); continue;
            }
            if (m_f2N > 0) F2Esvaziar("interpolador desligado");
            // latencia 1: so pega o quadro depois que o swapchain aceita outro - assim o que
            // vai para a tela e o mais novo que existia no instante em que havia lugar.
            // TESTE-PONTE: este swapchain nasce SEM RENDER_TARGET_OUTPUT (para o ReShade pular),
            // e nesse caminho composto o objeto de espera nao sinaliza a cada vblank - ele
            // estourava o timeout de 100 ms e prendia o apresentador em ~7 fps. Timeout curto:
            // se o slot ja esta livre segue na hora; se nao, tenta no proximo quadro novo.
            LARGE_INTEGER tW0{}, tW1{}, tM1{}, tP1{}, qf{};
            QueryPerformanceFrequency(&qf);
            uint64_t n = 0;
            // RODADA5: com a cadencia por carimbo, o PROXIMO em ordem (nenhum quadro pulado); sem ela, o
            // mais novo, como sempre. A folga do Codec (m_segurar) fica no caminho de antes.
            // RODADA7: EM ORDEM sempre (cadencia ligada ou nao) - nenhum quadro da ponte e pulado. So a folga do
            // Codec (m_segurar) segue no caminho de antes. Se a cadencia SEGURA o quadro ate o alvo, quem decide e
            // CadAntesDoPresent (modo forcado, ou auto com razao nao inteira).
            const bool emOrdem = !m_segurar.load();
            bool cad = false;
            uint64_t filaDepois = 0, puladosPonte = 0;
            ID3D11Texture2D* t = emOrdem ? p->ReceberSeguinte(&n, &filaDepois, &puladosPonte) : p->Receber(&n);
            const double tRecebEst = Esteira::Ms();   // ESTEIRA: o quadro saiu da ponte para este laco
            if (!t) { ReapresentarSoPonteiro(); SetEvent(m_evtConsumiu); m_pulos++; continue; }
            if (emOrdem) {
                if (filaDepois > 0) m_novoPendente = true;   // ha mais na ponte: a proxima volta nao espera evento
            } else if (m_cadUltN && n > m_cadUltN + 1) {
                puladosPonte = n - m_cadUltN - 1;            // o Receber() de sempre pega o mais novo: os de tras sumiram
            }
            m_cadUltN = n;
            // a contabilidade (novo/copia, regua fonte x tela, latencia) vale nos DOIS modos - e ela que da o A/B;
            // a espera ate o prazo so com a cadencia ligada (forcada, ou auto em razao nao inteira)
            cad = CadAntesDoPresent(n, filaDepois, puladosPonte, emOrdem && m_cadOn.load(std::memory_order_relaxed));
            /* X-TDR (19/09/2026): a espera pelo lugar no swapchain ficava AQUI EM CIMA (4 ms, resultado
               ignorado) e o Present bloqueava depois, sem teto - no COD do dono ele ficou 1000 ms
               preso (a placa paginando por VRAM acima do orcamento). Agora a espera e UMA por Present,
               logo antes dele, com teto (EsperarLugar): sem lugar em 50 ms o quadro e pulado e a tela
               fica com o ultimo - a thread nunca mais fica presa atras da placa. */
            // FOLGA DE UM QUADRO (so no Codec - ver SegurarFolga). O Receber ja enfileirou a
            // espera/decodificacao no contexto do destino, entao o quadro guardado esta pronto
            // quando chegar a vez dele; o pico do decodificador cai no que ja estava na mao.
            if (m_segurar.load()) {
                ID3D11Texture2D* mostrar = m_folgaTex; const uint64_t mostrarN = m_folgaN;
                m_folgaTex = t; m_folgaN = n;
                if (!mostrar) { SetEvent(m_evtConsumiu); continue; }   // enchendo a folga: 1 quadro
                t = mostrar; n = mostrarN;
            } else if (m_folgaTex) {
                p->Liberar(m_folgaN); m_folgaTex = nullptr; m_folgaN = 0;   // folga desligada em voo
            }
            QueryPerformanceCounter(&tW0);
            bool lugar = EsperarLugar(kPresentMaxMs);
            // RODADA5: com a cadencia, o quadro sem lugar NAO e pulado de primeira - espera mais (em fatias de
            // 50 ms, atento a pausa/saida) ate ~300 ms. So uma placa parada de verdade passa disso, e ai o
            // descarte e contado e dito no log (a thread nao pode ficar presa atras de uma placa travada - X-TDR).
            // RODADA7 (MAPA-FLUXO §3 #11): em ordem, o quadro sem lugar NUNCA e pulado por pressa - ele espera
            // (fatias de 50 ms, atento a pausa/saida) ate 2 s, o prazo do watchdog do Windows (TDR). AVISO a 100 ms.
            // So uma placa travada de verdade passa disso, e ai o descarte e contado e dito (X-TDR: a thread nao
            // pode ficar presa para sempre atras de uma placa morta).
            if (!lugar && emOrdem) {
                m_semLugarEsperas.fetch_add(1, std::memory_order_relaxed);
                int k = 0; bool avisou = false;
                for (; !lugar && k < 40 && !m_sair.load() && !m_pausar.load(); k++) {
                    lugar = EsperarLugar(kPresentMaxMs);
                    if (!lugar && k == 1 && !avisou) {
                        avisou = true; m_semLugarLongas.fetch_add(1, std::memory_order_relaxed);
                        if (m_log) m_log("apresentador: AVISO - quadro %llu esperando lugar no swapchain ha ~100 ms (ADIADO, nao descartado)", (unsigned long long)n);
                    }
                }
                const double espMs = 50.0 * (k + 1);
                if (espMs > m_semLugarPiorMs) m_semLugarPiorMs = espMs;
                if (!lugar) {
                    m_cadSemLugar.fetch_add(1, std::memory_order_relaxed);
                    if (m_log) m_log("apresentador: AVISO - quadro %llu DESCARTADO: o swapchain ficou sem lugar por ~%.0f ms (placa parada? pausa/saida?)", (unsigned long long)n, espMs);
                }
            }
            QueryPerformanceCounter(&tW1);
            if (!lugar) { p->Liberar(n); SetEvent(m_evtConsumiu); continue; }
            Mostrar(t);
            QueryPerformanceCounter(&tM1);
            p->Liberar(n);
            // MAGPIE: Present(0,0) - apresenta o mais novo sem esperar vblank; a fila de 4
            // buffers absorve, e o present para de bloquear na contrapressao do compositor.
            // RODADA5: com a cadencia o quadro ja foi solto no prazo (meio refresh antes do vblank alvo) e sai com
            // SyncInterval 1 - fica ao menos um vblank na tela, nunca e substituido antes de o DWM compor. "Sem
            // VSync" (ou VRR de fato) continua em 0.
            // RODADA7: em ordem (cadencia ou nao) o Present e (1,0): "primeiro vblank depois de pronto". Com (0,0) no
            // flip model sem tearing, um quadro ainda na fila e SUBSTITUIDO pelo seguinte antes do vblank - um descarte
            // silencioso. So a folga do Codec segue com (0,0).
            UINT syncCad = 0, flCad = 0;
            if (emOrdem) {
                const int sm = m_sync.load(std::memory_order_relaxed);
                const bool semVblank = (sm == SYNC_SEM) || (sm == SYNC_VRR && m_telaVrr.load(std::memory_order_relaxed));
                syncCad = semVblank ? 0u : 1u;
                flCad = (sm == SYNC_SEM && m_tearingOk) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
            }
            HRESULT hr;
            { VigiaMarca vm(g_vigiaApresDesde); hr = m_sc->Present(syncCad, flCad); }
            ConferirPresent(hr, "cadencia");   // NS-2309 item 5(c)
            m_ponteiro.DepoisDoPresent();   // RESET-CURSOR: a medida do ponteiro (posicao desenhada x GetCursorPos agora)
            m_ultPresMs = Ms();
            QueryPerformanceCounter(&tP1);
            Esteira::Tela(n, CarimboDe(n), tRecebEst, Esteira::Ms());   // ESTEIRA: ordem na tela (carimbo sempre crescente) + tempos da tela
            CadDepoisDoPresent();
            if (cad) { m_cadEsperouJan = true; m_cadSegJanN++; }
            m_cadPresJanN++;
            // OCLUSAO (23/09/2026): a regua le a cada Present, logo depois dele (ver ReguaModa).
            if (m_regua.Amostrar(m_sc) && m_log) m_log("%s", m_regua.Linha("apresentador").c_str());
            // RODADA6: o veredito do iFlip da janela VISIVEL, 1x/s (ver FlipProva::Iflip).
            if (m_iflip.Observar(m_sc) && m_log) { m_log("%s", m_iflip.Linha().c_str()); CadIflipJanela(); }
            // MEDIDA-PONTE: soma quanto cada etapa custa, pra saber onde vao os ~200 ms/quadro.
            if (qf.QuadPart) {
                const double ms = 1000.0 / (double)qf.QuadPart;
                m_accEspera  += (tW1.QuadPart - tW0.QuadPart) * ms;   // espera do waitable
                m_accMostrar += (tM1.QuadPart - tW1.QuadPart) * ms;   // Receber + CopyResource
                const double presMs = (tP1.QuadPart - tM1.QuadPart) * ms;
                m_accPresent += presMs;   // Present()
                // DIAG-SOBREPOSICAO: este Present foi lento? E ele coincidiu com a janela do
                // Present OCULTO (cadeia neural, outra thread)? Se quase todo lento cai dentro
                // dessa janela, o apresentador esta sendo SERIALIZADO atras da cadeia (lock).
                if (presMs > 10.0) {
                    m_lentos++;
                    const long long hi = g_hpIni.load(std::memory_order_relaxed), hf = g_hpFim.load(std::memory_order_relaxed);
                    const long long a0 = tM1.QuadPart, a1 = tP1.QuadPart;
                    const bool ocultoEmCurso = (hi > hf);                       // oculto comecou e ainda nao acabou
                    const bool sobrepoe = ocultoEmCurso ? (a0 <= hi + (a1 - a0)) : !(a1 < hi || a0 > hf);
                    if (sobrepoe) m_lentosSobre++;
                }
                if (++m_medN >= 60 && m_log) {
                    m_log("apresentador MEDIDA: %d quadros | espera %.1f ms | receber+copiar %.1f ms | PRESENT %.1f ms | pulos(sem quadro novo) %llu | sem lugar em %u ms (placa atrasada): %llu | LENTOS(>10ms) %llu, dos quais DURANTE o Present oculto: %llu [media por quadro]",
                          m_medN, m_accEspera / m_medN, m_accMostrar / m_medN, m_accPresent / m_medN, (unsigned long long)m_pulos,
                          (unsigned)kPresentMaxMs, (unsigned long long)m_presPulados,
                          (unsigned long long)m_lentos, (unsigned long long)m_lentosSobre);
                    m_accEspera = m_accMostrar = m_accPresent = 0; m_medN = 0; m_pulos = 0; m_presPulados = 0; m_lentos = 0; m_lentosSobre = 0;
                }
            }
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                if (!m_deviceMorreu.exchange(true) && m_log)
                    m_log("apresentador: Present devolveu 0x%08X (device removed; motivo 0x%08X)",
                          (unsigned)hr, (unsigned)(m_dev ? m_dev->GetDeviceRemovedReason() : hr));
            }
            m_apresentados.fetch_add(1);
            SetEvent(m_evtConsumiu);
        }
    }

    /* ======================= TETO DE ESPERA NO CAMINHO DO PRESENT (X-TDR, 19/09/2026) =======================
       Regra: nada no caminho do Present desta thread espera mais de ~50 ms. O que acontecia (COD do dono,
       fonte monitor + Interpolada, 12:14): a placa parou de dar vazao (VRAM acima do orcamento, depois o
       watchdog), a fila do swapchain (4 buffers, latencia 3) encheu, e o `Present(0,0)` - que "nao
       bloqueia" - bloqueou 1000 ms por quadro (linha `Present 1000.32 ms`), com a thread de ritmo em
       prioridade ALTA presa atras da placa; o laco principal, que espera o `evtConsumiu`, ia junto.
       O objeto de espera do swapchain (GetFrameLatencyWaitableObject) e um SEMAFORO: cada Present que
       o compositor consome libera um lugar, cada espera que da certo toma um. Por isso a espera e UMA
       por Present e fica logo antes dele (antes, havia uma espera de 4 ms no topo do laco cujo resultado
       era ignorado, e o Present bloqueava do mesmo jeito). Sem lugar em kPresentMaxMs: o quadro e
       PULADO (a tela fica com o ultimo, que o compositor segura), o slot da ponte e devolvido e o laco
       segue - contado em `sem lugar` (linha MEDIDA) e `pulados` (linha INTERPOLADO). */
    static constexpr DWORD kPresentMaxMs = 50;
    bool EsperarLugar(DWORD ms) {
        if (!m_latWait) return true;
        if (WaitForSingleObjectEx(m_latWait, ms, FALSE) == WAIT_OBJECT_0) return true;
        m_presPulados++; m_presPuladosTotal++;
        return false;
    }
    /* ---------------- RODADA5: CADENCIA POR CARIMBO (ver Cadencia()) ---------------- */
    // O carimbo cru do quadro n da ponte (0 = nao ha: modo injetado/Nativo, ou o anel ja reescreveu).
    double CarimboDe(uint64_t n) const {
        const int i = (int)(n % kCarQ);
        if (m_carQn[i].load(std::memory_order_acquire) != n) return 0.0;
        const double v = m_carQv[i].load(std::memory_order_relaxed);
        return (m_carQn[i].load(std::memory_order_acquire) == n) ? v : 0.0;
    }
    // A grade do DWM (vblank, periodo e fase da composicao) relida a cada 100 ms - uma leitura por segundo
    // deixaria a fase escorregar com o erro do periodo (FLUIDEZ2). O qpcVBlank que o DWM devolve e o do
    // PROXIMO vblank e anda a cada leitura: os indices daqui sao ABSOLUTOS, contados de uma origem fixa
    // (m_cadOrigem), senao dois quadros lidos com grades diferentes nao seriam comparaveis. So vale se o
    // DWM fala da tela da janela (m_ancoraOk).
    bool CadGrade(double agora) {
        LerTela(agora);
        if (m_cadGradeMs <= 0.0 || agora - m_cadGradeMs >= 100.0) {
            m_cadGradeMs = agora;
            DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti);
            LARGE_INTEGER f; QueryPerformanceFrequency(&f);
            if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.qpcRefreshPeriod > 0 && ti.qpcVBlank > 0 && f.QuadPart) {
                m_cadP  = (double)ti.qpcRefreshPeriod * 1000.0 / (double)f.QuadPart;
                m_cadVb = (double)ti.qpcVBlank * 1000.0 / (double)f.QuadPart;
                if (m_cadOrigem <= 0.0 || fabs(m_cadP - m_cadPOrig) > 0.01 * m_cadP) { m_cadOrigem = m_cadVb; m_cadPOrig = m_cadP; m_cadTemAnt = false; m_cadTelaTemAnt = false; }
                m_cadBase = (long long)floor((m_cadVb - m_cadOrigem) / m_cadP + 0.5);
                // PESQ-CADENCIA 5.1: a fase da COMPOSICAO em relacao ao vblank (fracao de P), media circular lenta
                if (ti.qpcCompose > 0) {
                    const double c = (double)ti.qpcCompose * 1000.0 / (double)f.QuadPart;
                    double r = fmod((c - m_cadVb) / m_cadP, 1.0); if (r < 0.0) r += 1.0;
                    m_cadPhiCx = 0.9 * m_cadPhiCx + 0.1 * cos(2.0 * M_PI * r);
                    m_cadPhiCy = 0.9 * m_cadPhiCy + 0.1 * sin(2.0 * M_PI * r);
                    double a = atan2(m_cadPhiCy, m_cadPhiCx) / (2.0 * M_PI);   // em (-0,5; 0,5]: perto de 0 nao pula entre 0 e 1
                    m_cadPhiC = a;
                }
            }
        }
        return m_ancoraOk && m_cadP > 0.0 && m_cadVb > 0.0;
    }
    // A coordenada de um instante na grade ABSOLUTA, em refreshes (inteiro = um vblank).
    double CadG(double t) const { return (t - m_cadVb) / m_cadP + (double)m_cadBase; }
    // K (= D em refreshes): o menor inteiro com K*P >= p90(captura->pronto) + fase da fonte - fase da
    // composicao + P/2 (a soltura e meio refresh antes da composicao), entre -3 e o teto. Sobe na hora (pelo
    // p90 OU por atrasados > 1% em 2 s - PESQ-CADENCIA 5.3); desce um refresh por vez depois de 5 s sobrando.
    /* RODADA7: quanto ANTES do vblank alvo o Present e solto, em fracao de P. Composto: meio refresh antes da
       composicao que antecede o alvo (0,5 - fase da composicao), como na rodada 5. iFlip (FlipProva diz OVERLAY):
       nao ha composicao - o quadro vai a varredura no primeiro vblank depois do Present; o lead comeca em 0,5 P e
       sobe 0,25 P (ate 1 P) quando o present->tela medido mostra vblank perdido (CadIflipJanela). */
    double CadAvanco() const { return m_cadIflip ? m_cadLeadP : (0.5 - m_cadPhiC); }
    // RODADA7: 1x/s, depois da linha `pelicula:`. Atualiza o iFlip e ajusta o lead pelo present->tela REAL.
    void CadIflipJanela() {
        const bool antes = m_cadIflip;
        m_cadIflip = m_iflip.EmIflip();
        const double P = m_cadP;
        if (m_cadIflip && m_cadEsperouJan && m_cadSegJanN * 10 >= m_cadPresJanN * 9 && P > 0.0 && m_iflip.UltN() >= 20) {
            // segurando, o Present sai lead*P antes do vblank alvo: present->tela ~ lead*P quando acerta, +1 P quando perde
            const double p95 = m_iflip.UltP95();
            if (p95 > (m_cadLeadP + 0.6) * P) {
                m_cadLeadPerdaMs = Ms();   // RODADA8: vblank perdido nesta janela (a descida so conta 10 s depois do ultimo)
                // RODADA8 (a1b): com o lead ja no teto (1 P) e o FG 2x ativo, o vblank perdido e da GPU (o par nao ficou pronto a
                // tempo - o relogio da CPU soltou na hora e nao ve): quem sobe e o K do FG.
                if (m_cadLeadP >= 1.0 - 1e-9 && m_f2Prep) m_f2SubirK = true;
                if (m_cadLeadP < 1.0) {
                    const double novo = m_cadLeadP + 0.25;
                    // RODADA8: subiu de novo logo depois de uma descida (<= 5 s)? O degrau de baixo nao se sustenta: a proxima
                    // descida espera o DOBRO (10 -> 20 -> 40 -> 80 s). Sem isso o lead oscilava 0,75 <-> 1,0 a cada 10 s (a1).
                    const bool recaida = m_cadLeadDesceuMs > 0.0 && m_cadLeadPerdaMs - m_cadLeadDesceuMs <= 5000.0;
                    if (recaida) m_cadLeadEsperaMs = std::min(80000.0, 2.0 * m_cadLeadEsperaMs);
                    if (m_log) m_log("cadencia: iFlip - present->tela p95 %.2f ms > lead %.2f P + 0,6 P (vblank perdido): lead %.2f -> %.2f P%s", p95, m_cadLeadP, m_cadLeadP, novo,
                                     recaida ? " (recaida logo apos a descida: a proxima descida espera o dobro)" : "");
                    m_cadLeadP = novo; m_cadDRecalcMs = 0.0;
                }
            }
        }
        /* RODADA8 (PLANO-FLUIDEZ, Agente C, item 3): o lead tambem DESCE, pela mesma regra do K - um degrau (0,25 P) depois
           de 10 s sem nenhum atrasado (reais da cadencia e do FG 2x) e sem nenhuma janela com vblank perdido. Piso 0,5 P.
           Se o degrau de baixo recair logo (<= 5 s), a espera da proxima descida dobra (ate 80 s). */
        if (m_cadIflip && m_cadLeadP > 0.5 + 1e-9) {
            const double agora = Ms();
            // o relogio da descida conta do ULTIMO evento (atrasado ou vblank perdido) ou do ultimo degrau
            double ultEvento = m_cadLeadPerdaMs;
            for (int i = 0; i < 64; i++) { if (m_cadAtrT[i] > ultEvento) ultEvento = m_cadAtrT[i]; if (m_f2AtrT[i] > ultEvento) ultEvento = m_f2AtrT[i]; }
            if (m_cadLeadBaixaDesde <= 0.0 || ultEvento > m_cadLeadBaixaDesde) m_cadLeadBaixaDesde = std::max(ultEvento, m_cadLeadBaixaDesde > 0.0 ? m_cadLeadBaixaDesde : agora);
            if (agora - m_cadLeadBaixaDesde >= m_cadLeadEsperaMs) {
                const double novo = std::max(0.5, m_cadLeadP - 0.25);
                if (m_log) m_log("cadencia: iFlip - %.0f s sem atrasados nem vblank perdido: lead %.2f -> %.2f P", m_cadLeadEsperaMs / 1000.0, m_cadLeadP, novo);
                m_cadLeadP = novo; m_cadDRecalcMs = 0.0; m_cadLeadBaixaDesde = agora; m_cadLeadDesceuMs = agora;
            }
        } else m_cadLeadBaixaDesde = 0.0;
        if (antes != m_cadIflip && m_log)
            m_log("cadencia: pelicula %s - avanco da soltura agora %.2f P (%s); present->tela p50 %.2f ms", m_cadIflip ? "EM iFlip" : "COMPOSTA",
                  CadAvanco(), m_cadIflip ? "lead do caminho direto" : "0,5 - fase da composicao", m_iflip.UltP50());
        if (antes != m_cadIflip) m_cadDRecalcMs = 0.0;   // K recalculado ja
        m_cadEsperouJan = false; m_cadSegJanN = m_cadPresJanN = 0;
    }
    // RODADA7: a razao tela/fonte, medida 1x/s sobre os carimbos crus dos quadros NOVOS dos ultimos 4 s (media dos
    // intervalos < 100 ms: fonte parada nao conta). Inteira (tolerancia 1%) = cadencia auto DESLIGADA; nao inteira =
    // LIGADA. Tres decisoes iguais seguidas para trocar.
    void CadAuto(double car, double agora) {
        m_cadAutoCar[m_cadAutoPos] = car; m_cadAutoPos = (m_cadAutoPos + 1) % 256;
        if (m_cadAutoIni <= 0.0) m_cadAutoIni = car;
        if (car - m_cadAutoIni < 5000.0) return;   // os primeiros 5 s (abertura, montagem da rede) nao decidem nada
        if (m_cadAutoMs > 0.0 && agora - m_cadAutoMs < 1000.0) return;
        m_cadAutoMs = agora;
        double v[256]; int nv = 0;
        for (int i = 0; i < 256; i++) if (m_cadAutoCar[i] > 0.0 && car - m_cadAutoCar[i] <= 4000.0 && m_cadAutoCar[i] <= car) v[nv++] = m_cadAutoCar[i];
        if (nv < 10 || m_cadP <= 0.0) return;
        std::sort(v, v + nv);
        double soma = 0.0; int nd = 0;
        // intervalos da fonte (todos < 100 ms), janela de 4 s. NAO se aparam os longos: no jogo falso os 7-8 refreshes
        // (5%) sao compensados pelos 3 (20%) e a media inteira da 4,00; aparar dava 3,79 (medido na R1/N1 da rodada 5).
        double dd[256]; int ndd = 0;
        for (int i = 1; i < nv; i++) { const double d = v[i] - v[i - 1]; if (d > 0.5 && d < 100.0) dd[ndd++] = d; }
        if (ndd < 8) return;
        for (int i = 0; i < ndd; i++) { soma += dd[i]; nd++; }   // (ate 256 quadros: acima de 64 fps a janela encurta)
        if (nd < 8) return;
        const double per = soma / nd;
        const double r = per / m_cadP;
        const double ri = floor(r + 0.5);
        const bool inteira = ri >= 1.0 && fabs(r - ri) <= 0.01 * r;
        const bool quer = !inteira;
        m_cadAutoR = r; m_cadAutoFps = 1000.0 / per;
        if (quer == m_cadAutoLiga) { m_cadAutoVoto = 0; return; }
        if (++m_cadAutoVoto < 3) return;   // 3 decisoes iguais seguidas (3 s) para trocar
        m_cadAutoVoto = 0; m_cadAutoLiga = quer;
        if (m_log) m_log("cadencia AUTO: fonte %.2f fps (%.3f ms), tela %.2f Hz -> razao %.3f %s (tolerancia 1%%) -> cadencia %s",
                         m_cadAutoFps, per, 1000.0 / m_cadP, r, inteira ? "INTEIRA" : "NAO inteira", quer ? "LIGADA (segura ate carimbo + K)" : "desligada (primeiro vblank depois de pronto)");
    }
    void CadAtualizarD(double agora, double P) {
        if (m_cadDkOk && agora - m_cadDRecalcMs < 250.0) return;
        m_cadDRecalcMs = agora;
        double v[512]; int n = 0;
        for (int i = 0; i < 512; i++) if (m_cadPathT[i] > 0.0 && agora - m_cadPathT[i] <= 2000.0) v[n++] = m_cadPathV[i];
        if (n < 8) { if (!m_cadDkOk) { m_cadDk = m_cadTeto.load(); m_cadDkOk = true; m_cadDMsAt.store(m_cadDk * P); } return; }
        std::sort(v, v + n);
        m_cadP90 = v[std::min(n - 1, (int)(n * 0.90))];
        int atr = 0; for (int i = 0; i < 64; i++) if (m_cadAtrT[i] > 0.0 && agora - m_cadAtrT[i] <= 2000.0) atr++;
        const int teto = m_cadTeto.load();
        // RODADA7: em iFlip nao ha composicao entre o Present e a tela (present->tela ~2 ms contra ~10 ms composto): o
        // avanco da soltura e o lead medido (CadAvanco), e nao mais meio refresh antes de uma composicao que nao existe.
        int k = (int)ceil((m_cadP90 + m_cadPhiS * P + CadAvanco() * P) / P - 1e-6);
        if (k < -3) k = -3;   // piso: K pode ser 0 ou negativo - o carimbo do WGC ja e um instante FUTURO (a hora de exibicao da composicao), e o quadro costuma ficar pronto ~6 ms ANTES dele (medido na R1b); o que segura K para cima sao os atrasados
        const bool porAtraso = (atr >= 2 && atr * 100 > n) && m_cadDkOk && k <= m_cadDk;
        if (porAtraso) k = m_cadDk + 1;
        const bool estourou = k > teto;
        if (k > teto) k = teto;
        const int antes = m_cadDk;
        if (!m_cadDkOk || !m_cadDkMed || k > m_cadDk) { m_cadDk = k; m_cadDkOk = true; m_cadDkMed = true; m_cadBaixaDesde = 0.0; }   // a primeira MEDIDA vale na hora (antes dela, o teto)
        else if (k < m_cadDk) {
            // descer custa um quadro com 1 refresh a menos (um 3 isolado): so depois de 10 s SEM nenhum atrasado
            int atr10 = 0; for (int i = 0; i < 64; i++) if (m_cadAtrT[i] > 0.0 && agora - m_cadAtrT[i] <= 10000.0) atr10++;
            if (atr10 > 0) m_cadBaixaDesde = agora;
            else if (m_cadBaixaDesde <= 0.0) m_cadBaixaDesde = agora;
            else if (agora - m_cadBaixaDesde >= 10000.0) { m_cadDk--; m_cadBaixaDesde = agora; }
        } else m_cadBaixaDesde = 0.0;
        m_cadDMsAt.store(m_cadDk * P, std::memory_order_relaxed);
        if (m_cadDk != antes && m_log)
            m_log("cadencia: K %d -> %d refreshes (%.2f ms) | p90 captura->pronto %.2f ms em %d quadros (2 s), atrasados %d%s | fase da fonte %.2f P, da composicao %.2f P | teto %d%s",
                  antes, m_cadDk, m_cadDk * P, m_cadP90, n, atr, porAtraso ? " (> 1%: subiu por atraso)" : "", m_cadPhiS, m_cadPhiC, teto,
                  estourou ? " - O CAMINHO NAO CABE NO TETO: quem passar da hora sai na hora (atrasados)" : "");
    }
    // RODADA7: devolve true se SEGUROU o quadro ate o alvo. `permitido` = em ordem e cadencia nao desligada; se ela
    // segura de fato decide o modo (forcada, ou auto com razao tela/fonte nao inteira - CadAuto).
    bool CadAntesDoPresent(uint64_t n, uint64_t fila, uint64_t pulados, bool permitido) {
        const int modoCad = m_cadModo.load(std::memory_order_relaxed);
        const bool esperar = permitido && (modoCad == 1 || (modoCad == 2 && m_cadAutoLiga));
        bool segurou = false;
        const double tPronto = Ms();
        if (esperar && !m_cadPrep) {
            m_cadPrep = true;
            CriarDespertador();
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
            // PESQ-CADENCIA 5.2: fila do swapchain em 1 no modo cadencia. Com 3, dois presents colados (um
            // atrasado + o seguinte) entravam os dois na fila e o segundo caia um refresh depois sem aviso; com
            // 1 a espera acontece no EsperarLugar, medida.
            if (m_sc2) { const HRESULT hl = m_sc2->SetMaximumFrameLatency(1); if (m_log) m_log("cadencia: fila do swapchain do apresentador = 1 quadro (era 3) -> hr=0x%08X", (unsigned)hl); }
        }
        m_cadNovoAt = m_cadCopiaAt = false; m_cadCarAt = 0.0;
        if (pulados) {
            m_cadPulados.fetch_add(pulados, std::memory_order_relaxed);
            if (m_log) m_log("apresentador: AVISO - %llu quadro(s) da ponte reescritos antes de serem lidos (quadro %llu)", (unsigned long long)pulados, (unsigned long long)n);
        }
        if (fila > m_cadFilaMax) m_cadFilaMax = fila;
        if (fila >= 2) {
            m_cadFilaAvisos.fetch_add(1, std::memory_order_relaxed);
            if (m_log && tPronto - m_cadAvisoMs > 1000.0) { m_cadAvisoMs = tPronto; m_log("apresentador: AVISO - fila de %llu quadros prontos atras do quadro %llu (a tela nao esta acompanhando a fonte; nada e descartado)", (unsigned long long)fila, (unsigned long long)n); }
        }
        const double car = CarimboDe(n);
        if (car <= 0.0) return false;                                     // sem carimbo: sai na hora, como antes
        if (m_cadUltCar > 0.0 && car == m_cadUltCar) { m_cadCopiaAt = true; return false; }   // mesma entrega de novo (present ocioso da pelicula)
        m_cadNovoAt = true; m_cadCarAt = car;
        if (!CadGrade(tPronto)) { m_cadSemAncora++; m_cadUltCar = car; m_cadTemAnt = false; return false; }
        CadAuto(car, tPronto);   // RODADA7: a razao tela/fonte (so muda a decisao do PROXIMO quadro)
        const double P = m_cadP;
        const double path = tPronto - car;
        if (path > -100.0 && path < 250.0) { m_cadPathV[m_cadPathPos] = path; m_cadPathT[m_cadPathPos] = tPronto; m_cadPathPos = (m_cadPathPos + 1) % 512; }
        // PESQ-CADENCIA 5.1 (1): a FASE DA FONTE. O carimbo do WGC e um tique do DWM, entao a fracao dele na
        // grade e quase constante; arredondar em torno DELA (e nao em torno da grade do destino) tira a moeda
        // do carimbo que cai perto do meio de dois vblanks. Media circular das ultimas chegadas (lenta).
        const double gc = CadG(car);
        {
            const double r = gc - floor(gc);
            m_cadPhiSx = 0.94 * m_cadPhiSx + 0.06 * cos(2.0 * M_PI * r);
            m_cadPhiSy = 0.94 * m_cadPhiSy + 0.06 * sin(2.0 * M_PI * r);
            double a = atan2(m_cadPhiSy, m_cadPhiSx) / (2.0 * M_PI);   // em (-0,5; 0,5]: fase ~0 nao pula entre 0 e 1 (o indice da fonte mudaria 1)
            m_cadPhiS = a;
        }
        CadAtualizarD(tPronto, P);
        // o vblank da FONTE, arredondado em torno da fase dela; se a fase estiver perto de 1 (logo antes do
        // vblank), o -phiS leva o indice para o vblank de cima, que e o certo
        const long long srcIdx = (long long)floor(gc - m_cadPhiS + 0.5);
        long long idx = srcIdx + m_cadDk;                                  // o alvo: o vblank da fonte + K
        if (m_cadTemAnt && idx <= m_cadIdxAnt) { idx = m_cadIdxAnt + 1; m_cadEmpurrados++; }   // nunca dois quadros no mesmo vblank
        // PESQ-CADENCIA 5.1 (3): soltar no MEIO do intervalo entre as composicoes que antecedem o alvo.
        double solta = m_cadVb + ((double)(idx - m_cadBase) - CadAvanco()) * P;   // RODADA7: composto = (0,5 - fase da composicao) P, como antes; iFlip = lead
        const double teto = car + (double)m_cadDk * P + 0.5 * P;          // nunca segurar mais de 1 refresh alem do alvo
        if (solta > teto) solta = teto;
        if (!esperar) {
            // cadencia desligada: sai na hora (o de sempre), mas a conta diz se teria sido atrasado
        } else if (tPronto < solta) {
            m_cadSeg++; m_cadEspAcc += solta - tPronto;
            EsperarAte(solta, true); segurou = true;
        } else if (tPronto > solta + 0.25) {
            // atrasado: sai JA (Present(1)); o proximo continua mirando o PROPRIO srcIdx + K - o erro nao propaga
            m_cadAtrasados.fetch_add(1, std::memory_order_relaxed); m_cadAtrJan++;
            m_cadAtrT[m_cadAtrPos] = tPronto; m_cadAtrPos = (m_cadAtrPos + 1) % 64;
        }
        m_cadIdxAt = idx;
        m_cadIdxAnt = idx; m_cadUltCar = car; m_cadTemAnt = true;
        return segurou;
    }
    // Depois do Present: contadores, latencia carimbo->Present e a regua FONTE x TELA (relogio: o proximo
    // vblank depois do Present, na grade do DWM - a mesma pergunta da regua do relogio, por quadro NOVO).
    void CadDepoisDoPresent() {
        const double tP = Ms();
        if (m_cadCopiaAt) { m_cadTelaCopias.fetch_add(1, std::memory_order_relaxed); m_cadCopJan++; }
        if (m_cadNovoAt) {
            m_cadTelaNovos.fetch_add(1, std::memory_order_relaxed); m_cadNovosJan++;
            const double lat = tP - m_cadCarAt;
            if (lat > -100.0 && lat < 1000.0 && m_cadLatN < 4096) m_cadLat[m_cadLatN++] = lat;
            if (m_cadP > 0.0 && m_cadVb > 0.0) {
                const long long idxTela = (long long)floor(CadG(tP)) + 1;   // o proximo vblank depois do Present, na grade ABSOLUTA
                if (m_cadTelaTemAnt) {
                    const double dc = m_cadCarAt - m_cadTelaCarAnt;
                    if (dc > 0.0 && dc < 250.0) {
                        if (dc > 21.0) m_cadF21++;
                        if (dc > 25.0) m_cadF25++;
                        if (dc > m_cadFMax) m_cadFMax = dc;
                        const long long ds = (long long)floor(dc / m_cadP + 0.5);
                        const long long dt = idxTela - m_cadTelaIdxAnt;
                        m_cadHFonte[ds < 0 ? 0 : ds > 15 ? 15 : ds]++;
                        m_cadHTela[dt < 0 ? 0 : dt > 15 ? 15 : dt]++;
                        m_cadCmpN++; if (ds == dt) m_cadCasou++;
                    }
                }
                m_cadTelaIdxAnt = idxTela; m_cadTelaCarAnt = m_cadCarAt; m_cadTelaTemAnt = true;
            }
        }
        if (m_cadLogMs <= 0.0) m_cadLogMs = tP;
        if (tP - m_cadLogMs >= 10000.0 && m_log) {
            auto hist = [](const uint64_t* h, uint64_t tot, char* out, size_t cap) {
                int ord[16]; for (int i = 0; i < 16; i++) ord[i] = i;
                std::sort(ord, ord + 16, [h](int a, int b) { return h[a] > h[b]; });
                size_t pos = 0; out[0] = 0;
                for (int j = 0; j < 4 && tot; j++) {
                    if (!h[ord[j]]) break;
                    const int e = _snprintf(out + pos, cap - pos, "%s%d=%.0f%%", j ? " " : "", ord[j], 100.0 * (double)h[ord[j]] / (double)tot);
                    if (e <= 0 || (size_t)e >= cap - pos) break;
                    pos += (size_t)e;
                }
                if (!pos) _snprintf(out, cap, "-");
            };
            char hf[96], ht[96]; hist(m_cadHFonte, m_cadCmpN, hf, sizeof hf); hist(m_cadHTela, m_cadCmpN, ht, sizeof ht);
            double p50 = 0.0, p95 = 0.0;
            if (m_cadLatN > 0) { std::sort(m_cadLat, m_cadLat + m_cadLatN); p50 = m_cadLat[m_cadLatN / 2]; p95 = m_cadLat[std::min(m_cadLatN - 1, (int)(m_cadLatN * 0.95))]; }
            m_log("cadencia: %llu quadros novos em %.0f s | FONTE (carimbo cru, em refreshes) %s | TELA (relogio) %s | casou com a fonte %.1f%% de %llu | K %d refreshes (%.2f ms; p90 captura->pronto %.2f ms; fase fonte %.2f P, composicao %.2f P) | latencia carimbo->Present p50 %.2f / p95 %.2f ms | segurados %llu (espera media %.2f ms) | atrasados %llu | empurrados %llu | copias %llu | fila max %llu | sem ancora %llu | modo %s (auto: razao tela/fonte %.3f, fonte %.2f fps -> %s) | %s, avanco %.2f P, present->tela p50 %.2f ms | sem lugar: %llu adiados (pior %.0f ms) | fonte: intervalos >21 ms %llu, >25 ms %llu, max %.1f ms",
                  (unsigned long long)m_cadNovosJan, (tP - m_cadLogMs) / 1000.0, hf, ht,
                  m_cadCmpN ? 100.0 * (double)m_cadCasou / (double)m_cadCmpN : 0.0, (unsigned long long)m_cadCmpN,
                  m_cadDk, m_cadDk * m_cadP, m_cadP90, m_cadPhiS, m_cadPhiC, p50, p95,
                  (unsigned long long)m_cadSeg, m_cadSeg ? m_cadEspAcc / (double)m_cadSeg : 0.0,
                  (unsigned long long)m_cadAtrJan, (unsigned long long)m_cadEmpurrados, (unsigned long long)m_cadCopJan,
                  (unsigned long long)m_cadFilaMax, (unsigned long long)m_cadSemAncora,
                  m_cadModo.load() == 0 ? "DESLIGADA" : m_cadModo.load() == 1 ? "FORCADA" : "AUTO", m_cadAutoR, m_cadAutoFps, m_cadAutoLiga ? "segura" : "1o vblank",
                  m_cadIflip ? "iFlip" : "composta", CadAvanco(), m_iflip.UltP50(), (unsigned long long)m_semLugarEsperas.load(), m_semLugarPiorMs,
                  (unsigned long long)m_cadF21, (unsigned long long)m_cadF25, m_cadFMax);
            m_semLugarPiorMs = 0.0; m_cadF25 = m_cadF21 = 0; m_cadFMax = 0.0;
            m_cadLogMs = tP;
            for (int i = 0; i < 16; i++) { m_cadHFonte[i] = 0; m_cadHTela[i] = 0; }
            m_cadCasou = m_cadCmpN = 0; m_cadLatN = 0; m_cadSeg = 0; m_cadEspAcc = 0.0; m_cadAtrJan = 0;
            m_cadEmpurrados = 0; m_cadCopJan = 0; m_cadFilaMax = 0; m_cadNovosJan = 0; m_cadSemAncora = 0;
        }
    }
    // ------------------------------------------------ FLUIDEZ INTERPOLADA (experimental)
    static double Ms() { LARGE_INTEGER t, f; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / (double)f.QuadPart; }

    /* ======================= O DESPERTADOR DO RITMO (18/09/2026) =======================
       O mercado inteiro apresenta o quadro gerado por uma thread de ritmo de alta prioridade
       com espera precisa (FSR: media movel + espera ativa; XeFG: thread de alta prioridade;
       ver PESQ-FG §3). Aqui a espera e em dois tempos: o grosso num waitable timer de ALTA
       RESOLUCAO (CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, Windows 10 1803+: ~0,5 ms de
       precisao sem mexer na resolucao global do timer, que e o que timeBeginPeriod faria
       para o processo inteiro), e a reta final (kSpinMs) em espera ativa, que e o unico jeito
       de acertar sub-milissegundo num SO que nao e de tempo real. Se o timer de alta
       resolucao nao existir (Windows antigo), cai para timer comum + timeBeginPeriod(1)
       carregado em tempo de execucao (winmm nao entra no link), com reta final maior. */
    static constexpr double kSpinHrMs = 1.0;    // reta final com timer de alta resolucao
    static constexpr double kSpinLoMs = 2.5;    // reta final com timeBeginPeriod(1)
    void CriarDespertador() {
        if (m_timer) return;
        m_timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002u /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
        m_timerHr = (m_timer != nullptr);
        if (!m_timer) {
            m_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
            typedef UINT (WINAPI *PfnPeriodo)(UINT);
            HMODULE mm = LoadLibraryW(L"winmm.dll");
            PfnPeriodo tb = mm ? (PfnPeriodo)(void*)GetProcAddress(mm, "timeBeginPeriod") : nullptr;
            if (tb && tb(1) == 0) m_periodo1 = true;
        }
        m_spinMs = m_timerHr ? kSpinHrMs : kSpinLoMs;
        if (m_log) m_log("apresentador: despertador do ritmo = %s (reta final em espera ativa de %.1f ms); thread do apresentador em prioridade ALTA",
                         m_timerHr ? "waitable timer de ALTA resolucao" : m_periodo1 ? "timer comum + timeBeginPeriod(1)" : "timer comum (SEM resolucao de 1 ms: o ritmo vai sofrer)", m_spinMs);
    }
    void SoltarDespertador() {
        if (m_timer) { CloseHandle(m_timer); m_timer = nullptr; }
        if (m_periodo1) {
            typedef UINT (WINAPI *PfnPeriodo)(UINT);
            HMODULE mm = GetModuleHandleW(L"winmm.dll");
            PfnPeriodo te = mm ? (PfnPeriodo)(void*)GetProcAddress(mm, "timeEndPeriod") : nullptr;
            if (te) te(1);
            m_periodo1 = false;
        }
        m_timerHr = false;
    }
    /* Espera ate `alvoMs` (relogio de Ms()). `anotarNovo`: um quadro novo que chegar no meio
       fica em m_novoPendente e a espera CONTINUA ate o prazo - e assim que o B deixa de ser
       adiantado pela chegada. A pausa coordenada e o Parar() interrompem (m_pausar/m_sair).
       Devolve quanto o despertador (antes da reta final) passou do ponto pedido, em ms - e a
       medida da qualidade do timer, que vai para o log. */
    /* `sairNoNovo` (X-FG-QUALIDADE, fila de um quadro): um quadro novo que chegar com mais de
       LimiarGerarMs() de folga ate o prazo INTERROMPE a espera (anotado), para o laco gerar o par
       dele enquanto o pendente ainda nao esta na hora - e voltar a esperar. Sem folga, a espera
       segue. K-RITMO: esse limiar era 5 ms fixos e agora e `c + p` desta maquina (ver MedirC/MedirP)
       - numa placa em que o par custa 3 ms ele e 3x menor, e numa em que custa 15 ms, 3x maior. */
    double EsperarAte(double alvoMs, bool anotarNovo, bool sairNoNovo = false) {
        double atrasoTimer = 0.0;
        for (;;) {
            if (m_sair.load() || m_pausar.load()) return 0.0;
            const double falta = alvoMs - Ms();
            if (falta <= m_spinMs) break;
            const double pedido = falta - m_spinMs;
            HANDLE hs[2] = { m_evtNovo, m_timer };
            DWORD n = 1, tmo = (DWORD)std::max(1.0, pedido);
            if (m_timer) {
                LARGE_INTEGER due; due.QuadPart = -(LONGLONG)(pedido * 10000.0);
                if (due.QuadPart > -1) due.QuadPart = -1;
                if (SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE)) { n = 2; tmo = 60; }
            }
            const double t0 = Ms();
            const DWORD r = WaitForMultipleObjects(n, hs, FALSE, tmo);
            if (r == WAIT_OBJECT_0) {   // quadro novo: anotado, a espera segue (ou sai, com folga, na fila)
                if (anotarNovo) m_novoPendente = true;
                if (sairNoNovo && alvoMs - Ms() > LimiarGerarMs()) return atrasoTimer;
                continue;
            }
            atrasoTimer = (Ms() - t0) - pedido;   // quanto o timer passou do que foi pedido
        }
        while (Ms() < alvoMs) YieldProcessor();   // reta final: espera ativa curta
        return atrasoTimer;
    }
    // mediana filtrada das ultimas 16 chegadas DA CADEIA (medidas em Acordar(); intervalos
    // absurdos ficam fora ao entrar). `n` sai com quantas ha (o gate so decide com >= 8).
    double MedianaChegada(int* nAmostras = nullptr) const {
        double v[16]; int n;
        /* X-FG-QUALIDADE (19/09/2026): com a captura entregando quadros REPETIDOS (o interpolador os
           descarta por conteudo - fg_ffx.h, CriarDedup), a chegada da cadeia conta dois "quadros" por
           quadro do jogo e a mediana dela e metade do intervalo real: o G sairia no quarto do
           intervalo, o FI receberia um frameTimeDelta errado e o gate compararia com uma chegada
           que a tela nunca veria sem FG. Enquanto houver repetido recente (ultimos 3 s) e amostras
           bastantes, vale o anel dos DISTINTOS (intervalos entre quadros que o interpolador aceitou,
           medidos no relogio da cadeia - m_dstDts). Sem repetidos, o de sempre: o anel da cadeia,
           que nao sofre do circulo fechado do apresentador (ver Acordar()). */
        /* X-FONTE-SOLTA (20/09/2026): o mesmo vale para os quadros que a ADMISSAO recusou antes
           do FI (ver AdmitirPar). Se a mediana continuasse vindo da cadeia, o tique nasceria de
           uma taxa de chegada que a fila nunca ve e ela secaria entre um par e o seguinte. */
        const double ultDesc = std::max(m_ultRepetidoMs, m_ultDescarteMs);
        if (m_dstN >= 8 && ultDesc > 0.0 && Ms() - ultDesc < 3000.0) {
            n = std::min(m_dstN, 16); for (int i = 0; i < n; i++) v[i] = m_dstDts[i];
            if (nAmostras) *nAmostras = n;
            std::sort(v, v + n); return (n & 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
        }
        AcquireSRWLockShared(&m_srcLock);
        n = std::min(m_srcN, 16); for (int i = 0; i < n; i++) v[i] = m_srcDts[i];
        ReleaseSRWLockShared(&m_srcLock);
        if (nAmostras) *nAmostras = n;
        if (n == 0) return 16.667;
        std::sort(v, v + n); return (n & 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    }
    /* X-FG-QUALIDADE: a mediana da cadeia crua (com os repetidos), so para o log dizer as duas. */
    double MedianaCadeiaCrua() const {
        double v[16]; int n;
        AcquireSRWLockShared(&m_srcLock);
        n = std::min(m_srcN, 16); for (int i = 0; i < n; i++) v[i] = m_srcDts[i];
        ReleaseSRWLockShared(&m_srcLock);
        if (n == 0) return 0.0;
        std::sort(v, v + n); return (n & 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    }
    /* Latencia maxima do swapchain no modo interpolado (X-FG-QUALIDADE, 19/09/2026). O apresentador
       nasce com 3 (MAGPIE: BufferCount-1, para o Present nunca bloquear na contrapressao do
       compositor). Com o FG cada present ja sai no relogio (G e B em prazos proprios), entao a fila
       so cresce quando o compositor atrasa - e ai cada quadro na fila e ~um intervalo de present a
       mais entre o present e a tela (PRESENT->TELA 37/95 ms no COD do dono). Duas basta: uma em
       composicao e uma esperando. Sem lugar em kPresentMaxMs o quadro e pulado (EsperarLugar), como
       antes - o teto nunca prende a thread de ritmo. Chamada na thread do apresentador, antes de
       qualquer Present interpolado. DLSS5_FG_LATENCIA=<1..3> na bancada. */
    /* O1: a escolha do card (1 a 3) manda; 0 = o padrao, que e 2 - ou 1 no modo "Sem VSync",
       onde a razao de existir do modo e justamente nao ter quadro esperando. A env da bancada
       continua valendo por cima. Reaplicada quando o modo ou a escolha mudam (m_latAplicada). */
    void AjustarLatenciaFg() {
        if (!m_sc2 || !m_latWait) return;
        int quer = m_latQuer > 0 ? m_latQuer : (m_sync.load() == SYNC_SEM ? 1 : 2);
        { wchar_t e[8]{}; if (GetEnvironmentVariableW(L"DLSS5_FG_LATENCIA", e, 8) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 1 && v <= 3) quer = (int)v; } }
        if (quer == m_latAplicada) return;
        const HRESULT hr = m_sc2->SetMaximumFrameLatency((UINT)quer);
        m_latAplicada = quer;
        if (m_log) m_log("apresentador: latencia maxima do swapchain no modo interpolado = %d quadro(s) (nasce com 3) -> hr=0x%08X%s", quer, (unsigned)hr, m_latQuer > 0 ? " (escolha do card)" : "");
    }
    /* Ha quanto tempo a cadeia leu o quadro mais novo (ms). ~0 quando o apresentador estava
       ocioso esperando por ele; grande quando o quadro ja esperava (o apresentador esta atras). */
    double IdadeDoNovoMs() const {
        AcquireSRWLockShared(&m_srcLock);
        const double u = m_srcUlt;
        ReleaseSRWLockShared(&m_srcLock);
        return u > 0.0 ? std::max(0.0, Ms() - u) : 0.0;
    }
    /* ======================= O GATE: "eu estaria melhor SEM o FG?" =======================
       ATE 13/09/2026 o gate era um NUMERO FIXO: pausa com chegada < 40 fps por 3 s, volta com
       >= 45 por 2 s. O numero nao respondia a pergunta certa e, medido, chegou a fazer o
       contrario do que promete: receita "60 fps com qualidade" (rede 50%, 4K nativo) entregava
       36 fps de chegada, o gate de 40 pausava, e a entrega final ficava em 38,6 apresentados
       contra 58,1 SEM FG nenhum (D:\claude-scratch\receita\TABELA.txt, linhas D e E).
       A raiz: ligar a fluidez custa +8,9 ms FIXOS na cadeia (medido duas vezes: 17,22->26,18 e
       22,60->31,46) porque o modo interpolado obriga janela oculta + apresentador + FI na
       MESMA GPU. Ou seja, a chegada COM o FG ligado NAO e o que a pessoa veria com ele
       desligado - comparar com ela e comparar com a coisa errada.

       O CRITERIO NOVO tem duas pernas, e as duas tem de passar:

       (1) ECONOMICA - "compensa?". Compara os APRESENTADOS POR SEGUNDO medidos contra a
           entrega que a pessoa teria SEM FG. Essa entrega nao e a chegada de agora: e
           1000 / (intervalo_de_agora - custo_do_proprio_FG). O custo do FG e MEDIDO, nao
           chutado, de dois jeitos:
             - enquanto nunca houve pausa: a soma do que o FG gasta na GPU (FI + passes+VME),
               que o proprio interpolador cronometra (estimativa CONSERVADORA - ela ignora a
               travessia, entao subestima um pouco o quanto o FG custa);
             - DEPOIS da primeira pausa: a pausa E a prova ao vivo. Com o FG pausado o FI para
               de ser despachado e a cadeia acelera sozinha; 1 s depois a chegada medida E a
               chegada "sem FG", e a diferenca de intervalo E o custo do FG. Mesmo espirito da
               prova ao vivo da guarda do Codec (main.cpp) - a medida sintetica mente.
           Pausa quando o ganho fica < 1,00 por 3 s; volta quando o ganho PREVISTO (2 quadros
           por chegada, descontado o custo do FG) passa da margem por 2 s. A margem sobe 0,10 a
           cada pausa economica (teto 1,50): se ja se provou que nao compensava, exige-se mais
           para tentar de novo. Isso e a histerese que impede o pisca-pisca.

       (2) MATERIAL - "da para interpolar isto?". Com os quadros reais longe demais no tempo,
           o quadro do meio seria invencao demais.
           ATE 13/09/2026 este piso era 30 fps de chegada, e a justificativa era honesta mas
           ERRADA: "e o espacamento em que o FI foi medido" - os tripletos do Hogwarts foram
           gravados a 60 fps com A=n e B=n+2 = 33,3 ms. So que isso nao era o limite do FI: era
           o limite da MEDICAO. E o preco de manter um piso por FALTA de medida foi medido e e
           enorme: DESLIGAVA a Fluidez nas duas topologias em que o produto vive (duas placas no
           padrao, chegada 15,8 -> 16,5 presents/s com `gerados` congelado; uma placa no padrao
           de fabrica, chegada 21,6 -> idem), onde com o piso fora do caminho a mesma bancada
           entrega 26,4 e 43,9 presents/s e o proprio gate mede 1,93x (D:\claude-scratch\over\
           RESULTADOS.txt, 13/09 18:05).

           A MEDIDA QUE FALTAVA FOI FEITA (13/09/2026, D:\claude-scratch\taxa\TABELA.txt):
           os mesmos tripletos a 60 fps sao decimados de K em K (A=n, B=n+K), o quadro REAL do
           meio e a verdade de terra, e o gerado e comparado com o PISO HONESTO - "repetir o
           quadro anterior", que e literalmente o que a tela mostra sem FG. 36 combinacoes:
           3 cenas x 2 modos de campo (MV=0 = duas placas, e o campo local) x 6 taxas de
           chegada (30, 20, 15, 12, 10 e 7,5 fps), metricas pelo metodo padrao do projeto
           (PSNR + SSIM + acutancia, faixa do topo excluida - armadilha 47).
           RESULTADO: o gerado GANHA do "repetir" em TODAS as 36 combinacoes. O ganho de PSNR
           nao desaba (pan +1,6 a +2,2 dB; combate +3,0 a +7,2; menu +1,8 a +5,9) e a vantagem
           de SSIM CRESCE conforme a chegada cai (pan MV=0: +0,018 a 30 fps -> +0,051 a 7,5),
           porque "repetir" piora mais rapido que interpolar quando os quadros se afastam.
           NAO HA CRUZAMENTO dentro da faixa medida - nem a 7,5 fps.

           => O PISO PASSA A SER 10 fps de chegada (volta em 13), e a margem e DECLARADA: o
           ponto mais baixo medido e 7,5 fps e o piso fica 33% acima dele, para o app nunca
           rodar em terreno sem medida. Abaixo de 10 ninguem mediu - ai sim o programa para.
           O que NAO melhora com a chegada baixa e a LATENCIA (meio intervalo: 50 ms a 10 fps)
           nem a nitidez do quadro inventado (acutancia do gerado 36,23 -> 36,68 em pan
           enquanto um quadro real fica em 36,11) - por isso o painel AVISA nessa faixa em vez
           de bloquear em silencio (main.cpp, a linha viva da Fluidez).
           DLSS5_FG_MIN_FPS continua rebaixando o piso (BANCADA).

       O MOTIVO de cada pausa vai para o log E para o painel - o gate nunca mais pausa em
       silencio "por chegada baixa" quando a razao foi outra. */
    /* X-HUD-PISCA: GATE_VRAM = o main mediu memoria de video acima do orcamento e pediu a pausa (o
       contexto do FI e solto nesta thread); GATE_MORTO = o interpolador morreu (device removido /
       fila parada) - pausa que nao volta nesta sessao. */
    enum MotivoGate { GATE_OK = 0, GATE_NAO_COMPENSA = 1, GATE_MATERIAL = 2, GATE_VRAM = 3, GATE_MORTO = 4 };
public:
    /* Chamado pelo main (thread do laco principal) quando a memoria de video passa do orcamento
       e quando volta a haver folga. A histerese e do main (ver a sonda de VRAM em main.cpp). */
    void PausarPorVram(bool p) { m_vramPausa.store(p); }
    /* FG-BASE (20/09/2026): A UNICA COISA QUE AINDA BAIXA O N. A memoria de video acima do
       orcamento e seguranca (o defeito 153: a placa reinicia), nao opiniao sobre o que vale a
       pena - e o 2o contexto do FI sao ~290 MB de volta na hora. E NAO vale mais a sessao: quando
       a VRAM libera, o main solta e o N volta a ser o que a pessoa escolheu. */
    void DerrubarPara2x(bool derruba = true) {
        if (derruba == m_multQueda) return;
        m_multQueda = derruba;
        if (derruba) m_multCaiu.store(true);
        if (m_log) m_log("apresentador: %s (memoria de video)", derruba ? "N derrubado para 2x" : "N de volta ao escolhido");
    }
private:
    /* O que RETOMAR renderia, contra a chegada sem FG: 2 quadros por chegada descontado o custo
       do FG, e NUNCA acima do teto de apresentacao medido (m_apresMax, ver Gate()). */
    double GanhoPrevisto(double tOn, double cUse) const {
        if (tOn <= 0.0 || tOn + cUse <= 0.0) return 0.0;
        double apres = (double)m_multEf * 1000.0 / (tOn + cUse);   /* X-FG-6: K quadros por chegada (2 em 2x) */
        if (m_apresMax > 0.0 && apres > m_apresMax) apres = m_apresMax;
        if (m_refreshHz > 0.0 && apres > m_refreshHz) apres = m_refreshHz;   // X-HUD-PISCA: a tela e teto
        return apres * tOn / 1000.0;
    }
    static constexpr double kMenuChegadaAlta = 45.0;   // ver a pausa de MENU, mais abaixo
    const char* NomeMotivo(int m) const {
        return m == GATE_NAO_COMPENSA ? "nao compensa" : m == GATE_MATERIAL ? "chegada abaixo do piso medido"
             : m == GATE_VRAM ? "memoria de video acima do orcamento" : m == GATE_MORTO ? "interpolador morto (placa reiniciou)"
             : "-";
    }
    void Gate(double fps, double agora, FrameGen::Interpolador* fg) {
        if (m_pisoFps < 0.0) {   // uma vez por sessao
            wchar_t e[16]{};
            m_pisoFps = 10.0; m_pisoVolta = 13.0;   // MEDIDO ate 7,5 fps + 33% de margem - ver o bloco acima
            if (GetEnvironmentVariableW(L"DLSS5_FG_MIN_FPS", e, 16) > 0 && _wtof(e) > 0.0) {
                m_pisoFps = _wtof(e); m_pisoVolta = m_pisoFps + 3.0;
                if (m_log) m_log("apresentador: BANCADA - piso de material rebaixado por DLSS5_FG_MIN_FPS: pausa < %.0f fps de chegada, volta >= %.0f", m_pisoFps, m_pisoVolta);
            }
            wchar_t g[16]{};
            if (GetEnvironmentVariableW(L"DLSS5_FG_GATE", g, 16) > 0 && (g[0] == L'f' || g[0] == L'F')) {
                m_gateFixo = true; m_pisoFps = 40.0; m_pisoVolta = 45.0;
                if (m_log) m_log("apresentador: BANCADA - DLSS5_FG_GATE=fixo: de volta ao gate antigo (pausa < 40 fps de chegada, volta >= 45), sem a perna economica");
            }
            if (m_log && !m_gateFixo)
                m_log("apresentador: gate do FG = 2 pernas - (1) compensa? apresentados/s contra a entrega estimada SEM FG; (2) material: chegada >= %.0f fps (medido: o quadro gerado ganha de 'repetir o anterior' em 36/36 combinacoes ate 7,5 fps de chegada; o piso fica 33%% acima do ponto mais baixo medido)", m_pisoFps);
        }
        /* X-HUD-PISCA: MORTO nao volta; VRAM e decisao do main (pausa na hora, volta quando ele solta) -
           as duas passam por cima das pernas de sempre e nao mexem na margem nem no recuo. */
        if (fg && fg->Morto()) {
            if (!m_pausada || m_motivo != GATE_MORTO) { m_pausada = true; m_motivo = GATE_MORTO; m_pausas++; m_baixaDesde = 0.0; m_altaDesde = 0.0; if (m_log) m_log("apresentador: FG EM PAUSA - interpolador morto (device removido ou fila parada); nao volta nesta sessao"); }
            m_pausadaAt.store(true); m_motivoAt.store(GATE_MORTO); return;
        }
        if (m_vramPausa.load()) {
            if (!m_pausada || m_motivo != GATE_VRAM) { m_pausada = true; m_motivo = GATE_VRAM; m_pausas++; m_baixaDesde = 0.0; m_altaDesde = 0.0; m_pausadaDesde = 0.0; if (m_log) m_log("apresentador: FG EM PAUSA - memoria de video acima do orcamento (pedido do main); o contexto do FI e solto"); }
            m_pausadaAt.store(true); m_motivoAt.store(GATE_VRAM); return;
        }
        if (m_pausada && m_motivo == GATE_VRAM) {
            m_pausada = false; m_motivo = GATE_OK; m_baixaDesde = 0.0; m_altaDesde = 0.0;
            if (m_log) m_log("apresentador: FG VOLTA - a memoria de video voltou a ter folga (o main soltou a pausa)");
        }
        /* FG-BASE (20/09/2026, ordem do dono): NADA aqui olha para o custo do FG contra o jogo.
           "Se eu ativar o frame gen eu quero que ele ligue mesmo se for burrice"; "nao vai ter modo
           Auto". O N e da pessoa, o FG liga e fica, e quem conta a verdade e o contador na tela
           (os reais caindo estao la, em numero) e a linha `fg base:` do log. */
        if (m_dtN < 8) { m_pausadaAt.store(m_pausada); return; }
        /* FONTE PARADA: nao ha o que julgar - `fps` e a chegada de ANTES do vao (a mediana nao
           conta vaos) e os apresentados/s sao os da fonte parada. Nem pausa, nem volta, nem
           relogio correndo: o gate congela como esta ate a chegada voltar (ver LacoInterpolado). */
        if (m_fonteParada) { m_baixaDesde = 0.0; m_altaDesde = 0.0; m_motivoPend = GATE_OK; m_multCustoDesde = 0.0; /* X-TDR: a queda de 3x/4x tambem nao acumula */ m_pausadaAt.store(m_pausada); return; }
        const double tOn = fps > 0.0 ? 1000.0 / fps : 0.0;

        // ---- custo do proprio FG na cadeia: medido na pausa; antes dela, o que o FG gasta na GPU
        // BANCADA - DLSS5_FG_CUSTO_MS=<n>: soma milissegundos fantasmas AO QUE O GATE VE (nada
        // muda no que o FG faz). E o unico jeito de exercitar a perna economica nesta maquina,
        // onde o FG ganha com folga; mesmo recurso da guarda do Codec (DLSS5_PONTE_CODEC_TESTE_MS).
        if (m_custoFantasma < 0.0) {
            wchar_t e[16]{}; m_custoFantasma = 0.0;
            if (GetEnvironmentVariableW(L"DLSS5_FG_CUSTO_MS", e, 16) > 0 && _wtof(e) > 0.0) {
                m_custoFantasma = _wtof(e);
                if (m_log) m_log("apresentador: BANCADA - DLSS5_FG_CUSTO_MS=%.1f: o gate vai somar %.1f ms ao custo do FG (o FG nao muda)", m_custoFantasma, m_custoFantasma);
            }
        }
        const double custoGpu = (fg ? (fg->FiMs() + fg->PassesMs() + fg->VmeMs()) : 0.0) + m_custoFantasma;
        const double custo = m_custoFgMs > 0.0 ? m_custoFgMs + m_custoFantasma : custoGpu;

        /* FG-BASE (20/09/2026): O "%dx NAO CABE" (X-FG-6) SAIU. Ele derrubava 3x/4x para 2x pela
           sessao quando o tempo de GPU do FI passava de 80% do intervalo, ou quando os
           apresentados/s ficavam abaixo de (K - 0,5) chegadas. Os dois sao verdade - e nenhum dos
           dois e motivo para desfazer a escolha da pessoa: se a placa nao da conta, o contador
           mostra os reais caindo; se a tela nao apresenta tantos, a ADMISSAO descarta o excesso,
           que e o trabalho dela. O unico que ainda baixa o N e a memoria de video acima do
           orcamento (DerrubarPara2x), porque ali o risco e a placa reiniciar - e mesmo esse volta
           ao N escolhido quando a VRAM libera. */
        (void)custoGpu; (void)agora;   // sem relogio de queda nem de pausa economica, ninguem mais os le

        /* ============ FG-BASE (20/09/2026): O GATE NAO JULGA MAIS SE "COMPENSA" ============
           No log do dono: `FG EM PAUSA - NAO COMPENSA: 9.0 apresentados/s contra 6.6 fps` e o
           cartaz "FLUIDEZ INTERPOLADA EM PAUSA" - com ele tendo escolhido o multiplicador a mao,
           minutos antes. A ordem e "se eu ativar o frame gen eu quero que ele ligue mesmo se for
           burrice": quem decide se vale a pena e a pessoa, olhando o contador (os reais caindo
           estao la, em numero). Entao sairam as DUAS pernas de julgamento - a economica ("nao
           compensa") e a de material (chegada abaixo do piso medido). Ficam so as pausas de
           SEGURANCA, que nao sao opiniao sobre o que vale a pena e ja passaram acima, com
           `return` proprio: memoria de video acima do orcamento, interpolador morto e fonte
           parada. O que era a conta da perna economica continua sendo MEDIDO - vira a linha
           `GATE` do log de 5 s, informacao para quem le, nunca decisao.

           O QUE SAIU JUNTO, por nao ter mais dono: a prova ao vivo (ela media o custo do FG na
           pausa economica, que nao existe mais - quem informa o custo agora e o tempo de GPU do
           proprio FI, ver BaseJogo::Reavaliar no main), a margem crescente e o recuo de volta de
           5/15/60 s (histerese de uma pausa que nao acontece mais). */
        if (m_menu) {
            /* PAUSA DE MENU ATIVA: so o quadro real sai, POR DECISAO NOSSA (cena parada). Os
               apresentados/s caem para a chegada e a perna economica lia isso como "nao
               compensa" - pausava, projetava 2x, voltava, o menu segurava de novo, pausava...
               (RDR, 18/09/2026: `menu` subindo 80/s, `gerados` congelado, um cartaz a cada troca).
               Em menu a perna economica nao decide e nao acumula: nao ha o que julgar. */
            m_baixaDesde = 0.0; m_motivoPend = GATE_OK;
        } else {
            // ---- (2) material: piso absoluto, o unico que o gate antigo de fato protegia
            const bool ruimMaterial = fps < m_pisoFps;
            /* ---- (1) economia: apresentados/s medidos contra a entrega estimada sem FG.
               ATE 13/09/2026 a referencia era `m_offFps` - a chegada MEDIDA numa pausa - usada
               daquele instante em diante, para sempre. Isso e um LATCH: `m_offFps` congela um
               numero de UM instante e a cadeia muda depois dele (a pessoa mexe na regua da rede,
               na resolucao, no ampliador). Com a cadeia mais lenta que quando mediu, o gate
               comparava 30 apresentados contra 58 "sem FG" de outra epoca e pausava por "nao
               compensa" com o FG DOBRANDO a entrega. O que a prova ao vivo mede e transferivel
               NAO e a chegada: e o CUSTO (m_custoFgMs) - a chegada de agora ja esta no tOn.
               No INSTANTE da medida os dois caminhos dao o mesmo numero por construcao
               (custo = m_tOnAntes - tOn_off e tOn = m_tOnAntes => 1000/(tOn-custo) = m_offFps),
               entao isto nao muda nada onde o antigo estava certo e destrava o resto. */
            double refOff = 0.0;
            if (tOn > custo + 1.0) refOff = 1000.0 / (tOn - custo);
            // FONTE DE TAXA FIXA (placa de captura): sem FG a cadeia nao entregaria mais do que a
            // fonte manda - a referencia fica presa a taxa dela (ver TaxaFonteFixa).
            if (m_fonteFps > 0.0 && refOff > m_fonteFps) refOff = m_fonteFps;
            // A TELA tambem e teto (X-HUD-PISCA): sem FG a pessoa nunca ve mais quadros por segundo
            // do que o monitor mostra. Com a chegada agora medida na cadeia (COD a 120-180 fps numa
            // tela de 100 Hz) a referencia "sem FG" passaria de 100 e o gate pausaria com a conta
            // errada - o FG ali nao compensa mesmo (2 x chegada > refresh), mas pelo motivo certo.
            if (m_refreshHz > 0.0 && refOff > m_refreshHz) refOff = m_refreshHz;
            // SEM REFERENCIA (intervalo da cadeia menor que o custo estimado do FG, ou ainda
            // sem apresentados contados) o gate NAO DECIDE - e nao zera o relogio que ja estava
            // correndo. Zerar era um defeito de verdade: a mediana da chegada balanca entre
            // quadros e bastava uma janela sem referencia para apagar 2,9 s de "esta ruim".
            /* O TETO DE APRESENTACAO MEDIDO (18/09/2026): o maior apresentados/s visto com o FG
               ativo (fora de menu), decaindo 0,5%/s para acompanhar uma cadeia que mudou. Continua
               sendo medido porque o log e o painel o mostram; nao trava mais nada. */
            if (m_apresFps > 0.0 && !m_apresSujo) m_apresMax = std::max(m_apresFps, m_apresMax * 0.995);   // (X-TDR: janela suja nao entra)
            /* FG-BASE: daqui para baixo so se GUARDA o que foi medido. `m_gateRef` e a entrega que
               a cadeia teria sem FG e `m_gateGanho` e a razao contra ela - os dois numeros que a
               perna economica usava para pausar, e que agora so aparecem na linha GATE do log.
               `ruimMaterial` (chegada abaixo do piso medido) idem: o log conta, ninguem pausa. */
            m_gateRef = refOff;
            m_gateGanho = (refOff > 0.0 && m_apresFps > 0.0) ? m_apresFps / refOff : 0.0;
            m_gateAbaixoPiso = ruimMaterial;
            m_baixaDesde = 0.0; m_motivoPend = GATE_OK;
        }
        m_pausadaAt.store(m_pausada);
        m_motivoAt.store(m_pausada ? m_motivo : GATE_OK);
    }

    /* `alvoMs` e o intervalo que ESTE present deveria ter em relacao ao anterior (meio
       intervalo de chegada, no ritmo 2x); 0 = sem alvo (B sozinho num corte/menu/pausa, ou o
       primeiro depois de um vao). E o que alimenta a conta de "apresentado fora de hora": o
       present cujo intervalo real fugiu do alvo mais que a tolerancia (25%, minimo 1 ms) -
       o mesmo espirito do animation error (PESQ-FG §3.5): cadencia uniforme de presents nao
       garante ritmo certo, mas ritmo errado sempre aparece aqui. */
    void ApresentarTex(ID3D11Texture2D* t, bool gerado, double alvoMs = 0.0) {
        // X-TDR: UMA espera por Present, com teto (ver EsperarLugar). Sem lugar, este quadro e
        // pulado - a telemetria por segundo (abaixo) continua saindo, para o log nao emudecer
        // justamente quando a placa trava.
        // RODADA3: o teto de apresentacao (ver TetoApresentacao). Antes do EsperarLugar: o
        // gerado descartado nao toma lugar no swapchain; o real adiado espera fora dele.
        {
            const double teto = m_tetoApres.load(std::memory_order_relaxed);
            if (teto > 0.0 && m_ultPresentMs > 0.0) {
                const double minMs = 1000.0 / teto;
                const double alvo = m_ultPresentMs + 0.95 * minMs;
                if (Ms() < alvo) {
                    if (gerado) { m_tetoDescartados.fetch_add(1, std::memory_order_relaxed); return; }
                    m_tetoAdiados.fetch_add(1, std::memory_order_relaxed);
                    for (double r = alvo - Ms(); r > 0.0 && r <= minMs; r = alvo - Ms()) {
                        if (r > 1.5) Sleep(1); else YieldProcessor();
                    }
                }
            }
        }
        const bool lugar = EsperarLugar(kPresentMaxMs);
        const double antes = Ms();
        HRESULT hr = S_OK;
        /* O1: no modo "Sem VSync" o present pede o rasgo. SyncInterval ja era 0 desde sempre
           (MAGPIE), entao o que muda de fato e o flag - e, em tela composta, so a latencia
           (ver o bloco do ALLOW_TEARING na criacao do swapchain). */
        const UINT fl = (m_sync.load(std::memory_order_relaxed) == SYNC_SEM && m_tearingOk) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        if (lugar) { Mostrar(t); { VigiaMarca vm(g_vigiaApresDesde); hr = m_sc->Present(0, fl); } m_ponteiro.DepoisDoPresent(); m_ultPresMs = Ms(); ConferirPresent(hr, "direto"); }
        // OCLUSAO (23/09/2026): a regua da tela, ANTES do Observar da sonda (que amostra de 8
        // em 8) - mesmo arranjo do FlipProvaLog da pelicula no main.cpp.
        if (lugar && m_regua.Amostrar(m_sc) && m_log) m_log("%s", m_regua.Linha("apresentador").c_str());
        if (lugar && m_iflip.Observar(m_sc) && m_log) m_log("%s", m_iflip.Linha().c_str());   // RODADA6
        if (lugar && m_log && m_flip.Observar(m_sc)) {
            m_log("%s", m_flip.Linha("apresentador", m_pMs).c_str());
            const std::string cima = m_flip.PorCima();
            if (!cima.empty())
                m_log("flip [apresentador]: VISIVEIS POR CIMA da nossa janela - %s. Cada uma obriga "
                      "o DWM a compor, e com composicao nao ha caminho direto nenhum.", cima.c_str());
        }
        const double agora = Ms();
        if (lugar) {
            if (m_ultPresentMs > 0.0 && m_nInt < 4096) m_intervalos[m_nInt++] = agora - m_ultPresentMs;
            if (alvoMs > 0.0 && m_ultPresentMs > 0.0) {
                // o intervalo e medido de present a present, ANTES do custo deste Present: e o que
                // o compositor ve como "hora em que o quadro entrou na fila"
                const double erro = (antes - m_ultPresentMs) - alvoMs;
                const double tol = std::max(1.0, 0.25 * alvoMs);
                if (gerado) { m_rG++; m_rErroG += fabs(erro); if (fabs(erro) > tol) m_rForaG++;
                              m_r5G++; if (fabs(erro) > tol) m_r5ForaG++;
                              if (m_r5N < (int)(sizeof(m_r5Err) / sizeof(m_r5Err[0]))) m_r5Err[m_r5N++] = fabs(erro); }
                else        { m_rB++; m_rErroB += fabs(erro); if (fabs(erro) > tol) m_rForaB++; }
            }
            /* ===== X-GERADO (20/09/2026): O QUE A TELA MOSTRA, medido AQUI DENTRO =====
               "presents/s" nunca foi o numero que interessa: o compositor mostra UM quadro por
               composicao, entao dois presents dentro da mesma composicao viram UMA imagem na tela
               e o segundo foi trabalho jogado fora. Era isso que a FG-PROVA §3 via de fora como
               "31,2 imagens distintas/s com 60 presents/s".
               O conteudo ja esta provado noutro lugar - `gerado: distinto de A / de B / da mistura`
               (fg_ffx.h, ProvaDespachar) compara os buffers na GPU e diz que G nao e copia de B.
               O que faltava era o RELOGIO: um present so vira imagem nova se caiu numa composicao
               diferente da do present anterior. O criterio usa so o PERIODO da tela, nunca a fase
               absoluta: a ancora do vblank e relida uma vez por segundo e a 240 Hz um erro de 0,1%
               no periodo ja desloca a fase de um vblank inteiro em um segundo - contar por fase
               velha mediria o nosso erro de relogio, nao a tela. */
            if (m_ultPresentMs > 0.0 && m_telaPeriodoMs > 0.0) {
                if (antes - m_ultPresentMs >= 0.9 * m_telaPeriodoMs) m_telaDist++;
            } else m_telaDist++;   // o primeiro present da sessao/janela conta
            m_ultPar = gerado || alvoMs > 0.0;   // G, ou o B de um par: o proximo G tem alvo; B sozinho (corte/menu/pausa) nao
            m_ultPresentMs = agora;
            m_accPresent += agora - antes;
            // `q`: quanto Mostrar + Present custam NESTA maquina - a margem com que a ancora do
            // vblank (C3) recua o prazo. Era 1,5 ms escolhido a mao.
            m_qMs = m_qMs > 0.0 ? 0.9 * m_qMs + 0.1 * (agora - antes) : (agora - antes);
            MedirP(agora);   // `p`: present -> scanout, pelo DWM (amostrado)
            if (gerado) m_gerados++;
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                if (!m_deviceMorreu.exchange(true) && m_log)
                    m_log("apresentador: Present devolveu 0x%08X (device removed; motivo 0x%08X)", (unsigned)hr, (unsigned)(m_dev ? m_dev->GetDeviceRemovedReason() : hr));
            }
            m_apresentados.fetch_add(1);
            ++m_medN; m_r5Pres += 1.0;
        }
        /* K-RITMO: a linha CURTA do ritmo, a cada 5 s - o que o dono le para julgar fluidez sem
           ter de atravessar as outras. "G fora de hora" e a mesma conta que o PresentMon faz
           sobre MsBetweenDisplayChange (fracao fora de +-25% do alvo); "erro mediano" e a
           MEDIANA do |erro| dos gerados (a media do RITMO esconde a cauda); "fila" e quantos
           quadros a chegada achou na fila, em media - o queue target do LS; "base" e a pedida
           ao jogo. Aceite: fora de hora <= 5%, erro mediano < 1,5 ms, fila ~1. */
        if (m_ultLog5Ms == 0.0) m_ultLog5Ms = agora;
        if (agora - m_ultLog5Ms >= 5000.0 && m_log) {
            std::sort(m_r5Err, m_r5Err + m_r5N);
            /* X-GERADO: a DISTRIBUICAO da chegada, para saber de quem e o tremor que `j` mede.
               `carimbo` = os instantes vem do SystemRelativeTime do quadro do WGC (o relogio da
               FONTE); `recepcao` = do relogio desta maquina. Com carimbo, p05/p95 longe da mediana
               quer dizer que quem entrega fora de hora e a captura/compositor, nao a nossa fila. */
            char cheg[128] = "";
            {
                double v[512]; int n = 0;
                AcquireSRWLockExclusive(&m_srcLock);
                n = m_carN; for (int i = 0; i < n; i++) v[i] = m_carDt[i];
                m_carN = 0;
                ReleaseSRWLockExclusive(&m_srcLock);
                if (n >= 8) {
                    std::sort(v, v + n);
                    snprintf(cheg, sizeof cheg, " | chegada(%s) p05 %.1f p50 %.1f p95 %.1f ms de %d",
                             m_carimboFonte.load(std::memory_order_relaxed) ? "carimbo" : "recepcao",
                             v[(int)(n * 0.05)], v[n / 2], v[(int)(n * 0.95)], n);
                }
            }
            /* X-GERADO: O QUE A TELA MOSTRA. `presents` e o que mandamos; `imagens distintas` e quantos
               desses cairam numa composicao diferente da do anterior - dois presents na mesma
               composicao viram UMA imagem e o segundo foi trabalho jogado fora. O aceite do frame
               gen mora aqui: com a fonte a 30 fps e 2x, tem de dar ~60/s, nao ~30/s. */
            const double presTela5 = m_r5Pres;   // O1-TELA: guardado antes de o bloco de baixo zerar
            char tela[152] = "";
            {
                const double seg = (agora - m_ultLog5Ms) / 1000.0;
                if (seg > 0.5)
                    snprintf(tela, sizeof tela, " | tela: presents %.0f/s, imagens distintas %.0f/s (composicao %.2f ms), present a %.1f ms do vblank",
                             m_r5Pres / seg, (double)m_telaDist / seg, m_telaPeriodoMs, m_telaFaseN ? m_telaFaseAcc / m_telaFaseN : 0.0);
                m_telaDist = 0; m_telaFaseAcc = 0.0; m_telaFaseN = 0; m_r5Pres = 0.0;
            }
            /* O1: `apres` diz em uma palavra o que a pessoa escolheu na apresentacao - o
               multiplicador que VALE (e de onde ele veio: escolha ou adaptativo) e a
               sincronizacao. Sem isso nao da para ler nenhuma das medidas abaixo, porque
               `tique`, `T` e `fora de hora` querem dizer coisas diferentes em 2x e em 4x. */
            char apres[96] = "";
            {
                const int sy = m_sync.load();
                char mt[24] = "";
                if (m_modoTela) snprintf(mt, sizeof mt, " (meta %.0f)", MetaFps());
                snprintf(apres, sizeof apres, " | apres %dx%s, %s%s", m_multEf,
                         mt,
                         sy == SYNC_SEM ? "sem VSync" : sy == SYNC_VRR ? "VRR" : "VSync",
                         sy == SYNC_VRR && !m_telaVrr.load() ? " (a tela nao esta em VRR: como VSync)" : "");
            }
            /* O1-TELA: a linha propria do modo. `razao` e quantos vblanks cabem num intervalo da
               fonte (o que o modo tem de fechar); `por intervalo` e o que ele de fato repartiu -
               os dois batem quando o modo esta dentro do teto do FI e divergem quando nao esta,
               que e como se enxerga o teto. `fracao` e o erro medio entre a fracao que cada
               vblank queria e a que o FI sabia dar: e o preco do modo, em fracao de intervalo. */
            /* META (21/09/2026): a linha propria da meta. `alvo` e a meta (Tela = refresh medido),
               `fonte` a cadeia crua, `R` a razao pedida e `entregue` a que sai (cortada pelo N
               que o custo permite), `gerados/int` o que o acumulador repartiu, `apresentados`
               os presents reais, `fora de hora` a mesma conta da linha de ritmo, `custo` o FI
               por gerado x gerados do par contra o intervalo da fonte. */
            char mtela[300] = "";
            if (m_modoTela) {
                const double seg5 = (agora - m_ultLog5Ms) / 1000.0;
                const double fonte = m_telaFonteMs > 0.0 ? 1000.0 / m_telaFonteMs : 0.0;
                const int g = std::max(1, m_multEf - 1);
                const char* motivo = !m_metaGerando ? "sem gerados (meta <= fonte)" : m_metaCapK < 8 && m_telaR > (double)m_metaCapK + 0.25 ? "custo" : m_telaR > (double)kTelaMax + 0.25 ? "FI" : "cabe";
                snprintf(mtela, sizeof mtela, " | tela-modo: alvo %.0f | fonte %.1f fps | R %.2f (entregue %.2f = %.0f/s) | gerados/int %.2f | apresentados %.0f/s | fora de hora %.1f%% | custo %.2f x %d = %.1f ms de %.1f (teto %dx: %s) | fracao %.3f",
                         MetaFps(), fonte, m_telaR, m_metaRef, m_metaEntregue,
                         m_telaInt ? (double)(m_telaVb - m_telaInt) / (double)m_telaInt : 0.0,
                         seg5 > 0.5 ? presTela5 / seg5 : 0.0,
                         m_r5G ? 100.0 * (double)m_r5ForaG / (double)m_r5G : 0.0,
                         m_metaCg, g, m_metaCg * g, m_telaFonteMs, m_metaCapK, motivo,
                         m_telaErrN ? m_telaErrF / (double)m_telaErrN : 0.0);
                m_telaErrF = 0.0; m_telaErrN = 0; m_telaVb = 0; m_telaInt = 0;
            }
            m_log("apresentador ritmo: G fora de hora %.1f%% | erro mediano %.2f ms | fila %.1f | base %.0f Hz (tela %.0f Hz%s) | fase %.2f ms, pulos %llu | prof %.2f ms | T %.2f c %.2f p %.2f%s j %.2f D %.2f ms (q %.2f, limiar %.2f) | gerar %.2f ms | repetidos descartados %llu (%llu quadros na fila)%s%s%s%s",
                  m_r5G ? 100.0 * (double)m_r5ForaG / (double)m_r5G : 0.0,
                  m_r5N ? m_r5Err[m_r5N / 2] : 0.0,
                  m_r5FilaN ? m_r5Fila / (double)m_r5FilaN : 0.0,
                  m_baseFps, m_telaHz, m_tiqueComens ? ", tique em vblanks" : "",
                  m_faseN ? m_faseErroAcc / (double)m_faseN : 0.0, (unsigned long long)m_fasePulos,
                  m_profN ? m_profAcc / (double)m_profN : 0.0,
                  m_tMs, m_cMs, m_pMs, m_pStats == 1 ? " (stats)" : "", m_jMs, m_dMs, m_qMs, LimiarGerarMs(), m_gerCpuMs,
                  (unsigned long long)m_r5Rep, (unsigned long long)m_r5RepPend, apres, mtela, cheg, tela);
            m_r5Rep = m_r5RepPend = 0;
            m_r5G = m_r5ForaG = 0; m_r5N = 0; m_r5Fila = 0.0; m_r5FilaN = 0;
            m_faseErroAcc = 0.0; m_faseN = 0; m_fasePulos = 0; m_profAcc = 0.0; m_profN = 0;
            m_ultLog5Ms = agora;
        }
        // TELEMETRIA POR SEGUNDO: e o que diz se os limiares dos gates estao frouxos (quanto
        // cada gate zerou) e quantas pausas automaticas houve.
        if (m_ultLogMs == 0.0) m_ultLogMs = agora;
        if (agora - m_ultLogMs >= 1000.0) {
            m_apresFps = m_medN * 1000.0 / (agora - m_ultLogMs);   // o que o gate compara
            /* X-TDR: a janela e SUJA quando teve vao (fonte parada), present pulado por falta de lugar ou
               um intervalo acima de 250 ms - os apresentados/s dela nao dizem nada sobre a placa nem
               sobre a tela, e a queda de 3x/4x (Gate) nao pode julgar por eles. No COD do dono o 3x
               caiu "pela sessao" com 0,7 apresentados/s medidos numa FONTE PARADA de 4,9 s. */
            double pior = 0.0; for (int i = 0; i < m_nInt; i++) pior = std::max(pior, m_intervalos[i]);
            m_apresSujo = m_fonteParada || m_janelaVao || m_presPulados > 0 || pior > 250.0;
            m_janelaVao = false;
        }
        if (agora - m_ultLogMs >= 1000.0 && m_log) {
            std::sort(m_intervalos, m_intervalos + m_nInt);
            const double med = m_nInt ? m_intervalos[m_nInt / 2] : 0, p95 = m_nInt ? m_intervalos[(int)(m_nInt * 0.95)] : 0;
            FrameGen::Interpolador* fg = m_fg.load();
            const double mv = m_gMov ? (double)m_gMov : 1.0;
            // "fonte parada" = a ultima chegada veio depois de um vao (menu/pausa do jogo: a
            // captura nao entrega e a cadeia so re-apresenta a mesma imagem a cada 500 ms).
            // Os 2 presents/s dessa fase sao a fonte, nao o apresentador - ver LacoInterpolado.
            char parada[128] = "";
            if (m_fonteParada) snprintf(parada, sizeof parada, " | FONTE PARADA ha %.1f s (parou %llu vez(es); gate congelado)", (agora - m_fonteParadaDesde) / 1000.0, (unsigned long long)m_paradas);
            else if (m_paradas) snprintf(parada, sizeof parada, " | fonte parou %llu vez(es), %.1f s no total, %llu quadros apos vao", (unsigned long long)m_paradas, m_fonteParadaMs / 1000.0, (unsigned long long)m_vaos);
            // HUD (X-HUD-PISCA): quanto do quadro gerado foi copiado do quadro real por estar parado
            // ha N quadros (mira, minimapa, texto - e fundo parado tambem). Em jogo com camera em
            // movimento este numero e ~a area do HUD; em menu chega perto de 100%. 'desligado' = a
            // mascara nao subiu (formato sem escrita tipada em UAV, ou DLSS5_FG_HUD=0).
            char pausaTxt[80] = "";
            if (m_pausada) snprintf(pausaTxt, sizeof pausaTxt, " (EM PAUSA: %s)", NomeMotivo(m_motivo));
            char pul[64] = "";
            if (m_presPulados) snprintf(pul, sizeof pul, " | %llu presents PULADOS (sem lugar em %u ms: placa atrasada)", (unsigned long long)m_presPulados, (unsigned)kPresentMaxMs);
            // X-FG-QUALIDADE: "parados" e o que a mascara MEDIU (fracao da tela sem mudanca ha N quadros);
            // "protegido" e o que ela APLICOU (zero quando passou do teto: cena parada ou quadro repetido)
            char hud[160] = "";
            if (fg && fg->HudLigado()) snprintf(hud, sizeof hud, " | HUD protegido %.1f%% dos pixels (parados %.1f%%, >= %u quadros, teto %.0f%%), %.3f ms", 100.0 * fg->HudFrac(), 100.0 * fg->HudParadosFrac(), fg->HudQuadros(), 100.0 * fg->HudMaxFrac(), fg->HudMs());
            else if (fg) snprintf(hud, sizeof hud, " | HUD: mascara desligada");
            // X-FG-QUALIDADE: quadros repetidos da captura descartados nesta janela, a chegada crua da
            // cadeia (com eles) e a espera pela resposta da GPU do dedup (media/max; "sem resposta" =
            // tratado como distinto)
            char rep[200] = "";
            if (fg && fg->DedupLigado()) {
                const double crua = MedianaCadeiaCrua();
                const uint64_t desc = fg->DedupDesconhecidos();
                snprintf(rep, sizeof rep, " | repetidos %llu (cadeia crua %.1f fps) | dedup %.2f/%.2f ms%s",
                         (unsigned long long)m_repetidos, crua > 0.0 ? 1000.0 / crua : 0.0, fg->DedupEsperaMediaMs(), fg->DedupEsperaMaxMs(),
                         desc != m_dedupDescAntes ? " (SEM RESPOSTA em alguns: contados como distintos)" : "");
                m_dedupDescAntes = desc;
                fg->DedupZerarJanela();
            }
            m_repetidos = 0;
            // (as fracoes dos gates so fazem sentido quando houve bloco em movimento; com 0 blocos moventes
            //  saem como 0, e nao como "100%" de um denominador que nao existe)
            const bool houveMov = m_gMov > 0;
            /* X-GERADO (20/09/2026): A PROVA. O resto da linha diz quantos G sairam e em que hora;
               isto diz se o G e uma IMAGEM NOVA. `distinto de B 0%` = o gerado E o quadro real
               (era o que o gate de ambiguidade estava fazendo - FG-PROVA §3). Medido na GPU pelo
               mesmo passo do dedup, sem nenhuma espera (fg_ffx.h, ProvaDespachar). */
            char prova[144] = "";
            if (fg && fg->ProvaDistB() >= 0.0)
                snprintf(prova, sizeof prova, " | gerado: distinto de A %.1f%% / de B %.1f%% / da mistura (A+B)/2 %.1f%% (blocos)",
                         100.0 * fg->ProvaDistA(), 100.0 * fg->ProvaDistB(), 100.0 * fg->ProvaDistMistura());
            m_log("apresentador INTERPOLADO: %d presents/s (%llu gerados, %llu cortes, %llu menu) | intervalo mediana %.1f ms p95 %.1f | chegada %.1f fps | Present %.2f ms | FI %.2f ms, passes+VME %.2f ms | gates zeraram: variancia %.0f%% ambiguidade %.0f%% mascara %.0f%% dos %.0f%% blocos moventes%s | pausas %llu%s | pan %llu, mv0 %llu (so medido, nao aplicado), sem campo %llu | corte s/campo: %llu, %.3f ms, fracao %.3f%s%s%s%s",
                  m_medN, (unsigned long long)m_gerados, (unsigned long long)m_cortes, (unsigned long long)m_menus, med, p95, m_chegadaFps.load(), m_medN ? m_accPresent / m_medN : 0.0,
                  fg ? fg->FiMs() : 0.0, fg ? (fg->PassesMs() + fg->VmeMs()) : 0.0,
                  houveMov ? 100.0 * m_gFlat / mv : 0.0, houveMov ? 100.0 * m_gAmbig / mv : 0.0, houveMov ? 100.0 * m_gMask / mv : 0.0, m_gBlocos ? 100.0 * m_gMov / (double)m_gBlocos : 0.0,
                  prova,
                  (unsigned long long)m_pausas, pausaTxt,
                  (unsigned long long)(fg ? fg->Pans() : 0), (unsigned long long)(fg ? fg->Mv0s() : 0), (unsigned long long)(fg ? fg->SemCampo() : 0),
                  (unsigned long long)(fg ? fg->CortesSemCampo() : 0), fg ? fg->CorteMs() : 0.0, fg ? fg->CorteFrac() : 0.0, hud, rep, parada, pul);
            // RITMO: e a linha para o dono julgar se o FG esta "fluido" ou "tremido". Alvo = meio
            // intervalo de chegada. "fora de hora" = present cujo intervalo fugiu do alvo mais que
            // 25% (min. 1 ms). "B atrasado" = quanto o B saiu depois do prazo dele (mediana/p95).
            // "G esperou" = quanto o G foi segurado para cair no relogio. "despertador" = quanto o
            // timer acordou depois do pedido (deve ficar abaixo de 1 ms com o timer de alta
            // resolucao; +7 a +15 ms e o sintoma de timer sem resolucao).
            {
                std::sort(m_rAtrB, m_rAtrB + m_rAtrN);
                const double alvo = m_chegadaFps.load() > 0.0 ? 500.0 / m_chegadaFps.load() : 0.0;
                // "G encurtado" = quantas vezes (e quanto) a ancora do G recuou porque o quadro ja
                // esperava por nos (X-HUD-PISCA) - deve ser ~0 com a cadeia mais lenta que a tela
                // e subir quando a cadeia entrega mais do que o apresentador consegue mostrar.
                // X-FG-QUALIDADE (fila): o tique em vigor (preso a base pedida ao jogo, ou livre da mediana), quantas
                // chegadas acharam a fila cheia (2 reais: encurta 1%) ou vazia (alonga 1%), estouros de capacidade
                // (par de dois Gerar atras ainda na fila: saiu na hora) e recomecos da cadencia (vao/pausa)
                char fila[160] = "";
                if (m_fila > 0) snprintf(fila, sizeof fila, " | fila 1: tique %.2f ms (%s, ajuste %.2f), cheia %llu, vazia %llu, estouro %llu, recomeco %llu", m_fTique, m_fPreso ? "preso a base" : "da mediana", m_tiqueAj, (unsigned long long)m_fCheia, (unsigned long long)m_fVazia, (unsigned long long)m_fEstouro, (unsigned long long)m_fRecomeco);
                m_fCheia = m_fVazia = m_fEstouro = m_fRecomeco = 0;
                m_log("apresentador RITMO: alvo %.2f ms | fora de hora: G %llu de %llu, B %llu de %llu | erro medio G %.2f ms, B %.2f ms | B atrasado mediana %.2f p95 %.2f ms (%llu > 2 ms) | B adiantado pela chegada %llu | G esperou %.2f ms em media (%llu vezes) | G encurtado pela idade do quadro %.2f ms (%llu vezes) | despertador +%.2f ms em media%s%s",
                      alvo, (unsigned long long)m_rForaG, (unsigned long long)m_rG, (unsigned long long)m_rForaB, (unsigned long long)m_rB,
                      m_rG ? m_rErroG / m_rG : 0.0, m_rB ? m_rErroB / m_rB : 0.0,
                      m_rAtrN ? m_rAtrB[m_rAtrN / 2] : 0.0, m_rAtrN ? m_rAtrB[(int)(m_rAtrN * 0.95)] : 0.0, (unsigned long long)m_rAtr2,
                      (unsigned long long)m_rAdiant, m_rEspG ? m_rEspGMs / m_rEspG : 0.0, (unsigned long long)m_rEspG,
                      m_rIdadeN ? m_rIdadeMs / m_rIdadeN : 0.0, (unsigned long long)m_rIdadeN,
                      m_rWakeN ? m_rWake / m_rWakeN : 0.0, m_fonteFps > 0.0 ? " | fonte de taxa fixa" : "", fila);
                m_rG = m_rB = m_rForaG = m_rForaB = m_rAtr2 = m_rAdiant = m_rEspG = m_rWakeN = m_rIdadeN = 0;
                m_rErroG = m_rErroB = m_rEspGMs = m_rWake = m_rIdadeMs = 0.0; m_rAtrN = 0;
            }
            /* GATE: SO INFORMACAO (FG-BASE, 20/09/2026). Nenhuma pausa sai mais daqui por
               julgamento - esta linha existe para quem le o log poder dizer "o FG esta
               entregando X contra os Y que a cadeia daria sem ele". Sem "RETOMAR": nao ha
               pausa economica para retomar. */
            {
                const double tOn = m_chegadaFps.load() > 0.0 ? 1000.0 / m_chegadaFps.load() : 0.0;
                // O m_custoFantasma (BANCADA) entra aqui tambem, para o numero do log ser o
                // mesmo que a bancada esta simulando. Fora da bancada ele e 0 e nada muda.
                const double custo = (fg ? (fg->FiMs() + fg->PassesMs() + fg->VmeMs()) : 0.0)
                                     + (m_custoFantasma > 0.0 ? m_custoFantasma : 0.0);
                m_log("apresentador GATE (so informacao): apresentados/s %.1f contra %.1f fps sem FG (custo do FG %.2f ms de GPU, intervalo de chegada %.2f ms) = ganho %.2fx | teto de apresentacao medido %.0f/s | chegada %.1f fps%s | %s%s",
                      m_apresFps, m_gateRef, custo, tOn, m_gateGanho, m_apresMax, m_chegadaFps.load(),
                      m_gateAbaixoPiso ? " (ABAIXO do piso medido de 10 fps: o quadro gerado entra em terreno sem medida - e a pessoa quem decide)" : "",
                      m_pausada ? NomeMotivo(m_motivo) : "ativo",
                      m_fonteParada ? " (fonte parada: nada a julgar)" : m_menu ? " (menu: so o quadro real sai)" : "");
            }
            m_medN = 0; m_nInt = 0; m_accPresent = 0; m_ultLogMs = agora; m_presPulados = 0;
            m_gFlat = m_gAmbig = m_gMask = m_gMov = m_gBlocos = 0;
        }
    }
    /* ======================= RODADA8: FG COM SAIDA FIXA 2x (ver FgSaida2x) ======================= */
    struct F2Pend {
        ID3D11Texture2D* t = nullptr; bool gerado = false; bool temIdx = false; long long idx = 0; long long idxLimite = 0;
        double soltaFixa = 0.0; double car = 0.0; uint64_t ger = 0; uint64_t liberarN = 0; uint64_t n = 0; bool segurado = false;
    };
    static constexpr int kF2Max = 8;
    // A hora de soltar o Present(1,0) do vblank `idx` da grade absoluta (a mesma da cadencia): lead*P antes dele em iFlip,
    // (0,5 - fase da composicao)*P antes no composto. Sem indice (sem ancora), a hora fixa guardada.
    double F2Solta(const F2Pend& pd) const {
        if (!pd.temIdx || m_cadP <= 0.0) return pd.soltaFixa;
        return m_cadVb + ((double)(pd.idx - m_cadBase) - CadAvanco()) * m_cadP;
    }
    double F2SoltaIdx(long long idx) const { return m_cadVb + ((double)(idx - m_cadBase) - CadAvanco()) * m_cadP; }
    void F2Push(const F2Pend& pd) {
        if (m_f2N >= kF2Max) { F2ApresentarFrente(true); }   // nunca acontece (<= 2 pares vivos); garantia: o da frente sai ja
        m_f2[m_f2N++] = pd;
    }
    // Tira o da frente e o apresenta. `agoraMesmo` = sem esperar a hora (pausa/saida/garantia).
    void F2ApresentarFrente(bool agoraMesmo) {
        if (m_f2N <= 0) return;
        const F2Pend pd = m_f2[0];
        for (int i = 1; i < m_f2N; i++) m_f2[i - 1] = m_f2[i];
        m_f2N--;
        const double agora = Ms();
        const double P = m_cadP;
        if (pd.gerado) {
            // o gerado so vale ANTES do vblank do real que vem depois dele: passou da hora dele por mais de meio refresh,
            // ele cairia no vblank do real (ou depois) - descartado, contado a parte
            if (agoraMesmo || (pd.temIdx && P > 0.0 && agora > F2Solta(pd) + 0.5 * P)) {
                m_f2GerDescJan++; m_f2GerTardeJan++; m_f2GerDescTot.fetch_add(1, std::memory_order_relaxed);
                if (!agoraMesmo) { m_f2AtrT[m_f2AtrPos] = agora; m_f2AtrPos = (m_f2AtrPos + 1) % 64; }
                return;
            }
        } else if (pd.temIdx && !agoraMesmo && agora > F2Solta(pd) + 0.25) {
            // real atrasado: sai JA, no primeiro vblank possivel (Present(1)) - nunca pulado
            m_cadAtrasados.fetch_add(1, std::memory_order_relaxed); m_f2RealAtrTot.fetch_add(1, std::memory_order_relaxed); m_f2RealAtrJan++;
            m_f2AtrT[m_f2AtrPos] = agora; m_f2AtrPos = (m_f2AtrPos + 1) % 64;
        }
        // lugar no swapchain: o real ESPERA (fatias de 50 ms ate 2 s - o prazo do TDR, como no caminho sem FG); o gerado
        // so espera ate meio refresh (depois disso ele ja nao cabe antes do real)
        bool lugar = EsperarLugar(pd.gerado ? (DWORD)std::max(1.0, 0.5 * (P > 0.0 ? P : 4.0)) : (DWORD)kPresentMaxMs);
        if (!lugar && !pd.gerado) {
            m_semLugarEsperas.fetch_add(1, std::memory_order_relaxed);
            int k = 0; bool avisou = false;
            for (; !lugar && k < 40 && !m_sair.load(); k++) {
                lugar = EsperarLugar(kPresentMaxMs);
                if (!lugar && k == 1 && !avisou) { avisou = true; m_semLugarLongas.fetch_add(1, std::memory_order_relaxed); if (m_log) m_log("fg2x: AVISO - quadro real %llu esperando lugar no swapchain ha ~100 ms (ADIADO, nao descartado)", (unsigned long long)pd.n); }
            }
            const double espMs = 50.0 * (k + 1); if (espMs > m_semLugarPiorMs) m_semLugarPiorMs = espMs;
            if (!lugar) { m_cadSemLugar.fetch_add(1, std::memory_order_relaxed); if (m_log) m_log("fg2x: AVISO - quadro real %llu DESCARTADO: o swapchain ficou sem lugar por ~%.0f ms (placa parada? saida?)", (unsigned long long)pd.n, espMs); }
        }
        if (!lugar) {
            if (pd.gerado) { m_f2GerDescJan++; m_f2GerTardeJan++; m_f2GerDescTot.fetch_add(1, std::memory_order_relaxed); }
            if (pd.liberarN) { if (Ponte* pp = m_ponte.load()) pp->Liberar(pd.liberarN); }
            return;
        }
        Mostrar(pd.t);
        const int sm = m_sync.load(std::memory_order_relaxed);
        const bool semVblank = (sm == SYNC_SEM) || (sm == SYNC_VRR && m_telaVrr.load(std::memory_order_relaxed));
        const UINT fl = (sm == SYNC_SEM && m_tearingOk) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        HRESULT hr;
        { VigiaMarca vm(g_vigiaApresDesde); hr = m_sc->Present(semVblank ? 0u : 1u, fl); }
        m_ponteiro.DepoisDoPresent();   // RESET-CURSOR
        ConferirPresent(hr, "interpolado");   // NS-2309 item 5(c)
        m_ultPresMs = Ms();
        const double tP = Ms();
        if (pd.liberarN) { if (Ponte* pp = m_ponte.load()) pp->Liberar(pd.liberarN); }
        m_apresentados.fetch_add(1); m_f2PresSeg++; m_f2PresJan++;
        m_cadPresJanN++; if (pd.segurado) { m_cadSegJanN++; m_cadEsperouJan = true; m_f2SegJan++; }
        // a regua do relogio por present (o proximo vblank depois do Present, na grade absoluta)
        if (P > 0.0 && m_cadVb > 0.0) {
            const long long idxTela = (long long)floor(CadG(tP)) + 1;
            if (m_f2TemTela) { const long long d = idxTela - m_f2UltIdxTela; m_f2HTela[d < 0 ? 0 : d > 15 ? 15 : d]++; }
            m_f2UltIdxTela = idxTela; m_f2TemTela = true;
            if (!pd.gerado && pd.car > 0.0) {
                if (m_f2TemTelaReal && m_f2UltCarTela > 0.0) {
                    const double dc = pd.car - m_f2UltCarTela;
                    if (dc > 0.0 && dc < 250.0) {
                        const long long ds = (long long)floor(dc / P + 0.5), dt = idxTela - m_f2UltIdxTelaReal;
                        m_f2HFonte[ds < 0 ? 0 : ds > 15 ? 15 : ds]++; m_f2HReal[dt < 0 ? 0 : dt > 15 ? 15 : dt]++;
                        m_f2CmpN++; if (ds == dt) m_f2Casou++;
                    }
                }
                m_f2UltIdxTelaReal = idxTela; m_f2UltCarTela = pd.car; m_f2TemTelaReal = true;
            }
        }
        if (pd.gerado) { m_f2GerJan++; m_f2GerTelaTot.fetch_add(1, std::memory_order_relaxed); m_gerados++; }
        else {
            m_f2RealJan++;
            m_cadTelaNovos.fetch_add(1, std::memory_order_relaxed);   // a linha `quadros:` do main: tela = reais apresentados
            if (pd.car > 0.0) { const double lat = tP - pd.car; if (lat > -100.0 && lat < 1000.0 && m_f2LatN < 4096) m_f2Lat[m_f2LatN++] = lat; }
        }
        if (m_regua.Amostrar(m_sc) && m_log) m_log("%s", m_regua.Linha("apresentador").c_str());
        if (m_iflip.Observar(m_sc) && m_log) { m_log("%s", m_iflip.Linha().c_str()); CadIflipJanela(); }
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            if (!m_deviceMorreu.exchange(true) && m_log)
                m_log("apresentador: Present devolveu 0x%08X (device removed; motivo 0x%08X)", (unsigned)hr, (unsigned)(m_dev ? m_dev->GetDeviceRemovedReason() : hr));
        }
        F2Log(tP);
    }
    // Pausa / troca de modo / interpolador desligado: os reais pendentes saem agora, em ordem; os gerados sao descartados.
    void F2Esvaziar(const char* motivo) {
        if (m_f2N <= 0) return;
        int g = 0, r = 0; for (int i = 0; i < m_f2N; i++) { if (m_f2[i].gerado) g++; else r++; }
        while (m_f2N > 0) F2ApresentarFrente(true);
        m_f2TemAnt = false;
        if (m_log) m_log("fg2x: fila esvaziada (%s) - %d real(is) apresentado(s) na hora, %d gerado(s) descartado(s)", motivo, r, g);
    }
    // K (em refreshes) do REAL: o gerado vai h vblanks antes dele (h = metade tipica do intervalo da fonte), entao o que
    // tem de caber e o caminho carimbo -> G pronto na GPU. K = h + ceil((p90 + fase da fonte + avanco) / P). Sobe na hora
    // (pelo p90 ou por atrasados > 1% em 2 s); desce 1 depois de 10 s sem nenhum atrasado - a regra do K sem FG.
    void F2AtualizarK(double agora, double P) {
        if (m_f2KOk && agora - m_f2KRecalc < 250.0) return;
        m_f2KRecalc = agora;
        // h: metade (piso) da mediana dos intervalos da fonte em vblanks
        {
            int v[64]; int nv = 0; const int tot = std::min(m_f2GapN, 64);
            for (int i = 0; i < tot; i++) v[nv++] = m_f2Gap[i];
            if (nv >= 4) { std::sort(v, v + nv); m_f2H = std::max(1, v[nv / 2] / 2); }
        }
        double v[512]; int n = 0;
        for (int i = 0; i < 512; i++) if (m_f2PathT[i] > 0.0 && agora - m_f2PathT[i] <= 2000.0) v[n++] = m_f2PathV[i];
        const int teto = m_f2H + 4;
        if (n < 8) { if (!m_f2KOk) { m_f2K = m_f2H + 2; m_f2KOk = true; } return; }
        std::sort(v, v + n);
        m_f2P90 = v[std::min(n - 1, (int)(n * 0.90))];
        int atr = 0; for (int i = 0; i < 64; i++) if (m_f2AtrT[i] > 0.0 && agora - m_f2AtrT[i] <= 2000.0) atr++;
        int kG = (int)ceil((m_f2P90 + m_cadPhiS * P + CadAvanco() * P) / P - 1e-6);
        if (kG < -3) kG = -3;
        int k = kG + m_f2H;
        // os primeiros 5 s (montagem da rede, Present preso na abertura: STALLs de 0,5-1,4 s medidos ate t+3 s) nao sobem K por atraso: o transitorio da abertura
        // (medido na a1: K 3 -> 6 no primeiro segundo) custava ~30 s de latencia a mais ate a descida devolver
        const bool porAtraso = atr >= 2 && atr * 100 > n && k <= m_f2K && m_f2IniMs > 0.0 && agora - m_f2IniMs > 5000.0;
        if (porAtraso) k = m_f2K + 1;
        // o DXGI viu vblank perdido com o lead no teto (CadIflipJanela): a GPU nao fecha o par a tempo neste K
        const bool porVblank = m_f2SubirK && k <= m_f2K;
        if (porVblank) k = m_f2K + 1;
        m_f2SubirK = false;
        if (k > teto) k = teto;
        const int antes = m_f2K;
        if (k > m_f2K) {
            // subiu logo depois de uma descida (<= 5 s): o degrau de baixo nao se sustenta - a proxima descida espera o dobro
            if (m_f2KDesceuMs > 0.0 && agora - m_f2KDesceuMs <= 5000.0) m_f2KEsperaMs = std::min(80000.0, 2.0 * m_f2KEsperaMs);
            m_f2K = k; m_f2KBaixaDesde = 0.0;
        }
        else if (k < m_f2K) {
            int atr10 = 0; for (int i = 0; i < 64; i++) if (m_f2AtrT[i] > 0.0 && agora - m_f2AtrT[i] <= m_f2KEsperaMs) atr10++;
            if (atr10 > 0 || m_f2KBaixaDesde <= 0.0) m_f2KBaixaDesde = agora;
            else if (agora - m_f2KBaixaDesde >= m_f2KEsperaMs) { m_f2K--; m_f2KBaixaDesde = agora; m_f2KDesceuMs = agora; }
        } else m_f2KBaixaDesde = 0.0;
        if (antes != m_f2K && m_log)
            m_log("fg2x: K %d -> %d refreshes (%.2f ms) | h %d (metade do intervalo da fonte) | p90 carimbo->G pronto %.2f ms em %d quadros | atrasados %d em 2 s%s | fase da fonte %.2f P, avanco %.2f P (%s) | proxima descida depois de %.0f s limpos",
                  antes, m_f2K, m_f2K * P, m_f2H, m_f2P90, n, atr, porAtraso ? " (subiu por atraso)" : porVblank ? " (subiu: o DXGI viu vblank perdido com o lead no teto)" : "", m_cadPhiS, CadAvanco(), m_cadIflip ? "iFlip" : "composta",
                  m_f2KEsperaMs / 1000.0);
    }
    // Um quadro novo da ponte (em ordem): gera o ponto medio (A, B), marca o vblank do real (carimbo + K) e o do gerado (o do
    // meio) e enfileira os dois. Nada sai daqui: quem apresenta e F2ApresentarFrente, na hora de cada um.
    void F2Novo(Ponte* p, FrameGen::Interpolador* fg, ID3D11Texture2D* t, uint64_t n, uint64_t fila, uint64_t pulados) {
        const double tPronto = Ms();
        if (pulados) {
            m_cadPulados.fetch_add(pulados, std::memory_order_relaxed);
            if (m_log) m_log("fg2x: AVISO - %llu quadro(s) da ponte reescritos antes de serem lidos (quadro %llu)", (unsigned long long)pulados, (unsigned long long)n);
        }
        if (fila > m_cadFilaMax) m_cadFilaMax = fila;
        // o carimbo cru do quadro (o main o poe ANTES do Enviar; a espera curta e so garantia)
        double car = CarimboDe(n);
        for (int i = 0; car <= 0.0 && i < 20; i++) { YieldProcessor(); Sleep(0); car = CarimboDe(n); }
        // chegada/panel (o mesmo numero do caminho de antes)
        const double mediana = MedianaChegada(&m_dtN);
        m_chegadaFps.store(mediana > 0.0 ? 1000.0 / mediana : 0.0);
        m_interpAtiva.store(true);
        // seguranca: VRAM acima do orcamento (o main pede) ou interpolador morto -> so o real, na mesma cadencia
        const bool vram = m_vramPausa.load();
        if (vram && !fg->ContextoSolto()) fg->SoltarContextoVram();
        else if (!vram && fg->ContextoSolto()) fg->RestaurarContextoVram();
        const bool semFi = vram || fg->Morto() || fg->ContextoSolto();
        m_pausada = semFi; m_motivo = vram ? GATE_VRAM : fg->Morto() ? GATE_MORTO : GATE_OK;
        m_pausadaAt.store(semFi); m_motivoAt.store(semFi ? m_motivo : GATE_OK);
        FrameGen::Resultado r;
        bool gerou = false;
        const uint64_t ger = fg->ProximoGer();
        if (!semFi) {
            const double tg0 = Ms();
            gerou = fg->Gerar(m_ctx, t, n, (float)mediana, &r);
            const double dg = Ms() - tg0; m_gerCpuMs = m_gerCpuMs > 0.0 ? 0.9 * m_gerCpuMs + 0.1 * dg : dg;
        } else if (!fg->Morto()) fg->Alimentar(m_ctx, t);
        const double tGer = Ms();
        // B com copia propria do FI (vale por kSlots Gerar): a ponte e solta ja. Sem copia (sem FI, ou repetido), o real
        // vai com a textura da ponte e o slot so e solto depois do Present dele.
        const bool copiaPropria = r.B != nullptr && !r.repetido;
        ID3D11Texture2D* texB = copiaPropria ? r.B : t;
        if (copiaPropria) p->Liberar(n);
        SetEvent(m_evtConsumiu);   // o laco principal segue (a ponte ja tem o proximo slot livre, ou o real guarda o dele)
        ID3D11Texture2D* texG = (gerou && !r.corte && !r.repetido) ? (r.nG > 0 && r.Gs[0] ? r.Gs[0] : r.G) : nullptr;
        if (gerou && r.corte) m_f2CortesJan++;
        if (!m_f2TetoAvisou && m_tetoApres.load(std::memory_order_relaxed) > 0.0) {
            m_f2TetoAvisou = true;
            if (m_log) m_log("fg2x: o teto de apresentacao (realimentacao) NAO vale neste modo - a trava ao vblank ja limita a saida ao refresh, e nenhum quadro e descartado por teto");
        }
        const bool grade = car > 0.0 && CadGrade(tPronto);
        const double P = m_cadP;
        F2Pend pb; pb.t = texB; pb.gerado = false; pb.car = car; pb.liberarN = copiaPropria ? 0 : n; pb.n = n;
        pb.ger = copiaPropria ? ger : ger + (1ull << 40);   // com a textura da ponte o slot do FI nao importa (quem segura e o slot da ponte)
        if (grade) {
            // a FASE DA FONTE (media circular lenta, como na cadencia sem FG): o arredondamento e em torno dela
            const double gc = CadG(car);
            { const double rr = gc - floor(gc);
              m_cadPhiSx = 0.94 * m_cadPhiSx + 0.06 * cos(2.0 * M_PI * rr); m_cadPhiSy = 0.94 * m_cadPhiSy + 0.06 * sin(2.0 * M_PI * rr);
              m_cadPhiS = atan2(m_cadPhiSy, m_cadPhiSx) / (2.0 * M_PI); }
            const long long srcIdx = (long long)floor(gc - m_cadPhiS + 0.5);
            long long gap = m_f2TemAnt ? srcIdx - m_f2SrcAnt : 0;
            if (gap >= 1 && gap <= 16) { m_f2Gap[m_f2GapPos] = (int)gap; m_f2GapPos = (m_f2GapPos + 1) % 64; if (m_f2GapN < 64) m_f2GapN++; }
            const double path = (tGer - car) + (gerou ? fg->FiMs() : 0.0);
            if (path > -100.0 && path < 250.0) { m_f2PathV[m_f2PathPos] = path; m_f2PathT[m_f2PathPos] = tPronto; m_f2PathPos = (m_f2PathPos + 1) % 512; }
            F2AtualizarK(tPronto, P);
            long long idxB = srcIdx + m_f2K;
            if (m_f2TemAnt && idxB <= m_f2IdxAntB) { idxB = m_f2IdxAntB + 1; m_f2EmpurJan++; }
            if (m_f2TemTela && idxB <= m_f2UltIdxTela) { idxB = m_f2UltIdxTela + 1; m_f2EmpurJan++; }
            pb.temIdx = true; pb.idx = idxB;
            // o GERADO: o vblank do meio entre o real anterior (A) e este (B)
            if (texG) {
                const long long d = m_f2TemAnt ? idxB - m_f2IdxAntB : 0;
                // vao (fonte parada / primeiro par depois de uma pausa): nao ha "meio" que faca sentido - so o real
                const int medGap = std::max(1, 2 * m_f2H);
                if (!m_f2TemAnt || gap > std::max(8, 4 * medGap)) m_f2VaosJan++;
                else if (d < 2) { m_f2GerDescJan++; m_f2GerSemVbJan++; m_f2GerDescTot.fetch_add(1, std::memory_order_relaxed); }   // 1 vblank entre os reais: nao ha vaga
                else {
                    long long h = d / 2;
                    if (d & 1) { h += m_f2Alterna ? 1 : 0; m_f2Alterna = !m_f2Alterna; }   // impar: piso/teto alternados, deterministico
                    long long idxG = m_f2IdxAntB + h;
                    bool gCabe = true;
                    const double agoraG = Ms();
                    if (agoraG > F2SoltaIdx(idxG) + 0.25) {
                        long long c = idxG + 1;
                        while (c < idxB && agoraG > F2SoltaIdx(c) + 0.25) c++;
                        if (c < idxB) { idxG = c; m_f2GerMovidoJan++; }
                        else gCabe = false;
                    }
                    if (!gCabe) {
                        m_f2GerDescJan++; m_f2GerTardeJan++; m_f2GerDescTot.fetch_add(1, std::memory_order_relaxed);
                        m_f2AtrT[m_f2AtrPos] = agoraG; m_f2AtrPos = (m_f2AtrPos + 1) % 64;
                    } else {
                        F2Pend pg; pg.t = texG; pg.gerado = true; pg.temIdx = true; pg.idx = idxG; pg.idxLimite = idxB; pg.ger = ger; pg.n = n;
                        pg.segurado = Ms() < F2SoltaIdx(idxG);
                        F2Push(pg);
                    }
                }
            }
            pb.segurado = Ms() < F2SoltaIdx(idxB);
            m_f2IdxAntB = idxB; m_f2SrcAnt = srcIdx; m_f2TemAnt = true; m_f2CarAnt = car;
        } else {
            // sem carimbo ou sem ancora no vblank: o de antes, G ja e B meio intervalo depois (contado)
            m_f2SemAncoraJan++;
            const double agoraF = Ms();
            if (texG) { F2Pend pg; pg.t = texG; pg.gerado = true; pg.soltaFixa = agoraF; pg.ger = ger; pg.n = n; F2Push(pg); }
            pb.soltaFixa = agoraF + (texG ? 0.5 * (mediana > 0.0 ? mediana : 16.667) : 0.0);
            m_f2TemAnt = false;
        }
        F2Push(pb);
    }
    // Uma volta do laco no modo 2x: solta o pendente da hora; senao pega o proximo quadro em ordem (se ha lugar nos slots do
    // FI e tempo ate o pendente); senao espera (despertador de alta resolucao, ou o evento de quadro novo).
    void F2Volta(Ponte* p, FrameGen::Interpolador* fg) {
        if (!m_f2Prep) {
            m_f2Prep = true;
            CriarDespertador();
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
            m_f2LogMs = m_f2SegMs = m_f2IniMs = Ms();
            if (m_log) m_log("fg2x: LIGADO - saida = 2 x fonte travada ao vblank (real em carimbo + K, gerado no vblank do meio; intervalo impar alterna piso/teto); sem admissao, sem descarte por amostragem, sem teto; reais nunca descartados");
        }
        if (!m_f2Dedup) {
            fg->DedupPermitir(false); m_f2Dedup = true;
            if (m_log) m_log("fg2x: o dedup POR AMOSTRAGEM do FI fica desligado neste modo (so vale o dedup EXATO, bit a bit, antes da rede)");
        }
        if (fg->Multiplicador() != 2) fg->Multiplicador(2);
        m_multEf = 2;
        AjustarLatenciaFg();
        const double agora = Ms();
        CadGrade(agora);
        // 1. o pendente da hora
        if (m_f2N > 0 && agora >= F2Solta(m_f2[0]) - 0.05) { F2ApresentarFrente(false); return; }
        // 2. quadro novo: so se os slots do FI aguentam (o proximo Gerar reescreve o slot de quem tem ger <= proximo - kSlots)
        //    e se ha tempo de gerar antes da hora do pendente
        bool cabe = true;
        const uint64_t prox = fg->ProximoGer();
        for (int i = 0; i < m_f2N; i++) if (m_f2[i].ger + (uint64_t)FrameGen::Interpolador::kSlots <= prox) { cabe = false; break; }
        const bool temTempo = m_f2N == 0 || F2Solta(m_f2[0]) - agora > LimiarGerarMs() + 0.5;
        if (cabe && temTempo) {
            uint64_t n = 0, fila = 0, pul = 0;
            ID3D11Texture2D* t = p->ReceberSeguinte(&n, &fila, &pul);
            if (t) { F2Novo(p, fg, t, n, fila, pul); return; }
        }
        // 3. espera
        if (m_f2N > 0) { m_rWake += EsperarAte(F2Solta(m_f2[0]), true, cabe); m_rWakeN++; }
        else WaitForSingleObject(m_evtNovo, 50);
        if (m_f2N == 0) F2Log(Ms());
    }
    // A linha do modo, a cada 10 s (e o apresentados/s do painel a cada 1 s).
    void F2Log(double tP) {
        if (m_f2SegMs <= 0.0) m_f2SegMs = tP;
        if (tP - m_f2SegMs >= 1000.0) { m_apresFps = (double)m_f2PresSeg * 1000.0 / (tP - m_f2SegMs); m_f2PresSeg = 0; m_f2SegMs = tP; }
        if (m_f2LogMs <= 0.0) m_f2LogMs = tP;
        if (tP - m_f2LogMs < 10000.0 || !m_log) return;
        auto hist = [](const uint64_t* h, char* out, size_t cap) {
            uint64_t tot = 0; for (int i = 0; i < 16; i++) tot += h[i];
            int ord[16]; for (int i = 0; i < 16; i++) ord[i] = i;
            std::sort(ord, ord + 16, [h](int a, int b) { return h[a] > h[b]; });
            size_t pos = 0; out[0] = 0;
            for (int j = 0; j < 4 && tot; j++) {
                if (!h[ord[j]]) break;
                const int e = _snprintf(out + pos, cap - pos, "%s%d=%.1f%%", j ? " " : "", ord[j], 100.0 * (double)h[ord[j]] / (double)tot);
                if (e <= 0 || (size_t)e >= cap - pos) break;
                pos += (size_t)e;
            }
            if (!pos) _snprintf(out, cap, "-");
        };
        char ht[96], hr[96], hf[96]; hist(m_f2HTela, ht, sizeof ht); hist(m_f2HReal, hr, sizeof hr); hist(m_f2HFonte, hf, sizeof hf);
        double p50 = 0.0, p95 = 0.0;
        if (m_f2LatN > 0) { std::sort(m_f2Lat, m_f2Lat + m_f2LatN); p50 = m_f2Lat[m_f2LatN / 2]; p95 = m_f2Lat[std::min(m_f2LatN - 1, (int)(m_f2LatN * 0.95))]; }
        const double seg = (tP - m_f2LogMs) / 1000.0;
        const double fonte = seg > 0.0 ? (double)m_f2RealJan / seg : 0.0;
        const double hz = m_cadP > 0.0 ? 1000.0 / m_cadP : 0.0;
        m_log("fg2x: %.0f s | reais na tela %llu (%.1f/s) | gerados na tela %llu (%.1f/s) | gerados DESCARTADOS %llu (sem vblank livre %llu, prontos tarde %llu; movidos p/ o vblank seguinte %llu) | cortes %llu | vaos %llu | reais atrasados %llu | empurrados %llu | sem ancora %llu | "
              "TELA entre presents (refreshes) %s | real->real na tela %s x FONTE %s (casou %.1f%% de %llu) | saida alvo 2 x %.1f = %.1f/s (tela %.0f Hz, limite %.0f/s) | tela %.1f presents/s | "
              "K %d (h %d, p90 carimbo->G pronto %.2f ms) | latencia carimbo->Present (reais) p50 %.2f / p95 %.2f ms | %s, avanco %.2f P, present->tela p50 %.2f ms | segurados %llu de %llu | gerar %.2f ms CPU, FI %.2f ms GPU",
              seg, (unsigned long long)m_f2RealJan, fonte, (unsigned long long)m_f2GerJan, seg > 0.0 ? (double)m_f2GerJan / seg : 0.0,
              (unsigned long long)m_f2GerDescJan, (unsigned long long)m_f2GerSemVbJan, (unsigned long long)m_f2GerTardeJan, (unsigned long long)m_f2GerMovidoJan,
              (unsigned long long)m_f2CortesJan, (unsigned long long)m_f2VaosJan, (unsigned long long)m_f2RealAtrJan, (unsigned long long)m_f2EmpurJan, (unsigned long long)m_f2SemAncoraJan,
              ht, hr, hf, m_f2CmpN ? 100.0 * (double)m_f2Casou / (double)m_f2CmpN : 0.0, (unsigned long long)m_f2CmpN,
              fonte, std::min(2.0 * fonte, hz > 0.0 ? hz : 2.0 * fonte), hz, hz, seg > 0.0 ? (double)m_f2PresJan / seg : 0.0,
              m_f2K, m_f2H, m_f2P90, p50, p95, m_cadIflip ? "iFlip" : "composta", CadAvanco(), m_iflip.UltP50(),
              (unsigned long long)m_f2SegJan, (unsigned long long)m_f2PresJan, m_gerCpuMs, m_fg.load() ? m_fg.load()->FiMs() : 0.0);
        m_f2LogMs = tP;
        for (int i = 0; i < 16; i++) { m_f2HTela[i] = m_f2HReal[i] = m_f2HFonte[i] = 0; }
        m_f2Casou = m_f2CmpN = 0; m_f2LatN = 0;
        m_f2RealJan = m_f2GerJan = m_f2GerDescJan = m_f2GerSemVbJan = m_f2GerTardeJan = m_f2GerMovidoJan = 0;
        m_f2CortesJan = m_f2VaosJan = m_f2RealAtrJan = m_f2EmpurJan = m_f2SemAncoraJan = m_f2SegJan = m_f2PresJan = 0;
    }
    // Um giro do laco com interpolador: o B atrasado que venceu o prazo sai; um quadro novo vira
    // G (agora) + B (meio intervalo depois). Corte de cena ou sem quadro anterior: B direto.
    void LacoInterpolado(Ponte* p, FrameGen::Interpolador* fg) {
        if (!m_timer) {   // primeira volta com interpolador: despertador e prioridade de thread de ritmo
            CriarDespertador();
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        }
        AjustarLatenciaFg();   // O1: sai na hora quando nada mudou; refaz a fila quando o card troca de modo/latencia
        // O PENDENTE SAI NO PRAZO. A espera do Laco() ja o trouxe ate aqui (ou a pausa/saida
        // a interrompeu, e ai ele sai um pouco cedo - contado como "adiantado").
        // X-FG-6: com 3x/4x a fila tem mais de um (G2/4, G3/4 e o B); sai UM por volta, e enquanto
        // sobrar algum o quadro novo que chegou espera - e o relogio quem distribui os quadros,
        // nao a chegada (I13). Em 2x a fila tem so o B e a volta segue como sempre.
        /* X-FG-QUALIDADE (fila de um quadro): quantos quadros estao na fila NESTE instante - a medida
           do "queue target" do LS, tirada ANTES de o pendente da hora sair. Em ritmo, a chegada acha
           exatamente 1 (o B do par anterior, na hora de sair); 2 ou mais = o G anterior ainda nem saiu,
           a chegada esta mais rapida que o tique (encurta 1%); 0 = mais lenta (alonga 1%). */
        const int pendNaChegada = m_pendN - m_pendI;
        m_r5Fila += (double)pendNaChegada; m_r5FilaN++;
        /* K-RITMO C1: a FASE, medida na chegada. `folgaChegada` e quanto faltava para o pendente
           sair quando o quadro novo entrou - o `folga` (phi) do LS-RITMO §6.4. A ocupacao (0/1/2)
           diz se a fila tem quadro; so a folga diz se ele vai sair NA HORA. Era essa a cegueira
           do momentum antigo: cheia 0, vazia 0 com todos os G fora de hora. */
        /* RITMO-3X (20/09/2026, o C1 do K-RITMO): a PROFUNDIDADE da fila - quanto de TEMPO ela
           ainda guarda - e `m_ultAlvo - agora`, o quanto a cadencia corre adiante do relogio.
           `folgaChegada` (a distancia ate o PROXIMO pendente sair) nunca passa de UM TIQUE por
           construcao, seja qual for o K: a 3x, com tique de 11,11 ms, nenhum alvo de fase acima
           disso era alcancavel e `D` era cortado antes de poder pedir a folga que a maquina
           precisa (O1-APRESENTACAO, a secao do 3x). A folga continua medida - e ela que manda na
           regra de soltura, logo abaixo, no `falta` -, mas quem responde por FASE e por RITMO
           agora e a profundidade, e `folgaChegada` deixou de existir como variavel. */
        const double tChegada = Ms();
        const double profFila = m_ultAlvo > 0.0 ? m_ultAlvo - tChegada : 0.0;
        const bool   profVale = m_ultAlvo > 0.0;
        if (const Pend* pd = PendProximo()) {
            const double falta = pd->alvo - tChegada;
            if (m_fila > 0 && falta > LimiarGerarMs()) {
                /* fila: a espera foi interrompida por um quadro novo com folga - o pendente NAO esta
                   na hora; o par novo e gerado primeiro e a espera recomeca no Laco() */
            } else {
                if (falta > 0.5) m_rAdiant++;
                else if (falta < -2.0) m_rAtr2++;
                if (m_rAtrN < 4096) m_rAtrB[m_rAtrN++] = std::max(0.0, -falta);
                ApresentarTex(pd->t, pd->gerado, pd->passo); m_pendI++;
                if (m_fila > 0) { if (!PendProximo()) PendLimpar(); }   /* fila: os que sobram esperam o tique deles; o quadro novo entra ja */
                else { if (PendProximo()) return; PendLimpar(); }   /* ainda ha quadro deste intervalo por mostrar: o proximo B espera */
            }
        }
        // (X-TDR: a espera pelo lugar no swapchain saiu daqui - e uma por Present, dentro de ApresentarTex)
        uint64_t n = 0;
        ID3D11Texture2D* t = p->Receber(&n);
        if (!t) { SetEvent(m_evtConsumiu); m_pulos++; return; }
        m_novoPendente = false;   // o quadro anotado durante a espera e este
        const double agora = Ms();
        DescartarRepetidos(fg, agora);   // K-DEDUP §4 (ver a funcao): o veredito que ja tiver chegado
        /* X-FG-QUALIDADE (fila): CAPACIDADE. O interpolador alterna DOIS slots (inB/outG[n%2]); o Gerar
           deste quadro (e o Alimentar da pausa, e a copia de um repetido) reescreve o slot do par de
           DOIS Gerar atras. Qualquer pendente desse par sai AGORA, antes de o slot ser tocado - e
           contado como estouro (o tique encurtando 1% por quadro deve tornar isto raro). */
        /* X-GERADO (20/09/2026): SO O ULTIMO DO DESPEJO VAI A TELA. Ate hoje cada pendente que
           estourava a capacidade era apresentado, um atras do outro, em milissegundos. O compositor
           mostra UM quadro por composicao: desses presents colados so o ULTIMO vira imagem - os
           outros sao trabalho jogado fora que ainda por cima empurra o relogio do ritmo
           (m_ultPresentMs) e derruba a conta de `imagens distintas`. MEDIDO no alvo a 30 fps: as
           janelas com `estouro 14..28` caiam para 52-53 imagens distintas/s contra 59 nas limpas.
           Apresentar so o ultimo nao perde imagem nenhuma (as puladas nao chegariam a tela de
           qualquer jeito) e o contador segue dizendo quantas foram. O estouro em si e da FONTE:
           depois de um vao do WGC a captura despeja o atraso de uma vez e nascem tres pares antes
           de o primeiro sair - quem tem de recusar o par ali e a admissao, nao este despejo. */
        if (m_fila > 0 && m_pendI < m_pendN) {
            /* X-GERADO: com kSlots do interpolador, o par de `kSlots - 1` Gerar atras e o que vai ser
               reescrito - com tres slots o pendente pode atravessar UMA chegada, que e o que a
               fila precisa para absorver o `j` da captura. */
            const uint64_t nSl = (uint64_t)FrameGen::Interpolador::kSlots - 1;
            const uint64_t limite = fg->Gerados() >= nSl ? fg->Gerados() - nSl : 0;
            int fim = m_pendI;
            while (fim < m_pendN && m_pend[fim].par <= limite) fim++;
            if (fim > m_pendI) {
                const Pend ult = m_pend[fim - 1];
                const uint64_t quantos = (uint64_t)(fim - m_pendI);
                m_pendI = fim;
                m_fEstouro += quantos; m_fEstouroTot += quantos;
                ApresentarTex(ult.t, ult.gerado, 0.0);
            }
            if (!PendProximo()) PendLimpar();
        }
        /* FONTE PARADA (I18, PCSX2 do dono, 18/09/2026). Com o emulador em menu/pausa a captura
           nao entrega nada (WGC so acorda quando a janela muda) e a cadeia oculta re-apresenta
           a MESMA imagem a cada 500 ms (o present ocioso do laco principal). Este laco entao
           recebe ~2 quadros/s - e isso nao e travamento: nao ha quadro novo para mostrar, e a
           tela fica com o ultimo (o compositor segura o present anterior). O que ERA defeito:
           a mediana da chegada ignora vaos acima de 250 ms (de proposito, para um soluco nao
           derrubar o ritmo), entao `fps` continuava dizendo 45,6 com a fonte parada, e o gate
           comparava 1,8 apresentados/s contra "45 sem FG": pausava por "nao compensa",
           projetava a volta com a mesma chegada fantasma, voltava, pausava... um cartaz a
           cada troca. Agora um vao maior que 4 intervalos (minimo 250 ms) marca a fonte como
           PARADA: o gate nao decide nem acumula ate a chegada voltar (Gate()), e o quadro que
           chega depois do vao sai como B direto - interpolar entre a imagem velha e a nova,
           meio segundo ou mais depois, so inventaria um quadro entre duas cenas. */
        bool vaoAgora = false;
        if (m_ultChegada > 0.0) {
            // o vao continua medido AQUI (entre dois quadros que este laco pegou): e o que diz
            // se a fonte parou. O intervalo de chegada, nao - ele vem da cadeia (ver Acordar()).
            const double dt = agora - m_ultChegada;
            const double medAntes = MedianaChegada();
            const double tetoVao = std::max(250.0, (medAntes > 0.0 ? medAntes : 50.0) * 4.0);
            if (dt >= tetoVao) { vaoAgora = true; if (!m_fonteParada) { m_fonteParada = true; m_fonteParadaDesde = m_ultChegada; m_paradas++; } m_janelaVao = true; }
            else if (m_fonteParada) { m_fonteParada = false; m_fonteParadaMs += agora - m_fonteParadaDesde; }
            /* `j`: o tremor da chegada e o DESVIO em relacao a mediana, nao o intervalo. Vaos nao
               entram: eles falam da fonte parada, nao de como ela entrega quando esta entregando.
               X-GERADO: com carimbo do quadro (captura WGC) os desvios vem prontos de Acordar, do
               relogio DO QUADRO - o relogio de recepcao aqui carrega o atraso do WGC e o da propria
               volta do laco, e era ele que punha `j` em 21 ms com o alvo a 30 fps cravados. Sem
               carimbo (modo injetado) a fila vem vazia e vale a medida de sempre. */
            int nj = 0; double jv[32];
            AcquireSRWLockExclusive(&m_srcLock);
            nj = m_jFilaN; for (int i = 0; i < nj; i++) jv[i] = m_jFila[i];
            m_jFilaN = 0;
            ReleaseSRWLockExclusive(&m_srcLock);
            if (nj > 0) { for (int i = 0; i < nj; i++) MedirJ(agora, jv[i]); }
            else if (!vaoAgora && medAntes > 0.0 && !m_carimboFonte.load(std::memory_order_relaxed)) MedirJ(agora, fabs(dt - medAntes));
        }
        m_ultChegada = agora;
        const double mediana = MedianaChegada(&m_dtN);
        const double fps = mediana > 0.0 ? 1000.0 / mediana : 0.0;
        m_chegadaFps.store(fps);
        m_interpAtiva.store(true);
        Gate(fps, agora, fg);
        /* K-RITMO C0 (o defeito K1). Ate 20/09/2026 este laco descarregava TUDO o que estava na
           fila aqui, no instante da chegada, com alvo 0 - inclusive o B que a espera de cima
           tinha deixado de proposito para sair no prazo dele. Com isso a fila de um quadro nunca
           segurava nada: o B saia na chegada, o G saia phi ms depois do lugar dele, e a tela
           via 25/8/25/8 ms em vez de 16/16/16/16 (LS-RITMO §6.1). Com fila, os pendentes FICAM:
           quem os solta e o prazo, na espera do Laco(). Sem fila (m_fila == 0) nada muda. */
        if (m_fila == 0) {
            while (const Pend* pd = PendProximo()) { m_rAdiant++; ApresentarTex(pd->t, pd->gerado); m_pendI++; }
            PendLimpar();
        }
        /* BANCADA - DLSS5_FG_SEM_FI=1: percorre TODO o caminho do modo interpolado (janela
           oculta + ponte + apresentador em outro device) mas NAO despacha o interpolador.
           Existe para repartir os +8,9 ms que ligar a fluidez custa na cadeia: o que sobrar
           aqui e o caminho; o resto e o FI. Nao muda nada quando a env nao esta posta. */
        if (m_semFi < 0) {
            wchar_t e[8]{}; m_semFi = 0;
            if (GetEnvironmentVariableW(L"DLSS5_FG_SEM_FI", e, 8) > 0 && e[0] == L'1') {
                m_semFi = 1;
                if (m_log) m_log("apresentador: BANCADA - DLSS5_FG_SEM_FI=1: o caminho do modo interpolado roda inteiro, mas o FI NAO e despachado (so quadros reais)");
            }
        }
        if (m_semFi == 1) { FilaEsvaziarAgora(); ApresentarTex(t, false); p->Liberar(n); SetEvent(m_evtConsumiu); return; }
        // EM PAUSA nao se despacha o FI: so se guarda o quadro (ver fg_ffx.h, Alimentar()).
        // Pagar o interpolador sem receber quadro gerado era o pior dos dois mundos - medido
        // em 13/09/2026: com o gate pausado a cadeia ficava em 26,18 ms em vez de 17,22.
        if (m_pausada) {
            /* BANCADA - DLSS5_FG_SEM_ALIM=1: pausado, nao copia o quadro para a textura propria
               do FI. E a unica coisa que o FG ainda gasta na placa do JOGO durante a pausa (uma
               CopyResource 4K por quadro); existe para repartir os -42% da tarefa 1q. Com ela
               ligada o historico do FI fica velho ao retomar - so bancada. Ausente, nada muda. */
            if (m_semAlim < 0) {
                wchar_t e[8]{}; m_semAlim = 0;
                if (GetEnvironmentVariableW(L"DLSS5_FG_SEM_ALIM", e, 8) > 0 && e[0] == L'1') {
                    m_semAlim = 1;
                    if (m_log) m_log("apresentador: BANCADA - DLSS5_FG_SEM_ALIM=1: pausado, o quadro NAO e copiado para o FI");
                }
            }
            if (m_motivo == GATE_VRAM) fg->SoltarContextoVram();   // X-HUD-PISCA: nesta thread, fora de qualquer Gerar (idempotente)
            FilaEsvaziarAgora();   // X-FG-QUALIDADE: pausado nao ha ritmo de par; o Alimentar reescreve um slot que pode estar na fila
            if (m_semAlim != 1) fg->Alimentar(m_ctx, t);
            ApresentarTex(t, false);
            p->Liberar(n);          // depois do Mostrar: o slot so pode ser reescrito apos a copia
            SetEvent(m_evtConsumiu);
            return;
        }
        // X-HUD-PISCA: voltando da pausa por VRAM, o contexto do FI e refeito aqui (0,3-0,8 s, uma vez)
        if (fg->ContextoSolto() && !fg->RestaurarContextoVram()) {
            ApresentarTex(t, false); p->Liberar(n); SetEvent(m_evtConsumiu); return;   // nao voltou: o Gate ve fg->Morto()/sem contexto no proximo quadro
        }
        /* ===== X-FONTE-SOLTA (20/09/2026): A ADMISSAO. Ver AdmitirPar(). ===== */
        m_admChegadas++;
        const bool admite = AdmitirPar(fg, agora);
        if (!admite) m_ultDescarteMs = agora;
        else m_admAceitas++;
        AdmissaoLog(agora);
        if (!admite) {
            /* O quadro NAO entra no FI: e solto de volta e o proximo Receber() pega o mais
               recente - o candidato foi SUBSTITUIDO, nao enfileirado. Se a fila de pendentes
               esta vazia e ja passou um tique desde o ultimo present, a tela ficaria parada:
               ai sai o quadro REAL, que e exatamente o que sairia com a fluidez desligada.
               O teto do tique e o que impede isso de virar 200 presents/s com a fonte solta. */
            const double tique = m_fTique > 0.0 ? m_fTique : mediana;
            if (!PendProximo() && (m_ultPresentMs <= 0.0 || agora - m_ultPresentMs >= tique)) ApresentarTex(t, false);
            p->Liberar(n); SetEvent(m_evtConsumiu);
            return;
        }
        /* X-FG-6 (3x/4x): a TELA limita o multiplicador - K por chegada acima da taxa do monitor so
           vira present descartado pelo compositor (cadencia quebrada). O limite acompanha a chegada
           (a fonte pode mudar de 60 para 30); a queda pela PLACA (Gate) e que fica pela sessao. */
        {
            /* FG-BASE: O N E O QUE A PESSOA PEDIU. Dois cortes moravam aqui e sairam: o da TELA
               (`%dx nao cabe na tela de %.0f Hz`) e o da placa. Numa tela de 100 Hz com o jogo a
               60, pedir 4x da 240 presents/s que a tela nao mostra - e isso NAO e motivo para
               mudar o pedido: quem escolhe o que entra e o que sobra e a ADMISSAO, que ja mede
               rajada e descarte, e o contador conta o resultado em numero. Resta a queda por
               memoria de video (m_multQueda, pedida pelo main), que e seguranca e e temporaria. */
            int k = m_multQueda ? 2 : m_mult;
            /* ===== O1-TELA: a RAZAO, e o N que ela pede ao interpolador =====
               `R` = quantos vblanks cabem num intervalo da fonte. Vem do `m_medTique` (a EMA de
               ~20 chegadas do RITMO-3X, nao a mediana crua: a mediana salta um vblank inteiro
               entre chegadas e R saltaria junto) e e re-estimado a cada chegada, mas so MUDA de
               N com histerese de 0,2 em volta do ponto de virada - trocar de N custa o
               nascimento/morte do 2o contexto do FI (0,3-0,8 s), e nao pode acontecer porque a
               fonte oscilou de 39,8 para 40,2 fps.
               Que N pedir: o modo escolhe pela riqueza de FRACOES, nao pela contagem. 4x da
               {1/4, 1/2, 3/4} - tres fracoes para escolher; 3x daria so {1/4, 3/4}, que e PIOR
               para este modo apesar de custar o mesmo (tres despachos do FI nos dois). Entao e
               2x (uma fracao, 1/2) quando cabe um gerado por intervalo, e 4x acima disso. */
            if (m_modoTela) {
                /* A RAZAO VEM DA FONTE CRUA, e isto foi medido: a primeira versao usava o
                   `m_medTique`, que nasce da mediana dos quadros ADMITIDOS - e a admissao (1b)
                   recusava tudo que chegasse a menos de N periodos da tela. Circulo fechado: o
                   alvo a 96 fps aparecia como `fonte 50 fps`, R dava 4,8, o N ia a 4, e a
                   admissao seguia recusando. A cadeia crua (`m_srcDts`, escrita em Acordar
                   antes de qualquer recusa) e a unica que diz quanto o jogo de fato entrega.
                   EMA de 0,95/0,05 (~20 chegadas) por cima, pelo mesmo motivo do RITMO-3X: a
                   mediana crua salta um vblank inteiro entre chegadas. */
                const double per = MetaPeriodoMs();   // META: 1000/meta (Tela = o periodo do vblank)
                const double crua = MedianaCadeiaCrua();
                /* a fonte MUDOU de patamar (mais de 20%: a rede ficou mais pesada, ou aliviou): a EMA
                   salta para o valor novo em vez de arrastar 20 chegadas com o fps de antes */
                if (crua > 0.0 && m_telaFonteMs > 0.0 && fabs(crua - m_telaFonteMs) > 0.2 * m_telaFonteMs) m_telaFonteMs = crua;
                if (crua > 0.0) m_telaFonteMs = m_telaFonteMs > 0.0 ? 0.95 * m_telaFonteMs + 0.05 * crua : crua;
                if (per > 0.0 && m_telaFonteMs > 0.0 && m_dtN >= 8) m_telaR = m_telaFonteMs / per;
                /* META (dono): GERAR OU NAO, com histerese de 2 s. R >= 1,25 pede gerados; abaixo, a meta
                   ja esta coberta pelo jogo (60 com o jogo a 60) e nada e inventado. A primeira decisao
                   e imediata; as trocas seguintes esperam 2 s no estado novo para nao piscar quando o
                   fps do jogo oscila em volta do ponto. O log diz de onde para onde a fonte foi. */
                if (m_telaR > 0.0 && m_dtN >= 8) {
                    const bool quer = m_telaR >= kMetaRMin;
                    if (quer != m_metaGerando) {
                        if (!m_metaDecidiu) { m_metaGerando = quer; m_metaDecidiu = true; m_metaFonteAntes = 1000.0 / m_telaFonteMs; }
                        else if (m_metaGerDesde <= 0.0) m_metaGerDesde = agora;
                        else if (agora - m_metaGerDesde >= 2000.0) {
                            m_metaGerando = quer; m_metaGerDesde = 0.0;
                            const double fonteAgora = 1000.0 / m_telaFonteMs;
                            if (m_log) m_log("apresentador tela-modo: fonte %s %.0f -> %.0f, %s (meta %.0f)",
                                             fonteAgora < m_metaFonteAntes ? "caiu" : "subiu", m_metaFonteAntes, fonteAgora,
                                             quer ? "gerando" : "sem gerados (meta <= fonte)", MetaFps());
                            m_metaFonteAntes = fonteAgora;
                        }
                    } else m_metaGerDesde = 0.0;
                }
                /* META: que N pedir - 2x (R ate 2), 4x (ate 4) ou 8x (ate 8), com histerese de 0,2
                   nos dois pontos de virada; e nunca acima do que o CUSTO permite (MetaCusto). */
                MetaCusto(fg, agora);
                int quer = 2;
                if (m_metaGerando) quer = m_telaR > (m_multEf >= 8 ? 4.0 : 4.2) ? 8 : m_telaR > (m_multEf >= 4 ? 2.0 : 2.2) ? 4 : 2;
                if (!m_multQueda) k = std::min(quer, m_metaCapK);
                /* O ARRANQUE E O COOLDOWN (w-153, 21/09): no HEAD 9469737 o Tela oscilava 8x <-> 4x nove vezes
                   em 20 s no arranque, criando e destruindo o 3o contexto a cada troca. Duas causas: R ainda
                   nascendo (a EMA da fonte com poucas chegadas) e o custo por gerado medido com o FiMs de UM
                   N e dividido pelos gerados de OUTRO (a EMA do FI atrasa a troca). Entao: o N so e decidido
                   depois de 3 s de medida (ate la fica o que estava), e uma troca so pode acontecer 5 s
                   depois da anterior. A queda por VRAM (m_multQueda) continua imediata. */
                if (m_metaInicioMs <= 0.0) m_metaInicioMs = agora;
                if (k != m_multEf && !m_multQueda) {
                    const bool cedo = agora - m_metaInicioMs < 3000.0;
                    const bool esfriando = m_metaMultMs > 0.0 && agora - m_metaMultMs < 5000.0;
                    if (cedo || esfriando) k = m_multEf;
                    else m_metaMultMs = agora;
                }
                m_metaRef = m_metaGerando ? std::min(m_telaR, (double)k) : 1.0;   // o R que de fato sai
                m_metaEntregue = m_telaFonteMs > 0.0 ? 1000.0 / m_telaFonteMs * m_metaRef : 0.0;
            }
            if (k != m_multEf) { m_multEf = k; m_multCustoDesde = 0.0; }
            if (fg->Multiplicador() != m_multEf) fg->Multiplicador(m_multEf);
        }
        /* ===== O1-TELA: o jogo ja passa da tela (R < 1,5) - nao ha o que inventar =====
           A 140 fps numa tela de 100 Hz nao falta quadro, SOBRA. Gerar aqui seria pagar o FI
           para produzir uma imagem que nunca teria vblank em que cair. O quadro real sai no
           proximo vblank e a ADMISSAO descarta o excesso, que e o caminho que ja existe (o piso
           dela sai do `m_fTique`, que neste modo e um vblank). `Alimentar` mantem o historico
           do FI quente sem despachar - o mesmo que a pausa por economia faz - entao voltar a
           gerar quando o jogo cair de fps nao custa um contexto novo nem um quadro sujo. */
        if (m_modoTela && m_telaR > 0.0 && !m_metaGerando && m_dtN >= 8) {   /* META: a meta ja esta coberta pelo jogo */
            FilaEsvaziarAgora();   // o Alimentar reescreve um slot que pode estar na fila
            fg->Alimentar(m_ctx, t);
            m_fTique = MetaPeriodoMs() > 0.0 ? MetaPeriodoMs() : m_fTique;   // META: o periodo da meta
            ApresentarTex(t, false);
            p->Liberar(n);
            SetEvent(m_evtConsumiu);
            return;
        }
        FrameGen::Resultado r;
        /* X-GERADO (20/09/2026): o CUSTO DE CPU do Gerar, cronometrado. E ele que decide se da
           tempo de gerar antes de soltar o pendente (LimiarGerarMs) - nao o `p` do compositor,
           que so acontece DEPOIS do present. Inclui a copia de B, os passes, o despacho do FI e
           as esperas de fila que o Gerar faz na CPU; e o que de fato atrasa o pendente. */
        const double tGer0 = Ms();
        const bool gerou = fg->Gerar(m_ctx, t, n, (float)mediana, &r);
        { const double dg = Ms() - tGer0; m_gerCpuMs = m_gerCpuMs > 0.0 ? 0.9 * m_gerCpuMs + 0.1 * dg : dg; }
        p->Liberar(n);   // o Gerar ja copiou B para uma textura propria
        /* K-DEDUP §4, o ponto que de fato pega: o Gerar acabou de COLHER os vereditos das voltas
           anteriores, entao o do quadro passado existe agora - e o B dele ainda pode estar na fila
           esperando o prazo. Tirar aqui economiza o present de uma imagem que ja esta na tela. */
        DescartarRepetidos(fg, agora);
        /* X-FG-QUALIDADE (19/09/2026): QUADRO REPETIDO. O interpolador comparou B com o A anterior
           por conteudo e sao iguais (a captura do monitor entregou o mesmo quadro do jogo de novo -
           ver fg_ffx.h, CriarDedup). Nada foi gerado e NADA vai para a tela: o compositor ja mostra
           esta imagem (o B do par anterior saiu no prazo dele, antes de este laco pegar o quadro
           novo). Apresenta-lo de novo so mexeria no relogio do ritmo (m_ultPresentMs) e empurraria
           o G do proximo par. O intervalo entre quadros DISTINTOS e o que passa a valer para o ritmo
           e para o FI (ver MedianaChegada). */
        if (r.repetido) {
            m_repetidos++; m_repetidosTotal++; m_ultRepetidoMs = agora;
            SetEvent(m_evtConsumiu);
            return;
        }
        {   // distinto: o intervalo entre este e o distinto anterior, no relogio da CADEIA (Acordar)
            AcquireSRWLockShared(&m_srcLock);
            const double src = m_srcUlt;
            ReleaseSRWLockShared(&m_srcLock);
            if (m_dstUltSrc > 0.0 && src > m_dstUltSrc) {
                const double dt = src - m_dstUltSrc;
                if (dt > 1.0 && dt < 250.0) m_dstDts[m_dstN++ % 16] = dt;
            }
            if (src > 0.0) m_dstUltSrc = src;
        }
        // PAUSA DE "MENU": cena quase parada (quase nenhum bloco em movimento) por >= 1 s com
        // chegada alta -> so B. Interpolar menu/fade so produz franja em texto (tripletos do
        // Hogwarts: menu 31 dB / 1,7% de fantasma, o pior dos tres).
        if (r.nBlocos) {
            const bool parada = r.nMov < 0.02 * r.nBlocos && r.nDiffBig < 0.02 * r.nBlocos;
            if (parada) { if (m_paradaDesde == 0.0) m_paradaDesde = agora; } else m_paradaDesde = 0.0;
            // 45 fps aqui e o limiar de "chegada alta" da pausa de MENU, e nao tem relacao com
            // o gate: so se pula o quadro gerado numa cena parada quando ha quadros reais de
            // sobra. Era o antigo m_fpsVolta; virou constante nomeada quando o gate mudou.
            /* X-HUD-PISCA: com a FONTE PARADA (captura morta apos o watchdog, 12:14 do dono) a cadeia
               re-apresenta a mesma imagem a cada 500 ms: A == B, "parada", e a mediana (que ignora vaos)
               ainda dizia 115 fps -> virava "menu" com o jogo andando e o contador subia 480 vezes.
               Fonte parada ou quadro depois de um vao nao e menu: e vao. */
            m_menu = parada && (agora - m_paradaDesde >= 1000.0) && fps >= kMenuChegadaAlta && !m_fonteParada && !vaoAgora;
            m_gFlat += r.nFlat; m_gAmbig += r.nAmbig; m_gMask += r.nMask; m_gMov += r.nMov; m_gBlocos += r.nBlocos;
        }
        /* CADENCIA NO CORTE DE CENA EM MV=0 (item 3k, medido em 13/09/2026 - NAO e bug).
           Em duas placas quem detecta o corte e a GPU (fg_ffx.h, PassoCorteDecide) e a troca
           G := B tambem, depois do FI. A CPU nao fica sabendo a tempo: `r.corte` continua false,
           entao este ramo de cima roda e o B seguinte AINDA e enfileirado - o quadro real sai em
           DOIS presents seguidos, em vez de um so como fazia o caminho antigo do `r->corte`.
           POR QUE FICA ASSIM: os dois presents carregam a MESMA imagem, e o tempo total dela na
           tela e o mesmo nos dois caminhos (com chegada de 60 fps: B do instante t ate t+16,7 ms
           nos dois). Ou seja, o "quadro duplicado" nao se ve - o hitch de um quadro no corte e
           estrutural (dois quadros reais para tres lugares de present) e existe igual no caminho
           antigo, que em vez de repetir o present simplesmente nao apresentava nada naquele lugar.
           O unico custo real e UM present redundante por corte (~3% dos presents no teste
           sintetico de um corte a cada 2 s; em jogo, muito menos).
           POR QUE NAO CONSERTAR: pular o B exigiria ler o contador do corte de volta na CPU. O Map
           bloqueante custa 68,1 ms (armadilha 22); o Map DO_NOT_WAIT acerta so ~86% das vezes
           (24/28 cortes vistos no log, 13/09). Nos outros 14% a atribuicao sairia um quadro
           deslocada e o pulo cairia sobre um B de verdade - um quadro real NUNCA apresentado,
           que e um defeito PIOR que o de repetir uma imagem invisivelmente. */
        /* ======== X-FG-QUALIDADE: FILA DE UM QUADRO COM CADENCIA FIXA (ver FilaQuadros) ========
           Nada e apresentado AGORA: os gerados e o B entram na fila com prazos que continuam do
           ULTIMO PRAZO AGENDADO (m_ultAlvo), a um tique cada - o relogio e da fila, nao da chegada.
           Sem G (corte, menu): so o B, no lugar dele (base + K tiques); a tela fica um tique a mais
           com o quadro anterior, em vez de mostrar B adiantado. Vao (fonte parada): B sai ja e a
           cadencia recomeca. O tique = mediana entre distintos / K, preso a 1000/base/K quando a
           chegada esta a 8% da base pedida ao jogo, vezes o ajuste do momentum (0,85..1,20). */
        if (m_fila > 0 && !m_pausada && !vaoAgora && r.B) {   // (sem r.B o interpolador nao copiou: a textura da ponte nao pode esperar na fila)
            const bool comG = gerou && !r.corte && !m_menu;
            if (r.corte) m_cortes++;
            if (m_menu) m_menus++;
            const int nG = comG ? (r.nG > 0 ? r.nG : 1) : 0;
            const int K = comG ? nG + 1 : std::max(2, m_multEf);
            LerTela(agora);
            MedirC(fg);   // `c`: o tempo de GPU do par, dos timestamps do proprio FI
            /* K-RITMO C2 (o defeito K3): a TAXA vem da TELA, nao da mediana da chegada. Com base
               pedida ao jogo e a chegada a 25% dela (era 8% - fora dos 8% o tique voltava a
               "da mediana" justamente nos jogos que mais precisam), o tique e 1000/(base*K)
               quantizado a um numero INTEIRO de vblanks: a 30 fps em 240 Hz, 4 vblanks exatos.
               So quando o arredondamento cabe em 5% do pedido - 30 fps em 100 Hz nao e
               comensuravel (1,67 vblank) e ali vale o tique pedido, sem ancora. */
            /* RITMO-3X: a TAXA nao pode herdar o ruido da mediana. A mediana e de 16 chegadas e
               a fonte chega em VBLANKS: com 30 fps em 240 Hz cada intervalo e 8 ou 9 vblanks, e a
               mediana salta entre 33,33 e 37,50 ms - 12% de erro de taxa, QUATRO VEZES a
               autoridade da fase (T/32 = 3% por par). MEDIDO: `tique 10,16 ... 12,50` na mesma
               corrida, com a profundidade da fila passeando de 8 a 32 ms atras dele. A EMA de
               ~20 chegadas tira o degrau do vblank e deixa a taxa media, que e o que a fila
               precisa; a mediana crua continua valendo para todo o resto (vaos, gate, `j`). */
            m_medTique = m_medTique > 0.0 ? 0.95 * m_medTique + 0.05 * mediana : mediana;
            double tique = m_medTique / (double)K;
            m_fPreso = false; m_tiqueComens = false;
            /* ===== O1-TELA: o tique E UM VBLANK, e quantos quadros saem deste intervalo =====
               Aqui a cadencia deixa de ser "K partes iguais do intervalo da fonte" e passa a ser
               "um present por vblank". Quantos vblanks este intervalo leva sai de um acumulador:
               `acc += R; m = floor(acc); acc -= m`. A 40 fps em 100 Hz (R = 2,5) isso da
               2, 3, 2, 3... - media 2,5 exata, sem erro que se acumule. O tique de um vblank e
               comensuravel por definicao, entao a ancora do C3 vale de graca.
               O teto `kTelaMax` e o que o FI consegue dar: 3 gerados + o real = 4 por intervalo.
               ACIMA DO TETO O MODO SAI DE CENA, e isto foi medido (21/09, alvo a 40 e 51 fps
               numa tela de 240 Hz, R = 5,8 e 4,8): empilhar os 4 quadros nos 4 primeiros vblanks
               e deixar os outros 1,8 vazios deu `fora de hora 70-80%` e 99-130 imagens
               distintas/s - pior que o 4x comum, que reparte o intervalo em quatro PARTES IGUAIS
               (tique = T/4) e mede 107-120. Entao com R > kTelaMax o intervalo vai pelo caminho
               de sempre com N = 4: os mesmos 4 quadros, espacados por igual. A tela nao enche
               (nao ha gerado para isso), mas a cadencia fica certa. */
            /* META (21/09/2026): o teto passou a ser 8 (7 gerados, 3o contexto) e o que decide se
               este intervalo vai pelo acumulador e se R CABE no N que esta valendo (`m_metaRef`,
               ja cortado pelo custo): acima disso o intervalo vai pelo caminho de N partes iguais,
               que e o que o O1 mediu como melhor quando nao ha gerado para encher. O tique e o
               periodo da META (1000/meta): igual ao vblank quando a meta e a Tela; com meta 120
               numa tela de 240 Hz sao 2 vblanks exatos (comensuravel, ancora vale); com meta 100
               em 240 Hz sao 2,4 vblanks - nao comensuravel, sem ancora, e o present cai no
               vblank mais proximo (e o preco de uma meta que nao divide o refresh; a dica diz). */
            int mTela = 0;
            m_telaAtivo = false;
            const double perMeta = MetaPeriodoMs();
            if (m_modoTela && perMeta > 0.0 && m_metaGerando && m_telaR <= m_metaRef + 0.25 && comG) {
                m_telaAcc += m_telaR;
                mTela = (int)floor(m_telaAcc + 1e-9);
                m_telaAcc -= (double)mTela;
                if (mTela < 1) { mTela = 1; m_telaAcc = 0.0; }   /* META: com R entre 1,25 e 2 ha intervalos so com o B (1, 1, 2...) */
                if (mTela > kTelaMax) { mTela = kTelaMax; m_telaAcc = 0.0; }
                if (mTela > nG + 1) mTela = nG + 1;   // so ha nG gerados para os vblanks de dentro
                tique = perMeta;
                m_tiqueComens = false; m_tiqueVb = 0.0;
                if (m_telaPeriodoMs > 0.0) {
                    const double vb = perMeta / m_telaPeriodoMs, nv = floor(vb + 0.5);
                    if (nv >= 1.0 && fabs(nv - vb) <= 0.05 * vb) { tique = nv * m_telaPeriodoMs; m_tiqueComens = true; m_tiqueVb = nv; }
                }
                m_tiqueAj = 1.0; m_erroF = 0.0;
                m_telaAtivo = true;
            }
            /* O1-TELA: no modo Tela o tique JA e o vblank - a taxa presa a base, a quantizacao e o
               momentum abaixo sao todos maneiras de achar um tique que ali ja esta achado. */
            if (!m_telaAtivo) {
            if (m_baseFps > 0.0) {
                const double pb = 1000.0 / m_baseFps;
                if (fabs(mediana - pb) < 0.25 * pb) { tique = pb / (double)K; m_fPreso = true; }
            }
            /* X-GERADO (20/09/2026): A QUANTIZACAO A VBLANKS VALE TAMBEM COM O TIQUE DA MEDIANA.
               Ela estava DENTRO do "preso a base", e a base so prende quando a chegada bate com o
               que se pediu ao jogo. No Universal ninguem atende esse pedido: com o alvo a 30 fps o
               log dizia `base 120 Hz` (o pedido) contra `mediana 33,3 ms` (a entrega), 4x de
               distancia, entao o tique nunca era ancorado - e nada disso tem a ver com a TELA, que
               e quem de fato mostra o quadro. A 30 fps numa tela de 240 Hz o tique tem de ser
               16,67 ms = 4 vblanks exatos, venha o numero da base pedida ou da mediana medida. */
            /* E ELA SEGURA (histerese em VBLANKS). A mediana da chegada e de 16 amostras de uma
               fonte que treme (o WGC entrega em 7, 8, 11 ou 12 vblanks - ver `chegada(carimbo)`),
               e um solavanco dela nao pode desfazer uma cadencia que ja esta caindo no vblank:
               MEDIDO, o tique escorregava para 15,4-15,6 ms em 3 de 14 janelas porque a mediana
               pulou para 29,2 ms por um instante. ENTRA com 5% (so uma taxa de fato comensuravel
               entra - 30 fps em 100 Hz da 1,67 vblank e continua de fora, como sempre) e SEGURA
               ate 0,65 vblank de distancia; alem disso, larga e volta ao tique da mediana. */
            /* ===== O1 (20/09/2026): O 3x FOI ATACADO AQUI, MEDIDO, E NAO FICOU =====
               O tique de 3x a 30 fps numa tela de 240 Hz e 33,33/3 = 11,11 ms = 2,667 vblanks:
               nao passa nos 5% acima, a ancora desliga e o momentum volta. Foram tentadas DUAS
               coisas, nesta ordem, cada uma medida por 40 s no alvo64 (8 janelas de 5 s):

               1) ancorar o PAR em vez do tique (T = 8 vblanks exatos) e puxar cada prazo de
                  dentro ao vblank mais proximo - 2,667 / 5,333 / 8 vira 3 / 5 / 8, ou seja
                  passos de 3, 2 e 3 vblanks. O passo de 2 vblanks (8,33 ms) tem tolerancia de
                  25% = 2,08 ms, menos do que custa atender uma chegada no meio dele.
                  MEDIDO: 31,3% dos G fora de hora, 74,8 imagens distintas/s de media.
               2) o mesmo, com os prazos de dentro nos TERCOS EXATOS (2,667 vblanks de distancia
                  ja garantem composicoes diferentes; nao e preciso cair EM vblank), e com o
                  teste do par vencendo a histerese do `m_tiqueVb` - que estava latindo em 3
                  vblanks e segurando `tique 12,50 ms` em 27 de 41 segundos, pedindo 80
                  presents/s contra os 90 que nascem.
                  MEDIDO: 42,4% fora de hora, 68,2 imagens distintas/s.

               Contra a BASE (este codigo, sem mexer): 21,4% fora de hora, 77,4 distintas/s.
               As duas pioraram, e o motivo esta na equacao, nao na quantizacao: `D = c + j` com
               teto de 0,80 do TIQUE. A 2x, D = 13,33 ms e `j` = 12,5 ms (o tremor do WGC) -
               cabe, e por isso 2x e limpo. A 3x o teto cai para 0,80 x 11,11 = 8,89 ms e `j`
               continua 12,5: a fila mira uma folga 5,4 ms MENOR do que o tremor da fonte, entao
               ~5% das chegadas a furam e a cadencia entra no estado degenerado de `erro mediano
               11,11 ms` (um tique inteiro: os presents saem em pares). O `tique 12,50` latido
               pela histerese escondia isso baixando a taxa para 80/s - errado, mas estavel.
               O QUE FALTA, com nome e numero: a fase e medida por `folgaChegada`, a distancia
               ate o PROXIMO pendente, que por construcao nunca passa de um tique - entao a 3x
               nenhum alvo acima de 11,11 ms e alcancavel e `D` e cortado antes de poder pedir a
               folga que a maquina precisa. Medir a profundidade da fila por `m_ultAlvo - agora`
               (quanto a cadencia corre adiante, que nao tem teto de um tique) resolveria os dois
               multiplicadores com a mesma equacao. E mexer no C1 do K-RITMO, que vale para 2x e
               4x tambem: fica APONTADO para quem cuida do ritmo, nao feito as pressas aqui.
               O 4x nao tem nada disto: 8,33 ms = 2 vblanks exatos, e mede 107-120 distintas/s. */
            if (m_telaPeriodoMs > 0.0 && tique > 0.0) {
                const double vb = tique / m_telaPeriodoMs;
                const double nv = std::max(1.0, floor(vb + 0.5));
                if (fabs(nv - vb) <= 0.05 * vb) { tique = nv * m_telaPeriodoMs; m_tiqueComens = true; m_tiqueVb = nv; }
                /* RITMO-3X: a histerese de saida nao pode impor um ERRO DE TAXA maior do que a
                   tolerancia de entrada. Com a banda fixa de 0,65 vblank o tique de 3x (2,667
                   vblanks) prendia em 3 vblanks = 12,50 ms contra os 11,11 ms da fonte - 12,5% de
                   taxa a mais, que a fase teria de desfazer todo par. MEDIDO na base: `fila 4,3`,
                   `recomeco 1-2` por segundo e o colapso assim que o trinco solta. Entra a 5% (o
                   teste acima), sai a 10%: histerese de verdade, e proporcional ao proprio vb. */
                else if (m_tiqueVb >= 1.0 && fabs(vb - m_tiqueVb) <= std::min(0.65, 0.10 * vb)) { tique = m_tiqueVb * m_telaPeriodoMs; m_tiqueComens = true; }
                else m_tiqueVb = 0.0;
            }
            /* K-RITMO C1/P1 (o defeito K2): momentum CONTINUO, e so quando o tique vem da mediana.
               Preso a base, a taxa ja e a da tela e mexer nela tiraria a comensurabilidade (e a
               correcao que falta ali e de FASE, logo abaixo). O sinal deixa de ser o binario
               +-1% por chegada (que oscilava 0,88 <-> 1,17 sem nunca acertar) e passa a ser o
               erro EM TEMPO, suavizado por EMA de ~5 chegadas, com faixa 0,90-1,10. */
            /* X-GERADO: o momentum sai de cena tambem quando o tique e COMENSURAVEL com a tela, pela
               mesma razao ja escrita para o preso a base - a taxa ali ja e a da tela, e mexer nela
               desfaz a comensurabilidade; o que falta e FASE, corrigida logo abaixo. MEDIDO no alvo
               a 30 fps: sem isto o `m_tiqueAj` ia ao teto de 1,10 e ficava la, e o tique de 16,67
               virava 18,33 ms - que nao e multiplo de 4,167 e nunca cai num vblank.
               E POR QUE ELE IA AO TETO: com o tique longo a fila entope, bate na CAPACIDADE (o par
               de dois Gerar atras) e e DESPEJADA na hora (`estouro`). A chegada seguinte acha a fila
               vazia - mas vazia porque NOS a esvaziamos, nao porque o tique esta curto - e o
               momentum le isso como "alongar mais 1%". Realimentacao positiva. Por isso a chegada
               logo depois de um estouro nao alimenta mais o momentum. */
            if (!m_fPreso && !m_tiqueComens) {
                const uint64_t estouros = m_fEstouroTot;
                if (profVale && tique > 0.0 && estouros == m_fEstouroAnt) {
                    /* RITMO-3X: o momentum passa a ler o MESMO erro da fase - a profundidade da
                       fila contra `D` -, so que integrado devagar. Ate aqui ele mirava
                       `folga = tique/2`, um alvo DIFERENTE do da fase: os dois controladores
                       puxavam para lados opostos e, fora da comensurabilidade (que e o caso do
                       3x), o momentum ia ao trilho de 0,90 e ficava la. MEDIDO na base: `ajuste
                       0,90` nas ultimas 12 janelas, com `G fora de hora` de 40-90% e `B atrasado
                       p95 30 ms`. Com o mesmo alvo nos dois, a fase corrige por par e ao momentum
                       sobra so a deriva lenta entre o relogio da fonte e o nosso. `m_dMs` e o D
                       do par ANTERIOR: D depende do tique, que e o que esta sendo ajustado aqui. */
                    const double erro = (profFila - m_dMs) / (tique * (double)K);   // > 0 = fila funda demais: encurtar
                    /* RITMO-3X: a EMA era de ~5 chegadas e a AUTORIDADE do momentum (+-10% do
                       tique) e muito maior que a da fase (T/32 por par): ele respondia ao mesmo
                       transitorio que a fase ja estava apagando e passeava entre 0,98 e 1,10
                       (MEDIDO, `vazia 27-30` por segundo). O que sobra para a taxa e so a DERIVA
                       LENTA entre o relogio da fonte e o nosso - ppm -, entao a EMA vai para ~50
                       chegadas. E ainda assim ele NAO mexe mais na taxa, e este e o desvio a
                       registrar: um erro de PROFUNDIDADE nao se corrige mudando a TAXA - isso e
                       integrar duas vezes, e o resultado medido foi sempre o trilho (`ajuste
                       0,90` ou `1,10`, 40-90% de G fora de hora). Quem corrige profundidade e a
                       FASE, por par. A taxa vem da mediana suavizada acima, e a fase tem T/32 =
                       3% por par de autoridade - mais do que a mediana erra. O sinal continua
                       calculado e contado em `cheia`/`vazia`, que e o diagnostico de fila funda
                       ou seca. */
                    m_erroF = 0.98 * m_erroF + 0.02 * erro;
                    /* A TAXA sai do que a FASE vem empurrando. Se a cadencia precisa ser puxada
                       para o mesmo lado par apos par, o tique esta errado nessa exata medida:
                       a media de `aplica` (o deslize aplicado, em fracao do intervalo) E o erro
                       de taxa, e so ele - um desvio de profundidade que a fase ja apagou nao
                       aparece aqui. POR QUE PRECISA EXISTIR: a mediana da chegada e enviesada
                       para BAIXO, porque um vao da captura e descartado (> 250 ms) mas as
                       chegadas curtas da rajada que vem depois dele contam. MEDIDO sem esta
                       correcao: `T 29,97` contra os 33,33 da fonte, a fila secando (`prof
                       -5,98 ms`, `vazia 24-30` por segundo) e 37,8% de G fora de hora. O ganho
                       de 5% por par fecha em ~1 s, bem mais devagar que a fase. */
                    m_tiqueAj *= (1.0 - 0.05 * std::min(1.0, std::max(-1.0, m_apF)));
                    if (m_erroF > 0.25) m_fCheia++; else if (m_erroF < -0.25) m_fVazia++;
                }
                m_fEstouroAnt = estouros;
                m_tiqueAj = std::min(1.10, std::max(0.90, m_tiqueAj));
                tique *= m_tiqueAj;
            } else { m_tiqueAj = 1.0; m_erroF = 0.0; m_fEstouroAnt = m_fEstouroTot; }
            }   // fim do "se nao e o modo Tela"
            m_fTique = tique;
            /* K-RITMO C1: travar a FASE, com a EQUACAO DA MAQUINA (20/09/2026, pedido do dono:
               "uma equacao que sempre ponha o quadro no lugar certo em qualquer maquina").
               A chegada do quadro n tem de achar o pendente a `D` de sair, e D nao e um numero
               em ms escolhido a mao:

                   D = c + j, entre kDmin e kDmax do TIQUE

               (ate 20/09/2026 era `T/2 + c + p + j` com teto em T - inalcancavel; o porque da
               mudanca esta no bloco grande logo abaixo, onde D e calculado.)
               `j` porque a fonte nao chega em hora exata e a cauda dela e quem seca a fila; `c`
               porque o par so existe depois que a GPU o fez. Numa placa rapida com fonte em fase
               D encolhe sozinho (menos latencia); numa lenta, ou com uma captura que treme, ele
               cresce - que e o unico jeito de a cadencia nao quebrar. TETO E PISO em fracao do
               TIQUE, que e a profundidade real da fila (o pendente mais proximo esta sempre a
               menos de um tique).
               O erro anda kFaseGanho por par, com teto de T/32 (3% de meio intervalo: abaixo da
               tolerancia de 25%, invisivel); converge em ~10 pares. Fora de T/2 por kFasePulo
               pares seguidos nao se resolve aos poucos: vai um PULO, a correcao inteira de uma
               vez (a regra 2 do LS - pular, nunca encurtar aos poucos). DESVIO ANOTADO: o §6.4
               sugere pular o G do par seguinte; saltar a fase toda da o mesmo resultado sem
               perder um quadro gerado. */
            /* ===== X-GERADO (20/09/2026): D TEM DE CABER NA FILA, E `p` NAO E FILA =====
               O QUE ESTAVA ERRADO. `folgaChegada` e a distancia ate o PROXIMO pendente sair, e os
               pendentes de um par moram em `base + tique*1 .. base + tique*K`. Seja qual for o K,
               o pendente mais proximo esta SEMPRE a menos de UM TIQUE: `folgaChegada` nunca passa
               de 16,67 ms com a fonte a 30 fps. O alvo era `D = T/2 + c + p + j` = 16,7 + 0,7 +
               21,4 + 12,5 = 51 ms, cortado no teto em 26,67 - ainda 60% acima do maximo que a fila
               pode mostrar. Alvo inalcancavel: o erro de fase ficava preso em -26 ms, o
               controlador deslizava a cadencia todo par sem nunca chegar, e a cada kFasePulo
               pares dava um PULO que joga G e B na mesma composicao. MEDIDO: `fase 25,7-26,7 ms,
               pulos 3-4` por janela, e a janela com `G fora de hora 89,3%` caindo a 39 imagens
               distintas/s com `estouro 0` - nao era capacidade nem tique, era isto.

               POR QUE `p` E `T/2` SAIRAM, e nao a fila que cresceu. `p` (present -> scanout) e um
               atraso DEPOIS do present: ele desloca por igual TODOS os quadros, entao se cancela
               na diferenca que a cadencia mede e nao pede fila nenhuma - so latencia. `T/2` era a
               posicao do gerado, que ja esta no proprio `tique`. Guardar mais pares para alcancar
               um D de 51 ms custaria 4 slots de inB/inBFi/outG em resolucao cheia e ~66 ms de
               atraso a mais numa funcao cuja razao de existir e fluidez sem latencia - e nao
               melhoraria o ritmo em nada, porque o que inflava D nao era ritmo.

               O QUE SOBRA e o que de fato precisa de folga na chegada: `j`, o quanto a fonte
               chega fora de hora (a cauda dela e quem seca a fila), e `c`, o tempo de GPU do par
               que esta nascendo. Piso e teto em fracao do TIQUE - a profundidade real da fila -,
               nunca em ms escolhido a mao. Com isso |erro de fase| < um tique por construcao, e o
               PULO (que exige erro acima de T/2 por kFasePulo pares) deixa de disparar sozinho. */
            const double intervalo = tique * (double)K;
            /* RITMO-3X: o TETO de D e fracao do INTERVALO, nao do tique. O tique era o teto certo
               enquanto a fase se media por `folgaChegada` (que nunca passa de um tique); medida a
               profundidade, o que a fila alcanca de verdade e um intervalo - com kSlots = 3 os
               pendentes do par `n-1` so precisam ter saido antes da chegada de `n+1`, que e um
               intervalo depois (quem garante e a CAPACIDADE, acima). Com o teto no tique, a 3x D
               valia 0,80 x 11,11 = 8,89 ms contra `j` = 12,5 ms do WGC: a fila mirava uma folga
               MENOR que o tremor da fonte, e ~5% das chegadas a furavam. Agora `D = c + j` cabe
               igual em 2x, 3x e 4x - e e disso que se trata: D e da maquina, nao do multiplicador. */
            const double Dalvo = std::min(std::max(m_cMs + m_jMs, kDmin * tique), kDmax * intervalo);
            /* SATURACAO: quem manda na folga e a REGRA DE SOLTURA. O pendente e solto assim que a
               chegada o encontra a menos de LimiarGerarMs() de sair, entao enquanto esse limiar
               for maior que o alvo a folga na chegada e ZERO por construcao e nao ha fase a
               corrigir. Nesse regime D vira 0 e o controlador fica QUIETO, em vez de empurrar a
               cadencia atras de um alvo que a regra de soltura nao deixa existir - era esse
               empurrao sem fim que acabava no PULO que junta G e B na mesma composicao. Com uma
               fonte muito tremida (j grande) ou uma tela direta (p pequeno) o alvo passa o limiar
               e o controlador volta a ter o que fazer, sozinho. */
            /* RITMO-3X: D e um ALVO, e alvo que se mexe e perturbacao. `j` e o p95 numa janela
               de 2 s e salta de 12,5 para 27 ms quando o WGC engasga - MEDIDO nesta bancada, e a
               cada salto o controlador arrastava a cadencia inteira atras do D novo (`fase`
               15-20 ms nas janelas do engasgo, contra 4 nas limpas).
               A REGRA QUE FIXA A EMA: o alvo nao pode andar mais depressa do que a fase consegue
               segui-lo. A fase anda no maximo `T/32` = 1,04 ms por par; com 0,02 o alvo anda no
               maximo 0,02 x (teto de D) = 0,5 ms por par, metade disso. Com a EMA de 0,9/0,1 que
               estava aqui ele andava 2,6 ms por par - a fase saturava, deixava de ser um erro de
               fase e virava transporte, e o laco da TAXA (abaixo) aprendia o transitorio como se
               fosse deriva: MEDIDO, `j` saltou para 29 ms, `ajuste` foi a 1,07 e a profundidade
               passou de 12 para 32 ms na janela seguinte, com 14% de G fora de hora. */
            m_dAlvo = m_dAlvo > 0.0 ? 0.98 * m_dAlvo + 0.02 * Dalvo : Dalvo;
            const double D = m_dAlvo > LimiarGerarMs() ? m_dAlvo : 0.0;
            m_dMs = D; m_tMs = intervalo;
            /* RITMO-3X: so ha fase enquanto a cadencia CONTINUA. Fora destes limites o `base`
               logo abaixo recomeca do zero, e medir fase contra um prazo que vai ser jogado fora
               so sujaria a media e ainda podia disparar um pulo. Os limites sao os do recomeco. */
            if (profVale && intervalo > 0.0 && profFila > -1.5 * mediana && profFila < 2.0 * mediana) {
                const double erroFase = profFila - D;
                m_faseErroAcc += fabs(erroFase); m_faseN++;
                m_profAcc += profFila; m_profN++;
                if (fabs(erroFase) > 0.5 * intervalo) m_faseForaN++; else m_faseForaN = 0;
                double desliza;
                if (m_faseForaN >= kFasePulo) {
                    desliza = erroFase; m_fasePulos++; m_faseForaN = 0;
                    if (m_log) m_log("apresentador: fase: pulo de %.2f ms (erro acima de T/2 = %.2f ms por %d pares)", desliza, 0.5 * intervalo, kFasePulo);
                } else {
                    const double teto = intervalo / 32.0;
                    desliza = std::min(teto, std::max(-teto, kFaseGanho * erroFase));
                }
                /* X-GERADO: a correcao de fase tem de SOBREVIVER a ancora do vblank. `base` passa
                   por AoVblank todo par, e um deslize menor que meio periodo da tela (2,08 ms a
                   240 Hz) volta ao mesmo vblank - e o teto do deslize e T/32 = 1,04 ms, MENOR que
                   isso. A correcao era desfeita toda vez e o erro ficava parado; o unico que
                   sobrevivia era o PULO, e ele e que jogava G e B na mesma composicao.
                   Agora o deslize se ACUMULA e a cadencia anda um vblank INTEIRO de cada vez, que
                   e o unico passo que uma cadencia ancorada pode dar. Sem ancora, o de sempre. */
                double aplica = desliza;
                if (AncoraAtiva()) {
                    /* ZONA MORTA DE UM VBLANK. Com a cadencia ancorada, o menor passo de fase que
                       existe e um vblank (4,167 ms a 240 Hz) - e um vblank ja e 25% do tique de
                       16,67, exatamente a tolerancia do "fora de hora". Corrigir um erro menor que
                       isso troca um desvio invisivel por um present deslocado de 25%: o proprio
                       conserto vira o defeito. Abaixo de um vblank de erro a fase ja esta tao
                       certa quanto a tela permite, e o acumulador volta devagar para zero. */
                    if (fabs(erroFase) <= m_telaPeriodoMs) m_faseAcc *= 0.5; else m_faseAcc += desliza;
                    if (fabs(erroFase) <= m_telaPeriodoMs)            aplica = 0.0;
                    else if (m_faseAcc <= -0.5 * m_telaPeriodoMs)    { aplica = -m_telaPeriodoMs; m_faseAcc += m_telaPeriodoMs; }
                    else if (m_faseAcc >= 0.5 * m_telaPeriodoMs)     { aplica =  m_telaPeriodoMs; m_faseAcc -= m_telaPeriodoMs; }
                    else aplica = 0.0;
                }
                for (int i = m_pendI; i < m_pendN; i++) m_pend[i].alvo -= aplica;
                m_ultAlvo -= aplica;
                m_apF = 0.95 * m_apF + 0.05 * (aplica / intervalo);   // alimenta a taxa (ver o momentum)
            }
            double base = m_ultAlvo;
            if (base <= 0.0 || agora - base > mediana * 1.5 || base - agora > mediana * 2.0) {
                base = agora; m_fRecomeco++; m_erroF = 0.0; m_faseForaN = 0; m_faseAcc = 0.0; m_apF = 0.0;
            }
            base = AoVblank(base);   // K-RITMO C3: a cadencia inteira nasce de um vblank do DWM
            /* C0 deixou o pendente na fila: sem compactar, m_pendN andaria ate o fim do vetor e
               o estouro abaixo esvaziaria a fila a cada oito pares. */
            PendCompactar();
            if (m_pendN + std::max(nG + 1, mTela) > (int)(sizeof(m_pend) / sizeof(m_pend[0]))) FilaEsvaziarAgora();   // nunca acontece com a capacidade acima; garantia
            const uint64_t par = fg->Gerados();
            if (m_telaAtivo) {
                /* ===== O1-TELA: um pendente por vblank, cada um com a fracao mais proxima =====
                   Os `mTela - 1` vblanks de dentro do intervalo queriam o quadro nas fracoes
                   1/R, 2/R, ... Entre as fracoes que o FI de fato produziu ({1/4, 1/2, 3/4} em
                   4x; so {1/2} em 2x) escolhe-se a mais proxima, e NUNCA a mesma duas vezes: dois
                   pendentes com a mesma textura seriam dois presents e UMA imagem na tela - o
                   `imagens distintas` cairia e o trabalho do segundo iria fora. Por isso o
                   `usado[]`: o segundo vblank pega a melhor das que sobraram, nao a melhor.
                   O erro de fracao vai para o log (`fracao` na linha `tela-modo`): e o preco
                   deste modo, e tem de ser um numero que se possa ler, nao uma promessa. */
                bool usado[7] = {};   /* META: ate 7 gerados (8x) */
                for (int k = 1; k <= mTela - 1; k++) {
                    const double f = (double)k / m_telaR;
                    int melhor = -1; double dist = 1e9;
                    for (int i = 0; i < nG && i < 7; i++) {
                        if (usado[i]) continue;
                        const double d = fabs((double)r.fracs[i] - f);
                        if (d < dist) { dist = d; melhor = i; }
                    }
                    if (melhor < 0) break;   // acabaram os gerados: o resto do intervalo e o B
                    usado[melhor] = true;
                    m_telaErrF += dist; m_telaErrN++;
                    Pend& pd = m_pend[m_pendN++];
                    pd.t = r.Gs[melhor]; pd.gerado = true; pd.passo = tique; pd.alvo = base + tique * k; pd.par = par; pd.idFonte = n;
                }
                { Pend& pd = m_pend[m_pendN++]; pd.t = r.B; pd.gerado = false; pd.passo = tique; pd.alvo = base + tique * mTela; pd.par = par; pd.idFonte = n; }
                m_ultAlvo = base + tique * mTela;
                m_telaVb += mTela; m_telaInt++;
            } else {
                for (int i = 1; i <= nG; i++) { Pend& pd = m_pend[m_pendN++]; pd.t = r.Gs[i - 1]; pd.gerado = true; pd.passo = tique; pd.alvo = base + tique * i; pd.par = par; pd.idFonte = n; }
                { Pend& pd = m_pend[m_pendN++]; pd.t = r.B; pd.gerado = false; pd.passo = tique; pd.alvo = base + tique * K; pd.par = par; pd.idFonte = n; }
                m_ultAlvo = base + tique * K;
            }
            SetEvent(m_evtConsumiu);
            return;
        }
        if (m_fila > 0) { FilaEsvaziarAgora(); }   // pausada / vao: o B sai ja, abaixo, e a cadencia recomeca
        if (gerou && !r.corte && !m_pausada && !m_menu && !vaoAgora) {
            /* G NO RELOGIO, NAO NA CHEGADA. Ate 18/09/2026 o G saia no instante em que B
               chegava, e o B meio intervalo depois: o jitter da chegada (a cadeia oculta, a
               captura) ia inteiro para a tela. Agora G mira `ultimo present + meio intervalo`
               - se a chegada veio ADIANTADA, G espera o pouco que falta (nunca mais que meio
               intervalo, e o proximo quadro que chegar no meio fica anotado); se veio atrasada,
               sai ja (sem folga de quadro nao ha como esconder atraso - isso e a leva da
               "folga de um quadro", descrita no relatorio). E o esquema do FSR: cada quadro
               fica o mesmo tempo na tela. */
            /* X-FG-6 (3x/4x): o intervalo e repartido em K partes iguais (K = 1 + gerados que
               vieram): o primeiro gerado sai agora (no relogio), os outros e o B entram na fila,
               cada um a um passo do anterior. Em 2x, K = 2 e `passo` e o meio intervalo de sempre. */
            const int nG = r.nG > 0 ? r.nG : 1;
            const double passo = mediana / (double)(nG + 1);
            const bool emRitmo = m_ultPar && (m_ultPresentMs > 0.0) && (agora - m_ultPresentMs) < mediana * 1.5;   // o present anterior era o B do par passado
            if (emRitmo) {
                /* X-HUD-PISCA: a ancora so vale a folga que a CHEGADA deu. Se o quadro ja esperava
                   por nos (a cadeia o leu ha `idade` ms - o apresentador esta atras), a espera do G
                   encolhe dessa idade: a volta inteira (G + B) volta a caber num intervalo da fonte e
                   o laco nao fica um passo atras dela para sempre (ver Acordar()). Quadro que chegou
                   com o apresentador ocioso tem idade ~0 e a ancora fica como o X-FG-VIDEO a fez. */
                const double idade = IdadeDoNovoMs();
                double alvoG = m_ultPresentMs + passo;
                if (idade > 0.5) { alvoG -= std::min(idade, passo); m_rIdadeMs += std::min(idade, passo); m_rIdadeN++; }
                const double faltaG = alvoG - Ms();
                if (faltaG > 0.25 && faltaG <= passo) { m_rWake += EsperarAte(alvoG, true); m_rWakeN++; m_rEspGMs += faltaG; m_rEspG++; }
            }
            ApresentarTex(r.nG > 0 ? r.Gs[0] : r.G, true, emRitmo ? passo : 0.0);
            PendLimpar();
            for (int i = 1; i < nG; i++) { Pend& pd = m_pend[m_pendN++]; pd.t = r.Gs[i]; pd.gerado = true; pd.passo = passo; pd.alvo = m_ultPresentMs + passo * i; }
            { Pend& pd = m_pend[m_pendN++]; pd.t = r.B; pd.gerado = false; pd.passo = passo; pd.alvo = m_ultPresentMs + passo * nG; }
        } else {
            if (r.corte) m_cortes++;
            if (m_menu) m_menus++;
            if (vaoAgora) m_vaos++;
            ApresentarTex(r.B ? r.B : t, false);
        }
        SetEvent(m_evtConsumiu);
    }
public:
    // telemetria para o painel (lida na thread do laco, uma vez por segundo)
    bool   InterpolacaoAtiva() const { return m_interpAtiva.load(); }
    bool   Pausada() const { return m_pausadaAt.load(); }
    /* Por que esta pausado (0 = nao esta, 1 = nao compensa, 2 = chegada abaixo do piso). */
    int    MotivoPausa() const { return m_motivoAt.load(); }
    /* O que o gate usou na ultima decisao: apresentados/s, a entrega estimada sem FG e se ela
       foi MEDIDA numa pausa (para o painel poder dizer a verdade em vez de "chegada baixa"). */
    double ApresentadosFps() const { return m_apresFps; }
    /* A entrega que a pessoa teria SEM o FG, AGORA. Pausada, a cadeia ja esta sem FG: e a
       propria chegada. Ativa, desconta o custo do FG do intervalo de agora - nunca o
       `m_offFps` congelado de uma pausa antiga (era o latch; ver Gate()). */
    double SemFgFps() const {
        if (m_pausada) return m_chegadaFps.load();
        const double f = m_chegadaFps.load(); if (f <= 0.0) return 0.0;
        const double tOn = 1000.0 / f;
        return (m_custoFgMs > 0.0 && tOn > m_custoFgMs + 1.0) ? 1000.0 / (tOn - m_custoFgMs) : 0.0;
    }
    /* Pausada: o que RETOMAR renderia na tela (2 quadros por chegada, descontado o custo do FG) -
       o mesmo numero que a perna economica exige contra a margem para voltar. Ativa: o medido. */
    double ApresPrevistoFps() const {
        if (!m_pausada) return m_apresFps;
        const double f = m_chegadaFps.load(); if (f <= 0.0) return 0.0;
        const double tOn = 1000.0 / f, c = m_custoFgMs > 0.0 ? m_custoFgMs : 0.0;
        return f * GanhoPrevisto(tOn, c);   // respeita o teto de apresentacao medido
    }
    bool   SemFgMedido() const { return m_custoFgMs > 0.0; }
    /* A fonte esta parada (menu/pausa do jogo: nada chega e o gate esta congelado)? Lido na
       thread do laco principal; e um bool, a leitura torta so atrasa um quadro. */
    bool   FonteParada() const { return m_fonteParada; }
    double ChegadaFps() const { return m_chegadaFps.load(); }
    /* X-FG-QUALIDADE: com a fila de um quadro o B sai ~um intervalo depois da chegada (nao meio). */
    double LatenciaEstimadaMs(double fiMs) const { const double f = m_chegadaFps.load(); return (f > 0.0 ? (m_fila > 0 ? 1000.0 : 500.0) / f : 0.0) + fiMs; }
private:

    /* ---------------- RESET-CURSOR: O PONTEIRO COM A TELA PARADA ----------------
       Com o ponteiro desenhado na apresentacao, ele so anda quando o apresentador apresenta. Com o
       conteudo PARADO (desktop, menu, video em pausa: a deduplicacao da captura segura as copias e nada
       chega pela ponte) o ponteiro ficaria congelado. Entao, sem quadro real ha >= 50 ms, o apresentador
       RE-APRESENTA o ultimo quadro (a copia sem ponteiro) com a seta na posicao de agora, no maximo um
       por refresh e so quando o ponteiro mexeu. Com conteudo andando (>= 20 fps) nada disso roda: a seta
       vai em cada quadro real, com a posicao do instante do Present - re-apresentar entre dois quadros
       reais roubaria o vblank do proximo real (a cadencia e o 2x do FG contam com ele). */
    static bool PonteiroNaApresentacao() {
        return Ponteiro::g_desenhoOnde.load(std::memory_order_relaxed) == Ponteiro::DESENHO_APRESENTACAO && Ponteiro::g_modo != Ponteiro::MODO_ESCONDER;
    }
    // m_ultimo = o quadro LIMPO (sem seta; e dele que sai a re-apresentacao com a tela parada); m_comp = quadro + seta.
    bool GarantirCopias(const D3D11_TEXTURE2D_DESC& bd) {
        auto garantir = [&](ID3D11Texture2D*& tx) -> bool {
            if (tx) {
                D3D11_TEXTURE2D_DESC ud{}; tx->GetDesc(&ud);
                if (ud.Width != bd.Width || ud.Height != bd.Height || ud.Format != bd.Format) { Soltar(tx); if (&tx == &m_ultimo) m_ultimoOk = false; }
            }
            if (!tx) {
                D3D11_TEXTURE2D_DESC ud{};
                ud.Width = bd.Width; ud.Height = bd.Height; ud.MipLevels = 1; ud.ArraySize = 1; ud.Format = bd.Format;
                ud.SampleDesc.Count = 1; ud.Usage = D3D11_USAGE_DEFAULT; ud.BindFlags = 0;
                if (FAILED(m_dev->CreateTexture2D(&ud, nullptr, &tx)) || !tx) { tx = nullptr; return false; }
            }
            return true;
        };
        return garantir(m_ultimo) && garantir(m_comp);
    }
    /* sec. 27 (v3, item 9, pedido do dono): A SETA NA TAXA DO MONITOR TAMBEM COM O CONTEUDO ANDANDO. Entre dois quadros
       reais (sem FG; com FG os gerados ja apresentam no meio) re-apresenta-se o ultimo quadro limpo com a seta de agora,
       no maximo um por refresh e so com o mouse mexendo - mas SO na janela que nao encosta no vblank do proximo real: pelo
       menos ~1 refresh depois do ultimo real e com 1,5 refresh de folga antes do proximo (video 60 em 240 Hz: nos refreshes
       2 e 3 de cada 4). `[ultima] ponteiro_entre_quadros=0` volta ao de antes (so com a tela parada >= 50 ms). */
    bool PonteiroEntrePossivel() const {
        if (!g_ponteiroEntre.load(std::memory_order_relaxed) || !m_ultimoOk || !m_comp || m_fg.load() || !PonteiroNaApresentacao()) return false;
        const double P = m_telaPeriodoMs;
        return P > 0.5 && m_ivRealMs > 0.0 && m_ivRealMs >= 2.4 * P;
    }
    bool PonteiroSoVivo() const {
        if (!(m_ultimoOk && m_comp && !m_fg.load() && PonteiroNaApresentacao())) return false;
        const double desde = Ms() - m_ultRealMs;
        if (desde >= 50.0) return true;
        if (!PonteiroEntrePossivel()) return false;
        const double P = m_telaPeriodoMs;
        return desde >= 0.9 * P && desde + 1.5 * P <= m_ivRealMs;
    }
    void ReapresentarSoPonteiro() {
        if (!PonteiroSoVivo() || m_pausar.load() || !m_sc) return;
        const double ag = Ms();
        if (ag - m_ultPresMs < 0.9 * (m_telaPeriodoMs > 0.0 ? m_telaPeriodoMs : 4.0)) return;   // no maximo um por refresh
        POINT pos{};
        if (!GetCursorPos(&pos)) return;
        if (pos.x == m_ponteiro.posDesenhada.x && pos.y == m_ponteiro.posDesenhada.y) return;       // parado: nada a mostrar
        RECT rc{}; POINT org{ 0, 0 };
        if (!GetClientRect(m_janela, &rc)) return;
        ClientToScreen(m_janela, &org);
        const bool dentro = pos.x >= org.x && pos.y >= org.y && pos.x < org.x + rc.right && pos.y < org.y + rc.bottom;
        // saiu da pelicula: uma ultima re-apresentacao limpa a seta que ficou desenhada na borda
        if (!dentro && !m_ponteiro.posDentro) return;
        if (m_latWait && WaitForSingleObjectEx(m_latWait, 0, FALSE) != WAIT_OBJECT_0) return;   // sem lugar agora: tenta na proxima volta
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(m_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) return;
        D3D11_TEXTURE2D_DESC bd{}; bb->GetDesc(&bd);
        m_ctx->CopyResource(m_comp, m_ultimo);
        m_ponteiro.Desenhar(m_dev, m_ctx, m_comp, bd, m_janela);
        m_ctx->CopyResource(bb, m_comp);   // uma copia inteira (ver Mostrar)
        m_ponteiro.posDentro = dentro;
        if (!dentro) m_ponteiro.posDesenhada = pos;   // a seta saiu: nao re-apresentar de novo por isso
        if (m_fpsLigado.load(std::memory_order_relaxed)) FpsDesenhar(bb, bd);
        bb->Release();
        const int sm = m_sync.load(std::memory_order_relaxed);
        const bool semVblank = (sm == SYNC_SEM) || (sm == SYNC_VRR && m_telaVrr.load(std::memory_order_relaxed));
        const UINT fl = (sm == SYNC_SEM && m_tearingOk) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        HRESULT hrP;
        { VigiaMarca vm(g_vigiaApresDesde); hrP = m_sc->Present(semVblank ? 0u : 1u, fl); }
        ConferirPresent(hrP, "ponteiro");   // NS-2309 item 5(c)
        m_ponteiro.nSoPonteiro++;
        m_ponteiro.DepoisDoPresent();
        m_ultPresMs = Ms();
        if (m_iflip.Observar(m_sc) && m_log) { m_log("%s", m_iflip.Linha().c_str()); CadIflipJanela(); }
    }

    void Mostrar(ID3D11Texture2D* t) {
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(m_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) return;
        FpsContarPresent();   // I21: cada Mostrar e um Present - e o "tela N" do contador
        D3D11_TEXTURE2D_DESC td{}; t->GetDesc(&td);
        D3D11_TEXTURE2D_DESC bd{}; bb->GetDesc(&bd);
        const bool copiaDireta = (td.Width == bd.Width && td.Height == bd.Height &&
                                  (td.Format == bd.Format || td.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB));
        // RESET-CURSOR: com o ponteiro na apresentacao o quadro vai primeiro para m_ultimo (a copia LIMPA), a seta e
        // desenhada em m_comp e o backbuffer recebe UMA copia inteira - medido: escrever so o recorte da seta no
        // backbuffer (CopySubresourceRegion) deixava a pelicula COMPOSTA depois que o mouse mexia (0 de 43 em iFlip,
        // contra 12 de 43 do desenho na captura); a copia inteira e o que o caminho antigo sempre fez.
        const bool pont = PonteiroNaApresentacao() && GarantirCopias(bd);
        ID3D11Texture2D* destino = pont ? m_ultimo : bb;
        if (copiaDireta) {
            m_ctx->CopyResource(destino, t);
        } else {
            // tamanho ou formato diferente: desenha com o triangulo de tela cheia numa textura
            // intermediaria (o backbuffer nao e render target - ver o BufferUsage) e copia
            if (m_srvTex != t) {
                Soltar(m_srv); Soltar(m_srvTex);
                D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                sd.Format = td.Format; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
                if (sd.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                if (sd.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                if (SUCCEEDED(m_dev->CreateShaderResourceView(t, &sd, &m_srv))) { m_srvTex = t; m_srvTex->AddRef(); }
            }
            if (!m_inter) {
                D3D11_TEXTURE2D_DESC id{};
                id.Width = bd.Width; id.Height = bd.Height; id.MipLevels = 1; id.ArraySize = 1;
                id.Format = bd.Format; id.SampleDesc.Count = 1; id.Usage = D3D11_USAGE_DEFAULT;
                id.BindFlags = D3D11_BIND_RENDER_TARGET;
                if (SUCCEEDED(m_dev->CreateTexture2D(&id, nullptr, &m_inter)) && m_inter)
                    m_dev->CreateRenderTargetView(m_inter, nullptr, &m_rtv);
            }
            if (m_srv && m_rtv && m_inter) {
                D3D11_VIEWPORT vp{ 0.f, 0.f, (float)bd.Width, (float)bd.Height, 0.f, 1.f };
                m_ctx->OMSetRenderTargets(1, &m_rtv, nullptr);
                m_ctx->RSSetViewports(1, &vp);
                m_ctx->IASetInputLayout(nullptr);
                m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                m_ctx->VSSetShader(m_vs, nullptr, 0);
                m_ctx->PSSetShader(m_ps, nullptr, 0);
                m_ctx->PSSetShaderResources(0, 1, &m_srv);
                m_ctx->PSSetSamplers(0, 1, &m_samp);
                m_ctx->Draw(3, 0);
                ID3D11ShaderResourceView* n = nullptr; m_ctx->PSSetShaderResources(0, 1, &n);
                ID3D11RenderTargetView* nr = nullptr; m_ctx->OMSetRenderTargets(1, &nr, nullptr);
                m_ctx->CopyResource(destino, m_inter);
            }
        }
        // RESET-CURSOR: o ponteiro por cima do quadro PRONTO, com a posicao de agora (GetCursorPos logo antes do Present);
        // ele nao passa pela rede. Antes do contador, que fica por cima de tudo.
        {   // sec. 27: o intervalo tipico entre quadros reais (a seta entre dois deles usa)
            const double ag = Ms(), d = ag - m_ultRealMs;
            if (m_ultRealMs > 0.0 && d > 1.0 && d < 200.0) m_ivRealMs = m_ivRealMs > 0.0 ? 0.9 * m_ivRealMs + 0.1 * d : d;
            m_ultRealMs = ag;
        }
        if (pont) {
            m_ultimoOk = true;
            m_ctx->CopyResource(m_comp, m_ultimo);
            m_ponteiro.Desenhar(m_dev, m_ctx, m_comp, bd, m_janela);
            m_ctx->CopyResource(bb, m_comp);
        }
        if (m_fpsLigado.load(std::memory_order_relaxed)) FpsDesenhar(bb, bd);   // I21: por cima do quadro pronto, nos dois caminhos
        bb->Release();
    }

    // ------------------------------------------------ I21: o contador de fps (tudo na thread do apresentador)
    static constexpr float kFpsPx = 21.f, kFpsMargem = 22.f;   // as mesmas unidades logicas do cartaz (cartaz.h)
    static constexpr int   kFpsAlfaContorno = 210;             // contorno escuro forte: o contador vive sem caixa por tras
    float FpsEscala() const {
        const float e = m_fpsEscala.load();
        return e > 0.f ? e : std::max(1.f, (float)m_h / 1200.f);   // sem escala do main: pela resolucao, como o cartaz
    }
    void FpsContarPresent() {
        const double agora = Ms();
        m_fpsCnt++;
        if (m_fpsCntT0 <= 0.0) { m_fpsCntT0 = agora; m_fpsCnt = 0; return; }
        const double dt = agora - m_fpsCntT0;
        if (dt >= 1000.0) { m_fpsTelaFps = m_fpsCnt * 1000.0 / dt; m_fpsCnt = 0; m_fpsCntT0 = agora; }
    }
    /* O texto: os numeros do card do FG. "jogo" so quando o interpolador esta de fato inventando
       quadros (senao chegada = tela e o segundo numero e ruido); pausado, parado ou sem FG: "tela N". */
    void FpsTexto(wchar_t* b, size_t n) const {
        const double chegada = m_chegadaFps.load();
        const bool comJogo = m_fg.load() != nullptr && !m_pausada && !m_fonteParada && chegada > 0.0;
        if (m_fpsTelaFps <= 0.0) { b[0] = 0; return; }   // ainda sem a primeira janela de 1 s: nada a dizer (nao "tela 0")
        /* O1 (20/09/2026): so os dois numeros - "60 \u2192 120 fps". Quem le sabe o que e a seta; as
           palavras "jogo" e "tela" eram texto a mais numa coisa que fica por cima do jogo o
           tempo todo (a regra do pouco texto). O que a seta diz esta na dica do card. */
        if (comJogo) _snwprintf(b, n, L"%.0f \u2192 %.0f fps", chegada, m_fpsTelaFps);
        else         _snwprintf(b, n, L"%.0f fps", m_fpsTelaFps);
        b[n - 1] = 0;
    }
    /* Texto -> textura BGRA premultiplicada, por GDI. Duas passadas numa DIB: o texto deslocado nas
       8 direcoes (a uniao e a cobertura do contorno) e o texto no lugar (a cobertura da letra);
       compostas na CPU: letra branca por cima de contorno preto. Antialias em cinza (a DIB nao tem
       ClearType), entao o canal R e a cobertura. Custa ~1 ms de CPU, no maximo 1x/s. */
    bool FpsGerarTextura(const wchar_t* texto) {
        const float esc = FpsEscala();
        const int px = (int)(kFpsPx * esc + 0.5f), cont = std::max(1, (int)(1.5f * esc + 0.5f)), pad = cont + 2;
        const int len = (int)wcslen(texto);
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc) return false;
        HFONT f = CreateFontW(-px, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Semibold");
        HGDIOBJ f0 = SelectObject(dc, f);
        SIZE sz{}; GetTextExtentPoint32W(dc, texto, len, &sz);
        const int w = sz.cx + 2 * pad, h = sz.cy + 2 * pad;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = (w > 0 && h > 0) ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
        bool ok = false;
        if (bmp && bits) {
            HGDIOBJ b0 = SelectObject(dc, bmp);
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
            const size_t n = (size_t)w * h;
            std::vector<uint8_t> uni(n);
            memset(bits, 0, n * 4);
            for (int dy = -cont; dy <= cont; dy++) for (int dx = -cont; dx <= cont; dx++) {
                if (dx == 0 && dy == 0) continue;
                TextOutW(dc, pad + dx, pad + dy, texto, len);
            }
            GdiFlush();
            for (size_t i = 0; i < n; i++) uni[i] = (uint8_t)(((const uint32_t*)bits)[i] & 0xFF);
            memset(bits, 0, n * 4);
            TextOutW(dc, pad, pad, texto, len);
            GdiFlush();
            std::vector<uint32_t> out(n);
            for (size_t i = 0; i < n; i++) {
                const uint32_t A = ((const uint32_t*)bits)[i] & 0xFF;                 // letra (branca)
                const uint32_t U = (uni[i] * (uint32_t)kFpsAlfaContorno) / 255u;    // contorno (preto)
                const uint32_t a = A + (U * (255u - A)) / 255u;                     // premultiplicado: rgb = A, alfa = A + U(1-A)
                out[i] = (a << 24) | (A << 16) | (A << 8) | A;
            }
            D3D11_TEXTURE2D_DESC td{};
            td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA sd{ out.data(), (UINT)(w * 4), 0 };
            Soltar(m_fpsTextoSrv); Soltar(m_fpsTexto);
            if (SUCCEEDED(m_dev->CreateTexture2D(&td, &sd, &m_fpsTexto)) && m_fpsTexto &&
                SUCCEEDED(m_dev->CreateShaderResourceView(m_fpsTexto, nullptr, &m_fpsTextoSrv)) && m_fpsTextoSrv) {
                m_fpsTW = (UINT)w; m_fpsTH = (UINT)h; ok = true;
            } else { Soltar(m_fpsTextoSrv); Soltar(m_fpsTexto); m_fpsTW = m_fpsTH = 0; }
            SelectObject(dc, b0);
        }
        SelectObject(dc, f0);
        if (bmp) DeleteObject(bmp);
        if (f) DeleteObject(f);
        DeleteDC(dc);
        return ok;
    }
    /* Desenha o contador no backbuffer ja pronto: recorte -> alvo pequeno -> texto por cima -> volta.
       `bb` nao e render target (BufferUsage sem RENDER_TARGET_OUTPUT, de proposito), por isso a volta. */
    void FpsDesenhar(ID3D11Texture2D* bb, const D3D11_TEXTURE2D_DESC& bd) {
        const double agora = Ms();
        if (agora - m_fpsUltConf >= 250.0 || !m_fpsTextoSrv) {   // 4x/s: so refaz a textura quando o texto muda
            m_fpsUltConf = agora;
            wchar_t t[64]; FpsTexto(t, 64);
            if (!t[0]) return;   // sem medida ainda
            if (!m_fpsTextoSrv || wcscmp(t, m_fpsTextoAtual) != 0) {
                wcsncpy(m_fpsTextoAtual, t, 63); m_fpsTextoAtual[63] = 0;
                if (!FpsGerarTextura(t)) return;
            }
        }
        if (!m_fpsBlend) {
            D3D11_BLEND_DESC bl{};
            bl.RenderTarget[0].BlendEnable = TRUE;
            bl.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(m_dev->CreateBlendState(&bl, &m_fpsBlend)) || !m_fpsBlend) return;
        }
        if (!m_fpsTextoSrv || !m_fpsTW || !m_fpsTH || m_fpsTW > bd.Width || m_fpsTH > bd.Height) return;
        const int margem = (int)(kFpsMargem * FpsEscala() + 0.5f), canto = m_fpsCanto.load();
        const bool esq = (canto == FPS_SUP_ESQ || canto == FPS_INF_ESQ), inf = (canto == FPS_INF_DIR || canto == FPS_INF_ESQ);
        int x = esq ? margem : (int)bd.Width - margem - (int)m_fpsTW;
        int y = inf ? (int)bd.Height - margem - (int)m_fpsTH : margem;
        x = std::max(0, std::min(x, (int)bd.Width - (int)m_fpsTW));
        y = std::max(0, std::min(y, (int)bd.Height - (int)m_fpsTH));
        if (m_fpsAlvo && (m_fpsAW != m_fpsTW || m_fpsAH != m_fpsTH)) { Soltar(m_fpsAlvoRtv); Soltar(m_fpsAlvo); }
        if (!m_fpsAlvo) {
            D3D11_TEXTURE2D_DESC ad{};
            ad.Width = m_fpsTW; ad.Height = m_fpsTH; ad.MipLevels = 1; ad.ArraySize = 1; ad.Format = bd.Format;
            ad.SampleDesc.Count = 1; ad.Usage = D3D11_USAGE_DEFAULT; ad.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (FAILED(m_dev->CreateTexture2D(&ad, nullptr, &m_fpsAlvo)) || !m_fpsAlvo) return;
            if (FAILED(m_dev->CreateRenderTargetView(m_fpsAlvo, nullptr, &m_fpsAlvoRtv)) || !m_fpsAlvoRtv) { Soltar(m_fpsAlvo); return; }
            m_fpsAW = m_fpsTW; m_fpsAH = m_fpsTH;
        }
        D3D11_BOX box{ (UINT)x, (UINT)y, 0, (UINT)x + m_fpsTW, (UINT)y + m_fpsTH, 1 };
        m_ctx->CopySubresourceRegion(m_fpsAlvo, 0, 0, 0, 0, bb, 0, &box);
        D3D11_VIEWPORT vp{ 0.f, 0.f, (float)m_fpsTW, (float)m_fpsTH, 0.f, 1.f };
        m_ctx->OMSetRenderTargets(1, &m_fpsAlvoRtv, nullptr);
        m_ctx->OMSetBlendState(m_fpsBlend, nullptr, 0xFFFFFFFFu);
        m_ctx->RSSetViewports(1, &vp);
        m_ctx->IASetInputLayout(nullptr);
        m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_ctx->VSSetShader(m_vs, nullptr, 0);
        m_ctx->PSSetShader(m_ps, nullptr, 0);
        m_ctx->PSSetShaderResources(0, 1, &m_fpsTextoSrv);
        m_ctx->PSSetSamplers(0, 1, &m_samp);
        m_ctx->Draw(3, 0);
        ID3D11ShaderResourceView* n = nullptr; m_ctx->PSSetShaderResources(0, 1, &n);
        ID3D11RenderTargetView* nr = nullptr; m_ctx->OMSetRenderTargets(1, &nr, nullptr);
        m_ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        m_ctx->CopySubresourceRegion(bb, 0, (UINT)x, (UINT)y, 0, m_fpsAlvo, 0, nullptr);
    }

    bool CriarDesenho(std::wstring* erro) {
        static const char kHlsl[] =
            "Texture2D t : register(t0); SamplerState s : register(s0);\n"
            "struct VSO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
            "VSO VS(uint id : SV_VertexID) { VSO o; float2 uv = float2((id << 1) & 2, id & 2);\n"
            "  o.p = float4(uv * float2(2,-2) + float2(-1,1), 0, 1); o.uv = uv; return o; }\n"
            "float4 PS(VSO i) : SV_Target { return t.Sample(s, i.uv); }\n";
        typedef HRESULT (WINAPI *PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
        HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
        PFN_Compile compile = dc ? (PFN_Compile)(void*)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!compile) { if (erro) *erro = L"apresentador: d3dcompiler_47.dll indisponivel"; return false; }
        ID3DBlob *vsb = nullptr, *psb = nullptr, *eb = nullptr;
        if (FAILED(compile(kHlsl, sizeof(kHlsl) - 1, "apresentador", nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vsb, &eb)) || !vsb)
        { if (erro) *erro = L"apresentador: VS nao compilou"; if (eb) eb->Release(); return false; }
        if (eb) { eb->Release(); eb = nullptr; }
        if (FAILED(compile(kHlsl, sizeof(kHlsl) - 1, "apresentador", nullptr, nullptr, "PS", "ps_4_0", 0, 0, &psb, &eb)) || !psb)
        { if (erro) *erro = L"apresentador: PS nao compilou"; if (eb) eb->Release(); vsb->Release(); return false; }
        if (eb) eb->Release();
        m_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &m_vs);
        m_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &m_ps);
        vsb->Release(); psb->Release();
        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.MaxLOD = D3D11_FLOAT32_MAX;
        m_dev->CreateSamplerState(&smp, &m_samp);
        if (!m_vs || !m_ps || !m_samp) { if (erro) *erro = L"apresentador: shaders/sampler nao criados"; return false; }
        return true;
    }

    void (*m_log)(const char*, ...) = nullptr;
    bool m_ok = false;
    bool m_deviceCru = false;
    HWND m_janela = nullptr;
    Ponteiro::NaApresentacao m_ponteiro;   // RESET-CURSOR (so a thread do apresentador mexe)
    ID3D11Texture2D* m_ultimo = nullptr;   // RESET-CURSOR: copia do ultimo quadro SEM ponteiro (para re-apresentar so o ponteiro com a tela parada)
    ID3D11Texture2D* m_comp = nullptr;     // RESET-CURSOR: o quadro + a seta, copiado INTEIRO para o backbuffer
    bool m_ultimoOk = false;
    double m_ultRealMs = 0.0, m_ultPresMs = 0.0, m_ivRealMs = 0.0;
    UINT m_w = 0, m_h = 0;
    ID3D11Device*        m_dev = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    IDXGISwapChain1*     m_sc  = nullptr;
    IDXGISwapChain2*     m_sc2 = nullptr;
    HANDLE               m_latWait = nullptr;
    ID3D11RenderTargetView*   m_rtv = nullptr;
    ID3D11Texture2D*          m_inter = nullptr;   // alvo do desenho quando a copia direta nao serve
    ID3D11ShaderResourceView* m_srv = nullptr;
    ID3D11Texture2D*          m_srvTex = nullptr;
    ID3D11VertexShader*  m_vs = nullptr;
    ID3D11PixelShader*   m_ps = nullptr;
    ID3D11SamplerState*  m_samp = nullptr;
    // I21: o contador de fps na janela visivel (ver FpsNaTela)
    std::atomic<bool>  m_fpsLigado{ false };
    std::atomic<int>   m_fpsCanto{ FPS_SUP_DIR };
    std::atomic<float> m_fpsEscala{ 0.f };          // 0 = pela resolucao (m_h/1200)
    ID3D11Texture2D*          m_fpsTexto = nullptr;    // o texto, BGRA premultiplicado (refeito so quando muda)
    ID3D11ShaderResourceView* m_fpsTextoSrv = nullptr;
    ID3D11Texture2D*          m_fpsAlvo = nullptr;     // o recorte do backbuffer sob o contador (render target)
    ID3D11RenderTargetView*   m_fpsAlvoRtv = nullptr;
    ID3D11BlendState*         m_fpsBlend = nullptr;
    UINT     m_fpsTW = 0, m_fpsTH = 0, m_fpsAW = 0, m_fpsAH = 0;
    wchar_t  m_fpsTextoAtual[64] = {};
    double   m_fpsUltConf = 0.0, m_fpsCntT0 = 0.0, m_fpsTelaFps = 0.0;   // presents/s desta thread (janela de 1 s)
    uint64_t m_fpsCnt = 0;
    std::thread           m_th;
    std::atomic<bool>     m_sair{ false };
    std::atomic<bool>     m_vsync{ false };
    std::atomic<bool>     m_deviceMorreu{ false };
    std::atomic<uint64_t> m_apresentados{ 0 };
    // pausa coordenada (troca de nivel da ponte em voo) e folga de um quadro (nivel Codec)
    std::atomic<bool> m_pausar{ false }, m_pausado{ false }, m_segurar{ false };
    ID3D11Texture2D*  m_folgaTex = nullptr;   // emprestada da ponte (nao tem ref propria)
    uint64_t          m_folgaN = 0;
    std::atomic<Ponte*>   m_ponte{ nullptr };
    HANDLE m_evtNovo = nullptr, m_evtConsumiu = nullptr;
    // FLUIDEZ INTERPOLADA (so existe com interpolador ligado)
    std::atomic<FrameGen::Interpolador*> m_fg{ nullptr };
    /* X-FG-6 (3x/4x): a FILA dos quadros ainda por mostrar, em ordem de tempo - os gerados que
       sobram (G2/4, G3/4) e, por ultimo, o B atrasado. Em 2x tem so o B (o comportamento de sempre). */
    /* `par` e o contador de pares do FI (a CAPACIDADE dos dois slots); `idFonte` e o numero do
       quadro DA PONTE que gerou este par - e por ele que o dedup responde (K-DEDUP §4). Sao
       coisas diferentes e nao dao para reaproveitar uma pela outra. */
    struct Pend { ID3D11Texture2D* t = nullptr; double alvo = 0.0; double passo = 0.0; bool gerado = false; uint64_t par = 0; uint64_t idFonte = 0; };
    Pend m_pend[16]; int m_pendN = 0, m_pendI = 0;   // X-FG-QUALIDADE: dois pares (a fila de um quadro guarda o par anterior); META: de 8x = 16
    const Pend* PendProximo() const { return m_pendI < m_pendN ? &m_pend[m_pendI] : nullptr; }
    void PendLimpar() { m_pendN = m_pendI = 0; }
    /* ===== K-DEDUP (§4): DESCARTE DO PAR DE UM QUADRO REPETIDO =====
       O dedup compara B com o distinto anterior NA GPU. Quando o quadro `n` era a mesma imagem
       que ja esta na tela, o par (G, B) que o FI fez para ele e G == B == essa imagem: presents
       da mesma coisa, fora de hora, parte do 25/8/25/8 do LS-RITMO §6.1. Tira-se da fila SEM
       APRESENTAR (nada a liberar: o Pend e textura do FI, nao da ponte). O par de n+1 continua
       certo sem correcao nenhuma - o slot `prev` dele guarda B(n), identico a B(n-1).

       POR QUE VARRER POR VEREDITO, e nao comparar com "o `n` da volta passada" (o que o comentario
       do gancho em fg_ffx.h sugere): o veredito de `n` so e COLHIDO dentro do Gerar da volta
       seguinte (fg_ffx.h, DedupColher roda antes do despacho), entao no ponto de chamada da volta
       n+1 ele em geral AINDA NAO EXISTE e um teste com id fixo quase nunca dispararia. Varrendo a
       frente da fila por veredito, e chamando isto nos dois pontos da volta - antes da CAPACIDADE
       e logo depois do Gerar, quando o veredito acabou de chegar - o descarte fica imune ao atraso
       exato da resposta da GPU, que e justamente o que ninguem controla.

       O QUE ISTO TEM DE DELICADO COM A FILA DE UM QUADRO (C0): se o descarte esvaziar a fila,
       `m_ultAlvo` fica apontando para o prazo de um present que NUNCA houve, e o G do par seguinte
       nasceria um tique alem de um fantasma. A cadencia entao recomeca do ULTIMO QUADRO QUE FOI A
       TELA. Nao se repete o G nem se apresenta nada no lugar: a imagem certa ja esta na tela, e
       mexer no relogio do ritmo e o defeito, nao o conserto. */
    int DescartarRepetidos(FrameGen::Interpolador* fg, double agora) {
        if (m_fila <= 0 || !fg || m_pendI >= m_pendN) return 0;
        int tirados = 0; uint64_t idAnt = ~0ull;
        while (const Pend* pd = PendProximo()) {
            if (!fg->QuadroRepetido(pd->idFonte)) break;
            if (pd->idFonte != idAnt) { idAnt = pd->idFonte; m_repetidos++; m_repetidosTotal++; m_r5Rep++; }
            m_pendI++; tirados++; m_r5RepPend++;
        }
        if (tirados > 0) {
            m_ultRepetidoMs = agora;
            if (!PendProximo()) { PendLimpar(); if (m_ultPresentMs > 0.0) m_ultAlvo = m_ultPresentMs; }
        }
        return tirados;
    }
    /* K-RITMO C0: com a fila de verdade sempre sobra um pendente, e o PendLimpar() de antes
       (que so roda com a fila vazia) nunca chegaria. Joga o que resta para o inicio do vetor. */
    void PendCompactar() {
        if (m_pendI == 0) return;
        int k = 0; for (int i = m_pendI; i < m_pendN; i++) m_pend[k++] = m_pend[i];
        m_pendN = k; m_pendI = 0;
    }
    /* K-RITMO C2/C3: o periodo e o vblank vem do DWM (o compositor que de fato mostra na tela),
       e o refresh nominal, do monitor onde a JANELA VISIVEL esta - nao o primario. Era esse o
       defeito K3: o log dizia 240 Hz numa linha (DWM) e 100 Hz noutra (monitor do alvo), e 80
       presents/s num compositor de 100 Hz da 10-10-20 ms por construcao. Relido a cada ~1 s:
       o dono troca de monitor e de refresh, e ligar/desligar VRR muda o periodo.
       Quando os dois discordam mais de 5%, o DWM esta falando de OUTRA tela: o tique segue o
       monitor da janela e a ancora (C3) fica desligada, porque o vblank que temos nao e o dela. */
    void LerTela(double agora) {
        if (m_telaLidoMs > 0.0 && agora - m_telaLidoMs < 1000.0) return;
        m_telaLidoMs = agora;
        if (m_ancoraEnv < 0) {
            wchar_t e[8]{}; m_ancoraEnv = 0;
            if (GetEnvironmentVariableW(L"DLSS5_FG_ANCORA", e, 8) > 0 && e[0] == L'0') {
                m_ancoraEnv = 1; m_ancora = false;
                if (m_log) m_log("apresentador: BANCADA - DLSS5_FG_ANCORA=0: os prazos NAO sao ancorados no vblank");
            }
        }
        double perDwm = 0.0;
        DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti);
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.qpcRefreshPeriod > 0 && f.QuadPart) {
            perDwm = (double)ti.qpcRefreshPeriod * 1000.0 / (double)f.QuadPart;
            m_telaVblankMs = (double)ti.qpcVBlank * 1000.0 / (double)f.QuadPart;
        }
        double hz = 0.0;
        DEVMODEW dm{}; dm.dmSize = sizeof(dm);
        MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
        HMONITOR mon = m_janela ? MonitorFromWindow(m_janela, MONITOR_DEFAULTTONEAREST) : nullptr;
        if (mon && GetMonitorInfoW(mon, (MONITORINFO*)&mi) &&
            EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
            hz = (double)dm.dmDisplayFrequency;
        const double perJanela = hz > 0.0 ? 1000.0 / hz : 0.0;
        bool mesmaTela = true;
        if (perDwm > 0.0 && perJanela > 0.0 && fabs(perDwm - perJanela) > 0.05 * perJanela) mesmaTela = false;
        m_telaPeriodoMs = mesmaTela && perDwm > 0.0 ? perDwm : (perJanela > 0.0 ? perJanela : perDwm);
        m_ancoraOk = mesmaTela && perDwm > 0.0 && m_telaVblankMs > 0.0;
        const double novoHz = m_telaPeriodoMs > 0.0 ? 1000.0 / m_telaPeriodoMs : 0.0;
        if (m_log && novoHz > 0.0 && fabs(novoHz - m_telaHz) > 0.5) {
            m_log("apresentador: tela da janela visivel %.0f Hz (periodo %.3f ms)%s", novoHz, m_telaPeriodoMs,
                  mesmaTela ? " - ancorado no vblank do DWM" : " - o DWM fala de outra tela: SEM ancora no vblank");
        }
        m_telaHz = novoHz;
        // e a mesma tela que limita o 3x/4x (X-FG-6): o RefreshHz() do main vinha do monitor do
        // ALVO e podia ser outra. Quem manda e a tela que de fato mostra os nossos presents.
        if (m_telaHz > 1.0) m_refreshHz = m_telaHz;
    }
    /* ===== AS MEDIDAS DA MAQUINA (K-RITMO, 20/09/2026) =====
       Nenhum prazo deste apresentador pode nascer de um numero em milissegundos escolhido a
       mao: a mesma equacao roda numa 4090 com 240 Hz e num notebook com 60, e o que e folga
       numa e atraso na outra. Tudo se mede aqui e entra nas contas por nome:

         T = intervalo da base (ms)            - o que o jogo entrega entre dois quadros reais
         c = tempo de GPU do FI por quadro     - fg->FiMs() (timestamps de GPU) + passes + VME
         p = present -> scanout                - do DWM, medido abaixo
         j = p95 do tremor da chegada (2 s)    - o quanto a fonte chega fora de hora
         q = custo de Mostrar + Present        - ja medido em ApresentarTex

       Os unicos numeros absolutos que sobram sao do SISTEMA OPERACIONAL, nao da maquina:
       a granularidade do despertador (m_spinMs, medida no arranque) e o intervalo de
       releitura da tela (1 s - trocar de monitor e um gesto humano, nao um evento de quadro). */
    double CMs() const { return m_cMs; }
    double PMs() const { return m_pMs; }
    double JMs() const { return m_jMs; }
    /* `c`: o tempo de GPU que o par custa. O FI ja cronometra com D3D11_QUERY/timestamp de GPU
       no device dele (fg_ffx.h, m_tsFreq) - nao ha o que acrescentar, so ler e suavizar. */
    void MedirC(FrameGen::Interpolador* fg) {
        if (!fg) return;
        const double c = fg->FiMs() + fg->PassesMs() + fg->VmeMs();
        if (c > 0.0) m_cMs = m_cMs > 0.0 ? 0.9 * m_cMs + 0.1 * c : c;
    }
    /* META (21/09/2026): O TETO PELO CUSTO. `cg` = tempo de GPU do FI por gerado (o FiMs mede os
       despachos encadeados juntos; dividido pelos gerados do N que estava valendo), EMA 0,9/0,1.
       O par de k custa `passes + VME + cg x (k - 1)` e tem de caber em 0,8 x intervalo da fonte -
       acima disso o pendente do par n ainda estaria na fila quando o n+2 chega (o mesmo teto
       estrutural do D, K-RITMO). Desce na hora (8 -> 4 -> 2); sobe so quando o degrau de cima
       caberia em 0,7 (histerese de 10% do intervalo). Nunca limita o jogo: o que nao cabe e
       gerado a menos, e o log diz "entregando". */
    double MetaPeriodoMs() const { return m_metaFps > 0.0 ? 1000.0 / m_metaFps : m_telaPeriodoMs; }
    void MetaCusto(FrameGen::Interpolador* fg, double agora) {
        if (!fg || m_telaFonteMs <= 0.0) return;
        const int g = std::max(1, m_multEf - 1);
        const double fi = fg->FiMs();
        /* w-153: a amostra so vale com o N parado ha 1 s - o FiMs e uma EMA e, logo depois de uma
           troca, ainda mede os despachos do N anterior (7 despachos divididos por 3 gerados dava um
           custo 2x maior; 3 por 7, 2x menor - e o teto ia e vinha com ele). */
        const bool nEstavel = m_metaMultMs <= 0.0 || agora - m_metaMultMs >= 1000.0;
        if (fi > 0.0 && nEstavel) { const double cg = fi / (double)g; m_metaCg = m_metaCg > 0.0 ? 0.9 * m_metaCg + 0.1 * cg : cg; }
        if (m_metaCg <= 0.0) return;
        if (m_metaCapMs > 0.0 && agora - m_metaCapMs < 5000.0) return;   // o teto tambem tem cooldown de 5 s
        const double fixo = fg->PassesMs() + fg->VmeMs(), T = m_telaFonteMs;
        const auto custo = [&](int k) { return fixo + m_metaCg * (double)(k - 1); };
        int cap = m_metaCapK;
        if (cap > 2 && custo(cap) > 0.8 * T) { cap = cap == 8 ? 4 : 2; if (cap > 2 && custo(cap) > 0.8 * T) cap = 2; }
        else if (cap < 8) { const int acima = cap == 2 ? 4 : 8; if (custo(acima) < 0.7 * T) cap = acima; }
        if (cap == m_metaCapK) return;
        m_metaCapK = cap; m_metaCapMs = agora;
        if (m_log && m_metaCapLog < 20) {
            m_metaCapLog++;
            const double fonte = 1000.0 / T, meta = MetaFps();
            const double da = std::min(meta, fonte * (double)cap);
            m_log("apresentador meta: custo %.2f ms por gerado (%d gerados = %.1f ms de %.1f ms do intervalo da fonte) - teto %dx%s",
                  m_metaCg, cap - 1, custo(cap), T, cap,
                  da < meta - 0.5 ? "" : " (a meta cabe)");
            if (da < meta - 0.5) m_log("apresentador meta: a placa nao da para %.0f: entregando %.0f (o jogo segue solto)", meta, da);
        }
    }
    /* `p`: present -> scanout.
       CORRECAO N6-FLIP (20/09/2026): ate aqui este bloco dizia "a nossa janela e LAYERED: quem
       a poe na tela e o DWM, e o GetFrameStatistics nao responde por ela". **ISSO ESTA ERRADO
       na pelicula em tela cheia** e custou uma bancada inteira: desde a M6 ela nasce SEM
       LAYERED e com WS_EX_NOREDIRECTIONBITMAP (main.cpp, bloco do NOREDIRECTIONBITMAP), e o
       modo Ponteiro (F9) e a unica excecao, enquanto dura. A frase virou premissa em tres
       arquivos e levou o O1 a atribuir `p` = 20-28 ms a um estilo que a janela nao tem.
       Quem responde agora e o proprio DXGI, uma vez por janela de medida: ver `m_flip`
       (flip_prova.h), que classifica cada quadro em OVERLAY (plano de hardware) ou COMPOSED e
       tambem diz se o GetFrameStatistics responde. Este `p` continua igual - ele e a segunda
       opiniao, e as duas precisam concordar antes de virar veredito.
       O criterio e o de sempre, que ja esta em producao: guarda-se o
       qpcVBlank de ANTES do present; quando ele avanca, aquele quadro teve a vez dele, e a
       distancia do present ate esse vblank e o p desta maquina. Amostrado de kAmostraP em
       kAmostraP presents para o DwmGetCompositionTimingInfo nao entrar no caminho de todo
       quadro (ele custa uma ida ao compositor). */
    static constexpr int kAmostraP = 8;
    /* ===== O1 (20/09/2026, pedido do n6): O ARTEFATO DO `p`, E A MEDIDA CERTA =====
       O criterio antigo (abaixo, como reserva) so CONFERE o vblank no present SEGUINTE: entre o
       present amostrado e a conferencia passa um intervalo de present inteiro, e o `qpcVBlank`
       lido ali e o ULTIMO vblank antes da conferencia - nao o primeiro depois do present. Entao
       `p` nunca media menos que ~um intervalo de present (16,7 ms a 60/s), e foi por isso que
       "present -> tela 20-28 ms" apareceu como se fosse do compositor. Com a pelicula no plano
       de hardware (N6-FLIP: OVERLAY nos dois monitores) o quadro vai ao scanout no vblank
       seguinte ao present, e a medida certa e a que o proprio DXGI da: `GetFrameStatistics`
       devolve, para o ultimo quadro EXIBIDO, o `PresentCount` dele e o `SyncQPCTime` do refresh
       em que foi exibido. Guarda-se o id do present amostrado (`GetLastPresentCount`, logo
       depois do Present) e, quando as estatisticas dizem que aquele id foi exibido,
       `p = SyncQPCTime - t_present`. Se `PresentCount` ja passou do id, o quadro foi engolido
       (nunca teve refresh proprio) e a amostra nao vale. Sem estatisticas (janela composta,
       DXGI antigo) volta o criterio antigo, e o log diz qual dos dois esta valendo. */
    void MedirP(double tPresent) {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        if (!f.QuadPart) return;
        const double teto = (m_telaPeriodoMs > 0.0 ? m_telaPeriodoMs : 16.7) * 8.0;
        if (m_pStats != 0 && m_sc) {
            if (m_pEsperando) {
                DXGI_FRAME_STATISTICS st{};
                const HRESULT hr = m_sc->GetFrameStatistics(&st);
                if (SUCCEEDED(hr)) {
                    if (st.PresentCount == m_pPresentId && st.SyncQPCTime.QuadPart) {
                        const double scan = (double)st.SyncQPCTime.QuadPart * 1000.0 / (double)f.QuadPart;
                        const double v = scan - m_pPresentMs;
                        if (v > 0.0 && v < teto) { m_pMs = m_pMs > 0.0 ? 0.9 * m_pMs + 0.1 * v : v; m_pStatsN++; }
                        if (m_telaPeriodoMs > 0.0) {   // a fase, medida do mesmo jeito de sempre
                            double ph = fmod(m_pPresentMs - scan, m_telaPeriodoMs);
                            if (ph < 0.0) ph += m_telaPeriodoMs;
                            m_telaFaseAcc += ph; m_telaFaseN++;
                        }
                        m_pEsperando = false;
                    } else if (st.PresentCount > m_pPresentId) { m_pEsperando = false; m_pStatsPerd++; }
                    else if (tPresent - m_pPresentMs > teto) m_pEsperando = false;
                } else if (hr != DXGI_ERROR_FRAME_STATISTICS_DISJOINT) {
                    if (m_pStats == 1 && m_log) m_log("apresentador: GetFrameStatistics parou de responder (0x%08X) - `p` volta ao criterio do vblank do DWM", (unsigned)hr);
                    m_pStats = 0; m_pEsperando = false;
                } else m_pEsperando = false;   // DISJOINT: a contagem reiniciou (troca de modo); a amostra vai fora
                return;
            }
            if (++m_pConta < kAmostraP) return;
            m_pConta = 0;
            UINT id = 0;
            if (SUCCEEDED(m_sc->GetLastPresentCount(&id))) {
                DXGI_FRAME_STATISTICS st{};
                const HRESULT hr = m_sc->GetFrameStatistics(&st);
                if (SUCCEEDED(hr) || hr == DXGI_ERROR_FRAME_STATISTICS_DISJOINT) {
                    if (m_pStats < 0 && m_log) m_log("apresentador: `p` (present -> tela) passa a vir do GetFrameStatistics (SyncQPCTime do refresh em que o quadro foi exibido)");
                    m_pStats = 1; m_pPresentId = id; m_pPresentMs = tPresent; m_pEsperando = true;
                    return;
                }
                if (m_log) m_log("apresentador: GetFrameStatistics nao responde por este swapchain (0x%08X) - `p` fica no criterio do vblank do DWM", (unsigned)hr);
            }
            m_pStats = 0;
        }
        DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti);
        if (m_pEsperando) {
            if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti))) {
                const double vb = (double)ti.qpcVBlank * 1000.0 / (double)f.QuadPart;
                if (vb > m_pVblankAntes) {
                    // X-GERADO: de quebra, a FASE - a que distancia do vblank anterior o present saiu.
                    // Perto de 0 (ou do periodo inteiro) o present tangencia a composicao e um
                    // solavanco de 1 ms muda de quadro; no meio do periodo ha folga dos dois lados.
                    if (m_telaPeriodoMs > 0.0) {
                        double ph = fmod(m_pPresentMs - m_pVblankAntes, m_telaPeriodoMs);
                        if (ph < 0.0) ph += m_telaPeriodoMs;
                        m_telaFaseAcc += ph; m_telaFaseN++;
                    }
                    const double v = vb - m_pPresentMs;
                    // so vale o que cabe num punhado de refreshes: acima disso houve pausa,
                    // troca de modo ou a tela dormiu, e a amostra nao fala do caminho normal
                    if (v > 0.0 && v < teto) m_pMs = m_pMs > 0.0 ? 0.9 * m_pMs + 0.1 * v : v;
                    m_pEsperando = false;
                } else if (tPresent - m_pPresentMs > teto) {
                    m_pEsperando = false;   // desistiu desta amostra
                }
            }
            return;
        }
        if (++m_pConta < kAmostraP) return;
        m_pConta = 0;
        if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti))) {
            m_pVblankAntes = (double)ti.qpcVBlank * 1000.0 / (double)f.QuadPart;
            m_pPresentMs = tPresent; m_pEsperando = true;
        }
    }
    /* `j`: o tremor da chegada. Nao e o intervalo, e o DESVIO dele em relacao a mediana - o
       quanto a fonte (WGC, cadeia oculta, o jogo) erra a hora de entregar. p95 de uma janela
       de kJanelaJms, porque e a cauda que estoura a fila, nao a media. */
    static constexpr double kJanelaJms = 2000.0;
    void MedirJ(double agora, double desvio) {
        m_jAmostra[m_jPos % kJN] = desvio; m_jQuando[m_jPos % kJN] = agora; m_jPos++;
        if (agora - m_jCalcMs < 100.0) return;   // o p95 nao precisa sair a cada quadro
        m_jCalcMs = agora;
        double v[kJN]; int n = 0;
        for (int i = 0; i < kJN; i++) if (m_jQuando[i] > 0.0 && agora - m_jQuando[i] <= kJanelaJms) v[n++] = m_jAmostra[i];
        if (n < 4) return;
        std::sort(v, v + n);
        m_jMs = v[(int)(n * 0.95)];
    }
    /* K-RITMO C3: puxa o prazo para o vblank mais proximo, menos `q` - o tempo que Mostrar +
       Present levam NESTA maquina (media medida em ApresentarTex), que e quanto o quadro tem
       de sair antes para chegar ao compositor a tempo daquele vblank. So com o tique em numero
       inteiro de vblanks - fora disso a quantizacao pioraria o que ela deveria consertar.
       Arredondamento ao mais proximo, e nao ceil, para que um prazo ja alinhado volte igual
       apesar do erro de ponto flutuante. */
    /* O1: a ancora vale com o tique comensuravel (um numero inteiro de vblanks) e SAI no modo
       VRR com a tela de fato em taxa variavel: ali nao ha vblank fixo em que encaixar, e o
       painel espera o quadro no instante calculado. */
    bool AncoraAtiva() const {
        if (!m_ancora || !m_ancoraOk || m_telaPeriodoMs <= 0.0) return false;
        if (m_sync.load(std::memory_order_relaxed) == SYNC_VRR && m_telaVrr.load(std::memory_order_relaxed)) return false;
        return m_tiqueComens;
    }
    double AoVblank(double d) const {
        if (!AncoraAtiva()) return d;
        const double margem = m_qMs > 0.0 ? m_qMs : m_spinMs;
        const double k = floor((d + margem - m_telaVblankMs) / m_telaPeriodoMs + 0.5);
        return m_telaVblankMs + k * m_telaPeriodoMs - margem;
    }
    /* O limiar que decide "ja esta na hora de soltar o pendente?" e "vale a pena sair da espera
       para gerar o par novo?". Era 5 ms fixos (kFilaGerarMs). Agora e o que a maquina precisa
       para por um quadro na tela: `c + p`. Piso no que o despertador consegue resolver - abaixo
       dele a espera nao tem como acertar coisa nenhuma, e isso e do SO, nao da placa. */
    /* X-GERADO (20/09/2026): `p` SAIU DAQUI, e era ele quem prendia a fase em zero.
       Este limiar decide, quando um quadro novo chega com o pendente ainda por sair: solta o
       pendente JA (cedo, fora do prazo dele) ou gera o par novo primeiro e deixa o pendente sair
       na hora? A conta certa e "o pendente vence antes de eu terminar de gerar?" - isso e `c`, o
       tempo do par, mais a granularidade do despertador. `p` (present -> scanout) e o que o
       compositor leva DEPOIS do present: ele nao atrasa a geracao nem antecipa o prazo.
       Nem `c` serve: `c` e tempo de GPU. O que de fato atrasa o pendente e o tempo de CPU que o
       Gerar gasta antes de a volta do laco poder apresentar de novo - e esse agora e MEDIDO
       (m_gerCpuMs, EMA do proprio fg->Gerar; sai no log como `gerar`, 0,74-0,86 ms no alvo).
       HISTORICO (20/09/2026, para nao se repetir a tentativa pela metade): baixar este limiar
       SO funciona com o interpolador em TRES slots. Com dois, o pendente que fica passa a
       atravessar duas chegadas, a CAPACIDADE do FI o despeja e o `estouro` foi a 40..47 em 5 de
       30 janelas. Com tres (fg_ffx.h, kSlots) o par de n so e reescrito por n+3 e a fila pode
       segurar o pendente por uma chegada - que e o que faz a folga da chegada existir e o
       controlador de fase ter o que corrigir. */
    double LimiarGerarMs() const { return std::max(m_gerCpuMs, 2.0 * m_spinMs); }
    /* X-FG-QUALIDADE (fila): tudo o que esta na fila sai AGORA (pausa do gate, vao, sem FI) e a
       cadencia recomeca no proximo par. */
    void FilaEsvaziarAgora() {
        while (const Pend* pd = PendProximo()) { ApresentarTex(pd->t, pd->gerado, 0.0); m_pendI++; }
        PendLimpar(); m_ultAlvo = 0.0;
    }
    /* ===== X-FONTE-SOLTA (20/09/2026): ADMISSAO ANTES DO FI =====
       O DEFEITO (medido em relatorios\FG-PROVA.md, logs em bancada3\fg-prova-2009\): no modo
       Universal com Fluidez 2x e o RE2 correndo SOLTO a ~200 fps, o device D3D12 do interpolador
       era REMOVIDO (0x887A0006) em 16 s e em 105 s; com `--pulo 3/4` - a chegada limitada a
       ~30 fps - foram 220 s sem nenhuma falha. A causa nao e o FI ser lento: e nao haver ninguem
       dizendo NAO. Cada chegada entrava no Gerar() e, antes mesmo da espera pelo alocador (a
       unica contra-pressao que existia, e que so agia com DOIS pares em voo), ja tinha pago uma
       CopyResource 4K, o reduz, o VME, o corte e a mascara de HUD no device do JOGO. A 200
       chegadas/s isso e trabalho de GPU entrando mais rapido do que sai, sem teto, ate o
       watchdog do Windows reiniciar a placa.

       A regra agora e a de qualquer fila que nao pode crescer: quem chega sem vaga NAO entra -
       e SUBSTITUIDO. O quadro e solto de volta para a ponte e o proximo Receber() pega o mais
       recente (a ponte sempre entrega o mais novo), entao o que chega ao FI e sempre o quadro
       mais atual que cabia. Nenhuma chave nova aparece para a pessoa: quem decide sao as
       medidas da maquina (`c` e `T`), como no resto do K-RITMO.

       Tres motivos de recusa, nenhum com milissegundo escolhido a mao:
         em voo    - a fence do par anterior nao sinalizou: NUNCA mais de um Gerar em voo;
         orcamento - `c` (GPU do par, cronometrada pelo proprio FI) passa de kOrcamento do
                     tempo decorrido desde o ultimo quadro ACEITO: o FI sozinho comeria a placa;
         cedo      - esse mesmo tempo nao chegou a K periodos da TELA: o par rende K quadros,
                     um por tique, e um tique nao pode ser menor que um refresh - aceitar mais
                     depressa que isso e pagar GPU por quadro que a tela nunca mostra.
       Os dois ultimos medem desde o ultimo ACEITO, e nao desde a ultima chegada, de proposito:
       esse tempo CRESCE enquanto se descarta, entao a recusa sempre acaba - uma regra medida
       contra a taxa de chegada podia recusar para sempre (a taxa nao muda por descartar) e a
       tela secaria. Em regime, o orcamento se estabiliza na taxa que o FI aguenta nesta
       maquina: um par a cada c/kOrcamento ms.
       O piso da tela e proposital em vez de uma fracao de T: a captura entrega REPETIDOS (49-61%
       no RE2 medido hoje) a meio T, e cortar por T poria a admissao brigando com o dedup, que e
       quem sabe distinguir os dois - o piso da tela deixa o repetido passar e o dedup decide.
       A mediana da chegada passa a ser a dos ACEITOS (ver MedianaChegada), como ja acontecia
       com os repetidos do dedup - senao o tique nasceria de uma taxa que a fila nunca ve.

       COMPATIVEL COM N1: com a fonte em fase (o limitador do --pulo/vblank) a chegada ja vem
       em T, a fence ja sinalizou e `c` cabe folgado - nenhum dos tres dispara e nada muda. A
       admissao e para a fonte que NAO tem a peca: a captura de janela e a do console. */
    static constexpr double kOrcamento = 0.80;   // fracao do tempo desde o ultimo aceito que o FI pode custar
    static constexpr double kVigiaT    = 4.0;    // T's sem a fence sinalizar antes de o vigia conferir o device
    bool AdmitirPar(FrameGen::Interpolador* fg, double agora) {
        if (!fg->Pronto()) { m_admUltAceitaMs = agora; return true; }   // morto / sem contexto: o caminho de sempre decide (o quadro real sai)
        // (1) UM GERAR EM VOO. A fence do par anterior ainda nao sinalizou.
        if (fg->FiEmVoo()) {
            /* (3) O VIGIA. A fence nao sinaliza ha mais de min(teto do FI, kVigiaT * T): ou a
               placa esta atolada, ou o device ja morreu e ninguem perguntou - com a admissao
               recusando antes do Gerar(), a pergunta que ele fazia na entrada dele deixou de
               ser feita. Morto, o quadro PASSA: o Gerar() devolve false logo na entrada, o
               quadro real sai e a FilaEsvaziarAgora() do caminho de sempre solta os pendentes -
               e a fila nao fica presa com texturas de um interpolador que nao existe mais. */
            const double tetoFi = FrameGen::Interpolador::TetoVigiaMs();
            const double teto = m_tMs > 0.0 ? std::min(tetoFi, kVigiaT * m_tMs) : tetoFi;
            const double emVoo = fg->FiEmVooMs();
            if (emVoo > teto) {
                if (!fg->ConferirDevice()) { m_admVigia++; return true; }
                if (agora - m_admVigiaLogMs >= 5000.0) {
                    m_admVigiaLogMs = agora; m_admVigia++;
                    if (m_log) m_log("apresentador: admissao: o FI esta ha %.0f ms sem terminar o par (teto %.0f ms) - nenhum quadro novo entra ate ele terminar", emVoo, teto);
                }
            }
            m_admEmVoo++; return false;
        }
        const double desde = m_admUltAceitaMs > 0.0 ? agora - m_admUltAceitaMs : 0.0;
        // (2) ORCAMENTO DE GPU: o par custa `c` e so ha lugar para ele a cada c/kOrcamento ms.
        if (m_cMs > 0.0 && desde > 0.0 && m_cMs > kOrcamento * desde) { m_admOrc++; return false; }
        // (1b) CHEGADA ADIANTADA DEMAIS: o par rende K quadros e o tique nao cabe abaixo de um
        //      refresh - aceitar mais depressa que K periodos da tela e GPU por quadro invisivel.
        /* O1-TELA: no modo Tela ha UM present por vblank e o par nao rende K quadros fixos - o que
           nao cabe e so a fonte mais rapida que a tela, entao o piso e um periodo, nao N deles.
           Com N x periodo o modo se estrangulava sozinho (medido: alvo a 96 fps admitido a 50). */
        /* META (bancada 21/09): o piso e 3/4 do periodo da meta, nao o periodo inteiro. Com meta 60 e
           o jogo a 60 as chegadas vem a 16,6-16,8 ms e o piso de 16,67 recusava metade delas -
           `apresentados 32/s` com a fonte a 60. A 3/4 aceita-se a chegada nominal com o seu tremor
           e continua-se recusando o excesso (a 120 fps de fonte, uma em duas). */
        const double pisoCedo = m_modoTela ? 0.75 * MetaPeriodoMs() : (double)std::max(2, m_multEf) * m_telaPeriodoMs;
        if (m_modoTela && !m_metaGerando && MetaPeriodoMs() > 0.0) {
            /* SEM GERADOS (meta coberta pelo jogo): a captura entrega AOS PARES (O1-TELA §prova-2: 4,2 /
               12,5 ms com a fonte a 60) e qualquer piso por intervalo recusa o segundo do par - medido
               46/s com a fonte a 60 e a meta 60. O que se quer e a TAXA media: um balde de fichas que
               enche a `meta` por segundo (ate 2 de reserva, o tamanho do par) e cada chegada gasta uma.
               A 120 fps de fonte e meta 60 metade e recusada, como antes; a 60 nada e. */
            const double refil = (agora - m_admBaldeMs) / MetaPeriodoMs();
            m_admBalde = std::min(2.0, m_admBalde + (m_admBaldeMs > 0.0 ? refil : 2.0)); m_admBaldeMs = agora;
            if (m_admBalde < 1.0) { m_admCedo++; return false; }
            m_admBalde -= 1.0;
            m_admUltAceitaMs = agora;   // (1d) tambem recusaria o 2o do par: sem gerados nao ha par a proteger
            return true;
        } else if (m_telaPeriodoMs > 0.0 && desde > 0.0 && desde < pisoCedo) { m_admCedo++; return false; }
        /* (1d) A RAJADA DA CAPTURA (X-GERADO, 20/09/2026). Quando o WGC para e volta
           (`LAT ritmo do WGC ... max 108 ms`) ele despeja o atraso de uma vez: tres quadros em
           poucos ms. Cada um virava um Gerar, nasciam tres pares antes de o primeiro sair, e a
           CAPACIDADE (dois slots do FI) despejava a fila - `estouro 11..29` em 9 de 25 janelas
           medidas no alvo a 30 fps. Esses pares nunca chegariam a tela: o par rende K quadros, um
           por tique, e dois pares aceitos a menos de meio par de distancia ja se atropelam.
           Entao: um par so a cada MEIO PAR (K * tique / 2). Quem e recusado e solto de volta e o
           proximo Receber() pega o MAIS RECENTE - aceita-se o quadro novo da rajada, nao o velho.
           O piso vem do TIQUE EM VIGOR, que e ancorado ao vblank e ja nasce da mediana; e `desde`
           conta do ultimo ACEITO, entao ele cresce enquanto se recusa e a recusa sempre acaba.
           SEM REALIMENTACAO: com a fonte em regime (chegadas a T = 2 tiques) nada e recusado, e
           numa rajada a taxa aceita continua sendo a da fonte - a mediana nao se mexe. O relogio
           da fila nao e tocado aqui: m_ultAlvo e o tique seguem como estavam. */
        {
            // O1-TELA: no modo Tela "meio par" e meio intervalo da FONTE (o tique e um vblank e nao diz nada sobre o par)
            const double piso = m_modoTela ? (m_telaFonteMs > 0.0 ? 0.5 * m_telaFonteMs : 0.0) : (m_fTique > 0.0 ? 0.5 * (double)std::max(2, m_multEf) * m_fTique : 0.0);
            if (piso > 0.0 && desde > 0.0 && desde < piso) { m_admRajada++; return false; }
        }
        m_admUltAceitaMs = agora;
        return true;
    }
    /* X-FONTE-SOLTA: a linha de 5 s da admissao. Sai AQUI (na chegada) e nao no ApresentarTex,
       porque com a fonte solta pode nao haver present nenhum na janela - e e justamente ai que
       ela precisa falar. */
    void AdmissaoLog(double agora) {
        if (m_admLogMs == 0.0) { m_admLogMs = agora; return; }
        if (agora - m_admLogMs < 5000.0) return;
        const double s = (agora - m_admLogMs) / 1000.0;
        const uint64_t desc = m_admEmVoo + m_admOrc + m_admCedo + m_admRajada;
        if (m_log)
            m_log("apresentador admissao: chegadas %.0f/s aceitas %.0f/s descartadas %.0f/s (em voo %llu, orcamento %llu, cedo %llu, rajada %llu, vigia %llu) | c %.2f ms de T %.2f ms",
                  m_admChegadas / s, m_admAceitas / s, desc / s,
                  (unsigned long long)m_admEmVoo, (unsigned long long)m_admOrc,
                  (unsigned long long)m_admCedo, (unsigned long long)m_admRajada, (unsigned long long)m_admVigia, m_cMs, m_tMs);
        m_admChegadas = m_admAceitas = m_admEmVoo = m_admOrc = m_admCedo = m_admRajada = m_admVigia = 0;
        m_admLogMs = agora;
    }
    uint64_t m_admChegadas = 0, m_admAceitas = 0, m_admEmVoo = 0, m_admOrc = 0, m_admCedo = 0, m_admVigia = 0;
    uint64_t m_admRajada = 0;   // X-GERADO: pares recusados por rajada da captura (ver AdmitirPar)
    double   m_admLogMs = 0.0, m_admVigiaLogMs = 0.0, m_admUltAceitaMs = 0.0;
    double   m_ultDescarteMs = 0.0;          // ultima recusa da admissao (ver MedianaChegada)
    int      m_fila = 1;                     // FilaQuadros(): 0 = G ancorado na chegada; 1 = fila de um quadro, tique fixo
    double   m_baseFps = 0.0;                // a base pedida ao jogo (BaseFps); 0 = nenhuma
    double   m_ultAlvo = 0.0;                // ultimo prazo agendado na fila (a cadencia continua dele)
    double   m_tiqueAj = 1.0, m_fTique = 0.0; bool m_fPreso = false;   // momentum do tique; tique em vigor; preso a base
    double   m_tiqueVb = 0.0;   // X-GERADO: em quantos vblanks o tique esta ancorado (0 = solto); histerese
    // X-GERADO: o que a TELA mostra - presents que cairam em composicoes DIFERENTES (dois presents na
    // mesma composicao viram uma imagem so) e a fase do present dentro do periodo da tela.
    uint64_t m_telaDist = 0; double m_telaFaseAcc = 0.0; int m_telaFaseN = 0;
    double   m_r5Pres = 0.0;   // presents na janela de 5 s (m_medN zera a cada segundo)
    /* K-RITMO: fase (C1), taxa presa a tela (C2) e ancora no vblank (C3). Os limiares sao
       FRACOES (adimensionais) do que se mede na maquina - nenhum milissegundo a mao. */
    static constexpr double kFaseGanho = 0.10;   // do erro de fase por par (PLL de fase lenta)
    static constexpr int    kFasePulo  = 32;     // pares seguidos fora de T/2 antes do pulo
    // X-GERADO: D e a folga que a chegada tem de achar, e a fila guarda UM TIQUE - as duas pontas
    // sao fracao do TIQUE. O piso existe so para desempatar a ordem quando a fonte nao treme nada.
    static constexpr double kDmax      = 0.80;   // teto de D: fracao do INTERVALO que a fila cabe (RITMO-3X)
    static constexpr double kDmin      = 0.25;   // piso de D: fracao do tique
    double   m_faseAcc = 0.0;   // X-GERADO: deslize de fase acumulado, gasto em vblanks inteiros
    double   m_gerCpuMs = 0.0;  // X-GERADO: custo de CPU do fg->Gerar (EMA) - o limiar de "da tempo de gerar antes?"
    static constexpr int    kJN        = 256;    // amostras do anel do tremor
    double   m_erroF = 0.0;                  // C1/P1: EMA do erro de ocupacao, em tiques
    double   m_faseErroAcc = 0.0; uint64_t m_faseN = 0;   // media do |erro de fase| por janela do log
    double   m_profAcc = 0.0;     uint64_t m_profN = 0;   // RITMO-3X: profundidade media da fila (ms) na janela
    double   m_dAlvo = 0.0;                               // RITMO-3X: D suavizado (EMA 0,9/0,1) - o alvo de profundidade
    double   m_medTique = 0.0;                            // RITMO-3X: mediana da chegada suavizada, so para a TAXA do tique
    double   m_apF = 0.0;                                 // RITMO-3X: media do deslize da fase (fracao do intervalo) = erro de taxa
    int      m_faseForaN = 0;                // pares seguidos com |erroFase| acima de T/2
    uint64_t m_fasePulos = 0;
    // as medidas da maquina (ver MedirC/MedirP/MedirJ e o comentario que as abre)
    double   m_cMs = 0.0, m_pMs = 0.0, m_jMs = 0.0, m_qMs = 0.0, m_dMs = 0.0, m_tMs = 0.0;
    double   m_pVblankAntes = 0.0, m_pPresentMs = 0.0; bool m_pEsperando = false; int m_pConta = 0;
    // O1: `p` pelo GetFrameStatistics: -1 = ainda nao tentou, 1 = respondendo, 0 = nao responde (reserva do DWM)
    int      m_pStats = -1; UINT m_pPresentId = 0; uint64_t m_pStatsN = 0, m_pStatsPerd = 0;
    FlipProva::Sonda m_flip;                 // N6-FLIP: OVERLAY x COMPOSED, dito pelo DXGI
    FlipProva::Regua m_regua;                // OCLUSAO (23/09/2026): a regua de fluidez nativa deste swapchain
    FlipProva::Iflip m_iflip;                // RODADA6: pelicula em flip independente? (linha "pelicula:" 1x/s)
    double   m_jAmostra[kJN] = {}, m_jQuando[kJN] = {}; int m_jPos = 0; double m_jCalcMs = 0.0;
    bool     m_tiqueComens = false;          // o tique e um numero inteiro de vblanks
    /* O1-TELA: o modo "Tela" - um present por vblank, com o numero de quadros por intervalo da
       fonte variando para fechar o refresh. `kTelaMax` = o que o FI consegue dar (3 gerados + o
       real); `m_telaAcc` reparte a razao fracionaria; `m_telaErrF/N` medem o erro de fracao. */
    static constexpr int kTelaMax = 8;       // META: 7 gerados + o real (3o contexto do FI)
    bool     m_modoTela = false, m_telaAtivo = false;
    double   m_telaR = 0.0, m_telaAcc = 0.0, m_telaErrF = 0.0, m_telaFonteMs = 0.0;
    /* META (21/09/2026): a meta (> 0 fps; -1 = Tela; 0 = desligada), o R que sai, o que ela
       entrega em fps, o custo do FI por gerado e o teto de N que o custo permite (2/4/8). */
    double   m_metaFps = 0.0, m_metaRef = 1.0, m_metaEntregue = 0.0, m_metaCg = 0.0;
    int      m_metaCapK = 8, m_metaCapLog = 0;
    bool     m_metaGerando = false, m_metaDecidiu = false;   // gerar ou nao (R >= 1,25), com histerese de 2 s
    double   m_metaGerDesde = 0.0, m_metaFonteAntes = 0.0;
    double   m_metaInicioMs = 0.0, m_metaMultMs = 0.0, m_metaCapMs = 0.0;
    double   m_admBalde = 0.0, m_admBaldeMs = 0.0;   // o balde de fichas da admissao sem gerados (meta coberta)   // w-153: arranque de 3 s, cooldown de 5 s do N e do teto
    uint64_t m_telaErrN = 0, m_telaVb = 0, m_telaInt = 0;
    std::atomic<int>  m_sync{ 0 };           // 0 = VSync, 1 = sem VSync, 2 = VRR
    std::atomic<double>   m_tetoApres{ 0.0 };         // RODADA3: teto de apresentacao (0 = sem)
    std::atomic<uint64_t> m_tetoDescartados{ 0 }, m_tetoAdiados{ 0 };
    std::atomic<bool> m_telaVrr{ false };    // o gancho: a tela esta em taxa variavel (posto pelo main)
    bool     m_tearingOk = false;            // o swapchain nasceu com ALLOW_TEARING
    int      m_latQuer = 0, m_latAplicada = -1;   // 0 = padrao do modo; -1 = ainda nao aplicada
    double   m_telaPeriodoMs = 0.0, m_telaVblankMs = 0.0, m_telaHz = 0.0, m_telaLidoMs = 0.0;
    bool     m_ancoraOk = false;             // o vblank do DWM e o da tela da janela visivel
    bool     m_ancora = true;                // DLSS5_FG_ANCORA=0 desliga a C3
    int      m_ancoraEnv = -1;
    // linha `ritmo` de 5 s: fracao fora de hora, mediana do erro dos G e ocupacao media da fila
    double   m_r5Err[4096]; int m_r5N = 0;
    uint64_t m_r5G = 0, m_r5ForaG = 0;
    double   m_r5Fila = 0.0; uint64_t m_r5FilaN = 0;
    double   m_ultLog5Ms = 0.0;
    // K-DEDUP: pares e quadros tirados da fila por serem repetidos, na janela do log
    uint64_t m_r5Rep = 0, m_r5RepPend = 0;
    uint64_t m_fCheia = 0, m_fVazia = 0, m_fEstouro = 0, m_fRecomeco = 0;   // por segundo (log)
    // X-GERADO: estouros da SESSAO (m_fEstouro zera a cada segundo, no log) e o valor na chegada
    // anterior: a fila que NOS esvaziamos por capacidade nao pode alimentar o momentum do tique.
    uint64_t m_fEstouroTot = 0, m_fEstouroAnt = 0;
    int    m_mult = 2;                       // o multiplicador pedido (2, 3 ou 4)
    int    m_multEf = 2;                     // o que esta valendo (cai para 2 quando a placa ou a tela nao dao conta)
    double m_multCustoDesde = 0.0;           // desde quando o custo nao cabe no intervalo (para a queda)
    std::atomic<bool> m_multCaiu{ false };   // a queda aconteceu (o laco do main le uma vez e avisa)
    double m_refreshHz = 0.0;                // taxa do monitor, se o main informou (0 = desconhecida)
    bool   m_multQueda = false;              // ja caiu para 2x nesta sessao: nao tenta de novo
    bool   m_multLogTela = false;            // o aviso "a tela nao mostra Kx" ja saiu
    // o despertador do ritmo (ver CriarDespertador/EsperarAte)
    HANDLE   m_timer = nullptr;
    bool     m_timerHr = false, m_periodo1 = false, m_novoPendente = false, m_ultPar = false;
    double   m_spinMs = kSpinHrMs;
    double   m_fonteFps = 0.0;               // taxa fixa da fonte (placa de captura); 0 = livre
    // metricas de ritmo, por segundo (ver a linha "apresentador RITMO")
    uint64_t m_rG = 0, m_rB = 0, m_rForaG = 0, m_rForaB = 0, m_rAtr2 = 0, m_rAdiant = 0, m_rEspG = 0, m_rWakeN = 0, m_rIdadeN = 0;
    double   m_rErroG = 0.0, m_rErroB = 0.0, m_rEspGMs = 0.0, m_rWake = 0.0, m_rIdadeMs = 0.0;
    double   m_rAtrB[4096] = {}; int m_rAtrN = 0;   // atraso do B em relacao ao prazo (mediana/p95)
    // ultimas chegadas DA CADEIA (escritas em Acordar(), na thread do laco principal; lidas aqui)
    mutable SRWLOCK m_srcLock = SRWLOCK_INIT;
    double   m_srcDts[16] = {}; int m_srcN = 0; double m_srcUlt = 0.0;
    // X-GERADO: desvios da chegada medidos NO CARIMBO do quadro (escritos em Acordar, sob m_srcLock;
    // drenados pelo laco em MedirJ). Sem carimbo a fila fica vazia e vale o caminho de sempre.
    double   m_jFila[32] = {}; int m_jFilaN = 0;
    std::atomic<bool> m_carimboFonte{ false };   // a fonte carimba os quadros (captura WGC): `j` so vem de la
    double   m_carDt[512] = {}; int m_carN = 0;  // intervalos da chegada na janela do log (p05/p50/p95)
    int      m_dtN = 0;                      // quantas amostras a mediana tem (copiado a cada quadro; o gate exige >= 8)
    // X-FG-QUALIDADE: intervalos entre quadros DISTINTOS (o interpolador descartou os repetidos por
    // conteudo), no relogio da cadeia; quando valeu o ultimo repetido; contagem (janela / sessao)
    double   m_dstDts[16] = {}; int m_dstN = 0; double m_dstUltSrc = 0.0, m_ultRepetidoMs = 0.0;
    uint64_t m_repetidos = 0, m_repetidosTotal = 0, m_dedupDescAntes = 0;
    double   m_ultChegada = 0.0, m_ultPresentMs = 0.0;
    double   m_intervalos[4096] = {}; int m_nInt = 0;   // intervalos entre presents (p95 no log)
    uint64_t m_gerados = 0, m_cortes = 0, m_menus = 0, m_pausas = 0;
    bool     m_pausada = false, m_menu = false;
    // FONTE PARADA (I18): a ultima chegada veio depois de um vao (>= 4 intervalos, min. 250 ms)
    bool     m_fonteParada = false;
    double   m_fonteParadaDesde = 0.0, m_fonteParadaMs = 0.0;   // inicio do vao atual; soma dos vaos
    uint64_t m_paradas = 0, m_vaos = 0;                          // vezes que a fonte parou; quadros que chegaram apos vao
    double   m_pisoFps = -1.0, m_pisoVolta = 33.0;   // PISO DE MATERIAL (-1 = env ainda nao lida)
    bool     m_gateFixo = false;                     // DLSS5_FG_GATE=fixo: gate antigo (bancada)
    double   m_apresFps = 0.0;                       // apresentados/s da ultima janela de 1 s
    double   m_offFps = 0.0;                         // chegada MEDIDA com o FG pausado (0 = nunca mediu)
    double   m_custoFgMs = 0.0;                      // custo do FG na cadeia, MEDIDO na pausa (0 = so estimativa)
    double   m_custoMedidoEm = 0.0, m_pausadaDesde = 0.0, m_tOnAntes = 0.0;
    double   m_margem = 1.15;                        // ganho previsto exigido para voltar (sobe a cada pausa economica)
    double   m_apresMax = 0.0;                       // teto de apresentacao MEDIDO (maior apresentados/s com FG ativo, decai 0,5%/s)
    double   m_esperaVoltaMs = 5000.0;               // quanto a condicao de volta tem de valer apos pausa economica (5 -> 15 -> 60 s)
    int      m_pausasEcon = 0;                       // pausas por "nao compensa" na sessao
    double   m_custoFantasma = -1.0;                 // BANCADA (DLSS5_FG_CUSTO_MS); -1 = env ainda nao lida
    int      m_motivo = GATE_OK, m_motivoPend = GATE_OK;
    std::atomic<int> m_motivoAt{ GATE_OK };
    std::atomic<bool> m_vramPausa{ false };          // X-HUD-PISCA: o main pediu pausa por VRAM acima do orcamento
    // FG-BASE: o que era a conta da perna economica, agora so para a linha GATE do log
    double m_gateRef = 0.0, m_gateGanho = 0.0; bool m_gateAbaixoPiso = false;
    double   m_baixaDesde = 0.0, m_altaDesde = 0.0, m_paradaDesde = 0.0, m_ultLogMs = 0.0;
    uint64_t m_gFlat = 0, m_gAmbig = 0, m_gMask = 0, m_gMov = 0, m_gBlocos = 0;   // gates, por segundo
    std::atomic<bool>   m_interpAtiva{ false }, m_pausadaAt{ false };
    std::atomic<double> m_chegadaFps{ 0.0 };
    // MEDIDA-PONTE: acumuladores de tempo por etapa (media a cada 60 quadros no log)
    double   m_accEspera = 0, m_accMostrar = 0, m_accPresent = 0;
    int      m_semFi = -1;                           // BANCADA: -1 env nao lida, 0 normal, 1 sem FI
    int      m_semAlim = -1;                         // BANCADA: -1 env nao lida, 0 normal, 1 sem Alimentar na pausa
    int      m_medN = 0;
    uint64_t m_pulos = 0;
    uint64_t m_lentos = 0, m_lentosSobre = 0;   // DIAG-SOBREPOSICAO
    // X-TDR: presents pulados por falta de lugar no swapchain em kPresentMaxMs (janela / sessao) e a
    // janela "suja" (vao, pulo ou intervalo > 250 ms): os apresentados/s dela nao julgam nada
    uint64_t m_presPulados = 0, m_presPuladosTotal = 0;
    bool     m_apresSujo = false, m_janelaVao = false;
    // ---- RODADA5: cadencia por carimbo (ver Cadencia()) ----
    static constexpr int kCarQ = 32;                       // anel n -> carimbo cru (a ponte tem 3 slots; folga larga)
    std::atomic<uint64_t> m_carQn[kCarQ] = {};
    std::atomic<double>   m_carQv[kCarQ] = {};
    std::atomic<bool>     m_cadOn{ true };
    std::atomic<int>      m_cadTeto{ 2 };
    std::atomic<uint64_t> m_cadTelaNovos{ 0 }, m_cadTelaCopias{ 0 }, m_cadAtrasados{ 0 }, m_cadPulados{ 0 }, m_cadSemLugar{ 0 }, m_cadFilaAvisos{ 0 };
    uint64_t m_cadUltN = 0;                                   // o ultimo quadro da ponte que passou por aqui (pulados sem cadencia)
    std::atomic<double>   m_cadDMsAt{ 0.0 };
    bool     m_cadPrep = false;                            // despertador + prioridade ja postos
    double   m_cadOrigem = 0.0, m_cadPOrig = 0.0; long long m_cadBase = 0;   // origem fixa da grade (indices absolutos)
    double   m_cadPhiC = 0.0, m_cadPhiCx = 1.0, m_cadPhiCy = 0.0;          // fase da composicao (fracao de P, media circular)
    double   m_cadPhiS = 0.0, m_cadPhiSx = 1.0, m_cadPhiSy = 0.0;          // fase da FONTE (fracao de P, media circular)
    double   m_cadAtrT[64] = {}; int m_cadAtrPos = 0;                       // quando foram os ultimos atrasados (gatilho de K)
    double   m_cadVb = 0.0, m_cadP = 0.0, m_cadGradeMs = 0.0;   // grade do DWM relida a cada 100 ms
    int      m_cadDk = 0; bool m_cadDkOk = false, m_cadDkMed = false;                                   // D em refreshes (0 = ainda sem medida)
    double   m_cadBaixaDesde = 0.0, m_cadDRecalcMs = 0.0, m_cadP90 = 0.0;
    double   m_cadPathV[512] = {}, m_cadPathT[512] = {}; int m_cadPathPos = 0;
    bool     m_cadTemAnt = false; double m_cadUltCar = 0.0; long long m_cadIdxAnt = 0;
    // o quadro desta volta (posto em CadAntesDoPresent, lido depois do Present)
    bool     m_cadNovoAt = false, m_cadCopiaAt = false; double m_cadCarAt = 0.0; long long m_cadIdxAt = 0;
    // a regua fonte x tela (janela de 10 s)
    bool     m_cadTelaTemAnt = false; double m_cadTelaCarAnt = 0.0; long long m_cadTelaIdxAnt = 0;
    uint64_t m_cadHFonte[16] = {}, m_cadHTela[16] = {}; uint64_t m_cadCasou = 0, m_cadCmpN = 0;
    double   m_cadLat[4096] = {}; int m_cadLatN = 0;
    uint64_t m_cadSeg = 0, m_cadEmpurrados = 0, m_cadAtrJan = 0, m_cadCopJan = 0, m_cadSemAncora = 0; double m_cadEspAcc = 0.0;
    uint64_t m_cadFilaMax = 0, m_cadNovosJan = 0; double m_cadLogMs = 0.0, m_cadAvisoMs = 0.0;
    // ---- RODADA7: modo automatico, iFlip e o "sem lugar" que adia ----
    std::atomic<int>      m_cadModo{ 2 };                      // 0 desligada, 1 forcada, 2 automatica (padrao)
    bool     m_cadAutoLiga = false; int m_cadAutoVoto = 0; double m_cadAutoMs = 0.0, m_cadAutoIni = 0.0;   // decisao do modo auto (histerese de 3 s)
    double   m_cadAutoR = 0.0, m_cadAutoFps = 0.0;             // a ultima razao tela/fonte medida (log)
    double   m_cadAutoCar[256] = {}; int m_cadAutoPos = 0;     // carimbos crus dos quadros novos (fonte)
    bool     m_cadIflip = false;                               // a pelicula esta em flip independente (FlipProva, 1x/s)
    double   m_cadLeadP = 0.5;                                 // iFlip: soltura = vblank alvo - lead*P (ajustada pelo present->tela real)
    bool     m_cadEsperouJan = false;                          // a cadencia segurou algum quadro na janela de 1 s do iFlip
    unsigned m_cadSegJanN = 0, m_cadPresJanN = 0;              // RODADA7: presents segurados / todos na janela de 1 s (o lead so se ajusta com >= 90% segurados)
    std::atomic<uint64_t> m_semLugarEsperas{ 0 }, m_semLugarLongas{ 0 };   // sem lugar no swapchain: adiado (nao pulado)
    double   m_semLugarPiorMs = 0.0;
    uint64_t m_cadF25 = 0, m_cadF21 = 0; double m_cadFMax = 0.0;   // RODADA7: intervalos da FONTE (carimbo a carimbo, quadros novos) > 21 e > 25 ms, e o maior
    // ---- RODADA8: descida do lead do iFlip e o FG com saida fixa 2x (F2*) ----
    double   m_cadLeadPerdaMs = 0.0, m_cadLeadBaixaDesde = 0.0, m_cadLeadDesceuMs = 0.0, m_cadLeadEsperaMs = 10000.0;
    double   m_f2IniMs = 0.0, m_f2KDesceuMs = 0.0, m_f2KEsperaMs = 10000.0; bool m_f2SubirK = false;
    std::atomic<bool> m_f2Pedido{ true };                      // fg_saida=2x (padrao)
    F2Pend   m_f2[kF2Max]; int m_f2N = 0;
    bool     m_f2Prep = false, m_f2Dedup = false, m_f2TetoAvisou = false;
    bool     m_f2TemAnt = false; long long m_f2IdxAntB = 0, m_f2SrcAnt = 0; bool m_f2Alterna = false; double m_f2CarAnt = 0.0;
    long long m_f2UltIdxTela = 0; bool m_f2TemTela = false; long long m_f2UltIdxTelaReal = 0; bool m_f2TemTelaReal = false; double m_f2UltCarTela = 0.0;
    int      m_f2K = 0; bool m_f2KOk = false; double m_f2KBaixaDesde = 0.0, m_f2KRecalc = 0.0, m_f2P90 = 0.0; int m_f2H = 1;
    double   m_f2PathV[512] = {}, m_f2PathT[512] = {}; int m_f2PathPos = 0;
    double   m_f2AtrT[64] = {}; int m_f2AtrPos = 0;               // reais atrasados e gerados tarde (gatilho de K e do lead)
    int      m_f2Gap[64] = {}; int m_f2GapPos = 0, m_f2GapN = 0;  // intervalo da fonte em vblanks (para h = metade tipica)
    // janela de 10 s
    uint64_t m_f2HTela[16] = {}, m_f2HReal[16] = {}, m_f2HFonte[16] = {}, m_f2Casou = 0, m_f2CmpN = 0;
    uint64_t m_f2RealJan = 0, m_f2GerJan = 0, m_f2GerDescJan = 0, m_f2GerSemVbJan = 0, m_f2GerTardeJan = 0, m_f2GerMovidoJan = 0;
    uint64_t m_f2CortesJan = 0, m_f2VaosJan = 0, m_f2RealAtrJan = 0, m_f2EmpurJan = 0, m_f2SemAncoraJan = 0, m_f2SegJan = 0, m_f2PresJan = 0;
    double   m_f2Lat[4096] = {}; int m_f2LatN = 0; double m_f2LogMs = 0.0, m_f2SegMs = 0.0; uint64_t m_f2PresSeg = 0;
    std::atomic<uint64_t> m_f2GerTelaTot{ 0 }, m_f2GerDescTot{ 0 }, m_f2RealAtrTot{ 0 };
};

} // namespace PonteGpu
