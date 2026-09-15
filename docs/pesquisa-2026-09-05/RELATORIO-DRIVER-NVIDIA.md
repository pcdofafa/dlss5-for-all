# Driver NVIDIA atual x mais recente — traz algo que deixe o DLSS5 For All mais leve?

Pesquisa feita em 2026-09-05. Somente leitura do projeto (`amd\` e `DLSS5ForAll-AMD\bin\*.log`); nenhum arquivo do app foi alterado, nada foi instalado ou executado. Máquina: RTX 4080, Windows 11.

Pergunta do usuário: *"verifica o novo driver da NVIDIA; às vezes o driver novo traz algum arquivo ou recurso que deixe o nosso programa mais leve e otimizado. Se for o caso, baixa o driver atual, descompacta e vê o que tem dentro."*

**Resultado direto: não baixei o instalador.** A pesquisa (passo 2, abaixo) mostrou que o driver novo não traz nenhuma DLL ou recurso de NGX/DLSS relevante para nós — e as duas DLLs que mais importam para o app (`nvngx_dlss.dll` e `nvngx_dlssnr.dll`, embarcadas em `bin\`) já são iguais ou **mais novas** que tudo o que existe publicamente hoje, dentro ou fora de qualquer driver. Baixar e extrair o pacote de ~700 MB não acrescentaria informação; expliquei o porquê com fontes abaixo.

---

## 1. Estado local

### 1.1 Driver instalado

| Item | Valor |
|---|---|
| Driver (nvidia-smi) | **596.49** Game Ready, WHQL |
| GPU | GeForce RTX 4080 |
| Pasta no DriverStore | `nv_dispi.inf_amd64_b7cca8360c0d57e9` |
| `nvngx_update.exe` (marca a build completa do pacote) | FileVersion `32.0.15.9649` — o final "9649" bate com "596.49", confirmando a versão |

### 1.2 DLLs NGX/NVIDIA no sistema (DriverStore) x embarcadas no app

| Arquivo | Onde está | Versão | Tamanho | Usada pelo app? |
|---|---|---|---|---|
| `nvngx.dll` (núcleo NGX) | DriverStore (driver 596.49) | 30.0.14.9516 | 488.824 B | Sim, indiretamente (`NVSDK_NGX_D3D12_Init`) |
| `nvngx_dlssg.dll` (Frame Generation) | DriverStore (driver 596.49) | 310.2.1.0 | 9.299.624 B | **Não** — o app não usa DLSS-G hoje |
| `NvPresent64.dll` | DriverStore (driver 596.49) | sem FileVersion exposta (recurso interno) | 8.355.048 B | Não diretamente |
| `nvngx_dlss.dll` (Super Resolution/DLAA) | **`DLSS5ForAll-AMD\bin\`** (embarcada pelo app) | **310.8.0.0** | 58.956.400 B | **Sim** — é a que o Feeder carrega |
| `nvngx_dlssnr.dll` (DLSS 5 Neural Rendering) | **`DLSS5ForAll-AMD\bin\`** (embarcada pelo app) | **310.8.SF.0** | 165.830.144 B | Sim — usada pelo RenoDX DLSS5 |

Não há `nvngx_dlss.dll` nem `nvngx_dlssd.dll` (Ray Reconstruction) em nenhuma pasta do driver no sistema — confirma o que já era esperado: o pacote de driver da NVIDIA só traz o núcleo NGX e o Frame Generation; DLSS Super Resolution e Neural Rendering vêm do jogo/SDK, nunca do driver. Isso muda a estratégia do passo 3 (ver §4).

### 1.3 O que o app carrega de fato (logs, somente leitura)

Do `dlss5-feed.log` (Feeder 0.7.0, sessão de hoje):

```
[feed] adapter: NVIDIA GeForce RTX 4080  vram=16048 MB
[feed] NVSDK_NGX_D3D12_Init -> 0x00000001 (Success)
[feed] NGX capabilities: SuperSampling.Available=1 NeedsUpdatedDriver=0 MinDriver=470.0
[feed] DLSS render preset hint: 5 (E)
[feed] feature ready: 1920x1020 DLAA, flags=66 (SDR MVLowRes AutoExposure), ...
```

`MinDriver=470.0` confirma que o driver mínimo exigido pelo NGX para este recurso é bem antigo — o 596.49 instalado sobra de longe. O `dxgi.dll` do ReShade 6.8.0.2155 carrega o add-on `renodx-dlss5.addon64 v0.2026.828.517`, que abre a sessão D3D12 e inicializa o NGX apontando para as DLLs locais do `bin\` (não há caminho para `nvngx_dlssg` no log — Frame Generation não é acionado pelo app).

---

## 2. Pesquisa: o driver mais novo traz algo para nós?

| Fonte | Achado |
|---|---|
| [NVIDIA — GeForce Game Ready Driver 616.64](https://www.nvidia.com/en-us/geforce/news/nba-2k27-dlss-5-3d-guided-neural-rendering-geforce-game-ready-driver/) | Driver atual mais recente: **616.64 WHQL**, lançado em 03/09/2026 (supera o 616.56 de 26/08). Destaque único: suporte de jogo para NBA 2K27 com **DLSS 5 3D-Guided Neural Rendering — limitado a RTX 50**, e suporte a Onimusha: Way of the Sword. |
| [Notas oficiais 616.64 (PDF)](https://us.download.nvidia.com/Windows/616.64/616.64-win11-win10-release-notes.pdf) — lida na íntegra | "What's New": só CUDA 13.4 e o Game Ready da NBA 2K27. **`Fixed Gaming Bugs` = N/A** e **`Fixed General Bugs` = N/A** — não há nenhuma correção listada nesta versão. Nada sobre DLSS SR/NR, Smooth Motion, RTX Video, D3D12, HAGS ou WGC/overlay. |
| Mesmo PDF, "Open Issues 616.64" | Dois problemas **ainda abertos**: "Prefer Maximum Performance" pode não ser aplicado corretamente [6007998], e **"Intermittent flicker may be observed in browsers when navigating to certain websites"** [6673430]. |
| [VideoCardz — flicker no 616.56](https://videocardz.com/newz/geforce-616-56-driver-may-cause-monitor-flickering-nvidia-is-already-working-on-a-fix), [Guru3D](https://www.guru3d.com/story/nvidia-geforce-61656-driver-causes-flickering-on-some-8bit-monitors-as-fix-is-tested/) | O bug 6673430 é o mesmo flicker reportado em massa desde o 616.56: **piscadas/flashes pretos em monitores de 8 bpc**, sobretudo em apps baseados em Chromium e ao rolar página/vídeo. Contorno: trocar saída de cor para 10 bpc no NVIDIA App/Painel, ou habilitar Windows Auto Color Management. Vários usuários voltaram para o **610.88**. |
| [TechPowerUp — DLSS DLL catalog](https://www.techpowerup.com/download/nvidia-dlss-dll/) | Última **DLSS Super Resolution** catalogada publicamente: **310.7.129** (23/08/2026). Nosso `nvngx_dlss.dll` embarcado já é **310.8.0.0** — **mais novo** que o catálogo público. |
| [TechPowerUp — DLSS Frame Generation DLL](https://www.techpowerup.com/download/nvidia-dlss-3-frame-generation-dll/) | Última **Frame Generation** catalogada: **310.7.129** (26/08/2026) — mais nova que a que está no driver instalado (310.2.1.0), mas o app não usa Frame Generation hoje (ver `RELATORIO-METODOS-NVIDIA.md`, item 1: exige D3D12/Streamline, nosso swapchain é D3D11). |
| [VideoCardz — DLSS 5 Neural Rendering vazado da NBA 2K27](https://videocardz.com/newz/nvidia-dlss-5-neural-rendering-dll-found-in-nba-2k27-early-access-build-file-is-3x-larger-than-dlss-4) | O `nvngx_dlssnr.dll` que vazou da build antecipada da NBA 2K27 (26/08/2026) é identificado como **versão 310.8.0.0, ~158 MB** — essencialmente o mesmo build central que já embarcamos (`310.8.SF.0`, 165.830.144 B ≈ 158 MiB). **Não existe nada mais novo publicamente**; o que temos é a ponta mais avançada que já vazou de qualquer jogo. |
| [NVIDIA — Smooth Motion chega ao RTX 40](https://www.nvidia.com/en-us/geforce/news/nvidia-app-global-dlss-overrides-rtx-40-series-smooth-motion/), [Guru3D — driver 590.26](https://www.guru3d.com/story/enable-smooth-motion-on-rtx-40-series-gpus-with-geforce-59026-preview-driver/) | Smooth Motion oficial em RTX 40 exige só o driver **581.08** (ago/2025) — muito anterior ao 596.49 já instalado. Nada muda aqui com o 616.64. |

**Conclusão do passo 2:** o driver 616.64 não contém nenhuma atualização de DLSS SR/NR, Smooth Motion, RTX Video SR/HDR, D3D12/HAGS ou correção de overlay/WGC que nos interesse — e ainda carrega um bug de flicker em aberto. As duas DLLs que o app realmente usa (`nvngx_dlss.dll` e `nvngx_dlssnr.dll`) já estão à frente de tudo o que está publicamente catalogado, driver incluído (o pacote de driver nem contém essas duas DLLs — ver §1.2).

## 3. Por que não baixei e extraí o instalador

O item 3 da tarefa era condicional a achar algo relevante no item 2. Como:

1. o pacote de driver **não contém** `nvngx_dlss.dll` nem `nvngx_dlssd.dll` (confirmado no driver 596.49 já instalado, e é assim desde sempre — essas DLLs vêm do jogo/SDK);
2. as notas oficiais do 616.64 não citam nada de NGX, DLSS, RTX Video ou D3D12;
3. o único componente NGX que mudaria dentro do pacote de driver (`nvngx_dlssg.dll`, Frame Generation) não é usado pelo app hoje;

baixar ~700 MB pelo navegador e extrair com 7-Zip só confirmaria isso sem trazer nada de novo. Preferi documentar a conclusão com fontes verificáveis. Se quiser, faço o download e a extração mesmo assim para conferência visual — é rápido (7-Zip está em `C:\Program Files\7-Zip\7z.exe`) — mas não deve mudar a conclusão acima.

## 4. Vale atualizar o driver do usuário?

**Não, por ora.** Motivos:

- Não há nenhum ganho de DLSS/NGX documentado entre 596.49 e 616.64 para uma RTX 4080.
- `MinDriver=470.0` reportado pelo próprio NGX mostra que o 596.49 já está muito acima do mínimo exigido.
- O 616.64 carrega um bug de flicker **ainda aberto** (herdado do 616.56) que afeta monitores de 8 bpc e apps Chromium — risco real de regressão visual sem benefício compensador.
- Smooth Motion e o resto do que a NVIDIA lançou para RTX 40 recentemente já funciona no 596.49.

## 5. Vale trocar alguma DLL NGX embarcada?

**Não.** `nvngx_dlss.dll` (310.8.0.0) e `nvngx_dlssnr.dll` (310.8.SF.0) já são iguais ou mais novas que qualquer coisa publicamente disponível — inclusive mais novas que o catálogo do TechPowerUp e do que acabou de vazar de um jogo AAA. Trocar por algo do driver seria downgrade (o driver nem carrega essas DLLs) ou trocar por uma fonte sem procedência clara.

Nota de licença, para registro: `nvngx_dlss.dll` da SDK do DLSS é redistribuível sob a licença de parceiro; `nvngx_dlssg.dll` e qualquer `nvngx_dlssnr.dll` que viesse do driver ou de um vazamento de jogo **não são redistribuíveis** — a proveniência da nossa cópia (SDK 1.1.4, já documentada em `amd-nr-integration-findings.md`) deve continuar sendo a única fonte usada.

## 6. O que fazer agora

1. **Não atualizar** o driver do usuário para 616.64 agora — sem ganho para nós e com um bug de flicker aberto (6673430); reavaliar quando sair uma WHQL que o corrija.
2. **Não mexer** em `nvngx_dlss.dll` nem `nvngx_dlssnr.dll` em `bin\` — já são a versão mais avançada publicamente conhecida.
3. Guardar este relatório como referência: se um dia implementarmos a "Arquitetura B" (segundo apresentador D3D12 para DLSS-G, descrita em `RELATORIO-METODOS-NVIDIA.md` item 1), aí sim vale buscar `nvngx_dlssg.dll` 310.7.129 (TechPowerUp) em vez do 310.2.1.0 do driver do usuário — só nesse cenário futuro.
4. Se o usuário relatar telas piscando ou flashes pretos no navegador/overlay depois de qualquer atualização futura de driver, é o bug 6673430 conhecido: orientar a trocar a saída de cor para 10 bpc no NVIDIA App, não investigar como bug do nosso app.
5. Repetir esta checagem quando a NVIDIA lançar a próxima WHQL (ciclo mensal) ou quando algum jogo vazar uma versão de `nvngx_dlssnr`/`nvngx_dlss` mais nova que 310.8 — sinal de que valeria reabrir a comparação.

---

### Fontes

- [NVIDIA — GeForce Game Ready Driver 616.64: DLSS 5 em NBA 2K27, Onimusha & Dawnwalker](https://www.nvidia.com/en-us/geforce/news/nba-2k27-dlss-5-3d-guided-neural-rendering-geforce-game-ready-driver/)
- [Notas oficiais de versão do driver 616.64 (PDF)](https://us.download.nvidia.com/Windows/616.64/616.64-win11-win10-release-notes.pdf)
- [TechPowerUp — NVIDIA Releases GeForce 616.64 WHQL](https://www.techpowerup.com/352335/nvidia-releases-geforce-616-64-whql-game-ready-drivers)
- [Neowin — DLSS 5 driver com flickering](https://www.neowin.net/news/nvidias-dlss-5-driver-is-here-but-with-major-display-flickering-issue/)
- [VideoCardz — flicker no 616.56, correção em teste](https://videocardz.com/newz/geforce-616-56-driver-may-cause-monitor-flickering-nvidia-is-already-working-on-a-fix)
- [Guru3D — flicker em monitores de 8 bpc](https://www.guru3d.com/story/nvidia-geforce-61656-driver-causes-flickering-on-some-8bit-monitors-as-fix-is-tested/)
- [TechPowerUp — NVIDIA DLSS DLL (Super Resolution) 310.7.129](https://www.techpowerup.com/download/nvidia-dlss-dll/)
- [TechPowerUp — NVIDIA DLSS Frame Generation DLL 310.7.129](https://www.techpowerup.com/download/nvidia-dlss-3-frame-generation-dll/)
- [VideoCardz — DLSS 5 Neural Rendering DLL vazado da NBA 2K27](https://videocardz.com/newz/nvidia-dlss-5-neural-rendering-dll-found-in-nba-2k27-early-access-build-file-is-3x-larger-than-dlss-4)
- [Guru3D — primeiro runtime DLSS 5 encontrado na NBA 2K27](https://www.guru3d.com/story/first-dlss-5-runtime-discovered-in-nba-2k27-pc-build/)
- [NVIDIA — NVIDIA App traz Smooth Motion para RTX 40](https://www.nvidia.com/en-us/geforce/news/nvidia-app-global-dlss-overrides-rtx-40-series-smooth-motion/)
- [Guru3D — Smooth Motion via driver 590.26 preview](https://www.guru3d.com/story/enable-smooth-motion-on-rtx-40-series-gpus-with-geforce-59026-preview-driver/)
