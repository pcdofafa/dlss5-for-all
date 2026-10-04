# DLSS5 For All

🇺🇸 [English version](README.md)

**por PC do Fafa** — canal: <https://youtube.com/@pcdofafa> · site: <https://pcdofafa.com.br/dlss>

Uma janela Direct3D 11 mínima que mostra **uma fonte de vídeo qualquer** — placa de captura, webcam,
**uma janela de jogo** ou **um monitor inteiro** — e existe por um único motivo: ser o alvo mais simples e
previsível possível pra cadeia **ReShade → DLSS5-Feeder → LumeniteFX → renodx-dlss**.
É o mesmo truque que o EDMIRE usou no Switch 2 (Feeder dentro do OBS + projetor), só que sem o OBS no meio:
um exe, um swapchain, um depth buffer real, uma resolução.

O DLSS 5 (Neural Rendering) recebe **um frame 2D + motion vectors** e "fotorrealiza" o que vê.
O Feeder fabrica o resto do contrato (depth + motion vectors por optical flow).
Como nada é injetado no processo do jogo, isso funciona com **qualquer** jogo que rode em janela ou borderless
— inclusive emulador, vídeo do YouTube, jogo antigo em DirectX 9, o que for.

## Fontes de vídeo

| Fonte | O que é | Como abre |
|---|---|---|
| **Monitor** | Windows Graphics Capture do monitor inteiro | seletor (é o padrão), ou `--source monitor --monitor N` |
| **Placa de captura / câmera** | Media Foundation (webcam, ezcap, DroidCam…) | seletor, ou `--device N --mode M` |
| **Janela** | Windows Graphics Capture da janela escolhida — **mais leve** | seletor, ou `--source window --window <parte do título ou do exe>` |

**Monitor é o padrão, mas a Janela deixa o filtro mais leve** — e essa é a razão de ela existir no seletor. A rede
processa **área**: um jogo em 2560x1080 são 2,8 MP por quadro contra 5,0 MP de uma tela 3440x1440, quase metade
da conta. No modo monitor não há como fugir disso, porque a captura é a tela toda mesmo que o jogo ocupe um
pedaço dela. Em compensação, a Janela pede que você escolha o alvo numa lista, e a camada some quando ele perde
o foco.

E as fontes de tela (janela e monitor) têm **dois modos de operação**, escolhidos na caixa
*"SOBREPOR o alvo"* do seletor (ou `--overlay`):

- **Janela separada** (caixa desmarcada): o alvo aparece dentro da janela do DLSS5 For All, com o filtro
  aplicado. Bom pra gravar, comparar lado a lado e mexer no ReShade à vontade.
- **Sobreposto** (caixa marcada / `--overlay`): a janela vira um popup sem borda **colado exatamente em cima
  do alvo**, sempre no topo e **transparente ao mouse** — quem recebe clique e teclado continua sendo o que
  está embaixo. Parece que o DLSS 5 está rodando dentro do jogo.
  - sobre uma **janela**: segue posição e tamanho, e some sozinho quando o jogo perde o foco;
  - sobre um **monitor**: cobre a tela inteira, e como a nossa janela é excluída da captura
    (`WDA_EXCLUDEFROMCAPTURE`), não há espelho infinito — **tudo o que estiver naquela tela passa pelo filtro**.
    Se nada mudar na tela por vários segundos a imagem pode parar (a camada acaba tapando a própria fonte);
    o app detecta, recria a captura e, se ainda assim não voltar, devolve a imagem original sozinho.

> **Duas coisas fazem o modo sobreposto ser usável, e as duas foram descobertas medindo.**
>
> **1. A camada é `WS_EX_LAYERED`, e sem isso o clique não atravessa.** Só `WS_EX_TRANSPARENT` + responder
> `HTTRANSPARENT` no `WM_NCHITTEST` **não basta**, e a documentação diz por quê: `HTTRANSPARENT` manda o
> clique para as janelas de baixo *"in the same thread"* — ou seja, só para janelas do próprio app. Para o
> Explorer, o desktop ou o jogo, o clique morre na camada. Quem atravessa processos é a dupla
> `LAYERED + TRANSPARENT`. Medido aqui, mesma sessão e mesmo ponto, só essa variável mudando: sem layered,
> **bloqueado**; com layered, **passou**. `DLSS5_SEM_LAYERED=1` volta ao comportamento antigo, caso o layered
> brigue com o flip model em alguma máquina.
>
> **2. O layered também resolve um segundo problema, e por isso a janela NÃO precisa de uma thread só dela.**
> O `WM_NCHITTEST` é **síncrono**: quando o Windows precisa perguntar "o clique é seu?", ele espera a resposta
> da thread dona da janela antes de entregar o clique a quem quer que seja. Com a camada cobrindo a tela e o
> render na mesma thread, cada `Present` caro do DLSS segurava o mouse do sistema inteiro — medido: **1.864 ms**
> de espera, com o log marcando `present travou 841 ms` no mesmo instante. A janela chegou a ser movida para
> uma thread própria por isso (0,4 ms no pior caso), mas com `WS_EX_LAYERED` o sistema **descarta a camada no
> hit-test sem perguntar nada**, e a espera some pela raiz. A thread extra foi desfeita porque cobrava caro em
> outro lugar: o ReShade vive dentro deste processo e o `PokeReShade` (o F6) posta a tecla na janela contando
> com o `Present` da mesma thread — com a janela em outra, o F6 passou a demorar. Nada é interceptado nem
> reencaminhado: a camada só não é consultada, e o clique segue o caminho normal do sistema.

No modo sobreposto o app **não aparece na barra de tarefas nem no Alt+Tab**: ele fica na **bandeja do sistema**,
e o menu do ícone traz os comandos e os atalhos.

### Atalhos (só dois)

| Tecla | O quê |
|---|---|
| **End** | abre e fecha o painel — tudo o mais (filtro, neural, fps, HDR, ReShade, sair) é feito nele, com o mouse |
| **F6** | liga/desliga **tudo o que o painel configurou** (neural + a camada, no modo sobreposto) de uma vez — o antes/depois — e volta exatamente como estava; aviso na tela do estado real |
| **Alt+Esc** | sai do app (duas vezes em 2 s, para não fechar sem querer) |
| **F8** | troca o modo de processamento na mão: Nativo ↔ Universal (ver "Modos de processamento" abaixo) |

Todas as teclas são configuráveis — no botão **Atalhos…** do seletor (antes de escolher a fonte) e na seção
ATALHOS do painel, em "Mais opções": painel, neural, filtro, teclado pro ReShade e sair, cada uma com a tecla
que você quiser (com detecção de conflito e "restaurar padrões"). Por padrão End, F6 e Alt+Esc têm tecla.

O botão **Atalhos…** do seletor também configura a tecla que **abre o overlay do ReShade** (padrão **Home**).
Essa é a única da lista que não é do app: ela vai para `[INPUT] KeyOverlay` do `ReShade.ini` — o app faz um
backup (`ReShade.ini.bak-keyoverlay-…`) antes da primeira gravação e recusa uma tecla já usada por um atalho
daqui, nos dois sentidos. Escolhida no seletor, vale já nesta abertura: o ReShade lê o `ReShade.ini` quando o
primeiro swapchain sobe, e isso acontece depois que você fecha o seletor.
Ao iniciar, um cartaz com as teclas fica na tela por 30 s (some ao apertar End; "não mostrar de novo" disponível,
e o painel tem "Como usar" para rever). Se outro programa já tiver a tecla,
o app pega a reserva e mostra qual valeu no cartaz inicial. **Esc nunca fecha o app** — só o painel ou a tela
que estiver aberta; sair é por Alt+Esc, pelo botão do painel, pelo X (modo janela separada) ou pela bandeja.

#### Quando o atalho não chega dentro do jogo

O `RegisterHotKey` é global e funciona com qualquer janela na frente — **menos** em dois casos, e os dois têm
saída:

- **o jogo segura as hotkeys do Windows** (alguns pedem `RIDEV_NOHOTKEYS` no Raw Input). Não há API para
  perguntar isso de fora; o app resolve **observando a tecla por outro caminho**: uma janela invisível nossa
  registrada como receptora de **Raw Input** (`RIDEV_INPUTSINK`, em thread própria) vê a tecla sem foco, espera
  250 ms pelo `WM_HOTKEY` e, se ele não vier, aciona a ação assim mesmo — com deduplicação nos dois sentidos,
  para uma tecla nunca virar duas ações. O log diz quando isso acontece (`o WM_HOTKEY NAO chegou em 250 ms … a
  janela na frente era …`). `[ultima] atalho_reserva=0` desliga.
  **Era um hook `WH_KEYBOARD_LL` até 15/09/2026.** Um hook de baixo nível entra na cadeia por onde todo o teclado
  do sistema passa antes de chegar ao jogo, que é o que heurística de anti-cheat lê como macro; Raw Input é a
  mesma API que o próprio jogo usa para ler o teclado, apontada para a nossa janela, e não põe nada no caminho
  do jogo (o `NOHOTKEYS` do jogo só desliga hotkeys enquanto ele está na frente, não impede outro processo de
  ler o teclado como dispositivo). Some também o `LowLevelHooksTimeout`; a thread continua própria só por
  latência, para o `WM_INPUT` não esperar um `Present` travado. Quando o app detecta um sistema anti-cheat
  conhecido na pasta do exe capturado (fonte janela), ele liga o **Modo anti-cheat** para essa fonte e um cartaz
  diz isso na abertura; este caminho reserva **segue ligado** (só lê, fora do processo do jogo) — End e F6
  continuam por ele, pelo `RegisterHotKey` e pela bandeja;
- **o jogo roda elevado e o app não.** Aí nenhuma tecla nossa chega lá (UIPI), e não há truque: o app detecta
  (`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` + `TokenElevation`; o próprio `ACCESS_DENIED` já é resposta)
  e mostra um cartaz dizendo para abrir o DLSS5 For All como administrador também.

Há ainda um caminho que **não depende de tecla nenhuma**: o **ícone na bandeja**, cujo menu abre o painel.

> **O botão flutuante saiu em 11/09/2026.** Havia uma pastilha sempre por cima da camada, como terceira porta
> para o painel; o usuário pediu a remoção — era mais uma coisa em cima do jogo. **Não ficou chave para ligar
> de volta**: opção escondida no ini para um desenho que ninguém mais vê não é configuração, é código morto com
> um interruptor em cima. As chaves `[ultima] botao_flutuante`, `botao_x` e `botao_y` deixaram de ser lidas. O
> caminho reserva de **teclado** continua — é ele que resolve o caso "o jogo segura a hotkey", que era o motivo
> original da pastilha.

## Painel na tela (End)

A tecla **End** abre um painel por cima de tudo (fora da captura, então não entra no vídeo nem no filtro).

A **primeira vista** traz só o que se mexe no meio de uma partida; o resto fica em **Mais opções**, que abre uma
segunda coluna ao lado. As explicações não ficam mais na tela: passar o mouse sobre qualquer controle abre um
**balão** com o texto, depois de 350 ms. O painel nunca é desenhado abaixo do tamanho base — se não couber na
altura da tela, esconde-se conteúdo, não se encolhe a letra.

- **DLSS 5**: um card só, HABILITADO/DESABILITADO (F6), que liga e desliga a camada **e** o neural juntos. O estado
  do Neural Rendering aparece nele como informação — é como se percebe que a cadeia não subiu.
  **O programa abre sempre ligado, em 1 passe** (decisão do usuário): desligar pelo F6 vale **para a sessão**,
  e a próxima abertura começa ligada. O arranque desfaz a marca `[ultima] neural_off_nosso` que a sessão
  anterior tenha deixado — sem isso o card mostraria um estado que já não é o real
- **Perfil da imagem (rede neural)**: um cartão com o resumo do que está valendo
  (`Modelo B · Intensidade 100% · 2 passes`) e o botão **Ajustar**, que abre uma janela própria
  (classe `DLSS5ForAllPerfil`, no mesmo molde da janela de Filtros) com os seis controles:
  MODELO (A/B/C), INTENSIDADE, ESTRUTURA, PELE E ROSTOS, TOM LOCAL e TOM GLOBAL
- **PASSES DA REDE** 1…10 — quantas vezes a rede refaz o **mesmo** quadro (`DirectNeuralRenderingPassCount`).
  Medido nesta árvore: ~4,2 ms de GPU por passe. Substitui o antigo "1x / 2x / 3x"
- **DLSS FRAME SKIPPING** Desligado / 1 de cada 2 / 3 / 4 — a rede processa um quadro de cada N e o app
  **desenha** os outros, projetando a cena pelo campo de movimento. O movimento sintetizado é automático
  (não há interruptor) e cai sozinho para "segurar" quando falta base ou campo. Uma seção só: no modo
  sobreposto ela usa a camada intermitente com síntese, no espelho usa o pulo simples — as duas chaves
  (`alterna_quadros`, `pulo_quadros`) continuam no ini e ligar uma desliga a outra
- **FLUIDEZ DO DLSS 5 NA TELA** `[−] 60 [+]` e **Destravar** — quantos quadros por segundo a película
  apresenta; não muda o fps do jogo. **Abaixo** do que o jogo entrega o número é um teto e é **exato**.
  **Acima**, entram quadros projetados pelo movimento — e isso é **experimental**: a entrega é **parcial**,
  conforme a folga da placa. Medido aqui com o jogo a 60: pedindo 90 a película foi a **70**; pedindo 120,
  a **73** (o teto útil da tela era 94). Sobe de verdade, mas não alcança o número pedido.
  **Destravar** não desliga a projeção: sem número escolhido, o alvo passa a ser o máximo útil da tela
  (95% do refresh). O `[−]` continua valendo no estado destravado e volta ao último número escolhido.
  Grava em `[ultima] fps_teto_pelicula`; padrão 60; `0` = destravado. Uma linha de status embaixo mostra
  `jogo N/s · película N/s · N projetados por segundo` — e a **película conta tudo o que vai à tela**:
  processado, sintetizado pelo frame skipping e projetado. (Até 11/09/2026 o sintetizado ficava de fora e,
  com o skipping ligado, a linha mostrava metade do que a pessoa estava vendo.)

  **Compõe com o DLSS FRAME SKIPPING**, e isso foi medido: com skipping 1 de 2 e a Fluidez em 120, a rede
  processou ~20/s e a película saiu a **74–80/s**. A prova externa (contagem de quadros distintos na tela,
  fora do app) deu **≥57/s** — é um piso, porque o amostrador satura em ~88 amostras/s; mesmo assim está
  muito acima dos ~20 processados, que é o que a composição precisava mostrar.
- **Resolução do DLSS** Auto/100%/77%/67%/58% — mexe no **custo da rede**. `Auto` escolhe pela área da fonte
  (≤2,4 MP: 100 · ≤4,0: 77 · ≤6,0: 67 · acima: 58). Vale ao vivo: o feed segura o rebuild por 60 quadros e
  reconstrói (medido: ~2 s), com o neural desligado durante a reconstrução e religado 4 s depois
- **Mais opções**: Filtros (nitidez, anti-serrilhado, detalhes, cor, brilho e contraste, exposição e gamma, vibrance, curvas, sépia, preto e branco, modo noturno, vinheta e grão de filme — cada um com chavinha e barrinha, valendo na hora, em janela própria), V-Sync, HDR, Mostrar FPS na tela,
  **Modo comparativo**, FPS do jogo, idioma, atalhos, o **Grupo de ofertas — PC do Fafa**, o canal,
  Abrir o ReShade, **Trocar fonte ou placa** e Sair
- **Trocar fonte ou placa** (ao lado do Sair): volta para a tela de escolha da fonte sem fechar o app na mão.
  O caminho é uma **reabertura** — a cadeia NGX não sobrevive a uma troca de fonte com o device vivo —, com
  cartaz na frente enquanto troca. A cópia nova leva a fonte, a placa e o idioma de agora **já marcados**
  (`--pre-fonte`, `--pre-janela`/`--pre-monitor`/`--pre-dispositivo`/`--pre-modo`, `--placa`); sem esses
  argumentos o seletor cairia no que está no ini, que é a última vez que alguém abriu o app pelo seletor.
  A janela casa por **título**: se o jogo muda o título sozinho, o seletor abre no primeiro item da lista.
  O app também volta sozinho para essa tela quando a janela capturada é fechada

### O perfil só vale quando o add-on carrega

MODELO, INTENSIDADE, ESTRUTURA, PELE, TOM e PASSES são gravados em três lugares (o `[ultima]` do nosso ini, a
config viva do ReShade pela ponte e o `ReShade.ini`), e mesmo assim **não mudam a rede enquanto o app roda**: o
add-on RenoDX lê o perfil uma vez, quando carrega, e não relê. Medido: `DirectNeuralRenderingPassCount=10`
escolhido no painel deixou o feed em 0,56–0,64 ms/quadro e 61 fps; o mesmo 10 lido **na abertura** deu
3,04–6,54 ms/quadro e 24,6 fps.

Por isso o app **se reabre sozinho** depois de um ajuste de perfil: 2,5 s de espera (cada novo toque reinicia a
contagem, então mexer em seis controles reabre uma vez só), cartaz na tela e `ReabrirApp` preservando alvo (por
HWND), fonte, placa, pulo e intermitente. `[ultima] perfil_reabre=0` desliga a reabertura — aí a escolha fica
gravada e vale na próxima abertura feita à mão.

### Motor da rede: RenoDX (padrão) ou o próprio (experimental)

> **Escondido nesta release — volta na próxima.** O interruptor do motor, a régua
> **TAMANHO DA REDE NEURAL** e o **ORÇAMENTO DE ESPERA** do módulo AMD só aparecem com
> `[ultima] recursos_experimentais=1`. O motivo não é a interface nem a segurança: o feeder que
> vai junto com esta versão **já traz** a guarda que impede os dois motores neurais de rodarem
> juntos (a que evita o `0xC0000005` no `evaluate` do SuperSampling). O que falta é **medição**:
> o motor próprio só foi comparado em cena sintética, e o que não está fechado não sai de fábrica
> ligado. Com a chave, tudo abaixo volta a aparecer; nada foi removido do programa. Os chips de
> **Resolução do DLSS** de sempre continuam onde estavam — eles são outra coisa (o tamanho em que
> a cadeia inteira trabalha).

Duas implementações da **mesma rede** convivem na pasta, e a troca é só configuração — nada é renomeado
nem removido. Em **Mais opções → MOTOR DA REDE NEURAL**:

| | RenoDX (padrão) | Próprio (experimental) |
|---|---|---|
| `ReShade.ini` `[RENODX-DLSS] DirectNeuralRenderingHookPoint` | `2` | `0` |
| `dlss5-feed.cfg` `nr_spike` | `0` | `1` |
| quem avalia a rede | `renodx-dlss.addon64` | `dlss5-feed.addon64` |
| perfil e passes | valem **na próxima abertura** | valem **na hora** (o cfg é relido a cada 60 quadros) |
| custo de GPU | — | **empate**; não é mais rápido |
| imagem | borra bastante com muitos passes | borra bem menos (laplaciano 834 contra 168 com 4 passes) |

**Trocar reabre o app uma vez**, e não é escolha de conveniência: o add-on do RenoDX lê o `HookPoint`
uma única vez, quando carrega. **Os dois nunca ficam ligados juntos** — isso derruba o `evaluate` do
SuperSampling em `0xC0000005` e a cena segue sem filtro nenhum; o painel grava as duas chaves na mesma
ação, o arranque as reaplica, e o feeder recusa subir o motor próprio se a chave do outro estiver ligada.

**TAMANHO DA REDE NEURAL** (só no motor próprio): `Leve 65%` · `100%` · `125%` · `150%` · `200%`. Não é
a "Resolução do DLSS" da primeira vista — aquela é o tamanho em que a **cadeia inteira** trabalha; esta é
o tamanho em que **só a rede** roda. É **um controle só** porque as chaves são um par: `nr_escala` só age
com `nr_reduzida=1`, então "rede leve" e "supersampling da rede" são o mesmo par em degraus diferentes —
dois controles separados se apagariam um ao outro. `Leve 65%` rende +21% a +34% de fps com 4 a 10 passes;
acima de 100% custa caro (200% ≈ 2,4× o custo) **e a rede age menos** na imagem.

O painel lê `dlss5-feed.status` (chave=valor, ao lado do log) e mostra `motor · rede · passes`. É de lá
que sai o cartaz da **passagem segura**: sem esse arquivo o aviso do feeder morria no `ReShade.log` e a
pessoa via a imagem sem filtro sem saber por quê. `estado=ok` com `motivo` preenchido quer dizer "a cadeia
está filtrando, mas o motor pedido não subiu" — e o cartaz diz isso, não "parou".

### No módulo AMD o perfil vai para o mod, e metade dele não existe

No pacote AMD quem faz o Neural Rendering é o mod **DLSS-NR-on-AMD**, de terceiro — o add-on do
RenoDX nem entra na pasta. Até a 1.2 o painel gravava o perfil no bloco `[RenoDX.DLSS5]` do
`ReShade.ini` **mesmo ali**, onde ninguém lia: os controles pareciam funcionar e não chegavam à
rede. Agora eles vão para `[DlssNrOnAmd]` do `dlssnr_on_amd.ini`, que é quem o mod lê.

| controle do painel | chave do mod |
|---|---|
| TOM LOCAL | `LocalTone` |
| ESTRUTURA | `LocalStructure` |
| PELE E ROSTOS | `SkinStructure` |
| máscara automática | `UseAutoMask` |
| MODELO, INTENSIDADE, PASSES | **não existem no mod** — aparecem apagados, com o motivo |
| TOM GLOBAL | existe (`ToneChannels`) e **fica apagado assim mesmo**: a ajuda do próprio mod diz que este build dele mantém esses canais desligados e que subir o valor *"mostly darkens the frame"* |

As chaves foram conferidas **no binário do mod** (o instalador e o `version.dll` v0.2.18), não
supostas. Os nossos cinco degraus (0/25/50/75/100%) entram na escala do mod por arredondamento em
0..2; o `-1` de fábrica do `SkinStructure` quer dizer **auto** ("segue a Structure"), e o limite
superior **não está confirmado** — nenhuma string do binário o nomeia. Por isso o log grava o par
"nosso valor → valor do mod" em toda gravação. A gravação troca uma chave por vez e preserva o
resto do arquivo (inclusive o CRLF), porque o mod também escreve nele pelo overlay dele.

Mexer no perfil **reabre o app**, por dois motivos: o mod só tem prova documentada de releitura ao
vivo para `Inline` e `Interop`; e a releitura dele **refaz o staging**, ou seja, mesmo funcionando
ao vivo daria um repique na imagem. E o que de fato ganha fps no modo AMD não é nada disto — é a
**Resolução do DLSS**, e o painel diz isso no rótulo daquela seção (13,5 ms por quadro só de rede
a 634x669 numa RX 9060 XT — **medido em vídeo na tela, não em jogo**, na sessão de outro PC).

**ORÇAMENTO DE ESPERA POR QUADRO** (`[−] 25 ms [+]`, passo 5, de 5 a 200) é o `InlineWaitMs` do
mod: quanto um quadro espera pela rede antes de o mod mostrar o retoque do anterior em vez de
segurar a imagem. **Menor = mais fluido, com resíduo do quadro anterior quando a rede atrasa;
maior = mais fiel, menos fps.** 25 ms é o valor comprovado numa RX 9060 XT; o mod nasce em 200.
Só aparece se o mod instalado conhecer a chave (v0.2.18 em diante) — e essa conferência não é por
número de versão: o app procura a chave no `dlssnr_on_amd.ini` e, se não achar, o nome dentro do
`version.dll`. O mod não carimba `VERSIONINFO`, então comparar versão não era possível de
verdade.

### Modo comparativo (para gravar o antes e o depois)

Em **Mais opções → Modo comparativo (metade sem DLSS 5)**. A tela é partida ao meio: à **esquerda** a imagem
**original** do jogo, à **direita** a mesma imagem **com o DLSS 5 aplicado** — as duas metades do **mesmo
quadro**, que é o que convence. No meio fica uma risca branca de 2 px.

Quem faz isso é a ponte (`dlss5-bridge.addon64`), em dois ganchos: no `reshade_begin_effects` o back buffer
ainda é o nativo e uma cópia inteira é guardada; no `reshade_finish_effects` a metade esquerda dessa cópia é
devolvida por cima do resultado. Custo medido pelo desenho: duas cópias de GPU por quadro (~8 MB + ~4 MB a
1080p), e só com o modo ligado.

No alto de cada metade vai um **selo**: as imagens `marca\comparar-original.png` e `marca\comparar-dlss5.png`,
ao lado do executável. São carregadas do arquivo (dá para trocar a arte), redimensionadas para **9% da altura
da tela** (`[comparar] rotulo_altura`, de 5 a 15) com margem de 1,5%, e desenhadas **sem esticar**. Faltando
os PNGs entra um letreiro de reserva escrito na hora — cartão escuro com a palavra `ORIGINAL` / `DLSS 5`. Os
selos são copiados, não misturados: não há transparência valendo, e em back buffer HDR (10 ou 16 bits) eles
não aparecem (o `ReShade.log` diz).

**As duas metades são do mesmo instante, e isso foi medido, não suposto.** Bateria de 45 fotos (parado,
movimento a 8 px/quadro e movimento com frame skipping, contra `[comparar] atraso` 0/1/2), medindo o degrau
na emenda dividido pelo degrau típico da imagem: sem compensação a razão fica em **0,87** com a cena andando
e **0,40** com skipping (1,00 = emenda tão suave quanto o resto); compensar um quadro leva a 1,78 e 2,97, ou
seja, **piora**. O salto que às vezes aparece numa foto é rasgo da captura (`PrintWindow` pegando a janela no
meio da troca de quadro), não defasagem da cadeia. `[comparar] atraso` fica disponível, com padrão `0`.

**No quadro sintetizado do DLSS FRAME SKIPPING as duas metades ficam iguais**, e isso não tem conserto aqui:
naquele quadro a cadeia inteira está suprimida — não existe "com" e "sem", existe um quadro desenhado por nós.
Para gravar comparação, deixe o frame skipping em **Desligado**.

### Modos de processamento: Automático, Nativo, Universal — e o Modo anti-cheat

O DLSS 5 For All é um só; o **modo** é só o caminho por onde a imagem chega até ele. Todas as funções do painel
funcionam igual nos dois.

- **Universal** — captura a imagem da tela e aplica o DLSS 5 sobre ela, como um filtro, numa camada nossa.
  Compatível com praticamente qualquer aplicação exibida na tela; não interage com o jogo. Indicado para jogos com
  anti-cheat.
- **Nativo** — integra-se ao jogo: uma DLL nossa de 64 bits é carregada no jogo já em execução, o quadro é
  processado na GPU antes da apresentação na tela e o próprio jogo mostra o resultado (a nossa camada some). Menor
  custo de desempenho e menor latência. Disponível para jogos de 64 bits em DirectX 11 e 12, sem anti-cheat. Nada é
  gravado na pasta do jogo e o jogo não reinicia; ao desligar o modo, a DLL se descarrega sozinha.
- **Automático** (padrão) — utiliza o modo Nativo quando disponível; caso contrário, o Universal. A captura sobe
  primeiro (a imagem aparece) e uns 2 s depois do primeiro quadro o app tenta o Nativo por baixo. Se entrar, a
  camada some; se não, a captura simplesmente continua e o motivo vai para o log
  (`modo: Nativo indisponivel nesta fonte (...)`).

A escolha fica no card **Modo** do painel (End), com as três posições; o selo ao lado diz qual transporte está em uso
agora. A posição Nativo fica esmaecida quando não está disponível nesta fonte (jogo de 32 bits, anti-cheat
detectado, a integração falhou); o motivo aparece como dica ao passar o mouse. **F8** troca Nativo ↔ Universal na mão
a qualquer momento, sem mudar a escolha gravada.

O **Modo anti-cheat** é um botão na tela de escolha da fonte (em OPÇÕES). Recomendado para jogos online: mantém
apenas a captura de tela, sem qualquer interação com o jogo, como um gravador de gameplay — o Nativo fica desligado,
a peça do limitador de fps não é instalada na pasta do jogo (uma que já esteja lá é removida) e o mapeamento de mouse
(F9) não entra. O caminho reserva das teclas (Raw Input, só leitura) fica. Aumenta a chance de não ser detectado por
sistemas anti-cheat, mas não garante que isso não possa ocorrer. Quando o app encontra um anti-cheat conhecido na
pasta do exe capturado, ele liga este modo sozinho para essa fonte: o botão mostra isso, sem gravar como escolha sua,
e o log diz `anti-cheat: modo ligado sozinho para esta fonte`.

### Chaves novas do `dlss5forall.ini`

| chave | seção | padrão | o que faz |
|---|---|---|---|
| `fps_teto_pelicula` | `[ultima]` | 60 (chave ausente) | o número da FLUIDEZ DO DLSS 5 NA TELA; `0` = destravado |
| `fluidez` | `[ultima]` | ausente = automático | força os quadros projetados (2 ou 3); só vale se for maior que 1 |
| `perfil_reabre` | `[ultima]` | ligado | `0` não reabre o app ao mudar o perfil/passes |
| `atalho_reserva` | `[ultima]` | ligado | `0` desliga o caminho reserva das teclas (Raw Input) |
| `alterna_modo` | `[ultima]` | `sintetizar` | como o quadro pulado é preenchido: `sintetizar`, `segurar`, `alfa`, `regiao` |
| `ligado` | `[comparar]` | `0` | o modo comparativo; é o que o chip do painel grava |
| `divisor` | `[comparar]` | `50` | onde fica a risca, em porcento da largura (aceita 5 a 95) |
| `rotulos` | `[comparar]` | `1` | `0` esconde os selos ORIGINAL / DLSS 5 FOR ALL |
| `rotulo_altura` | `[comparar]` | `9` | altura dos selos em % da altura da tela (5 a 15) |
| `atraso` | `[comparar]` | `0` | quantos quadros o lado esquerdo recua; medido, 0 é o alinhado |
| `motor` | `[ultima]` | `renodx` | `proprio` usa o motor de rede do feeder (experimental) |
| `recursos_experimentais` | `[ultima]` | ausente | `1` mostra o interruptor do motor, a régua do tamanho da rede e o orçamento de espera do mod AMD (experimental, volta na próxima versão) |
| `LocalTone`, `LocalStructure`, `SkinStructure`, `UseAutoMask` | `[DlssNrOnAmd]` do `dlssnr_on_amd.ini` | do mod | **módulo AMD**: é para onde vão TOM LOCAL, ESTRUTURA, PELE E ROSTOS e a máscara |
| `nr_escala` | `[ultima]` | `100` | o degrau aceso do TAMANHO DA REDE (65, 100, 125, 150, 200) |
| `modo_processamento` | `[ultima]` | `auto` | o modo de processamento: `auto`, `nativo`, `universal` (o card Modo grava; o F8 não) |
| `anticheat` | `[ultima]` | `0` | `1` = Modo anti-cheat (o botão da tela de escolha da fonte grava) |
| `ponte_modo` | `[ultima]` | `pipelined` | só no Nativo: `sincrono` espera o quadro processado no mesmo Present (menos latência, mais custo) |

## Idioma

A interface toda existe em **português e inglês**, e o app decide sozinho na primeira execução pelo idioma
do Windows (`GetUserDefaultUILanguage`): Windows em português abre em português, qualquer outro abre em
inglês. A partir do momento em que a pessoa escolhe, a escolha dela manda — fica em `[idioma] lingua=pt|en`
do `dlss5forall.ini`.

Troca-se em dois lugares, e nos dois **vale na hora**, sem reiniciar: na primeira linha do **seletor de
fonte** (a primeira tela, ao lado de *Fonte:*), num par `Português | English` que fica sempre à vista — sem
menu para abrir e sem clique intermediário —, e na seção **IDIOMA / LANGUAGE** do painel. No seletor a janela
se refaz inteira no idioma novo; no painel o cartaz, a barra de título e o menu da bandeja acompanham na
mesma hora.

O que **não** muda de idioma, de propósito: as mensagens do `dlss5forall.log` (são ferramenta de
diagnóstico e há documentação que depende do texto exato), as chaves do `.ini` e os nomes de técnica, shader
e arquivo do ReShade. A única exceção no log é a linha `FATAL:`, que é a cópia do que apareceu na caixa de
erro. Os textos do convite (`ofertas.ini`) têm as chaves de sempre para o português e as mesmas com sufixo
`_en` para o inglês; sem a chave do idioma em uso, vale o texto embutido, que já vem traduzido.

**Cor (LUT) e nitidez saíram do painel.** Eram chips que escreviam nos shaders `DLSS5_Grade.fx` e
`DLSS5_Sharpen.fx` pelo add-on `dlss5-bridge.addon64`. Os shaders, a textura das LUTs e os
`preset-02..05-*.ini` continuam no pacote: quem quiser mexer liga a técnica pelo overlay do ReShade
(a tecla do F7) e ajusta lá mesmo.

### O que roda de verdade na cadeia

O preset `FafaDLSS5.ini` carrega **três técnicas**, e elas não são intercambiáveis:

| Técnica | Papel | Dá pra tirar? |
|---|---|---|
| `Lumenite_Kernel` | fabrica o *depth* | **não** — sem ele o neural não sobe |
| `DLSS5_Feed` | fabrica os *motion vectors* e entrega o par ao DLSS | **não** — é o contrato do Neural Rendering |
| `DLSS5_Sharpen` | pós-nitidez (`Sharpness=0.35`) | **sim** — é gosto, não requisito |

#### Os controles do add-on neural: quais valem e quais não

O painel do `DLSS 5 Neural Rendering` (tecla Home, no ReShade) mostra mais controles do que esta
build usa de fato. Medido em 6 de setembro de 2026 nesta cadeia:

| controle | efeito aqui |
|---|---|
| `NRStyle` | **vale, e é o que mais muda a imagem.** `0` (Default) é o padrão do app; `1` (Natural) comprime as altas-luzes em 10,5% e, como a periferia da cena costuma ser mais clara que o meio, isso se lê como uma **vinheta escura** em volta da imagem. Se você viu essa vinheta, era isto. |
| `NRIntensity`, `NRLocalStructure`, `NRSkinStructure`, `NRLocalTone` | valem |
| `NRGlobalTone` | **sem efeito nesta build** |
| `NREnableUpscaling` | **sem efeito nesta build** — aqui a rede trabalha em resolução plena |
| `NRTransferStrength` | **sem efeito nesta build** |

O app grava `NRStyle` e `NRLocalTone` no `ReShade.ini` **toda vez que abre**, a partir de
`[ultima] nr_estilo` e `[ultima] nr_tom_local` do `dlss5forall.ini`. Isso existe porque o
*AutoSavePreset* do ReShade grava no `ReShade.ini` o que estiver na memória quando ele fecha, e
foi assim que um `NRStyle=1` apareceu num pacote sem ninguém ter escolhido — a imagem mudava de
estilo entre uma sessão e outra sem explicação. Para usar outro estilo, troque no `dlss5forall.ini`:
o valor de lá é respeitado em todas as aberturas.

O Neural Rendering em si não é técnica do preset: quem o roda é o add-on `renodx-dlss.addon64`.
Ele é também **a parte cara** — é por isso que o painel tem um botão só para ele, separado do filtro:
a diferença de fps ao desligá-lo é a medida do que ele custa nesta máquina. O custo do resto está no
`dlss5-feed.log` de cada sessão, nesta forma: `600 frames: feed CPU 3.36 ms/frame … feed is 6% of the
frame`.

O **Neural Rendering** é o primeiro item do painel e o motivo do app existir: o estado real (ativo / iniciando /
desligado) aparece em destaque, e o app avisa alto se ele não subir.

**Ficou pesado?** O filtro custa GPU (o Neural Rendering é a parte cara). `F6` desliga na hora (e religa tudo do jeito que estava) e o jogo
continua rodando normal por baixo — ele nunca parou; o que você via era a nossa entrega. Ligar o **DLSS FRAME SKIPPING** em "1 de cada 2" corta o trabalho da rede pela metade, e baixar o número da
**FLUIDEZ DO DLSS 5 NA TELA** devolve GPU ao jogo. O app avisa sozinho, uma vez, quando percebe que está apresentando muito
abaixo do que captura.

**HDR**: quando o monitor do alvo está em HDR, o app captura em float16 e converte para SDR no próprio shader,
**automaticamente** — funciona com o jogo já aberto, sem mexer no display. Conteúdo HDR de verdade (jogo com HDR
ligado) deixou de sair estourado em branco. A caixa *Desligar o HDR do monitor enquanto captura (plano B)* é
opcional: desliga e restaura ao sair (inclusive se o app for morto), mas jogo já aberto costuma ler o HDR só na
inicialização. Na placa de captura a caixa confirma a conversão PQ/BT.2020. Medições em `docs/hdr.md`.

**Tela cheia exclusiva não funciona** — o Windows não deixa a captura alcançar um swapchain exclusivo, e a
nossa camada não aparece por cima dele. Jogo em **janela** ou **borderless** (a maioria dos jogos novos só tem
borderless). Vale para as duas fontes, janela e monitor.

**Se o jogo mudar de tamanho** (janela redimensionada, troca de resolução): o filtro continua — o backbuffer fica no
tamanho em que nasceu (mexer nele com a cadeia NGX viva derruba o Feeder), a imagem nova é encaixada nele, a captura
é recriada se o Windows a encerrar, e o painel oferece "Reabrir o app na resolução nova".

**Resolução interna** — foi construída e medida (`docs/supersampling.md`): o neural **não** melhora rodando em backbuffer
maior; só texto/HUD ganha um pouco, ao custo de fps do jogo. Retirada do app por decisão do autor.

**Presets** (troque no overlay do ReShade): `preset-01` núcleo com `IMAGE_SPACE=1` · `02` grade neutra ·
`03` grade cinema · `04` brilho contido · `05` nitidez. Todos deixam `FafaDLSS5.ini` intacto. Do `02` ao `05`
são material para o overlay do ReShade — desde que cor e nitidez saíram do painel, nada no app os seleciona.

**Tela do grupo**: abre na hora em que o app é aberto (antes do seletor), a cada execução, com QR e botão. Não
fecha sozinha — a pessoa fecha em `Agora não`, no X ou com Esc — sem contagem regressiva, como o aviso do WinRAR. Tudo configurável em
`ofertas.ini` (link, textos, `mostrar=sempre|primeira_vez|0`).

## Requisitos

- Windows 10 1903+ / 11 64-bit (a captura de janela usa Windows Graphics Capture; câmera precisa do Media Feature Pack no Windows N)
- GeForce RTX com driver recente (a 4080 roda; em 1080p espera cair pra ~metade do FPS com o NR ligado)
- Visual C++ Runtime 2015-2022 x64 (o script instala se faltar)

## Instalação (uma vez)

1. Descompacta tudo numa pasta **sem acento e fora de Arquivos de Programas** (ex.: `C:\DLSS5ForAll\`).
2. Botão direito em `setup-dlss5.ps1` → **Executar com o PowerShell**
   (ou `powershell -ExecutionPolicy Bypass -File .\setup-dlss5.ps1`).
   Ele monta a cadeia inteira ao lado do exe:
   - ReShade (build com add-ons) instalado no `DLSS5ForAll.exe` → `dxgi.dll`
   - `ReShade.fxh`, `ReShadeUI.fxh`, `DrawText.fxh`
   - `dlss5-feed.addon64` + `DLSS5_Feed.fx` (repo legítimo: `jlrouzies-fr/DLSS5-Feeder`)
   - LumeniteFX (`lumenite_*.fx`, `include\`, blue noise)
   - `nvngx_dlss.dll` copiado do driver NVIDIA ou de um jogo
   - `ReShade.ini` + preset `FafaDLSS5.ini` já com `DLSS5_MV_PROVIDER=3` e as técnicas na ordem certa
3. **Você** coloca `renodx-dlss.addon64` + `nvngx_dlssnr.dll` ao lado do exe (instalador RHI do RankFTW ou canal #DLSS5 do Discord do RenoDX).
   O script para e espera por eles. São o código vazado da NVIDIA — o script não baixa isso por você.
   **Se abrir o app sem eles**, o seletor mostra uma faixa dizendo o que falta — e a conferência inclui o
   `nvngx_dlss.dll` do passo 2, que o script copia mas pode não achar: sem ele a sessão NGX não abre, o
   feeder cai na passagem segura e a tela fica idêntica à original, sem erro nenhum. Há um botão que abre as
   releases do RHI e outro que abre a pasta onde os arquivos vão. Instalou com o seletor aberto? Clique de
   volta na janela e a faixa some sozinha. O app continua abrindo e mostrando a imagem sem eles — o que não
   acontece é o realce do DLSS 5, e era exatamente essa a confusão: a imagem sai igual à original e parece
   que o programa não funciona.

## Uso

1. Abre `DLSS5ForAll.exe`. No seletor escolhe a **fonte** (câmera / janela / monitor), o alvo e as opções. **INICIAR**.
   A última escolha fica salva em `dlss5forall.ini`.
2. Na janela: **Home** abre o ReShade (no modo sobreposto, antes abre o painel com **End** e usa o botão de mouse/teclado, ou `Ctrl+Alt+I`).
   - Aba *Home*: `LUMENITE: Kernel 2.0` ligado **acima** de `DLSS 5 Feed` ligado (o preset já faz isso).
   - Aba *Add-ons* → `DLSS 5 Feed`: tem que mostrar `feature ready … DLAA` e `frame N delivered`.
   - Painel do add-on neural (`DLSS 5 Neural Rendering`): liga o NR. **F6** alterna liga/desliga — é o antes/depois.
3. Teclas do app (modo janela normal): **F11** tela cheia sem borda · Alt+Enter também alterna tela cheia · sair: **Alt+Esc** duas vezes, X da janela, bandeja ou painel (Esc nunca fecha o app).

Linha de comando:

```
DLSS5ForAll.exe --device 0 --mode 3 --fullscreen        # câmera / placa
DLSS5ForAll.exe --source window --window cyberpunk      # espelho de uma janela
DLSS5ForAll.exe --window cyberpunk --overlay            # colado por cima do jogo
DLSS5ForAll.exe --source monitor --monitor 1            # monitor numa janela
DLSS5ForAll.exe --source monitor --monitor 1 --overlay  # filtro por cima da tela inteira
DLSS5ForAll.exe --list-windows                          # lista janelas e monitores no log
DLSS5ForAll.exe --list                                  # lista câmeras e modos no log
```

Flags extras: `--full-window` (não recorta a barra de título), `--cursor` (desenha o mouse),
`--no-clickthrough` (overlay recebe o mouse), `--hide-self` / `--no-hide-self`
(tira a nossa janela da captura — ligado por padrão na fonte monitor), `--capture-border` (borda amarela do Windows).

## Frame generation com o Lossless Scaling

O DLSS 5 aqui faz Neural Rendering, **não** gera quadros. Quem pode dobrar o fps é o Lossless Scaling (LS),
rodando **por cima da nossa janela** — ele captura o `DLSS5ForAll.exe` já filtrado e interpola em cima disso.
Não precisa de nenhuma alteração no app; precisa da configuração certa.

Ordem da cadeia:

```
jogo (janela / borderless)
  -> nossa captura WGC da janela do jogo
  -> ReShade: Lumenite Kernel -> DLSS5_Feed -> RenoDX Neural Rendering
  -> Present do DLSS5ForAll.exe
  -> Lossless Scaling captura a NOSSA janela e interpola
```

As duas capturas não brigam: nós pegamos a janela do jogo, o LS pega a nossa. Não há espelho.

**Como usar**

1. Abre o DLSS5 For All com fonte **Janela**, apontando pro jogo, e **SOBREPOR desmarcado** (modo janela separada).
   Overlay e fonte monitor **não servem** aqui — veja as armadilhas abaixo.
2. Deixa a **nossa** janela em foco e aperta o atalho de *Scale* do LS. O alvo dele é o `DLSS5ForAll.exe`;
   se apontar pro jogo, ele interpola a imagem **sem** o filtro.
3. Desliga outras overlays (GeForce Experience, Discord, etc.) — qualquer coisa por cima da janela do LS quebra o FG.
4. Não usa limitador de fps externo. O limite, se houver, é no jogo.

**Armadilhas (as duas são do nosso lado)**

| Situação | Por que quebra |
|---|---|
| Fonte **monitor** (qualquer modo) | a nossa janela fica com `WDA_EXCLUDEFROMCAPTURE` e o LS enxerga tela preta |
| Modo **sobreposto** | a nossa camada reafirma o topo a cada 1 s e briga com a janela de saída do LS |
| Apontar o LS pro **jogo** | ele interpola o frame cru, sem passar pela cadeia DLSS 5 |

**O preço:** o LSFG em 2x custa cerca de **1,5 a 2,5 frames** a mais, somados aos 2-3 frames que a nossa cadeia
já tem. A 60 fps de entrada, algo entre +25 e +42 ms. Serve pra jogo lento e pra vídeo; não serve pra competitivo.

O LS é software proprietário que você comprou na Steam — nada dele é redistribuído aqui, só a receita de uso.

## Gravando o resultado

O ReShade aplica tudo **no Present** da nossa janela, então quem grava tem que capturar a janela já apresentada:

- **OBS → Captura de Janela** (método Windows 10 / WGC) apontando pro `DLSS5ForAll.exe`. Não usa *Captura de Jogo*.
- **NVIDIA App / ShadowPlay** também funciona.
- Se a fonte for **monitor** com "esconder esta janela da captura" ligado, o OBS **não vê** a nossa janela —
  use dois monitores (jogo num, DLSS5 For All no outro) ou capture por janela.
- Para o vídeo de **antes e depois**, ligue o **Modo comparativo** (Mais opções): as duas metades saem no mesmo
  quadro, já com os selos, e não é preciso cortar nada na edição. Deixe o DLSS FRAME SKIPPING desligado.

Latencia: medida nesta maquina, do frame capturado ao pixel na tela, **18-21 ms com a GPU livre** (era ~121 ms
antes do swapchain waitable com `SetMaximumFrameLatency(1)`). O Neural Rendering acrescenta +3 a 4,5 ms a 1080p e
~10 ms a 3440x1440 — e o custo dele, irredutivel sem perder imagem. Nenhum knob (V-Sync, teto de fps, sliders,
resolucao de trabalho, topmost, tela cheia) mede melhor que o padrao atual; medicoes em `docs/latencia-nr.md`. Da pra jogar coisa lenta e assistir video; pra
competitivo, nao. Medicoes e metodologia em `docs/latencia.md`.

**Se a captura estiver presa em 50 fps:** monitor virtual instalado (Meta Virtual Monitor, Parsec Virtual
Display, Virtual Desktop) faz o WGC entregar num ritmo fixo de 20 ms, independente do jogo. Custa ~10 ms de
latencia e metade da fluidez. Desativa os adaptadores virtuais no Gerenciador de Dispositivos (ou pelo menos
poe o monitor real como principal) e confere a linha `LAT ritmo do WGC` no `dlss5forall.log`.

## Limitações honestas

- **Fullscreen exclusivo não funciona.** Põe o jogo em janela ou borderless — é o modo que o WGC e a overlay enxergam.
- Nada é injetado no processo do jogo (nem hook, nem DLL): a cadeia roda toda dentro do `DLSS5ForAll.exe`.
  Mesmo assim, anti-cheats agressivos podem não gostar de overlay topmost — em jogo online, use por sua conta.
- Motion vectors são **estimados por optical flow**, não vêm da engine. Movimento rápido e HUD podem fantasmar.
- Resolução do backbuffer = resolução do recorte. 1080p é o ponto ideal na 4080; 4K quadruplica a carga.

## Problemas comuns

| Sintoma | Causa provável | Faz o quê |
|---|---|---|
| Overlay do ReShade não aparece com Home | a tecla foi trocada, ou o `dxgi.dll` não carregou | Confere a tecla em **Atalhos…** (linha "Abrir o ReShade"), ou `[INPUT] KeyOverlay` no `ReShade.ini`. Se estiver certa: renomeia `dxgi.dll` → `d3d11.dll` e confere o `ReShade.log`. |
| No modo sobreposto o teclado não faz nada | é click-through de propósito | painel (**End**) → botão de mouse/teclado, ou `Ctrl+Alt+I`; o mesmo de novo pra voltar a jogar |
| A overlay some quando clico fora | comportamento normal | ela só aparece com o jogo em primeiro plano |
| A overlay fica atrás do jogo | jogo topmost brigando | o app reafirma o topo a cada 1 s; se persistir, roda o jogo em janela normal |
| Imagem nítida parada mas borra ao mexer | motion vectors não chegando | Overlay → seção *Motion vectors* do Feeder. Kernel **acima** do Feed e `DLSS5_MV_PROVIDER=3`. |
| A imagem sai igual à original, "não faz nada" | falta um de `renodx-dlss.addon64`, `nvngx_dlssnr.dll` ou `nvngx_dlss.dll` | O seletor avisa numa faixa **dizendo qual** e leva às releases do RHI. Sem o `nvngx_dlss.dll` a sessão NGX nem abre e o feeder cai na passagem segura — o sintoma é exatamente "igual ao original", sem erro. |
| Painel DLSS 5 em STANDBY | add-on perdeu o primeiro create | O Feeder recria sozinho depois de ~180 frames. Espera. |
| "0.0 fps (alvo parado)" | a janela capturada não redesenhou nada | normal em janela estática; num jogo o número sobe |
| Fatal: "dispositivo em uso" / nenhum modo (câmera) | outro app segurando a captura | Fecha OBS/Discord/navegador/app Câmera, clica em *Atualizar lista*. |
| Nada abre, sem janela | olha `dlss5forall.log` | Ele diz onde parou (MFStartup, D3D11, shaders, WGC…). |

## Sobre os arquivos

- `DLSS5ForAll.exe` — o app (C++ puro, Win32 + Media Foundation + D3D11 + WinRT/WGC, sem dependências além do Windows).
- `main.cpp` — janela, renderer, seletor, captura de câmera. `screen_capture.h` — módulo de captura de janela/monitor.
- `idioma.h` — os dois idiomas da interface: `T(L"português", L"english")` no ponto de uso, detecção e persistência.
- `build-mingw.sh` — recompila com o MinGW-w64 do msys2 (`./build-mingw.sh`).
- `setup-dlss5.ps1` — monta a cadeia.
- `dlss5forall.log` / `dlss5-feed.log` / `ReShade.log` — os três logs que importam quando algo dá errado.

**O que precisa ir no zip** (além do que o `setup-dlss5.ps1` baixa): `DLSS5ForAll.exe`, `dlss5-bridge.addon64` (ponte do
painel — é por ela que o F6 alterna o neural sem teclado), `reshade-shaders\Shaders\DLSS5_Grade.fx` e `DLSS5_Sharpen.fx`
(opcionais, para quem quiser grading pelo overlay do ReShade),
`reshade-shaders\Textures\dlss5_grade_luts.png`, `marca\comparar-original.png` e `marca\comparar-dlss5.png`
(os selos do modo comparativo — sem eles o modo funciona, mas com letreiro de reserva),
os `preset-0*.ini`, `ofertas.ini`, `README.md` e `setup-dlss5.ps1`.
O `ReShade.ini` e o `FafaDLSS5.ini` o script gera; `dlss5forall.ini` nasce na primeira execução. Não distribua `nvngx_dlssnr.dll`.

Da 2.0 em diante vale a licença do arquivo `LICENSE` (redistribuir o original com crédito é permitido; versões modificadas ou com outra marca não); as versões até a v2026.09.04 continuam MIT. O app não contém nenhum código da NVIDIA. O que ele hospeda (ReShade, Feeder, LumeniteFX, RenoDX, o DLL vazado)
tem cada um sua licença e seu risco — o DLSS 5 nesse estado é um build interno que caiu do NBA 2K27, e a Take-Two já
derrubou coisa por copyright. Faz o vídeo, deixa claro que é experimento com build vazado, e não redistribui o `nvngx_dlssnr.dll`.

---

**DLSS5 For All — por PC do Fafa.**
Canal: <https://youtube.com/@pcdofafa> · Download e novidades: <https://pcdofafa.com.br/dlss>