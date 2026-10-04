# autoteste-sem-host.ps1 -- prova, SEM o host, o que da para provar sozinho:
#   1. a DLL injeta e a thread sobe (nada no DllMain);
#   2. a descoberta de vtables (device/swapchain descartaveis) funciona;
#   3. o Present do alvo e interceptado pelo hook por vtable;
#   4. sem host, a ponte desliga sozinha (pipe nao apareceu em 5 s), restaura
#      as vtables e se descarrega -- e o alvo continua vivo e apresentando.
#
# O ciclo COMPLETO (conectado ao host, build enviado, 300 quadros) precisa do
# host de referencia e do injetar.ps1 do B3; ver autoteste-com-host.ps1.
#
# So o DISPLAY2. Encerra apenas o alvo que ESTE script abriu.
$ErrorActionPreference = 'Stop'
$aqui = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz = (Resolve-Path "$aqui\..\..\..\..").Path         # poc-integrado
$alvo = "$raiz\bancada2\alvo64\alvo64-d3d11.exe"
$dll  = (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path

if (-not (Test-Path $alvo)) { Write-Host "SEM ALVO: $alvo (o B3 ainda nao publicou); nada a testar."; exit 2 }

# limpa log antigo deste teste
Get-ChildItem "$env:TEMP\dlss5-ponte.*.log" -ErrorAction SilentlyContinue | Remove-Item -ErrorAction SilentlyContinue

Write-Host "Subindo o alvo D3D11..."
$p = Start-Process -FilePath $alvo -PassThru
Start-Sleep -Seconds 3
if ($p.HasExited) { Write-Host "ALVO MORREU ao subir (code $($p.ExitCode))"; exit 3 }
Write-Host "Alvo pid=$($p.Id) vivo. Injetando a DLL..."

& powershell -ExecutionPolicy Bypass -File "$aqui\Inject.ps1" $p.Id $dll

$log = "$env:TEMP\dlss5-ponte.$($p.Id).log"
Write-Host "Esperando o ciclo (conectar 5s -> desligar -> descarregar)... log: $log"
$fim = (Get-Date).AddSeconds(12)
while ((Get-Date) -lt $fim) {
    if (Test-Path $log) {
        $txt = Get-Content $log -Raw -ErrorAction SilentlyContinue
        if ($txt -match 'descarregada') { break }
    }
    Start-Sleep -Milliseconds 500
}

Write-Host "`n===== LOG DA PONTE ====="
if (Test-Path $log) { Get-Content $log } else { Write-Host "(log nao apareceu)" }
Write-Host "========================`n"

$vivo = -not $p.HasExited
Write-Host ("Alvo ainda vivo apos o ciclo: {0}" -f $vivo)

# encerra SO o alvo que este script abriu
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }

# veredito
$ok = $true
if (Test-Path $log) {
    $t = Get-Content $log -Raw
    foreach ($linha in @('injetada pid=', 'primeiro Present interceptado', 'desligada', 'descarregada')) {
        if ($t -notmatch [regex]::Escape($linha)) { Write-Host "FALTOU no log: '$linha'"; $ok = $false }
    }
} else { $ok = $false }
if (-not $vivo) { Write-Host "FALHA: o alvo caiu (a regra dura e que o jogo nunca cai)"; $ok = $false }
if ($ok) { Write-Host "AUTOTESTE SEM HOST: PASSOU" } else { Write-Host "AUTOTESTE SEM HOST: FALHOU" }
