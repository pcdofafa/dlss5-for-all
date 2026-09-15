# O pulo de quadro — o que é, o que compra, o que cobra

Escrito em 2026-09-08 para o autor do app e para quem for explicar a ferramenta a quem usa.

**Como ler as marcas.** Cada afirmação deste documento carrega de onde veio:

| marca | significa |
|---|---|
| **[código]** | li no fonte, com arquivo e linha |
| **[log]** | li num log de execução real, com o horário |
| **[doc]** | vem de outro documento desta árvore, com a medição dele |
| **[conta]** | aritmética a partir de números **[log]**/**[doc]**, sem medição nova |
| **[extrapolação]** | conta que estende medições além dos pontos medidos — trate como estimativa |
| **[suposição]** | raciocínio meu, sem medição. Digo sempre qual medição faltaria |

---

## 1. Para que serve, em duas frases

O pulo faz o filtro processar só 1 de cada N quadros que ele captura do jogo: com "1 de cada 2",
metade do trabalho da placa que o filtro consumia volta a ficar livre — para o jogo, ou para
rodar mais passes de rede neural no quadro que sobrou.

O que se paga é fluidez: com "1 de cada 3", o que a pessoa vê atualiza um terço das vezes.

---

## 2. O mecanismo

### 2.1 Onde está

| peça | onde | o quê |
|---|---|---|
| a razão | `main.cpp:3221` | `static const int kPulos[] = { 1, 2, 3, 4 };` |
| os rótulos | `main.cpp:3224` | `NomePulo()` — "Todos", "1 de cada 2", "1 de cada 3", "1 de cada 4" |
| memória | `main.cpp:3244`–`3249` | `PuloGravarIni` / `PuloLerIni`, chave `[ultima] pulo_quadros`, grampeada em 1..4 |
| leitura no arranque | `main.cpp:9693` | `C.puloN = argPulo > 0 ? argPulo : PuloLerIni();` — um lugar só, para os quatro caminhos de entrada |
| linha de comando | `main.cpp:8936`–`8938` | `--pulo N`, grampeado em 1..4 |
| contador | `main.cpp:9997` | `int quadrosDesdeUltimo = 0;` |
| **a decisão** | `main.cpp:11487`–`11493` | o bloco abaixo |
| clique no painel | `main.cpp:10412`–`10420` | zera `quadrosDesdeUltimo` ao trocar a razão |
| chips e legendas | `main.cpp:5270`–`5332` | quatro linhas de texto, uma por pergunta |

### 2.2 A decisão, literal **[código]**

`main.cpp:11487`:

```cpp
bool capOk = true;
if (C.puloN > 1 && got) {
    capOk = (++quadrosDesdeUltimo >= C.puloN);
    if (capOk) quadrosDesdeUltimo = 0;
}
if ((got && !hidden && capOk) || now - lastPresent >= gap) {
    LONGLONG p0 = g_lat.Now();
    ...
    R.DrawAndPresent();
```

Três propriedades que decorrem disso, e todas importam:

1. **Conta quadros, não tempo.** O contador só anda com `got` — quadro novo copiado da captura.
   O laço gira muito mais rápido que a fonte; contar por volta de laço faria o pulo depender da
   carga da máquina, que é exatamente o defeito do teto por relógio que ele substituiu **[código,
   `main.cpp:11480`–`11486`]**.

2. **O quadro descartado não é enfileirado — é jogado fora.** Quando `capOk` é falso, o laço
   simplesmente não apresenta; a textura da captura será sobrescrita pelo próximo `Tick()`. O
   quadro que a camada apresenta é **sempre o mais novo que chegou** **[código]**. Isso é o que
   separa o pulo de um limitador com fila: ele não atrasa o quadro exibido, ele espaça os
   quadros exibidos. Volto a esta distinção em §6.3.

3. **O pulo governa o `Present`, e o ReShade só roda a cadeia no `Present`.** Portanto quadro
   descartado não passa por SMAA, Lumenite_Kernel, DLSS5_Feed, DLSS, Neural Rendering nem
   DLSS5_Sharpen — nem publica na textura compartilhada para a camada 2, que é escrita de dentro
   do `reshade_finish_effects` **[código, `main.cpp:815` e ARQUITETURA §2]**.

### 2.3 Uma correção ao enunciado: o pulo **não** é grátis do lado da captura

O enunciado que circulou ("quadros descartados não passam por nada") é quase verdade, e a parte
que falta muda uma recomendação. O que o quadro descartado **ainda** paga **[código]**:

- **A captura e a cópia.** `scap.Tick()` roda antes da decisão (`main.cpp:11274`) e faz
  `CopySubresourceRegion` do quadro na GPU. O contador `copiado` do log é incrementado ali
  (`screen_capture.h:840`/`878`), isto é, **antes** do pulo. Custo pequeno (uma cópia BGRA de
  1920×1080), mas não zero.
- **Os motion vectors de hardware**, quando ligados. `MvHw::Frame()` está sob `if (got)`, em
  `main.cpp:11428`–`11429`, **acima** do bloco do pulo. Com `[captura] mv_hardware=1` o
  estimador do bloco de vídeo roda em todo quadro capturado, inclusive nos descartados.

A segunda tem uma consequência que não é só de custo, e ela é um **defeito latente**:

> **[código] Com `mv_hardware=1` e pulo > 1, o campo de MV entregue ao DLSS descreve o
> deslocamento errado.** `MvHw::Frame()` estima entre quadros capturados **consecutivos** e
> publica o SRV no add-on da ponte; cada chamada sobrescreve a anterior. Quando o quadro N é
> finalmente processado, o campo disponível é o do par (N−1 → N), enquanto a rede reconstrói a
> partir do último quadro **processado** — a N−3, com pulo 3. O vetor entregue tem um terço da
> magnitude do deslocamento real.
>
> **No padrão de fábrica isso não acontece**: `mv_hardware` tem padrão **0** (`main.cpp:8419`) e
> os MV vêm do `Lumenite_Kernel`, que roda dentro da cadeia do ReShade, ou seja **só nos quadros
> processados** — o histórico dele é o último quadro processado, e a magnitude sai certa.
>
> Isto é leitura de código, não medição. O teste que confirma: rodar com `mv_hardware=1` e pulo
> 3 e ler a sonda de MV do `dlss5-feed.log` (`Depth probe` / amostras de MV) com a câmera girando
> em velocidade conhecida; a mediana deve sair perto de 1/3 do esperado.

---

## 3. A razão é exata — e está comprovada no log de hoje

O painel promete "a razão é exata; o fps, não" (`main.cpp:5320`). O log da sessão de teste de
hoje confirma, com precisão de um décimo de fps **[log,
`DLSS5ForAll-AMD\bin-teste-passes\dlss5forall.log`]**:

| horário | pulo | chegada | copiado | present | `copiado/N` |
|---|---|---|---|---|---|
| 04:38:41 | 2 | 60,1 | 60,1 | **30,0** | 30,0 |
| 04:40:35 | 4 | 60,0 | 60,0 | **15,0** | 15,0 |
| 04:41:05 | 4 | 94,7 | 94,6 | **23,7** | 23,7 |
| 04:45:46 | 3 | 97,9 | 89,8 | **29,9** | 29,9 |
| 04:46:06 | 3 | 98,3 | 89,9 | **30,0** | 30,0 |

E a saída é **regular**: nas dez janelas seguidas entre 04:45 e 04:46, o intervalo típico entre
presents ficou em 30,5–30,8 ms, p99 41,2–41,8 ms, **0 quadros acima de 120 ms** **[log]**. O
mecanismo cumpre exatamente o que o comentário do código promete.

**Um detalhe que a tabela revela e que não é do pulo:** às 04:45 a captura entregava 97,9 e o
laço copiou 89,8. Os ~8 quadros/s de diferença são descartados **antes** do pulo, no dreno da
fila do WGC (só o mais novo é copiado) — a camada já perdia quadros sem pulo nenhum, porque a
fonte chegava mais rápido que o ciclo do laço **[log + código, `screen_capture.h:878`]**.

---

## 4. O que o pulo compra: orçamento de GPU por quadro processado

### 4.1 O mecanismo, e ele é simples

A cadeia roda uma vez por `Present`. Com pulo N e captura a `f` quadros/s, os presents ficam
espaçados de `N/f` segundos. Esse é o tempo que a GPU tem para terminar a cadeia inteira sem que
a camada perca o ritmo:

```
orçamento por execução da cadeia  =  N / f_captura
```

**[conta]**, e ela vale para qualquer coisa que só rode dentro do `Present`: os passes de Neural
Rendering, o DLSS, o Kernel, o SMAA, o Sharpen.

| f_captura | N=1 | N=2 | N=3 | N=4 |
|---|---|---|---|---|
| 60 fps (monitor 60 Hz) | 16,7 ms | 33,3 ms | 50,0 ms | 66,7 ms |
| 99 fps (o monitor desta máquina) | 10,1 ms | 20,2 ms | 30,3 ms | 40,4 ms |
| 144 fps | 6,9 ms | 13,9 ms | 20,8 ms | 27,8 ms |

**Confirmando o número que o lead trouxe:** os ~4,2 ms de GPU por passe estão medidos e são
reais. `ARQUITETURA.md` §10 (correção de 2026-09-08 03:35), Witcher 3, 1920×1080, RTX 4080, mesma
cena, três pontos em sequência **[doc]**:

| passes | fps | quadro |
|---|---|---|
| 6 | 30,5 | 32,78 ms |
| 8 | 23,8 | 42,05 ms |
| 10 | 20,1 | 49,71 ms |

De 6→8 são +9,3 ms para 2 passes; de 8→10, +7,7 ms. **~4,2 ms de GPU por passe.**

Ajustando uma reta a esses três pontos, o quadro fica em **≈ 4,9 ms + 4,64 ms × passes**
**[extrapolação]** — o intercepto é o resto da cadeia (captura, Kernel, SMAA, DLSS, Sharpen,
composição). Cruzando com a tabela de orçamento, quantos passes cabem:

| | N=1 | N=2 | N=3 | N=4 |
|---|---|---|---|---|
| captura a 60 fps | 2 passes | 6 | 9 | 10 (teto) |
| captura a 99 fps | 1 passe | 3 | 5 | 7 |

**[extrapolação]** — os únicos pontos medidos são 6, 8 e 10 passes a 1920×1080 numa cena
específica. A tabela é para dimensionar, não para prometer. Duas ressalvas que o próprio
`ARQUITETURA.md` §10 registra: os pontos de 1 e 4 passes que aparecem lá foram medidos **em outra
cena** e não fecham com esta curva; e a carga de GPU depende do que está na tela, então pontos de
cenas diferentes não formam curva.

### 4.2 A tensão que ninguém pode ignorar: o pulo compra **uma** coisa, não duas

Os chips do painel dizem "menos placa no filtro, mais placa para o jogo" (`main.cpp:5316`). Isso
é verdade **enquanto o número de passes não muda**. Se a pessoa usar o orçamento comprado para
subir os passes, a GPU volta a ficar igualmente ocupada e o jogo não recebe nada de volta.

```
pulo 2 + passes fixos   ->  a placa sobra, e a sobra vai para o jogo
pulo 2 + passes dobrados ->  a placa continua cheia; o que muda é a imagem por quadro
```

São dois usos legítimos e **mutuamente exclusivos** do mesmo controle **[conta]**. Quando o
painel ganhar os botões de 1..10 passes (pendência 3 de `ARQUITETURA.md` §10), essa escolha vai
precisar aparecer na tela — senão a pessoa sobe os dois e conclui que o pulo "não fez nada".

---

## 5. O que o pulo custa em qualidade

### 5.1 Fluidez: o custo óbvio, e é exatamente a razão

Com pulo 3 e captura a 90 fps, a tela atualiza 30 vezes por segundo. Não há sutileza aqui: é o
preço anunciado.

### 5.2 Qualidade temporal: o custo que não está no rótulo

DLSS e Neural Rendering são reconstrutores **temporais** — acumulam histórico e o reprojetam com
motion vectors. Pular quadros multiplica por N o deslocamento entre dois quadros consecutivos
**do ponto de vista da rede** **[conta]**.

Quanto é isso em pixels? Para um giro de câmera:

```
px por quadro processado = ω[°/s] × (largura[px] / FOV[°]) × N / f_captura
```

A 1920 px com FOV horizontal de 90° são 21,3 px por grau. Com a captura a 60 fps:

| giro | N=1 | N=2 | N=3 | N=4 |
|---|---|---|---|---|
| 30 °/s (giro lento) | 10,7 px | 21,3 | 32,0 | 42,7 |
| 60 °/s | 21,3 px | 42,7 | 64,0 | 85,3 |
| 90 °/s (giro rápido) | 32,0 px | 64,0 | **96,0** | **128,0** |
| 180 °/s (giro brusco) | 64,0 px | 128,0 | 192,0 | 256,0 |

**[conta]**. Agora os limites medidos, de `docs/motion-vectors.md` **[doc]**:

- **Envelope da cadeia** (`chain_test`, o `PS_Publish` + `ValidateTests` do `DLSS5_Feed.fx`, que
  vale para qualquer origem de MV porque é a jusante): praticamente sem perda até **~16 px por
  quadro**; ~10 % dos vetores zerados a 32 px; ~20 % a 40 px; **desaba a partir de ~56 px**
  (47,7 % de sobreviventes). Quem zera é o teste `MV_CONSISTENCY`, e o documento argumenta —
  com varredura — que **não se deve relaxá-lo**: um vetor que difere do vizinho temporal em mais
  de 21 px é genuinamente ruim, e MV errado é pior que MV zero.
- **Alcance do estimador padrão** (`Lumenite_Kernel`, mip 5 com `SEARCH_RADIUS 3`): **±96 px**.
  Passando disso, o comportamento medido no VME — e o documento afirma o mesmo mecanismo — não é
  "vetor grande e errado", é **vetor pequeno e errado**: nenhum limiar de magnitude pega isso, e
  ele espalha o histórico na direção errada.
- **Ressalva de fidelidade que o próprio documento faz:** o `chain_test` usa ruído em blocos 2×2 e
  o alvo real tem detalhe mais fino; o run medido deu 46 % de sobreviventes a 40 px onde a
  simulação dá 83 %. **Os percentuais reais são piores que os simulados.**

Cruzando as duas tabelas **[conta]**:

| | veredito |
|---|---|
| pulo 2, giro até 30 °/s | dentro do envelope limpo |
| pulo 2, giro de 60 °/s | 42,7 px — ~20 % dos vetores zerados |
| **pulo 3, giro de 90 °/s** | **96 px — bate no alcance máximo do estimador** |
| **pulo 4, giro de 90 °/s** | **128 px — passa do alcance; o campo mente** |
| pulo 4, giro brusco | 256 px — a reconstrução temporal não tem o que reprojetar |

**A conclusão operacional:** o pulo alto não é neutro em imagem. Ele empurra a cena rápida para
fora do envelope onde os motion vectors ainda funcionam, e a degradação não é suave — a partir
do alcance do estimador ela troca "vetor ausente" (que o `distrust` trata) por "vetor errado"
(que ninguém trata).

### 5.3 Cena escura: onde o pulo tem pouco a piorar, e mesmo assim piora

`ARQUITETURA.md` §8 registra, medido: **os motion vectors colapsam em cena escura** — o
`Lumenite_Kernel` estima fluxo óptico na imagem, e sem contraste não há o que casar; amostras
zeradas com o personagem andando e a câmera girando, à noite **[doc]**.

Precisando o efeito do pulo aí **[suposição, com o mecanismo explícito]**: se o campo já é zero,
o pulo não o piora — a rede já estava reconstruindo sem reprojeção. O que piora é o **outro**
lado da conta: com o histórico N vezes mais distante e nenhum vetor para alinhá-lo, o
descasamento entre o histórico e o quadro atual cresce por N. O resultado esperado é mais
fantasma e mais arraste à noite, na razão do pulo.

**Não medi isso, e a medição existe:** rodar a mesma cena noturna com pulo 1 e pulo 3, gravar
`DLSS5_DUMP=<arquivo.png>` num quadro fixo depois de um giro conhecido (o mecanismo de dump está
em `main.cpp:11530`) e comparar as duas saídas com o quadro cru. Não é caro.

---

## 6. A tese do input lag: procede ou não?

A tese, nas palavras do autor:

> "eu gostei dessa ferramenta porque em teoria eu consigo reduzir o input lag do jogo fazendo o
> fps base aumentar e rodando o jogo com o fps destravado sem ter a rede neural descontrolada"

**Veredito curto: a segunda metade procede; a primeira, no modo sobreposto, se contradiz.**
O pulo realmente impede a rede de acompanhar o ritmo do jogo, e isso é um ganho real e medido.
Mas "destravar o jogo para baixar o input lag" só entrega o que promete se a pessoa **não estiver
olhando a camada** — e no modo sobreposto ela está olhando exatamente ela.

Abaixo o raciocínio inteiro, porque o veredito não é uniforme.

### 6.1 As duas metades do input lag, e elas não somam igual

**(a) apertar → o jogo processar.** Depende do fps do jogo. É onde a tese quer ganhar.

**(b) o jogo processar → aparecer na tela.** No modo sobreposto isso passa **inteiro** pela
camada, porque a camada cobre o jogo e é o que a pessoa vê **[código, ARQUITETURA §1]**.

O problema é que as duas metades têm curvas diferentes **[conta]**:

| fps do jogo | período de simulação | ganho sobre a linha anterior |
|---|---|---|
| 60 | 16,7 ms | — |
| 120 | 8,3 ms | −8,3 ms |
| 200 | 5,0 ms | −3,3 ms |
| 300 | 3,3 ms | −1,7 ms |

A parte (a) **satura**: de 60 para 120 fps são 8,3 ms; de 120 para 300, todos os 180 fps a mais
compram 5 ms. Já a parte (b) cresce **linearmente** com o pulo (§6.3). É uma troca ruim por
construção.

### 6.2 O teto que a tese não enxerga: a captura não passa do refresh do monitor

Este ponto é o que mais enfraquece a tese, e a boa notícia é que o teto é bem melhor do que o
documento antigo registrava.

O `docs/latencia.md` §4 documenta um teto de **50,0 fps** cravados. **Esse teto foi consertado.**
A causa foi encontrada e está registrada em `screen_capture.h:199`–`218` **[código]**: era o
`MinUpdateInterval` da `IGraphicsCaptureSession5`, cujo padrão de sistema é 16 ms; num monitor de
100 Hz o "no mínimo 16 ms" arredonda para **dois** vblanks — 20,00 ms = 50,0 fps. O app agora
pede 0 antes do `StartCapture`. Medido no mesmo alvo e mesma sessão, só mudando o campo **[doc,
`screen_capture.h:212`–`215`]**:

```
MinUpdateInterval = 16 ms (padrão)  ->  49,93 fps, gap 20,00 ms
MinUpdateInterval =  0 ms           ->  99,83 fps, gap 10,00 ms
Desktop Duplication (controle)      -> 100,25 fps, gap 10,00 ms
```

E o log de hoje confirma no jogo real: `captura: MinUpdateInterval 16.00 ms -> 0.00 ms` às
04:38:16, e `ritmo do WGC ... media 10,2 ms`, chegada 97,9–99,3 fps **[log]**.

**Mas o teto seguinte continua de pé, e é intransponível daqui:** o WGC entrega no vblank do DWM.
A captura **nunca passa do refresh do monitor** — é o que o comentário do código diz e o que o
comentário do painel repete (`main.cpp:11484`–`11486`, `main.cpp:5310`–`5312`) **[código]**.

Portanto, **respondendo à pergunta do lead**: um jogo a 300 fps num monitor de 99 Hz entrega
**~99** quadros/s à camada. Os outros ~200 são descartados pelo compositor, antes de existirem
para nós. O pulo não está descartando "os quadros extras do jogo destravado" — esses já foram. O
pulo descarta 2 de cada 3 dos **99 que sobreviveram ao compositor**.

Consequência direta: **acima do refresh do monitor, subir o fps do jogo não entrega mais nada à
camada.** Compra latência de simulação (parte a, e com retorno decrescente) e nada de imagem.

### 6.3 O que o pulo custa em latência de exibição, com números

Aqui vale a distinção que o código torna possível, e ela é favorável ao desenho:

> **O pulo não atrasa o quadro exibido — ele espaça os quadros exibidos.** Como o descarte não
> enfileira nada (§2.2), o quadro que sai no `Present` é sempre o mais novo capturado. A latência
> *daquele* quadro é a mesma com pulo 1 ou pulo 4 **[código]**.

O que muda é a **idade média da imagem que está na tela**, porque entre um quadro novo e o
seguinte a tela mostra o antigo por mais tempo:

```
idade média do que está visível  =  L_pipeline  +  T_present / 2
onde T_present = N / f_copiado
```

Com os números medidos hoje às 04:45–04:46 (Witcher 3, monitor 99 Hz, pulo 3) **[log + conta]**:

| parcela | valor |
|---|---|
| `srt` (o quadro já tinha esta idade quando o WGC nos avisou) | 8,5 ms |
| `acordar` + `slot` + `tick` + `present` | 1,45 + 1,90 + 0,18 + 1,73 = 5,3 ms |
| `PRESENT->TELA` | 15,2 ms |
| **L_pipeline** | **≈ 29 ms** |
| T_present com pulo 3 (`copiado` 89,8) | 33,4 ms → **+16,7 ms** de meia-espera |
| T_present com pulo 1 (mesmo `copiado`) | 11,1 ms → **+5,6 ms** |

→ **o pulo 3 acrescenta ≈ 11 ms à idade média do que a pessoa está vendo.** Na forma geral,
`(N−1) / (2 · f_copiado)` **[conta]**:

| f_copiado | N=2 | N=3 | N=4 |
|---|---|---|---|
| 60 fps | +8,3 ms | +16,7 ms | +25,0 ms |
| 90 fps | +5,6 ms | +11,1 ms | +16,7 ms |

**Ressalva honesta:** a linha "N=1" acima é aritmética, não medição. Não existe no log de hoje uma
janela com pulo 1 e a fonte a ~90 fps nesta configuração para confirmar que a camada aguentaria
apresentar 89,8. Os indicadores dizem que aguentaria — `stalls 0` em quatorze janelas seguidas de
600 quadros, feed a 4 % do frame, `DirectNeuralRenderingPassCount=1` no `ReShade.ini` **[log]** —
mas *dizem*, não *provam*. §10 traz como medir em dois minutos.

### 6.4 O golpe mais duro na tese: "jogo destravado" já foi medido, e foi o pior cenário

Isto não é raciocínio, é medição, e ela veio de uma queixa do próprio usuário
**[doc, `docs/pesquisa-2026-09-06/RELATORIO-DESEMPENHO-JOGO.md` §1 e §3]**:

> "Testei Resident Evil com fps destravado e a película rodava a 19 fps; quando travei o fps do
> jogo, joguei a 60 com a película."

E a bancada reproduziu o mecanismo com um jogo de teste saturando a placa:

| estratégia | película | jogo | present→tela |
|---|---|---|---|
| jogo sozinho, sem camada | — | 164,5 | — |
| **base: jogo livre, camada normal** | **9,3** | 153,7 | **328 ms** |
| + nossa prioridade de GPU = REALTIME | **99,2** | 148,2 | **17 ms** |

Duas leituras, e as duas contam a favor de partes diferentes:

- **Contra a tese:** jogo destravado é ativamente hostil à camada. Sem contramedida, a camada foi
  a 9,3 fps e o `present→tela` a 328 ms — a *parte (b)* do input lag explodiu enquanto a parte (a)
  ganhava alguns milissegundos. E o custo do jogo por ter a camada aberta foi de 164,5 → 153,7 fps:
  **7 %.** A tese troca 7 % de fps do jogo por um colapso da imagem que a pessoa vê.
- **A favor da tese, e é mudança real:** a prioridade de GPU REALTIME resolve isso, levou a
  camada de 9,3 a 99,2 fps, e **hoje é automática** — o log de hoje mostra
  `prioridade de gpu: 2 -> 5` às 04:38:14 **[log]**. Ou seja, o cenário catastrófico de 06/09 não
  é mais o cenário de fábrica. "Destravado" deixou de ser fatal.

Há ainda uma medição direta contra a metade "destravado" da tese, no comentário que sustenta os
chips de FPS DO JOGO **[código, `main.cpp:3264`–`3266`]**:

> "Esta firme: travar o jogo numa taxa fixa deixa o filtro nitidamente mais regular, e **deixar
> solto foi o pior de cinco configurações, por margem larga**. O NUMERO ainda não está."

Note o que está firme e o que não está: a **direção** (travar > soltar) está medida; o **valor**
(60? 90? 120?) não. O 60 é ponto de partida, não recomendação com fundamento — e o próprio
comentário registra o prazo de validade dessa marca.

### 6.5 Veredito

**A tese se sustenta em uma condição, e ela não é a condição em que o app costuma ser usado.**

| onde | procede? | por quê |
|---|---|---|
| **modo sobreposto** (a camada cobre o jogo) | **não, como formulada** | a pessoa vê a camada. Ganhar 3 ms de simulação (150→300 fps) e perder 11–17 ms de idade média da imagem (pulo 3) é troca negativa **[conta]**. E acima do refresh do monitor o fps extra do jogo não chega à camada **[código]** |
| **janela separada, olhando o jogo** | **sim** | a parte (b) sai da conta: o jogo apresenta direto na tela dele. O pulo devolve placa ao jogo e a camada vira monitor/gravação. Ver §8 |
| **jogo limitado pela GPU** | **sim, parcialmente** | aqui o pulo devolve placa ao jogo de verdade, e o fps do jogo sobe. Mas só se os passes ficarem fixos (§4.2) |

O que **procede sem ressalva** na intuição do autor é a outra metade, e ela é a mais valiosa:
**desacoplar o ritmo do filtro do ritmo do jogo.** Antes, filtro e jogo eram travados um ao
outro e a única saída era baixar o jogo. Com o pulo, o jogo pode andar no ritmo dele e o filtro
no ritmo que a placa aguenta — e a razão é exata, o que o teto por relógio nunca conseguiu ser
(pedir 60 entregava 50; pedir 45 entregava 30) **[código, `main.cpp:3204`–`3212`]**.

**A correção que eu faria no enunciado**, e é de uma frase: o pulo não reduz input lag. Ele
compra **orçamento de GPU** — e o que se faz com esse orçamento (devolver ao jogo, ou gastar em
passes de rede) é uma segunda decisão, que o painel ainda não pede.

---

## 7. Quando usar: a tabela

`P` = passes de Neural Rendering. Os passes hoje só se ajustam por `nr_passes` no
`dlss5-feed.cfg` (NR próprio, faixa 1..10, `dlss5-feed.cpp:1007` e `:1031`) ou por
`DirectNeuralRenderingPassCount` no `ReShade.ini` (add-on de terceiros) — **o painel ainda não
tem esse controle** (pendência 3 de `ARQUITETURA.md` §10) **[código]**.

| situação | pulo | passes | limitador do jogo | ganha | perde |
|---|---|---|---|---|---|
| **jogo leve, monitor 240 Hz** | **Todos (1)** | P=1 | livre | fluidez máxima; a camada acompanha a captura 1:1 | nada — não há orçamento a comprar; o teto é o monitor |
| **jogo pesado, monitor 60 Hz, GPU sobrando** | **Todos (1)** | P=2 | 60 (ou o que a placa segurar) | imagem em todos os quadros, 16,7 ms de orçamento | — |
| **jogo pesado, GPU saturada, quero o jogo fluido** | **1 de cada 2** | P **fixo** | 60 | ~metade do custo da camada volta para o jogo | a camada cai à metade; +8 ms de idade média a 60 fps |
| **quero a melhor imagem possível, fluidez é secundária** | **1 de cada 3** | P=5..9 (conforme f_captura, §4.1) | 60 | 50 ms de orçamento a 60 fps: cabe rede pesada | 20 fps na tela; +17 ms de idade média; cena rápida sai do envelope de MV (§5.2) |
| **monitor de alta taxa (100–144 Hz)** | **1 de cada 2** | P=3 | livre ou 120 | 20 ms de orçamento e ainda ~50 fps na saída | menos orçamento por passo do que a 60 Hz — o refresh alto **encurta** o orçamento |
| **cena escura / noturna** | **Todos (1)** ou 2 | P baixo | 60 | mantém o histórico próximo, que é o único apoio quando o MV colapsa | com pulo alto, arraste e fantasma na razão do pulo (§5.3) |
| **jogo competitivo, resposta acima de tudo** | não use o filtro no modo sobreposto | — | livre | a parte (b) do input lag some | sem filtro |

**Duas leituras contraintuitivas que valem repetir:**

- **Monitor de alta taxa dá MENOS orçamento, não mais.** O orçamento é `N / f_captura`, e
  `f_captura` sobe com o refresh. A 60 Hz o pulo 2 dá 33 ms; a 144 Hz dá 14 ms. Quem tem monitor
  rápido e quer rede pesada precisa de razão maior **[conta]**.
- **Travar o jogo em 60 e pulo 1 costuma ser melhor do que jogo livre e pulo 3**, se o objetivo é
  o que aparece na tela: a captura fica a 60, a camada apresenta 60, e o orçamento é o mesmo
  16,7 ms — sem os 17 ms de idade média nem o estresse no MV. **[conta]**, e o
  `RELATORIO-DESEMPENHO-JOGO` §3.1 aponta a mesma direção (travar deixa o filtro mais regular).

---

## 8. O modo janela separada muda a resposta?

**Muda, e muda o suficiente para inverter o veredito** — mas por um motivo que talvez não seja o
que se espera.

No modo janela separada o jogo continua desenhando na tela dele; o app tem uma janela própria
que espelha o resultado filtrado. Duas situações completamente diferentes:

**(a) A pessoa joga olhando a janela do app.** Não muda nada em relação ao sobreposto — pior,
até: agora o jogo **também** apresenta na tela, gastando GPU e um caminho de composição que
ninguém está usando. Toda a análise de §6.1–6.3 vale, com um custo a mais.

**(b) A pessoa joga olhando o jogo, e a janela do app é para outra coisa** — gravar, transmitir,
alimentar o Lossless Scaling (que é o cenário priorizado de `docs/latencia.md` §1). **Aqui a tese
do autor procede inteira:** a parte (b) do input lag sai da conta, porque o caminho até os olhos
da pessoa é o do jogo, sem a camada no meio. O pulo passa a ser puro ganho — devolve GPU ao jogo,
e a única coisa que piora é a taxa da gravação/transmissão.

**A ressalva do lead sobre o WGC vale duas vezes nesse cenário [doc, `latencia.md` §"Relevante
para o Lossless Scaling"]:** se o LS (ou o OBS) capturar a **nossa** janela por WGC, ele bate no
mesmo relógio, e agora sobre uma janela que apresenta a `f/N`. Com pulo 3 o interpolador recebe
30 quadros/s para interpolar. Isso é decisão consciente, não acidente — mas precisa estar dita.

> **Uma pergunta que fica aberta e é de produto, não de código:** hoje o chip do pulo aparece
> igual nos dois modos. No modo janela separada com uso (b), a recomendação é o oposto da do modo
> sobreposto. **[suposição]** de que o painel deveria dizer isso — não é medição, é observação de
> interface.

---

## 9. Interação com o limitador de fps do jogo

O app tem **um controle e dois motores** para o fps do jogo (`main.cpp:4552`–`4566`) **[código]**:

| motor | como | quando vale | disponível em |
|---|---|---|---|
| `MOTOR_AO_VIVO` | a peça `limitador/fpslimit_host.h` instalada na pasta do jogo | **na hora** | onde a peça instala e o anticheat não recusa |
| `MOTOR_DRIVER` | NvAPI DRS, chave `FRL_FPS_ID` no perfil do exe | **só na reabertura do jogo** | NVIDIA |
| `MOTOR_RECUSADO` | a peça está lá e recusou (anticheat) | — | cai para o driver |
| `MOTOR_NENHUM` | nem peça nem driver | — | **é o caso AMD de hoje**: `LimiteJogoDisponivelAmd()` devolve `false` **[doc, §5 do RELATORIO]** |

As combinações, com o que cada uma faz **[conta]**:

| limitador | pulo | resultado | vale a pena? |
|---|---|---|---|
| livre | 1 | jogo come a placa; camada disputa. Hoje sobrevive pela prioridade REALTIME | só com GPU sobrando |
| livre | 2..4 | camada a `f_monitor/N`; a placa que ela devolve o jogo consome inteira | **sim, se o objetivo for o jogo** |
| 60 | 1 | captura 60, camada 60, orçamento 16,7 ms. **O par mais previsível** | **sim, é o padrão sensato** |
| 60 | 2 | camada a 30, orçamento 33 ms. Espaço para rede pesada | **sim, para imagem** |
| 60 | 3..4 | camada a 20/15. Orçamento grande, fluidez ruim | só para cena lenta |
| 30 | 2..4 | camada a 15/10/7,5 | **não** — se anulam: o limitador já cortou a chegada, o pulo corta de novo |

**A regra em uma frase:** os dois cortam a **mesma** coisa (quantos quadros a camada processa),
em pontos diferentes da cadeia. Limitar o jogo corta na origem — devolve GPU ao sistema inteiro e
mantém a camada 1:1 com a fonte. Pular corta no destino — devolve GPU só do que a camada gastava,
e desacopla os dois ritmos. **Usar os dois com força é dividir duas vezes**, e o resultado (7,5
fps com limitador 30 e pulo 4) não serve para nada.

**Um perigo já neutralizado no código, que vale conhecer** — porque quem mexer ali pode
reintroduzi-lo. `main.cpp:9908`–`9913` **[código]**:

> "O TERCEIRO ARGUMENTO É ZERO, E NÃO O PULO. (…) Passar a razão ali seria desastre silencioso:
> um ini com `modo=igual` mais '1 de cada 2' limitaria o **JOGO** a 2 fps, e o número é plausível
> demais para alguém desconfiar."

O modo `JOGO_IGUAL` saiu dos chips em 06/09 mas continua sendo lido de um ini antigo. Com pulo
por razão não existe "teto de fps da película" para igualar, então a resposta certa é **nenhum
teto** — e é o que o código passa.

### A camada 2 não multiplica o pulo (verificado)

O `--pulo` **não** é repassado na linha de comando da camada 2 (`main.cpp:3819`–`3822`): ela
recebe `--camada 2 --source window --window-hwnd … --overlay`, mais transporte e placa **[código]**.
A camada 2 lê `pulo_quadros` do **próprio** `bin-camada2\dlss5forall.ini`. Como a camada 1 só
publica no `Present`, ela já entrega 1 de cada N — e a camada 2, em pulo 1, processa todos os que
recebe. **O pulo se aplica uma vez à cadeia inteira**, que é o comportamento certo.

**O risco de edição manual, porém, existe:** um `pulo_quadros=2` escrito à mão no ini da camada 2
multiplica — camada 1 em 3 e camada 2 em 2 dá 1 de cada 6. Não há aviso nenhum sobre isso hoje.

---

## 10. O que ainda não foi medido, e como medir

Em ordem de valor, com o custo de cada uma.

### 10.1 A referência de pulo 1 na configuração de hoje — **2 minutos, e é a que falta**

Todo o §6.3 compara pulo 3 contra um pulo 1 **calculado**. Sem essa referência, não sei se a
camada aguentaria os 90 fps que a aritmética assume.

**Como:** com o jogo na mesma cena e o mesmo `PassCount`, abrir o painel, clicar em "Todos",
esperar 30 s, ler três linhas `LAT 10s` e três de `regularidade` no `dlss5forall.log`. Comparar
`present` e o intervalo típico com as de pulo 3. Se `present` chegar perto de `copiado`, a conta
de §6.3 está validada; se ficar em ~30 mesmo com pulo 1, a GPU é o limite e **o pulo 3 estava de
graça** — o que mudaria a recomendação inteira.

### 10.2 Idade média medida, e não calculada — **meia hora, precisa de bancada**

O `L_pipeline + T/2` de §6.3 é aritmética. A medição direta é um alvo com carimbo de tempo visível
(um contador de quadros na tela do jogo) capturado por uma câmera rápida ou por um segundo
processo lendo a tela — o mesmo problema que `latencia-nr.md` §9.2 registrou como não resolvido
("o backbuffer é `FLIP_DISCARD`, não dá para ler de volta depois do `Present`").

Aproximação barata e honesta: instrumentar `srt` já dá a idade do quadro na entrada; falta o
outro extremo.

### 10.3 A curva de passes na cena do usuário — **10 minutos**

Os 4,2 ms/passe são de uma cena do Witcher 3. `ARQUITETURA.md` §10 é explícito: pontos medidos em
cenas diferentes não formam curva. As tabelas de §4.1 dependem disso.

**Como:** mesma cena, jogo **pausado** (o ciclo dia/noite já invalidou três medições nesta
árvore), variar `nr_passes` no `dlss5-feed.cfg` em 1, 2, 4, 6, 8 e ler o `frame interval` do
`dlss5-feed.log`. **Usar o fps, não o `ms/frame` do log** — o contador do feed mede CPU e não vê o
trabalho da GPU, erro que já produziu uma tabela errada nesta árvore.

### 10.4 O envelope de MV com pulo, em jogo real — **20 minutos**

§5.2 cruza duas tabelas medidas separadamente (deslocamento por giro, e sobrevivência de vetores)
sem nunca medir as duas juntas com pulo ligado.

**Como:** o `mvtarget` com deslocamento conhecido já existe (`docs/motion-vectors.md` §"Bancada").
Rodar com pulo 1, 2, 3 e 4 sobre o **mesmo** deslocamento por quadro e ler as amostras de MV do
`dlss5-feed.log`. A previsão a testar: o valor lido cai como se o deslocamento tivesse sido
multiplicado por N, e a partir de ~56 px efetivos a fração de vetores mantidos desaba.

### 10.5 O defeito latente do `mv_hardware=1` com pulo — **5 minutos**

§2.3. Rodar com `[captura] mv_hardware=1` e pulo 3, ler a sonda de MV. Se a mediana sair perto de
1/3 do esperado, está confirmado, e o conserto é mover `MvHw::Frame()` para dentro do ramo do
`capOk` (ou acumular os campos intermediários).

### 10.6 Cena escura com pulo — **15 minutos**

§5.3. Dump de quadro com pulo 1 e pulo 3 na mesma cena noturna, depois de um giro conhecido.

---

## 11. Duas correções a outros documentos

1. **`ARQUITETURA.md` §10 aponta para a seção errada.** Duas vezes ("ver §5 — o pulo é o que
   compra orçamento para passes altos", em "O caminho daqui" item 4; e "Lembrar que o **pulo de
   quadro dobra ou triplica o orçamento** (§5)"). A §5 do `ARQUITETURA.md` é *As duas gerações do
   add-on RenoDX* — não fala de pulo. A referência certa passa a ser **este documento**.

2. **`docs/latencia.md` §4 está desatualizado no ponto mais citado.** O "WGC preso em 50 Hz" foi
   **resolvido**: a causa é o `MinUpdateInterval` da `IGraphicsCaptureSession5`, documentada em
   `screen_capture.h:199`–`218`, e o app pede 0 antes do `StartCapture`. Medido: 49,93 → 99,83
   fps. O log de hoje mostra 97,9–99,3 fps de chegada. O que **continua** valendo daquela seção é
   o teto seguinte: a captura não passa do refresh do monitor.

---

## 12. Resumo para quem for explicar à pessoa que usa

- **O que é:** "o filtro processa 1 de cada N quadros do jogo".
- **O que ganha:** menos placa gasta pelo filtro — e essa placa vai para o jogo, **ou** para
  rodar uma rede mais pesada no quadro que sobrou. **Uma coisa ou outra, não as duas.**
- **O que perde:** a imagem atualiza N vezes menos. E, com N alto, cena rápida começa a arrastar,
  porque a rede reconstrói a partir de um quadro N vezes mais distante.
- **O que ele NÃO é:** não é limitador do jogo (isso é FPS DO JOGO, em Mais opções) e **não
  reduz input lag** — no modo sobreposto ele aumenta em ~8 a 25 ms o tempo em que a tela mostra
  uma imagem velha.
- **O ponto de partida sensato:** jogo travado em 60, pulo em "Todos". Suba o pulo quando quiser
  rede mais pesada, ou quando o jogo precisar da placa de volta.

---

## 13. O que NÃO fazer, e por quê

| não faça | por quê |
|---|---|
| **Vender o pulo como recurso de latência** | no modo sobreposto ele **piora** a latência de exibição em 8–25 ms (§6.3). O painel hoje acerta ao não citar milissegundo nenhum, e o comentário em `main.cpp:5305`–`5309` explica por que a linha de latência do teto antigo não foi herdada. Não a traga de volta |
| **Subir o pulo e os passes juntos e esperar que o jogo melhore** | são o mesmo orçamento gasto duas vezes (§4.2). O jogo não recebe nada de volta |
| **Usar limitador de jogo baixo + pulo alto** | dividem a mesma coisa duas vezes: 30 no jogo com pulo 4 dá 7,5 fps na camada (§9) |
| **Usar pulo 3–4 em jogo de câmera rápida** | a 90 °/s o deslocamento por quadro processado bate ou passa o alcance de ±96 px do estimador; além dele o campo devolve vetor **pequeno e errado**, que é pior que vetor zero (§5.2) |
| **Escrever `pulo_quadros` à mão no ini da camada 2** | multiplica com o da camada 1, sem aviso nenhum (§9) |
| **Passar a razão do pulo como "teto da película" para o `Desempenho::Sincronizar`** | limitaria o **jogo** a 2 fps, com um número plausível demais para alguém desconfiar. `main.cpp:9908` documenta a armadilha; o terceiro argumento é zero de propósito |
| **Contar com o pulo para "aproveitar" um jogo destravado a 300 fps** | a captura não passa do refresh do monitor. Os quadros acima dele nunca existiram para nós (§6.2) |
| **Medir o custo de passes pelo `ms/frame` do `dlss5-feed.log`** | esse contador mede **CPU**. O trabalho da rede é GPU e ele não o vê — erro que já produziu uma tabela errada nesta árvore (`ARQUITETURA.md` §10, correção de 03:35). Use o **fps** |
