# Promoção da release à produção (`DLSS5ForAll-AMD\`)

**Data:** 2026-09-11, 06:34–06:45
**Fonte:** `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\amd\`
**Produção:** `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\DLSS5ForAll-AMD\` (app em `bin\`, launcher na raiz)

**Resultado: promoção concluída, incluindo o feeder novo, que passou na validação de 90 s.**
Um achado de configuração apareceu na conferência final e foi corrigido (§6).

---

## 0. Pré-condição

`DLSS5ForAll.exe` **não estava em execução** (`Get-Process`, só leitura — nada foi encerrado).
Havia quatro processos `alvo_animado` de outros agentes; **não foram tocados**, e por isso o teste
da §5 endereçou a janela por **HWND** e não por título (todos se chamam `ZEBRA7`, e casar por
título teria sequestrado o alvo de outra pessoa).

## 1. Backup

`D:\backup-bin-antes-release-20260911-0634\`

| conteúdo | itens |
|---|---|
| `bin\` inteira (robocopy /E) | **13.871 arquivos**, 310,49 MB — contagem conferida contra a origem |
| launcher da raiz | `DLSS5ForAll.exe`, `LEIA-ME.txt`, `README.txt`, `WebView2Loader.dll` |

Restauração é uma cópia de volta.

## 2. Método: remover antes de copiar

Todo arquivo substituído foi **removido primeiro** e só depois copiado, pelo motivo registrado em
`RELATORIO-MIGRACAO-BIN.md` §2: sobrescrever por cima trunca o inode compartilhado com
`bin-camada2\` e `bin-camada3\` e mudaria as três pastas de uma vez.

Conferido com `fsutil hardlink list` **antes** de tocar em cada um: nesta rodada os cinco alvos já
tinham **1 link só** (a migração de hoje de manhã os recriou como inodes independentes), então o
risco não se materializaria. O método foi mantido assim mesmo — a contagem de links é estado que
muda a cada remontagem das camadas, e depender dela seria depender de sorte.

## 3. Hashes de origem conferidos ANTES de copiar

Os dois esperados bateram exatamente. Nenhuma promoção foi feita às cegas.

| origem | SHA-256 | esperado |
|---|---|---|
| `amd\DLSS5ForAll.exe` | `60DB213BE057B5E3…D358B139` | confere |
| `amd\dlss5-bridge.addon64` | `35C39EF540009467…EFF7AE96` | confere |

**Onde estava o bridge:** em `amd\dlss5-bridge.addon64` (raiz da fonte), não em `amd\build\`.
Foi localizado por varredura recursiva, e é o único arquivo com esse nome na fonte.

**Launcher:** `amd\portable-launcher\DLSS5ForAll.exe` com mtime **11/09/2026 06:22:52** — posterior
ao piso de 06:20 e também posterior ao `launcher.cpp` (06:01:44) e ao `launcher.rc` (05:43:43), ou
seja, compilado a partir do fonte atual. **Não foi preciso recompilar.**

**LEIA-ME:** `amd\portable-launcher\LEIA-ME.txt` é o mesmo arquivo do launcher em produção —
mesmo cabeçalho e mesma abertura, versão ampliada (13.471 B contra 12.334 B). Promovido.

## 4. Arquivos trocados

| arquivo | antes (SHA-256 / bytes) | depois (SHA-256 / bytes) |
|---|---|---|
| `bin\DLSS5ForAll.exe` | `D1A0172C702BD9B9…47107070` / 2.677.392 | `60DB213BE057B5E3…D358B139` / **2.917.861** |
| `bin\dlss5-bridge.addon64` | `92C70BE72FA0E9EC…F462AC49F` / 718.457 | `35C39EF540009467…EFF7AE96` / **719.214** |
| `bin\dlss5-feed.addon64` | `036D1E7EB07F892B…A02FA11A` / 980.403 | `207E173AA0EB9E8F…C918B0F2` / **994.756** |
| `bin\ofertas.ini` | `0D9D3703A58858BE…8BE311B1` / 8.780 | `7F480487AF0A5AF1…6007F114` / **8.813** |
| `DLSS5ForAll.exe` (launcher, raiz) | `A38160D89BA683AE…7B5658B7` / 1.808.875 | `DBA166A0F8F5E6C9…C250A87B` / **2.160.656** |
| `LEIA-ME.txt` (raiz) | `18F3996D5DAD7984…DF519B0F` / 12.334 | `10914864DCA287E8…3D2C7C0A` / **13.471** |

Cada destino foi re-hasheado **depois** da cópia e comparado com a origem: os seis bateram.
`bin\` segue com 13.871 arquivos.

**Não tocados:** `renodx-dlss.addon64` (`85EAE478…`, 2.499.584 B), `dxgi.dll` (`0CEE63F9…`,
5.592.064 B), `WebView2Loader.dll` da raiz (idêntico byte a byte ao da fonte, `5D8D6B66…` nos dois
— conferido, nada a fazer).

## 5. O feeder novo: validado numa cópia antes de entrar

O `dlss5-feed-amd.addon64` do `residual-nr` (DRED/passthrough/correções, `207E173A…`, 994.756 B)
**não foi promovido direto**. Foi validado em `DLSS5ForAll-AMD\bin-teste-release\`, cópia
independente da `bin\` já promovida (`Copy-Item -Recurse`, 13.871 arquivos, 1 hardlink por arquivo
— conferido, não compartilha inode com nada).

Na cópia: o feed trocado pelo novo (renomeado para `dlss5-feed.addon64`), `renodx-dlss.addon64`
mantido, `nr_spike=0` mantido (a chave mora no `dlss5-feed.cfg`, não no `dlss5forall.ini`).

**Execução:** 90 s, `amd\tests\alvo_animado.exe` 1280x720 a 8 px/quadro, posicionado em X=2200 —
dentro da faixa **física** do monitor secundário (1920..5360). A automação declarou
`SetProcessDpiAwarenessContext(-4)` **antes** de qualquer medida de geometria, pela razão da §7 do
`RELATORIO-MIGRACAO-BIN.md`; o `GetWindowRect` devolveu X 2200..3498, físico. O app foi aberto com
`--outra-instancia --source window --window-hwnd <hwnd>`.

**Os quatro critérios, todos cumpridos:**

| critério | resultado |
|---|---|
| `evaluate > 0 ms` contínuo | 0,75–1,03 ms em toda a corrida; **4.800 quadros entregues**; o único `evaluate 0.00` é o frame 0, antes de a sessão NGX abrir |
| sem crash / `.dmp` | **0 dumps**; `shut down cleanly.` no fim do log; os dois processos vivos aos 90 s |
| `ReShade.log` sem erro novo | 12 mensagens WARN/ERROR distintas contra **13 no baseline** da produção anterior; `Compare-Object` acusou **nenhuma nova** |
| `dlss5forall.log` com present estável | 57,0–60,0 fps de present ao longo de todas as janelas de 5 s, 5.175 quadros apresentados, **0 erros** |

Os três add-ons registraram (`DLSS 5 Bridge`, `DLSS 5 Feed 0.12.0`, `RenoDX DLSS`) e o log confirmou
que é o binário certo: `[amd] variant: DLSS5-Feeder AMD build (FEED_AMD_VARIANT)` e
`config (neural): nr_spike=0 nr_passes=1 … dred=1`, com `DRED on: GPU breadcrumbs and page-fault
capture armed`.

**Duas linhas que parecem falha e não são**, registradas para ninguém reabrir a dúvida:

- `Output: D3D12->D3D11 path failed 0x80070057, trying the other direction` — é a primeira
  tentativa de um par. A linha seguinte é `Output 1276x711 B8G8R8A8_UNORM via D3D11->D3D12
  (d3d12 flags=0x24 UAV)`: a outra direção pegou, e a feature ficou pronta.
- `Depth probe … min 0, max 0` em todas as sondas — é o depth zerado já conhecido, e o próprio log
  explica que é **estrutural** para captura de janela. Não é regressão deste feeder.

Com isso o feeder foi promovido à `bin\` pelo mesmo método (remover, copiar, re-hashear: bateu).
A pasta `bin-teste-release\` foi deixada no lugar como evidência da corrida.

**Um stall de 208 ms** apareceu no frame 3038, com `outside the feed 206,9 ms` — fora do add-on
(jogo, driver ou hooks de terceiros). Um em 4.800 quadros, e não no feed.

## 6. Conferência final da `bin\`

| item pedido | resultado |
|---|---|
| `dxgi.dll` 64 bits | `machine=0x8664` lido do cabeçalho PE — **x64** |
| `ReShade.ini` com `PresetPath=.\FafaDLSS5.ini` | presente, relativo |
| `ReShade.ini` com `EffectSearchPaths` | `.\reshade-shaders\Shaders\**` (e `TextureSearchPaths=.\reshade-shaders\Textures\**`), relativos |
| `dlss5forall.ini` sem chaves de teste | **nenhuma** ocorrência de `sint_`, `DLSS5_`, bancada, debug ou teste |
| `ofertas.ini` com `url_base` | **estava vazio — corrigido, ver abaixo** |
| `Fafá`/`FAFÁ` em `bin\*.ini`, `*.txt`, `*.md` | **0**, em 13 arquivos, varridos em UTF-8 **e** na codificação do sistema |

**O achado: `bin\ofertas.ini` estava com `url_base=` vazio.** A raiz da produção e a fonte
`amd\ofertas.ini` traziam `url_base=https://pcdofafa.com.br/dlss5-app`; a cópia da `bin\` divergia
em **exatamente essa linha e mais nenhuma** (`Compare-Object`: 2 diferenças, que são os dois lados
da mesma chave). Quem lê a chave é o **launcher** (`launcher.cpp`), e sem ela não há checagem de
atualização nenhuma — `main.cpp` não a consulta. Foi alinhada com a fonte pelo mesmo método de
remover-e-copiar, e agora os três arquivos são idênticos (`7F480487…`, 8.813 B).

**Uma observação, sem ação:** `ReShade.ini` tem `IntermediateCachePath` com caminho absoluto desta
máquina. Isso **não vaza para o pacote** — `empacotar.ps1` reescreve a chave para
`.\reshade-cache` e recusa qualquer caminho absoluto em `EffectSearchPaths`/`TextureSearchPaths`/
`PresetPath` (linhas 152-154). Na `bin\` de execução local o ReShade regrava a chave sozinho a cada
arranque, então mexer aqui não faria diferença.

## 7. Segunda leva: os três documentos da release (07:33)

Promovidos depois, numa segunda passada, com a mesma disciplina. Backup dos substituídos em
`D:\backup-docs-antes-release-20260911-0733\`, ao lado do backup da `bin\`.

| destino na produção | antes (SHA-256 / bytes) | depois (SHA-256 / bytes) |
|---|---|---|
| `LEIA-ME.txt` (raiz) ← `amd\portable-launcher\LEIA-ME.txt` | `10914864DCA287E8…3D2C7C0A` / 13.471 | `876A1445C2BEB1A1…E4BE4554` / **17.689** |
| `README.txt` (raiz) ← `amd\portable-launcher\README.txt` | `24CBD6FA29C3C3E7…4BAA2065` / 12.431 | `A19F2D84D2089C15…31479A77` / **16.686** |
| `bin\README.md` ← `amd\README.md` | `02AF143969480005…0CFD20FA` / 25.803 | `F37084EC5CDD9AAD…C1E2BE15` / **30.580** |

**Onde cada um mora, conferido e não presumido:** `LEIA-ME.txt` e `README.txt` ficam **só na raiz** —
uma varredura recursiva da `bin\` inteira não achou cópia de nenhum dos dois, então não havia
segundo lugar a atualizar. O README técnico mora em **`bin\README.md`**, e é o mesmo documento que
`amd\README.md` (cabeçalho idêntico, `# DLSS5 For All` e o mesmo parágrafo de abertura).

**Codificação preservada porque os bytes foram copiados, não reescritos** (`Copy-Item`, sem
`Get-Content`/`Set-Content` no meio). Conferido nos três destinos depois da cópia: os dois `.txt`
continuam UTF-8 **com** BOM (`EF BB BF`), o `.md` continua UTF-8 **sem** BOM (`23 20 44`, que é
`# D`) — exatamente como as respectivas fontes. O hash de cada destino bate com o da sua origem.

**Marca:** `Fafá`/`FAFÁ` = **0** nos três, varridos em UTF-8 e na codificação do sistema, antes
(nas fontes) e depois (nos destinos). As ocorrências de "fafa" que existem são todas legítimas:
`FafaDLSS5.ini` (nome de arquivo), `pcdofafa.com.br` (domínio) e **`PC do Fafa`** sem acento no
`README.md`, que é a forma correta da marca.

**Um processo alheio apareceu no meio:** às 07:34 subiu um `DLSS5ForAll.exe` (PID 31484) de
`bin-teste-residual\` — pasta de teste de outro agente, não a `bin\` de produção. Não foi tocado, e
não interfere: a produção estava fechada quando esta leva começou e nenhum arquivo promovido estava
em uso.

## 8. Terceira leva: a 1.2 final (08:24)

Backup dos seis substituídos em `D:\backup-bin-antes-1.2-0824\`, com a mesma hierarquia da
produção (`bin\` dentro de `bin\`). Os dois PNG da marca são arquivos novos, e `bin\marca\` não
existia — foi criada.

**Os três hashes esperados bateram na origem antes de qualquer cópia.** Os oito destinos foram
re-hasheados depois e todos batem com a sua origem.

| destino | antes (SHA-256 / bytes) | depois (SHA-256 / bytes) |
|---|---|---|
| `bin\DLSS5ForAll.exe` | `60DB213BE057B5E3…D358B139` / 2.917.861 | `852583C5BCEDBE97…B987AEFC` / **2.928.431** |
| `bin\dlss5-bridge.addon64` | `35C39EF540009467…EFF7AE96` / 719.214 | `2D951D739138D5DB…6F195F10` / **1.019.764** |
| `DLSS5ForAll.exe` (launcher, raiz) | `DBA166A0F8F5E6C9…C250A87B` / 2.160.656 | `77C59E43DF8C174D…C3A129B3F` / **2.161.092** |
| `bin\marca\comparar-original.png` | não existia | `BABEA7D010D5AC6E…23080074` / **346.662** |
| `bin\marca\comparar-dlss5.png` | não existia | `00E687A0E10090AB…D4359F31` / **453.614** |
| `LEIA-ME.txt` (raiz) | `876A1445C2BEB1A1…E4BE4554` / 17.689 | `1F59E13C5131EEE5…8E98A43E` / **19.301** |
| `README.txt` (raiz) | `A19F2D84D2089C15…31479A77` / 16.686 | `4B3D6DF0FFFE861D…0225D43D` / **18.347** |
| `bin\README.md` | `F37084EC5CDD9AAD…C1E2BE15` / 30.580 | `A10DD3B243000821…DBE98C22` / **34.467** |

**A bridge cresceu 42%** (719.214 → 1.019.764 B), o maior salto desta release.

**Conferência depois:**

| item | resultado |
|---|---|
| `FileVersion` dos dois exes | **1.2.0.0** em `bin\DLSS5ForAll.exe` e no launcher da raiz; `ProductVersion` 1.2.0.0 e `ProductName` `DLSS5 For All` nos dois |
| `Fafá`/`FAFÁ` nos três textos | **0**, em UTF-8 e na codificação do sistema |
| `Fafá`/`FAFÁ` na `bin\` inteira | **0** em 13 arquivos `.ini`/`.txt`/`.md` |
| ini sem chaves de bancada | **nenhuma** ocorrência de `sint_medir_luz`, `sint_`, bancada ou `DLSS5_*=` |
| `ReShade.ini` | `PresetPath=.\FafaDLSS5.ini` e `EffectSearchPaths=.\reshade-shaders\Shaders\**`, relativos |

**Codificação preservada** de novo por cópia de bytes: os dois `.txt` seguem com BOM UTF-8
(`EF BB BF`) e o `.md` sem BOM (`23 20 44`), iguais às fontes. Nenhuma instância da produção estava
rodando durante a troca.

## 9. O módulo AMD (`bin-amd\`) tinha ficado para trás

`bin-amd\DLSS5ForAll.exe` estava em **1.1.0.0** e o bridge de lá era ainda mais velho — o da `bin\`
**de antes da migração desta manhã**. Backup dos dois em `D:\backup-bin-antes-1.2-0824\bin-amd\`.

**O bridge de lá é o binário comum, não uma variante AMD** — a dúvida foi resolvida por duas provas
independentes, não por suposição:

1. **Byte a byte:** `bin-amd\dlss5-bridge.addon64` era `A4FD4B3610376BC8…5312CD04` / 453.569 B,
   **idêntico** ao `dlss5-bridge.addon64` guardado em `D:\backup-bin-antes-migracao-20260911\` —
   mesmo SHA-256, mesmo tamanho, e o mesmo MD5 `5B9E09FBCA0F…` que a §4 do
   `RELATORIO-MIGRACAO-BIN.md` registrou como o bridge antigo da `bin\`.
2. **No script:** `montar-portable.ps1:130` copia `DLSS5ForAll.exe`, `dlss5-bridge.addon64` e
   `dxgi.dll` da **mesma origem** que abastece a `bin\`. O que é próprio do módulo AMD é só o
   **Feeder** (`4b`, `feeder-amd\dlss5-feed-amd.addon64`, portador FSR), e o cabeçalho do script
   diz isso em letras: *"o mesmo exe + ReShade + Bridge, o Feeder na variante AMD"*.

Os dois foram atualizados; o `dlss5-feed.addon64` da variante AMD **não foi tocado**.

| arquivo em `bin-amd\` | antes (SHA-256 / bytes) | depois (SHA-256 / bytes) |
|---|---|---|
| `DLSS5ForAll.exe` | `45252F50317E4C64…C211F6FAC` / 2.452.553 (**1.1.0.0**) | `852583C5BCEDBE97…B987AEFC` / **2.928.431** (**1.2.0.0**) |
| `dlss5-bridge.addon64` | `A4FD4B3610376BC8…5312CD04` / 453.569 | `2D951D739138D5DB…6F195F10` / **1.019.764** |

`FileVersion` conferido depois da cópia: **1.2.0.0**, `ProductName` `DLSS5 For All`. Hash de destino
igual ao de origem nos dois. App fechado durante a troca.

## 10. Build final da 1.2 (13:25) — o mesmo par nas duas pastas

Recompilação da 1.2 (capivara nos menus). Os dois hashes esperados bateram na origem antes de
qualquer cópia; os quatro destinos foram re-hasheados depois e todos batem. Backup em
`D:\backup-bin-antes-1.2-1325\`, com `bin\` e `bin-amd\` separados.

| arquivo | antes (SHA-256 / bytes) | depois (SHA-256 / bytes) |
|---|---|---|
| `DLSS5ForAll.exe` (em `bin\` **e** `bin-amd\`) | `852583C5BCEDBE97…B987AEFC` / 2.928.431 | `C13671EE11316907…9C1C8A34` / **2.934.875** |
| `dlss5-bridge.addon64` (em `bin\` **e** `bin-amd\`) | `2D951D739138D5DB…6F195F10` / 1.019.764 | `7B79BDF627FF2C3C…2C83C5B5` / **1.019.764** |

**A bridge tem exatamente o mesmo tamanho de antes e hash diferente** — 1.019.764 B nas duas
versões. É recompilação do mesmo código com conteúdo alterado, e o tamanho igual seria armadilha
para quem conferisse só por bytes: aqui o hash é o que separa uma da outra.

As duas pastas continuam recebendo o par comum, como manda `montar-portable.ps1:130` (§9).
`FileVersion` conferido nos **três** executáveis da produção depois da troca — `bin\`, `bin-amd\` e
o launcher da raiz — todos **1.2.0.0** com `ProductName` `DLSS5 For All`.

Nenhuma instância da produção estava rodando. O único `DLSS5ForAll.exe` vivo era de
`bin-teste-residual\`, pasta de teste de outro agente, e não foi tocado.

## 11. O módulo AMD promovido inteiro (14:24): `bin-amd-1.2-proposta\` → `bin-amd\`

Backup completo da `bin-amd\` anterior em `D:\backup-bin-amd-antes-1.2-1424\` — **49 arquivos**,
25,07 MB, contagem conferida contra a origem. A `bin-amd\` fechou com **51 arquivos e 27,8 MB**,
e o inventário bate com a proposta: **zero diferenças**.

**Nada precisou ser movido para o backup por sobra.** A comparação de inventário feita *antes* de
tocar em qualquer arquivo mostrou **0 arquivos só na produção** — tudo o que havia lá existe na
proposta. A proposta acrescentou 2 arquivos e não removeu nenhum.

| o que aconteceu | quantos |
|---|---|
| trocados (hash diferente; removidos antes de copiar) | **9** |
| novos (`dlssnr_on_amd.ini`, `reshade-shaders\Shaders\DLSS5_HUD.fx`) | **2** |
| já idênticos, não tocados | **40** |

Os 9 trocados: `dlss5-feed.addon64`, `dlss5-feed.cfg`, `dlssnr_on_amd_setup.exe`, `FafaDLSS5.ini`,
`LEIA-ME-AMD.txt`, `README-AMD.txt`, `preparar-mod-amd.ps1`, `ReShade.ini` e
`reshade-shaders\Shaders\DLSS5_Feed.fx`. Cada destino foi re-hasheado logo após a cópia e comparado
com a origem — os onze bateram, e o laço aborta na primeira divergência.

**`DLSS5ForAll.exe` e `dlss5-bridge.addon64` não aparecem entre os trocados porque já estavam
certos** — a §10 os promoveu às duas pastas às 13:25, e o hash confirma que a proposta traz
exatamente os mesmos binários.

**Conferência final, item a item do que foi pedido:**

| item | resultado |
|---|---|
| `DLSS5ForAll.exe` | `C13671EE11316907…9C1C8A34`, 2.934.875 B, **FileVersion 1.2.0.0** |
| `dlss5-bridge.addon64` | `7B79BDF627FF2C3C…2C83C5B5`, 1.019.764 B |
| feeder AMD igual ao da proposta | **idêntico** (`207E173AA0EB9E8F…C918B0F2`) |
| `dlss5-feed.cfg` | `amd_decoy_swapchain=1` e `nr_spike=0` — e também `amd_carrier=1`, `warmup_rebuild=180`, `create_delay=60`, `work_sharpness=0.30`, `dred=1`, `nr_passes=1`, como a §11.1 do relatório de otimização decidiu |
| `dlssnr_on_amd.ini` | presente, com **`InlineWaitMs=25`** |
| instalador do mod | `DAD67CC649AD91BA…6E2315C3`, 7.570.162 B — **bate com o hash oficial da v0.2.18** registrado na §473 daquele relatório |
| `Fafá`/`FAFÁ` | **0** em 32 arquivos de texto, varridos em UTF-8 e na codificação do sistema |
| log, `.bak`, dump, `ReShade2.ini`, handoff | **nenhum** |
| `version.dll`, pesos, `nvngx_dlssnr.dll` | **nenhum** — nada de não redistribuível entrou |

**Uma pasta vazia foi removida:** `bin-amd\reshade-cache\` sobrou sem nenhum arquivo dentro e a
proposta não a tem — a §13 do relatório de otimização a removeu de propósito, por ser específica do
par vendor/device. Está no backup, e o ReShade a recria sozinho no primeiro arranque.

Nenhuma instância da produção estava rodando. Os dois `DLSS5ForAll.exe` vivos eram de uma pasta de
bancada no scratchpad, de outro agente, e não foram tocados.

## 12. A promoção final da 1.2 (17:26)

Backup em `D:\backup-bin-antes-1.2-final-1726\`, com `bin\` e `bin-amd\` separados (5 arquivos).
Os três prefixos de hash e os dois tamanhos declarados foram conferidos na origem **antes** de
qualquer cópia; os cinco destinos foram re-hasheados depois e todos batem com a origem.

| destino | antes (SHA-256 / bytes) | depois (SHA-256 / bytes) |
|---|---|---|
| `bin\DLSS5ForAll.exe` | `C13671EE11316907…9C1C8A34` / 2.934.875 | `7FDE5BA72E2B501EDC61041A41840017C5180EEA7B35FC2C5278123769A51A0C` / **2.971.118** |
| `bin-amd\DLSS5ForAll.exe` | `C13671EE11316907…9C1C8A34` / 2.934.875 | o mesmo `7FDE5BA7…A51A0C` / **2.971.118** |
| `bin\dlss5-bridge.addon64` | `7B79BDF627FF2C3C…2C83C5B5` / 1.019.764 | `C40DD21E490087FF540D3D04DF048F36E44E0B4674FF8FBDA0C493F8917027C3` / **1.019.764** |
| `bin-amd\dlss5-bridge.addon64` | `7B79BDF627FF2C3C…2C83C5B5` / 1.019.764 | o mesmo `C40DD21E…7027C3` / **1.019.764** |
| `bin\dlss5-feed.addon64` | `207E173AA0EB9E8F…C918B0F2` / 994.756 | `3D6C19645886BF4E4AA412B3451FEB1BCD48F101282D46238359624B913AAC62` / **1.001.841** |

**A bridge repetiu o tamanho pela terceira vez seguida** — 1.019.764 B em `2D951D73…`,
`7B79BDF6…` e agora `C40DD21E…`. Três binários diferentes, um só tamanho: conferir por bytes aqui
não separa nada, e só o hash resolve.

**O feeder da `bin-amd\` não foi tocado, e isso foi verificado nos dois sentidos:** antes da
promoção ele era `207E173AA0EB9E8F…C918B0F2` / 994.756 B, e depois continua exatamente o mesmo.
A variante AMD permanece com o feeder dela; o `3D6C1964…` entrou **só** na `bin\`.

### Conferência da `bin\`

| item | resultado |
|---|---|
| `ReShade.ini` sem BOM | bytes iniciais `5B 41 44` (`[AD`) — **sem BOM** |
| `ReShade.ini` com seções únicas | 10 seções, 10 distintas — **nenhuma duplicada** |
| `Fafá`/`FAFÁ` | **0** em `bin\` e **0** em `bin-amd\`, 32 arquivos de texto cada, em UTF-8 e na codificação do sistema |
| `FileVersion` | **1.2.0.0** nos três executáveis: `bin\`, `bin-amd\` e o launcher da raiz |

**`botao_flutuante=0` saiu**, como autorizado — o botão não existe mais. A chave foi apagada
por substituição direta no texto, gravando sem BOM: o arquivo passou de 800 para 781 bytes,
exatamente os 19 da linha, os outros 50 CRLF continuam CRLF e não há um só byte acima de 127.

**O que eu NÃO mexi, e é preciso dizer:** o `[ultima]` do `bin\dlss5forall.ini` continua sendo o
**estado real do usuário** — `janela=RESIDENT EVIL requiem`, o monitor dele, `painel_x/y`,
`placa=NVIDIA GeForce RTX 4080`, `dispositivo=Animaze Virtual Camera` — e junto com ele seguem
`alterna_quadros=2` e `alterna_modo=sintetizar`. Não são chaves de bancada esquecidas: são a
escolha dele nessa instalação local. **E nada disso viaja no pacote:** `empacotar.ps1` (item 3)
não copia este arquivo, ele **escreve um `dlss5forall.ini` de fábrica do zero**, com `[ultima]`
mínimo e sem nenhuma dessas chaves. Mexer aqui mudaria a máquina dele sem mudar o zip.

### 12.1 Complemento: os três documentos, de novo (17:27)

Os documentos mudaram outra vez depois da promoção dos binários. Backup dos anteriores dentro do
mesmo `D:\backup-bin-antes-1.2-final-1726\`, junto com o resto da rodada.

| destino | antes (SHA-256 / bytes) | depois (SHA-256 / bytes) |
|---|---|---|
| `LEIA-ME.txt` (raiz) | `1F59E13C5131EEE54E0B…` / 19.301 | `55965B7CFAD366D41C43…` / **22.555** |
| `README.txt` (raiz) | `4B3D6DF0FFFE861D81F2…` / 18.347 | `6459CAAF50965E1710BE…` / **21.643** |
| `bin\README.md` | `A10DD3B2430008217D23…` / 34.467 | `BCE82D19D9B09CF354E0…` / **42.693** |

Cópia por bytes, hash de destino igual ao de origem nos três. **BOM e quebras de linha preservados
e conferidos depois:** os dois `.txt` seguem com BOM UTF-8 (`EF BB BF`), o `.md` sem BOM
(`23 20 44`), e os três são LF puro — 445, 429 e 559 quebras, **zero CRLF**. `Fafá`/`FAFÁ` deu
**0** nas fontes antes de copiar e nos destinos depois.

### 12.2 O feeder já estava certo, e as duas origens são o mesmo arquivo

Conferido em vez de recopiado: `amd\feeder-amd\build\dlss5-feed-amd.addon64` e
`bin-teste-requiem\dlss5-feed.addon64` têm **o mesmo SHA-256**
(`3D6C19645886BF4E…B913AAC62`), o mesmo tamanho (1.001.841 B) e o mesmo mtime (13:24) — são o
mesmo binário em dois lugares, então a escolha da origem era indiferente. A `bin\` já o recebeu na
§12 e está com exatamente esse hash. Nada a refazer.

`bin\renodx-dlss.addon64` conferido e inalterado: `85EAE478F1E733E8…19201CEE`, 2.499.584 B.

### 12.3 Última cópia dos documentos: a marca voltou (17:30)

Backup dos anteriores em `D:\backup-docs-antes-marca-1730\`. Cópia por bytes, hash de destino
igual ao de origem nos três.

| destino | antes (SHA-256 / bytes) | depois (SHA-256 / bytes) |
|---|---|---|
| `LEIA-ME.txt` (raiz) | `55965B7CFAD366D41C43…` / 22.555 | `1BBBF025EF7BE7054C4E37173F4E67C9CAFB007F8436BD7B4E2F840959B2B0AD` / **23.030** |
| `README.txt` (raiz) | `6459CAAF50965E1710BE…` / 21.643 | `37A9D9FA4AE4AE9F589FE9C16A9AED711BE1A51C329949DE700471EF86E4F08F` / **22.103** |
| `bin\README.md` | `BCE82D19D9B09CF354E0…` / 42.693 | `556C7559AF1555B1E21A75E301B26B007BE971F1AAE948CF9FBF58F3F8526E41` / **42.934** |

BOM e quebras conferidos depois: `.txt` com `EF BB BF`, `.md` sem BOM, os três LF puro (457, 441 e
565 quebras, zero CRLF). **`PC do Fafa` presente nos três** — `LEIA-ME.txt` nas linhas 3 e 451,
`README.txt` nas 3 e 435, `README.md` nas 3, 160 e 565 (cabeçalho, corpo e rodapé).
`Fafá`/`FAFÁ` = **0** nos três, e **0** também numa revarredura da `bin\` (14 arquivos) e da
`bin-amd\` (18 arquivos).

> **ARMADILHA, e eu caí nela: `Select-String` com uma lista de CAMINHOS pelo pipeline não lê
> arquivo nenhum.** `@("a.txt","b.txt") | Select-String -Pattern X` trata cada string como o
> **texto a examinar** e procura o padrão dentro do próprio caminho — sempre 0, sem erro e sem
> aviso. Foi assim que eu relatei, uma rodada antes, que "a marca `PC do Fafa` não aparece nos
> documentos": ela aparecia, e o comando é que nunca abriu os arquivos. Medido lado a lado nos
> mesmos três arquivos: pipeline de string **0**, `-Path` **7**.
>
> O que salva: passar os caminhos em **`-Path`** (`Select-String -Path $lista -Pattern X`), ou
> mandar pelo pipeline **objetos de arquivo** (`Get-ChildItem ... | Select-String`), que o
> `Select-String` reconhece e abre. Os greps desta promoção feitos com `Get-ChildItem` sempre
> foram válidos; os que montaram um array de strings deram falso negativo. Aqui o veredito não
> mudou — a revarredura com `-Path` confirmou `Fafá` = 0 em tudo — mas o método que produziu o
> número estava quebrado, e um dia ele esconderia uma ocorrência de verdade.

## 12.4 Oitava rodada (17:44): o exe de novo, e os documentos com o nome certo do add-on

Backup em `D:\backup-bin-antes-1.2-final-1744-b\`. Os cinco destinos foram re-hasheados depois da
cópia e batem com a origem.

| destino | antes (SHA-256 / bytes) | depois (SHA-256 / bytes) |
|---|---|---|
| `bin\DLSS5ForAll.exe` | `7FDE5BA72E2B501E…69A51A0C` / 2.971.118 | `15A1E4C8A1F2AF3A51EA55D2EA9CD650CCB14409572FC94E53133B5F182F6ED1` / 2.971.118 |
| `bin-amd\DLSS5ForAll.exe` | `7FDE5BA72E2B501E…69A51A0C` / 2.971.118 | o mesmo `15A1E4C8…2F6ED1` / 2.971.118 |
| `LEIA-ME.txt` (raiz) | `1BBBF025EF7BE705…59B2B0AD` / 23.030 | `84B01DACF5A204FF5C015D1A2399997A811726B035C133B94A698D6A56C91DEF` / 23.028 |
| `README.txt` (raiz) | `37A9D9FA4AE4AE9F…86E4F08F` / 22.103 | `F358C65C3993F5FB36938294A27A650875682AF8643EDF1DE4AB4CD619B8A456` / 22.101 |
| `bin\README.md` | `556C7559AF1555B1…F8526E41` / 42.934 | `29E14C50E5B184D014AF9C02298E0928EBC2BE97A2D199A4C9D19F9A75062BE6` / 42.931 |

**O executável repetiu o tamanho exato de antes, 2.971.118 B, com hash diferente** — é a terceira
vez nesta release que um binário troca sem mudar de tamanho (a bridge fez o mesmo três vezes
seguidas). Conferir por bytes aqui não acusaria nada.

**O que não podia mudar, e não mudou** — conferido depois: bridge `C40DD21E…` nas duas pastas,
`bin\dlss5-feed.addon64` `3D6C1964…`, `bin-amd\dlss5-feed.addon64` `207E173A…` (a variante AMD,
intacta), `renodx-dlss.addon64` `85EAE478…`, e o launcher da raiz `77C59E43…`, que vai em outra
rodada. `FileVersion` **1.2.0.0** nos três executáveis.

Documentos copiados por bytes: BOM UTF-8 nos dois `.txt`, sem BOM no `.md`, os três LF puro (457,
441 e 565 quebras, zero CRLF). `PC do Fafa` presente **7 vezes** nos três; `Fafá`/`FAFÁ` = **0**.

### `renodx-dlss5`: 8 → 1, e a que sobrou

O critério pedido era **0**, e o resultado é **1**. Não é regressão: **a produção tinha 8
ocorrências antes desta rodada** e os documentos novos corrigiram 7. Sobrou uma, no parágrafo de
abertura do `bin\README.md`:

```
L7: previsível possível pra cadeia **ReShade → DLSS5-Feeder → LumeniteFX → renodx-dlss5**.
```

As 7 corrigidas eram as do corpo dos três arquivos (`renodx-dlss5.addon64` → `renodx-dlss.addon64`,
nas listas de "falta um destes três em bin\", na nota de build vazado e na tabela de solução de
problemas). A linha 7 é a única que escapou, e escapou porque ali o nome aparece **solto**, sem o
sufixo `.addon64` — uma substituição feita sobre `renodx-dlss5.addon64` não a alcança.

**Não editei a fonte:** `amd\README.md` está sendo escrito por outro agente nesta mesma hora
(mtime 17:41), e uma edição minha por baixo entraria em conflito com a próxima gravação dele. A
correção pertence a quem escreve o documento.

## 12.5 Nona rodada (17:49): o launcher e a linha 7 do README

Backup em `D:\backup-bin-antes-1.2-final-1749-c\`. Dois arquivos trocados, hash de destino igual ao
de origem nos dois.

| destino | antes (SHA-256 / bytes) | depois (SHA-256 / bytes) |
|---|---|---|
| `DLSS5ForAll.exe` (launcher, raiz) | `77C59E43DF8C174D…C3A129B3F` / 2.161.092 | `7FD14C6E06B7E0871E4552AA3F259044963590798997398809A351DDA8530B93` / 2.161.092 |
| `bin\README.md` | `29E14C50E5B184D0…A75062BE6` / 42.931 | `BCB6F1EACF03B5764C597941BF4768A406F27DD96F203EE92106992B1517B0EE` / 42.930 |

**O launcher também repetiu o tamanho exato**, 2.161.092 B, com hash diferente — o quarto binário
desta release a fazer isso.

**`LEIA-ME.txt` e `README.txt` não foram copiados**, como instruído: o hash da fonte é idêntico ao
do destino (`84B01DAC…` e `F358C65C…`), então não havia o que promover. Copiar teria trocado o
mtime sem trocar um byte.

### A pendência da rodada anterior fechou

A linha 7 do `README.md` agora lê `… LumeniteFX → renodx-dlss`, e o critério que faltava foi
atingido:

| critério | antes da 8ª rodada | depois da 8ª | **agora** |
|---|---|---|---|
| `renodx-dlss5` nos três documentos | 8 | 1 | **0** |
| `PC do Fafa` | — | 7 | **7** |
| `Fafá`/`FAFÁ` | — | 0 | **0** |

`bin\README.md` sem BOM (`23 20 44`) e LF puro (565 quebras, zero CRLF), como antes.
`FileVersion` **1.2.0.0** nos três executáveis, agora com o launcher em `7FD14C6E…`.

## 12.6 A pasta limpa em Documentos (17:55), e um arquivo que não entrou no zip

`C:\Users\PCDOFAFA\Documents\DLSS5 For All\` criada por extração do
`publicar\privado\DLSS5ForAll-1.2-completo-PRIVADO.zip` (166,9 MB), com o SHA-256 conferido antes
de extrair: `31C6CCF4C624B64F…52132F9A`, confere. O destino não existia — o `Documents\dlss5-for-all`
que já havia ali é outra coisa (o repositório do site) e não foi tocado. **Nada foi executado.**

**111 arquivos, 262 MB, 13 pastas** — a contagem esperada.

```
DLSS5 For All\
├─ DLSS5ForAll.exe (launcher) · LEIA-ME.txt · README.txt · ofertas.ini · dlss5forall.ini · versao.txt · WebView2Loader.dll
├─ bin\      54 arq. — app, dxgi, 3 add-ons, nvngx_dlss + nvngx_dlssnr, presets, setup, README.md
│   ├─ marca\            comparar-original.png · comparar-dlss5.png
│   └─ reshade-shaders\  (+ reshade-cache\ vazia)
└─ bin-amd\  50 arq. — app, bridge, feeder AMD, FidelityFX, instalador do mod, textos AMD
```

| conferência | resultado |
|---|---|
| `bin\renodx-dlss.addon64`, `nvngx_dlss.dll`, `nvngx_dlssnr.dll` | os três presentes (2.499.584 / 58.956.400 / 165.840.496 B) |
| `bin-amd\dlssnr_on_amd_setup.exe` | presente, 7.570.162 B (v0.2.18) |
| log, `.bak`, `.dmp`, `.tmp`, pasta de teste | **nenhum** |
| executáveis | **1.2.0.0** nos três — launcher `7FD14C6E…`, app `15A1E4C8…` em `bin\` e `bin-amd\` |
| `dlss5forall.ini` de fábrica | **123 B** nos três lugares, só o cabeçalho de comentário |
| `ReShade.ini` sem BOM | `5B 41 44` (`[AD`) em `bin\` e em `bin-amd\` |

### O achado: falta `bin-amd\dlssnr_on_amd.ini`

Comparando arquivo a arquivo a `bin-amd\` da produção com a do zip: **51 contra 50**, e a diferença
é **um único arquivo**, `dlssnr_on_amd.ini` (225 B). Nada mais falta, e nada sobra — nem na
`bin-amd\`, nem na `bin\`, cujos 54 arquivos vieram todos da produção.

Não é detalhe: é o arquivo que a §10 do `RELATORIO-AMD-OTIMIZACAO.md` mandou **incorporar byte a
byte** por ser "a configuração que rodou", e é onde mora `InlineWaitMs=25` — a alavanca de latência
que só existe a partir da v0.2.18 do mod. Sem ele, quem instalar o módulo AMD cai nos padrões do
mod e perde justamente o ajuste que foi medido.

A pasta extraída e o zip foram deixados **como estão**: o conserto é no script que monta o pacote,
não na cópia já publicada, e essa decisão é de quem cuida do empacotamento.

### 12.6.1 Refeita com o zip corrigido (17:58) — a falta fechou

O zip foi regerado e a pasta refeita do zero. Antes de apagar, conferi que a pasta ainda era a que
eu havia criado e que **ninguém a tinha alterado**: 111 arquivos, 262 MB, mtime mais recente
17:51:50, nenhum arquivo modificado depois da extração. Zip novo conferido antes de extrair:
`F6CD8B4CB81BF4D54AC349428A1DB19BA34FA07E900D4B80B5F9AEF5737B6DFC`, confere. Nada foi executado.

**112 arquivos** agora, 262 MB, 13 pastas.

| conferência | resultado |
|---|---|
| `bin-amd\dlssnr_on_amd.ini` | **presente**, 225 B, com `InlineWaitMs=25`, e **idêntico ao da produção** |
| `preparar-mod-amd.ps1` | o novo, 22.157 B (era 20.539), idêntico ao da produção |
| `bin-amd\` zip × produção | **51 × 51, zero diferenças** — o inventário fechou |
| obrigatórios da `bin\` | `renodx-dlss.addon64`, `nvngx_dlss.dll`, `nvngx_dlssnr.dll` presentes |
| instalador do mod | 7.570.162 B (v0.2.18) |
| log, `.bak`, `.dmp`, `.tmp`, pasta de teste | **nenhum** |
| executáveis | **1.2.0.0** nos três — launcher `7FD14C6E…`, app `15A1E4C8…` nas duas pastas |
| `dlss5forall.ini` de fábrica | **123 B** nos três lugares |
| `ReShade.ini` sem BOM | `5B 41 44` em `bin\` e em `bin-amd\` |

## 13. Estado final

Promoção completa. Seis arquivos trocados, todos com hash conferido nos dois lados; o feeder entrou
só depois de passar nos quatro critérios; a `bin\` fechou a conferência com um único desvio, que foi
corrigido. Backup íntegro em `D:\backup-bin-antes-release-20260911-0634\`. Os únicos processos
encerrados foram os dois PIDs criados por esta sessão para o teste.
