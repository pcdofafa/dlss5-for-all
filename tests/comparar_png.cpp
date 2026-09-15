// =====================================================================================
//  comparar_png.cpp -- compara duas saidas da cadeia em pixel, alinhando antes.
//
//  POR QUE ALINHAR. Os dumps saem de aberturas diferentes do app, e a area util da janela
//  alvo pode diferir por alguns pixels entre uma abertura e outra (1596x891 contra
//  1592x882, medido). Comparar sem alinhar produziria um numero enorme que so mede o
//  desencontro de bordas -- e diria "mudou tudo" quando nao mudou nada.
//
//  Procura o deslocamento inteiro que minimiza o erro e reporta o erro NESSE alinhamento.
//
//  Compilar (msys64):
//    g++ -std=c++17 -O2 -Wall -Wextra tests/comparar_png.cpp -o tests/comparar_png.exe
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
#include <algorithm>

struct Img {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // BGRA
    const uint8_t* At(int x, int y) const { return &px[((size_t)y * (size_t)w + (size_t)x) * 4]; }
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
    im.w = iw; im.h = ih; im.px.resize((size_t)iw * (size_t)ih * 4);
    for (int y = 0; y < ih; y++)
        memcpy(&im.px[(size_t)y * (size_t)iw * 4],
               (const uint8_t*)bd.Scan0 + (INT_PTR)y * bd.Stride, (size_t)iw * 4);
    bmp.UnlockBits(&bd);
    return im;
}

struct Erro { double psnr, mae, maxAbs; double porCanal[3]; };

// Compara b deslocado de (dx,dy) contra a, sobre a area comum com margem.
static Erro Comparar(const Img& a, const Img& b, int dx, int dy, int margem)
{
    Erro e{ 0, 0, 0, { 0, 0, 0 } };
    double soma2 = 0, soma1 = 0; double s1[3] = { 0, 0, 0 };
    size_t n = 0, nc = 0;
    const int x0 = std::max(margem, margem - dx), x1 = std::min(a.w - margem, b.w - margem - dx);
    const int y0 = std::max(margem, margem - dy), y1 = std::min(a.h - margem, b.h - margem - dy);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const uint8_t* p = a.At(x, y);
            const uint8_t* q = b.At(x + dx, y + dy);
            for (int c = 0; c < 3; c++) {
                const double d = (double)p[c] - (double)q[c];
                soma2 += d * d; soma1 += fabs(d); s1[c] += fabs(d);
                if (fabs(d) > e.maxAbs) e.maxAbs = fabs(d);
                n++;
            }
            nc++;
        }
    }
    if (!n) { e.psnr = 0; return e; }
    const double mse = soma2 / (double)n;
    e.psnr = mse > 0 ? 10.0 * log10(255.0 * 255.0 / mse) : 99.0;
    e.mae = soma1 / (double)n;
    for (int c = 0; c < 3; c++) e.porCanal[c] = nc ? s1[c] / (double)nc : 0.0;
    return e;
}

int main(int argc, char** argv)
{
    if (argc < 3) { printf("uso: comparar_png <a.png> <b.png> [busca]\n"); return 1; }
    const int busca = (argc >= 4) ? atoi(argv[3]) : 12;

    Gdiplus::GdiplusStartupInput gi; ULONG_PTR tok = 0;
    if (Gdiplus::GdiplusStartup(&tok, &gi, nullptr) != Gdiplus::Ok) { printf("gdiplus falhou\n"); return 2; }

    Img a = Carregar(argv[1]);
    Img b = Carregar(argv[2]);
    if (a.px.empty() || b.px.empty()) { printf("nao consegui abrir as imagens\n"); Gdiplus::GdiplusShutdown(tok); return 3; }
    printf("A: %s  %dx%d\nB: %s  %dx%d\n", argv[1], a.w, a.h, argv[2], b.w, b.h);

    // O `> 0` que estava aqui era um defeito, e dos que mentem bonito: ele DESCARTAVA o
    // alinhamento perfeito. Com duas imagens identicas, o erro em (0,0) e exatamente zero,
    // a condicao rejeitava esse ponto e o programa escolhia o segundo melhor -- imprimindo
    // "MAE 3,589, dy=-1" para um par de arquivos iguais. Um teste que nunca consegue dizer
    // "igual" nao serve para decidir nada.
    int melhorX = 0, melhorY = 0; double melhorMae = 1e18;
    for (int dy = -busca; dy <= busca; dy++)
        for (int dx = -busca; dx <= busca; dx++) {
            const Erro e = Comparar(a, b, dx, dy, busca + 8);
            if (e.psnr > 0.0 && e.mae < melhorMae) { melhorMae = e.mae; melhorX = dx; melhorY = dy; }
        }

    const Erro e = Comparar(a, b, melhorX, melhorY, busca + 8);
    printf("\nalinhamento: dx=%d dy=%d\n", melhorX, melhorY);
    printf("PSNR   %.2f dB\n", e.psnr);
    printf("MAE    %.3f / 255 por canal\n", e.mae);
    printf("por canal  B=%.3f  G=%.3f  R=%.3f\n", e.porCanal[0], e.porCanal[1], e.porCanal[2]);
    printf("maior diferenca em um canal: %.0f / 255\n", e.maxAbs);

    Gdiplus::GdiplusShutdown(tok);
    return 0;
}
