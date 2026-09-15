# Migração da pasta de produção `bin\` para a arquitetura nova

**Data:** 2026-09-11
**Motivo:** `DLSS5ForAll-AMD\bin\` estava na arquitetura **anterior** à troca de add-on de
07/09 — não apenas com binários velhos. Carregava o consumidor neural antigo
(`renodx-dlss5.addon64`, Krish/Lecram) e não tinha o `renodx-dlss.addon64` (ShortFuse). Como
`montar-portable.ps1` e `empacotar.ps1` leem de lá, empacotar distribuiria a cadeia antiga.

**Resultado: migração concluída e verificada em execução real.** Um achado de painel apareceu
no caminho, sem relação com a migração (§6).

---

## 1. Backup

`D:\backup-bin-antes-migracao-20260911\` — **12.373 arquivos**, contagem conferida contra a
origem. Inventário com MD5, tamanho e data de cada arquivo em `bin-antes.csv` (scratchpad da
sessão). Restauração é uma cópia de volta.

## 2. Um risco encontrado antes de tocar em nada: hardlinks

Todo arquivo de `bin\` tinha **3 hardlinks**. O mesmo inode é compartilhado com
`bin-camada2\` e `bin-camada3\` — as pastas das camadas 2 e 3 do app:

```
bin           inode=1970324838969588   956031 bytes
bin-camada2   inode=1970324838969588   956031 bytes
bin-camada3   inode=1970324838969588   956031 bytes
```

Sobrescrever por cima (`Copy-Item` sobre o destino) trunca **o inode compartilhado** e mudaria
as três pastas de uma vez, pelas costas. O método correto é **remover primeiro** (o que só
decrementa a contagem de links, deixando as outras duas intactas) e **depois** copiar — o novo
arquivo nasce como inode independente.

Isso é seguro porque o app remonta as camadas a cada uso. `main.cpp:4157-4160`:

```cpp
// link rigido novo a cada montagem: se o arquivo de origem mudou (atualizacao), o link
// antigo apontaria para o conteudo velho. Se o volume nao aceitar link, copia.
DeleteFileW(dst.c_str());
if (!CreateHardLinkW(dst.c_str(), src.c_str(), nullptr)) CopyFileW(src.c_str(), dst.c_str(), FALSE);
```

Foi esse o método usado em todos os arquivos substituídos.

## 3. Rebuild a partir da fonte

| binário | script | resultado |
|---|---|---|
| `DLSS5ForAll.exe` | `amd\build-mingw.sh` | 2.677.392 B |
| `dlss5-feed.addon64` | `amd\feeder-amd\build-feed-amd.sh` | 980.403 B |
| `dlss5-bridge.addon64` | `amd\addon\build-addon.sh` | 718.457 B |

**Export obrigatório conferido com `objdump -p`** antes de instalar — era a condição para o modo
`sintetizar` não cair silenciosamente no `segurar`:

```
[   3] +base[   4]  0003 DLSS5Bridge_SetExternalMV
[   4] +base[   5]  0004 DLSS5Bridge_SetSharedOutput   <-- presente
```

**Sobre o feed instalado:** foi usado o binário de `bin-teste-passes` (MD5 `5A00D4003E2F…`) em
vez do recém-compilado. Mesmo código-fonte — a recompilação só muda o timestamp embutido no PE
— mas aquele é o binário **validado em execução real** hoje, no Witcher 3 e no Hollow Knight.
Ambos foram conferidos e contêm o fix do NGX (string `not re-initialising NGX` presente nos dois).

## 4. Tabela antes/depois

| arquivo | antes (MD5/bytes) | depois (MD5/bytes) |
|---|---|---|
| `DLSS5ForAll.exe` | `99815A8D45C6` / 2.452.553 | `DF41C3454A28` / 2.677.392 |
| `dlss5-feed.addon64` | `719A45DF5B17` / 956.031 | `5A00D4003E2F` / 980.403 |
| `dlss5-bridge.addon64` | `5B9E09FBCA0F` / 453.569 | `54CFB4C4E35A` / 718.457 |
| `renodx-dlss.addon64` | **ausente** | `174743013DC1` / 2.499.584 |
| `renodx-dlss5.addon64` (antigo) | `AFA85A271B8C` / 1.732.608 | **REMOVIDO** (está no backup) |
| `nvngx_dlssnr.dll` | `8FC583152592` / 165.830.144 | `900D8C311FB2` / 165.840.496 |
| `dlss5-feed.cfg` | `CBA1C33A425A` / 186 | `427AB9A1A1CD` / 451 |
| `reshade-shaders\Shaders\DLSS5_HUD.fx` | **ausente** | `C3BDDD0713BD` / 12.116 |
| `dxgi.dll` | `B1DE41B152B9` | `B1DE41B152B9` — **não tocado** |

A pasta `reshade-shaders\` foi substituída inteira: **13 → 14 `.fx`** (entrou o `DLSS5_HUD.fx`),
27 arquivos ao todo.

## 5. Configuração: o que mudou e o que foi preservado

**Atualizado (é da arquitetura):**
- `ReShade.ini`: `OverlayCollapsed` passou de `DLSS 5 Feed 0.7.0` para
  `DLSS 5 Feed 0.12.0@dlss5-feed.addon64,DLSS 5 Bridge@dlss5-bridge.addon64`.
- `ReShade.ini`: a seção **`[RenoDX.DLSS5]` do add-on antigo foi removida** e no lugar entraram
  `[RENODX-DLSS]` (`DirectNeuralRenderingHookPoint=2`) e `[RENODX-DLSS-preset1]`.
- `FafaDLSS5.ini`: `Techniques=` e `TechniqueSorting=` ganharam `DLSS5_HUD` — sem isso o
  `DLSS5_HUD.fx` recém-instalado nunca rodaria. Acrescentada a seção `[DLSS5_HUD.fx]` com os
  padrões (`HUD_RESTORE_ENABLE=0`, desligado).

**Preservado (é da pessoa):** `dlss5forall.ini` inteiro (placa, idioma, `painel_x/y`, fonte,
`nr_passes`, `nr_estilo`, `dlss_camadas`), todos os valores de uniforms do `FafaDLSS5.ini`
(Sharpness, SMAA, Lumenite, DLSS5_Feed), `ofertas.ini`, e as preferências de overlay do
`ReShade.ini` (`ShowFPS`, `KeyOverlay`, docking, estilo).

**Uma observação sobre o `PassCount`:** foi gravado `=2`, mas o app o reescreveu para `1` no
primeiro arranque, e **está correto**. O valor efetivo vem de `nr_passes` no `dlss5forall.ini`,
que é preferência da pessoa e hoje vale `1` (`AplicarPassesNr`, main.cpp:8486). Para o
`PassCount` valer 2 de forma estável seria preciso mudar `nr_passes=2` no ini dela — decisão
dela, não da migração. O mesmo vale para `DirectNeuralRenderingStyle`, que seguiu `nr_estilo=0`.

## 6. Verificação em execução real

`bin\DLSS5ForAll.exe` aberto com `--outra-instancia --window "Bloco de notas"`, fonte um Bloco
de Notas. Tudo o que foi pedido, conferido:

```
Registered add-on "DLSS 5 Bridge"       v0.0.0.0   ReShade API 20
Registered add-on "DLSS 5 Feed 0.12.0"  v0.12.0.1  ReShade API 20
Registered add-on "RenoDX DLSS"         v0.0.0.0   ReShade API 18
```
- **O add-on antigo não aparece** — zero ocorrências de `renodx-dlss5` no `ReShade.log`.
- **14 shaders compilados** (era 13).
- `neural: Hook Method = 2 (escolha preservada, nao e o Auto que quebra)`.
- `[feed] effects: ... DLSS5_MV_PROVIDER=3 (LumeniteFX Kernel) -> Lumenite_Kernel (enabled)`
- `[feed] feature ready: 1904x1012 DLAA, flags=66 (SDR MVLowRes AutoExposure)`
- **A cadeia neural nova está ativa:**
```
[RenoDX] DLSS-NR direct: CreateFeature(Reserved18) succeeded: size=1904x1012 preset=1
[RenoDX] DLSS-NR direct: EvaluateFeature succeeded: evaluation=1 options_revision=1
RenoDX DLSS-NR source evaluation completed: source=1 replace_source=true
```
`evaluation=1` (e não 1..2) é consequência de `nr_passes=1`, como explicado na §5.

## 7. Spot-check do painel: o "não cabe na tela" era ARTEFATO DE DPI — retratação

Com `DLSS5_LOG_ITENS=1`, o painel registrou 54 itens, o mais baixo em `Y=1175`:

```
painel aberto em 2750,270  453x1004  visivel=1  itens=54  escala=0.80 (dpi 0.80)
```

**Eu relatei que cinco itens caíam fora da tela. Estava errado, e o erro foi meu: comparei
coordenadas FÍSICAS com alturas LÓGICAS.** O `DLSS5_LOG_ITENS` despeja coordenadas de tela
**físicas**, mas eu li a geometria dos monitores com `Screen.AllScreens` a partir de um
PowerShell **DPI-unaware**, que devolve as alturas virtualizadas:

| | lógico (o que eu usei — errado) | **físico (o correto)** |
|---|---|---|
| secundário (ultrawide) | 2293x960 | **3440x1440**, área útil **3440x1368**, DPI 144 (150%) |
| primário | 1536x864 | **1920x1080**, área útil **1920x1020**, DPI 120 (125%) |

Medido de novo com `SetProcessDpiAwarenessContext(-4)` antes de qualquer chamada de geometria,
e comparando com o `rcWork` do monitor onde o painel realmente está (o `X=2750` cai no
secundário, cuja faixa física é `X 1920..5360`):

```
painel:  X 2750..3203   Y 270..1274
monitor: X 1920..5360   área útil Y 0..1368

itens fora da área útil física (Y > 1368):  NENHUM
item mais baixo:        Y = 1175   -> folga de 193 px
borda inferior do painel: Y = 1274 -> folga de  94 px
X dos itens: 2822..3153            -> dentro de 1920..5360
```

**Está tudo dentro da tela, com folga.** O "Mais opções" inclusive. Não há problema de painel,
e não há nada a corrigir aqui.

Este é o **mesmo artefato** que já havia produzido dois falsos positivos idênticos hoje (o
agente `testar-botoes` retratou um "atrás da barra de tarefas" que era 912 lógico contra 1368
físico; ver `RELATORIO-BOTOES.md`, FALHA 1-b). Fica registrado para que ninguém o repita:

> **Ao conferir geometria de janela nesta máquina, declare DPI-awareness antes de medir.** Um
> processo DPI-unaware recebe coordenadas virtualizadas, e compará-las com os números que o app
> despeja (físicos) inventa um transbordo que não existe. Em PowerShell:
> `SetProcessDpiAwarenessContext((IntPtr)(-4))` antes de `Screen.AllScreens`,
> `GetSystemMetrics` ou `MonitorFromPoint`; e prefira `GetMonitorInfo`/`rcWork` à área total,
> porque é o `rcWork` que já desconta a barra de tarefas.

## 8. Estado final

Migração completa e funcional. Nenhum passo falhou, nada ficou pela metade. Backup íntegro em
`D:\backup-bin-antes-migracao-20260911\` (12.373 arquivos) e o add-on antigo preservado lá.
Os processos de teste foram encerrados apenas pelos PIDs criados nesta sessão.
