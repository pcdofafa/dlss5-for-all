<#
  medir-nitidez.ps1 -- mede NITIDEZ em PNG/JPG, com numero, para o A/B nao depender
  de impressao. Feito para o A/B de docs\borrao-e-nitidez.md.

  MIT, como o resto do projeto.

  ---------------------------------------------------------------------------
  O QUE ELE MEDE

    LapVar   variancia do laplaciano 3x3 sobre a luma BT.709. E a metrica classica
             de foco/blur: um passa-alta ao quadrado. Cai quando a imagem borra.
             E a metrica principal -- olhe para ela primeiro.

    Tenen    Tenengrad: media de |gradiente Sobel|^2. Mede energia de BORDA. Sobe
             com nitidez real E com halo/realce exagerado, entao serve de contraprova:
             LapVar subindo COM Tenen subindo muito mais = ganho artificial (halo).

    RMS      contraste RMS da luma (desvio padrao). NAO e nitidez -- e a referencia
             que diz se as duas imagens tem o mesmo contraste global. Se o RMS mudou
             muito entre A e B, o LapVar nao e comparavel direto: um tone map que
             levanta sombras muda as duas coisas ao mesmo tempo.

    Clip     % de pixels em 0 ou 255 na luma. Se subiu, houve clipping -- e detalhe
             perdido que nenhuma metrica de nitidez enxerga.

  ---------------------------------------------------------------------------
  O QUE ELE **NAO** MEDE, e e importante saber

    - NAO mede arrasto/ghosting em movimento. Fantasma e um erro TEMPORAL: aparece
      entre frames, nao dentro de um. Uma imagem so nao o contem.
    - NAO compara imagens de CONTEUDO DIFERENTE. Duas fotos de lugares diferentes do
      jogo tem LapVar diferente por causa do conteudo, nao da configuracao. Comparar
      so faz sentido com a MESMA cena.

    Consequencia pratica, dita sem rodeio: este script resolve o A/B do Neural
    Rendering (o par F5 do add-on e do mesmo frame -- conteudo identico, ideal) e o
    do DLSS5_Sharpen com a camera parada. Ele NAO resolve o teste do reset_every,
    que so se manifesta em movimento e continua sendo julgamento visual.

  ---------------------------------------------------------------------------
  COMO USAR

    # todas as imagens da pasta atual
    .\medir-nitidez.ps1

    # uma pasta especifica
    .\medir-nitidez.ps1 -Caminho 'C:\Users\...\Desktop\dlss cam'

    # so um recorte (compara sempre a MESMA regiao; use quando a cena tem ceu liso,
    # que dilui a media). x,y,largura,altura em pixels.
    .\medir-nitidez.ps1 -Recorte 1200,600,800,400

    # recorte central automatico de 50% -- o atalho mais comum
    .\medir-nitidez.ps1 -Centro

    # parear automaticamente os screenshots [pre-NR] / [NR on] do F5 do add-on
    .\medir-nitidez.ps1 -Pares

    # rotular a rodada, para juntar varias medicoes num CSV
    .\medir-nitidez.ps1 -Centro -Rotulo 'NR=1111' -Csv .\nitidez.csv
#>

[CmdletBinding()]
param(
    [string]   $Caminho = '.',
    [int[]]    $Recorte,
    [switch]   $Centro,
    [switch]   $Pares,
    [string]   $Rotulo  = '',
    [string]   $Csv     = ''
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------- nucleo em C#
# 3440x1440 = 5 milhoes de pixels. Em PowerShell puro (GetPixel) isso levaria
# minutos; em C# com LockBits leva menos de um segundo por imagem.
$fonte = @'
using System;
using System.Drawing;
using System.Drawing.Imaging;

public class MedidorNitidez
{
    public double LapVar, Tenengrad, Rms, ClipPct, Media;
    public int Largura, Altura;

    public static MedidorNitidez Medir(string caminho, int rx, int ry, int rw, int rh)
    {
        using (Bitmap bmp = new Bitmap(caminho))
        {
            // recorte pedido, aparado ao que a imagem realmente tem
            if (rw <= 0 || rh <= 0) { rx = 0; ry = 0; rw = bmp.Width; rh = bmp.Height; }
            if (rx < 0) rx = 0;
            if (ry < 0) ry = 0;
            if (rx + rw > bmp.Width)  rw = bmp.Width  - rx;
            if (ry + rh > bmp.Height) rh = bmp.Height - ry;
            if (rw < 3 || rh < 3) throw new Exception("recorte menor que 3x3 px");

            Rectangle r = new Rectangle(rx, ry, rw, rh);
            BitmapData d = bmp.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);

            float[] luma = new float[rw * rh];
            int clip = 0;
            try
            {
                byte[] linha = new byte[rw * 4];
                for (int y = 0; y < rh; y++)
                {
                    IntPtr p = (IntPtr)((long)d.Scan0 + (long)y * d.Stride);
                    System.Runtime.InteropServices.Marshal.Copy(p, linha, 0, rw * 4);
                    for (int x = 0; x < rw; x++)
                    {
                        // BGRA
                        float b = linha[x * 4 + 0];
                        float g = linha[x * 4 + 1];
                        float rr = linha[x * 4 + 2];
                        float l = 0.2126f * rr + 0.7152f * g + 0.0722f * b;   // BT.709
                        luma[y * rw + x] = l;
                        if (l <= 0.5f || l >= 254.5f) clip++;
                    }
                }
            }
            finally { bmp.UnlockBits(d); }

            MedidorNitidez m = new MedidorNitidez();
            m.Largura = rw; m.Altura = rh;
            m.ClipPct = 100.0 * clip / (rw * (double)rh);

            // media e desvio padrao da luma (contraste RMS)
            double soma = 0.0;
            for (int i = 0; i < luma.Length; i++) soma += luma[i];
            double media = soma / luma.Length;
            double var = 0.0;
            for (int i = 0; i < luma.Length; i++) { double dv = luma[i] - media; var += dv * dv; }
            m.Media = media;
            m.Rms = Math.Sqrt(var / luma.Length);

            // laplaciano 3x3 (0 1 0 / 1 -4 1 / 0 1 0) e Sobel, na area interna
            double sL = 0.0, sL2 = 0.0, sT = 0.0;
            long n = 0;
            for (int y = 1; y < rh - 1; y++)
            {
                int o = y * rw;
                for (int x = 1; x < rw - 1; x++)
                {
                    int i = o + x;
                    float c  = luma[i];
                    float up = luma[i - rw], dn = luma[i + rw];
                    float le = luma[i - 1],  ri = luma[i + 1];

                    double lap = up + dn + le + ri - 4.0 * c;
                    sL += lap; sL2 += lap * lap;

                    float ul = luma[i - rw - 1], ur = luma[i - rw + 1];
                    float dl = luma[i + rw - 1], dr = luma[i + rw + 1];
                    double gx = (ur + 2.0 * ri + dr) - (ul + 2.0 * le + dl);
                    double gy = (dl + 2.0 * dn + dr) - (ul + 2.0 * up + ur);
                    sT += gx * gx + gy * gy;
                    n++;
                }
            }
            double mL = sL / n;
            m.LapVar    = (sL2 / n) - (mL * mL);
            m.Tenengrad = sT / n;
            return m;
        }
    }
}
'@

if (-not ([System.Management.Automation.PSTypeName]'MedidorNitidez').Type) {
    Add-Type -AssemblyName System.Drawing
    Add-Type -TypeDefinition $fonte -ReferencedAssemblies System.Drawing -ErrorAction Stop
}

# ---------------------------------------------------------------- coleta
if (Test-Path -LiteralPath $Caminho -PathType Container) {
    $arquivos = Get-ChildItem -LiteralPath $Caminho -File |
                Where-Object { $_.Extension -match '^\.(png|jpg|jpeg|bmp)$' } |
                Sort-Object Name
} else {
    $arquivos = Get-Item -LiteralPath $Caminho
}

if (-not $arquivos -or $arquivos.Count -eq 0) {
    Write-Host "Nenhuma imagem em '$Caminho'." -ForegroundColor Yellow
    Write-Host "Screenshots do ReShade caem na pasta do app (SavePath=.\ no ReShade.ini);"
    Write-Host "a tecla e PrintScreen (KeyScreenshot=44), e o F5 grava o par [pre-NR]/[NR on]."
    return
}

# ---------------------------------------------------------------- recorte
$rx = 0; $ry = 0; $rw = 0; $rh = 0
if ($Recorte) {
    if ($Recorte.Count -ne 4) { throw "-Recorte precisa de 4 numeros: x,y,largura,altura" }
    $rx = $Recorte[0]; $ry = $Recorte[1]; $rw = $Recorte[2]; $rh = $Recorte[3]
}

$resultados = @()
foreach ($f in $arquivos) {
    try {
        $ax = $rx; $ay = $ry; $aw = $rw; $ah = $rh
        if ($Centro -and -not $Recorte) {
            $img = [System.Drawing.Image]::FromFile($f.FullName)
            try {
                $aw = [int]($img.Width  / 2); $ah = [int]($img.Height / 2)
                $ax = [int](($img.Width - $aw) / 2); $ay = [int](($img.Height - $ah) / 2)
            } finally { $img.Dispose() }
        }
        $m = [MedidorNitidez]::Medir($f.FullName, $ax, $ay, $aw, $ah)
        $resultados += [pscustomobject]@{
            Arquivo = $f.Name
            Regiao  = "$($m.Largura)x$($m.Altura)"
            LapVar  = [math]::Round($m.LapVar, 2)
            Tenen   = [math]::Round($m.Tenengrad, 1)
            RMS     = [math]::Round($m.Rms, 2)
            Media   = [math]::Round($m.Media, 1)
            Clip    = [math]::Round($m.ClipPct, 2)
            Rotulo  = $Rotulo
        }
    } catch {
        Write-Host "  ! $($f.Name): $($_.Exception.Message)" -ForegroundColor Red
    }
}

if ($resultados.Count -eq 0) { return }

Write-Host ""
Write-Host "NITIDEZ  (LapVar = principal, maior = mais nitido)" -ForegroundColor Cyan
if ($Centro -and -not $Recorte) { Write-Host "regiao: recorte central de 50%" -ForegroundColor DarkGray }
elseif ($Recorte)               { Write-Host "regiao: recorte $rx,$ry $($rw)x$($rh)" -ForegroundColor DarkGray }
else                            { Write-Host "regiao: imagem inteira" -ForegroundColor DarkGray }
Write-Host ""
$resultados | Format-Table Arquivo, Regiao, LapVar, Tenen, RMS, Media, Clip -AutoSize

# ---------------------------------------------------------------- pares [pre-NR] / [NR on]
if ($Pares) {
    Write-Host "PARES DO F5  (o add-on grava [pre-NR] e [NR on] do MESMO frame)" -ForegroundColor Cyan
    Write-Host ""
    $achou = $false
    foreach ($pre in $resultados) {
        if ($pre.Arquivo -notmatch '\[pre-NR\]') { continue }
        $chave = $pre.Arquivo -replace '\[pre-NR\]', ''
        $pos = $resultados | Where-Object {
            $_.Arquivo -match '\[NR on\]' -and ($_.Arquivo -replace '\[NR on\]','') -eq $chave
        } | Select-Object -First 1
        if (-not $pos) { continue }
        $achou = $true

        $dLap = 0.0; if ($pre.LapVar -ne 0) { $dLap = 100.0 * ($pos.LapVar - $pre.LapVar) / $pre.LapVar }
        $dTen = 0.0; if ($pre.Tenen  -ne 0) { $dTen = 100.0 * ($pos.Tenen  - $pre.Tenen)  / $pre.Tenen  }
        $dRms = 0.0; if ($pre.RMS    -ne 0) { $dRms = 100.0 * ($pos.RMS    - $pre.RMS)    / $pre.RMS    }

        Write-Host ("  " + $chave.Trim())
        Write-Host ("    LapVar  {0,10:N2} -> {1,10:N2}   {2,+7:N1} %" -f $pre.LapVar, $pos.LapVar, $dLap)
        Write-Host ("    Tenen   {0,10:N1} -> {1,10:N1}   {2,+7:N1} %" -f $pre.Tenen,  $pos.Tenen,  $dTen)
        Write-Host ("    RMS     {0,10:N2} -> {1,10:N2}   {2,+7:N1} %" -f $pre.RMS,    $pos.RMS,    $dRms)

        Write-Host -NoNewline "    veredicto: "
        if ([math]::Abs($dRms) -gt 15.0) {
            Write-Host "contraste mudou $([math]::Round($dRms,1))% -- o NR mexeu no tom, LapVar nao e comparavel direto." -ForegroundColor Yellow
        } elseif ($dLap -lt -8.0) {
            Write-Host "o Neural Rendering ESTA AMACIANDO ($([math]::Round($dLap,1))%). Baixe os sliders NR (docs\borrao-e-nitidez.md secao 3)." -ForegroundColor Red
        } elseif ($dLap -gt 8.0 -and $dTen -gt (2.5 * $dLap)) {
            Write-Host "ganho com cara de halo (Tenen subiu $([math]::Round($dTen,1))% contra LapVar $([math]::Round($dLap,1))%). Olhe as bordas." -ForegroundColor Yellow
        } elseif ($dLap -gt 8.0) {
            Write-Host "o NR esta ajudando (+$([math]::Round($dLap,1))%)." -ForegroundColor Green
        } else {
            Write-Host "neutro (dentro de +-8%). O borrao vem de outro estagio -- veja secoes 1, 2 e 4." -ForegroundColor Gray
        }
        Write-Host ""
    }
    if (-not $achou) {
        Write-Host "  Nenhum par [pre-NR]/[NR on] encontrado." -ForegroundColor Yellow
        Write-Host "  Gere um: com o jogo capturado e o neural ligado (F6), aperte F5 durante o gameplay."
        Write-Host ""
    }
}

# ---------------------------------------------------------------- CSV
if ($Csv) {
    $existe = Test-Path -LiteralPath $Csv
    if ($existe) { $resultados | Export-Csv -LiteralPath $Csv -NoTypeInformation -Append -Encoding UTF8 }
    else         { $resultados | Export-Csv -LiteralPath $Csv -NoTypeInformation -Encoding UTF8 }
    Write-Host "gravado em $Csv (rotulo: '$Rotulo')" -ForegroundColor DarkGray
}

Write-Host "Comparar so vale entre imagens da MESMA cena. Ver o cabecalho deste script." -ForegroundColor DarkGray
