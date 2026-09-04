# gerar-luts.ps1 -- gera as LUTs do DLSS5 For All por formula, do zero.
#
# Nenhuma LUT de terceiro entra aqui: tudo sai das curvas definidas em $LUTS abaixo,
# e as formulas em C# mais adiante sao as MESMAS que DLSS5_Grade.fx aplica ao vivo.
#
# Saida em reshade-shaders\Textures\ :
#
#   dlss5_grade_luts.png     atlas 4096 x 320 (5 LUTs 64^3 empilhadas)
#                            -> e o que DLSS5_Grade.fx le no estagio de LUT
#   dlss5_lut_<nome>.png     strips 4096 x 64 avulsos, so com -Strips
#
# Layout (definido por nos, e o que DLSS5_Grade.fx espera):
#   largura = N*N, altura = N * QUANTAS.
#   O pixel (t*N + x, k*N + y) guarda a cor graduada da entrada
#   RGB = (x/(N-1), y/(N-1), t/(N-1)) pela LUT numero k.
#   O azul indexa a fatia; o shader interpola linearmente entre fatias vizinhas
#   e o filtro bilinear da textura cobre vermelho e verde.
#
# Uso:  powershell -ExecutionPolicy Bypass -File .\gerar-luts.ps1
#       powershell -ExecutionPolicy Bypass -File .\gerar-luts.ps1 -Strips

[CmdletBinding()]
param(
    [int]    $Size = 64,
    [switch] $Strips,
    [string] $OutDir = (Join-Path $PSScriptRoot 'reshade-shaders\Textures')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# ---------------------------------------------------------------- definicoes
# Ordem desta lista == ordem das linhas no atlas == ordem do combo LUT_SELECT em
# DLSS5_Grade.fx (deslocada de 1, porque la o item 0 e "Desligada").
# NAO reordene sem mexer no ui_items do shader.
#
#   Temp    ganho RGB (normalizado pela luma) -- temperatura de cor, em luz linear
#   Lift    novo nivel de preto por canal      (0 = neutro)
#   Gain    novo nivel de branco por canal     (1 = neutro)
#   Gamma   por canal                          (1 = neutro)
#   SCurve  0..1, mistura com smoothstep       (0 = neutro)
#   Contr   ganho de contraste em torno de Pivot (1 = neutro)
#   Vib     vibrance: satura mais o que esta pouco saturado (0 = neutro)
#   Sat     saturacao global                   (0 = neutro)

$LUTS = @(
    @{ Nome='neutra';           Temp=@(1.000,1.000,1.000); Lift=@(0.000,0.000,0.000); Gain=@(1.000,1.000,1.000); Gamma=@(1.00,1.00,1.00); SCurve=0.00; Contr=1.00; Pivot=0.50; Vib=0.00; Sat= 0.00 }
    @{ Nome='quente-cine';      Temp=@(1.045,1.000,0.945); Lift=@(0.000,0.008,0.016); Gain=@(1.000,0.995,0.975); Gamma=@(1.00,1.00,1.00); SCurve=0.18; Contr=1.00; Pivot=0.50; Vib=0.10; Sat= 0.03 }
    @{ Nome='fria-noturna';     Temp=@(0.945,0.990,1.070); Lift=@(0.012,0.014,0.020); Gain=@(0.985,0.995,1.010); Gamma=@(1.03,1.03,1.03); SCurve=0.10; Contr=1.00; Pivot=0.50; Vib=0.00; Sat=-0.06 }
    @{ Nome='contraste-alto';   Temp=@(1.000,1.000,1.000); Lift=@(0.000,0.000,0.000); Gain=@(1.000,1.000,1.000); Gamma=@(1.00,1.00,1.00); SCurve=0.40; Contr=1.06; Pivot=0.45; Vib=0.00; Sat=-0.03 }
    @{ Nome='saturacao-suave';  Temp=@(1.000,1.000,1.000); Lift=@(0.000,0.000,0.000); Gain=@(1.000,1.000,1.000); Gamma=@(1.00,1.00,1.00); SCurve=0.00; Contr=1.00; Pivot=0.50; Vib=0.22; Sat= 0.02 }
)

# ---------------------------------------------------------------- gerador
# O laco fica em C# porque sao ~1,3 milhao de pixels; em PowerShell puro levaria minutos.
$cs = @'
using System;

public struct GradeParams {
    public double Tr, Tg, Tb;
    public double Lr, Lg, Lb;
    public double Gr, Gg, Gb;
    public double Yr, Yg, Yb;      // gamma
    public double SCurve, Contr, Pivot, Vib, Sat;
}

public static class LutGen {
    const double LW_R = 0.2126, LW_G = 0.7152, LW_B = 0.0722;

    static double SrgbToLinear(double c) {
        return c <= 0.04045 ? c / 12.92 : Math.Pow((c + 0.055) / 1.055, 2.4);
    }
    static double LinearToSrgb(double c) {
        if (c <= 0.0) return 0.0;
        return c <= 0.0031308 ? c * 12.92 : 1.055 * Math.Pow(c, 1.0 / 2.4) - 0.055;
    }
    static double Sat01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

    // Aplica a grade a uma cor sRGB display-referred (0..1), no mesmo espaco em
    // que o shader entrega o backbuffer. Devolve sRGB 0..1.
    public static void Grade(double r, double g, double b, ref GradeParams p,
                             out double or_, out double og_, out double ob_) {

        // 1) temperatura de cor -- em luz linear, com a luma preservada
        if (p.Tr != 1.0 || p.Tg != 1.0 || p.Tb != 1.0) {
            double lr = SrgbToLinear(r), lg = SrgbToLinear(g), lb = SrgbToLinear(b);
            double y0 = LW_R * lr + LW_G * lg + LW_B * lb;
            lr *= p.Tr; lg *= p.Tg; lb *= p.Tb;
            double y1 = LW_R * lr + LW_G * lg + LW_B * lb;
            if (y1 > 1e-9) { double k = y0 / y1; lr *= k; lg *= k; lb *= k; }
            r = LinearToSrgb(lr); g = LinearToSrgb(lg); b = LinearToSrgb(lb);
        }

        // 2) lift / gain / gamma, por canal, no espaco de exibicao
        r = Sat01(p.Lr + r * (p.Gr - p.Lr));
        g = Sat01(p.Lg + g * (p.Gg - p.Lg));
        b = Sat01(p.Lb + b * (p.Gb - p.Lb));
        if (p.Yr != 1.0) r = Math.Pow(r, 1.0 / p.Yr);
        if (p.Yg != 1.0) g = Math.Pow(g, 1.0 / p.Yg);
        if (p.Yb != 1.0) b = Math.Pow(b, 1.0 / p.Yb);

        // 3) contraste: S-curve + ganho em torno do pivo, aplicados so na luma
        //    (a razao volta para o RGB, entao o matiz nao se mexe)
        if (p.SCurve != 0.0 || p.Contr != 1.0) {
            double y = LW_R * r + LW_G * g + LW_B * b;
            if (y > 1e-6) {
                double t = Sat01(y);
                if (p.SCurve != 0.0) {
                    double s = t * t * (3.0 - 2.0 * t);          // smoothstep = S fixa no 0.5
                    t = t + (s - t) * p.SCurve;
                }
                if (p.Contr != 1.0) {
                    t = Sat01(p.Pivot + (t - p.Pivot) * p.Contr);
                }
                double k = t / y;
                r *= k; g *= k; b *= k;
            }
        }

        // 4) vibrance + saturacao
        if (p.Vib != 0.0 || p.Sat != 0.0) {
            double y = LW_R * r + LW_G * g + LW_B * b;
            double mx = Math.Max(r, Math.Max(g, b));
            double mn = Math.Min(r, Math.Min(g, b));
            double sat = mx > 1e-6 ? (mx - mn) / mx : 0.0;       // saturacao HSV
            double amt = p.Sat + p.Vib * (1.0 - sat);
            r = y + (r - y) * (1.0 + amt);
            g = y + (g - y) * (1.0 + amt);
            b = y + (b - y) * (1.0 + amt);
        }

        or_ = Sat01(r); og_ = Sat01(g); ob_ = Sat01(b);
    }

    // Preenche uma faixa BGRA de N*N x N pixels (stride em bytes) com a LUT.
    public static void Fill(byte[] buf, int stride, int rowOffsetPx, int n, ref GradeParams p) {
        double inv = 1.0 / (n - 1);
        for (int ty = 0; ty < n; ty++) {
            int line = (rowOffsetPx + ty) * stride;
            double gIn = ty * inv;
            for (int tile = 0; tile < n; tile++) {
                double bIn = tile * inv;
                for (int tx = 0; tx < n; tx++) {
                    double rIn = tx * inv;
                    double ro, go, bo;
                    Grade(rIn, gIn, bIn, ref p, out ro, out go, out bo);
                    int o = line + (tile * n + tx) * 4;
                    buf[o + 0] = (byte)Math.Round(bo * 255.0);   // B
                    buf[o + 1] = (byte)Math.Round(go * 255.0);   // G
                    buf[o + 2] = (byte)Math.Round(ro * 255.0);   // R
                    buf[o + 3] = 255;
                }
            }
        }
    }
}
'@

Add-Type -TypeDefinition $cs -ReferencedAssemblies System.Drawing | Out-Null

function New-GradeParams($d) {
    $p = New-Object GradeParams
    $p.Tr = $d.Temp[0];  $p.Tg = $d.Temp[1];  $p.Tb = $d.Temp[2]
    $p.Lr = $d.Lift[0];  $p.Lg = $d.Lift[1];  $p.Lb = $d.Lift[2]
    $p.Gr = $d.Gain[0];  $p.Gg = $d.Gain[1];  $p.Gb = $d.Gain[2]
    $p.Yr = $d.Gamma[0]; $p.Yg = $d.Gamma[1]; $p.Yb = $d.Gamma[2]
    $p.SCurve = $d.SCurve; $p.Contr = $d.Contr; $p.Pivot = $d.Pivot
    $p.Vib = $d.Vib;       $p.Sat = $d.Sat
    return $p
}

function Save-Bgra([byte[]]$buf, [int]$w, [int]$h, [int]$stride, [string]$path) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
    try {
        if ($data.Stride -eq $stride) {
            [System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $data.Scan0, $buf.Length)
        } else {
            for ($y = 0; $y -lt $h; $y++) {
                $dst = [IntPtr]::Add($data.Scan0, $y * $data.Stride)
                [System.Runtime.InteropServices.Marshal]::Copy($buf, $y * $stride, $dst, $w * 4)
            }
        }
    } finally { $bmp.UnlockBits($data) }
    if (Test-Path $path) { Remove-Item $path -Force }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# ---------------------------------------------------------------- execucao
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

$N       = $Size
$width   = $N * $N
$stride  = $width * 4
$count   = $LUTS.Count

Write-Host "gerando $count LUT(s) $($N)x$($N)x$($N) -> strip ${width}x${N}" -ForegroundColor Cyan

# atlas: uma LUT por faixa de N linhas
$atlas = New-Object byte[] ($stride * $N * $count)

for ($i = 0; $i -lt $count; $i++) {
    $d = $LUTS[$i]
    $p = New-GradeParams $d

    $one = New-Object byte[] ($stride * $N)
    [LutGen]::Fill($one, $stride, 0, $N, [ref]$p)

    if ($Strips) {
        $strip = Join-Path $OutDir ("dlss5_lut_{0}.png" -f $d.Nome)
        Save-Bgra $one $width $N $stride $strip
        Write-Host ("  [{0}] {1,-18} -> {2}" -f $i, $d.Nome, (Split-Path $strip -Leaf))
    } else {
        Write-Host ("  [{0}] {1,-18} -> linha {0} do atlas" -f $i, $d.Nome)
    }

    [Array]::Copy($one, 0, $atlas, $i * $stride * $N, $one.Length)
}

$atlasPath = Join-Path $OutDir 'dlss5_grade_luts.png'
Save-Bgra $atlas $width ($N * $count) $stride $atlasPath
Write-Host ("  atlas             -> {0} ({1}x{2})" -f (Split-Path $atlasPath -Leaf), $width, ($N * $count))

# ---------------------------------------------------------------- verificacao
# A LUT 'neutra' tem de ser identidade byte a byte: e o controle que prova que a
# cadeia (shader + interpolacao + quantizacao) nao esta mexendo em cor sozinha.
$pNeutra = New-GradeParams $LUTS[0]
$chk = New-Object byte[] ($stride * $N)
[LutGen]::Fill($chk, $stride, 0, $N, [ref]$pNeutra)
$maxErr = 0
for ($ty = 0; $ty -lt $N; $ty++) {
    for ($tile = 0; $tile -lt $N; $tile++) {
        for ($tx = 0; $tx -lt $N; $tx++) {
            $o = $ty * $stride + ($tile * $N + $tx) * 4
            $expB = [Math]::Round($tile / ($N - 1) * 255.0)
            $expG = [Math]::Round($ty   / ($N - 1) * 255.0)
            $expR = [Math]::Round($tx   / ($N - 1) * 255.0)
            $e = [Math]::Max([Math]::Abs($chk[$o] - $expB),
                 [Math]::Max([Math]::Abs($chk[$o+1] - $expG), [Math]::Abs($chk[$o+2] - $expR)))
            if ($e -gt $maxErr) { $maxErr = $e }
        }
    }
}
if ($maxErr -eq 0) { Write-Host "verificacao: LUT neutra e identidade exata (erro 0/255)" -ForegroundColor Green }
else               { Write-Warning "LUT neutra desviou ate $maxErr/255 -- confira as formulas" }

Write-Host "pronto." -ForegroundColor Cyan
