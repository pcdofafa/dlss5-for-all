# autoteste-ngx-sonda.ps1 -- prova a SONDA NGX (G1, fase 3) sem jogo nenhum.
#
# O host de referencia do Feeder (dlss5-feed-host64.exe, host-mock do B3) cria e avalia uma
# feature DLSS SR de verdade em D3D12 (DLAA: interno = saida) assim que um cliente manda o
# Build. Entao:
#   1. sobe o alvo D3D12 (DISPLAY2);
#   2. sobe o host ANTES de injetar (ele e o servidor do pipe);
#   3. grava %TEMP%\dlss5-ponte.<pid do HOST>.modo = sonda e injeta a DLL NO HOST: os hooks
#      NGX passam a logar em %TEMP%\dlss5-ponte.<pid do host>.log;
#   4. injeta a DLL no ALVO (modo normal): a ponte conecta, manda o Build, o host cria a
#      feature -> a sonda dentro do host ve CreateFeature e os EvaluateFeature por quadro;
#   5. espera, imprime as linhas "ngx:" do host, grava o .parar para a sonda se descarregar;
#   6. encerra SO o alvo e o host que este script subiu (por pid).
#
# So o DISPLAY2. Nao usa derrubar.ps1 (pids.txt e compartilhado entre agentes).
param([int]$Segundos = 12, [string]$Fmt = "rgba8", [string]$Rotulo = "ngx-sonda")
$ErrorActionPreference = 'Stop'
$aqui  = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz  = (Resolve-Path "$aqui\..\..\..\..").Path
$dll   = (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path
$subir = "$raiz\bancada2\host-mock\subir-mock.ps1"
$inj   = "$raiz\bancada2\injetar.ps1"
$alvo  = "$raiz\bancada2\alvo64\alvo64-d3d12.exe"
$logs  = "$raiz\bancada2\logs"
function Linhas($f) { if (Test-Path $f) { Get-Content $f -ErrorAction SilentlyContinue } else { @() } }

Write-Host "===== $Rotulo =====" -ForegroundColor Cyan
# 1. alvo
$pa = Start-Process -FilePath $alvo -ArgumentList @('--fmt', $Fmt, '--x', '100', '--y', '100') -PassThru
Start-Sleep -Seconds 3
if ($pa.HasExited) { Write-Host "ALVO MORREU (code $($pa.ExitCode))" -ForegroundColor Red; exit 3 }
Write-Host "1) alvo pid=$($pa.Id)"

# 2. host
$antes = @(Get-Process dlss5-feed-host64 -ErrorAction SilentlyContinue | Select-Object -Expand Id)
& powershell -ExecutionPolicy Bypass -File $subir $alvo $pa.Id | Out-Host
Start-Sleep -Seconds 3
$depois = @(Get-Process dlss5-feed-host64 -ErrorAction SilentlyContinue | Select-Object -Expand Id)
$meuHost = @($depois | Where-Object { $antes -notcontains $_ })
if ($meuHost.Count -ne 1) { Write-Host "nao identifiquei UM host novo ($($meuHost -join ',')); abortando sem tocar em nada" -ForegroundColor Red; Stop-Process -Id $pa.Id -Force; exit 4 }
$ph = $meuHost[0]
Write-Host "2) host pid=$ph"

# 3. sonda no host
$logHost = "$env:TEMP\dlss5-ponte.$ph.log"
Remove-Item "$env:TEMP\dlss5-ponte.$ph.parar" -ErrorAction SilentlyContinue
Set-Content -Path "$env:TEMP\dlss5-ponte.$ph.modo" -Value 'sonda' -NoNewline -Encoding Ascii
& powershell -ExecutionPolicy Bypass -File $inj $ph $dll | Out-Host
Start-Sleep -Seconds 2
Write-Host "3) sonda injetada no host (log: $logHost)"

# 4. ponte no alvo
$logAlvo = "$env:TEMP\dlss5-ponte.$($pa.Id).log"
Remove-Item "$env:TEMP\dlss5-ponte.$($pa.Id).modo" -ErrorAction SilentlyContinue
& powershell -ExecutionPolicy Bypass -File $inj $pa.Id $dll | Out-Host
Write-Host "4) ponte injetada no alvo (log: $logAlvo); observando $Segundos s"
Start-Sleep -Seconds $Segundos

Write-Host "`n----- LOG DA SONDA (host) -----" -ForegroundColor Yellow
Linhas $logHost
Write-Host "----- LOG DA PONTE (alvo, ultimas 6) -----" -ForegroundColor Yellow
Linhas $logAlvo | Select-Object -Last 6

# 5. parar a sonda
Set-Content -Path "$env:TEMP\dlss5-ponte.$ph.parar" -Value '1' -NoNewline -Encoding Ascii
Start-Sleep -Seconds 3
Write-Host "----- fim do log da sonda -----" -ForegroundColor Yellow
Linhas $logHost | Select-Object -Last 4

# 6. encerrar so o meu alvo e o meu host
$stamp = Get-Date -Format 'HHmmss'
Copy-Item $logHost "$logs\$Rotulo-host-$stamp.log" -ErrorAction SilentlyContinue
Copy-Item $logAlvo "$logs\$Rotulo-alvo-$stamp.log" -ErrorAction SilentlyContinue
$p = Get-Process -Id $ph -ErrorAction SilentlyContinue; if ($p -and $p.ProcessName -eq 'dlss5-feed-host64') { Stop-Process -Id $ph -Force }
Start-Sleep -Seconds 2
$p = Get-Process -Id $pa.Id -ErrorAction SilentlyContinue; if ($p -and $p.ProcessName -like 'alvo64*') { Stop-Process -Id $pa.Id -Force }
Remove-Item "$env:TEMP\dlss5-ponte.$ph.modo", "$env:TEMP\dlss5-ponte.$ph.parar" -ErrorAction SilentlyContinue
Write-Host "logs guardados em $logs\$Rotulo-*-$stamp.log"
