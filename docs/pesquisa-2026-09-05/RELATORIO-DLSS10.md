# "DLSS 5 x2" (o tal "DLSS 10") no DLSS5 For All — viabilidade e recomendação

Pesquisa feita em 2026-09-05. Somente leitura do projeto; nenhum arquivo do app foi alterado.

Convenções deste documento:

- **[FATO]** — verificado em código, log, documento oficial ou página pública citada.
- **[INFERÊNCIA]** — raciocínio meu a partir dos fatos; pode estar errado e está marcado como tal.
- Referências a código são `arquivo:linha`. `main.cpp` é `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\main.cpp`; `dlss5-feed.cpp` é a cópia do Feeder no scratchpad (`...\scratchpad\dlss5-feed.cpp`, 5898 linhas, versão com interop do Deep Fried Chicken); `screen_capture.h` e `README.md` ficam ao lado do `main.cpp`.

---

## Resumo executivo

1. **A comunidade não chama isso de "DLSS 10".** [FATO] Nenhuma busca (Google via WebSearch, Reddit, X, YouTube, GitHub, Nexus) devolveu "DLSS 10" ou "double DLSS 5" como termo. O que existe, com bastante atividade desde o fim de agosto de 2026, é chamado de **multi-pass / two-pass / three-pass / "Extreme" / "Extreme Plus" / "layers" / "cascade" / "stacking"**. Três ferramentas fazem exatamente "rodar a rede N vezes por quadro":
   - **Alex's Toolkit** (`alexs-toolkit.addon64`, v0.9.0-beta): interpõe o NGX e roda 2 ou 3 passadas ("Extreme" e "Extreme Plus"). Fechado, distribuído por link do Discord. **Só funciona com o RenoDX da geração v4.55; com v4.6/v4.7 fica inerte** (`dlss5-feed.cpp:492-507`). O app usa v4.7 (ReShade.log do projeto, linha 48).
   - **Deep Fried Chicken (DFC)** (`deep-fried-chicken.addon64`, 1.4.8-alpha): substitui o RenoDX como consumidor neural e tem `layers=1..30`, com parâmetros por camada e um "clean fry" para tirar a deriva de cor. Fechado, Discord. É o sucessor do toolkit pelo mesmo autor.
   - **OptiScaler_DLSSNR** (fork do OptiScaler): não empilha passadas, mas roda o modelo **acima de 100%** (supersampling) e reduz — outra forma de "mais rede".
2. **Resultados relatados de 2+ passadas** [FATO, poucas fontes]: mais "detalhe/profundidade" em foto e vídeo; em jogo em tempo real "derruba o fps completamente"; **borrão atrás de movimento rápido e demora para assentar após corte de câmera** (histórico temporal dobrado); **flicker** que "alguns jogos aceitam melhor que outros"; **deriva de cor cumulativa** (mais frio e mais claro a cada passada, medido); ganho de estrutura com retorno decrescente (1,29x → 1,41x da 1ª para a 2ª passada); 4 passadas ficam inutilizáveis (1,2 fps numa 4090 a 1440p).
3. **Caminho A (duas instâncias do app, sem código)** é **possível hoje, mas não "sem nada"**: a instância única é um mutex de sessão, não de pasta — rodar de duas pastas **não** contorna; só a flag oculta `--outra-instancia` contorna (`main.cpp:5935-5948`, `6007-6009`). Feito com duas pastas separadas e a primeira instância em fonte **Janela** (que não esconde a janela da captura), funciona pelo mesmo mecanismo do Lossless Scaling. Mesma pasta é inviável (cfg do Feeder, logs e ini compartilhados). Custo: dois pipelines completos, piso medido de 18-21 ms cada, mais duas redes.
4. **Caminho B (dentro do processo)** tem três variantes: (B1) Alex's Toolkit drop-in — zero código, mas exige voltar o RenoDX para v4.55 e depender de um binário fechado de link efêmero; (B2) trocar RenoDX por DFC — precisa atualizar o Feeder para ≥ 0.11 e reescrever a parte do painel que fala com o RenoDX; (B3) **modificar o Feeder (código aberto, já compilamos) para chamar `Evaluate` duas vezes** com a saída da primeira como cor da segunda, mesmos depth/MV — é o que o DFC faz internamente (log dele: `pass 2 ... Color=previous-pass Depth=game MVec=game`). A NVIDIA confirma que múltiplas avaliações por quadro funcionam desde que os recursos de entrada/saída sejam distintos. Se o RenoDX v4.7 aceita dois features no mesmo quadro, **só um teste responde**; há indícios positivos ("workset pool ... até 4 gerações").
5. **Recomendação:** não colocar "DLSS 5 x2" como opção visível agora. Fazer primeiro um **teste manual de 30 minutos pelo Caminho A** (receita abaixo) para ver se o resultado visual vale alguma coisa nesta cadeia (MV estimados, não da engine). Se valer, o caminho de produto é **B3** (Feeder próprio com `passes=2`), exposto como opção **experimental** com aviso explícito de custo e de borrão em movimento, e casado com a "Resolução do DLSS" (duas passadas a 77% custam quase o mesmo que uma a 100%). Se não valer, documentar a receita do Caminho A no README como curiosidade, igual à seção do Lossless Scaling, e encerrar.

---

## 1. O que a comunidade faz e chama de "duas passadas"

### 1.1 Terminologia [FATO]

Buscas feitas em 2026-09-05 por `"DLSS 10"`, `"double DLSS 5"`, `"DLSS 5" twice`, `"DLSS 5 2x"` e variações em Reddit/X/YouTube não retornaram nenhum uso desses termos para "aplicar duas vezes". O termo "DLSS 5 On/Off" é meme (Know Your Meme, PC Gamer), não técnica. O vocabulário real da cena é:

| Termo | Onde aparece |
|---|---|
| *two-pass* / *three-pass*, *cascade*, *B->A* / *B->C->A* | Alex's Toolkit; documentação do DLSS5-Feeder (`DEPLOY-DEV.md` §8) |
| *Extreme* (2 passadas) / *Extreme Plus* (3 passadas) | nome dos modos do Alex's Toolkit (issue #1 do 1-Click-DLSS5) |
| *layers*, *multi-pass*, *stacking*, *multipass NR* | Deep Fried Chicken (`layers=`), webcam-demo, vídeo do Pandorian Grey |
| *passes*, *Neural passes* | Easy AIO Installer (Nexus 2251), Merserk/dlss5-visual-enhancer issue #7 |

### 1.2 Ferramentas que fazem isso

**Alex's Toolkit** (`alexs-toolkit.addon64` + `alexs-toolkit.cfg`, v0.9.0-beta) [FATO]

- Descrição do próprio pedido de integração (1-Click-DLSS5, issue #1): *"Adds Extreme two-pass, Extreme Plus three-pass, adaptive blend controls and an experimental native 8K Texture Boost for compatible 4K paths."* Autor testou em *"RTX 4080 SUPER with NVIDIA driver 616.56, ReShade 6.8.0.2155 and RenoDX DLSS 5 Neural Rendering 0.2026.827.2036"*. Distribuído por link `cdn.discordapp.com` (expira).
- Mecanismo, segundo o Feeder (`dlss5-feed.cpp:374-397`): interpõe o NGX, cria features privados (A, B, C) e por quadro roda *"private pass -> ... native"* em cascata. *"It does NOT mishandle our inputs: every stage has the same input dimensions ... What it costs is temporal: each stage keeps its OWN history, so a two-pass cascade roughly doubles the effective history length and a three-pass one triples it. With screen-space estimated motion vectors -- which are inherently one frame late -- that reads as smearing and lag behind fast motion, and it multiplies how long the image takes to settle after a hard camera cut."* O autor do Feeder diz ter uma captura de 16 mil quadros sem fallback.
- Config: `enabled=1`, `two_pass=1`, `three_pass=0` (lida ao vivo). O Feeder detecta e mostra no overlay: *"Alex's Toolkit 0.9.0-beta: 2-pass DLSS 5 cascade active -- roughly 2x the temporal history"* (`dlss5-feed.cpp:477-481`, `5688-5691`).
- **Limitação decisiva para nós**: *"That signature only matches the older (v4.55-era) build: against v4.6 and v4.7 its scan finds 'candidates=0 (expected exactly 1)', it declines to touch the IAT, and it STOPS RETRYING for the whole process -- so the cascade silently does nothing"* (`dlss5-feed.cpp:492-507`). O app roda **RenoDX DLSS5 Generic v4.7 (build Sep 2 2026)** (ReShade.log da pasta do projeto, linha 48; `dlss5-feed.log` diz `v0.2026.828.517 -- v45+`).
- Corrida de carga: precisa se prender antes de o RenoDX resolver `nvngx_dlssnr.dll` pela primeira vez, senão fica pass-through a sessão inteira (`dlss5-feed.cpp:390-396`).

**Deep Fried Chicken (DFC)** (`deep-fried-chicken.addon64`, `-nvngx.dll`, `.cfg`; 1.4.8-alpha) [FATO]

- Issue #7 do Merserk/dlss5-visual-enhancer (1 set 2026): *"Alex has replaced alexs-toolkit with a new standalone add-on called Deep Fried Chicken (DFC). Instead of repeated spatial upscaling, it chains multiple DLSS Neural Rendering post-passes on the resolved frame using native depth and motion vectors."* Pede *"a pass count slider (1x–10x) in the Gradio UI for batch and offline rendering"*. O snippet da busca do mesmo issue resume o consenso: *"in real-time games it can tank FPS completely, but for offline batch tools the extra passes add significant detail and depth."*
- README do Feeder: *"Start with one pass. It can do up to 30. That is a stress test, not a starting point."* Config padrão *"on, one pass, extras off"*. Exclusivo: com RenoDX ou Alex's Toolkit na pasta *"it does nothing at all for the whole session — silently"*.
- Como faz a 2ª passada (log reproduzido no README do jpneagle/dlss5-webcam-demo):
  ```
  create #1 feature 18: A=0x00000001 Success; MODE=TWO_PASSES requested=2
  evaluate #1 pass 1 handle=... Color=game          Depth=game MVec=game
  evaluate #1 pass 2 handle=... Color=previous-pass Depth=game MVec=game
  ```
  Ou seja: **cor = saída da passada anterior, depth e MV = os mesmos do jogo.** É exatamente o contrato que a pergunta 3 propõe.
- Chaves relevantes do `deep-fried-chicken.cfg`: `layers=2`, `preserve_native_tone_color`, `clean_fry_enabled` (*"multi-pass cleanup; kills the colour cast but also detail"*), `clean_fry_cleanup_strength=0.650`, `neural_work_percent`, `layer_N_nr_preset / nr_style / intensity / local_tone / local_structure / skin_structure`.
- 1-Click-DLSS5 issue #11 (3 set 2026): usuário pede o DFC porque *"can perform multiple DLSS 5 image processing, effectively stacking several layers of DLSS 5 image effects, which is excellent"* — e diz que não conseguiu fazer funcionar.
- Easy AIO Installer (Nexus 2251): changelog *"now allows you to set multiple Neural passes"* (snippet de busca; página bloqueou o fetch).

**OptiScaler_DLSSNR** (Dagherbou) v0.2.0, 3 set 2026 [FATO]: *"The model-resolution slider now goes past 100%. Above native the model runs supersampled — on a larger, cleaner input — and its answer is averaged back down ... It is costly — the model's time grows with the area — so it is a quality lever for when you have the headroom."* Não é duas passadas, mas é a outra resposta da cena para "quero mais rede".

**OBS**: existe filtro `obs-dlss5-nr` (Saganaki22) que roda NR em qualquer fonte, *"per-frame (no temporal flow yet)"*, com botão *"Reset History (fixes smearing after scene cuts)"*. Não achei relato de gente encadeando OBS + projetor duas vezes; o que se faz é encadear **dentro** do add-on (DFC) ou no OptiScaler.

### 1.3 Resultados visuais e de desempenho relatados

| Fonte | Setup | Resultado |
|---|---|---|
| jpneagle/dlss5-webcam-demo (README, medições) [FATO] | RTX 4090, driver 616.56, webcam 1280x720 → 2560x1440, DFC 1.4.8, sliders no máximo (2,00) | `layers=1`: 30 fps, estrutura 1,29x vs bicúbico; `layers=2`: 30 fps, **1,41x**; `layers=4`: **1,2 fps ("unusable")**. Deriva de cor: 1 passada R−B −14,7 / luminância +5,4; **2 passadas −15,9 / +10,1** ("NR shifts the output cooler and brighter ... inherent to NR, not a consequence of stacking" — mas o brilho **acumula**). *"DLSS 5 NR is designed to constrain its output to the input. It does not change a person's identity; facial change is a failure mode to guard against, not a feature."* |
| Pandorian Grey, YouTube `MEP4D99RNJ0` (descrição) [FATO] | Wanted: Dead, RTX 5090, add-ons 4.55/4.6/4.7/ShortFuse Multi/DFC | *"I was trying to reduce the flicker from the high level of multipass NR, but it seems some games adopt better than others and there is no real one size fits all solution. The most graphically stable in this case seemed to be v4.55 or Shortfuse Multi at 1x but with a reduced NR realistic look."* |
| DLSS5-Feeder `DEPLOY-DEV.md` §8 e `dlss5-feed.cpp:374-397` [FATO] | Feeder + Alex's Toolkit, MV estimados | *"the doubled history shows up as smearing behind fast motion and a slow settle after a hard camera cut. If a cutscene transition looks like the feeder briefly stopped working and then recovered, that is the cascade's history re-converging"*. |
| Merserk issue #7 (snippet) [FATO] | offline/batch | *"extra passes add significant detail and depth"*; em tempo real *"tank FPS completely"*. |
| 1-Click-DLSS5 issue #11 [FATO] | — | *"stacking several layers ... is excellent"* (impressão do usuário, sem números). |

Não encontrei relato textual de "aquarela"/"pintura a óleo" ligado especificamente a múltiplas passadas. O que aparece ligado a uma passada, em implementações não oficiais, é *"unpleasantly plastic"* e *"worms writhing around under their skin"* (snippets de busca sobre mods; sem link direto confiável, tratar como boato).

Custo de **uma** passada, para calibrar (todos [FATO]): HotHardware (4 set 2026), NBA 2K27, RTX 5070 Ti a 1440p: 240 → 68,9 fps médios; Wccftech (mods, várias GPUs): 155-250 → 118-125 fps; gamegpu (título): RTX 4090 ultrawide perde 33% de fps e +50% de frame time; nossa medição: `docs/latencia-nr.md:18-25` (+3 a +4,5 ms a 1080p; +2 ms de frame e ~+10 ms fim a fim a 3440x1440 numa 4080), e `main.cpp:6454-6457` ("30 ms por quadro ... 96% eram a rede" em ultrawide a 100%).

---

## 2. Caminho A — duas instâncias do app encadeadas (sem código)

Cadeia proposta: jogo → instância **A** (fonte Janela = jogo, janela separada) → instância **B** (fonte Janela = janela de A) → tela. É o mesmo padrão do Lossless Scaling documentado em `README.md:228-258`.

### 2.a Instância única [FATO]

- O bloqueio é um **mutex nomeado de sessão**: `#define DLSS5_MUTEX_INSTANCIA L"Local\\DLSS5ForAll.instancia"` (`main.cpp:5930`), criado em `main.cpp:6007-6009`. O nome não contém a pasta nem o hash do exe. **Logo, rodar de duas pastas diferentes NÃO contorna**: a segunda cópia recebe `ERROR_ALREADY_EXISTS`, mostra o MessageBox "já está aberto" e sai (`main.cpp:6062-6103`).
- O que contorna é a linha de comando: `--outra-instancia` (e os modos sem camada `--panel-shot`, `--guia-shot`, `--list`, `--list-windows`) em `LinhaDeComandoIgnoraInstancia()` (`main.cpp:5935-5948`). Com ela, o app segue e loga "instancia: ja existe outra, mas a linha de comando pediu para seguir assim mesmo" (`main.cpp:6104-6106`). `docs/correcoes-entrada.md:561` chama isso de "a porta explícita".
- A mensagem de bloqueio já antecipa o problema: *"Duas cópias juntas empilham duas camadas na mesma tela e a segunda fica sem tecla de saída"* (`main.cpp:6076-6079`).
- Detalhe útil: mesmo ignorando o mutex, a segunda cópia abre o log em modo *append* (`modoLog = outraInstancia ? "a" : "w"`, `main.cpp:6017`), então não trunca o log da primeira **se estiverem na mesma pasta**.

**Conclusão (a):** Caminho A exige `--outra-instancia` na segunda cópia. Não é "sem código", é "sem código novo": a porta existe, mas é oculta e não está no seletor.

### 2.b A janela de A pode ser capturada por B? [FATO]

Depende da **fonte** de A, não do modo:

| Configuração de A | `hideSelf` | B consegue capturar A? |
|---|---|---|
| Fonte **Janela**, seletor | caixa "Esconder esta janela..." vem **desmarcada** (`main.cpp:2010`) | **Sim** |
| Fonte **Janela**, CLI | `C.hideSelf = argHide` (padrão falso; `--hide-self` liga) (`main.cpp:6275`, `6189`) | **Sim** por padrão |
| Fonte **Monitor**, seletor | caixa vem **marcada** (`main.cpp:2009`) | **Não**, a menos que a pessoa desmarque |
| Fonte **Monitor**, CLI | `C.hideSelf = !argNoHide` (padrão ligado; `--no-hide-self` desliga) (`main.cpp:6285`, `6190`) | **Não**, salvo `--no-hide-self` |
| Monitor + Sobreposto | forçado ligado (`main.cpp:2375`, `6410`) | **Não**, e não dá para desligar |

O mecanismo é `HideFromCapture()` → `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` (`main.cpp:2726-2734`), aplicado em `main.cpp:6531-6533` só quando `C.hideSelf`. O README já documenta a consequência para o LS e o OBS: *"Fonte monitor (qualquer modo): a nossa janela fica com WDA_EXCLUDEFROMCAPTURE e o LS enxerga tela preta"* (`README.md:255`, `275`).

Também relevante:

- O painel, o cartaz-guia e o toast de A são janelas separadas, `WS_EX_TOOLWINDOW` e escondidas da captura (`main.cpp:2862`, `4849`, `5285`). B não os verá — bom.
- A lista de janelas de B vai **listar a janela de A**: `WgcEnumProc` exclui só o próprio HWND, tool windows, janelas cloaked e menores que 64 px (`screen_capture.h:241-262`); a janela de A tem classe `FafaDLSS5CamWnd`, título `"DLSS5 For All - <fonte>"` (`main.cpp:77`, `6412`) e é `WS_OVERLAPPEDWINDOW`. Na CLI, `--window` casa por trecho do título ou do nome do processo (`main.cpp:6268-6271`), então `--window "DLSS5 For All"` acha A.
- [INFERÊNCIA] Em WGC por janela (`CreateForWindow`) a captura pega o conteúdo da própria janela, não o que está por cima; por isso B pode até ficar em modo sobreposto em cima de A sem espelho infinito. Mas, no modo sobreposto, a camada de B "reafirma o topo a cada 1 s" (`README.md:256`) e A fica escondida atrás — o usuário só vê B, o que é o desejado. Não testei.
- Ao sobrepor A por B, a heurística de "alvo em primeiro plano" de B (`FollowTarget`, `main.cpp:5680-5690`) considera qualquer janela do **próprio processo** como "nossa", mas A é outro processo: se o foco for para o jogo, a camada de B some (comportamento normal de overlay sobre uma janela que perdeu o foco). [INFERÊNCIA] O fluxo de foco jogo ↔ A ↔ B pode fazer a camada de B piscar; é um dos pontos a testar.

**Conclusão (b):** funciona se A usa fonte **Janela** (padrão do seletor) ou monitor com a caixa desmarcada / `--no-hide-self`. Monitor sobreposto em A é incompatível por construção.

### 2.c Hotkeys globais [FATO]

- Padrões: `End` painel, `F6` liga/desliga tudo, `Alt+Esc` sair (`main.cpp:1429-1433`). São `RegisterHotKey(nullptr, ...)` globais (`main.cpp:1485-1493`). A segunda instância **falha** ao registrar as mesmas teclas e cai na cascata de reserva: painel `End → Insert → Pause → Scroll → Ctrl+Alt+D → Ctrl+Alt+P`; tudo `F6 → F7 → Alt+F6 → Ctrl+Alt+T`; sair `Alt+Esc → Ctrl+Alt+Q → Ctrl+Alt+F12 → Ctrl+Shift+Q` (`main.cpp:1470-1482`). A pessoa vê o toast "Alguma tecla já estava em uso" (`main.cpp:6742-6749`) e o cartaz-guia mostra a tecla que ficou de fato.
- No modo sobreposto há uma segunda cascata (`Alt+F9...`, `Alt+F10...`, `Alt+F12...`, `main.cpp:6657-6698`) com a mesma lógica.
- A tecla do overlay do ReShade (`Home`, lida do `ReShade.ini`) é por janela em foco, não global: cada instância abre o seu overlay quando está em foco. O toggle de NR do RenoDX (`F6` interno) é entregue pela ponte dentro de cada processo (`main.cpp:5531-5543`), não é hotkey global.
- [INFERÊNCIA] Funciona, mas é confuso: `F6` desliga só A (a primeira que registrou), `F7` desliga só B. Um "antes e depois" honesto exigiria desligar as duas. Se isso virar receita documentada, o texto tem de dizer qual tecla é de quem.

### 2.d Arquivos: mesma pasta vs. pastas diferentes [FATO]

Na **mesma pasta** os conflitos são reais e alguns quebram a primeira instância:

| Arquivo | Quem escreve | Efeito com duas instâncias |
|---|---|---|
| `dlss5-feed.cfg` | o app, na abertura, grava `work_resolution` escolhido pela área da fonte (`GravarWorkResolution`, `main.cpp:2613-2645`, chamado em `6481`) | B **sobrescreve** o valor de A; o Feeder **relê o cfg a cada 60 quadros** (`dlss5-feed.cpp:5070`) e A muda de resolução de trabalho no meio da sessão |
| `dlss5-feed.log` | Feeder, em *append* (`dlss5-feed.cpp:79-104`), caminho fixo ao lado do add-on (`874-880`) | linhas das duas instâncias misturadas; o app lê a cauda desse log para dizer se o feed entregou (`main.cpp:3475-3482`) |
| `ReShade.log` | ReShade rotaciona na abertura (`main.cpp:3265`) | [INFERÊNCIA] B rotaciona o log que A está lendo; o detector de estado do NR de A (`main.cpp:3447-3450`, procura `inline feature 18 evaluation succeeded`) passa a ler o log de B |
| `ReShade.ini` | ReShade (AutoSave) e o app (`KeyOverlay`, `ShowFPS`, `NeuralUplift`) | último a gravar vence; o app faz backup a cada gravação (`main.cpp:1596-1624`), veja a pilha de `.bak-keyoverlay-*` na pasta |
| `dlss5forall.ini` | o app (`IniSet`, `main.cpp:6347-6367`, `7136`) | último a gravar vence: fonte, janela, `work_resolution`, atalhos |
| `dlss5forall.log` | o app; a segunda abre em *append* (`main.cpp:6017`) | intercalado, mas não truncado |
| `reshade-cache\` | ReShade | compartilhável sem problema |

Em **duas pastas** cada instância tem seu cfg, logs, ini e ReShade.ini próprios; só sobram o mutex (precisa de `--outra-instancia`) e as hotkeys. **Duas pastas é obrigatório.** O pacote portátil (`DLSS5ForAll-Portable\bin`) já é uma segunda pasta pronta.

### 2.e Latência e custo esperados

- [FATO] Piso medido do app com a GPU livre: **18-21 ms** do quadro capturado ao pixel na tela (`README.md:279`; `docs/latencia-nr.md:325-327`), sendo ~9 ms de espera pela swapchain e ~9 ms de Present até o vblank. Com NR: +3 a +4,5 ms a 1080p, ~+10 ms fim a fim a 3440x1440 (`docs/latencia-nr.md:18-25`).
- [INFERÊNCIA] Dois estágios em série somam: ≥ **36-42 ms** de piso + duas redes, ou seja algo entre **45 e 60 ms** a 1080p e mais a ultrawide, antes de qualquer frame generation. Para comparação, o README classifica o LS por cima (+25 a +42 ms a 60 fps) como "serve pra jogo lento e pra vídeo; não serve pra competitivo" (`README.md:263-264`). Duas instâncias ficam nessa mesma classe ou pior.
- [INFERÊNCIA] GPU: as duas redes processam a mesma área, então o tempo de rede dobra. Na 4080 a 1080p o README espera "cair pra ~metade do FPS" com uma passada (`README.md:179`); com o tempo de rede igual ao do resto do pipeline, duas passadas levam a ~1/3 do fps original. Em ultrawide, onde a rede já era 96% dos 30 ms (`main.cpp:6454-6457`), duas passadas a 100% dariam ~55-60 ms/quadro (16-18 fps).
- [INFERÊNCIA] B só recebe quadro novo quando A apresenta; o fps de B é limitado pelo de A, e há duas filas WGC em série (cada uma pode entregar em rajada quando o consumidor atrasa, ver `docs/latencia-nr.md:161-162`).
- [INFERÊNCIA] VRAM: dois processos, cada um com `nvngx_dlssnr.dll` (165 MB em disco) e seu working set de modelo, duas swapchains, dois devices D3D12 privados. Não medi.

### 2.f Receita de teste do Caminho A (para responder as perguntas da seção 6)

1. Pasta 1 = pasta do projeto; pasta 2 = `DLSS5ForAll-Portable\bin` (já existe, cfg/ini próprios).
2. Instância A (pasta 1): seletor → fonte **Janela** = jogo, **SOBREPOR desmarcado**, "Esconder esta janela" **desmarcado**.
3. Instância B (pasta 2): `DLSS5ForAll.exe --outra-instancia --source window --window "DLSS5 For All"` (janela separada) ou com `--overlay` para sobrepor A.
4. Conferir no toast de B quais teclas ficaram (`Insert`/`F7`/`Ctrl+Alt+Q` esperados).
5. Comparar: jogo cru / só A / A+B, parados e em movimento rápido; corte de câmera; fps no título de cada janela; `LAT` no `dlss5forall.log` de cada pasta.

---

## 3. Caminho B — duas passadas dentro do processo

### 3.0 O que o Feeder é hoje [FATO]

- Contrato: o Feeder copia cor (backbuffer), depth e MV (de `DLSS5_Feed.fx`) para texturas compartilhadas com um device D3D12 privado, roda **um** `NVSDK_NGX_D3D12_EvaluateFeature` em modo DLAA e copia o resultado de volta (`dlss5-feed.cpp:6-14`). O RenoDX intercepta esse Evaluate e insere o feature 18 (NR).
- Estado global único: `struct Feed` tem **um** `params`, **um** `feature`, **um** conjunto `tex12[SLOT_COUNT]` com slots `Color, Output, Depth, MV, Mask` (`dlss5-feed.cpp:1034`, `1208-1209`, `1216-1218`), instanciado como `static Feed g;` (`1269`). O caminho D3D11 (o do app) é `FeedFrame11` (`5060-5313`): sinaliza a fence, faz barreiras, monta `NVSDK_NGX_D3D12_DLSS_Eval_Params` com `pInColor = tex12[SLOT_COLOR]`, `pInOutput = tex12[SLOT_OUTPUT]`, depth, MV, máscara e `InReset` (`5228-5243`), chama `SafeEvaluateDLSS` (`5247`), desfaz barreiras, `EndCommands`, espera e `BlitOutputToBackbuffer` (`5271`).
- Serialização: `FeedEnter/FeedLeave` com `g_feed_cs` (`751-806`); uma passada por Present é o contrato publicado ao DFC (`DFC_EVALUATE_CADENCE 1u // one Evaluate per Present`, `feed_dfc.h:52`; `dlss5-feed.cpp:1594-1600`).
- Runtime: o Feeder rastreia até 6 effect runtimes mas **liga-se a um só**, o que renderiza a técnica `DLSS5_Feed` de `DLSS5_Feed.fx` (`kEffectFile/kTechnique`, `1036-1037`; `OnInitEffectRuntime` `5504-5543`; `OnRenderTechnique` `5583-5610`, que ignora qualquer técnica cujo handle não seja `g.technique.handle` em `5609`).
- A versão empacotada no app é a **0.7.0** (`ReShade.ini` do projeto, `[ADDON] OverlayCollapsed=...DLSS 5 Feed 0.7.0...`; `dlss5-feed.addon64` datado 2026-09-01). A cópia de fonte no scratchpad é mais nova (tem interop DFC, que chegou na 0.11.0-beta.1) e há um `feeder-build.bat` no scratchpad, ou seja, o time já compila o Feeder.

### 3.1 Variante B4 (descartada): uma segunda técnica de ReShade que refaz o contrato

[FATO] Não funciona sem código: o Feeder aceita **uma** técnica com **um** nome fixo por processo (`5609`) e um único `g.feature`. Uma segunda cópia de `DLSS5_Feed.fx` com outro nome seria ignorada; uma segunda cópia do add-on com outro nome de arquivo compartilharia `dlss5-feed.cfg`/`.log` (caminhos derivados do próprio módulo, `874-880`) e brigaria pelo mesmo runtime. Descartar.

### 3.2 Variante B1: Alex's Toolkit drop-in

- [FATO] Zero código: copiar `alexs-toolkit.addon64` + `.cfg` para a pasta e ligar `two_pass=1`. O Feeder já detecta e reporta (`416-508`), já protege a corrida de carga com `create_delay` (`393-397`, `5200-5212`) e o overlay avisa quando está inerte (`5682-5687`).
- [FATO] **Bloqueio:** com o RenoDX v4.7 do app o toolkit fica inerte (`492-507`). Teria de voltar o `renodx-dlss5.addon64` para a geração v4.55 — perdendo a ponte de cor reversível (SDR sRGB / HDR BT.709 / PQ) e o pool de worksets da 4.7 (`dlss5-feed.cpp:183`, `337`), e desfazendo o trabalho de HDR do app (`docs/hdr.md`, `docs/cor-hdr-e-filtros.md`).
- [FATO] Distribuição: binário fechado num link de CDN do Discord (expira); o próprio autor já o substituiu pelo DFC. Não dá para embutir no instalador nem para prometer suporte.
- Veredito: bom para **um teste rápido de qualidade** se alguém tiver o arquivo e a v4.55 (existe `renodx-dlss5.addon64.bak-4.1.5` na pasta do projeto, que é mais antiga ainda — não sei se o toolkit a reconhece). Não é caminho de produto.

### 3.3 Variante B2: trocar o RenoDX pelo Deep Fried Chicken

- [FATO] O DFC faz N passadas nativamente, com `layers`, presets/intensidade por camada e `clean_fry` para a deriva de cor. Ele **substitui** o RenoDX (exclusão mútua; `dlss5-feed.cpp:566-571`).
- [FATO] Pré-requisito: Feeder ≥ 0.11.0-beta.1 (interop ABI 1). O app está na 0.7.0. Atualizar o Feeder já estava na lista por outro motivo (`docs/viabilidade-framegen-upscaling.md:491`: "corrupção/crash se ficar no 0.7.0" com o Smooth Motion).
- [FATO] O autor do Feeder validou o DFC em jogo só em Vulkan 64 bits e no helper de 32 bits; o caminho D3D11 (o do app) é "source-contract compatible per its author" — **não testado** (README do Feeder, "Known limitations").
- [FATO] O app está amarrado ao RenoDX em vários pontos: a ponte alterna o NR pela tecla do RenoDX (`ReShade.log` do projeto, linha 90: "tecla do RenoDX = 0x75"), o painel lê `[RenoDX.DLSS5] NeuralUplift` (`main.cpp:3177`) e o estado "ATIVO" vem das strings do RenoDX no `ReShade.log` (`main.cpp:3448-3450`). Com DFC, tudo isso precisa de um segundo caminho (strings, ini `deep-fried-chicken.cfg`, aba própria no overlay).
- [INFERÊNCIA / risco de nome] O README do Feeder manda não ter "DX11 bridge" na pasta junto com o DFC (refere-se ao `dlss5-dx11-bridge` do NIGos, `README` do Feeder linhas 375-377 e 392). O add-on de controle do app chama-se `dlss5-bridge.addon64` e exporta o nome "DLSS 5 Bridge". Se o DFC detecta o bridge do NIGos por nome de arquivo ou por substring, **pode se declarar inerte por causa do nosso add-on**. Só um teste responde.
- [FATO] Windows Defender sinaliza o DFC (ele usa Detours no NGX), segundo o instalador do Feeder — atrito de instalação para o público do app.
- Esforço [INFERÊNCIA]: 2-4 dias (atualizar Feeder, refazer a integração de estado/toggle do painel, refazer testes de HDR e latência), mais o risco de o DFC não rodar no caminho D3D11. Ganha-se um multi-pass configurável e mantido por terceiro; perde-se controle e o RenoDX v4.7 cuja cor/HDR já foi afinada.

### 3.4 Variante B3: modificar o Feeder para avaliar duas vezes (recomendada se for fazer)

O que o DFC faz internamente (`Color=previous-pass, Depth=game, MVec=game`) cabe no Feeder com mudanças localizadas:

1. `Cfg`: nova chave `passes` (1 ou 2), lida ao vivo como `work_resolution` (`920-992`, `1013-1031`), exposta no overlay do Feeder e, no app, como chip ao lado de "Resolução do DLSS" (`main.cpp:3157-3158`, escrita no cfg como em `GravarWorkResolution`).
2. `struct Feed`: um segundo par `params2/feature2`, uma textura `SLOT_OUTPUT2` (mesmo formato/tamanho de `SLOT_OUTPUT`), criados em `BuildResources`/`SafeCreateDLSS` com **os mesmos** `NVSDK_NGX_DLSS_Create_Params` (DLAA 1:1). Recriar/soltar junto com o primeiro em todos os caminhos de rebuild (há quatro pontos que comentam "re-create its own feature": `4062`, `4319`, `4888`, `5163`).
3. `FeedFrame11`: após o primeiro `SafeEvaluateDLSS` bem-sucedido, barreira `SLOT_OUTPUT: UAV → NON_PIXEL_SHADER_RESOURCE`, `SLOT_OUTPUT2: COMMON → UAV`, segundo `Eval_Params` com `pInColor = tex12[SLOT_OUTPUT]`, `pInOutput = tex12[SLOT_OUTPUT2]`, mesmos `pInDepth/pInMotionVectors/pInBiasCurrentColorMask`, mesmo `InReset`, mesmo `InRenderSubrectDimensions`; segundo `SafeEvaluateDLSS` com `g.feature2`; barreiras de volta; `BlitOutputToBackbuffer` lendo `SLOT_OUTPUT2` (o `output_srv` hoje aponta para `SLOT_OUTPUT`, `1214`).
4. [FATO] A NVIDIA confirma o requisito: no fórum de desenvolvedores, sobre encadear dois Evaluates na mesma view, a resposta foi *"multiple evaluations can be done if different input and output resources are used each time"* (o caso relatado com o mesmo recurso dava imagem preta).
5. Máscara e reset: passar `InReset` igual nas duas; [INFERÊNCIA] a segunda feature deve resetar junto porque seu histórico é da saída da primeira.

**O RenoDX aguenta dois features no mesmo quadro?** Ninguém documenta. Indícios:

- [FATO] O Alex's Toolkit fez exatamente isso (features privados A/B/C e Evaluates encadeados) sobre o RenoDX v4.55, e o Feeder confirma "16k-frame capture shows zero fallbacks" (`dlss5-feed.cpp:383`). Logo, pelo menos uma geração do RenoDX aceita múltiplos features/Evaluates por quadro.
- [FATO] O RenoDX v4.7 loga *"GPU-safe NR workset pool active: up to 4 scratch generations; exact queue fences recycle only completed submissions"* e *"private feature-18 GPU ordering active: same-queue NR stays wait-free; only unfinished cross-queue reuse of the same temporal feature inserts a GPU wait"* (ReShade.log do projeto, linhas 51-52). [INFERÊNCIA] Isso sugere que ele mantém estado temporal **por feature** e tem espaço para mais de um workset em voo — compatível com dois features por quadro, mas não é garantia.
- [FATO] O Feeder documenta que criar/recriar features perto do re-arme dos hooks do RenoDX já foi fonte de crash (`5553-5556`); dobrar o número de creates dobra a superfície. O `create_delay` existente cobre isso se os dois creates acontecerem juntos.
- [FATO] Se um dia for DFC em vez de RenoDX, o DFC tem orçamentos ("feature-pair lifetime budget (4)") e um contrato de "um Evaluate por Present" (`FEEDBACK-DFC.md`; `feed_dfc.h:52`) que um Feeder com duas passadas violaria; ali a resposta certa é usar o `layers` do próprio DFC, não duplicar no Feeder.

Esforço [INFERÊNCIA]: 1-2 dias de código (150-300 linhas no `dlss5-feed.cpp`, caminho D3D11 primeiro; os caminhos D3D12/Vulkan/GL podem ficar em `passes=1`), meio dia no `main.cpp` (chip, cfg, aviso), e **o grosso em teste**: estabilidade do RenoDX com dois features, VRAM, tempo por passada, comportamento em reset/redimensionamento/alt-tab. Custo de manutenção: o Feeder upstream anda rápido (0.7.0 → 0.13.1-beta.1 em poucos dias, segundo o playground.ru); um fork com passes precisa ser rebaseado ou o patch enviado upstream (o autor já mostrou interesse em multi-pass ao detectar o toolkit e ao pedir o modo feeder ao DFC: *"DLSS-less games are precisely where a 10-pass neural pipeline has the most room to shine"*, `FEEDBACK-DFC.md`).

---

## 4. Qualidade: o que acumula quando a rede vê a própria saída

### 4.1 O que se sabe do modelo [FATO]

- NVIDIA (Jacob Freeman, 20 mar 2026, via Notebookcheck): *"DLSS 5 only takes rendered frame and motion vectors as inputs, while materials are inferred from the rendered frame."*
- Página de pesquisa da NVIDIA: o modelo é *"conditioned on the current rendered frame, engine motion vectors, carried temporal state, and artistic-direction values"*, usa *"learned appearance priors"*, é *"causal and deterministic"* e *"trained for frame-to-frame temporal stability"*.
- Back2Gaming (2 set 2026): *"DLSS 5 uses engine motion vectors and runs strictly one frame in, one frame out, which is what keeps the output free of shimmer, swimming and drift."*
- webcam-demo: *"DLSS 5 NR is designed to constrain its output to the input. It does not change a person's identity; facial change is a failure mode to guard against."*
- O app já roda os sliders no teto: `intensity=2.000000 global_tone=2.000000` (ReShade.log do projeto, linha 47); o webcam-demo confirma que 2,00 é o clamp. Ou seja, "mais efeito" hoje só vem de mais passadas ou de supersampling.

### 4.2 O que acumula (medido ou documentado) [FATO]

- **Histórico temporal dobrado**: cada passada tem seu próprio estado temporal; com MV estimados (um quadro atrasados) isso aparece como **borrão atrás de movimento rápido e assentamento lento após corte de câmera** (Feeder, `dlss5-feed.cpp:384-389`; `DEPLOY-DEV.md` §8).
- **Flicker**: "high level of multipass NR" flicka, e "some games adopt better than others" (Pandorian Grey).
- **Deriva de tom**: luminância +5,4 → +10,1 da 1ª para a 2ª passada; cor mais fria (webcam-demo). O DFC precisou de um `clean_fry` que "kills the colour cast but also detail". No app, a correção poderia ir no `DLSS5_Grade.fx` (pós-NR), como o webcam-demo faz na apresentação.
- **Retorno decrescente**: estrutura 1,29x (1 passada) → 1,41x (2 passadas): a segunda passada acrescentou 0,12 onde a primeira acrescentou 0,29. E o custo é linear ou pior (4 passadas: 1,2 fps).

### 4.3 Raciocínio sobre o que mais deve acontecer [INFERÊNCIA]

- A rede projeta a imagem para uma "aparência fotográfica" aprendida. Rodar de novo empurra mais na mesma direção: pele/cabelo/tecido ficam mais "de foto", texturas planas ganham microestrutura inventada, e **a segunda passada trata as invenções da primeira como verdade**. É o mecanismo clássico de amplificação de artefato em filtros generativos iterados: onde a primeira passada errou (rosto ligeiramente diferente, reflexo inventado), a segunda consolida.
- No **Caminho A** o problema é pior que no B: B (a segunda instância) recalcula depth e MV **sobre a saída de A** (Lumenite_Kernel / optical flow), isto é, estimativas de segunda geração feitas sobre uma imagem já filtrada temporalmente e com detalhes sintéticos. Onde A inventou microdetalhe, o optical flow de B vê "textura" que se move de forma inconsistente; onde A suavizou, B tem menos gradiente para casar. No **Caminho B3/DFC** a segunda passada recebe os **mesmos** depth/MV da primeira (é o que o DFC faz), então pelo menos a guia não degrada; sobra o histórico dobrado.
- Rosto/identidade: o próprio autor do webcam-demo trata "mudança de rosto" como modo de falha. Duas passadas dobram a chance. Em jogos com personagens conhecidos isso é o que vai virar reclamação.
- HDR: a segunda passada recebe a cor já reconstruída pela ponte reversível da 4.7; se a saída da 1ª passada estiver em outro espaço (linear vs sRGB) que a entrada esperada, a 2ª passada pode ler tom errado. Precisa conferir formato de `SLOT_OUTPUT` vs `SLOT_COLOR` (`OutputFormatFor`, `1364`; `ResolveOutputFormat`, `1424`).

### 4.4 Quando faria sentido e quando não [INFERÊNCIA, apoiada nos relatos]

Faz sentido:
- Fonte **pobre em detalhe**: jogos antigos, emuladores (PS2/Wii), vídeo comprimido, webcam, placa de captura de console antigo — é o caso do webcam-demo, onde 720p → 1440p ganhou 1,41x de estrutura a 30 fps estáveis. Quanto menos informação verdadeira na entrada, mais espaço a segunda passada tem para "acrescentar" e menos há para destruir.
- **Vídeo e cenas lentas**, onde 50-60 ms de latência e o assentamento pós-corte não importam.
- Uso **offline/gravação**, que é onde a comunidade diz que "adds significant detail and depth".

Não faz sentido:
- Jogo moderno já detalhado, onde a primeira passada já está no teto e a segunda só inventa e desloca tom.
- Qualquer coisa com movimento rápido de câmera ou cortes frequentes (histórico dobrado + MV estimados).
- Competitivo (latência) e arte estilizada (a rede "fotorrealiza" o que não devia).

---

## 5. Recomendação

**Não oferecer "DLSS 5 x2" como opção normal do painel agora.** Motivos: ninguém publicou um caso convincente de duas passadas em jogo em tempo real (os relatos positivos são offline/vídeo; os relatos em jogo falam de fps que despenca, borrão e flicker); o custo dobra numa parte que já é "a parte cara" e "irredutível" segundo os próprios docs do app; e a cadeia do app usa MV estimados, exatamente o cenário em que o autor do Feeder diz que o histórico dobrado dói mais.

**O que fazer, em ordem:**

1. **Teste manual pelo Caminho A (meia hora, zero código)** com a receita da seção 2.f: duas pastas, A em fonte Janela sem "esconder", B com `--outra-instancia`. Filmar jogo cru / A / A+B em cena parada, em movimento rápido e num corte de câmera; anotar fps de cada janela e `LAT` dos dois logs. Se o ganho visual não for óbvio numa cena parada, **parar aqui** e, no máximo, registrar a receita no README como curiosidade ao lado da seção do Lossless Scaling, com os avisos abaixo.
2. Se o resultado parado valer a pena, **implementar B3** (Feeder com `passes=2`), porque é o único caminho que fica dentro do processo, mantém o RenoDX v4.7 (cor/HDR já afinados), não depende de binário fechado de link efêmero e é medível com a instrumentação que já existe (`stall_log_ms`, `LAT`). Manter como **opção experimental**, desligada por padrão.
3. **Não** fazer B1 (toolkit) em produto; usar só se alguém quiser um segundo dado de qualidade e tiver a v4.55 à mão. **Não** fazer B2 (DFC) por causa desta feature; reavaliar o DFC por seus próprios méritos quando o Feeder for atualizado.

**Se virar opção na interface:**

- Lugar: chip "Passadas: 1 / 2" ao lado de "Resolução do DLSS", pois o custo é da mesma natureza (área × passadas). Auto: nunca 2.
- Casar com resolução: `2 passadas a 77%` ≈ 1,19× o custo de `1 passada a 100%` (0,77² × 2); `2 a 67%` ≈ 0,90×. É a única forma de oferecer x2 sem dobrar o custo — e é uma das perguntas de teste (o webcam-demo sugere que "menos entrada, mais reconstrução" ajuda a segunda passada).
- Avisos no painel, curtos: "Experimental. Dobra o custo da rede. Borra em movimento rápido e demora a assentar após cortes de câmera. Clareia a imagem." e, no README, a tabela de custo medida.
- Custo esperado (a confirmar em teste): 1080p na 4080, de ~metade do fps (1 passada) para ~um terço (2 passadas a 100%); ultrawide a 100%: ~16-18 fps; fim a fim +3 a +10 ms a mais por passada conforme a área.
- Compensar a deriva de tom no `DLSS5_Grade.fx` (ganho por canal pós-NR), como o webcam-demo faz, em vez de mexer no que a rede vê.

---

## 6. Perguntas que só um teste responde

1. **O RenoDX v4.7 aceita dois features NGX (feature 1 → feature 18) no mesmo quadro e na mesma command list?** Estável por horas? Há crash no rebuild (alt-tab, troca de resolução, `warmup_rebuild`)? É a pergunta que decide B3.
2. **Ganho visual em jogo real, cena parada**: a segunda passada acrescenta algo perceptível sobre a primeira em 1080p com sliders a 2,00? Em quais tipos de conteúdo (jogo antigo, emulador, vídeo, jogo moderno)?
3. **Movimento**: quanto borrão e quanto tempo de assentamento após corte com MV estimados pelo Lumenite (e com `mv_hardware=1`)? Compare com o relato do Feeder para o toolkit.
4. **Flicker**: aparece em superfícies de baixo contraste sob pan lento, como o Pandorian Grey relata?
5. **Rostos**: identidade preservada ou "yassificação" dobrada?
6. **Tom**: a luminância sobe ~+5 por passada como no webcam-demo? Um ganho fixo no `DLSS5_Grade.fx` compensa sem apagar detalhe?
7. **Custo real na 4080**: ms por passada a 1080p/1440p/ultrawide, com `stall_log_ms`; fps final em A+B (Caminho A) e em `passes=2` (B3).
8. **2 passadas a 77% vs 1 a 100%**: igual custo, qual imagem é melhor?
9. **VRAM** de dois features/dois processos; comportamento em placa de 8-12 GB.
10. **Caminho A**: a camada de B em modo sobreposto sobre a janela de A fica estável, ou pisca quando o foco vai para o jogo? A janela de B em modo separado é capturável pelo OBS/LS (terceiro estágio)?
11. **Caminho A**: as duas filas WGC em série entregam em rajada (metade dos quadros descartados) como observado em `docs/latencia-nr.md:161`?
12. **Se DFC entrar em pauta**: ele fica inerte ao ver `dlss5-bridge.addon64` (nome parecido com o "DX11 bridge" que ele rejeita)? Roda no caminho D3D11 do Feeder?
13. **HDR**: com `hdrTreat` ligado, a segunda passada recebe a cor no espaço certo (formato de `SLOT_OUTPUT` vs `SLOT_COLOR`)?
14. **Alex's Toolkit** com o `renodx-dlss5.addon64.bak-4.1.5` que está na pasta: arma ou fica inerte? (Só para ter um segundo dado de qualidade sem escrever código.)

---

## Fontes

Código e documentos locais (somente leitura):
- `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\main.cpp` — linhas citadas ao longo do texto (77, 1273, 1429-1433, 1470-1493, 1496-1521, 2009-2010, 2359, 2375, 2509, 2613-2645, 2726-2734, 2862, 3157-3158, 3177, 3447-3450, 3475-3482, 4849, 5285, 5531-5543, 5680-5690, 5930-5948, 6007-6017, 6024-6042, 6062-6106, 6189-6190, 6268-6275, 6285, 6347-6367, 6410-6423, 6454-6481, 6531-6533, 6657-6698, 6742-6749, 7136).
- `...\screen_capture.h:241-274`; `...\README.md:15-30, 140-150, 179, 228-264, 275-294`; `...\docs\latencia-nr.md:1-40, 161-162, 325-327`; `...\docs\correcoes-entrada.md:550-575`; `...\docs\viabilidade-framegen-upscaling.md:384, 491-492`; `...\CREDITOS.md`; `...\reshade_bridge.h:1-60`.
- `...\ReShade.log` (pasta do projeto), linhas 46-52, 90 — versão e logs do RenoDX v4.7; `...\dlss5-feed.log` — `renodx-dlss5.addon64 v0.2026.828.517 -- v45+`; `...\ReShade.ini` — `DLSS 5 Feed 0.7.0`; `...\dlss5-feed.cfg` — `work_resolution=100`, `preset=5`.
- `...\scratchpad\dlss5-feed.cpp` (Feeder, versão com interop DFC): 6-14, 79-104, 374-397, 400-401, 416-508, 512-531, 566-571, 751-806, 869-880, 920-1031, 1034-1037, 1120-1269, 1364, 1424, 1594-1600, 4062, 4319, 4888, 5060-5313, 5163, 5355-5410, 5504-5610, 5682-5693, 5867-5868.
- `...\scratchpad\pesquisa-dlss10\feeder-README.md`, `feeder-DEPLOY-DEV.md` (§8-9), `feeder-FEEDBACK-DFC.md`, `feed_dfc.h`, `webcam-demo-README.md` (linhas 75-90, 258-352) — cópias baixadas dos repositórios abaixo.

Web:
- DLSS5-Feeder (jlrouzies-fr): https://github.com/jlrouzies-fr/DLSS5-Feeder — README, `DEPLOY-DEV.md` (§8 "Optional: Alex's Toolkit (multi-pass DLSS 5 cascade)", §9 "Deep Fried Chicken"), `FEEDBACK-DFC.md`, `src/feed_dfc.h`, releases.
- jpneagle/dlss5-webcam-demo (medições de `layers` 1/2/4 e deriva de cor): https://github.com/jpneagle/dlss5-webcam-demo
- reiluisii/1-Click-DLSS5 issue #1 (Alex's Toolkit, Extreme/Extreme Plus): https://github.com/reiluisii/1-Click-DLSS5/issues/1 ; issue #11 (DFC "stacking several layers"): https://github.com/reiluisii/1-Click-DLSS5/issues/11
- Merserk/dlss5-visual-enhancer issue #7 (toolkit → DFC, slider 1x-10x): https://github.com/Merserk/dlss5-visual-enhancer/issues/7
- Pandorian Grey, "DLSS 5 | Testing different Addon versions 4.55 / 4.6 / 4.7 / Shortfuse Multi / Deep Fried Chicken": https://www.youtube.com/watch?v=MEP4D99RNJ0
- Easy AIO Installer (Nexus 2251, "multiple Neural passes"): https://www.nexusmods.com/site/mods/2251 e https://github.com/shTNT/Easy-AIO-Installer-DLSS5-NR
- OptiScaler_DLSSNR (supersampling >100%): https://github.com/Dagherbou/OptiScaler_DLSSNR/releases ; VideoCardz: https://videocardz.com/newz/dlss-5-neural-rendering-can-now-run-through-optiscaler
- obs-dlss5-nr: https://github.com/Saganaki22/obs-dlss5-nr
- NVIDIA Developer Forums, "Does DLSS support multiple evaluations in single view?": https://forums.developer.nvidia.com/t/does-dlss-support-multiple-evaluations-in-single-view/294808
- NVIDIA Research, DLSS 5 (entradas, estado temporal): https://research.nvidia.com/labs/adlr/DLSS5/
- Notebookcheck, "Nvidia clarifies DLSS 5 infers on 2D screencaps plus motion vectors": https://www.notebookcheck.net/Nvidia-clarifies-DLSS-5-infers-on-2D-screencaps-plus-motion-vectors-and-does-not-use-existing-geometry-textures-or-lighting.1255501.0.html
- Back2Gaming, DLSS 5 Technical Preview ("one frame in, one frame out"): https://www.back2gaming.com/features/nvidia-dlss-5-technical-preview-3d-guided-neural-rendering/
- HotHardware, DLSS 5 tested (custo de uma passada): https://hothardware.com/news/nvidia-dlss-5-neural-rendering-tested
- Wccftech, DLSS 5 em 10 jogos (custo em mods): https://wccftech.com/nvidia-dlss-5-neural-rendering-in-10-modern-games-the-best-unofficial-dlss-5-on-vs-off-comparisons-so-far/
- gamegpu, RTX 4090 −33% fps em ultrawide: https://en.gamegpu.com/news/zhelezo/geforce-rtx-4090-s-dlss-5-teryaet-33-fps-i-uvelichivaet-vremya-kadra-na-50-v-razreshenii-ultrawide
- Know Your Meme, "DLSS 5 Off / DLSS 5 On" (para registro de que "DLSS 10" não é termo corrente): https://knowyourmeme.com/memes/dlss-5-off-dlss-5-on

Páginas que bloquearam a leitura automática (403/402) e ficaram só no snippet de busca: Nexus 2224/2251, Tom's Hardware, TechPowerUp p.2, gamegpu, PC Gamer (FM26).
