# transicao-upstream-fase2.ps1 -- reproduz o defeito do H2 e prova a correcao: no MESMO alvo-ngx,
# N vezes seguidas: app com upstream (nativo_upstream=1) -> fecha o app -> app so fase 2
# (nativo_upstream=0) -> fecha o app. Cada troca e uma injecao nova da DLL no mesmo processo.
# Passa se em nenhum log da DLL/app aparecer 0x887A0005 (DEVICE_REMOVED), toda fase 2 ativar
# ("integrar: ponte ativa") e o alvo estiver vivo no fim.
param([int]$Rodadas = 3, [int]$SegundosUp = 12, [int]$SegundosF2 = 8)
$ErrorActionPreference = 'Stop'
$aqui = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz = (Resolve-Path "$aqui\..\..\..\..").Path
$bin  = "$raiz\bancada2\bin-g1"; $logs = "$raiz\bancada2\logs"
$alvo = "$raiz\bancada3\alvo-ngx\alvo64-ngx.exe"
function Diz($m) { Write-Host ("[transicao {0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) -ForegroundColor Cyan }
function Linhas($f) { if (Test-Path $f) { Get-Content $f -ErrorAction SilentlyContinue } else { @() } }
function EsperaLog($arq, $regex, $seg) { $fim = (Get-Date).AddSeconds($seg); while ((Get-Date) -lt $fim) { if (((Linhas $arq) -join "`n") -match $regex) { return $true }; Start-Sleep -Milliseconds 400 }; return $false }
function IniUltima($ini, $chave, $valor) {
    $l = @(Get-Content $ini -Encoding UTF8); $s = New-Object System.Collections.Generic.List[string]; $sec = ''; $ok = $false
    foreach ($x in $l) { if ($x -match '^\[(.+)\]') { if ($sec -eq 'ultima' -and -not $ok) { $s.Add("$chave=$valor"); $ok = $true }; $sec = $Matches[1] }
        if ($sec -eq 'ultima' -and $x -match "^$chave=") { if (-not $ok) { $s.Add("$chave=$valor"); $ok = $true }; continue }; $s.Add($x) }
    if (-not $ok) { $s.Add("$chave=$valor") }; Set-Content $ini $s -Encoding UTF8
}
function FecharApp($desde) { foreach ($p in @(Get-Process DLSS5ForAll -ErrorAction SilentlyContinue | Where-Object { $_.StartTime -gt $desde })) { Stop-Process -Id $p.Id -Force }; Start-Sleep -Seconds 2 }
if (Get-Process DLSS5ForAll -ErrorAction SilentlyContinue) { Diz "ja ha um DLSS5ForAll; abortando"; exit 5 }
Copy-Item "$raiz\src\DLSS5ForAll-poc.exe" "$bin\DLSS5ForAll.exe" -Force
Copy-Item (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path "$bin\dlss5-ponte64.dll" -Force
$ini = "$bin\dlss5forall.ini"; IniUltima $ini 'modo' 'automatico'; IniUltima $ini 'reabrir_auto' '0'
$appLog = "$bin\dlss5forall.log"; $stamp = Get-Date -Format 'HHmmss'
$pa = Start-Process -FilePath $alvo -ArgumentList @('--quadros','400000','--w','1280','--h','720','--x','100','--y','100') -WorkingDirectory (Split-Path $alvo) -PassThru
Start-Sleep -Seconds 4
$dllLog = "$env:TEMP\dlss5-ponte.$($pa.Id).log"
$falhas = 0; $resumo = @()
for ($r = 1; $r -le $Rodadas; $r++) {
    foreach ($modo in @(@('upstream','1','ngx: upstream ligado', $SegundosUp), @('fase2','0','integrar: ponte ativa', $SegundosF2))) {
        Remove-Item "$bin\dlss5forall.log*" -ErrorAction SilentlyContinue
        IniUltima $ini 'nativo_upstream' $modo[1]
        $t0 = Get-Date
        Start-Process -FilePath "$bin\DLSS5ForAll.exe" -ArgumentList @('--source','window','--window','ZEBRA-NGX') -WorkingDirectory $bin | Out-Null
        $arq = if ($modo[0] -eq 'upstream') { $dllLog } else { $appLog }
        $ok = EsperaLog $arq $modo[2] 45
        Start-Sleep -Seconds $modo[3]
        $dev = ((Linhas $dllLog) + (Linhas $appLog)) -match '887A0005|DEVICE_REMOVED|device is gone'
        $linha = "rodada $r $($modo[0]): ativou=$ok device_removed=$($dev.Count -gt 0)"
        Copy-Item $dllLog "$logs\transicao-$stamp-r$r-$($modo[0])-dll.log" -ErrorAction SilentlyContinue
        Copy-Item $appLog "$logs\transicao-$stamp-r$r-$($modo[0])-app.log" -ErrorAction SilentlyContinue
        FecharApp $t0
        $desc = EsperaLog $dllLog 'ponte: descarregada' 15
        $linha += " descarregada=$desc"
        Diz $linha; $resumo += $linha
        if (-not $ok -or $dev.Count -gt 0) { $falhas++ }
        Start-Sleep -Seconds 3
    }
}
$vivo = Get-Process -Id $pa.Id -ErrorAction SilentlyContinue
Stop-Process -Id $pa.Id -Force -ErrorAction SilentlyContinue
IniUltima $ini 'nativo_upstream' '1'
Write-Host "----- resumo -----" -ForegroundColor Yellow
$resumo | ForEach-Object { Write-Host "  $_" }
Diz ("alvo no fim: {0}; falhas: {1}; logs em {2}\transicao-{3}-*" -f $(if ($vivo) { 'VIVO' } else { 'MORREU' }), $falhas, $logs, $stamp)
