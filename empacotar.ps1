# Empacota o DLSS5 For All pra distribuicao: so o que e nosso + o que o setup precisa.
# NAO entra: nvngx_dlssnr.dll / renodx (vazados), nvngx_dlss.dll (NVIDIA), dxgi.dll e dlss5-feed (o setup baixa),
#            lumenite_* (o setup baixa), logs, .bak, docs\, addon\ (fonte), main.cpp e cia.
#            De marca\ entram SO os dois PNGs dos selos do modo comparativo (item 1b).
# Uso:  powershell -ExecutionPolicy Bypass -File .\empacotar.ps1        -> DLSS5ForAll-<versao>.zip ao lado
#
# -SomenteStaging       monta a pasta de distribuicao e PARA, sem zipar. Existe para o
#                       instalador (gerar-instalador.ps1 / instalador.iss) consumir a MESMA
#                       lista de arquivos daqui. Se o instalador tivesse a propria lista, as
#                       duas divergiriam no primeiro arquivo novo e alguem publicaria um
#                       pacote errado -- entao a lista mora so neste arquivo.
# -Staging <pasta>      onde montar (padrao: %TEMP%\DLSS5ForAll-pack). Quem chama escolhe a
#                       pasta e ja sabe o caminho: nao ha valor de retorno para adivinhar.
# -PermitirBuildVelho   SO PARA TESTE do instalador. Rebaixa as duas checagens de data (item 0)
#                       de erro para aviso, e em troca CARIMBA "-TESTE" na versao, para que um
#                       pacote gerado assim nunca saia com nome de release. Ver comentario no item 0.
param([string]$Versao = (Get-Date -Format 'yyyy.MM.dd'),
      [switch]$SomenteStaging,
      [string]$Staging,
      [switch]$PermitirBuildVelho)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Out  = if ($Staging) { $Staging } else { Join-Path $env:TEMP "DLSS5ForAll-pack" }
Remove-Item $Out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $Out, "$Out\reshade-shaders\Shaders", "$Out\reshade-shaders\Textures" | Out-Null

# 0. o exe tem que ser mais novo que o codigo (nunca empacotar build velho)
#
# -PermitirBuildVelho NAO desliga a checagem: ela continua rodando, so muda o desfecho.
# O motivo de existir e um so -- dar para testar a MECANICA do instalador enquanto o codigo
# esta sendo mexido por outra pessoa (o exe da pasta e, nesse instante, mais velho que o
# main.cpp de proposito). Como o perigo de publicar build velho continua o mesmo, o pacote
# gerado assim sai com "-TESTE" no nome: quem for publicar tropeca no nome antes de publicar.
function Test-Build($alvo, $fonte, $comoRefazer) {
    if ($fonte.LastWriteTime -le $alvo.LastWriteTime) { return }
    $msg = "$($alvo.Name) ($($alvo.LastWriteTime)) e mais velho que $($fonte.Name) ($($fonte.LastWriteTime)). Recompile antes ($comoRefazer)."
    if (-not $PermitirBuildVelho) { throw $msg }
    Write-Host "[!!] $msg" -ForegroundColor Yellow
    Write-Host "[!!] seguindo assim porque -PermitirBuildVelho foi pedido: PACOTE DE TESTE, NAO PUBLICAR." -ForegroundColor Yellow
}
$exe = Get-Item (Join-Path $Root 'DLSS5ForAll.exe')
$src = Get-ChildItem $Root -Include *.cpp,*.h -Recurse | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Test-Build $exe $src 'build-mingw.sh'
$addon = Get-Item (Join-Path $Root 'dlss5-bridge.addon64')
$asrc  = Get-Item (Join-Path $Root 'addon\dlss5_bridge_addon.cpp')
Test-Build $addon $asrc 'addon\build-addon.sh'
if ($PermitirBuildVelho -and $Versao -notmatch 'TESTE') { $Versao = "$Versao-TESTE" }

# 0b. o exe tem de ter O ICONE e o VERSIONINFO embutidos.
#
# POR QUE ESTA GUARDA EXISTE. O build-mingw.sh, de proposito, NAO morre quando os recursos
# nao entram: se faltar o marca\DLSS5ForAll.ico ou o windres nao estiver no PATH, ele
# imprime um AVISO e compila assim mesmo ("um exe sem icone e melhor que nenhum exe"). O
# aviso e uma linha no meio da saida do compilador e some. O resultado e um exe com o
# quadrado branco do Windows, que ninguem percebe ate alguem instalar - foi o que
# aconteceu em 04/09/2026, e o susto de "o icone sumiu" foi o que motivou isto aqui.
#
# O TESTE E DIRETO, nao heuristica: "existe recurso RT_GROUP_ICON (tipo 14) neste PE?".
# Nada de comparar bitmaps com o icone generico do Windows, que muda entre versoes do
# sistema e entre DPIs. O exe e aberto com LOAD_LIBRARY_AS_DATAFILE |
# LOAD_LIBRARY_AS_IMAGE_RESOURCE (0x22): so a tabela de recursos e mapeada, nenhum codigo
# do binario roda e nenhuma DLL dele e carregada.
#
# Enumera em vez de perguntar pelo id 1: o app.rc numera o icone como 1 (e explica la por
# que tem de ser o menor id), mas uma guarda que depende do id vira falso positivo no dia
# em que alguem renumerar. A pergunta certa e "tem ALGUM grupo de icone".
if (-not ([Management.Automation.PSTypeName]'DLSS5Recursos').Type) {
Add-Type -TypeDefinition @'
using System; using System.Runtime.InteropServices;
public class DLSS5Recursos {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr LoadLibraryExW(string f, IntPtr h, uint flags);
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool FreeLibrary(IntPtr h);
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool EnumResourceNamesW(IntPtr h, IntPtr type, EnumResNameProc cb, IntPtr param);
  delegate bool EnumResNameProc(IntPtr h, IntPtr type, IntPtr name, IntPtr param);
  // -1 = nem abriu o arquivo; 0 = abriu e nao ha grupo de icone; >0 = quantos grupos.
  public static int GruposDeIcone(string exe) {
    IntPtr h = LoadLibraryExW(exe, IntPtr.Zero, 0x22);
    if (h == IntPtr.Zero) return -1;
    int n = 0;
    // o delegate fica numa variavel: passado direto, o GC pode recolhe-lo no meio da chamada.
    EnumResNameProc cb = delegate(IntPtr a, IntPtr b, IntPtr c, IntPtr d) { n++; return true; };
    try { EnumResourceNamesW(h, new IntPtr(14), cb, IntPtr.Zero); } finally { FreeLibrary(h); }
    return n;
  }
}
'@
}
$comoRefazer = @"
Recompile com build-mingw.sh e confira o AVISO no comeco da saida. As duas causas que ele ja
     conhece: (1) marca\DLSS5ForAll.ico nao existe - gere com
     'powershell -ExecutionPolicy Bypass -File .\marca\gerar-icone-capivara.ps1'; (2) windres fora do PATH -
     rode o build pelo bash do msys2, que poe C:\msys64\mingw64\bin no PATH.
"@
$grupos = [DLSS5Recursos]::GruposDeIcone($exe.FullName)
if ($grupos -lt 0) { throw "nao consegui ler os recursos de $($exe.Name) - o arquivo esta corrompido ou em uso?" }
if ($grupos -eq 0) { throw "$($exe.Name) foi compilado SEM o app.rc: nao tem icone embutido, e sairia no pacote com o quadrado branco do Windows.`n     $comoRefazer" }

# O VERSIONINFO vem do MESMO app.rc, entao na pratica ele cai junto com o icone e esta
# checagem quase nunca dispara sozinha - eu a mantive assim mesmo porque custa uma linha,
# nao tem falso positivo (o valor e fixo no app.rc) e cobre o caso que a do icone nao cobre:
# alguem mexer no app.rc e quebrar so o bloco de versao. E a FileVersion impressa no fim do
# resumo e a unica forma de quem publica ver, sem abrir propriedades, QUAL build esta indo.
if ($exe.VersionInfo.ProductName.Trim() -ne 'DLSS5 For All') {
    throw "$($exe.Name) esta sem o VERSIONINFO do app.rc (ProductName='$($exe.VersionInfo.ProductName)', esperado 'DLSS5 For All').`n     $comoRefazer"
}

# 1. arquivos nossos
#
# MATERIAL OPCIONAL, nao mais controlado pelo painel: gerar-luts.ps1, preset-02..05,
# DLSS5_Grade.fx, DLSS5_Sharpen.fx e dlss5_grade_luts.png. Os chips de cor/LUT e de nitidez
# sairam do painel em 03/09/2026 (o preset em uso nao carregava nenhum dos dois shaders,
# entao nao faziam nada). Continuam no pacote de proposito: quem quiser grading liga a
# tecnica pelo overlay do ReShade. Tirar do zip e decisao do usuario, nao do script.
$raiz = @('DLSS5ForAll.exe','dlss5-bridge.addon64','README.md','LEIA-ME.md','LICENSE','CREDITOS.md','CREDITS.md','ofertas.ini',
          'setup-dlss5.ps1','setup-dlss5.auto.ps1','gerar-luts.ps1',
          'preset-01-nucleo-afinado.ini','preset-02-grade-neutra.ini','preset-03-grade-cinema.ini',
          'preset-04-brilho-contido.ini','preset-05-nitidez.ini')
foreach ($f in $raiz) { Copy-Item (Join-Path $Root $f) $Out -Force }
# DLSS5_Feed.fx ENTRA NESTA LISTA, e a ausencia dele era um buraco silencioso.
#
# Ele nao era copiado por nenhum passo automatico: as copias espalhadas pela maquina foram
# feitas a mao, em momentos diferentes.
#
# O NUMERO VEM COM O ESCOPO JUNTO, porque sem escopo ele nao quer dizer nada e ja circularam
# tres valores diferentes para a mesma pergunta. Contagem de 07/09/2026: 7 dentro desta pasta
# de projeto, 14 em pastas que uma pessoa abre (somando as outras copias do Desktop e os
# pacotes em Downloads), 47 se forem contados tambem os 33 caches sob AppData\Local, que
# ninguem edita. As 14 estao em QUATRO versoes distintas, a mais velha de 31/08.
#
# Isso funcionou por acaso enquanto ninguem editava o arquivo -- conferido no mesmo dia: as
# sete copias de 03/09, inclusive as dos quatro pacotes, sao IDENTICAS byte a byte ao que
# esta no repositorio. Nada se perdeu ate aqui, e nao se perdeu porque nao havia o que
# propagar. Mas o acaso acabou na primeira edicao que diverge, que e a de hoje, e o proximo
# empacotamento levaria a versao velha sem avisar.
#
# E o modo de falha e o pior tipo: o programa CARREGA o shader velho e nao reclama de
# nada. Quem testasse veria "compila, roda", com o codigo errado dentro.
foreach ($f in 'DLSS5_Feed.fx','DLSS5_Grade.fx','DLSS5_Sharpen.fx','DLSS5_MV_Hardware.fx','DLSS5_HUD.fx') { Copy-Item (Join-Path $Root "reshade-shaders\Shaders\$f") "$Out\reshade-shaders\Shaders" -Force }
Copy-Item (Join-Path $Root 'reshade-shaders\Textures\dlss5_grade_luts.png') "$Out\reshade-shaders\Textures" -Force
# I22 (19/09/2026): OS FILTROS DO CARD "FILTROS". Dois shaders nossos (Detalhes, Modo noturno) e seis do
# pacote SweetFX (CeeJayDK, MIT - a licenca vai junto), mais o SMAA do mesmo pacote, que o card sempre
# usou e nunca era copiado por nenhum passo automatico (mesmo buraco do DLSS5_Feed.fx acima).
foreach ($f in 'DLSS5_Detalhes.fx','DLSS5_Noturno.fx',
               'Vignette.fx','Sepia.fx','Monochrome.fx','Curves.fx','FilmGrain.fx','Vibrance.fx','LICENSE-SweetFX.txt',
               'SMAA.fx','SMAA.fxh','LICENSE-SweetFX-SMAA.txt') { Copy-Item (Join-Path $Root "reshade-shaders\Shaders\$f") "$Out\reshade-shaders\Shaders" -Force }
foreach ($f in 'AreaTex.png','SearchTex.png') { Copy-Item (Join-Path $Root "reshade-shaders\Textures\$f") "$Out\reshade-shaders\Textures" -Force }
Copy-Item (Join-Path $Root 'addon\include\reshade\LICENSE-ReShade.md') $Out -Force -ErrorAction SilentlyContinue

# 1b. marca\ -- DUAS imagens, e so elas.
#
# A pasta marca\ do repositorio inteira fica de fora (icone .ico, scripts de geracao, as
# artes em tamanho original do usuario). Estes dois PNGs sao a excecao porque o app os LE
# em tempo de execucao: sao os selos "ORIGINAL" e "DLSS 5 FOR ALL" do modo comparativo,
# carregados de <pasta do exe>\marca\ pelo dlss5-bridge (ver addon\rotulos_comparar.h).
# Faltando os arquivos o modo continua funcionando e entra um letreiro de reserva escrito
# com GDI -- feio de proposito, para a falta aparecer.
New-Item -ItemType Directory -Force -Path "$Out\marca" | Out-Null
foreach ($f in 'comparar-original.png','comparar-dlss5.png') {
    Copy-Item (Join-Path $Root "marca\$f") "$Out\marca" -Force
}

# 2. ReShade.ini limpo: nada de caminho absoluto de quem empacotou
$ini = Get-Content (Join-Path $Root 'ReShade.ini') -Encoding UTF8
# As chaves NR* deste arquivo (NRAutoMask, NREnableUpscaling, NRUICorrection, NRDepthMode,
# NRDiffuseWhiteNits, NRGlobalTone...) sao a AFINACAO DO DLSS que o autor deixou pronta, e
# e de proposito que elas viajem no pacote: quem instalar pela primeira vez recebe o filtro
# ja ajustado, sem ter de descobrir cada opcao no overlay do ReShade.
#
# O que NAO pode viajar e o lixo de sessao: o layout das janelas do overlay guarda a
# RESOLUCAO de quem empacotou (aqui, 3440x1440). Num monitor menor isso abre o overlay do
# ReShade maior que a tela, com metade dos botoes fora. Sao tres chaves, e zera-las faz o
# ReShade recriar o layout no tamanho de quem instalou.
$ini = $ini | ForEach-Object {
    if ($_ -match '^IntermediateCachePath=') { 'IntermediateCachePath=.\reshade-cache' }
    elseif ($_ -match '^(EffectSearchPaths|TextureSearchPaths|PresetPath)=.*:[\/]') { throw "ReShade.ini com caminho absoluto: $_" }
    elseif ($_ -match '^Docking=')          { 'Docking=' }
    elseif ($_ -match '^Window=')           { 'Window=' }
    elseif ($_ -match '^OverlayCollapsed=') { 'OverlayCollapsed=' }
    # NeuralUplift=1 SEMPRE, sem depender de como a ultima sessao terminou.
    # Aconteceu aqui: o pacote sairia com NeuralUplift=0 porque uma sessao anterior fechou
    # com o neural desligado e o RenoDX gravou isso no ini. Quem instalasse abriria o app,
    # veria "HABILITADO" e nao veria diferenca nenhuma na imagem - a pior primeira
    # impressao possivel para um app que existe por causa do Neural Rendering.
    elseif ($_ -match '^NeuralUplift=')     { 'NeuralUplift=1' }
    # OS QUATRO SLIDERS DO NEURAL, pelo mesmo motivo do NeuralUplift acima e com a mesma
    # causa: o RenoDX regrava essas chaves sozinho (AutoSave), entao elas saem do pacote
    # com o valor em que a ULTIMA SESSAO DE TESTES parou, e nao com o que foi afinado.
    #
    # Aconteceu de novo em 04/09/2026: o pacote saiu com 2/2/2/2 - os quatro no maximo -
    # que e exatamente a combinacao diagnosticada em docs\brilho-witcher3.md e comentada
    # no setup-dlss5.ps1 como causa de imagem "plastica": NRLocalStructure e
    # NRSkinStructure no topo fazem a rede reescrever a trama local, e sobre uma
    # RECAPTURA ela sintetiza detalhe a partir de um sinal que ja perdeu frequencia.
    # NRSkinStructure=-1 e o padrao NATIVO (nao sobrescrever), nao um valor baixo.
    #
    # Os valores aqui sao os mesmos que o setup-dlss5.ps1 grava - e tem de continuar
    # sendo. Se um dia divergirem, quem instalar recebe uma afinacao no pacote e outra
    # depois que o script da cadeia rodar, e ninguem vai entender por que a imagem mudou.
    elseif ($_ -match '^NRIntensity=')      { 'NRIntensity=1' }
    elseif ($_ -match '^NRLocalTone=')      { 'NRLocalTone=1' }
    elseif ($_ -match '^NRLocalStructure=') { 'NRLocalStructure=1' }
    elseif ($_ -match '^NRSkinStructure=')  { 'NRSkinStructure=-1' }
    elseif ($_ -match '^NRStyle=')          { 'NRStyle=0' }
    else { $_ }
}
# ------------------------------------------------------------------ AS DUAS GERACOES DO RENODX
# O bloco acima higieniza a geracao ANTIGA ([RenoDX.DLSS5], NRIntensity/NRStyle...). A bin\ que
# o usuario roda hoje e a geracao NOVA ([RENODX-DLSS], DirectNeuralRendering*), e ate 11/09/2026
# este script nao sabia que essas chaves existiam. O resultado era silencioso e ruim nas duas
# pontas: empacotando de amd\ o pacote saia SEM DirectNeuralRenderingHookPoint - e chave ausente
# vale Auto, que trava esta cadeia -, e empacotando da bin\ os throw da geracao antiga disparavam
# por falta de chaves que aquele ini legitimamente nao tem.
#
# Agora a geracao e decidida pelo que ESTA no arquivo, e os valores vem de preset-fabrica.ps1 -
# o mesmo que o publicar-versao.ps1 usa, para o pacote do site e o zip da atualizacao nunca
# sairem com afinacoes diferentes.
. (Join-Path $Root 'preset-fabrica.ps1')
$geracao = Geracao-DoReShadeIni $ini
if ($geracao -eq 'nenhuma') { throw "ReShade.ini nao tem nem [RENODX-DLSS] nem [RenoDX.DLSS5] - o pacote sairia sem preset do neural" }
$presetAlvo = if ($geracao -eq 'nova') { $PresetFabricaNovo } else { $PresetFabricaAntigo }
$r = Aplicar-Preset $ini $presetAlvo
$ini = $r.linhas
# Uma chave que NAO EXISTE no ini nao tem o que ser reescrito, e o pacote sairia sem ela - com
# o padrao do add-on no lugar do nosso. Acrescentar na secao certa e melhor que recusar: o
# empacotamento continua, e a linha em cinza diz o que foi acrescentado.
if ($r.faltando.Count) {
    $secao = if ($geracao -eq 'nova') { '[RENODX-DLSS-preset1]' } else { '[RenoDX.DLSS5]' }
    $novas = New-Object System.Collections.ArrayList
    $posto = $false
    foreach ($l in $ini) {
        [void]$novas.Add($l)
        if (-not $posto -and $l.Trim() -eq $secao) { foreach ($f in $r.faltando) { [void]$novas.Add($f) }; $posto = $true }
    }
    if (-not $posto) { [void]$novas.Add($secao); foreach ($f in $r.faltando) { [void]$novas.Add($f) } }
    $ini = $novas.ToArray()
    Write-Host "    ReShade.ini ($geracao): acrescentadas em $secao -> $($r.faltando -join ', ')" -ForegroundColor DarkGray
}
Write-Host "    ReShade.ini: geracao $geracao, preset de fabrica aplicado" -ForegroundColor DarkGray
# O neural LIGADO continua sendo condicao de publicacao, mas so onde a chave existe: na
# geracao nova quem liga e o DirectNeuralRenderingHookPoint=2, ja garantido acima.
if ($geracao -eq 'antiga' -and -not ($ini -match '^NeuralUplift=1$')) {
    throw "ReShade.ini sem NeuralUplift=1 - o pacote sairia com o neural desligado"
}
if ($ini -match '[A-Za-z]:\\') { throw "ReShade.ini ainda tem caminho absoluto: $($ini -match '[A-Za-z]:\\')" }
Salvar-IniSemBom "$Out\ReShade.ini" $ini
# BOM ou secao repetida derrubam o pacote aqui, e nao no computador de quem instalou: com BOM,
# o WritePrivateProfileString do painel nao acha a primeira secao e cria uma SEGUNDA no fim do
# arquivo - a leitura continua pegando a primeira, e o perfil que a pessoa escolhe nunca vale.
$probs = Conferir-IniDoReShade "$Out\ReShade.ini"
if ($probs.Count) { throw "ReShade.ini do pacote: $($probs -join ' | ')" }
Copy-Item (Join-Path $Root 'FafaDLSS5.ini') $Out -Force   # preset ativo (NRIntensity, ordem das tecnicas)

# 3. dlss5forall.ini de fabrica (nao a escolha de quem empacotou)
#
# A ESCOLHA DA FONTE nao viaja - cada pessoa tem o seu jogo e o seu monitor, e o seletor
# existe para isso. O que viaja e o que o autor AFINOU e vale para todo mundo:
#
#   fps_tela=1        o contador do ReShade ligado, como ele usa
#   [atalhos]         o mapa de teclas afinado aqui, com F7 no lugar que importa: um so
#                     atalho abre o overlay do ReShade E entrega o mouse pra ele, e aperta
#                     de novo fecha e devolve. Sem esta secao, quem instala cai na cascata
#                     automatica e pode receber outra tecla, e o guia da abertura promete
#                     uma coisa que nao esta la.
@"
[ultima]
fonte=janela
telacheia=0
vsync=0
hdr=0
sobrepor=1
guia_visto=0
escala=100
fps_tela=1

[ofertas]
ja_mostrado=0
[hdr]
tratar=0
[atalhos]
interativo=F7
"@ | Out-File "$Out\dlss5forall.ini" -Encoding ascii

# 3z. CONFERENCIA DE LICENCA DA STAGING (FASE4, 22/09/2026)
#
# Este script monta por LISTA DE PERMISSAO, entao hoje o LumeniteFX nao entra por construcao -
# e o cabecalho la em cima ja dizia isso. Mesmo assim a conferencia existe, e por um motivo
# concreto: o `montar-portable.ps1` montava por copia de pasta inteira e levou os
# `lumenite_*` para todo pacote publicado ate hoje (FONTES-ABERTAS-2209 §7.1.1). Um dia
# alguem acrescenta um `foreach` de pasta aqui tambem. Custa um Get-ChildItem.
#   - lumenite_* : AGNYA License (github.com/nvb-uy/AGNYA-License) - PROIBE redistribuir.
#                  O setup-dlss5.ps1 os baixa na maquina de quem instala.
#   - FidelityFX : MIT - PODE redistribuir, mas so COM a licenca junto (unica condicao).
$proibido = Get-ChildItem $Out -Recurse -File | Where-Object { $_.Name -match '^lumenite[_.-]' }
if ($proibido) {
    throw ("arquivo com licenca que proibe redistribuicao na staging: " +
           (($proibido | ForEach-Object { $_.FullName.Substring($Out.Length + 1) }) -join ', ') +
           " (LumeniteFX/AGNYA - o setup-dlss5.ps1 e quem os baixa)")
}
if ((Test-Path "$Out\amd_fidelityfx_dx12.dll") -and -not (Test-Path "$Out\LICENSE-FidelityFX.txt")) {
    throw "a staging leva amd_fidelityfx_dx12.dll sem LICENSE-FidelityFX.txt - a MIT do FidelityFX exige a licenca junto (copie de perfil-amd\LICENSE-FidelityFX.txt)"
}

# 4. zip (ou parada em -SomenteStaging, para o instalador pegar a pasta pronta)
if ($SomenteStaging) {
    Write-Host "ok -> staging em $Out (sem zip, -SomenteStaging)"
} else {
    $zip = Join-Path $Root "DLSS5ForAll-$Versao.zip"
    Remove-Item $zip -Force -ErrorAction SilentlyContinue
    Compress-Archive -Path "$Out\*" -DestinationPath $zip -CompressionLevel Optimal
    $mb = [math]::Round((Get-Item $zip).Length / 1MB, 2)
    Write-Host "ok -> $zip ($mb MB)"
}
Write-Host "build: DLSS5ForAll.exe FileVersion $($exe.VersionInfo.FileVersion.Trim()) compilado em $($exe.LastWriteTime), com icone ($grupos grupo(s) RT_GROUP_ICON)"
Get-ChildItem $Out -Recurse -File | ForEach-Object { $_.FullName.Substring($Out.Length + 1) }

# A AFINACAO DO DLSS QUE ESTA INDO NO PACOTE, na cara.
# O RenoDX regrava essas chaves sozinho (AutoSave): basta uma sessao de testes esquecida
# para o pacote sair com uma afinacao que ninguem escolheu. Listar aqui custa nada e e a
# unica chance de perceber antes de publicar.
Write-Host ""
Write-Host "afinacao do DLSS que vai como padrao (ReShade.ini):"
Select-String -Path "$Out\ReShade.ini" -Pattern '^(NR|DLSS)' | ForEach-Object { "   $($_.Line)" }
