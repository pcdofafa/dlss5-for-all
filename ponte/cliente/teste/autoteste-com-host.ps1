# autoteste-com-host.ps1 -- o ciclo COMPLETO da DLL contra o host de referencia
# do Feeder (host-mock do B3). Prova hello/build/frame e a autodescarga limpa.
#
#   .\autoteste-com-host.ps1 -Alvo <exe> [-Args rgba8|10bit|16f] [-Rotulo nome]
#
# Passos: sobe o alvo -> sobe o host ANTES de injetar -> injeta -> observa o log
# da DLL (conectado, build enviado, quadros) -> encerra o host (dispara "pipe
# fechado") -> confere "desligada" + "descarregada" e o alvo vivo.
#
# So o DISPLAY2. Encerra APENAS o alvo e o host que ESTE script subiu (achados
# por diff de PID; nao usa derrubar.ps1 para nao pegar processos de outro agente).
param(
    [Parameter(Mandatory=$true)][string]$Alvo,
    [string]$Fmt = "",                 # rgba8|10bit|16f -> vira "--fmt <val>"
    [string]$Modo = "",                # pipelined|sincrono -> grava %TEMP%\dlss5-ponte.<pid>.modo (como o app faz)
    [string]$Rotulo = "teste"
)
$AlvoArgs = if ($Fmt) { @('--fmt', $Fmt) } else { @() }
$ErrorActionPreference = 'Stop'
$aqui   = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz   = (Resolve-Path "$aqui\..\..\..\..").Path
$dll    = (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path
$subir  = "$raiz\bancada2\host-mock\subir-mock.ps1"
$Alvo   = (Resolve-Path $Alvo).Path

function Linhas($f) { if (Test-Path $f) { Get-Content $f } else { @() } }

Write-Host "===== $Rotulo =====" -ForegroundColor Cyan
Get-ChildItem "$env:TEMP\dlss5-ponte.*.log" -ErrorAction SilentlyContinue | Remove-Item -ErrorAction SilentlyContinue

# 1. alvo
Write-Host "1) subindo o alvo: $Alvo $($AlvoArgs -join ' ')"
$pa = if ($AlvoArgs.Count) { Start-Process -FilePath $Alvo -ArgumentList $AlvoArgs -PassThru } else { Start-Process -FilePath $Alvo -PassThru }
Start-Sleep -Seconds 3
if ($pa.HasExited) { Write-Host "ALVO MORREU ao subir (code $($pa.ExitCode))" -ForegroundColor Red; exit 3 }
Write-Host "   alvo pid=$($pa.Id) vivo"

# 2. host ANTES de injetar (diff de pid para achar so o meu host)
$antes = @(Get-Process dlss5-feed-host64 -ErrorAction SilentlyContinue | Select-Object -Expand Id)
Write-Host "2) subindo o host de referencia..."
& powershell -ExecutionPolicy Bypass -File $subir $Alvo $pa.Id | Out-Host
Start-Sleep -Seconds 2
$depois = @(Get-Process dlss5-feed-host64 -ErrorAction SilentlyContinue | Select-Object -Expand Id)
$meuHost = @($depois | Where-Object { $antes -notcontains $_ })
if ($meuHost.Count -eq 0) { Write-Host "AVISO: nao identifiquei um host novo; talvez ja houvesse um. Nao vou encerrar host nenhum." -ForegroundColor Yellow }
else { Write-Host "   host pid=$($meuHost -join ',')" }

# 2b. o modo, do jeito que o app grava ANTES de injetar
$arqModo = "$env:TEMP\dlss5-ponte.$($pa.Id).modo"
if ($Modo) { Set-Content -Path $arqModo -Value $Modo -NoNewline -Encoding Ascii; Write-Host "   modo gravado em $arqModo = $Modo" }

# 3. injetar
$log = "$env:TEMP\dlss5-ponte.$($pa.Id).log"
Write-Host "3) injetando a DLL... (log: $log)"
& powershell -ExecutionPolicy Bypass -File "$aqui\Inject.ps1" $pa.Id $dll | Out-Host

# 4. observar
Write-Host "4) observando o log da DLL por ate 30 s..."
$fim = (Get-Date).AddSeconds(30)
$viuBuild = $false; $viuQuadros = $false; $viuConect = $false
while ((Get-Date) -lt $fim) {
    $t = (Linhas $log) -join "`n"
    if ($t -match 'conectado ao host') { $viuConect = $true }
    if ($t -match 'build enviado')     { $viuBuild = $true }
    if ($t -match '300 quadros')       { $viuQuadros = $true }
    if ($t -match 'desligada')         { break }   # erro precoce
    if ($viuQuadros) { break }
    Start-Sleep -Milliseconds 500
}

Write-Host "`n----- LOG DA DLL (parcial) -----"
Linhas $log | Select-Object -Last 25
Write-Host "--------------------------------`n"

# 5. encerrar o host (dispara pipe fechado). So o meu.
Write-Host "5) encerrando o host que subi..."
foreach ($hp in $meuHost) {
    $p = Get-Process -Id $hp -ErrorAction SilentlyContinue
    if ($p -and $p.ProcessName -eq 'dlss5-feed-host64') { Stop-Process -Id $hp -Force -ErrorAction SilentlyContinue }
}

# 6. conferir desligada + descarregada
$fim2 = (Get-Date).AddSeconds(12)
while ((Get-Date) -lt $fim2) {
    $t = (Linhas $log) -join "`n"
    if ($t -match 'descarregada') { break }
    Start-Sleep -Milliseconds 400
}

Write-Host "`n===== LOG DA DLL (completo) ====="
Linhas $log
Write-Host "=================================`n"

$vivo = -not $pa.HasExited
Write-Host ("alvo vivo no fim: {0}" -f $vivo)

# encerra so o alvo que subi
if (-not $pa.HasExited) { Stop-Process -Id $pa.Id -Force -ErrorAction SilentlyContinue }
if ($Modo) { Remove-Item $arqModo -ErrorAction SilentlyContinue }

# veredito
$t = (Linhas $log) -join "`n"
$ok = $true
foreach ($m in @('conectado ao host','build enviado','desligada','descarregada')) {
    if ($t -notmatch [regex]::Escape($m)) { Write-Host "FALTOU: '$m'" -ForegroundColor Yellow; $ok = $false }
}
if (-not $vivo) { Write-Host "FALHA: alvo caiu" -ForegroundColor Red; $ok = $false }
if ($viuQuadros) { Write-Host "OK: linha de 300 quadros observada" -ForegroundColor Green }
if ($ok) { Write-Host "AUTOTESTE COM HOST ($Rotulo): PASSOU" -ForegroundColor Green }
else     { Write-Host "AUTOTESTE COM HOST ($Rotulo): FALHOU" -ForegroundColor Red }
