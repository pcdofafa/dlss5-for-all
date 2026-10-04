# autoteste-ngx-controle.ps1 -- prova a ENTREGA 3 (override de preset/qualidade) sem jogo.
# O host de referencia cria a feature DLSS SR (DLAA, preset K). Gravamos dlss_preset=J ANTES de
# injetar; a sonda deve mostrar, na linha "create", o preset J (bruto 10) que injetamos, e o
# host segue vivo. Depois, ao vivo, mudamos para preset L e conferimos "feature recriada".
param([int]$Segundos = 10)
$ErrorActionPreference = 'Stop'
$aqui  = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz  = (Resolve-Path "$aqui\..\..\..\..").Path
$dll   = (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path
$subir = "$raiz\bancada2\host-mock\subir-mock.ps1"
$inj   = "$raiz\bancada2\injetar.ps1"
$alvo  = "$raiz\bancada2\alvo64\alvo64-d3d12.exe"
$logs  = "$raiz\bancada2\logs"
function Linhas($f) { if (Test-Path $f) { Get-Content $f -ErrorAction SilentlyContinue } else { @() } }
Write-Host "===== ngx-controle =====" -ForegroundColor Cyan
$pa = Start-Process -FilePath $alvo -ArgumentList @('--fmt','rgba8','--x','100','--y','100') -PassThru
Start-Sleep -Seconds 3
if ($pa.HasExited) { Write-Host "ALVO MORREU" -ForegroundColor Red; exit 3 }
$antes = @(Get-Process dlss5-feed-host64 -ErrorAction SilentlyContinue | Select-Object -Expand Id)
& powershell -ExecutionPolicy Bypass -File $subir $alvo $pa.Id | Out-Null
Start-Sleep -Seconds 3
$ph = @(Get-Process dlss5-feed-host64 -ErrorAction SilentlyContinue | Select-Object -Expand Id | Where-Object { $antes -notcontains $_ })
if ($ph.Count -ne 1) { Write-Host "nao achei UM host novo; abortando" -ForegroundColor Red; Stop-Process -Id $pa.Id -Force; exit 4 }
$ph = $ph[0]
$log = "$env:TEMP\dlss5-ponte.$ph.log"
Remove-Item "$env:TEMP\dlss5-ponte.$ph.parar" -ErrorAction SilentlyContinue
Set-Content -Path "$env:TEMP\dlss5-ponte.$ph.modo" -Value 'sonda' -NoNewline -Encoding Ascii
# override ANTES de injetar: preset J (10), qualidade DLAA
Set-Content -Path "$env:TEMP\dlss5-ponte.$ph.ngx" -Value "dlss_preset=J`ndlss_qualidade=dlaa`n" -Encoding Ascii
& powershell -ExecutionPolicy Bypass -File $inj $ph $dll | Out-Null
Start-Sleep -Seconds 2
# agora a ponte no ALVO: ele conecta ao host, o host monta o build e CRIA a feature -> a sonda
# intercepta o CreateFeature com o override ja ativo
Remove-Item "$env:TEMP\dlss5-ponte.$($pa.Id).modo" -ErrorAction SilentlyContinue
& powershell -ExecutionPolicy Bypass -File $inj $pa.Id $dll | Out-Null
Start-Sleep -Seconds $Segundos
Write-Host "`n----- create/controle (preset injetado) -----" -ForegroundColor Yellow
Linhas $log | Where-Object { $_ -match '^ngx: (create|controle)' } | Select-Object -First 6
# mudanca AO VIVO: preset L
Write-Host "----- mudando ao vivo para preset L -----" -ForegroundColor Yellow
Set-Content -Path "$env:TEMP\dlss5-ponte.$ph.ngx" -Value "dlss_preset=L`ndlss_qualidade=dlaa`n" -Encoding Ascii
Start-Sleep -Seconds 6
Linhas $log | Where-Object { $_ -match '^ngx: controle' } | Select-Object -Last 3
$vivo = Get-Process -Id $ph -ErrorAction SilentlyContinue
Set-Content -Path "$env:TEMP\dlss5-ponte.$ph.parar" -Value '1' -NoNewline -Encoding Ascii
Start-Sleep -Seconds 3
Write-Host ("host no fim: {0}" -f $(if ($vivo) { 'VIVO' } else { 'MORREU' })) -ForegroundColor Cyan
$stamp = Get-Date -Format 'HHmmss'
Copy-Item $log "$logs\ngx-controle-$stamp.log" -ErrorAction SilentlyContinue
$p = Get-Process -Id $ph -ErrorAction SilentlyContinue; if ($p -and $p.ProcessName -eq 'dlss5-feed-host64') { Stop-Process -Id $ph -Force }
Start-Sleep -Seconds 1
$p = Get-Process -Id $pa.Id -ErrorAction SilentlyContinue; if ($p -and $p.ProcessName -like 'alvo64*') { Stop-Process -Id $pa.Id -Force }
Remove-Item "$env:TEMP\dlss5-ponte.$ph.modo","$env:TEMP\dlss5-ponte.$ph.parar","$env:TEMP\dlss5-ponte.$ph.ngx" -ErrorAction SilentlyContinue
Write-Host "log em $logs\ngx-controle-$stamp.log"
