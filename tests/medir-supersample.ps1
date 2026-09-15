# =====================================================================================
#  medir-supersample.ps1 -- mede a rede neural rodando ACIMA da resolucao de saida
#  (nr_escala > 100 no dlss5-feed.cfg) contra ela rodando na resolucao de saida.
#
#  Mesma bancada e as mesmas tres travas de medir-residual.ps1 (leia os comentarios de la):
#  encerrar por CAMINHO e nao por PID, conferir no log a configuracao que o feed leu, e
#  A/B INTERCALADO porque a GPU e compartilhada.
#
#  Uso:
#    powershell -ExecutionPolicy Bypass -File tests\medir-supersample.ps1
#    ... [-Bin <pasta>] [-Saida <pasta>] [-Segundos 40] [-Voltas 2] [-SoNitidez] [-SoFps]
# =====================================================================================
param(
  [string]$Bin   = "C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\DLSS5ForAll-AMD\bin-teste-residual",
  [string]$Saida = "",
  [int]$Segundos = 40,
  [int]$Voltas   = 2,
  [string]$Img   = "",
  [switch]$SoNitidez,
  [switch]$SoFps
)

$ErrorActionPreference = "Stop"
$raiz  = Split-Path -Parent $PSScriptRoot
$tests = Join-Path $raiz "tests"
if ($Saida -eq "") { $Saida = Join-Path $env:TEMP "medir-supersample" }
New-Item -ItemType Directory -Force $Saida | Out-Null
$cfg   = Join-Path $Bin "dlss5-feed.cfg"
$log   = Join-Path $Bin "dlss5-feed.log"
$pasta = Split-Path -Leaf $Bin
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

# TODO alvo antigo tem de morrer antes de comecar. O titulo da janela do alvo e FIXO
# (ZEBRA7) e o app acha por titulo: um alvo esquecido de outra sessao -- um `--img`, por
# exemplo, cuja janela tem outro tamanho -- e capturado no lugar do certo, e o dump sai
# de uma cena que nao e a da medicao. Aconteceu: uma rodada saiu 1276x711 no meio de sete
# rodadas de 1596x891, com um laplaciano de 55 que nao queria dizer nada.
function Matar-Alvos {
  Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -eq 'alvo_animado' } |
    ForEach-Object { try { Stop-Process -Id $_.Id -Force -ErrorAction Stop } catch {} }
  Start-Sleep -Milliseconds 500
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
  # E a rede subiu MESMO na escala pedida? Um teto de memoria ou de lado que tenha agido
  # deixa a rede na resolucao de saida em silencio -- do ponto de vista do fps, isso
  # pareceria "o supersampling nao custou nada".
  if ($c.r -ne 0 -and @(Select-String -Path $log -Pattern 'nr: (supersampled|reduced-resolution) path ready' -ErrorAction SilentlyContinue).Count -eq 0) {
    Write-Host "      DESCARTADA: a rede nao subiu na escala pedida (teto de memoria/lado?)" -ForegroundColor DarkYellow
    return $false
  }
  return $true
}

$configs = @(
  @{ n="C-sem-rede"; s=0; p=1; r=0; e=100; f=100 },
  @{ n="SS-100-1p";  s=1; p=1; r=0; e=100; f=100 },
  @{ n="SS-125-1p";  s=1; p=1; r=1; e=125; f=100 },
  @{ n="SS-150-1p";  s=1; p=1; r=1; e=150; f=100 },
  @{ n="SS-200-1p";  s=1; p=1; r=1; e=200; f=100 },
  @{ n="SS-200-1p-direto"; s=1; p=1; r=1; e=200; f=-1 },
  @{ n="SS-100-4p";  s=1; p=4; r=0; e=100; f=100 },
  @{ n="SS-200-4p";  s=1; p=4; r=1; e=200; f=100 }
)

# ------------------------------------------------------------------ FASE NITIDEZ
if (-not $SoFps) {
  Write-Host "`n=== FASE NITIDEZ ===" -ForegroundColor Cyan
  Matar-Todos
  Matar-Alvos
  $args = if ($Img -ne "") { @('--img', $Img, "$alvoX", "$alvoY") } else { @('1600','900','0','1',"$alvoX","$alvoY") }
  $alvo = Start-Process (Join-Path $tests "alvo_animado.exe") -ArgumentList $args -PassThru
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
      $fim = (Get-Date).AddSeconds(90)
      while ((Get-Date) -lt $fim -and -not $app.HasExited) { Start-Sleep -Milliseconds 500 }
      Matar-Todos
      Remove-Item Env:\DLSS5_DUMP, Env:\DLSS5_DUMP_FRAME -ErrorAction SilentlyContinue
      Start-Sleep -Seconds 2
      if (-not ((Test-Path $png) -and (Rodada-Valida $c))) { Remove-Item $png -ErrorAction SilentlyContinue }
    }
    Copy-Item $log (Join-Path $Saida "$($c.n)-feed.log") -ErrorAction SilentlyContinue
    Write-Host ("  {0,-18} dump={1}" -f $c.n, (Test-Path $png))
  }
  Matar-Todos
  try { Stop-Process -Id $alvo.Id -ErrorAction Stop } catch {}
  Write-Host "`n--- nitidez (laplaciano; maior = mais detalhe fino) ---"
  & (Join-Path $tests "nitidez_png.exe") (Get-ChildItem $Saida -Filter *.png | ForEach-Object { $_.FullName })

  # A SEGUNDA METRICA, e sem ela a primeira mente. No modo residual a saida e
  # `nativo + delta`: quanto MENOR o delta, mais a saida se parece com o nativo e MAIOR o
  # laplaciano. Ou seja, "mais nitido" pode querer dizer apenas "a rede fez menos" -- e uma
  # configuracao que nao faz nada ganharia a tabela de nitidez. O MAE contra a referencia
  # sem rede diz quanto a rede realmente mexeu; as duas juntas e que decidem.
  $ref = Join-Path $Saida "C-sem-rede.png"
  if (Test-Path $ref) {
    Write-Host "`n--- quanto a rede alterou a imagem (contra C-sem-rede) ---"
    foreach ($f in Get-ChildItem $Saida -Filter *.png) {
      if ($f.Name -eq "C-sem-rede.png") { continue }
      $s = & (Join-Path $tests "comparar_png.exe") $ref $f.FullName 4
      $mae  = ($s | Select-String 'MAE') -replace '.*MAE\s+([\d.,]+).*','$1'
      $psnr = ($s | Select-String 'PSNR') -replace '.*PSNR\s+([\d.,]+).*','$1'
      Write-Host ("  {0,-20} MAE={1,-8} PSNR={2}" -f $f.BaseName, $mae, $psnr)
    }
  }
}

# ------------------------------------------------------------------ FASE FPS
if (-not $SoNitidez) {
  Write-Host "`n=== FASE FPS (alvo destravado, A/B intercalado, $Segundos s por rodada) ===" -ForegroundColor Cyan
  function Uma-Rodada($c) {
    Matar-Todos
    Escrever-Cfg $c
    if (Test-Path $log) { [System.IO.File]::Delete($log) }
    $app = Start-Process (Join-Path $Bin "DLSS5ForAll.exe") -ArgumentList '--window','ZEBRA7','--outra-instancia' -PassThru
    Start-Sleep -Seconds $Segundos
    Matar-Todos
    Start-Sleep -Seconds 2
    if (-not (Rodada-Valida $c)) { return $null }
    $l = @(Select-String -Path $log -Pattern '600 frames: feed CPU' -ErrorAction SilentlyContinue)
    if ($l.Count -eq 0) { Write-Host "      DESCARTADA: nao fechou 600 quadros" -ForegroundColor DarkYellow; return $null }
    return [double]($l[-1].Line -replace '.*\(([\d.]+) fps\).*','$1')
  }
  function Med($v) { if ($v.Count -eq 0) { return "-" }; $s = @($v | Sort-Object); return $s[[int]([math]::Floor($s.Count/2))] }

  $pares = @(
    @{ rot = "1 passe, 100% vs 150%"; A = @{ s=1; p=1; r=0; e=100; f=100 }; B = @{ s=1; p=1; r=1; e=150; f=100 } },
    @{ rot = "1 passe, 100% vs 200%"; A = @{ s=1; p=1; r=0; e=100; f=100 }; B = @{ s=1; p=1; r=1; e=200; f=100 } },
    @{ rot = "4 passes, 100% vs 200%";A = @{ s=1; p=4; r=0; e=100; f=100 }; B = @{ s=1; p=4; r=1; e=200; f=100 } }
  )
  Matar-Todos
  Matar-Alvos
  $alvo = Start-Process (Join-Path $tests "alvo_animado.exe") -ArgumentList '1600','900','8','0',"$alvoX","$alvoY" -PassThru
  Start-Sleep -Seconds 3
  foreach ($par in $pares) {
    $fa = @(); $fb = @()
    for ($i = 1; $i -le $Voltas; $i++) {
      $a = Uma-Rodada $par.A
      $b = Uma-Rodada $par.B
      Write-Host ("  {0} volta {1}: saida={2}  ampliada={3}" -f $par.rot, $i, $a, $b)
      if ($null -ne $a) { $fa += $a }
      if ($null -ne $b) { $fb += $b }
    }
    Write-Host ("  >>> {0}: mediana saida={1}  mediana ampliada={2}   (saida: {3} | ampl: {4})" -f
                $par.rot, (Med $fa), (Med $fb), ($fa -join ', '), ($fb -join ', ')) -ForegroundColor Green
  }
  Matar-Todos
  try { Stop-Process -Id $alvo.Id -ErrorAction Stop } catch {}
}

Write-Host "`nsaida em $Saida" -ForegroundColor Green
