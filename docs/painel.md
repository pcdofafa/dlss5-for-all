# O painel na tela (HUD) — como funciona e o que custou descobrir

Tudo o que está aqui vive no `main.cpp`, no `namespace Hud` e nas funções soltas em volta
dele (`PokeReShade`, `PumpMessages`, `HandleHotkeyMsg`, `AbrirGrupoOfertas`, os wrappers
de HDR). Este documento é para quem for mexer nisso depois — em especial as três coisas
que parecem certas no código e não funcionam na prática.

---

## 1. Por que o painel é uma janela separada, e não desenho no nosso pipeline

A tentação é desenhar o HUD dentro do nosso `DrawAndPresent`. Não dá:

- o que entra no swapchain **passa pelo filtro** (LumeniteFX → DLSS5 Feed → RenoDX). O
  painel sairia borrado, com halo, e alimentaria os motion vectors com lixo;
- o que entra no swapchain **entra na captura**. No modo sobreposto o OBS grava a nossa
  janela: o painel apareceria na live.

Então o painel é uma janela própria: `WS_POPUP` + `WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
WS_EX_LAYERED`, com `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` aplicado — o mesmo
padrão do cartaz de aviso (`ShowToast`), com uma diferença deliberada: **o cartaz é
click-through (`WS_EX_TRANSPARENT | WS_EX_NOACTIVATE`), o painel não é**. Ele precisa
receber mouse e teclado.

Consequência do `EXCLUDEFROMCAPTURE`: **o painel não aparece em print de tela nenhum**,
nem em `BitBlt`, nem em `PrintWindow`. Para conferir o visual existe
`DLSS5ForAll.exe --panel-shot arquivo.png`, que grava o painel em PNG usando exatamente o
mesmo código de desenho. Para gravar um tutorial mostrando o painel, a variável de
ambiente `DLSS5_PANEL_NA_CAPTURA=1` desliga a exclusão.

### Desenho

`UpdateLayeredWindow` sobre um DIB de 32 bits **premultiplicado** (`PixelFormat32bppPARGB`),
pintado pelo GDI+. É o único caminho que dá alpha por pixel de verdade: cantos com
antialias, sombra macia e fundo translúcido sem franja preta. `SetLayeredWindowAttributes`
não serve — ele aplica alpha uniforme e os cantos ficam serrilhados.

`Render(Graphics* g)` faz **layout e desenho na mesma passada**: com `g == nullptr` ela só
mede e preenche a lista de áreas clicáveis; com `g != nullptr` ela desenha. Não existe a
chance clássica de o retângulo do clique sair do lugar do retângulo desenhado.

### DPI

O processo é `PER_MONITOR_AWARE_V2` (a janela de vídeo tem de ser 1:1 com a captura), então
ninguém escala nada por nós. Mas o painel é **interface**, não vídeo: num monitor a 125% ele
sairia 20% menor que o resto do sistema. Todo o layout é escrito em unidades lógicas (96
dpi) e a escala entra de uma vez só, num `ScaleTransform`; o hit-test divide por
`g_scale`. Se a altura não couber na área útil do monitor, a escala cai até caber (piso de
70%).

---

## 2. Mandar tecla para o ReShade: as três armadilhas

O painel precisa apertar `End` (abre o overlay do ReShade) e `F6` (liga/desliga o Neural
Rendering do add-on RenoDX) em nome do usuário. Tudo isto foi medido, não deduzido.

### 2.1. A janela precisa estar em foco — e no modo sobreposto ela é click-through

No modo sobreposto a nossa janela é `WS_EX_TRANSPARENT | WS_EX_NOACTIVATE` de propósito:
mouse e teclado passam direto para o jogo. Nesse estado o ReShade não vê tecla nenhuma. A
sequência de `PokeReShade` é: fecha o painel (que está com o foco) → sai do click-through →
ativa a janela → manda a tecla → decide se volta ao estado anterior.

Para o `End` **não** voltamos ao click-through: sem isso ninguém consegue mexer no overlay
que acabou de abrir. Para o `F6` voltamos, porque é um aperta-e-pronto.

### 2.2. O ReShade só olha o teclado uma vez por Present

Esta é a que custa horas. O ReShade (e os add-ons) decidem "esta tecla foi apertada agora"
comparando o estado do teclado entre dois `Present`. Se o *down* e o *up* couberem no
intervalo entre dois Presents — e cabem, porque com o alvo parado o app apresenta a cada
100 ms — o aperto **passa despercebido**: no instante do Present a tecla já subiu.

A correção é apresentar frames com a tecla ainda em pé:

```
manda o KEYDOWN
6 x { bombeia a fila por ~45 ms; PresentNow(1) }
manda o KEYUP
```

`PresentNow()` chama o `DrawAndPresent` do renderer direto, por isso o `main.cpp` guarda um
`Renderer* g_renderer`.

### 2.3. São dois leitores de teclado diferentes, e mandar pelos dois é pior

| destino | como ele lê | o que funciona |
|---|---|---|
| ReShade (`End`) | fila de mensagens da janela | **`PostMessage`** |
| add-on RenoDX (`F6`) | estado real do teclado | **`SendInput`** |

O detalhe cruel: com o **overlay do ReShade aberto**, ele *engole* o teclado do aplicativo.
A tecla sintetizada por `SendInput` se perde no caminho — o `End` abria o overlay e nunca
conseguia fechar. A mensagem postada direto na fila da janela passa nos dois estados.

Do outro lado, o add-on RenoDX não olha a fila: ele lê o estado real do teclado. Com
`PostMessage` sozinho, o `F6` nunca alternava o Neural Rendering.

**Mandar pelos dois caminhos ao mesmo tempo não resolve** — foi testado: as duas transições
podem cair em Presents diferentes, o ReShade alterna duas vezes e volta ao estado de
origem. Por isso `PokeReShade` recebe um `TeclaVia`: `VIA_MENSAGEM` para o `End`,
`VIA_TECLADO` para o `F6`.

Como medir se funcionou, sem olhar a tela: `PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)`
na nossa janela captura o que o ReShade desenhou no swapchain (inclusive o contador de FPS
dele). Contar pixels claros no canto superior esquerdo diz se o overlay está aberto.

---

## 2.5. O cartaz de boas-vindas / guia (`namespace Guia`)

O cartaz de aviso pisca e some: serve para confirmar uma ação, não para ensinar. Quem abre
o app pela primeira vez precisa de uma tela parada, escrita grande, dizendo o que cada
tecla faz. É o guia — que também **é** o cartaz de boas-vindas: uma janela só, em vez de um
toast de texto corrido que ninguém lê. Mesma tecnologia do painel (janela layered + GDI+),
com três diferenças de propósito:

- **fonte bem maior** (título 26 px, nome do atalho 19, descrição 15) — é para ser lido de
  longe, com o jogo atrás;
- **reafirma o topo a cada 120 ms**, não 250 como o painel: ele nasce junto com a camada do
  filtro, que está brigando pelo topmost nos primeiros segundos de vida;
- **mexer o mouse cancela o timer** (`g_ate = 0` no `WM_MOUSEMOVE`) — quem está lendo não
  merece ver a janela sumir na cara.

Abre sozinho por **30 s** (configurável em `[cartaz] segundos`, 3 a 300) e tem três saídas
óbvias: o **X** no canto, o botão "Entendi" e Esc/Enter/Espaço. **Abrir o painel também o
fecha** — os dois são topmost e o cartaz nasceu depois, então ele ficava por cima do painel
e os cliques caíam nele (foi assim que apareceu num teste: `WindowFromPoint` no meio do
painel respondia `DLSS5ForAllGuia`). "Não mostrar de novo" grava
`[ultima] guia_visto=1`. Pode ser reaberto pelo item "Ver os atalhos" no painel ou pelo menu
da bandeja — aberto assim, fica até a pessoa fechar (`Open(0)`).

Conteúdo: uma linha por tecla, lida do **mapa de atalhos** (§3) e da tecla do overlay no
`ReShade.ini` — nunca texto fixo —, mais o estado do Neural Rendering e a linha de HDR
quando há o que dizer. Nada de lista de quatro combinações: as duas teclas que importam,
grandes, e o resto está no painel.

`--guia-shot arquivo.png` grava o guia em PNG, pelo mesmo motivo do `--panel-shot`.

## 2.6. Onde as janelas aparecem

"Se a janela que estamos capturando está na tela 2, abre na tela 2; se a janela for grande
o suficiente, abre dentro da janela." Vale para painel, guia, cartaz e a tela do grupo:

- `AreaParaJanela(w, h)` devolve o retângulo do **alvo** quando ele comporta a janela com
  48 px de folga, senão a área útil do **monitor do alvo** (`g_alvoWin` / `g_alvoMon`);
- `PosicaoParaJanela(w, h)` centraliza nesse retângulo **e depois prende dentro da área
  útil do monitor** — sem esse segundo passo, um alvo maior que a tela (janela de jogo com
  bordas fora do monitor) joga metade do painel para fora;
- `AlturaDisponivel(w, h)` é o que decide a escala.

**A armadilha que já mordeu:** a decisão de escala e a de posição precisam ser feitas na
**mesma unidade**. Comparar `cardH` (lógico) com a altura de um retângulo em pixels físicos
fez o painel abrir em `y = -76`, com o topo fora da tela, num monitor a 125%. Agora os dois
cálculos são em pixels físicos, e a escala é recalculada antes de posicionar.

Relacionado, e não é bug nosso: a **primeira** chamada de `ShowWindow` de um processo
obedece ao `wShowWindow` herdado do processo pai. Aberto por um atalho marcado como
"minimizado" — ou por um script — o app nascia com a janela em `-32000` e o usuário não via
nada. Por isso o `ShowWindow(SW_SHOWNORMAL)` é seguido de `if (IsIconic) ShowWindow(SW_RESTORE)`
nos dois modos. E a janela do modo separado passou a ser dimensionada pela área útil do
**monitor do alvo**, não pelo `SPI_GETWORKAREA`, que responde sempre pelo primário.

## 3. Atalhos

O esquema antigo eram quatro combinações com modificador (`Alt+F8/F9/F10/F12`) que ainda
mudavam de máquina para máquina por causa da cascata. O usuário não aprendia nunca — e
tinha razão. Hoje são **duas teclas, sem modificador**:

| tecla | ação |
|---|---|
| **End** | abre e fecha o painel |
| **F6** | liga e desliga **tudo** (o antes-e-depois) |

O resto se faz no painel, com o mouse. `Home` (lida do ReShade.ini) abre o overlay do
ReShade e `Alt+Esc` sai — as duas aparecem nos textos, mas não são o que a pessoa precisa
decorar.

### O mapa (`g_atalhos`)

Cinco ações — painel, tudo, filtro, teclado pro ReShade, sair — numa tabela só, gravada em
`[atalhos]` do `dlss5forall.ini` em texto legível (`painel=End`, `tudo=F6`,
`sair=Ctrl+Shift+F9`). As três últimas nascem **sem tecla**: assim o padrão continua sendo
duas teclas, e quem quiser mais configura. `TextoAtalho(acao)` é a única fonte para
qualquer texto de interface — **nunca escreva o nome de uma tecla direto num texto**,
porque a cascata pode ter mudado e o usuário pode ter remapeado.

Só o painel tem cascata de reserva (`End → Insert → Pause → Scroll Lock → Ctrl+Alt+D →
Ctrl+Alt+P`): sem ele a pessoa fica sem porta de entrada nenhuma. As outras ações, se a
tecla estiver ocupada, ficam sem tecla e o painel faz o trabalho.

A configuração aparece em **dois lugares, com um mapa só**: a seção ATALHOS do painel e o
diálogo "Atalhos…" do seletor (Win32 comum, como o resto do seletor) — porque a pessoa abre
o app e já quer poder mudar antes de escolher a fonte. Clicar em "mudar" entra em modo de
captura, e a próxima tecla com modificadores vira o atalho: conflito com outra ação nossa é
recusado com aviso, tecla tomada por outro programa volta atrás e avisa, e há "restaurar
padrões".

Três detalhes que custaram a aparecer:

1. **A captura precisa suspender as nossas hotkeys.** Enquanto o painel (ou o diálogo)
   espera a tecla, `SuspenderAtalhos()` desregistra tudo — senão a tecla vira `WM_HOTKEY` e
   nunca chega na janela que está capturando.
2. **O mesmo vale para o `PokeReShade`.** O `F6` que sintetizamos para o RenoDX é também o
   nosso atalho de "liga/desliga tudo": sem suspender, o `SendInput` dispararia a nossa
   própria hotkey e a ação chamaria a si mesma.
3. **O mapa é registrado antes do seletor.** Sem isso o diálogo "Atalhos…" mostrava "em uso
   por outro programa" em todas as teclas — o registro ainda nem tinha sido tentado.

`WM_HOTKEY` é traduzido em um lugar só (`HandleHotkeyMsg`), usado tanto pelo loop principal
quanto pelo `PumpMessages` — senão uma tecla apertada durante uma espera do `PumpMessages`
sumiria.

### Esc não fecha o app

Em jogo aperta-se Esc o tempo todo. `RenderWndProc` não trata mais `VK_ESCAPE` com
`DestroyWindow` — herança da janela de câmera que o usuário pegou na prática. Esc fecha o
que é nosso e flutuante (painel, cartaz, tela do grupo), cada um a própria janela. Sair é
por `Alt+Esc`, pelo botão do painel, pela bandeja ou pelo X.

`Alt+Esc` é combinação reservada do Windows, mas **`RegisterHotKey(MOD_ALT, VK_ESCAPE)`
funciona** — testado nesta máquina: o `WM_HOTKEY` chega antes de o sistema trocar de
janela, então não foi preciso hook de baixo nível. Para não fechar sem querer, exige duas
pressões em 2 s (a primeira mostra um cartaz); o botão do painel pede dois cliques, com a
confirmação expirando em 4 s.

### F6 = tudo, não só o neural

`AlternarTudo` guarda o estado (neural e camada), dá o poke do neural e — no modo
sobreposto — ativa o bypass da camada. O segundo toque devolve na ordem certa: neural
primeiro, camada por último. É o antes-e-depois honesto, não um "desligar o neural".

> **Mudou (03/09/2026):** o estado guardado tinha também LUT, intensidade, nitidez e
> grading, e a função desligava as duas técnicas pela ponte antes do poke. Cor e nitidez
> saíram do painel (§4), então `EstadoFiltro` ficou com `neural`/`neuralNos`/`camada` e o
> ramo de desfazer, que devolvia as técnicas quando o poke abortava, não tem mais o que
> desfazer. O **contrato tudo-ou-nada continua igual**: poke abortado ⇒ `g_antes` volta a
> zero, `g_tudoDesligado` não é marcado, a camada não se mexe e o retorno é texto vazio.

Dois defeitos reais apareceram só testando o ciclo inteiro, e os dois deixavam o usuário
**sem neural achando que estava tudo ligado** — o pior desfecho possível num app cujo
produto é o neural:

1. **Não consultar o `NeuralUplift` do ini para decidir o religamento.** O RenoDX grava
   essa chave com atraso (AutoSave), então logo depois de desligar ela ainda diz `1`, e a
   condição "só religa se o ini disser 0" bloqueava o religamento — medido: o F6 desligava
   e nunca trazia de volta. Hoje o `EstadoFiltro` guarda `neuralNos` ("fomos nós que
   desligamos") e o religamento é incondicional e simétrico. O estado *guardado* também
   vem do painel (ini + log, atualizado por segundo), não só do ini.
2. **A tecla física chega ao RenoDX por fora da nossa hotkey.** Ele lê o estado real do
   teclado, então o `F6` do usuário pode alternar o neural sozinho *além* do nosso poke —
   em algumas situações, duas vezes, invertendo o resultado. Por isso, 2,5 s depois de cada
   F6, o app **confere** pelo `NR toggled ON/OFF` do log (fonte imediata, ao contrário do
   ini) e corrige com um poke se ficou ao contrário.

Ciclo validado: neural ligado → F6 → `NR toggled OFF` + `TUDO DESLIGADO (guardado:
neural=1 …)` → F6 → `NR toggled ON` + `TUDO LIGADO de volta`, com `NeuralUplift=1` de volta
ao original.

### "O ReShade na tela inteira sobrepõe os botões"

Relato do usuário, investigado. O que **não** é: o overlay do ReShade é desenhado dentro do
nosso swapchain, não é uma janela — ele nunca cobre o painel nem o guia. Medido com
`WindowFromPoint` no centro das duas janelas, com o overlay do ReShade aberto, no modo
sobreposto e com a janela em F11: em todos os casos quem responde é `DLSS5ForAllPanel` /
`DLSS5ForAllGuia`.

O que provavelmente é: **modo monitor sobreposto + modo interativo**. Ali a nossa camada
cobre a tela inteira, e sair do click-through faz com que ela receba *todo* clique — os
botões que aparecem embaixo dela (barra de tarefas, outras janelas) continuam visíveis,
porque a camada mostra a captura, mas param de responder. É o comportamento inevitável de
um filtro de tela cheia que precisa receber mouse para o ReShade funcionar; o que dava para
melhorar era avisar, e o cartaz do modo interativo agora diz isso quando a fonte é monitor,
com o atalho que devolve o clique ao Windows.

### Z-order e foco

- O jogo (e a nossa própria camada) reafirmam `HWND_TOPMOST` o tempo todo; o painel
  reafirma a cada 250 ms no `Hud::Tick`, senão some por trás da camada do filtro.
- `FollowTarget` esconde a camada quando o jogo perde o foco. Abrir o painel tira o foco do
  jogo — por isso a condição lá aceita `fg == g_hud.wnd`. Sem isso, abrir o painel apagaria
  o filtro.
- O painel fecha em `WM_ACTIVATE`/`WA_INACTIVE`: é assim que "clicar fora" fecha. A margem
  da sombra devolve `HTTRANSPARENT`, então clique na sombra também conta como clique fora.

---

## 4. A ponte da LUT — RETIRADA DO PAINEL (03/09/2026)

> Os chips de **Cor · LUT**, **Força da LUT** e **Nitidez** saíram do painel por decisão do
> usuário ("o programa precisa ficar mais leve pra operar"). O motivo que tornou a remoção
> barata: o preset em uso, `FafaDLSS5.ini`, tem
> `Techniques=Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_Feed@DLSS5_Feed.fx` — nem
> `DLSS5_Grade` nem `DLSS5_Sharpen` estão carregados, então os controles já não faziam nada
> e só produziam `UiSetLut recusado` / "a nitidez precisa do DLSS5_Sharpen" no log.
>
> Saíram do `main.cpp`: `CMD_LUT_BASE`/`CMD_LUTSTR_BASE`/`CMD_SHARP_BASE`, `UiSetLut`,
> `UiSetLutStrength`, `UiSetSharpen`, `UiLutStatus`, `struct LutStatus`, `kLutNames`,
> `kSharpNomes`, `kSharpNiveis`, `E_LUT`/`E_LUTSTR`/`E_SHARP`, os campos
> `lut`/`lutStrength`/`sharpen`/`lutSt` do `HudState` e o card inteiro do painel.
>
> **A ponte não foi tocada.** `rsbridge::SetLut`, `SetLutStrength`, `SetSharpen*`,
> `kGradeTechnique` e o `GetStatus` continuam no `reshade_bridge.h`, sem chamador, e o
> `DLSS5_BRIDGE_PROTOCOL` está intacto — o `dlss5-bridge.addon64` já instalado segue
> compatível com o exe. Os shaders e presets continuam no disco: quem quiser o grading liga
> a técnica pelo overlay do ReShade. O resto desta seção fica como registro do que existia.

`reshade_bridge.h` (header-only) resolve os exports do `dlss5-bridge.addon64`, que é
carregado dentro do nosso processo pelo ReShade. **Armadilha:** o header define atalhos
globais chamados `SetLut` e `SetTechniqueEnabled`. As nossas funções se chamavam `UiSetLut` /
`UiLutStatus` e sempre qualificavam `rsbridge::` — sem isso vira recursão infinita silenciosa.
A armadilha continua valendo para quem for chamar a ponte de novo.

Três estados diferentes, e o painel diz qual é qual em vez de deixar o clique sem efeito:

1. add-on ausente → "indisponível — o add-on da ponte não está carregado";
2. add-on presente mas `DLSS5_Grade` fora do preset → "o preset em uso não carrega o
   DLSS5_Grade" (o `FafaDLSS5.ini` não inclui; os `preset-02/03/04` incluem);
3. tudo certo → os chips funcionam.

Escolher uma LUT também liga a técnica (`SetTechniqueEnabled`), senão trocar de LUT com o
grading desligado pareceria não fazer nada. Uniform vale no frame corrente; ligar técnica
custa um frame — não espere confirmação síncrona. O `current_lut` do `GetStatus` é a fonte
de verdade e o painel se sincroniza com ele a cada desenho.

Medido com a cor média da janela, para provar que a ponte chega no pixel: Neutra
`R=30 G=70 B=120` → Quente cinema `27/67/112` → Fria noturna `34/72/122` → Desligada volta
ao original.

### Nitidez (mesma ponte, `DLSS5_Sharpen`)

Quatro degraus — Off / Leve / Média / Forte (0 / 0,3 / 0,6 / 0,9) — em vez de slider:
slider de 0 a 1 numa overlay briga com o mouse e ninguém precisa de 0,47 de nitidez. Dois
detalhes que vieram do agente da nitidez e estão no código:

- os setters **recusam** valor fora da faixa em vez de limitar (devolvem `false`), então o
  valor sai daqui já pronto;
- `Sharpness = 0` deixa o shader em identidade mas **o passe continua rodando e custando**.
  Por isso o degrau Off chama `SetSharpenEnabled(false)`, não `SetSharpen(0)`.

Não há campo de status para a nitidez no `DLSS5BridgeStatus`, então o critério prático é o
retorno de `SetSharpen`: `false` = a ponte não está lá ou o `DLSS5_Sharpen.fx` não está no
preset (é o `preset-05-nitidez.ini` que o traz). Medido pelo contraste local da janela:
Off `41,5` → Forte `46,6`.

---

## 4.5. Teto de fps — o que ele NÃO faz

Serve para segurar consumo, temperatura e ruído de cooler. **Não reduz a latência**: medido
com a GPU livre, o teto de 60 piorou o `PRESENT→TELA` (17 ms contra 6,8 ms sem teto). O
texto do painel diz exatamente isso, e não "devolve GPU pro jogo" — que era a promessa
antiga, nunca medida.

Há um defeito conhecido e não resolvido: o teto só pode agir quando chega frame novo, então
ele **quantiza** na taxa com que o loop consome frames (fonte a 78–80 fps, teto 60 entrega
50). Tentei corrigir escolhendo "o frame mais próximo do prazo" em vez de "o primeiro
depois do prazo": em bancada ficou **pior** (teto 30 entregou 18,8 contra ~28), e o agente
de latência explicou por quê — o preditor usava o intervalo entre *chegadas*, mas quem
manda é o período do *loop*, que com a cadeia inteira no Present é bem maior; ele espera um
frame que demora o dobro e cai um período inteiro depois do prazo. Revertido de propósito:
não vale trocar um comportamento previsível por um que não se consegue demonstrar melhor,
ainda mais numa opção que a pessoa mexe com o jogo rodando. Ver `docs/latencia-nr.md`
§4.2–4.3.

---

## 4.6. Nunca mexer no backbuffer com a cadeia viva

Esta é a regra mais importante do arquivo, e ela custou dois crashes para ficar clara:
**`ResizeBuffers` com a cadeia NGX montada mata o processo.** O ReShade recria o runtime de
efeitos inteiro quando o backbuffer muda de tamanho, e a feature do DLSS morre no meio
(`CreateFeature raised exception 0xC0000005`). Apareceu por dois caminhos independentes: ao
trocar a resolução interna pelo painel, e — pior, porque é uso normal — **quando o jogo
redimensiona a própria janela**.

Então `Renderer::FonteMudou` substituiu o antigo `Resize`: ele atualiza `srcW/srcH`, marca o
cbuffer como sujo, recria só a textura da câmera quando é o caso, e **não toca no
swapchain**. O backbuffer fica do tamanho em que nasceu e a fonte nova é desenhada nele; o
viewport vira letterbox quando a proporção muda (melhor uma tarja do que a imagem
esticada). Quem quiser o filtro rodando na resolução nova reabre o app — e o painel oferece
um botão "Reabrir o app na resolução nova", que aparece só nesse caso e relança com a mesma
linha de comando.

Junto veio um segundo problema: o WGC **encerra a sessão de captura** quando o alvo recria o
swapchain dele, e o app ficava vivo mas sem imagem nova ("A janela capturada foi fechada",
com a janela ainda aberta). Se a captura cai com o alvo vivo, ela é recriada (`Stop`+`Start`,
até 10 tentativas espaçadas de 1,5 s, e a cota se renova depois de 10 s de pé) — o que também
não encosta no backbuffer.

**Correção de 03/09/2026 (revisão crítica, item 5):** essa recriação existia mas era **código
morto**. O campo `closed` do `ScreenCapture` só virava `true` quando a janela do alvo morria —
e a condição de entrada é `!Alive() && IsWindow(alvo)`, que nesse caso nunca podia ser
verdadeira. Faltava registrar o evento `Closed` do `GraphicsCaptureItem`, que é o único aviso
que o sistema dá quando a sessão cai com a janela viva. Agora existe o `WgcClosedHandler`
(mesmo molde do `WgcFrameHandler`, registrado no `Start`, removido no `Stop`), o `closed`
virou `std::atomic<bool>` porque quem escreve nele é uma thread do sistema, e ele passou a
significar **só** "o WGC encerrou a sessão" — a janela morta já é checada pelo próprio
`Alive()`, e misturar os dois sinais escondia qual dos dois tinha chegado. `ScreenCapture::
SessaoCaiu()` expõe a diferença, e o log de queda diz os dois:
`captura caiu: Closed do WGC=1, janela ainda viva=1`.

Duas coisas foram junto, pelo mesmo caminho: o `evt` do `ScreenCapture` passou a ser criado
**antes** de qualquer `return false` do `Start`, e o `waitEvt` do laço principal é reatribuído
nos dois desfechos da recriação. Antes, um `Start` que falhasse cedo deixava o laço esperando
num handle já fechado — `MsgWaitForMultipleObjects` devolve `WAIT_FAILED` na hora e o loop
vira busy-loop de 100% de um núcleo.

Reprodução, para quem for mexer nisso:

* mudança de tamanho do alvo: `scratchpad\ss\gametarget.exe 1280 720 --redim-apos 20`. O log
  diz `fonte mudou de 1280x720 para 1120x630 - backbuffer segue em 1280x720 (sem recriar a
  cadeia)` e a rede continua avaliando. **Este alvo não derrubou a sessão do WGC** — a captura
  seguiu a janela. Não é o mesmo que dizer que redimensionar nunca derruba: o cenário que
  interessa (jogo recriando o próprio swapchain, ou entrando em tela cheia exclusiva) **não
  foi reproduzido aqui**, e é para ele que a recriação existe.
* queda da sessão com a janela viva (o caso do jogo trocando de resolução): não dá para
  provocar com o `gametarget`, então existe a chave `DLSS5_TESTA_QUEDA=<segundos>`, que
  derruba a sessão de propósito uma vez. Medido: `captura caiu: Closed do WGC=1, janela ainda
  viva=1` seguido de `captura recriada em 800x450 (tentativa 1)` 37 ms depois, com o fps
  voltando a 59,9.
* o evento `Closed` de verdade: fechar a janela do alvo. O log sai com `Closed do WGC=1`,
  o que prova que o handler está registrado e é chamado.

### São todos o mesmo problema

Vale registrar para quem pegar isto depois, porque parecem três assuntos e são um só:

| sintoma | é o mesmo bloqueio |
|---|---|
| ~~trocar a resolução interna com o app aberto~~ | `ResizeBuffers` com a cadeia viva (a funcionalidade saiu — §4.7) |
| o jogo redimensionar a janela | idem |
| **F11**: a tela cheia estica a imagem com bilinear | corrigir exigiria o backbuffer acompanhar a janela — idem |

Os que sobraram se resolvem juntos, e **fora daqui**: o conserto definitivo é um passe de
ampliação dentro do add-on (`dlss5-bridge`), que saberia recriar os próprios recursos sem
derrubar a feature do DLSS. Enquanto isso, o `main.cpp` segue a regra de nunca mexer no
backbuffer depois que a cadeia sobe — e as duas coisas ficam com a solução de contorno:
letterbox no redimensionamento e o esticão do F11 como está.

### O que o watchdog NÃO vê

Anotação do agente de supersampling, e é uma limitação real: **o vigia olha o nosso fps, não
o do jogo.** Na medição dele, com fonte 1440p a 200%, o nosso present ficou em 26,6 fps e a
taxa de chegada do alvo caiu junto para 26,6 — o jogo perdeu ~73% do fps. Nesse cenário o app
está saudável pelos três critérios (apresenta, acima de 5 fps, feature de pé), passa no teste
de sobrevivência e é aprovado, enquanto quem está jogando sente o jogo travando. Um cartaz de
confirmação de 10 s também não pegaria — em 10 s a pessoa ainda está entendendo o que mudou.

O que existe hoje contra isso é passivo e suficiente para decidir: o placar mostra
**CHEGADA** ao lado de PRESENT, e a chegada é o fps que o jogo está entregando. Quem ligar
200% e vir a chegada despencar tem a informação na cara.

---

## 4.7. Resolução interna (supersampling) — medida e RETIRADA

**Decisão do usuário, 03/09/2026: a Resolução interna saiu do app.** "Não vale a pena o
redimensionamento" — o neural não melhora e o custo sai do fps do jogo. Os números que
levaram a essa conclusão estão em `docs/supersampling.md` e continuam valendo; o que segue
é o registro de por que o código foi embora e de **o que ficou**, porque é fácil levar junto
o que não devia.

### O que foi removido

`Renderer::scale`, `ScaleDim`, `ScaleCabe`, `kMaxDim`/`kMaxPixels`, a flag `--escala`, os
chips 75/100/150/200 e os textos por degrau, `g_hud.escalaPct/escalaEscolhida/escalaCabe/
escalaPendente/escalaSegundos`, `E_ESCALA`/`E_ESCALA_OK`, `CMD_ESCALA_BASE`/`CMD_ESCALA_OK`,
a leitura e a gravação de `[ultima] escala` e `escala_pendente`, o watchdog de sobrevivência
inteiro (com os dois prazos e o teto absoluto) e os auxiliares `CadeiaMorreu`/`TamanhoFeedLog`,
que só existiam para ele. Uma chave `escala=` sobrando no ini de quem já usou é **ignorada em
silêncio** — sem cartaz, sem reversão, sem reescrever o arquivo.

Com isso, três achados da revisão crítica deixaram de existir por não haver mais código: o
**item 4** (a faixa da escala engolindo o `CMD_REABRIR`), o **item 6** (o degrau 75% que
nunca era aplicado) e o **item 7** (`escala_pendente` não limpo no encerramento normal).

### O que FICOU, e por quê

O backbuffer e a fonte continuam sendo dois tamanhos separados (`w/h` e `srcW/srcH`), e o
ampliador do pixel shader continua inteiro: `ScaleCB`, `UpFilter`, `SampleCatmullRom`,
`SampleDirecional` e `SampleArea`. **Isso não é resto do supersampling.** É o caminho de
quando o alvo muda de tamanho com a cadeia NGX viva: o backbuffer não pode ser recriado (ver
§4.6), então a fonte nova é desenhada no backbuffer antigo e quem faz a conta é o PS —
`SampleDirecional` quando a fonte ficou menor, `SampleArea` quando ficou maior, com letterbox
no viewport se a proporção mudou. Remover o ampliador junto com os chips faria esse caso cair
no bilinear do sampler, que é visivelmente pior em texto de HUD — exatamente a qualidade que
o caminho de letterbox existe para preservar.

Também ficaram o `FonteMudou` (nunca mexer no backbuffer com a cadeia viva), a recriação da
captura e o botão "Reabrir o app na resolução nova".

### A descoberta que vale guardar

Mesmo sem a funcionalidade, o achado continua útil para quem for mexer no backbuffer: trocar
a resolução em runtime funciona no papel e **derruba o app na prática**. Com a cadeia
carregada, `ResizeBuffers` faz o ReShade recompilar tudo e o DLSS 5 Feed morre recriando a
feature NGX (`creating the DLSS feature crashed`). Foi por causa disso que a escala, enquanto
existiu, só era aplicada no arranque — e é por causa disso que o `FonteMudou` existe.

## 5. V-Sync

`Present(1,0)` fixo era o limitador desde que a captura destravou (WGC a ~100 fps): ele
trava no refresh do monitor onde a janela está. Agora é `Present(C.vsync ? 1 : 0, 0)`, com
padrão **desligado**.

Ressalva que precisa estar no texto da interface: `Present(0,0)` em flip model **não
produz tearing** — para tearing seria preciso `ALLOW_TEARING`, que não usamos. Desligado
aqui significa "não espera o vblank", não "imagem rasgada". Quem usa Lossless Scaling por
cima e vir irregularidade de pacing deve ligar o V-Sync primeiro.

Com V-Sync ligado, `WaitForPresentSlot()` **sai na hora**: o `Present(1,0)` já bloqueia até
o vblank, e somar o objeto de espera do swapchain waitable daria duas esperas por frame
(correção do agente de latência). Sem V-Sync nada muda — o caminho padrão é idêntico.

## 5.1 FPS na tela — o contador não é nosso

O número que aparece no canto quando o filtro liga é o contador do **overlay do ReShade**
(`[OVERLAY] ShowFPS` no `ReShade.ini`), não do app. Todo mundo acha que é nosso porque ele
aparece junto com o filtro — e foi essa a origem do pedido "coloca uma caixa para que isso
seja opcional".

Onde se mexe, os dois lugares:

- **caixa no seletor** (`ID_FPSTELA`, ao lado de V-Sync/HDR) — a escolha vale já nesta
  abertura, porque o ReShade lê o `ReShade.ini` quando o `effect_runtime` sobe, no primeiro
  swapchain, e isso é depois de o seletor fechar (a mesma medição da tecla do overlay, em
  `docs/correcoes-entrada.md` §6);
- **chavinha no painel** (`E_FPSTELA` → `CMD_FPSTELA`), para trocar sem reabrir.

`DefinirFpsNaTela()` grava nos **dois** caminhos, nesta ordem e por motivos diferentes:

1. `WritePrivateProfileStringW` no `ReShade.ini` — é o que a próxima abertura vai ler, e
   funciona mesmo com o ReShade ausente;
2. `ReShadeSetConfigValue`, a API que o **próprio ReShade** exporta pelo `dxgi.dll`
   (conferido nos exports com `objdump -p`; a assinatura está em
   `addon/include/reshade/reshade.hpp`, e `runtime = nullptr` significa o ini global). Isso
   deixa o cache em memória do ReShade coerente — sem ela, o **AutoSave** dele pode
   regravar o ini com o valor antigo e desfazer o que acabamos de escrever.

Não usa o `dlss5-bridge.addon64`: a API é do ReShade, exportada para quem estiver no
processo, e o app já está lá dentro. Nenhum comando novo na ponte foi preciso.

**O que o texto da interface não promete:** que o contador some *na hora*. Quem redesenha o
overlay é o ReShade, e o único momento em que ele com certeza releu o `ShowFPS` é a próxima
abertura. Por isso o subtexto e o cartaz dizem "se não sumir agora, some na próxima
abertura" — verdadeiro nos dois casos, e a pessoa não fica achando que o clique falhou.

A escolha também vai para `[ultima] fps_tela` no `dlss5forall.ini` e é **reaplicada na
abertura**, antes do seletor (em `wWinMain`, logo depois do `DetectarGpu()`). Sem isso o
AutoSave do ReShade poderia devolver o `ShowFPS` dele entre uma sessão e outra e a escolha
do usuário se perderia. Chave ausente = ninguém pediu nada, e aí não se toca em
configuração alheia: o padrão continua sendo o do ReShade, que é **ligado**.

---

## 6. Avisar que dá para desligar

A cadeia é cara (a ~50 → ~30 fps a 1080p só do Neural Rendering) e o app vai para gente que
não acompanhou nada disso. Alguém vai abrir num PC mais fraco, achar que travou tudo e
desinstalar. Então:

- o cartaz inicial termina com `Ficou pesado? <atalho> desliga o filtro na hora.`, com o
  atalho que **realmente** foi registrado;
- o item do filtro no painel diz que desligar devolve o desempenho na hora e que **o jogo
  continua rodando normal por baixo** (isso não é óbvio: a pessoa acha que o app quebrou o
  jogo);
- o app percebe sozinho: `present < chegada/2` por 5 s seguidos dispara **uma vez por
  execução** um cartaz apontando as duas saídas reais (desligar o filtro, baixar o teto de
  fps). Não dispara quando há teto de fps configurado (aí o present está baixo porque foi
  pedido), nem com o filtro desligado, nem com a fonte parada (`chegada >= 20 fps`).

O critério é agnóstico à fonte, de propósito: a cadeia custa o mesmo venha o frame de
janela, monitor ou placa de captura — no monitor inteiro custa mais, porque a resolução é
maior. Para a fonte câmera/placa o texto fala em "filtro", não em "jogo": a pessoa pode
estar capturando um console.

---

## 6.5. O Neural Rendering é o produto — e o painel trata assim

O app existe por causa do Neural Rendering, então ele é o **primeiro e maior** cartão do
painel (acima do "Filtro DLSS 5", que é a camada inteira) e uma coluna do placar. E o
estado é **lido**, nunca inferido de uma flag nossa — a pessoa pode ter ligado pelo overlay
do ReShade sem passar por aqui. `LerEstadoNeural` cruza três fontes:

1. **os arquivos existem?** Sem `renodx-dlss5.addon64` ou `nvngx_dlssnr.dll` o estado é
   NÃO INSTALADO e a interface diz *qual* falta, em vez de um "desligado" enganoso;
2. **`[RenoDX.DLSS5] NeuralUplift` do ReShade.ini** — o que foi *pedido*;
3. **o fim do `ReShade.log`** (`inline feature 18 evaluation succeeded`,
   `NGX feature create intercepted: feature=1`) — o que está *acontecendo*.

Daí os três estados visíveis: **ATIVO** (avaliando), **LIGADO · INICIANDO** (pedido, ainda
sem frame pela rede — os primeiros segundos) e **DESLIGADO**. O log é lido só do fim (96 KB)
porque ele cresce muito.

Quando o neural não sobe, um cartaz avisa **uma vez por execução**, 6 s depois da abertura
(para não acusar no meio da partida), apontando a saída ou o arquivo que falta. Um app cujo
propósito é o neural não pode abrir em silêncio com ele desligado.

Depois de alternar pelo painel, o estado é **relido** e o cartaz diz o que de fato
aconteceu — se o add-on não respondeu, ele avisa em vez de mentir.

O rótulo do item diz o que se ganha e o que se perde: *"desligar = resposta rápida (~10 ms
a menos, sem o realce do DLSS 5)"*. Isso veio medido pelo agente de latência (~10 ms fim a
fim, +16 fps em 3440x1440) e é o **único** ganho real de latência da cadeia — nenhum ajuste
interno do NR chega perto. Não existe "modo baixa latência" como preset, de propósito.

---

## 7. Os dois números de fps

O placar mostra **chegada** (frames que a captura entregou) e **present** (frames que
apresentamos). Eles divergem desde que o `MinUpdateInterval` do WGC foi zerado, e a
diferença é informativa: é o custo do filtro. Quando o present cai abaixo de 60% da
chegada, o número fica laranja.

---

## 8. HDR e grupo de ofertas

Ambos são de outros módulos e entram por `__has_include`, então o `main.cpp` fecha o build
mesmo sem eles:

- `hdr_control.h` → `Hdr::Treatment`. A detecção roda sempre (é barata); o tratamento só
  se o usuário marcar a caixa — mexer no display de alguém sem pedir é invasivo. A caixa
  nasce **desmarcada**, e quando o app detecta HDR com a caixa desmarcada o painel e o
  cartaz avisam que a imagem vai chegar lavada. `HdrShutdown()` no encerramento religa o
  HDR que desligamos.

  Três pontos que não são óbvios:

  1. **A conversão PQ/HLG → SDR da placa de captura acontece no nosso `kShader`**, não num
     passe extra: um `cbuffer b0` alimentado por `Hdr::ShaderParams` a cada frame. Com
     `HdrMode == 0` o `PS` é passthrough, então quem não usa HDR não paga nada. O `PS`
     passa de 4 para ~66 slots de instrução quando a conversão está compilada.
  2. **`Fatal()` sai por `ExitProcess`**, que não roda destrutor nem `atexit`. Ele chama
     `Hdr::Manager::RestoreActive()` antes — senão um erro fatal deixaria o monitor do
     usuário com o HDR desligado.
  3. A caixa mora no ini do próprio módulo (`[hdr] tratar`), fonte única — o `main.cpp`
     não guarda uma segunda cópia dela.

  O `#include` do módulo (e o do splash) fica **no topo do arquivo**, não junto do painel:
  o seletor aparece muito antes e já usa `Hdr::Txt::Caixa()`.
- `splash_ofertas.h` → `ShowOfferSplash`. Aparece sozinho conforme a política do
  `ofertas.ini` (padrão: primeira execução) e depois disso a porta permanente é o item fixo
  no rodapé do painel, mais o item no menu da bandeja. Roda em **thread própria**: a tela
  tem loop de mensagens e contagem regressiva, e travar o loop principal congelaria o
  filtro na cara do usuário. Sem o header, o item abre o link no navegador.

---

## 9. Identidade visual

Paleta em `marca/PALETA.md` (tirada do grupo.pcdofafa.com.br): preto profundo, superfícies
`#111827`/`#1f2937`, acento ciano `#06b6d4` indo para o azul `#2563eb`, texto branco →
`#d1d5db` → `#9ca3af` → `#6b7280`, laranja `#fb923c` para aviso. O ciano marca **estado**
(ligado, selecionado, hover), não decora; o azul é o segundo acento (os chips de nitidez).
Título em caixa alta com espaçamento entre letras (`TxtSpaced`), no espírito do Oswald do
site — o GDI+ não tem letter-spacing, então vai letra por letra.

**Sem imagem nenhuma**: por decisão do usuário o painel não carrega logo. O item do grupo
é só texto, com uma barrinha ciano no lugar onde a logo estaria. Isso também tira uma
dependência de arquivo em runtime.

---

## 10. Como testar isto sem olhar a tela

O painel não sai em print, então a validação é indireta e vale registrar:

- `dlss5forall.log` registra abertura do painel (posição, tamanho, escala, nº de itens),
  cada clique (`painel: clique em x,y -> item N`) e cada `PokeReShade`;
- `PrintWindow` com `PW_RENDERFULLCONTENT` na janela de render mostra o overlay do ReShade;
- para clicar de fora, `SetCursorPos` + `mouse_event`, com o processo de teste marcado como
  DPI-aware (`SetProcessDpiAwarenessContext(-4)`) — senão as coordenadas saem escaladas e
  os cliques caem no lugar errado;
- `DLSS5_LOG_ITENS=1` despeja as áreas clicáveis em coordenadas de tela, que é como se
  clica num item específico sem adivinhar posição;
- `DLSS5_TESTA_AVISO=1` dispara o cartaz de "está pesado" na hora, para conferir o texto
  sem precisar de um PC lento;
- o linker falha com "cannot open output file … Permission denied" se houver um
  `DLSS5ForAll.exe` rodando. Não é erro de código. Quando o app do usuário está aberto,
  compile para o scratchpad com outro nome em vez de matar o processo dele — e, ao matar
  processos de teste, filtre pela linha de comando (`CommandLine -like "*ALVO-*"`).

**Duas instâncias mudam o resultado dos testes.** Com o app do usuário aberto, a segunda
instância não consegue `End`, `F6` nem `Alt+Esc` (a primeira já os tem) e cai na cascata —
foi assim que vi o painel abrir em `Insert` num teste e quase confundi com bug. O diálogo
de atalhos mostra isso explicitamente ("em uso por outro programa"), que é a informação
certa, mas convém fechar a instância antiga antes de testar.

- `DLSS5_TESTA_QUEDA=<segundos>` derruba a sessão do WGC de propósito uma vez, para
  exercitar a recriação da captura sem um jogo trocando de resolução;
- **alvo de teste**: `scratchpad\lattarget.exe` para qualquer coisa que passe de meio minuto.
  O `scratchpad\ss\gametarget.exe` **morre sozinho aos ~16 s** (código -1) e um teste longo
  em cima dele mede a morte do alvo, não o app — perdi rodadas nisso antes de descobrir;
- a tela do grupo agora abre **antes do seletor e não fecha sozinha**: todo teste automático
  precisa fechá-la (`PostMessage(WM_CLOSE)` na classe `DLSS5ForAllOfertas`) antes de o app
  chegar à captura;
- para clicar num item do painel sem depender do foco, `PostMessage(WM_LBUTTONUP)` na
  janela `DLSS5ForAllPanel` com coordenadas de cliente. Um clique de mouse de verdade
  atravessa os pixels transparentes da janela em camada (a moldura da sombra), **desativa
  o painel e ele se fecha** — o que testa a janela, não o despacho de comandos;
- o `dlss5forall.ini` com BOM UTF-8 fazia o app perder TODAS as escolhas em silêncio (ver
  §11.15). O app hoje conserta isso sozinho na abertura, mas em teste continua valendo
  escrever sem BOM (`[IO.File]::WriteAllText` com `UTF8Encoding($false)`) — senão a
  primeira execução gasta um passo consertando o arquivo e o log enche de ruído.

---

## 11. Rodada da revisão crítica (03/09/2026)

Uma revisão adversarial (`docs/revisao-critica.md`) achou onze defeitos reais. O que ficou
para a posteridade, além do §4.6 já corrigido acima:

### 11.1. Faixas de comando disjuntas (o pior deles)

Os comandos que o painel enfileira para o laço principal são um `int` só, e cada grupo era
uma base mais um valor. A intensidade da LUT começava em 350 e somava a porcentagem, então
o chip **"50%" gerava 400** — que era exatamente `CMD_ATALHO_BASE + AT_PAINEL`. Como o bloco
de atalhos é avaliado antes, clicar em "intensidade 50%" entrava em "aperte a tecla…" e
chamava `SuspenderAtalhos()`. Do mesmo jeito, a faixa da escala (`500..799`) engolia o
`CMD_REABRIR` (701): o botão "Reabrir" gravava `escala=201`.

Hoje cada grupo mora no seu próprio milhar e **`static_assert`s** travam a reincidência na
compilação. O mapa completo, como ele ficou depois da remoção da Resolução interna:

| faixa | base | largura usada | o que é |
|---|---|---|---|
| avulsos | 300–309 | 10 fixos | painel, ReShade, neural, V-Sync, HDR, ofertas, guia, tudo, sair, sair-painel |
| teto de fps | 400 | +0..4 (`kFpsCaps`) | Máx/60/45/30/20 |
| ~~LUT~~ | ~~1000~~ | — | **vaga**: os chips saíram do painel (§4) |
| ~~intensidade da LUT~~ | ~~1100~~ | — | **vaga**: idem |
| ~~nitidez~~ | ~~1300~~ | — | **vaga**: idem |
| ~~resolução interna~~ | ~~2000~~ / ~~2500~~ | — | **vaga**: a funcionalidade saiu (§4.7) |
| atalhos (capturar) | 3000 | +0..`AT_COUNT` | entra em "aperte a tecla…" |
| atalhos (fábrica) | 3100 | 1 | restaura os padrões |
| reabrir | 4000 | 1 | relança o app |
| tecla capturada | 100000 | + mods*256 + vk | a combinação que a pessoa apertou |

As faixas de 1000/1100/1300 e 2000/2500 **ficam vagas de propósito**. Reaproveitar o número de um comando morto é a
receita para um ini ou um atalho antigo virar clique errado — o próprio defeito que originou
este item. A largura declarada em cada `static_assert` é o dobro do que o código consegue
somar, também de propósito.

Lição geral: espaço de identificadores compartilhado por grupos com aritmética é uma
armadilha silenciosa — o defeito não aparece em revisão de linha, só quando alguém soma dois
números que moram em telas diferentes.

### 11.2. Sair da captura de atalho é um ponto único

Entrar em "aperte a tecla…" chama `SuspenderAtalhos()`, senão a tecla vira `WM_HOTKEY` e
nunca chega ao painel. O caminho de sucesso retomava; **o Esc e o clique fora não**, e End,
F6 e Alt+Esc ficavam desregistrados o resto da sessão, sem nada na tela dizendo isso.

Agora existe `CancelarCapturaAtalho()`, um ponto único que limpa o estado e chama
`RetomarAtalhos()`. Ele é chamado no Esc, no `Hud::Close()` (que cobre clique fora,
`WM_CLOSE` e a própria tecla do painel) e o "padrão de fábrica" re-registra tudo por conta
própria. Os dois caminhos de desistência ficam no log:
`captura de atalho iniciada … hotkeys suspensas` → `captura de atalho cancelada - hotkeys de
volta no ar`.

### 11.3 e 11.4 — deixaram de existir com a Resolução interna

Os dois eram defeitos reais da Resolução interna: o degrau **75% nunca era aplicado** (só
`pct > 100` entrava, e o clamp ainda puxava qualquer valor menor de volta para 1.0), e a marca
`escala_pendente` **não era limpa no encerramento normal** (fechar antes do prazo revertia
para 100% acusando que a sessão anterior não tinha terminado bem). Os dois foram corrigidos e
medidos — e então a funcionalidade inteira saiu por decisão do usuário (§4.7). Ficam
registrados aqui para quem for procurar a correção e não achar o código: **não há mais o que
corrigir**. O item **4** (a faixa da escala engolindo o `CMD_REABRIR`) sumiu pelo mesmo motivo,
além de já estar resolvido pela renumeração do 11.1.

### 11.5. "Reabrir" com o nome do exe cravado

`ShellExecuteW(… g_exeDir + L"DLSS5ForAll.exe" …)` ignorava que o nome do exe não é fixo (o
usuário renomeia; os testes rodam como `DLSS5ForAll-hud.exe`) e ignorava o retorno. Com o
nome errado, o botão fechava o app e não abria nada. Hoje usa `GetModuleFileNameW`, e se o
`ShellExecuteW` devolver ≤ 32 o app **não fecha** e avisa.

### 11.6. `Capture::Stop` e o `ReadSample` travado

`th.detach()` depois de 2 s e `SafeRelease(reader)`/`SafeRelease(src)`/`CloseHandle(evt)` em
seguida: se a thread ficou presa dentro do `ReadSample` (acontece com cabo ou placa de
captura ruim, e o `Shutdown()` nem sempre a destrava), ela ainda usa os três — use-after-free
na saída, crash na cara de quem já estava com problema de hardware. Agora, quando a thread
sobrevive ao prazo, os três são **abandonados de propósito**: o processo está encerrando, e
um vazamento que dura milissegundos é melhor do que uma tela de erro.

### 11.7. `_snwprintf` não termina a string ao truncar

Vale para todo uso com título de janela: o título de um navegador ou launcher passa de 512
caracteres com folga. `buf[N-1] = 0` na mão em `main.cpp` (título da janela e rótulo de modo
de vídeo) e em `screen_capture.h` (rótulo de janela e de monitor).

### 11.8. URL do `ofertas.ini` sem validação

`ShellExecuteW("open", s)` não abre só páginas: com um caminho de arquivo, um UNC de rede ou
um verbo de protocolo registrado, ele **executa** o que estiver ali. O `ofertas.ini` fica ao
lado do exe e é editável por qualquer um. Agora só `http://` e `https://` (sem espaço na
frente, case-insensitive) passam, em duas barreiras: no `LoadConfig` e no `OpenLink`.
Qualquer outra coisa cai no endereço de fábrica e sai um `OutputDebugString`. Conferido com
`file:`, caminho local, UNC, `ms-settings:` e `javascript:` — todos recusados.

### 11.9. `R.extSrv` apontando para um SRV já liberado

Dois lugares, o mesmo erro de forma: o ponteiro da textura da captura só era atualizado no
caminho de sucesso.

* no laço normal, `R.extSrv = scap.srv;` estava dentro do `if (got)`. Quando o `EnsureTex`
  falha numa troca de tamanho, o SRV de dentro do `ScreenCapture` já foi solto e a atribuição
  condicional deixava `R.extSrv` pendurado no ponteiro morto até o próximo frame bom;
* na retomada da captura, `scap.Stop()` libera `srv` e `tex`. Se o `scap.Start()` seguinte
  falhar (alvo em tela cheia exclusiva, jogo trocando de modo), `R.extSrv` continuava
  apontando para o SRV liberado e o `DrawAndPresent` desenhava com ele a cada ~100 ms. Agora
  o `Stop()` vem acompanhado de `R.extSrv = nullptr` e a atribuição do `scap.srv` acontece
  **nos dois desfechos**, como o `waitEvt` ao lado. Em falha o `scap.srv` é nulo e o desenho
  cai no caminho de "sem fonte" (`DrawAndPresent` já trata: `use = extSrv ? extSrv : srv`).

Este segundo virou crítico exatamente porque o item 5 tornou a recriação **alcançável**: era
código morto, ninguém passava por ali.

### 11.10. F11/Alt+Enter na camada sobreposta

`ToggleFullscreen` decidia pelo estilo: com `WS_OVERLAPPEDWINDOW` tira a moldura, sem ele
devolve. No modo sobreposto a janela é um `WS_POPUP` colado no alvo, que nunca teve moldura —
então o F11 lhe **dava** barra de título e borda por cima do jogo, e sem volta, porque o F11
seguinte encontraria `WS_OVERLAPPEDWINDOW` e entraria em "tela cheia".

A ausência do estilo, sozinha, não distingue "popup sobreposto" de "janela que nós mesmos
deixamos sem moldura", então a guarda precisa de uma marca explícita: `static bool emTelaCheia`,
ligada só quando fomos nós que tiramos. Medido nos dois modos: sobreposto, o estilo fica
idêntico (`0x94000000`) e o log diz `F11/Alt+Enter ignorado: janela sem moldura (modo
sobreposto)`; janela separada, `0x14CF0000` → `0x14000000` a 1920x1080 e de volta ao retângulo
original, como sempre.

### 11.11. O balão da bandeja ainda anunciava Alt+F9/F10/F12

Texto cravado de antes dos atalhos configuráveis. Pior que desatualizado: as teclas podem nem
estar registradas nesta máquina, porque a cascata troca a combinação quando outro programa já
tomou a primeira escolha. Agora sai de `g_hkBypass`, `g_hkInteract` e `TextoAtalho(...)`. De
quebra, `wcsncpy` também não termina a string quando enche o buffer — `szTip`, `szInfoTitle` e
`szInfo` ganharam o zero na mão, mesma classe do item 11.

### 11.12. A tela do grupo passou para a abertura, sem prazo

Decisão do usuário: o convite aparece **antes do seletor**, não na hora de iniciar a captura —
é quando a pessoa está parada, decidindo. E **não fecha sozinha em caso nenhum**: sem anel, sem
número, sem barra de tempo; sai por "Agora não", X, Esc ou pelo botão do grupo. Modelo WinRAR.

No `main.cpp` isso é `OfertasNaAbertura(hi)`, chamado logo depois de registrar os atalhos e
antes de `ShowSelector`, nos dois caminhos (com seletor e por linha de comando). Ele roda na
thread principal, então bloqueia até a janela fechar — que é o comportamento pedido. Duas
diferenças em relação ao caminho do painel/bandeja: não se esconde da captura (não existe
captura nossa ainda, e é propaganda do canal — deve sair em quem estiver gravando a tela) e a
posição vem de `AreaParaJanela` sem alvo, que cai na área útil do monitor primário, onde o
seletor vai abrir. A chave `segundos` do `ofertas.ini` é aceita e descartada.

### 11.13. Splash e cartaz-guia empilhados

Consequência direta do 11.12: com a splash sem prazo, ela e o cartaz-guia nasciam juntos, os
dois topmost e centralizados na mesma área, um por cima do outro até a pessoa fechar os dois.
Resolvido pelo próprio encadeamento — a splash acontece antes do seletor, então já fechou
quando o guia abre. A chamada que abria a splash junto com a captura foi removida; mostrar
duas vezes na mesma execução seria a segunda propaganda.

### 11.14. `FollowTarget` escondia a camada enquanto a splash estivesse aberta

A condição de mostrar a camada sobreposta listava as janelas "nossas" uma a uma: o alvo, a
janela de render e o painel. A tela do grupo faz `SetForegroundWindow` e não estava na lista,
então a camada ia para -32000 enquanto ela estivesse aberta — e, desde que ela deixou de fechar
sozinha, **por tempo indeterminado**. Listar janela por janela já falhou uma vez; agora
qualquer janela do nosso processo conta (`pidF == GetCurrentProcessId()`), o que cobre splash,
guia, cartaz e toast de uma vez.

### 11.15. BOM no `dlss5forall.ini` — perder as escolhas em silêncio

`GetPrivateProfileString` é a API de .ini do Windows e **não conhece BOM UTF-8**: com os três
bytes `EF BB BF` na frente, a primeira seção passa a se chamar `﻿[ultima]` e nenhuma
chave é encontrada. Não há erro — o app simplesmente volta a todos os padrões, como se fosse
a primeira execução. Quem põe o BOM é qualquer editor comum (o Bloco de Notas ao "Salvar como
UTF-8", o `Set-Content -Encoding UTF8` do PowerShell 5.1); nós nunca pomos, porque
`WritePrivateProfileString` escreve sem.

Descobri isso perdendo uma rodada de teste, e para o usuário seria pior: fonte, atalhos,
V-Sync, HDR e "não mostrar de novo" evaporam sem uma palavra. `CorrigirBomDoIni()`
(`main.cpp:1837`) roda antes de qualquer `IniGet`, e só age quando o BOM está de fato lá:
copia o original para `dlss5forall.ini.bak-bom`, reescreve sem os três bytes e registra no log.

**O escopo é estreito de propósito**, e essa foi a parte que mereceu discussão: só o
`dlss5forall.ini`, que é nosso e só nós escrevemos. O `ofertas.ini` **não** — é editado à mão,
cheio de comentários e blocos alternativos, e o `splash_ofertas.h` já lê BOM sozinho. O
`ReShade.ini` **não** — é do ReShade. E nada de varrer a pasta atrás de arquivos com BOM:
reescrever arquivo alheio em silêncio é pior do que o problema que se quer resolver.

Medido: ini gravado com BOM → log `dlss5forall.ini estava com BOM UTF-8 … reescrito sem BOM`,
arquivo passa a começar em `5B 75 6C…` (`[ultima]`), backup criado, e o `guia_visto=1` que
estava lá dentro volta a ser respeitado (o cartaz não abre). Segunda abertura, já sem BOM:
nenhum backup novo, nenhuma linha no log, arquivo intocado.

---

## 12. Rodada 2 (03/09/2026) — médios e consequências

### 12.1. A tela do grupo não aparece no caminho de linha de comando

Decisão do usuário. Quem monta um atalho "jogo + filtro" (`--window`, `--source`, `--monitor`,
`--device`) vai usar isso todo dia; fazer essa pessoa fechar propaganda à mão a cada partida
transforma o convite em obstáculo. A tela é para quem abre o app na mão e já está parado
escolhendo a fonte. `if (!fromCli) OfertasNaAbertura(hi);` — e o item do painel e o da bandeja
continuam abrindo a mesma tela sob demanda, nos dois modos.

### 12.2. Alt+Esc durante a tela do grupo

`WM_HOTKEY` é mensagem de **thread** (hwnd nulo): o laço interno da splash a recebia e o
`DispatchMessage` a jogava fora, então o atalho de sair ficava inerte enquanto a propaganda
estivesse aberta — e ela não fecha mais sozinha. Agora a splash roda em thread própria e o
laço de mensagens fica no `OfertasNaAbertura`, que vê a tecla, fecha a janela e devolve
`false`; o `wWinMain` encerra limpo.

**Sem dupla pressão neste ponto, de propósito.** A confirmação existe porque a combinação pode
ser apertada sem querer *no meio de um jogo*. Aqui não há jogo: a pessoa está olhando para um
convite e apertou "sair" por vontade própria. Pedir confirmação seria transformar a propaganda
em obstáculo — exatamente o que a 12.1 evita.

### 12.3. "A captura caiu e não voltou"

As dez tentativas de recriação existiam, mas gastá-las não dizia nada a ninguém: a imagem
congelava no último quadro e só o log sabia. Agora, ao gastar a cota com o alvo ainda vivo,
sai um cartaz e o painel passa a mostrar o botão "Reabrir o app" (o mesmo do
redimensionamento) — a pessoa vê o que aconteceu e tem a saída na mão. Uma vez por queda: a
marca se limpa quando a captura volta a ficar 10 s de pé.

Para exercitar isso, `DLSS5_TESTA_QUEDA` passou a aceitar valor **negativo**: derruba a sessão
a cada |N| segundos, sem parar. As retomadas se acumulam mais rápido que os 10 s de
estabilidade que zeram a cota, e o caminho chega ao fim. Medido com `-2`: dez
`captura recriada … (tentativa N)` e então `captura caiu e nao voltou depois de 10 tentativas`,
com `item[29] kind=16` (o botão Reabrir) presente no despejo do painel.

### 12.4. Escrita atômica do ini sem BOM

O conserto de BOM da §11.15 reescrevia o arquivo por cima. `CREATE_ALWAYS` trunca antes de
escrever: uma queda no meio, ou um `WriteFile` parcial, transformaria "arquivo com BOM que o
Windows não lê" em "arquivo truncado" — estritamente pior que o problema original. Agora grava
num `.tmp` e troca com `MoveFileExW(… REPLACE_EXISTING | WRITE_THROUGH)`, conferindo retorno e
bytes escritos; em qualquer falha apaga o `.tmp`, loga o motivo e **não** troca. Ou o ini
antigo continua inteiro, ou o novo aparece pronto. O `.bak-bom` fica: é barato e ajuda no
suporte.

### 12.5. M9 — a pasta do exe não aceita escrita

O README pede para não instalar em `C:\Program Files`, e o público instala. Como este processo
é de 64 bits, **não** existe a virtualização do VirtualStore (ela só vale para processos de 32
bits): a escrita simplesmente falha com acesso negado. O sintoma seria mudo e caro — escolha de
fonte nunca lembrada, atalhos configurados perdidos a cada abertura e, o pior, a pista
`[hdr] restaurar` não gravada antes de desligar o HDR do monitor, o que num `TerminateProcess`
deixaria a tela em SDR sem ninguém para religar.

O teste é o próprio log: se ele abre na pasta do exe, a pasta aceita escrita. Se não,
`g_dadosDir` vira `%LOCALAPPDATA%\DLSS5ForAll\`, o ini que existia ao lado do exe é migrado uma
vez (`CopyFileW` com "falhe se já existir", para não sobrescrever o de lá em sessões seguintes)
e sai um cartaz. O `hdr_control` acompanha sozinho, porque recebe o caminho do ini de nós
(`Init(IniPath())`).

**O que continua saindo de `g_exeDir`**, e tem de continuar: `ReShade.ini`, `ReShade.log`,
`dlss5-feed.log`, os presets e os `.addon64` — quem lê esses arquivos é o ReShade, e ele os
procura ao lado do `dxgi.dll`. Ou seja: numa pasta sem escrita o app passa a lembrar as
escolhas, mas o ReShade continua sem conseguir gravar preset nem cache de shader. Por isso o
cartaz manda mover o app, em vez de fingir que está tudo resolvido.

Medido: exe numa pasta onde `dlss5forall.log` não pode ser criado → log e ini em
`%LOCALAPPDATA%\DLSS5ForAll\`, ini de 209 bytes migrado, e a linha
`a pasta do exe nao aceita escrita (…) - ini e log vao para …`.

### 12.6. M16 e M17

**M16** — `ResizeClientTo` usava `SPI_GETWORKAREA`, que responde sempre pelo monitor primário.
É o mesmo erro que a criação da janela já tinha corrigido: com o alvo num monitor maior que o
primário, a janela era limitada pelo tamanho errado toda vez que a fonte mudava. Agora usa a
área útil do monitor onde a janela está, com o `SPI_GETWORKAREA` só como último recurso.

**M17** — `RetomarAtalhos` re-registrava e ignorava o resultado. Se outro programa tomasse a
tecla durante a janela de suspensão (que é exatamente quando o painel está em "aperte a
tecla…"), a flag continuava dizendo "registrado", o painel seguia anunciando a tecla como
válida e ela não fazia nada. O resultado do `RegisterHotKey` volta para a flag, e a perda sai
no log com o nome da ação e da tecla.

### 12.7. M18 — a fonte monitor não tem janela para morrer

Na fonte monitor não existe `targetWin`, então `Alive()` é sempre `true`: desligar ou
desconectar o monitor capturado congelava a imagem sem uma palavra. O sinal é o `HMONITOR`
deixar de ser válido — `GetMonitorInfoW` passa a falhar. Três segundos seguidos de falha antes
de acusar, porque uma troca de modo de vídeo derruba o handle por um instante e voltar dela é
normal.

**Não verificado em runtime**: não dá para desligar um monitor da máquina do usuário no meio de
um teste. É leitura de código.

### 12.8. Médios que deixaram de existir

**M11** (o marco do `dlss5-feed.log` ser o tamanho do *tail*, não do arquivo) e **M15** (a
escala do ini aplicada sem revalidar `ScaleCabe` contra a fonte atual) sumiram com a Resolução
interna: `CadeiaMorreu`, `TamanhoFeedLog` e `ScaleCabe` não existem mais. Registrado aqui para
quem for procurar a correção.


---

## 13. Rodada 2 — polimento de produto (03/09/2026)

Achados do `polidor-produto` (`docs/polimento-produto.md`) e os médios que sobraram da
revisão crítica. O fio que liga quase todos: **o app dizia coisas que não eram verdade**.

### 13.1. Nunca anunciar uma tecla que o Windows recusou

Abrir o app duas vezes basta para reproduzir: a segunda instância perde F6 e Alt+Esc para a
primeira, e mesmo assim o cartaz-guia ensinava "F6 — Ligar e desligar tudo", o card do filtro
mostrava o badge `F6` e o título da janela repetia. A pessoa aperta e nada acontece. No modo
sobreposto isso é pior do que uma decepção: sem barra de tarefas e sem Alt+Tab, "Alt+Esc
morto" significa que a única saída visível é o ícone da bandeja.

`TemAtalho()` já existia e testava exatamente isso — só não estava sendo usado. Agora o guia,
o badge do card, o rodapé do seletor e o balão da bandeja mostram `sem tecla` e a razão
(`outro programa está com essa tecla — mude no painel, em ATALHOS`), e um cartaz único na
abertura avisa quando **alguma** ficou de fora.

**Junto veio a cascata de reserva para mais duas ações.** Só o painel tinha. O "tudo" é o
antes-e-depois — a demonstração que faz a pessoa entender para que serve o app — e o "sair" é
a única saída visível no modo sobreposto. `kCascataTudo` (F6, F7, Alt+F6, Ctrl+Alt+T) e
`kCascataSair` (Alt+Esc, Ctrl+Alt+Q, Ctrl+Alt+F12, Ctrl+Shift+Q). Se nenhuma pegar, a escolha
original volta ao mapa, para o painel mostrar a tecla que a pessoa pediu marcada como recusada
— e não a última que tentamos pelas costas dela.

`Alt+Esc` **continua** o padrão de sair, por decisão explícita do usuário, com a dupla pressão
mantida. Ele é atalho do Windows e o app o sequestra enquanto roda; `Ctrl+Alt+Q` está logo
atrás na cascata e disponível em ATALHOS para quem preferir.

### 13.2. O aviso "ficou pesado" mandava fazer o que não dava

No modo janela separada o card "Filtro DLSS 5" **nasce desabilitado** ("só no modo
sobreposto"), e o toast automático de desempenho, nesse mesmo modo, dizia "abra o painel e
desligue o filtro". A pessoa abria o painel, achava o item que o cartaz mandou procurar, e ele
estava cinza — no pior momento possível, com o jogo travando. O que funciona nos dois modos é
o "tudo", e só se ele tiver tecla; o texto agora escolhe entre as três saídas reais.

### 13.3. Sem o ReShade carregado, o neural ficava "iniciando" para sempre

`LerEstadoNeural` procurava `evaluation succeeded` no `ReShade.log`. Sem `dxgi.dll` (setup não
rodado, ou o antivírus levou o ReShade — comum com add-ons), o arquivo não existe, nada é
encontrado e o estado ficava em `INICIANDO` **indefinidamente**, com o texto "esperando o
primeiro frame passar pela rede": uma promessa que não ia se cumprir nunca, sem nenhum caminho
na tela até a causa real.

Duas checagens novas: o `dxgi.dll` carregado no processo **ou** presente ao lado do exe (senão
`o ReShade não carregou — rode o setup-dlss5.ps1`), e um prazo de 25 s para o `INICIANDO`
(depois disso, `não subiu — confira "DLSS 5 Feed" no ReShade, ou reabra o app`). O relógio
começa na primeira consulta, não na abertura: o ReShade só compila os shaders no primeiro
Present.

### 13.4. Estado e ação se contradizendo

O card grande do neural mostrava `LIGADO · INICIANDO` com um botão escrito `ligar` ao lado.
Quem lê conclui que não está ligado. O estado virou só `INICIANDO` (o "LIGADO" era redundante
com o botão) e o botão passou a oferecer `desligar` também nesse estado — que é o oposto do
estado, que é o que um botão deve oferecer. Quem conta o que falta é o subtexto.

O mesmo tipo de mentira estava no badge do card "Ver os atalhos", que mostrava `End`: lido em
voz alta, "Ver os atalhos … End" promete que End abre o guia, e End abre o **painel**. O badge
virou `abrir`.

### 13.5. O seletor

Era a primeira janela do app e a única inteiramente sem acentuação — "escolha a fonte de
video", "Area util da janela", "resolucao" —, num app cujo painel é acentuado e bem escrito.
Padronizado com acento (só o `.ps1` fica em ASCII, por causa do codepage do console).

**DPI:** o processo é `PER_MONITOR_AWARE_V2`, então o Windows não escala nada por nós. O
painel tratava isso; o seletor não, e saía 20% menor que uma janela normal a 125% (medido:
533×422 lógicos em vez de 660×492) e pela metade num 4K a 200%. Agora um `E()` único
multiplica cada coordenada e a altura da fonte por `dpi/96`, na criação. Medido depois: a 125%
o seletor sai em 831×660 físicos = 665×528 lógicos, como uma janela normal.

**Rodapé:** `End abre o painel · F6 liga/desliga tudo` (~290 px) num STATIC de 200×34 quebrava
em duas linhas e a segunda era **cortada pela borda de baixo**. Campo em 196×34 com
`SS_CENTERIMAGE`, texto encurtado e montado por `RodapeAtalhos()`, que também respeita o 13.1.

**Dicas:** o campo tem 100 px e cabe cinco linhas de Segoe UI 15; os textos tinham seis e sete.
A linha que sumia na fonte Janela era justamente a que responde "posso gravar isso no OBS?".
Reescritos em cinco linhas, com a informação promovida para dentro das que ficam.

**Sem placa de captura:** o `return` antecipado saía antes de qualquer `SetWindowTextW(hHint)`,
então quem só tem teclado e monitor escolhia "Placa de captura", o combo ficava vazio, o
INICIAR apagava e a dica na tela continuava sendo **a do modo anterior**, falando de SOBREPOR e
de janela. O app parecia travado. Agora há uma dica própria, que diz o que aconteceu e manda
trocar a Fonte para "Janela".

### 13.6. A bandeja passou a existir nos dois modos

O ícone só era criado no modo sobreposto, mas o cartaz-guia prometia "o ícone na bandeja tem os
mesmos comandos" para todo mundo — inclusive para quem está no modo janela separada, que é o
modo que o próprio README recomenda para gravar no OBS. Agora existe nos dois, e o texto do
guia é condicional: no modo separado ele aponta a barra de título, que é a referência útil ali.

### 13.7. Médios da revisão crítica

**M1** — `Fatal` saía por `ExitProcess` sem tirar o ícone da bandeja; ele ficava de fantasma ao
lado do relógio até alguém passar o mouse por cima. Um `Fatal` dentro do laço (falha ao recriar
a textura da câmera) deixava exatamente esse rastro.

**M2** — `gap`, `nArrive` e `lastArr` do `LatProbe` são escritos pelo `Invoke` do WGC, numa
thread do sistema, e lidos/zerados pelo laço principal. *Data race* formal: não corrompe
memória, mas pode congelar a série de `gap` no relatório LAT — justamente o número que diz o
ritmo real do WGC. Um `SRWLOCK` em volta das três somas e da leitura no `Dump`.

**M6** — `Present` não checava `HRESULT`. Num TDR, ou numa atualização de driver com o app
aberto (comum em público), o app seguia apresentando num device morto: janela preta, laço a
100% de um núcleo, nenhuma mensagem. Não há recuperação possível aqui — recriar o device
recriaria o backbuffer, e a cadeia NGX não sobrevive a isso (§4.6) —, então a resposta honesta
é um cartaz e uma saída limpa, que é o que devolve o HDR do monitor.

**M10** — `HdrSetTreat` só era chamado quando a caixa estava marcada. Quem desmarcasse no
seletor com `[hdr] tratar=1` no ini deixava o módulo com `enabled_ = true`; `WarnLine()`
devolve vazio nesse estado, então o aviso "a imagem vai chegar lavada" sumia **justamente para
quem desmarcou**. Agora é chamado sempre, com o valor real.

**M14** — `g_trayCmd` era um `int` só: o segundo clique antes de o laço consumir o primeiro
substituía o primeiro, em silêncio. E o laço fica fora de circulação por bastante tempo — o
`PokeReShade` sozinho apresenta seis quadros com a tecla segurada, o que passa de 300 ms.
Virou fila com mutex, mantendo a interface (`= cmd` e `.exchange(0)`) para não espalhar a
mudança pelos ~30 pontos que enfileiram comandos.

### 13.8. Chaves de teste atrás de `DLSS5_DIAG`

`DLSS5_TESTA_QUEDA` e `DLSS5_TESTA_AVISO` **mudam o comportamento** do app e eram lidas do
ambiente sem guarda nenhuma; a linha `DLSS5_TESTA_QUEDA: derrubando a sessao de proposito`
chegou a aparecer no log desta pasta e confundiria qualquer suporte que o lesse. Agora as duas
exigem `DLSS5_DIAG=1` junto. As chaves que só leem ou logam (`DLSS5_LOG_ITENS`, `DLSS5_DRAIN`,
`DLSS5_MINUPD`…) continuam soltas.

### 13.9. A splash com o nome do app

A janela nascia com título vazio e `WS_EX_TOOLWINDOW` (fora da barra de tarefas e do Alt+Tab).
Quem dá duplo clique no exe via, como primeiro e único elemento na tela, um cartão topmost com
QR e um botão "QUERO AS OFERTAS", sem nenhuma marca do app — a assinatura visual de um adware.
Agora a janela tem título `DLSS5 For All` e o cartão traz `DLSS5 For All · abrindo…` no rodapé,
no canto oposto ao botão.

**Consequência para os testes:** `FindWindowW("DLSS5ForAllOfertas", $null)` **parou de achar a
splash**, porque o PowerShell converte `$null` em string vazia e `FindWindow(cls, "")` só casa
com janela de título vazio. É a mesma armadilha da §10, agora do outro lado: use a sobrecarga
com `IntPtr`.

### 13.10. O que ficou de fora, e por quê

**M8 — a splash não escala com o DPI.** Ela desenha com coordenadas absolutas em GDI, num
processo `PER_MONITOR_AWARE_V2`: num 4K a 200% ela aparece com metade do tamanho aparente.
O conserto certo não é multiplicar os ~60 números do `Paint` um a um — é `SetGraphicsMode`
(`GM_ADVANCED`) + `SetWorldTransform` no DC de memória, mantendo o layout em unidades lógicas,
com a divisão correspondente nos três testes de ponto (`PontoAbreLink`, botão, X). São três
lugares e um risco real de desalinhar hit-test e desenho, e não cabia com segurança nesta
rodada. Fica registrado com o plano pronto.

> **Fechado depois (03/09/2026):** o plano do `SetWorldTransform` acima foi **descartado** — eu
> cheguei a implementá-lo e ele não funciona, pelo motivo medido na §14.9 (`BitBlt` e regiões de
> recorte não participam da transformada). O M8 foi resolvido pelo agente `splashfix` com
> **escala explícita** (`Esc(v, dpi)` no `Layout` e nas fontes, hit-test em coordenada física,
> `WM_DPICHANGED`), medido nos dois monitores desta máquina. Ver §14.9, §14.13 e o comentário
> logo acima de `DpiDaJanela` no `splash_ofertas.h`.

**M18 e `FollowTarget` não foram medidos em runtime**, pelos motivos já registrados: não dá
para desligar um monitor da máquina do usuário no meio de um teste, e não consigo tomar o
foreground nesta máquina (`SetForegroundWindow` é recusado em silêncio, mesmo com
`AttachThreadInput`, pela mesma trava que descarta entrada injetada quando o `GameInputSvc`
está em primeiro plano). Os dois são revisão por leitura.

---

## 14. Rodada 2 — lote do `polidor-codigo` (03/09/2026)

Fecha em `DLSS5ForAll-r2.exe` + `dlss5-bridge-r2.addon64`. Aqui ficam só os achados que
mudaram alguma coisa não óbvia; o resto está na tabela do `docs/polimento-codigo.md`.

### 14.1. O `Capture` na pilha do `wWinMain` (C1)

O §11.6 já tinha consertado metade disto: quando o `ReadSample` trava, o `Stop()` desiste de
esperar, abandona a thread e **não** libera `reader`/`src`/`evt`, porque a thread ainda os usa.
O que faltou ver é que o próprio objeto também é dela: `Capture cap;` era uma variável local
do `wWinMain`, e o retorno da função destruiria o `mutex` e o `vector` debaixo da thread
abandonada. Um use-after-free na saída, exatamente no caso em que o hardware já estava com
problema.

Agora o objeto nasce no heap (`unique_ptr`), e quando o `Stop()` reporta `threadViva` a gente
**solta o ponteiro de propósito** — o processo está encerrando e alguns KB vazados são melhores
que uma tela de erro. Junto vai um detalhe que só aparece pensando na thread: `MFShutdown()`
também não pode ser chamado nesse caso, porque a thread ainda está dentro do Media Foundation.

Lição: "não libere o que a thread usa" não termina nos ponteiros que você vê na função — o
`this` também conta.

### 14.2. `SendInput` é teclado global (A2)

O mais perigoso do lote, e não é hipotético. `PokeReShade` faz `SetForegroundWindow` e depois
manda a tecla; mas **o Windows recusa `SetForegroundWindow`** quando outro processo tem a
entrada, e isso é o caso comum com jogo em tela cheia. Quando recusa, o `SendInput` seguinte
não vai para lugar nenhum específico: ele vai para **quem estiver em foco**, que é o jogo. E a
tecla em questão é F6 — quicksave, console ou trocar de arma, dependendo do jogo.

Agora, no caminho `VIA_TECLADO`, se `GetForegroundWindow() != h` depois do pump, a função
**aborta** com cartaz e devolve o click-through e as hotkeys. O caminho `VIA_MENSAGEM` continua
disparando sempre, porque `PostMessage` endereça a nossa janela por handle e não tem esse risco.

### 14.3. O aviso de "filtro pesado" acusava um problema inexistente (A3)

Observado em runtime, não por leitura: com a camada fora de vista (jogo em segundo plano) o
present cai para 2/s **de propósito** — só o bastante para a cadeia do ReShade não dormir. A
chegada continua em 60/s. A condição do aviso é "present < metade da chegada por 5 s", que fica
verdadeira na hora: bastava um alt-tab de cinco segundos para o app acusar que estava pesado.

`if (hidden) segundosPesado = 0;` e `!hidden` na condição. Medido depois: 30 s com a camada
escondida, **zero** linhas `aviso de desempenho` no log.

### 14.4. Laços de DXGI que só saíam em `NOT_FOUND` (M2)

`QueryDxgi` percorria `EnumAdapters1`/`EnumOutputs` até receber `DXGI_ERROR_NOT_FOUND`. Com
qualquer outro erro — `INVALID_CALL`, `DEVICE_REMOVED`, sessão remota — o ponteiro voltava nulo,
o `for` incrementava e o laço **nunca terminava**. O app travaria na abertura, antes de
qualquer janela aparecer, numa máquina que não é esta. `if (FAILED(hr) || !ptr) break;` nos
dois.

É o tipo de defeito que só existe fora da máquina de quem escreveu: aqui os dois enums sempre
devolvem `NOT_FOUND` no fim.

### 14.5. A fonte monitor não tinha caminho de recriação (M1)

A condição de recriação era `SessionClosed() && C.win && IsWindow(C.win)`. Numa fonte monitor
`C.win` é nulo — a condição é sempre falsa, e a recriação simplesmente não existia para esse
modo. Virou `alvoVivo`, que é `IsWindow(C.win)` na fonte janela e `C.mon != nullptr` na fonte
monitor, e os cartazes passaram a dizer qual das duas caiu.

### 14.6. A thread da tela do grupo (M4)

Ela chama `ShellExecuteW` (o botão abre o navegador), e o shell exige COM inicializado **na
thread que chama**. Sem `CoInitializeEx(APARTMENTTHREADED)` o clique podia não abrir nada, em
silêncio — o pior desfecho possível para o único botão que o app pede que a pessoa aperte.
A thread também deixou de ser `detach`: é guardada e esperada no encerramento, senão o processo
podia terminar com a janela dela ainda viva.

### 14.7. Os baixos que valem registro

- **`Renderer::Rebuild` foi apagado.** Era o único `ResizeBuffers` do arquivo e não tinha
  chamador desde o `FonteMudou` — ou seja, era exatamente a função proibida pela §4.6 esperando
  alguém chamar por engano.
- **`--source window` sem `--window`** casava com a primeira janela da lista, porque `IStrHas`
  com string vazia dá true para todas: o app abria filtrando o navegador ou o próprio terminal
  de quem escreveu o atalho. Agora é erro, com a dica do `--list-windows`.
- **CR/LF no título da janela** ia cru para o `WritePrivateProfileString`, e a linha seguinte do
  ini virava lixo — o que derruba **todas** as chaves depois dela. Sanitizado antes de gravar.
- **`g_toast`/`g_toastFont`** só eram destruídos no modo sobreposto; o cartaz passou a ser usado
  nos dois modos desde os avisos novos (tecla recusada, pasta sem escrita, captura perdida).
- **O add-on** ganhou `break` de limite nos dois batches (são arrays de pilha dentro do processo
  do jogo: a checagem custa nada e transforma um bug futuro de contagem numa ordem perdida em
  vez de corrupção de pilha) e passou a zerar `g_current_lut`/`g_grade_enabled` no
  `on_destroy_runtime` — sem isso o painel seguia mostrando o estado do runtime anterior, e um
  jogo que troca de API (DX11 → DX12 no menu) passa por ali.
- **`build-mingw.sh`** agora compila com `-Wall -Wextra` e chama o `build-addon.sh` junto
  (`SKIP_ADDON=1` pula). Publicar um exe novo com um `.addon64` velho é silencioso e caro: o
  `resolve()` do `reshade_bridge.h` recusa protocolo diferente sem dizer nada, e a LUT e a
  nitidez simplesmente param de responder.

### 14.8. Compatibilidade da ponte, medida nas três combinações

O protocolo **não mudou** (`DLSS5_BRIDGE_PROTOCOL` continua 1, as duas structs e os quatro
exports estão idênticos — conferido com `objdump -p` nos dois `.addon64`). As mudanças foram
internas ao add-on. Ainda assim, medido em runtime, porque "não devia ter mudado" não é
verificação:

| exe | add-on | LUT ativas | nitidez ativas |
|---|---|---|---|
| instalado (02:28) | **novo** (r2) | 6 de 6 | 4 de 4 |
| **r2** | instalado (velho) | 6 de 6 | 4 de 4 |
| **r2** | **novo** (r2) | 6 de 6 | 4 de 4 |

"Ativas" quer dizer `Available()` respondendo e o preset carregando o `DLSS5_Grade` — nos três
casos o F6 registrou `TUDO DESLIGADO (… lut=1 … grade=1 …)`, que é a ponte respondendo de
verdade, não só carregando.

### 14.9. O M8 continua fora, e agora com o motivo medido

Tentei o plano da §13.10 (`GM_ADVANCED` + `SetWorldTransform`, com a divisão nos hit-tests).
**Metade funcionou:** a janela nasceu no tamanho certo (1100×565 a 125%, que é 880×452 × 1,25) e
o clique no X, mandado em coordenadas físicas, acertou — a matemática do hit-test está correta.

**O desenho não acompanhou.** Medindo o alcance do conteúdo dentro do quadro: chega a x=875 de
1100 e y=448 de 565. O cartão continuou desenhado em 880×452 dentro de uma janela de 1100×565,
com faixa preta na direita e embaixo — **pior** do que está hoje.

O motivo é do GDI: `BitBlt` **não participa da transformada de mundo** (só enxerga translação),
e todo o fundo em degradê desse cartão passa por um `BitBlt` de `DIBSection`; as **regiões de
recorte** (`CreateRoundRectRgn` + `SelectClipRgn`, que fazem os cantos arredondados) também são
em coordenadas de dispositivo e ignoram a transformada. Fazer certo exige trocar os `BitBlt` por
`StretchBlt` e converter cada região com `LPtoDP` — reescrever os helpers de desenho, não
envolver o `Paint` numa camada.

Revertido, com os três helpers de DPI mantidos no arquivo e o registro do que foi tentado, para
ninguém repetir. A tarefa foi para o agente `splashfix`.

### 14.10. R2.1 — o `AlternarTudo` ignorava o retorno do poke

Achado do revisor, e é consequência direta do A2 (§14.2): assim que o `PokeReShade` passou a
poder abortar, ignorar o retorno virou o pior estado possível. Com o poke abortado, o neural
continuava **ligado**, mas `g_tudoDesligado` virava `true` e `neuralNos` afirmava que fomos nós
que desligamos. Saíam dois cartazes contraditórios, e no F6 seguinte o ramo de religar
**desligava** o neural que nunca tinha saído.

A correção é tudo-ou-nada nos dois ramos, e essa foi uma decisão minha além do pedido: em vez
de só não marcar o estado, o `AlternarTudo` **desfaz pela ponte o que já tinha feito** (grade e
nitidez) e volta ao estado anterior, devolvendo texto vazio — o cartaz do motivo quem mostra é
o próprio `PokeReShade`, e um segundo seria a contradição que o item quer eliminar. O motivo de
desfazer em vez de deixar meio aplicado: `g_antes` já tinha sido salvo, então um segundo F6
entraria de novo no ramo de desligar e **regravaria** `g_antes` com a grade já desligada,
perdendo o estado original. Meio caminho aqui é pior que caminho nenhum.

> **Hoje (03/09/2026):** grade e nitidez saíram do painel (§4), então não há mais nada a
> desfazer pela ponte antes do poke — o ramo de desfazer sumiu com elas. O que o item
> garantia continua garantido pelo resto do bloco: `g_antes = EstadoFiltro{}`,
> `g_tudoDesligado` intocado, camada parada e retorno vazio.

Medido com `DLSS5_TESTA_POKE=abortar` (sob `DLSS5_DIAG=1`), dois F6 seguidos: **zero** linhas
`TUDO DESLIGADO`, **zero** `TUDO LIGADO`, dois `o poke do neural abortou - desfeito, nada
mudou`, e as 6 LUTs continuando ativas no painel — a ponte foi desfeita, não quebrada.

### 14.11. O que o A2 revelou: o poke nunca funcionou nesta máquina

Ao testar o R2.1 apareceu uma coisa que muda a leitura de tudo o que veio antes: **`NR toggled`
não aparece nenhuma vez no `ReShade.log`** — nem hoje, nem nas execuções anteriores. Ou seja, o
F6 nunca chegou a alternar o Neural Rendering em nenhum teste automatizado desta máquina. As
linhas `TUDO DESLIGADO` que apareceram em todos os testes anteriores eram o código **relatando
um sucesso que nunca aconteceu**: ele não checava nada.

A causa é ambiental e já estava documentada por outro caminho (§10): a janela
`GameInputServiceWindow`, do serviço `GameInputSvc`, fica com o foreground. `SetForegroundWindow`
é recusado, e o `SendInput` ia para lá em vez de para a nossa janela. Acrescentei a tentativa
com `AttachThreadInput` (empresta a fila de entrada da thread em primeiro plano, que é a técnica
padrão para isto) e **mesmo assim é recusado** — é um serviço, não uma janela comum.

Então: o A2 e o R2.1 não quebraram nada. Eles pararam de mentir. O que antes era "não faz nada
e diz que fez" virou "não faz e explica por quê, com o caminho na tela".

**O que fica em aberto, e precisa de um teste humano:** quando o F6 vem de uma hotkey global de
verdade, o processo acabou de receber entrada e o `SetForegroundWindow` costuma ser aceito —
que é o caso real do usuário, e é diferente do meu arnês, que enfileira `WM_HOTKEY` com
`PostThreadMessage` e portanto nunca recebeu entrada nenhuma. Não consigo distinguir os dois
casos aqui, porque não consigo injetar tecla de verdade nesta máquina (mesmo bloqueio). Alguém
tem de apertar F6 de verdade, com o jogo em foco, e conferir se sai `NR toggled` no
`ReShade.log`. Se sair, está tudo certo. Se **não** sair, o F6 nunca alternou o neural para
ninguém, e o conserto não é o guard — é trocar o caminho do RenoDX por uma chamada direta,
como a ponte já faz para LUT e nitidez.

### 14.12. R2.2 e R2.3

**R2.2** — o prazo de 25 s do `INICIANDO` era um `static` que nunca rearmava: quem ligasse o
neural depois de 25 s de app lia "não subiu" por um segundo antes de a cadeia responder. Agora
o relógio zera nas duas transições que importam — quando o neural aparece desligado (`pedido == 0`)
e quando a cadeia sobe (`NR_ATIVO`) —, então cada "ligado" novo começa a contar do zero.

**R2.3** — `nArrive` era lido fora do `lkGap` no `Dump()`, sobrando exatamente a corrida que o
lock foi criado para fechar; agora sai sob o mesmo lock, junto dos valores de `gap`. E o aviso
de pasta sem escrita ganhou uma `MessageBox` de última instância para o caso em que **nem o
`%LOCALAPPDATA%` aceita**: ali não há log para registrar nada e o cartaz da tela só apareceria
muito depois, se aparecesse. É o único ponto do app em que uma caixa de bloqueio se justifica,
porque é a única coisa que a pessoa vai ver a respeito.

### 14.13. A tela do grupo no monitor certo, depois do DPI

O `splash_ofertas.h` do `splashfix` passou a escalar por DPI: os 880×452 viraram unidades
lógicas, e a janela nasce com 1100×565 a 125% ou 1320×678 a 150%. Isso quebrou uma suposição do
`main.cpp` que não era óbvia: `op.area = AreaParaJanela(880, 452)` perguntava "cabe?" com os
números **lógicos**. Uma janela de jogo que comporta 880×452 mas não 1320×678 respondia sim, e
o header — ao descobrir na hora que não cabia — caía para a área de trabalho do primário,
abrindo a tela no **monitor errado**.

`AreaDaTelaDeOfertas()` resolve na ordem certa, que é a única possível: uma área provisória diz
em que monitor estamos, o monitor diz o DPI, e só então dá para perguntar de novo com o tamanho
físico. O DPI sai do próprio header (`ofertas::detail::DpiEfetivoPonto`), não de uma conta
paralela — assim o `DLSS5_SPLASH_DPI` do teste vale para os dois lados e não existem duas
respostas para a mesma pergunta.

Medido nesta máquina (primário a 125%): a splash nasce em **1100×565** e o desenho chega a
x=1099 de 1100 e y=560 de 565. Contra os 875 de 1100 (79,5%) da minha tentativa revertida
(§14.9), é a diferença entre o cartão preencher a janela e sobrar tarja preta. O clique no X em
coordenada física continua fechando.

Vale registrar o que isso significava na prática: **o usuário deste PC vinha vendo a tela do
grupo 20% menor do que o desenhado**, porque o primário dele está a 125%. Não era um caso
hipotético de 4K.


---

## 15. O Neural Rendering pela ponte (03/09/2026)

### 15.1. O que estava errado, e por quanto tempo

Durante toda a rodada 2 eu reportei `TUDO DESLIGADO` como prova de que o F6 funcionava. Não
era. O agente da ponte mediu a causa: enquanto a `GameInputServiceWindow` — de integridade mais
alta — tem o foreground, **o UIPI descarta o `SendInput` sem sinalizar erro** (ele ainda devolve
1). Não era falta de foco, como eu supunha: era o Windows jogando a tecla fora em silêncio,
mesmo com o foreground concedido.

Isso fecha o mistério do `NR toggled` = zero no `ReShade.log` que eu vinha medindo sem saber
explicar. E fecha também a minha hipótese anterior — eu havia dito ao lead que talvez
funcionasse com hotkey real, "porque aí o processo acabou de receber entrada". A parte do
foreground estava certa; a conclusão estava errada, porque o problema não era o foreground.

### 15.2. `AlternarNeural()`: ponte primeiro, teclado como reserva

```
static bool AlternarNeural() {
    if (rsbridge::ToggleNeural()) return true;   // sem foco, sem SendInput
    return PokeReShade(VK_F6, false, VIA_TECLADO);
}
```

O add-on serve o `GetAsyncKeyState` direto para o RenoDX, dentro do processo — não há tecla
nenhuma no meio, então não há o que o UIPI descarte. Os quatro pontos que alternavam o neural
passaram a usar esta função: os dois ramos do `AlternarTudo`, o botão do card do painel e a
correção automática do "neural saiu invertido".

O contrato do §14.10 continua idêntico: `AlternarNeural()` devolve `false` só quando **nenhum**
dos dois caminhos funcionou, e é aí que o cartaz aparece e a troca inteira é desfeita.

**`true` quer dizer "armado", não "alternado"** — quem entrega é o próximo frame do RenoDX.
A conferência de 2,5 s pelo `ReShade.log` continua sendo o que fecha o ciclo.

`NeuralState()` existe mas devolve sempre `NEURAL_DESCONHECIDO`, e isso é definitivo: o RenoDX
não publica o estado em lugar nenhum que a ponte alcance. O card continua com o `NeuralPeloLog()`.

**Medido, e é a primeira vez:**

```
04:07:56  NR toggled OFF via F6
04:08:02  NR toggled ON  via F6
```

Dois F6, dois toggles, delta exatamente 2 — contra zero em toda a sessão anterior.

### 15.3. O defeito que só apareceu quando o F6 passou a funcionar

Primeiro teste com o toggle real, e o `ReShade.ini` ficou com `NeuralUplift=0`: eu tinha
apertado F6 (desliga tudo) e saído sem religar. **O F6 é uma comparação, não uma configuração**
— sair no meio dela deixava o Neural Rendering desligado para sempre, e a abertura seguinte
subiria sem neural, com o card dizendo DESLIGADO. A pessoa concluiria que o app quebrou, e o
motivo estaria num ini que ela não sabe que existe.

Isso nunca tinha aparecido porque o poke nunca funcionava. É o mesmo padrão do §14.11: o defeito
estava lá o tempo todo, escondido atrás de uma função que não fazia nada.

O encerramento agora religa o que **nós** desligamos, na mesma lógica do `neuralNos`:

```
if (g_tudoDesligado && g_antes.neuralNos) {
    if (AlternarNeural()) PresentNow(8);
    g_antes.neuralNos = false;
}
```

Apresentar depois de armar o toque não é superstição: quem o lê é o RenoDX, no `Present`
seguinte, e ele vive **dentro deste processo**. Sem apresentar, o app morre antes de a tecla ser
lida e o religamento se perde. A primeira versão apresentava oito quadros no escuro; com o
`NeuralDelivered()` da ponte (que só sobe quando o RenoDX efetivamente leu) o laço apresenta
**até a entrega**, com teto de 12. Medido: `religamento: 2->3 toques entregues em 1 quadro(s)`.

Medido de ponta a ponta: card religa (`NeuralUplift` 0 → 1), F6 desliga tudo (`→ 0`), Alt+Esc
duplo, e depois de sair **`NeuralUplift = 1`**, com `NR toggled ON` no log do ReShade em cima do
`encerrando com o neural desligado por nos - religando antes de sair`.

### 15.4. O caminho de reserva continua vivo, e foi testado

Com o add-on **v1** instalado (protocolo antigo), `ToggleNeural()` devolve `false` e a cadeia
inteira segue pelo caminho antigo:

```
neural: a ponte nao pode alternar agora - caindo no caminho do teclado
PokeReShade abortado: o foco ficou em ... - SendInput cairia dentro do jogo
TUDO: o poke do neural abortou - desfeito, nada mudou
```

Zero `TUDO DESLIGADO`, zero `TUDO LIGADO`, dois abortos, 6 LUTs ativas e `NeuralUplift`
intacto. O R2.1 continua valendo sem uma linha de mudança — que era exatamente o objetivo de
ter escrito o guard como tudo-ou-nada.

### 15.5. Compatibilidade

O `DLSS5Bridge_Version()` ficou congelado em 1 de propósito: os binários já publicados têm
`ver() != 1 -> recusa` gravado, e devolver 2 ali faria o app antigo perder a ponte inteira —
LUT e nitidez junto. A versão real vem do export novo `DLSS5Bridge_Protocol()`. Conferido nas
duas direções ao longo da rodada: exe antigo com add-on novo e exe novo com add-on antigo, LUT
6/6 e nitidez 4/4 nos dois casos.

**Ao instalar, o arquivo tem de se chamar `dlss5-bridge.addon64`.** Com outro nome o add-on
carrega no ReShade e mesmo assim a ponte fica invisível para o app, porque o cliente resolve o
módulo pelo nome — custou uma rodada de teste ao agente da ponte, e não deixa rastro nenhum no
log.

### 15.6. Duas coisas que a ponte ofereceu e eu não usei

Registro porque as duas parecem melhorias óbvias e não são.

**`ToggleNeuralConfirmed()` bloqueante — o agente da ponte implementou e removeu**, e a razão
vale para qualquer coisa que se pense em esperar aqui: quem lê o toque é o RenoDX *dentro* do
`Present`, então esperar pela confirmação na thread que apresenta impede o próprio `Present` que
faria a entrega. Medido por ele no mesmo binário: `false` depois de 254 ms quando chamada da
thread de render, `true` em 28 ms fora dela. Como o `AlternarTudo` roda na thread do laço de
mensagens, ela devolveria `false` justamente quando funcionou.

**`NeuralToggleAvailable() == false` para o estado `NR_SEM_ADDON`** — não serve, e usar seria
um retrocesso. Ele também é `false` com um add-on **v1** instalado, onde o neural pode estar
perfeitamente instalado e funcionando (só não dá para alternar pela ponte). O card diria "NÃO
INSTALADO" para quem tem tudo no lugar e um `.addon64` de uma versão anterior. O
`NR_SEM_ADDON` continua saindo da checagem de arquivos e do `dxgi.dll` (§13.3), que é o que a
frase realmente afirma.

