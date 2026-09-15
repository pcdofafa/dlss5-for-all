# Créditos e licenças

🇺🇸 [English version](CREDITS.md)

**Código deste projeto (MIT — ver `LICENSE`):** `DLSS5ForAll.exe` (`main.cpp`, `screen_capture.h`, `hdr_control.h`,
`splash_ofertas.h`, `qr.h`, `reshade_bridge.h`), o add-on `dlss5-bridge.addon64` (`addon\`), os shaders
`DLSS5_Grade.fx` e `DLSS5_Sharpen.fx`, as LUTs `dlss5_grade_luts.png` (geradas por `gerar-luts.ps1`) e os presets.

**Cabeçalhos da API de add-ons do ReShade** (`addon\include\reshade\`): © crosire, BSD-3-Clause OR MIT — usados sob MIT
(`addon\include\reshade\LICENSE-ReShade.md`).

**Componentes de terceiros que o `setup-dlss5.ps1` baixa (cada um com a própria licença; não são deste projeto):**
ReShade (crosire), DLSS5-Feeder (jlrouzies-fr), LumeniteFX (umar-afzaal), `nvngx_dlss.dll` (NVIDIA, do driver).

**Shader de anti-serrilhado incluído no pacote (janela "Efeitos"):** `SMAA.fx` e `SMAA.fxh` do pacote SweetFX
(CeeJayDK, MIT — `reshade-shaders\Shaders\LICENSE-SweetFX-SMAA.txt`); o algoritmo SMAA é © 2013 Jorge Jimenez,
Jose I. Echevarria, Belen Masia, Fernando Navarro e Diego Gutierrez (licença permissiva no cabeçalho do `SMAA.fxh`).
As texturas `AreaTex.png` e `SearchTex.png` vêm do mesmo pacote.

**O que você traz e NÃO é redistribuído aqui:** `renodx-dlss5.addon64` (RenoDX) e `nvngx_dlssnr.dll` (build interno
vazado da NVIDIA — não distribua).

**Módulo AMD (pasta `bin-amd\`, beta):**
- `amd_fidelityfx_dx12.dll` — AMD FidelityFX SDK 1.1.4 (FSR 3.1.4), © Advanced Micro Devices, Inc., licença MIT
  (`LICENSE-FidelityFX.txt`; fonte: github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK, tag v1.1.4). Binário assinado
  pela AMD, redistribuído sem alteração. FSR e Radeon são marcas da AMD.
- `dlss5-feed.addon64` do `bin-amd\` é a **variante AMD do DLSS5-Feeder** (jlrouzies-fr, MIT): fonte original mais a
  ramificação "portador FSR" deste projeto, compilada com MinGW. O mérito do Feeder é do autor original.
- **DLSS-NR-on-AMD** (danielblnc, github.com/danielblnc/DLSS-NR-on-AMD): o mod que roda a rede do DLSS 5 em Radeon.
  **NÃO é redistribuído** — nem o instalador, nem `version.dll`, `dlssnr_on_amd.ini` ou `dlssnr_on_amd_weights.bin`.
  Você baixa o instalador do autor e o roda dentro de `bin-amd\`, sobre a sua cópia do `nvngx_dlssnr.dll`.

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
