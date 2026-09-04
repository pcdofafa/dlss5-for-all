# =====================================================================================
#  gerar-icone.ps1  -  monta o DLSS5ForAll.ico a partir da foto do autor
#
#  Roda com:  powershell -ExecutionPolicy Bypass -File .\marca\gerar-icone.ps1
#  Sai em:    marca\DLSS5ForAll.ico  (+ os PNGs de cada tamanho, para o site e o setup)
#
#  Por que um script e nao um .ico solto no repositorio: o icone e DERIVADO da foto.
#  Trocar a foto, o enquadramento ou a cor do anel e mudar uma linha aqui e rodar de novo,
#  em vez de reabrir um editor de imagem e refazer nove tamanhos na mao.
# =====================================================================================
param(
    [string]$Foto   = "$env:USERPROFILE\Downloads\logo dlss5.png",
    [string]$Estilo = 'escuro',      # escuro | gradiente | quadrado
    [string]$Saida  = $null
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$Raiz = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Saida) { $Saida = Join-Path $Raiz 'DLSS5ForAll.ico' }
if (-not (Test-Path $Foto)) { throw "foto nao encontrada: $Foto" }

# ------------------------------------------------------------------ paleta (marca\PALETA.md)
$cianoClaro = [System.Drawing.Color]::FromArgb(255, 0x22, 0xd3, 0xee)
$ciano      = [System.Drawing.Color]::FromArgb(255, 0x08, 0x91, 0xb2)
$azul       = [System.Drawing.Color]::FromArgb(255, 0x25, 0x63, 0xeb)
$escuro     = [System.Drawing.Color]::FromArgb(255, 0x0b, 0x12, 0x20)

# ------------------------------------------------------------------ recorte do fundo + escrita do .ico
Add-Type -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.IO;

public class IconeUtil {
    // Tira o fundo por FLOOD FILL a partir das bordas, nao por limiar global.
    // Limiar global comeria o branco da esclera dos olhos - e os olhos arregalados sao
    // justamente o traco que mantem o icone vivo a 16px.
    public static Bitmap RecortaFundo(string caminho, int limiar, out Rectangle caixa) {
        Bitmap orig = new Bitmap(caminho);
        int W = orig.Width, H = orig.Height;
        Bitmap dst = new Bitmap(W, H, PixelFormat.Format32bppArgb);
        Rectangle r = new Rectangle(0, 0, W, H);
        BitmapData sd = orig.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        int stride = sd.Stride;
        byte[] buf = new byte[stride * H];
        Marshal.Copy(sd.Scan0, buf, 0, buf.Length);
        orig.UnlockBits(sd);

        bool[] fundo = new bool[W * H];
        Queue<int> fila = new Queue<int>();
        for (int x = 0; x < W; x++) { Semeia(buf, fundo, fila, stride, W, x, 0, limiar); Semeia(buf, fundo, fila, stride, W, x, H - 1, limiar); }
        for (int y = 0; y < H; y++) { Semeia(buf, fundo, fila, stride, W, 0, y, limiar); Semeia(buf, fundo, fila, stride, W, W - 1, y, limiar); }
        int[] dx = { 1, -1, 0, 0 };
        int[] dy = { 0, 0, 1, -1 };
        while (fila.Count > 0) {
            int idx = fila.Dequeue();
            int x0 = idx % W, y0 = idx / W;
            for (int k = 0; k < 4; k++) {
                int nx = x0 + dx[k], ny = y0 + dy[k];
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                Semeia(buf, fundo, fila, stride, W, nx, ny, limiar);
            }
        }

        byte[] dbuf = new byte[stride * H];
        int minX = W, maxX = -1, minY = H, maxY = -1;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int i = y * stride + x * 4;
                int idx = y * W + x;
                dbuf[i] = buf[i]; dbuf[i+1] = buf[i+1]; dbuf[i+2] = buf[i+2];
                if (fundo[idx]) { dbuf[i+3] = 0; continue; }
                int viz = 0, tot = 0;
                for (int ay = -1; ay <= 1; ay++) for (int ax = -1; ax <= 1; ax++) {
                    if (ax == 0 && ay == 0) continue;
                    int nx = x + ax, ny = y + ay;
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    tot++;
                    if (fundo[ny * W + nx]) viz++;
                }
                dbuf[i+3] = (byte)(viz > 0 ? 255 - (int)(255.0 * viz / tot * 0.55) : 255);
                if (x < minX) minX = x; if (x > maxX) maxX = x;
                if (y < minY) minY = y; if (y > maxY) maxY = y;
            }
        }
        BitmapData dd = dst.LockBits(r, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        Marshal.Copy(dbuf, 0, dd.Scan0, dbuf.Length);
        dst.UnlockBits(dd);
        orig.Dispose();
        caixa = new Rectangle(minX, minY, maxX - minX + 1, maxY - minY + 1);
        return dst;
    }
    static void Semeia(byte[] buf, bool[] fundo, Queue<int> fila, int stride, int W, int x, int y, int limiar) {
        int idx = y * W + x;
        if (fundo[idx]) return;
        int i = y * stride + x * 4;
        if (buf[i] >= limiar && buf[i+1] >= limiar && buf[i+2] >= limiar) { fundo[idx] = true; fila.Enqueue(idx); }
    }

    // Escreve o .ico multi-resolucao.
    // Ate 64px vai BMP (BITMAPINFOHEADER + BGRA de baixo para cima + mascara AND), que e
    // o que TODO shell do Windows le. 128 e 256 vao PNG, que e o formato esperado desses
    // dois tamanhos desde o Vista e evita um .ico de megabytes.
    public static void Escreve(string caminho, List<Bitmap> imagens) {
        List<byte[]> blocos = new List<byte[]>();
        foreach (Bitmap b in imagens) {
            blocos.Add(b.Width >= 128 ? ComoPng(b) : ComoBmp(b));
        }
        using (FileStream fs = new FileStream(caminho, FileMode.Create, FileAccess.Write))
        using (BinaryWriter w = new BinaryWriter(fs)) {
            w.Write((ushort)0);                    // reservado
            w.Write((ushort)1);                    // tipo: 1 = icone
            w.Write((ushort)imagens.Count);
            int off = 6 + 16 * imagens.Count;
            for (int i = 0; i < imagens.Count; i++) {
                Bitmap b = imagens[i];
                w.Write((byte)(b.Width  >= 256 ? 0 : b.Width));   // 0 quer dizer 256
                w.Write((byte)(b.Height >= 256 ? 0 : b.Height));
                w.Write((byte)0);                  // paleta: 0 = sem paleta
                w.Write((byte)0);                  // reservado
                w.Write((ushort)1);                // planos
                w.Write((ushort)32);               // bits por pixel
                w.Write((uint)blocos[i].Length);
                w.Write((uint)off);
                off += blocos[i].Length;
            }
            foreach (byte[] bl in blocos) w.Write(bl);
        }
    }
    static byte[] ComoPng(Bitmap b) {
        using (MemoryStream ms = new MemoryStream()) { b.Save(ms, ImageFormat.Png); return ms.ToArray(); }
    }
    static byte[] ComoBmp(Bitmap b) {
        int W = b.Width, H = b.Height;
        Rectangle r = new Rectangle(0, 0, W, H);
        BitmapData sd = b.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        int stride = sd.Stride;
        byte[] px = new byte[stride * H];
        Marshal.Copy(sd.Scan0, px, 0, px.Length);
        b.UnlockBits(sd);

        int linhaMascara = ((W + 31) / 32) * 4;   // a mascara AND e alinhada em 4 bytes
        int tamMascara = linhaMascara * H;
        using (MemoryStream ms = new MemoryStream())
        using (BinaryWriter w = new BinaryWriter(ms)) {
            w.Write((uint)40);                     // biSize
            w.Write((int)W);
            w.Write((int)(H * 2));                 // altura dobrada: cor + mascara
            w.Write((ushort)1);
            w.Write((ushort)32);
            w.Write((uint)0);                      // BI_RGB
            w.Write((uint)(W * 4 * H + tamMascara));
            w.Write((int)0); w.Write((int)0); w.Write((uint)0); w.Write((uint)0);
            for (int y = H - 1; y >= 0; y--) w.Write(px, y * stride, W * 4);   // de baixo para cima
            // mascara AND toda zerada: o alfa de 32 bits ja resolve a transparencia, e o
            // Windows so consulta a mascara em modos de cor antigos.
            w.Write(new byte[tamMascara]);
            return ms.ToArray();
        }
    }
}
'@ -ReferencedAssemblies System.Drawing

$caixaSujeito = New-Object System.Drawing.Rectangle
$imgFoto = [IconeUtil]::RecortaFundo($Foto, 245, [ref]$caixaSujeito)
Write-Host "sujeito recortado da foto: $($caixaSujeito.Width)x$($caixaSujeito.Height) em $($caixaSujeito.X),$($caixaSujeito.Y)"

# A CAIXA DA CABECA, medida no perfil da foto: o bone comeca em y=33 e os ombros so
# entram por volta de y=640, quando a largura da linha salta de ~300 para 414 pixels.
# Enquadrar pela caixa do sujeito inteiro puxaria a camiseta para dentro do circulo e
# encolheria o rosto - que e o unico conteudo que importa nos tamanhos pequenos.
$cabeca = New-Object System.Drawing.RectangleF 426, 33, 397, 607

function Desenha-Icone {
    param([int]$S)

    # Enquadramento por tamanho, e a razao esta medida: em 256px o anel de 3% da largura
    # e um detalhe elegante; em 16px o GDI+ arredonda a caneta para 1 pixel inteiro, que
    # vira 6% do icone e come a cara. Abaixo de 32px, portanto, nao ha anel - e a cabeca
    # cresce para ocupar o espaco que ele deixou.
    $pequeno    = ($S -lt 32)
    $anelW      = if ($pequeno) { 0.0 } elseif ($S -lt 64) { 1.0 } else { $S * 0.03 }
    $zoomCabeca = if ($pequeno) { 1.06 } elseif ($S -lt 64) { 0.99 } else { 0.94 }

    $bmp = New-Object System.Drawing.Bitmap $S, $S, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality

    $m = if ($pequeno) { 0.0 } else { $S * 0.015 }
    $d = $S - 2 * $m
    $forma = New-Object System.Drawing.Drawing2D.GraphicsPath
    if ($Estilo -eq 'quadrado') {
        $rr = $S * 0.22
        $forma.AddArc($m, $m, $rr, $rr, 180, 90)
        $forma.AddArc($m + $d - $rr, $m, $rr, $rr, 270, 90)
        $forma.AddArc($m + $d - $rr, $m + $d - $rr, $rr, $rr, 0, 90)
        $forma.AddArc($m, $m + $d - $rr, $rr, $rr, 90, 90)
        $forma.CloseFigure()
    } else {
        $forma.AddEllipse($m, $m, $d, $d)
    }

    $r0 = New-Object System.Drawing.RectangleF 0, 0, $S, $S
    if ($Estilo -eq 'escuro') { $br = New-Object System.Drawing.SolidBrush $escuro }
    else { $br = New-Object System.Drawing.Drawing2D.LinearGradientBrush $r0, $ciano, $azul, 135.0 }
    $g.FillPath($br, $forma)
    $br.Dispose()

    $g.SetClip($forma)
    $alt = $d * $zoomCabeca
    $esc = $alt / $cabeca.Height
    $lw  = $cabeca.Width * $esc
    $lh  = $cabeca.Height * $esc
    $cx  = $S / 2 - $lw / 2
    $cy  = $m + $d * (1.0 - $zoomCabeca) * 0.35
    $g.DrawImage($imgFoto, (New-Object System.Drawing.RectangleF $cx, $cy, $lw, $lh),
                 $cabeca, [System.Drawing.GraphicsUnit]::Pixel)
    $g.ResetClip()

    if ($anelW -gt 0) {
        $cor = if ($Estilo -eq 'escuro') { $cianoClaro } else { [System.Drawing.Color]::FromArgb(235, 255, 255, 255) }
        $pen = New-Object System.Drawing.Pen $cor, $anelW
        $pen.Alignment = [System.Drawing.Drawing2D.PenAlignment]::Inset
        $g.DrawPath($pen, $forma)
        $pen.Dispose()
    }
    $g.Dispose(); $forma.Dispose()
    return $bmp
}

# Os nove tamanhos que o Windows realmente pede: 16 (barra de titulo, bandeja), 20/24
# (bandeja em DPI alto), 32 (Alt+Tab, atalho), 40/48 (icones medios), 64, 128 e 256
# (icones grandes e a tela do instalador).
$tamanhos = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$lista = New-Object 'System.Collections.Generic.List[System.Drawing.Bitmap]'
$pngDir = Join-Path $Raiz 'icone-png'
New-Item -ItemType Directory -Force -Path $pngDir | Out-Null
foreach ($t in $tamanhos) {
    $b = Desenha-Icone $t
    $lista.Add($b)
    $b.Save((Join-Path $pngDir "dlss5forall-$t.png"), [System.Drawing.Imaging.ImageFormat]::Png)
}

[IconeUtil]::Escreve($Saida, $lista)
foreach ($b in $lista) { $b.Dispose() }
$imgFoto.Dispose()

$kb = [math]::Round((Get-Item $Saida).Length / 1KB, 1)
Write-Host "ok -> $Saida ($kb KB, $($tamanhos.Count) tamanhos: $($tamanhos -join ', '))"
Write-Host "     PNGs em $pngDir (para o site e o instalador)"
