# =====================================================================================
#  montar-portable.ps1  -  monta a versao PORTABLE do DLSS5 For All.
#
#  Resultado (padrao: ..\DLSS5ForAll-AMD, ao lado desta pasta):
#     DLSS5ForAll.exe      launcher (portable-launcher\) - tela do grupo com um INICIAR;
#                          abre bin\DLSS5ForAll.exe (com --amd no modo AMD) e sai
#     LEIA-ME.txt, README.txt, ofertas.ini
#     bin\                 o programa inteiro: exe, ReShade (dxgi.dll), add-ons, DLLs da
#                          NVIDIA/RenoDX, shaders, cache, configuracoes, scripts de setup
#     bin\mod-amd\         v2.1 PASTA UNICA: so as instrucoes do mod AMD (LEIA-ME.txt,
#                          README.txt) e o preparar-mod-amd.cmd/.ps1. O mod DLSS-NR-on-AMD
#                          (terceiro) a pessoa baixa do autor e instala ali; o app o carrega
#                          so no modo AMD. Nao existe mais bin-amd\.
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
# 4. bin\mod-amd\ - v2.1 PASTA UNICA. Nao existe mais bin-amd\: o modo AMD e o mesmo
#    bin\DLSS5ForAll.exe aberto com --amd, e o mod DLSS-NR-on-AMD (de terceiro; a licenca
#    proibe redistribuir) e instalado PELA PESSOA nesta subpasta. Aqui so entram as
#    instrucoes (LEIA-ME.txt / README.txt) e o conferidor (preparar-mod-amd.cmd/.ps1), que
#    vivem em mod-amd\ na origem. NUNCA copia nem toca version.dll, dlssnr_on_amd*,
#    dlssnr_on_amd_setup.exe ou nvngx_dlssnr.dll que a pessoa ja tenha la.
#    As diferencas de configuracao do modo AMD (chaves amd_* do dlss5-feed.cfg, RenoDX fora
#    do ReShade) o proprio app aplica ao abrir no modo AMD e desfaz na volta (main.cpp,
#    AplicarChavesDoModo / AplicarMotorNasChaves) - por isso nao ha mais perfil separado.
# =====================================================================================
Write-Host "bin\mod-amd (instrucoes do mod AMD):"
$ModAmd = Join-Path $Bin 'mod-amd'
New-Item -ItemType Directory -Force -Path $ModAmd | Out-Null
foreach ($f in 'LEIA-ME.txt', 'README.txt', 'preparar-mod-amd.cmd', 'preparar-mod-amd.ps1') {
    $src = Join-Path (Join-Path $Root 'mod-amd') $f
    if (Test-Path $src) { Copy-Item $src $ModAmd -Force } else { Write-Host "[!] faltou em mod-amd\ (pulado): $f" -ForegroundColor Yellow }
}
# Trava: nada do mod pode ter saido da ORIGEM para o pacote (so acontece se alguem o pos em mod-amd\ da origem).
$doModNaOrigem = @(Get-ChildItem (Join-Path $Root 'mod-amd') -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^(version\.dll|dlssnr_on_amd.*|nvngx_.*)$' })
if ($doModNaOrigem.Count) { Write-Host "[!] mod-amd\ da ORIGEM tem arquivos do mod ($($doModNaOrigem.Name -join ', ')) - NAO foram copiados" -ForegroundColor Yellow }
$doMod = @('version.dll', 'dlssnr_on_amd.ini', 'dlssnr_on_amd_weights.bin') | Where-Object { -not (Test-Path (Join-Path $ModAmd $_)) }
if ($doMod) { Write-Host "    bin\mod-amd sem o mod (esperado ate a pessoa rodar o instalador dele la): $($doMod -join ', ')" }
# bin-amd\ de uma montagem antiga neste destino: so avisa (pode ter o mod da pessoa dentro).
if (Test-Path (Join-Path $Destino 'bin-amd')) {
    Write-Host "[!] $Destino\bin-amd existe (layout antigo). Nao apaguei: o app ainda carrega o mod de la se ele nao estiver em bin\mod-amd" -ForegroundColor Yellow
}

Write-Host "ok -> $Destino"
Get-ChildItem $Destino -Recurse -File | Measure-Object -Property Length -Sum | ForEach-Object {
    "   $($_.Count) arquivos, $([math]::Round($_.Sum / 1MB, 1)) MB"
}
