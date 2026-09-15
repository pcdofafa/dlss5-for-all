# Reduzir o borrão: o que vale para a camada 1 sozinha e o que só vale na segunda passada

Pesquisa feita em 2026-09-06. **Somente leitura**: nenhum arquivo do app foi alterado, nada foi executado.

Escopo, fixado pelo usuário: a camada 3 está **em pausa** (nota no fim, um parágrafo). O foco é (a) reduzir o
borrão da segunda passada sobre imagem já reconstruída e (b) dizer, de cada técnica, **se ela também melhora
a camada 1 sozinha** — porque a maioria dos usuários roda uma camada só, e o que melhora a camada 1 vale
muito mais.

Base de fatos medidos: `scratchpad\camada3x\RELATORIO-BORRAO.md` (matriz de 12 variantes, fonte de formas e
fonte de textura), `docs\borrao-e-nitidez.md` (2026-09-03) e `docs\motion-vectors.md`. Descartes anteriores:
`RELATORIO-2X-VISUAL.md`, `RELATORIO-DLSS10.md`, `RELATORIO-FRAMEGEN.md`, `RELATORIO-FEEDER-012.md`.

Convenções: **[FATO]** = verificado no código deste repositório, em log ou em documento público citado nas
Fontes. **[MEDIDO]** = número da nossa própria bancada. **[INFERÊNCIA]** = raciocínio meu, pode estar errado.

---

## Tabela-resumo

Ordenada por **quanto ajuda a camada 1 e a 2 juntas**, que é o critério pedido.

| # | Técnica | Ataca qual causa | **Ajuda a camada 1 sozinha?** | Ajuda a 2ª passada? | Esforço | Ganho esperado | Risco |
|---|---|---|---|---|---|---|---|
| 1 | **`work_upscale=2` para quem já roda abaixo de 100%** (todo mundo acima de 2,4 Mpx) | falta de jitter: o DLAA nunca teve subpixel | **SIM — é a maior mudança disponível na camada 1** | sim | **0,5 dia** de teste, o código existe | alto | médio |
| 2 | **Máscara reativa calibrada** (`STATIC_DISTRUST`, `LUMA_TOLERANCE`, `MASK_STRENGTH`) | histórico reprojetado onde o fluxo errou | **SIM, integralmente** | sim | 1 dia (+0,5 para o canal `[camada2_fx]`) | médio-alto | baixo |
| 3 | **A/B do preset: E (hoje) × J × K** | quão duro o modelo trava o histórico | **SIM** | sim | **0,5 dia** | médio | baixo |
| 4 | **NvOFA no lugar do estimador de fluxo** | precisão do fluxo, que contamina tudo | **SIM — é onde ele mais vale** | pouco (ver §5.4) | 4–6 dias | alto na 1 | médio |
| 5 | **Métrica com referência verdadeira + busca automática de parâmetros** | instrumentação; sem ela nada acima é otimizável | **SIM (vale para as duas)** | sim | 1 + 2–3 dias | médio, **garantido** | baixo |
| 6 | **Dizer ao usuário para desligar o TAA do jogo** | a camada 1 vira segunda passada quando o jogo já filtra | **SIM** | sim | 0 (texto) | alto quando o jogo permite | nulo |
| 7 | **`work_upscale=2` partindo de 100%** (fontes ≤ 2,4 Mpx) | idem 1, mas trocando amostras por fase | sim, **incerto** | sim | 0,5 dia | incerto | médio |
| 8 | **Mistura α da 2ª camada com a própria entrada** | degradação cumulativa | **NÃO** (na 1ª seria só "menos DLSS") | **sim, alto e contínuo** | 1,5 dia | alto na 2ª | baixo |
| 9 | `reset_every=1` | histórico sobre imagem já filtrada | **NÃO** — na 1ª joga fora o único acúmulo legítimo | já é o padrão | — | — | — |
| 10 | Flags do NGX (`MVLowRes`, `AutoExposure`, `DepthInverted`, `IsHDR`) | — | já estão corretas nas duas | idem | — | ~0 | — |
| 11 | Treinar estimador de fluxo próprio (ONNX/DirectML) | precisão do fluxo | — | — | 15–25 dias | negativo | alto — **não fazer** |
| 12 | Alternar quadros entre camadas | — | — | — | 1 dia | negativo | alto — **não fazer** |

Leitura em uma linha: **as quatro primeiras técnicas melhoram a camada 1 tanto quanto ou mais que a
camada 2**, e três delas custam meio dia. A única que é exclusiva da segunda passada é a mistura α (item 8),
e ela é a melhor coisa disponível *para quem roda 2x*.

---

## 1. A camada 1 também é uma segunda passada — quando o jogo tem TAA ligado

Este é o elo entre as duas metades do escopo, e ele muda como ler tudo o que vem depois.

A camada 1 recebe "a imagem original do jogo". Mas a imagem original de um jogo moderno **normalmente já
passou por um filtro temporal**: TAA, ou o próprio DLSS/FSR/XeSS do jogo. Nesse caso a camada 1 já é,
estruturalmente, uma segunda passada temporal sobre imagem reconstruída — exatamente a situação que
estudamos na camada 2 [FATO, é o que `docs/borrao-e-nitidez.md` §1.2 registra: *"uma imagem já resolvida, já
com o TAA/DLSS do próprio jogo aplicado"*].

Três consequências práticas:

1. **Tudo que descobrimos sobre a camada 2 vale, em grau menor, para a camada 1 num jogo com TAA.** A
   diferença é de grau: o TAA do jogo tem motion vectors geométricos exatos, então borra menos que a nossa
   camada 1 borra a camada 2.
2. **Nossas duas fontes de bancada representam os dois casos**, e isso nunca foi dito: a fonte de formas
   chapadas (`fonte-caos2.ps1`) é o análogo de um jogo **sem** AA (conteúdo com serrilhado, bordas duras); a
   fonte de foto (`fonte-textura.ps1`) é o análogo de um jogo **com** TAA (imagem já filtrada, sem
   serrilhado para reconstruir). O resultado de que "nenhuma camada acrescenta detalhe" [MEDIDO, §9] foi
   obtido na fonte de foto — ou seja, ele prevê o comportamento em jogo **com TAA ligado**, e não diz o que
   acontece com TAA desligado.
3. **A recomendação mais barata do relatório inteiro não tem código**: dizer ao usuário que, se o jogo
   permitir, desligar o AA do próprio jogo dá ao DLSS 5 a entrada com serrilhado que ele foi treinado para
   receber. É o item 6 da tabela. Uma linha no README e, se couber, uma dica na interface — sem jargão:
   *"se o jogo tiver anti-serrilhado (TAA), experimente desligá-lo: o DLSS 5 trabalha melhor sobre a imagem
   crua."*

**Não afirmo que desligar o TAA melhora sempre** — sem TAA o jogo entrega serrilhado e cintilação que o
nosso DLAA sem jitter pode não resolver bem, e aí a troca é ruim. É uma hipótese com fundamento forte, e o
passo 6 do plano diz como medir.

---

## 2. Jitter: o caso está mais vivo do que o arquivo diz

### 2.1 O que já foi decidido, e por quê [FATO]

`docs/borrao-e-nitidez.md` §1.3 e `docs/motion-vectors.md` fecharam a questão do jitter com uma conclusão
forte e correta **para o caso que examinaram**:

> "deslocar a imagem apresentada por uma fração de pixel a cada quadro e declarar esse deslocamento como
> jitter... não funciona... a imagem capturada **já está amostrada** na resolução de saída... Reamostrar
> essa imagem com meio pixel de deslocamento não recupera fase nenhuma, só move o que já existe. Pior:
> declarar esse deslocamento como jitter **mente para a rede**."

Está certo, e o argumento é bom. Mas ele descreve o deslocamento **na própria resolução de saída (1:1)**.

### 2.2 O que existe no Feeder e é uma coisa diferente [FATO]

`amd\feeder-amd\src-feeder\src\dlss5-feed.cpp` implementa `work_upscale=2`, descrito no código como
"DLSS Super Resolution fed with synthetic jitter (experimental: the downsample grid is shifted sub-pixel
each frame and DLSS reconstructs the native size itself)" (linhas 826-834). O mecanismo:

- `HaltonJitter()` (linha 1312) gera **Halton(2,3)**, com `jitter_phases` = `8·(nativo/trabalho)²` — a mesma
  fórmula que NVIDIA e Intel publicam (§2.5).
- O shader de reamostragem desloca **os quatro guias juntos** (`jitter_uv`, linhas 2303-2316), então cor,
  profundidade, motion vectors e máscara continuam alinhados.
- `ep.InJitterOffsetX/Y` recebe o deslocamento em pixels de render (linhas 5375-5378), com `jitter_sign`.
- O índice da sequência zera em todo reset (linha 5316).
- Fora desse caminho, `InJitterOffsetX/Y` é literalmente `0.0f` (linhas 4279, 4690, 5100).

Restrição: **D3D11 de 64 bits apenas** — que é exatamente a nossa janela sobreposta.

### 2.3 Por que isto não é a mentira de §2.1 [INFERÊNCIA, e é a tese central desta seção]

A diferença está em **de onde vem a amostra**:

| | o que se desloca | de onde saem as amostras | há fase nova? |
|---|---|---|---|
| **Jitter falso a 1:1** (o que foi descartado, com razão) | a imagem de saída | interpolação dos próprios pixels de saída | **não** — é a mesma amostra remexida |
| **`work_upscale=2` abaixo de 100%** | a grade de **redução** | pixels **nativos** capturados, com pesos diferentes a cada quadro | **sim**, em relação à grade de trabalho |

A 77%, cada pixel de trabalho é uma combinação de ~1,3×1,3 pixels nativos. Deslocar essa grade por uma
fração de pixel de trabalho escolhe **quais** pixels nativos entram e **com que peso** — combinações
genuinamente diferentes a cada quadro. A rede recebe a fase correta em `InJitterOffset`, e a fase é
verdadeira: o sinal nativo capturado faz o papel que a cena renderizada faria num jogo integrado.

**O teto é honesto e precisa ser dito**: a reconstrução pode chegar até a resolução nativa capturada,
**nunca além**. O jogo já jogou fora tudo acima do Nyquist dele. Isto não transforma o DLAA em fonte de
detalhe que não existe; transforma um **esticamento espacial** num **reconstrutor temporal legítimo**.

### 2.4 O que torna isto a recomendação nº 1 da camada 1 [FATO]

O app **já reduz a resolução de trabalho sozinho**, por área da fonte (`main.cpp:7676-7690`):

| megapixels da fonte | `work_resolution` escolhido | exemplo |
|---|---|---|
| ≤ 2,4 | **100** | 1920×1080 (2,07 Mpx) |
| ≤ 4,0 | **77** | 2560×1440 (3,69 Mpx) |
| ≤ 6,0 | **67** | 3440×1440 (4,95 Mpx) |
| > 6,0 | **58** | 3840×2160 (8,29 Mpx) |

E o `dlss5-feed.cfg` que sai de fábrica nos dois pacotes (`DLSS5ForAll-Portable\bin\` e
`DLSS5ForAll-AMD\bin\`) traz **`work_upscale=1`** [FATO], que é FSR 1 (EASU + RCAS): um esticamento
espacial com nitidez, **sem jitter e sem reconstrução temporal**.

Ou seja: **todo usuário acima de 1080p já roda abaixo de 100% hoje, e recebe o pior dos dois mundos** —
paga a redução de resolução e não ganha a reconstrução. Trocar `work_upscale` de 1 para 2 nesses casos
**não pede que ninguém abra mão de nada**: a redução já aconteceu, por decisão do app, por custo. Só muda
como a imagem volta ao tamanho nativo: FSR 1 (espacial) → DLSS SR com jitter Halton (temporal, com fase
real).

Esta é a razão de ele ser o item 1 da tabela-resumo, e vale **para a camada 1 sozinha**.

**Ressalvas, que são três e nenhuma é pequena:**

1. **Custo.** Hoje o DLSS roda 77%→77% e o FSR 1 estica. Com `work_upscale=2` o DLSS roda 77%→100%: a saída
   da rede fica maior, então o custo sobe, ainda que o passe de FSR 1 saia. **Tem de ser medido em ms** — a
   4080 já vai a 99% com duas camadas a 60 fps [MEDIDO].
2. **Sinal do jitter.** `jitter_sign` errado faz a imagem rastejar em cena parada em vez de estabilizar (o
   próprio comentário do código avisa). Testar os dois com a fonte **parada** antes de qualquer medição com
   movimento.
3. **Falha segura, mas silenciosa para o usuário.** Se nenhum preset de qualidade do DLSS cobrir a razão
   pedida, o Feeder loga `no DLSS preset covers ...; staying on DLAA + FSR 1` e volta sozinho. Bom para
   estabilidade, ruim para diagnóstico: a medição precisa **conferir a linha do log**, senão mede o caminho
   antigo achando que mediu o novo.

### 2.5 O caso de 1080p (item 7 da tabela)

Para fontes ≤ 2,4 Mpx o app usa 100%, e obter jitter exige **descer** para 90 ou 95%. Aí sim há uma troca
real: abre-se mão de 10% de amostras lineares para ganhar fase. A literatura sustenta que reconstrução com
fase costuma pagar (é a premissa de todo modo Quality de DLSS/FSR/XeSS), mas **sobre entrada renderizada e
com serrilhado**, não sobre uma captura já filtrada. **Não recomendo adotar sem medir**, e por isso este
item está separado do anterior na tabela. É barato de testar e o passo 7 diz como.

### 2.6 O que a documentação oficial sustenta [FATO]

- **NVIDIA**: *"NVIDIA found that using a Halton sequence for the jitter pattern provided the best results,
  and the deep learning training undertaken for DLSS uses Halton for the training data"*; *"To correctly
  integrate the current frame with those from the past, DLSS must know how the jitter for the current frame
  was applied... These values should always be between -0.5 and +0.5... provided in pixel-space at the
  render target size"*.
- **Intel (XeSS-SR)**, que documenta o mesmo princípio com mais detalhe: *"generally accepts rendered low
  resolution aliased jittered color as input"*; *"jitter offset values should be in the range [-0.5,0.5]"*;
  sequência quasi-aleatória de comprimento mínimo `8·(largura_entrada/largura_alvo)²`. É a fórmula que o
  nosso `jitter_phases` já usa.

---

## 3. Preset: hoje rodamos o CNN legado, e isso foi de propósito

### 3.1 O estado atual [FATO]

O `dlss5-feed.cfg` de fábrica dos dois pacotes traz **`preset=5`**, que é o preset **E — CNN legado**.
Não é o padrão do modelo nem o transformer.

A escolha está documentada e é bem argumentada (`docs/borrao-e-nitidez.md` §6.1, 2026-09-03): com
`preset=0` o runtime 310.8 usa o **transformer** para DLAA, e o transformer é o modelo que mais confia no
histórico acumulado — virtude quando os motion vectors são exatos, defeito quando não são. O preset E tem
clamp de vizinhança mais duro: descarta histórico que diverge do quadro atual. Menos acumulação, menos
arrasto. O documento também registra o erro de ter escrito `preset=1` (preset A, legado) antes de descobrir
que a chave guarda o enum cru do NGX.

### 3.2 Por que reabrir isso agora, mesmo assim

Três razões, e nenhuma contradiz a decisão original:

1. **A escolha foi por raciocínio, não por A/B na bancada.** Em 3 de setembro a bancada de véu, nitidez,
   detalhe e carimbo de quadro não existia — ela é de 6 de setembro. Hoje dá para medir E, J e K em três
   rodadas, na mesma fonte, com número no fim. **Meio dia.**
2. **O preset K é justamente o transformer corrigido para ghosting.** A leitura pública consistente é que J
   é o transformer base — mais nítido, com mais instabilidade e ghosting — e **K é o refinamento focado em
   estabilidade temporal, menos ghosting e movimento mais limpo**, ao custo de um pouco de nitidez. O
   argumento de 3 de setembro era contra o transformer *base*; ele não foi testado contra K.
3. **A premissa do argumento é exatamente o que os itens 1, 2 e 4 mudam.** "O transformer confia demais no
   histórico e os nossos MV são ruins" deixa de valer se o NvOFA melhorar os MV, ou se o jitter der à rede
   informação de fase de verdade. **Então o A/B do preset tem de ser refeito depois de cada um desses dois
   passos**, e não uma vez só. Está assim no plano.

Vale para **as duas camadas**: é a mesma chave no mesmo Feeder. Para a camada 2 a sobrescrita é
`[camada2_feed] preset=<n>`; para a camada 1 é o `dlss5-feed.cfg` da pasta principal.

---

## 4. Máscara reativa: já existe, já chega ao DLSS, e nunca foi calibrada em nenhuma das duas camadas

### 4.1 O que temos [FATO]

`DLSS5_Feed.fx` gera `DLSS5_Mask` (R8) e o Feeder a entrega em `ep.pInBiasCurrentColorMask`
(`dlss5-feed.cpp:4278`). A máscara sai da **validação por erro de reprojeção**: reprojeta pelo vetor e
testa luma contra a vizinhança 3×3, profundidade e consistência do vetor com o quadro anterior. Quem falha
é zerado e marcado, para o DLSS preferir o quadro atual ali.

Controles em `FafaDLSS5.ini`, seção `[DLSS5_Feed.fx]`, com os valores de hoje:

| chave | hoje | o que faz |
|---|---|---|
| `MASK_STRENGTH` | 1.0 | quanto um pixel desconfiado pede ao DLSS para preferir o quadro atual |
| `STATIC_DISTRUST` | 0.5 | desconfiança quando a hipótese "está parado" vence o fluxo |
| `LUMA_TOLERANCE` | 0.25 | folga do teste de luma contra a vizinhança |
| `MV_CONSISTENCY` | 1.1 | quanto o vetor de agora tem de parecer com o do quadro anterior |
| `VALIDATE_DEPTH` | **0** | teste de profundidade, desligado |
| `STATIC_MIN_CONTRAST` | 0.012 | contraste mínimo para o teste estático valer |

### 4.2 Vale para a camada 1? **Sim, integralmente, e talvez mais que para a 2**

E é importante separar isto da regra que a §10 do RELATORIO-BORRAO fixou. Aquela regra disse para não levar
à camada 1 os achados de **modelo** (`NRIntensity`, `NRLocalStructure`, `reset_every`), por dois motivos:
são escolha estética da pessoa, ou destroem o acúmulo legítimo da primeira passada. **Nenhum dos dois se
aplica aqui**: a máscara não é aparência, é **correção** — ela diz ao DLSS onde o nosso próprio fluxo
estimado não é confiável. Errar para menos ali produz arrasto; errar para mais produz cintilação. É um
parâmetro de exatidão, e parâmetros de exatidão se calibram por medição nas duas camadas.

A camada 1 tem, aliás, **mais** do que a máscara foi feita para pegar: desoclusão real (a câmera do jogo
anda e descobre fundo), partículas, fogo, luz piscando, HUD. A camada 2 vê tudo isso já suavizado.

### 4.3 O que ajustar, e o bloqueio de 20 linhas

Hipóteses diretas, a medir (§4.4 do plano):

- **`STATIC_DISTRUST` 0,5 → 0,75 → 1,0.** O tooltip do próprio shader descreve o sintoma que queremos matar:
  quando o teste estático vence, o vetor é zerado e "o histórico fica preso ao detalhe que devia estar se
  movendo".
- **`LUMA_TOLERANCE` 0,25 → 0,18 → 0,12** na camada 2. [INFERÊNCIA] Numa imagem já suavizada a vizinhança
  3×3 é mais estreita, então a tolerância de fábrica aceita reprojeções que deveria recusar.
- **`VALIDATE_DEPTH`: manter desligado.** Nossa profundidade é estimada; validar contra sinal fraco adiciona
  ruído sem adicionar informação.
- **Termo residual novo** (~meio dia de shader): `mask += k·saturate(|cor − cor_reprojetada|/τ)`. É a
  heurística de luminância que o FSR usa no gerador automático de reactive mask, com a vantagem de que já
  temos a reprojeção feita.

**Bloqueio prático, só para a camada 2**: `main.cpp:3001-3017` tem três canais de sobrescrita
(`[camada2_reshade]` → `ReShade.ini`, `[camada2_feed]` → `dlss5-feed.cfg`, `[camada2_captura]`) e **nenhum**
para `FafaDLSS5.ini [DLSS5_Feed.fx]`. Um quarto canal `[camada2_fx]` é o mesmo laço com outro caminho e
outra seção. Para a **camada 1 não há bloqueio**: o `FafaDLSS5.ini` da pasta principal já é o arquivo real.

### 4.4 O que os outros fazem [FATO]

- **FSR 3.1** separa **reactive mask** (ajusta o balanço de acumulação) de **transparency & composition mask**
  (ajusta a proteção do histórico), e oferece `ffxFsr3UpscalerContextGenerateReactiveMask`, que *"launches a
  compute shader which computes these values for each pixel using a luminance-based heuristic"*. Sem máscara,
  usa uma textura 1×1 zerada. A 3.1 também passou a reaplicar os *locks* a cada quadro, "o que reduz o
  ghosting causado por locks que ficavam ativos tempo demais" — mesmo princípio do nosso `reset_every`,
  porém por pixel em vez de global.
- **XeSS 2** tem *responsive pixel mask*, 0..1, onde *"1.0 indicates that accumulated history should be fully
  ignored"*, com o aviso de só usar "quando melhora claramente a qualidade".
- **`NRAutoMask` do RenoDX**: não achei documentação pública verificável dessa chave. **Não afirmo nada sobre
  ela**; se existir na v4.7, testá-la custa uma linha em `[camada2_reshade]` e um minuto.

---

## 5. Motion vectors e o NvOFA

### 5.1 O que temos hoje [FATO/MEDIDO]

| provedor | como estima | custo | exatidão medida (deslocamento conhecido) |
|---|---|---|---|
| **Lumenite Kernel** (padrão) | fluxo piramidal em pixel shader, mediana por nível, à trous, semeadura pelo quadro anterior; grade 1/8 | fila 3D | **8 px → 7,995 (−0,06%)**; **45 px → 44,668 (−0,7%)**, 99% dos blocos |
| **D3D12 Video Motion Estimation** (`mv_hardware=1`) | bloco de vídeo do encoder, 8×8 a meia resolução | 0,89 ms, fila de vídeo | **45 px → 31,946 (−29%)**, 87% dos blocos |

O Kernel **ganha do hardware de vídeo com folga**, e isso está medido. Duas medições independentes
concordam: na matriz de caos, `mv_hardware=1` piorou o véu (7,02 contra 6,59). O comentário do código que
manda manter o Kernel como padrão está certo.

Onde a grade 1/8 do Kernel realmente custa é na **descontinuidade de movimento** — a borda de um objeto
passando na frente do fundo, onde a interpolação bilinear entre vetores espaçados de 8 px devolve a média
de dois movimentos que podem diferir de dezenas de pixels. **É exatamente onde nasce o halo de borda.**
Isso está escrito em `docs/motion-vectors.md` e não foi resolvido.

### 5.2 NvOFA — os números [FATO]

Hardware dedicado (NVOFA) em toda Turing exceto TU117, e acima; não disputa a fila 3D nem os CUDA cores.
Desempenho oficial a 1080p, por preset e grade:

| grade | preset | Turing | Ampere | **Ada (a nossa 4080)** | ms/quadro na Ada |
|---|---|---|---|---|---|
| 4×4 | FAST | 768 fps | 613 fps | **1296 fps** | **0,77** |
| 4×4 | MEDIUM | 468 | 405 | **1000 fps** | **1,00** |
| 4×4 | SLOW | 225 | 200 | **536 fps** | **1,87** |
| 2×2 | SLOW | n/d | 94 | 210 fps | 4,76 |

A grade 4×4 é a única disponível em Turing e é a que interessa: a 1920×1080 dá um campo de **480×270**,
contra os 240×135 do 1/8 do Kernel — **o dobro da densidade**, e na fila de vídeo em vez da fila 3D. As
grades 2×2 e 1×1 custam 5 a 10× mais e não valem.

Quatro coisas que fazem o NvOFA ser diferente do VME, e não "outro estimador de bloco":

1. **Dicas temporais ligadas por padrão**: *"NVOFA uses the flow vectors of earlier frame as hints for doing
   motion search"* — a mesma semeadura pelo quadro anterior que o Kernel faz, em hardware.
2. **Buffer de custo (confiança) por bloco**, em Ampere+, recomendado em `NV_OF_BUFFER_FORMAT_UINT8`. É um
   mapa de "não confie neste vetor" **de graça**, e vira termo direto da máscara reativa da §4.
3. **Saída em S10.5 fixo** (sinal + 10 inteiros + 5 fracionários): precisão de **1/32 de pixel** e alcance
   de ±1024 px. O VME devolve inteiro e erra 29% a 45 px.
4. **Dicas externas** (Turing+) e **ROI** (Ampere+).

Integração: `NvCreateOpticalFlowD3D12(ID3D12Device*)` em **`nvofapi64.dll`, que vem com o driver** — nada a
redistribuir. Isso resolve a ressalva de licença que travou o NvOFFRUC no `RELATORIO-FRAMEGEN.md`. O molde
já existe: `motion_estimate.h` faz o mesmo interop D3D11↔D3D12 para o VME, com textura compartilhada e
conversão para RGBA16F em delta UV. É reescrever a metade de baixo daquele arquivo.

### 5.3 Vale para a camada 1? **Sim, e é onde ele mais vale**

A camada 1 mantém o histórico (não usa `reset_every`), então a qualidade do vetor decide diretamente para
onde o histórico é arrastado. E o véu da camada 1 de 1,63% contra 0,012% da fonte crua [MEDIDO] entra em
tudo o que vem depois: metade do véu do 2x (3,42%) já vem de baixo.

Onde eu **não** espero ganho é no que a §5.1 mostra: o Kernel já acerta a magnitude com erro sub-1%. O ganho
do NvOFA não é exatidão de magnitude, é **densidade nas descontinuidades** (2× a grade) e **confiança por
bloco de graça**. Essas duas são exatamente as duas causas de halo de borda que sobraram.

### 5.4 Vale para a camada 2? **Provavelmente pouco, e isso é testável em meia hora**

[INFERÊNCIA, mas com consequência prática grande] Com `reset_every=1` — o padrão novo da camada 2 — o DLSS
zera o histórico a cada quadro. Sem histórico, o motion vector quase não tem para onde apontar: ele serve
para buscar o pixel no acúmulo anterior. Se isso estiver certo, melhorar o fluxo da camada 2 não muda quase
nada, e todo o esforço de NvOFA deve mirar a camada 1 — que é, aliás, o que o novo escopo pede.

O teste que decide está no passo 0 do plano e custa meia hora.

### 5.5 As alternativas, e por que não

| opção | veredito |
|---|---|
| **RAFT / PWC-Net em ONNX Runtime DirectML** | RAFT completo ~100 ms/quadro a 1080p; variantes reduzidas 10–30 ms. **10 a 30× o NvOFA**, numa GPU que já vai a 99% com duas camadas [MEDIDO]. Descartado para tempo real; serve só como **verdade de referência offline** (§7.3). |
| **Fluxo óptico do Media Foundation / DirectML** | Não existe um fluxo óptico do Media Foundation exposto para uso genérico. O que há é o Video Processor e o VME que já usamos. Descartado. |
| **Block matching hierárquico em compute shader** | É o que o Lumenite Kernel já é, e ele mede erro de 0,7% a 45 px. Reescrever é semanas para empatar. Não vale. |
| **Lossless Scaling (LSFG 3)** | Rede proprietária de fluxo entre quadros finais, com `Flow scale` ajustável (comunidade recomenda ~75% para 3D). Confirma que o problema tem solução aprendida; o modelo é fechado. Referência de qualidade e latência (+13 ms medidos). |
| **OptiScaler** | Não estima fluxo próprio: **redireciona** chamadas de upscaler que o jogo já tem. Não resolve MV ausente. Útil como referência da sequência de chamadas NGX (fork `OptiScaler_DLSSNR`). A wiki dele descreve o nosso sintoma: fluxo óptico inferido de quadros finais, quando erra, mistura os pixels errados e produz rastro. |
| **mpv / RIFE** | Interpolação de vídeo; latência incompatível. |

### 5.6 Como validar qualquer estimador contra verdade conhecida

Quase de graça, porque a `fonte-caos2.ps1` tem seed fixa e trajetória **em função do número do quadro**:
1. Reaproveitar o LCG para escrever, dado N, o campo de fluxo verdadeiro (deslocamento da forma que cobre
   cada pixel; fundo = 0).
2. Ler o campo estimado.
3. Métricas padrão: **EPE** (erro médio em px) e **Fl-all** (% de pixels com erro > 3 px e > 5% da
   magnitude) — a mesma métrica KITTI que a NVIDIA usa nos próprios números.
4. Reportar por faixa de velocidade (0–10, 10–30, 30–60, >60 px/quadro) **e nos 3 quadros seguintes a uma
   inversão de 180°**, que é onde o halo aparece.

Sem isso, "o NvOFA é melhor" é opinião. Com isso, é um número por provedor — e vale para julgar o Kernel
também.

---

## 6. Flags do NGX: já estão certas, nas duas camadas

`dlss5-feed.cpp:2663-2665` monta `MVLowRes | AutoExposure` (+ `DepthInverted`, + `IsHDR`), o que dá o
`flags=66` que aparece no log. Item por item:

- **`MVLowRes`** — declara MV na resolução de render. Em DLAA render == alvo, então é verdadeira e inócua.
  O Streamline confirma o critério: *"`NVSDK_NGX_DLSS_Feature_Flags_MVLowRes` is handled automatically based
  on tagged motion vector buffer's size and extent"*. **Não mexer.** Com `work_upscale=2` continua correta:
  os MV são reamostrados junto com a cor, na resolução de trabalho.
- **`AutoExposure`** — correta e obrigatória: não temos textura de exposição, porque capturamos a imagem
  final já tonemapeada. Streamline: *"If `sl::kBufferTypeExposure` is NOT provided or
  `dlssOptions.useAutoExposure` is set to be true then DLSS will be in auto-exposure mode"*. **Não mexer.**
- **`MVJittered`** — **deve continuar desligada, inclusive se o item 1 for adotado.** [INFERÊNCIA amarrada ao
  código] O `work_upscale=2` desloca a *grade de amostragem* e reamostra os quatro guias juntos; os
  **valores** dos vetores continuam sendo o fluxo nativo, sem jitter embutido. Ligar `MVJittered` mandaria o
  DLSS descontar um jitter que não está lá, e o resultado seria borrão de meio pixel por quadro. Vale como
  item explícito de verificação, porque errar aqui produz exatamente o sintoma que queremos matar.
- **`DoSharpening`** — obsoleta (a NVIDIA removeu o sharpening interno a partir do DLSS 2.5.1) e já fazemos
  nitidez fora, no `DLSS5_Sharpen.fx`. **Não usar.**
- **`DepthInverted` / `IsHDR`** — automáticas e corretas.

**Veredito**: nenhum ganho esperado aqui, nas duas camadas. Uma varredura de `flags` bruto é barata (a chave
existe) mas serve como controle negativo, não como aposta.

---

## 7. O que é exclusivo da segunda passada

### 7.1 O que a literatura de cascata sustenta

Não existe trabalho publicado sobre "aplicar DLSS sobre saída de DLSS" — a busca não devolve nada e eu não
vou inventar citação. Ninguém, na literatura, quer fazer isso: é uma ideia de usuário final. O que existe, e
se aplica por analogia direta, é a literatura de **restauração em cascata**:

- **Regressão à média**: operadores treinados com perda por pixel super-suavizam textura, e aplicá-los
  repetidamente empurra a imagem para a média. Nossa §9 é uma medição limpa disso: detalhe 3,48 (fonte crua)
  → 3,25 (1x) → 2,99 (2x novo padrão).
- **Cascata ingênua perde alta frequência entre estágios**, e a proposta da literatura é troca de informação
  entre eles. Nós temos o caso extremo: dois estágios idênticos, sem troca nenhuma.
- **Deriva cumulativa**, medida por terceiros na nossa própria família de ferramentas: a 1ª passada move a
  imagem 0,057 da fonte e a 2ª move **mais 0,035**, com a recomendação explícita *"Lower the strengths for
  the second pass rather than repeating the first"* [FATO, `conv-README-main.md`]. Já fizemos isso (1.2 → 0.3).

As mitigações que essa literatura oferece, traduzidas:

| mitigação | tradução | aplicável? |
|---|---|---|
| **misturar com a entrada** (limitar o passo) | `lerp(entrada da camada 2, saída da camada 2, α)` | **sim — é o item 8 da tabela** |
| limitar o número de passos | 2 camadas, não 3 | já sabido |
| condicionar na entrada original | é o que a mistura faz | parcial |
| injeção de ruído | grão antes da 2ª passada | **não** — sob filtro temporal, ruído vira cintilação |

### 7.2 A mistura α — a melhor coisa disponível para quem roda 2x

**O que é**: dois passes de ReShade só na camada 2. `DLSS5_MixSave` (acima de `DLSS5_Feed`) copia o
backbuffer para uma textura `pooled = false`; `DLSS5_MixApply` (abaixo) faz `lerp(cópia, neural, DLSS5_MIX)`.

**Por que vale**:
1. É a mitigação que a literatura de cascata mais sustenta.
2. Dá um **eixo contínuo** entre 1x (α=0) e 2x pleno (α=1). Hoje a única forma de andar nesse eixo é
   `NRIntensity`, que é parâmetro **do modelo** e mexe em tom, estrutura e pele junto; α mexe só no quanto
   da segunda passada entra.
3. Na interface vira **um** controle com nome honesto — "força da segunda camada" — que é a regra de UI
   fixada pelo usuário: poucas decisões, sem jargão.
4. Custo de GPU desprezível: uma cópia e um lerp.
5. Reversível: α=1 reproduz o comportamento de hoje.

**Vale para a camada 1? Não.** Na camada 1 a entrada é o jogo, e misturá-la de volta é só "menos DLSS" —
algo que a tecla de liga/desliga já faz, e que não é o produto. É o único item deste relatório genuinamente
exclusivo da segunda passada.

### 7.3 As outras alternativas de encadeamento, em curto

- **Duas redes sobre a MESMA fonte, compondo**: inútil com os mesmos parâmetros (modelo determinístico, duas
  execuções idênticas). Só faria sentido com presets diferentes (J nítido onde está parado, K estável onde há
  movimento), e exige as duas passadas **no mesmo processo** (`passes=2` no Feeder, caminho B3 do
  `RELATORIO-DLSS10`). 5–8 dias, e depende de outro projeto.
- **Segunda em resolução menor, mesclando só o detalhe (unsharp guiado)**: a versão simples disso é a
  mistura α. Fazer a mistura primeiro; evoluir para banda de frequência só se o resultado pedir.
- **Alternar quadros entre camadas**: não fazer. A 60 fps o espectador veria dois níveis de nitidez e de tom
  piscando a 30 Hz, e os filtros temporais de ambas veriam entrada inconsistente a cada quadro.
- **Segunda como denoiser espacial sem histórico**: já é praticamente o que `reset_every=1` faz, e foi
  [MEDIDO] como a variante que mais preserva detalhe (+10 a +14%).

---

## 8. "Treinar": o que é viável

### 8.1 O que está fora de alcance, sem rodeio

Não podemos treinar o DLSS. Os pesos estão em `nvngx_dlss.dll` / `nvngx_dlssnr.dll`, o treinamento é da
NVIDIA, e nem arquitetura nem dados são públicos. Escolher `preset` é escolher entre modelos prontos, não
treinar. Qualquer plano que comece por "fine-tuning do DLSS" está morto na largada.

### 8.2 O que vale: busca automática de parâmetros — **e vale para as duas camadas**

É "treinar" no sentido útil: otimizar os graus de liberdade que temos contra uma métrica objetiva,
automaticamente, em vez de à mão como as 12 variantes da matriz.

**Espaço de busca da camada 1** (o que mais importa, pelo novo escopo): `work_upscale` {1, 2},
`work_resolution` {58…100}, `preset` {0, 5, 6, 10, 11}, `MASK_STRENGTH`, `STATIC_DISTRUST`,
`LUMA_TOLERANCE`, `MV_CONSISTENCY`, `jitter_sign`. **Da camada 2**: as mesmas mais `NRIntensity`,
`NRLocalStructure`, `reset_every` e `DLSS5_MIX`.

**Objetivo escalar**, com a referência verdadeira da §8.3:

```
J = w1·véu + w2·max(0, detalhe_referência − detalhe) + w3·(1 − SSIM) + w4·LPIPS
```

Pesos fixados **antes** de rodar, senão vira ajustar o objetivo até dar o resultado que se queria. LPIPS por
ONNX Runtime na CPU, offline sobre as capturas — não precisa ser rápido.

**Método**: (1) **coordinate descent** grosso e depois fino, ~100 avaliações — barato, interpretável, e
produz a tabela "qual chave importa" que hoje não temos; (2) **CMA-ES** só nas 4 a 6 dimensões contínuas que
sobrarem, população 8 × 40 gerações ≈ 320 avaliações. CMA-ES é a escolha certa por não precisar de
gradiente, aguentar ruído de medição e ser o padrão para 5–20 dimensões contínuas.

**Custo de máquina**: cada avaliação ≈ 70–90 s (abrir, aquecer, rajada de 30–40 capturas, fechar). 100
avaliações ≈ 2,5 h; 420 ≈ 10 h. **Cabe em duas noites.** Repetir 3× os 10 melhores, porque o ruído da
bancada é de ~2% [MEDIDO].

**Harness**: ~80% existe (`fonte-caos2.ps1`, `fonte-textura.ps1`, `medir-caos.ps1`, `matriz-caos.ps1`,
`preparar.ps1`). Falta o laço que escreve o vetor de parâmetros, roda, lê o JSON e devolve J. **2–3 dias.**

**Ganho esperado: médio, e honestamente limitado** — as 12 variantes à mão já pegaram o ganho fácil da
camada 2 (véu 6,59 → 3,42). O valor real da busca é outro, e é grande: ela **impede regressão**. Vira o teste
automático que valida qualquer mudança futura (jitter, preset, NvOFA, máscara) sem depender de olho.

### 8.3 A referência verdadeira que destrava tudo

Hoje as métricas são **sem referência** (véu, gradiente, laplaciano): dizem "borrou", não dizem "ficou
parecido com o certo". Como a fonte é determinística e cada quadro carrega o **carimbo do número N**, lido
com sucesso em 30 de 30 capturas de 20 rodadas [MEDIDO]:

1. Gerar offline, com o mesmo LCG, o quadro N em alta qualidade (supersample 4×, sem compressão).
2. Ler o carimbo da captura → N.
3. Alinhar (escala e recorte são conhecidos) e comparar: **SSIM, PSNR e LPIPS**.

Isso transforma "véu de 3,42%" em "quão longe do certo", que é a única métrica que responde à pergunta que o
usuário faz de verdade. **1 dia**, pré-requisito da §8.2, e vale para as duas camadas.

### 8.4 Treinar um modelo pequeno nosso

| candidato | tempo | custo em inferência | veredito |
|---|---|---|---|
| **estimador de fluxo** (PWC-Net pequeno, ONNX/DirectML) | 15–25 dias | 10–30 ms/quadro | **NÃO.** O NvOFA custa 1 ms em hardware ocioso e é fruto de anos de engenharia; o nosso Kernel já erra 0,7% a 45 px. Treinar algo pior e 20× mais caro é trabalho desperdiçado. |
| **máscara reativa aprendida** (rede minúscula: 2 quadros + fluxo → 1 canal) | 8–15 dias | 1–3 ms | **TALVEZ, depois.** Único candidato com relação de custo plausível, e só faz sentido **depois** de esgotar e medir a máscara heurística da §4. Se a heurística der 80% do ganho, a rede não se paga. |
| **classificador por bloco 16×16** ("aqui não acumule") | 5–8 dias | <0,5 ms | Versão barata do anterior; é o desenho do "classifier guided temporal supersampling". Só considerar se a heurística falhar de forma sistemática e identificável. |

Regra: **não treinar nada antes de a busca da §8.2 ter rodado**, porque é ela que dirá se ainda sobra ganho
no espaço que já temos.

---

## 9. Estado da arte, verificável

| trabalho / repositório | o que é | aplicável a nós |
|---|---|---|
| **NVIDIA Optical Flow SDK 5.0** + **NVOFA Application Note** + **NVOFA Programming Guide** | fluxo óptico em hardware, D3D12, buffer de custo, dicas temporais, saída S10.5 | **Sim, alto.** Fonte dos números da §5.2 e do `NvCreateOpticalFlowD3D12`. |
| **NVIDIA Streamline, `ProgrammingGuideDLSS.md`** | requisitos oficiais de MV, exposição, presets (A–D removidos, G–I reservados, E/F legados) | **Sim** — sustenta §3 e §6. |
| **Intel XeSS-SR Developer Guide 2.0** | a fonte pública mais explícita sobre jitter, entrada com serrilhado, `historyReset`, responsive mask, comprimento da sequência | **Sim** — sustenta §2.6 e §4.4. |
| **AMD FidelityFX SDK, `super-resolution-upscaler.md`** | reactive mask, transparency & composition mask, gerador automático por heurística de luminância, locks por quadro na 3.1 | **Sim** — molde direto para a §4. |
| **k-DOP Clipping: Robust Ghosting Mitigation in TAA** (SIGGRAPH Asia 2024) | rectificação de histórico com politopos k-DOP em vez de AABB | **Parcial** — não dá para mexer no clamping *dentro* do DLSS, mas o princípio é o que a máscara e a mistura α aproximam por fora. |
| **Temporal Antialiasing Starter Pack** (Alex Tardif) e o **survey de TAA de Lei Yang** (EG 2020) | variance clipping, color clamping, rejeição de histórico, peso temporal menor para conteúdo rápido | **Sim, conceitualmente** — é o vocabulário de todo este relatório. |
| **Neural Supersampling for Real-time Rendering** (Xiao et al., ACM TOG 2020) | supersampling neural 4×4 a partir de cor + profundidade + MV em baixa resolução | Referência: confirma que MV continuam entrada obrigatória mesmo na academia; ninguém entrega qualidade só com cor. |
| **Classifier Guided Temporal Supersampling** (Guo et al., CGF 2022) | classifica pixels e trata cada classe diferente; 1080p→2160p em 13,4 ms numa 3090 | Referência para a §8.4: desenho e ordem de grandeza de custo de uma máscara aprendida. |
| **An Iterative Framework of Cascaded Deblocking and Super-Resolution** | cascata ingênua super-suaviza alta frequência entre estágios | **Sim, por analogia** — sustenta a §7.1. |
| **Lossless Scaling / LSFG 3** | rede proprietária de fluxo entre quadros finais, `Flow scale` ajustável | Referência de qualidade e latência. Fechado. |
| **OptiScaler** e **OptiScaler_DLSSNR** | redireciona chamadas de upscaler; o fork adiciona neural rendering em DX12 | Referência da sequência de chamadas NGX. **Não** resolve MV ausente. |
| **DLSSTweaks** (emoose) | força presets e opções de DLSS por jogo, inclusive opções de jitter em desenvolvimento | Referência de quais chaves NGX são ajustáveis de fora. |

---

## 10. Plano recomendado, ordenado por quanto ajuda as duas camadas juntas

Cada passo diz **em qual camada aplica**, **como medir** e **o critério de parar**.

### Passo 0 — Portão: os motion vectors da camada 2 ainda importam? (meia hora)

*Camada 2. Não muda nada sozinho; decide o escopo do passo 5.*

**Faça**: com o padrão atual (`NRIntensity=0.3`, estrutura 1, `reset_every=1`), 3 rodadas com
`[camada2_captura] mv_hardware=1` (MV pior de propósito) e 3 sem.

**Meça**: véu, nitidez e detalhe nas duas fontes.

**Critério**: se as duas condições ficarem dentro de 3% uma da outra, está provado que **com reset a cada
quadro o MV da camada 2 é carga morta** — o NvOFA vai inteiro para a camada 1, e ainda vale medir quanto de
GPU se economiza desligando a estimativa na camada 2.

### Passo 1 — `work_upscale=2` na CAMADA 1, para quem já roda abaixo de 100% (0,5 dia)

*Camada 1 (e a 2 herda a entrada melhor). É a maior mudança disponível.*

**Faça**: em fonte acima de 2,4 Mpx (1440p ou ultrawide, onde o app já escolhe 77 ou 67%), trocar
`work_upscale` de 1 para 2 no `dlss5-feed.cfg` principal, com `work_sharpness=0.3`. **Antes de tudo**, achar
o `jitter_sign` certo com a **fonte parada**: o certo estabiliza, o errado faz a imagem rastejar.

**Confira no log** (senão você mede o caminho antigo achando que mediu o novo):
`work_upscale=2: DLSS <qualidade>, WxH -> WxH, Halton(2,3) over N phases`. Se aparecer
`no DLSS preset covers`, aquela razão caiu de volta para DLAA + FSR 1 e a rodada não vale.

**Meça**: véu, nitidez e **detalhe** nas duas fontes, mais **fps e ms de GPU** — a saída da rede fica maior,
o custo sobe, e isso precisa de número.

**Critério de adotar**: o **detalhe** tem de subir. Este é o único caminho de toda a cadeia capaz de fazer o
DLSS *reconstruir* em vez de só filtrar, porque é o único que dá fase de verdade à rede. Se o detalhe não
subir em nenhuma razão testada (77, 67, 58), a hipótese está refutada nesta cadeia — registrar, porque é um
resultado negativo valioso que fecha uma pergunta aberta desde 3 de setembro.

**Ressalva a escrever no resultado**: isto **não** contradiz `docs/motion-vectors.md`. Aquele documento
descartou deslocar a imagem **na resolução de saída**, onde não há fase nova a recuperar, e está certo.
Aqui a grade que se desloca é a de **redução**, e as amostras vêm dos pixels nativos — ver §2.3.

### Passo 2 — Calibrar a máscara reativa na CAMADA 1 (1 dia)

*Camada 1 direto; a camada 2 herda e depois repete com o canal novo.*

**Faça**: varrer no `FafaDLSS5.ini` principal, seção `[DLSS5_Feed.fx]`: `STATIC_DISTRUST`
{0,5 · 0,75 · 1,0}, `LUMA_TOLERANCE` {0,25 · 0,18 · 0,12}, `MASK_STRENGTH` {1,0 · 0,7}. Manter
`VALIDATE_DEPTH=0`.

**Meça**: véu e, principalmente, o **perfil de halo** — o instrumento que falta: perfil radial de luma em
anéis de 1 a 20 px em volta das formas, separado por lado de ataque e lado de fuga. O véu global mistura
halo com fantasma; o perfil separa.

**Critério**: adotar a combinação que baixar o halo do lado de fuga em ≥ 10% sem perder detalhe. Se nenhuma
chegar lá, a heurística está saturada e o próximo item é o **termo residual** da §4.3, não uma rede aprendida.

### Passo 3 — A/B do preset nas duas camadas (0,5 dia)

*Camadas 1 e 2.*

**Faça**: 3 rodadas de cada, nas duas fontes, com `preset` em {5 (E, o de hoje), 10 (J), 11 (K)}.
Na camada 2 via `[camada2_feed] preset=<n>`.

**Meça**: as quatro métricas. Esperado: K baixa véu e nitidez juntos; J sobe os dois.

**Critério**: manter E só se ele ganhar de fato. **Refazer este passo depois dos passos 1 e 5**, porque o
argumento que escolheu E ("o transformer confia demais no histórico e os nossos MV são ruins") é exatamente
a premissa que o jitter e o NvOFA mudam.

### Passo 4 — Canal `[camada2_fx]` e a máscara na camada 2 (0,5 + 0,5 dia)

*Camada 2.*

**Faça**: acrescentar ao laço de `main.cpp:3001-3017` um quarto tipo que escreve em `FafaDLSS5.ini`
seção `[DLSS5_Feed.fx]` da pasta da camada — a mesma função com outro caminho e outra seção. Depois repetir
a varredura do passo 2 na camada 2, onde a hipótese é que `LUMA_TOLERANCE` precise ser **mais apertada** que
na camada 1 (entrada já suavizada).

**Meça e critério**: iguais aos do passo 2.

### Passo 5 — NvOFA na CAMADA 1 (4–6 dias) — condicional ao passo 0

*Camada 1. Só mirar a camada 2 se o passo 0 mostrar que o MV dela importa.*

**Faça**: espelhar `motion_estimate.h` trocando o VME por `NvCreateOpticalFlowD3D12` em `nvofapi64.dll`,
grade 4×4, preset MEDIUM, dicas temporais ligadas, buffer de custo em UINT8. Converter S10.5 → delta UV,
reamostrar a grade e publicar pelo mesmo `update_texture_bindings` de hoje. O buffer de custo entra como
termo extra da máscara reativa.

**Meça**: primeiro **contra verdade de fluxo** (§5.6): EPE e Fl-all por faixa de velocidade e nos 3 quadros
após inversão, comparando **Kernel × VME × NvOFA**. Depois, na imagem: as quatro métricas. E o custo em ms.

**Critério**: adotar se o EPE cair na faixa de 30 a 60 px/quadro **e** o véu do 1x cair. Atenção: o Kernel
já acerta magnitude com erro de 0,7%, então o ganho esperado é em **densidade nas bordas** e **confiança de
graça**, não em magnitude. Se o EPE melhorar e o véu não, o gargalo da camada 1 não era o fluxo — e isso
também é resposta.

### Passo 6 — Medir o jogo com e sem o TAA dele (0,5 dia, zero código)

*Camada 1, e é a recomendação mais barata do relatório.*

**Faça**: um jogo que permita desligar o AA próprio, mesma cena, mesmo percurso, com e sem TAA, 1x.

**Meça**: as quatro métricas, mais uma comparação visual honesta (o serrilhado sem TAA é custo real).

**Critério**: se o detalhe subir sem que o serrilhado fique pior a olho, escrever a dica no README e, se
couber na interface, uma linha sem jargão: *"se o jogo tiver anti-serrilhado (TAA), experimente
desligá-lo"*. Não transformar em opção nem em automatismo — é dica, e a decisão é da pessoa.

### Passo 7 — `work_upscale=2` partindo de 100% (0,5 dia)

*Camada 1 em fontes ≤ 2,4 Mpx (1080p), e camada 2.*

**Faça**: `work_resolution` em {95, 90, 85, 77} com `work_upscale=2`, contra o 100% de hoje.

**Meça e critério**: iguais ao passo 1. Aqui a troca é real (abre-se mão de amostras para ganhar fase), então
o critério é mais duro: só adotar se o detalhe **e** o véu melhorarem, em 3 repetições.

### Passo 8 — Métrica com referência verdadeira e busca automática (1 + 2–3 dias)

*As duas camadas.*

**Faça**: o gerador offline do quadro N + SSIM/PSNR/LPIPS (§8.3); depois o laço de busca da §8.2, primeiro
sobre as chaves da **camada 1**, depois as da 2.

**Meça**: recalcular tudo que já foi medido contra a referência, e conferir se o ranking por SSIM/LPIPS bate
com o ranking por véu/detalhe. **Se divergir, as métricas sem referência estavam medindo a coisa errada** e
as conclusões anteriores precisam de revisão — esse é o valor do passo.

**Critério**: adotar o ótimo se bater o padrão por mais que o ruído (~2%) em 3 repetições independentes.
Depois, manter o harness como **teste de regressão** de qualquer mudança futura.

### Passo 9 — Mistura α na camada 2 (1,5 dia)

*Camada 2 apenas. É o último porque não ajuda quem roda uma camada só.*

**Faça**: `DLSS5_MixSave` acima de `DLSS5_Feed` (copia o backbuffer para textura `pooled = false`) e
`DLSS5_MixApply` abaixo (`lerp(cópia, atual, DLSS5_MIX)`), só quando `g_camada2`.

**Meça**: varrer `DLSS5_MIX` em {0 · 0,25 · 0,5 · 0,75 · 1,0} e traçar a curva véu × detalhe. Teste de
sanidade: α=0 tem de reproduzir o 1x dentro do ruído, e α=1 a variante 12.

**Critério**: escolher o joelho da curva e expor como **um** controle, "força da segunda camada", 0 a 100%,
com o joelho como padrão. Sem expor `NRIntensity` nem `reset_every`.

---

## 11. O que NÃO fazer

1. **Treinar um estimador de fluxo próprio.** 15 a 25 dias para entregar algo mais lento e pior que 1 ms de
   hardware ocioso, contra um Kernel que já erra 0,7% a 45 px.
2. **Alternar quadros entre camadas.** Cintilação a 30 Hz entre dois níveis de filtragem.
3. **Injetar ruído antes da segunda passada.** Sob filtro temporal, ruído vira cintilação.
4. **Deslocar a imagem a 1:1 e declarar como jitter.** Continua sendo mentira para a rede, e continua
   descartado — a §2.3 explica por que o `work_upscale=2` é outra coisa.
5. **Levar `NRIntensity`, `NRLocalStructure` ou `reset_every=1` para a camada 1.** Os dois primeiros são
   escolha estética da pessoa; o terceiro jogaria fora o único acúmulo temporal legítimo da cadeia.
6. **Ligar `VALIDATE_DEPTH`.** Profundidade estimada é sinal fraco; validar contra ele adiciona ruído.
7. **`DoSharpening`.** Obsoleta desde o DLSS 2.5.1, e já fazemos nitidez fora.
8. **Voltar a caçar quadro repetido/pulado ou trocar o transporte por causa do borrão.** [MEDIDO] 0% de
   repetidos em 30 de 30 capturas em 20 rodadas. A textura compartilhada continua valendo pelos +21 ms de
   atraso, e é outro projeto.

---

## Nota sobre três ou mais camadas

Em pausa por decisão do usuário, e a medição apoia a pausa: cada camada custa ~20 pontos de uma RTX 4080
(5,5–6,7 ms/quadro) e +1,0–1,1 GB de VRAM; duas camadas a 60 fps já levam a GPU a 99%; três só se sustentam
a ~44 fps, com latência de ~135 ms, quatro a cinco vezes a do 1x [MEDIDO, `RELATORIO-3X.md`]. Tudo neste
relatório se aplica a uma terceira camada por herança, sem nada de específico a acrescentar.

---

## Fontes

- NVIDIA Optical Flow SDK 5.0 — https://docs.nvidia.com/video-technologies/optical-flow-sdk/
- NVOFA Application Note (desempenho por preset/grade/geração) — https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-application-note/index.html
- NVOFA Programming Guide (D3D12, `NvCreateOpticalFlowD3D12`, S10.5, cost buffer, temporal hints) — https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvofa-programming-guide/index.html
- NVIDIA Optical Flow SDK no GitHub — https://github.com/NVIDIA/NVIDIAOpticalFlowSDK
- NVIDIA Streamline, `ProgrammingGuideDLSS.md` — https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md
- Intel XeSS-SR Developer Guide 2.0 — https://www.intel.com/content/www/us/en/developer/articles/technical/xess-sr-developer-guide.html
- AMD FidelityFX Super Resolution 3.1 Upscaler — https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/
- FidelityFX-SDK, `super-resolution-upscaler.md` — https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/techniques/super-resolution-upscaler.md
- k-DOP Clipping: Robust Ghosting Mitigation in Temporal Antialiasing (SIGGRAPH Asia 2024) — https://dl.acm.org/doi/10.1145/3681758.3697996
- Temporal Antialiasing Starter Pack (Alex Tardif) — https://alextardif.com/TAA.html
- A Survey of Temporal Antialiasing Techniques (Lei Yang, EG 2020) — https://www.leiy.cc/publications/TAA/TAA_EG2020_Talk.pdf
- Intel Graphics Optimized TAA — https://github.com/GameTechDev/TAA
- Neural Supersampling for Real-time Rendering (ACM TOG 2020) — https://dl.acm.org/doi/abs/10.1145/3386569.3392376
- Classifier Guided Temporal Supersampling for Real-time Rendering (CGF 2022) — https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.14672
- An Iterative Framework of Cascaded Deblocking and Super-Resolution for Compressed Images — https://www.researchgate.net/publication/320636791_An_Iterative_Framework_of_Cascaded_Deblocking_and_Super-Resolution_for_Compressed_Images
- OptiScaler — https://github.com/optiscaler/OptiScaler · problemas conhecidos — https://github.com/optiscaler/OptiScaler/wiki/Known-Issues
- OptiScaler_DLSSNR — https://github.com/Dagherbou/OptiScaler_DLSSNR
- DLSSTweaks, opções de jitter — https://github.com/emoose/DLSSTweaks/issues/145
- LSFG 3 (Lossless Scaling) — https://losslessscaling.com/lsfg-3/
- Presets do DLSS, J × K — https://ithy.com/article/dlss-4-preset-j-vs-k-comparison-ce8ju1oi
- NVIDIA DLSS Presets, visão geral — https://www.destructoid.com/nvidia-dlss-presets-what-do-they-mean-and-which-one-should-you-choose/
