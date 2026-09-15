# =====================================================================================
#  setup-dlss5.ps1  -  monta a cadeia inteira ao lado do DLSS5ForAll.exe
#
#  Roda com:  botao direito > "Executar com o PowerShell"
#         ou: powershell -ExecutionPolicy Bypass -File .\setup-dlss5.ps1
#
#  O que ele faz (nessa ordem):
#   1. ReShade (build com add-ons) instalado no DLSS5ForAll.exe  -> dxgi.dll
#   2. ReShade.fxh / ReShadeUI.fxh / DrawText.fxh (headers que os shaders incluem)
#   3. DLSS5-Feeder (jlrouzies-fr)  -> dlss5-feed.addon64 + DLSS5_Feed.fx
#   4. LumeniteFX (umar-afzaal)     -> lumenite_*.fx + include\ + blue noise
#   5. nvngx_dlss.dll (runtime DLSS normal) copiado do driver NVIDIA / de um jogo
#   6. renodx-dlss5.addon64 + nvngx_dlssnr.dll  -> ESSES VOCE TRAZ (RHI ou Discord do RenoDX)
#   7. ReShade.ini + preset FafaDLSS5.ini ja configurados (provider 3 = LumeniteFX Kernel)
#
#  Pode rodar de novo quantas vezes quiser: ele so refaz o que estiver faltando.
# =====================================================================================
$ErrorActionPreference = 'Stop'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

$Root     = Split-Path -Parent $MyInvocation.MyCommand.Path
$Exe      = Join-Path $Root 'DLSS5ForAll.exe'
$Shaders  = Join-Path $Root 'reshade-shaders\Shaders'
$Textures = Join-Path $Root 'reshade-shaders\Textures'
$Tmp      = Join-Path $env:TEMP 'fafadlss5-setup'
$UA       = @{ 'User-Agent' = 'DLSS5ForAll-setup' }

function Step($t) { Write-Host ""; Write-Host "== $t" -ForegroundColor Cyan }
function Ok($t)   { Write-Host "   [OK] $t" -ForegroundColor Green }
function Warn($t) { Write-Host "   [!!] $t" -ForegroundColor Yellow }
function Info($t) { Write-Host "   $t" }
function Get-File($url, $dest) {
    Info "baixando $url"
    Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing -Headers $UA
    if (-not (Test-Path $dest) -or (Get-Item $dest).Length -lt 100) { throw "download vazio: $url" }
}
function Set-IniValue($path, $section, $key, $value) {
    # $section = '' -> chave global no topo do arquivo (formato dos presets do ReShade)
    $lines = @(); if (Test-Path $path) { $lines = @(Get-Content $path -Encoding UTF8) }
    $out = New-Object System.Collections.Generic.List[string]
    $cur = ''; $keySet = $false
    foreach ($l in $lines) {
        if ($l -match '^\s*\[(.*)\]\s*$') {
            if (($cur -eq $section) -and -not $keySet) { $out.Add("$key=$value"); $keySet = $true }
            $cur = $Matches[1]
            $out.Add($l); continue
        }
        if (($cur -eq $section) -and ($l -match "^\s*$([regex]::Escape($key))\s*=")) {
            if (-not $keySet) { $out.Add("$key=$value"); $keySet = $true }
            continue
        }
        $out.Add($l)
    }
    if (($cur -eq $section) -and -not $keySet) { $out.Add("$key=$value"); $keySet = $true }
    if (-not $keySet) {
        if ($section -eq '') { $out.Insert(0, "$key=$value") }
        else { if ($out.Count -gt 0) { $out.Add('') }; $out.Add("[$section]"); $out.Add("$key=$value") }
    }
    [IO.File]::WriteAllLines($path, $out, (New-Object System.Text.UTF8Encoding($false)))
}

Write-Host "DLSS5 For All - setup da cadeia DLSS 5" -ForegroundColor Magenta
Write-Host "Pasta: $Root"
if (-not (Test-Path $Exe)) { throw "DLSS5ForAll.exe nao esta nessa pasta. Coloca o script na mesma pasta do exe." }
New-Item -ItemType Directory -Force -Path $Shaders, $Textures, $Tmp | Out-Null

# ---------------------------------------------------------------- 0. VC++ runtime (o feeder precisa)
Step 'Visual C++ Runtime 2015-2022 x64 (o dlss5-feed.addon64 depende dele)'
if (Test-Path "$env:SystemRoot\System32\vcruntime140_1.dll") { Ok 'ja instalado' }
else {
    $vc = Join-Path $Tmp 'vc_redist.x64.exe'
    try {
        Get-File 'https://aka.ms/vs/17/release/vc_redist.x64.exe' $vc
        Start-Process -FilePath $vc -ArgumentList '/install /quiet /norestart' -Wait
        Ok 'instalado'
    } catch { Warn "nao consegui instalar automaticamente ($_). Instala manualmente: https://aka.ms/vs/17/release/vc_redist.x64.exe" }
}

# ---------------------------------------------------------------- 1. ReShade
Step 'ReShade com suporte a add-ons'
$dxgi = Join-Path $Root 'dxgi.dll'
if (Test-Path $dxgi) { Ok 'dxgi.dll ja esta ao lado do exe' }
else {
    $setup = $null
    try {
        $html = (Invoke-WebRequest 'https://reshade.me' -UseBasicParsing -Headers $UA).Content
        if ($html -match 'ReShade_Setup_(\d+\.\d+\.\d+)_Addon\.exe') {
            $ver = $Matches[1]
            $setup = Join-Path $Tmp "ReShade_Setup_${ver}_Addon.exe"
            if (-not (Test-Path $setup)) { Get-File "https://reshade.me/downloads/ReShade_Setup_${ver}_Addon.exe" $setup }
            Ok "instalador ReShade $ver (Addon) baixado"
        }
    } catch { Warn "nao consegui baixar o instalador sozinho: $_" }

    if ($setup -and (Test-Path $setup)) {
        Info 'tentando instalacao silenciosa...'
        try { Start-Process -FilePath $setup -ArgumentList "`"$Exe`" --api dxgi --headless" -Wait -WindowStyle Hidden } catch {}
        if (-not (Test-Path $dxgi)) {
            Warn 'o modo silencioso nao gerou o dxgi.dll - abrindo o instalador normal.'
            Write-Host ''
            Write-Host '   NO INSTALADOR:' -ForegroundColor Yellow
            Write-Host '     1) "Browse" e escolhe DLSS5ForAll.exe (essa pasta)'
            Write-Host '     2) API: Direct3D 10/11/12'
            Write-Host '     3) se aparecer "Enable loading of add-ons", MARCA'
            Write-Host '     4) pacotes de efeitos: pode desmarcar tudo (o script traz o que precisa)'
            Start-Process -FilePath $setup -ArgumentList "`"$Exe`""
        }
    } else {
        Warn 'baixa manualmente em https://reshade.me  ->  "ReShade_Setup_x.x.x_Addon.exe", aponta pro DLSS5ForAll.exe, API Direct3D 10/11/12.'
        Start-Process 'https://reshade.me'
    }
    while (-not (Test-Path $dxgi)) { Read-Host '   Quando o instalador terminar (dxgi.dll ao lado do exe), aperta ENTER' | Out-Null }
    Ok 'dxgi.dll presente'
}
# arquivos que o instalador pode ter deixado e que so atrapalham aqui
foreach ($junk in @('ReShade.log')) { Remove-Item (Join-Path $Root $junk) -Force -ErrorAction SilentlyContinue }

# ---------------------------------------------------------------- 2. headers do ReShade
Step 'ReShade.fxh / ReShadeUI.fxh / DrawText.fxh'
$need = @('ReShade.fxh', 'ReShadeUI.fxh', 'DrawText.fxh') | Where-Object { -not (Test-Path (Join-Path $Shaders $_)) }
if ($need.Count -eq 0) { Ok 'ja presentes' }
else {
    $zip = Join-Path $Tmp 'reshade-shaders-slim.zip'
    Get-File 'https://codeload.github.com/crosire/reshade-shaders/zip/refs/heads/slim' $zip
    $x = Join-Path $Tmp 'rs-slim'; Remove-Item $x -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive -Path $zip -DestinationPath $x -Force
    foreach ($f in $need) { Copy-Item (Join-Path $x "reshade-shaders-slim\Shaders\$f") $Shaders -Force }
    Ok ("copiados: " + ($need -join ', '))
}

# ---------------------------------------------------------------- 3. DLSS5-Feeder
Step 'DLSS5-Feeder (jlrouzies-fr) - o unico repo legitimo do feeder'
$feedAddon = Join-Path $Root 'dlss5-feed.addon64'
$feedFx    = Join-Path $Shaders 'DLSS5_Feed.fx'
if ((Test-Path $feedAddon) -and (Test-Path $feedFx)) { Ok 'ja presentes' }
else {
    # O FEEDER DEIXOU DE PUBLICAR OS ARQUIVOS SOLTOS.
    # Ate a v0.12 dava para baixar releases/latest/download/dlss5-feed.addon64 (e o .fx)
    # direto. Da v0.13 em diante a release traz UM ZIP (DLSS5-Feeder-<versao>.zip) e as duas
    # URLs antigas devolvem 404. Como este script roda com ErrorActionPreference=Stop, o
    # 404 matava o setup AQUI: quem instalasse ficava com o ReShade instalado, sem feeder,
    # sem LumeniteFX e sem ReShade.ini - a cadeia pela metade, sem explicacao.
    #
    # POR QUE PELA API E NAO POR UMA URL NOVA FIXA: o nome do zip carrega a versao, entao
    # uma URL fixa quebra na proxima release igualzinho. A API devolve os assets da release
    # atual e a escolha e por PADRAO DE NOME. Dentro do zip, mesma ideia: procura-se pelos
    # NOMES na arvore extraida, e nao por caminho fixo (hoje o .addon64 esta na raiz e o
    # .fx em reshade-shaders\Shaders\, mas isso e empacotamento do autor, nao contrato).
    $pegou = $false
    try {
        $rel = Invoke-RestMethod 'https://api.github.com/repos/jlrouzies-fr/DLSS5-Feeder/releases/latest' -Headers $UA
        $asset = $rel.assets | Where-Object { $_.name -like '*DLSS5-Feeder*.zip' } | Select-Object -First 1
        if (-not $asset) { throw "a release $($rel.tag_name) nao traz zip do Feeder" }
        Info "release $($rel.tag_name) -> $($asset.name)"
        $zipFeed = Join-Path $Tmp $asset.name
        Get-File $asset.browser_download_url $zipFeed
        $xFeed = Join-Path $Tmp 'feeder'; Remove-Item $xFeed -Recurse -Force -ErrorAction SilentlyContinue
        Expand-Archive -Path $zipFeed -DestinationPath $xFeed -Force
        foreach ($par in @(@{n='dlss5-feed.addon64'; d=$feedAddon}, @{n='DLSS5_Feed.fx'; d=$feedFx})) {
            $achado = Get-ChildItem $xFeed -Recurse -File -Filter $par.n | Select-Object -First 1
            if (-not $achado) { throw "o zip $($asset.name) nao tem $($par.n) - o empacotamento mudou" }
            Copy-Item $achado.FullName $par.d -Force
        }
        $pegou = $true
    } catch {
        Warn "nao deu pelo zip da release ($($_.Exception.Message)). Tentando as URLs soltas antigas..."
        # FALLBACK de proposito: se um dia o autor voltar a publicar os arquivos soltos,
        # este caminho passa a funcionar de novo sem ninguem precisar mexer aqui.
        try {
            if (-not (Test-Path $feedAddon)) { Get-File 'https://github.com/jlrouzies-fr/DLSS5-Feeder/releases/latest/download/dlss5-feed.addon64' $feedAddon }
            if (-not (Test-Path $feedFx))    { Get-File 'https://github.com/jlrouzies-fr/DLSS5-Feeder/releases/latest/download/DLSS5_Feed.fx' $feedFx }
            $pegou = $true
        } catch { Warn "as URLs soltas tambem falharam ($($_.Exception.Message))" }
    }

    # VALIDA O QUE CHEGOU. Um zip que mudou de estrutura nao pode passar em silencio
    # deixando arquivo faltando: o sintoma seria o ReShade subir sem a tecnica DLSS5_Feed e
    # o neural nunca ligar, que ninguem liga a um download.
    $ruim = @()
    if (-not (Test-Path $feedAddon) -or (Get-Item $feedAddon).Length -lt 50KB) { $ruim += 'dlss5-feed.addon64' }
    if (-not (Test-Path $feedFx)    -or (Get-Item $feedFx).Length    -lt 5KB)  { $ruim += 'DLSS5_Feed.fx' }
    if ($ruim.Count -gt 0) {
        $pegou = $false
        foreach ($r in $ruim) { Remove-Item (@{'dlss5-feed.addon64'=$feedAddon;'DLSS5_Feed.fx'=$feedFx})[$r] -Force -ErrorAction SilentlyContinue }
    }
    if ($pegou) { Ok 'dlss5-feed.addon64 + DLSS5_Feed.fx' }
    else {
        Warn 'NAO consegui trazer o DLSS5-Feeder. Sem ele a cadeia nao sobe (o DLSS5_Feed e quem entrega o par quadro+vetores ao DLSS).'
        Write-Host ''
        Write-Host '   NA MAO (leva um minuto):' -ForegroundColor Yellow
        Write-Host '     1) abre https://github.com/jlrouzies-fr/DLSS5-Feeder/releases'
        Write-Host '     2) baixa o DLSS5-Feeder-<versao>.zip da release mais nova'
        Write-Host "     3) de dentro dele, copia  dlss5-feed.addon64  para  $Root"
        Write-Host "        e  reshade-shaders\Shaders\DLSS5_Feed.fx  para  $Shaders"
        Write-Host '     4) roda este script de novo (ele so refaz o que estiver faltando)'
        Write-Host ''
    }
}

# ---------------------------------------------------------------- 4. LumeniteFX
Step 'LumeniteFX (motion vectors por optical flow - provider 3, Kernel)'
if (Test-Path (Join-Path $Shaders 'lumenite_Kernel.fx')) { Ok 'ja presente' }
else {
    $zip = Join-Path $Tmp 'lumenite.zip'
    Get-File 'https://codeload.github.com/umar-afzaal/LumeniteFX/zip/refs/heads/mainline' $zip
    $x = Join-Path $Tmp 'lum'; Remove-Item $x -Recurse -Force -ErrorAction SilentlyContinue
    Expand-Archive -Path $zip -DestinationPath $x -Force
    $src = Join-Path $x 'LumeniteFX-mainline'
    Copy-Item (Join-Path $src 'Shaders\*.fx') $Shaders -Force
    New-Item -ItemType Directory -Force -Path (Join-Path $Shaders 'include') | Out-Null
    Copy-Item (Join-Path $src 'Shaders\include\*') (Join-Path $Shaders 'include') -Force
    Copy-Item (Join-Path $src 'Textures\*') $Textures -Force
    Ok 'lumenite_*.fx + include\ + lumenite_bluenoise256.png'
}

# ---------------------------------------------------------------- 5. nvngx_dlss.dll
Step 'nvngx_dlss.dll (runtime DLSS comum - o feeder abre uma sessao NGX com ele)'
$dlss = Join-Path $Root 'nvngx_dlss.dll'
if (Test-Path $dlss) { Ok 'ja presente' }
else {
    $cands = @()
    $roots = @("$env:SystemRoot\System32\DriverStore\FileRepository")
    $roots += @("${env:ProgramFiles(x86)}\Steam\steamapps\common", "$env:ProgramFiles\Steam\steamapps\common")
    foreach ($d in (Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Used -gt 0 })) {
        $roots += @("$($d.Root)SteamLibrary\steamapps\common", "$($d.Root)Steam\steamapps\common", "$($d.Root)Games", "$($d.Root)Program Files\Epic Games")
    }
    foreach ($r in $roots) {
        if (Test-Path $r) {
            Info "procurando em $r ..."
            $cands += Get-ChildItem -Path $r -Filter 'nvngx_dlss.dll' -Recurse -Depth 4 -ErrorAction SilentlyContinue -File
        }
    }
    if ($cands.Count -gt 0) {
        $best = $cands | Sort-Object { try { [version]$_.VersionInfo.FileVersion.Replace(',', '.') } catch { [version]'0.0' } } -Descending | Select-Object -First 1
        Copy-Item $best.FullName $dlss -Force
        Ok "copiado de $($best.FullName) (v$($best.VersionInfo.FileVersion))"
    } else {
        Warn 'nao achei nenhum nvngx_dlss.dll. O feeder tenta usar a copia do driver; se reclamar, pega um com o DLSS Swapper (github.com/beeradmoore/dlss-swapper) e poe ao lado do exe.'
    }
}

# ---------------------------------------------------------------- 6. add-on neural + modelo (vazado - voce traz)
Step 'renodx-dlss5.addon64 + nvngx_dlssnr.dll'
$nr1 = Join-Path $Root 'renodx-dlss5.addon64'
$nr2 = Join-Path $Root 'nvngx_dlssnr.dll'
if ((Test-Path $nr1) -and (Test-Path $nr2)) { Ok 'os dois ja estao ao lado do exe' }
else {
    Warn 'Esses dois arquivos sao o DLSS 5 vazado (codigo da NVIDIA nao lancado). Este script NAO baixa eles.'
    Write-Host ''
    Write-Host '   Onde a comunidade pega:' -ForegroundColor Yellow
    Write-Host '     - Instalador RHI (RankFTW):  https://github.com/RankFTW/RHI/releases'
    Write-Host '       (aponta pra ESSA pasta / DLSS5ForAll.exe; ele deposita renodx-dlss5.addon64 + nvngx_dlssnr.dll)'
    Write-Host '     - Ou o canal #DLSS5 no Discord do RenoDX (mensagens fixadas)'
    Write-Host '   O DLSS5-Feeder documenta que a versao v4.55 do add-on e a combinacao "sabidamente boa" com ele.'
    Write-Host '   Confere o SHA256 no canal antes de rodar qualquer coisa: semana de leak = temporada de malware.'
    Write-Host ''
    try { Start-Process 'https://github.com/RankFTW/RHI/releases' } catch {}
    while (-not ((Test-Path $nr1) -and (Test-Path $nr2))) {
        $r = Read-Host '   Quando os dois arquivos estiverem ao lado do exe aperta ENTER (ou digita "pular" pra configurar sem eles)'
        if ($r -eq 'pular') { Warn 'seguindo sem o add-on neural: o Feeder vai rodar so DLAA (sem DLSS 5) ate voce colocar os arquivos.'; break }
    }
    if ((Test-Path $nr1) -and (Test-Path $nr2)) { Ok 'add-on neural + modelo presentes' }
}

# ---------------------------------------------------------------- 7. ReShade.ini + preset
Step 'ReShade.ini + preset FafaDLSS5.ini'
$ini    = Join-Path $Root 'ReShade.ini'
$preset = Join-Path $Root 'FafaDLSS5.ini'
Set-IniValue $ini 'GENERAL' 'EffectSearchPaths'   '.\reshade-shaders\Shaders\**'
Set-IniValue $ini 'GENERAL' 'TextureSearchPaths'  '.\reshade-shaders\Textures\**'
Set-IniValue $ini 'GENERAL' 'IntermediateCachePath' '.\reshade-cache'   # relativo: nunca a pasta de quem empacotou
Set-IniValue $ini 'GENERAL' 'PresetPath'          '.\FafaDLSS5.ini'
Set-IniValue $ini 'GENERAL' 'PerformanceMode'     '0'
Set-IniValue $ini 'GENERAL' 'NoReloadOnInit'      '0'
# depth do app: z=0.5 plano, D24S8, nao invertido, nao logaritmico
Set-IniValue $ini 'GENERAL' 'PreprocessorDefinitions' 'DLSS5_MV_PROVIDER=3,RESHADE_DEPTH_INPUT_IS_REVERSED=0,RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN=0,RESHADE_DEPTH_INPUT_IS_LOGARITHMIC=0,RESHADE_DEPTH_LINEARIZATION_FAR_PLANE=1000.0'
Set-IniValue $ini 'ADDON'   'AddonPath'           '.\'
Set-IniValue $ini 'INPUT'   'KeyOverlay'          '36,0,0,0'      # Home (End abre o painel do app)
Set-IniValue $ini 'OVERLAY' 'TutorialProgress'    '4'             # pula o tutorial do ReShade
Set-IniValue $ini 'OVERLAY' 'ShowFPS'             '1'

# IMAGE_SPACE ficou FORA de proposito. O preset em uso (FafaDLSS5.ini) tem so o
# DLSS5_MV_PROVIDER=3, e o instalador nao pode mudar o comportamento do Kernel por baixo
# de quem ja esta rodando. IMAGE_SPACE=1 tira o passe de normais (que aqui e inutil: o
# depth do app e plano) mas TAMBEM troca, dentro do ATrousFilter, um gate de depth inerte
# por um gate de fluxo agressivo (zera vizinhos que discordam mais de ~0,3 px). Isso e
# decisao de quem cuida do lumenite_Kernel.fx, nao do instalador.
# Ver docs\borrao-e-nitidez.md secao 2.
Set-IniValue $preset '' 'PreprocessorDefinitions' 'DLSS5_MV_PROVIDER=3'
# --- ordem das tecnicas.
# Kernel -> Feed -> Sharpen. O Sharpen esta LIGADO por medicao (docs\atualizacao-cadeia.md):
# com ele a imagem final fica melhor que a cadeia sem NR nenhum (grade de 1 px 71,1% contra
# 69,5%, laplaciano do texto 48,1% contra 40,2%, erro 3,96 -> 3,45). Ele tem de ficar ABAIXO
# do DLSS5_Feed -- acima, o DLSS trata o realce como detalhe real e o acumula no historico
# (docs\nitidez.md secao 1).
#
# QUANDO O VME SUBIR: NAO ha preset a trocar, e nao se cria um segundo. O
# DLSS5_MV_Hardware.fx publica no MESMO Kernel::tFlow que o Lumenite_Kernel, entao os dois
# nunca podem rodar juntos -- e quem garante isso e o ADD-ON, em reshade_present: ele liga
# DLSS5_MV_Hardware e desliga Lumenite_Kernel quando o SRV do estimador chega, e faz o
# inverso quando some. O preset em repouso e o de hoje; o Hardware so precisa estar no
# TechniqueSorting (abaixo), na posicao certa, para o add-on te-lo onde ligar.
Set-IniValue $preset '' 'Techniques'        'Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_Feed@DLSS5_Feed.fx,DLSS5_Sharpen@DLSS5_Sharpen.fx'
Set-IniValue $preset '' 'TechniqueSorting'  'Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_MV_Hardware@DLSS5_MV_Hardware.fx,DLSS5_Feed@DLSS5_Feed.fx,DLSS5_Sharpen@DLSS5_Sharpen.fx,DLSS5_Grade@DLSS5_Grade.fx'

# nitidez: os padroes do shader, gravados para quem ligar/desligar pela overlay achar tudo pronto
Set-IniValue $preset 'DLSS5_Sharpen.fx' 'Sharpness'   '0.350000'
Set-IniValue $preset 'DLSS5_Sharpen.fx' 'HaloProtect' '0.800000'
Set-IniValue $preset 'DLSS5_Sharpen.fx' 'Denoise'     '0.025000'

# --- validacao do DLSS5_Feed. Por que cada um, em docs\borrao-e-nitidez.md secao 4.
#   STATIC_BIAS=0     o vies inflava em 15% o erro do vetor do provider, e a hipotese
#                     "nao se moveu" ganhava empates. Com motion vector de 1/8 de
#                     resolucao isso zerava vetores justo em textura fina -- e o codigo
#                     zera o vetor SEM avisar o DLSS (o teste estatico nao entra na
#                     mascara de desconfianca), entao o historico fica preso exatamente
#                     onde mora a nitidez. Com 0 o empate volta para o provider.
#   VALIDATE_LUMA=1   devolve a mascara "este pixel mudou de aparencia, use o quadro
#                     atual" -- o antidoto direto do ghosting. Estava desligada, e como o
#                     teste de depth e inerte aqui (linha abaixo), a mascara tinha uma
#                     unica fonte viva.
#   VALIDATE_DEPTH=0  o app desenha o depth PLANO (z=0.5 na grade de quads do main.cpp),
#                     entao dp==dc sempre e o teste da 0 em todo pixel. Mantê-lo ligado
#                     nao mudava resultado e custava 18 amostras de textura por pixel.
Set-IniValue $preset 'DLSS5_Feed.fx' 'STATIC_BIAS'     '0.000000'
Set-IniValue $preset 'DLSS5_Feed.fx' 'VALIDATE_LUMA'   '1'
Set-IniValue $preset 'DLSS5_Feed.fx' 'VALIDATE_DEPTH'  '0'
#   STATIC_DISTRUST=0.5  uniform novo (o P1). Quando a hipotese estatica vence, o vetor era
#                     zerado SEM avisar o DLSS -- ele reprojetava o historico da propria
#                     posicao do pixel e o prendia ali, justo na textura fina. Agora
#                     distrust recebe bad.w * STATIC_DISTRUST * confianca_do_provider.
#                     0 = comportamento antigo. Requer o DLSS5_Feed.fx com o P1.
#   MV_CONSISTENCY=1.1  com MV de bloco, vetor incoerente entre quadros e o sintoma classico
#                     de "casou o bloco errado"; 1.4 era folgado demais para esse caso.
Set-IniValue $preset 'DLSS5_Feed.fx' 'STATIC_DISTRUST' '0.500000'
Set-IniValue $preset 'DLSS5_Feed.fx' 'MV_CONSISTENCY'  '1.100000'

# --- Neural Rendering nos padroes do add-on.
# Os quatro sliders estavam no MAXIMO (2/2/2/2). NRLocalStructure e NRSkinStructure no
# topo fazem a rede reescrever a trama local da imagem -- sobre uma recaptura, ela
# sintetiza detalhe a partir de um sinal que ja perdeu frequencia, e o resultado le como
# plastico. NRSkinStructure=-1 e o padrao NATIVO: nao sobrescrever. O mesmo conjunto ja
# tinha sido diagnosticado em docs\brilho-witcher3.md como causa de "claro demais".
Set-IniValue $ini 'RenoDX.DLSS5' 'NRIntensity'      '1'
Set-IniValue $ini 'RenoDX.DLSS5' 'NRLocalTone'      '1'
Set-IniValue $ini 'RenoDX.DLSS5' 'NRLocalStructure' '1'
Set-IniValue $ini 'RenoDX.DLSS5' 'NRSkinStructure'  '-1'
Set-IniValue $ini 'RenoDX.DLSS5' 'NRStyle'          '0'

# --- preset do DLSS no dlss5-feed.cfg: 5 = "E (legacy CNN)".
# ATENCAO ao valor: e o enum do NGX (Default=0, A=1, B=2, C=3, D=4, E=5, F=6, J=10,
# K=11), NAO o indice do combo da overlay. Com um valor fora da tabela o add-on loga
# "DLSS render preset hint: N (?)" e pede um preset que o runtime 310.x pode nem ter.
# E = o preset que prende menos o historico contra o quadro atual, que e o que se quer
# com motion vectors sinteticos. Ver docs\borrao-e-nitidez.md secao 6.1.
# So mexe se o arquivo ja existir: cria-lo aqui com uma chave so seria pior que nada.
$cfgFeed = Join-Path $Root 'dlss5-feed.cfg'
if (Test-Path $cfgFeed) { Set-IniValue $cfgFeed '' 'preset' '5' }
Ok 'gravados'

# ---------------------------------------------------------------- resumo
Step 'Pronto. Checklist do que esta na pasta:'
$check = [ordered]@{
    'dxgi.dll (ReShade)'                        = (Test-Path $dxgi)
    'dlss5-feed.addon64 (Feeder)'               = (Test-Path $feedAddon)
    'reshade-shaders\Shaders\DLSS5_Feed.fx'     = (Test-Path $feedFx)
    'reshade-shaders\Shaders\lumenite_Kernel.fx'= (Test-Path (Join-Path $Shaders 'lumenite_Kernel.fx'))
    'reshade-shaders\Shaders\ReShade.fxh'       = (Test-Path (Join-Path $Shaders 'ReShade.fxh'))
    'dlss5-bridge.addon64 (ponte do painel)'    = (Test-Path (Join-Path $Root 'dlss5-bridge.addon64'))
    'reshade-shaders\Shaders\DLSS5_Grade.fx'    = (Test-Path (Join-Path $Shaders 'DLSS5_Grade.fx'))
    'reshade-shaders\Shaders\DLSS5_Sharpen.fx'  = (Test-Path (Join-Path $Shaders 'DLSS5_Sharpen.fx'))
    'reshade-shaders\Textures\dlss5_grade_luts.png' = (Test-Path (Join-Path $Textures 'dlss5_grade_luts.png'))
    'ofertas.ini (tela do grupo)'               = (Test-Path (Join-Path $Root 'ofertas.ini'))
    'nvngx_dlss.dll'                            = (Test-Path $dlss)
    'renodx-dlss5.addon64 (neural)'             = (Test-Path $nr1)
    'nvngx_dlssnr.dll (modelo DLSS 5)'          = (Test-Path $nr2)
    'ReShade.ini / FafaDLSS5.ini'               = ((Test-Path $ini) -and (Test-Path $preset))
}
foreach ($k in $check.Keys) { if ($check[$k]) { Ok $k } else { Warn "$k  FALTANDO" } }
Write-Host ''
Write-Host 'Agora: abre DLSS5ForAll.exe, escolhe a captura, INICIAR. Na janela:' -ForegroundColor Magenta
Write-Host '  Home  -> overlay do ReShade (End abre o painel do app). Aba Home: "LUMENITE: Kernel 2.0" ligado ACIMA de "DLSS 5 Feed" ligado.'
Write-Host '  Aba Add-ons -> "DLSS 5 Feed": status/log. Aba "DLSS 5 Neural Rendering" (do add-on): liga o NR, F6 alterna.'
Write-Host '  dlss5forall.log (app) e dlss5-feed.log (feeder) ficam nessa pasta se algo der errado.'
Write-Host ''
Read-Host 'ENTER pra fechar' | Out-Null
