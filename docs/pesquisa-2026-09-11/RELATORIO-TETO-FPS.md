# Teto de fps da película (avançado), remoção do idioma e correção crítica de escala — 11/09/2026

Pedido do usuário, nas palavras dele:

> "a gente precisa colocar uma configuração avançada que define somente o fps max da
> película, que a gente possa usar pra configurar — por exemplo eu to testando o programa no
> modo dual gpu e nesse caso não faz sentido a película ter o fps travado."

Reforço, na segunda mensagem: é a **limitação de fps de verdade** (travar o present num valor
escolhido), **não** o "pular quadro por razão", que é outra coisa e já existe.

---

## 1. O que eu encontrei (a premissa da tarefa estava desatualizada)

**Não existe mais "TETO DE FPS DO FILTRO" com chips Máx / 60 / 45 / 30 / 20 no painel.** Foi
retirado em **07/09/2026** e substituído por **pulo de quadro por razão** (`kPulos = {1,2,3,4}`,
"Todos / 1 de cada 2 / 3 / 4"). Consequência: **a película já rodava sem teto por padrão.**

### Por que tiraram em vez de só consertar? (pergunta do líder)

Fui ao histórico. O último commit da árvore é **c11b598, 07/09/2026 04:29**, e ele ainda tem o
teto antigo inteiro — o `kPulos` **nunca chegou a ser commitado**, vive só na árvore de
trabalho. Não existe commit nenhum, em toda a história do repositório, falando de teto de fps
ou de pulo de quadro.

O código do teto antigo, extraído desse commit:

```cpp
bool capOk = true;
if (C.fpsCap > 0) {
    double tnow = QpcMs();
    capOk = (tnow - lastPresentMs) >= (1000.0 / (double)C.fpsCap) - 0.5;
    if (capOk && got) lastPresentMs = tnow;      // <- AQUI. O resto morre todo quadro.
}
if ((got && !hidden && capOk) || now - lastPresent >= gap) {   // <- e o ocioso furava
```

São **dois defeitos, não um**, e o segundo nunca foi citado em lugar nenhum: o present ocioso
não passava pelo `capOk`, então nos primeiros 45 s (quando o gap ocioso é de 16 ms) o teto era
furado por baixo, fosse qual fosse o valor. A correção dos dois cabe em poucas linhas.

**A resposta honesta é que não há registro de ninguém ter tentado corrigir.** O único texto
que sobrou sobre a decisão é o comentário do `kPulos` (main.cpp:3353, a causa raiz citada pelo
líder). Ele abre com uma **preferência** do usuário ("prefiro o 1x 2x 3x 4x manual pra eu
controlar") e só depois afirma "POR QUE TROCOU, e não é preferência". As duas coisas estão no
mesmo parágrafo: o defeito do relógio entrou como justificativa de uma troca que já tinha sido
pedida. Nada indica que a correção tenha sido considerada e recusada — indica que ninguém
olhou para ela. Isso está escrito no comentário do código novo, citando main.cpp:3353.

---

## 2. O teto de fps da película (avançado)

### Onde fica e como é

Seção **"TETO DE FPS DA NOSSA IMAGEM (AVANÇADO)"**, coluna "Mais opções", logo abaixo de
"FPS DO JOGO" — a vizinhança é proposital: são as duas únicas seções do painel que falam em
fps, e uma diz "jogo" e a outra diz "nossa imagem".

```
TETO DE FPS DA NOSSA IMAGEM (AVANÇADO)
[ −10 ][ −1 ][      72 fps      ][ +1 ][ +10 ]
[        Tirar o teto                        ]
```

**Por que +/− e não uma régua de chips fina** (24/30/48/60/72/90/120/144/165/Sem limite):
continuaria sendo o que o usuário pediu para NÃO ser — uma lista de valores prontos, só que
maior. Com dois passos (1 e 10) qualquer número inteiro é alcançável em poucos cliques, e o
painel é GDI+ desenhado à mão: um campo de texto nativo aqui seria o único do app inteiro.

Regras: `0` = sem limite e é o padrão; sair de "Sem limite" cai no **refresh do monitor**;
passar do topo da escala vira "Sem limite"; `+1` e `+10` ficam apagados quando já está sem
limite; mínimo 10 fps.

### É um mecanismo separado do pulo por razão (pedido explícito do usuário)

Nada foi implementado em cima do outro. São dois laços diferentes:

| | pulo de quadro (`kPulos`, primeira tela) | teto de fps (avançado, Mais opções) |
|---|---|---|
| conta | quadros **capturados** | **tempo** (QPC) |
| regra | "processe 1 de cada N que chegarem" | "não apresente mais que X por segundo" |
| a taxa | **acompanha o jogo**: fonte a 120 com 1 de cada 2 dá 60; se o jogo cair para 80, cai para 40 | **não acompanha**: para em X e fica lá |
| serve para | economizar placa | fixar um número (bater o refresh, comparar medições) |

**Ligados juntos eles se compõem em cascata, e o resultado fica um pouco ABAIXO do mais
apertado dos dois — não exatamente o menor deles.** Eu tinha escrito "o resultado é o menor dos
dois"; fui medir e não é. Captura a ~89/s:

| configuração | present/s | contra o mais apertado sozinho |
|---|---|---|
| pulo "1 de cada 2", sem teto | 44,5 | — |
| pulo "1 de cada 2" + teto 90 (folgado) | 46,3 | o pulo manda, sem perda |
| pulo "1 de cada 2" + teto 45 (perto) | **41,4** | ~7 % abaixo dos 44,5 do pulo |
| pulo "1 de cada 2" + teto 20 (apertado) | **18,4** | ~6 % abaixo dos 19,6 do teto sozinho |

**A perda é de quantização, e tem explicação exata:** o teto só sabe RECUSAR o candidato que
chegou, e com o pulo ligado os candidatos são raros. Um quadro que chega pouco antes do prazo é
recusado, e o próximo candidato só aparece um ciclo inteiro de pulo depois — o intervalo entre
os dois fica maior que o pedido. Quanto mais próximas as duas taxas, mais isso acontece; com o
teto folgado, quase nunca.

Os dois convivem sem surpresa grande, mas não é de graça: **quem quer um número exato deixa o
pulo em "Todos" e usa só o teto** — ali a entrega ficou em 98 % do pedido. Está escrito assim
no comentário do laço.

### O conserto do defeito

```cpp
tetoPrazo += tetoIntervalo;                     // AVANÇA, não é reposto no present
const double t = tPresent0 + durPresent;        // sem leitura nova de relógio
if (tetoPrazo < t) tetoPrazo = t + tetoIntervalo;   // engasgou: recomeça, sem rajada
```

Relógio QPC (`GetTickCount64`, de passo ~15 ms, não separa 60 de 72). O teto **não vale
enquanto a camada aquece** (o Feeder conta presents até criar a feature) e **vale também para
o present ocioso** (nos primeiros 45 s o gap ocioso é 16 ms e furaria qualquer teto abaixo de
60).

A decisão fica em `tetoSegurou`, uma variável com nome, e `presentPorQuadro` **ficou com a
semântica original, intocada** — a condição é composta por fora, numa linha
(`vaiApresentar = (presentPorQuadro || presentOcioso) && !tetoSegurou`). Isso foi um pedido do
agente fluxo-optico, que precisa distinguir "não apresentei por intermitência" de "não
apresentei por teto" para o modo `sintetizar`.

### Persistência

Chave `[ultima] fps_teto_pelicula`. **A ausência da chave significa coisa diferente do zero**,
e é isso que sustenta a regra pedida:

| ini | significado | confirmado no log |
|---|---|---|
| chave ausente | ninguém nunca mexeu | `sem limite (padrao - ninguem mexeu)` |
| `=0` | escolheu "Sem limite" com o dedo | `sem limite (escolhido)` |
| `=N` | escolha manual — respeitada, nunca sobrescrita | `45 fps (do ini; refresh do monitor 60 Hz)` |

Por isso `TetoGravarIni` só é chamado no clique, jamais na abertura.

### Modo de duas placas

`DuasPlacas()` = duas ou mais placas de hardware **E** a placa do filtro escolhida
deliberadamente (`[ultima] placa` ou `--placa`). As duas condições juntas, e não só "tem duas
placas": todo notebook híbrido tem duas, e ali jogo e filtro costumam rodar na mesma.

**Isso não muda padrão nenhum, de propósito:** o padrão já é sem limite com uma placa ou com
duas. Serve para o painel **explicar** — "você separou as placas: a do filtro não disputa com
o jogo" + "por isso o padrão aqui é sem limite — travar só limitaria à toa". Com teto manual, a
segunda linha vira, em âmbar, "com as placas separadas, esse teto está segurando a imagem sem
precisar": **um aviso, não uma correção.** Acendeu sozinho nesta máquina (RTX 4080 + Radeon).

---

## 3. Correção CRÍTICA: a escala do painel (o "os botões não funcionam")

Achado do agente testar-botoes, aplicado por mim porque eu já estava nesta região.

`Paint()` põe `g_scale` no DPI, depois **encolhe essa mesma global** se o cartão não couber, e
desenha com ela. No Paint seguinte, a saída adiantada do "nada mudou" acontece **depois** do
`UpdateScale` e **antes** do encolhimento — então a global volta ao DPI enquanto a tela mostra
pixels desenhados na escala menor. O `HitTest` dividia por essa global.

Correção: `g_scaleDesenhado`, gravada no instante em que os pixels saem (logo após
`Render(&g)`), e usada por `HitTest`, pelo `WM_NCHITTEST` (a margem da sombra) e pelo despejo
do `DLSS5_LOG_ITENS`. O log de "painel aberto" passou a mostrar as duas (`escala` e `dpi`).

**Verificado por mim, com cliques reais:** 6 cliques em 6 botões distintos do teto, todos
acertando o alvo publicado pelo app (seção 4).

---

## 4. Remoção do seletor de idioma do painel

Era o **terceiro** lugar do programa onde se troca de idioma. Os outros dois vêm antes na
ordem de uso e ambos gravam `[idioma] lingua=` no ini, que é lido em toda abertura
(`CarregarIdioma`, antes de qualquer texto ir para a tela):

1. o **launcher portátil** (chips "Português / English" — grava no ini dos dois módulos);
2. o **seletor de fonte**, primeira tela do app (`seletor_hud.h`, `K_LANG` → `GravarIdioma`).

### Achei um caminho que fica preso, e mantive a saída (resposta ao pedido do líder)

Quem abre o app com a fonte na linha de comando (`--source` / `--window` / `--monitor` /
`--overlay`) cai no ramo `fromCli` do `wWinMain`, e ali **o seletor não abre** — é um `if
(!fromCli)`. Essa pessoa tem painel e não teria como trocar de idioma sem editar o ini à mão.

**Solução: os chips só aparecem nesse caminho.** No uso normal (launcher ou seletor) a seção
some; em `fromCli` ela fica. Uma linha no log diz qual caminho valeu. As camadas de cima
(`--camada 2/3`) também pulam o seletor, mas não têm painel, então não entram na conta.

O layout fechou sem buraco: com a seção ausente, "Ver os atalhos" sobe direto, e o cartão
encolheu de 1170 para 1081 px. Ver `painel-teto-semlimite.png` (sem) e `painel-com-idioma.png`
(com, via `DLSS5_SHOT_IDIOMA=1`).

---

## 5. Medições

Cópia isolada: `DLSS5ForAll-AMD\bin-teste-fpsavancado`. Fonte: janela, alvo repintando sem
parar. `present/s` = `apresentados` do log dividido pelo tempo medido.

### Fps entregue contra fps pedido (monitor de 60 Hz)

| teto pedido | apresentados | tempo | present/s | comentário |
|---|---|---|---|---|
| sem limite | 4532 | 46,5 s | **97,5** | o app não segura nada |
| 200 fps | 3972 | 41,5 s | **95,7** | acima do que a fonte entrega: quem manda é a captura |
| 72 fps | 2860 | 41,5 s | **68,9** | 95,7 % do pedido |
| 45 fps | 2056 | 46,5 s | **44,2** | 98,2 % do pedido |
| 20 fps | 421 | 21,5 s | **19,6** | 98,0 % do pedido |

**Antes / depois.** O teto antigo, pedindo 45 com fonte a 60, entregava 30 — 33 % abaixo. O
novo entrega entre 95,7 % e 98,2 % do pedido.

### Cliques reais nos botões (mouse de verdade)

Autorizado pelo usuário, feito só no monitor secundário, com o cursor devolvido ao lugar. As
coordenadas vieram do próprio app (`DLSS5_LOG_ITENS=1`), o que só funciona com a correção de
escala da seção 3. Monitor do alvo: **100 Hz**.

| clique | o app registrou | valor |
|---|---|---|
| "Mais opções" | `clique em 403,905 -> item 11` | coluna abriu (16 → 41 itens) |
| `−1` (saindo de sem limite) | `item 27` | **100** (cai no refresh do monitor) |
| `−1` | `item 27` | 99 |
| `−10` | `item 26` | 89 |
| `+10` | `item 29` | 99 |
| `+1` | `item 28` | 100 |
| "Sem limite" | `item 30` | 0 — e o ini ficou `fps_teto_pelicula=0` |

Os botões `+1` e `+10` vieram com `ativo=0` no despejo enquanto o teto estava em "Sem limite",
confirmando que ficam apagados. A reabertura seguinte leu **`sem limite (escolhido)`**,
provando a distinção entre chave ausente e zero.

---

## 6. "Sem limite" remove mesmo qualquer limitação? — limites conhecidos

Sim, do lado do app. Varri o laço atrás de qualquer outra coisa que prendesse a cadência:

- **`fpsCap`** — não existe mais; o que existe é este controle.
- **present ocioso (16 ms / 500 ms)** — é **piso**, não teto: acrescenta presents quando nada
  chega, para a cadeia do ReShade não dormir.
- **`WaitForPresentSlot`** (swapchain waitable, `SetMaximumFrameLatency(1)`) — espera a GPU
  liberar, não uma cadência fixa.
- **V-Sync** — esse **sim** trava no refresh, e é um controle separado, padrão desligado.
- **pulo de quadro** — corta por razão de quadros capturados (ver a tabela da seção 2).

### O limite físico, medido, CONTRARIANDO o que eu tinha escrito primeiro

A primeira redação da legenda dizia "sem limite = o que a captura entregar, **e ela não passa
dos %d Hz do monitor**". **A medição desmentiu:** num monitor de 60 Hz a película apresentou
**97,5 fps**. A captura WGC de janela entrega acima do refresh (o comentário do V-Sync em
`DrawAndPresent` já dizia "WGC a ~100 fps") e o nosso `Present(0,0)` não espera vblank nenhum.

Duas consequências, aplicadas:

1. A legenda passou a dizer só o que é verdade: *"sem limite = o app não segura nada; o topo
   vira o que a captura entregar"*. Nenhum número prometido.
2. A escala **não** para no refresh — num monitor de 60 Hz um teto de 90 ainda corta. O topo é
   240 (ou o refresh, se for maior).

**O limite que sobra e não dá para remover:** quem entrega quadro é o Windows Graphics
Capture, e o app não faz o WGC entregar mais do que ele entrega. Não é um teto fixo e
conhecido — depende da fonte, da janela e do compositor. É por isso que a linha da UI fala em
"o que a captura entregar" em vez de um número.

---

## 7. Dois achados de fora do escopo, para quem for pegar

1. **O painel não cabe na tela desta máquina e a parte de baixo fica atrás da barra de
   tarefas.** Medido: painel aberto em `1920,16` com **808x1352 px**, no monitor secundário,
   cuja área útil tem **912 px de altura** (`DISPLAY1 bounds 2293x960, workingArea 912`). O
   `WindowFromPoint` no botão "Mais opções" (y=921) devolveu `Shell_SecondaryTrayWnd` — a
   barra de tarefas, não o painel. O código PREVÊ o encolhimento (`AlturaDisponivel`), mas ele
   não disparou nesta abertura: a escala desenhada saiu em 1,43, igual ao DPI puro. Isso pode
   ser exatamente o "a metade de baixo do painel fica morta" relatado pelo testar-botoes, por
   uma causa adicional à do `g_scale`. Não mexi — é território dele.

2. **Automação de UI neste projeto exige processo DPI-aware.** O PowerShell é DPI-unaware e o
   Windows virtualiza as coordenadas dele (fator medido: **1,498** nesta máquina), então
   `SetCursorPos` num ponto que o app publicou cai em outro lugar. A cura é uma linha:
   `SetProcessDpiAwarenessContext(-4)` antes de qualquer clique. Foi o que destravou o teste
   de clique real. Vale registrar para os próximos agentes de QA.

---

## 8. Um erro meu, registrado

Numa série de execuções, meu script de teste lia o ini com `Get-Content -Raw` (sem
`-Encoding`) e regravava com `WriteAllText` em UTF-8. O título da janela alvo tem acentos:
cada passada recodificava os bytes e **o arquivo dobrava de tamanho**. Em algumas rodadas o
`dlss5forall.ini` da cópia de teste chegou a **1,1 GB**.

Atingiu **apenas** `bin-teste-fpsavancado`; restaurei do `bin\`, e conferi que todos os outros
`dlss5forall.ini` da árvore continuam com 628-807 bytes. Os scripts passaram a escrever o ini
pela mesma API do app (`WritePrivateProfileStringW`), que mexe só na chave pedida.

---

## 9. Arquivos

- `amd\main.cpp` — bloco "TETO DE FPS DA PELÍCULA", seção do painel em `colunaMaisA`, handler
  `CMD_TETO_BASE`, aplicação no laço (`tetoSegurou` / `vaiApresentar`), `g_scaleDesenhado`,
  seção IDIOMA condicionada a `pulouSeletor`.
- `DLSS5ForAll-AMD\bin-teste-fpsavancado\` — cópia isolada de teste.
- `painel-teto-semlimite.png` — o estado padrão (sem idioma, com as linhas de duas placas).
- `painel-teto-72.png` — com teto manual e o aviso âmbar.
- `painel-com-idioma.png` — o caminho que pulou o seletor, com os chips de idioma.
- `painel-teto-en.png` — em inglês, conferido contra truncamento.

Nada em `bin\`, `bin-teste-passes\`, `bin-teste-botoes\` ou `bin-teste-fluxoptico\` foi
tocado. Nenhum processo que eu não tenha criado foi encerrado.
