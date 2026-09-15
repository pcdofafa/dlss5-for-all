# =====================================================================================
#  preparar-mod-amd.ps1  -  confere a pasta bin-amd do DLSS5 For All (módulo AMD, BETA)
#
#  O que ele FAZ: lê a pasta onde está, o Windows e a placa de vídeo, e diz em português
#  o que já está certo, o que falta e qual é o próximo passo.
#  O que ele NÃO FAZ: não baixa nada, não roda o instalador do mod, não mexe em binário.
#  As duas únicas alterações que ele oferece (e só depois de você responder S):
#    - copiar o SEU nvngx_dlssnr.dll de ..\bin\ para cá, se ele estiver lá e faltar aqui;
#    - trocar Inline=0 por Inline=1 no dlssnr_on_amd.ini (texto, gerado pelo setup do mod).
#
#  Uso: dois cliques em preparar-mod-amd.cmd (ele segura a janela no fim), ou
#       powershell -NoProfile -ExecutionPolicy Bypass -File .\preparar-mod-amd.ps1 [-SemPerguntas]
# =====================================================================================
param([switch]$SemPerguntas)
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
$Pasta = Split-Path -Parent $MyInvocation.MyCommand.Path
$UrlMod = 'https://github.com/danielblnc/DLSS-NR-on-AMD/releases/latest'
$Sha256Dlssnr = 'e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e'   # nvngx_dlssnr.dll 310.8.0.0, o hash que o próprio setup do mod confere
# Versão mínima do mod que este módulo espera. A 9060 XT (gfx1200) só entra na v0.2.14 (autor, 05/09/2026).
$VersaoMinimaMod  = [version]'0.2.11'
$VersaoMod9060    = [version]'0.2.14'
# SHA-256 dos instaladores publicados no GitHub (campo digest da API de releases). Um hash fora
# desta lista NÃO é erro: pode ser uma versão mais nova; aí o script lê a versão de dentro do arquivo.
$SetupsConhecidos = @{
    'dad67cc649ad91ba28e83c30049fc899900ae532daf818803bd1123e6e2315c3' = '0.2.18'
    'dbf7e40ca6940924ffe0960b350f3d55cb99ed27dde84aec47cdf8a0b7303a74' = '0.2.11'
    '5a28d907bba471504c6c7be6901a4eb6610f46b4d86ecb23b0430ba8bf3fc5d5' = '0.2.10'
    'b4b31e19e1d9028b3d63b7ac5074d7f71ede736d6185c11d25863f6415c6ece9' = '0.2.9'
    '19281e170071aa8098ba1f748f8e888370947e4ecfadba0c721ca52e3a509f4d' = '0.2.8'
    '3218ff020dffa6c3d6f878d36f5da6c8421086fcdcea1f1252e63e469ca1c818' = '0.2.7'
    'b16840f476140218a04f72f9f4504b395537c657b7d6c4499d30fac16293a5f2' = '0.2.6'
    'c462f4a9626112bb95a63241137d20a12f34d5da2dc777ab90a7465a351061db' = '0.2.0'
    'c90476d8c7769b34bce14db698de425ceeef7261b2ba5962337a3844fc04ac3e' = '0.1.0'
}
# Lê a versão gravada dentro do instalador ou do version.dll do mod (string ASCII "DLSS-NR on AMD vX.Y.Z",
# o título do overlay dele). Devolve [version] ou $null.
function VersaoDoMod($caminho) {
    try {
        $bytes = [System.IO.File]::ReadAllBytes($caminho)
        $txt = [System.Text.Encoding]::ASCII.GetString($bytes)
        $m = [regex]::Match($txt, 'DLSS[- ]NR on AMD v(\d+\.\d+\.\d+)')
        if ($m.Success) { return [version]$m.Groups[1].Value }
    } catch {}
    return $null
}

$faltando = New-Object System.Collections.Generic.List[string]
$avisos   = New-Object System.Collections.Generic.List[string]
function Ok($t)    { Write-Host "  [ok] $t" -ForegroundColor Green }
function Falta($t) { Write-Host "  [ X] $t" -ForegroundColor Red;    $faltando.Add($t) }
function Aviso($t) { Write-Host "  [ !] $t" -ForegroundColor Yellow; $avisos.Add($t) }
function Info($t)  { Write-Host "       $t" -ForegroundColor DarkGray }
function Titulo($t){ Write-Host ""; Write-Host $t -ForegroundColor Cyan }
function Pergunta($t) {
    if ($SemPerguntas) { return $false }
    $r = Read-Host "  $t [S/N]"
    return ($r -match '^[sSyY]')
}

Write-Host "====================================================================="
Write-Host "  DLSS5 FOR ALL - conferência do módulo AMD (BETA)"
Write-Host "  pasta: $Pasta"
Write-Host "====================================================================="

# ------------------------------------------------------------------ 1. Windows e placa
Titulo "1. Windows e placa de vídeo"
$build = [System.Environment]::OSVersion.Version.Build
if ($build -ge 22000) { Ok "Windows 11 (build $build)" }
else { Aviso "Windows 10 (build $build): o mod DLSS-NR-on-AMD é testado em Windows 11; pode não abrir" }

$placas = @()
try { $placas = @(Get-CimInstance Win32_VideoController -ErrorAction Stop) } catch {}
$radeon = $null
foreach ($p in $placas) {
    $nome = "$($p.Name)"
    $drv  = "$($p.DriverVersion)"
    if ($nome -match 'Radeon|AMD') {
        if ($nome -match 'RX\s*(7\d{3}|9\d{3})') {
            Ok "$nome (driver $drv)"
            if (-not $radeon) { $radeon = $nome }
            if ($nome -match '9060') {
                # Só marca a placa. O aviso de versão sai mais adiante, e SÓ se a versão
                # instalada for velha demais: antes ele disparava mesmo com a v0.2.18 no
                # disco, que é justamente a que resolve.
                $script:placa9060 = $true
                Info "RX 9060 XT (chip gfx1200): suportada a partir do mod v$VersaoMod9060. Espere uns 15 a 20 fps em 1080p; a Resolução do DLSS mais baixa alivia. Veja o LEIA-ME-AMD.txt"
            }
        } else {
            Aviso "${nome}: o mod cobre só Radeon RX 7000 (RDNA3) e RX 9000 (RDNA4)"
        }
    } else {
        Info "outra placa no PC: $nome"
    }
}
if (-not $radeon -and $placas.Count -gt 0) {
    Aviso "nenhuma Radeon RX 7000 / 9000 apareceu. Se ela existe, confira o Gerenciador de Dispositivos"
}
$hip = Join-Path ([Environment]::GetFolderPath('System')) 'amdhip64_7.dll'
if (Test-Path $hip) { Ok "runtime HIP do driver Adrenalin presente (amdhip64_7.dll)" }
else { Falta "runtime HIP (amdhip64_7.dll em System32): instale o driver Adrenalin 26.1.1 ou mais novo" }

# ------------------------------------------------------------------ 2. arquivos nossos
Titulo "2. Arquivos do pacote (vêm prontos no bin-amd)"
foreach ($f in 'DLSS5ForAll.exe','dxgi.dll','dlss5-bridge.addon64','amd_fidelityfx_dx12.dll','ReShade.ini','FafaDLSS5.ini','dlss5-feed.cfg') {
    if (Test-Path (Join-Path $Pasta $f)) { Ok $f } else { Falta "$f (remonte o pacote: amd\montar-portable.ps1)" }
}
if (Test-Path (Join-Path $Pasta 'dlss5-feed.addon64')) { Ok "dlss5-feed.addon64 (Feeder, variante AMD)" }
else { Falta "dlss5-feed.addon64: este pacote foi montado antes de o Feeder AMD ser compilado; sem ele nada chega ao mod" }
foreach ($f in 'renodx-dlss5.addon64','nvngx_dlss.dll') {
    if (Test-Path (Join-Path $Pasta $f)) { Aviso "$f está nesta pasta mas não pertence ao módulo AMD (é da cadeia NVIDIA); pode tirar" }
}

# ------------------------------------------------------------------ 3. arquivos seus
Titulo "3. O arquivo que só você tem: nvngx_dlssnr.dll"
$dlssnr = Join-Path $Pasta 'nvngx_dlssnr.dll'
if (-not (Test-Path $dlssnr)) {
    $noBin = Join-Path (Split-Path -Parent $Pasta) 'bin\nvngx_dlssnr.dll'
    if (Test-Path $noBin) {
        Aviso "nvngx_dlssnr.dll não está aqui, mas está em ..\bin\ (a sua cópia da cadeia NVIDIA)"
        if (Pergunta "Copiar ..\bin\nvngx_dlssnr.dll para cá agora?") {
            try { Copy-Item $noBin $dlssnr -Force; Ok "copiado" } catch { Falta "não consegui copiar: $($_.Exception.Message)" }
        }
    }
}
if (Test-Path $dlssnr) {
    $vi = $null
    try { $vi = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($dlssnr) } catch {}
    $ver = if ($vi) { "$($vi.FileVersion)" } else { '?' }
    $tam = (Get-Item $dlssnr).Length
    # O setup do mod conhece a build 310.8.0.0 pelo SHA-256; qualquer outra ele nao recusa de
    # cara: "this DLL build is not one the mod knows ... checking its tensors instead" e aceita
    # se todos os tensores baterem. Entao aqui e informacao, nao veredito.
    Ok "nvngx_dlssnr.dll presente: versão $ver, $([math]::Round($tam/1MB)) MB"
    try {
        $h = (Get-FileHash $dlssnr -Algorithm SHA256).Hash.ToLower()
        if ($h -eq $Sha256Dlssnr) { Ok "é exatamente a build 310.8.0.0 que o setup do mod conhece" }
        else { Info "não é a build 310.8.0.0 exata (hash diferente): o setup do mod vai conferir os tensores um a um e só aceita se todos baterem. Se ele recusar, procure a 310.8.0.0" }
    } catch {}
} else {
    Falta "nvngx_dlssnr.dll (o mesmo arquivo que a cadeia NVIDIA usa; NÃO é distribuído com o pacote). Coloque a sua cópia nesta pasta"
}

# ------------------------------------------------------------------ 4. o que o setup do mod gera
Titulo "4. Arquivos que o instalador do mod gera (dlssnr_on_amd_setup.exe)"
$setupPath = Join-Path $Pasta 'dlssnr_on_amd_setup.exe'
$setupAqui = Test-Path $setupPath
$versaoMod = $null
if ($setupAqui) {
    # Aceita qualquer instalador de versão >= $VersaoMinimaMod. A versão vem de dentro do arquivo;
    # o hash só diz se é um dos arquivos publicados que conhecemos.
    $vSetup = VersaoDoMod $setupPath
    $hSetup = $null
    try { $hSetup = (Get-FileHash $setupPath -Algorithm SHA256).Hash.ToLower() } catch {}
    $conhecido = if ($hSetup -and $SetupsConhecidos.ContainsKey($hSetup)) { $SetupsConhecidos[$hSetup] } else { $null }
    if (-not $vSetup -and $conhecido) { $vSetup = [version]$conhecido }
    if ($vSetup) {
        $versaoMod = $vSetup
        if ($vSetup -lt $VersaoMinimaMod) {
            Aviso "dlssnr_on_amd_setup.exe é a v$vSetup, mais antiga que a v$VersaoMinimaMod que este módulo espera. Baixe a mais nova em $UrlMod e rode de novo (tecla U atualiza)"
        } elseif ($conhecido) {
            Ok "dlssnr_on_amd_setup.exe v$vSetup (arquivo publicado no GitHub, hash conferido)"
        } else {
            Ok "dlssnr_on_amd_setup.exe v$vSetup"
            Aviso "o hash desse instalador não é de nenhuma release que este script conhece (as conhecidas vão até a v0.2.18). Se você baixou da página oficial de releases, é só uma versão mais nova: confira o SHA-256 lá. Se veio de fórum ou Discord, é um arquivo que ninguém auditou"
        }
    } else {
        Aviso "dlssnr_on_amd_setup.exe está aqui, mas não consegui ler a versão dele (nem o hash bate com uma release conhecida). Confira se é o arquivo da página oficial de releases"
    }
}
$gerados = 'version.dll','dlssnr_on_amd.ini','dlssnr_on_amd_weights.bin'
$faltaGerado = $false
foreach ($f in $gerados) {
    if (Test-Path (Join-Path $Pasta $f)) { Ok $f } else { Falta $f; $faltaGerado = $true }
}
if ($faltaGerado) {
    Write-Host ""
    if ($setupAqui) {
        Info "O dlssnr_on_amd_setup.exe já está nesta pasta. Dê dois cliques NELE (este script não o executa por você)."
        Info "Ele lê o nvngx_dlssnr.dll daqui, extrai os pesos e grava os três arquivos acima."
    } else {
        Info "Baixe o instalador do mod (só o dlssnr_on_amd_setup.exe) em:"
        Info "   $UrlMod"
        Info "copie-o para ESTA pasta (bin-amd) e dê dois cliques nele. Ele precisa do nvngx_dlssnr.dll ao lado."
    }
    Write-Host ""
    Aviso "ATENÇÃO ao prompt 'Install as dxgi.dll?' do instalador: responda 'version'."
    Info "   O instalador vê que o app carrega dxgi.dll e se oferece para tomar esse nome."
    Info "   Nesta pasta, dxgi.dll É O RESHADE. Aceitar apaga o ReShade e derruba a cadeia"
    Info "   inteira: sem ReShade não há Feeder, sem Feeder não há FSR, e sem FSR o mod não"
    Info "   tem onde se enganchar. O nome certo aqui é sempre version.dll."
    Write-Host ""
    Info "Antivírus: o instalador do mod tem detecções conhecidas (issue 40 do projeto); é você quem decide confiar."
}
if (Test-Path (Join-Path $Pasta 'version.dll')) {
    # O version.dll instalado também carrega a versão; é ele que vale quando o setup foi apagado ou trocado.
    $vDll = VersaoDoMod (Join-Path $Pasta 'version.dll')
    if ($vDll) {
        $versaoMod = $vDll
        if ($vDll -lt $VersaoMinimaMod) { Aviso "version.dll instalado é o mod v$vDll (este módulo espera v$VersaoMinimaMod ou mais novo). Baixe o instalador novo e rode com U" }
        else { Ok "mod instalado: v$vDll (lido do version.dll)" }
    } else { Info "não consegui ler a versão do mod no version.dll (não é um problema por si só)" }
}
if ($script:placa9060 -and $versaoMod -and $versaoMod -lt $VersaoMod9060) {
    Aviso "RX 9060 XT com mod v${versaoMod}: essa versão não tem os kernels da gfx1200. O log vai dizer 'invalid kernel file' ou 'UNSUPPORTED GPU ARCHITECTURE'. Precisa da v$VersaoMod9060 ou mais nova ($UrlMod)"
}

$ini = Join-Path $Pasta 'dlssnr_on_amd.ini'
if (Test-Path $ini) {
    $linhas = Get-Content $ini
    function Chave($k) { $l = $linhas | Where-Object { $_ -match "^\s*$k\s*=" } | Select-Object -First 1; if ($l) { return ($l -split '=',2)[1].Trim() } else { return $null } }
    $inline = Chave 'Inline'
    if ($inline -eq '1') { Ok "dlssnr_on_amd.ini: Inline=1 (obrigatório aqui: nossa janela é D3D11, o modo async do mod não se aplica)" }
    else {
        Aviso "dlssnr_on_amd.ini: Inline=$inline (precisa ser 1)"
        if (Pergunta "Trocar para Inline=1 agora?") {
            $novo = @(); $achou = $false
            foreach ($l in $linhas) { if ($l -match '^\s*Inline\s*=') { $novo += 'Inline=1'; $achou = $true } else { $novo += $l } }
            if (-not $achou) { $novo += 'Inline=1' }
            try { Set-Content -Path $ini -Value $novo -Encoding ASCII; Ok "gravado Inline=1" } catch { Falta "não consegui gravar o ini: $($_.Exception.Message)" }
        }
    }
    foreach ($k in 'Interop','UseFsrInputs','UseDepth','Enabled') {
        $v = Chave $k
        if ($v -eq '1' -or $null -eq $v) { Ok "dlssnr_on_amd.ini: $k=$(if ($null -eq $v) { '(padrão 1)' } else { $v })" }
        else { Aviso "dlssnr_on_amd.ini: $k=$v (o módulo espera 1)" }
    }
    $hd = Chave 'HipDevice'
    if ($hd -and $hd -ne '-1') { Info "HipDevice=$hd (fixo; -1 deixa o mod casar a placa sozinho)" }
    # InlineWaitMs: orçamento de espera do modo inline, só existe do mod v0.2.18 em diante.
    # É o teto de quanto o quadro espera pela rede; ao estourar, o mod mostra o resíduo do
    # quadro anterior em vez de segurar a cadeia. Medido em 11/09/2026 numa RX 9060 XT: a
    # rede levou ~13,5 ms por quadro, então 25 sobra e 200 deixaria um quadro ruim travar
    # a imagem por 0,2 s.
    $wait = Chave 'InlineWaitMs'
    # O INSTALADOR DO MOD REESCREVE ESTE INI. Ele grava as chaves dele com WritePrivateProfileString
    # (visível nas strings do dlssnr_on_amd_setup.exe v0.2.18, ao lado de "InlineWaitMs"), então quem
    # rodar o instalador depois de instalar o pacote perde o nosso 25 e fica com o padrão do mod.
    # Por isso esta conferência REPÕE o valor, em vez de só avisar: é a única chave nossa que o
    # instalador tem como sobrescrever, e a que mais muda o que a pessoa vê na tela.
    if ($null -eq $wait -or [int]$wait -gt 60) {
        if ($null -eq $wait) { Aviso "dlssnr_on_amd.ini: sem InlineWaitMs (o mod usa o padrão dele, alto). Testamos 25" }
        else { Aviso "dlssnr_on_amd.ini: InlineWaitMs=$wait é alto. Com esse teto, um quadro lento segura a imagem por até $wait ms. Testamos 25" }
        if (Pergunta "Repor InlineWaitMs=25 (o valor deste perfil) agora?") {
            $novo = @(); $achou = $false
            foreach ($l in $linhas) { if ($l -match '^\s*InlineWaitMs\s*=') { $novo += 'InlineWaitMs=25'; $achou = $true } else { $novo += $l } }
            if (-not $achou) { $novo += 'InlineWaitMs=25' }
            try { Set-Content -Path $ini -Value $novo -Encoding ASCII; Ok "gravado InlineWaitMs=25" } catch { Falta "não consegui gravar o ini: $($_.Exception.Message)" }
        }
    } else {
        Ok "dlssnr_on_amd.ini: InlineWaitMs=$wait (teto de espera da rede por quadro)"
    }
}

# ------------------------------------------------------------------ 5. cfg do Feeder
Titulo "5. dlss5-feed.cfg (as chaves do portador FSR)"
$cfg = Join-Path $Pasta 'dlss5-feed.cfg'
if (Test-Path $cfg) {
    $c = Get-Content $cfg
    function ValorCfg($k) { $l = $c | Where-Object { $_ -match "^\s*$k\s*=" } | Select-Object -First 1; if ($l) { return ($l -split '=',2)[1].Trim() } else { return $null } }
    foreach ($par in @(@('amd_carrier','1'), @('amd_color_fmt','87'))) {
        $v = ValorCfg $par[0]
        if ($null -eq $v) { Aviso "$($par[0]) não está no cfg (o padrão do perfil é $($par[1])); remonte o pacote ou acrescente a linha" }
        elseif ($v -eq $par[1]) { Ok "$($par[0])=$v" }
        else { Info "$($par[0])=$v (o padrão do perfil é $($par[1]))" }
    }
    # Esta é a chave que decide se a rede neural roda. Com 0, numa janela D3D11 como a
    # nossa, o mod engancha DXGI e Present mas nunca arma o hook do FSR: sem engine init,
    # sem HIP, sem 'network job', e a imagem sai como FSR puro -- parece "só um filtro".
    $decoy = ValorCfg 'amd_decoy_swapchain'
    if ($decoy -eq '1') { Ok "amd_decoy_swapchain=1 (o swapchain-isca que faz o mod adotar o device certo)" }
    else {
        if ($null -eq $decoy) { Falta "amd_decoy_swapchain não está no cfg. Sem ele em 1, a rede neural NÃO roda nesta janela D3D11" }
        else { Falta "amd_decoy_swapchain=${decoy}: com esse valor a rede neural NÃO roda (a imagem sai como FSR puro)" }
        # Repor, e nao so apontar: sem esta chave em 1 o caminho AMD entrega FSR puro e parece
        # estar funcionando - o defeito mais caro de diagnosticar que este modulo tem.
        if (Pergunta "Gravar amd_decoy_swapchain=1 no dlss5-feed.cfg agora?") {
            $novoC = @(); $achouC = $false
            foreach ($l in $c) { if ($l -match '^\s*amd_decoy_swapchain\s*=') { $novoC += 'amd_decoy_swapchain=1'; $achouC = $true } else { $novoC += $l } }
            if (-not $achouC) { $novoC += 'amd_decoy_swapchain=1' }
            try { Set-Content -Path $cfg -Value $novoC -Encoding ASCII; Ok "gravado amd_decoy_swapchain=1" } catch { Falta "não consegui gravar o cfg: $($_.Exception.Message)" }
        }
    }
}

# ------------------------------------------------------------------ 6. logs, se já rodou
Titulo "6. Logs da última execução (se houver)"
$logMod = Join-Path $Pasta 'dlssnr_on_amd.log'
if (Test-Path $logMod) {
    $t = Get-Content $logMod -Raw
    if ($t -match 'network job \d+ done in (\d+) ms') { Ok "mod: a rede neural rodou (último job em $($Matches[1]) ms)" }
    elseif ($t -match 'engine init ok') { Aviso "mod: motor iniciou, mas nenhum 'network job' apareceu (o quadro não está chegando)" }
    else { Aviso "mod: não achei 'engine init ok' no log" }
    if ($t -match 'UNSUPPORTED GPU ARCHITECTURE\s*(\S+)') { Falta "mod: placa não suportada por este build do mod ($($Matches[1])). RX 9060 XT = gfx1200: precisa do mod v$VersaoMod9060 ou mais novo" }
    elseif ($t -match 'invalid kernel file') { Falta "mod: 'invalid kernel file' = este build do mod não tem os kernels da sua placa (na RX 9060 XT, precisa da v$VersaoMod9060 ou mais nova)" }
    if ($t -match 'unsupported colour format (\d+)') { Aviso "mod: rejeitou o formato de cor $($Matches[1]). Troque amd_color_fmt=10 no dlss5-feed.cfg" }
    if ($t -match 'FSR is not active') { Aviso "mod: não viu o FSR ativo. Confira se dlss5-feed.addon64 (variante AMD) e amd_fidelityfx_dx12.dll estão aqui e amd_carrier=1" }
    elseif (($t -match 'setup failed; idle') -or (($t -match 'route backbuffer') -and ($t -notmatch 'route fsr'))) {
        Aviso "mod: não pegou o FSR ('setup failed; idle' / 'route backbuffer'): ele viu a tela, mas não o quadro do Feeder. Confira dlss5-feed.addon64 (variante AMD), amd_fidelityfx_dx12.dll e amd_carrier=1; se o log também falar em 'unsupported colour format', troque amd_color_fmt=10"
    }
    if ($t -match 'inline: (\d+) GPU wait timeouts') { Aviso "mod: $($Matches[1]) estouro(s) de espera da GPU (a rede está lenta demais para o teto). Baixe a resolução do DLSS no painel (End)" }
    if ($t -match 'no HIP device matches') { Aviso "mod: a placa que renderiza não é a Radeon. No seletor do app, escolha a Radeon em 'Placa de vídeo que roda o filtro'" }
    if ($t -match 'renders with D3D11: unsupported' -and $t -notmatch 'network job') { Aviso "mod: adotou o swapchain D3D11 do app. Confira amd_decoy_swapchain=1 no dlss5-feed.cfg (é o padrão do perfil)" }
    # O sintoma exato de decoy=0: o log do mod para nos hooks de DXGI e não vai adiante.
    if (($t -match 'hooked IDXGISwapChain::Present') -and ($t -notmatch 'engine init') -and ($t -notmatch 'ffxDispatch')) {
        Falta "mod: o log para nos hooks de DXGI, sem 'engine init' e sem hook do FFX. É o sintoma de amd_decoy_swapchain diferente de 1: a rede não roda e a imagem sai como FSR puro"
    }
    if ($t -match 'FAULT: exception') { Falta "mod: registrou uma exceção (FAULT). Veja o fim do dlssnr_on_amd.log" }
} else { Info "sem dlssnr_on_amd.log ainda (aparece depois da primeira execução com o mod instalado)" }
$logFeed = Join-Path $Pasta 'dlss5-feed.log'
if (Test-Path $logFeed) {
    $t = Get-Content $logFeed -Raw
    if ($t -match 'frame delivered') { Ok "Feeder: entregou quadros" }
    elseif ($t -match 'amd|fsr|ffx') { Aviso "Feeder: iniciou o portador FSR, mas não registrou 'frame delivered'" }
    else { Aviso "Feeder: nenhuma linha do portador FSR; confira se o dlss5-feed.addon64 é a variante AMD" }
}

# ------------------------------------------------------------------ resumo
Write-Host ""
Write-Host "====================================================================="
if ($faltando.Count -eq 0) {
    Write-Host "  TUDO NO LUGAR. Abra o DLSS5ForAll.exe da pasta de cima e clique no botão AMD." -ForegroundColor Green
    Write-Host "  Dentro do app, F6 mostra o antes e depois. Se nada mudar, rode este script de novo: ele lê os logs."
} else {
    Write-Host "  FALTA:" -ForegroundColor Red
    foreach ($f in $faltando) { Write-Host "   - $f" -ForegroundColor Red }
}
if ($avisos.Count -gt 0) {
    Write-Host "  AVISOS:" -ForegroundColor Yellow
    foreach ($a in $avisos) { Write-Host "   - $a" -ForegroundColor Yellow }
}
Write-Host "  Tudo explicado em LEIA-ME-AMD.txt (English: README-AMD.txt). Isto é BETA."
Write-Host "====================================================================="
