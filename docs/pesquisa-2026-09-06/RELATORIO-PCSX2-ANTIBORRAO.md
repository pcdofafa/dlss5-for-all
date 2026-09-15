# PCSX2 contra o borrão de upscaling — o que a build local tem, como funciona, e o que serve para o DLSS5 For All

Data: 2026-09-06. Pesquisa e inspeção de arquivos, sem alterar nada do emulador nem do projeto.
Build inspecionada: `C:\Users\PCDOFAFA\Downloads\PCSX2-DLSS5\pcsx2-qt.exe` — **PCSX2 2.9.9** (nightly,
`FileVersion 2.9.9.0`, shaders com cabeçalho `2002-2026 PCSX2 Dev Team`).

---

## 1. Resposta curta

O PCSX2 tem **um** mecanismo anti-borrão que é conceitualmente igual ao nosso problema, e ele não é
um filtro: é **alinhamento**. O `Anti-Blur` detecta que o jogo está somando duas cópias da mesma
imagem com **1 pixel de deslocamento** e força as duas ao mesmo offset. Todo o resto do arsenal do
emulador (`Half Pixel Offset`, `Round Sprite`, `Align Sprite`, `Native Scaling`) é a mesma ideia
aplicada à rasterização: **quando duas amostras da mesma coisa entram com meio pixel de diferença, o
resultado é borrão constante, e a cura é corrigir a fase, não filtrar depois.**

Isso importa para nós por um motivo concreto: **a nossa apresentação tem um caminho com viewport
fracionário**. Em `amd\main.cpp:1115-1119`, quando a proporção da fonte difere da do backbuffer, o
viewport recebe `vx`, `vy`, `vw`, `vh` em ponto flutuante sem arredondamento. Um viewport com origem
ou tamanho a meio pixel desloca a grade de amostragem da imagem inteira e produz exatamente o borrão
uniforme que o `Anti-Blur` do PCSX2 existe para matar. **A §5 descreve o teste que mede isso em
minutos** — é a hipótese mais barata que resta, e ainda não foi testada.

O que **não** é aproveitável: quase tudo que o PCSX2 faz é de **rasterização**, com acesso a
vértices, coordenadas de textura e registradores do GS. Nós recebemos uma imagem pronta. Dos hacks
de upscaling, nenhum tem análogo direto na nossa cadeia. Dos **filtros de saída**, dois têm: o
**Bilinear (Sharp)** e o **Integer Scaling**.

E há uma ressalva de licença que vale para tudo: **o PCSX2 é GPL-3.0+**. O código dos shaders em
`resources\shaders\` carrega `SPDX-License-Identifier: GPL-3.0+` no cabeçalho. **Nada disso pode ser
copiado para dentro do nosso app.** Os algoritmos são simples e estão descritos aqui para
reimplementação própria. A única exceção é o CAS, que vem da AMD sob MIT (§3.7).

---

## 2. A build local e o que está ligado no ini do usuário

### 2.1 O que a build 2.9.9 oferece

Todas as opções da lista pedida existem nesta build, confirmado por extração de strings do
executável. Duas delas são recentes e substituíram hacks antigos:

- **`Native Scaling`** (Off / Normal / Aggressive / Normal (Maintain Upscale) / Aggressive (Maintain Upscale))
- **`Bilinear Dirty Upscale`** (Automatic / Force Bilinear / Force Nearest)

Os valores de `Half Pixel Offset` nesta versão são seis: `Off`, `Normal (Vertex)`,
`Special (Texture)`, `Special (Texture - Aggressive)`, `Align to Native`,
`Align to Native with Texture Offset` (enum `GSHalfPixelOffset` em `pcsx2/Config.h`).

Shaders presentes em `resources\shaders\`: `present.fx` (filtros de TV, CRT Lottes, 4xRGSS, NxAGSS),
`convert.fx`, `merge.fx`, `interlace.fx`, `shadeboost.fx`, `tfx.fx`, `cas.hlsl`, `common\fxaa.fx`,
`common\ffx_cas.h`, `common\ffx_a.h` — nas três APIs (dx11, opengl, vulkan). **Não há xBR, xBRZ, hqx,
scale2x nem Lanczos em lugar nenhum do PCSX2.** Quem quiser esses filtros usa ReShade por fora.

### 2.2 O que o usuário já ativou (`inis\PCSX2.ini`)

| chave | valor | leitura |
|---|---|---|
| `Renderer` | 3 | Direct3D 11 — é o que permite o ReShade `d3d11.dll` da pasta |
| `upscale_multiplier` | **6** | 6× a resolução interna. De 640×448 isso dá 3840×2688 |
| `filter` | 2 | Bilinear (PS2) — o correto, respeita o que o jogo pede |
| `TriFilter` | -1 | trilinear desligado |
| `MaxAnisotropy` | **0** | **anisotrópico DESLIGADO** — ver §6, é ganho de nitidez grátis |
| `mipmap` / `hw_mipmap` | true | ligados |
| `dithering_ps2` | 2 | Unscaled (dither em tamanho nativo) |
| `accurate_blending_unit` | 1 | Basic |
| `texture_preloading` | 2 | Full (hash cache) — o recomendado |
| `HWDownloadMode` | 0 | Accurate |
| `pcrtc_antiblur` | **true** | **o Anti-Blur está LIGADO** (é o padrão) |
| `disable_interlace_offset` | false | offset de entrelaçamento ativo |
| `deinterlace_mode` | 0 | Automatic |
| `linear_present_mode` | **1** | **Bilinear (Smooth)** — o filtro de saída borrado |
| `IntegerScaling` | false | desligado |
| `TVShader` | 0 | nenhum |
| `fxaa` / `ShadeBoost` / `CASMode` | false / false / 0 | todos desligados — **certo para o nosso caso** |
| `UserHacks` | **false** | **todos os hacks manuais inativos**; valem os ajustes automáticos do GameIndex |
| `UserHacks_HalfPixelOffset` | 0 | Off (e inerte, com `UserHacks=false`) |
| `UserHacks_native_scaling` | 0 | Off (idem) |
| `UserHacks_round_sprite_offset` | 0 | Off (idem) |
| `SkipDuplicateFrames` | **true** | **pula a apresentação de quadros repetidos — ver §6, isto atrapalha a nossa cadeia** |
| `VsyncEnable` | false | desligado |
| `SyncToHostRefreshRate` | false | desligado |
| `EnableNoInterlacingPatches` | false | desligado — ver §6 |
| `ExtendedUpscalingMultipliers` | false | multiplicadores acima de 8× ocultos |

O ReShade já está injetado dentro do emulador (`d3d11.dll`, `dlss5-feed.addon64`,
`renodx-dlss5.addon64`, `nvngx_dlss.dll`, `nvngx_dlssnr.dll` na raiz da pasta), com
`Techniques=Lumenite_Kernel,DLSS5_Feed` e `DLSS5_MV_PROVIDER=3`. Ou seja: **no caso do PS2 a nossa
camada 1 não é captura de janela, é injeção direta no swapchain do PCSX2.** Isso elimina de saída
a maior parte dos riscos de reamostragem da camada 1 — e concentra tudo no que o próprio PCSX2 faz
com a imagem antes do `Present`.

---

## 3. Como o PCSX2 resolve borrão — o mecanismo de cada técnica

### 3.1 Anti-Blur (`pcrtc_antiblur`) — o mais interessante para nós

**O que o hardware faz.** O PS2 tem dois circuitos de display (o *merge circuit* do PCRTC). Muitos
jogos programam **os dois circuitos lendo o mesmo framebuffer, com 1 pixel de deslocamento vertical
entre eles**, e o merge soma os dois. O resultado é um borrão vertical de meio pixel aplicado à
imagem inteira: é o "flicker filter" que alguns jogos deixavam o jogador ligar e desligar. Serve
para suavizar o tremular do entrelaçado numa TV de tubo, e alternando o offset a cada campo dá para
mandar uma imagem progressiva de 30 fps para uma saída entrelaçada de 60 Hz sem cintilar.

**O que o PCSX2 faz.** Detecta o padrão e força os dois offsets a serem iguais. O código está em
`GSState::GSPCRTCRegs::CalculateDisplayOffset` e em `SetRects`:

```cpp
int blurOffset = abs(PCRTCDisplays[1].displayOffset.y - PCRTCDisplays[0].displayOffset.y);
if (GSConfig.PCRTCAntiBlur && !scanmask && blurOffset < 4)
{
    if (PCRTCDisplays[1].displayOffset.y > PCRTCDisplays[0].displayOffset.y)
        PCRTCDisplays[1].displayOffset.y -= blurOffset;
    else
        PCRTCDisplays[0].displayOffset.y -= blurOffset;
}
```

Três guardas dizem tudo sobre o critério: só age quando os dois circuitos leem a **mesma fonte**
(`PCRTCSameSrc`), quando o deslocamento é **pequeno** (`< 4`, ou `<= 1` no caminho do framebuffer) e
quando **não** há `scanmask`. Fora disso o deslocamento é intencional (tela dividida em DmC, Time
Crisis 2) e é preservado.

**A leitura que interessa:** o borrão que o emulador combate não é falta de nitidez, é **duas cópias
da mesma imagem somadas fora de fase**. A correção não filtra nada — realinha. É o mesmo diagnóstico
que a §5 propõe testar na nossa cadeia.

### 3.2 Half Pixel Offset — por que o upscaling revela costura

**A causa raiz.** O blog oficial explica que a resolução nativa esconde os erros: *"native resolution
works because of aliasing issues. The holes and errors in the image are below the sampling rate"*.
Ao multiplicar a resolução, buracos de meio pixel e amostras fora da borda da textura, que antes
caíam abaixo da frequência de amostragem, viram linhas e costuras visíveis. Duas famílias de
problema: retângulos pequenos demais que deixam uma fresta entre si, e texturas que precisariam ser
um texel maiores para a interpolação linear ter o que amostrar na borda.

**O que o hack faz.** Nos modos antigos (`Normal (Vertex)`), subtrai aproximadamente meio pixel de
`XYOFFSET.OFX`/`OFY` — o comentário do código é explícito:

```cpp
//This hack subtracts around half a pixel from OFX and OFY.
//The resulting shifted output aligns better with common blending / corona / blurring effects,
//but introduces a few bad pixels on the edges.
```

O ajuste depende do multiplicador de upscaling, com constantes escolhidas empiricamente conforme a
paridade (`GSRenderer::GetModXYOffset`: `+0.2` para múltiplo par, `+0.3` ou `+0.1` para ímpar). Nos
modos `Special (Texture)` a correção vai para as coordenadas de textura
(`GSRendererHW::RealignTargetTextureCoordinate`) em unidades de 1/16 de pixel — `half_offset.x = 8`
é meio pixel na representação de ponto fixo do GS.

Nos modos novos (`Align to Native`, `Align to Native with Texture Offset`) a lógica muda de
natureza: em vez de corrigir a fase depois, o renderizador **calcula as coordenadas na grade da
resolução nativa** e só então aplica a escala, o que remove a classe inteira de erro em vez de
compensá-la. É a opção que a comunidade recomenda hoje como padrão para a maioria dos jogos.

### 3.3 Native Scaling — renderizar o pós-processamento em resolução nativa

Introduzido no PR #11337 (*"GS/HW: Natively downscale targets - General post processing"*). O
problema: jogos de PS2 fazem bloom e blur reduzindo o alvo e reamostrando com filtro bilinear. Com
6× de resolução interna, essa redução amostra uma vizinhança 6× maior com o mesmo número de taps —
o efeito ou quebra ou fica exponencialmente mais caro, e o borrão que era 1 pixel na TV vira 6.

A solução: detectar a redução, **trazer o alvo de volta à resolução nativa antes de filtrar**,
aplicar o efeito lá, e só então reescalar. As variantes `Maintain Upscale` mantêm o alvo em alta
resolução para o resto do desenho. A descrição na UI da build local: *"Emulates native PS2 coordinate
scaling behavior when upscaling to reduce misalignment artifacts and seams in games that draw custom
2D elements."*

**Este é o mais próximo de um princípio que vale para nós**, e a §7 volta nele: *efeito que foi
calibrado numa resolução deve ser calculado nessa resolução*.

### 3.4 Os hacks de sprite, na descrição da própria build

Texto extraído do executável local, verbatim:

| hack | o que faz |
|---|---|
| **Round Sprite** | *"Corrects the sampling of 2D sprite textures when upscaling. Fixes lines in sprites of games like Ar tonelico when upscaling. Half option is for flat sprites, Full is for all sprites."* |
| **Align Sprite** | *"Fixes issues with upscaling (vertical lines) in Namco games like Ace Combat, Tekken, Soul Calibur, etc."* |
| **Merge Sprite** | *"Replaces post-processing multiple paving sprites by a single fat sprite. It reduces various upscaling lines."* |
| **Wild Arms Hack** | *"Lowers the GS precision to avoid gaps between pixels when upscaling. Fixes the text on Wild Arms games."* |
| **Texture Offsets** | *"Offset for the ST/UV texture coordinates. Fixes some odd texture issues and might fix some post processing alignment too."* |
| **Bilinear Dirty Upscale** | *"Can smooth out textures due to be bilinear filtered when upscaling. E.g. Brave sun glare."* |
| **Force Even Sprite Position** | força posição par de sprite, mesma família |

Todos são de rasterização. Todos manipulam vértices ou coordenadas de textura antes do desenho.
**Nenhum tem equivalente numa imagem já renderizada.**

### 3.5 Filtros de saída — os dois que servem para nós

`linear_present_mode` (enum `GSPostBilinearMode`) tem três valores: `Off`, `BilinearSmooth`,
`BilinearSharp`. A UI da build descreve o Smooth como *"Enables bilinear post processing filter.
Smooths the overall picture as it is displayed on the screen. Corrects positioning between pixels."*

**O Bilinear (Sharp) é o algoritmo que vale copiar como ideia.** Não é um kernel novo: é uma
**pré-escala por múltiplo inteiro com amostragem nearest, seguida de bilinear para o tamanho final**.
Em `GSRenderer::VSync`:

```cpp
// Sharpens biinear at lower resolutions, almost nearest but with more uniform pixels.
if (GSConfig.LinearPresent == GSPostBilinearMode::BilinearSharp &&
    (g_gs_device->GetWindowWidth() > fs.x || g_gs_device->GetWindowHeight() > fs.y))
{
    g_gs_device->Resize(g_gs_device->GetWindowWidth(), g_gs_device->GetWindowHeight());
}
```

E `GSDevice::Resize` faz o essencial: sobe o multiplicador inteiro até a imagem cobrir a janela e
faz um `StretchRectAuto(..., Nearest)`. Depois disso, a apresentação bilinear tem uma fonte maior
que o alvo e só suaviza o degrau residual. O ganho: **bordas quase duras, sem a ondulação de largura
de pixel do nearest puro** (aquele efeito em que umas colunas de pixel ficam com 3 px e outras com 2).
O nome clássico da técnica é *sharp bilinear*.

**Integer Scaling** *"adds padding to the display area to ensure that the ratio between pixels on the
host to pixels in the console is an integer number. May result in a sharper image in some 2D games."*
Em outras palavras: **elimina a reamostragem fracionária**, à custa de barras pretas.

**TV Shaders.** O array `s_tv_shader_indices` tem oito entradas, e as duas últimas não são efeito de
TV, são reamostradores: `SUPERSAMPLE_4xRGSS` (*"4xRGSS downsampling (4x Rotated Grid
SuperSampling)"*) e `SUPERSAMPLE_AUTO` (*"NxAGSS downsampling (Nx Automatic Grid SuperSampling)"*).

O 4xRGSS, em `present.fx`, tira quatro amostras numa grade rotacionada dentro da pegada de um pixel
de destino (offsets `±1/8`, `±3/8` do derivativo do uv) e faz a média — supersampling de 4 amostras
válido em qualquer razão de redução. O NxAGSS calcula `ratio = (source/target) * 0.5`, toma
`steps = floor(ratio)` e soma uma grade de `steps × steps` amostras. **Detalhe prático: com
`steps = 0` o laço não roda e ele vira bilinear puro** — o NxAGSS só faz alguma coisa quando a fonte
é pelo menos o dobro do alvo, e só fica forte a partir de 4×.

### 3.6 Deinterlacing — onde mora o borrão temporal de verdade

O PCSX2 **não tem TAA, deghosting nem qualquer acumulação temporal**. A única coisa temporal na
saída é o desentrelaçamento (`GSInterlaceMode`): `Automatic`, `Off`, `Weave TFF/BFF`, `Bob TFF/BFF`,
**`Blend TFF/BFF`**, `Adaptive TFF/BFF`.

**Os modos Blend misturam dois campos consecutivos** — isto é borrão temporal literal, aplicado à
imagem inteira, e é indistinguível de fantasma para um filtro temporal a jusante. O `Adaptive` faz o
blend só onde detecta movimento. O usuário está em `Automatic`, que escolhe conforme o jogo.

Há ainda `disable_interlace_offset`: *"Disables interlacing offset which may reduce blurring in some
situations."* Vem do PR #6106, que corrigiu o offset de entrelaçamento em jogos FFMD (que desenham
meio quadro e esticam) — antes o offset era por linha e borrava a imagem em jogos como Baldur's
Gate 2, Armored Core e Crash Bandicoot.

### 3.7 Sharpening e AA espacial

`CASMode`: `Off`, *"Sharpen Only (Internal Resolution)"*, *"Sharpen and Resize (Display
Resolution)"*. É o FidelityFX CAS da AMD. `fxaa`: FXAA clássico. Ambos são espaciais e ambos são
**exatamente o que não se quer antes de um DLAA** — sharpening cria halos que o filtro temporal
depois carrega, e FXAA borra as bordas serrilhadas que são o insumo do DLAA.

---

## 4. Tabela: técnica × mecanismo × aplicável a nós × esforço × licença

| técnica do PCSX2 | o que faz, em uma linha | aplicável a nós | esforço | licença |
|---|---|---|---|---|
| **Anti-Blur (PCRTC)** | detecta duas cópias da mesma imagem somadas com 1 px de offset e iguala os offsets | **Adaptado — o princípio, não o código.** É o diagnóstico da §5: procurar o meio pixel na nossa cadeia | baixo (o teste), baixo (a correção: arredondar viewport) | GPL-3.0+, **não copiar** |
| **Half Pixel Offset** (todos os modos) | corrige a fase de vértices e coordenadas de textura no rasterizador | **Não.** Precisa de vértices; recebemos imagem pronta | — | GPL-3.0+ |
| **Align to Native** | calcula coordenadas na grade nativa antes de escalar | **Não** diretamente, mas ver o princípio em §7 | — | GPL-3.0+ |
| **Native Scaling** | traz alvos de pós-processamento à resolução nativa antes de filtrar | **Não** como código. **Sim** como princípio: efeito calibrado numa resolução deve rodar nela | — | GPL-3.0+ |
| **Round / Align / Merge Sprite, Wild Arms, Texture Offsets, Bilinear Dirty Upscale** | ajustes de sprite e de coordenada no desenho | **Não.** Rasterização pura | — | GPL-3.0+ |
| **Bilinear (Sharp)** = pré-escala inteira nearest + bilinear | bordas quase duras, sem ondulação de largura de pixel | **Sim.** Vira um `UpFilter` novo para quando a fonte é menor que a janela; útil na camada 2 | **baixo** — 15 linhas de HLSL, algoritmo trivial | reimplementação própria; a ideia não é copiável nem exclusiva |
| **Integer Scaling** | garante razão inteira host/console, elimina reamostragem fracionária | **Sim, e é a versão forte:** garantir 1:1 entre camadas já elimina a reamostragem | **baixo** — é verificação, não filtro | idem |
| **4xRGSS** (downsample) | 4 amostras em grade rotacionada na pegada do pixel de destino | **Sim, marginal.** Já temos `SampleArea` (box com grade de taps bilineares), que cobre a mesma necessidade | baixo, ganho pequeno | GPL-3.0+ o código; o algoritmo é folclore gráfico |
| **NxAGSS** (downsample) | grade `NxN` de amostras quando a fonte é ≥2× o alvo | **Não.** Nosso `SampleArea` já é melhor: adapta `n` pelo `Ratio` e cobre a pegada exata | — | GPL-3.0+ |
| **CAS (FidelityFX)** | sharpening adaptativo ao contraste | **Tecnicamente sim, não recomendado.** Halo antes do DLAA volta amplificado | baixo | **MIT (AMD)** — `ffx_cas.h` na pasta local traz a licença; seria o único copiável |
| **FXAA** | AA espacial por borda | **Não.** Come o aliasing que é o insumo do DLAA | — | GPL-3.0+ nesta versão |
| **Deinterlace Blend** | mistura dois campos | **Não** — é o que queremos evitar, não adotar | — | GPL-3.0+ |
| **xBR / xBRZ / hqx / scale2x** | — | **Não existem no PCSX2.** E não serviriam: são para pixel art indexada; num render 3D com textura filtrada dão aparência de plástico. xBRZ ainda é GPL | — | — |
| **Lanczos** | — | **Não existe no PCSX2**, e não recomendo: o lóbulo negativo cria ringing que um filtro temporal a jusante transforma em halo persistente. O Catmull-Rom que já temos é o meio-termo certo | — | — |

---

## 5. O teste de alinhamento subpixel — passo a passo para o agente de implementação

**Hipótese a testar:** existe um deslocamento de fase subpixel em algum ponto da nossa cadeia, e ele
produz borrão uniforme em toda a imagem, indistinguível de "o DLSS borrou".

**Por que vale a pena:** é o teste mais barato que resta, dá um número inequívoco, e o suspeito
principal já está localizado no código (`amd\main.cpp:1112-1119`, viewport de letterbox com origem e
tamanho fracionários). O relatório do borrão da camada 2 (§4 de `camada3x\RELATORIO-BORRAO.md`)
descartou quadro repetido e quadro pulado; a fase subpixel nunca foi medida.

### 5.1 A fonte de teste

Uma janela sem borda, tamanho exato conhecido (sugestão: 1280×720, para caber em qualquer monitor),
DPI-aware, desenhando um padrão estático — **estático de propósito**: nenhum movimento, para que
qualquer diferença seja espacial e não temporal. Quatro quadrantes mais registro:

1. **Quadrante A (superior esquerdo): listras verticais de 1 px**, alternando preto (0,0,0) e branco
   (255,255,255). É a frequência de Nyquist em X.
2. **Quadrante B (superior direito): listras horizontais de 1 px**, mesma alternância. Nyquist em Y.
3. **Quadrante C (inferior esquerdo): xadrez de 1 px.** Nyquist nas duas direções.
4. **Quadrante D (inferior direito): cinza chapado 128.** Referência — é com este valor que os outros
   quadrantes vão se confundir se houver meio pixel de erro.
5. **Registro:** uma moldura branca de exatamente 1 px no perímetro da área cliente, e uma cruz de
   1 px de espessura em cada canto, a 8 px da borda. Serve para medir deslocamento **inteiro** e
   corte de borda, que é um problema diferente e precisa ser separado do fracionário.

Cores em 0 e 255 puros, sem antialiasing de nenhum tipo, sem transparência, sem sombra de janela.

### 5.2 As medições

Para cada condição da §5.3, capturar a **área cliente** da janela de saída e calcular, por quadrante:

- **contraste** = percentil 99 menos percentil 1 da luma. Ideal: **255**.
- **média** da luma no quadrante. Ideal: **~128** (metade preto, metade branco).
- **fase fracionária estimada `t`**: nas listras verticais, medir o valor médio das colunas que
  deveriam ser brancas (`a`) e das que deveriam ser pretas (`b`). Com bilinear, um deslocamento `t`
  entre 0 e 1 dá `a = 255·(1−t)` e `b = 255·t`. Portanto **`t = b / 255`** e a checagem de
  consistência é `a + b ≈ 255`. Reportar `t` em X (do quadrante A) e em Y (do B).
- **deslocamento inteiro**: posição das cruzes e da moldura na captura menos a posição esperada.

### 5.3 As condições, nesta ordem

| # | condição | o que isola |
|---|---|---|
| 1 | captura direta da janela fonte, sem o app | valida o instrumento. Contraste tem que dar 255 e `t = 0` |
| 2 | camada 1 sobre a fonte, janela do **mesmo tamanho** da fonte, `UpFilter = 0` | o caminho 1:1. Qualquer perda aqui é da captura ou do sampler |
| 3 | camada 1, janela com **proporção diferente** da fonte (ex.: fonte 4:3, janela 16:9) | **aciona o letterbox do `main.cpp:1115`** — é o suspeito principal |
| 4 | camada 1 com `UpFilter` = 1, 2 e 3, janela do mesmo tamanho | verifica se os filtros de reescala erram a fase quando não deveriam nem agir |
| 5 | camada 2 sobre a camada 1, ambas 1:1 | isola a captura WGC da janela da camada 1 |
| 6 | condição 2 repetida com a janela movida 1 px por vez em X, dez posições | se o contraste oscilar com a posição, há reamostragem dependente de posição (DPI, DWM ou captura) |

Rodar tudo com o DLSS desligado se houver caminho para isso, ou com a menor intensidade possível: o
que se mede aqui é geometria, não o modelo.

### 5.4 Como ler o resultado

| resultado | diagnóstico | correção |
|---|---|---|
| contraste ≈ 255, `t ≈ 0` em todas as condições | não há erro de fase. Hipótese descartada, e o borrão é mesmo do filtro temporal | nada |
| contraste ≈ **0** e média ≈ **128** | **meio pixel exato** (`t = 0,5`). Duas amostras somadas 50/50 — é literalmente o caso que o Anti-Blur do PCSX2 corrige | achar o `+0.5` que falta ou sobra |
| contraste entre 0 e 255 com `t` estável | offset fracionário parcial | idem |
| falha só na condição 3 | **é o viewport de letterbox.** `vx`, `vy`, `vw`, `vh` fracionários | arredondar os quatro para inteiro (`floorf(x + 0.5f)`), e preferir `vw`/`vh` de paridade que mantenha a origem inteira |
| falha só na condição 5 | é a captura WGC da janela da camada 1 | conferir `srcW`/`srcH` contra a área cliente real; WGC pode entregar a moldura |
| falha em 6 com oscilação de período 1 px | reescalonamento por DPI ou pelo compositor | o app já pede `PER_MONITOR_AWARE_V2` (`main.cpp:7044`), então investigar a janela alvo, não a nossa |
| deslocamento inteiro ≠ 0 nas cruzes | recorte de borda, não borrão | corrigir o retângulo de origem da captura |

**Critério de aprovação:** condição 2 com contraste ≥ 240 e `|t| ≤ 0,05`. Abaixo disso há
reamostragem indevida no caminho que deveria ser cópia bit a bit.

### 5.5 O que já se sabe olhando o código

- `SampleCatmullRom` e `SampleDirecional` (`main.cpp:497` e `538`) usam `floor(p - 0.5) + 0.5`, que é
  a convenção **correta** de centro de texel. Estes dois provavelmente passam.
- `SampleArea` (`main.cpp:602`) centra a grade de taps em `uv` com offsets simétricos — também
  correto.
- O caminho padrão `UpFilter == 0` é `tex.Sample(smp, i.uv)` com uv de 0 a 1 no quad: correto **se e
  somente se** o viewport cobrir exatamente o alvo em coordenadas inteiras.
- **O viewport de letterbox é o único lugar do arquivo que produz coordenada fracionária.** É por
  isso que a condição 3 do teste é a decisiva.

---

## 6. Receita PS2 + DLSS 5 — as opções recomendadas do emulador

O princípio que organiza tudo: **o DLAA quer entrada nítida, com aliasing genuíno, sem pré-filtro e
sem sharpening.** Aliasing é o insumo dele. Toda suavização que o emulador aplicar antes é trabalho
que o DLAA não vai poder fazer, e ruído de reamostragem é coisa que ele vai amplificar. O relatório
`camada3x\RELATORIO-BORRAO.md` (§9.2) mediu isso de outro jeito: numa foto já filtrada, exibida 1:1,
**nenhuma camada acrescenta detalhe** — só há o que perder. O DLAA ganha onde há serrilhado.

### 6.1 O problema do `upscale_multiplier = 6` na janela de 1080p

Com 6× a partir de 640×448, o GS renderiza 3840×2688. A área 4:3 numa janela 1920×1080 é 1440×1080.
O `Present` então **reduz por um fator de 2,67× em X e 2,49× em Y usando bilinear puro**
(`linear_present_mode = 1`). Bilinear olha 2×2 texels para cobrir uma pegada de 2,5 texels: **mais da
metade dos pixels renderizados é simplesmente descartada**, e o que sobra é aliasing de reamostragem
e moiré em textura fina. Não é supersampling — é subamostragem cara.

Ou seja, a configuração atual paga o custo de GPU de 6× e entrega ao DLAA uma imagem com ruído de
reamostragem em vez de detalhe. **É a mudança de maior impacto desta seção.**

### 6.2 As três receitas, em ordem de preferência

**Receita A — fidelidade máxima, 1:1 sem reamostragem nenhuma** (a que eu recomendaria testar primeiro)

| chave | valor | por quê |
|---|---|---|
| `upscale_multiplier` | **2** | 640×448 × 2 = 1280×896. Cabe inteiro em 1080p |
| `IntegerScaling` | **true** | trava a razão host/console em inteiro |
| `linear_present_mode` | **0** (Off) | sem filtro de saída: cada pixel renderizado vira um pixel de tela |

Resultado: **zero reamostragem entre o GS e o backbuffer**, com barras pretas de 92 px em cima e
embaixo. A imagem chega ao DLAA com o serrilhado que ele sabe resolver. É a entrada mais limpa
possível, e é a única configuração em que o teste da §5 daria contraste 255 na cadeia inteira.

**Receita B — equilíbrio, tela mais cheia**

| chave | valor |
|---|---|
| `upscale_multiplier` | **3** (1920×1344) |
| `linear_present_mode` | **2** (Bilinear Sharp) |
| `IntegerScaling` | false |

A redução passa a ser de 1,33×, muito mais benigna que 2,5×, e o Bilinear (Sharp) preserva as bordas
melhor que o Smooth. Ocupa os 1080 px de altura.

**Receita C — manter 6×, se a preferência estética for essa**

| chave | valor |
|---|---|
| `upscale_multiplier` | 6 |
| `TVShader` | **6** (4xRGSS) |
| `linear_present_mode` | 1 ou 2 |

O 4xRGSS faz o downsample com quatro amostras em vez de uma, o que **conserta a subamostragem
descrita em §6.1**. Aviso honesto: ao reduzir corretamente, ele também reduz o aliasing — o DLAA vai
ter menos a fazer. É a receita de imagem mais macia das três. **Não use `TVShader = 7` (NxAGSS)
aqui**: com razão 2,67 ele calcula `steps = 1` e vira quase um bilinear.

### 6.3 O resto, igual nas três receitas

| chave | recomendado | está em | por quê |
|---|---|---|---|
| `MaxAnisotropy` | **16** | 0 | **ganho de nitidez grátis.** Sem aniso, textura de chão em ângulo raso borra; o tooltip da build é explícito. Requer `mipmap = true`, que já está |
| `filter` | 2 (Bilinear PS2) | 2 | manter. `Forced` borra o que o jogo mandou não borrar; `Nearest` quebra jogos |
| `TriFilter` | -1 (off) | -1 | manter. Trilinear forçado suaviza demais para o DLAA |
| `fxaa` | false | false | manter desligado. Come o serrilhado que é o insumo do DLAA |
| `CASMode` | 0 | 0 | manter desligado. Sharpening antes de filtro temporal vira halo persistente |
| `ShadeBoost` | false | false | manter. O ajuste de imagem é do nosso lado |
| `pcrtc_antiblur` | **true** | true | **manter ligado.** É o anti-borrão real do emulador, e sem ele volta o borrão de meio pixel do merge circuit |
| `dithering_ps2` | testar **3** (Force 32bit) | 2 | o dither é ruído de alta frequência espacialmente incoerente: é a pior entrada possível para um filtro temporal, que tende a transformá-lo em cintilância. `Force 32bit` remove o banding sem gerar o padrão. Se algum jogo estranhar, voltar para 2 |
| `SkipDuplicateFrames` | **false** | true | **mudar.** O ReShade roda no `Present`; um `Present` pulado é um quadro que não passa pela nossa cadeia, e o histórico temporal do DLAA vê um salto onde não houve. O ganho de folga de GPU não compensa |
| `EnableNoInterlacingPatches` | **true** | false | os patches fazem o jogo renderizar progressivo. Sem entrelaçamento não há campo, não há blend e não há offset — remove a família inteira de borrão da §3.6. Só existe para os jogos que têm patch |
| `deinterlace_mode` | 0 (Automatic), e se houver fantasma trocar para **Adaptive** | 0 | evitar Blend a todo custo: mistura dois campos, que é fantasma servido pronto |
| `disable_interlace_offset` | testar **true** se houver borrão vertical persistente | false | *"Disables interlacing offset which may reduce blurring in some situations"* |
| `UserHacks` | **false** | false | **manter desligado.** Ligar desativa os ajustes automáticos do GameIndex, e aí `Half Pixel Offset`, `Round Sprite` e companhia viram responsabilidade sua **por jogo**. Só ligue se um jogo específico apresentar costura, e aí comece por `Half Pixel Offset = Align to Native` + `Native Scaling = Normal` |
| `SyncToHostRefreshRate` | testar **true** | false | cadência regular ajuda o histórico temporal. Custa menos de 1% de velocidade de emulação |
| `accurate_blending_unit` | 1 (Basic) | 1 | manter. Subir custa muito e não ataca borrão |
| `texture_preloading` | 2 (Full) | 2 | manter, é o recomendado |
| `HWDownloadMode` | 0 (Accurate) | 0 | manter |

### 6.4 O que não fazer

- **Não** ligar FXAA, CAS ou TV Shader de CRT junto com o DLSS. São três formas diferentes de gastar
  a informação que o DLAA usaria.
- **Não** desligar o `pcrtc_antiblur` achando que ele é um filtro. Ele é um corretor de alinhamento;
  desligá-lo devolve o borrão.
- **Não** ligar `UserHacks` global sem motivo. A build está usando os ajustes automáticos por jogo, e
  eles são melhores que um palpite manual na maioria dos casos.

---

## 7. O que levar do PCSX2 para o nosso app, em três frases

1. **Borrão uniforme quase nunca é falta de nitidez — é fase errada.** O Anti-Blur inteiro do PCSX2 é
   uma verificação de alinhamento com três guardas e uma subtração. A §5 aplica o mesmo raciocínio à
   nossa cadeia, e é o próximo teste a rodar.
2. **Reamostragem fracionária é sempre um custo.** O Integer Scaling do emulador existe para
   eliminá-la. Na nossa cadeia isso vira uma regra: **garantir 1:1 entre camadas** e, quando não der,
   usar um filtro que preserve borda em vez do bilinear do sampler.
3. **Efeito calibrado numa resolução deve ser calculado nessa resolução** — é o que o Native Scaling
   faz com o bloom dos jogos. Vale como princípio para a segunda passada de Neural Rendering, que
   hoje trabalha sobre uma imagem já filtrada e é justamente onde o borrão do 2x mora.

---

## 8. Licenças

| origem | licença | podemos usar? |
|---|---|---|
| PCSX2 (todo o código e os shaders em `resources\shaders\`) | **GPL-3.0+** (`SPDX-License-Identifier: GPL-3.0+`, e `docs\GPL.html` na pasta) | **Não.** Nada de copiar código. Os algoritmos estão descritos aqui em prosa exatamente para permitir reimplementação independente |
| `resources\shaders\common\ffx_cas.h` e `dx11\cas.hlsl` | **MIT** — *"Copyright (c) 2017-2019 Advanced Micro Devices, Inc."* | Sim, tecnicamente. Mas não recomendo CAS na nossa cadeia (§4) |
| `common\fxaa.fx` | cabeçalho GPL-3.0+ nesta versão | Não |

Os dois itens que a §4 marca como aplicáveis (**sharp bilinear** e **integer scaling**) são ideias de
domínio comum, com implementação de poucas linhas. A implementação tem que ser nossa, escrita a
partir da descrição, sem consultar o arquivo do PCSX2 lado a lado.

---

## 9. Fontes

Inspeção local:
- `C:\Users\PCDOFAFA\Downloads\PCSX2-DLSS5\pcsx2-qt.exe` (versão, strings de UI e tooltips)
- `C:\Users\PCDOFAFA\Downloads\PCSX2-DLSS5\inis\PCSX2.ini`
- `C:\Users\PCDOFAFA\Downloads\PCSX2-DLSS5\resources\shaders\` (present.fx, convert.fx, merge.fx, cas.hlsl, ffx_cas.h, fxaa.fx)
- `C:\Users\PCDOFAFA\Downloads\PCSX2-DLSS5\ReShade.ini`, `dlss5-feed.cfg`, `ReShadePreset.ini`, `LEIA-ME-PRIMEIRO.md`
- `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\amd\main.cpp` (linhas 434-660 dos filtros, 1099-1165 da apresentação, 7038-7045 do DPI)

Código do PCSX2 (lido em `raw.githubusercontent.com/PCSX2/pcsx2/master`):
- `pcsx2/GS/GSState.cpp` — `GSPCRTCRegs::CalculateDisplayOffset` e `SetRects` (Anti-Blur)
- `pcsx2/GS/Renderers/HW/GSRendererHW.cpp` — `RealignTargetTextureCoordinate`, `SetupIA` (Half Pixel Offset)
- `pcsx2/GS/Renderers/Common/GSRenderer.cpp` — `VSync`, `GetModXYOffset`, `s_tv_shader_indices`
- `pcsx2/GS/Renderers/Common/GSDevice.cpp` / `.h` — `GSDevice::Resize`, `enum class PresentShader`
- `pcsx2/Config.h` — `GSHalfPixelOffset`, `GSNativeScaling`, `GSPostBilinearMode`, `GSInterlaceMode`, `GSBilinearDirtyMode`

Web:
- [Explanation: hacks needed for upscaling glitches — blog oficial do PCSX2](https://pcsx2.net/blog/2015/explanation-hacks-needed-for-upscaling-glitches/)
- [PR #6106 — GS: Reduce blurring when upscaling on some games](https://github.com/PCSX2/pcsx2/pull/6106)
- [PR #11337 — GS/HW: Natively downscale targets - General post processing](https://github.com/PCSX2/pcsx2/pull/11337)
- [Issue #5479 — disabling anti-blur with an UI setting](https://github.com/PCSX2/pcsx2/issues/5479)
- [Issue #860 — Half-pixel offset](https://github.com/PCSX2/pcsx2/issues/860)
- [Issue #12351 — Aggressive native scaling mode remains enabled on normal](https://github.com/PCSX2/pcsx2/issues/12351)
- [Maister — Emulating old junk from yesteryear: making native resolution PS2 emulation look good](https://themaister.net/blog/2026/05/09/emulating-old-junk-from-yesteryear-or-my-obsession-making-native-resolution-ps2-emulation-look-good/) (os dois CRTCs, o flicker filter e o offset de 1 px)
