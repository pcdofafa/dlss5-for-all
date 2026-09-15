# Motion vectors e profundidade "zerados" no DLSS 5 For All

**Data:** 2026-09-11
**Pergunta de partida:** por que o `dlss5-feed.log` relata, em toda amostragem,
`MV probe ... mean |mv| 0.000 px, 0% non-zero` e `Depth probe ... min 0, max 0, variance 0`?
A suspeita inicial era regressão causada pela troca do add-on `renodx-dlss5.addon64`
(Krish/Lecram) pelo `renodx-dlss.addon64` (ShortFuse) em 2026-09-07.

**Resposta curta: não é regressão e não é bug.** A cadeia de motion vectors funciona.
Os dois zeros têm causas diferentes e ambas foram medidas nesta máquina hoje:

1. **MV zerado = a imagem capturada não estava se mexendo.** Com a mesma build, os mesmos
   shaders e o mesmo `ReShade.ini`, a mesma janela dá `34,7 px / 97% non-zero` quando anima e
   `0,017 px / 27%` quando redesenha o mesmo quadro.
2. **Depth zerada é estrutural desta arquitetura.** Uma fonte por captura de janela/monitor
   compõe o quadro pronto de outro processo; não existe buffer de profundidade para o ReShade
   entregar. `ReShade::DepthBuffer` é o *fallback* vazio e o `DLSS5_Feed.fx` copia zeros
   fielmente. Acontece igual com e sem movimento — não é sintoma de nada.

O que estava errado era o **texto do diagnóstico**, que acusava o provedor de motion vectors nos
dois casos e mandou a depuração atrás de um bug inexistente. Isso foi corrigido.

---

## 1. O que foi lido

- Memórias da máquina: `renodx-dois-addons.md`, `dlss5-hookpoint-upscaled.md`,
  `nunca-matar-app-do-usuario.md`.
- `amd/main.cpp` — `CorrigirHookPointNoArranque()` (3765-3812, chamada em 10215),
  `AplicarPassesNr()` (8486-8505), `MvHw::Ensure()` (9186-9246), `ConferirShaders()` (1978-2004).
- `amd/feeder-amd/src-feeder/src/dlss5-feed.cpp` — probes de guias (1907-2093), probe de
  *staleness* (2095-2200), `CopyOrResampleInputs()` (4800), caminho D3D11 do feed (6230-6520),
  `ResolveHandles()` (6614-6700), `OnRenderTechnique()` (6847).
- `amd/reshade-shaders/Shaders/DLSS5_Feed.fx` — seleção de provedor (77-135), `ProviderMV()`
  (580-588), `ValidateTests()` (629-687), `PS_MotionVectors()` (909-1051), técnica (1167-1183).
- `amd/reshade-shaders/Shaders/lumenite_Kernel.fx` — texturas `Kernel::tFlow`/`tConfidence`
  (76-79), cascata de fluxo óptico e técnica (717-751).
- `amd/reshade-shaders/Shaders/DLSS5_MV_Hardware.fx` — publica em `Kernel::tFlow` (250-251).
- `bin-teste-passes/`: `ReShade.ini`, `FafaDLSS5.ini`, `dlss5-feed.cfg`, `dlss5forall.ini`,
  `ReShade.log`, `dlss5-feed.log`, `dlss5forall.log` e os backups `.bak-antes-*`.
- Logs históricos: `dlss5-feed.log.bak-baseline-4.1.5` (03/09), `dlss5-feed.log` da raiz (04/09),
  `DLSS5ForAll-AMD/bin/dlss5-feed.log` (09/09).

## 2. O que derruba a hipótese de regressão

O `MV probe` zerado aparece em **todos** os logs guardados no projeto, inclusive nos anteriores à
troca de add-on:

| Log | Data | Add-on ativo | MV probe |
|---|---|---|---|
| `dlss5-feed.log.bak-baseline-4.1.5` (raiz) | 03/09 12:11 | `renodx-dlss5` (o antigo) | `0.000 px, 0% non-zero` |
| `dlss5-feed.log` (raiz) | 04/09 15:19 | `renodx-dlss5` (o antigo) | `0.000 px, 0% non-zero` (11 amostras) |
| `bin-teste-passes/dlss5-feed.log` | 09/09 01:41-01:49 | `renodx-dlss` (ShortFuse) | `0.000 px, 0% non-zero` (41 amostras) |

O add-on mudou; o número não. Então o que quer que produza o zero é anterior e independente da
troca — o que já exclui as hipóteses (a), (d), (e) e (f) da lista original.

Também ficou confirmado que a partilha de textura entre efeitos funciona e é intencional: o
`DLSS5_MV_Hardware.fx` escreve em `Kernel::tFlow` (`DLSS5_MV_Hardware.fx:250`) exatamente para ser
lido pelo `DLSS5_Feed.fx`, que redeclara a mesma textura (`DLSS5_Feed.fx:108-118`). O log confirma
a seleção certa a cada sessão:

```
[feed] effects: DLSS5_Feed.fx technique found, ColorInput found, DLSS5_MV found, DLSS5_Depth found,
       DLSS5_Mask found, DLSS5_MV_PROVIDER=3 (LumeniteFX Kernel) -> Lumenite_Kernel (enabled)
```

## 3. A medição que fecha a questão

Cópia isolada em `DLSS5ForAll-AMD\bin-teste-passes-fix\` (nada foi escrito em `bin-teste-passes\`
nem em `bin\`), `ReShade.ini` com `EffectSearchPaths=.\reshade-shaders\Shaders\**`,
`PresetPath=.\FafaDLSS5.ini`, `DirectNeuralRenderingHookPoint=2`, `PassCount=2`. Fonte: uma janela
WinForms de 1280x720 (`ALVO-MV-TESTE`), com dois conteúdos:

**A. Janela ANIMADA** (tabuleiro e barras deslizando ~10-30 px por quadro):

```
00:33:49.170  MV probe (centre 64x64, frame 600):  mean |mv| 34.731 px, max 88.29 px, 97% non-zero
00:34:04.752  MV probe (centre 64x64, frame 1200): mean |mv| 73.080 px, max 100.31 px, 99% non-zero
00:34:20.443  MV probe (centre 64x64, frame 1800): mean |mv|  3.588 px, max 4.34 px, 99% non-zero
```

**B. A MESMA janela redesenhando o MESMO quadro** (controle negativo):

```
00:35:17.169  MV probe (centre 64x64, frame 600):  mean |mv| 0.017 px, max 0.19 px, 27% non-zero
00:35:33.331  MV probe (centre 64x64, frame 1200): mean |mv| 0.017 px, max 0.19 px, 27% non-zero
```

Mesmo binário, mesmos shaders, mesmo `.ini`, mesmo RenoDX do ShortFuse em `HookPoint=Upscaled` com
`PassCount=2`. A única variável foi o movimento na fonte. **A cadeia
`Lumenite_Kernel -> DLSS5_Feed.fx -> DLSS5_MV -> NGX` entrega motion vectors corretamente.**

O `Depth probe` deu `min 0, max 0, variance 0, 100% finite` nos **dois** casos — confirmando que a
profundidade não tem relação com movimento: ela simplesmente não existe nesta arquitetura.

### Por que a fonte do usuário estava parada

No `dlss5forall.log` da sessão de 09/09 (01:41-01:49), o foco do teclado nunca esteve no jogo:

```
[01:41:40.099] overlay fora de vista: bypass=1 ... foco=... pid 32440 classe Chrome_WidgetWin_1
[01:42:53.712] overlay fora de vista: bypass=1 ... foco=... pid 33736 classe DLSS5ForAllPanel
```

O alvo era `janela 'The Witcher 3'` (`dlss5forall.ini:5`) e o foco esteve o tempo todo no Chrome e
no painel do próprio app. O Witcher 3 pausa ao perder o foco: continua apresentando a ~50-60 fps
(daí `fps chegada=50..60` no log, que foi lido como "conteúdo dinâmico"), mas com a cena congelada.
`fps de chegada` mede **entrega de quadros pelo WGC**, não mudança de conteúdo — e essa confusão
é exatamente o que a correção abaixo resolve.

## 4. A correção aplicada

Não há o que consertar na cadeia de MV. O que foi corrigido é o **diagnóstico**, em
`amd/feeder-amd/src-feeder/src/dlss5-feed.cpp`:

1. **`StaleProbeRecord()` passa a rodar no caminho D3D11** (`dlss5-feed.cpp:6496-6503`). Ela já
   existia e já era chamada nos caminhos Vulkan (5757) e OpenGL (6156); o D3D11 — o único que o app
   realmente usa — era o que não a tinha. Sem ela, um campo de MV zerado não tem contexto nenhum.

2. **A probe de *staleness* agora acumula uma corrida de amostras** e só imprime nas transições
   (`dlss5-feed.cpp:1907-1914` e `2135-2160`). Uma linha por transição em vez de uma por amostra
   (eram ~1/s nos caminhos VK/GL); e a "corrida" existe porque o `MV probe` é uma foto de um único
   quadro: um padrão periódico pode cair num quadro que coincide com o anterior.

3. **O `MV probe` deixa de acusar o provedor quando a fonte está parada**
   (`dlss5-feed.cpp:1989-1999`):

   ```
   MV probe (...): mean |mv| 0.014 px, max 0.18 px, 29% non-zero  <-- and the colour input has not
   changed for several samples either: the source is still (paused game, background window, static
   image). Zero is the correct answer here.
   ```

4. **O `Depth probe` deixa de mandar mexer no Generic Depth quando não há profundidade nenhuma**
   (`dlss5-feed.cpp:2022-2037`):

   ```
   Depth probe (...): min 0, max 0, mean 0, variance 0, 100% finite  <-- depth is identically zero:
   this host has no depth buffer (window/monitor capture). Expected; DLSS runs on colour + motion
   vectors.
   ```

### Prova depois do patch (mesma cópia isolada)

Fonte parada:

```
00:40:13.456  [feed] stale probe (frame 120): colour-in SAME (14d925bc7fc781c7), output changed
00:40:26.566  [feed] MV probe (frame 600): mean |mv| 0.014 px, max 0.18 px, 29% non-zero
              <-- and the colour input did not change either: the source is still ...
00:40:26.567  [feed] Depth probe (frame 600): ... <-- depth is identically zero: this host has no
              depth buffer (window/monitor capture). Expected ...
```

Fonte com movimento (a nota de "fonte parada" não aparece, como deve ser):

```
00:40:59.251  [feed] stale probe (frame 120): colour-in changed (...), output changed (...)
00:41:28.273  [feed] MV probe (frame 1200): mean |mv| 3.991 px, max 4.63 px, 99% non-zero
```

Ressalva: esse último trecho (fonte em movimento) foi medido na build intermediária, quando o
critério da nota ainda era a última amostra de *staleness*; depois ele passou a exigir uma corrida
de três amostras, o que só o torna mais restritivo. A repetição do teste com movimento na build
final ficou bloqueada porque um `DLSS5ForAll-qa.exe` de outra pasta (`bin-teste-botoes`, PID 10464,
que não é meu) segura o mutex de instância única e o app sai no arranque. Vale refazer esse teste
quando a bancada estiver livre.

Compilado com `bash amd/feeder-amd/build-feed-amd.sh` (MinGW/msys64), sem avisos novos. O binário
testado está em `bin-teste-passes-fix\dlss5-feed.addon64`; **nada foi instalado em
`bin-teste-passes\` nem em `bin\`**.

## 5. Revisão geral (tarefa 2)

**Abre e fecha limpo.** Encerramento sem crash, `shut down cleanly.` no feeder, e os hooks NGX
saem um a um (`vtable::Unhook(... unhooked successfully)`). Os três `WARN | Add-on "X" was not
unregistered!` são benignos: o ReShade emite o aviso antes do `FreeLibrary`, e o
`unregister_addon()` acontece no `DLL_PROCESS_DETACH` (feeder: 7214; ponte: 1197).

**HookPoint continua correto.** `CorrigirHookPointNoArranque()` (main.cpp:3765) grava
`[RENODX-DLSS] DirectNeuralRenderingHookPoint` antes da criação do device — isto é, antes de o
ReShade carregar os add-ons (chamada em main.cpp:10215) — e só mexe quando o valor é `Auto`.
Confirmado no log: `neural: Hook Method = 2 (escolha preservada, nao e o Auto que quebra)`.

**Custo do Pass Count.** Medido no próprio log do usuário de 09/09 (janelas de 600 quadros com
`stalls 0`, `HookPoint=2`):

| Regime | Janela | fps médio |
|---|---|---|
| `PassCount=1` | 01:41:47-01:42:46 | 55,4 fps (53,2 / 51,5 / 57,6 / 59,1) |
| `PassCount=2` | 01:43:05 em diante | 54,8 fps (18 janelas, 51,8 a 59,3) |

Ou seja, **o segundo passe não custa fps mensurável** nesta fonte (1920x1080, DLAA, RTX 4080). O
teto ali é o ritmo de chegada do WGC, não a rede. `nr_passes=2` está coerente entre
`dlss5forall.ini:30` e `[RENODX-DLSS-preset1] DirectNeuralRenderingPassCount` no `ReShade.ini`.

**Avisos recorrentes no `ReShade.log`, todos já conhecidos e não fatais:**

- `renodx-dlss.addon64 is not listed in ADDON.LoadFromDllMain. Early DLSS hooks are not guaranteed`
  — o RenoDX pede para ser carregado no `DllMain`. Hoje o projeto não escreve essa chave em lugar
  nenhum (`LoadFromDllMain` não aparece no `main.cpp`). **Sugestão a testar, não aplicada:** acrescentar
  `[ADDON] LoadFromDllMain=renodx-dlss.addon64` ao `ReShade.ini`. Mexe no timing de carga do add-on
  que já travou o jogo uma vez (ver `dlss5-hookpoint-upscaled`), então merece sessão de teste própria.
- `Failed to find NVSDK_NGX_D3D11/D3D12_Init_with_ProjectID` e `..._EvaluateFeature_C` — o
  forwarder `_nvngx.dll` do driver não exporta essas quatro; o RenoDX fica com 8/10 e 2/4 hooks e
  funciona (`CreateFeature(Reserved18) succeeded`, `EvaluateFeature succeeded`, 49.764 avaliações).
- `Streamline interposer not found` — esperado; só afeta o modo Hudless (HookPoint 3).
- `[DLSS 5 Feed] DLSS5_Feed.fx is not loaded (technique/textures missing)` no arranque — transitório:
  é impresso antes de os efeitos compilarem e a linha seguinte já diz `technique found`. Ruído com
  cara de erro; candidato a segurar por alguns segundos antes de imprimir.

**Achados nos backups / configuração inconsistente:**

- `ReShade.ini.bak-codex-paths-20260909-014043` guarda um `ReShade.ini` com
  `EffectSearchPaths=.\`, `TextureSearchPaths=.\` e `PresetPath=.\ReShadePreset.ini`. Com esse
  arquivo em vigor **nenhum shader é encontrado e o preset `FafaDLSS5.ini` não é carregado**: nenhuma
  técnica roda, o feeder não acha o `DLSS5_Feed.fx` e sobra só o RenoDX agindo sozinho. Se essa
  variante chegou a rodar, é candidata muito forte ao "o programa ficou meio quebrado". **O
  `main.cpp` não escreve nenhuma dessas chaves** (verificado: `EffectSearchPaths` só é lido, em
  `ConferirShaders()`; `PresetPath`, `TextureSearchPaths`, `AddonPath` e `IntermediateCachePath`
  não aparecem no arquivo), então veio de fora — mão humana ou ferramenta externa. Vale conferir
  se algum script auxiliar reescreve esses caminhos.
- **Atenção, mexida de fora durante esta sessão:** `bin-teste-passes\ReShade.ini` mudou sozinho às
  `00:34:38` de hoje (de 2187 para 3221 bytes, voltando ao conteúdo de
  `ReShade.ini.bak-antes-desfazer-20260909-015309`), enquanto eu só trabalhava em
  `bin-teste-passes-fix\`. Alguém ou algum processo está mexendo nessa pasta em paralelo.
- `PreprocessorDefinitions=` está **vazio** no `ReShade.ini` de `bin-teste-passes`. Não quebra nada
  hoje porque o preset traz `PreprocessorDefinitions=DLSS5_MV_PROVIDER=3` na primeira linha
  (`FafaDLSS5.ini:1`) e o log confirma o provedor 3. Mas as definições de profundidade que existiam
  em `ReShade.ini.bak-antes-restaurar-renodx` (`RESHADE_DEPTH_INPUT_IS_REVERSED=0`,
  `..._IS_UPSIDE_DOWN=0`, `..._IS_LOGARITHMIC=0`, `..._LINEARIZATION_FAR_PLANE=1000.0`) sumiram.
  Irrelevante enquanto não houver profundidade, e uma armadilha no dia em que houver.
- **Duas chaves diferentes chamadas `nr_passes`:** a de `dlss5forall.ini` (passes do RenoDX, via
  `AplicarPassesNr()`, main.cpp:8486) e a de `dlss5-feed.cfg` (passes do caminho de Neural
  Rendering *interno do feeder*, ligado por `nr_spike`, hoje `nr_spike=0`, portanto inativo:
  `NrEvaluateFrame()` retorna imediatamente em dlss5-feed.cpp:3629). Hoje valem `2` e `1`
  respectivamente, e a divergência é inofensiva — mas o nome repetido já custou tempo de leitura.
- `dlss5-feed.cfg` tem `work_sharpness=0.45` enquanto a sessão de 09/09 rodou com `0.30`: foi
  alterado depois do teste. Nada de errado, só não confunda o log com o arquivo atual.
- **A pasta `bin\` está na arquitetura ANTERIOR à troca de add-on de 07/09** — não é só
  "binários velhos". Levantado a partir de um sintoma encontrado pelo agente do fluxo óptico (o
  `dlss5-bridge.addon64` de lá não exporta `DLSS5Bridge_SetSharedOutput`, que existe no fonte):

  | arquivo | `bin\` | `bin-teste-passes\` |
  |---|---|---|
  | `dlss5-bridge.addon64` | 05/09, 453.569 B, **sem** `DLSS5Bridge_SetSharedOutput` | 08/09, 718.457 B, com |
  | `dlss5-feed.addon64` | 06/09, 956.031 B | 11/09, 980.403 B |
  | `DLSS5ForAll.exe` | 06/09 00:33 | 08/09 18:13 |
  | `nvngx_dlssnr.dll` | 30/08 | 07/09 |
  | `renodx-dlss.addon64` (ShortFuse) | **ausente** | presente |
  | `renodx-dlss5.addon64` (Krish/Lecram, o antigo) | **presente** | ausente |

  As duas últimas linhas são o ponto: `bin\` carrega o **consumidor neural antigo**. Como
  `montar-portable.ps1` e `empacotar.ps1` leem de lá, empacotar hoje distribuiria a cadeia
  anterior — sem o `Pass Count`, sem o feed 0.12.0 e sem as correções de 11/09. Pode ser
  proposital (rede de segurança enquanto se testa em `bin-teste-passes\`); é decisão do usuário.
  **Nada foi alterado em `bin\`.**

## 6. O que NÃO foi confirmado

- **Não medi o efeito visual do `PassCount=2` na imagem**, só o custo em fps. Se a queixa de
  "quebrado" for visual (rastro/fantasma), o próximo passo é comparar capturas com `PassCount=1`
  e `2` na mesma cena em movimento, com o jogo em foco.
- **Não testei o `ReShade.ini` degradado do `bak-codex-paths` rodando**, porque isso exigiria
  escrever em `bin-teste-passes\`. A conclusão sobre ele é por leitura do arquivo, não por execução.
- **Não sei quem gravou o `ReShade.ini` degradado** nem quem o restaurou hoje às 00:34.
