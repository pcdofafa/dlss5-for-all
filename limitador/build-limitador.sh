#!/usr/bin/env bash
# Compila as duas pecas do limitador de fps, com o mesmo MinGW que o resto do projeto usa.
#
#   dlss5-fpslimit.dll   a peca que segura o quadro (gancho no Present + o ritmo)
#   dxgi.dll             a casca que o jogo carrega e que so repassa
#
# AS DUAS VAO JUNTAS para a pasta do jogo. A casca sozinha nao faz nada alem de repassar, e a
# peca sozinha nunca e carregada. O app instala as duas ou nenhuma.
#
# NAO COMPILE ISTO PARA DENTRO DE amd\: sao bibliotecas para a pasta de UM JOGO, e um dxgi.dll
# ao lado do DLSS5ForAll.exe seria carregado pelo proprio app, que ja tem o dxgi do ReShade ali.
# A saida vai para build/ e quem a leva para o jogo e o app.
set -eu
export PATH="/c/msys64/mingw64/bin:$PATH"

AQUI="$(cd "$(dirname "$0")" && pwd)"
SRC="$AQUI/../feeder-amd/src-feeder"          # o MinHook mora com o Feeder, e ja e do projeto
OUT="$AQUI/build"
mkdir -p "$OUT"

CXX=""
for c in /c/msys64/mingw64/bin/g++ /c/msys64/ucrt64/bin/g++ x86_64-w64-mingw32-g++ g++; do
  command -v "$c" >/dev/null 2>&1 && { CXX="$c"; break; }
done
[ -z "$CXX" ] && { echo "g++ do MinGW-w64 nao encontrado"; exit 1; }
CC="${CXX%++}cc"; command -v "$CC" >/dev/null 2>&1 || CC="/c/msys64/mingw64/bin/gcc"

echo "== MinHook (C) =="
# Os quatro .c do MinHook, compilados como C. Sao os mesmos que a variante AMD do Feeder ja usa,
# entao a licenca (BSD de duas clausulas) ja esta aceita e presente no projeto.
# ARRAY, e nao string: o caminho do projeto tem espacos ("dlss5 for all portable"), e uma lista
# de objetos guardada em string se parte em pedacos na hora de chamar o linkador. O erro que
# isso da e enganoso - "cannot find C:/Users/.../dlss5" - e parece arquivo faltando.
MH_OBJ=()
for c in buffer hook trampoline hde/hde64; do
  o="$OUT/mh_$(basename "$c").o"
  [ -f "$SRC/external/minhook/src/$c.c" ] || { echo "faltando: $SRC/external/minhook/src/$c.c"; exit 1; }
  "$CC" -O2 -c "$SRC/external/minhook/src/$c.c" -I"$SRC/external/minhook/include" -o "$o"
  MH_OBJ+=("$o")
done
echo "  ok"

echo "== dlss5-fpslimit.dll (a peca) =="
# -static-libgcc -static-libstdc++: a biblioteca entra no processo do JOGO, que nao tem as DLLs
# do runtime do MinGW ao lado. Sem isto ela nao carrega na maquina de quem baixa, e a falha
# aparece como "o limitador nunca liga" sem nenhuma pista.
"$CXX" -std=c++17 -O2 -shared -municode \
  -I"$SRC/external/minhook/include" -I"$AQUI" \
  "$AQUI/fpslimit_dll.cpp" "${MH_OBJ[@]}" \
  -o "$OUT/dlss5-fpslimit.dll" \
  -static -static-libgcc -static-libstdc++ \
  -ld3d11 -ldxgi -lpsapi -ldwmapi -Wl,--enable-stdcall-fixup
echo "  ok -> $OUT/dlss5-fpslimit.dll"

echo "== dxgi.dll (a casca) =="
"$CXX" -std=c++17 -O2 -shared \
  "$AQUI/fpslimit_wrapper.cpp" \
  -o "$OUT/dxgi.dll" \
  -static -static-libgcc -static-libstdc++
echo "  ok -> $OUT/dxgi.dll"

echo
echo "== conferencia: a casca exporta o que o jogo vai pedir? =="
# Se um destes nomes faltar, o jogo que o chamar NAO ABRE. Vale conferir a cada build.
FALTOU=0
for f in CreateDXGIFactory CreateDXGIFactory1 CreateDXGIFactory2 \
         DXGIGetDebugInterface1 DXGIDeclareAdapterRemovalSupport; do
  # O padrao casa o nome no FIM da linha, precedido de espaco. A primeira versao exigia "] "
  # logo antes do nome, e a linha do objdump traz "+base[  1]  0000 Nome" - o ordinal e o hint
  # entram no meio. O resultado era "FALTOU" nos cinco, com os cinco presentes: mais uma
  # verificacao mentindo com resposta plausivel.
  if objdump -p "$OUT/dxgi.dll" 2>/dev/null | grep -qE "[[:space:]]$f\$"; then
    echo "  ok      $f"
  else
    echo "  FALTOU  $f"; FALTOU=1
  fi
done
[ "$FALTOU" = 0 ] || { echo; echo "ATENCAO: a casca esta incompleta e travaria o jogo na abertura."; exit 1; }
echo
echo "pronto. As duas vao JUNTAS para a pasta do jogo."
