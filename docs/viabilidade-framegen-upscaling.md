# Viabilidade: frame generation e upscaling no DLSS5 For All

Parecer técnico — 2 de setembro de 2026.
Método: leitura de `main.cpp`, `screen_capture.h`, logs e configs; extração de strings dos binários
`dlss5-feed.addon64` e `renodx-dlss5.addon64`; pesquisa em documentação oficial (NVIDIA NGX/Streamline,
NVIDIA Optical Flow SDK, AMD GPUOpen/FidelityFX) e no repositório upstream do DLSS5-Feeder.

**O app não foi executado** (o PC está em uso) e **nenhum arquivo do projeto foi modificado** —
as mudanças propostas estão aqui como diff, não aplicadas.

---

## 1. Estado real verificado

| Peça | Versão / fato | Onde foi verificado |
|---|---|---|
| `dlss5-feed` | **0.7.0** (build 31/ago/2026) | `dlss5-feed.log` |
| `renodx-dlss5` | v0.2026.828.517 | `ReShade.log` |
| ReShade | 6.8.0.2155, carregado de `dxgi.dll` | `ReShade.log` |
| GPU / driver | GeForce RTX 4080, driver 596.49 | `ReShade.log` |
| Runtime NR | `nvngx_dlssnr.dll` sha256 `6EB209E7…3927` — *"custom runtime accepted; untested build"* | `ReShade.log` |
| Swapchain | 3420x1367, `B8G8R8A8_UNORM`, `FLIP_DISCARD`, 2 buffers, `Windowed=TRUE` | `ReShade.log` + `main.cpp:416-433` |
| Present | **`sc->Present(1, 0)` — vsync ligado** | `main.cpp:508` |
| Backbuffer | sempre igual ao tamanho do recorte capturado | `main.cpp:1214`, `1222-1224`, `1318-1320` |
| Depth | textura `R24G8_TYPELESS` real, plana em z = 0.5 | `main.cpp:339-347`, `385-394` |
| F11 / Alt+Enter | só redimensiona a **janela**; **não existe handler de `WM_SIZE`**, então o swapchain nunca é recriado | `main.cpp:521-537`, `539-558` |
| WGC | pool de 2 buffers (`optPoolBuf = 2`), drena a fila e copia **só o frame mais novo** | `screen_capture.h:321`, `394`, `457`, `573-585` |
| Modo overlay | `WS_EX_TOPMOST`, reafirma o topo a cada 1 s | `main.cpp:980`, `1019`, `1026-1031` |
| `hideSelf` (fonte janela) | `false` por padrão (`C.hideSelf = argHide`) | `main.cpp:1125` |
| `hideSelf` (fonte monitor) | `true` por padrão (`!argNoHide`), e **forçado** quando `overlay` | `main.cpp:1133`, `1187` |
| `WDA_EXCLUDEFROMCAPTURE` | aplicado quando `hideSelf` | `main.cpp:808-815`, `1217` |
| Feed cfg atual | `mode=2 preset=0 work_resolution=100 mv_scale=1.000,1.000` | `dlss5-feed.cfg` |
| RenoDX cfg atual | `NREnableUpscaling=1`, `EnableHooks=2` (NGX only, Streamline não patchado) | `ReShade.ini` |
| Estado do feed no último run | `feature ready: 1334x783 DLAA` · `building: 1334x783 work resolution (100%) -> 1334x783 backbuffer` | `dlss5-feed.log` |

---

## 2. Upscaling

### 2.1 O que o Feeder realmente faz — evidência das strings do binário

Strings extraídas de `dlss5-feed.addon64` (0.7.0):

```
[feed] feature ready: %ux%u DLAA, flags=%d (%s%s%s%s), color %s -> output %s, depth R32_FLOAT%s, mv R16G16_FLOAT
[feed] building: %ux%u work resolution (%d%%) -> %ux%u backbuffer %s (mv %s, depth %s, depth reversed=%d)
[feed] copy-back and work-resolution resample shaders ready
[feed] work-resolution source: %ux%u staging copy -> %ux%u
[feed] settled D3D11 work resolution=%d%%; rebuilding private resources
Work resolution (%)
Work resolution: 100%% (adjustable path currently supports 64-bit D3D11)
```

Três coisas se concluem daí:

1. **A feature DLSS é criada sempre como DLAA.** O formato `feature ready: %ux%u DLAA` tem **uma única**
   resolução e a palavra "DLAA" é **literal**, não um `%s`. Não existe caminho no binário que crie a feature
   com render ≠ output.
2. Os `DLSS.Hint.Render.Preset.{DLAA,Quality,Balanced,Performance,UltraQuality,UltraPerformance}` que aparecem
   no binário **não são modos de qualidade** — são os *hints de preset temporal* (A–F), que controlam o quanto
   o DLSS trava o histórico contra o frame atual. É o que o `preset=0` do nosso `.cfg` seleciona. O tooltip do
   próprio add-on diz: *"Presets differ in how hard DLSS clamps history against the current frame."*
3. **`work_resolution` é knob de custo, não upscaling.** Tooltip verbatim do binário:

   > *"Scales both axes of the private DLAA + Neural Rendering work textures. **The game/backbuffer stays
   > native-sized.** Applied once 400 ms after dragging stops."*

   O fluxo é: downsample do backbuffer para a work res → DLAA + NR na work res → *resample* de volta ao
   backbuffer. No **0.7.0 esse resample é bilinear**; o **FSR 1 (EASU + RCAS) só entrou no 0.12.0**.

E a string `Work resolution: 100% (adjustable path currently supports 64-bit D3D11)` confirma que **o nosso caso
é justamente o caminho ajustável** (somos 64-bit D3D11).

O FAQ upstream do DLSS5-Feeder fecha a questão sem margem:

> *"render resolution still equals DLAA output resolution"*
> *"Quality/Balanced/Performance mode cannot be added"*
> *"the result can never exceed the native frame the game rendered"* (release notes do 0.12.0)

**Onde se configura:** slider *"Work resolution (%)"* no painel do add-on `DLSS 5 Feed` (aplica 400 ms depois de
soltar o slider), ou a chave `work_resolution=` em `dlss5-feed.cfg` (hoje `100`). Faixa 50–100%.

### 2.2 Por que o `NREnableUpscaling=1` do RenoDX é inócuo hoje

O `ReShade.ini` já tem `NREnableUpscaling=1` em `[RenoDX.DLSS5]`, mas ele não faz nada. Strings de
`renodx-dlss5.addon64`:

```
Enable Upscaling (WIP)
DLSSNR.Upscaling
DLSSNR.ScalingRatio
DLSSNRComputeScalingRatioCallback
NR upscaling is not applicable: the game's DLSS already renders at
NR upscaling fell back to native: the signed runtime rejected the
NR upscaling is blocked for this title and following frames use the native path
Successful NR frames: %llu | Guides: %ux%u | Output: %ux%u
Upscaling: requested %s | active %s | ratio %.2f
). To get upscaling, select a DLSS Balanced/Performance preset in the NVIDIA App and restart. NR continues on the native path.
```

O upscaling do RenoDX só engata quando o contrato NGX do host já é **sub-nativo** — quando `Guides` (a resolução
de entrada) é menor que `Output`. Como o Feeder entrega `guides == output` (DLAA), a razão é 1.00 e o add-on cai
sempre no caminho nativo, imprimindo `NR upscaling is not applicable`.

Isso também significa: **se um dia o contrato virasse sub-nativo, o upscaling do RenoDX passaria a engatar
sozinho** — a chave já está ligada. O bloqueio está no Feeder, não no RenoDX.

Vale registrar o outro lado: o RenoDX declara explicitamente que **não encosta em frame generation**:

```
) is not DLSS/DLSSD; frame generation and other NGX features are untouched
```

### 2.3 O que se ganha e o que se perde no nosso caso

Nossa entrada é um **frame já finalizado**, não um render de engine. Isso impõe um teto duro: nenhuma quantidade
de DLSS recupera detalhe que o jogo não renderizou. Capturar em resolução menor e apresentar maior, mantendo o
jogo na mesma resolução, é **perda pura** — é exatamente o que o `work_resolution < 100%` faz, e por isso ele é
vendido como knob de custo.

**O caminho de valor real existe, mas não é onde parece.** Ele não está na nossa overlay — está em **o jogo
renderizar menor** e a cadeia fazer a ampliação bem. E aqui há um achado concreto:

> **Hoje, quem faz essa ampliação é o DXGI/DWM com bilinear burro.**

Porque `ToggleFullscreen` (`main.cpp:521-537`) só muda o estilo e o tamanho da **janela** — e **não existe
handler de `WM_SIZE`** no `RenderWndProc` (`main.cpp:539-558`). O swapchain continua no tamanho do recorte
(o único `R.Resize` do loop é disparado por mudança da *captura*, `main.cpp:1318-1320`). Então em F11 num
monitor 1440p com fonte 720p, o resultado é: cadeia inteira em 720p (barato, bom) → **esticão bilinear do DXGI
até 1440p** (ruim).

Trocar esse esticão por FSR 1 é o ganho concreto disponível, e é uma mudança pequena.

### 2.4 Diff proposto — desacoplar o backbuffer da captura

**Não aplicado.** Mexe em `main.cpp`, que está com outro agente.

O shader atual (`main.cpp:334-351`) gera a grade por `SV_VertexID` cobrindo o viewport inteiro e amostra o SRV
com sampler **LINEAR** (`main.cpp:453-457`). Escala arbitrária entre fonte e backbuffer **já funciona** — o HLSL
não muda em nada.

```diff
  struct Renderer {
      ...
      UINT w = 0, h = 0;          // <- passa a ser SÓ o backbuffer
+     UINT srcW = 0, srcH = 0;    // <- resolução da fonte, independente
  };
```

```diff
@@ main.cpp:1316-1326  (loop principal, fonte de tela)
          got = scap.Tick();
          if (got) {
-             if (scap.w != R.w || scap.h != R.h) {
-                 if (!R.Resize(scap.w, scap.h, &err)) Fatal(L"Falha ao redimensionar:\n" + err);
-                 ResizeClientTo(hwnd, R.w, R.h);
-                 Log("Alvo mudou de tamanho: %ux%u", R.w, R.h);
-             }
+             // a captura só troca o SRV; o tamanho do backbuffer é assunto da JANELA
+             if (scap.w != R.srcW || scap.h != R.srcH) {
+                 R.srcW = scap.w; R.srcH = scap.h;
+                 Log("Alvo mudou de tamanho: %ux%u", R.srcW, R.srcH);
+                 // em janela normal (não overlay, não tela cheia) a janela ainda acompanha a fonte
+                 if (!C.overlay && !g_fullscreen) ResizeClientTo(hwnd, R.srcW, R.srcH);
+             }
              R.extSrv = scap.srv;
          }
```

```diff
@@ main.cpp:539-558  (RenderWndProc) — handler que hoje não existe
      case WM_ERASEBKGND: return 1;
+     case WM_SIZE:
+         if (wp != SIZE_MINIMIZED && g_renderer) {
+             RECT rc{}; GetClientRect(h, &rc);
+             UINT nw = (UINT)(rc.right - rc.left), nh = (UINT)(rc.bottom - rc.top);
+             if (nw && nh && (nw != g_renderer->w || nh != g_renderer->h)) {
+                 std::wstring e;
+                 if (!g_renderer->Resize(nw, nh, &e))
+                     Log("ResizeBuffers falhou no WM_SIZE: %s", U8(e).c_str());
+             }
+         }
+         return 0;
      }
```

```diff
@@ main.cpp:521-537  (ToggleFullscreen) — só registrar o estado, o WM_SIZE faz o resto
  static void ToggleFullscreen(HWND h) {
      static WINDOWPLACEMENT wp{ sizeof(wp) };
      DWORD st = (DWORD)GetWindowLongPtrW(h, GWL_STYLE);
      if (st & WS_OVERLAPPEDWINDOW) {
          ...
+         g_fullscreen = true;
      } else {
          ...
+         g_fullscreen = false;
      }
  }
```

Pré-requisitos: um `Renderer* g_renderer` (ou equivalente) visível para o `WndProc`, e um `bool g_fullscreen`.
`Renderer::Resize` já libera e recria RTV + depth corretamente (`main.cpp:470-477`, `409-412`), então não há
nada novo a escrever ali.

**Como usar depois de aplicado:** jogo em janela 1280x720 → F11 na nossa janela num monitor 2560x1440 →
backbuffer 1440p → `work_resolution = 50%` no painel do Feeder → DLAA + NR rodam em 720p (mesmo custo de hoje)
→ **FSR 1 (EASU + RCAS) do Feeder 0.12.0** faz o 720p→1440p, com slider de Sharpness.

### 2.5 Ganho visual esperado da troca — avaliação honesta

Isso é o que interessa para a decisão de aplicar agora ou não.

**O que melhora, e é real:** a ampliação final deixa de ser bilinear do DXGI e passa a ser **EASU + RCAS**.
Em fator 2x (720p→1440p) a diferença é bem visível: bordas com muito menos escada e serrilha, texto e HUD
legíveis em vez de borrados, e o RCAS devolvendo micro-contraste que o bilinear come. O próprio changelog do
0.12.0 descreve como *"much crisper at 50–75%"*. É o mesmo salto de qualidade que FSR 1 dá sobre um esticão
de driver em qualquer jogo — modesto perto de um upscaler temporal, mas nada sutil perto de bilinear.

**O que piora, e precisa ser dito:** o `Lumenite_Kernel` e o `DLSS5_Feed.fx` passam a rodar na resolução de
**saída**, não na da fonte. Custa mais (dois passes de tela cheia a 1440p em vez de 720p) e, pior, os motion
vectors passam a ser estimados **sobre uma imagem já esticada** — optical flow sobre pixels interpolados é
menos confiável que sobre pixels nativos. O `work_resolution` cobre o custo do DLAA/NR, mas **não** o dos
efeitos ReShade, que sempre rodam no backbuffer.

**E há um round-trip:** nosso shader sobe 720→1440 (bilinear), e o Feeder desce 1440→720 para a work res.
Não é lossless — há um borrão de ida e volta antes do neural ver a imagem.

**Recomendação:** vale aplicar, mas medir os dois lados. O ganho é no filtro final; o custo é MV pior e dois
passes mais caros. Se o fps cair demais, a alternativa mínima e sem efeito colateral é não desacoplar nada e
apenas rodar um passe espacial (FSR 1 / CAS) por último — mas isso não é possível hoje, porque **o ReShade roda
sempre na resolução do backbuffer**, e é exatamente por isso que o desacoplamento é o mecanismo necessário.

### 2.6 O que dá para ter hoje, sem tocar em código

Trocar o `dlss5-feed.addon64` 0.7.0 pelo **0.12.0** e usar `work_resolution` em **67–75%**. Isso não é upscaling,
mas com o FSR 1 expand-back o custo/qualidade fica muito melhor do que era com bilinear. Recupera fps para
compensar o que o NR come (o README já registra ~metade do fps em 1080p com NR ligado).

### 2.7 Veredito — upscaling

| Caminho | Veredito | Esforço | Ganho | Risco |
|---|---|---|---|---|
| DLSS Quality/Balanced/Performance **nesta cadeia** | **Inviável** — o Feeder é DLAA por construção e o autor afirma que não dá para adicionar | — | — | — |
| DLSS real escrevendo nosso próprio integrador NGX (substituir o Feeder) | Viável com **esforço muito grande**; ganho duvidoso, a fonte continua sendo um frame pronto | semanas | baixo | reescrever o que o Feeder já faz |
| Feeder 0.12.0 + `work_resolution` 67–75% + FSR 1 | **Viável hoje** | minutos | recupera fps, filtro decente | baixo |
| Backbuffer desacoplado (jogo 720p → saída 1440p com FSR 1) | **Viável** | ~1 dia | troca bilinear do DXGI por EASU+RCAS | mexe em `main.cpp`; MV pior; efeitos mais caros |

---

## 3. Frame generation

### 3.1 DLSS Frame Generation (`nvngx_dlssg.dll`) — **INVIÁVEL**

Quatro bloqueios independentes. Cada um, sozinho, já encerra o assunto.

1. **API.** `sl.dlss_g` suporta **DirectX 12 e Vulkan 1.2+**. **D3D11 não é suportado.** Nosso Present é D3D11
   (`main.cpp:426`, `508`). Não há flag, workaround ou fallback — é ausência de backend.
2. **Reflex é obrigatório.** A documentação é explícita: `sl.reflex` precisa estar integrado, e *o SDK Reflex
   normal não substitui*. Além disso, o frame index passado em `slSetConstants()` precisa casar com os present
   markers do Reflex.
3. **Buffers que não existem no nosso mundo.** DLSS-G exige **HUDless color** (a cena completa **sem** UI) e
   **UI color + alpha** como recursos separados, ambos na resolução exata do backbuffer. Nosso frame **é** o jogo
   com o HUD já queimado dentro dos pixels. Separar HUD de cena a partir de uma imagem pronta é um problema de
   segmentação, não de engenharia de pipeline.
4. **`sl::Constants` com matrizes de câmera.** São exigidos `clipToPrevClip`, jitter e escala de MV, todos
   row-major e **sem offset de jitter**. Optical flow não produz matriz de câmera — não há como derivar
   `clipToPrevClip` de um campo de fluxo 2D estimado. Não é difícil: é indefinido.

O que o RenoDX/Feeder expõem nesse sentido: **nada**. O RenoDX declara que passa longe
(`is not DLSS/DLSSD; frame generation and other NGX features are untouched`), e o Feeder só sabe montar um
contrato de **DLAA** — ele nem chama a família DLSSG.

Reescrever o app em D3D12, integrar Streamline e ainda assim ter que inventar HUD-less e matrizes de câmera:
custo de semanas para um resultado que o próprio NGX rejeitaria ou entregaria com artefato pesado em UI.
**Não atacar.**

### 3.2 FSR 3.1 / FidelityFX frame interpolation — **VIÁVEL COM ESFORÇO GRANDE, não recomendado**

- **Portabilidade.** O FidelityFX SDK oficial só implementa frame interpolation no **backend D3D12 nativo**
  (Vulkan está em desenvolvimento). O fork `optiscaler/FidelityFX-SDK-DX11` **porta o SDK para DX11 mas não traz
  a interpolação junto** — a limitação *"At this time, FSR3 only supports use of the native DirectX 12 backend"*
  continua de pé nele.
- **O que seria preciso.** Abrir um device D3D12 nosso e compartilhar texturas com o D3D11 — o Feeder já prova
  que isso funciona (ele mantém um D3D12 privado e faz `D3D11->D3D12` com texturas compartilhadas; ver as
  strings `[feed] %-6s %ux%u %s via D3D11->D3D12 (d3d12 flags=0x%X%s)`). Mas o `FrameInterpolationSwapChain`
  quer ser **dono do Present** — e o Present é exatamente onde o ReShade e a cadeia inteira moram. Briga direta
  pela posse do swapchain, com o ReShade injetado via `dxgi.dll` no meio.
- **Qualidade com MV estimado.** O FSR 3 roda optical flow próprio **além** dos motion vectors da engine. Sem MV
  real, o disocclusion mask degrada — as áreas que aparecem atrás de objetos em movimento são justamente onde
  MV estimado erra mais.
- **Licença:** **MIT**. Sem risco jurídico.
- **Esforço:** semanas. **Ganho sobre o Smooth Motion: nenhum evidente.**

### 3.3 NVIDIA Optical Flow Accelerator (NVOFA) direto / **NvOFFRUC** — **VIÁVEL, esforço médio**

Este é o caminho "faça você mesmo" que realmente fecha, porque a NVIDIA já entrega pronta a peça difícil: a
biblioteca **FRUC** (*Frame Rate Up Conversion*) do **Optical Flow SDK**.

- **API:** `NvFRUCCreate` → `NvFRUCRegisterResource` → `NvFRUCProcess` (em loop) → `NvFRUCUnregisterResource`
  → `NvFRUCDestroy`.
- **Suporta `ID3D11Device` nativamente**, com superfícies **ARGB** e NV12, e sincronização por `ID3D11Fence` ou
  `IDXGIKeyedMutex`. É o único caminho de FG desta lista que fala D3D11 de forma nativa.
- **Calcula o optical flow ela mesma**, no **NVOFA em hardware** da Ada + CUDA. Não precisa de MV da engine,
  **não depende do Lumenite Kernel nem do `DLSS5_Feed.fx`** — remove a dependência mais frágil da cadeia.
- **Hardware:** Turing ou superior. A 4080 (Ada) está coberta com folga.

**Onde isso encaixa no nosso pipeline — e é um detalhe que muda o plano:** precisamos do frame **já filtrado**
pelo ReShade/DLSS5. Isso só existe **depois** do Present. Logo, **não é código no `main.cpp`** — teria que ser um
**add-on ReShade nosso**, engatado em `reshade::addon_event::present` ou `finish_effects`, guardando o backbuffer
N-1, gerando o intermediário e apresentando dois frames por frame capturado.

- **Esforço:** dias, não semanas. É o melhor custo/benefício entre os caminhos "próprios".
- **Ponto fraco sério:** em D3D11 não existe swapchain proxy, então **o pacing é manual** — dois Presents por
  frame capturado, ritmados por nós. Pacing ruim anula o benefício visual inteiro, e é o ponto onde este
  caminho pode fracassar mesmo funcionando tecnicamente.
- **Risco de licença:** usar é livre. **Redistribuir `NvOFFRUC.dll` exige ler a EULA do Optical Flow SDK antes.**
  A documentação pública traz apenas o aviso genérico *"for information purposes only"*, sem termos explícitos
  de redistribuição — precisa ser verificado no pacote do SDK.

### 3.4 Interpolação por rede (RIFE e similares) via DirectML / TensorRT — **VIÁVEL, projeto grande, pior custo-benefício**

RIFE 4.x compilado em TensorRT FP16 roda 1080p em poucos milissegundos numa 4080 — tecnicamente cabe no
orçamento de frame. Mas:

- exige exportar o modelo, compilar a engine, versionar por driver, e gerenciar layout NHWC/NCHW e conversão
  de formato a cada frame;
- **compete diretamente com o NR pela mesma GPU** — e o NR sozinho já derruba para ~metade do fps em 1080p
  (o próprio README registra isso);
- o resultado dificilmente supera o NVOFA em hardware ou o Smooth Motion, que custam uma fração disso.

Só faz sentido como pesquisa, não como entrega. **Não atacar.**

### 3.5 Lossless Scaling (LSFG) por cima da nossa janela — **FUNCIONA HOJE, SEM CÓDIGO NOSSO**

**Sim, funciona hoje, sem uma linha de código nossa** — desde que a configuração esteja certa. Esta é a rota
prática de curto prazo junto com o Smooth Motion.

**Ordem correta da cadeia:**

```
jogo (janela/borderless)
  → nossa captura WGC da JANELA do jogo
  → ReShade: Lumenite Kernel → DLSS5_Feed → RenoDX NR
  → nosso Present (DLSS5ForAll.exe)
  → Lossless Scaling captura a NOSSA janela
  → LSFG interpola
  → LS apresenta
```

**As duas capturas não brigam.** Nós capturamos a **janela do jogo** com WGC de janela — que pega o conteúdo da
janela alvo, não a região da tela. O LS captura a **nossa** janela. São alvos diferentes; **não há loop de
espelho** e nenhuma das duas rouba a outra.

**O que o usuário precisa configurar — e a armadilha, que é do nosso próprio código:**

O LS captura por WGC/DXGI. Se a nossa janela estiver marcada com `WDA_EXCLUDEFROMCAPTURE` (`main.cpp:1217`),
**o LS não enxerga absolutamente nada** — tela preta ou recusa de capturar. Quando isso acontece:

| Configuração | `hideSelf` | LS consegue capturar? |
|---|---|---|
| Fonte **janela**, modo janela separada | `false` (padrão, `main.cpp:1125`) | **Sim** |
| Fonte **janela**, modo overlay | `false` (padrão), a menos que `--hide-self` | Sim, mas ver abaixo |
| Fonte **monitor**, qualquer modo | `true` (padrão, `main.cpp:1133`) | **Não** |
| Fonte **monitor** + overlay | `true` **forçado** (`main.cpp:1187`) | **Não** |

Segunda armadilha: o LS captura *"o elemento mais no topo"* da tela, e a nossa janela em modo overlay é
`WS_EX_TOPMOST` **e reafirma o topo a cada 1 segundo** (`main.cpp:1026-1031`). Ela e a janela de saída do LS
vão brigar pelo topo indefinidamente.

**Configuração recomendada ao usuário:**

1. Rodar o DLSS5 For All em **modo janela separada** (caixa *"SOBREPOR o alvo"* **desmarcada**), fonte
   **janela**, apontando para o jogo. Não usar overlay, não usar fonte monitor.
2. Deixar a nossa janela em foco e apertar o atalho de Scale do LS — **a janela que ele aponta é a
   `DLSS5ForAll.exe`**, nunca a do jogo. Apontar para o jogo devolveria o frame *sem* o filtro.
3. Desligar outras overlays (GeForce Experience Highlights, Discord, etc.) — a documentação do LS avisa que
   qualquer overlay acima da janela dele quebra o frame generation.
4. **Não usar limitador de fps externo.** Só o jogo deve ter o limite real; limitador em cima do LS causa
   latência dobrada.
5. Como o nosso Present é `Present(1, 0)` (vsync ligado, `main.cpp:508`), o LS terá menos margem de pacing.
   Se ficar irregular, é o primeiro item a ajustar (ver §5, proposta de `--vsync 0`).

**Latência somada:** LSFG em modo 2x fixo custa **~1,5 a 2,5 frames** adicionais, **em cima** dos 2–3 frames que
a nossa cadeia já tem (§4). A 60 fps de entrada isso é algo entre **+25 e +42 ms** só do LS.

**Licença/legal:** o Lossless Scaling é proprietário e **não redistribuível** — mas ninguém precisa redistribuir
nada. O usuário usa a cópia que ele comprou na Steam. Basta documentar o passo a passo. **Zero risco jurídico
adicional**, e não piora em nada a exposição que a DLL vazada já cria.

### 3.6 NVIDIA Smooth Motion — **A MELHOR APOSTA DE CURTO PRAZO**

Frame generation **no driver**, funciona em qualquer aplicação DX11/DX12 (e Vulkan em driver recente). Liga-se
por **perfil de programa** no NVIDIA App (*Graphics → Program Settings → `DLSS5ForAll.exe` → Driver Settings →
Smooth Motion*) ou pelo NVIDIA Profile Inspector. **Zero linha de código nossa.** Oficialmente liberado para a
linha RTX 40 — a 4080 com driver 596.49 está coberta.

A evidência mais forte de que esta é *a* rota da comunidade para esta cadeia específica: **o próprio autor do
DLSS5-Feeder já trabalhou nisso**. O histórico de releases tem:

- `v0.8.0-beta.4` — **NVIDIA Smooth Motion compatibility**
- `v0.11.0-beta.2` — **Smooth Motion runtime binding**

> ### Aviso que muda a ordem das prioridades
>
> **Nossa versão é a 0.7.0, anterior a esse fix.**
>
> O changelog do `v0.8.0-beta.4` diz, verbatim, que o feeder **não era thread-safe**:
> *"The feeder was not thread-safe. It had exactly one lock in the whole add-on, and it was for the log file."*
>
> E explica o mecanismo da falha: o Smooth Motion **embrulha o swapchain e chama `Present` várias vezes por
> frame do jogo, a partir de threads diferentes** — exatamente a corrida que um lock só de log não cobre.
>
> O fix: *"serializes the whole per-frame path — one lock plus a re-entrancy guard, at the single dispatch point
> all four backends go through"*, liga `ID3D11Multithread` no contexto imediato do jogo, detecta e reporta
> ativação do Smooth Motion no overlay e nos logs, e passa a exigir **três** timeouts consecutivos de GPU antes
> de parar o feed (em vez de desabilitar permanentemente no primeiro).
>
> **Conclusão: ligar Smooth Motion hoje, com o Feeder 0.7.0, é pedir corrupção de imagem ou crash.**
> Atualizar o `dlss5-feed.addon64` para ≥ 0.8.0-beta.4 (de preferência **0.12.0**, que traz junto o FSR 1
> expand-back) **não é opcional** — é o passo 1.

Se aparecer artefato mesmo depois de atualizar, o changelog recomenda: desligar o Smooth Motion **seletivamente
por API** via o campo de bitfield do NVIDIA Profile Inspector (em vez de desligar globalmente), e usar os
**Debug Bars** para identificar se o artefato está nos frames gerados ou nos reais. Para reportar bug ao autor,
ele pede as linhas `MV probe` / `Depth probe` do `dlss5-feed.log` e, em runs com Smooth Motion, as linhas
`fed from thread`.

**Segundo ajuste, do nosso lado:** `sc->Present(1, 0)` (`main.cpp:508`) é **vsync ligado**. Frame generation de
driver quer teto de fps **abaixo** do refresh para ter espaço de inserir o frame gerado. Proposta em §5.

---

## 4. Latência — a conta honesta

**O que já existe hoje, antes de qualquer frame generation:**

| Fonte | Custo |
|---|---|
| Windows Graphics Capture | 1–2 frames |
| Nosso `Present(1, 0)` com vsync | 1 frame |
| Custo do NR (ReShade + DLAA + Neural Rendering) | variável; ~metade do fps em 1080p na 4080 |

Ponto positivo: o `optDrain` (`screen_capture.h:573-585`) já drena a fila do WGC e **copia apenas o frame mais
novo**, devolvendo os antigos sem gastar banda. Isso está bem feito e não é onde a latência mora.

**Toda** frame generation por interpolação **soma** por cima disso, sem exceção — porque ela **segura o frame
novo** para interpolar contra o anterior antes de apresentar o intermediário. É matemática, não implementação:

| Caminho | Latência adicional |
|---|---|
| Smooth Motion | +1 frame renderizado + pacing do driver |
| Lossless Scaling 2x | +1,5 a 2,5 frames |
| NvOFFRUC próprio | +1 frame + pacing manual (nosso, mais arriscado) |
| FSR 3.1 FI | +1 a 2 frames |
| RIFE/TensorRT | +1 frame + tempo de inferência |

A 60 fps de entrada, cada frame vale ~16,7 ms. Então FG soma **+16 a 42 ms** dependendo da rota, empurrando o
total do pipeline para a faixa de **~60 a 100 ms**.

**Conclusão sem enfeite:** num pipeline que já é "espelho + filtro", frame generation serve para **vídeo, jogo
lento e demonstração — nunca para competitivo**. O que o README já diz ("dá pra jogar coisa lenta; pra jogo
competitivo, não") fica mais verdadeiro ainda.

E o corolário que decide a prioridade: **como o trade de latência é praticamente o mesmo em todos os caminhos**,
escolher aquele que custa **zero código** é a decisão óbvia. Não existe rota que compre latência menor com mais
esforço de engenharia — só rotas que custam mais para chegar no mesmo lugar.

---

## 5. Proposta menor: expor o vsync

Vale para o Smooth Motion **e** para o Lossless Scaling — os dois querem que o app tenha teto abaixo do refresh.

```diff
@@ main.cpp:508
-         sc->Present(1, 0);
+         sc->Present(g_vsync ? 1 : 0, 0);
```

com `--vsync 0` na linha de comando e/ou uma opção no seletor, persistida em `dlss5forall.ini` junto com
`fonte` e `telacheia`. Sem vsync convém um limitador simples no loop (o `lastPresent`/`gap` de
`main.cpp:1328-1333` já é o lugar natural para isso).

---

## 6. Veredito consolidado

| Caminho | Veredito | Esforço | Ganho | Latência somada | Risco |
|---|---|---|---|---|---|
| **NVIDIA Smooth Motion** no perfil do exe | **Viável hoje** | ~0 (após atualizar o Feeder) | 2x fps percebido | +1 frame + pacing | **corrupção/crash se ficar no 0.7.0** |
| **Lossless Scaling** por cima, modo janela | **Viável hoje** | ~0 | 2x–4x | +1,5–2,5 frames | conflito de captura (`EXCLUDEFROMCAPTURE`) e de topmost; proprietário |
| Feeder 0.12.0 + `work_resolution` 67–75% + FSR 1 | **Viável hoje** | minutos | recupera fps, filtro melhor | 0 | baixo |
| Backbuffer desacoplado (720p → 1440p com FSR 1) | **Viável** | ~1 dia | troca bilinear do DXGI por EASU+RCAS | 0 | mexe em `main.cpp`; MV pior; efeitos mais caros |
| **NvOFFRUC** via add-on ReShade nosso | **Viável, esforço médio** | dias | FG próprio, sem depender de terceiro | +1 frame + pacing manual | EULA do OF SDK; pacing manual em D3D11 |
| FSR 3.1 frame interpolation | **Viável com esforço grande** | semanas | nenhum sobre o Smooth Motion | +1–2 frames | briga pela posse do Present |
| RIFE / DirectML / TensorRT | **Viável, projeto grande** | semanas | qualidade incerta | +1 frame + inferência | compete com o NR pela GPU |
| DLSS Quality/Balanced/Performance nesta cadeia | **Inviável** | — | — | — | Feeder é DLAA por construção |
| **DLSS-G (`nvngx_dlssg.dll`)** | **Inviável** | — | — | — | D3D11 não suportado; sem HUD-less, sem UI alpha, sem matrizes de câmera |

---

## 7. Ordem recomendada de ataque

### Esta semana, sem escrever código

1. **Atualizar `dlss5-feed.addon64` de 0.7.0 → 0.12.0.**
   Destrava o fix de thread-safety (pré-requisito **obrigatório** do Smooth Motion) *e* o FSR 1 expand-back.
   É o passo que habilita os dois itens seguintes. Guardar o 0.7.0 como backup.
2. **Testar Smooth Motion** no perfil do `DLSS5ForAll.exe` (NVIDIA App ou Profile Inspector).
   Frame generation de verdade, zero código. Se der artefato, desligar seletivamente por API no bitfield do
   Profile Inspector; usar Debug Bars para ver se o artefato está nos frames gerados.
3. **Lossless Scaling** como plano B do FG — **em modo janela separada**, fonte janela, apontando o LS para a
   **nossa** janela. Nunca em overlay, nunca com fonte monitor (`hideSelf` mata a captura do LS).
4. **`work_resolution` 67–75%** no painel do Feeder, para recuperar o fps que o NR consome — agora com FSR 1
   em vez de bilinear.

### Depois, código pequeno

5. **Desacoplar o backbuffer da captura** (`WM_SIZE` → `R.Resize`, §2.4). Habilita "jogo em 720p, saída 1440p
   com FSR 1" e mata o esticão bilinear do DXGI. Medir MV e fps antes/depois.
6. **Expor `--vsync 0`** (§5). Ajuda tanto o Smooth Motion quanto o LS.

### Projeto grande, só se quiserem FG próprio

7. **Add-on ReShade nosso com NvOFFRUC** (§3.3). Dias de trabalho; o risco está no pacing manual, não na API.

### Não atacar

DLSS-G, FSR 3.1 frame interpolation, RIFE/DirectML.

---

## 8. Risco legal

Nada aqui muda o que já existe. O `nvngx_dlssnr.dll` continua sendo build interno vazado — o `ReShade.log`
confirma no próprio texto do add-on: `custom runtime accepted; untested build, NR failures may be specific to it`.

- **Smooth Motion** e **Lossless Scaling** são caminhos **limpos**: driver oficial da NVIDIA e software comprado
  na Steam. Nenhum dos dois piora a exposição existente, e nenhum exige redistribuir nada.
- **FSR 3** é **MIT** — sem problema.
- **NvOFFRUC** é o único item novo com pendência: usar é livre, **redistribuir a DLL exige verificar a EULA do
  Optical Flow SDK** antes de embarcar no pacote.
- A recomendação do README continua valendo: deixar claro que é experimento com build vazado e **não
  redistribuir o `nvngx_dlssnr.dll`**.

---

## Fontes

- [DLSS5-Feeder — repositório, README e releases](https://github.com/jlrouzies-fr/DLSS5-Feeder)
- [Streamline — ProgrammingGuideDLSS_G.md](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md)
- [DLSS Frame Generation Plugin (sl.dlss_g)](https://deepwiki.com/NVIDIA-RTX/Streamline/4.4-dlss-frame-generation-plugin-(sl.dlss_g))
- [NVIDIA FRUC Programming Guide (Optical Flow SDK 5.0)](https://docs.nvidia.com/video-technologies/optical-flow-sdk/nvfruc-programming-guide/index.html)
- [NVIDIA Optical Flow Engine-Assisted Frame Rate Up Conversion (PDF)](https://docs.nvidia.com/video-technologies/optical-flow-sdk/pdf/NVOFA_FRUC.pdf)
- [FidelityFX Frame Interpolation Swapchain 1.1.3 — GPUOpen](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation-swap-chain/)
- [FidelityFX Super Resolution 3.1.4 — upscaling e frame generation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/)
- [optiscaler/FidelityFX-SDK-DX11](https://github.com/optiscaler/FidelityFX-SDK-DX11)
- [NVIDIA — Smooth Motion para a linha RTX 40](https://www.nvidia.com/en-us/geforce/news/gfecnt/20258/nvidia-app-global-dlss-overrides-rtx-40-series-smooth-motion/)
- [NVIDIA — Enabling Smooth Motion in NVIDIA App](https://nvidia.custhelp.com/app/answers/detail/a_id/5621/~/enabling-smooth-motion-in-nvidia-app)
- [Lossless Scaling — FrameGen Detailed Guide](https://steamcommunity.com/app/993090/discussions/0/4418677017727367960/)
