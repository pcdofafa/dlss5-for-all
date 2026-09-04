# Revisão crítica — DLSS5 For All

Revisão adversarial do código como ele vai sair para o público. Base: leitura integral de
`main.cpp` (4817 linhas), `screen_capture.h`, `hdr_control.h`, `splash_ofertas.h`, `qr.h`,
`reshade_bridge.h`, `addon/dlss5_bridge_addon.cpp`, `build-mingw.sh`, `setup-dlss5.ps1`,
`README.md`, `ReShade.ini`/`ofertas.ini`/`dlss5forall.ini` distribuídos, mais uma
compilação limpa com `-Wall -Wextra`.

**O que NÃO foi feito:** o app não foi executado (o PC está em uso). Nada aqui vem de
observação em runtime — é leitura de código, aritmética sobre os identificadores e o
compilador. Onde não consegui concluir, está dito explicitamente na §5.

Achados já documentados como limitação conhecida em `docs/painel.md` foram omitidos, com
uma exceção: o §4.6 descreve um comportamento que o código **não tem** (ver A2).

---

## 0. Estado (rodada 1 fechada em 03/09/2026)

Os textos das seções 1 a 5 ficaram **como foram escritos na revisão** — eles descrevem o
defeito no código daquele momento, não o código de hoje. Esta tabela é o que vale para
saber onde cada coisa está. Tudo marcado *corrigido* foi reconferido por leitura do código
e com o `main.cpp` compilando em `-Wall -Wextra`; nada foi aceito por descrição.

| # | estado | onde ficou |
|---|---|---|
| C1 colisão de IDs | **corrigido** | faixas por milhar + 9 `static_assert` (`main.cpp:2088-2116`) |
| C2 Esc sem retomar atalhos | **corrigido** | `CancelarCapturaAtalho()`, chamado no Esc e no topo do `Hud::Close` |
| C3 `ShellExecuteW` com URL do ini | **corrigido** | `UrlEhWeb`/`UrlSegura` nos 4 pontos de entrada (`splash_ofertas.h`) |
| A1 faixa da escala engolindo `CMD_REABRIR` | **sem objeto** | a renumeração do C1 já resolvia; a Resolução interna saiu depois |
| A2 queda do WGC não detectada | **corrigido** | `add_Closed` + `WgcClosedHandler`; `Start()` com recomeço limpo |
| A3 degrau 75% inaplicável | **sem objeto** | Resolução interna removida por decisão do usuário |
| A4 `escala_pendente` no fim limpo | **sem objeto** | idem |
| A5 "Reabrir" com nome de exe fixo | **corrigido** | `GetModuleFileNameW` + retorno do `ShellExecuteW` |
| A6 `Capture::Stop` use-after-free | **corrigido** | thread presa: reader/src/evt abandonados de propósito, com log |
| A7 setup/README sem a ponte e os shaders | **pendente** | é do team-lead |
| A8 `_snwprintf` sem terminador | **corrigido** | 4 pontos, `main.cpp` e `screen_capture.h` |
| **CRÍTICO da retomada** (nasceu ao corrigir o A2) | **corrigido** | `Stop()` → `extSrv`/`waitEvt` nulos; reatribuídos nos dois desfechos |
| M3 `extSrv` pendurado | **corrigido** | atribuição fora do `if (got)` |
| M4 `Start` vazando nos erros | **corrigido** | guard `Desfaz` cobrindo todo `return false` |
| M5 `Stop` fechando `evt` com `Invoke` em voo | **corrigido** | `Desarmar()` + `inFlight` nos dois handlers |
| M7 F11 quebrando a camada sobreposta | **corrigido** | marca `emTelaCheia` |
| M19 balão da bandeja com tecla fixa | **corrigido** | vem de `TextoAtalho(...)`, com terminadores de `wcsncpy` |
| M11 tail de 256 KB no watchdog | **sem objeto** | `CadeiaMorreu`/`TamanhoFeedLog` saíram com o vigia da escala |
| M15 escala do ini sem revalidar | **sem objeto** | idem |
| M1, M2, M6, M8, M9, M10, M14, M16, M17, M18 | **pendentes** | rodada 2 de polimento |
| M12 `ReShade.ini` com o caminho do autor · M13 README | **pendentes** | com o team-lead |
| §4 BAIXOS | **pendentes**, menos os warnings de compilação (destrutor virtual e indentação enganosa), **corrigidos** | |

### Novos, encontrados durante a reconferência (para a rodada 2)

1. **Propaganda no caminho de linha de comando** — `OfertasNaAbertura` (`main.cpp:4100`)
   roda antes do `if (!fromCli)`, então quem abre por atalho com parâmetros
   (`--window x --overlay`) fecha a janela à mão a cada abertura antes de o filtro subir.
   Com `mostrar=sempre` no `ofertas.ini` do zip, é toda vez. Uma linha no ini resolve.
2. **`Alt+Esc` inerte durante a splash** — as hotkeys já estão registradas quando ela abre,
   e o laço de mensagens dela descarta `WM_HOTKEY` (hwnd nulo). Sem dano; registrado.
3. **Captura caída com as 10 tentativas gastas** — não recria e não avisa na tela; a pessoa
   vê imagem congelada e só o log explica. Cabe um cartaz depois da última tentativa.
4. **`WriteFile` sem verificação em `CorrigirBomDoIni`** (`main.cpp:~1865`) — numa escrita
   parcial o ini fica truncado e o log ainda diz "reescrito sem BOM". Uma linha. Se quiser
   atomicidade de verdade: gravar em `.tmp` e `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`.

### Uma correção de rumo sobre a §4.6 do `docs/painel.md`

O A2 estava certo como diagnóstico — o `add_Closed` não existia quando esta revisão foi
escrita, e a condição de recriação era mesmo inalcançável. O que a medição do agente de
captura mostrou depois é outra coisa, e é boa: **redimensionar a janela do alvo não derruba
a sessão do WGC** (o pool é recriado e a captura segue). Ou seja, o cenário que a §4.6
descrevia não foi reproduzido com esse alvo — o que não prova que ele não exista em tela
cheia exclusiva ou com o jogo recriando o swapchain. A §4.6 foi reescrita nesses termos.

---

## 1. CRÍTICOS

### C1. Clicar em "intensidade 50%" da LUT sequestra a configuração de atalhos e desliga todas as hotkeys

`main.cpp:2060` (`CMD_LUTSTR_BASE = 350`), `main.cpp:2051` (`CMD_ATALHO_BASE = 400`),
`main.cpp:3079` (`Activate`), `main.cpp:4374` (despacho).

Os chips de intensidade mandam `CMD_LUTSTR_BASE + porcentagem`. Para 50% isso dá
**exatamente 400**, que é `CMD_ATALHO_BASE + AT_PAINEL`. O bloco de atalhos (linha 4374) é
avaliado **antes** do bloco de intensidade (linha 4533) e zera `cmd`. Resultado: o clique
não muda a intensidade nenhuma, o painel entra em modo "aperte…" na linha *Abrir o painel*
e `SuspenderAtalhos()` desregistra `End`, `F6` e `Alt+Esc`.

Reprodução: painel → escolher qualquer LUT (para os chips de intensidade aparecerem) →
clicar em **50%**. A linha "Abrir o painel" da seção ATALHOS passa a mostrar "aperte…".
Os outros degraus escapam por sorte: 25%→375, 75%→425, 100%→450.

**Correção:** separar os espaços de comando (por exemplo `CMD_LUTSTR_BASE = 1000`,
`CMD_ATALHO_BASE = 2000`, `CMD_ESCALA_BASE = 3000`) em vez de blocos de 100 encostados uns
nos outros — ver também A1, que é o mesmo defeito em outro par.

### C2. Cancelar a captura de atalho com Esc deixa o app sem nenhuma tecla, para sempre

`main.cpp:3132-3137` (`Hud::Proc`, `WM_KEYDOWN` em modo de captura) e `main.cpp:4418`
(único `RetomarAtalhos()` do caminho).

`SuspenderAtalhos()` é chamado no loop principal ao entrar em captura (4377), e
`RetomarAtalhos()` só existe no ramo que processa a tecla capturada (`CMD_ATALHO_SET_BASE`,
4418). Os dois caminhos de desistência não retomam nada:

- **Esc** (3133-3136): zera `capturandoAtalho`, repinta e volta — sem `RetomarAtalhos()`;
- **clicar fora / o painel perder o foco** (`WM_ACTIVATE` → `Close()`, 3150-3152): idem, e
  `capturandoAtalho` nem sequer é zerado, então o painel reabre já em modo de captura.

Depois disso `End` não abre mais o painel, `F6` não liga/desliga nada e `Alt+Esc` não sai.
No modo janela separada não há ícone na bandeja (ele só é criado com `C.overlay`), então
sobra fechar pelo X. Combinado com C1, chega-se lá sem nunca ter pedido para mudar tecla
nenhuma.

Reprodução: painel → ATALHOS → "mudar" em qualquer linha → Esc → fechar o painel → apertar
End.

**Correção:** chamar `RetomarAtalhos()` (e zerar `capturandoAtalho`) em todo caminho que sai
do modo de captura — Esc, `Hud::Close()` e qualquer outro comando que chegue com
`capturandoAtalho >= 0`.

### C3. `ShellExecuteW` com URL vinda do `ofertas.ini`, sem validar o esquema

`splash_ofertas.h:553-555` (`OpenLink`), chamada em 642 e 649; valor lido em 162
(`base.url = v`, sem nenhuma verificação).

`ofertas.ini` fica ao lado do exe, é editável à mão e viaja no zip. `ShellExecuteW(..., L"open", url, ...)`
não exige um esquema web: `url=C:\Windows\System32\cmd.exe`, `url=\\servidor\share\x.exe`
ou `url=file:///...` executam o alvo quando a pessoa clica no botão QUERO AS OFERTAS, no QR
ou no endereço — e o botão é a ação que o app inteiro convida a fazer. Como o app é
distribuído de graça e o ini é texto plano ao lado do exe, quem reempacotar o zip tem
execução de código com um clique, sem tocar no binário.

**Correção:** em `OpenLink`, recusar tudo que não comece com `https://` (ou `http://`),
comparação case-insensitive, antes de chamar `ShellExecuteW` — e aplicar a mesma checagem
ao carregar `base.url` no `LoadConfig`, para o QR também não codificar lixo.

---

## 2. ALTOS

### A1. O botão "Reabrir o app na resolução nova" não reabre — grava `escala=201` no ini

`main.cpp:4355` (`if (cmd >= CMD_ESCALA_BASE && cmd < CMD_ESCALA_BASE + 300)`),
`main.cpp:2055-2056` (`CMD_ESCALA_OK = 700`, `CMD_REABRIR = 701`), `main.cpp:4422`.

`CMD_ESCALA_BASE` é 500 e a faixa vai até 800, então **701 cai dentro dela**. O clique em
"Reabrir" é interpretado como "escolher resolução interna de 201%": passa no `ScaleCabe`,
grava `escala=201` no `dlss5forall.ini`, mostra o cartaz "RESOLUCAO INTERNA: 201%" e faz
`cmd = 0` — o `if (cmd == CMD_REABRIR)` da linha 4422 nunca é alcançado. Na abertura
seguinte, `201` não passa no filtro `pct > 100 && pct <= 200` (4134) e a escala volta a
100% em silêncio. `CMD_ESCALA_OK` (700) tem o mesmo problema (hoje inofensivo porque
nenhum item `E_ESCALA_OK` é criado no `Render`).

Reprodução: abrir sobre um jogo, redimensionar a janela do jogo (o item "Reabrir" aparece),
clicar nele.

**Correção:** limitar a faixa ao que é de fato escala (`cmd <= CMD_ESCALA_BASE + 200`) e
mover `CMD_ESCALA_OK`/`CMD_REABRIR` para fora dela.

### A2. A queda da sessão do WGC não é detectada; o código que recria a captura é inalcançável

`screen_capture.h:356` (`closed`), `:714` (único ponto que o liga), `:718` (`Alive`),
`:720-734` (`Stop`), `main.cpp:4670-4688` (retomada).

`closed` só vira `true` quando `!IsWindow(targetWin)`. Não há `add_Closed` no
`GraphicsCaptureItem` (conferido: o header só usa `IClosable` para devolver frames), então
quando o WGC encerra a sessão com a janela viva — o caso que `docs/painel.md` §4.6 descreve,
o jogo recriando o swapchain — `Alive()` continua `true` e nada acontece: o app fica exibindo
o último frame, sem imagem nova e sem aviso.

E a condição da retomada é contraditória: exige `!scap.Alive()` **e** `IsWindow(C.win)`.
Como `Alive()` só é falso quando a janela morreu, as duas nunca são verdadeiras juntas —
o bloco de recriação (4671-4684) é código morto. Se a detecção for corrigida, ele ainda não
funciona, por dois motivos:

- `Start()` não zera `closed` (nem `Stop()`), então a captura recriada nasce "morta" e o
  laço queima as 10 tentativas;
- se `Start()` falhar, `waitEvt` (main.cpp:4322/4679) continua apontando para o handle que
  `Stop()` já fechou → `MsgWaitForMultipleObjects` falha na hora e o loop principal vira
  busy-wait a 100% de um núcleo.

**Correção:** registrar `add_Closed` no `IGraphicsCaptureItem` (ou detectar ausência de
frames por N segundos) para ligar `closed`; zerar `closed`/`cacheOk`/`poolW/H` no início de
`Start()`; e, quando `Start()` falhar, apontar `waitEvt` para um evento válido antes de
voltar ao loop.

### A3. O degrau de 75% do painel nunca é aplicado

`main.cpp:2066` (`kEscalas`), `main.cpp:4133-4141` (leitura do ini), `main.cpp:4143` (clamp).

O painel oferece 75%, escreve `escala=75` no ini e promete "passa a valer quando você
reabrir o app". Na abertura, só `pct > 100 && pct <= 200` vira escala; e mesmo por linha de
comando o clamp `R.scale = (argEscala < 1.0f) ? 1.0f : ...` força 1,0. Ou seja: o usuário
escolhe 75%, reabre e continua em 100%, com o painel mostrando 100% escolhido — sem
nenhuma mensagem. O caminho `SampleArea` do shader (`UpFilter == 3`, main.cpp:539-560),
escrito justamente para escala < 1, é código morto.

**Correção:** aceitar `pct >= 50 && pct <= 200` na leitura do ini e baixar o piso do clamp
para 0,5 — ou remover o degrau de 75% do painel enquanto não for aplicado.

### A4. Fechar o app rápido reverte a resolução interna e acusa um travamento que não houve

`main.cpp:4137` (grava `escala_pendente`), `main.cpp:4176-4178` (prazos),
`main.cpp:4795-4817` (encerramento — não limpa a marca).

A marca `escala_pendente` é gravada antes de criar o dispositivo e só sai quando o teste de
sobrevivência aprova: 20 s **depois** de a cadeia subir, ou 90 s de teto absoluto se ela
nunca subir. O encerramento normal não limpa a marca. Consequência: quem liga 150%, reabre,
olha e fecha em menos de ~30 s (ou menos de 90 s numa máquina sem o RenoDX instalado, onde
`NR_ATIVO` nunca acontece) tem a escolha revertida para 100% (`IniSet(L"escala", L"100")`,
4130) e recebe o cartaz "a sessão anterior não terminou bem nessa escala" — um diagnóstico
falso, sobre um fechamento voluntário.

**Correção:** limpar `escala_pendente` no encerramento normal (junto do `Log("Encerrando…")`),
já que sair pelo X/painel/atalho é prova de que o app estava vivo.

### A5. "Reabrir o app" depende do nome do arquivo e não confere se deu certo

`main.cpp:4426-4429`.

`ShellExecuteW(..., g_exeDir + L"DLSS5ForAll.exe", ...)` com nome fixo, seguido de
`g_quit = true` sem olhar o retorno. Se o usuário renomeou o exe (ou o pacote tiver outro
nome), o app simplesmente fecha e não volta — do ponto de vista de quem clicou, "o botão
fecha o programa".

**Correção:** usar `GetModuleFileNameW(nullptr, ...)` como caminho e só sair se o retorno de
`ShellExecuteW` for `> 32`, avisando por cartaz em caso contrário.

### A6. `Capture::Stop` pode liberar o `IMFSourceReader` debaixo da thread de captura

`main.cpp:362-372`.

Se a thread não terminar em 2 s, o código faz `th.detach()` e logo em seguida
`SafeRelease(reader); SafeRelease(src);`. A thread destacada continua no
`reader->ReadSample(...)` (main.cpp:307) sobre um objeto já liberado. É exatamente o
cenário de placa de captura que trava o `ReadSample` — hardware barato, cabo HDMI puxado,
driver ruim — e o sintoma na máquina do usuário é "crashou ao fechar".

**Correção:** não destacar; esperar o `join` indefinidamente após o `Shutdown()` do
`IMFMediaSource` (que é o que destrava o `ReadSample`), ou manter reader/source vivos numa
estrutura com contagem de referência que a thread solte por último.

### A7. Numa instalação feita pelo `setup-dlss5.ps1`, LUT e nitidez nascem mortas

`setup-dlss5.ps1:221-222` (`Techniques` = só `Lumenite_Kernel` + `DLSS5_Feed`), e o script
não copia/valida `dlss5-bridge.addon64`, `DLSS5_Grade.fx`, `DLSS5_Sharpen.fx` nem a textura
`reshade-shaders\Textures\dlss5_grade_luts.png` (3,6 MB). O `README.md:106-121` (Instalação)
e a seção "Sobre os arquivos" (`README.md:231-236`) também não citam nenhum desses arquivos
nem o `ofertas.ini`.

Quem seguir o README ao pé da letra recebe um app em que a seção COR · LUT do painel diz
"indisponível — o add-on da ponte não está carregado" e a nitidez responde "precisa do
DLSS5_Sharpen no preset". A degradação é limpa e bem comunicada — mas metade dos controles
do painel nunca funciona, e o usuário não tem como saber que faltou arquivo no pacote.

**Correção:** acrescentar ao script a cópia do `dlss5-bridge.addon64`, dos dois `.fx` e da
textura, incluir `DLSS5_Grade@DLSS5_Grade.fx` e `DLSS5_Sharpen@DLSS5_Sharpen.fx` no preset
padrão, e listar no README o conteúdo mínimo do zip.

### A8. `_snwprintf` sem terminador com título de janela longo

`screen_capture.h:235-237` (`wchar_t buf[512]`, rótulo do seletor) e `main.cpp:4787-4791`
(`wchar_t t[512]`, título da nossa janela).

No CRT do Windows, `_snwprintf` **não** termina a string quando trunca. O rótulo é montado
com o título da janela alvo, que não tem limite: um navegador, um player ou um jogo com
título longo (≈480+ caracteres, e `GetWindowTextLength` não impõe teto) faz `wt.label = buf`
e `SetWindowTextW(hwnd, t)` lerem para fora do buffer da pilha. O sintoma vai de lixo no
seletor a crash na enumeração — e a enumeração roda na abertura, antes de qualquer coisa.

**Correção:** terminar à força (`buf[511] = 0;`) nos dois pontos, ou trocar por
`swprintf_s`/`StringCchPrintfW`; e truncar o título a ~200 caracteres antes de formatar.

---

## 3. MÉDIOS

| # | onde | o que quebra | correção |
|---|---|---|---|
| M1 | `main.cpp:99-104` (`Fatal`) | Sai por `ExitProcess` sem `TrayRemove` nem `Stop()` da captura. Um `Fatal` dentro do loop (ex.: 4645, falha ao redimensionar a textura da câmera) deixa o ícone fantasma na bandeja até a pessoa passar o mouse por cima. | Chamar `TrayRemove` no `Fatal` (ponteiro global para a janela da bandeja). |
| M2 | `screen_capture.h:295-304` vs `:102-135` | O `Invoke` do WGC roda em thread do sistema e escreve em `g_lat.gap`, `g_lat.lastArr`, `g_lat.nArrive` sem sincronização, enquanto o loop principal faz `Reset()`/`Dump()` nos mesmos campos. Não corrompe memória (o índice é checado), mas é *data race* formal e pode congelar a série de `gap` no relatório LAT. | Tornar `nArrive`/`lastArr` atômicos e proteger `gap` com um `SRWLOCK`, ou acumular no handler em variáveis próprias. |
| M3 | `main.cpp:4649-4665` + `screen_capture.h:432-445` | `EnsureTex` libera `srv`/`tex` **antes** de recriar; se a criação falhar (VRAM cheia numa troca de tamanho), `CopyFrame` devolve `false`, `got` fica `false` e `R.extSrv` continua apontando para o SRV já liberado — o `DrawAndPresent` seguinte usa ponteiro morto. | Atualizar `R.extSrv = scap.srv` a cada iteração, não só quando `got`. |
| M4 | `screen_capture.h:493-609` | Todos os `return false` de `Start()` largam o que já foi criado (`rtDev`, `item`, `pool`, `handler`, `ictx`, `evt`). Hoje o chamador responde com `Fatal` e o processo morre; no dia em que A2 for corrigido, cada tentativa de retomada vaza um pool e um handler. | Um `guard` que chame `Stop()` em qualquer saída por erro. |
| M5 | `screen_capture.h:720-734` | `Stop()` fecha `evt` e libera o handler sem garantir que não há um `Invoke` em voo; um `SetEvent` atrasado escreve num handle fechado (e potencialmente reciclado). | Zerar `handler->evt`/`counter`/`arriveQpc` antes do `CloseHandle`, e só então soltar o handler. |
| M6 | `main.cpp:1025` (`sc->Present`) | Nenhum tratamento de `DXGI_ERROR_DEVICE_REMOVED`/`DEVICE_RESET`. Num TDR ou numa atualização de driver com o app aberto (comum em público), o app segue apresentando num device morto: janela preta, loop a 100%, sem mensagem. | Checar o `HRESULT` do `Present` e, em device removed, avisar por cartaz e encerrar limpo (restaurando o HDR). |
| M7 | `main.cpp:1094-1109` + `1120-1129` | No modo sobreposto a janela é `WS_POPUP`; F11/Alt+Enter caem no ramo "sair da tela cheia" e aplicam `SetWindowPlacement` com um `WINDOWPLACEMENT` zerado, além de somar `WS_OVERLAPPEDWINDOW`: a camada ganha borda e barra de título por cima do jogo. Só acontece em modo interativo (fora do click-through a janela recebe teclado). | Ignorar F11/Alt+Enter quando a janela não tem `WS_OVERLAPPEDWINDOW`. |
| M8 | `splash_ofertas.h:232`, `238-253` | A tela do grupo tem tamanho (880×452) e fontes fixos em pixels físicos, num processo `PER_MONITOR_AWARE_V2`. Num 4K a 200% ela aparece com metade do tamanho aparente e texto que ninguém lê — justo a peça que sustenta o app. | Multiplicar `SPW`/`SPH`, o `Layout` e as alturas de fonte por `GetDpiForWindow/96`. |
| M9 | `main.cpp:1783` (`IniPath`), `3871` (log), `hdr_control.h:568-571` | Tudo é gravado ao lado do exe. Instalado em `C:\Program Files` (o README pede para não fazer isso, mas o público faz), as escritas vão para o VirtualStore ou falham em silêncio: escolha não é lembrada, `escala_pendente` não protege nada e — o pior — a pista `[hdr] restaurar` não é gravada antes de desligar o HDR do monitor; um `TerminateProcess` deixa o monitor em SDR sem recuperação. | Testar a gravabilidade da pasta no arranque e, se falhar, avisar e cair para `%LOCALAPPDATA%`. |
| M10 | `main.cpp:4217` | `HdrSetTreat(false)` nunca é chamado: se a pessoa desmarcar a caixa no seletor e o ini tiver `[hdr] tratar=1`, o módulo continua com `enabled_ = true`. `WarnLine()` retorna vazio nesse estado, então o aviso "a imagem vai chegar lavada" some justamente para quem desmarcou. | Chamar `HdrSetTreat(C.hdrTreat)` sempre, não só quando marcado. |
| M11 | `main.cpp:3693-3703` | `TamanhoFeedLog()` devolve o tamanho do *tail* (teto de 256 KB), não do arquivo. Se o `dlss5-feed.log` passar de 256 KB durante a sessão, `t.size() <= offsetInicial` e `CadeiaMorreu` devolve `false` para sempre — o watchdog "cadeia morta" fica cego. Hoje é remoto (o log é truncado a cada anexação), mas basta uma sessão com o Feeder reclamando muito. | Usar `GetFileSizeEx` para o marco e comparar tamanhos de arquivo, lendo só o trecho novo. |
| M12 | `ReShade.ini:12` | O ini distribuído carrega `IntermediateCachePath=C:\Users\PCDOFAFA\AppData\Local\Temp\ReShade` — vaza o nome de usuário do autor e aponta para um caminho que não existe na máquina de ninguém (recompilação de shaders a cada abertura, ~30 s). | Apagar a chave do ini distribuído (o ReShade usa o padrão) — e conferir `Docking`/`Window`, que também guardam estado local. |
| M13 | `README.md:132` | "sair: **Alt+F12**" descrito para o modo janela normal, onde `HK_QUIT` só é registrado quando `C.overlay` (main.cpp:4237-4257). No modo janela a saída por tecla é `Alt+Esc` (`AT_SAIR`). O README também não menciona `End`/`F6` na seção Uso — só `Home` e `F11`. | Reescrever a seção Uso a partir do mapa atual (End abre o painel, F6 liga/desliga tudo, Alt+Esc sai). |
| M14 | `main.cpp:1956` (`g_trayCmd`) + `3072-3094` | Um `int` único guarda o comando pendente: dois cliques no painel antes de o loop consumir (o loop pode ficar ~500 ms dentro de um `PokeReShade`) descartam o primeiro em silêncio. | Fila pequena com `SRWLOCK`, ou pelo menos ignorar cliques enquanto houver comando pendente. |
| M15 | `main.cpp:4133-4143` | A escala do ini é aplicada sem revalidar `ScaleCabe` contra a fonte **atual**: quem gravou 200% com um alvo 1080p e depois abre sobre um alvo 4K sobe numa resolução em que a cadeia não sobe. O watchdog só pega depois, pelo log. | Chamar `Renderer::ScaleCabe(cw, ch, pct/100.f)` antes de aceitar o valor do ini. |
| M16 | `main.cpp:1800-1808` (`ResizeClientTo`) | Usa `SPI_GETWORKAREA`, que responde sempre pelo monitor primário — exatamente o erro que a criação da janela (4096-4112) corrigiu. Num setup com o alvo num monitor maior que o primário, a janela é limitada pelo tamanho errado quando a fonte muda. | Usar a área útil do monitor do alvo, como em `AreaParaJanela`. |
| M17 | `main.cpp:1330-1334` (`RetomarAtalhos`) | Re-registra só o que tem `registrado == true` e não atualiza a flag se o `RegisterHotKey` falhar. Se outro programa pegar a tecla durante a janela de suspensão, o painel continua mostrando a tecla como válida e ela não faz nada. | Atribuir o resultado do `RegisterHotKey` de volta a `registrado`. |
| M18 | `screen_capture.h:390-395`, `:718` | Na fonte monitor, `Alive()` é sempre `true` (não há `targetWin`). Desligar/desconectar aquele monitor faz `GetMonitorInfoW` falhar, `TargetRect` devolver `false` e a imagem congelar sem uma linha de aviso ao usuário. | Marcar `closed` quando `GetMonitorInfoW` falhar N vezes seguidas e mostrar cartaz. |
| M19 | `main.cpp:2002-2004` (`TrayAdd`) | O balão da bandeja anuncia "Alt+F9 liga/desliga o filtro / Alt+F10 modo interativo / Alt+F12 sai" — texto fixo, do esquema antigo, contrariando a regra do `docs/painel.md` §3 ("nunca escreva o nome de uma tecla direto num texto"). | Montar a partir de `TextoAtalho(...)`/`g_hkBypass`. |

---

## 4. BAIXOS

- `ofertas.ini:19` distribuído com `mostrar=sempre` e o padrão embutido também é
  `POL_SEMPRE` (`splash_ofertas.h:62`): a propaganda abre **toda** vez que o app abre. O
  `docs/painel.md` §8 e o comentário do `main.cpp:4279` dizem "padrão: primeira execução" —
  a documentação está errada, ou o ini está.
- `main.cpp:832` — o `HANDLE` de `GetFrameLatencyWaitableObject` nunca recebe `CloseHandle`.
- `screen_capture.h:292` — `delete this` numa classe polimórfica sem destrutor virtual
  (único warning real de `-Wall -Wextra` além de dois `misleading-indentation` em
  `screen_capture.h:502` e `:598`). Funciona porque o tipo estático é o concreto.
- `main.cpp:3411` — `Guia::Paint` chama `Hud::UpdateScale`, que sobrescreve `Hud::g_scale`;
  se o painel tiver encolhido para caber e o guia repintar depois, o hit-test do painel passa
  a dividir pela escala errada. Hoje os dois não coexistem (abrir um fecha o outro).
- `main.cpp:4801-4806` — `g_toast`/`g_toastFont` só são destruídos no modo sobreposto.
- `build-mingw.sh` não compila `addon/dlss5_bridge_addon.cpp` (só `addon/build-addon.sh`
  faz) e não liga `-Wall`; é fácil publicar um exe novo com um `.addon64` velho, e o
  `resolve()` do `reshade_bridge.h:192` recusa protocolo diferente em silêncio.
- `reshade_bridge.h:179` — `GetModuleHandleW` sem `AddRef`: se o ReShade descarregar o
  add-on, os ponteiros cacheados ficam pendurados.
- Exe não assinado, com `SetWindowDisplayAffinity`, `SendInput`, hotkeys globais e um
  `dxgi.dll` ao lado: SmartScreen e antivírus vão reclamar. Não é bug, é fato de
  distribuição que merece um parágrafo no README.
- `Renderer::Rebuild` (`main.cpp:907-915`) é código morto desde `FonteMudou` — e é
  justamente o `ResizeBuffers` que a §4.6 proíbe. Vale apagar antes que alguém o chame.

---

## 5. O que não consegui verificar

- **Nada foi executado.** Sem observação em runtime: sem confirmação prática de C1, C2 e A1
  (todos derivados da aritmética dos identificadores, que é determinística), nem dos
  cenários de crash (A6, M3, M6).
- **Máquinas diferentes:** não testei Windows 10 sem `IGraphicsCaptureSession5`, monitor
  único, sem HDR, DPI 150/200% nem dois monitores com escalas diferentes. Pela leitura, as
  cascatas de `Init` (waitable → legacy → DISCARD), de sessão WGC (5 → 3 → 2) e de HDR estão
  corretas e degradam com log; a splash de ofertas é a única peça que ignora DPI (M8).
- **`dlss5-feed.addon64` é de terceiros:** se ele abrir o próprio log sem `FILE_SHARE_READ`,
  `TailArquivo` falha sempre e o watchdog "cadeia morta" (`CadeiaMorreu`) fica cego — não dá
  para saber sem testar com o Feeder rodando.
- **Shaders `lumenite_*`** e `DLSS5_Feed.fx` são de terceiros e não foram revisados; de
  `DLSS5_Grade.fx`/`DLSS5_Sharpen.fx` verifiquei apenas a superfície que a ponte usa (nomes
  de uniform, técnica e a dependência da textura `dlss5_grade_luts.png`).
- **`qr.h`, `hdr_control.h` e `addon/dlss5_bridge_addon.cpp` saem limpos desta revisão.**
  O add-on valida `struct_size`, tipo e terminação das strings antes de enfileirar; o
  `hdr_control` tem quatro redes de restauração (destrutor, `atexit`, filtro de exceção e a
  pista no ini) e revalida o `devicePath` — o único furo é o M9, quando a pasta não é
  gravável.
- **Acentos e espaços no caminho:** conferi todos os pontos de arquivo (`_wfopen`,
  `GetPrivateProfileStringW`, `CreateFileW`, `ShellExecuteW`, `GetModuleFileNameW`) — são
  todos Unicode e o `ShellExecuteW` do "reabrir" passa o caminho separado dos argumentos.
  O código aguenta acento e espaço; a recomendação do README continua valendo pelo ReShade,
  não por nós.

---

# Rodada 2 — reconferência (03/09/2026)

Alvo: `main.cpp` (03:11, 5366 linhas), `screen_capture.h` (02:58), `splash_ofertas.h`
(03:13), `hdr_control.h` (03:07). Método: leitura do código, com o binário recompilado no
scratchpad — `-Wall -Wextra`, **exit 0, e os dois avisos de função morta da rodada 1
(`Guia::Aberto`, `HdrAviso`) sumiram**. Nada aceito por descrição: cada item abaixo foi
localizado e lido.

## Veredito por item

| item (origem) | veredito | o que eu vi no código |
|---|---|---|
| **C1** `Capture` destruído sob a thread presa | **CONFIRMADO** | `std::unique_ptr<Capture> capPtr` + `Capture& cap` (`main.cpp:4501-4502`). No caminho normal o `unique_ptr` destrói (sem vazamento); com a thread presa, `capPtr.release()` **e** `mfOk = false` (`:5355`), então nem o objeto é destruído nem o `MFShutdown` roda (`:5361`). `Stop()` só é chamado em `:5351`; depois dele o `std::thread` não é mais joinable, então o destrutor não pode chamar `terminate` — não há caminho de destruição dupla. |
| **A1** `_snwprintf(b, 16, "tecla %u")` | **CONFIRMADO** | buffer para 24 e `b[23] = 0` (`main.cpp:1269`). |
| **A2** `SendInput` sem foco cai no jogo | **CONFIRMADO (com consequência aberta — ver §R2.1)** | aborta só em `VIA_TECLADO` (`main.cpp:4051`); `VIA_MENSAGEM` segue direto, correto, porque o `PostMessage` vai por handle. Antes de voltar, restaura o click-through e chama `RetomarAtalhos()` — não deixa o app preso em modo interativo nem sem hotkeys. O cartaz tem prazo de 5 s e o `ToastTick` continua rodando: não fica preso. |
| **A3** "filtro pesado" no alt-tab | **CONFIRMADO** | `if (hidden) segundosPesado = 0;` e `!hidden` na condição (`main.cpp:5288-5290`); o texto novo aponta o "tudo", que funciona nos dois modos. |
| **A4** caixa de HDR desmarcada não desligava | **CONFIRMADO** | `HdrSetTreat(C.hdrTreat)` incondicional na abertura (`:4642`) e no painel (`:5005`). |
| **A5 / M9** pasta do exe sem escrita | **CONFIRMADO** | o próprio log é o teste (`:4262-4270`); caindo, vai para `%LOCALAPPDATA%\DLSS5ForAll\` e `g_dadosDir` passa a mandar no `IniPath()` (`:1933`), então ini, `[atalhos]` e a pista `[hdr] restaurar` ficam no mesmo lugar — o `Hdr::Treatment::Init(IniPath())` (`:4618`) e o `RecoverStale()` (`:4619`) leem de lá. Migração com `CopyFileW(..., TRUE)`: não sobrescreve o que já migrou. |
| **M2** laço infinito no `QueryDxgi` | **CONFIRMADO** | `FAILED(...) || !ad` e `FAILED(...) || !op` nos dois laços (`hdr_control.h`, `QueryDxgi`). |
| **M4** splash em thread e COM | **CONFIRMADO** | `CoInitializeEx(APARTMENTTHREADED)` na thread (`:2664`) com `CoUninitialize` no fim; no encerramento, `WM_CLOSE` + `join` (`:5345-5347`). Os quatro términos fecham a thread: X/botão/Esc → `ShowOfferSplash` retorna → `fechou` + `PostThreadMessage`; `Alt+Esc` → o laço externo posta `WM_CLOSE`. O toque único aqui é decisão justificada e eu concordo: a dupla pressão existe por causa do jogo, e aqui não há jogo. |
| **M14** comandos perdidos | **CONFIRMADO** | `FilaComandos` com mutex, `deque` e teto de 32 (`:2213-2227`), mantendo `= cmd` / `.exchange(0)`. |
| **M1** ícone fantasma no `Fatal` | **CONFIRMADO** | `TrayRemove(g_trayWnd)` dentro do `Fatal` (`:106`). |
| **M6** device removed | **CONFIRMADO** | tratado com cartaz e saída limpa (o encerramento normal é quem religa o HDR). |
| **Faixas de ID** | **CONFIRMADO** | recalculadas pelos valores gerados, não pelos asserts: TRAY 200-205 · avulsos 300-309 · fps 400-404 · LUT 1000-1005 · intensidade 1125/1150/1175/1200 · nitidez 1300-1303 · atalhos 3000-3004 · fábrica 3100 · reabrir 4000 · tecla 100000-102047. Nenhum `EKind` ou `TRAY_*` novo entrou (`E_REABRIR` = 16, como anunciado), `AT_COUNT` continua 5, e os seis consumidores testam exatamente as faixas declaradas. A janela 2000/2500 segue vaga. |
| **Cartaz de tecla recusada** | **CONFIRMADO** | `g_algumAtalhoRecusado` só liga quando o Windows recusou (`:1418-1420`), e o cartaz sai uma vez na abertura (`:4716`), apontando o painel ou a bandeja conforme o que sobrou. |
| **Cartaz "caiu e não voltou" + Reabrir** | **CONFIRMADO** | uma vez por queda (`avisouDesistiu`), liga `g_hud.fonteMudou` para o botão aparecer e chama `Hud::Refresh()` (`:5148-5157`); a cota de retomadas renova e o flag é rearmado quando a captura estabiliza. |
| **`--source window` sem `--window`** | **CONFIRMADO** | `Fatal` com a linha de uso (`:4401`). |
| **`DLSS5_DIAG`** | **CONFIRMADO** | só as chaves que MUDAM comportamento passam pelo gate (`:2184-2190`); as que só leem/logam continuam soltas, que é a divisão certa. |
| **Título sanitizado no ini** | **CONFIRMADO** | CR/LF/TAB → espaço antes do `IniSet(L"janela")` (`:4484-4489`) — evitava justamente o ini virar lixo da linha seguinte em diante. |
| **Concorrência do `g_lat`** | **CONFIRMADO** | `SRWLOCK lkGap`, escrita pelo `Invoke` via `ChegouFrame()` sob lock exclusivo, `Reset()` idem, e o `Dump()` lê as estatísticas sob lock compartilhado, copiando para locais antes de logar. |
| **`inFlight` dos handlers** | **CONFIRMADO** | inalterado e ainda correto depois das mudanças no `Start`/`Teardown`: `Desarmar()` zera os ponteiros **antes** de esperar, e todo uso no `Invoke` é load-e-testa. Ordem do `Teardown`: `Close`/`remove` → `Desarmar` → `Release` → `CloseHandle(evt)` só com `manterEvt=false`. |
| **Regressões da rodada 1** | **NENHUMA** | faixas de ID intactas; `R.extSrv` fora do `if (got)` (`:5076`), nulo no `Stop()` da retomada (`:5119`) e reatribuído nos dois desfechos (`:5128`); o BOM ficou **melhor** do que eu tinha pedido — `.tmp` + `WriteFile` verificado + `MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`, com o temporário apagado em cada caminho de erro (`:1995-2012`). |

## R2.1. ALTO — o F6 mente quando o `PokeReShade` aborta (consequência do A2)

`main.cpp:4110-4113` e `:4131-4134` (`AlternarTudo`).

O A2 fez o `PokeReShade` **abortar** quando o foco não veio para a nossa janela — está
certo, e é o caso comum com jogo em tela cheia exclusiva. Só que o `AlternarTudo` **ignora
o retorno** nos dois lados:

```
if (g_antes.neural) {
    PokeReShade(VK_F6, false, VIA_TECLADO);   // pode ter abortado
    g_antes.neuralNos = true;                 // marca como se tivesse desligado
}
```

Com o poke abortado, o neural **continua ligado**, mas `g_tudoDesligado` vira `true` e
`neuralNos` fica dizendo "fomos nós que desligamos". O que a pessoa vê:

1. dois cartazes contraditórios, um atrás do outro — "NÃO CONSEGUI FALAR COM O RESHADE" e
   "FILTRO DESLIGADO — imagem original";
2. no F6 seguinte, o ramo de religar entra (`if (g_antes.neuralNos)`) e, se o foco vier
   desta vez, **desliga** o neural que nunca foi desligado. Dois toques em F6 e a pessoa
   fica sem o neural achando que voltou ao normal.

É exatamente o desfecho que o `docs/painel.md` §3 chama de "o pior possível num app cujo
produto é o neural", agora por um caminho novo. A verificação de 2,5 s pelo log não salva:
ela também dispara um poke, que aborta pelo mesmo motivo.

**Correção sugerida (três linhas):** usar o retorno — `g_antes.neuralNos = PokeReShade(...)`
nos dois pontos — e, quando ele for `false`, não marcar `g_tudoDesligado` como se o neural
tivesse saído (ou dizer no texto que o neural não pôde ser desligado). Quem corrige é o
`hud`.

## R2.2. MÉDIO — o prazo de 25 s do INICIANDO nunca rearma

`main.cpp:2487-2492` (`LerEstadoNeural`).

```
static ULONGLONG t0Iniciando = 0;
if (!t0Iniciando) t0Iniciando = GetTickCount64();
if (GetTickCount64() - t0Iniciando > 25000) { ...; return NR_DESLIGADO; }
```

O relógio é armado uma vez por execução e nunca volta a zero. Resolve o caso que motivou o
item (ReShade ausente na abertura), mas depois de 25 s de app aberto **qualquer** entrada em
INICIANDO vira "não subiu" na hora: quem liga o neural pelo painel aos cinco minutos vê
"DESLIGADO — não subiu, confira o DLSS 5 Feed" antes de o log ter tido tempo de responder, e
o cartaz do botão pode dizer "o add-on não respondeu" quando ele está respondendo. Some
sozinho no segundo seguinte, quando a linha de avaliação aparece no log — é mensagem errada,
não estado errado. **Correção:** rearmar `t0Iniciando` quando o estado sai de INICIANDO (ou
guardar o instante do último pedido de liga/desliga em vez de um estático de execução).

## R2.3. BAIXOS

- `nArrive` continua lido fora do `lkGap` no `Dump()` (`screen_capture.h`), enquanto o
  `Invoke` o incrementa sob o lock. É contador de diagnóstico: não corrompe nem trava, mas
  é o resíduo formal do M2. Uma linha, se quiserem fechar.
- Se o `%LOCALAPPDATA%` também falhar (A5/M9), `g_log` fica nulo e **o aviso sobre a pasta
  sem escrita não sai em lugar nenhum** — o app roda sem persistência e sem uma palavra.
  Canto raro (a variável sempre existe), mas é o único ponto do fallback sem saída visível.

## Veredito da r2 (histórico)

**NÃO INSTALA** — por causa do R2.1, e só dele. O R2.1 era pequeno em código e grande em
consequência: desligava o produto central do app pelas costas do usuário, num cenário (tela
cheia exclusiva) que é justamente o que o A2 foi escrito para tratar.

---

# Rodada 3 — só o diff r2→r3 (03/09/2026)

`main.cpp` de 03:30, recompilado aqui: `-Wall -Wextra`, **exit 0, sem avisos**.

## R2.1 — CONFIRMADO, e melhor do que eu tinha pedido

Eu havia sugerido usar o retorno do `PokeReShade`. Ele foi além e fez **tudo-ou-nada**: com
o poke abortado, a ponte desfaz o que já tinha aplicado e nada é marcado. Os quatro pontos
que interessavam:

**(a) o desfazer cobre o que foi aplicado, nos dois ramos** — sim, e com a assimetria certa.
No ramo de desligar, aplica-se `SetTechniqueEnabled(grade,false)` + `SetSharpenEnabled(false)`
incondicionalmente, e o desfazer religa **só** o que estava ligado (`if (grade || lut > 0)`,
`if (sharpen > 0)`): quem estava desligado continua desligado. O `SetLut` não é desfeito
porque também não é aplicado nesse ramo — não há o que reverter. No ramo de religar, o
desfazer desliga incondicionalmente, e aí o incondicional é que está certo: o estado de
partida era "tudo desligado", então desligar de novo é no-op e o `SetLut` reaplicado vale o
mesmo valor que já estava no uniform.

**(b) `g_antes` não é perdido nem sobrescrito** — os dois ramos fazem a coisa oposta, e cada
uma é a correta. No aborto ao desligar, `g_antes = EstadoFiltro{}` e `g_tudoDesligado`
continua `false`: como nada aconteceu, não há estado a guardar, e o próximo F6 relê o mundo
real — que é justamente o que a restauração da grade, feita **antes** do zeramento, garante
estar íntegro. No aborto ao religar, `g_antes` e `neuralNos` são **mantidos** e
`g_tudoDesligado` continua `true`, então o próximo F6 tenta religar de novo em vez de
inverter o sentido.

**(c) sem cartaz duplo** — `AlternarTudo` devolve string vazia e o chamador tem
`if (!t.empty()) ShowToast(...)` (`main.cpp:5004`). Nem cartaz contraditório, nem caixa
vazia na tela. O único chamador é o `CMD_TUDO` (`:4999`), conferido por grep.

**(d) painel igual ao de antes** — `*bypass` não é tocado (a atribuição está depois do
`return`), `g_hud.tudoDesligado` recebe o `g_tudoDesligado` inalterado, e o card do neural é
relido do log a cada segundo. Detalhe que fecha bem: com `g_antes` zerado, o
`if (g_antes.neural)` do chamador não agenda a verificação de 2,5 s — não sobra um poke
órfão para corrigir um estado que ninguém mudou. No aborto ao religar, a verificação é
agendada com `neuralAlvo = 0`, que é o estado real: ela confirma em vez de "corrigir".

## `AttachThreadInput` — CONFIRMADO

`main.cpp:4045-4060`. O attach só é tentado na segunda volta, com `tidFg && tidFg != tidEu`,
e o `AttachThreadInput(..., FALSE)` está dentro do mesmo `if` que só executa quando o attach
devolveu `TRUE` — não existe caminho que saia da função anexado. Entre attach e detach há
apenas `SetForegroundWindow` e `BringWindowToTop` sobre a **nossa** janela: nenhuma chamada
síncrona para a thread alvo, e o `PumpMessages` está fora do bloco anexado. É a ordem que
evita o deadlock clássico. Com um serviço de integridade mais alta (`GameInputServiceWindow`)
o `AttachThreadInput` é recusado pelo UIPI e devolve `FALSE`, então nem se entra no bloco —
o guard seguinte aborta o poke, que é o comportamento correto.

## R2.2 e R2.3 — CONFIRMADOS

- O relógio do INICIANDO é zerado nas duas transições: ao ver o neural desligado
  (`main.cpp:2463`) e ao ver a cadeia avaliando (`:2485`), e armado só no caminho de espera
  (`:2493`). Ligar o neural tarde na sessão volta a contar do zero.
- `nArrive` passou a ser lido sob `AcquireSRWLockShared(&lkGap)` e copiado para local antes
  do `Log` (`screen_capture.h:145-149`) — fecha o resíduo do M2.
- O caso sem saída (nem a pasta do exe nem o `%LOCALAPPDATA%`) agora tem `MessageBoxW`
  (`main.cpp:4358`), com o texto certo: o app funciona, mas nada será lembrado. É o único
  ponto do app em que uma caixa bloqueante se justifica, e é onde ela está.

## Um BAIXO que fica

`PokeReShade` devolve `false` por dois motivos diferentes — abortei sem enviar (guard, que
mostra cartaz) e `sent < 2` (`SendInput` parcialmente recusado, sem cartaz). O
`AlternarTudo` trata os dois como "nada aconteceu", o que é seguro; mas no segundo caso o F6
desfaz tudo **sem nenhuma explicação na tela**. É um canto quase inalcançável — se o foco já
veio para a nossa janela, o `SendInput` não costuma falhar — e não muda o veredito.

## Veredito

**INSTALA.**

---

# Rodada 4 — diff r3→r4 e o `splash_ofertas.h` inteiro (03/09/2026)

`main.cpp` de 03:40 e `splash_ofertas.h` de 03:36 (949 linhas), recompilados aqui:
`-Wall -Wextra`, **exit 0, sem avisos**.

## Escopo 1 — o diff no `main.cpp`

- **`AreaDaTelaDeOfertas()`** (`:2652`) — CONFIRMADO. A ordem é a única que funciona: área
  provisória em unidades lógicas → centro → DPI daquele monitor → área de novo, agora com o
  tamanho **físico**. Sem isso, "cabe na janela do jogo?" era respondido com 880×452 para um
  cartão que a 150% tem 1320×678, e o header, ao descobrir que não cabia, caía para a área
  do primário — a tela abrindo no monitor errado. O DPI vem do próprio header
  (`ofertas::detail::DpiEfetivoPonto`), então `DLSS5_SPLASH_DPI` vale aqui também e não
  existem duas contas divergentes. Usado nos dois pontos de abertura (`AbrirGrupoOfertas` e
  `OfertasNaAbertura`).
- **Cartaz no caminho `sent < 2`** (`:4137`) — CONFIRMADO, e é exatamente o BAIXO que eu
  havia registrado na rodada 3. Agora os dois motivos de `false` do `PokeReShade` têm
  resposta na tela, e sem cartaz duplo: o `AlternarTudo` continua devolvendo string vazia e
  o chamador só mostra o que não for vazio.

**O que eu não pude fazer, e é honesto dizer:** não guardei o fonte r3, então **não fiz um
diff literal** contra ele. O que fiz no lugar: confirmei por marcador exato que cada ponto
aprovado nas rodadas anteriores continua no arquivo (`R.extSrv = nullptr` nos dois lugares,
`MoveFileExW` do BOM, as três chamadas de `AttachThreadInput`, os três `t0Iniciando = 0`, o
`if (!t.empty()) ShowToast`, o `g_antes = EstadoFiltro{}` do tudo-ou-nada), e o arquivo
cresceu 1.666 bytes — compatível com as duas adições descritas e com nada além delas. Não é
prova, é evidência forte.

## Escopo 2 — `splash_ofertas.h` inteiro

| ponto de atenção | veredito |
|---|---|
| **C3 intacto** | `UrlEhWeb` e `ReadTextFile` estão idênticos ao que aprovei na rodada 1, incluindo a aspa dupla na lista e o `&` liberado. |
| **HFONT ao refazer** | `Fonts::make()` chama `free()` na primeira linha, então o `make(novo)` do `WM_DPICHANGED` não vaza; `free()` também no fim do `ShowOfferSplash`. |
| **HBITMAP/HDC** | o `WM_PAINT` restaura o objeto anterior, deleta o bitmap e o DC no mesmo escopo, e o buffer nasce do tamanho **físico** do cliente — depois de um `WM_DPICHANGED` a janela já mudou de tamanho e a próxima pintura acompanha. |
| **HRGN** | `SetWindowRgn(h, rg, TRUE)` **sem** `DeleteObject` depois: correto, o sistema toma posse da região e libera a anterior. Chamar de novo no `WM_DPICHANGED` não vaza. E a ordem lá está certa: `SetWindowPos` primeiro, `RoundCorners` depois, então o `GetWindowRect` de dentro já vê o tamanho novo. |
| **`DrawTextCaixa` termina** | laço decrescente com piso (`a >= minimoLogico`) e `break` explícito no piso; a fonte da iteração anterior é deletada antes de criar a próxima, a última é usada no desenho e só então deletada. Com `alturaLogica - 1 < minimoLogico` o laço nem roda e desenha com a fonte base. Se o `CreateFontW` falhar, mede com a fonte corrente do DC e desenha com a base — degrada, não quebra. |
| **dpi = 0** | `Esc(v, dpi)` usa `dpi ? dpi : 96` e o denominador é a constante 96: não há divisão por zero. `DpiDaJanela` e `DpiDoPonto` têm piso 96; `DpiForcado` só vale com `DLSS5_DIAG=1` e na faixa 96..960. |
| **hit-test x desenho** | `WM_MOUSEMOVE`, `WM_LBUTTONDOWN`, `WM_LBUTTONUP` e `WM_SETCURSOR` usam o **mesmo** `Layout(rc.right, rc.bottom, rt->dpi)` do `Paint`, com o ponto físico como ele vem — sem conversão de ida e volta para desalinhar. |
| **os quatro términos** | X e "Agora não" (`PontoFecha`), botão/QR/endereço (`OpenLink` + `DestroyWindow`), Esc (`WM_KEYDOWN`) e `Alt+Esc` (o laço de `OfertasNaAbertura` posta `WM_CLOSE`) — todos chegam em `WM_DESTROY` → `PostQuitMessage`, que encerra o laço da thread. |
| **texto do ini com fonte reduzida** | `DrawTextW(..., -1, ...)` sobre string terminada e já cortada em 512 pelo `Sanitiza`; não há índice manual em lugar nenhum do caminho de desenho. |
| **alpha final** | termina em 255, opaco, e o fallback do `SetTimer` que falha também aplica 255. |

## Os BAIXOS que ficam

1. `ShowOfferSplash`: se o `CreateWindowExW` falhar, a função volta sem `UnregisterClassW` —
   a classe fica registrada até o processo sair. Pré-existente, inofensivo (a próxima
   chamada reaproveita a classe).
2. `DpiDoPonto` faz `LoadLibraryW`/`FreeLibrary` de `shcore.dll` a cada chamada. Só acontece
   na abertura da tela, nunca em caminho quente — custo irrelevante, fica só registrado.
3. `DrawTextCaixa` com `CreateFontW` falhando mede pela fonte corrente do DC: o texto pode
   ficar um degrau maior que o ideal. Degradação silenciosa e sem consequência.

## Veredito

**INSTALA.**

---

# Rodada 5 — o F6 pela ponte (hook de IAT) — 03/09/2026

Escopo: `addon/dlss5_bridge_addon.cpp`, `reshade_bridge.h` e o diff r4→r5 do `main.cpp`
(04:11). Recompilei o **exe** com `-Wall -Wextra`: exit 0, sem avisos. **Não compilei o
add-on** (precisa do SDK do ReShade) — revisei o fonte; o binário entregue não foi conferido.
Nada foi executado: os cenários cruzados de versão estão verificados por leitura da ABI.

## 1. O hook — CONFIRMADO

| ponto | o que vi |
|---|---|
| proteção de página | `VirtualProtect(PAGE_READWRITE)` antes da escrita e **restauração da proteção original** logo depois, nos dois sentidos (instalar e remover). Falha do primeiro `VirtualProtect` zera `g_nr_slot` e loga — não deixa estado meio-feito. |
| escrita atômica | um ponteiro alinhado de 8 bytes no x64: o RenoDX lendo em paralelo vê o valor velho ou o novo. |
| módulo certo, e só ele | `GetModuleHandleExW(..., L"renodx-dlss5.addon64", ...)` e `find_iat_slot` só naquele módulo, comparando `"USER32.dll"` com `_stricmp` e o nome da função com `strcmp`. Nenhum outro add-on é tocado. |
| `FLAG_PIN` | presente, e é o que impede o módulo de sair debaixo do ponteiro. |
| desmontagem | só em `FreeLibrary` real (`reserved == nullptr`); no fim do processo a IAT do RenoDX não é tocada. Correto: nesse momento ela pode já não existir. |
| 6 tentativas | `if (nr_attempts < 6 && g_nr_ready == 0)` — **depois de instalado nunca reinstala**, e isso importa mais do que parece: uma segunda instalação leria `*g_nr_slot` já trocado e `g_nr_original` passaria a apontar para a própria substituta, com recursão infinita no primeiro frame. A guarda fecha isso. Só a última tentativa escreve o motivo no log. |
| `NRToggleKey` | lido de `[RenoDX.DLSS5]`, aceito só em `0 < cfg < 256`, com F6 como padrão. |
| pulso com borda | `0x8000` durante as 6 leituras e `0x0001` na primeira, cobrindo tanto quem testa nível com borda própria quanto quem testa "pressionada desde a última chamada". A janela entre os dois `store` do disparo não duplica nem perde o toque — quem ler no meio consome a borda e recebe o nível nos frames seguintes. |
| não altera o RenoDX para o resto | qualquer `vKey` diferente do configurado cai direto em `g_nr_original`. |
| o toque não vira estado | `NR_TOGGLE` não entra na fila nem no "estado desejado": um reload de efeitos não alterna o neural sozinho. |

## 2. Compatibilidade de versão — CONFIRMADA nos dois sentidos

`DLSS5BridgeCommand` **não mudou de tamanho** (mesmos campos), então o `Submit` do add-on
novo, que exige igualdade de `struct_size`, continua aceitando o exe antigo. O
`DLSS5BridgeStatus` cresceu **só no fim**, e os 11 campos originais dão exatamente os 44
bytes de `STATUS_SIZE_V1`.

- **exe v1 (r4) + add-on novo:** `Version()` devolve 1 (congelado em `PROTOCOL_MIN`), a
  igualdade que o cliente v1 exige é satisfeita; `Query` aceita `struct_size` de 44 até o
  atual e faz `memcpy(out, &s, want)`. LUT, nitidez e grading seguem.
- **exe novo + add-on v1:** `resolve()` aceita `ver() <= PROTOCOL` em vez de exigir
  igualdade; `DLSS5Bridge_Protocol` não existe no v1, e é a ausência dele que marca `proto = 1`;
  aí o `GetStatus` **pede 44**, que é o único tamanho que o add-on v1 aceita. `NR_TOGGLE` (6)
  cai fora da faixa de tipos do v1 e é recusado, então `ToggleNeural()` devolve false e o app
  usa o `PokeReShade`. Nada quebra em nenhuma das quatro combinações.

## 3. `main.cpp` — CONFIRMADO

`AlternarNeural()` tenta a ponte e cai no `PokeReShade`, e está nos quatro pontos: os dois
ramos do `AlternarTudo` (com o tudo-ou-nada do R2.1 **intacto** — o retorno continua
decidindo `neuralNos` e o desfazer continua lá), o botão do card e a correção automática do
"invertido".

**Religar no encerramento** — os cinco critérios:
- só com `g_tudoDesligado && g_antes.neuralNos`: quem desligou o neural por conta própria
  não é tocado;
- não trava: o laço tem teto de 12 quadros e só roda se o toque foi armado; ele sai no
  primeiro `Present` quando o RenoDX lê, porque a condição é `NeuralDelivered()` mudar —
  medir a entrega em vez de chutar um número de quadros é o que faz isso terminar cedo;
- **a posição está certa**, e é o ponto mais fácil de errar: o bloco vem **antes** de
  `Guia::Shutdown`, `Hud::Shutdown`, `TrayRemove`, `HdrShutdown()`, `cap.Stop()` e
  `R.Shutdown()`. O renderer precisa estar vivo para apresentar (é o `PresentNow` que faz o
  RenoDX ler o toque), o HDR ainda não foi restaurado e o `Capture` do C1 ainda não foi
  parado — nenhuma colisão com os três.

## 4. Faixas de ID — CONFIRMADAS

Nenhum comando novo entrou (o toque do neural não passa pelo `g_trayCmd`). Recalculadas
pelos valores gerados: 200-205 · 300-309 · 400-404 · 1000-1005 · 1125/1150/1175/1200 ·
1300-1303 · 3000-3004 · 3100 · 4000 · 100000-102047, com os 8 `static_assert` no lugar.

## Os BAIXOS

1. `g_nr_left` é decrementado com `load` + `store`, não com um read-modify-write atômico. O
   RenoDX lê a tecla na thread de render dele (uma só), então na prática não há corrida; se
   um dia houvesse duas, o pior caso é o pulso durar um frame a mais.
2. No encerramento, se a ponte não estiver disponível, o religamento cai no `PokeReShade` —
   que pode abortar por foco e mostrar um cartaz de 5 s numa janela que será destruída em
   seguida. Flash sem consequência, e o neural fica desligado no ini: é exatamente o
   comportamento que existia antes da ponte.
3. `find_iat_slot` confia na integridade do PE do módulo já carregado (não valida limites de
   `imp->Name` nem de `AddressOfData`). É a forma canônica de percorrer uma IAT de um módulo
   que o loader já mapeou; um PE malformado a esta altura seria problema do loader.
4. `NeuralUplift=1` no ini ao final depende de o AutoSave do ReShade gravar durante o
   descarregamento, depois do nosso `return`. Medido pelo `hud`, não por mim.
5. **Sem a ponte, o laço do religamento espera os 12 quadros inteiros** — ver a nota de
   alvo abaixo.

## Nota de alvo (04:16:46) e uma correção de premissa

O fonte revisado é o de **04:16:46**: o binário que compilei dele tem 1.930.053 bytes, o
mesmo tamanho do `DLSS5ForAll-r5.exe` final. O bloco do religamento é idêntico ao que eu já
tinha lido — o teto de 12 medindo `NeuralDelivered()` já estava no fonte às 04:11; era o
binário de 04:12 que estava atrás.

**O teto está correto** (`q < 12`, limitado e sem espera indefinida). Mas a segunda metade
da pergunta tem resposta negativa, e vale registrar: **sem ponte, ou com ponte e sem RenoDX,
o laço NÃO sai cedo — ele roda os 12 quadros.** O motivo é aritmético:
`NeuralDelivered()` devolve `0` quando não há add-on (o `GetStatus` falha) e devolve o
`nr_delivered` parado em `0` quando o hook não instalou. Nos dois casos
`entreguesAntes == 0` e a condição `NeuralDelivered() == entreguesAntes` continua
verdadeira até o teto. Como nesse caminho quem agiu foi o `PokeReShade` — que já apresentou
8 quadros por conta própria —, os 12 são puro desperdício.

Custo real: `PresentNow(1)` é `WaitForPresentSlot()` (espera com timeout de 100 ms, que no
encerramento costuma voltar na hora, com a GPU livre) mais um `DrawAndPresent`. Na prática,
dezenas de milissegundos; no pior caso teórico, ~1,2 s a mais para fechar o app. Não trava e
não muda o veredito. **Correção, quando houver outra rodada:** só entrar no laço quando o
toque foi armado pela ponte — `AlternarNeural()` dizendo por qual caminho foi, ou um
`rsbridge::NeuralToggleAvailable()` antes do `for`.

## Veredito

**INSTALA.**
