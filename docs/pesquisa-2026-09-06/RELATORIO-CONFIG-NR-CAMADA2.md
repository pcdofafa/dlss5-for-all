# Varredura das configurações do Neural Rendering na segunda camada

**Data:** 6 de setembro de 2026 · máquina do usuário, Windows 11 26200, RTX 4080, driver 596.49
**Pergunta:** "testar as configurações do DLSS Neural dentro do ReShade de forma que a gente
consiga aplicar o DLSS 5 na sua totalidade mas diminua o borrão na segunda camada."

> **Este arquivo está sendo escrito enquanto a varredura roda.** As seções 1 a 3 (instrumento,
> inventário de chaves e armadilhas) estão fechadas. As de resultado são preenchidas ao fim.

---

## 1. O que este trabalho acrescenta ao anterior

O `scratchpad\camada3x\RELATORIO-BORRAO.md` mediu doze combinações escolhidas à mão, em **duas**
fontes separadas: formas em movimento caótico (custo = véu) e uma foto em vai-e-vem (benefício =
detalhe). Nenhuma configuração foi vista nas duas ao mesmo tempo, e faltava a medida decisiva:
**quanta força o efeito ainda tem**. Sem ela, qualquer busca automática converge para a
configuração que simplesmente **desliga** o Neural Rendering — véu mínimo, borrão mínimo, efeito
nenhum.

Este trabalho traz três coisas novas:

1. **uma fonte só**, com uma faixa em movimento caótico e uma faixa perfeitamente estática no
   mesmo quadro;
2. **uma métrica de força do efeito** com piso de ruído medido;
3. **uma varredura sistemática** de todas as chaves com efeito, e não de doze pontos escolhidos.

---

## 2. Instrumento

### 2.1 A fonte

`scratchpad\config-nr\fonte-mista.ps1` — janela de 1728×1007 px com três faixas, delimitadas por
linhas marcadoras de cor pura que o medidor reencontra sozinho:

| faixa | conteúdo | o que mede |
|---|---|---|
| carimbo (topo) | dois marcadores brancos + 12 bits do número do quadro | atraso fonte→tela, quadros repetidos e pulados |
| **caos** (linha azul em volta) | 28 formas de alto contraste sobre preto, 600–2000 px/s, virando de direção a cada 100–400 ms, seed fixa | **véu** e **nitidez** |
| **estática** (linha verde em volta) | recorte 1:1 de uma foto rica em alta frequência (62% da largura) + carta de linhas de 1, 2, 3 e 4 px | **detalhe** e **efeito** |

A faixa estática nunca se move — é isso que permite a métrica nova. O laço é compilado em C# com
`DispatcherTimer` de 1 ms e `timeBeginPeriod(1)`, o único arranjo que sustenta 60 fps nesta
máquina (memória `fonte-teste-60fps-windows`).

### 2.2 As métricas

| métrica | definição | fonte crua | camada 1 sozinha |
|---|---|---|---|
| **véu** | % de pixels com luma entre piso+12 e piso+70 na faixa de caos | **0,000%** | 0,60% |
| **nitidez** | \|dL/dx\| médio na faixa de caos | 1,33 | 1,49 |
| **detalhe (foto)** | laplaciano horizontal médio na imagem média de 24 capturas da faixa estática | 5,78 | 6,54 |
| **detalhe (carta)** | idem, na carta de linhas de 1 a 4 px | 167,2 | 128,5 |
| **efeito** | média \|L(camada 2) − L(camada 1)\| na faixa estática, com alinhamento de ±4 px | — | — |
| **tremor** | desvio-padrão temporal médio por pixel na faixa estática | 0,00 | 3,63 |

O **efeito** é a métrica nova: literalmente "quanto a segunda passada mexeu na imagem que
recebeu". O piso de ruído dela foi medido de duas formas independentes:

| controle | efeito medido |
|---|---|
| a camada 1 medida contra a própria referência da camada 1 | **1,040** |
| a camada 2 com o Feeder desligado (`[camada2_feed] enabled=0`) | **1,076** |

Abaixo de ~1,15 a segunda passada não está fazendo nada. Isso dá sentido a todo o resto da
tabela: uma configuração com efeito 4 está fazendo três vezes mais do que o ruído; uma com
efeito 10 está fazendo nove vezes.

### 2.3 Guardas contra medir a coisa errada

- **A camada de cima estava mesmo na tela?** `WindowFromPoint` não serve: a janela da camada é
  click-through e o ponto atravessa até a fonte. O que se checa é a lista de janelas de classe
  `FafaDLSS5CamWnd` dos nossos processos — visíveis e cobrindo a fonte. Somam-se `camada 2 ATIVA`
  no log do app, `evaluation succeeded` no `ReShade.log` e `delivered (` no `dlss5-feed.log`,
  contados por rodada.
- **A imagem congelou?** Uma rodada saiu com as 24 capturas mostrando o mesmo quadro e um atraso
  de 22 s — métricas perfeitamente estáveis e sem valor nenhum. Toda rodada com mais de 50% de
  quadros repetidos, razão de quadros abaixo de 0,6 ou fonte fora de 52–66 fps é descartada e
  refeita.
- **Arquivos limpos.** `ReShade.ini`, `FafaDLSS5.ini` e `dlss5-feed.cfg` são restaurados de uma
  base antes de cada variante, e o `dlss5forall.ini` é reescrito por inteiro — nada sobra da
  rodada anterior.

### 2.4 Três armadilhas de DPI que custaram a primeira leva de medições

Registradas porque quem repetir vai bater nelas de novo.

1. **Os dois monitores desta máquina têm escalas diferentes** — o primário a 125% e o segundo a
   150%. `GetDpiForMonitor` chamado de um processo apenas *system-aware* devolve 120 (125%) para
   os **dois**, então a diferença não aparece na consulta óbvia.
2. **Uma fonte de teste apenas *system-aware* na tela de 150% é virtualizada**: o Windows a
   desenha a 125% e a estica 1,2× com filtro bilinear. A carta de linhas borra, o preto vira luma
   intermediária e o **véu da fonte crua sai 1,6% em vez de 0,000%** — o piso da métrica principal
   fica quatro vezes maior que o sinal. Também desloca as faixas: o marcador azul esperado na
   linha 66 foi achado na 96, e o verde esperado na 610 foi achado na 748. A correção é
   `SetProcessDpiAwarenessContext(-4)` (per-monitor v2) **antes da primeira janela** — que é o que
   o próprio `DLSS5ForAll.exe` já faz (`amd\main.cpp:7154`).
3. **`SetProcessDPIAware()` depois de `Add-Type -AssemblyName System.Drawing` não pega.** No
   medidor, a chamada tem de vir antes de o GDI+ subir; senão a captura sai deslocada ~28 px à
   esquerda e ~38 acima, entra uma tira de área de trabalho e some a base da faixa estática.

E duas de WPF, pelo mesmo motivo:

- `ClipToBounds` num `Canvas` com a foto inteira posicionada em coordenada negativa **não é
  confiável**: ora recortava, ora não, e numa das capturas a foto cobriu a janela inteira. A foto
  entra agora já recortada (`CroppedBitmap`) no tamanho exato da faixa.
- `-WindowStyle Minimized` no `Start-Process` minimiza a **primeira janela do processo**, que é a
  do WPF (rect −25600,−25600), não o console.

---

## 3. Inventário completo das chaves

Levantado das strings de `renodx-dlss5.addon64` (build de 2 de setembro de 2026, "RenoDX DLSS5
Generic v4.7"), das strings de `dlss5-feed.addon64` ("DLSS 5 Feed 0.12.0") e do código dos
shaders em `reshade-shaders\Shaders\`.

### 3.1 `[RenoDX.DLSS5]` do `ReShade.ini`

| chave | rótulo na interface do add-on | valores | parâmetro que chega na rede |
|---|---|---|---|
| `NeuralUplift` | Enable DLSS Neural Rendering | 0/1 | `DLSSNR.Enabled` |
| `NREnableUpscaling` | Enable Upscaling (WIP) | 0/1 | `DLSSNR.Upscaling` |
| `NRPreset` | NR Preset: Default, Preset #1, #2, #3 | 0..3 | `DLSSNR.Hint.Render.Preset` |
| `NRStyle` | NR Style: Default, Natural, Cinematic | 0..2 | `DLSSNR.Style` |
| `NRIntensity` | Overall Intensity | 0..2 | `DLSSNR.Intensity` |
| `NRGlobalTone` | Global Tone Intensity | 0..2 | `DLSSNR.GlobalToneStrength` |
| `NRLocalTone` | Local Tone Intensity | 0..2 | `DLSSNR.LocalToneStrength` |
| `NRLocalStructure` | Structure Intensity | 0..2 | `DLSSNR.LocalStructureStrength` |
| `NRSkinStructure` | Character/Skin Structure | 0..2 | `DLSSNR.SkinStructureStrength` |
| `NRAutoMask` | Automatic / Character Mask | 0/1 | `DLSSNR.UseAutoMask` |
| `NRUICorrection` | NR UI Correction | 0/1 | `DLSSNR.UICorrection` |
| `NRDepthMode` | Depth Convention: usar a flag do jogo / forçar normal / forçar invertida | 0..2 | `DLSSNR.DepthInverted` |
| `NRMVecScaleX` / `NRMVecScaleY` | Motion Scale X/Y Multiplier | float | `DLSSNR.MVecScaleX/Y` |
| `NRDiffuseWhiteNits` | Diffuse White (nits), na seção "HDR color bridge" | float | só a ponte HDR |
| `NRPaperWhiteScale` | *não aparece na interface* | float | só a ponte HDR |
| `NRTransferStrength` | *não aparece na interface* | float | **nenhum** |
| `NRColorStrength` | *não aparece na interface* | float | **nenhum** |
| `NRToggleKey` / `NRScreenshotKey` | NR Toggle Key / Screenshot Key | tecla | — |
| `EnableHooks` | 2 = só ganchos NGX; 0 = modo seguro, sem NR | 0/1/2 | — |

O shader da ponte HDR, embutido no add-on, começa com `if (HdrMode == 0) { Output = Neural[pixel];
return; }` — em SDR a ponte devolve a saída da rede sem tocar. É por construção que
`NRDiffuseWhiteNits`, `NRPaperWhiteScale`, `NRTransferStrength` e `NRColorStrength` não têm como
agir na nossa cadeia, que é SDR.

### 3.2 `dlss5-feed.cfg` (Feeder 0.12.0)

Vinte e uma chaves: `enabled, mode, hdr, depth_inverted, flags, reset_every, warmup_rebuild,
rebuild, log_frames, create_delay, preset, work_resolution, work_upscale, work_sharpness,
gpu_timeout_ms, buffer_home, async_home, sync_home, mv_scale_x, mv_scale_y, stall_log_ms`, mais
`half_home, passthrough, jitter_sign, jitter_phases` só de diagnóstico.

Combos lidos do binário:

- `mode`: Inert (0) / Transport test, no NGX (1) / Full DLSS path (2)
- `hdr` e `depth_inverted`: Auto (−1) / Force off (0) / Force on (1)
- `preset`: Default / E (legacy CNN) / F (legacy CNN) / J (transformer) / K (transformer) —
  o app grava `5`, e o log confirma `DLSS render preset hint: 5 (E)`
- `work_upscale`: 0 stretch bilinear, 1 FSR 1 (EASU+RCAS), 2 DLSS SR com jitter sintético
- `flags`: bits do `NVSDK_NGX_DLSS_Feature_Flags`. Com `flags=-1` o Feeder resolve para **66** e o
  log diz `flags=66 (SDR MVLowRes AutoExposure)` → MVLowRes = 2, AutoExposure = 64

### 3.3 Uniforms do preset `FafaDLSS5.ini`

Quatro técnicas ativas, nesta ordem: `SMAA`, `Lumenite_Kernel`, `DLSS5_Feed`, `DLSS5_Sharpen`.
Em `[DLSS5_Feed.fx]` mora a decisão de quanto histórico o DLSS pode usar:

| uniform | padrão | o que faz |
|---|---|---|
| `MASK_STRENGTH` | 1.0 | quanto um pixel desconfiado pede ao DLSS para preferir o quadro **atual** |
| `STATIC_DISTRUST` | 0.5 | desconfiança quando a hipótese "não se moveu" vence; 0 = comportamento antigo |
| `MV_VALIDATE` | ligado | valida o vetor contra o quadro anterior |
| `VALIDATE_STATIC` | ligado | teste "não se moveu" × vetor do provedor |
| `STATIC_BIAS` | 0.15 | quanto pior a explicação estática pode ser e ainda vencer |
| `STATIC_MIN_CONTRAST` | 0.012 | abaixo deste contraste 3×3 o teste estático se abstém |
| `VALIDATE_LUMA` | **desligado** | teste de luma; só levanta a máscara, nunca zera o vetor |
| `LUMA_TOLERANCE` | 0.25 | folga do teste de luma |
| `VALIDATE_DEPTH` / `DEPTH_TOLERANCE` | ligado / 0.10 | teste de profundidade (zera o vetor) |
| `VALIDATE_MV` / `MV_CONSISTENCY` | ligado / 1.4 px | teste de consistência (zera o vetor e mascara) |
| `MV_SCALE` / `MV_SIGN` | 1.0 / (1,1) | escala e sinal do vetor, diagnóstico |
| `MV_LOWRES_FILTER` | 0 (bilinear) | como a flow de 1/8 sobe para a resolução cheia |
| `GEOM_ENABLE` + `GEOM_*` | desligado | vetores de geometria por modelo de câmera + profundidade |

`[DLSS5_Sharpen.fx]`: `Sharpness` 0.35, `Denoise` 0.025, `HaloProtect` 0.80.

### 3.4 Achado de arquitetura: o preset não tem sobrescrita por camada

As três seções que o app lê para a camada de cima (`[camada2_reshade]`, `[camada2_feed]`,
`[camada2_captura]`, em `amd\main.cpp:3001-3020`) cobrem o `ReShade.ini`, o `dlss5-feed.cfg` e o
`[captura]` do ini da camada. **Não existe equivalente para o `FafaDLSS5.ini`** — e o
`Camada2EhConfig` trata `fafadlss5.ini` como configuração, copiando o da camada 1 por cima do da
camada 2 a cada ligada. Qualquer ajuste das uniforms acima vale hoje para **as duas** camadas.
Isso importa porque, na cadeia 2x, `SMAA`, `Lumenite_Kernel`, `DLSS5_Feed` e `DLSS5_Sharpen`
rodam **duas vezes** sobre a mesma imagem.

---

*(seções de resultado em preenchimento)*
