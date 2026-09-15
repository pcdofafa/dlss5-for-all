# img.ps1 -- medidor de imagem da auditoria. Dot-source.
#   Img-Comparar A B   ->  alinha (busca dx,dy) e devolve MAE, % de pixels diferentes, PSNR
# O alinhamento existe porque a janela do alvo e o backbuffer da pelicula nem sempre tem a
# mesma origem; sem ele, um deslocamento de 1 px vira "a imagem mudou" numa cena com detalhe.

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public class ImgCmp {
    public static byte[] Ler(string caminho, out int w, out int h, out int stride) {
        using (Bitmap b = (Bitmap)Image.FromFile(caminho)) {
            w = b.Width; h = b.Height;
            Rectangle r = new Rectangle(0, 0, w, h);
            BitmapData d = b.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            stride = d.Stride;
            byte[] px = new byte[stride * h];
            Marshal.Copy(d.Scan0, px, 0, px.Length);
            b.UnlockBits(d);
            return px;
        }
    }

    // MAE por canal entre A e B com B deslocado de (dx,dy). Amostra 1 em cada 'passo'.
    public static double Mae(byte[] a, int aw, int ah, int astr,
                             byte[] b, int bw, int bh, int bstr,
                             int dx, int dy, int margem, int passo) {
        double soma = 0; long n = 0;
        for (int y = margem; y < ah - margem; y += passo) {
            int yb = y + dy;
            if (yb < 0 || yb >= bh) continue;
            for (int x = margem; x < aw - margem; x += passo) {
                int xb = x + dx;
                if (xb < 0 || xb >= bw) continue;
                int oa = y * astr + x * 4, ob = yb * bstr + xb * 4;
                soma += Math.Abs(a[oa] - b[ob]) + Math.Abs(a[oa+1] - b[ob+1]) + Math.Abs(a[oa+2] - b[ob+2]);
                n += 3;
            }
        }
        return n > 0 ? soma / n : 999.0;
    }

    // Percentual de pixels com diferenca acima do limiar em algum canal, e MSE para o PSNR.
    public static double[] Detalhe(byte[] a, int aw, int ah, int astr,
                                   byte[] b, int bw, int bh, int bstr,
                                   int dx, int dy, int margem, int limiar) {
        long dif = 0, tot = 0; double soma = 0, mse = 0; int pico = 0;
        for (int y = margem; y < ah - margem; y++) {
            int yb = y + dy;
            if (yb < 0 || yb >= bh) continue;
            for (int x = margem; x < aw - margem; x++) {
                int xb = x + dx;
                if (xb < 0 || xb >= bw) continue;
                int oa = y * astr + x * 4, ob = yb * bstr + xb * 4;
                int m = 0;
                for (int c = 0; c < 3; c++) {
                    int d = a[oa+c] - b[ob+c];
                    if (d < 0) d = -d;
                    soma += d; mse += (double)d * d;
                    if (d > m) m = d;
                }
                if (m > pico) pico = m;
                if (m > limiar) dif++;
                tot++;
            }
        }
        if (tot == 0) return new double[] { 999, 100, 0, 0 };
        double mae = soma / (tot * 3);
        mse = mse / (tot * 3);
        double psnr = mse > 0 ? 10.0 * Math.Log10(255.0 * 255.0 / mse) : 99.0;
        return new double[] { mae, 100.0 * dif / tot, psnr, pico };
    }
}
'@ -ReferencedAssemblies System.Drawing

function Img-Comparar([string]$A,[string]$B,[int]$Limiar=8,[int]$Busca=10,[switch]$Silencioso) {
    $aw=0;$ah=0;$astr=0;$bw=0;$bh=0;$bstr=0
    $pa = [ImgCmp]::Ler($A,[ref]$aw,[ref]$ah,[ref]$astr)
    $pb = [ImgCmp]::Ler($B,[ref]$bw,[ref]$bh,[ref]$bstr)
    $margem = 24
    $melhor = 1e9; $mdx=0; $mdy=0
    for ($dy = -$Busca; $dy -le $Busca; $dy++) {
      for ($dx = -$Busca; $dx -le $Busca; $dx++) {
        $m = [ImgCmp]::Mae($pa,$aw,$ah,$astr,$pb,$bw,$bh,$bstr,$dx,$dy,$margem,4)
        if ($m -lt $melhor) { $melhor = $m; $mdx = $dx; $mdy = $dy }
      }
    }
    $r = [ImgCmp]::Detalhe($pa,$aw,$ah,$astr,$pb,$bw,$bh,$bstr,$mdx,$mdy,$margem,$Limiar)
    $o = [pscustomobject]@{
        A = (Split-Path $A -Leaf); B = (Split-Path $B -Leaf)
        TamA = "${aw}x${ah}"; TamB = "${bw}x${bh}"
        dx = $mdx; dy = $mdy
        MAE = [math]::Round($r[0],3)
        PctDif = [math]::Round($r[1],2)
        PSNR = [math]::Round($r[2],2)
        Pico = [int]$r[3]
    }
    if (-not $Silencioso) {
        Write-Host ("  {0}  vs  {1}" -f $o.A, $o.B) -ForegroundColor Gray
        Write-Host ("    {0} / {1}  alinhamento dx={2} dy={3}" -f $o.TamA,$o.TamB,$o.dx,$o.dy) -ForegroundColor DarkGray
        Write-Host ("    MAE {0}/255   pixels diferentes (>{1}) {2}%   PSNR {3} dB   pico {4}" -f $o.MAE,$Limiar,$o.PctDif,$o.PSNR,$o.Pico) -ForegroundColor White
    }
    return $o
}
Write-Host "medidor de imagem carregado" -ForegroundColor Green
