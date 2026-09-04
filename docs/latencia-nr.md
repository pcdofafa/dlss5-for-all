# Latência com o Neural Rendering ligado — medições

Segunda rodada. Pergunta: **quanto o NR (DLSS 5 Neural Rendering) acrescenta de latência, e o que
dá para reduzir sem tocar na imagem.** Medições de 2026-09-02, 22:50–23:18.

Complementa `docs\latencia.md` (primeira rodada: 121 → 42 ms com swapchain waitable + espera
antes do Tick).

**Regra que manda neste documento:** o objetivo do app é melhoria gráfica. Qualidade > latência.
Nada que reduza o ganho visual do NR entra como padrão. Por decisão do usuário,
`work_resolution` abaixo de 100% e jogar em resolução menor estão **fora**, nem como opção — não
aparecem em nenhuma recomendação abaixo.

---

## O número, sem rodeio

Com qualidade máxima (tudo no padrão atual, `work_resolution=100`, sliders no máximo):

| resolução do alvo | sem NR | com NR | custo do NR |
|---|---|---|---|
| 1920x1080 | espera-GPU 1,5 ms | espera-GPU 5,5 ms | **+3 a +4,5 ms** |
| 3440x1400 | 98 fps, frame 10,2 ms | 82 fps, frame 12,3 ms | **+2,0 ms de frame time; ~+10 ms fim a fim** |

**Não há como baixar isso sem perder imagem.** Testei todos os knobs gratuitos que existem e
nenhum é mais barato (seção 3). O NR custa o que custa.

---

## 1. Bancada e as duas contaminações

**Bancada:** cópia isolada de todo o projeto em `scratchpad\nr\` (fontes atuais + cadeia ReShade
completa + `dlss5-bridge.addon64`), compilada como `lat_test.exe`. Alvo = `lattarget.exe` (janela
D3D11 que redesenha a cada refresh), rodando **fora** da pasta com o `dxgi.dll`. Nenhum arquivo
do projeto foi alterado — os testes editam só as cópias de `ReShade.ini` e `dlss5-feed.cfg` em
`nr\`. Cada execução ~25 s, processo morto em seguida, sem overlay.

Instrumentação nova: **`espera-GPU`** — tempo preso no `WaitForSingleObjectEx(latWait)`, ou seja,
quanto a GPU nos fez esperar antes de podermos pegar o próximo frame. É a medida direta de custo
de GPU virando latência.

### Contaminação 1 — outro agente usando a GPU

Durante boa parte da sessão um `DLSS5ForAll-hud.exe` de outro agente consumiu **26% do engine 3D**
(caiu para 11% no fim). Isso produziu leituras inconsistentes a 1080p: a mesma configuração deu
22 ms numa execução e 35 ms na seguinte. Contornei rodando A/B/A encadeado e repetindo; onde a
repetição não confirmou, marquei como ruído. As medições de 3440x1400 (as mais importantes) foram
feitas na janela de menor contenção e repetem dentro de 1,5%.

### Contaminação 2 — janela fora da tela invalida o lado do display

Para não piscar a tela do usuário, a janela do app roda em `-6000,-6000`. Consequência medida:

| métrica | janela na tela (log real do usuário, 22:45) | janela fora da tela (bancada) |
|---|---|---|
| `PRESENT->TELA` | 8,5 – 11,6 ms | 16 – 26 ms |

Portanto **valores absolutos de `PRESENT->TELA` não valem aqui**, e qualquer coisa que dependa do
`Present(1,0)` bloquear (V-Sync, teto de fps) fica sem medição válida — o DXGI estrangula o
Present de uma janela invisível. Uso `espera-GPU`, `acordar`, `fps`, `frame interval` e
`present` (CPU), que com `Present(0,0)` não dependem da composição da janela; e as **diferenças**
de `PRESENT->TELA` entre configurações medidas na mesma condição.

---

## 2. Quanto o NR custa

### 3440x1400 — a resolução que interessa, e onde satura

Aqui a GPU vira o gargalo, então os números são diretos.

| | NR OFF (G1/G3) | NR ON (G2/H0) |
|---|---|---|
| fps present | 96,1 / 97,6 / 98,5 | 81,6 / 81,7 / 81,9 / 82,1 |
| `frame interval` (feed) | 10,21 / 10,30 ms | 12,25 / 12,26 ms |
| `espera-GPU` média | 0,64 / 1,29 / 1,47 | **11,08 / 11,33 / 11,33** |
| `acordar` (idade do frame ao pegar) | 0,37 / 0,82 / 0,96 | **4,80 / 5,00 / 5,07** |
| `present` CPU | 0,46 – 0,54 | 0,76 – 0,94 |
| `feed CPU` | 0,29 – 0,33 | 0,59 – 0,74 |
| fim a fim (`acordar`+`tick`+`present`+`tela`) | ~19 – 20 ms | ~29 – 31 ms |

Lendo isso:

- **+2,0 ms de tempo de frame puro** (10,25 → 12,26 ms), que é a queda de 98 para 82 fps.
- **+4,2 ms de idade do frame** (`acordar` 0,7 → 5,0): com a GPU ocupada mais tempo, quando o loop
  volta a pegar imagem o frame mais novo disponível já tem ~5 ms. Isso é fase entre o relógio do
  WGC (~100 fps) e o nosso ciclo agora mais lento — não é fila nossa.
- **+~10 ms fim a fim.** O absoluto está inflado pela janela fora da tela nos dois lados; a
  diferença é a parte confiável.

### 1920x1080 — A/B/A encadeado

| | NR OFF | NR ON | NR OFF |
|---|---|---|---|
| `espera-GPU` média | 1,49 / 1,95 | **6,31 / 5,64** | 1,54 / 1,22 |
| `present` CPU | 0,41 | 0,72 | 0,43 |
| `feed CPU` | 0,25 | 0,59 | 0,25 |
| fps present | 89 – 90 | 93 | 91 – 92 |

Repetição posterior com a mesma configuração NR ON: `espera-GPU` 4,64 / 6,62.
→ **+3 a +4,5 ms.** A 1080p a GPU ainda dá conta (93 fps contra a fonte a 100), então o custo
aparece como espera, não como queda de fps.

### 1280x720

Custo médio quase some, mas aparece uma **trava de p95 de ~7 ms** que não existe com o NR
desligado (p95 0,0–0,9 sem NR, 7,1–9,3 com NR). É essa trava intermitente — não a média — que
vira engasgo perceptível.

---

## 3. Lista (a): reduções de latência SEM custo visual

São as que podem ser padrão. **Todas já estão implementadas e ligadas.**

| medida | ganho | estado |
|---|---|---|
| swapchain waitable + `SetMaximumFrameLatency(1)` | 121 → 76 ms (1ª rodada) | já no app |
| esperar a GPU **antes** do Tick, não dentro do Present | 76 → 42 ms (1ª rodada) | já no app |
| descartar captura atrasada: drenar a fila e copiar só o frame mais novo | protege contra rajada | já no app |
| contexto imediato cacheado, geometria do recorte cacheada | ~0,02 ms | já no app |
| V-Sync desligado, sem teto de fps | — | já é o padrão |

**Sobre "descartar captura atrasada enquanto a GPU está ocupada com o NR":** conferi o ciclo atual
e ele já faz exatamente isso, na ordem certa — `WaitForPresentSlot()` (espera a GPU) →
`scap.Tick()` (drena a fila fechando os antigos e copia só o mais novo) → `DrawAndPresent()`.
Não sobrou nada para colher aí. Os 5 ms de `acordar` com NR ligado **não são frame velho na
nossa fila**: são fase entre o relógio do WGC e o nosso ciclo. Esperar mais para pegar um frame
mais novo não ajudaria — apresentaria um frame 0 ms mais velho, 5 ms mais tarde. Dá no mesmo.

### Uma correção pendente (enviada ao agente `hud`) — agora confirmada na tela

Com V-Sync ligado, o `Present(1,0)` já bloqueia até o vblank e o `WaitForPresentSlot()` bloqueia
de novo — duas esperas por frame. Correção de 2 linhas já enviada ao `hud`.

Medido com a janela visível (seção 4.1): a correção **elimina a espera dupla** (`espera-GPU`
passa a n=0, ou seja, o loop não bloqueia mais no objeto) e o fps subiu de 52–57 para 72–91.
A magnitude exata está confundida por carga externa de GPU (64% na execução sem correção contra
45% na execução com correção), mas a direção é clara e o mecanismo está confirmado.

---

## 4.1 As 4 execuções com a janela visível (V-Sync e teto de fps)

Autorizadas pelo usuário. Janela do espelho 1280x720 **visível**, canto inferior esquerdo do
monitor real (1940,680); alvo 1280x720 encostado na borda direita (só ~260 px aparecem);
NR ligado, `work_resolution=100`, sliders no máximo; 25 s cada, processo morto em seguida.

Instância do usuário presente durante todas: `DLSS5ForAll.exe` PID 28028, iniciada 22:40:56,
sem argumentos — **não foi encerrada**, conforme combinado.

| # | configuração | fps chegada / copiado / present | `espera-GPU` | `PRESENT->TELA` méd / p95 | GPU externa |
|---|---|---|---|---|---|
| 1 | sem vsync, sem teto (padrão) | 91,5 / 48,9 / 48,9 | 18,8 – 19,3 | **17,95 – 18,09 / 18,8 – 19,0** | 52% |
| 2 | V-Sync, **como está hoje** | 93,1 / 56,7 / 56,7 | 16,0 – 17,6 | 15,3 – 16,6 / 18,6 | 64% |
| 3 | V-Sync, **com a correção** | 91,4 / 91,4 / 91,4 | **0 (n=0)** | 26,5 – 29,3 / 37 – 47 | 45% |
| 4 | teto 60, sem vsync | 60,0 / 60,0 / 40,0 | 0,01 | **6,53 / 7,18** | 60% |

**Contaminação séria nesta rodada:** a carga externa de GPU de outros agentes ficou entre **45% e
64%** durante as quatro execuções (contra 11–26% nas rodadas anteriores). Isso derrubou o
`copiado` para metade do `chegada` nas execuções 1, 2 e 4 — o loop não deu conta e o WGC passou a
entregar em rajada (`p05` do intervalo caiu para 0,3–0,8 ms). Os números abaixo têm que ser lidos
com isso em mente.

O que dá para concluir:

- **A correção do V-Sync funciona.** Na execução 3 o `espera-GPU` foi n=0 (a espera dupla sumiu,
  como projetado) e `copiado` voltou a acompanhar `chegada` (91,4 = 91,4), enquanto nas outras
  metade dos frames se perdia. Isso apesar de a execução 3 ter tido *menos* GPU externa —
  a direção é clara, a magnitude não é atribuível só à correção.
- **V-Sync continua sendo a pior opção, mesmo corrigido.** Execução 3: `PRESENT->TELA` 26,5–29,3
  ms com p95 de 37–47 ms, contra 18 ms da execução 1 sem vsync. Confirma manter **V-Sync
  desligado por padrão**.
- **O `PRESENT->TELA` com a janela na tela é ~18 ms** no modo padrão a 720p sob 52% de carga
  externa — contra os 8,5–11,6 ms do log real do usuário sem essa carga. Ou seja, o número real
  do usuário é o do log dele, não o desta bancada.

### Correção da coluna "GPU externa" desta tabela

A função que eu usava para medir carga de terceiros **não descontava o meu próprio processo** —
o filtro tinha um bug e somava tudo. Os valores de "durante" (45–64%) estavam inflados pelo app
que eu estava medindo. **Os valores corretos de carga externa são os de "antes": 7%, 25%, 5% e
43%.** Corrigi a função antes da rodada de confirmação.

### Um resultado que parecia contrariar o que eu tinha dito — e que a confirmação derrubou

Na execução 4, na janela de medição em que a carga permitiu (`chegada` 60,0 = `copiado` 60,0),
o **teto de 60 deu `PRESENT->TELA` de 6,53 ms, p95 7,18** — o **menor de todas as medições desta
rodada**, contra 18 ms sem teto. Faz sentido fisicamente: apresentando a 40–60 fps num monitor de
99 Hz sobra folga, e cada Present pega o vblank seguinte em vez de esperar na fila.

Isso **contraria** o que escrevi antes com base na bancada fora da tela (onde o teto parecia
piorar). A segunda janela de medição da mesma execução, já sob carga, deu 17,37 ms — ou seja, o
resultado bom apareceu uma vez só.

### 4.2 A confirmação — e o teto de fps NÃO é a causa

Três execuções com a GPU liberada pelo lead (referência sem teto + teto 60 + teto 72). Incluí a
referência porque sem uma medida na mesma condição as outras duas não são interpretáveis.

| execução | GPU externa (antes) | janela | chegada / copiado / present | `PRESENT->TELA` méd / p95 |
|---|---|---|---|---|
| **REF sem teto** | **8%** | 1ª | **60,0 / 60,0 / 60,0** | **6,77 / 7,30** |
| REF sem teto | 8% | 2ª | 64,7 / 59,0 / 59,0 | 8,22 / 18,31 |
| teto 60 | 80% | 1ª | 60,0 / 60,0 / 40,0 | 6,56 / 7,44 |
| teto 60 | 80% | 2ª | 83,8 / 57,2 / 46,3 | 15,49 / 18,66 |
| teto 72 | 78% | 1ª | 47,8 / 47,3 / 31,7 | 9,22 / 19,80 |
| teto 72 | 78% | 2ª | 65,7 / 57,7 / 41,5 | 9,75 / 18,50 |

**A referência SEM teto nenhum deu 6,77 ms.** O 6,5 ms não era do teto — reproduz sem teto
algum. O que as duas janelas boas têm em comum não é a configuração: é a **taxa da fonte**
(`chegada = 60,0` nas duas). Com a fonte a 60 fps num monitor de 99 Hz sobra folga e cada Present
pega o vblank seguinte; com a fonte a 90–100 fps apresentamos quase todo vblank, a fila enche e o
`PRESENT->TELA` sobe para 15–18 ms.

→ **O teto de fps não entra na lista (a).** Ele não produziu nenhum ganho que a configuração
padrão já não tivesse. Recomendação: **manter o app sem teto por padrão, como está hoje.**

### 4.3 Teto de fps refeito com a GPU realmente livre — mede pior, não melhor

As duas execuções com teto da tabela acima pegaram 78–80% de carga externa. Refeitas com **5–6%**:

| execução | GPU externa | janela | chegada / copiado / present | `espera-GPU` | `PRESENT->TELA` |
|---|---|---|---|---|---|
| REF sem teto | 8% | 1ª | 60,0 / 60,0 / 60,0 | 0,00 | **6,77 / 7,30** |
| REF sem teto | 8% | 2ª | 64,7 / 59,0 / 59,0 | 2,51 | 8,22 / 18,31 |
| teto 60 | **5%** | 1ª | 78,5 / 55,7 / 50,0 | **17,52** | 17,47 / 18,94 |
| teto 60 | **5%** | 2ª | 80,5 / 56,5 / 50,0 | **17,42** | 17,37 / 18,85 |
| teto 72 | **6%** | 1ª | 60,0 / 60,0 / 40,0 | 0,07 | **6,93 / 7,50** |
| teto 72 | **6%** | 2ª | 60,0 / 50,6 / 49,4 | **16,80** | 16,93 / 18,61 |

**Com a GPU livre, as configurações com teto mediram 17 ms e a sem teto mediu 6,8–8,2 ms.**
Não há ganho de latência a rotular — o teto mediu pior.

**E há um regime biestável que explica a bagunça toda desta investigação.** Olhe as duas janelas
da execução com teto 72: **mesma execução, mesmo teto, mesma taxa de fonte (60,0 nas duas)** — e
uma deu 6,93 ms e a outra 16,93 ms. O que muda entre elas é o `espera-GPU`: 0,07 contra 16,80.

Ou seja, o app alterna entre dois regimes:

| regime | `espera-GPU` | `PRESENT->TELA` |
|---|---|---|
| rápido | ~0 | ~7 ms |
| lento | ~17 | ~17 ms |

A alternância acontece **dentro de uma mesma execução, sem mudar nada na configuração**, e é ela
— não o teto, não a taxa da fonte, não os knobs do NR — que domina o `PRESENT->TELA`. A hipótese
mais provável é o DWM entrando e saindo de *independent flip* para a nossa janela (quando a janela
deixa de poder ir direto para a tela, a composição entra no caminho e a latência dobra); é um
comportamento conhecido do modelo flip e explica a biestabilidade, o tamanho do salto e o fato de
não responder a nenhum knob nosso. **Não medi isso** — é hipótese, e testá-la exigiria
instrumentar `IDXGISwapChain::GetFrameStatistics` procurando descontinuidade de `PresentRefreshCount`,
ou o rastreamento do DWM. Fica registrado como a explicação mais provável e a próxima
investigação de maior valor.

**Recomendação sobre o teto de fps:** não é recurso de latência. É limitador de quadros — serve
para segurar consumo, temperatura e ruído de cooler. O texto do painel não deve prometer
milissegundo nenhum.

---

## 4.4 Caça ao regime biestável — veredito: não é nosso, e boa parte era contenção

7 execuções com instrumentação nova. Cada condição isolada, janela visível, 25 s, alvo a 100 fps.
Layout: DISPLAY1 = (1920,0)-(5360,1440) 3440x1440 @99 Hz, **secundário** (tela de trabalho);
DISPLAY2 = (0,0)-(1920,1080), **principal**.

`TOTAL` = espera-GPU + tick + present + PRESENT→TELA, que é a soma que o usuário sente.

| condição | GPU externa | chegada | present | espera-GPU | tela | **TOTAL** |
|---|---|---|---|---|---|---|
| R2 referência (canto DISPLAY1) | **4%** | 98,2 / 99,7 | 93 / 97 | 7,4 / 9,3 | 9,8 / 9,3 | **18,1 / 19,6** |
| R6 **topmost** (2ª janela) | **8%** | 96,3 | 79,1 | 7,4 | 10,4 | **18,9** |
| R3 janela pequena no DISPLAY2 | **3%** | 100 | 98 | **0,7 / 0,8** | 19,1 / 19,5 | **20,5 / 21,1** |
| R7 **ALLOW_TEARING** só na criação | 63% | 100 | 89,5 | 10,0 | 9,9 | **21,1** |
| R1 referência | 42% | 94,7 | 88,5 | 9,9 | 9,6 | **20,7** |
| R4 **sem borda cobrindo o DISPLAY2** | 42% | 100 | 88 | 10,3 | 25,5 / 26,0 | **36,7 / 37,3** |
| R5 **maxFrameLatency = 2** | 46% | 51,6 | 50,3 | 17,5 / 18,9 | 38,1 / 39,4 | **57,0 / 59,5** |

### O que os indicadores novos disseram

- **`dRefresh` (SyncRefreshCount − PresentRefreshCount) = 0,00 em TODAS as condições**, mínimo 0,
  máximo 0. Não houve uma única descontinuidade.
- **`DwmGetCompositionTimingInfo`: DWM compondo a 100,0 Hz em todas**, `atrasados=0`,
  `pendentes=1` em todas.
- **`Present() != S_OK`: zero em todas.** Nenhum `OCCLUDED`, nenhum `MODE_CHANGE`.

Ou seja: **nenhum indicador de composição variou entre o "regime rápido" e o "lento".**
A hipótese do independent flip está **descartada por medição**, não por argumento — e ela já era
implausível, porque uma janela de 1280x720 num monitor de 3440x1440 nunca foi elegível a iFlip.

### O que realmente explicava a variação

Ordenando a tabela por carga externa de GPU em vez de por condição, o padrão aparece sozinho:

| carga externa | TOTAL observado |
|---|---|
| 3 – 8% | 18,1 / 18,9 / 19,6 / 20,5 / 21,1 |
| 42 – 63% | 20,7 / 21,1 / 36,7 / 37,3 / 57,0 / 59,5 |

**Com a GPU livre o total fica estável em 18–21 ms em todas as condições testadas.** A
"biestabilidade" que eu persegui era, em boa parte, **carga de GPU de outros processos** — agravada
por um erro meu de instrumentação: a função que media a carga externa não descontava o meu próprio
processo, então eu classifiquei como "limpas" execuções que não eram, e vice-versa. Corrigi a função
no meio desta investigação; as tabelas anteriores a 4.3 têm essa ressalva.

### Respostas item a item

| condição testada | resultado |
|---|---|
| (a) `WS_EX_TOPMOST` | **sem efeito** (18,9 contra 18,1–19,6 da referência) |
| (b) sem borda cobrindo o monitor inteiro | **piorou** (36,7–37,3 contra ~20). Não há caminho curto a ganhar |
| (c) `ALLOW_TEARING` só na criação | **sem efeito** (21,1 sob 63% de carga). E sem tearing: o Present continua sem a flag |
| (e) DWM/composição | **constante em todas** — não é a variável |
| (f) `SetMaximumFrameLatency` 1 vs 2 | **2 é muito pior**: 57–59 ms contra ~19, e o fps cai para 50. Confirma que 1 é o valor certo |
| lado da captura vs da apresentação | a latência **desloca** entre os dois sem mudar o total: no DISPLAY2 a espera-GPU cai para 0,7 e a tela sobe para 19; no DISPLAY1 a espera é 7–9 e a tela é 9–10. Soma igual |

### Veredito

**Não é nosso, e não há nada a corrigir no código.** Nenhuma condição de janela, de swapchain ou
de monitor melhorou o total; duas delas (cobrir o monitor, frame latency 2) pioraram bastante. A
configuração atual do app — flip discard, waitable, `SetMaximumFrameLatency(1)`, sem vsync, janela
comum — já é a melhor das sete testadas.

O piso com a GPU livre é **18–21 ms de ponta a ponta**, e o que sobra dele é: ~9 ms de espera pelo
swapchain (o ritmo do display, 99 Hz = 10,1 ms) e ~9 ms de Present até o vblank. São dois
tempos de tela, não código nosso.

Um detalhe registrado sem explicação: no DISPLAY2 aparecem **56–61 presents "engolidos"** por
janela de 300 (apresentamos e o `PresentCount` do DXGI não andou), contra 0–1 no DISPLAY1. Não
mudou o total e não investiguei além disso.

**Observação que vale mais que o teto:** o ritmo de entrega do WGC **não é estável**. Ao longo da
sessão ele apareceu travado em 50 fps, depois em 60,0 (intervalo médio de 16,67 ms, exatamente
60 Hz) e depois em 90–100. E o `PRESENT->TELA` acompanha: **6,8 ms com a fonte a 60, 15–18 ms com
a fonte a 90–100.** É um relógio do sistema que não controlamos, e é ele que manda na latência de
apresentação — não os nossos knobs.

O teto continua quantizando na taxa da fonte (pedido 60, entregue 40–46), defeito separado já
reportado ao `hud`, que tentou corrigir, mediu pior e reverteu (concordo com a reversão).

## 5. Lista (b): reduções COM custo visual

**Vazia.**

Por decisão do usuário, `work_resolution` < 100% e resolução menor estão fora — e eram as duas
únicas candidatas. Não sobrou nenhuma opção de "trocar imagem por latência" para oferecer.

Registro só para não se perder: `work_resolution` 75 e 66 deram p95 de `espera-GPU` 5,9–6,7
contra a banda 7,1–9,3 do 100 a 720p — **na borda do ruído**, nem sequer um ganho comprovado.
Ou seja, mesmo se estivesse na mesa, não valeria a imagem.

---

## 5. Os knobs do NR: nenhum é mais barato

Medido a **3440x1400**, que é onde satura e onde uma diferença apareceria:

| configuração | fps present | `espera-GPU` méd/p95 | `frame interval` |
|---|---|---|---|
| referência (`NRPreset=0`, `NRStyle=0`, sliders 2/2/2/2) | 81,7 / 81,9 | 11,33 / 12,1 – 12,3 | 12,25 |
| `NRPreset=1` | 81,6 / 81,5 | 11,26 – 11,39 / 12,2 | 12,31 |
| `NRStyle=1` | 80,1 / 80,1 | 11,56 – 11,61 / 12,4 – 12,5 | 12,50 |
| sliders 1/1/1/1 | 81,2 / 81,2 | 11,46 / 12,3 – 12,4 | 12,33 |

Tudo dentro de 1,5%. `NRStyle=1` é até marginalmente **mais lento**.

Confirmação a 720p, com o `ReShade.log` conferido para garantir que o NR realmente executou em
cada caso (`inline feature 18 evaluation succeeded` nos três):

| configuração | `espera-GPU` média / p95 |
|---|---|
| sliders 2/2/2/2, `NREnableUpscaling=1` | 0,79 / 7,61 |
| sliders 2/2/2/2, `NREnableUpscaling=0` | 0,65 / 7,09 |
| sliders 0/0/0/0, `NREnableUpscaling=1` | 0,87 / 7,50 |

**Correção de uma leitura minha:** numa primeira passagem, sliders a zero e `NREnableUpscaling=0`
apareceram com custo zero e 100 fps. Era ruído — na repetição controlada a diferença sumiu. Não
me baseie na primeira leitura.

### Consequências

- **Os quatro sliders (`NRIntensity`, `NRLocalTone`, `NRLocalStructure`, `NRSkinStructure`) não
  custam GPU mensurável.** Como não custam latência, **a latência não tem nada a dizer sobre
  eles** — deixe como está. Se eles clareiam demais (a questão do `docs\brilho-witcher3.md`),
  isso se decide por imagem, não por desempenho, e não é minha medição: **não fiz PSNR/SSIM nem
  comparação visual**, então não afirmo nada sobre o ganho de imagem deles.
- **`NREnableUpscaling` não faz nada aqui.** O log explica: `NR upscaling is not applicable: the
  game's DLSS already renders at output resolution (1276x711 vs output 1276x711). NR continues on
  the native path.` O NR sempre roda no caminho nativo no nosso pipeline.
- **Não há preset nem estilo mais barato** para oferecer.

---

## 6. Atualizar o Feeder (0.7.0 → 0.12.0): sem justificativa de latência

Os números dizem onde o custo mora: com NR ligado a 3440x1400, o `feed CPU` é **0,59–0,74 ms
(5–6% do frame)**, enquanto a GPU fica 11,3 ms por frame ocupada. **O custo é a avaliação da
feature DLSSNR, não o trabalho do Feeder.** A mudança do 0.12.0 que importaria (FSR 1 em vez de
bilinear na re-expansão) só age quando `work_resolution` < 100%, que está fora da mesa.

→ **Não levaria a atualização ao usuário como argumento de latência.** Ela não reduz o custo do NR
na mesma resolução. Se houver motivo de nitidez, é outra conversa e não é medição minha.

---

## 7. "Modo baixa latência" — o que é honesto expor

Do que foi medido, a única coisa que ainda muda latência de forma relevante é **desligar o
Neural Rendering**: −3 a −4,5 ms a 1080p, ~−10 ms e +16 fps a 3440x1400.

Tudo o mais que reduz latência já está ligado por padrão e não custa imagem (seção 3).

O que eu colocaria no painel:

- O botão de baixa latência é, na prática, **o interruptor do Neural Rendering**. Que ele diga
  isso com todas as letras — algo como *"Resposta rápida — desliga o Neural Rendering
  (~10 ms a menos, +16 fps em 3440x1440; a imagem perde o realce do DLSS 5)"*. O padrão do app
  continua o modo qualidade, com NR ligado.
- **Não** criar um preset que baixa os sliders "para reduzir latência": pelos meus números seria
  propaganda falsa — eles não custam GPU.
- **Não** oferecer `work_resolution` nem resolução menor (decisão do usuário), e por sorte
  também não valeriam: o ganho medido estava na borda do ruído.
- Nos tooltips do V-Sync e do teto de fps, dizer que os dois **aumentam** a latência. O teto
  ainda quantiza na taxa da fonte: com a fonte a ~92 fps, pedir 60 entrega 45 e pedir 72 entrega
  56, porque a condição só pode disparar quando chega frame novo.

---

## 8. Uma hipótese sobre "senti a latência" — raciocínio, não medição

A 1080p o NR acrescenta ~4 ms de relógio. É pouco para alguém sentir.

O NR é um reconstrutor **temporal**: acumula ao longo dos frames usando motion vectors. No nosso
caso os motion vectors são **estimados** pelo LumeniteFX Kernel a partir da imagem recapturada
(`DLSS5_MV_PROVIDER=3`), não vêm do motor do jogo. Motion vector aproximado em reconstrução
temporal produz arraste no movimento, e arraste é o que o olho lê como "atraso" mesmo com o
relógio parado.

Deixo explícito: **isto é raciocínio a partir de como a técnica funciona, não medição.** Se
alguém quiser testar, o experimento é visual (olhar movimento rápido com NR ligado e desligado) e
é de graça em fps — mas ele contraria a prioridade "qualidade > latência", então só faz sentido
se o usuário quiser investigar o arraste em si.

---

## 9. O que ficou sem medição

1. ~~Confirmação do teto de fps~~ — **fechado na seção 4.2: o teto não era a causa.**
   A referência sem teto deu o mesmo 6,8 ms. Nada a fazer aqui.
2. **Medida objetiva do arraste temporal.** Não consegui e não tentei improvisar: o backbuffer é
   `FLIP_DISCARD`, não dá para ler de volta depois do Present, e a saída do NR só existe dentro da
   cadeia do ReShade, depois do nosso `Present`. Medir "quantos frames uma borda leva para
   estabilizar" exigiria um caminho de captura próprio (ler o backbuffer antes do flip, ou capturar
   a nossa janela com um segundo processo) — trabalho de outra ordem, que não cabia nas 4
   execuções autorizadas. A hipótese da seção 8 continua sendo raciocínio, não medição.
3. **Ganho de imagem dos sliders no máximo.** Não fiz PSNR/SSIM nem crop comparativo — é teste
   visual e não é minha área.
4. **Sessão sem outro agente na GPU.** Carga externa de 11–26% nas primeiras rodadas e de
   **45–64%** nas 4 execuções com a janela visível. Nessa última faixa o loop passou a perder
   metade dos frames capturados, o que distorce tudo. Qualquer reconfirmação precisa da GPU livre.

---

## 10. Bancada

`C:\Users\PCDOFAFA\AppData\Local\Temp\claude\C--Users-PCDOFAFA-Desktop-dlss-cam\c9f972eb-7d23-432a-b980-290e431b1700\scratchpad\`

- `nr\` — cópia isolada do projeto + cadeia ReShade, com `lat_test.exe`. Dois patches só de
  bancada: `DLSS5_WINPOS` (joga a janela para fora da área de trabalho) e a medição `espera-GPU`
  em volta do `WaitForSingleObjectEx`.
- `run-nr.ps1` — roda uma configuração: `-W -H -Uplift -Work -Intensity -LocalTone -LocalStruct
  -SkinStruct -Preset -Style -Upscaling -Fps -Vsync`. Reescreve só as cópias isoladas.
- `lattarget.exe` — alvo animado. Precisa ficar **fora** da pasta com `dxgi.dll`, senão o ReShade
  engancha nele também (foi o primeiro erro desta rodada e contaminou a primeira leitura).

A medição `espera-GPU` vale a pena manter no `screen_capture.h` do projeto — é uma linha de log e
é o único jeito de ver custo de GPU virando latência. Ainda **não apliquei** lá, porque precisa de
duas linhas no `main.cpp` para ser alimentada e o arquivo está com o agente `hud`.
