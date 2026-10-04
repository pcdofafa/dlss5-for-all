# fumaca-q1.ps1 -- Q1 (16/09/2026): card "DLSS do jogo" + depth D24S8 -> R32F, no alvo64-ngx (DLSS SR real).
#   Fase A (alvo com depth R32F): app de bancada2\bin-q1 integra (Nativo + upstream); pelo WM_COMMAND
#     da bandeja (C1.md secao 7) o card manda preset K, depois qualidade Desempenho, depois volta a
#     Auto/Jogo. Prova: `dlss-jogo: ... enviado` no app, `controle: feature recriada ... preset=K` na DLL,
#     `.ngx-estado` publicado pela DLL, chaves no [perfil:alvo64-ngx.exe] do ini, foto do painel.
#   Fase B (alvo --depth-d24s8): `ngx: depth convertida fmt=45 -> R32F` na DLL e
#     `upstream: depth quadro N: min=.. max=.. nao-zero=..%` no app (min 0.5 / max 1.0 = a cena do alvo).
# Um app por vez; so o DISPLAY2; Start-Sleep; encerra so o que abriu (por pid).
param([int]$Segundos = 10, [switch]$SoA, [switch]$SoB)
$ErrorActionPreference = 'Stop'
$aqui  = Split-Path -Parent $MyInvocation.MyCommand.Path
$raiz  = (Resolve-Path "$aqui\..\..\..\..").Path
$dll   = (Resolve-Path "$aqui\..\dlss5-ponte64.dll").Path
$alvo  = "$raiz\bancada3\alvo-ngx\alvo64-ngx.exe"
$bin   = "$raiz\bancada2\bin-q1"
$logs  = "$raiz\bancada2\logs"
$exe   = "$raiz\src\DLSS5ForAll-poc.exe"
function Diz($m) { Write-Host ("[fumaca-q1 {0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) -ForegroundColor Cyan }
function Linhas($f) { if (Test-Path $f) { Get-Content $f -ErrorAction SilentlyContinue } else { @() } }
# chave=valor numa secao do ini (cria a secao se nao existe)
function IniSecao($ini, $secao, $chave, $valor) {
    $linhas = @(Get-Content $ini -Encoding UTF8); $saida = New-Object System.Collections.Generic.List[string]
    $atual = ''; $feito = $false; $viuSecao = $false
    foreach ($l in $linhas) {
        if ($l -match '^\[(.+)\]') { if ($atual -eq $secao -and -not $feito) { $saida.Add("$chave=$valor"); $feito = $true }; $atual = $Matches[1]; if ($atual -eq $secao) { $viuSecao = $true } }
        if ($atual -eq $secao -and $l -match "^$chave=") { if (-not $feito) { $saida.Add("$chave=$valor"); $feito = $true }; continue }
        $saida.Add($l)
    }
    if (-not $feito) { if (-not $viuSecao) { $saida.Add("[$secao]") }; $saida.Add("$chave=$valor") }
    Set-Content $ini $saida -Encoding UTF8
}
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class W {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    // o app exclui as proprias janelas da captura de tela (WDA_EXCLUDEFROMCAPTURE): CopyFromScreen ve o que esta
    // atras. PrintWindow com PW_RENDERFULLCONTENT (2) pede a janela em si.
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
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
    // a maior janela visivel do app (classe FafaDLSS5*/DLSS5ForAll*): com o painel aberto, e o painel
    public static IntPtr MaiorJanelaDoApp(uint app) {
        IntPtr melhor = IntPtr.Zero; long area = 0;
        EnumWindows(delegate(IntPtr h, IntPtr p) { if (!IsWindowVisible(h)) return true;
            uint pid; GetWindowThreadProcessId(h, out pid); if (pid != app) return true;
            var c = new System.Text.StringBuilder(64); GetClassNameW(h, c, 64); string cs = c.ToString();
            if (!cs.StartsWith("FafaDLSS5") && !cs.StartsWith("DLSS5ForAll")) return true;
            RECT r; GetWindowRect(h, out r); long a = (long)(r.R - r.L) * (r.B - r.T);
            if (a > area) { area = a; melhor = h; }
            return true; }, IntPtr.Zero);
        return melhor;
    }
}
'@
[void][W]::SetProcessDpiAwarenessContext([IntPtr](-4))
function EsperaLog($arq, $regex, $seg) { $fim = (Get-Date).AddSeconds($seg); while ((Get-Date) -lt $fim) { if (((Linhas $arq) -join "`n") -match $regex) { return $true }; Start-Sleep -Milliseconds 500 }; return $false }
function Bandeja() { $h = [W]::FindWindowW("DLSS5ForAllTray", $null); if ($h -eq [IntPtr]::Zero) { throw "bandeja nao achada (app aberto?)" }; return $h }
function Comando($id) { [void][W]::PostMessageW((Bandeja), 0x111, [IntPtr]$id, [IntPtr]0) }
function FotoPainel($appPid, $nome) {
    $h = [W]::FindWindowW("DLSS5ForAllPanel", $null)   # a janela do painel (End), propria e em camadas
    if ($h -eq [IntPtr]::Zero -or -not [W]::IsWindowVisible($h)) { $h = [W]::MaiorJanelaDoApp([uint32]$appPid) }
    if ($h -eq [IntPtr]::Zero) { Diz "foto $nome PULADA: nenhuma janela visivel do app"; return }
    $r = New-Object W+RECT; [void][W]::GetWindowRect($h, [ref]$r)
    if ($r.R - $r.L -le 0) { Diz "foto $nome PULADA: janela sem tamanho"; return }
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    $okPw = [W]::PrintWindow($h, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $p = Join-Path $logs $nome; $bmp.Save($p, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); Diz "foto: $p (PrintWindow=$okPw, $($r.R - $r.L)x$($r.B - $r.T))"
}
function FecharApp($desde) {
    Start-Sleep -Seconds 1
    foreach ($p in @(Get-Process DLSS5ForAll -ErrorAction SilentlyContinue | Where-Object { $_.StartTime -gt $desde })) { Diz "fechando o app pid $($p.Id)"; Stop-Process -Id $p.Id -Force }
    Start-Sleep -Seconds 3
}

if (Get-Process DLSS5ForAll -ErrorAction SilentlyContinue) { Diz "ja existe um DLSS5ForAll vivo (de outro agente): abortando"; exit 5 }
if (-not (Test-Path "$bin\DLSS5ForAll.exe")) { Diz "montando $bin (copia da bin-g1 sem logs)"; New-Item -ItemType Directory $bin | Out-Null; Get-ChildItem "$raiz\bancada2\bin-g1" | Where-Object { $_.Name -notmatch '\.log|\.bak-|dlss5-feed\.status' } | ForEach-Object { Copy-Item $_.FullName "$bin\" -Recurse -Force } }
Copy-Item $exe "$bin\DLSS5ForAll.exe" -Force
Copy-Item $dll "$bin\dlss5-ponte64.dll" -Force
$ini = "$bin\dlss5forall.ini"
foreach ($kv in @(@('modo','automatico'),@('ponte_backbuffer','auto'),@('reabrir_auto','0'),@('janela_livre','900,60,1520,520'),@('nativo_upstream','1'),@('modo_processamento','auto'),@('dlss_preset','jogo'),@('dlss_qualidade','jogo'))) { IniSecao $ini 'ultima' $kv[0] $kv[1] }
IniSecao $ini 'perfil:alvo64-ngx.exe' 'dlss_preset' 'jogo'
IniSecao $ini 'perfil:alvo64-ngx.exe' 'dlss_qualidade' 'jogo'
$appLog = "$bin\dlss5forall.log"
$stamp = Get-Date -Format 'HHmmss'

function Fase($nome, $argsAlvo, $acao) {
    Remove-Item "$bin\dlss5forall.log*" -ErrorAction SilentlyContinue
    $pa = Start-Process -FilePath $alvo -ArgumentList $argsAlvo -WorkingDirectory (Split-Path $alvo) -PassThru
    Start-Sleep -Seconds 4
    if ($pa.HasExited) { Diz "alvo saiu (fase $nome)"; return }
    [void][W]::MinimizarConsole([uint32]$pa.Id)
    Diz "fase $nome : alvo-ngx pid=$($pa.Id) args=$($argsAlvo -join ' ')"
    Remove-Item "$env:TEMP\dlss5-ponte.$($pa.Id).*" -ErrorAction SilentlyContinue
    $t0 = Get-Date
    $papp = Start-Process -FilePath "$bin\DLSS5ForAll.exe" -ArgumentList @('--source','window','--window','ZEBRA-NGX') -WorkingDirectory $bin -PassThru
    Diz "app subindo; esperando 'integrar: ponte ativa'"
    if (-not (EsperaLog $appLog 'integrar: ponte ativa' 40)) { Diz "ponte nao ativou" }
    $ok = EsperaLog $appLog 'upstream: (sessao|ligad|desligado|backbuffer)' 40
    Diz "upstream no app: $ok"
    $dllLog = "$env:TEMP\dlss5-ponte.$($pa.Id).log"
    & $acao $pa.Id $dllLog
    Write-Host "----- app (dlss-jogo / upstream / integrar) -----" -ForegroundColor Yellow
    Linhas $appLog | Where-Object { $_ -match 'dlss-jogo|upstream: (sessao|ligad|desligado|backbuffer|depth|texturas)|integrar: (ponte|desligada)|perfil:' } | Select-Object -Last 30
    Write-Host "----- DLL -----" -ForegroundColor Yellow
    Linhas $dllLog | Where-Object { $_ -match '^ngx: (upstream ligado|prova de prontidao: |controle|depth convertida|300 aval|desligado|hook|create feature|evaluate feature)|^ponte: (build|desligada|descarregada)' } | Select-Object -Last 30
    Copy-Item $appLog "$logs\fumaca-q1-$nome-app-$stamp.log" -ErrorAction SilentlyContinue
    Copy-Item $dllLog "$logs\fumaca-q1-$nome-dll-$stamp.log" -ErrorAction SilentlyContinue
    FecharApp $t0
    $vivo = Get-Process -Id $pa.Id -ErrorAction SilentlyContinue
    Diz ("alvo no fim da fase {0}: {1}" -f $nome, $(if ($vivo) { 'VIVO' } else { 'MORREU' }))
    Stop-Process -Id $pa.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
}

# ---------------------------------------------------------------- fase A: o card
$acaoA = {
    param($pidAlvo, $dllLog)
    Start-Sleep -Seconds $Segundos
    $estado = "$env:TEMP\dlss5-ponte.$pidAlvo.ngx-estado"
    Diz ("estado publicado pela DLL: " + ((Linhas $estado) -join ' | '))
    # abre o painel (CMD_PANEL_TOGGLE = 300) e fotografa o card antes de mexer
    Comando 300; Start-Sleep -Seconds 2
    $app = Get-Process DLSS5ForAll -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($app) { FotoPainel $app.Id "fumaca-q1-painel-antes-$stamp.png" }
    Diz "card: preset K (CMD 2008)"
    Comando 2008
    $r1 = EsperaLog $dllLog 'controle: feature recriada handle=\d+ preset=K' 12
    Diz "DLL recriou com preset K: $r1"
    Start-Sleep -Seconds 2
    Diz ("estado publicado pela DLL: " + ((Linhas $estado) -join ' | '))
    if ($app) { FotoPainel $app.Id "fumaca-q1-painel-K-$stamp.png" }
    Diz "card: qualidade Desempenho (CMD 2023)"
    Comando 2023
    $r2 = EsperaLog $dllLog 'controle: feature recriada handle=\d+ preset=K qualidade=performance' 12
    Diz "DLL recriou com K + performance: $r2"
    Start-Sleep -Seconds 2
    Diz ("estado publicado pela DLL: " + ((Linhas $estado) -join ' | '))
    if ($app) { FotoPainel $app.Id "fumaca-q1-painel-K-perf-$stamp.png" }
    Write-Host "----- ini [perfil:alvo64-ngx.exe] com K + performance -----" -ForegroundColor Yellow
    $sec = ''; Get-Content $ini -Encoding UTF8 | ForEach-Object { if ($_ -match '^\[(.+)\]') { $sec = $Matches[1] } elseif ($sec -eq 'perfil:alvo64-ngx.exe' -and $_ -match '^dlss_') { $_ } }
    Diz "card: Auto (CMD 2000) e Jogo (CMD 2020)"
    Comando 2000; Start-Sleep -Seconds 3; Comando 2020
    $r3 = EsperaLog $dllLog 'controle: voltando a feature do jogo' 12
    Diz "DLL voltou a feature do jogo: $r3"
    Start-Sleep -Seconds 3
    Diz ("estado publicado pela DLL: " + ((Linhas $estado) -join ' | '))
    Comando 300; Start-Sleep -Seconds 1
    Write-Host "----- ini [perfil:alvo64-ngx.exe] -----" -ForegroundColor Yellow
    $sec = ''; Get-Content $ini -Encoding UTF8 | ForEach-Object { if ($_ -match '^\[(.+)\]') { $sec = $Matches[1] } elseif ($sec -eq 'perfil:alvo64-ngx.exe' -and $_ -match '^dlss_') { $_ } }
    Write-Host "----- ini [ultima] dlss_* -----" -ForegroundColor Yellow
    $sec = ''; Get-Content $ini -Encoding UTF8 | ForEach-Object { if ($_ -match '^\[(.+)\]') { $sec = $Matches[1] } elseif ($sec -eq 'ultima' -and $_ -match '^dlss_') { $_ } }
}
# ---------------------------------------------------------------- fase B: a depth
$acaoB = {
    param($pidAlvo, $dllLog)
    $c = EsperaLog $dllLog 'depth convertida fmt=\d+ -> R32F' 20
    Diz "DLL converteu a depth: $c"
    $m = EsperaLog $appLog 'upstream: depth quadro \d+: min=' 20
    Diz "app mediu a depth: $m"
    Start-Sleep -Seconds $Segundos
}

if (-not $SoB) { Fase 'A' @('--quadros','200000','--w','1280','--h','720','--x','100','--y','100') $acaoA }
if (-not $SoA) { Fase 'B' @('--quadros','200000','--w','1280','--h','720','--x','100','--y','100','--depth-d24s8') $acaoB }
Diz "logs em $logs\fumaca-q1-*-$stamp.*"
