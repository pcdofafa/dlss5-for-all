/* =====================================================================
   ponte_feeder_host.h -- o NOSSO exe no papel do dlss5-feed-host64.exe do
   DLSS5-Feeder: recebe o quadro do jogo por textura compartilhada e devolve
   o resultado pela mesma via, sem captura de tela (lado HOST, dentro do app).

   Prova de conceito do modo integrado (CONTRATO.md, 15/09/2026). Fonte de
   verdade do protocolo: external\DLSS5-Feeder\src\feed_ipc.h (v9) e o host
   de referencia external\DLSS5-Feeder\host\dlss5-feed-host64.cpp. Tudo o que
   esta escrito abaixo foi lido desses dois arquivos e do cliente de 32 bits
   (src\dlss5-feed32.cpp); onde este header diverge do host de referencia,
   o comentario diz por que.

   1. QUEM LANCA QUEM, E COM QUE ARGUMENTOS
   ----------------------------------------
   O add-on (dlss5-feed.addon32, dentro do jogo) lanca o host com
   CreateProcessA, CREATE_NO_WINDOW, diretorio de trabalho = <pasta do
   jogo>\host64, e a linha de comando:

       "<pasta do jogo>\host64\dlss5-feed-host64.exe" <pid do jogo>[ --behind][ --gpu-priority]

   - <pid do jogo>: decimal, obrigatorio, o PRIMEIRO token numerico. E o
     unico jeito de saber que somos uma sessao de ponte: o app normal nunca
     recebe um numero solto na linha de comando.
   - --behind: passado quando dlss5-feed.cfg tem host_window=0 (padrao).
     Significa "janela ferramenta parada atras de tudo"; --hide e --test
     existem no host mas o add-on NUNCA os passa. Aqui os quatro sao lidos e
     ignorados: quem decide onde nasce a janela do app e o main.cpp (A5).
   - --gpu-priority: host_gpu_priority=1 no cfg. Ignorado aqui.

   O PIPE e criado pelo HOST (CreateNamedPipe) com o nome
   \\.\pipe\dlss5-feed.<pid do jogo>; o jogo tenta CreateFile nele a cada
   100 ms durante 15 s contados do CreateProcess. Se nao aparecer, o add-on
   desiste ("pipe never appeared"). Por isso Preparar() cria o pipe na hora,
   ANTES de subir a thread, e nao precisa de device nenhum: e para ser
   chamado no comeco do wWinMain, antes de qualquer tela.

   2. A SEQUENCIA NO PIPE (structs pack(1), nunca pixels)
   ------------------------------------------------------
   jogo -> host   FeedHello {magic 'DLS5', version, pid, client_kind, self_process}
   host -> jogo   FeedHelloAck {magic, version=9, panel_width=0, panel_height=0}
                  A versao vai na resposta MESMO quando nao bate, para o add-on
                  poder nomear o problema no log do jogo; depois o host fecha.
                  panel 0x0 = "este host nao tem painel", e o jogo entao manda
                  panel_tex=0 no Build. Nao ha painel nesta prova.
   jogo -> host   'B' + FeedBuild (92 bytes) a cada mudanca de resolucao/formato:
                  width/height (resolucao de trabalho), color_fmt, output_fmt,
                  tex[4] = VALORES de NT handle no processo do jogo, client_flags
                  (FEED_BUILD_ASYNC_HOME diz o modo do laco), target_* (SR),
                  panel_tex, transport.
   host -> jogo   FeedBuildAck (116 bytes): ok=1, ngx_result=1 (sucesso NGX),
                  fence_in/fence_out = valores de handle validos NO JOGO (o host
                  duplica para la), tex[]/tex_size[] so quando o host criou as
                  texturas, output_fmt ecoado, flags.
   jogo -> host   'F' + FeedFrameMsg (20 bytes) por quadro: n, reset, jitter.
   jogo -> host   'W' + FeedWindowMsg (8 bytes): redimensionar a janela do host.
                  Lido e ignorado (o payload TEM de ser consumido para nao
                  dessincronizar o pipe).
   jogo -> host   'O' sem payload: "abra o overlay do ReShade". Ignorado.
   Pipe quebrado de qualquer lado = "pare de alimentar".

   FORMATOS (confirmado na bancada pelo A3: "shared set ready: 800x600 color
   fmt=87 output fmt=28"): Color = formato tipado do backbuffer do jogo
   (87 = B8G8R8A8_UNORM); Output = OutputFormatFor(color) = 28 =
   R8G8B8A8_UNORM para qualquer backbuffer de 8 bits; Depth R32_FLOAT (41);
   MV R16G16_FLOAT (34). Sessao::formatoSaida e o que EntregarSaida espera.

   3. HANDLES: COMO CHEGAM E COMO ABRIMOS EM D3D11
   -----------------------------------------------
   FeedHello::self_process (v4+) e um handle do PROCESSO DO JOGO ja valido no
   host: o add-on faz DuplicateHandle(GetCurrentProcess() -> handle do host
   que o CreateProcess lhe deu) com PROCESS_DUP_HANDLE|PROCESS_QUERY_LIMITED_
   INFORMATION. O host de referencia cai para OpenProcess quando vem 0; aqui
   NUNCA: OpenProcess sobre o jogo e proibido nesta prova, e sem handle a
   sessao termina com "erro:sem handle do processo do jogo".

   Texturas (cliente D3D11): o JOGO cria as quatro (D3D11_RESOURCE_MISC_
   SHARED_NTHANDLE|SHARED; Color/Depth/MV com SRV|RTV, Output com SRV|UAV)
   e manda os valores de handle DELE. Nos fazemos DuplicateHandle(hJogo ->
   nos) e ID3D11Device1::OpenSharedResource1 no device do APP, na fase 2.
   Textura criada em D3D11 abre em D3D11 sem restricao (e o mesmo caminho
   que o shared_source.h / addon\shared_frame.h ja usam entre as camadas).
   O host de referencia abre em D3D12 (OpenSharedHandle) porque o NGX vive
   la; nao precisamos disso.

   Fences: o HOST cria as duas e duplica para o jogo, e isso tem de
   acontecer na fase 1 (o BuildAck leva os handles), antes de existir o
   device do app. Elas nascem num device D3D12 AUXILIAR proprio deste header
   e, na fase 2, sao abertas no D3D11 do app via ID3D11Device5::
   OpenSharedFence. D3D12 e nao D3D11.4 no auxiliar porque ID3D11Fence NAO
   TEM Signal pela CPU (em D3D11 so ID3D11DeviceContext4::Signal existe, e o
   contexto imediato pertence a thread principal): o host precisa sinalizar
   out_fence pela CPU fora dela -- ao fechar o pipe (soltar todo Wait que o
   jogo enfileirou, senao a fila dele trava ate o TDR), a cada quadro que
   chega antes de haver device, e no Parar(). ID3D12Fence::Signal e
   SetEventOnCompletion fazem isso de qualquer thread. E o mesmo tipo de
   objeto (fence D3D12 compartilhada) que o cliente ja testou com o host
   original, entao o OpenSharedFence do jogo ve exatamente o que espera.
   O auxiliar nasce no adaptador padrao; na fase 2 o LUID e conferido com o
   do device do app e a sessao falha com motivo claro se forem diferentes
   (fence compartilhada nao cruza adaptadores).
   Clientes GL/Vulkan precisariam do host CRIAR as texturas em D3D12 (heap
   SHARED + GetResourceAllocationInfo para tex_size[]): fora desta prova, a
   sessao e recusada com motivo no log.

   4. O LACO POR QUADRO E AS FENCES
   --------------------------------
   Host, sempre: espera in_fence >= n, processa, Signal(out_fence, n).
   Jogo, async_home=0 (mesmo quadro): copia entradas, Signal(in, n), manda
   'F', enfileira Wait(out, n) + blit Output -> backbuffer.
   Jogo, async_home=1 (pipelined, padrao): enfileira Wait(out, m) do quadro
   m enviado ANTES, copia entradas de n, blit do Output que o host produziu
   para m, DEPOIS Signal(in, n) e 'F'. O blit le Output antes do signal que
   autoriza o host a sobrescreve-lo: um slot unico de Output fica sem
   corrida. Custo: o resultado na tela e um quadro velho.
   Consequencia que este header explora: nos DOIS modos a copia das entradas
   de n+1 pelo jogo fica atras (na fila dele) do Wait(out, n). Logo in_fence
   nunca chega a n+1 antes de sinalizarmos out n: ha no maximo UM quadro em
   voo, e o slot unico de Color tambem fica sem corrida DESDE QUE o nosso
   Signal(out, n) seja enfileirado DEPOIS de toda leitura de Color n pela
   nossa GPU -- e e: ctx4->Signal vai no contexto imediato, atras de tudo o
   que o app desenhou, e a copia para Output tambem. A ordem em
   EntregarSaida e: copia para Output, Signal(out, n), Flush.
   O host tambem tem de sinalizar TODO n que recebeu, mesmo que nao processe
   (antes da fase 2, timeout, rebuild no meio): o jogo espera exatamente
   esse valor no Wait dele. Entre Preparar e Iniciar cada 'F' e solto na
   hora pela CPU (o jogo mostra o Output ainda vazio por esses segundos; a
   alternativa, segurar, estoura o prazo de 4 s da escrita no pipe do jogo
   e ele desiste do host). Fence e monotonica: sinalizar UINT64_MAX ao
   fechar solta qualquer espera pendente.

   5. O QUE O JOGO FAZ QUANDO FECHA
   --------------------------------
   HostClose no add-on: drena (espera out_fence >= ultimo n, 2 s), FECHA O
   PIPE, espera o processo do host sair por ate 4 s e entao TerminateProcess.
   Ou seja: depois de "ponte: encerrada motivo=pipe fechado" o app tem 4 s
   para sair por conta propria (A5).

   THREADS
   -------
   A thread do pipe (propria) faz handshake, Build, le os 'F' e espera a
   in_fence pela CPU. Preparar/Iniciar/ObterQuadro/EntregarSaida/Parar sao
   da thread principal (a dona do contexto imediato). Um mutex protege o
   estado; a thread do pipe NUNCA toca o ID3D11DeviceContext. Nenhuma
   excecao C++ atravessa fronteira: tudo e Win32/COM com HRESULT; erro vai
   para o log e Ativa() vira false.

   CONTRATO DE USO (thread principal)
   ----------------------------------
   - Fase 1: Preparar(GetCommandLineW(), &sessao, timeoutMs) o mais cedo
     possivel. Devolve false sem log se nao e sessao de ponte; com log
     "ponte: encerrada motivo=erro:..." se e sessao e falhou (inclusive
     timeout esperando o Build: a sessao e derrubada). Com true, `sessao`
     tem largura/altura/formatos para o app criar o backbuffer certo.
     timeoutMs = 0: nao espera o Build (Iniciar de 3 argumentos usa isso).
   - Fase 2: Iniciar(dev, ctx) depois do device. Abre fences e texturas no
     device do app. Iniciar(linha, dev, ctx) faz as duas fases em sequencia.
   - ObterQuadro devolve no maximo um quadro por vez: enquanto o app nao
     chamar EntregarSaida(n) do quadro obtido, devolve false. Os ponteiros
     do Quadro valem ate EntregarSaida (ou Parar); nao dar Release neles.
     EventoQuadro() e um evento auto-reset que dispara quando ha quadro novo
     (opcional, para a espera do laco principal).
   - EntregarSaida(resultado, n): `resultado` ja no formato e tamanho do
     Output (Sessao::formatoSaida, largura x altura; o blit e do A2), e a
     copia e um CopyResource. Se vier em outro formato/tamanho o header
     converte por compute shader (caminho de seguranca, com aviso no log).
     Sempre sinaliza out_fence, com ou sem imagem: e o que destrava o jogo.
     resultado=nullptr entrega "sem imagem".
   ===================================================================== */
#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cmath>      /* F0: std::ldexp na leitura de half (estatistica de faixa) */
#include <vector>     /* F0: linha do dump .ppm */
#include <atomic>
#include <mutex>

/* A funcao de log do app (main.cpp). O header e incluido antes da definicao. */
static void Log(const char* fmt, ...);

namespace PonteHost {

/* ------------------------------------------------------------------
   O protocolo v9, copiado de feed_ipc.h. Duplicado de proposito, como o
   shared_source.h duplica o cabecalho do add-on: a arvore do app nao inclui
   nada do Feeder. Os static_assert de tamanho abaixo sao a garantia de que a
   copia bate byte a byte com o que o add-on de 32 bits escreve no pipe.
   ------------------------------------------------------------------ */
static const uint32_t kMagic   = 0x35534C44u;   /* 'DLS5' */
static const uint32_t kVersao  = 9u;
static const uint32_t kHelloV1 = 3u * sizeof(uint32_t);

static const uint32_t kBuildHostCria     = 1u;   /* FEED_BUILD_HOST_CREATES  */
static const uint32_t kBuildSemUav       = 2u;   /* FEED_BUILD_OUTPUT_NO_UAV */
static const uint32_t kBuildAsyncHome    = 4u;   /* FEED_BUILD_ASYNC_HOME    */
static const uint32_t kAckSrIndisponivel = 2u;   /* FEED_ACK_SR_UNAVAILABLE  */

enum Slot { SLOT_COLOR = 0, SLOT_OUTPUT, SLOT_DEPTH, SLOT_MV, SLOTS };
enum Cliente { CLIENTE_D3D11 = 0, CLIENTE_GL = 1, CLIENTE_VULKAN = 2 };

#pragma pack(push, 1)
struct FeedHello    { uint32_t magic, version, pid, client_kind; uint64_t self_process; };
struct FeedHelloAck { uint32_t magic, version, panel_width, panel_height; };
struct FeedBuild
{
    uint32_t width, height, color_fmt, output_fmt;
    int32_t  hdr, depth_inverted, flags_override, transport;
    float    mv_scale_x, mv_scale_y;
    uint64_t tex[SLOTS];
    uint32_t client_flags;
    uint32_t target_width, target_height;
    uint64_t panel_tex;
};
struct FeedBuildAck
{
    int32_t  ok;
    uint32_t ngx_result;
    uint64_t fence_in, fence_out;
    uint64_t tex[SLOTS];
    uint64_t tex_size[SLOTS];
    uint32_t output_fmt, flags, sr_quality;
    uint64_t panel_tex, panel_size;
};
struct FeedWindowMsg { uint32_t width, height; };
struct FeedFrameMsg  { uint64_t n; uint32_t reset; float jitter_x, jitter_y; };
#pragma pack(pop)
static_assert(sizeof(FeedHello)     == 24,  "FeedHello v9 tem 24 bytes");
static_assert(sizeof(FeedHelloAck)  == 16,  "FeedHelloAck v9 tem 16 bytes");
static_assert(sizeof(FeedBuild)     == 92,  "FeedBuild v9 tem 92 bytes (93 com a tag)");
static_assert(sizeof(FeedBuildAck)  == 116, "FeedBuildAck v9 tem 116 bytes");
static_assert(sizeof(FeedFrameMsg)  == 20,  "FeedFrameMsg v9 tem 20 bytes (21 com a tag)");
static_assert(sizeof(FeedWindowMsg) == 8,   "FeedWindowMsg v9 tem 8 bytes");

/* J5 (18/09/2026): 'D' + FeedDadosMsg, ANTES do 'F' do mesmo quadro. A DLL diz que os slots
   Depth/MV deste quadro tem dado REAL do upscaler do jogo (na resolucao de render, canto
   superior esquerdo) e com que convencao. Espelho byte a byte de ponte/cliente/ponte_comum.h.
   So chega quando o nosso BuildAck levou kAckDados; quadro sem 'D' = slots sem dado valido. */
#pragma pack(push, 1)
struct FeedDadosMsg
{
    uint64_t n;
    uint32_t render_w, render_h;   /* tamanho de Depth neste quadro (MV: mv_w x mv_h) */
    uint32_t mv_w, mv_h;
    float    jitter_x, jitter_y, mv_scale_x, mv_scale_y, frame_dt_ms;
    float    cam_near, cam_far, cam_fov, view_to_m;   /* 0 = o jogo nao informou */
    uint32_t flags;                /* kDados* */
    uint32_t origem;               /* 1 dlss, 2 fsr31, 3 fsr2, 4 xess */
    int64_t  qpc_dispatch;         /* QPC do dispatch do upscaler no jogo (mesmo relogio): a idade do dado */
};
#pragma pack(pop)
static_assert(sizeof(FeedDadosMsg)  == 76,  "FeedDadosMsg (J5) tem 76 bytes (77 com a tag)");
static const uint32_t kAckDados        = 0x400u;   /* FeedBuildAck::flags: este host entende 'D' */
static const uint32_t kDadosReset      = 1u;       /* FeedDadosMsg::flags */
static const uint32_t kDadosDepthInv   = 2u;
static const uint32_t kDadosDepthIncog = 4u;
static const uint32_t kDadosMvTela     = 8u;
static const uint32_t kDadosMvJitter   = 16u;
static const uint32_t kDadosMvNdc      = 32u;

/* ------------------------------------------------------------------ */
/* A interface do CONTRATO.md (nomes e assinaturas fixos) + as duas     */
/* fases pedidas pelo coordenador (A2-integracao.md, item 1).           */
/* ------------------------------------------------------------------ */
/* J5: o que a DLL disse sobre depth/mv deste quadro (valido=false = slots sem dado; fluxo optico) */
struct DadosJogo {
    bool valido;
    UINT renderW, renderH, mvW, mvH;
    float jitterX, jitterY, mvScaleX, mvScaleY, dtMs;
    float camNear, camFar, camFov, viewToM;
    uint32_t flags;             /* kDados* */
    uint32_t origem;            /* 1 dlss, 2 fsr31, 3 fsr2, 4 xess */
    int64_t qpcDispatch;        /* QPC do dispatch do upscaler no jogo */
};
struct Quadro {                 // um quadro recebido do jogo, ja aberto no NOSSO device
    uint64_t n;                 // numero do quadro (valor da fence)
    ID3D11Texture2D* cor;       // FEED_COLOR   (sempre)
    ID3D11Texture2D* depth;     // FEED_DEPTH   (pode ser nullptr)
    ID3D11Texture2D* mv;        // FEED_MV      (pode ser nullptr)
    UINT largura, altura; DXGI_FORMAT formato;
    float jitterX, jitterY;     // FeedFrameMsg v6+
    int64_t qpcChegada;         // QueryPerformanceCounter no instante em que in_fence >= n foi observado
    DadosJogo dados;            // J5: depth/mv reais neste quadro? (ver DadosJogo)
};
struct Sessao {                 // o que o Build negociou (para o app criar o backbuffer certo)
    DWORD pidJogo;
    UINT largura, altura;       // resolucao de trabalho = tamanho de Color e de Output
    DXGI_FORMAT formatoCor, formatoSaida;
    int clienteKind;            // Cliente
    bool asyncHome;
};
// Fase 1, SEM device: le a linha de comando, sobe o servidor do pipe, faz Hello/HelloAck e
// Build/BuildAck (fences criadas num device D3D12 auxiliar), guarda os handles e devolve os
// dados da sessao. Bloqueia ate o Build chegar ou ate timeoutMs (0 = nao espera o Build).
static bool Preparar(const wchar_t* linhaDeComando, Sessao* out, DWORD timeoutMs);
// Fase 1 EM TEMPO DE EXECUCAO (Modo Integrado, Fase 2 do projeto): o app JA esta rodando
// quando o usuario manda integrar. Cria o pipe servidor para o pid do JOGO (que veio da
// janela capturada, nao da linha de comando) e sobe a thread, mas NAO bloqueia o laco de
// render esperando o hello/Build. Volta true assim que o pipe e a thread existem; o caller
// injeta a DLL e depois consulta ObterSessao() ate a sessao ficar pronta. timeoutMs = prazo
// da thread para a DLL conectar.
static bool PrepararEmExecucao(DWORD pidJogo, Sessao* out, DWORD timeoutMs);
// true (e preenche *out) quando o Build ja chegou e a sessao esta negociada (mesmo antes da
// fase 2). Para o laco consultar depois de PrepararEmExecucao + Injetar, sem bloquear.
static bool ObterSessao(Sessao* out);
// Fase 2: abre texturas e fences no device do app.
static bool Iniciar(ID3D11Device* dev, ID3D11DeviceContext* ctx);
// As duas fases em sequencia (a forma do CONTRATO; nao espera o Build).
static bool Iniciar(const wchar_t* linhaDeComando, ID3D11Device* dev, ID3D11DeviceContext* ctx);
static bool Ativa();
static bool ObterQuadro(Quadro* q);
static void EntregarSaida(ID3D11Texture2D* resultado, uint64_t n);
static void Parar();
static HANDLE EventoQuadro();   // extra: auto-reset, dispara quando ObterQuadro tem algo novo (nullptr sem sessao)

/* ------------------------------------------------------------------ */
/* Estado                                                              */
/* ------------------------------------------------------------------ */
template <class T> static void Soltar(T*& p) { if (p) { p->Release(); p = nullptr; } }
static void FecharHandle(HANDLE& h) { if (h != nullptr && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = nullptr; }

/* O conjunto de texturas de uma sessao (um Build). Tem geracao porque um
   rebuild pode chegar com um quadro do conjunto anterior ainda nas maos do
   app: o velho so e solto quando ninguem mais o usa. Copia rasa de
   proposito (os ponteiros mudam de dono); quem libera chama Liberar(). */
struct Conjunto
{
    uint32_t geracao = 0;
    bool     valido  = false;
    ID3D11Texture2D* tex[SLOTS] = { nullptr, nullptr, nullptr, nullptr };
    HANDLE   handleLocal[SLOTS] = { nullptr, nullptr, nullptr, nullptr };   /* so quando NOS criamos */
    ID3D11UnorderedAccessView* outUav = nullptr;   /* UAV do Output (ou do rascunho) */
    ID3D11Texture2D* rascunho = nullptr;           /* Output sem UAV (kBuildSemUav): CS escreve aqui, CopyResource leva */
    UINT w = 0, h = 0, outW = 0, outH = 0;
    DXGI_FORMAT corFmt = DXGI_FORMAT_UNKNOWN, outFmt = DXGI_FORMAT_UNKNOWN;
    bool asyncHome = false, transporte = false, hostCriou = false;

    void Liberar()
    {
        Soltar(outUav);
        Soltar(rascunho);
        for (int i = 0; i < SLOTS; ++i) { Soltar(tex[i]); FecharHandle(handleLocal[i]); }
        valido = false;
    }
};

struct Estado
{
    /* fase 2: devices do app */
    ID3D11Device*          dev  = nullptr;
    ID3D11DeviceContext*   ctx  = nullptr;
    ID3D11Device1*         dev1 = nullptr;
    ID3D11Device5*         dev5 = nullptr;
    ID3D11DeviceContext4*  ctx4 = nullptr;
    bool devPronto = false;
    /* fase 1: o auxiliar das fences */
    ID3D12Device* dev12  = nullptr;
    ID3D12Fence*  f12In  = nullptr;   /* vista D3D12: CPU (espera e signal de emergencia) */
    ID3D12Fence*  f12Out = nullptr;
    ID3D11Fence*  f11In  = nullptr;   /* vista D3D11 (fase 2): Signal ordenado no contexto imediato */
    ID3D11Fence*  f11Out = nullptr;
    HANDLE hIn = nullptr, hOut = nullptr;          /* NT handles locais das fences */
    uint64_t jogoIn = 0, jogoOut = 0;              /* valores validos no processo do jogo */
    /* pipe e thread */
    HANDLE pipe    = nullptr;
    HANDLE evIo    = nullptr;    /* OVERLAPPED das transferencias */
    HANDLE evStop  = nullptr;    /* Parar() */
    HANDLE evFence = nullptr;    /* SetEventOnCompletion da in_fence */
    HANDLE evBuild = nullptr;    /* manual: o primeiro Build chegou (Preparar espera nele) */
    HANDLE evQuadro = nullptr;   /* auto: ha quadro pronto */
    HANDLE thread  = nullptr;
    HANDLE hJogo   = nullptr;    /* FeedHello::self_process (DUP_HANDLE|QUERY_LIMITED: serve para DuplicateHandle) */
    HANDLE hJogoEspera = nullptr; /* duplicado com SYNCHRONIZE: o UNICO que entra em wait set (ver Handshake) */
    DWORD  pid     = 0;
    DWORD  conexaoMs = 0;        /* prazo da thread para a DLL conectar (0 = kEsperaConexaoMs). Modo integrado usa 5 s. */
    uint32_t cliente = CLIENTE_D3D11;
    /* o Build que chegou antes do device: montado na fase 2 */
    FeedBuild pendente = {};
    bool temPendente = false;
    Sessao sessao = {};
    bool temSessao = false;
    uint64_t soltosSemDevice = 0;  /* 'F' respondidos so com Signal, antes da fase 2 */
    /* conjuntos */
    Conjunto cj, cjVelho;
    uint32_t geracoes = 0;
    /* quadro pronto (a thread do pipe escreve, a principal consome) */
    bool     prontoValido = false;
    uint64_t prontoN = 0;
    int64_t  prontoQpc = 0;
    float    prontoJx = 0.f, prontoJy = 0.f;
    DadosJogo prontoDados = {};    /* J5: o 'D' que veio antes deste 'F' (valido=false sem 'D') */
    /* J5: o 'D' lido e ainda sem o seu 'F' (so na thread do pipe) */
    FeedDadosMsg dadosPend = {};
    bool     temDadosPend = false;
    uint64_t dadosRecebidos = 0;
    bool     dadosLogou = false;
    /* quadro em uso pelo app (entre ObterQuadro e EntregarSaida) */
    bool     emUso = false;
    uint64_t emUsoN = 0;
    uint32_t emUsoGeracao = 0;
    int64_t  emUsoQpc = 0;         /* qpcChegada do quadro em uso */
    int64_t  qpcObter = 0, qpcEntregar = 0;
    uint64_t ultimoRecebido = 0;   /* maior n lido do pipe */
    uint64_t ultimoSinal = 0;      /* maior n que sinalizamos em out_fence */
    uint64_t entregues = 0;        /* para a amostragem do log */
    /* copia para Output */
    ID3D11ComputeShader* cs = nullptr;
    ID3D11Buffer*        cb = nullptr;
    ID3D11SamplerState*  amostrador = nullptr;
    ID3D11Texture2D*     espelho = nullptr;   /* copia SRV do resultado quando ele nao tem SRV (backbuffer) */
    ID3D11ShaderResourceView* espelhoSrv = nullptr;
    bool csTentado = false;
    HMODULE compilador = nullptr;
    /* log */
    char motivo[160] = "";
    bool encerradaLogada = false;
    bool avisoW = false, avisoO = false, avisoEntrega = false, avisoCopia = false, avisoConversao = false;
    int64_t qpcFreq = 1;
};
static Estado g_ph;
static std::mutex g_mu;                     /* protege g_ph entre a thread do pipe e a principal */
static std::atomic<bool> g_ativa{ false };
/* G2 (fase 3): CALA A LINHA POR QUADRO. O `Log` do app faz fflush em cada linha, entao a linha
   `ponte: quadro N espera=... total=...` e uma gravacao em disco - nos primeiros 300 quadros,
   uma por quadro. Na fase 2 sozinha isso e barato e vale o que informa. Com a sessao upstream
   no ar o quadro da fase 2 volta intocado (nao ha o que medir nele) e o laco esta no caminho
   critico do quadro do JOGO: a gravacao passa a ser custo puro. Os contadores continuam. */
static std::atomic<bool> g_quieto{ false };
[[maybe_unused]] static void Silenciar(bool q) { g_quieto = q; }
/* R1: vigia de saude do Nativo (app). Timeouts de in_fence SEGUIDOS (sinal 2 = tela preta: o
   jogo parado esperando out_fence) e, so na bancada, uma saida que para de ser copiada
   (DLSS5_R1_CONGELAR: simula o congelamento para provar a vigia da DLL e o sinal 3). */
static std::atomic<int>  g_inFenceSeguidas{ 0 };
static std::atomic<bool> g_testeCongelar{ false };
[[maybe_unused]] static int  FalhasInFenceSeguidas() { return g_inFenceSeguidas.load(); }
[[maybe_unused]] static void TesteCongelarSaida(bool c) { g_testeCongelar = c; }

static int64_t Agora() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static double  Ms(int64_t de, int64_t ate) { return 1000.0 * double(ate - de) / double(g_ph.qpcFreq); }
static HANDLE   ComoHandle(uint64_t v) { return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(v)); }
static uint64_t ComoValor(HANDLE h)    { return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h)); }
static bool     PipeAberto()           { return g_ph.pipe != nullptr && g_ph.pipe != INVALID_HANDLE_VALUE; }

static const char* NomeCliente(uint32_t k) { return k == CLIENTE_GL ? "GL" : k == CLIENTE_VULKAN ? "VULKAN" : "D3D11"; }
static const char* NomeSlot(int s)
{
    return s == SLOT_COLOR ? "Color" : s == SLOT_OUTPUT ? "Output" : s == SLOT_DEPTH ? "Depth" : "MV";
}

/* O motivo do encerramento, formatado. snprintf e nao strcpy_s: o app nao
   usa a secure API e o MinGW so a expoe com define proprio. */
static void Motivo(const char* fmt, ...)
{
    /* "Parar" nunca sobrescreve um motivo ja anotado: quem chama Parar depois
       de um erro (timeout do Build, Iniciar que falhou) quer o erro no log,
       e a thread do pipe acorda pelo evStop achando que foi um Parar comum. */
    if (strcmp(fmt, "Parar") == 0 && g_ph.motivo[0] != '\0') return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_ph.motivo, sizeof(g_ph.motivo), fmt, ap);
    va_end(ap);
}

/* A linha final. So uma vez, com o primeiro motivo -- o segundo (Parar depois
   de "pipe fechado", por exemplo) nao conta a historia. */
static void LogEncerrada(const char* motivo)
{
    if (g_ph.encerradaLogada) return;
    g_ph.encerradaLogada = true;
    Log("ponte: encerrada motivo=%s", motivo);
}

/* Signal pela CPU: solta o jogo de qualquer thread. Chamar com g_mu. A
   fence e monotonica para quem espera; um valor menor que o ja sinalizado
   nao solta nada, entao so sobe. */
static void SinalCpu(uint64_t v)
{
    if (g_ph.f12Out == nullptr) return;
    if (v <= g_ph.ultimoSinal) return;
    /* R1: com o auxiliar removido (TDR) o Signal pode falhar; nao anota, e o Parar tenta pelo D3D11 */
    if (FAILED(g_ph.f12Out->Signal(v))) return;
    g_ph.ultimoSinal = v;
}

/* ------------------------------------------------------------------ */
/* Pipe: transferencias overlapped, bounded, canceladas por Parar()    */
/* ------------------------------------------------------------------ */
enum Xfer { XFER_OK, XFER_FECHADO, XFER_PARAR, XFER_JOGO_MORREU, XFER_TIMEOUT, XFER_ERRO };

/* Byte-mode pipe pode entregar leitura curta, dai o laco. A espera inclui o
   evento de Parar e o handle do processo do jogo: um jogo que morre sem
   fechar o pipe (kill) acorda a thread do mesmo jeito. */
static Xfer Transferir(void* buf, DWORD len, bool escrever, DWORD timeoutMs)
{
    BYTE* p = static_cast<BYTE*>(buf);
    while (len > 0)
    {
        OVERLAPPED ov = {};
        ov.hEvent = g_ph.evIo;
        ResetEvent(g_ph.evIo);
        const BOOL comecou = escrever ? WriteFile(g_ph.pipe, p, len, nullptr, &ov)
                                      : ReadFile(g_ph.pipe, p, len, nullptr, &ov);
        if (!comecou)
        {
            const DWORD e = GetLastError();
            if (e == ERROR_BROKEN_PIPE || e == ERROR_NO_DATA || e == ERROR_PIPE_NOT_CONNECTED) return XFER_FECHADO;
            if (e != ERROR_IO_PENDING) return XFER_ERRO;
        }
        /* hJogoEspera, e NUNCA hJogo: o self_process vem sem SYNCHRONIZE e o wait falha
           com ERROR_ACCESS_DENIED (WAIT_FAILED) - foi a queda da primeira fumaca (A5, 15/09:
           "timeout no pipe (tag)" 1 ms depois do hello). */
        HANDLE hs[3] = { g_ph.evIo, g_ph.evStop, g_ph.hJogoEspera };
        const DWORD n = g_ph.hJogoEspera != nullptr ? 3 : 2;
        const DWORD r = WaitForMultipleObjects(n, hs, FALSE, timeoutMs);
        const DWORD erroWait = (r == WAIT_FAILED) ? GetLastError() : 0;
        DWORD moveu = 0;
        if (r != WAIT_OBJECT_0)
        {
            CancelIoEx(g_ph.pipe, &ov);
            GetOverlappedResult(g_ph.pipe, &ov, &moveu, TRUE);   /* o cancel completa; nao vazar o OVERLAPPED */
            if (r == WAIT_OBJECT_0 + 1) return XFER_PARAR;
            if (r == WAIT_OBJECT_0 + 2) return XFER_JOGO_MORREU;
            if (r == WAIT_FAILED) { SetLastError(erroWait); return XFER_ERRO; }   /* nao e timeout: e um handle ruim no wait set */
            return XFER_TIMEOUT;
        }
        if (!GetOverlappedResult(g_ph.pipe, &ov, &moveu, FALSE) || moveu == 0)
        {
            const DWORD e = GetLastError();
            return (moveu == 0 || e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED) ? XFER_FECHADO : XFER_ERRO;
        }
        p   += moveu;
        len -= moveu;
    }
    return XFER_OK;
}
static Xfer Ler(void* buf, DWORD len, DWORD timeoutMs)            { return Transferir(buf, len, false, timeoutMs); }
static Xfer Escrever(const void* buf, DWORD len, DWORD timeoutMs) { return Transferir(const_cast<void*>(buf), len, true, timeoutMs); }

/* Traduz o resultado de uma transferencia num motivo de encerramento. Jogo
   morto e pipe quebrado sao a mesma coisa para quem le o log (T5). */
static void MotivoDeXfer(Xfer x, const char* onde)
{
    switch (x)
    {
    case XFER_FECHADO:     Motivo("pipe fechado"); break;
    case XFER_JOGO_MORREU: Log("ponte: o processo do jogo terminou (%s)", onde); Motivo("pipe fechado"); break;
    case XFER_PARAR:       Motivo("Parar"); break;
    case XFER_TIMEOUT:     Motivo("erro:timeout no pipe (%s)", onde); break;
    default:               Motivo("erro:pipe (%s) win32=%lu (WAIT_FAILED = handle sem SYNCHRONIZE no wait set)", onde, GetLastError()); break;
    }
}

/* ------------------------------------------------------------------ */
/* Texturas                                                            */
/* ------------------------------------------------------------------ */
static DXGI_FORMAT FormatoTipado(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}
/* CopyResource exige formatos identicos ou da mesma familia typeless. */
static bool MesmaFamilia(DXGI_FORMAT a, DXGI_FORMAT b) { return FormatoTipado(a) == FormatoTipado(b); }
/* F0 (15/09/2026, HDR de 16 bits na ponte): o Output que este host QUER para um dado
   formato de cor. Cor em 16F (scRGB) ou 10:10:10:2 volta no MESMO formato, sem tone
   mapping e sem clipping; qualquer cor de 8 bits volta em R8G8B8A8, como sempre. E a
   mesma regra do OutputFormatFor do cliente de referencia (dlss5-feed32.cpp) e do
   FormatoSaidaPara do fonte_ponte.h - os tres tem de concordar. O cliente cria a
   textura do Output ANTES do Build (o fio v9 leva o handle dela no proprio Build), entao
   e ele quem propoe; aqui a proposta e conferida contra esta regra e o ack a ecoa. */
static DXGI_FORMAT FormatoSaidaPara(DXGI_FORMAT cor)
{
    switch (FormatoTipado(cor))
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R11G11B10_FLOAT:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_UNORM:  return DXGI_FORMAT_R10G10B10A2_UNORM;
    default:                             return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}
static bool FormatoEhHdr(DXGI_FORMAT f)
{
    f = FormatoTipado(f);
    return f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R11G11B10_FLOAT || f == DXGI_FORMAT_R10G10B10A2_UNORM;
}
/* Uma view tem de repetir o formato do recurso quando ele e tipado; so um
   recurso TYPELESS aceita a view tipada. */
static DXGI_FORMAT FormatoDaView(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return FormatoTipado(f);
    default: return f;
    }
}

/* So quando o jogo pede kBuildHostCria (device de feature level 10.x no jogo,
   issue #33 do Feeder). Criada aqui em D3D11 com NT handle, e o jogo abre com
   OpenSharedResource1 -- D3D11 para D3D11, a direcao que o driver aceita. */
static ID3D11Texture2D* CriarCompartilhada(UINT w, UINT h, DXGI_FORMAT fmt, UINT bind, HANDLE* handleLocal)
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = bind;
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    ID3D11Texture2D* t = nullptr;
    HRESULT hr = g_ph.dev->CreateTexture2D(&td, nullptr, &t);
    if (FAILED(hr)) { Log("ponte: CreateTexture2D %ux%u fmt=%u bind=0x%X falhou 0x%08X", w, h, (unsigned)fmt, bind, (unsigned)hr); return nullptr; }
    IDXGIResource1* r = nullptr;
    hr = t->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(&r));
    if (SUCCEEDED(hr))
    {
        hr = r->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, handleLocal);
        r->Release();
    }
    if (FAILED(hr)) { Log("ponte: CreateSharedHandle falhou 0x%08X", (unsigned)hr); t->Release(); return nullptr; }
    return t;
}

/* Abre a textura que o jogo criou: duplica o handle DELE para ca e abre no
   nosso device. O handle local e fechado logo depois: a textura aberta
   segura a memoria sozinha. */
static ID3D11Texture2D* AbrirDoJogo(int slot, uint64_t valorNoJogo)
{
    if (valorNoJogo == 0) { Log("ponte: slot %s veio sem handle", NomeSlot(slot)); return nullptr; }
    HANDLE local = nullptr;
    if (!DuplicateHandle(g_ph.hJogo, ComoHandle(valorNoJogo), GetCurrentProcess(), &local, 0, FALSE, DUPLICATE_SAME_ACCESS))
    { Log("ponte: DuplicateHandle(%s) do jogo falhou %lu", NomeSlot(slot), GetLastError()); return nullptr; }
    ID3D11Texture2D* t = nullptr;
    const HRESULT hr = g_ph.dev1->OpenSharedResource1(local, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t));
    CloseHandle(local);
    if (FAILED(hr)) { Log("ponte: OpenSharedResource1(%s) falhou 0x%08X", NomeSlot(slot), (unsigned)hr); return nullptr; }
    return t;
}

static bool ConfereDesc(int slot, ID3D11Texture2D* t, UINT w, UINT h, DXGI_FORMAT fmt)
{
    D3D11_TEXTURE2D_DESC d = {};
    t->GetDesc(&d);
    if (d.Width == w && d.Height == h && d.Format == fmt) return true;
    Log("ponte: a textura %s do jogo e %ux%u fmt=%u, nao %ux%u fmt=%u", NomeSlot(slot),
        d.Width, d.Height, (unsigned)d.Format, w, h, (unsigned)fmt);
    return false;
}

/* Um UAV no Output (ou num rascunho do mesmo formato quando o jogo pediu
   Output sem UAV) para a copia com conversao de formato em EntregarSaida.
   Falhar aqui nao derruba o Build: sem UAV so resta CopyResource, que e o
   caminho previsto de qualquer jeito. */
static void PrepararUav(Conjunto& c, bool semUav)
{
    D3D11_TEXTURE2D_DESC d = {};
    c.tex[SLOT_OUTPUT]->GetDesc(&d);
    ID3D11Texture2D* alvo = c.tex[SLOT_OUTPUT];
    if (semUav || (d.BindFlags & D3D11_BIND_UNORDERED_ACCESS) == 0)
    {
        D3D11_TEXTURE2D_DESC rd = {};
        rd.Width = d.Width; rd.Height = d.Height; rd.MipLevels = 1; rd.ArraySize = 1;
        rd.Format = d.Format; rd.SampleDesc.Count = 1; rd.Usage = D3D11_USAGE_DEFAULT;
        rd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_ph.dev->CreateTexture2D(&rd, nullptr, &c.rascunho))) { c.rascunho = nullptr; return; }
        alvo = c.rascunho;
    }
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = FormatoDaView(d.Format);
    ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    if (FAILED(g_ph.dev->CreateUnorderedAccessView(alvo, &ud, &c.outUav)))
    {
        c.outUav = nullptr;
        Soltar(c.rascunho);
        Log("ponte: sem UAV tipado para o formato %u do Output; a entrega depende de CopyResource", (unsigned)d.Format);
    }
}

/* ------------------------------------------------------------------ */
/* Build: negocia (fase 1) e monta o conjunto (fase 2)                 */
/* ------------------------------------------------------------------ */

/* Espera o app largar o quadro do conjunto atual (ObterQuadro sem
   EntregarSaida ainda). Chamado SEM o mutex. */
static void EsperarSemUso(DWORD ms)
{
    const ULONGLONG limite = GetTickCount64() + ms;
    for (;;)
    {
        { std::lock_guard<std::mutex> l(g_mu); if (!g_ph.emUso) return; }
        if (GetTickCount64() >= limite) return;
        if (WaitForSingleObject(g_ph.evStop, 5) == WAIT_OBJECT_0) return;
    }
}

/* Um rebuild aposenta o conjunto atual. Se o app ainda segura um quadro
   dele, o conjunto vira "velho" e vive ate o EntregarSaida desse quadro (ou
   o Parar): nunca um ponteiro pendurado nas maos do app. Com g_mu. */
static void AposentarConjunto()
{
    if (g_ph.cjVelho.valido)
    {
        if (g_ph.emUso && g_ph.emUsoGeracao == g_ph.cjVelho.geracao)
            Log("ponte: dois rebuilds com o mesmo quadro %llu em uso; soltando o conjunto mais velho", (unsigned long long)g_ph.emUsoN);
        g_ph.cjVelho.Liberar();
    }
    if (g_ph.cj.valido && g_ph.emUso && g_ph.emUsoGeracao == g_ph.cj.geracao)
    {
        Log("ponte: rebuild com o quadro %llu ainda em uso pelo app; o conjunto velho fica vivo ate a entrega", (unsigned long long)g_ph.emUsoN);
        g_ph.cjVelho = g_ph.cj;
    }
    else g_ph.cj.Liberar();
    g_ph.cj = Conjunto();
    g_ph.prontoValido = false;
    g_ph.temPendente  = false;
    /* Solta o jogo de qualquer espera do conjunto que acabou. */
    SinalCpu(g_ph.ultimoRecebido);
}

/* Fase 2 de um Build: abre (ou cria) as texturas no device do app e deixa o
   conjunto valido. Com g_mu e com devPronto. `ack` pode ser nullptr quando o
   ack ja foi respondido na fase 1 (Build pendente). */
static bool MontarConjunto(const FeedBuild& b, FeedBuildAck* ack)
{
    Conjunto c;
    c.geracao    = ++g_ph.geracoes;
    c.w = b.width; c.h = b.height; c.outW = b.width; c.outH = b.height;
    c.corFmt     = static_cast<DXGI_FORMAT>(b.color_fmt);
    c.outFmt     = static_cast<DXGI_FORMAT>(b.output_fmt);
    c.asyncHome  = (b.client_flags & kBuildAsyncHome) != 0;
    c.transporte = b.transport != 0;
    c.hostCriou  = (b.client_flags & kBuildHostCria) != 0;
    const bool semUav = c.hostCriou && (b.client_flags & kBuildSemUav) != 0;

    bool ok = true;
    if (c.hostCriou)
    {
        /* O device do jogo recusou o conjunto: criamos aqui e duplicamos os
           handles PARA o jogo. Output com UAV so se o jogo consegue abrir
           uma textura com esse bind; senao SRV|RTV e o rascunho faz o UAV. */
        if (ack == nullptr) { Log("ponte: host_creates sem ack para responder (Build pendente): recusado"); return false; }
        const DXGI_FORMAT fmt[SLOTS] = { c.corFmt, c.outFmt, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_FLOAT };
        const UINT bindOut = static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE) |
                             static_cast<UINT>(semUav ? D3D11_BIND_RENDER_TARGET : D3D11_BIND_UNORDERED_ACCESS);
        const UINT bindGuia = static_cast<UINT>(D3D11_BIND_SHADER_RESOURCE) | static_cast<UINT>(D3D11_BIND_RENDER_TARGET);
        const UINT bind[SLOTS] = { bindGuia, bindOut, bindGuia, bindGuia };
        for (int i = 0; i < SLOTS && ok; ++i)
        {
            c.tex[i] = CriarCompartilhada(c.w, c.h, fmt[i], bind[i], &c.handleLocal[i]);
            if (c.tex[i] == nullptr) { ok = false; break; }
            HANDLE remoto = nullptr;
            if (!DuplicateHandle(GetCurrentProcess(), c.handleLocal[i], g_ph.hJogo, &remoto, 0, FALSE, DUPLICATE_SAME_ACCESS))
            { Log("ponte: DuplicateHandle(%s) para o jogo falhou %lu", NomeSlot(i), GetLastError()); ok = false; break; }
            ack->tex[i] = ComoValor(remoto);   /* tex_size[] e so para o import GL: fica 0 */
        }
        if (ok) Log("ponte: conjunto criado aqui e entregue ao jogo (%ux%u)%s", c.w, c.h, semUav ? " com o UAV do Output deste lado" : "");
    }
    else
    {
        c.tex[SLOT_COLOR]  = AbrirDoJogo(SLOT_COLOR,  b.tex[SLOT_COLOR]);
        c.tex[SLOT_OUTPUT] = AbrirDoJogo(SLOT_OUTPUT, b.tex[SLOT_OUTPUT]);
        c.tex[SLOT_DEPTH]  = AbrirDoJogo(SLOT_DEPTH,  b.tex[SLOT_DEPTH]);
        c.tex[SLOT_MV]     = AbrirDoJogo(SLOT_MV,     b.tex[SLOT_MV]);
        ok = c.tex[SLOT_COLOR] != nullptr && c.tex[SLOT_OUTPUT] != nullptr;
        if (ok) ok = ConfereDesc(SLOT_COLOR, c.tex[SLOT_COLOR], c.w, c.h, c.corFmt) &&
                     ConfereDesc(SLOT_OUTPUT, c.tex[SLOT_OUTPUT], c.outW, c.outH, c.outFmt);
        /* Depth e MV sao opcionais para o app: se nao bateram, viram nullptr. */
        if (c.tex[SLOT_DEPTH] != nullptr && !ConfereDesc(SLOT_DEPTH, c.tex[SLOT_DEPTH], c.w, c.h, DXGI_FORMAT_R32_FLOAT)) Soltar(c.tex[SLOT_DEPTH]);
        if (c.tex[SLOT_MV] != nullptr && !ConfereDesc(SLOT_MV, c.tex[SLOT_MV], c.w, c.h, DXGI_FORMAT_R16G16_FLOAT)) Soltar(c.tex[SLOT_MV]);
    }
    if (!ok) { c.Liberar(); return false; }

    PrepararUav(c, semUav);
    c.valido = true;
    g_ph.cj = c;
    Log("ponte: texturas abertas no device do app (geracao %u): Color=%p Output=%p Depth=%p MV=%p",
        c.geracao, (void*)c.tex[SLOT_COLOR], (void*)c.tex[SLOT_OUTPUT], (void*)c.tex[SLOT_DEPTH], (void*)c.tex[SLOT_MV]);
    return true;
}

static void GuardarSessao(const FeedBuild& b)
{
    g_ph.sessao.pidJogo      = g_ph.pid;
    g_ph.sessao.largura      = b.width;
    g_ph.sessao.altura       = b.height;
    g_ph.sessao.formatoCor   = static_cast<DXGI_FORMAT>(b.color_fmt);
    g_ph.sessao.formatoSaida = static_cast<DXGI_FORMAT>(b.output_fmt);
    g_ph.sessao.clienteKind  = static_cast<int>(g_ph.cliente);
    g_ph.sessao.asyncHome    = (b.client_flags & kBuildAsyncHome) != 0;
    g_ph.temSessao = true;
}

/* Thread do pipe. Decide o ack. Antes da fase 2 so guarda o Build (o ack ja
   vai com ok=1: fences e formatos nao dependem do device do app; se a
   abertura das texturas falhar depois, a sessao encerra com o motivo). */
static void TratarBuild(const FeedBuild& b, FeedBuildAck* ack)
{
    memset(ack, 0, sizeof(*ack));
    ack->fence_in   = g_ph.jogoIn;
    ack->fence_out  = g_ph.jogoOut;
    ack->output_fmt = b.output_fmt;
    ack->ngx_result = 1;   /* NVSDK_NGX_Result_Success */

    Log("ponte: build %ux%u color=%u output=%u hdr=%d inverted=%d transport=%d flags=0x%X target=%ux%u",
        b.width, b.height, b.color_fmt, b.output_fmt, b.hdr, b.depth_inverted, b.transport,
        b.client_flags, b.target_width, b.target_height);
    /* F0: o Output que este host quer para esta cor. O handle da textura ja veio no Build,
       entao o ack so pode ecoar o que o cliente criou; se o cliente propos outra coisa (dll
       anterior ao F0, que pedia RGBA8 para tudo), fica registrado o que isso custa. */
    {
        const DXGI_FORMAT cor = static_cast<DXGI_FORMAT>(b.color_fmt);
        const DXGI_FORMAT querido = FormatoSaidaPara(cor);
        if (static_cast<DXGI_FORMAT>(b.output_fmt) == querido)
            Log("ponte: output negociado fmt=%u = %s (cor fmt=%u)%s", b.output_fmt,
                FormatoEhHdr(cor) ? "o formato da cor, sem tone mapping" : "RGBA8 (cor SDR)", b.color_fmt,
                b.output_fmt == 10 ? " [16F]" : b.output_fmt == 24 ? " [10 bits]" : "");
        else
            Log("ponte: output proposto fmt=%u difere do que este host devolveria (fmt=%u) para a cor fmt=%u: ecoando a "
                "proposta (a textura ja existe)%s", b.output_fmt, (unsigned)querido, b.color_fmt,
                FormatoEhHdr(cor) && !FormatoEhHdr(static_cast<DXGI_FORMAT>(b.output_fmt))
                    ? " - a faixa acima do branco de referencia sera CORTADA na volta (dll anterior ao F0?)" : "");
    }

    EsperarSemUso(2000);
    std::lock_guard<std::mutex> l(g_mu);
    AposentarConjunto();

    if (g_ph.cliente != CLIENTE_D3D11)
    {
        Log("ponte: cliente %s precisa que o host crie as texturas em D3D12 (fora desta prova)", NomeCliente(g_ph.cliente));
        return;   /* ok=0 */
    }
    /* v6: Output maior que o trabalho = DLSS Super Resolution. O app nao faz
       isso; responder SR_UNAVAILABLE e o caminho do protocolo para o jogo
       refazer o Build como DLAA (Output == trabalho). */
    if (b.target_width != 0 && b.target_height != 0 && (b.target_width != b.width || b.target_height != b.height) && b.transport == 0)
    {
        Log("ponte: work_upscale=2 pedido (%ux%u -> %ux%u); respondendo SR_UNAVAILABLE para o jogo refazer como DLAA",
            b.width, b.height, b.target_width, b.target_height);
        ack->flags = kAckSrIndisponivel;
        return;
    }
    if (b.width == 0 || b.height == 0) { Log("ponte: build com tamanho zero"); return; }
    const bool hostCria = (b.client_flags & kBuildHostCria) != 0;

    if (!g_ph.devPronto)
    {
        /* host_creates precisa de um device para criar as texturas e o ack
           tem de levar os handles: sem device, ok=0; o add-on tenta de novo
           com recuo (1 s, 5 s, 30 s) e a proxima chega ja na fase 2. */
        if (hostCria) { Log("ponte: host_creates antes do device do app: recusado por ora; o jogo tenta de novo"); return; }
        if (b.tex[SLOT_COLOR] == 0 || b.tex[SLOT_OUTPUT] == 0) { Log("ponte: build sem handle de Color/Output"); return; }
        g_ph.pendente    = b;
        g_ph.temPendente = true;
    }
    else if (!MontarConjunto(b, ack)) return;

    GuardarSessao(b);
    ack->ok = 1;
    /* J5: e este bit que autoriza a DLL a mandar 'D' (depth/mv do upscaler nos slots). Uma DLL
       anterior ao J5 ignora o bit; uma nova com um app anterior nunca manda a tag. */
    ack->flags |= kAckDados;
    g_ph.temDadosPend = false; g_ph.prontoDados = DadosJogo{}; g_ph.dadosLogou = false;
    Log("ponte: sessao pid=%lu cliente=%s versao=%u %ux%u fmt=%u saida=%u async_home=%d (entendo 'D': depth/mv do jogo)",
        g_ph.pid, NomeCliente(g_ph.cliente), kVersao, b.width, b.height, b.color_fmt, b.output_fmt, g_ph.sessao.asyncHome ? 1 : 0);
    if (b.transport != 0) Log("ponte: modo transporte (mode=1 no cfg): o jogo espera Color copiado em Output; o app processa do mesmo jeito");
    SetEvent(g_ph.evBuild);
}

/* ------------------------------------------------------------------ */
/* A thread do pipe                                                    */
/* ------------------------------------------------------------------ */
static const DWORD kEsperaConexaoMs = 20000;   /* o jogo tenta por 15 s */
static const DWORD kEsperaHelloMs   = 15000;   /* kPipeHelloMs do cliente */
static const DWORD kEsperaBuildMs   = 60000;   /* kPipeBuildMs do cliente */
static const DWORD kEsperaFenceMs   = 5000;    /* in_fence >= n: so estoura se o jogo travou */

static bool Conectar()
{
    OVERLAPPED ov = {};
    ov.hEvent = g_ph.evIo;
    ResetEvent(g_ph.evIo);
    const BOOL c = ConnectNamedPipe(g_ph.pipe, &ov);
    const DWORD e = GetLastError();
    if (!c && e == ERROR_IO_PENDING)
    {
        HANDLE hs[2] = { g_ph.evIo, g_ph.evStop };
        const DWORD r = WaitForMultipleObjects(2, hs, FALSE, g_ph.conexaoMs != 0 ? g_ph.conexaoMs : kEsperaConexaoMs);
        DWORD m = 0;
        if (r != WAIT_OBJECT_0)
        {
            CancelIoEx(g_ph.pipe, &ov);
            GetOverlappedResult(g_ph.pipe, &ov, &m, TRUE);
            Motivo(r == WAIT_OBJECT_0 + 1 ? "Parar" : "erro:o jogo nao conectou no pipe em %lu ms", kEsperaConexaoMs);
            return false;
        }
        if (!GetOverlappedResult(g_ph.pipe, &ov, &m, FALSE)) { Motivo("erro:ConnectNamedPipe win32=%lu", GetLastError()); return false; }
    }
    else if (!c && e != ERROR_PIPE_CONNECTED)
    {
        Motivo("erro:ConnectNamedPipe win32=%lu", e);
        return false;
    }
    return true;
}

/* Hello: le so o que a versao do cliente escreveu (um v1 manda 12 bytes e
   bloquearia num read maior), responde SEMPRE com a nossa versao, e so
   depois recusa o que nao entende. */
static bool Handshake()
{
    FeedHello hello = {};
    Xfer x = Ler(&hello, kHelloV1, kEsperaHelloMs);
    if (x != XFER_OK) { MotivoDeXfer(x, "hello"); return false; }
    if (hello.magic != kMagic) { Motivo("erro:hello sem magic"); return false; }
    if (hello.version >= 2 && (x = Ler(&hello.client_kind, sizeof(hello.client_kind), kEsperaHelloMs)) != XFER_OK) { MotivoDeXfer(x, "hello client_kind"); return false; }
    if (hello.version >= 4 && (x = Ler(&hello.self_process, sizeof(hello.self_process), kEsperaHelloMs)) != XFER_OK) { MotivoDeXfer(x, "hello self_process"); return false; }

    FeedHelloAck ack = {};
    ack.magic = kMagic; ack.version = kVersao;   /* panel 0x0: sem painel */
    if ((x = Escrever(&ack, sizeof(ack), kEsperaHelloMs)) != XFER_OK) { MotivoDeXfer(x, "hello ack"); return false; }
    if (hello.version != kVersao)
    {
        Motivo("erro:add-on fala protocolo v%u, este host v%u", hello.version, kVersao);
        return false;
    }
    if (hello.pid != g_ph.pid)
        Log("ponte: o hello diz pid %u, a linha de comando %lu; seguindo com o do hello", hello.pid, g_ph.pid);
    g_ph.pid     = hello.pid;
    g_ph.cliente = hello.client_kind;

    /* O handle do processo do jogo, entregue por ele. Nunca OpenProcess. */
    if (hello.self_process == 0) { Motivo("erro:sem handle do processo do jogo (self_process=0)"); return false; }
    HANDLE h = ComoHandle(hello.self_process);
    if (GetProcessId(h) != g_ph.pid)
    {
        Motivo("erro:self_process nao nomeia o pid %lu (win32=%lu)", g_ph.pid, GetLastError());
        return false;
    }
    g_ph.hJogo = h;
    /* ESPERAR NO PROCESSO DO JOGO EXIGE SYNCHRONIZE, e o self_process nao tem: o add-on o
       duplica com PROCESS_DUP_HANDLE|PROCESS_QUERY_LIMITED_INFORMATION (feed32:2288). Com ele
       num wait set, WaitForMultipleObjects devolve WAIT_FAILED (ERROR_ACCESS_DENIED) na hora -
       e a primeira fumaca do A5 morreu assim, 1 ms depois do hello. Um DuplicateHandle do
       NOSSO handle pedindo so SYNCHRONIZE resolve (o acesso e conferido contra a DACL do
       processo do jogo, que e do mesmo usuario); nao e OpenProcess. Se o SO negar, a morte do
       jogo passa a ser vista so pelo pipe (que quebra junto), e o log diz isso. */
    {
        HANDLE hs = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &hs, SYNCHRONIZE, FALSE, 0) && hs != nullptr)
            g_ph.hJogoEspera = hs;
        else
            Log("ponte: nao consegui um handle com SYNCHRONIZE do processo do jogo (win32=%lu): a morte dele so sera vista pelo pipe", GetLastError());
    }

    /* As fences para o jogo: duplicadas com o handle que ele nos deu. */
    HANDLE jin = nullptr, jout = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), g_ph.hIn,  g_ph.hJogo, &jin,  0, FALSE, DUPLICATE_SAME_ACCESS) ||
        !DuplicateHandle(GetCurrentProcess(), g_ph.hOut, g_ph.hJogo, &jout, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        Motivo("erro:DuplicateHandle das fences para o jogo win32=%lu", GetLastError());
        return false;
    }
    g_ph.jogoIn = ComoValor(jin); g_ph.jogoOut = ComoValor(jout);
    Log("ponte: jogo pid %lu conectado (protocolo v%u, cliente %s)", g_ph.pid, hello.version, NomeCliente(g_ph.cliente));
    return true;
}

/* Espera in_fence >= n pela CPU, acordando tambem por Parar e pela morte do
   jogo. Devolve false se desistiu (ai o quadro e solto sem processar). */
static bool EsperarEntrada(uint64_t n)
{
    const ULONGLONG limite = GetTickCount64() + kEsperaFenceMs;
    for (;;)
    {
        if (g_ph.f12In->GetCompletedValue() >= n) return true;
        if (FAILED(g_ph.f12In->SetEventOnCompletion(n, g_ph.evFence))) return false;
        const ULONGLONG agora = GetTickCount64();
        if (agora >= limite) return false;
        HANDLE hs[3] = { g_ph.evFence, g_ph.evStop, g_ph.hJogoEspera };   /* hJogoEspera: o unico com SYNCHRONIZE */
        const DWORD r = WaitForMultipleObjects(g_ph.hJogoEspera != nullptr ? 3 : 2, hs, FALSE, static_cast<DWORD>(limite - agora));
        /* O evento e auto-reset e pode acordar por um disparo atrasado de uma
           espera anterior: so o valor da fence decide, dai reconferir. */
        if (r != WAIT_OBJECT_0) return false;
    }
}

static DWORD WINAPI ThreadPipe(void*)
{
    /* B9 (16/09/2026): o jogo espera por esta thread a cada quadro. Em prioridade normal ela
       perdia a vez para jogos que ocupam todos os nucleos. HIGHEST dentro da classe normal
       (nunca TIME_CRITICAL): a thread vive parada em espera de pipe/evento, e mesmo girando
       nao tira a vez do sistema. So existe enquanto o Nativo esta ligado. */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    if (Conectar() && Handshake()) for (;;)
    {
        BYTE tag = 0;
        Xfer x = Ler(&tag, 1, INFINITE);
        if (x != XFER_OK) { MotivoDeXfer(x, "tag"); break; }

        if (tag == 'B')
        {
            FeedBuild b = {};
            if ((x = Ler(&b, sizeof(b), kEsperaBuildMs)) != XFER_OK) { MotivoDeXfer(x, "build"); break; }
            FeedBuildAck ack;
            TratarBuild(b, &ack);
            if ((x = Escrever(&ack, sizeof(ack), kEsperaBuildMs)) != XFER_OK) { MotivoDeXfer(x, "build ack"); break; }
            if (ack.ok == 0 && g_ph.cliente != CLIENTE_D3D11)
            { Motivo("erro:cliente %s nao suportado nesta prova", NomeCliente(g_ph.cliente)); break; }
        }
        else if (tag == 'F')
        {
            FeedFrameMsg fm = {};
            if ((x = Ler(&fm, sizeof(fm), kEsperaBuildMs)) != XFER_OK) { MotivoDeXfer(x, "frame"); break; }
            bool temConjunto;
            {
                std::lock_guard<std::mutex> l(g_mu);
                g_ph.ultimoRecebido = fm.n;
                temConjunto = g_ph.cj.valido;
                if (!temConjunto)
                {
                    /* Sem conjunto (antes da fase 2, ou Build recusado): solta o
                       jogo na hora e segue. Ver secao 4 do topo. */
                    SinalCpu(fm.n);
                    if (!g_ph.devPronto) ++g_ph.soltosSemDevice;
                }
            }
            if (!temConjunto) continue;

            if (!EsperarEntrada(fm.n))
            {
                if (WaitForSingleObject(g_ph.evStop, 0) == WAIT_OBJECT_0) { Motivo("Parar"); break; }
                if (g_ph.hJogoEspera != nullptr && WaitForSingleObject(g_ph.hJogoEspera, 0) == WAIT_OBJECT_0) { MotivoDeXfer(XFER_JOGO_MORREU, "fence"); break; }
                Log("ponte: quadro %llu: in_fence nao chegou em %lu ms; soltando o jogo sem processar", (unsigned long long)fm.n, kEsperaFenceMs);
                ++g_inFenceSeguidas;   /* R1: sinal 2 da vigia */
                std::lock_guard<std::mutex> l(g_mu);
                g_ph.prontoValido = false;
                SinalCpu(fm.n);
                continue;
            }
            const int64_t chegada = Agora();
            g_inFenceSeguidas = 0;   /* R1 */
            std::lock_guard<std::mutex> l(g_mu);
            if (g_ph.prontoValido && !g_ph.emUso)
            {
                /* O app nao veio buscar o anterior (nao deveria acontecer: o jogo
                   so chega em n+1 depois do nosso out n). Solta e substitui. */
                Log("ponte: quadro %llu substituido por %llu sem ser processado", (unsigned long long)g_ph.prontoN, (unsigned long long)fm.n);
                SinalCpu(g_ph.prontoN);
            }
            g_ph.prontoValido = true;
            g_ph.prontoN   = fm.n;
            g_ph.prontoQpc = chegada;
            g_ph.prontoJx  = fm.jitter_x;
            g_ph.prontoJy  = fm.jitter_y;
            /* J5: o 'D' deste quadro (se veio) anda junto com ele; sem 'D', slots sem dado */
            g_ph.prontoDados = DadosJogo{};
            if (g_ph.temDadosPend && g_ph.dadosPend.n == fm.n)
            {
                const FeedDadosMsg& d = g_ph.dadosPend;
                DadosJogo& q = g_ph.prontoDados;
                q.valido = true;
                q.renderW = d.render_w; q.renderH = d.render_h; q.mvW = d.mv_w; q.mvH = d.mv_h;
                q.jitterX = d.jitter_x; q.jitterY = d.jitter_y; q.mvScaleX = d.mv_scale_x; q.mvScaleY = d.mv_scale_y; q.dtMs = d.frame_dt_ms;
                q.camNear = d.cam_near; q.camFar = d.cam_far; q.camFov = d.cam_fov; q.viewToM = d.view_to_m;
                q.flags = d.flags; q.origem = d.origem; q.qpcDispatch = d.qpc_dispatch;
                ++g_ph.dadosRecebidos;
                if (!g_ph.dadosLogou)
                {
                    g_ph.dadosLogou = true;
                    Log("ponte: depth+mv do jogo chegando pela fase 2 (origem %u: render %ux%u, mv %ux%u, mvscale %.4f,%.4f, flags 0x%X) - o FG da pelicula pode usar",
                        d.origem, d.render_w, d.render_h, d.mv_w, d.mv_h, d.mv_scale_x, d.mv_scale_y, d.flags);
                }
            }
            g_ph.temDadosPend = false;
            SetEvent(g_ph.evQuadro);
        }
        else if (tag == 'D')
        {
            /* J5: vem antes do 'F' do mesmo n; e guardado e casado la em cima */
            FeedDadosMsg dm = {};
            if ((x = Ler(&dm, sizeof(dm), kEsperaBuildMs)) != XFER_OK) { MotivoDeXfer(x, "dados"); break; }
            g_ph.dadosPend = dm; g_ph.temDadosPend = true;
        }
        else if (tag == 'W')
        {
            FeedWindowMsg wm = {};
            if ((x = Ler(&wm, sizeof(wm), kEsperaBuildMs)) != XFER_OK) { MotivoDeXfer(x, "window"); break; }
            if (!g_ph.avisoW) { g_ph.avisoW = true; Log("ponte: 'W' (janela %ux%u) ignorado nesta prova", wm.width, wm.height); }
        }
        else if (tag == 'O')
        {
            if (!g_ph.avisoO) { g_ph.avisoO = true; Log("ponte: 'O' (overlay do ReShade) ignorado nesta prova"); }
        }
        else
        {
            Motivo("erro:tag desconhecida 0x%02X", tag);
            break;
        }
    }

    /* Saida: o jogo pode ter um Wait(out_fence, n) enfileirado para cada 'F'
       que escreveu, lido ou nao. Soltar tudo antes de sumir, senao a fila
       dele (Present incluido) trava ate o TDR. */
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_ph.prontoValido = false;
        SinalCpu(UINT64_MAX);
        g_ativa = false;
        if (g_ph.motivo[0] == '\0') Motivo("erro:desconhecido");
        LogEncerrada(g_ph.motivo);
        SetEvent(g_ph.evBuild);    /* um Preparar parado na espera acorda e ve o erro */
        SetEvent(g_ph.evQuadro);   /* idem para o laco do app */
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Preparar (fase 1) / Iniciar (fase 2) / Parar                        */
/* ------------------------------------------------------------------ */

/* Le a linha de comando do jeito que o add-on a monta (secao 1 do topo).
   Devolve o pid, ou 0 quando nao ha token numerico = nao e sessao de ponte. */
static DWORD PidDaLinha(const wchar_t* linha)
{
    if (linha == nullptr) return 0;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(linha, &argc);
    if (argv == nullptr) return 0;
    DWORD pid = 0;
    /* UMA LINHA COM FONTE DE CAPTURA NUNCA E O HOST DO FEEDER (B5, fumaca 2 de 15/09): o app
       aberto com `--source window --window RESIDENT EVIL 2` (titulo sem aspas) entrava aqui
       como host do "jogo pid 2" e ficava 30 s esperando um hello que nunca vem. O add-on do
       Feeder lanca o host so com "<pid> --behind"; quem traz --source/--window/--monitor/
       --window-hwnd e o app normal, e um numero solto nessa linha e parte do titulo. */
    for (int i = 1; i < argc; ++i)
    {
        const wchar_t* a = argv[i];
        if (wcscmp(a, L"--source") == 0 || wcscmp(a, L"--window") == 0 ||
            wcscmp(a, L"--monitor") == 0 || wcscmp(a, L"--window-hwnd") == 0 ||
            /* A REABERTURA DO PROPRIO APP TAMBEM NAO E O HOST (C1, achado do B7): a linha
               "--reabrindo G --esperar-pid N --pre-fonte ..." nao tem --source, e o N (pid da
               copia velha) virava o "pid do jogo" - o app entrava como host, esperava 20 s por
               um hello que nunca vem e so entao seguia. */
            wcscmp(a, L"--reabrindo") == 0 || wcscmp(a, L"--esperar-pid") == 0 ||
            wcscmp(a, L"--pre-fonte") == 0)
        { LocalFree(argv); return 0; }
    }
    for (int i = 1; i < argc; ++i)
    {
        const wchar_t* a = argv[i];
        if (wcscmp(a, L"--behind") == 0 || wcscmp(a, L"--hide") == 0 || wcscmp(a, L"--gpu-priority") == 0 ||
            wcscmp(a, L"--test") == 0 || wcscmp(a, L"--ponte") == 0) continue;
        if (pid == 0 && a[0] >= L'0' && a[0] <= L'9')
        {
            wchar_t* fim = nullptr;
            const unsigned long v = wcstoul(a, &fim, 10);
            if (fim != nullptr && *fim == L'\0' && v != 0) pid = static_cast<DWORD>(v);
        }
    }
    LocalFree(argv);
    return pid;
}

static void LiberarTudo()
{
    g_ph.cj.Liberar();
    g_ph.cjVelho.Liberar();
    Soltar(g_ph.espelhoSrv);
    Soltar(g_ph.espelho);
    Soltar(g_ph.cs);
    Soltar(g_ph.cb);
    Soltar(g_ph.amostrador);
    Soltar(g_ph.f11In);
    Soltar(g_ph.f11Out);
    Soltar(g_ph.f12In);
    Soltar(g_ph.f12Out);
    Soltar(g_ph.dev12);
    Soltar(g_ph.ctx4);
    Soltar(g_ph.dev5);
    Soltar(g_ph.dev1);
    FecharHandle(g_ph.hIn);
    FecharHandle(g_ph.hOut);
    FecharHandle(g_ph.hJogo);
    FecharHandle(g_ph.hJogoEspera);
    FecharHandle(g_ph.pipe);
    FecharHandle(g_ph.evIo);
    FecharHandle(g_ph.evStop);
    FecharHandle(g_ph.evFence);
    FecharHandle(g_ph.evBuild);
    FecharHandle(g_ph.evQuadro);
    FecharHandle(g_ph.thread);
    if (g_ph.compilador != nullptr) { FreeLibrary(g_ph.compilador); g_ph.compilador = nullptr; }
    g_ph.dev = nullptr; g_ph.ctx = nullptr; g_ph.devPronto = false;
    g_ph.prontoValido = false; g_ph.emUso = false; g_ph.temPendente = false;
}

/* As fences (fase 1): nascem em D3D12, no adaptador padrao, e sao
   duplicadas para o jogo no handshake. Ver secao 3 do topo. */
static bool CriarFences(char* erro, size_t erroLen)
{
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&g_ph.dev12));
    if (FAILED(hr) || g_ph.dev12 == nullptr) { snprintf(erro, erroLen, "D3D12CreateDevice (auxiliar, so fences) falhou 0x%08X", (unsigned)hr); return false; }
    if (FAILED(g_ph.dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&g_ph.f12In))) ||
        FAILED(g_ph.dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&g_ph.f12Out))))
    { snprintf(erro, erroLen, "CreateFence D3D12 compartilhada falhou"); return false; }
    if (FAILED(g_ph.dev12->CreateSharedHandle(g_ph.f12In,  nullptr, GENERIC_ALL, nullptr, &g_ph.hIn)) ||
        FAILED(g_ph.dev12->CreateSharedHandle(g_ph.f12Out, nullptr, GENERIC_ALL, nullptr, &g_ph.hOut)))
    { snprintf(erro, erroLen, "CreateSharedHandle das fences falhou"); return false; }
    return true;
}

static bool Falhou(const char* erro)
{
    char m[256];
    snprintf(m, sizeof(m), "erro:%s", erro);
    std::lock_guard<std::mutex> l(g_mu);
    SinalCpu(UINT64_MAX);
    g_ativa = false;
    LogEncerrada(m);
    return false;
}

static bool Preparar(const wchar_t* linhaDeComando, Sessao* out, DWORD timeoutMs)
{
    if (out != nullptr) *out = Sessao();
    const DWORD pid = PidDaLinha(linhaDeComando);
    if (pid == 0) return false;   /* nao e sessao de ponte: o app segue como sempre */
    if (g_ativa)
    {
        /* Ja preparado (ou ja na fase 2): so devolve a sessao, esperando o Build se for o caso. */
        if (timeoutMs != 0 && g_ph.evBuild != nullptr) WaitForSingleObject(g_ph.evBuild, timeoutMs);
        std::lock_guard<std::mutex> l(g_mu);
        if (out != nullptr) *out = g_ph.sessao;
        return g_ph.temSessao || timeoutMs == 0;
    }
    if (g_ph.thread != nullptr) Parar();   /* sobras de uma sessao anterior */

    g_ph = Estado();
    g_ph.pid = pid;
    { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_ph.qpcFreq = f.QuadPart > 0 ? f.QuadPart : 1; }
    char erro[200] = "";
    bool ok = true;

    /* O pipe PRIMEIRO: o jogo esta contando 15 s desde o CreateProcess. */
    char nome[128];
    snprintf(nome, sizeof(nome), "\\\\.\\pipe\\dlss5-feed.%lu", pid);
    /* IN 256 bytes = 12 mensagens de quadro de folga; e o que limita o
       run-ahead do jogo (issue #15 do Feeder). OUT 1024 cabe o FeedBuildAck. */
    g_ph.pipe = CreateNamedPipeA(nome, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1024, 256, 0, nullptr);
    if (g_ph.pipe == INVALID_HANDLE_VALUE) { snprintf(erro, sizeof(erro), "CreateNamedPipe %s falhou %lu", nome, GetLastError()); ok = false; }
    Log("ponte: preparando pid=%lu pipe=%s linha=%ls", pid, nome, linhaDeComando);

    if (ok) ok = CriarFences(erro, sizeof(erro));
    if (ok)
    {
        g_ph.evIo     = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
        g_ph.evStop   = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
        g_ph.evFence  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_ph.evBuild  = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
        g_ph.evQuadro = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (g_ph.evIo == nullptr || g_ph.evStop == nullptr || g_ph.evFence == nullptr || g_ph.evBuild == nullptr || g_ph.evQuadro == nullptr)
        { snprintf(erro, sizeof(erro), "CreateEvent falhou"); ok = false; }
    }
    if (ok)
    {
        g_ativa = true;
        g_ph.thread = CreateThread(nullptr, 0, &ThreadPipe, nullptr, 0, nullptr);
        if (g_ph.thread == nullptr) { g_ativa = false; snprintf(erro, sizeof(erro), "CreateThread falhou"); ok = false; }
    }
    if (!ok) { Falhou(erro); LiberarTudo(); return false; }

    if (timeoutMs == 0) { if (out != nullptr) { out->pidJogo = pid; } return true; }

    /* Espera o primeiro Build (ou a thread morrer, que tambem dispara evBuild). */
    WaitForSingleObject(g_ph.evBuild, timeoutMs);
    bool temSessao;
    { std::lock_guard<std::mutex> l(g_mu); temSessao = g_ph.temSessao; if (out != nullptr) *out = g_ph.sessao; }
    if (temSessao) return true;
    if (g_ativa)
    {
        /* Timeout de verdade: derruba a sessao com o motivo, para o app nao
           ficar com um host meio vivo que ele nao sabe dimensionar. */
        Log("ponte: o Build nao chegou em %lu ms", timeoutMs);
        Motivo("erro:timeout de %lu ms esperando o Build", timeoutMs);
    }
    Parar();   /* a thread ja encerrou (e logou o motivo) ou encerra agora */
    return false;
}

/* Fase 1 em tempo de execucao. Mesma montagem do Preparar (pipe, fences, eventos, thread),
   mas o pid vem direto (o app nao foi lancado pelo add-on) e NADA bloqueia o laco: sobe a
   thread e volta. O caller injeta a DLL e depois espera a sessao por fora (ObterSessao).
   conexaoMs vira o prazo da thread para a DLL conectar (o caller tambem tem o dele). */
static bool PrepararEmExecucao(DWORD pidJogo, Sessao* out, DWORD timeoutMs)
{
    if (out != nullptr) *out = Sessao();
    if (pidJogo == 0) return false;
    if (g_ativa || g_ph.thread != nullptr) Parar();   /* sobras de uma sessao anterior */

    g_ph = Estado();
    g_inFenceSeguidas = 0; g_testeCongelar = false;   /* R1 */
    g_ph.pid = pidJogo;
    g_ph.conexaoMs = timeoutMs;
    { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_ph.qpcFreq = f.QuadPart > 0 ? f.QuadPart : 1; }
    char erro[200] = "";
    bool ok = true;

    /* O pipe PRIMEIRO: a DLL, ao subir, tenta CreateFile nele. */
    char nome[128];
    snprintf(nome, sizeof(nome), "\\\\.\\pipe\\dlss5-feed.%lu", pidJogo);
    g_ph.pipe = CreateNamedPipeA(nome, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 1024, 256, 0, nullptr);
    if (g_ph.pipe == INVALID_HANDLE_VALUE) { snprintf(erro, sizeof(erro), "CreateNamedPipe %s falhou %lu", nome, GetLastError()); ok = false; }
    Log("ponte: preparando em execucao pid=%lu pipe=%s (o app injeta a dll a seguir)", pidJogo, nome);

    if (ok) ok = CriarFences(erro, sizeof(erro));
    if (ok)
    {
        g_ph.evIo     = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
        g_ph.evStop   = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
        g_ph.evFence  = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_ph.evBuild  = CreateEventW(nullptr, TRUE,  FALSE, nullptr);
        g_ph.evQuadro = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (g_ph.evIo == nullptr || g_ph.evStop == nullptr || g_ph.evFence == nullptr || g_ph.evBuild == nullptr || g_ph.evQuadro == nullptr)
        { snprintf(erro, sizeof(erro), "CreateEvent falhou"); ok = false; }
    }
    if (ok)
    {
        g_ativa = true;
        g_ph.thread = CreateThread(nullptr, 0, &ThreadPipe, nullptr, 0, nullptr);
        if (g_ph.thread == nullptr) { g_ativa = false; snprintf(erro, sizeof(erro), "CreateThread falhou"); ok = false; }
    }
    if (!ok) { Falhou(erro); LiberarTudo(); return false; }
    if (out != nullptr) out->pidJogo = pidJogo;
    return true;   /* nao bloqueia: a sessao fica pronta quando o Build chegar (ObterSessao) */
}

static bool ObterSessao(Sessao* out)
{
    if (!g_ativa) return false;
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_ph.temSessao) return false;
    if (out != nullptr) *out = g_ph.sessao;
    return true;
}

static bool Iniciar(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    if (!g_ativa) { Log("ponte: Iniciar(dev, ctx) sem Preparar antes (ou a sessao ja encerrou)"); return false; }
    if (dev == nullptr || ctx == nullptr) return Falhou("Iniciar sem device");
    std::lock_guard<std::mutex> l(g_mu);
    if (g_ph.devPronto) return true;
    g_ph.dev = dev; g_ph.ctx = ctx;
    char erro[200] = "";
    bool ok = true;
    if (ok && (FAILED(dev->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&g_ph.dev1))) || g_ph.dev1 == nullptr))
    { snprintf(erro, sizeof(erro), "o device nao tem ID3D11Device1"); ok = false; }
    if (ok && (FAILED(dev->QueryInterface(__uuidof(ID3D11Device5), reinterpret_cast<void**>(&g_ph.dev5))) || g_ph.dev5 == nullptr))
    { snprintf(erro, sizeof(erro), "o device nao tem ID3D11Device5 (Windows 10 1703+)"); ok = false; }
    if (ok && (FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), reinterpret_cast<void**>(&g_ph.ctx4))) || g_ph.ctx4 == nullptr))
    { snprintf(erro, sizeof(erro), "o contexto nao tem ID3D11DeviceContext4"); ok = false; }
    if (ok)
    {
        /* As fences do auxiliar, abertas no device do app. Fence compartilhada
           nao cruza adaptadores: o LUID entra no motivo quando falha. */
        HRESULT hr = g_ph.dev5->OpenSharedFence(g_ph.hIn, __uuidof(ID3D11Fence), reinterpret_cast<void**>(&g_ph.f11In));
        if (SUCCEEDED(hr)) hr = g_ph.dev5->OpenSharedFence(g_ph.hOut, __uuidof(ID3D11Fence), reinterpret_cast<void**>(&g_ph.f11Out));
        if (FAILED(hr))
        {
            LUID app = {};
            IDXGIDevice* dxdev = nullptr; IDXGIAdapter* ad = nullptr; DXGI_ADAPTER_DESC ad_desc = {};
            if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxdev))) && dxdev != nullptr)
            {
                if (SUCCEEDED(dxdev->GetAdapter(&ad)) && ad != nullptr) { ad->GetDesc(&ad_desc); app = ad_desc.AdapterLuid; ad->Release(); }
                dxdev->Release();
            }
            const LUID aux = g_ph.dev12->GetAdapterLuid();
            snprintf(erro, sizeof(erro), "ID3D11Device5::OpenSharedFence falhou 0x%08X (adaptador do app %08lX:%08lX, do auxiliar %08lX:%08lX)",
                     (unsigned)hr, (unsigned long)app.HighPart, app.LowPart, (unsigned long)aux.HighPart, aux.LowPart);
            ok = false;
        }
    }
    if (ok)
    {
        g_ph.devPronto = true;
        if (g_ph.soltosSemDevice != 0)
            Log("ponte: %llu quadros soltos sem processar entre Preparar e Iniciar", (unsigned long long)g_ph.soltosSemDevice);
        if (g_ph.temPendente)
        {
            g_ph.temPendente = false;
            if (!MontarConjunto(g_ph.pendente, nullptr)) { snprintf(erro, sizeof(erro), "nao abri as texturas do jogo no device do app"); ok = false; }
        }
    }
    if (!ok)
    {
        g_ph.devPronto = false;
        char m[256];
        snprintf(m, sizeof(m), "erro:%s", erro);
        Motivo("%s", m);
        SetEvent(g_ph.evStop);   /* a thread do pipe encerra e loga o motivo */
        return false;
    }
    Log("ponte: device do app ligado (fase 2)");
    return true;
}

/* [[maybe_unused]] (A5, 15/09): o main.cpp usa as duas fases separadas (Preparar no
   comeco do wWinMain, Iniciar(dev, ctx) depois do R.Init); esta forma fica pelo CONTRATO
   e para quem nao precisar do tamanho antes do device. Sem o atributo, -Wall acusa
   "defined but not used" - seria o unico warning novo do build. */
[[maybe_unused]] static bool Iniciar(const wchar_t* linhaDeComando, ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    Sessao s;
    if (!g_ativa && !Preparar(linhaDeComando, &s, 0)) return false;
    return Iniciar(dev, ctx);
}

static bool Ativa() { return g_ativa; }
static HANDLE EventoQuadro() { return g_ph.evQuadro; }

/* R1 (16/09/2026): o device do app ainda vive? O auxiliar D3D12 e o MESMO device do feed
   (D3D12 da um device por adaptador no processo; a fase 2 exige o mesmo adaptador), entao um
   TDR na rede aparece aqui; o D3D11 da fase 2 tambem. S_OK = vivo. Barato: para o tique de 1 s. */
[[maybe_unused]] static HRESULT MotivoDeviceRemovido()
{
    std::lock_guard<std::mutex> l(g_mu);
    if (g_ph.dev12 != nullptr) { const HRESULT r = g_ph.dev12->GetDeviceRemovedReason(); if (FAILED(r)) return r; }
    if (g_ph.dev   != nullptr) { const HRESULT r = g_ph.dev->GetDeviceRemovedReason();   if (FAILED(r)) return r; }
    return S_OK;
}

/* R1, SO BANCADA (DLSS5_R1_DERRUBAR no ambiente): remove o device D3D12 do processo - o mesmo
   do feed - como o TDR do RE2 fez, para provar a deteccao no alvo sintetico sem esperar um erro
   de GPU de verdade. Devolve false se o device nao expoe ID3D12Device5. */
[[maybe_unused]] static bool DerrubarDeviceParaTeste()
{
    std::lock_guard<std::mutex> l(g_mu);
    if (g_ph.dev12 == nullptr) return false;
    ID3D12Device5* d5 = nullptr;
    if (FAILED(g_ph.dev12->QueryInterface(__uuidof(ID3D12Device5), reinterpret_cast<void**>(&d5))) || d5 == nullptr) return false;
    d5->RemoveDevice();
    d5->Release();
    return true;
}

static void Parar()
{
    if (g_ph.evStop == nullptr && g_ph.thread == nullptr) return;
    if (g_ph.evStop != nullptr) SetEvent(g_ph.evStop);
    if (PipeAberto()) CancelIoEx(g_ph.pipe, nullptr);
    if (g_ph.thread != nullptr && WaitForSingleObject(g_ph.thread, 10000) != WAIT_OBJECT_0)
        Log("ponte: a thread do pipe nao terminou em 10 s");
    {
        std::lock_guard<std::mutex> l(g_mu);
        SinalCpu(UINT64_MAX);
        /* R1: o auxiliar morreu e o Signal pela CPU nao saiu - solta o jogo pela vista D3D11
           da mesma fence (Parar e da thread principal, dona do contexto imediato). */
        if (g_ph.ultimoSinal != UINT64_MAX && g_ph.ctx4 != nullptr && g_ph.f11Out != nullptr)
        {
            g_ph.ctx4->Signal(g_ph.f11Out, UINT64_MAX);
            g_ph.ctx->Flush();
            g_ph.ultimoSinal = UINT64_MAX;
        }
        g_ativa = false;
        LogEncerrada(g_ph.motivo[0] != '\0' ? g_ph.motivo : "Parar");
    }
    if (PipeAberto()) DisconnectNamedPipe(g_ph.pipe);
    LiberarTudo();
}

/* ------------------------------------------------------------------ */
/* Thread principal: ObterQuadro / EntregarSaida                        */
/* ------------------------------------------------------------------ */
static bool ObterQuadro(Quadro* q)
{
    if (q == nullptr || !g_ativa) return false;
    std::lock_guard<std::mutex> l(g_mu);
    if (g_ph.emUso || !g_ph.prontoValido || !g_ph.cj.valido) return false;
    const Conjunto& c = g_ph.cj;
    q->n        = g_ph.prontoN;
    q->cor      = c.tex[SLOT_COLOR];
    q->depth    = c.tex[SLOT_DEPTH];
    q->mv       = c.tex[SLOT_MV];
    q->largura  = c.w;
    q->altura   = c.h;
    q->formato  = c.corFmt;
    q->jitterX  = g_ph.prontoJx;
    q->jitterY  = g_ph.prontoJy;
    q->qpcChegada = g_ph.prontoQpc;
    q->dados    = g_ph.prontoDados;   /* J5 */
    if (q->depth == nullptr || q->mv == nullptr) q->dados.valido = false;   /* sem os slots abertos nao ha o que ler */
    g_ph.prontoValido = false;
    g_ph.emUso        = true;
    g_ph.emUsoN       = q->n;
    g_ph.emUsoGeracao = c.geracao;
    g_ph.emUsoQpc     = g_ph.prontoQpc;
    g_ph.qpcObter     = Agora();
    return true;
}

/* O compute shader da copia de seguranca: le o resultado por SRV (bilinear,
   entao tambem redimensiona) e escreve no UAV do Output. O caminho previsto
   e o resultado ja no formato/tamanho do Output (o blit e do A2) e um
   CopyResource; isto cobre o que vier diferente, com aviso no log.
   Compilado uma vez, com o d3dcompiler_47 carregado a mao como o main.cpp faz. */
typedef HRESULT (WINAPI* PonteCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

static bool PrepararCopia()
{
    if (g_ph.cs != nullptr) return true;
    if (g_ph.csTentado) return false;
    g_ph.csTentado = true;
    static const char kSrc[] =
        "cbuffer CB : register(b0) { uint2 dstSize; uint2 pad; };\n"
        "Texture2D<float4> src : register(t0);\n"
        "SamplerState samp : register(s0);\n"
        "RWTexture2D<float4> dst : register(u0);\n"
        "[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "  if (id.x >= dstSize.x || id.y >= dstSize.y) return;\n"
        "  float2 uv = (float2(id.xy) + 0.5) / float2(dstSize);\n"
        "  dst[id.xy] = src.SampleLevel(samp, uv, 0);\n"
        "}\n";
    if (g_ph.compilador == nullptr) g_ph.compilador = LoadLibraryW(L"d3dcompiler_47.dll");
    PonteCompile compile = g_ph.compilador != nullptr ? reinterpret_cast<PonteCompile>(reinterpret_cast<void*>(GetProcAddress(g_ph.compilador, "D3DCompile"))) : nullptr;
    if (compile == nullptr) { Log("ponte: d3dcompiler_47.dll indisponivel; a entrega depende de CopyResource"); return false; }
    ID3DBlob* cod = nullptr; ID3DBlob* err = nullptr;
    HRESULT hr = compile(kSrc, sizeof(kSrc) - 1, "ponte_cs", nullptr, nullptr, "main", "cs_5_0", 0, 0, &cod, &err);
    if (FAILED(hr))
    {
        Log("ponte: compilacao do CS de copia falhou 0x%08X: %s", (unsigned)hr, err != nullptr ? static_cast<const char*>(err->GetBufferPointer()) : "");
        Soltar(err);
        return false;
    }
    Soltar(err);
    hr = g_ph.dev->CreateComputeShader(cod->GetBufferPointer(), cod->GetBufferSize(), nullptr, &g_ph.cs);
    cod->Release();
    if (FAILED(hr)) { Log("ponte: CreateComputeShader falhou 0x%08X", (unsigned)hr); return false; }
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(g_ph.dev->CreateBuffer(&bd, nullptr, &g_ph.cb))) { Soltar(g_ph.cs); return false; }
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(g_ph.dev->CreateSamplerState(&sd, &g_ph.amostrador))) { Soltar(g_ph.cs); Soltar(g_ph.cb); return false; }
    return true;
}

/* SRV do resultado. Um backbuffer nasce so com RENDER_TARGET_OUTPUT, sem
   SRV: nesse caso passa por uma copia ("espelho") do mesmo formato.
   O chamador solta a view devolvida. */
static ID3D11ShaderResourceView* SrvDoResultado(ID3D11Texture2D* r, const D3D11_TEXTURE2D_DESC& rd)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {};
    vd.Format = FormatoDaView(rd.Format);
    vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    vd.Texture2D.MipLevels = 1;
    if (rd.BindFlags & D3D11_BIND_SHADER_RESOURCE)
    {
        ID3D11ShaderResourceView* srv = nullptr;
        if (SUCCEEDED(g_ph.dev->CreateShaderResourceView(r, &vd, &srv))) return srv;
    }
    D3D11_TEXTURE2D_DESC ed = {};
    if (g_ph.espelho != nullptr) g_ph.espelho->GetDesc(&ed);
    if (g_ph.espelho == nullptr || ed.Width != rd.Width || ed.Height != rd.Height || ed.Format != rd.Format)
    {
        Soltar(g_ph.espelhoSrv);
        Soltar(g_ph.espelho);
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = rd.Width; td.Height = rd.Height; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = rd.Format; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_ph.dev->CreateTexture2D(&td, nullptr, &g_ph.espelho))) { g_ph.espelho = nullptr; return nullptr; }
        if (FAILED(g_ph.dev->CreateShaderResourceView(g_ph.espelho, &vd, &g_ph.espelhoSrv))) { g_ph.espelhoSrv = nullptr; Soltar(g_ph.espelho); return nullptr; }
    }
    g_ph.ctx->CopyResource(g_ph.espelho, r);
    g_ph.espelhoSrv->AddRef();
    return g_ph.espelhoSrv;
}

/* Leva o resultado para o Output do conjunto c. Caminho previsto:
   CopyResource (tamanho e familia de formato batem). Senao o CS de
   seguranca, com aviso uma vez no log. */
static bool CopiarParaOutput(ID3D11Texture2D* resultado, Conjunto& c)
{
    D3D11_TEXTURE2D_DESC rd = {};
    resultado->GetDesc(&rd);
    const bool mesmoTamanho = rd.Width == c.outW && rd.Height == c.outH;
    if (mesmoTamanho && MesmaFamilia(rd.Format, c.outFmt) && rd.SampleDesc.Count == 1)
    {
        g_ph.ctx->CopyResource(c.tex[SLOT_OUTPUT], resultado);
        return true;
    }
    if (!g_ph.avisoConversao)
    {
        g_ph.avisoConversao = true;
        Log("ponte: resultado %ux%u fmt=%u nao e o formato/tamanho do Output (%ux%u fmt=%u): convertendo por compute shader",
            rd.Width, rd.Height, (unsigned)rd.Format, c.outW, c.outH, (unsigned)c.outFmt);
    }
    if (c.outUav == nullptr || !PrepararCopia())
    {
        if (!g_ph.avisoCopia)
        {
            g_ph.avisoCopia = true;
            Log("ponte: sem CS/UAV para converter: a entrega sai sem imagem");
        }
        return false;
    }
    ID3D11ShaderResourceView* srv = SrvDoResultado(resultado, rd);
    if (srv == nullptr) return false;
    const UINT dims[4] = { c.outW, c.outH, 0, 0 };
    g_ph.ctx->UpdateSubresource(g_ph.cb, 0, nullptr, dims, 0, 0);
    ID3D11UnorderedAccessView* uav = c.outUav;
    g_ph.ctx->CSSetShader(g_ph.cs, nullptr, 0);
    g_ph.ctx->CSSetConstantBuffers(0, 1, &g_ph.cb);
    g_ph.ctx->CSSetSamplers(0, 1, &g_ph.amostrador);
    g_ph.ctx->CSSetShaderResources(0, 1, &srv);
    g_ph.ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    g_ph.ctx->Dispatch((c.outW + 7) / 8, (c.outH + 7) / 8, 1);
    /* Desligar tudo do estagio CS: o app nao conta com nada preso aqui, e um
       UAV/SRV pendurado num recurso que ele vai usar como RTV e um hazard. */
    ID3D11UnorderedAccessView* nuav = nullptr;
    ID3D11ShaderResourceView*  nsrv = nullptr;
    g_ph.ctx->CSSetUnorderedAccessViews(0, 1, &nuav, nullptr);
    g_ph.ctx->CSSetShaderResources(0, 1, &nsrv);
    g_ph.ctx->CSSetShader(nullptr, nullptr, 0);
    srv->Release();
    if (c.rascunho != nullptr) g_ph.ctx->CopyResource(c.tex[SLOT_OUTPUT], c.rascunho);
    return true;
}

/* F0: ESTATISTICA DE FAIXA (a prova do HDR sem clipping). Ligada por
   DLSS5_PONTE_ESTATISTICA=1 no ambiente do app. Em algumas entregas (60, 120, 300 e
   depois a cada 600) le Color e Output de volta para a CPU (staging + Map: sincroniza a
   GPU, custa alguns ms - so diagnostico) e loga o maximo por canal e quantos pixels passam
   de 1.0. Com a cor em 16F e o Output em 16F os dois maximos batem acima de 1.0; com o
   Output em RGBA8 o maximo do Output fica preso em 1.0 - e o clipping que o F0 elimina. */
static int g_estatistica = -1;   /* -1 = ambiente nao lido; 0 = desligada; 1 = ligada */
static float MeioParaFloat(uint16_t h)
{
    const uint32_t s = (h >> 15) & 1u, e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
    float v;
    if (e == 0)        v = (float)m * (1.0f / 16777216.0f);           /* subnormal: m * 2^-24 */
    else if (e == 31)  v = m ? 0.0f : 3.0e38f;                        /* inf/NaN: NaN vira 0 */
    else               v = (float)(m | 0x400u) * std::ldexp(1.0f, (int)e - 25);
    return s ? -v : v;
}
static void EstatisticaTextura(ID3D11Texture2D* t, const char* rotulo, uint64_t n)
{
    if (t == nullptr) return;
    D3D11_TEXTURE2D_DESC d = {}; t->GetDesc(&d);
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
    ID3D11Texture2D* stg = nullptr;
    if (FAILED(g_ph.dev->CreateTexture2D(&sd, nullptr, &stg)) || stg == nullptr) return;
    g_ph.ctx->CopyResource(stg, t);
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (SUCCEEDED(g_ph.ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m)))
    {
        float mx[3] = { 0, 0, 0 }; uint32_t acima = 0, total = 0;
        const DXGI_FORMAT f = FormatoTipado(d.Format);
        for (UINT y = 0; y < d.Height; y += 2)   /* metade das linhas basta para a conta */
        {
            const BYTE* linha = static_cast<const BYTE*>(m.pData) + (size_t)y * m.RowPitch;
            for (UINT x = 0; x < d.Width; x += 2)
            {
                float c[3];
                if (f == DXGI_FORMAT_R16G16B16A16_FLOAT)
                {
                    const uint16_t* p = reinterpret_cast<const uint16_t*>(linha) + (size_t)x * 4;
                    c[0] = MeioParaFloat(p[0]); c[1] = MeioParaFloat(p[1]); c[2] = MeioParaFloat(p[2]);
                }
                else if (f == DXGI_FORMAT_R10G10B10A2_UNORM)
                {
                    const uint32_t v = *(reinterpret_cast<const uint32_t*>(linha) + x);
                    c[0] = (float)(v & 1023u) / 1023.0f; c[1] = (float)((v >> 10) & 1023u) / 1023.0f; c[2] = (float)((v >> 20) & 1023u) / 1023.0f;
                }
                else
                {
                    const BYTE* p = linha + (size_t)x * 4;   /* RGBA8 ou BGRA8: o maximo nao depende da ordem */
                    c[0] = p[0] / 255.0f; c[1] = p[1] / 255.0f; c[2] = p[2] / 255.0f;
                }
                for (int k = 0; k < 3; ++k) if (c[k] > mx[k]) mx[k] = c[k];
                if (c[0] > 1.0f || c[1] > 1.0f || c[2] > 1.0f) ++acima;
                ++total;
            }
        }
        /* F0: comparacao VISUAL. Com DLSS5_PONTE_DUMP_DIR=<pasta>, a textura inteira vai para
           <pasta>\ponte-<rotulo>-<n>.ppm com a cor dividida por DLSS5_PONTE_DUMP_ESCALA
           (padrao 2.0 = o --pico do alvo): 2.0 vira branco, entao um topo achatado em 1.0
           aparece como cinza medio chapado, e o gradiente inteiro aparece continuo. So 16F. */
        {
            char dir[MAX_PATH] = ""; const DWORD ld = GetEnvironmentVariableA("DLSS5_PONTE_DUMP_DIR", dir, sizeof(dir));
            if (ld > 0 && ld < sizeof(dir) && f == DXGI_FORMAT_R16G16B16A16_FLOAT)
            {
                char esc[16] = ""; float escala = 2.0f;
                if (GetEnvironmentVariableA("DLSS5_PONTE_DUMP_ESCALA", esc, sizeof(esc)) > 0) escala = (float)atof(esc);
                if (!(escala > 0.0f)) escala = 2.0f;
                char nome[MAX_PATH + 64]; const char* r2 = strchr(rotulo, '(') ? "" : rotulo;
                snprintf(nome, sizeof(nome), "%s\\ponte-%.*s-%llu.ppm", dir, (int)(strchr(rotulo, '(') ? strchr(rotulo, '(') - rotulo : (int)strlen(r2)), rotulo, (unsigned long long)n);
                FILE* fp = fopen(nome, "wb");
                if (fp != nullptr)
                {
                    fprintf(fp, "P6\n%u %u\n255\n", d.Width, d.Height);
                    std::vector<unsigned char> linhaOut((size_t)d.Width * 3);
                    for (UINT y = 0; y < d.Height; ++y)
                    {
                        const uint16_t* p = reinterpret_cast<const uint16_t*>(static_cast<const BYTE*>(m.pData) + (size_t)y * m.RowPitch);
                        for (UINT x = 0; x < d.Width; ++x)
                            for (int k = 0; k < 3; ++k)
                            {
                                float v = MeioParaFloat(p[(size_t)x * 4 + k]) / escala;
                                v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
                                linhaOut[(size_t)x * 3 + k] = (unsigned char)(v * 255.0f + 0.5f);
                            }
                        fwrite(linhaOut.data(), 1, linhaOut.size(), fp);
                    }
                    fclose(fp);
                    Log("ponte: dump %s (cor / %.2f)", nome, escala);
                }
            }
        }
        g_ph.ctx->Unmap(stg, 0);
        Log("ponte: estatistica quadro %llu %s fmt=%u %ux%u max=(%.3f %.3f %.3f) acima_de_1=%u de %u amostrados (%.1f%%)",
            (unsigned long long)n, rotulo, (unsigned)d.Format, d.Width, d.Height, mx[0], mx[1], mx[2],
            acima, total, total ? 100.0 * acima / total : 0.0);
    }
    stg->Release();
}
static void EstatisticaDaEntrega(Conjunto& c, uint64_t n, uint64_t k)
{
    if (g_estatistica < 0)
    {
        char v[8] = ""; DWORD len = GetEnvironmentVariableA("DLSS5_PONTE_ESTATISTICA", v, sizeof(v));
        g_estatistica = (len > 0 && len < sizeof(v) && v[0] == '1') ? 1 : 0;
        if (g_estatistica) Log("ponte: estatistica de faixa LIGADA (DLSS5_PONTE_ESTATISTICA=1): Color e Output lidos de volta em algumas entregas");
    }
    if (g_estatistica != 1) return;
    if (!(k == 60 || k == 120 || k == 300 || (k % 600) == 0)) return;
    EstatisticaTextura(c.tex[SLOT_COLOR],  "cor(entrada)", n);
    EstatisticaTextura(c.tex[SLOT_OUTPUT], "output(saida)", n);
}

static void EntregarSaida(ID3D11Texture2D* resultado, uint64_t n)
{
    std::lock_guard<std::mutex> l(g_mu);
    if (!g_ph.emUso || n != g_ph.emUsoN)
    {
        if (!g_ph.avisoEntrega)
        {
            g_ph.avisoEntrega = true;
            Log("ponte: EntregarSaida(%llu) sem ObterQuadro correspondente (em uso: %llu)",
                (unsigned long long)n, (unsigned long long)(g_ph.emUso ? g_ph.emUsoN : 0));
        }
        return;
    }
    g_ph.qpcEntregar = Agora();
    Conjunto* c = nullptr;
    if (g_ph.cj.valido && g_ph.cj.geracao == g_ph.emUsoGeracao) c = &g_ph.cj;
    else if (g_ph.cjVelho.valido && g_ph.cjVelho.geracao == g_ph.emUsoGeracao) c = &g_ph.cjVelho;
    /* Um rebuild no meio: o jogo ja largou o Output velho; so o Signal importa. */
    if (c != nullptr && resultado != nullptr && g_ph.f11Out != nullptr && !g_testeCongelar.load())   /* R1: bancada */
    {
        CopiarParaOutput(resultado, *c);
        EstatisticaDaEntrega(*c, n, g_ph.entregues + 1);   /* F0: so com DLSS5_PONTE_ESTATISTICA=1 */
    }
    if (g_ph.ctx4 != nullptr && g_ph.f11Out != nullptr)
    {
        /* Ordenado atras de toda leitura de Color n e da copia para Output. */
        g_ph.ctx4->Signal(g_ph.f11Out, n);
        g_ph.ctx->Flush();
        if (n > g_ph.ultimoSinal) g_ph.ultimoSinal = n;
    }
    const int64_t fim = Agora();
    g_ph.emUso = false;
    if (c == &g_ph.cjVelho) g_ph.cjVelho.Liberar();   /* o app largou o ultimo quadro do conjunto velho */

    /* espera = do in_fence >= n visto ate ObterQuadro devolver; proc = ate
       EntregarSaida ser chamada; entrega = ate o Signal enfileirado e Flush;
       total = soma. Todos os 300 primeiros (o medir.ps1 e o T8 da bancada leem estes);
       B10 (16/09/2026): o 1 em 10 dali em diante (~6 gravacoes por segundo no laco, durante
       o jogo inteiro) so com DLSS5_PONTE_LOG_QUADROS=1, que e diagnostico. */
    static const bool todosOsQuadros = []{ wchar_t e[8]{}; return GetEnvironmentVariableW(L"DLSS5_PONTE_LOG_QUADROS", e, 8) > 0 && e[0] == L'1'; }();
    const uint64_t k = ++g_ph.entregues;
    if ((k <= 300 || (todosOsQuadros && (k % 10) == 0)) && !g_quieto.load())
    {
        const double espera  = Ms(g_ph.emUsoQpc, g_ph.qpcObter);
        const double proc    = Ms(g_ph.qpcObter, g_ph.qpcEntregar);
        const double entrega = Ms(g_ph.qpcEntregar, fim);
        Log("ponte: quadro %llu espera=%.2f proc=%.2f entrega=%.2f total=%.2f",
            (unsigned long long)n, espera, proc, entrega, espera + proc + entrega);
    }
}

} // namespace PonteHost
