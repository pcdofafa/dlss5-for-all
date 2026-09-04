# Enxugar o painel — cor (LUT) e nitidez fora

**03/09/2026.** Decisão do usuário: *"o programa precisa ficar mais leve pra operar; por
exemplo poderíamos remover o filtro de LUT e nitidez"*. Feito. Este documento registra o
que saiu, o que ficou órfão no disco, e o levantamento do que ainda custa por frame.

---

## 1. A confirmação que veio antes de apagar

O preset em uso, `FafaDLSS5.ini`, tem exatamente:

```
Techniques=Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_Feed@DLSS5_Feed.fx
```

Nem `DLSS5_Grade`, nem `DLSS5_Sharpen`. Os dois efeitos que os chips do painel comandavam
**não estavam na cadeia**. Cada clique caía no ramo de erro — `UiSetLut(n) recusado
(ponte=… grade=…)` e `A nitidez precisa do DLSS5_Sharpen no preset (preset-05)` — e a nota
do card já dizia na tela "o preset em uso não carrega o DLSS5_Grade".

**Não se perdeu imagem nenhuma.** Saíram controles mortos.

## 2. O que saiu do `main.cpp`

| O que | Onde estava |
|---|---|
| `CMD_LUT_BASE` (1000), `CMD_LUTSTR_BASE` (1100), `CMD_SHARP_BASE` (1300) | enum de comandos |
| 4 `static_assert` de faixa, substituídos por 1 (`CMD_FPS_BASE + 16 < CMD_ATALHO_BASE`) | logo abaixo |
| `kLutNames[6]`, `kSharpNomes[4]`, `kSharpNiveis[4]` | tabelas de nomes |
| `struct LutStatus`, `UiLutStatus()`, `UiSetLut()`, `UiSetLutStrength()`, `UiSetSharpen()` | ponte da LUT |
| campos `lut`, `lutStrength`, `sharpen`, `lutSt` do `HudState` | estado do painel |
| campos `lut`, `sharpen`, `grade` do `EstadoFiltro` | estado guardado do F6 |
| `E_LUT`, `E_LUTSTR`, `E_SHARP` do `enum EKind` e os 3 `case` do `Activate` | itens clicáveis |
| o card **COR · LUT** inteiro: 6 chips + nota de estado + 4 chips de intensidade + 4 chips de nitidez | `Render()` |
| `g_hud.lutSt = UiLutStatus()` a cada desenho | `Hud::Paint()` |
| os 3 ramos de comando (LUT, nitidez, intensidade) com seus toasts | laço principal |
| a preparação de LUT/nitidez do `--panel-shot` | modo de captura de tela |

O menu da bandeja não citava nenhum dos dois (ele só diz "tudo se faz pelo painel"). A guia
de atalhos citava: `"é por aqui que se faz tudo: filtro, cor, nitidez, fps"` virou
`"…: filtro, neural, fps, HDR"`.

### `AlternarTudo` (F6) — o que mudou, em detalhe

`EstadoFiltro` guardava seis coisas; agora guarda três: `guardado`, `neural`, `neuralNos`,
`camada`. Sumiram `lut`, `sharpen` e `grade`, e com eles:

- o bloco que desligava `kGradeTechnique` e `SetSharpenEnabled(false)` **antes** do poke;
- o ramo de desfazer dentro do `if (!g_antes.neuralNos)` — não há mais o que desfazer,
  porque nada foi feito pela ponte antes do poke;
- o mesmo bloco no caminho de religar (`SetLut`, `UiSetSharpen`, técnica de grade);
- o desfazer simétrico quando o poke de religar aborta;
- os campos `lut=%d nitidez=%d grade=%d` da linha de log, que virou
  `TUDO DESLIGADO (guardado: neural=%d camada=%d)`.

**O contrato tudo-ou-nada continua idêntico.** Poke abortado ⇒ `g_antes = EstadoFiltro{}`,
`g_tudoDesligado` não é marcado, a camada não se mexe, retorno é `std::wstring()` vazio (o
cartaz do motivo é do `PokeReShade`). O que mudou é só que a lista de coisas a restaurar
ficou vazia. O ramo de religar mantém `neuralNos` marcado quando o poke falha, para o F6
seguinte tentar de novo em vez de inverter o sentido — igual a antes.

## 3. O que NÃO foi tocado

- **`reshade_bridge.h` inteiro.** `rsbridge::SetLut`, `SetLutStrength`, `SetSharpen*`,
  `SetGradeEnabled`, `kGradeTechnique`, `GetStatus` e os campos `grade_loaded` /
  `grade_enabled` / `current_lut` do `DLSS5BridgeStatus` continuam lá, agora sem chamador.
  `DLSS5_BRIDGE_PROTOCOL` e todas as structs estão intactos: **o `dlss5-bridge.addon64`
  instalado (protocolo 2) segue compatível com o exe novo.**
- **`addon/dlss5_bridge_addon.cpp`** — nenhuma linha.
- **Nenhum arquivo apagado do disco.**

## 4. Ficou órfão (para o usuário decidir depois)

Nada disso é mais selecionado por nenhum código do app. Tudo continua no disco e no pacote
de propósito — quem quiser grading liga a técnica pelo overlay do ReShade (Home) e ajusta
os uniforms lá mesmo.

| Arquivo | Tamanho | Situação |
|---|---|---|
| `reshade-shaders\Shaders\DLSS5_Grade.fx` | — | usável só pelo overlay do ReShade |
| `reshade-shaders\Shaders\DLSS5_Sharpen.fx` | — | idem |
| `reshade-shaders\Textures\dlss5_grade_luts.png` | — | o atlas das 5 LUTs; só o Grade.fx usa |
| `gerar-luts.ps1` | 11 KB | gera o atlas acima |
| `preset-02-grade-neutra.ini` | 1,4 KB | traz o `DLSS5_Grade` |
| `preset-03-grade-cinema.ini` | 1,4 KB | idem |
| `preset-04-brilho-contido.ini` | 2,7 KB | idem |
| `preset-05-nitidez.ini` | 2,7 KB | traz o `DLSS5_Sharpen` |

`preset-01-nucleo-afinado.ini` **não** é órfão: ele é o núcleo (`IMAGE_SPACE=1`), sem
grading. O `empacotar.ps1` continua copiando todos eles, agora com um comentário dizendo
que são material opcional; tirar do zip é decisão do usuário, não do script.

Também ficam órfãos os trechos do `reshade_bridge.h` listados em §3 — deliberadamente, para
não mexer no protocolo.

## 5. Ganho medido (não estimado)

Comparação real: `--panel-shot` do exe anterior contra o exe novo, mesmas condições, mesmo
diretório.

| | antes | depois |
|---|---|---|
| painel renderizado | 564 × **1437** px | 564 × **1236** px |
| PNG resultante | 92.028 B | 80.077 B |

**201 px a menos de altura — 14 % da área do painel.** Por desenho isso significa, além da
área:

- **10 a 14 itens clicáveis a menos** no `g_items` (6 LUT + 4 nitidez + 4 intensidade
  quando visível). Como `Paint()` chama `Render()` **duas vezes** (uma com `g=nullptr` só
  para medir a altura, outra para desenhar), são 20 a 28 `FillRound` antialiasados e outras
  tantas chamadas `DrawString` do GDI+ a menos por repintura;
- **20 camadas de sombra** (`SH=20`) sobre um retângulo 201 px mais baixo — a sombra é o
  laço mais caro do `DrawAll`, e ela encolheu junto;
- **uma chamada `rsbridge::GetStatus()` por desenho a menos** (era feita no `Paint()` só
  para saber que os chips estavam mortos).

Isso vale a cada `Hud::Refresh()` — que acontece 1×/s enquanto o painel está aberto, mais
uma vez por clique.

---

# Segunda parte — onde o app ainda gasta, por ganho/risco

Levantamento, não implementação. Números do `dlss5forall.log` da última sessão:
`acordar 1.63/9.96 ms`, `PRESENT->TELA 31.40/44.46 ms`, fps de chegada 14,9, e o ritmo do
WGC com `média 64.02 ms / p95 200.50 ms` entre `FrameArrived`.

**Leitura desses números antes de otimizar qualquer coisa:** a fonte estava entregando
14,9 fps porque o WGC só produz frame quando a tela muda — o `p05 9.74 ms` mostra que
quando há movimento ele entrega rápido. O `PRESENT->TELA` de 31–58 ms é a fila do
compositor mais a cadeia `Lumenite_Kernel + DLSS5_Feed + DLSS NR` na GPU. **Nenhum dos dois
é CPU nossa.** O que segue são gastos nossos, e todos são pequenos perto do `Present` —
valem pelo que tiram do caminho, não por um salto de fps.

### 1. `TailArquivo` do `ReShade.log`: 96 KB do disco 1×/s, no laço de render — GANHO ALTO, RISCO BAIXO

`LerEstadoNeural()` roda no tick de 1 s do laço principal (`main.cpp:5270`) e, no seu
passo 3, faz `TailArquivo(g_exeDir + L"ReShade.log", 96 * 1024)`. Isso é, por segundo:
`CreateFileW` + `GetFileSizeEx` + `SetFilePointerEx` + `ReadFile` de 96 KB síncrono, uma
alocação de `std::string` de 96 KB, e **três `find` varrendo os 96 KB** (`inline feature 18
evaluation succeeded`, `first NGX evaluate intercepted`, `NGX feature create intercepted`).
Cerca de 300 KB de varredura por segundo para responder uma pergunta booleana. O
`NeuralPeloLog()` (`main.cpp:4098`) faz outra leitura de 96 KB, com mais dois `rfind`, nas
conferências pós-F6.

O arquivo hoje tem 22 KB — a leitura pega ele inteiro. Ele chega a 96 KB+ em sessão longa.

**Correção:** guardar o tamanho do arquivo da chamada anterior e **sair cedo quando ele não
cresceu** (o caso comum: o ReShade só escreve quando algo acontece). Quando cresceu, ler só
o delta. Um `static LONGLONG ultimoTam` resolve o essencial em ~5 linhas. Alternativa
menor e ainda mais barata: reduzir a janela de 96 KB para 16 KB — as linhas procuradas
sempre estão no fim.

> **ATENÇÃO:** `LerEstadoNeural` é território do outro agente nesta rodada. Isto é
> **relato**, não foi editado.

### 2. O tick de 1 s lê o disco mesmo com o painel FECHADO — GANHO ALTO, RISCO BAIXO-MÉDIO

`main.cpp:5270-5271`:

```cpp
g_hud.neural = LerEstadoNeural(&g_hud.neuralDetalhe);
Hud::Refresh();
```

`Hud::Refresh()` é `{ if (g_hud.open) Paint(); }` — ele já não faz nada com o painel
fechado. Mas a linha acima **roda sempre**, lendo o log do disco a cada segundo para
atualizar um campo que, no uso normal (painel fechado), ninguém está olhando.

**Correção:** com o painel fechado, espaçar para 5 s; ao abrir o painel, ler na hora (os
caminhos de `CMD_PANEL_TOGGLE` já chamam `LerEstadoNeural` em `main.cpp:5017` e `5066`).
Elimina 4 de cada 5 leituras de disco da sessão inteira.

**Cuidado obrigatório:** o `t0Iniciando` de 25 s dentro do `LerEstadoNeural` conta o prazo
do "não subiu", e `g_hud.neural` alimenta o `AlternarTudo`. Espaçar o intervalo é seguro;
*pular* a leitura enquanto o estado é `NR_INICIANDO` não é. Mesmo aviso de território do
item 1.

### 3. `Paint()` monta o layout duas vezes por repintura — GANHO MÉDIO, RISCO MÉDIO

`Paint()` chama `Render(nullptr)` só para descobrir `cardH`, e `DrawAll()` chama
`Render(&g)` de novo para desenhar. Todo o layout (dezenas de `RectF`, o `g_items` limpo e
reconstruído, todas as `std::wstring` de texto) é feito duas vezes a cada desenho.

**Correção:** memorizar o `cardH` e só remedir quando algo que muda a altura mudou (DPI,
seção de atalhos aberta, aviso de HDR presente, botão "Reabrir" visível). É uma mudança
contida, mas mexe no caminho de desenho do painel inteiro — o tipo de coisa que quebra
layout em silêncio.

### 4. A sombra do painel: 20 `FillRound` antialiasados sobre a área toda — GANHO MÉDIO, RISCO MÉDIO

`DrawAll()` desenha `SH=20` retângulos arredondados translúcidos, cada um cobrindo
praticamente o painel inteiro (564 × 1236 hoje), com `SmoothingModeAntiAlias`. É o laço
mais caro do desenho, e ele se repete a cada `Refresh` — inclusive no tick de 1 s, em que
só mudaram três números de fps.

**Correção:** desenhar sombra + cartão uma vez num `Bitmap` de cache e reaproveitá-lo
enquanto `W`/`H`/`g_scale` não mudarem, redesenhando só o conteúdo por cima. Custa uma
alocação extra de bitmap e obriga a invalidar o cache em todo lugar que muda o tamanho.

### 5. `Refresh()` de 1 s repinta mesmo quando nada visível mudou — GANHO PEQUENO, RISCO BAIXO

Com o painel aberto, o tick repinta o painel inteiro por segundo. O que realmente muda são
`fpsSrc`, `fpsPresent` e (raramente) o estado do neural. Comparar um resumo do estado
visível com o do último desenho e pular o `Paint()` quando idêntico é seguro, mas o ganho
é pequeno na prática: os fps mudam quase sempre. Só compensa se combinado com arredondar os
fps exibidos.

### 6. `FollowTarget` a cada volta do laço — GANHO PEQUENO, RISCO BAIXO

Chamado toda iteração (mínimo 10×/s pelo timeout de 100 ms do
`MsgWaitForMultipleObjects`, até ~90×/s com frames chegando). Faz `GetForegroundWindow`,
`IsIconic` e dois `GetWindowThreadProcessId` — chamadas que cruzam para o subsistema de
janelas. Já usa `TargetRectCached` em vez de `TargetRect` justamente para não bater no DWM.
Dá para limitar a checagem de foreground a 100 ms, mas o ganho é da ordem de microssegundos
por volta.

### 7. Alocação de 96 KB por chamada no `TailArquivo` — GANHO PEQUENO, RISCO MUITO BAIXO

`std::string buf((size_t)(tam.QuadPart - ini), '\0')` aloca e zera até 96 KB a cada
chamada, 1–2×/s. Um buffer estático reaproveitado resolve. Só vale fazer junto do item 1 —
sozinho é ruído perto do `Present`.

### 8. O custo dominante não é CPU nossa — SEM AÇÃO DE CÓDIGO

`PRESENT->TELA 31,4 / 44,5 ms` é a cadeia na GPU mais a fila do compositor. As alavancas
reais de "mais leve" aí são de configuração, não de código:

- o **teto de fps do filtro** já existe no painel e continua sendo o botão certo para
  consumo/temperatura (e o próprio código registra, com medição, que ele **não** reduz
  latência — `docs/latencia-nr.md` §4.2-4.3);
- o preset em uso roda `Lumenite_Kernel` **e** `DLSS5_Feed`. Se o objetivo for peso de GPU,
  medir o app com só o `DLSS5_Feed` é o próximo experimento de maior retorno — e é uma
  decisão de imagem, do usuário, não uma otimização que eu deva aplicar sozinho.

---

## Ordem sugerida

1. **Item 1** (delta do `ReShade.log`) — maior retorno, cinco linhas, risco baixo.
2. **Item 2** (não ler com o painel fechado) — mesmo arquivo, mesma sessão de trabalho.
3. **Item 4** (cache da sombra) — o maior ganho no desenho, se o painel aberto incomodar.
4. **Item 3** (layout uma vez só) — só depois do 4, e com um `--panel-shot` de antes e
   depois para provar que o layout não mexeu (foi assim que se mediram os 201 px acima).
5. Itens 5, 6 e 7 são polimento; não justificam risco sozinhos.

Os itens 1 e 2 estão em `LerEstadoNeural` / `TailArquivo`, que nesta rodada pertencem ao
outro agente. Coordenar antes de tocar.

---

# Terceira parte — os itens 3 a 6, aplicados e medidos

O usuário aprovou seguir. Feitos os itens 3, 4, 5 e 6; os itens 1, 2 e 7 ficaram com o
outro agente, porque moram no `LerEstadoNeural` / `TailArquivo`.

## A surpresa da medição: a sombra era 93 % do desenho

Antes de escolher o que otimizar, dois microbenchmarks isolados, no tamanho real do painel
de hoje (564 × 1236, `cardH = 1196`, escala 1,0), nesta máquina:

| o que | custo por repintura |
|---|---|
| **moldura** (as `SH=20` camadas de sombra + o cartão) | **22,9 ms** |
| conteúdo (~30 linhas/chips + ~65 `DrawString`) | 1,64 ms |
| 4 × `GetPrivateProfileStringW` no `ReShade.ini` | 0,14 ms |

A sombra sozinha era **93 % do custo de desenhar o painel** — 20 `FillPath` arredondados,
com antialias, cada um cobrindo a área quase inteira: cerca de 14 milhões de pixels
compostos por repintura. E isso rodava **uma vez por segundo, no mesmo thread do laço
principal que faz o `Present`** — um pico de 23 ms bem no caminho crítico.

Isso reordena a lista original: o item 4 não era "ganho médio", era o item.

## O que mudou no código

**Item 4 — moldura guardada.** `DrawAll` foi partido em `DesenharMoldura` (sombra +
cartão) e o resto. O `Paint` agora guarda a moldura pronta num `Bitmap` em pixels físicos
(`g_fundo`) e a copia com a transformação identidade e `CompositingModeSourceCopy` — blit
1:1, sem reamostrar (escalar um bitmap pronto borraria a borda da sombra) e sem blend, o
que dispensa também o `Clear` da superfície inteira. O cache é refeito só quando `W`, `H`
ou `g_scale` mudam, e liberado no `Shutdown`.

| | antes | depois |
|---|---|---|
| pôr a moldura na tela | 22,9 ms | **0,072 ms** (317×) |

**Isso foi validado pixel a pixel, não no olho:** a bancada desenha a moldura pelos dois
caminhos e compara os bitmaps byte a byte — **0 bytes diferentes de 2.788.416**, pior
desvio de canal 0. O cache é indistinguível do desenho original.

**Item 5 — não repintar o que não mudou.** `AssinaturaVisivel()` (FNV-1a) resume tudo que
o `Render` transforma em pixel; igual à da última pintura, o `Paint` sai antes de tocar em
qualquer GDI. Dois cuidados que valem registro:

- os fps entram **arredondados** (`(int64_t)(x + 0.5)`), porque é assim que aparecem
  (`%.0f`). Sem isso a assinatura mudaria a cada tick e a economia não existiria. Mas o
  realce laranja do `PRESENT` lê os valores **crus**, então o booleano dele entra separado;
- o `Paint` não decide só o conteúdo: ele também posiciona a janela e encolhe a escala
  quando o painel não cabe na tela. Por isso entram na assinatura o retângulo da janela de
  render e a área de trabalho do monitor — sem eles, arrastar o jogo ou trocar a resolução
  com o painel aberto deixaria o painel parado no lugar velho.

Abrir o painel chama `InvalidarPintura()`: reabrir sempre desenha. Custa uma repintura e
elimina a classe inteira de "abri e está com dado velho".

**Item 3 — medir a altura só quando ela pode ter mudado.** `AssinaturaAltura()` resume as
duas únicas coisas que mexem na altura do cartão (o card "Reabrir", que depende de
`fonteMudou`, e a linha de HDR do rodapé — o resto é soma de literais). Iguais, o `cardH`
da última medição vale ao pixel e o `Render(nullptr)` não roda. O `g_items` não se perde:
quem o preenche de verdade é o `Render(&g)` que desenha.

**Bônus do item 3 — o `ReShade.ini` saiu do laço de desenho.** `TeclaOverlayReShade()` faz
`GetPrivateProfileStringW`, e o `Render` a chamava **duas vezes** (card do ReShade e
rodapé). Com dois layouts por repintura, eram **4 leituras de ini por repintura**. Agora é
uma por layout, guardada numa variável local. Medido: 0,035 ms por leitura.

**Item 6 — `FollowTarget`.** Avaliado e aplicado só o que é seguro: o PID da janela alvo e
o do nosso processo não mudam enquanto a janela for a mesma, e eram consultados a cada
volta do laço — agora só quando o alvo troca. **Não** espacei a checagem de foreground:
atrasá-la faria a camada demorar a sair da frente no Alt+Tab, que é exatamente o que ela
não pode fazer. O ganho aqui é de microssegundos por volta; entrou por ser gratuito, não
por ser importante.

## Resultado

Custo de uma volta do tick de 1 s, com o painel **aberto**:

| situação | antes | depois |
|---|---|---|
| nada mudou (o caso comum) | ~24,7 ms | **~0,04 ms** (só a assinatura) |
| algo mudou, mesma altura | ~24,7 ms | **~1,8 ms** (blit + conteúdo) |
| mudou a altura | ~24,7 ms | ~1,9 ms (+ um layout de medição) |
| primeira pintura / novo tamanho | ~24,7 ms | ~24,7 ms (gera o cache) |

Com o painel **fechado** nada disso roda — o `Refresh()` já saía cedo antes desta rodada.

## Como isso foi verificado

- **`--panel-shot` byte a byte**: o PNG do painel antes e depois das quatro mudanças tem o
  mesmo MD5 (`07e213ad99914e67743ad555ede5554a`, 564 × 1236). O layout não se mexeu.
- **bancada de pixels** para o cache da moldura: 0 bytes diferentes em 2,7 MB (acima).
- **compilação** com `-Wall -Wextra`: sem erro e sem warning novo.
- **instrumentação permanente**, como o lead pediu: `ContaPaint()` cronometra o `Paint`
  com `QueryPerformanceCounter` e, **só com `DLSS5_DIAG=1`**, resume a cada 30 repinturas:
  `painel: 30 repinturas, X ms media, Y ms pior; Z pulos (nada mudou)`. Fica no app para
  medir em uso real — o que estas bancadas não conseguem fazer.

**Ressalva honesta sobre os números:** os 22,9 / 1,64 / 0,072 ms vêm de bancadas isoladas
que replicam o código de desenho, não de uma sessão do app com o painel aberto — não dava
para rodar o app interativo nesta rodada. As contagens de chamadas e os dois testes de
igualdade de pixels são exatos; os tempos são desta máquina e desta escala (1,0), e
escalam com a área. A linha do `DLSS5_DIAG` existe justamente para confirmar em uso real.

## Uma regra que o próximo a mexer precisa saber

Está no comentário do código, e vale repetir aqui: **qualquer coisa nova que o `Render()`
passe a ler PRECISA entrar na `AssinaturaVisivel()`.** Se ficar de fora, o painel congela
mostrando o valor velho, e o defeito aparece como "o painel não atualiza" — sintoma que
não aponta para a função culpada. Na dúvida, inclua: uma entrada a mais custa alguns
nanossegundos de hash; uma a menos custa uma tarde de depuração.
