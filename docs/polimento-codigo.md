# Polimento de código — DLSS5 For All

Segunda rodada adversarial, focada em **código, robustez, concorrência, segurança, ciclo de
vida e desempenho**. Base: leitura integral de `main.cpp`, `screen_capture.h`,
`hdr_control.h`, `splash_ofertas.h`, `qr.h`, `reshade_bridge.h`,
`addon/dlss5_bridge_addon.cpp`, `DLSS5_Grade.fx`, `DLSS5_Sharpen.fx`, `build-mingw.sh`;
compilação limpa com `-Wall -Wextra`; e **três execuções reais** do app sobre
`scratchpad\lattarget.exe` (espelho e sobreposto), medindo CPU e lendo o log.

**Instantâneo:** `main.cpp` com 5024 linhas (03/09/2026 02:37). O arquivo estava sendo
editado por outros agentes durante a revisão — três achados da rodada anterior foram
corrigidos no meio dela (ver §5). As linhas abaixo são desse instantâneo; cada achado traz
o trecho de código para que a referência sobreviva ao deslocamento.

**O que foi de fato exercitado em runtime:** abertura, seletor por linha de comando, WGC
sobre janela, modo espelho, modo sobreposto, degradação sem `dlss5-bridge.addon64` e sem
`renodx-dlss5.addon64`, captura em HDR (o monitor desta máquina está em HDR: o pool abriu
em `R16G16B16A16_FLOAT`, paper white 240 nits), `ReShade.ini` deliberadamente envenenado.
O que **não** foi exercitado está na §6.

---

## 1. CRÍTICO

### C1. `Capture::Stop` abandona `reader`/`src`/`evt` — mas não o objeto `cap`, que é destruído embaixo da thread

`main.cpp:369-393` (`Capture::Stop`), objeto declarado em `main.cpp:4245` (`Capture cap;`),
destruído no `return` de `wWinMain` (`main.cpp:5023`).

A correção da rodada anterior (§11.6 do `painel.md`) resolveu metade do problema: quando a
thread não termina em 2 s, `reader`, `src` e `evt` são deixados com ela de propósito. Só que
a thread continua usando **o objeto inteiro**, não só esses três ponteiros:

```cpp
// Capture::Loop, main.cpp:322-352
std::lock_guard<std::mutex> lk(mtx);        // mtx é membro de 'cap'
if (buf.size() == rowBytes * h && h > 0) {  // buf/w/h são membros de 'cap'
    ... memcpy(&buf[y * rowBytes], ...);
```

`cap` é um **local de pilha do `wWinMain`**. A sequência de encerramento é
`cap.Stop()` → `R.Shutdown()` → `MFShutdown()` → `CoUninitialize()` → `fclose` → `return`,
e só então o destrutor de `cap` roda: `std::mutex` destruído, `std::vector buf` liberado, a
pilha do `wWinMain` some. Se o `ReadSample` travado destravar nessa janela (dezenas a
centenas de milissegundos), a thread destacada trava um mutex destruído e escreve num vetor
liberado — **use-after-free, crash na saída**, exatamente o desfecho que a correção queria
evitar, no exato cenário (placa/cabo ruim) que a motivou.

Agrava: `MFShutdown()` é chamado com a thread ainda dentro do Media Foundation.

**Reprodução:** placa de captura USB com o cabo puxado, ou driver que trava o `ReadSample`;
fechar o app. Não reproduzível sem esse hardware — é análise de código, e o próprio
comentário do `Stop()` descreve o cenário como real e já visto.

**Correção (uma frase):** alocar o `Capture` no heap com `std::shared_ptr` que a thread
também segure (ou simplesmente `new Capture` e vazar o objeto quando `viva == true`), para
que `mtx`/`buf` sobrevivam à thread abandonada.

---

## 2. ALTOS

### A1. `_snwprintf(b, 16, L"tecla %u", vk)` estoura o buffer com um `vk` de 10 dígitos vindo do `ReShade.ini`

`main.cpp:1248` (`NomeTecla`), alimentado por `main.cpp:1384` (`TeclaOverlayReShade`).

```cpp
wchar_t b[16]; _snwprintf(b, 16, L"tecla %u", vk);
return s + b;
```

`"tecla "` são 6 caracteres; `%u` de um `UINT` chega a 10 dígitos. **16 exatos** — e o
`_snwprintf` do CRT do Windows **não põe o terminador quando o texto preenche o buffer**.
O `std::wstring` da linha seguinte lê para fora da pilha.

O `vk` não é confiável: vem de `swscanf(b, L"%u,%u,%u,%u", &vk, ...)` sobre
`[INPUT] KeyOverlay` do `ReShade.ini` — arquivo de terceiro, texto plano ao lado do exe, que
viaja no zip.

**Confirmado experimentalmente** (msys2/mingw64, mesmo CRT do build):

```
_snwprintf(b, 16, L"tecla %u", 4294967295u)
ret=16   b[15]=0x0035 (dígito, não NUL)   b[16]=0x4141 (sentinela intacta)
wcslen(b)=29        <-- leu 13 wchar_t além do buffer
```

E o app rodou com `KeyOverlay=1234567890,0,0,0`: não caiu (a pilha logo depois tinha um zero
por sorte), mas a leitura fora do buffer aconteceu e o resultado vai para o painel, para o
guia e para o rodapé.

**Correção:** `wchar_t b[24]` e `b[23] = 0;` (ou `swprintf_s`), e recusar `vk > 0xFF` já em
`TeclaOverlayReShade`.

---

### A2. F6 é sintetizado com `SendInput` sem conferir se o foco veio para nós — a tecla cai dentro do jogo

`main.cpp:3813-3836` (`PokeReShade`).

```cpp
if (wasCT) SetClickThrough(h, false);
SetForegroundWindow(h);      // <- retorno ignorado
SetActiveWindow(h);
SetFocus(h);
PumpMessages(70);
...
else  sent = SendInput(1, &in[0], sizeof(INPUT));   // VIA_TECLADO: F6 físico
```

`SetForegroundWindow` é recusado pelo Windows quando o processo chamador não tem direito de
foreground — e o caso normal do F6 é exatamente esse: o **jogo** está em primeiro plano
quando a hotkey global dispara. Quando a chamada falha, o `SendInput` seguinte injeta um F6
**real** na janela que estiver com o foco, isto é, no jogo. F6 é quicksave/quickload/console
em uma quantidade enorme de jogos.

O código já **loga** o descompasso depois do fato — `foco=%p janela=%p`, `main.cpp:3847` —
mas não o usa para decidir. O `VIA_MENSAGEM` (End → ReShade) é seguro: `PostMessageW` vai
para a nossa janela.

**Correção:** depois do `PumpMessages(70)`, abortar com cartaz se
`GetForegroundWindow() != h` em vez de disparar o `SendInput`.

---

### A3. "O FILTRO ESTÁ PESADO" dispara sozinho quando a pessoa sai do jogo — **observado em runtime**

`main.cpp:4877` (define `hidden`) e `main.cpp:4970` (a condição do aviso, que não o consulta).

```cpp
bool hidden = C.overlay && !overlayVisible;
ULONGLONG gap = hidden ? 500 : 100;          // camada escondida: Present a 2 fps DE PROPÓSITO
...
if (!avisouPesado && (forcaAviso || (!bypass && C.fpsCap == 0 && fps >= 20.0 && pfps < fps * 0.5))) {
```

Com a camada fora de vista (jogo minimizado, ou Alt+Tab para o navegador), o Present cai
para 2/s **porque o código quer isso** — e a chegada continua em 60/s, porque o WGC continua
entregando. A razão `pfps < fps*0.5` fica satisfeita 100% do tempo, `segundosPesado` sobe
sem que o `else if (pfps >= fps*0.7)` jamais o zere, e em 5 segundos o cartaz acusa a
máquina do usuário de estar lenta.

**Observado, não deduzido** — trecho do `dlss5forall.log` da execução em modo sobreposto:

```
[02:33:56.371] overlay fora de vista (alvo fora de foco / minimizado / bypass)
[02:34:01.362] aviso de desempenho: chegada=60.0 present=2.0
```

Cinco segundos exatos depois de sair de vista. E como `avisouPesado` vira `true` para
sempre, o aviso **verdadeiro** nunca mais vai aparecer nessa sessão.

Atinge o modo padrão da fonte Janela (SOBREPOR vem marcado), na interação mais comum que
existe: sair do jogo por um instante.

**Correção:** somar `&& !hidden` à condição e zerar `segundosPesado` enquanto `hidden`.

---

### A4. Desmarcar a caixa de HDR no seletor não desliga nada — e na placa de captura a conversão continua rodando

`main.cpp:4383` e `hdr_control.h:918-942` / `hdr_control.h:978`.

```cpp
if (C.hdrTreat) g_hud.hdrTreat = C.hdrTreat = HdrSetTreat(true);   // main.cpp:4383
```

`HdrSetTreat(false)` **nunca é chamado**. `Treatment::Init` já ligou `enabled_` a partir de
`[hdr] tratar` do ini (`hdr_control.h:884`), então quem tinha a chave em `1` e desmarcou a
caixa fica com `enabled_ == true` e `C.hdrTreat == false` — o app e o painel discordam.

Na fonte de tela o estrago é só o aviso sumindo (`WarnLine()` devolve vazio quando
`enabled_`). Na **placa de captura / console** é a imagem: o gate da conversão é o
`enabled_`, não a caixa —

```cpp
// hdr_control.h:978
if (src_ != SRC_VIDEO || !enabled_ || !hdrFound_) return p;    // VM_NONE
p.mode = (vcol_.hasTf && vcol_.tf == VTF_HLG) ? VM_HLG_2020 : VM_PQ_2020;
```

— ou seja, a conversão PQ/BT.2020 continua ligada com a chave desmarcada, e para desligá-la
o usuário tem de clicar duas vezes no painel (a primeira só sincroniza os dois estados).
Este é o M10 da rodada anterior; confirmei que continua e que o efeito é maior do que o
descrito lá.

**Correção:** trocar por `g_hud.hdrTreat = C.hdrTreat = HdrSetTreat(C.hdrTreat);`,
incondicional.

---

### A5. Instalado em `C:\Program Files`, o monitor pode ficar em SDR sem nenhum caminho de volta

`main.cpp:4033` (`_wfopen(g_exeDir + L"dlss5forall.log", L"w")`), `main.cpp:1818` (`IniPath`),
`hdr_control.h:568-571` (`Manager::IniSet`), `hdr_control.h:499-506`.

Tudo é gravado ao lado do exe. Num processo **de 64 bits não existe virtualização de
arquivo** (o VirtualStore só vale para 32 bits), então numa pasta protegida as escritas
simplesmente falham:

- `g_log` fica `NULL` e o `Log()` vira no-op — nenhum diagnóstico;
- as escolhas do seletor não são lembradas, em silêncio;
- e o grave: a pista `[hdr] restaurar` — que o `DisableFor` grava **antes** de desligar o
  HDR justamente para o caso de o processo morrer — não é gravada. Um `TerminateProcess`
  (gerenciador de tarefas, antivírus, TDR) depois disso deixa o monitor do usuário em SDR
  **sem nenhuma recuperação**: nem o destrutor, nem o `atexit`, nem o filtro de exceção
  rodam, e o `RecoverStale()` da próxima abertura não tem o que ler.

O README pede para não instalar em `Program Files`; o público instala.

**Correção:** testar a gravabilidade da pasta no arranque e, se falhar, cair para
`%LOCALAPPDATA%\DLSS5ForAll` (log, ini e pista de HDR juntos) com um aviso no cartaz.

---

## 3. MÉDIOS

| # | onde | o que quebra | correção |
|---|---|---|---|
| M1 | `main.cpp:4818` e `4845-4851` | Na fonte **monitor**, a recriação da captura exige `C.win` (`scap.SessionClosed() && C.win && IsWindow(C.win)`), que é sempre nulo — não há caminho de recuperação nenhum. E o ramo seguinte (`!C.win \|\| !IsWindow(C.win)`) é satisfeito por `!C.win`, então uma sessão de monitor que caia (troca de modo, monitor desconectado) loga **"A janela capturada foi fechada"** e congela a imagem para sempre. | Tratar o monitor no mesmo ramo (`SessionClosed() && (C.win ? IsWindow(C.win) : C.mon)`) e separar a mensagem por tipo de fonte. |
| M2 | `hdr_control.h:290-295` (`QueryDxgi`) | `for (UINT a = 0; !out.ok; a++) { if (fac->EnumAdapters1(a,&ad) == DXGI_ERROR_NOT_FOUND) break; ...}` — só `DXGI_ERROR_NOT_FOUND` encerra o laço. Qualquer outro `HRESULT` de erro (com `ad`/`op` nulos) faz o laço **girar para sempre**: travamento na abertura, numa máquina que não é a do autor. O laço interno de `EnumOutputs` tem o mesmo desenho. | `if (FAILED(hr)) break;` nos dois laços. |
| M3 | `screen_capture.h:307-318` vs `:102-135` | O `Invoke` do WGC (thread do sistema) escreve `g_lat.gap`, `g_lat.lastArr` e `g_lat.nArrive` sem sincronização, enquanto o loop principal faz `Reset()`/`Dump()` nos mesmos campos. Não há escrita fora de faixa (o índice é checado contra `CAP`), mas basta um `Reset()` cair entre o `if (n < CAP)` e o `v[n++]` para `n` ficar travado em 1024: **a linha "ritmo do WGC" de todos os relatórios LAT seguintes passa a repetir os mesmos 1024 valores antigos**. É o M2 da rodada anterior; continua aberto. | `nArrive`/`lastArr` atômicos e um `SRWLOCK` em torno de `gap`, ou acumular no handler em variáveis próprias. |
| M4 | `main.cpp:2488-2491` (`AbrirGrupoOfertas`) | A splash roda numa `std::thread(...).detach()` que **nunca chama `CoInitializeEx`**, e o botão "QUERO AS OFERTAS" termina em `ShellExecuteW` (`splash_ofertas.h:601`), que a documentação exige que seja chamado com COM inicializado (ele delega para extensões de shell). Na máquina errada o botão que sustenta o app não faz nada, sem erro. Segundo problema do mesmo ponto: a thread destacada sobrevive ao `wWinMain`, e o `exit()`/`ExitProcess` do `Fatal()` pode matá-la segurando o heap lock. | `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` no começo da lambda (e `CoUninitialize` no fim); esperar a thread no encerramento em vez de destacá-la. |
| M5 | `main.cpp:1048` (`sc->Present`) | `HRESULT` ignorado: sem tratamento de `DXGI_ERROR_DEVICE_REMOVED`/`DEVICE_RESET`. Num TDR ou numa atualização de driver com o app aberto — comum em público — o app segue apresentando num device morto: janela preta, sem mensagem, e o HDR só volta quando o usuário descobre sozinho que precisa fechar. Não é busy-loop (os `wait` têm timeout de 100 ms), mas é uma tela preta permanente. | Checar o retorno e, em device removed, cartaz + encerramento limpo (que já restaura o HDR). |
| M6 | `main.cpp:2084` (`g_trayCmd`) | Um `int` atômico guarda **um** comando pendente. O `PokeReShade` segura o loop por ~500 ms (`6 × PumpMessages(45)` + `PumpMessages(80)`); dois cliques no painel nessa janela descartam o primeiro em silêncio. M14 da rodada anterior, ainda aberto. | Fila curta com `SRWLOCK`, ou ignorar cliques enquanto houver comando pendente. |
| M7 | `main.cpp:1767-1769` (`ShowSelector`) | `CreateWindowExW` sem checagem de retorno; com `h == nullptr` o `SetWindowLongPtrW` é inócuo mas o `while (GetMessageW(&msg, nullptr, 0, 0) > 0)` logo abaixo **bloqueia para sempre sem janela nenhuma**: o app trava na abertura sem nada na tela. O `ShowAtalhosDialog` e o `ShowOfferSplash` já checam; só o seletor não. | `if (!h) return false;`. |
| M8 | `splash_ofertas.h:317`, `:323-337`, `:355-365` | `SPW/SPH` (880×452), o `Layout()` inteiro e as alturas de fonte são pixels **físicos**, num processo `PER_MONITOR_AWARE_V2`. Num 4K a 200% a tela do grupo aparece com metade do tamanho aparente e texto ilegível — justo a peça que paga o app. M8 da rodada anterior, ainda aberto. Não consegui testar (não dá para mexer no DPI da máquina do usuário agora). | Multiplicar tamanho, layout e fontes por `GetDpiForWindow(h)/96`. |
| M9 | `main.cpp:99-104` (`Fatal`) | Sai por `ExitProcess` sem `TrayRemove` nem `Stop()` da captura: um `Fatal` dentro do loop (ex.: `main.cpp:4786`, falha ao redimensionar a textura da câmera) deixa ícone fantasma na bandeja. M1 da rodada anterior, ainda aberto. | Ponteiro global para a janela da bandeja e `TrayRemove` no `Fatal`. |
| M10 | `main.cpp:3942` (`FollowTarget`) | Usa `scap.TargetRect` (não a versão cacheada) uma vez por volta do laço: `DwmGetWindowAttribute` é uma chamada ao processo do DWM, e é exatamente o custo que o `TargetRectCached` (`screen_capture.h:492`) existe para evitar no caminho crítico. **Honestidade:** medi e não achei custo relevante — 0,67 s de CPU em 14 s a 60 fps no modo espelho, 0,34 s em 12 s no sobreposto; ~10-50 µs por chamada a 60 Hz não aparece. Fica como higiene, não como gargalo. | Trocar por `TargetRectCached`. |

---

## 4. BAIXOS

- **`main.cpp:5022`** — `fclose(g_log)` sem `g_log = nullptr`. Qualquer `Log()` posterior
  (destrutor estático do `g_hdrTr`, `atexit` do `Hdr::Manager`) escreveria num `FILE*`
  fechado. Hoje nenhum desses caminhos loga (o `Restore()` sai cedo com `off_` vazio), mas é
  uma mina armada a um `return` de distância.
- **`main.cpp:851` / `1052`** — o `HANDLE` de `GetFrameLatencyWaitableObject` nunca recebe
  `CloseHandle` no `Renderer::Shutdown`.
- **`main.cpp:927`** — `Renderer::Rebuild` continua sem chamador nenhum, e é justamente o
  `ResizeBuffers` que o `painel.md` §4.6 proíbe. Apagar antes que alguém o chame.
- **`main.cpp:4380-4382`** — `SetScreenFp16` é chamado **antes** do primeiro `Detect()`,
  então `dxgi_.ok` ainda é `false` e `screenPeak_` nunca recebe o pico real do painel
  (`hdr_control.h:956`); fica no 1000 chutado que o comentário diz não querer. Inócuo hoje
  porque o joelho padrão é 1,0 (sem roll-off); passa a importar com `DLSS5_HDR_KNEE < 1`.
- **`main.cpp:4177`** — `--source window` sem `--window` casa com a **primeira** janela
  enumerada (`IStrHas` devolve `true` para agulha vazia). `--overlay` sozinho cai nisso.
- **`main.cpp:4241`** — `IniSet(L"janela", C.winTitle)` grava o título cru no ini; um título
  com `\r\n` (permitido pelo Win32) corrompe o `dlss5forall.ini`.
- **`main.cpp:4954-4963`** — `g_toast`/`g_toastFont` só são destruídos no modo sobreposto.
- **`screen_capture.h:327` e `:376`** — o `Desarmar()` espera no máximo 500 ms pelo
  `inFlight`; estourado o teto, o `CloseHandle(evt)` acontece com um `Invoke` possivelmente
  ainda de posse do handle. O comentário assume o risco; vale registrar que ele existe.
- **`addon/dlss5_bridge_addon.cpp:353` e `:395`** — `batch[kQueueCap + kStateCap]` (192) e
  `batch[kTechCap + kStateCap]` (96) cabem **exatamente** no pior caso (64 estados + 128 da
  fila; 64 estados + 32 da fila). Está correto hoje, e quebra silenciosamente no dia em que
  alguém mexer numa das constantes. Um `if (batch_count >= sizeof(batch)/sizeof(batch[0])) break;`
  custa nada.
- **`addon/dlss5_bridge_addon.cpp:430-436`** — `on_destroy_runtime` zera `grade_loaded` mas
  não `g_current_lut`/`g_grade_enabled`: o painel mostra a LUT antiga depois que o runtime
  morre.
- **`main.cpp:4029-4034` / `4132` / `4217`** — os caminhos `--list`, `--list-windows` e
  "cancelado no seletor" saem por `return 0` sem `MFShutdown`/`GdiplusShutdown`/`fclose`.
  Inofensivo (o processo morre), mas assimétrico.
- **`main.cpp:3965`** — `CommandLineToArgvW` sem `LocalFree`.
- **`build-mingw.sh`** — continua sem compilar `addon/dlss5_bridge_addon.cpp` e sem `-Wall`;
  é fácil publicar um exe novo com um `.addon64` velho, e o `resolve()` do
  `reshade_bridge.h:192` recusa protocolo diferente **em silêncio**.

---

## 5. Da rodada anterior: o que confirmei corrigido

Verificado no código, não aceito pelo documento:

- **C1** (faixas de comando encostadas) — resolvido de verdade: as faixas moram em milhares
  separados e há `static_assert` travando a reincidência (`main.cpp:2210-2217`).
- **C2** (sair da captura de atalho) — `CancelarCapturaAtalho()` é ponto único, chamado do
  Esc e do `Hud::Close()`.
- **C3** (URL do `ofertas.ini`) — `UrlEhWeb` recusa esquema, espaço, controle, `\`, `"`,
  `<`, `>`, `|`, `^` e mais de 2048 caracteres, em duas barreiras (`LoadConfig` e
  `OpenLink`). Revisei os bypasses óbvios (`%`-encoding, userinfo, host longo) e não achei
  furo.
- **A2/M4/M5** (queda da sessão WGC) — `add_Closed` registrado, `Start()` faz `Teardown`
  limpo com guarda RAII, `evt` criado antes de qualquer caminho de erro, handlers desarmados
  antes do `CloseHandle`. A contagem de referências dos dois handlers está correta nos
  quatro desfechos.
- **A5** (reabrir) — usa `GetModuleFileNameW` e confere o retorno do `ShellExecuteW`.
- **A8/§11.7** (`_snwprintf` sem terminador) — corrigido nos pontos de título de janela e
  rótulo de modo… **menos em um**, que é o A1 desta rodada.
- **M3** (`R.extSrv` pendurado) — a atribuição saiu do `if (got)`.
- **M7** (F11 na camada sobreposta) — marca explícita `emTelaCheia`.
- **M16** (`ResizeClientTo` com `SPI_GETWORKAREA`) e **M17** (`RetomarAtalhos` sem atualizar
  `registrado`) — corrigidos **durante esta revisão**, por outro agente.
- **Warnings** — `Guia::Aberto` e `HdrAviso` foram removidos durante a revisão; a compilação
  com `-Wall -Wextra` agora só emite `-Wmissing-field-initializers` de `WNDCLASSEXW{sizeof}`
  e um `-Wcast-function-type` de `GetProcAddress`, ambos idiomáticos e inofensivos.

Ainda abertos e já documentados (repetidos acima com o número novo): M1→M9, M2→M3, M6→M5,
M8→M8, M10→A4, M14→M6, M9→A5.

Fora do escopo desta rodada, mas continuam de pé: **A7** (o `setup-dlss5.ps1` não copia o
`dlss5-bridge.addon64`, os dois `.fx` nem a textura das LUTs), **M12** (`ReShade.ini`
distribuído com `IntermediateCachePath` apontando para a pasta do autor), **M13** (README
com o mapa de teclas velho) e **M19** (balão da bandeja — este, aliás, **já foi corrigido**:
`main.cpp:2130-2139` monta o texto a partir de `g_hkBypass`/`TextoAtalho`).

---

## 6. O que NÃO consegui verificar, e por quê

- **Máquinas diferentes.** Só esta: Windows 11 26200, monitor em HDR, DPI 100%, GPU com
  `IGraphicsCaptureSession5` e `IDXGISwapChain2` presentes. **Não testei** Windows 10 sem
  `IGraphicsCaptureSession5`, sem `IDXGISwapChain2`, DPI 150/200%, dois monitores com
  escalas diferentes, GPU AMD/Intel, nem usuário sem privilégio. Por leitura, as cascatas
  (waitable → legacy → DISCARD; sessão 5 → 3 → 2; `GET_ADV_COLOR_INFO_2` → `_1`) estão
  corretas e degradam com log; os pontos frágeis para "outra máquina" são o M2 (laço
  infinito no `QueryDxgi`) e o M8 (splash sem DPI).
- **Placa de captura.** A máquina tem uma (`ezcap Game Link RAW`), mas usá-la com o usuário
  dormindo, sem sinal no HDMI, testaria o caminho de erro e não o de sucesso. O A4 (fonte de
  vídeo com `enabled_` preso em `true`) é leitura de código; a lógica é direta, mas não foi
  observada rodando.
- **DPI alto.** Não mexi na escala do Windows do usuário. O M8 e a divergência de escala
  entre painel e guia continuam por conferir na tela.
- **`nvngx_dlssnr.dll` / RenoDX / a ponte.** As execuções rodaram **sem** eles (é a
  degradação que eu queria ver, e ela é limpa: o painel diz "NÃO INSTALADO" e o cartaz
  aparece uma vez). Então **nada do caminho com o neural vivo foi exercitado**: `PokeReShade`
  de verdade, `NeuralPeloLog`, a correção de "neural saiu invertido" e o A2 acima estão
  todos sem observação em runtime.
- **`dlss5-feed.addon64` é de terceiros** e não foi revisado; o `DLSS5_Feed.fx` e os
  `lumenite_*` também não.
- **`qr.h` sai limpo desta revisão.** Conferi as tabelas contra a norma, os laços de
  `addEccAndInterleave` (o consumo de `data` fecha exatamente com `dataCodewords`), o
  `penalty` e o `ToUtf8` com pares substitutos. Não achei leitura fora de faixa nem caminho
  de dado vazio que quebre.
- **`DLSS5_Grade.fx` e `DLSS5_Sharpen.fx` saem limpos.** O ponto que eu esperava achar — o
  atlas de LUT sangrando entre fatias e entre linhas com filtro linear — está resolvido pelo
  `+0.5` de centro de texel nos dois eixos; a única amostra que passa do atlas
  (`uv.x + tile` na última fatia) só acontece com `frac == 0`, onde o `lerp` a descarta. No
  Sharpen, a divisão por luma tem transição para aditivo abaixo de 0,05 e não gera NaN.
- **O `main.cpp` mudou durante a revisão** (4858 → 5024 linhas). Reconferi cada achado
  contra o arquivo às 02:37 de 03/09, mas se outro agente mexer nos mesmos trechos, use o
  código citado e não a linha.

---

**Atualização (03/09, 03:40) — M8:** a sugestão de `SetWorldTransform` foi tentada e descartada (`BitBlt` e as
regiões de recorte ignoram a transformada de mundo; ver `docs/painel.md` §14.9). O M8 foi resolvido com escala
explícita (`Esc(v, dpi)` no `Layout`, fontes por `MulDiv`, `WM_DPICHANGED`), medido nos dois monitores reais
(125% e 150%). No caminho, o alpha de repouso da splash (250/255, pré-existente) passou a 255.
