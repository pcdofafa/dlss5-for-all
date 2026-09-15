# Atacar o borrão pelo mecanismo: quatro linhas, quatro respostas negativas, e um achado que vale mais

Data: 2026-09-06, 05:00 às 05:50. Máquina: RTX 4080, Windows 11 26200.
Monitor primário `\\.\DISPLAY2`, 1920×1080 a 60 Hz, escala 125%. Secundário a 150% e 100 Hz.
Binário usado: compilado 05:44 em `scratchpad\mecanismo\` — **o mais novo dos cinco em uso na
máquina naquela noite**, com ~3 h de mudanças a mais que o mais antigo. Todo número deste
relatório veio dele; comparação com número de outra frente pode refletir código, não medida.

Instrumentos e dados: `scratchpad\mecanismo\` (planos, tabelas `FINAL-*.csv`, logs por rodada).

---

## 1. Resposta curta

**O borrão da segunda camada não tem solução por configuração.** As três linhas nomeadas —
jitter, presets do modelo, máscara reativa — foram medidas e nenhuma produz ganho. Uma quarta,
que apareceu por acidente, é cosmética. O padrão em uso já está no melhor ponto que a
configuração alcança.

Isso é resposta, não fracasso: quatro caminhos fechados com número, três com o mecanismo
verificado no log em vez de inferido da tabela. Uma das perguntas estava aberta desde 03/09.

**E o número mais acionável da noite não é sobre borrão:** reduzir a resolução de trabalho
custa **43% do detalhe fino** e quase dobra o véu. Ver §6.

### O que o padrão de hoje entrega, com o piso descontado

| | véu | efeito | força de Neural Rendering |
|---|---|---|---|
| camada 1 sozinha | 0,37 | — | — |
| DLAA sozinho (`NRIntensity=0`) | 0,52 | 4,51 | 0 % |
| **padrão de hoje** | **0,62** | **5,38** | **11 %** |
| perfil antigo | 1,65 | 12,26 | 100 % |

Os "38%" anunciados antes vinham de dividir efeito por efeito sem descontar o que o DLAA
entrega sozinho. Descontando nos dois termos, sobram **11%**. O padrão reprova no corte de
metade e no de dois terços.

---

## 2. A causa física (leitura de código, confirmada por duas fontes independentes)

DLAA é um acumulador temporal. A qualidade vem de juntar amostras da mesma cena em posições
**subpixel diferentes** a cada quadro. Num jogo, essa diferença vem do jitter da câmera.

Na nossa cadeia não há jitter: `dlss5-feed.cpp:2669` cria a feature como *DLAA: render ==
target, unjittered*, e `:5377` passa deslocamento zero fora do modo experimental. Daí:

1. **Amostras correlacionadas.** A segunda passada recebe a saída da primeira, onde cada pixel
   já é média de vários quadros. Somar amostras correlacionadas não acrescenta resolução; só
   alonga a constante de tempo do filtro.
2. **Erro de reprojeção que compõe.** Os vetores vêm de fluxo óptico (Lumenite_Kernel, 1/8 de
   resolução, subido por bilinear), não do motor. A segunda passada estima fluxo sobre uma
   imagem já deslocada por um erro que desconhece e aplica o seu por cima.

Isso explica a assinatura medida e nada mais explica: o véu sobe, a nitidez cai, e o rastro
atrás de uma borda dura fica **inalterado** em todas as variantes.

**Consequência:** `NRIntensity` e `reset_every` funcionam porque ambos reduzem *quanto o
estágio contribui*. Por isso derrubam efeito e borrão juntos, sempre.

Esta conclusão foi alcançada por leitura de código sem conhecer os documentos anteriores, e
coincide com `docs\borrao-e-nitidez.md` §1 (03/09), cujo título é *"sem jitter, o DLAA não
reconstrói: ele só filtra"*. O crédito do achado original é daquele documento; o agente
`pesquisa-borrao-temporal` (06/09, 05:47–06:06) reabriu o caso mostrando que o mecanismo está
implementado no Feeder e nunca foi ligado. Nenhum dos dois é deste trabalho.

---

## 3. Presets do modelo — negativo, e o padrão atual sai defendido por medição

O pacote roda `preset=5` (E, CNN legado). Medidas com o perfil antigo (força cheia):

| preset | família | véu | efeito | detalhe ÷ camada 1 |
|---|---|---|---|---|
| **E (5), o atual** | CNN | 1,65 | 12,39 | **1,19** |
| F (6) | CNN | 1,63 | 17,11 | 0,85 |
| Default (0) | transformer | 1,34 | 19,75 | 0,89 |
| J (10) | transformer | 1,36 | 19,91 | 0,89 |
| K (11) | transformer | 1,54 | 20,07 | 0,90 |

**Duas conclusões erradas foram descartadas no caminho, e as duas por controles próprios.**

A primeira: "o ganho vem de recriar a feature com preset explícito". Refutada pelos logs — com
`preset=0` o Feeder **não envia hint nenhum** (`if (g_cfg.preset > 0)`), então o controle é o
caso de *menos* intervenção, e mesmo assim agrupa com os transformer.

A segunda, minha: "é CNN contra transformer". Refutada pelo preset F, que é CNN legado
(`[feed] DLSS render preset hint: 6 (F)`) e agrupa com os transformer no detalhe.

**A leitura que sobra é menos elegante e mais útil: o preset E é uma exceção entre cinco.**
Quatro de cinco derrubam o detalhe abaixo do que a camada única entrega; o que está no produto
é o único que acrescenta. **Não sabemos por quê**, e não vale inventar mecanismo — foi assim
que as duas conclusões acima nasceram e caíram.

Isso confirma por medição a escolha de 03/09 (`borrao-e-nitidez.md` §6.1), feita por raciocínio:
o transformer confia mais no histórico acumulado, virtude com vetores exatos e defeito sem eles.
**Prático:** a escolha está cercada de opções piores, então mexer ali é perigoso.

---

## 4. Máscara reativa — negativo, e desligar piora

A máscara já existe: `DLSS5_Feed.fx` produz `DLSS5_Mask` (R8), que marca onde o vetor não é
confiável para o DLSS se apoiar no quadro atual. As quatro chaves fora do padrão do shader no
pacote (`VALIDATE_LUMA=1`, `VALIDATE_DEPTH=0`, `STATIC_BIAS=0`, `MV_CONSISTENCY=1.1`) são
**deliberadas e documentadas** em `docs\motion-vectors.md`. `STATIC_BIAS=0` em particular é a
correção de um bug que fez o projeto passar um dia achando que o estimador estava quebrado.

Medida com a validação desligada (`MV_VALIDATE`, `VALIDATE_MV` e `VALIDATE_STATIC` em 0):

| | véu | detalhe ÷ camada 1 |
|---|---|---|
| perfil antigo | 1,65 | 1,18 |
| **validação desligada** | **1,83 (o maior da tabela)** | 1,18 |

O vetor ruim continua presente e continua deformando; desligar a validação apenas para de
detectá-lo. Magnitude (11%) no limite do ruído, n=1: **direção, não número fechado.**

Nota para quem for mexer: `MV_VALIDATE` e `VALIDATE_MV` são chaves **diferentes**, e a segunda
domina por 5 a 30×. Um teste chamado "sem validação" que desligue só a primeira mede muito
menos do que o nome sugere.

---

## 5. Jitter — ligado pela primeira vez, e refutado

O jitter exige `work_upscale=2` **com** `work_resolution<100` (só há grade para deslocar se a
de trabalho for menor). A primeira tentativa, na camada 2, **não testou nada**: a sobrescrita
de resolução era apagada (§7). Refeito na **camada 1**, onde o caminho funciona — e que é a
única camada que existe no uso normal.

Prova de que desta vez ligou:

```
[feed] work_upscale=2: DLSS Quality, 1508x884 -> 1732x1016, Halton(2,3) over 11 phases, jitter sign +1
[feed] feature ready: 1508x884 -> 1732x1016 DLSS Quality (synthetic jitter)
```

**Critério escrito ANTES de medir: o detalhe tem de subir.** É o único caminho da cadeia capaz
de fazer a rede reconstruir em vez de filtrar.

| | véu | detalhe |
|---|---|---|
| âncora, resolução plena | 0,375 | 7,40 |
| 87 % com jitter / sem | 0,823 / 0,634 | 3,66 / 4,24 |
| 77 % com jitter / sem | 0,899 / 0,652 | 4,31 / 4,01 |
| 67 % com jitter / sem | 0,881 / 0,718 | 3,37 / 4,15 |

**No detalhe o jitter não tem direção:** −14 %, +7 %, −19 % nos três pares. Um positivo, dois
negativos, todos dentro ou perto do ruído.
**No véu ele piora consistentemente:** +30 %, +38 %, +23 %, acima do ruído nos três.

**Refutado por piorar o que não devia piorar**, e não por deixar de melhorar o que prometia —
a distinção importa para quem pensar em revisitar a linha.

O par de controle é o que torna isso interpretável: cada resolução aparece duas vezes, com a
mesma redução, uma com jitter e outra sem. Foi exatamente o par que faltou na matriz anterior,
que testou redução com esticão bilinear e chamou aquilo de teste de jitter.

Controle de deriva das duas pontas: véu 0,375 → 0,390, detalhe 7,400 → 7,445. A onda mais
limpa da noite.

---

## 6. O achado que vale mais: a resolução de trabalho custa 43 % do detalhe

Das mesmas rodadas, comparando **sem jitter** contra a resolução plena:

| resolução de trabalho | detalhe | contra 100 % | véu | contra 100 % |
|---|---|---|---|---|
| 100 % (hoje, telas pequenas) | 7,40 | — | 0,375 | — |
| 87 % | 4,24 | **−43 %** | 0,634 | +69 % |
| 77 % | 4,01 | **−46 %** | 0,652 | +74 % |
| 67 % | 4,15 | **−44 %** | 0,718 | +91 % |

Três pontos independentes, todos entre −43 % e −46 %, com o controle de deriva validado na
mesma onda. **Reduzir a resolução de trabalho custa quase metade do detalhe fino e quase dobra
o véu.**

O programa oferece essa redução no painel como melhoria de desempenho e **escolhe o valor
sozinho** pela área da fonte (`main.cpp`: 100 até 2,4 MP, 77 até 4,0, 67 até 6,0, 58 acima).
Ninguém tinha medido o custo de imagem, porque as duas tentativas anteriores usaram a chave
quebrada e mediram o padrão duas vezes.

**A troca, com honestidade nos dois lados.** Num ultrawide sem reduzir, o app roda a 20–24 fps
e o gargalo é a rede, não o jogo (`borrao-e-nitidez.md`). Imagem fluida a 67 % pode valer mais
que imagem parada a 100 %. **A escolha continua defensável; o que faltava era o preço.** Quem
escolher reduzir merece saber que paga ~44 % do detalhe fino.

**Recomendação:** apresentar os dois números no painel, ou ao menos no README, em vez de
oferecer a redução como ganho sem custo.

---

## 7. O bug corrigido: a resolução da camada de cima era descartada em silêncio

`[camada2_feed] work_resolution` **nunca funcionou**. Log da primeira camada:

```
[05:25:17.560] dlss 2x: camada 2 com sobrescrita [camada2_feed] work_resolution=87
[05:25:17.703] [feed] config: ... work_resolution=100% work_upscale=2 ...
```

143 ms entre aplicar e ler o valor antigo. A causa: toda camada, inclusive as de cima, lê a
resolução do próprio ini e a regrava no cfg do Feeder (`main.cpp` ~8014). O ini da camada de
cima é cópia do da primeira, então ela **apaga a sobrescrita na própria abertura**. É o mesmo
defeito que a seção `[desempenho]` tinha, e a correção segue o mesmo molde (`!g_camada2`).

**Para quem usa:** a configuração era descartada em silêncio e o programa ainda registrava que
tinha aplicado.

**Retroativo:** a variante `work_resolution=75` da matriz de borrão anterior **não testou
nada** — mediu o padrão duas vezes. Isso explica com precisão por que aquelas duas medições
discordavam na direção e nenhuma passava do ruído: eram a mesma coisa. A conclusão registrada
de que "resolução reduzida piora" não tinha base. (O número real está na §6, e é bem pior do
que aquela conclusão sugeria — mas por medição, não por acaso.)

Correção aplicada em `main.cpp`, compilada sem avisos novos, **não commitada**. Inclui uma
linha de log nova para o caso da camada de cima, para o valor herdado não virar mistério na
próxima investigação.

---

## 8. Método — o que a bancada aprendeu, e vale mais que as tabelas

**Reprovação automática por duas provas independentes.** Uma rodada só entra na tabela com
prova de que a camada trabalhou (ativa, com evaluation, com delivered, as duas janelas cobrindo
a fonte) **e** prova de que a imagem estava viva (repetidos < 50 %, cadência > 0,5, tremor ≠ 0,
atraso < 300 ms). As duas são necessárias porque falham por caminhos diferentes.

**O modo de falha mais traiçoeiro:** imagem congelada produz véu artificialmente **baixo** —
véu conta pixels gerados por movimento, e sem movimento não há rastro. A rodada não parece
defeituosa: **parece a melhor linha da tabela.** Seis rodadas congelaram na noite; todas foram
pegas, e três delas produziram falsos achados antes de serem filtradas.

**Ressalva sobre um critério:** `tremor ≠ 0` só funciona porque a faixa de referência tem ruído
temporal residual. Numa fonte determinística vira falso positivo universal.

**Referência nas duas pontas, blocos intercalados, ponto de ruído no meio.** Comparar entre
ondas é proibido: a mesma configuração deu véu 1,65 e 1,39 em ondas diferentes. Três dos falsos
achados da noite nasceram exatamente disso.

**Par de controle na mesma condição.** O jitter só é interpretável porque cada resolução foi
medida duas vezes, com e sem. Sem o par, redução e jitter ficam confundidos.

**Critério de adoção escrito antes de medir.** "O detalhe tem de subir" foi escrito antes da
primeira rodada de jitter. Escrever depois é ajustar o alvo ao tiro.

**Nunca citar número que não passou pela análise com as reprovações** — nem em leitura parcial,
nem em espiada rápida. Todos os falsos achados da noite nasceram de ler tabela crua.

**Todo número viaja com a grandeza ao lado.** Este padrão apareceu **quatro vezes** em frentes
diferentes na mesma noite, e nenhuma delas teria sido pega por teste — só por leitura:

| número solto | a referência que ficou para trás | consequência |
|---|---|---|
| um limiar de dispersão | percentil 95 sobre média, ou 99 sobre mediana? | critério aplicado com a régua errada |
| "teto de 30 s" / "45 s" | aquecimento da camada, ou laço de apresentação? | duas frentes calibraram a espera **abaixo** dos 45 s e mediriam dentro do aquecimento *com a proteção ligada* |
| "linhas 7890 e 7905" | numeração da base do commit, ou do arquivo atual? | bloco atribuído ao autor errado |
| véu 1,65 → 0,71 | rodada válida, ou congelada? | dois falsos achados (§8, contaminação) |

O número sobrevive à viagem; a referência não. O leitor completa com a que tem na mão, e o
erro é silencioso porque o valor continua plausível. **Escreva a grandeza junto do número, ou
não escreva o número.**

**Desenhe o controle que derruba a sua própria conclusão.** Três das quatro linhas foram
fechadas por controles feitos para isso (preset Default, preset F, realce zerado, par sem
jitter). **Nenhuma foi fechada pela variante que se queria que ganhasse.**

**Contaminação:** três dos quatro congelamentos caem nos 6 min seguintes ao início de uma
colisão de placa confirmada (provável, não provado — não há registro retroativo de processos).
A hipótese de que compilar causava os congelamentos foi **testada e caiu**: compilação única às
04:48:49, congelamentos a partir de 05:03:59.

**Sobre a confiabilidade destes números.** Quatro erros de leitura foram encontrados e
reportados contra o próprio interesse de quem os cometeu, incluindo três inversões seguidas do
mesmo veredito e a recusa de um crédito de refutação já oferecido e publicado. Quem ler este
relatório precisa saber que os números passaram por esse filtro — e que a versão que ficou de
pé é a que sobreviveu a ele, não a primeira que pareceu boa.

---

## 9. Ressalvas

- **Atraso não é confiável nesta bancada.** Derivou de 32 para 105 ms ao longo de uma matriz.
  Véu e detalhe não usam carimbo de tempo e não são afetados.
- **A fonte é sintética.** Formas de alto contraste e uma foto estática medem custo e detalhe
  bem; não medem o benefício do DLAA em cena 3D com aliasing real, que é o caso para o qual a
  rede foi feita.
- **Binário mais novo que os das outras frentes** (topo do documento).
- O piso do DLAA (§1) tem n=1 na onda em que foi medido; a repetição planejada caiu por
  congelamento. A ordem de grandeza é firme, o segundo decimal não.
