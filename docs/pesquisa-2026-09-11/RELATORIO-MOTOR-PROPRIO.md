# Motor da rede "Próprio" como opção — o lado do feeder

Data: 2026-09-11. O painel vai ganhar um interruptor **"Motor da rede: RenoDX (padrão) /
Próprio (experimental)"**. Este relatório entrega o lado do feeder: o contrato da troca, os
controles do painel valendo no motor próprio, o que acontece quando algo falha, e a
comparação entre os dois motores. A interface fica com o `painel-requiem`; **o `main.cpp`
não foi tocado**.

---

## 1. O contrato da troca

Os dois add-ons ficam na pasta **o tempo todo**. Nada é renomeado, nada é removido: a troca
é só configuração.

| | Motor **RenoDX** (padrão) | Motor **Próprio** (experimental) |
|---|---|---|
| `ReShade.ini` `[RENODX-DLSS]` `DirectNeuralRenderingHookPoint` | **2** (Upscaled) | **0** (Off) |
| `dlss5-feed.cfg` `nr_spike` | **0** | **1** |
| quem avalia a rede | `renodx-dlss.addon64` | `dlss5-feed.addon64` |

**Provado nos dois sentidos, com os dois add-ons presentes:**

```
##### MOTOR PROPRIO  (HookPoint=0, nr_spike=1)
  feeder  : janelas de 600 quadros com rede = 4
            nr: ready at 1596x891 B8G8R8A8_UNORM, 1 pass per frame
  RenoDX  : CreateFeature = 0   EvaluateFeature = 0
  >>> SO O FEEDER

##### MOTOR RENODX  (HookPoint=2, nr_spike=0)
  feeder  : janelas de 600 quadros com rede = 0
  RenoDX  : CreateFeature = 1   EvaluateFeature = 1
  >>> SO O RENODX
```

### 1.1 Por que a troca exige reabrir o app

Não é escolha de conveniência. **O add-on do RenoDX lê o `HookPoint` uma única vez, quando
carrega** (ARQUITETURA §5, medido: mudar a config viva não produz reação nenhuma). Escrever
a chave só vale antes de os add-ons subirem — que é exatamente o que
`CorrigirHookPointNoArranque()` já faz no `main.cpp`. Logo: gravar as chaves, reabrir.

O `nr_spike`, do nosso lado, valeria ao vivo; mas de nada adianta ligar um motor se o outro
só desliga na próxima abertura.

### 1.2 OS DOIS LIGADOS AO MESMO TEMPO DERRUBAM O QUADRO — e agora o feeder recusa

Descoberto doendo, no meio desta sessão: uma bateria rodou com o `HookPoint` em 2 de um
teste anterior **e** `nr_spike=1`. Resultado, um quadro depois do nosso create:

```
[feed] nr: ready at 1596x891 ... 1 pass per frame
[feed] evaluate raised exception 0xC0000005 (caught; nothing was submitted)
[feed] safe passthrough: the chain keeps running WITHOUT DLSS/neural
stopped: the DLSS evaluate crashed ...
```

É o mesmo crash que o `ARQUITETURA.md` §10 registra como "o crash de 02:29", e o mecanismo
está lá: o add-on monta a sessão de NR dele **preguiçosamente, dentro do evaluate do
SuperSampling**, com os mesmos identificadores (AppId, data path, módulo) que acabamos de
usar, e encontra o terreno mexido.

A passagem segura pegou o crash e a imagem continuou — **mas sem filtro nenhum**, que é o
pior dos dois mundos: a pessoa perde o DLSS *e* o neural por ter ligado neural demais.

**Então a checagem passou a acontecer antes.** O feeder lê a chave de config do outro
add-on e, se ela estiver diferente de zero, simplesmente não sobe o motor próprio:

```
[feed] nr: NOT starting the built-in neural engine -- renodx-dlss's own is enabled
       (DirectNeuralRenderingHookPoint=2). Running both crashes the SuperSampling evaluate
       (0xC0000005). Set DirectNeuralRenderingHookPoint=0 in ReShade.ini BEFORE the add-ons
       load, or set nr_spike=0 to use theirs.
```

Testado de propósito depois da correção: **nenhum 0xC0000005, nenhum passthrough**, e a
cadeia seguiu filtrando com o motor do add-on. A chave de config é o sinal certo porque ela
só existe quando o add-on dele está na pasta, e `0` é o único valor que significa desligado
(1..4 são quatro formas de ligar).

**Consequência para o painel:** o interruptor pode gravar as duas chaves sem medo. Se
alguém editar os arquivos à mão e pedir os dois, o feeder decide — e decide pelo que já
estava funcionando, não pelo novo.

---

## 2. Os controles do painel no motor próprio

Os seis controles de imagem e os passes **não chegavam** ao motor próprio: `NrSetStaticParams`
gravava valores fixos (1,0 em tudo, estilo 0). Trocar de motor deixaria os seis inertes —
que é exatamente o defeito que custou um dia inteiro do outro lado, em 2026-09-08.

Agora cada um tem chave no `dlss5-feed.cfg` e vai ao mapa NGX:

| controle do painel | `[ultima]` no `dlss5forall.ini` | `dlss5-feed.cfg` | parâmetro NGX |
|---|---|---|---|
| PASSES (1..10) | `nr_passes` | `nr_passes` | *(o laço da cascata)* |
| MODELO (A/B/C) | `nr_estilo` | `nr_estilo` | `DLSSNR.Style` |
| INTENSIDADE | `nr_intensidade` | `nr_intensidade` | `DLSSNR.Intensity` |
| ESTRUTURA | `nr_estrutura_local` | `nr_estrutura_local` | `DLSSNR.LocalStructureStrength` |
| PELE E ROSTOS | `nr_estrutura_pele` | `nr_estrutura_pele` | `DLSSNR.SkinStructureStrength` |
| TOM LOCAL | `nr_tom_local` | `nr_tom_local` | `DLSSNR.LocalToneStrength` |
| TOM GLOBAL | `nr_tom_global` | `nr_tom_global` | `DLSSNR.GlobalToneStrength` |
| *(máscara automática)* | `nr_mascara_auto` | `nr_mascara_auto` | `DLSSNR.UseAutoMask` |

Os valores vão **sem conversão**, como o add-on de terceiros também faz ("manually entered
values are forwarded unchanged"). Os nomes das chaves do cfg foram escolhidos **idênticos**
aos do `[ultima]` de propósito: o painel copia valor por valor, sem tabela de tradução —
e tabela de tradução foi a origem do defeito de 08/09.

**Uma diferença a favor do motor próprio, e ela é estrutural:** o perfil é escrito no mapa
NGX **a cada quadro**, não na montagem. Um clique no painel vale no quadro seguinte, sem
recriar a feature e sem reabrir o app — o que o add-on de terceiros não consegue fazer, já
que lê o dele uma vez no carregamento (ARQUITETURA §5 e a memória
`renodx-nao-rele-a-config-viva`). É a única coisa deste relatório que o motor novo faz e o
antigo **não pode** fazer. O log só imprime quando algo muda:

```
[feed] nr: profile -> style=0 intensity=1.00 localTone=1.00 globalTone=1.00
       localStructure=1.00 skinStructure=1.00 autoMask=0
```

### 2.1 Cada controle muda a imagem?

Duas provas independentes, e as duas são necessárias:

1. **O valor chega ao mapa NGX** — a linha `nr: profile ->` do log bate, em cada rodada,
   exatamente com o que o cfg pediu. Isso prova o caminho painel → cfg → feeder → NGX.
2. **A imagem muda** — e aqui entra a medida que a memória do projeto exige: a rede tem
   componente temporal, então dois dumps da **mesma** configuração já diferem. Por isso
   `ruido-A` e `ruido-B` são a mesma configuração, dumpada duas vezes: elas são o **piso**.
   Um controle só "age" quando sai desse piso.

Alvo sintético parado, 1596×891, todos contra `ruido-A`:

| o que mudou | MAE / 255 | pior pixel | contra o piso |
|---|---|---|---|
| **`ruido-B` (a mesma coisa — o piso)** | **0,284** | 8 | — |
| TOM GLOBAL 1 → 0 | 0,282 | 5 | **no piso: não agiu** |
| PELE E ROSTOS 1 → 0 | 0,287 | 4 | **no piso: não agiu** |
| TOM LOCAL 1 → 0 | 2,149 | 14 | 7,6× |
| ESTRUTURA 1 → 0 | 2,489 | 26 | 8,8× |
| INTENSIDADE 1 → 0 | 3,429 | 25 | 12× |
| MODELO A → C (`Style` 0 → 2) | 3,608 | 31 | 12,7× |
| **PASSES 1 → 10** | **28,784** | **208** | **101×** |

**Quatro dos seis controles, mais os passes, agem.** Os dois que ficaram no piso são
justamente os dois cujo efeito depende do **conteúdo** — um alvo de ruído e grade não tem
pele nenhuma, e não tem faixa dinâmica para um tom global mexer. Isso não é prova de que
estão inertes; é prova de que este alvo não os exercita. A §2.3 refaz os dois numa cena de
jogo.

### 2.2 Custo por passes

Alvo destravado, 1596×891, duas amostras por ponto:

| passes | fps | ms por quadro | custo do passe extra |
|---|---|---|---|
| 1 | 100,0 · 100,0 | 10,0 | *(teto da captura — o WGC não passa de ~100 aqui)* |
| 4 | 62,7 · 62,5 | 15,98 | — |
| 10 | 26,9 · 26,8 | 37,17 | **~3,5 ms de GPU por passe**, entre 4 e 10 |

A repetibilidade é o que dá confiança aqui: 62,7/62,5 e 26,9/26,8 em rodadas separadas, com
o app reaberto entre elas. O ponto de 1 passe **não** entra na conta do custo por passe —
ele está no teto da captura, não no da GPU.

### 2.3 TOM GLOBAL e PELE E ROSTOS: não mediram efeito em nenhum alvo

Refiz os dois no segundo alvo (`cena-do-teste-perfil.png`: texto denso, linhas finas,
retângulos e uma **escada de cinza** — que é faixa dinâmica de verdade), e desta vez com os
**extremos**, não só o zero:

| o que mudou | MAE / 255 | contra o piso |
|---|---|---|
| **`r-ruido-B` (a mesma coisa — o piso)** | **1,108** | — |
| PELE E ROSTOS 1 → 0 | 1,119 | no piso |
| PELE E ROSTOS 1 → **2,5** | 1,109 | no piso |
| TOM GLOBAL 1 → 0 | 1,119 | no piso |
| TOM GLOBAL 1 → **2,5** | 1,110 | no piso |
| máscara automática 0 → 1 | 1,160 | 1,05× — quase o piso |

**Nem no extremo.** Um controle que vai de 0 a 2,5 sem mover a imagem mais do que ela se
move sozinha não está agindo.

**O que isso permite afirmar, e o que não permite.** Permite: nesta cadeia, nestes dois
alvos, com valores de 0 a 2,5, `DLSSNR.GlobalToneStrength` e `DLSSNR.SkinStructureStrength`
não alteram a imagem de forma mensurável, e `DLSSNR.UseAutoMask` quase não. **Não permite**
dizer que são inertes em qualquer caso — e há uma razão concreta para a ressalva, que é a
mesma correção da §2.1 levada às últimas consequências:

> **Nesta árvore não existe uma captura de jogo real para medir.** Os dois alvos são
> sintéticos; nenhum tem rosto humano. "Pele e rostos" é, pelo nome, um controle que só age
> onde a rede reconhece pele — e nenhuma das duas cenas lhe dá isso. Para o TOM GLOBAL a
> suspeita é outra: a cadeia roda em `B8G8R8A8_UNORM`, SDR, e um controle de tom global
> pode depender de faixa que o SDR não tem.

**Consequência prática, e ela vale para os dois motores** (o add-on de terceiros escreve nos
mesmos dois campos NGX): são dois controles do painel que provavelmente não fazem nada para
quem está usando o programa hoje. Isso merece uma medição em jogo antes de qualquer decisão
sobre a interface — e é medição que só o usuário pode fazer, jogando.

---

## 3. A resolução do DLSS (`work_resolution`) com o motor próprio

Esta é a pergunta que a memória do projeto manda fazer: em 07/09/2026, mudar
`work_resolution` ao vivo deixou o NGX em **hold permanente**, com `evaluate 0.00 ms` e
nenhuma linha de erro. O motor próprio acrescenta uma segunda feature NGX na mesma sessão,
então a dúvida volta a valer inteira.

**Testado ao vivo: 100% → 77% → 58% → 100%, com o app rodando.** Resultado, em cada troca:

```
config: ... work_resolution=77% ...
holding the feature (re)build (1/59 frames; ...)
holding the feature (re)build (59/59 frames; ...)     ← a graça TERMINA
building: 1228x686 work resolution (77%) -> 1596x891 backbuffer
feature ready: 1228x686 DLAA
nr: ready at 1228x686 B8G8R8A8_UNORM, 1 pass per frame
nr: 600 frames: 0.25 ms/frame ... network at 1228x686   ← a rede VOLTOU A AVALIAR
```

| degrau | feature do SR | rede | voltou a avaliar? |
|---|---|---|---|
| 100% | 1596×891 | 1596×891 | sim |
| 77% | 1228×686 | 1228×686 | sim |
| 58% | 924×516 | 924×516 | sim |
| 100% (volta) | 1596×891 | 1596×891 | sim |

**Oito linhas de `holding` na sessão inteira** — duas por troca, o par `1/59` e `59/59` que
marca o começo e o fim da graça. Nenhuma repetição sem fim, que era a assinatura do defeito
de 07/09.

E de brinde: **a rede acompanha a resolução sozinha**. `NrEnsure` é chamado no fim de cada
`CreateDlssFeature` com o novo tamanho de saída, então o Neural Rendering é remontado junto
(~270 ms) e passa a rodar no tamanho novo, sem nenhuma chave a mais.

---

## 4. Robustez: o que a pessoa vê quando falha

A passagem segura já estava medida no `RELATORIO-RESIDUAL-NR.md` (sem `nvngx_dlssnr.dll`, o
NR não sobe e o DLSS continua; sem `nvngx_dlss.dll`, a sessão NGX não abre e a imagem
continua). O que faltava era **o app ficar sabendo**.

O `Warn()` do feeder chega ao `dlss5-feed.log` e ao log/overlay do ReShade, **e não mais
longe que isso**: o `DLSS5ForAll.exe` não o vê. Sem um canal, a passagem segura funciona e
ninguém avisa — a pessoa vê a imagem sem filtro e conclui que o programa quebrou.

Entrou um arquivo de estado, `dlss5-feed.status`, ao lado do log. Formato deliberadamente
burro (chave=valor, uma por linha, ASCII) para o lado do app ser dez linhas de leitura e
nunca um parser:

```
estado=ok|passthrough      ok = a cadeia está filtrando
motivo=<texto>             o que o cartaz deve dizer
rede=<LxA>|nenhuma         onde o Neural Rendering próprio está rodando
passes=<n>
motor=proprio|terceiros
```

É reescrito quando o estado muda — algumas vezes por sessão, nunca no caminho quente. Os
pontos que o atualizam: `FeedDisable` (passthrough), a rede ficar pronta, a rede cair no
meio da sessão, o modelo não ser encontrado, o create falhar, e a recusa de dois motores.

**Exemplo real, capturado na recusa de dois motores:**

```
estado=ok
motivo=the other neural engine is on (DirectNeuralRenderingHookPoint=2); this one stays off
rede=nenhuma
passes=1
motor=proprio
```

Repare que `estado=ok`: a cadeia **está** filtrando (com o motor do add-on), e o `motivo`
explica por que o motor pedido não subiu. São duas informações diferentes e o cartaz precisa
das duas — um cartaz de "parou" ali seria mentira.

---

## 5. Motor próprio contra motor RenoDX

Mesma cena, mesma máquina, alternando só as duas chaves da §1. Alvo sintético parado,
1596×891, dump no quadro 600 (folgado: o add-on monta a sessão dele preguiçosamente).

**Cada rodada carrega a prova de que rodou o que foi pedido:** para o RenoDX, a contagem de
`DLSS-NR direct: EvaluateFeature` no `ReShade.log` (1 para um passe, 4 para quatro) e o
`source evaluation completed ... replace_source=true`; para o próprio, o `nr: ready at` e as
janelas de 600 quadros no `dlss5-feed.log`.

| motor | passes | laplaciano | MAE vs sem rede |
|---|---|---|---|
| *(sem rede — referência)* | — | **836,2** | — |
| **Próprio** | 1 | **680,2** | 3,66 |
| **Próprio** | 4 | **834,1** | 19,84 |
| RenoDX | 1 | 509,6 | 9,42 |
| RenoDX | 4 | **168,4** | 30,47 |

**Os dois motores não fazem a mesma coisa, e a diferença é grande.** Com um passe, o
próprio borra bem menos (680 contra 510, sobre uma referência de 836) e mexe menos na
imagem (3,66 contra 9,42). Com quatro passes a distância vira um abismo: o RenoDX derruba o
laplaciano para **168 — um quinto da referência**, enquanto o próprio fica em 834,
praticamente a nitidez da imagem original, ainda alterando bastante (19,84).

O `proprio-4p` é a linha que melhor resume: **altera muito a imagem sem borrá-la**, que é
exatamente o que se quer de um passe neural. Não afirmo *por que* os dois divergem tanto —
o `Reserved18` não tem contrato público, o add-on se enxerta noutro ponto da cadeia
(`HookPoint=2`, sobre o backbuffer já apresentado) e o nosso roda dentro da command list do
feed, sobre o `SLOT_OUTPUT`. São caminhos diferentes e o resultado mostra isso; a explicação
exigiria desmontar o binário dele de novo.

**Ressalva que limita tudo acima:** alvo sintético, cena parada, uma amostra por ponto. A
nitidez de cena parada não diz nada sobre arrasto ou cintilação em movimento, e esta árvore
não tem uma captura de jogo real para medir (ver §2.3).

### 5.0 Custo de GPU: praticamente empate

Alvo destravado, mesma cena. Cada linha do RenoDX traz a contagem de avaliações do
`ReShade.log`, que é a prova de que os passes pedidos foram os que rodaram:

| passes | RenoDX | Próprio | diferença |
|---|---|---|---|
| 1 | 100,0 *(eval=1)* | 100,0 | empate — teto da captura |
| 4 | 61,5 *(eval=4)* | 62,5 | +1,6% para o próprio |
| 10 | 24,1 *(eval=10)* | 26,8 | +11% para o próprio |
| 1 + "rede leve" | — | **101,7** | *(sem custo mensurável)* |

**O motor próprio não é mais barato: é igual.** A vantagem de 11% a dez passes é real mas
pequena, e a dez passes ninguém vai rodar. Vale dizer isto com clareza porque a ARQUITETURA
§10 chegou a registrar "onze vezes mais barato por passe" — número que ela mesma retirou
depois, ao descobrir que comparava um contador de CPU com um de GPU. **Esta tabela é fps
contra fps, e ela diz empate.**

Logo, o argumento para o motor próprio **não é desempenho**. É: imagem menos borrada nos
passes altos (§5), o perfil valendo ao vivo sem reabrir (§2), a "rede leve" (que só existe
lá), passagem segura com motivo legível (§4), e não depender de um binário de terceiros que
muda de interface sem aviso.

### 5.1 Uma armadilha que quase virou conclusão

A primeira rodada desta tabela mediu `renodx-1p` e `renodx-4p` **idênticos** (MAE 9,48 e
9,43). A leitura fácil seria "o PassCount do add-on não faz nada". A leitura certa estava no
`ReShade.ini` depois da rodada: `DirectNeuralRenderingPassCount=1`, embora eu tivesse
escrito 4 antes de abrir o app.

**Quem reescreve é o próprio app.** Ele grava `DirectNeuralRenderingPassCount` no arranque a
partir de `[ultima] nr_passes` do `dlss5forall.ini` (`AplicarPassesNr`/`RsGravarNr`).
Escrever direto no `ReShade.ini` não adianta — o valor volta.

Isso é informação para o painel, não só para o teste: **o app já é o dono dessa chave**.
Para o motor próprio, basta ele passar a gravar o mesmo `nr_passes` também no
`dlss5-feed.cfg`, que é o que a §6 pede.

---

## 6. A lista exata de chaves, para o `painel-requiem`

### Ao escolher "Próprio" (e então reabrir o app)

| arquivo | seção | chave | valor |
|---|---|---|---|
| `ReShade.ini` | `[RENODX-DLSS]` | `DirectNeuralRenderingHookPoint` | `0` |
| `dlss5-feed.cfg` | — | `nr_spike` | `1` |
| `dlss5-feed.cfg` | — | `nr_passes` | o mesmo de `[ultima] nr_passes` |
| `dlss5-feed.cfg` | — | `nr_estilo` | o mesmo de `[ultima] nr_estilo` |
| `dlss5-feed.cfg` | — | `nr_intensidade` | o mesmo de `[ultima] nr_intensidade` |
| `dlss5-feed.cfg` | — | `nr_tom_local` | idem |
| `dlss5-feed.cfg` | — | `nr_tom_global` | idem |
| `dlss5-feed.cfg` | — | `nr_estrutura_local` | idem |
| `dlss5-feed.cfg` | — | `nr_estrutura_pele` | idem |
| `dlss5-feed.cfg` | — | `nr_mascara_auto` | idem |

Opcionais, e só se o painel expuser os controles (ver `RELATORIO-RESIDUAL-NR.md` e
`RELATORIO-RESOLUCAO-200.md`):

| chave | valores | padrão | observação |
|---|---|---|---|
| `nr_reduzida` | 0/1 | 0 | 1 = "Rede leve" |
| `nr_escala` | 50..200 | 65 | **>100 não recomendado** — ver `RELATORIO-RESOLUCAO-200.md` |
| `nr_residual` | 0..200 | 100 | não expor |

### Ao escolher "RenoDX" (e então reabrir o app)

| arquivo | seção | chave | valor |
|---|---|---|---|
| `ReShade.ini` | `[RENODX-DLSS]` | `DirectNeuralRenderingHookPoint` | `2` |
| `dlss5-feed.cfg` | — | `nr_spike` | `0` |

O perfil continua indo para `[RENODX-DLSS-preset1]` como hoje. **Sugestão:** gravar o perfil
nos *dois* destinos sempre, independentemente do motor — é mais barato que manter estado
sobre qual está ativo, e o destino errado é simplesmente ignorado por quem não lê.

### Três detalhes que valem mais que o resto desta lista

1. **Escrever o `ReShade.ini` só vale antes de os add-ons carregarem.** Com o app aberto, o
   AutoSave do ReShade desfaz a escrita minutos depois (ARQUITETURA §3). O caminho é gravar
   e reabrir — o que o interruptor já vai fazer.
2. **O `dlss5-feed.cfg` é relido a cada 60 quadros**, então tudo que é do feeder vale ao
   vivo. A reabertura é exigência do *outro* lado, não do nosso.
3. **`GravarChaveCfg()` (o `main.cpp` já tem) preserva o resto do arquivo.** Não reescrever
   o cfg inteiro: chaves de diagnóstico postas à mão somem, e o feeder tem várias.

---

## 6.1 Recomendação

**O interruptor pode entrar, com "RenoDX" como padrão** — que é o que o plano já previa. O
motor próprio está pronto o suficiente para ser uma opção honesta:

- os controles do painel valem nele (§2), e valem **ao vivo**, o que o outro não faz;
- a resolução do DLSS continua funcionando, inclusive ao vivo (§3);
- a falha tem passagem segura com motivo legível pelo app (§4);
- a imagem é menos borrada nos passes altos (§5);
- os dois motores juntos não se atrapalham mais, porque o feeder recusa (§1.2).

**O que ele não é: mais rápido.** Empate em fps (§5.0). Se o rótulo do painel sugerir
desempenho, vai prometer o que não entrega.

**Uma frase de ajuda honesta:** *"outra implementação da mesma rede, feita por nós — borra
menos com muitos passes e responde na hora aos ajustes; o custo é o mesmo"*.

---

## 7. O que este relatório não mediu

- **Ninguém jogou.** Todas as medidas são de alvos sintéticos parados, no monitor
  secundário. Nitidez de cena parada não diz nada sobre arrasto, fantasma ou cintilação em
  movimento — e a rede tem componente temporal.
- **Não há captura de jogo real nesta árvore** (§2.3). Os dois "alvos" são sintéticos, e um
  relatório anterior meu chamou o segundo de "cena real de jogo" — corrigido lá e aqui.
- **PELE E ROSTOS e TOM GLOBAL continuam sem efeito medido**, e a causa mais provável é a
  ausência de conteúdo que os exercite, não um defeito. Só jogo responde.
- **Uma amostra por ponto** nas tabelas de imagem; as de fps têm duas.
- A GPU esteve compartilhada em parte da sessão (o usuário jogando). As comparações são
  relativas e feitas em sequência.

---

## 8. Arquivos

| arquivo | o que mudou |
|---|---|
| `amd\feeder-amd\src-feeder\src\dlss5-feed.cpp` | perfil da rede por quadro, recusa de dois motores, `dlss5-feed.status` |
| `amd\tests\medir-residual.ps1`, `medir-supersample.ps1`, `nitidez_png.exe` | (já existiam) |

Para a `bin\`: recompilar `dlss5-feed.addon64` de `amd\feeder-amd\build\`. **Nada liga
sozinho:** `nr_spike` continua `0` de fábrica, e sem ele todo este relatório é código
dormindo.
