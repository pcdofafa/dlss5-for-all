# tests/

Testes pequenos e de rodar à mão. Cada um existe para responder **uma** pergunta que
custaria uma sessão de depuração dentro do jogo. Nenhum precisa do ReShade, do add-on
carregado ou de um jogo aberto.

| Arquivo | O que prova |
|---|---|
| `nr_device_probe.cpp` | **O runtime D3D12 recicla o device por adaptador.** `D3D12CreateDevice` com o mesmo adaptador e o mesmo feature level devolve `S_OK` e o **mesmo objeto**, só com o refcount incrementado — inclusive quando o `IDXGIAdapter1` é outro objeto, vindo de `EnumAdapterByLuid`. A identidade COM (`QueryInterface` de `IUnknown`) confirma. Consequência: **não existe "criar um device D3D12 privado"** no mesmo adaptador para isolar uma sessão NGX; um `Release()` nesse "device privado" apenas decrementa uma referência do device que já estava em uso. |
| `seh_shim_test.cpp` | O substituto de `__try`/`__except` do build MinGW (`src-amd/feed_seh_shim.h`) captura uma falha de hardware e volta pelo caminho da guarda com o código certo. |
| `abi_ret_test.cpp` | O retorno de struct por valor nos métodos virtuais do ReShade atravessa a fronteira de ABI MinGW↔MSVC pelos thunks (o ponteiro oculto em RDX que o g++ não usa e o MSVC usa). |

## Como rodar

```sh
export PATH="/c/msys64/mingw64/bin:$PATH"
g++ -std=c++20 -O2 tests/nr_device_probe.cpp -o build/nr_device_probe.exe -ld3d12 -ldxgi -ldxguid
./build/nr_device_probe.exe
```

Saída obtida numa RTX 4080 (2026-09-08), que é o resultado que o `nr_device_probe`
existe para deixar reconferível:

```
create #1 (adapter a0)       ptr=0000018fd82045d0
create #2 (adapter a0)       ptr=0000018fd82045d0  -> MESMO OBJETO? SIM
EnumAdapterByLuid            ptr=0000018fde79e4c0  (o mesmo objeto de a0? nao)
create #3 (adapter por LUID) ptr=0000018fd82045d0  -> MESMO QUE #1? SIM
IUnknown de #1 = ...4000, de #3 = ...4000  -> mesma identidade COM? SIM
refcount de #1 depois de AddRef: 6
```
