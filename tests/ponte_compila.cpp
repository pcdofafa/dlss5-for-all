// ponte_compila.cpp -- prova de que ponte_feeder_host.h compila sozinho com o
// mesmo compilador, as mesmas flags e a mesma lista de includes do main.cpp
// (copiada do topo dele), sem erro e sem warning:
//
//   cd src && /c/msys64/mingw64/bin/g++ -std=c++17 -O2 -municode -mwindows -Wall -Wextra
//       -Ifeeder-amd/src-feeder/external/ffx -Iexternal/ffmpeg-lgpl/include
//       -fsyntax-only tests/ponte_compila.cpp        (tudo numa linha so)
//
// Nao linka nem roda: A1 e A2 nao compilam o app inteiro (CONTRATO.md).
// A ordem importa: o header vem ANTES da definicao de Log, como no main.cpp,
// e por isso ele declara o prototipo. O wmain referencia cada funcao da
// interface para o -Wunused-function nao apontar as cinco como mortas.
#define _WIN32_WINNT 0x0A00
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
#include <gdiplus.h>

#include "../ponte_feeder_host.h"

static FILE* g_log = stderr;   // no app e o dlss5forall.log; aqui o console, para ver as linhas "ponte:"
static void Log(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

int wmain(int argc, wchar_t**)
{
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    // As duas fases (o caminho do A5): Preparar sem device, Iniciar com ele.
    // Com argc > 2 usa a forma de 3 argumentos do CONTRATO, so para referencia-la.
    PonteHost::Sessao s = {};
    bool ok = argc > 2 ? PonteHost::Iniciar(GetCommandLineW(), dev, ctx)
                       : PonteHost::Preparar(GetCommandLineW(), &s, 100) && PonteHost::Iniciar(dev, ctx);
    if (!ok) { PonteHost::Parar(); return 0; }
    PonteHost::Quadro q = {};
    while (PonteHost::Ativa())
    {
        HANDLE ev = PonteHost::EventoQuadro();
        if (ev != nullptr) WaitForSingleObject(ev, 1);
        if (PonteHost::ObterQuadro(&q)) PonteHost::EntregarSaida(q.cor, q.n);
    }
    PonteHost::Parar();
    return (int)s.largura;
}
