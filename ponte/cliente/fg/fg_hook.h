/* =====================================================================
   fg_hook.h -- FASE 4 (Q4, 16/09/2026): frame generation D3D12 DENTRO do
   jogo, com o frame interpolation do FidelityFX 3.1 (amd_fidelityfx_dx12.dll
   1.1.4, MIT). A DLL vira dona da apresentacao e insere um quadro
   interpolado entre cada dois quadros reais do jogo.

   DESENHO APROVADO (PLANO-FASES-3-4.md, secao Fase 4; pesquisa em
   pesquisa-modo-integrado\C-framegen.md secao 1, o FrameInterpolationSwapchain
   da AMD):
     - Nao da para trocar a swapchain de um jogo ja aberto. Entao, a partir do
       PRIMEIRO ResizeBuffers depois de integrar, a DLL engancha GetBuffer (e
       GetCurrentBackBufferIndex) e entrega ao jogo TEXTURAS NOSSAS (proxies);
       o jogo desenha nelas sem saber. No Present, a DLL NAO apresenta: ela
       gera o quadro interpolado (FFX) e enfileira o par {interpolado, real}
       para uma thread de ritmo. A thread apresenta o interpolado, espera
       metade do intervalo medido por QPC, e apresenta o real -- copiando cada
       um para o backbuffer REAL (GetBuffer original) e chamando o Present
       original na fila do jogo. Fences separadas: "quadro pronto" (jogo ->
       ritmo) e "consumido" (ritmo -> jogo, contrapressao).
     - Dados: MV/depth vem da sessao .up quando o jogo tem upscaler (Ngx::Up);
       sem eles, so fluxo optico (qualidade inferior).
     - Chave fg=0|1|auto no arquivo de controle %TEMP%\dlss5-ponte.<pid>.ngx
       (auto = liga quando ha MV/depth pela .up; sem dados so se fg=1). O
       amd_fidelityfx_dx12.dll e carregado por CAMINHO ABSOLUTO passado no
       mesmo arquivo (fg_dll=...), nunca da pasta do jogo.
     - Multiplicador 2x nesta rodada. Sem ResizeBuffers, o FG fica AGUARDANDO
       (a pessoa alterna Alt+Enter, muda a resolucao, ou abre o jogo pelo app).

   REGRA DURA: nada aqui derruba o jogo. Qualquer falha -> Desistir(): a DLL
   larga a apresentacao e volta ao Present direto (fase 2/3 de sempre).
   ===================================================================== */
#pragma once

#include <windows.h>
#include <cstdint>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace Fg {

typedef void (*LogFn)(const char* fmt, ...);
/* J4 (18/09/2026): registro de um estouro (falha de hardware) numa thread NOSSA do FG, no
   erros-nativo.log da ponte: onde, codigo, endereco e o que a DLL fez a respeito. */
typedef void (*EstouroFn)(const char* onde, DWORD code, void* addr, const char* efeito);

void Iniciar(LogFn log, EstouroFn estouro = nullptr);
void Servico(bool jogoD3D11 = false);           /* thread da ponte, ~250 ms: le a chave fg e publica o estado */
/* J4: a imagem do jogo esta parada (quadros identicos ha mais de 1,5 s), vista pela sonda do
   proprio FG - vale so com o FG dono, quando a vigia do R1 (ProcessarQuadro) nao roda. */
bool ImagemParada();

/* P1 (16/09/2026): o estado do FG para o app, anexado ao .ngx-estado pelo Ngx:
   fg_estado=aguardando|ligado|desligado, fg_motivo=<codigo>, fg_refs=<referencias do jogo ao
   backbuffer 0, -1 = nao medido>. EstadoVersao muda a cada mudanca.
   J1 (18/09/2026): mais tres, para o card do app parar de depender do Afterburner:
   fg_dados=optico|jogo (de onde vem o movimento), fg_fps_jogo e fg_fps_tela (janela de 1 s;
   0 = sem FG dono). Cabe em 256 bytes. */
unsigned EstadoVersao();
void EstadoTexto(char* buf, size_t n);
/* P1: thread do Present, antes do quadro, com o FG pedido e ainda nao dono: mede UMA vez quantas
   referencias o jogo segura ao backbuffer (so diagnostico; ver P1-ritmo.md, Alt+Enter). */
void Sondar(IDXGISwapChain* sc);
void FilaVista(ID3D12CommandQueue* fila);       /* C11: registra as filas DIRECT do ECL; a primeira fica de reserva */
ID3D12CommandQueue* FilaDoSwapchain(IDXGISwapChain* sc);   /* C11: fila em que o swapchain apresenta (AddRef) ou nullptr */
/* logo depois do ResizeBuffers original. B19: do ResizeBuffers1, filaPedida = a fila comum a todos
   os buffers (ou nullptr) e filasMistas = filas ou nos diferentes por buffer (o FG nao assume). */
void AposResize(IDXGISwapChain* sc, IUnknown* filaPedida = nullptr, bool filasMistas = false);
void AntesResize();                             /* logo antes: solta proxies/backbuffers */

/* =====================================================================
   J3 (18/09/2026): PROFUNDIDADE E VETORES DE MOVIMENTO REAIS DO JOGO.
   Quem enganchou o upscaler do jogo (ngx_upstream para DLSS, sr_hook para
   FSR 2/3.1 e XeSS) ja copia depth e MV para texturas nossas, no device e na
   fila DO JOGO. Este e o balcao em que esses dados sao deixados para o FG.
   Contrato:
     - PublicarDados roda na THREAD DO JOGO, dentro do dispatch do upscaler,
       depois de a lista de copia ter sido submetida. O Fg toma a sua propria
       referencia (AddRef) e a segura ate trocar de pacote ou Limpar.
     - LimparDados antes de liberar as texturas da sessao do upscaler.
     - QuerDados() e falso com o FG desligado no controle: nesse caso publicar
       e desperdicio no caminho quente do jogo.
   Tudo o que vai aqui esta NA RESOLUCAO DE RENDER do jogo (ou de tela, quando
   mvDisplayRes), que e o que o ffxDispatchDescFrameGenerationPrepare pede
   (ffx_framegeneration.h:127). */
struct DadosJogo
{
    ID3D12Resource* depth = nullptr;        /* R32_FLOAT, renderW x renderH (ou maior) */
    ID3D12Resource* mv = nullptr;           /* R16G16_FLOAT, mvW x mvH (ou maior) */
    ID3D12CommandQueue* fila = nullptr;     /* fila em que as copias foram gravadas */
    unsigned renderW = 0, renderH = 0, mvW = 0, mvH = 0;
    float jitterX = 0, jitterY = 0;         /* mesmo jitter passado ao upscaler */
    float mvScaleX = 1, mvScaleY = 1;       /* escala que leva o MV a pixels de render */
    float dtMs = 0;                         /* frameTimeDelta do jogo (ms); 0 = nao informado */
    float camNear = 0, camFar = 0, camFov = 0, viewToM = 0;   /* 0 = o jogo nao informou */
    int   depthInv = -1;                    /* 1 invertida, 0 normal, -1 desconhecida */
    bool  mvDisplayRes = false;             /* MV em resolucao de tela, nao de render */
    bool  mvJittered = false;               /* o jitter esta embutido no MV */
    bool  mvNdc = false;                    /* MV em NDC (XeSS): convencao nao resolvida */
    bool  reset = false;                    /* corte de cena declarado pelo jogo */
    const char* origem = "-";               /* literal estatico: dlss|fsr31|fsr2|xess */
};
bool QuerDados();                               /* o FG esta pedido no controle (fg=1|auto) */
void PublicarDados(const DadosJogo& d);         /* thread do jogo, no dispatch do upscaler */
void LimparDados();                             /* a sessao do upscaler vai soltar as texturas */

bool DonoDaApresentacao();                      /* true = a thread de ritmo cuida do Present */
HRESULT Presentar(IDXGISwapChain* sc, UINT sync, UINT flags, void* origPresent);
void Recolher();                                /* C3: thread do Present; desmonta o que o ritmo largou ao desistir */
bool Espelhar(IDXGISwapChain* sc);              /* C10: Present direto apos desistir: proxy -> backbuffer real; true = copiou */
void ApiDoJogo(bool d3d12);                     /* API vista no 1o Present; sai sempre no .ngx-estado (jogo_api=) */
void AntesTelaCheia();                          /* C9: hook de SetFullscreenState, antes da original */
void AposTelaCheia(IDXGISwapChain* sc);         /* C9: depois da original */
uint64_t PresentsNaTela();                      /* C2: quadros apresentados pelo ritmo (acumulado) */

/* C13: descarga da ponte com o jogo ainda desenhando nos proxies */
bool JogoNosProxies();                          /* FG dono ou espelho e o jogo segura referencia ao proxy 0 */
void Largar();                                  /* thread da ponte, hooks drenados: desiste mantendo o espelho; nao remonta mais */
bool Espelhando();                              /* o espelho segue ligado (o ResizeBuffers do jogo desliga) */

/* C16: o espelho passa da dll que descarrega para a copia que assume (as referencias vao junto) */
struct Espelho
{
    ID3D12Device* dev; IDXGISwapChain* sc; ID3D12CommandQueue* fila;
    ID3D12GraphicsCommandList* listaCopia; ID3D12Fence* fCopia; HANDLE evCopia; uint64_t copiaVal;
    UINT nBuf; ID3D12Resource* proxy[8]; ID3D12CommandAllocator* allocCopia[8]; uint64_t copiaFeita[8];
};
bool Entregar(Espelho* e);                      /* hooks restaurados e nenhum Present no espelho: move o espelho para e */
void Herdar(const Espelho& e);                  /* copia, antes de instalar os hooks: segue espelhando */

/* Overrides de vtable: devolvem true quando preencheram (FG dono); senao a
   ponte chama o trampolim original. */
bool GetBufferProxy(IDXGISwapChain* sc, UINT buf, REFIID riid, void** pp);
bool IndiceProxy(UINT* out);

void Originais(void* origGetBuffer, void* origGetCurBBIndex);
void Parar();

} // namespace Fg
