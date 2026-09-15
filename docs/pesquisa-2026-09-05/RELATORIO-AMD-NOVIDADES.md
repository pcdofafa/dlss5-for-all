# Novidades do mod DLSS-NR-on-AMD desde 2026-09-04

Checagem feita em **2026-09-05 às 15:08 (horário local, 18:08 UTC)**, via API do GitHub (`gh api`, conta autenticada), páginas HTML do GitHub e busca na web. Nenhum arquivo foi baixado e nada foi alterado fora deste relatório e do arquivo de memória.

## Resposta curta

- **Nenhuma release nova foi publicada.** A mais recente continua sendo a **v0.2.11** ("Alpha 0.2.11", publicada em 2026-09-05 02:19 UTC), com o mesmo `dlssnr_on_amd_setup.exe` de 4.737.763 bytes e SHA-256 `dbf7e40c…03a74`. É exatamente o arquivo que está em `perfil-amd\` e `bin-amd\` (hash conferido localmente). Por isso o passo 3 da tarefa (baixar pelo navegador, copiar, renomear) **não se aplicou**.
- **Mas o repo se mexeu bastante hoje:** há uma tag `v0.2.12` (só README), e commits chamados "Tag v0.2.13" (07:41 UTC) e "Tag v0.2.14" (16:53 UTC) sem release nem tag pública. O README passou a dizer "Join the Discord for support, **early access to new mod builds** and testing", ou seja, as builds 0.2.13 e 0.2.14 estão circulando só no Discord do autor.
- **A notícia que importa para nós:** o autor respondeu hoje na issue 71 ("Working on rx 9060 xt with a FIX") **"Will be fixed in v0.2.14"** (17:50 UTC), e na issue 49 (16:44 UTC) que "the permissions for the ini file should be fixed soon along with **not needing a patched exe for the 9060 XT**", mais "the next two builds will increase performance by a combined 8%". O suporte oficial a gfx1200 (RX 9060 XT) está prometido para a próxima release pública.

## Estado do repo

| Item | Em 2026-09-04 (o que integramos) | Agora (2026-09-05 18:08 UTC) |
|---|---|---|
| Release mais recente | v0.2.11 (05/09 02:19 UTC) | **v0.2.11, sem mudança** |
| Instalador | `dlssnr_on_amd_setup.exe`, 4.737.763 B, `dbf7e40c…` | idêntico |
| Tags | até v0.2.11 | + `v0.2.12` (aponta para commit "Updated README" de 04:49 UTC; não há release) |
| Commits | "Tag v0.2.11" | + "Updated README" (02:56, 04:49, 18:05 UTC), **"Tag v0.2.13"** (07:41), **"Tag v0.2.14"** (16:53). Os commits "Tag" não alteram arquivo nenhum (o repo só tem `README.md` e `.github/FUNDING.yml`) |
| Código-fonte | não publicado; promessa na issue 4 (04/09) | continua não publicado; sem comentário novo do autor sobre isso |
| Estrelas / forks / issues abertas | (não anotado) | 617 / 25 / 68 |

### O que mudou no README (diff 2970755f → 67486de3)

- Título agora inclui **RX 7000 (RDNA3)** como suportada ("should work, reports are welcome"; antes "untested").
- Link para o **Discord** (`discord.gg/5gCwc6mskc`) com "early access to new mod builds and testing".
- Desempenho citado subiu de 28 para **31 FPS em 1080p na 9070 XT**.
- Requisito de driver ficou explícito: **Adrenalin 26.1.1 ou mais novo** (nosso `preparar-mod-amd.ps1` já diz isso).
- Nova seção FAQ: "não é compatibility layer; reimplementação do zero em HIP; nenhum código NVIDIA incluído ou traduzido em runtime".

## Tabela do que mudou e o impacto em nós

| # | Item | Fonte | Impacto em nós |
|---|---|---|---|
| 1 | Nenhuma release nova; v0.2.11 continua a atual | releases API / página de releases | **Nenhum.** Instalador, hash e nome batem com o que temos |
| 2 | Autor: gfx1200 (RX 9060 XT) "will be fixed in v0.2.14"; "not needing a patched exe for the 9060 XT" | issues 71 e 49 (comentários de danielblnc hoje) | **Oportunidade grande.** Quando a v0.2.14 sair como release pública, a RX 9060 XT do usuário passa a ser suportada sem patch de ELF. Ajustar textos que dizem "RX 9060 XT: AINDA NÃO" (LEIA-ME-AMD, README-AMD, aviso do `preparar-mod-amd.ps1`) assim que confirmar |
| 3 | v0.2.13 e v0.2.14 existem só no Discord (early access) | README + commits "Tag v0.2.13/14" sem release | **Nenhum agora.** Não temos como baixar sem entrar no Discord; não recomendo distribuir build de early access |
| 4 | "Próximas duas builds: +8% de desempenho combinado" | issue 49 | Nenhum no código; melhora a expectativa (~62 ms por job na 9060 XT hoje → ~57 ms) |
| 5 | Permissão de escrita do `dlssnr_on_amd.ini` ("cannot write … settings will not persist") será corrigida | issue 49 | **Nenhum para nós:** nosso app roda em pasta do usuário (Desktop), não em Program Files; o `AlternarNeuralAmd()` grava o ini pelo app, não pelo mod |
| 6 | RE Engine (Resident Evil Requiem, Monster Hunter Wilds) "potentially fixed in v0.2.14" | issues 37 e 35 | Nenhum direto. Mas a issue 35 confirma que o **formato de cor 67 (R9G9B9E5) é rejeitado** ("unsupported colour format 67 / setup failed; idle") e que o mod também hooka `amd_fidelityfx_upscaler_dx12.dll` e `amd_fidelityfx_loader_dx12.dll` (SDK 2.x), além da `amd_fidelityfx_dx12.dll` 1.1.x que usamos. Nossa escolha do SDK 1.1.4 continua válida |
| 7 | Chaves do ini: nenhuma nova. A issue 72 publica um ini completo em uso na 9060 XT: `Enabled=1 LocalTone=0 LocalStructure=1 SkinStructure=-1 UseAutoMask=1 ToneChannels=0 Scale=0.03125 Temporal=0 Tonemap=-1 UseFsrInputs=1 UseDepth=1 Interop=1 Inline=1 HipDevice=-1` | issue 72 | **Nenhum** (mesmas chaves do Apêndice A do PLANO-AMD). Detalhe: a comunidade está usando **`Temporal=0`** como "configuração estável" na 9060 XT; vale considerar como valor sugerido no LEIA-ME se aparecer flicker |
| 8 | Linhas de log: nenhuma nova entre as que lemos. Confirmadas em logs reais de hoje: `env: using HIP device 0 (matches the game's D3D12 adapter)`, `engine init ok`, `hooked amd_fidelityfx_dx12.dll!ffxDispatch`, `first ffxDispatch type 0x10001`, `staging ready: colour 1920x1080`, `interop: inputs shared (zero-copy), output shared (zero-copy)`, `network job N done in M ms`, `self-check: pre-block zero bytes 0.108% (healthy)`, `route fsr`, `frames 600 dispatches 0 submitted 0 ready -1 route backbuffer`, `unsupported colour format 67`, `setup failed; idle`, `inline: N GPU wait timeouts so far`, `inline: too many frames in flight, skipping one`, `staging re-created (settings change)`, `FAULT: exception 0x80000003` | issues 49, 62, 35, 59 | **Nenhum.** O parser do `main.cpp` (RELATORIO-APP-AMD) e o do `preparar-mod-amd.ps1` já cobrem as linhas decisivas. Sugestão barata: tratar `setup failed; idle` e `route backbuffer` como "mod não pegou o FSR" no `preparar-mod-amd.ps1` |
| 9 | Nome do instalador, `version.dll`, `dlssnr_on_amd.ini/.log/_weights.bin` | README atual | **Nenhum.** Tudo igual |
| 10 | Nenhum PR aberto no repo; 25 forks, todos cópias vazias exceto dois (abaixo) | forks API | Nenhum |
| 11 | ReShade: issue 72 ("Request: ReShade Support / Compatibility Layer for DLSS-NR on AMD", por lolo3lili, hoje) com um zip "DLSS-NR-AMD-ReShade-Layer.zip" pedindo colaboradores | issue 72 | **Oportunidade / concorrência.** É exatamente a ideia do nosso módulo AMD (Feeder como portador FSR + mod). O zip é binário de terceiro sem fonte; não baixei. Vale acompanhar a issue; se o autor abrir repo com fonte, comparar com o nosso `dlss5-feed-amd` |
| 12 | Issue 43: o `version.dll` do mod funciona renomeado para `.asi` com um ASI loader | issue 43 | Nenhum (não usamos ASI loader), mas confirma que o mod não depende do nome `version.dll` para inicializar, só do ponto de injeção |
| 13 | Driver 26.9.1: relatos de tela preta na 9070 XT com v0.2.11 (issue 68, comentário de ziyi233) e sugestão de usar v0.2.10 | issue 68 | Anotar no LEIA-ME como sintoma conhecido ("tela preta com UI visível": tentar driver anterior ou v0.2.10). Não é ação de código |
| 14 | Overlay do mod (End) não abre com MSI Afterburner aberto antes do jogo (issue 52) | issue 52 | Anotar no LEIA-ME (não usamos o overlay do mod, mas o usuário pode querer abri-lo) |
| 15 | Flicker some ao desativar a iGPU no Gerenciador de Dispositivos (issue 51) | issue 51 | Relevante para o PC do usuário (RTX 4080 + RX 9060 XT + iGPU?): reforça o risco multi-GPU já anotado na memória |

## Estado do suporte a gfx1200 (RX 9060 XT)

- **Release pública:** continua **sem** suporte. A v0.2.11 traz kernels só para gfx1100/1101/1102/1201. Confirmação independente: a análise estática do fork `0x-punk/DLSS-NR-on-AMD-DUMP-` (sobre a v0.2.10) lista as mesmas quatro arquiteturas no bundle `__CLANG_OFFLOAD_BUNDLE__`.
- **Promessa do autor (hoje):** "Will be fixed in v0.2.14" (issue 71, 17:50 UTC) e "not needing a patched exe for the 9060 XT" (issue 49, 16:44 UTC). A v0.2.14 já existe como build (commit "Tag v0.2.14" às 16:53 UTC) mas só no Discord.
- **Enquanto isso:** a comunidade continua usando o patch da issue 33 (e_flags 0x4E→0x48 e strings gfx1201→gfx1200). Relatos de hoje com esse patch: Fort Solis ~20 fps 1080p (issue 71), Helldivers 2 ~10 fps 1440p (issue 54), GTA V Enhanced ~62 ms por job = ~16 fps (issue 49), KCD2 15 fps 1080p (issue 33). O autor confirmou que **~60 ms por job é o esperado na 9060 XT** (metade da 9070 XT, que faz ~30 ms). Ou seja, mesmo com suporte oficial, a 9060 XT em 1080p fica na faixa de 15 a 20 fps de rede neural.
- **Recomendação mantida:** não redistribuir nem aplicar o patch; esperar a v0.2.14 pública. Nada muda no nosso código para isso: o `preparar-mod-amd.ps1` e o `main.cpp` decidem pelo log (`UNSUPPORTED GPU ARCHITECTURE`/`invalid kernel file`), então uma release nova com gfx1200 passa a funcionar sem recompilar.

## Forks e projetos paralelos (só o que tem fonte verificável)

| Projeto | O que é | Relevância |
|---|---|---|
| `guentra/DLSS-NR-on-AMD-Linux` (fork, 1 estrela, commits de hoje) | Instalador Linux/Proton para o mod v0.2.11 (Python, baixa ROCm em user-space, wrapper de launch) | Nenhuma para Windows; mostra que o mod roda sob Proton com HIP 7 |
| `0x-punk/DLSS-NR-on-AMD-DUMP-` (fork, só docs) | Análise estática do instalador v0.2.10: PE duplo (setup + `version.dll` embutido no overlay em 0x3A800), 34 kernels HIP, FP8 E4M3 + fp32, zero-copy via `CreateSharedHandle → hipImportExternalMemory`, hooks só em DXGI/ExecuteCommandLists | Útil como referência técnica (confirma nossas inferências do PLANO-AMD: staging por `hipImportExternalMemory`, residual aplicado por shader HLSL na fila do jogo, exigência de UAV na saída) |
| `xXJSONDeruloXx/dlssnr-proton` (GPL-3.0, 0 estrelas) | "DLSS NR through FSR on RDNA4 and Proton; asynchronous HIP staging bridge"; baixa o runtime v0.2.10 do mod | Paralelo Linux; usa o mesmo mod por baixo |
| `xXJSONDeruloXx/dlssnr-native` (GPL-3.0) | Backend Vulkan nativo em desenvolvimento ("104 kernels still stop at unsupported memory-barrier operations"), sem HIP | Longe de jogável |
| `RedDukeDev/dlss5-image-enhancer-zluda` (MIT, 5 estrelas; issue 63) | Roda o `nvngx_dlssnr.dll` original em AMD via fork do ZLUDA, só imagem estática; autor diz ter um plugin ReShade experimental "lento demais" | Abordagem alternativa (CUDA→HIP por tradução), desempenho ruim |
| `vladbogun1/dlss5-amd` | Ferramentas de pesquisa clean-room (inspeção do PE, inventário de PTX, protótipos Vulkan); último push 02/09 | Não é runtime |
| `NapXDD/addon-dlssnr-linux` (GPL-3.0, 4 estrelas) | Add-on ReShade para DLSS NR em **NVIDIA** sob Linux/Proton (dirige o snippet como feature 18) | Não é AMD, mas é um add-on ReShade com fonte que fala com o `nvngx_dlssnr.dll` diretamente; referência para a cadeia NVIDIA |

Imprensa (sem novidade técnica além do que está acima): TechPowerUp, VideoCardz, PC Gamer, OC3D cobriram o mod entre 03 e 05/09 com os números da 9070 XT (28 a 31 fps em 1080p). O thread do Guru3D ("DLSS 5 NR on AMD RDNA3&4 thread", id 461672) existe mas retornou 403 para leitura automática. Não encontrei thread relevante no Reddit ou ResetEra com informação além das issues. Nexus Mods não tem o mod.

## O que fazer agora

1. **Nada a trocar de binário.** Manter o `dlssnr_on_amd_setup.exe` v0.2.11 (`dbf7e40c…`) em `perfil-amd\` e `bin-amd\`.
2. **Vigiar a v0.2.14 pública.** Checar `https://api.github.com/repos/danielblnc/DLSS-NR-on-AMD/releases/latest` (ou a página de releases) ao longo do fim de semana; quando `tag_name` passar de v0.2.11, baixar pelo navegador do usuário, conferir o SHA-256 do campo `digest`, e só então testar na RX 9060 XT.
3. **Preparar os textos para a virada** (sem publicar ainda): no `LEIA-ME-AMD.txt`, `README-AMD.txt` e no aviso da RX 9060 XT do `preparar-mod-amd.ps1`, trocar "AINDA NÃO / sem release oficial" por "precisa da v0.2.14 ou mais nova do mod" assim que ela sair. Manter a decisão pelo log (`UNSUPPORTED GPU ARCHITECTURE`, `invalid kernel file`), que já funciona para qualquer versão.
4. **Ajuste barato no `preparar-mod-amd.ps1` (opcional):** reconhecer `setup failed; idle` e `route backbuffer` como "o mod não pegou o FSR" (mesmo veredito do `FSR is not active`), e `inline: … GPU wait timeouts` como aviso de desempenho.
5. **Anotar no LEIA-ME os sintomas de hoje:** tela preta com UI visível no driver 26.9.1 (tentar driver anterior); overlay End não abre com MSI Afterburner aberto antes; flicker some desativando a iGPU; `Temporal=0` é o que a comunidade usa como estável na 9060 XT.
6. **Expectativa de desempenho a comunicar:** na RX 9060 XT, ~60 ms por quadro da rede em 1080p (15 a 20 fps) mesmo com suporte oficial; o autor confirma que é metade da 9070 XT.
7. **Acompanhar a issue 72** (camada ReShade da comunidade) e a issue 4 (código-fonte). Se o fonte sair, recompilar com `--offload-arch=gfx1200` deixa de ser necessário porque o autor já vai incluir; o ganho seria poder auditar o hook de `ffxDispatch` e o formato de cor aceito.

## Fontes

- Releases: https://github.com/danielblnc/DLSS-NR-on-AMD/releases e https://api.github.com/repos/danielblnc/DLSS-NR-on-AMD/releases/latest (v0.2.11, digest `sha256:dbf7e40ca6940924ffe0960b350f3d55cb99ed27dde84aec47cdf8a0b7303a74`)
- Commits/tags: https://github.com/danielblnc/DLSS-NR-on-AMD/commits/master (be632789 "Tag v0.2.14", ac19c71d "Tag v0.2.13", a619605a = tag v0.2.12), https://api.github.com/repos/danielblnc/DLSS-NR-on-AMD/tags
- README atual: https://github.com/danielblnc/DLSS-NR-on-AMD (commit 67486de3)
- gfx1200: issue 71 https://github.com/danielblnc/DLSS-NR-on-AMD/issues/71 ("Will be fixed in v0.2.14"), issue 49 https://github.com/danielblnc/DLSS-NR-on-AMD/issues/49, issue 33 https://github.com/danielblnc/DLSS-NR-on-AMD/issues/33, issue 59 https://github.com/danielblnc/DLSS-NR-on-AMD/issues/59, issue 26 https://github.com/danielblnc/DLSS-NR-on-AMD/issues/26, issue 54 https://github.com/danielblnc/DLSS-NR-on-AMD/issues/54
- Formato de cor / RE Engine: issue 35 https://github.com/danielblnc/DLSS-NR-on-AMD/issues/35, issue 37 https://github.com/danielblnc/DLSS-NR-on-AMD/issues/37, issue 38 https://github.com/danielblnc/DLSS-NR-on-AMD/issues/38
- ReShade layer da comunidade: issue 72 https://github.com/danielblnc/DLSS-NR-on-AMD/issues/72
- Código-fonte: issue 4 https://github.com/danielblnc/DLSS-NR-on-AMD/issues/4
- Outros: issues 43, 51, 52, 62, 63, 68 no mesmo repo
- Forks/paralelos: https://github.com/guentra/DLSS-NR-on-AMD-Linux, https://github.com/0x-punk/DLSS-NR-on-AMD-DUMP-, https://github.com/xXJSONDeruloXx/dlssnr-proton, https://github.com/xXJSONDeruloXx/dlssnr-native, https://github.com/RedDukeDev/dlss5-image-enhancer-zluda, https://github.com/vladbogun1/dlss5-amd, https://github.com/NapXDD/addon-dlssnr-linux
- Imprensa: https://www.techpowerup.com/352360/modders-get-dlss-5-working-on-amd-graphics-cards-though-performance-is-rough, https://videocardz.com/newz/modder-found-a-way-to-run-nvidia-dlss-5-neural-rendering-on-radeon-rx-9000-and-rx-7000-gpus, https://www.pcgamer.com/hardware/graphics-cards/me-you-can-now-run-dlss-5-on-amd-gpus-my-rx-9070-xt-please-stop-it-hurts-us/, https://forums.guru3d.com/threads/dlss-5-nr-on-amd-rdna3-4-thread.461672/ (403 na leitura automática)
