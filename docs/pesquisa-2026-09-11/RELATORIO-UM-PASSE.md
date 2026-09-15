# "1 passe se comporta como zero passes" — o que foi medido, e o que era

**Data:** 2026-09-11, 16:05 às 17:00
**Cópia isolada:** `DLSS5ForAll-AMD\bin-teste-umpasse\` (cópia integral de `bin\` por `robocopy /E`, 315,9 MB)
**Produção não foi tocada.** Nenhum processo alheio foi encerrado — só instâncias cujo `Path`
apontava para `bin-teste-umpasse`. Nada foi promovido para `bin\`. O `VERSIONINFO` não mudou.

## A resposta curta

**O `DirectNeuralRenderingPassCount=1` está correto e a rede roda.** Um passe cria a feature
uma vez, avalia uma vez, dobra o custo de CPU do feed e muda 98% dos pixels do back buffer.

O "MAE 0,000 com o mesmo SHA-256" não veio do número de passes. Veio de **duas armadilhas da
bancada**, e eu reproduzi as duas. A primeira explica inclusive por que dez passes pareciam
funcionar e um passe não — o número de passes entra nela só pelo custo.

---

## 1. O que um passe faz, medido

Alvo: `cena-do-teste-perfil.png` (1600x900, imagem parada) numa janela `ZEBRA1P` no monitor
secundário, físico 3720,60. Um alvo próprio, compilado com título diferente, para não colidir
com a bancada `ZEBRA7` que outro agente usava na mesma máquina.

O app subiu pela cópia com `--source window --window-hwnd <hwnd> --overlay --outra-instancia
--no-hide-self --no-vsync --no-hdr`, `DLSS5_SEM_HOTKEY=1` e `DLSS5_DUMP` no quadro 400.
**Cada linha da tabela é uma sessão nova**, com `DirectNeuralRenderingHookPoint`,
`DirectNeuralRenderingPassCount` e `[ultima] nr_passes` gravados **antes** de o app abrir — é
a única forma que vale, porque o add-on lê o perfil quando carrega
(`renodx-nao-rele-a-config-viva`). DLSS Frame Skipping desligado.

Binário: o de produção, `C13671EE11316907BD872A847E4744B577F52F9AA967AD032A8AA33C9C1C8A34`.

| condição | MAE / 255 | pixels alterados | feed CPU | fps | `CreateFeature` | `EvaluateFeature` |
|---|---|---|---|---|---|---|
| rede desligada (referência) | — | — | 0,35–0,39 ms | 59,7–60,0 | **0** | **0** |
| *(piso: dois dumps iguais)* | *0,003* | *0,0%* | | | | |
| **1 passe** | **6,86** | **98,0%** | **0,67–0,80 ms** | 52–59 | **1** | **1** |
| 2 passes | 11,17 | 99,7% | 1,07–1,13 ms | 46–49 | 2 | 2 |
| 10 passes | 32,40 | 100,0% | 7,35 ms | 16,6 | 10 | 10 |

Três provas independentes, e as três dizem a mesma coisa:

1. **Pixels.** 6,86 contra um piso de 0,003 — 2.285 vezes o ruído da própria bancada. A
   distribuição da diferença com um passe: 2,0% dos pixels ficam parados, 20,8% mudam de 2 a
   4, 42,6% de 5 a 9, 28,4% de 10 a 24 e 6,2% mudam 25 ou mais.
2. **Custo.** O feed vai de 0,35–0,39 ms para 0,67–0,80 ms por quadro. Um passe **dobra** o
   custo. Não há como dobrar o custo sem rodar.
3. **O log do próprio add-on.** A contagem de `DLSS-NR direct: CreateFeature(Reserved18)
   succeeded` bate com o número pedido — 1, 2 e 10 — e com um passe sai
   `EvaluateFeature succeeded: evaluation=1 options_revision=1 result=0x00000001`, seguido de
   `source evaluation completed: source=1 application_frame=67 size=1596x891 replace_source=true
   return_output=false`. O resultado substitui a fonte, que é o caminho certo.

As strings do `renodx-dlss.addon64` fecham a semântica sem deixar margem:

```
Runs 1 to 10 sequential Neural Rendering evaluations for each source.
Additional passes consume the preceding Neural Rendering output directly;
source preparation and output composition run only once.
```

Um passe é uma avaliação. Não existe "passe zero" que o painel estaria chamando de 1.

### As quatro hipóteses do enunciado, uma a uma

| hipótese | veredito | o que a derruba |
|---|---|---|
| (a) o `PassCount` conta o DLAA do feed como passe 1 | **descartada** | `CreateFeature` = 1 com `PassCount=1`; a string do add-on diz "1 to 10 evaluations for each source" |
| (b) a rede roda mas o resultado não volta ao back buffer | **descartada** | `replace_source=true` no log, e o back buffer muda 98% dos pixels |
| (c) alguma chave do preset só vale do 2º passe | **descartada** | a escada 6,86 → 11,17 → 32,40 é contínua; não há degrau entre 1 e 2 |
| (d) o bridge/feeder pula algo com `passes=1` | **descartada** | com `nr_spike=0` o feeder nem avalia a rede; ele só repassa `nr_passes` ao cfg. O log dele confirma: `config (neural): nr_spike=0 nr_passes=1` |

**HookPoint=1 não foi testado**, de propósito: 1 é o `Auto`, e o `main.cpp` documenta que o
Auto trava o jogo nesta cadeia (`CorrigirHookPointNoArranque`). Como a hipótese (b) já caiu
por medida direta, não havia o que ganhar arriscando uma trava.

**Versão do add-on:** `renodx-dlss.addon64` de 07/09/2026 22:56, SHA-256 começando em
`85EAE478F1E733E85B247C32469C2B2C`. Não há string de versão legível no binário e o
`ReShade.log` não a imprime. Não procurei notas de release na web: a semântica do `PassCount`
já estava respondida pelo binário e pela medida, e uma nota de release não venceria nenhuma
das duas.

---

## 2. Armadilha 1 — o quadro pulado pelo DLSS Frame Skipping

**Esta é a que explica a assimetria entre um passe e dez.**

O `DLSS5_DUMP` grava o **back buffer do Present**. Com o DLSS Frame Skipping ligado, a cadeia
fica **suprimida** nos quadros pulados: ali o back buffer é a imagem crua da captura. Um dump
que caia nesse quadro mede a fonte, não o filtro.

Isso não é hipótese. Seis rodadas com um passe, quadros consecutivos, skipping em 1 de cada 2
(`alterna_quadros=2`, `alterna_modo=sintetizar` — que é o estado do ini da `bin\` de produção):

| quadro do dump | 400 | 401 | 402 | 403 | 404 | 405 |
|---|---|---|---|---|---|---|
| MAE / 255 | **0,18** | 6,69 | 6,56 | 6,66 | 6,75 | **0,18** |
| pixels alterados | **3,8%** | 98,1% | 97,9% | 98,0% | 98,0% | **3,9%** |

Duas das seis vieram **sem rede nenhuma**. Uma rodada só, caindo ali, devolve exatamente
"habilitar entrega a mesma imagem que desabilitado".

As mesmas seis rodadas com **dez** passes, mesma configuração:

| quadro do dump | 200 | 201 | 202 | 203 | 204 | 205 |
|---|---|---|---|---|---|---|
| MAE / 255 | 32,95 | 32,82 | 33,24 | 30,11 | 32,75 | 30,57 |

**Seis de seis com rede, nenhuma falha.** O motivo é o custo: com dez passes a cadeia cai para
16–18 fps e o app deixa de pular quadro (o log mostra `processados 17,4/s` contra `30,0/s` com
um passe). Ou seja, **o número de passes muda a chance de o dump cair num quadro pulado**, e é
daí que nasce a leitura "a rede só age a partir de 2 passes".

O `RELATORIO-AUDITORIA-BOTOES.md` já tinha registrado a metade disto no item (e): *"o
`DLSS5_DUMP` grava a saída da cadeia e não alcança o caminho do quadro sintetizado"*. O que
faltava era ligar essa frase ao número de passes.

---

## 3. Armadilha 2 — o "desabilitado" que religa sozinho

Esta dá MAE **0,000 com SHA-256 igual**, em qualquer número de passes, e é independente da
primeira.

Desabilitar o DLSS 5 pelo card do painel ou pelo F6 grava `[ultima] neural_off_nosso=1` junto
com `DirectNeuralRenderingHookPoint=0`. Na abertura seguinte, `CorrigirHookPointNoArranque()`
trata esse `Off` como **resto de um toque de F6** e devolve o HookPoint para 2 — de propósito,
e o código explica por quê (senão quem fechasse o app com o filtro desligado reabriria num
estado em que o F6 não consegue mais ligar).

Consequência para quem mede: **quem desabilita, fecha o app e reabre para dumpar, dumpa com a
rede ligada nas duas pontas.** Medido, com o ini dizendo `HookPoint=0` e a marca presente:

```
[16:40:02.412] neural: o Off era nosso (F6 da sessao passada) -> Upscaled de volta (gravado)
```

| o dump feito "com o DLSS 5 desabilitado" | MAE / 255 | pixels alterados |
|---|---|---|
| contra o dump de **1 passe** | **0,389** | 7,3% |
| contra a rede **de fato** desligada | 6,787 | 97,9% |

Ele é o dump de um passe. Não é defeito do código — é armadilha de bancada, e não estava
escrita em lugar nenhum.

**Uma terceira coisa apareceu no caminho e vale o registro:** gravar `HookPoint=0` no
`ReShade.ini` **deixou de bastar** para desligar a rede. O `main.cpp` em que eu compilei (com
o trabalho em curso do painel do motor da rede) reaplica `HookPoint=2` no arranque a partir de
`[ultima] motor`, e o `DisabledAddons` do ReShade também não segurou (medido:
`CreateFeature=1` com a chave posta). A única linha de base honesta que sobrou foi **tirar o
`renodx-dlss.addon64` da pasta**, e é assim que as tabelas da seção 5 foram feitas.

---

## 4. A correção

Mínima, restrita ao caminho de bancada, e **nada do produto muda**: fora de uma sessão com
`DLSS5_DUMP` armado, nenhuma linha nova é executada.

Duas partes, em `amd\main.cpp`:

**(1) Com o dump armado, a supressão da cadeia não acontece.** Nos quatro quadros anteriores
ao quadro pedido, o DLSS Frame Skipping para de suprimir — todo Present passa pela rede, e o
back buffer gravado é necessariamente o filtrado.

```cpp
const bool dumpPerto = dumpPath[0] && (presented + 4 >= dumpFrame);
if (alternaAtiva && Alterna::g_modo == Alterna::MODO_SINTETIZAR && Sintetiza::Ativo() && got) {
    const bool proximoProcessa = (quadrosDesdeAlterna + 1 >= C.alternaN);
    if (dumpPerto) dumpEsperou = 1;
    Sintetiza::Suprimir(!dumpPerto && !proximoProcessa && Sintetiza::TemBase());
}
```

**(2) A linha do log passa a dizer o estado do quadro gravado**, incluindo o HookPoint
**efetivo** lido da config viva — que é o que denuncia a armadilha 2:

```
DUMP: frame 415 -> '...\c2-sk414.png' ok | HookPoint efetivo 2 | passes 1 |
      frame skipping LIGADO | supressao suspensa para o dump
```

O `presented == dumpFrame` também virou `>=`: com o contador pulando um valor, o dump antigo
simplesmente não acontecia.

### O caminho que NÃO funcionou, registrado para ninguém repetir

A primeira versão esperava um quadro com `cadeiaRodouNeste` verdadeiro antes de gravar.
**Não serve.** Nas rodadas dos quadros 414 e 416 o app dizia `cadeia rodou` e a imagem saiu
sem rede nenhuma. É exatamente o aviso que o `RELATORIO-REQUIEM.md` já tinha escrito sobre o
contador `processados N/s`: `cadeiaRodouNeste` continua `true` com o estado líquido
desligado. O único sinal confiável é **não deixar a supressão acontecer**.

---

## 5. A prova, depois da correção

Binário corrigido: `6838242BF912EF7196EC8D4F4C9C9B7196407C727E371850D0038646B5B313D2`
(o de produção, para comparar, é `C13671EE…`). Linha de base com o `renodx-dlss.addon64`
fora da pasta.

**Oito rodadas com um passe e o DLSS Frame Skipping ligado**, nos mesmos quadros em que o
binário de produção falhava:

| quadro | 412 | 414 | 416 | 418 | 420 | 422 | 424 | 426 |
|---|---|---|---|---|---|---|---|---|
| MAE / 255 | 7,56 | 7,56 | 7,65 | 7,52 | 7,82 | 7,38 | 7,33 | 7,15 |
| pixels alterados | 98,7% | 98,8% | 98,7% | 98,5% | 98,7% | 98,4% | 98,5% | 98,0% |

**Oito de oito com a rede.** Antes, os quadros 414 e 416 saíam sem rede nenhuma.

**Os outros passes continuam funcionando** (mesmo binário, skipping desligado):

| | piso | 1 passe | 2 passes | 10 passes |
|---|---|---|---|---|
| MAE / 255 | 0,014 | 9,13 | 19,03 | 31,66 |
| pixels alterados | 0,0% | 98,3% | 99,9% | 100,0% |

**O custo não mudou**, que é o que prova que a correção não vazou para o produto:

| | binário de produção | binário corrigido |
|---|---|---|
| 1 passe, feed CPU | 0,67–0,80 ms | 0,66–0,67 ms |
| 1 passe, fps | 52–59 | 58,6–60,0 |
| 2 passes, feed CPU | 1,07–1,13 ms | 0,92–0,93 ms |
| 2 passes, fps | 46–49 | 59,4–60,0 |

As duas escadas de MAE (6,86/11,17/32,40 no binário de produção e 9,13/19,03/31,66 no
corrigido) **não são comparáveis entre si** e não deveriam ser lidas como mudança de efeito:
são binários diferentes, com o trabalho em curso de outro agente no meio, e a rede tem
componente temporal. Cada escada é internamente consistente, que é o que cada uma precisa
provar.

---

## 5-bis. O Modelo B (Style=1) com um passe NÃO é inerte

Chegou o achado de que só o `DirectNeuralRenderingStyle=1` — o Modelo B, que é o do perfil de
fábrica — ficaria inerte com um passe. **A minha bancada responde isso de graça, porque ela
rodou exatamente esse perfil.** O `ReShade.ini` da cópia, conferido depois das medidas:

```
[RENODX-DLSS-preset1]
DirectNeuralRenderingStyle=1          <- Modelo B
DirectNeuralRenderingIntensity=1
DirectNeuralRenderingPassCount=1
```

E o `[ultima]` do `dlss5forall.ini` da cópia: `nr_estilo=1`, `nr_intensidade=1`, `nr_passes=1`.

Ou seja, **toda a seção 1 deste relatório é Modelo B com um passe e intensidade 1**, e ela deu
MAE 6,86 com 98,0% dos pixels alterados, contra um piso de 0,003. O Style=1 age. O achado
contrário foi retirado pelo próprio autor: os dumps dele estavam com o DLSS Frame Skipping
ligado, que é a armadilha da seção 2.

Com isso, **não há nada a corrigir no mapeamento do estilo e não há motivo para trocar o
modelo padrão de fábrica.** A matriz Style 0/1/2 × Intensity 1,0/0,71 ficou sem objeto: o caso
que ela existia para explicar não existe.

Fica de pé um ponto de coerência, e é de empacotamento, não de rede — está na seção 6-bis.

---

## 6-bis. Recomendação de empacotamento

O `empacotar.ps1` já faz a parte difícil e faz certo: ele **gera** o `dlss5forall.ini` de
fábrica do zero (linhas 223 a 240), em vez de copiar o de quem empacotou. O ini que viaja tem
só `fonte`, `telacheia`, `vsync`, `hdr`, `sobrepor`, `guia_visto`, `escala`, `fps_tela`, mais
`[ofertas]`, `[hdr]` e `[atalhos]`. **Nenhuma chave de rede, nenhuma de quadros.** Como chave
ausente é padrão, o pacote já sai com o DLSS Frame Skipping desligado, um passe, fluidez 60,
motor RenoDX e sem `neural_off_nosso`. **Do lado do nosso ini, não há nada a mudar.**

O problema está no `ReShade.ini`, e é concreto: **o empacotador só conhece a geração ANTIGA do
RenoDX.** Ele higieniza `[RenoDX.DLSS5]` — `NeuralUplift`, `NRIntensity`, `NRStyle`,
`NRLocalStructure`, `NRSkinStructure` — e tem três `throw` exigindo essas chaves (linhas 199 a
207). A `bin\` que o usuário roda é a geração **nova**: `[RENODX-DLSS]` e
`[RENODX-DLSS-preset1]`, com `DirectNeuralRendering*`. **O empacotador não sabe que essas
chaves existem.**

Duas consequências, as duas medidas nos arquivos:

1. **Empacotando de `amd\`** (que é o `$Root` do script, `Split-Path -Parent` dele mesmo), o
   `ReShade.ini` que viaja é o antigo e **não tem `DirectNeuralRenderingHookPoint` nenhum**.
   Chave ausente, para o add-on novo, é `Auto` — e o `main.cpp` documenta que o Auto trava o
   jogo nesta cadeia. Salva pelo `CorrigirHookPointNoArranque`, que corrige para 2 na primeira
   abertura; mas o pacote sai errado e depende de um conserto em tempo de execução.
2. **Empacotando de `bin\`**, o `ReShade.ini` viaja com o resquício do overlay do usuário —
   hoje `DirectNeuralRenderingIntensity=0.71`, `PassCount=2`, `Style=1` — enquanto o
   `dlss5forall.ini` gerado não tem nenhuma dessas chaves. Na prática o script nem chega lá:
   os três `throw` de `NRIntensity=1` e companhia disparam, porque a `bin\` não tem as chaves
   antigas. **O empacotador, hoje, não roda contra a `bin\`.**

### O que acrescentar ao `empacotar.ps1`

Um bloco irmão do que já existe para as chaves `NR*`, no mesmo lugar (depois da linha 197) e
com a mesma guarda de recusa. Os valores, para bater com o `dlss5forall.ini` de fábrica:

| seção | chave | valor | por quê |
|---|---|---|---|
| `[RENODX-DLSS]` | `DirectNeuralRenderingHookPoint` | `2` | `Auto` trava esta cadeia; 2 é Upscaled, o que o app corrige sozinho hoje |
| `[RENODX-DLSS-preset1]` | `DirectNeuralRenderingPassCount` | `1` | igual ao `nr_passes` ausente do ini de fábrica |
| `[RENODX-DLSS-preset1]` | `DirectNeuralRenderingIntensity` | `1` | tira o `0.71` do overlay |
| `[RENODX-DLSS-preset1]` | `DirectNeuralRenderingStyle` | `0` | **ver a nota abaixo** |
| `[RENODX-DLSS-preset1]` | `DirectNeuralRenderingLocalToneStrength` | `1` | |
| `[RENODX-DLSS-preset1]` | `DirectNeuralRenderingGlobalToneStrength` | `1` | |
| `[RENODX-DLSS-preset1]` | `DirectNeuralRenderingLocalStructureStrength` | `1` | |
| `[RENODX-DLSS-preset1]` | `DirectNeuralRenderingSkinStructureStrength` | `1` | |
| `[RENODX-DLSS-preset1]` | `DirectNeuralRenderingAutoMask` | `0` | |

E os três `throw` precisam virar condicionais: **recusar se faltar a família da geração que
está indo no pacote**, e não exigir sempre as chaves antigas. Do jeito que está, o script é
uma trava contra empacotar a geração nova.

**A nota do Style, que é a única escolha de verdade nesta lista.** Com o `dlss5forall.ini` de
fábrica, `[ultima] nr_estilo` está **ausente**, e o painel nasce no degrau 0 — Modelo A. O
arranque aplica os passes (`AplicarPassesNr`) mas **não** aplica os outros controles do perfil,
então quem manda na rede é o que estiver no `ReShade.ini`. Se o pacote levar `Style=1` e o
painel mostrar Modelo A, **o painel mente na primeira abertura** — que é o mesmo defeito de
estado divergente que o `RELATORIO-REQUIEM.md` registrou com o `Intensity=0.71`. Por isso
`Style=0` na tabela: é o valor que casa com o painel de fábrica, e é também o que o
`empacotar.ps1` já grava na geração antiga (`NRStyle=0`).

A alternativa, mais cara e mais robusta, é o arranque passar a aplicar o perfil inteiro a
partir do `[ultima]`, como já faz com os passes — aí o `ReShade.ini` do pacote deixa de
importar e a classe inteira de divergência morre. **Não implementei nenhuma das duas:** é
mudança de empacotamento e de arranque, fora do que me foi pedido, e a segunda mexe no mesmo
`main.cpp` que outro agente está editando.

O `montar-portable.ps1` não precisa de nada: ele remove a seção `[RenoDX.DLSS5]` do ramo AMD
de propósito (o RenoDX não entra na pasta ali) e preserva o resto.

---

## 6. O que fica aberto, e é decisão de quem pediu

**O produto fica como está** — decisão do líder depois de confirmar as duas armadilhas com o
autor da medição original. A queixa do usuário era *"um passe não faz nada"*, e a causa era o
DLSS Frame Skipping ligado, não a intensidade nem o modelo. Nada de rede foi mudado aqui.

**Um passe age, mas age pouco:** 3,5 a 4,7 vezes menos que dez passes em MAE, embora mexa em
98% dos pixels. Se algum dia a queixa voltar como *"muda pouco para o que eu esperava"*, aí sim
é escolha de produto — padrão de fábrica de passes ou intensidade — e não de correção.

**Já não há pergunta de bancada aberta.** O autor da medição original confirmou os dois
caminhos: a cópia dele veio da `bin\` com `alterna_quadros=2` e `alterna_modo=sintetizar`, e o
baseline `HookPoint 2 vs 0` dele foi gravando o ini e reabrindo o app, que é exatamente a
armadilha 2. Os números dele fecham com os meus: mesma configuração, mudando só o skipping, ele
mediu MAE 2,674 com ele ligado contra 11,496 desligado, e observou que com dez passes o mesmo
quadro 300 ainda dava 95,69% dos pixels — a mesma assimetria da seção 2, medida por outra mão.

**O que este relatório não mediu:**

- **Ninguém jogou.** Alvo sintético, cena parada, monitor secundário. Nitidez de cena parada
  não diz nada sobre arrasto ou cintilação em movimento.
- **A GPU esteve compartilhada** o tempo todo: outro agente rodava a própria bancada na mesma
  máquina. As tabelas de pixels são imunes a isso; as de fps são relativas, feitas em
  sequência, e uma rodada de custo com skipping saiu com 116 stalls e foi descartada.
- **O que chega à TELA não foi medido com sucesso.** Tentei por captura do desktop e o método
  não vale: a camada só é desenhada quando a janela alvo tem foco (`overlay fora de vista
  (alvo fora de foco / minimizado / bypass)`), e sem foco a captura pega o alvo por baixo. Das
  três séries que fiz, uma inteira fotografou o alvo cru achando que fotografava a camada.
  Fica registrado como método que **não** funciona sem garantir o foco.
- **Uma amostra por ponto** nas tabelas de pixels; as de custo têm duas janelas de 600 quadros.
- **A bancada foi encerrada por ordem do líder** no meio da sessão, depois de a tela do usuário
  travar com três apps de teste na mesma GPU. Toda a medição desta página é anterior a isso e
  está completa; a matriz Style × Intensity que chegou a ser pedida não foi rodada, e a seção
  5-bis explica por que ela perdeu o objeto antes de precisar da GPU.

---

## 7. Arquivos

| arquivo | o que mudou |
|---|---|
| `amd\main.cpp` | a supressão da cadeia para perto do dump; a linha do `DUMP` diz HookPoint efetivo, passes e o estado do frame skipping; `presented >= dumpFrame` |
| `amd\DLSS5ForAll-umpasse.exe` | o binário de prova, `6838242B…` — **não** foi promovido |
| `DLSS5ForAll-AMD\bin-teste-umpasse\` | a cópia isolada, com o binário corrigido instalado e o de produção guardado em `DLSS5ForAll.exe.bak-antes-correcao` |
| `amd\empacotar.ps1` | **não foi alterado** — a recomendação da seção 6-bis está por escrito, para quem for mexer no empacotamento |

**Aviso sobre o binário de prova:** o `main.cpp` estava sendo editado por outro agente durante
esta sessão (as linhas se deslocaram entre duas leituras do mesmo arquivo). O
`DLSS5ForAll-umpasse.exe` contém o trabalho em curso dele junto com esta correção. Para
promover, recompilar quando a árvore estiver parada.
