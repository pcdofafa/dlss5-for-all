# fumaca-q1-rdr.ps1 -- Q1: a depth D24S8 do Red Dead Redemption (jogo real, DLSS 3.7, D3D12, HDR)
# convertida para R32F pela DLL e medida no host. Igual a fumaca-upstream-rdr.ps1 do G1, mas com os
# binarios de bancada2\bin-q1 e a prova curta (a copia do RDR as vezes sai sozinha perto dos 85 s):
# sobe o app, espera o upstream, le `ngx: depth convertida` na DLL e `upstream: depth quadro N` no app.
# O RDR tem de estar aberto (subir-rdr.ps1 -AplicarConfig -SuperResolution 2 -NaoCapturarTela; F8: desde o
# F6, sem -AplicarConfig o subir so avisa e o graphicsOptions.xml fica como estava). Fecha SO o app.
param([int]$Segundos = 12, [string]$Rotulo = 'fumaca-q1-rdr')
$ErrorActionPreference = 'Stop'
$aqui = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz = (Resolve-Path "$aqui\..\..\..\..").Path
$bin  = "$raiz\bancada2\bin-q1"; $logs = "$raiz\bancada2\logs"
function Diz($m) { Write-Host ("[q1-rdr {0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) -ForegroundColor Cyan }
function Linhas($f) { if (Test-Path $f) { Get-Content $f -ErrorAction SilentlyContinue } else { @() } }
function EsperaLog($arq, $regex, $seg) { $fim = (Get-Date).AddSeconds($seg); while ((Get-Date) -lt $fim) { if (((Linhas $arq) -join "`n") -match $regex) { return $true }; Start-Sleep -Milliseconds 500 }; return $false }
$jogo = Get-Process RDR -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $jogo) { Diz "RDR nao esta aberto"; exit 2 }
if (Get-Process DLSS5ForAll -ErrorAction SilentlyContinue) { Diz "ja ha um DLSS5ForAll; abortando"; exit 5 }
Copy-Item "$raiz\src\DLSS5ForAll-poc.exe" "$bin\DLSS5ForAll.exe" -Force
Copy-Item (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path "$bin\dlss5-ponte64.dll" -Force
Remove-Item "$bin\dlss5forall.log*" -ErrorAction SilentlyContinue
Remove-Item "$env:TEMP\dlss5-ponte.$($jogo.Id).*" -ErrorAction SilentlyContinue
$appLog = "$bin\dlss5forall.log"; $dllLog = "$env:TEMP\dlss5-ponte.$($jogo.Id).log"
$t0 = Get-Date; $stamp = Get-Date -Format 'HHmmss'
Start-Process -FilePath "$bin\DLSS5ForAll.exe" -ArgumentList @('--source','window','--window','"Red Dead Redemption"') -WorkingDirectory $bin | Out-Null
Diz "app subindo contra o RDR pid $($jogo.Id)"
if (-not (EsperaLog $appLog 'integrar: (ponte ativa|falhou)' 45)) { Diz "ponte nao ativou em 45 s" }
$ok = EsperaLog $dllLog 'ngx: upstream (ligado|: o app nao serve|desligado)|ngx: desligado' 40
Diz "upstream na DLL: $ok"
$c = EsperaLog $dllLog 'depth convertida fmt=\d+ -> R32F' 20
Diz "DLL converteu a depth: $c"
$m = EsperaLog $appLog 'upstream: depth quadro \d+: min=' 20
Diz "app mediu a depth: $m"
Start-Sleep -Seconds $Segundos
Diz ("estado publicado pela DLL: " + ((Linhas "$env:TEMP\dlss5-ponte.$($jogo.Id).ngx-estado") -join ' | '))
Write-Host "----- app -----" -ForegroundColor Yellow
Linhas $appLog | Where-Object { $_ -match 'upstream: (sessao|ligad|desligado|backbuffer|depth|texturas|build)|integrar:|dlss-jogo|reabr' } | Select-Object -Last 20
Write-Host "----- DLL -----" -ForegroundColor Yellow
Linhas $dllLog | Where-Object { $_ -match '^ngx: (upstream ligado|prova de prontidao: |depth convertida|300 aval|desligado|hook|evaluate feature)|^ponte: (build|desligada|descarregada)' } | Select-Object -Last 16
Copy-Item $appLog "$logs\$Rotulo-app-$stamp.log" -ErrorAction SilentlyContinue
Copy-Item $dllLog "$logs\$Rotulo-dll-$stamp.log" -ErrorAction SilentlyContinue
foreach ($p in @(Get-Process DLSS5ForAll -ErrorAction SilentlyContinue | Where-Object { $_.StartTime -gt $t0 })) { Diz "fechando o app pid $($p.Id)"; Stop-Process -Id $p.Id -Force }
Start-Sleep -Seconds 4
Linhas $dllLog | Select-Object -Last 3
$vivo = Get-Process -Id $jogo.Id -ErrorAction SilentlyContinue
Diz ("RDR no fim: {0}; logs em {1}\{2}-*-{3}.*" -f $(if ($vivo) { 'VIVO' } else { 'MORREU' }), $logs, $Rotulo, $stamp)
