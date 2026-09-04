# =====================================================================================
#  publicar.ps1 — põe a página /dlss no ar, por fases.
#
#  NADA aqui foi executado. Cada fase é independente e pode ser rodada sozinha.
#
#      .\publicar.ps1 backend    cria o D1, aplica o schema, gera os secrets e deploya
#                                o worker novo `fafa-dlss-api`. Não toca em nada existente.
#      .\publicar.ps1 pagina     publica o HTML em pcdofafa.com.br/live/dlss
#      .\publicar.ps1 testar     bateria de testes CONTRA O QUE ESTÁ NO AR
#      .\publicar.ps1 alias      (opcional, por último) acrescenta a rota /dlss no worker
#                                de produção, com paridade conferida, backup e reversão
#                                automática se qualquer rota antiga cair
#
#  Ordem recomendada:  backend -> pagina -> testar -> alias -> testar
#
#  Requisitos: Node/npx, wrangler logado (`npx wrangler whoami`), e py no PATH.
# =====================================================================================

param(
  [Parameter(Position = 0)]
  [ValidateSet("backend", "pagina", "testar", "alias")]
  [string]$Fase
)

$ErrorActionPreference = "Stop"

$RAIZ      = Split-Path -Parent $MyInvocation.MyCommand.Path
$BACKEND   = Join-Path $RAIZ "backend"
$PAGINA    = Join-Path $RAIZ "dlss.html"
$BOTCORE   = "C:\Users\PCDOFAFA\Documents\fafa-bot-core"
$PRODUCAO  = "C:\Users\PCDOFAFA\Documents\fafa-attribution\workers\ecurtadorpcdofafa-producao"
$SITE      = "https://pcdofafa.com.br"
$UA        = "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"   # o Cloudflare bloqueia UA de script

function Titulo($t) { Write-Host ""; Write-Host "=== $t ===" -ForegroundColor Cyan }
function Ok($t)     { Write-Host "  [ok]   $t" -ForegroundColor Green }
function Aviso($t)  { Write-Host "  [!]    $t" -ForegroundColor Yellow }
function Erro($t)   { Write-Host "  [ERRO] $t" -ForegroundColor Red }

function Aleatorio([int]$bytes = 32) {
  $b = New-Object byte[] $bytes
  [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b)
  return [Convert]::ToBase64String($b).Replace("+", "-").Replace("/", "_").TrimEnd("=")
}

function Codigo($url) {
  try { (Invoke-WebRequest -Uri $url -UserAgent $UA -Method Get -TimeoutSec 20 -SkipHttpErrorCheck).StatusCode }
  catch { 0 }
}

# =====================================================================================
if ($Fase -eq "backend") {
  Titulo "1. Banco D1 + worker fafa-dlss-api"
  Set-Location $BACKEND

  $toml = Get-Content "wrangler.toml" -Raw
  if ($toml -match 'database_id\s*=\s*"PREENCHER') {
    Write-Host "  criando o banco pcdofafa-dlss..."
    $saida = npx wrangler d1 create pcdofafa-dlss 2>&1 | Out-String
    Write-Host $saida
    $m = [regex]::Match($saida, '([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})')
    if (-not $m.Success) { Erro "não consegui ler o database_id da saída. Cole à mão em wrangler.toml e rode de novo."; exit 1 }
    $id = $m.Groups[1].Value
    ($toml -replace 'PREENCHER-APOS-wrangler-d1-create', $id) | Set-Content "wrangler.toml" -Encoding utf8
    Ok "database_id $id gravado em wrangler.toml"
  } else {
    Aviso "wrangler.toml já tem um database_id — pulando a criação"
  }

  Write-Host "  aplicando o schema..."
  npx wrangler d1 execute pcdofafa-dlss --remote --file=schema.sql
  if (-not $?) { Erro "falha ao aplicar o schema"; exit 1 }
  Ok "5 tabelas criadas (idempotente: rodar de novo não apaga nada)"

  Titulo "2. Secrets"
  $salt  = Aleatorio 32
  $admin = Aleatorio 24
  $salt  | npx wrangler secret put IP_SALT
  $admin | npx wrangler secret put ADMIN_TOKEN
  Ok "IP_SALT gerado (32 bytes aleatórios) — não precisa ser guardado por ninguém"

  Write-Host ""
  Write-Host "  ------------------------------------------------------------------" -ForegroundColor Yellow
  Write-Host "   GUARDE ISTO AGORA. É a senha da moderação e não aparece de novo:" -ForegroundColor Yellow
  Write-Host ""
  Write-Host "   $SITE/dlss-api/admin?token=$admin" -ForegroundColor White
  Write-Host ""
  Write-Host "   Quem tem esse link modera. Não poste no grupo, não mande em print." -ForegroundColor Yellow
  Write-Host "   Para trocar: npx wrangler secret put ADMIN_TOKEN (o antigo morre na hora)." -ForegroundColor Yellow
  Write-Host "  ------------------------------------------------------------------" -ForegroundColor Yellow
  Write-Host ""

  Aviso "Opcional: avisos no Telegram a cada avaliação nova. Sem isso, a fila fica sem ninguém olhando:"
  Write-Host "    npx wrangler secret put TG_BOT_TOKEN     (token real do bot)"
  Write-Host "    npx wrangler secret put TG_ADMIN_CHAT    (o seu chat id no Telegram)"

  Titulo "3. Deploy do worker novo"
  npx wrangler deploy --dry-run --outdir .dryrun | Out-Null
  npx wrangler deploy
  if (-not $?) { Erro "falha no deploy"; exit 1 }
  Ok "fafa-dlss-api no ar em $SITE/dlss-api/*"
  Aviso "O worker do encurtador NÃO foi tocado: a rota é declarada por este wrangler.toml."

  Write-Host ""
  Write-Host "  conferindo..."
  $c = Codigo "$SITE/dlss-api/estado"
  if ($c -eq 200) { Ok "/dlss-api/estado responde 200" } else { Erro "/dlss-api/estado respondeu $c" }
  exit 0
}

# =====================================================================================
if ($Fase -eq "pagina") {
  Titulo "Publicando a página em $SITE/live/dlss"

  $html = Get-Content $PAGINA -Raw
  if ($html -match 'url:\s*""') {
    Aviso "nenhum espelho tem URL — a página vai ao ar em 'Download em breve'."
    Aviso "É o estado honesto e combinado. Para ativar o download depois, preencha a"
    Aviso "url do array ESPELHOS (dlss.html, linha ~62) e rode esta fase de novo."
  }

  Set-Location $BOTCORE
  py tools/subir_live.py $PAGINA dlss --titulo "DLSS5 For All"
  if (-not $?) { Erro "falha ao publicar"; exit 1 }

  $c = Codigo "$SITE/live/dlss"
  if ($c -eq 200) { Ok "$SITE/live/dlss responde 200" } else { Erro "respondeu $c" }
  exit 0
}

# =====================================================================================
if ($Fase -eq "testar") {
  Titulo "Bateria contra o que está NO AR"
  $api = "$SITE/dlss-api"

  function Post($rota, $corpo, $ua) {
    try {
      Invoke-RestMethod -Uri "$api/$rota" -Method Post -UserAgent $ua `
        -ContentType "application/json" -Body ($corpo | ConvertTo-Json -Compress) -TimeoutSec 20
    } catch { $null }
  }
  function Get1($ua) {
    try { Invoke-RestMethod -Uri "$api/estado" -UserAgent $ua -TimeoutSec 20 } catch { $null }
  }

  $uaA = "$UA TesteA"; $uaB = "$UA TesteB"

  $e0 = Get1 $uaA
  if (-not $e0) { Erro "/estado não respondeu — o backend está no ar?"; exit 1 }
  Ok "estado inicial: $($e0.downloads.total) downloads, $($e0.avaliacao.votos) votos"

  $d1 = Post "download" @{ espelho = "drive" } $uaA
  $d2 = Post "download" @{ espelho = "drive" } $uaA
  if ($d1.contado -eq $true -and $d2.contado -eq $false) { Ok "contador: 1º clique conta, 2º do mesmo visitante não (dedupe 6 h)" }
  else { Erro "dedupe do download não funcionou: $($d1.contado) / $($d2.contado)" }

  $eR = Get1 $uaA
  if ($eR.downloads.total -eq $d2.downloads.total) { Ok "recarregar a página não altera o contador" }
  else { Erro "o total mudou só recarregando" }

  $v1 = Post "voto" @{ nota = 5 } $uaA
  $v2 = Post "voto" @{ nota = 2 } $uaA
  if ($v1.avaliacao.votos -eq $v2.avaliacao.votos -and $v2.avaliacao.meu_voto -eq 2) { Ok "voto entra e TROCA sem virar dois votos" }
  else { Erro "troca de voto contou duas vezes" }

  $a1 = Post "avaliacao" @{ nota = 4; nome = "Teste"; texto = "avaliacao de teste da bateria automatica"; t = 9000 } $uaB
  if ($a1.estado -eq "pendente") { Ok "avaliação nasce PENDENTE (não aparece sem aprovação)" } else { Erro "avaliação não nasceu pendente" }

  $hp = Post "avaliacao" @{ nota = 5; nome = "robo"; texto = "spam do honeypot"; website = "http://x"; t = 9000 } $uaA
  $eH = Get1 $uaA
  if ($hp.ok -and ($eH.avaliacoes | Where-Object { $_.texto -like "*honeypot*" }).Count -eq 0) { Ok "honeypot: resposta de sucesso, mas nada gravado" }
  else { Erro "honeypot deixou passar" }

  $du = Post "avaliacao" @{ nota = 5; nome = "Outro"; texto = "AVALIACAO DE TESTE DA BATERIA AUTOMATICA!!!"; t = 9000 } $uaA
  if ($du.ok) { Ok "texto repetido de outro visitante: aceito na resposta e descartado (confira em /admin)" }

  $x = Post "avaliacao" @{ nota = 5; nome = "<img src=x onerror=alert(1)>"; texto = "<script>alert(1)</script> teste de escape"; t = 9000 } $uaB
  if ($x.ok) { Ok "XSS enviado — confirme em /admin que aparece escapado, como &lt;img ...&gt;" }

  Write-Host ""
  Aviso "Curtida precisa de avaliação APROVADA. Aprove uma em /dlss-api/admin?token=... e rode:"
  Write-Host "    curl -X POST $api/curtida -H 'Content-Type: application/json' -d '{\"id\":1}'"
  Write-Host "    (o mesmo comando de novo deve DESCURTIR)"
  exit 0
}

# =====================================================================================
if ($Fase -eq "alias") {
  Titulo "Rota /dlss no worker de PRODUÇÃO — a única parte sensível"
  Set-Location $PRODUCAO

  # -------- 1. paridade: a cópia local é mesmo a que está no ar?
  Write-Host "  conferindo se esta cópia é idêntica à de produção..."
  npx wrangler deploy --dry-run --outdir .dryrun-antes | Out-Null
  $local = [IO.File]::ReadAllBytes(".dryrun-antes\worker.js")

  $env_path = Join-Path $BOTCORE ".env"
  $tok = ((Select-String -Path $env_path -Pattern "^CF_API_TOKEN=").Line -split "=", 2)[1].Trim('"', "'", " ")
  $acc = ((Select-String -Path $env_path -Pattern "^CF_ACCOUNT_ID=").Line -split "=", 2)[1].Trim('"', "'", " ")
  $resp = Invoke-WebRequest -Uri "https://api.cloudflare.com/client/v4/accounts/$acc/workers/scripts/ecurtadorpcdofafa" `
            -Headers @{ Authorization = "Bearer $tok" } -UserAgent $UA -TimeoutSec 60
  [IO.File]::WriteAllBytes("$env:TEMP\prod-atual.js", $resp.Content)

  $py = @'
import hashlib, re, sys
from pathlib import Path
bruto = Path(sys.argv[1]).read_bytes()
m = re.search(rb'name="worker\.js"\r?\n(?:[^\r\n]*\r?\n)*?\r?\n(.*?)\r?\n--', bruto, re.S)
corpo = m.group(1) if m else bruto
local = Path(sys.argv[2]).read_bytes()
n = lambda b: b.replace(b"\r\n", b"\n").rstrip(b"\n")
print("IGUAIS" if hashlib.sha256(n(corpo)).hexdigest() == hashlib.sha256(n(local)).hexdigest() else "DIFEREM")
'@
  $py | Out-File "$env:TEMP\par.py" -Encoding utf8
  $r = py "$env:TEMP\par.py" "$env:TEMP\prod-atual.js" ".dryrun-antes\worker.js"

  if ($r -notmatch "IGUAIS") {
    Erro "A cópia local DIVERGE do que está em produção."
    Erro "PARE AQUI. Deployar assim é exatamente o acidente de 24/07/2026."
    exit 1
  }
  Ok "paridade confirmada — esta cópia é a que está no ar"

  # -------- 2. backup
  $carimbo = Get-Date -Format "yyyyMMdd-HHmm"
  $bkp = "worker.js.bak-antes-dlss-$carimbo"
  Copy-Item "worker.js" $bkp
  Ok "backup: $bkp"

  # -------- 3. patch (por âncora, não por número de linha)
  $src = Get-Content "worker.js" -Raw
  if ($src -match '"/dlss"') { Aviso "a rota /dlss já existe — nada a fazer"; exit 0 }

  $ancora = @'
      const slug = pathname.slice("/live/".length);
      return handleLiveGet(slug, env);
    }
'@
  if ($src -notmatch [regex]::Escape($ancora)) { Erro "não achei o bloco /live/<slug>. Aplique à mão."; exit 1 }

  $novo = $ancora + @'

    // GET /dlss — atalho bonito para a página live:dlss (DLSS5 For All)
    if (request.method === "GET" && (pathname === "/dlss" || pathname === "/dlss/")) {
      return handleLiveGet("dlss", env);
    }
'@
  ($src -replace [regex]::Escape($ancora), $novo.Replace('$', '$$')) | Set-Content "worker.js" -Encoding utf8 -NoNewline
  Ok "rota acrescentada (3 linhas, aditivas)"

  # -------- 4. deploy
  npx wrangler deploy
  if (-not $?) {
    Erro "deploy falhou — restaurando o backup"
    Copy-Item $bkp "worker.js" -Force
    exit 1
  }

  # -------- 5. as rotas que já existiam continuam de pé?
  Titulo "Testando o que JÁ EXISTIA"
  Start-Sleep -Seconds 5
  $rotas = @{ "/" = 200; "/dlss" = 200; "/live/dlss" = 200; "/robots.txt" = 200; "/sitemap.xml" = 200; "/p/" = 200 }
  $falhou = $false
  foreach ($r in $rotas.Keys) {
    $c = Codigo "$SITE$r"
    if ($c -eq $rotas[$r]) { Ok "$r -> $c" } else { Erro "$r -> $c (esperado $($rotas[$r]))"; $falhou = $true }
  }

  if ($falhou) {
    Erro "ALGUMA ROTA CAIU. Revertendo agora."
    Copy-Item $bkp "worker.js" -Force
    npx wrangler deploy
    Erro "revertido. AVISE o responsável antes de tentar de novo."
    exit 1
  }
  Ok "tudo de pé. $SITE/dlss no ar."
  Write-Host ""
  Aviso "Commite o worker.js neste repo — fonte sem commit é como o acidente de julho começou."
  exit 0
}

Write-Host "Uso: .\publicar.ps1 [backend|pagina|testar|alias]"
Write-Host "Ordem recomendada: backend -> pagina -> testar -> alias -> testar"
