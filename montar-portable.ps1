# =====================================================================================
#  montar-portable.ps1  -  monta a versao PORTABLE do DLSS5 For All.
#
#  Resultado (padrao: ..\DLSS5ForAll-AMD, ao lado desta pasta):
#     DLSS5ForAll.exe      launcher (portable-launcher\) - tela do grupo com os botoes
#                          NVIDIA/AMD; abre bin\ ou bin-amd\DLSS5ForAll.exe e sai
#     LEIA-ME.txt, README.txt, ofertas.ini
#     bin\                 o programa inteiro: exe, ReShade (dxgi.dll), add-ons, DLLs da
#                          NVIDIA/RenoDX, shaders, cache, configuracoes, scripts de setup
#     bin-amd\             MODULO AMD (beta): o mesmo exe + ReShade + Bridge, o Feeder na
#                          variante AMD (feeder-amd\dlss5-feed-amd.addon64, portador FSR),
#                          amd_fidelityfx_dx12.dll (FidelityFX 1.1.4, MIT), perfil de
#                          ReShade SEM a secao [RenoDX.DLSS5], cfg com as chaves amd_*,
#                          LEIA-ME-AMD/README-AMD e o preparar-mod-amd.cmd. O que vai
#                          la sai de perfil-amd\ (o que e proprio do modulo) e desta
#                          pasta (o que e comum). NAO vao: renodx-dlss5.addon64,
#                          nvngx_dlss.dll, nvngx_dlssnr.dll (a pessoa poe a copia dela e
#                          roda o setup do mod DLSS-NR-on-AMD dentro de bin-amd).
#
#  SO COPIA. Nunca move nem apaga nada da pasta de origem. Os arquivos de terceiros
#  (nvngx_dlssnr.dll, renodx-dlss5.addon64, nvngx_dlss.dll, dxgi.dll, dlss5-feed.addon64,
#  ReShade*.fxh) vao byte a byte, sem alteracao.
#
#  FASE4 (22/09/2026): os `lumenite_*` NAO VAO MAIS - nem os .fx, nem os include\*.fxh, nem a
#  textura de blue noise, nem o reshade-cache (que os leva compilados). O LumeniteFX e do
#  umar-afzaal sob AGNYA License, que proibe redistribuicao. Quem instala os recebe do
#  `setup-dlss5.ps1` (passo 4), que os baixa do repositorio do autor.
#
#  A UNICA linha alterada na copia: IntermediateCachePath do ReShade.ini, que na origem
#  aponta para um caminho absoluto e no portable vira .\reshade-cache.
#
#  Uso:  powershell -ExecutionPolicy Bypass -File .\montar-portable.ps1 [-Destino <pasta>]
#        -SobrescreverConfig  regrava tambem os .ini/.cfg no destino (por padrao, se o
#                             portable ja existe, as configuracoes de la sao preservadas).
# =====================================================================================
param([string]$Destino, [switch]$SobrescreverConfig)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Destino) { $Destino = Join-Path (Split-Path -Parent $Root) 'DLSS5ForAll-AMD' }   # a copia em desenvolvimento nasce AO LADO da pasta que funciona, nunca em cima dela
$Bin = Join-Path $Destino 'bin'

$launcher = Join-Path $Root 'portable-launcher\DLSS5ForAll.exe'
if (-not (Test-Path $launcher)) { throw "launcher nao compilado: rode bash portable-launcher/build-launcher.sh antes" }

New-Item -ItemType Directory -Force -Path $Destino, $Bin, "$Bin\reshade-shaders\Shaders\include", "$Bin\reshade-shaders\Textures", "$Bin\reshade-cache" | Out-Null

# 1. raiz do portable: launcher + tutorial (LEIA-ME.txt em portugues, README.txt em ingles).
#    O README.md tecnico, a LICENSE e o CREDITOS.md vao para bin\ (item 2).
Copy-Item $launcher (Join-Path $Destino 'DLSS5ForAll.exe') -Force
# WebView2Loader.dll: a ponte do launcher ate o WebView2 do Windows (a vitrine de ofertas na coluna
# da esquerda da tela; ver vitrine.h). Fica AO LADO do launcher; sem ela a tela abre sem a coluna.
# A licenca do SDK vai para bin\ junto do CREDITOS.md.
$loader = Join-Path $Root 'portable-launcher\WebView2Loader.dll'
if (Test-Path $loader) { Copy-Item $loader (Join-Path $Destino 'WebView2Loader.dll') -Force }
else { Write-Host "[!] portable-launcher\WebView2Loader.dll nao encontrada (a vitrine ficara desligada no pacote)" -ForegroundColor Yellow }
$licWv = Join-Path $Root 'portable-launcher\webview2\LICENSE-WebView2.txt'
if (Test-Path $licWv) { Copy-Item $licWv (Join-Path $Bin 'LICENSE-WebView2.txt') -Force }
# O launcher mostra a tela do grupo por conta propria e le o ofertas.ini da PROPRIA pasta (a raiz).
Copy-Item (Join-Path $Root 'ofertas.ini') $Destino -Force
foreach ($f in 'LEIA-ME.txt','README.txt') { Copy-Item (Join-Path $Root "portable-launcher\$f") $Destino -Force }
# versao.txt na raiz: a versao de versao.h. E por ele que o atualizador (atualizacao.h) reconhece
# um pacote DLSS5 For All antes de trocar qualquer arquivo, e o publicar-versao.ps1 o poe no zip.
$versaoH = Get-Content (Join-Path $Root 'versao.h') -Raw
if ($versaoH -notmatch 'DLSS5_VERSAO\s+L"([0-9.]+)"') { throw "versao.h sem #define DLSS5_VERSAO" }
[System.IO.File]::WriteAllText((Join-Path $Destino 'versao.txt'), $Matches[1] + "`r`n", (New-Object System.Text.UTF8Encoding $false))

# 2. bin\ - programa e componentes (sempre atualizados)
$programa = @(
    'DLSS5ForAll.exe', 'dlss5-bridge.addon64',
    # terceiros, sem alteracao
    'dxgi.dll', 'dlss5-feed.addon64', 'nvngx_dlss.dll', 'nvngx_dlssnr.dll', 'renodx-dlss5.addon64',
    # material do usuario
    'ofertas.ini',
    'preset-01-nucleo-afinado.ini', 'preset-02-grade-neutra.ini', 'preset-03-grade-cinema.ini',
    'preset-04-brilho-contido.ini', 'preset-05-nitidez.ini',
    'setup-dlss5.ps1', 'setup-dlss5.auto.ps1', 'gerar-luts.ps1',
    'README.md', 'LEIA-ME.md', 'LICENSE', 'CREDITOS.md', 'CREDITS.md'
)
foreach ($f in $programa) {
    $src = Join-Path $Root $f
    if (Test-Path $src) { Copy-Item $src $Bin -Force } else { Write-Host "[!] faltou na origem (pulado): $f" -ForegroundColor Yellow }
}
Copy-Item (Join-Path $Root 'instalador\configurar-cadeia.cmd') $Bin -Force

# FASE4 (22/09/2026): O FIDELITYFX AGORA E DO bin\ TAMBEM, E A LICENCA VAI COLADA NELE.
# O `amd_fidelityfx_dx12.dll` entrou no caminho NVIDIA quando o Frame Generation passou a usar
# a interpolacao da AMD (`fg_ffx.h`: FSR 3.1, SDK 1.1.4) - ate aqui este script so o copiava
# para o `bin-amd\`, e o `bin\` do portable saia sem ele (o app achava a DLL so porque alguem
# a tinha posto la a mao). A licenca e MIT e a MIT tem UMA condicao: o aviso de copyright e o
# texto acompanham a copia. Por isso os dois saem no MESMO foreach - quem tirar um tira o
# outro, e nao da para esquecer so a licenca.
foreach ($f in 'amd_fidelityfx_dx12.dll', 'LICENSE-FidelityFX.txt') {
    $src = Join-Path (Join-Path $Root 'perfil-amd') $f
    if (Test-Path $src) { Copy-Item $src $Bin -Force } else { Write-Host "[!] faltou em perfil-amd\ (pulado): $f" -ForegroundColor Yellow }
}
if ((Test-Path (Join-Path $Bin 'amd_fidelityfx_dx12.dll')) -and -not (Test-Path (Join-Path $Bin 'LICENSE-FidelityFX.txt'))) {
    throw "bin\amd_fidelityfx_dx12.dll sem bin\LICENSE-FidelityFX.txt - a MIT do FidelityFX exige a licenca junto"
}

# FASE4 (22/09/2026): O LUMENITEFX NAO VIAJA EM PACOTE NENHUM.
#
# Os oito `lumenite_*.fx`, os quatro `include\lumenite_*.fxh` e a textura
# `lumenite_bluenoise256.png` sao do LumeniteFX (umar-afzaal) e o cabecalho deles diz
# `License : AGNYA License (https://github.com/nvb-uy/AGNYA-License)` - que PROIBE
# redistribuicao (FONTES-ABERTAS-2209 §7.1.1). Ate aqui este script copiava a pasta de
# shaders inteira, entao eles entravam em todo portable e em todo zip publicado, embora o
# CREDITOS.md ja dissesse o contrario ("componentes que o setup-dlss5.ps1 baixa").
# Quem instala continua com eles: o `setup-dlss5.ps1`/`.auto.ps1`, passo 4, baixa o
# LumeniteFX de codeload.github.com/umar-afzaal/LumeniteFX na maquina da pessoa.
# O padrao pega o nome com qualquer caixa e qualquer extensao - a proxima versao deles pode
# trazer um `.fxh` ou um `.png` novo e nao vamos lembrar de acrescentar nome por nome.
$NaoRedistribuir = '^lumenite[_.-]'
# shaders e texturas (sem os .bak de desenvolvimento), cache dos shaders compilados
Get-ChildItem (Join-Path $Root 'reshade-shaders\Shaders') -File | Where-Object { $_.Name -notmatch '\.bak' -and $_.Name -notmatch $NaoRedistribuir } |
    ForEach-Object { Copy-Item $_.FullName "$Bin\reshade-shaders\Shaders" -Force }
Get-ChildItem (Join-Path $Root 'reshade-shaders\Shaders\include') -File | Where-Object { $_.Name -notmatch '\.bak' -and $_.Name -notmatch $NaoRedistribuir } |
    ForEach-Object { Copy-Item $_.FullName "$Bin\reshade-shaders\Shaders\include" -Force }
Get-ChildItem (Join-Path $Root 'reshade-shaders\Textures') -File | Where-Object { $_.Name -notmatch '\.bak' -and $_.Name -notmatch $NaoRedistribuir } |
    ForEach-Object { Copy-Item $_.FullName "$Bin\reshade-shaders\Textures" -Force }
# FASE4 (22/09/2026): o reshade-cache do bin\ PASSA A SAIR VAZIO, como o do bin-amd\ ja sai.
# Os arquivos do cache tem nome de hash (`ReShade-<crc>.i/.cso`) e nao da para saber, olhando,
# qual deles e o LumeniteFX COMPILADO - e um binario compilado a partir de um fonte que a
# AGNYA proibe redistribuir e continua sendo redistribuicao dele. Nao havendo como filtrar por
# nome, o cache inteiro fica de fora. Preco: o ReShade recompila os efeitos na primeira
# abertura (alguns segundos, uma vez). Alem disso o cache seria inutil mesmo: os
# `lumenite_*.fx` so chegam na maquina depois que o `setup-dlss5.ps1` os baixa.
# Para voltar atras basta descomentar a linha abaixo.
# Get-ChildItem (Join-Path $Root 'reshade-cache') -File | ForEach-Object { Copy-Item $_.FullName "$Bin\reshade-cache" -Force }

# 3. configuracoes: preservadas se o portable ja existir (a nao ser com -SobrescreverConfig)
function Copiar-Config($nome) {
    $dst = Join-Path $Bin $nome
    if ((Test-Path $dst) -and -not $SobrescreverConfig) { Write-Host "    mantido: bin\$nome"; return $false }
    Copy-Item (Join-Path $Root $nome) $dst -Force
    return $true
}
Copiar-Config 'FafaDLSS5.ini'   | Out-Null
Copiar-Config 'dlss5forall.ini' | Out-Null
Copiar-Config 'dlss5-feed.cfg'  | Out-Null
if (Copiar-Config 'ReShade.ini') {
    $ini = Get-Content (Join-Path $Bin 'ReShade.ini') -Encoding UTF8
    $ini = $ini | ForEach-Object { if ($_ -match '^IntermediateCachePath=') { 'IntermediateCachePath=.\reshade-cache' } else { $_ } }
    [System.IO.File]::WriteAllLines((Join-Path $Bin 'ReShade.ini'), $ini, (New-Object System.Text.UTF8Encoding $false))
    # so confere o que ESTE script acabou de escrever: um ReShade.ini preservado ja foi regravado
    # pelo proprio ReShade em uso, que grava o IntermediateCachePath absoluto da propria pasta
    # (e o regrava de novo se a pasta mudar de lugar) - isso nao e erro de montagem
    $sobrou = Get-Content (Join-Path $Bin 'ReShade.ini') | Where-Object { $_ -match '^(EffectSearchPaths|TextureSearchPaths|PresetPath|IntermediateCachePath)=.*[A-Za-z]:[\\/]' }
    if ($sobrou) { throw "ReShade.ini do portable ainda tem caminho absoluto: $sobrou" }
}

# =====================================================================================
# 4. bin-amd\ - modulo AMD (beta). Mesma politica: so copia, configs preservadas a nao
#    ser com -SobrescreverConfig. Nunca toca no que e da pessoa (nvngx_dlssnr.dll,
#    version.dll, dlssnr_on_amd.ini, dlssnr_on_amd_weights.bin, dlssnr_on_amd_setup.exe).
# =====================================================================================
Write-Host "bin-amd (modulo AMD, beta):"
$BinAmd = Join-Path $Destino 'bin-amd'
$Perfil = Join-Path $Root 'perfil-amd'
if (-not (Test-Path $Perfil)) { throw "perfil-amd\ nao existe ao lado deste script" }
New-Item -ItemType Directory -Force -Path $BinAmd, "$BinAmd\reshade-shaders\Shaders\include", "$BinAmd\reshade-shaders\Textures", "$BinAmd\reshade-cache" | Out-Null

# Copia que nao derruba o script: o exe em uso (app aberto) e o unico caso esperado.
function Copiar-Amd($src, $dstNome) {
    $dst = Join-Path $BinAmd $dstNome
    if (-not (Test-Path $src)) { Write-Host "[!] faltou na origem (pulado): $src" -ForegroundColor Yellow; return }
    try { Copy-Item $src $dst -Force }
    catch { Write-Host "[!] nao copiei bin-amd\$dstNome ($($_.Exception.Message.Trim())); feche o app e rode de novo" -ForegroundColor Yellow }
}

# 4a. comum com bin\ (da origem): app, ponte, ReShade, presets, licenca, creditos
foreach ($f in 'DLSS5ForAll.exe', 'dlss5-bridge.addon64', 'dxgi.dll',
               'preset-01-nucleo-afinado.ini', 'preset-02-grade-neutra.ini', 'preset-03-grade-cinema.ini',
               'preset-04-brilho-contido.ini', 'preset-05-nitidez.ini',
               'LICENSE', 'CREDITOS.md', 'LEIA-ME.md', 'CREDITS.md') {
    Copiar-Amd (Join-Path $Root $f) $f
}

# 4b. Feeder na variante AMD (compilado pelo build do feeder-amd\). Entra com o nome que o
#     ReShade.ini/OverlayCollapsed e o app conhecem: dlss5-feed.addon64. O oficial (NGX)
#     NAO serve de reserva: em AMD ele falha no Init e se desliga sozinho.
$feedAmd = Join-Path $Root 'feeder-amd\dlss5-feed-amd.addon64'
if (Test-Path $feedAmd) { Copiar-Amd $feedAmd 'dlss5-feed.addon64' }
else { Write-Host "[!] dlss5-feed-amd.addon64 ainda nao compilado: bin-amd fica sem Feeder" -ForegroundColor Yellow }

# 4c. proprio do modulo (perfil-amd\): FSR 3.1 da AMD (MIT) + licenca, textos, conferidor
foreach ($f in 'amd_fidelityfx_dx12.dll', 'LICENSE-FidelityFX.txt',
               'LEIA-ME-AMD.txt', 'README-AMD.txt', 'preparar-mod-amd.cmd', 'preparar-mod-amd.ps1') {
    Copiar-Amd (Join-Path $Perfil $f) $f
}

# 4d. shaders e texturas iguais aos do bin\; o reshade-cache fica VAZIO de proposito (o
#     ReShade recompila na primeira abertura; a cadeia e outra e o cache do NVIDIA nao vale)
# FASE4 (22/09/2026): mesmo filtro do bin\ - o LumeniteFX (AGNYA) nao e redistribuivel.
Get-ChildItem (Join-Path $Root 'reshade-shaders\Shaders') -File | Where-Object { $_.Name -notmatch '\.bak' -and $_.Name -notmatch $NaoRedistribuir } |
    ForEach-Object { Copy-Item $_.FullName "$BinAmd\reshade-shaders\Shaders" -Force }
Get-ChildItem (Join-Path $Root 'reshade-shaders\Shaders\include') -File | Where-Object { $_.Name -notmatch '\.bak' -and $_.Name -notmatch $NaoRedistribuir } |
    ForEach-Object { Copy-Item $_.FullName "$BinAmd\reshade-shaders\Shaders\include" -Force }
Get-ChildItem (Join-Path $Root 'reshade-shaders\Textures') -File | Where-Object { $_.Name -notmatch '\.bak' -and $_.Name -notmatch $NaoRedistribuir } |
    ForEach-Object { Copy-Item $_.FullName "$BinAmd\reshade-shaders\Textures" -Force }

# 4e. configuracoes (preservadas se ja existirem, como no bin\)
function Config-Amd($nome) {
    $dst = Join-Path $BinAmd $nome
    if ((Test-Path $dst) -and -not $SobrescreverConfig) { Write-Host "    mantido: bin-amd\$nome"; return $false }
    return $true
}
$utf8 = New-Object System.Text.UTF8Encoding $false
# FafaDLSS5.ini: o mesmo preset do bin\ (as tecnicas sao as mesmas; o que muda e quem consome o quadro)
if (Config-Amd 'FafaDLSS5.ini')   { Copy-Item (Join-Path $Root 'FafaDLSS5.ini') (Join-Path $BinAmd 'FafaDLSS5.ini') -Force }
# dlss5forall.ini minimo do perfil (fonte janela, sobrepor, convite ja visto)
if (Config-Amd 'dlss5forall.ini') { Copy-Item (Join-Path $Perfil 'dlss5forall.ini') (Join-Path $BinAmd 'dlss5forall.ini') -Force }
# dlss5-feed.cfg do perfil: as chaves do bin\ + amd_carrier/amd_color_fmt/amd_decoy_swapchain.
# Aviso (nao erro) se as chaves comuns divergirem do dlss5-feed.cfg da origem: alguem mudou
# um lado e esqueceu o outro.
if (Config-Amd 'dlss5-feed.cfg')  { Copy-Item (Join-Path $Perfil 'dlss5-feed.cfg') (Join-Path $BinAmd 'dlss5-feed.cfg') -Force }
# Comparacao chave a chave. Antes isto comparava LINHAS e filtrava o amd_ de um lado so, o
# que fazia as quatro chaves amd_* aparecerem como divergencia em toda montagem -- um alarme
# permanente, que e o mesmo que alarme nenhum.
#   - as amd_* saem dos DOIS lados: sao exclusivas do perfil AMD, por definicao;
#   - tres chaves tem valor DELIBERADAMENTE diferente no perfil AMD (motivo em $AmdProprio);
#   - o que sobra e dito em duas listas separadas, porque sao problemas diferentes: uma chave
#     que existe de um lado so costuma ser um cfg desatualizado; mesmo nome com valor
#     diferente costuma ser alguem que mexeu num lado e esqueceu o outro.
$AmdProprio = @{
    'warmup_rebuild' = 'a rede do mod e lenta: o arranque do perfil AMD segura mais quadros'
    'create_delay'   = 'idem'
    'work_sharpness' = '0.30 e a afinacao do portador FSR; 0.45 e a do caminho NGX'
}
# Chaves do NEURAL PROPRIO do feed (o caminho nr_spike=1, via NGX). No perfil AMD quem roda a
# rede e o mod DLSS-NR-on-AMD, dentro do dispatch do FSR, e o nr_spike fica 0 por obrigacao --
# entao estas nao valem nada la e o perfil AMD nao as ganha. Ficam de fora do aviso de "a
# origem tem chaves que o perfil nao tem", mas se alguma APARECER no perfil AMD o aviso sai,
# porque ai e engano de quem montou.
# Nao inclua aqui nr_spike, nr_passes, dred nem hud_restore*: essas o perfil AMD TEM, e o
# nr_spike=0 e justamente o que precisa continuar sendo vigiado.
$SoNvidia = @('nr_reduzida', 'nr_escala', 'nr_residual', 'nr_estilo', 'nr_intensidade',
              'nr_tom_local', 'nr_tom_global', 'nr_estrutura_local', 'nr_estrutura_pele',
              'nr_mascara_auto')
function ParesCfg($caminho) {
    $h = @{}
    Get-Content $caminho | Where-Object { $_ -match '^\s*([A-Za-z_]+)\s*=' -and $_ -notmatch '^\s*amd_' } |
        ForEach-Object { $p = $_.Trim() -split '=', 2; $h[$p[0].Trim()] = $p[1].Trim() }
    return $h
}
$parOrigem = ParesCfg (Join-Path $Root 'dlss5-feed.cfg')
$parPerfil = ParesCfg (Join-Path $Perfil 'dlss5-feed.cfg')
$soOrigem = @($parOrigem.Keys | Where-Object { -not $parPerfil.ContainsKey($_) -and $SoNvidia -notcontains $_ } | Sort-Object)
$soPerfil = @($parPerfil.Keys | Where-Object { -not $parOrigem.ContainsKey($_) } | Sort-Object)
$intrusas = @($parPerfil.Keys | Where-Object { $SoNvidia -contains $_ } | Sort-Object)
if ($intrusas.Count) { Write-Host "[!] dlss5-feed.cfg: perfil-amd tem chaves do neural proprio ($($intrusas -join ', ')), que so valem com nr_spike=1 -- no perfil AMD quem roda a rede e o mod" -ForegroundColor Yellow }
$valorDif = @($parOrigem.Keys | Where-Object { $parPerfil.ContainsKey($_) -and $parPerfil[$_] -ne $parOrigem[$_] -and -not $AmdProprio.ContainsKey($_) } | Sort-Object)
if ($soOrigem.Count) { Write-Host "[!] dlss5-feed.cfg: a origem tem chaves que perfil-amd nao tem ($($soOrigem -join ', ')) -- o perfil AMD ficou para tras" -ForegroundColor Yellow }
if ($soPerfil.Count) { Write-Host "[!] dlss5-feed.cfg: perfil-amd tem chaves que a origem nao tem ($($soPerfil -join ', ')) -- a origem ficou para tras" -ForegroundColor Yellow }
if ($valorDif.Count) { Write-Host "[!] dlss5-feed.cfg: mesmo nome, valor diferente ($(($valorDif | ForEach-Object { "$_`: $($parOrigem[$_]) na origem, $($parPerfil[$_]) no perfil" }) -join '; '))" -ForegroundColor Yellow }
foreach ($k in 'amd_carrier=1', 'amd_color_fmt=', 'amd_decoy_swapchain=') {
    if (-not (Select-String -Path (Join-Path $Perfil 'dlss5-feed.cfg') -Pattern "^$k" -Quiet)) { throw "perfil-amd\dlss5-feed.cfg sem a chave $k" }
}
# ofertas.ini: o da origem com mostrar=0 garantido (o launcher da raiz ja mostrou o convite)
if (Config-Amd 'ofertas.ini') {
    $of = Get-Content (Join-Path $Root 'ofertas.ini') -Encoding UTF8
    $of = $of | ForEach-Object { if ($_ -match '^mostrar=') { 'mostrar=0' } else { $_ } }
    [System.IO.File]::WriteAllLines((Join-Path $BinAmd 'ofertas.ini'), $of, $utf8)
}
# ReShade.ini: o da origem, SEM a secao [RenoDX.DLSS5] (em AMD o RenoDX nao entra na pasta e
# a secao so confundiria quem le), cache relativo. Tudo o mais (teclas, preset, caminhos,
# preprocessor) fica identico ao bin\ - por isso e derivado aqui e nao mantido a mao.
if (Config-Amd 'ReShade.ini') {
    $dentro = $false
    $ini = Get-Content (Join-Path $Root 'ReShade.ini') -Encoding UTF8 | ForEach-Object {
        if ($_ -match '^\[RenoDX\.DLSS5\]') { $dentro = $true; return }
        if ($dentro -and $_ -match '^\[') { $dentro = $false }
        if ($dentro) { return }
        if ($_ -match '^IntermediateCachePath=') { 'IntermediateCachePath=.\reshade-cache' } else { $_ }
    }
    # a secao removida deixava uma linha em branco dupla: normaliza
    $limpo = New-Object System.Collections.Generic.List[string]
    foreach ($l in $ini) { if ($l -eq '' -and $limpo.Count -gt 0 -and $limpo[$limpo.Count - 1] -eq '') { continue }; $limpo.Add($l) }
    [System.IO.File]::WriteAllLines((Join-Path $BinAmd 'ReShade.ini'), $limpo, $utf8)
    # (mesma regra do bin\: confere so o que acabou de ser escrito)
    $rsAmd = Get-Content (Join-Path $BinAmd 'ReShade.ini')
    if ($rsAmd | Where-Object { $_ -match '^\[RenoDX\.DLSS5\]' }) { throw "bin-amd\ReShade.ini ainda tem a secao [RenoDX.DLSS5]" }
    $sobrou = $rsAmd | Where-Object { $_ -match '^(EffectSearchPaths|TextureSearchPaths|PresetPath|IntermediateCachePath)=.*[A-Za-z]:[\\/]' }
    if ($sobrou) { throw "bin-amd\ReShade.ini ainda tem caminho absoluto: $sobrou" }
}
elseif (Get-Content (Join-Path $BinAmd 'ReShade.ini') | Where-Object { $_ -match '^\[RenoDX\.DLSS5\]' }) {
    Write-Host "[!] bin-amd\ReShade.ini (preservado) tem a secao [RenoDX.DLSS5]; rode com -SobrescreverConfig para refazer o perfil" -ForegroundColor Yellow
}

# 4f. o que NAO deve estar la (nao apaga: so avisa, pode ser coisa que a pessoa pos de proposito)
foreach ($f in 'renodx-dlss5.addon64', 'nvngx_dlss.dll') {
    if (Test-Path (Join-Path $BinAmd $f)) { Write-Host "[!] bin-amd\$f existe mas nao faz parte do modulo AMD (nao apaguei)" -ForegroundColor Yellow }
}
# 4g. estado do que e da pessoa, so para o resumo
$doMod = @('nvngx_dlssnr.dll', 'version.dll', 'dlssnr_on_amd.ini', 'dlssnr_on_amd_weights.bin') | Where-Object { -not (Test-Path (Join-Path $BinAmd $_)) }
if ($doMod) { Write-Host "    bin-amd sem os arquivos da pessoa/do mod (esperado ate ela rodar o setup do mod): $($doMod -join ', ')" }

Write-Host "ok -> $Destino"
Get-ChildItem $Destino -Recurse -File | Measure-Object -Property Length -Sum | ForEach-Object {
    "   $($_.Count) arquivos, $([math]::Round($_.Sum / 1MB, 1)) MB"
}
Get-ChildItem $BinAmd -Recurse -File | Measure-Object -Property Length -Sum | ForEach-Object {
    "   bin-amd: $($_.Count) arquivos, $([math]::Round($_.Sum / 1MB, 1)) MB"
}
