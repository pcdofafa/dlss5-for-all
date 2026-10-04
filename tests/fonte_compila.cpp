// fonte_compila.cpp -- teste de SINTAXE do fonte_ponte.h (A2), com os mesmos includes do
// topo do main.cpp e as mesmas flags do build-mingw.sh. Nao linka nem roda.
//
//   cd src
//   /c/msys64/mingw64/bin/g++ -std=c++17 -O2 -municode -Wall -Wextra -fsyntax-only
//       -Itests/ponte_stub tests/fonte_compila.cpp        (tudo numa linha so)
//
// -Itests/ponte_stub: enquanto src\ponte_feeder_host.h (A1) nao existe, o include cai no
// stub (tests\ponte_stub.h). Com o header real em src\, ele ganha sozinho e a flag sobra.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <deque>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <objidl.h>
using std::min;
using std::max;
#include <gdiplus.h>

#include "../shared_source.h"
#include "../fonte_ponte.h"

// o Log do main.cpp, com a mesma assinatura variadica
static void Log(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
}

// Exercita TODA a interface do contrato e os extras, para o -fsyntax-only ver cada chamada.
int wmain() {
    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    FontePonte::g_log = &Log;
    if (!FontePonte::Ligar(dev, ctx)) return 0;
    ID3D11ShaderResourceView* srv = nullptr; UINT w = 0, h = 0; uint64_t n = 0;
    while (FontePonte::Ativa()) {
        if (FontePonte::Puxar(&srv, &w, &h, &n)) {
            ID3D11Texture2D* saida = nullptr;
            FontePonte::Devolver(FontePonte::ColherSaida(&saida) ? saida : nullptr, n);
        }
        FontePonte::FecharQuadro();
        Sleep(FontePonte::EsperaMs(100));
    }
    UINT jw = 0, jh = 0; HWND jan = nullptr;
    (void)FontePonte::MedirJanelaDoJogo(0, &jw, &jh, &jan);
    (void)FontePonte::QuadroPendente(); (void)FontePonte::NumeroQuadro();
    (void)FontePonte::LarguraQuadro(); (void)FontePonte::AlturaQuadro();
    (void)FontePonte::FormatoQuadro(); (void)FontePonte::QpcChegada();
    FontePonte::Desligar();
    return 0;
}
