# Cor, HDR e filtros complementares ao DLSS 5

Levantamento do que dá para somar à cadeia `LumeniteFX Kernel → DLSS5_Feed → renodx-dlss5`
para melhorar imagem e personalizar cor, feito lendo `ReShade.ini`, `FafaDLSS5.ini`,
`dlss5-feed.cfg`, os logs, os `.fx` instalados, `main.cpp`, `screen_capture.h` e as strings
dos dois add-ons. **Nada foi executado** — o app abre janelas e o PC está em uso.

Estado da instalação analisada: ReShade 6.8.0.2155, `dlss5-feed.addon64` 0.7.0,
`renodx-dlss5.addon64` v0.2026.828.517 (RenoDX DLSS5 Generic v4.1.5), RTX 4080 driver 596.49,
backbuffer 3420×1367 `DXGI_FORMAT_B8G8R8A8_UNORM`.

---

## 1. Inventário

### O que está instalado

`reshade-shaders\Shaders\`:

| Arquivo | O que é |
|---|---|
| `DLSS5_Feed.fx` | prepara motion vectors + depth + máscara de confiança para o add-on |
| `lumenite_Kernel.fx` | optical flow piramidal + normais reconstruídas — é o provedor de MV (`DLSS5_MV_PROVIDER=3`) |
| `lumenite_QuantMotion.fx` | corte leve do Kernel (seria `DLSS5_MV_PROVIDER=4`) |
| `lumenite_TRAA.fx` | anti-aliasing temporal |
| `lumenite_AnamorphicBloom.fx` | bloom anamórfico |
| `lumenite_LSAO.fx`, `lumenite_QuantAO.fx`, `lumenite_RTAO.fx` | ambient occlusion |
| `lumenite_SSSR.fx` | reflexos em espaço de tela |
| `ReShade.fxh`, `ReShadeUI.fxh`, `DrawText.fxh` | headers |
| `include\lumenite_*.fxh` | ColorManagement, Compute, Helpers, Projections |
| **`DLSS5_Grade.fx`** | **nosso**, adicionado por este trabalho — ver §5 |

`reshade-shaders\Textures\`: `lumenite_bluenoise256.png` e **`dlss5_grade_luts.png`** (nosso, §6).

Antes deste trabalho **não havia nenhum shader de cor**. `LUT.fx` e `MultiLUT.fx` não estão
instalados e continuam não estando — o `DLSS5_Grade.fx` não depende deles.

### Ativo no preset atual (`FafaDLSS5.ini`)

`Lumenite_Kernel` → `DLSS5_Feed`, nessa ordem. Correto.

### O que o RenoDX expõe

Chaves lidas do binário: `NRIntensity`, `NRLocalTone`, `NRLocalStructure`, `NRSkinStructure`,
`NRColorStrength`, `NRTransferStrength`, `NRPaperWhiteScale`, `NRPreset`, `NRStyle`,
`NRUICorrection`, `NRAutoMask`, `NREnableUpscaling`, `NRDepthMode`, `NRMVecScaleX/Y`,
`NRToggleKey`, `NRScreenshotKey`. Estilos: `Natural`, `Cinematic`, `Default`. Presets de rede:
`Preset #1/#2/#3`.

### O que o Feeder expõe

`dlss5-feed.cfg`: `enabled`, `mode`, `hdr` (−1 = auto), `depth_inverted`, `flags`,
`reset_every`, `warmup_rebuild`, `rebuild`, `log_frames`, `create_delay`, `preset`,
`work_resolution`, `mv_scale_x/y`. Formatos de backbuffer aceitos, lidos do binário:
`B8G8R8A8_UNORM`, `R8G8B8A8_UNORM_SRGB`, `B8G8R8A8_UNORM_SRGB`, **`R10G10B10A2_UNORM`**,
**`R16G16B16A16_FLOAT`**.

---

## 2. Achado: a profundidade é constante, e isso mata metade do LumeniteFX

`main.cpp:347` desenha a grade de quads com

```
o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.5, 1.0);
```

`z = 0.5` em **todo** vértice. O depth stencil é real (`D24_UNORM_S8_UINT`, `main.cpp:392`) e a
grade 32×18 existe só para dar contagem de vértices às heurísticas do Generic Depth
(`main.cpp:47-48`), mas o conteúdo é um plano.

`include\lumenite_Helpers.fxh:61` define `GetDepth` como `ReShade::GetLinearizedDepth`, então
todo consumidor de profundidade recebe o mesmo valor em cada pixel. Consequências:

- **`lumenite_LSAO`, `lumenite_QuantAO`, `lumenite_RTAO`, `lumenite_SSSR` não produzem imagem.**
  As normais reconstruídas (`lumenite_Kernel.fx:420-429`) saem todas planas, o gradiente de
  profundidade é zero e o AO fica uniforme. Só custam frames. Mantenha desligados.
- **`GEOM_ENABLE` do `DLSS5_Feed` também não serve.** O modelo de câmera do Feed ajusta um termo
  de paralaxe por profundidade inversa; com profundidade constante o termo é degenerado.
  Já está em `0` no preset — está certo.
- **`VALIDATE_DEPTH` do Feed é um no-op.** O teste compara a profundidade linear reprojetada com
  a atual; ambas constantes, nunca reprova nada.
- **O gate de descontinuidade do filtro à-trous do Kernel está desligado sem ninguém saber.**
  Ver §3.

---

## 3. `IMAGE_SPACE=1`: o ganho grátis

`lumenite_Kernel.fx:334-360`, dentro de `ATrousFilter`:

```
#if IMAGE_SPACE == 0
    float  sampleDepth  = tex2Dlod(sDepth, float4(sampleUV, 0, mip)).r;
    float absDepthDiff  = abs(centerDepth - sampleDepth);
    float depthWeight   = (absDepthDiff < 0.003) ? 1.0 : 0.0;
    discontinuityGate = depthWeight;
#else
    float2 flowDeltaPx = (sampleFlow - centerFlow) * BUFFER_SCREEN_SIZE;
    float rawMotionGate = exp2(-dot(flowDeltaPx, flowDeltaPx) / (0.01 + EPSILON));
    float motionGate = lerp(1.0, rawMotionGate, saturate(centerConf));
    discontinuityGate = motionGate;
#endif
```

Com o padrão (`IMAGE_SPACE=0`) e a profundidade plana, `absDepthDiff` é sempre 0, logo
`depthWeight` é sempre 1: **o gate está permanentemente aberto** e o filtro borra os motion
vectors por cima das silhuetas dos objetos em movimento. É ghosting fabricado.

`IMAGE_SPACE=1` troca o gate por discordância de fluxo — que funciona em vídeo 2D — e, de
quebra, `lumenite_Kernel.fx:723` deixa de compilar o pass de normais (MRT `RGBA16F` + `R16F`
em resolução cheia, inútil aqui). **Ganho de imagem e de performance ao mesmo tempo.**

Ressalva honesta: o gate novo é apertado (divisor `0.01`, ou seja qualquer diferença acima de
~0.1 px zera o peso), suavizada pelo `lerp(1.0, ..., centerConf)`. Em cena de baixa confiança o
fluxo pode ficar mais granulado. Vale um A/B com a técnica `DLSS5_Feed_Debug` na vista de
motion vectors.

`IMAGE_SPACE` é definição de pré-processador — vai em `PreprocessorDefinitions`, junto do
`DLSS5_MV_PROVIDER=3`.

---

## 4. Ordem das técnicas: os três slots

O add-on `dlss5-feed` dispara DLSS + Neural Rendering **logo depois** que a técnica
`DLSS5_Feed` renderiza (está escrito no cabeçalho do próprio `DLSS5_Feed.fx`: *"anything placed
below it in the list is applied on top of the neural output"*). Isso cria três posições:

| Slot | Posição na lista | O efeito enxerga | O que ele contamina |
|---|---|---|---|
| **A** | acima de `Lumenite_Kernel` | frame cru da captura | o optical flow do Kernel **e** a cor que o NR recebe |
| **B** | entre Kernel e Feed | frame cru | só a cor do NR, mas dessincroniza a validação por luma do Feed, que reamostra o backbuffer no pass dela |
| **C** | abaixo de `DLSS5_Feed` | saída neural pronta | nada |

Regras que saem daí:

- **Nitidez, grão, dither, bloom, qualquer coisa que injete alta frequência: só em C.**
  Em A o Kernel casa o ruído em vez do conteúdo (o fluxo vira lixo) e o DLSS trata o grão como
  detalhe real, acumulando-o no histórico temporal.
- **Color grading: C por padrão.** São transformações suaves e monotônicas; em C o NR vê o sinal
  display-referred sRGB que ele espera e você grada o resultado. É por isso que o
  `DLSS5_Grade.fx` traz o aviso no cabeçalho e no tooltip da técnica.
  Em A o grade vira literalmente o `original` do codec do RenoDX: uma S-curve forte esmaga preto
  e estoura branco **antes** do optical flow, e contraste é exatamente o que o Kernel usa para
  casar pixels — motion vector pior, mais fantasma. Só se justifica com curva suave e monotônica
  quando a captura está tão mal exposta que a própria rede lê errado.
- **B praticamente nunca.** Não há ganho que compense dessincronizar o Feed do Kernel.
- **Anti-aliasing extra (TRAA, FXAA, SMAA, CMAA): não.** DLAA já é o AA. `Lumenite_TRAA` acima do
  Feed é um segundo acumulador temporal brigando com o do DLSS; abaixo é ghosting sobre ghosting.

Meio-termo se você quiser o look influenciando a reconstrução sem estragar o fluxo: deixe o
grade em C e baixe `NRColorStrength` (§7) — o NR devolve luminância neural sobre a cor que o
grade definiu.

---

## 5. `DLSS5_Grade.fx` — grading nosso, sem dependência externa

`reshade-shaders\Shaders\DLSS5_Grade.fx`. Licença MIT (a mesma do app); código original, sem
derivação de shader de terceiro; a única dependência é o `ReShade.fxh` do próprio ReShade (CC0).

Um passe, técnica `DLSS5_Grade`, rótulo *"DLSS 5 Grade (cor -- ponha ABAIXO do DLSS 5 Feed)"*.
O cabeçalho do arquivo e o tooltip da técnica repetem o aviso de ordem, para não ser instalado
no slot errado.

Estágios, nesta ordem:

| # | Estágio | Espaço | Uniforms |
|---|---|---|---|
| 1 | Exposição + temperatura/tint | luz **linear** (correção de cena) | `Exposure` (stops), `Temperature` (frio↔quente), `Tint` (verde↔magenta) |
| 2 | Lift / Gamma / Gain | exibição (criativo) | `Lift`, `Gamma`, `Gain` — float3 cada |
| 3 | Contraste | só na luma, matiz preservado | `SCurve`, `Contrast`, `ContrastPivot` |
| 4 | Vibrance / saturação | exibição | `Vibrance`, `Saturation` |
| 5 | LUT 3D opcional | exibição | `LUT_SELECT`, `LUT_Strength`, `LUT_AmountLuma`, `LUT_AmountChroma` |

Detalhes que importam:

- **Identidade nos padrões.** Cada estágio é guardado por um teste de "está fora do neutro?", então
  com os valores de fábrica a imagem passa intacta e o custo é um punhado de comparações.
- **O balanço de branco repõe a luminância.** Mudar a temperatura muda a cor, não a exposição.
- **Contraste e S-curve agem só na luma** e a razão volta ao RGB — o matiz não se mexe.
- **Vibrance usa saturação HSV** (`(max-min)/max`): satura mais o que está fosco e poupa o que já
  está saturado. É o jeito seguro de dar vida sem estourar pele e céu.
- **O estágio de LUT separa luma e croma sem `normalize()`.** O `LUT.fx` clássico do ReShade usa
  `normalize(color)`, que é NaN em pixel preto; aqui é `dot` com os pesos BT.709 e dois `lerp`.
- **`DLSS5_GRADE_LUT=0`** nas `PreprocessorDefinitions` remove o estágio de LUT inteiro, caso a
  textura suma.

Custo: um passe fullscreen, ~40 ALU e 2 fetches quando a LUT está ligada. Em 1080p é ruído
estatístico ao lado do Neural Rendering.

---

## 6. As LUTs — nossas, geradas por fórmula

`gerar-luts.ps1`, na raiz do projeto. PowerShell + `Add-Type` (o laço de 1,3 milhão de pixels
fica em C#, porque em PowerShell puro levaria minutos) + `System.Drawing` para o PNG. Nenhum
toolchain extra.

```
powershell -ExecutionPolicy Bypass -File .\gerar-luts.ps1
powershell -ExecutionPolicy Bypass -File .\gerar-luts.ps1 -Strips   # strips avulsos também
```

Saída: `reshade-shaders\Textures\dlss5_grade_luts.png`, atlas 4096×320.

**Layout (definido por nós, é o que o `DLSS5_Grade.fx` espera):** strip 3D→2D com as LUTs
empilhadas. Largura `N*N`, altura `N * QUANTAS`, com `N = 64`. O pixel `(t*N + x, k*N + y)`
guarda a cor graduada da entrada `RGB = (x/(N-1), y/(N-1), t/(N-1))` pela LUT número `k`. O azul
indexa a fatia e o shader interpola linearmente entre fatias vizinhas; o filtro bilinear da
textura cobre vermelho e verde. Como o mapeamento usa `(c*(N-1)+0.5)`, as bordas caem no centro
do texel — não há vazamento entre as LUTs empilhadas.

Escolhi `N = 64` (e não os 32 habituais) porque o sinal é de 8 bits e gradiente liso é
exatamente onde LUT pequena aparece. Custa 3,6 MB de PNG.

As formulas em `gerar-luts.ps1` são **as mesmas** que o `DLSS5_Grade.fx` aplica ao vivo:
temperatura em luz linear com luminância reposta, lift/gain/gamma, S-curve na luma,
vibrance/saturação. Editar a lista `$LUTS` no topo do script e regerar é o fluxo previsto.

| # no combo | Nome | Quando usar |
|---|---|---|
| 0 | Desligada | pula o estágio |
| 1 | **Neutra (controle)** | identidade byte a byte. Ligue para provar que o estágio de LUT não mexe em cor sozinho; é também a base para editar |
| 2 | **Quente cinematográfica** | *teal & orange* suave: sombras levemente ciano, altas quentes, S-curve leve, +vibrance. O look "filme"; bom em pele e interiores |
| 3 | **Fria noturna** | ~7500 K, preto levantado de leve, saturação −6%. Cena noturna, sci-fi, ou fonte quente demais |
| 4 | **Contraste alto** | S-curve forte na luma, matiz preservado. Para captura lavada — típico de janela HDR que o WGC entregou tone-mapada |
| 5 | **Saturação suave** | só vibrance. Para vídeo/emulador fosco, sem mexer em contraste |

O script termina verificando que a LUT neutra é identidade byte a byte e imprime
`verificacao: LUT neutra e identidade exata (erro 0/255)`. Passou.

---

## 7. Cor e HDR: o que dá hoje sem tocar em código

### 7.1 Os dois sliders do RenoDX que ninguém está usando

O codec do `renodx-dlss5.addon64` traz o HLSL embutido no binário. Reconstruído das strings:

```hlsl
float3 UpgradeToneMap(float3 original, float3 proxy, float3 neural) {
  const float neural_y = Luminance(neural);
  if (neural_y <= 1e-5) return original;
  const float original_y = Luminance(original);
  const float proxy_y    = Luminance(proxy);
  float ratio;
  if (original_y < proxy_y) {
    ratio = original_y / max(proxy_y, 1e-6);
  } else {
    const float new_y = neural_y + max(0.0, original_y - proxy_y);
    ratio = new_y / neural_y;
  }
  const float3 scaled = HueOkLab(neural * ratio, neural);
  return lerp(original, scaled, TransferStrength);
}

// ... e no estágio final:
upgraded = UpgradeToneMap(original, proxy, neural);
const float  ratio          = original_y == 0.0 ? 1.0 : clamp(upgraded_y / original_y, 0.0, 4.0);
const float3 luminance_only = original * ratio;
const float3 result         = lerp(luminance_only, upgraded, ColorStrength);
```

O `HdrMode` vem do que o NGX marca como HDR (`Codec: FP16 working surface; HDR soft-clip/sRGB
only when NGX marks HDR`), e o decode tem três ramos:

| `HdrMode` | Formato | `original` |
|---|---|---|
| 0 | SDR | `source.rgb` — passa direto |
| 1 | scRGB / FP16 linear | `max(source.rgb, 0) / PaperWhiteScale` |
| 2 | PQ / `R10G10B10A2` | `PQToLinear(max(source.rgb, 0))` |

**No nosso caso `HdrMode == 0`.** Aí `original == proxy`, logo `original_y == proxy_y`, cai no
ramo `else`, `new_y = neural_y`, `ratio = 1`, `scaled = neural`. O `UpgradeToneMap` colapsa em
`lerp(original, neural, TransferStrength)`. Consequências práticas:

- **`NRTransferStrength`** (rótulo *HDR Transfer Strength*; padrão 1.0, e **está ausente do
  `ReShade.ini`**, o log confirma `transfer=1.000000`) é, em SDR, a **opacidade do efeito neural
  inteiro**. 0.6–0.8 dá um NR discreto sem desligá-lo — algo que hoje só existe como liga/desliga
  no F6.
- **`NRColorStrength`** (hoje `1`) decide **quanto o NR pode repintar a cor**. Em 0, o resultado é
  `original * ratio`: a cor original com a luminância neural. Em 1, cor e matiz vêm da rede.
  É o knob de personalização de cor que já existe, custo zero.
- **`NRPaperWhiteScale=16` não faz absolutamente nada em SDR** — só é lido em `HdrMode == 1`.
  O log mostra `paper_white=16.000000` e isso é inócuo hoje.

Ajuste pela overlay (*Add-ons → DLSS 5 Neural Rendering*; `AutoSavePreset=1` grava sozinho) ou
nas chaves da seção `[RenoDX.DLSS5]` do `ReShade.ini`.

### 7.2 Preset da rede DLSS

`dlss5-feed.cfg` tem `preset=0`. A dica do próprio add-on, lida do binário:

> *Presets differ in how hard DLSS clamps history against the current frame. If motion warps
> around transparents (dust, smoke, flames), try E or F.*

Com motion vectors de optical flow isso acontece o tempo todo. Escolha pelo combo
*DLSS render preset* na aba do Feeder — pelo combo, não pelo número, para não errar o enum NGX.

---

## 8. HDR de verdade: o que exigiria mudar código

### O estado atual é 8 bits ponta a ponta

| Etapa | Formato | Onde |
|---|---|---|
| frame pool do WGC | `DirectXPixelFormat_B8G8R8A8UIntNormalized` | `screen_capture.h:394` (`Recreate`) e `:457` (`CreateFreeThreaded`) |
| textura intermediária | `DXGI_FORMAT_B8G8R8A8_UNORM` | `screen_capture.h:384` |
| textura de upload da câmera | `DXGI_FORMAT_B8G8R8A8_UNORM` | `main.cpp:400` |
| swapchain | `DXGI_FORMAT_B8G8R8A8_UNORM` | `main.cpp:419`, confirmado no `ReShade.log` |

**O que o WGC entrega:** com o frame pool em BGRA8, uma janela HDR chega já **tone-mapada para
SDR pelo compositor** — a informação se perdeu antes de o ReShade ver o frame. Para capturar HDR
sem clipar é preciso criar o pool em `R16G16B16A16Float`; nesse formato o WGC devolve **scRGB
linear**, onde 1.0 = 80 nits.

### Diff literal — **NÃO APLICADO**

```diff
--- a/screen_capture.h
+++ b/screen_capture.h
@@ -384
-        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
+        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.SampleDesc.Count = 1;
@@ -394
-        if (FAILED(pool->Recreate(rtDev, WGC::WGDX::DirectXPixelFormat_B8G8R8A8UIntNormalized, optPoolBuf, sz))) return false;
+        if (FAILED(pool->Recreate(rtDev, WGC::WGDX::DirectXPixelFormat_R16G16B16A16Float, optPoolBuf, sz))) return false;
@@ -457
-        hr = st->CreateFreeThreaded(rtDev, WGC::WGDX::DirectXPixelFormat_B8G8R8A8UIntNormalized, optPoolBuf, isz, &pool);
+        hr = st->CreateFreeThreaded(rtDev, WGC::WGDX::DirectXPixelFormat_R16G16B16A16Float, optPoolBuf, isz, &pool);
```

```diff
--- a/main.cpp
+++ b/main.cpp
@@ -400
-        ft.Format = DXGI_FORMAT_B8G8R8A8_UNORM; ft.SampleDesc.Count = 1;
+        ft.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; ft.SampleDesc.Count = 1;
@@ -419
-        sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
+        sd.BufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
```

E, logo depois do `if (FAILED(hr)) { *err = L"D3D11CreateDeviceAndSwapChain falhou " + Hex(hr); return false; }`
(por volta da linha 432 de `main.cpp`), inserir a declaração do color space:

```cpp
        // scRGB: 1.0 = 80 nits, linear, primarias BT.709. So existe no modelo flip.
        IDXGISwapChain3* sc3 = nullptr;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3)) && sc3) {
            UINT sup = 0;
            if (SUCCEEDED(sc3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &sup)) &&
                (sup & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
                sc3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
                Log("swapchain scRGB (R16G16B16A16_FLOAT) ativo");
            } else {
                Log("display sem scRGB; swapchain FP16 sera apresentado como SDR");
            }
            sc3->Release();
        }
```

Cuidados obrigatórios:

1. **O fallback `DXGI_SWAP_EFFECT_DISCARD`** (`main.cpp:429`) tem de continuar em
   `B8G8R8A8_UNORM` — FP16 só existe no modelo flip.
2. `ResizeBuffers` já usa `DXGI_FORMAT_UNKNOWN` (`main.cpp:473`), que preserva o formato. Nada a
   fazer ali.
3. `dlss5-feed.cfg` tem `hdr=-1` (auto). Se o auto errar, force `hdr=1`.
4. O `PS` do app devolve `float4(tex.Sample(...).rgb, 1.0)` (`main.cpp:350`) — passa valores
   acima de 1.0 sem clipar, então serve para scRGB como está.

### Vale a pena?

**Só se a fonte capturada for HDR de verdade.** Com jogo SDR o ganho é apenas precisão (sem
banding ao longo da cadeia de passes), e isso sai muito mais barato com o estágio de contraste
do `DLSS5_Grade.fx` bem ajustado ou um deband. Some a isso que o `nvngx_dlssnr.dll` é build
vazado — o próprio log avisa
`custom runtime accepted; untested build, NR failures may be specific to it` — e que
`R10G10B10A2_UNORM` (10 bits, ainda SDR) é um caminho arriscado: o Feeder aceita o formato, mas
o auto-detect de HDR pode marcá-lo como PQ e aplicar linearização de PQ num sinal gamma 2.2,
destruindo a cor. Se for testar 10 bits, force `hdr=0`.

---

## 9. Recomendações, em ordem de retorno

| # | O quê | Ganho | Custo |
|---|---|---|---|
| 1 | `IMAGE_SPACE=1` nas `PreprocessorDefinitions` | **o maior de todos, e é grátis.** Reativa o gate de descontinuidade do filtro de motion vectors, que hoje está permanentemente aberto por causa da profundidade plana (§3) | **negativo** — elimina o pass de normais |
| 2 | `VALIDATE_LUMA=1`, `VALIDATE_DEPTH=0` no `DLSS5_Feed` | o teste de profundidade nunca reprova nada; o de luma é *mask only* e é exatamente o que pega fogo, partícula e HUD piscando — estava desligado | ~zero |
| 3 | `NRColorStrength` 0.4–0.6 e `NRTransferStrength` 0.8 | personalização de cor e dosagem do NR sem shader nenhum (§7.1) | zero |
| 4 | *DLSS render preset* E ou F no Feeder | menos warping em transparentes, que é o ponto fraco de MV por optical flow | zero |
| 5 | `DLSS5_Grade` abaixo do Feed, LUT *Quente cinematográfica*, peso de luma 0.7 | look pessoal sem achatar o contraste que o NR criou | ~0.1 ms em 1080p |
| 6 | `DLSS5_Grade` com S-curve 0.10 + vibrance 0.10, LUT desligada | correção sóbria para fonte lavada, sem estilizar | ~0.05 ms |

**Não vale:** `Lumenite_TRAA` (briga com o DLAA), qualquer AO ou SSSR (profundidade plana, §2),
`AnamorphicBloom` (enfeite; já está em intensidade 0), sharpening extra (o NR já tem *Local
Structure Strength*, e o app desenha 1:1 sem reescala — não há blur de rescale para compensar).

---

## 10. Arquivos entregues

Nenhum arquivo pré-existente foi modificado. `main.cpp`, `screen_capture.h`, `ReShade.ini` e
`FafaDLSS5.ini` continuam intactos.

| Arquivo | O quê |
|---|---|
| `reshade-shaders\Shaders\DLSS5_Grade.fx` | o shader de grading (§5) |
| `reshade-shaders\Textures\dlss5_grade_luts.png` | atlas 4096×320 com as 5 LUTs (§6) |
| `gerar-luts.ps1` | gerador das LUTs, versionado (§6) |
| `preset-01-nucleo-afinado.ini` | Kernel → Feed, com `IMAGE_SPACE=1` e a validação reajustada. Zero extras |
| `preset-02-grade-neutra.ini` | 01 + `DLSS5_Grade` com S-curve 0.10 e vibrance 0.10, LUT desligada |
| `preset-03-grade-cinema.ini` | 01 + `DLSS5_Grade` com a LUT *Quente cinematográfica*, peso de luma 0.7 |
| `docs\cor-hdr-e-filtros.md` | este documento |

Nos três presets a ordem em `Techniques` e `TechniqueSorting` já é
`Lumenite_Kernel → DLSS5_Feed → DLSS5_Grade`.

---

## 11. O que não foi verificado

- **O `DLSS5_Grade.fx` não foi compilado pelo ReShade.** Não rodei o app. O que fiz foi extrair o
  corpo HLSL do arquivo, adaptar os *wrappers* de textura e compilar com o `D3DCompile` do
  `d3dcompiler_47.dll` em `ps_5_0`: **compilou limpo, sem avisos**. Isso cobre expressões,
  intrínsecas e conversões de tipo — o risco real. **Não cobre** a camada ReShade FX:
  anotações `ui_*`, o bloco `texture { source = ...; }`, o bloco `sampler` e o `technique`.
  Esses foram escritos seguindo o que `ReShade.fxh` e `DLSS5_Feed.fx` já usam nesta instalação.
- **O layout do atlas foi verificado numericamente, não visualmente.** Reproduzi em PowerShell
  exatamente a conta de indexação do shader
  (`uv.x = (r*(N-1)+0.5)/(N*N) + tile/N`, `uv.y = (g*(N-1)+0.5 + row*N)/(N*NL)`) e li os texels do
  atlas: batem com a grade esperada, a linha neutra é identidade e as demais se comportam como
  desenhadas (pele `(210,162,138)` → quente `(222,163,131)`, fria `(206,168,153)`,
  saturação-suave `(217,161,132)` com a luma preservada).
- **O ganho de `IMAGE_SPACE=1` é dedução de leitura do código, não medição.** A conclusão de que o
  gate está aberto é certa; o quanto isso melhora na tela depende da cena.
