/* =====================================================================
   tela.h -- X-DLL-2 (16/09/2026): o que a DLL faz na TELA do jogo.
     C2: contador de fps pequeno desenhado dentro do jogo (canto superior
         direito), ligado pela chave fps_tela=1 do .ngx. Mostra o fps do jogo
         e, com o frame generation dono, "jogo>tela". Sem shader: uma caixa
         escrita na CPU e copiada para o backbuffer antes do Present
         (D3D12: CopyTextureRegion de um buffer UPLOAD; D3D11: UpdateSubresource).
     E8: publica no .ngx-estado exibicao=janela|sem_bordas|tela_cheia e
         tela_cheia_ok=0|1; aceita exibicao=tela_cheia|janela com
         exibicao_pedido=<GetTickCount64 do clique> no .ngx e chama
         SetFullscreenState na thread da janela do jogo (o caminho do Alt+Enter),
         uma vez por pedido. Se o jogo desfizer, tela_cheia_ok vira 0 e ninguem insiste.
   REGRA DURA: nada aqui derruba o jogo; qualquer falha so deixa de desenhar.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace Tela {

typedef void (*LogFn)(const char* fmt, ...);

void Iniciar(LogFn log);
void Servico();                                  /* thread da ponte, ~250 ms: le o .ngx e cuida do pedido de exibicao */
void FilaVista(ID3D12CommandQueue* fila);        /* a fila DIRECT do jogo (hook de ECL) */
void AoApresentar(IDXGISwapChain* sc);           /* thread do jogo, todo Present: fps, modo de exibicao */
void Desenhar(IDXGISwapChain* sc);               /* thread do jogo, logo antes do Present original (FG nao dono) */
void GravarNaCopia(ID3D12GraphicsCommandList* lista, ID3D12Resource* bb);   /* FG: bb em COPY_DEST na lista do ritmo */
void TelaCheiaMudou();                           /* hook de SetFullscreenState: rele o modo no proximo Present */

unsigned EstadoVersao();
void EstadoTexto(char* buf, size_t n);
void Parar();                                    /* solta o hook de mensagens e os recursos (ritmo ja parado) */

} // namespace Tela
