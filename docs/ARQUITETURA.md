# Arquitetura do DLSS5 For All

Documento de referência da cadeia inteira: o que é cada peça, como elas conversam, o que
ler quando algo quebra. Escrito em 2026-09-08, depois da noite em que a atualização do
ReShade quebrou o liga/desliga do filtro e travou os jogos.

Cada seção é curta e fechada de propósito: uma mudança futura deve tocar um trecho só.

---

## 1. A cadeia, em um diagrama

```
   ┌───────────────────────────────────────────────────────────────────────┐
   │  PROCESSO DO JOGO  (witcher3.exe, b1.exe, ...)                        │
   │                                                                       │
   │  NADA NOSSO ENTRA AQUI. Sem DLL injetada, sem hook, sem overlay.      │
   │  O jogo só desenha na janela dele, como sempre.                       │
   └───────────────────────────────────────────────────────────────────────┘
                                    │
                    Windows Graphics Capture (WGC)
                    o compositor entrega o quadro pronto
                                    ▼
   ┌───────────────────────────────────────────────────────────────────────┐
   │  PROCESSO DA CAMADA  (DLSS5ForAll.exe)                                │
   │                                                                       │
   │   screen_capture.h ──► quadro capturado                               │
   │          │                                                            │
   │          ▼                                                            │
   │   janela de render (D3D11)  ◄── É AQUI que o ReShade vive             │
   │          │                                                            │
   │          │   dxgi.dll (ReShade) carregado neste processo              │
   │          │        │                                                   │
   │          │        ├─ dlss5-feed.addon64    → DLSS/DLAA (NGX D3D12)    │
   │          │        ├─ renodx-dlss.addon64   → Neural Rendering         │
   │          │        └─ dlss5-bridge.addon64  → ponte de controle        │
   │          ▼                                                            │
   │   Present ──► a janela sobreposta, colada em cima do jogo             │
   └───────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
                            o que a pessoa vê
```

### O fato que confunde todo mundo

**O ReShade não é injetado no jogo.** O `dxgi.dll` fica ao lado do `DLSS5ForAll.exe` e é
carregado no processo **da camada**, não no do jogo. O jogo é capturado de fora, pelo
compositor do Windows (WGC), e o que passa por ReShade, DLSS e Neural Rendering é a
**cópia** do quadro, dentro da nossa janela.

É por isso que o app funciona "sem injetar DLL no jogo" e não dispara anticheat. E é por
isso que, quando algo trava o jogo, a culpa quase nunca é de render — é de entrada
(foco, fila de mensagens), que é o único ponto em que tocamos o processo alheio. Ver §7.

---

## 2. As peças

| Peça | Origem | O que é | O que faz |
|---|---|---|---|
| `DLSS5ForAll.exe` | `main.cpp` (~11k linhas) + headers | processo | captura, camada, painel, atalhos, orquestra tudo |
| `dxgi.dll` | ReShade (terceiros, binário) | DLL proxy | carrega o ReShade e os add-ons no processo da camada |
| `dlss5-feed.addon64` | `feeder-amd/src-feeder/` (terceiros + patch) | add-on ReShade | faz DLSS/DLAA via NGX em D3D12 |
| `renodx-dlss.addon64` | terceiros (ShortFuse) | add-on ReShade | Neural Rendering (`Reserved18`) — ver §5 |
| `dlss5-bridge.addon64` | `addon/dlss5_bridge_addon.cpp` | add-on ReShade | ponte de controle app ↔ ReShade |
| `nvngx_dlss.dll` | NVIDIA | modelo | pesos do DLSS (super resolution) |
| `nvngx_dlssnr.dll` | NVIDIA | modelo | pesos do Neural Rendering (~165 MB) |
| launcher | `portable-launcher/launcher.cpp` | processo | escolhe NVIDIA (`bin\`) ou AMD (`bin-amd\`) e abre o exe certo |

### Headers do app (`main.cpp` inclui)

| Header | Responsabilidade |
|---|---|
| `screen_capture.h` | captura WGC de janela/monitor |
| `reshade_bridge.h` | **o protocolo da ponte** (§4) — lado cliente |
| `idioma.h` | `T(pt, en)` no ponto de uso; `LINGUA_PT`/`LINGUA_EN` |
| `seletor_hud.h` | a primeira tela (escolha da fonte) |
| `splash_ofertas.h` | tela do grupo/QR; também é a tela do launcher (modo launcher) |
| `desempenho.h` | limite de fps, prioridade de GPU, "Otimizar" |
| `limitador/fpslimit_host.h` | a peça que roda **dentro** do jogo para travar fps |
| `motion_estimate.h` | motion vectors pelo bloco de vídeo da GPU (D3D12 VME) |
| `hdr_control.h`, `shared_source.h`, `qr.h`, `atualizacao.h`, `versao.h` | HDR, transporte entre camadas, QR, atualização, versão |

### Camadas (`--camada N`)

`kMaxCamadas = 2` (`main.cpp:799`) — existe "1x" e "2x"; a terceira foi desabilitada. Cada
camada é **uma instância separada do exe**, empilhada sobre a anterior. A de baixo publica
o quadro por textura compartilhada (`Local\DLSS5Frame_<pid>`, `main.cpp:815`), escrita pelo
add-on da ponte de dentro do `reshade_finish_effects`; a de cima consome. Com
`DLSS5_SEM_SHARED=1` volta ao transporte antigo (a de cima captura a de baixo pelo
compositor).

**Camada ≠ passe.** Camada é a cadeia inteira de novo (custo dobrado). Passe é a rede
neural rodando N vezes dentro de **uma** avaliação. Ver §5.

### Módulo AMD (`--amd`)

`g_moduloAmd` liga por detecção (`version.dll` + `dlssnr_on_amd.ini` ao lado do exe,
`main.cpp:8433`) ou pela flag `--amd`. Nesse modo **não há RenoDX no processo**: quem faz o
neural é o mod externo DLSS-NR-on-AMD, e o estado sai do `dlssnr_on_amd.ini` dele
(`[DlssNrOnAmd] Enabled`), não do ReShade. Todo o caminho RenoDX/NGX de §5 não se aplica.

---

## 3. Arquivos de configuração: quem lê, quem escreve, **quando**

Esta é a seção que custou uma noite de trabalho. O "quando" importa tanto quanto o "quem".

| Arquivo | Dono | O app lê | O app escreve |
|---|---|---|---|
| `dlss5forall.ini` | nosso | sim | sim |
| `ReShade.ini` | ReShade | sim | sim, com cuidado (ver abaixo) |
| `dlss5-feed.cfg` | feeder | — | não confirmado |
| `FafaDLSS5.ini` | ReShade (preset) | — | não confirmado |
| `dlssnr_on_amd.ini` | mod AMD | sim | sim (só no módulo AMD) |

### `dlss5forall.ini` — a memória do app

Caminho: `g_dadosDir + dlss5forall.ini` (`IniPath()`). Seções observadas:

- `[ultima]` — a última escolha: `fonte`, `janela`, `monitor`, `sobrepor`, `telacheia`,
  `vsync`, `hdr`, `escala`, `work_resolution`, `placa`, `dlss_camadas`, `fps_tela`,
  `painel_x`/`painel_y`, `transporte_camadas`, `nr_estilo`, `nr_tom_local`
- `[atalhos]` — `painel`, `tudo`, `filtro`, `interativo`, `efeitos`, `sair`
- `[idioma]` — `lingua=pt|en` (o launcher grava a mesma chave, nos dois módulos)
- `[hdr]`, `[efeitos]`, `[ofertas]`, `[camada2_feed]`

### `ReShade.ini` — e a regra que não é óbvia

Chaves que o app toca:

| Seção | Chave | O app faz | Observação |
|---|---|---|---|
| `[INPUT]` | `KeyOverlay` | lê | a tecla que abre o overlay (`TeclaOverlayReShade`); o app **nunca** assume "Home" |
| `[OVERLAY]` | `ShowFPS` | lê+grava | `DefinirFpsNaTela()` |
| `[GENERAL]` | `EffectSearchPaths` | lê | onde estão os shaders |
| `[RENODX-DLSS]` | `DirectNeuralRenderingHookPoint` | lê+grava | **o liga/desliga do neural no add-on novo** (§5) |
| `[RenoDX.DLSS5]` | `NeuralUplift`, `NRToggleKey` | lê | **legado**, só vale com o add-on antigo |

> ### ⚠️ O ReShade regrava o `ReShade.ini` por cima
>
> O ReShade mantém a configuração **inteira em memória** e salva o arquivo sozinho
> (AutoSave). Uma escrita feita de fora com `WritePrivateProfileStringW` vale até ele
> salvar — e então é apagada, com o valor antigo de volta.
>
> **Medido em 2026-09-08:** o app gravou `HookPoint=2` às 00:16:44; o ReShade reescreveu o
> arquivo às 00:19:28 e o valor voltou a `0`.
>
> Escrever de fora só é confiável quando o ReShade **ainda vai ler** o arquivo, isto é,
> antes de ele carregar. Durante a sessão, use a API: `PedirConfigAoReShade()` no app
> (export `ReShadeSetConfigValue` do `dxgi.dll`) ou `rsbridge::SetConfigInt()` pela ponte.
> O app grava pelos dois caminhos de propósito.

> ### ⚠️ O add-on novo do RenoDX lê o Hook Method **uma vez só**
>
> **Medido:** mudamos a config viva pela ponte, o ReShade confirmou (`config [RENODX-DLSS]
> DirectNeuralRenderingHookPoint = 2`), e o add-on **não reagiu** — nem no minuto seguinte,
> nem ao abrir o overlay. Ele lê no carregamento e mantém a própria cópia.
>
> Consequência de projeto: **não existe toggle em tempo real do Neural Rendering.** O único
> momento em que dá para acertar o caminho é antes de os add-ons carregarem, e é o que
> `CorrigirHookPointNoArranque()` (`main.cpp:3283`) faz, chamada em `main.cpp:9159` — logo
> antes de nascer a janela de render, que é o que dispara o carregamento dos add-ons.

### `dlss5-feed.cfg`

Lido pelo feeder. Chaves observadas no arquivo: `enabled`, `mode`, `hdr`,
`depth_inverted`, `flags`, `reset_every`, `warmup_rebuild`, `rebuild`, `log_frames`,
`create_delay`, `preset`, `work_resolution`, `mv_scale_x`, `mv_scale_y`.
**Não confirmei** se o app escreve neste arquivo.

### `FafaDLSS5.ini`

Preset do ReShade (apontado por `[GENERAL] PresetPath`). Define `Techniques` e
`TechniqueSorting` — quais shaders entram na cadeia — e os uniforms de cada `.fx`. O preset
em uso carrega `SMAA`, `Lumenite_Kernel`, `DLSS5_Feed` e `DLSS5_Sharpen`.

---

## 4. O protocolo da ponte (`reshade_bridge.h`)

O app não fala com o ReShade por teclado. Fala com o **nosso** add-on, que roda dentro do
processo, por três exports:

| Export | Papel |
|---|---|
| `DLSS5Bridge_Version` | congelado em `PROTOCOL_MIN` para sempre — "a versão mais velha que ainda falo" |
| `DLSS5Bridge_Protocol` | a versão real; **não existe** nos add-ons antigos, e a ausência já é a resposta |
| `DLSS5Bridge_Submit` | enfileira/aplica um comando |
| `DLSS5Bridge_Query` | devolve o status |

### Comandos

| # | Comando | Uso |
|---|---|---|
| 1–3 | `UNIFORM_INT` / `FLOAT` / `BOOL` | escrever uniform de shader |
| 4 | `TECHNIQUE` | ligar/desligar técnica |
| 5 | `RESET_UNIFORM` | devolver o uniform ao padrão |
| 6 | `NR_TOGGLE` | *(protocolo 2)* pulso na hotkey do RenoDX **antigo** — morto com o add-on novo |
| 7 | `SET_CONFIG_INT` | *(protocolo 3)* grava inteiro na config viva do ReShade |

`SET_CONFIG_INT` reaproveita a struct existente: `effect` = seção, `name` = chave,
`ivalue[0]` = valor. Não passa pela fila de efeitos — não depende de `effect_runtime` nem de
shader compilado, e o valor precisa valer na hora.

### Regra de compatibilidade

**Campos novos de status entram sempre no fim do struct**, e o add-on aceita qualquer
`struct_size` entre o tamanho da v1 (`44`, congelado) e o atual, preenchendo só o que couber.
É o padrão `cbSize` do Win32, e é o que deixa as quatro combinações (exe velho/novo ×
add-on velho/novo) andarem sem quebrar.

Campos do protocolo 3, no fim: `config_ready` (1 = o add-on sabe gravar config) e
`nr_hookpoint` (o Hook Method lido da **config viva**, `-1` = não sei).

`rsbridge::NeuralHookPoint()` devolve `-1` quando `protocol < 3`, e é por isso que o app
sempre tem um caminho de reserva pelo arquivo.

---

## 5. As duas gerações do add-on RenoDX

**Dois add-ons diferentes, de autores diferentes, com nomes quase iguais.** Confundi-los
custa horas — a documentação pública mistura os dois o tempo todo.

| | `renodx-dlss5.addon64` | `renodx-dlss.addon64` |
|---|---|---|
| autor | Krish / Lecram | ShortFuse |
| natureza | **consumidor** neural: lê o contrato NGX que já existe | **construtor**: monta a requisição DLSS ele mesmo |
| convive com feeder? | **sim** — é feito para isso | **não** — "neither needs nor tolerates the feeder" |
| liga/desliga | `[RenoDX.DLSS5] NeuralUplift`, hotkey F6 | `[RENODX-DLSS] DirectNeuralRenderingHookPoint`, **sem hotkey** |
| linha de toque no log | `NR toggled ON/OFF` | não escreve nada |
| tem `Pass Count`? | não | **sim** (1 a 10) |

O `dlss5-feed` é funcionalmente um feeder. Trocar do primeiro para o segundo trocou um
colaborador por um concorrente pelo mesmo NGX — **foi essa troca, e não a atualização do
ReShade em si, que quebrou o liga/desliga em 2026-09-07.**

O app detecta qual está instalado em `RenoDxNovo()` (`main.cpp:3235`): pelo nome do arquivo
primeiro, pela chave no ini como confirmação.


### As chaves de qualidade mudaram de nome E de seção (corrigido em 2026-09-08)

Não é só o liga/desliga que trocou de nome. **Todo o perfil de imagem** trocou, e ficou
inerte por um dia inteiro sem que nada no log acusasse:

| add-on antigo — `[RenoDX.DLSS5]` | add-on novo | seção nova |
|---|---|---|
| `NRIntensity` | `DirectNeuralRenderingIntensity` | `[RENODX-DLSS-preset1]` |
| `NRStyle` | `DirectNeuralRenderingStyle` | `[RENODX-DLSS-preset1]` |
| `NRLocalTone` | `DirectNeuralRenderingLocalToneStrength` | `[RENODX-DLSS-preset1]` |
| `NRGlobalTone` | `DirectNeuralRenderingGlobalToneStrength` | `[RENODX-DLSS-preset1]` |
| `NRSkinStructure` | `DirectNeuralRenderingSkinStructureStrength` | `[RENODX-DLSS-preset1]` |
| `NRLocalStructure` | `DirectNeuralRenderingLocalStructureStrength` | `[RENODX-DLSS-preset1]` |
| `NRAutoMask` | `DirectNeuralRenderingAutoMask` | `[RENODX-DLSS-preset1]` |
| `NRUICorrection` | `DirectNeuralRenderingUiCorrectionMode` | `[RENODX-DLSS]` |
| `NRDiffuseWhiteNits` | `DirectNeuralRenderingDiffuseWhiteNits` | `[RENODX-DLSS]` |
| `NRPreset` | **não existe** — o preset virou a própria seção (`-preset1/2/3`) | — |
| `NeuralUplift=1` | `DirectNeuralRenderingHookPoint=2` | `[RENODX-DLSS]` |

**O valor NÃO é convertido.** As duas gerações empurram o número para o mesmo campo NGX —
as duas carregam as strings `DLSSNR.Intensity`, `DLSSNR.LocalToneStrength`,
`DLSSNR.SkinStructureStrength` — e o binário novo diz, no próprio tooltip, que "manually
entered values are forwarded unchanged" e que os binários "consume this value without range
validation". O que mudou foi só o **padrão de fábrica** (o antigo nascia em 2, o novo em 1,
que é o default do backend) e o passo do slider. Logo `0.3` continua sendo `0.3` para a
rede, e os números da matriz de borrão valem como estão.

A tradução vive em `RsGravarNr()` / `RsLerNr()` (`main.cpp`, logo depois de `RenoDxNovo()`),
com a tabela `kRsChavesNr`. A geração é decidida pela **pasta do próprio ini**
(`RenoDxNovoEm`), e não por `g_exeDir`: o `CamadaIniciar` prepara o pacote de *outra* camada.

Os padrões por geração ficam em `wWinMain` (tabela `tom[]`, colunas `padraoNovo` /
`padraoAntigo`), gravados a partir de `[ultima] nr_estilo`, `nr_tom_local`,
`nr_intensidade`, `nr_tom_global`, `nr_estrutura_local`, `nr_estrutura_pele`,
`nr_mascara_auto` do `dlss5forall.ini`.

**Verificado**: com `nr_estilo=2`, `nr_intensidade=0.55` e `nr_estrutura_pele=0.7` no nosso
ini, o app gravou `DirectNeuralRenderingStyle=2`, `DirectNeuralRenderingIntensity=0.55` e
`DirectNeuralRenderingSkinStructureStrength=0.7` em `[RENODX-DLSS-preset1]`, lendo
corretamente o valor anterior ("estava 1") na seção nova.

`[camada2_reshade]` do nosso ini agora aceita **`Secao/Chave=valor`** como escape para
qualquer chave que a tabela não conheça; sem barra, o nome antigo é traduzido.
Issue pública quase idêntica ao nosso caso, aberta e **sem resposta do autor**:
<https://github.com/clshortfuse/renodx/issues/651>

### Hook Method (`DirectNeuralRenderingHookPoint`) — valores medidos

Rótulos lidos do próprio binário: `0 Off`, `1 Auto`, `2 Upscaled`, `3 Hudless`, `4 Present`.
Só o zero desliga; os outros quatro são formas de ligar (por isso "ligado?" é `!= 0`, nunca
`== 1`). Ver o enum `RS_HOOK_*` em `main.cpp:3230`.

Medido no Witcher 3, RTX 4080, fonte janela 1920×1080:

| Modo | Avaliação | NGX evaluate | Captura |
|---|---|---|---|
| **`1` Auto** | `failed` nas duas variantes | **~950 ms/quadro** | 50,9 fps, com stalls — o jogo congela |
| **`2` Upscaled** | `succeeded`, `replace_source=true` | **~1 ms/quadro** | **60,0 fps, `stalls 0`** |

O `Auto` escolhe sozinho o caminho *presentation-backbuffer*: roda a rede sobre o quadro
**já apresentado**, depois de o feed ter feito o DLSS — sem movimento nem profundidade. O
`Upscaled` prende dentro do pipeline, onde o feed já está.

`Hudless (3)` não serve nesta cadeia: depende do Streamline, e o log de abertura diz
`Streamline interposer not found: sl.interposer.dll`.

Por isso `CorrigirHookPointNoArranque()` corrige **só o `Auto`** — que é padrão de fábrica,
escolha de ninguém. Quem está em `Off` fica em `Off`; quem escolheu `Upscaled`, `Hudless`
ou `Present` no overlay fica onde escolheu.

### O que NÃO está confirmado

A semântica numérica de `source=`, `Style`, `Encoding` e `UiCorrectionMode` **não tem fonte
pública**. Quem tentou documentar se recusou a publicar palpite. O `source=3 = dummy` que
circula na issue #651 é inferência do próprio relator, não rótulo decodificado.

O que se pode afirmar sobre o nosso caso **sem** decodificar o enum: rodamos com
`Require DLSS = On`, cujo gate "espera por inputs temporais DLSS SR/AA/RR correspondentes",
e ele **destravou** (`CreateFeature` e `EvaluateFeature` deram `succeeded`). Se destravou, é
porque casou com uma fonte DLSS temporal real — seja lá o que o inteiro `1` signifique, não
é o caminho de backbuffer com inputs falsos.

### `Reserved18`

O Neural Rendering usa o slot `NVSDK_NGX_Feature_Reserved18 = 18`, que **existe nos headers
oficiais** da NVIDIA (`feeder-amd/src-feeder/external/ngx/nvsdk_ngx_defs.h:224`) — é um slot
reservado, sem nome público, mas declarado. Os parâmetros (`DLSSNR.Color`, `DLSSNR.Output`,
`DLSSNR.MVec`, `DLSSNR.Depth`, `DLSSNR.Intensity`, `DLSSNR.Style`, …) foram extraídos do
binário do ShortFuse, não de documentação. **Sem contrato público: a NVIDIA pode mudar o que
há atrás desse slot num driver novo, sem aviso.**

---

## 6. Diagnóstico: os logs

| Log | Quem escreve | Para que serve |
|---|---|---|
| `dlss5forall.log` | o app | atalhos, estado do neural, camada, latência, regularidade |
| `dlss5-feed.log` | o feeder | DLSS, stalls **com atribuição de culpa**, sondagens de depth/MV |
| `ReShade.log` | ReShade + add-ons | carregamento, hooks NGX, DLSS-NR |
| `launcher.log` | o launcher | qual módulo abriu, idioma |

### Linhas de saúde

```
[RenoDX] DLSS-NR direct: CreateFeature(Reserved18) succeeded: ... preset=1
[RenoDX] DLSS-NR direct: EvaluateFeature succeeded: evaluation=1 result=0x00000001
RenoDX DLSS-NR source evaluation completed: source=1 ... replace_source=true
[feed] 600 frames: ... 16.67 ms (60.0 fps) ... stalls 0
LAT ritmo do WGC (intervalo entre FrameArrived): ... max 21.8 ms
neural: HookPoint=0 -> 2 (pela ponte, na config viva do ReShade)
```

### Linhas de doença

```
RenoDX DLSS-NR ... evaluation with retained DLSS temporal inputs failed; the original
                     backbuffer was retained.
RenoDX DLSS-NR ... evaluation with dummy temporal inputs failed; ...
[feed] STALL frame N: ... feed 953 ms (of which NGX evaluate 953 ms)
       -- most of it was INSIDE the NGX evaluate -- the neural consumer's detour
[feed] Depth probe: min 0, max 0, variance 0  <-- sampled depth is flat
LAT ritmo do WGC: ... max 462 ms
regularidade: ... p99 4250.1 ms
```

### Como ler o `STALL` do feed

O feed **atribui a culpa** sozinho, e essa frase economiza horas:

- *"most of it was INSIDE the NGX evaluate — the neural consumer's detour"* → o consumidor
  neural (RenoDX) está no meio da avaliação. Foi assim que o `Auto` foi pego.
- *"most of it was OUTSIDE this add-on entirely (game, driver, or another add-on's hooks)"*
  → a demora não é do feed. Quando isso coincide com `PokeReShade` no `dlss5forall.log`, a
  causa é o laço principal parado (ver §7).

### As linhas do app que mais dizem

| Linha | Significa |
|---|---|
| `neural: confirmacao alvo=A log=L card=C entregue=E` | `log=-1` = o log não fala do assunto (normal com o add-on novo) |
| `neural: Hook Method estava em Auto -> Upscaled` | a correção de arranque agiu |
| `PokeReShade vk=0x.. enviados=N/2` | `N<2` = a tecla não foi entregue |
| `interativo: sai por F7, sem prazo` | modo interativo sem volta automática (comportamento atual) |
| `regularidade: ... N acima de 120 ms` | engasgo perceptível; `0` é o valor saudável |

---

## 7. Armadilhas conhecidas

### `AttachThreadInput` congela o jogo

Anexar a fila de entrada da nossa thread à de **outro processo** faz das duas **uma** fila:
se o nosso lado demora, o outro congela junto. O sintoma é inconfundível — som continua
(outra thread), um quadro ou outro ainda sai, e a janela não responde.

Hoje o `PokeReShade` só faz isso quando a janela da frente é **do nosso próprio processo**
(`pidFg == meuPid`, `main.cpp:7593`). Nunca com o jogo. **Não reverta isso.**

### `PumpMessages(ms)` sempre gasta o tempo cheio

`main.cpp:7511` — o laço dorme de 4 em 4 ms até o prazo fechar; não sai antes mesmo sem
nada a fazer. `PumpMessages(45)` são 45 ms de laço principal parado, sempre.

O que faz o ReShade enxergar uma tecla é o **Present** (ele lê o teclado uma vez por quadro
apresentado), não o relógio. Por isso os `PumpMessages` entre os `PresentNow` foram zerados:
mesma garantia, sem os ~420 ms de parada por poke.

### `SendInput` é descartado pelo UIPI, em silêncio

Enquanto uma janela de integridade mais alta tem o foreground, o Windows joga o `SendInput`
fora — às vezes devolvendo sucesso. Foi isso que fez o toque no neural parecer "não
funciona" por muito tempo. `PostMessage`, que vai para a janela por handle, não tem esse
problema — daí a distinção `VIA_TECLADO` × `VIA_MENSAGEM` no `PokeReShade` (`main.cpp:7548`).

Vale também para automação de fora: cliques sintéticos no seletor do app são descartados.

### O modo interativo não volta sozinho

`ArmarVoltaInterativo()` (`main.cpp:2985`) — havia dois prazos (20 s de ócio, 45 s total) e
os dois expulsavam quem estava **lendo** o painel do ReShade, porque ler não gera entrada.
Hoje a saída é só pela tecla, e o que paga por isso é o **cartaz permanente** na tela
("aperte F7 para voltar ao jogo").

**Exceção deliberada:** sem nenhuma tecla livre (`g_saidaInterativo` vazio) os prazos voltam
a valer — é a única saída que existe, e um cartaz sem tecla a prometer prenderia a pessoa.

---

## 8. Pendências abertas

### Limitações estruturais — NÃO são pendências, e não têm conserto aqui

Estavam listadas como pendências (1) e (2) até 2026-09-08. **Não são defeitos: são consequência
do desenho da captura**, e tentar "consertá-las" no feeder é trabalho jogado fora. Dois agentes
chegaram a isso por caminhos independentes na mesma noite.

- **Depth constante.** O WGC entrega **cor, e só cor** — não existe depth do jogo para capturar.
  A camada desenha a imagem 2D sobre um quad plano com z fixo: `main.cpp:454` grava
  `float4(..., 0.5, 1.0)` e o comentário em `main.cpp:446` diz que é assim de propósito
  ("depth buffer plano, valido pro ReShade"). Com `DepthWriteMask = ALL` (`main.cpp:1186`), o
  depth é constante por construção. O probe do feed lê **0** e não 0,5 — prova de que o Generic
  Depth do ReShade nem chega a selecionar esse buffer nesta janela.
- **Jitter zero.** Jitter é deslocamento sub-pixel da matriz de projeção de uma cena 3D. Aqui não
  há projeção nenhuma: há uma imagem pronta desenhada num quad.
- **Consequência de tabela:** o gate de descontinuidade do `lumenite_Kernel.fx` depende de depth e
  fica preso em 1, e o `PS_ReconstructNormals` reconstrói normais de um plano — trabalho inútil
  todo quadro. `IMAGE_SPACE=1` nas `PreprocessorDefinitions` desliga os dois e troca o gate por
  concordância de fluxo. **Candidato não validado:** medido três vezes em 2026-09-08 e as três
  deram inconclusivo (cena parada, camada caindo, iluminação mudando). Avaliar jogando.
- **Achado colateral, não resolvido:** os motion vectors **colapsam em cena escura** — o
  `Lumenite_Kernel` estima fluxo óptico na imagem, e sem contraste não há o que casar. Medido:
  amostras zeradas com o personagem andando e a câmera girando, à noite. À noite a cadeia roda
  sem depth **e** sem MV. `mv_hardware=1` (o modo leve, pela fila de vídeo) pode não ter essa
  fraqueza — não testado.

### Latência — frente fechada

`docs/latencia.md` e `docs/latencia-nr.md` já esgotaram o assunto, e 2026-09-08 fechou o resto: o
código responde por **1,5 ms de ~16,8 ms** (9%); o `PRESENT->TELA` é ~1 refresh exato, metade
espera de fase (irredutível) e metade composição do DWM; o único caminho para essa metade
(*independent flip*, que pede janela sem borda cobrindo o monitor) **já foi medido e deu quase o
dobro do custo**. Os adaptadores virtuais foram desativados pelo usuário e não mudaram nada.


### A escala de resolução desligava o filtro (corrigido e medido em 2026-09-08)

Relato: *"qualquer alteração que eu faça que não deixe o DLSS 5 a 100% está desligando o
efeito"*. **Reproduzido, causa encontrada, corrigido, e provado no log.**

**A causa** estava em `dlss5-feed.cpp`, na graça que segura o rebuild do feature:

```cpp
if (g.frame_ready && needs_build11) g.create_grace = 0;          // ← re-armava TODO quadro
if (ok && needs_build11 && g.create_grace < g_cfg.create_delay) { ++g.create_grace; ok = false; }
```

Enquanto o mesmo build continuasse pendente, a primeira linha zerava o contador que a
segunda acabara de incrementar: `0 → 1 → 0 → 1`, para sempre. O build nunca acontecia, `ok`
ficava `false` pelo resto da sessão — e `ok=false` **pula o evaluate**, então a imagem
voltava a ser a captura crua. Não era o filtro "desligando": era o feeder preso.

Medido antes da correção, ao trocar 100% → 75% num ultrawide 3440x1440: a linha
`holding the feature (re)build` saiu **a cada 2 ms** até o processo ser morto.

**A correção** dá à graça uma *assinatura* do que está sendo construído (largura e altura de
trabalho, dimensões e formato do backbuffer, `want_sr`). Re-arma quando a assinatura muda —
uma resolução nova no meio da espera ganha graça nova — e deixa o contador subir enquanto o
build pendente for o mesmo. O log agora imprime `(n/N frames)`, porque uma graça parada era
indistinguível de uma graça lenta.

Depois da correção, o mesmo teste:

```
holding the feature (re)build (1/60 frames; ...)
holding the feature (re)build (60/60 frames; ...)
building: 2580x1080 work resolution (75%) -> 3440x1440 backbuffer B8G8R8A8_UNORM
feature ready: 2580x1080 DLAA, flags=66 ...
frame 3600 delivered (2580x1080 at 75% -> 3440x1440, reset=0)
```

**Por que só quebrava em tempo de execução:** na abertura `g.frame_ready` ainda é `false`, a
primeira linha não roda, e o contador sobe livre. Só depois do primeiro build — isto é, só
quando a pessoa mexe no painel End — o laço se fecha.

#### Duas hipóteses vizinhas que a medição REJEITOU

Ambas estavam na lista de suspeitos e **nenhuma se confirmou**. Ficam aqui para não serem
"corrigidas" de novo:

1. **`work_resolution`/`work_upscale` faltarem no conjunto `rebuild` de `CfgReload`** — não
   é defeito. `needs_build11` já detecta a mudança pela dimensão de trabalho, e a de
   `work_upscale` para 2 entra por `want_sr != g.sr_requested`. O teste acima passou sem
   tocar nesse conjunto. (`work_upscale` 0→1 de fato não dispara rebuild, e não deve: só
   troca o caminho do `BlitOutput`, que lê a chave a cada quadro.)

2. **Forçar `work_upscale=2` quando a escala for < 100%** — parecia óbvio (super-resolution
   real em vez de um blit bilinear), e **piorou**. Medido com o mesmo alvo estático (texto
   denso, mesmo enquadramento, 1485x912), escala 75%:

   | | nitidez (energia de gradiente na luminância) |
   |---|---|
   | `work_upscale=0` (DLAA menor + blit bilinear) | **15,58** |
   | `work_upscale=2` (DLSS SR, jitter Halton sintético) | 14,46 |

   Sete por cento pior, e visível no recorte ampliado: o SR **engrossa** as maiúsculas em
   vez de reconstruí-las. Coerente com as limitações estruturais da §8 — o SR depende de
   profundidade e vetores de movimento reais, e nesta cadeia a profundidade é constante e os
   MV são estimados na imagem. O padrão `work_upscale=0` está certo e **fica como está**.
### Pendências abertas de verdade

| # | Pendência | Onde | Impacto |
|---|---|---|---|
| 3 | ~~**Liga/desliga entre sessões**~~ — RESOLVIDO em 2026-09-08: o `Off` gravado pelo nosso F6 deixa a marca `neural_off_nosso` no `dlss5forall.ini` e é restaurado no arranque; o `Off` escolhido no overlay não deixa marca e é respeitado | `main.cpp` §3 | — |
| 4 | **Índice fora dos limites** — `camadaEstado[3]` num array de 3 posições | `main.cpp:8786` | escrita fora do array; o compilador acusa |
| 5 | ~~**Vitrine não acompanha o idioma**~~ — RESOLVIDO em 2026-09-08: `VitrineHost::TrocarIdioma()` (virtual, padrão vazio) em `splash_ofertas.h`, implementado no `vitrine::Motor` (troca só o valor de `l=` na query e renavega) e chamado no `aoTrocarIdioma` do `launcher.cpp`. **Falta teste manual de clique** — o clique sintético não chega à janela neste ambiente | `vitrine.h`, `launcher.cpp` | — |
| 6 | **Publi não acompanha o idioma** — o texto do `ofertas.json` é escolhido no download | `splash_ofertas.h` | trocar o idioma depois não rebusca a rede |
| 7 | **Passes próprios no feeder** — `Reserved18` dentro do `dlss5-feed`. Ver §10 | `feeder-amd/` | criação **PROVADA**; falta avaliação e loop de passes |
| 8 | ~~**Perfil de qualidade do NR inerte**~~ — RESOLVIDO em 2026-09-08: `NRStyle`, `NRIntensity`, `NRLocalTone`, `NRSkinStructure` e companhia escreviam em `[RenoDX.DLSS5]`, que o add-on novo não lê. Tabela de tradução `kRsChavesNr` + `RsGravarNr()`/`RsLerNr()`. Ver §5 | `main.cpp` | — |
| 9 | ~~**Escala de resolução desligava o filtro**~~ — RESOLVIDO em 2026-09-08: a graça do rebuild re-armava a cada quadro e o build nunca acontecia. Assinatura de build em `dlss5-feed.cpp`. Ver §8 | `dlss5-feed.cpp` | — |
| 10 | **BOM no `dlss5forall.ini` cria uma segunda seção `[ultima]`** — encontrado em 2026-09-08 numa cópia de teste: com os três bytes `EF BB BF` na frente, a API do Windows não encontra a `[ultima]` original e **cria outra no fim do arquivo**. Todas as escolhas gravadas a partir daí ficam invisíveis. O app remove o BOM na abertura, mas o duplicado que já nasceu não é limpo | `main.cpp` (`IniPath`) | escolhas perdidas em silêncio; a seção órfã fica para sempre |

---

## 9. Como manter este documento

- Mudou um contrato entre peças (protocolo da ponte, chave de ini, quem lê quando)?
  Atualize **§3 ou §4**, que são as seções que outra pessoa vai consultar antes de mexer.
- Descobriu um número novo por medição? Ponha em **§5** com o cenário (jogo, placa,
  resolução) — número sem cenário não serve para comparar depois.
- Resolveu uma pendência? Tire de **§8** e, se virou regra, promova para **§7**.
- Encontrou uma armadilha que custou tempo? **§7**, com a causa. É a seção mais barata de
  escrever e a que mais economiza depois.

### O que este documento não cobre

Interface e comportamento do painel (`docs/painel.md`), cor/HDR (`docs/hdr.md`,
`docs/cor-hdr-e-filtros.md`), motion vectors (`docs/motion-vectors.md`), nitidez
(`docs/borrao-e-nitidez.md`), latência (`docs/latencia.md`, `docs/latencia-nr.md`),
entrada (`docs/correcoes-entrada.md`) e o neural sem teclado
(`docs/neural-sem-teclado.md` — **parcialmente obsoleto**: descreve o hook de IAT do
RenoDX antigo, que não existe mais no add-on novo).

---

## 10. Neural Rendering próprio (`Reserved18`) — estado em 2026-09-08

A criação da feature de Neural Rendering **dentro do nosso feed funciona**. Falta a avaliação
e o loop de passes. Esta seção existe para que quem retomar não redescubra nada disto.

### Como o NR se monta (extraído do desmonte do `renodx-dlss.addon64`)

O `nvngx_dlssnr.dll` **não é um pacote de pesos: é uma runtime NGX completa.** Ele exporta
`NVSDK_NGX_D3D12_Init_Ext`, `_CreateFeature`, `_EvaluateFeature`, `_ReleaseFeature`,
`_Shutdown1`. O que ele NAO exporta é `AllocateParameters`/`DestroyParameters` — daí o
desenho ser híbrido:

> **Mapa de parâmetros do DRIVER (`_nvngx.dll`), `CreateFeature` do SNIPPET.**
> O único objeto que atravessa a fronteira é o `NVSDK_NGX_Parameter*`.

A sequência, na ordem, toda obrigatória:

1. `LoadLibraryW("nvngx_dlssnr.dll")` + `GetProcAddress` dos cinco exports de ciclo de vida.
2. **Spoof de identidade do chamador.** O `Init_Ext` do snippet pega o endereço de retorno,
   descobre o módulo chamador com `GetModuleHandleExA(FROM_ADDRESS)`, lê o nome com
   `GetModuleFileNameW` e exige que contenha `"nvngx.dll"`; se não, `0xBAD00002`. O RenoDX
   satisfaz isso hookando a IAT do snippet. **Todo o ciclo que fala com o snippet precisa
   correr sob o spoof — inclusive o `Shutdown1`**, ou ele falha com o mesmo erro e a
   instância fica aberta enquanto o `FreeLibrary` descarrega o módulo por baixo dela.
3. Preparar `%TEMP%\RenoDX\dlssnr` (os dois componentes são strings **wide** no binário).
4. `_nvngx.dll`: `NVSDK_NGX_D3D12_Init(AppId=0x876232C, <o diretório do passo 3>, device, 0x15)`.
   Quatro argumentos, não cinco — a variante `NGX_SNIPPET_BUILD` do header. (O feed usa a de
   cinco no caminho do SR, o que na ABI real entrega `Version = 0`; funciona, e **não foi
   mexido**, porque o SR depende disso.)
5. `nvngx_dlssnr.dll`: `Init_Ext(AppId=0x876232C, <o diretório do DLL>, device, 0x15, NULL)`.
   Repare que o caminho aqui é **outro** — é o do DLL, não o de dados. É intencional no
   desenho dele.
6. `_nvngx.dll`: `AllocateParameters`, os ~20 `Set` de `DLSSNR.*`, e então
   `CreateFeature(cmdlist, Reserved18, params, &handle)` **do snippet**.
   `PerfQualityValue = 5` (DLAA) — o "performance=6" que aparece no log do RenoDX é a
   contagem 1-based da interface dele; o binário faz `dec` antes de passar ao NGX.

### A regra que organiza tudo: uma instância NGX por DEVICE

Do header oficial (`nvsdk_ngx.h:263-264`): *"If a device is provided, `NVSDK_NGX_Shutdown1`
shuts down the instance corresponding to the specified device. If nullptr is provided, all
instances are shut down."* O argumento decisivo é a ausência — `Shutdown1` recebe **só** um
device, sem AppId nem handle de sessão. Se dois `Init` no mesmo device pudessem produzir duas
instâncias, ele seria ambíguo.

**`Shutdown1(nullptr)` derruba o NGX de todo o processo. Nunca chamar assim.**

**Decisão de arquitetura — EM REVISÃO (2026-09-08, 03:00).** A recomendação inicial foi um `ID3D12Device` PRIVADO, no mesmo
adaptador (achado por LUID), com fila, allocator, lista e fence próprios. Não porque o
`g.dev12` compartilhado tenha falhado — ele funcionou nos testes —, mas por três razões:

- compartilhar só é seguro enquanto **nenhum outro** consumidor NGX existir no processo, e
  isso é condição sobre a máquina do usuário, não sobre o nosso código (o feed já loga por
  `Alex's Toolkit` e `Deep Fried Chicken`, e o DFC arma detours NGX);
- o nosso `Init` deixaria a instância do `g.dev12` com o `ApplicationId` e o data path do NR
  **valendo para o SuperSampling também** — o AppId governa overrides de perfil do driver.
  Não crasha, não aparece no fps, ninguém descobre;
- com device próprio o `Shutdown1` volta a ser seguro, e é isso que torna o NR **desligável e
  recriável** — pré-requisito para os passes responderem em tempo de execução.

O preço é a travessia das texturas (Color/Depth/MV/Output pertencem ao `g.dev12`). **MEDIR
antes de projetar**: o feed inteiro custa 0,92 ms/frame hoje.

### O "crash" de 02:29 — o que era e o que não era

Sintoma: `evaluate raised exception 0xC0000005` no `EvaluateFeature` do SuperSampling, logo
após o `CreateFeature(Reserved18)` do spike.

**Experimento controlado:** mesmo binário, mesma sequência, `renodx-dlss.addon64` renomeado
para fora da pasta → **sem crash, 1800 quadros a 60 fps, `stalls 0`**. A única variável era o
RenoDX. Isso mata toda hipótese cuja causa não passe por ele: o segundo `Init` no core, o
`Init_Ext`, o `AllocateParameters`, a command list compartilhada, a tabela de handles.

O mecanismo é o detour: o RenoDX hooka `NVSDK_NGX_D3D12_EvaluateFeature` (`ReShade.log:143`)
e monta a sessão de NR dele **preguiçosamente, dentro do evaluate do SR**. O nosso spike roda
na janela entre o create do SR e o primeiro evaluate — e usa **os identificadores dele**
(mesmo AppId, mesmo `%TEMP%\RenoDX\dlssnr`, mesmo device, mesmo DLL), monta e desmonta. Ele
então encontra o terreno alterado. Como o objetivo é remover o RenoDX da pasta, **o conflito
desaparece por construção**.

Duas coisas ficaram por verificar, e valem um minuto cada quando alguém retomar:

- Ele também hooka `NVSDK_NGX_D3D12_Init` (`ReShade.log:132`). Se for detour **inline no
  export**, a nossa chamada do passo 4 atravessou o hook dele e foi registrada como sendo
  dele. Teste: logar os 16 primeiros bytes de `GetProcAddress(core, "NVSDK_NGX_D3D12_Init")`
  — `E9`/`FF 25` denuncia inline; um prólogo normal diz que escapamos.
- **Talvez não tenha havido corrupção nenhuma.** O nosso guard instala
  `AddVectoredExceptionHandler(1, ...)` (`feed_seh_shim.h:87`), prioridade máxima, e o detour
  do RenoDX roda **dentro** do nosso `FEED_GUARD_ARM`. Uma exceção que ele trataria sozinho
  pode ter sido interceptada por nós primeiro, e lida como "o meu evaluate crashou".

### Revisão da decisão (2026-09-08, 03:00) — o `Init` do core pode ser dispensável

Depois de a decisão acima ser tomada, apareceu evidência que a coloca em xeque, e ela é
direta: **`objdump -p nvngx_dlssnr.dll` mostra que o snippet importa apenas `VERSION`,
`ADVAPI32`, `USER32` e `KERNEL32`.** Nada de `nvngx.dll`/`_nvngx.dll`. Em 16 MB de binário a
string wide `nvngx` aparece duas vezes: o nome do arquivo de log e a *needle* da verificação
de identidade. **O snippet não fala com o core.**

Se isso se confirmar, o `Init` do core que fazíamos no `g.dev12` é dispensável — quem
destravou o `Init_Ext` foi o spoof do `GetModuleFileNameW`, não o `Init`. Os dois entraram em
rodadas diferentes e nunca foram testados separadamente. E aí o desenho "mesmo device" **não
faz nenhum `Init` no `g.dev12`**: sem segundo `Init`, não há reconfiguração, e a objeção da
bomba-relógio desaparece — junto com a parede do cross-device.

Duas observações que sustentam esse lado:

- **A recriação do SR já foi exercitada sem estrago.** O feed recria a feature sozinho
  (`warmup_rebuild=180`). As quatro janelas de 600 quadros do run limpo passaram do quadro
  180, ou seja o SR foi **recriado com o AppId e o data path do NR já trocados na sessão**, e
  sobreviveu. Recriação era justamente o cenário onde se esperaria o dano.
- **O custo do device privado não é cópia por quadro** (correção de um número que circulou
  antes): devices no mesmo adaptador compartilham a mesma memória física via
  `CreateSharedHandle`/`OpenSharedHandle`. O custo real é que as texturas do caminho quente
  (`g.tex12[]`: Color, Output, Depth, MV, Mask) teriam de nascer com `D3D12_HEAP_FLAG_SHARED`
  — hoje nascem com `HEAP_FLAG_NONE` — o que mexe no caminho quente do SR.

**O teste que decide, e é de uma linha:** comentar o `core Init`, manter o spoof, e ver se o
`Init_Ext` ainda devolve `Success`. Rodar com e sem o `renodx-dlss.addon64` na pasta. Enquanto
esse teste não for feito, **nenhum dos dois desenhos está decidido** — o device privado
continua implementado e preservado no patch, para não se perder.

Regras que valem em qualquer um dos dois desenhos:

- **Nunca chamar o `Shutdown1` do core.** A instância do core é do feed, e ele já a desliga
  no teardown dele (`dlss5-feed.cpp:2415`). Não inicializamos, não desligamos.
- **`Shutdown1(nullptr)` é proibido** — derruba o NGX de todo o processo.
- O `Shutdown1` **do snippet** é outra tabela, de outro módulo, e é legítimo — desde que
  chamado **sob o spoof**, senão falha na verificação de identidade e deixa a instância aberta.

### DECISÃO FECHADA (2026-09-08, 03:00) — device do feed, sem `Init` no core

O teste foi feito e **o snippet é autocontido**. Com o `Init` do core pulado:

```
nr_spike: core Init SKIPPED
nr_spike: snippet Init_Ext(...)     -> 0x00000001 (Success)
nr_spike: CreateFeature(Reserved18) -> succeeded, handle valido
nr_spike: snippet Shutdown1         -> 0x00000001 (Success)
[feed] 600 frames: 60,0 fps | stalls 0     (x3 janelas)
```

**O desenho é: device e command list do próprio feed, NENHUM `Init` no core.** As duas
objeções que tinham levado ao device privado caem:

- *"o `Init` contaminaria o `ApplicationId` do SuperSampling"* — **não há mais `Init`**;
- *"o `Shutdown1` seria inseguro"* — ele **passa**, e o ciclo fica simétrico. Isso é o que
  torna o NR desligável e recriável, pré-requisito para trocar a contagem de passes em
  tempo de execução.

E de brinde: sem cross-device, sem `D3D12_HEAP_FLAG_SHARED` nas texturas do caminho quente.

> ### ⚠️ O "device privado" (`nr_spike=2`) NÃO EXISTE — não use, e remova
>
> Provado fora do jogo, por teste de API pura (`feeder-amd/tests/nr_device_probe.cpp`):
> **o runtime D3D12 recicla o device por adaptador.** `D3D12CreateDevice` com o mesmo
> adaptador devolve `S_OK` e **o mesmo objeto**, com o refcount incrementado — inclusive
> quando o `IDXGIAdapter1` passado é outro ponteiro (`EnumAdapterByLuid` devolveu outro
> adaptador e o device saiu idêntico). `QueryInterface(IID_IUnknown)` confirma a identidade
> COM. E a LUID `HighPart=0 LowPart=0x13408` é o valor real e completo — LUIDs de GPU têm a
> metade alta zerada; não estava truncada.
>
> Consequências: (a) nunca houve isolamento — o `Init_Ext` do "privado" e o do feed sempre
> estiveram na **mesma instância**; (b) `NrDestroyDevice()` faz `Release()` no device do
> **feed**, soltando uma referência que não criou de forma independente.
>
> **Isolamento real, se um dia for preciso, não sai de `D3D12CreateDevice`** — teria de ser
> outro adaptador, ou outro processo com recursos compartilhados por handle NT. Outra
> conversa, e bem maior.

`nr_spike=1` é o desenho decidido, e é o único que existe de verdade.

Timings de setup (pago uma vez, não por quadro):
`attach 0,25–1,4 · init_ext ~131–146 · create ~175–188 · teardown 6–52 · total ~340–360 ms`

**O que NÃO foi resolvido, e não precisa ser:** com o `renodx-dlss.addon64` na pasta, o
`EvaluateFeature` do SR ainda estoura depois do nosso create. Testado: sem ele, tudo limpo;
com ele, crash. Não é o `Init` hookado (pulá-lo não mudou nada) — é a montagem preguiçosa
dele no detour, encontrando o terreno usado pelos mesmos identificadores. **Como o destino é
remover o RenoDX, o conflito desaparece por construção.** Não gastar tempo com isso.

### O caminho daqui

1. Avaliação: alimentar `DLSSNR.Color`, `.Output`, `.MVec`, `.Depth`, jitter e chamar
   `EvaluateFeature` do snippet. Tudo no device do feed, onde as texturas já estão.
2. Loop de passes: encadear saída→entrada, 1 a 10. Preparação e composição rodam uma vez;
   só a avaliação repete (~4 ms de GPU por passe extra, medido com o add-on de terceiros).
3. Remover o `renodx-dlss.addon64` do pacote e ligar o NR próprio.
4. Trocar os chips "DLSS 2x/3x" do painel pelos botões de 1 a 10 passes, mostrando o custo
   em função do pulo de quadro (ver §5 — o pulo é o que compra orçamento para passes altos).

### FUNCIONA — avaliação e passes medidos (2026-09-08, 03:30)

O Neural Rendering próprio **cria, avalia e encadeia passes**, sem o `renodx-dlss.addon64`
na pasta. Medido no Witcher 3, 1920x1080, RTX 4080:

| `nr_passes` | custo do NR | fps | stalls |
|---|---|---|---|
| 1 | 0,49 ms/quadro (0,49 ms/passe) | **60,0** | 0 |
| 4 | 1,38 ms/quadro (0,35 ms/passe) | **60,0** | 0 |
| 10 | 3,42 ms/quadro (0,34 ms/passe) | 20,1 | 512 |

**Contra o add-on de terceiros, na mesma máquina e cena:** ele custava ~4 ms/passe e 4 passes
derrubavam para 34,6 fps. O nosso custa **0,35 ms/passe** e segura **60,0 fps** com 4 —
**onze vezes mais barato por passe**.

Em 10 passes o custo de CPU continua baixo (3,42 ms) mas o quadro vai a 49,7 ms: **satura a
GPU**. O limite prático está entre 4 e 10 — falta medir 6 e 8 para ter a curva. Lembrar que
o **pulo de quadro dobra ou triplica o orçamento** (§5): 1-de-cada-2 dá ~33 ms por quadro.

Setup, pago uma vez: `attach 2,1 · init_ext 164,9 · create 211,3 · total ~380 ms`.

**A imagem chega à tela e está correta.** Comparação de pixels com o jogo PAUSADO (a primeira
medição foi invalidada pelo ciclo dia/noite trocando a iluminação entre as capturas — erro
que já custou três medições nesta árvore, ver §6):

```
ruído da cena         14,7 / 765
efeito do NR próprio  42,4 / 765   em 77,6% dos pixels     (~3x o ruído)
```

Inspeção visual: cores naturais, detalhe preservado, sem artefato, banda ou fantasma.

### Como está implementado

- `NrEnsure(w,h)` monta uma vez; handle, mapa e texturas **vivem entre quadros**.
- `NrEvaluateFrame()` roda depois do evaluate do SR, **na mesma command list**, e só se o
  evaluate do SR não falhou (quadro que vai ser descartado não merece a cascata).
- **Barreiras:** o add-on de terceiros só barreira o recurso de **saída** (duas, em
  `0x18007676b` e `0x180076e2f`) — presume Color/Depth/MVec já em estado de leitura, que é a
  situação no nosso caminho. O nosso precisa de **uma a mais**, para virar o `SLOT_OUTPUT`
  recém-escrito em UAV para leitura no passe 1.
- **Passes:** ping-pong entre duas texturas em `g.output_fmt`; fonte preparada uma vez,
  composição uma vez, só a avaliação repete. `Reset` só no primeiro passe do quadro.
- **Falha ⇒ o quadro continua:** exceção ou `NVSDK_NGX_FAILED` desliga o NR, devolve o
  `SLOT_OUTPUT` ao estado esperado e o feed entrega a imagem do SR. O feed nunca para.

### O que falta

1. **`FeedFrame12`** — o caminho D3D12 nativo ficou de fora (ele usa os recursos do próprio
   ReShade, não `g.tex12[]`). Não afeta a camada do app, que é D3D11; afeta jogo D3D12 nativo,
   onde o NR simplesmente não age, sem erro e sem log.
2. Medir 6 e 8 passes para a curva de custo.
3. Painel: trocar os chips "DLSS 2x/3x" pelos botões 1..10, mostrando o custo em função do
   pulo de quadro.
4. Remover o `renodx-dlss.addon64` do pacote e ligar o NR próprio por padrão.

### CORREÇÃO da tabela acima (2026-09-08, 03:35) — o "11x mais barato" era falso

A comparação registrada acima **misturava métricas** e foi retirada. O que aconteceu:

- O contador `ms/pass` do feed mede **CPU** — o tempo de gravar os `Set`, as barreiras e as
  chamadas na command list, que retornam assim que o comando é enfileirado. **O trabalho da
  rede acontece na GPU depois, e essa métrica não o vê.** A prova está nos próprios dados:
  10 passes deram 3,42 ms de CPU dentro de um quadro de 49,7 ms — os 46 ms restantes são GPU.
- O número do add-on de terceiros (~4 ms/passe) tinha sido derivado do **fps**, ou seja, de
  GPU. Comparar um com o outro não significa nada.

**A curva de GPU, medida em sequência na mesma cena (6, 8 e 10 passes):**

| passes | fps | quadro |
|---|---|---|
| 6 | 30,5 | 32,78 ms |
| 8 | 23,8 | 42,05 ms |
| 10 | 20,1 | 49,71 ms |

De 6→8: +9,3 ms para 2 passes. De 8→10: +7,7 ms. **~4,2 ms de GPU por passe** — praticamente
o mesmo custo do add-on de terceiros. **Não há ganho de uma ordem de grandeza.**

Os pontos de 1 e 4 passes (60,0 fps) foram medidos **em outra cena** e não fecham com essa
curva — a 4,2 ms/passe, 4 passes dariam ~41 fps, não 60. A carga de GPU depende do que está
na tela, então pontos medidos em cenas diferentes não formam curva. Ficam como registro de
que o caminho funciona, não como medida de custo.

**O que o NR próprio ganha de verdade** (e continua valendo a pena):
- **integração**: liga/desliga e contagem de passes sob nosso controle, em tempo de execução
  — o add-on de terceiros só lê o modo no carregamento;
- **um add-on a menos** disputando o mesmo NGX, que foi a origem de toda a noite;
- **independência** de um binário de terceiros que muda de interface sem aviso;
- **custo de CPU baixo e conhecido**: ~0,19 ms fixos + ~0,30 ms por passe.

**Para a interface**, o número que vale é o **fps**, não o `ms/frame` do log. E a medição de
custo tem de ser feita na mesma cena, ponto a ponto — foi o erro que produziu a tabela errada.

---

## 11. Sessão de 2026-09-08 (tarde) — o painel ganhou o perfil da rede

### Os chips "1x 2x 3x" saíram do painel

O controle abria uma **segunda instância inteira do app** por camada, cada uma com o seu
ReShade, o seu Feeder e a sua rede, todas disputando a mesma placa. Derrubou a máquina e o
jogo três vezes nos testes. Retirado a pedido do usuário.

**Só a interface saiu.** `CamadaIniciar`, `E_CAMADAS`, `CMD_CAMADAS_BASE` e `--camada N`
continuam inteiros — duas camadas ainda rodam pela linha de comando ou por `[ultima]
dlss_camadas`. A decisão foi sobre o botão, não sobre o recurso.

### O perfil de imagem da rede, no painel End

Seis controles em degraus (`kCtrlNr` em `main.cpp`) mais os passes de 1 a 10:

| controle | chave no add-on novo | seção |
|---|---|---|
| MODELO (A/B/C) | `DirectNeuralRenderingStyle` | `[RENODX-DLSS-preset1]` |
| INTENSIDADE | `DirectNeuralRenderingIntensity` | idem |
| ESTRUTURA | `DirectNeuralRenderingLocalStructureStrength` | idem |
| PELE E ROSTOS | `DirectNeuralRenderingSkinStructureStrength` | idem |
| TOM LOCAL | `DirectNeuralRenderingLocalToneStrength` | idem |
| TOM GLOBAL | `DirectNeuralRenderingGlobalToneStrength` | idem |
| PASSES (1..10) | `DirectNeuralRenderingPassCount` | idem |

Cada clique grava em **dois destinos**: `[ultima] nr_*` no nosso ini (a memória, que
sobrevive ao AutoSave do ReShade) e a config **viva** do ReShade pela ponte (o que faz valer
agora). Ver `AplicarAjusteNr()` e `AplicarPassesNr()`.

**Verificado com clique real** (não só compilado): `perfil da rede:
DirectNeuralRenderingLocalToneStrength = 0.75 (pela ponte, vale agora)` e `passes = 6 (pela
ponte, vale agora)`, com os valores aparecendo em `[RENODX-DLSS-preset1]` e em `[ultima]`.

### A ponte foi ao protocolo 4: `SetConfigFloat`

`set_config_value` do ReShade é **tipado**. O perfil do RenoDX novo é float normalizado de 0
a 1 — gravar `0.55` pelo caminho `<int>` guarda `0` e apaga o ajuste, sem erro nenhum. Com só
o int a gente alcançava `HookPoint`, `Encoding` e `Style`, e mais nada.
`DLSS5_BRIDGE_CMD_SET_CONFIG_FLOAT 8u`, handler em `dlss5_bridge_addon.cpp`.

### Duas medidas de layout que vieram de erro real

- O painel foi a **1486 px** de altura com rótulo, chips e frase de ajuda em três linhas por
  controle — mais que os 1440 do monitor do autor, cortando o fim do painel. Com rótulo e
  chips na mesma linha são 38 px por controle e o painel fica em **1271 px**.
- A frase dos passes dizia `teto ~238 fps` com um passe. Número que o programa não entrega
  nem promete: o passe é **uma** das contas do quadro (há a captura, o DLSS do Feeder, o SMAA
  e a apresentação). Passou a dizer quanto **custa** (`~4,2 ms de placa por passe · N = ~X ms
  por quadro`), que é verdade.

### O TDR da troca de resolução

Ver §8. A troca agora desliga o neural pela ponte durante a reconstrução e religa em 4 s
(`ProtegerTrocaDeResolucao` / `ReligarNeuralSePendente`), e GPU morta deixou de queimar as
três tentativas do `FeedFail` — passa a dizer `the graphics device is gone` e mandar reabrir.

### A sincronização da camada intermitente, e uma leitura que se corrigiu

O log de 16:17 mostrava `esperou a tela 3, estourou o prazo 146` — 97% das trocas sem
confirmação. A conclusão na hora foi "a sincronização não funciona", e o `DwmGetComposition
TimingInfo` entrou como fonte alternativa (o DXGI não vê janela *layered*; quem a põe na tela
é o compositor).

**Mas o teste seguinte, com a GPU sadia, deu `esperou a tela 71 (dxgi 71, dwm 0), estourou o
prazo 4`** — 95% confirmado, e **pelo DXGI**, cujo caminho não foi tocado. Ou seja: o "97%
estourou" era sintoma da **placa morrendo** (era o mesmo minuto do TDR), não defeito da
sincronia. O caminho pelo DWM fica como rede de segurança legítima, mas **não foi ele que
consertou nada** — e os contadores separados (`dxgi X, dwm Y`) existem justamente para essa
distinção não se perder de novo.

Consequência para o piscar: com 95% das trocas sincronizadas e o piscar continuando, a causa
é a **diferença entre as duas imagens** (a processada e o jogo cru), não o momento da troca.

### Diagnóstico: o retrato da queda e três gerações de log

Quando a fonte cai, o log passa a gravar `ESTADO NO MOMENTO EM QUE A FONTE CAIU` com
resolução, passes, neural, intermitente, pulo, camadas e o perfil inteiro. E a rotação foi de
uma para **três** gerações (`.1 .2 .3`): com um `.1` só, três aberturas seguidas apagam a
evidência da primeira — foi o que aconteceu investigando o travamento do Witcher de 08/09,
em que as aberturas de teste empurraram para fora justamente o log da sessão que caiu.

### A sincronização da camada: o erro, a correção e a medição

Primeira tentativa: perguntar ao DWM por `cFrameDisplayed` do `DWM_TIMING_INFO`, esperando
"o meu quadro apareceu". **Esse campo não é preenchido no Windows atual.** Medido com um
programa de quatro linhas (`scratchpad/dwmtest.cpp`): a chamada devolve `S_OK`, `cFrame` anda
(1, 2, 3, 4, 5…) e `cFrameDisplayed` fica em **zero para sempre**. A condição
`cFrameDisplayed >= alvo` nunca era verdadeira — o caminho inteiro era código morto, e os
contadores do log diziam isso em toda medição (`dwm 0`) sem que ninguém lesse.

O que funciona, medido no mesmo programa: `qpcVBlank` e `qpcRefreshPeriod` (o período bateu
com os 100 Hz do monitor). A correção espera **o próximo vblank** depois do Present: quando
ele avança, passou um refresh inteiro e o quadro entregue ao compositor teve a vez dele.

Resultado, com a fonte em movimento e a camada em sobreposição:

```
antes:  esperou a tela  0 (dxgi  0, dwm  0), estourou o prazo 4    ← nada sincronizava
depois: esperou a tela 26 (dxgi  0, dwm 26), estourou o prazo 0    ← 100%
```

E o `dxgi 0` confirma a hipótese: **no modo sobreposição o DXGI não enxerga a janela
layered**. Quem sabe quando ela virou pixel é o compositor, e só ele.

---

## 12. Piscar do frame skipping: fluxo óptico **sim**, frame generation **não**

O usuário propôs duas saídas para o piscar: fluxo óptico entre os quadros pulados, ou frame
generation na camada. A investigação (binários, DLLs do sistema e sondas compiladas nesta
máquina) responde de forma definitiva.

| caminho | status | evidência |
|---|---|---|
| Frame gen do RenoDX | **não gera quadro nenhum** | `FrameGenerationPresentationPath` é só *ordem de composição*: "Selects a startup presentation topology"; opções `ReShade before Frame Generation` / `ReShade on generated output`. Ele *hooka* um FG que o **jogo** já tenha. |
| DLSS-G nativo | **bloqueado** | `sl.interposer.dll`, `sl.dlss_g.dll`, `sl.common.dll` não existem no sistema nem no projeto. `ReShade.log`: `Streamline interposer not found`. E exige depth + MV válidos, que aqui são zero. |
| FSR Frame Generation | **bloqueado pela arquitetura** | O `amd_fidelityfx_dx12.dll` do projeto *tem* `ffxProvider_FrameGenerationSwapChain_DX12`, mas FG da AMD precisa **envelopar a swapchain** — e `main.cpp` já registra que recriar a swapchain mata a cadeia NGX (ver `FonteMudou`). |
| **Optical Flow da NVIDIA** | **DISPONÍVEL** | `C:\Windows\System32\NvOFAPI64.dll`, v32.0.15.9649, exporta `NvOFAPICreateInstanceD3D11` **e** `NvOFAPICreateInstanceD3D12`. A RTX 4080 tem OFA em hardware. |
| Video Processor do DirectX | **indisponível nas duas GPUs** | Sonda compilada (`scratchpad/vpprobe.cpp`): NVIDIA `ProcessorCaps=0x17` — o bit `FRAME_RATE_CONVERSION` (0x20) **não está setado**. AMD tem FRC (0x20) mas `PastFrames=0 FutureFrames=0` — sem quadro futuro não há interpolação. |

### Por que o fluxo óptico é o caminho, e não o frame gen

O motivo é estrutural: **NvOFAPI recebe duas imagens de COR e devolve um campo de fluxo.**
Não pede profundidade, não pede vetores de movimento do motor, não pede HUDless, não pede
Streamline. É exatamente o formato do que a WGC entrega — que é a limitação que derruba todos
os outros caminhos.

**Ganho duplo:** o mesmo campo serve para preencher `SLOT_MV` (`dlss5-feed.cpp`,
`ep.pInMotionVectors`), que hoje colapsa (`MV probe: 0% non-zero`). O trabalho conserta o
piscar **e** dá ao DLSS vetores reais no lugar dos estimados pelo `Lumenite_Kernel`.

**O que falta:** os headers do NVIDIA Optical Flow SDK (`nvOpticalFlowCommon.h`,
`nvOpticalFlowD3D12.h` — públicos, e a DLL já está na máquina) e um shader de *warp* que
sintetize o quadro intermediário, para a camada **desenhar** nos quadros pulados em vez de
baixar o alpha para zero.

**Ressalva:** NvOFAPI é NVIDIA-only. Numa máquina só-AMD o caminho seria o fluxo óptico
interno do FSR (os parâmetros `opticalFlow*` estão dentro do `amd_fidelityfx_dx12.dll`, mas a
ffx-api não os expõe como efeito isolado) ou seguir no `Lumenite_Kernel` em compute.

### E por que nenhuma sincronização sozinha resolveria

Com a troca 100% sincronizada (acima) e o piscar continuando, a causa é o que sobra: a camada
alterna entre **duas imagens diferentes** — a processada e o jogo cru. Isso é flicker por
construção, e não some ajustando o instante da troca. Só some quando os quadros pulados
passarem a mostrar algo parecido com o processado, que é precisamente o que o fluxo óptico
permite sintetizar.

### Prova de vida do Optical Flow (2026-09-08) — o caminho existe

`tests/ofa_probe.cpp`, compilado e executado nesta máquina:

```
LoadLibrary NvOFAPI64.dll -> 00007ffe8c110000
versao maxima suportada: status=0  version=0x00000050
NvOFAPICreateInstanceD3D11: status=0  ponteiros preenchidos=9
```

**Nove ponteiros de função preenchidos, sem o SDK instalado** — só com a DLL que já vem no
driver. Duas armadilhas custaram tentativas e ficam registradas:

1. A *function list* **não** tem campo de versão. A assinatura real é
   `NvOFAPICreateInstanceD3D11(uint32_t apiVer, NV_OF_D3D11_API_FUNCTION_LIST*)` — a versão é
   o **primeiro parâmetro**, e o device só entra depois, no `nvOFInit`.
2. Passar o device onde ele espera o `uint32` da versão devolve **status 7**
   (`NV_OF_ERR_INVALID_VERSION`), que parece "versão errada" e manda procurar no lugar errado.

O que falta para o recurso: criar a sessão (`nvOFInit` com os parâmetros de grid e preset),
os buffers de entrada/saída, a execução por quadro, e o shader de *warp* que usa o campo de
fluxo para sintetizar o quadro intermediário. A partir daí a camada **desenha** nos quadros
pulados em vez de baixar o alpha para zero — e o piscar deixa de existir por construção.

### O modo `segurar`, e por que "frame gen" e "fluxo óptico" são a mesma coisa

`[ultima] alterna_modo` agora aceita três valores: `alfa` (padrão), `regiao` e **`segurar`**.

No `segurar` a camada **nunca fica transparente**: o quadro pulado simplesmente não é
reapresentado, e o compositor continua mostrando o último quadro processado. A economia é
idêntica — quem a produz é o `alternaOk`, que já pula o Present (e com ele o filtro); o modo
só muda **o que aparece** no lugar.

Isso elimina o piscar por construção: nos outros dois modos a tela alterna entre **duas
imagens diferentes** (a processada e o jogo cru), e isso é flicker que não vem do *instante*
da troca — com a sincronia em 100% e zero estouros ele continuaria. Vem da diferença entre as
imagens. Segurando o último processado, as duas passam a ser a mesma.

**O que se paga:** judder. O movimento anda na metade da taxa, porque um quadro em cada dois
é repetição. E é exatamente esse judder que um quadro **sintetizado** trocaria por movimento
real — que é o ponto seguinte.

#### Frame generation *é* fluxo óptico

Vale registrar porque a pergunta apareceu: fluxo óptico não é uma alternativa ao frame gen,
é o **motor** dele. O DLSS-G usa o Optical Flow Accelerator internamente; o FSR-FG tem fluxo
óptico próprio (os parâmetros `opticalFlowBlockSize`, `opticalFlowScale`,
`opticalFlowPyramidLevelCount` estão dentro do `amd_fidelityfx_dx12.dll` do projeto). Gerar
um quadro intermediário **é** estimar movimento entre dois quadros e deformar a imagem por
esse campo.

O que está bloqueado aqui são os frame gens **prontos** (§12): DLSS-G quer Streamline e
depth/MV reais; FSR-FG quer envelopar a swapchain, o que mata a cadeia NGX; o RenoDX não gera
nada. Usar o `NvOFAPI64.dll` + um shader de *warp* **é construir o nosso frame gen**, sob
medida para a camada, pelo único caminho que esta arquitetura permite.

### Limite honesto da medição feita aqui

Foi montado um alvo animado (`scratchpad/movimento.html`) e medido com um capturador em C++
(`scratchpad/pisca.cpp`, energia de gradiente a ~70 amostras). Resultado: **com e sem a
camada intermitente deu igual** (amplitude 0,37 contra 0,46; salto entre leituras 0,03 nos
dois), inclusive com 4 passes e intensidade máxima.

**Isso NÃO prova que o piscar acabou.** Duas ressalvas, e as duas importam:

1. O alvo é sintético — bordas duras e cores chapadas dão pouco material para a rede alterar,
   então a diferença entre "processado" e "cru" ali é pequena por construção.
2. Energia de gradiente é insensível a translação: blocos deslizando não mudam a métrica.

O que a medição sustenta é mais estreito e ainda assim útil: **a sincronização passou de 97%
de falha para 100% de acerto** (`esperou a tela 560 (dxgi 0, dwm 560), estourou o prazo 0`),
e o piscar observado pelo usuário aconteceu justamente quando 146 de 149 trocas caíam fora de
hora. É plausível que tenha ido junto — mas quem confirma é o jogo real, não este alvo.

---

## 12.1 O quadro sintetizado, medido contra verdade-terreno (2026-09-11)

A §12 terminou com três pendências: os headers do Optical Flow SDK, um shader de *warp*, e
ligar o campo ao `SLOT_MV`. Esta seção fecha a segunda com número, corrige uma premissa da
primeira, e explica por que a terceira ainda não foi mexida.

### A premissa que estava errada: o campo de fluxo **já existe nesta árvore**

O `NvOFAPI64.dll` respondeu na sonda e isso virou "o caminho é o OFA". Só que o app **já
tem um estimador de movimento por hardware, em produção, medido e validado**:
`motion_estimate.h` (D3D12 Video Motion Estimation), exposto no `main.cpp` pelo namespace
`MvHw` e entregue à cadeia pelo export `DLSS5Bridge_SetExternalMV`. Ele devolve exatamente
o que o *warp* precisa: **RGBA16F em delta UV, `prev_uv = uv + mv`, com canal de validade**.

Ele está **desligado de fábrica** (`[captura] mv_hardware=0`) por uma decisão medida e
registrada no próprio código: o `Lumenite_Kernel` é mais preciso (a 45 px/quadro, −0,7%
contra −6%), e a prioridade declarada é qualidade acima de custo. Isso vale para alimentar o
DLSS; **não vale para o warp**, que é outro consumidor com outro critério — e o custo dele
(fila de vídeo, fora da fila 3D) é justamente o que um quadro sintetizado pode pagar.

### As peças, e onde elas estão

| arquivo | o que é |
|---|---|
| `warp_frame.h` | **o módulo de produção**: guarda o último quadro processado e desenha o intermediário deformado. Não decide *quando* sintetizar, não liga técnica nenhuma e não fala com a ponte — recebe textura, campo e instante, e desenha. |
| `tests/warp_test.cpp` | mede o `warp_frame.h` contra verdade-terreno. **Usa o módulo de produção**, não uma cópia parecida: um teste com shader próprio poderia passar com um shader que a camada não usa. |
| `tests/alvo_animado.cpp` | fonte de teste: translada conteúdo rico a velocidade conhecida (`px/quadro` por argumento, vsync opcional), ou mostra um PNG parado (`--img`) para realimentar a cadeia nela mesma. |
| `tests/comparar_png.cpp` | compara dois dumps em pixel, alinhando antes (a área útil da janela varia alguns px entre aberturas). |

### O teste: `tests/warp_test.cpp`

O §12 admitiu que a medição anterior não provava nada (alvo de cores chapadas + métrica
insensível a translação). Este teste corrige os dois defeitos pela raiz: **impõe a
translação e compara contra o quadro que deveria aparecer**. Uma imagem base com ruído
multi-oitava, grade fina e áreas chapadas; os quadros são **recortes** dela (translação
exata, sem reamostragem); e o quadro do meio — a verdade — é o recorte deslocado pela
metade. Compara-se, contra essa verdade, o que a camada pode mostrar num quadro pulado:

| o que aparece | de onde vem |
|---|---|
| `segurar` | o quadro anterior, repetido — **o que o app faz hoje** |
| `cru` | o jogo ao vivo por baixo — o que os modos `alfa` e `regiao` mostram |
| `sintetizar` | o quadro anterior deformado por `0,5 × campo` — o que se avalia |

**3440×1440, estimador no padrão do app (divisor 2, bloco 16), PSNR em dB (maior = melhor),
área central (borda de 64 px fora da conta: nenhum warp inventa o que entrou pela borda):**

Translação em **pixel fracionário** — o caso real, onde a amostragem cai entre texels e o
warp paga o borrão do filtro bilinear:

| px/quadro | segurar (hoje) | cru | **sintetizar** | ganho |
|---|---|---|---|---|
| 2,5  | 23,21 | 25,78 | **36,38** | +13,2 dB |
| 6,5  | 22,71 | 24,06 | **36,64** | +13,9 dB |
| 12,5 | 21,87 | 22,95 | **37,13** | +15,3 dB |
| 20,5 | 20,79 | 21,59 | **37,73** | +16,9 dB |
| 32,5 | 19,39 | 19,96 | **37,69** | +18,3 dB |
| 48,5 | 17,96 | 18,36 | **36,78** | +18,8 dB |

Objeto de 400×300 andando sobre fundo **parado** (oclusão de verdade — o caso em que
nenhum campo único pode acertar tudo, porque o objeto descobre fundo que não existe em
nenhum dos dois quadros):

| px/quadro | segurar | cru | **sintetizar** | ganho |
|---|---|---|---|---|
| 2  | 37,72 | 37,72 | **51,67** | +14,0 dB |
| 12 | 36,01 | 36,00 | **42,08** | +6,1 dB |
| 32 | 33,21 | 33,04 | **38,14** | +4,9 dB |
| 48 | 31,73 | 31,57 | **34,58** | +2,9 dB |

Três leituras que importam:

1. **O erro do `segurar` cresce com a velocidade; o do `sintetizar` não.** De 2,5 a 48,5
   px/quadro o warp fica plano em ~37 dB (erro médio 0,8/255 por canal), enquanto o
   segurar despenca de 23 para 18 dB. Isso é exatamente a descrição do judder: o defeito
   aparece quando há movimento, e piora com ele.
2. **`cru` e `segurar` erram quase igual** contra a verdade do meio — como tem de ser, os
   dois estão à mesma distância do instante intermediário. O que essa métrica **não** mede
   é o flicker dos modos `alfa`/`regiao`, que vem da diferença de *conteúdo* (processado
   contra cru), não de posição. Para aquele defeito o número aqui não diz nada; para o
   judder, diz tudo.
3. **Oclusão é o piso do método, e o piso ainda está acima do chão.** No caso difícil o
   ganho cai de +19 para +3 dB a 48 px — o warp erra onde o fundo é descoberto —, mas
   continua melhor que repetir o quadro em toda a faixa medida.

Com deslocamento **inteiro** o teste dá PSNR de 99 a 102 dB (reconstrução praticamente
exata). Esse número está no teste e **não deve ser citado como resultado**: ele só existe
porque o warp cai no texel exato, e câmera de jogo não anda em pixel redondo. O número
honesto é a tabela fracionária.

**A grade fina vale mais para o warp do que para o DLSS.** Com `mv_divisor=1` e
`mv_bloco=8` (resolução plena, grade 4× mais densa) o mesmo cenário fracionário sobe de
36–38 dB para **41–44 dB**:

| px/quadro | divisor 2, bloco 16 (padrão) | divisor 1, bloco 8 |
|---|---|---|
| 6,5  | 36,64 | **44,42** |
| 12,5 | 37,13 | **41,38** |
| 20,5 | 37,73 | **44,05** |
| 48,5 | 36,78 | **42,78** |

O mecanismo tem nome no shader, e a distinção é mais útil que "o DLSS perdoa": o
`DLSS5_Feed.fx` roda `ValidateTests()` e, no caminho `MV_VALIDATE`, faz
`mv = flow * (1.0 - zero_vector)` — ele **descarta** o vetor que não passa nos testes de
luma, consistência e hipótese estática, em vez de usá-lo torto. É esse descarte que absorve
a grade grossa. Um warp desenhado direto na tela **não tem para onde descartar**: cada
vetor ruim vira pixel errado, e cada bloco de 16 px vira degrau na borda de movimento.

A consequência prática inverte a conclusão óbvia: se o custo apertar, a saída **não** é
aumentar a densidade do campo (grade fina custa ~2,9 ms na fila de vídeo contra ~1,86 ms),
e sim **aplicar no warp uma máscara de confiança equivalente à do `ValidateTests`**, que
custa quase nada. Grade fina é a solução cara para um problema que o Feed já resolve de
graça. (Diagnóstico do agente que revisou o caminho do feeder — ver
`pesquisa-2026-09-11/`.)

### Custo

Medido no mesmo teste, a 3440×1440: **campo 0,14 ms de CPU + warp 0,82 ms**. Os dois
números são tetos grosseiros e para cima, e vale dizer por quê:

- o 0,14 ms é o custo de **enfileirar** o `Update` (a execução acontece na fila de vídeo,
  assíncrona); o custo de GPU real dessa fila está medido no cabeçalho do
  `motion_estimate.h`: **1,86 ms a 3440×1440 com bloco 16** — e ele **não disputa a fila
  3D** com o render;
- o 0,82 ms do warp inclui **criar e destruir o alvo de render e a staging a cada chamada**,
  que numa integração de verdade são persistentes. É um passe de tela cheia com duas
  amostragens: o custo real fica bem abaixo disso.

Contra o que se compara: a cadeia inteira (Kernel + Feed + DLSS + NR) num quadro
processado. Enquanto o sintetizado custar uma fração dela, a troca se paga.

### O custo end-to-end, medido no app de verdade (2026-09-11)

A pergunta que decide o recurso não é quanto custa o warp — é **quanto custa o quadro em
que a cadeia é suprimida**. Se ele não sair muito mais barato que reprocessar, o modo
`sintetizar` não se paga, porque o `segurar` custa **zero** no quadro pulado (ele
simplesmente não apresenta).

Bancada, montada para esta medição e deixada na árvore:

- `tests/alvo_animado.cpp` — uma janela que translada uma imagem de conteúdo rico a uma
  velocidade **conhecida** (px/quadro por argumento), com vsync opcional. Existe porque um
  jogo real não é reproduzível: o Witcher 3 **pausa fora de foco**, e foi isso que produziu
  a leitura falsa de "motion vectors zerados" (ver `pesquisa-2026-09-11/`);
- app em cópia isolada (`bin-teste-sintetizar`), fonte 2750×1125, modo janela separada.
  Janela separada de propósito: no modo sobreposto a camada some quando o alvo perde o
  foco, e com vários agentes mexendo em janelas a medição não fecha.

Com a fonte **presa ao vblank** (60 fps) a camada acompanha e o custo fica escondido atrás
da folga. Com a fonte **destravada** (~110 fps) a camada vira o gargalo e o fps dela passa
a ser a medida direta do custo por quadro:

| rodada | o que roda na cadeia | fps de chegada | fps apresentado | espera de slot (média/p95) | custo por quadro |
|---|---|---|---|---|---|
| **D** | cadeia completa (SMAA + Lumenite_Kernel + DLSS5_Feed + Sharpen + NR) | 110 | **58,2–58,5** | 7,0 / 13,8 ms | **17,1 ms** |
| **E** | `Techniques=` vazio — só o NR do RenoDX | 99,6 | **99,0–99,7** | **0,02 / 0,03 ms** | **≤ 10 ms, com folga** |

Na rodada E a camada **não perde um único quadro** e a espera de slot é praticamente zero:
a GPU deixou de ser o gargalo, e quem limita passa a ser a captura (o WGC não passa de
~100 fps). Ou seja, 10 ms é **teto de instrumento, não medida** — o custo real do quadro
barato é bem menor.

Vale desfazer uma suposição fácil aqui: **o teto não é o refresh do monitor.** Foi medido
nesta mesma árvore, em outra frente, **97,5 presents por segundo num monitor de 60 Hz**
(4532 presents em 46,5 s, fonte janela, sem teto) — a captura de janela entrega acima do
refresh e o nosso `Present(0, 0)` não espera vblank. Quem for calcular quantos quadros
sintetizados cabem entre dois reais não deve usar 60: o número é o que a captura estiver
entregando naquele instante, e ele varia.

**Resposta ao critério:** sim, o quadro sintetizado sai mais barato que reprocessar.
Reprocessar custa 17,1 ms; suprimir as técnicas e desenhar o warp custa **menos de 10 ms**
(o warp em si mede 0,2–0,8 ms, §12.1 acima). A economia é de **pelo menos 7 ms por quadro
sintetizado**, e provavelmente do dobro disso.

Uma conta de orçamento, com 1 quadro em cada 2 (os números vêm da tabela):

| modo | presents por segundo | GPU gasta | o que a pessoa vê |
|---|---|---|---|
| sem pulo | 58 caros | ~100% | movimento cheio |
| `segurar` (hoje) | 29 caros | ~50% | **judder**: metade dos quadros é repetição |
| `sintetizar` | 29 caros + 29 baratos | ~50% + o barato | movimento cheio, sintetizado |

**Confirmação do mecanismo:** as técnicas `.fx` (Kernel + Feed + SMAA + Sharpen) são o
custo dominante, e desligá-las por quadro **funciona** — é o que as rodadas D e E provam,
cada uma com o mesmo alvo e a mesma resolução. O NR do RenoDX, que **não** sai por esse
caminho, cabe folgado sozinho: na rodada E ele sustentou 99 fps.

### O obstáculo que sobrou, e ele não é o shader

O warp está provado. O que falta para virar `[ultima] alterna_modo=sintetizar` é
**arquitetura de apresentação**, não matemática:

> **Todo `Present` nosso dispara a cadeia inteira.** O ReShade e o RenoDX estão hookados no
> `Present` da nossa swapchain. O modo `segurar` economiza GPU porque **não apresenta** o
> quadro pulado. Um quadro sintetizado precisa de um `Present` próprio — e esse `Present`
> roda a cadeia de novo. Sem suprimir a cadeia nesse quadro, o modo `sintetizar` fica **mais
> caro que não pular nada**, e o recurso existe justamente para aliviar a GPU.

As peças para resolver isso **já existem na árvore** — esta é a parte boa, e é o que torna a
integração um trabalho de ligação, não de invenção:

| o que falta no quadro sintetizado | peça que já existe |
|---|---|
| ter o último quadro **processado** em textura | `DLSS5Bridge_SetSharedOutput` (add-on, publica no `reshade_finish_effects`) + `shared_source.h` (consumidor, no app) |
| o campo de movimento | `MvHw` / `motion_estimate.h`, com `[captura] mv_hardware=1` |
| desenhar o warp | o shader deste teste, aplicado no `Renderer` como `extSrv` |
| **suprimir a cadeia naquele Present** | `rsbridge::SetTechniqueEnabled`, que o app já usa ao vivo (`Efeitos::Suspender`) |

O ponto de risco é o último, e a medição das rodadas D e E acima já responde à metade
dele: **suprimir as técnicas funciona e a economia é grande** (17,1 ms contra menos de
10 ms). O NR do RenoDX **não** sai por esse caminho — ele é add-on com sessão NGX própria,
não é técnica `.fx`; o único jeito de desligá-lo é o `AlternarNeural()`, que custa ~700 ms
pelo teclado e só vale no quadro seguinte pela ponte, inviável 30 vezes por segundo. Ou
seja, o quadro sintetizado **paga o NR de qualquer jeito** — e a rodada E mostra que isso
cabe: sozinho, o NR sustentou 99 fps na mesma fonte.

O que continua **não medido**, e é o risco de verdade que sobra:

1. **Alternar técnica a cada quadro nunca foi exercitado nesta árvore.** As rodadas D e E
   ligaram e desligaram as técnicas *entre execuções*, não 30 vezes por segundo. O custo de
   alternar não foi medido.
2. **Sincronismo entre camadas.** No empilhamento (camada 2), o quadro pulado hoje nunca
   chega à camada de cima; um quadro sintetizado que passe a chegar muda a contagem, e não
   há registro de teste desse caso.

### O risco de cintilação, testado — e ele não existe pelo caminho que eu supunha

Antes de implementar qualquer coisa, ficou a dúvida que poderia matar o recurso. Vale
registrar as **duas** formulações, porque a primeira parecia óbvia e estava errada:

**Suposição 1 (errada):** *"desligar o `DLSS5_Feed` num quadro abre buraco no histórico do
DLSS"*. Não é novidade nenhuma: o pulo de quadro **já em produção** faz exatamente isso —
no quadro pulado não há `Present`, logo não há Feed. Do ponto de vista do histórico da
rede, o `sintetizar` tem a **mesma cadência** do `segurar` de hoje.

**Suposição 2 (também errada, e esta precisou de medição):** *"o NR do RenoDX roda em todo
`Present`, então no quadro sintetizado ele passaria por cima de uma imagem que já tem NR —
a tela alternaria entre 1× NR e 2× NR"*. Isso tinha apoio: o
`pesquisa-2026-09-06/RELATORIO-SINCRONIA-BORRAO.md` §7.1 mede regressão à média ao repetir
passes em cascata.

**O teste, que não exigiu implementar o modo.** `DLSS5_DUMP` grava a saída real da cadeia;
`tests/alvo_animado.cpp --img <png>` mostra um PNG parado, do tamanho exato dele. Junto, dá
para **realimentar a saída da cadeia nela mesma** e medir o que cada parte faz.
Comparações com `tests/comparar_png.cpp` (que alinha antes de comparar, porque a área útil
da janela varia alguns pixels entre aberturas):

| dump | técnicas `.fx` | NR | contra | resultado |
|---|---|---|---|---|
| C | não | não | — | referência |
| **B** | não | **sim** | C | **idêntico bit a bit (mesmo MD5)** |
| D | sim | não | C | PSNR 58,1 dB · MAE 0,086/255 |
| F | sim | sim | D | PSNR 24,1 dB · MAE **12,2**/255 (máx. 219) |

A leitura é direta e cada linha checa a outra:

- **O NR não roda quando não há técnicas do ReShade ativas.** B e C saíram idênticos byte a
  byte, e a única diferença entre eles era `NeuralUplift` ligado contra desligado. Não é
  "o NR mudou pouco" — é **não rodou**;
- **e ele estava funcional**, o que impede a leitura preguiçosa de "essa cópia está com o
  NR morto": com técnicas ativas, ligar o NR muda a imagem em 12,2/255 de média e 219/255
  de pico (F contra D);
- logo, no quadro em que as técnicas são suprimidas, **nada da cadeia roda — nem `.fx` nem
  NR**. O que explica, de quebra, os 99 fps sem esforço da rodada E.

**Consequência: o risco de cintilação por NR duplo não existe.** O quadro sintetizado não
recebe NR nenhum, e o conteúdo que ele mostra é o quadro processado anterior deformado —
que **já carrega** o NR. Os dois quadros alternados têm o mesmo tratamento de imagem, que é
exatamente o que o modo precisa.

**As três ressalvas, para este resultado não ser usado além do que ele sustenta:**

1. a fonte estava **parada**. Isso explica a linha D: com cena parada o DLAA converge e as
   técnicas quase não alteram (0,086/255) — **não generalizar** esse número para cena em
   movimento;
2. as técnicas foram alternadas **entre execuções**, pelo `.ini`, e não uma vez por quadro.
   O mecanismo é estrutural (o RenoDX se enxerta no caminho de efeitos; sem efeitos não há
   onde se enxertar), mas o **transiente de religar** não foi medido: se o NR precisar de
   alguns quadros para reengatar a cada volta, a cintilação reaparece por outra porta —
   e é isso que o primeiro teste em jogo tem de olhar;
3. o `comparar_png.cpp` nasceu com um defeito que vale contar porque mente bonito: ele
   rejeitava o alinhamento de erro zero (`if (e.mae > 0 …)`) e, para dois arquivos
   idênticos, imprimia um deslocamento plausível com "MAE 3,589" em vez de "igual". Um
   comparador que não consegue dizer "igual" não serve para decidir nada. Corrigido — e
   foi essa correção que revelou o `B ≡ C`.

**Recomendação:** o caminho está livre o suficiente para implementar. O primeiro teste com
jogo aberto deve ser o transiente da ressalva 2, e é barato: alternar as técnicas em quadros
alternados e olhar se a imagem "respira" na cadência da alternância.

---

## 12.2 O modo `sintetizar`, implementado e rodando (2026-09-11)

`[ultima] alterna_modo` agora aceita **quatro** valores: `alfa` (padrão), `regiao`,
`segurar` e **`sintetizar`**. Os três primeiros não mudaram.

### As peças e a ligação entre elas

| peça | papel |
|---|---|
| `warp_frame.h` | guarda o último quadro processado e desenha o intermediário deformado |
| `MvHw` / `motion_estimate.h` | o campo de movimento (fila de vídeo, fora da fila 3D) |
| `DLSS5Bridge_SetSharedOutput` + `shared_source.h` | de onde sai o último quadro **processado** |
| `rsbridge::SetTechniqueEnabled` | suprime a cadeia no quadro sintetizado |
| `Renderer::ApresentarSemDesenhar` | apresenta o backbuffer que o warp pintou, sem redesenhar a captura |
| namespace `Sintetiza` (main.cpp) | a orquestração: quem guarda, quem desenha, quando suprimir |

O ciclo, com "1 de cada 2": **quadro processado** → apresenta normal, e guarda a saída que o
add-on publicou no `reshade_finish_effects`; **quadro pulado** → desenha o guardado
deformado por `0,5 × campo` e apresenta com a cadeia suprimida.

Três dependências foram resolvidas para o recurso se sustentar sozinho:

- **`mv_hardware` liga sozinho** quando o modo é `sintetizar`. Sem campo não existe quadro
  sintetizado, e o `Lumenite_Kernel` (o estimador preciso) não serve aqui: ele roda *dentro*
  da cadeia, que é justamente o que fica suprimido. Obrigar a pessoa a descobrir uma segunda
  chave num arquivo de texto para o recurso funcionar seria esconder a dependência;
- o modo cai para o **`segurar`** sempre que falta base ou campo — e o log conta quantas
  vezes. Cair para o degrau anterior é sempre melhor que apresentar lixo;
- o `DLSS5Bridge_SetSharedOutput` **existe no fonte do add-on mas não estava no
  `.addon64` instalado**. Foi preciso recompilar o add-on.

### Funciona — e o log traz uma validação de brinde

Fonte de 1396×791 a 60 fps, alvo transladando a **10 px/quadro**:

```
mv-hw: ligado automaticamente porque [ultima] alterna_modo=sintetizar
mv-hw: VME ok, grade 43x24, divisor 2, bloco 16 (fonte 1396x791)
sintetizar: pronto (transporte MUTEX 1396x791) - o quadro pulado passa a ser DESENHADO
sintetizar: 305 quadros desenhados | caiu no 'segurar' 0 vezes
mv-hw: |mv| media 9.904 px, mediana 9.915 px | 100% nao-zero, 0% campo zerado
```

**O campo mediu 9,90 px com o alvo andando a 10,0 px/quadro** — o estimador, dentro do app,
com captura real pelo WGC, acerta o deslocamento imposto. É a validação ponta a ponta que o
`warp_test` só conseguia dar offline.

E o resultado que interessa: `fps chegada=60,1 | present=30,1` **mais** 30,1 quadros
sintetizados por segundo. A camada apresenta **60,2/s processando 30,1/s** — taxa cheia com
metade do trabalho de cadeia. O contador de fps do próprio ReShade, na tela, marca 60.

### O limite que apareceu, e ele importa

Repetindo a medição com a fonte **destravada** (mesma cena, ~192 fps de chegada):

| modo | chegada | processados/s | sintetizados/s | total na tela | espera de slot |
|---|---|---|---|---|---|
| `segurar` | 192 | **96** | — | 96 | 0,5 ms |
| `sintetizar` | 100 | **50** | 50 | 100 | **7,96 ms** |

A taxa **total** na tela ficou igual (96 contra 100), e a de chegada **caiu de 192 para
100**. A causa está na própria tabela: `slot 7,96 ms` e `serial 17,2 ms` por volta, contra
1,88 ms no `segurar`. São **dois Presents por volta do laço**, e cada um espera o seu slot
(a swapchain é *waitable* com `SetMaximumFrameLatency(1)`) — as esperas **serializam**, e o
laço inteiro, captura incluída, anda no ritmo delas.

A leitura honesta, então, é que o `sintetizar` tem **dois regimes**:

- **quando a cadeia é o gargalo** (o caso do usuário: filtro pesado, fonte a 60) ele
  **dobra a fluidez** — 60 na tela contra 30 do `segurar`, medido;
- **quando a apresentação é o gargalo** (fonte entregando muito acima do refresh) ele
  **não aumenta a taxa**: economiza metade do trabalho de cadeia e entrega o mesmo número
  de quadros. Não é perda, mas também não é o ganho que o nome promete.

O ajuste que isso pede — e que **ainda não está feito** — é não sintetizar quando a fonte já
entrega acima do que a tela aproveita. O número para essa decisão não é 60: foi medido nesta
árvore que a película faz **97,5 presents/s num monitor de 60 Hz**, porque a captura de
janela passa do refresh e o nosso `Present(0,0)` não espera vblank.

### O segundo regime, resolvido

O modo agora **não sintetiza quando já há quadros de sobra**. O critério é o **refresh do
monitor** (`RefreshDoMonitor()`, com margem de 5%), e não o teto físico da película: acima
do refresh o DWM descarta os quadros extras antes de virarem luz, então um quadro a mais
custa e não aparece. Medido com a fonte destravada:

```
sintetizar: 0 quadros desenhados | processados 460,0/s | dispensado por ja haver quadros 4262
```

Zero síntese, zero custo — o modo se desliga sozinho e o comportamento volta a ser o do
`segurar`. No regime de fonte a 60 (a cadeia sendo o gargalo) ele continua desenhando
~305 por 10 s com ~30 processados/s.

### A cintilação, medida — e ela não existe

O instrumento ficou **dentro do app** (`[captura] sint_medir_luz=1`, desligado de fábrica):
uma amostra de 64×64 do centro, com leitura assíncrona (uma amostra de atraso, `DO_NOT_WAIT`
— um `Map` bloqueante dentro do laço de present seria um instrumento que muda o que
pretende medir). O quadro **processado** é amostrado da saída que o add-on publicou; o
**sintetizado**, do backbuffer antes do Present (ali nada roda depois, então é o que vai
para a tela).

| janela | processado | sintetizado | **diferença** | salto da cena (controle) |
|---|---|---|---|---|
| 1 | 134,59 (n=235) | 134,64 (n=234) | **0,05** | 9,87 / 9,82 |
| 2 | 134,07 (n=301) | 134,07 (n=302) | **0,00** | 9,33 / 9,26 |
| 3 | 132,08 (n=305) | 132,17 (n=304) | **0,09** | 9,35 / 9,40 |

A diferença entre os dois tipos de quadro é de **0,00 a 0,09 em 255**, enquanto a própria
cena varia **9,4** entre amostras consecutivas do mesmo tipo. **Razão de ~100 para 1** — o
que a alternância acrescenta está duas ordens de grandeza abaixo do que a cena faz sozinha.
Os `n` batendo (235/234, 301/302, 305/304) confirmam que os dois tipos foram amostrados na
mesma proporção, e não que um deles simplesmente não apareceu.

Isso fecha, com número e dentro do app, a dúvida que abriu a §12.1: **o quadro sintetizado
tem o mesmo tratamento de imagem que o processado.** É consequência direta do achado de que
sem técnicas nada roda — o sintetizado não recebe neural novo, e não precisa: ele herda o
do quadro que foi deformado.

A tentativa anterior, por **captura de tela externa**, foi descartada e vale como registro
do que não fazer: ela não sincroniza com o Present (não dá para saber qual tipo de quadro
se amostrou), mede o conteúdo passando em vez do estilo, e no meio da medição outro
programa abriu uma janela sobre a região amostrada.

### Três defeitos encontrados rodando, e o que cada um ensina

Nenhum apareceu na compilação; todos precisaram do app em execução.

1. **Espera que congela quem ela espera.** O preparo chamava `AbrirEsperando(4000)` pelo
   transporte — mas quem publica é o add-on, **dentro do Present, na mesma thread**.
   Esperar ali congela exatamente o laço que produziria o quadro esperado. O log dizia "a
   camada de baixo ainda não publicou", e estava certo: não ia publicar mesmo. Agora são
   duas fases sem espera.
2. **Laço morto por dependência circular.** Uma vez suprimida a cadeia, não havia mais
   saída publicada; sem saída não havia base; sem base não havia síntese; e a decisão só
   era revista quando algo era apresentado. O estado se fechava e não abria: **zero** quadros
   desenhados com o log repetindo "caiu no 'segurar' 305 vezes (sem base 305)". A regra que
   saiu daí: *este bloco desliga o que produz a entrada dele mesmo — toda condição que
   desliga a cadeia precisa provar antes que já tem o que precisa dela.*
3. **Bandeira que se inicializa dentro do que ela protege.** O instrumento de luminância só
   lia a própria chave do ini na primeira amostra — mas a guarda que evita custo no caminho
   quente é a mesma bandeira. Começando em `false`, nunca era chamada, e o instrumento não
   imprimia uma linha com todo o resto funcionando.

E um quarto, de ordem e não de lógica: `MvHw::Ensure` lê a chave **uma vez só** e rodava
**antes** de `Alterna::LerModoIni`, então via `MODO_ALFA` e o modo subia sem campo de
movimento — caindo no `segurar` em todo quadro, calado. O log de cada peça estava certo; só
a ordem entre elas estava errada.

### O controle no painel: **um interruptor**, não quatro opções

Por baixo existem quatro mecanismos (`alterna_modo` = `alfa` / `regiao` / `segurar` /
`sintetizar`), mas três deles diferem só em **qual caminho técnico faz a camada sumir** —
é decisão de implementação, não escolha de quem usa. Só o quarto muda o que aparece na
tela. Então o painel oferece **a única escolha que muda o que se vê**:

> **Movimento sintetizado** — *no lugar do jogo cru, a camada desenha o quadro do meio —
> some o tremular*

Fica logo abaixo dos chips da camada intermitente, e **só aparece com ela ligada**: sem
quadro pulado não há o que sintetizar, e interruptor visível que não faz nada é pior que
interruptor nenhum (regra que a seção já aplicava). Os outros três valores continuam no
ini, para quem precisar de saída quando o padrão não aparecer numa máquina.

Um detalhe que não é cosmético: a linha de aviso *"pode tremular: um quadro tem DLSS e
neural, o seguinte é o jogo cru"* **desaparece quando o interruptor está ligado**. Com o
movimento sintetizado os dois quadros passam a ter o mesmo tratamento (0,05 em 255, medido
acima), então manter o aviso ali seria assustar a pessoa com um defeito que ela acabou de
desligar.

**Testado ao vivo**, com clique real no painel (o painel não sai em captura de tela —
`EXCLUDEFROMCAPTURE` —, então a conferência foi pelo `DLSS5_LOG_ITENS=1`, que despeja as
áreas clicáveis em coordenadas de tela):

```
item[10] kind=5 val=0 ativo=1  centro-na-tela=3088,787
... clique ...
intermitente: movimento sintetizado desligado (mecanismo 'segurar')
... clique ...
intermitente: movimento sintetizado LIGADO (mecanismo 'sintetizar')
sintetizar: pronto (transporte MUTEX 1396x791)
sintetizar: 47 quadros desenhados | processados 30,0/s
```

Os dois sentidos desfazem o que o outro faz: ligar acorda o estimador de movimento no meio
da sessão (`MvHw::Ligar`, que derruba as duas guardas do `Ensure` — elas existem para o
caminho de abertura e fariam o botão não responder); desligar **religa a cadeia antes de
qualquer outra coisa**, porque ela pode estar suprimida naquele instante e sair do modo sem
religar deixaria o filtro morto na tela.

### Conferência visual

Capturas da camada em movimento (alvo a 10 px/quadro), com a grade fina do alvo servindo de
prova: qualquer erro de warp apareceria como ondulação, quebra ou duplicação nas linhas.
Elas saíram **retas e contínuas**, com as bordas dos blocos nítidas e sem fantasma.

### Em jogo real (Hollow Knight, 11/09)

Rodou. A camada subiu sobre o jogo em janela de 1536×864, no modo `sintetizar`, e
apresentou **100 fps na tela processando 50/s** (50 processados + 50 sintetizados) — o
contador do ReShade no canto confirma os 100. A imagem saiu limpa: o texto ornamentado do
título e as filigranas finas nítidas, partículas sem rastro.

A medição de cintilação, agora sobre cena de jogo:

| janela | processado | sintetizado | **diferença** |
|---|---|---|---|
| 1 | 95,21 (n=500) | 95,21 (n=500) | **0,00** |
| 2 | 95,73 (n=499) | 95,73 (n=500) | **0,00** |
| 3 | 95,37 (n=500) | 95,37 (n=499) | **0,00** |

**Duas ressalvas que impedem de chamar isto de prova completa**, e elas importam mais que a
tabela:

1. **A cena estava quase parada.** Só consegui chegar ao menu principal: o jogo **não
   aceita teclado sintético** — nem `keybd_event` com virtual-key, nem `SendInput` com
   scancode, nem Enter nem Z, com a janela comprovadamente em foco. Sem entrar no save não
   há câmera andando. O campo confirma o quadro parado: `|mv| média 0,044 px, 4% não-zero`
   — que é, por outro caminho, a mesma conclusão já registrada em `pesquisa-2026-09-11`
   (campo zero = fonte parada, não defeito);
2. por isso o `salto` da própria cena caiu para 0,04–0,07 (contra 9,4 no alvo em
   movimento). Com a cena parada, "diferença 0,00" prova que **não há viés sistemático**
   entre os dois tipos de quadro, mas não exercita o warp. **O teste que exercita o warp
   continua sendo o do alvo animado**, com deslocamento conhecido.

O que falta, então, é estreito e não depende mais de código: **alguém jogando**. Movimento
de câmera com paralaxe, num jogo, com o interruptor ligado e desligado.

### Pendência para a remontagem

O `DLSS5Bridge_SetSharedOutput` **existe no fonte do add-on mas não estava no `.addon64`
instalado em `bin\`** (de 05/09) — foi preciso recompilar. **Sem recompilar o add-on junto,
o modo não sobe**: ele avisa no log e cai no `segurar`, mas ninguém vai ligar uma coisa à
outra. Vale conferir o que mais em produção está atrasado em relação ao fonte.

Vale dizer o que **não** deve ser feito: empilhar isso na mesma rodada do `SLOT_MV`, e
mexer no caminho de apresentação sem jogo aberto — é onde moram `FonteMudou`, a cadeia NGX
e o `ConferirUmPorUm`.

### Optical Flow SDK: o que exatamente faltou

A prova de vida do §12 (9 ponteiros preenchidos) **não determina o layout da
`NV_OF_D3D11_API_FUNCTION_LIST`** — a sonda inseriu um `uint32` de versão no início por
suposição, e se ele não existir na struct real os ponteiros lidos estavam deslocados em 4
bytes, o que produziria uma contagem parecida. A contagem, sozinha, não distingue os dois
casos.

O que é público e o que não é:

- **público**: `https://github.com/NVIDIA/NVIDIAOpticalFlowSDK` — mas a raiz só tem
  `README.md`, `nvOpticalFlowCommon.h` e `nvOpticalFlowCuda.h`. O *common* dá os enums, os
  status e `NV_OF_INIT_PARAMS`/`NV_OF_EXECUTE_*_PARAMS`;
- **não público**: `nvOpticalFlowD3D11.h` / `nvOpticalFlowD3D12.h`, que são justamente onde
  moram a ordem da function list, as assinaturas de `nvOFRegisterResourceD3D11/12` e as
  structs de registro de recurso. Eles vêm no pacote de
  `https://developer.nvidia.com/opticalflow-sdk`, **atrás de login de NVIDIA Developer**;
- o *Programming Guide* público (`docs.nvidia.com/video-technologies/optical-flow-sdk/`)
  descreve o fluxo de chamadas, mas **não** os campos das structs.

Varredura do disco inteiro (`nvOpticalFlow*.h`, `NvOFInterface*.h`): **nada**. Não há CUDA
Toolkit nem SDK de desenvolvimento nesta máquina. A extensão do Chrome estava
**desconectada**, então o download pelo navegador do usuário não foi possível.

**Decisão: não reconstruir essa ABI por adivinhação.** Uma function list com a ordem errada
não falha limpo — ela chama o ponteiro errado com os argumentos errados, dentro do driver.
E, mais importante: **o OFA deixou de ser pré-requisito**. O campo do VME já sustenta o warp
com os números acima. O OFA continua valendo como *upgrade* de qualidade (grade de 4 px
contra bloco de 16, e melhor comportamento em oclusão), e para isso basta o usuário baixar o
SDK uma vez, logado, e apontar o caminho — é o que deve ser pedido quando o assunto voltar.
