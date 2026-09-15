# RELATÓRIO — Variante AMD do DLSS5-Feeder (portador FSR + swapchain-isca), build MinGW

Data: 2026-09-05. Máquina: RTX 4080 (adaptador 0), iGPU Radeon RDNA2 (não usada), Windows 11, ReShade 6.8.0.2155, g++ 16.2 (msys64). Nenhuma alteração fora de `amd\feeder-amd\` e do scratchpad.

Convenção: **[FATO]** = medido/observado nesta máquina (log ou binário citado); **[INFERÊNCIA]** = dedução, ainda não confirmada em hardware AMD.

---

## 0. Resumo

- **[FATO]** `dlss5-feed-amd.addon64` compila com g++ 16.2 a partir do fonte do Feeder (HEAD `7927553`, `FEED_VERSION 0.12.0`) mais ~1.000 linhas novas, carrega no ReShade 6.8 (API 20) e roda no app da cópia de teste nos quatro modos abaixo, sem crash, por 45 s cada.
- **[FATO]** Modo portador FSR forçado numa RTX (`amd_carrier=1`): a sessão abre **sem NGX**, carrega `amd_fidelityfx_dx12.dll` 1.1.4, cria o contexto FSR 3.1 1:1 (NativeAA) no device D3D12 privado do Feeder, despacha a cada frame e o copy-back continua mostrando imagem (foto `logs\D-rgba16f.png`, 95 % de pixels não pretos, conteúdo do charmap legível).
- **[FATO]** Fora do modo AMD (`amd_carrier=-1` numa RTX), a variante se comporta como o Feeder oficial: NGX inicializa via forwarder para o `_nvngx.dll` do driver, o RenoDX intercepta o CreateFeature e o log mostra `inline feature 18 evaluation succeeded (count=60)`, ou seja, **neural rendering real através do binário MinGW**.
- **[FATO]** Swapchain-isca (`amd_decoy_swapchain=1`): sessão D3D12 aberta no `init_effect_runtime` (antes do primeiro Present do app), swapchain D3D12 32x32 criado na fila do Feeder, apresentado uma vez ali e depois ~30 Hz numa thread; o ReShade viu o `CreateSwapChainForHwnd` mas **não criou um effect runtime** para ele.
- Dois problemas de ABI MinGW↔MSVC apareceram e foram resolvidos no build (seção 3). Sem eles a variante crashava no primeiro `find_technique`.
- O que só o hardware AMD responde continua pendente (seção 7): se o mod DLSS-NR-on-AMD adota o device do Feeder, se aceita o formato 87, se os proxies do ReShade atrapalham o `hipImportExternalMemory`.

---

## 1. Onde está o quê

```
C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\amd\feeder-amd\
  build-feed-amd.sh              script de build (g++ + gcc para o MinHook + windres)
  version-amd.rc                 VERSIONINFO 0.12.0.1, OriginalFilename dlss5-feed-amd.addon64
  src-amd\feed_amd.h             portador FSR + swapchain-isca (467 linhas)
  src-amd\feed_seh_shim.h        __try/__except -> VEH + longjmp (g++ nao tem SEH)
  src-amd\ngx_forward.cpp        substitui a nvsdk_ngx_d.lib: repassa ao _nvngx.dll do driver ou devolve FeatureNotSupported
  src-feeder\                    clone do github.com/jlrouzies-fr/DLSS5-Feeder (MIT) com as ramificacoes FEED_AMD_VARIANT
  src-feeder\external\ngx\*.h    headers publicos do NGX (repositorio NVIDIA/DLSS), copiados do mingw-trial
  src-feeder\external\vulkan\    Vulkan-Headers (Khronos), idem
  src-feeder\external\ffx\       ffx_api 1.1.4 (ffx_api.h, ffx_api_types.h, ffx_upscale.h, dx12\ffx_api_dx12.h) + LICENSE (MIT)
  src-feeder\external\reshade\include\  headers do ReShade DO REPOSITORIO, com o shim de ABI (secao 3.1)
  patches-feeder-amd.diff        git diff do que mudou no clone (714 linhas)
  ffx-bin\amd_fidelityfx_dx12.dll  FidelityFX SDK v1.1.4, baixada do GitHub da AMD (sha256 12a50812...93a6, 6.675.664 bytes)
  build\dlss5-feed-amd.addon64   o binario (961.584 bytes, sha256 ad810e16...2ff1)
  dist\                          PRONTO PARA O bin-amd: dlss5-feed.addon64 (ja renomeado), amd_fidelityfx_dx12.dll, dlss5-feed.cfg, licencas
  tests\seh_shim_test.cpp        teste do shim SEH (passa: AV, UD2, C++ throw, 1000x)
  tests\abi_ret_test.cpp         prova de como o g++ passa o ponteiro de retorno oculto (secao 3.1)
scratchpad\feeder-amd\
  RELATORIO-FEEDER-AMD.md        este arquivo
  rodar-teste.ps1, foto.ps1      harness dos testes (roda FeedAmdTeste.exe 45 s, fotografa aos 30 s, mata, copia logs)
  logs\<teste>-dlss5-feed.log, -ReShade.log, -dlss5forall.log, <teste>.png
scratchpad\teste-feeder-amd\     copia de DLSS5ForAll-AMD usada nos testes (bin\ com o addon novo, a DLL FSR e o exe copiado como FeedAmdTeste.exe)
```

O binário de referência da pasta original (`dlss5-feed.addon64` 0.7.0, 164 KB, MSVC) foi guardado na cópia de teste como `dlss5-feed.addon64.orig-0.7.0`. **[FATO]** A variante é baseada no upstream 0.12.0, não no 0.7.0 que o app distribui hoje; entre eles mudaram nomes de log (por exemplo `jitter +0.000,+0.000` no `frame N delivered`) e o `DESCRIPTION` do add-on. O `main.cpp` do app lê `dlss5-feed.log`; se ele casar linhas por texto exato, conferir (não verifiquei).

---

## 2. Build (entrega 1)

`bash build-feed-amd.sh` → `build\dlss5-feed-amd.addon64`. **[FATO]** Compila limpo (só os dois avisos `'NAME' initialized and declared 'extern'` que o próprio upstream tem e os `novtable` silenciados com `-Wno-attributes`). Flags: `-std=c++20 -O2 -shared -DFEED_AMD_VARIANT -static -static-libgcc -static-libstdc++`, libs `d3d11 d3d12 dxgi dxguid version ole32 advapi32 user32 gdi32`. Imports do binário: `ADVAPI32 dxgi KERNEL32 msvcrt USER32 VERSION` (nenhuma DLL do MinGW). Exports: `NAME`, `DESCRIPTION`. ReShade registra: `Registered add-on "DLSS 5 Feed 0.12.0" v0.12.0.1 using ReShade API version 20`.

O que mudou no fonte (tudo atrás de `#ifdef FEED_AMD_VARIANT`, salvo indicado):

| Item | Arquivo:linha | O que |
|---|---|---|
| SEH | `dlss5-feed.cpp:61` inclui `feed_seh_shim.h`; wrappers em `:1649` (SafeNgxInit12), `:1680` (CreateDLSSGuarded), `:1740` (EvaluateDLSSGuarded), `:1766` (CloseListGuarded), `:2112` (SafeReleaseFeature) | os cinco `__try/__except` viram `FEED_GUARD_ARM(guard)`; o original fica no `#else` |
| casts | `feed_vk_hook.h:260` e `:290` | `reinterpret_cast<void *>(GetProcAddress(...))` (incondicional; vale para MSVC também) |
| cfg | `Cfg` `:872-875`; parse `:974-977`; validação e `rebuild` logo abaixo; `CfgWriteDefault` e `CfgSave` gravam as 4 chaves | `amd_carrier`, `amd_color_fmt`, `amd_srgb`, `amd_decoy_swapchain` |
| include | `:1615` | `feed_amd.h` entra depois de `DrainGpu()` (precisa de `g`, `g_cfg`, helpers de formato) |
| A | `InitSession` `:2797` | `if (AmdCarrierWanted(vendor_id)) AmdCarrierStart() else { bloco NGX original }`; o forwarder loga onde achou o `_nvngx.dll` |
| B | `CreateDlssFeature` `:2661` | `if (g_amd.active) return AmdCreateContext(...)` |
| C | `FeedFrame11` `:5363` | `AmdDispatch(reset)` no lugar do bloco `NVSDK_NGX_D3D12_DLSS_Eval_Params` + `SafeEvaluateDLSS`; barreiras, `EndCommands`, wait e blit iguais |
| D | `ReleaseFrameResources` `:2208` | `AmdDestroyContext()` (rebuild por resolução destrói e recria o contexto) |
| E | `ShutdownSession` `:2902` | `AmdDecoyDestroy()` antes de soltar a fila; `g_amd.active=false`. As chamadas NGX já eram guardadas por `g.params`/`ngx_inited` nulos |
| F | `BuildResources` `:2497` e `:2547`; `CopyOrResampleInputs` `:3794`; SRV do staging usa `TypedColorFormat(bb_fmt)` | `amd_color_fmt` diferente do backbuffer força o caminho de reamostragem (staging + `resample_ps`) já existente |
| G | `OnInitEffectRuntime` `:5656` (runtime da isca é ignorado) e `:5699` (`AmdDecoyOnRuntimeInit`); `OnDestroyEffectRuntime` e `OnRenderTechnique` `:5749` ignoram o runtime da isca | swapchain-isca |
| H | `WarmupRebuildDue` `:1716`; `OnCreateFeatureFailed` `:2434` | sem warm-up nem `ReinitNgx` no modo portador |
| I | `DrawOverlay` `:5842` | linha "AMD carrier: ..." no painel do ReShade (não exercitada, ver 7) |
| — | `DllMain` `:6034` | `AmdLogVariant()`: loga as chaves e as bases dos módulos (para transformar endereço de crash em RVA) |
| — | `TrackRuntime`/`ResolveHandles`/`OnInitEffectRuntime` | `Breadcrumb(...)` extras (incondicionais, inofensivos) que localizaram o crash da seção 3.1 |

### 2.1 Stub NGX virou forwarder

**[FATO]** `ngx_forward.cpp` define as 9 funções `NVSDK_NGX_D3D12_*` que o Feeder referencia mais `NVSDK_NGX_D3D12_EvaluateFeature_C` (o helper `NGX_D3D12_EVALUATE_DLSS_EXT` chama a variante `_C`) e os 16 `NVSDK_NGX_Parameter_Set*/Get*` (os headers NGX 310 usam essas funções C em vez de `params->Set`). Em vez de devolver sempre falha, ele procura o `_nvngx.dll` (já carregado → `HKLM\SOFTWARE\NVIDIA Corporation\Global\NGXCore\FullPath` → caminho de busca) e repassa 1:1 pelos exports de mesmo nome (`Init` → `NVSDK_NGX_D3D12_Init_Ext`, `Init_with_ProjectID` → `NVSDK_NGX_D3D12_Init_ProjectID`, os demais iguais). Sem driver NVIDIA tudo devolve `NVSDK_NGX_Result_FAIL_FeatureNotSupported` (0xBAD00001), que é o mesmo que o NGX real devolveria numa Radeon. Resultado: um só binário serve NVIDIA e AMD.

### 2.2 SEH sem SEH

**[FATO]** `feed_seh_shim.h`: Vectored Exception Handler (first) + `_setjmp(buf, NULL)`/`longjmp` (sem unwind). Só intercepta códigos de falha de hardware (AV, in-page, instrução ilegal, divisão por zero, etc.); exceções C++ (0xE06D7363), nomes de thread e `RaiseException` genéricos seguem o caminho normal. A guarda é `thread_local` e se desarma no destrutor (cobre saída normal, longjmp e exceção C++ atravessando). `tests\seh_shim_test.cpp` passa (AV e UD2 capturados com o código certo, throw C++ chega ao catch, 1000 AVs seguidas). **[INFERÊNCIA]** Diferença prática em relação ao MSVC: uma exceção C++ lançada dentro do NGX e não tratada lá derrubaria o processo em vez de virar "disable"; stack overflow não é capturado.

---

## 3. Os dois problemas de ABI MinGW ↔ MSVC (e por que o add-on da ponte não os tinha)

### 3.1 Retorno de struct por valor em função-membro virtual

**[FATO]** Primeira execução: `### CRASH RECORDED ### exception 0xC0000005 at ... dxgi.dll; last doing: runtime init: find_technique` (`logs\` da rodada A2, RVA 0xC48D2 no dxgi.dll do ReShade). Causa: o MSVC devolve **qualquer** struct de uma função-membro por ponteiro oculto em **RDX** (this em RCX); o g++ devolve structs de 8 bytes em RAX e, quando usa ponteiro oculto, põe-no em **RCX, antes do this** (`tests\abi_ret_test.s`: `Impl::wrapped` grava em `[rcx]`). O add-on da ponte deste projeto já tinha documentado o sintoma (`addon\dlss5_bridge_addon.cpp:53-80`) e contornou usando `enumerate_techniques`; o Feeder chama `find_technique`, `find_texture_variable` e `get_resource_from_view` em vários lugares, então contornar no chamador não servia.

Solução: nos headers do ReShade **da cópia do repositório** (`src-feeder\external\reshade\include`), sob `#if defined(__GNUC__) && !defined(__clang__)`, os 8 métodos virtuais que devolvem struct por valor (`effect_runtime::get_back_buffer/find_uniform_variable/find_texture_variable/find_technique`, `device::get_resource_desc/get_resource_from_view/get_resource_view_desc`, `swapchain::get_back_buffer`) são declarados como `virtual void X_msvc_slot_()` (ocupa o slot) e ganham um wrapper inline `X(...)` que chama o slot com a convenção do MSVC (`this, sret, args...`), obtendo o índice do slot pelo ponteiro-para-membro (ABI Itanium: `ptr = 1 + offset`). Ver `reshade_api_resource.hpp` (helper `mingw_abi`), `reshade_api.hpp:62,193,448,583`, `reshade_api_device.hpp:375,408,423,1380`. Parâmetros struct de 8 bytes continuam em registrador nas duas ABIs, por isso os demais métodos ficam intactos. **[FATO]** Depois disso as rodadas A3/A4/B/C/D/E não crasharam mais.

### 3.2 Ordem das sobrecargas virtuais em `NVSDK_NGX_Parameter`

**[FATO — regra do MSVC, ver `MicrosoftVTableContext` do Clang]** O MSVC agrupa sobrecargas de mesmo nome na vtable e as emite em **ordem inversa** de declaração; o g++ usa a ordem de declaração. O objeto `NVSDK_NGX_Parameter` (8 `Set` + 8 `Get` + `Reset`) vem do `_nvngx.dll` (MSVC). `external\ngx\nvsdk_ngx_params.h` foi patchado para, sob g++, declarar a classe já na ordem da vtable do MSVC (`Set(void*)` primeiro ... `Set(ULL)` por último; idem `Get`). **[FATO]** Com isso `SuperSampling.Available=1`, `CreateFeature` e `EvaluateFeature` funcionam e o RenoDX registra `inline feature 18 evaluation succeeded` (teste A4/E). Este item só importa no caminho NGX (NVIDIA); o portador FSR não usa `NVSDK_NGX_Parameter`. **[INFERÊNCIA]** A ordem foi deduzida da regra, não por desmontagem do `_nvngx.dll`; a evidência de que está certa é o DLSS real ter funcionado ponta a ponta (com a ordem errada, `SetD3d12Resource` cairia em `Set(float)` e o evaluate falharia).

### 3.3 O que NÃO precisou de shim

**[FATO]** ImGui: o `DrawOverlay` do Feeder usa só funções que devolvem `bool/float/void` (`Checkbox`, `Combo`, `SliderFloat`, `GetFontSize`, `Text*`...); nenhuma `ImVec2` por valor. A tabela de funções é de ponteiros livres, não vtable. Não exercitei o overlay (seção 7).

---

## 4. Modo portador FSR (entrega 2) — o que faz

`feed_amd.h` (`src-amd\`), funções `AmdCarrierStart :132`, `AmdLoadFfx :95`, `AmdCreateContext :187`, `AmdDispatch :258`, `AmdDestroyContext :174`, `AmdApplyColorFormat :160`.

- **Decisão**: `amd_carrier` -1 → `VendorId == 0x1002`; 1 força; 0 nunca. Só na abertura da sessão (mudar exige reiniciar o app).
- **Carga da DLL**: `LoadLibraryW(<pasta do add-on>\amd_fidelityfx_dx12.dll)` por caminho completo, na `InitSession` (isto é, quando o primeiro frame do efeito chega ou, com a isca, já no init do runtime); nunca é descarregada (o mod pode ter detour dentro dela). Resolve `ffxCreateContext/ffxDestroyContext/ffxDispatch/ffxQuery/ffxConfigure`.
- **Contexto** (`ffxCreateContextDescUpscale` + `ffxCreateBackendDX12Desc` com `g.dev12`): `maxRenderSize = maxUpscaleSize = tamanho de trabalho` (NativeAA 1:1), flags `AUTO_EXPOSURE | (DEPTH_INVERTED se depth invertido) | (HIGH_DYNAMIC_RANGE se g.hdr) | (NON_LINEAR_COLORSPACE se SDR, controlável por amd_srgb)`; a chave `flags` do cfg, se ≥ 0, substitui tudo (agora com o significado do FSR). `fpMessage` loga `[amd] ffx error/warning: ...`. **[FATO]** 385 ms na primeira criação (compila pipelines), 22-29 ms nas seguintes na mesma sessão.
- **Dispatch por frame** (`ffxDispatchDescUpscale`): `commandList = g.list`, cor/depth/MV em `COMPUTE_READ`, saída em `UNORDERED_ACCESS`, `reactive = DLSS5_Mask` quando presente (mesma semântica do bias mask do DLSS), jitter 0, `motionVectorScale = (mv_scale_x, mv_scale_y)`, `frameTimeDelta` medido por QPC (1..200 ms), `reset` igual ao do NGX, near 0,1 / far 1000 / fov 1 rad, flag `NON_LINEAR_COLOR_SRGB` em SDR. As barreiras antes/depois e o copy-back são os do Feeder.
- **Log** (`dlss5-feed.log`): `[amd] carrier: amd_carrier=1 vendor=0x10DE -> FSR carrier (no NGX on this session)`, `[amd] carrier: amd_fidelityfx_dx12.dll loaded: ...`, `[amd] carrier: context ok 554x489 -> 554x489 (NativeAA 1:1) flags=0x120 (SDR AutoExposure NonLinearSRGB) in 385.3 ms; ...`, `[amd] carrier: dispatch #1 ok (...)` para os primeiros `log_frames` e a cada 1800, `[amd] carrier: erro: ffxCreateContext -> N (...)` / `[amd] carrier: erro: ffxDispatch #n -> N` em falha, `[amd] carrier: context destroyed (N dispatches)`.
- **amd_color_fmt** (F): 0 mantém o do backbuffer; 87/28/10 trocam `g.color_fmt` e `g.output_fmt` do portador. Quando difere do backbuffer, a cor deixa de ir por `CopyResource` e passa pelo blit de reamostragem 1:1 (staging + `resample_ps`), e a volta continua pelo `BlitOutputToBackbuffer` (que já reamostra por shader). `g.hdr` continua decidido pelo formato do **backbuffer**, então um portador RGBA16F com conteúdo SDR fica SDR para o FSR. **[FATO]** Teste D: `Color/Output 554x489 R16G16B16A16_FLOAT`, `work-resolution source: 554x489 staging copy -> 554x489`, imagem correta (foto).

---

## 5. Swapchain-isca (entrega 3) — o que faz

`feed_amd.h` `AmdDecoyOnRuntimeInit :423`, `AmdDecoyCreate :342`, `AmdDecoyThread :331`, `AmdDecoyDestroy :399`.

- Só com `amd_decoy_swapchain=1`, só no runtime D3D11 do jogo e só se `AmdCarrierWanted(vendor)` (numa RTX com `amd_carrier=-1` loga `[amd] decoy: carrier not wanted on vendor 0x10DE; no decoy` e não faz nada — teste E).
- No fim de `OnInitEffectRuntime` (que o ReShade dispara de dentro do `CreateSwapChain` do jogo, portanto **antes do primeiro Present do app**): abre a sessão D3D12 já ali (`InitSession(dev11, immediate context)`; o carrier decide/carrega a DLL), cria janela oculta 1x1 (`WS_POPUP`, classe `DLSS5FeedAmdDecoy`) e `IDXGIFactory2::CreateSwapChainForHwnd(g.queue, hwnd, 32x32 B8G8R8A8 FLIP_DISCARD 2 buffers)`, faz `Present(0,0)` imediatamente e sobe uma thread que apresenta a cada 33 ms até `ShutdownSession`.
- **[FATO]** Teste C: `[amd] decoy: opening the D3D12 session early ...` às 02:55:38.881, `session open` .947, `[amd] decoy: D3D12 swapchain ... on queue ...; first Present -> 0x00000000; ReShade runtime on it: 0000000000000000; presenter thread started (~30 Hz)` .955; o primeiro frame do efeito só veio às :40.641. O ReShade logou o `CreateSwapChainForHwnd` da isca (`Redirecting IDXGIFactory2::CreateSwapChainForHwnd(... pDevice = <g.queue> ...)`) e **não** criou um segundo effect runtime, então a proteção contra "o ReShade adota o runtime da isca" (`decoy_creating`/`decoy_rt`) ficou sem uso, mas continua no código.
- **[FATO]** O RenoDX instalou seu "native D3D12 queue submission tracker" na nossa fila logo antes (mesmo log). Não atrapalhou.
- **[INFERÊNCIA]** É isto que deve fazer o mod adotar "o device do primeiro swapchain apresentado" = device do Feeder. Se o mod só olhar swapchains criados **antes** dos hooks dele estarem armados, ou se ele identificar o device pelo objeto **real** e o Feeder entregar proxies do ReShade (ver 7.3), a isca não basta.
- `AmdDecoyDestroy` para a thread (3 s de tolerância), solta o swapchain e destrói a janela se estiver na thread criadora (senão `WM_CLOSE`); loga `[amd] decoy: released after N presents`.

---

## 6. Validação na RTX 4080 (entrega 4) — o que rodou

Todos com `FeedAmdTeste.exe --outra-instancia --source window --window charmap` (cópia do `DLSS5ForAll.exe` da pasta de teste com outro nome: uma instância anterior morreu com código -1 sem log aos 10 s, o que casa com um `Stop-Process DLSS5ForAll` de outro teste rodando nesta sessão; com o nome próprio não se repetiu). Fonte = janela "Mapa de Caracteres" 554x489 (o Bloco de Notas não aparecia na lista de janelas do app). 45 s cada, morto pelo script, logs em `scratchpad\feeder-amd\logs\`.

| Teste | cfg | Resultado [FATO] |
|---|---|---|
| A4 `A4-ngx` | `amd_carrier=-1` (RTX → NGX), decoy 0 | `ngx forwarder: _nvngx.dll loaded via NGXCore\FullPath`, `NVSDK_NGX_D3D12_Init -> 0x1 (Success)`, `SuperSampling.Available=1`, `feature ready: 554x489 DLAA, flags=66`, frames delivered; ReShade.log: RenoDX `NGX feature create intercepted`, `feature 18 created via the signed snippet`, `inline feature 18 evaluation succeeded (count=60)` |
| B `B-carrier` | `amd_carrier=1`, fmt 87, decoy 0 | `FSR carrier (no NGX on this session)`, DLL carregada, `context ok ... flags=0x120 ... in 385.3 ms`, `dispatch #1..3 ok`, `frame 1..3 delivered`, nenhuma falha em 45 s; RenoDX ficou inerte (nenhuma linha depois do build) |
| C `C-decoy` | `amd_carrier=1`, fmt 87, decoy 1 | sessão aberta no init do runtime, isca criada e apresentada antes do primeiro frame do efeito, contexto e dispatches ok |
| D `D-rgba16f` | `amd_carrier=1`, fmt 10, decoy 0 | buffers RGBA16F, caminho de staging, contexto ok em 28,6 ms, dispatches ok, **foto `D-rgba16f.png` com a imagem do charmap correta** |
| E `E-ngx-auto-decoy` | `amd_carrier=-1`, decoy 1 | `decoy: carrier not wanted on vendor 0x10DE; no decoy`; caminho NGX igual ao A4; foto `E-ngx-auto-decoy.png` com imagem |

Observações dos logs:
- **[FATO]** Em B/C/D o app logou `aviso: neural nao esta ativo (não subiu — confira "DLSS 5 Feed" no ReShade ...)`: o `main.cpp` decide NR pelas linhas do RenoDX no `ReShade.log`, que não existem no modo portador. É o ramo AMD do estado neural previsto no plano (seção 6.4), fora deste escopo.
- **[FATO]** `STALL frame 0: interval ~110 ms` e `present travou` são do app capturando uma janela pequena a ~9 fps (a rodada de referência 0.7.0 das 01:59 tinha 7,6 fps); nada a ver com a variante.
- **[FATO]** O RenoDX loga `vtable::Hook(Failed to find NVSDK_NGX_D3D12_EvaluateFeature_C)` para os dois módulos NGX; é ele procurando um export que o `_nvngx.dll` não tem, aparece igual com o Feeder oficial.

---

## 7. Pendências e riscos

1. **Hardware AMD**: nada do lado do mod foi exercitado (não há Radeon suportada aqui). A lista 3.5 do plano continua valendo inteira; primeiro `amd_decoy_swapchain=0`, depois 1.
2. **Proxies do ReShade** [INFERÊNCIA]: `g.dev12`, `g.queue` e `g.list` são os wrappers do ReShade (ele hooka `D3D12CreateDevice`). O FSR funcionou através deles (métodos são repassados), mas o mod pode comparar o device "adotado" (real, obtido do swapchain/fila via hooks de vtable) com `commandList->GetDevice()` (proxy) e descartar o dispatch (`ignoring a swapchain that is not on our device`). Se acontecer, o próximo passo é obter os objetos nativos (`rt->get_device()->get_native()` só existe para devices com runtime; com a isca ativa o ReShade **não** criou runtime, então seria preciso outro caminho, por exemplo `QueryInterface` privado do ReShade ou criar a fila fora do proxy).
3. **Formato 87** [INFERÊNCIA]: continua sem prova de que o mod aceita `B8G8R8A8_UNORM`; `amd_color_fmt=10` está pronto e testado como plano B.
4. **Overlay do ReShade** (página do add-on): não aberto durante os testes; os ImGui usados não têm `ImVec2` por valor, então [INFERÊNCIA] deve funcionar.
5. **`ffxQuery`/`ffxConfigure`** resolvidos mas não usados; `FFX_UPSCALE_ENABLE_DEBUG_CHECKING` não ligado (pode ser ligado via `flags=` no cfg para depurar em AMD: `flags=288` = 0x120, somar 128 para debug).
6. **Caminhos D3D12/Vulkan/OpenGL do Feeder** (`FeedFrame12/Vk/Gl`): não ramificados para o portador; em modo AMD continuam tentando NGX e falham limpo (`FeatureNotSupported`). O app é D3D11, então não afeta este projeto.
7. **`amd_carrier` e `amd_decoy_swapchain` só valem na próxima sessão** (reiniciar o app); `amd_color_fmt`/`amd_srgb` disparam rebuild ao vivo (60 frames).
8. **Versão do upstream**: a variante é 0.12.0; o app distribui 0.7.0 MSVC. Se o `main.cpp` parseia texto do `dlss5-feed.log`, revisar as strings.
9. O `NgxForward` de `Init` chama `NVSDK_NGX_D3D12_Init_Ext(id, path, dev, versão, info)` primeiro e cai para o export `Init` se falhar; a ordem dos dois últimos parâmetros do export `Init` foi assumida igual à do SDK (só o `_Ext` foi exercitado, e funcionou).

---

## 8. Instruções de uso (entrega 5) — o que vai no `bin-amd`

Copiar de `amd\feeder-amd\dist\` (nomes exatos):

| Arquivo em `bin-amd\` | Origem | Observação |
|---|---|---|
| `dlss5-feed.addon64` | `dist\dlss5-feed.addon64` (= `build\dlss5-feed-amd.addon64` renomeado) | substitui o 0.7.0 MSVC; um só binário para NVIDIA e AMD |
| `amd_fidelityfx_dx12.dll` | `dist\amd_fidelityfx_dx12.dll` | FidelityFX SDK 1.1.4, MIT, assinada pela AMD; **tem de ficar na mesma pasta do `.addon64`** |
| `dlss5-feed.cfg` | `dist\dlss5-feed.cfg` | os 14 valores atuais do app + as 4 chaves novas (abaixo) |
| `LICENSE-FidelityFX-SDK-1.1.4.txt`, `LICENSE-DLSS5-Feeder.txt`, `LICENSE-MinHook.txt` | `dist\` | avisos de copyright (MIT/BSD-2) |

Não vai: `renodx-dlss5.addon64` no perfil AMD (inerte sem NGX, e o `ReShade.log` dele confunde o app — decisão do plano 3.4), `nvngx_dlss.dll` não é necessário ao portador (o mod precisa do `nvngx_dlssnr.dll` ao lado do `version.dll` dele).

Chaves novas do `dlss5-feed.cfg` (o parser só aceita números):

| Chave | Valores | Padrão | Efeito |
|---|---|---|---|
| `amd_carrier` | -1 / 0 / 1 | -1 | -1 = portador FSR só se o adaptador do device D3D11 for AMD (0x1002); 0 = nunca; 1 = força (é como testei na RTX). Vale na abertura da sessão |
| `amd_color_fmt` | 0 / 87 / 28 / 10 | 87 | formato DXGI do buffer de cor **e** da saída do portador; 0 = o do backbuffer; 87 = BGRA8 (igual ao backbuffer do app, sem conversão); 10 = RGBA16F (único formato comprovado nos logs do mod); 28 = RGBA8 |
| `amd_srgb` | -1 / 0 / 1 | -1 | flags `NON_LINEAR_COLORSPACE` (create) + `NON_LINEAR_COLOR_SRGB` (dispatch) do FSR; -1 = ligado quando SDR |
| `amd_decoy_swapchain` | 0 / 1 | 0 | 1 = swapchain D3D12 isca apresentado antes do primeiro Present do jogo (seção 5). Vale na abertura da sessão |

As chaves antigas continuam: `flags=-1` (com ≥ 0, no portador vira as flags de criação do FSR), `mv_scale_x/y` (inverter o sinal se a imagem "rastejar" ao contrário em AMD), `mode=1` (transporte sem FSR/NGX, diagnóstico), `preset` (ignorado no portador).

Onde ler o estado em `dlss5-feed.log`: `[amd] variant: ...` (chaves em vigor), `[amd] carrier: amd_carrier=... -> FSR carrier|NGX path`, `[amd] carrier: context ok ...`, `[amd] carrier: dispatch #n ok`, `[feed] frame n delivered`; erros começam com `[amd] carrier: erro:`; isca em `[amd] decoy: ...`. No caminho NVIDIA: `[amd] ngx forwarder: _nvngx.dll loaded via ...` seguido do `NVSDK_NGX_D3D12_Init -> 0x00000001` de sempre.

Para recompilar: `bash "C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\amd\feeder-amd\build-feed-amd.sh"` (msys64 g++ 16.2; ~40 s). Para reproduzir os testes: `powershell -File scratchpad\feeder-amd\rodar-teste.ps1 -Segundos 45 -Rotulo X` com o cfg desejado em `scratchpad\teste-feeder-amd\bin\dlss5-feed.cfg` (o exe de teste é `FeedAmdTeste.exe`; abrir um `charmap` antes ou deixar o script abrir).
