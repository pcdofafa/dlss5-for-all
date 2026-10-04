/* =====================================================================
   colheita.h -- X-FG-6 (18/09/2026): PROFUNDIDADE E VETORES DE MOVIMENTO
   DO JOGO SEM A SESSAO `.up` (a leva "2 + H1 fundidas" do X-FG-PLANO).

   Ate aqui o depth e os MV so existiam com a sessao upstream de pe: as
   copias moravam em sr_hook::AntesD12/D11 e ngx_upstream::AntesD12Impl/
   D11Impl, que so rodam com estado == E_PRONTO (X-FG-5 §6 item 1). No
   Nativo comum (Upstream desligado, ou o app sem servir o .up, ou a sessao
   caida) o balcao ficava vazio: o FG da DLL e a pelicula (fase 2, tag 'D')
   voltavam ao fluxo optico.

   Este modulo e a COLHEITA: quem enganchou o upscaler (os dois modulos
   acima) chama Publicar() a cada dispatch em que NAO esta servindo a sessao
   .up, e a colheita copia depth e MV para texturas SUAS e avisa os dois
   consumidores de sempre (Fg::PublicarDados, Ponte::Dados::Publicar). A
   imagem do jogo nao e tocada: nada e trocado, a chamada original segue.

   ONDE AS COPIAS SAO GRAVADAS - e por que e diferente da sessao .up:
     D3D12: NA COMMAND LIST DO JOGO, logo antes do dispatch do upscaler
            (como a segunda copia da prova de prontidao). A sessao .up
            submete uma lista propria ANTES da do jogo porque o app espera
            pela cor; aqui ninguem espera, e gravar na lista do jogo e o que
            garante que depth e MV sao os DESTE quadro, sem depender da
            prova de prontidao (cena e dispatch na mesma lista = a copia
            separada leria o quadro anterior). Barreiras: os estados que o
            dispatch declara (os mesmos que o upscaler vai usar) e volta.
            A conversao por compute (D24S8/D32S8/D16 -> R32F; MV fora de
            R16G16F -> R16G16F) tambem vai na lista do jogo: o estado de
            compute que ela pisa e o mesmo que o upscaler pisa logo depois
            (FSR/XeSS/DLSS montam heaps, root signature e PSO proprios e nao
            restauram nada).
            X-CRASH-3 (18/09, principio do dono "a gente nunca toca no jogo"):
            o recurso do jogo e so LIDO, por uma SRV criada sobre ele no estado
            que o upscaler ja exige (NPSR). Nenhuma barreira nele, nenhuma
            copia dele, nenhuma intermediaria: compute le a SRV e escreve no
            nosso slot; eDepth/eMv da Entrada sao ignorados. Recurso que nega
            SRV = sem dado neste quadro. O residuo na lista e o estado de
            compute (heaps/RS/PSO), que o upscaler pisa logo a seguir.
     D3D11: no contexto imediato, antes do dispatch (como a sessao .up); a
            copia e leitura pura e o estado de CS volta como estava.
   Os consumidores leem os slots no Present (mesma fila): a lista do jogo
   com o dispatch e submetida antes do Present, entao a ordem esta dada.

   TAMANHO DOS SLOTS: o tamanho de SAIDA do jogo (o render nunca e maior),
   ou o do recurso do jogo se for maior. So cresce; encolher nao refaz. Um
   conjunto trocado por um maior fica num cemiterio ate uma fence propria
   (dois quadros depois) e 1 s dizerem que nenhuma lista o usa.

   CHAVE: `[ultima] dados_sem_upstream=0` no dlss5forall.ini desliga a
   colheita (o comportamento anterior: dado so com a sessao .up).
   Regra dura: nada aqui derruba o jogo - try/catch proprio, falha = sem
   dado neste quadro, log uma vez.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <cstdint>

namespace Ponte { namespace Colheita {

typedef void (*LogFn)(const char* fmt, ...);

struct Entrada
{
    bool d3d12 = true;
    ID3D12GraphicsCommandList* lista = nullptr;   /* D3D12: a lista do jogo (a copia vai nela, antes do dispatch) */
    ID3D12CommandQueue* fila = nullptr;           /* D3D12: a fila em que essa lista executa (a dos consumidores) */
    ID3D11DeviceContext* ctx = nullptr;           /* D3D11: o contexto imediato */
    IUnknown* depth = nullptr; IUnknown* mv = nullptr;   /* recursos do jogo */
    D3D12_RESOURCE_STATES eDepth = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, eMv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;   /* X-CRASH-3: ignorados (leitura por SRV, sem barreira) */
    unsigned rw = 0, rh = 0;                      /* renderSize deste dispatch */
    unsigned outW = 0, outH = 0;                  /* tamanho de saida do jogo (teto dos slots) */
    unsigned bxDepth = 0, byDepth = 0, bxMv = 0, byMv = 0;   /* subrects */
    bool mvDisplayRes = false;                    /* MV em resolucao de saida (copia outW x outH) */
    bool mvJittered = false, mvNdc = false, reset = false;
    int  depthInv = -1;
    float jx = 0, jy = 0, msx = 1, msy = 1, dtMs = 0;
    float camNear = 0, camFar = 0, camFov = 0, viewToM = 0;
    unsigned origem = 0; const char* origemTxt = "-";   /* 1 dlss, 2 fsr31, 3 fsr2, 4 xess */
};

void Iniciar(LogFn log);
bool Ligada();                       /* a chave do ini (lida uma vez) */
bool AlguemQuer();                   /* o FG da DLL ou a pelicula querem dado (senao Publicar e desperdicio) */
void Publicar(const Entrada& e);     /* thread do jogo, no dispatch do upscaler, ANTES da chamada original */
void Servico();                      /* thread da ponte: o cemiterio */
void Parar();                        /* depois de os hooks dos upscalers sairem */

} } // namespace Ponte::Colheita
