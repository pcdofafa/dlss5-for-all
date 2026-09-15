#!/usr/bin/env bash
# Compila os testes do limitador. Nenhum deles precisa de jogo, de placa de video ou da maquina
# do dono: rodam sozinhos e saem com codigo 0 quando passam.
#
#   teste-ritmo.exe      o nucleo do ritmo: 30, 45, 60 e 120 sem placa e sem jogo
#   teste-reversao.exe   instalar e remover, INCLUSIVE os modos de falha que davam mensagem errada
#   teste-inerte.dll     uma DLL vazia, so para o teste poder carregar algo e provar que o
#                        Windows recusa apagar imagem mapeada (ver teste-inerte.cpp)
#
# O teste-reversao precisa das pecas de verdade em build/, entao rode build-limitador.sh antes.
set -eu
export PATH="/c/msys64/mingw64/bin:$PATH"
AQUI="$(cd "$(dirname "$0")" && pwd)"

CXX=""
for c in /c/msys64/mingw64/bin/g++ /c/msys64/ucrt64/bin/g++ x86_64-w64-mingw32-g++ g++; do
  command -v "$c" >/dev/null 2>&1 && { CXX="$c"; break; }
done
[ -z "$CXX" ] && { echo "g++ do MinGW-w64 nao encontrado"; exit 1; }

echo "== teste-inerte.dll =="
"$CXX" -O2 -shared "$AQUI/teste-inerte.cpp" -o "$AQUI/teste-inerte.dll" \
  -static -static-libgcc -static-libstdc++
echo "  ok -> teste-inerte.dll"

echo "== teste-reversao.exe =="
# -municode: o teste usa wmain, para lidar com caminho com acento sem conversao no meio.
"$CXX" -std=c++17 -O2 -municode "$AQUI/teste-reversao.cpp" \
  -o "$AQUI/teste-reversao.exe" \
  -static -static-libgcc -static-libstdc++
echo "  ok -> teste-reversao.exe"

echo "== teste-ritmo.exe =="
"$CXX" -std=c++17 -O2 "$AQUI/teste-ritmo.cpp" -o "$AQUI/teste-ritmo.exe" \
  -static -static-libgcc -static-libstdc++
echo "  ok -> teste-ritmo.exe"

echo
echo "pronto. Rode:  ./teste-reversao.exe   e   ./teste-ritmo.exe"
