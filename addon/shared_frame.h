/* =====================================================================
   shared_frame.h -- publica a saida da camada, ja com os efeitos, numa
   TEXTURA COMPARTILHADA entre processos (lado PRODUTOR, dentro do add-on).

   POR QUE ISTO EXISTE
   -------------------
   No DLSS 2x/3x, a camada de cima captura a janela da camada de baixo pelo
   compositor do Windows (Windows Graphics Capture). Medido em 2026-09-06
   (RELATORIO-BORRAO.md §4): a cadencia entre as camadas esta correta -- nao
   ha quadro repetido nem pulado --, mas o caminho custa +21 ms de atraso
   (30 ms no 1x contra 52 ms no 2x) e obriga a janela da camada de baixo a
   ficar visivel para o compositor ter o que compor. Passar a imagem por uma
   textura compartilhada tira o compositor do meio: cada quadro da camada de
   baixo vira exatamente um da camada de cima, sem cópia pela CPU.

   POR QUE NO reshade_finish_effects, E POR QUE PELO PONTEIRO NATIVO
   ----------------------------------------------------------------
   A imagem que a camada de cima precisa e a de DEPOIS dos efeitos. Depois do
   Present nao serve: com flip model, o conteudo do back buffer e indefinido
   assim que ele e apresentado. O unico ponto em que a imagem final existe e
   ainda esta acessivel e o reshade_finish_effects.

   E ele e o unico caminho que a ABI do MinGW aceita. O add-on nao pode
   chamar effect_runtime::get_back_buffer() nem device::get_resource_from_view():
   os dois devolvem um handle POR VALOR de um metodo virtual, e o binario do
   ReShade (MSVC) devolve isso por ponteiro oculto enquanto o g++ espera em RAX
   -- e a mesma armadilha documentada no topo do dlss5_bridge_addon.cpp, que ja
   custou uma violacao de acesso. O finish_effects entrega a resource_view como
   ARGUMENTO, e argumento a ABI acerta. Do handle em diante saimos da API do
   ReShade e falamos D3D11 direto: em D3D, handle DE resource_view E o ponteiro
   da ID3D11View, como a propria documentacao do ReShade diz.

   O PROTOCOLO
   -----------
   Um mapeamento de memoria nomeado ("<nome>") com o cabecalho abaixo, e uma
   textura compartilhada por NT handle NOMEADO ("<nome>_tex_<geracao>"), que o
   consumidor abre com ID3D11Device1::OpenSharedResourceByName. O nome da
   textura leva a geracao porque mudanca de resolucao exige recriar tudo, e um
   nome so faria o consumidor reabrir a textura velha.

   Sincronizacao por keyed mutex, com uma CHAVE SO (0) e regras assimetricas:

     produtor   AcquireSync(0, 0 ms)   -- nunca espera. Se o consumidor estiver
                                          com a textura, o quadro e pulado.
     consumidor AcquireSync(0, N ms)   -- espera um pouco, e so copia se o
                                          numero do quadro mudou.

   Quem nunca pode travar e a camada de BAIXO: ela e a que a pessoa esta
   vendo. Se a de cima nao acompanhar, ela perde quadros e o contador diz
   quantos -- e melhor do que engasgar a imagem principal.

   HDR fica de fora por ora: so BGRA8/RGBA8 SDR. Formato diferente e recusado
   com uma linha no log, e a camada de cima cai de volta na captura por
   compositor.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>

namespace dlss5_shared {

/* Cabecalho do mapeamento de memoria. Cresce so no fim, e a versao trava o
   contrato: consumidor com versao diferente recusa e volta para o compositor. */
constexpr uint32_t kMagic   = 0x44355346; /* 'D5SF' */
constexpr uint32_t kVersion = 2;   /* 2 acrescentou modo/slots/slot no fim */

/* MODOS DE TRANSPORTE. O consumidor NAO escolhe: ele le 'modo' do cabecalho e
   obedece. Quem escolhe e o produtor, pela variavel de ambiente
   DLSS5_TRANSPORTE, porque e ele quem cria as texturas.

   MODO_MUTEX (1) e o de sempre e continua sendo o padrao. Uma textura, keyed
   mutex, produtor com timeout zero.

   MODO_ROTACAO (2) e o desenho do OBS (graphics-hook): N texturas em anel,
   MISC_SHARED_NTHANDLE puro, SEM keyed mutex. O motivo de existir: o
   AcquireSync trava a THREAD DE CPU, entao com buffer unico o consumidor
   segura o produtor durante toda a copia dele. Serializar os dois e a hipotese
   para o transporte nunca ter ganhado do compositor mesmo depois do conserto
   do prazo do laco.

   O RISCO QUE A ROTACAO TRAZ, e esta escrito como HIPOTESE A VERIFICAR e nao
   como certeza: sem mutex, nada IMPEDE o consumidor de ler um slot enquanto
   ele e escrito. O OBS tolera porque uma imagem levemente rasgada num video
   passa despercebida; a nossa cadeia e TEMPORAL e alimenta uma rede, e um
   quadro rasgado pode virar artefato que se acumula no historico. Tres slots
   devem bastar (o produtor so volta ao slot i depois de N quadros), mas
   "devem" nao e "provado" -- por isso o consumidor confere o numero do quadro
   ANTES e DEPOIS de copiar e DESCARTA a copia se o produtor deu a volta no
   anel durante ela. Leitura suja detectada e melhor que leitura suja usada.

   O QUE A MEDICAO DIZ HOJE, E E O CONTRARIO DO QUE O NOME SUGERE
   --------------------------------------------------------------
   Quem ler isto daqui a tres meses vai supor que o caminho compartilhado ganhou
   do compositor, porque e para isso que ele existe. NAO GANHOU. A matriz M7
   mediu, com jogo real e duas camadas:

                       camada de baixo    CAMADA DE CIMA (a que chega na tela)
       compositor            50,3                    50,3
       compartilhado         53,6                    49,3

   O compartilhado ganha na de baixo, porque ela deixa de ser capturada pelo
   compositor, e PERDE na de cima -- que e a unica que a pessoa ve. A diferenca
   esta dentro do piso de ruido, entao o honesto e dizer que ele empatou e nao
   provou valor, e nao que ele perdeu.

   A hipotese para isso, e o motivo de o anel existir, e que o AcquireSync trava
   a THREAD DE CPU: com buffer unico, o consumidor segura o produtor durante
   toda a copia dele, e os dois processos se serializam. O anel tira o cadeado
   do caminho. Se ele tambem empatar, a terceira via anotada e ID3D11Fence
   compartilhada, que sinaliza na GPU em vez de travar a CPU, e que ninguem
   mediu ainda.

   Consequencia pratica para quem mexe na interface: o caminho pelo compositor
   NAO e a opcao ruim que se esconde. Ele e o mais conservador, empata ou leva
   leve vantagem no que importa, e e a primeira coisa que se pede para tentar
   quando as duas camadas dao problema estranho. */
constexpr uint32_t kModoMutex   = 1;
constexpr uint32_t kModoRotacao = 2;
constexpr uint32_t kMaxSlots    = 4;   /* 3 e o recomendado; 4 e o teto do vetor */

struct Header
{
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;        /* DXGI_FORMAT do que esta na textura */
    uint32_t generation;    /* sobe a cada recriacao (resolucao mudou) */
    uint64_t frame;         /* numero do quadro publicado; so cresce */
    uint64_t qpc;           /* QueryPerformanceCounter da publicacao */
    uint32_t producerPid;
    uint32_t publishing;    /* 1 enquanto o produtor esta vivo e publicando */
    wchar_t  texName[96];   /* nome do NT handle da textura desta geracao (modo mutex) */
    /* --- versao 2 em diante. Um consumidor que veja version==1 ignora daqui
       para baixo e trata como MODO_MUTEX, que era o unico que existia. --- */
    uint32_t modo;          /* kModoMutex | kModoRotacao */
    uint32_t slots;         /* quantas texturas no anel (modo rotacao) */
    uint32_t slot;          /* em qual delas esta o quadro publicado */
    uint32_t reservado;     /* alinhamento; zero */
};

/* ---------------------------------------------------------------- produtor */
class Producer
{
public:
    /* Liga (ou desliga, com nome vazio). Idempotente. */
    void enable(const wchar_t *name, void (*logfn)(const char *))
    {
        m_log = logfn;
        if (name == nullptr || name[0] == L'\0') { shutdown(); return; }
        if (m_enabled && wcscmp(m_name, name) == 0) return;
        shutdown();
        wcsncpy(m_name, name, 63); m_name[63] = L'\0';

        /* MODO ESCOLHIDO ANTES DE COMECAR, e nunca em voo. Le DLSS5_TRANSPORTE
           uma vez, aqui, porque a partir da criacao das texturas trocar de modo
           exigiria recriar a cadeia com ela viva - e recriar cadeia viva ja
           custou dois travamentos a este projeto. A camada de cima nao precisa
           de configuracao nenhuma: ela le 'modo' do cabecalho e obedece.
             (vazio) ou "mutex"   -> o de sempre, e o padrao
             "rotacao"            -> anel de 3, sem keyed mutex
             "rotacao4"           -> anel de 4, para o caso de 3 nao bastar */
        m_modo  = kModoMutex;
        m_slots = 1;
        {
            wchar_t env[24]{};
            const DWORD n = GetEnvironmentVariableW(L"DLSS5_TRANSPORTE", env, 24);
            if (n > 0 && n < 24)
            {
                if      (_wcsicmp(env, L"rotacao4") == 0) { m_modo = kModoRotacao; m_slots = 4; }
                else if (_wcsicmp(env, L"rotacao")  == 0) { m_modo = kModoRotacao; m_slots = 3; }
            }
        }
        m_enabled = true;
        if (m_modo == kModoRotacao)
        {
            char b[128];
            snprintf(b, sizeof(b), "[DLSS 5 Bridge] saida compartilhada pedida - modo ROTACAO de %u slots (sem keyed mutex)", (unsigned)m_slots);
            log(b);
        }
        else log("[DLSS 5 Bridge] saida compartilhada pedida - modo MUTEX (padrao)");
    }

    bool enabled() const { return m_enabled; }
    uint64_t frames() const { return m_frame; }
    uint64_t pulados() const { return m_skipped; }
    /* O modo EFETIVO, e nao o pedido. Se o anel nao nasceu e caiu para mutex,
       e este que diz a verdade - e e o que o painel precisa mostrar, senao a
       pessoa escolhe o experimental, ele cai, e ela nao fica sabendo. */
    uint32_t modo()  const { return m_modo; }
    uint32_t slots() const { return m_slots; }

    /* Chamado de dentro do reshade_finish_effects, com a RTV do alvo dos
       efeitos. Nao lanca, nao bloqueia e nunca deixa de retornar. */
    void publish(void *rtvNativeHandle)
    {
        if (!m_enabled || rtvNativeHandle == nullptr) return;

        ID3D11RenderTargetView *rtv = reinterpret_cast<ID3D11RenderTargetView *>(rtvNativeHandle);
        ID3D11Resource *res = nullptr;
        rtv->GetResource(&res);
        if (res == nullptr) return;

        ID3D11Texture2D *tex = nullptr;
        if (FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&tex))) || tex == nullptr)
        { res->Release(); return; }

        D3D11_TEXTURE2D_DESC td{};
        tex->GetDesc(&td);

        /* SDR de 8 bits por canal, e so. HDR (R16G16B16A16_FLOAT, R10G10B10A2)
           exige acordar o espaco de cor com a camada de cima, o que fica para
           depois; recusar aqui e mais honesto do que entregar cor errada. */
        if (td.Format != DXGI_FORMAT_B8G8R8A8_UNORM && td.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
            td.Format != DXGI_FORMAT_R8G8B8A8_UNORM && td.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            if (!m_saidFormat)
            {
                m_saidFormat = true;
                char b[192];
                snprintf(b, sizeof(b), "[DLSS 5 Bridge] saida compartilhada: formato %u nao e SDR de 8 bits - "
                                       "a camada de cima segue pelo compositor", (unsigned)td.Format);
                log(b);
            }
            tex->Release(); res->Release();
            return;
        }

        ID3D11Device *dev = nullptr;
        tex->GetDevice(&dev);
        if (dev == nullptr) { tex->Release(); res->Release(); return; }

        if (!ensure(dev, td.Width, td.Height, td.Format))
        { dev->Release(); tex->Release(); res->Release(); return; }

        /* ---- MODO ROTACAO: sem mutex, escreve no proximo slot do anel ----
           O produtor NUNCA espera aqui, e agora nem pode ser feito esperar: nao
           ha cadeado para o consumidor segurar. Ele escreve no slot seguinte e
           so entao anuncia, na ordem slot -> barreira -> frame. O consumidor le
           frame, copia do slot anunciado, e confere frame de novo; se o
           produtor deu a volta no anel durante a copia, ele descarta.
           Publicar o numero do quadro POR ULTIMO e o que torna o anuncio
           atomico do ponto de vista de quem le. */
        if (m_modo == kModoRotacao)
        {
            ID3D11DeviceContext *ctx = nullptr;
            dev->GetImmediateContext(&ctx);
            if (ctx != nullptr && m_head != nullptr)
            {
                const uint32_t s = (uint32_t)(m_frame % m_slots);
                ctx->CopyResource(m_slotTex[s], tex);
                ctx->Release();
                LARGE_INTEGER now; QueryPerformanceCounter(&now);
                ++m_frame;
                m_head->qpc  = (uint64_t)now.QuadPart;
                m_head->slot = s;
                MemoryBarrier();
                m_head->frame = m_frame;
            }
            else if (ctx != nullptr) ctx->Release();
            dev->Release(); tex->Release(); res->Release();
            return;
        }

        /* AcquireSync com timeout ZERO: a camada de baixo e o que a pessoa ve,
           e ela nao espera por ninguem. Perder um quadro aqui custa menos do
           que engasgar a imagem principal. */
        if (m_mutex != nullptr && m_mutex->AcquireSync(0, 0) == WAIT_OBJECT_0)
        {
            ID3D11DeviceContext *ctx = nullptr;
            dev->GetImmediateContext(&ctx);
            if (ctx != nullptr)
            {
                ctx->CopyResource(m_shared, tex);
                ctx->Release();
                LARGE_INTEGER now; QueryPerformanceCounter(&now);
                ++m_frame;
                if (m_head != nullptr)
                {
                    m_head->qpc   = (uint64_t)now.QuadPart;
                    /* o numero do quadro por ULTIMO: o consumidor le esse campo
                       para saber que ha imagem nova, e ele so pode mudar depois
                       de tudo o mais estar no lugar */
                    MemoryBarrier();
                    m_head->frame = m_frame;
                }
            }
            m_mutex->ReleaseSync(0);
        }
        else
        {
            ++m_skipped;
        }

        dev->Release(); tex->Release(); res->Release();
    }

    void shutdown()
    {
        if (m_head != nullptr) { m_head->publishing = 0; UnmapViewOfFile(m_head); m_head = nullptr; }
        if (m_map != nullptr)    { CloseHandle(m_map); m_map = nullptr; }
        if (m_mutex != nullptr)  { m_mutex->Release(); m_mutex = nullptr; }
        if (m_shared != nullptr) { m_shared->Release(); m_shared = nullptr; }
        if (m_texHandle != nullptr) { CloseHandle(m_texHandle); m_texHandle = nullptr; }
        m_w = m_h = 0; m_fmt = 0; m_enabled = false; m_name[0] = L'\0';
    }

private:
    void log(const char *s) { if (m_log) m_log(s); }

    /* Cria (ou recria, quando a resolucao ou o formato mudam) a textura
       compartilhada e o mapeamento. A geracao sobe a cada recriacao e entra no
       nome da textura, senao o consumidor reabriria a de antes. */
    bool ensure(ID3D11Device *dev, UINT w, UINT h, DXGI_FORMAT fmt)
    {
        if (m_shared != nullptr && w == m_w && h == m_h && (uint32_t)fmt == m_fmt) return true;

        if (m_shared != nullptr) { m_shared->Release(); m_shared = nullptr; }
        if (m_mutex != nullptr)  { m_mutex->Release();  m_mutex = nullptr; }
        if (m_texHandle != nullptr) { CloseHandle(m_texHandle); m_texHandle = nullptr; }

        ID3D11Device1 *dev1 = nullptr;
        if (FAILED(dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void **>(&dev1))) || dev1 == nullptr)
        {
            log("[DLSS 5 Bridge] saida compartilhada: sem ID3D11Device1 - desligando");
            m_enabled = false;
            return false;
        }

        ++m_generation;

        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        /* NTHANDLE sempre: da nome ao handle e o sujeita as regras de seguranca
           normais. Sem ele o runtime nao valida parametros, e a propria
           Microsoft diz que o comportamento "pode variar de driver para driver".
           KEYEDMUTEX so no modo de sempre; no anel ele e justamente o que sai. */
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        if (m_modo == kModoMutex) td.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

        const uint32_t n = (m_modo == kModoRotacao) ? m_slots : 1u;
        for (uint32_t i = 0; i < n; ++i)
        {
            ID3D11Texture2D *t = nullptr;
            HRESULT hr = dev1->CreateTexture2D(&td, nullptr, &t);
            if (FAILED(hr) || t == nullptr)
            {
                /* QUEDA PARA O CAMINHO DE SEMPRE, e nao desligar. A opcao nova e
                   experimental: se o anel nao nascer, a pessoa nao pode ficar
                   sem imagem por ter escolhido experimentar. Cai para o modo
                   mutex, avisa, e a de cima nem percebe - ela le o modo do
                   cabecalho. Se o mutex tambem falhar, ai sim desliga, e a
                   camada de cima volta para o compositor. */
                char b[200];
                if (m_modo == kModoRotacao)
                {
                    snprintf(b, sizeof(b), "[DLSS 5 Bridge] anel de %u slots nao pode ser criado (0x%08lX) - "
                             "CAINDO para o modo mutex, que e o padrao", (unsigned)m_slots, (unsigned long)hr);
                    log(b);
                    soltarAnel();
                    m_modo = kModoMutex; m_slots = 1;
                    dev1->Release();
                    --m_generation;      /* o ensure vai subir de novo */
                    return ensure(dev, w, h, fmt);
                }
                snprintf(b, sizeof(b), "[DLSS 5 Bridge] saida compartilhada: CreateTexture2D falhou (0x%08lX) - desligando", (unsigned long)hr);
                log(b);
                dev1->Release(); m_enabled = false; return false;
            }

            /* No anel, o nome leva o indice: o consumidor abre os N por nome.
               No modo de sempre o nome nao muda, para um consumidor antigo
               continuar achando a textura onde sempre esteve. */
            wchar_t texName[96];
            if (m_modo == kModoRotacao) _snwprintf(texName, 96, L"%ls_tex_%u_%u", m_name, (unsigned)m_generation, (unsigned)i);
            else                        _snwprintf(texName, 96, L"%ls_tex_%u",    m_name, (unsigned)m_generation);
            texName[95] = L'\0';

            IDXGIResource1 *dr = nullptr;
            if (SUCCEEDED(t->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void **>(&dr))) && dr != nullptr)
            {
                hr = dr->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                            texName, &m_texHandles[i]);
                dr->Release();
            }
            else hr = E_NOINTERFACE;

            if (FAILED(hr) || m_texHandles[i] == nullptr)
            {
                char b[200];
                if (m_modo == kModoRotacao)
                {
                    snprintf(b, sizeof(b), "[DLSS 5 Bridge] anel: CreateSharedHandle falhou no slot %u (0x%08lX) - "
                             "CAINDO para o modo mutex", (unsigned)i, (unsigned long)hr);
                    log(b);
                    t->Release(); soltarAnel();
                    m_modo = kModoMutex; m_slots = 1;
                    dev1->Release();
                    --m_generation;
                    return ensure(dev, w, h, fmt);
                }
                snprintf(b, sizeof(b), "[DLSS 5 Bridge] saida compartilhada: CreateSharedHandle falhou (0x%08lX) - desligando", (unsigned long)hr);
                log(b);
                t->Release(); dev1->Release(); m_enabled = false; return false;
            }

            if (m_modo == kModoMutex)
            {
                t->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&m_mutex));
                wcsncpy(m_texName0, texName, 95); m_texName0[95] = L'\0';
            }
            m_slotTex[i] = t;
        }
        m_shared = m_slotTex[0];
        m_texHandle = m_texHandles[0];
        if (m_modo == kModoRotacao) { wcsncpy(m_texName0, m_name, 63); m_texName0[63] = L'\0'; }
        m_w = w; m_h = h; m_fmt = (uint32_t)fmt;
        dev1->Release();

        if (!ensureMap()) return false;

        m_head->magic      = kMagic;
        m_head->version    = kVersion;
        m_head->width      = w;
        m_head->height     = h;
        m_head->format     = (uint32_t)fmt;
        m_head->producerPid = (uint32_t)GetCurrentProcessId();
        m_head->publishing = 1;
        wcsncpy(m_head->texName, m_texName0, 95); m_head->texName[95] = L'\0';
        m_head->modo  = m_modo;
        m_head->slots = (m_modo == kModoRotacao) ? m_slots : 1u;
        m_head->slot  = 0;
        m_head->reservado = 0;
        MemoryBarrier();
        m_head->generation = m_generation;   /* por ultimo: e o sinal de "reabra a textura" */

        char b[224];
        snprintf(b, sizeof(b), "[DLSS 5 Bridge] saida compartilhada pronta: %ux%u formato %u geracao %u modo=%s slots=%u",
                 (unsigned)w, (unsigned)h, (unsigned)fmt, (unsigned)m_generation,
                 (m_modo == kModoRotacao) ? "ROTACAO" : "MUTEX", (unsigned)m_head->slots);
        log(b);
        return true;
    }

    /* Solta so o anel, sem mexer no mapeamento nem no estado de "ligado".
       Existe para a queda de rotacao para mutex poder recomecar limpa. */
    void soltarAnel()
    {
        if (m_mutex != nullptr) { m_mutex->Release(); m_mutex = nullptr; }
        for (uint32_t i = 0; i < kMaxSlots; ++i)
        {
            if (m_slotTex[i] != nullptr)    { m_slotTex[i]->Release(); m_slotTex[i] = nullptr; }
            if (m_texHandles[i] != nullptr) { CloseHandle(m_texHandles[i]); m_texHandles[i] = nullptr; }
        }
        m_shared = nullptr; m_texHandle = nullptr;
    }

    bool ensureMap()
    {
        if (m_head != nullptr) return true;
        m_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Header), m_name);
        if (m_map == nullptr)
        {
            log("[DLSS 5 Bridge] saida compartilhada: CreateFileMapping falhou - desligando");
            m_enabled = false; return false;
        }
        m_head = (Header *)MapViewOfFile(m_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Header));
        if (m_head == nullptr)
        {
            CloseHandle(m_map); m_map = nullptr;
            log("[DLSS 5 Bridge] saida compartilhada: MapViewOfFile falhou - desligando");
            m_enabled = false; return false;
        }
        ZeroMemory(m_head, sizeof(Header));
        return true;
    }

    void (*m_log)(const char *) = nullptr;
    bool     m_enabled   = false;
    bool     m_saidFormat = false;
    wchar_t  m_name[64]  = { 0 };
    ID3D11Texture2D *m_shared = nullptr;   /* = m_slotTex[0]; so para o modo mutex */
    IDXGIKeyedMutex *m_mutex  = nullptr;   /* nulo no modo rotacao, de proposito */
    HANDLE   m_texHandle = nullptr;        /* = m_texHandles[0] */
    /* o anel. No modo mutex so o indice 0 e usado. */
    ID3D11Texture2D *m_slotTex[kMaxSlots]   = { nullptr, nullptr, nullptr, nullptr };
    HANDLE           m_texHandles[kMaxSlots] = { nullptr, nullptr, nullptr, nullptr };
    wchar_t  m_texName0[96] = { 0 };
    uint32_t m_modo  = kModoMutex;
    uint32_t m_slots = 1;
    HANDLE   m_map  = nullptr;
    Header  *m_head = nullptr;
    UINT     m_w = 0, m_h = 0;
    uint32_t m_fmt = 0;
    uint32_t m_generation = 0;
    uint64_t m_frame = 0;
    uint64_t m_skipped = 0;
};

} // namespace dlss5_shared
