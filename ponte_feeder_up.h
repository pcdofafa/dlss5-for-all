/* ------------------------------------------------------------------
   ponte_feeder_up.h - o SEGUNDO servidor de pipe da ponte: a sessao
   "upstream" (fase 3, G2, 15/09/2026).

   O QUE E. Na fase 2 o app recebe o quadro FINAL do jogo (do tamanho do
   backbuffer dele), processa e devolve. Na fase 3 a DLL do G1 engancha o
   `EvaluateFeature` do DLSS SR dentro do jogo e nos manda a COR PEQUENA -
   a cor na resolucao INTERNA, antes do super resolution -, junto com os
   vetores de movimento e a profundidade daquele mesmo evaluate. O app roda
   SO A REDE nessa cor pequena e devolve no mesmo tamanho e no mesmo
   formato; a DLL troca o recurso `Color` do parametro pelo nosso Output e
   deixa o DLSS do jogo fazer o super resolution em cima da imagem
   melhorada. A rede passa a rodar ANTES do upscale, e nao depois.

   POR QUE UM PIPE PROPRIO, E NAO UM SEGUNDO Build NA MESMA SESSAO. Foi a
   decisao do G1 (relatorios\G1-sonda.md, secao 5) e ela esta certa: o
   `FeedFrameMsg` v9 nao tem campo para dizer "este quadro e do conjunto A
   ou do conjunto B", e um segundo 'B' na mesma sessao faz o
   `PonteHost::TratarBuild` APOSENTAR as texturas do primeiro. Dois pipes
   deixam a fase 2 exatamente como estava: este arquivo nao toca em
   `ponte_feeder_host.h`, so o inclui para reaproveitar o protocolo v9 e as
   regras de formato do F0.

   O CONTRATO (o que este host aceita):
     pipe     \\.\pipe\dlss5-feed.<pid do jogo>.up  (o `.up` e a unica
              diferenca de nome; o hello e o v9 de sempre)
     Build    `FeedBuild` v9 com `client_flags & FEED_BUILD_UPSTREAM`
              (0x100). `width/height` sao o tamanho INTERNO da feature,
              nao o do backbuffer. `output_fmt` tem de ser o mesmo da
              `color_fmt` (regra do F0: a cor volta na faixa em que veio).
              `FEED_BUILD_ASYNC_HOME` e RECUSADO: aqui o modo e sincrono e
              ponto - a DLL esta com uma command list aberta esperando.
     'X'      `FeedExtraMsg` (40 bytes, pack 1) ANTES do 'F' do mesmo
              quadro: o que o v9 nao sabe levar (escala de MV, exposicao,
              dt, flags). Guardado por `n` e entregue junto com o quadro.
     'F'      `FeedFrameMsg` v9 de sempre (n, reset, jitter): e ele que
              fecha o quadro. O jitter vem redundante nos dois de
              proposito, para um host antigo nao quebrar.
     volta    o Output no MESMO tamanho e formato da Color, e o
              `out_fence` sinalizado NO MESMO QUADRO (sincrono).

   O ORCAMENTO. O app tem 4 ms por avaliacao. Quem conta o tempo e o lado
   do app (`FonteUpstream`, em fonte_ponte.h); aqui em baixo existe so o
   caminho de escape: `Entregar(nullptr, n)` copia Color->Output e sinaliza,
   e o jogo recebe a cor INTOCADA naquele quadro. Do lado da DLL ha um vigia
   de 50 ms (G1) que sinaliza por CPU se nem isso chegar: o jogo nunca fica
   parado esperando o app.

   O QUE ESTE ARQUIVO NAO FAZ: nao cria texturas (o `FEED_BUILD_HOST_CREATES`
   e recusado - a DLL do G1 sempre cria, ela tem o device do jogo), nao
   atende cliente GL/Vulkan, nao negocia super resolution (`target_*` e
   ignorado: aqui nunca ha upscale) e nao tem painel.
   ------------------------------------------------------------------ */
#pragma once

/* ponte_feeder_host.h TEM de vir antes: o protocolo v9 (FeedHello, FeedBuild,
   FeedBuildAck, FeedFrameMsg), as regras de formato do F0 (FormatoSaidaPara,
   FormatoTipado, MesmaFamilia) e o `Log` do app moram la. Duplicar tudo isso
   aqui so criaria duas verdades para a mesma coisa. */
#include "ponte_feeder_host.h"

namespace PonteUp {

using PonteHost::FeedHello;
using PonteHost::FeedHelloAck;
using PonteHost::FeedBuild;
using PonteHost::FeedBuildAck;
using PonteHost::FeedFrameMsg;
using PonteHost::FeedWindowMsg;

/* O bit novo do `client_flags`. 1/2/4 ja sao do v9 (HOST_CREATES,
   OUTPUT_NO_UAV, ASYNC_HOME); 0x100 e o que o G1 escolheu para "esta sessao
   e upstream". Um host que nao o conheca vai tratar o Build como uma sessao
   normal do tamanho interno - por isso o pipe tem nome proprio: ninguem
   chega aqui por engano. */
static const uint32_t kBuildUpstream = 0x100u;

/* A confirmacao, no `flags` do `FeedBuildAck`: "eu entendi o bit 0x100 e vou tratar esta sessao
   como upstream". A DLL do G1 so passa a mandar a mensagem lateral 'X' depois de ver isto, e
   esta certa: um host que ecoasse ok=1 sem entender o upstream receberia 41 bytes por quadro que
   nao sabe ler e dessincronizaria o pipe no primeiro quadro. Vai junto com ok=1, e so ali. */
static const uint32_t kAckUpstream = 0x100u;

/* A mensagem lateral. 40 bytes, pack 1 - a conta do G1 no relatorio diz 36,
   mas os campos que ele mesmo lista sao 8 + 7*4 + 4 = 40; vale a LISTA, que
   e o que os dois lados precisam ler igual. flags: bit 0 = reset, bit 1 =
   havia ExposureTexture na feature, bit 2 = o jogo pediu nitidez ao DLSS. */
#pragma pack(push, 1)
struct FeedExtraMsg
{
    uint64_t n;
    float    jitter_x, jitter_y;
    float    mv_scale_x, mv_scale_y;
    float    pre_exposure, exposure_scale;
    float    frame_dt_ms;
    uint32_t flags;
};
#pragma pack(pop)
static_assert(sizeof(FeedExtraMsg) == 40, "FeedExtraMsg tem 40 bytes (41 com a tag 'X')");

/* Q2 (degrau 2, 16/09/2026): a sessao 0x200. O jogo tem FSR 2/3.1 ou XeSS, nao DLSS; a DLL
   intercepta o dispatch do upscaler e manda cor pequena + MV + depth (+ exposicao em `panel_tex`)
   por ESTE pipe, com `client_flags = FEED_BUILD_SR_REQUEST` (0x200), `target_width/height` = o
   tamanho de SAIDA do jogo e `output_fmt` = o formato do destino do upscaler; `flags_override` =
   DLSS.Feature.Create.Flags propostos; `depth_inverted` -1 = desconhecido (heuristica no app).
   O app cria a feature DLSS SR do NGX (D3D11) em `dlss_sr_app.h`, avalia e devolve o Output no
   tamanho de saida. Por quadro vem tambem 'S' + FeedSrMsg (24 bytes) com o renderSize do
   dispatch (resolucao dinamica) e a nitidez pedida. Formato exato: relatorios\Q2-fio.md. */
static const uint32_t kBuildSr = 0x200u;   /* FEED_BUILD_SR_REQUEST */
static const uint32_t kAckSr   = 0x200u;   /* FEED_ACK_SR: este host entende 'S' e faz o DLSS SR */
#pragma pack(push, 1)
struct FeedSrMsg { uint64_t n; uint32_t render_w, render_h; float sharpness; uint32_t flags; };
#pragma pack(pop)
static_assert(sizeof(FeedSrMsg) == 24, "FeedSrMsg tem 24 bytes (25 com a tag 'S')");
/* [ultima] nativo_sr = 0 | 1 (padrao 1): o main poe aqui se o Build 0x200 e aceito. */
static bool g_upAceitaSr = true;
/* H1/I6: o Build 0x100 (rede antes do DLSS do jogo) e aceito? O main poe aqui o que o modo escolhido pede (UpstreamQuerido). */
static bool g_upAceitaRede = true;

static const uint32_t kExtraReset    = 1u;
static const uint32_t kExtraExpoTex  = 2u;
static const uint32_t kExtraSharpen  = 4u;

/* O que o Build negociou. O app le isto para dizer no painel e no log em que
   resolucao a rede vai rodar. */
struct SessaoUp
{
    DWORD       pidJogo = 0;
    UINT        largura = 0, altura = 0;      /* o tamanho INTERNO da feature */
    DXGI_FORMAT formatoCor = DXGI_FORMAT_UNKNOWN, formatoSaida = DXGI_FORMAT_UNKNOWN;
    bool        hdr = false;
    bool        depthInvertido = false;
    float       mvScaleX = 1.f, mvScaleY = 1.f;
    /* Q2: sessao 0x200 (DLSS SR no app) */
    bool        sr = false;
    UINT        saidaW = 0, saidaH = 0;     /* o tamanho de SAIDA do jogo (Output do slot) */
    int         flagsNgx = 0;               /* DLSS.Feature.Create.Flags propostos pela DLL */
    int         depthInv3 = -1;             /* -1 desconhecido, 0 normal, 1 invertida */
    bool        temExposicao = false;       /* a DLL manda a textura 1x1 de exposicao (panel_tex) */
};

/* Um quadro upstream, ja aberto no device do app. `extra` e o 'X' daquele
   `n` (ou os zeros, quando a DLL nao mandou). */
struct QuadroUp
{
    uint64_t n = 0;
    ID3D11Texture2D* cor = nullptr;
    ID3D11Texture2D* depth = nullptr;   /* pode ser nullptr */
    ID3D11Texture2D* mv = nullptr;      /* pode ser nullptr */
    UINT largura = 0, altura = 0;
    DXGI_FORMAT formato = DXGI_FORMAT_UNKNOWN;
    FeedExtraMsg extra = {};
    bool temExtra = false;
    int64_t qpcChegada = 0;
    /* Q2 */
    bool sr = false;
    ID3D11Texture2D* saida = nullptr;   /* o slot Output no tamanho de saida (so para conferir) */
    ID3D11Texture2D* expo = nullptr;    /* 1x1 R32F, ou nullptr */
    FeedSrMsg srMsg = {};
    bool temSr = false;
};

/* ------------------------------------------------------------------ */
/* Estado (espelho enxuto do Estado do host: so o que o upstream usa)   */
/* ------------------------------------------------------------------ */
struct EstadoUp
{
    /* device do app (fase 2) */
    ID3D11Device*         dev  = nullptr;
    ID3D11DeviceContext*  ctx  = nullptr;
    ID3D11Device1*        dev1 = nullptr;
    ID3D11Device5*        dev5 = nullptr;
    ID3D11DeviceContext4* ctx4 = nullptr;
    bool devPronto = false;
    /* auxiliar das fences */
    ID3D12Device* dev12  = nullptr;
    ID3D12Fence*  f12In  = nullptr;
    ID3D12Fence*  f12Out = nullptr;
    ID3D11Fence*  f11In  = nullptr;
    ID3D11Fence*  f11Out = nullptr;
    HANDLE hIn = nullptr, hOut = nullptr;
    uint64_t jogoIn = 0, jogoOut = 0;
    /* pipe */
    HANDLE pipe = nullptr, evIo = nullptr, evStop = nullptr, evFence = nullptr;
    HANDLE evQuadro = nullptr, thread = nullptr;
    HANDLE hJogo = nullptr, hJogoEspera = nullptr;
    DWORD  pid = 0;
    /* conjunto. A PREMISSA ANTIGA ESTAVA ERRADA, e derrubava o app (18/09/2026):
       este comentario dizia "um rebuild refaz na hora - nao ha quadro do jogo em voo
       aqui, o modo e sincrono". Ha: o `emUso` abaixo E o quadro em voo. O ObterQuadro
       entrega ao laco de render os ponteiros CRUS destas texturas (sem AddRef), solta o
       mutex e o app passa o quadro inteiro usando-os - cria SRV, roda a rede, apresenta -
       ate chamar Entregar. Um Build novo chega na thread do PIPE e, antes, liberava as
       texturas no meio disso: use-after-free, 0xC0000005.
       Agora vale a mesma regra da fase 2 (ver AposentarConjunto em ponte_feeder_host.h):
       cada conjunto tem GERACAO, o rebuild espera o quadro sair de uso e, se nao sair, o
       conjunto vira "velho" e vive ate a entrega daquele quadro. */
    bool valido = false;
    ID3D11Texture2D* tex[PonteHost::SLOTS] = { nullptr, nullptr, nullptr, nullptr };
    uint64_t geracao = 0, geracoes = 0;      /* geracao do conjunto vivo */
    /* o conjunto aposentado, vivo enquanto o app ainda segura um quadro dele */
    ID3D11Texture2D* texVelho[PonteHost::SLOTS] = { nullptr, nullptr, nullptr, nullptr };
    ID3D11Texture2D* texExpoVelho = nullptr;
    bool velhoValido = false;
    uint64_t velhoGeracao = 0;
    UINT w = 0, h = 0;
    DXGI_FORMAT corFmt = DXGI_FORMAT_UNKNOWN, outFmt = DXGI_FORMAT_UNKNOWN;
    SessaoUp sessao;
    bool temSessao = false;
    FeedBuild pendente = {};
    bool temPendente = false;
    /* o 'X' mais recente (a DLL manda um por quadro, logo antes do 'F') */
    FeedExtraMsg extra = {};
    bool temExtra = false;
    /* Q2: o 'S' mais recente e a textura de exposicao */
    FeedSrMsg srMsg = {}; bool temSr = false;
    FeedSrMsg prontoSr = {}; bool prontoTemSr = false;
    ID3D11Texture2D* texExpo = nullptr;
    UINT ow = 0, oh = 0;   /* tamanho do Output (= w,h fora do SR) */
    /* quadro pronto / em uso */
    bool prontoValido = false;
    uint64_t prontoN = 0;
    int64_t  prontoQpc = 0;
    FeedFrameMsg prontoFm = {};
    FeedExtraMsg prontoExtra = {};
    bool prontoTemExtra = false;
    bool emUso = false;
    uint64_t emUsoN = 0;
    uint64_t emUsoGeracao = 0;   /* de qual conjunto e o quadro que o app tem na mao */
    uint64_t ultimoSinal = 0, ultimoRecebido = 0;
    int64_t  qpcUltimoF = 0;     /* I7: quando chegou o ultimo 'F' (a pausa e medida daqui) */
    /* contadores do log */
    uint64_t avaliacoes = 0, atrasos = 0;
    uint64_t recebidos = 0, soltos = 0;   /* 'F' aceitos e quadros soltos sem o app processar */
    double somaRedeMs = 0.0, somaTotalMs = 0.0;
    /* log */
    char motivo[160] = "";
    bool encerradaLogada = false;
    bool avisoCopia = false, avisoFmt = false, avisoTag = false;
    int64_t qpcFreq = 1;
};
static EstadoUp g_up;
static std::mutex g_upMu;
static std::atomic<bool> g_upAtiva{ false };
/* Desligado por decisao do app (orcamento estourado, ou `nativo_upstream=0`):
   o pipe cai e a DLL volta ao caminho da fase 2. */
static std::atomic<bool> g_upDesligado{ false };
/* H4 (17/09/2026): a espera do `ConnectNamedPipe` acabou sem ninguem do outro lado - o jogo nao
   chamou upscaler nenhum em `kUpConexaoMs`. Antes isso so aparecia no log: o app caia para o
   Nativo de sempre em silencio e a pessoa ficava sem saber por que a imagem nao mudou. O laco do
   main le esta bandeira UMA vez e poe o cartaz; a thread do pipe nao toca na tela. */
static std::atomic<bool> g_upSemPedido{ false };
/* I7 (18/09/2026): a sessao upstream CAIU DE VERDADE - por decisao do app (orcamento, o DLSS SR
   falhando) ou porque o pipe fechou com uma sessao negociada. E diferente da pausa (o jogador
   desligou o upscaling: o pipe fica, os quadros param) e do jogo que nunca pediu. O laco do
   main le uma vez e registra `upstream_falhas` no perfil do jogo (cadeia A8/A9). */
static std::atomic<bool> g_upFalhou{ false };
static char g_upFalhaMotivo[160] = "";

static bool PipeUpAberto() { return g_up.pipe != nullptr && g_up.pipe != INVALID_HANDLE_VALUE; }
/* A frequencia do contador e DAQUI, e nao do `PonteHost::Ms`: aquela vive no estado da fase 2 e
   so nasce no Preparar dela. Enquanto as duas sessoes subirem juntas dava na mesma; no dia em
   que nao subirem, os milissegundos do log do upstream sairiam multiplicados pela frequencia do
   processador - um numero absurdo que ninguem saberia de onde veio. */
static double MsUp(int64_t de, int64_t ate) { return 1000.0 * double(ate - de) / double(g_up.qpcFreq); }

static void MotivoUp(const char* fmt, ...)
{
    if (strcmp(fmt, "Parar") == 0 && g_up.motivo[0] != '\0') return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_up.motivo, sizeof(g_up.motivo), fmt, ap);
    va_end(ap);
}
static void LogUpEncerrada(const char* motivo)
{
    if (g_up.encerradaLogada) return;
    g_up.encerradaLogada = true;
    Log("upstream: desligado (%s)", motivo);
}
/* Signal pela CPU: solta o jogo de qualquer thread. Com g_upMu. */
static void SinalCpuUp(uint64_t v)
{
    if (g_up.f12Out == nullptr) return;
    if (v <= g_up.ultimoSinal) return;
    g_up.f12Out->Signal(v);
    g_up.ultimoSinal = v;
}

/* ------------------------------------------------------------------ */
/* Pipe: mesma transferencia overlapped do host, sobre o estado daqui   */
/* ------------------------------------------------------------------ */
static PonteHost::Xfer TransferirUp(void* buf, DWORD len, bool escrever, DWORD timeoutMs)
{
    BYTE* p = static_cast<BYTE*>(buf);
    while (len > 0)
    {
        OVERLAPPED ov = {};
        ov.hEvent = g_up.evIo;
        ResetEvent(g_up.evIo);
        const BOOL comecou = escrever ? WriteFile(g_up.pipe, p, len, nullptr, &ov)
                                      : ReadFile(g_up.pipe, p, len, nullptr, &ov);
        if (!comecou)
        {
            const DWORD e = GetLastError();
            if (e == ERROR_BROKEN_PIPE || e == ERROR_NO_DATA || e == ERROR_PIPE_NOT_CONNECTED) return PonteHost::XFER_FECHADO;
            if (e != ERROR_IO_PENDING) return PonteHost::XFER_ERRO;
        }
        HANDLE hs[3] = { g_up.evIo, g_up.evStop, g_up.hJogoEspera };
        const DWORD n = g_up.hJogoEspera != nullptr ? 3 : 2;
        const DWORD r = WaitForMultipleObjects(n, hs, FALSE, timeoutMs);
        const DWORD erroWait = (r == WAIT_FAILED) ? GetLastError() : 0;
        DWORD moveu = 0;
        if (r != WAIT_OBJECT_0)
        {
            CancelIoEx(g_up.pipe, &ov);
            GetOverlappedResult(g_up.pipe, &ov, &moveu, TRUE);
            if (r == WAIT_OBJECT_0 + 1) return PonteHost::XFER_PARAR;
            if (r == WAIT_OBJECT_0 + 2) return PonteHost::XFER_JOGO_MORREU;
            if (r == WAIT_FAILED) { SetLastError(erroWait); return PonteHost::XFER_ERRO; }
            return PonteHost::XFER_TIMEOUT;
        }
        if (!GetOverlappedResult(g_up.pipe, &ov, &moveu, FALSE) || moveu == 0)
        {
            const DWORD e = GetLastError();
            return (moveu == 0 || e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED) ? PonteHost::XFER_FECHADO : PonteHost::XFER_ERRO;
        }
        p   += moveu;
        len -= moveu;
    }
    return PonteHost::XFER_OK;
}
static PonteHost::Xfer LerUp(void* b, DWORD n, DWORD t)            { return TransferirUp(b, n, false, t); }
static PonteHost::Xfer EscreverUp(const void* b, DWORD n, DWORD t) { return TransferirUp(const_cast<void*>(b), n, true, t); }

static void MotivoDeXferUp(PonteHost::Xfer x, const char* onde)
{
    switch (x)
    {
    case PonteHost::XFER_FECHADO:     MotivoUp("pipe fechado"); break;
    case PonteHost::XFER_JOGO_MORREU: MotivoUp("pipe fechado"); break;
    case PonteHost::XFER_PARAR:       MotivoUp("Parar"); break;
    case PonteHost::XFER_TIMEOUT:     MotivoUp("timeout no pipe (%s)", onde); break;
    default:                          MotivoUp("erro de pipe (%s) win32=%lu", onde, GetLastError()); break;
    }
}

/* ------------------------------------------------------------------ */
/* Texturas: a DLL cria, nos so abrimos                                 */
/* ------------------------------------------------------------------ */
static ID3D11Texture2D* AbrirDoJogoUp(int slot, uint64_t valorNoJogo)
{
    if (valorNoJogo == 0) return nullptr;
    HANDLE local = nullptr;
    if (!DuplicateHandle(g_up.hJogo, PonteHost::ComoHandle(valorNoJogo), GetCurrentProcess(), &local, 0, FALSE, DUPLICATE_SAME_ACCESS))
    { Log("upstream: DuplicateHandle(%s) do jogo falhou %lu", PonteHost::NomeSlot(slot), GetLastError()); return nullptr; }
    ID3D11Texture2D* t = nullptr;
    const HRESULT hr = g_up.dev1->OpenSharedResource1(local, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t));
    CloseHandle(local);
    if (FAILED(hr)) { Log("upstream: OpenSharedResource1(%s) falhou 0x%08X", PonteHost::NomeSlot(slot), (unsigned)hr); return nullptr; }
    return t;
}
static bool ConfereDescUp(int slot, ID3D11Texture2D* t, UINT w, UINT h, DXGI_FORMAT fmt)
{
    D3D11_TEXTURE2D_DESC d = {};
    t->GetDesc(&d);
    if (d.Width == w && d.Height == h && d.Format == fmt) return true;
    Log("upstream: a textura %s e %ux%u fmt=%u, nao %ux%u fmt=%u", PonteHost::NomeSlot(slot),
        d.Width, d.Height, (unsigned)d.Format, w, h, (unsigned)fmt);
    return false;
}

static void LiberarConjuntoUp()
{
    for (int i = 0; i < PonteHost::SLOTS; ++i) PonteHost::Soltar(g_up.tex[i]);
    PonteHost::Soltar(g_up.texExpo);
    g_up.valido = false;
    g_up.prontoValido = false;
}

static void LiberarVelhoUp()
{
    for (int i = 0; i < PonteHost::SLOTS; ++i) PonteHost::Soltar(g_up.texVelho[i]);
    PonteHost::Soltar(g_up.texExpoVelho);
    g_up.velhoValido = false;
    g_up.velhoGeracao = 0;
}

/* Espera o app soltar o quadro que tem na mao. FORA do mutex (quem o solta e o
   Entregar, que precisa dele) e com limite: se o laco de render travou, e melhor
   aposentar o conjunto - que continua vivo - do que ficar preso aqui. Mesmo desenho do
   EsperarSemUso da fase 2. Chamado da thread do pipe, sem g_upMu na mao. */
static void EsperarSemUsoUp(DWORD ms)
{
    const ULONGLONG limite = GetTickCount64() + ms;
    for (;;)
    {
        { std::lock_guard<std::mutex> l(g_upMu); if (!g_up.emUso) return; }
        if (GetTickCount64() >= limite) return;
        if (g_up.evStop != nullptr) { if (WaitForSingleObject(g_up.evStop, 5) == WAIT_OBJECT_0) return; }
        else Sleep(5);
    }
}

/* Um rebuild aposenta o conjunto atual. Se o app ainda segura um quadro dele (o
   EsperarSemUsoUp estourou), o conjunto vira "velho" e vive ate o Entregar desse quadro
   (ou o Parar): nunca um ponteiro pendurado nas maos do app. Com g_upMu. */
static void AposentarConjuntoUp()
{
    if (g_up.velhoValido)
    {
        /* Dois rebuilds sem o app entregar no meio: o mais velho vai embora agora. O app
           so pode estar segurando UM quadro, e se for o deste conjunto avisamos - e o
           unico caso em que ainda se solta algo em uso, e ele exige o laco parado por
           dois rebuilds seguidos. */
        if (g_up.emUso && g_up.emUsoGeracao == g_up.velhoGeracao)
            Log("upstream: dois rebuilds com o mesmo quadro %llu em uso; soltando o conjunto mais velho",
                (unsigned long long)g_up.emUsoN);
        LiberarVelhoUp();
    }
    if (g_up.valido && g_up.emUso && g_up.emUsoGeracao == g_up.geracao)
    {
        Log("upstream: rebuild com o quadro %llu ainda em uso pelo app; o conjunto velho fica vivo ate a entrega",
            (unsigned long long)g_up.emUsoN);
        for (int i = 0; i < PonteHost::SLOTS; ++i) { g_up.texVelho[i] = g_up.tex[i]; g_up.tex[i] = nullptr; }
        g_up.texExpoVelho = g_up.texExpo; g_up.texExpo = nullptr;
        g_up.velhoValido  = true;
        g_up.velhoGeracao = g_up.geracao;
        g_up.valido = false;
        g_up.prontoValido = false;
    }
    else LiberarConjuntoUp();
    g_up.geracao = 0;
}

/* Fase 2 de um Build. Com g_upMu e com devPronto. */
static bool MontarConjuntoUp(const FeedBuild& b)
{
    /* Aposenta, e nao libera: quem chega aqui pelo Build pendente (o device ficou pronto
       depois do Build) nao passou pelo TratarBuildUp, e mesmo ali a regra tem de valer em
       um lugar so. Sem quadro em uso, aposentar E liberar. */
    AposentarConjuntoUp();
    g_up.geracao = ++g_up.geracoes;
    const bool sr = (b.client_flags & kBuildSr) != 0;   /* Q2 */
    g_up.w = b.width; g_up.h = b.height;
    g_up.ow = sr && b.target_width && b.target_height ? b.target_width : b.width;
    g_up.oh = sr && b.target_width && b.target_height ? b.target_height : b.height;
    g_up.corFmt = static_cast<DXGI_FORMAT>(b.color_fmt);
    g_up.outFmt = static_cast<DXGI_FORMAT>(b.output_fmt);

    g_up.tex[PonteHost::SLOT_COLOR]  = AbrirDoJogoUp(PonteHost::SLOT_COLOR,  b.tex[PonteHost::SLOT_COLOR]);
    g_up.tex[PonteHost::SLOT_OUTPUT] = AbrirDoJogoUp(PonteHost::SLOT_OUTPUT, b.tex[PonteHost::SLOT_OUTPUT]);
    g_up.tex[PonteHost::SLOT_DEPTH]  = AbrirDoJogoUp(PonteHost::SLOT_DEPTH,  b.tex[PonteHost::SLOT_DEPTH]);
    g_up.tex[PonteHost::SLOT_MV]     = AbrirDoJogoUp(PonteHost::SLOT_MV,     b.tex[PonteHost::SLOT_MV]);
    /* Q2: a exposicao 1x1 vem no campo `panel_tex` (nao ha painel nesta sessao) */
    g_up.texExpo = (sr && b.panel_tex != 0) ? AbrirDoJogoUp(PonteHost::SLOT_OUTPUT, b.panel_tex) : nullptr;

    bool ok = g_up.tex[PonteHost::SLOT_COLOR] != nullptr && g_up.tex[PonteHost::SLOT_OUTPUT] != nullptr;
    if (ok) ok = ConfereDescUp(PonteHost::SLOT_COLOR,  g_up.tex[PonteHost::SLOT_COLOR],  g_up.w, g_up.h, g_up.corFmt) &&
                 ConfereDescUp(PonteHost::SLOT_OUTPUT, g_up.tex[PonteHost::SLOT_OUTPUT], g_up.ow, g_up.oh, g_up.outFmt);
    /* MV e Depth sao guias: se o formato nao for o que o contrato pede
       (R16G16_FLOAT e R32_FLOAT, os mesmos que a sonda do G1 mediu), viram
       nullptr e a rede roda so com a cor - nunca derrubam a sessao. No SR a
       MV pode vir no tamanho de SAIDA (MV em resolucao de tela, sem MVLowRes). */
    if (g_up.tex[PonteHost::SLOT_DEPTH] != nullptr &&
        !ConfereDescUp(PonteHost::SLOT_DEPTH, g_up.tex[PonteHost::SLOT_DEPTH], g_up.w, g_up.h, DXGI_FORMAT_R32_FLOAT))
        PonteHost::Soltar(g_up.tex[PonteHost::SLOT_DEPTH]);
    if (g_up.tex[PonteHost::SLOT_MV] != nullptr)
    {
        D3D11_TEXTURE2D_DESC md = {}; g_up.tex[PonteHost::SLOT_MV]->GetDesc(&md);
        const bool interno = md.Width == g_up.w && md.Height == g_up.h, tela = sr && md.Width == g_up.ow && md.Height == g_up.oh;
        if (md.Format != DXGI_FORMAT_R16G16_FLOAT || !(interno || tela))
        { ConfereDescUp(PonteHost::SLOT_MV, g_up.tex[PonteHost::SLOT_MV], g_up.w, g_up.h, DXGI_FORMAT_R16G16_FLOAT); PonteHost::Soltar(g_up.tex[PonteHost::SLOT_MV]); }
    }
    if (g_up.texExpo != nullptr && !ConfereDescUp(PonteHost::SLOT_OUTPUT, g_up.texExpo, 1, 1, DXGI_FORMAT_R32_FLOAT)) PonteHost::Soltar(g_up.texExpo);

    if (!ok) { LiberarConjuntoUp(); return false; }
    g_up.valido = true;
    Log("upstream: texturas abertas no device do app: Color=%p Output=%p Depth=%p MV=%p%s%s",
        (void*)g_up.tex[PonteHost::SLOT_COLOR], (void*)g_up.tex[PonteHost::SLOT_OUTPUT],
        (void*)g_up.tex[PonteHost::SLOT_DEPTH], (void*)g_up.tex[PonteHost::SLOT_MV],
        sr ? " (sessao 0x200: Output no tamanho de saida)" : "", g_up.texExpo ? " Expo=1x1" : "");
    return true;
}

static void TratarBuildUp(const FeedBuild& b, FeedBuildAck* ack)
{
    memset(ack, 0, sizeof(*ack));
    ack->fence_in   = g_up.jogoIn;
    ack->fence_out  = g_up.jogoOut;
    ack->output_fmt = b.output_fmt;   /* ecoado: a textura ja existe do lado da DLL */
    ack->ngx_result = 1;

    const DXGI_FORMAT cor = static_cast<DXGI_FORMAT>(b.color_fmt);
    Log("upstream: build %ux%u (interno) color=%u output=%u hdr=%d inverted=%d flags=0x%X mvscale=%.3f,%.3f",
        b.width, b.height, b.color_fmt, b.output_fmt, b.hdr, b.depth_inverted, b.client_flags,
        (double)b.mv_scale_x, (double)b.mv_scale_y);

    /* O QUADRO EM VOO PRIMEIRO (18/09/2026). Este caminho roda na thread do PIPE, e o
       laco de render pode estar neste instante desenhando com as texturas deste conjunto
       (ver o comentario do `emUso` no EstadoUp). Antes daqui saia um LiberarConjuntoUp
       direto - inclusive nas recusas logo abaixo, que arrancavam as texturas de um Build
       que nem seria aceito. Agora: espera o app soltar o quadro e, se ele nao soltar,
       aposenta (o conjunto continua vivo).
       QUANDO ISTO ACONTECE NA VIDA REAL: a DLL manda um Build novo em sessao JA VIVA
       quando o dispatch muda de tamanho ou de formato - `sr: refazendo a sessao` em
       ponte/cliente/sr/sr_hook.cpp. Mexer no upscaler pelo menu do jogo (ligar, desligar
       ou trocar a qualidade do FSR/DLSS) muda o tamanho de render e cai exatamente aqui. */
    EsperarSemUsoUp(2000);
    std::lock_guard<std::mutex> l(g_upMu);
    AposentarConjuntoUp();

    /* Q2: a sessao 0x200 (DLSS SR no app). Recusada quando [ultima] nativo_sr=0. */
    const bool sr = (b.client_flags & kBuildSr) != 0;
    if (sr && !g_upAceitaSr) { Log("upstream: build 0x200 (DLSS SR no app) recusado (nativo_sr=0 no ini)"); return; }
    if (!sr && !g_upAceitaRede) { Log("upstream: build 0x100 (DLSS 5 antes do DLSS do jogo) recusado (o modo escolhido nao pede o Upstream); o jogo segue com o DLSS dele e o DLSS 5 fica na imagem final"); return; }
    if (sr && (b.target_width == 0 || b.target_height == 0)) { Log("upstream: build 0x200 sem target_width/height (tamanho de saida): recusado"); return; }
    /* As tres recusas duras. Cada uma existe porque o contrario quebraria
       alguma coisa em silencio, e silencio aqui vira ghosting na tela. */
    if ((b.client_flags & kBuildUpstream) == 0 && !sr)
    { Log("upstream: build sem o bit FEED_BUILD_UPSTREAM (0x100) neste pipe: recusado"); return; }
    if ((b.client_flags & PonteHost::kBuildAsyncHome) != 0)
    { Log("upstream: build pediu ASYNC_HOME; aqui o modo e sincrono (a DLL esta com a command list aberta): recusado"); return; }
    if ((b.client_flags & PonteHost::kBuildHostCria) != 0)
    { Log("upstream: build pediu HOST_CREATES; quem cria as texturas do evaluate e a DLL: recusado"); return; }
    if (b.width == 0 || b.height == 0) { Log("upstream: build com tamanho zero"); return; }
    if (b.tex[PonteHost::SLOT_COLOR] == 0 || b.tex[PonteHost::SLOT_OUTPUT] == 0)
    { Log("upstream: build sem handle de Color/Output"); return; }

    /* F0: o Output volta no MESMO formato da cor. Quem propoe e a DLL (a
       textura ja existe quando o Build chega), entao aqui so da para conferir
       e dizer o que custa - mas em upstream a conta e mais dura que na fase 2:
       a saida entra de volta no DLSS do jogo, e um formato menor que o da cor
       corta a faixa ANTES do super resolution. */
    if (!sr && static_cast<DXGI_FORMAT>(b.output_fmt) != PonteHost::FormatoTipado(cor))
    {
        if (PonteHost::FormatoEhHdr(cor) && !PonteHost::FormatoEhHdr(static_cast<DXGI_FORMAT>(b.output_fmt)))
        { Log("upstream: output fmt=%u nao acompanha a cor fmt=%u; a faixa seria cortada antes do super resolution: recusado", b.output_fmt, b.color_fmt); return; }
        if (!g_up.avisoFmt)
        { g_up.avisoFmt = true; Log("upstream: output fmt=%u difere da cor fmt=%u (ecoando a proposta da DLL)", b.output_fmt, b.color_fmt); }
    }

    if (!g_up.devPronto) { g_up.pendente = b; g_up.temPendente = true; }
    else if (!MontarConjuntoUp(b)) return;

    g_up.sessao.pidJogo        = g_up.pid;
    g_up.sessao.largura        = b.width;
    g_up.sessao.altura         = b.height;
    g_up.sessao.formatoCor     = cor;
    g_up.sessao.formatoSaida   = static_cast<DXGI_FORMAT>(b.output_fmt);
    g_up.sessao.hdr            = b.hdr != 0 || PonteHost::FormatoEhHdr(cor);
    g_up.sessao.depthInvertido = b.depth_inverted != 0;
    g_up.sessao.mvScaleX       = b.mv_scale_x != 0.f ? b.mv_scale_x : 1.f;
    g_up.sessao.mvScaleY       = b.mv_scale_y != 0.f ? b.mv_scale_y : 1.f;
    /* Q2 */
    g_up.sessao.sr             = sr;
    g_up.sessao.saidaW         = sr ? b.target_width : b.width;
    g_up.sessao.saidaH         = sr ? b.target_height : b.height;
    g_up.sessao.flagsNgx       = sr && b.flags_override >= 0 ? b.flags_override : 0;
    g_up.sessao.depthInv3      = b.depth_inverted < 0 ? -1 : (b.depth_inverted != 0 ? 1 : 0);
    g_up.sessao.temExposicao   = sr && b.panel_tex != 0;
    g_up.temSessao = true;
    /* X-FG-6: um Build em sessao viva e sinal de vida (a DLL trocou de upscaler no mesmo pipe, ou
       de qualidade): o relogio da pausa do I7 recomeca aqui, para a troca nao virar cartaz. */
    if (g_up.recebidos != 0) g_up.qpcUltimoF = PonteHost::Agora();
    ack->ok = 1;
    ack->flags |= kAckUpstream;   /* e este bit que autoriza a DLL a mandar o 'X' */
    if (sr) ack->flags |= kAckSr; /* Q2: e este o que autoriza o 'S' e diz que o DLSS SR e nosso */
    if (sr) Log("upstream: sessao 0x200 (DLSS SR no app) %ux%u -> %ux%u cor=%u saida=%u flags_ngx=0x%X depth_inv=%d expo=%s",
                b.width, b.height, b.target_width, b.target_height, b.color_fmt, b.output_fmt, (unsigned)g_up.sessao.flagsNgx, g_up.sessao.depthInv3, g_up.sessao.temExposicao ? "1x1" : "auto");
    else Log("upstream: sessao %ux%u fmt=%u (interno)", b.width, b.height, b.color_fmt);
}

/* ------------------------------------------------------------------ */
/* A thread do pipe                                                     */
/* ------------------------------------------------------------------ */
static const DWORD kUpConexaoMs = 120000;   /* H4: depois disto o jogador e avisado; a escuta continua (I7) */
static const DWORD kUpHelloMs   = 15000;
static const DWORD kUpBuildMs   = 60000;
static const DWORD kUpFenceMs   = 2000;     /* sincrono: se a cor nao chegou em 2 s, o jogo travou */

/* I7 (18/09/2026): A ESCUTA NAO ACABA NOS 120 s. Antes, `WAIT_TIMEOUT` aqui encerrava a thread e o
   `Abrir` nao ressuscitava a sessao: quem ligasse o DLSS/FSR/XeSS no menu do jogo depois de 2 min
   (menu, carregar o save, ou o jogador que abriu o jogo com o upscaling desligado e ligou depois)
   ficava sem o Upstream ate reabrir o app. Agora o timeout so vale para o aviso do H4 (uma vez) e
   a thread volta a esperar; so `Parar` (evStop) encerra. Devolve: 1 conectou, 0 timeout (escutar
   de novo), -1 parar/erro. */
static int ConectarUp()
{
    OVERLAPPED ov = {};
    ov.hEvent = g_up.evIo;
    ResetEvent(g_up.evIo);
    const BOOL c = ConnectNamedPipe(g_up.pipe, &ov);
    const DWORD e = GetLastError();
    if (!c && e == ERROR_IO_PENDING)
    {
        HANDLE hs[2] = { g_up.evIo, g_up.evStop };
        const DWORD r = WaitForMultipleObjects(2, hs, FALSE, kUpConexaoMs);
        DWORD m = 0;
        if (r != WAIT_OBJECT_0)
        {
            CancelIoEx(g_up.pipe, &ov);
            GetOverlappedResult(g_up.pipe, &ov, &m, TRUE);
            if (r == WAIT_TIMEOUT) return 0;
            MotivoUp("Parar");
            return -1;
        }
        if (!GetOverlappedResult(g_up.pipe, &ov, &m, FALSE)) { MotivoUp("ConnectNamedPipe win32=%lu", GetLastError()); return -1; }
    }
    else if (!c && e != ERROR_PIPE_CONNECTED) { MotivoUp("ConnectNamedPipe win32=%lu", e); return -1; }
    return 1;
}
/* A escuta em laco: timeout avisa uma vez (H4) e volta a esperar. */
static bool EsperarConexaoUp()
{
    bool avisou = false;
    for (;;)
    {
        const int r = ConectarUp();
        if (r == 1) return true;
        if (r < 0) return false;
        if (!avisou)
        {
            avisou = true;
            Log("upstream: a DLL nao pediu upstream em %lu s (o jogo nao chamou DLSS, FSR nem XeSS); a escuta continua ate a ponte fechar", kUpConexaoMs / 1000);
            g_upSemPedido = true;   /* H4: o laco do main avisa o jogador (uma vez) */
        }
    }
}

static bool HandshakeUp()
{
    FeedHello hello = {};
    PonteHost::Xfer x = LerUp(&hello, PonteHost::kHelloV1, kUpHelloMs);
    if (x != PonteHost::XFER_OK) { MotivoDeXferUp(x, "hello"); return false; }
    if (hello.magic != PonteHost::kMagic) { MotivoUp("hello sem magic"); return false; }
    if (hello.version >= 2 && (x = LerUp(&hello.client_kind, sizeof(hello.client_kind), kUpHelloMs)) != PonteHost::XFER_OK) { MotivoDeXferUp(x, "hello client_kind"); return false; }
    if (hello.version >= 4 && (x = LerUp(&hello.self_process, sizeof(hello.self_process), kUpHelloMs)) != PonteHost::XFER_OK) { MotivoDeXferUp(x, "hello self_process"); return false; }

    FeedHelloAck ack = {};
    ack.magic = PonteHost::kMagic; ack.version = PonteHost::kVersao;
    if ((x = EscreverUp(&ack, sizeof(ack), kUpHelloMs)) != PonteHost::XFER_OK) { MotivoDeXferUp(x, "hello ack"); return false; }
    if (hello.version != PonteHost::kVersao) { MotivoUp("a DLL fala v%u, este host v%u", hello.version, PonteHost::kVersao); return false; }
    if (hello.self_process == 0) { MotivoUp("sem handle do processo do jogo"); return false; }

    HANDLE h = PonteHost::ComoHandle(hello.self_process);
    if (GetProcessId(h) != hello.pid) { MotivoUp("self_process nao nomeia o pid %u", hello.pid); return false; }
    g_up.pid   = hello.pid;
    g_up.hJogo = h;
    {
        HANDLE hs = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &hs, SYNCHRONIZE, FALSE, 0) && hs != nullptr)
            g_up.hJogoEspera = hs;
    }
    HANDLE jin = nullptr, jout = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), g_up.hIn,  g_up.hJogo, &jin,  0, FALSE, DUPLICATE_SAME_ACCESS) ||
        !DuplicateHandle(GetCurrentProcess(), g_up.hOut, g_up.hJogo, &jout, 0, FALSE, DUPLICATE_SAME_ACCESS))
    { MotivoUp("DuplicateHandle das fences para o jogo win32=%lu", GetLastError()); return false; }
    g_up.jogoIn = PonteHost::ComoValor(jin); g_up.jogoOut = PonteHost::ComoValor(jout);
    Log("upstream: a DLL do jogo pid %lu conectou no pipe .up (protocolo v%u)", g_up.pid, hello.version);
    return true;
}

static bool EsperarEntradaUp(uint64_t n)
{
    const ULONGLONG limite = GetTickCount64() + kUpFenceMs;
    for (;;)
    {
        if (g_up.f12In->GetCompletedValue() >= n) return true;
        if (FAILED(g_up.f12In->SetEventOnCompletion(n, g_up.evFence))) return false;
        const ULONGLONG agora = GetTickCount64();
        if (agora >= limite) return false;
        HANDLE hs[3] = { g_up.evFence, g_up.evStop, g_up.hJogoEspera };
        const DWORD r = WaitForMultipleObjects(g_up.hJogoEspera != nullptr ? 3 : 2, hs, FALSE, static_cast<DWORD>(limite - agora));
        if (r != WAIT_OBJECT_0) return false;
    }
}

static DWORD WINAPI ThreadPipeUp(void*)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);   /* B9: ver ThreadPipe (ponte_feeder_host.h) */
    if (EsperarConexaoUp() && HandshakeUp()) for (;;)
    {
        BYTE tag = 0;
        PonteHost::Xfer x = LerUp(&tag, 1, INFINITE);
        if (x != PonteHost::XFER_OK) { MotivoDeXferUp(x, "tag"); break; }

        if (tag == 'B')
        {
            FeedBuild b = {};
            if ((x = LerUp(&b, sizeof(b), kUpBuildMs)) != PonteHost::XFER_OK) { MotivoDeXferUp(x, "build"); break; }
            FeedBuildAck ack;
            TratarBuildUp(b, &ack);
            if ((x = EscreverUp(&ack, sizeof(ack), kUpBuildMs)) != PonteHost::XFER_OK) { MotivoDeXferUp(x, "build ack"); break; }
        }
        else if (tag == 'X')
        {
            FeedExtraMsg ex = {};
            if ((x = LerUp(&ex, sizeof(ex), kUpBuildMs)) != PonteHost::XFER_OK) { MotivoDeXferUp(x, "extra"); break; }
            std::lock_guard<std::mutex> l(g_upMu);
            g_up.extra = ex;
            g_up.temExtra = true;
        }
        else if (tag == 'S')   /* Q2: o renderSize e a nitidez do dispatch do FSR/XeSS */
        {
            FeedSrMsg sm = {};
            if ((x = LerUp(&sm, sizeof(sm), kUpBuildMs)) != PonteHost::XFER_OK) { MotivoDeXferUp(x, "sr"); break; }
            std::lock_guard<std::mutex> l(g_upMu);
            g_up.srMsg = sm;
            g_up.temSr = true;
        }
        else if (tag == 'F')
        {
            FeedFrameMsg fm = {};
            if ((x = LerUp(&fm, sizeof(fm), kUpBuildMs)) != PonteHost::XFER_OK) { MotivoDeXferUp(x, "frame"); break; }
            bool tem;
            {
                std::lock_guard<std::mutex> l(g_upMu);
                g_up.ultimoRecebido = fm.n;
                g_up.qpcUltimoF = PonteHost::Agora();   /* I7: o relogio da pausa */
                tem = g_up.valido && !g_upDesligado.load();
                if (!tem) SinalCpuUp(fm.n);   /* sem conjunto (ou desligado): solta o jogo na hora */
            }
            if (!tem) continue;

            if (!EsperarEntradaUp(fm.n))
            {
                if (WaitForSingleObject(g_up.evStop, 0) == WAIT_OBJECT_0) { MotivoUp("Parar"); break; }
                Log("upstream: quadro %llu: in_fence nao chegou em %lu ms; soltando o jogo sem processar",
                    (unsigned long long)fm.n, kUpFenceMs);
                std::lock_guard<std::mutex> l(g_upMu);
                g_up.prontoValido = false;
                SinalCpuUp(fm.n);
                continue;
            }
            const int64_t chegada = PonteHost::Agora();
            std::lock_guard<std::mutex> l(g_upMu);
            /* O APP NAO VEIO BUSCAR O ANTERIOR. Soltamos o jogo (ele nao pode ficar parado) e
               substituimos - mas isto NAO e inocente como na fase 2. Ali o jogo recebe de volta
               o proprio quadro dele; aqui a DLL troca o recurso `Color` pelo nosso Output antes
               de chamar o DLSS, e um Output que ninguem escreveu para este `n` e a cor de um
               quadro ANTERIOR entrando no DLSS junto com os MV deste - que e a receita exata do
               ghosting que o G1 se recusou a fazer. Por isso conta, aparece no log e, passando
               de 5% dos quadros recebidos, desliga o upstream em vez de sujar a imagem calado. */
            if (g_up.prontoValido && !g_up.emUso)
            {
                SinalCpuUp(g_up.prontoN);
                const uint64_t s = ++g_up.soltos;
                if (s == 1 || (s % 100) == 0)
                    Log("upstream: quadro %llu solto sem processar (o app nao veio buscar; %llu ate agora) - a DLL vai trocar a Color por um Output do quadro anterior",
                        (unsigned long long)g_up.prontoN, (unsigned long long)s);
            }
            ++g_up.recebidos;
            /* Quadros soltos sem processar sao um problema de IMAGEM, nao de desempenho: o
               orcamento de 4 ms nunca os ve (eles nem chegam ao app). Dai a regra propria, com
               a mesma fatia de 5% e um piso que evita desligar por causa do arranque. */
            if (g_up.recebidos >= 200 && g_up.soltos * 20 > g_up.recebidos && !g_upDesligado.load())
            {
                Log("upstream: %llu de %llu quadros soltos sem processar (mais de 5%%): a Color trocada seria de um quadro anterior",
                    (unsigned long long)g_up.soltos, (unsigned long long)g_up.recebidos);
                g_upDesligado = true;
                MotivoUp("o app nao acompanhou: mais de 5% dos quadros voltariam com a cor do quadro anterior");
                SinalCpuUp(fm.n);
                SetEvent(g_up.evStop);
                continue;
            }
            g_up.prontoValido   = true;
            g_up.prontoN        = fm.n;
            g_up.prontoQpc      = chegada;
            g_up.prontoFm       = fm;
            /* O 'X' vale para ESTE quadro so se a DLL o mandou com o mesmo n. */
            g_up.prontoTemExtra = g_up.temExtra && g_up.extra.n == fm.n;
            g_up.prontoExtra    = g_up.prontoTemExtra ? g_up.extra : FeedExtraMsg();
            g_up.prontoTemSr    = g_up.temSr && g_up.srMsg.n == fm.n;   /* Q2 */
            g_up.prontoSr       = g_up.prontoTemSr ? g_up.srMsg : FeedSrMsg();
            SetEvent(g_up.evQuadro);
        }
        else if (tag == 'W')
        {
            FeedWindowMsg wm = {};
            if ((x = LerUp(&wm, sizeof(wm), kUpBuildMs)) != PonteHost::XFER_OK) { MotivoDeXferUp(x, "window"); break; }
        }
        else if (tag == 'O') { /* overlay: nao ha painel nesta sessao */ }
        else { MotivoUp("tag desconhecida 0x%02X", tag); break; }
    }

    {
        std::lock_guard<std::mutex> l(g_upMu);
        g_up.prontoValido = false;
        SinalCpuUp(UINT64_MAX);   /* solta todo Wait(out, n) que a DLL tenha enfileirado */
        g_upAtiva = false;
        if (g_up.motivo[0] == '\0') MotivoUp("desconhecido");
        /* I7: sessao negociada que acabou sem o app pedir = a DLL/o jogo fecharam o pipe no meio.
           O laco confere se o jogo ainda esta vivo antes de contar como falha do Upstream. */
        if (g_up.temSessao && strcmp(g_up.motivo, "Parar") != 0 && !g_upDesligado.load() && !g_upFalhou.load())
        {
            strncpy(g_upFalhaMotivo, g_up.motivo, sizeof(g_upFalhaMotivo) - 1); g_upFalhaMotivo[sizeof(g_upFalhaMotivo) - 1] = 0;
            g_upFalhou = true;
        }
        LogUpEncerrada(g_up.motivo);
        SetEvent(g_up.evQuadro);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Abrir / Ligar / Parar                                                */
/* ------------------------------------------------------------------ */
static void LiberarTudoUp()
{
    LiberarConjuntoUp();
    LiberarVelhoUp();   /* o conjunto aposentado tambem morre no fim da sessao */
    PonteHost::Soltar(g_up.f11In);
    PonteHost::Soltar(g_up.f11Out);
    PonteHost::Soltar(g_up.f12In);
    PonteHost::Soltar(g_up.f12Out);
    PonteHost::Soltar(g_up.dev12);
    PonteHost::Soltar(g_up.ctx4);
    PonteHost::Soltar(g_up.dev5);
    PonteHost::Soltar(g_up.dev1);
    PonteHost::FecharHandle(g_up.hIn);
    PonteHost::FecharHandle(g_up.hOut);
    PonteHost::FecharHandle(g_up.hJogo);
    PonteHost::FecharHandle(g_up.hJogoEspera);
    PonteHost::FecharHandle(g_up.pipe);
    PonteHost::FecharHandle(g_up.evIo);
    PonteHost::FecharHandle(g_up.evStop);
    PonteHost::FecharHandle(g_up.evFence);
    PonteHost::FecharHandle(g_up.evQuadro);
    PonteHost::FecharHandle(g_up.thread);
    g_up.dev = nullptr; g_up.ctx = nullptr; g_up.devPronto = false;
    g_up.emUso = false; g_up.temPendente = false; g_up.temSessao = false;
}

/* Sobe o servidor do pipe `.up` para o pid do jogo. Chamado junto com o
   `PonteHost::Preparar`/`PrepararEmExecucao` quando `nativo_upstream=1`. Nao
   bloqueia: a DLL so conecta depois de ver a primeira avaliacao de DLSS SR
   (pode demorar minutos, ou nunca vir - jogo sem DLSS). */
static bool Abrir(DWORD pidJogo)
{
    if (pidJogo == 0) return false;
    if (g_upAtiva.load()) return true;
    if (g_up.thread != nullptr) return false;   /* ja encerrada: nao ressuscita nesta sessao */

    g_up = EstadoUp();
    g_upDesligado = false;
    g_up.pid = pidJogo;
    { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_up.qpcFreq = f.QuadPart > 0 ? f.QuadPart : 1; }

    char nome[160];
    snprintf(nome, sizeof(nome), "\\\\.\\pipe\\dlss5-feed.%lu.up", pidJogo);
    g_up.pipe = CreateNamedPipeA(nome, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1024, 512, 0, nullptr);
    if (g_up.pipe == INVALID_HANDLE_VALUE)
    { g_up.pipe = nullptr; Log("upstream: CreateNamedPipe %s falhou %lu", nome, GetLastError()); return false; }

    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&g_up.dev12));
    bool ok = SUCCEEDED(hr) && g_up.dev12 != nullptr;
    if (ok) ok = SUCCEEDED(g_up.dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&g_up.f12In))) &&
                 SUCCEEDED(g_up.dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&g_up.f12Out)));
    if (ok) ok = SUCCEEDED(g_up.dev12->CreateSharedHandle(g_up.f12In,  nullptr, GENERIC_ALL, nullptr, &g_up.hIn)) &&
                 SUCCEEDED(g_up.dev12->CreateSharedHandle(g_up.f12Out, nullptr, GENERIC_ALL, nullptr, &g_up.hOut));
    if (ok)
    {
        g_up.evIo     = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
        g_up.evStop   = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
        g_up.evFence  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_up.evQuadro = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ok = g_up.evIo != nullptr && g_up.evStop != nullptr && g_up.evFence != nullptr && g_up.evQuadro != nullptr;
    }
    if (ok)
    {
        g_upAtiva = true;
        g_up.thread = CreateThread(nullptr, 0, &ThreadPipeUp, nullptr, 0, nullptr);
        if (g_up.thread == nullptr) { g_upAtiva = false; ok = false; }
    }
    if (!ok) { Log("upstream: nao subiu o servidor %s (fences ou thread)", nome); LiberarTudoUp(); return false; }
    Log("upstream: servidor no ar em %s (a DLL conecta quando vir DLSS SR)", nome);
    return true;
}

/* Fase 2: as fences e as texturas no device do app. */
static bool Ligar(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    if (!g_upAtiva.load() || dev == nullptr || ctx == nullptr) return false;
    std::lock_guard<std::mutex> l(g_upMu);
    if (g_up.devPronto) return true;
    g_up.dev = dev; g_up.ctx = ctx;
    bool ok = SUCCEEDED(dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&g_up.dev1))) && g_up.dev1 != nullptr;
    if (ok) ok = SUCCEEDED(dev->QueryInterface(__uuidof(ID3D11Device5), reinterpret_cast<void**>(&g_up.dev5))) && g_up.dev5 != nullptr;
    if (ok) ok = SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), reinterpret_cast<void**>(&g_up.ctx4))) && g_up.ctx4 != nullptr;
    if (ok) ok = SUCCEEDED(g_up.dev5->OpenSharedFence(g_up.hIn,  __uuidof(ID3D11Fence), reinterpret_cast<void**>(&g_up.f11In))) &&
                 SUCCEEDED(g_up.dev5->OpenSharedFence(g_up.hOut, __uuidof(ID3D11Fence), reinterpret_cast<void**>(&g_up.f11Out)));
    if (!ok) { Log("upstream: o device do app nao abriu as fences do upstream; a sessao fica fora"); MotivoUp("as fences nao abriram no device do app"); SetEvent(g_up.evStop); return false; }
    g_up.devPronto = true;
    if (g_up.temPendente) { g_up.temPendente = false; if (!MontarConjuntoUp(g_up.pendente)) Log("upstream: o Build pendente nao montou no device do app"); }
    return true;
}

static bool Ativa()  { return g_upAtiva.load() && !g_upDesligado.load(); }
[[maybe_unused]] static HANDLE EventoQuadro() { return g_up.evQuadro; }
static bool ObterSessao(SessaoUp* s)
{
    if (!g_upAtiva.load()) return false;
    std::lock_guard<std::mutex> l(g_upMu);
    if (!g_up.temSessao) return false;
    if (s != nullptr) *s = g_up.sessao;
    return true;
}

static void Parar()
{
    if (g_up.evStop == nullptr && g_up.thread == nullptr) return;
    if (g_up.evStop != nullptr) SetEvent(g_up.evStop);
    if (PipeUpAberto()) CancelIoEx(g_up.pipe, nullptr);
    bool threadViva = false;
    if (g_up.thread != nullptr && WaitForSingleObject(g_up.thread, 5000) != WAIT_OBJECT_0)
    {
        threadViva = true;
        Log("upstream: a thread do pipe nao terminou em 5 s");
    }
    {
        std::lock_guard<std::mutex> l(g_upMu);
        SinalCpuUp(UINT64_MAX);
        g_upAtiva = false;
        if (threadViva) g_up.devPronto = false;   /* ninguem monta conjunto novo daqui em diante */
        LogUpEncerrada(g_up.motivo[0] != '\0' ? g_up.motivo : "Parar");
    }
    /* NAO LIBERAR COM A THREAD VIVA (18/09/2026). O caminho antigo apenas LOGAVA o estouro
       dos 5 s e seguia para o LiberarTudoUp - que fecha o pipe, o evStop, o evFence e o
       evQuadro e solta as fences e o device auxiliar, tudo isso com a thread do pipe
       possivelmente dentro de um WaitForMultipleObjects ou de um ReadFile sobre esses
       mesmos handles. E violacao de acesso, e o pior e onde ela cairia: o `Desligar` roda
       EM TEMPO DE EXECUCAO (quando o orcamento estoura, ou quando o upscaler do jogo sai
       do caminho), com o app aberto e o jogo rodando.
       Vazar a sessao ate o fim do processo e o preco certo aqui: sao alguns handles e uma
       textura por slot, o processo esta de saida ou segue sem upstream, e o guarda do
       Abrir (`thread != nullptr`) impede que alguem monte outra sessao por cima desta. */
    if (threadViva)
    {
        Log("upstream: os recursos da sessao NAO foram liberados de proposito - fechar o pipe, "
            "os eventos e as fences com a thread ainda dentro deles derrubaria o processo. Eles "
            "vazam ate o app fechar, e nenhuma sessao nova sobe em cima desta.");
        return;
    }
    if (PipeUpAberto()) DisconnectNamedPipe(g_up.pipe);
    LiberarTudoUp();
}

/* Desliga o upstream SEM derrubar a fase 2: o pipe cai, a DLL do G1 ve o
   fechamento e volta a chamar a `EvaluateFeature` original com a cor
   intocada. E o que o app faz quando estoura o orcamento. */
static void Desligar(const char* motivo)
{
    if (g_upDesligado.exchange(true)) return;
    MotivoUp("%s", motivo != nullptr ? motivo : "pedido do app");
    /* I7: desligar por decisao do app (orcamento, DLSS SR falhando) e falha do Upstream neste jogo */
    if (!g_upFalhou.load())
    {
        strncpy(g_upFalhaMotivo, g_up.motivo, sizeof(g_upFalhaMotivo) - 1); g_upFalhaMotivo[sizeof(g_upFalhaMotivo) - 1] = 0;
        g_upFalhou = true;
    }
    Parar();
}

/* I7: A PAUSA. O jogador desligou o upscaling no menu do jogo: a DLL fica conectada (ela so
   esquece o contexto do upscaler), mas nenhum 'F' chega mais. Nao ha sinal proprio da DLL
   para isso (registrado no relatorio), entao o sinal e o relogio: ms desde o ultimo 'F' de
   uma sessao negociada que ja mandou quadro. -1 = sem sessao ou sem quadro ainda. Quem
   decide o que e "pausa" (e quanto tempo) e o laco do main, que tambem ve a fase 2 andando. */
static double MsSemQuadro()
{
    if (!g_upAtiva.load() || g_upDesligado.load()) return -1.0;
    std::lock_guard<std::mutex> l(g_upMu);
    if (!g_up.temSessao || g_up.recebidos == 0 || g_up.qpcUltimoF == 0) return -1.0;
    return MsUp(g_up.qpcUltimoF, PonteHost::Agora());
}
/* Sessao negociada (Build aceito) que ainda nao mandou quadro nenhum: ms desde o Build. */
static bool SessaoSemQuadro()
{
    if (!g_upAtiva.load() || g_upDesligado.load()) return false;
    std::lock_guard<std::mutex> l(g_upMu);
    return g_up.temSessao && g_up.recebidos == 0;
}

/* ------------------------------------------------------------------ */
/* Thread principal: ObterQuadro / Entregar                             */
/* ------------------------------------------------------------------ */
/* Q1 (16/09/2026): a PROVA de que a depth chega. Readback do slot Depth (R32F) no device do app,
   duas vezes por sessao (30.a e 300.a busca): min, max e fracao de texels nao-zero. Uma depth
   ZERADA (formato que a DLL nao copia/converte) aparece aqui como min=max=0, nao-zero=0 %. */
static void MedirDepthUp(ID3D11Texture2D* depth, uint64_t n)
{
    if (depth == nullptr || g_up.dev == nullptr || g_up.ctx == nullptr) return;
    D3D11_TEXTURE2D_DESC td; depth->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.MiscFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* st = nullptr;
    if (FAILED(g_up.dev->CreateTexture2D(&td, nullptr, &st)) || st == nullptr) return;
    g_up.ctx->CopyResource(st, depth);
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (SUCCEEDED(g_up.ctx->Map(st, 0, D3D11_MAP_READ, 0, &m)))
    {
        float mn = 3.4e38f, mx = -3.4e38f; uint64_t naoZero = 0, total = 0;
        for (UINT y = 0; y < td.Height; ++y)
        {
            const float* lin = reinterpret_cast<const float*>(static_cast<const BYTE*>(m.pData) + (size_t)y * m.RowPitch);
            for (UINT x = 0; x < td.Width; ++x) { const float v = lin[x]; if (v < mn) mn = v; if (v > mx) mx = v; if (v != 0.f) ++naoZero; ++total; }
        }
        g_up.ctx->Unmap(st, 0);
        Log("upstream: depth quadro %llu: min=%.5f max=%.5f nao-zero=%.1f%% (%ux%u R32F, %s)",
            (unsigned long long)n, mn, mx, total ? 100.0 * (double)naoZero / (double)total : 0.0, td.Width, td.Height,
            naoZero == 0 ? "ZERADA" : "chegou");
    }
    st->Release();
}

static bool ObterQuadro(QuadroUp* q)
{
    if (q == nullptr || !Ativa()) return false;
    std::lock_guard<std::mutex> l(g_upMu);
    if (g_up.emUso || !g_up.prontoValido || !g_up.valido) return false;
    { static uint64_t buscas = 0; ++buscas; if (buscas == 30 || buscas == 300) MedirDepthUp(g_up.tex[PonteHost::SLOT_DEPTH], g_up.prontoN); }
    q->n          = g_up.prontoN;
    q->cor        = g_up.tex[PonteHost::SLOT_COLOR];
    q->depth      = g_up.tex[PonteHost::SLOT_DEPTH];
    q->mv         = g_up.tex[PonteHost::SLOT_MV];
    q->largura    = g_up.w;
    q->altura     = g_up.h;
    q->formato    = g_up.corFmt;
    q->temExtra   = g_up.prontoTemExtra;
    q->extra      = g_up.prontoExtra;
    q->qpcChegada = g_up.prontoQpc;
    /* Q2 */
    q->sr         = g_up.sessao.sr;
    q->saida      = g_up.tex[PonteHost::SLOT_OUTPUT];
    q->expo       = g_up.texExpo;
    q->temSr      = g_up.prontoTemSr;
    q->srMsg      = g_up.prontoSr;
    if (!q->temSr) { q->srMsg.n = g_up.prontoN; q->srMsg.render_w = g_up.w; q->srMsg.render_h = g_up.h; }
    /* O 'F' v9 e a verdade do reset e do jitter; o 'X' so completa. */
    if (!q->temExtra)
    {
        q->extra.n = g_up.prontoN;
        q->extra.jitter_x = g_up.prontoFm.jitter_x;
        q->extra.jitter_y = g_up.prontoFm.jitter_y;
        q->extra.mv_scale_x = g_up.sessao.mvScaleX;
        q->extra.mv_scale_y = g_up.sessao.mvScaleY;
        q->extra.pre_exposure = 1.f;
        q->extra.exposure_scale = 1.f;
    }
    if (g_up.prontoFm.reset != 0) q->extra.flags |= kExtraReset;
    g_up.prontoValido = false;
    g_up.emUso = true;
    g_up.emUsoN = q->n;
    /* De QUAL conjunto sao os ponteiros que acabaram de sair daqui. E o que permite a um
       rebuild aposentar o conjunto em vez de liberar por baixo do laco de render. */
    g_up.emUsoGeracao = g_up.geracao;
    return true;
}

/* Devolve o quadro. `resultado` nullptr (ou fora do tamanho/formato) =
   passa-through: Color->Output, a cor do jogo intocada naquele quadro.
   `redeMs` e so para o log de 300 em 300; `atrasado` conta o estouro de
   orcamento que o app ja mediu. Sinaliza a out_fence SEMPRE. */
static void Entregar(ID3D11Texture2D* resultado, uint64_t n, double redeMs, bool atrasado)
{
    std::lock_guard<std::mutex> l(g_upMu);
    if (!g_up.emUso || n != g_up.emUsoN) return;

    ID3D11Texture2D* out = g_up.valido ? g_up.tex[PonteHost::SLOT_OUTPUT] : nullptr;
    ID3D11Texture2D* cor = g_up.valido ? g_up.tex[PonteHost::SLOT_COLOR]  : nullptr;
    if (out != nullptr && g_up.ctx != nullptr)
    {
        ID3D11Texture2D* fonte = nullptr;
        if (resultado != nullptr)
        {
            D3D11_TEXTURE2D_DESC d = {};
            resultado->GetDesc(&d);
            if (d.Width == g_up.ow && d.Height == g_up.oh && PonteHost::MesmaFamilia(d.Format, g_up.outFmt)) fonte = resultado;
            else if (!g_up.avisoCopia)
            {
                g_up.avisoCopia = true;
                Log("upstream: o resultado e %ux%u fmt=%u e o Output e %ux%u fmt=%u; passando a cor sem processar",
                    d.Width, d.Height, (unsigned)d.Format, g_up.ow, g_up.oh, (unsigned)g_up.outFmt);
            }
        }
        /* Q2: no SR o Output e maior que a cor; sem resultado, nao ha passa-through possivel por
           CopyResource (tamanhos diferentes) - o slot fica como estava (a DLL, sem a troca ligada,
           chama a original; com a troca ligada, o quadro repete o Output anterior e conta atraso). */
        if (fonte == nullptr && !g_up.sessao.sr) fonte = cor;   /* passa-through */
        if (fonte != nullptr) g_up.ctx->CopyResource(out, fonte);
    }
    if (g_up.ctx4 != nullptr && g_up.f11Out != nullptr)
    {
        g_up.ctx4->Signal(g_up.f11Out, n);
        g_up.ctx->Flush();
        if (n > g_up.ultimoSinal) g_up.ultimoSinal = n;
    }
    g_up.emUso = false;
    /* A APOSENTADORIA FECHA AQUI: o app acabou de soltar o quadro, e se ele era do
       conjunto aposentado por um rebuild, agora as texturas velhas podem morrer. Este e o
       unico lugar que as solta no caminho normal - por isso vale para o quadro
       passa-through tambem (o `return` do topo nao chega aqui, mas ele so acontece quando
       nao ha quadro em uso, e ai nao havia nada a soltar). */
    if (g_up.velhoValido && g_up.emUsoGeracao == g_up.velhoGeracao) LiberarVelhoUp();

    const double totalMs = MsUp(g_up.prontoQpc != 0 ? g_up.prontoQpc : PonteHost::Agora(), PonteHost::Agora());
    g_up.somaRedeMs  += redeMs;
    g_up.somaTotalMs += totalMs > 0.0 ? totalMs : redeMs;
    if (atrasado) ++g_up.atrasos;
    const uint64_t k = ++g_up.avaliacoes;
    /* As medias sao DAS ULTIMAS 300, e nao do acumulado desde o inicio. A media acumulada
       escondeu a deriva na primeira fumaca: `total` subiu de 2,0 para 8,3 ms em 40 s e so deu
       para ver disso fazendo a conta a mao entre duas linhas. Uma janela mostra na hora. */
    if ((k % 300) == 0)
    {
        Log("upstream: %llu avaliacoes | rede %.2f ms | total %.2f ms | atrasos %llu | soltos %llu",
            (unsigned long long)k, g_up.somaRedeMs / 300.0, g_up.somaTotalMs / 300.0,
            (unsigned long long)g_up.atrasos, (unsigned long long)g_up.soltos);
        g_up.somaRedeMs = 0.0; g_up.somaTotalMs = 0.0;
    }
}

[[maybe_unused]] static uint64_t Avaliacoes() { return g_up.avaliacoes; }
[[maybe_unused]] static uint64_t Atrasos()    { return g_up.atrasos; }

} // namespace PonteUp
