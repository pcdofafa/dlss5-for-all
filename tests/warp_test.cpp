// =====================================================================================
//  warp_test.cpp -- o quadro sintetizado vale mais que o quadro segurado?
//
//  O QUE ESTA PERGUNTA QUER DIZER. No frame skipping (docs/ARQUITETURA.md §12) um quadro
//  em cada N nao e apresentado. O modo `segurar` deixa o ULTIMO quadro processado na tela
//  nesse intervalo: acaba o piscar e sobra judder, porque o movimento anda na metade da
//  taxa. O modo `sintetizar` proposto troca isso por um quadro DEFORMADO pelo campo de
//  movimento -- o mesmo que o frame generation faz por dentro.
//
//  ESTE TESTE MEDE ISSO COM VERDADE-TERRENO, que e a unica forma honesta de responder sem
//  um jogo na frente. A medicao anterior registrada no §12 (scratchpad/movimento.html +
//  energia de gradiente) foi declarada insuficiente pelo proprio documento por dois
//  motivos, e os dois sao corrigidos aqui:
//
//    1. o alvo era sintetico demais (cores chapadas): aqui a imagem tem ruido multi-oitava,
//       grade fina e bordas duras -- material de sobra para o casamento de blocos errar;
//    2. energia de gradiente e INSENSIVEL A TRANSLACAO: aqui a metrica e a diferenca
//       pixel a pixel contra o quadro que DEVERIA aparecer no instante pulado, que e a
//       definicao exata do erro que o olho ve como judder.
//
//  Como a translacao e imposta por nos, o quadro do meio e conhecido: ele e o recorte da
//  mesma imagem base deslocado pela METADE. Entao da para comparar, contra essa verdade,
//  as tres coisas que a camada pode mostrar num quadro pulado:
//
//      segurar ...... o quadro anterior (t-1)          <- o que o app faz hoje
//      cru .......... o jogo ao vivo por baixo (t)     <- o que os modos alfa/regiao fazem
//      sintetizar ... warp de (t-1) por 0,5 * campo    <- o que se quer avaliar
//
//  O campo vem do MESMO estimador que o app ja usa em producao (motion_estimate.h, o bloco
//  de video da GPU). Nao ha nada de novo na estimativa: o que se mede aqui e se o WARP
//  paga o proprio custo.
//
//  Compilar (msys64):
//    g++ -std=c++17 -O2 -Wall -Wextra tests/warp_test.cpp -o tests/warp_test.exe
//        -ld3d11 -ld3d12 -ldxgi -ldxguid -I.
// =====================================================================================

#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

#include "../motion_estimate.h"
// O WARP MEDIDO AQUI E O DE PRODUCAO, e nao uma copia parecida. O shader vive dentro do
// warp_frame.h, que e o mesmo header que o main.cpp usa; se alguem mexer nele, os numeros
// desta medicao mudam junto -- que e exatamente o que se quer de um teste.
#include "../warp_frame.h"

// ------------------------------------------------------------------ utilidades

template <class T> static void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

// (A compilacao dos shaders do warp mora dentro do warp_frame.h, que e o modulo de
// producao. O teste nao tem shader proprio de proposito: se tivesse, poderia passar com
// um shader que a camada nao usa.)

// ------------------------------------------------------------------ a imagem de teste
//
// Uma imagem so, grande, da qual se recortam as janelas deslocadas. Recortar em vez de
// redesenhar garante que o unico deslocamento entre os quadros e o que a gente pediu --
// sem reamostragem, sem erro de arredondamento, sem nada para atrapalhar a medida.

struct Imagem {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // BGRA
    uint8_t* At(int x, int y) { return &px[((size_t)y * (size_t)w + (size_t)x) * 4]; }
    const uint8_t* At(int x, int y) const { return &px[((size_t)y * (size_t)w + (size_t)x) * 4]; }
};

static uint32_t Hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
static float Aleatorio(int x, int y, int semente)
{
    return (float)(Hash32((uint32_t)(x * 374761393 + y * 668265263 + semente * 362437)) & 0xFFFF) / 65535.0f;
}
// Ruido de valor com interpolacao suave: da textura de banda larga, que e justamente o
// que faltava no alvo de cores chapadas da medicao anterior.
static float RuidoOitava(float x, float y, int celula, int semente)
{
    const float fx = x / (float)celula, fy = y / (float)celula;
    const int   x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    const float tx = fx - (float)x0, ty = fy - (float)y0;
    const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty);
    const float a = Aleatorio(x0, y0, semente),     b = Aleatorio(x0 + 1, y0, semente);
    const float c = Aleatorio(x0, y0 + 1, semente), d = Aleatorio(x0 + 1, y0 + 1, semente);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}

static Imagem GerarBase(int w, int h)
{
    Imagem im; im.w = w; im.h = h; im.px.assign((size_t)w * (size_t)h * 4, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // quatro oitavas de ruido: conteudo em toda a faixa de frequencia
            float n = 0.0f, amp = 0.5f;
            for (int celula = 64; celula >= 8; celula /= 2) {
                n += RuidoOitava((float)x, (float)y, celula, celula) * amp;
                amp *= 0.5f;
            }
            float r = n, g = n * 0.85f + 0.10f, b = n * 0.70f + 0.20f;
            // gradiente suave, para que nem tudo seja ruido
            r += 0.15f * (float)x / (float)w; g += 0.10f * (float)y / (float)h;
            // grade fina: bordas duras de alta frequencia, o pior caso do casamento de blocos
            if ((x % 37) == 0 || (y % 43) == 0) { r += 0.35f; g += 0.35f; b += 0.35f; }
            // retangulos chapados, para haver tambem regiao SEM textura (onde todo
            // estimador de bloco fica ambiguo)
            if (((x / 97) + (y / 89)) % 7 == 0) { r *= 0.45f; g *= 0.55f; b *= 0.75f; }
            uint8_t* p = im.At(x, y);
            p[0] = (uint8_t)std::clamp((int)(b * 255.0f), 0, 255);
            p[1] = (uint8_t)std::clamp((int)(g * 255.0f), 0, 255);
            p[2] = (uint8_t)std::clamp((int)(r * 255.0f), 0, 255);
            p[3] = 255;
        }
    }
    return im;
}

// Recorta uma janela WxH da base, com origem (ox,oy). E assim que a translacao entra:
// exata, em pixel inteiro, sem reamostrar nada.
static Imagem Recortar(const Imagem& base, int ox, int oy, int w, int h)
{
    Imagem im; im.w = w; im.h = h; im.px.resize((size_t)w * (size_t)h * 4);
    for (int y = 0; y < h; y++) {
        const int sy = std::clamp(oy + y, 0, base.h - 1);
        for (int x = 0; x < w; x++) {
            const int sx = std::clamp(ox + x, 0, base.w - 1);
            memcpy(im.At(x, y), base.At(sx, sy), 4);
        }
    }
    return im;
}

// Recorte com origem FRACIONARIA (bilinear). Existe por um motivo so, e ele importa: com
// deslocamento inteiro o warp acerta o texel exato e o PSNR sai perfeito -- numero bonito e
// mentiroso, porque em jogo nenhum a camera anda em pixel redondo. Com fracao, a amostragem
// do warp cai ENTRE texels e paga o borrao do filtro bilinear, que e o custo real do metodo.
//
// Os tres quadros do cenario passam por esta MESMA funcao, entao o borrao de reamostragem
// e piso comum aos tres metodos comparados; o que muda entre eles continua sendo so o
// acerto do movimento. (As fases fracionarias nao sao identicas entre os tres, entao ha um
// residuo de filtragem embutido no piso -- ele penaliza o warp, nunca o favorece.)
static Imagem RecortarSub(const Imagem& base, float ox, float oy, int w, int h)
{
    Imagem im; im.w = w; im.h = h; im.px.resize((size_t)w * (size_t)h * 4);
    for (int y = 0; y < h; y++) {
        const float fy = oy + (float)y;
        const int   y0 = (int)floorf(fy); const float ty = fy - (float)y0;
        for (int x = 0; x < w; x++) {
            const float fx = ox + (float)x;
            const int   x0 = (int)floorf(fx); const float tx = fx - (float)x0;
            const int xa = std::clamp(x0, 0, base.w - 1), xb = std::clamp(x0 + 1, 0, base.w - 1);
            const int ya = std::clamp(y0, 0, base.h - 1), yb = std::clamp(y0 + 1, 0, base.h - 1);
            const uint8_t* p00 = base.At(xa, ya); const uint8_t* p10 = base.At(xb, ya);
            const uint8_t* p01 = base.At(xa, yb); const uint8_t* p11 = base.At(xb, yb);
            uint8_t* d = im.At(x, y);
            for (int c = 0; c < 4; c++) {
                const float a = (float)p00[c] + ((float)p10[c] - (float)p00[c]) * tx;
                const float b = (float)p01[c] + ((float)p11[c] - (float)p01[c]) * tx;
                d[c] = (uint8_t)std::clamp((int)lrintf(a + (b - a) * ty), 0, 255);
            }
        }
    }
    return im;
}

// Cola um bloco da base (deslocado) por cima: e o objeto que se move sobre fundo parado.
// O caso dificil de verdade -- onde o objeto cobre e descobre o fundo, e nenhum warp por
// campo unico pode acertar tudo.
static void ColarObjeto(Imagem& dst, const Imagem& base, int dstX, int dstY,
                        int srcX, int srcY, int w, int h)
{
    for (int y = 0; y < h; y++) {
        const int dy = dstY + y; if (dy < 0 || dy >= dst.h) continue;
        for (int x = 0; x < w; x++) {
            const int dx = dstX + x; if (dx < 0 || dx >= dst.w) continue;
            const uint8_t* s = base.At(std::clamp(srcX + x, 0, base.w - 1),
                                       std::clamp(srcY + y, 0, base.h - 1));
            uint8_t* d = dst.At(dx, dy);
            // invertido de proposito: o objeto tem de ser visivelmente diferente do fundo
            d[0] = (uint8_t)(255 - s[2]); d[1] = (uint8_t)(255 - s[1]);
            d[2] = (uint8_t)(255 - s[0]); d[3] = 255;
        }
    }
}

// ------------------------------------------------------------------ metricas

struct Erro { double psnr; double mae; };

// Compara so a area central (margem de `borda` px). O warp nao tem como inventar o que
// entrou pela borda da tela nesse intervalo -- nem o frame gen de ninguem tem --, e
// contar essa faixa mediria a borda, nao o metodo.
static Erro Comparar(const Imagem& a, const Imagem& b, int borda)
{
    double soma2 = 0.0, soma1 = 0.0; size_t n = 0;
    for (int y = borda; y < a.h - borda; y++) {
        for (int x = borda; x < a.w - borda; x++) {
            const uint8_t* p = a.At(x, y); const uint8_t* q = b.At(x, y);
            for (int c = 0; c < 3; c++) {
                const double d = (double)p[c] - (double)q[c];
                soma2 += d * d; soma1 += fabs(d); n++;
            }
        }
    }
    if (!n) return { 0.0, 0.0 };
    const double mse = soma2 / (double)n;
    return { mse > 0.0 ? 10.0 * log10(255.0 * 255.0 / mse) : 99.0, soma1 / (double)n };
}

// ------------------------------------------------------------------ GPU

struct Gpu {
    ID3D11Device*        dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;

    bool Init();
    void Shutdown();
    ID3D11Texture2D* Subir(const Imagem& im, ID3D11ShaderResourceView** srv);
    // out = src deformado por t * campo, pelo MESMO codigo que a camada usa
    bool Warp(dlss5::WarpFrame& warp, ID3D11Texture2D* src, ID3D11ShaderResourceView* mv,
              float covU, float covV, float t, int w, int h, Imagem* out);
};

bool Gpu::Init()
{
    D3D_FEATURE_LEVEL fl{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                   D3D11_SDK_VERSION, &dev, &fl, &ctx);
    if (FAILED(hr)) { printf("D3D11CreateDevice 0x%08X\n", (unsigned)hr); return false; }

    return dev && ctx;
}

void Gpu::Shutdown()
{
    if (ctx) ctx->ClearState();
    Rel(ctx); Rel(dev);
}

ID3D11Texture2D* Gpu::Subir(const Imagem& im, ID3D11ShaderResourceView** srv)
{
    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)im.w; td.Height = (UINT)im.h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sr{}; sr.pSysMem = im.px.data(); sr.SysMemPitch = (UINT)im.w * 4;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, &sr, &tex))) return nullptr;
    if (srv) dev->CreateShaderResourceView(tex, nullptr, srv);
    return tex;
}

bool Gpu::Warp(dlss5::WarpFrame& warp, ID3D11Texture2D* src, ID3D11ShaderResourceView* mv,
               float covU, float covV, float t, int w, int h, Imagem* out)
{
    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* alvo = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &alvo))) return false;
    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(dev->CreateRenderTargetView(alvo, nullptr, &rtv))) { Rel(alvo); return false; }

    // `src` faz o papel do ultimo quadro PROCESSADO: na camada ele vem do backbuffer depois
    // da cadeia; aqui e o quadro anterior da sequencia sintetica. O caminho e o mesmo.
    if (!warp.Guardar(ctx, src)) { Rel(rtv); Rel(alvo); return false; }
    const D3D11_VIEWPORT vp{ 0.f, 0.f, (float)w, (float)h, 0.f, 1.f };
    if (!warp.Desenhar(ctx, rtv, mv, covU, covV, t, vp)) { Rel(rtv); Rel(alvo); return false; }
    ctx->OMSetRenderTargets(0, nullptr, nullptr);

    bool ok = false;
    if (out) {
        D3D11_TEXTURE2D_DESC sd = td;
        sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* stg = nullptr;
        if (SUCCEEDED(dev->CreateTexture2D(&sd, nullptr, &stg))) {
            ctx->CopyResource(stg, alvo);
            D3D11_MAPPED_SUBRESOURCE ms{};
            if (SUCCEEDED(ctx->Map(stg, 0, D3D11_MAP_READ, 0, &ms))) {
                out->w = w; out->h = h; out->px.resize((size_t)w * (size_t)h * 4);
                for (int y = 0; y < h; y++)
                    memcpy(out->At(0, y), (const uint8_t*)ms.pData + (size_t)y * ms.RowPitch, (size_t)w * 4);
                ctx->Unmap(stg, 0);
                ok = true;
            }
            Rel(stg);
        }
    } else {
        ok = true;
    }
    Rel(rtv); Rel(alvo);
    return ok;
}

// ------------------------------------------------------------------ um cenario

struct Resultado {
    Erro segurar, cru, sintetizar;
    double msUpdate = 0.0, msWarp = 0.0;
};

static bool Rodar(Gpu& g, const Imagem& f0, const Imagem& f1, const Imagem& verdade,
                  UINT divisor, UINT bloco, int borda, Resultado* out)
{
    dlss5::MotionEstimator me;
    // rejectPx enorme: aqui se quer o campo CRU do hardware. O descarte por magnitude e
    // decisao de producao (MV errado e pior que MV zero para o DLSS); para julgar o warp
    // ele so esconderia o comportamento real nos deslocamentos grandes.
    if (!me.Init(g.dev, (UINT)f0.w, (UINT)f0.h, divisor, bloco, 1.0e6f)) {
        printf("  estimador indisponivel: %s\n", me.LastError() ? me.LastError() : "?");
        return false;
    }

    ID3D11ShaderResourceView *s0 = nullptr, *s1 = nullptr;
    ID3D11Texture2D* t0 = g.Subir(f0, &s0);
    ID3D11Texture2D* t1 = g.Subir(f1, &s1);
    if (!t0 || !t1 || !s0 || !s1) { Rel(s0); Rel(s1); Rel(t0); Rel(t1); return false; }

    // Primeiro Update so preenche a referencia; o segundo produz o campo f1 -> f0.
    me.Update(g.ctx, s0);
    me.Update(g.ctx, s1);

    // O MESMO modulo que a camada usa (warp_frame.h), inicializado do mesmo jeito.
    dlss5::WarpFrame warp;
    if (!warp.Init(g.dev, (UINT)f0.w, (UINT)f0.h)) {
        printf("  warp indisponivel: %s\n", warp.UltimoErro() ? warp.UltimoErro() : "?");
        Rel(s0); Rel(s1); Rel(t0); Rel(t1); return false;
    }

    Imagem sint;
    if (!g.Warp(warp, t0, me.MotionSRV(), me.CoverageU(), me.CoverageV(), 0.5f,
                f0.w, f0.h, &sint)) {
        Rel(s0); Rel(s1); Rel(t0); Rel(t1); return false;
    }

    out->segurar    = Comparar(f0,   verdade, borda);
    out->cru        = Comparar(f1,   verdade, borda);
    out->sintetizar = Comparar(sint, verdade, borda);

    // ---- custo. Medido com o relogio de alta resolucao em volta de um Flush + leitura,
    // que e o que forca a GPU a terminar. Nao e um timestamp de GPU (o app ja tem essa
    // medida em producao, na linha "mv-hw"); e o custo de parede de uma passada completa,
    // que e o numero que interessa para decidir se cabe no intervalo de um quadro.
    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
    const int N = 60;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    for (int i = 0; i < N; i++) { me.Update(g.ctx, (i & 1) ? s0 : s1); }
    g.ctx->Flush();
    { // fecha a fila de verdade: uma leitura de 1 pixel do campo
        dlss5::MotionEstimator::ProbeStats st; me.ProbeSample(g.ctx, 1, &st);
    }
    QueryPerformanceCounter(&b);
    out->msUpdate = 1000.0 * (double)(b.QuadPart - a.QuadPart) / (double)fq.QuadPart / (double)N;

    QueryPerformanceCounter(&a);
    for (int i = 0; i < N; i++)
        g.Warp(warp, t0, me.MotionSRV(), me.CoverageU(), me.CoverageV(), 0.5f, f0.w, f0.h, nullptr);
    g.ctx->Flush();
    { Imagem lixo; g.Warp(warp, t0, me.MotionSRV(), me.CoverageU(), me.CoverageV(), 0.5f, f0.w, f0.h, &lixo); }
    QueryPerformanceCounter(&b);
    out->msWarp = 1000.0 * (double)(b.QuadPart - a.QuadPart) / (double)fq.QuadPart / (double)(N + 1);

    Rel(s0); Rel(s1); Rel(t0); Rel(t1);
    return true;
}

// ------------------------------------------------------------------ main

int main(int argc, char** argv)
{
    int W = 1920, H = 1080;
    if (argc >= 3) { W = atoi(argv[1]); H = atoi(argv[2]); }
    const UINT divisor = (argc >= 4) ? (UINT)atoi(argv[3]) : 2;
    const UINT bloco   = (argc >= 5) ? (UINT)atoi(argv[4]) : 16;

    printf("=== warp_test: o quadro sintetizado contra o quadro segurado ===\n");
    printf("resolucao %dx%d | estimador: divisor %u, bloco %u\n\n", W, H, divisor, bloco);

    Gpu g;
    if (!g.Init()) return 1;

    const int MARGEM = 160;   // folga na base para deslocar sem esbarrar na borda
    Imagem base = GerarBase(W + 2 * MARGEM, H + 2 * MARGEM);

    // Os deslocamentos: a 60 fps, 2 px por quadro e um movimento lento de mira, 40 px e
    // um giro rapido de camera. PARES de proposito -- o quadro do meio tem de existir
    // em pixel inteiro para ser verdade-terreno, e nao uma reamostragem nossa.
    const int deslocamentos[] = { 2, 6, 12, 20, 32, 48 };

    printf("--- CENARIO A: camera transladando (a tela inteira anda)\n");
    printf("%6s | %-22s | %-22s | %-22s\n", "px/q", "segurar (hoje)", "cru (alfa/regiao)", "sintetizar (warp)");
    printf("%6s | %10s %10s | %10s %10s | %10s %10s\n", "", "PSNR dB", "MAE", "PSNR dB", "MAE", "PSNR dB", "MAE");
    double custoUpdate = 0.0, custoWarp = 0.0; int medidas = 0;
    for (int d : deslocamentos) {
        Imagem f0      = Recortar(base, MARGEM,         MARGEM, W, H);
        Imagem f1      = Recortar(base, MARGEM + d,     MARGEM, W, H);
        Imagem verdade = Recortar(base, MARGEM + d / 2, MARGEM, W, H);
        Resultado r;
        if (!Rodar(g, f0, f1, verdade, divisor, bloco, 64, &r)) { g.Shutdown(); return 2; }
        printf("%6d | %10.2f %10.2f | %10.2f %10.2f | %10.2f %10.2f\n",
               d, r.segurar.psnr, r.segurar.mae, r.cru.psnr, r.cru.mae,
               r.sintetizar.psnr, r.sintetizar.mae);
        custoUpdate += r.msUpdate; custoWarp += r.msWarp; medidas++;
    }

    printf("\n--- CENARIO B: objeto de 400x300 andando sobre fundo PARADO (oclusao real)\n");
    printf("%6s | %-22s | %-22s | %-22s\n", "px/q", "segurar (hoje)", "cru (alfa/regiao)", "sintetizar (warp)");
    printf("%6s | %10s %10s | %10s %10s | %10s %10s\n", "", "PSNR dB", "MAE", "PSNR dB", "MAE", "PSNR dB", "MAE");
    for (int d : deslocamentos) {
        const int OW = 400, OH = 300, OX = W / 2 - OW / 2, OY = H / 2 - OH / 2;
        Imagem f0 = Recortar(base, MARGEM, MARGEM, W, H);
        Imagem f1 = f0, verdade = f0;
        ColarObjeto(f0,      base, OX,         OY, 700,  500, OW, OH);
        ColarObjeto(f1,      base, OX + d,     OY, 700,  500, OW, OH);
        ColarObjeto(verdade, base, OX + d / 2, OY, 700,  500, OW, OH);
        Resultado r;
        if (!Rodar(g, f0, f1, verdade, divisor, bloco, 64, &r)) { g.Shutdown(); return 2; }
        printf("%6d | %10.2f %10.2f | %10.2f %10.2f | %10.2f %10.2f\n",
               d, r.segurar.psnr, r.segurar.mae, r.cru.psnr, r.cru.mae,
               r.sintetizar.psnr, r.sintetizar.mae);
        custoUpdate += r.msUpdate; custoWarp += r.msWarp; medidas++;
    }

    printf("\n--- CENARIO C: camera transladando em PIXEL FRACIONARIO (o caso real)\n");
    printf("%6s | %-22s | %-22s | %-22s\n", "px/q", "segurar (hoje)", "cru (alfa/regiao)", "sintetizar (warp)");
    printf("%6s | %10s %10s | %10s %10s | %10s %10s\n", "", "PSNR dB", "MAE", "PSNR dB", "MAE", "PSNR dB", "MAE");
    for (int d : deslocamentos) {
        const float dd = (float)d + 0.5f;   // 2,5 / 6,5 / 12,5 ... o meio cai em quarto de pixel
        Imagem f0      = RecortarSub(base, (float)MARGEM,             (float)MARGEM, W, H);
        Imagem f1      = RecortarSub(base, (float)MARGEM + dd,        (float)MARGEM, W, H);
        Imagem verdade = RecortarSub(base, (float)MARGEM + dd * 0.5f, (float)MARGEM, W, H);
        Resultado r;
        if (!Rodar(g, f0, f1, verdade, divisor, bloco, 64, &r)) { g.Shutdown(); return 2; }
        printf("%6.1f | %10.2f %10.2f | %10.2f %10.2f | %10.2f %10.2f\n",
               (double)dd, r.segurar.psnr, r.segurar.mae, r.cru.psnr, r.cru.mae,
               r.sintetizar.psnr, r.sintetizar.mae);
        custoUpdate += r.msUpdate; custoWarp += r.msWarp; medidas++;
    }

    if (medidas) {
        printf("\ncusto por quadro sintetizado: campo %.3f ms + warp %.3f ms = %.3f ms\n",
               custoUpdate / medidas, custoWarp / medidas,
               (custoUpdate + custoWarp) / medidas);
        printf("(campo = motion_estimate.h na fila de VIDEO; warp = 1 passe de tela cheia na fila 3D)\n");
    }

    printf("\nCOMO LER: PSNR mais alto e mais parecido com o quadro que DEVERIA aparecer.\n");
    printf("'segurar' e o que o app faz hoje no quadro pulado; 'sintetizar' so se paga se\n");
    printf("ficar acima dele. 'cru' e a referencia do que os modos alfa/regiao mostram.\n");
    g.Shutdown();
    return 0;
}
