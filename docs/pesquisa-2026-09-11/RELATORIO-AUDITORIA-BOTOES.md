# Auditoria independente dos botões do painel End

**Data:** 11/09/2026, 15:06 às 16:40
**Cópia auditada:** `DLSS5ForAll-AMD\bin-auditoria\` (cópia integral de `bin\`, `Copy-Item -Recurse`, sem hardlinks)
**Produção não foi tocada.** Nenhum processo alheio foi encerrado: só instâncias cujo `Path` apontava para `bin-auditoria` e alvos criados por mim, sempre por PID.

## Conferência dos binários

| arquivo | SHA-256 medido | confere |
|---|---|---|
| `DLSS5ForAll.exe` | `C13671EE11316907BD872A847E4744B577F52F9AA967AD032A8AA33C9C1C8A34` | sim |
| `dlss5-bridge.addon64` | `7B79BDF627FF2C3CE0F95CC0B97EA69383F6DD5DD1FBC2DE3A8B4AE32C83C5B5` | sim |

---

# ERRATA — leia antes do resto

**Uma conclusão deste relatório foi publicada errada e está retirada.** Eu afirmei que, no perfil de fábrica (Modelo B, 1 passe), ligar e desligar o DLSS 5 entregava o mesmo pixel — MAE 0,000, mesmo SHA-256 — e que era essa a explicação do "o painel mostra que está ligando e não está".

**Isso é falso. Com 1 passe a rede AGE.** O agente `um-passe` reproduziu o meu MAE 0 e achou a causa; a refutação é dele, não minha.

**Medido por ele, na bancada dele:** 1 passe contra a rede desligada dá **MAE 6,86 · 98% dos pixels**, com `CreateFeature` ×1 e `evaluation=1` no ReShade.log.

**Duas armadilhas de bancada produziram o meu falso zero, e elas se somaram:**

1. **O frame skipping estava ligado.** Minha cópia veio de `bin\` com `alterna_quadros=2` e `alterna_modo=sintetizar`. O quadro pulado sai por `ApresentarSemDesenhar`, com a cadeia suprimida, e o `DLSS5_DUMP` cai nele às vezes. Seis rodadas do `um-passe` com 1 passe, mesma configuração: **0,18 · 6,69 · 6,56 · 6,66 · 6,75 · 0,18**. O valor honesto é ~6,7; o 0,18 é o dump que caiu num quadro pulado. **E isso explica a assimetria que me enganou:** com 1 passe o app é rápido e pula; com 10 passes ele é lento e não pula — por isso o par de 10 passes deu diferença enorme e o de 1 passe deu zero.
2. **O meu par "HookPoint 2 vs 0" nunca mediu o que eu pensei.** Eu gravava a chave no ini e reabria o app — e o arranque **religa o neural** (`o Off era nosso -> Upscaled de volta`). Os dois dumps do par eram, portanto, **com a rede ligada nos dois casos**. Saírem idênticos não era achado nenhum; era o esperado.

**A conclusão correta:** 1 passe age; o perfil de fábrica não é inerte; e a queixa original do usuário continua sem explicação medida.

**O defeito real que sobrou desta investigação** é outro, e é de release: **o pacote embarca o `[ultima]` de teste do usuário em vez de um estado inicial limpo** — `alterna_quadros=2` com `alterna_modo=sintetizar` no `dlss5forall.ini`, e `PassCount=2` com `Intensity=0.71` no `ReShade.ini`, ambos divergentes do que o ini do app declara (`nr_passes=1`, `nr_intensidade=1`).

Tudo o que vem abaixo já está corrigido para esta errata.

---

## A REGRA DESTA AUDITORIA: a prova é a imagem

O usuário já foi enganado por painel que anuncia e não entrega. Aqui, **nada que o app escreve na tela conta como prova** — nem "HABILITADO", nem cartaz, nem contador. A evidência principal de cada linha é o pixel que chegou à tela.

**Como o pixel foi capturado.** `DLSS5_DUMP=<arquivo.png>` grava o **backbuffer da película** e encerra o app (`R.SaveBackbufferPng`, `main.cpp:16972`). É a saída final, depois de toda a cadeia — não a fonte, não um print de tela (que passaria pelo tone map do DWM).

**Fonte fixa.** `amd\tests\alvo_animado.exe --img cena.png`, com uma cena minha de detalhe em várias escalas (círculos, diagonais finas, blocos de ruído 4x4, texto em três corpos, degraus de cinza). Cena parada de propósito: com a cena andando, o acúmulo temporal do DLSS entra na conta e a medida deixa de ser sobre o botão. A cena e os medidores estão em `auditoria-botoes\`.

**Medidor.** Escrito para esta auditoria (`auditoria-botoes\medidor-img.ps1`): busca o alinhamento (dx, dy em ±10) que minimiza o erro, e devolve **MAE por canal**, **percentual de pixels com diferença acima de 8/255**, PSNR e pico. Todos os dumps abaixo saíram **1596x891 com alinhamento dx=0 dy=0**.

**Determinismo — e o erro que ele me fez cometer.** Eu tomei dois dumps bit a bit idênticos (mesmo SHA-256, MAE 0,000) como prova de que o método era determinístico. Não era prova disso: os dois dumps eram de **configurações diferentes**, e sair idênticos era o sintoma de um artefato, não a medida de um método estável. Ver a Errata, no topo. Determinismo se prova repetindo a **mesma** configuração, nunca comparando duas diferentes.

**As duas referências fixas** contra as quais tudo é comparado:

| referência | o que é | como foi feita |
|---|---|---|
| **fonte crua** | o quadro sem processamento nenhum | dump com `dlss5-feed.cfg enabled=0` (`base-feed-off.png`) |
| **cadeia inteira** | o quadro com o DLSS 5 no perfil em teste | dump com tudo ligado |

Contraprova da referência: fonte crua contra a cadeia inteira dá **MAE 2,674/255 e 6,70% dos pixels** — a cadeia realmente altera o quadro.

**Contaminação registrada.** Outros agentes rodaram testes na mesma máquina durante parte da auditoria (`bin-teste-requiem`, `bin-teste-umpasse`). Dois efeitos: (a) o `alvo_animado` tem título fixo `ZEBRA7`, e uma vez o app capturou a janela de outro agente — o dump saiu 1276x711, foi descartado, e daí em diante a bancada passou a rodar presa ao `--window-hwnd` do meu alvo, com o dump recusado se a resolução não bater; (b) a GPU ficou compartilhada, então **os fps absolutos valem como comparação A/B em sequência curta, não como número publicável**. As medidas de imagem não são afetadas por carga.

---

## TABELA FINAL

| # | botão | veredito | imagem mudou? (MAE · % de pixels) | outra evidência |
|---|---|---|---|---|
| a | PASSES 1 → 10 | **PASSA** | **42,72/255 · 95,49%** | `PassCount=10`, feed CPU 0,57 → 6,89 ms |
| b | MODELO A → C | **PASSA** | **7,61/255 · 55,04%** | `Style` 0 → 2, reabertura PID |
| c | INTENSIDADE 100% → 0% | **PASSA** | **9,55/255 · 69,04%** | `Intensity=0`, e bate com a rede desligada |
| d | RESOLUÇÃO Auto → 58% | **PASSA** | **7,01/255 · 37,72%** | `building: 924x516 (58%)` + `feature ready` |
| e | FRAME SKIPPING Desl. → 1 de 2 | **PASSA no efeito**; veredito do resultado SUSPENSO | **11,33/255 · 51,76%** contra a fonte | present 59,7 → 30,0, mas ver ressalva |
| f1 | FLUIDEZ [+] até **120** | **FALHA** | não se aplica (é taxa) | present não muda; log mostra o liga-desliga |
| f2 | FLUIDEZ [+] até **150** | **PASSA** | não se aplica | película 86,3 fps contra 60 de chegada |
| f3 | FLUIDEZ [−] | **PASSA** | não se aplica | teto 55 → present 48,2 |
| f4 | FLUIDEZ Destravar | **PASSA**, com efeito contrário ao nome | não se aplica | tira o teto e DESLIGA os projetados |
| f5 | FLUIDEZ [−] depois de Destravar | **FALHA** | não se aplica | item `ativo=0`, três cliques sem efeito |
| g | HABILITADO desligar/ligar | **PASSA ao vivo**; par de imagem PENDENTE de refazer | medida retirada — ver Errata | `HookPoint 2→0→2`; feed 59 → 2 → 60 fps |
| h | Trocar fonte ou placa | **PASSA** | não se aplica (é janela) | seletor reabre com a fonte pré-selecionada |
| i | Modo comparativo | **PASSA** | esquerda **2,16** da fonte, direita **0,54** da processada | costura de 147,8/255 no meio |
| j | Tecla End e botão flutuante | **PASSA** | não se aplica (é janela) | `por: a tecla (WM_HOTKEY)` / `por: o botao flutuante` |

---

## Como a errata se sustenta nos meus próprios dumps

A refutação é do `um-passe`, mas ela também aparece nos dumps que eu já tinha, sem subir nada. Mesma configuração (Modelo B, 1 passe, `HookPoint=2`), mudando só o frame skipping:

| dump | skipping | contra a fonte crua |
|---|---|---|
| `base-habilitado` | LIGADO | MAE 2,674 · 6,70% |
| `skip-off` | DESLIGADO | **MAE 11,496 · 51,36%** |
| os dois entre si | — | MAE 9,266 · 47,78% |

O artefato **não é determinístico** — com 10 passes e o skipping ligado o mesmo frame 300 ainda deu 95,69% contra a fonte crua. É a assimetria que a errata explica: com 1 passe o app pula, com 10 passes ele não pula.

**O que continua de pé:** toda medida feita com o skipping desligado, e toda medida cuja diferença é grande demais para ser artefato — PASSES, MODELO, INTENSIDADE, RESOLUÇÃO e COMPARATIVO. **O que caiu junto:** o par HookPoint ligado/desligado, que precisa ser refeito com `alterna_quadros=1` **e sem passar por reabertura do app** (o arranque religa o neural). Medida **pendente**, exige GPU.

> **Regra que fica:** antes de qualquer dump comparativo, gravar `alterna_quadros=1` e conferir no `dlss5forall.log` que o quadro do dump não foi um `sintetizar`. Nunca desligar o neural gravando o ini e reabrindo — o arranque o religa. E desconfiar de **MAE perto de zero entre dois estados que deveriam diferir**: é quase sempre artefato de captura, não prova de que o botão é inerte.

---

## Botão a botão

### (a) PASSES 1 → 10 — PASSA

Clique → reabertura automática em 2,5 s (`perfil da rede: PASSES mudou ha 2500 ms - reabrindo o app`), PID 5888 → 34656, `ReShade.ini` com `DirectNeuralRenderingPassCount=10`.

| | 1 passe | 10 passes |
|---|---|---|
| **contra a fonte crua** | MAE 2,674 · 6,70% | **MAE 44,05 · 95,69%** |
| **1 passe contra 10 passes** | — | **MAE 42,72 · 95,49% · pico 237** |
| feed CPU | 0,57 ms/frame | 6,89 ms/frame |
| frame interval (feed) | 16,7 ms (59,7 fps) | 51,25 ms (19,5 fps) |

Reversível: voltar ao chip 1 devolveu `PassCount=1`, 0,53 ms/frame e 60,0 fps.

### (b) MODELO A → C — PASSA

Reabertura (PID 33016 → 29232 para A, 3284 → 33580 para C), `Style` 0 e 2, ini `nr_estilo` idem.

| | contra a fonte crua |
|---|---|
| Modelo A | MAE 6,738 · 28,43% |
| Modelo C | MAE 11,817 · 73,87% |
| **A contra C** | **MAE 7,610 · 55,04% · pico 37** |

O modelo muda a imagem **mesmo com 1 passe** — e o Modelo B, que eu havia dado como inerte, não é: com 1 passe ele age (MAE ~6,7 contra a rede desligada, medido pelo `um-passe`). Ver a Errata.

### (c) INTENSIDADE 100% → 0% — PASSA

Reabertura (PID 7732 → 15832), `DirectNeuralRenderingIntensity=0`, ini `nr_intensidade=0`.

- 100% contra 0%: **MAE 9,546 · 69,04% · pico 58**
- **0% contra a rede desligada pelo HookPoint: MAE 0,021 · 0% · pico 2**

A segunda linha é a checagem cruzada mais limpa da auditoria: "intensidade zero" e "rede desligada" são o mesmo destino, alcançado por dois caminhos independentes, e as imagens coincidem. Confirma que o rótulo do controle ("quanto do resultado da rede entra na imagem final") descreve o que ele faz.

**Ressalva (defeito de baixa gravidade):** clicar no chip **já selecionado** grava o mesmo valor e ainda assim dispara a reabertura (`DirectNeuralRenderingIntensity = 1 (gravado...)` seguido de `INTENSIDADE mudou ha 2500 ms - reabrindo o app`). Uns 10 s de reabertura e uma piscada por um clique que não muda nada. Um `if (novo == atual) return;` resolve.

### (d) RESOLUÇÃO DO DLSS Auto → 58% — PASSA

O único controle que **vale ao vivo, sem reabrir o app** (PID 33244 antes e depois).

```
15:58:41.248  resolucao do DLSS: troca #1, 100% -> 58% (escolha da pessoa)
15:58:44.545  [feed] building: 924x516 work resolution (58%) -> 1596x891 backbuffer
15:58:44.559  [feed] feature ready: 924x516 DLAA, flags=66
```

Do clique à rede pronta: **3,3 s**. O `holding the feature (re)build` foi de 59/59 quadros, o normal — **não houve hold permanente**, e o evaluate voltou a rodar.

- Auto (que resolve para 100%) contra 58%: **MAE 7,011 · 37,72% · pico 99**
- fonte crua contra 58%: MAE 12,889 · 51,23%

Nota: **Auto resolve para 100%** nesta fonte (`troca #1, 100% -> 100% (automatico)`). Quem clicar em Auto esperando ganho de fps não vê nada.

### (e) DLSS FRAME SKIPPING Desligado → 1 de cada 2 — PASSA no efeito, FALHA no resultado

| | desligado | 1 de cada 2 |
|---|---|---|
| LAT present | 59,7 | **30,0** |
| LAT chegada | 60,9 | 60,0 |
| regularidade | 595 quadros/10 s, típico 20,0 ms | 301 quadros/10 s, típico 31,0 ms |
| frame interval (feed) | 16,80 ms (59,5 fps) | 46,03 ms (21,7 fps) |
| feed CPU | 0,56 ms/frame | 0,74 ms/frame |

**Imagem:** com o skipping ligado, o quadro na tela **continua processado** — contra a fonte crua dá **MAE 11,328 · 51,76% · pico 245**. Esse ponto está provado. Contra o dump com skipping desligado dá MAE 0,694 · 0,73%, o esperado numa cena parada.

**RESSALVA DE MÉTODO, e ela suspende o veredito.** `LAT present` e `regularidade` contam só o Present do quadro **processado** (`DrawAndPresent`); o quadro sintetizado sai por `ApresentarSemDesenhar` e **não entra nesses contadores**. Portanto o "present 30/s" acima **não prova** que a tela perdeu metade dos quadros — prova apenas que metade dos quadros deixou de passar pela cadeia, que é o que o recurso promete fazer. A medida certa é contar **quadros únicos por segundo** capturando a janela da película **por fora**, com o alvo **em movimento**, e comparar um quadro sintetizado com o processado anterior (deve aparecer deslocado por warp, não idêntico). Essa medida está **pendente**.

Como corroboração, não como prova: o `dlss5forall.log` registra `sintetizar: 202 quadros desenhados | processados 30.0/s | caiu no 'segurar' 0 vezes (sem base 0, sem campo 0)` — ou seja, o próprio app diz estar desenhando os pulados. É contador do app, e por isso não vale sozinho nesta auditoria.

**O que continua de pé no item:**

1. **A fluidez não reavalia** depois da mudança — a última decisão dela é anterior ao clique e nunca é revista.
2. **Não há ganho que pague a conta** nesta bancada: o feed CPU mal se move (0,56 → 0,74 ms) e o evaluate fica abaixo de 1 ms. O recurso só se justifica onde o evaluate domina o quadro.
3. **O quadro na tela continua processado** (MAE 11,33 · 51,76% contra a fonte crua) — esse ponto está provado.

**Sobre o que o enunciado esperava e não existe:** não há técnica do ReShade alternando entre drenagens. O `ReShade.log` registra `[DLSS 5 Feed] DLSS5_Feed.fx is not loaded (technique/textures missing)` — nesta configuração a rede roda pelo caminho direto do RenoDX (`CreateFeature(Reserved18) succeeded: handle=0x28682f7c240 size=1596x891 performance=6 preset=1`), e o skipping é implementado pelo mecanismo `sintetizar`.

### (f) FLUIDEZ — dois passam, dois falham

Aqui a prova é a **taxa**, não a imagem: a fluidez não muda o conteúdo do quadro, muda quantos quadros vão à tela.

**[+] até 120 — FALHA.** O número grava (`fluidez do DLSS 5 na tela: 120 fps`, ini `fps_teto_pelicula=120`) e o present não muda: 59,4 → 60,0, feed em 16,79 ms. O log pegou a causa em flagrante, em dois segundos seguidos:

```
16:05:12.743  fluidez: 1 quadro(s) projetado(s) por chegada (pedido 120 na tela, o jogo entrega 60)
16:05:13.741  fluidez: so os quadros que chegam (pedido 120 na tela, o jogo entrega 61)
```

A razão é piso: `quer = (int)(tetoFps / fps)`. `floor(120/60)=2` liga; `floor(120/61)=1` desliga. Com a chegada oscilando entre 60 e 61 — o normal — **o mecanismo liga e desliga sozinho e acaba desligado**. Com fonte a 60, toda a faixa de 70 a 119 fps é morta, e 120 é a borda exata.

**[+] até 150 — PASSA.** `fluidez: 25x/s extrapolados | chegada 61.0 | pelicula 86.0`, e o feed confirma de fora: **frame interval 11,58 ms = 86,3 fps** com a fonte entregando 60.

**[−] — PASSA** quando há teto: 65 → 60 → 55, present cai para 48,2 (feed 20,45 ms, 48,9 fps).

**Destravar — PASSA, com efeito contrário ao nome.** Tira o teto e, com isso, **desliga os quadros projetados**: a película caiu de 86,3 para 60,0 fps (= a chegada), porque a condição de projetar exige `tetoFps > 0`.

**[−] depois de Destravar — FALHA.** O item fica `ativo=0` (é `enabled = !semTeto` no código) e três cliques não produziram uma linha de log. Quem destrava não volta pelo [−]; só pelo [+], que reinicia em 65.

**Recomendação:** arredondar para cima em vez de para baixo, e habilitar o [−] no estado destravado.

### (g) HABILITADO → desabilitar → habilitar — PASSA ao vivo

```
16:22:14.846  neural: HookPoint=2 -> 0 (pela ponte, na config viva do ReShade)
16:22:14.846  DLSS 5 DESABILITADO (estava: neural=1 camada=1)
```

Grava em três lugares: config viva do ReShade, `ReShade.ini` (`HookPoint=0`) e ini do app (`neural_off_nosso=1`).

**Imagem — PENDENTE.** Os pares que eu tinha (Modelo C: MAE 9,553 · 69,05%; Modelo B: MAE 0,000) não valem, por duas razões somadas (ver a Errata): foram tomados com o **frame skipping ligado**, e o "desligado" era obtido gravando o ini e **reabrindo o app**, cujo arranque religa o neural — ou seja, os dois lados do par estavam com a rede ligada. **O par precisa ser refeito com `alterna_quadros=1` e desligando pelo card na sessão viva, sem reabertura.** Até lá, a evidência do card é a de log e de taxa, abaixo — sólida e independente do dump.

**Taxa:** com a rede desligada o feed cai de 59,3 para 2 fps (intervalo de 500 ms, o ritmo ocioso); religando volta a 59,6 fps e o present a 59,9. Desligar é imediato, ligar leva os ~3 s da contagem.

**O app abre sempre com o DLSS ligado — e isso é decisão do usuário, não defeito.** Mesmo com `HookPoint=0` no `ReShade.ini` e `neural_off_nosso=1` no ini, toda abertura registra `neural na abertura: estado=1` (o arranque faz `o Off era nosso -> Upscaled de volta`). Eu havia classificado isso como defeito de persistência; está retirado da lista. O comportamento é intencional: o programa abre ligado.

> O que esse comportamento causa é uma **armadilha de bancada**, não um problema para quem usa: ele impede fotografar o estado desligado por dump, porque o dump exige reabrir o app e o arranque religa a rede. Os dois dumps que eu havia nomeado `card-desabilitado` e `card-habilitado` saíram, por isso, **os dois com a rede ligada** (MAE 0,315 entre eles, ambos a ~74% da fonte crua). Descartados. Para medir o card por imagem é preciso desligá-lo **na sessão viva** e capturar sem reabertura.

### (h) Trocar fonte ou placa — PASSA

```
16:38:44.803  painel: 'Trocar fonte ou placa' - reabrindo o app sem fonte (volta ao seletor)
16:38:44.803  reabrindo: ... --reabrindo 0 --esperar-pid 33988 --pre-fonte janela --pre-janela "ZEBRA7 ..." --placa "NVIDIA GeForce RTX 4080"
16:38:45.284  seletor: pre-selecao pela linha de comando (fonte=janela janela='ZEBRA7 ...')
```

PID 33988 → 31284. Janela do seletor conferida na tela: "DLSS5 For All 1.2 — escolha a fonte", 950x965 em 485,27, com a fonte já escolhida.

**Falso alarme descartado:** durante uma rodada havia na tela uma caixa `DLSS5ForAll.exe - Imagem Inválida` (classe `#32770`, dona `csrss`). Ela **já estava aberta antes de eu subir qualquer instância** — aparece na primeira enumeração de janelas desta auditoria, feita antes do primeiro `Start-Process`. É resíduo de execução anterior, não consequência deste botão.

### (i) Mais opções → Modo comparativo — PASSA

Vale ao vivo, sem reabrir; persiste em `[comparar] ligado=1`.

Recortei as duas metades do dump e comparei cada uma com as duas referências:

| metade do comparativo | contra a **fonte crua** | contra a **cadeia processada** |
|---|---|---|
| **ESQUERDA** | **MAE 2,157 · 2,64%** | MAE 11,185 · 81,63% |
| **DIREITA** | MAE 9,98 · 66,33% | **MAE 0,541 · 0,84%** |

Cada metade cola na referência certa e se afasta da outra: esquerda é a fonte, direita é o DLSS 5.

A costura no meio confirma pelo outro lado. O salto entre colunas vizinhas na coluna central, contra a mediana das demais colunas:

| | salto na coluna do meio | mediana das outras | razão |
|---|---|---|---|
| comparativo desligado | 21,24/255 | 6,12 | 3,5× |
| comparativo ligado | **147,77/255** | 7,62 | **19,4×** |
| (bancada com fundo mais liso) desligado | 1,23/255 | 1,07 | 1,1× |
| (bancada com fundo mais liso) ligado | **127,28/255** | 1,12 | **113,6×** |

E este resultado responde de tabela ao "processado ≠ fonte": a metade direita difere da fonte crua em **66,33% dos pixels**, no mesmo quadro e na mesma captura.

### (j) Tecla End e botão flutuante — PASSA

```
16:38:22.423  painel aberto em 1248,0  672x1004  visivel=1  itens=28  escala=1.19  por: a tecla (WM_HOTKEY)
16:38:24.580  painel fechado
16:38:26.875  painel aberto ... por: o botao flutuante
```

Conferido também de fora do app, por `GetWindowRect` num processo DPI-aware: uma janela do painel depois do primeiro toque de End, zero depois do segundo, uma de novo depois do clique no botão flutuante (janela `DLSS5ForAllBotao`, 57x57 em 3625,190).

---

## Armadilhas de método, para quem repetir

1. **O log de itens sai antes do reflow.** `DLSS5_LOG_ITENS=1` publica as coordenadas na **primeira** abertura do painel, antes de o `Paint` encolher a escala e redimensionar a janela. Medido: o log dizia `705x968` com itens em `x` até 1835, e a janela real era `451x619`. Clicar por essas coordenadas não aciona nada, e o sintoma é idêntico ao defeito do `g_scale` já corrigido. **Feche e reabra o painel** e use os itens da segunda abertura; confira sempre que o centro do item cai dentro do `GetWindowRect`.
2. **O alvo tem título fixo.** Com mais de um agente medindo, `--window ZEBRA7` pode capturar a janela de outro. Use `--window-hwnd` e recuse qualquer dump cuja resolução não bata com a da bancada.
3. **Encerre alvos por PID.** Durante esta auditoria alguém encerrou `alvo_animado` por nome e derrubou bancadas alheias duas vezes (`FATAL: --window-hwnd: a janela nao existe mais`). Vale a mesma regra que já existe para o app: por PID, ou por caminho da pasta de teste.
4. **Cena de teste com detalhe de 1 pixel não serve.** A primeira tentativa usou xadrez de 1 px; o alinhamento e a reamostragem destroem a medida (MAE 90/255 entre imagens equivalentes). Detalhe em várias escalas resolve.
5. **Nunca desligue o neural gravando o ini e reabrindo o app.** O arranque religa (`o Off era nosso -> Upscaled de volta`). Os dois lados do par saem com a rede ligada e a comparação vira zero. Desligue pelo card, na sessão viva.
6. **Mantenha TODAS as janelas no monitor secundário.** Durante esta auditoria a tela do usuário, no monitor primário, travou duas vezes com várias bancadas na GPU ao mesmo tempo. O alvo e a película estavam no monitor 2 (2100,120), mas **o painel End abria em 1248,0 (672x1004) e o seletor em 485,27 (950x965)** — os dois com X < 1536, ou seja, no primário, e o painel é uma janela `WS_EX_LAYERED` topmost repintada a cada segundo. É a causa provável do peso, e é evitável: arraste o painel para o monitor 2 antes de medir (a posição fica gravada em `painel_x`/`painel_y` no ini). Some-se a isso a regra de coordenação: **uma bancada por vez na máquina**.

## O que fica aberto

1. **A produção embarca estado de teste, não padrão de fábrica.** O `dlss5forall.ini` de `bin\` vai com `alterna_quadros=2` + `alterna_modo=sintetizar` (frame skipping LIGADO). O `ReShade.ini` de `bin\` vai com `PassCount=2` e `Intensity=0.71`, divergentes do ini do app (`nr_passes=1`, `nr_intensidade=1`) — o app corrige na abertura, mas o pacote sai inconsistente. **Decidir o estado inicial antes de publicar.**
2. **Fluidez entre 1× e 2× da taxa do jogo** — arredondamento para baixo mata a faixa; 120 sobre 60 é o caso mais provável de uso.
3. **[−] da fluidez morto depois de Destravar.**
4. **Frame skipping sem compensação aparente** — mas ver a ressalva de método no item (e): os contadores usados não enxergam o quadro sintetizado, e a medida correta é contar quadros únicos por captura externa. **Pendente.**
5. **Reabertura à toa** ao clicar num chip de perfil já selecionado.
6. **Pendente de medir:** o par HookPoint ligado/desligado, com o skipping desligado e sem reabertura; e se o quadro pulado é sintetizado ou repetido.

Saiu desta lista: *"o desligado não persiste"*. O app abrir sempre com o DLSS ligado é decisão do usuário, não defeito — ver o item (g).

## Duas medidas pendentes, e por quê

Ficaram de fora porque exigem subir o app, e a GPU foi bloqueada por ordem do usuário (a tela do monitor primário travou com várias bancadas rodando ao mesmo tempo):

- **par HookPoint ligado/desligado com `alterna_quadros=1`** — é o que fecha ou reabre a questão do card;
- **contagem de quadros únicos por captura externa da película**, com o alvo EM MOVIMENTO, para saber se o quadro pulado é sintetizado (≈60 únicos/s) ou repetido (≈30). O `LAT present` e a `regularidade` contam só o Present do quadro processado (`DrawAndPresent`); o sintetizado sai por `ApresentarSemDesenhar` e não entra nesses contadores — **então o "present 30/s" do item (e) NÃO prova que a tela perdeu metade dos quadros**, e o veredito "FALHA no resultado" daquele item fica suspenso até a contagem externa.

## Arquivos

Dumps, cena de referência e os três medidores estão em `amd\docs\pesquisa-2026-09-11\auditoria-botoes\` (30 MB): `cena-fonte.png`, `base-feed-off.png` (fonte crua), `passes-1/10`, `modelo-A/C`, `intens-0`, `res-auto/58`, `skip-off/2`, `comp-on`, mais `medidor-img.ps1`, `medidor-costura.ps1` e `gerar-cena.ps1`.
