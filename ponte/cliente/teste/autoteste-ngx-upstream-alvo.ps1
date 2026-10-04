# autoteste-ngx-upstream-alvo.ps1 -- upstream (G1, entrega 2) com o alvo64-ngx.exe do G3 como
# "jogo" (DLSS SR real, 853x480 -> 1280x720, cena submetida ANTES do evaluate) e o host de
# referencia do Feeder como servidor do pipe .up (ele recebe a cor pequena, faz DLAA nela e
# devolve). A DLL entra no alvo em modo sonda + upstream; a prova de prontidao decide a troca.
#
#   .\autoteste-ngx-upstream-alvo.ps1 [-Segundos 25] [-Prova 1|0]
#
# Esperado com -Prova 1: "prova de prontidao ... IGUAL" 8/8, "troca da Color LIGADA", depois
# linhas "ngx: 300 avaliacoes | copia-in | espera | troca" com troca=ligada, alvo vivo.
# So o DISPLAY2. Encerra SO o alvo e o host que subiu (por pid).
param([int]$Segundos = 25, [int]$Prova = 1, [string]$Rotulo = "ngx-upstream-alvo")
$ErrorActionPreference = 'Stop'
$aqui  = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz  = (Resolve-Path "$aqui\..\..\..\..").Path
$dll   = (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path
$inj   = "$raiz\bancada2\injetar.ps1"
$alvo  = "$raiz\bancada3\alvo-ngx\alvo64-ngx.exe"
$h64   = "$raiz\bancada2\host-mock\host64"
$logs  = "$raiz\bancada2\logs"
function Linhas($f) { if (Test-Path $f) { Get-Content $f -ErrorAction SilentlyContinue } else { @() } }
$Rotulo = "$Rotulo-prova$Prova"
Write-Host "===== $Rotulo =====" -ForegroundColor Cyan

# 1. alvo NGX (o jogo)
$pa = Start-Process -FilePath $alvo -ArgumentList @('--quadros','20000','--w','1280','--h','720','--x','100','--y','100') -WorkingDirectory (Split-Path $alvo) -PassThru
Start-Sleep -Seconds 4
if ($pa.HasExited) { Write-Host "ALVO SAIU ($($pa.ExitCode))" -ForegroundColor Red; exit 3 }
Write-Host "1) alvo-ngx pid=$($pa.Id)"

# 2. host do Feeder servindo \\.\pipe\dlss5-feed.<pid do alvo> (sera o nosso .up)
Remove-Item "$h64\dlss5-feed-host.log" -ErrorAction SilentlyContinue
$ph = Start-Process -FilePath "$h64\dlss5-feed-host64.exe" -ArgumentList @("$($pa.Id)", '--behind') -WorkingDirectory $h64 -PassThru
Start-Sleep -Seconds 4
if ($ph.HasExited) { Write-Host "host saiu ($($ph.ExitCode))" -ForegroundColor Red; Stop-Process -Id $pa.Id -Force; exit 4 }
Write-Host "2) host (servidor .up) pid=$($ph.Id)"

# 3. DLL no alvo: sonda (sem fase 2) + upstream apontado para o host
$log = "$env:TEMP\dlss5-ponte.$($pa.Id).log"
Remove-Item "$env:TEMP\dlss5-ponte.$($pa.Id).parar" -ErrorAction SilentlyContinue
Set-Content -Path "$env:TEMP\dlss5-ponte.$($pa.Id).modo" -Value 'sonda' -NoNewline -Encoding Ascii
Set-Content -Path "$env:TEMP\dlss5-ponte.$($pa.Id).ngx" -Value "upstream=1`nprova=$Prova`npipe_up=\\.\pipe\dlss5-feed.$($pa.Id)`n" -Encoding Ascii
& powershell -ExecutionPolicy Bypass -File $inj $pa.Id $dll | Out-Null
Write-Host "3) DLL injetada; observando $Segundos s"
Start-Sleep -Seconds $Segundos

Write-Host "`n----- DLL no alvo -----" -ForegroundColor Yellow
Linhas $log | Where-Object { $_ -match '^ngx: (upstream|prova|300 aval|desligado|contrato|evaluate feature)' } | Select-Object -First 30
Write-Host "----- host: ultimas 8 -----" -ForegroundColor Yellow
Linhas "$h64\dlss5-feed-host.log" | Select-Object -Last 8
$vivo = Get-Process -Id $pa.Id -ErrorAction SilentlyContinue
Set-Content -Path "$env:TEMP\dlss5-ponte.$($pa.Id).parar" -Value '1' -NoNewline -Encoding Ascii
Start-Sleep -Seconds 4
Linhas $log | Select-Object -Last 3
Write-Host ("alvo no fim: {0}" -f $(if ($vivo) { 'VIVO' } else { 'MORREU' })) -ForegroundColor Cyan
$stamp = Get-Date -Format 'HHmmss'
Copy-Item $log "$logs\$Rotulo-dll-$stamp.log" -ErrorAction SilentlyContinue
Copy-Item "$h64\dlss5-feed-host.log" "$logs\$Rotulo-host-$stamp.log" -ErrorAction SilentlyContinue
$p = Get-Process -Id $ph.Id -ErrorAction SilentlyContinue; if ($p -and $p.ProcessName -eq 'dlss5-feed-host64') { Stop-Process -Id $ph.Id -Force }
Start-Sleep -Seconds 1
$p = Get-Process -Id $pa.Id -ErrorAction SilentlyContinue; if ($p -and $p.ProcessName -like 'alvo64*') { Stop-Process -Id $pa.Id -Force }
Remove-Item "$env:TEMP\dlss5-ponte.$($pa.Id).modo","$env:TEMP\dlss5-ponte.$($pa.Id).parar","$env:TEMP\dlss5-ponte.$($pa.Id).ngx" -ErrorAction SilentlyContinue
Write-Host "logs em $logs\$Rotulo-*-$stamp.log"
