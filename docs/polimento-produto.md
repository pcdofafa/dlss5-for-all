# Polimento de produto — DLSS5 For All

Revisão crítica com a cabeça de quem **baixa o zip numa máquina que não é esta** e abre o app
sem ter acompanhado nada. Foco em produto e uso: o que confunde, trava ou frustra.

Método: leitura de `README.md`, `setup-dlss5.ps1`, `setup-dlss5.auto.ps1`, `ofertas.ini`,
`main.cpp`, `splash_ofertas.h`; execução real do app numa **pasta limpa** (só `DLSS5ForAll.exe`
+ `ofertas.ini`), captura da splash, do seletor, do painel (`--panel-shot`) e do cartaz
(`--guia-shot`), e uma segunda execução com o app já aberto para ver a disputa de atalhos.

Versão auditada: `main.cpp` de 03/09 02:24, `DLSS5ForAll.exe` de 03/09 00:48.

---

## Resumo

| # | Severidade | Onde | Uma linha |
|---|---|---|---|
| 1 | CRÍTICO | `setup-dlss5.ps1:216` / `.auto.ps1:216` | O setup instala o overlay do ReShade **no End**, a mesma tecla do painel do app |
| 2 | CRÍTICO | `main.cpp:3392-3400`, `2755`, `1798`, `4830` | Quando o Windows recusa F6/Alt+Esc, o app **anuncia as teclas assim mesmo** |
| 3 | CRÍTICO | `main.cpp:4859-4864` + `2740` | Toast "ficou pesado" manda desligar o filtro num botão que está **desabilitado** nesse modo |
| 4 | CRÍTICO | `FafaDLSS5Cam.zip` | O zip publicado é a versão 1.0: nome de exe antigo, sem ponte, sem shaders, sem `ofertas.ini` |
| 5 | CRÍTICO | `main.cpp:2296-2320` | Sem o ReShade carregado, o neural fica **"LIGADO · INICIANDO" para sempre**, sem explicar |
| 6 | ALTO | `main.cpp:1666` | Caixa do V-Sync cita um atalho que não existe: `Ctrl+Alt+D` |
| 7 | ALTO | `README.md:57`, `61`, `70` | README contradiz o painel em três pontos (F6, rótulo do HDR, `Alt+F8`) |
| 8 | ALTO | `main.cpp:1798`, seletor | O rodapé de atalhos **vaza para fora da janela**, cortado pela borda |
| 9 | ALTO | `main.cpp:1409-1428`, hint | A dica do seletor tem 6 linhas num campo que cabe 5: a última some sempre |
| 10 | ALTO | `main.cpp:1436` | Escolher "Placa de captura" sem placa deixa INICIAR cinza **sem dizer por quê** |
| 11 | ALTO | `main.cpp:2717-2727` | O card do neural mistura estado e ação: diz "LIGADO" e o botão diz "ligar" |
| 12 | ALTO | `main.cpp:3487` | O guia afirma que existe ícone na bandeja — não existe no modo janela separada |
| 13 | ALTO | seletor inteiro | Toda a primeira janela do app está **sem acentuação**, ao contrário do painel |
| 14 | ALTO | `main.cpp:4836`, `4860` | Os cartazes de erro estão em CAIXA ALTA sem acento ("NAO ESTA INSTALADO") |
| 15 | ALTO | `main.cpp:1291-1322` | Só o painel tem tecla reserva; F6 e Alt+Esc, se ocupados, ficam **sem tecla nenhuma** |
| 16 | ALTO | `CarregarAtalhos`, `AT_SAIR` | `Alt+Esc` é atalho do Windows: enquanto o app roda, o sistema perde a função |
| 17 | ALTO | pasta / zip | Não há `LICENSE`; o README declara MIT sem arquivo, e há 2 exes e 6 logs na pasta |
| 18 | MÉDIO | `main.cpp:1790` (`W=660`) | O seletor não escala com o DPI: a 125% já sai 20% menor que uma janela normal |
| 19 | MÉDIO | `splash_ofertas.h:750` | A splash não tem título nem marca do app: parece adware na primeira abertura |
| 20 | MÉDIO | `main.cpp:2915` | "Ver os atalhos" mostra o badge **End**, que é a tecla do painel, não do guia |
| 21 | MÉDIO | painel, seção ReShade | "Abrir o ReShade" × "Teclado pro ReShade" × item ATALHOS: três coisas parecidas |
| 22 | MÉDIO | `main.cpp:2884` | A linha do HDR trunca no painel ("…convertendo (automáti…") |
| 23 | MÉDIO | `setup-dlss5.ps1:112` | Se o ReShade não instalar, o script entra em laço de ENTER **sem opção de pular** |
| 24 | MÉDIO | `setup-dlss5.auto.ps1` | Arquivo de desenvolvimento com o mesmo nome-base; não pode ir no zip |
| 25 | MÉDIO | `README.md:85`, `194` | Dois caminhos quebrados: `docssupersampling.md`, `docsatencia-nr.md` |
| 26 | MÉDIO | seletor, primeira execução | Vem pré-selecionada a primeira janela da lista (aqui: um terminal) |
| 27 | BAIXO | `README.md:37` | "Atalhos (só dois)" numa tabela de três |
| 28 | BAIXO | splash, `ofertas.ini`, painel | A marca é "PC do Fafa", sempre sem acento; splash, `ofertas.ini` e painel escreviam com acento |
| 29 | BAIXO | fullscreen exclusivo | Está no README, mas na tela não há nenhuma mensagem quando acontece |
| 30 | BAIXO | `main.cpp:4384`, `4699` | `DLSS5_TESTA_QUEDA` / `DLSS5_TESTA_AVISO` continuam ativos no build de release |

---

## CRÍTICOS

### 1. O setup instala o overlay do ReShade na mesma tecla do painel

**Onde:** `setup-dlss5.ps1:216` e `setup-dlss5.auto.ps1:216`

```powershell
Set-IniValue $ini 'INPUT'   'KeyOverlay'          '35,0,0,0'      # End
```

**O problema:** `35` é `VK_END`. O app usa **End** para o painel dele e ensina **Home** para o
ReShade — no guia, no painel, no README e no `ReShade.ini` desta máquina (`KeyOverlay=36`, que
foi corrigido à mão aqui e **não** é o que o script grava). Numa instalação nova, apertar End
abre o painel do app *e* o overlay do ReShade ao mesmo tempo, um por cima do outro. O próprio
app já sabe disso e só escreve no log (`main.cpp:4278`):
`ATENCAO: End abre o painel E o overlay do ReShade - mude uma das duas`.

Pior: a linha 246 do mesmo script **ensina o errado** no fim da instalação
(`End -> overlay do ReShade`), contradizendo tudo o mais.

**Correção:**

```powershell
Set-IniValue $ini 'INPUT'   'KeyOverlay'          '36,0,0,0'      # Home (End e do painel do app)
```

e a linha 246:

```powershell
Write-Host '  End   -> painel do app (filtro, cor, nitidez, fps)'
Write-Host '  Home  -> overlay do ReShade. Aba Home: "LUMENITE: Kernel 2.0" ligado ACIMA de "DLSS 5 Feed" ligado.'
```

---

### 2. O app anuncia teclas que o Windows recusou

**Onde:** `main.cpp:3392-3400` (guia), `2755` (badge do card Filtro), `1798` (seletor),
`4830` (título da janela), `2053` (menu da bandeja).

**Reprodução (feita):** abri o app numa pasta limpa com uma instância já rodando. Log:

```
atalho painel = Insert
atalho tudo = F6  (RECUSADO - ja em uso)
atalho sair = Alt+Esc  (RECUSADO - ja em uso)
```

O painel caiu na reserva (Insert) e **avisa corretamente**. F6 e Alt+Esc foram recusados e
mesmo assim aparecem: o cartaz de boas-vindas ensina *"F6 — Ligar e desligar tudo"* e
*"Alt+Esc — Sair"*, o card "Filtro DLSS 5" mostra o badge `F6`, o título da janela repete.
A pessoa aperta e não acontece nada. **No modo sobreposto — sem barra de tarefas, sem Alt+Tab —
"Alt+Esc morto" significa que a única saída visível é o ícone da bandeja.**

Não é um caso de laboratório: abrir o app duas vezes basta, e qualquer utilitário residente
com F6 global (gravadores, macro de teclado) produz o mesmo.

`TemAtalho()` (`main.cpp:1281`) já existe e testa `registrado` — só não é usado nesses lugares.
A seção ATALHOS do painel já pinta a tecla recusada de laranja (`main.cpp:3000`); o resto não.

**Correção:**

- `Lista()` do guia (`main.cpp:3392-3400`): trocar `TextoAtalho(...)` por `TemAtalho(...)` e,
  quando recusado, desenhar a linha com o badge `sem tecla` e a descrição
  `outro programa está com essa tecla — use o painel (mude em ATALHOS)`.
- `if (g_atalhos[AT_SAIR].vk)` (linha 3399) → `if (TemAtalho(AT_SAIR))`; e, quando não houver
  tecla de saída, acrescentar uma linha fixa no guia:
  `Para sair: botão "Sair do DLSS5 For All" no painel, ou o ícone na bandeja.`
- Badge do card Filtro (`2755`) e título da janela (`4830`): mesma checagem.
- Um cartaz único na abertura quando **alguma** tecla foi recusada:
  `ALGUMA TECLA JÁ ESTAVA EM USO` / `o painel abre com <tecla>; as outras estão em ATALHOS, no painel`

---

### 3. O aviso "ficou pesado" manda fazer o que não dá para fazer

**Onde:** `main.cpp:4859-4864` (texto do toast) contra `main.cpp:2740` (`const bool en = g_hud.overlay;`)

No **modo janela separada** o card "Filtro DLSS 5" do painel nasce desabilitado — o subtexto
dele é literalmente `"só no modo sobreposto"`. E o toast automático de desempenho, nesse mesmo
modo, diz:

```
O FILTRO ESTA PESADO NESTA CONFIGURACAO
abra o painel e desligue o filtro
ou baixe o teto de fps no painel (End)
```

A pessoa abre o painel, acha o item que o cartaz mandou procurar, e ele está cinza. É o pior
momento possível para uma instrução falsa: ela já está com o jogo travando.

O que **funciona** nesse modo é o F6 (`AlternarTudo`, `main.cpp:3867`) — ele desliga neural,
LUT, nitidez e grading nos dois modos — e o botão `desligar` do card Neural Rendering.

**Correção (texto pronto), `main.cpp:4859-4864`:**

```cpp
std::wstring t = L"O filtro está pesado nesta configuração\n";
t += C.overlay ? (g_hkBypass + L" tira o filtro na hora")
               : (TemAtalho(AT_TUDO) ? TextoAtalho(AT_TUDO) + L" desliga o filtro na hora"
                                     : std::wstring(L"desligue o Neural Rendering no painel"));
t += L"\nou baixe o teto de fps no painel (" + g_hkPanel + L")";
t += g_hud.camera ? L"\nsem o filtro, a imagem continua chegando normal"
                  : L"\nsem o filtro, o jogo volta ao desempenho de sempre";
```

---

### 4. O zip publicado é de outra versão do produto

**Onde:** `FafaDLSS5Cam.zip` (349 KB, 01/09)

Conteúdo real:

```
FafaDLSS5Cam.exe   1087534    <- nome antigo; hoje o exe se chama DLSS5ForAll.exe
setup-dlss5.ps1      14528
README.md             5972    <- 1/3 do README atual
main.cpp             37959
build-mingw.sh         427
build-msvc.bat         462    <- nem existe mais na pasta
```

Falta **tudo** o que o próprio README (linha 232) diz que precisa ir: `dlss5-bridge.addon64`,
`DLSS5_Grade.fx`, `DLSS5_Sharpen.fx`, `dlss5_grade_luts.png`, os `preset-0*.ini` e
`ofertas.ini`. Quem baixar isso hoje recebe um app onde LUT e nitidez ficam permanentemente
"indisponíveis", sem tela de ofertas, e um `setup-dlss5.ps1` que aborta na linha 64
(`DLSS5ForAll.exe nao esta nessa pasta`) porque o exe do zip tem outro nome.

**Correção:** regerar o zip. Sugestão de lista fechada, checada contra a pasta:

```
DLSS5ForAll.exe
dlss5-bridge.addon64
setup-dlss5.ps1
README.md
ofertas.ini
LICENSE                                  (a criar — ver item 17)
preset-01-nucleo-afinado.ini
preset-02-grade-neutra.ini
preset-03-grade-cinema.ini
preset-04-brilho-contido.ini
preset-05-nitidez.ini
reshade-shaders\Shaders\DLSS5_Grade.fx
reshade-shaders\Shaders\DLSS5_Sharpen.fx
reshade-shaders\Textures\dlss5_grade_luts.png
```

**Fora do zip, explicitamente:** `nvngx_dlssnr.dll` e `renodx-dlss5.addon64` (vazados),
`nvngx_dlss.dll` (58 MB, o script busca no driver), `dxgi.dll` (o script instala),
`setup-dlss5.auto.ps1`, `*.log` / `ReShade.log*`, `*.bak*`, `dlss5forall.ini`,
`reshade-cache\`, `marca\`, `docs\`, `addon\`, `main.cpp` e os `.h` (o README aponta o
repositório para quem quiser compilar), `DLSS5ForAll-hud.exe`, `FafaDLSS5Cam.exe.bak-1.0`.

**Atenção antes de gerar:** o `DLSS5ForAll.exe` na pasta é de **00:48** e o `main.cpp` de
**02:24**. O binário atual ainda desenha a seção "RESOLUÇÃO INTERNA" no painel — feature que o
`main.cpp` já removeu e que o README (linha 85) diz não existir. Recompilar antes de empacotar.

---

### 5. Sem o ReShade carregado, o neural fica "iniciando" para sempre

**Onde:** `main.cpp:2296-2320` (`LerEstadoNeural`) e `2263` (`NeuralNoIni`)

A função decide o estado assim:

1. `renodx-dlss5.addon64` e `nvngx_dlssnr.dll` existem? Se não → `NÃO INSTALADO`. Bom.
2. `ReShade.ini` → `[RenoDX.DLSS5] NeuralUplift`. Se o arquivo não existe, devolve `-1`
   ("não sei") e o fluxo **segue**.
3. Procura no `ReShade.log` por `evaluation succeeded`. Se o arquivo não existe, `TailArquivo`
   devolve vazio → não achou → **`NR_INICIANDO`**, com o texto
   `"ligado, esperando o primeiro frame passar pela rede"`.

Ou seja: quem copiou os dois arquivos vazados mas **não** tem o `dxgi.dll` (não rodou o setup,
ou o antivírus levou o ReShade — acontece muito com build de add-ons) vê o painel e o cartaz
dizendo "LIGADO · INICIANDO" indefinidamente, com um texto que promete que já vai. Não há
nenhum caminho na tela até a causa real.

**Correção:** duas checagens novas em `LerEstadoNeural`, antes do passo 3:

- se `GetModuleHandleW(L"dxgi.dll") == nullptr` **e** não existe `dxgi.dll` ao lado do exe →
  estado `NR_SEM_ADDON` com detalhe `o ReShade não carregou — rode o setup-dlss5.ps1`;
- limite de tempo: passados ~25 s de `NR_INICIANDO` sem sinal no log, virar aviso —
  `não subiu: confira "DLSS 5 Feed" no ReShade (Home)`.

`rsbridge::Available()` (`reshade_bridge.h`) já serve como prova de que o ReShade está vivo no
processo, e o painel já o consulta para LUT/nitidez: dá para reaproveitar sem código novo de
detecção.

---

## ALTOS

### 6. A caixa do V-Sync cita um atalho que não existe

**Onde:** `main.cpp:1666`

```
Da pra mudar em runtime pelo painel (Ctrl+Alt+D).
```

`Ctrl+Alt+D` é a **quinta** opção da cascata de reserva do painel (`kCascataPainel`,
`main.cpp:1323`) — só vale se End, Insert, Pause e ScrollLock estiverem todos ocupados. Na
prática o texto está errado para ~100% dos usuários, e é o único lugar do app que ainda cita a
nomenclatura antiga.

**Texto novo:** `Dá pra mudar depois no painel, sem fechar o app.`
(sem citar tecla — a caixa aparece **antes** de o app saber qual tecla vai valer.)

Enquanto está aqui: essa `MessageBox` é a única do app com acentuação inconsistente
("padrao", "estavel", "cadencia") e é acionada por um clique inocente numa caixa de seleção.

### 7. O README contradiz o painel em três pontos

**Onde:** `README.md:57`, `61`, `70`

- **linha 57:** `**Filtro DLSS 5** liga/desliga · **Neural Rendering** (F6)` — invertido. No
  painel o badge `F6` está no card **Filtro DLSS 5** (`main.cpp:2755`), e o card Neural
  Rendering não tem tecla. F6 é "liga/desliga **tudo**", não "liga o neural".
  → `**Neural Rendering** (o motivo do app) · **Filtro DLSS 5** liga/desliga tudo (F6) · **Teto de fps** Máx/60/45/30/20`
- **linha 61:** `**Tratar HDR automaticamente** (padrão desligado...)` — o painel diz
  **"Desligar o HDR do monitor (plano B)"** (`main.cpp:2871`) e o seletor traz o texto longo do
  módulo. O rótulo do README é o antigo, de outra semântica.
  → `**Desligar o HDR do monitor (plano B)** — opcional; a conversão HDR→SDR já é automática`
- **linha 70:** `` `Alt+F8` desliga na hora `` — `Alt+F8` é a **segunda** opção da lista
  `kBypass` (`main.cpp:4289`), só usada no modo sobreposto e só se `Alt+F9` estiver tomado.
  → `` **F6** desliga na hora e o jogo continua rodando normal por baixo ``

Mesma classe de problema em `README.md:120` e `215` (`Alt+F10` apresentado como fixo, quando é
a primeira tentativa de uma lista): trocar por *"o atalho de 'Teclado pro ReShade' que o app
mostrar no painel"*.

### 8. O rodapé de atalhos vaza para fora da janela do seletor

**Onde:** `main.cpp:1798` (texto) e `1783` (`st.hAtalhos = mk(L"STATIC", L"", 0, 302, 437, 200, 34, 0)`)

O texto montado é `End abre o painel  ·  F6 liga/desliga tudo` (~41 caracteres, Segoe UI 15px,
≈290 px) num STATIC de **200×34**. Na captura da execução real ele quebra em duas linhas e a
segunda (`liga/desliga tudo`) é **cortada pela borda inferior da janela** — sobra meia letra
visível. É a primeira janela do app.

**Correção:** ampliar o campo e encurtar o texto. O espaço entre "Atalhos..." (termina em 294) e
"INICIAR" (começa em 504) comporta 200 px; o problema é a altura e o comprimento.

```cpp
st.hAtalhos = mk(L"STATIC", L"", SS_CENTERIMAGE, 302, 428, 196, 34, 0);
...
SetWindowTextW(st.hAtalhos, (TextoAtalho(AT_PAINEL) + L" = painel   ·   " +
                             TextoAtalho(AT_TUDO)  + L" = liga/desliga").c_str());
```

(o mesmo em `main.cpp:1685-1686`, no retorno do diálogo de Atalhos.)

### 9. A dica do seletor é maior que o campo que a mostra

**Onde:** `main.cpp:1409-1414` (6 linhas), `1425-1432` (7 linhas) contra
`main.cpp:1782`: `st.hHint = mk(L"STATIC", L"", 0, 16, 318, 628, 100, 0)`

100 px de altura com Segoe UI 15 cabem 5 linhas. Na fonte **Janela** a 6ª linha —
`Nos dois casos o OBS grava a janela (Captura de Janela). So janela / borderless.` — nunca
aparece; e é justamente a que responde "posso gravar isso?" e "por que não funciona no meu
jogo?". Na fonte **Monitor** somem as duas últimas, incluindo o aviso de latência da tela
inteira.

**Correção:** `st.hHint` com altura 110 e os textos cortados para 5 linhas, com a informação que
some promovida para dentro das linhas que ficam. Ex. para a fonte Janela:

```
SOBREPOR desmarcado = JANELA SEPARADA: o jogo aparece dentro de uma janela do app, igual
à imagem de uma placa de captura. Move, redimensiona, F11 põe em tela cheia.
SOBREPOR marcado = a MESMA janela, sem borda, colada em cima do jogo; mouse e teclado
passam direto pra ele — parece filtro dentro do jogo.
Nos dois casos o OBS grava a nossa janela (Captura de Janela). Só janela / borderless.
```

### 10. "Placa de captura" sem placa: INICIAR fica cinza e ninguém diz por quê

**Onde:** `main.cpp:1436`

```cpp
if (di < 0 || di >= (int)s->devs->size()) { EnableWindow(s->hStart, FALSE); return; }
```

O `return` sai **antes** de qualquer `SetWindowTextW(s->hHint, ...)`. Resultado: quem tem só
teclado e monitor escolhe "Placa de captura / câmera / console", o combo fica vazio, o botão
INICIAR apaga, e a dica na tela continua sendo a **do modo anterior** (falando de SOBREPOR e de
janela). O app parece travado.

**Correção:** antes do `return`,

```cpp
SetWindowTextW(s->hHint,
    L"Nenhuma placa de captura ou câmera foi encontrada neste PC.\n"
    L"Se ela está ligada agora, clique em \"Atualizar lista\".\n"
    L"Para filtrar um jogo que roda nesta máquina, troque a Fonte para \"Janela\" ali em cima.");
```

### 11. O card do neural mistura estado e ação

**Onde:** `main.cpp:2717-2727`

O card grande mostra o estado (`ATIVO` / `LIGADO · INICIANDO` / `DESLIGADO` / `NÃO INSTALADO`)
e, ao lado, um botão cujo rótulo é a **ação** (`ligar` / `desligar`). O card logo abaixo,
"Filtro DLSS 5", faz o oposto: o botão mostra o **estado** (`LIGADO` / `DESLIGADO`). Na captura
do painel os dois aparecem juntos: *"LIGADO · INICIANDO"* com um botão escrito *"ligar"*
— quem lê conclui que não está ligado.

**Correção (mínima, sem mexer no layout):** dar ao botão do neural a mesma gramática visual do
chip de estado, escrevendo a ação no infinitivo e o alvo:

```cpp
Txt(g, ok || g_hud.neural == NR_INICIANDO ? L"desligar" : L"ligar", ...);
```

com o estado `LIGADO · INICIANDO` encurtado para `INICIANDO` (o "LIGADO" já é redundante com o
botão `desligar` ao lado) e o subtexto assumindo o papel de dizer o que falta:
`ligado — esperando o primeiro frame passar pela rede`.

### 12. O guia promete um ícone na bandeja que não existe

**Onde:** `main.cpp:3487`

```cpp
Txt(g, L"o ícone na bandeja, ao lado do relógio, tem os mesmos comandos", ...)
```

O ícone só é criado dentro de `if (C.overlay)` (`main.cpp:4309`: `trayWnd = MakeTrayWindow(hi)`).
No **modo janela separada** — que é o modo recomendado pelo próprio README para gravar no OBS e
para o Lossless Scaling — não há bandeja. O cartaz da primeira execução manda a pessoa procurar
uma coisa que não está lá.

**Correção:** condicionar a linha e trocar o texto no outro caso:

```cpp
Txt(g, g_hud.overlay ? L"o ícone na bandeja, ao lado do relógio, tem os mesmos comandos"
                     : L"a barra de título mostra a fonte, o tamanho e os fps · F11 põe em tela cheia", ...)
```

### 13. A primeira janela do app está inteira sem acentuação

**Onde:** todo o seletor — `main.cpp:1398-1453`, `1656-1680`, `1770-1800`

O painel e o cartaz-guia são acentuados e bem escritos ("é por aqui que se faz tudo", "não rasga
a imagem, só não espera"). O seletor, que vem **antes** e é o primeiro contato, está em ASCII:

- título da janela: `escolha a fonte de video`
- `Area util da janela (sem barra de titulo / bordas)`
- `Modo (resolucao / fps / formato)`
- `Placa de captura / camera / console`
- `Esconder esta janela de gravadores/capturas (obrigatorio no monitor sobreposto)`
- `O jogo continua visivel do lado, do jeito que sempre foi.`
- `Atencao: a tela inteira ganha a latencia do filtro`
- `Esse dispositivo nao expos nenhum modo de video`

Não há razão técnica: são `wchar_t*` compilados como UTF-16, os mesmos do painel. Para um
produto pago em atenção (o app pede que a pessoa entre num grupo), a primeira janela em ASCII
lê como beta interno.

**Decisão recomendada: padronizar tudo com acento.** Só o `setup-dlss5.ps1` fica sem — ali é
console e o codepage 850/437 estraga de verdade.

### 14. Os cartazes de erro estão em caixa alta sem acento

**Onde:** `main.cpp:4836-4839` e `4860-4866`

```
NEURAL RENDERING NAO ESTA INSTALADO
O NEURAL RENDERING ESTA DESLIGADO
e ele e o motivo deste app existir - ligue no painel (End)
O FILTRO ESTA PESADO NESTA CONFIGURACAO
```

São os textos que a pessoa lê **exatamente quando algo deu errado** e o único momento em que o
app grita. Caixa alta sem acento tem cara de erro de sistema, não de recado.

**Textos novos:**

```
Neural Rendering não está instalado
falta o renodx-dlss5.addon64 ao lado do app

O Neural Rendering está desligado
é ele o motivo deste app existir — ligue no painel (End)

O filtro está pesado nesta configuração
F6 desliga o filtro na hora
ou baixe o teto de fps no painel (End)
sem o filtro, o jogo volta ao desempenho de sempre
```

### 15. Só o painel tem tecla reserva

**Onde:** `main.cpp:1291-1298` (padrões), `1323-1326` (`kCascataPainel`), `1336-1348`

O comentário do código assume a escolha ("As outras acoes, se a tecla estiver ocupada, ficam sem
tecla - e o painel continua fazendo tudo pelo mouse"), e para LUT/nitidez isso é verdade. Mas
`AT_TUDO` é o **antes-e-depois**, a demonstração que vende o app, e `AT_SAIR` é a saída de um
modo sem barra de tarefas. Perder essas duas em silêncio é caro demais.

**Correção:** dar cascata às duas, como já existe para o painel:

```cpp
static const struct { UINT mods, vk; } kCascataTudo[] = {
    { 0, VK_F6 }, { 0, VK_F7 }, { MOD_ALT, VK_F6 }, { MOD_CONTROL | MOD_ALT, 'T' },
};
static const struct { UINT mods, vk; } kCascataSair[] = {
    { MOD_ALT, VK_ESCAPE }, { MOD_CONTROL | MOD_ALT, VK_F12 }, { MOD_CONTROL | MOD_ALT, 'Q' },
};
```

Combinado com o item 2 (não anunciar o que não registrou), fecha o buraco.

### 16. `Alt+Esc` sequestra um atalho do Windows

**Onde:** `main.cpp:1296` — `{ AT_SAIR, MOD_ALT, VK_ESCAPE }`

`Alt+Esc` é atalho do sistema: alterna para a próxima janela sem o painel do Alt+Tab. Enquanto o
DLSS5 For All estiver aberto — e ele fica aberto o tempo todo, por design — **o Windows inteiro
perde essa função**, inclusive fora do jogo. Quem usa Alt+Esc vai atribuir a quebra ao app sem
saber por quê; e vai fechar o app sem querer ao tentar alternar de janela (duas vezes em 2 s é
exatamente o gesto de quem alterna rápido).

**Correção:** padrão `Ctrl+Alt+Q` (já é reserva em `kQuit`, `main.cpp:4300`), com `Alt+Esc`
disponível na lista de quem quiser. Se a decisão for manter, o guia precisa dizer
`Alt+Esc (enquanto o app roda, o Windows não usa mais essa tecla)`.

### 17. Sem licença, com dois exes e seis logs na pasta

**Onde:** raiz do projeto

`README.md:237` afirma "O app é MIT" e não existe nenhum `LICENSE` na pasta. Quem baixar não tem
o texto da licença, e a atribuição dos terceiros (ReShade, DLSS5-Feeder, LumeniteFX, RenoDX)
está só como frase corrida no fim do README.

Além disso, a pasta que vira o zip contém hoje: `DLSS5ForAll.exe` **e** `DLSS5ForAll-hud.exe`
(qual a pessoa abre?), `FafaDLSS5Cam.exe.bak-1.0`, `ReShade.log` até `ReShade.log5`,
`ReShade.ini.bak-*` (2), `dlss5forall.log`, `dlss5-feed.log`, `reshade-cache\`, `marca\`,
`docs\`, `addon\` e o `nvngx_dlssnr.dll` de **165 MB** que não pode ser redistribuído.

**Correção:** criar `LICENSE` (MIT, com o ano e o autor) e um `CREDITOS.md` curto:

```
DLSS5 For All é MIT. Ele não contém código da NVIDIA.
O que ele monta ao lado dele tem cada um sua licença:
  ReShade (crosire)            — https://reshade.me         · BSD 3-clause
  DLSS5-Feeder (jlrouzies-fr)  — repositório do autor
  LumeniteFX (umar-afzaal)     — repositório do autor
  renodx-dlss5 + nvngx_dlssnr.dll — build interno da NVIDIA, não redistribuído aqui.
  Você traz esses dois; nós não hospedamos, não baixamos e não damos suporte a eles.
```

---

## MÉDIOS

### 18. O seletor não escala com o DPI

`main.cpp:1790` fixa `const int W = 660, H = 492` e todos os controles em pixels, com
`CreateFontW(-15, ...)`. O processo é `PER_MONITOR_AWARE_V2` (`main.cpp:3941`), então **nada é
escalado pelo Windows**. O painel trata isso (`main.cpp:2498-2507`, `g_scale = dpi / 96`); o
seletor não.

Medido nesta máquina (1920×1080 a 125%): a janela do seletor ocupa 533×422 pontos lógicos em vez
dos 660×492 esperados — 20% menor que uma janela normal do Windows, com fonte proporcionalmente
miúda. Num 4K a 200% cai para metade. Não trunca (fonte e caixas encolhem juntas), mas fica
visivelmente "de outra época" ao lado do painel, que é bonito e escala certo.

**Correção:** aplicar `MulDiv(v, GetDpiForWindow(h), 96)` na criação (ou `-15 * dpi / 96` na
fonte e um multiplicador único nas coordenadas do lambda `mk`).

### 19. A primeira tela do app é uma propaganda sem nome

`splash_ofertas.h:749-751`: a janela nasce com `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED`
e **título vazio**. `WS_EX_TOOLWINDOW` a tira da barra de tarefas e do Alt+Tab.

Quem dá duplo clique em `DLSS5ForAll.exe` vê, como primeiro e único elemento na tela, um cartão
topmost com QR e o botão "QUERO AS OFERTAS", sem nenhuma marca do app, sem entrada na barra de
tarefas. É exatamente a assinatura de um adware. A decisão de mostrar o convite antes do seletor
é legítima e está bem executada (fecha por "Agora não", X ou Esc, sem contagem — item 91 do
README); o que falta é a assinatura do app.

**Correção:** título de janela `DLSS5 For All` e uma linha discreta no rodapé do cartão, à
esquerda do "Agora não":

```
DLSS5 For All 1.1 · abrindo…
```

### 20. "Ver os atalhos" mostra a tecla do painel

`main.cpp:2915` desenha `g_hkPanel` (End) no badge do card **Ver os atalhos**. Na leitura do
painel, "Ver os atalhos … End" diz que End abre o guia — e End abre/fecha o **painel**.

**Correção:** trocar o badge por `abrir` (como um botão), ou remover o badge desse card. O guia
não tem tecla própria e não precisa ter.

### 21. Três itens diferentes com "ReShade" no nome

No painel convivem: o card **"Abrir o ReShade"** (`main.cpp:2921`, badge `Home`), o botão
**"Teclado pro ReShade"** (`main.cpp:3036`) e a linha **"Teclado pro ReShade"** na seção ATALHOS
(rótulo em `g_atalhos[AT_INTERATIVO].rotulo`). Um abre o overlay; os outros dois são a mesma
ação (soltar o click-through), uma como botão e outra como configuração de tecla.

**Correção de nomenclatura:**

| Hoje | Proposto |
|---|---|
| Abrir o ReShade | Abrir o ReShade (presets e shaders) |
| Teclado pro ReShade | Deixar o mouse clicar no filtro |
| ATALHOS ▸ Teclado pro ReShade | ATALHOS ▸ Mouse no filtro / no jogo |

### 22. A linha do HDR trunca no painel

`main.cpp:2884-2897`: o subtexto do switch é desenhado em `r.Width - 96` ≈ 378 px. Na captura o
texto sai como `monitor em HDR · capturando em 16 bits e convertendo (automáti…`. É a única
informação que explica por que a imagem parece diferente.

**Correção:** encurtar a versão de painel para `monitor em HDR · convertendo automaticamente` e
deixar o texto longo só no seletor (que tem 628 px) — o próprio código já reconhece essa
diferença no comentário da linha 2869, mas só para o rótulo, não para o subtexto.

### 23. O setup entra em laço sem saída se o ReShade não instalar

`setup-dlss5.ps1:112`:

```powershell
while (-not (Test-Path $dxgi)) { Read-Host '   Quando o instalador terminar (dxgi.dll ao lado do exe), aperta ENTER' | Out-Null }
```

Sem `dxgi.dll`, o único jeito de sair é fechar a janela. A etapa 6 (add-on neural), logo abaixo,
tem `"pular"` — a etapa 1 não. E é a mais provável de falhar: o instalador do ReShade é baixado
de `reshade.me` por scraping de HTML (`linha 87`), e a build com add-ons costuma ser barrada
por antivírus.

**Correção:** mesma saída da etapa 6.

```powershell
while (-not (Test-Path $dxgi)) {
    $r = Read-Host '   ENTER quando o dxgi.dll estiver ao lado do exe (ou "pular" pra continuar sem ReShade)'
    if ($r -eq 'pular') { Warn 'sem ReShade nao ha filtro nenhum: o app so mostra a imagem crua. Rode este script de novo depois.'; break }
}
```

### 24. `setup-dlss5.auto.ps1` não pode sair de casa

É `setup-dlss5.ps1` com os `Read-Host` comentados como `# [auto]` — ferramenta de teste. Se for
no zip, alguém vai rodar o arquivo errado (os nomes são quase iguais e o Explorer ordena os dois
juntos) e ficar com uma instalação sem ReShade **sem nenhum aviso**, porque o modo auto troca o
laço de espera por um `Warn` e segue.

### 25. Dois caminhos quebrados no README

`README.md:85` — ``(`docssupersampling.md`)`` → ``(`docs\supersampling.md`)``
`README.md:194` — ``medicoes em `docsatencia-nr.md` `` → ``medições em `docs\latencia-nr.md` ``

(As barras invertidas foram comidas em algum processamento; o parágrafo de latência,
`README.md:191-200`, é também o único trecho longo do README **sem acentos** — "Latencia",
"irredutivel", "medicoes", "poe", "confere". Vale uma passada.)

### 26. O seletor já vem apontando para uma janela qualquer

`FillTargets` seleciona `sel = 0` quando não há escolha salva (`main.cpp:1465-1479`), e
`EnumCaptureWindows` devolve na ordem do Z-order. Na execução real em pasta limpa, o campo veio
preenchido com `Módulo de captura de janela com filtro DLSS [WindowsTerminal.exe] 962x1151` — um
terminal. Quem clicar direto em INICIAR captura a janela errada e conclui que o app não
funciona.

**Correção:** na primeira execução (`s->primeiraVez`, campo que já existe em `SelState`), não
pré-selecionar nada: `CB_SETCURSEL, -1`, com o botão INICIAR desabilitado e o hint dizendo
`Escolha ali em cima a janela do jogo que você quer filtrar.` Alternativa mais barata: manter a
seleção, mas empurrar para o fim da lista os processos com `Windows.UI.`/`ApplicationFrameHost`/
`explorer`/terminais.

---

## BAIXOS

### 27. "Atalhos (só dois)" com três na tabela
`README.md:37` — a tabela logo abaixo tem End, F6 e Alt+Esc. → `### Atalhos`

### 28. Marca escrita com acento fora do menu da bandeja
A marca é "PC do Fafa" / "FAFA", sempre sem acento. O menu da bandeja
(`main.cpp:2098`, `L"Grupo de ofertas - PC do Fafa"`) já está certo; quem escrevia com acento
era a splash, o `ofertas.ini` e o painel (`main.cpp`, seção de ofertas). Os `ofertas.ini`
foram corrigidos em 11/09/2026; faltam a splash (`splash_ofertas.h`), o painel (`main.cpp`),
o `CompanyName` do `app.rc` e do `portable-launcher/launcher.rc`. A unificação que resta é só
a do travessão (`—` do painel contra `-` da bandeja).
→ `L"Grupo de ofertas — PC do Fafa"`

### 29. Fullscreen exclusivo não tem resposta na tela
É a limitação nº 1 do README (`linha 204`), mas no app o sintoma é só `0.0 fps (alvo parado)` no
título da janela (`main.cpp:4875`). Quem não leu o README não tem como chegar na causa.
**Sugestão:** quando a fonte for `SRC_WINDOW`, o alvo estiver vivo e o WGC não entregar nenhum
frame por 5 s, um toast: `O jogo não está entregando imagem` / `se ele estiver em tela cheia
exclusiva, mude para "janela" ou "sem bordas" nas opções do jogo`.

### 30. Variáveis de teste ativas no build de release
`DLSS5_TESTA_QUEDA` (`main.cpp:4384`, derruba a captura de propósito) e `DLSS5_TESTA_AVISO`
(`main.cpp:4802`, dispara o cartaz de "pesado") são lidas do ambiente sem nenhuma guarda de
compilação. São inofensivas por serem obscuras, mas aparecem no `dlss5forall.log` do usuário
(`DLSS5_TESTA_QUEDA: derrubando a sessao de proposito aos 12 s` está no log **desta** pasta) e
vão confundir qualquer suporte que leia o log.
**Sugestão:** `#ifdef DLSS5_DEV` em volta das duas, ou pelo menos não logar quando desativadas.

---

## O que checar antes de publicar (checklist)

1. Recompilar o `DLSS5ForAll.exe` a partir do `main.cpp` atual (o binário na pasta ainda tem a
   "Resolução interna" que o README diz não existir).
2. Corrigir `KeyOverlay` para `36,0,0,0` nos dois setups e a linha 246.
3. Rodar `setup-dlss5.ps1` numa **VM limpa**, sem NVIDIA App, sem Steam, com Windows em inglês —
   é o único jeito de saber se o scraping de `reshade.me` e a busca do `nvngx_dlss.dll` sobrevivem.
4. Abrir o app **duas vezes** e confirmar que a segunda instância não anuncia teclas mortas.
5. Regerar o zip com a lista do item 4 e conferir o tamanho (deve ficar em poucos MB, não em 200).
6. Criar `LICENSE`.
