# =====================================================================================
#  gerar-icone-capivara.ps1  -  monta o DLSS5ForAll.ico a partir da arte da capivara
#
#  Roda com:  powershell -ExecutionPolicy Bypass -File .\marca\gerar-icone-capivara.ps1
#  Sai em:    marca\DLSS5ForAll.ico
#             marca\capivara-512.png / capivara-256.png   (launcher e site)
#             marca\icone-png\dlss5forall-<tamanho>.png    (conferencia e instalador)
#
#  POR QUE UM SCRIPT E NAO UM .ICO SOLTO: o icone e DERIVADO da arte. Trocar o
#  enquadramento, a margem ou o raio da mascara e mexer numa linha aqui e rodar de novo,
#  em vez de refazer nove tamanhos na mao num editor de imagem.
#
#  A ARTE JA VEM COM MOLDURA. Diferente da foto antiga (gerar-icone.ps1, que recortava o
#  fundo e desenhava um anel ciano), aqui a moldura azul neon ja esta na imagem. O trabalho
#  do script e outro: so tirar os cantos escuros que sobram FORA da moldura arredondada,
#  com um recorte alfa antisserrilhado, e reduzir com qualidade.
# =====================================================================================
param(
    # Arte de origem (1254x1254, moldura azul neon arredondada).
    [string]$Arte = (Join-Path (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)) 'assets\icone\capivara-original.png'),

    # 'inteira'  = a arte toda, so com os cantos recortados (padrao para os tamanhos grandes)
    # 'moldura'  = corta o halo escuro externo, a moldura fica justa na borda do icone
    # 'rosto'    = fecha no rosto da capivara (metade pixel-art + metade realista)
    [string]$Pequenos = 'moldura',   # enquadramento usado em 16/20/24/32 px

    [string]$Saida = $null,
    [switch]$SoPngs                  # gera os PNGs de conferencia e NAO escreve o .ico
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$Raiz = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Saida) { $Saida = Join-Path $Raiz 'DLSS5ForAll.ico' }
if (-not (Test-Path $Arte)) { throw "arte nao encontrada: $Arte" }

# ------------------------------------------------------------------ escrita do .ico
# Identica em espirito a do gerar-icone.ps1: ate 64px o bloco vai em BMP (que TODO shell do
# Windows le) e de 128 para cima em PNG (esperado desde o Vista; evita um .ico de megabytes).
Add-Type -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.IO;

public class IconeCapivara {
    // Multiplica o alfa da imagem pelo alfa da mascara. A mascara chega ja reduzida de um
    // desenho 4x maior, entao a borda arredondada sai suave em vez de escadinha - SetClip
    // do GDI+ recorta com borda dura mesmo em modo AntiAlias, e a 16px isso aparece.
    public static void AplicaMascara(Bitmap img, Bitmap mask) {
        int W = img.Width, H = img.Height;
        Rectangle r = new Rectangle(0, 0, W, H);
        BitmapData di = img.LockBits(r, ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
        BitmapData dm = mask.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        int si = di.Stride, sm = dm.Stride;
        byte[] bi = new byte[si * H];
        byte[] bm = new byte[sm * H];
        Marshal.Copy(di.Scan0, bi, 0, bi.Length);
        Marshal.Copy(dm.Scan0, bm, 0, bm.Length);
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int i = y * si + x * 4, j = y * sm + x * 4;
                int a = bi[i + 3] * bm[j + 3] / 255;
                bi[i + 3] = (byte)a;
                // Pre-multiplicar NAO: o .ico de 32 bits guarda BGRA direto. Mas onde o alfa
                // zerou vale zerar a cor tambem, senao alguns leitores antigos mostram a
                // franja escura do canto ao ignorar o canal alfa.
                if (a == 0) { bi[i] = 0; bi[i + 1] = 0; bi[i + 2] = 0; }
            }
        }
        Marshal.Copy(bi, 0, di.Scan0, bi.Length);
        img.UnlockBits(di);
        mask.UnlockBits(dm);
    }

    public static void Escreve(string caminho, List<Bitmap> imagens) {
        List<byte[]> blocos = new List<byte[]>();
        foreach (Bitmap b in imagens) blocos.Add(b.Width >= 128 ? ComoPng(b) : ComoBmp(b));
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
            w.Write(new byte[tamMascara]);         // mascara AND zerada: o alfa de 32 bits resolve
            return ms.ToArray();
        }
    }
}
'@ -ReferencedAssemblies System.Drawing

# ------------------------------------------------------------------ geometria medida na arte
# A moldura neon foi medida pixel a pixel na arte de 1254x1254: o centro do traco fica em
# x=38 e x=1212, y=37 e y=1202, e o canto arredondado tem raio de ~176 px. Fora dela sobra
# so o halo azul e o fundo quase preto (#040d18).
#
#   'inteira' - a arte toda; a mascara passa POR FORA da moldura e engole so os cantos.
#   'moldura' - corta o halo, a moldura encosta na borda do icone. Nos tamanhos pequenos
#               isso vale ~8% de area util, que a 16px e a diferenca entre ver o bicho e
#               ver um borrao azul com um ponto claro no meio.
#   'rosto'   - so a cara (o olho pixel-art da esquerda e o focinho realista da direita).
# NOME LONGO DE PROPOSITO: o PowerShell nao diferencia maiusculas em nomes de variavel, e
# um `$ENQ` aqui seria a MESMA variavel que o parametro `$Enq` da funcao logo abaixo - o
# indexador devolveria $null e o script morreria dizendo "enquadramento desconhecido".
$Enquadramentos = @{
    'inteira' = @{ x =   0.0; y =   0.0; w = 1254.0; h = 1254.0; margem = 0.010; raio = 0.170 }
    'moldura' = @{ x =  26.0; y =  25.0; w = 1200.0; h = 1200.0; margem = 0.006; raio = 0.155 }
    'rosto'   = @{ x = 430.0; y =  95.0; w =  790.0; h =  790.0; margem = 0.006; raio = 0.200 }
}

$arteBmp = New-Object System.Drawing.Bitmap $Arte

function Caminho-Arredondado {
    param([single]$m, [single]$d, [single]$r)
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $dd = 2 * $r
    $p.AddArc($m, $m, $dd, $dd, 180, 90)
    $p.AddArc($m + $d - $dd, $m, $dd, $dd, 270, 90)
    $p.AddArc($m + $d - $dd, $m + $d - $dd, $dd, $dd, 0, 90)
    $p.AddArc($m, $m + $d - $dd, $dd, $dd, 90, 90)
    $p.CloseFigure()
    return $p
}

function Desenha-Icone {
    param([int]$S, [string]$Enq)
    $e = $Enquadramentos[$Enq]
    if (-not $e) { throw "enquadramento desconhecido: $Enq" }

    # ---- 1) a arte reduzida, em alta qualidade
    $bmp = New-Object System.Drawing.Bitmap $S, $S, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.SmoothingMode      = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    # WrapMode Clamp: sem isto o bicubico busca pixels fora da borda do recorte e devolve
    # uma linha fantasma clara em volta do icone - visivel justamente nos tamanhos grandes.
    $attr = New-Object System.Drawing.Imaging.ImageAttributes
    $attr.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
    $m = [single]($S * $e.margem)
    $dst = New-Object System.Drawing.Rectangle ([int][math]::Floor($m)), ([int][math]::Floor($m)), ([int][math]::Ceiling($S - 2*$m)), ([int][math]::Ceiling($S - 2*$m))
    $g.DrawImage($arteBmp, $dst, $e.x, $e.y, $e.w, $e.h, [System.Drawing.GraphicsUnit]::Pixel, $attr)
    $g.Dispose(); $attr.Dispose()

    # ---- 2) a mascara, desenhada 4x maior e reduzida (borda suave de verdade)
    $F = 4
    $big = New-Object System.Drawing.Bitmap ($S*$F), ($S*$F), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $gb = [System.Drawing.Graphics]::FromImage($big)
    $gb.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $forma = Caminho-Arredondado ([single]($m*$F)) ([single](($S - 2*$m)*$F)) ([single]($S*$e.raio*$F))
    $br = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    $gb.FillPath($br, $forma)
    $br.Dispose(); $forma.Dispose(); $gb.Dispose()

    $mask = New-Object System.Drawing.Bitmap $S, $S, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $gm = [System.Drawing.Graphics]::FromImage($mask)
    $gm.InterpolationMode  = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $gm.PixelOffsetMode    = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $gm.CompositingMode    = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $gm.DrawImage($big, (New-Object System.Drawing.Rectangle 0, 0, $S, $S))
    $gm.Dispose(); $big.Dispose()

    [IconeCapivara]::AplicaMascara($bmp, $mask)
    $mask.Dispose()
    return $bmp
}

# Os nove tamanhos que o Windows realmente pede: 16 (barra de titulo, bandeja), 20/24
# (bandeja em DPI alto), 32 (Alt+Tab, atalho), 40/48 (icones medios), 64, 128 e 256
# (icones grandes e a tela do instalador). O main.cpp escolhe a IMAGEM pelo tamanho
# (LoadImageW com largura e altura explicitas), entao cada um destes e usado de verdade.
$tamanhos = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$lista = New-Object 'System.Collections.Generic.List[System.Drawing.Bitmap]'
$pngDir = Join-Path $Raiz 'icone-png'
New-Item -ItemType Directory -Force -Path $pngDir | Out-Null

foreach ($t in $tamanhos) {
    $enq = if ($t -le 32) { $Pequenos } else { 'inteira' }
    $b = Desenha-Icone $t $enq
    $lista.Add($b)
    $b.Save((Join-Path $pngDir "dlss5forall-$t.png"), [System.Drawing.Imaging.ImageFormat]::Png)
}

# PNGs grandes para o launcher e o site (mesma mascara, enquadramento inteiro).
foreach ($t in @(512, 256)) {
    $b = Desenha-Icone $t 'inteira'
    $b.Save((Join-Path $Raiz "capivara-$t.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    $b.Dispose()
}

if (-not $SoPngs) {
    [IconeCapivara]::Escreve($Saida, $lista)
    $kb = [math]::Round((Get-Item $Saida).Length / 1KB, 1)
    Write-Host "ok -> $Saida ($kb KB, $($tamanhos.Count) tamanhos: $($tamanhos -join ', '))"
    Write-Host "     pequenos (<=32px) com enquadramento '$Pequenos'"
}
foreach ($b in $lista) { $b.Dispose() }
$arteBmp.Dispose()
Write-Host "     PNGs em $pngDir  +  capivara-512.png / capivara-256.png em $Raiz"
