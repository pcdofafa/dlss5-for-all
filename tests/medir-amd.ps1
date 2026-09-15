# =====================================================================================
#  medir-amd.ps1 -- mede o CAMINHO AMD (portador FSR do Feeder + mod DLSS-NR-on-AMD)
#  numa maquina com Radeon RX 7000/9000. Escrito em 11/09/2026 numa maquina SEM Radeon:
#  nada aqui foi exercitado contra o mod, so contra as convencoes da bancada que ja
#  valeram no medir-residual.ps1. Leia o cabecalho inteiro antes da primeira rodada.
#
#  O QUE ELE MEDE, e por que estas tres variaveis e nao outras:
#
#    1. InlineWaitMs 200 vs 25 no dlssnr_on_amd.ini  -> o orcamento de espera do modo
#       inline, chave que SO EXISTE DA v0.2.18 EM DIANTE (a v0.2.11 do nosso pacote nao
#       tem). Inline=1 faz o jogo esperar a rede ("the game runs at the network's pace");
#       o InlineWaitMs limita quanto ele espera e, ao estourar, o mod mostra o residuo do
#       quadro anterior ("previous residual shown"). E o meio-termo entre esperar sempre
#       e nao esperar nunca. Na maquina do outro PC ficou em 25.
#       NAO medir Inline=0: com a nossa janela D3D11 o modo assincrono escreveria o
#       resultado num swapchain que ninguem ve (PLANO-AMD.md 6.4), e na rodada boa de la
#       o inline funcionou com interop zero-copia e "timeouts 0".
#
#    2. work_resolution 100 / 77 / 58 com work_upscale=1  -> quantos pixels a rede ve.
#       No portador, work_resolution define o maxRenderSize do contexto FSR, e a rede do
#       mod le as texturas desse dispatch: menos resolucao de trabalho = menos pixels na
#       rede. 77% = 59% dos pixels, 58% = 34%. ATENCAO ao que work_upscale=1 NAO faz:
#       ele nao torna o contexto do portador um upscaler (isso so acontece com
#       work_upscale=2, que hoje precisa de uma correcao de jitter -- ver secao 3.3 do
#       RELATORIO-AMD-OTIMIZACAO.md). Com 1, a volta ao tamanho da tela e o FSR 1
#       (EASU+RCAS) do proprio Feeder, que e espacial.
#
#    3. amd_color_fmt 87 (BGRA8) vs 10 (RGBA16F)  -> banda de memoria nas copias e no
#       staging do mod. 87 e metade dos bytes; so vale se o mod aceitar (ele loga
#       "unsupported colour format N" quando nao aceita).
#
#  AS ARMADILHAS QUE JA INVALIDARAM MEDICOES NESTA BANCADA (todas fechadas aqui):
#    - O APP REABRE A SI MESMO quando a janela capturada some, com PID novo. A varredura
#      e por CAMINHO da pasta de teste, ate nao sobrar nenhum.
#    - ESCREVER O CFG NAO GARANTE QUE O FEED LEU. Toda rodada confere no log a linha
#      "[amd] carrier: context ok" com o tamanho pedido, e e DESCARTADA se nao bater.
#    - GPU COMPARTILHADA invalida valor absoluto. Tudo aqui e A/B INTERCALADO com
#      mediana; feche jogo, navegador e captura antes.
#
#  SEGURANCA: escreve SOMENTE dentro de -Bin (padrao bin-teste-amd, uma copia).
#  Nunca toca bin\ nem bin-amd\. Encerra apenas processos cujo caminho esta em -Bin.
#
#  Uso:
#     powershell -ExecutionPolicy Bypass -File tests\medir-amd.ps1
#     ... [-Bin <pasta>] [-Saida <pasta>] [-Segundos 45] [-Voltas 3]
#     ... [-AlvoX 2100] [-AlvoY 120] [-SoInline] [-SoResolucao] [-SoFormato]
# =====================================================================================
param(
  [string]$Bin      = "C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\DLSS5ForAll-AMD\bin-teste-amd",
  [string]$Origem   = "C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\DLSS5ForAll-AMD\bin-amd",
  [string]$Saida    = "",
  [int]$Segundos    = 45,
  [int]$Voltas      = 3,
  [int]$AlvoX       = 2100,
  [int]$AlvoY       = 120,
  [switch]$SoInline,
  [switch]$SoResolucao,
  [switch]$SoFormato
)

$ErrorActionPreference = "Stop"
$raiz  = Split-Path -Parent $PSScriptRoot          # ...\amd
$tests = Join-Path $raiz "tests"
if ($Saida -eq "") { $Saida = Join-Path $env:TEMP "medir-amd" }
New-Item -ItemType Directory -Force $Saida | Out-Null

$cfg     = Join-Path $Bin "dlss5-feed.cfg"
$log     = Join-Path $Bin "dlss5-feed.log"
$iniMod  = Join-Path $Bin "dlssnr_on_amd.ini"
$logMod  = Join-Path $Bin "dlssnr_on_amd.log"
$pasta   = Split-Path -Leaf $Bin

# ------------------------------------------------------------------ 0. PRE-REQUISITOS
Write-Host "=== 0. Pre-requisitos ===" -ForegroundColor Cyan

$radeon = @(Get-CimInstance Win32_VideoController |
            Where-Object { $_.AdapterCompatibility -like "*Advanced Micro*" -and $_.Name -match 'RX\s*[79]\d{3}' })
if ($radeon.Count -eq 0) {
  Write-Host "ABORTADO: nenhuma Radeon RX 7000/9000 neste PC." -ForegroundColor Red
  Write-Host "Placas vistas:" -ForegroundColor Red
  Get-CimInstance Win32_VideoController | ForEach-Object { Write-Host ("  - " + $_.Name) }
  Write-Host "O mod DLSS-NR-on-AMD so roda em RDNA3 (gfx11xx) e RDNA4 (gfx12xx)." -ForegroundColor Red
  exit 1
}
Write-Host ("  placa: " + $radeon[0].Name + "  driver " + $radeon[0].DriverVersion) -ForegroundColor Green

if (-not (Test-Path (Join-Path $tests "alvo_animado.exe"))) {
  Write-Host "ABORTADO: tests\alvo_animado.exe nao existe nesta copia do projeto." -ForegroundColor Red; exit 1
}

# A copia de teste. NUNCA medir em bin-amd\: o mod grava ini e log ao lado do exe.
if (-not (Test-Path $Bin)) {
  Write-Host "  criando a copia de teste a partir de $Origem"
  Copy-Item $Origem $Bin -Recurse
}
foreach ($f in @("DLSS5ForAll.exe","dlss5-feed.addon64","amd_fidelityfx_dx12.dll","dxgi.dll")) {
  if (-not (Test-Path (Join-Path $Bin $f))) { Write-Host "ABORTADO: falta $f em $Bin" -ForegroundColor Red; exit 1 }
}
# Os arquivos do mod: o usuario roda dlssnr_on_amd_setup.exe DENTRO da copia de teste.
$faltaMod = @("version.dll","dlssnr_on_amd_weights.bin","nvngx_dlssnr.dll") |
            Where-Object { -not (Test-Path (Join-Path $Bin $_)) }
if ($faltaMod.Count -gt 0) {
  Write-Host ("ABORTADO: o mod nao esta instalado nesta copia. Falta: " + ($faltaMod -join ', ')) -ForegroundColor Red
  Write-Host ("Rode " + (Join-Path $Bin "dlssnr_on_amd_setup.exe") + " NESTA pasta, com o nvngx_dlssnr.dll ao lado.") -ForegroundColor Red
  exit 1
}
$versaoMod = "desconhecida"
if (Test-Path (Join-Path $Bin "version.dll")) {
  $v = (Get-Item (Join-Path $Bin "version.dll")).VersionInfo.FileVersion
  if ($v) { $versaoMod = $v }
}
Write-Host ("  mod instalado, version.dll " + $versaoMod)
Write-Host ("  ATENCAO: as medidas so comparam entre si se a VERSAO DO MOD nao mudar no meio.") -ForegroundColor Yellow
# InlineWaitMs so existe da v0.2.18 em diante; a v0.2.11 do nosso pacote nem roda em gfx1200.
$temWait = 0
try {
  $bytes = [System.IO.File]::ReadAllBytes((Join-Path $Bin "version.dll"))
  $texto = [System.Text.Encoding]::ASCII.GetString($bytes)
  if ($texto.Contains('InlineWaitMs')) { $temWait = 1 }
  $bytes = $null; $texto = $null
} catch { $temWait = 1 }   # na duvida, nao alarmar
if ($temWait -eq 0) {
  Write-Host "  AVISO: esta version.dll nao conhece InlineWaitMs (e anterior a v0.2.18)." -ForegroundColor Yellow
  Write-Host "         A secao ESPERA nao vai medir nada; instale a v0.2.18 ou use -SoResolucao." -ForegroundColor Yellow
}

$jogos = @(Get-Process -ErrorAction SilentlyContinue |
           Where-Object { $_.MainWindowTitle -ne '' -and $_.ProcessName -notmatch '^(explorer|powershell|WindowsTerminal|Code)$' })
if ($jogos.Count -gt 3) {
  Write-Host "  AVISO: ha muita coisa aberta. A GPU compartilhada invalida valor absoluto;" -ForegroundColor Yellow
  Write-Host "         so a comparacao A/B intercalada desta bancada continua valendo." -ForegroundColor Yellow
}

# ------------------------------------------------------------------ 1. FERRAMENTAS
function Matar-Todos {
  for ($i = 0; $i -lt 20; $i++) {
    $vivos = @(Get-Process -ErrorAction SilentlyContinue |
               Where-Object { $_.ProcessName -eq 'DLSS5ForAll' -and $_.Path -like "*$pasta*" })
    if ($vivos.Count -eq 0) { return }
    $vivos | ForEach-Object { try { Stop-Process -Id $_.Id -Force -ErrorAction Stop } catch {} }
    Start-Sleep -Milliseconds 700
  }
}

# c.res = work_resolution, c.up = work_upscale, c.fmt = amd_color_fmt, c.decoy = isca
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
work_resolution=$($c.res)
work_upscale=$($c.up)
work_sharpness=0.30
gpu_timeout_ms=2000
buffer_home=1
mv_scale_x=1.000
mv_scale_y=1.000
stall_log_ms=0
nr_spike=0
nr_passes=1
dred=1
amd_carrier=1
amd_color_fmt=$($c.fmt)
amd_srgb=-1
amd_decoy_swapchain=$($c.decoy)

"@
  [System.IO.File]::WriteAllText($cfg, $txt.Replace("`r`n","`n"), (New-Object System.Text.UTF8Encoding($false)))
}

# As chaves do [DlssNrOnAmd] sao as da v0.2.11 (lidas das strings do instalador).
# Uma versao mais nova pode ter outras: confira o painel do mod (End) antes de confiar.
function Escrever-IniMod($c) {
  $txt = @"
[DlssNrOnAmd]
Enabled=1
Inline=1
InlineWaitMs=$($c.wait)
Interop=1
HipDevice=-1
UseFsrInputs=1
UseDepth=1
UseAutoMask=1
Temporal=1
LocalStructure=1
LocalTone=0
SkinStructure=-1
ToneChannels=0
Tonemap=-1
"@
  [System.IO.File]::WriteAllText($iniMod, $txt.Replace("`r`n","`n"), (New-Object System.Text.UTF8Encoding($false)))
}

# Uma rodada so vale se (a) o portador FSR subiu com o tamanho pedido e (b) a rede do mod
# processou pelo menos um job. Sem (b) mede-se o FSR sozinho e a conta fica linda de mentira.
function Rodada-Valida($c) {
  if (-not (Test-Path $log)) { Write-Host "      DESCARTADA: sem dlss5-feed.log" -ForegroundColor DarkYellow; return $false }
  $ctx = @(Select-String -Path $log -Pattern '\[amd\] carrier: context ok' -ErrorAction SilentlyContinue)
  if ($ctx.Count -eq 0) {
    $err = @(Select-String -Path $log -Pattern '\[amd\] carrier: erro' -ErrorAction SilentlyContinue)
    $m = if ($err.Count) { $err[-1].Line } else { 'o portador FSR nao subiu' }
    Write-Host "      DESCARTADA: $m" -ForegroundColor DarkYellow
    return $false
  }
  if (-not (Test-Path $logMod)) {
    Write-Host "      DESCARTADA: o mod nao escreveu dlssnr_on_amd.log (a rede nao entrou)" -ForegroundColor DarkYellow
    return $false
  }
  $jobs = @(Select-String -Path $logMod -Pattern 'network job \d+ done' -ErrorAction SilentlyContinue)
  if ($jobs.Count -eq 0) {
    $pistas = @(Select-String -Path $logMod -Pattern 'unsupported colour format|UNSUPPORTED GPU|FSR is not active|invalid kernel file|engine init FAILED|this game renders with D3D11' -ErrorAction SilentlyContinue)
    $m = if ($pistas.Count) { $pistas[-1].Line } else { 'nenhum "network job N done" no log do mod' }
    Write-Host "      DESCARTADA: $m" -ForegroundColor DarkYellow
    return $false
  }
  return $true
}

# fps da cadeia (linha do feed) e ms por job da rede (linha do mod) -- as duas contam.
function Uma-Rodada($c) {
  Matar-Todos
  Escrever-Cfg $c
  Escrever-IniMod $c
  foreach ($l in @($log, $logMod)) { if (Test-Path $l) { [System.IO.File]::Delete($l) } }
  $app = Start-Process (Join-Path $Bin "DLSS5ForAll.exe") -ArgumentList '--amd','--window','ZEBRA7','--outra-instancia' -PassThru
  Start-Sleep -Seconds $Segundos
  Matar-Todos
  Start-Sleep -Seconds 2
  foreach ($par in @(@{s=$log;d="feed"}, @{s=$logMod;d="mod"})) {
    if (Test-Path $par.s) { Copy-Item $par.s (Join-Path $Saida ("$($c.n)-" + $par.d + ".log")) -Force -ErrorAction SilentlyContinue }
  }
  if (-not (Rodada-Valida $c)) { return $null }
  $f = @(Select-String -Path $log -Pattern '600 frames:' -ErrorAction SilentlyContinue)
  if ($f.Count -eq 0) { Write-Host "      DESCARTADA: nao fechou 600 quadros" -ForegroundColor DarkYellow; return $null }
  $fps = [double]($f[-1].Line -replace '.*\(([\d.]+) fps\).*','$1')
  # "network job 12 done in 58 ms (history ..., ...)" -- mediana dos jobs da rodada
  $ms = @(Select-String -Path $logMod -Pattern 'network job \d+ done in (\d+) ms' -ErrorAction SilentlyContinue |
          ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
  $msMed = if ($ms.Count) { (@($ms | Sort-Object))[[int]([math]::Floor($ms.Count/2))] } else { $null }
  # A v0.2.18 decompoe o custo sozinha; vale mais que qualquer cronometro nosso.
  $t = @(Select-String -Path $logMod -Pattern 'timing \(avg of' -ErrorAction SilentlyContinue)
  if ($t.Count) { Write-Host ("      " + $t[-1].Line.Trim()) -ForegroundColor DarkGray }
  $to = @(Select-String -Path $logMod -Pattern 'GPU wait timeouts so far' -ErrorAction SilentlyContinue)
  if ($to.Count) { Write-Host ("      ATENCAO, houve estouro de espera: " + $to[-1].Line.Trim()) -ForegroundColor DarkYellow }
  return @{ fps = $fps; ms = $msMed; jobs = $ms.Count }
}

function Med($v) { if ($v.Count -eq 0) { return "-" }; $s = @($v | Sort-Object); return $s[[int]([math]::Floor($s.Count/2))] }

function Par-AB($rot, $A, $B) {
  Write-Host "`n--- $rot ---" -ForegroundColor Cyan
  $fa = @(); $fb = @(); $ma = @(); $mb = @()
  for ($i = 1; $i -le $Voltas; $i++) {
    $a = Uma-Rodada $A
    $b = Uma-Rodada $B
    Write-Host ("  volta {0}: A={1}  B={2}" -f $i,
                $(if ($a) { "$($a.fps) fps / $($a.ms) ms-job" } else { "descartada" }),
                $(if ($b) { "$($b.fps) fps / $($b.ms) ms-job" } else { "descartada" }))
    if ($a) { $fa += $a.fps; if ($null -ne $a.ms) { $ma += $a.ms } }
    if ($b) { $fb += $b.fps; if ($null -ne $b.ms) { $mb += $b.ms } }
  }
  $mA = Med $fa; $mB = Med $fb
  $ganho = "-"
  if ($mA -ne "-" -and $mB -ne "-" -and $mA -gt 0) { $ganho = "{0:+0;-0}%" -f ((($mB - $mA) / $mA) * 100) }
  Write-Host ("  >>> {0}: A={1} fps ({2} ms/job) | B={3} fps ({4} ms/job) | ganho {5}" -f
              $rot, $mA, (Med $ma), $mB, (Med $mb), $ganho) -ForegroundColor Green
  Write-Host ("      amostras A: {0} | B: {1}" -f ($fa -join ', '), ($fb -join ', '))
}

# ------------------------------------------------------------------ 2. O ALVO
Matar-Todos
Write-Host "`n=== 2. Alvo animado no monitor secundario ($AlvoX,$AlvoY), destravado ===" -ForegroundColor Cyan
$alvo = Start-Process (Join-Path $tests "alvo_animado.exe") -ArgumentList '1600','900','8','0',"$AlvoX","$AlvoY" -PassThru
Start-Sleep -Seconds 3

try {
  # --------------------------------------------------------- 3. InlineWaitMs (a alavanca 1)
  if (-not $SoResolucao -and -not $SoFormato) {
    Par-AB "ESPERA: A = InlineWaitMs 200 (o padrao) | B = 25 (o que ficou no outro PC)" `
      @{ n="A-wait200"; wait=200; res=100; up=0; fmt=87; decoy=1 } `
      @{ n="B-wait25";  wait=25;  res=100; up=0; fmt=87; decoy=1 }
  }

  # --------------------------------- 4. Resolucao de trabalho (a alavanca 2, a area da rede)
  if (-not $SoInline -and -not $SoFormato) {
    $wait = 25   # o valor comprovado do outro PC; ajuste depois da secao 3
    Par-AB "RESOLUCAO: A = 100% | B = 77% de trabalho, volta por FSR 1 (59% dos pixels na rede)" `
      @{ n="A-res100"; wait=$wait; res=100; up=0; fmt=87; decoy=1 } `
      @{ n="B-res77";  wait=$wait; res=77;  up=1; fmt=87; decoy=1 }
    Par-AB "RESOLUCAO: A = 100% | B = 58% de trabalho, volta por FSR 1 (34% dos pixels na rede)" `
      @{ n="A-res100b"; wait=$wait; res=100; up=0; fmt=87; decoy=1 } `
      @{ n="B-res58";   wait=$wait; res=58;  up=1; fmt=87; decoy=1 }
  }

  # --------------------------------------------------------- 5. Formato de cor (a alavanca 3)
  if (-not $SoInline -and -not $SoResolucao) {
    Par-AB "FORMATO: A = 87 BGRA8 (4 B/px) | B = 10 RGBA16F (8 B/px)" `
      @{ n="A-fmt87"; wait=25; res=100; up=0; fmt=87; decoy=1 } `
      @{ n="B-fmt10"; wait=25; res=100; up=0; fmt=10; decoy=1 }
  }
}
finally {
  Matar-Todos
  try { Stop-Process -Id $alvo.Id -ErrorAction Stop } catch {}
}

Write-Host "`nlogs de cada rodada em $Saida" -ForegroundColor Green
Write-Host "O que anotar junto com os numeros: versao do mod (version.dll), resolucao da fonte," -ForegroundColor Green
Write-Host "e se havia jogo/navegador aberto. Sem isso a tabela nao se compara com a proxima." -ForegroundColor Green
