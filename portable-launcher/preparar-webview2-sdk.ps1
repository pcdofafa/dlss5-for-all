# =====================================================================================
#  preparar-webview2-sdk.ps1 - tira do pacote NuGet Microsoft.Web.WebView2 (um .nupkg, que e
#  um zip) so o que o launcher precisa e poe em portable-launcher\webview2\ (a mesma pasta que
#  hoje vem do pacote mingw-w64-ucrt-x86_64-webview2-loader do msys2; a DLL do NuGet e a
#  assinada pela Microsoft, por isso e a preferida quando existir):
#     include\WebView2.h (+ WebView2EnvironmentOptions.h)   o cabecalho (compilacao)
#     WebView2Loader.dll                                     x64, vai ao lado do launcher
#     LICENSE-WebView2.txt                                   a licenca do SDK (CREDITOS.md)
#  O .nupkg e baixado PELO NAVEGADOR (regra do projeto: nada de curl/Invoke-WebRequest):
#     https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.3912.50
#  e cai em Downloads. Este script pega o mais novo de la (ou o caminho em -Pacote).
#  Uso: powershell -ExecutionPolicy Bypass -File .\preparar-webview2-sdk.ps1 [-Pacote <caminho.nupkg>]
# =====================================================================================
param([string]$Pacote)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Pacote) {
    $cand = Get-ChildItem (Join-Path $env:USERPROFILE 'Downloads') -Filter 'microsoft.web.webview2*.nupkg' -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $cand) { throw "nenhum microsoft.web.webview2*.nupkg em Downloads: baixe pelo navegador (ver o cabecalho deste script)" }
    $Pacote = $cand.FullName
}
$dest = Join-Path $Root 'webview2'
$tmp  = Join-Path $env:TEMP ('webview2-sdk-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $dest, (Join-Path $dest 'include'), $tmp | Out-Null
$zip = Join-Path $tmp 'pacote.zip'
Copy-Item $Pacote $zip -Force
Expand-Archive -Path $zip -DestinationPath $tmp -Force
$inc = Join-Path $tmp 'build\native\include'
foreach ($h in 'WebView2.h', 'WebView2EnvironmentOptions.h') {
    $src = Join-Path $inc $h
    if (-not (Test-Path $src)) { throw "o pacote nao tem build\native\include\$h" }
    Copy-Item $src (Join-Path $dest "include\$h") -Force
}
$dll = Join-Path $tmp 'build\native\x64\WebView2Loader.dll'
if (-not (Test-Path $dll)) { throw "o pacote nao tem build\native\x64\WebView2Loader.dll" }
Copy-Item $dll (Join-Path $dest 'WebView2Loader.dll') -Force
$lic = Join-Path $tmp 'LICENSE.txt'
if (Test-Path $lic) { Copy-Item $lic (Join-Path $dest 'LICENSE-WebView2.txt') -Force } else { Write-Host '[!] o pacote nao trouxe LICENSE.txt' -ForegroundColor Yellow }
$spec = Get-ChildItem $tmp -Filter '*.nuspec' | Select-Object -First 1
if ($spec) { $v = ([xml](Get-Content $spec.FullName -Raw)).package.metadata.version; "versao do SDK: $v" | Out-File (Join-Path $dest 'VERSAO.txt') -Encoding utf8 }
$sig = Get-AuthenticodeSignature (Join-Path $dest 'WebView2Loader.dll')
Write-Host ("WebView2Loader.dll: assinatura {0} ({1})" -f $sig.Status, $sig.SignerCertificate.Subject)
Remove-Item $tmp -Recurse -Force
Write-Host "ok -> $dest"
