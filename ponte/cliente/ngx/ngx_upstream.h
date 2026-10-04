/* =====================================================================
   ngx_upstream.h -- entrega 2 do G1: a cor PEQUENA (resolucao interna), os
   vetores de movimento e a profundidade vao para o app pela sessao
   "upstream", o app devolve a cor pequena processada e a DLL a entrega ao
   DLSS do jogo no lugar da Color, antes do super resolution.

   Fase 3 do Modo Integrado (CONTRATO-FASE3.md, 15/09/2026). Agente G1.
   Protocolo com o app (proposta ao G2, relatorios\G1-sonda.md secao 5):
     - pipe proprio \\.\pipe\dlss5-feed.<pid>.up, servido pelo app quando
       nativo_upstream=1; a DLL so conecta depois de a sonda ver DLSS-SR;
     - FeedHello v9 igual; FeedBuild v9 no tamanho INTERNO com
       client_flags |= FEED_BUILD_UPSTREAM (0x100) e SEM ASYNC_HOME (sincrono);
     - por quadro, 'X' + FeedExtraMsg (so se o ack trouxe FEED_ACK_UPSTREAM
       0x100) e depois o 'F' v9 de sempre;
     - espera de GPU na fila da chamada (Wait na fila / no contexto), nunca CPU;
       vigia de 50 ms sinaliza a fence por CPU e, na 5.a vez, desliga.

   O que o modulo faz DENTRO do EvaluateFeature (thread do jogo):
     AntesD12/AntesD11: copia Color/MV/Depth para os slots, Signal(in,n),
       manda o quadro, Wait(out,n) na fila, troca a Color do parametro pelo
       nosso Output. Devolve true so se trocou.
     DepoisD12/DepoisD11: restaura a Color (e as bases de subrect).
   Se qualquer coisa falhar, devolve false e o hook chama a original intocada.

   A PROVA DE PRONTIDAO (D3D12): antes de trocar qualquer coisa, por 8 quadros
   a Color e copiada duas vezes -- na nossa lista, executada na hora do hook,
   e dentro da lista do jogo, antes do evaluate -- e comparada por readback.
   Iguais = a cena ja estava submetida quando o evaluate foi gravado = o
   sincrono e possivel. Diferentes = impossivel neste jogo: upstream fica
   desligado com motivo (ver G1-sonda.md secao 4).
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include "ngx_contrato.h"
#include "ngx_hook.h"

namespace Ngx { namespace Up {

void Iniciar(LogFn log);
void Servico();                                   /* thread da ponte, a cada ~250 ms */
void FilaVista(ID3D12CommandQueue* fila);         /* a fila em que a lista do evaluate executou */
bool AntesD12(ID3D12GraphicsCommandList* lista, const Handle* h, const Parameter* p);
void FeatureSolta(const Handle* h);               /* X-FG-6: do ReleaseFeature - o jogo soltou uma feature (troca de upscaler?) */
void DepoisD12(const Parameter* p);
bool AntesD11(ID3D11DeviceContext* ctx, const Handle* h, const Parameter* p);
void DepoisD11(const Parameter* p);
void Parar();
bool Ativo();

} } // namespace Ngx::Up
