# RELATÓRIO — Feeder 0.7.0 × 0.12.0 na cópia NVIDIA (build MinGW, sem portador AMD)

Data: 2026-09-05/06. Máquina: RTX 4080 (driver 596.49), Windows 11, ReShade 6.8.0.2155, RenoDX DLSS 5 v4.7 (0.2026.828.517), g++ 16.2 (msys64). Nada foi alterado fora de `amd\feeder-amd\dist\` (o binário novo), deste arquivo e do scratchpad `feeder-012\`.

Convenção: **[FATO]** = medido nesta máquina (log citado); **[INFERÊNCIA]** = dedução.

---

## 0. Resumo e recomendação

- **Recomendação: TROCAR** para o 0.12.0, com o cfg da seção 4 e o passo a passo da seção 7. Motivo decisivo, **[FATO]**: na rodada 2x, a segunda camada com o 0.7.0 morreu aos 3 frames (`the GPU did not retire allocator slot 1 within 2 s` → `stopped: the GPU stopped completing work`) e nunca mais entregou; no mesmo cenário, no mesmo instante de contenção (um stall de ~3 s), o 0.12.0 esperou, largou o frame e **seguiu a 54–57 fps** até o fim. É exatamente a mudança do 0.8.0-beta.4 ("timeout handling rather than permanent disabling") mais a serialização do caminho de frame. O mesmo se repetiu com **uma** camada quando outro processo ocupou 96 % da GPU (seção 3.4): o 0.7.0 parou de vez nas duas rodadas, o 0.12.0 abandonou 4 frames isolados e continuou.
- **[FATO]** Em rodadas limpas (uma camada, GPU sem concorrência) os dois são indistinguíveis: 60 fps apresentados, ~0,7 ms de CPU do feed por frame (4 % do frame), 2 avaliações NGX registradas pelo RenoDX (count=1 e count=60, como sempre), 2 "present travou" de arranque (do app, iguais), ~1,2–1,3 GB de VRAM do processo.
- **[FATO]** O 0.12.0 compilado aqui carrega no ReShade (`Registered add-on "DLSS 5 Feed 0.12.0" v0.12.0.1`), abre a sessão NGX pelo forwarder (`NVSDK_NGX_D3D12_Init -> 0x00000001`, `SuperSampling.Available=1`), entrega frames e o RenoDX avalia (`inline feature 18 evaluation succeeded (count=60)`), com 1 e com 2 camadas, e resistiu a abrir/fechar o painel 2× e alternar F6 10× sem erro, crash ou minidump.
- Binário oficial da release não foi baixado: a extensão do Chrome estava desconectada ("Browser extension is not connected"); o binário entregue é compilado da fonte local (seção 1). URL da release para quem quiser o MSVC oficial: `https://github.com/jlrouzies-fr/DLSS5-Feeder/releases/tag/v0.12.0` (asset `dlss5-feed.addon64`).
- Todas as strings do `dlss5-feed.log` que o `main.cpp` lê continuam iguais no 0.12.0 (seção 5). Única cosmética: `OverlayCollapsed` do `ReShade.ini` cita `DLSS 5 Feed 0.7.0@dlss5-feed.addon64`; com o nome novo a página do Feed nasce expandida no overlay do ReShade até alguém recolher (seção 7).

---

## 1. O binário entregue

`amd\feeder-amd\dist\dlss5-feed-nvidia-0.12.0.addon64` — 956.031 bytes, sha256 `9a2352df04da21e56618cd84765cd788d06d11d2c5825ec211c5383e9b826452`. Para uso, renomear para `dlss5-feed.addon64` (é o nome que o ReShade.ini, o app e o `montar-portable.ps1` conhecem).

**[FATO]** Compilado da fonte local `amd\feeder-amd\src-feeder\` (upstream HEAD `7927553`, `FEED_VERSION "0.12.0"`) com a mesma receita da variante AMD (`build-feed-amd.sh`: g++ `-std=c++20 -O2 -shared -static`, MinHook em C, forwarder NGX `ngx_forward.cpp`, shim SEH `feed_seh_shim.h`, headers do ReShade com os thunks de ABI e `nvsdk_ngx_params.h` na ordem de vtable do MSVC), mais uma define nova `-DFEED_NV_ONLY` aplicada numa cópia da fonte no scratchpad (`feeder-012\src-nv\`, script `build-feed-nv.sh`, `version-nvidia.rc`). O que a define faz (5 pontos, todos triviais):

| Onde | O quê |
|---|---|
| `feed_amd.h` `AmdCarrierWanted()` | devolve sempre `false` → o bloco NGX original roda sempre; nunca carrega `amd_fidelityfx_dx12.dll` |
| `feed_amd.h` `AmdDecoyOnRuntimeInit()` | retorna na entrada → sem swapchain-isca |
| `feed_amd.h` `AmdLogVariant()` | loga `[feed] build: DLSS5-Feeder 0.12.0 NVIDIA build (MinGW g++, SEH shim + ABI thunks; FSR carrier compiled out)` em vez de `[amd] variant:` |
| `dlss5-feed.cpp` `CfgWriteDefault()`/`CfgSave()` | não gravam as 4 chaves `amd_*` |
| `dlss5-feed.cpp` `DrawOverlay()` | sem a linha "AMD carrier:" |

O parser do cfg ainda aceita `amd_*` (ignoradas), e a linha `[amd] modules:` e `[amd] ngx forwarder: _nvngx.dll loaded via NGXCore\FullPath: ...` continuam no log (úteis para diagnóstico). Diferença de dependências, **[FATO]**: o 0.7.0 oficial importa `MSVCP140.dll`, `VCRUNTIME140.dll` e os `api-ms-win-crt-*` (VC++ 2015-2022 redist); o build MinGW importa só `msvcrt.dll` (não depende de redist).

Para recompilar: copiar `feeder-012\src-nv\` (ou repetir os 5 pontos acima sobre `amd\feeder-amd\`) e rodar `bash build-feed-nv.sh` (~40 s). O build sai com os mesmos 2 avisos do upstream (`'NAME' initialized and declared 'extern'`).

---

## 2. Changelog resumido 0.7.0 → 0.12.0 (releases do GitHub + README/PLAN-ISSUES da fonte)

| Versão (data) | O que muda que importa para o app |
|---|---|
| 0.7.0 (31 ago) | o que o app distribui hoje: OpenGL nas duas bitness; host cria recursos para importação GL |
| 0.8.0-beta.1/.2 (31 ago) | Vulkan/DXVK 32-bit; probes MV/Depth a cada 600 frames; shader do Feed em uma passada em vez de duas |
| 0.8.0-beta.3 (31 ago) | suporte ao RenoDX v4.6 (detecção pelos marcadores do binário; caminho lazy-adoption); grava defaults seguros |
| **0.8.0-beta.4 (1 set)** | **thread-safety**: `g_feed_cs` + flag `g_feed_busy` serializam o caminho por frame; Present reentrante é **descartado** em vez de aninhado; **timeout de GPU (`gpu_timeout_ms`, 2 s) abandona o frame em vez de desligar o feed de vez** (3 abandonos seguidos param); loga `first frame fed from thread N` / `frame fed from thread N, not the usual M` / `re-entrant frame ... dropped` |
| 0.9.0-beta.1 (1 set) | Vulkan external-memory; RenoDX v4.7; aviso de Smooth Motion; fix KOTOR (GL_EXTENSIONS) |
| 0.10.0-beta.1/.2 (1 set) | 32-bit `async_home`; detecção de Smooth Motion em D3D11/12; **resiliência do CreateFeature: nova tentativa com re-init do NGX; detecção de device removed com espera longa (300+ frames)**; reload mais silencioso |
| 0.11.0-beta.1 (2 set) | Deep Fried Chicken (ABI negociada); verificador de instalação |
| **0.11.0-beta.2 (2 set)** | **rastreia todos os effect runtimes e alimenta o que renderiza `DLSS5_Feed`** (antes podia ligar no runtime errado quando há 2, caso Smooth Motion); reconhece nomes versionados do add-on neural; **falha dentro do NGX init vira desligamento limpo; `dlss5-feed-crash.dmp` ao lado do log**; fallback FL10 no 32-bit |
| **0.12.0 (2 set)** | `work_upscale=1` (FSR 1 EASU+RCAS na volta do `work_resolution` < 100 %, muito mais nítido que o stretch bilinear) + `work_sharpness`; `work_upscale=2` (DLSS SR com jitter sintético, experimental, não recomendado); **rebuild drena a GPU antes de soltar feature/texturas** (evitava GPU hang com consumidor que mantém grafo vivo); `stall_log_ms` (linhas `STALL frame` e `worst frame`/`stalls` no resumo de 600 frames); painel in-game 32-bit; helper encerra limpo |

Cronologia curiosa, **[FATO]**: o 0.7.0 embarcado foi compilado em 31 ago 12:10 e todas as versões acima saíram entre 31 ago e 2 set — o autor lançou 12 releases em 3 dias. O `nvidia-smi` e o RenoDX não mudaram entre as rodadas.

---

## 3. Matriz comparativa (cópia isolada `scratchpad\feeder-012\app\`)

Fonte: janela do Edge com perfil limpo (`--user-data-dir` próprio, `--app=`) mostrando `fonte1080p60.html` (canvas 60 fps com texto rolando, partículas e formas; captura 2382×1341 no monitor principal a 125 %). App em `--outra-instancia --source window --window FonteTesteFeeder012`, DLSS 5 ligado (cfg do app: `mode=2 preset=5 work_resolution=100 create_delay=60 warmup_rebuild=180`), 60 s por rodada, morto por PID ao fim. GPU/VRAM amostrados a cada 2,5 s (`nvidia-smi` para a placa, contador `GPU Process Memory` para o processo). Painel/F6 injetados por `PostThreadMessage(WM_HOTKEY)` na thread principal do **nosso** PID (não mexe em atalhos globais nem em outras instâncias). Logs de cada rodada em `scratchpad\feeder-012\logs\<rótulo>-*.log` e `-gpu.csv`.

**Aviso de método [FATO]:** a máquina é compartilhada com outros agentes de teste. As rodadas das 16:06–16:10 (B1, A2, B2, A3) pegaram outra carga de GPU (placa a 100 %, VRAM total 4,4 GB contra 3,4 GB nas limpas; o próprio 0.12.0 atribuiu os stalls a "OUTSIDE this add-on entirely"), e as duas versões foram penalizadas alternadamente: 0.7.0 caiu a 14 fps (A3) e a menos de 10 (A2), o 0.12.0 a 15,8 (B1) e menos de 10 (B2). Repeti as quatro às 00:23 (B1r, A2r, B2r, A3r; janela-fonte no monitor secundário, captura 2858×1451). A B1r saiu limpa; a partir da A2r entrou o `scratchpad\otimizar-desempenho\jogo-teste.exe` de outro agente ocupando 96 % do motor 3D (contador `GPU Engine`) e as três seguintes rodaram a 100 % de GPU. Então: **para fps e custo valem A1, S0, B3, B1r, C1 e D1**; as rodadas a 100 % não medem desempenho, mas viraram um teste de **contenção** que separou as duas versões (3.4). Todos os logs ficam em `logs\`.

### 3.1 Uma camada

| Rodada | Feeder | Até `frame 1 delivered` | fps apresentados (blocos de 600 frames) | CPU do feed (ms/frame) | `evaluation succeeded` | `present travou` | `stopped:`/crash/re-entrant | GPU % (média/máx) | VRAM proc. (média/máx MB) |
|---|---|---|---|---|---|---|---|---|---|
| A1 | 0.7.0 | 4,05 s | 54,2 · 59,9 · 60,0 · 60,1 · 59,9 | 2,67 → 0,71–0,80 | 2 | 2 (arranque) | nenhum | 47 / 53 | 1171 / 1209 |
| S0 (25 s) | 0.12.0 | 4,04 s | 53,6 · 60,0 | 2,90 → 0,72 | 2 | 2 (arranque) | nenhum | 43 / 50 | 1128 / 1209 |
| B3 (+painel 2×, F6 10×) | 0.12.0 | 3,68 s | 50,4 · 60,0 · 59,8 · 60,0 · 59,5 | 2,79 → 0,45–0,67 | 2 | 2 (arranque) | nenhum | 72 / 100 | 1339 / 1361 |
| B1r (2858×1451, placa livre) | 0.12.0 | 5,24 s | 54,0 · 60,0 · 58,9 · 44,9 · 59,5 | 2,84 → 0,78–0,92 (3,27 no bloco com stall) | 2 | 3 | nenhum | 77 / 100 | 1480 / 1664 |
| A1 × B1r | — | iguais dentro do ruído | 60 fps de teto nos dois; o bloco de 44,9 fps da B1r tem `worst frame 1383 ms` = 1 stall externo | 0,71–0,80 × 0,78–0,92 | 2 × 2 | | | | a VRAM maior da B1r é a captura maior (2858×1451 × 2382×1341) |

Rodadas contaminadas (GPU a 100 % por outro teste; só para registro): A2 (0.7.0) e B2 (0.12.0) não fecharam nenhum bloco de 600 frames; A3 (0.7.0) 14,0 fps com painel/F6 sem erro; B1 (0.12.0) 15,8 fps.

Leitura: com a placa livre, os dois entregam o teto da fonte (60 fps) com o mesmo custo de CPU do feed (~0,7–0,9 ms = 4–5 % do frame). Os 2 "present travou" são sempre os mesmos dois do arranque do app (≈850 ms e ≈450 ms, quando o ReShade compila os shaders e o Feeder abre a sessão D3D12), com qualquer versão. O 0.12.0 imprime linhas `STALL frame` (é o `stall_log_ms=50` novo, só diagnóstico): nas rodadas limpas são só as 2–3 do arranque.

### 3.4 Sob contenção de GPU (outro processo a 96 % do motor 3D) — o teste que não estava no plano

| Rodada | Feeder | Até `frame 1 delivered` | O que aconteceu depois |
|---|---|---|---|
| A2r | 0.7.0 | 19,4 s | 3 frames entregues; 2,6 s depois `the GPU did not retire allocator slot 1 within 2 s` → **`stopped: the GPU stopped completing work`** → `failure: command list`; feed morto pelos 38 s restantes; RenoDX só viu `count=1` |
| B2r | 0.12.0 | 28,7 s | 4× `the GPU did not retire allocator slot 2 within 2000 ms` / `failure: command list` (00:26:05, :12, :24, :31), **nunca 3 seguidos, nunca `stopped:`**; o feed continuou entregando entre os abandonos (menos de 600 frames em 30 s, então sem bloco de fps); RenoDX `count=1` |
| A3r | 0.7.0 | 15,7 s | 3 frames e **`stopped:`** aos 17,8 s, antes mesmo do painel/F6 (t+27,8 s) |

**[FATO]** Nas três rodadas o 0.7.0 desligou o feed de vez no primeiro fence que passou de 2 s; o 0.12.0, no mesmo tipo de carga, abandonou frames isolados e seguiu vivo. É o mesmo mecanismo da seção 3.2 (camada 2), agora com uma camada só. **[INFERÊNCIA]** Para o usuário isso é a diferença entre "a rede caiu e só volta reabrindo o app" e "deu uma engasgada e voltou" sempre que outro programa pesado (jogo, navegador com vídeo, OBS) disputar a placa.

### 3.2 Duas camadas (`dlss_camadas=2` / `dlss2x=1`)

A segunda camada é outra instância do app (`--camada 2 --window-hwnd <janela da primeira>`) numa pasta `camada2\` espelhada, com o **mesmo** `dlss5-feed.addon64` e `create_delay=15 warmup_rebuild=30` gravados pelo app. Captura 1902×973.

| Rodada | Feeder | Camada | Até `frame 1 delivered` | fps (blocos de 600) | `evaluation succeeded` | `present travou` | Erros | GPU % / VRAM proc. (2 procs) |
|---|---|---|---|---|---|---|---|---|
| C1 | 0.7.0 | 1 | 4,77 s | 34,4 · 59,8 · 60,0 · 60,0 · 60,0 | 2 | 2 | nenhum | 77 / 100 · 2159 / 2318 MB |
| C1 | 0.7.0 | 2 | 3,50 s | **nenhum bloco (3 frames entregues)** | 2 | 3 (um de 3013 ms) | **`the GPU did not retire allocator slot 1 within 2 s` → `stopped: the GPU stopped completing work` → `failure: command list` aos 17 s; feed morto até o fim** | |
| D1 | 0.12.0 | 1 | 4,76 s | 43,7 · 58,7 · 59,3 · 58,4 · 58,6 | 2 | 5 | nenhum | 90 / 100 · 2195 / 2317 MB |
| D1 | 0.12.0 | 2 | 3,50 s | **35,7 · 54,1 · 57,0 · 56,6** | 2 | 6 (um de 3037 ms) | nenhum `stopped:`; o mesmo stall aparece como `STALL frame 201: feed 2036.94 ms` (a espera de `gpu_timeout_ms`) e `worst frame 3138 ms ... stalls 7` no primeiro bloco; depois `stalls 0` | |

Leitura **[FATO]**: o cenário 2x reproduz de forma determinística (duas rodadas, mesmo instante t+8…14 s, mesmo stall de ~3 s na segunda camada: é o momento em que as duas instâncias criam feature e o RenoDX constrói o grafo neural nas duas). O 0.7.0 trata o fence não sinalizado em 2 s como "GPU morreu" e desliga o feed para sempre — a segunda camada passa o resto da sessão em bypass, exibindo o quadro sem rede (é o sintoma "a segunda camada nunca subiu" do RELATORIO-2X-VISUAL). O 0.12.0 espera o mesmo prazo, abandona o frame, contabiliza e continua; só desligaria com 3 abandonos seguidos. **[INFERÊNCIA]** É também o que acontecerá numa máquina mais fraca ou com jogo pesado ao lado, mesmo com uma camada.

### 3.3 Estabilidade (painel e F6)

**[FATO]** B3 (0.12.0, placa livre) e A3 (0.7.0, sob carga a 14 fps): painel do app aberto/fechado 2× e F6 (liga/desliga tudo, que também dispara o `PokeReShade`) 10× a partir de t+27 s. Nas duas versões: nenhuma linha de erro no `dlss5-feed.log`, nenhum `stopped:`, nenhum `### CRASH RECORDED ###`, nenhum `dlss5-feed-crash.dmp`, o feed volta a entregar depois de cada religada; no B3 os blocos de 600 frames seguem a 59,5–60 fps durante e depois dos toques. (Na A3r o 0.7.0 já tinha parado por contenção antes de os toques começarem, seção 3.4.) `ReShade.log`: os únicos 2 `ERROR` de cada rodada são o RenoDX procurando `NVSDK_NGX_D3D12_EvaluateFeature_C` (idênticos nas duas versões, inofensivos).

---

## 4. Compatibilidade de configuração (`dlss5-feed.cfg`)

**[FATO]** As 14 chaves que o app grava hoje existem no 0.12.0 com o mesmo nome e significado (`enabled, mode, hdr, depth_inverted, flags, reset_every, warmup_rebuild, rebuild, log_frames, create_delay, preset, work_resolution, mv_scale_x, mv_scale_y`); os defaults do 0.12.0 para elas são os mesmos que o app usa, exceto `preset` (default 0; o app grava 5, e o valor do arquivo vence). O 0.12.0 lê o cfg do app sem reclamar (log `config: ... work_resolution=100% work_upscale=0 work_sharpness=0.30 gpu_timeout_ms=2000 ... stall_log_ms=50`) e o app continua gravando `work_resolution`/`create_delay`/`warmup_rebuild` por substituição de linha (`GravarChaveCfg`), sem apagar chaves que não conhece. Se alguém tocar a página do Feed no overlay do ReShade, o `CfgSave()` do 0.12.0 regrava o arquivo inteiro com as 21 chaves abaixo (o app continua lendo/gravando as suas normalmente).

Chaves novas (todas opcionais; sem elas vale o default):

| Chave | Default | Significado | Proposta para o app |
|---|---|---|---|
| `work_upscale` | 0 | como a saída em `work_resolution` < 100 volta ao tamanho nativo: 0 stretch bilinear, 1 FSR 1 (EASU+RCAS), 2 DLSS SR com jitter sintético (experimental, custa como 100 %, "shimmer") | **1** — só tem efeito quando `desempenho.h` encolhe a camada (100 → 80 → 67); nesses casos a imagem fica visivelmente mais nítida sem custo relevante. A 100 % só roda o RCAS |
| `work_sharpness` | 0.3 | força do RCAS para `work_upscale` 1/2, 0..1 | manter 0.3 (a 100 % com `work_upscale=0` não roda) |
| `gpu_timeout_ms` | 2000 | quanto um frame espera a GPU liberar um allocator antes de ser abandonado; 3 seguidos param o feed (100..60000) | **manter 2000** (foi o que salvou a camada 2); numa máquina fraca 4000 é opção |
| `stall_log_ms` | 50 | log `STALL frame` para intervalos acima disso (0 desliga) | **0** no pacote (log mais limpo para o usuário; o app não lê essas linhas); ligar quando se investiga |
| `buffer_home`, `async_home`, `sync_home` | 1, 0, 0 | só transporte Vulkan/32-bit; ignorados no D3D11 64-bit | omitir |
| `half_home`, `passthrough`, `jitter_sign`, `jitter_phases` | 0, 0, 1, 0 | diagnósticos (parse-only) | omitir |
| `host_window`, `cast_key`, `cast_scale`, `cast_mode` | — | só jogos 32-bit (helper) | não existem no 64-bit |

Chaves que mudaram de comportamento: `warmup_rebuild` continua "só caminho RenoDX, pulado em v45+/v4.6+" (o log do 0.12.0 confirma `v4.7+ (per-present rescan, lazy adoption, ...)`, então o re-create de aquecimento **não** roda, igual ao 0.7.0); `flags` idem; `create_delay` idem. Nenhuma chave foi removida.

`dlss5-feed.cfg` proposto para o `amd\` (origem do `montar-portable.ps1`):

```
enabled=1
mode=2
hdr=-1
depth_inverted=-1
flags=-1
reset_every=0
warmup_rebuild=180
rebuild=0
log_frames=3
create_delay=60
preset=5
work_resolution=100
work_upscale=1
work_sharpness=0.30
gpu_timeout_ms=2000
stall_log_ms=0
mv_scale_x=1.000
mv_scale_y=1.000
```

O 0.7.0 ignora as 4 linhas novas (parser por nome), então este cfg serve para os dois binários; `perfil-amd\dlss5-feed.cfg` precisa das mesmas linhas comuns, senão o `montar-portable.ps1` avisa "chaves comuns diferem".

---

## 5. O que o app lê do `dlss5-feed.log` — tudo continua igual

| String procurada pelo `main.cpp` | Onde | 0.12.0 |
|---|---|---|
| `NVSDK_NGX_D3D12_Init ->` | `NgxPeloFeed` (linha ~3269) | igual |
| `NGX capabilities:` / `SuperSampling.Available=1` / `NeedsUpdatedDriver=1` | idem (~3253) | igual |
| ` delivered (` | `CadeiaEntregando` (~3588) e estado da camada 2 (~8565) | igual: `frame 1 delivered (2382x1341 at 100% -> 2382x1341, reset=1, jitter +0.000,+0.000)` — ganhou o sufixo `jitter`, o app só procura o prefixo |
| `stopped:` | estado da camada 2 (~8574, ~8604) | igual (`Warn("stopped: %s...")`) |
| `dlss5-feed.cfg` chaves `work_resolution`, `create_delay`, `warmup_rebuild` | `GravarChaveCfg` | iguais |

No `ReShade.log` o app procura linhas do RenoDX, não do Feeder; a única linha do Feeder que muda ali é o registro (`Registered add-on "DLSS 5 Feed 0.12.0" v0.12.0.1`).

---

## 6. Riscos

1. **Build MinGW, não o MSVC oficial.** Diferenças conhecidas [INFERÊNCIA a partir do RELATORIO-FEEDER-AMD §2.2]: exceção C++ lançada dentro do NGX e não tratada derruba o processo em vez de virar "disable" (o shim SEH só captura falhas de hardware: AV, UD2, divisão por zero); stack overflow não é capturado. Nos testes nada disso ocorreu. O crash dump `dlss5-feed-crash.dmp` do 0.11.0-beta.2 é escrito por um handler do próprio Feeder e continua funcionando.
2. **Forwarder NGX** em vez da `nvsdk_ngx_d.lib`: `Init` tenta `NVSDK_NGX_D3D12_Init_Ext` e cai para `Init`; só o `_Ext` foi exercitado (funcionou). Sem `_nvngx.dll` (máquina sem driver NVIDIA) o feed desliga limpo com `FeatureNotSupported`.
3. **Thunks de ABI** (8 métodos virtuais do ReShade + ordem de vtable do `NVSDK_NGX_Parameter`): validados por funcionamento ponta a ponta (DLAA + RenoDX avaliando) em 1 e 2 camadas, painel e F6. Um método virtual do ReShade que devolva struct por valor e que o Feeder passe a chamar numa versão futura precisaria de thunk novo.
4. **`OverlayCollapsed`** do `ReShade.ini` cita `DLSS 5 Feed 0.7.0`; com o 0.12.0 a página do Feed aparece expandida na aba Add-ons até ser recolhida (o ReShade regrava a chave). Cosmético; ver 7.
5. **Log mais falante**: `STALL frame` (desligável com `stall_log_ms=0`) e `worst frame ... stalls N` no resumo de 600 frames. Nada que o app leia.
6. **Sem teste em jogo real** nesta rodada (o cenário foi janela do Edge, que é o caso de uso do app: captura de janela + DLAA 1:1). O RELATORIO-FEEDER-AMD já tinha exercitado o mesmo binário-base (com `amd_carrier=-1`) no charmap.
7. **`work_upscale=1`** só foi validado pela linha `fsr1 shaders: ok (EASU + RCAS expand-back available)`; não medi imagem a 67/80 % nesta rodada. Se preferir zero risco visual, deixar `work_upscale=0` (comportamento idêntico ao 0.7.0) e ligar depois de um teste do `desempenho.h`.

---

## 7. Passo a passo para trocar (não executado)

O `montar-portable.ps1` copia `dlss5-feed.addon64` **da raiz `amd\`** para `bin\` (lista `$programa`, linha ~66) e o `dlss5-feed.cfg` da raiz para `bin\` só quando não existe ou com `-SobrescreverConfig` (`Copiar-Config`, linha ~98). O `bin-amd` recebe a variante AMD (`feeder-amd\dlss5-feed-amd.addon64`) e não é afetado.

1. Guardar o atual: `amd\dlss5-feed.addon64` → `amd\dlss5-feed.addon64.bak-0.7.0` (164.352 bytes, sha256 `e6861abe…c0e2`).
2. Copiar `amd\feeder-amd\dist\dlss5-feed-nvidia-0.12.0.addon64` para `amd\dlss5-feed.addon64` (mesmo nome de sempre; nada muda no `ReShade.ini`, no app ou no script). Alternativa sem sobrescrever a raiz: no `montar-portable.ps1`, trocar a entrada `'dlss5-feed.addon64'` de `$programa` por uma cópia explícita `Copy-Item (Join-Path $Root 'feeder-amd\dist\dlss5-feed-nvidia-0.12.0.addon64') (Join-Path $Bin 'dlss5-feed.addon64') -Force`.
3. Atualizar `amd\dlss5-feed.cfg` com o bloco da seção 4 (4 linhas novas) e replicar as mesmas linhas comuns em `amd\perfil-amd\dlss5-feed.cfg` (o script compara as chaves comuns e avisa se diferirem).
4. `amd\ReShade.ini`, chave `[ADDON] OverlayCollapsed`: trocar `DLSS 5 Feed 0.7.0@dlss5-feed.addon64` por `DLSS 5 Feed 0.12.0@dlss5-feed.addon64` (e o mesmo em `perfil-amd\ReShade.ini` se ele tiver a chave). Cosmético.
5. Rodar o `montar-portable.ps1` como de costume; como o portable já existe, o `bin\dlss5-feed.cfg` antigo é **mantido** (sem as chaves novas → defaults do 0.12.0: `work_upscale=0`, `stall_log_ms=50`); para levar o cfg novo usar `-SobrescreverConfig` ou copiar o arquivo à mão. O atualizador (`atualizacao.h`) já trata `dlss5-feed.cfg` como configuração preservada, então em máquinas de usuário vale a mesma observação.
6. Conferir no `bin\dlss5-feed.log` da primeira abertura: `dlss5-feed 0.12.0 (built Sep  5 2026 ...) attached.`, `[feed] build: DLSS5-Feeder 0.12.0 NVIDIA build (...)`, `NVSDK_NGX_D3D12_Init -> 0x00000001`, `frame 1 delivered`; e no `ReShade.log` o `inline feature 18 evaluation succeeded (count=60)`.
7. `CREDITOS.md`/`README.md`: a versão do Feeder citada passa a 0.12.0 (licença MIT inalterada; MinHook BSD-2 já está no `LICENSE-MinHook.txt` do `dist\`).

Reverter = devolver o `.bak-0.7.0` ao nome original; o cfg novo é compatível com o 0.7.0.

---

## 8. Arquivos

```
amd\feeder-amd\dist\dlss5-feed-nvidia-0.12.0.addon64     o binário (956.031 bytes)
amd\docs\pesquisa-2026-09-05\RELATORIO-FEEDER-012.md      este relatório
scratchpad\feeder-012\
  src-nv\                 cópia da fonte com FEED_NV_ONLY, build-feed-nv.sh, version-nvidia.rc, build\build-nv.log
  app\                    cópia isolada do app (exe, dxgi.dll, add-ons, cfg/ini, shaders, cache) + dlss5-feed.addon64.orig-0.7.0
  fonte1080p60.html       a fonte de movimento; edge.ps1 abre-a no Edge com perfil limpo (edge-profile\)
  rodar.ps1               harness (troca o addon, ajusta camadas, roda 60 s, amostra GPU, injeta painel/F6, mata por PID, resume)
  matriz.sh / matriz2.sh  as 8 rodadas + as 4 repetidas
  logs\<rótulo>-dlss5-feed.log, -ReShade.log, -dlss5forall.log, -gpu.csv (+ -camada2-* nas rodadas 2x); matriz.txt, matriz2.txt
```
