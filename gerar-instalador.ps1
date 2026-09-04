# =====================================================================================
#  gerar-instalador.ps1  -  do fonte ao DLSS5ForAll-<versao>-setup.exe, num comando so.
#
#  Uso:  powershell -ExecutionPolicy Bypass -File .\gerar-instalador.ps1
#        ...  -PermitirBuildVelho     (SO PARA TESTE: ver empacotar.ps1, item 0)
#        ...  -Versao 2026.09.10      (padrao: a data de hoje, igual ao empacotar.ps1)
#
#  O que ele faz:
#    1. chama o empacotar.ps1 -SomenteStaging  -> a MESMA lista de arquivos do zip, com as
#       mesmas validacoes (build novo, ReShade.ini sem caminho absoluto, NeuralUplift=1)
#    2. converte o CREDITOS.md para texto puro (o Inno mostra a pagina de creditos antes de
#       instalar, e nao renderiza markdown)
#    3. compila o instalador.iss com o ISCC do Inno Setup
#
#  Por que Inno Setup e nao iexpress/makecab: o requisito e instalar SEM administrador em
#  %LOCALAPPDATA% com pasta escolhivel, atalhos com icone, entrada em "Aplicativos
#  instalados" e desinstalador que preserve o que a pessoa trouxe. iexpress e um
#  auto-extraivel: nao tem nada disso (nem desinstalador, nem entrada, nem pagina de
#  licenca). O Inno fica na maquina de quem COMPILA; o setup gerado nao exige nada na
#  maquina de quem instala.
# =====================================================================================
param([string]$Versao = (Get-Date -Format 'yyyy.MM.dd'),
      [switch]$PermitirBuildVelho)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

function Passo($t) { Write-Host ""; Write-Host "== $t" -ForegroundColor Cyan }

# ---------------------------------------------------------------- 0. o compilador
Passo 'Inno Setup (ISCC.exe)'
$iscc = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",       # instalacao por usuario (winget)
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "ISCC.exe nao encontrado. Instala com:  winget install --id JRSoftware.InnoSetup" }
Write-Host "   $iscc"

# O carimbo -TESTE tem de valer aqui TAMBEM, e nao so dentro do empacotar.ps1: e o nome
# deste arquivo que alguem vai ver na hora de publicar.
if ($PermitirBuildVelho -and $Versao -notmatch 'TESTE') { $Versao = "$Versao-TESTE" }

# ---------------------------------------------------------------- 1. staging
Passo "empacotar.ps1 -SomenteStaging (a lista de arquivos mora la, nao aqui)"
$trabalho = Join-Path $env:TEMP 'DLSS5ForAll-instalador'
$staging  = Join-Path $trabalho 'staging'
Remove-Item $trabalho -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $trabalho | Out-Null
$parms = @{ Versao = $Versao; SomenteStaging = $true; Staging = $staging }
if ($PermitirBuildVelho) { $parms.PermitirBuildVelho = $true }
& (Join-Path $Root 'empacotar.ps1') @parms
if (-not (Test-Path (Join-Path $staging 'DLSS5ForAll.exe'))) { throw "a staging nao tem o DLSS5ForAll.exe - o empacotar.ps1 falhou" }

# ---------------------------------------------------------------- 2. creditos em texto puro
# A pagina InfoBefore do Inno mostra o arquivo como esta: os **negritos** e os `backticks`
# do markdown apareceriam crus. Convertido daqui, e nao mantido a mao, para nao existir uma
# segunda versao dos creditos para alguem esquecer de atualizar.
Passo 'CREDITOS.md -> CREDITOS.txt'
$cred = Get-Content (Join-Path $Root 'CREDITOS.md') -Encoding UTF8 -Raw
$cred = $cred -replace '\*\*', '' -replace '`', '' -replace '(?m)^#\s*', ''
$credTxt = Join-Path $trabalho 'CREDITOS.txt'
# UTF-8 COM BOM: sem o BOM o Inno le o arquivo como ANSI e os acentos viram lixo na tela.
[IO.File]::WriteAllText($credTxt, $cred, (New-Object System.Text.UTF8Encoding $true))

# ---------------------------------------------------------------- 3. compilar
Passo 'ISCC instalador.iss'
# VersionInfoVersion do Windows so aceita numeros: "2026.09.04-TESTE" vira "2026.9.4.0".
$n = @(($Versao -replace '[^0-9.]', '') -split '\.' | Where-Object { $_ -ne '' } | ForEach-Object { [int]$_ })
while ($n.Count -lt 4) { $n += 0 }
$versaoNum = ($n[0..3] -join '.')
$nome = "DLSS5ForAll-$Versao-setup"
$argsIscc = @(
    "/DStaging=$staging",
    "/DVersao=$Versao",
    "/DVersaoNum=$versaoNum",
    "/DSaidaDir=$Root",
    "/DSaidaNome=$nome",
    "/DCreditos=$credTxt",
    (Join-Path $Root 'instalador.iss')
)
& $iscc @argsIscc
if ($LASTEXITCODE -ne 0) { throw "ISCC falhou (codigo $LASTEXITCODE)" }

$exe = Join-Path $Root "$nome.exe"
$mb  = [math]::Round((Get-Item $exe).Length / 1MB, 2)
Write-Host ""
Write-Host "ok -> $exe ($mb MB)" -ForegroundColor Green
if ($PermitirBuildVelho) {
    Write-Host "ATENCAO: gerado com -PermitirBuildVelho. E um instalador DE TESTE (o exe pode ser mais velho que o codigo). NAO PUBLICAR." -ForegroundColor Yellow
}
