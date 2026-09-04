# Correções de entrada — tecla do ReShade, neural e camada travada

Três defeitos que a pessoa sentia como um só ("o app prende a tela e o neural não liga").
Todos em `main.cpp`. Nada aqui muda o pipeline de vídeo.

---

## 1. O botão "Abrir o ReShade" mandava a tecla errada

### O que estava errado

`CMD_RESHADE` chamava `PokeReShade(VK_END, true, VIA_MENSAGEM)` com a tecla **cravada no
código**. O `ReShade.ini` desta instalação diz:

```
[INPUT]
KeyOverlay=36,0,0,0      ; 36 = VK_HOME
```

End é o atalho do **nosso painel**, não do ReShade. Ou seja: o botão nunca abriu overlay
nenhum — mandava a nossa própria tecla para dentro da nossa própria janela. É exatamente a
queixa "não consigo acessar o ReShade sem apertar Home".

Pior: mesmo sem abrir nada, o código fazia `C.clickThrough = false` e deixava a camada com
o mouse **para sempre**. Daí o defeito 3.

O arquivo já tinha `TeclaOverlayReShade()` lendo o ini desde sempre, com um comentário
dizendo "nunca escrever End ou Home fixo". O painel e o guia já usavam essa função; só o
botão que de fato manda a tecla é que não usava.

### O que mudou

* `TeclaOverlayReShade(UINT* vkOut, UINT* modsOut)` passou a devolver também os
  modificadores. Quem manda a tecla precisa disso: o `PokeReShade` sintetiza a tecla **nua**,
  então "Ctrl+Home" chegaria do outro lado como "Home".
* `CMD_RESHADE` agora tem três caminhos:
  1. **sem tecla no ini** (`vk == 0`): não manda nada e explica onde se resolve
     (ReShade → Settings → Overlay key). Chutar uma tecla em nome do usuário seria pior
     que não fazer nada.
  2. **tecla com Ctrl/Shift/Alt**: entrega o teclado à camada, arma a volta automática e
     diz *qual combinação apertar*. Estender o poke para segurar modificadores é possível,
     mas são dois caminhos de entrega diferentes (PostMessage e SendInput) e cada um exige
     um jeito próprio de manter o modificador em pé pelos seis frames apresentados —
     complexidade que se paga uma vez e se quebra para sempre. A metade honesta é melhor
     que a inteira frágil.
  3. **tecla simples**: manda ela, e só fica interativo se ela foi mesmo entregue.
* Os textos que diziam "End abre o ReShade" (cartaz do modo interativo) passaram a usar
  `TeclaOverlayReShade()`; o "F6 liga/desliga o neural" virou `TextoAtalho(AT_TUDO)`, que é
  a tecla que realmente está registrada nesta máquina.

### Como testar

1. Confirme a tecla: `[INPUT] KeyOverlay` no `ReShade.ini`. Aqui é `36,0,0,0` = Home.
2. Abra o painel, clique em **Abrir o ReShade**. O overlay do ReShade tem de aparecer e o
   cartaz tem de dizer `ReShade aberto (Home)` — não "(End)".
3. Troque no `ReShade.ini` para `KeyOverlay=35,0,0,0` (End), reabra o app e repita: o badge
   do painel, o cartaz e a tecla enviada mudam juntos.
4. Apague a linha `KeyOverlay` e reabra: o botão tem de dizer que o ReShade está sem tecla
   de overlay, **sem** mandar tecla nenhuma e **sem** prender o mouse.
5. Ponha `KeyOverlay=36,1,0,0` (Ctrl+Home): o botão entrega o teclado à camada e pede que
   você aperte Ctrl+Home. Aperte — o overlay abre.

---

## 2. O neural "nem sempre ativa"

### O que estava errado

Três coisas somadas em `CMD_NEURAL`:

* **(a) Lia o resultado cedo demais.** `rsbridge::ToggleNeural()` apenas *arma* um pulso
  (leituras de `GetAsyncKeyState` que o add-on serve ao RenoDX); quem entrega é o **próximo
  frame do RenoDX**. O código lia o estado na linha seguinte à chamada — sempre antes de a
  entrega acontecer. O `reshade_bridge.h` documenta o padrão certo em `NeuralDelivered()`.
* **(b) Lia da fonte errada.** `LerEstadoNeural()` decidia ligado/desligado pelo
  `NeuralUplift` do `ReShade.ini`, que o RenoDX só grava quando o AutoSave do ReShade
  resolve — segundos depois, às vezes nunca. O próprio `AlternarTudo` já comentava isso.
* **(c) Não conferia depois.** `CMD_TUDO` agendava uma verificação de 2,5 s com poke
  corretivo (`neuralAlvo` / `verificaNeuralAte`); `CMD_NEURAL` não agendava nada.

Somados: a pessoa clicava, o painel mostrava o estado velho, o cartaz dizia "o add-on não
respondeu", ela clicava de novo, **armava um segundo pulso, e os dois se cancelavam**. Daí
"nem sempre ativa".

### O que mudou

* `LerEstadoNeural()` passou a ler o ON/OFF do **log** (`NR toggled ON/OFF`, imediato) e a
  usar o ini só como reserva. Isso mata o estado grudado: com o último toque em OFF o card
  diz DESLIGADO na hora, mesmo com o ini ainda em 1.
* `CMD_NEURAL` guarda `rsbridge::NeuralDelivered()` antes de agir, arma o toque, mostra um
  cartaz **no gerúndio** ("LIGANDO O NEURAL RENDERING…") e agenda a mesma verificação de
  2,5 s do `CMD_TUDO`. Nenhum estado é anunciado antes de ser confirmado.
* A verificação assíncrona agora serve aos dois caminhos, usa `NeuralDelivered()` para
  distinguir "o add-on não recebeu" de "recebeu e não mudou", corrige **uma** vez (com nova
  verificação, em vez de reler na hora) e mostra o resultado final.
* Duplo-clique bloqueado: enquanto há verificação pendente, `CMD_NEURAL` e `CMD_TUDO`
  recusam com "UM INSTANTE…", e o botão do card do neural fica em "aguarde…" e não aceita
  clique (`g_hud.neuralPendente`).

### Uma coisa que o pedido pedia e não foi feita como pedido

O pedido dizia para exigir que a evidência de avaliação (`inline feature 18 evaluation
succeeded`) fosse **recente** — posterior ao último toggle. Medi no `ReShade.log` desta
máquina antes de implementar: **essa linha aparece duas vezes por sessão** (`count=1` e
`count=60`) e nunca mais. Ela é marco de "a cadeia rodou", não batimento.

Exigir recência trocaria um card grudado em ATIVO por um grudado em DESLIGADO: quem
desligasse e religasse o neural ficaria em INICIANDO até o prazo de 25 s e depois em
DESLIGADO, com o neural rodando. O grude foi resolvido no lugar certo — o passo que decide
ON/OFF, que agora lê o log. Com o último toque em OFF nem se chega à busca da evidência.
O comentário no código registra a medição.

### Como testar

1. `DLSS5_DIAG=1` ajuda, mas não é preciso. Abra o painel com o app capturando.
2. Clique em **ligar** no card do neural. Tem de aparecer "LIGANDO O NEURAL RENDERING…",
   o botão vira "aguarde…", e ~2,5 s depois sai "NEURAL RENDERING LIGADO".
3. **Clique duas vezes rápido** no botão. O segundo clique tem de ser recusado com
   "UM INSTANTE…" — antes ele desfazia o primeiro. Confira o resultado: ligado, não
   de volta ao ponto de partida.
4. Aperte F6 (liga/desliga tudo) enquanto a confirmação do passo 2 está pendente: mesma
   recusa.
5. Desligue o neural pelo botão e observe o card: ele tem de ir para DESLIGADO na hora, sem
   esperar o `NeuralUplift` do `ReShade.ini` mudar. (Confira: `grep NR toggled ReShade.log`
   mostra o toque no mesmo segundo; o ini demora.)
6. Com o add-on da ponte fora da pasta (renomeie `dlss5-bridge.addon64`), o caminho cai no
   teclado; o cartaz de resultado tem de distinguir "o add-on não respondeu" de "recebeu e
   não mudou".

---

## 3. A camada bloqueava teclado e mouse no modo monitor

### O que estava errado

No modo sobreposto a janela é topmost e cobre o alvo inteiro. Com click-through ligado o
mouse atravessa; sem ele, **tudo** cai na nossa camada — e na fonte monitor "tudo" é a tela
inteira: barra de tarefas, outras janelas, nada responde.

Quem tirava o click-through nunca o devolvia:

* `CMD_RESHADE` fazia `C.clickThrough = false` e deixava assim para sempre. Como a tecla
  enviada estava errada (defeito 1), nada abria e a tela ficava presa **sem nada na frente
  que justificasse**.
* `PokeReShade(stayInteractive=true)` também não devolvia o click-through nem quando o poke
  abortava ou não entregava a tecla.

### O que mudou

* **Volta automática, sempre.** Todo caminho que sai do click-through arma um prazo:
  20 s sem nenhuma entrada dirigida à nossa janela, ou 45 s no total, e o click-through
  volta sozinho com cartaz.
  Só **tecla** estica o prazo total; mouse não. O motivo: quem está de fato usando o
  overlay do ReShade digita nele; quem está preso numa tela morta *clica* — e clique
  renovando o prazo total seria a armadilha se perpetuando. Quem for devolvido no meio de
  um uso legítimo aperta a tecla de volta e ganha outro período inteiro.
* **`PokeReShade` só fica interativo se entregou.** No aborto por foco e quando os dois
  eventos não saem (`sent < 2`), o click-through volta mesmo com `stayInteractive=true`.
* **Aviso impossível de perder na fonte monitor.** Enquanto a camada estiver com o mouse,
  um cartaz renovado a cada 3 s diz que a tela inteira é do filtro, qual tecla devolve e
  quantos segundos faltam para a volta automática.
* **Saída de emergência garantida.** No registro das hotkeys o app decide, uma vez, qual
  tecla devolve o mouse: o atalho escolhido pela pessoa (`interativo=F7` no
  `dlss5forall.ini` atual), senão a cascata do modo sobreposto, senão Ctrl+Alt+I. Se
  **nenhuma** ficou registrada, os cartazes não prometem tecla e o prazo total encurta para
  25 s — ali a volta automática é a única saída que existe. O log da abertura registra qual
  ficou valendo.

### Como testar

1. Abra o app com fonte **monitor** e sobrepor marcado.
2. Aperte F7 (ou clique em "Teclado pro ReShade" no painel). Tem de aparecer o cartaz
   grande dizendo que a tela inteira é do filtro, com a tecla de volta e a contagem.
3. **Não toque em nada por 20 s.** O click-through volta sozinho e o cartaz avisa. Confira
   que a barra de tarefas responde de novo.
4. Entre de novo e fique clicando na tela (sem teclar) por mais de 45 s: mesmo clicando, o
   prazo total vence e a camada devolve o mouse. É o caso "estou preso e clicando".
5. Entre de novo e use o teclado (abra o overlay do ReShade e digite): o prazo total se
   renova a cada tecla, e você continua trabalhando.
6. No `dlss5forall.ini`, ponha `interativo=sem tecla` e reabra. Entre no modo interativo: o
   cartaz não pode prometer tecla nenhuma se o Windows tiver recusado todas, e a volta
   automática cai para 25 s. Confira a linha `volta do interativo:` no `dlss5forall.log`.
7. Force uma falha de entrega: `DLSS5_DIAG=1 DLSS5_TESTA_POKE=abortar` e clique em "Abrir o
   ReShade". O poke aborta e o click-through **tem de continuar ligado** — antes ficava
   preso.

---

## 4. O app não verificava se a placa aguenta DLSS

### O que estava errado

Nada, em lugar nenhum, olhava a GPU. Num PC com Radeon, Arc ou GeForce **GTX**, o
`NGX_Init` falha lá no fundo do add-on e o painel dizia apenas:

> não subiu — confira "DLSS 5 Feed" no ReShade, ou reabra o app

Um conselho inútil: não há nada a conferir, e reabrir não muda nada. A pessoa fica
tentando resolver o que não tem conserto.

### O que mudou

* `DetectarGpu()` na abertura, logo depois da primeira linha do log, via DXGI
  (`EnumAdapters1` → `DXGI_ADAPTER_DESC1`). Loga uma linha por adaptador e a conclusão:
  `gpu: <descrição> vendor=0x%04X rtx=%d`.
* O critério **não é o fabricante**: a GTX 16xx é NVIDIA (`0x10DE`) e não tem Tensor Core
  nenhum. É `VendorId == 0x10DE` **e** "RTX" no nome — o nome que a própria NVIDIA só usa
  nas placas que trazem os núcleos que o DLSS precisa.
* Estado novo `NR_SEM_RTX`, com a mesma cor de aviso do `NR_SEM_ADDON`, o botão de ligar
  fora do card (não existe o que ligar), "sem RTX" no placar, "INDISPONÍVEL" no card e
  linha própria no guia.
* O cartaz de abertura ganhou o caso: diz que o neural não roda nesta máquina **e** que o
  resto do app continua servindo (espelho, captura, filtro). O app não é bloqueado.
* **A prova definitiva continua sendo o NGX**, não o nome da placa. `NgxPeloFeed()` lê o
  `dlss5-feed.log`:
  ```
  [feed] NVSDK_NGX_D3D12_Init -> 0x00000001 (Success)
  [feed] NGX capabilities: SuperSampling.Available=1 NeedsUpdatedDriver=0 MinDriver=470.0
  ```
  Se a placa é RTX mas o feed logou `Available=0`, `NeedsUpdatedDriver=1` ou um Init sem
  `(Success)`, o estado também é `NR_SEM_RTX` e o detalhe passa a falar de driver
  ("atualize o driver NVIDIA, mínimo 470"). Sem essas linhas ainda no log (os primeiros
  segundos, antes de o feed abrir a sessão D3D12) a função devolve "não sei" e **não**
  acusa ninguém.

### Uma coisa que a medição acrescentou ao pedido

Rodei um teste de adaptadores nesta máquina antes de fechar o código:

```
adapter 0: NVIDIA ... vendor=0x10DE vram=16048 MB     <- RTX 4080
adapter 1: AMD    ... vendor=0x1002 vram=485 MB       <- iGPU
adapter 2: Microsoft ... vendor=0x1414 (software)
D3D11 usou: vendor=0x10DE
```

Confirma que o adaptador 0 é o que o `D3D11CreateDevice(nullptr, HARDWARE)` usa — mas
mostra que máquinas híbridas são a norma, não a exceção. Em notebook, o 0 pode ser a iGPU
com a RTX no 1. Dizer "esta máquina não tem RTX" ali seria mentira, e mentira que faz
desistir do app. Então a detecção varre a lista inteira: se o adaptador usado não é RTX
mas existe uma RTX na máquina, o detalhe vira

> esta máquina tem uma <RTX>, mas o app está rodando na <iGPU> — em Configurações ›
> Sistema › Tela › Gráficos, escolha "Alto desempenho" para o DLSS5ForAll.exe e reabra

que é acionável. Adaptadores de software (`DXGI_ADAPTER_FLAG_SOFTWARE`) são ignorados.

### Como testar

1. Nesta máquina (RTX 4080): `dlss5forall.log` tem de trazer `gpu: NVIDIA GeForce RTX 4080
   vendor=0x10DE rtx=1`, e o painel segue igual ao de sempre.
2. Sem placa RTX (ou forçando o app na iGPU pelo Windows): o card do neural mostra
   INDISPONÍVEL, sem botão de ligar, com o nome da placa no subtexto; o cartaz de abertura
   diz que o resto do app continua funcionando. O app **não** pode se recusar a abrir.
3. Driver antigo: com `NeedsUpdatedDriver=1` no `dlss5-feed.log`, o detalhe tem de falar de
   driver, não de placa. Dá para exercitar editando essa linha do log à mão com o app
   aberto — a leitura é por arquivo e atualiza no próximo tique.

---

## 5. Duas otimizações no caminho do estado do neural

### `TailArquivo` lia 96 KB do disco por segundo

Dentro do laço que apresenta os quadros, uma vez por segundo (às vezes duas na mesma volta:
`LerEstadoNeural` e `NeuralPeloLog` leem o mesmo `ReShade.log`), para um arquivo que quase
nunca muda.

Agora há cache com **tamanho + horário de escrita**: iguais, devolve o que já está na mão.
Dois pontos que não são detalhe:

* **Tamanho menor também invalida.** Só perguntar "cresceu?" seria a armadilha: o ReShade
  rotaciona o log na abertura (`ReShade.log` → `.log1`, novo do zero) e o app passaria o
  resto da execução respondendo com a sessão passada.
* **Duas entradas, não uma.** Com a checagem do NGX, a mesma volta lê dois arquivos
  diferentes (`ReShade.log` e `dlss5-feed.log`). Com uma entrada só eles se expulsariam a
  cada chamada e o cache nunca acertaria — trabalho a mais para nada.

Medido com um teste descartável contra os arquivos reais: seis chamadas alternando entre os
dois arquivos → **duas** leituras de disco. Rotação (arquivo encolhendo), remoção e
recriação do arquivo foram exercitadas e invalidam corretamente.

### `LerEstadoNeural` era chamado de segundo em segundo com o painel fechado

E o `Hud::Refresh()` logo abaixo não desenha nada quando o painel está fechado. Agora, com
o painel fechado, a leitura é de 5 em 5 s. Três exceções mantêm 1 s:

* **painel aberto** — o card mostra o estado, tem de acompanhar;
* **verificação pendente** (`verificaNeuralAte`) — é a janela pós-toggle, que vive da
  leitura rápida;
* **estado INICIANDO** — e este é o cuidado que o espaçamento exigia: o prazo de 25 s é
  contado *dentro* do `LerEstadoNeural` e só avança quando alguém a chama. Espaçar ali
  faria o "não subiu" aparecer com 30 s ou mais.

### Como testar

1. Com o painel fechado e o neural ativo, o app tem de continuar mostrando o estado certo —
   só que com até 5 s de atraso para mudanças feitas por fora (pelo overlay do ReShade).
2. Abra o painel: o card volta a acompanhar de segundo em segundo.
3. Clique em ligar/desligar com o painel fechado: a confirmação de 2,5 s continua saindo no
   tempo, porque a verificação pendente força a leitura rápida.
4. Ligue o neural e deixe a cadeia falhar (por exemplo sem o `DLSS5_Feed.fx` no preset): o
   "não subiu" tem de aparecer por volta dos 25 s, não dos 30.

---

## 6. Escolher a tecla que abre o ReShade, no seletor

Pedido do usuário: "antes de abrir a janela pra captura, tem que haver a possibilidade de
configurar com qual botão queremos abrir o ReShade base também".

### O que mudou

O diálogo **Atalhos…** do seletor ganhou uma linha, separada das outras por um respiro e
por um rótulo que diz de onde ela vem: *"Esta última é do ReShade, não do app — ela vai
para o ReShade.ini"*. Mudar ali grava `[INPUT] KeyOverlay=vk,ctrl,shift,alt`. O painel e o
botão "Abrir o ReShade" já liam essa chave, então refletem na hora.

Modificadores funcionam (a captura de tecla do diálogo já os pegava, foi reaproveitada) e o
`CMD_RESHADE` reescrito no defeito 1 já sabe lidar com eles.

### Quando o ReShade lê isso — medido, não suposto

```
12:40:20:404  Initializing crosire's ReShade ... loaded from 'dxgi.dll'   <- antes do nosso wWinMain
12:40:20:454  (dlss5forall.log) "DLSS5 For All 1.1 iniciando"             <- o seletor é aqui
12:40:22:284  Recreated runtime environment on runtime ... ('ReShade.ini')
```

A última linha cita o próprio `ReShade.ini` no instante em que o `effect_runtime` sobe — que
é o primeiro swapchain, criado só **depois** de a pessoa fechar o seletor. Então a gravação
entra a tempo e o diálogo diz "vale já nesta abertura".

Como isso é uma conclusão sobre o comportamento do ReShade e não um contrato, o cartaz do
botão "Abrir o ReShade" carrega uma linha de escape **enquanto a tecla tiver sido trocada
nesta sessão**: *"se não abrir: a tecla foi trocada agora; feche e abra o app"*. Se a
conclusão não valer em alguma versão do ReShade, a pessoa lê o que fazer em vez de apertar
uma tecla morta. A linha some na abertura seguinte.

### Segurança do arquivo

* **Sem BOM**: conferido, o `ReShade.ini` começa em `[ADDON]`. Por isso o
  `WritePrivateProfileStringW` o enxerga — e por isso `GetPrivateProfileStringW` já
  funcionava em `TeclaOverlayReShade`.
* **Backup uma vez por sessão** antes da primeira gravação:
  `ReShade.ini.bak-keyoverlay-<AAAAMMDD-HHMM>`.
* **Só a chave muda.** Testado numa cópia do `ReShade.ini` real, gravando cinco teclas
  diferentes (Home, End, Ctrl+Home, Ctrl+Shift+Alt+F10, Alt+K): round-trip correto nos
  cinco (`36,0,0,0`, `36,1,0,0`, `121,1,1,1`, `75,0,0,1`) e o resto do arquivo byte a byte
  idêntico.
* **Armadilha que o teste pegou**: `WritePrivateProfileStringW` com caminho *relativo* grava
  em `%WINDIR%` e devolve 0. O app usa `g_exeDir + "ReShade.ini"`, que é absoluto
  (`GetModuleFileNameW`) — mas ficou anotado, porque é o tipo de erro que não aparece em
  leitura de código.

### Colisão

Recusada **nos dois sentidos**, no diálogo e não como aviso no log:

* escolher para o ReShade uma tecla que já é de um atalho do app → *"X já é de 'abrir o
  painel' aqui no app — escolha outra para o ReShade"*;
* escolher para um atalho do app a tecla que já abre o ReShade → *"X já abre o ReShade —
  escolha outra, ou mude a tecla do ReShade abaixo"*.

O padrão de fábrica é End para o painel e Home para o ReShade; sem essa recusa, quem
escolhesse End para o ReShade teria as duas coisas na mesma tecla, brigando em silêncio. O
log de aviso antigo continua existindo, porque a pessoa também pode editar o `ReShade.ini`
à mão.

**"Restaurar padrões"** devolve os atalhos do app **e** o ReShade para Home (36) — restaurar
metade seria meia verdade.

### Como testar

1. Abra o app, clique em **Atalhos…** no seletor. A última linha tem de mostrar
   "Abrir o ReShade (overlay): Home".
2. Clique em **Mudar** nela e aperte `Insert`. O rótulo vira "Insert", o aviso confirma, e
   `grep KeyOverlay ReShade.ini` tem de mostrar `45,0,0,0`. Confira que apareceu um
   `ReShade.ini.bak-keyoverlay-…` na pasta.
3. Feche o seletor, inicie a captura e aperte Insert: o overlay do ReShade abre. O badge do
   painel e o botão "Abrir o ReShade" também têm de dizer "Insert".
4. Tente escolher **End** para o ReShade: tem de ser recusado, citando o painel.
5. Tente escolher **Insert** (a tecla do ReShade agora) para o painel: recusado também.
6. **Restaurar padrões**: volta tudo, e `KeyOverlay` volta a `36,0,0,0`.
7. Combinação: escolha `Ctrl+Home` para o ReShade. O `ReShade.ini` tem de ficar
   `36,1,0,0`, e o botão "Abrir o ReShade" no painel pede que você aperte a combinação (ele
   não sintetiza modificadores — ver defeito 1).

---

## 7. "Mostrar FPS na tela" — o contador que não era nosso

Pedido do usuário: "o programa mostra o FPS quando aplicado; coloca uma caixa para que isso
seja opcional". O número é o contador do overlay do ReShade (`[OVERLAY] ShowFPS`), que
aparece junto com o filtro — daí a confusão.

Caixa no **seletor** (ao lado de V-Sync/HDR) e chavinha no **painel**. Detalhes de
implementação, incluindo por que a gravação usa dois caminhos e o que o texto não promete,
estão em `docs/painel.md` §5.1. O resumo:

* `ReShadeSetConfigValue` — a API que o **próprio ReShade** exporta pelo `dxgi.dll`
  (conferido com `objdump -p`) — evita que o AutoSave do ReShade desfaça a gravação. Não
  foi preciso comando novo na ponte nem tocar no add-on.
* A escolha também vai para `[ultima] fps_tela` e é reaplicada na abertura, antes do
  seletor. Chave ausente = ninguém pediu nada, e o padrão do ReShade (ligado) fica.
* A interface **não promete** que o contador some na hora: quem o redesenha é o ReShade, e
  o momento em que ele com certeza releu o valor é a próxima abertura.

### Como testar

1. Seletor: a caixa "Mostrar FPS na tela" nasce marcada (é o `ShowFPS=1` de hoje).
   Desmarque e inicie — o contador não deve aparecer.
2. `grep ShowFPS ReShade.ini` tem de mostrar `ShowFPS=0`, e `grep fps_tela dlss5forall.ini`
   → `fps_tela=0`.
3. Abra o painel: a chavinha "Mostrar FPS na tela" tem de estar desligada. Ligue —
   o cartaz aparece e o contador volta (na hora ou na próxima abertura).
4. Feche e reabra o app sem passar pelo seletor: a escolha tem de continuar valendo, mesmo
   que o ReShade tenha regravado o ini dele nesse meio-tempo.
5. Quem nunca mexeu não vê diferença nenhuma: sem `[ultima] fps_tela`, o app não escreve no
   `ReShade.ini` na abertura.

---

## 8. "Nada na tela estava clicável por alguns segundos" — aquecimento em bypass

Queixa: ligou o filtro no monitor grande e, nos primeiros segundos, nada respondia.

### O que acontece de verdade

Cronologia da sessão dele (logs das 15:27):

```
15:27:46.06  swapchain pronto — a camada topmost já cobre 3440x1440
15:27:47.48  DLSS5_Feed.fx compilado
15:27:48.34  sessão D3D12 do feed aberta
15:27:49.99  build da feature
15:27:50.36  feature 18 criada + primeira avaliação
```

São **~4 s** com a camada na frente e o nosso Present preso atrás da criação das features
NGX/DLSSNR na mesma GPU. A camada mostra um quadro **parado** enquanto o desktop real
continua vivo por baixo. Os cliques atravessam — o click-through está correto desde a
criação da janela — mas a pessoa não vê efeito nenhum e conclui, com razão, que a tela
travou. Num monitor inteiro, isso é a tela toda.

### O que mudou

**A camada nasce em bypass** (o mesmo caminho que já a tira de vista) e só aparece quando a
cadeia entrega de verdade. `CadeiaEntregando()` aceita três respostas independentes, porque
a cadeia pode estar entregando com o neural desligado:

1. `LerEstadoNeural() == NR_ATIVO`;
2. `frame N delivered` no `dlss5-feed.log`;
3. `evaluation succeeded` no `ReShade.log`.

Sondagem a cada 250 ms, sem leitura nova de disco (as duas fontes já estão no cache do
`TailArquivo`). Vale para janela sobreposta tanto quanto para monitor.

**Teto de 30 s**: se nada chegar, a camada aparece mesmo assim, com um cartaz que não
finge sucesso — "A REDE NÃO RESPONDEU / mostrando o filtro como está". Deixar a camada
escondida para sempre seria trocar um defeito visível por um invisível.

**A escolha da pessoa desarma o automático**: mexer no filtro (atalho/bandeja) ou apertar o
F6 durante o aquecimento encerra o modo. No caso do F6 a camada sai do bypass **antes** de
o `AlternarTudo` rodar — senão ele guardaria "a camada estava escondida" em `g_antes.camada`
e o toque seguinte devolveria o app para o bypass, de onde não sairia mais.

**Cartazes**: "INICIANDO A REDE NEURAL… (a tela continua sua enquanto isso)", renovado a
cada 2 s, e "FILTRO LIGADO" quando a camada aparece. O cartaz do aquecimento não sobe por
cima de um aviso de abertura ("alguma tecla já estava em uso", "a pasta é somente leitura"),
que dura 8-9 s e é o que a pessoa precisa ler.

### Uma armadilha que quase anulou o recurso

Com a camada em bypass, `FollowTarget` a joga para fora da tela e `overlayVisible` vira
falso — e o laço então usa um Present lento (500 ms) para a camada escondida. Só que **é o
Present que faz o ReShade compilar os efeitos e a cadeia subir**. Sem tratar isso, o
aquecimento (que existe para encurtar a espera) passaria a alongá-la para dezenas de
segundos, até estourar o teto. Por isso `if (aquecendo) hidden = false;`: fora de vista,
mas apresentando no ritmo normal.

### Diagnóstico permanente

No laço de present, uma linha no log quando o `DrawAndPresent` passar de 250 ms, no máximo
10 por sessão:

```
present travou 812 ms (t+3.4 s, intervalo 900 ms)
```

**Mede a duração do `DrawAndPresent`, não o intervalo entre dois Presents** — o intervalo
inclui espera proposital (o gap de 100/500 ms e o teto de fps) e encheria o log de travas
que não existem; ele entra na linha como informação, não como critério. Se as travas
aparecerem agrupadas nos primeiros segundos, a hipótese acima está confirmada; se não
aparecerem, a causa é outra e o aquecimento é paliativo.

### Como testar

1. Abra no **monitor** com SOBREPOR marcado. Nos primeiros segundos a tela tem de continuar
   normal e **clicável de verdade** (abra o menu Iniciar, arraste uma janela), com o cartaz
   "INICIANDO A REDE NEURAL…" no canto.
2. Quando a cadeia subir, o filtro aparece e sai "FILTRO LIGADO". No `dlss5forall.log`:
   `aquecimento: camada liberada apos N ms (a cadeia entregou)`.
3. Repita com fonte **janela** sobreposta — mesmo comportamento.
4. Durante o aquecimento, aperte o atalho do filtro: a camada aparece na hora e o log diz
   `aquecimento: desarmado - a pessoa mexeu no filtro na mao`.
5. Durante o aquecimento, aperte F6 duas vezes: o filtro tem de voltar a aparecer no
   segundo toque (não pode ficar preso em bypass).
6. Force o teto: renomeie o `DLSS5_Feed.fx` para a cadeia não subir. Aos 30 s a camada
   aparece com o cartaz "A REDE NÃO RESPONDEU".
7. Procure `present travou` no `dlss5forall.log` depois de uma abertura no monitor grande —
   é a linha que confirma ou refuta a hipótese.

---

## 9. Instância única — duas cópias sobre a mesma tela

### O que aconteceu

Na sessão do usuário havia **duas** cópias do `DLSS5ForAll.exe` rodando (15:27 e 15:43), as
duas em monitor sobreposto sobre a mesma tela. O log da segunda conta a história inteira:

```
atalho painel = Insert
atalho tudo = Alt+F6
atalho interativo = F7 (RECUSADO - ja em uso)
atalho sair = (nenhum livre)
Modo sobreposto: (nenhum livre) = sair
```

A primeira cópia já tinha End/F6/Alt+Esc, então a segunda caiu na cascata de reserva e
acabou **sem tecla de saída**. Duas camadas topmost empilhadas, duas cadeias NGX disputando
a GPU, e a barra de tarefas atrás de tudo. É a origem provável do "nada clicável".

### O que mudou

Mutex nomeado `Local\DLSS5ForAll.instancia`, criado **antes de qualquer coisa que escreva**.
Se já existir, esta cópia não abre nada: loga, avisa e sai.

`Local\` e não `Global\` de propósito — o escopo é a sessão de logon. Dois usuários na mesma
máquina (troca rápida de usuário) têm cada um a sua tela e o seu app.

**A ordem importa mais do que parece.** O log é aberto em `"w"`, que **trunca**. Criar o
mutex depois do log significaria que a cópia bloqueada apagaria justamente o log da cópia
viva — a única prova do que estava acontecendo. Agora o mutex vem primeiro e, sabendo que há
outra, o log abre em `"a"`: a linha do bloqueio entra no fim, sem destruir nada.

O aviso é um `MessageBox` (não o nosso cartaz, que some sozinho e sai da captura — errado
para a única coisa que essa cópia tem a dizer) e diz **como fechar a que está aberta**, com
as teclas lidas do ini e a hora de início tirada da primeira linha do log. Sem log legível,
não cita hora, em vez de inventar uma. O PID da outra cópia vai para o log, para que o
diagnóstico seja acionável no Gerenciador de Tarefas.

Também traz a cópia existente para frente **quando isso adianta**: se ela está no seletor ou
é uma janela separada. No modo sobreposto não mexe — aquela janela é topmost e
`WS_EX_TRANSPARENT`, e `SetForegroundWindow` nela não traz nada para lugar nenhum, só rouba
o foco de quem estava trabalhando.

### A exceção, e por que ela não é conveniência

Ignoram o mutex: `--outra-instancia` (a porta explícita) e os modos que **não abrem camada
nenhuma** — `--panel-shot`, `--guia-shot`, `--list`, `--list-windows`. O perigo aqui é
especificamente a janela sobreposta em cima da tela do usuário, e nenhum desses a cria;
bloqueá-los quebraria a bancada sem evitar nada. **O caminho do usuário — sem argumentos,
pelo seletor — nunca ignora.**

### Como testar

1. Abra o app normalmente e deixe rodando. Abra de novo: tem de aparecer o `MessageBox`
   "O DLSS5 For All já está aberto (desde HH:MM)", e **nenhuma** segunda janela.
2. `dlss5forall.log`: a última linha é `instancia: ja existe outra (pid N), saindo` — e as
   linhas da cópia viva **continuam lá**, não foram truncadas.
3. Feche a primeira e abra de novo: tem de abrir normalmente (o mutex sai com o processo).
4. Mate a primeira pelo Gerenciador de Tarefas e abra de novo: idem — não existe o caso "o
   app não abre mais porque ficou um mutex preso".
5. Com o app aberto, rode `DLSS5ForAll.exe --panel-shot x.png`: tem de funcionar, e sem
   truncar o log da cópia viva.
6. Com o app aberto em janela separada, abra de novo: além do aviso, a janela existente vem
   para frente. No modo sobreposto, o foco de quem está jogando **não** pode ser mexido.

---

## Arquivos tocados

`main.cpp` e `README.md`. Regiões do `main.cpp`: `TeclaOverlayReShade`,
`BackupReShadeIniUmaVez`/`GravarTeclaOverlayReShade`, `SetClickThrough` (bloco novo de volta
automática logo abaixo), `GpuInfo`/`DetectarGpu`/`PlacaComTensorCore`, `TailArquivo`,
`NgxPeloFeed`, `HudState`, `LerEstadoNeural`, o card e o placar do neural no painel, a
linha do neural no guia, `PokeReShade`, o diálogo de atalhos do seletor (`AtalhoDlg`,
`AtalhoDlgAtualiza`, `AtalhoDlgProc`, `ShowAtalhosDialog`), o registro das hotkeys do modo
sobreposto e o laço principal (`CMD_TUDO`, `TRAY_INTERACT`, `CMD_RESHADE`, `CMD_NEURAL`, a
verificação assíncrona, o tique de volta do modo interativo e o espaçamento da leitura do
neural).
