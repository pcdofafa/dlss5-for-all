# PLANO-AMD — DLSS 5 Neural Rendering em Radeon via o mod DLSS-NR-on-AMD

Data da pesquisa: 2026-09-05. Somente leitura do projeto; nada foi executado, instalado ou aplicado.

Convenção de marcação usada neste documento:

- **[FATO]** verificado em código, header, log, release ou issue (fonte citada).
- **[INFER]** inferência a partir de strings do binário, de comportamento documentado ou de leitura do código; precisa de confirmação.
- **[TESTE]** só o hardware responde.

Arquivos de apoio nesta pasta (`scratchpad\pesquisa-amd\`): `setup-strings.txt` (strings do instalador do mod), `ffx\` (headers ffx-api 1.1.4 e 2.3.0, doc do upscaler, licenças), `feeder-*.md` (README, DEPLOY-DEV, PLAN-PROXY-SWAPCHAIN do Feeder), `mingw-trial\` (teste de compilação do Feeder com g++, logs `trial-gpp2.log` e `trial-gpp3.log`).

---

## 0. Resumo executivo

A hipótese "portador FSR" é tecnicamente coerente e a API necessária existe, é pública e é MIT: o Feeder já tem cor, depth e MV em texturas D3D12 no formato e nos estados de recurso que o FSR 3.1 pede, e a saída já tem flag UAV. O que falta é uma ramificação de uns 300 a 500 linhas no `dlss5-feed.cpp` que, em GPU AMD, troque `NGX Init/Create/Evaluate` por `ffxCreateContext/ffxDispatch` 1:1 (NativeAA, jitter zero) numa `amd_fidelityfx_dx12.dll` carregada pelo próprio Feeder.

Três riscos decidem o projeto e só se resolvem em hardware:

1. **O mod adota o device D3D12 "do primeiro swapchain apresentado"** e tem a string literal `yes (this game renders with D3D11: unsupported)`. O swapchain do app é D3D11. Se o mod ficar inerte ao ver isso, é preciso um **swapchain-isca D3D12** (janela oculta 1x1 no device/fila do Feeder, apresentado antes do primeiro Present do app) para o mod adotar o device certo. Existe uma segunda via de descoberta (`device %p via queue`) que talvez baste.
2. **Formato de cor aceito pelo mod.** Ele rejeita formatos com `unsupported colour format %d` (issue 38: 67 = R9G9B9E5). Nosso B8G8R8A8_UNORM é 87; não há prova de que 87 seja aceito. Plano: tornar o formato do buffer de cor do portador configurável (R16G16B16A16_FLOAT = 10 é o formato comprovado nos logs de Cyberpunk).
3. **RX 9060 XT (gfx1200)** continua sem release oficial: v0.2.11 (2026-09-05) só traz gfx1100/1101/1102/1201. O único caminho hoje é o patch comunitário do ELF (issue 33), que não recomendo redistribuir; o autor prometeu publicar o fonte (comentário na issue 4, 2026-09-04).

Build: **MinGW é viável** para uma variante AMD do Feeder. Medido nesta máquina com o g++ 16.2: 32 erros no fonte original, dos quais 30 são as 5 construções `__try/__except` e 2 são casts `FARPROC → void*`; substituindo o SEH, sobram 2 erros triviais. A variante AMD não chama NGX, então não precisa da `nvsdk_ngx_d.lib` (basta um stub) nem de Build Tools.

---

## 1. FidelityFX SDK: versão, DLLs, símbolos, licença, assinatura da API

### 1.1 Qual DLL e qual família de símbolos o mod hooka

**[FATO]** Strings do instalador v0.2.11 (`setup-strings.txt`) e logs anexados nas issues 16 e 49 mostram os hooks feitos pelo mod:

| DLL alvo | Símbolos | Família |
|---|---|---|
| `amd_fidelityfx_dx12.dll` | `ffxCreateContext`, `ffxDispatch` | ffx-api (SDK 1.1.x, FSR 3.1.x) |
| `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll` | `ffxCreateContext`, `ffxDispatch` | ffx-api (SDK 2.x, FSR 3.1.5 + FSR 4) |
| `ffx_fsr3_x64.dll` | `ffxFsr3ContextCreate`, `ffxFsr3ContextDispatchUpscale` | FSR 3.0 (API antiga) |
| `ffx_fsr3upscaler_x64.dll` | `ffxFsr3UpscalerContextCreate`, `ffxFsr3UpscalerContextDispatch` | FSR 3.0 (API antiga) |

**[FATO]** O log de Cyberpunk (issue 16) registra `first ffxDispatch type 0x10001`; `0x00010001u` é `FFX_API_DISPATCH_DESC_TYPE_UPSCALE` (`ffx\ffx_upscale.h`). O log de GTA V Enhanced (issue 49), caso de sucesso completo, usou exatamente `amd_fidelityfx_dx12.dll!ffxCreateContext/ffxDispatch`. Portanto o caminho comprovado é a ffx-api com a DLL do SDK 1.1.x.

**[FATO]** O mod lê as flags do contexto: string `%s: flags 0x%x -> depth inverted %d, display-res MVs %d`, e a escala de MV do dispatch: `job %d motion: mean |mv| = (%.3f, %.3f) x scale (%.1f, %.1f)`.

**[INFER]** O mod só hooka a DLL FSR depois de `engine init ok` (ordem das linhas no log da issue 16), ou seja, ele procura os módulos já carregados naquele momento. Não há string de hook de `LoadLibrary`. Consequência para nós: carregar a `amd_fidelityfx_dx12.dll` cedo (no `OnInitEffectRuntime`, antes do primeiro Present) e nunca depois. [TESTE] se ele re-escaneia.

### 1.2 Onde baixar, tamanho, licença

| Release | Arquivo | Tamanho | Licença | Observação |
|---|---|---|---|---|
| **v1.1.4** (2025-05-08) | `PrebuiltSignedDLL/amd_fidelityfx_dx12.dll` | 6,68 MB | **MIT** (`sdk/LICENSE.txt`, `docs/license.md`) | FSR 3.1.4. Redistribuição livre com aviso de copyright. **Recomendada.** |
| v2.3.0 (2026-06-24) | `Kits/FidelityFX/signedbin/amd_fidelityfx_loader_dx12.dll` (26 KB) + `amd_fidelityfx_upscaler_dx12.dll` (28,8 MB) | 28,8 MB | **Binário, não MIT**: redistribuição em forma binária permitida com aviso; proibido engenharia reversa (`Kits/FidelityFX/docs/license.md`) | FSR 4.1.1 (ML, RDNA4) com fallback 3.1.5. Cabeçalhos ganham `ffxCreateContextDescUpscaleVersion` obrigatório. |

**[FATO]** Fonte: `gh api repos/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases` e árvores das tags `v1.1.4` e `v2.3.0`. Download direto: `https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/raw/v1.1.4/PrebuiltSignedDLL/amd_fidelityfx_dx12.dll`. A DLL é assinada pela AMD.

Por que 1.1.4 e não 2.x: (a) MIT; (b) 6,7 MB contra 28,8 MB; (c) é a DLL do único log de sucesso completo do mod (issue 49); (d) o NR do mod substitui o resultado do upscaler, então a qualidade do FSR em si é irrelevante (o mod usa a saída como base e as entradas como guias); (e) a API 1.1.4 não exige o struct de versão.

### 1.3 Assinaturas e structs (ffx-api 1.1.4, `ffx\ffx_api.h`, `ffx_upscale.h`, `ffx_api_dx12.h`)

```c
ffxReturnCode_t ffxCreateContext (ffxContext* ctx, ffxCreateContextDescHeader* desc, const ffxAllocationCallbacks* cb);
ffxReturnCode_t ffxDestroyContext(ffxContext* ctx, const ffxAllocationCallbacks* cb);
ffxReturnCode_t ffxDispatch      (ffxContext* ctx, const ffxDispatchDescHeader* desc);
ffxReturnCode_t ffxQuery         (ffxContext* ctx, ffxQueryDescHeader* desc);
ffxReturnCode_t ffxConfigure     (ffxContext* ctx, const ffxConfigureDescHeader* desc);
// retorno 0 = FFX_API_RETURN_OK; typedefs PfnFfxCreateContext etc. para GetProcAddress
```

Criação (DX12 backend encadeado por `pNext`):

```c
ffxCreateBackendDX12Desc be = {};                 // ffx_api_dx12.h:31
be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;   // 0x2
be.device      = g.dev12;

ffxCreateContextDescUpscale cd = {};              // ffx_upscale.h
cd.header.type    = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;    // 0x00010000
cd.header.pNext   = &be.header;
cd.flags          = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE              // 1<<5
                  | (inverted ? FFX_UPSCALE_ENABLE_DEPTH_INVERTED : 0)   // 1<<3
                  | (g.hdr    ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0); // 1<<0
cd.maxRenderSize  = { w, h };
cd.maxUpscaleSize = { w, h };                     // 1:1 = NativeAA
cd.fpMessage      = LogFfx;                       // void(uint32_t type, const wchar_t*)
```

Dispatch por frame:

```c
ffxDispatchDescUpscale dd = {};
dd.header.type      = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;         // 0x00010001
dd.commandList      = g.list;                                     // ID3D12GraphicsCommandList*
dd.color            = ffxApiGetResourceDX12(g.tex12[SLOT_COLOR],  FFX_API_RESOURCE_STATE_COMPUTE_READ);
dd.depth            = ffxApiGetResourceDX12(g.tex12[SLOT_DEPTH],  FFX_API_RESOURCE_STATE_COMPUTE_READ);
dd.motionVectors    = ffxApiGetResourceDX12(g.tex12[SLOT_MV],     FFX_API_RESOURCE_STATE_COMPUTE_READ);
dd.exposure         = {};   // opcional (AUTO_EXPOSURE ligado)
dd.reactive         = {};   // opcional
dd.transparencyAndComposition = {};
dd.output           = ffxApiGetResourceDX12(g.tex12[SLOT_OUTPUT], FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
dd.jitterOffset     = { 0.0f, 0.0f };
dd.motionVectorScale= { g_cfg.mv_scale_x, g_cfg.mv_scale_y };     // MVs já em pixels
dd.renderSize       = { w, h };
dd.upscaleSize      = { w, h };
dd.enableSharpening = false; dd.sharpness = 0.0f;
dd.frameTimeDelta   = dt_ms;                                      // QPC entre frames
dd.preExposure      = 1.0f;
dd.reset            = reset != 0;
dd.cameraNear = 0.1f; dd.cameraFar = 1000.0f; dd.cameraFovAngleVertical = 1.0f; dd.viewSpaceToMetersFactor = 1.0f;
dd.flags            = 0;    // FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB se a cor for sRGB perceptual
```

**[FATO]** `ffxApiGetResourceDX12` (`ffx_api_dx12.h:256`) preenche formato, dimensões e usage a partir de `GetDesc()`; marca `UAV` quando o recurso tem `D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS`. Formatos suportados pela tabela `ffxApiGetSurfaceFormatDX12` incluem `B8G8R8A8_UNORM`, `R8G8B8A8_UNORM`, `R16G16B16A16_FLOAT`, `R16G16_FLOAT`, `R32_FLOAT` (`ffx_api_dx12.h:122-253`).

**[FATO]** Modo 1:1: `FFX_UPSCALE_QUALITY_MODE_NATIVEAA = 0` ("ratio 1.0x"), e o README do mod diz "any quality / FSRAA".

**[FATO]** Convenções da documentação (`ffx\fsr-upscaler-doc.md` linhas 128-135, 137-146, "Exposure"): depth é 1 canal float (R32_FLOAT serve), MV é 2 canais float na faixa em pixels `[-w,-h]..[w,h]` com `motionVectorScale` para ajustar, MV **sem** jitter, entradas em resolução de render. Recomendam inverted + infinite depth, mas os outros modos são suportados via flags. Auto-exposure é a recomendação padrão.

**[INFER]** Sinal dos MVs: tanto DLSS quanto FSR reprojetam com `uv_anterior = uv_atual + mv` (o FSR documenta "motion vectors ... pointing to previous frame position"; o Feeder já entrega MVs em pixels ao DLSS com `InMVScale` = `mv_scale_x/y`, `dlss5-feed.cpp:5243-5244`). O mesmo buffer e a mesma escala devem servir. Se a imagem "rastejar" na direção errada, inverter o sinal via `mv_scale_x/y = -1` no `dlss5-feed.cfg` (já é chave existente, `dlss5-feed.cpp:955-956`). [TESTE]

**[FATO]** Estados de recurso: o Feeder já coloca COLOR/DEPTH/MV em `NON_PIXEL_SHADER_RESOURCE` e OUTPUT em `UNORDERED_ACCESS` antes do evaluate (`dlss5-feed.cpp:5218-5226`) e volta para `COMMON` depois (`5263-5268`). Isso é exatamente o par `COMPUTE_READ`/`UNORDERED_ACCESS` que a ffx-api espera; o backend DX12 restaura os estados informados ao fim do dispatch, então as barreiras de saída existentes continuam válidas.

---

## 2. No `dlss5-feed.cpp`: onde ramificar, o que já existe, o que muda

### 2.1 Recursos D3D12 que já existem no caminho D3D11 (o do app)

**[FATO]** `BuildResources` (`dlss5-feed.cpp:2381-2520`) cria, via `MakeSharedPair` (`2135-2202`), cinco texturas compartilhadas D3D12↔D3D11:

| Slot | Formato | Flags D3D12 | Linha |
|---|---|---|---|
| `SLOT_COLOR` | `g.color_fmt` (= B8G8R8A8_UNORM, do backbuffer) | SIMULTANEOUS_ACCESS + RENDER_TARGET | 2436 |
| `SLOT_OUTPUT` | `g.output_fmt` | SIMULTANEOUS_ACCESS + **ALLOW_UNORDERED_ACCESS** | 2437 |
| `SLOT_DEPTH` | R32_FLOAT | + RENDER_TARGET | 2438 |
| `SLOT_MV` | R16G16_FLOAT | + RENDER_TARGET | 2439 |
| `SLOT_MASK` | R8_UNORM | + RENDER_TARGET | 2440 |

**[FATO]** O log real desta máquina confirma (`dlss5-feed.log` linhas 17-23): `Output 1920x1080 B8G8R8A8_UNORM via D3D11->D3D12 (d3d12 flags=0x24 UAV)`. Flag 0x24 = ALLOW_UNORDERED_ACCESS | ALLOW_SIMULTANEOUS_ACCESS. O mod exige UAV na saída para o modo residual (`output has no UAV flag; residual apply unavailable, using frame replacement`), então estamos cobertos. A checagem existente em `2199-2200` já loga a ausência.

**[FATO]** Device e fila privados: `InitSession` (`2655-2778`) pega o adaptador do device D3D11 (`2662-2672`), carrega `d3d12.dll` (`2678`), cria o device nesse adaptador (`2683`), depois faz `NVSDK_NGX_D3D12_Init` (`2692-2701`), consulta capacidades (`2703-2716`; **falha aqui em AMD**: `SuperSampling.Available=0` ou Init retorna erro → `goto fail` → `FeedDisable`), aloca parâmetros (`2718`), cria fila DIRECT, 3 allocators e a lista (`2722-2733`), e o fence compartilhado D3D12→D3D11 (`2735-2745`).

**[FATO]** Fluxo por frame em `FeedFrame11` (`5060-5329`): cópia dos inputs D3D11 para as texturas compartilhadas em `CopyOrResampleInputs` (`3679-3682`); handoff por fence (`5210-5213`: `ctx4->Signal(fence11)`, `Flush`, `queue->Wait(fence12)`); `BeginCommands` (`5215`); barreiras (`5218-5226`); `NVSDK_NGX_D3D12_DLSS_Eval_Params` (`5229-5247`); `SafeEvaluateDLSS` (`5251`); barreiras de volta e `EndCommands` (`5263-5269`); `ctx4->Wait(fence11, v_out)` e `BlitOutputToBackbuffer` (`5280-5281`). O `mode=1` (`5202-5206`) é o "transporte só": `CopyResource(OUTPUT <- COLOR)` sem NGX, útil como diagnóstico em AMD.

### 2.2 Pontos de ramificação propostos

Toda a ramificação fica atrás de um novo campo de sessão `g.amd_fsr` (bool) decidido em `InitSession`, mais uma chave de cfg `amd_carrier` (-1 auto por VendorId 0x1002, 0 off, 1 forçar).

| # | Onde | O que fazer |
|---|---|---|
| A | `InitSession`, logo após criar `g.dev12` (`2683-2685`) e antes de `SafeNgxInit12` (`2692`) | Se `ad.VendorId == 0x1002` (ou `amd_carrier=1`): `LoadLibraryW(<pasta do add-on>\amd_fidelityfx_dx12.dll)`, resolver `ffxCreateContext/ffxDispatch/ffxDestroyContext/ffxQuery` por `GetProcAddress`, marcar `g.amd_fsr = true`, **pular** o bloco NGX inteiro (`2692-2719`) e seguir para fila/lista/fence (`2722`). Sem NGX: `g.params` fica nulo, então cada uso de `g.params` precisa de guarda (há usos em `PublishDfcInterop` `1592-1599` e `CreateDlssFeature` `2585-2589`). |
| B | `CreateDlssFeature` (`2559-2650`) | No início: `if (g.amd_fsr) return CreateFsrCarrier(w, h, inverted);`. A nova função cria o contexto (seção 1.3), guarda em `g.ffx_ctx`, seta `g.frame_ready = true; g.need_reset = true`. Sem `BeginCommands`: a ffx-api cria recursos internos fora de lista de comandos. |
| C | `FeedFrame11`, no bloco do evaluate (`5229-5251`) | `if (g.amd_fsr) { rc = DispatchFsrCarrier(reset); } else { ...NGX... }`. As barreiras (`5218-5226`, `5263-5268`), o `EndCommands`, o wait e o blit ficam iguais. `GuideProbeRecord` (`5223`) pode ficar: só faz cópias. |
| D | `ReleaseFrameResources` (`2123-2128`) | `if (g.ffx_ctx) ffxDestroyContext(&g.ffx_ctx, nullptr)` no lugar de `SafeReleaseFeature`. O FSR guarda histórico dimensionado no `maxUpscaleSize`, então rebuild por resolução exige destruir e recriar, como hoje. |
| E | `ShutdownSession` (`2783-2784`) | Pular `DestroyParameters`/`Shutdown1` quando `g.amd_fsr`; `FreeLibrary` opcional (melhor não: o mod pode ter detour dentro da DLL). |
| F | `BuildResources` (`2400-2405`, `2436`) | Formato do buffer de cor do portador configurável: nova chave `amd_color_fmt` (0 = manter `g.color_fmt`; 10 = R16G16B16A16_FLOAT; 28 = R8G8B8A8_UNORM). Quando diferente do backbuffer, `CopyOrResampleInputs` (`3679`) troca o `CopyResource` de cor por um blit com o `blit_ps` já existente (`MakeBlitShaders` `2204`) para a `input_rtv[SLOT_COLOR]` (já criada em `2497-2506`). O `SLOT_OUTPUT` deve seguir o mesmo formato e `BlitOutputToBackbuffer` (`3802`) já reamostra por shader, então a volta não muda. |
| G | `OnInitEffectRuntime` (`5504`) | Em AMD, antes do primeiro Present: (G1) `LoadLibrary` da DLL FSR; (G2) **swapchain-isca** opcional (seção 3.3): janela oculta 1x1, `IDXGIFactory2::CreateSwapChainForHwnd` sobre `g.queue` D3D12, um `Present(0,0)` imediato e depois um por frame no fim de `FeedFrame11`. Exige criar device e fila já aqui (hoje isso só acontece no primeiro frame em `5143`); é uma reordenação, não código novo. |
| H | `Cfg` (`810-869`) e `CfgReload` (`935-959`) | Chaves novas: `amd_carrier`, `amd_color_fmt`, `amd_decoy_swapchain`, `amd_fsr_dll` (nome da DLL, padrão `amd_fidelityfx_dx12.dll`). |
| I | `DrawOverlay` (`5665`) | Linha de status: "AMD carrier: FSR ctx ok / dispatch ok / mod log: ..." (opcional). |
| J | Os cinco wrappers SEH (`1606-1618`, `1625-1629`, `1674-1678`, `1692-1695`, `2030-2035`) | Só importa para o build MinGW (seção 4): na variante AMD viram funções que não chamam NGX (stub) ou usam `try/catch` inócuo. |

### 2.3 O que muda no copy-back

Nada estrutural. O mod, em modo `Inline=1`, escreve o resultado no recurso de saída do próprio dispatch **antes** de a fila sinalizar o fence (ver 3.2). O `ctx4->Wait(fence11, v_out)` + `BlitOutputToBackbuffer` (`5280-5281`) já espera o fence D3D12, então recebe a saída já processada. Se o mod aplicasse só no Present (modo async), nossa saída sairia sem NR; por isso `Inline=1` é obrigatório neste projeto.

### 2.4 Estimativa de código novo

| Peça | Linhas |
|---|---|
| Loader dinâmico da ffx-api + tipos mínimos (não precisa vendorizar headers inteiros; 4 structs + enums) | 80 a 120 |
| `CreateFsrCarrier` / `DispatchFsrCarrier` / destruição | 80 a 120 |
| Ramificações A a F, guardas de `g.params`, cfg e log | 60 a 100 |
| Swapchain-isca (janela oculta + swapchain + Present por frame + teardown) | 100 a 140 |
| Stub NGX para o link MinGW (11 funções, seção 4.3) | 30 a 50 |
| **Total** | **350 a 530** |

---

## 3. Coexistência no processo: ReShade dxgi.dll + version.dll do mod

### 3.1 Ordem de carga

**[FATO]** `DLSS5ForAll.exe` não referencia `version.dll` (0 ocorrências da string no binário). O `dxgi.dll` do ReShade referencia (1 ocorrência), assim como `dlss5-feed.addon64` (linka `version.lib`, `feeder-build.bat`) e `nvngx_dlssnr.dll`. `version.dll` não é KnownDLL, então uma cópia na pasta do exe é a que carrega.

**[INFER]** O proxy do mod entra no processo assim que o ReShade (`dxgi.dll`, import do exe) resolve seus imports, isto é, antes do `WinMain`. Nesse momento o mod instala os detours de `D3D12CreateDevice`, `CreateDXGIFactory1/2`, cria um device e swapchain "dummy" (`dlssnr_dummy`, `dummy device failed`, `dummy swapchain failed; no present hook`) para obter as vtables e hooka `Present`, `Present1`, `CreateSwapChain*` e `ExecuteCommandLists`. Como o ReShade também hooka `D3D12CreateDevice` e as factories, há dois detours inline encadeados na mesma função. Isso normalmente funciona (ambos usam trampolins), mas é o primeiro item de teste (3.5). A issue 43 relata que o proxy só funciona como `version.dll`; no nosso processo não há outro `version.dll`, então não há conflito de nome.

### 3.2 Como o mod escolhe device, fila e swapchain

**[FATO]** Strings: `device %p (from the first presented swapchain)`, `device %p via queue`, `present queue %p (first direct queue on our device)`, `env: d3d12 device %s; d3d11 device %s`, `yes (this game renders with D3D11: unsupported)`, `ignoring a swapchain that is not on our device (another renderer in the process, e.g. an overlay)`, `swapchain %p created on queue %p (device %p)`.

**[INFER]** Modelo de funcionamento do mod:

1. `CreateSwapChain*` hookado registra "swapchain X criado na fila Y (device Z)"; isso só existe para swapchains D3D12 (o argumento é a fila).
2. No primeiro `Present`, o mod tenta `GetDevice(ID3D12Device)` no swapchain; se der certo, esse vira "our device"; se for D3D11, loga `d3d11 device yes (this game renders with D3D11: unsupported)`. Não há evidência de que ele depois adote um device D3D12 que apareça por `ExecuteCommandLists` (`device %p via queue`), embora a string exista.
3. `ffxCreateContext` hookado lê flags; `ffxDispatch` hookado copia cor/MV/depth/exposure para buffers de staging **próprios** (criados compartilhados e importados no HIP via `hipImportExternalMemory`; `interop: inputs %s, output %s`), grava um shader que espera a rede e aplica o residual na saída (`inline: spin used %u iterations`, `GPU wait timeouts`, `inline mode needs zero-copy interop`), e no `ExecuteCommandLists` dispara o job HIP.
4. O `Present` também serve ao overlay (`overlayTick(IDXGISwapChain*)`, tecla End) e ao modo async.

**[FATO]** O Present do nosso swapchain D3D11 passa pelo hook do mod (a vtable de `IDXGISwapChain` é a mesma para D3D11 e D3D12; o ReShade embrulha o swapchain mas chama o Present real por dentro). O mod vai logar `ignoring a swapchain that is not on our device` ou o aviso de D3D11. Não quebra nada, só não serve para overlay nem para async.

**[FATO]** Seleção de GPU HIP (v0.2.10 "fixed GPU detection for systems with multiple GPUs"): strings `using HIP device %d (matches the game's D3D12 adapter)`, `no HIP device matches the game's adapter; best supported one`, `driver reports no adapter LUIDs; best supported one`, `env: WARNING: the game renders on an adapter HIP does not expose; staging cannot be shared with it and the pass may fall back to readback or fail`, chave `HipDevice=-1` no ini. Ou seja: casa o LUID do adaptador do device D3D12 adotado com os LUIDs dos devices HIP; sem casamento, pega "o melhor suportado" e perde o zero-copy.

**Consequência para RTX 4080 + RX 9060 XT [INFER]:** o HIP só enumera a Radeon. Se o device D3D12 do Feeder estiver na 4080 (hoje ele herda o adaptador do D3D11 do app, `dlss5-feed.cpp:2662-2683`, que é o adaptador 0 = 4080, `main.cpp:3216-3219`), o mod cai em "no HIP device matches" e o modo inline (que exige zero-copy) não sobe. Logo **o app inteiro precisa rodar na 9060 XT** (seção 6), ou ao menos o device D3D11 do app, para o Feeder herdar o adaptador certo. Compartilhar texturas D3D11 (4080) ↔ D3D12 (9060 XT) exigiria heaps CROSS_ADAPTER e reescrever `MakeSharedPair`; não recomendo.

### 3.3 O problema do "primeiro swapchain apresentado" e o swapchain-isca

**[INFER]** Como o app apresenta frames D3D11 desde o início e o device D3D12 do Feeder só nasce no primeiro frame de efeito, o primeiro Present que o mod vê é D3D11. Se o mod fixar "unsupported" nesse ponto, nenhum `ffxDispatch` será processado.

Mitigação proposta (item G da seção 2.2): no `OnInitEffectRuntime` (que roda antes do primeiro Present), em AMD, o Feeder cria device e fila D3D12 e um swapchain D3D12 de 1x1 numa janela oculta (`CreateWindowExW` sem `WS_VISIBLE`, `CreateSwapChainForHwnd` na `g.queue`, FLIP_DISCARD, 2 buffers) e chama `Present(0, 0)` uma vez ali mesmo e depois uma vez por frame ao fim de `FeedFrame11`. Efeitos esperados: o mod adota o device do Feeder ("from the first presented swapchain"), tem "present queue" para o overlay (tecla End desenharia na janela oculta; irrelevante) e o modo async passa a existir, embora inútil para nós. Custo: um Present vazio por frame. O `PLAN-PROXY-SWAPCHAIN.md` do Feeder trata de outra ideia (mentir o tamanho do backbuffer ao jogo) e não se aplica aqui.

Alternativa sem isca: confiar em `device %p via queue` (o primeiro `ExecuteCommandLists` numa fila D3D12 seria do Feeder). [TESTE] decide qual das duas é necessária; implementar a isca atrás de `amd_decoy_swapchain=1` e testar com 0 primeiro.

### 3.4 RenoDX e o resto da cadeia em AMD

**[FATO]** O RenoDX (`renodx-dlss5.addon64`) só faz sentido com NGX. Em AMD ele não verá `CreateFeature` nenhum e ficará inerte. **[INFER]** Ainda assim pode carregar `nvngx_dlssnr.dll`/`_nvngx.dll` e falhar com ruído no `ReShade.log`; e o `main.cpp` lê o `ReShade.log` procurando linhas do RenoDX para decidir `NR_ATIVO` (`main.cpp:3363-3466`, `NeuralPeloLog` `5502`). Recomendação: em modo AMD, não listar o RenoDX no `AddonPath`/pasta de add-ons e fazer o app ler o estado do `dlssnr_on_amd.log` (linhas `engine init ok`, `staging ready`, `network job %d done in %llu ms`, `FSR is not active`, `env: UNSUPPORTED GPU ARCHITECTURE`, `unsupported colour format`) e do `dlss5-feed.log`.

**[FATO]** O mod grava `dlssnr_on_amd.log` e lê `dlssnr_on_amd.ini` na pasta do exe; pesos em `dlssnr_on_amd_weights.bin` (gerados pelo setup a partir do `nvngx_dlssnr.dll`, recurso `WEIGHTS_HT`); precisa do `nvngx_dlssnr.dll` 310.8.0.0 ao lado (`Weights missing: nvngx_dlssnr.dll was not found next to version.dll`). O app já distribui esse DLL.

### 3.5 Riscos de coexistência a testar (ordem)

1. Processo abre com `version.dll` do mod + `dxgi.dll` do ReShade, sem crash, e `dlssnr_on_amd.log` mostra os cinco `hooked ...`.
2. Após o primeiro frame do Feeder: o log do mod mostra `device ... ` com o device do Feeder (por swapchain-isca ou via queue) e `env: using HIP device 0 (matches the game's D3D12 adapter)`.
3. `hooked amd_fidelityfx_dx12.dll!ffxCreateContext/ffxDispatch` aparece (a DLL foi vista a tempo).
4. `first ffxDispatch type 0x10001` e `staging ready: colour WxH dxgi 87 ...` sem `unsupported colour format`.
5. `interop: inputs shared (zero-copy), output shared (zero-copy); mode inline`.
6. `network job N done in X ms` e a imagem no app muda (comparar com `mode=1`).

---

## 4. Build do Feeder modificado

### 4.1 Opção (a): Visual Studio Build Tools via winget

**[FATO]** IDs: `Microsoft.VisualStudio.2022.BuildTools` (ou `Microsoft.VisualStudio.BuildTools`, canal estável atual, VS 18). Componentes mínimos para o `build.bat` oficial: `Microsoft.VisualStudio.Component.VC.Tools.x86.x64` (cl, link, lib) e um Windows SDK (`Microsoft.VisualStudio.Component.Windows11SDK.26100` ou `.22621`), que traz `rc.exe` e os headers/libs do Win32. O `tools\vcvars.bat` do Feeder localiza a instalação por `vswhere` e cai no caminho `...\Microsoft Visual Studio\18\BuildTools` (`feeder-vcvars.bat`). Comando:

```
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--passive --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100"
```

**[INFER]** Espaço: a Microsoft lista de 2,3 GB para cima para os Build Tools; relatos de usuários com compilador + SDK ficam entre 4 e 6 GB em disco. Além disso é preciso baixar o NGX SDK (`external/ngx`, não redistribuído; `feeder-ngx-README.md`), que a variante AMD não precisa.

### 4.2 Opção (b): clang-cl / clang

**[FATO]** `clang-cl` não é autônomo: usa headers e libs do MSVC e do Windows SDK, então exige a opção (a) de qualquer forma. O `clang` alvo mingw existe no MSYS2 como `mingw-w64-x86_64-clang` (22.1.8, 29 MB de download, 321 MB instalado; não está instalado) e aceita `__try/__except` com `-fms-extensions` no x86_64 (documentação do Clang: "Structured exceptions (__try / __except / __finally) mostly work on x86 and x64"). Ainda assim, o `.lib` do NGX é MSVC e o SEH do clang/mingw não pega exceções assíncronas no mesmo frame, que é exatamente o caso de uso do Feeder (access violation dentro do NGX). Não resolve o problema de verdade e adiciona um toolchain.

### 4.3 Opção (c): portar para MinGW (g++ 16.2 já instalado)

**[FATO] Medido nesta máquina** (`mingw-trial\`, cópia do `dlss5-feed.cpp` + headers do Feeder, ReShade do `addon\include`, imgui do repositório do Feeder, NGX headers públicos do repositório NVIDIA/DLSS, Vulkan-Headers, MinHook.h):

| Tentativa | Erros | Do quê |
|---|---|---|
| `g++ -std=c++20 -fsyntax-only` no fonte original | **32** | 30 vêm das **5** construções `__try/__except` (`dlss5-feed.cpp:1609-1617, 1627-1628, 1676-1677, 1694, 2033-2034`, 6 erros cada); 2 são `FARPROC → void*` em `feed_vk_hook.h:260` e `:290` |
| Mesmo fonte com `__try→try`, `__except(...)→catch(...)` | **2** | Só os dois casts (`-fpermissive` ou `reinterpret_cast<void*>`) |

Tudo o mais compila: `strcpy_s`, `sprintf_s`, `_snprintf_s`, `_stricmp`, `fopen_s`, `__uuidof`, `__declspec(dllexport)`, o ReShade API, imgui 1.92.5, MinHook, Vulkan e OpenGL. Nove avisos, todos `novtable` ignorado nos headers do ReShade (o add-on da ponte do próprio projeto já compila assim, `addon\build-addon.sh`).

O que falta para linkar:

- **NGX**: `nvsdk_ngx_d.lib` é um `.lib` MSVC estático (CRT /MD) e o Feeder referencia 11 símbolos (`NVSDK_NGX_D3D12_Init`, `Init_with_ProjectID`, `GetCapabilityParameters`, `AllocateParameters`, `DestroyParameters`, `Shutdown1`, `CreateFeature`, `ReleaseFeature`, `EvaluateFeature` via os macros `NGX_D3D12_CREATE_DLSS_EXT`/`EVALUATE_DLSS_EXT`, e os tipos). Na **variante AMD nada disso é chamado**: um `ngx_stub.cpp` com essas 11 funções devolvendo `NVSDK_NGX_Result_FAIL_FeatureNotSupported` satisfaz o linker. Assim a variante AMD não precisa do NGX SDK nem de Build Tools.
- **SEH**: na variante AMD, os cinco wrappers viram chamadas diretas (nunca executadas) ou `try/catch`; documentar que a variante AMD não tem a rede de segurança do SEH (o mod, por sua vez, tem seu próprio `FAULT: exception ...` handler).
- **Link**: `-shared -static-libgcc -static-libstdc++ -ld3d11 -ld3d12 -ldxgi -ldxguid -ld3dcompiler -lversion -lole32 -ladvapi32 -luser32 -lgdi32` mais os quatro `.c` do MinHook (compilam com gcc). O `version.rc` do Feeder passa pelo `windres` como o `app.rc` do app (`build-mingw.sh`).
- Saída sugerida: `dlss5-feed-amd.addon64`, para não confundir com o binário oficial NVIDIA.

### 4.4 Recomendação

**Opção (c)** para a variante AMD (zero instalação, 2 erros triviais + 5 wrappers, sem NGX SDK), mantendo o `dlss5-feed.addon64` oficial MSVC para NVIDIA. Se mais tarde se quiser um único binário para as duas placas, aí sim a opção (a), porque o caminho NVIDIA depende do SEH real e do `.lib` do NGX. Enviar o patch ao upstream do Feeder (MIT) como PR também é razoável: o autor já tem `feed_fsr1.h` e planos por documento.

---

## 5. gfx1200 (RX 9060 XT): estado, o patch da issue 33, riscos

### 5.1 Estado no repositório (consultado 2026-09-05)

**[FATO]**

- Releases: v0.1.0 e v0.2.0 (2026-09-03), v0.2.6, v0.2.7 (30 fps em 1080p na 9070 XT), v0.2.8, v0.2.9 (extrator reconhece mais builds do `nvngx_dlssnr.dll`), v0.2.10 (04/09, "Fixed GPU detection for systems with multiple GPUs"), **v0.2.11 (05/09 02:19 UTC, "+2% performance")**. Nenhuma menciona gfx1200.
- O instalador v0.2.11 contém apenas `hipv4-amdgcn-amd-amdhsa--gfx1100/1101/1102/1201` (`setup-strings.txt`) e a mensagem `env: UNSUPPORTED GPU ARCHITECTURE %s: this build contains code for RDNA4 (gfx12) and RDNA3 (gfx11) only`.
- Issues abertas sobre a 9060 XT: 16, 19, 24, 26, 33, 38, 45, 49; a 29 foi fechada por um usuário apontando a 33, não pelo autor.
- Repositório sem arquivo de licença, só `README.md` e `.github`. O autor escreveu na issue 4 (2026-09-04 18:16 UTC): "Source code will be published soon, I need to clean it up first for legal reasons".
- Detecções no VirusTotal (issue 40) e desconfiança generalizada (issues 8, 12, 17, 27).

### 5.2 O que é o patch da issue 33 (só documentação)

**[FATO]** Relato de `abdurrahman2go`/`lolo3lili`: dentro do `version.dll` há um fat binary HIP (seções `.hip_fatbin`/`__hipRegisterFatBinary`) com um code object ELF por arquitetura. O patch (1) troca no cabeçalho ELF do objeto gfx1201 o identificador de máquina AMDGPU em `e_flags` de `0x4E` para `0x48` e (2) renomeia as strings de alvo `gfx1201` → `gfx1200` na tabela do bundle. **[FATO]** Na tabela `EF_AMDGPU_MACH_*` do LLVM, `GFX1200 = 0x048` e `GFX1201 = 0x04E`, o que confere. Um comentário na issue 29 descreve que a v0.2.10 já tentou algo parecido mas gravou `0x4D` (valor reservado), o que explicaria o `invalid kernel file`.

**[INFER]** Funciona porque gfx1200 (Navi 44) e gfx1201 (Navi 48) são a mesma geração ISA (GFX12); o runtime HIP recusa carregar um objeto cujo ID não bate com o dispositivo, não por incompatibilidade real de instruções. Confirmado empiricamente em Cyberpunk, GTA V Enhanced, KCD2 e RE Requiem (issues 33, 38, 45, 49), com 8 a 30 fps.

### 5.3 Riscos

- **Jurídico/ético**: código fechado sem licença; modificar e redistribuir o binário do mod não é permitido. O zip da issue 33 é um binário de terceiro desconhecido; não usar. Se fizer, o patch deve ser feito localmente pelo próprio usuário, sobre a cópia dele, e refeito a cada release.
- **Técnico**: cada release muda offsets; a assinatura de "trojan" já existente piora; qualquer instrução ou tuning específico de gfx1201 passaria despercebido; o modo inline depende de spin-wait na GPU e a 9060 XT (16 CUs) já está no teto de 15 a 30 fps relatado (issue 45: o teto independe da carga gráfica, o que aponta para sincronização/dispatch, não para força bruta).
- **Estratégico**: o autor prometeu o fonte. Com fonte, recompilar com `--offload-arch=gfx1200` (ou `gfx12-generic`) resolve de vez. Recomendo que o plano trate o suporte a gfx1200 como dependência externa e, enquanto isso, valide o pipeline numa RDNA3/RDNA4 suportada ou aceite o patch local só para teste.

---

## 6. Seletor de placa no app

### 6.1 Criar o device D3D11 num adaptador escolhido

**[FATO]** Hoje: `main.cpp:870` (`InitWaitable`) e `913`/`917` (`InitLegacy`) chamam `D3D11CreateDevice`/`D3D11CreateDeviceAndSwapChain` com `pAdapter = nullptr` e `D3D_DRIVER_TYPE_HARDWARE`; `DetectarGpu` (`3215-3253`) enumera com `IDXGIFactory1::EnumAdapters1` e assume que o adaptador 0 é o usado (`3216-3219`). O gate `NR_SEM_RTX` está em `LerEstadoNeural` (`3367-3378`) com `PlacaComTensorCore` (`3209-3213`), e há um segundo gate pelo log do Feeder em `3427` (`NgxPeloFeed`, `3336`).

Mudança: (1) nova chave ini `[gpu] adaptador=` (índice ou substring do nome; padrão "auto"); (2) `DetectarGpu` passa a devolver também o `IDXGIAdapter1*` escolhido (regra auto: se houver RTX, ela; senão, se houver AMD 0x1002 e o modo AMD estiver disponível, ela; senão adaptador 0); (3) em `InitWaitable`/`InitLegacy`, quando houver adaptador escolhido, chamar `D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, ...)` (a API exige `UNKNOWN` quando `pAdapter != nullptr`; com `HARDWARE` retorna `E_INVALIDARG`), e no `InitLegacy` a mesma regra em `D3D11CreateDeviceAndSwapChain`; (4) `DetectarGpu` é chamada em `6107`, antes do Renderer, então a ordem já serve.

**[FATO]** O Feeder herda automaticamente: `InitSession` lê `IDXGIDevice::GetAdapter` do device D3D11 (`dlss5-feed.cpp:2662-2672`) e passa esse adaptador ao `D3D12CreateDevice` (`2683`). Nenhuma mudança no Feeder para isso.

### 6.2 Impacto na captura WGC

**[FATO]** `screen_capture.h:639-647` cria o `IDirect3DDevice` do WinRT a partir do nosso device D3D11 (`CreateDirect3D11DeviceFromDXGIDevice`) e o `Direct3D11CaptureFramePool` nele (`678`); o item vem de `CreateForWindow`/`CreateForMonitor` (`660-661`).

**[INFER]** A API não exige que o device de captura esteja na GPU que renderiza a janela ou dirige o monitor: o DWM entrega o frame no device informado, fazendo cópia entre adaptadores quando preciso. Funciona; o custo é uma cópia extra por frame via memória de sistema (dezenas de MB/s em 1080p60, mais em 4K) e latência adicional. [TESTE] medir o FPS de captura com jogo na 4080 e app na 9060 XT.

### 6.3 Impacto no overlay em monitor ligado à outra GPU

**[INFER]** O swapchain do app é flip model em janela (`main.cpp:884-895`, FLIP_DISCARD + waitable object). Quando a GPU que renderiza não é a que dirige o monitor, o DWM faz apresentação cross-adapter (o mesmo mecanismo de notebooks híbridos): funciona, mas o frame passa por uma cópia extra e o modo independent flip fica indisponível, então há um a dois frames de latência a mais e o `SetMaximumFrameLatency(1)` perde parte do efeito. Não afeta a correção. O melhor arranjo físico é o monitor do app na Radeon.

### 6.4 Outros pontos do app afetados por rodar na Radeon

- **[FATO]** `mv_hardware=1` usa `ID3D12VideoMotionEstimator` (`motion_estimate.h:257`, `371`, `398`); **[INFER]** drivers AMD não expõem `D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR`, então o `CheckFeatureSupport` falha e o app deve cair no caminho por shader (padrão `mv_hardware=0`, `main.cpp:5803`). Confirmar que a falha é silenciosa.
- **[FATO]** O estado neural (`main.cpp:3189`, `3363-3466`) precisa de um ramo AMD: (i) não retornar `NR_SEM_RTX` quando vendor = 0x1002 e o modo AMD estiver habilitado (`3367`); (ii) checar os arquivos do mod (`version.dll`, `dlssnr_on_amd.ini`, `dlssnr_on_amd_weights.bin`, `amd_fidelityfx_dx12.dll`) em vez do `renodx-dlss5.addon64` (`3380-3389`); (iii) pular `NgxPeloFeed` (`3427`); (iv) decidir `NR_ATIVO` por `network job` no `dlssnr_on_amd.log` e por `frame delivered` no `dlss5-feed.log`, com mensagens novas para `FSR is not active`, `unsupported colour format`, `UNSUPPORTED GPU ARCHITECTURE` e `no HIP device matches`.
- **Instalação**: o app teria de orientar o usuário a rodar o `dlssnr_on_amd_setup.exe` na pasta do app (é ele que extrai os pesos do `nvngx_dlssnr.dll` e grava o `version.dll`); não redistribuir o mod. O ini gerado deve ficar com `Inline=1`, `Interop=1`, `UseFsrInputs=1`, `UseDepth=1`; `HipDevice=-1`.

---

## 7. Perguntas que só o hardware responde

1. O mod adota o device D3D12 do Feeder num processo cujo primeiro swapchain apresentado é D3D11? Se não, o swapchain-isca resolve?
2. Com `version.dll` do mod e `dxgi.dll` do ReShade hookando `D3D12CreateDevice`/`CreateDXGIFactory*`/`Present`, o processo abre e o Feeder cria o device sem crash? Em que ordem os hooks acabam encadeados?
3. O mod hooka a `amd_fidelityfx_dx12.dll` carregada pelo Feeder antes de `engine init ok`? Ele re-escaneia módulos depois?
4. `unsupported colour format`: 87 (B8G8R8A8_UNORM) é aceito? E 28 (R8G8B8A8_UNORM)? Ou só 10 (RGBA16F), 24, 26? Idem MV em R16G16_FLOAT (34; Cyberpunk usava 10) e depth R32_FLOAT (41; Cyberpunk usava 19 typeless).
5. O sinal e a escala dos MVs fabricados pelo `DLSS5_Feed.fx` são os que o FSR/mod esperam (`mean |mv|` no log do mod)? O `depth autodetect` do mod concorda com o `RESHADE_DEPTH_INPUT_IS_REVERSED`?
6. Em `Inline=1`, o resultado está mesmo na `SLOT_OUTPUT` quando o fence D3D12 sinaliza (ou o mod só aplica após o `ExecuteCommandLists` retornar num submit separado, o que também funcionaria, ou só no Present, o que não funcionaria)?
7. O `hipImportExternalMemory` funciona com os buffers de staging do mod quando o device D3D12 é o do Feeder (privado, não é o "do jogo")?
8. Desempenho real: ms por `network job` em 1080p na 9060 XT com o app (o teto de 15 a 30 fps das issues 45 e 49 vale para nós?) e o efeito do spin-wait na fila do Feeder (o `BeginCommands` tem `gpu_timeout_ms=2000`, `dlss5-feed.cpp:1505-1553`).
9. Captura WGC e apresentação cross-adapter: FPS e latência com o jogo na 4080 e o app na 9060 XT.
10. Estabilidade em redimensionamento: destruir e recriar o contexto FSR enquanto o mod tem staging vivo (`staging re-created`, `ini changed ... re-creating staging`).
11. gfx1200: até sair release ou fonte, só com patch local; testar primeiro numa RDNA3/RDNA4 suportada se houver acesso.

---

## 8. Tarefas em ordem, esforço, e o que dá para fazer sem a 9060 XT

| # | Tarefa | Esforço | Sem a 9060 XT? |
|---|---|---|---|
| 1 | Baixar `amd_fidelityfx_dx12.dll` v1.1.4 + os 4 headers ffx-api para `external/ffx` (ou tipos mínimos inline) e escrever o loader dinâmico | 0,5 dia | Sim |
| 2 | Build MinGW da variante AMD: script `build-feed-amd.sh` (windres + g++ + MinHook .c), `ngx_stub.cpp`, macro `FEED_AMD_VARIANT` que troca os 5 wrappers SEH, corrigir os 2 casts em `feed_vk_hook.h` | 0,5 a 1 dia | Sim (compila e carrega no ReShade da 4080; deve se comportar como o oficial fora do modo AMD) |
| 3 | Ramificação A a F no `dlss5-feed.cpp` (contexto FSR 1:1, dispatch, teardown, cfg `amd_carrier`/`amd_color_fmt`) | 1 a 2 dias | Sim: com `amd_carrier=1` forçado **na 4080**, o FSR 3.1 roda em qualquer GPU DX12. Isso valida contexto, dispatch, barreiras, formatos e copy-back sem AMD nenhuma (o resultado será FSR NativeAA puro, sem NR). |
| 4 | Swapchain-isca atrás de `amd_decoy_swapchain` | 0,5 a 1 dia | Sim (funcional na 4080; só o efeito sobre o mod precisa da AMD) |
| 5 | `main.cpp`: seletor de adaptador (`[gpu] adaptador`, `D3D_DRIVER_TYPE_UNKNOWN` em `870`/`913`/`917`, `DetectarGpu` devolvendo o adaptador) | 0,5 dia | Sim (testar forçando o Basic Render / a iGPU se houver) |
| 6 | `main.cpp`: ramo AMD do estado neural (`3367`, `3380-3389`, `3427`, `3454`), leitura do `dlssnr_on_amd.log`, textos pt/en, checagem dos arquivos do mod | 1 dia | Sim (simular com logs colados das issues 16/49) |
| 7 | Setup/instalador: perfil AMD do `ReShade.ini` sem RenoDX, instruções para rodar o setup do mod, `dlssnr_on_amd.ini` com `Inline=1`, avisos sobre antivírus e gfx1200 | 0,5 dia | Sim |
| 8 | Teste em hardware AMD suportado (RDNA3/RDNA4 com release oficial) seguindo a lista 3.5 e as perguntas da seção 7; iterar formato de cor e isca | 1 a 3 dias | Não |
| 9 | 9060 XT: aguardar release/fonte com gfx1200; se for testar antes, patch local do usuário conforme 5.2, nunca redistribuído | dependência externa | Não |
| 10 | Opcional: PR ao upstream do Feeder com o modo portador FSR | 0,5 dia | Sim |

Total sem hardware AMD: 4,5 a 7 dias de trabalho. Com hardware: mais 1 a 3 dias de iteração.

---

## Apêndice A — Chaves do `dlssnr_on_amd.ini` (v0.2.11) [FATO, strings]

`Enabled=1`, `Inline=1` ("applies the result to the frame it was computed from; the game runs at the network's pace"), `Interop=1`, `UseFsrInputs=1`, `UseDepth=1`, `UseAutoMask=1`, `Temporal=1`, `Tonemap=-1`, `ToneChannels=0`, `LocalTone=0`, `LocalStructure=1`, `SkinStructure=-1`, `HipDevice=-1`, `Scale=0.03125` (significado não identificado). Mudanças em `Inline`/`Interop` recriam o staging em tempo real (`ini changed Inline=%d Interop=%d; re-creating staging`).

## Apêndice B — Linhas do log do mod úteis para o app [FATO, strings]

`dlssnr_amd %s (build %s) loaded (standalone version.dll proxy) from %s`, `hooked %s`, `hooked %s!%s`, `device %p (from the first presented swapchain)`, `device %p via queue`, `env: adapter %u: %ls (vendor 0x%04x, ...)`, `env: d3d12 device %s; d3d11 device %s`, `env: HIP device %d: %s, arch %s, %d CUs`, `env: using HIP device %d (%s)`, `engine init ok`, `first ffxDispatch type 0x%x`, `staging ready: colour %ux%u dxgi %d (pix %d, tonemap %d); motion %dx%d dxgi %d; depth %dx%d dxgi %d (inverted %d); exposure %s; residual %s`, `interop: inputs %s, output %s; mode %s`, `unsupported colour format %d`, `FSR is not active: enable FSR ...`, `network job %d done in %llu ms`, `inline: %u GPU wait timeouts so far`, `FAULT: exception ...`.

## Apêndice C — Fontes

- Mod: `https://github.com/danielblnc/DLSS-NR-on-AMD` (README, releases v0.1.0 a v0.2.11, issues 4, 5, 12, 16, 26, 29, 33, 38, 40, 43, 45, 49), instalador v0.2.11 analisado por strings (`setup-strings.txt`, 13.358 linhas).
- FidelityFX SDK: `https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK` (releases v1.1.4 e v2.3.0; `ffx-api/include/ffx_api/*.h` da v1.1.4; `docs/techniques/super-resolution-upscaler.md`; `docs/license.md` de ambas).
- Feeder: `https://github.com/jlrouzies-fr/DLSS5-Feeder` (v0.13.1-beta.1; `src/dlss5-feed.cpp` 5898 linhas; `build.bat`; `tools/vcvars.bat`; `external/ngx/README.md`; `PLAN-PROXY-SWAPCHAIN.md`; `README.md`).
- Projeto local (só leitura): `main.cpp` (7964 linhas), `screen_capture.h`, `motion_estimate.h`, `build-mingw.sh`, `addon/build-addon.sh`, `dlss5-feed.log`, binários para contagem de strings.
- Microsoft Learn, "Install the Microsoft C++ (MSVC) Build Tools" (winget/.vsconfig, IDs de componentes); Clang "MSVC compatibility" (SEH "mostly work on x86 and x64").
