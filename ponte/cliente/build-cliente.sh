#!/usr/bin/env bash
# build-cliente.sh -- compila a DLL injetada dlss5-ponte64.dll (B1, fase 2; G1, fase 3).
#
# Toolchain: g++/gcc do MinGW-w64 em C:\msys64\mingw64\bin. Rode do msys2, ou do
# PowerShell com C:\msys64\mingw64\bin na FRENTE do PATH (sem isso o g++ nao
# acha as proprias DLLs e sai com codigo 1 sem mensagem).
#
# Saida: src\ponte\cliente\dlss5-ponte64.dll
#
# Fontes:
#   ponte_cliente.cpp            -- a ponte (B1/F0) + os pontos de entrada do NGX (G1)
#   ngx/ngx_hook.cpp             -- hook dos exports do _nvngx.dll (sonda/upstream/controle)
#   minhook/src/*.c, hde/hde64.c -- MinHook (BSD-2, TsudaKageyu/minhook, vendorado com LICENSE.txt)
#
# -shared            : e uma DLL
# -static-libgcc/-static-libstdc++ : sem depender das libs do gcc na pasta do jogo
# -O2                : o contrato pede
# nada de -municode  : a DLL nao tem main/WinMain proprio
# Sem .def / sem exports: entra por LoadLibraryW.
set -e

AQUI="$(cd "$(dirname "$0")" && pwd)"
export PATH="/c/msys64/mingw64/bin:$PATH"
OBJ="$AQUI/obj"
mkdir -p "$OBJ"

# MinHook (C). hde32.c so entra em x86; aqui e x64 puro.
for f in buffer hook trampoline; do
    gcc -O2 -c -o "$OBJ/mh_$f.o" "$AQUI/minhook/src/$f.c"
done
gcc -O2 -c -o "$OBJ/mh_hde64.o" "$AQUI/minhook/src/hde/hde64.c"

# NGX (C++): sonda + upstream
g++ -std=c++17 -O2 -c -o "$OBJ/ngx_hook.o" "$AQUI/ngx/ngx_hook.cpp"
g++ -std=c++17 -O2 -c -o "$OBJ/ngx_upstream.o" "$AQUI/ngx/ngx_upstream.cpp"

# Q2 (degrau 2): hook de FSR 2/3.1 e XeSS -> DLSS SR pelo app. Os cabecalhos ffx_api (MIT, SDK
# 1.1) vem de feeder-amd por -I; -lversion e pela versao de arquivo do ffx_fsr2_api_x64.dll.
g++ -std=c++17 -O2 -I"$AQUI/../../feeder-amd/src-feeder/external/ffx" -c -o "$OBJ/sr_hook.o" "$AQUI/sr/sr_hook.cpp"

# Q4 (fase 4): frame generation D3D12 dentro do jogo, com o frame interpolation do FidelityFX 3.1
# (amd_fidelityfx_dx12.dll, carregado por caminho absoluto passado no arquivo de controle). Os
# mesmos cabecalhos ffx_api do Q2/do app.
g++ -std=c++17 -O2 -I"$AQUI/../../feeder-amd/src-feeder/external/ffx" -c -o "$OBJ/fg_hook.o" "$AQUI/fg/fg_hook.cpp"

# X-FG-6: o pipe .up compartilhado (um dono por vez) e a colheita de depth/MV sem sessao .up
g++ -std=c++17 -O2 -c -o "$OBJ/pipe_up.o" "$AQUI/pipe_up.cpp"
g++ -std=c++17 -O2 -c -o "$OBJ/colheita.o" "$AQUI/dados/colheita.cpp"

# X-DLL-2: contador de fps dentro do jogo (C2) e modo de exibicao/tela cheia (E8)
g++ -std=c++17 -O2 -c -o "$OBJ/tela.o" "$AQUI/tela/tela.cpp"

# A ponte + link
g++ -std=c++17 -O2 \
    -static-libgcc -static-libstdc++ \
    -shared \
    -o "$AQUI/dlss5-ponte64.dll" \
    "$AQUI/ponte_cliente.cpp" \
    "$OBJ/ngx_hook.o" "$OBJ/ngx_upstream.o" "$OBJ/sr_hook.o" "$OBJ/fg_hook.o" "$OBJ/tela.o" "$OBJ/pipe_up.o" "$OBJ/colheita.o" "$OBJ/mh_buffer.o" "$OBJ/mh_hook.o" "$OBJ/mh_trampoline.o" "$OBJ/mh_hde64.o" \
    -Wl,--enable-stdcall-fixup \
    -Wl,--exclude-all-symbols \
    -ld3d11 -ld3d12 -ldxgi -ld3dcompiler -ldxguid -lole32 -luuid -lversion -lwinmm \
    -Wl,-Bstatic -lstdc++ -lpthread -Wl,-Bdynamic

echo "OK: $AQUI/dlss5-ponte64.dll"
ls -l "$AQUI/dlss5-ponte64.dll"
