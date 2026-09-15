#!/usr/bin/env bash
# Compila a ponte de controle (dlss5-bridge.addon64) com MinGW-w64 (msys2).
#
#   ./addon/build-addon.sh                 -> gera ./dlss5-bridge.addon64 na raiz
#   ./addon/build-addon.sh /caminho/x.addon64
#
# O g++ do msys64 precisa das DLLs de C:\msys64\mingw64\bin no PATH; se voce
# chamar o compilador pelo caminho absoluto sem ajustar o PATH, o cc1plus morre
# em silencio (sai 1, nao imprime nada). Por isso o export abaixo.
set -e

export PATH="/c/msys64/mingw64/bin:$PATH"

CXX=${CXX:-}
if [ -z "$CXX" ]; then
  for c in /c/msys64/mingw64/bin/g++ /c/msys64/ucrt64/bin/g++ x86_64-w64-mingw32-g++ g++; do
    command -v "$c" >/dev/null 2>&1 && { CXX="$c"; break; }
  done
fi
[ -z "$CXX" ] && { echo "g++ do MinGW-w64 nao encontrado"; exit 1; }

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT=${1:-"$HERE/../dlss5-bridge.addon64"}

"$CXX" -std=c++17 -O2 -shared \
  -I"$HERE/include/reshade" \
  "$HERE/dlss5_bridge_addon.cpp" -o "$OUT" \
  -static -static-libgcc -static-libstdc++ \
  -lkernel32 -lgdi32 -luser32 -lole32

echo "ok -> $OUT"
