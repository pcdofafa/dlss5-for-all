# Atualização da cadeia DLSS 5 a partir do RHI — 2026-09-03

O RHI (`%LOCALAPPDATA%\RHI`) foi atualizado e passou a ter versões novas dos arquivos
que a nossa pasta usa. Este documento registra o que foi comparado, o que foi trocado, a
prova de que a cadeia continua subindo, e a medição de nitidez feita antes e depois.

Nada aqui foi feito abrindo o RHI: só foram **lidos** os arquivos que ele já tinha
baixado. Nenhum backup existente foi apagado.

---

## 1. Resumo do que mudou na pasta

| Arquivo | Antes | Depois | Backup criado |
|---|---|---|---|
| `renodx-dlss5.addon64` | RenoDX DLSS5 Generic **v4.1.5** (build Aug 30 2026 22:25:38), 1 694 720 B | RenoDX DLSS5 Generic **v4.7** (build Sep 2 2026 01:15:39), 1 732 608 B | `renodx-dlss5.addon64.bak-4.1.5` |
| `nvngx_dlssnr.dll` | 310.8.SF-v2, 165 830 144 B | **mantido** (310.8.SF-v2) | `nvngx_dlssnr.dll.bak-310.8.SF-v2` |
| `nvngx_dlss.dll` | 310.8.0, 58 956 400 B | **mantido** — o RHI tem o mesmo arquivo (SHA-256 idêntico) | — |
| `dlss5-feed.addon64` | DLSS 5 Feed **0.7.0** | **mantido** — o do RHI é **0.6.0-beta.1**, mais velho | — |
| `dlss5-bridge.addon64` | build nosso, 03/09 04:29 | **mantido** — é código deste projeto, não do RHI | — |
| `dxgi.dll` (ReShade) | 6.8.0.2155 oficial | **mantido** — ver §6 | — |
| `ReShade.ini` | — | não alterado por mim | `ReShade.ini.bak-20260903-1400-antes-4.70` |

Backups gravados na própria pasta do app, todos com sufixo `.bak-`.

---

## 2. Inventário: o que é cada arquivo do RHI

### 2.1 `rdx5\renodx-dlss5.addon64` — versão 4.70 ✅ instalado

`rdx5\version.txt` = `4.70`; o add-on se identifica no log como
`RenoDX DLSS5 Generic v4.7 (build Sep  2 2026 01:15:39)`. É o mesmo add-on que já
usávamos, duas versões à frente.

O que mudou dentro dele (diferença de strings entre os dois binários):

- **Pool de *worksets* de NR seguro para a GPU**: até 4 gerações de scratch, reciclagem
  por *fence* exata de fila, e ordenação da história temporal do `feature 18` entre filas
  DIRECT. Antes não havia nada disso. É correção de sincronização — exatamente a classe
  de bug que produz artefato temporal.
- **Rastreador de estado do host híbrido**: pré-história leve de PSO/root-signature cobre
  o primeiro uso do DLSS; depois de observado o DLSS, cada Reset bem-sucedido matricula a
  lista viva. A 4.1.5 simplesmente pulava o NR quando o estado estava incompleto.
- **Codec de cor novo**: `created reversible NR color bridge pipeline (SDR sRGB / linear
  HDR BT.709 / PQ BT.2020)` no lugar do `Control-equivalent soft-clip/sRGB/UpgradeToneMap
  codec`. Traz compressão de gamute reversível em torno do eixo neutro D65.
- **Duas opções novas** em `[RenoDX.DLSS5]`: `NRGlobalTone` e `NRDiffuseWhiteNits`.
  **Nenhuma chave antiga foi removida** — o nosso `ReShade.ini` continua válido.
- Mudança de rótulos na overlay: *Local Tone Strength* → *Local Tone Intensity*,
  *Scene Paper-White Scale* → *Diffuse White (nits)*.

⚠️ **Mudança de semântica que importa**: a linha de estado ativo passou de

```
4.1.5:  upscaling=OFF intensity=1 color_strength=1 transfer=1 paper_white=16 preset=0 style=0
4.70:   upscaling=OFF intensity=1 global_tone=1 diffuse_white_nits=203 preset=0 style=0
```

O branco de referência deixou de ser um **fator** (`NRPaperWhiteScale=16`) e passou a ser
**nits absolutos** (`NRDiffuseWhiteNits`, padrão 203). O `NRPaperWhiteScale=16` que está
no nosso `ReShade.ini` é ignorado pela 4.70. Isso é relevante para
`docs/borrao-e-nitidez.md` §3, que manteve aquela chave por ser "inerte em SDR" — na 4.70
ela é inerte em tudo, e quem manda é um valor que não está escrito no ini.

### 2.2 `rdx5\renodx-dlss.addon64` — "RenoDX DLSS" 0.51 ❌ não instalado

`rdx5\version-sf.txt` = `0.51`, baixado 03/09 04:13. **Não é uma versão nova do nosso
add-on: é outro add-on.** Registra-se como `RenoDX DLSS` (API ReShade 18; o nosso usa 20)
e é muito mais amplo: controla modo de qualidade do DLSS, *hint* de render preset,
frame generation, Streamline, e tem um "Direct Neural Rendering" próprio com opções que o
nosso não tem (`DirectNeuralRenderingPassCount` de 1 a 10, `Style` com Modelos A/B/C,
`DirectNeuralRenderingHookPoint`, `...Encoding`, `...UiCorrectionMode`).

Foi testado de verdade (run **E**), instalado com o nome de arquivo que a nossa ponte
procura. Dois motivos duros para **não** adotá-lo hoje:

1. **Quebra o Neural Rendering sem teclado.** Log de 12:37:49:
   `[DLSS 5 Bridge] o RenoDX nao importa USER32!GetAsyncKeyState pelo nome (versao nova?):
   alternancia do NR indisponivel.` A ponte (`addon/dlss5_bridge_addon.cpp`) reescreve a
   IAT desse símbolo — é assim que o F6 e o painel ligam o neural. Com o 0.51 o botão do
   painel deixa de funcionar.
2. **Não produziu NR nenhum no nosso cenário.** Não houve criação de `feature 18` /
   `Reserved18`; o screenshot saiu do mesmo tamanho do run sem NR (2 135 507 B contra
   2 135 574 B do run F, e 3,7–3,8 MB nos runs com NR). O "Direct NR" dele é opt-in por
   configuração e pressupõe um jogo com DLSS nativo, não a nossa sessão sintética.

Fica registrado como **candidato futuro**: se um dia quisermos `PassCount` ou os modelos
A/B/C, o caminho é ajustar a ponte (o lead decide; não mexi em `main.cpp`).

### 2.3 Runtimes `nvngx_dlssnr.dll` — os três do RHI

`dlss_manifest.json` (atualizado hoje 12:13) lista exatamente três, e o RHI já baixou os
três:

| Versão | SHA-256 (16 primeiros) | Tamanho | Resultado no teste |
|---|---|---|---|
| `310.8.0` | `E16BCF15E16E13F5` | 165 840 496 | ❌ **`feature 18 create failed with 0xbad00001`** |
| `310.8.SF` | `4C5BD1171C7336B4` | 165 830 144 | ✅ funciona |
| `310.8.SF-v2` | `6EB209E764F39872` | 165 830 144 | ✅ funciona — **é o que usamos** |

O detalhe que engana: **`310.8.0` é o build que o add-on considera "de referência"** — o
SHA-256 `E16BCF15…` está gravado dentro do próprio `renodx-dlss5.addon64`, e com ele o log
diz `signed runtime sha256 … (reference match)`, enquanto com o SF-v2 diz
`(custom runtime accepted; untested build, NR failures may be specific to it)`.

Mesmo assim, **o "de referência" é o único dos três que não funciona nesta máquina**
(RTX 4080, driver 596.49): ele inicializa (`signed DLSSNR 310.8.0 D3D12 runtime
initialized`) e falha na criação do feature. O aviso de "untested build" no nosso log é,
portanto, ruído — não é sinal de problema. Não trocar.

### 2.4 O que existe no RHI e não se aplica a nós

- `DLSS-D\310.7.129\nvngx_dlssd.dll` (Ray Reconstruction) e `DLSS-G\310.8.0\nvngx_dlssg.dll`
  (Frame Generation): a nossa cadeia só usa SuperSampling (DLAA) + NR. Não entram.
- `addons\DLSS5 Feeder.addon64` (0.6.0-beta.1, 30/08) — **mais velho** que o nosso 0.7.0.
- `addons\DLSS5 DX11 Bridge.addon64` (30/08) — é o add-on de terceiro; o nosso
  `dlss5-bridge.addon64` é código deste projeto, com outra função. Não substituir.
- `addons\RenoDX Upgrade.addon64`, `downloads\renodx\*` — mods por jogo (Witcher 3,
  Silksong, AC Shadows…), não têm relação com a captura.

---

## 3. Como foi medido

Trocar versão sem medir é apostar. O aparato:

1. **Alvo sintético 3440×1200**, gerado por script: grade de linhas de 1 px, *zone plate*
   (frequência crescente do centro para fora), texto Consolas de 8 a 20 px, barras de
   alto contraste, ruído determinístico e rampas de cor. Exibido numa janela sem borda,
   1:1, com um piscador de 33 ms num canto fora das regiões medidas — só para o WGC
   continuar entregando frames novos numa cena estática.
2. **O app espelha essa janela** (`--window ALVOFAFA --fullscreen --no-vsync`). Como o
   backbuffer segue o tamanho da fonte, o mapeamento é **pixel a pixel**: qualquer
   diferença contra o alvo original foi a cadeia que introduziu.
3. Depois de ~26 s (NR já armado e com histórico), o **screenshot do ReShade** grava o
   backbuffer em PNG. O `F5` do próprio add-on grava, além disso, o par
   `[pre-NR]` / `[NR on]` **do mesmo frame** — é o A/B mais limpo possível, sem variação
   entre execuções.
4. Métricas contra a imagem original, por região, depois de alinhar com busca de
   deslocamento ±3 px:
   - `grad%` — energia de gradiente (|dx|+|dy|) em luma BT.709, em % da referência;
   - `lap%` — variância do laplaciano, em % da referência (mais sensível ao borrão);
   - `rmse` — erro quadrático médio em luma contra a referência.

> ⚠️ **Não repita este método como está.** Dois detalhes dele atrapalharam o usuário, que
> estava usando a máquina: o `--fullscreen` cobre a tela por ~30 s a cada execução, e o
> `PrintScreen` enviado por `keybd_event` abre a Ferramenta de Captura do Windows por cima
> de tudo (no Windows 11 é o comportamento padrão da tecla). A forma correta de repetir a
> medição é: janela-alvo própria, o app em modo espelho **sem** `--fullscreen`, e a captura
> feita por `PrintWindow`/`BitBlt` no HWND do app — nunca por tecla. Um run com
> `--source monitor` (12:24) também não deveria ter sido feito. Ver a regra em
> `memory/nunca-abrir-o-app-sobre-a-tela-do-usuario.md`.

Constantes de todos os runs, verificadas por hash a cada execução: `DLSS5ForAll.exe`
`C1AFAAA6…` (não foi recompilado durante a bateria), `ReShade.ini` com os valores novos
do `docs/borrao-e-nitidez.md` (`NRIntensity=1`, `NRLocalTone=1`, `NRLocalStructure=1`,
`NRSkinStructure=-1`, `NRStyle=0`), `dlss5-feed.cfg` com `preset=1`, monitor em HDR.

---

## 4. Resultados

17 execuções. `grad%`/`lap%` = quanto da estrutura da imagem original sobreviveu.

| # | cadeia | grade 1 px | zone plate | texto 8–20 px | rmse |
|---|---|---|---|---|---|
| F5 off | **sem NR** (pre-NR do mesmo frame) | 69,5 / 33,2 | 98,8 / 66,7 | 76,5 / 40,2 | **1,93** |
| F | sem add-on de NR | 69,6 / 33,4 | 98,7 / 65,4 | 76,4 / 40,3 | 1,93 |
| C | 4.70 + NR 310.8.0 (NR falhou) | 69,5 / 33,3 | 98,8 / 66,7 | 76,5 / 40,2 | 1,93 |
| E | add-on 0.51 (não fez NR) | 69,6 / 33,4 | 98,7 / 65,3 | 76,5 / 40,3 | 1,93 |
| A2 | **4.1.5** + SF-v2 | 61,3 / 25,7 | 83,8 / 46,9 | 77,5 / 36,6 | 3,97 |
| A3 | **4.1.5** + SF-v2 | 61,1 / 25,6 | 83,8 / 46,7 | 77,5 / 36,6 | 3,96 |
| B | **4.70** + SF-v2 | 61,1 / 25,5 | 83,8 / 46,6 | 77,7 / 36,6 | 4,00 |
| B2 | **4.70** + SF-v2 | 61,1 / 25,6 | 83,8 / 47,0 | 77,6 / 36,5 | 3,96 |
| B3 | **4.70** + SF-v2 | 61,3 / 25,7 | 83,9 / 46,7 | 77,6 / 36,6 | 3,95 |
| D | 4.70 + NR 310.8.SF | 61,2 / 25,6 | 83,9 / 46,8 | 77,0 / 36,6 | 3,96 |
| F5 on | 4.70, NR on do mesmo frame | 61,3 / 25,7 | 83,8 / 46,8 | 77,4 / 36,5 | 3,92 |
| G80 | 4.70, `NRDiffuseWhiteNits=80` | 61,2 / 25,6 | 83,8 / 46,9 | 77,5 / 36,6 | 3,91 |
| G400 | 4.70, `NRDiffuseWhiteNits=400` | 61,1 / 25,5 | 83,6 / 46,6 | 77,5 / 36,4 | 4,00 |
| H | 4.70 + **`DLSS5_Sharpen` ativo** | 71,1 / 36,4 | 84,1 / 59,9 | 85,4 / 48,1 | **3,45** |

### 4.1 A versão nova não muda a imagem

`4.1.5` (A2, A3) e `4.70` (B, B2, B3) ficam dentro de **0,3 %** uma da outra em todas as
seis métricas — menos que a variação entre duas execuções da *mesma* versão. **A 4.70 não
é melhor nem pior em qualidade de imagem. Não há regressão.** O que ela traz são as
correções de sincronização de GPU da §2.1, que não aparecem numa cena estática mas
existem, e é por elas que ela vale a atualização.

> **Um susto que valeu a pena investigar.** A primeira execução com a 4.1.5 (run "A", às
> 12:30) deu 71,2 / 36,4 — muito melhor que tudo o mais — e, se eu tivesse parado ali,
> teria concluído que a 4.70 piorou a nitidez. Ela não piorou: naquele run o
> `DLSS5_Sharpen` estava ativo no preset, e o `FafaDLSS5.ini` foi regravado **sem ele** às
> 12:31:24, no encerramento daquela mesma sessão. A prova é o run **H**, que reproduziu o
> A ao decimal (71,1 / 36,4 / 84,1 / 59,9 / 85,4 / 48,1 contra 71,2 / 36,4 / 84,1 / 59,7 /
> 85,2 / 48,0) apenas religando o filtro. Sem repetir A duas vezes (A2, A3) o relatório
> teria saído errado.

### 4.2 O Neural Rendering custa nitidez, e o quanto está medido

Comparando o par `[pre-NR]` / `[NR on]` do **mesmo frame** (F5):

| | sem NR | com NR | variação |
|---|---|---|---|
| grade de 1 px — gradiente | 69,5 % | 61,1 % | **−12 %** |
| grade de 1 px — laplaciano | 33,2 % | 25,5 % | **−23 %** |
| zone plate — gradiente | 98,8 % | 83,8 % | **−15 %** |
| zone plate — laplaciano | 66,7 % | 46,7 % | **−30 %** |
| texto — gradiente | 76,5 % | 77,5 % | +1 % |
| texto — laplaciano | 40,2 % | 36,5 % | **−9 %** |
| erro contra a original (RMSE) | 1,93 | 4,01 | **+108 %** |

A assinatura do borrão está na linha do texto: o gradiente de primeira ordem **sobe** e o
laplaciano **cai**. Isso é exatamente "borda mais marcada, detalhe fino apagado" — a
imagem parece nítida de longe e macia de perto. É a queixa do usuário, medida.

No fundo liso a diferença também aparece: sem NR o fundo sai **bit a bit igual** ao
original (média 24,29, desvio 0,000); com NR o preto desloca para 23,29 e ganha ruído
(desvio 0,04–0,07). Pouco, mas não é zero — a rede reescreve a imagem inteira, inclusive
onde não havia nada para reconstruir.

Comparação visual (recorte do texto de 8 a 20 px, ampliado 2× sem interpolação):
`docs/img/cadeia-referencia.png`, `cadeia-nr-off.png`, `cadeia-nr-on.png`,
`cadeia-nr-on-sharpen.png`.

### 4.3 O filtro de nitidez do projeto é o item de maior retorno

O run **H** (mesma 4.70, apenas com `DLSS5_Sharpen` de volta ao preset) fica **melhor que
a cadeia sem NR nenhum**: grade 71,1 % contra 69,5 %, laplaciano do texto 48,1 % contra
40,2 %, e o erro cai de 3,96 para 3,45.

⚠️ **E ele não está ativo.** O `FafaDLSS5.ini` de agora tem
`Techniques=Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_Feed@DLSS5_Feed.fx` — sem o Sharpen.
A ativação registrada em `docs/borrao-e-nitidez.md` §6 foi desfeita pelo próprio app ao
encerrar (mesmo mecanismo do `AutoSavePreset=1` já descrito lá para o `ReShade.ini`).
Avisado ao agente `nitidez`; a correção é no código, não aqui.

### 4.4 As opções novas da 4.70 são inertes neste conteúdo

`NRDiffuseWhiteNits` a 80, 203 (padrão) e 400 dá o mesmo resultado dentro do ruído
(G80, B, G400). Faz sentido: o conteúdo é SDR. A chave importa se um dia a captura for
HDR de verdade — e aí vale lembrar que `NRPaperWhiteScale` deixou de existir na prática.

---

## 5. Prova de que a cadeia sobe (run final, 12:46)

Combinação que ficou na pasta — RenoDX 4.70 + NR 310.8.SF-v2 + a ponte recompilada hoje
às 12:44 (`74942988…`):

```
12:46:08:699 | INFO | [DLSS 5 Neural Rendering] RenoDX DLSS5 Generic v4.7 (build Sep  2 2026 01:15:39) loaded
                      (hotkeys: NR toggle F6, screenshot F5) | EnableHooks=2
12:46:10:833 | INFO | [DLSS 5 Bridge] alternancia do Neural Rendering pronta
                      (tecla do RenoDX = 0x75, sem teclado e sem foco)
12:46:14:633 | INFO | signed DLSSNR 310.8.0 D3D12 runtime initialized
12:46:14:857 | INFO | feature 18 created via the signed snippet after DLSS/DLAA
                      for NR input 3440x1200 -> output 3440x1200 with guides 3440x1200
12:46:14:857 | INFO | inline feature 18 evaluation succeeded (count=1, ...)
12:46:17:643 | INFO | inline feature 18 evaluation succeeded (count=60, ...)
```

```
12:46:14.521  [feed] feature ready: 3440x1200 DLAA, flags=66 (SDR MVLowRes AutoExposure)
12:46:14.865  [feed] frame 1 delivered (3440x1200 at 100% -> 3440x1200, reset=1)
12:46:14.957  [feed] frame 2 delivered (...)
```

Os quatro critérios do briefing estão atendidos, inclusive **a linha da ponte** — a 4.70
continua importando `USER32!GetAsyncKeyState` pelo nome, então o F6 e o botão do painel
seguem funcionando sem teclado.

Os dois `ERROR | vtable::Hook(Failed to find NVSDK_NGX_D3D12_EvaluateFeature_C)` que
aparecem no log são **anteriores à atualização** (estão idênticos no baseline de 12:10 com
a 4.1.5) e não impedem nada: o caminho usado é o *inline*, e ele funciona.

---

## 6. O que ficou de fora, e por quê

| Item | Decisão | Motivo |
|---|---|---|
| `nvngx_dlssnr.dll` 310.8.0 | ❌ não trocar | falha em criar o `feature 18` (`0xbad00001`) nesta máquina, apesar de ser o build "de referência" |
| `nvngx_dlssnr.dll` 310.8.SF | ❌ não trocar | funciona, mas é idêntico ao SF-v2 em todas as métricas (run D) — trocar não traz nada |
| `renodx-dlss.addon64` 0.51 | ❌ não instalar | quebra o Neural Rendering sem teclado e não produziu NR (§2.2) |
| ReShade nightly | ❌ não trocar | `reshade-nightly\ReShade64.dll` é `6.8.0.2` **UNOFFICIAL**, do nightly de 02/09 23:13; o nosso `dxgi.dll` é `6.8.0.2155` oficial, mesma versão de produto. Trocar um build oficial por um não-oficial de mesma versão é risco sem ganho identificável |
| `DLSS5 Feeder` do RHI | ❌ não trocar | 0.6.0-beta.1 contra o nosso 0.7.0 |
| DLSS-D / DLSS-G | ❌ fora de escopo | a cadeia não usa Ray Reconstruction nem Frame Generation |
| `C:\SWITCH-DLSS5\` e `C:\PCSX2-DLSS5\` | ⚠️ não mexi | as duas têm a cadeia antiga (RenoDX 4.1.5 + SF-v2, hashes idênticos aos nossos de ontem). Ficaram para trás; atualizá-las é decisão do usuário, não é a pasta do app |

---

## 7. Backups e como reverter

```powershell
cd "C:\Users\PCDOFAFA\Desktop\dlss cam"
Copy-Item renodx-dlss5.addon64.bak-4.1.5      renodx-dlss5.addon64 -Force   # volta ao add-on antigo
Copy-Item nvngx_dlssnr.dll.bak-310.8.SF-v2    nvngx_dlssnr.dll     -Force   # runtime (não mudou; backup por precaução)
Copy-Item ReShade.ini.bak-20260903-1400-antes-4.70  ReShade.ini    -Force   # ini como estava antes da troca
```

Nenhum backup pré-existente foi apagado. Os arquivos criados por mim:

- `renodx-dlss5.addon64.bak-4.1.5`
- `nvngx_dlssnr.dll.bak-310.8.SF-v2`
- `ReShade.ini.bak-20260903-1400-antes-4.70`
- `ReShade.ini.bak-20260903-1245-antes-limpeza`
- `ReShade.log.bak-baseline-4.1.5`, `dlss5-feed.log.bak-baseline-4.1.5`
- `FafaDLSS5.ini.bak-20260903-antes-teste-sharpen`

### Efeitos colaterais dos testes, todos revertidos

- O run E (add-on 0.51) criou a seção `[RENODX-DLSS]` no `ReShade.ini`
  (`DLSSAutoExposure=0`, `DLSSQualityMode=0`). **Removida**, com backup antes.
- O teste de `NRDiffuseWhiteNits` gravou a chave no `ReShade.ini`. **Removida** ao fim.
- O run H mudou `Techniques` do `FafaDLSS5.ini`. **Devolvido** ao conteúdo original.
- Os dois `.ini` ficaram com BOM UTF-8 depois de serem reescritos por script. **BOM
  removido** dos dois.
- A pasta `DLSS5 Screenshots\` (criada pelo `F5` do add-on) está vazia; os PNGs de teste
  foram levados para fora da pasta do app.

---

## 8. Resposta curta

O add-on foi atualizado de 4.1.5 para 4.70 e a cadeia sobe igual, com a ponte do neural
funcionando. A imagem é a mesma: a versão nova **não conserta o borrão**, e não era de se
esperar que consertasse — o borrão não está na versão do add-on, está no Neural Rendering
em si, e agora está medido: ele apaga entre 23 % e 30 % do detalhe fino e dobra o erro
contra a imagem original.

Os três runtimes de NR do RHI foram testados e o que já estava instalado (`310.8.SF-v2`)
é o certo — o "oficial de referência" nem cria o recurso nesta placa.

O que **realmente** devolve nitidez, medido: religar o `DLSS5_Sharpen`. Com ele a imagem
final fica melhor do que a cadeia sem Neural Rendering nenhum. Ele está desligado no
preset porque o app o apaga ao encerrar — isso é código, e está com o agente `nitidez`.
