# Robustez com fonte Janela: resolução trocada em voo e janela-alvo fechada

**Data:** 11/09/2026 · **Fonte:** `amd\main.cpp` · **Cópia de teste:** `DLSS5ForAll-AMD\bin-teste-robustez\`
(exe `DLSS5ForAll-rob.exe`, compilado de `amd\` com `SKIP_ADDON=1 bash build-mingw.sh DLSS5ForAll-rob.exe`)

Dois defeitos relatados, os dois com **Fonte: Janela**:

1. *"Se eu executei um jogo no modo janela e eu troco a resolução do jogo, a sobreposição continua
   na resolução antiga. O programa deve ser capaz de reiniciar e se adaptar à resolução."*
2. *"Se eu fecho a tela do jogo, aquela bandeja que acessamos apertando End continua ativa. O
   programa deveria voltar à tela anterior uma vez que a janela foi fechada."*

---

## 1. O que o programa fazia antes (reproduzido, não deduzido)

Bancada: uma janela WinForms redimensionável por código (`ALVO-ROBUSTEZ`, repinta 10×/s e desenha o
próprio tamanho), o app capturando essa janela, e um script que a redimensiona ou fecha de fora.
Os scripts ficam no scratchpad da sessão (`alvo.ps1`, `janela.ps1`, `tecla.ps1`, `show.ps1`).

### PARTE A — resolução trocada em voo

```
[01:03:53.733] fonte mudou de 1600x900 para 1200x901 - backbuffer segue em 1600x900 (sem recriar a cadeia)
[01:03:53.733] fonte mudou: a apresentacao deixou de ser 1:1 - o PS passa a AMPLIAR a imagem ...
[01:03:58.075] fonte mudou de 1201x1175 para 1280x1175 - backbuffer segue em 1600x900 (sem recriar a cadeia)
```

E era só isso, para sempre: `Renderer::FonteMudou` (`main.cpp`, hoje em `:1290`) atualiza `srcW/srcH`,
liga a reamostragem no PS e **não toca no backbuffer**, porque `ResizeBuffers` com a cadeia NGX viva
mata o processo — a regra está escrita ali e é citada em `docs\ARQUITETURA.md §12`. O único caminho de
volta ao 1:1 era um cartaz de 9 s pedindo para a pessoa abrir o painel e clicar em "Reabrir o app".
Quem não viu o cartaz ficava com a imagem borrada e o filtro na resolução velha até fechar o app na mão.

### PARTE B — janela-alvo fechada

```
[01:04:29.145] captura caiu: Closed do WGC=1, janela ainda viva=0
[01:04:29.145] A janela capturada foi fechada
[01:04:29.145] ESTADO NO MOMENTO EM QUE A FONTE CAIU: ...
```

Depois dessas três linhas, nada. O processo continuava de pé com o último quadro congelado, a barra
de título vazia, e **o painel continuava abrindo**: no teste ele foi aberto 48 s depois de a janela
morrer (`[01:06:36.037] painel aberto em 2750,270 600x1004 visivel=1`), mostrando fps, resolução e
botões de uma janela que não existia mais. Nenhum ponto do código fechava o painel nesse caso.

---

## 2. O que foi corrigido

A saída dos dois casos já existia, escrita e testada: o botão **"Reabrir o app"** do painel
(`CMD_REABRIR`). O que faltava era o app percorrê-la sozinho. Nenhum caminho novo foi inventado —
o que se fez foi tirar o clique do meio e tapar dois buracos que o botão já tinha.

### Peças novas (`amd\main.cpp`)

| Onde | O quê |
|---|---|
| `:9734` | `kMaxReaberturas = 3` — reaberturas automáticas **encadeadas** antes de desistir e só avisar. |
| `:9739` `ArgsDeReabertura` | Monta a linha da cópia nova a partir da **escolha da sessão** (HWND do alvo + overlay/cursor/recorte/vsync/HDR/pulo/placa), não da linha de comando de quando o app abriu. `manterAlvo=false` devolve uma linha **sem fonte**, e aí a cópia nova abre o seletor. |
| `:9784` `ReabrirApp` | Fecha painel/guia/efeitos, lança a cópia nova, e só então deixa o chamador encerrar. Devolve `false` quando o lançamento falha — e aí ninguém encerra nada. |
| `:9812` (topo do `wWinMain`) | `--esperar-pid N`: a cópia nova espera a velha morrer **antes** do mutex de instância única. Sem isto a cópia nova batia em "já existe outra cópia aberta" e saía — a corrida já existia no botão "Reabrir" do painel e deixava a pessoa sem app nenhum. |
| `:9653` | `--reabrindo` entra em `LinhaDeComandoIgnoraInstancia` — rede embaixo, caso a espera de 20 s estoure. |
| `:10235` | `--reabrindo N` / `--esperar-pid N` no parser de argumentos. |
| `:10351` | Reabertura com o alvo **já morto**: cai no seletor em vez do `Fatal("--window-hwnd: a janela nao existe mais")`. |
| `:10376` | O título do alvo passa a vir de `GetWindowTextW` numa reabertura (antes ficava cravado "DLSS5 For All", que só está certo para uma camada de cima), e `g_camada2Pai` só é preenchido quando a instância é mesmo camada — senão o `FollowTarget` e a ordem Z tratariam o **jogo** como janela nossa. |
| `:10441` | A tela do grupo (ofertas) não aparece numa reabertura automática. |
| `:11415` | `reabrirAuto` = não é camada, `[ultima] reabrir_auto` ≠ `0` no ini, e geração < 3. Sai no log na abertura. |
| `:11556` | `CMD_REABRIR` (o botão do painel) passou a usar o mesmo `ReabrirApp`/`ArgsDeReabertura` — ganhou de graça o `--esperar-pid` e o alvo pelo HWND. |
| `:12780` | Ao detectar mudança de tamanho, arma/rearma o relógio `tamanhoMudouEm`. |
| `:12802` | O cartaz muda de texto quando a reabertura automática está ligada: em vez de mandar clicar num botão, diz que o app vai se reabrir sozinho. |
| `:12969` | **O bloco central**, no laço principal, conferido de 250 em 250 ms. |

### O bloco central, em uma frase cada

- **Alvo destruído** (`C.kind == SRC_WINDOW && C.win && !IsWindow(C.win)`): fecha painel, guia e
  efeitos, e reabre **sem fonte** — a cópia nova cai no seletor, que é a tela anterior de quem abriu o
  app na mão. O teste é `IsWindow`, e **não** o `!scap.Alive()` que já existia logo acima: aquele
  também é falso com a janela apenas minimizada (a sessão do WGC cai), e reabrir o app porque alguém
  minimizou o jogo seria pior que o defeito. Fonte Monitor não entra: monitor não fecha.
- **Tamanho mudou e parou de mudar**: espera 1500 ms de tamanho estável (arrastar a borda produz
  dezenas de tamanhos por segundo; reabrir no primeiro seria reabrir no meio do arrasto) e então
  reabre **com o mesmo alvo**, na resolução nova.
- **Com o conjunto desligado (F6) ou em bypass, não reabre**: quem desligou, desligou de propósito, e
  reabrir faria o app nascer ligado de novo. O relógio fica armado e a reabertura acontece no instante
  em que a pessoa religar — que é justamente quando ela quer a imagem de volta e certa.
- **Cota**: 3 reaberturas automáticas encadeadas. A contagem **se renova** quando a sessão durou mais
  de 60 s (mesma ideia da cota de retomadas da captura: "captura estavel de novo"), então trocar de
  resolução cinco vezes numa noite nunca esgota nada — o teto existe só contra o pisca-pisca.

Fonte Monitor é atendida de passagem: `ArgsDeReabertura` sabe reabrir por `--source monitor --monitor N`,
e câmera/placa por `--device/--mode` (antes, clicar em "Reabrir" com fonte câmera caía no seletor).

---

## 3. Prova

Todos os tempos vêm do `dlss5forall.log` da cópia de teste. A resolução aparece em pixels **físicos**
(a janela de teste não é DPI-aware e o monitor está a 125%: 1024 lógicos = 1280 físicos).

### PARTE A — modo sobreposto (`--overlay`)

```
[01:28:50.454] fonte mudou de 1600x1175 para 1280x1175 - backbuffer segue em 1600x1175 (sem recriar a cadeia)
[01:28:52.022] o alvo esta estavel em 1280x1175 ha 1578 ms e o backbuffer nasceu 1600x1175 -
               reabrindo o app na resolucao nova (geracao 1; esta sessao durou 12 s)
[01:28:52.022] reabrindo o app (o alvo mudou de tamanho): ...\DLSS5ForAll-rob.exe --reabrindo 1
               --esperar-pid 23936 --source window --window-hwnd 199496 --overlay --no-hide-self
               --no-vsync --no-hdr --placa "NVIDIA GeForce RTX 4080"
[01:28:53.455] DLSS5 For All 1.1 iniciando
[01:28:53.473] Fonte: janela 'ALVO-ROBUSTEZ' 1280x1175 (recorte=area util cursor=0 esconder=0)
[01:28:54.051] apresentacao (na abertura): 1:1 exato - backbuffer 1280x1175 = area cliente, sem reamostragem
[01:28:57.215] ritmo: a cadeia entregou apos 3141 ms - present ocioso passa a 500 ms
```

**Tempo de adaptação: 3,6 s até a imagem voltar a 1:1 na resolução nova; 6,8 s até a cadeia neural
estar entregando de novo.** Desses 3,6 s, 1,6 s são a espera proposital de estabilidade.

Três resoluções seguidas, inclusive proporções diferentes (1600×900 → 1375×1175 → 1125×1175 →
1475×1175), cada uma acompanhada sozinha, sem ninguém fechar e reabrir o app:

```
log.3  [01:23:15.921] o alvo esta estavel em 1375x1175 ... (geracao 1; esta sessao durou 10 s)
log.2  [01:23:17.922] apresentacao (na abertura): 1:1 exato - backbuffer 1375x1175 = area cliente
log.2  [01:23:30.485] o alvo esta estavel em 1125x1175 ... (geracao 2; esta sessao durou 12 s)
log.1  [01:23:32.524] apresentacao (na abertura): 1:1 exato - backbuffer 1125x1175 = area cliente
log.1  [01:23:44.806] o alvo esta estavel em 1475x1175 ... (geracao 3; esta sessao durou 12 s)
log    [01:23:46.779] apresentacao (na abertura): 1:1 exato - backbuffer 1475x1175 = area cliente
log    [01:23:46.813] reabrir sozinho: cota de reaberturas gasta (geracao 3 de 3)
```

A quarta troca, na cópia de geração 3, **não** reabriu — voltou ao cartaz de antes
(`[01:23:57.417] fonte mudou de 1475x1175 para 1225x1175 - backbuffer segue em 1475x1175`). É a
trava contra pisca-pisca funcionando.

### PARTE A — com DLSS 2x ligado

```
[01:18:30.417] o alvo esta estavel em 1250x1175 ha 1625 ms ... (geracao 1; esta sessao durou 39 s)
[01:18:31.861] dlss 2x: camada 2 fechada
[01:18:33.327] Fonte: janela 'ALVO-ROBUSTEZ' 1250x1175
[01:18:33.902] apresentacao (na abertura): 1:1 exato - backbuffer 1250x1175 = area cliente
[01:18:33.991] dlss 2x: camada 2 aberta (pid 32796) ... / dlss 2x: camada 2 ATIVA
```

**4,9 s** da troca até as duas camadas de volta na resolução nova. A camada de cima morre junto com a
de baixo (`CamadasParar`, que já existia no encerramento) e nasce de novo com a cópia nova: nenhuma
camada órfã, nenhuma travada na resolução errada.

### PARTE B — janela fechada com o painel do End aberto

```
[01:17:05.880] painel aberto em 2750,270  600x1004  visivel=1  itens=16  escala=1.06
        (WM_CLOSE na janela-alvo às 01:17:07.445)
[01:17:07.628] a janela capturada foi DESTRUIDA (hwnd 0000000000370b92) - fechando painel/guia/efeitos
               e voltando para a tela de escolha da fonte
[01:17:07.633] painel fechado
[01:17:07.633] reabrindo o app (a janela capturada fechou): ...\DLSS5ForAll-rob.exe --reabrindo 0 --esperar-pid 32732
[01:17:07.653] Encerrando (apresentados=730)
[01:17:09.101] DLSS5 For All 1.1 iniciando
[01:17:09.184] seletor aberto em 485,27  950x965  escala=1.25  fonte=janela  alvos=20  itens=26
```

**Painel fechado 188 ms depois do Alt+F4 no alvo; seletor de fonte na tela 1,74 s depois.** Nenhum
processo órfão: a única janela restante era `DLSS5 For All 1.1 — escolha a fonte`.

Com 2x ligado, o mesmo: painel fechado em 80 ms (`01:19:09.888` → `01:19:09.960`), camada 2 fechada em
`01:19:11.412`, seletor em `01:19:12.940`.

Matando o **processo** do alvo (em vez de Alt+F4), e com ele minimizado no momento da morte, o
desfecho é igual: `[01:22:23.015] a janela capturada foi DESTRUIDA ...` → seletor em `01:22:24.574`.

### PARTE A — a borda arrastada com o mouse de verdade

Os testes acima trocam o tamanho de uma vez (`SetWindowPos`), que é o que um jogo faz ao mudar de
modo. Falta o outro caso, o da pessoa: arrastar a borda a mão, que produz **dezenas de tamanhos por
segundo**. Feito com o mouse real (`SetCursorPos` + `mouse_event`, 40 passos de 25 ms), no monitor
ultrawide, com o app em modo sobreposto:

```
01:37:56.743  BOTAO PRESSIONADO em 3418,997   (canto inferior direito da janela-alvo)
[01:37:56.908] fonte mudou de 1650x1050 para 1638x1044 - backbuffer segue em 1650x1050
[01:37:56.923] fonte mudou de 1638x1044 para 1632x1038 - backbuffer segue em 1650x1050
   ... 38 linhas "fonte mudou" em 1,22 s (~31 tamanhos por segundo) ...
[01:37:58.125] fonte mudou de 1299x816 para 1290x810 - backbuffer segue em 1650x1050
01:37:58.285  BOTAO SOLTO
[01:37:59.905] o alvo esta estavel em 1290x810 ha 1781 ms e o backbuffer nasceu 1650x1050 -
               reabrindo o app na resolucao nova (geracao 1; esta sessao durou 23 s)
[01:38:01.334] Fonte: janela 'ALVO-ROBUSTEZ' 1290x810
[01:38:01.854] apresentacao (na abertura): 1:1 exato - backbuffer 1290x810 = area cliente, sem reamostragem
[01:38:06.329] ritmo: a cadeia entregou apos 4453 ms
```

**Uma única reabertura, nenhuma no meio do arrasto** — é exatamente para isso que a espera de
estabilidade existe: 38 tamanhos intermediários passaram sem que o app se reabrisse em nenhum deles.
**3,7 s** entre soltar o botão e a imagem 1:1 no tamanho final; 8,0 s até a cadeia neural voltar a
entregar.

### O que NÃO pode disparar — e não dispara

| Cenário | Resultado |
|---|---|
| Alvo **minimizado** por 15 s e restaurado | Mesmo PID do começo ao fim; nenhuma linha `DESTRUIDA`, nenhuma reabertura. Só `overlay fora de vista ... minimizado=1`, como antes. |
| Conjunto **desligado** (F6) e resolução trocada | `[01:32:32.747] DLSS 5 DESABILITADO` → troca → 12 s depois o mesmo PID vivo, só `fonte mudou de 1436x1175 para 1250x1175`. Ao religar: `[01:32:48.238] DLSS 5 HABILITADO` e `[01:32:48.375] o alvo esta estavel ... reabrindo` — 137 ms depois, imagem 1:1 em 2,2 s. |
| `[ultima] reabrir_auto=0` no ini | `reabrir sozinho: desligado no ini (geracao 0 de 3)`; a troca só loga `fonte mudou`, exatamente como antes desta mudança. |
| Fonte **Monitor** | `Fonte: monitor '\\.\DISPLAY1 3440x1440'`, sobe normal, nada dispara — não há janela para destruir e o tamanho não muda. |
| Sessão longa (80 s) antes da troca | `(geracao 1; esta sessao durou 80 s)` — a contagem se renovou em vez de subir para 2. |

---

## 4. O que ficou de fora, de propósito

- **Recriar a swapchain em voo continua proibido.** Nada aqui encosta em `ResizeBuffers`: a regra do
  `Renderer::FonteMudou` está intacta, e a cadeia NGX nunca é reconstruída dentro da sessão. O preço
  da adaptação são os ~3,6 s de um processo novo — medidos, e o app volta a funcionar sozinho.
- **`docs\ARQUITETURA.md` não foi editado** (vários agentes mexendo no repositório hoje). O §12 cita
  `FonteMudou`; quando for atualizado, o que falta dizer lá é que a resposta a uma troca de resolução
  deixou de ser "reabra o app" e passou a ser "o app se reabre".
- O painel não ganhou caixinha para `reabrir_auto`: a chave existe no ini para quem quiser o
  comportamento antigo, e a interface não precisa de mais um botão para um caso que se resolve sozinho.
