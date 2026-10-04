# fumaca-upstream-app.ps1 -- upstream de ponta a ponta com o APP do G2 (nao o host do Feeder):
#   alvo64-ngx (DLSS SR 853x480 -> 1280x720) <- DLL (fase 2 + upstream) -> app (bin-g1) roda a rede
#   na cor pequena e devolve; o DLSS do alvo faz o super resolution em cima.
# Tres capturas do DISPLAY2 para comparar: (a) upstream, (b) fase 2 (nativo_upstream=0), (c) sem app.
# Um app por vez; so o DISPLAY2; encerra so o que abriu (por pid/nome+hora).
param([int]$SegundosUp = 45, [int]$SegundosF2 = 30, [switch]$SoUpstream)
$ErrorActionPreference = 'Stop'
$aqui  = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz  = (Resolve-Path "$aqui\..\..\..\..").Path
$dll   = (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path
$alvo  = "$raiz\bancada3\alvo-ngx\alvo64-ngx.exe"
$bin   = "$raiz\bancada2\bin-g1"
$logs  = "$raiz\bancada2\logs"
$exeG2 = "$raiz\src\DLSS5ForAll-poc.exe"
function Diz($m) { Write-Host ("[fumaca-up {0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) -ForegroundColor Cyan }
function Linhas($f) { if (Test-Path $f) { Get-Content $f -ErrorAction SilentlyContinue } else { @() } }
function IniUltima($ini, $chave, $valor) {
    $linhas = @(Get-Content $ini -Encoding UTF8); $saida = New-Object System.Collections.Generic.List[string]
    $secao = ''; $feito = $false
    foreach ($l in $linhas) {
        if ($l -match '^\[(.+)\]') { if ($secao -eq 'ultima' -and -not $feito) { $saida.Add("$chave=$valor"); $feito = $true }; $secao = $Matches[1] }
        if ($secao -eq 'ultima' -and $l -match "^$chave=") { if (-not $feito) { $saida.Add("$chave=$valor"); $feito = $true }; continue }
        $saida.Add($l)
    }
    if (-not $feito) { $saida.Add("$chave=$valor") }
    Set-Content $ini $saida -Encoding UTF8
}
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class W {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    // minimiza SO a janela de console do processo (classe ConsoleWindowClass); a janela do jogo fica como esta
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    // O console do alvo nasce num Windows Terminal (outro processo) cujo titulo traz o nome do exe:
    // minimiza toda janela visivel de topo com "alvo64-ngx" no titulo, exceto a do jogo (classe AlvoNgxDlss).
    public static int MinimizarConsole(uint alvo) {
        int n = 0;
        EnumWindows(delegate(IntPtr h, IntPtr p) { if (!IsWindowVisible(h)) return true;
            var c = new System.Text.StringBuilder(64); GetClassNameW(h, c, 64); if (c.ToString() == "AlvoNgxDlss") return true;
            var t = new System.Text.StringBuilder(512); GetWindowTextW(h, t, 512);
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (t.ToString().IndexOf("alvo64-ngx", StringComparison.OrdinalIgnoreCase) >= 0 || (pid == alvo && c.ToString() == "ConsoleWindowClass")) { ShowWindow(h, 6); n++; }
            return true; }, IntPtr.Zero);
        return n;
    }
}
'@
[void][W]::SetProcessDpiAwarenessContext([IntPtr](-4))
function Tela($nome) {
    $h = [W]::FindWindowW("AlvoNgxDlss", "ZEBRA-NGX"); if ($h -ne [IntPtr]::Zero) { [void][W]::SetForegroundWindow($h); Start-Sleep -Milliseconds 600 }
    $r = New-Object W+RECT; [void][W]::GetClientRect($h, [ref]$r); $pt = New-Object W+POINT; [void][W]::ClientToScreen($h, [ref]$pt)
    if ($r.R -le 0 -or $r.B -le 0) { Diz "captura $nome PULADA: janela do alvo sem cliente (h=$h)"; return }
    $bmp = New-Object System.Drawing.Bitmap $r.R, $r.B
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($pt.X, $pt.Y, 0, 0, $bmp.Size); $g.Dispose()
    $p = Join-Path $logs $nome; $bmp.Save($p, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); Diz "captura: $p"
}
function EsperaLog($arq, $regex, $seg) { $fim = (Get-Date).AddSeconds($seg); while ((Get-Date) -lt $fim) { if (((Linhas $arq) -join "`n") -match $regex) { return $true }; Start-Sleep -Milliseconds 500 }; return $false }
function FecharApp($desde) {
    Start-Sleep -Seconds 1
    foreach ($p in @(Get-Process DLSS5ForAll -ErrorAction SilentlyContinue | Where-Object { $_.StartTime -gt $desde })) { Diz "fechando o app pid $($p.Id)"; Stop-Process -Id $p.Id -Force }
    Start-Sleep -Seconds 3
}

if (Get-Process DLSS5ForAll -ErrorAction SilentlyContinue) { Diz "ja existe um DLSS5ForAll vivo (de outro agente): abortando"; exit 5 }
if (-not (Test-Path "$bin\DLSS5ForAll.exe")) { Diz "montando $bin (copia da bin-f0)"; Copy-Item "$raiz\bancada2\bin-f0" $bin -Recurse -Force }
Copy-Item $exeG2 "$bin\DLSS5ForAll.exe" -Force
Copy-Item $dll "$bin\dlss5-ponte64.dll" -Force
Remove-Item "$bin\dlss5forall.log*" -ErrorAction SilentlyContinue
$ini = "$bin\dlss5forall.ini"
IniUltima $ini 'modo' 'automatico'
IniUltima $ini 'ponte_backbuffer' 'auto'
IniUltima $ini 'reabrir_auto' '0'
IniUltima $ini 'janela_livre' '900,60,1520,520'
$appLog = "$bin\dlss5forall.log"
$stamp = Get-Date -Format 'HHmmss'

# alvo
$pa = Start-Process -FilePath $alvo -ArgumentList @('--quadros','200000','--w','1280','--h','720','--x','100','--y','100','--parado') -WorkingDirectory (Split-Path $alvo) -PassThru   # --parado: quadro congelado, para comparar imagem
Start-Sleep -Seconds 4
if ($pa.HasExited) { Diz "alvo saiu"; exit 3 }
# o console do alvo (que nasce por cima da janela do jogo) vai para a barra; a janela ZEBRA-NGX fica
$nc = [W]::MinimizarConsole([uint32]$pa.Id); Diz "console do alvo minimizado ($nc janela)"
Diz "alvo-ngx pid=$($pa.Id)"
Remove-Item "$env:TEMP\dlss5-ponte.$($pa.Id).*" -ErrorAction SilentlyContinue

# (a) upstream
IniUltima $ini 'nativo_upstream' '1'
$t0 = Get-Date
Start-Process -FilePath "$bin\DLSS5ForAll.exe" -ArgumentList @('--source','window','--window','ZEBRA-NGX') -WorkingDirectory $bin | Out-Null
Diz "app (upstream) subindo; esperando 'integrar: ponte ativa'"
if (-not (EsperaLog $appLog 'integrar: ponte ativa' 40)) { Diz "ponte nao ativou"; }
$ok = EsperaLog $appLog 'upstream: (sessao|ligad|desligado|backbuffer)' 40
Diz "upstream no app: $ok"
Start-Sleep -Seconds $SegundosUp
Tela "fumaca-up-a-upstream-$stamp.png"
$dllLog = "$env:TEMP\dlss5-ponte.$($pa.Id).log"
Write-Host "----- app (upstream/integrar/ponte) -----" -ForegroundColor Yellow
Linhas $appLog | Where-Object { $_ -match 'upstream|integrar:|ponte: (sessao|output|build|backbuffer)|reabr' } | Select-Object -Last 25
Write-Host "----- DLL -----" -ForegroundColor Yellow
Linhas $dllLog | Where-Object { $_ -match '^ngx: (upstream|prova|300 aval|desligado|hook)|^ponte: (build|desligada|descarregada|conectado)' } | Select-Object -Last 25
Copy-Item $appLog "$logs\fumaca-up-app-upstream-$stamp.log" -ErrorAction SilentlyContinue
Copy-Item $dllLog "$logs\fumaca-up-dll-upstream-$stamp.log" -ErrorAction SilentlyContinue
FecharApp $t0
if ($SoUpstream) { Stop-Process -Id $pa.Id -Force -ErrorAction SilentlyContinue; exit 0 }

# (b) fase 2
Remove-Item "$bin\dlss5forall.log*" -ErrorAction SilentlyContinue
IniUltima $ini 'nativo_upstream' '0'
$t1 = Get-Date
Start-Process -FilePath "$bin\DLSS5ForAll.exe" -ArgumentList @('--source','window','--window','ZEBRA-NGX') -WorkingDirectory $bin | Out-Null
$okF2 = EsperaLog $appLog 'integrar: ponte ativa' 40
Diz "fase 2 ativa: $okF2"
Start-Sleep -Seconds $SegundosF2
Tela "fumaca-up-b-fase2-$stamp.png"
Copy-Item $appLog "$logs\fumaca-up-app-fase2-$stamp.log" -ErrorAction SilentlyContinue
Copy-Item "$env:TEMP\dlss5-ponte.$($pa.Id).log" "$logs\fumaca-up-dll-fase2-$stamp.log" -ErrorAction SilentlyContinue
FecharApp $t1

# (c) sem app
Start-Sleep -Seconds 4
Tela "fumaca-up-c-semapp-$stamp.png"
$vivo = Get-Process -Id $pa.Id -ErrorAction SilentlyContinue
Diz ("alvo no fim: {0}" -f $(if ($vivo) { 'VIVO' } else { 'MORREU' }))
Stop-Process -Id $pa.Id -Force -ErrorAction SilentlyContinue
IniUltima $ini 'nativo_upstream' '1'
Diz "logs e capturas em $logs\fumaca-up-*-$stamp.*"
