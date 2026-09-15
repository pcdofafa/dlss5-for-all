#!/usr/bin/env bash
# Compila o launcher da versao portable com o MinGW-w64 do msys2.
# Sai em: portable-launcher\DLSS5ForAll.exe (o montar-portable.ps1 copia para a raiz do portable).
# O launcher inclui a tela do grupo (../splash_ofertas.h, ../qr.h, ../idioma.h) e enumera as
# placas pelo DXGI - dai o -I.. e as bibliotecas a mais em relacao ao launcher antigo.
# winhttp e bcrypt sao da verificacao/atualizacao de versao (../atualizacao.h).
# ole32/uuid sao da vitrine (../vitrine.h, WebView2). O SDK fica em webview2\: include\WebView2.h,
# a WebView2Loader.dll x64 e a LICENSE-WebView2.txt (vieram do pacote mingw-w64-ucrt-x86_64-
# webview2-loader 1.0.3912.50 do msys2; preparar-webview2-sdk.ps1 monta a mesma pasta a partir
# do .nupkg Microsoft.Web.WebView2 baixado pelo navegador, cuja DLL e assinada pela Microsoft).
# A DLL do loader NAO e linkada: vitrine.h a carrega com LoadLibrary, entao o launcher abre
# mesmo onde nao ha runtime. Ela precisa ficar AO LADO do exe: este script a copia para ca e o
# montar-portable.ps1 para o pacote. -DDLSS5_LAUNCHER marca o build do launcher (o main.cpp
# inclui o mesmo splash_ofertas.h sem essa macro).
set -e
cd "$(dirname "$0")"
export PATH="/c/msys64/mingw64/bin:$PATH"
if [ ! -f webview2/include/WebView2.h ]; then
    echo "falta webview2/include/WebView2.h: rode preparar-webview2-sdk.ps1 (extrai do .nupkg em Downloads)" >&2
    exit 1
fi
# --codepage=65001: o launcher.rc e UTF-8 e tem acento no CompanyName do VERSIONINFO. Sem
# isso o windres o leria na pagina de codigo ANSI da maquina e o nome sairia errado nas
# Propriedades do arquivo. O proprio .rc tambem tem #pragma code_page(65001).
windres --codepage=65001 launcher.rc -O coff -o launcher.res.o
g++ -std=c++17 -O2 -municode -mwindows -Wall -Wextra -DDLSS5_LAUNCHER -I.. -Iwebview2/include launcher.cpp launcher.res.o -o DLSS5ForAll.exe \
    -ldxgi -ldxguid -lgdi32 -lshell32 -luser32 -lkernel32 -lwinhttp -lbcrypt -lole32 -luuid \
    -static -static-libgcc -static-libstdc++
cp -f webview2/WebView2Loader.dll ./WebView2Loader.dll
echo "ok -> portable-launcher/DLSS5ForAll.exe (+ WebView2Loader.dll)"
