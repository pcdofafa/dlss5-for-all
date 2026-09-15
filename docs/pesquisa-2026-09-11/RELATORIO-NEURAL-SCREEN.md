# Relatório: o concorrente "NeuralScreen" (DLSS5-NeuralScreen 1.5.6)

Data: 2026-09-11. Estudo **estático**: nada foi executado — nem o `.exe`, nem o launcher, nem o
worker, nem a DLL da NVIDIA. Fontes: a pasta baixada em `C:\Users\PCDOFAFA\Downloads\neural screen\`
(código Python e binários), o repositório público clonado só com o código-fonte (sem blobs
binários) para o scratchpad, hashes/assinaturas/strings dos binários, e a página de issues.
Os números de desempenho *deles* vêm do `TECHNICAL.md` deles (medidos numa RTX 5070 Ti, 4K); os
*nossos* vêm dos nossos docs (RTX 4080, 1920×1080 / 3440×1440). **Não são comparáveis diretamente** —
ver §5 para o que só a medição lado a lado responde.

## Tabela-resumo

| Aspecto | NeuralScreen 1.5.6 | DLSS5 For All | Quem leva |
|---|---|---|---|
| Como chama a rede | `nvngx_dlssnr.dll` **direto** (Init_Ext/Create/Evaluate por `GetProcAddress`), worker C++ próprio, sem ReShade/RenoDX/Feeder | ReShade em processo + add-ons (RenoDX ShortFuse hoje; NR próprio no feed já funciona) | NeuralScreen (menos peças, menos armadilhas de config) |
| Passes | **1 só**; pedido de multi-pass fechado como "not planned" (issue #23) | 1 a 10 (`PassCount`) | Nós |
| Captura | Desktop Duplication (tela) ou WGC (uma janela), dentro do worker, tudo na GPU | WGC (janela ou monitor) | Empate técnico; a deles ignora o monitor escolhido no modo padrão (§2) |
| Motion vectors | DIS optical flow (OpenCV, **CPU**, 320×180), piso de ruído 0,5 px de fluxo ≈ 6 px a 4K | Lumenite (shader, −0,7 % a 45 px) ou VME por hardware (0,95 ms na fila de vídeo) | Nós |
| Profundidade / jitter | **Nenhuma** (não passa `DLSSNR.Depth`; sem jitter) | Depth plano (estrutural), sem jitter | Empate (limite da captura, nos dois) |
| Exposição | **Adaptativa** (`DLSS.Exposure.Scale` 1,00–1,10 pela luminância média, EMA 0,5 s) | Fixa | NeuralScreen |
| Custo da rede | Modo `nr_small`: rede em resolução menor + **composição residual** sobre o quadro nativo (4K: 42,9 → 65,3 fps) | `work_resolution` reduz a cadeia toda e faz blit bilinear; NR ~4,2 ms/passe de GPU | NeuralScreen (ideia a copiar, §3.1) |
| Apresentação | Janela própria D3D12 flip-discard, `Present(0,0)`, **pipeline serializado** (CPU espera fence em cada estágio, 1 quadro em voo, Python no laço) | Janela D3D11 sobreposta, código responde por 1,5 ms de ~16,8 ms; PRESENT→TELA ≈ 1 refresh | Nós (latência; eles mesmos declaram 40–60 ms) |
| Pulo de quadro / síntese / teto de fps | Nenhum | Pulo por razão, quadro sintetizado por fluxo (VME), teto manual | Nós |
| Seleção de placa | Primeiro adaptador NVIDIA, sem escolha | Seletor (rede numa placa, jogo em outra) | Nós |
| AMD / Intel | **Não** (worker exige VendorId 0x10DE) | Módulo AMD (mod DLSS-NR-on-AMD) | Nós |
| RTX 30/40 | Hook em memória de `NvAPI_GPU_GetArchInfo` (responde Blackwell 0x1B0) | Funciona na RTX 4080 do usuário pela cadeia atual | Empate prático |
| HDR | Não suportado (README pede SDR) | Controle de HDR (`hdr_control.h`) | Nós |
| Pós-nitidez | Nenhuma | `DLSS5_Sharpen.fx` (item de maior retorno da nossa matriz) | Nós |
| Painel / usabilidade | Menu dentro do overlay, 12 idiomas, presets do usuário, wipe antes/depois, gravação AV1+áudio, screenshots, Spout2, tray+taskbar, autostart | Painel End, pt/en, seletor inicial, launcher com atualização e vitrine | NeuralScreen (polimento) |
| Instalação | Zip, `NeuralScreen.exe` (sem instalador, sem atualização automática) | Portable + launcher com atualização | Nós (atualização) |
| Estabilidade | Worker em processo separado (crash isolado), SAFE PASSTHROUGH, DRED, SEH, 3 restarts + auto-revive 30 s | Cadeia no nosso processo; `FeedFail` 3×, proteção de TDR na troca de resolução | NeuralScreen (isolamento e diagnóstico) |
| Tamanho | ≈ 410 MB descompactado (165 MB DLL NVIDIA, 112 MB OpenCV, 63 MB FFmpeg, Python 3.13) | Bem menor (C++ nativo) | Nós |
| Licença | MIT (código); DLL NVIDIA vazada **redistribuída** no zip | MIT; DLL NVIDIA **não** distribuída | — (só informo) |

---

## 1. Identidade

| Item | Evidência |
|---|---|
| Nome / versão | `NeuralScreen 1.5.6`, commit `b63cb85a705b2d3340642157a0b25a6f26a8b073`, built `2026-09-11 06:28:27 UTC` (`VERSION.txt:1-6`) |
| Autor | `perseval-BLR` (GitHub); canal `@perseval_BLR` no YouTube (`main.py:57`, `main.py:197`). Commits assinados por `perseval-BLR`, alguns com co-autoria "claude" |
| Repositório | https://github.com/perseval-BLR/DLSS5-NeuralScreen — 297 estrelas, 18 forks, **200 commits entre 2026-09-06 18:49 e 2026-09-11 09:28 (+03:00)**; ou seja, cinco dias de vida |
| Licença | **MIT**, © 2026 perseval-BLR (`LICENSE:1-21`). Nota no próprio LICENSE: o `nvngx_dlssnr.dll` é da NVIDIA, "leaked pre-release build (310.8.0) redistributed as-is for research purposes" (`LICENSE:25-27`) |
| Linguagem / stack | Python 3.13.15 embutido (`runtime\python.exe`, assinado pela PSF) + `pygame 2.6.1` (HUD/menu) + `dxcam 0.3.0` (captura fallback) + `mss 10.2` (GDI fallback) + `opencv-python-headless 5.0.0.93` (fluxo óptico DIS) + `numpy 2.5.2` + `PyAV 18.1` (gravação NVENC) + `pystray`/`comtypes`. Worker nativo em C++ (`native\nvngx.dll`, 156 KB, **um EXE renomeado**, sem assinatura e sem recurso de versão) |
| Origem do worker | `native/dlss5-feed-host64.cpp` (4885 linhas). Linha 1: `// dlss5-feed-host64 - the 64-bit half of DLSS5-Feeder for 32-bit games.` — é um **fork do host64 do DLSS5-Feeder** (jlrouzies-fr, MIT), crescido até virar o worker de desktop. Restam funções mortas do feeder (`DetectRenodxAddon` em `:67`, `ReShadeHasFeature18` em `:3909`, `Serve()` por named pipe em `:4626`) que o modo `--live` nunca chama |
| Tamanho | ≈ 410 MB descompactado: `native\` 158,8 MB (dos quais `nvngx_dlssnr.dll` 165 840 496 bytes), `cv2` 111,8 MB, `av.libs` (FFmpeg) 62,6 MB, `runtime\` 20 MB + `numpy.libs` 20 MB + PIL 14 MB + pygame 9,7 MB |
| Dependências que **não** usa | ReShade, RenoDX, DLSS5-Feeder (o add-on), Streamline, OptiScaler, DLSS SDK "oficial" — nenhuma string nem import. Usa os headers `nvsdk_ngx.h` e a lib `nvsdk_ngx_d.lib` do SDK NGX para falar com o **core do driver** (`_nvngx.dll`) só para `Init`/`AllocateParameters` (`cpp:904-916`) |
| `nvngx_dlssnr.dll` | `310,8,0,0`, "NVIDIA DLSSNR - DVS PRODUCTION", `CL 38718415`; sha256 `dcc0dc24…d36f`; assinatura Authenticode **HashMismatch** (arquivo alterado depois de assinado); kernels CUDA por leitura de fatbin: `sm_75×15, sm_86×15, sm_89×15, sm_120×24` |
| O nosso, para comparar | `DLSS5ForAll-AMD\bin\nvngx_dlssnr.dll`: mesmo tamanho e mesma família (`310,8,0,0`, `CL 38718415`), sha256 `e67dee20…89a`, HashMismatch, `sm_75×15, sm_86×15, sm_89×15, sm_120×23`. `amd\nvngx_dlssnr.dll`: `310.8.SF.0`, 165 830 144 bytes, **sem assinatura**, mesmos kernels (×23). São três builds diferentes da mesma runtime 310.8.0; todas carregam kernels para RTX 20–50 |

Observação sobre `DLSSNR.Upscaling`, `DLSSNR.Scale`, `DLSSNR.ScalingRatio` que o worker seta na criação
(`cpp:953-955`): **nenhuma das três strings existe dentro do `nvngx_dlssnr.dll`** (busca ASCII e UTF-16
nas três cópias). É coerente com a medição deles de que "no modo upscale o `work_scale` não custa nada"
(`TECHNICAL.md:226-259`): a rede ignora esses parâmetros e processa o quadro inteiro. O ganho real deles
vem só do `nr_small` (§3.1), que faz a redução fora da rede.

## 2. Arquitetura

### 2.1 Dois processos, um protocolo binário

```
 Python (main.py, pythonw)                      Worker C++ (native\nvngx.dll --live)
 ─────────────────────────                      ────────────────────────────────────
 config.json, menu pygame (camada HUD)          D3D12 device no 1º adaptador NVIDIA
 hotkeys (RegisterHotKey), tray, taskbar        NGX: _nvngx.dll (core) + nvngx_dlssnr.dll (direto)
 fluxo óptico DIS 320×180 (OpenCV, CPU)  ◄──── GRAY: luminância AREA 320×180 numa seção nomeada
 motion float16 320×180 ───────────────► MOTS: upscale bilinear em compute → DLSSNR.MVec
 FRM1 (24 bytes) por quadro ───────────► captura DDA/WGC → swizzle → nr_in → Evaluate → output
                                                 → janela própria (WNDO, flip-discard) → Present(0,0)
 OUT1 (bytes=0) ◄──────────────────────  ou pixels de volta (OUTS seção + seqlock) p/ gravação
```

Mensagens: `D5V3`, `SHMI/SACK`, `WNDO/WACK`, `MOTS/MACK`, `DDA1/DACK`, `WGCW/WGAK`, `GRAY/GAK`,
`OUTS/OAK2`, `RNSZ/RACK`, `FRM1/OUT1` (`main.py:201-289`, `cpp:1090-1332`, com `static_assert` dos
tamanhos em `cpp:1308-1332`). O laço Python é **estritamente pareado** send→recv, um slot de memória
compartilhada, sem pipelining (`main.py:372-375`, `cpp:1191-1198`).

### 2.2 Por que o worker se chama `nvngx.dll`

O `nvngx_dlssnr.dll` exige que o módulo chamador se chame `nvngx.dll` (o mesmo cheque que o nosso
ARQUITETURA §10 descreve e que o RenoDX/o nosso feed satisfazem por spoof de `GetModuleFileNameW`).
Eles resolveram **sem hook nenhum**: o executável do worker é gravado em disco como `native\nvngx.dll`
e o Python o lança com `subprocess.Popen([".../nvngx.dll", "--live"])` (`main.py:313-316`,
`main.py:770-777`; explicação em `cpp:414-422`). Consequência: a rede roda obrigatoriamente **fora**
do processo da interface — o que dá isolamento de crash de graça.

### 2.3 NGX

- `NVSDK_NGX_D3D12_Init(0x1000000, dir_do_exe, dev, …)`, fallback `Init_with_ProjectID("a0f57b54-…")`
  (`cpp:904-911`) — o ProjectID é o do DLSS5-Feeder.
- `AllocateParameters` no core; depois `Init_Ext` **do snippet** com o mesmo AppId (`cpp:472`).
- `CreateFeature(list, NVSDK_NGX_Feature_Reserved18, params, &handle)` do snippet (`cpp:962`), com
  `DLSSNR.Width/Height/InputWidth/InputHeight/OutputWidth/OutputHeight/Output.Width/Output.Height`,
  `DLSSNR.Upscaling/Scale/ScalingRatio` (inertes, ver §1), `DLSSNR.Hint.Render.Preset` (env
  `NS_NR_PRESET`, padrão 0), `DLSS.Feature.Create.Flags = 0` (`cpp:943-957`). Os flags
  `MVLowRes|AutoExposure|DepthInverted` são calculados e **descartados** (`(void)flags`, `cpp:939`).
- Por quadro (`cpp:3703-3725`): `DLSSNR.Color`, `DLSSNR.Output`, `DLSSNR.MVec`, subrects, `MVecScale 1,1`,
  `Enabled 1`, `Reset`, `Intensity`, `LocalToneStrength`, `LocalStructureStrength`,
  `SkinStructureStrength`, `UseAutoMask`, `Style`, `UICorrection`, `DLSS.Pre.Exposure 1.0`,
  `DLSS.Exposure.Scale = g_pw_exposure`. **Não há `DLSSNR.Depth`, não há jitter, não há máscara.**
- Uma única chamada `EvaluateFeature` por quadro; CPU espera a fence (até 60 s) antes de seguir
  (`cpp:3758`). Warm-up de 120 avaliações descartadas no arranque (`config.json:6`, `cpp:4497-4503`),
  4 em placas pré-Blackwell (`main.py:1666-1671`).
- Perfis (`main.py:292-301`): Faithful `0,70/0,75/0,75/pele −1, style 0`; Natural (padrão)
  `1,00/1,00/1,00/−1, style 1`; Strong `1,65/1,40/1,50/1,0, style 2, automask 1, preset 2`; Extreme
  `2,50/2,00/2,00/1,5`. Os valores vão sem conversão para o mesmo campo NGX que o nosso `RsGravarNr()`
  escreve.

### 2.4 Captura

- **Tela inteira**: Desktop Duplication no worker — `EnumAdapters1(0)` → `EnumOutputs(0)` →
  `DuplicateOutput` (`cpp:3113-3120`). O comando `DDA1` leva só largura/altura/flags (`cpp:1244-1248`):
  **não há índice de monitor**. O `monitor` do `config.json` (identidade por `\\.\DISPLAY1`,
  `capture.py:7-12`) só governa a captura `dxcam` do lado Python, que é o *fallback* quando
  `capture_in_worker` falha. Pela leitura, com o padrão `capture_in_worker=true` o worker captura
  sempre a saída 0 do adaptador 0. O README admite: "A second monitor works but was not tested".
- **Uma janela**: WGC com `CreateForWindow`, `Direct3D11CaptureFramePool::CreateFreeThreaded`
  (B8G8R8A8, 2 buffers), `IsCursorCaptureEnabled(false)`, `IsBorderRequired(false)`,
  `TryGetNextFrame` por polling (`cpp:3434-3539`). É o mesmo mecanismo do nosso `screen_capture.h`.
- Ponte D3D11→D3D12 por textura NT compartilhada + fence compartilhada, espera por evento
  (`cpp:3142-3247`); swizzle BGRA→RGBA em compute (`cpp:2623-2636`); `AcquireNextFrame(100 ms)` —
  se o desktop não muda, o quadro anterior é reapresentado.
- Laptop híbrido (Optimus): o D3D11 da captura também nasce em `EnumAdapters1(0)` (`cpp:3089`); se for
  a iGPU, a DDA falha com `DXGI_ERROR_UNSUPPORTED` e o Python cai para **GDI (`mss`)**, "slower"
  (`capture.py:157-167`, release v1.5.6). Nós capturamos pelo compositor (WGC) nos dois casos.
- Redimensionar a janela capturada: espera 0,5 s de tamanho estável e **reconstrói o pipeline
  inteiro** (worker, seção, texturas) — `main.py:2315-2373`.

### 2.5 Motion vectors, profundidade, exposição

- Fluxo óptico `cv2.DISOpticalFlow` preset FAST, `finestScale 1`, propagação espacial, sobre a
  luminância AREA 320×180 vinda do worker (`guides.py:45-47`, `cpp:2704-2724`). Cena estática
  (`scene_score < 0,001`) → vetores zero; corte de cena (`> 0,24`) → `reset=1` (`guides.py:100-106`).
  Piso de ruído: vetores com magnitude `< 0,5 px` **na grade de fluxo** são zerados — a 4K isso é
  `≈ 6 px` de tela, a 1440p `≈ 4 px` (`guides.py:49-55, 120-121`). Movimento lento cai no zero.
- Profundidade: nenhuma. Jitter: nenhum. (Nós: depth plano e jitter zero — limitação estrutural igual.)
- Exposição adaptativa "PaperWhite" (`cpp:2974-3075`): média da luminância AREA →
  `smoothstep(NS_PW_DARK=0,10, NS_PW_LIT=0,40)` → exposição entre `NS_PW_MAX=1,10` (escuro) e
  `NS_PW_MIN=1,00` (claro), EMA com `tau 0,5 s`, entregue em `DLSS.Exposure.Scale`. Crédito no
  comentário: "Ghady983/RenoDX-DLSS-5-Artifact-Fix". Custo: um somatório de 57 600 bytes por quadro.

### 2.6 Apresentação, foco, DPI, HDR

- Janela do worker: `WS_POPUP` com `WS_EX_TOPMOST|NOACTIVATE|TOOLWINDOW|TRANSPARENT`; depois de criar a
  swapchain adiciona `WS_EX_LAYERED` (a flip-model recusa janela layered na criação) e
  `SetLayeredWindowAttributes(alpha 255)` — receita de click-through (`cpp:1484-1597`). Swapchain
  `FLIP_DISCARD`, 2 buffers, `R8G8B8A8_UNORM`, criada na **mesma fila** do NGX; `CopyResource` do output
  para o backbuffer, fence esperada na CPU, `Present(0,0)` (`cpp:1720-1756`). Sem waitable object, sem
  `SetMaximumFrameLatency`, sem `ALLOW_TEARING`, sem V-Sync.
- Escondida até o primeiro Present bem-sucedido (`cpp:1711-1718`) — sem flash preto no arranque.
- `WDA_EXCLUDEFROMCAPTURE` ligado na captura de tela (evita realimentação), **desligado** na captura de
  janela (`cpp:1438-1448`) — é o que deixa OBS/NVIDIA App verem o resultado.
- Topmost reafirmado a cada 300 quadros no worker, só se a janela do topo não for deles
  (`cpp:1687-1702`); a camada HUD Python reafirma a cada 30 quadros (`main.py:3294-3308`).
- Menu: outra janela (pygame) com chroma key `LWA_COLORKEY` magenta (`display.py:112-118`); ao abrir,
  tira `WS_EX_TRANSPARENT|NOACTIVATE` e faz **`AttachThreadInput` com a thread do foreground (o jogo)
  + `SetForegroundWindow`** (`display.py:504-525`). É exatamente a armadilha do nosso ARQUITETURA §7
  ("`AttachThreadInput` congela o jogo"); eles anexam por um instante e desanexam, o que reduz mas não
  elimina o risco.
- Hotkeys por `RegisterHotKey` no numpad (Num1/2/3/0/4/5/6, Ctrl+Alt+Q): o jogo **não recebe** essas
  teclas enquanto o programa roda; exige Num Lock (`hotkeys.py:1-21, 52-57`).
- DPI: `PER_MONITOR_AWARE_V2` nos dois processos (`main.py:159-174`, `cpp:4838-4849`); posição pelo
  `DWMWA_EXTENDED_FRAME_BOUNDS` (`main.py:951-971`, `cpp:1659`); tamanho físico confirmado pelo `WGAK`.
- HDR: **não suportado** (`README.md:192`); tudo `R8G8B8A8_UNORM`; só loga `HDREnabled` do registro.
- Tela cheia exclusiva: overlay não aparece (`README.md:189`) — mesmo limite nosso.
- Teto de trabalho **2560×1440** ("NGX feature 18 goes silent at 3840×2160", `main.py:327-331`).
  Não reproduzimos isso: a nossa cadeia cria features de 2648×1108 (RELATORIO-BOTOES) e roda fontes
  3440×1440; pode ser específico do caminho direto deles.

### 2.7 Estabilidade

- Worker em processo separado: se o NGX explode, morre o worker; o Python reinicia até 3× seguidas,
  depois desliga o NR, esconde o overlay e tenta **um** revive automático 30 s depois — salvo
  `0xBAD00001` (falha "dura", placa incapaz), que nunca revive (`main.py:1935-1942, 1446-1455,
  3411-3457`).
- **SAFE PASSTHROUGH**: se `CreateFeature` falha, o worker fica vivo e apresenta a captura crua em vez
  de derrubar tudo (`cpp:4225-4234`, `cpp:4506`).
- `AbortCommands()` descarta a command list em que o NGX lançou exceção (`cpp:571-580`); SEH em
  create/evaluate/release (`cpp:582-601`); ring de 3 allocators; `GetCompletedValue()==UINT64_MAX`
  tratado como device removido (`cpp:546-564`).
- **DRED** ligado (`SetAutoBreadcrumbsEnablement`, `SetPageFaultEnablement`) e lido no
  `GetDeviceRemovedReason` (`cpp:838-875`, `cpp:505-527`) — o log diz *onde* a GPU morreu.
- `RNSZ`: troca de resolução/perfil recria feature e texturas **dentro** do worker (~60 ms), sem
  reiniciar processo (`cpp:4273-4329`); restart de processo espera 2 s por causa dos 165 MB do NGX
  (`main.py:1501-1512`).
- Instância única por mutex `NeuralScreen_SingleInstance` (`main.py:1578-1581`).
- Issues abertas nos 5 dias: #24 "Crashing and the only fix is to restart windows" (sem resposta),
  #21 "Doesn't seem to work in borderless", #25 "Trojan.Generic" (falso positivo do AV), #27 "tudo claro
  demais, sliders sem efeito" (fechada no dia da exposição adaptativa), #17 "RTX 4070 Ti SUPER not
  working" e #16 "5070/2070" (corrigidas no hotfix 1.5.1 do spoof).

---

## 3. O que ele tem que nós não temos, ou faz melhor

### 3.1 Rede em resolução menor + composição residual ("matched residual composite") — **o item que vale**

Mecanismo (`cpp:2083-2110`, `cpp:3675-3751`): com `nr_small`, a cor nativa é reduzida por compute
bilinear para `nr_in` (resolução de trabalho), a rede roda **ali** (`nr_in → nr_out`), e o resultado
final é `output = native + (nr_out↑ − nr_in↑) × strength` em resolução nativa, com `nr_in`/`nr_out`
amostrados bilinearmente. O quadro nativo é a âncora (texto, bordas, HUD ficam 1:1); a rede só
fornece o **delta** (o "relighting"). Aritmética em espaço de display (UNORM), não linear — decisão
deliberada, documentada.

Números deles (5070 Ti, 4K, `TECHNICAL.md:277-334`): `eval = 1,50 ms + 1,51 ms/MPix`; 4K inteiro
16,05 ms → trabalho 2560×1440 7,25 ms; **42,9 → 65,3 fps**; ponta a ponta 47,9 → 71,9 fps com
`work_scale 0,65`. Nitidez num padrão sintético: laplaciano **983** com o residual contra **88** com
upscale bilinear simples (entrada 951).

Por que importa para nós: o nosso `work_resolution` reduz a **cadeia inteira** e sobe por blit
bilinear (DLAA menor + blit; medimos 15,58 de nitidez contra 14,46 com SR — `borrao-e-nitidez.md`),
ou seja, pagamos em nitidez. O residual paga em nitidez quase nada e corta o custo do NR por passe
proporcionalmente aos pixels. Com o NR próprio (`Reserved18` no feed, ARQUITETURA §10) nós controlamos
as texturas — dá para aplicar só à etapa neural, mantendo DLAA/Sharpen em resolução cheia. A 3440×1440
(4,95 MPix) o passe custa ~4,2 ms; a `0,65` (2,1 MPix) o modelo deles prevê ~1,5+3,2 ≈ **~2 ms/passe**,
o que torna **8–10 passes a 60 fps** plausíveis (hoje 10 passes = 20 fps).

- Esforço: **1–2 dias** (dois compute shaders — o deles tem 40 linhas de HLSL —, duas texturas
  `nr_in/nr_out`, um chip no painel, e a medição de nitidez com o mesmo alvo sintético da nossa matriz).
- Ganho: **alto** (custo por passe), a confirmar: (a) se o delta em resolução baixa preserva o
  ganho de "estrutura local" que a rede dá; (b) interação com o `DLSS5_Sharpen`; (c) se a variação
  entre passes acumula ruído no residual.
- Licença: MIT deles, reimplementação trivial de qualquer modo. Crédito: "DLSSNR-Cost-Scaler principle"
  (eles citam esse projeto como origem da ideia).

### 3.2 Exposição adaptativa para a rede

Mecanismo em §2.5. Esforço: **horas** — já temos luminância na cadeia (o `Lumenite_Kernel` calcula
pirâmide de cinza; ou um mip da cor) e o feed já escreve `DLSS.Exposure.Scale`/`Pre.Exposure` no
evaluate. Ganho: **médio e incerto** — a hipótese deles é que a rede produz artefatos em cenas
escuras por ver quadro quase preto; os nossos docs registram outro problema no escuro (MV colapsam por
falta de contraste), que isso não resolve. A issue #27 deles ("tudo claro demais") mostra que o teto
`1,10` foi escolhido para não estourar. Testar com o `brilho-witcher3.md` na mão.

### 3.3 SAFE PASSTHROUGH + DRED + classificação de falha dura

- Quando o `CreateFeature(Reserved18)` falha, manter a cadeia viva apresentando a imagem sem NR, em
  vez de `FeedFail` × 3 e cadeia morta. Esforço: **horas**. Ganho: médio (placas/drivers em que o NR
  não cria; hoje a pessoa fica sem imagem nenhuma?). Verificar o comportamento atual antes.
- DRED breadcrumbs no `g.dev12` do feed (`SetAutoBreadcrumbsEnablement` antes de qualquer submit) e
  leitura no caminho `the graphics device is gone`. Esforço: **1–2 h**. Ganho: só diagnóstico, mas os
  TDRs da troca de resolução (ARQUITETURA §11) são exatamente o caso em que "onde morreu" vale ouro.
- `0xBAD00001` = nunca tentar de novo; o resto = um revive automático com backoff. Esforço: 1 h.

### 3.4 Wipe antes/depois

`SplitCompose` (`cpp:3808-3860`): um `CopyTextureRegion` da faixa esquerda da captura crua sobre o
output + um `ClearUnorderedAccessViewFloat` com `rect` para o divisor; a posição viaja nos 16 bits
altos das flags do quadro, sem recriar nada. Cai na gravação e no screenshot de graça. Esforço:
**2–4 h** no nosso shader de composição (um `if (uv.x < split)`). Ganho: médio para o canal (mostrar o
efeito) e para o usuário calibrar intensidade; baixo para a imagem em si.

### 3.5 Isolamento de crash por processo

O NR deles roda noutro processo; um crash do NGX não derruba a interface. O nosso ReShade+NGX vivem
no `DLSS5ForAll.exe`. Copiar isso é reprojeto (**semanas**; texturas por handle NT, fence
compartilhada — o que o feeder host64 já faz e que é a origem do worker deles). Ganho real só se
tivermos crashes de NGX frequentes; hoje o crash que investigamos (Witcher 3) não era nosso. **Não
recomendo** agora; registro para o futuro, porque o `Serve()`/`feed_ipc.h` (MIT) do feeder é um ponto
de partida pronto.

### 3.6 Polimento de produto

- Presets do usuário (snapshot dos sliders + `profile/preset/style/auto_mask/ui_correction`,
  validados ao carregar, `main.py:523-591`). Esforço: ~1 dia. Ganho: baixo-médio.
- 12 idiomas (`i18n.py`, 55 KB de strings). Esforço: depende do painel; ganho baixo para o público do
  canal (pt/en já cobrem).
- Gravação interna AV1/HEVC/H.264 NVENC + WASAPI loopback com limitador (`recorder.py`, `audio.py`),
  30 fps, fila de 4 quadros, PTS do relógio. Esforço para nós em C++ (Media Foundation): **2–4 dias**.
  Ganho médio: o overlay deles é excluído da captura externa no modo tela, daí a necessidade; nós já
  temos o RELATORIO-OBS.
- Saída Spout2 para OBS (`NS_SPOUT=1`, `spout_bridge.cpp`). Esforço 1 dia; ganho baixo.
- Autostart (HKCU Run), botão na barra de tarefas, ícone na bandeja, alertas de falha silenciosa
  (screenshot/gravação/config), escrita atômica do `config.json` (`main.py:643-664`). Esforço:
  horas cada; ganho baixo-médio.

### 3.7 O que eles fazem melhor em engenharia miúda (vale ler, não copiar)

- Protocolo com `static_assert` dos tamanhos dos dois lados (`cpp:1303-1332`, `tests/test_protocol_sizes.py`).
- Cinza AREA em compute com bloco `ceil(src/dst)` igual ao `INTER_AREA` (`cpp:2697-2724`) — "bilinear
  aliasa texto e quebra o fluxo (verificado)".
- Upscale de MV com pesos float32 à mão, porque o sampler quantiza a 1/256 e "deu até 21 níveis de
  diferença" (`cpp:2024-2029`).
- Espera de fence por evento, não `Sleep(1)` (13 ms → 1–2 ms, `cpp:3222-3226`).
- 53 testes automatizados (`tests/`), `build_release_zip.py` recusa empacotar runtime sem os kernels
  esperados (`:196-256`), `VERSION.txt` com commit + sha256 da runtime.

## 4. O que nós temos que ele não tem

1. **Passes 1–10** (eles: 1, e "not planned").
2. **Estimadores de MV na GPU**: Lumenite (−0,7 % a 45 px) e VME por hardware (0,95 ms na fila de
   vídeo, ±80 px com divisor 2), com validação por patch e confiança. Eles: DIS na CPU a 320 px de
   largura, vetores < ~6 px (4K) zerados.
3. **Pulo de quadro por razão, quadro sintetizado por fluxo (VME warp, +13 a +19 dB PSNR contra
   repetir), teto de fps manual** — eles processam toda mudança do desktop, um quadro em voo, sem cadência.
4. **Seletor de placa** (rede numa GPU, jogo em outra) — eles pegam o primeiro adaptador NVIDIA.
5. **Módulo AMD** — eles: NVIDIA apenas, por construção (`cpp:827-831`).
6. **HDR** — eles: SDR apenas.
7. **Pós-filtro de nitidez** (`DLSS5_Sharpen.fx`, o item de maior retorno da nossa matriz) e o
   DLAA/`DLSS5_Feed` antes do NR — eles entregam a rede crua.
8. **Escolha de monitor real** no caminho padrão (§2.4) e captura de monitor pelo compositor (funciona
   em Optimus sem cair para GDI).
9. **Launcher com atualização**, vitrine, instalador — eles: zip e nada de auto-update.
10. Latência: pipeline nosso mede 1,5 ms de código em ~16,8 ms e ~1 refresh de PRESENT→TELA; o deles
    tem espera de fence na CPU em captura, upload, evaluate e present, mais a volta ao Python por quadro
    — eles mesmos publicam **40–60 ms** (`README.md:193`).
11. Tamanho e superfície: binário nativo pequeno contra 410 MB com Python/OpenCV/FFmpeg.

## 5. Funciona melhor? Opinião fundamentada só na leitura

**Onde ele tende a ser melhor**

- Simplicidade da cadeia: um consumidor NGX, sem ReShade nem RenoDX, logo sem `ReShade.ini`
  regravado por cima, sem `HookPoint` lido uma vez só, sem dois add-ons disputando o NGX. Muitas das
  nossas noites de 07–08/09 não existem lá.
- Custo de GPU **quando `nr_small` está ligado** (desligado de fábrica): metade do tempo de rede a 4K.
- Robustez a falha de criação do feature (passthrough) e diagnóstico de TDR (DRED).
- Experiência de primeiro uso: desempacotar, abrir, Num2, tudo no overlay.

**Onde ele tende a ser pior**

- **Qualidade temporal/movimento**: MV grosseiros (320 px, CPU, piso de 6 px a 4K), um passe só,
  sem nitidez compensatória. Nos nossos números o NR tira 23 % do laplaciano e o Sharpen devolve — eles
  não têm o segundo termo.
- **Latência e fluidez**: serialização total, Python no laço, um quadro em voo, `AcquireNextFrame`
  bloqueante de até 100 ms; sem pulo de quadro ou síntese para segurar fps.
- **Alcance de hardware**: só NVIDIA, sem multi-GPU, sem HDR, teto 2560×1440 de trabalho.
- **Monitor secundário e laptops híbridos**: adaptador 0/saída 0 fixos; GDI como rede de segurança.
- **Maturidade**: cinco dias de vida, 200 commits, issues de crash sem resposta.

**Veredito de leitura**: para *desktop/vídeo/foto* numa RTX 50 com um monitor, o NeuralScreen é
mais fácil e mais leve de GPU (com `nr_small`). Para *jogo* — que é o nosso caso — a nossa cadeia
tende a ganhar em movimento, latência, fps e alcance; o que eles têm de realmente bom (residual
composite, exposição adaptativa, passthrough/DRED) cabe no nosso NR próprio em poucos dias.

**O que só a medição lado a lado responde** (protocolo para depois, não executado agora):

1. *Latência ponta a ponta*: mesma máquina (RTX 4080), mesma fonte (janela de teste a 60 fps com
   contador de quadros), fotodiodo/câmera de alta velocidade ou o nosso medidor de PRESENT→TELA; 5
   minutos por modo; comparar mediana e p99. Prever: nosso ≈ 17–25 ms, deles 40–60 ms.
2. *Custo de GPU*: `NS_PHASE=1` neles (dá `eval on GPU` por timestamp) contra o nosso `[feed]`;
   mesma resolução de trabalho (1920×1080 e 2560×1440), NR 1 passe, ambos com e sem modo reduzido.
3. *Nitidez*: o mesmo alvo sintético da nossa matriz (grade 1 px, zone plate, texto) capturado dos dois;
   energia de gradiente/laplaciano e RMSE contra o alvo; e uma cena real parada (jogo pausado).
4. *Movimento*: pan sintético a 2/8/20/45 px por quadro; medir arrasto e fantasma (diferença contra
   verdade-terreno deslocada), como no `warp_test.cpp`.
5. *Estabilidade*: 30 min com alt-tab, troca de resolução do jogo, minimizar/restaurar, troca de
   monitor; contar reinícios de worker (log deles) contra `FeedFail`/TDR (nosso).
6. *Escuro*: cena noturna do Witcher 3 com e sem exposição adaptativa (neles) contra o nosso, medindo
   artefatos por variância temporal em região estática.

## 6. Licença e reuso

- NeuralScreen: **MIT** (© 2026 perseval-BLR). Nosso projeto: **MIT** (© 2026 PC do Fafa,
  `amd\LICENSE`). **Compatível**: podemos copiar trechos (HLSL do residual, exposição adaptativa, cinza
  AREA, parser de fatbin) mantendo o aviso de copyright deles no arquivo/CREDITOS.md. Nada é GPL.
- Cadeia de crédito que vale registrar se copiarmos: o worker deles deriva do **DLSS5-Feeder host64**
  (jlrouzies-fr, MIT) — o mesmo feeder que já creditamos; o parser de fatbin veio do "DLSS5-Autopilot
  core/gpu.py, MIT" (`build_release_zip.py:198-199`); o residual cita o "DLSSNR-Cost-Scaler"; a
  exposição cita "Ghady983/RenoDX-DLSS-5-Artifact-Fix". O `LICENSE` deles não reproduz o aviso do
  feeder — só informo.
- Spout2 (`Spout.dll`, `SpoutDX.dll`): BSD-2-Clause, redistribuível.
- `nvngx_dlssnr.dll`: propriedade da NVIDIA, build vazado; eles **redistribuem** no zip. O nosso
  `CREDITOS.md` diz "não distribua" — manter.
- O hook de `NvAPI_GPU_GetArchInfo` (`cpp:227-404`) é MIT, mas eles mesmos escrevem que "likely
  conflicts with the license terms of NVIDIA's redistributable" (`TECHNICAL.md:390-392`). Não
  precisamos dele hoje (a cadeia roda na RTX 4080 do usuário); decisão do usuário se um dia precisar.
- Se preferirmos não copiar linha nenhuma, os algoritmos cabem em três frases cada: (1) residual —
  `out = native + (up(nr_out) − up(nr_in)) × k`, `k = 1`, `max(·, 0)`, espaço de display; (2) exposição
  — média da luminância em 320×180, `smoothstep(0,10, 0,40)`, mapeia para `1,10 → 1,00`, EMA
  `a = 1 − e^(−dt/0,5)`, escreve `DLSS.Exposure.Scale`; (3) wipe — copiar a faixa `[0, split_x)` da
  cor crua sobre o output e limpar uma coluna de 2–3 px com a cor de destaque.

## 7. Sinais de alerta (só informo, sem julgar)

- **DLL da NVIDIA vazada redistribuída** no pacote, com assinatura Authenticode em `HashMismatch`
  (modificada após assinada). A nossa cópia em `bin\` está na mesma condição; a de `amd\` (310.8.SF.0)
  está sem assinatura.
- **Binários próprios sem assinatura e sem recurso de versão** (`native\nvngx.dll`, `Spout*.dll`;
  `NeuralScreen.exe` tem versão mas não assinatura). Issue #25 "Trojan.Generic" e o README avisam do
  SmartScreen.
- **Patch de código em memória** do `nvapi64.dll` (`VirtualProtect PAGE_EXECUTE_READWRITE` + escrita de
  `mov rax; jmp rax`) — comportamento que heurísticas de AV e anticheat reconhecem. O próprio README:
  "Do not use it in competitive online games. A process named nvngx.dll plus a fullscreen overlay is
  exactly what anti-cheat systems look for" (`README.md:61-62`).
- Processo **executável com extensão `.dll`** (`native\nvngx.dll --live`).
- `AttachThreadInput` com a thread do jogo ao abrir o menu (`display.py:513-523`).
- `RegisterHotKey` captura Num0–Num6 do sistema inteiro enquanto roda.
- **Sem telemetria, sem download em segundo plano, sem chave de API, sem ofuscação**: nenhum socket no
  Python nem no worker; as únicas URLs são o repositório e o canal, abertas no navegador só por clique
  (`main.py:2976-2986, 3021-3030`); `verify_github.py` (usa `urllib` para o GitHub) é ferramenta do
  autor e **não vai no zip** (`build_release_zip.py:74-81`). Sem atualização automática.
- Escritas fora da pasta: só `HKCU\...\Run\NeuralScreen` quando o usuário liga o autostart
  (`main.py:1539-1563`). Lê chaves do driver/HDR no registro (`main.py:107-147`). Log, config,
  `recordings\` e `screenshots\` ficam ao lado do programa.
- Código morto do feeder que escreveria `EnableHooks=2` num `ReShade.ini` ao lado do worker
  (`cpp:103-114`) — nunca chamado no modo `--live`.
- Modelo: tudo `R8G8B8A8_UNORM`, sem sRGB explícito — o NR recebe bytes de display como está.

---

## O que vale implementar (ordem de ganho ÷ esforço)

1. **Rede em resolução reduzida + composição residual no NR próprio** (§3.1) — 1–2 dias; ganho alto:
   custo por passe cai ~proporcional aos pixels, abre 8–10 passes a 60 fps, preserva texto/bordas 1:1.
   Medir nitidez com o alvo sintético da nossa matriz antes de ligar por padrão.
2. **DRED + SAFE PASSTHROUGH + falha dura vs. transitória** (§3.3) — horas; ganho médio: TDR da troca de
   resolução vira log com "onde morreu"; placa sem NR continua mostrando o jogo.
3. **Exposição adaptativa para a rede** (§3.2) — horas; ganho incerto/médio, só com A/B em cena escura
   (Witcher 3 à noite) medindo artefato temporal; teto 1,10 e EMA 0,5 s como ponto de partida.
4. **Wipe antes/depois** (§3.4) — 2–4 h; ganho médio para demonstração no canal e calibração pelo usuário.
5. **Presets do usuário no perfil da rede** (§3.6) — ~1 dia; ganho baixo-médio (snapshot dos 6 chips +
   passes, validado ao carregar, nome automático "Preset N").
6. **Gravação interna com áudio** (§3.6) — 2–4 dias em C++ (Media Foundation/NVENC + WASAPI loopback);
   ganho médio; só depois dos itens acima e se o RELATORIO-OBS mostrar que a captura externa não basta.

**Não copiar**: runtime Python (410 MB), fluxo óptico na CPU, pipeline serializado com um quadro em voo,
`AttachThreadInput` no jogo, hook de nvapi (zona cinzenta de licença, e não precisamos), redistribuir o
`nvngx_dlssnr.dll`.

---

### Apêndice: método e evidências

- Pasta analisada: `C:\Users\PCDOFAFA\Downloads\neural screen\` (data dos arquivos 2026-09-11 03:39).
- Código-fonte: `git clone --filter=blob:none --no-checkout` + sparse-checkout só de `.py/.md/.cpp/.h/
  .bat/.json/tests/` em
  `…\scratchpad\neural-screen\repo\` (HEAD `b63cb85…`, igual ao `VERSION.txt` do pacote).
- Hashes/assinaturas: `Get-FileHash`, `Get-AuthenticodeSignature`, `VersionInfo` (PowerShell).
- Strings dos binários: extração ASCII/UTF-16 por regex para
  `…\scratchpad\neural-screen\strings-{nvngx-worker,launcher,spoutdx}.txt`.
- Kernels CUDA: `…\scratchpad\neural-screen\fatbin_archs.py` (parser de registros fatbin, leitura de
  bytes, Python do sistema — não carrega a DLL).
- Nada foi executado do pacote; nada em `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\` foi alterado
  além deste relatório.
