# Latência do DLSS5 For All — medições e conclusões

Trabalho focado na latência fim-a-fim do caminho **fonte janela → WGC → Tick → Present**.
Cenário priorizado: **fonte janela, modo janela separada (espelho)** — é essa janela que o
Lossless Scaling captura e interpola.

Medições feitas em 2026-09-02, 19:00–19:41.
O patch do `main.cpp` está em `docs\patch-main-latencia.md` (blocos ANTES/DEPOIS literais).

---

## Resumo em uma tabela

| | ANTES | DEPOIS |
|---|---|---|
| frame na mão → pixel na tela (1080p + DLSS) | **~121 ms** | **~42 ms** |
| `PRESENT->TELA` (fila do DXGI/DWM) | 108.5 / 111 ms | 41.5 / 44 ms |
| `DrawAndPresent` CPU | 12.6 / 19.5 ms | 0.87 / 1.2 ms |
| sem carga de GPU (720p) | 8.5 / 8.7 ms | 8.5 / 8.7 ms (igual) |

Vsync mantido. Sem tearing. Sem mudança de resolução, de formato de pixel ou de contagem de
frames apresentados. 1 Present por frame novo continua valendo.

---

## 1. Metodologia

**Alvo:** `lattarget.exe`, escrito para o teste — janela D3D11, FLIP_DISCARD, `Present(1,0)`,
limpa o backbuffer com uma cor diferente a cada frame. Produz exatamente 1 frame novo por
refresh, como um jogo. Tamanho, posição e swap effect por linha de comando; mostra o próprio
fps no título. Usei um alvo sintético em vez das janelas do usuário justamente para ter uma
fonte com taxa conhecida e controlável.

**Máquina:**
- DISPLAY1 = RTX 4080, 3440x1440 @ **99 Hz** — o monitor real
- DISPLAY2 = **Meta Virtual Monitor**, 1920x1080 @ 59 Hz — e é o monitor **principal**
- também presentes: Parsec Virtual Display Adapter e Virtual Desktop Monitor

**Cadeia ReShade:** cópia isolada em `scratchpad\rs\` (dxgi.dll, ReShade.ini, FafaDLSS5.ini,
dlss5-feed.addon64, renodx-dlss5.addon64, nvngx_dlss*.dll, reshade-shaders), para não
sobrescrever `ReShade.log` / `dlss5-feed.log` do projeto. O feed confirma
`adapter: NVIDIA GeForce RTX 4080` e `frame N delivered` — DLSS/NR rodando de verdade, não é
simulação.

**Instrumentação:** `QueryPerformanceCounter` nos quatro pontos do caminho, mais
`IDXGISwapChain::GetLastPresentCount` casado com `DXGI_FRAME_STATISTICS.SyncQPCTime` — que dá o
vblank em que o frame **realmente** apareceu. Sem isso a fila do driver é invisível: o `Present`
retorna em 0.04 ms e parece que não há custo nenhum. Sai no `dlss5forall.log` a cada 300 frames.

O que cada coluna do log significa:

| coluna | o que mede |
|---|---|
| `srt` | `SystemRelativeTime` do frame vs. o instante em que o WGC nos avisou |
| `acordar` | último `FrameArrived` → entrada no `Tick()` |
| `tick` | drenar a fila + medir geometria + `CopySubresourceRegion` |
| `present` | `DrawAndPresent()` inteiro, CPU (inclui toda a cadeia ReShade/DLSS) |
| `PRESENT->TELA` | fim do `Present()` → vblank real (**a fila do DXGI/DWM**) |
| `ritmo do WGC` | intervalo entre dois `FrameArrived` |

**Protocolo:** duas execuções de 50 s por configuração, alternando ANTES/DEPOIS com o **mesmo
binário** (as variantes ligadas por variável de ambiente, para não comparar builds diferentes).
Descartei sempre a primeira janela de 300 frames, que pega o aquecimento do DLSS.

---

## 2. Resultado principal

**Alvo 1920x1080, cadeia ReShade+DLSS NR ativa, espelho no monitor de 99 Hz.**

| etapa (média / p95, ms) | ANTES | DEPOIS |
|---|---|---|
| `acordar` | 0.01 / 0.02 | 12.6 / 19.4 ¹ |
| `tick` | 0.08 / 0.11 | 0.19 / 0.36 |
| `present` (CPU) | **12.6 / 19.5** | **0.87 / 1.2** |
| `PRESENT->TELA` | **108.5 / 111** | **41.5 / 44** |
| **frame na mão → pixel na tela** | **~121 ms** | **~42 ms** |

Amostras brutas (3 janelas de 300 frames de cada lado, 2 execuções):

```
ANTES   present 12.61 + tela 107.59  |  12.65 + 109.06  |  12.66 + 108.96
DEPOIS  present  0.84 + tela  41.72  |   0.89 +  41.22  |   0.94 +  41.16
```

¹ Os 12.6 ms de `acordar` no DEPOIS **não são atraso acumulado**. É o loop parado no objeto de
espera até a GPU liberar, e só então indo pegar o frame **mais novo que existe**. No ANTES o
frame era pego cedo e envelhecia 110 ms dentro da fila do driver. Trocamos "pegar cedo e
apodrecer na fila" por "esperar e pegar fresco" — mesma espera total, frame muito mais novo.

**Sem carga de GPU (alvo 720p, mesma cadeia):** idêntico antes e depois — `PRESENT->TELA`
8.5 / 8.7 ms, 50 fps, `present` 0.7 ms. A mudança não cobra nada quando não há pressão de fila;
ela só age quando a fila encheria.

---

## 3. Os cinco pontos da investigação, respondidos

### 3.1 `Present(1,0)` — o custo do vsync

**Não era o problema.** O `Present` não bloqueava: 0.04 ms de CPU. O custo estava invisível — o
driver enfileirava até 3 frames na nossa frente e o `Present` voltava na hora.

Medi `Present(0,0)` (sem `ALLOW_TEARING`, portanto sem rasgar — no modelo flip o DWM só compõe o
mais recente): **7.82 / 8.62 contra 8.03 / 8.34 da base. Diferença nula.** Como o WGC nos entrega
a 50 Hz num monitor de 99 Hz, nunca há disputa pelo vblank. **Vsync mantido.**

### 3.2 `SetMaximumFrameLatency` e o waitable swapchain

É aqui que estava tudo. Quatro variantes medidas sob carga (1920x1080 + DLSS NR):

| variante | `present` CPU | `PRESENT->TELA` | total |
|---|---|---|---|
| base (fila do driver = 3) | 12.6 / 19.5 | 108.5 / 111 | ~121 ms |
| `IDXGIDevice1::SetMaximumFrameLatency(1)`, sem waitable | 33.1 / 39.9 | 42.6 / 46 | ~76 ms |
| waitable, esperando **dentro** do `DrawAndPresent` | 33.2 / 40.2 | 44.7 / 47 | ~78 ms |
| **waitable, esperando ANTES do `Tick`** | **0.87 / 1.2** | **41.5 / 44** | **~42 ms** |

Três armadilhas confirmadas na prática, todas evitadas no patch:

1. **Esperar dentro do `DrawAndPresent` não ganha nada** (78 ms) — é o mesmo que deixar o
   `Present` bloquear. O ganho vem de esperar **antes de pegar o frame da fonte**.
2. **Somar as duas esperas mata o throughput:** `IDXGISwapChain2::SetMaximumFrameLatency(1)`
   junto com `IDXGIDevice1::SetMaximumFrameLatency(1)` produz duas esperas por frame e derruba
   de 50 para **20 fps**, com o `present` travado em ~50 ms. No patch, o do device só entra
   quando **não** há waitable.
3. **1 espera por Present, nem mais nem menos** (flag `slotWaited`) — o loop acorda também por
   mensagem e por timeout, e sem a trava ele consumiria mais fichas do semáforo do que devolve.

O swapchain waitable exige nascer de `IDXGIFactory2::CreateSwapChainForHwnd` (o
`D3D11CreateDeviceAndSwapChain` não aceita a flag), e a flag precisa voltar no `ResizeBuffers`
— senão o swapchain deixa de ser waitable no primeiro redimensionamento e a latência volta.
O fallback para o caminho de hoje (FLIP_DISCARD → DISCARD clássico) continua intacto.

### 3.3 Frame pool do WGC

**Número de buffers não muda nada:** `CreateFreeThreaded` com 1, 2 e 3 buffers deu latência e
taxa de chegada idênticas. Mantido em 2.

**Copiar só o frame mais novo:** implementado (drena a fila fechando os antigos sem copiar).
Ganho medido ~0.01 ms, porque aqui chega 1 frame por Tick. Vale como proteção contra rajada
depois de um travamento — sem isso, um acúmulo de 3–4 frames vira 3–4 cópias de 1920x1080
enfileiradas na frente do Present, e só a última aparece na tela mesmo.

### 3.4 O loop principal

**Não é gargalo.** `MsgWaitForMultipleObjects(1,&evt,FALSE,100,QS_ALLINPUT)` acorda em 0.01 ms
(média e p95). O re-present ocioso de 100 ms está correto e foi mantido — é o que mantém a
cadeia ReShade viva quando a fonte para.

### 3.5 Quanto é da cadeia ReShade/DLSS

Mesmo binário, com e sem `dxgi.dll` na pasta, alvo 720p:

- **sem cadeia:** `DrawAndPresent` = 0.04–0.05 ms de CPU
- **com cadeia:** 0.78–0.95 ms de CPU
- o próprio feed reporta:
  `600 frames: feed CPU 2.99 ms/frame | frame interval 22.30 ms (44.8 fps) | feed is 13% of the frame`
- `PRESENT->TELA` **não muda** por causa da cadeia quando há folga (8.5 ms com e sem)

A 1920x1080 a cadeia é o que **limita o throughput**: a saída trava em **30 fps** enquanto o WGC
entrega 50. A 2556x1391 satura e todos os números degradam. Isso é tempo de GPU do DLSS NR —
teto real, fora do nosso alcance.

---

## 4. O achado que vale mais que tudo isso junto: o WGC preso em 50 Hz

**O Windows Graphics Capture entrega exatamente 50.0 fps nesta máquina, sempre.**
Intervalo entre `FrameArrived`: média **20.00 ms**, p05 18.4, p95 21.0, min 14, max 26.

Não muda com nenhuma destas variações:

| variação testada | chegada |
|---|---|
| alvo a 100 fps (monitor 99 Hz) | 50.0 fps |
| alvo a 60 fps (monitor 59 Hz) | 50.0 fps |
| alvo em FLIP_DISCARD / em DISCARD clássico (bitblt) | 50.0 fps |
| pool com 1 / 2 / 3 buffers | 50.0 fps |
| `timeBeginPeriod(1)` | 50.0 fps |
| **`DLSS5_NOCOPY=1`** (dreno e descarto, zero cópia, zero Present) | **50.0 fps** |

O último caso é a prova de que **não é código nosso**: sem nenhum trabalho nosso no caminho —
sem cópia de GPU, sem draw, sem Present — o relógio continua marcando 20.00 ms. E não é
"um frame sim, um não": se fosse, o alvo de 60 fps teria dado 30, não 50.

**Custo:** ~10 ms de latência média (metade do período de 20 ms) e metade da suavidade —
de 100 fps na fonte chegam 50 até nós.

### Suspeita

A máquina tem **três adaptadores de vídeo virtuais** além da RTX 4080 (Meta Virtual Monitor,
Parsec Virtual Display Adapter, Virtual Desktop Monitor), e o **monitor principal do Windows é o
Meta Virtual Monitor, a 59 Hz** — não o monitor real de 99 Hz.

### Passo a passo para o usuário verificar (10 minutos)

1. Abra o `dlss5forall.log` e anote a linha atual de referência:
   `LAT ritmo do WGC (intervalo entre FrameArrived): ... media 20.00 ...`
2. **Torne o monitor real o principal:** Configurações → Sistema → Vídeo → clique no monitor de
   3440x1440 → marque *Tornar minha tela principal*.
3. Rode o app em modo janela por ~10 s sobre um jogo ou vídeo em movimento e confira a mesma
   linha no log. Se a média cair para ~10 ms, o problema era esse — e são ~10 ms de graça.
4. Se não resolver, **desative os monitores virtuais** que não estiverem em uso: Gerenciador de
   Dispositivos → Adaptadores de vídeo → botão direito em *Meta Virtual Monitor*, *Parsec Virtual
   Display Adapter* e *Virtual Desktop Monitor* → *Desativar dispositivo*. (Reative quando for
   usar o Quest Link / Parsec / Virtual Desktop — desativar só tira do caminho do DWM enquanto
   não estiver usando.)
5. Repita o passo 3.

Não fiz nenhuma dessas mudanças eu mesmo — mexem na configuração de vídeo do PC dele.

### Relevante para o Lossless Scaling

Se o LS capturar a nossa janela por WGC, **ele bate no mesmo relógio de 20 ms**. Esse limite não
é só nosso: vai limitar o que chega no interpolador também. Resolver isso melhora os dois lados
da cadeia de uma vez.

---

## 5. `ALLOW_TEARING` — não medido, e não recomendado neste arranjo

**Não foi testado.** Nenhum número deste documento vem de `ALLOW_TEARING` ou de
`Present(0, DXGI_PRESENT_ALLOW_TEARING)`. Não vou apresentar estimativa como medição.

Recomendo **não** seguir por aí, com três motivos:

1. **Não há ganho a capturar.** O único componente que o tearing removeria é a espera pelo
   vblank do nosso Present — 8.5 ms sem carga. Mas `Present(0,0)` sem tearing já foi medido e deu
   **exatamente o mesmo** que `Present(1,0)` (7.82 vs 8.03 ms): como o WGC nos entrega a 50 Hz num
   monitor de 99 Hz, nunca há disputa pelo vblank. O que custava 110 ms era a fila, e a fila já
   foi resolvida.
2. **Com o Lossless Scaling por cima, tearing vira artefato.** Quem apresenta na tela no fim é o
   LS. Uma imagem rasgada entrando no interpolador produz artefato de interpolação em cima da
   linha de rasgo — piora a qualidade, que é justamente o que não pode acontecer.
3. **Nossa janela é composta pelo DWM de qualquer jeito.** Em janela (não fullscreen exclusivo),
   `ALLOW_TEARING` só surte efeito real quando o DWM concede *independent flip*; com uma janela
   topmost/borderless por cima (o LS), essa condição normalmente não se sustenta.

Se um dia for para testar: `IDXGIFactory5::CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING)`,
flag `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` no `DXGI_SWAP_CHAIN_DESC1` (somável ao
`FRAME_LATENCY_WAITABLE_OBJECT`), `Present(0, DXGI_PRESENT_ALLOW_TEARING)`, e a flag tem que ir
junto no `ResizeBuffers` — o `scFlags` do patch já carrega isso. Deve ficar atrás de uma chave,
com vsync como padrão.

### Sobre o "piscar" que o usuário viu

**Não foi tearing.** Nenhum teste rodou com tearing habilitado — só `Present(1,0)` e, em duas
medições, `Present(0,0)` sem a flag de tearing (que no modelo flip não rasga: o DWM compõe o
buffer mais recente). O piscar veio das janelas de teste abrindo e fechando na tela real: foram
~25 execuções, cada uma criando uma janela de espelho de 1280x720 a 2556x1391 no monitor dele,
mais um teste de modo sobreposto de 18 s às 19:40. A partir do aviso, nenhum overlay foi
executado e todos os processos foram encerrados.

---

## 6. Mudanças aplicadas em `screen_capture.h`

Já aplicadas em `C:\Users\PCDOFAFA\Desktop\dlss cam\screen_capture.h`. Compilado contra o
`main.cpp` **intocado** do projeto para confirmar que continua fechando sozinho.

1. **Instrumentação `LatProbe` / `LatSeries`** — carimba `SystemRelativeTime`, `FrameArrived`,
   Tick, Present e o vblank real; loga média/p95 a cada 300 frames no `dlss5forall.log`.
   Se o patch do `main.cpp` não for aplicado, o módulo ainda loga sozinho (sem as colunas de
   apresentação). Custo: ~4 `QueryPerformanceCounter` por frame.
2. **Contexto imediato cacheado** (`ictx`) — `GetImmediateContext` saía do caminho crítico a cada
   frame (AddRef/Release + lock interno, sem motivo nenhum).
3. **`Tick()` copia só o frame mais novo** — drena a fila fechando os antigos sem copiar.
4. **Geometria do recorte cacheada** (`TargetRectCached`) — `DwmGetWindowAttribute` é uma chamada
   ao processo do DWM e rodava a cada frame; agora só quando o `GetWindowRect` muda.
   Invalidada em `RecreatePool`.
5. **Buffers do pool parametrizados** (padrão 2, inalterado).
6. **Chaves de diagnóstico por variável de ambiente**, padrão = modo rápido:
   `DLSS5_DRAIN=0` (volta a copiar todos os frames), `DLSS5_RECTCACHE=0` (volta a perguntar ao
   DWM a cada frame), `DLSS5_POOLBUF=N`, `DLSS5_NOCOPY=1` (mede o ritmo bruto do WGC).

Nada disso mexe em resolução, formato de pixel, número de frames apresentados ou na cadeia.
O recorte continua `B8G8R8A8_UNORM` 1:1, `CopySubresourceRegion` direto na GPU.

Uma nota de acoplamento: o header agora depende de `Log()` estar definido no `main.cpp` antes do
`#include` — já era o caso na prática, mas agora é dependência de verdade.

---

## 7. Validações do build com o patch aplicado

- **espelho de janela + cadeia DLSS:** `swapchain waitable ... flags=0x40`, objeto de espera
  obtido através do proxy do ReShade (ele repassa o `GetFrameLatencyWaitableObject` corretamente),
  feed entregando frames normalmente
- **modo sobreposto sobre janela:** 49.7 fps, `present` 0.05 ms, `PRESENT->TELA` 8.4 ms, colagem e
  click-through normais (teste de 18 s, feito antes da ordem de parar overlays)
- **modo câmera** (Animaze Virtual Camera, 1280x720@30): 29.2 fps, `PRESENT->TELA` 26.6 / 35.1 ms
  contra 27.7 / 35.0 e 24.8 / 30.9 do build sem waitable — estatisticamente igual, sem regressão
- **1 Present por frame novo continua valendo:** os contadores `copiado` e `present` batem em
  todos os logs
- **fallback:** se o `CreateSwapChainForHwnd` waitable falhar, cai no swapchain de hoje e ainda
  aplica `SetMaximumFrameLatency(1)` no device — que sozinho já vale 121 → 76 ms

---

## 8. O que é irredutível

| item | custo | de quem é |
|---|---|---|
| amostragem do WGC (relógio de 20 ms) | ~10 ms médio | sistema — seção 4, é o próximo ganho |
| 1 vblank do nosso Present | 8.5 ms @ 99 Hz (16.9 @ 60 Hz) | DWM/monitor |
| composição do DWM do lado do alvo | ~1 quadro | DWM |
| **GPU da cadeia DLSS NR** | **~33 ms/frame a 1080p — trava a saída em 30 fps** | DLSS/renodx |
| nosso código (acordar + tick + submeter) | **~1 ms** | nosso |

O item mais pesado que sobra é a própria cadeia: a 1920x1080 com Neural Rendering ela consome
~33 ms de GPU por frame, o que **limita a saída a 30 fps mesmo com o WGC entregando 50**. Não é
latência de sincronização — é trabalho de GPU. Reduzir isso só com resolução menor ou preset mais
leve, e o usuário já disse que qualidade não pode piorar.

Piso realista depois das mudanças: **~20 ms mais o tempo de GPU da cadeia**.
Antes era **~121 ms** mais o tempo de GPU da cadeia.

---

## 9. Ressalvas de honestidade

- As duas primeiras medições de linha de base (27 ms de `PRESENT->TELA`, 720p sem carga) **não se
  reproduziram** em nenhuma execução posterior. Foram descartadas; todos os números deste
  documento vêm de execuções reprodutíveis, alternadas, com pelo menos duas repetições por lado.
- `ALLOW_TEARING` **não foi medido** — a seção 5 é análise, não medição.
- A latência do próprio Lossless Scaling **não foi medida** por mim.
- Os números do modo câmera são de uma câmera virtual (Animaze), não da placa de captura ezcap.
- A causa raiz do relógio de 50 Hz do WGC **não foi provada** — só isolada (é externa ao nosso
  código). A hipótese dos monitores virtuais precisa do teste da seção 4 para confirmar.

---

## 10. Bancada de teste (para reproduzir)

`C:\Users\PCDOFAFA\AppData\Local\Temp\claude\C--Users-PCDOFAFA-Desktop-dlss-cam\c9f972eb-7d23-432a-b980-290e431b1700\scratchpad\`

- `lattarget.cpp` / `.exe` — alvo animado. Argumentos: `x y modo largura altura`
  (modo 0 = FLIP_DISCARD, 1 = bitblt). Mostra o próprio fps no título.
- `run-lat.ps1` — roda uma configuração e imprime as linhas `LAT` do log.
- `rs\` — cópia isolada da cadeia ReShade (não toca nos logs do projeto).
- `final\main.cpp` e `final\main.diff` — o `main.cpp` com o patch aplicado, compilando.

Para reconferir o ritmo do WGC depois de mexer nos monitores virtuais, basta rodar o app
normalmente e olhar a linha `LAT ritmo do WGC` no `dlss5forall.log`.
