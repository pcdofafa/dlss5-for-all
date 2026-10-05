# =====================================================================================
#  preparar-mod-amd.ps1  -  confere a pasta bin\mod-amd do DLSS5 For All (placas AMD, experimental)
#
#  v2.1 PASTA UNICA: o modo AMD é o mesmo bin\DLSS5ForAll.exe aberto com --amd; o mod
#  DLSS-NR-on-AMD (de Daniel Blanco / danielblnc) mora em bin\mod-amd\ (a pasta deste script)
#  e o programa o carrega por caminho completo, só no modo AMD.
#
#  O que ele FAZ: lê esta pasta, a pasta bin\ (a de cima), o Windows e a placa de vídeo, e diz
#  em português o que já está certo, o que falta e qual é o próximo passo.
#  O que ele NÃO FAZ: não baixa nada, não roda o instalador do mod, não mexe em binário, não
#  apaga nada. As únicas alterações que ele oferece (e só depois de você responder S):
#    - copiar o SEU nvngx_dlssnr.dll de bin\ para cá (o instalador do mod precisa dele ao lado);
#    - COPIAR os arquivos do mod da pasta antiga ..\bin-amd\ para cá (quem veio da 1.2/2.0);
#    - trocar Inline=0 por Inline=1 e repor InlineWaitMs=25 no dlssnr_on_amd.ini.
#
#  Uso: dois cliques em preparar-mod-amd.cmd (ele segura a janela no fim), ou
#       powershell -NoProfile -ExecutionPolicy Bypass -File .\preparar-mod-amd.ps1 [-SemPerguntas]
# =====================================================================================
param([switch]$SemPerguntas)
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
$Pasta = Split-Path -Parent $MyInvocation.MyCommand.Path          # bin\mod-amd
$Bin   = Split-Path -Parent $Pasta                                # bin
$Raiz  = Split-Path -Parent $Bin
$Antiga = Join-Path $Raiz 'bin-amd'                               # layout da 1.2/2.0
$UrlMod = 'https://github.com/danielblnc/DLSS-NR-on-AMD/releases'
$UrlHipSdk = 'https://www.amd.com/en/developer/resources/rocm-hub/hip-sdk.html'
$Sha256Dlssnr = 'e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e'   # nvngx_dlssnr.dll 310.8.0.0, o hash que o próprio setup do mod confere
# Piso de versão do mod (o que este programa nunca aceitaria abaixo). NÃO é a recomendada: use
# sempre a mais nova da página oficial que cubra a sua placa.
$VersaoMinimaMod  = [version]'0.2.11'
$VersaoMod9060    = [version]'0.2.14'   # RX 9060 XT (gfx1200)
$VersaoModRdna2   = [version]'0.6.0'    # RX 6000 (RDNA2), segundo as notas do autor; NÃO testada aqui
# SHA-256 de instaladores publicados no GitHub que já conferimos. Um hash fora desta lista NÃO é erro:
# pode ser uma versão mais nova; aí o script lê a versão de dentro do arquivo.
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
# o título do overlay dele). Só leitura de bytes, sem executar nada. Devolve [version] ou $null.
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
Write-Host "  DLSS5 FOR ALL - conferência do mod AMD (experimental)"
Write-Host "  pasta do mod: $Pasta"
Write-Host "  mod DLSS-NR-on-AMD de Daniel Blanco (danielblnc) - não vem com o programa"
Write-Host "====================================================================="

# ------------------------------------------------------------------ 1. Windows e placa
Titulo "1. Windows e placa de vídeo"
$build = [System.Environment]::OSVersion.Version.Build
if ($build -ge 22000) { Ok "Windows 11 (build $build)" }
else { Aviso "Windows 10 (build $build): o mod DLSS-NR-on-AMD é testado em Windows 11; pode não abrir" }

# A LISTA DAS SÉRIES (igual a kSeriesRadeonMod no app e kSeriesRadeon no launcher).
$placas = @()
try { $placas = @(Get-CimInstance Win32_VideoController -ErrorAction Stop) } catch {}
$radeon = $null; $rdna2 = $false; $rdna34 = $false
foreach ($p in $placas) {
    $nome = "$($p.Name)"
    $drv  = "$($p.DriverVersion)"
    if ($nome -match 'Radeon|AMD') {
        if ($nome -match 'RX\s*(7\d{3}|9\d{3})') {
            Ok "$nome (driver $drv)"
            if (-not $radeon) { $radeon = $nome }
            $rdna34 = $true
            if ($nome -match '9060') {
                $script:placa9060 = $true
                Info "RX 9060 XT (chip gfx1200): suportada a partir do mod v$VersaoMod9060."
            }
        } elseif ($nome -match 'RX\s*6\d{3}') {
            Aviso "${nome}: RX 6000 (RDNA2) é coberta a partir do mod v$VersaoModRdna2, com o AMD HIP SDK 7.2 instalado à parte ($UrlHipSdk). NÃO testada por este programa"
            if (-not $radeon) { $radeon = $nome }
            $rdna2 = $true
        } else {
            Aviso "${nome}: o mod cobre Radeon RX 7000 e RX 9000 (e RX 6000 a partir da v$VersaoModRdna2). Confira a lista atual em $UrlMod"
        }
    } else {
        Info "outra placa no PC: $nome"
    }
}
if (-not $radeon -and $placas.Count -gt 0) {
    Aviso "nenhuma Radeon coberta pelo mod apareceu. Se ela existe, confira o Gerenciador de Dispositivos"
}
$hip = Join-Path ([Environment]::GetFolderPath('System')) 'amdhip64_7.dll'
if (Test-Path $hip) { Ok "runtime HIP do driver Adrenalin presente (amdhip64_7.dll)" }
elseif ($rdna34) { Falta "runtime HIP (amdhip64_7.dll em System32): instale o driver Adrenalin 26.1.1 ou mais novo" }
elseif ($rdna2) { Info "RX 6000: o runtime vem do HIP SDK 7.2 ($UrlHipSdk); este script não confere qual DLL ele instala - o log do mod diz se faltou" }

# ------------------------------------------------------------------ 2. arquivos do programa (bin\)
Titulo "2. Arquivos do programa (pasta bin, a de cima)"
foreach ($f in 'DLSS5ForAll.exe','dxgi.dll','dlss5-bridge.addon64','dlss5-feed.addon64','amd_fidelityfx_dx12.dll','ReShade.ini','dlss5-feed.cfg') {
    if (Test-Path (Join-Path $Bin $f)) { Ok "bin\$f" } else { Falta "bin\$f (reinstale o pacote)" }
}
if (Test-Path (Join-Path $Pasta 'dxgi.dll')) { Aviso "há um dxgi.dll AQUI em mod-amd: o instalador do mod foi instalado com o nome errado. Rode-o de novo e responda 'version'" }

# ------------------------------------------------------------------ 3. pasta antiga (1.2/2.0)
$gerados = 'version.dll','dlssnr_on_amd.ini','dlssnr_on_amd_weights.bin'
$modAqui = (Test-Path (Join-Path $Pasta 'version.dll')) -and (Test-Path (Join-Path $Pasta 'dlssnr_on_amd.ini'))
$modAntigo = (Test-Path (Join-Path $Antiga 'version.dll')) -and (Test-Path (Join-Path $Antiga 'dlssnr_on_amd.ini'))
if ($modAntigo -and -not $modAqui) {
    Titulo "3. Mod na pasta antiga (bin-amd)"
    Aviso "o mod está em $Antiga (layout da 1.2/2.0). O programa ainda o carrega de lá, mas o lugar novo é esta pasta"
    if (Pergunta "COPIAR os arquivos do mod de bin-amd para cá agora? (nada é apagado)") {
        foreach ($f in $gerados + 'nvngx_dlssnr.dll','dlssnr_on_amd_setup.exe') {
            $de = Join-Path $Antiga $f
            if (Test-Path $de) {
                try { Copy-Item $de (Join-Path $Pasta $f) -Force; Ok "copiado: $f" } catch { Falta "não consegui copiar ${f}: $($_.Exception.Message)" }
            }
        }
        Info "A pasta bin-amd ficou como estava. Depois de conferir que funciona, você pode apagá-la."
    }
}

# ------------------------------------------------------------------ 4. nvngx_dlssnr.dll ao lado do instalador
Titulo "4. O arquivo que só você tem: nvngx_dlssnr.dll (ao lado do instalador do mod)"
$dlssnr = Join-Path $Pasta 'nvngx_dlssnr.dll'
if (-not (Test-Path $dlssnr)) {
    $noBin = Join-Path $Bin 'nvngx_dlssnr.dll'
    if (Test-Path $noBin) {
        Aviso "nvngx_dlssnr.dll não está aqui, mas está em bin\ (a sua cópia da cadeia NVIDIA)"
        if (Pergunta "Copiar bin\nvngx_dlssnr.dll para cá agora?") {
            try { Copy-Item $noBin $dlssnr -Force; Ok "copiado" } catch { Falta "não consegui copiar: $($_.Exception.Message)" }
        }
    }
}
if (Test-Path $dlssnr) {
    $vi = $null
    try { $vi = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($dlssnr) } catch {}
    $ver = if ($vi) { "$($vi.FileVersion)" } else { '?' }
    Ok "nvngx_dlssnr.dll presente: versão $ver, $([math]::Round((Get-Item $dlssnr).Length/1MB)) MB"
    try {
        $h = (Get-FileHash $dlssnr -Algorithm SHA256).Hash.ToLower()
        if ($h -eq $Sha256Dlssnr) { Ok "é exatamente a build 310.8.0.0 que o setup do mod conhece" }
        else { Info "não é a build 310.8.0.0 exata (hash diferente): o setup do mod confere os tensores um a um e só aceita se todos baterem" }
    } catch {}
} elseif (-not ((Test-Path (Join-Path $Pasta 'dlssnr_on_amd_weights.bin')))) {
    Falta "nvngx_dlssnr.dll (o mesmo arquivo que a cadeia NVIDIA usa; NÃO é distribuído com o pacote). Coloque a sua cópia nesta pasta, ao lado do instalador do mod"
} else {
    Info "nvngx_dlssnr.dll não está aqui, mas os pesos já foram extraídos (só o instalador precisa dele)"
}

# ------------------------------------------------------------------ 5. o que o setup do mod gera
Titulo "5. Arquivos que o instalador do mod gera nesta pasta"
$setupPath = Join-Path $Pasta 'dlssnr_on_amd_setup.exe'
$setupAqui = Test-Path $setupPath
$versaoMod = $null
if ($setupAqui) {
    $vSetup = VersaoDoMod $setupPath
    $hSetup = $null
    try { $hSetup = (Get-FileHash $setupPath -Algorithm SHA256).Hash.ToLower() } catch {}
    $conhecido = if ($hSetup -and $SetupsConhecidos.ContainsKey($hSetup)) { $SetupsConhecidos[$hSetup] } else { $null }
    if (-not $vSetup -and $conhecido) { $vSetup = [version]$conhecido }
    if ($vSetup) {
        $versaoMod = $vSetup
        if ($vSetup -lt $VersaoMinimaMod) { Aviso "dlssnr_on_amd_setup.exe é a v$vSetup, antiga demais. Baixe a mais nova em $UrlMod" }
        elseif ($conhecido) { Ok "dlssnr_on_amd_setup.exe v$vSetup (arquivo publicado no GitHub, hash conferido)" }
        else { Ok "dlssnr_on_amd_setup.exe v$vSetup"; Info "hash fora da lista que este script conhece: normal numa versão mais nova. Confira que veio de $UrlMod" }
    } else {
        Aviso "dlssnr_on_amd_setup.exe está aqui, mas não consegui ler a versão dele. Confira se é o arquivo da página oficial: $UrlMod"
    }
}
$faltaGerado = $false
foreach ($f in $gerados) {
    if (Test-Path (Join-Path $Pasta $f)) { Ok $f } else { Falta "$f (em bin\mod-amd)"; $faltaGerado = $true }
}
if ($faltaGerado) {
    Write-Host ""
    if ($setupAqui) {
        Info "O dlssnr_on_amd_setup.exe já está nesta pasta. Dê dois cliques NELE (este script não o executa por você)."
        Info "Ele lê o nvngx_dlssnr.dll daqui, extrai os pesos e grava os três arquivos acima AQUI."
    } else {
        Info "Baixe o instalador do mod (o dlssnr_on_amd_setup.exe) SOMENTE da página oficial:"
        Info "   $UrlMod"
        Info "copie-o para ESTA pasta (bin\mod-amd), com o nvngx_dlssnr.dll ao lado, e dê dois cliques nele aqui."
    }
    Write-Host ""
    Aviso "Se o instalador perguntar com que nome instalar ('Install as dxgi.dll?'), responda 'version'. O dxgi.dll de bin\ é o ReShade."
    Info "Antivírus: o instalador do mod tem detecções conhecidas; é você quem decide confiar. Ponha esta pasta nas exclusões antes."
}
if (Test-Path (Join-Path $Pasta 'version.dll')) {
    $vDll = VersaoDoMod (Join-Path $Pasta 'version.dll')
    if ($vDll) {
        $versaoMod = $vDll
        if ($vDll -lt $VersaoMinimaMod) { Aviso "version.dll instalado é o mod v$vDll, antigo demais. Baixe o instalador novo em $UrlMod" }
        else { Ok "mod instalado: v$vDll (lido do version.dll)" }
    } else { Info "não consegui ler a versão do mod no version.dll (não é um problema por si só)" }
}
if ($script:placa9060 -and $versaoMod -and $versaoMod -lt $VersaoMod9060) {
    Aviso "RX 9060 XT com mod v${versaoMod}: essa versão não tem os kernels da gfx1200. Precisa da v$VersaoMod9060 ou mais nova ($UrlMod)"
}
if ($rdna2 -and -not $rdna34 -and $versaoMod -and $versaoMod -lt $VersaoModRdna2) {
    Aviso "RX 6000 com mod v${versaoMod}: a RX 6000 só entra na v$VersaoModRdna2 ($UrlMod)"
}

$ini = Join-Path $Pasta 'dlssnr_on_amd.ini'
if (Test-Path $ini) {
    $linhas = Get-Content $ini
    function Chave($k) { $l = $linhas | Where-Object { $_ -match "^\s*$k\s*=" } | Select-Object -First 1; if ($l) { return ($l -split '=',2)[1].Trim() } else { return $null } }
    $inline = Chave 'Inline'
    if ($inline -eq '1') { Ok "dlssnr_on_amd.ini: Inline=1 (obrigatório: a janela do programa é D3D11)" }
    else {
        Aviso "dlssnr_on_amd.ini: Inline=$inline (precisa ser 1)"
        if (Pergunta "Trocar para Inline=1 agora?") {
            $novo = @(); $achou = $false
            foreach ($l in $linhas) { if ($l -match '^\s*Inline\s*=') { $novo += 'Inline=1'; $achou = $true } else { $novo += $l } }
            if (-not $achou) { $novo += 'Inline=1' }
            try { Set-Content -Path $ini -Value $novo -Encoding ASCII; Ok "gravado Inline=1"; $linhas = $novo } catch { Falta "não consegui gravar o ini: $($_.Exception.Message)" }
        }
    }
    foreach ($k in 'Interop','UseFsrInputs','UseDepth','Enabled') {
        $v = Chave $k
        if ($v -eq '1' -or $null -eq $v) { Ok "dlssnr_on_amd.ini: $k=$(if ($null -eq $v) { '(padrão 1)' } else { $v })" }
        else { Aviso "dlssnr_on_amd.ini: $k=$v (o programa espera 1)" }
    }
    $wait = Chave 'InlineWaitMs'
    if ($null -eq $wait -or [int]$wait -gt 60) {
        if ($null -eq $wait) { Aviso "dlssnr_on_amd.ini: sem InlineWaitMs (o mod usa o padrão dele, alto). Testamos 25" }
        else { Aviso "dlssnr_on_amd.ini: InlineWaitMs=$wait é alto: um quadro lento segura a imagem por até $wait ms. Testamos 25" }
        if (Pergunta "Repor InlineWaitMs=25 agora?") {
            $novo = @(); $achou = $false
            foreach ($l in $linhas) { if ($l -match '^\s*InlineWaitMs\s*=') { $novo += 'InlineWaitMs=25'; $achou = $true } else { $novo += $l } }
            if (-not $achou) { $novo += 'InlineWaitMs=25' }
            try { Set-Content -Path $ini -Value $novo -Encoding ASCII; Ok "gravado InlineWaitMs=25" } catch { Falta "não consegui gravar o ini: $($_.Exception.Message)" }
        }
    } else { Ok "dlssnr_on_amd.ini: InlineWaitMs=$wait" }
}

# ------------------------------------------------------------------ 6. cfg do Feeder: o programa cuida
Titulo "6. dlss5-feed.cfg (bin\) - o programa ajusta sozinho"
Info "Numa pasta só, o programa grava amd_carrier=1 e amd_decoy_swapchain=1 (e o resto do perfil AMD)"
Info "ao ABRIR no modo AMD, e devolve os valores do NVIDIA ao voltar. Não edite essas chaves à mão."
$iniApp = Join-Path $Bin 'dlss5forall.ini'
if (Test-Path $iniApp) {
    $m = (Get-Content $iniApp | Where-Object { $_ -match '^\s*modo\s*=' } | Select-Object -First 1)
    if ($m) { Info "última abertura registrada: $($m.Trim()) ([modo_pasta_unica] no dlss5forall.ini)" }
}

# ------------------------------------------------------------------ 7. logs, se já rodou
Titulo "7. Logs da última execução (se houver)"
$logApp = Join-Path $Bin 'dlss5forall.log'
if (Test-Path $logApp) {
    $la = Get-Content $logApp -Raw
    $mm = [regex]::Matches($la, 'mod AMD: [^\r\n]*')
    if ($mm.Count -gt 0) { foreach ($x in $mm) { Info "programa: $($x.Value)" } }
    else { Info "dlss5forall.log sem linhas 'mod AMD:' (a última abertura foi no modo NVIDIA ou é de antes da 2.1)" }
}
$logMod = Join-Path $Pasta 'dlssnr_on_amd.log'
if (-not (Test-Path $logMod) -and (Test-Path (Join-Path $Bin 'dlssnr_on_amd.log'))) { $logMod = Join-Path $Bin 'dlssnr_on_amd.log'; Info "o log do mod está em bin\ (não em mod-amd)" }
if (Test-Path $logMod) {
    $t = Get-Content $logMod -Raw
    if ($t -match 'log in (.+?); settings in ([^\r\n]+)') { Info "o mod diz: log em $($Matches[1]); configurações em $($Matches[2])" }
    if ($t -match 'network job \d+ done in (\d+) ms') { Ok "mod: a rede neural rodou (último job em $($Matches[1]) ms)" }
    elseif ($t -match 'engine init ok') { Aviso "mod: motor iniciou, mas nenhum 'network job' apareceu (o quadro não está chegando)" }
    else { Aviso "mod: não achei 'engine init ok' no log" }
    if ($t -match 'UNSUPPORTED GPU ARCHITECTURE\s*(\S+)') { Falta "mod: a versão instalada não cobre esta placa ($($Matches[1])). Veja a lista atual em $UrlMod" }
    elseif ($t -match 'invalid kernel file') { Falta "mod: 'invalid kernel file' = esta versão do mod não tem os kernels da sua placa ($UrlMod)" }
    if ($t -match 'amdhip64[^\r\n]*not found') { Falta "mod: runtime HIP não encontrado (Adrenalin na RX 7000/9000; HIP SDK 7.2 na RX 6000: $UrlHipSdk)" }
    if ($t -match 'unsupported colour format (\d+)') { Aviso "mod: rejeitou o formato de cor $($Matches[1]). Troque amd_color_fmt=10 em bin\dlss5-feed.cfg" }
    if ($t -match 'FSR is not active') { Aviso "mod: não viu o FSR ativo. O programa abriu no modo AMD? (veja as linhas 'mod AMD:' acima)" }
    elseif (($t -match 'setup failed; idle') -or (($t -match 'route backbuffer') -and ($t -notmatch 'route fsr'))) {
        Aviso "mod: não pegou o FSR ('setup failed; idle' / 'route backbuffer'). Confira se o programa abriu no modo AMD e se amd_fidelityfx_dx12.dll está em bin\"
    }
    if ($t -match 'inline: (\d+) GPU wait timeouts') { Aviso "mod: $($Matches[1]) estouro(s) de espera da GPU (rede lenta para o teto). Baixe a resolução do DLSS no painel (Insert)" }
    if ($t -match 'no HIP device matches') { Aviso "mod: a placa que renderiza não é a Radeon. Escolha a Radeon em 'Placa de vídeo que roda o filtro'" }
    if (($t -match 'hooked IDXGISwapChain::Present') -and ($t -notmatch 'engine init') -and ($t -notmatch 'ffxDispatch')) {
        Falta "mod: o log para nos hooks de DXGI, sem 'engine init' e sem hook do FFX: o portador FSR não chegou (amd_decoy_swapchain=1 no modo AMD)"
    }
    if ($t -match 'FAULT: exception') { Falta "mod: registrou uma exceção (FAULT). Veja o fim do dlssnr_on_amd.log" }
} else { Info "sem dlssnr_on_amd.log ainda (aparece depois da primeira abertura no modo AMD com o mod instalado)" }
$logFeed = Join-Path $Bin 'dlss5-feed.log'
if (Test-Path $logFeed) {
    $t = Get-Content $logFeed -Raw
    if ($t -match 'frame delivered') { Ok "Feeder: entregou quadros" }
    else { Info "Feeder: sem 'frame delivered' no último log" }
}

# ------------------------------------------------------------------ resumo
Write-Host ""
Write-Host "====================================================================="
if ($faltando.Count -eq 0) {
    Write-Host "  TUDO NO LUGAR. Abra o programa pelo DLSS5ForAll.exe da pasta principal e clique em INICIAR;" -ForegroundColor Green
    Write-Host "  em 'Placa de vídeo que roda o filtro', escolha a Radeon. F6 mostra o antes e depois."
} else {
    Write-Host "  FALTA:" -ForegroundColor Red
    foreach ($f in $faltando) { Write-Host "   - $f" -ForegroundColor Red }
}
if ($avisos.Count -gt 0) {
    Write-Host "  AVISOS:" -ForegroundColor Yellow
    foreach ($a in $avisos) { Write-Host "   - $a" -ForegroundColor Yellow }
}
Write-Host "  Tudo explicado no LEIA-ME.txt desta pasta (English: README.txt). Isto é experimental."
Write-Host "====================================================================="
