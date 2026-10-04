/* =====================================================================
   ngx_hook.h -- o que a DLL da ponte (ponte_cliente.cpp) ve do modulo NGX.

   Fase 3 do Modo Integrado (CONTRATO-FASE3.md, 15/09/2026). Agente G1.

   Tres entregas moram atras desta interface:
     1. sonda    -- engancha os exports do _nvngx.dll e SO LOGA (linhas "ngx:");
     2. upstream -- cor pequena/MV/depth para a ponte e a troca da Color;
     3. controle -- preset/qualidade/DLAA por parametro.

   Contrato com a ponte:
     - Iniciar(log) na thread da ponte, depois dos hooks de Present. Nao
       bloqueia; se o _nvngx.dll ainda nao esta no processo, Servico() volta
       a tentar.
     - Servico() no laco da thread da ponte (a cada ~250 ms): engancha quando o
       _nvngx.dll aparece, le o arquivo de controle, emite as linhas periodicas.
     - AoExecutar() de dentro do hook de ExecuteCommandLists da ponte: e assim
       que a sonda descobre em que fila a command list do EvaluateFeature roda.
     - Parar() antes de restaurar os hooks de Present e descarregar: desliga os
       hooks do MinHook e espera as chamadas em voo sairem.

   Regra dura: nada aqui derruba o jogo. Todo caminho de erro chama a funcao
   original intocada e loga uma vez.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d12.h>

namespace Ngx {

typedef void (*LogFn)(const char* fmt, ...);

/* Estado que a ponte consulta (tudo atomico ou so-leitura). */
struct Estado
{
    bool hookD3D12;       /* exports D3D12 enganchados */
    bool hookD3D11;
    bool viuSR;           /* ja vimos um EvaluateFeature de DLSS-SR */
    unsigned api;         /* 12 ou 11 (da ultima avaliacao de SR) */
    unsigned internoW, internoH, saidaW, saidaH;   /* da ultima avaliacao de SR */
    unsigned corFmt, mvFmt, depthFmt, saidaFmt;    /* DXGI_FORMAT numericos */
    unsigned mvW, mvH, depthW, depthH;             /* dimensoes dos recursos MV/Depth do jogo */
    unsigned flags;       /* DLSS.Feature.Create.Flags da feature */
    float mvScaleX, mvScaleY;
    void* dev12;          /* ID3D12Device* do jogo (sem AddRef: quem usar faz o seu) */
    void* dev11;          /* ID3D11Device* idem */
    unsigned avaliacoes;  /* total de EvaluateFeature vistos */
    unsigned avaliacoesSR;   /* X-FG-6: so os de DLSS SR (o upstream conta os novos depois de largar o pipe) */
};

void Iniciar(LogFn log);
void Servico();
void AoExecutar(ID3D12CommandQueue* fila, UINT n, ID3D12CommandList* const* listas);
void AoApresentar();   /* do hook de Present: marca o fim do quadro para a contagem do ciclo */
void Parar();
Estado Ler();

} // namespace Ngx
