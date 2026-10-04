// ponte_stub.h -- STUB do PonteHost (A1) SO PARA O TESTE DE SINTAXE do fonte_ponte.h (A2).
// A interface e a do CONTRATO.md (secao A1), letra por letra. Nao faz nada: devolve false.
// Quando o src\ponte_feeder_host.h do A1 existir, o include do fonte_ponte.h acha o real
// primeiro (mesma pasta) e este stub deixa de ser usado. NAO incluir no main.cpp.
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>

namespace PonteHost {
    struct Quadro {                 // um quadro recebido do jogo, ja aberto no NOSSO device
        uint64_t n;                 // numero do quadro (valor da fence)
        ID3D11Texture2D* cor;       // FEED_COLOR   (sempre)
        ID3D11Texture2D* depth;     // FEED_DEPTH   (pode ser nullptr)
        ID3D11Texture2D* mv;        // FEED_MV      (pode ser nullptr)
        UINT largura, altura; DXGI_FORMAT formato;
        float jitterX, jitterY;     // FeedFrameMsg v6+
        int64_t qpcChegada;         // QueryPerformanceCounter no instante em que in_fence >= n foi observado
    };
    static bool Iniciar(const wchar_t* linhaDeComando, ID3D11Device* dev, ID3D11DeviceContext* ctx) {
        (void)linhaDeComando; (void)dev; (void)ctx; return false;
    }
    static bool Ativa() { return false; }
    static bool ObterQuadro(Quadro* q) { (void)q; return false; }
    static void EntregarSaida(ID3D11Texture2D* resultado, uint64_t n) { (void)resultado; (void)n; }
    static void Parar() {}
}
