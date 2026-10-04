// me_test.cpp -- prova isolada do VME (motion_estimate.h): deslocamento CONHECIDO.
//
//   me_test.exe [largura] [altura] [divisor] [bloco] [dx]
//
// POR QUE ISTO EXISTE (VME-FLUXO, 20/09/2026). O campo de movimento chegava 100% zerado
// no app, e dois relatorios concluiram dai que "o VME nao serve nesta placa". Nao era o
// VME: este teste roda o MESMO motion_estimate.h com um par de quadros deslocado de dx px
// e, na RTX 4080, le 7,998 px para 8 px pedidos com 100% dos blocos em todas as grades da
// bancada. Quando o campo zerar de novo, rode isto ANTES de culpar o hardware: se aqui da
// certo, o problema esta na ENTRADA que o app entrega (estado de pipeline herdado, SRV
// errado ou quadro repetido), e a linha `mv-diag: entrada ... mudou em X%` diz qual.
//
// COMPILAR (Git Bash, com C:\msys64\mingw64\bin no PATH), da raiz do fonte:
//   g++ -std=c++17 -O2 -I. tests/me_test.cpp -o me_test.exe -ld3d11 -ld3d12 -ldxgi -ld3dcompiler
#include "motion_estimate.h"
#include <cstdio>
#include <cstdlib>

static void Ruido(std::vector<uint8_t>& px, int w, int h, int dx)
{
    px.resize((size_t)w * h * 4);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        int sx = x - dx, sy = y;                      // conteudo deslocado dx para a direita
        unsigned v = (unsigned)((sx * 2654435761u) ^ (sy * 40503u));
        unsigned a = (v >> 3) & 0xFF, b = (v >> 11) & 0xFF, c = (v >> 19) & 0xFF;
        // um pouco de estrutura de baixa frequencia por cima do ruido
        a = (a / 2 + (unsigned)(((sx / 17) * 37 + (sy / 13) * 53) & 0x7F));
        uint8_t* p = &px[((size_t)y * w + x) * 4];
        p[0] = (uint8_t)a; p[1] = (uint8_t)b; p[2] = (uint8_t)c; p[3] = 255;
    }
}

int main(int argc, char** argv)
{
    const int W = argc > 1 ? atoi(argv[1]) : 1280;
    const int H = argc > 2 ? atoi(argv[2]) : 720;
    const unsigned DIV = argc > 3 ? (unsigned)atoi(argv[3]) : 2;
    const unsigned BLK = argc > 4 ? (unsigned)atoi(argv[4]) : 16;
    const int DX = argc > 5 ? atoi(argv[5]) : 8;

    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                   D3D11_SDK_VERSION, &dev, &fl, &ctx);
    if (FAILED(hr)) { printf("D3D11CreateDevice 0x%08lX\n", (unsigned long)hr); return 1; }

    dlss5::MotionEstimator me;
    if (!me.Init(dev, (UINT)W, (UINT)H, DIV, BLK)) {
        printf("VME %dx%d div %u bloco %u -> SEM SUPORTE: %s\n", W, H, DIV, BLK, me.LastError());
        return 2;
    }

    ID3D11Texture2D* tex[2]{}; ID3D11ShaderResourceView* srv[2]{};
    for (int i = 0; i < 2; i++) {
        std::vector<uint8_t> px; Ruido(px, W, H, i == 0 ? 0 : DX);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc = { 1, 0 };
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sr{ px.data(), (UINT)W * 4, 0 };
        if (FAILED(dev->CreateTexture2D(&td, &sr, &tex[i]))) { printf("textura %d\n", i); return 3; }
        dev->CreateShaderResourceView(tex[i], nullptr, &srv[i]);
    }

    me.Update(ctx, srv[0]);          // preenche a referencia
    me.Update(ctx, srv[1]);          // o par: A -> B com DX px de deslocamento
    dlss5::MotionEstimator::ProbeStats st{};
    me.ProbeSample(ctx, 1, &st);     // agenda
    ctx->Flush();
    me.Update(ctx, srv[1]);          // nao muda o campo (quadro igual ao anterior nao e o caso: e o mesmo B)
    bool tem = me.ProbeSample(ctx, 1, &st);

    printf("VME %dx%d div %u bloco %u grade %ux%u | dx pedido %d px | %s"
           " |mv| media %.3f px, mediana %.3f, max %.2f | %.0f%% nao-zero, %.0f%% descartado, %.0f%% campo zerado\n",
           W, H, DIV, me.BlockSize(), me.MvWidth(), me.MvHeight(), DX,
           tem ? "" : "(sem amostra) ", st.meanPx, st.medianPx, st.maxPx,
           st.pctNonZero, st.pctRejected, st.pctFieldZero);

    for (int i = 0; i < 2; i++) { if (srv[i]) srv[i]->Release(); if (tex[i]) tex[i]->Release(); }
    me.Shutdown(); ctx->Release(); dev->Release();
    return 0;
}
