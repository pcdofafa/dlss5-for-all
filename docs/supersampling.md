# Resolução interna — rodar a cadeia num tamanho diferente do da captura

A pergunta original do usuário: *o Neural Rendering fica melhor quanto maior a resolução da
fonte — então dá para aumentar a qualidade do filtro simulando mais pixels na tela, mesmo que
a resolução não suba?* Depois: *e para baixo também, para ganhar fps.*

Ou seja: capturar em `W×H`, **rodar a cadeia inteira** (Kernel → DLSS5_Feed → DLAA → Neural →
Grade → Sharpen) num backbuffer `k·W × k·H`, e voltar ao tamanho da janela na apresentação.
Faixa testada: **25% a 300%**.

Medido em 2026-09-02 23:19 → 2026-09-03 00:20. RTX 4080, monitor a 99 Hz.

---

## Resumo

| | veredito |
|---|---|
| **funciona?** | sim, na inicialização. **Não** em runtime — ver §9, é um bug do add-on e ele **já existe hoje** |
| **teto da máquina** | a feature do DLSS sobe até **7680×4320**; falha em **10320×4320** (ultrawide a 300%) |
| **ganho acima de 100%** | real, pequeno e concentrado em **texto** e em **menos grão**; nunca em aliasing |
| **abaixo de 100%** | 75% é utilizável; 50% e 25% não. Um passe EASU+RCAS no add-on quase dobraria o detalhe em 75% (§8.3) |
| **degraus entregues** | **75% (com aviso) · 100% (padrão) · 150%** — os cortes estão justificados no §8.5 |

O ganho acima de 100% **não é o neural ficando melhor**. É a cadeia ficando menos destrutiva,
porque os kernels dela são de tamanho fixo em pixels (§6.2). Isso não mudou desde a primeira
rodada de medições.

Figuras: `docs\img\supersampling-crops.png` (a faixa inteira, o que a tela mostra) e
`docs\img\supersampling-neural.png` (neural ligado × desligado em cada escala).

---

## 1. O que foi construído

`--escala k`, de 0,25 a 3,0. Com `k ≠ 1`:

- o swapchain nasce em `k·W × k·H` (`Renderer::w/h`) e passa a existir `Renderer::srcW/srcH`
  com a resolução da **fonte**, que continua mandando no tamanho da janela;
- o `kShader` ganhou três caminhos, escolhidos sozinho pelo `Renderer`:

| `UpFilter` | quando | o que faz |
|---|---|---|
| 0 | escala = 100% | `tex.Sample` — o caminho de sempre, byte por byte |
| 1 | acima de 100% | Catmull-Rom separável em 9 amostras |
| 2 | acima de 100% (**padrão**) | ampliador direcional com kernel anisotrópico — §7 |
| 3 | abaixo de 100% | redução por área, grade de taps bilineares cobrindo a pegada |

- a cadeia inteira roda em `k·W × k·H` **sem saber**. Confirmado no `dlss5-feed.log`: com
  `--escala 2` e fonte 1280×720 ele registra `frame N delivered (2560x1440 at 100% ->
  2560x1440)`. Isso **não** é o `work_resolution` do Feeder — é o nosso backbuffer;
- quem volta ao tamanho da janela é o **DXGI**, na apresentação (§5).

O ampliador direcional é código nosso (MIT), no espírito do EASU do FSR mas escrito aqui: numa
borda, borrar *ao longo* dela não custa nitidez e mata o degrau; borrar *através* é que
estraga. Então o kernel vira uma elipse alinhada com a borda — larga na direção dela, estreita
na perpendicular — e em área lisa volta a ser um cúbico comum. Fecha com clamp no min/max do
2×2 central, que é o que segura o ringing. O realce opcional (`--escala-realce`) é um unsharp
limitado pelo mesmo min/max, no espírito do RCAS mas **não** é o RCAS (que roda num passe
próprio em espaço de saída; aqui é um reaproveitamento dos taps que já estão em registrador).

**Um caminho não existe:** fazer a volta ao tamanho da janela num passe nosso. A cadeia do
ReShade roda dentro do `Present`, depois de tudo que o app desenha — não há como inserir um
passe *depois* dela sem ser por dentro do add-on. Com o backbuffer em `k·W`, a reamostragem
final é necessariamente do DXGI, nas duas direções. É por isso que o EASU pedido para o
caminho abaixo de 100% **não pôde ser usado onde ele decidiria**: ele estaria antes da cadeia,
não depois. §5.3 mede quanto isso custa.

---

## 2. Metodologia

**Alvo.** `scratchpad\ss\gametarget.cpp`, escrito para isto. O `lattarget.exe` troca de cor a
cada frame (ele mede *latência*); para medir *qualidade* eu preciso do contrário: um frame
idêntico ao anterior, sempre, para que duas execuções sejam comparáveis pixel a pixel. Cena
gerada uma vez na CPU com LCG de semente fixa. Sete regiões: estrela de Siemens (48 raios),
zone plate `sin(k·r²)`, pele com grão de 1,5/255, cinco chapados lisos, barras de
32/16/8/4/3/2/1 px, textura fina e HUD com texto de 20/14/11 px. Janela `WS_POPUP` e
DPI-aware: sem isso a captura vinha 1276×711 sob DPI 150% e o alinhamento se perdia.

**Cadeia.** Cópia isolada em `scratchpad\ss\rs\`. **A pasta do projeto não foi tocada** — nem
`main.cpp`, nem `ReShade.ini`, nem os logs. É o que me deixou ligar/desligar o neural sem
atrapalhar o app que o usuário está usando.

**Duas medidas de saída, e a diferença importa:**

- **backbuffer** — `DLSS5_DUMP` grava a saída real da cadeia, antes da apresentação, e eu
  reduzo com Lanczos-3 (`sstool down`). É a redução **ideal**: isola o que a *cadeia* faz.
- **tela** — um segundo app captura a **janela** do primeiro. A captura WGC de janela entrega
  a superfície já composta, no tamanho da janela: exatamente a saída do reamostrador do DXGI.
  O segundo roda numa pasta sem `dxgi.dll` (cadeia nenhuma), em escala 1.
  **Controle:** em 100% (sem reamostragem) esse caminho reproduz o dump direto com erro de
  0,01 — medido duas vezes, em execuções diferentes: hud_hp 14,65 / 14,65, hud_sb 47,77 /
  47,86. O método é confiável e repetível.

Dump por **tempo** (`DLSS5_DUMP_MS`, 20 s), não por número de frame: cada escala roda num fps
diferente e comparar exige o mesmo instante de aquecimento. 20 s também é o que garante que o
banner de abertura do ReShade já saiu do backbuffer e que o feed passou do `warmup_rebuild`.

**Referência de verdade.** A estrela e o zone plate são analíticos, então gerar a cena em
2560×1440 e reduzir com Lanczos dá a imagem 1280×720 **verdadeira** — o teto que nenhum filtro
pode ultrapassar. É contra ela que se mede aliasing. (Só vale para essas duas regiões: o texto
e as barras têm tamanho fixo em pixels e não escalam.)

**Métricas** (`sstool met` / `sstool linha`): `hp` = desvio padrão do sinal menos a média 3×3
(energia de detalhe); `sobel` = magnitude média do gradiente; `ruido` = desvio padrão dentro
dos cinco chapados; `mtf1px` = contraste de Michelson na barra de 1 px.

---

## 3. Teto da máquina — até onde a cadeia sobe

Cada linha é uma execução real, com o `dlss5-feed.log` conferido.

| fonte | escala | backbuffer | Mpx | feature do DLSS |
|---|---|---|---|---|
| 1920×1080 | 250% | 4800×2700 | 13,0 | **ok** |
| 1920×1080 | 300% | 5760×3240 | 18,7 | **ok** |
| 2560×1440 | 250% | 6400×3600 | 23,0 | **ok** |
| 2560×1440 | 300% | 7680×4320 | 33,2 | **ok** |
| 3440×1440 | 300% | 10320×4320 | 44,6 | **FALHA** |

A falha é explícita:

```
[feed] Output: D3D12->D3D11 path failed 0x80070057, trying the other direction
[feed] CreateFeature failed 0xBAD00005 (InvalidParameter)
[feed] failure: resource build
stopped: repeated failures. The game renders normally.
```

O limite do D3D11 (16384 por lado) **não** é o que morde: 10320 e 4320 cabem folgados. Quem
recusa é o NGX.

> **E o fps continuou em 78.** Com a cadeia morta o app apresenta mais rápido do que nunca —
> ou seja, **um watchdog que só olha fps nunca pegaria esse caso**. É por isso que a rede de
> segurança do §9 lê também o que o Feeder escreveu.

Onde exatamente está o limite entre 33,2 e 44,6 Mpx eu não estreitei (exigiria alvos do
tamanho do monitor, com o usuário trabalhando). O que está verificado: **sobe em 7680×4320,
não sobe em 10320×4320.** O painel usa isso como teto estático e o watchdog cobre o resto,
porque outra máquina pode aguentar menos.

---

## 4. Custo — fps, latência, VRAM

Fonte = janela, neural ligado, sem v-sync. `PRESENT→TELA` é a fila do DXGI/DWM, medida como em
`docs\latencia.md`. `chegada` é a taxa do **alvo** — é o fps que o jogo perde para a cadeia.

| escala | backbuffer (1080p) | chegada | **present** | **PRESENT→TELA** méd/p95 | backbuffer (1440p) | chegada | **present** | **PRESENT→TELA** |
|---|---|---|---|---|---|---|---|---|
| 50% | 960×540 | 100,0 | **100,0** | 15,8 / 19,5 | 1280×720 | 100,0 | **100,0** | 17,4 / 20,6 |
| 75% | 1440×810 | 100,0 | **100,0** | 17,3 / 20,7 | 1920×1080 | 100,2 | **100,2** | 20,9 / 30,9 |
| 100% | 1920×1080 | 100,0 | **89,5** | 21,3 / 27,6 | 2560×1440 | 99,8 | **99,5** | 23,0 / 28,3 |
| 125% | 2400×1350 | 100,0 | **100,0** | 23,7 / 31,4 | 3200×1800 | 100,0 | **63,2** | 26,7 / 33,4 |
| 150% | 2880×1620 | 85,3 | **80,2** | 26,6 / 34,9 | 3840×2160 | 94,0 | **47,0** | 30,7 / 38,3 |
| 200% | 3840×2160 | 55,5 | **48,1** | 34,2 / 43,4 | 5120×2880 | 26,6 | **26,6** | 86,6 / 93,5 |
| 250% | 4800×2700 | 23,3 | **23,0** | 69,1 / 103,2 | 6400×3600 | 16,5 | **15,9** | 122,3 / 182,2 |
| 300% | 5760×3240 | 20,0 | **19,8** | 93,3 / 139,4 | 7680×4320 | 10,5 | **10,5** | — |

VRAM do processo (contador `GPU Process Memory` do Windows), fonte 1920×1080:

| escala | 50% | 75% | 100% | 150% | 200% | 250% | 300% |
|---|---|---|---|---|---|---|---|
| VRAM | 540 MiB | 694 MiB | 915 MiB | 1533 MiB | 2356 MiB | 3527 MiB | 4862 MiB |

**Onde o present cai abaixo de 20 fps:** em 1080p, só em **300%** (19,8). Em 1440p, já em
**250%** (15,9) — e 300% desce a 10,5.

Duas leituras que importam mais que o nosso fps:

**A coluna `chegada` cai.** Em 1080p ela vai de 100,0 para 55,5 (200%), 23,3 (250%) e 20,0
(300%). Essa coluna é a taxa do alvo: **o jogo perde até 80% do fps** para a nossa cadeia. Numa
RTX 4080.

**Abaixo de 100% a latência melhora de verdade:** 21,3 → 17,3 (75%) → 15,8 ms (50%) em 1080p.
É o único ganho real dessa metade da faixa.

**Engasgo da troca:** o `ResizeBuffers` mais a recriação dos alvos custa **4,3 a 10,0 ms**
(sete medições). Não é ele o problema — ver §9.

---

## 5. A reamostragem do DXGI

### 5.1 Acima de 100%: custa uns 7%

O mesmo backbuffer de 200%, reduzido de dois jeitos: PSNR entre eles **41,1 dB**, diferença
média 1,07/255. Perto, mas sistematicamente mais mole: `hp` −7% a −9% em toda região, `sobel`
−5% a −8%. Em 250% e 300% (comparação limpa, mesmo filtro) o custo aparece no PSNR contra a
verdade: 21,44 → 21,19 (250%) e 21,29 → 20,61 (300%).

### 5.2 O desvio de luma em 250% e 300% **não** é do DXGI

Cheguei a suspeitar que o DXGI sub-amostrava nas escalas altas, porque a luma da estrela sobe
de 86,4 (fonte) para 93,5 / 93,9. Está errado: a **redução ideal do mesmo backbuffer** dá
93,34 e 93,96 — praticamente igual. O desvio vem da **cadeia**, não do reamostrador. É o
neural/DLAA clareando a região de altíssima frequência quando roda em 2,5× e 3×.

### 5.3 Abaixo de 100%: o esticão custa ~20%, mas o buraco é outro

Ampliando o mesmo backbuffer por mim (Catmull-Rom) em vez de deixar o DXGI esticar:

| escala | `hud_hp` ideal | `hud_hp` do DXGI | `hud_hp` em 100% |
|---|---|---|---|
| 75% | 8,08 | 6,77 | **14,65** |
| 50% | 5,78 | 4,31 | **14,65** |
| 25% | 0,74 | 0,75 | **14,65** |

Ou seja: uma ampliação decente recupera ~19% do que o esticão do DXGI perde — mas mesmo com
ampliação perfeita, rodar a cadeia em 75% custa **45% do detalhe do texto**. O buraco é
informação que não foi renderizada, não o filtro de subida. **Um passe EASU/RCAS no add-on não
salvaria os degraus abaixo de 100%** — melhoraria pouco algo que já está ruim por outro
motivo.

---

## 6. Qualidade acima de 100%

Tudo com o filtro direcional, neural ligado, o que a **tela** mostra. `fonte` é a captura sem
cadeia nenhuma.

| medida | fonte | 100% | 125% | 150% | 200% | 250% | 300% |
|---|---|---|---|---|---|---|---|
| ruído em área lisa | 0,000 | 0,868 | 0,673 | **0,585** | 0,969 | 0,572 | **0,437** |
| MTF barra de 1 px | 0,843 | **0,832** | 0,749 | 0,767 | 0,535 | 0,712 | 0,780 |
| `hp` texto do HUD | 22,41 | 14,65 | 11,57 | 14,15 | 15,52 | 17,72 | **20,32** |
| `sobel` texto do HUD | 57,19 | 47,77 | 46,78 | 52,71 | 52,37 | 56,00 | **57,59** |
| `hp` zone plate | 19,54 | 13,78 | 14,48 | 14,73 | 14,17 | 15,63 | **16,14** |
| `hp` estrela | 32,58 | 17,59 | 15,44 | 16,68 | 20,14 | 22,88 | **25,56** |
| `hp` barras | 27,54 | 19,76 | 17,65 | 17,59 | 20,19 | 23,47 | **24,51** |
| `hp` textura fina | 12,59 | **9,95** | 5,68 | 6,14 | 7,76 | 8,20 | 8,82 |
| **PSNR estrela vs. verdade** | — | 21,37 | 21,18 | 21,27 | **21,67** | 21,19 | 20,61 |

### 6.1 O que os números dizem

**Texto e grão melhoram monotonicamente com a escala.** `sobel` do texto sobe de 47,8 (100%)
para 52,7 (150%) e 57,6 (300%); o grão do neural em área lisa cai de 0,868 para 0,437. Isso é
visível a olho na primeira faixa das duas figuras: em 100% a palavra "alcancar" sai borrada e
manchada; de 150% para cima ela volta a ter a forma da fonte.

**Aliasing não melhora em escala nenhuma.** O PSNR contra a verdade fica entre 20,6 e 21,7 em
toda a faixa — 200% ganha 0,30 dB do 100%, e 300% *perde* 0,76 dB. A terceira faixa da figura
confirma: as sete estrelas são praticamente a mesma imagem. Isso não é surpresa e não é
consertável: a entrada é um **frame já finalizado**, o moiré veio assado na captura, e ampliar
depois do fato não cria amostra nova.

**Textura fina nunca volta.** 9,95 em 100%, no máximo 8,82 em 300%. Nenhuma escala recupera.

**125% é pior que 100% em quase tudo** — `hp` do texto 11,57 contra 14,65, textura 5,68 contra
9,95 — e ainda cobra fps. Uma ampliação pequena e não-inteira paga o ciclo de ida e volta sem
comprar escala de cadeia suficiente para compensar.

### 6.2 O ganho não é do neural

Ligado contra desligado **na mesma escala** (`docs\img\supersampling-neural.png`), o texto
praticamente não muda: 14,73 → 14,64 em 100%; 16,99 → 16,38 em 200%. O que muda é a escala:
100% sem neural já dá 14,73, e 150% sem neural já dá 15,89.

Quem estragava o texto era o **Kernel/DLAA rodando em 100%**. Esses efeitos medem o mundo em
pixels; rodando em `k·W`, o mesmo kernel cobre `1/k` da imagem e borra `1/k` do que borrava. O
supersampling dilui o estrago da cadeia — não melhora o neural.

O neural, esse, faz **mais** coisa em escala maior sem fazer coisa melhor: a diferença média
absoluta entre ligado e desligado vai de 5,66 (100%) para 9,22 (200%), e na textura fina de
8,15 para 20,78 — sem que a energia de detalhe suba junto e com o brilho médio igual. Ele está
reescrevendo a trama, não recuperando trama. Numa cena com movimento isso é matéria-prima de
*texture crawl*.

---

## 7. Qual filtro de ampliação: Catmull-Rom ou direcional

A/B em 200%, mesma cadeia, mesma sessão:

| | ruído | MTF 1px | `hp` texto | `sobel` texto | `hp` estrela | `hp` barras | `hp` textura | **PSNR estrela vs. verdade** |
|---|---|---|---|---|---|---|---|---|
| Catmull-Rom | 1,054 | 0,481 | 15,28 | **56,59** | 16,64 | 15,13 | **9,15** | 20,60 |
| **direcional** | **0,969** | **0,535** | **15,52** | 52,37 | **20,14** | **20,19** | 7,76 | **21,67** |

O direcional ganha onde importa: **+1,07 dB contra a verdade** — ou seja, está mais perto da
imagem correta *e* com mais estrutura, que é a combinação que descarta a hipótese de "só
parece mais nítido". Recupera boa parte da estrutura fina que o Catmull-Rom perdia (barras
+33%, 1 px +11%) e injeta menos grão. Perde em realce de borda de texto e em textura fina.

**É o padrão.** Em 150% a vantagem é menor e mista (1 px 0,767 contra 0,682; texto 52,7 contra
57,1), mas em 200% é clara, e é justamente em 200% que o Catmull-Rom mais sofria.

---

## 8. Qualidade abaixo de 100%

Aqui a cadeia roda **menor** e a imagem é ampliada de volta para o tamanho da janela. Quem
amplia é o DXGI (§1) — o nosso passe não pode ficar depois da cadeia. Então há duas perguntas
separadas, e a segunda é a que decide se vale mexer no add-on.

### 8.1 O que é nosso hoje: a redução na ENTRADA

Essa metade **é** nossa: o PS reduz a captura para o backbuffer pequeno. Medi contra o sampler
bilinear cru (`--escala-filtro -1`), e o primeiro resultado foi contra mim: a minha grade de
taps estava **pior** que o bilinear em 75% (`hp` do texto 6,77 contra 7,72; linha de 1 px
0,324 contra 0,731). O bug era a largura da pegada — os taps cobriam o dobro do que deviam.

Corrigido (taps espaçados de `Ratio/n` texels, e um único tap quando `Ratio < 1,5`), medido no
backbuffer para ser determinístico, os dois ampliados por mim com o mesmo EASU:

| escala | | ruído | MTF 1px | `hp` texto | `hp` barras | `hp` textura | `dabs` |
|---|---|---|---|---|---|---|---|
| 75% | antes | 0,674 | 0,426 | 12,37 | 16,12 | 4,34 | 10,71 |
| 75% | **agora** | 0,691 | **0,871** | **13,68** | **17,74** | **5,96** | **10,08** |
| 50% | antes | 1,374 | 0,865 | 11,89 | 19,03 | 5,99 | 13,92 |
| 50% | agora | 1,527 | 0,872 | 11,86 | 19,02 | 5,86 | 14,03 |
| 25% | antes | 3,019 | 0,392 | 5,25 | 8,80 | 0,52 | 33,53 |
| 25% | agora | 4,641 | 0,479 | 4,72 | 10,52 | 0,90 | **28,85** |

Ganho claro em **75%** (que é justamente o piso candidato), empate em 50%, e em 25% fica mais
perto da fonte mas mais ruidoso. **É uma correção que vale entrar.**

### 8.2 O que a tela mostra hoje, com o esticão do DXGI

| medida | fonte | 25% | 50% | 75% | 100% |
|---|---|---|---|---|---|
| ruído em área lisa | 0,000 | 4,071 | 1,759 | 0,690 | 0,868 |
| MTF barra de 1 px | 0,843 | 0,225 | 0,517 | 0,732 | **0,832** |
| `hp` texto do HUD | 22,41 | 1,27 | 4,27 | 7,71 | **14,65** |
| `sobel` texto do HUD | 57,19 | 22,48 | 37,95 | 46,48 | **47,77** |
| `hp` barras | 27,54 | 2,38 | 6,16 | 9,59 | **19,76** |
| `hp` textura fina | 12,59 | 1,20 | 2,18 | 2,88 | **9,95** |

### 8.3 O teto: quanto um passe EASU+RCAS no add-on devolveria

Escrevi o ampliador direcional **e** um RCAS de verdade (limitador de contraste pela cruz de
4 vizinhos, forma do FSR, implementação própria) e apliquei ao mesmo backbuffer, offline.
Não dá para rodar isso no app (§1), mas o número diz se vale construí-lo no add-on. Dose do
RCAS em 0,35 — acima disso a energia passa da fonte (`sobel` do texto 60,1 contra 57,2 da
fonte em dose 0,75, ou seja, realce inventado).

| escala | | MTF 1px | `hp` texto | `sobel` texto | `hp` barras | `hp` textura | `dabs` |
|---|---|---|---|---|---|---|---|
| 75% | hoje (DXGI) | 0,732 | 7,71 | 46,48 | 9,59 | 2,88 | 11,23 |
| 75% | **com EASU+RCAS** | **0,871** | **13,68** | **53,44** | **17,74** | **5,96** | **10,08** |
| 50% | hoje | 0,517 | 4,27 | 37,95 | 6,16 | 2,18 | 14,84 |
| 50% | **com EASU+RCAS** | **0,872** | **11,86** | **47,09** | **19,02** | 5,86 | 14,03 |
| 25% | hoje | 0,225 | 1,27 | 22,48 | 2,38 | 1,20 | 27,33 |
| 25% | com EASU+RCAS | 0,479 | 4,72 | 22,63 | 10,52 | 0,90 | 28,85 |
| — | **100%, para comparar** | 0,832 | 14,65 | 47,77 | 19,76 | 9,95 | 7,77 |

**O ganho é grande e é real:** em 75% o detalhe do texto quase dobra (7,71 → 13,68, contra
14,65 do 100%) e o contraste de linha de 1 px vai de 0,732 para 0,871. Em 50%, as barras
chegam a 19,02 — praticamente o valor de 100%.

Isso corrige o que eu tinha escrito na primeira versão deste documento. Eu havia estimado o
teto com **Catmull-Rom** (`hp` do texto 8,08 em 75%) e concluído que "nem com ampliação
perfeita valeria". O Catmull-Rom é um ampliador ruim para este caso; o direcional + RCAS
entrega quase o dobro. **A insistência no EASU+RCAS estava certa e a minha estimativa
anterior estava errada.**

### 8.4 Mas o texto pequeno não volta — e é isso que fixa o piso

Ampliando os crops do texto a 6× (`scratchpad\ss\out\dose.png`), aparece o que as métricas de
energia não mostram: em 75% as letras têm **fantasma/eco**, e o eco está lá **também sem
RCAS**. Ou seja, não é o realce inventando contorno — o estrago já vem da cadeia rodando em
960×540, e um ampliador melhor só o deixa mais visível. O borrão do DXGI, ironicamente,
esconde o defeito.

É por isso que `dabs` (fidelidade) continua ruim mesmo quando `hp` (energia) melhora: em 75%,
10,08 com EASU contra 7,77 em 100%. A energia volta; a forma da letra, não.

### 8.5 A escada final, com o motivo de cada corte

| degrau | entra? | por quê |
|---|---|---|
| 25% | **não** | destruído nos dois cenários: `hp` do texto 1,27 hoje, 4,72 até com EASU, contra 14,65 |
| 50% | **não** | 29% do detalhe do texto hoje; com EASU as barras voltam, mas o fantasma do texto fica e a textura fica em 59% |
| **75%** | **sim, com aviso** | hoje já é utilizável (texto legível, mais mole); −4 ms de latência e fôlego de GPU. Com o passe no add-on ficaria a 93% do 100% em texto |
| **100%** | **sim, padrão** | — |
| 125% | **não** | medidamente **pior** que 100% (§6.1) e ainda cobra fps |
| **150%** | **sim** | +10% de borda no texto, −33% de grão, aliasing igual ao de 100% |
| 200% | limítrofe | melhor fidelidade medida (PSNR 21,67), mas metade do fps e −36% de contraste em linha de 1 px. Cortado por custo |
| 250% | **não** | 15,9 fps em 1440p; ganho pequeno sobre 200% por 2× o custo |
| 300% | **não** | 19,8 fps em 1080p, 10,5 em 1440p, e é o **único** degrau que piora contra a verdade |

**Entregar: 75% (com aviso) · 100% (padrão) · 150%.**

### 8.6 O mesmo passe consertaria o F11 — e o mesmo bug o bloqueia

Em tela cheia a janela fica maior que o backbuffer e o DXGI estica com bilinear
(`docs\viabilidade-framegen-upscaling.md` §2.4). Aqui o passe **pode** ser nosso: basta o
backbuffer acompanhar a janela e o PS ampliar a captura com o filtro direcional — é
exatamente o caminho acima de 100%, que já está pronto e medido no §7.

Só que entrar e sair de tela cheia **redimensiona o backbuffer em runtime**, que é o que
derruba o app (§9). Então o conserto do F11 depende do mesmo conserto no add-on. Os dois
problemas são o mesmo problema.

### A escada final, com o motivo de cada corte

| degrau | entra? | por quê |
|---|---|---|
| 25% | **não** | imagem destruída: `hp` do texto 0,75 de 14,65 |
| 50% | **não** | 29% do detalhe do texto; barras finas somem |
| **75%** | **sim, com aviso** | −54% de detalhe, mas −4 ms de latência e fôlego de GPU |
| **100%** | **sim, padrão** | — |
| 125% | **não** | medidamente **pior** que 100% (§6.1) e ainda cobra fps |
| **150%** | **sim** | +10% de borda no texto, −33% de grão, aliasing igual ao de 100% |
| **200%** | **sim** | melhor fidelidade medida (PSNR 21,67); custa 1 px (0,535) e metade do fps |
| 250% | **não** | 15,9 fps em 1440p; ganho pequeno sobre 200% por 2× o custo |
| 300% | **não** | 19,8 fps em 1080p, 10,5 em 1440p, e é o **único** degrau que piora contra a verdade |

**Entregar: 75% · 100% · 150% · 200%.**

250% continua existindo no código (a faixa vai a 300%) e funciona bem em fonte pequena — só
não é oferecido no painel, porque não passa em 1440p, que é o monitor do usuário.

---

## 9. A troca em runtime não é possível hoje — e isso já é um bug do app

Isto derrubou o desenho original ("`SetScale` chamado pelo painel"). O resultado é reprodutível
e não é do meu código.

**Mudar o tamanho do backbuffer com a cadeia quente mata o processo.** Sempre, nas duas
direções:

| troca | app |
|---|---|
| 100% → 150% | **morreu** |
| 100% → 200% | **morreu** |
| 100% → 75% | **morreu** |
| 100% → 50% | **morreu** |

```
[feed] CreateFeature raised exception 0xC0000005 (caught; nothing was submitted)
stopped: creating the DLSS feature crashed (the DLSS 5 add-on may be incompatible).
### CRASH RECORDED ###  exception 0xE06D7363 ... this add-on was last doing: creating the DLSS feature
```

O `ResizeBuffers` em si custa 4,3–10,0 ms e não tem culpa. O que morre é a reconstrução da
feature: o `ReShade.log` mostra que **o ReShade recria o runtime de efeitos inteiro quando o
backbuffer muda de tamanho** (`Recreated runtime environment`, todos os `.fx` recompilados), e
a criação da feature do DLSS estoura nesse caminho.

Tentei desmontar antes: desligar a técnica `DLSS5_Feed` pela ponte, deixar 12 Presents
passarem, redimensionar, religar. **Não resolve** — o log mostra a ponte funcionando
(`ponte=1 desligou_feed=1`) e o ReShade religando a técnica sozinho no reload que ele mesmo
dispara. O conserto é dentro do add-on, não no `main.cpp`.

### Isto não é culpa da resolução interna — já acontece hoje

Testei o caminho que **já existe** no app: escala 100%, sem nada do meu código no meio, e o
alvo capturado muda de tamanho sozinho (é o que acontece quando a pessoa redimensiona a janela
do jogo, ou o jogo troca de resolução). O `main.cpp` chama `R.Resize` → `ResizeBuffers`:

```
[00:19:44] Swapchain redimensionado para 1120x630 (fonte 1120x630, escala 100%) em 3.94 ms
[00:19:44] Alvo mudou de tamanho: 1120x630
[00:19:45] [feed] CreateFeature raised exception 0xC0000005
   -> app MORREU
```

**Redimensionar a janela capturada com o filtro ligado derruba o app.** É um bug de hoje, em
uso normal, independente deste trabalho. Vale um item próprio para o agente da ponte/feed.

### O que isso muda no desenho

A resolução interna **se aplica na inicialização**, como a escolha da fonte: o painel grava a
escolha no ini e reinicia o app. Foi assim que todas as medições deste documento rodaram
(`--escala` na linha de comando), e é por isso que elas funcionaram: o swapchain nasce no
tamanho certo **antes** de a cadeia montar a feature.

---

## 10. Rede de segurança

O medo do usuário — *"colocar uma resolução muito alta, travar, e não conseguir abaixar"* — é
legítimo e está coberto, adaptado ao fato do §9. Quatro camadas, no `EscalaGuard`:

**1. Flag `escala_pendente` no ini, gravada ANTES de aplicar.** Só é apagada depois de 5 s de
presents saudáveis. Se o app abrir e achar `escala_pendente`, sobe em **100%** e avisa
*"a última escala não terminou bem; voltei para 100%"*. Esta é a camada que importa mais no
desenho novo: como a escala entra na inicialização, um travamento **nunca** prende a pessoa na
próxima abertura. Já está implementada e é o caminho testado.

**2. Confirmação com contagem.** Depois de reiniciar na escala nova, cartaz *"Resolução interna
200% — manter? (volta em 10 s)"* com Manter/Voltar. Sem clique em 10 s, volta sozinho.
"Voltar" = gravar 100% e reiniciar. O cartaz é janela GDI nossa, então desenha mesmo com a GPU
afogada.

**3. Watchdog.** Nos primeiros segundos: se não houve Present em 3 s, ou o present caiu abaixo
de 5 fps, **ou o Feeder registrou falha**, volta para 100% e avisa. A terceira condição não é
luxo — no teste de 10320×4320 o fps ficou em **78** com a cadeia morta (§3); só a leitura do
`dlss5-feed.log` pega esse caso. `EscalaGuard::CadeiaMorreu()` lê o que o Feeder escreveu
depois da troca e procura `CreateFeature failed`, `failure: resource build` e
`stopped: repeated failures`.

**4. Teto detectado.** `Renderer::ScaleCabe(srcW, srcH, k)` recusa antes de tentar: 16384 por
lado (D3D11) e o teto de área medido no §3. Degraus acima aparecem desabilitados, com o motivo.

**5. Prioridade dos atalhos.** `EscalaGuard::Tick()` roda no **topo** do ciclo, antes de
`WaitForPresentSlot()`: mesmo com a GPU afogada, a volta acontece antes de o app dormir
esperando um slot de apresentação. Os atalhos continuam funcionando porque são hotkeys globais
e o painel é GDI.

---

## 11. Diff

O diff unificado completo está em `scratchpad\ss\out\ss.diff` (`diff -u main.cpp.orig
main.cpp`, 684 linhas). Os blocos que importam, literais:

### 11.1 `kShader` — cbuffer novo (b1) e os três caminhos

Depois do `cbuffer HdrCB`, antes do comentário `// ---- EOTF SMPTE ST 2084`, inserir o
`cbuffer ScaleCB : register(b1)` com `SrcSize`, `SrcSizeInv`, `Ratio`, `UpFilter` e `Sharp`,
mais `CubicKernel`, `SampleCatmullRom`, `SampleDirecional` e `SampleArea`. São ~150 linhas de
HLSL; estão no diff e no `scratchpad\ss\kshader.hlsl` já extraído (e validado isolado pelo
`hlslcheck.exe`, que compila VS e PS sem abrir device).

A única mudança dentro do `PS` é a primeira linha:

**ANTES**
```hlsl
    float3 col = tex.Sample(smp, i.uv).rgb;
```
**DEPOIS**
```hlsl
    // UpFilter == 0 e o caminho de sempre (escala 100%): amostra direta, bit a bit igual
    // ao que o app fazia antes de existir resolucao interna.
    float3 col;
    if      (UpFilter == 1) col = max(SampleCatmullRom(i.uv), 0.0);
    else if (UpFilter == 2) col = max(SampleDirecional(i.uv), 0.0);
    else if (UpFilter == 3) col = SampleArea(i.uv);
    else                    col = tex.Sample(smp, i.uv).rgb;
```

### 11.2 `struct Renderer` — campos novos

**ANTES**
```cpp
    ID3D11RasterizerState*   rs = nullptr;
    UINT w = 0, h = 0;

    // Camera: o frame vem da CPU e sobe pra frameTex (DYNAMIC).
```
**DEPOIS**
```cpp
    ID3D11RasterizerState*   rs = nullptr;
    UINT w = 0, h = 0;

    // --------- resolucao interna
    // 'w/h' passou a ser SO o backbuffer. 'srcW/srcH' e a resolucao da FONTE (captura),
    // que continua mandando no tamanho da janela.
    UINT  srcW = 0, srcH = 0;
    float scale = 1.0f;
    float sharp = 0.0f;      // realce do ampliador direcional
    int   filtro = 2;        // 1 = Catmull-Rom, 2 = direcional (padrao) - so acima de 100%
    static UINT ScaleDim(UINT v, float s) {
        if (s == 1.0f) return v;           // 100% = o backbuffer de sempre, sem arredondar nada
        UINT r = (UINT)(v * s + 0.5f);
        return r < 8 ? 8 : (r & ~1u);      // par: a reducao 2x2 do DXGI cai em cima do texel
    }

    // Teto duro do D3D11 no feature level 11: 16384 em cada lado. MEDIDO: o NGX cria a
    // feature em 7680x4320 e recusa em 10320x4320 - dai o teto por area.
    static const UINT kMaxDim = 16384;
    static constexpr double kMaxPixels = 40.0e6;
    static bool ScaleCabe(UINT sw, UINT sh, float s) {
        double w = ScaleDim(sw, s), h = ScaleDim(sh, s);
        return w <= kMaxDim && h <= kMaxDim && (w * h) <= kMaxPixels;
    }

    struct ScaleCBData { float srcW, srcH, invW, invH, ratX, ratY; int upFilter; float sharp; };
    ID3D11Buffer* scaleCB = nullptr;
    bool          scaleDirty = true;
    double        lastRebuildMs = 0.0;   // engasgo da ultima troca de escala

    // Camera: o frame vem da CPU e sobe pra frameTex (DYNAMIC).
```

### 11.3 `CreateTargets`, `Init`, `InitRest`, `Shutdown`

**ANTES**
```cpp
        D3D11_TEXTURE2D_DESC ft{};
        ft.Width = w; ft.Height = h; ft.MipLevels = 1; ft.ArraySize = 1;
```
**DEPOIS**
```cpp
        // ATENCAO: a textura da camera tem o tamanho da FONTE, nao do backbuffer.
        D3D11_TEXTURE2D_DESC ft{};
        ft.Width = srcW; ft.Height = srcH; ft.MipLevels = 1; ft.ArraySize = 1;
```

**ANTES**
```cpp
    bool Init(HWND hwnd, UINT cw, UINT ch, std::wstring* err) {
        w = cw; h = ch;
```
**DEPOIS**
```cpp
    bool Init(HWND hwnd, UINT cw, UINT ch, std::wstring* err) {
        srcW = cw; srcH = ch;
        w = ScaleDim(cw, scale); h = ScaleDim(ch, scale);
```

**ANTES**
```cpp
        dev->CreateBuffer(&cb, nullptr, &hdrCB);   // se falhar, o PS ve zeros = HdrMode 0
```
**DEPOIS**
```cpp
        dev->CreateBuffer(&cb, nullptr, &hdrCB);   // se falhar, o PS ve zeros = HdrMode 0

        cb.ByteWidth = sizeof(ScaleCBData);
        dev->CreateBuffer(&cb, nullptr, &scaleCB); // se falhar, zeros = UpFilter 0 = 100%
```

**ANTES**  `SafeRelease(hdrCB);`
**DEPOIS** `SafeRelease(hdrCB); SafeRelease(scaleCB);`

### 11.4 `Resize` vira `Rebuild` + `Resize` + `SetScale`; `Upload` usa a fonte

**ANTES**
```cpp
    bool Resize(UINT nw, UINT nh, std::wstring* err) {
        ReleaseTargets();
        w = nw; h = nh;
        HRESULT hr = sc->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, scFlags);
        if (FAILED(hr)) { *err = L"ResizeBuffers " + Hex(hr); return false; }
        Log("Swapchain redimensionado para %ux%u", w, h);
        return CreateTargets(err);
    }

    void Upload(const BYTE* src, UINT sw, UINT sh) {
        if (!frameTex || sw != w || sh != h) return;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(frameTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        const size_t row = (size_t)w * 4;
        for (UINT y = 0; y < h; y++) memcpy((BYTE*)m.pData + (size_t)y * m.RowPitch, src + (size_t)y * row, row);
        ctx->Unmap(frameTex, 0);
    }
```
**DEPOIS**
```cpp
    // Refaz backbuffer + alvos para a fonte 'srcW x srcH' na escala 'scale' atual.
    bool Rebuild(std::wstring* err) {
        LARGE_INTEGER f{}, t0{}, t1{};
        QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
        ReleaseTargets();
        w = ScaleDim(srcW, scale); h = ScaleDim(srcH, scale);
        HRESULT hr = sc->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, scFlags);
        if (FAILED(hr)) { *err = L"ResizeBuffers " + Hex(hr); return false; }
        scaleDirty = true;
        bool ok = CreateTargets(err);
        QueryPerformanceCounter(&t1);
        lastRebuildMs = f.QuadPart ? (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart : 0.0;
        Log("Swapchain redimensionado para %ux%u (fonte %ux%u, escala %.0f%%) em %.2f ms",
            w, h, srcW, srcH, scale * 100.0f, lastRebuildMs);
        return ok;
    }

    // A fonte mudou de tamanho (alvo redimensionado, camera trocou de modo).
    bool Resize(UINT nw, UINT nh, std::wstring* err) {
        srcW = nw; srcH = nh;
        return Rebuild(err);
    }

    // NAO chamar do painel em runtime - ver secao 9. Existe para a inicializacao e para
    // o teste; a aplicacao de verdade e no arranque.
    bool SetScale(float s, std::wstring* err) {
        if (s < 0.25f) s = 0.25f;
        if (s > 3.0f)  s = 3.0f;
        if (s == scale) return true;
        if (!ScaleCabe(srcW, srcH, s)) {
            *err = L"escala nao cabe: " + std::to_wstring(ScaleDim(srcW, s)) + L"x" +
                   std::to_wstring(ScaleDim(srcH, s)) + L" passa do limite da placa";
            return false;
        }
        float antes = scale;
        scale = s;
        if (Rebuild(err)) return true;
        scale = antes;                      // nao conseguiu: volta e tenta refazer o antigo
        std::wstring e2;
        if (!Rebuild(&e2)) Log("PERIGO: nao consegui nem voltar para %.0f%%: %s", antes * 100.0f, U8(e2).c_str());
        return false;
    }

    void Upload(const BYTE* src, UINT sw, UINT sh) {
        if (!frameTex || sw != srcW || sh != srcH) return;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(frameTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        const size_t row = (size_t)srcW * 4;
        for (UINT y = 0; y < srcH; y++) memcpy((BYTE*)m.pData + (size_t)y * m.RowPitch, src + (size_t)y * row, row);
        ctx->Unmap(frameTex, 0);
    }
```

### 11.5 `DrawAndPresent` — alimentar o cbuffer novo

**ANTES**
```cpp
            ctx->PSSetConstantBuffers(0, 1, &hdrCB);
        }
        ctx->PSSetShaderResources(0, 1, &use);
```
**DEPOIS**
```cpp
            ctx->PSSetConstantBuffers(0, 1, &hdrCB);
        }
        if (scaleCB) {
            if (scaleDirty) {
                // 0 = 1:1; acima de 100% o filtro escolhido; abaixo, reducao por area.
                int uf = 0;
                if      (w > srcW || h > srcH) uf = (filtro == 1) ? 1 : 2;
                else if (w < srcW || h < srcH) uf = 3;
                ScaleCBData s{ (float)srcW, (float)srcH,
                               srcW ? 1.f / (float)srcW : 0.f, srcH ? 1.f / (float)srcH : 0.f,
                               w ? (float)srcW / (float)w : 1.f, h ? (float)srcH / (float)h : 1.f,
                               uf, sharp };
                D3D11_MAPPED_SUBRESOURCE m{};
                if (SUCCEEDED(ctx->Map(scaleCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                    memcpy(m.pData, &s, sizeof(s));
                    ctx->Unmap(scaleCB, 0);
                    scaleDirty = false;
                }
            }
            ctx->PSSetConstantBuffers(1, 1, &scaleCB);
        }
        ctx->PSSetShaderResources(0, 1, &use);
```

### 11.6 `EscalaGuard` — a rede de segurança

Struct nova, logo antes do bloco `// comandos que o painel enfileira pro loop principal`.
Campos: `R`, `anterior`, `esperando`, `t0`, `presentsNoT0`, `presents`, `fpsAtual`,
`limpezaPendente`, `feedLogNoT0`; constantes `kConfirmaMs = 10000`, `kWatchdogMs = 3000`,
`kLimpaMs = 5000`; métodos `Pedir`, `Manter`, `Voltar`, `Tick`, `CadeiaMorreu`,
`TamanhoArquivo`; e três ponteiros de função que o painel preenche (`AbrirCartaz`,
`FecharCartaz`, `Avisar`). Texto completo no diff. O miolo do watchdog:

```cpp
    void Tick() {
        if (!R) return;
        ULONGLONG agora = GetTickCount64();
        if (esperando) {
            UINT32 feitos = (presents ? *presents : 0) - presentsNoT0;
            if (agora - t0 >= kWatchdogMs) {
                if (feitos == 0)                      { Voltar(L"nenhum frame saiu"); return; }
                if (fpsAtual > 0.0 && fpsAtual < 5.0) { Voltar(L"menos de 5 fps"); return; }
                if (CadeiaMorreu())                   { Voltar(L"o filtro nao subiu nesse tamanho"); return; }
            }
            if (agora - t0 >= kConfirmaMs) { Voltar(L"ninguem confirmou"); return; }
        }
        if (limpezaPendente && !esperando && agora - t0 >= kLimpaMs &&
            (presents ? *presents : 0) - presentsNoT0 > 30) {
            IniSet(L"escala_pendente", L"");
            limpezaPendente = false;
        }
    }
```

No loop principal, no **topo**, antes do `MsgWaitForMultipleObjects`:

```cpp
    g_escala.R = &R; g_escala.presents = &presented;

    while (!g_quit) {
        // TOPO do ciclo, antes de qualquer espera: com a GPU afogada a volta para a escala
        // anterior tem de acontecer antes de a gente dormir esperando um slot.
        g_escala.Tick();
```

e no bloco de 1 Hz: `g_escala.fpsAtual = pfps;`

### 11.7 Proteção na inicialização e linha de comando

**ANTES**
```cpp
    Renderer R;
    R.cpuUpload = (C.kind == SRC_CAMERA);
```
**DEPOIS**
```cpp
    // Protecao na inicializacao: se a ultima escala ficou marcada como 'pendente', ela nao
    // sobreviveu 5 s de presents saudaveis - provavelmente travou. Sobe em 100%.
    {
        std::wstring pend = IniGet(L"escala_pendente");
        if (!pend.empty()) {
            Log("a resolucao interna de %s%% da ultima vez nao terminou bem - subindo em 100%%", U8(pend).c_str());
            IniSet(L"escala_pendente", L"");
            argEscala = 1.0f;
        }
    }
    Renderer R;
    R.scale = (argEscala < 0.25f) ? 0.25f : (argEscala > 3.0f ? 3.0f : argEscala);
    if (argEscalaFiltro) R.filtro = argEscalaFiltro;
    R.sharp = argEscalaSharp;
    if (R.scale != 1.0f)
        Log("Resolucao interna: %.0f%% (filtro %d, realce %.2f)", R.scale * 100.0f, R.filtro, R.sharp);
    R.cpuUpload = (C.kind == SRC_CAMERA);
```

**ANTES**
```cpp
    int  argFps = 0;
```
**DEPOIS**
```cpp
    int  argFps = 0;
    float argEscala = 1.0f;      // resolucao interna: backbuffer k vezes a fonte (0.25 a 3.0)
    int   argEscalaFiltro = 0;   // 1 = Catmull-Rom, 2 = direcional (0 = padrao do Renderer)
    float argEscalaSharp = 0.0f; // realce do ampliador direcional
```

**ANTES**
```cpp
        else if (a == L"--fps" && i + 1 < argc) argFps = _wtoi(argv[++i]);
```
**DEPOIS**
```cpp
        else if (a == L"--fps" && i + 1 < argc) argFps = _wtoi(argv[++i]);
        else if (a == L"--escala" && i + 1 < argc) argEscala = (float)_wtof(argv[++i]);
        else if (a == L"--escala-filtro" && i + 1 < argc) argEscalaFiltro = _wtoi(argv[++i]);
        else if (a == L"--escala-realce" && i + 1 < argc) argEscalaSharp = (float)_wtof(argv[++i]);
```

### 11.8 Chamadores — `R.w/R.h` vira `R.srcW/R.srcH` onde o assunto é a FONTE

Nove linhas, todas mecânicas:

| linha | ANTES | DEPOIS |
|---|---|---|
| `if (scap.w != R.w \|\| scap.h != R.h)` (após `scap.Start`) | `R.w`, `R.h` | `R.srcW`, `R.srcH` |
| `ResizeClientTo(hwnd, R.w, R.h);` (× 2) | `R.w, R.h` | `R.srcW, R.srcH` |
| `g_hud.w = R.w; g_hud.h = R.h;` (× 2) | idem | idem |
| `if (fw != R.w \|\| fh != R.h)` (câmera) | idem | idem |
| `if (scap.w != R.w \|\| scap.h != R.h)` (loop) | idem | idem |
| `Log("Alvo mudou de tamanho: %ux%u", R.w, R.h);` | idem | idem |
| título da janela: `..., R.w, R.h, fps, warn);` | idem | idem |

Pontos de atenção para quem integrar:

- o cbuffer novo é `b1`; o `main.cpp` de hoje só usa `b0` (verificado), então não colide com o
  trabalho do agente `hdr`;
- `R.w`/`R.h` continuam **certos** onde o assunto é o backbuffer — não trocar às cegas;
- `g_hud.w = R.w; g_hud.h = R.h;` (aparece 2×) **tem** de virar `R.srcW/R.srcH`, senão o painel
  passa a mostrar a resolução do backbuffer em vez da resolução da fonte;
- `SetScale` **não** deve ser chamado direto pelo painel (§9). O painel grava no ini e reinicia.

---

## 12. Arquivos

Tudo em `scratchpad\ss\`, fora do projeto:

| arquivo | o que é |
|---|---|
| `main.cpp` / `main.cpp.orig` | a cópia com a resolução interna e o `EscalaGuard`, e o original |
| `gametarget.cpp` | o alvo; `--png <arq>` grava a cena, `--redim-apos <s>` reproduz o bug do §9 |
| `sstool.cpp` | `down` (Lanczos), `amp` (Catmull-Rom), `met`, `linha`, `crops`, `stack` |
| `hlslcheck.cpp` | compila o `kShader` fora do app, só CPU — valida o shader sem tocar na GPU |
| `ss-run.ps1` | uma configuração: `-Mode dump` ou `-Mode perf` |
| `ss-dxgi.ps1` | o experimento da §2 (o que a tela mostra) |
| `ss-tabela.ps1` | as duas tabelas |
| `rs\` | a cadeia ReShade isolada |
| `out\` | dumps, reduções e crops |

Modos de teste do build (não existem no app de produção): `DLSS5_DUMP_MS`,
`DLSS5_ESCALA_CICLO=1,1.5,2` (aplica cada degrau e loga o engasgo) e `DLSS5_ESCALA_PROVA=2.5`
(aplica e não confirma, para ver a volta automática).
