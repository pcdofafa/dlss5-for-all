# Frame generation dentro do DLSS5 For All — como gerar quadros intermediários no nosso pipeline

Pesquisa feita em 2026-09-05. Somente leitura do projeto; nenhum arquivo do app foi alterado, nada foi executado. Substitui o `RELATORIO-METODOS-NVIDIA.md` desta mesma pasta (foco redirecionado pelo usuário para frame generation).

Alvo prático definido pelo usuário: **dobrar os fps apresentados na janela sobreposta**, usando os motion vectors e a profundidade que já temos (kernels Lumenite) ou fluxo óptico calculado por nós, para jogos e para janelas genéricas (vídeo).

Convenções:

- **[FATO]** — verificado em código, log, documento oficial ou página pública citada nas Fontes.
- **[INFERÊNCIA]** — raciocínio meu a partir dos fatos; marcado como tal.
- Caminhos: `amd\main.cpp` (9.298 linhas), `amd\addon\dlss5_bridge_addon.cpp`, `amd\feeder-amd\src-feeder\` (Feeder 0.12.0), `amd\feeder-amd\src-amd\feed_amd.h`, `amd\feeder-amd\ffx-bin\`. Medições próprias: `docs\latencia.md`, `docs\supersampling.md`, `RELATORIO-DESEMPENHO.md`, `RELATORIO-3X.md` (scratchpad `camada3x`). Parecer anterior sobre o tema: `docs\viabilidade-framegen-upscaling.md` (2026-09-02).

---

## Tabela-resumo

| Caminho | Onde roda | Placa | Entradas que usa | Latência somada (a 60 fps de base) | Esforço | Veredito |
|---|---|---|---|---|---|---|
| **(c1) NVIDIA Smooth Motion** por perfil do exe | driver, em processo (`NvPresent64.dll` embrulha o nosso swapchain D3D11) | RTX 40/50 | só cor (fluxo óptico do driver) | +1 quadro segurado + pacing (~+20–25 ms) | **0 código** + atualizar Feeder para 0.12.0 (0,5 dia) | **Testar primeiro.** Nunca visto engatando num exe "não jogo" D3D11 em janela (mpv falhou; mpc-hc carregou a DLL sem efeito). Debug Bars respondem em 30 min. |
| **(b) AMD FSR 3.1 Frame Generation** (FidelityFX SDK 1.1.4, MIT) num **segundo apresentador D3D12** dentro do app | nosso código | **qualquer** (NVIDIA e AMD; analítico, SM 6.2) | cor final + MV Lumenite + depth + fluxo óptico próprio do FSR | +1 quadro + até 1 período de refresh (~+20–35 ms) | **7–10 dias úteis** para protótipo | **Recomendado para o protótipo.** DLL já está no repositório, licença limpa, serve a variante AMD, e o mesmo desenho aceita DLSS-G depois. |
| **(a) DLSS Frame Generation / MFG** via Streamline `sl.dlss_g` no mesmo segundo apresentador | nosso código + Streamline | RTX 40 (2x), RTX 50 (3x/4x, DMFG) | cor + MV + depth (+ HUDless/UI opcionais); matrizes podem ser identidade | igual ao FSR-FG (+ pacer do SL) | +5–8 dias sobre (b) | **Segunda etapa.** Só D3D12/Vulkan; NGX direto não é suportado (só via Streamline); binários fechados sob licença dos RTX SDKs. O OptiScaler mostra a sequência exata de chamadas. |
| **(c2) AMD AFMF 2.1** por perfil (variante AMD) | driver, injeção em processo | RDNA 2/3/4 | só cor | +1 quadro + pacing (AFMF 2 −28 % vs AFMF 1) | 0 código | Exige tela cheia ou sem borda, V-Sync off; injeção pode não pegar num app genérico (VLC D3D11 não funciona; PotPlayer bloqueia). Testar na Radeon. |
| **(d1) NVIDIA Optical Flow SDK / NvOFFRUC** (FRUC pronto, D3D11 nativo) | nosso código, add-on ReShade ou app | RTX 20+ (NVOFA) | só cor (ARGB/NV12); calcula o fluxo em hardware | +1 quadro + pacing manual nosso | 5–8 dias | Único FG **D3D11 nativo**. Pacing manual em D3D11 é o risco. Licença DesignWorks: redistribuição da `NvFRUC.dll` a confirmar. |
| **(d2) fluxo óptico próprio** (Lumenite já estima MV) + shader de interpolação nosso | nosso código | qualquer | cor + nosso MV | igual a (d1) | 2–3 semanas para ficar apresentável | O que o LSFG faz. Não vale reinventar: o FSR-FG já traz optical flow + interpolação + pacing prontos em MIT. |
| **Lossless Scaling** por cima da nossa janela (comparação comercial) | outro processo | qualquer | só cor | **+13 ms medidos** a 40→80 fps (LSFG 3.1) | 0 (receita no README) | Referência de qualidade/latência. Com DLSS 5 + LS numa 4080 a comunidade mediu ≤ 30 fps com ghosting: saturação de GPU. |

**Regra que vale para todos:** frame generation **não reduz** o custo da rede; ela precisa de **folga de GPU** para caber e de uma taxa de base decente. Medimos que cada camada de NR custa ~20 pontos de uma RTX 4080 (5,5–6,7 ms/quadro a ~2000x1080) e que 2x a 60 fps já leva a GPU a 99 % [FATO, `RELATORIO-3X.md`]. Ver §4 antes de decidir em que modo ligar.

---

## 1. O que o nosso pipeline oferece a um gerador de quadros [FATO]

- **Saída final** já pronta a cada Present do nosso swapchain D3D11 (FLIP_DISCARD, waitable, latência 1, `main.cpp:935-997`). A cadeia inteira (Lumenite → SMAA → `DLSS5_Feed` → DLAA + NR → Sharpen) roda **dentro do Present**, pelo ReShade (`RELATORIO-DESEMPENHO.md` §a.3).
- **Motion vectors e profundidade** existem como texturas do ReShade produzidas pelo `DLSS5_Feed.fx`/Lumenite: o Feeder as lê com `get_texture_binding` e copia cor (4 B) + depth R32_FLOAT + MV R16G16_FLOAT + máscara para texturas compartilhadas D3D11→D3D12 a cada quadro (4 `CopyResource`, `dlss5-feed.cpp` `FeedFrame11`). A profundidade do app é plana (z = 0,5) a menos que o Lumenite estime; os MV são **estimados por fluxo óptico**, não vêm da engine (`docs\motion-vectors.md`).
- **Device D3D12 privado** com fila própria no Feeder, sincronizado com o contexto D3D11 por fence (`ctx4->Signal` / `queue->Wait` / `ctx4->Wait`), e um precedente de **segundo swapchain D3D12 em processo** convivendo com o ReShade: o Feeder AMD cria um swapchain-isca D3D12 32x32 numa janela oculta, o ReShade abre um segundo effect runtime sobre ele e o Feeder o ignora por flag (`feed_amd.h:317-345`).
- **Nosso add-on ReShade** (`dlss5_bridge_addon.cpp`) já está registrado em `reshade_begin_effects` e `reshade_present` (`:927-928`) — isto é, temos um gancho **depois** de todos os efeitos e do NR, antes do Present real. É o ponto natural para exportar cor final + MV + depth para um estágio de FG.
- **Medições que fixam a conta:** 1x ≈ 30 ms fonte→pixel (42 ms no teste sintético de `latencia.md`), 2x ≈ 85–90 ms, 3x ≈ 130–145 ms; GPU 58 % em 1x a 60 fps, 99 % em 2x a 60 fps e em 3x a 44 fps; a 3x com a GPU saturada o present→tela de cada camada sobe de ~22 para ~34 ms (`RELATORIO-3X.md`).
- **Versão embarcada do Feeder: 0.7.0** em todas as cópias NVIDIA (`amd\dlss5-feed.log:1`, `DLSS5ForAll-AMD\bin\dlss5-feed.log:1`, raiz). O fonte em `feeder-amd\src-feeder` é o 0.12.0 (`src\version.rc`).

---

## 2. Os caminhos, com mecanismo, requisitos e esforço

### 2.1 (a) DLSS Frame Generation — Streamline `sl.dlss_g` (e por que não `nvngx_dlssg` direto)

**Exigências oficiais** [FATO, `ProgrammingGuideDLSS_G.md`, changelog 2.12.0]:

- APIs: **D3D12 e Vulkan**. D3D11 não existe no guia nem em nenhuma entrada do changelog. Um funcionário da NVIDIA respondeu em 18/10/2023 que não há Frame Generation fora do Streamline — o `nvngx_dlssg.dll` não é uma API pública para chamar direto no device D3D12 do Feeder.
- Hardware: Ada no mínimo; **HAGS obrigatório**; Windows 10 20H1+. MFG: `numFramesToGenerateMax` consultado por `slDLSSGGetState` (RTX 50 para 3x/4x); DMFG (dinâmico) exige driver 595.41 e "is currently limited to D3D12, only".
- Reflex: "It is required for sl.reflex to be integrated" — é sincronização do índice de quadro com os markers `PresentStart/End`.
- Swapchain: o Streamline devolve uma **fábrica DXGI proxy** e "will automatically attach to any swap-chain created by the application"; recriar o swapchain ao ligar/desligar.
- Entradas: backbuffer (interceptado), **depth** e **MV** obrigatórios; HUDless e UI color+alpha "for the best image quality"; `sl::Constants` com matrizes e jitter "must be provided if any SL feature is active".
- V-Sync com FG depende de Independent Flip, que "can be blocked by certain overlays, resolution mismatches, or forced DWM composition". Medir com `MsBetweenDisplayChange` (FrameView), porque o SL pacer atrasa a imagem depois do Present.
- **Custo por quadro gerado:** RTX 4090 — 1,35 ms (1080p), 1,78 ms (1440p), 2,77 ms (4K); RTX 5080 1080p — 1,45 ms (2x) / 2,43 ms (4x); RTX 5090 4K — 1,72 / 3,32 ms.

**Como o OptiScaler liga DLSS-G em jogo que não tem** [FATO, `FG_Hooks.cpp` e `DLSSG_Dx12.cpp`]:

1. Hook de `CreateSwapChain`/`CreateSwapChainForHwnd`, `Present`/`Present1`, `ResizeBuffers`, `SetFullscreenState`, `GetFrameLatencyWaitableObject`, `Release`. Exige `ID3D12CommandQueue` como device (`QueryInterface` → senão `E_INVALIDARG`): **só D3D12**; em jogos D3D11 há um interop `Dx11wDx12` experimental e o pedido de OptiFG para DX11/Vulkan segue aberto (issue #1110).
2. `StreamlineProxy::LoadStreamline()` (`sl.interposer.dll`) → `InitWithD3D12(device)` → `UpgradeInterface(&factory)` → `SetFeatureLoaded(kFeatureDLSS_G, true)` → `factory2->CreateSwapChainForHwnd(cmdQueue, hwnd, ...)` → lê `numFramesToGenerateMax`.
3. `DLSSGOptions{ mode = eOn, numFramesToGenerate = N, queueParallelismMode = eBlockPresentingClientQueue }` (ou `eDynamic` + `dynamicTargetFrameRate`).
4. **Reflex:** `ReflexOptions{ mode = eLowLatency, useMarkersToOptimize = gameIsSendingMarkers() }` — o "obrigatório" é satisfeito ligando o plug-in `sl.reflex` do próprio SL; markers do jogo são opcionais.
5. **Constantes:** `cameraViewToClip = XMMatrixPerspectiveFovRH(fov, aspect, near, far)` quando há dados, senão só avisa "Can't calculate projectionMatrix"; **`clipToPrevClip`, `prevClipToClip` e `clipToLensClip` = identidade**; `mvecScale = escala / largura da textura de MV`; `cameraMotionIncluded = true`; `jitterOffset` do upscaler (para nós, zero).
6. Por quadro: `GetNewFrameToken` + `SetConstants` + `SetTagForFrame` (depth, MV, hudless/UI quando existem, com `eValidUntilPresent`). Em `FGPresent()`: `fg->Present()` e depois o Present original.

O Nukem9 `dlssg-to-fsr3` confirma o contrato do lado NGX (o que o `nvngx_dlssg` recebe): `DLSSG.Backbuffer`/`HUDLess`/`Depth`/`MVecs`, `CameraViewToClip`, `CameraFOV/Near/Far`, `MvecScaleX/Y`, `JitterOffsetX/Y`, `ColorBuffersHDR`, `DepthInverted`, `EnableInterp`, `Reset`, `MultiFrameCount/Index`, saídas `OutputInterpolated`/`OutputReal` — e que **quem apresenta e ritma é o Streamline**, não o NGX.

**Para nós:** o device D3D12 privado do Feeder não serve; o que serve é o **segundo apresentador D3D12** do §2.2, trocando o swapchain FFX pelo swapchain do SL. O relatório de 02/09 listava quatro bloqueios (D3D11, Reflex, HUDless, matrizes); o código do OptiScaler reduz a **um** duro (D3D11) e três degradações aceitas pela comunidade. Esforço: +5–8 dias sobre (b). Placas: RTX 40 (2x) e 50 (MFG/DMFG). DLLs: `sl.interposer.dll`, `sl.common.dll`, `sl.dlss_g.dll`, `sl.reflex.dll`, `nvngx_dlssg.dll` — o framework é MIT, os plug-ins fechados e o NGX ficam sob a licença dos RTX SDKs (marcas NVIDIA em splash/about/créditos, "material additional functionality", sem engenharia reversa) [FATO].

### 2.2 (b) AMD FidelityFX Frame Generation (FSR 3.1) — o candidato ao protótipo

**O que é** [FATO, GPUOpen]: desde a 3.1.0 "frame generation runs independently of FSR upscaling"; "you can run FSR Frame Generation without any upscaling method". A API (`ffx_api`, a mesma que o nosso `feed_amd.h` já usa via `amd_fidelityfx_dx12.dll` do SDK 1.1.4, MIT, 6,7 MB, já em `feeder-amd\ffx-bin\` e `feeder-amd\dist\`):

- **Contexto de FG** (`FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION`) e **swapchain de interpolação** (`..._FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12`, ou `_NEW_DX12` / `_WRAP_DX12` sobre um swapchain existente). O swapchain "acts as a drop-in replacement for DXGI"; usa **duas filas** (compute assíncrono para a interpolação, fila de present para composição/apresentação); pacing por "a high-priority pacing thread [that] keeps track of average frame time ... and calculates the target presentation time delta", apresentando o quadro gerado e depois o real. O OptiScaler cria exatamente assim (`FSRFG_Dx12.cpp:955-964`) e afina o pacing por `FfxSwapchainFramePacingTuning` (`:282`).
- **Passe "prepare"** (`ffxDispatchDescFrameGenerationPrepare`): `depth`, `motionVectors`, `renderSize`, `jitterOffset`, `motionVectorScale`, `cameraNear/Far`, `cameraFovAngleVertical`, `frameTimeDelta`, `viewSpaceToMetersFactor` (`FSRFG_Dx12.cpp:560-590`). O bloco opcional `PrepareCameraInfo` (posição/up/right/forward) o OptiScaler **só encadeia se não for tudo zero** — "the driver upgrade dll is looking at this and falling back to FSR 3 FG" (`:508-514`). Ou seja: o FG 3.1 analítico funciona sem câmera real; o FG 4 (ML) do SDK 2.x exige "valid information about the camera position and orientation".
- **Fluxo óptico próprio:** "a highly-optimized optical flow technology, written in pure HLSL", combinado com a interpolação "allows generating a new 4K frame in less time than some upscale technologies", em compute assíncrono [FATO, GPUOpen]. AMD não publica tabela em ms para o FG; assumo 2–4 ms em 1440p numa 4080, da ordem do DLSS-G, por rodar em shaders genéricos e incluir o fluxo óptico [INFERÊNCIA].
- **UI:** três opções — callback de apresentação, textura de UI separada, ou HUDless para "detect UI areas ... and suppress distortion". Nada obrigatório.
- **Dispatch manual** sem o swapchain: "It is possible, but *highly* discouraged" [FATO, manual do FSR SDK] — o pacing é o motivo.
- APIs: **DX12 (CS 6.2/6.6) e Vulkan**; DX11 não (o fork `optiscaler/FidelityFX-SDK-DX11` porta só o upscaler). Placa: qualquer com SM 6.2 — o Nukem9 roda FSR3-FG em RTX 20/30 e o OptiScaler em tudo. **FSR 4 FG (ML, "Redstone")** exige RDNA 4 oficialmente, Agility SDK 1.4.9+ e Windows 11; moddado roda em RDNA 3 [FATO].

**Onde entra no nosso código (desenho "segundo apresentador")** [INFERÊNCIA a partir do código]:

```
WGC ─► swapchain A (D3D11, janela OCULTA) ─Present─► ReShade: Lumenite → Feed → DLAA+NR → Sharpen
                                                      └─ dlss5_bridge_addon @ reshade_present:
                                                         copia cor final + MV (R16G16) + depth (R32)
                                                         para 3 texturas compartilhadas (NT handle) + fence
apresentador D3D12 (thread própria) ─► ffx prepare(MV, depth) ─► swapchain FFX FG (janela VISÍVEL, overlay)
                                          └─ apresenta gerado + real, pacing do FFX
```

- `main.cpp`: o `Renderer` de hoje passa a apresentar numa janela oculta (o Present de FLIP_DISCARD numa janela oculta devolve `DXGI_STATUS_OCCLUDED` e o ReShade continua rodando a cadeia — é o que o swapchain-isca do Feeder AMD já faz, `feed_amd.h:329-333`). A janela visível (seguir alvo, topmost, clique-através, `WS_EX_LAYERED`, F11) passa a ser a do apresentador D3D12; o laço principal só muda quem recebe `FollowTarget`/`ResizeClientTo`.
- `dlss5_bridge_addon.cpp`: novo callback em `reshade_present` (já registrado, `:928`) que faz 3 `CopyResource` para texturas compartilhadas e um `Signal` de fence — mesmo padrão do `FeedFrame11` do Feeder (que já resolve os `get_texture_binding` do MV e do depth do `DLSS5_Feed.fx`).
- Apresentador: device D3D12 nosso (ou o do Feeder), `ffxCreateContext` do back-end DX12 + FG + swapchain FOR_HWND, `ffxConfigureDescFrameGeneration{ frameGenerationEnabled, allowAsyncWorkloads, flags de debug }`, por quadro `prepare` + o callback de FG que o swapchain dispara no Present.
- ReShade vai abrir um **segundo runtime** sobre o swapchain visível (`ReShade2.ini`); empacotar um `ReShade2.ini` sem técnicas (custo ~0). Isso **exige Feeder ≥ 0.11.0-beta.2**, que rastreia runtimes e alimenta o que renderiza `DLSS5_Feed` [FATO, README 0.12.0] — mais um motivo para o 0.12.0.
- Convenções a acertar: sinal/escala dos MV (o Feeder já tem `mv_scale` e `depth_inverted` no cfg), depth plano vs. estimado, `cameraNear/Far/Fov` sintéticos (1 / 1000 / 60°), `frameTimeDelta` da nossa sonda LAT. HUDless: nenhum (o HUD do jogo será interpolado, como no Smooth Motion e no LSFG).

**Esforço:** 7–10 dias úteis para protótipo funcional (spike isolado 2 dias, exportação 2 dias, apresentador + janela 3 dias, convenções e medição 2–3 dias). **Licença:** MIT (arquivo `LICENSE-FidelityFX-SDK-1.1.4.txt` já embarcado); nada da NVIDIA. **Placa:** qualquer — inclusive a variante AMD do projeto.

### 2.3 (c) Frame generation no driver: Smooth Motion e AFMF 2

**Smooth Motion** [FATO]: RTX 40/50, DX11/DX12/Vulkan, ligado **por programa** (NVIDIA app → Driver Settings, ou Profile Inspector: `0xB0D384C0` Enable, `0xB0CC0875` Enabled APIs = 1 DX12 / 2 DX11 / 4 Vulkan, `0xB01B8B02` Debug Bars). No Windows é um **interposer em processo**: `NvPresent64.dll` injetado do DriverStore, hook de `CreateDXGIFactory*`, **wrapper COM sobre o `IDXGISwapChain` do app**, segundo device D3D11 e swapchain proxy invisível (`InvisibleWindowClassNvPresent`), **mais de um Present por quadro a partir de uma thread própria**; liga Low Latency Mode sozinho. Ganhos publicados numa 4090 em 4K: +40–70 % (Cyberpunk, Destiny 2, Mafia, Wuchang), artefatos leves em vegetação. Em apps não-jogo: navegadores e players vêm com o bitfield em 0; forçar 15 no `mpc-hc64.exe` carrega a DLL "mas não está claro se é efetivo"; no mpv não funcionou (issue #17140, 12/2025); emuladores Vulkan funcionam. Vulkan + Feeder **não** funciona (o driver interpola cópias tiradas antes do add-on) — irrelevante para nós, que somos D3D11.

Riscos específicos do nosso exe [INFERÊNCIA]: o driver pode não injetar num exe sem perfil de jogo (só o teste responde); o wrapper precisa repassar `GetFrameLatencyWaitableObject` (temos fallback para swapchain clássico, `main.cpp:965`); janela `WS_EX_LAYERED` + topmost impede Independent Flip (o NvPresent apresenta pelo proxy invisível, então talvez não dependa disso — sem fonte). Nossas janelas de painel/cartaz são separadas, logo **não** são interpoladas. **Pré-requisito absoluto:** Feeder ≥ 0.11.0-beta.2 (thread-safe desde 0.8.0; alimenta o runtime certo desde 0.11.0-beta.2) — o 0.7.0 embarcado corrompe ou alimenta o swapchain invisível [FATO, README do Feeder]. Já hoje, quem liga Smooth Motion globalmente no NVIDIA app quebra o nosso 0.7.0.

**AFMF 2 / 2.1** [FATO]: RDNA 2/3/4 (RX 6000+), DX11/DX12/Vulkan/OpenGL, "AFMF 2 currently requires the game to be played in exclusive or borderless fullscreen mode with V-SYNC disabled" (sem borda só em RX 7000+), AMD recomenda 60 fps de base; AFMF 2 reduziu a latência em 28 % vs AFMF 1. Funciona por injeção de código: PotPlayer bloqueia, VLC D3D11 não funciona, MPC-HC só funcionou via DXVK num driver preview. Para a variante AMD do projeto: adicionar o exe no Adrenalin e testar em tela cheia sem borda; expectativa baixa para uma janela sobreposta em camadas [INFERÊNCIA].

### 2.4 (d) Fluxo óptico sem MV: NvOFFRUC, VK_NV_optical_flow, ou o nosso shader

- **NvOFFRUC (Optical Flow SDK 5.x)** [FATO]: Turing+ (NVOFA em hardware), driver 511.65+, **`ID3D11Device`/`ID3D11Texture2D` nativos** e CUDA, formatos ARGB e NV12, sincronização por `ID3D11Fence` ou `IDXGIKeyedMutex`, sequência `NvFRUCCreate → RegisterResource → Process → UnregisterResource → Destroy`. Calcula o fluxo ele mesmo: não depende do Lumenite. Sem números de desempenho na documentação. Licença "NVIDIA DesignWorks SDK License" — redistribuição da `NvFRUC.dll` a confirmar (a implementação de 2023 no FFmpeg não entrou no mainline por licença). É o único FG **D3D11 nativo** da lista; o preço é **pacing manual**: em D3D11 não há swapchain proxy, então seríamos nós a apresentar gerado + real com o waitable como marca-passo. Esforço 5–8 dias; risco no pacing, não na API.
- **VK_NV_optical_flow** [FATO]: em 29/08/2026 o FFmpeg incorporou o filtro `fruc_vulkan` — fluxo pela extensão Vulkan pública e interpolação num compute shader do próprio FFmpeg, "sidestepping SDK licensing"; RTX 30+. Mostra que dá para ter o NVOFA sem a DLL da NVIDIA, mas exige um device Vulkan e interop: mais uma API no processo. Não recomendado para o protótipo.
- **Shader próprio** [INFERÊNCIA]: o Lumenite já produz o fluxo; faltaria o interpolador (warp bidirecional, oclusão, preenchimento) e o pacing. É exatamente o que o LSFG e o FSR-FG fazem — e o FSR-FG entrega tudo isso pronto, em MIT, com pacing. Só faria sentido se quiséssemos ficar em D3D11 puro sem a NVIDIA; mesmo assim o NvOFFRUC é mais barato.

---

## 3. (e) Latência, e antes ou depois da rede

**Onde o FG entra:** **depois** da rede, na saída final, uma única vez para a cascata inteira. Motivos: (1) gerar quadros antes do NR obrigaria a rodar a rede também nos quadros gerados — dobra o custo dominante; (2) o NR é 1:1 e não desloca pixels, então os MV estimados na entrada da última camada continuam válidos para a saída dela [INFERÊNCIA]; (3) no 3x, o FG na última camada usa os MV que ela mesma estimou sobre a saída da segunda — já são "MV da imagem que vai ser interpolada".

**Quanto cada caminho soma** (todo interpolador segura o quadro novo até ter o par; a partir daí é pacing e cálculo):

| Caminho | Composição da soma | A 60 fps de base | A 30 fps de base | Fonte |
|---|---|---|---|---|
| Lossless Scaling LSFG 3.1 | medido fim a fim | **+13 ms** (28,47 → 41,70 ms a 40→80 fps) | ~+25–30 ms [INFERÊNCIA proporcional] | Tom's Hardware |
| Smooth Motion | 1 quadro + pacing do driver + LLM automático | +17–25 ms | +35–50 ms | sem medição publicada em ms; estrutura igual ao DLSS-G |
| DLSS-G (jogo) | 1 quadro + SL pacer | ~+16 ms medidos em Cyberpunk (47 → 63 ms) | +33–50 ms | Hardware Unboxed via digitalphablet |
| FSR 3.1 FG (swapchain FFX) | 1 quadro + "may increase input latency (by up to one refresh period)" com V-Sync | +17–33 ms | +33–66 ms | GPUOpen |
| NvOFFRUC / shader próprio | 1 quadro + pacing nosso | +17 ms + o que errarmos | +33 ms + idem | — |

Somando ao que já temos (`RELATORIO-3X.md`): **1x: 30 → ~45–60 ms; 2x: 85–90 → ~105–125 ms; 3x: 130–145 → ~165–210 ms.** O README já classifica +25–42 ms do LS como "não serve para competitivo"; nada aqui muda isso.

---

## 4. Quando o frame gen ajuda e quando atrapalha

**Custo por quadro gerado, numa RTX 4080 a ~2000x1080–1440p** [INFERÊNCIA a partir da tabela oficial da 4090 e da razão de desempenho 4080/4090]: DLSS-G ≈ 1,8–2,5 ms; FSR 3.1 FG ≈ 2–4 ms (fluxo óptico + interpolação, sem número oficial); Smooth Motion e LSFG na mesma ordem (LSFG 3.1 corta 40 % da carga do LSFG 2, mas ninguém publica ms). Some **0,5–1 ms** de cópias e composição do nosso lado.

**O que acontece com a GPU saturada** [FATO + INFERÊNCIA]: o FG roda na mesma GPU. Com 99 % de uso, cada ms do FG sai das camadas: o fps **real** cai (a 30 fps com 33 ms de orçamento, 2,5–3,5 ms são 8–10 %: 30 → 27–28 fps reais), e o pior não é a média: o pacing precisa que o quadro gerado esteja pronto no meio do intervalo, e com três redes disputando a fila isso não é garantido — medimos o present→tela de cada camada subindo de ~22 para ~34 ms justamente quando a GPU satura, e uma vez a primeira camada morreu por timeout de fence com a GPU a 99–100 % (`RELATORIO-3X.md`). O próprio guia do Lossless Scaling manda travar o fps do jogo para "avoid 100 % GPU load", e o experimento da comunidade com DLSS 5 + LS numa 4080 acabou em ≤ 30 fps com ghosting pesado e quedas constantes [FATO, gamegpu]. Regra de folga: o FG precisa do seu custo **mais margem de pacing**, o que dá **~15 % do orçamento de quadro livre a 60 fps de base (≈ 2,5–3 ms) e ~10 % a 30 fps (≈ 3–4 ms)**; abaixo disso o resultado é pior do que sem FG.

**Taxa de base:** AMD recomenda 60 fps mínimos; Intel aceita 40; LS pede 30 (ideal 60); a NVIDIA não publica número, mas a imprensa converge em "a 30 fps de base a experiência é ruim e os artefatos se amplificam" [FATO]. A 30 fps o deslocamento entre quadros dobra, e é onde fluxo óptico estimado erra mais (desoclusões, HUD).

**Veredito por cenário (RTX 4080, ~2000x1080, FG na última camada, monitor de ≥ 100 Hz):**

| Cenário | GPU hoje | Folga | O que o FG faz | Latência | Vale? |
|---|---|---|---|---|---|
| **1x a 60 fps** | 58 % (≈ 9,7 ms de 16,7) | ~7 ms | 60 → ~120 apresentados, base fica em 60 | 30 → ~45–60 ms | **Vale.** Único cenário com folga sobrando. Só faz sentido em monitor ≥ 100 Hz (em 60 Hz não há onde pôr o quadro extra). |
| **2x a 60 fps** | 99 % | nenhuma | base cai para ~50–52, pacing irregular | 85–90 → ~110–125 ms | **Não vale assim.** Vale **com teto**: 2x travado a 45 fps deixa a GPU em ~75–80 %, e o FG entrega ~90 apresentados com ~115 ms. |
| **3x a 30 fps** | 99 % | nenhuma | base cai para ~27–28, gerados ~55, artefatos de base a 30 fps | 130–145 → ~185–210 ms | **Não vale.** Três históricos temporais em série + interpolação a 30 fps + GPU no teto. Para "30 → 60 fluido" o caminho é **2x travado a 30** (GPU ≈ 66 %, folga ~11 ms) com FG: 60 apresentados a ~125–140 ms; ou 1x a 60 sem truque. |

Resposta direta à pergunta: **"3x a 30 + FG para mostrar 60" não vale** — a GPU não tem os ~10 % de folga, a base de 30 fps está abaixo do piso de qualquer fornecedor, e a latência passa de 180 ms. O FG só ajuda quando sobra GPU: ~15 % de folga a 60 fps de base, ~10 % a 30 fps de base, e com a saída num monitor cuja taxa comporte os quadros extras.

---

## 5. Recomendação e plano

**Ordem:** (0) teste de 30 minutos do Smooth Motion, porque é de graça; (1) protótipo do **segundo apresentador com FSR 3.1 Frame Generation** em 1–2 semanas, porque é a única rota que fica **dentro do programa**, sem DLL proprietária nova, em qualquer placa, e cujo desenho aceita DLSS-G depois; (2) DLSS-G via Streamline no mesmo apresentador se a qualidade do FSR-FG com MV estimado decepcionar em RTX 40/50.

### Passo 0 — Smooth Motion (0,5 dia, sem código)

Atualizar o `dlss5-feed.addon64` da variante NVIDIA para 0.12.0 numa **cópia de teste** (compilar `feeder-amd\src-feeder` com o `build.bat` upstream/MSVC, ou baixar o release); conferir `dlss5-feed 0.12.0`, `EnableHooks=2`, `NRStyle=0`. Perfil no Profile Inspector: Enable = 1, Enabled APIs = 2, Debug Bars = 1. Rodar em modo janela separada, 1x, alvo a 60 fps: barras coloridas = funciona; medir com PresentMon (`MsBetweenDisplayChange`), `nvidia-smi` e a linha LAT. Sem barras em janela separada, sem borda e overlay → encerrar e documentar. Com barras → README + (opcional, 1–2 dias) chip no painel que grava o perfil por `NvAPI_DRS_*` e mostra "ATIVO" quando `NvPresent64.dll` está carregada.

### Passo 1 — protótipo FSR 3.1 FG no segundo apresentador (7–10 dias úteis)

1. **Spike isolado (dias 1–2).** Exe de teste D3D12 que cria `ffxCreateContext` (back-end DX12 + `FRAMEGENERATION` + `FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12`) com a `amd_fidelityfx_dx12.dll` 1.1.4 de `feeder-amd\ffx-bin\` e os headers `ffx_api` que o `feed_amd.h` já inclui; alimenta cor sintética em movimento, MV zerado e depth plano; liga `FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES`/`DEBUG_VIEW`; confirma 2 presents por quadro na RTX 4080 e mede o custo com PIX/`nvidia-smi`. Referência de chamadas: `FSRFG_Dx12.cpp:824-1000` (criação) e `:496-612` (prepare).
2. **Exportação a partir do ReShade (dias 3–4).** No `dlss5_bridge_addon.cpp`, no callback de `reshade_present` (registrado em `:928`): resolver as texturas de MV e depth do `DLSS5_Feed.fx` por `get_texture_binding` (mesmos nomes que o Feeder usa em `FeedFrame11`), copiar cor final + MV + depth para três texturas `D3D11_RESOURCE_MISC_SHARED_NTHANDLE` e sinalizar um `ID3D11Fence` compartilhado; expor os handles pelo mesmo `GetProcAddress` que o `reshade_bridge.h` já usa.
3. **Apresentador e janelas (dias 5–7).** Em `main.cpp`: o `Renderer` atual passa a apresentar numa janela oculta; nova janela visível recebe o swapchain FFX D3D12 e herda `FollowTarget`, topmost, `WS_EX_LAYERED`/clique-através e F11; thread do apresentador espera a fence, roda `prepare` (MV com `mv_scale`/`depth_inverted` do cfg do Feeder, `cameraNear/Far/Fov` = 1/1000/60°, `frameTimeDelta` da sonda LAT) e deixa o swapchain FFX fazer o Present duplo. Empacotar `ReShade2.ini` vazio para o segundo runtime e confirmar no `dlss5-feed.log` (0.12.0) que o Feeder ficou preso ao runtime da janela oculta.
4. **Convenções e medição (dias 8–9).** Sinal do MV (testar as duas convenções numa panorâmica lenta), depth plano vs. Lumenite, `allowAsyncWorkloads` on/off. Medir 1x a 60 e 2x travado a 45: fps apresentado (PresentMon), utilização (`nvidia-smi -l 1`), latência (LAT PRESENT→TELA + o quadro segurado), e um vídeo de teste com HUD estático para ver a distorção de UI. Critério de aceitação: pacing sem soluços com a GPU < 80 % e latência dentro do previsto em §3.
5. **Produto mínimo (dia 10).** Chip no painel "Quadros extras" (regra do projeto: sem jargão, só efeito garantido), habilitado só quando `camadas == 1` ou quando há teto de fps que deixe a GPU < 80 %; aviso de latência no cartaz; README com o desenho e a tabela do §4. DLLs novas: apenas `amd_fidelityfx_dx12.dll` (MIT; a licença já está em `feeder-amd\dist\LICENSE-FidelityFX-SDK-1.1.4.txt`). Nada da NVIDIA é redistribuído.

### Passo 2 — DLSS-G no mesmo apresentador (só em RTX 40/50, +5–8 dias)

Trocar o swapchain FFX pelo do Streamline seguindo `DLSSG_Dx12.cpp`: `slInit` com D3D12, `slUpgradeInterface` na fábrica, `CreateSwapChainForHwnd` na fila, `DLSSGOptions`/`ReflexOptions` como no §2.1, tags de depth/MV, constantes com `clipToPrevClip` identidade. Exige HAGS ligado e os cinco binários do SL sob a licença dos RTX SDKs (marcas NVIDIA no about/créditos). Vale se a diferença de qualidade sobre o FSR-FG for visível com os nossos MV estimados — o que só o passo 1 mostra.

**O que não fazer:** FG antes da rede; NvOFFRUC/shader próprio antes de esgotar o FSR-FG; qualquer FG em 3x a 30 fps.

---

## Fontes

Projeto (leitura local): `amd\main.cpp:865-870, 935-997, 2597, 5073`; `amd\addon\dlss5_bridge_addon.cpp:1-40, 534, 607, 924-942`; `amd\reshade_bridge.h`; `amd\addon\include\reshade\reshade_events.hpp:1666-1761`; `amd\feeder-amd\src-feeder\README.md:118-200, 1000-1030, 1078-1100`; `amd\feeder-amd\src-feeder\PLAN-SMOOTHMOTION.md`; `amd\feeder-amd\src-feeder\PLAN-PROXY-SWAPCHAIN.md`; `amd\feeder-amd\src-feeder\src\dlss5-feed.cpp:636-740, 2616-2690, 5060+`; `amd\feeder-amd\src-feeder\src\version.rc`; `amd\feeder-amd\src-amd\feed_amd.h:9-38, 204-230, 317-345`; `amd\feeder-amd\ffx-bin\`, `amd\feeder-amd\dist\`; `amd\dlss5-feed.log:1`; `amd\docs\latencia.md`; `amd\docs\supersampling.md` §4; `amd\docs\viabilidade-framegen-upscaling.md`; `RELATORIO-3X.md`; `RELATORIO-DESEMPENHO.md` §a.3.

NVIDIA:
- [Streamline — ProgrammingGuideDLSS_G.md](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md) · [changelog.txt](https://raw.githubusercontent.com/NVIDIA-RTX/Streamline/main/changelog.txt) · [ProgrammingGuideReflex.md](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideReflex.md) · [license.txt](https://raw.githubusercontent.com/NVIDIA-RTX/Streamline/main/license.txt)
- [Fórum NVIDIA — "Frame Generation Without Streamline" (18/10/2023)](https://forums.developer.nvidia.com/t/frame-generation-without-streamline/269756)
- [NGX EULA](https://docs.nvidia.com/ngx/latest/ngx-eula/index.html)
- [NVIDIA app: Smooth Motion para RTX 40](https://www.nvidia.com/en-us/geforce/news/nvidia-app-global-dlss-overrides-rtx-40-series-smooth-motion/) · [Driver 590.26 preview (VideoCardz)](https://videocardz.com/newz/nvidia-590-26-preview-drivers-introduce-smooth-motion-frame-generation-for-geforce-rtx-40-series) · [README Linux cap. 39 — VK_LAYER_NV_present](https://download.nvidia.com/XFree86/Linux-x86_64/580.126.18/README/nvpresent.html)
- [NvOFFRUC Programming Guide (Optical Flow SDK 5.0)](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvfruc-programming-guide/index.html) · [NVOFA FRUC (PDF)](https://docs.nvidia.com/video-technologies/optical-flow-sdk/pdf/NVOFA_FRUC.pdf) · [Optical Flow SDK](https://developer.nvidia.com/optical-flow-sdk)
- [NVAPI — grupo DX (DRS e Reflex)](https://docs.nvidia.com/nvapi/group__dx.html)

AMD:
- [FSR 3.1.4 — Upscaling and Frame Generation (manual)](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/) · [Frame Interpolation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation/) · [Frame Interpolation Swapchain](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation-swap-chain/) · [Migrating to FSR 3.1 (prepare pass)](https://gpuopen.com/manuals/fidelityfx_sdk/getting-started/migrating-to-fsr-3-1/) · [FSR Frame Generation API (SDK 2.x)](https://gpuopen.com/manuals/fsr_sdk/techniques/frame-interpolation-api/) · [AMD FSR Frame Generation](https://gpuopen.com/amd-fsr-framegeneration/) · [FSR 3 — detalhes técnicos (optical flow em HLSL, async)](https://gpuopen.com/news/fsr3-in-games-technical-details/) · [FidelityFX SDK releases](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases) · [FSR SDK 2.3 (FSR 4.1 em RDNA 3)](https://gpuopen.com/learn/amd-fsr-sdk-2-3-blog/)
- [AFMF 2 — release notes do preview](https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-AFMF2-TECH-Preview.html) · [AFMF 2 — Tom's Hardware (−28 % latência)](https://www.tomshardware.com/pc-components/gpus/amd-fluid-motion-frames-2-lowers-latency-by-28-percent) · [AFMF 2.1 — itechguides](https://www.itechguides.com/amd-afmf-2-1-is-now-available-what-improved-which-gpus-support-it-and-how-it-compares-with-nvidia-smooth-motion/) · [AFMF em vídeo (VideoCardz)](https://videocardz.com/newz/amd-fluid-motion-frames-can-be-used-for-videos-a-return-of-fluid-motion-video) · [guru3D — AFMF for video (PotPlayer/VLC)](https://forums.guru3d.com/threads/discover-amd-fluid-motion-frames-for-video.449942/)

Projetos abertos:
- [OptiScaler — FG_Hooks.cpp](https://github.com/optiscaler/OptiScaler/blob/master/OptiScaler/hooks/FG_Hooks.cpp) · [DLSSG_Dx12.cpp](https://github.com/optiscaler/OptiScaler/blob/master/OptiScaler/framegen/dlssg/DLSSG_Dx12.cpp) · [FSRFG_Dx12.cpp](https://github.com/optiscaler/OptiScaler/blob/master/OptiScaler/framegen/ffx/FSRFG_Dx12.cpp) · [Wiki: Frame Generation Options](https://github.com/optiscaler/OptiScaler/wiki/Frame-Generation-Options) · [Issue #1110 — OptiFG para DX11/Vulkan](https://github.com/optiscaler/OptiScaler/issues/1110) · [optiscaler/FidelityFX-SDK-DX11](https://github.com/optiscaler/FidelityFX-SDK-DX11)
- [Nukem9/dlssg-to-fsr3 — FFFrameInterpolator.cpp](https://github.com/Nukem9/dlssg-to-fsr3/blob/master/source/maindll/FFFrameInterpolator.cpp) · [README](https://github.com/Nukem9/dlssg-to-fsr3)
- [DLSS5-Feeder (upstream)](https://github.com/jlrouzies-fr/DLSS5-Feeder) · [Orbmu2k/nvidiaProfileInspector](https://github.com/Orbmu2k/nvidiaProfileInspector)
- [mpv issue #17140 — Smooth Motion não funciona com o mpv](https://github.com/mpv-player/mpv/issues/17140) · [guru3D — driver 590.10, Feature Flags / mpc-hc](https://forums.guru3d.com/threads/geforce-590-10-sm-6-9-preview-driver.456559/page-11)
- [FFmpeg `fruc_vulkan` via VK_NV_optical_flow (Phoronix, 29/08/2026)](https://www.phoronix.com/news/FFmpeg-NVIDIA-RTX-FRUC) · [XenoSpectrum — licença do SDK contornada](https://xenospectrum.com/en/ffmpeg-vulkan-fruc-frame-interpolation/)
- [Special K — Streamline Compatibility](https://wiki.special-k.info/en/Compatibility/Streamline) (gerencia pacing/Reflex em jogos que já têm DLSS-G; não injeta FG em swapchain arbitrária)

Medições de terceiros:
- [Tom's Hardware — LSFG 3/3.1: 28,47 → 41,70 ms a 40→80 fps; −40 % GPU](https://www.tomshardware.com/video-games/pc-gaming/lossless-scaling-3-update-touts-greatly-improved-latency-and-performance-universal-frame-gen-tool-boasts-24-percent-reduced-latency) · [Lossless Scaling — guia 2026 (base mínima 30/40/60 fps, travar fps para evitar 100 % GPU)](https://tech-insider.org/lossless-scaling-setup-2026/)
- [gamegpu — DLSS 5 + Lossless Scaling numa RTX 4080: ≤ 30 fps, ghosting, quedas](https://en.gamegpu.com/news/zhelezo/vnedrenie-tekhnologii-dlss-5-v-lossless-scaling-vyzvalo-propusk-kadrov-na-rtx-4080)
- [Hardware Times — Smooth Motion na RTX 4090 (+40–70 %)](https://hardwaretimes.com/nvidia-smooth-motion-almost-doubles-fps-on-rtx-40-gpus-frame-generation-benchmark-tests/) · [PC Gamer — Smooth Motion na RTX 40 (+44 %)](https://www.pcgamer.com/hardware/graphics-cards/rtx-40-series-graphics-cards-can-now-enable-frame-generation-in-unsupported-games-via-the-drivers-up-to-a-44-percent-fps-improvement-in-my-initial-tests/)
- [DLSS-G latência em Cyberpunk 47 → 63 ms (Hardware Unboxed via digitalphablet)](https://www.digitalphablet.com/news/frame-gen-actually-improves-latency/) · [TechSpot — DLSS 4 MFG review](https://www.techspot.com/article/2945-nvidia-dlss-4/) · [wccftech — como usar FG (pisos de fps por fornecedor)](https://wccftech.com/how-to/how-to-use-nvidia-dlss-amd-fsr-intel-xess-frame-generation-technologies/)
