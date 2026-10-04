/* =====================================================================
   ponte_dados.h -- J5 (18/09/2026): o BALCAO da profundidade e dos vetores
   de movimento do jogo para a PONTE DA FASE 2 (o app, pela pelicula).

   E o irmao do balcao do fg_hook.h (J3), com duas diferencas: serve aos
   dois backends (D3D11 e D3D12) e quem consome e o Present da ponte
   (ponte_cliente.cpp), que copia depth e MV para os slots SLOT_DEPTH/SLOT_MV
   da fase 2 e avisa o app com a tag 'D' (ponte_comum.h, FeedDadosMsg).

   Contrato (o mesmo do J3):
     - Publicar roda na THREAD DO JOGO, dentro do dispatch do upscaler, DEPOIS
       de a copia para os slots da sessao .up ter sido submetida (D3D12) ou
       gravada no contexto imediato (D3D11). A ponte toma a sua propria
       referencia (AddRef) e a segura ate trocar de pacote ou Limpar.
     - Limpar ANTES de a sessao .up soltar as texturas dela.
     - Quer() e falso sem sessao da fase 2 montada ou com um app que nao
       entende 'D': nesse caso publicar e desperdicio no caminho quente.
   As texturas publicadas sao as da sessao .up (sr_hook / ngx_upstream), na
   resolucao de render: depth R32_FLOAT renderW x renderH (ou maior), MV
   R16G16_FLOAT mvW x mvH (ou maior). Nada aqui derruba o jogo: publicar e
   limpar tem try/catch proprios.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <cstdint>

namespace Ponte { namespace Dados {

struct Pacote
{
    /* exatamente UM dos pares abaixo vem preenchido, conforme a API do jogo */
    ID3D12Resource*     depth12 = nullptr;  ID3D12Resource*     mv12 = nullptr;
    ID3D12CommandQueue* fila = nullptr;     /* D3D12: fila em que as copias da .up foram gravadas */
    ID3D11Texture2D*    depth11 = nullptr;  ID3D11Texture2D*    mv11 = nullptr;
    unsigned renderW = 0, renderH = 0, mvW = 0, mvH = 0;
    float jitterX = 0, jitterY = 0;
    float mvScaleX = 1, mvScaleY = 1;
    float dtMs = 0;
    float camNear = 0, camFar = 0, camFov = 0, viewToM = 0;
    int   depthInv = -1;                    /* 1 invertida, 0 normal, -1 desconhecida */
    bool  mvDisplayRes = false, mvJittered = false, mvNdc = false, reset = false;
    unsigned origem = 0;                    /* 1 dlss, 2 fsr31, 3 fsr2, 4 xess */
};

bool Quer();                     /* a fase 2 esta montada e o app entende 'D' */
void Publicar(const Pacote& p);  /* thread do jogo, no dispatch do upscaler */
void Limpar();                   /* a sessao .up vai soltar as texturas */

} } // namespace Ponte::Dados
