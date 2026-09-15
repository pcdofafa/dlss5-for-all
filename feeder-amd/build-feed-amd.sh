#!/usr/bin/env bash
# Compila a VARIANTE AMD do DLSS5-Feeder (dlss5-feed-amd.addon64) com o g++ do msys64.
#
#   bash build-feed-amd.sh                  -> ./build/dlss5-feed-amd.addon64
#   bash build-feed-amd.sh /caminho/x.addon64
#
# O que entra:
#   src-feeder/src/dlss5-feed.cpp   fonte do Feeder (MIT) com as ramificacoes FEED_AMD_VARIANT
#   src-amd/feed_amd.h              modo portador FSR + swapchain-isca
#   src-amd/feed_seh_shim.h         __try/__except -> VEH + longjmp (o g++ nao tem SEH)
#   src-amd/ngx_forward.cpp         substitui a nvsdk_ngx_d.lib: repassa ao _nvngx.dll do
#                                   driver quando ele existe, senao devolve FeatureNotSupported
#   src-feeder/external/minhook     os quatro .c do MinHook (compilados como C, com gcc)
#   version-amd.rc                  VERSIONINFO via windres
#
# Headers: reshade (repo), imgui (repo), minhook (repo), ngx (headers publicos do NVIDIA/DLSS,
# copiados para src-feeder/external/ngx), vulkan (Khronos, src-feeder/external/vulkan),
# ffx-api 1.1.4 (src-feeder/external/ffx).
#
# O g++ do msys64 precisa das DLLs de C:\msys64\mingw64\bin no PATH; chamado pelo caminho
# absoluto sem isso o cc1plus morre em silencio (sai 1, nao imprime nada). Igual ao
# build-addon.sh do projeto.
set -e

export PATH="/c/msys64/mingw64/bin:$PATH"

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/src-feeder"
AMD="$HERE/src-amd"
OUT_DIR="$HERE/build"
OUT=${1:-"$OUT_DIR/dlss5-feed-amd.addon64"}
mkdir -p "$OUT_DIR"

CXX=${CXX:-}
if [ -z "$CXX" ]; then
  for c in /c/msys64/mingw64/bin/g++ /c/msys64/ucrt64/bin/g++ x86_64-w64-mingw32-g++ g++; do
    command -v "$c" >/dev/null 2>&1 && { CXX="$c"; break; }
  done
fi
[ -z "$CXX" ] && { echo "g++ do MinGW-w64 nao encontrado"; exit 1; }
CC="${CXX%g++}gcc"
command -v "$CC" >/dev/null 2>&1 || CC=gcc
WINDRES=$(command -v windres || command -v x86_64-w64-mingw32-windres || true)

for f in "$SRC/src/dlss5-feed.cpp" "$AMD/feed_amd.h" "$AMD/feed_seh_shim.h" "$AMD/ngx_forward.cpp" \
         "$SRC/external/ngx/nvsdk_ngx.h" "$SRC/external/vulkan/vulkan/vulkan.h" "$SRC/external/ffx/ffx_api/ffx_api.h"; do
  [ -f "$f" ] || { echo "falta $f"; exit 1; }
done

# Array, nao string: o caminho do projeto tem espacos.
INC=(-I"$SRC/external/reshade/include" -I"$SRC/external/ngx" -I"$SRC/external/vulkan" -I"$SRC/external/imgui"
     -I"$SRC/external/minhook/include" -I"$SRC/external/ffx" -I"$AMD" -I"$SRC/src")

# ---------------------------------------------------------------- recursos (versao)
RES_OBJ=()
if [ -n "$WINDRES" ] && [ -f "$HERE/version-amd.rc" ]; then
  "$WINDRES" "$HERE/version-amd.rc" -O coff -o "$OUT_DIR/version-amd.res.o"
  RES_OBJ=("$OUT_DIR/version-amd.res.o")
else
  echo "AVISO: windres nao encontrado ou version-amd.rc ausente - o .addon64 sai sem VERSIONINFO"
fi

# ---------------------------------------------------------------- MinHook (C)
MH_OBJS=()
for c in buffer hook trampoline hde/hde64; do
  o="$OUT_DIR/mh_$(basename "$c").o"
  "$CC" -O2 -c "$SRC/external/minhook/src/$c.c" -I"$SRC/external/minhook/include" -o "$o"
  MH_OBJS+=("$o")
done

# ---------------------------------------------------------------- forwarder NGX (C++)
"$CXX" -std=c++20 -O2 -c "$AMD/ngx_forward.cpp" "${INC[@]}" -o "$OUT_DIR/ngx_forward.o"

# ---------------------------------------------------------------- o add-on
# -Wno-attributes: os __declspec(novtable) dos headers do ReShade (mesmo aviso que o
# add-on da ponte do projeto). -DFEED_AMD_VARIANT liga as ramificacoes; sem ela o fonte
# volta a ser o do upstream (e nao compila no g++, por causa do SEH).
"$CXX" -std=c++20 -O2 -shared -DFEED_AMD_VARIANT -Wno-attributes \
  "${INC[@]}" \
  "$SRC/src/dlss5-feed.cpp" "$OUT_DIR/ngx_forward.o" "${MH_OBJS[@]}" "${RES_OBJ[@]}" \
  -o "$OUT" \
  -static -static-libgcc -static-libstdc++ \
  -ld3d11 -ld3d12 -ldxgi -ldxguid -lversion -lole32 -ladvapi32 -luser32 -lgdi32 -lkernel32

ls -la "$OUT"
echo "ok -> $OUT"
