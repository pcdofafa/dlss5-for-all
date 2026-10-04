# sonda-processo.ps1 -- injeta a dlss5-ponte64.dll em modo SONDA num processo ja aberto (um
# jogo com DLSS) e colhe as linhas "ngx:" por N segundos. Nao precisa do app nem de host.
#
#   .\sonda-processo.ps1 -ProcessoPid 1234 [-Segundos 60] [-Rotulo witcher3]
#
# O que faz: grava %TEMP%\dlss5-ponte.<pid>.modo = sonda (a DLL le isso e nao procura o
# pipe), injeta pelo bancada2\injetar.ps1, espera, imprime o log, grava o .parar (a DLL
# restaura os hooks e se descarrega sozinha) e copia o log para bancada2\logs\.
# NAO abre nem fecha o jogo: quem abriu e que fecha.
param(
    [Parameter(Mandatory = $true)][int]$ProcessoPid,
    [int]$Segundos = 60,
    [string]$Rotulo = 'sonda'
)
$ErrorActionPreference = 'Stop'
$aqui = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz = (Resolve-Path "$aqui\..\..\..\..").Path
$dll  = (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path
$inj  = "$raiz\bancada2\injetar.ps1"
$logs = "$raiz\bancada2\logs"
$log  = "$env:TEMP\dlss5-ponte.$ProcessoPid.log"
function Linhas($f) { if (Test-Path $f) { Get-Content $f -ErrorAction SilentlyContinue } else { @() } }

$p = Get-Process -Id $ProcessoPid -ErrorAction SilentlyContinue
if (-not $p) { Write-Host "pid $ProcessoPid nao existe" -ForegroundColor Red; exit 1 }
Write-Host "===== ${Rotulo}: sonda em $($p.ProcessName) (pid $ProcessoPid) por $Segundos s =====" -ForegroundColor Cyan
Remove-Item "$env:TEMP\dlss5-ponte.$ProcessoPid.parar" -ErrorAction SilentlyContinue
Set-Content -Path "$env:TEMP\dlss5-ponte.$ProcessoPid.modo" -Value 'sonda' -NoNewline -Encoding Ascii
& powershell -ExecutionPolicy Bypass -File $inj $ProcessoPid $dll | Out-Host
if ($LASTEXITCODE -ne 0) { Write-Host "injecao falhou (codigo $LASTEXITCODE)" -ForegroundColor Red; exit 2 }

$fim = (Get-Date).AddSeconds($Segundos)
$mostradas = 0
while ((Get-Date) -lt $fim) {
    Start-Sleep -Seconds 2
    $l = @(Linhas $log)
    if ($l.Count -gt $mostradas) { $l[$mostradas..($l.Count - 1)] | ForEach-Object { Write-Host "  $_" }; $mostradas = $l.Count }
    $pv = Get-Process -Id $ProcessoPid -ErrorAction SilentlyContinue
    if (-not $pv) { Write-Host "o processo saiu" -ForegroundColor Yellow; break }
}
Set-Content -Path "$env:TEMP\dlss5-ponte.$ProcessoPid.parar" -Value '1' -NoNewline -Encoding Ascii
Start-Sleep -Seconds 4
$l = @(Linhas $log)
if ($l.Count -gt $mostradas) { $l[$mostradas..($l.Count - 1)] | ForEach-Object { Write-Host "  $_" } }
$stamp = Get-Date -Format 'HHmmss'
if (-not (Test-Path $logs)) { New-Item -ItemType Directory -Path $logs -Force | Out-Null }
Copy-Item $log "$logs\$Rotulo-$stamp.log" -ErrorAction SilentlyContinue
Remove-Item "$env:TEMP\dlss5-ponte.$ProcessoPid.modo", "$env:TEMP\dlss5-ponte.$ProcessoPid.parar" -ErrorAction SilentlyContinue
$pv = Get-Process -Id $ProcessoPid -ErrorAction SilentlyContinue
Write-Host ("processo no fim: {0}; log em {1}\{2}-{3}.log" -f ($(if ($pv) { 'VIVO' } else { 'saiu' }), $logs, $Rotulo, $stamp)) -ForegroundColor Cyan
