# =====================================================================================
#  publicar-oferta.ps1  -  a PUBLI da tela do grupo, trocada pela web sem lancar versao.
#
#  Monta o ofertas.json que o launcher busca em {url_base}/ofertas.json e, com -Publicar,
#  envia ao servidor (POST {url_base}/publicar?token=... com {"tipo":"ofertas","dados":{...}}).
#  O token vem SO da variavel de ambiente DLSS5_TOKEN_ADMIN - nunca de arquivo no repositorio.
#
#  Todos os campos sao opcionais: o que vier preenchido SOBREPOE o ofertas.ini de quem usa;
#  o que ficar vazio continua como esta no ini/no programa. O launcher guarda o ultimo JSON
#  valido ao lado dele (ofertas-remoto.json) e abre com ele mesmo sem rede; passada a
#  -Validade (data local > validade), ele volta ao texto do ini sozinho.
#
#  USO
#     $env:DLSS5_TOKEN_ADMIN = "..."     # so na sessao; nao grave em arquivo
#     powershell -ExecutionPolicy Bypass -File .\publicar-oferta.ps1 `
#         -Titulo "SSD de 1 TB por R$ 299" -Mensagem "Só até domingo, no link do grupo." `
#         -Cupom PCDOFAFA10 -CupomTexto "10% no SSD X até domingo" -Validade 2026-09-14 `
#         -UrlBase https://<seu-worker>.workers.dev -Publicar
#     Sem -Publicar, so escreve ..\publicar\ofertas.json para voce subir a mao.
#     Para TIRAR a publi e voltar ao ini: publique um JSON vazio (rode sem campos) ou com
#     -Validade de ontem.
#
#  Campos: -Titulo/-TituloEn, -Mensagem/-MensagemEn (aceitam \n), -Cta/-CtaEn (o selo),
#  -Botao/-BotaoEn, -Url (destino do QR e do botao), -Canal, -Cupom, -CupomTexto/-CupomTextoEn,
#  -Validade (AAAA-MM-DD), -VersaoConteudo (padrao: data+hora, so para o log do launcher).
#  Textos passam pela mesma peneira do ofertas.ini no launcher (sem controle, 512 chars).
# =====================================================================================
param(
    [string]$Titulo, [string]$TituloEn,
    [string]$Mensagem, [string]$MensagemEn,
    [string]$Cta, [string]$CtaEn,
    [string]$Botao, [string]$BotaoEn,
    [string]$Url, [string]$Canal,
    [string]$Cupom, [string]$CupomTexto, [string]$CupomTextoEn,
    [string]$Validade,
    [string]$VersaoConteudo,
    [string]$Saida,
    [string]$UrlBase,
    [switch]$Publicar
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Saida) { $Saida = Join-Path (Split-Path -Parent $Root) 'publicar' }
if (-not $VersaoConteudo) { $VersaoConteudo = Get-Date -Format 'yyyy-MM-dd-HHmm' }
foreach ($u in @($Url, $Canal)) { if ($u -and $u -notmatch '^https?://') { throw "endereco precisa comecar com https://: $u" } }
if ($Validade -and $Validade -notmatch '^\d{4}-\d{2}-\d{2}$') { throw "-Validade no formato AAAA-MM-DD" }
if ($Cupom -and $Cupom.Length -gt 32) { throw "-Cupom com mais de 32 caracteres" }

function J($s) { if ($null -eq $s) { $s = '' }; return ($s -replace '\\', '\\' -replace '"', '\"' -replace "`r", '' -replace "`n", '\n') }
$campos = [ordered]@{
    versao_conteudo = $VersaoConteudo
    titulo = $Titulo; titulo_en = $TituloEn
    mensagem = $Mensagem; mensagem_en = $MensagemEn
    cta = $Cta; cta_en = $CtaEn
    botao = $Botao; botao_en = $BotaoEn
    url = $Url; canal = $Canal
    cupom = $Cupom; cupom_texto = $CupomTexto; cupom_texto_en = $CupomTextoEn
    validade = $Validade
}
$partes = @()
foreach ($k in $campos.Keys) { if ($campos[$k]) { $partes += ('"' + $k + '":"' + (J $campos[$k]) + '"') } }
$json = '{' + ($partes -join ',') + '}'

New-Item -ItemType Directory -Force $Saida | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding $false
$arq = Join-Path $Saida 'ofertas.json'
[IO.File]::WriteAllText($arq, $json + "`n", $utf8)
Write-Host "ofertas.json: $arq"
Write-Host $json

if ($Publicar) {
    if (-not $UrlBase) { throw "-Publicar precisa de -UrlBase (o dominio do servidor, sem barra no fim)" }
    if (-not $env:DLSS5_TOKEN_ADMIN) { throw "defina a variavel de ambiente DLSS5_TOKEN_ADMIN (o token do servidor) antes de -Publicar" }
    if ($UrlBase -notmatch '^https://' -and $UrlBase -notmatch '^http://(127\.0\.0\.1|localhost)') { throw "-UrlBase precisa ser https://" }
    $corpo = '{"tipo":"ofertas","dados":' + $json + '}'
    # token no cabecalho (Authorization: Bearer), como o publicar.ps1 do worker - nao vai na URL nem no log
    $r = Invoke-WebRequest -Uri ($UrlBase.TrimEnd('/') + '/publicar') -Method Post -Headers @{ Authorization = "Bearer $env:DLSS5_TOKEN_ADMIN" } `
         -ContentType 'application/json; charset=utf-8' -Body ([Text.Encoding]::UTF8.GetBytes($corpo)) -UseBasicParsing -TimeoutSec 20
    Write-Host "publicado: HTTP $($r.StatusCode) $($r.Content)"
    Write-Host "o launcher pega a publi nova na proxima abertura (e guarda em ofertas-remoto.json)"
}
