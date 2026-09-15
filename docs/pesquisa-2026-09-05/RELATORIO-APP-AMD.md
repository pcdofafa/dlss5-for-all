# Relatório — ramo AMD dentro do app (passo 4 do plano)

Data: 2026-09-05. Único arquivo alterado: `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\amd\main.cpp`. O `idioma.h` não foi tocado. Nada foi instalado; `montar-portable.ps1`, `amd\feeder-amd\`, a raiz do projeto e `DLSS5ForAll-Portable\` ficaram intactos. Nenhum `version.dll`, `dlssnr_on_amd.ini` ou log de mentira foi criado dentro de `amd\`: toda a simulação rodou numa cópia em `scratchpad\teste-app-amd\`.

O exe compilado está em `amd\DLSS5ForAll.exe` (build com `SKIP_ADDON=1 bash ./build-mingw.sh`, g++ do msys64, sem erros e sem nenhum aviso novo além dos `-Wmissing-field-initializers` / `-Wcast-function-type` que já existiam).

## 1. O que mudou (arquivo:linha em `amd\main.cpp`)

### Detecção do módulo AMD
- `main.cpp:747-758` — `g_moduloAmd` e `kUrlModAmd` (página de releases do mod), com o comentário que explica os dois pacotes.
- `main.cpp:7132-7143` — decidido logo depois de `g_exeDir`, antes de qualquer outra coisa (o `CarregarAtalhos` da cópia bloqueada e o `DetectarGpu` dependem disso): `version.dll` **e** `dlssnr_on_amd.ini` ao lado do exe, ou `--amd` na linha de comando.
- `main.cpp:7233-7237` — linha de log na abertura: `modulo AMD: os arquivos do mod estao ao lado do exe` ou `forcado por --amd (sem os arquivos do mod)`.
- `main.cpp:7330` — `--amd` aceito no laço geral de argumentos (só para não cair no "desconhecido").

### Compatibilidade da placa
- `main.cpp:3392-3410` — `PlacaRadeonParaMod(vendor, nome)`: vendor 0x1002 e `RX 7ddd` / `RX 9ddd` no nome (iGPU "AMD Radeon(TM) Graphics" não passa). `PlacaServeDlss5()` escolhe: no módulo AMD **só** a Radeon compatível conta; no NVIDIA continua `PlacaComTensorCore`. Uma RTX na mesma máquina, no módulo AMD, é "a outra placa".
- `main.cpp:3580-3584` e `3592`, `3619` — `DetectarGpu` usa `PlacaServeDlss5`; o rótulo no log passa a ser `Radeon compativel com o mod` / `radeon-mod=`. Como o padrão do seletor já é "a primeira com `dlss`", no módulo AMD ele prefere a Radeon sozinho, sem código a mais.
- `main.cpp:3916-3939` — passo 0 do `LerEstadoNeural`: texto do `NR_SEM_RTX` no módulo AMD é "o mod DLSS-NR-on-AMD precisa de uma Radeon RX 7000 ou RX 9000; esta máquina tem X"; o caso "tem uma Radeon compatível mas o filtro roda em outra placa" reaproveita o texto existente que manda escolher em "Placa de vídeo que roda o filtro" (o `g_gpu.outraRtx` agora é a Radeon). Depois do passo 0, `if (g_moduloAmd) return LerEstadoNeuralAmd(detalhe);`.

### Estado do neural no módulo AMD
- `main.cpp:3730-3766` — `ModAmdIni()`, `ModAmdLog()` (procura `dlssnr_on_amd.log` ao lado do exe e, como reserva, `<exe>_dlssnr_on_amd.log` ao lado e os dois nomes em `%LOCALAPPDATA%\DlssNrOnAmd\`), `NeuralNoIniAmd()` (`[DlssNrOnAmd] Enabled`; sem a chave = 1, que é o padrão do mod).
- `main.cpp:3798-3914` — `LerEstadoNeuralAmd()`:
  1. arquivos: sem `version.dll`/ini → `NR_SEM_ADDON` "rode o dlssnr_on_amd_setup.exe aqui, com o nvngx_dlssnr.dll ao lado"; sem `nvngx_dlssnr.dll`, sem `dlssnr_on_amd_weights.bin` (texto pedido: "rode o dlssnr_on_amd_setup.exe nesta pasta com o nvngx_dlssnr.dll ao lado") ou sem `amd_fidelityfx_dx12.dll` → `NR_SEM_ADDON` com o nome do arquivo;
  2. `Enabled=0` → `NR_DESLIGADO`;
  3. ReShade não carregou → `NR_SEM_ADDON` (mesmo texto do NVIDIA: sem Feeder não há FSR portador);
  4. log do mod, **só a sessão atual** (do último `loaded (standalone version.dll proxy)` em diante):
     - `UNSUPPORTED GPU ARCHITECTURE`, `invalid kernel file`, `amdhip64_7.dll` + `not found`, `No AMD GPU found` / `HIP: no usable device` → `NR_SEM_RTX` com o detalhe pt/en correspondente;
     - `engine init FAILED` / `FAILED (no dlssnr_on_amd_weights.bin)` → `NR_SEM_ADDON`;
     - `network job N done` ou `FSR active   job` mais recente que o último `FSR is not active` → `NR_ATIVO`;
     - `unsupported colour format` → `NR_DESLIGADO` com "troque o formato de cor do portador no dlss5-feed.cfg";
     - senão `NR_INICIANDO` com detalhe por etapa ("esperando o mod carregar" / "iniciando o motor (HIP)" / "esperando o portador FSR do Feeder" / "esperando o primeiro quadro passar pela rede"), com o mesmo prazo de ~25 s do caminho NVIDIA; ao estourar, `NR_DESLIGADO` com o diagnóstico (inclusive "o mod só enxergou o swapchain D3D11 e não viu o FSR do Feeder").
- `main.cpp:3638-3651` — cache do `TailArquivo` passou de 2 para 3 entradas (ReShade.log, dlss5-feed.log e agora o log do mod não se expulsam).
- `main.cpp:6605-6608` — `NeuralPeloLog()` devolve -1 no módulo AMD (não há RenoDX no processo; `g_neuralSolto` fica falso, então o "religar sozinho" não dispara à toa).

### Ligar/desligar (F6 / card DLSS 5)
- `main.cpp:3780-3796` — `AlternarNeuralAmd()`: lê `Enabled` e grava o oposto em `[DlssNrOnAmd]` do `dlssnr_on_amd.ini`; devolve `false` se o ini não existe ou a gravação falhou (aí o `AlternarTudo` desfaz a troca, como antes).
- `main.cpp:6641-6644` — `AlternarNeural()` chama `AlternarNeuralAmd()` no módulo AMD em vez de `rsbridge::ToggleNeural()` / `PokeReShade(VK_F6)`. O resto do fluxo (bypass da camada, confirmação em 2,5 s, religar antes de sair) ficou igual: como `NeuralPeloLog()` devolve -1, a confirmação compara o card (que lê o ini) com o alvo.
- `main.cpp:3436-3437` e `3491-3493` — DLSS 2x: `dlssnr_on_amd.ini` entra na lista de arquivos COPIADOS (não linkados) para a pasta da segunda camada, e ela nasce com `Enabled=1`, pela mesma regra do `NeuralUplift=1`.

### Faixa do seletor (arquivos do mod)
- `main.cpp:1789-1826` — `FaltaDlss` ganhou `std::vector<std::wstring> mod`; `kArquivosModAmd[]` = `version.dll`, `dlssnr_on_amd.ini`, `dlssnr_on_amd_weights.bin`, `nvngx_dlssnr.dll`, `amd_fidelityfx_dx12.dll`; `VerFaltaDlss()` só confere esses no módulo AMD (o RenoDX não entra na conta).
- `main.cpp:1989-2018` — `TextoFaltaDlss()` com o ramo AMD: lista exata do que falta, a linha "o app abre do mesmo jeito" e "Rode o dlssnr_on_amd_setup.exe nesta pasta com o nvngx_dlssnr.dll ao lado." (≤ 80 caracteres; sem o ReShade essa última não entra, porque a linha do ReShade já manda rodar o setup primeiro).
- `main.cpp:2073` — o log da faixa ganhou `mod-amd faltando=N`.
- `main.cpp:2461-2466` e `2713` — botão "Baixar o mod" abre `https://github.com/danielblnc/DLSS-NR-on-AMD/releases/latest`; "Abrir a pasta" já existia e continua.
- `main.cpp:2706-2712` — o STATIC da faixa passou de 78 para 84 de altura. Motivo medido: a 125% de DPI, 78 dá 97 px e quatro linhas de Segoe UI 19 px ocupam 100 px; o STATIC não desenha linha que não cabe inteira e a quarta (a de como resolver) sumia. Isso vale para o pior caso NVIDIA também (são quatro linhas). Os botões não se moveram (ficam em `kFaixaY + 90`), a faixa (`kFaixaH`) não mudou.

### Textos do painel/guia/cartazes
- `main.cpp:4499` — placar do painel: "sem Radeon" / "no Radeon" em vez de "sem RTX" no módulo AMD.
- `main.cpp:4565-4568` — "confirmando com o mod DLSS-NR-on-AMD…" no lugar de "confirmando com o add-on…".
- `main.cpp:8608-8610` e `8620-8622` — os dois cartazes de confirmação que mandavam "abra o ReShade e confira o DLSS 5" apontam, no módulo AMD, para o mod (End) e o `dlssnr_on_amd.log`.
- O guia, o cartaz de "não subiu" e os demais textos são genéricos ("nesta placa", "não está instalado") e já recebem o `neuralDetalhe` do ramo AMD, então não precisaram mudar. Nenhum texto do caminho NVIDIA foi alterado.

### Tecla do painel
- `setup-strings.txt` não tem chave de tecla no ini do mod (só `End: hide   Up/Down: select ...`, `Danielblnc's DLSS-NR on AMD v0.2.11   (End to close)` e `GetAsyncKeyState`): o End é fixo.
- `main.cpp:1497-1504` — padrão de `AT_PAINEL` = `Insert` no módulo AMD (só quando não há tecla salva em `[atalhos]`; uma tecla gravada continua mandando).
- `main.cpp:1584-1587` — a cascata de reserva do painel pula o End no módulo AMD.
- O guia, o rodapé do seletor e o painel leem do mapa de atalhos e já mostram "Insert" (conferido nos PNGs).

### Gancho de teste
- `main.cpp:3554-3566` e `3580` — variável de ambiente `DLSS5_FINGE_PLACA=1` marca toda placa de hardware como compatível, com linha de log "(TESTE)". Sem isso não dá para exercitar o ramo do log do mod numa máquina sem Radeon RX 7000/9000, porque o passo 0 (a placa) responde antes. Mesmo espírito do `DLSS5_TESTA_AVISO` que já existia.
- `main.cpp:7369` — o `--panel-shot` agora loga `neural (shot): estado=N (detalhe)`, para conferir o estado sem abrir o PNG.

## 2. Como testei

Scripts e imagens em `scratchpad\app-amd\` (`cenarios.ps1`, `cenarios-lib.ps1`, `teste-real.ps1`, `foto-seletor.ps1`, `texto-faixa.ps1`) e `scratchpad\shots\`.

### 2.1 Caminho NVIDIA (a partir de `amd\`, sem os arquivos do mod — nada deve mudar)
- `--panel-shot` e `--guia-shot` → `shots\nv-painel.png` e `shots\nv-guia.png`: painel com "End" no atalho do painel, NEURAL "subindo", textos iguais aos de antes.
- `DLSS5ForAll.exe --source monitor --monitor 0 --outra-instancia` por 25 s e `Stop-Process`. No `amd\dlss5forall.log`: nenhuma linha "modulo AMD", `gpu: usando [0] NVIDIA GeForce RTX 4080 vendor=0x10DE rtx=1`, `atalho painel = End`, `D3D11 ok, swapchain waitable 3440x1440 FLIP_DISCARD`, `neural na abertura: estado=1 (ligado, esperando o primeiro frame passar pela rede)`. No `amd\dlss5-feed.log`: `feature ready: 3440x1440 DLAA ...` e `frame 1/2/3 delivered`.

### 2.2 Módulo AMD simulado (cópia em `scratchpad\teste-app-amd\`, flag `--amd`, `version.dll` = cópia do de `System32`, ini/pesos/`amd_fidelityfx_dx12.dll` de mentira, log colado do Apêndice B)
Todos com `--amd --panel-shot`, lendo a linha `neural (shot)` (estados: 0 DESLIGADO, 1 INICIANDO, 2 ATIVO, 3 SEM_ADDON, 4 SEM_RTX):

| Cenário | Esperado | Obtido |
|---|---|---|
| sem arquivos do mod, sem fingir placa (RTX 4080 + iGPU AMD) | 4 | 4 — "o mod DLSS-NR-on-AMD precisa de uma Radeon RX 7000 ou RX 9000; esta máquina tem NVIDIA GeForce RTX 4080" |
| sem arquivos do mod, `DLSS5_FINGE_PLACA=1` | 3 | 3 — "não está nesta pasta — rode o dlssnr_on_amd_setup.exe aqui..." |
| arquivos ok, sem log | 1 | 1 — "esperando o mod DLSS-NR-on-AMD carregar" |
| `engine init ok`, sem FSR | 1 | 1 — "esperando o portador FSR do Feeder" |
| `FSR is not active` | 1 | 1 — idem |
| `first ffxDispatch`, sem job | 1 | 1 — "esperando o primeiro quadro passar pela rede" |
| `network job 2 done` | 2 | 2 — "a rede neural está refazendo a imagem (mod DLSS-NR-on-AMD)" (`shots\amd-painel-ativo.png`: NEURAL "ATIVO", atalho "Insert") |
| `unsupported colour format 67` | 0 | 0 — com o motivo |
| `UNSUPPORTED GPU ARCHITECTURE gfx1200` | 4 | 4 (`shots\amd-painel-semplaca.png`: placar "sem Radeon") |
| `invalid kernel file` | 4 | 4 |
| `amdhip64_7.dll was not found` | 4 | 4 |
| `engine init FAILED (no dlssnr_on_amd_weights.bin)` | 3 | 3 |
| `Enabled=0` | 0 | 0 |
| sem `dlssnr_on_amd_weights.bin` | 3 | 3 — "rode o dlssnr_on_amd_setup.exe nesta pasta com o nvngx_dlssnr.dll ao lado" |
| job só na sessão anterior do log | 1 | 1 (a sessão antiga não conta) |

- `--amd --guia-shot` → `shots\amd-guia.png`: primeira tecla "Insert".
- Seletor no módulo AMD sem os arquivos do mod (`foto-seletor.ps1`, PrintWindow) → `shots\amd-seletor-faixa.png`: faixa lista `version.dll, dlssnr_on_amd.ini, dlssnr_on_amd_weights.bin e amd_fidelityfx_dx12.dll`, botão "Baixar o mod", rodapé "Insert painel · F6 liga/desliga", placa "NVIDIA GeForce RTX 4080 — sem DLSS 5". Foi essa foto que mostrou a quarta linha sumindo com 78 de altura; `texto-faixa.ps1` (WM_GETTEXT no STATIC) confirmou que o texto estava inteiro no controle, e com 84 a linha aparece.
- Módulo AMD de verdade na pasta de teste (`--amd --source monitor --monitor 0 --outra-instancia`, `DLSS5_FINGE_PLACA=1`, log em ATIVO), F6 sintetizado com `WScript.Shell.SendKeys` duas vezes: `Enabled=1` → `0` → `1` no `dlssnr_on_amd.ini`; log: `neural (amd): Enabled=1 -> 0 no dlssnr_on_amd.ini (gravado)`, `DLSS 5 DESABILITADO (estava: neural=1 camada=1)`, `neural: confirmacao alvo=0 log=-1 card=0`, depois `Enabled=0 -> 1`, `DLSS 5 HABILITADO`, `confirmacao alvo=1 log=-1 card=2`. `atalho painel = Insert`, `atalho tudo = F6`.

Ao fim, nenhum processo `DLSS5ForAll` ficou aberto e `amd\` não tem `version.dll`, `dlssnr_on_amd.*` nem `amd_fidelityfx_dx12.dll`.

## 3. O que ficou como inferência

- **[INFERÊNCIA] O mod relê `Enabled` em tempo real.** As strings do binário provam releitura ao vivo de `Inline`/`Interop` (`ini changed Inline=%d Interop=%d; re-creating staging`, `staging re-created (settings change)`) e o mod grava o próprio ini pelo overlay (`cannot write %s (settings will not persist)`). Que o `Enabled` siga o mesmo caminho é dedução; se não seguir, o F6 passa a valer só na próxima abertura e o card fica em INICIANDO até o prazo. Só a Radeon confirma.
- **[INFERÊNCIA] Nome e pasta do log do mod.** O caso principal (`dlssnr_on_amd.log` ao lado do exe) é o do plano. As strings `_dlssnr_on_amd.log` e `DlssNrOnAmd\` sugerem `<exe>_dlssnr_on_amd.log` e/ou `%LOCALAPPDATA%\DlssNrOnAmd\`; `ModAmdLog()` tenta os quatro caminhos e fica com o primeiro que existir.
- **[INFERÊNCIA] `FSR is not active` e `FSR active   job` são linhas de log.** Ambas existem no binário, mas podem ser só linhas do overlay; por isso o `NR_ATIVO` também aceita `network job %d done`, que é linha de log confirmada (issue 49).
- **[INFERÊNCIA] `amd_fidelityfx_dx12.dll` ao lado do exe.** A checagem cobra o arquivo na pasta do exe, como pedido. Se o agente do Feeder AMD carregar a DLL de outra pasta (`feeder-amd\ffx-bin\`, por exemplo), basta ajustar o caminho em `kArquivosModAmd`/`LerEstadoNeuralAmd`.
- **[INFERÊNCIA] Texto do `unsupported colour format`.** Aponta para "o formato de cor do portador no dlss5-feed.cfg" sem citar o nome da chave, porque ela ainda está sendo definida no Feeder AMD (o plano sugere `amd_color_fmt`).
- A checagem de placa aceita qualquer `RX 7ddd`/`RX 9ddd` (inclui RX 9060 XT / gfx1200, que o mod ainda não suporta): a palavra final é do log do mod (`UNSUPPORTED GPU ARCHITECTURE` → `NR_SEM_RTX` com o texto certo), e isso foi testado.

---

## 4. Tarefas extras (selo "DLSS 2x" e as correções do DLSS 2x)

Feitas depois do ramo AMD, no mesmo `amd\main.cpp`. **Conflito de edição:** enquanto eu implementava, alguém mais gravou no arquivo um `namespace Selo2x` (GDI+, `main.cpp:6495`), o bloco do perfil "cinematic" logo após o `DetectarGpu` (`main.cpp:7401`) e o novo critério de "ATIVA" da segunda camada. Para não duplicar, apaguei as minhas versões dessas três peças e mantive as que já estavam; só acrescentei ao bloco de estado o corte na sessão atual do log da segunda camada (abaixo). O exe compila limpo com tudo junto.

### O que ficou meu
- `main.cpp:2787-2823` — `GravarChaveCfg(caminho, chave, valor)`: o miolo do antigo `GravarWorkResolution` virou genérico (troca só a linha da chave, casa a chave no começo da linha, preserva o resto byte a byte, acrescenta se não existir). `GravarWorkResolution` passou a chamá-lo.
- `main.cpp:3504-3546` — (A) em `Camada2Iniciar`, depois do `NeuralUplift=1`: `create_delay=15` e `warmup_rebuild=30` no `dlss5-feed.cfg` da segunda camada (15 e não 5: o README do Feeder avisa que armar cedo demais pode travar), e o bloco "suave" no `ReShade.ini` dela (`NRIntensity=1.2`, `NRLocalStructure=2`, `NRLocalTone=0.6`, `NRGlobalTone=0.6`, `NRSkinStructure=0.8`, `NRStyle=1`, `NRPreset=0`) em todo perfil menos "igual". O comentário documenta os três valores de `[ultima] dlss2x_perfil`: vazio/"suave" (padrão: primeira intocada, segunda suave/Natural), "igual" (cópia fiel, tudo 2.0) e "cinematic" (segunda suave E primeira em `NRStyle=2` na próxima abertura, porque o RenoDX só lê na carga; Cinematic embaixo + Natural em cima foi escolha do usuário).
- `main.cpp:3702` — cache do `TailArquivo` com 6 entradas (os três logs da segunda camada entram na conta, uma vez por segundo).
- `main.cpp:8476-8500` — toasts do `CMD_CAMADA2`: no padrão, "DLSS 2x LIGADO/DESLIGADO" como antes; só no perfil "cinematic" entra a linha "a camada de baixo passa para Cinematic na próxima abertura" (se o `NRStyle` em vigor ainda não for 2) e, ao desligar, "volta ao estilo anterior na próxima abertura" (se `dlss2x_estilo_original` existir).
- `main.cpp:8697-8704` — (B) complemento: só a sessão atual do `dlss5forall.log` da segunda camada conta (ela abre o log em "a" por ser `--outra-instancia`; um "TETO DE 30 s" ou "aviso: neural nao esta ativo" de uma ligada anterior marcaria "não subiu"/"sem neural" na ligada nova). O critério em si (" delivered (" no `dlss5-feed.log` dela E "evaluation succeeded" no `ReShade.log` dela; "camada liberada" sozinho não conta; teto/40 s = não subiu) já estava gravado pela outra edição.
- `main.cpp:9062-9070` — (C) a segunda instância apresenta a cada 33 ms enquanto aquece (só ela, só no aquecimento), para os presents do `create_delay` passarem rápido mesmo com a fonte parada.

### Teste do DLSS 2x (`scratchpad\app-amd\teste-2x.ps1`, cópia de teste, Bloco de Notas como alvo, `dlss2x=1` no ini, 45 s)

| Marco | Hora | Desde a abertura da 2ª |
|---|---|---|
| segunda camada aberta (pid) | 02:45:40.129 | 0 s |
| sessão D3D12 do Feeder dela | 02:45:43.339 | 3,2 s |
| `holding the feature (re)build for 15 frames` | 02:45:43.339 | 3,2 s |
| `building` / `feature ready` | 02:45:44.308 / .339 | 4,2 s |
| `frame 1 delivered` | 02:45:44.766 | **4,6 s** |
| `inline feature 18 evaluation succeeded (count=1)` no ReShade.log dela | 02:45:44.756 | 4,6 s |
| `aquecimento: camada liberada apos 3906 ms (a cadeia entregou)` (2ª) | 02:45:44.778 | 4,6 s |
| **`dlss 2x: segunda camada ATIVA`** no log da primeira | 02:45:45.766 | 5,6 s |
| `aquecimento: camada liberada apos 9843 ms` (1ª) e `overlay colada` | 02:45:49.9 | 9,8 s |
| `dlss 2x: selo na tela` | 02:45:50.477 | 10,3 s (espera a camada da 1ª ficar à vista) |

Os 15 presents do `create_delay` passaram em ~1 s (antes eram 60 presents ao ritmo da primeira). O `camada2\dlss5-feed.cfg` ficou com `create_delay=15` e `warmup_rebuild=30`; o `camada2\ReShade.ini` com o bloco suave (`style=1` confirmado na linha "active settings" do `ReShade.log` dela); o `ReShade.ini` da primeira ficou com `NRStyle=0` (perfil padrão não mexe). O RenoDX da segunda avaliou 60 quadros até 02:45:53. Ao fim, a segunda encerrou sozinha quando a primeira fechou ("a primeira camada fechou - encerrando"). O `--panel-shot` a partir de `amd\` continuou ok.

O selo não aparece em foto: ele tem `WDA_EXCLUDEFROMCAPTURE` (de propósito, para não entrar no vídeo nem na captura da segunda camada), então o `PrintWindow` também não o pega; a prova é a linha "dlss 2x: selo na tela" e o fato de ele só sair depois de a camada da primeira colar.

Não testado: o perfil "cinematic" de ponta a ponta (troca do `NRStyle` da primeira na próxima abertura) — o bloco que faz isso não é meu; o perfil "igual".

### Reconciliação final (pedido do team-lead) e segunda rodada do teste

- Ficou UM bloco "cinematic" (o que está logo após `DetectarGpu()`, `main.cpp:~7401`): só age quando `dlss2x=1` E `dlss2x_perfil=="cinematic"`; caso contrário restaura `NRStyle` se `dlss2x_estilo_original` existir. O meu duplicado (após `DefinirFpsNaTela`) foi apagado. Um só `namespace Selo2x` (`Tick` ao lado de `Efeitos::Tick()`, `Shutdown` antes de `Hud::Close()`); o meu selo em GDI foi apagado. Um só critério de "ATIVA" (o do bloco "ATIVA so com a REDE rodando de verdade"), com o corte na sessão atual acrescentado.
- (C) completo: além do `gap` de 33 ms, o `MsgWaitForMultipleObjects` do topo do laço (`main.cpp:~8221`) espera 33 ms em vez de 100 quando `g_camada2 && aquecendo` — sem isso o laço só acordava a cada 100 ms com a fonte parada.
- Segunda rodada (`teste-2x.ps1`, exe final): segunda camada aberta 02:48:38.175; `holding ... 15 frames` 02:48:41.189; `building` 02:48:41.678 (os 15 presents passaram em 0,49 s, ~30 Hz); `frame 1 delivered` 02:48:42.147 (**4,0 s** após abrir); `evaluation succeeded (count=1)` 02:48:42.138; `dlss 2x: segunda camada ATIVA` no log da primeira 02:48:42.979 (4,8 s); camada da primeira colada 02:48:47.083; `dlss 2x: selo na tela` 02:48:47.633 (9,5 s). `--panel-shot` a partir de `amd\` ok. cfg e ReShade.ini da segunda camada como na primeira rodada; `NRStyle=0` na primeira.
