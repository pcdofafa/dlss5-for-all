# Procura a COSTURA do modo comparativo: uma descontinuidade vertical no meio da imagem.
# Mede, para cada coluna x, a diferenca media entre a coluna x e a x-1; depois compara o
# pico perto do meio com a mediana das demais colunas. Sem modo comparativo nao ha pico.
param([string]$Img)
Add-Type -AssemblyName System.Drawing
$i = [System.Drawing.Bitmap]::FromFile($Img)
$w = $i.Width; $h = $i.Height
$r = New-Object System.Drawing.Rectangle 0,0,$w,$h
$d = $i.LockBits($r,'ReadOnly','Format32bppArgb')
$n = $d.Stride * $h
$b = New-Object byte[] $n
[System.Runtime.InteropServices.Marshal]::Copy($d.Scan0,$b,0,$n)
$i.UnlockBits($d); $i.Dispose()
$col = New-Object double[] $w
for ($x=1; $x -lt $w; $x++) {
  [double]$s = 0; [int]$c = 0
  for ($y=0; $y -lt $h; $y+=3) {
    $o = $y*$d.Stride + $x*4
    $p = $o - 4
    for ($k=0; $k -lt 3; $k++) { $s += [Math]::Abs([int]$b[$o+$k] - [int]$b[$p+$k]); $c++ }
  }
  $col[$x] = $s / $c
}
$meio = [int]($w/2)
$janela = 6
$pico = 0.0; $picoX = 0
for ($x=$meio-$janela; $x -le $meio+$janela; $x++) { if ($col[$x] -gt $pico) { $pico = $col[$x]; $picoX = $x } }
$fora = @(); for ($x=20; $x -lt $w-20; $x++) { if ([Math]::Abs($x-$meio) -gt 20) { $fora += $col[$x] } }
$fora = $fora | Sort-Object
$med = $fora[[int]($fora.Count/2)]
$p99 = $fora[[int]($fora.Count*0.99)]
Write-Output ("{0}" -f (Split-Path $Img -Leaf))
Write-Output ("  coluna do meio  x={0}  salto {1:N2}/255" -f $picoX, $pico)
Write-Output ("  demais colunas  mediana {0:N2}  p99 {1:N2}" -f $med, $p99)
Write-Output ("  razao pico/mediana  {0:N1}x   (acima de p99? {1})" -f ($pico/$med), ($pico -gt $p99))
