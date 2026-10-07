# =====================================================================================
#  publicar-versao.ps1  -  gera o que vai para o site para o atualizador do launcher.
#
#  v2.1 - SO PARA O MODO DIRETO. O launcher 2.1 tem dois modos, escolhidos pelo manifesto (ver
#  atualizacao.h): MODO SITE (so `versao` [+ `pagina`, `notas`]: a caixa abre a pagina de download e o
#  programa instalado segue abrindo) e MODO DIRETO (o manifesto traz o zip: o launcher baixa, confere o
#  SHA-256 e troca). Para o MODO SITE este script NAO e necessario: nao ha zip nem hash a gerar;
#  publique o manifesto a mao, ex.:  {"versao":"2.2","notas":"...","notas_en":"..."}  (ver
#  docs/ATUALIZACAO-2.1.md). Este script gera o manifesto do modo direto:
#     sem -SoDireto21  (padrao, como era): grava `url` + `sha256` - 1.2, 2.0 e 2.1+ atualizam direto
#     com -SoDireto21 : grava `zip_url` + `zip_sha256` - 1.2 e 2.0 ignoram o manifesto (so a 2.1+ atualiza)
#  Obs.: o /publicar do worker so aceita manifesto SEM `url`/`sha256` (modo site, -SoDireto21) depois do ajuste descrito em
#  docs/ATUALIZACAO-2.1.md (validarManifestoVersao).
#
#  A partir do pacote portable MONTADO (padrao: ..\DLSS5ForAll-AMD, o que o
#  montar-portable.ps1 produz), escreve em -Saida (padrao: ..\publicar\):
#     DLSS5ForAll-<versao>.zip   o pacote SEM o que e da pessoa ou de terceiros nao
#                                redistribuiveis (lista em $Excluir, embaixo)
#     versao.json                o manifesto que o launcher le (versao, data, url, sha256,
#                                notas, notas_en, minima_para_auto)
#     DLSS5ForAll-<versao>.sha256  o hash em texto, para conferencia
#
#  A versao vem de versao.h (#define DLSS5_VERSAO) - o mesmo numero que o launcher compila.
#  Antes de publicar: suba a versao em versao.h, main.cpp (APP_VERSION), launcher.rc e
#  app.rc; recompile o app e o launcher; rode montar-portable.ps1; so entao rode este.
#
#  O zip tem os arquivos NA RAIZ (DLSS5ForAll.exe, bin\ com bin\mod-amd\, versao.txt...), sem
#  pasta envolvendo. O versao.txt e obrigatorio: e por ele que o atualizador confere que
#  o zip e um pacote DLSS5 For All da versao anunciada antes de trocar qualquer arquivo.
#  O zip e feito com o tar.exe do Windows (bsdtar), o mesmo que o launcher usa para abrir.
#
#  USO
#     powershell -ExecutionPolicy Bypass -File .\publicar-versao.ps1 `
#         -UrlZip "https://github.com/<usuario>/<repo>/releases/download/v1.2/DLSS5ForAll-1.2.zip" `
#         -Notas "O que mudou, em uma ou duas frases." -NotasEn "What changed, in a sentence or two."
#     Parametros opcionais: -Origem, -Saida, -Versao (sobrepoe versao.h), -MinimaParaAuto
#     (menor versao INSTALADA que pode atualizar sozinha; vazio = todas), -Data (padrao hoje).
#     Sem -UrlZip o script gera tudo e deixa "url" com um marcador para voce trocar.
#
#  O ZIP COMPLETO, PRIVADO (-Completo)
#     Com -Completo o script gera, ALEM do publico e do mesmo conteudo, um segundo pacote:
#        publicar\privado\DLSS5ForAll-<versao>-completo-PRIVADO.zip  (+ .sha256 ao lado)
#     Ele leva tambem os componentes de terceiros que o publico nao pode redistribuir (DLSS,
#     Neural Rendering, RenoDX e o que o mod DLSS-NR-on-AMD instala) - ou seja, e uma
#     instalacao que ja funciona sem baixar mais nada. E para BACKUP E TESTE DO USUARIO.
#     ELE NUNCA SOBE PARA LUGAR NENHUM: -Completo e -Publicar nao andam juntos, e o -Publicar
#     tambem recusa um manifesto cuja url aponte para esse arquivo.
#
#  COMO PUBLICAR NO GITHUB RELEASES
#     1. Crie a release com a tag v<versao> (ex.: v1.2) e anexe o DLSS5ForAll-<versao>.zip.
#     2. A URL do anexo e https://github.com/<usuario>/<repo>/releases/download/v1.2/DLSS5ForAll-1.2.zip
#        - passe-a em -UrlZip (ou edite o versao.json depois; o hash nao muda com a URL).
#     3. Anexe TAMBEM o versao.json na MESMA release. O endereco fixo
#        https://github.com/<usuario>/<repo>/releases/latest/download/versao.json
#        sempre aponta para a release mais recente: e ele que vai no ofertas.ini
#        ([atualizacao] url_manifesto=). Alternativa: commitar o versao.json no repo e usar
#        https://raw.githubusercontent.com/<usuario>/<repo>/main/versao.json (cache de ~5 min).
#     O GitHub redireciona o download para objects.githubusercontent.com (https): o launcher
#     segue redirecionamento https->https; nunca https->http.
#
#  COMO PUBLICAR NO SITE DO GRUPO
#     Suba o zip e o versao.json para uma pasta servida em https (ex.:
#     https://grupo.pcdofafa.com.br/dlss5/), com "url" no json apontando para o zip la.
#     O servidor precisa devolver Content-Length (a janela de progresso mostra "x de y MB").
#     Nada de http puro: o launcher recusa.
#
#  Nao envia nada para lugar nenhum: so escreve os tres arquivos em -Saida.
# =====================================================================================
param(
    [string]$Origem,
    [string]$Saida,
    [string]$Versao,
    [string]$UrlZip,
    [string]$Notas   = '',
    [string]$NotasEn = '',
    [string]$MinimaParaAuto = '',
    [string]$Data,
    # -Publicar envia o versao.json ao servidor: POST {UrlBase}/publicar?token=... com
    # {"tipo":"versao","dados":{...}}. Token SO da variavel de ambiente DLSS5_TOKEN_ADMIN.
    # O zip continua indo por fora (GitHub Releases / site): o servidor guarda so o manifesto.
    [string]$UrlBase,
    [switch]$Publicar,
    # v2.1: grava zip_url/zip_sha256 em vez de url/sha256 (a 1.2 em campo nao ve a atualizacao direta)
    [switch]$SoDireto21,
    # v2.1: pagina de download (so https de pcdofafa.com.br; vazio = a padrao do launcher)
    [string]$Pagina = '',
    # -Completo gera, DEPOIS do zip publico e a partir do mesmo conteudo, um segundo zip
    # PRIVADO com os componentes de terceiros que o publico nao pode levar. E o backup/teste
    # do usuario: uma instalacao que funciona sem baixar mais nada. Ver a lista adiante.
    [switch]$Completo
)
$ErrorActionPreference = 'Stop'
# ---------------------------------------------------- O ZIP PRIVADO NUNCA VAI PARA O SERVIDOR
# As duas coisas nao acontecem na mesma execucao, de proposito. Publicar e um ato deliberado e
# separado; se -Completo pudesse ser passado junto com -Publicar, bastaria um historico de
# comando reaproveitado para o zip com DLSS, NR e RenoDX da NVIDIA acabar anunciado em publico.
if ($Completo -and $Publicar) {
    throw "-Completo e -Publicar nao andam juntos: o zip completo e PRIVADO (leva componentes de terceiros que nao podemos redistribuir). Gere primeiro com -Completo, confira, e publique depois numa execucao so com -Publicar."
}
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Origem) { $Origem = Join-Path (Split-Path -Parent $Root) 'DLSS5ForAll-AMD' }
if (-not $Saida)  { $Saida  = Join-Path (Split-Path -Parent $Root) 'publicar' }
if (-not $Data)   { $Data   = Get-Date -Format 'yyyy-MM-dd' }
if (-not $Versao) {
    $h = Get-Content (Join-Path $Root 'versao.h') -Raw
    if ($h -notmatch 'DLSS5_VERSAO\s+L"([0-9.]+)"') { throw "versao.h sem #define DLSS5_VERSAO" }
    $Versao = $Matches[1]
}
if ($Versao -notmatch '^[0-9]+(\.[0-9]+)*$') { throw "versao invalida: $Versao (so numeros e pontos)" }
if (-not (Test-Path (Join-Path $Origem 'DLSS5ForAll.exe'))) { throw "pacote nao montado em $Origem (rode montar-portable.ps1)" }
if (-not (Test-Path (Join-Path $Origem 'bin\DLSS5ForAll.exe'))) { throw "$Origem\bin\DLSS5ForAll.exe nao existe" }
# 07/10/2026: sem o auxiliar o modo Nativo nao integra (o app nao injeta mais sozinho - ver ponte\integrar.cpp)
if (-not (Test-Path (Join-Path $Origem 'bin\dlss5-integrar.exe'))) { throw "$Origem\bin\dlss5-integrar.exe nao existe (compile com build-mingw.sh e remonte com montar-portable.ps1)" }

# O que NAO vai no zip: arquivos da pessoa (configuracao, logs, cache, camada 2, backup) e
# terceiros que nao podemos redistribuir (DLSS/NR da NVIDIA, RenoDX, o mod DLSS-NR-on-AMD e
# os pesos dele). Mesma lista que o atualizar.cmd nunca sobrescreve, mais os .bak.
# O dlssnr_on_amd.ini SAIU desta lista em 11/09/2026. Ele nao e do mod nem da pessoa: e o
# NOSSO perfil do mod, e nele mora InlineWaitMs=25 - o teto de espera da rede por quadro que
# decide a experiencia inteira no caminho AMD. Sem ele no pacote, o instalador do mod cria o
# ini com o padrao dele (200 ms), e um quadro lento segura a imagem por 0,2 s. O arquivo nao
# tem segredo, nao tem log e nao tem estado de sessao: e configuracao, como o dlss5-feed.cfg.
# v2.1: VOLTOU para a lista. O dossie de licenca (16/09+) diz que a licenca do mod proibe redistribuir
# qualquer arquivo dele, inclusive as configuracoes; o ini e criado pelo instalador do mod na maquina da
# pessoa (em bin\mod-amd\) e o preparar-mod-amd repoe o InlineWaitMs=25.
$ExcluirArquivos = @(
    'nvngx_dlssnr.dll', 'nvngx_dlss.dll', 'renodx-dlss5.addon64', 'version.dll',
    'dlssnr_on_amd_weights.bin', 'dlssnr_on_amd_setup.exe', 'dlssnr_on_amd.log', 'dlssnr_on_amd.ini',
    'dlss5forall.log', 'launcher.log', 'ReShade.log', 'ReShade.log1', 'ReShade.log2', 'ReShade.log3', 'dlss5-feed.log'
)
# '*.log*' e nao '*.log': os logs ROTACIONADOS (dlss5forall.log.1, .log.2, .log.3) passavam
# pelo filtro antigo e iam para o zip de quem baixa - com o historico de uso da maquina de
# quem publicou dentro. Achado ao conferir o zip de 1.2, em 11/09/2026.
# 'renodx*': o arquivo se chama renodx-dlss.addon64 nesta build, e a lista de exclusao dizia
# renodx-dlss5.addon64 (com o "5"). Um nome fixo errado deixou um componente de TERCEIRO nao
# redistribuivel entrar no pacote publico. Por padrao, entao, e nao por nome.
# '*.dmp': o despejo de falha que o feeder deixa na pasta (dlss5-feed-crash.dmp) e a memoria
# do processo na maquina de quem publica. Achado em 11/09/2026, ja dentro do zip - vazio desta
# vez, mas um despejo de verdade leva para o pacote publico o que estava na memoria aqui.
# 'lumenite*' (FASE4, 22/09/2026): os `lumenite_*.fx`, os `include\lumenite_*.fxh` e a textura
# `lumenite_bluenoise256.png` sao do LumeniteFX (umar-afzaal) sob AGNYA License
# (github.com/nvb-uy/AGNYA-License), que PROIBE redistribuicao - FONTES-ABERTAS-2209 §7.1.1.
# Estavam indo no zip publico desde sempre, porque a copia do montar-portable.ps1 levava a
# pasta de shaders inteira e nenhuma das duas listas daqui tinha o nome. Quem instala
# continua com eles: o setup-dlss5.ps1, passo 4, os baixa do repositorio do autor.
# Este padrao e a SEGUNDA barreira - a primeira e o filtro do montar-portable.ps1 -, e as duas
# existem pelo mesmo motivo do renodx ter escapado por um digito: uma barreira so ja falhou.
# v2.1 PASTA UNICA: 'dlssnr_on_amd*' e 'version.dll' tambem por PADRAO - nada do mod DLSS-NR-on-AMD (licenca de
# terceiro que proibe redistribuir, inclusive as configuracoes dele) entra em zip nenhum, em pasta nenhuma.
$ExcluirPadroes = @('*.log', '*.log.*', '*.bak*', 'ReShade.ini.bak-*', 'renodx*', 'nvngx_*', '*.dmp', 'lumenite*', 'dlssnr_on_amd*', 'version.dll')
$ExcluirPastas  = @('reshade-cache', 'backup-versao-anterior', 'cache')
# AS PASTAS DE TESTE NAO VAO NO ZIP. A raiz de trabalho acumula copias inteiras do programa
# para experimentos (bin-teste-passes, bin-teste-requiem, launcher-teste...) - em 11/09/2026
# eram 13 pastas somando ~2,7 GB, e nenhuma delas tinha nada a ver com o que a pessoa baixa.
# Por padrao, e nao por nome fixo: a proxima pasta de teste vai se chamar outra coisa.
$ExcluirPastasPadraoTeste = '^(bin-teste-.*|.*-teste|teste-.*)$'

# ...E, DE QUALQUER FORMA, SO ESTAS DUAS PASTAS ENTRAM.
#
# Esta e uma lista de PERMISSAO, e ela existe porque a lista de exclusao falhou tres vezes no
# mesmo dia (11/09/2026): o renodx escapou por um nome com um digito a mais, os logs
# rotacionados por um curinga que nao previa ".log.1", e uma pasta de trabalho chamada
# "bin-amd-1.2-proposta" - copia inteira do modulo AMD - por nao se parecer com nada da lista.
# Ela sozinha somava 10,6 MB comprimidos e levou o zip a estourar o limite do servidor.
#
# A diferenca de fundo: uma lista de exclusao erra para o lado de PUBLICAR o que ninguem
# previu; uma lista de permissao erra para o lado de deixar de fora - e deixar de fora aparece
# no teste de instalacao, enquanto publicar demais so aparece depois, em publico. Uma pasta
# nova e legitima do produto precisa ser acrescentada aqui de proposito, e isso e o desejado.
# v2.1 PASTA UNICA: bin-amd\ saiu do produto (o modo AMD e bin\ com --amd; o mod mora em bin\mod-amd\, e
# la so vao as instrucoes - ver $PermitidosEmModAmd). Um bin-amd\ que sobrar na pasta de origem fica de fora.
$PastasDoProduto = @('bin')
$PermitidosEmModAmd = @('LEIA-ME.txt', 'README.txt', 'preparar-mod-amd.cmd', 'preparar-mod-amd.ps1')

# ------------------------------------------------------- O QUE SO O ZIP PRIVADO (-Completo) LEVA
# Sao os componentes de TERCEIROS que o pacote publico deixa de fora por licenca: o DLSS e o
# neural da NVIDIA, o add-on do RenoDX e o que o mod DLSS-NR-on-AMD instala na pasta do modulo.
# Aqui eles entram por CAMINHO COMPLETO, e nao por nome solto, para que a isencao valha so
# nestes lugares e o filtro do zip publico continue barrando o nome em qualquer outro canto.
# Cada um e opcional: nesta maquina o mod AMD pode nem estar instalado, e a lista so avisa.
$TerceirosDoCompleto = @(
    'bin\renodx-dlss.addon64',          # RenoDX - add-on que faz o neural no caminho NVIDIA
    'bin\nvngx_dlss.dll',               # DLSS (NVIDIA)
    'bin\nvngx_dlssnr.dll'              # Neural Rendering (NVIDIA)
    # v2.1: o mod DLSS-NR-on-AMD (version.dll, pesos, ini, instalador) SAIU daqui: a licenca do
    # autor proibe redistribuir, inclusive num zip privado. Ele fica so na maquina de quem o instala.
)

# ------------------------------------------------------------------ O PRESET DE FABRICA
# Vem de preset-fabrica.ps1, que e o UNICO lugar onde esses valores existem - o empacotar.ps1
# carrega o mesmo arquivo. Se cada script tivesse a sua copia, quem instala pelo site e quem
# atualiza pelo launcher acabariam com afinacoes diferentes do neural.
. (Join-Path $Root 'preset-fabrica.ps1')
# camada2, camada3, bin-camada2, bin-camada3... qualquer numero de camadas, presente ou futura
# (o app suporta ate kMaxCamadas hoje; listar so "bin-camada2"/"camada2" por nome deixava as
# de cima vazarem pro zip - ver amd\main.cpp CamadaPasta): pega por padrao, nao por nome fixo.
$ExcluirPastasPadrao = '^(bin-)?camada\d+$'
$ExcluirArquivos += @('relatorio-uso.txt', 'atualizacao-resultado.txt')
# Os .ini/.cfg VAO no zip: sao o padrao de fabrica para quem nao tem (o atualizador so os
# acrescenta quando faltam; o que a pessoa ja tem nunca e sobrescrito).

$tmp = Join-Path $env:TEMP ("DLSS5ForAll-publicar-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    Write-Host "origem : $Origem"
    Write-Host "versao : $Versao"
    # 1. copia filtrada para a pasta de trabalho
    $arqs = Get-ChildItem $Origem -Recurse -File | Where-Object {
        $rel = $_.FullName.Substring($Origem.Length).TrimStart('\')
        $partes = $rel -split '\\'
        $nome = $_.Name
        # A PASTA DE PRIMEIRO NIVEL decide primeiro: ou o arquivo esta na raiz, ou esta numa
        # das pastas do produto. Qualquer outra coisa fica de fora sem discussao.
        if ($partes.Length -gt 1 -and ($PastasDoProduto -notcontains $partes[0])) { return $false }
        $foraPasta = $false
        foreach ($p in $partes[0..($partes.Length - 2)]) {
            if (($ExcluirPastas -contains $p) -or ($p -match $ExcluirPastasPadrao) -or ($p -match $ExcluirPastasPadraoTeste)) { $foraPasta = $true }
        }
        if ($foraPasta) { return $false }
        if ($ExcluirArquivos -contains $nome) { return $false }
        foreach ($pad in $ExcluirPadroes) { if ($nome -like $pad) { return $false } }
        # v2.1: em bin\mod-amd\ so as instrucoes (lista de PERMISSAO, como $PastasDoProduto).
        if ($partes.Length -ge 3 -and $partes[0] -eq 'bin' -and $partes[1] -eq 'mod-amd' -and $PermitidosEmModAmd -notcontains $nome) { return $false }
        return $true
    }
    $n = 0; $bytes = 0
    foreach ($a in $arqs) {
        $rel = $a.FullName.Substring($Origem.Length).TrimStart('\')
        $dst = Join-Path $tmp $rel
        New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
        Copy-Item $a.FullName $dst -Force
        $n++; $bytes += $a.Length
    }
    # ------------------------------------------------------------------ ESTADO DE FABRICA
    # O PACOTE NAO PODE LEVAR A SESSAO DE QUEM PUBLICOU. Os .ini vao no zip de proposito (sao
    # o padrao para quem nao tem), mas na maquina de quem monta eles acumulam o estado do
    # ultimo teste. Auditado em 11/09/2026, o zip levava: [ultima] com "RESIDENT EVIL requiem"
    # e "Animaze Virtual Camera" como fonte, alterna_quadros=2 e alterna_modo=sintetizar (pulo
    # de quadros LIGADO), e o id_instalacao do autor - que faria TODO MUNDO que baixasse contar
    # como a mesma pessoa na telemetria.
    #
    # A regra e a mesma da lista de permissao das pastas: em vez de tentar apagar chave por
    # chave o que e "de sessao", escreve-se do zero o arquivo com o que deve haver. O app
    # recria o resto na primeira execucao, com os proprios padroes - que sao a verdade.
    $utf8 = New-Object System.Text.UTF8Encoding $false
    $cabec = "; Arquivo de fabrica: o programa escreve aqui o que voce for escolhendo.`r`n" +
             "; Se apagar este arquivo, tudo volta ao padrao.`r`n"
    # raiz: SEM id_instalacao (o launcher gera um novo na primeira abertura) e sem modulo
    [IO.File]::WriteAllText((Join-Path $tmp 'dlss5forall.ini'), $cabec, $utf8)
    foreach ($m in $PastasDoProduto) {
        $p = Join-Path $tmp "$m\dlss5forall.ini"
        if (Test-Path $p) { [IO.File]::WriteAllText($p, $cabec, $utf8) }
    }
    # o preset do RenoDX volta ao de fabrica (ver preset-fabrica.ps1)
    foreach ($m in $PastasDoProduto) {
        $p = Join-Path $tmp "$m\ReShade.ini"
        if (-not (Test-Path $p)) { continue }
        $linhas = [IO.File]::ReadAllLines($p)
        $ger = Geracao-DoReShadeIni $linhas
        # O MODULO AMD NAO TEM RENODX, e isso e de proposito: la quem faz o neural e o mod
        # DLSS-NR-on-AMD, e o montar-portable.ps1 chega a RECUSAR um bin-amd\ReShade.ini que
        # tenha a secao [RenoDX.DLSS5]. Entao "sem preset" so e erro na pasta NVIDIA.
        if ($ger -eq 'nenhuma') {
            if ($m -eq 'bin') { throw "bin\ReShade.ini nao tem nem [RENODX-DLSS] nem [RenoDX.DLSS5]: o pacote sairia sem preset do neural" }
            # ...mas ele passa pela normalizacao do mesmo jeito: o BOM estraga a gravacao do
            # painel em QUALQUER ReShade.ini, tenha ele preset do RenoDX ou nao.
            Salvar-IniSemBom $p $linhas
            Write-Host "  $m\ReShade.ini: sem secao RenoDX (esperado no modulo AMD, que usa o mod); normalizado sem BOM" -ForegroundColor DarkGray
            continue
        }
        $preset = if ($ger -eq 'nova') { $PresetFabricaNovo } else { $PresetFabricaAntigo }
        $r = Aplicar-Preset $linhas $preset
        # UMA CHAVE AUSENTE NAO PASSA EM SILENCIO: sem ela vale o padrao do add-on, e no caso
        # do HookPoint esse padrao e Auto, que trava esta cadeia. Acrescenta na secao certa.
        if ($r.faltando.Count) {
            $secao = if ($ger -eq 'nova') { '[RENODX-DLSS-preset1]' } else { '[RenoDX.DLSS5]' }
            $novas = New-Object System.Collections.ArrayList
            $posto = $false
            foreach ($l in $r.linhas) {
                [void]$novas.Add($l)
                if (-not $posto -and $l.Trim() -eq $secao) { foreach ($f in $r.faltando) { [void]$novas.Add($f) }; $posto = $true }
            }
            if (-not $posto) { [void]$novas.Add($secao); foreach ($f in $r.faltando) { [void]$novas.Add($f) } }
            $r.linhas = $novas.ToArray()
            Write-Host ("  $m\ReShade.ini: acrescentadas em $secao -> " + ($r.faltando -join ', ')) -ForegroundColor DarkGray
        }
        Salvar-IniSemBom $p $r.linhas
        Write-Host ("  $m\ReShade.ini: geracao $ger, preset de fabrica aplicado") -ForegroundColor DarkGray
    }
    # CONFERENCIA FINAL dos ini que viajam: BOM ou secao repetida derrubam o empacotamento.
    # Os dois produzem um app que parece funcionar e nao funciona - o painel grava o perfil
    # num lugar que ninguem le -, e nenhum dos dois aparece em teste de instalacao.
    foreach ($m in $PastasDoProduto) {
        $p = Join-Path $tmp "$m\ReShade.ini"
        if (-not (Test-Path $p)) { continue }
        $probs = Conferir-IniDoReShade $p
        if ($probs.Count) { throw "$m\ReShade.ini: $($probs -join ' | ')" }
    }
    Write-Host "estado de fabrica: dlss5forall.ini zerados e preset do neural normalizado" -ForegroundColor DarkGray

    # o cache do ReShade fica como pasta VAZIA (o ReShade.ini aponta para ela)
    foreach ($d in 'bin\reshade-cache', 'bin\mod-amd') { New-Item -ItemType Directory -Force (Join-Path $tmp $d) | Out-Null }
    # v2.1 PASTA UNICA: trava final - nada do mod DLSS-NR-on-AMD no zip, em pasta nenhuma.
    $doMod = @(Get-ChildItem $tmp -Recurse -File | Where-Object { $_.Name -like 'dlssnr_on_amd*' -or $_.Name -eq 'version.dll' })
    if ($doMod.Count) { throw "arquivo do mod DLSS-NR-on-AMD no zip (licenca proibe): $(($doMod.FullName) -join ', ')" }
    if (-not (Test-Path (Join-Path $tmp 'bin\mod-amd\LEIA-ME.txt'))) { Write-Host "[!] bin\mod-amd\LEIA-ME.txt nao esta no pacote (rode o montar-portable.ps1): quem tem Radeon fica sem as instrucoes" -ForegroundColor Yellow }
    # versao.txt: o do pacote precisa bater com a versao anunciada (senao o launcher recusa o zip)
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [IO.File]::WriteAllText((Join-Path $tmp 'versao.txt'), "$Versao`r`n", $utf8)
    Write-Host ("copiados: {0} arquivos, {1:N1} MB (sem o que e da pessoa/terceiros)" -f $n, ($bytes / 1MB))
    # DIGA O QUE FICOU DE FORA. Uma lista de permissao e segura justamente porque erra para o
    # lado de excluir - e o unico jeito de esse erro nao passar despercebido e ele aparecer na
    # tela toda vez. Uma pasta legitima nova entra em $PastasDoProduto.
    $ignoradas = Get-ChildItem $Origem -Directory | Where-Object { $PastasDoProduto -notcontains $_.Name } |
                 ForEach-Object { "{0} ({1:N0} MB)" -f $_.Name, ((Get-ChildItem $_.FullName -Recurse -File -ErrorAction Ignore | Measure-Object Length -Sum).Sum / 1MB) }
    if ($ignoradas) { Write-Host ("fora do zip (nao sao pastas do produto): " + ($ignoradas -join ', ')) -ForegroundColor DarkGray }

    # conferencia: nada proibido escapou
    # a conferencia usa OS MESMOS padroes do filtro, e nao uma copia deles a mao: foi por
    # divergirem que o renodx e os logs rotacionados escaparam uma vez
    $vazou = Get-ChildItem $tmp -Recurse -File | Where-Object {
        $nome = $_.Name
        if ($ExcluirArquivos -contains $nome) { return $true }
        foreach ($pad in $ExcluirPadroes) { if ($nome -like $pad) { return $true } }
        return $false
    }
    if ($vazou) { throw "arquivo proibido no zip: $(($vazou.FullName) -join ', ')" }
    # e nenhuma pasta de teste ou de camada extra sobrou (a exclusao acontece na copia; esta
    # conferencia existe para o dia em que alguem mexer no filtro e nao perceber)
    $pastaRuim = Get-ChildItem $tmp -Recurse -Directory | Where-Object {
        ($_.Name -match $ExcluirPastasPadraoTeste) -or ($_.Name -match $ExcluirPastasPadrao) -or ($ExcluirPastas -contains $_.Name -and $_.Name -ne 'reshade-cache')
    }
    if ($pastaRuim) { throw "pasta que nao devia ir no zip: $(($pastaRuim.FullName) -join ', ')" }
    # FASE4 (22/09/2026): BINARIO DE TERCEIRO NAO VIAJA SEM A LICENCA DELE.
    # O `amd_fidelityfx_dx12.dll` e da AMD sob MIT (FidelityFX SDK 1.1.4 - ver o CREDITOS.md), e
    # a MIT exige que o aviso de copyright e o texto da licenca acompanhem a copia. Hoje o
    # arquivo `LICENSE-FidelityFX.txt` so era copiado para o `bin-amd\`, e o zip de 22/09/2026
    # tinha a DLL no `bin\` SEM a licenca ao lado - descumprimento da unica condicao da MIT.
    # Vale por PASTA: a licenca tem de estar na mesma pasta que a DLL, porque e assim que a
    # pessoa que abre o zip encontra as duas.
    foreach ($m in $PastasDoProduto) {
        $dll = Join-Path (Join-Path $tmp $m) 'amd_fidelityfx_dx12.dll'
        $lic = Join-Path (Join-Path $tmp $m) 'LICENSE-FidelityFX.txt'
        if ((Test-Path $dll) -and -not (Test-Path $lic)) {
            throw "$m\amd_fidelityfx_dx12.dll vai no zip sem $m\LICENSE-FidelityFX.txt - a MIT do FidelityFX exige a licenca junto (copie de perfil-amd\LICENSE-FidelityFX.txt)"
        }
    }
    # A VERSAO ANUNCIADA TEM DE SER A QUE OS BINARIOS DECLARAM. O versao.txt do zip e escrito
    # por este script, entao ele sempre "bate"; o que nao bate sozinho e o VERSIONINFO dos
    # exe, que sai do app.rc/launcher.rc na hora de compilar. Se eles ficarem para tras, quem
    # instalar a versao nova continua se dizendo a antiga - e recebe o mesmo aviso de
    # atualizacao para sempre. Por isso isto e um erro, e nao um aviso.
    foreach ($exe in @('DLSS5ForAll.exe', 'bin\DLSS5ForAll.exe', 'bin\dlss5-integrar.exe')) {
        $p = Join-Path $tmp $exe
        if (-not (Test-Path $p)) { continue }
        $fv = (Get-Item $p).VersionInfo.FileVersion
        $curto = ($fv -replace '[, ]', '.') -replace '(\.0)+$', ''
        if ($curto -ne $Versao) {
            throw "$exe declara versao $fv, mas o pacote anuncia $Versao. Suba versao.h, app.rc, launcher.rc e ponte\integrar.rc para $Versao, recompile e remonte antes de publicar - senao quem atualizar vai receber o mesmo aviso de versao nova para sempre."
        }
    }
    # OS DEMAIS EXES do pacote (hoje so o bin-amd\) sao AVISO, e nao erro: quem compara a
    # versao com o manifesto e o launcher da raiz, entao um modulo atrasado nao produz o laco
    # de atualizacao. Mas ele sai nas Propriedades do arquivo com o numero errado, e isso e o
    # tipo de coisa que so se descobre depois de publicado.
    foreach ($outro in (Get-ChildItem $tmp -Recurse -Filter 'DLSS5ForAll.exe')) {
        $rel = $outro.FullName.Substring($tmp.Length).TrimStart('\')
        if ($rel -eq 'DLSS5ForAll.exe' -or $rel -eq 'bin\DLSS5ForAll.exe') { continue }
        $fv = $outro.VersionInfo.FileVersion
        $curto = ($fv -replace '[, ]', '.') -replace '(\.0)+$', ''
        if ($curto -ne $Versao) {
            Write-Host "[!] $rel declara versao $fv, e o pacote anuncia $Versao (nao impede a publicacao: quem compara com o manifesto e o launcher da raiz)" -ForegroundColor Yellow
        }
    }

    # 2. o zip (tar.exe do Windows; Compress-Archive como reserva)
    New-Item -ItemType Directory -Force $Saida | Out-Null
    $zip = Join-Path $Saida "DLSS5ForAll-$Versao.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (Test-Path $tar) {
        Push-Location $tmp
        try { & $tar -a -cf $zip -- * ; if ($LASTEXITCODE -ne 0) { throw "tar.exe devolveu $LASTEXITCODE" } }
        finally { Pop-Location }
    } else {
        Compress-Archive -Path (Join-Path $tmp '*') -DestinationPath $zip -CompressionLevel Optimal
    }

    # O LIMITE DO SERVIDOR: 25 MiB por arquivo. Passar disso nao e um detalhe - o zip
    # simplesmente nao sobe, e isso so se descobre na hora de publicar. Aviso alto, e nao
    # erro: o pacote pode ir para outro lugar (GitHub Releases aceita muito mais).
    $limiteServidor = 25MB
    if ((Get-Item $zip).Length -gt $limiteServidor) {
        Write-Host ("[!] o zip tem {0:N1} MiB e o servidor do grupo aceita {1:N0} MiB por arquivo - ele NAO vai subir la (no GitHub Releases cabe)" -f ((Get-Item $zip).Length / 1MB), ($limiteServidor / 1MB)) -ForegroundColor Yellow
    }

    # 3. hash e manifesto
    $sha = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
    [IO.File]::WriteAllText("$zip.sha256", "$sha *DLSS5ForAll-$Versao.zip`n", $utf8)
    if (-not $UrlZip) { $UrlZip = "https://TROQUE-ESTE-ENDERECO/DLSS5ForAll-$Versao.zip" }
    if ($UrlZip -notmatch '^https://') { throw "-UrlZip precisa ser https:// (o launcher recusa http)" }
    # SEGUNDA TRAVA DO ZIP PRIVADO. A primeira recusa -Completo junto com -Publicar; esta
    # recusa o MANIFESTO que aponte para ele - e vem antes de o versao.json ser escrito, para
    # que nao fique em disco um manifesto com o endereco errado esperando alguem subir na mao.
    # O que o servidor guarda e so o endereco, e e o endereco que faz o launcher de todo mundo
    # baixar o arquivo.
    if ($UrlZip -match '(?i)privado|completo') {
        throw "a url do manifesto aponta para o pacote PRIVADO ($UrlZip). O zip completo nunca vai para o servidor: no versao.json vai o endereco do DLSS5ForAll-$Versao.zip."
    }
    function J($s) { if ($null -eq $s) { $s = '' }; return ($s -replace '\\', '\\' -replace '"', '\"' -replace "`r", '' -replace "`n", ' ') }
    # tamanho_bytes: usado pela pagina de download (pcdofafa.com.br/dlss), que mostra
    # "versao · tamanho · data" ao lado do botao, E pelo launcher desde 11/09/2026 - a caixa
    # que pergunta escreve "Download de 48,3 MB · publicada em <data>" antes de a pessoa
    # responder. Se este campo sumir do JSON, a caixa simplesmente nao fala de tamanho.
    $campoUrl = if ($SoDireto21) { 'zip_url' } else { 'url' }; $campoSha = if ($SoDireto21) { 'zip_sha256' } else { 'sha256' }
    $json = '{"versao":"' + $Versao + '","data":"' + $Data + '","' + $campoUrl + '":"' + (J $UrlZip) + '","' + $campoSha + '":"' + $sha +
            ($(if ($Pagina) { '","pagina":"' + (J $Pagina) } else { '' })) +
            '","tamanho_bytes":' + (Get-Item $zip).Length +
            ',"notas":"' + (J $Notas) + '","notas_en":"' + (J $NotasEn) + '","minima_para_auto":"' + (J $MinimaParaAuto) + '"}'
    [IO.File]::WriteAllText((Join-Path $Saida 'versao.json'), $json + "`n", $utf8)

    Write-Host "zip    : $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
    Write-Host "sha256 : $sha"
    Write-Host "json   : $(Join-Path $Saida 'versao.json')"
    Write-Host $json
    if ($UrlZip -like 'https://TROQUE-ESTE-ENDERECO/*') { Write-Host "[!] troque a url no versao.json pelo endereco real do zip antes de subir" -ForegroundColor Yellow }

    # ------------------------------------------------------------------ 4. O ZIP PRIVADO
    # Ele sai DEPOIS do publico e da MESMA pasta de trabalho, e nao de uma segunda copia com
    # os proprios filtros: assim os dois pacotes sao o mesmo produto, com o mesmo estado de
    # fabrica nos ini, e a unica diferenca sao os arquivos acrescentados aqui. Uma segunda
    # montagem poderia divergir em silencio - e divergir e justamente o que nao se percebe.
    if ($Completo) {
        $privado = Join-Path $Saida 'privado'
        New-Item -ItemType Directory -Force $privado | Out-Null
        $zipC = Join-Path $privado "DLSS5ForAll-$Versao-completo-PRIVADO.zip"
        if (Test-Path $zipC) { Remove-Item $zipC -Force }
        $post = 0; $bytesC = 0; $faltaram = @()
        foreach ($rel in $TerceirosDoCompleto) {
            $orig = Join-Path $Origem $rel
            if (-not (Test-Path $orig)) { $faltaram += $rel; continue }
            $dst = Join-Path $tmp $rel
            New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
            Copy-Item $orig $dst -Force
            $post++; $bytesC += (Get-Item $orig).Length
        }
        if ($faltaram) {
            # Nao e erro: o mod AMD pode nao estar instalado nesta maquina, e nesse caso nao ha
            # o que guardar. Mas tem de APARECER, senao um backup incompleto passa por completo.
            Write-Host ("[!] nao estavam na producao e ficaram de fora do zip completo: " + ($faltaram -join ', ')) -ForegroundColor Yellow
        }
        # Conferencia: dentro do privado so podem existir os proibidos QUE ESTAO NA LISTA, e
        # nos caminhos dela. Qualquer outro arquivo barrado que apareca aqui e vazamento.
        $vazouC = Get-ChildItem $tmp -Recurse -File | Where-Object {
            $rel = $_.FullName.Substring($tmp.Length).TrimStart('\')
            if ($TerceirosDoCompleto -contains $rel) { return $false }
            $nome = $_.Name
            if ($ExcluirArquivos -contains $nome) { return $true }
            foreach ($pad in $ExcluirPadroes) { if ($nome -like $pad) { return $true } }
            return $false
        }
        if ($vazouC) { throw "arquivo proibido fora da lista do zip completo: $(($vazouC.FullName) -join ', ')" }
        if (Test-Path $tar) {
            Push-Location $tmp
            try { & $tar -a -cf $zipC -- * ; if ($LASTEXITCODE -ne 0) { throw "tar.exe devolveu $LASTEXITCODE" } }
            finally { Pop-Location }
        } else {
            Compress-Archive -Path (Join-Path $tmp '*') -DestinationPath $zipC -CompressionLevel Optimal
        }
        $shaC = (Get-FileHash $zipC -Algorithm SHA256).Hash.ToLower()
        [IO.File]::WriteAllText("$zipC.sha256", "$shaC *$(Split-Path -Leaf $zipC)`n", $utf8)
        Write-Host ""
        Write-Host ("privado: $zipC ({0:N1} MB, +{1} arquivos de terceiros, {2:N1} MB soltos)" -f ((Get-Item $zipC).Length / 1MB), $post, ($bytesC / 1MB)) -ForegroundColor Cyan
        Write-Host "sha256 : $shaC"
        Write-Host "NAO SUBA ESTE ARQUIVO PARA LUGAR NENHUM: ele leva DLSS, Neural Rendering, RenoDX e o mod AMD, que sao de terceiros e nao podemos redistribuir. E backup e teste do usuario." -ForegroundColor Yellow
    }

    if ($Publicar) {
        if (-not $UrlBase) { throw "-Publicar precisa de -UrlBase (o dominio do servidor, sem barra no fim)" }
        if (-not $env:DLSS5_TOKEN_ADMIN) { throw "defina a variavel de ambiente DLSS5_TOKEN_ADMIN (o token do servidor) antes de -Publicar" }
        if ($UrlBase -notmatch '^https://' -and $UrlBase -notmatch '^http://(127\.0\.0\.1|localhost)') { throw "-UrlBase precisa ser https://" }
        if ($UrlZip -like 'https://TROQUE-ESTE-ENDERECO/*') { throw "nao publico um manifesto com url de mentira: passe -UrlZip" }
        $corpo = '{"tipo":"versao","dados":' + $json + '}'
        # token no cabecalho (Authorization: Bearer), como o publicar.ps1 do worker - nao vai na URL nem no log
        $r = Invoke-WebRequest -Uri ($UrlBase.TrimEnd('/') + '/publicar') -Method Post -Headers @{ Authorization = "Bearer $env:DLSS5_TOKEN_ADMIN" } `
             -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($corpo)) -UseBasicParsing -TimeoutSec 20
        Write-Host "publicado: HTTP $($r.StatusCode) $($r.Content)"
        Write-Host "lembre de subir o zip em $UrlZip - o servidor guarda so o manifesto"
    }
}
finally {
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
