# Relatório — pasta `bin-amd\` no pacote portátil (passo 5 do PLANO-AMD)

Data: 2026-09-05. Escopo: `amd\montar-portable.ps1`, `amd\perfil-amd\*`, os textos do launcher e o `CREDITOS.md`. Nada foi tocado em `main.cpp`, `splash_ofertas.h`, `feeder-amd\`, `DLSS5ForAll-Portable\` ou na raiz do projeto. Nenhum programa foi instalado, o instalador do mod não foi baixado nem executado, nenhum binário do mod foi alterado.

## 1. O que o script faz agora

`amd\montar-portable.ps1` continua montando a raiz (launcher + LEIA-ME/README + ofertas.ini) e `bin\` como antes, e ganhou a seção 4, `bin-amd\`, com a mesma política: só copia, nunca apaga, e as configurações já existentes no destino são preservadas a não ser com `-SobrescreverConfig`.

| Passo | Origem | Destino em `bin-amd\` | Observação |
|---|---|---|---|
| 4a | `amd\` | `DLSS5ForAll.exe`, `dlss5-bridge.addon64`, `dxgi.dll`, `preset-01..05-*.ini`, `LICENSE`, `CREDITOS.md` | o mesmo exe do `bin\`; cópia tolerante a "arquivo em uso" (avisa e segue) |
| 4b | `amd\feeder-amd\dlss5-feed-amd.addon64` | `dlss5-feed.addon64` | se não existir: `[!] dlss5-feed-amd.addon64 ainda nao compilado: bin-amd fica sem Feeder` e segue. O Feeder oficial (NGX) **não** entra como reserva |
| 4c | `amd\perfil-amd\` | `amd_fidelityfx_dx12.dll`, `LICENSE-FidelityFX.txt`, `LEIA-ME-AMD.txt`, `README-AMD.txt`, `preparar-mod-amd.cmd`, `preparar-mod-amd.ps1` | DLL baixada da tag v1.1.4 do FidelityFX-SDK (6.675.664 bytes, SHA-256 `12a50812…93a6`), licença MIT da mesma tag |
| 4d | `amd\reshade-shaders\` | `reshade-shaders\Shaders`, `\include`, `\Textures` | iguais ao `bin\`; `reshade-cache\` criado **vazio** |
| 4e | `amd\FafaDLSS5.ini` | `FafaDLSS5.ini` | mesmo preset |
| 4e | `perfil-amd\dlss5forall.ini` | `dlss5forall.ini` | mínimo: fonte janela, sobrepor, convite já visto |
| 4e | `perfil-amd\dlss5-feed.cfg` | `dlss5-feed.cfg` | chaves do `bin\` + `amd_carrier=1`, `amd_color_fmt=87`, `amd_decoy_swapchain=0`. O script **avisa** se as chaves comuns divergirem do `amd\dlss5-feed.cfg` e **falha** se faltar alguma das três `amd_*` |
| 4e | `amd\ofertas.ini` | `ofertas.ini` | com `mostrar=0` forçado |
| 4e | `amd\ReShade.ini` | `ReShade.ini` | **derivado**: sem a seção `[RenoDX.DLSS5]`, `IntermediateCachePath=.\reshade-cache`, resto idêntico (teclas, preset, caminhos relativos, preprocessor). Falha se a seção sobrar ou se ficar caminho absoluto |
| 4f | — | — | avisa (não apaga) se `renodx-dlss5.addon64` ou `nvngx_dlss.dll` estiverem em `bin-amd\` |
| 4g | — | — | lista, só para informação, quais arquivos da pessoa/do mod ainda faltam |

Não copiados de propósito: `renodx-dlss5.addon64`, `nvngx_dlss.dll`, `nvngx_dlssnr.dll`, `setup-dlss5*.ps1`, `configurar-cadeia.cmd`, `gerar-luts.ps1`, `README.md` (o técnico continua em `bin\README.md`).

Por que o `ReShade.ini` é derivado e não mantido à mão em `perfil-amd\`: o arquivo de origem muda com frequência (teclas, `Docking`/`Window` regravados pelo ReShade) e a única diferença do perfil AMD é a ausência da seção do RenoDX. Duas cópias divergiriam em uma semana. Já o `dlss5-feed.cfg` é estático em `perfil-amd\` porque o conteúdo próprio (as chaves `amd_*` e os comentários) é o que importa, com a verificação de deriva compensando.

### Correção de bug pré-existente no mesmo script

A primeira execução parou na linha 92 (trecho do `bin\`, anterior a esta tarefa): o `bin\ReShade.ini` preservado do usuário tinha `IntermediateCachePath` absoluto, gravado pelo próprio ReShade em uso, e a checagem de "caminho absoluto" rodava também sobre arquivo preservado. Movi a checagem para dentro do `if` que escreve o arquivo (confere só o que o script acabou de gerar) e apliquei a mesma regra na seção `bin-amd\`. Sem isso o script falha toda vez que o usuário já usou o app uma vez.

## 2. Árvore final de `bin-amd\` (47 arquivos, 18,5 MB)

```
bin-amd\
  DLSS5ForAll.exe            2.337.749  (mesmo do bin\)
  dxgi.dll                   5.592.064  ReShade
  dlss5-bridge.addon64         453.569
  amd_fidelityfx_dx12.dll    6.675.664  FidelityFX 1.1.4, MIT, assinada pela AMD
  ReShade.ini                    3.079  sem [RenoDX.DLSS5], cache relativo
  FafaDLSS5.ini                  2.238
  dlss5-feed.cfg                   916  + amd_carrier/amd_color_fmt/amd_decoy_swapchain
  dlss5forall.ini                   89
  ofertas.ini                    4.378  mostrar=0
  preset-01-nucleo-afinado.ini … preset-05-nitidez.ini
  LEIA-ME-AMD.txt / README-AMD.txt      UTF-8 com BOM
  preparar-mod-amd.cmd / .ps1           conferidor (não baixa, não executa o setup)
  LICENSE, CREDITOS.md, LICENSE-FidelityFX.txt
  reshade-shaders\Shaders (19) \include (4)  Textures (4)
  reshade-cache\             vazio
  [ausentes por enquanto]  dlss5-feed.addon64 (variante AMD, agente feeder-amd)
  [da pessoa]              nvngx_dlssnr.dll, dlssnr_on_amd_setup.exe
  [gerados pelo setup]     version.dll, dlssnr_on_amd.ini, dlssnr_on_amd_weights.bin
```

`ReShade.ini` de `bin-amd\` conferido: seções `[ADDON] [DEPTH] [GENERAL] [INPUT] [OVERLAY] [PROXY] [SCREENSHOT] [STYLE]`, `AddonPath=.\`, `PresetPath=.\FafaDLSS5.ini`, `IntermediateCachePath=.\reshade-cache`, `KeyOverlay=36`. Ficou o fragmento `[Window][RenoDX-DLSSNR]` dentro da linha `Window=` do `[OVERLAY]` (posição de janela do ImGui; inofensivo e o ReShade regrava).

## 3. Textos e créditos

- `perfil-amd\LEIA-ME-AMD.txt` e `README-AMD.txt`: o que é, requisitos (Windows 11, Adrenalin 26.1.1+, RX 7000/9000, RX 9060 XT sem release por ser gfx1200), o que vem e o que a pessoa traz, passo a passo em 6 itens (nvngx_dlssnr.dll → baixar o setup → rodar dentro de `bin-amd` → conferir `Inline=1` → `preparar-mod-amd.cmd` → botão AMD), por que `Inline=1`, antivírus (issue 40), como saber se funciona (linhas do `dlssnr_on_amd.log`) e a tabela de sintomas → ajuste (`amd_color_fmt=10`, `amd_decoy_swapchain=1`, placa certa no seletor), desempenho, limites do beta, créditos.
- `portable-launcher\LEIA-ME.txt` e `README.txt`: bloco "MÓDULO AMD (beta)" / "AMD MODULE (beta)" inserido entre "Problemas comuns" e "Mover ou copiar a pasta". O resto não foi mexido.
- `CREDITOS.md`: bloco "Módulo AMD" com FidelityFX SDK 1.1.4 (MIT, fonte e tag), a variante AMD do Feeder (MIT, crédito ao autor original) e o DLSS-NR-on-AMD (não redistribuído, nem instalador nem arquivos gerados). Rodapé passa a citar AMD e o autor do mod.

Fatos das strings do instalador v0.2.11 usados nos textos: o template do `dlssnr_on_amd.ini` que ele grava já vem com `Inline=1`, `Interop=1`, `UseFsrInputs=1`, `UseDepth=1`, `HipDevice=-1`; ele reconhece a build 310.8.0.0 pelo SHA-256 `e16bcf15…fc8e` e, para outras builds, "checking its tensors instead" (aceita se todos baterem). Por isso o conferidor trata versão/hash como informação, não veredito.

## 4. `preparar-mod-amd.ps1` (o que confere)

Windows 11 (build), placas via `Win32_VideoController` (avisa RX 9060 XT), `amdhip64_7.dll` em System32, arquivos do pacote (inclusive a ausência do Feeder AMD), `nvngx_dlssnr.dll` (versão, tamanho, SHA-256 contra o do setup; oferece copiar de `..\bin\` se estiver lá), os três arquivos gerados pelo setup (com instrução de onde baixar e onde rodar), `dlssnr_on_amd.ini` (`Inline`, com oferta de corrigir para 1; `Interop`, `UseFsrInputs`, `UseDepth`, `Enabled`, `HipDevice`), as três chaves `amd_*` do cfg, e os logs `dlssnr_on_amd.log`/`dlss5-feed.log` traduzidos em veredito (network job, UNSUPPORTED GPU ARCHITECTURE, unsupported colour format, FSR is not active, no HIP device matches, D3D11 unsupported, FAULT). Chave `-SemPerguntas` para rodar sem interação. `.cmd` em ASCII puro (o cmd.exe usa a página OEM), `.ps1` com BOM.

## 5. Testes executados

| Teste | Resultado |
|---|---|
| `montar-portable.ps1` (sem `-SobrescreverConfig`) | 1ª vez: parou no bug pré-existente da linha 92 (acima). Após a correção: `ok`, 1839 arquivos / 485,7 MB no pacote, `bin-amd`: 47 / 18,5 MB, aviso do Feeder AMD ausente. 2ª execução: configs de `bin-amd` "mantidas", idempotente |
| Árvore e configs de `bin-amd` | conforme seção 2; `mostrar=0`; cfg com as três chaves; sem linhas em branco duplas no ini |
| `preparar-mod-amd.ps1 -SemPerguntas` dentro de `bin-amd` | 1ª vez: erro de parse (`"$nome:"`, corrigido para `${nome}`); depois saída completa e correta para esta máquina (RTX 4080 + iGPU "AMD Radeon(TM) Graphics" + monitores virtuais; HIP presente; faltam Feeder AMD, nvngx_dlssnr.dll e os três do setup) |
| Launcher com `DLSS5_LAUNCHER_FINGIR=amd` por 3 s | linha nova no `launcher.log`: `rtx=0 radeon=1 hip=1 nvidiaOk=0 amdOk=1 \| escolha=0 fechadoPor=9 apos 3047 ms \| nv="n" amd=""`. O botão AMD passou a `amdOk=1` porque `bin-amd\DLSS5ForAll.exe` existe. Fechei com WM_CLOSE (`CloseMainWindow`), ele saiu sozinho com exit 0, nada foi aberto, nenhum processo do usuário foi tocado |
| Processo `DLSS5ForAll` do usuário | não estava aberto em nenhum momento; a cópia do exe não falhou |

## 6. Pendências e avisos para os outros agentes

1. **feeder-amd**: o script espera exatamente `amd\feeder-amd\dlss5-feed-amd.addon64`. Além disso, no fonte do Feeder (`src/dlss5-feed.cpp`, `CfgSave()` ~linha 993) a página do overlay do ReShade **regrava o cfg inteiro só com as chaves conhecidas**; se a variante AMD não incluir `amd_carrier`, `amd_color_fmt` e `amd_decoy_swapchain` no `CfgSave()` (e no `CfgWriteDefault()`), qualquer ajuste feito pelo overlay apaga as três chaves e o portador volta ao padrão. O parser (`CfgReload()`) ignora linhas `#`, então os comentários do cfg são seguros.
2. **app-amd**: hoje o `bin-amd\DLSS5ForAll.exe` é o mesmo binário do `bin\`, ainda sem ramo AMD; até o app detectar `version.dll` + `dlssnr_on_amd.ini` (ou `--amd`), ao abrir pelo botão AMD ele vai mostrar a faixa "faltam renodx/nvngx_dlssnr" da cadeia NVIDIA. O launcher **não passa `--amd`** ao abrir `bin-amd\` (só repassa os argumentos do usuário), então a detecção por arquivos é o caminho que funciona sem mexer no launcher; se quiserem a flag, é uma linha em `launcher.cpp` (`Abrir(binAmd, args + L" --amd")`).
3. **nvngx_dlssnr.dll desta máquina** (`amd\` e `bin\`): versão de arquivo `310.8.SF.0`, SHA-256 `6eb209e7…3927`, diferente do `e16bcf15…fc8e` que o setup do mod conhece. O setup vai pelo caminho "checking its tensors"; se aceita ou não, só rodando ([TESTE]). Os textos já explicam isso.
4. O `reshade-cache\` de `bin-amd` nasce vazio: a primeira abertura recompila os shaders (alguns segundos a mais uma vez só).
5. Nada em `bin-amd` foi validado em hardware AMD (não há Radeon dedicada aqui). O que está validado é a montagem, os textos, o conferidor e a reação do launcher.

## 7. Arquivos criados/alterados

- `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\amd\montar-portable.ps1` (seção 4 nova, cabeçalho, correção da checagem do `bin\ReShade.ini`)
- `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\amd\perfil-amd\` — `amd_fidelityfx_dx12.dll`, `LICENSE-FidelityFX.txt`, `dlss5-feed.cfg`, `dlss5forall.ini`, `LEIA-ME-AMD.txt`, `README-AMD.txt`, `preparar-mod-amd.cmd`, `preparar-mod-amd.ps1`
- `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\amd\portable-launcher\LEIA-ME.txt` e `README.txt` (bloco AMD)
- `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\amd\CREDITOS.md`
- Pacote montado: `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\DLSS5ForAll-AMD\bin-amd\`
