# Bancada: alvo animado + app novo com a ponte forcada.
# Uso: bancada-ponte.ps1 [-Segundos 35] [-Cru 0] [-Janela cloak] [-Placa Radeon] [-Rede ""] [-Foto 1]
#   -Placa = trecho do nome da placa que APRESENTA (DLSS5_PONTE_PLACA)
#   -Rede  = trecho do nome da placa que processa (--placa); vazio = a escolha normal (RTX)
#   -Foto  = 1 tira uma foto da regiao da janela do alvo aos 2/3 do tempo
param([int]$Segundos = 35, [string]$Cru = "0", [string]$Janela = "cloak", [string]$Placa = "Radeon", [string]$Rede = "", [int]$Foto = 1)
$base = "C:\Users\PCDOFAFA\Desktop\dlss5 for all portable"
$bin  = "$base\bin-teste-ponte"
$out  = "C:\Users\PCDOFAFA\AppData\Local\Temp\claude\C--Users-PCDOFAFA-Desktop-dlss5-for-all-portable-DLSS5ForAll-AMD\d813d9b6-00e8-4398-be96-48a00b926649\scratchpad"
Copy-Item "$base\amd\DLSS5ForAll-ponte.exe" "$bin\DLSS5ForAll.exe" -Force
Get-ChildItem "$bin" -Filter "*.log*" | ForEach-Object { [System.IO.File]::Delete($_.FullName) }
$alvo = Start-Process -FilePath "$base\amd\tests\alvo_animado.exe" -ArgumentList "1280 720 8 1" -WorkingDirectory "$base\amd\tests" -PassThru
Start-Sleep 2
$env:DLSS5_PONTE_PLACA  = $Placa
$env:DLSS5_PONTE_CRU    = $Cru
$env:DLSS5_PONTE_JANELA = $Janela
$args = "--window ZEBRA7 --overlay --outra-instancia"
if ($Rede -ne "") { $args += " --placa `"$Rede`"" }
$app = Start-Process -FilePath "$bin\DLSS5ForAll.exe" -ArgumentList $args -WorkingDirectory $bin -PassThru
$meio = [int]($Segundos * 2 / 3)
Start-Sleep $meio
if ($Foto -eq 1) {
    Add-Type -AssemblyName System.Drawing
    $sig = '[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }'
    $u = Add-Type -MemberDefinition $sig -Name U32 -Namespace Foto -PassThru
    $h = ($alvo | ForEach-Object { $_.Refresh(); $_.MainWindowHandle })
    $r = New-Object Foto.U32+RECT
    if ([Foto.U32]::GetWindowRect($h, [ref]$r)) {
        $w = $r.R - $r.L; $hh = $r.B - $r.T
        $bmp = New-Object System.Drawing.Bitmap $w, $hh
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
        $bmp.Save("$out\foto-bancada.png", [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose()
        "foto: $out\foto-bancada.png ($w x $hh em $($r.L),$($r.T))"
    } else { "foto: GetWindowRect falhou" }
}
Start-Sleep ($Segundos - $meio)
$vivos = Get-Process | Where-Object { $_.Path -like "$bin\*" }
if ($vivos) { $vivos | ForEach-Object { "app vivo no fim: pid $($_.Id) cpu $([int]$_.CPU)s"; Stop-Process -Id $_.Id -Force } } else { "APP NAO ESTA MAIS VIVO (saiu ou caiu)" }
Start-Sleep 1
Stop-Process -Id $alvo.Id -Force -ErrorAction SilentlyContinue
"=== dlss5forall.log (linhas relevantes)"
Select-String -Path "$bin\dlss5forall.log" -Pattern "duas placas|apresentador|gpu: usando|QUEDA|Fatal|falhou|LAT |encerrando|neural" | Select-Object -First 60 | ForEach-Object { $_.Line }
"=== ReShade.log (bridge / swap chain)"
Select-String -Path "$bin\ReShade.log" -Pattern "Skipping|Recreated runtime|saida compartilhada" | Select-Object -Last 6 | ForEach-Object { $_.Line }
"=== WER (ultimos 3 min)"
Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddMinutes(-3)} -ErrorAction SilentlyContinue | Where-Object { $_.Id -eq 1000 -and $_.Message -like "*bin-teste-ponte*" } | Select-Object -First 1 | ForEach-Object { ($_.Message -split "`n" | Select-Object -First 6) -join " | " }
