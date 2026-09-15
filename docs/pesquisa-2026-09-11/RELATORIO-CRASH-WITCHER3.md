# O crash do Witcher 3 e a instalação por injeção direta

**Data:** 2026-09-11
**Pergunta:** o crash de `witcher3.exe` em 08/09/2026 17:16:15 (`0xc0000005`, offset `0x1f1f4ea`) foi
causado pela cadeia DLSS 5 instalada dentro da pasta do jogo?

**Resposta: não. No momento do crash, nada da nossa cadeia estava carregado no processo.**

---

## 1. A prova

`C:\ProgramData\Microsoft\Windows\WER\ReportArchive\AppCrash_witcher3.exe_8a89a8...\Report.wer`
(08/09/2026 17:16:20). O WER registra a lista completa dos módulos que estavam em memória:

```
Sig[0].Value = witcher3.exe
Sig[1].Value = 4.0.1.37654
Sig[3].Value = witcher3.exe        <- módulo de falha: o próprio jogo
Sig[6].Value = c0000005
Sig[7].Value = 0000000001f1f4ea
UI[2]        = D:\SteamLibrary\...\bin\x64_dx12\witcher3.exe

LoadedModule[55] = C:\Windows\SYSTEM32\dxgi.dll     <- o dxgi do SISTEMA
LoadedModule[88] = ...\bin\x64_dx12\sl.dlss.dll
LoadedModule[92] = ...\bin\x64_dx12\nvngx_dlss.dll
LoadedModule[93] = ...\bin\x64_dx12\nvngx_dlssg.dll
LoadedModule[96] = ...\bin\x64_dx12\sl.dlss_g.dll
```

Não há **nenhum** `dxgi.dll` da pasta do jogo, nenhum `.addon64`, nenhum RenoDX, nenhum ReShade.
Os módulos DLSS presentes (`sl.*`, `nvngx_dlss*`) são o **DLSS nativo do Witcher 3 4.0 via
Streamline**, que o jogo traz de fábrica.

Confirmação independente: o cache de shaders do ReShade
(`%LOCALAPPDATA%\Temp\ReShade\`) tem arquivos em 01, 04, 05, 06, 09 e 11/09 — e **nada em 07/09
nem em 08/09**. Nenhuma instância de ReShade compilou um shader no dia do crash.

## 2. Por que a cadeia não estava carregada

O `dxgi.dll` que está em `...\The Witcher 3\bin\x64_dx12\` é **de 32 bits**. Verificado no
cabeçalho PE:

| Arquivo | Machine | Versão | Identidade |
|---|---|---|---|
| `bin\x64_dx12\dxgi.dll` | **0x014C (i386)** | 6.8.0.2156 | `OriginalFilename = ReShade32.dll` |
| `bin\x64_dx12\witcher3.exe` | **0x8664 (x64)** | 4.0.1.37654 | o jogo |

É byte a byte idêntico ao `dxgi.dll` da **raiz** do jogo (onde mora o launcher de 32 bits): foi
copiado de lá para dentro de `x64_dx12` à mão. Um ReShade de 32 bits não carrega num processo de
64 bits, e o `Report.wer` mostra o jogo tendo subido com o `dxgi.dll` do System32.

## 3. Linha do tempo

Reconstruída pelo `CreationTime`, porque as cópias preservaram o `LastWriteTime` de origem (todos
os arquivos da pasta têm mtime de 30/08, o que engana):

| Quando | O quê |
|---|---|
| 30/08 20:08:46 | ReShade **6.8.0.2155 de 64 bits** inicializa de `bin\x64_dx12\dxgi.dll` (`ReShade.log1`) |
| 30/08 20:20:39 | **crash** de `bin\x64\witcher3.exe` — `c0000409` (fail fast) em `0xf5d8f9` |
| 30/08 20:21:28 | **crash** de `bin\x64\witcher3.exe` — `c0000005` em `0x4ca8a4` |
| 30/08 20:21:48 | o jogo é reaberto, agora pelo **DX12**; roda e **sai limpo** às 20:22:07 |
| 02/09 20:12 | entram `zzz_display_commander.addon32/64` |
| 03/09 23:51:08 | entram as **duplicatas** `DLSS5 Feeder.addon64` e `dlss5-bridge.addon64`; a pasta `bin\x64` é esvaziada |
| 04/09 04:35:49 | entram `renodx-thewitcher3.addon64` e o **`dxgi.dll` de 32 bits** — a partir daqui o ReShade morre |
| 07/09 e 08/09 | nenhum ReShade rodou (cache de shaders vazio nesses dias) |
| 08/09 17:16:15 | **o crash**, sem nada nosso em memória |

## 4. Os dois crashes de 30/08 — os únicos com a cadeia carregada

E eles são de **outro executável**: `bin\x64\witcher3.exe`, o binário **DX11**. Havia uma segunda
instalação ali naquele dia (sobraram um `dlss5-feed.log` e um `dlss5-dx11-bridge.log` de 30/08
20:21:13, sem dxgi nem add-ons: foi esvaziada depois).

**Crash de 20:20:39** (`c0000409`, fail fast): 75 módulos, só o `dxgi.dll` local carregado,
**nenhum add-on ainda**. Morreu antes de a cadeia se montar.

**Crash de 20:21:28** (`c0000005` em `0x4ca8a4`): 131 módulos, com tudo em memória:

```
bin\x64\dxgi.dll                    (ReShade)
bin\x64\dlss5-dx11-bridge.addon64   <--+
bin\x64\dlss5-feed.addon64          <--+ os dois JUNTOS
bin\x64\renodx-dlss5.addon64
bin\x64\renodx-thewitcher3.addon64
bin\x64\renodx-upgrade.addon64
bin\x64\nvngx_dlssnr.dll
```

É exatamente a combinação que o instalador upstream desabilita, com o motivo escrito no código
(`amd\feeder-amd\src-feeder\tools\Install-DLSS5Feeder.ps1:1830`):

```powershell
Disable-Conflict -Path (Find-FileIn $consumerDir 'dlss5-dx11-bridge.addon64') `
                 -Why 'the DX11 bridge must never be combined with DLSS5-Feeder'
```

Vinte segundos depois, o mesmo jogo aberto pelo **DX12** rodou e saiu limpo. Ou seja: naquele dia,
DX11 com a combinação proibida crashou duas vezes; DX12 não crashou.

## 5. O que ainda está errado naquela pasta

1. **`dxgi.dll` de 32 bits** ao lado de um exe de 64 bits — o ReShade não carrega.
2. **`dlss5-feed.addon64` + `dlss5-dx11-bridge.addon64` ativos juntos** — a combinação proibida,
   a mesma do crash de 30/08.
3. **Add-ons duplicados sob dois nomes.** O ReShade carrega TODO `*.addon64` da pasta; lá existem
   `dlss5-feed.addon64` + `DLSS5 Feeder.addon64` (140.800 bytes, v0.6.0.0) e
   `dlss5-dx11-bridge.addon64` + `dlss5-bridge.addon64` (287.744, v1.0.27) — dois feeders e dois
   bridges ganchando as mesmas vtables NGX. O log de 30/08 já mostrava
   `EvaluateFeature prologue: E9 ... <== already hooked by something else`.
4. **Não existe `reshade-shaders\` em lugar nenhum da árvore do jogo** — nenhum `.fx`. É a
   explicação literal do `DLSS5_Feed.fx is not loaded (technique/textures missing)`.
5. **Não existe `ReShade.ini` em `x64_dx12`** (só um de 260 bytes na raiz do jogo, sem `PresetPath`
   e sem `PreprocessorDefinitions`).
6. O feeder de lá é **v0.6.0.0-beta** (140.800 bytes) contra **v0.12.0.1** (979.379) aqui.

## 6. Armadilha de nome, para registro

`dlss5-bridge.addon64` significa **duas coisas diferentes**:

| Onde | Tamanho | O que é |
|---|---|---|
| pasta do jogo | 287.744 | cópia do `dlss5-dx11-bridge.addon64` v1.0.27 (upstream) |
| `bin-teste-passes` | 718.457 | a ponte de controle do nosso app |

Basta copiar um por cima do outro para reintroduzir a combinação proibida sem perceber. É
provavelmente assim que a duplicata na pasta do jogo apareceu.

## 7. Não existe mecanismo de deploy nosso

Varredura do projeto inteiro: `main.cpp` (o único "instalar na pasta do jogo" é a peça do
limitador de fps), `setup-dlss5.ps1`/`.auto.ps1` (varrem `steamapps\common` só para **ler** um
`nvngx_dlss.dll` e trazê-lo PARA a pasta do app), `montar-portable.ps1`, `empacotar.ps1`,
`publicar-versao.ps1`, `perfil-amd\preparar-mod-amd.ps1` — nenhum escreve dentro de um jogo.

O único que faz isso é o upstream `amd\feeder-amd\src-feeder\tools\Install-DLSS5Feeder.ps1`. E
**não foi ele que montou aquela pasta**: ele escolhe o ReShade por `-Bits $gameBits` (acertaria os
64 bits), renomeia o `dlss5-dx11-bridge.addon64` para `.disabled-by-installer`, e instalaria
`reshade-shaders\Shaders` e um `ReShade.ini` — nada disso está lá. A montagem foi manual.

Portanto a v0.6.0-beta não veio de um deploy nosso desatualizado: veio de um pacote antigo
baixado à mão. Não há mecanismo nosso para atualizar.

## 8. Teste controlado noutro jogo: Hollow Knight

Para saber se a cadeia por injeção direta é estável com os componentes **atuais**, sem depender da
pasta bagunçada do Witcher 3. Jogo escolhido: **Hollow Knight** — 64 bits (`Machine=0x8664`
conferido), Unity/D3D11, 4,9 GB, abre em segundos, sem anticheat, e a pasta estava limpa (nenhum
`dxgi.dll`/`d3d11.dll`/`opengl32.dll` nela).

Instalado (caminho, tamanho e MD5 registrados antes em `hk-instalar.csv`), **sem o dx11-bridge e
sem duplicatas de nome**:

```
dxgi.dll              5.592.064  B1DE41B1...  ReShade 6.8.0.2155 64-bit
dlss5-feed.addon64      979.379  0B043E97...  v0.12.0.1 (build novo, com as sondas)
dlss5-bridge.addon64    718.457  3A371F87...  a ponte do app
renodx-dlss.addon64   2.499.584  17474301...  ShortFuse
nvngx_dlss.dll       58.956.400  F9ADD387...
nvngx_dlssnr.dll    165.840.496  900D8C31...
FafaDLSS5.ini, dlss5-feed.cfg, ReShade.ini, reshade-shaders\ (27 arquivos)
steam_appid.txt  (367520 — o jogo pedia relançamento pelo Steam)
```

**Resultado: a cadeia inteira funciona e é estável.**

```
01:09:11.432  [feed] effects: DLSS5_Feed.fx technique found, ... DLSS5_MV_PROVIDER=3
              (LumeniteFX Kernel) -> Lumenite_Kernel (enabled)
01:09:12.216  [feed] NVSDK_NGX_D3D12_Init -> 0x00000001 (Success)
01:09:12.935  [feed] feature ready: 1280x720 DLAA, flags=74, color R8G8B8A8_UNORM
01:12:19.261  [feed] 600 frames: feed CPU 1.05 ms/frame | frame interval 10.15 ms (98.5 fps)
              | feed is 10% of the frame | worst frame 20.5 ms | stalls 0
```

Mais de **18.000 quadros** (30 janelas de 600), **98,2 fps constantes**, feed custando ~1 ms
(9-10% do quadro), **zero stalls** nas últimas 25 janelas, nenhum `FeedFail`, nenhuma exceção.
Nenhum relatório WER novo e nenhum crash dump novo foram gerados durante o teste.

Tudo foi removido depois: comparação com o snapshot pré-instalação (`hk-antes.csv`) deu
**1785 arquivos antes, 1785 depois, nenhuma sobra e nada faltando**.

Duas observações do teste:

1. O `MV probe` ficou em zero porque o jogo permaneceu na tela de título (ninguém jogou). O
   `stale probe` alternava `SAME`/`changed` — a nota nova de "fonte parada" exige três amostras
   `SAME` consecutivas e, corretamente, não disparou.
2. **A mensagem nova do `Depth probe` precisa de ajuste.** Ela afirma
   `this host has no depth buffer (window/monitor capture)`, mas apareceu aqui, num jogo por
   injeção direta, onde a causa é outra (o Unity 2D não expõe profundidade ao ReShade). O texto
   deve dizer que não há profundidade **sem cravar a causa**; a menção à captura de janela cabe
   como exemplo, não como diagnóstico. *(Corrigido no mesmo dia — ver §10.)*

## 9. A reinstalação limpa no Witcher 3, e o bug que ela revelou

Feita em etapas, com backup completo em `D:\backup-witcher3-x64_dx12-20260911\` (74 arquivos) e
tudo o que foi trocado preservado ao lado com sufixo `.disabled-teste-20260911`.

Etapas 1 a 3 (backup, desabilitar duplicatas e o dx11-bridge, trocar `dxgi.dll` para o de 64 bits
e o feed para a 0.12.0) resolveram o que se propunham: **o ReShade voltou a carregar no
`witcher3.exe`**, coisa que não acontecia desde 04/09. Os 27 shaders compilaram.

Mas o jogo passou a **crashar em 7,57 segundos**. E o log mostrou, ao segundo:

```
CreateFeature failed 0xBAD00010 (UnsupportedParameter)     x2
re-initialising NGX after repeated feature-create failures
<menos de 1 s>  CRASH
```

**Isolamento por eliminação, uma variável por execução:**

| Configuração | Sobreviveu | Resultado |
|---|---|---|
| etapa 4 completa, feed ligado | 7,57 s | crash |
| `zzz_display_commander` desabilitado, feed ligado | 5,68 s | crash |
| tudo igual, só `enabled=0` no feed | 198 s | **não crashou** (encerrado à mão) |
| **feed ligado, com o fix da §10** | **219 s** | **não crashou** |

O passo do `zzz_display_commander` derrubou uma hipótese: desabilitá-lo não resolveu, e os cinco
`Could not find associated add-on and therefore failed to register an event` continuaram sem ele
— portanto vêm de outro add-on, provavelmente um dos RenoDX do jogo (API 18). Ele continua sendo
lixo (erro 193, `ERROR_BAD_EXE_FORMAT`: é o binário de 32 bits com nome de 64, MD5 idêntico ao
`.addon32`), mas não era o culpado.

**Isto reescreve a §4:** a combinação proibida `feed + dx11-bridge` estava de fato carregada no
crash de 30/08, mas agora se sabe que **o feed sozinho basta** para derrubar um jogo com DLSS
nativo. A combinação era agravante, não a causa.

## 10. O fix

`amd\feeder-amd\src-feeder\src\dlss5-feed.cpp`, em `OnCreateFeatureFailed()` (~2584).

`ReinitNgx()` faz `NVSDK_NGX_D3D12_Shutdown1(g.dev12)` — derruba o NGX do **device inteiro**. Nos
caminhos D3D11/Vulkan/GL esse device é privado, criado pelo feeder, e derrubá-lo não afeta
ninguém. No caminho **same-device D3D12** ele é o device do próprio jogo: se o jogo dirige o
próprio DLSS (Streamline, DLSS nativo), o shutdown fecha a sessão dele.

A condição necessária já existia no código: **`g.dev12_owned`** (`true` só quando o device é
nosso). O fix é usá-la:

- o re-init só acontece quando a sessão é nossa;
- quando não é, o feed registra o motivo e desiste em silêncio na terceira tentativa, com uma
  razão específica em vez da genérica.

```
[feed] not re-initialising NGX: the session lives on the game's device, not ours --
       tearing it down would close the game's own DLSS session
[feed] stopped: creating the DLSS feature keeps failing on the game's own device -- this game
       most likely drives DLSS itself (Streamline / built-in DLSS), and the feed stays off
       rather than disturb that session. The game renders normally.
```

Validado: 219 s de jogo vivo, `Responding=True`, 437 s de CPU, com o feed **ligado** — encerrado
à mão, não por crash. O mesmo binário no Hollow Knight não muda de comportamento, porque lá
`dev12_owned` é `true`.

No mesmo arquivo, o texto do `Depth probe` foi corrigido para constatar a ausência de
profundidade **sem afirmar a causa**, citando a captura de janela como exemplo e não como
diagnóstico (o motivo está na §8, observação 2).

## 11. Etapa 5: com o `renodx-dlss.addon64`

Instalado sobre a etapa 4 já corrigida. **250 segundos de jogo, `Responding=True`, sem crash e
sem nenhum erro novo no log.** O add-on carrega, engancha o NGX e anexa o snippet de Neural
Rendering:

```
[RenoDX DLSS] Registered add-on "RenoDX DLSS" v0.0.0.0 using ReShade API version 18.
[RenoDX DLSS] [RenoDX] DLSS-NR direct: attached snippet ...\nvngx_dlssnr.dll
```

Mas **fica inerte**: não há `CreateFeature(Reserved18)` nem `EvaluateFeature`. É coerente — o
RenoDX-NR trabalha em cima de uma avaliação DLSS existente (`replace_source`), e aqui não há
nenhuma: o feed desistiu, e o DLSS nativo do jogo não estava em uso na tela de menu onde a
medição foi feita.

**Resultado final por injeção direta no Witcher 3:** a cadeia **carrega e convive** — ReShade,
shaders, feed e os três RenoDX no mesmo processo, sem crash. O que **não** funciona é o DLSS 5
em si: o `CreateFeature` falha com `0xBAD00010` porque o jogo já tem uma sessão NGX própria com
o seu `nvngx_dlss.dll`. O caminho que a comunidade usa para isso é remover o `nvngx_dlss.dll`
local do jogo — o que apaga o DLSS nativo que o usuário talvez use, e por isso **não foi feito**.
