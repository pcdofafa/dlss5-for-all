# fumaca-upstream-rdr.ps1 -- upstream no jogo real (Red Dead Redemption JA ABERTO numa cena com
# DLSS): sobe o app de bin-g1 capturando a janela do jogo; a ponte da fase 2 liga no Automatico,
# o app serve o .up, a DLL ve o DLSS-SR (1280x720 -> 1920x1080, cor 16F) e faz a sessao upstream.
# Observa N segundos, guarda os logs das duas pontas e uma captura, fecha SO o app. O jogo fica.
param([int]$Segundos = 45, [string]$Rotulo = 'rdr-up')
$ErrorActionPreference = 'Stop'
$aqui = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz = (Resolve-Path "$aqui\..\..\..\..").Path
$bin  = "$raiz\bancada2\bin-g1"; $logs = "$raiz\bancada2\logs"
function Diz($m) { Write-Host ("[rdr-up {0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) -ForegroundColor Cyan }
function Linhas($f) { if (Test-Path $f) { Get-Content $f -ErrorAction SilentlyContinue } else { @() } }
function EsperaLog($arq, $regex, $seg) { $fim = (Get-Date).AddSeconds($seg); while ((Get-Date) -lt $fim) { if (((Linhas $arq) -join "`n") -match $regex) { return $true }; Start-Sleep -Milliseconds 500 }; return $false }
$jogo = Get-Process RDR -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $jogo) { Diz "RDR nao esta aberto (subir-rdr.ps1 -AplicarConfig + entrar-no-jogo.ps1 antes)"; exit 2 }
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
Start-Sleep -Seconds $Segundos
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap 1920, 1080; $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen(0, 0, 0, 0, $bmp.Size); $g.Dispose()
$bmp.Save("$logs\$Rotulo-tela-$stamp.png"); $bmp.Dispose()
Write-Host "----- app -----" -ForegroundColor Yellow
Linhas $appLog | Where-Object { $_ -match 'upstream|integrar:|ponte: (sessao|output|build|backbuffer)|reabr|16f' } | Select-Object -Last 22
Write-Host "----- DLL -----" -ForegroundColor Yellow
Linhas $dllLog | Where-Object { $_ -match '^ngx: (upstream|prova|300 aval|desligado)|^ponte: (hook|build|desligada|descarregada|conectado)' } | Select-Object -Last 22
Copy-Item $appLog "$logs\$Rotulo-app-$stamp.log" -ErrorAction SilentlyContinue
Copy-Item $dllLog "$logs\$Rotulo-dll-$stamp.log" -ErrorAction SilentlyContinue
foreach ($p in @(Get-Process DLSS5ForAll -ErrorAction SilentlyContinue | Where-Object { $_.StartTime -gt $t0 })) { Diz "fechando o app pid $($p.Id)"; Stop-Process -Id $p.Id -Force }
Start-Sleep -Seconds 4
Linhas $dllLog | Select-Object -Last 3
$vivo = Get-Process -Id $jogo.Id -ErrorAction SilentlyContinue
Diz ("RDR no fim: {0}; logs em {1}\{2}-*-{3}.*" -f $(if ($vivo) { 'VIVO' } else { 'MORREU' }), $logs, $Rotulo, $stamp)
