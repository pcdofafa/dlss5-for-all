# =====================================================================================
#  preset-fabrica.ps1 - O PRESET DO NEURAL QUE VAI NO PACOTE, num lugar so.
#
#  Carregado por publicar-versao.ps1 (zip da atualizacao) e por empacotar.ps1 (pacote do
#  instalador). Os dois precisam produzir o MESMO ReShade.ini: se divergirem, quem instala
#  pelo site recebe uma afinacao e quem atualiza pelo launcher recebe outra, e ninguem
#  entende por que a imagem mudou.
#
#  Uso:   . (Join-Path $PSScriptRoot 'preset-fabrica.ps1')
#         ...e entao $PresetFabricaNovo / $PresetFabricaAntigo estao definidos.
#
#  DUAS GERACOES DO RENODX, e o pacote pode encontrar qualquer uma:
#    - NOVA  (a que a bin\ roda hoje): secoes [RENODX-DLSS] e [RENODX-DLSS-preset1], chaves
#      DirectNeuralRendering*.
#    - ANTIGA: secao [RenoDX.DLSS5], chaves NRIntensity / NRStyle / NeuralUplift...
#  Nenhuma das duas e "a errada": o que nao pode e um script conhecer so uma e sair calado
#  quando encontra a outra - foi o que aconteceu ate 11/09/2026, quando o empacotar.ps1 so
#  conhecia a antiga e o pacote saia SEM HookPoint (ausente = Auto, que trava esta cadeia).
# =====================================================================================

# ------------------------------------------------------------------ geracao NOVA
# O MODELO E O 0 (Modelo A), e isso e coerencia com o painel, nao gosto: com [ultima]
# nr_estilo AUSENTE - que e como o ini de fabrica viaja - o painel nasce no degrau 0, Modelo
# A, e o arranque so aplica os passes. Gravar Style=1 aqui faria o painel mostrar "Modelo A"
# enquanto a rede roda o B: o painel mentiria na primeira abertura.
#
# SOBRE O PassCount=1: houve uma medicao em 11/09/2026 concluindo que Style/1 passe deixava a
# rede INERTE (mesmo pixel ligada e desligada, MAE 0,000). A conclusao foi RETIRADA pelo
# proprio auditor e refutada: os dumps tinham sido tirados em quadros PULADOS, com o frame
# skipping ligado. Com o skipping desligado, 1 passe muda 51% dos pixels contra a fonte. O
# padrao abaixo esta conferido - este paragrafo existe para ninguem "corrigir" de novo.
$PresetFabricaNovo = [ordered]@{
    # 2.1: de fabrica a primeira abertura (captura, Universal, RTX) escolhe o motor PROPRIO, entao o ini sai no estado dele
    # (HookPoint/HookMethod 0 + RenoDX em DisabledAddons + nr_spike=1 no cfg): no do RenoDX a primeira abertura reabria o app.
    # AUSENTE = Auto, que trava - por isso a chave existe com valor, nunca ausente.
    'DirectNeuralRenderingHookPoint'              = '0'
    'DirectNeuralRenderingHookMethod'             = '0'
    'DirectNeuralRenderingIntensity'              = '1'
    'DirectNeuralRenderingPassCount'              = '1'
    'DirectNeuralRenderingStyle'                  = '0'    # Modelo A, o que o painel mostra de fabrica
    'DirectNeuralRenderingGlobalToneStrength'     = '1'
    'DirectNeuralRenderingLocalToneStrength'      = '1'
    'DirectNeuralRenderingLocalStructureStrength' = '1'
    'DirectNeuralRenderingSkinStructureStrength'  = '1'
    'DirectNeuralRenderingAutoMask'               = '0'
}

# ------------------------------------------------------------------ geracao ANTIGA
# Mantida porque um ReShade.ini antigo ainda pode chegar ao empacotador. NRStyle=0 e o mesmo
# Modelo A da tabela nova; NRSkinStructure=-1 e o valor que o script sempre gravou aqui.
$PresetFabricaAntigo = [ordered]@{
    'NeuralUplift'      = '1'
    'NRIntensity'       = '1'
    'NRLocalTone'       = '1'
    'NRLocalStructure'  = '1'
    'NRSkinStructure'   = '-1'
    'NRStyle'           = '0'
}

# Qual geracao este ReShade.ini usa? Decide pelo que ESTA no arquivo, e nao por versao
# declarada em lugar nenhum - o arquivo e a verdade.
function Geracao-DoReShadeIni([string[]]$linhas) {
    if ($linhas -match '^\s*DirectNeuralRendering') { return 'nova' }
    if ($linhas -match '^\s*\[RenoDX\.DLSS5\]')     { return 'antiga' }
    return 'nenhuma'
}

# ------------------------------------------------------------------ BOM E SECAO DUPLICADA
# O ReShade.ini NAO PODE TER BOM. O painel grava o perfil com WritePrivateProfileString, e a
# API de ini do Windows nao reconhece a primeira secao de um arquivo que comeca com BOM
# UTF-8: em vez de achar o [RENODX-DLSS-preset1] que ja existe, ela cria UM SEGUNDO no fim do
# arquivo. A leitura continua pegando o primeiro, entao o perfil que a pessoa escolhe no
# painel simplesmente nunca vale - e nada no caminho reclama. Achado pelo painel-requiem em
# 11/09/2026.
#
# Por isso o pacote normaliza todo ReShade.ini que viaja (sem BOM, CRLF) e CONFERE antes de
# fechar: BOM ou secao repetida derrubam o empacotamento, porque os dois produzem um app que
# parece funcionar e nao funciona.
function Salvar-IniSemBom([string]$caminho, [string[]]$linhas) {
    # WriteAllLines com UTF8Encoding($false) ja escreve sem BOM, e a quebra de linha e a do
    # Windows (CRLF) - que e a que o ReShade e a API de ini esperam.
    [IO.File]::WriteAllLines($caminho, $linhas, (New-Object System.Text.UTF8Encoding $false))
}
# Devolve a lista de problemas (vazia = em ordem). Quem chama decide entre avisar e recusar.
function Conferir-IniDoReShade([string]$caminho) {
    $problemas = New-Object System.Collections.ArrayList
    $b = [IO.File]::ReadAllBytes($caminho)
    if ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF) {
        [void]$problemas.Add('comeca com BOM UTF-8: o painel gravaria um segundo [RENODX-DLSS-preset1] e o perfil nunca valeria')
    }
    $txt = [IO.File]::ReadAllText($caminho)
    foreach ($sec in @('[RENODX-DLSS]', '[RENODX-DLSS-preset1]', '[RenoDX.DLSS5]')) {
        $n = ([regex]::Matches($txt, '(?m)^\s*' + [regex]::Escape($sec))).Count
        if ($n -gt 1) { [void]$problemas.Add("$sec aparece $n vezes: a leitura pega a primeira e as outras viram lixo silencioso") }
    }
    return $problemas
}

# Reescreve, NO LUGAR, as chaves do preset que existirem. Devolve as que NAO foram achadas:
# quem chama decide se isso e um aviso ou um erro. Uma chave ausente nao pode passar em
# silencio - para o add-on, ausente vale o padrao dele, que nao e o nosso.
function Aplicar-Preset([string[]]$linhas, $preset) {
    $faltando = New-Object System.Collections.ArrayList
    foreach ($k in $preset.Keys) {
        $achou = $false
        for ($i = 0; $i -lt $linhas.Count; $i++) {
            if ($linhas[$i] -match "^\s*$([regex]::Escape($k))\s*=") { $linhas[$i] = "$k=$($preset[$k])"; $achou = $true }
        }
        if (-not $achou) { [void]$faltando.Add("$k=$($preset[$k])") }
    }
    return @{ linhas = $linhas; faltando = $faltando }
}
