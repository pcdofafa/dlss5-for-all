# =====================================================================================
#  gerar-instalador-completo.ps1  -  do fonte ao DLSS5ForAll-<versao>-completo-setup.exe.
#
#  Uso:  powershell -ExecutionPolicy Bypass -File .\gerar-instalador-completo.ps1
#        ...  -PermitirBuildVelho     (SO PARA TESTE: ver empacotar.ps1, item 0)
#        ...  -Versao 2026.09.10      (padrao: a data de hoje, igual ao empacotar.ps1)
#        ...  -Sfx "outro.exe"        (padrao: "dlss5 for all setup.exe" ao lado)
#
#  QUAL A DIFERENCA PARA O gerar-instalador.ps1
#    O gerar-instalador.ps1 monta o instalador ENXUTO: so o que e nosso, e a cadeia
#    (ReShade, shaders, dxgi.dll) e BAIXADA no fim da instalacao. Este monta o COMPLETO:
#    mesma staging, mais o auto-extraivel "dlss5 for all setup.exe" embutido, que o
#    instalador aplica por cima da pasta logo depois de copiar - sem baixar nada e sem
#    pedir um clique. Os dois compartilham empacotar.ps1 e CREDITOS.md de proposito: a
#    lista de arquivos e os creditos moram num lugar so.
#
#  POR QUE O SFX ENTRA COMO ARQUIVO E NAO E GERADO AQUI
#    Ele e montado a mao no WinRAR (e um retrato da pasta de trabalho inteira, com
#    documentacao, fontes e os componentes que o pacote publicavel nao redistribui). Este
#    script nao tenta recria-lo: so confere que existe, que e mesmo um auto-extraivel e que
#    nao esta velho demais em relacao ao DLSS5ForAll.exe - ver o item 2.
#
#  SE FOR MEXER NO PASSO DE EXTRACAO, LEIA O CABECALHO DO .iss
#    O switch e "-s", nao "-s2". O -s2 esconde so a janela de abertura do WinRAR e deixa a
#    caixa de confirmar substituicao aparecer no meio da instalacao - que era exatamente o
#    "Yes to all" que este desenho existe para eliminar. E qualquer teste de extracao tem
#    de ser feito com a pasta JA POPULADA: com pasta vazia nao ha conflito e tudo parece
#    funcionar.
# =====================================================================================
param([string]$Versao = (Get-Date -Format 'yyyy.MM.dd'),
      [string]$Sfx,
      [switch]$PermitirBuildVelho)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

function Passo($t) { Write-Host ""; Write-Host "== $t" -ForegroundColor Cyan }

# ---------------------------------------------------------------- 0. o compilador
Passo 'Inno Setup (ISCC.exe)'
$iscc = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",       # instalacao por usuario (winget)
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "ISCC.exe nao encontrado. Instala com:  winget install --id JRSoftware.InnoSetup" }
Write-Host "   $iscc"

# O carimbo -TESTE tem de valer aqui TAMBEM, e nao so dentro do empacotar.ps1: e o nome
# deste arquivo que alguem vai ver na hora de publicar.
if ($PermitirBuildVelho -and $Versao -notmatch 'TESTE') { $Versao = "$Versao-TESTE" }

# ---------------------------------------------------------------- 1. staging
Passo "empacotar.ps1 -SomenteStaging (a lista de arquivos mora la, nao aqui)"
$trabalho = Join-Path $env:TEMP 'DLSS5ForAll-instalador-completo'
$staging  = Join-Path $trabalho 'staging'
Remove-Item $trabalho -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $trabalho | Out-Null
$parms = @{ Versao = $Versao; SomenteStaging = $true; Staging = $staging }
if ($PermitirBuildVelho) { $parms.PermitirBuildVelho = $true }
& (Join-Path $Root 'empacotar.ps1') @parms
if (-not (Test-Path (Join-Path $staging 'DLSS5ForAll.exe'))) { throw "a staging nao tem o DLSS5ForAll.exe - o empacotar.ps1 falhou" }

# ---------------------------------------------------------------- 2. o auto-extraivel
#
# TRES CHECAGENS, e cada uma cobre um jeito diferente de publicar 300 MB de lixo:
#
#   (a) existe? - o nome tem espacos e um "for" no meio; renomear por engano e facil, e o
#       ISCC so reclamaria la na frente, depois de comprimir a staging inteira.
#   (b) e mesmo um auto-extraivel? - um .rar comum tem o mesmo conteudo e o mesmo tamanho,
#       mas nao roda: o instalador embutiria 300 MB de arquivo que o Exec nao consegue
#       executar, e o unico sintoma seria a caixa de erro no fim da instalacao. O teste e o
#       par MZ (assinatura de executavel do Windows) nos dois primeiros bytes.
#   (c) esta mais novo que o exe do app? - o SFX e um retrato manual da pasta. Se alguem
#       recompilou o DLSS5ForAll.exe e esqueceu de refazer o retrato, o instalador copia o
#       exe novo pela staging e o auto-extraivel PASSA POR CIMA com o velho - uma regressao
#       silenciosa, porque a instalacao termina dizendo "sucesso". Aviso e nao erro: as
#       vezes o retrato e proposital (so a documentacao mudou), e quem esta rodando sabe.
Passo 'auto-extraivel (dlss5 for all setup.exe)'
if (-not $Sfx) { $Sfx = Join-Path $Root 'dlss5 for all setup.exe' }
if (-not (Test-Path $Sfx)) { throw "auto-extraivel nao encontrado: $Sfx`nGere ele no WinRAR (SFX da pasta) ou passe -Sfx <caminho>." }
$sfxItem = Get-Item $Sfx
$mz = [byte[]]::new(2)
$fs = [IO.File]::OpenRead($sfxItem.FullName)
try { [void]$fs.Read($mz, 0, 2) } finally { $fs.Close() }
if ($mz[0] -ne 0x4D -or $mz[1] -ne 0x5A) { throw "'$($sfxItem.Name)' nao e um executavel (faltou o MZ no comeco). Um .rar comum nao serve: tem de ser SFX." }
$exeApp = Get-Item (Join-Path $Root 'DLSS5ForAll.exe')
if ($sfxItem.LastWriteTime -lt $exeApp.LastWriteTime) {
    Write-Host "[!!] o auto-extraivel ($($sfxItem.LastWriteTime)) e mais velho que o DLSS5ForAll.exe ($($exeApp.LastWriteTime))." -ForegroundColor Yellow
    Write-Host "[!!] ele roda DEPOIS da copia e sobrescreve a pasta: o exe novo seria trocado pelo velho. Refaz o SFX." -ForegroundColor Yellow
}
Write-Host "   $($sfxItem.FullName) ($([math]::Round($sfxItem.Length / 1MB, 1)) MB)"

# ---------------------------------------------------------------- 2b. quanto ocupa depois
#
# O Inno so sabe somar o que ELE copia: a staging (poucos MB) e o SFX ainda comprimido.
# Anunciaria "requer 320 MB" para uma instalacao que ocupa quase 1 GB - e quem estivesse no
# limite do disco so descobriria no meio da extracao, com a pasta pela metade. Daqui sai o
# ExtraDiskSpaceRequired do .iss.
#
# A medida boa vem de listar o proprio SFX (o 7z le RAR): e o tamanho DESCOMPACTADO real do
# que vai ser gravado. Sem o 7z na maquina, o palpite e 3x o tamanho do SFX - grosseiro,
# mas errar para MAIS aqui so custa um aviso de espaco a mais, enquanto errar para menos
# custa uma instalacao pela metade.
Passo 'espaco necessario depois de extrair'
$sete = @("$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
$espaco = 0
if ($sete) {
    # "7z l -ba" -> uma linha por arquivo; a 4a coluna e o tamanho descompactado. As linhas
    # de pasta vem com tamanho vazio e caem fora pelo -match '^\d+$'.
    foreach ($linha in (& $sete l -ba -- $sfxItem.FullName)) {
        $col = ($linha -split '\s+') | Where-Object { $_ -ne '' }
        if ($col.Count -ge 4 -and $col[3] -match '^\d+$') { $espaco += [int64]$col[3] }
    }
}
if ($espaco -le 0) {
    $espaco = $sfxItem.Length * 3
    Write-Host "   7z nao encontrado - estimando por 3x o SFX" -ForegroundColor DarkGray
}
# 10% de folga: os ~7900 arquivos pagam o arredondamento do cluster do disco, e a staging
# entra por cima disso.
$espaco = [int64]($espaco * 1.1)
Write-Host "   $([math]::Round($espaco / 1MB, 0)) MB"

# ---------------------------------------------------------------- 3. creditos em texto puro
# A pagina InfoBefore do Inno mostra o arquivo como esta: os **negritos** e os `backticks`
# do markdown apareceriam crus. Convertido daqui, e nao mantido a mao, para nao existir uma
# segunda versao dos creditos para alguem esquecer de atualizar.
Passo 'CREDITOS.md -> CREDITOS.txt'
$cred = Get-Content (Join-Path $Root 'CREDITOS.md') -Encoding UTF8 -Raw
$cred = $cred -replace '\*\*', '' -replace '`', '' -replace '(?m)^#\s*', ''
$credTxt = Join-Path $trabalho 'CREDITOS.txt'
# UTF-8 COM BOM: sem o BOM o Inno le o arquivo como ANSI e os acentos viram lixo na tela.
[IO.File]::WriteAllText($credTxt, $cred, (New-Object System.Text.UTF8Encoding $true))

# ---------------------------------------------------------------- 4. compilar
Passo 'ISCC instalador-completo.iss'
# VersionInfoVersion do Windows so aceita numeros: "2026.09.04-TESTE" vira "2026.9.4.0".
$n = @(($Versao -replace '[^0-9.]', '') -split '\.' | Where-Object { $_ -ne '' } | ForEach-Object { [int]$_ })
while ($n.Count -lt 4) { $n += 0 }
$versaoNum = ($n[0..3] -join '.')
$nome = "DLSS5ForAll-$Versao-completo-setup"
$argsIscc = @(
    "/DStaging=$staging",
    "/DSfx=$($sfxItem.FullName)",
    "/DVersao=$Versao",
    "/DVersaoNum=$versaoNum",
    "/DEspacoExtra=$espaco",
    "/DSaidaDir=$Root",
    "/DSaidaNome=$nome",
    "/DCreditos=$credTxt",
    (Join-Path $Root 'instalador-completo.iss')
)
& $iscc @argsIscc
if ($LASTEXITCODE -ne 0) { throw "ISCC falhou (codigo $LASTEXITCODE)" }

$exe = Join-Path $Root "$nome.exe"
$mb  = [math]::Round((Get-Item $exe).Length / 1MB, 2)
Write-Host ""
Write-Host "ok -> $exe ($mb MB)" -ForegroundColor Green
Write-Host "     pergunta o idioma (portugues/ingles), instala em '$env:ProgramFiles\DLSS for All'" -ForegroundColor DarkGray
Write-Host "     pedindo administrador, cria os atalhos na area de trabalho e no Menu Iniciar," -ForegroundColor DarkGray
Write-Host "     e aplica o auto-extraivel sozinho na MESMA pasta escolhida, sem nenhum clique." -ForegroundColor DarkGray
if ($PermitirBuildVelho) {
    Write-Host "ATENCAO: gerado com -PermitirBuildVelho. E um instalador DE TESTE (o exe pode ser mais velho que o codigo). NAO PUBLICAR." -ForegroundColor Yellow
}
