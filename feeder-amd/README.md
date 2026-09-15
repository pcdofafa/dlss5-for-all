# DLSS5-Feeder — variante AMD (build MinGW)

Variante do add-on `dlss5-feed.addon64` (github.com/jlrouzies-fr/DLSS5-Feeder, MIT) compilada
com o g++ do msys64, com dois modos a mais, escolhidos pelo `dlss5-feed.cfg`:

- **portador FSR** (`amd_carrier`): numa Radeon o NGX nao inicializa; em vez dele o Feeder
  carrega `amd_fidelityfx_dx12.dll` (FidelityFX SDK 1.1.4, MIT), cria um contexto FSR 3.1
  1:1 (NativeAA) no seu device D3D12 privado e despacha por frame com as mesmas texturas de
  cor/depth/MV. O mod DLSS-NR-on-AMD hooka esse `ffxCreateContext/ffxDispatch` e aplica a
  rede do DLSS 5 em cima. Numa RTX o mesmo binario segue pelo NGX de sempre (ver abaixo).
- **swapchain-isca** (`amd_decoy_swapchain`): swapchain D3D12 numa janela oculta, na fila do
  Feeder, apresentado antes do primeiro Present do jogo, para o mod adotar o device certo.

Build: `bash build-feed-amd.sh` -> `build\dlss5-feed-amd.addon64`. Pasta `dist\` tem o que vai
para o `bin-amd` (addon ja renomeado, a DLL do FSR, o cfg e as licencas).

Pecas:

- `src-feeder\` clone do upstream com as ramificacoes `FEED_AMD_VARIANT` (diff em `patches-feeder-amd.diff`).
- `src-amd\feed_amd.h` portador + isca; `feed_seh_shim.h` substitui `__try/__except` (VEH + longjmp);
  `ngx_forward.cpp` substitui a `nvsdk_ngx_d.lib`: repassa ao `_nvngx.dll` do driver quando ele
  existe (NVIDIA: DLSS real), senao devolve `FeatureNotSupported` (AMD: o portador assume).
- `src-feeder\external\reshade\include` headers do ReShade com o shim de ABI MinGW<->MSVC: o MSVC
  devolve struct de funcao-membro por ponteiro oculto em RDX, o g++ nao; os 8 metodos virtuais da
  API que devolvem struct por valor sao chamados por thunk. `external\ngx\nvsdk_ngx_params.h`
  declara `NVSDK_NGX_Parameter` na ordem de vtable do MSVC (sobrecargas invertidas).

Relatorio completo, testes e logs: `scratchpad\feeder-amd\RELATORIO-FEEDER-AMD.md` desta sessao.
