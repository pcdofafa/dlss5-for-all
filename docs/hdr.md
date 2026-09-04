# HDR: detectar, tratar e a caixinha que o usuário controla

Entrega do agente de HDR. Cobre os **dois caminhos** por onde o HDR estraga a imagem deste
app — a captura de janela/monitor (Windows Graphics Capture) e a placa de captura/console
(Media Foundation) — sob **uma única opção do usuário**, desmarcada por padrão.

Foi executado nesta máquina: um ciclo real de desligar/religar o HDR do display, uma sonda
somente-leitura da ezcap Game Link RAW, a matemática da conversão verificada numericamente, o
pixel shader compilado de verdade e — desde a §8 — **a captura em FP16 implementada, rodada e
medida contra alvos de cor conhecida**. Os números têm procedência; o que é estimativa está
marcado como estimativa.

**Estado: implementado.** `screen_capture.h`, `main.cpp` e `hdr_control.h` foram alterados; o
`DLSS5ForAll.exe` da pasta foi recompilado e passou no teste de fumaça com a cadeia completa.
A prova visual está em `docs\img\hdr-antes-depois.png`.

---

## 0. Contrato com a interface — para o agente do `main.cpp`

Uma classe, `Hdr::Treatment`, resolve as duas fontes. **Todos os métodos são idempotentes e
seguros de chamar a qualquer momento**, inclusive com a captura rodando.

```cpp
#include "hdr_control.h"

Hdr::Treatment hdr;

// uma vez, no inicio do wWinMain
Hdr::g_logSink = [](const char* s) { Log("%s", s); };
hdr.Init(IniPath());          // persiste a caixa em [hdr] tratar=0|1
hdr.RecoverStale();           // desfaz o que uma sessao morta deixou desligado

// quando a fonte fica conhecida (uma das duas)
hdr.SetSourceScreen(C.win, C.mon);              // janela / monitor
hdr.SetScreenFp16(scap.hdrFp16, scap.hdrSdrWhiteNits);  // formato que a captura abriu
hdr.SetSourceVideo(Hdr::VideoColorFromMediaType(mt));   // placa / camera

// SEMPRE, independente da caixa - detectar e barato e nao mexe em nada
hdr.Detect();

// a caixa. Pode ser chamada no seletor, no painel, a qualquer hora
hdr.SetEnabled(marcada);

// so na fonte de video: alimenta a heuristica de conteudo (a cada ~30 frames)
hdr.FeedFrameStats(Hdr::MeasureBgra(cap.buf.data(), cap.w, cap.h, cap.stride));

// o que a interface le
hdr.IsEnabled();     // estado da caixa (vem do ini; NAO quer dizer "ja agiu")
hdr.HdrFound();      // detectou HDR nesta fonte?
hdr.Acting();        // esta agindo agora? (e este que diz se o HDR foi desligado)
hdr.LastAttempt();   // TENT_NENHUMA / TENT_OK / TENT_NAO_PRECISOU / TENT_FALHOU
hdr.StatusLine();    // uma linha para o painel
hdr.WarnLine();      // o aviso, quando ha HDR e a caixa esta desmarcada ("" se nao ha)
hdr.Shader();        // Hdr::ShaderParams -> o cbuffer do PS (mode 0 = nao faz nada)

// no encerramento
hdr.Restore();
```

Textos prontos, para não serem reinventados: `Hdr::Txt::Caixa()` (rótulo),
`Hdr::Txt::Tooltip()` (tooltip do seletor), `Hdr::Txt::Explicacao()` (hint longo /
rodapé do painel), `Hdr::Txt::Aviso(bool fonteDeTela)` (o toast). Estão na §5.

Uma linha a mais, em `Fatal()`: `Hdr::Manager::RestoreActive();` — `ExitProcess` não roda
destrutor nem `atexit`, e sem isso um erro fatal deixaria o display SDR.

**Duas regras de uso, que custaram um relato de campo (§4.4):**

1. **`StatusLine()` depois de agir, nunca antes.** Ela descreve o estado atual; chamada antes
   do `SetEnabled` ela conta o passado. Se você loga a linha, chame `Detect()` (ou o seu
   `HdrRefreshLine()`) **depois** do `SetEnabled`.
2. **A caixa marcada não significa que o app agiu.** `IsEnabled()` vem do ini e pode estar
   marcada desde a sessão anterior, antes de qualquer ação nesta. Quem responde "o HDR foi
   desligado?" é **`Acting()`**; quem responde "deu errado?" é `LastAttempt() == TENT_FALHOU`.

---

## 1. Dois caminhos, um sintoma

| | fonte janela / monitor | fonte placa de captura / console |
|---|---|---|
| quem tem o HDR | o Windows, no monitor onde está o alvo | o console, do outro lado do HDMI |
| como estraga | o frame pool é BGRA8 (SDR), então o compositor converte a janela HDR para SDR e entrega o resultado — antes de o ReShade existir no caminho | a placa entrega códigos PQ/BT.2020 e a cadeia os trata como gamma SDR BT.709 |
| dá para desligar? | **sim**, e só naquele monitor | **não** — não há API nossa do outro lado do cabo |
| o que o app faz | desliga o HDR do monitor enquanto roda, religa ao sair | converte PQ/BT.2020 → SDR BT.709 no pixel shader, antes do ReShade |

O sintoma que o usuário relatou é o mesmo nos dois: imagem lavada, acinzentada, sem preto e
sem branco. E nos dois casos nenhum ajuste do RenoDX desfaz — o estrago acontece antes.

---

## 2. O que a máquina respondeu

### 2.1 Display — dois monitores, os dois em HDR

| | monitor 0 | monitor 1 |
|---|---|---|
| GDI / nome | `\\.\DISPLAY1` · **LG ULTRAWIDE** | `\\.\DISPLAY2` · **25G64** (TCL) |
| área de trabalho | 2867×1200 em (1920,0) | 1920×1080 em (0,0) |
| DXGI `ColorSpace` | `12` = `RGB_FULL_G2084_NONE_P2020` (HDR10 PQ) | `12`, idem |
| bits / pico / tela cheia | 10 · 302 / 302 nits | 10 · 604 / 604 nits |
| **branco SDR** | **240 nits** | **240 nits** |
| API disponível | `GET_ADVANCED_COLOR_INFO_2` + `SET_HDR_STATE` (Win11 24H2+) | idem |

Os 2867×1200 são os 3440×1440 com a escala de 120% do Windows.

**O achado fora do roteiro:** `GET_SDR_WHITE_LEVEL` devolve **240 nits** nos dois (o padrão do
Windows é 80). O controle *"Brilho do conteúdo SDR"* está a três quartos do curso, e todo
conteúdo SDR dentro do desktop HDR é exibido 3× mais claro que a referência. Soma-se aos
quatro sliders neurais no máximo de `docs\brilho-witcher3.md` §2 — e é exatamente o número que
o caminho FP16 da §8 precisa para normalizar sem chutar. O módulo já o lê
(`Info::sdrWhiteNits`).

### 2.2 Ciclo real de desligar/religar

```
HDR: desligado em '25G64' em 380 ms   ->  DXGI hdr=0 cs=0  bits=8
(3 s parado)
HDR: restaurado em '25G64' em 358 ms  ->  DXGI hdr=1 cs=12 bits=10     OK: o HDR voltou.
```

**~380 ms para desligar, ~358 ms para religar**, chamada bloqueante — só retorna com o modo
aplicado. A tela pisca **uma vez por troca**, como no `Win+Alt+B`: é um modeset. Duas trocas
por sessão do app, nada por frame. Resolução e arranjo dos monitores não mudam — só o formato
de saída (10→8 bits) e o espaço de cor. Como todo modeset, ele acorda swapchains; por isso a
integração desliga **antes** de criar o device e a sessão de captura.

O ciclo foi no monitor secundário de propósito: o app estava rodando em primeiro plano no
outro, e não valia interromper o teste de outro agente.

### 2.3 A sonda da placa de captura — e o achado que muda o diagnóstico

Uma sonda somente-leitura (`GetNativeMediaType` em todos os modos, sem `SetCurrentMediaType`,
sem `ReadSample`, sem iniciar streaming) na **ezcap Game Link RAW**:

```
 [ 0] 1920x1080 @ 120.00  NV12  | prim=2 BT.709 | matriz=1 BT.709 | faixa=2 16-235
 [ 1] 1920x1080 @  60.00  NV12  | prim=2 BT.709 | matriz=1 BT.709 | faixa=2 16-235
 [ 2] 3840x2160 @  30.00  NV12  | prim=2 BT.709 | matriz=1 BT.709 | faixa=2 16-235
 [ 3] 2560x1440 @  60.00  NV12  | prim=2 BT.709 | matriz=1 BT.709 | faixa=2 16-235
 [ 4] 1280x720  @  60.00  NV12  | ...
 [ 5] 1920x1080 @  60.00  YUY2  | ...
 [ 6] 2560x1440 @  50.00  YUY2  | ...
 [ 7] 1280x720  @  60.00  YUY2  | ...
 [ 8] 1920x1080 @  30.00  RGB32 | ...
 [ 9] 1280x720  @  60.00  RGB32 | ...
```

Duas conclusões, e as duas importam:

1. **Todos os dez modos declaram exatamente a mesma coisa: BT.709, matriz BT.709, 16-235.**
   Nenhum menciona PQ, HLG ou BT.2020 — nem o 4K30, que é justamente o modo por onde um
   console HDR entraria. Isto é o preenchimento padrão que a placa despeja em tudo,
   independentemente do que chega pelo HDMI. **Os metadados desta placa não têm valor
   diagnóstico nenhum.** É exatamente o "etiquetam errado" antecipado, confirmado com a lista
   na mão.
2. **Não existe nenhum modo de 10 bits.** NV12 e YUY2 são 8 bits; RGB32 idem. Sem P010, sem
   Y210, sem v210. **A ezcap Game Link RAW é uma placa de 8 bits.** Isso decide a pergunta 4
   do enunciado — ver §8.

O nome "RAW" se refere a entregar vídeo não comprimido (NV12/YUY2 em vez de MJPEG/H.264), não
a profundidade de bits.

---

## 3. A matemática do estrago — por que "lavado" é literal

A curva PQ (SMPTE ST 2084) é absoluta: cada código representa uma luminância fixa. Calculado
da própria curva:

| luminância | código PQ | o que a cadeia SDR entende |
|---|---|---|
| 0,1 nit (preto de cena) | **0,062** | cinza escuro, não preto |
| 100 nits | 0,508 | meio-tom |
| **203 nits** (branco de referência HDR, BT.2408) | **0,581** | cinza médio-claro |
| 1000 nits (pico típico de jogo) | 0,752 | quase branco |
| 10000 nits | 1,000 | branco |

**A imagem inteira fica espremida entre 6% e 58%.** Não há preto, não há branco, o contraste
some. Isso não é uma impressão — é a definição da curva.

Simulei o caminho completo (cena SDR → mastigada em PQ com branco em 203 nits → matriz YCbCr
BT.709 aplicada a um sinal BT.2020 → quantização em 8 bits) e medi o que a cadeia recebe hoje:

| cor | deveria ser | chega como | erro |
|---|---|---|---|
| preto | 0,000 | 0,000 | — |
| cinza 18% | 0,460 | 0,412 | −0,048 |
| cinza 50% | 0,730 | 0,510 | −0,220 |
| **branco** | **1,000** | **0,580** | **−0,420** |
| pele (canal G) | 0,640 | 0,486 | −0,154 |
| céu azul (G) | 0,550 | 0,443 | −0,107 |
| **vermelho forte (R)** | **0,900** | 0,333 · e o R sobe de 0,100 para 0,333 no canal errado | saturação destruída |

O branco perde 42% e o vermelho forte perde metade da saturação. É o retrato exato da queixa.

---

## 4. `hdr_control.h` — o módulo

Header único, sem dependência de nada do projeto. Linka com `user32` (implícito) e `dxgi`
(já em `build-mingw.sh`). Compila limpo com `-Wall -Wextra` incluído depois de
`screen_capture.h`, com o preâmbulo exato do `main.cpp` — testado, não suposto.

### 4.1 Detecção do display, por duas vias

**DXGI** (`Hdr::QueryDxgi(HMONITOR)`): `IDXGIOutput6::GetDesc1()` — é o que o pipeline de
render enxerga: `ColorSpace`, `BitsPerColor`, luminâncias e primárias do painel.

**DisplayConfig** (`Hdr::Query(const Target&)`): é o que o sistema acha, e a única via que
também **escreve**. Duas gerações:

| | Win10 1803+ | Win11 24H2+ |
|---|---|---|
| ler | `GET_ADVANCED_COLOR_INFO` (9) | `GET_ADVANCED_COLOR_INFO_2` (15) |
| escrever | `SET_ADVANCED_COLOR_STATE` (10) | `SET_HDR_STATE` (16) |

O módulo prefere a nova e cai na velha se ela não responder — a escolha é **por tentativa**,
não por número de build. O motivo não é modismo: no 24H2 o "advanced color" legado passou a
englobar HDR **e** WCG/ACM, então desligar pelo caminho antigo derruba os dois; `SET_HDR_STATE`
mexe só no HDR ([xbmc/xbmc#26096](https://github.com/xbmc/xbmc/pull/26096) chega à mesma
conclusão). As structs das chamadas novas só existem no `wingdi.h` sob
`NTDDI_VERSION >= NTDDI_WIN11_GA` e o projeto compila com `_WIN32_WINNT 0x0A00`: estão
declaradas à mão, com os campos de bit achatados em um `UINT32` — mesma tática da
`IGraphicsCaptureSession5` no `screen_capture.h`.

**Por monitor, não global.** `TargetsForMonitor` casa o `viewGdiDeviceName` do DisplayConfig
com o `MONITORINFOEXW.szDevice`. Devolve vetor: em clone, mais de uma saída física divide o
mesmo *source*. A pergunta direta tem uma linha:
`Hdr::Manager::IsWindowOnHdrMonitor(hwnd)`.

### 4.2 Detecção da fonte de vídeo (Media Foundation)

`Hdr::VideoColorFromMediaType(IMFMediaType*)` lê os quatro atributos. **Atenção ao nome:** é
`MF_MT_TRANSFER_FUNCTION` — `MF_MT_VIDEO_TRANSFER_FUNCTION` não existe (o do enunciado). Os
outros três são `MF_MT_VIDEO_PRIMARIES`, `MF_MT_YUV_MATRIX` e `MF_MT_VIDEO_NOMINAL_RANGE`.

A regra que o módulo aplica, e o porquê:

- **`MFVideoTransFunc_2084` (PQ) ou `_HLG` → acredite.** Ninguém marca PQ por acidente. É o
  único campo que decide sozinho (`trustworthy = true`).
- **`MFVideoPrimaries_BT2020` ou matriz `BT2020_10/12` → forte indício**, mas sozinho não
  fecha (uma placa pode declarar gamut largo com transferência SDR).
- **BT.709 / matriz 709 / 16-235 → não prova nada.** É o preenchimento padrão de praticamente
  toda placa USB, como a ezcap acabou de demonstrar em dez modos seguidos. **"Não declarou
  HDR" nunca prova SDR.**

### 4.3 Quando os metadados mentem: a heurística de conteúdo

Como a via dos metadados está fechada nesta placa, sobra olhar o pixel. Um sinal PQ lido como
SDR tem assinatura estatística específica, que sai da tabela da §3: **o histograma não alcança
o branco nem o preto**. O discriminante forte é o percentil alto — qualquer jogo tem HUD, menu
ou céu encostando em 1,0 quando o sinal é SDR de verdade.

`Hdr::MeasureBgra(...)` amostra o buffer BGRA que o `Capture` já mantém (passo 7 ≈ 40 mil
amostras em 1080p, dezenas de microssegundos) e devolve p01/p50/p99/média.
`Hdr::GuessPq(stats)` opina. Medido com dois quadros sintéticos, mesma cena:

| quadro | p01 | p50 | p99 | veredito |
|---|---|---|---|---|
| SDR normal | 0,004 | 0,494 | **0,996** | não suspeito |
| PQ lido como SDR | 0,071 | 0,424 | **0,576** | suspeito, confiança 0,93 |

Onde ela **não** decide, e diz isso: cena muito escura (p99 < 0,35 → "escura demais para
decidir"). E há um falso amigo que ela já descarta pelo p99: faixa 16-235 não expandida também
levanta o preto para 0,063 — mas leva o branco a 0,92, não a 0,58.

Isto **acende o aviso**; quem confirma é o usuário, marcando a caixa e vendo a imagem mudar na
hora. É por isso que a caixa precisa ser alternável em runtime.

### 4.4 O relato de campo: "não consegui desligar" no segundo ciclo

Depois da integração, o app passou a responder `"HDR ligado · nao consegui desligar"` na
execução seguinte a uma em que o desligamento tinha funcionado. **O desligamento não estava
falhando — a mensagem é que estava errada**, e foi reproduzida com zero modesets:

```
A) ini limpo (tratar ausente):     IsEnabled=0  ->  "HDR ligado neste monitor · a imagem chega lavada"
B) grava tratar=1 (o que a sessao anterior deixou)
C) Treatment novo, Init + Detect:  IsEnabled=1  ->  "HDR ligado · nao consegui desligar"   <-- sem ter tentado nada
```

**Causa, em duas linhas:** `StatusLine()` inferia falha do estado da caixa — se `enabled_` era
verdadeiro e nada havia sido desligado, concluía que a tentativa tinha fracassado. Mas
`enabled_` vem do **ini**, e na segunda execução já nasce marcado, **antes** de qualquer
tentativa. O app agrava isso ao logar a linha antes de chamar `SetEnabled`.

As duas hipóteses do relato foram checadas e **descartadas**: a marca `[hdr] restaurar` era
limpa corretamente (`ini restaurar='' ` depois do ciclo 1), e o `SetDeviceInfo` já
re-enumerava os ids a cada chamada — nada de LUID cacheado. DXGI e DisplayConfig também
concordavam (`hdr=1` / `enabled=1`).

**O que mudou no `hdr_control.h`:**

| # | correção |
|---|---|
| 1 | `Treatment` ganhou um estado real de tentativa (`LastAttempt()`: nenhuma / ok / não precisou / falhou). `StatusLine()` só fala em falha quando houve uma. Marcada-mas-ainda-não-agi virou `"HDR ligado · vou desligar ao iniciar a captura"` |
| 2 | `SetEnabled()` chama `Detect()` antes de agir — deixou de depender da ordem de chamada e de um `Detect()` recente |
| 3 | `Manager::DisableFor` distingue e **loga** os três desfechos; antes ele saía em silêncio quando não havia nada a fazer, que foi o que tornou o diagnóstico difícil |
| 4 | `DisableFor` revalida o `HMONITOR` (um modeset — inclusive o nosso — pode invalidá-lo) e cai para `MonitorFromWindow` se o cacheado não responde mais |
| 5 | Se DXGI e DisplayConfig discordarem, tenta desligar assim mesmo: desligar o que já está desligado é no-op barato, perder a janela não é |
| 6 | `Restore()` que falha agora **mantém** a marca no ini, para o próximo início tentar de novo. Antes ele limpava a marca mesmo tendo falhado, e o HDR ficaria desligado sem ninguém sabendo |
| 7 | `InstallSafetyNets` tinha um `static bool` que impedia um segundo `Manager` de se registrar em `Self()`, deixando-o sem rede de segurança. Os ganchos globais continuam instalados uma vez só |

**Validado com dois ciclos completos** (quatro modesets, monitor secundário): primeiro
desligamento 338 ms, restauração 348 ms, **segundo desligamento 381 ms**, restauração 373 ms.
DXGI confirmou `hdr=1 bits=10` → `hdr=0 bits=8` → `hdr=1 bits=10` em cada etapa, `SetEnabled(true)`
repetido não desligou duas vezes, e o ini ficou limpo. Estado final: HDR ligado nos dois
monitores.

### 4.5 Restauração: quatro redes

1. **Destrutor do `Hdr::Manager`** (RAII).
2. **`atexit`**, instalado no primeiro `DisableFor`.
3. **`SetUnhandledExceptionFilter`**, encadeando no filtro anterior.
4. **O `ini`** — a única que sobrevive a `TerminateProcess`, tela azul e queda de energia.

A quarta é o "caso feio". Antes de desligar qualquer coisa o módulo grava
`[hdr] restaurar=<monitorDevicePath>`; a chave é apagada **depois** da restauração. Se ela
existe num início, o app morreu no meio — `RecoverStale()` religa e limpa. O identificador é o
`monitorDevicePath` e não o par `(adapterId, targetId)`: o `adapterId` é um LUID que muda a
cada boot, e o caso que mais interessa (app morreu, usuário reiniciou) é justamente o que o
LUID não sobrevive. Testado, inclusive com uma entrada morta na lista, que é ignorada sem erro.

---

## 5. A caixinha — semântica e textos finais

**Atenção à mudança de semântica trazida pela §8.2:** a **conversão de HDR não passa pela
caixa**. Quando o monitor do alvo está em HDR, a captura já abre em FP16 e o shader já
converte — automático, seguro, sem tocar no display de ninguém. A caixa ficou sendo o **plano
B**: desligar o HDR do monitor enquanto o app roda.

**Uma caixa só**, cobrindo as duas fontes, **desmarcada por padrão**. Persistida em
`dlss5forall.ini` como `[hdr] tratar=0|1`.

- **A detecção acontece sempre**, marcada ou não: é barata e não mexe em nada.
- **A conversão da fonte de tela acontece sempre** que o monitor está em HDR, marcada ou não.
- **Marcada, fonte de tela**: além de converter, desliga o HDR do monitor enquanto o app roda e
  religa ao sair. Serve quando a conversão não basta — tipicamente um jogo que escolheu HDR na
  inicialização.
- **Marcada, fonte de vídeo**: autoriza a conversão PQ/BT.2020. Ali a caixa **é** necessária,
  porque a detecção é palpite (§4.2, §4.3) e um falso positivo estragaria a imagem.
- **Alternável em runtime**, pelo painel. Na fonte de vídeo é instantâneo e reversível (muda um
  cbuffer, nada é recriado). Na fonte de tela custa os ~380 ms e um piscar.

### Rótulo — `Hdr::Txt::Caixa()`

```
Desligar o HDR do monitor enquanto captura (plano B)
```

### Tooltip do seletor — `Hdr::Txt::Tooltip()`

```
NÃO precisa marcar isto para o HDR funcionar: quando o monitor está em HDR o app
já captura em alta precisão e converte a imagem sozinho, sem mexer em nada seu.

Esta caixa é o plano B: além de converter, desliga o HDR do monitor onde está o
alvo enquanto o app roda, e religa sozinho ao sair. Serve quando a imagem ainda
sai errada - por exemplo um jogo que decidiu usar HDR e não gostou do resultado.
Custa um piscar de tela ao ligar e outro ao sair.

Em placa de captura ou console isto não desliga nada (o HDR está do outro lado do
cabo): ali a caixa autoriza a conversão de PQ/BT.2020, que é um palpite e por isso
pede sua confirmação.
```

### Hint longo / rodapé do painel — `Hdr::Txt::Explicacao()`

```
Imagem lavada, cinzenta, sem preto e sem branco quase sempre é HDR no caminho.
O app trata isso sozinho, de um jeito diferente para cada fonte:

  - jogo / janela / monitor: se o monitor está em HDR, a captura abre em 16 bits
    por canal e o app converte a imagem para SDR na hora, com o brilho de papel
    real do seu monitor. É automático, não mexe no seu display e não tem ajuste
    para fazer. É também o caminho mais fiel: a cor chega sem passar pelo
    conversor genérico do Windows.

  - placa de captura / console / câmera: o HDR vem do console pelo HDMI e não há
    o que desligar aqui. O app reconhece pelos dados da placa ou pela cara da
    imagem e converte (PQ / BT.2020 para SDR) - mas como aí é palpite, ele pede
    sua confirmação na caixa. Pode ligar e desligar com o jogo rodando.

A caixa "Desligar o HDR do monitor" é o plano B, para quando a conversão não
basta. Ela desliga o HDR só do monitor onde está o alvo, enquanto o app roda, e
religa sozinho ao fechar.

Duas ressalvas honestas:
  - jogo que já está aberto costuma decidir se usa HDR só na inicialização. Se
    você marcar a caixa com o jogo aberto, pode ser preciso reiniciar o jogo.
  - no console, ponha a saída em SDR se puder: a placa de captura é de 8 bits de
    qualquer jeito, então HDR nela é perda sem ganho nenhum.
```

### Informativo quando a conversão está ativa — `Hdr::Txt::ConversaoAtiva()`

```
Monitor em HDR: capturando em 16 bits e convertendo na hora.
Não precisa fazer nada - é automático e não mexe no seu display.
```

### Aviso — `Hdr::Txt::Aviso(bool fonteDeTela)`

Só aparece quando **ninguém** está tratando. Com a conversão automática ativa, `WarnLine()`
devolve vazio: não há o que avisar, o problema já está resolvido.

Fonte de tela (o caso raro em que o pool FP16 não abriu):
```
Esse monitor está em HDR e a captura não conseguiu abrir em alta precisão,
então a imagem chega já convertida pelo Windows - lavada.
Marque "Desligar o HDR do monitor" para o app resolver,
ou aperte Win+Alt+B para desligar o HDR na mão.
O jogo já aberto pode precisar reiniciar para perceber.
```

Fonte de vídeo:
```
Parece que a placa está recebendo HDR do console - a imagem chega lavada.
Marque a caixa de HDR para o app converter,
ou ponha a saída do console em SDR (é o melhor: a placa é de 8 bits).
```

### Linha do painel — `Hdr::Treatment::StatusLine()`

| situação | texto |
|---|---|
| tela, convertendo (o caso normal em HDR) | `monitor em HDR · capturando em 16 bits e convertendo (automático)` |
| tela, convertendo **e** HDR desligado pelo app | `convertendo em 16 bits · e HDR do monitor desligado pelo app` |
| tela, HDR desligado pelo app (sem FP16) | `HDR desligado pelo app · volta sozinho ao sair` |
| tela, detectado, sem conversão nem caixa | `HDR ligado · a captura não abriu em 16 bits, imagem lavada` |
| tela, caixa marcada, ainda não agiu | `HDR ligado · vou desligar ao iniciar a captura` |
| tela, tentou e falhou | `HDR ligado · nao consegui desligar (Win+Alt+B desliga na mao)` |
| tela, nada a fazer | `sem HDR neste monitor · nada a fazer` |
| vídeo, PQ declarado | `PQ/HDR declarado pela placa · marque para converter` / `· convertendo` |
| vídeo, só a heurística | `imagem com cara de HDR · marque para converter` / `· convertendo` |
| vídeo, nada | `a placa não indica HDR · nada a fazer` |

**A ressalva do jogo já aberto está nos textos de usuário**, com todas as letras. Ela é real: um
jogo decide se usa HDR na inicialização e não costuma reagir a um modeset. Marcar a caixa com o
jogo aberto pode não melhorar nada até reiniciar o jogo — e o texto diz isso em vez de fingir
que resolveu.

---

## 6. Integração — trechos literais

**Tudo isto foi aplicado** — os trechos ficam como registro do que mudou e onde. A–F são o
essencial; G e H são a interface. Onde o texto disser "Tratar HDR automaticamente" como rótulo
da caixa, vale o rótulo novo da §5 (`Hdr::Txt::Caixa()`).

### A. O include — `main.cpp:51`

```cpp
 #include "screen_capture.h"   // modulo de captura de janela / monitor (WGC)
+#include "hdr_control.h"      // deteccao e tratamento de HDR (display e placa de captura)
```

### B. `Fatal()` — `main.cpp:79`

```cpp
 static void Fatal(const std::wstring& msg) {
     Log("FATAL: %s", U8(msg).c_str());
+    Hdr::Manager::RestoreActive();   // ExitProcess nao roda destrutor nem atexit
     MessageBoxW(nullptr, msg.c_str(), APP_TITLE L" - erro", MB_ICONERROR | MB_OK);
     ExitProcess(1);
 }
```

### C. O pixel shader — `main.cpp:342` (`kShader`)

O `PS` de hoje é `float4(tex.Sample(smp, i.uv).rgb, 1.0)`. A conversão entra **nele**, não num
passe novo.

**ANTES**
```cpp
float4 PS(VSOut i) : SV_Target { return float4(tex.Sample(smp, i.uv).rgb, 1.0); }
```

**DEPOIS** — acrescentar o cbuffer logo abaixo do `SamplerState smp`, e trocar o `PS`:

```hlsl
// Conversao de HDR da fonte (placa de captura / console). Preenchido pelo app a partir
// do Hdr::ShaderParams. Com HdrMode == 0 o shader nao faz nada - e o caminho de hoje.
cbuffer HdrCB : register(b0)
{
    int   HdrMode;      // 0 = nada, 1 = PQ/BT.2020 (HDR10), 2 = HLG/BT.2020
    int   HdrFixMatrix; // 1 = desfazer a matriz YCbCr BT.709 aplicada a um sinal BT.2020
    float HdrRefWhite;  // nits que viram 1.0 na saida (BT.2408: 203)
    float HdrPeak;      // pico assumido do conteudo, para o roll-off (ex.: 1000)
    float HdrKnee;      // acima disto comeca a compressao (>= 1.0 = sem roll-off)
    float3 _hdrPad;
};

// ---- EOTF SMPTE ST 2084 (PQ): codigo 0..1 -> luz absoluta em nits (0..10000)
float3 PqToNits(float3 e)
{
    const float m1 = 0.1593017578125, m2 = 78.84375;
    const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    float3 p = pow(max(e, 0.0), 1.0 / m2);
    return 10000.0 * pow(max(p - c1, 0.0) / max(c2 - c3 * p, 1e-6), 1.0 / m1);
}

// ---- OETF inversa da ARIB STD-B67 (HLG) -> cena normalizada 0..1
float3 HlgToScene(float3 e)
{
    const float a = 0.17883277, b = 0.28466892, c = 0.55991073;
    return (e <= 0.5) ? (e * e) / 3.0 : (exp((e - c) / a) + b) / 12.0;
}

float4 PS(VSOut i) : SV_Target
{
    float3 col = tex.Sample(smp, i.uv).rgb;

    if (HdrMode != 0)
    {
        // 1. A placa etiquetou BT.709 e o conversor desmatriciou o YCbCr com os
        //    coeficientes errados. Desfazer, ainda no dominio NAO-linear.
        if (HdrFixMatrix != 0)
        {
            col = float3(
                dot(col, float3( 0.949900,  0.045506,  0.004594)),
                dot(col, float3(-0.054223,  1.038105,  0.016118)),
                dot(col, float3(-0.002956, -0.009944,  1.012900)));
        }

        // 2. Curva de transferencia -> luz linear, em nits.
        float3 nits;
        if (HdrMode == 1) nits = PqToNits(col);
        else              nits = HlgToScene(max(col, 0.0)) * 1000.0;   // HLG: 1.0 = pico nominal

        // 3. Primarias BT.2020 -> BT.709, em luz LINEAR.
        float3 lin = float3(
            dot(nits, float3( 1.660491, -0.587641, -0.072850)),
            dot(nits, float3(-0.124550,  1.132900, -0.008349)),
            dot(nits, float3(-0.018151, -0.100579,  1.118730)));
        lin = max(lin, 0.0);     // cores fora do gamut BT.709 viram negativas

        // 4. Branco de referencia -> 1.0.
        float3 v = lin / max(HdrRefWhite, 1.0);

        // 5. Roll-off nas altas, so na luma, para nao girar matiz.
        //    Reinhard estendido: mapeia [knee, peak/ref] -> [knee, 1] com f(W) = 1 exato.
        float y = dot(v, float3(0.2126, 0.7152, 0.0722));
        if (y > HdrKnee)
        {
            float W = max(HdrPeak / max(HdrRefWhite, 1.0) - HdrKnee, 1e-3);
            float t = y - HdrKnee;
            float s = saturate(t * (1.0 + t / (W * W)) / (1.0 + t));
            float ny = HdrKnee + (1.0 - HdrKnee) * s;
            v *= ny / max(y, 1e-6);
        }
        v = saturate(v);

        // 6. De volta a display-referred sRGB - que e o que o ReShade, o LumeniteFX,
        //    o DLSS e o Neural Rendering esperam ver.
        col = (v <= 0.0031308) ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
    }

    return float4(col, 1.0);
}
```

Compilado de verdade com `D3DCompile`, nos dois perfis que interessam: `vs_4_0`/`ps_4_0` (os
que o `InitRest` usa, `main.cpp:527`) e `ps_5_0`. **Zero avisos.** O `PS` passa de
`Approximately 4 instruction slots used` para `Approximately 66`.

**As duas matrizes não são de memória.** Foram calculadas das primárias BT.2020 e BT.709 com
branco D65 pelo método SMPTE RP 177; as linhas somam 1,000 (o branco continua branco). A
matriz da etapa 1 é `Inv2020(Fwd709(·))` sobre os coeficientes de luma não-constantes
(0,2627/0,0593 contra 0,2126/0,0722) — ela desfaz a desmatrização que o Video Processor fez com
os coeficientes errados, e por isso vai **antes** da EOTF, no domínio não-linear. A da etapa 3
é a conversão de primárias, e por isso vai **depois**, em luz linear. Trocar a ordem é o erro
clássico e produz cor errada nos dois casos.

### D. O branco de referência, e o que fazer com conteúdo a 1000 nits

`HdrRefWhite = 203` nits, por padrão. Não é 100. **100 nits é o branco de referência do SDR**
(BT.1886); **203 nits é o branco gráfico de referência em HDR**, definido pela ITU-R BT.2408 —
é onde menus, legendas e o "branco de papel" de um jogo HDR são mastigados, e é o que o próprio
Windows usa como referência. Mapear 100 → 1,0 deixaria a imagem ~1 stop clara demais.

Conteúdo mastigado a 1000 nits não é clipado: entra no roll-off. Medido na implementação:

| luminância da fonte | saída SDR |
|---|---|
| 100 nits | 0,730 |
| **203 nits (branco de referência)** | **0,907** |
| 300 nits | 0,936 |
| 500 nits | 0,966 |
| 1000 nits | 1,000 |
| 2000 nits | 1,000 (saturado) |

O branco de referência sai em 0,907, não em 1,0 — **de propósito**: é o headroom que faz céu e
explosão continuarem visíveis como algo mais claro que o branco de papel, em vez de virarem
chapadão. Quem preferir fidelidade máxima a highlights: `HdrKnee = 1.0` desliga o roll-off e
vira clamp seco (o branco sai exato, o que passa de 1000 nits vira branco chapado). Os dois
comportamentos saem do mesmo código, só mudando o cbuffer.

**Ida e volta verificada.** Simulando o caminho inteiro (cena SDR → PQ/BT.2020 → matriz errada
→ 8 bits → esta conversão), o erro contra a cena original:

| cor | erro |
|---|---|
| preto | 0,0 / 255 |
| cinza 18% | 0,9 / 255 |
| cinza 50% | 1,6 / 255 |
| pele | 1,2 / 255 |
| céu azul | 1,7 / 255 |
| vermelho forte | 3,2 / 255 |
| verde folha | 1,4 / 255 |
| branco | 23,9 / 255 — o roll-off, explicado acima |

**Erro médio 4,2/255**, e tirando o branco (que é escolha de projeto, não erro) fica abaixo de
3,2/255 — ou seja, dentro do ruído de quantização de 8 bits. A conversão está certa.

**Ressalva honesta:** a derivada tem um kink no joelho (a inclinação cai de 1,0 para 1−knee).
Não é visível em conteúdo normal, mas se aparecer banding no céu, o EETF Hermite do BT.2390 é a
versão sem kink e cabe no mesmo lugar.

### E. O `Renderer` — cbuffer e envio

Campos novos (junto de `ps`, `samp`, `rs`…):

```cpp
    // --- HDR da fonte de video: cbuffer b0 do PS. Espelha o Hdr::ShaderParams.
    struct HdrCBData { int mode, fixMatrix; float refWhite, peak, knee, pad0, pad1, pad2; };
    ID3D11Buffer* hdrCB = nullptr;
    HdrCBData     hdrData{ 0, 1, 203.f, 1000.f, 0.75f, 0.f, 0.f, 0.f };
    bool          hdrDirty = true;
    void SetHdrParams(const Hdr::ShaderParams& p) {
        hdrData.mode = p.mode; hdrData.fixMatrix = p.fixMatrix;
        hdrData.refWhite = p.refWhiteNits; hdrData.peak = p.peakNits; hdrData.knee = p.knee;
        hdrDirty = true;
    }
```

`sizeof(HdrCBData)` é 32 bytes, múltiplo de 16, e o layout casa campo a campo com o `cbuffer`
do HLSL (`int,int,float,float` no primeiro vetor de 16 bytes; `knee` + padding no segundo).

Criação, em `InitRest` logo depois do `CreateSamplerState` (`main.cpp:536`):

```cpp
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(HdrCBData);
        cb.Usage = D3D11_USAGE_DYNAMIC; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&cb, nullptr, &hdrCB);   // se falhar, o PS ve zeros = HdrMode 0
```

Envio, em `DrawAndPresent` entre o `PSSetShader` e o `Draw` (`main.cpp:583`):

```cpp
         ctx->PSSetShader(ps, nullptr, 0);
+        if (hdrCB) {
+            if (hdrDirty) {
+                D3D11_MAPPED_SUBRESOURCE m{};
+                if (SUCCEEDED(ctx->Map(hdrCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
+                    memcpy(m.pData, &hdrData, sizeof(hdrData));
+                    ctx->Unmap(hdrCB, 0);
+                    hdrDirty = false;
+                }
+            }
+            ctx->PSSetConstantBuffers(0, 1, &hdrCB);
+        }
         ctx->PSSetShaderResources(0, 1, &use);
```

E `SafeRelease(hdrCB);` em `Shutdown` (`main.cpp:592`).

### F. `wWinMain` — detecção, decisão, alimentação e encerramento

Depois do primeiro `Log` (por volta da linha 1721):

```cpp
     Log("DLSS5 For All %s iniciando", U8(APP_VERSION).c_str());
+
+    // HDR: liga o log do modulo e desfaz o que uma sessao anterior deixou desligado.
+    // Antes de qualquer captura.
+    Hdr::g_logSink = [](const char* s) { Log("%s", s); };
+    Hdr::Treatment hdr;
+    hdr.Init(IniPath());
+    hdr.RecoverStale();
```

Depois de medir a fonte e **antes** de criar a janela de render (por volta da linha 1867, logo
após `if (!cw || !ch) Fatal(...)`):

```cpp
+    // ---------------------------------------------------------------- HDR da fonte
+    // A deteccao acontece sempre; agir depende da caixa [hdr] tratar do ini.
+    if (C.kind == SRC_CAMERA) {
+        IMFMediaType* mt = nullptr;
+        if (SUCCEEDED(cap.reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &mt)) && mt) {
+            hdr.SetSourceVideo(Hdr::VideoColorFromMediaType(mt));
+            mt->Release();
+        }
+    } else {
+        hdr.SetSourceScreen(C.win, C.mon);
+    }
+    hdr.Detect();
+    hdr.SetEnabled(hdr.IsEnabled());   // aplica o que estava salvo no ini (idempotente)
+    Log("HDR: %s", U8(hdr.StatusLine()).c_str());
+    std::wstring hdrAviso = hdr.WarnLine();
```

O aviso na tela. No modo sobreposto, **anexar** ao toast de boas-vindas (`main.cpp:1966`) —
`ShowToast` tem uma janela só, então um segundo toast substituiria o primeiro:

```cpp
                   + g_hkQuit + L"  sai   -  o icone tambem fica na bandeja"
+                  + (hdrAviso.empty() ? L"" : L"\n\n" + hdrAviso),
+                  hdrAviso.empty() ? 6000 : 9000);
     } else {
         ShowWindow(hwnd, SW_SHOW);
+        if (!hdrAviso.empty()) ShowToast(hi, C.mon, hdrAviso, 8000);
     }
```

No laço principal, alimentar a heurística e manter o shader em dia — só na fonte de câmera, e
só de vez em quando (a cada ~30 frames; o custo é dezenas de microssegundos de CPU):

```cpp
+        if (C.kind == SRC_CAMERA && (presented % 30) == 0) {
+            std::lock_guard<std::mutex> lk(cap.mtx);
+            if (!cap.buf.empty())
+                hdr.FeedFrameStats(Hdr::MeasureBgra(cap.buf.data(), cap.w, cap.h, cap.stride));
+        }
+        R.SetHdrParams(hdr.Shader());
```

`SetHdrParams` é barato (copia 5 valores e marca um bool); o `Map` só acontece quando algo
mudou. Se preferir, chame-o só quando `hdr` mudar de estado — dá no mesmo.

No encerramento (`main.cpp:2118`):

```cpp
     if (C.kind == SRC_CAMERA) cap.Stop(); else scap.Stop();
+    hdr.Restore();          // religa o HDR antes de derrubar o device (idempotente)
     R.extSrv = nullptr;
     R.Shutdown();
```

### G. Seletor — a caixa

`enum` de IDs (`main.cpp:661`), mais a declaração adiantada do `IniPath()`, que hoje só existe
depois do seletor:

```cpp
 enum { ID_KIND = 100, ID_DEV = 101, ID_MODE = 102, ID_FS = 103, ID_START = 104,
-       ID_REFRESH = 105, ID_CURSOR = 106, ID_HIDE = 107, ID_OVERLAY = 108 };
+       ID_REFRESH = 105, ID_CURSOR = 106, ID_HIDE = 107, ID_OVERLAY = 108,
+       ID_HDR = 109 };
+static std::wstring IniPath();   // definido logo depois do seletor
```

Campo em `SelState` (`main.cpp:669`): `HWND hHdr = nullptr;`

O controle (`main.cpp:857`). O `hint` desce 26 px e perde 26 px de altura — cabe sem mexer no
tamanho da janela:

```cpp
     st.hOverlay = mk(L"BUTTON", L"SOBREPOR  -  ...", BS_AUTOCHECKBOX | WS_TABSTOP, 16, 238, 628, 22, ID_OVERLAY);
+    st.hHdr     = mk(L"BUTTON", Hdr::Txt::Caixa(), BS_AUTOCHECKBOX | WS_TABSTOP, 16, 264, 628, 22, ID_HDR);
-    st.hHint    = mk(L"STATIC", L"", 0, 16, 266, 628, 148, 0);
+    st.hHint    = mk(L"STATIC", L"", 0, 16, 292, 628, 122, 0);
```

Marcar conforme o ini, antes de `FillTargets` (`main.cpp:870`):

```cpp
+    if (Hdr::TreatmentEnabledFromIni(IniPath()))
+        SendMessageW(st.hHdr, BM_SETCHECK, BST_CHECKED, 0);
```

Gravar no `ID_START`, junto dos outros campos (`main.cpp:1088`):

```cpp
             s->ch.hideSelf = SendMessageW(s->hHide, BM_GETCHECK, 0, 0) == BST_CHECKED;
+            Hdr::SetTreatmentEnabledInIni(IniPath(),
+                SendMessageW(s->hHdr, BM_GETCHECK, 0, 0) == BST_CHECKED);
```

Opcional, e é o detalhe que faz diferença: rótulo que conta o estado **agora**, dentro de
`FillModes`, no ramo `k == SRC_WINDOW` (onde `di` já é o índice da janela):

```cpp
+        if (s->hHdr) {
+            const bool on = (di >= 0 && di < (int)s->wins.size())
+                          && Hdr::Manager::IsWindowOnHdrMonitor(s->wins[di].hwnd);
+            SetWindowTextW(s->hHdr, on
+                ? L"Tratar HDR automaticamente  -  esse monitor esta em HDR, e o que deixa a imagem lavada"
+                : Hdr::Txt::Caixa());
+        }
```

O tooltip vem de `Hdr::Txt::Tooltip()`; o texto longo, de `Hdr::Txt::Explicacao()` no `hHint`.

### H. Painel

`enum EKind` (`main.cpp:1154`) ganha `E_HDR`; o de comandos (`main.cpp:1097`) ganha
`CMD_HDR = 303`. Dois campos em `HudState`: `bool hdrOn = false, hdrActing = false;` e um
`std::wstring hdrStatus;`. Um caso em `Hud::Activate`: `case E_HDR: g_trayCmd = CMD_HDR; break;`

O cartão, no `Render`, logo depois do bloco *Neural Rendering* — mesma forma dos outros:

```cpp
    // -------- HDR
    {
        RectF r(L, cy, IW, 64);
        const bool en = g_hud.hdrOn || g_hud.hdrActing;
        const bool on = g_hud.hdrActing;
        const int idx = (int)g_items.size();
        Item it{ r, E_HDR, 0, en, on }; g_items.push_back(it);
        if (g) {
            FillRound(g, r, 12.f, (g_hot == idx) ? C_ROWH : C_ROW);
            if (on) FillRound(g, RectF(r.X, r.Y + 14, 4.f, r.Height - 28), 2.f, C_ON);
            Txt(g, L"Tratar HDR", fItem, en ? C_TXT : C_FAINT,
                RectF(r.X + 18, r.Y + 13, r.Width - 160, 22), 0);
            Txt(g, g_hud.hdrStatus.c_str(), fItemSub, C_DIM,
                RectF(r.X + 18, r.Y + 35, r.Width - 160, 18), 0, true);
            RectF pl(r.X + r.Width - 130, r.Y + 15, 112, 34);
            FillRound(g, pl, 17.f, on ? C_ON : C_OFF);
            Txt(g, !en ? L"—" : on ? L"TRATANDO" : L"TRATAR", fBadge, on ? C_DARK : C_TXT, pl, 1);
        }
        cy += 64 + 10;
    }
```

O comando, no laço principal, ao lado de `TRAY_BYPASS` (`main.cpp:2010`):

```cpp
        } else if (cmd == CMD_HDR) {
            const bool novo = !hdr.IsEnabled();
            hdr.SetEnabled(novo);
            g_hud.hdrActing = hdr.Acting();
            g_hud.hdrStatus = hdr.StatusLine();
            R.SetHdrParams(hdr.Shader());
            Hud::Refresh();
            ShowToast(hi, C.mon, hdr.StatusLine(), 2500);
        }
```

E, na atualização de 1 s do laço, manter o painel em dia:
`g_hud.hdrOn = hdr.HdrFound(); g_hud.hdrActing = hdr.Acting(); g_hud.hdrStatus = hdr.StatusLine();`

**Ressalva:** alternar pelo painel na **fonte de tela** faz o modeset com a captura já rodando —
é o único ponto desta entrega que não foi testado ao vivo. O esperado é que a sessão WGC e a
swapchain sobrevivam (o formato do pool não muda, o tamanho do item não muda), mas pode haver
um frame perdido ou um susto no ReShade. Na **fonte de vídeo** não há risco nenhum: muda um
cbuffer, nada é recriado, e é justamente onde ligar/desligar vendo o efeito é mais útil.

---

## 7. Latência e qualidade — os números, conferidos

O usuário perguntou se isso adiciona latência ou tira qualidade. Confirmo uma parte e **corrijo
outra**.

### O que estava impreciso

Os **0,12–0,19 ms** citados como "banda de cópia" são, no `docs\latencia.md` §2, a etapa
`tick` — definida ali mesmo como *"drenar a fila + medir geometria + `CopySubresourceRegion`"*.
Três coisas: é **tempo de CPU**, não de GPU; inclui bem mais que a cópia; e o
`CopySubresourceRegion` é uma chamada assíncrona — o custo real da cópia acontece depois, na
GPU, e não aparece nesse número. Consequência prática: **esse número quase não muda com o
formato**, porque é quase todo overhead fixo de API. Dobrar bytes por pixel não o dobra.

### O custo real, estimado com a conta na mesa

| item | 1920×1080 | 3440×1440 |
|---|---|---|
| cópia BGRA8 hoje (ler+escrever, 717 GB/s teóricos, ~60% de eficiência) | ~39 µs | ~92 µs |
| a mesma cópia em FP16 | ~78 µs | ~184 µs |
| **delta se fôssemos para FP16** | **~+0,04 ms** | **~+0,09 ms** |

Contra o orçamento medido de um frame — `present` 0,87 ms de CPU e `PRESENT→TELA` 41,5 ms —
isso é **0,1–0,2%**. Desprezível: confirmado, mas pelo motivo certo.

### O tone-map: nenhum passe extra

**Não é um passe.** Entra no `PS` que já existe e já roda uma vez por frame. Medido no
compilador: de **4 para 66 instruction slots**. Estimativa de custo (não medida na GPU): ~66
slots × 2,07 M pixels em 1080p, com dois `pow` por canal indo para as unidades de função
especial, dá **da ordem de 20–90 µs**; em 3440×1440, **50–200 µs**. Sem passe extra, sem cópia
extra, sem alocação, sem sincronização.

E quando está desligado o custo é **zero**: `HdrMode` vem de um cbuffer, o branch é uniforme
para todo o quadro e a GPU pula o bloco inteiro.

### Qualidade

Não tira; **repõe**. Hoje, sem conversão, o branco chega a 0,580 em vez de 1,000 e o vermelho
forte perde metade da saturação (§3). Com a conversão, o erro contra a cena original fica em
1–3/255 — dentro do ruído de quantização de 8 bits (§6.D).

O que a conversão **não** recupera: os bits que a placa já jogou fora. Ela entrega 8 bits;
esticar de volta a faixa [0,06 … 0,58] para [0 … 1] é um ganho de ~1,8× em contraste, e com
ele os degraus de quantização também crescem — na prática restam **~135 níveis efetivos dos
256**. Pode aparecer banding em gradiente liso (céu, fumaça). É melhor que a imagem lavada, mas
é a razão pela qual **pôr o console em SDR continua sendo a melhor solução**, e o texto da
caixinha diz isso ao usuário.

---

## 8. FP16 — o veredito, agora com a placa de captura na conta

### 8.1 A pergunta 4: 10 bits em PQ perdidos na captura de 8 bits?

**Não se aplica a esta placa, e o motivo é definitivo:** a ezcap Game Link RAW não tem nenhum
modo de 10 bits (§2.3). NV12, YUY2 e RGB32, e nada mais. A perda de bits acontece **no hardware
dela**, antes de qualquer decisão nossa. Levar FP16 ao caminho do Media Foundation não
recuperaria um único bit.

E haveria custo: o caminho da câmera é **CPU**, não GPU. O `Capture::Loop` copia linha a linha
para um `std::vector` (8,3 MB por quadro em 1080p) e o `Renderer::Upload` faz `Map` +
`memcpy` para uma textura dinâmica — ~16,6 MB de tráfego de CPU por quadro, da ordem de
milissegundos, não de microssegundos. Dobrar o formato dobraria um custo que **já é real**, em
troca de zero informação. **Para a fonte de placa/câmera: não.**

Isso mudaria com outra placa. Se algum dia entrar uma que exponha `P010`/`Y210` (10 bits), a
conversa recomeça — e aí o certo seria pedir esse formato ao Source Reader e converter YUV→RGB
nós mesmos na GPU, não pedir FP16 ao Video Processor. Fica registrado como condição, não como
plano.

### 8.2 Caminho B — IMPLEMENTADO e medido

Foi feito exatamente como estava proposto: **só** o frame pool e a textura intermediária do WGC
mudam para `R16G16B16A16_FLOAT`, e **só quando o monitor do alvo está em HDR**. Backbuffer,
ReShade, DLSS, Neural Rendering e o resto da cadeia continuam idênticos — o `dlss5-feed.log` do
teste de fumaça confirma: `color B8G8R8A8_UNORM -> output B8G8R8A8_UNORM`.

**O que mudou, por arquivo:**

| arquivo | mudança |
|---|---|
| `screen_capture.h` | campos `hdrFp16` / `hdrFp16Mode` / `hdrSdrWhiteNits` e os métodos `PoolFormat()` / `TexFormat()`. O `Start` pergunta ao `hdr_control.h` se o monitor do alvo está em HDR **antes** de criar o pool (não dá para trocar depois) e lê o paper white real do monitor. `EnsureTex` e `RecreatePool` seguem o mesmo formato, então o redimensionamento não o perde. `DLSS5_FP16=0/1` força para teste |
| `main.cpp` | `HdrMode == 3` no `kShader`: scRGB já é linear e já é BT.709, então só a escala `×80` e o resto do caminho comum. `SetScreenFp16` liga a captura ao shader. Novo `Renderer::SaveBackbufferPng` + `DLSS5_DUMP` para diagnóstico de cor |
| `hdr_control.h` | `VM_SCRGB`, `SetScreenFp16()`, textos e `StatusLine()` refeitos para a nova semântica |

**A semântica que ficou** (é a mudança de comportamento mais visível):

- **A conversão é automática.** Monitor em HDR → pool FP16 → shader converte. Não passa pela
  caixinha, porque não há nada a confirmar: **nós** escolhemos o formato, então sabemos o que
  está chegando. É seguro, não toca no display de ninguém e não tem efeito colateral.
- **A caixa virou o plano B**: desligar o HDR do monitor enquanto o app roda. Rótulo novo,
  *"Desligar o HDR do monitor enquanto captura (plano B)"*.
- **Na placa de captura nada disso muda**: lá a detecção é palpite (§4.2, §4.3) e a caixa
  continua sendo a confirmação para o caminho PQ.
- O log diz as duas coisas separadamente:
  `HDR: detectado=1 conversao=fp16 caixa=0 agindo=0 (...)`.

#### O que os alvos de cor mostraram

Dois alvos escritos para o teste (`hdrtarget.exe`, no scratchpad): uma janela **SDR** com 8
cores conhecidas e uma janela **HDR real** (swapchain `R16G16B16A16_FLOAT` +
`SetColorSpace1(RGB_FULL_G10_NONE_P709)`, confirmado `hr=0` e `sup=0x3`) com patches em
luminância absoluta. A medição é o **dump do backbuffer** (`DLSS5_DUMP`), não um print de tela:
num desktop HDR um print passa pelo tone map do DWM e não mede nada.

**Alvo SDR — a ida e volta.** Antes e depois, com o mesmo binário, só mudando `DLSS5_FP16`:

| patch | esperado | ANTES (8 bits) | DEPOIS (FP16) |
|---|---|---|---|
| preto | 0,000 | 0,000 | 0,000 |
| cinza 18% | 0,460 | 0,459 | 0,459 |
| cinza 50% | 0,730 | 0,729 | 0,729 |
| branco | 1,000 | 1,000 | 1,000 |
| pele | 0,820/0,640/0,540 | 0,820/0,639/0,541 | 0,820/0,639/0,541 |
| céu azul | 0,350/0,550/0,900 | 0,349/0,549/0,902 | 0,349/0,549/0,902 |
| vermelho forte | 0,900/0,100/0,100 | 0,902/0,102/0,102 | 0,902/0,102/0,102 |
| verde folha | 0,200/0,650/0,250 | 0,200/0,651/0,251 | 0,200/0,651/0,251 |
| **erro médio** | — | **0,3/255** | **0,3/255** |

Duas conclusões, e a primeira **corrige uma previsão minha**:

1. **O ANTES não estava lavado.** Uma janela **SDR** num desktop HDR já voltava correta pelo
   caminho de 8 bits (erro 0,3/255). Eu havia previsto branco em 0,58 para este caso — **estava
   errado**. O compositor promove o conteúdo SDR a scRGB pelo paper white e o WGC desfaz
   exatamente isso ao entregar BGRA8; a ida e volta é limpa. O "lavado" é problema de conteúdo
   **HDR**, não de desktop HDR.
2. **O DEPOIS é idêntico ao ANTES**, byte a byte nos oito patches. Ou seja: a mudança **não
   custou nada** no caso que já estava certo — que era o requisito mais importante, porque é o
   caso comum (jogo SDR num desktop HDR).

**Alvo HDR — onde a mudança aparece:**

| patch | ANTES (8 bits) | DEPOIS (FP16) | DEPOIS, joelho 0,75 |
|---|---|---|---|
| 0 nits | 0 | 0 | 0 |
| 80 nits | **255** | **156** | 156 |
| 203 nits (referência) | **255** | **237** | 227 |
| 500 nits | **255** | 255 | **244** |
| 1000 nits | **255** | 255 | 255 |
| vermelho 203 nits | 255, 0, 0 | **237, 0, 0** | 237, 0, 0 |
| verde / azul 203 nits | 255 / 255 | **237** / **237** | 237 / 237 |

Antes, **80, 203, 500 e 1000 nits saíam todos em 255**: tudo acima de 80 nits era destruído
antes de o ReShade ver o frame. Depois há gradação real, e os coloridos a 203 nits mantêm a
saturação em vez de estourar para o primário puro. A prova visual:
**`docs\img\hdr-antes-depois.png`**.

#### Uma decisão de projeto que os números forçaram

O roll-off nas altas **está desligado por padrão na captura de tela** (`knee = 1.0`), e a razão
é geometria pura: em SDR, tudo que está acima do branco de papel tem de caber entre ele e 1,0.
Ou o branco sai em 1,0 (fiel, sem realce) ou sai abaixo (com realce, mais escuro). Não há
terceira opção. Medido: com o roll-off ligado (`knee = 0,75`), o alvo SDR perdia o branco —
1,000 virava **0,906** — enquanto o alvo HDR ganhava 500 nits distinguível (244 em vez de 255).

Para este app o caso comum é conteúdo SDR num desktop HDR, e ele **tem** de voltar idêntico.
Por isso o padrão é fidelidade. Quem captura conteúdo HDR com muito realce liga o roll-off com
`DLSS5_HDR_KNEE=0.75` — os dois comportamentos saem do mesmo código.

Detalhe técnico que custou uma tentativa: cheguei a trocar a Reinhard estendida por uma Hermite
C1 (o joelho do BT.2390), que não tem o kink de inclinação. **Não funciona em domínio linear**:
com muito headroom a spline estoura acima de 1,0 antes de chegar ao pico — medido, com joelho
0,75 e pico de 604 nits, 500 nits saturava em 255. O BT.2390 só escapa disso porque trabalha em
domínio PQ. A Reinhard estendida voltou, por ser limitada e monotônica por construção.

**Caminho C — cadeia FP16 ponta a ponta com saída scRGB: continua não valendo.** O que quebra:
o `LumeniteFX Kernel` casa blocos por gradiente de luma com limiares absolutos escritos para
0..1, e em scRGB a imagem chega em 0..3,75 — arriscar o motion vector, que é o coração do
produto, por precisão de cor é o pior negócio possível; o `VALIDATE_LUMA` do `DLSS5_Feed` tem o
mesmo problema; a LUT do `DLSS5_Grade` colapsa tudo acima de 1,0 no último texel; e
`NRPaperWhiteScale=16`, hoje inerte, passa a valer e **está errado** — o valor certo seria 3,0
(240/80), e com 16 o soft-clip do proxy cairia em 12× o papel branco, ou seja soft-clip nenhum.

Some-se que o diff de `docs\cor-hdr-e-filtros.md` §8 está **desatualizado**: aponta
`main.cpp:419` (`sd.BufferDesc.Format`), mas o `Renderer` foi reescrito e hoje o caminho
preferido é `InitWaitable` com `DXGI_SWAP_CHAIN_DESC1` (por volta da linha 466), e o
`InitLegacy` é outro trecho — que **tem** de continuar em `B8G8R8A8_UNORM`, porque o fallback
`DXGI_SWAP_EFFECT_DISCARD` não aceita FP16. Aplicar aquele diff como está não compila.

O que se ganharia: HDR real na saída, que o app nem apresenta. **Ganho zero para o problema
relatado.**

### 8.3 Confirmado sobre o feeder

A tabela de formatos do `dlss5-feed.addon64` traz `B8G8R8A8_UNORM`, `B8G8R8A8_UNORM_SRGB`,
`R8G8B8A8_UNORM_SRGB`, `R10G10B10A2_UNORM` e **`R16G16B16A16_FLOAT`** — um backbuffer FP16 não
cairia no `backbuffer format ... is not supported`. E o caminho de UAV atual está saudável: o
binário tem o aviso *"B8G8R8A8_UNORM has no typed UAV store on this device; output stays
R8G8B8A8_UNORM (the copy home converts, so expect the washed-out image of issue #11)"*, e ele
**não** aparece no `dlss5-feed.log` desta máquina, que registra `output B8G8R8A8_UNORM`. Esse
caminho de washout conhecido do add-on **não** é o problema aqui. Vale o registro: o nome é
tentador e teria custado horas a alguém.

---

## 9. O que ficou de fora

Tudo o que estava planejado foi implementado: detecção nas duas fontes, o toggle do monitor com
restauração garantida, a conversão PQ/BT.2020 da placa, a captura em FP16 com conversão scRGB
e a caixa do usuário. Sobra isto:

1. **Testar com um jogo HDR de verdade.** Os alvos são sintéticos; eles provam a cadeia, não o
   comportamento de um título específico. É o teste de 30 segundos que só o usuário pode fazer:
   abrir o Witcher 3 em HDR e olhar.
2. **Testar com o console mandando HDR pela ezcap.** Mesma coisa do outro lado: a matemática do
   caminho PQ está validada numericamente, mas não contra a placa real recebendo PQ.
3. **Alternar o HDR do display com a captura já rodando** (o botão do painel, fonte de tela) —
   continua sendo o único caminho sem teste ao vivo.
4. **Caminho C** (cadeia FP16 ponta a ponta com saída scRGB): não agendar, pelos motivos da
   §8.2.

E o conselho que vale mais que tudo isso junto, e que está no texto da caixinha: **no console,
ponha a saída em SDR**. A placa é de 8 bits; HDR nela é perda sem ganho possível.

---

## 10. Verificado, e como

| afirmação | como |
|---|---|
| detecção por monitor, DisplayConfig e DXGI | rodado nesta máquina, dois monitores (§2.1) |
| a API nova (24H2) existe e responde aqui | `GET_ADVANCED_COLOR_INFO_2` retornou `ERROR_SUCCESS` |
| desligar e religar funciona e restaura | um ciclo real; DXGI confirmou `hdr=0 cs=0 bits=8` e depois `hdr=1 cs=12 bits=10` |
| tempo da troca (380 / 358 ms) | `QueryPerformanceCounter` em volta da chamada |
| `RecoverStale` acha pelo `devicePath`, ignora entrada morta, limpa o ini | teste dedicado |
| a caixa vai e volta do ini, padrão desmarcada, idempotente | teste dedicado do `Treatment` nas duas fontes |
| a ezcap declara BT.709 em todos os 10 modos e não tem modo de 10 bits | sonda somente-leitura, saída completa na §2.3 |
| a matemática da conversão | ida e volta simulada, erro médio 4,2/255 (§6.D); matrizes calculadas das primárias, linhas somando 1,000 |
| a curva do roll-off | tabela da §6.D, calculada na implementação |
| a heurística separa SDR de PQ | dois quadros sintéticos: p99 0,996 contra 0,576 (§4.3) |
| o shader compila | `D3DCompile` em `vs_4_0`/`ps_4_0` (os perfis do app) e `ps_5_0`: zero avisos, 4 → 66 instruction slots |
| o header compila no contexto do `main.cpp` | preâmbulo idêntico, `screen_capture.h` e Media Foundation incluídos, flags do `build-mingw.sh`, `-Wall -Wextra`: zero aviso vindo do `hdr_control.h` |
| o feeder aceita `R16G16B16A16_FLOAT`; o washout do issue #11 não está ativo | tabela de formatos do binário e o `dlss5-feed.log` |
| **o pool FP16 abre e a captura roda** | `captura: monitor em HDR -> pool R16G16B16A16_FLOAT (scRGB linear) (paper white 240 nits)` |
| **a ida e volta SDR não regride** | alvo SDR de 8 cores, dump do backbuffer: 0,3/255 antes e 0,3/255 depois, branco em 255 nos dois (§8.2) |
| **o conteúdo HDR deixa de clipar** | alvo scRGB real (`SetColorSpace1` hr=0): 80/203/500/1000 nits saíam todos 255, agora 156/237/255/255 (§8.2) |
| **a saturação para de estourar** | vermelho/verde/azul a 203 nits: 255 puro antes, 237 depois |
| **o roll-off custa o branco** | `DLSS5_HDR_KNEE=0.75`: alvo SDR perde o branco (1,000 → 0,906) e o alvo HDR ganha 500 nits (255 → 244). Daí o padrão ser sem roll-off |
| **a cadeia continua inteira** | teste de fumaça com `dxgi.dll`: `waitable=1`, `MinUpdateInterval 16.00 -> 0.00 ms`, `feature ready: 640x360 DLAA`, `frame N delivered`, e o feeder ainda vendo `color B8G8R8A8_UNORM -> output B8G8R8A8_UNORM` |

**Não verificado:**

- **Trocar o HDR do display com a captura já rodando** (§6.H na fonte de tela). Todo o teste foi
  feito sem sessão WGC viva sobre o monitor alvo.
- **A conversão contra sinal real de console.** Não havia console mandando HDR durante o teste;
  a validação é numérica, com o caminho console→placa simulado. O que ela prova é que a
  matemática está certa — não que a ezcap se comporte como o modelo. O teste de verdade custa
  30 segundos: ligar o console em HDR, marcar a caixa, olhar.
- **O custo do shader na GPU.** É estimativa a partir da contagem de instruções, não medição com
  timestamp query.
- **O comportamento de um pool FP16 com o desktop em SDR** — por isso a §8.2 condiciona o
  formato ao HDR estar ligado, em vez de apostar. Continua sem verificação, e de propósito.
- **A conversão contra um jogo HDR de verdade.** Os alvos são sintéticos e escritos por mim;
  eles provam que a cadeia captura, converte e entrega os valores certos, não que um jogo
  específico se comporte como o modelo.
- **Como fica com a cadeia ReShade/DLSS ligada.** A medição de cor foi feita com o exe fora da
  pasta, sem `dxgi.dll` ao lado — de propósito, para medir só a conversão. Com a cadeia ativa
  os valores mudam (no teste de fumaça, 203 nits saiu 193 e o preto subiu para 7): é o Neural
  Rendering agindo, com os quatro sliders no máximo, e isso é assunto de
  `docs\brilho-witcher3.md` §2, não da conversão.
- **A heurística contra cena real.** Foi validada com quadros sintéticos, onde a discriminação é
  limpa. Cena real escura ou com pouco branco pode empurrá-la para "não sei" — que é um
  resultado previsto e sinalizado, não uma falha silenciosa.

---

## 11. Arquivos

| arquivo | o quê |
|---|---|
| `hdr_control.h` | o módulo: detecção (display e vídeo), toggle, restauração, persistência, heurística, parâmetros do shader, textos |
| `screen_capture.h` | pool e textura em FP16 quando o monitor do alvo está em HDR (§8.2) |
| `main.cpp` | `HdrMode == 3` (scRGB) no `kShader`, o wiring da captura, `DLSS5_DUMP` |
| `docs\hdr.md` | este documento |
| `docs\img\hdr-antes-depois.png` | a prova: antes × depois, alvo SDR e alvo HDR |
| `DLSS5ForAll.exe` | recompilado com tudo acima |

`ReShade.ini`, `dlss5-feed.cfg` e os presets continuam intactos. `dlss5forall.ini` ganha a
seção `[hdr]` (só `tratar=0|1` e, em caso de queda, `restaurar`).

### Chaves de diagnóstico

| variável | o quê |
|---|---|
| `DLSS5_FP16=0` / `=1` | força o formato da captura (padrão: automático pelo HDR do monitor). O `=0` é o "antes" para comparação, com o mesmo binário |
| `DLSS5_HDR_KNEE=0.75` | liga o roll-off nas altas na captura de tela (padrão 1.0 = sem roll-off) |
| `DLSS5_DUMP=<arquivo.png>` | grava a saída da cadeia no frame `DLSS5_DUMP_FRAME` (padrão 120) e encerra. É o único jeito honesto de medir cor num desktop HDR |

O harness de teste (`hdrtest.cpp`, `mfprobe.cpp`, `mat.cpp`, `videotest.cpp`, `shadertest.cpp`,
`bug.cpp`, `cycle2.cpp`, `hdrtarget.cpp`, `readpatch.cpp`, `montar.cpp`) ficou no scratchpad da
sessão, fora do projeto.
