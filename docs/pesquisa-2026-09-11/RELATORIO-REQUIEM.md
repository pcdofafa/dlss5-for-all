# Painel End e Resident Evil Requiem — diagnóstico, correção e prova

**Data:** 2026-09-11 · **Máquina:** Windows 11, RTX 4080 + iGPU AMD, monitor primário 1920×1080 a
125% (área útil 1020 px) e secundário 3440×1440 a 150% (área útil 1368 px, 100 Hz).
**Origem:** relato do usuário testando ao vivo o *Resident Evil Requiem* (`re9.exe`) com o app
rodando de `DLSS5ForAll-AMD\bin\`.

Tudo abaixo foi medido **sem tocar** na sessão dele: os processos `re9.exe` (PID 408) e
`DLSS5ForAll.exe` (PID 24024, depois 15508) nunca foram encerrados nem alterados, e nada foi
escrito em `bin\`. Os logs de `bin\` foram lidos, e só. O trabalho ficou em `amd\` (fonte) e a
prova em `DLSS5ForAll-AMD\bin-teste-requiem\` (cópia isolada), sempre no monitor **secundário**.

> **Armadilha de leitura, registrada porque custou tempo:** `Get-ChildItem` mostrava
> `dlss5forall.log` com **0 bytes** enquanto o arquivo tinha 345 linhas. O NTFS não atualiza o
> tamanho na entrada de diretório enquanto o arquivo está aberto para escrita. Quem for ler log
> de sessão viva use `Get-Content`, nunca o tamanho.

> **Armadilha de bancada, nova:** `PostMessage(WM_LBUTTONUP)` aciona um controle do painel (é o
> método da memória `testar-botoes-sem-roubar-o-mouse`), mas `PostMessage(WM_MOUSEMOVE)` **não**
> produz hover — o `g_hot` não muda e a dica nunca é pedida. Comprovado no mesmo ponto de tela:
> o clique postado acertou `item 15`, o move postado deixou `g_hot` em `-1`. Para testar hover é
> preciso `SetCursorPos` de verdade (guardando e devolvendo a posição do cursor).

---

## Índice

| Item | Assunto | Situação |
|---|---|---|
| A | Atalhos não chegam dentro do jogo | **fechado**, com prova |
| B | PASSES/MODELO/perfil não valem em tempo real | **fechado**, com prova dura |
| B-bis | "O pulo de quadros desliga o DLSS 5" (bloqueador) | **fechado**, com prova |
| C | Movimento sintetizado vira o padrão | **fechado** |
| D | A película passar do fps do jogo | **fechado**, medido |
| E | Painel denso e letras pequenas (+ dicas, rótulo, marca) | **fechado**, com fotos |

---

## A. Os atalhos não chegam dentro do Resident Evil

### O que foi medido antes de mexer em uma linha

**1. As hotkeys ESTAVAM registradas.** Um processo de teste tentando registrar as mesmas teclas
levou `ERROR_HOTKEY_ALREADY_REGISTERED` (1409) em End, F6, F7 e Alt+Esc — e `Insert` veio livre,
o que confirma que a recusa era do nosso registro e não de um erro genérico. O log dele também
não mostra recusa: `atalho painel = End`, sem o sufixo `(RECUSADO - ja em uso)`.

```
End:     OCUPADA (err=1409 ERROR_HOTKEY_ALREADY_REGISTERED)
F6:      OCUPADA (err=1409)
F7:      OCUPADA (err=1409)
Insert:  LIVRE
Alt+Esc: OCUPADA (err=1409)
```

Ou seja: `RegisterHotKey` (`main.cpp:1871`) **não** é o problema.

**2. A hipótese clássica está DESCARTADA, com evidência.** Nem o jogo nem o app rodavam
elevados — os dois em integridade média:

```
re9  (408)  : elevated=0 integridade=0x2000
app  (24024): elevated=0 integridade=0x2000
```

Não é UIPI por elevação. (A checagem continua no código para os casos em que **for**: ver
`AlvoAcimaDeNos`, abaixo.)

**3. Duas causas possíveis sobraram, e as duas passaram a ser tratadas:**

- **(a) o jogo segura as hotkeys do Windows.** Um jogo pode pedir `RIDEV_NOHOTKEYS` no Raw
  Input, e aí o sistema para de entregar `WM_HOTKEY` enquanto a janela dele está na frente. Não
  existe API para perguntar isso de fora — só dá para **observar a tecla por outro caminho** e
  comparar.
- **(b) o painel abria e fechava no mesmo instante.** `main.cpp` fechava o painel em **qualquer**
  `WA_INACTIVE`, e o comentário do próprio código dizia "clicou fora / o jogo voltou". Com jogo
  em tela cheia sem borda que reafirma o próprio primeiro plano, a sequência é: tecla → painel
  abre → `SetForegroundWindow` tira o foco do jogo → o jogo toma de volta → `WA_INACTIVE` →
  `Close()`. Milissegundos. Do lado de quem apertou: "o atalho não faz nada".

**4. Um defeito de diagnóstico, que é o que impedia responder isso antes:** o log dizia
`painel aberto em 1339,0` e não havia como saber se aquilo tinha sido o End, o ícone da bandeja
ou outra coisa — que é exatamente a pergunta do relato.

### O que entrou na fonte

1. **`namespace AtalhoReserva`** — um `WH_KEYBOARD_LL` em **thread própria**. Ele vê a tecla
   antes de qualquer janela, espera **250 ms** pelo `WM_HOTKEY` (que continua sendo o caminho
   principal) e **só age se a mensagem não vier**. Deduplicação nos dois sentidos: se o
   `WM_HOTKEY` chegar atrasado, ele é descartado com uma linha no log, e vice-versa — nunca
   duas ações para uma tecla.
   - A thread própria não é luxo: o Windows **remove** um hook de baixo nível cujo dono demora
     mais que `LowLevelHooksTimeout` (300 ms por padrão) para voltar do callback, e o nosso laço
     principal já registrou `present travou 787 ms` num arranque. Na thread dedicada o callback
     só compara números.
   - Ele **não engole a tecla** (devolve sempre `CallNextHookEx`).
2. **Carência de 700 ms no `WM_ACTIVATE`** do painel: perder o foco logo depois de abrir deixa o
   painel **na tela**, apenas sem o teclado (ele é todo de mouse, e o clique devolve o foco).
   Passada a carência, fechar ao clicar fora continua como sempre.
3. **Botão flutuante** (`namespace BotaoFlutuante`): pastilha topmost `WS_EX_NOACTIVATE` na borda
   da camada. Clique abre/fecha o painel **sem tirar o primeiro plano do jogo**; arrastar move
   (posição gravada em `[ultima] botao_x/botao_y`); botão direito esconde até a próxima abertura;
   `[ultima] botao_flutuante=0` desliga de vez. Fica fora da captura, como o painel.
4. **`AlvoAcimaDeNos()`**: se o jogo for elevado e nós não, sai um cartaz dizendo exatamente o que
   fazer ("abra o DLSS5 For All como administrador"), e a linha correspondente no log.
5. **Origem da abertura no log**: `painel aberto ... por: a tecla (WM_HOTKEY)` /
   `por: a tecla (caminho reserva)` / `por: o botao flutuante`.
6. **`DLSS5_SEM_HOTKEY=1`** — interruptor de bancada que faz o app não registrar hotkey nenhuma,
   simulando o jogo que as segura. Sem ele não havia como **provar** o caminho reserva.

### A prova (cópia isolada, monitor secundário, alvo sintético ZEBRA7)

| # | Caminho | Linha do log |
|---|---|---|
| 1 | botão flutuante | `painel aberto em 3373,8 ... por: o botao flutuante` |
| 2 | tecla, caminho normal | `painel aberto ... por: a tecla (WM_HOTKEY)` |
| 3 | tecla, com as hotkeys indisponíveis | `atalho 'painel': a tecla foi apertada e o WM_HOTKEY NAO chegou em 250 ms - acionado pelo caminho reserva. A janela na frente era pid 19764 classe #32770` seguido de `painel aberto ... por: a tecla (caminho reserva)` |

Na prova 2 **não** saiu linha de reserva — a deduplicação funciona: quando o caminho normal
responde, o reserva se cala.

---

## B. PASSES, MODELO e o perfil não valem em tempo real

Este era o item de prioridade máxima, e **não** é o bug da memória
`nr-para-de-avaliar-ao-mudar-resolucao-de-trabalho` (o feeder não entrou em
`holding the feature (re)build` em nenhuma rodada desta bateria). É outra coisa — e o app estava
**mentindo no log**.

### A causa

O add-on **RenoDX** (`renodx-dlss.addon64`, de terceiro) lê o perfil da rede **uma vez, quando
carrega**, e nunca mais relê a config do ReShade. A nossa ponte grava certo nos três lugares
(`[ultima]` do nosso ini, config viva do ReShade, `ReShade.ini`) e o `set_config_value` devolve
sucesso — era por isso que o log dizia *"pela ponte, vale agora"*. Só que o add-on ignora.

### Prova 1 — o custo da rede diz quantos passes rodam

Não depende de olhar imagem nem de log de terceiro. Mesmo alvo (1596×891 e 1276×711), mesma cena,
`DirectNeuralRenderingPassCount` gravado nos dois arquivos nas três medidas:

| situação | feed CPU/quadro | fps da cadeia |
|---|---|---|
| 1 passe (linha de base) | **0,70 ms** | 61,0 fps |
| 10 passes escolhidos **no painel**, app aberto | **0,56–0,64 ms** | 61,0 fps |
| 10 passes lidos **na abertura** | **3,04–6,54 ms** | **24,6 fps** |

Nove vezes o custo e menos da metade do fps — com o **mesmo número** no **mesmo arquivo**. O
segundo caso simplesmente não estava rodando 10 passes.

### Prova 2 — o `options_revision` do próprio add-on

O RenoDX imprime `options_revision` a cada mudança de opção. No `ReShade.log` da sessão do
usuário com o Requiem esse número **só andou entre 04:29:32 e 04:29:58** — exatamente o intervalo
em que o overlay do ReShade esteve **aberto** e ele mexia nos sliders do RenoDX. Ficou parado em
29 até o fim da sessão (04:35:27), com o app rodando o tempo todo. Nenhum clique do nosso painel
mexeu nele; no teste controlado da cópia, passes 1→4 gravados nos três lugares deixaram o
revision parado em 1.

De quebra, isso explica a divergência no `bin\` dele: `DirectNeuralRenderingIntensity=0.71` no
`ReShade.ini` contra `nr_intensidade=1` no nosso — **o painel estava mostrando um estado que não
era o da rede**.

### Achado de tabela (o que ele fez, lido do log dele)

Na sessão das 04:28–04:35, o painel foi aberto **5 vezes** e houve **um único clique**
registrado. Nas outras quatro ele abriu, fechou em 2–6 s e foi direto para o ReShade com F7:

```
04:29:23 painel aberto ... escala=0.73      04:29:29 painel fechado → F7 (ReShade)
04:31:40 painel aberto ... escala=0.73      04:31:43 painel fechado → F7
04:32:19 painel aberto ... escala=0.73      04:32:23 painel fechado → F7
```

Isso conversa diretamente com o item E: o painel abriu com escala **0,73** — o piso — num monitor
a 125%.

### O que entrou na fonte

1. **O log e a UI pararam de prometer.** Agora: `perfil da rede: ... (gravado na config viva do
   ReShade e no ini; o add-on so le isto quando carrega - ver a reabertura)`.
2. **Reabertura rápida automática.** Qualquer ajuste de perfil/passes arma um relógio; cada novo
   toque **reinicia** (mexer em seis controles reabre **uma** vez); 1 s depois sai o cartaz
   `APLICANDO <controle>… reabrindo o app (uns 3 s)`; 2,5 s depois o app se reabre por
   `ArgsDeReabertura`/`ReabrirApp`, preservando alvo (por **HWND**), fonte, placa, pulo e
   intermitente. Não reabre em bypass nem com o conjunto desligado (F6) — o relógio fica armado.
   `[ultima] perfil_reabre=0` desliga, e aí o cartaz diz a verdade ("vale quando você reabrir").
3. **Log sem limite, corrigido no que é nosso.** As `tecnica X -> ligada/desligada` eram 8.526
   linhas em três minutos no log dele — não é defeito de lógica: no modo sintetizado a troca
   acontece de verdade ~50×/s. Agora saem as **12 primeiras** (que provam que o mecanismo subiu)
   e depois **um resumo a cada 30 s**.

### Prova da correção

```
[05:05:31] perfil da rede: passes = 10 (gravado na config viva do ReShade e no ini; ...)
[05:05:33] perfil da rede: PASSES mudou ha 2500 ms - reabrindo o app para o add-on ler o perfil novo (geracao 1)
[05:05:33] reabrindo o app (o perfil da rede mudou): ... --reabrindo 1 --esperar-pid 20892
           --source window --window-hwnd 3408362 --overlay --no-hide-self --no-vsync --no-hdr
           --placa "NVIDIA GeForce RTX 4080"
[05:06:00] [feed] 600 frames: feed CPU 6.54 ms/frame | frame interval 40.60 ms (24.6 fps)
```

E o mesmo pela janela nova, com o MODELO:

```
[05:32:11] perfil da rede: MODELO mudou ha 2500 ms - reabrindo o app para o add-on ler o perfil novo
dlss5forall.ini : nr_estilo=2        ReShade.ini : DirectNeuralRenderingStyle=2
```

Log do add-on, com a intermitente ligada:

```
... tecnica 'DLSS5_Feed' -> ligada  (daqui em diante so um resumo a cada 30 s)
[05:34:11] tecnicas ligadas/desligadas 4583 vezes nos ultimos 30 s (o quadro sintetizado
           suprime e devolve a cadeia a cada quadro)
```

**4.583 linhas viraram uma.**

### O que NÃO dá para corrigir daqui

As outras **21.074** linhas do log dele eram do RenoDX (`utils::device_proxy creating
device-only D3D12 proxy after first D3D11 present`, uma por present). É add-on de terceiro, sem
fonte, e não há chave para calá-lo. Fica registrado: em três minutos de jogo, 62% do
`ReShade.log` era essa linha.

---

## B-bis. "O pulo de quadros desliga o DLSS 5" (bloqueador de release)

Chegou pelo backlog às ~04:45: *"o PULO DE QUADROS NÃO PODE DESLIGAR O DLSS 5"*. **Não era o
DLSS que desligava — era o app que se convencia de que tinha desligado.**

`LerEstadoNeural()` decide "ATIVO" procurando as linhas de marco (`CreateFeature(Reserved18)
succeeded`, `EvaluateFeature succeeded`, `DLSS-NR source evaluation completed`) no **tail de
96 KB** do `ReShade.log`. Essas linhas saem **uma vez por sessão**. Quem enche o log empurra os
marcos para fora da janela, e a partir daí o app responde "não subiu" com a rede rodando:

- o nosso add-on escrevia uma linha por troca de técnica, e o quadro sintetizado troca ~50×/s —
  8.526 linhas em três minutos (corrigido no item B);
- o RenoDX escreve `device_proxy creating...` **uma por present** — 21.074 linhas na mesma
  sessão. **Só ele enche 96 KB em ~10 segundos**, e isso não dá para calar de fora.

Correção: "já avaliou nesta execução" virou **memória**, não releitura. Quem desliga o neural
nem chega nesse ponto (o passo 2 devolve DESLIGADO antes), então a flag não mantém ATIVO por
cima de um desligamento — ela só impede que o rolar do log apague um fato que aconteceu.

**Prova:** 60 s com a camada intermitente ligada, sem uma única linha `aviso: neural nao esta
ativo` (antes ela saía aos ~25 s).

---

## B-ter. O bloqueador de verdade: "on" e "off" na mesma drenagem

Diagnóstico do líder, confirmado e corrigido. A ponte aplica **técnica** no `reshade_present`,
no fim do Present em que drena — logo um comando enfileirado antes do Present(N) só vale a
partir do Present(N+1). A decisão `Sintetiza::Suprimir()` estava **entre os dois Presents da
volta**: na volta do quadro sintetizado o "on" entrava *depois* do Present dele, e na volta do
processado o "off" entrava *antes* do Present — os dois caíam na **mesma drenagem**, líquido
sempre desligado. A cadeia nunca rodava, em nenhum dos dois quadros.

```
04:34:16:99  tecnica 'Lumenite_Kernel'   -> ligada
04:34:16:99  tecnica 'DLSS5_Feed'        -> ligada
04:34:16:99  tecnica 'Lumenite_Kernel'   -> desligada   <- mesmo milissegundo, 12.790 linhas
```

**Correção:** o bloco da decisão (com `cadeiaRodouNeste` lido antes dela) subiu para **antes**
do bloco do quadro sintetizado, logo depois de `Alterna::Aplicar(hwnd, true)`. Assim o "on" da
volta S vale no Present P e o "off" da volta P vale no Present S — o ciclo pedido.

**Medido depois (alvo em movimento, 1 de cada 2, sintetizar):**

| | antes | depois |
|---|---|---|
| trocas de técnica | `ligada` e `desligada` no mesmo ms | `ligada` 18:164 · `desligada` 18:184 · `ligada` 18:203 |
| NGX evaluate | — | **0,77–0,90 ms** (> 0) |
| o que o feeder enxerga | — | **30,0 fps** com a chegada a 60,0 → a cadeia roda em 1 de cada 2 |
| sintetizar | — | `300 quadros desenhados · processados 30,0/s · caiu no 'segurar' 0 vezes` |

> **Aviso para quem for medir isto de novo:** o contador `processados N/s` **não serve de
> prova**. Ele lê `cadeiaRodouNeste`, que continuava `true` mesmo com o estado líquido
> desligado — por isso o log dizia "50/s" enquanto nada era processado. Quem prova é o
> `evaluate` do feed e o intervalo que o **feeder** enxerga.

**E a cintilação, agora medida com a cadeia rodando de verdade** (`[captura] sint_medir_luz=1`,
quatro janelas de 10 s, ~300 amostras cada):

```
sintetizar/luz: processado 134.34 (salto 6.23) | sintetizado 134.35 (salto 6.21) | diferença 0.01/255
sintetizar/luz: processado 133.98 (salto 6.30) | sintetizado 133.90 (salto 6.27) | diferença 0.08/255
```

**0,01 a 0,08 em 255** entre os dois tipos de quadro, contra **6,2** de variação da própria
cena. Ou seja: o quadro sintetizado e o processado têm a mesma luminância, e não há cintilação.
A medição de antes dava o mesmo número **pelo motivo errado** — não cintilava porque nada era
processado. Agora ela vale.

Repetida em **60 s corridos** (7 janelas de 10 s, ~260 amostras cada) depois de todas as
mudanças: diferença entre os tipos de **0,05 a 0,51 em 255**, contra 6,2–6,5 de variação da
cena. Continua muito abaixo do que o olho separa.

> **Um dado novo que a medição longa mostrou e vale registrar:** o salto de luminância **de um
> quadro para o seguinte** é maior no sintetizado (7,8–8,7) do que no processado (6,2–6,5),
> cerca de 25% acima. Isso **não** é cintilação entre os dois tipos — é a variação própria do
> quadro deformado pelo campo de movimento. Não incomodou em nenhuma medida e não mexi nisso;
> fica escrito porque, se um dia aparecer relato de "tremido" com o frame skipping ligado, é
> por aqui que se começa a procurar.

---

## C. Movimento sintetizado passa a ser o padrão

O interruptor "Movimento sintetizado" **saiu do painel**. A frase do usuário diz o motivo melhor
do que qualquer justificativa: *"o movimento sintetizado deve ser aplicado por padrão ao pulo de
quadros — não faz sentido ter um checkbox pra isso"*. E ele está certo por desenho: as duas
posições do interruptor não eram duas funções, eram **uma função e a falta dela** — desligado, o
quadro pulado mostrava o jogo cru (o tremular que o recurso existe para tirar).

- `Alterna::LerModoIni`: chave **ausente = sintetizar** (era `alfa`, ou seja, quem ligava a
  intermitente e não achava o interruptor recebia a versão pior do recurso de fábrica).
- Ligar a camada intermitente agora liga o mecanismo inteiro (garante o campo de movimento);
  desligar devolve a cadeia antes de qualquer outra coisa.
- O degrau de segurança continua e agora é automático: faltando base, campo ou transporte, o
  desenho cai no `segurar` sozinho, quadro a quadro (o contador `caiu no 'segurar'` do relatório
  de 10 s continua mostrando isso).
- `[ultima] alterna_modo` continua valendo como **sobrescrita avançada não exposta**
  (`alfa`/`regiao`/`segurar`/`sintetizar`).

---

## D. A película passando do fps do jogo

### Por que ela estava "travada"

Não era defeito, era desenho: **todo** present nosso nasce de uma chegada do WGC
(`presentPorQuadro` exige `got`). Sem chegada não há present — e o pulo e a intermitente só sabem
**dividir** essa taxa, nunca multiplicar. O quadro sintetizado devolvia o que a intermitência
tirava (empate), não acrescentava.

### O que entrou

Controle novo **FLUIDEZ** (`kFluidez`, `[ultima] fluidez`), dentro da mesma seção de quadros:
*Igual ao jogo / Dobrada / Triplicada*. Entre duas chegadas, a camada desenha 1 ou 2 quadros
**extrapolados** pelo campo de movimento.

A diferença entre interpolar e extrapolar é o que permite **não atrasar nada**:

- interpolar (`t` entre 0 e 1) = o quadro do meio entre o anterior e o atual — exige **segurar**
  um quadro, ou seja, latência;
- extrapolar (`t` negativo) = projeta adiante do atual, na direção em que a cena já ia — não
  segura nada; erra na borda e na desoclusão, e o erro dura um quadro.

O shader do `warp_frame.h` amostra `uv + t * mv` e não tem faixa fixa para `t`, então a
extrapolação coube na API que já existia.

**Tetos que mandam, e nenhum é negociável:** o refresh do monitor (acima dele o DWM descarta
antes de virar luz — `Sintetiza::ValePena`), o teto de fps avançado, e a **folga de present**,
que é medida e não suposta: se o Present extra custar mais que a fatia que ele tinha, o recurso
recua por 3 s; três recuos e ele se desliga na sessão, com cartaz e linha no log.

### A medição

Monitor secundário, **100 Hz**, alvo sintético a 8 px/quadro, intermitente desligada:

| | chegada do jogo | película | extrapolados |
|---|---|---|---|
| Fluidez **igual ao jogo** | 61,0/s | **61,0/s** | — |
| Fluidez **dobrada** | 61,0/s | **92–97/s** | 31–36/s |

A película passou de 61 para ~94 — **+54%**. Não chegou aos 122 teóricos porque o `ValePena`
corta em 95% do refresh (95 num monitor de 100 Hz), que é exatamente o teto que deveria mandar.

No painel, a linha de status responde à pergunta que não tinha resposta:
`chegada do jogo 61/s · película 94/s · 34 projetados por segundo`.

---

## E. O painel denso e as letras pequenas

### A causa era literal

As duas metades da frase do usuário ("muita informação logo de cara" e "as letras estão pequenas
por causa disso") são a **mesma** coisa: o painel é desenhado em unidades lógicas e multiplicado
pela escala do Windows; quando não cabia na altura da tela, o `Paint` **baixava essa escala** até
o piso de **0,70**. No monitor dele (125%, área útil 1020 px) o log registrou `escala=0,73` — o
texto chegava à tela a 73% do tamanho **base**, num monitor que pedia 125%. A menor fonte do
painel (13 px) saía com 9,5 px físicos.

**Tirar linha da primeira vista É aumentar a fonte.**

### O que entrou

1. **Piso novo: `kEscalaMinima = 1,00`** — nunca menor que o tamanho base. Abaixo disso não se
   encolhe: **esconde-se** conteúdo. Há um piso de emergência (0,80) só para tela que não
   comporta nem a primeira vista, e ele **avisa no log** em vez de agir mudo.
2. **Janela própria "Perfil da imagem"** (`namespace Perfil`, classe `DLSS5ForAllPerfil`), no
   mesmo molde da janela de Efeitos: mesma paleta, mesmo X no canto, Esc fecha, volta ao painel.
   Dentro dela, os 6 controles com espaço, rótulo legível e **uma linha de explicação por
   controle**, mais os 10 chips de PASSES com largura para o dedo acertar.
3. **Cartão-resumo no painel**, evidente: `Perfil da imagem (rede neural)` /
   `Modelo B · Intensidade 100% · 1 passe` / botão **Ajustar**. Quem olha o painel já sabe que
   há o que ajustar ali, sem precisar abrir nada — que era o risco de esconder controle atrás de
   botão.
4. **Primeira vista enxuta.** Critério: fica o que se mexe **no meio de uma partida**. Foram para
   "Mais opções": a linha Efeitos, o Grupo de ofertas, o Apoie o criador e o rodapé de teclas.
5. **As explicações viraram dica flutuante** (pedido dele, 11/09 ~05:30: *"reduziria muito a
   poluição visual"*). As quatro linhas de ajuda do pulo de quadros, as quatro da camada
   intermitente, a da fluidez e a da resolução do DLSS somavam ~180 unidades de altura de texto
   que se lê **uma vez na vida**. Agora elas aparecem num balão ao passar o mouse, com 350 ms de
   atraso (sem o atraso, atravessar o painel faz balões piscarem no caminho). O molde é o
   tooltip que o **seletor** já tem: janela layered própria, `WS_EX_TRANSPARENT` (nunca come um
   clique), título em negrito e corpo que quebra linha.
   - **O que NÃO virou dica**, e a distinção importa: a linha de status
     `chegada do jogo · película · projetados` e a estimativa de custo da resolução do DLSS
     **ficaram**. Elas não são ajuda — são informação viva, que muda com a escolha e com o que
     está acontecendo na tela.

### O resultado, medido

| | itens na primeira vista | escala desenhada (monitor a 150%) | altura pedida |
|---|---|---|---|
| antes | 58–61 | **0,94** | ~1.438 un. |
| depois do cartão-resumo | 24 | 1,14 | ~1.186 un. |
| depois de enxugar | 21 | 1,37 | ~987 un. |
| depois das dicas | **21** | **1,50 (o DPI cheio)** | **~804 un.** |

804 unidades × 1,25 = **1005 px**, e a área útil do monitor dele tem **1020**. Ou seja: o painel
passa a caber **sem encolher nada** — desenhado no DPI que o Windows manda (1,25), contra os
0,73 de antes. **A fonte cresce 71%.** A janela do perfil abre a **1,50**, o DPI cheio do
monitor secundário, também sem encolhimento.

Prova da dica (ela não sai em print — é layered e fica fora da captura, como o painel):

```
[05:51:00] painel: dica pedida para o item 15 (tem texto)
DICA visivel=True em 3559,1051  660x266
```

### O campo "FLUIDEZ DO DLSS 5 NA TELA" — um controle, um número

Existiam **dois** mecanismos para a mesma pergunta ("quantos quadros por segundo eu vejo"), em
lugares diferentes do painel, e nenhum deles é assunto de quem usa: o **teto** (conta tempo,
corta o que passa, morava atrás de "Mais opções") e os **quadros projetados** (inventam quadro
entre duas chegadas). Agora há um número só, na primeira vista:

```
FLUIDEZ DO DLSS 5 NA TELA
[ − ]   150   [ + ]                    [ Destravar ]
jogo 60/s · película 58/s
```

- grava na chave de sempre, `[ultima] fps_teto_pelicula`; passo de 5, e de 10 acima de 120;
  mínimo 30, máximo 240 (**não** o refresh — esta árvore já mediu 97,5 fps de película num
  monitor de 60 Hz); segurar o botão repete (420 ms para o primeiro repique, 110 ms depois);
- **abaixo** do que o jogo entrega, o teto segura; **acima**, o app liga sozinho os quadros
  projetados para alcançar. A razão usa **piso** e não arredondamento: com a chegada oscilando
  entre 60 e 61, `150/60` arredondava para 3 e `150/61` para 2, e o mecanismo trocava de razão
  a cada segundo;
- "Destravar" tira o limite (mostra `∞`) e a película volta a acompanhar o jogo;
- `[ultima] fluidez` continua como sobrescrita avançada, e **só vale se for maior que 1**:
  `fluidez=1` é o padrão escrito por extenso, não um pedido de calar o automático.

**Medido:** pedindo 150 com o jogo a 61 → película **86–90/s**, 25–29 projetados/s, estável
(o `ValePena` corta em 95% do refresh de 100 Hz, que é o teto certo). O teto duplicado saiu de
"Mais opções": dois controles para a mesma chave em telas diferentes são a receita do "mexi e
não mudou nada" que este relatório passou o dia inteiro consertando.

### Mais dois pedidos que chegaram pelo backlog e entraram junto

- **A capivara em todos os menus**, discreta: `Hud::DesenharMarca()` desenha o ícone do próprio
  exe (recurso 1, com o tamanho exato pedido ao `LoadImageW` — o `.ico` tem nove, e pedir o
  tamanho certo escolhe a *imagem* certa em vez de encolher a de 256). Entrou no canto do
  cabeçalho do painel, da janela do perfil e da janela de Efeitos — antes do número da versão,
  nunca no meio do conteúdo.
- **O rótulo virou "DLSS FRAME SKIPPING"**, igual nos dois idiomas, e a explicação (que é o
  pulo de quadros da rede neural do DLSS 5) ficou **só na dica**. Os cartazes do clique
  acompanham.

### Fotos

- `requiem-painel-primeira-vista.png` — a primeira vista completa, com as duas seções que só
  existem no modo sobreposto (`DLSS5_SHOT_SOBREPOSTO=1`, acrescentado nesta rodada — sem ele
  elas ficavam de fora de toda foto).
- `requiem-painel-en.png` — a mesma tela em inglês, com "DLSS FRAME SKIPPING" igual nos dois
  idiomas, como pedido.
- `requiem-perfil-da-imagem.png` — a janela nova (`--perfil-shot`, acrescentado nesta rodada,
  porque a janela é layered e sai preta em qualquer print comum).
- `requiem-painel-mais-opcoes.png` — com a coluna "Mais opções" aberta.

---

## Uma seção só de pulo de quadro, e os PASSES de volta

Três correções pedidas depois de ver a primeira foto:

1. **Havia duas seções com os mesmos chips** ("QUADROS QUE O FILTRO PROCESSA" e "DLSS FRAME
   SKIPPING"). A diferença era só de mecanismo — e mecanismo não é assunto de quem joga:
   - *pulo* (`[ultima] pulo_quadros`): a rede processa 1 de cada N capturados e a camada
     **repete a nossa imagem** nos outros (judder, mas funciona em qualquer modo);
   - *intermitente* (`alterna_quadros`): a rede processa 1 de cada N e o app **sintetiza** os
     outros pelo movimento (melhor resultado; precisa do modo sobreposto).

   Agora é **uma seção** ("DLSS FRAME SKIPPING") e o handler escolhe o mecanismo: sobreposto
   usa a intermitente com síntese, espelho usa o pulo. As duas chaves continuam no ini — um ini
   antigo com `pulo_quadros=2` acende o chip certo (o painel mostra a maior das duas) — e ligar
   uma **desliga** a outra, porque as duas juntas se multiplicam (1 de cada 2 nos dois = 1 de
   cada 4, que ninguém pediu).
2. **PASSES (1–10) voltou para a primeira vista.** Foi correção de leitura minha: os passes não
   são ajuste fino como MODELO e INTENSIDADE — são o que o usuário chama de "nível do DLSS", o
   controle que se mexe no meio da partida. A janela separada era para o modelo e as
   intensidades, não para eles.
3. A linha "a 100% a rede processa 100% da área — cerca de **0%** menos custo" só aparece
   quando há economia para anunciar.

## O padrão de fábrica da fluidez é 60

Era "sem limite", e isso era coerente enquanto o controle se chamava **teto** — um teto de
fábrica seguraria a imagem de quem nunca pediu nada. Como campo de **fluidez** o número mudou
de significado: ele é o alvo, e um alvo precisa nascer em algum lugar.

Vale **também com duas placas**, e isso é uma mudança deliberada: enquanto era teto, a exceção
tinha um motivo medido (com o filtro numa placa só dele, segurar a imagem não devolve nada para
o jogo), mas a máquina do usuário é justamente uma de duas placas — manter a exceção faria ele
nunca ver o 60 que pediu. O `[+]` e o "Destravar" estão a um clique, e a linha de status mostra
o que está acontecendo.

Só vale para a **chave ausente**: quem já escolheu — inclusive quem escolheu "Destravar", que
grava 0 — continua com a escolha, e nada é gravado no arranque.

## O "HABILITADO" nas duas direções

No log do usuário havia `neural: HookPoint=2 -> 0` seguido de `DLSS 5 HABILITADO` — o app
anunciando que ligou no mesmo instante em que desligava.

Causa: `AlternarTudo` decide *ligar* ou *desligar* pelo `g_hud.neural` (que sai do ReShade.log),
enquanto a função que executa era um **toggle** sobre o `HookPoint` real. Com o log poluído
(ver B-bis), o primeiro dizia "está desligado" e o segundo via `2` (ligado) — e inverter a
partir dali **desligava**, com o cartaz dizendo o contrário.

Correção: `DefinirNeural(bool)` / `AplicarNeuralRenoDxNovo(alvo)`. Quem decide "ligar" agora
manda **ligar**, não "inverta o que estiver aí"; se já estiver no estado pedido, não faz nada e
diz isso no log. O toggle continua existindo para o F6 e para os caminhos antigos, em que o
pedido e o estado vêm da mesma fonte e não podem divergir.

## Voltar ao seletor: o botão e a janela que fecha

Dois pedidos com a mesma resposta técnica. A cadeia NGX não sobrevive a uma troca de fonte com
o device vivo (ver `Renderer::FonteMudou`), então não há "desmontar e remontar" dentro da
sessão: o app **se relança sem fonte na linha de comando**, e um app sem fonte abre exatamente
no seletor — a tela anterior de quem abriu na mão.

- **Botão "Trocar fonte ou placa"** (`◀`, primeira vista, acima de Abrir o ReShade / Sair).
  Medido: clique às 07:35:47 → cartaz → `reabrindo o app (trocar fonte ou placa)` às 07:35:48 →
  `seletor aberto em 485,135 950x750 escala=1.25 … itens=20` às 07:35:49, com `placa: do ini`
  preservada.
- **A janela capturada fechou** já reabria no seletor; o que faltava era a pessoa entender.
  Medido com `WM_CLOSE` no alvo: `a janela capturada foi DESTRUIDA` às 07:36:38 → cartaz "A
  JANELA FECHOU" → reabertura às 07:36:40 → seletor.

**A espera de 1,5 s é o recurso, não desperdício:** o cartaz é uma janela deste processo e morre
com ele. Sem a espera (`PumpMessages`, que é o que deixa o cartaz ser pintado — um `Sleep`
deixaria a janela em branco), a tela apagava sem explicação, que é exatamente o que o pedido
mandou evitar.

> **Uma limitação honesta — RESOLVIDA às 08:02 do mesmo dia, ver abaixo:** o seletor voltava com a
> fonte que está **no ini**. Numa sessão normal é a mesma que está rodando, porque o seletor grava
> a escolha. Mas numa sessão aberta por linha de comando (`--source window …`, que é como a bancada
> e os atalhos do usuário funcionam) a linha de comando não grava no ini — e aí o seletor abria com
> a fonte anterior, não com a da sessão. Medido: sessão em `--source window`, seletor voltou em
> `fonte=monitor`.

### A fonte de agora viaja por argumento (`--pre-*`)

`ArgsDeReabertura`, no caminho **sem fonte**, passou a levar o que está valendo agora:
`--pre-fonte janela|monitor|camera` mais `--pre-janela` / `--pre-monitor` /
`--pre-dispositivo` + `--pre-modo`, e `--placa` **sempre** (antes a placa só ia junto quando havia
fonte, então "Trocar fonte ou placa" voltava na placa do ini).

São valores de **pré-seleção, não de abertura**: nenhum deles liga o `fromCli`. Se ligasse, a cópia
nova abriria direto na fonte e o botão deixaria de fazer o que promete, que é voltar para a tela de
escolha. Sem nenhum `--pre-*` nada muda e o ini continua mandando, como sempre foi.

Por que o ini não bastava, dito com precisão: o bloco que grava `fonte` / `janela` / `monitor` /
`dispositivo` / `modo` vive **dentro do `if (!fromCli)`** do `wWinMain`. Quem abriu por atalho de
jogo nunca grava essas chaves, e o seletor mostrava a última abertura feita à mão — podia ser outro
jogo, de outro dia.

**Prova** (o alvo da bancada fechou sozinho e disparou o mesmo caminho do botão):

```
[08:02:20.494] reabrindo o app (a janela capturada fechou): ...\DLSS5ForAll.exe --reabrindo 0
               --esperar-pid 24524 --pre-fonte janela
               --pre-janela "ZEBRA7  -  45.8 fps  -  8.0 px/quadro  -  60 quadros"
               --placa "NVIDIA GeForce RTX 4080"
[08:02:20.956] seletor: pre-selecao pela linha de comando (fonte=janela janela='ZEBRA7 ...'
               monitor='\\.\DISPLAY2 (principal)  1920x1080' ...)
```

**Limitação que fica:** a janela casa por **título**, porque é por ele que o seletor reencontra o
item na lista. Título que muda sozinho (mapa, pontuação, fps no título) pode não casar, e aí o
seletor abre no primeiro item — exatamente o que o ini já fazia, nem melhor nem pior.

## Modo comparativo (metade da tela sem DLSS 5)

Para gravar vídeo de comparação: metade esquerda com a imagem **original**, metade direita com o
**DLSS 5**, no **mesmo quadro** — o que apertar F6 e cortar o vídeo nunca dá.

**Onde entra, e por que exatamente ali** (`addon\comparar_lado_a_lado.h`): `reshade_begin_effects`
guarda uma cópia inteira do back buffer (ali ele ainda é a imagem nativa) e `reshade_finish_effects`
devolve a metade esquerda. O finish é o ponto certo, e isso foi conferido: o Neural Rendering do
RenoDX **não é uma técnica** — ele intercepta `NVSDK_NGX_*_EvaluateFeature` com
`replace_source=true`, ou seja, roda **dentro** da chamada NGX do nosso `DLSS5_Feed`, que é uma
técnica. Quando o finish acontece, tudo já passou: SMAA, Lumenite_Kernel, o Feed com o NR dentro
dele e o Sharpen.

Custo: duas cópias de GPU por quadro (uma inteira, uma de meia tela), só com o modo ligado.
Chaves em `[comparar]` do `dlss5forall.ini`: `ligado`, `divisor` (5 a 95, padrão 50) e `rotulos`.
No painel fica em **Mais opções**, porque é ferramenta de quem grava — um controle que parte a
tela ao meio seria o mais fácil de ligar sem querer na primeira vista.

**Duas limitações, medidas e não contornadas:**

1. **No quadro sintetizado do DLSS Frame Skipping os dois lados ficam iguais.** Ali a cadeia
   inteira está suprimida: não existe "com DLSS" e "sem DLSS" naquele quadro, existe um quadro
   desenhado por nós. O add-on diz isso no log e o cartaz do painel pede para desligar o frame
   skipping antes de gravar.
2. ~~Com a cena em movimento rápido, a emenda desalinha em ~1 quadro.~~ **ERRADO, e a medição
   está abaixo.** Eu tinha escrito que "o lado direito mostra o quadro processado, que sai do
   pipeline um quadro depois", a partir de **uma** foto que saltava na emenda
   (`requiem-comparativo.png`). Era hipótese, não medição — e a medição desmentiu. Ver
   **A compensação de atraso, e por que ela é zero**.

Nas duas fotos a diferença que o filtro faz aparece: à esquerda a grade fina do alvo chega nítida;
à direita ela é suavizada pela rede.

### A compensação de atraso, e por que ela é zero

A pergunta era: o lado direito está atrasado em relação ao esquerdo? Implementei a compensação
(roda de cópias do original; o lado esquerdo recua N quadros) e **medi antes de decidir o padrão**.

**Método.** Bateria de 45 fotos: três condições (parado, movimento a 8 px/quadro, movimento com
DLSS Frame Skipping em 1 de cada 2) × três valores de `[comparar] atraso` (0, 1, 2) × cinco fotos.
A métrica é o **degrau na emenda dividido pelo degrau típico da própria imagem** no mesmo passo de
3 px: `R = média_y |lum(corte-1,y) − lum(corte+2,y)| ÷ média_x,y |lum(x,y) − lum(x+3,y)|`. `R ≈ 1`
quer dizer "a emenda é tão suave quanto qualquer outro lugar da imagem"; `R` alto quer dizer
degrau. A razão existe porque o valor absoluto depende da cena — dividir pela própria imagem torna
a medida comparável entre rodadas.

**Resultado (mediana de 5):**

| condição | atraso 0 | atraso 1 | atraso 2 |
|---|---|---|---|
| parado | 1,00 | 0,99 | 1,00 |
| movimento 8 px/quadro | **0,87** | 1,78 | 2,31 |
| movimento + skipping 1 de 2 | **0,40** | 2,97 | 2,23 |

**Sem compensação já está alinhado, e compensar piora** — na medida exata de um quadro de
deslocamento. O begin e o finish são o mesmo Present e a cadeia não defasa nada. Padrão: `atraso=0`.

**Então o que era o salto que eu vi?** Rasgo da **captura**, não do pipeline. Se fosse defasagem,
apareceria nas cinco fotos da mesma condição; apareceu em uma. Nas seis fotos do build final,
cinco deram R = 0,90 a 1,11 e uma deu 3,03. A foto que me enganou tinha, além da emenda, uma
**segunda costura no meio da metade direita** — que a divisão ao meio não explica, e o
`PrintWindow` pegando a janela no meio de uma troca de quadro explica.

Na foto do build final (`requiem-comparativo-rotulos.png`) a diagonal atravessa a linha divisória
sem quebra, com o alvo a 8 px/quadro.

O método que NÃO deu certo, registrado para ninguém repetir: medir o deslocamento em pixels pela
fase das linhas verticais de 37 px da textura do alvo. A cadeia borra linhas de 1 px — a amplitude
ficou em ~2/255 e o resíduo do ajuste em 3 a 4 px, ruído maior que os 8 px procurados.

### Os selos ORIGINAL e DLSS 5 FOR ALL

As artes vieram do usuário em `amd\marca\comparar-*-original-do-usuario.png`, as duas 1983x793 e
**24bpp — RGB sem alfa**. Foram normalizadas com o mesmo enquadramento: recorte pela **borda externa
do brilho** da moldura (perfil por linha e por coluna, um pixel conta como brilho quando se afasta
mais de 55/255 da cor do fundo, e a linha conta quando 0,4% dela acende) mais **12 px** de margem,
e reamostragem bicúbica para **256 px de altura** — `comparar-original.png` ficou 728x256 e
`comparar-dlss5.png`, 640x256. As `-original-do-usuario` **não vão para o pacote**: são a fonte.

O desenho está em `addon\rotulos_comparar.h`: WIC lê o PNG **já na altura final**, o pixel vai para
uma textura com o **formato exato do back buffer** e uma única `CopySubresourceRegion` põe cada
selo no alto da sua metade. A escala é na CPU e acontece uma vez por resolução —
`CopySubresourceRegion` não escala. As duas saem com a **mesma altura**: se alguma não couber na
sua metade, ambas encolhem pelo mesmo fator e são recarregadas.

A altura é `[comparar] rotulo_altura`, em porcento da altura da tela (5 a 15), **padrão 9**;
margem de 1,5%. Começou fixa em 7% e ficou pequena para vídeo — a 1080p o selo tinha 75 px e num
corte de YouTube a palavra sumia. Medido na bancada (alvo 1280x720), antes e depois:
`marcas de marca\*.png, 142x50 e 125x50 px` a 7%; `182x64 e 160x64 px` a 9%
(`requiem-comparativo-rotulos.png`).

**Fallback em texto, que existe e foi testado:** renomeei os dois PNGs com o app vivo e forcei a
recriação pelo `[comparar] rotulos`; entrou o letreiro de reserva — cartão escuro, moldura azul de
3 px e a palavra desenhada com **GDI numa DIB** (`requiem-comparativo-rotulo-reserva.png`). GDI, e
não D2D/DirectWrite: rasterizar texto com Direct2D aqui traria dependência nova ao add-on e um
device de outra API no meio do pipeline do ReShade, por causa de dois selos.

**Duas limitações registradas:**

1. **Não há transparência valendo.** A cópia é `CopySubresourceRegion`, que não faz mistura — o
   selo entra com o fundo escuro da própria arte. Blend de verdade exigiria um quad com pixel
   shader e salvar/restaurar o estado do D3D11 no meio do pipeline do ReShade.
2. **Em back buffer HDR (10 ou 16 bits) os selos não aparecem**, porque não há conversão de 8 bits
   para esses formatos aqui. O add-on registra uma linha dizendo o formato e o motivo, uma vez.

**De quebra, um custo que estava escondido:** a risca divisória era desenhada copiando **pixel a
pixel** de uma textura 1x1 — 2 × altura chamadas de `CopySubresourceRegion` por quadro, 2.160 a
1080p, perto de um microssegundo de CPU cada. Passou a ser uma textura de 2 px pela altura da tela
e **uma** chamada.

## A capivara nas três telas que faltavam

O pedido era a marca em **todos** os menus. Estavam com ela o painel, o Perfil e os Efeitos;
faltavam o seletor, os cartazes e o botão flutuante.

- **Seletor** (`seletor_hud.h`): a capivara abre o cabeçalho, 28 px lógicos, e o título começa
  depois dela — é a primeira janela que a pessoa vê e era a única sem a marca
  (`requiem-seletor-capivara.png`).
- **Cartazes** (`ToastWndProc`): 24 px entre a barra ciano e o texto, e a caixa passou a reservar
  esse espaço. Em pixels **fixos**, não escalados por DPI, porque o cartaz inteiro é fixo — a
  fonte dele nasce com `-19` e não acompanha o DPI; escalar só o desenho o deixaria maior que a
  letra ao lado (`requiem-cartaz-capivara.png`).
- **Botão flutuante**: a pastilha **virou** a capivara, recortada em círculo (`SetClip` numa
  elipse, borda desenhada por cima), no lugar do "D5" escrito. Conferi a legibilidade antes de
  trocar, ampliando o ícone: a 24 px ainda se reconhece e a 32 fica limpa; a pastilha tem 38 px
  lógicos (57 a 150%) e o círculo útil fica em ~34, acima dos 28 que o pedido punha como piso
  (`requiem-botao-capivara.png`, os dois estados sobre xadrez).

Em todas, o desenho sai do **recurso do próprio exe** e o `LoadImageW` recebe o tamanho exato a
ser desenhado, para o `.ico` entregar a imagem certa dos nove tamanhos em vez de encolher a de
256. Faltando o recurso, nada quebra: o seletor mantém o título no lugar, o cartaz volta a
começar o texto em 18 px e a pastilha volta ao "D5".

`Hud::DesenharMarca` passou a pedir o ícone em **pixels físicos** (`px * g_scale`): o `Graphics`
dessas telas já tem a escala de DPI aplicada, então pedir 26 e desenhar 26 unidades a 150%
mandava o GDI+ **esticar** de 26 para 39. Isso melhora também o painel, o Perfil e os Efeitos.

**Duas ferramentas de bancada nasceram aqui, por necessidade:** `--botao-shot` e `--cartaz-shot`.
As duas peças têm `WDA_EXCLUDEFROMCAPTURE`, então **nenhuma captura as enxerga** — a captura de
tela vem sem elas e o `PrintWindow` com `PW_RENDERFULLCONTENT` devolve preto, porque passa pelo
DWM, que é justamente quem foi mandado escondê-las. Fotografei a região e veio a cena do jogo sem
a pastilha. Os dois shots desenham pelo **mesmo código** que vai para a tela (`DesenharEm` e
`PintarToast`, extraídos de quem os exibe), então a foto de conferência não é uma reconstrução
parecida. O `ToastWndProc` também passou a atender `WM_PRINTCLIENT`.

## Motor da rede no painel (para a 1.3, fora da release congelada)

O lado do feeder está em `RELATORIO-MOTOR-PROPRIO.md`. Este é o lado do painel.

**O interruptor** fica em Mais opções, com RenoDX como padrão. Trocar grava as duas chaves
(`DirectNeuralRenderingHookPoint` 2↔0 no `ReShade.ini` e `nr_spike` 0↔1 no `dlss5-feed.cfg`) e
reabre o app pelo caminho que já existia. **A escolha mora no nosso ini (`[ultima] motor`), e não
nas duas chaves** — o F6 desliga o neural gravando `HookPoint=0`, o mesmo valor do motor próprio,
e ler as chaves para descobrir a escolha confundiria "desliguei o neural" com "troquei de motor".
O arranque reaplica a escolha antes de os add-ons carregarem, o que também conserta o arquivo se
alguém o editar à mão. `CorrigirHookPointNoArranque` ganhou uma guarda: no motor próprio, `Off` é
a configuração certa, não um `Auto` a consertar.

**Cadeia completa, medida no binário final** (`D7F9ED53…`), do clique ao pixel:

```
14:16:55.640 motor da rede (arranque): RenoDX (padrao) - HookPoint=2 nr_spike=0
14:17:11.882 motor da rede (escolha do painel): PROPRIO (experimental) - HookPoint=0 nr_spike=1
14:17:13.395 reabrindo o app (troca do motor da rede): ... --source window --window-hwnd ... --placa "NVIDIA GeForce RTX 4080"
14:17:14.893 motor da rede (arranque): PROPRIO (experimental) - HookPoint=0 nr_spike=1
   feeder     nr: ready at 740x412 ... | nr: profile -> style=2 intensity=1.00 ... (o perfil do nosso ini)
   status     estado=ok  rede=740x412  passes=4  motor=proprio
```

E a volta, no mesmo binário: `HookPoint=2 nr_spike=0`, o feeder lendo `nr_spike=0` e não subindo
o motor próprio, e o `ReShade.log` com `DLSS-NR direct: EvaluateFeature succeeded` — **os dois
nunca ligados ao mesmo tempo**, que é o estado que derruba o `evaluate` em `0xC0000005`.

**Ao vivo, sem reabrir** (é o que o motor próprio tem de diferente):

```
14:17:44.854 [app]    tamanho da rede: 150% (nr_reduzida=1) no cfg, vale em ate 60 quadros
14:17:46.254 [feeder] nr: supersampled path ready -- network at 1110x618 (150% de 740x412)
             status   rede=1110x618
```

Os passes idem: clique em `4` → `[feed] config (neural): nr_spike=1 nr_passes=4 …` 1,1 s depois,
**sem reabertura agendada**. `AplicarAjusteNr` e `AplicarPassesNr` passaram a gravar também no
`dlss5-feed.cfg`, com o mesmo nome de chave, **nos dois motores** — é mais barato que guardar
estado sobre qual está ativo, e quem não lê a chave a ignora.

### Um controle, e não dois — com o porquê

O plano pedia chips de resolução acima de 100% **e** uma chave "Rede leve" separada. Não dá:
`RELATORIO-RESOLUCAO-200.md` diz, medido, que **`nr_escala` só age com `nr_reduzida=1`**
("nr_reduzida=0 de fábrica: nada disto liga sozinho"). Os dois controles gravariam exatamente o
mesmo par de chaves e ligar um apagaria o outro pelas costas da pessoa — o estado contraditório
que este painel não pode produzir.

Virou uma régua só, **TAMANHO DA REDE NEURAL**, com a rede leve como o degrau de baixo:
`Leve 65%` (`nr_reduzida=1`) · `100%` (`nr_reduzida=0`, o padrão) · `125/150/200%`
(`nr_reduzida=1`). O chip aceso sai das **duas** chaves juntas: com `nr_reduzida=0` a escala do
cfg não está valendo, então o chip certo é 100% mesmo que o arquivo guarde outro número — acender
150% com a rede rodando em tamanho natural seria o painel mentindo.

Com o motor RenoDX a régua fica cinza, com tooltip dizendo que requer o motor próprio.

### O que o painel afirma sobre a rede vem do feeder

`dlss5-feed.status` (chave=valor, ao lado do log) é lido uma vez por segundo e vira a linha
`motor · rede · passes` embaixo dos chips. Sem o arquivo — feeder antigo — o painel **não afirma
nada**: diz que aquela pasta não publica estado. O cartaz só sai na **mudança** de estado ou de
motivo, nunca a cada leitura.

`estado=ok` **com** motivo preenchido não é falha: quer dizer "a cadeia está filtrando, e o motor
pedido não subiu" (é o que acontece na recusa de dois motores). O cartaz desse caso diz
exatamente isso; um "parou" ali seria mentira. O texto do motivo vai **sem tradução**, como o
feeder o escreveu — inventar uma versão nossa de uma mensagem que muda com a versão dele seria
mentir com boa intenção no dia em que as duas divergissem.

Fotos: `requiem-motor-renodx-pt.png` (régua cinza), `requiem-motor-proprio-pt.png` e
`requiem-motor-proprio-en.png` (motor próprio, régua viva e a linha de estado real).

## O perfil no módulo AMD (1.3)

O achado é do `RELATORIO-AMD-OTIMIZACAO.md` §8.6: no módulo AMD o painel gravava o perfil no
bloco `[RenoDX.DLSS5]` do `ReShade.ini` — e **o RenoDX nem está naquela pasta**. Um painel que
parecia funcionar e não funcionava.

**Quais chaves o mod tem — conferido no binário, não suposto, e por duas leituras
independentes.** Eu procurei os nomes no `dlssnr_on_amd_setup.exe` (que embute o ini padrão e a
tabela de strings), em ASCII e em UTF-16; o `amd-otimizar` leu as strings do `version.dll`
v0.2.18 que veio do PC com a Radeon. As duas batem. A lista **completa** do `[DlssNrOnAmd]`:
`Enabled, Inline, InlineWaitMs, Interop, HipDevice, UseFsrInputs, UseDepth, UseAutoMask,
Temporal, LocalStructure, LocalTone, SkinStructure, ToneChannels, Tonemap, Scale`.

| controle do painel | chave do mod |
|---|---|
| TOM LOCAL | `LocalTone` |
| ESTRUTURA | `LocalStructure` |
| PELE E ROSTOS | `SkinStructure` |
| máscara automática | `UseAutoMask` |
| MODELO, INTENSIDADE, PASSES | **não existem no mod** — apagados, com o motivo |
| TOM GLOBAL | existe (`ToneChannels`) e **fica apagado assim mesmo** — ver abaixo |

**TOM GLOBAL é o caso mais delicado, e foi decisão minha deixá-lo cinza.** A chave existe, mas a
ajuda do próprio mod diz: *"Broad lighting and colour response. This DLL build gates these
channels off; raising it mostly darkens the frame."* Ligar um controle que o autor documenta como
desligado neste build — e cujo efeito colateral é escurecer a imagem — seria vender um ajuste que
não ajusta. O tooltip traz a frase dele. (`Tonemap` é outra coisa: a curva de tonemapping da
entrada, `-1` = automático; não é intensidade.)

**A faixa de valores: o que é fato e o que não é.**

- **FATO:** `-1` quer dizer **auto**, e não "não mexer" — a ajuda do `SkinStructure` diz *"auto
  follows Structure intensity; 0 disables the skin channel"*.
- **FATO:** `LocalStructure` traz *"(default 1)"* na ajuda.
- **NÃO CONFIRMADO:** o limite superior. Nenhuma string do binário o nomeia — quem o mostra é o
  painel do próprio mod (End, Left/Right). Todos os valores vindos da Radeon são inteiros
  pequenos, então 0..2 é o palpite conservador. Muda-se **uma linha** se aparecer outro, e o log
  grava o par (nosso valor → valor do mod) para isso ser conferível.
  **O 0..2 é NOSSO, e não do mod** — o único número com respaldo é o "(default 1)" da ajuda do
  `LocalStructure`. Se o limite real for maior, o painel entrega **menos** alcance do que existe,
  que é o erro certo a cometer; a primeira sessão em hardware real resolve pelo par no log.

**Uma comparação que engana, registrada para ninguém repetir:** o `nr_estrutura_local=2` do ini
do outro PC é a **nossa** escala, gravada por nós; o `LocalStructure=1` é a do mod. Os dois
números não se correspondem.

**Dois escritores no mesmo arquivo:** o mod também grava o ini dele pelo overlay (o binário tem
`cannot write %s (settings will not persist)`). Por isso a gravação usa
`WritePrivateProfileStringW`, que troca **uma** chave e devolve o resto como estava — inclusive as
quebras CRLF do arquivo do mod, conferido nos bytes: 16 CR para 16 LF antes e depois.

**"Vale ao vivo?" — só duas chaves têm prova**, e nenhuma delas é nossa: o binário tem
`ini changed Inline=%d Interop=%d; re-creating staging`, e a string nomeia só essas duas. Por
isso o painel reabre o app ao mexer no perfil, que é o que não erra.

E há um **segundo motivo, que reforça o primeiro**: repare no fim daquela string — a releitura do
mod **refaz o staging**. Mesmo onde funcionasse ao vivo, a pessoa veria um repique na imagem.
Reabrir é mais honesto do que prometer instantâneo e entregar um piscar.

**A corrida entre os dois escritores continua de pé, e isso está registrado de propósito:** o
`WritePrivateProfileStringW` preserva o resto do arquivo, mas não impede que o mod reescreva o
ini inteiro por cima logo depois, se a pessoa estiver com o **overlay dele aberto (End)** na hora.
Não é para tratar agora — é para ninguém estranhar um valor "voltando sozinho" num teste feito com
aquele overlay aberto.

**Prova, sem placa AMD, por arquivo e log.** Pasta isolada, `--amd`, `[ultima]` com
`nr_tom_local=1`, `nr_estrutura_local=0.5`, `nr_estrutura_pele=0.25`, `nr_mascara_auto=1`:

```
[app]  perfil (modulo AMD): nr_tom_local = 1.00 -> [DlssNrOnAmd] LocalTone=2 (gravado)
       perfil (modulo AMD): nr_estrutura_local = 0.50 -> [DlssNrOnAmd] LocalStructure=1 (gravado)
       perfil (modulo AMD): nr_estrutura_pele = 0.25 -> [DlssNrOnAmd] SkinStructure=1 (gravado)
       perfil (modulo AMD): nr_mascara_auto = 1.00 -> [DlssNrOnAmd] UseAutoMask=1 (gravado)
dlssnr_on_amd.ini  LocalTone=2  LocalStructure=1  SkinStructure=1  UseAutoMask=1   (era 0 / 1 / -1 / 1)
ReShade.ini        NEM FOI CRIADO
```

Essa última linha é metade da correção: o bloco do arranque que mantinha o `[RenoDX.DLSS5]` em
dia **passou a não rodar no módulo AMD**. Antes ele escrevia lá o perfil inteiro em toda
abertura, para um leitor que não existe naquela pasta.

Mais três consequências no painel: `nrPode` no módulo AMD passou a depender do **ini do mod**
(antes era sempre falso e o cartão do perfil nem aparecia); o cartão-resumo vira
`Tom local 100% · Estrutura 50% · mod AMD` em vez de falar de modelo e passes; e "Mais opções"
ganha o cartão **AMD (experimental) — mod DLSS-NR-on-AMD** dizendo onde os ajustes vão parar.

Fotos: `requiem-amd-painel-pt.png` / `-en.png` e `requiem-amd-perfil-pt.png` / `-en.png`.

**O que continua sem prova e está marcado como tal:** ninguém rodou isto numa Radeon — nem eu nem
o `amd-otimizar`, que também só tem uma RTX 4080 aqui. O limite superior da escala do mod segue
não confirmado.

**Duas coisas que o `amd-otimizar` trouxe e que ficam anotadas, não implementadas:**

1. O que de fato move o custo no modo AMD não é nenhum ajuste de imagem — é o **tamanho**: 13,5 ms
   por quadro só de rede a 634x669 numa RX 9060 XT. O painel passou a dizer isso no rótulo:
   *"RESOLUÇÃO DO DLSS — é aqui que se ganha fps no modo AMD"* — **sem citar o número**, que é de
   outra máquina.
   **Crédito, corrigido a pedido dele:** esse número **não é medição do `amd-otimizar`**. É da
   sessão do outro PC (RX 9060 XT, mod v0.2.18); ele apenas leu o handoff. E é **vídeo na tela,
   não jogo** — onde o número aparece nos documentos, aparece com essa ressalva.
2. `InlineWaitMs` (do mod v0.2.18 em diante) é o teto de quanto um quadro espera pela rede; ao
   estourar, o mod mostra o retoque do quadro anterior em vez de segurar a imagem. No PC da
   Radeon ficou em 25, vindo de 200. **É a chave que mais muda o que a pessoa sente** — e o
   líder mandou fazer, então virou controle (abaixo).

### ORÇAMENTO DE ESPERA POR QUADRO (`InlineWaitMs`)

`[−] 25 ms [+]`, passo de 5, faixa 5 a 200, no cartão do motor AMD em Mais opções. Vale por
reabertura, como o resto do perfil AMD.

**A detecção não é por número de versão, e sim pela chave.** O que importa não é "qual versão
está instalada" mas "este binário lê esta chave?". Comparar versão exigiria um `VERSIONINFO` que
o mod não tem — conferido: o instalador dele vem com os campos **vazios** — e ainda erraria num
build intermediário. Então: se a chave já está no `dlssnr_on_amd.ini`, conta (só um instalador
que a conhece a escreveria); senão, procura-se o nome **dentro do `version.dll`**, uma vez por
sessão, em blocos com sobreposição. Sem o `version.dll` na pasta, o controle fica cinza.

**Um defeito meu que o teste pegou, e que teria passado:** `GetPrivateProfileIntW` devolve
`UINT`. Com o padrão `-1`, a ausência da chave volta como `0xFFFFFFFF`, e `>= 0` num unsigned é
**sempre verdadeiro** — o controle aparecia habilitado em qualquer pasta, inclusive numa sem a
chave e sem o `version.dll`. Foi exatamente o que o primeiro teste mostrou, e o cast para `int`
resolveu. Os dois casos, depois da correção:

```
[sem a chave e sem version.dll]  modulo AMD: version.dll nao esta na pasta - InlineWaitMs fica indisponivel
[com a chave no ini]             modulo AMD: InlineWaitMs ja esta no dlssnr_on_amd.ini - o mod instalado a conhece
```

**Prova do controle, com o painel vivo** (bancada em `--amd`, clique no `[−]`):

```
14:49:54.256  modulo AMD: orcamento de espera = 20 ms -> [DlssNrOnAmd] InlineWaitMs=20 (gravado)
14:49:56.764  perfil da rede: ORÇAMENTO DE ESPERA mudou ha 2500 ms - reabrindo o app ... (geracao 1)
14:49:56.767  reabrindo o app (o perfil da rede mudou): ... --source window --window-hwnd ... --amd --placa "..."
```

O arquivo saiu de `InlineWaitMs=25` para `20`, e a reabertura preservou o `--amd`, o alvo e a
placa. O `[−]` apaga no piso e o `[+]` no teto: um botão que não pode mais andar tem de parecer
que não pode, senão a pessoa clica e conclui que travou.

## `nvngx_dlss.dll` entrou na conferência

`VerFaltaDlss()` conferia o `renodx-dlss5.addon64` e o `nvngx_dlssnr.dll`, e **não** o
`nvngx_dlss.dll` — que é outro arquivo e outra falta. O modo de falha era o pior possível: sem
ele a sessão NGX nem abre, o feeder cai na passagem segura e a tela fica **idêntica à original**,
sem erro nenhum — exatamente o sintoma que essa faixa existe para explicar. Quem tivesse o neural
no lugar e este não via "está tudo aqui" e uma tela sem efeito.

O texto da faixa passou a montar a lista como o caminho do módulo AMD já fazia (escrever as
combinações à mão já era limite com dois nomes e viraria oito frases com três), e com **três**
nomes a lista vai em linha própria — juntos eles passam dos ~80 caracteres que cabem por linha, e
o STATIC quebraria no meio de um nome de arquivo, empurrando para fora da faixa justamente a
frase que diz onde baixar.

Provado nos dois casos, com a pasta montada para isso (`requiem-faixa-falta-nvngx-dlss.png`,
`requiem-faixa-falta-tres.png`):

```
seletor: aviso do DLSS 5 mostrado (addon=0 dlssnr=0 dlss=1 reshade=0 mod-amd faltando=0)
   -> "O DLSS 5 não vai ligar: falta nvngx_dlss.dll aqui."
seletor: aviso do DLSS 5 mostrado (addon=1 dlssnr=1 dlss=1 reshade=0 mod-amd faltando=0)
   -> "O DLSS 5 não vai ligar. Faltam aqui:
       renodx-dlss5.addon64, nvngx_dlssnr.dll e nvngx_dlss.dll."
```

O `--seletor-shot` esconde a faixa de propósito (a máquina que fotografa quase sempre tem tudo no
lugar). `DLSS5_SELETOR_SHOT_FALTA=1` liga, e existe por um motivo concreto: é o único jeito de
conferir numa imagem o texto que a pessoa vai ler.

### Três comentários a acrescentar no código quando a 1.3 voltar

A 1.3 está **parada** aguardando a publicação da 1.2, e recompilar por comentário é o retrabalho
que a pausa existe para evitar. Então isto fica anotado aqui, e entra no `main.cpp` na volta:

1. No bloco "O PERFIL NO MODULO AMD": dizer que o **0..2 é nosso**, não do mod, e que errar para
   menos é o lado certo de errar.
2. Em `AplicarAjusteNr` / no bloco do motor: o **segundo motivo** da reabertura — a releitura do
   mod refaz o staging, então "ao vivo" daria um repique.
3. Junto da nota dos dois escritores: a preservação do arquivo **não resolve a corrida** com o
   overlay do mod aberto.

Os três são precisão de comentário; nenhum muda comportamento. Os textos visíveis ao usuário já
foram corrigidos (não exigem build).

## Fluidez fracionária, composição com o skipping e o que mais caiu junto (1.3)

**O defeito da auditoria.** A razão de projeção era `floor(alvo / chegada)`. Com o jogo a 61,
**qualquer** alvo entre 62 e 121 dava 1 — ou seja, **zero** quadros projetados. A pessoa mexia no
número, via o número mudar e a tela não mudava. Agora a razão é fracionária e um acumulador
(Bresenham) reparte a fração entre as chegadas: 90/61 = 1,48 vira um extra sim, outro não.

**Medido com a GPU livre** (alvo sintético a 8 px/quadro, chegada ~61, 10 amostras por ponto):

| pedido | película | erro | razão calculada |
|---|---|---|---|
| 70 | 61,7 | −12% | 1,18 |
| 90 | 69,8 | −22% | 1,49 |
| 120 | 73,1 | −39% | 1,56 (limitada pelo teto útil da tela, 94) |
| **Destravado** | **73,5** | — | 1,56 · log: *"alvo 95 - destravado, o maximo da tela"* |

**Não bati os ±5% pedidos, e o líder aceitou assim.** O que falta é custo de apresentação: cada
extra é warp + Present, e nem todos cabem na fatia entre duas chegadas. O tooltip e os três
documentos passaram a dizer isso com todas as letras — *abaixo do fps do jogo o número é exato;
acima, é experimental e a entrega é parcial* — com os números medidos dentro do próprio texto.

**Quatro defeitos estruturais apareceram no caminho:**

1. **O mesmo número era alvo e teto.** "Fluidez 90" pedia 90 projetando *e* segurava os presents
   em 90, em cadência regular. Medido: pedido 70 com chegada 60 dava **57** — abaixo da própria
   chegada. Com a projeção ativa, quem limita passou a ser a tela (95% do refresh).
2. **O teto barrava o próprio quadro projetado.** O extra tem hora marcada (metade do intervalo de
   chegada); o teto tem cadência regular. O extra ficava segurado 2 ms, a chegada seguinte vinha e
   o intervalo era reprogramado — um em cada quatro sobrava. Pedido 90 entregava 8 extras de 30.
3. **"Destravar" desligava a projeção.** O cálculo exigia `tetoFps > 0`, e Destravar grava 0: a
   película caía para a própria chegada. Destravado quer dizer "sem número escolhido", e o alvo
   passou a ser o máximo útil da tela. **Provado:** com a GPU livre e chegada 61, destravado
   entregou **73,5** — antes entregaria 61.
4. **O `[−]` morria depois de Destravar** (`enabled = !semTeto`), sem saída a não ser reabrir o
   app. Agora ele sai do ∞ direto para o último número escolhido (guardado em
   `[ultima] fps_teto_anterior`), ou para o refresh − 5. **Provado:** com `fps_teto_anterior=85`
   e a fluidez destravada, o item aparece `ativo=1` e o clique registra
   *"saiu do destravado para 85 fps (o ultimo escolhido)"*.

**E o recuo por falta de folga deixou de desligar tudo.** Com razão inteira, "recuar" só podia ser
voltar de 2x para 1x; com razão fracionária existe meio-termo, e uma máquina que aguenta 1,4 mas
não 2,0 recebia 1,0. Cada recuo agora tira 20% do teto da sessão; só abaixo de 1,05 o recurso sai
de cena.

### O contador da película não contava o quadro sintetizado

`presented` alimenta o "película N/s" do painel, a linha LAT e a escada de desempenho. O quadro
**projetado** da Fluidez entrava; o **sintetizado** do DLSS Frame Skipping, que vai para a tela
pelo mesmo `ApresentarSemDesenhar`, **não** — então com o skipping ligado o painel mostrava metade
do que a pessoa estava vendo, e qualquer medição de composição saía errada pela metade. Corrigido,
e a linha de status agora diz `N projetados + os sintetizados do skipping` quando os dois estão
ligados.

### A composição com o frame skipping

Com **skipping 1 de 2 + Fluidez 120**, alvo em movimento: a rede processou **~20/s** e a película
saiu a **74–80/s**. Ou seja, compõe: ~20 processados + os sintetizados + 15–30 projetados.

**Prova externa** (`scratchpad\quadros-unicos.ps1`), que não usa contador nenhum do app: BitBlt de
uma faixa da tela com hash por amostra, contando mudanças. Deu **≥57,2 quadros distintos/s** —
muito acima dos ~20 processados, que é o que a composição precisava mostrar.

**O limite do método, declarado:** o amostrador satura em ~88 amostras/s (o custo é do BitBlt da
tela, não do tamanho da área: 8x8 e 200x120 dão quase o mesmo). Com menos de duas amostras por
quadro o valor medido é um **piso**, não a taxa real — e ele sobe quando a amostragem sobe (40,9 →
52,9 → 66,7 conforme a área encolhe). Calibração feita: numa área **parada** o mesmo medidor conta
**0** quadros distintos, então o método não inventa mudança. Medir a taxa real exigiria Desktop
Duplication, que não entrou nesta rodada.

**A repetição desta medida com a GPU livre não saiu, e o motivo fica registrado:** no modo
sobreposto sobre uma **janela**, a camada só aparece com o alvo **em foco** — e nesta bancada o
foco não fica nele (o `SetForegroundWindow` não rouba foco, e o clique sintético que eu mandei
para a janela do alvo atravessou a camada e caiu no **desktop**, `classe Progman` no log). Com a
camada fora de vista o medidor externo conta **0**, corretamente. A medição que vale continua
sendo a de 17:00, feita com a máquina compartilhada — e naquela condição a composição já aparece.
Para repetir isso sem depender de foco, o caminho é a fonte **monitor**, que não foi usada aqui.

### O perfil inteiro no arranque — já valia, e a prova achou outra coisa

O pedido era fazer o arranque aplicar o perfil inteiro do `[ultima]` ao preset do ReShade, e não
só os passes. **Ele já aplicava**: o bloco do arranque percorre estilo, intensidade, tom local,
tom global, estrutura, pele e máscara, grava o que divergir e usa os padrões do painel quando a
chave está ausente. Prova pedida, feita: `ReShade.ini` com `Style=1` e `[ultima]` sem `nr_estilo`
→ depois do arranque, `DirectNeuralRenderingStyle=0`, uma seção só, e `nr_estilo=0` no nosso ini
(painel em Modelo A).

**Mas a prova encontrou um modo de falha silencioso vizinho.** No meu primeiro teste eu criei o
`ReShade.ini` com **BOM UTF-8** — e o arquivo terminou com **duas** seções
`[RENODX-DLSS-preset1]`: a antiga com `Style=1` e uma nova, no fim, com `Style=0`. O
`WritePrivateProfileString` não reconhece a seção existente quando há BOM e acrescenta outra; a
leitura pega a primeira. O sintoma é exatamente *"o painel mostra uma coisa e a rede roda outra"*,
sem erro em lugar nenhum.

Não conserto o arquivo — ele é do ReShade, e a decisão de escopo estreito do `CorrigirBomDoIni`
continua valendo. O que dá para fazer sem invadir é **dizer**: o arranque agora detecta o BOM no
`ReShade.ini` e escreve no log o que está acontecendo e como sair (salvar sem BOM, ou apagar e
rodar o `setup-dlss5.ps1`). Testado nos dois sentidos: com BOM o aviso sai; sem BOM não sai nada e
o perfil é aplicado normalmente. O aviso fica **no começo** do arranque, e não junto do resto —
os caminhos de foto saem antes daquele ponto, e foi num deles que eu descobri o problema.

### O programa abre sempre ligado (decisão do usuário)

O auditor apontou "desligado não sobrevive a fechar o app" como defeito; não é, é o comportamento
escolhido. O que estava errado era a marca `[ultima] neural_off_nosso` ficar para trás: o arranque
agora a limpa **sempre** que a encontra, e não só quando o HookPoint está em Off — deixada lá, ela
faria uma abertura seguinte "consertar" um Off que ninguém mais pediu, e o card mostraria um estado
que não é o real.

Fotos: `requiem-fluidez-pt.png`, `requiem-fluidez-en.png` e `requiem-fluidez-destravado.png` (o
`[−]` vivo ao lado do ∞).

## Binários finais

**A release 1.2, congelada para publicar** (11/09/2026 13:23) — botão "Trocar fonte ou placa",
modo comparativo com selos de altura configurável, compensação de atraso medida, pré-seleção do
seletor e a capivara nas três telas que faltavam:

| arquivo | tamanho | SHA-256 |
|---|---|---|
| `amd\DLSS5ForAll.exe` | 2934875 | `C13671EE11316907BD872A847E4744B577F52F9AA967AD032A8AA33C9C1C8A34` |
| `amd\dlss5-bridge.addon64` | 1019764 | `7B79BDF627FF2C3CE0F95CC0B97EA69383F6DD5DD1FBC2DE3A8B4AE32C83C5B5` |

**O que está em `amd\` agora (a 1.3 em andamento, NÃO promovida)**, 11/09/2026 14:40 — o motor da
rede, o perfil do módulo AMD e o `nvngx_dlss.dll` na conferência. O `VERSIONINFO` continua em
**1.2.0.0**: o número é do `launcher-profissional` e eu não o mexo sem ordem.

**Inclui a correção do `DLSS5_DUMP` gravada pelo agente `um-passe`** (com o dump armado, a
supressão da cadeia para nos 4 quadros anteriores). Conferido antes do build: as duas mudanças
coexistem, nada colidiu, e o `!dumpPerto &&` dele segue dentro do `Sintetiza::Suprimir` que a
Fluidez encosta.

| arquivo | tamanho | SHA-256 |
|---|---|---|
| `amd\DLSS5ForAll.exe` | 2971118 | `618E076F53CD11520B2A65AFC22FA194A254F3FCCA5A61A804EC817DD58A5E18` |
| `amd\dlss5-bridge.addon64` | 1019764 | `DEDFD265C786247B839DB697EFC36BE97FA41565E2C7F5E94E88290F17D856E1` |
| `amd\feeder-amd\build\dlss5-feed-amd.addon64` | 1001841 | (do `residual-nr`, 13:24 — usado na bancada) |

As provas do **motor da rede** (troca, volta e tamanho ao vivo) foram feitas no exe
`D7F9ED53…`, de 14:16; as do **módulo AMD** e da **faixa de arquivos**, neste. Entre um e outro
mudaram só as partes provadas depois, e nenhuma toca no motor.

O add-on saltou de 723 KB para 1,02 MB: WIC e GDI entraram nele (`-lgdi32 -luser32 -lole32` no
`build-addon.sh`), e o build é estático.

São os **mesmos bytes** que rodaram na bancada: `bin-teste-requiem\DLSS5ForAll.exe` e
`bin-teste-requiem\dlss5-bridge.addon64` conferem com os hashes acima. Todas as provas desta seção
foram refeitas neles.

### O que mudou desde o que está hoje na `bin\` (`60DB213B…` / `35C39EF5…`, promovidos às 06:45)

| onde | mudança |
|---|---|
| exe | botão **Trocar fonte ou placa** (`E_SELETOR` / `CMD_SELETOR`) ao lado do Sair |
| exe | volta automática ao seletor quando a janela capturada é **destruída** |
| exe | chip do **modo comparativo** em Mais opções, gravando `[comparar] ligado` |
| exe | **pré-seleção do seletor** por `--pre-*` e `--placa` sempre na relançada sem fonte |
| exe | versão **1.2** (`versao.h`, `app.rc`, `launcher.rc` — mexidos pelo launcher-profissional) |
| exe | capivara no **seletor**, nos **cartazes** e no **botão flutuante** (a pastilha virou ela) |
| exe | `Hud::DesenharMarca` pede o ícone em pixels físicos: nada de esticar em DPI alto |
| exe | bancada: `--botao-shot` e `--cartaz-shot` (as duas peças somem de qualquer captura) |
| add-on | `comparar_lado_a_lado.h`: o modo comparativo inteiro |
| add-on | `rotulos_comparar.h`: os selos, o fallback em texto e a conversão de formato |
| add-on | `[comparar] rotulo_altura` (5 a 15%, padrão 9) e `[comparar] atraso` (padrão 0, medido) |
| add-on | risca divisória de 2.160 cópias por quadro para **uma** |
| pacote | `empacotar.ps1` leva `marca\comparar-original.png` e `marca\comparar-dlss5.png` |

## O que precisa ir para `bin\` (eu NÃO levei)

Com o app do usuário **fechado**, e pelo método "remover antes de copiar" do
`RELATORIO-MIGRACAO-BIN.md` (por causa dos hardlinks):

| arquivo | de onde | por quê |
|---|---|---|
| `DLSS5ForAll.exe` | `amd\build-mingw.sh` | A, B, C, D, E |
| `dlss5-bridge.addon64` | `amd\addon\build-addon.sh` | o log limitado do item B(ii) |

O `dlss5-feed.addon64` **não** foi tocado nesta rodada.

## Chaves novas do `dlss5forall.ini`

| chave | seção | padrão | o que faz |
|---|---|---|---|
| `fluidez` | `[ultima]` | 1 | 1 = igual ao jogo · 2 = dobrada · 3 = triplicada |
| `perfil_reabre` | `[ultima]` | (ausente = ligado) | `0` desliga a reabertura automática ao mudar o perfil |
| `atalho_reserva` | `[ultima]` | (ausente = ligado) | `0` desliga o hook de teclado reserva |
| `botao_flutuante` | `[ultima]` | (ausente = ligado) | `0` esconde a pastilha |
| `recursos_experimentais` | `[ultima]` | (ausente = escondido) | `1` mostra as peças da 1.3 no painel (motor da rede, tamanho da rede, orçamento de espera do AMD) |
| `botao_x` / `botao_y` | `[ultima]` | — | posição da pastilha, gravada ao arrastar |

## Interruptores de bancada (não são para o usuário)

| variável | efeito |
|---|---|
| `DLSS5_SEM_HOTKEY=1` | o app não registra hotkey nenhuma — simula o jogo que as segura |
| `--perfil-shot <png>` | grava a janela do perfil pelo mesmo código de desenho |

## Frentes que ficam abertas

1. **A causa exata do Requiem não foi isolada** — as hotkeys estavam registradas e nenhum dos
   dois processos era elevado, então sobra "o jogo segura as hotkeys", que não dá para confirmar
   de fora. O caminho reserva resolve o sintoma **independentemente da causa**, e agora a
   próxima sessão dele deixa a resposta escrita no log
   (`o WM_HOTKEY NAO chegou em 250 ms ... A janela na frente era pid N classe X`).
2. **O RenoDX relendo a config** — se algum dia houver uma forma de fazê-lo reler sem reabrir o
   app, a reabertura automática vira desnecessária. Foi tentado alternar o `HookPoint` (2→0→2)
   para forçar o re-arm: não produziu `CreateFeature` novo nem mexeu no `options_revision`.
3. **Bug pequeno visto no log dele, não corrigido aqui:** alternar o DLSS 5 registra
   `DLSS 5 HABILITADO (neural estava desligado)` nas **duas** direções (2→0 e 0→2). O estado
   interno do card está dessincronizado do `HookPoint` em alguma transição.


## A UI da 1.3 escondida atrás de `recursos_experimentais` (11/09 17:25)

Decisão final do líder (17:28): **os dois**. O feeder novo (`3D6C1964…`, 13:24) vai para a
produção junto — ele aceitou o argumento de que é o mais rodado hoje e de que a guarda dos dois
motores mora nele — **e mesmo assim** a UI da 1.3 fica atrás da chave nesta release. O motivo,
portanto, deixou de ser o crash: com o feeder novo promovido, a guarda está lá. O que falta é
**medição** — o motor próprio só foi comparado em cena sintética, nunca em jogo — e o usuário
pediu para ocultar o que não está fechado. Nenhuma linha de código foi apagada.

`RecursosExperimentais()` (`main.cpp` ~4341) lê `[ultima] recursos_experimentais`; **ausente =
escondido**. A condição entra em dois pontos do painel:

| linha | o que some sem a chave |
|---|---|
| ~7166 | `MOTOR DA REDE NEURAL` (RenoDX / Próprio), a linha de estado real e a régua `TAMANHO DA REDE NEURAL` |
| ~7296 | o cartão do **módulo AMD** e o `ORÇAMENTO DE ESPERA` (`InlineWaitMs`) |

**Nuance que o líder precisa saber:** o pedido falava do "cartão AMD com orçamento de espera", e
eu escondi o **conjunto** — o cartão informativo do módulo AMD foi junto. Separar é uma linha,
se ele preferir manter o cartão visível e esconder só a régua de espera.

Nada foi removido: os chips de `RESOLUÇÃO DO DLSS`, `PASSES DA REDE`, `DLSS FRAME SKIPPING` e
`FLUIDEZ DO DLSS 5 NA TELA` continuam exatamente onde estavam.

**Provas** (binário final, exe `7FDE5BA7…` 2.971.118 B, addon `C40DD21E…` 1.019.764 B):

| foto | o que mostra |
|---|---|
| `release-sem-chave-primeira-pt.png` | primeira vista intacta: PASSES, FRAME SKIPPING, FLUIDEZ, RESOLUÇÃO DO DLSS |
| `release-sem-chave-pt.png` | "Mais opções" em pt **sem** MOTOR e **sem** TAMANHO DA REDE |
| `release-sem-chave-en.png` | o mesmo em inglês |
| `release-com-chave-pt.png` | com `recursos_experimentais=1`: as duas seções voltam, com a linha `motor: próprio · rede: 1110x618 · 4 passes` |

A chave está documentada como **experimental, volta na próxima versão** nos três documentos (`amd\README.md` na
tabela de chaves e na seção do motor, `LEIA-ME.txt` e `README.txt` na lista de chaves do ini).
O ini da cópia de teste voltou ao estado **sem** a chave depois das fotos.

## O nome do add-on na faixa "Faltam aqui" (11/09 17:45)

Achado no teste de instalação nova: a **detecção** já aceitava os dois nomes desde 02/09
(`AcharAddonRenoDX()` procura `renodx-dlss*.addon64`), mas o **texto** continuava citando
`renodx-dlss5.addon64` — o nome que o pacote do RHI não entrega mais. Quem lê a faixa procura a
string exata, então o texto mandava a pessoa atrás de um arquivo inexistente.

Corrigido em dois lugares de UI (`main.cpp`): `FaltaDlss::Nomes()` (a lista da faixa) e o detalhe
pt/en de `NR_SEM_ADDON`. Os três documentos também citavam o nome velho — trocados (4 ocorrências
no `README.md`, 2 em cada `.txt`). Fotos: `faixa-falta-addon-pt.png` ("falta renodx-dlss.addon64
aqui") e `faixa-falta-tres-pt.png` (os três nomes, com `nvngx_dlss.dll` que entrou nesta versão).
Exe `15A1E4C8…` 2.971.118 B; o bridge não mudou de código e ficou no binário já aprovado
(`C40DD21E…`).

**Fica aberto, fora do meu arquivo:** `amd\portable-launcher\atualizacao.h:119` lista
`renodx-dlss5.addon64` em `kArquivosDaPessoa` (o `/XF` do robocopy da atualização). Com o nome
atual, o add-on da pessoa **não está protegido** dessa exclusão. Um `renodx-dlss*.addon64`
resolveria, mas o arquivo é do launcher — reportado ao líder, não editado aqui.
