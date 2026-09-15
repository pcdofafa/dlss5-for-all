# Cena de teste para a auditoria: detalhe em VARIAS escalas (nao so 1 px), que e o que a
# rede neural muda de forma mensuravel sem que um deslocamento de 1 px estrague a conta.
param([int]$W = 1600,[int]$H = 900)
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap $W,$H,([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'
$g.TextRenderingHint = 'AntiAliasGridFit'

# fundo: gradiente diagonal
$br = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.Point 0,0), (New-Object System.Drawing.Point $W,$H),
        [System.Drawing.Color]::FromArgb(255,24,32,52),
        [System.Drawing.Color]::FromArgb(255,128,112,86))
$g.FillRectangle($br,0,0,$W,$H)

# circulos concentricos: bordas curvas em varias frequencias
$rnd = New-Object System.Random 7
for ($i=1; $i -le 26; $i++) {
  $p = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, (40+$i*8), (200-$i*6), (90+$i*5))), (1 + ($i % 4))
  $r = $i * 16
  $g.DrawEllipse($p, 300-$r, 450-$r, $r*2, $r*2)
  $p.Dispose()
}
# barras diagonais finas: alta frequencia mas orientada
for ($i=0; $i -lt 60; $i++) {
  $p = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255,235,235,245)), 2
  $g.DrawLine($p, (700 + $i*9), 80, (760 + $i*9), 460)
  $p.Dispose()
}
# blocos de ruido 4x4: textura, que e o que a rede reconstroi
for ($by=520; $by -lt 860; $by+=4) {
  for ($bx=700; $bx -lt 1540; $bx+=4) {
    $v = $rnd.Next(30,225)
    $b2 = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255,$v,[int]($v*0.8),[int](255-$v*0.6)))
    $g.FillRectangle($b2,$bx,$by,4,4)
    $b2.Dispose()
  }
}
# texto em varios tamanhos: bordas de alto contraste
foreach ($t in @(@(60,60,760),@(34,60,830),@(20,300,860))) {
  $f = New-Object System.Drawing.Font "Consolas",$t[0]
  $g.DrawString("DLSS5 AUDITORIA 0123456789", $f, [System.Drawing.Brushes]::White, [single]$t[1], [single]$t[2])
  $f.Dispose()
}
# degraus de cinza: qualquer mudanca de tom aparece aqui
for ($i=0; $i -lt 16; $i++) {
  $v = $i * 17
  $b3 = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255,$v,$v,$v))
  $g.FillRectangle($b3, (60 + $i*40), 60, 38, 60)
  $b3.Dispose()
}
$g.Dispose()
$out = Join-Path $PSScriptRoot "cena.png"
$bmp.Save($out,[System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "gerada: $out  ${W}x${H}"
