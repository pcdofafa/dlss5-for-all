# autoteste-ngx-upstream.ps1 -- prova o UPSTREAM (G1, entrega 2) sem jogo e sem o app do G2.
#
#   host 1 (dlss5-feed-host64 do Feeder, servindo o alvo D3D12)  = o "jogo": avalia DLSS SR real
#   nossa DLL injetada no host 1 (modo sonda + upstream)          = copia Color/MV/Depth, troca a Color
#   host 2 (outro dlss5-feed-host64, de uma copia host64b\)      = o "app": recebe a cor pequena pelo
#                                                                   pipe .up (sobrescrito no controle),
#                                                                   processa (DLAA+neural) e devolve
#
#   .\autoteste-ngx-upstream.ps1 [-Prova 1|0] [-Segundos 15]
#     -Prova 1: a prova de prontidao decide se a troca liga (no host 1 a cena e o evaluate estao
#               na mesma lista: espera-se "sincrono impossivel" e upstream desligado com motivo);
#     -Prova 0: troca forcada desde o 1.o quadro: prova transporte, fences, vigia e restauracao.
#
# So o DISPLAY2. Encerra SO o que este script subiu (por pid).
param([int]$Prova = 1, [int]$Segundos = 15, [string]$Rotulo = "ngx-upstream")
$ErrorActionPreference = 'Stop'
$aqui  = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz  = (Resolve-Path "$aqui\..\..\..\..").Path
$dll   = (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path
$subir = "$raiz\bancada2\host-mock\subir-mock.ps1"
$inj   = "$raiz\bancada2\injetar.ps1"
$alvo  = "$raiz\bancada2\alvo64\alvo64-d3d12.exe"
$logs  = "$raiz\bancada2\logs"
$h64   = "$raiz\bancada2\host-mock\host64"
$h64b  = "$raiz\bancada2\host-mock\host64b"
function Linhas($f) { if (Test-Path $f) { Get-Content $f -ErrorAction SilentlyContinue } else { @() } }
$Rotulo = "$Rotulo-prova$Prova"
Write-Host "===== $Rotulo =====" -ForegroundColor Cyan

# host64b: copia do host64 (o log do host nasce ao lado do exe; dois hosts na mesma pasta misturariam os logs)
if (-not (Test-Path "$h64b\dlss5-feed-host64.exe")) { Write-Host "montando $h64b (copia de host64)..."; Copy-Item $h64 $h64b -Recurse -Force }

# 1. alvo
$pa = Start-Process -FilePath $alvo -ArgumentList @('--fmt', 'rgba8', '--x', '100', '--y', '100') -PassThru
Start-Sleep -Seconds 3
if ($pa.HasExited) { Write-Host "ALVO MORREU" -ForegroundColor Red; exit 3 }
Write-Host "1) alvo pid=$($pa.Id)"

# 2. host 1 (o "jogo")
$antes = @(Get-Process dlss5-feed-host64 -ErrorAction SilentlyContinue | Select-Object -Expand Id)
& powershell -ExecutionPolicy Bypass -File $subir $alvo $pa.Id | Out-Null
Start-Sleep -Seconds 3
$depois = @(Get-Process dlss5-feed-host64 -ErrorAction SilentlyContinue | Select-Object -Expand Id)
$meu = @($depois | Where-Object { $antes -notcontains $_ })
if ($meu.Count -ne 1) { Write-Host "nao identifiquei UM host novo; abortando" -ForegroundColor Red; Stop-Process -Id $pa.Id -Force; exit 4 }
$ph1 = $meu[0]
Write-Host "2) host 1 (jogo) pid=$ph1"

# 3. controle + modo para a DLL no host 1
$log1 = "$env:TEMP\dlss5-ponte.$ph1.log"
Remove-Item "$env:TEMP\dlss5-ponte.$ph1.parar" -ErrorAction SilentlyContinue
Set-Content -Path "$env:TEMP\dlss5-ponte.$ph1.modo" -Value 'sonda' -NoNewline -Encoding Ascii
Set-Content -Path "$env:TEMP\dlss5-ponte.$ph1.ngx" -Value "upstream=1`nprova=$Prova`npipe_up=\\.\pipe\dlss5-feed.$ph1`n" -Encoding Ascii

# 4. host 2 (o "app"): serve \\.\pipe\dlss5-feed.<ph1>
Remove-Item "$h64b\dlss5-feed-host.log" -ErrorAction SilentlyContinue
$ph2p = Start-Process -FilePath "$h64b\dlss5-feed-host64.exe" -ArgumentList @("$ph1", '--behind') -WorkingDirectory $h64b -PassThru
Start-Sleep -Seconds 4
if ($ph2p.HasExited) { Write-Host "host 2 saiu (code $($ph2p.ExitCode))" -ForegroundColor Red; Get-Content "$h64b\dlss5-feed-host.log" | Select-Object -Last 10; Stop-Process -Id $ph1 -Force; Stop-Process -Id $pa.Id -Force; exit 5 }
$ph2 = $ph2p.Id
Write-Host "4) host 2 (app) pid=$ph2 servindo \\.\pipe\dlss5-feed.$ph1"

# 5. injeta no host 1 (sonda+upstream) e no alvo (ponte normal -> host 1 cria a feature)
& powershell -ExecutionPolicy Bypass -File $inj $ph1 $dll | Out-Null
Start-Sleep -Seconds 2
Remove-Item "$env:TEMP\dlss5-ponte.$($pa.Id).modo" -ErrorAction SilentlyContinue
& powershell -ExecutionPolicy Bypass -File $inj $pa.Id $dll | Out-Null
Write-Host "5) DLL no host 1 e no alvo; observando $Segundos s"
Start-Sleep -Seconds $Segundos

Write-Host "`n----- LOG DA DLL NO HOST 1 (linhas ngx:/ponte: relevantes) -----" -ForegroundColor Yellow
Linhas $log1 | Where-Object { $_ -match '^ngx: (hook|upstream|prova|300|desligado|controle|contrato|evaluate|create)|^ponte: (modo|desligada|descarregada)' }
Write-Host "----- HOST 2 (app): ultimas 12 linhas -----" -ForegroundColor Yellow
Linhas "$h64b\dlss5-feed-host.log" | Select-Object -Last 12
Write-Host "----- HOST 1 (jogo): erros? -----" -ForegroundColor Yellow
Linhas "$h64\dlss5-feed-host.log" | Where-Object { $_ -match 'fail|error|removed|gone|hung' } | Select-Object -Last 6

# 6. parar a DLL do host 1 (restaura e descarrega), depois encerrar so o meu
Set-Content -Path "$env:TEMP\dlss5-ponte.$ph1.parar" -Value '1' -NoNewline -Encoding Ascii
Start-Sleep -Seconds 4
Write-Host "----- fim -----" -ForegroundColor Yellow
Linhas $log1 | Select-Object -Last 4
$stamp = Get-Date -Format 'HHmmss'
Copy-Item $log1 "$logs\$Rotulo-host1-$stamp.log" -ErrorAction SilentlyContinue
Copy-Item "$h64b\dlss5-feed-host.log" "$logs\$Rotulo-host2-$stamp.log" -ErrorAction SilentlyContinue
Copy-Item "$env:TEMP\dlss5-ponte.$($pa.Id).log" "$logs\$Rotulo-alvo-$stamp.log" -ErrorAction SilentlyContinue
$vivo1 = Get-Process -Id $ph1 -ErrorAction SilentlyContinue; $vivoA = Get-Process -Id $pa.Id -ErrorAction SilentlyContinue
Write-Host ("host 1 (jogo) no fim: {0}; alvo: {1}" -f $(if ($vivo1) { 'VIVO' } else { 'MORREU' }), $(if ($vivoA) { 'VIVO' } else { 'MORREU' })) -ForegroundColor Cyan
foreach ($id in @($ph2, $ph1)) { $p = Get-Process -Id $id -ErrorAction SilentlyContinue; if ($p -and $p.ProcessName -eq 'dlss5-feed-host64') { Stop-Process -Id $id -Force } }
Start-Sleep -Seconds 2
$p = Get-Process -Id $pa.Id -ErrorAction SilentlyContinue; if ($p -and $p.ProcessName -like 'alvo64*') { Stop-Process -Id $pa.Id -Force }
Remove-Item "$env:TEMP\dlss5-ponte.$ph1.modo", "$env:TEMP\dlss5-ponte.$ph1.parar", "$env:TEMP\dlss5-ponte.$ph1.ngx" -ErrorAction SilentlyContinue
Write-Host "logs em $logs\$Rotulo-*-$stamp.log"
