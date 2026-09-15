// =====================================================================================
//  nitidez_png.cpp -- mede NITIDEZ de um PNG, para comparar saidas da cadeia.
//
//  POR QUE ISTO EXISTE. O comparar_png.cpp responde "quanto duas imagens diferem"; nao
//  responde "qual das duas tem mais detalhe". A pergunta do modo de rede em resolucao
//  reduzida e exatamente a segunda: se o residuo preserva o detalhe fino que uma
//  esticada bilinear perderia. O concorrente publica essa medida como "laplaciano" e
//  compara 983 (residual) contra 88 (bilinear); sem uma medida nossa, o numero deles nao
//  tem com o que ser confrontado.
//
//  DUAS MEDIDAS, de proposito, porque elas erram de jeitos diferentes:
//
//    laplaciano  variancia do operador laplaciano 3x3 sobre a luminancia. E a medida
//                classica de foco; sobe com detalhe fino E com ruido, entao nunca deve
//                ser lida sozinha.
//    gradiente   energia media do gradiente (Sobel) na luminancia. Mais estavel, menos
//                sensivel a ruido de um pixel so.
//
//  Uma BORDA e descartada (padrao 16 px). A area util da janela varia alguns pixels
//  entre aberturas do app, e a moldura entra na conta como se fosse detalhe.
//
//  Uso:
//     nitidez_png.exe <a.png> [b.png ...] [--borda N]
//
//  Compilar (msys64):
//    g++ -std=c++17 -O2 -Wall -Wextra tests/nitidez_png.cpp -o tests/nitidez_png.exe
//        -lgdiplus -static -static-libgcc -static-libstdc++
// =====================================================================================

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

struct Img {
    int w = 0, h = 0;
    std::vector<float> luma;   // 0..255
};

static Img Carregar(const char* caminho)
{
    Img im;
    const int n = MultiByteToWideChar(CP_ACP, 0, caminho, -1, nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, caminho, -1, &w[0], n);

    Gdiplus::Bitmap bmp(w.c_str(), FALSE);
    if (bmp.GetLastStatus() != Gdiplus::Ok) return im;
    const int iw = (int)bmp.GetWidth(), ih = (int)bmp.GetHeight();
    Gdiplus::Rect r(0, 0, iw, ih);
    Gdiplus::BitmapData bd{};
    if (bmp.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) return im;
    im.w = iw; im.h = ih; im.luma.resize((size_t)iw * (size_t)ih);
    for (int y = 0; y < ih; y++) {
        const uint8_t* row = (const uint8_t*)bd.Scan0 + (INT_PTR)y * bd.Stride;
        for (int x = 0; x < iw; x++) {
            // BGRA. Rec.601, a mesma luminancia que o resto da bancada usa.
            const float b = row[x * 4 + 0], g = row[x * 4 + 1], rr = row[x * 4 + 2];
            im.luma[(size_t)y * (size_t)iw + (size_t)x] = 0.114f * b + 0.587f * g + 0.299f * rr;
        }
    }
    bmp.UnlockBits(&bd);
    return im;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("uso: nitidez_png <a.png> [b.png ...] [--borda N]\n");
        return 1;
    }
    int borda = 16;
    std::vector<std::string> arquivos;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--borda") == 0 && i + 1 < argc) { borda = atoi(argv[++i]); continue; }
        arquivos.push_back(argv[i]);
    }

    Gdiplus::GdiplusStartupInput gi; ULONG_PTR tok = 0;
    if (Gdiplus::GdiplusStartup(&tok, &gi, nullptr) != Gdiplus::Ok) { printf("gdiplus falhou\n"); return 2; }

    printf("%-46s %10s %10s %10s %10s\n", "arquivo", "tamanho", "laplac.", "gradiente", "media");
    for (const std::string& f : arquivos) {
        const Img im = Carregar(f.c_str());
        if (im.luma.empty()) { printf("%-46s  (nao abriu)\n", f.c_str()); continue; }
        const int x0 = borda, y0 = borda, x1 = im.w - borda, y1 = im.h - borda;
        if (x1 - x0 < 8 || y1 - y0 < 8) { printf("%-46s  (borda grande demais)\n", f.c_str()); continue; }

        double somaL = 0, somaL2 = 0, somaG = 0, somaMed = 0;
        size_t n = 0;
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++) {
                const float* p = &im.luma[(size_t)y * (size_t)im.w + (size_t)x];
                const int W = im.w;
                // Laplaciano 3x3 (4 vizinhos), o operador padrao da medida de foco.
                const double lap = -4.0 * p[0] + p[-1] + p[1] + p[-W] + p[W];
                somaL  += lap;
                somaL2 += lap * lap;
                // Sobel.
                const double gx = (p[-W + 1] + 2.0 * p[1] + p[W + 1]) - (p[-W - 1] + 2.0 * p[-1] + p[W - 1]);
                const double gy = (p[W - 1] + 2.0 * p[W] + p[W + 1]) - (p[-W - 1] + 2.0 * p[-W] + p[-W + 1]);
                somaG += sqrt(gx * gx + gy * gy);
                somaMed += p[0];
                ++n;
            }
        }
        const double media = somaL / (double)n;
        const double varLap = somaL2 / (double)n - media * media;
        printf("%-46s %5dx%-4d %10.1f %10.2f %10.2f\n",
               f.c_str(), im.w, im.h, varLap, somaG / (double)n, somaMed / (double)n);
    }
    Gdiplus::GdiplusShutdown(tok);
    return 0;
}
