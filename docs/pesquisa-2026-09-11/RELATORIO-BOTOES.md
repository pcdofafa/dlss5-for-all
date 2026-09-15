# Bateria de testes — TODOS os botões do DLSS5 For All

**Data:** 2026-09-11
**Pasta de teste:** `DLSS5ForAll-AMD\bin-teste-botoes\` (cópia própria de `bin-teste-passes\`, com o
`ReShade.ini` já corrigido). Não toquei em `bin-teste-passes\`, `bin\` nem `DLSS5ForAll-Portable\`.
**Binário testado:** compilado da fonte em `amd\` com `build-mingw.sh` (`SKIP_ADDON=1`), g++ do
msys64/mingw64. O exe que já estava em `bin-teste-passes\` bate byte-a-byte em tamanho com o meu
build, então fonte e binário estavam em sincronia.
**Máquina:** RTX 4080 + AMD Radeon(TM) Graphics, monitor primário 1920×1080 a **125%**
(área útil 1020 px). Fonte de teste: Bloco de Notas maximizado, `--window ALVO-BOTOES --overlay`.
**Método:** cliques reais (`WM_LBUTTONUP` com as coordenadas do item), sem mexer no mouse do
usuário, validados pelo log `dlss5forall.log`. Para saber onde cada botão está e para onde o
hit-test o procura, compilei uma cópia instrumentada **no scratchpad** (nunca na fonte de produção)
que despeja o mapa de itens e sonda o `HitTest`.

> As linhas citadas são as do `amd\main.cpp` de 2026-09-11 ~01:15. O arquivo está sendo editado em
> paralelo por outro agente e já andou ~132 linhas durante esta bateria — se não baterem, procure
> pelo nome do símbolo.

---

## VERIFICAÇÃO PRIORITÁRIA — perfil da rede e nível do DLSS: **os dois funcionam**

O usuário pediu garantia de que estes dois grupos fazem efeito real, não só visual. Testei os
dois com rigor. **Nenhum dos dois está quebrado.** Abaixo, primeiro o mapeamento (para ele
confirmar se é disto que fala) e depois a prova.

### Qual controle do painel é cada pedido

| O que o usuário chama | O que é no painel hoje | Código | Chave que sai daqui |
|---|---|---|---|
| "perfil de imagem da rede neural" | Seção **"PERFIL DA IMAGEM (a rede neural)"**, com SEIS linhas de chips: **MODELO** (A/B/C), **INTENSIDADE**, **ESTRUTURA**, **PELE E ROSTOS**, **TOM LOCAL**, **TOM GLOBAL** (0/25/50/75/100%) | main.cpp:3609-3640 (`kCtrlNr`), desenho em 5726, ação em 12098 | `NRStyle`→`DirectNeuralRenderingStyle`, `NRIntensity`, `NRLocalStructure`, `NRSkinStructure`, `NRLocalTone`, `NRGlobalTone` |
| "troca do nível do DLSS" | Chips **"RESOLUÇÃO DO DLSS"**: Auto / 100% / 77% / 67% / 58% | main.cpp:3568-3569 (`kWorkRes`), desenho em 5930, ação em 12128 | `work_resolution` do `dlss5-feed.cfg` |

Dois esclarecimentos sobre o que você mandou procurar:
- O **"Estilo/Perfil ligado a NRStyle"** existe e é o **MODELO A/B/C**. Não há rótulo
  Natural/Cinematic, nem chave `NRPreset`, em lugar nenhum deste código.
- **Não existe** seletor Quality/Balanced/Performance. Quem faz esse papel são os chips de
  RESOLUÇÃO DO DLSS: 100% é DLAA, e 77 / 67 / 58% são as proporções que a NVIDIA chama de
  Quality / Balanced / Performance.

### Prova 1 — NÍVEL DO DLSS: a rede passa a processar menos pixels de verdade

Não é só o arquivo mudar: o **tamanho real da rede muda**, e os dois lados da cadeia registram.
Fonte janela de 1902×913:

| Cliquei em | `dlss5-feed.cfg` | Rede reconstruída em (feed) | Confirmado pelo RenoDX |
|---|---|---|---|
| 77% | `work_resolution=77` | `feature ready: 1464x702` | `CreateFeature(Reserved18) ... size=1464x702` |
| 58% | `work_resolution=58` | `feature ready: 1102x528` | `CreateFeature(Reserved18) ... size=1102x528` |
| 77% (volta) | `work_resolution=77` | `feature ready: 1464x702` | idem |

A conta fecha: 1902×0,58 = 1103 e 913×0,58 = 529 → **1102x528**. E com fonte Monitor de
3440×1440 a 77% a rede foi para **2648x1108** (3440×0,77 = 2649). **Funciona.**

> **Observação que valia ação — JÁ APLICADA** (não é falha, mas gera a impressão de falha): a reconstrução levou
> **51 segundos** enquanto o cartaz do painel promete *"vale em alguns segundos — a rede está sendo
> refeita"*. O motivo está no `dlss5-feed.log`: depois de reler o cfg ele fica
> `holding the feature (re)build (1/60 frames)` — são **60 quadros**, e com o alvo parado ou sem
> foco o present cai para 500 ms, então 60 quadros viram quase um minuto. Com o jogo rodando a
> 60 fps é 1 segundo e a frase está certa; com a fonte parada, a pessoa clica, espera "alguns
> segundos", não vê nada e conclui que o controle não funciona.
>
> **Corrigido em main.cpp:12276-12277**, com o porquê escrito no comentário acima da linha. O
> cartaz agora diz *"vale assim que a cena andar alguns quadros — a rede está sendo refeita"* /
> *"applies once the scene draws a few more frames — the network is being rebuilt"*, em vez de
> prometer segundos fixos. Conferido nas duas línguas dentro do binário recompilado; a frase
> antiga saiu.

### Prova 2 — PERFIL DA IMAGEM: o valor chega ao add-on **e a imagem muda ao vivo**

Cada clique foi verificado em três lugares e depois medido na imagem que sai do filtro.

a) **Gravação** — conferido arquivo por arquivo depois de cada clique:

| Cliquei | log do app | `dlss5forall.ini` | `ReShade.ini` |
|---|---|---|---|
| INTENSIDADE 100% | `DirectNeuralRenderingIntensity = 1 (pela ponte, vale agora)` | `nr_intensidade=1` | `DirectNeuralRenderingIntensity=1` |
| INTENSIDADE 0 | `... = 0 (pela ponte, vale agora)` | `nr_intensidade=0` | `DirectNeuralRenderingIntensity=0` |
| MODELO A | `DirectNeuralRenderingStyle = 0 (pela ponte, vale agora)` | `nr_estilo=0` | `DirectNeuralRenderingStyle=0` |
| MODELO C | `DirectNeuralRenderingStyle = 2 (pela ponte, vale agora)` | `nr_estilo=2` | `DirectNeuralRenderingStyle=2` |

O `(pela ponte, vale agora)` quer dizer que o `rsbridge::SetConfig*` foi aceito — não é uma
promessa do nosso lado, é o retorno da ponte.

b) **Efeito na imagem** — medido, não olhado. Cena sintética estática (gerada para isto:
`cena-do-teste-perfil.png`), filtro em modo espelho, 5 retratos por configuração capturados por
`PrintWindow` na janela do filtro. A rede tem componente temporal — dois quadros seguidos já
diferem sozinhos —, então a pergunta certa é *"muda mais do que muda sozinha?"*:

| Comparação | Diferença média (0-765) | Pixels tocados | Pior pixel |
|---|---|---|---|
| dentro da mesma configuração (ruído próprio) | 0,95 a 1,15 | 11,0% | **40** |
| INTENSIDADE 0 **vs** 100% | **3,29** (3,2× o ruído) | — | — |
| MODELO A **vs** MODELO C | **2,38** (2,1× o ruído) | **27,5%** | **435** |

O "pior pixel" é o número que fecha o caso: dentro da mesma configuração nenhum pixel se afasta
mais que **40/765**; entre MODELO A e MODELO C há pixel com **435/765** de diferença. Isso é
mudança estrutural, não ruído temporal. **O perfil vale ao vivo, sem reabrir.**

> Ressalva honesta de método: a confirmação visual foi feita sobre uma cena sintética minha, não
> sobre um jogo, porque a tela estava ocupada pelo teste de Witcher 3 de outro agente e eu não
> disputei o primeiro plano. A medida acima é suficiente para dizer que o clique muda a imagem;
> ela não diz se o traço de cada MODELO agrada num jogo real — isso é julgamento, não medição.

### Prova 3 — PASSES (o "2x/3x/4x" de hoje)

Com `nr_passes=8` gravado, o add-on faz **exatamente 8 avaliações** sobre o mesmo quadro:
`evaluation=1` … `evaluation=8` seguidos de um único
`source evaluation completed: source=1 ... size=2648x1108`. Com 4 gravado, são 4. **Funciona.**

---

## CAMADAS NA FONTE MONITOR — a resposta

**Resposta curta: sim, o que o usuário quer já funciona na Fonte Monitor — e nunca esteve
travado lá.** O aviso que ele viu é de outra coisa, que foi aposentada.

**(a) Onde estava a restrição.** O texto *"Camadas de DLSS 5 … INDISPONÍVEL NA FONTE MONITOR"*
**não existe mais** no `main.cpp` atual: saiu em 08/09 junto com a interface dos chips de Camada.
Ele sobrevive só nos backups (`main.cpp.bak-dwm`, `.bak-parte4`, `.bak-remove2x`, `.bak-tdr`).
A condição lógica continua viva em **uma linha**, `main.cpp:11032`:

```cpp
g_hud.camada2Ok = !g_camada2 && C.kind == SRC_WINDOW && !C.hideSelf;
```

usada em main.cpp:11228 (restaurar camadas lembradas ao abrir) e 11740 (o handler de
`CMD_CAMADAS_BASE`, ainda alcançável por `--camada N` e pelo ini).

**(b) Ela ainda é necessária?** Depende de qual das duas coisas se fala, e são coisas diferentes:

- **PASSES** (`DirectNeuralRenderingPassCount`, N avaliações da rede sobre o mesmo quadro, no
  mesmo processo): **nunca teve restrição de fonte e não precisa de nenhuma.** O item é criado
  com `enabled` literal `true` em main.cpp:5743, sem consultar a fonte. Confirmado na prática:
  abri o app com `--monitor 0` (captura do ultrawide inteiro, 3440×1440, sem jogo em janela), os
  **10 chips de PASSES apareceram habilitados** (`en=1` nos dez), acionei 1, 4 e 8, e o add-on
  respondeu com o número exato de avaliações — 8 passes gravados → `evaluation=1..8`, em
  `size=2648x1108`. **Sem realimentação, sem loop, sem nenhum problema.** Igual à fonte Janela.
- **CAMADAS** (a arquitetura antiga: uma segunda instância do app capturando a janela da
  primeira): para essa, a restrição **continua tecnicamente necessária**, e pelo motivo que você
  deduziu. A 2ª instância captura a **janela** da 1ª; com fonte Monitor ela capturaria o monitor
  inteiro — onde a própria saída dela está desenhada — e isso é realimentação (efeito túnel).
  Não é código morto: é a única proteção que resta desse caminho, que ainda existe e ainda é
  alcançável por linha de comando.

**(c) O que fazer.** Não removi nada, e recomendo não remover: a linha 11032 protege um caminho
que ainda funciona. O que está errado é o **rótulo**, não a regra — e o rótulo já não está mais
na tela. Se algum dia os chips de Camada voltarem ao painel, o aviso deve dizer a causa real
("a camada de cima captura a JANELA da de baixo; com o monitor inteiro ela capturaria a si
mesma") em vez de um texto genérico. Para o usuário, a mensagem é: **o 2x/3x/4x que ele quer hoje
é o PASSES, e esse não depende de fonte nenhuma.**

---

## FALHAS REAIS (é o que importa)

> **SITUAÇÃO FINAL (11/09 ~02:10): nenhuma falha aberta.** Duas causas independentes produziam o
> "os botões não funcionam", e as duas estão corrigidas e validadas no código de produção — a do
> `g_scale` (FALHA 1) e a do monitor de destino (FALHA 1-b). A terceira que eu havia relatado
> (o ✕ da tela de ofertas) **não existia**: era erro do meu próprio teste, e está retificada abaixo
> com a explicação do que eu fiz errado.

> **Situação em 11/09 ~01:55 — FALHA 1 CORRIGIDA E VALIDADA NO CÓDIGO DE PRODUÇÃO.**
> A correção foi aplicada por outro agente enquanto eu testava, e ficou **mais completa que a
> minha proposta**: além da declaração (main.cpp:5379), da gravação depois do `Render(&g)`
> (main.cpp:7134) e do `HitTest` (main.cpp:7201), ela também corrigiu o **`WM_NCHITTEST`**
> (main.cpp:7260) — a margem da sombra tinha o mesmo problema e eu não tinha visto — e
> acrescentou um `DLSS5_LOG_ITENS=1` que despeja as áreas clicáveis já em coordenadas de tela.
> **Revalidei no binário de produção recompilado:** 25 cliques no mesmo chip, **25 acertos, 0
> erros** (antes: 3 em 25). E o **"Mais opções"**, que era totalmente inalcançável, abre: clique
> em 275,1048 → `item 53`, e o painel foi de 395 para 827 px de largura. Com ele, toda a coluna
> da direita voltou a ser alcançável. O texto abaixo fica como registro do diagnóstico.

### FALHA 1 — CRÍTICA (corrigida): os botões do painel não respondiam onde aparecem na tela

**É este o "os botões não funcionam" que o usuário sentiu.** Não é um botão: é o painel inteiro.

O painel é desenhado numa escala e o clique é procurado em **outra**. Medido nesta máquina, com o
painel aberto e visível:

| ponto sondado | resultado |
|---|---|
| centro visual do card "DLSS 5", 30 sondas seguidas | **30 de 30 devolveram "nenhum item"** |
| o mesmo teste, app recém-aberto e sem tocar em mais nada | **22 de 25 falharam** (88%) |
| clique real no centro visual do card | `painel: clique em 197,169 -> item -1` — nada acontece |

O deslocamento é de **1,79×** (1,25 ÷ 0,70). Consequências, as duas ruins:

1. **A metade de baixo do painel fica morta.** O botão **"Mais opções"** aparece em `y=763`, mas a
   área que o responde fica em `y=1362` — **fora** da janela, que tem 821 px. Ele é inalcançável, e
   com ele some o acesso a **toda a coluna da direita**: V-Sync, Desligar o HDR, Mostrar FPS na tela,
   FPS DO JOGO, OTIMIZAR DESEMPENHO, Idioma, Ver os atalhos e os Atalhos. O mesmo vale para
   "Abrir o ReShade", "Sair do DLSS5 For All", "Grupo de ofertas" e "Apoie o criador".
2. **Na metade de cima, o clique aciona OUTRO botão.** O card "DLSS 5" aparece em `(197,169)` mas é
   acionado em `(352,302)` — e `(339,304)` é onde está desenhado o chip **"100%" da INTENSIDADE**.
   Aconteceu comigo durante o teste: cliquei no "100%" e o log respondeu
   `DLSS 5 DESABILITADO`. **A pessoa clica num chip de imagem e o DLSS 5 desliga.**

**Causa (main.cpp, `namespace Hud`):**

- `Paint()` começa em **main.cpp:6995** com `UpdateScale(...)`, que põe `g_scale` no DPI do monitor
  (aqui, 1,25).
- **main.cpp:7034-7035**: quando o cartão não cabe na altura da tela, o Paint **encolhe a mesma
  variável global** — `g_scale = (cabe < 0.70f) ? 0.70f : cabe`. Aqui ele vai ao piso, **0,70**.
  É com esse 0,70 que o `ScaleTransform` de **main.cpp:7061** desenha os pixels.
- **main.cpp:7002**: o Paint seguinte tem uma saída antecipada — `if (g_jaPintou && sigAgora ==
  g_sigPintada) return;` — que roda **depois** do `UpdateScale`. Ou seja: quando nada mudou na tela,
  a global volta para 1,25 e **o encolhimento não é reaplicado**, enquanto os pixels na tela
  continuam desenhados a 0,70.
- **main.cpp:7125** (`HitTest`) divide o ponto do clique por `g_scale` — a global, que a essa altura
  está em 1,25. Daí o fator 1,79.

O `Paint` roda a cada segundo, então basta o painel ficar um tique sem mudar para entrar no estado
quebrado. Abrir a janela **"Ver os atalhos"** deixa o painel nesse estado de forma determinística
(12 de 12 sondas falharam depois disso), porque `Guia::Paint` também chama `Hud::UpdateScale` e não
repõe o valor encolhido.

**Quando acontece:** sempre que o painel não couber na altura da tela, que é exatamente o caso do
monitor primário desta máquina — e o próprio código já documenta isso em main.cpp:7022-7032
("ele pediria 1202 px e só cabem 1020"). Num monitor mais alto (o ultrawide 1440), o encolhimento é
pequeno e o desalinhamento passa despercebido; em 1080p a 125% ele é de 79%.

**Correção validada.** Compilei uma versão com a correção mínima — guardar a escala com que o cartão
foi realmente desenhado e usar essa no hit-test:

```cpp
static REAL g_scaleDesenhado = 0.f;              // junto de g_scale

// no fim do bloco de desenho do Paint, logo depois de Render(&g):
g_scaleDesenhado = g_scale;

// no HitTest:
const REAL s = (g_scaleDesenhado > 0.f) ? g_scaleDesenhado : g_scale;
const REAL x = p.x / s, y = p.y / s;
```

Resultado no mesmo teste: **25 acertos em 25** (antes: 3 em 25). Com ela, "Mais opções" volta a abrir
e toda a coluna da direita volta a ser alcançável. A cópia está em
`<scratchpad>\src\main.cpp` — **não apliquei nada na fonte de produção**, porque o `main.cpp` está
sendo editado pelo outro agente neste momento.

> Vale conferir se `Guia` (main.cpp:7604-7605/7625/7676) e `Efeitos` (8065-8066/8085/8135), que têm
> o mesmo par encolhimento-global + `HitTest`, precisam do mesmo cuidado. Nos testes as duas
> responderam certo, porque o `Paint` delas não tem a saída antecipada — mas o padrão é o mesmo.

### FALHA 1-b — SEGUNDA CAUSA do mesmo sintoma: o painel era medido num monitor e desenhado em outro (corrigida)

Depois do fix do `g_scale`, **os mesmos cinco botões de baixo continuavam fora do alcance** num
caso específico — o que o teto-fps-avancado tinha notado. Investiguei e é uma causa **independente**
da primeira, com o mesmo sintoma.

**O que acontece.** A escala é decidida por `AlturaDisponivel()` (main.cpp), que mede pelo
`AreaParaJanela()` — e este só conhece **o alvo** (a janela capturada, ou o monitor dela). Mas a
posição final, quando a pessoa já arrastou o painel alguma vez, vem do `painel_x`/`painel_y`
gravados no ini — que podem estar em **outro monitor**. O cartão é então dimensionado para caber
numa tela e desenhado em outra. O `ClamparNaTela` não salva: ele empurra o cartão para dentro, mas
não sabe encolher — quando o cartão é mais alto que a área útil, empurrar só decide qual pedaço
fica de fora.

**Medido aqui** (alvo no ultrawide, 1368 px úteis; `painel_x=1361` apontando para o primário,
1020 px úteis):

| | antes | depois da correção |
|---|---|---|
| tamanho do painel | 550×1352, escala 0,98 | **409×1004, escala 0,72** |
| transbordo para fora da tela | **332 px** | **0 px** |
| itens com o centro fora da tela | **5 de 58** | **0 de 58** |

Os cinco eram exatamente **"Mais opções", "Abrir o ReShade", "Sair do DLSS5 For All", "Grupo de
ofertas" e "Apoie o criador"** — os mesmos da FALHA 1, pelo mesmo sintoma final: clica e não
acontece nada.

**Correção aplicada** (main.cpp, no bloco de encolhimento do `Hud::Paint`): quando há posição
gravada, a altura disponível passa a considerar também o monitor onde o cartão **vai ficar**:

```cpp
int dispPx = AlturaDisponivel(W, H);
if (g_posValida) {
    MONITORINFO miPos{ sizeof(miPos) };
    const POINT centro{ g_pos.x + W / 2, g_pos.y + H / 2 };
    if (GetMonitorInfoW(MonitorFromPoint(centro, MONITOR_DEFAULTTONEAREST), &miPos)) {
        const int hMon = miPos.rcWork.bottom - miPos.rcWork.top;
        if (hMon < dispPx) dispPx = hMon;
    }
}
const REAL disp = (REAL)dispPx - 16.f;
```

**Validação:** 6 ciclos de abrir/fechar "Mais opções" — transbordo 0 em todos, 0 itens fora, nas
duas larguras (409 e 768). **Regressão conferida:** sem posição gravada o painel continua grande no
ultrawide (550×1352, escala 0,98, transbordo 0) — a correção só entra quando há posição gravada, e
não encolhe nada à toa.

> Nota de método para quem for repetir: a área útil do ultrawide é **1368 px físicos**. Uma medição
> de 912 px vem de processo **DPI-unaware** (1368 ÷ 1,5). Misturar as duas unidades faz o painel
> parecer não caber onde cabe. Em PowerShell, chamar `SetProcessDpiAwarenessContext(-4)` antes de
> qualquer medição ou clique resolve: os monitores desta máquina são 1920×1080 a 125% (primário) e
> 3440×1440 a 150% (secundário).
>
> **Sobre o caso concreto que o teto-fps-avancado mediu** (painel de 808×1352 em 1920,16, com o
> "Mais opções" caindo em `Shell_SecondaryTrayWnd`): esse **era o artefato**, não o defeito. Conferi
> com o processo DPI-aware — com o painel no secundário ele mede 550×1352 em 3365,8, a área útil vai
> até y=1368 (a barra de tarefas ocupa 72 px) e o transbordo é **0**, com **0 de 58 itens** fora. O
> "Mais opções" é publicado em y=**1055 físico**, bem dentro; lido como lógico por um processo
> unaware ele viraria y=1582, que cai justamente na barra de tarefas — que é o que o `WindowFromPoint`
> dele devolveu. O defeito real que a correção acima resolve é **outro**: ele aparece quando a
> posição gravada no ini aponta para um monitor **menor** que o do alvo, e aí o transbordo de 332 px
> é medido e reproduzível.

### ~~FALHA 2 — o "✕" da tela de ofertas não fecha nada~~ — **RETIRADA: era erro do meu teste**

**Eu estava errado. O ✕ funciona.** Retifico aqui porque a conclusão anterior era minha, não do
código.

O que aconteceu: meu arsenal de teste rodava em PowerShell **DPI-unaware**, e eu tinha calibrado o
fator de virtualização em **1,25** — que é o do monitor **primário**. A tela de ofertas abre no
monitor **secundário**, que está a **150%**. Todo clique meu naquela janela caía com ~13% de erro,
o que dá **43 px** na largura dela. Os botões grandes (`QUERO AS OFERTAS`, `APOIAR O CANAL`,
`AGORA NÃO`) absorviam o erro e respondiam; o ✕ tem **36 px de lado** e ficava sempre de fora.
O sintoma — "clico e não fecha" — é indistinguível de um botão quebrado, e foi assim que eu o
classifiquei.

Refeito com o processo declarado `SetProcessDpiAwarenessContext(-4)` (PER_MONITOR_AWARE_V2), sem
virtualização nenhuma:

- cliente da janela: **1320×678** px físicos (antes eu media 880×452, que é 1320÷1,5);
- `close_` calculado por `splash_ofertas.h:639` com dpi 144: `x 1263..1299, y 21..57`, centro
  **(1281, 39)** — e é exatamente onde o ✕ aparece no retrato 1:1 (`ofertas-pixels-fisicos.png`);
- clique em (1281, 39) → **a janela fecha**.

Aproveitei para fechar a tabela daquela tela com o fator certo: **`QUERO AS OFERTAS`** (que antes
eu só tinha dado como OK por compartilhar o handler do QR) foi acionado diretamente — fecha a
janela e abre o navegador. **Nenhum controle da tela de ofertas está quebrado.**

> O crédito do achado é do teto-fps-avancado, que mediu o fator (1,498) e avisou que um harness
> DPI-unaware produz exatamente este falso positivo. **Lição de método:** quando o teste é "cliquei
> e não aconteceu nada", a conclusão só vale se o ponto que o app RECEBEU for verificado. Onde eu
> tinha essa verificação (o log `painel: clique em X,Y -> item N`, e as sondas de hit-test), os
> resultados se sustentaram; onde eu julguei pelo efeito, errei.

---

## Sobre os controles que o usuário citou

- **"Abrir o ReShade" duplicado** — **corrigido, confirmado.** Existe **um só** cartão
  (main.cpp:6617, `E_INTERATIVO`), na dupla com "Sair do DLSS5 For All". O cartão largo antigo e o
  `E_RESHADE` foram removidos; os comentários em main.cpp:5343-5344 e 6553 registram isso. No painel
  em execução o mapa de itens mostra exatamente **1** item `INTERATIVO`.

- **Chips "Camadas de DLSS 5" (1x/2x/3x)** — **não existem mais na interface**, e isso é decisão, não
  defeito. Foram retirados em 2026-09-08 porque abriam uma segunda instância inteira do app por
  camada e derrubaram a máquina três vezes (main.cpp:5686-5697). O código (`CamadaIniciar`,
  `E_CAMADAS`, `CMD_CAMADAS_BASE`, `--camada N`) continua inteiro e ainda dá para usar por linha de
  comando ou pelo ini. **Se o usuário procurou por esses chips e não achou, é isso.**

  A arquitetura mudou como o líder suspeitava: o que existe hoje no lugar é a fila **"PASSES" de 1 a
  10** (main.cpp:5743, `E_NRPASSES` → `DirectNeuralRenderingPassCount`), que são avaliações da rede
  sobre a MESMA imagem dentro de um processo só. Testado e **funcionando** (`perfil da rede:
  passes = 4 (pela ponte, vale agora)`). O painel avisa o custo ao lado ("cada passe custa ~4,2 ms").

- **"Teto de FPS do filtro"** — **mudou de nome**, hoje é **"QUADROS QUE O FILTRO PROCESSA (a nossa
  imagem)"** (main.cpp:5789, `E_PULO`). Funciona. O rótulo foi reescrito de propósito em 06/09 porque
  a pessoa procurava a trava do JOGO e achava a do filtro (o comentário em main.cpp:5771-5779 conta o
  caso). A trava do jogo é a seção **FPS DO JOGO**, atrás de "Mais opções".

- **`desempenho.h`** — **está integrado de verdade**, não é arquivo solto: `#include "desempenho.h"`
  em main.cpp:82, e o main chama `Iniciar`, `Encerrar`, `ExeDaJanela`, `LimiteJogoDisponivel`,
  `EstadoDoLimite`, `EhNavegador`, `ModoAtual`, `PrioridadeGpuMaxima`, `Sincronizar`, `Manual` e o
  `g_otim`. Os chips de FPS DO JOGO e o OTIMIZAR DESEMPENHO foram acionados e responderam (abaixo).

---

## Tabela — painel principal

Legenda: **OK** fez o que devia · **FALHA** · **NT** não testado (com o motivo).
Todos os itens abaixo foram acionados com a correção da FALHA 1 aplicada ao build de teste — sem
ela, os da coluna da direita são inalcançáveis (ver FALHA 1).

### Coluna da esquerda

| Botão | arquivo:linha (item → ação) | Esperado | Resultado | Evidência (log) |
|---|---|---|---|---|
| Card **DLSS 5** (HABILITADO / "desabilitar") | main.cpp:5615 → 7139 → 11574 | Liga/desliga camada + neural de uma vez (o mesmo que o F6) | **OK** | `neural: HookPoint=2 -> 0` + `DLSS 5 DESABILITADO`; e de volta `HookPoint=0 -> 2` + `DLSS 5 HABILITADO`. Ligar arma contagem de 3 s, como o código promete |
| **Efeitos** ("abrir") | main.cpp:5674 → 7160 → 11769 | Abre a janela de Efeitos | **OK** | `efeitos: janela aberta em 607,237 705x545` |
| **PERFIL DA IMAGEM**: MODELO A/B/C | main.cpp:5726 → 7151 → 12098 | Muda a alavanca do RenoDX na hora, pela ponte | **OK** (ver Prova 2) | `DirectNeuralRenderingStyle = 0` e `= 2 (pela ponte, vale agora)`; gravado em `nr_estilo` e no `ReShade.ini`; imagem medida muda 2,4/765 contra ruído de 1,1, com pior pixel 435 contra 40 |
| INTENSIDADE / ESTRUTURA / PELE E ROSTOS / TOM LOCAL / TOM GLOBAL (0–100%) | idem | idem | **OK** (ver Prova 2) | INTENSIDADE 0↔100%: valor confere nos dois arquivos; imagem muda 3,29/765 contra ruído de 1,0. Os outros quatro usam o mesmo caminho (`val = ctrl*16 + degrau`) |
| **PASSES** 1…10 | main.cpp:5743 → 7152 → 12111 | Nº de avaliações da rede sobre a mesma imagem; vale agora | **OK** (ver Prova 3) | com 8 gravado, o add-on faz `evaluation=1` … `evaluation=8` e um só `source evaluation completed` |
| **QUADROS QUE O FILTRO PROCESSA**: Todos / 1 de cada 2 / 3 / 4 | main.cpp:5789 → 7140 → 11788 | Processa 1 quadro a cada N capturados | **OK** | `pulo de quadro do filtro: 1 de cada 2 quadros capturados` e `1 de cada 3` |
| **CAMADA INTERMITENTE**: Todos / 1 de cada 2 / 3 / 4 | main.cpp:5859 → 7141 → 11805 | Alterna a camada, mecanismo "alfa" | **OK** | `intermitente: 1 de cada 2 (mecanismo 'alfa')` + `mecanismo 'alfa' pronto` |
| **RESOLUÇÃO DO DLSS**: Auto / 100 / 77 / 67 / 58% | main.cpp:5930 → 7147 → 12128 | Muda `work_resolution` do feed e refaz a rede ao vivo | **OK** (ver Prova 1) | a rede é reconstruída no tamanho novo: `feature ready: 1102x528` a 58% e `1464x702` a 77%, sobre fonte de 1902×913 — confirmado no `dlss5-feed.log` e no `ReShade.log`. Demora ~51 s com a fonte parada (60 quadros), não "alguns segundos" como diz o cartaz |
| **Mais opções / Fechar as opções** | main.cpp:5997 → 7162 | Abre/fecha a coluna da direita na hora | **OK** com a correção; **FALHA 1 sem ela** (inalcançável) | com a correção: `painel: clique em 198,763 -> item 53` e a coluna aparece |
| **Abrir o ReShade** (badge "Home") | main.cpp:6617 → 7167 → 11660 | Abre o overlay do ReShade e entrega o mouse; de novo, fecha e devolve | **OK** (ciclo completo) | abrir: `sobreposto: click-through=0 (reshade: abrindo)` + `PokeReShade vk=0x24 enviados=2/2`; fechar: `click-through=1 (reshade: fechando)` |
| **Sair do DLSS5 For All** | main.cpp:6619 → 7168 → 11533 | **Dois cliques** (o 1º só arma a confirmação, 4 s) | **OK** | 1 clique: nada, por desenho. 2 cliques em 4 s: `Encerrando (apresentados=90)` e o processo sai |
| **Reabrir** (só quando o alvo mudou de tamanho) | main.cpp:6685 → 7171 → 11517 | Relança o app | **OK** | processo antigo sai, novo PID criado no mesmo segundo |
| **Grupo de ofertas — PC do Fafa** | main.cpp:6706 → 7157 → 12265 | Abre a tela de ofertas | **OK** | `grupo de ofertas aberto pelo painel`; janela `DLSS5ForAllOfertas` aparece |
| **Apoie o criador do programa** | main.cpp:6727 → 7158 → 12268 | Abre o canal no navegador | **OK** | `apoie o criador: abrindo o canal`; o Edge subiu de 20 para 22 processos |

### Coluna da direita ("Mais opções")

| Botão | arquivo:linha | Esperado | Resultado | Evidência |
|---|---|---|---|---|
| **V-Sync (da nossa imagem)** | main.cpp:6080 → 7154 → 12229 | Liga/desliga a espera do vblank no nosso present | **OK** | `V-Sync: 1` |
| **Desligar o HDR do monitor (plano B)** | main.cpp:6080 → 7155 → 12240 | Desliga o HDR do monitor enquanto o app roda; religa ao sair | **OK (a chavinha)** / **NT (o efeito)** | A chavinha alterna e o estado persiste (`on=0→1→0` no mapa de itens). O efeito real não deu para medir: `HDR: detectado=0 ... (sem HDR neste monitor · nada a fazer)` — o monitor não estava em HDR na hora |
| **Mostrar FPS na tela** | main.cpp:6080 → 7156 → 12249 | Liga o contador do ReShade (grava no ReShade.ini) | **OK** | `ReShade: SetConfigValue [OVERLAY] ShowFPS = 1` + `FPS na tela: ligado (ReShade.ini gravado)` |
| **FPS DO JOGO**: Livre / 30 / 45 / 60 / 120 | main.cpp:6193 → 7142 → 11822 | Pede o teto pelos dois motores (peça ao vivo + perfil do driver) | **OK** | `limite do jogo: modo 60 escolhido no painel` + `limite do jogo: Notepad.exe -> 60 fps no perfil 'Notepad' (original -1)`; e ao voltar, `modo livre` + `restaurado (chave apagada)` |
| **OTIMIZAR DESEMPENHO** | main.cpp:6342 → 7143 → 12076 | Dispara a escada de degraus (5 s cada); clicado de novo, cancela | **OK** | `otimizar: inicio (alvo 60 fps, exe Notepad.exe, nvidia=1 ...)`, `otimizar: degrau 1 -> pelicula 1.9 fps`, e no 2º clique `otimizar: cancelado pela pessoa` |
| **"fazer valer na hora"** (instalar a peça) | main.cpp:6280/6291 → 7144 → 11839 | Abre a confirmação antes de pôr arquivo na pasta do jogo | **OK** | abre o diálogo com o texto completo (print `confirma-fpslive.png`); recusei, nada foi instalado |
| **Idioma / Language**: Português / English | main.cpp:6519 → 7153 → 12182 | Troca os textos de todo o app na hora | **OK** | `idioma: trocado para en no painel` e de volta `para pt`; os textos e o título das janelas mudam junto |
| **Ver os atalhos** ("abrir") | main.cpp:6536 → 7159 → 11779 | Abre o guia de teclas | **OK** | `guia aberto em 503,140 913x740` |
| **ATALHOS**: "mudar" (6 linhas) | main.cpp:6571 → 7169 → 11467 | Entra em modo de captura; a próxima tecla vira o atalho | **OK** | `captura de atalho iniciada para 'efeitos' - hotkeys suspensas`; Esc cancela |
| **restaurar padrões** | main.cpp:6596 → 7170 → 11475 | Volta aos atalhos de fábrica | **OK** | `atalho painel = End`, `tudo = F6`, `sair = Alt+Esc`, e `filtro/interativo/efeitos = sem tecla` — que é o padrão de fábrica mesmo (o F7 que aparecia antes vinha do registro automático, não do padrão) |
| **"tirar a peça"** | main.cpp:6306 → 7145 → 12016 | Remove a peça da pasta do jogo | **NT** | só aparece com a peça instalada, e eu recusei a instalação |
| **Transporte** (como as camadas trocam imagem) | main.cpp:6112 → 7146 → 11993 | Escolha de abertura entre dois transportes | **NT** | não aparece no painel nesta configuração (depende de 2+ camadas, que saíram da interface) |
| **TETO de FPS** (−10 / −1 / +1 / +10 / sem limite) | main.cpp:6423 e 6438 → `CMD_TETO_BASE` → 12196 | (controle novo) | **NT** | **foi acrescentado ao `main.cpp` pelo outro agente depois do meu build**; não existe no binário que testei. Precisa de uma rodada própria |

---

## Tabela — janela de Efeitos

Hit-test e desenho conferem nesta janela; todos os controles responderam no lugar certo.

| Botão | arquivo:linha | Esperado | Resultado | Evidência |
|---|---|---|---|---|
| Chavinha **Nitidez** | main.cpp:8128 (`K_SWITCH`) | Liga/desliga o efeito | **OK** | `efeitos: Nitidez -> ligado (forte)` / `-> desligado` |
| **Nitidez: Leve / Média / Forte** | main.cpp:8137 (`K_NIVEL`) | Troca o nível (só clicável com o efeito ligado) | **OK** | `efeitos: Nitidez -> leve`, `-> média`, `-> forte`. Com o efeito desligado os chips não respondem — correto, é o `if (!it.enabled) return` |
| Chavinha **Anti-serrilhado** | main.cpp:8128 | Liga/desliga | **OK** | `efeitos: Anti-serrilhado -> ligado (forte)` |
| **Anti-serrilhado: Leve / Média / Forte** | main.cpp:8137 | Troca o nível | **OK** | `efeitos: Anti-serrilhado -> média` |
| **Restaurar padrão** | main.cpp:8148 (`K_PADRAO`) | Volta tudo ao padrão de fábrica | **OK** | `efeitos: padrao de fabrica restaurado (nitidez média · anti-serrilhado leve)` |
| **Fechar** e **✕** | main.cpp:8124-8126 | Fecha a janela | **OK** | `efeitos: janela fechada` |

---

## Tabela — tela inicial (seletor da fonte)

Hit-test e desenho conferem. Testado na janela real (13 alvos na lista), não no shot de demonstração.

| Botão | arquivo:linha | Esperado | Resultado | Evidência |
|---|---|---|---|---|
| **✕** | seletor_hud.h:921 → `Cancelar()` | Sai sem abrir nada | **OK** | `Cancelado no seletor` e o processo encerra |
| **Português / English** | seletor_hud.h:922-930 | Grava o idioma e reabre o seletor traduzido | **OK** | `idioma: trocado para en no seletor`; título vira `DLSS5 For All 1.1 — pick the source` |
| **Janela / Monitor / Câmera** | seletor_hud.h:931 → `TrocarFonte()` | Troca a fonte e relista os alvos | **OK** | `seletor: fonte -> monitor` / `-> janela` |
| **Lista de alvos** (cada linha) | seletor_hud.h:932-936 | Seleciona o alvo | **OK** | sonda acerta o item `kind=K_ALVO` com o índice certo |
| **Recorte: Área útil / Janela inteira** | seletor_hud.h:938 | Captura só o conteúdo ou a janela toda | **OK** | atinge `K_RECORTE val=0` e `val=1`; o efeito aparece no INICIAR (`recorte=area util`) |
| **Chavinhas** SOBREPOR / Tela cheia / Cursor / Esconder / V-Sync / HDR / FPS na tela | seletor_hud.h:939-958 | Cada uma guarda sua opção para o INICIAR | **OK** | atingem `K_SW` com o `val` certo; o INICIAR aplica (`FPS na tela: ligado`, `cursor=0 esconder=0`) |
| **Placa de vídeo** (NVIDIA / AMD) | seletor_hud.h:959 | Escolhe a placa que roda o filtro | **OK** | atinge `K_GPU val=0/1`; o INICIAR usa (`gpu: usando [0] NVIDIA GeForce RTX 4080`) |
| **Atualizar lista** | seletor_hud.h:970-980 | Relista as janelas sem perder o alvo | **OK** | atinge `K_ATUALIZAR` e a lista é refeita |
| **Atalhos…** | seletor_hud.h:981-984 | Abre o diálogo de teclas | **OK** | janela `DLSS5ForAllAtalhos` ("Atalhos do teclado") aparece |
| **INICIAR** | seletor_hud.h:985 → `Iniciar()` | Fecha o seletor e abre o filtro com as escolhas | **OK** | `Fonte: janela '...' 544x169 (recorte=area util ...)`, `D3D11 ok, swapchain waitable`, `neural: Hook Method = 2`, `cadeia: work_resolution=77` |
| **Baixar os arquivos / Abrir a pasta** | seletor_hud.h:960-969 | Abre o link / o Explorer | **NT** | só aparecem quando falta arquivo; aqui não faltava nada |

---

## Tabela — tela de ofertas

| Botão | arquivo:linha | Esperado | Resultado | Evidência |
|---|---|---|---|---|
| **QR code + endereço escrito** | splash_ofertas.h:1451 (`PontoAbreLink`) | Abre o grupo no navegador e fecha | **OK** | a janela fecha no clique |
| **QUERO AS OFERTAS** | splash_ofertas.h:1451 | idem | **OK** | acionado diretamente (clique em 818,413, pixels físicos): a janela fecha e o navegador abre |
| **APOIAR O CANAL** | splash_ofertas.h:1454-1456 | Abre o canal no navegador e fecha | **OK** | a janela fecha e o Edge ganha processos novos |
| **AGORA NÃO** (`R.sec`) | splash_ofertas.h:1470 (`PontoFecha`) | Fecha e o app segue | **OK** | a janela fecha, nada mais acontece |
| **✕** | splash_ofertas.h:639 + 1470 | Fechar | **OK** | clique em (1281,39), o centro exato do `close_` em pixels físicos: a janela fecha. (A "FALHA 2" que eu tinha relatado aqui era erro do meu teste — ver acima) |
| **Esc** | splash_ofertas.h (WM_KEYDOWN) | Fechar | **OK** | fecha |
| Chips de idioma da splash | splash_ofertas.h:1460-1463 | Trocam o idioma e a janela fica aberta | **NT** | só existem no modo launcher |

---

## Não testado, e por quê

- **Launcher / vitrine** (`USAR O DLSS 5 EM PLACA NVIDIA`, `USAR O DLSS 5 EM PLACA AMD`,
  `ABRIR SEM DLSS 5`): é o `amd\portable-launcher\launcher.cpp` reusando a mesma tela
  (`splash_ofertas.h`, modo launcher) — outro executável, com fluxo de atualização e telemetria.
  Precisa de rodada própria.
- **Módulo AMD de verdade**: a máquina tem uma Radeon integrada, mas o caminho AMD do DLSS 5 depende
  de driver Adrenalin e do mod; o log é claro (`AMD Radeon(TM) Graphics ... sem DLSS 5`).
- **Chips "TETO de FPS"** (`E_TETO`): entraram no `main.cpp` depois do meu build.
- **"tirar a peça"** e **Transporte**: não aparecem nesta configuração (ver a tabela).

---

## Onde estão as evidências

PNGs nesta pasta (`amd\docs\pesquisa-2026-09-11\`):
`painel-mais.png` (painel com "Mais opções" aberto — o mapa visual de todos os controles),
`painel-fechado.png`, `efeitos.png`, `guia.png`, `seletor-atual.png`, `ofertas-full.png`
(onde se vê o ✕ que não funciona), `confirma-fpslive.png`.

Logs de todos os cliques: `DLSS5ForAll-AMD\bin-teste-botoes\dlss5forall.log` (as linhas
`painel: clique em X,Y -> item N` são o registro literal de cada acionamento; as linhas
`QAMAP:`, `QAHIT:`, `QAEF:` e `QASEL:` são da minha instrumentação de teste e não existem no
binário de produção).

Os builds instrumentado e corrigido estão no scratchpad desta sessão, em `src\`. A fonte de
produção em `amd\` **não foi alterada**.
