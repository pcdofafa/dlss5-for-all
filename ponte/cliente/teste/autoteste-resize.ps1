# autoteste-resize.ps1 -- T8: redimensiona a janela do alvo com a ponte ativa e
# confere que a DLL refaz o Build (segundo "build enviado") sem derrubar o alvo.
# So o DISPLAY2. Encerra so o que este script subiu.
param([string]$Alvo = "$PSScriptRoot\..\..\..\..\bancada2\alvo64\alvo64-d3d11.exe")
$ErrorActionPreference = 'Stop'
$aqui  = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz  = (Resolve-Path "$aqui\..\..\..\..").Path
$dll   = (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path
$subir = "$raiz\bancada2\host-mock\subir-mock.ps1"
$Alvo  = (Resolve-Path $Alvo).Path

Add-Type -Namespace W -Name Api -MemberDefinition @'
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    public static extern bool MoveWindow(System.IntPtr h, int x, int y, int w, int hgt, bool repaint);
'@

Get-ChildItem "$env:TEMP\dlss5-ponte.*.log" -ErrorAction SilentlyContinue | Remove-Item -ErrorAction SilentlyContinue
$pa = Start-Process -FilePath $Alvo -PassThru
Start-Sleep -Seconds 3
if ($pa.HasExited) { Write-Host "ALVO MORREU"; exit 3 }

$antes = @(Get-Process dlss5-feed-host64 -ErrorAction SilentlyContinue | Select-Object -Expand Id)
& powershell -ExecutionPolicy Bypass -File $subir $Alvo $pa.Id | Out-Null
Start-Sleep -Seconds 2
$meuHost = @(@(Get-Process dlss5-feed-host64 -ErrorAction SilentlyContinue | Select-Object -Expand Id) | Where-Object { $antes -notcontains $_ })

$log = "$env:TEMP\dlss5-ponte.$($pa.Id).log"
& powershell -ExecutionPolicy Bypass -File "$aqui\Inject.ps1" $pa.Id $dll | Out-Null

# espera o 1o build
$fim = (Get-Date).AddSeconds(20)
while ((Get-Date) -lt $fim -and -not (((Get-Content $log -Raw -ErrorAction SilentlyContinue)) -match 'build enviado')) { Start-Sleep -Milliseconds 300 }
$builds1 = @(Select-String -Path $log -Pattern 'build enviado' -ErrorAction SilentlyContinue).Count
Write-Host "builds antes do resize: $builds1"

# redimensiona a janela do alvo (dispara WM_SIZE -> ResizeBuffers no alvo)
Start-Sleep -Seconds 2
$hwnd = $pa.MainWindowHandle
if ($hwnd -eq [System.IntPtr]::Zero) { $hwnd = (Get-Process -Id $pa.Id).MainWindowHandle }
Write-Host "redimensionando a janela (hwnd=$hwnd) para 1024x640..."
[void][W.Api]::MoveWindow($hwnd, 60, 60, 1024, 640, $true)
Start-Sleep -Seconds 4

$builds2 = @(Select-String -Path $log -Pattern 'build enviado' -ErrorAction SilentlyContinue).Count
Write-Host "builds depois do resize: $builds2"

# encerra o host e confere descarga
foreach ($hp in $meuHost) { $p = Get-Process -Id $hp -ErrorAction SilentlyContinue; if ($p -and $p.ProcessName -eq 'dlss5-feed-host64') { Stop-Process -Id $hp -Force -ErrorAction SilentlyContinue } }
Start-Sleep -Seconds 3

Write-Host "`n===== LOG DA DLL ====="
Get-Content $log
Write-Host "======================`n"

$vivo = -not $pa.HasExited
if (-not $pa.HasExited) { Stop-Process -Id $pa.Id -Force -ErrorAction SilentlyContinue }

if ($builds2 -gt $builds1 -and $vivo) { Write-Host "RESIZE: PASSOU (novo build apos o resize, alvo vivo)" -ForegroundColor Green }
else { Write-Host "RESIZE: INCONCLUSIVO (builds $builds1 -> $builds2, alvo vivo=$vivo) -- ver se o alvo chama ResizeBuffers no WM_SIZE" -ForegroundColor Yellow }
