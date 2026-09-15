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
#include <thread>
#include <atomic>
#include <string>
#include "ponte_gpu.h"
#include "fg_ffx.h"       // fluidez INTERPOLADA (experimental): G entre A e B, apresentado com pacing
#include <algorithm>

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
            d1.Flags = 0;
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
            m_sc2->SetMaximumFrameLatency(3);   // MAGPIE: BufferCount-1, deixa o present enfileirar
            m_latWait = m_sc2->GetFrameLatencyWaitableObject();
        }
        if (!m_latWait) {
            IDXGIDevice1* dd1 = nullptr;
            if (SUCCEEDED(m_dev->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dd1)) && dd1) { dd1->SetMaximumFrameLatency(3); dd1->Release(); }
        }
        if (m_log) m_log("apresentador: swapchain %p (waitable=%d); compilando o desenho", (void*)m_sc, m_latWait ? 1 : 0);
        if (!CriarDesenho(erro)) { Parar(); return false; }

        m_evtNovo     = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_evtConsumiu = CreateEventW(nullptr, FALSE, TRUE,  nullptr);   // nasce sinalizado: o primeiro quadro nao espera
        m_sair.store(false);
        m_apresentados.store(0);
        m_deviceMorreu.store(false);
        m_ok = true;
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
    uint64_t Gerados() const { return m_gerados; }
    uint64_t Cortes()  const { return m_cortes; }

    /* Sobe a thread. Separado do Iniciar para a ponte ja existir quando ela acordar. */
    void Rodar() {
        if (!m_ok || m_th.joinable()) return;
        m_th = std::thread([this] { Laco(); });
    }

    /* O laco principal chama depois de cada Present do swapchain oculto: ha quadro novo. */
    void Acordar() { if (m_evtNovo) SetEvent(m_evtNovo); }
    /* Nisto o laco principal espera no lugar do objeto de espera do swapchain oculto. */
    HANDLE EventoConsumiu() const { return m_evtConsumiu; }

    bool     Ativo() const { return m_ok; }
    bool     DeviceCru() const { return m_deviceCru; }
    bool     DeviceMorreu() const { return m_deviceMorreu.load(); }
    uint64_t Apresentados() const { return m_apresentados.load(); }
    ID3D11Device* Device() const { return m_dev; }
    IDXGISwapChain1* SwapChain() const { return m_sc; }

    void Parar() {
        m_sair.store(true);
        m_pausar.store(false);          // senao o laco fica na pausa e nunca ve o m_sair
        if (m_evtNovo) SetEvent(m_evtNovo);
        if (m_th.joinable()) m_th.join();
        m_pausado.store(false);
        m_folgaTex = nullptr; m_folgaN = 0;
        m_ok = false;
        if (m_latWait) { CloseHandle(m_latWait); m_latWait = nullptr; }
        if (m_evtNovo) { CloseHandle(m_evtNovo); m_evtNovo = nullptr; }
        if (m_evtConsumiu) { CloseHandle(m_evtConsumiu); m_evtConsumiu = nullptr; }
        Soltar(m_rtv); Soltar(m_inter); Soltar(m_srv); Soltar(m_srvTex);
        Soltar(m_vs); Soltar(m_ps); Soltar(m_samp);
        Soltar(m_sc2); Soltar(m_sc);
        if (m_ctx) { m_ctx->ClearState(); m_ctx->Flush(); }
        Soltar(m_ctx); Soltar(m_dev);
    }
    ~Apresentador() { Parar(); }

private:
    void Laco() {
        SetThreadDescription(GetCurrentThread(), L"DLSS5 apresentador (placa do monitor)");
        while (!m_sair.load()) {
            WaitForSingleObject(m_evtNovo, EsperaMs());   // 50 ms sem interpolador (como sempre); ate o prazo do B atrasado com ele
            if (m_sair.load()) break;
            Ponte* p = m_ponte.load();
            // PAUSA COORDENADA: solta o que estiver na mao ANTES de confirmar, porque quem
            // pediu vai destruir esta ponte - um slot preso aqui travaria o Parar() dela, e uma
            // SRV apontando para uma textura dela ficaria pendurada num recurso morto.
            if (m_pausar.load()) {
                if (m_folgaN && p && p->Ativa()) p->Liberar(m_folgaN);
                m_folgaTex = nullptr; m_folgaN = 0;
                Soltar(m_srv); Soltar(m_srvTex);
                m_pausado.store(true);
                SetEvent(m_evtConsumiu);   // o laco principal nao pode ficar preso na espera
                continue;
            }
            m_pausado.store(false);
            if (!p || !p->Ativa()) continue;
            if (FrameGen::Interpolador* fg = m_fg.load()) { LacoInterpolado(p, fg); continue; }
            // latencia 1: so pega o quadro depois que o swapchain aceita outro - assim o que
            // vai para a tela e o mais novo que existia no instante em que havia lugar.
            // TESTE-PONTE: este swapchain nasce SEM RENDER_TARGET_OUTPUT (para o ReShade pular),
            // e nesse caminho composto o objeto de espera nao sinaliza a cada vblank - ele
            // estourava o timeout de 100 ms e prendia o apresentador em ~7 fps. Timeout curto:
            // se o slot ja esta livre segue na hora; se nao, tenta no proximo quadro novo.
            LARGE_INTEGER tW0{}, tW1{}, tM1{}, tP1{}, qf{};
            QueryPerformanceFrequency(&qf);
            QueryPerformanceCounter(&tW0);
            if (m_latWait) WaitForSingleObjectEx(m_latWait, 4, FALSE);
            QueryPerformanceCounter(&tW1);
            uint64_t n = 0;
            ID3D11Texture2D* t = p->Receber(&n);
            if (!t) { SetEvent(m_evtConsumiu); m_pulos++; continue; }
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
            Mostrar(t);
            QueryPerformanceCounter(&tM1);
            p->Liberar(n);
            // MAGPIE: Present(0,0) - apresenta o mais novo sem esperar vblank; a fila de 4
            // buffers absorve, e o present para de bloquear na contrapressao do compositor.
            const HRESULT hr = m_sc->Present(0, 0);
            QueryPerformanceCounter(&tP1);
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
                    m_log("apresentador MEDIDA: %d quadros | espera %.1f ms | receber+copiar %.1f ms | PRESENT %.1f ms | pulos(sem quadro novo) %llu | LENTOS(>10ms) %llu, dos quais DURANTE o Present oculto: %llu [media por quadro]",
                          m_medN, m_accEspera / m_medN, m_accMostrar / m_medN, m_accPresent / m_medN, (unsigned long long)m_pulos,
                          (unsigned long long)m_lentos, (unsigned long long)m_lentosSobre);
                    m_accEspera = m_accMostrar = m_accPresent = 0; m_medN = 0; m_pulos = 0; m_lentos = 0; m_lentosSobre = 0;
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

    // ------------------------------------------------ FLUIDEZ INTERPOLADA (experimental)
    // O despertador: sem interpolador (ou sem B pendente) e o timeout de sempre; com B atrasado
    // pendente, acorda no prazo dele (o quadro real nunca fica preso atras do timeout de 50 ms).
    DWORD EsperaMs() const {
        if (!m_fg.load() || !m_pendB) return 50;
        const double falta = m_pendAlvoMs - Ms();
        return falta <= 0.0 ? 0 : (DWORD)std::min(50.0, falta + 0.5);
    }
    static double Ms() { LARGE_INTEGER t, f; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / (double)f.QuadPart; }
    // mediana filtrada das ultimas 16 chegadas (intervalos absurdos ficam fora ao entrar)
    double MedianaChegada() const {
        const int n = std::min(m_dtN, 16); if (n == 0) return 16.667;
        double v[16]; for (int i = 0; i < n; i++) v[i] = m_dts[i];
        std::sort(v, v + n); return (n & 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
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
    enum MotivoGate { GATE_OK = 0, GATE_NAO_COMPENSA = 1, GATE_MATERIAL = 2 };
    static constexpr double kMenuChegadaAlta = 45.0;   // ver a pausa de MENU, mais abaixo
    const char* NomeMotivo(int m) const {
        return m == GATE_NAO_COMPENSA ? "nao compensa" : m == GATE_MATERIAL ? "chegada abaixo do piso medido" : "-";
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
        if (m_dtN < 8) { m_pausadaAt.store(m_pausada); return; }
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

        if (m_pausada) {
            // A PROVA AO VIVO: 1 s depois de pausar, a cadeia ja acelerou. O que se mede agora
            // e a entrega sem FG, e a diferenca de intervalo e o custo do FG.
            if (m_pausadaDesde > 0.0 && m_custoMedidoEm < m_pausadaDesde && agora - m_pausadaDesde >= 1000.0 && tOn > 0.0) {
                m_offFps = fps;
                m_custoMedidoEm = agora;
                if (m_tOnAntes > 0.0) {
                    const double c = m_tOnAntes - tOn;
                    if (c > 0.0 && c < m_tOnAntes * 0.9) m_custoFgMs = c;
                }
                if (m_log) m_log("apresentador: prova ao vivo do FG (pausado 1 s) - sem FG a chegada e %.1f fps (%.2f ms); com FG era %.2f ms => o FG custa %.2f ms na cadeia%s",
                                 m_offFps, tOn, m_tOnAntes, m_custoFgMs > 0.0 ? m_custoFgMs : (m_tOnAntes - tOn),
                                 m_custoFgMs > 0.0 ? " (MEDIDO)" : " (fora de faixa - mantida a estimativa)");
            }
            // volta: piso de material E ganho previsto acima da margem
            const double cUse = m_custoFgMs > 0.0 ? m_custoFgMs + m_custoFantasma : custoGpu;
            const double ganhoPrev = (tOn > 0.0 && tOn + cUse > 0.0) ? (2.0 * tOn) / (tOn + cUse) : 0.0;
            const bool podeMaterial = fps >= m_pisoVolta;
            const bool podeEconomia = m_gateFixo || m_motivo != GATE_NAO_COMPENSA || ganhoPrev >= m_margem;
            if (podeMaterial && podeEconomia) {
                if (m_altaDesde == 0.0) m_altaDesde = agora;
                if (agora - m_altaDesde >= 2000.0) {
                    m_pausada = false; m_baixaDesde = 0.0; m_pausadaDesde = 0.0;
                    if (m_log) m_log("apresentador: FG VOLTA - chegada %.1f fps (piso %.0f), ganho previsto %.2fx (2 quadros por chegada, custo do FG %.2f ms) contra a margem %.2f",
                                     fps, m_pisoVolta, ganhoPrev, cUse, m_margem);
                    m_motivo = GATE_OK;
                }
            } else m_altaDesde = 0.0;
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
            // SEM REFERENCIA (intervalo da cadeia menor que o custo estimado do FG, ou ainda
            // sem apresentados contados) o gate NAO DECIDE - e nao zera o relogio que ja estava
            // correndo. Zerar era um defeito de verdade: a mediana da chegada balanca entre
            // quadros e bastava uma janela sem referencia para apagar 2,9 s de "esta ruim".
            const bool semRef = (refOff <= 0.0 || m_apresFps <= 0.0);
            const double ganho = semRef ? 1.0 : m_apresFps / refOff;
            const bool ruimEconomia = !m_gateFixo && !semRef && ganho < 1.0;
            if (semRef && !ruimMaterial) { m_pausadaAt.store(m_pausada); m_motivoAt.store(GATE_OK); return; }
            if (ruimMaterial || ruimEconomia) {
                if (m_baixaDesde == 0.0) { m_baixaDesde = agora; m_motivoPend = ruimMaterial ? GATE_MATERIAL : GATE_NAO_COMPENSA; }
                if (agora - m_baixaDesde >= 3000.0) {
                    m_pausada = true; m_pausas++; m_altaDesde = 0.0;
                    m_pausadaDesde = agora; m_tOnAntes = tOn; m_motivo = m_motivoPend;
                    if (m_motivo == GATE_NAO_COMPENSA && m_margem < 1.50) m_margem += 0.10;
                    if (m_log) {
                        if (m_motivo == GATE_MATERIAL)
                            m_log("apresentador: FG EM PAUSA - chegada %.1f fps por 3 s, abaixo do piso de %.0f: os quadros reais ficam a %.0f ms um do outro e a qualidade do quadro do meio so foi medida ate 133 ms (7,5 fps de chegada), onde o gerado ainda ganhava de repetir o anterior", fps, m_pisoFps, tOn);
                        else
                            m_log("apresentador: FG EM PAUSA - NAO COMPENSA: %.1f apresentados/s contra %.1f fps que a cadeia entregaria sem FG (intervalo %.2f ms - custo do FG %.2f ms%s) = ganho %.2fx; proxima volta exige %.2fx",
                                  m_apresFps, refOff, tOn, custo, m_custoFgMs > 0.0 ? ", MEDIDO na pausa anterior" : ", estimado pelo tempo de GPU do FG", ganho, m_margem);
                    }
                }
            } else { m_baixaDesde = 0.0; m_motivoPend = GATE_OK; }
        }
        m_pausadaAt.store(m_pausada);
        m_motivoAt.store(m_pausada ? m_motivo : GATE_OK);
    }

    void ApresentarTex(ID3D11Texture2D* t, bool gerado) {
        Mostrar(t);
        const double antes = Ms();
        const HRESULT hr = m_sc->Present(0, 0);
        const double agora = Ms();
        if (m_ultPresentMs > 0.0 && m_nInt < 4096) m_intervalos[m_nInt++] = agora - m_ultPresentMs;
        m_ultPresentMs = agora;
        m_accPresent += agora - antes;
        if (gerado) m_gerados++;
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            if (!m_deviceMorreu.exchange(true) && m_log)
                m_log("apresentador: Present devolveu 0x%08X (device removed; motivo 0x%08X)", (unsigned)hr, (unsigned)(m_dev ? m_dev->GetDeviceRemovedReason() : hr));
        }
        m_apresentados.fetch_add(1);
        ++m_medN;
        // TELEMETRIA POR SEGUNDO: e o que diz se os limiares dos gates estao frouxos (quanto
        // cada gate zerou) e quantas pausas automaticas houve.
        if (m_ultLogMs == 0.0) m_ultLogMs = agora;
        if (agora - m_ultLogMs >= 1000.0) m_apresFps = m_medN * 1000.0 / (agora - m_ultLogMs);   // o que o gate compara
        if (agora - m_ultLogMs >= 1000.0 && m_log) {
            std::sort(m_intervalos, m_intervalos + m_nInt);
            const double med = m_nInt ? m_intervalos[m_nInt / 2] : 0, p95 = m_nInt ? m_intervalos[(int)(m_nInt * 0.95)] : 0;
            FrameGen::Interpolador* fg = m_fg.load();
            const double mv = m_gMov ? (double)m_gMov : 1.0;
            m_log("apresentador INTERPOLADO: %d presents/s (%llu gerados, %llu cortes, %llu menu) | intervalo mediana %.1f ms p95 %.1f | chegada %.1f fps | Present %.2f ms | FI %.2f ms, passes+VME %.2f ms | gates zeraram: variancia %.0f%% ambiguidade %.0f%% mascara %.0f%% dos %.0f%% blocos moventes | pausas %llu%s | pan %llu, mv0 %llu, sem campo %llu | corte s/campo: %llu, %.3f ms, fracao %.3f",
                  m_medN, (unsigned long long)m_gerados, (unsigned long long)m_cortes, (unsigned long long)m_menus, med, p95, m_chegadaFps.load(), m_accPresent / m_medN,
                  fg ? fg->FiMs() : 0.0, fg ? (fg->PassesMs() + fg->VmeMs()) : 0.0,
                  100.0 * m_gFlat / mv, 100.0 * m_gAmbig / mv, 100.0 * m_gMask / mv, m_gBlocos ? 100.0 * m_gMov / (double)m_gBlocos : 0.0,
                  (unsigned long long)m_pausas, m_pausada ? (m_motivo == GATE_MATERIAL ? " (EM PAUSA: chegada abaixo do piso medido)" : " (EM PAUSA: nao compensa)") : "",
                  (unsigned long long)(fg ? fg->Pans() : 0), (unsigned long long)(fg ? fg->Mv0s() : 0), (unsigned long long)(fg ? fg->SemCampo() : 0),
                  (unsigned long long)(fg ? fg->CortesSemCampo() : 0), fg ? fg->CorteMs() : 0.0, fg ? fg->CorteFrac() : 0.0);
            // GATE: o que ele esta comparando neste instante (sem isto, "EM PAUSA" nao se audita)
            {
                const double tOn = m_chegadaFps.load() > 0.0 ? 1000.0 / m_chegadaFps.load() : 0.0;
                // O m_custoFantasma (BANCADA) entra AQUI tambem: sem ele esta linha dizia
                // "ganho 1,79x" com a margem em 1,25 e o FG continuava pausado, porque quem
                // decide (ganhoPrev) ja somava o fantasma. Log que nao bate com a decisao e
                // pior que log nenhum. Fora da bancada m_custoFantasma e 0 e nada muda.
                const double custo = (m_custoFgMs > 0.0 ? m_custoFgMs : (fg ? (fg->FiMs() + fg->PassesMs() + fg->VmeMs()) : 0.0))
                                     + (m_custoFantasma > 0.0 ? m_custoFantasma : 0.0);
                /* PAUSADA: os apresentados DA PAUSA sao a propria chegada - compara-los com "sem
                   FG" da 0,86-0,89x POR CONSTRUCAO e o log parecia dizer que o FG piorava as
                   coisas quando quem o desligou foi o piso de material. Pausada, a pergunta certa
                   e a mesma que a perna economica ja faz para voltar: quanto renderia RETOMAR. */
                const bool p = m_pausada;
                const double refOff = p ? m_chegadaFps.load() : (tOn > custo + 1.0 ? 1000.0 / (tOn - custo) : 0.0);
                const double ganho  = p ? ((tOn > 0.0 && tOn + custo > 0.0) ? (2.0 * tOn) / (tOn + custo) : 0.0)
                                        : (refOff > 0.0 ? m_apresFps / refOff : 0.0);
                m_log("apresentador GATE: %s %.1f contra %.1f fps sem FG (custo do FG %.2f ms, %s) = ganho %.2fx | piso de material %.0f fps, chegada %.1f | margem de volta %.2fx | %s",
                      p ? "RETOMAR daria ~" : "apresentados/s ", p ? refOff * ganho : m_apresFps, refOff,
                      custo, m_custoFgMs > 0.0 ? "MEDIDO na pausa" : "tempo de GPU do FG", ganho,
                      m_pisoFps, m_chegadaFps.load(), m_margem,
                      p ? NomeMotivo(m_motivo) : "ativo");
            }
            m_medN = 0; m_nInt = 0; m_accPresent = 0; m_ultLogMs = agora;
            m_gFlat = m_gAmbig = m_gMask = m_gMov = m_gBlocos = 0;
        }
    }
    // Um giro do laco com interpolador: o B atrasado que venceu o prazo sai; um quadro novo vira
    // G (agora) + B (meio intervalo depois). Corte de cena ou sem quadro anterior: B direto.
    void LacoInterpolado(Ponte* p, FrameGen::Interpolador* fg) {
        if (m_pendB && Ms() >= m_pendAlvoMs) { ApresentarTex(m_pendB, false); m_pendB = nullptr; }
        if (m_latWait) WaitForSingleObjectEx(m_latWait, 4, FALSE);
        uint64_t n = 0;
        ID3D11Texture2D* t = p->Receber(&n);
        if (!t) { SetEvent(m_evtConsumiu); m_pulos++; return; }
        const double agora = Ms();
        if (m_ultChegada > 0.0) { const double dt = agora - m_ultChegada; if (dt > 1.0 && dt < 250.0) m_dts[m_dtN++ % 16] = dt; }
        m_ultChegada = agora;
        const double mediana = MedianaChegada();
        const double fps = mediana > 0.0 ? 1000.0 / mediana : 0.0;
        m_chegadaFps.store(fps);
        m_interpAtiva.store(true);
        Gate(fps, agora, fg);
        if (m_pendB) { ApresentarTex(m_pendB, false); m_pendB = nullptr; }   // o B anterior nao pode ficar para tras do novo
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
        if (m_semFi == 1) { ApresentarTex(t, false); p->Liberar(n); SetEvent(m_evtConsumiu); return; }
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
            if (m_semAlim != 1) fg->Alimentar(m_ctx, t);
            ApresentarTex(t, false);
            p->Liberar(n);          // depois do Mostrar: o slot so pode ser reescrito apos a copia
            SetEvent(m_evtConsumiu);
            return;
        }
        FrameGen::Resultado r;
        const bool gerou = fg->Gerar(m_ctx, t, n, (float)mediana, &r);
        p->Liberar(n);   // o Gerar ja copiou B para uma textura propria
        // PAUSA DE "MENU": cena quase parada (quase nenhum bloco em movimento) por >= 1 s com
        // chegada alta -> so B. Interpolar menu/fade so produz franja em texto (tripletos do
        // Hogwarts: menu 31 dB / 1,7% de fantasma, o pior dos tres).
        if (r.nBlocos) {
            const bool parada = r.nMov < 0.02 * r.nBlocos && r.nDiffBig < 0.02 * r.nBlocos;
            if (parada) { if (m_paradaDesde == 0.0) m_paradaDesde = agora; } else m_paradaDesde = 0.0;
            // 45 fps aqui e o limiar de "chegada alta" da pausa de MENU, e nao tem relacao com
            // o gate: so se pula o quadro gerado numa cena parada quando ha quadros reais de
            // sobra. Era o antigo m_fpsVolta; virou constante nomeada quando o gate mudou.
            m_menu = parada && (agora - m_paradaDesde >= 1000.0) && fps >= kMenuChegadaAlta;
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
        if (gerou && !r.corte && !m_pausada && !m_menu) {
            ApresentarTex(r.G, true);
            m_pendB = r.B; m_pendAlvoMs = Ms() + mediana * 0.5;
        } else {
            if (r.corte) m_cortes++;
            if (m_menu) m_menus++;
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
        return (tOn + c > 0.0) ? f * (2.0 * tOn) / (tOn + c) : 0.0;
    }
    bool   SemFgMedido() const { return m_custoFgMs > 0.0; }
    double ChegadaFps() const { return m_chegadaFps.load(); }
    double LatenciaEstimadaMs(double fiMs) const { const double f = m_chegadaFps.load(); return (f > 0.0 ? 500.0 / f : 0.0) + fiMs; }
private:

    void Mostrar(ID3D11Texture2D* t) {
        ID3D11Texture2D* bb = nullptr;
        if (FAILED(m_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) return;
        D3D11_TEXTURE2D_DESC td{}; t->GetDesc(&td);
        D3D11_TEXTURE2D_DESC bd{}; bb->GetDesc(&bd);
        const bool copiaDireta = (td.Width == bd.Width && td.Height == bd.Height &&
                                  (td.Format == bd.Format || td.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB));
        if (copiaDireta) {
            m_ctx->CopyResource(bb, t);
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
                m_ctx->CopyResource(bb, m_inter);
            }
        }
        bb->Release();
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
    ID3D11Texture2D* m_pendB = nullptr;      // o B atrasado, ainda por mostrar
    double   m_pendAlvoMs = 0.0;             // quando ele deve sair
    double   m_dts[16] = {}; int m_dtN = 0;  // ultimas chegadas (mediana filtrada)
    double   m_ultChegada = 0.0, m_ultPresentMs = 0.0;
    double   m_intervalos[4096] = {}; int m_nInt = 0;   // intervalos entre presents (p95 no log)
    uint64_t m_gerados = 0, m_cortes = 0, m_menus = 0, m_pausas = 0;
    bool     m_pausada = false, m_menu = false;
    double   m_pisoFps = -1.0, m_pisoVolta = 33.0;   // PISO DE MATERIAL (-1 = env ainda nao lida)
    bool     m_gateFixo = false;                     // DLSS5_FG_GATE=fixo: gate antigo (bancada)
    double   m_apresFps = 0.0;                       // apresentados/s da ultima janela de 1 s
    double   m_offFps = 0.0;                         // chegada MEDIDA com o FG pausado (0 = nunca mediu)
    double   m_custoFgMs = 0.0;                      // custo do FG na cadeia, MEDIDO na pausa (0 = so estimativa)
    double   m_custoMedidoEm = 0.0, m_pausadaDesde = 0.0, m_tOnAntes = 0.0;
    double   m_margem = 1.15;                        // ganho previsto exigido para voltar (sobe a cada pausa economica)
    double   m_custoFantasma = -1.0;                 // BANCADA (DLSS5_FG_CUSTO_MS); -1 = env ainda nao lida
    int      m_motivo = GATE_OK, m_motivoPend = GATE_OK;
    std::atomic<int> m_motivoAt{ GATE_OK };
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
};

} // namespace PonteGpu
