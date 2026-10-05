# Créditos e licenças

🇺🇸 [English version](CREDITS.md)

**Código deste projeto (MIT — ver `LICENSE`):** `DLSS5ForAll.exe` (`main.cpp`, `screen_capture.h`, `hdr_control.h`,
`splash_ofertas.h`, `qr.h`, `reshade_bridge.h`), o add-on `dlss5-bridge.addon64` (`addon\`), os shaders
`DLSS5_Grade.fx`, `DLSS5_Sharpen.fx`, `DLSS5_Detalhes.fx` e `DLSS5_Noturno.fx`, as LUTs `dlss5_grade_luts.png` (geradas por
`gerar-luts.ps1`) e os presets.

**Cabeçalhos da API de add-ons do ReShade** (`addon\include\reshade\`): © crosire, BSD-3-Clause OR MIT — usados sob MIT
(`addon\include\reshade\LICENSE-ReShade.md`).

**Componentes de terceiros que o `setup-dlss5.ps1` baixa (cada um com a própria licença; não são deste projeto):**
ReShade (crosire), DLSS5-Feeder (jlrouzies-fr), LumeniteFX (umar-afzaal), `nvngx_dlss.dll` (NVIDIA, do driver).

**LumeniteFX — baixado, nunca redistribuído.** Os arquivos `reshade-shaders\Shaders\lumenite_*.fx`, os
`reshade-shaders\Shaders\include\lumenite_*.fxh` e a textura `reshade-shaders\Textures\lumenite_bluenoise256.png` são
© umar-afzaal sob **AGNYA License** (github.com/nvb-uy/AGNYA-License), que **proíbe redistribuição**. Por isso eles
**não estão** em nenhum pacote, instalador ou portable deste projeto, e o `reshade-cache` também sai vazio (ele levaria
os mesmos shaders já compilados). Quem instala os recebe do passo 4 do `setup-dlss5.ps1`, que os baixa do repositório
do autor (`codeload.github.com/umar-afzaal/LumeniteFX`, ramo `mainline`) direto para a máquina da pessoa. Sem esse
passo, a técnica `Lumenite_Kernel` do preset simplesmente não carrega — rode o `setup-dlss5.ps1`.

**Shaders do pacote SweetFX incluídos (janela "Filtros"):** © CeeJayDK (Christian Cann Schuldt Jensen), licença MIT —
`reshade-shaders\Shaders\LICENSE-SweetFX.txt` (fonte: github.com/CeeJayDK/SweetFX, o pacote "SweetFX by CeeJay.dk" do
instalador oficial do ReShade). Arquivos, sem alteração: `Vignette.fx` (Vinheta), `Sepia.fx` (Sépia), `Monochrome.fx`
(Preto e branco), `Curves.fx` (Curvas), `FilmGrain.fx` (Grão de filme), `Vibrance.fx` (Vibrance), e o anti-serrilhado
`SMAA.fx` e `SMAA.fxh` (`LICENSE-SweetFX-SMAA.txt`); o algoritmo SMAA é © 2013 Jorge Jimenez, Jose I. Echevarria, Belen
Masia, Fernando Navarro e Diego Gutierrez (licença permissiva no cabeçalho do `SMAA.fxh`). As texturas `AreaTex.png` e
`SearchTex.png` vêm do mesmo pacote. Os filtros do NVIDIA Freestyle serviram só de referência de FUNÇÃO: nenhum código
da NVIDIA está aqui.

**O que você traz e NÃO é redistribuído aqui:** `renodx-dlss5.addon64` (RenoDX) e `nvngx_dlssnr.dll` (build interno
vazado da NVIDIA — não distribua).

**AMD FidelityFX (na pasta `bin\`):**
- `amd_fidelityfx_dx12.dll` — AMD FidelityFX SDK **1.1.4** (FSR 3.1.4), © Advanced Micro Devices, Inc., licença **MIT**
  (`LICENSE-FidelityFX.txt`, na mesma pasta que a DLL; fonte: github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK,
  tag v1.1.4). Binário assinado pela AMD, redistribuído sem alteração. A DLL declara `FileVersion 1.0.1.41314`,
  `ProductName "AMD FidelityFX"`, `FileDescription "DX12 AMD FidelityFX Library"`, `Copyright (C) 2024` — esse
  `1.0.1` é a versão da **FFX API** (a interface da DLL), não a do SDK. Ela serve aos dois modos porque o Frame
  Generation usa a interpolação da AMD também no caminho NVIDIA (`fg_ffx.h`). FSR e Radeon são marcas da AMD.
  *Nota de licença (22/09/2026):* o pacote do FidelityFX SDK **2.x** trocou a MIT por uma licença só-binário com
  proibição de engenharia reversa. Isso **não** se aplica aqui: a linha que distribuímos é a **1.1.4**, que é MIT.
  Se um dia a DLL for trocada por uma da linha 2.x, este texto e o `LICENSE-FidelityFX.txt` têm de ser trocados junto.

**Placas AMD Radeon (modo AMD, experimental; o mod mora em `bin\mod-amd\`):**
- `dlss5-feed.addon64` de `bin\` é a **variante AMD do DLSS5-Feeder** (jlrouzies-fr, MIT), um binário só para as duas
  placas: fonte original mais a ramificação "portador FSR" deste projeto, compilada com MinGW. O mérito do Feeder é do
  autor original.
- **DLSS-NR-on-AMD**, de Daniel Blanco (danielblnc), https://github.com/danielblnc/DLSS-NR-on-AMD: o mod que roda a rede
  do DLSS 5 em Radeon. **Não é distribuído com este programa, por exigência da licença dele** — nem o instalador, nem
  `version.dll`, `dlssnr_on_amd.ini` ou `dlssnr_on_amd_weights.bin`. Você baixa o instalador da página oficial do autor
  e o roda dentro de `bin\mod-amd\`, com a sua cópia do `nvngx_dlssnr.dll` ao lado (passo a passo em
  `bin\mod-amd\LEIA-ME.txt`); o programa só carrega o mod quando a Radeon é a placa escolhida.

**Launcher da versão portable (`DLSS5ForAll.exe` da raiz):**
- `WebView2Loader.dll` — Microsoft Edge WebView2 SDK, versão 1.0.3912.50, © Microsoft Corporation, licença
  BSD-3-Clause do SDK (`bin\LICENSE-WebView2.txt`), redistribuída sem alteração. Origem da cópia atual: o pacote
  `mingw-w64-ucrt-x86_64-webview2-loader` do MSYS2 (mesma versão e mesma licença do pacote NuGet
  `Microsoft.Web.WebView2`; `portable-launcher\preparar-webview2-sdk.ps1` troca pela DLL do NuGet, assinada pela
  Microsoft, quando o `.nupkg` estiver em Downloads). É só a ponte até o runtime do WebView2 já instalado no
  Windows (o runtime "Evergreen" **não** é redistribuído aqui); ela mostra a vitrine de ofertas na coluna da
  esquerda da tela do launcher (`vitrine.h`). Sem ela, ou sem o runtime, a tela abre normalmente sem a coluna.
  Microsoft Edge e WebView2 são marcas da Microsoft.

DLSS e RTX são marcas da NVIDIA. Este projeto não tem relação com a NVIDIA, com a AMD nem com o autor do mod.
