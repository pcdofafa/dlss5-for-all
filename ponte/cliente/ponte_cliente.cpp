/* =====================================================================
   ponte_cliente.cpp -- dlss5-ponte64.dll: o cliente injetado no jogo.

   Fase 2 do Modo Integrado (CONTRATO-FASE2.md, 15/09/2026). Agente B1.

   O que faz, em uma frase: injetada no jogo, engancha o Present, copia o
   backbuffer para uma textura compartilhada, deixa o nosso app processar
   fora do processo, e blita o resultado de volta antes do Present real --
   falando o protocolo feed_ipc v9 pelo mesmo named pipe do DLSS5-Feeder.

   Estrutura:
     1. Estado global (P).
     2. Descoberta de vtables (device/swapchain descartaveis) e hooks.
     3. IPC v9: conectar, hello, build, frame (thread da ponte).
     4. Backend D3D11 (por quadro).
     5. Backend D3D12 (por quadro).
     6. Hooks: Present, Present1, ResizeBuffers, ResizeBuffers1, ResizeTarget, ExecuteCommandLists.
     7. Desligar / drenar / autodescarga.
     8. Thread da ponte e DllMain.

   Nada de trabalho no DllMain. Todo erro -> Desligar() -> Present original.
   ===================================================================== */
#include "ponte_comum.h"
#include "ponte_seh.h"      /* I10: a rede de seguranca - um estouro nosso nao fecha o jogo */
#include "ngx/ngx_hook.h"   /* G1 (fase 3): hook NGX -- sonda, upstream, controle */
#include "sr/sr_hook.h"     /* Q2 (degrau 2): hook FSR 2/3.1 e XeSS -> DLSS SR pelo app (sessao .up 0x200) */
#include "fg/fg_hook.h"     /* Q4 (fase 4): frame generation D3D12 -- a DLL vira dona da apresentacao */
#include "tela/tela.h"       /* X-DLL-2: contador de fps no jogo (C2) e modo de exibicao (E8) */
#include "ponte_dados.h"     /* J5: balcao de depth/MV do upscaler -> slots da fase 2 (o app, pela pelicula) */
#include "pipe_up.h"         /* X-FG-6: o pipe .up e um so, passa de mao entre ngx_upstream e sr_hook */
#include "dados/colheita.h"  /* X-FG-6: depth+MV do jogo sem sessao .up (Nativo comum) */

using namespace Ponte;

/* ================================================================== */
/* 1. Estado global                                                    */
/* ================================================================== */
namespace {

enum Api  { API_NENHUMA, API_D3D11, API_D3D12 };
enum Fase { FASE_CONECTANDO, FASE_ATIVA, FASE_DRENANDO, FASE_MORTA };

const int kMaxBuf = 8;

/* --- backend D3D11 --- */
struct BackD11
{
    ID3D11Device*         dev  = nullptr;
    ID3D11DeviceContext*  ctx  = nullptr;
    ID3D11Device5*        dev5 = nullptr;
    ID3D11DeviceContext4* ctx4 = nullptr;

    ID3D11Texture2D*          color = nullptr;
    ID3D11Texture2D*          output = nullptr;
    ID3D11Texture2D*          depthTex = nullptr;   /* Depth/MV: zerados ate o J5; com o balcao (ponte_dados.h) */
    ID3D11Texture2D*          mvTex = nullptr;      /* recebem o dado real do upscaler, na resolucao de render */
    HANDLE                    hColor = nullptr, hOutput = nullptr, hDepth = nullptr, hMv = nullptr;
    ID3D11ShaderResourceView* outputSrv = nullptr;
    ID3D11Fence*              fIn = nullptr, *fOut = nullptr;

    ID3D11VertexShader*       vs = nullptr;
    ID3D11PixelShader*        ps = nullptr;
    ID3D11SamplerState*       amostrador = nullptr;
    ID3D11BlendState*         blend = nullptr;
    ID3D11DepthStencilState*  depth = nullptr;
    ID3D11RasterizerState*    raster = nullptr;
};

/* --- backend D3D12 --- */
struct BackD12
{
    ID3D12Device*        dev   = nullptr;
    ID3D12CommandQueue*  fila  = nullptr;   /* a fila do jogo (do hook de ExecuteCommandLists) */

    ID3D12Resource*      color = nullptr;
    ID3D12Resource*      output = nullptr;
    ID3D12Resource*      depth = nullptr, *mv = nullptr;   /* zerados ate o J5; ver BackD11::depthTex */
    HANDLE               hColor = nullptr, hOutput = nullptr, hDepth = nullptr, hMv = nullptr;
    ID3D12Fence*         fIn = nullptr, *fOut = nullptr;
    /* J5: as texturas da sessao .up de onde a lista deste buffer copiou depth/MV. Referencias
       NOSSAS, soltas so depois de a fence de reciclagem passar pela lista (D12EsperarAlloc) - o
       mesmo par tomar/soltar do fg_hook (X-FG-3 §1.3): nada e solto com a GPU lendo. */
    ID3D12Resource*      srcDepth[kMaxBuf] = {}, *srcMv[kMaxBuf] = {};

    ID3D12CommandAllocator* allocCopia[kMaxBuf] = {};
    ID3D12CommandAllocator* allocBlit[kMaxBuf]  = {};
    ID3D12GraphicsCommandList* listaCopia = nullptr;
    ID3D12GraphicsCommandList* listaBlit  = nullptr;
    ID3D12Fence*         fRecicla = nullptr;
    HANDLE               evRecicla = nullptr;
    uint64_t             reciclaVal = 0;
    uint64_t             fenceDoBuf[kMaxBuf] = {};

    ID3D12RootSignature* rootSig = nullptr;
    ID3D12PipelineState* pso = nullptr;
    ID3D12DescriptorHeap* rtvHeap = nullptr;   /* nBuf RTVs dos backbuffers */
    ID3D12DescriptorHeap* srvHeap = nullptr;   /* 1 SRV do Output (shader-visible) */
    UINT                 rtvStride = 0;
    UINT                 nBuf = 0;
};

struct Tempos { double copia = 0, espera = 0, blit = 0; };

/* R1 (16/09/2026): VIGIA DA SAIDA. No RE2 o device D3D12 do app caiu (nvlddmkm 153) e
   ninguem viu: o app seguiu devolvendo, e a DLL seguiu copiando um Output que nunca mais
   mudou - imagem congelada com o jogo rodando. A cada kVigiaCada quadros a DLL copia um
   bloco kVigiaLado x kVigiaLado do centro da Color (a entrada deste quadro) e do Output
   (o que vai ao backbuffer) para memoria de leitura, sem esperar a GPU, e compara hashes.
   Entrada mudando e saida parada por mais de kVigiaParadaMs = passagem direta (sem blit).
   Volta sozinha quando a saida muda. Entrada parada (jogo pausado) nunca dispara. */
const UINT    kVigiaLado = 64;
const UINT    kVigiaCada = 30;
const double  kVigiaParadaMs = 1000.0;

struct Vigia
{
    bool     desligada = false;          /* recurso nao nasceu: vigia fora nesta sessao */
    bool     pendente = false;           /* copias gravadas, esperando a GPU */
    uint64_t proximo = 0;                /* quadro da proxima amostra */
    uint64_t fencePend = 0;              /* D3D12: fRecicla que conclui as copias */
    bool     temHash = false;
    uint64_t hIn = 0, hOut = 0;
    int64_t  saidaMudouEm = 0;           /* QPC da ultima mudanca da saida (ou da entrada parada) */
    int      entradaMudou = 0;           /* mudancas da entrada desde entao */
    bool     direta = false;             /* passagem direta ligada */
    int64_t  diretaDesde = 0;
    uint64_t quadrosDiretos = 0;
    int64_t  entradaMudouEm = 0;         /* P1: QPC da ultima mudanca da entrada (movimento) */
    bool     parado = false;             /* P1: imagem do jogo parada ha 1,5 s (menu/pausa), publicado */
    /* D3D12: buffers de leitura e o layout de cada bloco */
    ID3D12Resource* rbIn = nullptr, *rbOut = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fpIn = {}, fpOut = {};
    UINT64   bytesIn = 0, bytesOut = 0;
    /* D3D11: texturas de staging */
    ID3D11Texture2D* stIn = nullptr, *stOut = nullptr;
};

struct Ponte_
{
    HMODULE hModule = nullptr;

    /* vtables descobertas e originais (trampolins) */
    void** vtSwap  = nullptr;
    void** vtSwap2 = nullptr;   /* se o D3D12 tiver vtable de swapchain distinta */
    void** vtFila  = nullptr;
    void*  origPresent = nullptr, *origPresent1 = nullptr;
    void*  origResizeBuffers = nullptr, *origResizeTarget = nullptr;
    void*  origECL = nullptr;
    void*  origGetBuffer = nullptr, *origGetCurBB = nullptr;   /* Q4: FG engancha estes para dar proxies ao jogo */
    void*  origSetFs = nullptr;                                /* C9/E8: SetFullscreenState */
    void*  origResizeBuffers1 = nullptr;                       /* B19: IDXGISwapChain3::ResizeBuffers1 */
    /* A11 (17/09/2026): os campos acima sao os originais da PRIMEIRA vtable (vtSwap). Quando o
       processo tem duas (D3D11 e D3D12 juntos, ou uma sobreposicao), a segunda tem os SEUS
       ponteiros: chamar o original da outra passaria por uma funcao de outra classe do DXGI e
       derrubaria o jogo. Antes todos os slots guardavam so o primeiro que aparecesse. */
    struct OrigsSwap
    {
        void* present = nullptr, *present1 = nullptr;
        void* resizeBuffers = nullptr, *resizeBuffers1 = nullptr, *resizeTarget = nullptr;
        void* getBuffer = nullptr, *getCurBB = nullptr, *setFs = nullptr;
    } orig2;                                                   /* A11: os da vtSwap2 */
    bool   sc3Swap = false, sc3Swap2 = false;                  /* B19: o descartavel da vtable abre IDXGISwapChain3 */
    bool   hooksInstalados = false;
    std::atomic<bool> hooksRestaurados{ false };

    /* pipe / host */
    HANDLE pipe = nullptr;
    DWORD  hostPid = 0;
    DWORD  jogoPid = 0;
    std::mutex muPipe;   /* uma escrita por vez no pipe */

    /* sessao */
    std::atomic<Api> api{ API_NENHUMA };
    std::atomic<Fase> fase{ FASE_CONECTANDO };
    IDXGISwapChain* swapchain = nullptr;   /* travado no primeiro Present real */

    /* desc do backbuffer (do primeiro Present / apos resize) */
    UINT  bbW = 0, bbH = 0, bbBuffers = 0, bbFlags = 0;
    DXGI_FORMAT bbFmt = DXGI_FORMAT_UNKNOWN;      /* formato cru do backbuffer */
    DXGI_FORMAT corFmt = DXGI_FORMAT_UNKNOWN;     /* tipado, vai no color_fmt */
    DXGI_FORMAT saidaFmt = DXGI_FORMAT_R8G8B8A8_UNORM;  /* o host devolve isto */
    std::atomic<bool> descPronto{ false };

    /* build / laco */
    bool   asyncHome = true;          /* pipelined = padrao */
    std::atomic<bool> pedidoBuild{ false };
    std::atomic<bool> built{ false };
    uint64_t n = 0;                   /* numero do quadro */
    uint64_t sentN = 0;
    bool   outValido = false;
    bool   copiaDireta = false;       /* F0: Output no formato do backbuffer -> CopyResource, sem shader */
    bool   avisouSemFila = false;
    bool   logouHook = false;
    bool   sonda = false;             /* G1: modo "sonda" (%TEMP%\dlss5-ponte.<pid>.modo = sonda): sem host, sem build; so os hooks NGX logam */

    /* A CORRIDA DO REBUILD (18/09/2026) - a causa das quatro quedas do `Resident Evil 2.exe`
       dentro desta DLL (16/09 13:21 e 13:47, 17/09 23:38 e 23:41), todas ao mudar a resolucao
       com o Nativo ligado. O `DerrubarConjunto` rodava na thread do JOGO, de dentro do
       ResizeBuffers, e soltava device, allocators, listas e texturas na hora; quem remonta e
       a thread da PONTE, que fica presa no `Ler` do pipe esperando o ack do Build por dezenas
       ou centenas de ms. O log da sessao 25844 mostra a ordem: "derrubando o conjunto" (2a
       vez) e SO DEPOIS "build enviado" - o Build da primeira ainda estava montando quando a
       segunda derrubada soltou tudo por baixo dele.
       Daqui em diante: a thread do jogo so MARCA (`derrubarPedido`), incrementa a `geracao` e
       solta APENAS o que prende os backbuffers, que e obrigatorio antes do ResizeBuffers
       original. Quem solta o resto e quem remonta - a thread da ponte -, e como as duas coisas
       passaram a acontecer na MESMA thread, a corrida deixa de existir por construcao, sem
       trava nenhuma no caminho por quadro. */
    std::atomic<unsigned> geracao{ 0 };            /* muda a cada derrubada pedida */
    std::atomic<bool>     derrubarPedido{ false }; /* a thread do jogo pediu; a da ponte completa */
    std::atomic<int>      noQuadro{ 0 };           /* threads DENTRO do conjunto agora */

    /* I10: A REDE DE SEGURANCA. `inerte` = um hook nosso estourou; daqui em diante todos
       eles so chamam o original, como se nunca tivessemos enganchado nada, e o jogo segue.
       Conferido ANTES da guarda em cada hook (ver ponte_seh.h: um estouro pode ter deixado
       trava presa, e ninguem mais pode tentar pega-la). So vai de false para true, uma vez
       por sessao. */
    std::atomic<bool> inerte{ false };
    char   inerteOnde[40] = "";       /* qual hook */
    DWORD  inerteCode = 0;
    void*  inerteAddr = nullptr;

    /* fences: valores validos no NOSSO processo (o host duplicou para ca) */
    uint64_t ackFenceIn = 0, ackFenceOut = 0;

    /* eventos / thread */
    HANDLE evBuild  = nullptr;   /* o Present pediu um build */
    HANDLE evStop   = nullptr;   /* Desligar */
    HANDLE evUnload = nullptr;   /* o dreno terminou; a thread pode descarregar */
    HANDLE evPipe   = nullptr;   /* B16: o OVERLAPPED do pipe (so com muPipe) */
    HANDLE thread   = nullptr;

    /* dreno / motivo */
    char   motivo[192] = "";
    std::atomic<int> drenados{ 0 };
    std::atomic<bool> logouPrimeiroPresent{ false };
    /* C13: descarga adiada enquanto o jogo desenha nos proxies do FG (o Present segue espelhando) */
    std::atomic<bool> residual{ false };
    bool foiResidual = false;
    /* C16: Presents dentro do espelho; C16/C18: 0 sem reinjecao, 1 contagem confirmou, 2 contagem ilegivel (reserva) */
    std::atomic<int> noEspelho{ 0 };
    int reinjecao = 0;
    bool copiaCarregada = false;

    /* medicao (janela de 300 quadros) */
    Tempos acc;
    double  accPresent = 0;
    /* B4: soluco (intervalo entre Presents do jogo > 60 ms) com as partes do quadro */
    Tempos ultimo;
    int64_t saidaAnt = 0;
    int solucos = 0;
    uint64_t quadrosJanela = 0;
    int64_t  janelaInicio = 0;

    /* J5 (18/09/2026): O BALCAO (ponte_dados.h). O gancho do upscaler (sr_hook / ngx_upstream)
       deixa aqui onde estao depth e MV do jogo; o Present da fase 2 copia para os slots
       SLOT_DEPTH/SLOT_MV e avisa o app com 'D'. Um pacote por dispatch: `dadosSeq` anda a cada
       publicacao e `dadosUsada` guarda o ultimo consumido - pacote velho NAO e reaproveitado
       (MV do quadro errado move a imagem para o lado errado; pior que nao ter MV). */
    std::mutex     muDados;
    Dados::Pacote  dados;                  /* texturas D3D12 com AddRef nosso enquanto publicadas */
    uint64_t       dadosSeq = 0, dadosUsada = 0;
    int64_t        dadosQpc = 0;
    bool           hostEntendeD = false;   /* o BuildAck trouxe kAckDados */
    bool           dadosLogou = false;     /* a linha "depth+mv do jogo publicados", uma vez por sessao */
    unsigned       dadosRecusas = 0;       /* avisos de pacote recusado (limitados) */
    uint64_t       janDados = 0;           /* quadros da janela de 300 com dado real nos slots */
    uint64_t       dadosEnviados = 0;

    BackD11 d11;
    BackD12 d12;
    Vigia   vigia;   /* R1 */
};
Ponte_ P;

/* ================================================================== */
/* HLSL do blit de saida (Output -> backbuffer). Um so shader serve para  */
/* qualquer formato de destino: o RTV converte na escrita (inclusive a     */
/* ordem BGRA), e a amostra le o Output RGBA8 do host. Nao ha tone mapping. */
/* ================================================================== */
const char kBlitHlsl[] =
    "Texture2D<float4> src : register(t0);\n"
    "SamplerState smp : register(s0);\n"
    "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VSOut vs(uint id : SV_VertexID) {\n"
    "  VSOut o; float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  o.uv = uv; o.pos = float4(uv * float2(2,-2) + float2(-1,1), 0, 1); return o; }\n"
    "float4 ps(VSOut i) : SV_Target { return src.Sample(smp, i.uv); }\n";

const char kBlitHlslRS[] =   /* versao D3D12 com root signature embutida */
    "#define RS \"RootFlags(0),\"\\\n"
    "  \"DescriptorTable(SRV(t0)),\"\\\n"
    "  \"StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP)\"\n"
    "Texture2D<float4> src : register(t0);\n"
    "SamplerState smp : register(s0);\n"
    "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "[RootSignature(RS)]\n"
    "VSOut vs(uint id : SV_VertexID) {\n"
    "  VSOut o; float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  o.uv = uv; o.pos = float4(uv * float2(2,-2) + float2(-1,1), 0, 1); return o; }\n"
    "[RootSignature(RS)]\n"
    "float4 ps(VSOut i) : SV_Target { return src.Sample(smp, i.uv); }\n";

/* d3dcompiler carregado a mao (como o app faz), para nao linkar estatico. */
static pD3DCompile g_D3DCompile = nullptr;
static HMODULE     g_compilador = nullptr;
static bool CarregarCompilador()
{
    if (g_D3DCompile != nullptr) return true;
    g_compilador = LoadLibraryW(L"d3dcompiler_47.dll");
    if (g_compilador == nullptr) { Log("ponte: d3dcompiler_47.dll indisponivel"); return false; }
    g_D3DCompile = reinterpret_cast<pD3DCompile>(reinterpret_cast<void*>(GetProcAddress(g_compilador, "D3DCompile")));
    if (g_D3DCompile == nullptr) { Log("ponte: D3DCompile ausente no d3dcompiler_47.dll"); return false; }
    return true;
}

/* ================================================================== */
/* Declaracoes adiantadas                                              */
/* ================================================================== */
void Desligar(const char* motivo);
void RestaurarHooks();

/* F0 (15/09/2026, HDR de 16 bits): o Output que a DLL PROPOE no Build para um dado
   formato de cor. Cor em 16F (scRGB) ou 10:10:10:2 pede Output no MESMO formato: o host
   processa nessa faixa e devolve sem tone mapping, e a volta ao backbuffer e um
   CopyResource. Cor de 8 bits (RGBA8/BGRA8) continua pedindo RGBA8, como antes do F0.
   Mesma regra do OutputFormatFor do cliente de referencia (dlss5-feed32.cpp) e do
   FormatoSaidaPara do host; o host confere e ecoa no ack. */
static DXGI_FORMAT FormatoSaidaPara(DXGI_FORMAT cor)
{
    switch (FormatoTipado(cor))
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_UNORM:  return DXGI_FORMAT_R10G10B10A2_UNORM;
    default:                             return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

/* ================================================================== */
/* 3. IPC v9                                                           */
/* ================================================================== */

/* Transferencia sincrona no pipe (mensagens pequenas; o host responde na
   hora). Nunca bloqueia mais que timeoutMs: o pipe do host esta em modo
   BYTE|WAIT com FILE_FLAG_OVERLAPPED, entao usamos OVERLAPPED com espera. */
static bool PipeIO(void* buf, DWORD len, bool escrever, DWORD timeoutMs)
{
    BYTE* p = static_cast<BYTE*>(buf);
    /* B16 (H3): um evento so, criado no DllMain, em vez de criar e fechar um por operacao.
       Toda chamada acontece com P.muPipe na mao, e cada operacao zera o evento antes de
       comecar; um cancelamento por timeout espera a conclusao (GetOverlappedResult TRUE),
       entao nenhuma operacao antiga sinaliza a seguinte. */
    HANDLE ev = P.evPipe;
    if (ev == nullptr) return false;
    bool ok = true;
    while (len > 0)
    {
        OVERLAPPED ov = {}; ov.hEvent = ev; ResetEvent(ev);
        const BOOL comecou = escrever ? WriteFile(P.pipe, p, len, nullptr, &ov)
                                      : ReadFile(P.pipe, p, len, nullptr, &ov);
        if (!comecou && GetLastError() != ERROR_IO_PENDING) { ok = false; break; }
        const DWORD r = WaitForSingleObject(ev, timeoutMs);
        if (r != WAIT_OBJECT_0) { CancelIoEx(P.pipe, &ov); DWORD m; GetOverlappedResult(P.pipe, &ov, &m, TRUE); ok = false; break; }
        DWORD moveu = 0;
        if (!GetOverlappedResult(P.pipe, &ov, &moveu, FALSE) || moveu == 0) { ok = false; break; }
        p += moveu; len -= moveu;
    }
    return ok;
}
static bool Escrever(const void* buf, DWORD len) { return PipeIO(const_cast<void*>(buf), len, true, 4000); }
static bool Ler(void* buf, DWORD len)            { return PipeIO(buf, len, false, 60000); }

/* ================================================================== */
/* J5. Depth e MV do jogo nos slots da fase 2 (o balcao, ponte_dados.h) */
/* ================================================================== */
/* O quadro n vai ao app com 'D' (se ha dado real nos slots) e depois o 'F' de sempre, na mesma
   trava do pipe, para as duas mensagens nunca se separarem. Um app antigo nunca recebe 'D':
   `hostEntendeD` so vem do ack. */
static bool MandarQuadro(uint64_t n, const FeedDadosMsg* dados)
{
    std::lock_guard<std::mutex> l(P.muPipe);
    if (dados != nullptr && P.hostEntendeD)
    {
        const BYTE td = 'D';
        if (!Escrever(&td, 1) || !Escrever(dados, sizeof(*dados))) return false;
        ++P.dadosEnviados;
    }
    const BYTE tag = 'F'; FeedFrameMsg fm = { n, 0, 0, 0 };
    if (dados != nullptr) { fm.reset = dados->flags & kDadosReset; fm.jitter_x = dados->jitter_x; fm.jitter_y = dados->jitter_y; }
    return Escrever(&tag, 1) && Escrever(&fm, sizeof(fm));
}

/* O pacote serve para os slots da fase 2? Os slots sao bbW x bbH: o dado de render cabe no
   canto superior esquerdo desde que nao passe da tela. Qualquer duvida = sem 'D' neste quadro
   (o app fica no fluxo optico), com motivo no log nas primeiras vezes. */
static bool PacoteCabe(const Dados::Pacote& p, bool d3d12, char* porque, size_t n)
{
    const char* m = nullptr;
    if (d3d12 ? (!p.depth12 || !p.mv12) : (!p.depth11 || !p.mv11)) m = "o pacote nao e da API do swapchain (ou veio sem depth+mv)";
    else if (d3d12 && (p.fila == nullptr || p.fila != P.d12.fila)) m = "o upscaler grava numa fila e a apresentacao esta em outra";
    else if (!p.renderW || !p.renderH || p.renderW > P.bbW || p.renderH > P.bbH) m = "resolucao interna fora do tamanho da tela";
    else if (!p.mvW || !p.mvH || p.mvW > P.bbW || p.mvH > P.bbH) m = "MV maior que a tela";
    else if (!(p.mvScaleX == p.mvScaleX) || !(p.mvScaleY == p.mvScaleY) || p.mvScaleX == 0 || p.mvScaleY == 0) m = "escala de MV invalida";
    if (m != nullptr) { if (porque && n) snprintf(porque, n, "%s", m); return false; }
    return true;
}

/* Thread do jogo, no Present: o pacote DESTE quadro (novo desde o ultimo consumido e com menos
   de 200 ms), ja conferido, e a mensagem 'D' pronta. As texturas saem com AddRef: em D3D12 o
   chamador as guarda em srcDepth/srcMv[idx] ate a GPU terminar a lista; em D3D11 solta logo
   depois de gravar a copia (o runtime segura o recurso ate a GPU terminar). */
static bool PegarDadosDoQuadro(uint64_t n, bool d3d12, Dados::Pacote* out, FeedDadosMsg* msg)
{
    if (!P.hostEntendeD) return false;
    Dados::Pacote p; uint64_t seq = 0; int64_t qpc = 0;
    {
        std::lock_guard<std::mutex> g(P.muDados);
        if (P.dadosSeq == 0 || P.dadosSeq == P.dadosUsada) return false;   /* o jogo nao chamou o upscaler neste quadro */
        p = P.dados; seq = P.dadosSeq; qpc = P.dadosQpc;
        /* a referencia sai daqui com a trava: o Limpar() da sessao .up roda na thread da ponte
           e poderia soltar a ultima entre a leitura do ponteiro e a copia */
        if (d3d12) { if (p.depth12) p.depth12->AddRef(); if (p.mv12) p.mv12->AddRef(); }
        else       { if (p.depth11) p.depth11->AddRef(); if (p.mv11) p.mv11->AddRef(); }
    }
    P.dadosUsada = seq;   /* consumido (servindo ou nao): pacote nunca vale para dois quadros */
    char porque[160] = "";
    bool ok = PacoteCabe(p, d3d12, porque, sizeof porque);
    if (ok && Ms(qpc, Agora()) > 200.0) { ok = false; snprintf(porque, sizeof porque, "pacote com mais de 200 ms"); }
    if (!ok)
    {
        if (d3d12) { Soltar(p.depth12); Soltar(p.mv12); } else { Soltar(p.depth11); Soltar(p.mv11); }
        if (P.dadosRecusas < 5) { ++P.dadosRecusas; Log("ponte: depth+mv do jogo recusados neste quadro (%s) - o app fica no fluxo optico", porque); }
        return false;
    }
    memset(msg, 0, sizeof(*msg));
    msg->n = n;
    msg->render_w = p.renderW; msg->render_h = p.renderH; msg->mv_w = p.mvW; msg->mv_h = p.mvH;
    msg->jitter_x = p.jitterX; msg->jitter_y = p.jitterY; msg->mv_scale_x = p.mvScaleX; msg->mv_scale_y = p.mvScaleY;
    msg->frame_dt_ms = p.dtMs;
    msg->cam_near = p.camNear; msg->cam_far = p.camFar; msg->cam_fov = p.camFov; msg->view_to_m = p.viewToM;
    msg->flags = (p.reset ? kDadosReset : 0u) | (p.depthInv == 1 ? kDadosDepthInv : 0u) | (p.depthInv < 0 ? kDadosDepthIncog : 0u) |
                 (p.mvDisplayRes ? kDadosMvTela : 0u) | (p.mvJittered ? kDadosMvJitter : 0u) | (p.mvNdc ? kDadosMvNdc : 0u);
    msg->origem = p.origem;
    msg->qpc_dispatch = qpc;
    if (!P.dadosLogou)
    {
        P.dadosLogou = true;
        Log("ponte: depth+mv do jogo publicados nos slots da fase 2 (%s: render %ux%u, mv %ux%u%s, mvscale %.4f,%.4f, jitter %.4f,%.4f, depth_inv %d, mv_com_jitter %d%s, tela %ux%u)",
            p.origem == 1 ? "dlss" : p.origem == 2 ? "fsr31" : p.origem == 3 ? "fsr2" : p.origem == 4 ? "xess" : "?",
            p.renderW, p.renderH, p.mvW, p.mvH, p.mvDisplayRes ? " (tela)" : " (render)", p.mvScaleX, p.mvScaleY, p.jitterX, p.jitterY,
            p.depthInv, p.mvJittered ? 1 : 0, p.mvNdc ? ", MV EM NDC (o app recusa)" : "", P.bbW, P.bbH);
        Log("ponte: convencao publicada: MV em pixels de render depois de mvscale, +x direita, +y baixo, do quadro atual para o anterior (a mesma do J3); depth R32F no canto superior esquerdo do slot");
    }
    *out = p;
    return true;
}

/* ponte_modo (pipelined|sincrono). Nao ha mensagem no fio v9 para o host mandar
   o modo antes do Build (FEED_BUILD_ASYNC_HOME e escolha do cliente), entao a
   DLL o descobre na hora de montar o Build, nesta ordem (decisao do coordenador):
     1. %TEMP%\dlss5-ponte.<pid do jogo>.modo -- texto "pipelined" ou "sincrono",
        gravado pelo app ANTES de injetar (o app cuida de apagar);
     2. [ponte] modo do dlss5forall.ini ao lado do exe do host;
     3. pipelined. */
static bool ModoEhSincrono(const wchar_t* s) { return _wcsicmp(s, L"sincrono") == 0 || _wcsicmp(s, L"sync") == 0; }

static void DescobrirModo()
{
    P.asyncHome = true;
    /* 1. arquivo em %TEMP% */
    wchar_t tmp[MAX_PATH] = L"";
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n > 0 && n < MAX_PATH)
    {
        wchar_t caminho[MAX_PATH];
        _snwprintf(caminho, MAX_PATH, L"%sdlss5-ponte.%lu.modo", tmp, GetCurrentProcessId());
        FILE* f = _wfopen(caminho, L"rb");
        if (f != nullptr)
        {
            char buf[64] = {};
            size_t lidos = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            buf[lidos] = 0;
            /* apara espacos/quebras */
            char* ini = buf; while (*ini == ' ' || *ini == '\t' || *ini == '\r' || *ini == '\n') ++ini;
            char* fim = ini + strlen(ini); while (fim > ini && (fim[-1] == ' ' || fim[-1] == '\t' || fim[-1] == '\r' || fim[-1] == '\n')) *--fim = 0;
            wchar_t w[64]; for (int i = 0; i < 64; ++i) { w[i] = (wchar_t)(unsigned char)ini[i]; if (!ini[i]) break; }
            const bool valido = _wcsicmp(w, L"pipelined") == 0 || ModoEhSincrono(w);
            if (valido) { P.asyncHome = !ModoEhSincrono(w); Log("ponte: modo lido de %ls = %s", caminho, P.asyncHome ? "pipelined" : "sincrono"); return; }
            Log("ponte: %ls com valor invalido (\"%s\"); caindo no ini", caminho, ini);
        }
    }
    /* 2. ini ao lado do exe do host */
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, P.hostPid);
    if (h == nullptr) return;
    wchar_t exe[MAX_PATH]; DWORD tam = MAX_PATH;
    bool temExe = QueryFullProcessImageNameW(h, 0, exe, &tam) != FALSE;
    CloseHandle(h);
    if (!temExe) return;
    if (wchar_t* s = wcsrchr(exe, L'\\')) *(s + 1) = 0;
    wchar_t ini[MAX_PATH]; _snwprintf(ini, MAX_PATH, L"%sdlss5forall.ini", exe);
    wchar_t modo[32] = L"";
    GetPrivateProfileStringW(L"ponte", L"modo", L"pipelined", modo, 32, ini);
    P.asyncHome = !ModoEhSincrono(modo);
    Log("ponte: modo lido do ini (%ls) = %s", ini, P.asyncHome ? "pipelined" : "sincrono");
}

/* Conecta no pipe do host (o app ja escuta), faz o handshake e guarda os
   dados. Chamada na thread da ponte. Logs exatos do contrato. */
static bool ConectarHost()
{
    P.jogoPid = GetCurrentProcessId();
    wchar_t nome[128];
    _snwprintf(nome, 128, L"\\\\.\\pipe\\dlss5-feed.%lu", P.jogoPid);

    const int64_t t0 = Agora();
    for (int i = 0; i < 50; ++i)   /* ate 5 s */
    {
        P.pipe = CreateFileW(nome, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (P.pipe != INVALID_HANDLE_VALUE) break;
        P.pipe = nullptr;
        if (WaitForSingleObject(P.evStop, 100) == WAIT_OBJECT_0) { Desligar("Parar durante a conexao"); return false; }
    }
    if (P.pipe == nullptr) { Desligar("o pipe do host nao apareceu em 5 s"); return false; }

    /* Descobre o pid do app (servidor do pipe) e monta o self_process: um
       handle do NOSSO processo (o jogo) valido NO APP. Nunca abrimos o jogo de
       fora; abrimos o app (mesmo usuario, nao elevado) so para o DuplicateHandle. */
    if (!GetNamedPipeServerProcessId(P.pipe, &P.hostPid))
    { Desligar("GetNamedPipeServerProcessId falhou"); return false; }
    HANDLE hApp = OpenProcess(PROCESS_DUP_HANDLE, FALSE, P.hostPid);
    HANDLE selfNoApp = nullptr;
    if (hApp != nullptr)
    {
        DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), hApp, &selfNoApp,
                        PROCESS_DUP_HANDLE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, 0);
        CloseHandle(hApp);
    }
    if (selfNoApp == nullptr)
    { Desligar("nao consegui duplicar o handle do jogo para o app (self_process)"); return false; }

    FeedHello hello = { kMagic, kVersao, P.jogoPid, CLIENTE_D3D11, ComoValor(selfNoApp) };
    FeedHelloAck ack = {};
    bool trocou = false;
    { std::lock_guard<std::mutex> l(P.muPipe);   /* B16: o evento do pipe so com a trava */
      trocou = Escrever(&hello, sizeof(hello)) && Ler(&ack, sizeof(ack)); }
    if (!trocou || ack.magic != kMagic)
    { Desligar("handshake falhou"); return false; }
    if (ack.version != kVersao)
    { char m[96]; snprintf(m, sizeof(m), "host fala protocolo v%u, a ponte v%u", ack.version, kVersao); Desligar(m); return false; }

    const double ms = Ms(t0, Agora());
    Log("ponte: conectado ao host pid=%lu em %.0f ms", P.hostPid, ms);
    return true;
}

/* ================================================================== */
/* R1. Vigia da saida (comum aos dois backends; ver struct Vigia)        */
/* ================================================================== */
static UINT VigiaBytesPorTexel(DXGI_FORMAT f) { return FormatoTipado(f) == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4; }

/* FNV-1a sobre os texels do bloco: so os bytes de cada linha, nunca o preenchimento do pitch. */
static uint64_t VigiaHash(const uint8_t* p, UINT pitch, DXGI_FORMAT f)
{
    const UINT porLinha = kVigiaLado * VigiaBytesPorTexel(f);
    uint64_t h = 1469598103934665603ull;
    for (UINT y = 0; y < kVigiaLado; ++y)
    {
        const uint8_t* l = p + (size_t)y * pitch;
        for (UINT i = 0; i < porLinha; ++i) { h ^= l[i]; h *= 1099511628211ull; }
    }
    return h;
}

/* Amostra neste quadro? A cada kVigiaCada quadros (ou 500 ms, para jogo lento), so com
   Output valido e nenhuma amostra ainda na GPU. */
static bool VigiaDevida(uint64_t n)
{
    Vigia& v = P.vigia;
    if (v.desligada || v.pendente || !P.outValido) return false;
    if (P.bbW < kVigiaLado || P.bbH < kVigiaLado) return false;
    static int64_t ultima = 0;
    const int64_t agora = Agora();
    if (n < v.proximo && (ultima == 0 || Ms(ultima, agora) < 500.0)) return false;
    v.proximo = n + kVigiaCada;
    ultima = agora;
    return true;
}

static void VigiaCaixa(UINT* x0, UINT* y0)
{
    *x0 = (P.bbW - kVigiaLado) / 2;
    *y0 = (P.bbH - kVigiaLado) / 2;
}

/* O app le isto a cada 1 s (vigia de saude do Nativo, sinal 3): %TEMP%\dlss5-ponte.<pid>.saude.
   So muda na transicao; o app apaga o arquivo ao ligar a ponte, entao nunca le um velho. */
static void VigiaPublicar(bool direta, double paradaMs)
{
    wchar_t tmp[MAX_PATH] = L"", caminho[MAX_PATH] = L"";
    DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (k == 0 || k >= MAX_PATH) return;
    _snwprintf(caminho, MAX_PATH, L"%sdlss5-ponte.%lu.saude", tmp, GetCurrentProcessId());
    caminho[MAX_PATH - 1] = 0;
    FILE* f = _wfopen(caminho, L"wb");
    if (f == nullptr) return;
    /* P1: movendo=0 com a imagem do jogo parada (o sinal 5 da vigia do app ignora quedas assim) */
    fprintf(f, "direta=%d\nparada_ms=%.0f\nmovendo=%d\n", direta ? 1 : 0, paradaMs, P.vigia.parado ? 0 : 1);
    fclose(f);
}

/* A decisao, com os hashes de uma amostra (entrada e saida). */
static void VigiaDecidir(uint64_t hIn, uint64_t hOut)
{
    Vigia& v = P.vigia;
    const int64_t agora = Agora();
    if (!v.temHash)
    {
        v.temHash = true; v.hIn = hIn; v.hOut = hOut;
        v.saidaMudouEm = agora; v.entradaMudou = 0;
        return;
    }
    const bool mudouIn = hIn != v.hIn, mudouOut = hOut != v.hOut;
    v.hIn = hIn; v.hOut = hOut;
    /* P1: movimento da entrada, publicado so na transicao (1,5 s sem mudar = parado) */
    if (mudouIn || v.entradaMudouEm == 0) v.entradaMudouEm = agora;
    const bool parado = Ms(v.entradaMudouEm, agora) > 1500.0;
    if (parado != v.parado) { v.parado = parado; VigiaPublicar(v.direta, v.direta ? Ms(v.saidaMudouEm, agora) : 0.0); }
    if (mudouOut)
    {
        if (v.direta)
        {
            v.direta = false;
            Log("ponte: saida do app voltou a mudar - blit religado (passagem direta por %.0f ms, %llu quadros)",
                Ms(v.diretaDesde, agora), (unsigned long long)v.quadrosDiretos);
            VigiaPublicar(false, 0.0);
        }
        v.saidaMudouEm = agora; v.entradaMudou = 0;
        return;
    }
    if (!mudouIn)
    {
        /* entrada parada (pausa, menu estatico): saida parada e o esperado, nao conta */
        if (!v.direta) { v.saidaMudouEm = agora; v.entradaMudou = 0; }
        return;
    }
    ++v.entradaMudou;
    const double parada = Ms(v.saidaMudouEm, agora);
    if (!v.direta && v.entradaMudou >= 2 && parada > kVigiaParadaMs)
    {
        v.direta = true; v.diretaDesde = agora; v.quadrosDiretos = 0;
        Log("ponte: saida do app parada ha %.0f ms - passagem direta", parada);
        VigiaPublicar(true, parada);
    }
}

/* Solta os recursos da vigia e zera o estado (resize, rebuild, descarga). */
static void VigiaZerar()
{
    Vigia& v = P.vigia;
    Soltar(v.rbIn); Soltar(v.rbOut); Soltar(v.stIn); Soltar(v.stOut);
    const bool publicouParado = v.parado;
    v = Vigia();
    if (publicouParado) VigiaPublicar(false, 0.0);   /* P1: o app nao fica com um movendo=0 velho */
}

/* ================================================================== */
/* 4. Backend D3D11                                                    */
/* ================================================================== */

static ID3D11Texture2D* D11CriarCompartilhada(UINT w, UINT h, DXGI_FORMAT fmt, UINT bind, HANDLE* handleOut)
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = bind;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    ID3D11Texture2D* t = nullptr;
    HRESULT hr = P.d11.dev->CreateTexture2D(&td, nullptr, &t);
    if (FAILED(hr)) { Log("ponte: D3D11 CreateTexture2D %ux%u fmt=%u falhou 0x%08X", w, h, (unsigned)fmt, (unsigned)hr); return nullptr; }
    IDXGIResource1* r = nullptr;
    hr = t->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(&r));
    if (SUCCEEDED(hr))
    { hr = r->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, handleOut); r->Release(); }
    if (FAILED(hr)) { Log("ponte: D3D11 CreateSharedHandle falhou 0x%08X", (unsigned)hr); t->Release(); return nullptr; }
    return t;
}

static bool D11CompilarBlit()
{
    if (!CarregarCompilador()) return false;
    ID3DBlob* vs = nullptr, *ps = nullptr, *err = nullptr;
    HRESULT hr = g_D3DCompile(kBlitHlsl, sizeof(kBlitHlsl) - 1, "blit", nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vs, &err);
    if (FAILED(hr)) { Log("ponte: D3D11 blit VS 0x%08X: %s", (unsigned)hr, err ? (const char*)err->GetBufferPointer() : ""); Soltar(err); return false; }
    Soltar(err);
    hr = g_D3DCompile(kBlitHlsl, sizeof(kBlitHlsl) - 1, "blit", nullptr, nullptr, "ps", "ps_4_0", 0, 0, &ps, &err);
    if (FAILED(hr)) { Log("ponte: D3D11 blit PS 0x%08X: %s", (unsigned)hr, err ? (const char*)err->GetBufferPointer() : ""); Soltar(err); Soltar(vs); return false; }
    Soltar(err);
    hr = P.d11.dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &P.d11.vs);
    if (SUCCEEDED(hr)) hr = P.d11.dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &P.d11.ps);
    vs->Release(); ps->Release();
    if (FAILED(hr)) { Log("ponte: D3D11 Create*Shader 0x%08X", (unsigned)hr); return false; }

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(P.d11.dev->CreateSamplerState(&sd, &P.d11.amostrador))) return false;

    D3D11_BLEND_DESC bd = {};   /* opaco, sem blend */
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(P.d11.dev->CreateBlendState(&bd, &P.d11.blend))) return false;

    D3D11_DEPTH_STENCIL_DESC dd = {};   /* depth off */
    dd.DepthEnable = FALSE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(P.d11.dev->CreateDepthStencilState(&dd, &P.d11.depth))) return false;

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    if (FAILED(P.d11.dev->CreateRasterizerState(&rd, &P.d11.raster))) return false;
    return true;
}

/* Cria o conjunto D3D11 e monta o FeedBuild com os valores de handle locais.
   Chamada na thread da ponte (criacao de recurso e thread-safe em D3D11). */
static bool D11Build(FeedBuild* b)
{
    /* Color no formato tipado do backbuffer; Output no que o host devolve (RGBA8).
       Depth (R32_FLOAT) e MV (R16G16_FLOAT) como o cliente de referencia cria: o
       host de referencia abre os quatro (nosso app tolera 0, o do Feeder nao). */
    P.d11.color  = D11CriarCompartilhada(P.bbW, P.bbH, P.corFmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &P.d11.hColor);
    P.d11.output = D11CriarCompartilhada(P.bbW, P.bbH, P.saidaFmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, &P.d11.hOutput);
    P.d11.depthTex = D11CriarCompartilhada(P.bbW, P.bbH, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &P.d11.hDepth);
    P.d11.mvTex    = D11CriarCompartilhada(P.bbW, P.bbH, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &P.d11.hMv);
    if (P.d11.color == nullptr || P.d11.output == nullptr || P.d11.depthTex == nullptr || P.d11.mvTex == nullptr) return false;
    if (FAILED(P.d11.dev->CreateShaderResourceView(P.d11.output, nullptr, &P.d11.outputSrv))) { Log("ponte: D3D11 SRV do Output falhou"); return false; }
    if (!D11CompilarBlit()) return false;

    memset(b, 0, sizeof(*b));
    b->width = P.bbW; b->height = P.bbH;
    b->color_fmt = P.corFmt; b->output_fmt = P.saidaFmt;
    b->flags_override = -1;
    b->mv_scale_x = 1.0f; b->mv_scale_y = 1.0f;
    b->tex[SLOT_COLOR]  = ComoValor(P.d11.hColor);
    b->tex[SLOT_OUTPUT] = ComoValor(P.d11.hOutput);
    b->tex[SLOT_DEPTH]  = ComoValor(P.d11.hDepth);
    b->tex[SLOT_MV]     = ComoValor(P.d11.hMv);
    b->client_flags = P.asyncHome ? kBuildAsyncHome : 0;
    return true;
}

static bool D11AbrirFences()
{
    if (FAILED(P.d11.dev->QueryInterface(__uuidof(ID3D11Device5), reinterpret_cast<void**>(&P.d11.dev5))))
    { Log("ponte: D3D11 sem ID3D11Device5 (Windows 10 1703+ exigido)"); return false; }
    HRESULT h1 = P.d11.dev5->OpenSharedFence(ComoHandle(P.ackFenceIn),  __uuidof(ID3D11Fence), reinterpret_cast<void**>(&P.d11.fIn));
    HRESULT h2 = P.d11.dev5->OpenSharedFence(ComoHandle(P.ackFenceOut), __uuidof(ID3D11Fence), reinterpret_cast<void**>(&P.d11.fOut));
    if (FAILED(h1) || FAILED(h2)) { Log("ponte: D3D11 OpenSharedFence 0x%08X/0x%08X", (unsigned)h1, (unsigned)h2); return false; }
    return true;
}

/* Salva e restaura o pedaco do estado D3D11 que o blit toca (o jogo nao pode
   ver estado corrompido depois do Present). */
struct EstadoD11
{
    ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]; UINT nVp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr;
    ID3D11ShaderResourceView* srv0 = nullptr; ID3D11SamplerState* smp0 = nullptr;
    ID3D11InputLayout* il = nullptr; D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11BlendState* blend = nullptr; FLOAT blendF[4] = {}; UINT blendMask = 0;
    ID3D11DepthStencilState* dss = nullptr; UINT stencil = 0;
    ID3D11RasterizerState* rs = nullptr;

    void Salvar(ID3D11DeviceContext* c)
    {
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
        c->RSGetViewports(&nVp, vp);
        c->VSGetShader(&vs, nullptr, nullptr); c->PSGetShader(&ps, nullptr, nullptr);
        c->PSGetShaderResources(0, 1, &srv0); c->PSGetSamplers(0, 1, &smp0);
        c->IAGetInputLayout(&il); c->IAGetPrimitiveTopology(&topo);
        c->OMGetBlendState(&blend, blendF, &blendMask);
        c->OMGetDepthStencilState(&dss, &stencil);
        c->RSGetState(&rs);
    }
    void Restaurar(ID3D11DeviceContext* c)
    {
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);
        if (nVp > 0) c->RSSetViewports(nVp, vp);
        c->VSSetShader(vs, nullptr, 0); c->PSSetShader(ps, nullptr, 0);
        c->PSSetShaderResources(0, 1, &srv0); c->PSSetSamplers(0, 1, &smp0);
        c->IASetInputLayout(il); c->IASetPrimitiveTopology(topo);
        c->OMSetBlendState(blend, blendF, blendMask);
        c->OMSetDepthStencilState(dss, stencil);
        c->RSSetState(rs);
        for (auto& r : rtv) Soltar(r);
        Soltar(dsv); Soltar(vs); Soltar(ps); Soltar(srv0); Soltar(smp0);
        Soltar(il); Soltar(blend); Soltar(dss); Soltar(rs);
    }
};

/* R1: vigia em D3D11 - staging do tamanho do bloco, no formato de cada textura. */
static bool VigiaD11Criar()
{
    Vigia& v = P.vigia;
    if (v.stIn != nullptr && v.stOut != nullptr) return true;
    const auto criar = [](ID3D11Texture2D* base, ID3D11Texture2D** st) -> bool
    {
        D3D11_TEXTURE2D_DESC td = {};
        base->GetDesc(&td);
        td.Width = kVigiaLado; td.Height = kVigiaLado; td.MipLevels = 1; td.ArraySize = 1;
        td.SampleDesc.Count = 1; td.SampleDesc.Quality = 0;
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
        return SUCCEEDED(P.d11.dev->CreateTexture2D(&td, nullptr, st)) && *st != nullptr;
    };
    if (!criar(P.d11.color, &v.stIn) || !criar(P.d11.output, &v.stOut))
    {
        Soltar(v.stIn); Soltar(v.stOut); v.desligada = true;
        Log("ponte: vigia da saida indisponivel (staging D3D11 nao nasceu)");
        return false;
    }
    return true;
}

static void VigiaD11Gravar(ID3D11Texture2D* tex, ID3D11Texture2D* st)
{
    UINT x0 = 0, y0 = 0; VigiaCaixa(&x0, &y0);
    const D3D11_BOX caixa = { x0, y0, 0, x0 + kVigiaLado, y0 + kVigiaLado, 1 };
    P.d11.ctx->CopySubresourceRegion(st, 0, 0, 0, 0, tex, 0, &caixa);
}

/* Le a amostra pendente sem esperar a GPU (DO_NOT_WAIT): ainda desenhando = proximo quadro. */
static void VigiaD11Colher()
{
    Vigia& v = P.vigia;
    if (!v.pendente) return;
    ID3D11DeviceContext* c = P.d11.ctx;
    D3D11_MAPPED_SUBRESOURCE mi = {}, mo = {};
    HRESULT hr = c->Map(v.stIn, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mi);
    if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
    if (FAILED(hr)) { v.pendente = false; return; }
    hr = c->Map(v.stOut, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mo);
    if (FAILED(hr)) { c->Unmap(v.stIn, 0); if (hr != DXGI_ERROR_WAS_STILL_DRAWING) v.pendente = false; return; }
    const uint64_t hIn  = VigiaHash(static_cast<const uint8_t*>(mi.pData), mi.RowPitch, P.corFmt);
    const uint64_t hOut = VigiaHash(static_cast<const uint8_t*>(mo.pData), mo.RowPitch, P.saidaFmt);
    c->Unmap(v.stIn, 0); c->Unmap(v.stOut, 0);
    v.pendente = false;
    VigiaDecidir(hIn, hOut);
}

/* A VOLTA (Output -> backbuffer), D3D11. F0: quando o Output esta no formato do backbuffer
   (a sessao HDR negociou 16F/10 bits, ou o jogo e RGBA8) e um CopyResource - sem shader,
   sem RTV, sem tocar no estado do jogo. Senao, o pixel shader de conversao de sempre (o
   caso do host responder RGBA8 para um backbuffer BGRA8/16F/10 bits). Serve aos dois
   modos: pipelined (Output do quadro anterior) e sincrono (Output deste quadro). */
static void D11Voltar(ID3D11Texture2D* bb)
{
    ID3D11DeviceContext* c = P.d11.ctx;
    if (P.copiaDireta) { c->CopyResource(bb, P.d11.output); return; }
    EstadoD11 est; est.Salvar(c);
    ID3D11RenderTargetView* rtv = nullptr;
    if (SUCCEEDED(P.d11.dev->CreateRenderTargetView(bb, nullptr, &rtv)) && rtv != nullptr)
    {
        D3D11_VIEWPORT vp = { 0, 0, (FLOAT)P.bbW, (FLOAT)P.bbH, 0, 1 };
        ID3D11RenderTargetView* nada = nullptr;
        c->OMSetRenderTargets(1, &nada, nullptr);   /* solta o Output de qualquer RTV antes de o ligar como SRV */
        c->OMSetRenderTargets(1, &rtv, nullptr);
        c->RSSetViewports(1, &vp);
        c->VSSetShader(P.d11.vs, nullptr, 0); c->PSSetShader(P.d11.ps, nullptr, 0);
        c->PSSetShaderResources(0, 1, &P.d11.outputSrv);
        c->PSSetSamplers(0, 1, &P.d11.amostrador);
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const FLOAT bf[4] = { 0, 0, 0, 0 };
        c->OMSetBlendState(P.d11.blend, bf, 0xffffffff);
        c->OMSetDepthStencilState(P.d11.depth, 0);
        c->RSSetState(P.d11.raster);
        c->Draw(3, 0);
        rtv->Release();
    }
    est.Restaurar(c);
}

static void D11Quadro(IDXGISwapChain* sc, Tempos* t)
{
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bb))) || bb == nullptr)
    { Desligar("D3D11 GetBuffer(backbuffer) falhou"); return; }

    ID3D11DeviceContext4* c4 = P.d11.ctx4;
    ID3D11DeviceContext*  c  = P.d11.ctx;
    const uint64_t n = ++P.n;

    /* R1: vigia da saida - colhe a amostra anterior e decide se este quadro amostra. */
    VigiaD11Colher();
    const bool amostrar = VigiaDevida(n) && VigiaD11Criar();

    /* pipelined: espera de GPU pelo quadro anterior antes de tocar as entradas. */
    if (P.asyncHome && P.sentN != 0) { const int64_t a = Agora(); c4->Wait(P.d11.fOut, P.sentN); t->espera += Ms(a, Agora()); }

    /* 1. backbuffer -> Color (mesmo formato: CopyResource). Captura o quadro limpo. */
    { const int64_t a = Agora(); c->CopyResource(P.d11.color, bb); t->copia += Ms(a, Agora()); }
    if (amostrar) VigiaD11Gravar(P.d11.color, P.vigia.stIn);

    /* 1b. J5: depth e MV do upscaler (slots da sessao .up, escritos no dispatch deste quadro, no
       mesmo contexto imediato) -> slots Depth/MV da fase 2, na resolucao de render, canto superior
       esquerdo. Depois do Wait(out) acima e antes do Signal(in) abaixo, como a Color: o app le n
       enquanto o jogo ainda nao escreveu n+1. Duas copias de regiao, do mesmo custo da Color
       (0,04 ms medidos no F0). Em D3D11 a referencia nao precisa ser segurada: o runtime adia a
       destruicao ate a GPU terminar a copia. */
    Dados::Pacote pd; FeedDadosMsg dm = {};
    const bool temDados = PegarDadosDoQuadro(n, false, &pd, &dm);
    if (temDados)
    {
        const int64_t a = Agora();
        const D3D11_BOX bd = { 0, 0, 0, pd.renderW, pd.renderH, 1 };
        const D3D11_BOX bm = { 0, 0, 0, pd.mvW, pd.mvH, 1 };
        c->CopySubresourceRegion(P.d11.depthTex, 0, 0, 0, 0, pd.depth11, 0, &bd);
        c->CopySubresourceRegion(P.d11.mvTex, 0, 0, 0, 0, pd.mv11, 0, &bm);
        Soltar(pd.depth11); Soltar(pd.mv11);   /* a referencia do PegarDadosDoQuadro; a copia ja esta gravada */
        t->copia += Ms(a, Agora());
        ++P.janDados;
    }

    /* 2. volta do Output anterior ao backbuffer (pipelined), ANTES do Signal(in).
       F0: CopyResource quando o formato bate; senao o shader de conversao (D11Voltar).
       R1: com a saida parada (vigia), passagem direta: o backbuffer fica com o jogo. */
    const bool carrega = P.asyncHome && P.outValido;
    if (carrega)
    {
        const int64_t a = Agora();
        if (amostrar) VigiaD11Gravar(P.d11.output, P.vigia.stOut);
        if (!P.vigia.direta) D11Voltar(bb); else ++P.vigia.quadrosDiretos;
        t->blit += Ms(a, Agora());
    }
    else if (!P.asyncHome)
    {
        /* sincrono: Signal(in), 'F', Wait(out, n), blit(n) -- resolvido abaixo. */
    }

    /* 3. Signal(in_fence, n) + Flush, depois manda 'D' (J5, se ha dado real) e 'F'. */
    c4->Signal(P.d11.fIn, n);
    c->Flush();
    if (!MandarQuadro(n, temDados ? &dm : nullptr)) { bb->Release(); Desligar("pipe fechado"); return; }

    if (!P.asyncHome)
    {
        /* sincrono: espera o resultado deste quadro e o devolve ao backbuffer (D11Voltar). */
        const int64_t a = Agora();
        c4->Wait(P.d11.fOut, n);
        t->espera += Ms(a, Agora());
        const int64_t b = Agora();
        if (amostrar) VigiaD11Gravar(P.d11.output, P.vigia.stOut);
        if (!P.vigia.direta) D11Voltar(bb); else ++P.vigia.quadrosDiretos;
        t->blit += Ms(b, Agora());
    }

    if (amostrar) P.vigia.pendente = true;
    P.sentN = n; P.outValido = true;
    bb->Release();
}

static void D11Liberar()
{
    BackD11& d = P.d11;
    Soltar(d.outputSrv);
    Soltar(d.color); Soltar(d.output); Soltar(d.depthTex); Soltar(d.mvTex);
    FecharHandle(d.hColor); FecharHandle(d.hOutput); FecharHandle(d.hDepth); FecharHandle(d.hMv);
    Soltar(d.fIn); Soltar(d.fOut);
    Soltar(d.vs); Soltar(d.ps); Soltar(d.amostrador);
    Soltar(d.blend); Soltar(d.depth); Soltar(d.raster);
    Soltar(d.dev5); Soltar(d.ctx4);
    VigiaZerar();   /* R1 */
    /* dev/ctx nao sao nossos (do jogo): so soltamos as refs que pegamos. */
    Soltar(d.dev); Soltar(d.ctx);
}

/* ================================================================== */
/* 5. Backend D3D12                                                    */
/* ================================================================== */

static ID3D12Resource* D12CriarCompartilhada(UINT w, UINT h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, HANDLE* handleOut)
{
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = flags;
    ID3D12Resource* r = nullptr;
    /* O DEVICE LIDO UMA VEZ, para uma local (18/09/2026). Este era um dos dois enderecos das
       quedas do RE2: `dlss5-ponte64.dll+0x3696`, que desassemblado e exatamente o `P.d12.dev`
       sendo RELIDO do global para a segunda chamada e ja nulo - a primeira
       (CreateCommittedResource) tinha passado. Quem zera e o D12Liberar do DerrubarConjunto,
       na thread do JOGO, enquanto esta funcao roda na thread da ponte montando o Build.
       Ler uma vez nao conserta a corrida (isso e o conjunto com geracao, em separado), mas
       tira daqui a janela de uma linha para a outra e transforma "nulo no meio do caminho"
       numa saida com log. */
    ID3D12Device* const dev = P.d12.dev;
    if (dev == nullptr) { Log("ponte: D3D12 criar compartilhada %ux%u fmt=%u sem device (o conjunto caiu no meio do Build)", w, h, (unsigned)fmt); return nullptr; }
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd,
                    D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&r));
    if (FAILED(hr)) { Log("ponte: D3D12 CreateCommittedResource %ux%u fmt=%u falhou 0x%08X", w, h, (unsigned)fmt, (unsigned)hr); return nullptr; }
    hr = dev->CreateSharedHandle(r, nullptr, GENERIC_ALL, nullptr, handleOut);
    if (FAILED(hr)) { Log("ponte: D3D12 CreateSharedHandle falhou 0x%08X", (unsigned)hr); r->Release(); return nullptr; }
    return r;
}

static bool D12CriarBlit()
{
    if (!CarregarCompilador()) return false;
    /* device numa local, a mesma regra do D12CriarCompartilhada */
    ID3D12Device* const dev = P.d12.dev;
    if (dev == nullptr) { Log("ponte: D3D12 criar blit sem device (o conjunto caiu no meio do Build)"); return false; }
    ID3DBlob* vs = nullptr, *ps = nullptr, *rsBlob = nullptr, *err = nullptr;
    HRESULT hr = g_D3DCompile(kBlitHlslRS, sizeof(kBlitHlslRS) - 1, "blit", nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vs, &err);
    if (FAILED(hr)) { Log("ponte: D3D12 blit VS 0x%08X: %s", (unsigned)hr, err ? (const char*)err->GetBufferPointer() : ""); Soltar(err); return false; }
    Soltar(err);
    hr = g_D3DCompile(kBlitHlslRS, sizeof(kBlitHlslRS) - 1, "blit", nullptr, nullptr, "ps", "ps_5_0", 0, 0, &ps, &err);
    if (FAILED(hr)) { Log("ponte: D3D12 blit PS 0x%08X: %s", (unsigned)hr, err ? (const char*)err->GetBufferPointer() : ""); Soltar(err); Soltar(vs); return false; }
    Soltar(err);
    /* A root signature vem embutida no shader (D3DCompile a serializa no blob). */
    hr = D3DGetBlobPart(vs->GetBufferPointer(), vs->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &rsBlob);
    if (FAILED(hr)) { Log("ponte: D3D12 root sig do blob 0x%08X", (unsigned)hr); Soltar(vs); Soltar(ps); return false; }
    hr = dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), reinterpret_cast<void**>(&P.d12.rootSig));
    Soltar(rsBlob);
    if (FAILED(hr)) { Log("ponte: D3D12 CreateRootSignature 0x%08X", (unsigned)hr); Soltar(vs); Soltar(ps); return false; }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = P.d12.rootSig;
    pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE; pd.DepthStencilState.StencilEnable = FALSE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = FormatoTipado(P.bbFmt);
    pd.SampleDesc.Count = 1;
    hr = dev->CreateGraphicsPipelineState(&pd, __uuidof(ID3D12PipelineState), reinterpret_cast<void**>(&P.d12.pso));
    vs->Release(); ps->Release();
    if (FAILED(hr)) { Log("ponte: D3D12 CreateGraphicsPipelineState (rtv fmt %u) 0x%08X", (unsigned)FormatoTipado(P.bbFmt), (unsigned)hr); return false; }
    return true;
}

/* B14 (H6): o que referencia os backbuffers do swapchain - os RTVs e as listas/allocators
   que gravaram copias e blits neles. E so isto que um ResizeBuffers com o mesmo tamanho e
   formato precisa soltar; texturas compartilhadas, fences, PSO e SRV ficam. */
static bool D12CriarRefsBackbuffer(IDXGISwapChain* sc)
{
    BackD12& d = P.d12;
    for (UINT i = 0; i < d.nBuf; ++i)
    {
        if (FAILED(d.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&d.allocCopia[i]))) ||
            FAILED(d.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&d.allocBlit[i]))))
        { Log("ponte: D3D12 CreateCommandAllocator falhou"); return false; }
    }
    if (FAILED(d.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, d.allocCopia[0], nullptr, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&d.listaCopia))) ||
        FAILED(d.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, d.allocBlit[0], nullptr, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&d.listaBlit))))
    { Log("ponte: D3D12 CreateCommandList falhou"); return false; }
    d.listaCopia->Close(); d.listaBlit->Close();

    /* heap e RTV por buffer */
    D3D12_DESCRIPTOR_HEAP_DESC rh = {}; rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rh.NumDescriptors = d.nBuf;
    if (FAILED(d.dev->CreateDescriptorHeap(&rh, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void**>(&d.rtvHeap)))) { Log("ponte: D3D12 rtvHeap falhou"); return false; }
    d.rtvStride = d.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = d.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < d.nBuf; ++i)
    {
        ID3D12Resource* buf = nullptr;
        if (SUCCEEDED(sc->GetBuffer(i, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&buf))) && buf != nullptr)
        {
            D3D12_RENDER_TARGET_VIEW_DESC rvd = {}; rvd.Format = FormatoTipado(P.bbFmt); rvd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            d.dev->CreateRenderTargetView(buf, &rvd, rtv);
            buf->Release();
        }
        rtv.ptr += d.rtvStride;
    }
    return true;
}

/* B14: fenceDoBuf zera junto com os allocators. Antes ficava com os valores da fence de
   reciclagem ANTERIOR, e o primeiro quadro de cada buffer depois de um rebuild esperava 1 s
   (D12EsperarAlloc) por um valor que a fence nova so alcanca depois dessa espera. */
static void D12SoltarRefsBackbuffer()
{
    BackD12& d = P.d12;
    Soltar(d.rtvHeap);
    Soltar(d.listaCopia); Soltar(d.listaBlit);
    for (UINT i = 0; i < kMaxBuf; ++i) { Soltar(d.allocCopia[i]); Soltar(d.allocBlit[i]); d.fenceDoBuf[i] = 0; }
}

/* A nossa GPU terminou tudo o que foi submetido? Espera ate 1 s, como o D3D12 Liberar. */
static bool D12GpuEmDia()
{
    BackD12& d = P.d12;
    if (d.fRecicla == nullptr || d.reciclaVal == 0 || d.fRecicla->GetCompletedValue() >= d.reciclaVal) return true;
    if (d.evRecicla != nullptr && SUCCEEDED(d.fRecicla->SetEventOnCompletion(d.reciclaVal, d.evRecicla)))
        WaitForSingleObject(d.evRecicla, 1000);
    return d.fRecicla->GetCompletedValue() >= d.reciclaVal;
}

static bool D12Build(FeedBuild* b, IDXGISwapChain* sc)
{
    BackD12& d = P.d12;
    /* nBuf do swapchain */
    DXGI_SWAP_CHAIN_DESC scd = {};
    sc->GetDesc(&scd);
    d.nBuf = scd.BufferCount; if (d.nBuf == 0 || d.nBuf > kMaxBuf) d.nBuf = 2;

    /* C15: a fila em que o swapchain apresenta (como o FG, C11). So troca a reserva ja vista:
       com d.fila nula o ExecuteCommandLists ainda pode escrever nela. */
    if (d.fila != nullptr)
    {
        static bool avisouReserva = false;
        if (ID3D12CommandQueue* q = Fg::FilaDoSwapchain(sc))
        {
            if (q != d.fila) { Log("ponte: fila do jogo pelo swapchain=0x%p (a primeira DIRECT vista era 0x%p)", (void*)q, (void*)d.fila); Soltar(d.fila); d.fila = q; }
            else { Log("ponte: fila do jogo pelo swapchain=0x%p (igual a vista)", (void*)q); q->Release(); }
        }
        else if (!avisouReserva) { avisouReserva = true; Log("ponte: fila do swapchain nao encontrada; usando a primeira fila DIRECT vista (reserva)"); }
    }

    /* FLAGS QUE O D3D11 DO APP EXIGE (B5, fumaca 1 de 15/09): o nosso host abre estas
       texturas com ID3D11Device1::OpenSharedResource1, e o runtime D3D11 recusa com
       E_INVALIDARG (0x80070057) uma textura D3D12 criada sem ALLOW_RENDER_TARGET; os
       formatos R32_FLOAT e R16G16_FLOAT exigem ainda ALLOW_SIMULTANEOUS_ACCESS (sonda
       isolada na RTX 4080: NONE, UAV e SIM sozinhos falham nos seis formatos; RT passa nos
       de cor; RT|SIM passa em todos, com ou sem UAV). O host de referencia do Feeder abre
       em D3D12 (OpenSharedHandle) e por isso nunca reclamou. As barreiras COMMON->X->COMMON
       de cada lista continuam validas com SIMULTANEOUS_ACCESS. */
    const D3D12_RESOURCE_FLAGS kAbrivelNoD3D11 = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    d.color  = D12CriarCompartilhada(P.bbW, P.bbH, P.corFmt, kAbrivelNoD3D11, &d.hColor);
    d.output = D12CriarCompartilhada(P.bbW, P.bbH, P.saidaFmt, kAbrivelNoD3D11 | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &d.hOutput);
    d.depth  = D12CriarCompartilhada(P.bbW, P.bbH, DXGI_FORMAT_R32_FLOAT, kAbrivelNoD3D11, &d.hDepth);
    d.mv     = D12CriarCompartilhada(P.bbW, P.bbH, DXGI_FORMAT_R16G16_FLOAT, kAbrivelNoD3D11, &d.hMv);
    if (d.color == nullptr || d.output == nullptr || d.depth == nullptr || d.mv == nullptr) return false;

    /* allocators + listas + RTVs dos backbuffers (B14: numa funcao, o resize mantido refaz so isto) */
    if (!D12CriarRefsBackbuffer(sc)) return false;
    if (FAILED(d.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&d.fRecicla))))
    { Log("ponte: D3D12 fence de reciclagem falhou"); return false; }
    d.evRecicla = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    /* heap do SRV do Output */
    D3D12_DESCRIPTOR_HEAP_DESC sh = {}; sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; sh.NumDescriptors = 1; sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(d.dev->CreateDescriptorHeap(&sh, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void**>(&d.srvHeap)))) { Log("ponte: D3D12 srvHeap falhou"); return false; }

    /* SRV do Output */
    D3D12_SHADER_RESOURCE_VIEW_DESC svd = {};
    svd.Format = P.saidaFmt; svd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    svd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; svd.Texture2D.MipLevels = 1;
    d.dev->CreateShaderResourceView(d.output, &svd, d.srvHeap->GetCPUDescriptorHandleForHeapStart());

    if (!D12CriarBlit()) return false;

    memset(b, 0, sizeof(*b));
    b->width = P.bbW; b->height = P.bbH;
    b->color_fmt = P.corFmt; b->output_fmt = P.saidaFmt;
    b->flags_override = -1;
    b->mv_scale_x = 1.0f; b->mv_scale_y = 1.0f;
    b->tex[SLOT_COLOR]  = ComoValor(d.hColor);
    b->tex[SLOT_OUTPUT] = ComoValor(d.hOutput);
    b->tex[SLOT_DEPTH]  = ComoValor(d.hDepth);
    b->tex[SLOT_MV]     = ComoValor(d.hMv);
    b->client_flags = P.asyncHome ? kBuildAsyncHome : 0;
    return true;
}

static bool D12AbrirFences()
{
    /* device numa local, a mesma regra do D12CriarCompartilhada */
    ID3D12Device* const dev = P.d12.dev;
    if (dev == nullptr) { Log("ponte: D3D12 abrir fences sem device (o conjunto caiu no meio do Build)"); return false; }
    HRESULT h1 = dev->OpenSharedHandle(ComoHandle(P.ackFenceIn),  __uuidof(ID3D12Fence), reinterpret_cast<void**>(&P.d12.fIn));
    HRESULT h2 = dev->OpenSharedHandle(ComoHandle(P.ackFenceOut), __uuidof(ID3D12Fence), reinterpret_cast<void**>(&P.d12.fOut));
    if (FAILED(h1) || FAILED(h2)) { Log("ponte: D3D12 OpenSharedHandle(fence) 0x%08X/0x%08X", (unsigned)h1, (unsigned)h2); return false; }
    return true;
}

static D3D12_RESOURCE_BARRIER Barr(ID3D12Resource* r, D3D12_RESOURCE_STATES de, D3D12_RESOURCE_STATES para)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = de; b.Transition.StateAfter = para;
    return b;
}

/* R1: vigia em D3D12 - um buffer de leitura por bloco, com o layout que o proprio runtime
   calcula para o formato (a licao do F0: pitch fixo derrubou o device em 16F). */
static bool VigiaD12Criar()
{
    Vigia& v = P.vigia;
    if (v.rbIn != nullptr && v.rbOut != nullptr) return true;
    /* device numa local, a mesma regra do D12CriarCompartilhada */
    ID3D12Device* const dev = P.d12.dev;
    if (dev == nullptr) return false;
    const auto criar = [dev](DXGI_FORMAT fmt, ID3D12Resource** rb, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* fp, UINT64* bytes) -> bool
    {
        D3D12_RESOURCE_DESC td = {};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = kVigiaLado; td.Height = kVigiaLado; td.DepthOrArraySize = 1; td.MipLevels = 1;
        td.Format = FormatoTipado(fmt); td.SampleDesc.Count = 1; td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        *bytes = 0;
        dev->GetCopyableFootprints(&td, 0, 1, 0, fp, nullptr, nullptr, bytes);
        if (*bytes == 0 || *bytes == UINT64_MAX) return false;
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = *bytes; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                           nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(rb))) && *rb != nullptr;
    };
    if (!criar(P.corFmt, &v.rbIn, &v.fpIn, &v.bytesIn) || !criar(P.saidaFmt, &v.rbOut, &v.fpOut, &v.bytesOut))
    {
        Soltar(v.rbIn); Soltar(v.rbOut); v.desligada = true;
        Log("ponte: vigia da saida indisponivel (buffer de leitura D3D12 nao nasceu)");
        return false;
    }
    return true;
}

/* Grava na lista a copia do bloco central de `tex` (em COMMON) para o buffer de leitura. */
static void VigiaD12Gravar(ID3D12GraphicsCommandList* l, ID3D12Resource* tex, ID3D12Resource* rb,
                           const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp)
{
    UINT x0 = 0, y0 = 0; VigiaCaixa(&x0, &y0);
    const D3D12_BOX caixa = { x0, y0, 0, x0 + kVigiaLado, y0 + kVigiaLado, 1 };
    D3D12_RESOURCE_BARRIER b = Barr(tex, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    l->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = tex; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
    l->CopyTextureRegion(&dst, 0, 0, 0, &src, &caixa);
    b = Barr(tex, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    l->ResourceBarrier(1, &b);
}

/* J5: grava na lista as duas copias de regiao (slot da .up -> slot da fase 2), depth em
   renderW x renderH e MV em mvW x mvH, canto superior esquerdo. Os slots da .up sao devolvidos
   a COMMON pelo sr/upstream (mesma fila, conferida no PacoteCabe) e nascem com
   ALLOW_SIMULTANEOUS_ACCESS; os nossos vivem em COMMON entre quadros (o app os le por D3D11).
   Barreiras explicitas de ida e volta, no padrao da vigia logo acima. */
static void D12GravarDados(ID3D12GraphicsCommandList* l, const Dados::Pacote& p)
{
    BackD12& d = P.d12;
    D3D12_RESOURCE_BARRIER b1[4] = { Barr(p.depth12, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                     Barr(p.mv12,    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                     Barr(d.depth,   D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
                                     Barr(d.mv,      D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST) };
    l->ResourceBarrier(4, b1);
    const auto copiar = [l](ID3D12Resource* dst, ID3D12Resource* src, UINT w, UINT h)
    {
        D3D12_TEXTURE_COPY_LOCATION s = {}; s.pResource = src; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION t = {}; t.pResource = dst; t.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; t.SubresourceIndex = 0;
        const D3D12_BOX caixa = { 0, 0, 0, w, h, 1 };
        l->CopyTextureRegion(&t, 0, 0, 0, &s, &caixa);
    };
    copiar(d.depth, p.depth12, p.renderW, p.renderH);
    copiar(d.mv, p.mv12, p.mvW, p.mvH);
    D3D12_RESOURCE_BARRIER b2[4] = { Barr(p.depth12, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
                                     Barr(p.mv12,    D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
                                     Barr(d.depth,   D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
                                     Barr(d.mv,      D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON) };
    l->ResourceBarrier(4, b2);
}

/* Le a amostra pendente so quando a fence de reciclagem ja passou dela (nunca espera). */
static void VigiaD12Colher()
{
    Vigia& v = P.vigia;
    if (!v.pendente || P.d12.fRecicla == nullptr || P.d12.fRecicla->GetCompletedValue() < v.fencePend) return;
    v.pendente = false;
    void* pi = nullptr, *po = nullptr;
    const D3D12_RANGE ri = { 0, (SIZE_T)v.bytesIn }, ro = { 0, (SIZE_T)v.bytesOut }, nada = { 0, 0 };
    if (FAILED(v.rbIn->Map(0, &ri, &pi)) || pi == nullptr) return;
    if (FAILED(v.rbOut->Map(0, &ro, &po)) || po == nullptr) { v.rbIn->Unmap(0, &nada); return; }
    const uint64_t hIn  = VigiaHash(static_cast<const uint8_t*>(pi) + v.fpIn.Offset,  v.fpIn.Footprint.RowPitch,  P.corFmt);
    const uint64_t hOut = VigiaHash(static_cast<const uint8_t*>(po) + v.fpOut.Offset, v.fpOut.Footprint.RowPitch, P.saidaFmt);
    v.rbIn->Unmap(0, &nada); v.rbOut->Unmap(0, &nada);
    VigiaDecidir(hIn, hOut);
}

static void D12EsperarAlloc(UINT idx)
{
    BackD12& d = P.d12;
    const uint64_t alvo = d.fenceDoBuf[idx];
    if (alvo == 0) return;
    if (d.fRecicla->GetCompletedValue() >= alvo) return;
    if (SUCCEEDED(d.fRecicla->SetEventOnCompletion(alvo, d.evRecicla)))
        WaitForSingleObject(d.evRecicla, 1000);   /* limite: nunca trava a CPU alem de ~1 quadro */
}

/* A VOLTA (Output -> backbuffer), D3D12: grava e submete uma lista na fila do jogo.
   F0: CopyResource quando o Output esta no formato do backbuffer (sessao HDR em 16F/10
   bits, ou jogo RGBA8): estados PRESENT->COPY_DEST no backbuffer e COMMON->COPY_SOURCE
   no Output, e de volta. Senao o pixel shader de conversao (PSO/RTV/SRV de sempre).
   Serve aos dois modos: pipelined (Output do quadro anterior) e sincrono (deste).
   R1: `blit` false = passagem direta da vigia; `amostrar` grava o bloco do Output que
   a vigia le (na mesma lista, depois do Wait(out) que a precede na fila).
   J5: `dados` = as copias de depth/MV para os slots da fase 2, NESTA lista e nao na de
   copia da Color, porque so esta corre depois do Wait(out, anterior) no modo pipelined: a
   lista de copia e submetida antes dele e escreveria n+1 nos slots enquanto o app ainda le
   n (o app copia depth/MV no fim do quadro dele, logo antes de devolver). */
static void D12Voltar(ID3D12Resource* bb, UINT idx, bool blit, bool amostrar, const Dados::Pacote* dados)
{
    BackD12& d = P.d12;
    if (!blit && !amostrar && dados == nullptr) return;
    d.allocBlit[idx]->Reset();
    d.listaBlit->Reset(d.allocBlit[idx], (blit && !P.copiaDireta) ? d.pso : nullptr);
    if (dados != nullptr) D12GravarDados(d.listaBlit, *dados);
    if (amostrar) VigiaD12Gravar(d.listaBlit, d.output, P.vigia.rbOut, P.vigia.fpOut);
    if (!blit)
    {
        /* so a amostra */
    }
    else if (P.copiaDireta)
    {
        D3D12_RESOURCE_BARRIER b1[2] = { Barr(d.output, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                         Barr(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST) };
        d.listaBlit->ResourceBarrier(2, b1);
        d.listaBlit->CopyResource(bb, d.output);
        D3D12_RESOURCE_BARRIER b2[2] = { Barr(bb, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT),
                                         Barr(d.output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON) };
        d.listaBlit->ResourceBarrier(2, b2);
    }
    else
    {
        D3D12_RESOURCE_BARRIER b1[2] = { Barr(d.output, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                                         Barr(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET) };
        d.listaBlit->ResourceBarrier(2, b1);
        d.listaBlit->SetGraphicsRootSignature(d.rootSig);
        ID3D12DescriptorHeap* heaps[1] = { d.srvHeap };
        d.listaBlit->SetDescriptorHeaps(1, heaps);
        d.listaBlit->SetPipelineState(d.pso);
        d.listaBlit->SetGraphicsRootDescriptorTable(0, d.srvHeap->GetGPUDescriptorHandleForHeapStart());
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = d.rtvHeap->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += (SIZE_T)idx * d.rtvStride;
        d.listaBlit->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        D3D12_VIEWPORT vp = { 0, 0, (FLOAT)P.bbW, (FLOAT)P.bbH, 0, 1 };
        D3D12_RECT sciss = { 0, 0, (LONG)P.bbW, (LONG)P.bbH };
        d.listaBlit->RSSetViewports(1, &vp); d.listaBlit->RSSetScissorRects(1, &sciss);
        d.listaBlit->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        d.listaBlit->DrawInstanced(3, 1, 0, 0);
        D3D12_RESOURCE_BARRIER b2[2] = { Barr(bb, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT),
                                         Barr(d.output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON) };
        d.listaBlit->ResourceBarrier(2, b2);
    }
    d.listaBlit->Close();
    ID3D12CommandList* lb[1] = { d.listaBlit };
    d.fila->ExecuteCommandLists(1, lb);
}

static void D12Quadro(IDXGISwapChain* sc, Tempos* t)
{
    BackD12& d = P.d12;
    if (d.fila == nullptr)
    {
        if (!P.avisouSemFila) { P.avisouSemFila = true; Log("ponte: D3D12 sem a fila do jogo ainda; passando direto ate ExecuteCommandLists aparecer"); }
        return;
    }

    IDXGISwapChain3* sc3 = nullptr;
    UINT idx = 0;
    if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3))) && sc3 != nullptr)
    { idx = sc3->GetCurrentBackBufferIndex(); sc3->Release(); }
    if (idx >= d.nBuf) idx = 0;

    ID3D12Resource* bb = nullptr;
    if (FAILED(sc->GetBuffer(idx, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&bb))) || bb == nullptr)
    { Desligar("D3D12 GetBuffer(backbuffer) falhou"); return; }

    const uint64_t n = ++P.n;
    D12EsperarAlloc(idx);
    /* J5: a GPU terminou a ultima lista deste buffer (a fence acima), entao as texturas da .up
       que ela leu podem ser soltas - na mesma thread que as tomou, com nada em voo apontando
       para elas (o par do X-FG-3 §1.3). */
    Soltar(d.srcDepth[idx]); Soltar(d.srcMv[idx]);

    /* R1: vigia da saida - colhe a amostra anterior e decide se este quadro amostra. */
    VigiaD12Colher();
    const bool amostrar = VigiaDevida(n) && VigiaD12Criar();
    const bool sincrono = !P.asyncHome;

    /* J5: o pacote de depth/MV deste quadro (ou nada: fluxo optico no app). As referencias
       ficam com o buffer `idx` ate a proxima passagem por ele. */
    Dados::Pacote pd; FeedDadosMsg dm = {};
    const bool temDados = PegarDadosDoQuadro(n, true, &pd, &dm);
    if (temDados) { d.srcDepth[idx] = pd.depth12; d.srcMv[idx] = pd.mv12; ++P.janDados; }

    /* --- lista de copia: backbuffer -> Color (mesmo formato) --- */
    const int64_t tc = Agora();
    d.allocCopia[idx]->Reset();
    d.listaCopia->Reset(d.allocCopia[idx], nullptr);
    D3D12_RESOURCE_BARRIER e1[2] = { Barr(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                     Barr(d.color, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST) };
    d.listaCopia->ResourceBarrier(2, e1);
    d.listaCopia->CopyResource(d.color, bb);
    D3D12_RESOURCE_BARRIER e2[2] = { Barr(d.color, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON),
                                     Barr(bb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT) };
    d.listaCopia->ResourceBarrier(2, e2);
    if (amostrar) VigiaD12Gravar(d.listaCopia, d.color, P.vigia.rbIn, P.vigia.fpIn);
    /* J5, sincrono: depth/MV aqui mesmo, antes do Signal(in) (o jogo espera out n antes de n+1:
       nao ha corrida com a leitura do app). No pipelined vao no D12Voltar, depois do Wait(out). */
    if (temDados && sincrono) D12GravarDados(d.listaCopia, pd);
    d.listaCopia->Close();
    ID3D12CommandList* lc[1] = { d.listaCopia };
    d.fila->ExecuteCommandLists(1, lc);
    t->copia += Ms(tc, Agora());

    if (P.asyncHome)
    {
        /* espera de GPU pelo anterior, blit do Output anterior (e depth/MV deste, J5), DEPOIS Signal(in,n). */
        if (P.sentN != 0) { const int64_t a = Agora(); d.fila->Wait(d.fOut, P.sentN); t->espera += Ms(a, Agora()); }
        if (P.outValido || temDados)
        {
            const int64_t a = Agora();
            D12Voltar(bb, idx, P.outValido && !P.vigia.direta, amostrar, temDados ? &pd : nullptr);   /* F0: CopyResource ou shader; R1: vigia; J5: dados */
            if (P.outValido && P.vigia.direta) ++P.vigia.quadrosDiretos;
            t->blit += Ms(a, Agora());
        }
        d.fila->Signal(d.fIn, n);
    }

    /* manda 'D' (J5, se ha dado real) e 'F' */
    if (!MandarQuadro(n, temDados ? &dm : nullptr)) { bb->Release(); Desligar("pipe fechado"); return; }

    if (sincrono)
    {
        d.fila->Signal(d.fIn, n);
        const int64_t a = Agora();
        d.fila->Wait(d.fOut, n);
        t->espera += Ms(a, Agora());
        const int64_t b = Agora();
        D12Voltar(bb, idx, !P.vigia.direta, amostrar, nullptr);   /* F0: CopyResource ou shader; R1: vigia */
        if (P.vigia.direta) ++P.vigia.quadrosDiretos;
        t->blit += Ms(b, Agora());
    }

    /* reciclar o allocator deste buffer */
    d.fila->Signal(d.fRecicla, ++d.reciclaVal);
    d.fenceDoBuf[idx] = d.reciclaVal;
    if (amostrar) { P.vigia.fencePend = d.reciclaVal; P.vigia.pendente = true; }   /* R1 */

    P.sentN = n; P.outValido = true;
    bb->Release();
}

static void D12Liberar()
{
    BackD12& d = P.d12;
    /* deixa a nossa GPU terminar antes de soltar o que ela usa */
    bool emDia = true;
    if (d.fila != nullptr && d.fRecicla != nullptr && d.reciclaVal > 0)
    {
        if (d.fRecicla->GetCompletedValue() < d.reciclaVal && d.evRecicla != nullptr &&
            SUCCEEDED(d.fRecicla->SetEventOnCompletion(d.reciclaVal, d.evRecicla)))
            WaitForSingleObject(d.evRecicla, 1000);
        emDia = d.fRecicla->GetCompletedValue() >= d.reciclaVal;
    }
    Soltar(d.pso); Soltar(d.rootSig); Soltar(d.srvHeap);
    D12SoltarRefsBackbuffer();   /* rtvHeap, listas, allocators; B14: e zera fenceDoBuf */
    /* J5: as texturas da .up lidas pelas listas, so com a GPU em dia; se a espera bateu no teto,
       vazam de proposito (soltar recurso com a GPU lendo e uso-apos-liberar, a regra do C4). */
    for (UINT i = 0; i < kMaxBuf; ++i)
    {
        if (emDia) { Soltar(d.srcDepth[i]); Soltar(d.srcMv[i]); }
        else if (d.srcDepth[i] != nullptr || d.srcMv[i] != nullptr) { d.srcDepth[i] = nullptr; d.srcMv[i] = nullptr; Log("ponte: GPU nao terminou em 1 s; as referencias de depth/mv do buffer %u ficam vazadas de proposito", i); }
    }
    Soltar(d.fRecicla); FecharHandle(d.evRecicla);
    Soltar(d.color); Soltar(d.output); Soltar(d.depth); Soltar(d.mv);
    FecharHandle(d.hColor); FecharHandle(d.hOutput); FecharHandle(d.hDepth); FecharHandle(d.hMv);
    Soltar(d.fIn); Soltar(d.fOut);
    VigiaZerar();   /* R1 */
    Soltar(d.fila);
    Soltar(d.dev);
}

/* ================================================================== */
/* 6. Descoberta de vtables e hooks                                    */
/* ================================================================== */

/* declaracoes dos hooks */
static HRESULT STDMETHODCALLTYPE HkPresent(IDXGISwapChain*, UINT, UINT);
static HRESULT STDMETHODCALLTYPE HkPresent1(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
static HRESULT STDMETHODCALLTYPE HkResizeBuffers(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
static HRESULT STDMETHODCALLTYPE HkResizeTarget(IDXGISwapChain*, const DXGI_MODE_DESC*);
static void    STDMETHODCALLTYPE HkExecuteCommandLists(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
static HRESULT STDMETHODCALLTYPE HkGetBuffer(IDXGISwapChain*, UINT, REFIID, void**);                 /* Q4 */
static UINT    STDMETHODCALLTYPE HkGetCurrentBackBufferIndex(IDXGISwapChain3*);                       /* Q4 */
static HRESULT STDMETHODCALLTYPE HkSetFullscreenState(IDXGISwapChain*, BOOL, IDXGIOutput*);            /* C9 */
static HRESULT STDMETHODCALLTYPE HkResizeBuffers1(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);   /* B19 */

typedef HRESULT (STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE* Present1Fn)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef HRESULT (STDMETHODCALLTYPE* ResizeBuffersFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
typedef HRESULT (STDMETHODCALLTYPE* ResizeTargetFn)(IDXGISwapChain*, const DXGI_MODE_DESC*);
typedef void    (STDMETHODCALLTYPE* ECLFn)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
typedef HRESULT (STDMETHODCALLTYPE* GetBufferFn)(IDXGISwapChain*, UINT, REFIID, void**);
typedef UINT    (STDMETHODCALLTYPE* GetCurBBFn)(IDXGISwapChain3*);
typedef HRESULT (STDMETHODCALLTYPE* SetFsFn)(IDXGISwapChain*, BOOL, IDXGIOutput*);
typedef HRESULT (STDMETHODCALLTYPE* ResizeBuffers1Fn)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);

/* Q4: slots de vtable dos hooks de FG (a IDXGISwapChain de qualquer jogo flip-model
   e uma IDXGISwapChain4 concreta, entao os dois indices existem).
   B19: ResizeBuffers1 e o 39 (IDXGISwapChain3: 36 GetCurrentBackBufferIndex, 37
   CheckColorSpaceSupport, 38 SetColorSpace1, 39 ResizeBuffers1; conferido no dxgi1_4.h). */
enum { VT_GETBUFFER = 9, VT_SETFULLSCREENSTATE = 10, VT_GETCURBACKBUFFERINDEX = 36, VT_RESIZEBUFFERS1 = 39 };

static LRESULT CALLBACK DummyProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

/* B19: so remenda o slot 39 numa vtable que o tem (DXGI 1.4 ou mais novo) */
static bool TemSwapChain3(IDXGISwapChain1* sc)
{
    IDXGISwapChain3* sc3 = nullptr;
    if (FAILED(sc->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&sc3))) || sc3 == nullptr) return false;
    const bool mesma = VtableDe(sc3) == VtableDe(sc);
    Soltar(sc3);
    return mesma;
}

/* Cria device+swapchain descartaveis em D3D11 e em D3D12, le as vtables do
   swapchain e da fila, e as guarda. Janela oculta propria. */
static void DescobrirVtables()
{
    WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DummyProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"PonteDummyWnd";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 32, 32, nullptr, nullptr, wc.hInstance, nullptr);

    /* --- D3D11, swapchain FLIP-model via CreateSwapChainForHwnd: e a vtable
       que os jogos de hoje (e o alvo do B3, e o RE2) usam; a antiga
       D3D11CreateDeviceAndSwapChain com SWAP_EFFECT_DISCARD tem OUTRA vtable e
       nao pegaria o Present do jogo. --- */
    {
        ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
        D3D_FEATURE_LEVEL fl;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                        D3D11_SDK_VERSION, &dev, &fl, &ctx);
        if (SUCCEEDED(hr) && dev != nullptr)
        {
            IDXGIFactory2* fac = nullptr;
            if (SUCCEEDED(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), reinterpret_cast<void**>(&fac))) && fac != nullptr)
            {
                DXGI_SWAP_CHAIN_DESC1 sd = {};
                sd.Width = 32; sd.Height = 32; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                IDXGISwapChain1* sc1 = nullptr;
                HRESULT h2 = fac->CreateSwapChainForHwnd(dev, hwnd, &sd, nullptr, nullptr, &sc1);
                if (SUCCEEDED(h2) && sc1 != nullptr) { P.vtSwap = VtableDe(sc1); P.sc3Swap = TemSwapChain3(sc1); Soltar(sc1); }
                else Log("ponte: D3D11 CreateSwapChainForHwnd descartavel falhou 0x%08X", (unsigned)h2);
                Soltar(fac);
            }
            Soltar(ctx); Soltar(dev);
        }
        else Log("ponte: D3D11 descartavel falhou 0x%08X (o jogo pode ser so D3D12)", (unsigned)hr);
        Log("ponte: vtable swapchain D3D11(flip)=%p", (void*)P.vtSwap);
    }

    /* --- D3D12 --- */
    {
        ID3D12Device* dev12 = nullptr;
        HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&dev12));
        if (SUCCEEDED(hr) && dev12 != nullptr)
        {
            ID3D12CommandQueue* fila = nullptr;
            D3D12_COMMAND_QUEUE_DESC qd = {}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            if (SUCCEEDED(dev12->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&fila))) && fila != nullptr)
            {
                P.vtFila = VtableDe(fila);
                IDXGIFactory2* fac = nullptr;
                if (SUCCEEDED(CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), reinterpret_cast<void**>(&fac))) && fac != nullptr)
                {
                    DXGI_SWAP_CHAIN_DESC1 sd = {};
                    sd.Width = 32; sd.Height = 32; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                    sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                    IDXGISwapChain1* sc1 = nullptr;
                    if (SUCCEEDED(fac->CreateSwapChainForHwnd(fila, hwnd, &sd, nullptr, nullptr, &sc1)) && sc1 != nullptr)
                    {
                        void** vt = VtableDe(sc1);
                        Log("ponte: vtable swapchain D3D12(flip)=%p", (void*)vt);
                        if (P.vtSwap == nullptr) { P.vtSwap = vt; P.sc3Swap = TemSwapChain3(sc1); }
                        else if (vt != P.vtSwap) { P.vtSwap2 = vt; P.sc3Swap2 = TemSwapChain3(sc1); }   /* vtable distinta: engancha as duas */
                        Soltar(sc1);
                    }
                    Soltar(fac);
                }
                Soltar(fila);
            }
            Soltar(dev12);
        }
        else Log("ponte: D3D12 descartavel falhou 0x%08X (o jogo pode ser so D3D11)", (unsigned)hr);
    }

    if (hwnd) DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

/* A11: `alt` = onde guardar os originais DESTA vtable; nullptr = os campos da primeira. */
static void PatchSwapchain(void** vt, bool sc3, Ponte_::OrigsSwap* alt)
{
    if (vt == nullptr) return;
    void* o;
    auto guardar = [&o](void*& destino) { if (destino == nullptr) destino = o; };
    if (sc3 && PatchSlot(vt, VT_RESIZEBUFFERS1, (void*)&HkResizeBuffers1, &o)) guardar(alt ? alt->resizeBuffers1 : P.origResizeBuffers1);   /* B19 */
    if (PatchSlot(vt, VT_PRESENT, (void*)&HkPresent, &o)) guardar(alt ? alt->present : P.origPresent);
    if (PatchSlot(vt, VT_PRESENT1, (void*)&HkPresent1, &o)) guardar(alt ? alt->present1 : P.origPresent1);
    if (PatchSlot(vt, VT_RESIZEBUFFERS, (void*)&HkResizeBuffers, &o)) guardar(alt ? alt->resizeBuffers : P.origResizeBuffers);
    if (PatchSlot(vt, VT_RESIZETARGET, (void*)&HkResizeTarget, &o)) guardar(alt ? alt->resizeTarget : P.origResizeTarget);
    /* Q4: GetBuffer e GetCurrentBackBufferIndex, para o FG entregar proxies ao jogo */
    if (PatchSlot(vt, VT_GETBUFFER, (void*)&HkGetBuffer, &o)) guardar(alt ? alt->getBuffer : P.origGetBuffer);
    if (PatchSlot(vt, VT_GETCURBACKBUFFERINDEX, (void*)&HkGetCurrentBackBufferIndex, &o)) guardar(alt ? alt->getCurBB : P.origGetCurBB);
    if (PatchSlot(vt, VT_SETFULLSCREENSTATE, (void*)&HkSetFullscreenState, &o)) guardar(alt ? alt->setFs : P.origSetFs);   /* C9 */
}

static void InstalarHooks()
{
    PatchSwapchain(P.vtSwap, P.sc3Swap, nullptr);
    PatchSwapchain(P.vtSwap2, P.sc3Swap2, &P.orig2);   /* A11: a segunda vtable tem os seus originais */
    if (P.vtFila != nullptr)
    {
        void* o;
        if (PatchSlot(P.vtFila, VT_EXECUTECOMMANDLISTS, (void*)&HkExecuteCommandLists, &o)) P.origECL = o;
    }
    P.hooksInstalados = P.origPresent != nullptr;
    if (P.hooksInstalados && P.vtSwap2 != nullptr)   /* A11: duas classes de swapchain no mesmo processo */
        Log("ponte: duas vtables de swapchain (0x%p e 0x%p): cada uma com os seus originais; o FG usa os da primeira",
            (void*)P.vtSwap, (void*)P.vtSwap2);
    if (P.hooksInstalados)
        Log("ponte: vtables enganchadas (swapchain=0x%p fila=0x%p)", (void*)P.vtSwap, (void*)P.vtFila);
    else
        Log("ponte: nao consegui enganchar o Present (vtSwap=%p)", (void*)P.vtSwap);
}

void RestaurarHooks()
{
    if (P.hooksRestaurados.exchange(true)) return;
    /* A11: cada vtable volta com os SEUS originais. */
    const Ponte_::OrigsSwap o1 = { P.origPresent, P.origPresent1, P.origResizeBuffers, P.origResizeBuffers1,
                                   P.origResizeTarget, P.origGetBuffer, P.origGetCurBB, P.origSetFs };
    struct { void** vt; const Ponte_::OrigsSwap* o; } alvos[2] = { { P.vtSwap, &o1 }, { P.vtSwap2, &P.orig2 } };
    for (const auto& a : alvos)
    {
        if (a.vt == nullptr) continue;
        RestaurarSlot(a.vt, VT_PRESENT, (void*)&HkPresent, a.o->present);
        RestaurarSlot(a.vt, VT_PRESENT1, (void*)&HkPresent1, a.o->present1);
        RestaurarSlot(a.vt, VT_RESIZEBUFFERS, (void*)&HkResizeBuffers, a.o->resizeBuffers);
        RestaurarSlot(a.vt, VT_RESIZETARGET, (void*)&HkResizeTarget, a.o->resizeTarget);
        RestaurarSlot(a.vt, VT_GETBUFFER, (void*)&HkGetBuffer, a.o->getBuffer);                          /* Q4 */
        RestaurarSlot(a.vt, VT_GETCURBACKBUFFERINDEX, (void*)&HkGetCurrentBackBufferIndex, a.o->getCurBB); /* Q4 */
        RestaurarSlot(a.vt, VT_SETFULLSCREENSTATE, (void*)&HkSetFullscreenState, a.o->setFs);               /* C9 */
        RestaurarSlot(a.vt, VT_RESIZEBUFFERS1, (void*)&HkResizeBuffers1, a.o->resizeBuffers1);              /* B19 */
    }
    if (P.vtFila != nullptr) RestaurarSlot(P.vtFila, VT_EXECUTECOMMANDLISTS, (void*)&HkExecuteCommandLists, P.origECL);
}

/* ================================================================== */
/* Determinar a API e a desc do backbuffer no primeiro Present real     */
/* ================================================================== */
static void PrimeiroPresente(IDXGISwapChain* sc)
{
    /* trava neste swapchain (ignora quaisquer outros) */
    if (P.swapchain == nullptr) P.swapchain = sc;
    if (P.swapchain != sc) return;

    /* API: um backbuffer que abre como ID3D12Resource e D3D12; senao D3D11. */
    Api api = API_NENHUMA;
    ID3D12Resource* r12 = nullptr;
    if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&r12))) && r12 != nullptr)
    {
        api = API_D3D12;
        r12->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&P.d12.dev));
        r12->Release();
    }
    else
    {
        ID3D11Texture2D* r11 = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&r11))) && r11 != nullptr)
        {
            api = API_D3D11;
            r11->GetDevice(&P.d11.dev);
            if (P.d11.dev != nullptr)
            {
                P.d11.dev->GetImmediateContext(&P.d11.ctx);
                P.d11.ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), reinterpret_cast<void**>(&P.d11.ctx4));
            }
            r11->Release();
        }
    }
    if (api == API_NENHUMA) { Desligar("GetBuffer nao devolveu nem D3D12 nem D3D11"); return; }

    DXGI_SWAP_CHAIN_DESC scd = {};
    sc->GetDesc(&scd);
    P.bbW = scd.BufferDesc.Width; P.bbH = scd.BufferDesc.Height;
    P.bbFmt = scd.BufferDesc.Format; P.bbBuffers = scd.BufferCount; P.bbFlags = scd.Flags;
    P.corFmt = FormatoTipado(P.bbFmt);
    P.saidaFmt = FormatoSaidaPara(P.corFmt);   /* F0: 16F/10 bits voltam no mesmo formato; 8 bits em RGBA8 */

    P.api = api;
    Fg::ApiDoJogo(api == API_D3D12);   /* X-PAINEL: jogo_api= sempre no .ngx-estado */
    Log("ponte: hook instalado api=%s swapchain=0x%p backbuffer=%ux%u fmt=%u buffers=%u flags=0x%X",
        api == API_D3D12 ? "D3D12" : "D3D11", (void*)sc, P.bbW, P.bbH, (unsigned)P.bbFmt, P.bbBuffers, P.bbFlags);
    P.logouHook = true;

    if (P.sonda) return;   /* G1: na sonda nao ha host nem build; o Present passa direto */

    if (!FormatoCorSuportado(P.corFmt))
    { Desligar("formato de backbuffer nao suportado"); return; }

    /* pede o build (a thread da ponte cria os recursos e faz o 'B'). */
    P.descPronto = true;
    P.pedidoBuild = true;
    SetEvent(P.evBuild);
}

/* ================================================================== */
/* 7. Nucleo por quadro (chamado do Present hook, thread do jogo)       */
/* ================================================================== */
/* O corpo, JA DENTRO do conjunto (quem chama contou a entrada). */
static void ProcessarQuadroNoConjunto(IDXGISwapChain* sc)
{
    Tempos t;
    if (P.api.load() == API_D3D11) D11Quadro(sc, &t);
    else                          D12Quadro(sc, &t);

    if (P.fase.load() != FASE_ATIVA) return;     /* Desligar no meio do quadro */

    /* medicao: janela de 300 quadros */
    if (P.quadrosJanela == 0) P.janelaInicio = Agora();
    P.ultimo = t;
    P.acc.copia += t.copia; P.acc.espera += t.espera; P.acc.blit += t.blit;
    ++P.quadrosJanela;
}

static void ProcessarQuadro(IDXGISwapChain* sc)
{
    const Fase f = P.fase.load();
    if (f != FASE_ATIVA) return;                 /* conectando / drenando / morta: passa direto */
    if (P.api.load() == API_NENHUMA) { PrimeiroPresente(sc); return; }
    if (P.sonda) return;                         /* G1: sonda: nada por quadro */
    if (P.swapchain != nullptr && P.swapchain != sc) return;   /* outro swapchain */
    if (!P.built.load()) return;                 /* ainda montando o conjunto */
    if (P.derrubarPedido.load(std::memory_order_acquire)) return;   /* derrubada pedida: nao entra mais */

    /* O QUADRO EM VOO, contado. E o que permite ao PedirDerrubada esperar antes de soltar
       allocator e lista - os objetos do endereco `Apresentar+0x69E` das quedas. Custo: dois
       incrementos atomicos por quadro, sem trava e sem chamada de sistema.
       Se um estouro levar a DLL a ficar inerte no meio, o decremento nao roda e o contador
       fica preso; a consequencia e so a espera bater no teto, e ninguem mais entra aqui. */
    P.noQuadro.fetch_add(1, std::memory_order_acq_rel);
    if (P.built.load() && !P.derrubarPedido.load(std::memory_order_acquire))
        ProcessarQuadroNoConjunto(sc);
    P.noQuadro.fetch_sub(1, std::memory_order_acq_rel);
}

/* ================================================================== */
/* 6b. Hooks propriamente ditos                                        */
/* ================================================================== */
/* A11: o trampolim certo para ESTE swapchain. Com uma vtable so (o caso de sempre) devolve
   `daPrimeira` sem olhar mais nada; com duas, o objeto diz de qual classe do DXGI ele e. */
/* ================================================================== */
/* I10: A REDE DE SEGURANCA                                            */
/* ================================================================== */
/* Ver ponte_seh.h para o porque e para o contrato da guarda. Aqui fica o que acontece
   DEPOIS de um estouro: registrar e ficar inerte.
//
   O QUE ESTA FUNCAO DELIBERADAMENTE NAO FAZ, que e a parte importante:
    - nao restaura os hooks e nao descarrega a DLL. Tirar os detours da vtable exige saber
      que nenhuma thread esta dentro deles, e acabamos de descobrir que nao sabemos nada
      sobre o estado do processo. Detour instalado que so repassa a chamada e barato e
      seguro; detour removido no instante errado, ou DLL descarregada com a vtable ainda
      apontando para ela, fecha o jogo - exatamente o que estamos evitando.
    - nao sinaliza `evUnload`: aquele evento conduz a sequencia de descarga, e ela mexe em
      recursos que podem ser justamente os que explodiram.
   `FASE_MORTA` entra porque as NOSSAS threads tambem precisam parar de tocar em D3D12: um
   estouro numa thread nossa mata o processo do jogo do mesmo jeito.
//
   LIMITE CONHECIDO, escrito porque alguem vai encontrar: se o frame generation ja tinha
   entregado proxies de backbuffer ao jogo (Fg::JogoNosProxies), ficar inerte deixa o jogo
   desenhando em superficies que ninguem mais apresenta - a imagem congela, o jogo nao
   fecha. Congelar e pior que funcionar e melhor que fechar, e sair desse estado com
   seguranca depende da correcao da causa, nao da rede. */
static void InerteRegistrar(const char* onde, DWORD code, void* addr);

static void FicarInerte(const char* onde, DWORD code, void* addr)
{
    bool esperado = false;
    if (!P.inerte.compare_exchange_strong(esperado, true)) return;   /* uma vez por sessao */
    if (onde != nullptr) { strncpy(P.inerteOnde, onde, sizeof(P.inerteOnde) - 1); P.inerteOnde[sizeof(P.inerteOnde) - 1] = 0; }
    P.inerteCode = code; P.inerteAddr = addr;
    /* NUNCA em silencio: cada estouro tem de sair no log para poder ser consertado. O
       deslocamento e o que se resolve depois com o objdump/addr2line da DLL. */
    HMODULE self = nullptr;
    void* base = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&FicarInerte), &self) && self != nullptr) base = (void*)self;
    const unsigned long long desl = (base != nullptr && addr != nullptr && addr >= base)
                                  ? (unsigned long long)((const char*)addr - (const char*)base) : 0ull;
    Log("ponte: ESTOURO em %s - excecao 0x%08X em %p (dlss5-ponte64.dll+0x%llX, thread %lu) [D5-NAT-206]. A DLL fica "
        "INERTE: daqui em diante todo hook nosso so chama o original, o jogo segue e nao tentamos de novo "
        "nesta sessao. O app vai ver o Nativo parar e cair para o Universal.",
        onde != nullptr ? onde : "?", (unsigned)code, addr, desl, GetCurrentProcessId() ? GetCurrentThreadId() : 0);
    P.fase = FASE_MORTA;   /* as nossas threads param de tocar em D3D12; sem evUnload */
    InerteRegistrar(onde, code, addr);
}

/* O arquivo que o dono manda: `erros-nativo.log` ao lado do exe do app (a mesma pasta de
   onde esta DLL foi injetada, e onde o app grava o erros-app.log). APPEND, nunca truncado -
   o log da DLL em %TEMP% e por pid e se perde; este acumula.
   J4 (18/09/2026): generalizado. `efeito` e a linha que diz o que a DLL fez com o estouro e
   `avisarApp` escreve o `inerte=1` no .saude. Ficar inerte e UM dos efeitos; a thread de ritmo
   do frame generation (fg_hook.cpp) usa o outro: desiste do FG e o jogo segue pelo Present
   direto, com a DLL inteira - entao o app nao deve ser mandado para o Universal por isso. */
static void EstouroRegistrar(const char* onde, DWORD code, void* addr, const char* efeito, bool avisarApp)
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&EstouroRegistrar), &self) || self == nullptr) return;
    wchar_t p[MAX_PATH]{};
    if (GetModuleFileNameW(self, p, MAX_PATH) == 0) return;
    const unsigned long long desl = (addr != nullptr && addr >= (void*)self)
                                  ? (unsigned long long)((const char*)addr - (const char*)self) : 0ull;
    if (wchar_t* s = wcsrchr(p, L'\\')) *(s + 1) = 0;
    wchar_t arq[MAX_PATH];
    _snwprintf(arq, MAX_PATH, L"%serros-nativo.log", p);
    arq[MAX_PATH - 1] = 0;
    wchar_t exe[MAX_PATH] = L"?";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SYSTEMTIME st; GetLocalTime(&st);
    /* X-CODIGOS-ERRO (18/09/2026): o bloco leva o CODIGO do catalogo (mensagens.h do app, que
       esta DLL nao inclui - os dois numeros ficam aqui, literais, e nunca mudam: D5-NAT-206 =
       a DLL ficou inerte, D5-FG-206 = a thread de ritmo do FG desistiu) e a ASSINATURA que
       agrupa os relatos: modulo + deslocamento + codigo da excecao + onde, nunca o endereco
       absoluto (ASLR). O app ve o mesmo sinal 6 e casa com D5-NAT-206 pelo vigia. */
    char corpo[1280];
    const int n = snprintf(corpo, sizeof corpo,
        "\r\n== ESTOURO NO NATIVO %04d-%02d-%02d %02d:%02d:%02d ==\r\n"
        "codigo     %s\r\n"
        "assinatura dlss5-ponte64.dll+0x%llX \xC2\xB7 0x%08X \xC2\xB7 %s\r\n"
        "hook       %s\r\n"
        "excecao    0x%08X em %p  (dlss5-ponte64.dll+0x%llX)\r\n"
        "jogo       %ls (pid %lu, thread %lu)\r\n"
        "dll        %ls\r\n"
        "estado     api=%d fase=%s built=%d swapchain=%p backbuffer=%ux%u\r\n"
        "efeito     %s\r\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        avisarApp ? "D5-NAT-206" : "D5-FG-206",
        desl, (unsigned)code, onde != nullptr ? onde : "?",
        onde != nullptr ? onde : "?", (unsigned)code, addr, desl,
        exe, GetCurrentProcessId(), GetCurrentThreadId(), p,
        (int)P.api.load(), P.fase.load() == FASE_MORTA ? "morta" : "ativa", P.built.load() ? 1 : 0,
        (void*)P.swapchain, P.bbW, P.bbH, efeito != nullptr ? efeito : "?");
    if (n <= 0) return;
    HANDLE h = CreateFileW(arq, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD escritos = 0;
    WriteFile(h, corpo, (DWORD)n, &escritos, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!avisarApp) return;

    /* AVISAR O APP pelo canal que ele JA le a cada 1 s (o vigia de saude, sinal 3): o
       mesmo %TEMP%\dlss5-ponte.<pid>.saude do VigiaPublicar, com duas chaves novas. Nao
       inventamos protocolo: um app antigo ignora as chaves que nao conhece e continua
       caindo para o Universal pelo sinal 5 (imagem do jogo parada com a ponte), que e
       como ele ja percebeu estas quedas. `parada_ms` alto e de proposito - e o que faz o
       vigia decidir rapido. Depois desta escrita a vigia nao publica mais (FASE_MORTA),
       entao ninguem sobrescreve o motivo. */
    wchar_t tmp[MAX_PATH] = L"", saude[MAX_PATH] = L"";
    const DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (k == 0 || k >= MAX_PATH) return;
    _snwprintf(saude, MAX_PATH, L"%sdlss5-ponte.%lu.saude", tmp, GetCurrentProcessId());
    saude[MAX_PATH - 1] = 0;
    char sc2[512];
    const int m = snprintf(sc2, sizeof sc2,
        "direta=0\nparada_ms=100000\nmovendo=0\ninerte=1\nmotivo=estouro em %s (0x%08X em dlss5-ponte64.dll+0x%llX)\n",
        onde != nullptr ? onde : "?", (unsigned)code, desl);
    if (m <= 0) return;
    HANDLE hs = CreateFileW(saude, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hs == INVALID_HANDLE_VALUE) return;
    WriteFile(hs, sc2, (DWORD)m, &escritos, nullptr);
    CloseHandle(hs);
}

static void InerteRegistrar(const char* onde, DWORD code, void* addr)
{
    EstouroRegistrar(onde, code, addr, "a DLL ficou INERTE; os hooks passam a chamar o original e o jogo segue.", true);
}

/* J4: o registrador que o frame generation recebe no Fg::Iniciar. A thread de ritmo dele e nossa
   e um estouro la mataria o jogo do mesmo jeito; guardada, ela desiste do FG (o Present direto
   volta a copiar o quadro do jogo pelo espelho, C10) e o resto da DLL segue de pe. Fica o bloco
   no erros-nativo.log e a linha no log, sem `inerte=1`: o Nativo continua valendo. */
static void EstouroRegistrarFg(const char* onde, DWORD code, void* addr, const char* efeito)
{
    EstouroRegistrar(onde, code, addr, efeito, false);
}

static void* OrigDoSwap(void* sc, void* daPrimeira, void* daSegunda)
{
    if (P.vtSwap2 == nullptr || daSegunda == nullptr || sc == nullptr) return daPrimeira;
    return VtableDe(sc) == P.vtSwap2 ? daSegunda : daPrimeira;
}

/* C14: Present e Present1 passam pelo mesmo caminho; um = veio do Present1 (pp so vale nele) */
static HRESULT PresentDireto(IDXGISwapChain* sc, UINT si, UINT flags, const DXGI_PRESENT_PARAMETERS* pp, bool um)
{
    return um ? ((Present1Fn)OrigDoSwap(sc, P.origPresent1, P.orig2.present1))((IDXGISwapChain1*)sc, si, flags, pp)
              : ((PresentFn)OrigDoSwap(sc, P.origPresent, P.orig2.present))(sc, si, flags);
}

static HRESULT Apresentar(IDXGISwapChain* sc, UINT si, UINT flags, const DXGI_PRESENT_PARAMETERS* pp, bool um)
{
    if (!P.logouPrimeiroPresent.exchange(true))
        Log("ponte: primeiro Present interceptado (hook por vtable funcionando)");
    Ngx::AoApresentar();   /* G1: marca o quadro para a contagem do ciclo da sonda */
    const Fase f = P.fase.load();
    if (f == FASE_DRENANDO)
    {
        const HRESULT hr = PresentDireto(sc, si, flags, pp, um);
        if (P.swapchain == sc && ++P.drenados >= 2)
        {
            /* C13: jogo ainda nos proxies do FG: os hooks ficam para o espelho da descarga */
            if (!Fg::JogoNosProxies()) RestaurarHooks();
            P.fase = FASE_MORTA; SetEvent(P.evUnload);
        }
        return hr;
    }
    if (f == FASE_MORTA)
    {
        ++P.noEspelho;   /* C16: conta antes de ler residual (a entrega espera zerar) */
        if (P.residual.load()) { try { Fg::Espelhar(sc); } catch (...) {} }   /* C13 */
        --P.noEspelho;
        return PresentDireto(sc, si, flags, pp, um);
    }

    /* Q4: com o FG dono da apresentacao, a thread de ritmo cuida do Present (interpolado +
       real). Ele so assume depois de um ResizeBuffers (ver HkResizeBuffers). Enquanto nao
       assume, o caminho de sempre (fase 2/3) roda. C14: o ritmo apresenta pelo Present. */
    const int64_t tEntra = Agora();
    Fg::Recolher();   /* C3: o ritmo desistiu? desmonta aqui, nunca na propria thread dele */
    try { Tela::AoApresentar(sc); } catch (...) {}   /* C2/E8: fps do jogo e modo de exibicao */
    if (Fg::DonoDaApresentacao())
        return Fg::Presentar(sc, si, flags, OrigDoSwap(sc, P.origPresent, P.orig2.present));   /* A11 */
    Fg::Sondar(sc);   /* P1: uma vez, so com o FG pedido (diagnostico do Alt+Enter) */

    const int64_t tProc = Agora();
    P.ultimo = Tempos();
    try { ProcessarQuadro(sc); } catch (...) {}   /* C3: nenhuma excecao sai do hook */
    const int64_t tDesenho = Agora();
    /* C10: FG desistiu e o jogo segue nos proxies: copia para o backbuffer real (ja com o contador) */
    if (!Fg::Espelhar(sc))
    {
        try { Tela::Desenhar(sc); } catch (...) {}    /* C2: contador por cima da imagem final */
    }
    const int64_t a = Agora();
    const HRESULT hr = PresentDireto(sc, si, flags, pp, um);
    if (P.swapchain == sc && P.built.load() && P.fase.load() == FASE_ATIVA)
    {
        const int64_t tSai = Agora();
        P.accPresent += Ms(a, tSai);
        /* B4: quadro do jogo que levou mais de 60 ms de Present a Present: onde foi o tempo */
        if (P.saidaAnt != 0 && Ms(P.saidaAnt, tSai) > 60.0 && P.solucos < 40)
        {
            ++P.solucos;
            SYSTEMTIME st; GetLocalTime(&st);
            Log("ponte: soluco %02d:%02d:%02d.%03d intervalo %.1f ms | fora do hook %.1f | antes %.1f | copia-in %.1f espera-out %.1f blit-out %.1f | resto do processar %.1f | contador %.1f | present %.1f ms",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, Ms(P.saidaAnt, tSai), Ms(P.saidaAnt, tEntra), Ms(tEntra, tProc),
                P.ultimo.copia, P.ultimo.espera, P.ultimo.blit, Ms(tProc, tDesenho) - P.ultimo.copia - P.ultimo.espera - P.ultimo.blit,
                Ms(tDesenho, a), Ms(a, tSai));
        }
        P.saidaAnt = tSai;
        if (P.quadrosJanela >= 300)
        {
            const double seg = Ms(P.janelaInicio, Agora()) / 1000.0;
            const double q = (double)P.quadrosJanela;
            /* J5: quantos desses quadros levaram depth+mv reais nos slots (0 = jogo sem upscaler
               enganchado, "antes do upscaling" desligado, ou app que nao entende 'D') */
            Log("ponte: 300 quadros | copia-in %.2f ms | espera-out %.2f ms | blit-out %.2f ms | present %.2f ms | fps %.1f | depth+mv do jogo em %llu/%llu",
                P.acc.copia / q, P.acc.espera / q, P.acc.blit / q, P.accPresent / q, seg > 0 ? q / seg : 0.0,
                (unsigned long long)P.janDados, (unsigned long long)P.quadrosJanela);
            P.acc = Tempos(); P.accPresent = 0; P.quadrosJanela = 0; P.janDados = 0;
        }
    }
    return hr;
}

/* I10: os hooks abaixo sao a CASCA e nada mais - o teste de `inerte`, a guarda, a chamada do
   corpo e o original. Nenhum objeto C++ com destrutor que precise rodar mora aqui (a PtGuarda
   e a unica, e o destrutor dela e idempotente e ja tratado no caminho do longjmp), porque o
   longjmp da guarda nao desenrola frame nenhum - ver ponte_seh.h.
   A ORDEM IMPORTA: `inerte` vem ANTES de armar. Depois de um estouro o corpo pode ter deixado
   uma trava presa, e a garantia de que o jogo nao trava e justamente ninguem voltar a entrar
   nele. */
static HRESULT STDMETHODCALLTYPE HkPresent(IDXGISwapChain* sc, UINT si, UINT flags)
{
    if (P.inerte.load(std::memory_order_relaxed)) return PresentDireto(sc, si, flags, nullptr, false);
    PtGuarda g;
    if (PT_GUARDA_ARMAR(g))
    {
        FicarInerte("Present", g.code, g.addr);
        return PresentDireto(sc, si, flags, nullptr, false);
    }
    const HRESULT hr = Apresentar(sc, si, flags, nullptr, false);
    PtGuardaDesarmar(&g);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HkPresent1(IDXGISwapChain1* sc, UINT si, UINT pf, const DXGI_PRESENT_PARAMETERS* pp)
{
    if (P.inerte.load(std::memory_order_relaxed)) return PresentDireto(sc, si, pf, pp, true);
    PtGuarda g;
    if (PT_GUARDA_ARMAR(g))
    {
        FicarInerte("Present1", g.code, g.addr);
        return PresentDireto(sc, si, pf, pp, true);
    }
    const HRESULT hr = Apresentar(sc, si, pf, pp, true);   /* C14: FG, espelho e contador como no Present */
    PtGuardaDesarmar(&g);
    return hr;
}

/* Espera o quadro em voo sair do conjunto. Mesmo desenho do `noEspelho` mais abaixo (C16):
   fora de qualquer trava e com teto. Se o teto estourar, seguimos - quem chega aqui esta num
   caminho que o jogo precisa que termine (o ResizeBuffers), e a `geracao` ja garante que o
   que for montado em cima nao sera publicado. */
static void EsperarSemQuadro(double ms)
{
    /* NO CASO NORMAL ISTO NAO ESPERA NADA: Present e ResizeBuffers vem da mesma thread do
       jogo, que nao pode estar nos dois ao mesmo tempo, e o contador ja e zero na primeira
       leitura. A espera existe para quando o frame generation esta com a thread de ritmo
       dele apresentando - ai o quadro em voo e de outra thread de verdade.
       Sleep(1) e nao Sleep(0): Sleep(0) so cede para thread de MESMA prioridade, e a da
       ponte pode nao estar nessa faixa - seria meio segundo de espera ocupada queimando um
       nucleo dentro do ResizeBuffers do jogo. */
    const int64_t t0 = Agora();
    while (P.noQuadro.load() > 0 && Ms(t0, Agora()) < ms) Sleep(1);
    if (P.noQuadro.load() > 0)
        Log("ponte: ainda havia %d quadro(s) no conjunto depois de %.0f ms de espera", P.noQuadro.load(), ms);
}

/* PEDIDO de derrubada, da thread do JOGO (ResizeBuffers). NAO solta o conjunto - ver o
   comentario de `geracao` no Ponte_. Faz tres coisas, nesta ordem, e cada uma importa:
     1. marca e incrementa a geracao ANTES de qualquer coisa, para que um Build que esteja
        montando neste instante seja descartado por quem o monta (ele confere a geracao no
        fim) e para que nenhum quadro novo entre no conjunto;
     2. espera o quadro em voo sair, porque o passo 3 solta allocator e lista de comandos -
        exatamente os objetos do outro endereco da queda (`Apresentar+0x69E`, que e
        `alloc[i]->Reset()`);
     3. solta SO o que prende os backbuffers. Isto e obrigatorio: o ResizeBuffers original
        falha se alguem ainda tiver referencia a eles. O resto (device, texturas
        compartilhadas, fences) fica para a thread da ponte. */
static void PedirDerrubada(const char* motivo)
{
    P.derrubarPedido.store(true, std::memory_order_release);
    P.built = false;
    const unsigned g = P.geracao.fetch_add(1, std::memory_order_acq_rel) + 1;
    EsperarSemQuadro(500.0);
    if (P.api.load() == API_D3D12)
    {
        /* A GPU PRIMEIRO, e este passo nao e opcional: o D12SoltarRefsBackbuffer solta
           allocators e listas de comandos, e soltar um allocator que a GPU ainda esta
           executando e erro de uso do D3D12. Quem fazia essa espera antes era o D12Liberar
           (a fence de reciclagem, no topo dele) - e ao passar a soltar as refs aqui, sem ela,
           eu tinha trocado uma corrida por outra. O caminho `manter` logo acima ja fazia
           exatamente isto, pelo mesmo motivo. */
        /* O LIMITE HONESTO DESTE PASSO: se a espera do quadro em voo bateu no teto, as refs
           saem de todo jeito - e a unica das trocas que nao pode ser adiada, porque o
           ResizeBuffers original FALHA com referencia pendente ao backbuffer, e devolver
           erro de resize para o jogo e pior do que o risco. O `built = false` acima fecha a
           porta para quem ainda nao entrou, e o que resta e uma janela estreita para quem ja
           estava dentro. Tudo o que PODE ser adiado (device, texturas, fences) e adiado, no
           CompletarDerrubada, e lá a regra de "nada com quadro em voo" vale sem exceção. */
        bool emDia = false;
        try { emDia = D12GpuEmDia(); } catch (...) { emDia = false; }
        if (!emDia) Log("ponte: a fence de reciclagem nao alcancou o valor em 1 s; soltando as refs de backbuffer de todo jeito (o ResizeBuffers do jogo nao pode esperar mais)");
        D12SoltarRefsBackbuffer();
    }
    else if (P.d11.ctx != nullptr) P.d11.ctx->Flush();
    P.descPronto = false; P.outValido = false; P.sentN = 0;
    Log("ponte: derrubada pedida (%s) - geracao %u; as refs de backbuffer sairam aqui, o resto "
        "sai na thread da ponte (quem remonta e quem solta)", motivo != nullptr ? motivo : "?", g);
    SetEvent(P.evBuild);   /* acorda a thread da ponte para completar e remontar */
}

/* COMPLETA a derrubada - so na thread da PONTE, e por isso nao ha corrida: e a mesma thread
   que monta. Chamada no topo da volta do laco de servico e no comeco do FazerBuild. */
static void CompletarDerrubada()
{
    if (!P.derrubarPedido.load(std::memory_order_acquire)) return;
    P.built = false; P.descPronto = false; P.outValido = false; P.sentN = 0;
    EsperarSemQuadro(500.0);   /* de novo: entre o pedido e aqui alguem pode ter entrado */
    /* NADA E SOLTO COM QUADRO EM VOO, e aqui isto e uma regra e nao um esforco. Se a espera
       bateu no teto, alguem ainda esta dentro do conjunto - tipicamente a thread de ritmo do
       frame generation, que pode estar parada esperando uma fence que justamente esta thread
       sinalizaria. Soltar agora seria trocar a corrida por um use-after-free garantido.
       A bandeira NAO e consumida: o laco de servico volta em 250 ms e tenta de novo. */
    if (P.noQuadro.load() > 0)
    {
        Log("ponte: derrubada ADIADA - ainda ha %d quadro(s) no conjunto; nada e solto com quadro em voo "
            "(nova tentativa na proxima volta)", P.noQuadro.load());
        return;
    }
    P.derrubarPedido.store(false, std::memory_order_release);
    if (P.api.load() == API_D3D11) D11Liberar();
    else if (P.api.load() == API_D3D12) D12Liberar();
    /* dev/ctx/fila sao do jogo: reabrimos no proximo build a partir do swapchain. */
    P.api = API_NENHUMA;   /* forca PrimeiroPresente a reler a desc nova */
    P.swapchain = nullptr;
    Log("ponte: derrubada completada na thread da ponte (geracao %u)", P.geracao.load());
}

/* B14 (H6): muitos jogos chamam ResizeBuffers(0, 0, ...) ao ganhar foco, com WM_SIZE ou ao
   sair de menu, sem mudar nada. So o caso sem duvida mantem o conjunto: sessao montada, FG
   fora (nem dono nem espelho: C13-C18 e a reinjecao C21 passam longe), e contagem, flags,
   largura, altura e formato pedidos iguais aos atuais ou "manter" (0 / UNKNOWN; 0 x 0 e o
   tamanho da janela, conferido pelo GetDesc depois da chamada original). Lido ANTES do
   Fg::AntesResize, que zera o estado do FG. */
/* B19: um pedido de resize. um = veio do ResizeBuffers1 (nos e filas so valem nele, um por buffer). */
struct PedidoResize
{
    UINT bc, w, h; DXGI_FORMAT fmt; UINT flags;
    const UINT* nos; IUnknown* const* filas; bool um;
    const char* Nome() const { return um ? "ResizeBuffers1" : "ResizeBuffers"; }
};

static HRESULT ResizeDireto(IDXGISwapChain* sc, const PedidoResize& r)
{
    return r.um ? ((ResizeBuffers1Fn)OrigDoSwap(sc, P.origResizeBuffers1, P.orig2.resizeBuffers1))(static_cast<IDXGISwapChain3*>(sc), r.bc, r.w, r.h, r.fmt, r.flags, r.nos, r.filas)
                : ((ResizeBuffersFn)OrigDoSwap(sc, P.origResizeBuffers, P.orig2.resizeBuffers))(sc, r.bc, r.w, r.h, r.fmt, r.flags);
}

/* B19: no ResizeBuffers1, filas e nos vem um por buffer (bc, ou a contagem atual com bc = 0).
   *fila = a fila comum a todos (nullptr sem filas); true = filas ou nos diferentes entre buffers. */
static bool FilasMistas(IDXGISwapChain* sc, const PedidoResize& r, IUnknown** fila)
{
    *fila = nullptr;
    if (!r.um || (r.nos == nullptr && r.filas == nullptr)) return false;
    UINT n = r.bc;
    if (n == 0) { DXGI_SWAP_CHAIN_DESC d = {}; if (FAILED(sc->GetDesc(&d))) return true; n = d.BufferCount; }
    for (UINT i = 1; i < n; ++i)
        if ((r.nos && r.nos[i] != r.nos[0]) || (r.filas && r.filas[i] != r.filas[0])) return true;
    if (r.filas && n > 0) *fila = r.filas[0];
    return false;
}

static bool ResizeMantemConjunto(IDXGISwapChain* sc, const PedidoResize& r, IUnknown* fila)
{
    if (P.swapchain != sc || P.fase.load() != FASE_ATIVA || !P.built.load() || P.sonda) return false;
    const Api api = P.api.load();
    if (api != API_D3D11 && api != API_D3D12) return false;
    if (Fg::DonoDaApresentacao() || Fg::Espelhando()) return false;
    if (r.bc != 0 && r.bc != P.bbBuffers) return false;
    if ((r.w != 0 && r.w != P.bbW) || (r.h != 0 && r.h != P.bbH)) return false;
    if (r.fmt != DXGI_FORMAT_UNKNOWN && r.fmt != P.bbFmt) return false;
    /* B19: fila (D3D12) ou device (D3D11) do ResizeBuffers1 diferente do conjunto: caminho completo */
    if (fila != nullptr && fila != (api == API_D3D12 ? static_cast<IUnknown*>(P.d12.fila) : static_cast<IUnknown*>(P.d11.dev))) return false;
    return r.flags == P.bbFlags;
}

/* B14: depois da chamada original, com o conjunto mantido. Qualquer coisa fora do caso simples
   (falhou, desc resolvida diferente, FG assumiu, ponte desligando) cai no caminho antigo. */
static void ReligarAposResize(IDXGISwapChain* sc, HRESULT hr, int64_t t0, const char* nome)
{
    if (P.fase.load() != FASE_ATIVA) return;   /* Desligar no meio: a thread da ponte libera tudo */
    DXGI_SWAP_CHAIN_DESC scd = {};
    const bool igual = SUCCEEDED(hr) && SUCCEEDED(sc->GetDesc(&scd)) &&
                       scd.BufferDesc.Width == P.bbW && scd.BufferDesc.Height == P.bbH &&
                       scd.BufferDesc.Format == P.bbFmt && scd.BufferCount == P.bbBuffers && scd.Flags == P.bbFlags;
    const bool fg = Fg::DonoDaApresentacao() || Fg::Espelhando();
    bool ok = igual && !fg;
    if (ok && P.api.load() == API_D3D12) ok = D12CriarRefsBackbuffer(sc);
    if (!ok)
    {
        Log("ponte: %s sem manter (hr=0x%08X, desc %ux%u fmt=%u buffers=%u flags=0x%X, fg=%d); derrubando o conjunto para refazer o Build",
            nome, (unsigned)hr, scd.BufferDesc.Width, scd.BufferDesc.Height, (unsigned)scd.BufferDesc.Format, scd.BufferCount, scd.Flags, fg ? 1 : 0);
        PedirDerrubada("religar falhou");
        return;
    }
    P.built = true;
    Log("ponte: %s mantido: backbuffers religados em %.1f ms (texturas, fences e sessao com o app intactas)", nome, Ms(t0, Agora()));
}

/* B19: threads dentro de um hook de resize. O DXGI (ou outro hook por cima do nosso) pode chamar o
   ResizeBuffers de dentro do ResizeBuffers1 pela vtable, ou o contrario: a chamada de dentro vai
   direto ao original, sem tratar duas vezes. Ids de thread em atomicos, sem TLS (a dll descarrega e
   recarrega no mesmo processo). Mais de 4 resizes ao mesmo tempo: o excedente fica sem a marca. */
static std::atomic<DWORD> g_noResize[4];
struct MarcaResize
{
    int i = -1;
    bool aninhada = false;
    MarcaResize()
    {
        const DWORD eu = GetCurrentThreadId();
        for (const auto& t : g_noResize) if (t.load() == eu) { aninhada = true; return; }
        for (int k = 0; k < 4; ++k) { DWORD livre = 0; if (g_noResize[k].compare_exchange_strong(livre, eu)) { i = k; return; } }
    }
    ~MarcaResize() { if (i >= 0) g_noResize[i] = 0; }
};

/* B14/B19: ResizeBuffers e ResizeBuffers1 passam por aqui */
static HRESULT Redimensionar(IDXGISwapChain* sc, const PedidoResize& r)
{
    const MarcaResize marca;
    if (marca.aninhada) return ResizeDireto(sc, r);
    bool manter = false, mistas = false;
    IUnknown* fila = nullptr;
    try
    {
        mistas = FilasMistas(sc, r, &fila);
        manter = !mistas && ResizeMantemConjunto(sc, r, fila);
    }
    catch (...) { manter = false; }
    /* Q4: se o FG ja era dono, solta os proxies/backbuffers ANTES do resize real. */
    Fg::AntesResize();
    const int64_t t0 = Agora();
    if (manter)
    {
        /* B14: o Present passa direto ate religar; solta so o que prende os backbuffers. */
        P.built = false;
        if (P.api.load() == API_D3D12)
        {
            try { manter = D12GpuEmDia(); } catch (...) { manter = false; }
            if (manter) D12SoltarRefsBackbuffer();
        }
        /* D3D11: nada nosso prende o backbuffer; o Flush so executa a destruicao adiada do RTV do
           blit (no modo sincrono ele e solto depois do Flush do quadro) */
        else if (P.d11.ctx != nullptr) P.d11.ctx->Flush();
        if (manter) Log("ponte: %s %ux%u fmt=%u buffers=%u; mesmo tamanho e formato, mantendo o conjunto", r.Nome(), r.w, r.h, (unsigned)r.fmt, r.bc);
    }
    if (!manter && P.swapchain == sc && P.fase.load() == FASE_ATIVA)
    {
        /* B14, O CASO QUE NAO PEGAVA (18/09/2026). O `manter` exige sessao montada; quando o
           jogador troca a resolucao DUAS vezes seguidas, a segunda chamada chega com a
           primeira montagem ainda em curso (`built` false), o `manter` cai e o caminho antigo
           derrubava um conjunto que ninguem tinha acabado de montar - atropelando a montagem.
           Era esse o segundo `derrubando o conjunto` dos logs das quatro quedas. Agora a
           diferenca esta dita no log e no efeito: sem conjunto montado nao ha o que derrubar,
           e a geracao nova (dentro do PedirDerrubada) faz a montagem em curso ser descartada
           por quem a monta, em vez de ter o chao retirado. */
        if (!P.built.load())
            Log("ponte: %s %ux%u fmt=%u%s com o conjunto AINDA MONTANDO; nada a derrubar - a geracao "
                "nova descarta a montagem em curso e o Build seguinte usa a desc nova",
                r.Nome(), r.w, r.h, (unsigned)r.fmt, mistas ? " (filas ou nos diferentes por buffer)" : "");
        else
            Log("ponte: %s %ux%u fmt=%u%s; derrubando o conjunto para refazer o Build", r.Nome(), r.w, r.h, (unsigned)r.fmt, mistas ? " (filas ou nos diferentes por buffer)" : "");
        /* solta as refs de backbuffer (RTVs) ANTES do ResizeBuffers original, senao ele falha;
           o resto do conjunto sai na thread da ponte. */
        PedirDerrubada(r.Nome());
    }
    const HRESULT hr = ResizeDireto(sc, r);
    /* Q4: DESENHO APROVADO -- e a partir do PRIMEIRO ResizeBuffers depois de integrar que a
       DLL vira dona da apresentacao (nao da para trocar a swapchain de um jogo ja aberto). Se
       fg=1|auto e D3D12, o FG monta os proxies com a desc NOVA e assume o Present.
       B19: do ResizeBuffers1, o FG recebe a fila pedida (ou nao assume com filas mistas). */
    if (SUCCEEDED(hr)) Fg::AposResize(sc, fila, mistas);
    /* B14: depois do AposResize, para ver se o FG assumiu (ai o conjunto cai, como antes) */
    if (manter) { try { ReligarAposResize(sc, hr, t0, r.Nome()); } catch (...) { PedirDerrubada("excecao ao religar"); } }
    return hr;
}

/* I10: o ResizeBuffers e o caminho mais perigoso que temos - e onde o conjunto e derrubado e
   remontado, e e dele que vieram as quatro quedas do RE2. A guarda aqui e o que separa "a
   troca de resolucao desligou o Nativo" de "a troca de resolucao fechou o jogo". No estouro,
   a chamada original AINDA e feita: o jogo pediu um resize e precisa dele para continuar. */
static HRESULT STDMETHODCALLTYPE HkResizeBuffers(IDXGISwapChain* sc, UINT bc, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags)
{
    const PedidoResize r{ bc, w, h, fmt, flags, nullptr, nullptr, false };
    if (P.inerte.load(std::memory_order_relaxed)) return ResizeDireto(sc, r);
    PtGuarda g;
    if (PT_GUARDA_ARMAR(g))
    {
        FicarInerte("ResizeBuffers", g.code, g.addr);
        return ResizeDireto(sc, r);
    }
    const HRESULT hr = Redimensionar(sc, r);
    PtGuardaDesarmar(&g);
    return hr;
}

/* B19: D3D12 (e D3D11 flip) podem redimensionar por aqui; o mesmo tratamento do ResizeBuffers */
static HRESULT STDMETHODCALLTYPE HkResizeBuffers1(IDXGISwapChain3* sc, UINT bc, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags,
                                                  const UINT* nos, IUnknown* const* filas)
{
    const PedidoResize r{ bc, w, h, fmt, flags, nos, filas, true };
    if (P.inerte.load(std::memory_order_relaxed)) return ResizeDireto(sc, r);
    PtGuarda g;
    if (PT_GUARDA_ARMAR(g))
    {
        FicarInerte("ResizeBuffers1", g.code, g.addr);
        return ResizeDireto(sc, r);
    }
    const HRESULT hr = Redimensionar(sc, r);
    PtGuardaDesarmar(&g);
    return hr;
}

static HRESULT STDMETHODCALLTYPE HkResizeTarget(IDXGISwapChain* sc, const DXGI_MODE_DESC* md)
{
    return ((ResizeTargetFn)OrigDoSwap(sc, P.origResizeTarget, P.orig2.resizeTarget))(sc, md);   /* A11 */
}

/* I10: so a parte NOSSA. O trampolim original fica fora, no hook, e por um motivo que nao e
   estetico: se a guarda pegasse o corpo inteiro, um estouro depois da chamada original faria
   o hook chamar o original DE NOVO no caminho da excecao - duas submissoes da mesma lista de
   comandos. Assim o original e chamado exatamente uma vez em qualquer caminho. */
static void ExecutarListasNosso(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists)
{
    /* a fila do jogo = a primeira fila DIRECT vista (reserva); C15: o Build troca pela do swapchain */
    if (q != nullptr && P.d12.fila == nullptr && P.api.load() != API_D3D11)
    {
        D3D12_COMMAND_QUEUE_DESC qd = q->GetDesc();
        if (qd.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            q->AddRef();
            P.d12.fila = q;
            Log("ponte: fila do jogo=0x%p tipo=DIRECT", (void*)q);
        }
    }
    Fg::FilaVista(q);               /* Q4: a fila do jogo, usada pela geracao e pelo ritmo do FG */
    Tela::FilaVista(q);             /* C2: a fila em que o contador e copiado para o backbuffer */
    Ngx::AoExecutar(q, n, lists);   /* G1: em que fila a lista do EvaluateFeature roda */
    Sr::AoExecutar(q, n, lists);    /* Q2: em que fila a lista do dispatch do FSR/XeSS roda */
}

static void STDMETHODCALLTYPE HkExecuteCommandLists(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists)
{
    /* C13: no espelho da descarga NGX/FSR/contador ja pararam. I10: inerte entra no mesmo teste. */
    if (P.fase.load() == FASE_MORTA || P.inerte.load(std::memory_order_relaxed))
    { ((ECLFn)P.origECL)(q, n, lists); return; }
    PtGuarda g;
    if (PT_GUARDA_ARMAR(g))
    {
        FicarInerte("ExecuteCommandLists", g.code, g.addr);
        ((ECLFn)P.origECL)(q, n, lists);
        return;
    }
    ExecutarListasNosso(q, n, lists);
    PtGuardaDesarmar(&g);
    ((ECLFn)P.origECL)(q, n, lists);
}

/* Q4: GetBuffer/GetCurrentBackBufferIndex -- quando o FG e dono, o jogo recebe NOSSOS
   proxies e o nosso indice; senao, o trampolim original.
   J4 (18/09/2026): os dois sao os caminhos mais quentes do proxy e estavam sem a rede do I10
   (X-FG-PLANO 1.2c). Mesma casca dos outros hooks: `inerte` ANTES de armar, guarda, corpo,
   original. No estouro o original ainda e chamado - o jogo pediu um buffer (ou um indice) e
   precisa da resposta para seguir; o que se abandona e so o nosso lado. Se o corpo ja tinha
   escrito *pp antes de estourar, o original sobrescreve com o backbuffer real e a referencia
   ao proxy vaza: aceitavel numa sessao que acabou de ficar inerte. */
static HRESULT STDMETHODCALLTYPE HkGetBuffer(IDXGISwapChain* sc, UINT buf, REFIID riid, void** pp)
{
    GetBufferFn const orig = (GetBufferFn)OrigDoSwap(sc, P.origGetBuffer, P.orig2.getBuffer);   /* A11 */
    if (P.inerte.load(std::memory_order_relaxed)) return orig(sc, buf, riid, pp);
    PtGuarda g;
    if (PT_GUARDA_ARMAR(g))
    {
        FicarInerte("GetBuffer", g.code, g.addr);
        return orig(sc, buf, riid, pp);
    }
    const bool nosso = Fg::GetBufferProxy(sc, buf, riid, pp);
    PtGuardaDesarmar(&g);
    if (nosso) return S_OK;
    return orig(sc, buf, riid, pp);
}
static UINT STDMETHODCALLTYPE HkGetCurrentBackBufferIndex(IDXGISwapChain3* sc)
{
    GetCurBBFn const orig = (GetCurBBFn)OrigDoSwap(sc, P.origGetCurBB, P.orig2.getCurBB);   /* A11 */
    if (P.inerte.load(std::memory_order_relaxed)) return orig(sc);
    UINT idx = 0;
    PtGuarda g;
    if (PT_GUARDA_ARMAR(g))
    {
        FicarInerte("GetCurrentBackBufferIndex", g.code, g.addr);
        return orig(sc);
    }
    const bool nosso = Fg::IndiceProxy(&idx);
    PtGuardaDesarmar(&g);
    if (nosso) return idx;
    return orig(sc);
}

/* C9: troca de tela cheia exclusiva (pelo jogo ou pelo pedido do app, E8). O ritmo do FG nao
   apresenta durante a chamada; o FG continua montado e o ResizeBuffers seguinte remonta. */
static HRESULT STDMETHODCALLTYPE HkSetFullscreenState(IDXGISwapChain* sc, BOOL fs, IDXGIOutput* alvo)
{
    void* const orig = OrigDoSwap(sc, P.origSetFs, P.orig2.setFs);   /* A11 */
    /* C13: contador ja parado. I10: inerte entra no mesmo teste. */
    if (P.fase.load() == FASE_MORTA || P.inerte.load(std::memory_order_relaxed)) return ((SetFsFn)orig)(sc, fs, alvo);
    /* I10: DUAS guardas, uma por lado, com a chamada original entre elas. O jogo pediu a
       troca de tela cheia e vai receber a resposta do DXGI em qualquer caminho - o que pode
       ser abandonado e so o nosso lado. */
    {
        PtGuarda g;
        if (PT_GUARDA_ARMAR(g)) FicarInerte("SetFullscreenState (antes)", g.code, g.addr);
        else { try { Fg::AntesTelaCheia(); } catch (...) {} PtGuardaDesarmar(&g); }
    }
    const HRESULT hr = ((SetFsFn)orig)(sc, fs, alvo);
    if (!P.inerte.load(std::memory_order_relaxed))
    {
        PtGuarda g;
        if (PT_GUARDA_ARMAR(g)) FicarInerte("SetFullscreenState (depois)", g.code, g.addr);
        else { try { Fg::AposTelaCheia(sc); Tela::TelaCheiaMudou(); } catch (...) {} PtGuardaDesarmar(&g); }
    }
    return hr;
}

/* ================================================================== */
/* 7b. Desligar / drenar                                               */
/* ================================================================== */
void Desligar(const char* motivo)
{
    Fase esperado = FASE_ATIVA;
    if (!P.fase.compare_exchange_strong(esperado, FASE_DRENANDO))
    {
        Fase e2 = FASE_CONECTANDO;
        if (!P.fase.compare_exchange_strong(e2, FASE_DRENANDO)) return;   /* ja desligando */
    }
    snprintf(P.motivo, sizeof(P.motivo), "%s", motivo);
    P.drenados = 0;
    Log("ponte: desligada (%s)", motivo);
    SetEvent(P.evStop);
    /* se ainda nao estamos processando quadros (sem swapchain travado), nao ha
       Present nosso para drenar: libera a thread para descarregar direto. */
    if (P.swapchain == nullptr)
    {
        if (!Fg::JogoNosProxies()) RestaurarHooks();   /* C13: senao os hooks ficam para o espelho */
        P.fase = FASE_MORTA; SetEvent(P.evUnload);
    }
}

/* ================================================================== */
/* 8. Thread da ponte + DllMain                                        */
/* ================================================================== */

/* Faz o build: cria os recursos no device do jogo e troca 'B'/ack. Thread da
   ponte (criacao de recurso e thread-safe nos dois devices). */
static bool FazerBuild()
{
    /* A derrubada que a thread do jogo PEDIU e completada aqui, na thread da ponte, antes de
       montar qualquer coisa. E o ponto inteiro do desenho: soltar e montar passaram a ser a
       mesma thread. */
    CompletarDerrubada();
    /* A derrubada pode ter sido ADIADA (quadro em voo, ver lá). Montar por cima do conjunto
       velho ainda vivo seria abrir device e texturas novas sem soltar as antigas - vazamento
       agora e confusão depois. O SetEvent(evBuild) do PedirDerrubada e a volta de 250 ms do
       laco garantem que passamos por aqui de novo. */
    if (P.derrubarPedido.load(std::memory_order_acquire))
    {
        Log("ponte: o Build espera a derrubada completar (quadro ainda em voo)");
        return false;
    }
    /* A GERACAO COM QUE ESTA MONTAGEM COMECOU. O trecho do pipe abaixo espera o ack do host e
       pode levar centenas de ms; se o jogador trocar a resolucao nesse meio, a thread do jogo
       incrementa a geracao e o que estamos montando ja nao serve. Conferir no fim e descartar
       e o que substitui "ter o chao retirado por outra thread". */
    const unsigned gInicio = P.geracao.load(std::memory_order_acquire);
    DescobrirModo();   /* na hora de montar o Build (vale tambem no rebuild por resize) */
    FeedBuild b = {};
    bool ok = (P.api.load() == API_D3D11) ? D11Build(&b) : D12Build(&b, P.swapchain);
    if (!ok) { Desligar("falha ao criar o conjunto compartilhado"); return false; }

    const BYTE tag = 'B';
    FeedBuildAck ack = {};
    {
        std::lock_guard<std::mutex> l(P.muPipe);
        if (!Escrever(&tag, 1) || !Escrever(&b, sizeof(b)) || !Ler(&ack, sizeof(ack)))
        { Desligar("troca do Build falhou"); return false; }
    }
    if (ack.ok != 1)
    {
        if (ack.flags & kAckSrIndisponivel) { Desligar("host respondeu SR_UNAVAILABLE (nao pedimos SR)"); return false; }
        Desligar("host recusou o Build (ok=0)"); return false;
    }
    P.ackFenceIn = ack.fence_in; P.ackFenceOut = ack.fence_out;
    /* J5: so com este bit o app recebe 'D' (um app antigo fecharia o pipe numa tag que nao conhece) */
    P.hostEntendeD = (ack.flags & kAckDados) != 0;
    P.dadosLogou = false; P.dadosRecusas = 0; P.dadosUsada = 0; P.janDados = 0;
    /* F0: o ack ECOA o formato do Output que propusemos (a textura ja foi criada e o handle
       foi no Build; o host nao tem como troca-la). Um host que respondesse outro formato nao
       mudaria a textura: fica registrado e seguimos com a que existe. */
    if (ack.output_fmt != 0 && (DXGI_FORMAT)ack.output_fmt != P.saidaFmt)
        Log("ponte: host respondeu output=%u, mas o Output criado e fmt=%u: seguindo com o criado (o fio v9 nao renegocia)",
            ack.output_fmt, (unsigned)P.saidaFmt);
    /* A volta: CopyResource quando o Output esta no formato (tipado) do backbuffer - o caso
       HDR 16F/10 bits negociado pelo F0 e o RGBA8 comum; senao o shader de conversao. */
    P.copiaDireta = MesmaFamilia(P.bbFmt, P.saidaFmt);

    ok = (P.api.load() == API_D3D11) ? D11AbrirFences() : D12AbrirFences();
    if (!ok) { Desligar("abrir as fences do host falhou"); return false; }

    /* A CONFERENCIA DA GERACAO, e este e o lugar exato: depois de tudo montado e ANTES de
       publicar `built`. O conjunto que acabamos de montar descreve uma resolucao que o jogo
       ja abandonou, e publica-lo poria a thread do jogo a desenhar com texturas do tamanho
       errado. Soltamos aqui mesmo - na thread da ponte, sem ninguem usando - e devolvemos
       false; o SetEvent(evBuild) do PedirDerrubada ja garantiu que ha outra volta vindo. */
    const unsigned gAgora = P.geracao.load(std::memory_order_acquire);
    if (gAgora != gInicio || P.derrubarPedido.load(std::memory_order_acquire))
    {
        Log("ponte: o conjunto foi derrubado durante a montagem (geracao %u -> %u): descartando o que "
            "montei e remontando com a desc nova", gInicio, gAgora);
        P.built = false;
        if (P.api.load() == API_D3D11) D11Liberar();
        else if (P.api.load() == API_D3D12) D12Liberar();
        P.api = API_NENHUMA;
        P.swapchain = nullptr;
        P.descPronto = false; P.outValido = false; P.sentN = 0;
        P.derrubarPedido.store(false, std::memory_order_release);
        return false;
    }

    P.built = true;
    Log("ponte: build enviado %ux%u color=%u output=%u modo=%s volta=%s | depth+mv para o app: %s",
        P.bbW, P.bbH, (unsigned)P.corFmt, (unsigned)P.saidaFmt, P.asyncHome ? "pipelined" : "sincrono",
        P.copiaDireta ? "copia (CopyResource, sem conversao)" : "shader (conversao de formato)",
        P.hostEntendeD ? "o app entende 'D' (slots recebem o dado do upscaler quando a sessao .up estiver de pe)"
                       : "app sem 'D' (slots ficam zerados)");
    return true;
}

/* G1 (fase 3): modo "sonda". Se %TEMP%\dlss5-ponte.<pid>.modo contem "sonda", a DLL nao
   procura host nem monta Build: fica residente com os hooks de Present (passagem direta,
   so o log do backbuffer) e os hooks NGX logando, ate aparecer o arquivo
   %TEMP%\dlss5-ponte.<pid>.parar (ou o processo sair). E o caminho mais curto para ler o
   contrato NGX de um jogo sem o app; o app nunca grava "sonda" nesse arquivo. */
static void ArquivoTemp(wchar_t* out, size_t n, const wchar_t* sufixo)
{
    wchar_t tmp[MAX_PATH] = L"";
    DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (k == 0 || k >= MAX_PATH) { tmp[0] = L'.'; tmp[1] = L'\\'; tmp[2] = 0; }
    _snwprintf(out, n, L"%sdlss5-ponte.%lu.%ls", tmp, GetCurrentProcessId(), sufixo);
}
static bool ModoSonda()
{
    wchar_t caminho[MAX_PATH]; ArquivoTemp(caminho, MAX_PATH, L"modo");
    FILE* f = _wfopen(caminho, L"rb");
    if (f == nullptr) return false;
    char buf[32] = {}; size_t lidos = fread(buf, 1, sizeof(buf) - 1, f); fclose(f); buf[lidos] = 0;
    return strncmp(buf, "sonda", 5) == 0;
}
static bool PararPedido()
{
    wchar_t caminho[MAX_PATH]; ArquivoTemp(caminho, MAX_PATH, L"parar");
    return GetFileAttributesW(caminho) != INVALID_FILE_ATTRIBUTES;
}

/* C13: o app subiu o pipe para uma sessao nova (instancia livre esperando cliente)? */
static bool PipeDoAppEsperando()
{
    wchar_t nome[128];
    _snwprintf(nome, 128, L"\\\\.\\pipe\\dlss5-feed.%lu", GetCurrentProcessId());
    return WaitNamedPipeW(nome, 1) != FALSE;
}

/* C13: quantas cargas o loader conta para este modulo; 0 = nao sei. LoadCount do DdagNode
   (LDR_DATA_TABLE_ENTRY+0x98, LDR_DDAG_NODE+0x18, x64 Windows 8+; conferido no 11 26200).
   A entrada vem do LdrFindEntryForAddress (trava do loader); nada de percorrer a lista. */
static ULONG ContagemDoModulo()
{
    typedef LONG (NTAPI* FindEntryFn)(void*, void**);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    const FindEntryFn achar = nt ? reinterpret_cast<FindEntryFn>(reinterpret_cast<void*>(GetProcAddress(nt, "LdrFindEntryForAddress"))) : nullptr;
    void* ent = nullptr;
    if (!achar || achar(P.hModule, &ent) < 0 || !ent) return 0;
    if (*static_cast<void**>(static_cast<void*>(static_cast<char*>(ent) + 0x30)) != (void*)P.hModule) return 0;   /* DllBase */
    const char* ddag = *static_cast<char**>(static_cast<void*>(static_cast<char*>(ent) + 0x98));
    if (!ddag) return 0;
    const ULONG n = *static_cast<const ULONG*>(static_cast<const void*>(ddag + 0x18));
    return (n == 0 || n > 1000) ? 0 : n;
}

/* C13: copia da dll em %TEMP% carregada como modulo novo (DllMain e estado proprios) */
static bool CarregarCopia()
{
    wchar_t eu[MAX_PATH] = L"", tmp[MAX_PATH] = L"", copia[MAX_PATH] = L"";
    if (GetModuleFileNameW(P.hModule, eu, MAX_PATH) == 0) return false;
    const DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (k == 0 || k >= MAX_PATH) return false;
    _snwprintf(copia, MAX_PATH, L"%sdlss5-ponte.%lu.copia%llu.dll", tmp, GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    copia[MAX_PATH - 1] = 0;
    return CopyFileW(eu, copia, FALSE) && LoadLibraryW(copia) != nullptr;
}

/* C17: apaga copias de sessoes anteriores em %TEMP%; pula as de processos vivos de outro pid (a
   carregada o Windows nao deixa apagar) */
static void LimparCopiasAntigas()
{
    wchar_t tmp[MAX_PATH] = L"", padrao[MAX_PATH], arq[MAX_PATH];
    const DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (k == 0 || k >= MAX_PATH - 40) return;
    _snwprintf(padrao, MAX_PATH, L"%lsdlss5-ponte.*.copia*.dll", tmp);
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileW(padrao, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    int apagadas = 0;
    do
    {
        const DWORD pid = wcstoul(fd.cFileName + 12, nullptr, 10);   /* depois de "dlss5-ponte." */
        if (pid != GetCurrentProcessId())
        {
            const HANDLE pr = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (pr != nullptr) { CloseHandle(pr); continue; }
        }
        _snwprintf(arq, MAX_PATH, L"%ls%ls", tmp, fd.cFileName);
        arq[MAX_PATH - 1] = 0;
        if (DeleteFileW(arq)) ++apagadas;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (apagadas > 0) Log("ponte: %d copia(s) antiga(s) da dll apagada(s) do %%TEMP%%", apagadas);
}

/* C16: a copia herda o espelho. Bloco e eventos nomeados por pid: a copia avisa que ja leu as
   vtables; a dll que sai restaura os hooks, espera o Present sair do espelho, entrega e avisa. */
struct Heranca { uint32_t versao; uint32_t pronto; Fg::Espelho fg; };
static const uint32_t kHeranca = 0xC16;
static void NomeHeranca(wchar_t* out, size_t n, const wchar_t* qual)
{
    _snwprintf(out, n, L"Local\\dlss5-ponte-heranca-%ls.%lu", qual, GetCurrentProcessId());
    out[n - 1] = 0;
}

/* C16: dll que descarrega; true = a copia foi carregada (com ou sem o espelho entregue) */
static bool PassarEspelhoACopia()
{
    wchar_t nome[96];
    NomeHeranca(nome, 96, L"bloco");
    HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Heranca), nome);
    NomeHeranca(nome, 96, L"vtables");
    HANDLE evVt = CreateEventW(nullptr, TRUE, FALSE, nome);
    NomeHeranca(nome, 96, L"solto");
    HANDLE evSolto = CreateEventW(nullptr, TRUE, FALSE, nome);
    Heranca* h = m ? static_cast<Heranca*>(MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Heranca))) : nullptr;
    bool carregou = false;
    if (h && evVt && evSolto)
    {
        h->versao = kHeranca;
        carregou = CarregarCopia();
        const bool viva = carregou && WaitForSingleObject(evVt, 5000) == WAIT_OBJECT_0;
        P.residual = false;
        RestaurarHooks();
        const int64_t t0 = Agora();
        while (P.noEspelho.load() > 0 && Ms(t0, Agora()) < 2000.0) Sleep(1);
        const bool semPresent = P.noEspelho.load() == 0;
        if (viva && semPresent && Fg::Entregar(&h->fg)) h->pronto = 1;
        SetEvent(evSolto);
        if (h->pronto == 1) Log("ponte: espelho passado a copia da dll; a imagem segue sem parar");
        else Log("ponte: espelho nao passou a copia (copia=%d avisou=%d present fora=%d); a imagem para ate o proximo Alt+Enter",
                 carregou ? 1 : 0, viva ? 1 : 0, semPresent ? 1 : 0);
    }
    else Log("ponte: bloco da heranca nao criado (erro %lu); a copia assume sem o espelho", GetLastError());
    if (h) UnmapViewOfFile(h);
    FecharHandle(m); FecharHandle(evVt); FecharHandle(evSolto);
    return carregou;
}

/* C16: copia carregada por PassarEspelhoACopia? Espera a outra soltar os hooks (senao os
   "originais" seriam os hooks dela) e herda o espelho. false = nao da para enganchar com seguranca. */
static bool AguardarHeranca()
{
    wchar_t nome[96];
    NomeHeranca(nome, 96, L"bloco");
    HANDLE m = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, nome);
    if (m == nullptr) return true;
    NomeHeranca(nome, 96, L"vtables");
    HANDLE evVt = OpenEventW(EVENT_MODIFY_STATE, FALSE, nome);
    NomeHeranca(nome, 96, L"solto");
    HANDLE evSolto = OpenEventW(SYNCHRONIZE, FALSE, nome);
    Heranca* h = static_cast<Heranca*>(MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Heranca)));
    bool ok = false;   /* bloco existe: a outra ainda pode estar com os hooks */
    if (h && evVt && evSolto && h->versao == kHeranca)
    {
        SetEvent(evVt);
        ok = WaitForSingleObject(evSolto, 10000) == WAIT_OBJECT_0;
        if (!ok) Log("ponte: a dll anterior nao soltou os hooks em 10 s; esta copia nao engancha");
        else if (h->pronto == 1) { h->pronto = 2; Fg::Iniciar(&Log, &EstouroRegistrarFg); Fg::Herdar(h->fg); }
        else Log("ponte: a dll anterior nao entregou o espelho");
    }
    else Log("ponte: bloco da heranca ilegivel; esta copia nao engancha");
    if (h) UnmapViewOfFile(h);
    FecharHandle(m); FecharHandle(evVt); FecharHandle(evSolto);
    return ok;
}

/* C13: descarga com o jogo desenhando nos proxies do FG. Sem o espelho a imagem para ate o
   jogo recriar os backbuffers; entao a DLL fica so com o Present espelhando ate o ResizeBuffers
   do jogo, ou ate o app abrir o pipe de novo (a copia assume, ver o fim da ThreadPonte). */
static void EspelhoDaDescarga()
{
    Sleep(120);     /* ExecuteCommandLists em voo sai do NGX/FSR (o hook ja passa direto) */
    Sr::Parar();
    Ngx::Parar();
    Ponte::Colheita::Parar();   /* X-FG-6: depois dos dois hooks (nenhum Publicar em voo) */
    Ponte::PipeUp::Fechar();    /* X-FG-6 */
    Tela::Parar();  /* o contador some com a ponte */
    Fg::Largar();
    if (Fg::Espelhando())
    {
        FecharHandle(P.pipe);   /* a instancia nossa nao conta como pipe novo */
        P.foiResidual = true;
        P.residual = true;
        Log("ponte: descarga adiada - o jogo desenha nas texturas do FG; espelhando ate o proximo ResizeBuffers");
        const int64_t t0 = Agora();
        bool pipeVisto = false;
        for (;;)
        {
            if (!Fg::Espelhando()) { Log("ponte: o ResizeBuffers do jogo encerrou o espelho em %.1f s", Ms(t0, Agora()) / 1000.0); break; }
            if (PipeDoAppEsperando())
            {
                if (!pipeVisto) Log("ponte: o app abriu o pipe de novo; esperando a injecao para encerrar o espelho");
                ULONG n = ContagemDoModulo();
                for (int i = 0; i < 150 && n == 1; ++i) { Sleep(20); n = ContagemDoModulo(); }   /* o LoadLibraryW do injetor, logo depois */
                /* C21: injetor lento (mais de 3 s): segue espelhando e espera de novo, sem parar a imagem */
                if (n == 1)
                {
                    if (!pipeVisto) Log("ponte: a injecao passou de 3 s; o espelho continua ate ela chegar ou o ResizeBuffers do jogo");
                    pipeVisto = true;
                    continue;
                }
                if (n > 1) Log("ponte: o app injetou de novo durante o espelho; uma copia da dll assume a sessao nova");
                /* C18: sem a contagem nao da para saber se o LoadLibraryW caiu aqui: a copia assume e
                   este modulo fica carregado, sem hooks nem thread (descarregar poderia derrubar o jogo) */
                if (n == 0) Log("ponte: contagem de carga do modulo ilegivel (estrutura interna do Windows); reserva: uma copia da dll assume a sessao nova e este modulo fica carregado, inerte");
                if (n != 1)
                {
                    P.reinjecao = (n > 1) ? 1 : 2;
                    P.copiaCarregada = PassarEspelhoACopia();   /* C16 */
                }
                break;
            }
            Sleep(100);
        }
        P.residual = false;
    }
    RestaurarHooks();
    Sleep(120);     /* Present em voo sai do Espelhar */
}

static DWORD WINAPI ThreadPonte(void*)
{
    LogAbrir();
    Log("ponte: injetada pid=%lu (dlss5-ponte64.dll, protocolo v%u)", GetCurrentProcessId(), kVersao);

    if (ContagemDoModulo() == 0)   /* C18: aviso cedo */
        Log("ponte: aviso - contagem de carga do modulo ilegivel neste Windows; religar o Nativo durante o espelho usa a reserva");
    LimparCopiasAntigas();   /* C17 */

    DescobrirVtables();
    if (AguardarHeranca()) InstalarHooks();   /* C16: a copia so engancha depois de a anterior soltar */
    if (!P.hooksInstalados) { Desligar("sem Present para enganchar"); }

    Ponte::PipeUp::Iniciar(&Log);     /* X-FG-6: antes dos dois modulos que o usam */
    Ponte::Colheita::Iniciar(&Log);   /* X-FG-6 */
    Ngx::Iniciar(&Log);   /* G1: engancha o _nvngx.dll se ja estiver no processo; senao Servico() insiste */
    Sr::Iniciar(&Log);    /* Q2: engancha FSR/XeSS se ja estiverem no processo; senao Servico() insiste */
    Fg::Iniciar(&Log, &EstouroRegistrarFg);    /* Q4: frame generation; assume a apresentacao no 1o ResizeBuffers com fg ligado. J4: registrador de estouro da thread de ritmo */
    Fg::Originais(P.origGetBuffer, P.origGetCurBB);   /* trampolins reais que o FG usa por fora dos hooks */
    Tela::Iniciar(&Log);  /* C2/E8: contador de fps e modo de exibicao */

    if (P.fase.load() == FASE_CONECTANDO)
    {
        if (ModoSonda())
        {
            P.sonda = true;
            P.fase = FASE_ATIVA;
            Log("ponte: modo SONDA (sem host, sem build): so os hooks NGX logam; encerra por %%TEMP%%\\dlss5-ponte.%lu.parar", GetCurrentProcessId());
        }
        else if (ConectarHost())
        {
            P.fase = FASE_ATIVA;   /* a partir daqui o Present hook processa */
        }
    }

    /* laco de servico: build sob demanda, ate Desligar/descarregar. */
    while (P.fase.load() == FASE_ATIVA)
    {
        HANDLE hs[2] = { P.evBuild, P.evStop };
        const DWORD r = WaitForMultipleObjects(2, hs, FALSE, 250);
        if (P.fase.load() != FASE_ATIVA) break;
        try   /* C3: excecao no laco nao pode matar o processo do jogo (std::terminate) */
        {
            /* B14 (H6): o build vem primeiro, assim que o evBuild do Present acorda a espera, sem
               passar pelos servicos. Nao depende de r: um pedido que chegou durante os servicos
               da volta anterior (evento ja consumido pela espera seguinte) tambem vale. */
            /* A derrubada pedida pela thread do jogo tem de ser completada mesmo que nenhum
               Build venha depois (o jogo pode ter parado de apresentar no meio da troca):
               senao o device e as texturas do conjunto velho ficariam vivos e ninguem
               remontaria. O FazerBuild tambem chama, e o exchange torna as duas idempotentes. */
            if (!P.sonda && P.derrubarPedido.load(std::memory_order_acquire))
            {
                PtGuarda gd;
                if (PT_GUARDA_ARMAR(gd)) FicarInerte("completar derrubada", gd.code, gd.addr);
                else { CompletarDerrubada(); PtGuardaDesarmar(&gd); }
                if (P.fase.load() != FASE_ATIVA) break;
            }
            if (!P.sonda && P.pedidoBuild.exchange(false) && !P.built.load())
            {
                /* I10: O CAMINHO DE REFAZER O BUILD, guardado. O `try` deste laco pega
                   excecao C++ e nao violacao de acesso, e foi daqui que saiu uma das duas
                   quedas do RE2 (D12CriarCompartilhada, com o device do conjunto derrubado
                   pela thread do jogo no meio da montagem). Estourando, a DLL fica inerte e
                   a FASE_MORTA faz o `break` logo abaixo: a thread segue para a descarga
                   normal, que restaura os hooks no momento seguro dela. */
                PtGuarda g;
                if (PT_GUARDA_ARMAR(g)) FicarInerte("Build (thread da ponte)", g.code, g.addr);
                else
                {
                    const bool feito = FazerBuild();
                    PtGuardaDesarmar(&g);
                    /* O PEDIDO VOLTA PARA A FILA quando o Build foi ADIADO pela derrubada que
                       ainda nao completou (quadro em voo). Sem isto o `exchange` acima teria
                       consumido o unico pedido: a derrubada completaria na volta seguinte e
                       ninguem mais mandaria montar - Nativo morto de pe, sem remontar. Nao
                       vale para as outras falhas do FazerBuild: elas chamam Desligar, a fase
                       muda e o laco sai logo abaixo. */
                    if (!feito && P.derrubarPedido.load(std::memory_order_acquire) && P.fase.load() == FASE_ATIVA)
                    { P.pedidoBuild = true; SetEvent(P.evBuild); }
                }
                if (P.fase.load() != FASE_ATIVA) break;
            }
            /* J4 (18/09/2026): os quatro servicos sob a mesma guarda do I10 (X-CRASH-REQUIEM
               9.6 os tinha deixado de fora). Estourando, a DLL fica inerte e a FASE_MORTA tira
               esta thread do laco pela saida normal dela. */
            {
                PtGuarda gs;
                if (PT_GUARDA_ARMAR(gs)) FicarInerte("Servico (thread da ponte)", gs.code, gs.addr);
                else
                {
                    Ngx::Servico();   /* G1: _nvngx.dll pode aparecer tarde; arquivo de controle */
                    Sr::Servico();    /* Q2: os modulos de FSR/XeSS podem aparecer tarde; sessao 0x200 */
                    Fg::Servico(P.api.load() == API_D3D11);    /* Q4: le a chave fg; P1: publica o estado do FG */
                    Tela::Servico();  /* C2/E8: fps_tela e pedido de exibicao do .ngx */
                    Ponte::Colheita::Servico();   /* X-FG-6: o cemiterio dos conjuntos aposentados */
                    /* J4: com o FG dono da apresentacao o ProcessarQuadro nao roda e a vigia do R1
                       congela (X-FG-3 5.1); o `movendo` do .saude passa a vir da sonda do proprio
                       FG, que ve todo quadro. So na transicao, como a vigia faz. */
                    if (Fg::DonoDaApresentacao())
                    {
                        const bool parado = Fg::ImagemParada();
                        if (parado != P.vigia.parado) { P.vigia.parado = parado; VigiaPublicar(P.vigia.direta, 0.0); }
                    }
                    PtGuardaDesarmar(&gs);
                }
                if (P.fase.load() != FASE_ATIVA) break;
            }
            if (P.sonda) { if (PararPedido()) Desligar("parar pedido (sonda)"); continue; }
        }
        catch (...) { Log("ponte: excecao no laco de servico"); Desligar("excecao no laco de servico"); }
        (void)r;
    }

    /* Desligar foi chamado (ou nunca ativou). Espera o dreno terminar
       (2 Presents pelo trampolim original), com teto para o caso de o jogo
       ter parado de apresentar. */
    if (WaitForSingleObject(P.evUnload, 2000) != WAIT_OBJECT_0)
        RestaurarHooks();

    /* C13: hooks ainda no lugar = o jogo desenha nos proxies do FG; espelha ate ele recriar */
    const bool espelhou = !P.hooksRestaurados.load();
    if (espelhou) EspelhoDaDescarga();

    Fg::Parar();    /* Q4: para a thread de ritmo e solta os proxies/FFX antes de tudo (usa a fila do jogo) */
    if (!espelhou)  /* C13: no espelho estes ja pararam */
    {
        Sr::Parar();    /* Q2: primeiro o degrau 2 (ele depende da fila e das fences); depois o NGX */
        Ngx::Parar();   /* G1: desliga os hooks NGX e espera as chamadas em voo sairem */
        Ponte::Colheita::Parar();   /* X-FG-6: depois dos dois hooks (nenhum Publicar em voo) */
        Ponte::PipeUp::Fechar();    /* X-FG-6: a conexao do .up, seja de quem for */
        Tela::Parar();  /* E8: solta o hook de mensagens da janela antes de descarregar; C2: recursos */
    }

    /* Dar tempo de qualquer Present em voo sair do nosso codigo antes de
       liberar o modulo. */
    Sleep(120);

    /* Libera os recursos do D3D (a GPU ja terminou o nosso trabalho). */
    if (P.api.load() == API_D3D11) D11Liberar();
    else if (P.api.load() == API_D3D12) D12Liberar();
    FecharHandle(P.pipe);
    if (g_compilador != nullptr) { FreeLibrary(g_compilador); g_compilador = nullptr; }
    FecharHandle(P.evBuild); FecharHandle(P.evStop); FecharHandle(P.evUnload);
    { std::lock_guard<std::mutex> l(P.muPipe); FecharHandle(P.evPipe); }   /* B16 */

    /* C13: o LoadLibraryW do app caiu neste modulo durante o espelho (sem DllMain, sem thread nova) */
    if (P.foiResidual && P.reinjecao == 0)
    {
        const ULONG n = ContagemDoModulo();
        if (n > 1) { P.reinjecao = 1; Log("ponte: o app injetou de novo durante o espelho; uma copia da dll assume a sessao nova"); }
        else if (n == 0 && PipeDoAppEsperando())   /* C18 */
        { P.reinjecao = 2; Log("ponte: contagem de carga do modulo ilegivel e o app esperando; reserva: uma copia da dll assume e este modulo fica carregado, inerte"); }
    }
    Log("ponte: descarregada");
    LogFechar();
    if (P.reinjecao != 0 && !P.copiaCarregada) P.copiaCarregada = CarregarCopia();

    HANDLE th = P.thread;
    if (th != nullptr) CloseHandle(th);
    if (P.reinjecao == 2 && P.copiaCarregada) ExitThread(0);   /* C18: o modulo fica, inerte */
    if (P.reinjecao == 1 && P.copiaCarregada)
        FreeLibrary(P.hModule);   /* a referencia do injetor; a nossa sai no FreeLibraryAndExitThread */
    FreeLibraryAndExitThread(P.hModule, 0);   /* solta o modulo e sai atomico */
    return 0;
}

} // namespace anonimo

/* ================================================================== */
/* J5. O balcao (ponte_dados.h): quem engancha o upscaler deixa aqui   */
/* depth e MV; o Present da fase 2 os copia (PegarDadosDoQuadro).      */
/* ================================================================== */
namespace Ponte { namespace Dados {

bool Quer()
{
    return P.built.load() && P.hostEntendeD && P.fase.load() == FASE_ATIVA && !P.inerte.load(std::memory_order_relaxed);
}

/* Thread do jogo, no dispatch do upscaler. Uma trava sem disputa e um par de AddRef/Release
   (D3D12; em D3D11 o runtime segura o recurso pela copia). Sem sessao da fase 2, nada. */
void Publicar(const Pacote& p)
{
    if (!Quer()) return;
    try
    {
        ID3D12Resource* dp = p.depth12; ID3D12Resource* mv = p.mv12;
        if (dp) dp->AddRef();
        if (mv) mv->AddRef();
        ID3D11Texture2D* d11 = p.depth11; ID3D11Texture2D* m11 = p.mv11;
        if (d11) d11->AddRef();
        if (m11) m11->AddRef();
        ID3D12Resource* velhoD = nullptr; ID3D12Resource* velhoM = nullptr;
        ID3D11Texture2D* velhoD11 = nullptr; ID3D11Texture2D* velhoM11 = nullptr;
        {
            std::lock_guard<std::mutex> g(P.muDados);
            velhoD = P.dados.depth12; velhoM = P.dados.mv12; velhoD11 = P.dados.depth11; velhoM11 = P.dados.mv11;
            P.dados = p; P.dados.depth12 = dp; P.dados.mv12 = mv; P.dados.depth11 = d11; P.dados.mv11 = m11;
            ++P.dadosSeq; P.dadosQpc = Agora();
        }
        Soltar(velhoD); Soltar(velhoM); Soltar(velhoD11); Soltar(velhoM11);   /* fora da trava: Release pode ser a ultima */
    }
    catch (...) {}
}

void Limpar()
{
    try
    {
        ID3D12Resource* velhoD = nullptr; ID3D12Resource* velhoM = nullptr;
        ID3D11Texture2D* velhoD11 = nullptr; ID3D11Texture2D* velhoM11 = nullptr;
        {
            std::lock_guard<std::mutex> g(P.muDados);
            velhoD = P.dados.depth12; velhoM = P.dados.mv12; velhoD11 = P.dados.depth11; velhoM11 = P.dados.mv11;
            P.dados = Pacote();
        }
        Soltar(velhoD); Soltar(velhoM); Soltar(velhoD11); Soltar(velhoM11);
    }
    catch (...) {}
}

} } // namespace Ponte::Dados

/* ------------------------------------------------------------------ */
/* DllMain: NADA de trabalho aqui. So sobe a thread.                    */
/* ------------------------------------------------------------------ */
BOOL WINAPI DllMain(HINSTANCE inst, DWORD motivo, LPVOID)
{
    if (motivo == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(inst);
        P.hModule = inst;
        P.evBuild  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        P.evStop   = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
        P.evUnload = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
        P.evPipe   = CreateEventW(nullptr, TRUE,  FALSE, nullptr);   /* B16 */
        P.thread = CreateThread(nullptr, 0, ThreadPonte, nullptr, 0, nullptr);
        if (P.thread == nullptr) return FALSE;
    }
    /* PROCESS_DETACH: nao fazemos nada -- a autodescarga e por
       FreeLibraryAndExitThread na propria thread; a DLL nunca e descarregada
       de fora enquanto a ponte estiver viva. */
    return TRUE;
}
