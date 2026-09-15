# DLSS5 For All — análise de desempenho (somente leitura)

Data: 2026-09-05. Fontes lidas: `amd\main.cpp` (9.228 linhas, em edição por outro agente), `screen_capture.h`, `motion_estimate.h`, `hdr_control.h`, `reshade_bridge.h`, `addon\dlss5_bridge_addon.cpp`, `reshade-shaders\Shaders\{DLSS5_Feed,DLSS5_Sharpen,lumenite_Kernel,SMAA,DLSS5_MV_Hardware}.fx`, `README.md`, `docs\{latencia,latencia-nr,supersampling,painel,enxugar-painel,motion-vectors}.md`, logs `amd\*.log` e `DLSS5ForAll-AMD\bin\*.log`, e o fonte do Feeder (`scratchpad\dlss5-feed.cpp`, 5.898 linhas).

Convenções: **[FATO]** = lido no código ou medido em log/doc; **[INFERÊNCIA]** = estimativa minha a partir do código, sem medição nesta máquina. Linhas de `main.cpp` podem deslocar alguns números porque o arquivo está sendo editado agora.

Resumo em cinco linhas:

1. O custo dominante é GPU, dentro do Present: Kernel + SMAA + Feed + DLSS/NR + Sharpen. O laço de CPU do app é enxuto (DrawAndPresent 1,0 ms média, tick 0,1 ms, acordar 0,03 ms **[FATO]** `bin\dlss5forall.log` linha LAT 11s).
2. O maior desperdício de GPU que dá para tirar com código do app é o **re-present ocioso a cada 100 ms**: com a fonte parada a cadeia inteira (inclusive o neural) roda 10×/s sobre a mesma imagem.
3. O maior ganho de **startup** é a espera de `create_delay=60` do Feeder ser contada em *presents*: com fonte lenta virou 6,8 s numa sessão real **[FATO]**. Apresentar a ~60 Hz durante o aquecimento resolve sem tocar no Feeder.
4. Na cadeia ReShade há três passes de tela cheia que não produzem nada para este app: a reconstrução de normais do Kernel (profundidade plana), o `FitSolve` do Feed com `GEOM_ENABLE=0` (sem early-out) e o `LinearizeDepth` do SMAA (predicação desligada).
5. Muita coisa já está bem feita e não deve ser mexida: swapchain waitable com espera antes do Tick, dreno do WGC copiando só o mais novo, geometria e contexto cacheados, cache de moldura e assinatura do painel, `TailArquivo` com stat-cache, leitura do neural espaçada com o painel fechado.

---

## (a) Mapa do que roda, por volta, por quadro e por segundo

### a.1 Uma volta do laço principal (`main.cpp:8044` em diante)

O laço acorda no evento do WGC ou em 100 ms (`MsgWaitForMultipleObjects`, `main.cpp:8048`). Com fonte a 60–100 fps são 60–100 voltas/s; parada, 10 voltas/s.

| Etapa (ordem real) | Onde | Custo por volta | Nota |
|---|---|---|---|
| `PeekMessage` + `g_trayCmd.exchange` (mutex) | 8051–8060 | ~µs | ok |
| Comandos do painel/atalhos | 8060–8427 | só em evento | ok |
| `FollowTarget` | 6772–6847, chamada 8479 | ~10–20 µs **[INFERÊNCIA]** | `GetWindowRect`(alvo) via `TargetRectCached`, `IsIconic`, `GetForegroundWindow`, `GetWindowThreadProcessId`(fg), `GetWindowRect`(nossa). A cada 1 s: `SetWindowPos` topmost + `Camada2Elevar` + `RaiseNow` do painel/guia/efeitos |
| `DevolverForegroundSeForNossaCamada` | 6751–6766, chamada 8480 | ~µs | segundo `GetForegroundWindow` da mesma volta |
| `Hud::Tick` / `Guia::Tick` / `Efeitos::Tick` | 5542, 5937, 6403 | ~µs fechados; `SetWindowPos` a cada 250/120/250 ms abertos | `Efeitos::Tick` faz `rsbridge::GetStatus` 1×/s (SRWLock + atômicos) |
| Camada 2 (se ligada) | 8490–8530 | 1×/s `TailArquivo` 16 KB (stat-cache) | ok |
| Aquecimento: `CadeiaEntregando` | 8534–8556, 4039–4045 | 4×/s durante o aquecimento | ver a.4: cada chamada copia ~230 KB de strings **[FATO]** |
| `ToastTick`, `HdrFeedShader` | 8676, 8687 | ~µs | ok |
| `R.WaitForPresentSlot` | 865–870, chamada 8691 | espera proposital | é o marca-passo do app (waitable, latência 1) |
| `scap.Tick()` | `screen_capture.h:829–877`, chamada 8701 | 0,09–0,11 ms média **[FATO]** LAT | drena a fila e copia só o mais novo (`CopySubresourceRegion` de tela cheia, `:815`) |
| `MvHw::Frame` | 6972–7016 | 0 (desligado por padrão) | ~0,18 ms CPU se `mv_hardware=1` **[FATO]** doc |
| Teto de fps | 8871–8876 | `QpcMs` | ok |
| `R.DrawAndPresent()` | 1088–1168, chamada 8880 | 1,00–1,07 ms média, 1,3–1,5 p95 **[FATO]** LAT | clear cor + clear D24S8, `Draw` de 576 quads, `Present(0,0)`. O tempo é quase todo o ReShade + add-ons rodando dentro do Present |
| `g_lat.Present` | `screen_capture.h:126–142` | ~5 µs | `GetLastPresentCount` + `GetFrameStatistics` |

Nenhuma alocação de heap no caminho quente por volta **[FATO]**: `FollowTarget`, `Tick`, `DrawAndPresent` e o cap não alocam. O único `Log()` regular é o LAT a cada 300 presents (com `fflush`, `main.cpp:118–124`).

### a.2 O tique de 1 segundo (`main.cpp:8916–9144`)

| O que | Onde | Custo | Frequência real |
|---|---|---|---|
| `GetLastInputInfo` ×2 | 8938–8940 | µs | 1/s |
| `LerEstadoNeural` | 8995–8999 → 3916–4028 | ver a.4 | 1/s com painel aberto, verificação pendente ou `NR_INICIANDO`; senão 1/5 s **[FATO]** |
| `Hud::Refresh` → `Paint` | 9031 → 5188–5269 | 0,04 ms se nada mudou; ~1,8 ms se mudou o conteúdo **[FATO]** `enxugar-painel.md` | só com painel aberto. Com jogo vivo o inteiro do fps muda quase todo segundo, então ~1,8 ms/s |
| `AssinaturaVisivel` | 5060–5115 | 0,035 ms (`GetPrivateProfileStringW` no ReShade.ini) + `Efeitos::Resumo()` (strings) + `GetWindowRect` + `GetMonitorInfoW` **[FATO/INFERÊNCIA]** | 1/s, painel aberto |
| `GetMonitorInfoW` (fonte monitor) | 9040–9042 | µs | 1/s |
| Título: `_snwprintf` + `TextoAtalho` ×2 + `SetWindowTextW` | 9137–9143 | ~20–50 µs; `WM_SETTEXT` acorda o DWM para a moldura da janela separada **[INFERÊNCIA]** | 1/s sempre, mesmo com texto igual |

### a.3 A cadeia gráfica, por quadro apresentado

Ordem real de execução na GPU para um quadro de W×H (1920×1079 na sessão do `bin`; 3440×1440 na do `amd`):

1. **WGC → `scap.tex`**: 1 `CopySubresourceRegion` de tela cheia (4 B/px). Pool com 2 buffers, `MinUpdateInterval` 0 ms, free-threaded, formato BGRA8 em SDR / FP16 em HDR **[FATO]** `screen_capture.h:678, 718–740`.
2. **PS do app** (`main.cpp:1088–1148`): clear de cor e de D24S8 (4 B/px), grade 32×18 = 576 quads com z=0,5, PS passthrough (`UpFilter==0`, `HdrMode==0`). ~0,1–0,2 ms **[INFERÊNCIA]**. O depth existe só para o Generic Depth do ReShade ter um buffer (`main.cpp:439`).
3. **ReShade Generic Depth**: com um depth buffer no Draw, o add-on copia/resolve a profundidade para a textura dele todo frame (4 B/px) **[INFERÊNCIA]**, a mesma profundidade plana sempre.
4. **SMAA** (`SMAA.fx:289–323`), 4 passes de tela cheia: `LinearizeDepth` (R16F, inútil com `PredicationEnabled=0` **[FATO]** `FafaDLSS5.ini`), `EdgeDetection` (luma), `BlendWeight`, `NeighborhoodBlending`.
5. **Lumenite_Kernel** (`lumenite_Kernel.fx:717–745`), 17 passes: `ReconstructNormals` (tela cheia, escreve RGBA16F + R16F **com 4 níveis de mip cada**, a partir de uma profundidade plana → resultado constante; `tNormals` só é lido pelo passe de debug, e `tDepth` é lido pelos passes de fluxo (`:201, :335, :350`) apenas como teste `< 0.999` e como peso `|Δdepth| < 0.003`, que com profundidade plana dão sempre "válido" e peso 1 **[FATO]**; o Feed só amostra `Kernel::tFlow` e `tConfidence`, `DLSS5_Feed.fx:105–107`), `PackFeatures` (tela cheia R16F + 8 mips), 12 passes de fluxo em 1/128…1/8 da tela (baratos), `Confidence` e `ATrous` a 1/8, `StoreFlow` (1/8), `StoreLuma` (tela cheia + 8 mips). Ou seja, 3 passes de tela cheia de verdade e três geração de mips, não "15 passes de tela cheia" como diz `motion-vectors.md` **[FATO]** (a doc superestima).
6. **DLSS5_Feed** (`DLSS5_Feed.fx:848–862`), 5 passes: `FitSamples` (40×23 px), `FitSolve` (**1 pixel** que roda `2 × 920` iterações seriais com acumulação 9×9 e um Gauss-Jordan 9×11 – `DLSS5_Feed.fx:553–647`), `MotionVectors` (tela cheia; com `VALIDATE_STATIC=1` e `VALIDATE_LUMA=1` faz 2× `PatchError` = 36 amostras + a vizinhança 3×3 ≈ 45 leituras/px **[FATO]** `:419–445, 458–480`), `Depth` (tela cheia R32F escrevendo a profundidade plana todo frame), `StoreHistory` (3 alvos de tela cheia). **Não há early-out por `GEOM_ENABLE` em `FitSamples`/`FitSolve`** **[FATO]**: eles rodam sempre, e o resultado só é consumido quando `GEOM_ENABLE && FitIsUsable()` (`:675`).
7. **Feeder** (`dlss5-feed.cpp`, `FeedFrame11` a partir de `:5060`): `get_texture_binding` ×4, **4 `CopyResource` de tela cheia** para as texturas compartilhadas (cor 4 B + depth 4 B + MV 4 B + máscara 1 B = 13 B/px, `:3679–3682`), `ctx4->Signal` + `ctx->Flush()` + `queue->Wait` (fila 3D → fila D3D12), `EvaluateFeature` (DLSS DLAA e, dentro do detour do RenoDX, o Neural Rendering), `EndCommands` + `ctx4->Wait` (D3D12 → fila 3D), `BlitOutputToBackbuffer` (mais 1 passe de tela cheia). Duas sincronizações de fila por quadro na linha do tempo da GPU **[FATO]**; cada uma é uma bolha de pipeline **[INFERÊNCIA]**. Custo de CPU do Feeder: 0,59–0,74 ms/quadro a 3440×1400 com NR **[FATO]** `latencia-nr.md §6`; o "3,29 ms/frame" do `bin\dlss5-feed.log` inclui as duas travas de 895 e 449 ms da abertura dentro da janela de 600 quadros (só elas somam 2,2 ms/quadro na média) **[INFERÊNCIA]**.
8. **DLSS5_Sharpen**: 1 passe de tela cheia (`DLSS5_Sharpen.fx:218–232`).
9. **Overlay do ReShade** (só o contador de FPS) e `Present` flip.

Bytes movidos só por cópias (sem contar os render targets):

| Resolução | WGC | Feeder (entrada) | Feeder (copy-back) | Depth do Generic Depth [INF.] | Total/quadro |
|---|---|---|---|---|---|
| 1920×1080 | 8,3 MB | 27 MB | 8,3 MB | 8,3 MB | ~52 MB |
| 3440×1440 | 19,8 MB | 64 MB | 19,8 MB | 19,8 MB | ~124 MB |

A 60 quadros/s em 3440×1440 são ~7,4 GB/s de banda só em cópias; numa RTX 4080 (~700 GB/s) é ~1 % do tempo de quadro. Não é o gargalo, mas não é zero **[INFERÊNCIA]**.

Custo total medido da cadeia (GPU): a 3440×1400, `frame interval` 10,25 ms sem NR → 12,26 ms com NR (98 → 82 fps) **[FATO]** `latencia-nr.md §2`. O comentário em `main.cpp:7629` cita uma medição anterior de 30 ms/quadro no ultrawide com 96 % na rede (outro build do NR). O neural é a parte cara e é irredutível sem `work_resolution`.

### a.4 Leituras de disco e strings no caminho do render

`TailArquivo` (`main.cpp:3638–3690`) tem cache de 3 entradas por (caminho, bytes) validado por `GetFileAttributesExW` (tamanho + mtime). Quando o arquivo não mudou não há `ReadFile` **[FATO]**. Mas:

- ela devolve `std::string` **por valor**: cada chamada com cache quente ainda aloca e copia 96 KB (ReShade.log) ou 32 KB (dlss5-feed.log) **[FATO]** `:3667`;
- os chamadores varrem a cópia inteira com `find`/`rfind` a cada chamada: `LerEstadoNeural` faz 3 `find` em 96 KB (`:4009–4011`), `NeuralPeloLog` 3 `rfind` em 96 KB (`:6610–6612`), `NgxPeloFeed` 1–2 `rfind` em 32 KB (`:3707, 3723`).

Uma chamada de `LerEstadoNeural` = ~224 KB de malloc + memcpy + ~700 KB de varredura **[FATO, contagem]** ≈ 0,1–0,3 ms **[INFERÊNCIA]**. `CadeiaEntregando` chama `LerEstadoNeural` e mais dois `TailArquivo` (`:4040–4044`), 4×/s durante o aquecimento, e `LerEstadoNeural` roda 1×/s enquanto o estado é `NR_INICIANDO` (os primeiros 10–25 s) ou com o painel aberto. Isso é ~1 MB/s de cópia+varredura na thread que apresenta, durante justamente a fase em que o Present já está engasgando **[INFERÊNCIA]**. Fora dessas fases é 1 leitura a cada 5 s e não importa.

O único `GetPrivateProfile*` periódico é o `TeclaOverlayReShade` dentro de `AssinaturaVisivel` (1×/s, painel aberto, 0,035 ms **[FATO]**). O `dlss5forall.ini` não é lido no laço **[FATO]**; `Efeitos::Carregar` e `MvHw::Ensure` leem uma vez.

### a.5 Startup: do seletor até o neural (sessão real, `bin\*.log`, 01:59)

| Instante | Evento | Δ acumulado |
|---|---|---|
| 04.063 | seletor fechado | 0 |
| 04.07–04.45 | ReShade carrega os 3 add-ons; RenoDX pré-carrega `nvngx_dlssnr.dll` (166 MB) no init do device | |
| 05.613 | `D3D11 ok, swapchain waitable` (R.Init) | **1,55 s** |
| 05.84–07.43 | 14 `.fx` compilados em threads paralelas; caminho crítico `DLSS5_Feed.fx` 1,64 s, `lumenite_Kernel.fx` 1,21 s | 3,4 s |
| 07.99 / 08.84 | Feeder abre sessão D3D12 / `NVSDK_NGX_D3D12_Init` (0,85 s) | 4,8 s |
| 08.88 | "holding the feature (re)build for 60 frames" | |
| 15.69 | "building" — os 60 presents levaram **6,8 s** (fonte a 3,3 fps + re-present de 100 ms) | 11,6 s |
| 15.73 / 15.85 / 16.13 | feature pronta / NR runtime init / 1ª avaliação | |
| 16.247 | `aquecimento: camada liberada apos 10469 ms` | **12,2 s** |
| travas | `present travou 895 ms (t+2.2 s)` = compilação/carga de efeitos dentro do Present; `449 ms (t+9.9 s)` = criação da feature + init do NR | |

Na sessão 3440×1440 (`amd\dlss5-feed.log`): sessão aberta 19.886 → building 23.921 = **4,0 s** para 60 presents (~15/s). Ou seja, entre um terço e metade do tempo até o neural é o app apresentando devagar enquanto o Feeder conta quadros **[FATO]**.

Dos 14 efeitos compilados, 9 não estão em `Techniques=` (Grade, MV_Hardware, RTAO, LSAO, QuantAO, AnamorphicBloom, TRAA, SSSR, QuantMotion) porque `SkipLoadingDisabledEffects=0` **[FATO]** `ReShade.ini`. Como compilam em paralelo, o tempo de parede é o do `DLSS5_Feed.fx`; em CPU de poucos núcleos a contenção aparece **[INFERÊNCIA]**.

### a.6 Memória e recursos

- Sem recursos D3D criados por quadro no app **[FATO]**. `EnsureTex` só recria na troca de tamanho; `RecreatePool` idem.
- `LatSeries`: 6 × 1024 doubles estáticos (48 KB); `pct()` aloca e ordena um vetor no Dump a cada 300 quadros — irrelevante.
- Painel: `g_items` reconstruído só quando a assinatura muda; moldura em cache (`g_fundo`) **[FATO]**.
- VRAM da cadeia a 3440×1440 (4,95 MP) **[INFERÊNCIA a partir dos formatos declarados]**: Kernel ~94 MB (dos quais ~53 MB são `tNormals` com mips, sem leitor ativo aqui), Feed ~84 MB, SMAA ~35 MB (10 MB do `depthTex` inútil), Feeder ~84 MB em texturas compartilhadas, mais o interno do DLSS e do NR. Em placa de 8 GB com jogo pesado isso conta.
- O único padrão de string por quadro que sobrou é o de a.4.

---

## (b) Otimizações, por ganho ÷ risco

### Seguras e pequenas (dá para fazer já)

**S1. Re-present ocioso de 100 ms → 250–500 ms depois do aquecimento.**
Onde: `main.cpp:8858` (`gap = hidden ? 500 : 100`), com a exceção de `aquecendo` em `:8857`.
O que muda: `gap = 100` só enquanto `aquecendo`; depois de `CadeiaEntregando()` ter respondido `true` uma vez, `gap = 400` (ou 500, que já é o valor do caminho `hidden`, comprovadamente inofensivo para a cadeia).
Ganho: com fonte parada (desktop, menu, vídeo pausado, alt-tab curto) a cadeia inteira deixa de rodar 10×/s sobre a mesma imagem: 12 ms × 10 = ~120 ms de GPU/s (≈12 % de uma 4080 a 3440×1440) caem para ~25 ms/s **[INFERÊNCIA]**. Menos calor e ruído de cooler no ocioso, que é exatamente o que o teto de fps tenta comprar.
Risco: baixo. O WGC já não entrega quadro idêntico (só dispara em mudança de composição), então nenhum quadro novo é atrasado. A ponte do neural apresenta por conta própria (`PokeReShade`, `PresentNow`) e a confirmação espera 2,5 s. A única mudança observável é que um toque no F6 com a fonte parada demora até 400 ms a mais para aparecer. Manter 100 ms enquanto `aquecendo` preserva a compilação do ReShade e a sonda do monitor sobreposto (que conta `scap.frames`, não presents).
Esforço: 5 linhas.

**S2. Cadência fixa de ~60 presents/s durante o aquecimento.**
Onde: mesmo bloco `main.cpp:8857–8858`, condição em `:8877`.
O que muda: enquanto `aquecendo && !pronta`, `gap = 16` (apresenta a ~60 Hz mesmo sem quadro novo).
Ganho: os 60 presents do `create_delay` do Feeder passam em ~1 s em vez de 4–7 s **[FATO: 6,8 s e 4,0 s medidos nas duas sessões]**. Tempo até o neural cai de ~12 s para ~7–8 s na sessão do `bin` **[INFERÊNCIA]**. Também encurta a fase em que o cartaz "INICIANDO A REDE NEURAL…" fica na tela.
Risco: baixo. Antes de a feature existir, a cadeia custa Kernel+SMAA+Feed (~2–3 ms/quadro) e a camada está em bypass (fora de vista), então ninguém vê nada. Não mexe na semântica do Feeder (`create_delay` continua sendo "60 presents"). Efeito colateral positivo: a primeira `frame 1 delivered (reset=1)` chega antes.
Esforço: 3 linhas. Alternativa pior: baixar `create_delay` no `dlss5-feed.cfg` (o app já grava esse arquivo em `GravarWorkResolution`, `:2784`) — é botão de terceiro com motivo documentado (RenoDX antigo re-arma hooks assincronamente); não recomendo sem teste no RenoDX clássico.

**S3. `TailArquivo` por referência e memorização do resultado analisado.**
Onde: `main.cpp:3638–3690` (assinatura), chamadores `:3705, :4008, :4041–4043, :6609, :8502`.
O que muda: (1) devolver `const std::string&` da entrada de cache em vez de cópia; (2) em `NeuralPeloLog`, `NgxPeloFeed` e no passo 3 de `LerEstadoNeural`, guardar o resultado (`int`/`bool`) junto com o carimbo (tamanho, mtime) do arquivo e só re-varrer quando o carimbo mudou — a própria `Entrada` já tem `tam` e `hora`; basta expor um "número de versão" que incrementa a cada releitura.
Ganho: elimina ~1 MB/s de memcpy+varredura na thread de render durante aquecimento/INICIANDO/painel aberto **[INFERÊNCIA ~0,5–1,5 ms/s]**; fora dessas fases, nada muda. Não é fps; é tirar trabalho do caminho crítico onde o Present já engasga.
Risco: muito baixo. Cuidado com a referência: o cache tem 3 vagas e `CadeiaEntregando` segura duas ao mesmo tempo; com a camada 2 ligada há um 4º arquivo (`bin-camada2\dlss5forall.log`) que atropela `cache[0]`. Solução: subir para 4 vagas ou copiar só quando houver 4º arquivo.
Esforço: ~25 linhas.

**S4. Não chamar `SetWindowTextW` quando o título não mudou.**
Onde: `main.cpp:9137–9143`.
Ganho: micro (evita um `WM_SETTEXT` e o redesenho da moldura pelo DWM 1×/s na janela separada, e a montagem de duas `std::wstring` de `TextoAtalho`). Risco nulo. Esforço: 2 linhas (guardar o último texto). Só vale por ser de graça.

**S5. Com teto de fps ativo, drenar sem copiar quando o cap diz "ainda não".**
Onde: `main.cpp:8871–8876` (cap) e `:8701` (`scap.Tick()`); `screen_capture.h:834–841` já tem o caminho "drenar e descartar" (`optNoCopy`).
O que muda: calcular `capOk` antes do `Tick` e, se `!capOk`, chamar um `Tick(false)` que só devolve os quadros ao pool sem `CopySubresourceRegion`.
Ganho: com fonte a 100 fps e teto 30, hoje são ~70 cópias de tela cheia por segundo jogadas fora (1,4 GB/s a 3440×1440 ≈ 2 ms de GPU/s) **[INFERÊNCIA]**. Pequeno, mas o teto existe justamente para quem está no limite de GPU.
Risco: baixo-médio. O comentário em `:8863–8870` registra que a quantização do teto é delicada e que uma tentativa de "escolher o quadro mais próximo do prazo" mediu pior. Esta mudança não altera qual quadro é apresentado (o próximo depois do prazo continua sendo copiado), só evita copiar os rejeitados. Precisa manter os contadores coerentes: hoje todo quadro copiado incrementa `scap.frames` (`:865`) mesmo quando o cap depois o descarta, e é `frames` que alimenta o "fps chegada" do título e a sonda do monitor sobreposto. Ao drenar sem copiar, incrementar `frames` também nesse caminho (ou passar a usar `arrived`, que o handler já conta), senão o título passaria a mostrar o fps do teto em vez do fps da fonte e a sonda de "5 s sem frame" dispararia com fonte viva.
Esforço: ~15 linhas.

**S6. Uma só chamada de `GetForegroundWindow` por volta.**
Onde: `main.cpp:6753` e `:6789`. Ganho: µs. Risco: nulo. Só se alguém já estiver mexendo em `FollowTarget`. Não espaçar a checagem de foco (a doc explica por quê: Alt+Tab).

**S7. `SetWindowPos` topmost do painel/guia/efeitos: 250/120 ms → 500 ms depois dos primeiros 5 s.**
Onde: `Hud::Tick 5542`, `Guia::Tick 5937`, `Efeitos::Tick 6403`, e o `RaiseNow` de 1 s em `FollowTarget 6839–6846`.
Ganho: micro (cada `SetWindowPos` topmost, mesmo sem mover, passa pelo gerenciador de janelas; ~20–50 µs). A briga pelo topo é real só nos primeiros segundos (a doc do Guia diz isso). Risco: baixo; visível só se algum jogo reafirmar topmost mais rápido que 500 ms. Prioridade baixa.

**S8. Cachear as duas `GetFileAttributesW` e o `GetModuleHandleW` de `LerEstadoNeural`.**
Onde: `main.cpp:3943–3947, 3975–3976`. Os arquivos não somem durante a sessão; um `static bool` depois do primeiro sucesso basta. Micro. Só junto com S3.

### Estruturais (ganho maior, exigem teste ou tocam shader/terceiro)

**E1. Desligar a reconstrução de normais do Kernel (`IMAGE_SPACE=1`).**
Onde: `lumenite_Kernel.fx:34–36` (`#ifndef IMAGE_SPACE`), pass em `:724`; a chave entra em `PreprocessorDefinitions` do `ReShade.ini` (`[GENERAL]`, já tem `DLSS5_MV_PROVIDER=3,...`) ou do `FafaDLSS5.ini`.
Por quê: com o depth plano do app (`main.cpp:447`, z=0,5) as normais são constantes. `tNormals` só é lido pelo passe de debug (`:646, :686`, compilado fora por `DEBUG_KERNEL=0`). `tDepth` **é lido** pelos passes de fluxo (`:201` teste `< 0.999` de validade; `:335–352` peso `|Δdepth| < 0.003` na filtragem), mas com profundidade plana esses testes respondem sempre "válido" e peso 1 **[FATO]**. Com `IMAGE_SPACE=1` o passe some e `tDepth` fica sem escrita (zeros): `0 < 0.999` continua verdadeiro e `|0 − 0| < 0.003` continua 1, então as decisões dos passes de fluxo não mudam **[INFERÊNCIA a confirmar no dump]**.
Ganho: 1 passe de tela cheia com 5 leituras de depth/px, duas gerações de mips (RGBA16F 4 níveis + R16F 4 níveis) e a alocação de `tNormals` (~53 MB a 3440×1440; `tDepth` continua declarada) **[INFERÊNCIA: 0,2–0,5 ms/quadro a 3440×1440]**.
Risco: médio. Verificar (1) que o `.fx` compila com `IMAGE_SPACE=1` (é um `#if` de fábrica do LumeniteFX); (2) que uma textura nunca escrita lê zero neste driver (o ReShade limpa texturas na criação, mas o comportamento de um alvo jamais renderizado precisa ser conferido); (3) o A/B pixel a pixel com `DLSS5_DUMP` antes/depois: a imagem tem de ser bit a bit igual. Se (2) falhar, a alternativa é editar o fork do `.fx` para pular só a reconstrução de normais mantendo a escrita de `tDepth` (um passe de tela cheia com 1 leitura em vez de 5, sem o alvo RGBA16F).
Esforço: 1 linha de ini + 1 sessão de A/B.

**E2. Early-out em `PS_FitSamples`/`PS_FitSolve` quando `GEOM_ENABLE=0`.**
Onde: `DLSS5_Feed.fx:540–548` e `:553–647`.
O que muda: `if (!GEOM_ENABLE) { P0 = P1 = ... = P5 = 0; return; }` no topo dos dois (ramo uniforme). Com `P5.z = 0`, `FitIsUsable()` (`:535–537`) continua falso, que é o estado de hoje.
Por quê: `FitSolve` é um único pixel que executa `2 × (920 iterações × acumulação de 45+18 termos) + eliminação 9×11` em série — uma thread de GPU trabalhando sozinha enquanto o resto da GPU espera o passe terminar. O preset do app tem `GEOM_ENABLE=0` **[FATO]** `FafaDLSS5.ini`.
Ganho: **[INFERÊNCIA]** 0,1–0,4 ms/quadro de latência de pipeline (é serial, não paralelo, então o custo não escala com a resolução e é fixo por quadro). Medir na aba Statistics do ReShade (tempo por técnica) antes de acreditar.
Risco: baixo; o app já distribui um fork próprio do `DLSS5_Feed.fx` (cabeçalho e uniforms em português nas docs). Reprocessar o cache de shader (`reshade-cache`) acontece sozinho.
Esforço: 4 linhas de HLSL.

**E3. Guardar o `LinearizeDepthPass` do SMAA atrás de um define.**
Onde: `SMAA.fx:90` (`#define SMAA_PREDICATION PredicationEnabled` — é uniform, não pré-processador) e `:291–296`.
O que muda: introduzir `SMAA_PREDICATION_PASS` (pré-processador, padrão 0) e envolver o pass e a declaração de `depthTex` com `#if`. Com predicação desligada (é o caso e a doc de efeitos diz que a detecção é por cor de propósito, `main.cpp:6052–6053`), o passe escreve um R16F de tela cheia que nada lê.
Ganho: 1 passe de tela cheia leve + 10 MB de VRAM a 3440×1440 **[INFERÊNCIA 0,05–0,1 ms]**. Risco: baixo (arquivo é fork local, `LICENSE-SweetFX-SMAA.txt` presente). Prioridade abaixo de E1/E2.

**E4. Um "modo leve" de MV exposto no painel (`mv_hardware=1`).**
Onde: já existe em `main.cpp:6906–6967` e `docs\motion-vectors.md`. Troca os 17 passes do Kernel na fila 3D por ~0,95 ms na fila de vídeo **[FATO doc]**, com perda medida de precisão entre 20 e 60 px/quadro. Não é otimização de código; é dar à pessoa com GPU no limite o botão que já existe no ini. Risco: o de produto (qualidade), já documentado.

**E5. Pré-aquecer o cache de disco enquanto o seletor está aberto.**
Onde: `ShowSelector` (`main.cpp:2558`) — abrir uma thread que faz `ReadFile` sequencial de `nvngx_dlssnr.dll` (166 MB), `nvngx_dlss.dll` (59 MB), `dxgi.dll`, `renodx-dlss5.addon64` e `reshade-shaders\Shaders\*` para o page cache.
Ganho: só na primeira abertura depois do boot ou em disco lento: os 1,55 s do `R.Init` (que incluem o RenoDX carregando o runtime de 166 MB **[FATO]** ReShade.log 04.449) podem cair para a metade em SSD e para segundos a menos em HDD **[INFERÊNCIA]**. Em abertura repetida o cache já está quente e o ganho é zero.
Risco: baixo (leitura pura, thread descartável). Esforço: ~30 linhas.

**E6. Feeder: pular a cópia de profundidade quando ela é constante.**
Onde: `dlss5-feed.cpp:3680` (`CopyResource` do depth). O app entrega sempre a mesma profundidade plana; a cópia de 4 B/px por quadro é redundante. Terceiro (MIT), mudança pequena mas fora do nosso repositório; ganho 0,03–0,06 ms/quadro **[INFERÊNCIA]**. Só vale se o Feeder for forkado por outro motivo. Idem para o `PS_Depth` do Feed (`DLSS5_Feed.fx:754`).

**E7. Não vale, mas registro para ninguém tentar: pipelining do Feeder (usar o Output do quadro N-1).**
Removeria as duas bolhas de sincronização de fila por quadro (`dlss5-feed.cpp` em torno de `:5211–5213` e `:5279`) ao custo de +1 quadro de latência. Contraria a prioridade declarada do projeto e a doc de latência; descartado.

---

## (c) O que NÃO vale a pena, e por quê

| Ideia | Por que não |
|---|---|
| Pular quadros idênticos / não reprocessar quando a fonte não mudou | Já acontece na fonte: o WGC só dispara `FrameArrived` quando a composição muda **[FATO]** (é a razão do "0.0 fps (alvo parado)"). O que sobra é o re-present ocioso do app — que é o S1. |
| Rodar SMAA ou Sharpen em quadros alternados | São filtros espaciais do quadro atual; alternar produz cintilação (SMAA) e "pulsação" de nitidez. Custo deles é 5 passes de tela cheia leves; o neural é 10× maior. |
| Substituir o `DLSS5_Sharpen` pelo RCAS do Feeder (`work_sharpness`) | Pouparia 1 passe e nada mais; muda o visual (o Sharpen do app tem proteção de halo e piso de ruído afinados em `docs\nitidez.md`). Decisão de imagem, não de desempenho. |
| `SkipLoadingDisabledEffects=1` | Os 9 efeitos extras compilam em paralelo; o tempo de parede é o do `DLSS5_Feed.fx` (1,64 s) **[FATO]**. E quebraria a troca automática para `DLSS5_MV_Hardware`, que a ponte liga em runtime (`dlss5_bridge_addon.cpp:645–658`) e não está em `Techniques=`. |
| Área de captura menor | Fonte janela já recorta a área cliente (`clientOnly`); fonte monitor é a tela inteira por definição. A única "área menor" útil é `work_resolution`, que já existe. |
| Espaçar `FollowTarget`/checagem de foco | Ganho de µs; atrasar o sumiço da camada no Alt+Tab é exatamente o que ela não pode fazer (já avaliado em `enxugar-painel.md` item 6). |
| Mover `LerEstadoNeural` para uma thread | Depois do S3 ela custa µs; a thread traria sincronização com `g_hud`. |
| Trocar o painel GDI+ por D3D | Custa 0,04 ms/s parado e ~1,8 ms quando muda **[FATO]**; não há o que ganhar. |
| Depth D24S8 → D16 no app | Poupa ~2 B/px no clear e na cópia do Generic Depth (~0,03 ms); com E1 aplicado ninguém mais lê essa profundidade além do Feed, que quer valor cru. Não paga o risco de o Generic Depth recusar o formato. |
| Reduzir os taps do `PS_MotionVectors` (`VALIDATE_STATIC/LUMA`) | É o que protege o DLSS de MV ruim (borrão em texto de HUD); custo ~45 leituras/px é aceitável e a doc de borrão pesou isso. |
| `ALLOW_TEARING`, `maxFrameLatency=2`, V-Sync, teto de fps como latência | Todos medidos e piores ou neutros em `latencia-nr.md §4.4` **[FATO]**. |
| Baixar `create_delay` do Feeder | Botão de terceiro com motivo (RenoDX clássico re-arma hooks assíncronos). S2 obtém o mesmo efeito sem tocar nele. |
| Mexer no `Present travou 895/449 ms` da abertura | São a compilação de efeitos do ReShade dentro do primeiro Present e a criação da feature NGX + init do NR; terceiros, uma vez por sessão, e o aquecimento em bypass já os esconde da pessoa. |

O que já está bem feito e deve ficar como está **[FATO]**: swapchain waitable + `SetMaximumFrameLatency(1)` com a espera **antes** do `Tick` (121 → 42 ms fim a fim, `latencia.md`); `Present(0,0)` sem tearing; dreno do WGC copiando só o mais novo; `MinUpdateInterval=0`; contexto imediato e geometria cacheados; PS passthrough com `HdrMode`/`UpFilter` zero custo; nenhuma alocação no laço; cache de moldura e assinatura do painel (24,7 → 0,04 ms); leitura do neural a cada 5 s com painel fechado; `gap` de 500 ms com a camada fora de vista; `TailArquivo` com stat-cache; LAT probe a ~4 QPC por quadro; janela de render na mesma thread com `WS_EX_LAYERED` (evita a espera de hit-test de 1,8 s).

---

## (d) Como medir antes e depois com o que o app já escreve

**Protocolo:** mesma fonte, mesma resolução, mesmo preset, 60 s por execução, 3 execuções por variante, sem outro processo na GPU (a doc registra contaminação de 45–64 % de carga externa nas rodadas anteriores). Comparar medianas.

1. **`dlss5forall.log`, linha `LAT …` (a cada 300 presents):** `fps chegada / copiado / present`, `tick` (custo do `scap.Tick`), `present` (CPU do `DrawAndPresent`, que é o ReShade+add-ons), `PRESENT->TELA` (fila até o vblank). Para S5, `copiado` deve cair para perto de `present` com teto ativo. Para S3, `present` p95 durante os primeiros 25 s.
2. **`dlss5forall.log`, `Encerrando (apresentados=N)`:** N ÷ segundos de sessão = taxa média de presents. Com fonte parada, antes do S1 dá ~10/s; depois, 2–2,5/s. É a prova direta do S1.
3. **`dlss5forall.log`, `aquecimento: camada liberada apos N ms`:** tempo até o neural (S2 e E5). Complementar com o `dlss5-feed.log`: intervalo entre `session open` e `building` (é o `create_delay` em segundos).
4. **`dlss5-feed.log`, `600 frames: feed CPU x ms/frame | frame interval y ms | feed is N% | worst frame … evaluate …`:** `frame interval` é o custo de GPU por quadro quando a GPU limita. Pôr `stall_log_ms=50` no `dlss5-feed.cfg` para ganhar as linhas `STALL … outside the feed … verdict` (o Feeder já sabe dizer se a trava foi no NGX, no add-on ou fora).
5. **Aba Statistics do overlay do ReShade (Home):** tempo de GPU por técnica. É a única forma direta de medir E1, E2 e E3 (Lumenite_Kernel, DLSS5_Feed, SMAA, DLSS5_Sharpen em ms). Anotar antes/depois com a mesma cena parada.
6. **`DLSS5_DIAG=1`:** linhas `painel: N repinturas, x ms media, y ms pior; z pulos`. Para S4/S7 não há linha; usar `typeperf "\Processor(_Total)\% Processor Time"` ou o Gerenciador de Tarefas, coluna do processo.
7. **GPU no ocioso (S1):** `typeperf "\GPU Engine(*engtype_3D)\Utilization Percentage" -si 1` com a fonte parada, 30 s, antes e depois.
8. **Regressão visual (E1, E2, E3):** `DLSS5_DUMP=<png>` no mesmo quadro (`DLSS5_DUMP_FRAME`) antes e depois; a saída tem de ser idêntica pixel a pixel, porque nenhuma das três mudanças altera dado consumido.

---

## Ordem sugerida

1. **S2** (cadência no aquecimento) e **S1** (re-present ocioso): mesmo bloco de código, 8 linhas, medíveis pelos itens 2 e 3.
2. **S3** (`TailArquivo` por referência + memorização): 25 linhas, tira trabalho do render nos piores segundos.
3. **E2** e **E1**: 1 linha de ini e 4 de HLSL; medir na aba Statistics e no dump.
4. **S5** (drenar sem copiar com teto): só se o teto de fps for o botão que as pessoas usam.
5. **E3**, **S4**, **S6–S8**: polimento, quando alguém já estiver naquele arquivo.
6. **E5** (pré-aquecer disco): vale para quem abre o app uma vez por sessão de jogo em HDD; medir a primeira abertura após reboot.
