/* =====================================================================
   sr_hook.h -- degrau 2 (Q2, 16/09/2026): jogo com FSR 2/3.1 ou XeSS, mas
   sem DLSS, passa a rodar DLSS Super Resolution real pelo nosso app.

   A DLL engancha (GetProcAddress + MinHook) o dispatch do upscaler do jogo:
     FSR 3.1+  ffxDispatch            (amd_fidelityfx_dx12.dll / _dx11.dll)
     FSR 2.x   ffxFsr2ContextDispatch (ffx_fsr2_api_x64.dll)
     XeSS      xessD3D12Execute       (libxess.dll)
   e, a cada dispatch, manda cor pequena + MV + depth (+ exposicao) pela
   sessao `.up` com o bit FEED_BUILD_SR_REQUEST (0x200): o Output vem no
   tamanho de SAIDA do jogo, no formato do destino. Espera sincrona na fila
   do jogo, copia o Output no destino do upscaler e PULA a chamada original.
   Falha em qualquer ponto = original intocada. Formato do fio: relatorios\
   Q2-fio.md.

   Contrato com a ponte (mesmo desenho do ngx_hook.h):
     - Iniciar(log) na thread da ponte; Servico() a cada ~250 ms (procura os
       modulos, engancha, le o controle, conecta/builda); AoExecutar() do hook
       de ExecuteCommandLists (em que fila a lista do dispatch roda); Parar()
       antes da descarga.
   Regra dura: nada aqui derruba o jogo.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d12.h>

namespace Sr {

typedef void (*LogFn)(const char* fmt, ...);

void Iniciar(LogFn log);
void Servico();
void AoExecutar(ID3D12CommandQueue* fila, UINT n, ID3D12CommandList* const* listas);
void Parar();
bool Ativo();            /* sessao 0x200 pronta (troca ligada ou em prova) */
bool HookInstalado();    /* algum upscaler de terceiro enganchado */

} // namespace Sr
