#!/usr/bin/env bash
# Compila o DLSS5 For All com MinGW-w64 (msys2).
# No Windows: usa o g++ do msys64 que ja esta instalado.
set -e

# O g++ do msys64 precisa das DLLs de C:\msys64\mingw64\bin no PATH. Sem isto, chamar
# o compilador pelo caminho absoluto faz o cc1plus morrer em silencio: sai 1 e nao
# imprime nada. Ja custou rodadas inteiras de depuracao. Igual ao build-addon.sh.
export PATH="/c/msys64/mingw64/bin:$PATH"

CXX=${CXX:-}
if [ -z "$CXX" ]; then
  for c in /c/msys64/mingw64/bin/g++ /c/msys64/ucrt64/bin/g++ x86_64-w64-mingw32-g++ g++; do
    command -v "$c" >/dev/null 2>&1 && { CXX="$c"; break; }
  done
fi
[ -z "$CXX" ] && { echo "g++ do MinGW-w64 nao encontrado"; exit 1; }
OUT=${1:-DLSS5ForAll.exe}

# ---------------------------------------------------------------- recursos (icone + versao)
# O icone e o VERSIONINFO entram pelo app.rc, compilado por windres num .o comum que
# depois vai junto no link. Sem isto o exe sai com o icone branco padrao do Windows.
#
# O .ico e DERIVADO da foto (marca\gerar-icone.ps1) e por isso pode nao existir numa
# copia recem-clonada: se faltar, o build segue sem ele em vez de morrer - um exe sem
# icone e melhor que nenhum exe. Mas avisa alto, porque publicar sem icone e o tipo de
# coisa que so se descobre depois do zip pronto.
RES_OBJ=""
if [ -f app.rc ]; then
  if [ ! -f marca/DLSS5ForAll.ico ]; then
    echo "AVISO: marca/DLSS5ForAll.ico nao existe - o exe vai sair SEM icone."
    echo "       gere com: powershell -ExecutionPolicy Bypass -File .\\marca\\gerar-icone.ps1"
  else
    WINDRES=$(command -v windres || command -v x86_64-w64-mingw32-windres || true)
    if [ -z "$WINDRES" ]; then
      echo "AVISO: windres nao encontrado no PATH - o exe vai sair SEM icone."
    else
      "$WINDRES" app.rc -O coff -o app.res.o || { echo "falhou ao compilar app.rc"; exit 1; }
      RES_OBJ=app.res.o
    fi
  fi
fi

# -Wall -Wextra sempre: os dois unicos avisos reais que apareceram aqui (destrutor nao
# virtual num handler COM, indentacao enganosa) eram defeitos de verdade.
"$CXX" -std=c++17 -O2 -municode -mwindows -Wall -Wextra main.cpp $RES_OBJ -o "$OUT" \
  -ld3d11 -ld3d12 -ldxgi -ldxguid \
  -lmfplat -lmf -lmfreadwrite -lmfuuid \
  -lole32 -loleaut32 -lcomctl32 -ldwmapi -lruntimeobject -lgdiplus -lgdi32 \
  -static -static-libgcc -static-libstdc++
# O ADD-ON JUNTO. Publicar exe novo com .addon64 velho e silencioso e caro: o resolve()
# do reshade_bridge.h recusa protocolo diferente sem dizer nada, e a LUT e a nitidez
# simplesmente param de responder no painel. SKIP_ADDON=1 pula, para iterar so no exe.
if [ -z "$SKIP_ADDON" ] && [ -f ./addon/build-addon.sh ]; then
  ( cd addon && bash ./build-addon.sh ) || { echo "falhou ao compilar o add-on da ponte"; exit 1; }
fi
echo "ok -> $OUT"
