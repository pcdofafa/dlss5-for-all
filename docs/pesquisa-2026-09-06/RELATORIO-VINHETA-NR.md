# A "vinheta escura" do DLSS 5 — o que é, medido

**Data:** 6 de setembro de 2026 · máquina do usuário, Windows 11, RTX 4080, driver 596.49
**Pergunta:** "tem uma vinheta escura em volta da imagem com o DLSS 5 — é filtro nosso ou é
o próprio modelo? Se for filtro, quero tirar."

---

## Resposta

**Não é filtro nosso**, e **não é uma vinheta.** Não existe nenhum escurecimento em função
da posição na tela: num campo cinza uniforme, os cantos saem **0,2% mais claros** que o
centro. O que existe é uma **curva de tom** da rede neural que comprime o contraste —
levanta as sombras e **derruba as altas-luzes em 10,5%**. Como a periferia da cena é quase
sempre mais clara que o meio (céu, janela, queda de iluminação), essa curva escurece
justamente a borda. O olho lê isso como vinheta.

**E dá para tirar com uma linha no `.ini`.** O gatilho é o `NRStyle=1` ("Natural") do
`DLSS5ForAll-AMD\bin\ReShade.ini`. Com **`NRStyle=0`** (o *Default* da rede, e o que os
nossos próprios scripts gravam) a queda das altas-luzes vai de **10,5% para 0,9%** e o
levantamento das sombras continua. A segunda linha, recomendada junto, é devolver
`NRLocalTone` de `2` para `1`, que é o padrão do add-on.

---

## 1. Como foi medido

O que se mede é **o backbuffer real da cadeia**, não um print de tela — um print passa pelo
compositor do Windows e não serve para medir cor. O app já tem a saída certa embutida:
`DLSS5_DUMP=<arquivo.png>` grava o backbuffer no frame `DLSS5_DUMP_FRAME` e encerra sozinho
(`amd\main.cpp:1193`, a função `SaveBackbufferPng`, chamada em `amd\main.cpp:9085` logo
**depois** do `DrawAndPresent` — ou seja, com os efeitos do ReShade já aplicados).

**Validação do instrumento.** Uma rodada de controle comparou o dump com o *screenshot do
próprio ReShade* (PrintScreen, `SaveOverlayShot=0`) do mesmo estado:

| par comparado | diferença média por canal (0–255) | canais diferentes |
|---|---|---|
| dump × screenshot do ReShade | 0,33 | 30% (um frame de diferença) |
| dump × imagem-fonte original | 12,75 | 99,2% |

O dump é a saída depois de toda a cadeia, e essa saída é muito diferente da fonte. O
instrumento mede a coisa certa.

**Fontes**, três, 1920×1080, numa janela sem moldura capturada por `--source window`:

- **cinza uniforme RGB 128** — a sonda de posição. Todo pixel entra com o mesmo valor, então
  qualquer variação espacial na saída é posição, e nada mais.
- **gradiente horizontal 24→232, sem dither** — a sonda de nível. Cada coluna tem um valor
  de entrada conhecido, então a curva de tom sai direto da imagem.
- **foto real** (papel de parede do Windows, recorte 16:9) — a confirmação em cena.

**Referências:** cada fonte também foi rodada com `NeuralUplift=0` (rede desligada, resto da
cadeia igual) e com o Feeder desligado (`enabled=0`).

Todas as rodadas: pacote `DLSS5ForAll-AMD` de 06/09 00:33 (Feeder 0.12.0), cópia isolada em
`scratchpad\vinheta-nr\bin`, FPS na tela desligado, uma rodada por processo, 900 frames
apresentados antes do dump. Os `.ini` são reescritos do zero a cada rodada a partir de uma
base, e a seção `[RenoDX.DLSS5]` é relida do arquivo **6 s depois de o app abrir** e gravada
em `renodx-efetivo.txt`: nas 28 rodadas, a chave pedida estava lá e nenhuma outra tinha
mudado. O add-on não reescreveu nada durante a medição.

**Uma armadilha que custou o primeiro lote, registrada para quem repetir:** no modo
sobreposto o app esconde a camada quando a janela-alvo perde o foco, e o `present` cai para
um a cada 500 ms — a rodada nunca chega ao frame do dump. Com outros agentes abrindo janelas
na mesma máquina isso acontece o tempo todo. As medidas valem porque foram feitas em janela
normal (`--overlay` fora); a rodada de controle `g2-padrao-b` reproduziu `g2-padrao` dentro
de 0,16 ponto percentual, então os dois modos medem a mesma coisa.

---

## 2. Descartado primeiro: filtro nosso

- As técnicas ativas no preset (`FafaDLSS5.ini`) são quatro: `SMAA`, `Lumenite_Kernel`,
  `DLSS5_Feed`, `DLSS5_Sharpen`.
- Busca por `vignette`, `vinheta`, `radial`, `falloff`, `border`, `length(uv-0.5)` nos `.fx`
  instalados: **nenhuma ocorrência** de escurecimento de borda.
- E a prova: com o gradiente e `NeuralUplift=0`, o perfil radial sai **exatamente plano em
  128,0** nas dez faixas, e a curva de tom sai identidade em todos os cinco níveis medidos.
  SMAA, Kernel, Feed e Sharpen, juntos, não mudam brilho.

| rodada | o que muda | canto/centro | perfil radial r0 → r9 |
|---|---|---|---|
| `g2-off` | `NeuralUplift=0` | 100,00% | 128,0 → 127,1 |
| `g2-semfeeder` | `enabled=0` no `dlss5-feed.cfg` | 100,00% | 128,0 → 127,1 |

Está descartado. O que muda a imagem é a rede.

---

## 3. Descartado em seguida: vinheta

Este é o ponto que inverte a hipótese de partida. **Cinza RGB 128 uniforme**, rede ligada com
o `.ini` de hoje:

| região | luma medida | vs. centro |
|---|---|---|
| centro | 123,60 | — |
| bordas (média das 4) | 123,71 | +0,09% |
| **cantos (média dos 4)** | **123,85** | **+0,21%** |
| faixa radial mais externa | 124,10 | +0,40% |

Os cantos saem **mais claros** que o centro. A imagem inteira escureceu 3,4% (128 → 123,6),
mas **de forma uniforme**. Um mapa da razão pixel a pixel, com o desvio médio removido e
amplificado 40×, não mostra nenhum padrão radial — só ruído de ±0,3%
(`mapa-cinza-uniforme.png`).

Segunda prova, imune a qualquer efeito de nível: no **gradiente**, comparar faixas de altura
diferente **na mesma coluna** — mesma entrada exata, só a posição vertical muda.

| coluna | topo / meio | base / meio |
|---|---|---|
| escura (entrada ≈ 40) | 100,06% | 99,94% |
| do meio (entrada ≈ 128) | 100,50% | 99,63% |
| clara (entrada ≈ 216) | 100,67% | 98,49% |

O maior desvio de posição em toda a imagem é 1,5%, na borda de baixo da coluna clara. Nada
que se pareça com os 10% que o olho vê.

### Por que o perfil radial do gradiente *parece* mostrar vinheta

Vale registrar a armadilha, porque ela quase fechou o diagnóstico errado. No gradiente com o
`.ini` de hoje o perfil radial cai de 124,6 no centro para 118,5 no canto — 5,3%, com cara de
vinheta de manual. **É artefato da métrica.** Todo anel radial tem a mesma *média* de entrada
(128), mas não a mesma *distribuição*: os anéis de fora pegam os extremos claro e escuro, os
de dentro só o meio. Com uma curva de tom não-linear, `média(g(v))` muda de anel para anel
mesmo sem nenhuma vinheta. O cinza uniforme e o teste vertical acima não têm essa brecha.

---

## 4. O que a rede faz de verdade — a curva de tom

Do gradiente, faixa central de altura, saída média por nível de entrada. `g2-off` é a régua:

| combinação (`[RenoDX.DLSS5]`) | entrada 40 | 80 | 128 | 176 | **216 (alta-luz)** |
|---|---|---|---|---|---|
| `NeuralUplift=0` (referência) | −0,1% | −0,1% | 0,0% | 0,0% | **0,0%** |
| **`.ini` de hoje** (`NRLocalTone=2`, `NRStyle=1`) | +14,7% | +5,4% | −2,8% | −6,6% | **−10,5%** |
| `NRGlobalTone=0` | +14,7% | +5,3% | −2,9% | −6,6% | **−10,6%** |
| `NREnableUpscaling=0` | +14,7% | +5,3% | −2,9% | −6,6% | **−10,7%** |
| `NRIntensity=0.4` | +7,4% | +2,8% | −1,6% | −3,6% | **−5,7%** |
| `NRLocalTone=1` | +10,5% | +2,2% | −3,9% | −7,2% | **−8,3%** |
| `NRLocalTone=0.5` | +5,7% | +1,0% | −2,0% | −3,8% | **−4,1%** |
| `NRLocalTone=0.3` | +4,4% | +0,5% | −1,3% | −2,3% | **−2,4%** |
| `NRLocalTone=0` | +1,0% | −0,1% | +0,3% | +0,2% | **+0,1%** |
| `NRTransferStrength=0.8` | +14,7% | +5,3% | −3,0% | −6,4% | **−10,7%** |
| `NRIntensity=2` | +19,8% | +7,4% | −3,8% | −8,8% | **−14,5%** |
| **`NRStyle=0`** (Default) | +7,3% | +3,4% | +1,2% | +0,6% | **−0,9%** |
| `NRStyle=2` (Cinematic) | +8,0% | −0,3% | +0,8% | +1,9% | **−0,4%** |
| `NRStyle=0` + `NRLocalTone=1` | +4,8% | +1,7% | +0,7% | −0,2% | **−1,0%** |
| `NRStyle=0` + `NRLocalTone=0.5` | +2,4% | +1,1% | +0,5% | −0,1% | **−0,4%** |
| `.ini` do pacote `Portable` (`NRIntensity=2`, `NRLocalTone=2`, **`NRStyle=0`**) | +8,1% | +5,7% | +2,3% | +0,8% | **−1,5%** |

Leitura em uma linha: hoje, no `.ini` do `DLSS5ForAll-AMD`, **tudo acima do meio-tom perde
brilho e tudo abaixo ganha**. É compressão de contraste, não vinheta.

E o gatilho principal é o **`NRStyle`**, não o `NRLocalTone`. A última linha da tabela é a
prova mais forte: o `.ini` do pacote `Portable` tem os dois sliders de tom no valor mais
agressivo possível (`NRIntensity=2`, o dobro do `0.74` do AMD, e `NRLocalTone=2`) e, mesmo
assim, só perde **1,5%** na alta-luz — porque está em `NRStyle=0`. Trocar apenas o estilo, de
`1` para `0`, leva a perda de **10,5% para 0,9%** sem mexer em mais nada.

Três chaves não mexem em nada: `NRGlobalTone` (0, 1 ou 2 dá o mesmo resultado até a segunda
casa), `NREnableUpscaling` (aqui a entrada já é 1920×1080 e o log diz `NR upscaling is not
applicable`) e `NRTransferStrength` (ver §8).

---

## 5. A prova final: a curva sozinha explica a foto inteira

Teste montado assim: extrair a curva do **gradiente** (que não tem termo de posição nenhum),
aplicá-la pixel a pixel na **foto com o NR desligado**, e comparar a previsão com a **foto
medida com o NR ligado**, nas nove regiões.

| região | NR desligado | NR ligado (medido) | previsto só pela curva | resíduo |
|---|---|---|---|---|
| centro | 78,38 | 82,02 | 81,15 | +0,87 |
| topo | 173,82 | 160,66 | 161,10 | −0,44 |
| base | 60,41 | 63,81 | 63,95 | −0,14 |
| esquerda | 186,88 | 175,35 | 172,27 | +3,08 |
| direita | 180,68 | 170,99 | 167,97 | +3,01 |
| canto sup. esq. | 174,20 | 165,49 | 162,96 | +2,53 |
| canto sup. dir. | 169,80 | 161,95 | 159,41 | +2,54 |
| canto inf. esq. | 193,11 | 177,22 | 176,37 | +0,86 |
| canto inf. dir. | 186,85 | 172,10 | 172,26 | −0,16 |

Uma curva global, **sem um único termo de posição**, acerta as nove regiões com erro máximo
de 3,1 níveis em 60–193. E o resíduo dos quatro cantos é **positivo**: a rede escurece os
cantos um pouco *menos* do que a curva pura previa. Não sobra nada para uma vinheta explicar.

O efeito na foto é grande porque o conteúdo colabora: o centro é escuro (78) e a periferia é
clara (169–193). A curva levanta o centro em 4,6% e derruba os cantos em 6,5%. A razão
canto/centro cai de 230,9% para 206,3% — 24,6 pontos. É isso que se vê como vinheta.

---

## 6. Matriz completa — razão canto/centro por combinação

Referência de cada fonte = a mesma imagem com `NeuralUplift=0`.

### foto real (centro escuro, periferia clara — é onde o efeito aparece)

| rodada | `[RenoDX.DLSS5]` | centro (escuro) | cantos (claros) | canto/centro | vs. referência |
|---|---|---|---|---|---|
| `f2-off` | `NeuralUplift=0` | 78,38 | 180,99 | 230,93% | — |
| `f2-padrao` | `.ini` de hoje | 82,02 | 169,19 | 206,27% | **−24,66 pontos** |
| `f2-lt1` | `NRLocalTone=1` | 80,15 | 168,80 | 210,59% | −20,34 |
| `f2-lt05` | `NRLocalTone=0.5` | 79,98 | 174,72 | 218,45% | −12,48 |
| `f2-lt0` | `NRLocalTone=0` | 79,38 | 181,49 | 228,65% | **−2,28** |
| `f2-lt1-st0` | `NRStyle=0` + `NRLocalTone=1` | 76,17 | 183,86 | 241,38% | +10,45 |
| `f2-st0` | `NRStyle=0` | 76,10 | 185,54 | 243,82% | +12,89 |

As duas últimas linhas têm sinal **positivo**: com `NRStyle=0` os cantos claros ficam mais
claros que a própria referência e o centro escuro fica um pouco mais escuro. É o **oposto** da
queixa — mais contraste local, não menos. Vale saber o porquê: no gradiente a vizinhança de
cada pixel é quase uniforme, então o operador local age como curva global; nesta foto o centro
é uma forma escura cercada de céu claro, e o operador aprofunda essa diferença. `NRStyle=1`
comprime, `NRStyle=0` expande. Nenhum dos dois é vinheta.

### cinza uniforme (a sonda de posição — aqui nada deveria mudar entre centro e canto)

| rodada | `[RenoDX.DLSS5]` | centro | cantos | canto/centro |
|---|---|---|---|---|
| `c2-off` | `NeuralUplift=0` | 128,00 | 128,00 | 100,00% |
| `c2-padrao` | `.ini` de hoje | 123,60 | 123,85 | **100,21%** |
| `c2-lt1` | `NRLocalTone=1` | 128,00 | 128,00 | 100,00% |
| `c2-lt0` | `NRLocalTone=0` | 128,04 | 128,07 | 100,02% |
| `c2-lt1-st0` | `NRStyle=0` + `NRLocalTone=1` | 128,10 | 128,64 | 100,42% |

Nenhuma combinação escurece o canto de um campo liso. Confirma a seção 3.

### gradiente (a sonda de nível — o número aqui é da curva, não de posição; ver §3)

| rodada | `[RenoDX.DLSS5]` | centro | cantos | canto/centro |
|---|---|---|---|---|
| `g2-off` | `NeuralUplift=0` | 128,00 | 128,00 | 100,00% |
| `g2-semfeeder` | Feeder desligado | 128,00 | 128,00 | 100,00% |
| `g2-padrao` | `.ini` de hoje | 124,73 | 119,51 | 95,82% |
| `g2-padrao-b` | idem, janela normal (controle) | 124,51 | 119,50 | 95,98% |
| `g2-gt0` | `NRGlobalTone=0` | 124,56 | 119,46 | 95,90% |
| `g2-gt1` | `NRGlobalTone=1` | 124,69 | 119,38 | 95,75% |
| `g2-ups0` | `NREnableUpscaling=0` | 124,60 | 119,42 | 95,84% |
| `g2-ts08` | `NRTransferStrength=0.8` | 124,48 | 119,39 | 95,92% |
| `g2-int2` | `NRIntensity=2` | 123,44 | 116,23 | 94,16% |
| `g2-int04` | `NRIntensity=0.4` | 126,12 | 123,26 | 97,73% |
| `g2-lt1` | `NRLocalTone=1` | 123,05 | 121,26 | 98,54% |
| `g2-lt05` | `NRLocalTone=0.5` | 125,46 | 124,77 | 99,46% |
| `g2-lt03` | `NRLocalTone=0.3` | 126,45 | 126,27 | 99,85% |
| `g2-lt0` | `NRLocalTone=0` | 128,40 | 128,37 | 99,98% |
| `g2-lt0gt0` | `NRLocalTone=0` + `NRGlobalTone=0` | 128,30 | 128,40 | 100,08% |
| `g2-st0` | `NRStyle=0` | 129,55 | 128,41 | 99,12% |
| `g2-st2` | `NRStyle=2` | 129,22 | 128,68 | 99,58% |
| `g2-lt1-st0` | `NRStyle=0` + `NRLocalTone=1` | 128,93 | 128,00 | 99,28% |
| `g2-lt05-st0` | `NRStyle=0` + `NRLocalTone=0.5` | 128,76 | 128,10 | 99,49% |
| `g2-portable` | `.ini` do pacote `Portable` | 130,78 | 128,05 | 97,92% |

`g2-padrao-b` é a rodada de controle em janela normal contra `g2-padrao` em modo sobreposto:
95,98% contra 95,82%, diferença de 0,16 ponto. Os dois modos medem a mesma coisa.

Em 28 rodadas, a seção `[RenoDX.DLSS5]` relida do `.ini` 6 s depois de abrir tinha sempre a
chave pedida, e só ela.

---

## 7. De onde veio o `2`

`NRLocalTone` é a força do operador de *tone mapping local* da rede
(`DLSSNR.LocalToneStrength` no `nvngx_dlssnr.dll`). Faixa `0,00`–`2,00`, **padrão do add-on
`1,00`** (`amd/feeder-amd/src-feeder/src/dlss5-feed32.cpp:1724`).

| arquivo | `NRLocalTone` | `NRStyle` |
|---|---|---|
| `DLSS5ForAll-Portable\bin\ReShade.ini` | **2** | 0 |
| `DLSS5ForAll-AMD\bin\ReShade.ini` | **2** | **1** |
| `setup-dlss5.ps1` e `setup-dlss5.auto.ps1` gravam | 1 | 0 |
| `amd\empacotar.ps1` normaliza no pacote publicado | 1 | 0 |
| perfil "suave" da 2ª camada do 2x (`amd\main.cpp:2954`) | 0.6 | 1 |

Os scripts de instalação e o empacotador gravam `NRLocalTone=1` e `NRStyle=0`. O
`DLSS5ForAll-AMD` vivo está com `2` e `1` — os dois trocados, e os dois na direção que
comprime. Isso veio de ajuste na overlay do ReShade, salvo pelo `AutoSavePreset=1`. **O que
o usuário está vendo não é o que o pacote publicado entrega** — é uma afinação local que
ficou gravada.

Note também que o `empacotar.ps1` só *substitui* linhas que já existem e depois recusa o
pacote se alguma faltar (`throw "ReShade.ini sem $par"`). Essa guarda pega a chave ausente,
mas não pega a chave presente com valor errado num `.ini` de trabalho — que é exatamente o
caso aqui.

Isso também bate com o `amd\docs\brilho-witcher3.md`, que já tinha diagnosticado os quatro
sliders neurais no máximo como causa do "claro demais" no Witcher 3. É o mesmo `.ini`, o
mesmo `2`, visto por outro sintoma.

---

## 8. Recomendação

O objetivo é parar de derrubar as altas-luzes sem perder o que o NR faz de bom (levantar
sombra e revelar detalhe). Em ordem de preferência:

### Primeira escolha — uma linha, em `DLSS5ForAll-AMD\bin\ReShade.ini`

```ini
[RenoDX.DLSS5]
NRStyle=0
```

| | sombras (entrada 40) | meio-tom (128) | altas-luzes (216) |
|---|---|---|---|
| hoje (`NRStyle=1`) | +14,7% | −2,8% | **−10,5%** |
| com `NRStyle=0` | +7,3% | +1,2% | **−0,9%** |

Tira o escurecimento da borda **e** mantém o ganho nas sombras, que é metade do porquê de
ligar o NR. `NRStyle=1` ("Natural") é justamente o estilo que mais comprime; `0` é o
*Default* da rede, e é o que os nossos scripts de instalação e o empacotador já gravam. O
pacote `DLSS5ForAll-Portable` já está em `0` — quem precisa dessa linha é o `DLSS5ForAll-AMD`.

**Diga-se o que muda junto:** na foto de teste, `NRStyle=0` não devolve a imagem ao ponto
neutro, ele passa do outro lado — os cantos claros ficam 2,5% **mais** claros que a
referência e o centro escuro 2,9% mais escuro (razão canto/centro 243,8% contra 230,9% da
referência). É mais contraste local, não menos. A queixa some; a imagem fica mais "dura".
Quem quiser esse ganho de sombra sem esse contraste a mais, some a linha de baixo.

### Segunda linha, recomendada junto — devolver o tom local ao padrão

```ini
NRStyle=0
NRLocalTone=1
```

Altas-luzes −1,0%, sombras +4,8% (razão canto/centro na foto: 241,4%). `1` é o padrão do
add-on e o valor que `setup-dlss5.ps1` e `amd\empacotar.ps1` já gravam; o `2` de hoje é
resíduo de ajuste na overlay. Vale corrigir de qualquer jeito, pelo `brilho-witcher3.md`.

### Se o objetivo for "não mexer no brilho, ponto"

```ini
NRLocalTone=0
```

É a única combinação que devolve a foto praticamente ao ponto da referência: razão
canto/centro 228,7% contra 230,9%, com o centro e os cantos dentro de 1,3% do original. A
curva de tom vira identidade nos cinco níveis. Em troca, some também o levantamento de
sombra. **A estrutura da rede continua agindo** (`NRLocalStructure` e `NRSkinStructure` são
chaves separadas, e ficam onde estão), então o NR não vira enfeite — só para de mexer no
brilho.

Meio-termo, se `0` tirar demais: `NRLocalTone=0.5` (altas-luzes −4,1%, razão 218,5%) ou
`NRLocalTone=0.3` (−2,4%).

### O que **não** fazer

- **`NRLocalTone=0`** zera o estágio de tom por completo: a curva vira identidade nos cinco
  níveis. Tira a vinheta, mas tira junto o levantamento de sombra, que é metade do porquê de
  ligar o NR. Só use se o objetivo for "nenhuma mudança de brilho, ponto".
- **`NRGlobalTone`** não faz diferença nenhuma aqui (0, 1 e 2 dão o mesmo número). Não gaste
  tempo com ela.
- **`NRTransferStrength=0.8`** — testado, **não faz nada nesta build**. Pela matemática do
  codec (`upgraded = lerp(original, neural, TransferStrength)`, `brilho-witcher3.md` §1) a
  alta-luz deveria ir de −10,5% para cerca de −8,4%. Medido: **−10,7%**, ou seja, o mesmo do
  padrão até a casa do ruído, com a chave gravada no `.ini` e o add-on tendo lido o arquivo.
  Fica registrado: essa chave não é um dial utilizável hoje. E, mesmo que fosse, seria o
  martelo errado — dilui a rede inteira para corrigir um estágio só.
- **`NREnableUpscaling`** não tem efeito na cor aqui: a entrada já está em 1920×1080 e o log
  registra `NR upscaling is not applicable`.

### Como aplicar

Com o app **fechado** (`AutoSavePreset=1` reescreve o `ReShade.ini` na saída), edite
`bin\ReShade.ini`, seção `[RenoDX.DLSS5]`. As chaves já existem no arquivo; é só trocar o
valor.

---

## 9. Plano B, caso um dia isso não saia por chave — proposta, NÃO aplicada

Não é preciso hoje. Fica registrado para o caso de aparecer um preset da rede em que a
compressão não desligue.

O lugar seria o `DLSS5_Sharpen.fx`, último passe antes do grading, que já tem a cor final na
mão. Mas note que a correção certa **não é radial** — o que a rede faz é uma curva, então a
compensação certa é a curva inversa, não uma máscara de borda. Uma máscara radial acertaria a
média da borda e erraria em qualquer cena com o meio claro e a borda escura (interior à noite,
túnel, cena com foco de luz central), onde ela *criaria* uma vinheta que não existia.

Se ainda assim se quiser a máscara radial, a forma mínima é esta, antes do `return` de
`PS_DLSS5_Sharpen`:

```hlsl
uniform float VignetteFix <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 0.15; ui_step = 0.005;
    ui_category = "Nitidez";
    ui_label = "Compensar escurecimento de borda";
> = 0.0;

if (VignetteFix > 0.0)
{
    const float  ar = BUFFER_WIDTH * ReShade::PixelSize.y;          // largura/altura
    const float2 d  = (texcoord - 0.5) * float2(ar, 1.0);
    const float  r2 = saturate(dot(d, d) / (0.25 * (ar * ar + 1.0)));  // 0 centro, 1 canto
    c = saturate(c * (1.0 + VignetteFix * r2));
}
```

Quadrático em `r` (não toca o centro), multiplicativo (não levanta o preto), normalizado pela
proporção da tela (senão em 21:9 a correção chega cedo nos lados). **Recomendação: não fazer.**
A chave do `.ini` resolve, é reversível e não inventa comportamento novo.

---

## 10. Arquivos

Imagens e dados brutos ficaram em
`C:\Users\PCDOFAFA\AppData\Local\Temp\claude\C--Users-PCDOFAFA-Desktop-dlss5-for-all-portable-DLSS5ForAll-Portable\0a9f9fd9-158c-4b4c-b5dd-bdc2b95ee0e1\scratchpad\vinheta-nr\`:

- `entrega\curva-de-tom.png` — **a figura principal**: ganho da rede por nível de entrada,
  uma linha por combinação. Mostra a compressão de hoje (+21% na sombra, −10% na alta-luz) e
  o que cada correção faz com ela.
- `entrega\mapa-cinza-uniforme.png` — a prova de que não há vinheta: campo liso, razão
  NR/referência com o desvio médio removido, amplificada 40×. Sem padrão radial, só ruído.
- `entrega\comparativo-foto.png` — canto superior esquerdo · centro · canto inferior direito
  da foto, cinco linhas: referência, `.ini` de hoje, `NRLocalTone=0`, `NRStyle=0` +
  `NRLocalTone=1`, `NRStyle=0`. Na segunda linha os cantos estão visivelmente mais apagados.
- `entrega\foto-prevista-so-pela-curva.png` — a foto prevista aplicando só a curva do
  gradiente; é o que a seção 5 compara.
- `entrega\mapa-residuo-foto.png` — o resíduo da seção 5 em mapa. O céu da periferia sai
  cinza uniforme (a curva acerta lá); o que sobra está nas bordas de conteúdo do centro, que
  é o operador local agindo — não posição.
- `runs\<rodada>\saida.png` — o dump de cada rodada; `medida.json` e `info.json` ao lado, com
  as nove regiões, o perfil radial e a configuração efetiva lida do `.ini` depois de abrir.
- `runs\<rodada>\recorte.png` — canto superior esquerdo · centro · canto inferior direito.
