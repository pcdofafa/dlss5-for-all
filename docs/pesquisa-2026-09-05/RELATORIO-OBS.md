# OBS Studio não captura a janela do DLSS5 For All — investigação

Data: 2026-09-05. Escopo: só leitura de `main.cpp`, `screen_capture.h`, `README.md` e `docs/` em
`C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\`, cruzada com o código-fonte do OBS Studio (branch
`master`) e a documentação da Microsoft. Nada foi executado nem alterado no projeto.

Legenda: **[FATO]** = lido no código ou na documentação citada; **[INFERÊNCIA]** = conclusão minha,
não confirmada por teste.

---

## 0. Resumo em cinco linhas

1. **[FATO]** A janela de vídeo só recebe `WDA_EXCLUDEFROMCAPTURE` quando `C.hideSelf` é verdadeiro, e
   isso acontece **por padrão na fonte MONITOR** (linha de comando e seletor) e **sempre** no monitor
   sobreposto. Na fonte JANELA e na CÂMERA o padrão é não esconder.
2. **[FATO]** Monitor é a fonte padrão do seletor (`README.md:20`) e a caixa "Esconder esta janela"
   nasce **marcada** para monitor (`main.cpp:2009`). Nessa combinação o OBS **não consegue capturar a
   janela por nenhum método** (a janela some do Display Capture e sai preta/vazia no Window Capture).
   É a explicação mais provável para "toda vez".
3. **[FATO]** Mesmo na fonte JANELA, o Window Capture do OBS em método **"Automático"** usa **BitBlt**
   para a classe `FafaDLSS5CamWnd`, e BitBlt não enxerga uma janela com swapchain **flip model**
   (`FLIP_DISCARD`, `main.cpp:883/910`) — resultado: quadro preto. Só o método
   **"Windows 10 (1903 e superior)"** (WGC) funciona.
4. **[FATO]** No modo sobreposto a camada é **parqueada em -32000,-32000** e passa a apresentar a cada
   500 ms sempre que o jogo perde o foco — inclusive quando a pessoa clica no OBS para configurar a
   fonte (`main.cpp:5666-5691`, `7589-7615`). O preview do OBS parece congelado ou vazio.
5. **Recomendação:** não mexer no código. Corrigir a **documentação** (README "Gravando o resultado" e
   dicas do seletor) para dizer exatamente: fonte Janela, SOBREPOR desmarcado, OBS Window Capture com
   método "Windows 10 (1903 e superior)". Opcionalmente, um aviso de log/cartaz quando a janela nasce
   invisível para gravadores (mudança de baixo risco, mas não indispensável).

---

## 1. O que o código faz com a janela de vídeo (fatos verificados)

### 1.1. Onde `HideFromCapture` / `SetWindowDisplayAffinity` são chamados

| Janela | Local | Condição | Estilos estendidos |
|---|---|---|---|
| Cartaz (toast) `DLSS5ForAllToast` | `main.cpp:2858-2862` | sempre | `TOPMOST\|TOOLWINDOW\|NOACTIVATE\|TRANSPARENT\|LAYERED` |
| Painel (HUD) `DLSS5ForAllPanel` | `main.cpp:4839-4849` | sempre, salvo `DLSS5_PANEL_NA_CAPTURA=1` | `TOPMOST\|TOOLWINDOW\|LAYERED` |
| Guia `DLSS5ForAllGuia` | `main.cpp:5281-5285` | sempre | `TOPMOST\|TOOLWINDOW\|LAYERED` |
| Tela de ofertas (`splash_ofertas.h:1022`) | `main.cpp:3644`, `6601` | escondida só em monitor+sobreposto | — |
| **Janela de vídeo `FafaDLSS5CamWnd`** | **`main.cpp:6531-6533`** | **`C.kind != SRC_CAMERA && C.hideSelf`** | ver 1.3 |

Implementação (`main.cpp:2726-2734`): `GetProcAddress(user32, "SetWindowDisplayAffinity")` com
`WDA_EXCLUDEFROMCAPTURE = 0x11`. Só loga falha; nunca é revertida (não há `WDA_NONE` em lugar nenhum
do código — verificado por grep). O painel, o guia e o cartaz **nunca** aparecem em captura: é
intencional (`docs/painel.md:16-25`).

### 1.2. De onde vem `C.hideSelf`

| Caminho | Fonte JANELA | Fonte MONITOR | Câmera |
|---|---|---|---|
| Linha de comando | `argHide` → padrão **false**, liga com `--hide-self` (`main.cpp:6275`) | `!argNoHide` → padrão **true**, desliga com `--no-hide-self` (`main.cpp:6285`) | não se aplica |
| Seletor (caixa "Esconder esta janela…") | nasce **desmarcada** (`main.cpp:2010`) | nasce **marcada** (`main.cpp:2009`) | caixa desabilitada |
| Forçado | — | **sempre true** se SOBREPOR marcado (`main.cpp:2375` e `6410`) | — |

A escolha da caixa **não é persistida** no `dlss5forall.ini` (só `fonte`, `sobrepor`, `telacheia`,
`vsync`, `fps_tela`, `janela`, `monitor`, `work_resolution` — `main.cpp:6347-6367`). Cada abertura do
seletor volta ao padrão acima.

### 1.3. Estilos da janela de vídeo em cada modo

| Modo | Criação | Estilos | Depois do `R.Init` |
|---|---|---|---|
| Janela separada | `main.cpp:6442-6444` | `WS_OVERLAPPEDWINDOW`, ex = 0 | F11 tira `WS_OVERLAPPEDWINDOW` e cobre o monitor (`main.cpp:1178-1185`) — borderless, **não** exclusivo |
| Sobreposto | `main.cpp:6420-6425` | `WS_POPUP`; ex = `TOPMOST` + (`TOOLWINDOW` **só se** `hideSelf`) + (`TRANSPARENT\|NOACTIVATE` se click-through, padrão sim) | `WS_EX_LAYERED` + `SetLayeredWindowAttributes(alpha 255)` (`main.cpp:6517-6527`), desligável com `DLSS5_SEM_LAYERED=1` |

O próprio comentário em `main.cpp:6417-6419` diz por que o `TOOLWINDOW` é condicional: "Sem
WS_EX_TOOLWINDOW ela continua sendo uma janela 'de verdade' pro sistema: o OBS a lista em Captura de
Janela". Ou seja, o autor já projetou o modo sobreposto sobre janela para ser capturável.

Swapchain: sempre **flip model** — `FLIP_DISCARD` + waitable (`main.cpp:880-887`), fallback
`FLIP_DISCARD` clássico (`main.cpp:910`), e só se ambos falharem `DISCARD` bitblt (`main.cpp:916`).

Título: `"DLSS5 For All - <fonte>"` na criação (`main.cpp:6413`), reescrito periodicamente com
resolução, fps e atalhos (`main.cpp:7877-7882`). Exe: `DLSS5ForAll.exe`. Classe: `FafaDLSS5CamWnd`.

### 1.4. Comportamento da camada no modo sobreposto que afeta a gravação

- **[FATO]** `FollowTarget` (`main.cpp:5660-5691`): sobre uma **janela**, a camada só fica à vista
  quando a janela em primeiro plano é o alvo, a própria camada ou qualquer janela do nosso processo.
  Caso contrário, com `keepAlive` (= `!C.hideSelf`, `main.cpp:7270`) ela é **movida para
  -32000,-32000** em vez de ocultada, justamente "para o OBS não perder a fonte" (`main.cpp:5657-5659`).
  Sem `keepAlive` (fonte monitor) ela é `SW_HIDE`.
- **[FATO]** Camada fora de vista → `hidden = true` e o `Present` passa a ocorrer a cada **500 ms**
  (`main.cpp:7589-7615`). O último quadro desenhado é o que fica.
- **[FATO]** Ao iniciar em sobreposto a camada começa em **bypass** ("aquecimento") por até 30 s
  (`main.cpp:6572-6580`): fora de vista até a cadeia ReShade/DLSS entregar.
- **[FATO]** Fonte monitor sobreposta: a camada cobre 100 % do monitor e está excluída da captura, então
  a própria captura WGC do app para de receber frames (`main.cpp:6817-6830`); existe uma sonda que
  recolhe a camada e devolve o original (`main.cpp:7250-7268`).

### 1.5. O que a documentação do projeto já diz

- `README.md:272`: "OBS → Captura de Janela (método Windows 10 / WGC) apontando pro `DLSS5ForAll.exe`.
  Não usa Captura de Jogo." — correto, mas o método aparece entre parênteses e não diz que o
  "Automático" falha.
- `README.md:274-275`: "Se a fonte for monitor com 'esconder esta janela da captura' ligado, o OBS não
  vê a nossa janela — use dois monitores (...) **ou capture por janela**." — ambíguo: "capturar por
  janela" no OBS **também falha** enquanto a caixa estiver ligada (ver §2). O que resolve é trocar a
  **fonte do app** para Janela, ou desmarcar a caixa.
- `README.md:259` (Lossless Scaling): "Fonte monitor (qualquer modo): a nossa janela fica com
  `WDA_EXCLUDEFROMCAPTURE` e o LS enxerga tela preta" — coerente com o código; vale igualmente para o
  OBS.
- Dicas do seletor (`main.cpp:1796`, `1813`, `2313`, `2319`): "Nos dois casos o OBS grava a nossa
  janela (Captura de Janela)" e "O desktop continua normal e o OBS grava essa janela sem problema" (esta
  última é a dica da fonte **monitor**, cuja caixa "esconder" nasce marcada — a frase é falsa nesse
  estado padrão).

---

## 2. O que cada método do OBS faz com estas janelas

Fontes primárias: `libobs/util/windows/window-helpers.c`, `plugins/win-capture/window-capture.c`,
`plugins/win-capture/duplicator-monitor-capture.c`, `plugins/win-capture/game-capture.c` (OBS Studio,
`master`, lidos em 2026-09-05), e docs da Microsoft. Links no §7.

### 2.1. Listagem de janelas (Window Capture e Game Capture "janela específica")

**[FATO]** `check_window_valid()` em `window-helpers.c` rejeita a janela se: não visível; (modo
EXCLUDE_MINIMIZED) minimizada ou *cloaked*; **`WS_EX_TOOLWINDOW`**; `WS_CHILD`; área cliente 0×0.
Também descarta exes internos da Microsoft. **Não** olha `WS_EX_LAYERED`, `WS_EX_TRANSPARENT`,
`WS_EX_TOPMOST` nem a afinidade de exibição.

Consequência para o app:

| Estado da janela de vídeo | Aparece na lista do OBS? |
|---|---|
| Janela separada (qualquer fonte) | **Sim** |
| Sobreposto + fonte janela (`hideSelf` false, sem `TOOLWINDOW`) | **Sim** (mesmo layered/transparent/topmost; mesmo parqueada em -32000, pois continua visível) |
| Sobreposto + fonte monitor (`TOOLWINDOW`) | **Não** |
| Painel / guia / cartaz | Não (`TOOLWINDOW`) |

Prioridade de casamento padrão do OBS é "título, senão mesmo executável" — o título que muda a cada
segundo (`main.cpp:7877`) não derruba a fonte, porque o exe `DLSS5ForAll.exe` casa. **[FATO]** para o
padrão; **[INFERÊNCIA]** para "não derruba" (não testado).

### 2.2. Window Capture — método "Automático" e "BitBlt"

**[FATO]** `choose_method()` em `window-capture.c`: com `METHOD_AUTO`, só escolhe WGC se a **classe**
da janela casar com `Chrome`/`Mozilla` (parcial) ou uma lista fixa (`ApplicationFrameWindow`,
`Windows.UI.Core.CoreWindow`, `XLMAIN`, `SDL_app`, …). **`FafaDLSS5CamWnd` não casa → BitBlt.**
Padrões (`wc_defaults`): `method = METHOD_AUTO`.

**[FATO]** Microsoft, `DXGI_SWAP_EFFECT`, Remarks: "After the first successful call to
IDXGISwapChain1::Present1 on a flip-model swap chain, GDI no longer works with the HWND that is
associated with that swap chain, even after the destruction of the swap chain."

**[INFERÊNCIA forte]** BitBlt sobre o HWND de vídeo (flip model) devolve **preto**. É o sintoma clássico
"janela preta, mude para Windows 10 (1903 e superior)" dos fóruns do OBS.

Com `WDA_EXCLUDEFROMCAPTURE`: BitBlt também não obtém o conteúdo (a afinidade vale para as APIs de
captura do sistema). Com `WS_EX_LAYERED`: BitBlt sem `CAPTUREBLT` historicamente ignora janelas
layered; irrelevante aqui porque o flip model já derruba o BitBlt antes.

### 2.3. Window Capture — método "Windows 10 (1903 e superior)" (WGC, `CreateForWindow`)

| Janela | Resultado |
|---|---|
| Janela separada, `hideSelf` false, flip model | **[FATO]** é o caminho documentado pelo próprio README e pelo LS; WGC captura o visual composto pelo DWM independentemente do modelo de apresentação. Deve funcionar. |
| Qualquer janela com `WDA_EXCLUDEFROMCAPTURE` | **[FATO]** Microsoft: "Everywhere else, the window does not appear at all"; a afinidade "works only when the DWM is composing the desktop" e vale para as APIs de captura do sistema. WGC de janela devolve quadro vazio/preto (ou a fonte fica em branco). **[INFERÊNCIA]** quanto à cor exata do quadro (preto vs. transparente): não testado. |
| Sobreposto + fonte janela (`LAYERED\|TRANSPARENT\|NOACTIVATE\|TOPMOST`, alpha 255) | **[INFERÊNCIA]** a captura WGC de janela lê o visual do DWM da janela, e o layered com alpha 255 é composto pelo DWM como qualquer outra; a janela é listada e o `IGraphicsCaptureItem` deve ser criado. Não achei documentação da Microsoft dizendo que layered quebra `CreateForWindow`. Os relatos de "janela transparente sai preta" nos fóruns do OBS são todos da era BitBlt (2017-2018). Ponto a testar (ver §6). |
| Sobreposto parqueada em -32000 (jogo sem foco / OBS em foco / aquecimento) | **[FATO]** a janela continua visível e listada; o app apresenta a cada 500 ms o último quadro. **[INFERÊNCIA]** o OBS mostra imagem parada (último frame) ou, durante o aquecimento, a janela ainda sem conteúdo útil. Para quem está ajustando a fonte no OBS, parece "não captura". |
| Sobreposto + fonte monitor | Não está na lista; e está excluída. Impossível. |

### 2.4. Display Capture — DXGI Desktop Duplication e WGC (`CreateForMonitor`)

**[FATO]** `duplicator-monitor-capture.c`: `METHOD_AUTO` → DXGI, salvo se o DXGI não achar o monitor ou
notebook com 2 GPUs na bateria (→ WGC). Ambos capturam o **desktop composto**: tudo o que o DWM desenha,
**menos** janelas com `WDA_EXCLUDEFROMCAPTURE` (Windows 10 2004+; em versões anteriores a afinidade
vira `WDA_MONITOR` = retângulo preto).

| Estado do app | O que o Display Capture grava |
|---|---|
| Janela separada, `hideSelf` false | a janela filtrada, como está na tela ✔ |
| Janela separada, `hideSelf` true (padrão da fonte monitor) | **a janela some**; aparece o que está atrás dela (desktop/jogo crus) |
| Sobreposto + janela, `hideSelf` false | a camada filtrada enquanto o jogo tiver foco; quando a camada sai de vista, o jogo cru |
| Sobreposto + monitor | só o desktop cru (a camada é excluída) — **por projeto**, para não haver espelho infinito |

Layered/transparent/topmost/toolwindow **não** afetam o Display Capture: é a imagem final do monitor.

### 2.5. Game Capture (hook no `Present`)

**[FATO]** `game-capture.c`: lista janelas com `INCLUDE_MINIMIZED` (mesmo filtro de `TOOLWINDOW`);
modo "qualquer aplicação em tela cheia" usa a **janela em primeiro plano** cujo retângulo é igual ao do
monitor e sem borda; padrão `anticheat hook` ligado e `capture_overlays` desligado. O hook captura o
backbuffer dentro do processo, então **`WDA_EXCLUDEFROMCAPTURE` não o afeta** e layered/transparent
tampouco.

Ressalvas específicas deste app:

- **[FATO]** O ReShade vive dentro do `DLSS5ForAll.exe` como `dxgi.dll`/`d3d11.dll` proxy
  (`main.cpp:1579`, `1664`, `5352`) e aplica tudo no `Present`. **[FATO]** Fóruns e a issue #11250 do OBS
  registram flicker/preto/hook não pegando quando ReShade e Game Capture coexistem; um mantenedor diz
  que "não há ordem definida para múltiplos hooks injetados" e que a opção "capturar overlays de
  terceiros" é "sorte". Resultado com o Game Capture: pode sair a imagem **sem** o filtro, com flicker
  ou preta, dependendo de quem enganchou por último. O README já manda não usar (`README.md:272`).
- **[FATO]** No sobreposto a camada é `WS_EX_NOACTIVATE` e nunca é primeiro plano → o modo "qualquer
  tela cheia" nunca a escolhe; e no monitor sobreposto ela nem está na lista (`TOOLWINDOW`).
- **[FATO]** Janela separada em F11 vira borderless do tamanho do monitor → o modo "qualquer tela
  cheia" a reconheceria quando estiver em primeiro plano.

### 2.6. Tabela-resumo: o que o OBS consegue em cada combinação do app

| Fonte / modo do app | `hideSelf` padrão | Listada no OBS | Window Capture Automático (BitBlt) | Window Capture Win10/WGC | Display Capture | Game Capture |
|---|---|---|---|---|---|---|
| **Janela / separada** | não | sim | **preto** (flip) | **funciona** | funciona | instável (ReShade) |
| **Janela / sobreposto** | não | sim | preto | provavelmente funciona **enquanto o jogo tem foco**; parada quando o jogo perde o foco ou no aquecimento | filtrada só com o jogo em foco | instável |
| **Monitor / separada** | **sim** | sim | preto | **vazio** (WDA) | **a janela some** | instável (mas o WDA não o impede) |
| **Monitor / sobreposto** | sim (forçado) | **não** | — | — | só o desktop cru | não (não listada, nunca foreground) |
| **Câmera** | nunca | sim | preto | funciona | funciona | instável |

Com `--no-hide-self` ou a caixa desmarcada, "Monitor / separada" passa a se comportar como "Janela /
separada" — ao custo de espelho infinito se a janela do app estiver **no mesmo monitor** que ela captura
(a janela é aberta no monitor do alvo, `main.cpp:6427-6433`, então é o caso comum com um só monitor).

---

## 3. Causa mais provável do relato

Sem saber a configuração exata (ver §6), a ordem de probabilidade é:

1. **Fonte MONITOR com "esconder" ligada (padrão) + qualquer método do OBS.** [INFERÊNCIA a partir de
   fatos] Monitor é a fonte padrão do seletor, a caixa nasce marcada e a escolha não é lembrada, então
   a pessoa cai nesse estado em toda abertura. Nele o OBS não consegue **de jeito nenhum**: a janela
   some do Display Capture e sai vazia no Window Capture. Bate com "toda vez".
2. **Fonte JANELA + Window Capture com método "Automático".** [FATO no código do OBS + doc MS] BitBlt
   sobre janela flip model = preto. A pessoa só sai disso trocando o método para "Windows 10 (1903 e
   superior)". O README menciona o método, mas de passagem.
3. **Modo SOBREPOSTO sobre janela: a camada some/parqueia sem o jogo em foco.** [FATO no código] Ao
   clicar no OBS para escolher a fonte, o jogo perde o foco e a camada vai para -32000 com Present a
   cada 500 ms; nos primeiros até 30 s ela está em bypass. O preview do OBS mostra imagem parada ou
   nada, e a conclusão natural é "não captura", mesmo que gravando com o jogo em foco funcione.
4. **Game Capture** por hook, brigando com o ReShade dentro do processo. [FATO: relatos + README já
   desaconselha].
5. **Hipótese não confirmada:** WGC de janela falhar especificamente por causa de `WS_EX_LAYERED` +
   flip model no sobreposto. Não achei base documental; só um teste responde (`DLSS5_SEM_LAYERED=1`
   permite isolar a variável sem mexer no código).

Hipótese descartada: "janela tool/layered não listada" no modo sobreposto sobre **janela** — o código
só põe `TOOLWINDOW` quando `hideSelf`, e o OBS não filtra layered/transparent. No monitor sobreposto
ela realmente não é listada, mas ali a exclusão é intencional e inevitável.

---

## 4. Correções possíveis e risco de cada uma

### (a) Só documentar — risco zero — **recomendada**

Mudanças de texto (README e, se quiser, as dicas do seletor). Nada no comportamento.

- README "Gravando o resultado": receita passo a passo —
  1. no app: fonte **Janela**, SOBREPOR **desmarcado** (ou marcado, sabendo que a camada some quando o
     jogo perde o foco), caixa "Esconder" **desmarcada**;
  2. no OBS: *Captura de Janela* → janela `[DLSS5ForAll.exe]: DLSS5 For All - …` → **Método de captura:
     "Windows 10 (1903 e superior)"** (o "Automático" usa BitBlt e sai **preto** nesta janela);
  3. não usar *Captura de Jogo* (ReShade dentro do processo; hooks brigam);
  4. fonte **Monitor**: com "Esconder" ligado o OBS não vê a janela em **nenhum** modo; só desmarque se
     o app estiver em **outro** monitor que não o capturado (senão, espelho infinito); no monitor
     **sobreposto** não há como gravar o resultado com OBS, por projeto — grave o filtro com fonte
     Janela.
  5. no sobreposto, o preview do OBS só se mexe com o jogo em primeiro plano; nos primeiros segundos a
     camada está em aquecimento (bypass).
- Corrigir `README.md:274-275`: trocar "ou capture por janela" por "ou troque a fonte do app para
  Janela / desmarque a caixa".
- Dica da fonte monitor no seletor (`main.cpp:1813`): "o OBS grava essa janela sem problema" é falsa
  com a caixa marcada (estado padrão). Sugestão de texto: "…o OBS grava essa janela (Captura de Janela,
  método Windows 10) **se a caixa 'Esconder' estiver desmarcada** — com um só monitor isso gera
  espelho". Se essa linha for mexida, é uma edição de string, sem lógica.
- Dicas `main.cpp:1796`/`2313`: acrescentar "método Windows 10 (1903+)" ao lado de "Captura de Janela".

O que pode quebrar: nada.

### (b) Flag "permitir captura pelo OBS" — não recomendada (já existe o equivalente)

- **[FATO]** Para a fonte **janela** o comportamento pedido **já é o atual**: `hideSelf` padrão false
  (`main.cpp:2010`, `6275`), sem `TOOLWINDOW`, janela listada. Uma flag nova não mudaria nada aí. Se
  falha, é pelo método do OBS (BitBlt) ou pela camada parqueada, não pela afinidade.
- Para a fonte **monitor em janela separada** já existe `--no-hide-self` e a caixa do seletor. O que
  falta é só a pessoa saber. Trocar o **padrão** para "não esconder" reintroduziria o espelho infinito
  no caso comum (um monitor, janela aberta no próprio monitor capturado) — risco alto de regressão
  visível, e o app **não tem** detecção de espelho para o modo janela separada (a sonda existe só no
  sobreposto, `main.cpp:6817+`).
- Para o **monitor sobreposto** não há flag possível: sem exclusão, a camada captura a si mesma.
- Um botão no painel para alternar em tempo de execução (`SetWindowDisplayAffinity(WDA_NONE)`) é
  tecnicamente trivial e reversível, mas tem o mesmo problema de espelho e adiciona estado ao painel.
  Não vale o risco para um caso que a documentação resolve.

### (c) Outras ideias de baixo risco

| Ideia | Efeito | Risco | Opinião |
|---|---|---|---|
| Uma linha de log + cartaz único quando `hideSelf` é aplicado à janela de vídeo (perto de `main.cpp:6532`): "janela invisível para gravadores (esconder ligado)" | a pessoa descobre na hora por que o OBS não vê | mínimo: só `Log` e `ShowToast`, sem mudar estilos | opcional; útil, mas a documentação já cobre |
| Manter o estilo/classe/título como estão | a janela já é listada e casa por exe | — | não mexer |
| Forçar WGC do lado do app | impossível: o método é escolhido no OBS pela classe; só classes fixas da lista do OBS forçam WGC | — | nada a fazer |
| Renomear a classe para algo da lista do OBS (ex.: `SDL_app`) para o "Automático" cair em WGC | faria o método Automático funcionar | mentir sobre a identidade da janela; outras ferramentas podem tratar como SDL; frágil a mudanças do OBS | **não recomendo** |
| Reverter para `DXGI_SWAP_EFFECT_DISCARD` (bitblt model) para o BitBlt do OBS funcionar | resolveria o "Automático" | destrói a latência medida (waitable + flip, `README.md:277`) e a detecção de HDR | **não** |
| `DLSS5_PANEL_NA_CAPTURA=1` para gravar o painel | já existe (`main.cpp:4846`) | — | documentar no README se quiser |

---

## 5. Recomendação final

**Deixar o código como está e corrigir a documentação**: todas as falhas encontradas decorrem de
escolhas deliberadas e corretas do app (exclusão da captura na fonte monitor, camada que sai de vista
sem foco) ou de configuração do OBS (método "Automático" → BitBlt → preto em flip model); o único
ajuste de código com algum valor é um aviso de log/cartaz quando a janela nasce invisível para
gravadores, e ele é opcional.

---

## 6. Perguntas que só um teste com o OBS responde

Para o autor responder (ou anotar no `dlss5forall.log` + log do OBS):

1. **Qual fonte do OBS** foi usada: *Captura de Janela*, *Captura de Tela* ou *Captura de Jogo*?
2. Se *Captura de Janela*: qual **Método de captura** (Automático / BitBlt / Windows 10 1903+)? A janela
   `[DLSS5ForAll.exe]` **aparece na lista**? O quadro sai **preto**, **transparente/vazio**, **parado**
   ou a fonte fica com "janela não encontrada"?
3. Se *Captura de Tela*: a janela do app **some** da gravação (aparece o que está atrás) ou aparece
   preta?
4. Se *Captura de Jogo*: qual modo (qualquer tela cheia / janela específica / hotkey), "hook anticheat"
   ligado?, e o log do OBS mostra `[game-capture: ...] Hooked ...`?
5. **Qual fonte do app**: Janela, Monitor ou Câmera? SOBREPOR marcado? A caixa **"Esconder esta janela
   de gravadores/capturas"** estava marcada? (Linha `Fonte: ... esconder=%d` no `dlss5forall.log`,
   `main.cpp:6396-6398`.)
6. Um ou dois monitores? O app e o OBS estão no mesmo monitor que o app captura?
7. Versão do Windows (`winver`): Windows 10 2004+ ou 11? (Antes de 2004 a exclusão vira retângulo
   preto em vez de "sumir".)
8. No modo sobreposto: o teste foi feito **com o jogo em primeiro plano** (a camada só existe assim) e
   **depois dos primeiros ~30 s** (aquecimento em bypass)?
9. Teste de isolamento do layered: mesmo cenário com `DLSS5_SEM_LAYERED=1` (o clique deixa de
   atravessar, mas isola a variável). Se o WGC de janela passar a funcionar só assim, a hipótese 5 do §3
   vira fato.
10. Teste de referência: fonte **Janela**, SOBREPOR desmarcado, OBS Window Capture método **Windows 10
    (1903 e superior)**. Se isso funcionar, o problema é configuração; se não, há algo novo a investigar
    (e vale anexar o log do OBS).

---

## 7. Referências

Código do app (caminhos absolutos em `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\`):
`main.cpp` linhas 883, 910, 916 (swapchain); 1178-1191 (F11); 2009-2010, 2359, 2375 (seletor);
2726-2734 (`HideFromCapture`); 2858-2862 (toast); 4839-4849 (painel); 5281-5285 (guia); 5657-5691
(`FollowTarget`); 6184-6190, 6275, 6285 (CLI); 6396-6398 (log da fonte); 6410-6444 (criação da janela);
6517-6533 (layered + afinidade); 6601 (ofertas); 7270 (`keepAlive`); 7589-7615 (Present lento);
7877-7882 (título). `screen_capture.h:243-270` (enumeração de janelas do próprio app, que também
descarta `TOOLWINDOW`). `README.md:20, 36-40, 259, 270-275`. `docs/painel.md:10-25`.

OBS Studio (fonte, branch master):
- window-helpers.c — `check_window_valid`: https://raw.githubusercontent.com/obsproject/obs-studio/master/libobs/util/windows/window-helpers.c
- window-capture.c — `choose_method`, listas de classes, `wc_defaults`: https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/win-capture/window-capture.c
- duplicator-monitor-capture.c — método automático do Display Capture: https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/win-capture/duplicator-monitor-capture.c
- game-capture.c — modos, filtro de janelas, `capture_overlays`: https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/win-capture/game-capture.c
- KB Window Capture Sources: https://obsproject.com/kb/window-capture-sources
- Issue #11250 (Game Capture + ReShade, flicker): https://github.com/obsproject/obs-studio/issues/11250
- Discussão #9793 (ordem de hooks é indefinida): https://github.com/obsproject/obs-studio/discussions/9793
- Fórum: OBS & SweetFX/ENB/ReShade: https://obsproject.com/forum/threads/obs-sweetfx-enb-reshade.84929/
- Fórum: OBS doesn't capture ReShade enhanced games anymore: https://obsproject.com/forum/threads/obs-doesnt-capture-reshade-enhanced-games-anymore.102478/
- Fórum: Capture transparent C# WPF window (era BitBlt): https://obsproject.com/forum/threads/capture-transparent-c-wpf-window.75380/
- Fórum: BitBlt doesn't work (aceleração por hardware): https://obsproject.com/forum/threads/bitblt-doesnt-work-but-i-cant-use-windows-graphics-capture.124437/

Microsoft:
- SetWindowDisplayAffinity (WDA_NONE / WDA_MONITOR / WDA_EXCLUDEFROMCAPTURE, 2004+, só com DWM): https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity
- DXGI_SWAP_EFFECT (GDI deixa de funcionar no HWND após o primeiro Present flip): https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect
- DXGI flip model: https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model
- IGraphicsCaptureItemInterop::CreateForWindow (1903+): https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow
- Windows Developer Blog, "New ways to do screen capture" (WDA_EXCLUDEFROMCAPTURE remove a janela da captura em vez de preto): https://blogs.windows.com/windowsdeveloper/2019/09/16/new-ways-to-do-screen-capture/
- Windows.UI.Composition-Win32-Samples issue #64 (WDA_MONITOR = preto, EXCLUDEFROMCAPTURE = some): https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/issues/64
