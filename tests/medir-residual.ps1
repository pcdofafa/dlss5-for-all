# =====================================================================================
#  medir-residual.ps1 -- mede a rede neural em resolucao reduzida + composicao residual
#  (nr_reduzida / nr_escala / nr_residual do dlss5-feed.cfg) contra a rede em resolucao
#  cheia e contra a cadeia sem rede nenhuma.
#
#  DUAS FASES, e elas medem coisas diferentes:
#
#    NITIDEZ  alvo PARADO, DLSS5_DUMP grava a saida real da cadeia num PNG e o app encerra
#             sozinho. tests/nitidez_png.exe le os PNG e devolve laplaciano e gradiente.
#             Parado de proposito: com a cena andando o arrasto temporal da rede entra na
#             conta e a medida deixa de ser sobre nitidez.
#
#    FPS      alvo ANIMADO e DESTRAVADO (sem vsync), A/B INTERCALADO, lendo a linha
#             "600 frames:" do dlss5-feed.log. Com a fonte presa ao vblank a folga esconde
#             o custo e todas as configuracoes empatam.
#
#  TRES ARMADILHAS QUE JA INVALIDARAM MEDICOES NESTA BANCADA, e que este script fecha:
#
#    1. O APP REABRE A SI MESMO quando a janela capturada some (ShellExecute +
#       --reabrindo), com um PID novo. Fechar "o meu PID" nao fecha nada, e ao fim de tres
#       rodadas havia TRES instancias disputando a placa: a mesma configuracao mediu
#       100 fps numa rodada e 25 na seguinte. Aqui a varredura e por CAMINHO da pasta de
#       teste, ate nao sobrar nenhuma.
#    2. ESCREVER O CFG NAO GARANTE QUE O FEED LEU. Rodadas inteiras rodaram com nr_spike=0
#       enquanto o arquivo no disco dizia 1. Toda rodada agora confere, no log, a linha
#       "config (neural)" que o feed imprimiu -- e e DESCARTADA se nao bater.
#    3. GPU COMPARTILHADA. Com jogo aberto os valores absolutos nao valem nada. A fase de
#       fps alterna A,B,A,B e compara MEDIANAS, o que cancela a deriva lenta da carga
#       alheia; duas rodadas distantes no tempo mediriam o jogo, nao a nossa mudanca.
#
#  Uso:
#     powershell -ExecutionPolicy Bypass -File tests\medir-residual.ps1
#     ... [-Bin <pasta>] [-Saida <pasta>] [-Segundos 45] [-Voltas 3] [-SoNitidez] [-SoFps]
# =====================================================================================
param(
  [string]$Bin   = "C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\DLSS5ForAll-AMD\bin-teste-residual",
  [string]$Saida = "",
  [int]$Segundos = 45,
  [int]$Voltas   = 3,
  [switch]$SoNitidez,
  [switch]$SoFps
)

$ErrorActionPreference = "Stop"
$raiz  = Split-Path -Parent $PSScriptRoot          # ...\amd
$tests = Join-Path $raiz "tests"
if ($Saida -eq "") { $Saida = Join-Path $env:TEMP "medir-residual" }
New-Item -ItemType Directory -Force $Saida | Out-Null
$cfg   = Join-Path $Bin "dlss5-feed.cfg"
$log   = Join-Path $Bin "dlss5-feed.log"
$pasta = Split-Path -Leaf $Bin

# O monitor SECUNDARIO, em coordenada FISICA. Medido com DPI-awareness: num PowerShell
# comum o mesmo monitor responde 2293x960 e a janela "cabe" onde nao cabe.
$alvoX = 2100
$alvoY = 120

function Matar-Todos {
  for ($i = 0; $i -lt 20; $i++) {
    $vivos = @(Get-Process -ErrorAction SilentlyContinue |
               Where-Object { $_.ProcessName -eq 'DLSS5ForAll' -and $_.Path -like "*$pasta*" })
    if ($vivos.Count -eq 0) { return }
    $vivos | ForEach-Object { try { Stop-Process -Id $_.Id -Force -ErrorAction Stop } catch {} }
    Start-Sleep -Milliseconds 700
  }
}

function Escrever-Cfg($c) {
  $txt = @"
enabled=1
mode=2
hdr=-1
depth_inverted=-1
flags=-1
reset_every=0
warmup_rebuild=0
rebuild=0
log_frames=3
create_delay=59
preset=5
work_resolution=100
work_upscale=0
work_sharpness=0.45
gpu_timeout_ms=2000
buffer_home=1
async_home=0
sync_home=0
mv_scale_x=1.000
mv_scale_y=1.000
stall_log_ms=0
hud_restore=-1
hud_restore_strength=-1
dred=1
amd_carrier=-1
amd_color_fmt=87
amd_srgb=-1
amd_decoy_swapchain=0
nr_spike=$($c.s)
nr_passes=$($c.p)
nr_reduzida=$($c.r)
nr_escala=$($c.e)
nr_residual=$($c.f)

"@
  [System.IO.File]::WriteAllText($cfg, $txt.Replace("`r`n","`n"), (New-Object System.Text.UTF8Encoding($false)))
}

# A rodada so vale se o FEED leu a configuracao pedida E a cadeia de shaders subiu.
function Rodada-Valida($c) {
  if (-not (Test-Path $log)) { return $false }
  $esperado = "nr_spike=$($c.s) nr_passes=$($c.p) nr_reduzida=$($c.r) nr_escala=$($c.e)% nr_residual=$($c.f)%"
  $cn = @(Select-String -Path $log -Pattern 'config \(neural\)' -ErrorAction SilentlyContinue)
  if ($cn.Count -eq 0 -or ($cn | Where-Object { $_.Line -notmatch [regex]::Escape($esperado) }).Count -gt 0) {
    Write-Host ("      DESCARTADA: o feed leu outra coisa -> " +
                $(if ($cn.Count) { ($cn[-1].Line -replace '.*config \(neural\): ','') } else { 'nada' })) -ForegroundColor DarkYellow
    return $false
  }
  if (@(Select-String -Path $log -Pattern 'technique found' -ErrorAction SilentlyContinue).Count -eq 0) {
    Write-Host "      DESCARTADA: a cadeia de shaders nao subiu" -ForegroundColor DarkYellow
    return $false
  }
  return $true
}

$configs = @(
  @{ n="C-sem-rede";    s=0; p=1; r=0; e=65; f=100 },
  @{ n="A-rede-cheia";  s=1; p=1; r=0; e=65; f=100 },
  @{ n="B-red77";       s=1; p=1; r=1; e=77; f=100 },
  @{ n="B-red65";       s=1; p=1; r=1; e=65; f=100 },
  @{ n="B-red50";       s=1; p=1; r=1; e=50; f=100 },
  @{ n="B-red65-ingen"; s=1; p=1; r=1; e=65; f=-1  },
  @{ n="A-cheia-4p";    s=1; p=4; r=0; e=65; f=100 },
  @{ n="B-red65-4p";    s=1; p=4; r=1; e=65; f=100 }
)

$jogos = @(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match '^(re9|witcher|hollow_knight)' })
if ($jogos.Count -gt 0) {
  Write-Host "AVISO: ha jogo aberto ($($jogos[0].ProcessName)). A GPU esta compartilhada:" -ForegroundColor Yellow
  Write-Host "       so a comparacao RELATIVA (A/B intercalado) vale." -ForegroundColor Yellow
}

# ------------------------------------------------------------------ FASE NITIDEZ
if (-not $SoFps) {
  Write-Host "`n=== FASE NITIDEZ (alvo parado, dump da saida da cadeia) ===" -ForegroundColor Cyan
  Matar-Todos
  $alvo = Start-Process (Join-Path $tests "alvo_animado.exe") -ArgumentList '1600','900','0','1',"$alvoX","$alvoY" -PassThru
  Start-Sleep -Seconds 3
  foreach ($c in $configs) {
    $png = Join-Path $Saida "$($c.n).png"
    Remove-Item $png -ErrorAction SilentlyContinue
    for ($t = 1; $t -le 4 -and -not (Test-Path $png); $t++) {
      Matar-Todos
      Escrever-Cfg $c
      if (Test-Path $log) { [System.IO.File]::Delete($log) }
      $env:DLSS5_DUMP = $png
      $env:DLSS5_DUMP_FRAME = "300"
      $app = Start-Process (Join-Path $Bin "DLSS5ForAll.exe") -ArgumentList '--window','ZEBRA7','--outra-instancia' -PassThru
      $fim = (Get-Date).AddSeconds(70)
      while ((Get-Date) -lt $fim -and -not $app.HasExited) { Start-Sleep -Milliseconds 500 }
      Matar-Todos
      Remove-Item Env:\DLSS5_DUMP, Env:\DLSS5_DUMP_FRAME -ErrorAction SilentlyContinue
      Start-Sleep -Seconds 2
      if (-not ((Test-Path $png) -and (Rodada-Valida $c))) { Remove-Item $png -ErrorAction SilentlyContinue }
    }
    Copy-Item $log (Join-Path $Saida "$($c.n)-feed.log") -ErrorAction SilentlyContinue
    Write-Host ("  {0,-16} dump={1}" -f $c.n, (Test-Path $png))
  }
  Matar-Todos
  try { Stop-Process -Id $alvo.Id -ErrorAction Stop } catch {}
  Write-Host "`n--- nitidez (laplaciano = variancia do laplaciano; maior = mais detalhe fino) ---"
  & (Join-Path $tests "nitidez_png.exe") (Get-ChildItem $Saida -Filter *.png | ForEach-Object { $_.FullName })
}

# ------------------------------------------------------------------ FASE FPS (A/B intercalado)
if (-not $SoNitidez) {
  Write-Host "`n=== FASE FPS (alvo animado e destravado, A/B intercalado, $Segundos s por rodada) ===" -ForegroundColor Cyan
  function Uma-Rodada($c) {
    Matar-Todos
    Escrever-Cfg $c
    if (Test-Path $log) { [System.IO.File]::Delete($log) }
    $app = Start-Process (Join-Path $Bin "DLSS5ForAll.exe") -ArgumentList '--window','ZEBRA7','--outra-instancia' -PassThru
    Start-Sleep -Seconds $Segundos
    Matar-Todos
    Start-Sleep -Seconds 2
    if (-not (Rodada-Valida $c)) { return $null }
    $l = @(Select-String -Path $log -Pattern '600 frames:' -ErrorAction SilentlyContinue)
    if ($l.Count -eq 0) { Write-Host "      DESCARTADA: nao fechou 600 quadros" -ForegroundColor DarkYellow; return $null }
    return [double]($l[-1].Line -replace '.*\(([\d.]+) fps\).*','$1')
  }
  function Med($v) { if ($v.Count -eq 0) { return "-" }; $s = @($v | Sort-Object); return $s[[int]([math]::Floor($s.Count/2))] }

  # Os pares que importam: a mesma contagem de passes, com e sem reducao.
  $pares = @(
    @{ rot = "4 passes";  A = @{ n="A4"; s=1; p=4;  r=0; e=65; f=100 }; B = @{ n="B4"; s=1; p=4;  r=1; e=65; f=100 } },
    @{ rot = "10 passes"; A = @{ n="A10";s=1; p=10; r=0; e=65; f=100 }; B = @{ n="B10";s=1; p=10; r=1; e=65; f=100 } }
  )
  Matar-Todos
  $alvo = Start-Process (Join-Path $tests "alvo_animado.exe") -ArgumentList '1600','900','8','0',"$alvoX","$alvoY" -PassThru
  Start-Sleep -Seconds 3
  foreach ($par in $pares) {
    $fa = @(); $fb = @()
    for ($i = 1; $i -le $Voltas; $i++) {
      $a = Uma-Rodada $par.A
      $b = Uma-Rodada $par.B
      Write-Host ("  {0} volta {1}: cheia={2}  reduzida={3}" -f $par.rot, $i, $a, $b)
      if ($null -ne $a) { $fa += $a }
      if ($null -ne $b) { $fb += $b }
    }
    Write-Host ("  >>> {0}: mediana cheia={1}  mediana reduzida={2}   (cheia: {3} | reduz: {4})" -f
                $par.rot, (Med $fa), (Med $fb), ($fa -join ', '), ($fb -join ', ')) -ForegroundColor Green
  }
  Matar-Todos
  try { Stop-Process -Id $alvo.Id -ErrorAction Stop } catch {}
}

Write-Host "`nsaida em $Saida" -ForegroundColor Green
