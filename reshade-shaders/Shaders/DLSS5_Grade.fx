/*
    DLSS5_Grade.fx -- color grading para a cadeia DLSS5 For All.

    Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).
    Codigo original: nao contem nem deriva de shader de terceiro. A unica
    dependencia e o ReShade.fxh do proprio ReShade (CC0).

    ------------------------------------------------------------------------
    ONDE ESTE EFEITO TEM DE FICAR NA LISTA -- LEIA ANTES DE ARRASTAR
    ------------------------------------------------------------------------
    Ordem correta na aba Home do ReShade:

        LUMENITE: Kernel 2.0        <- provedor de motion vectors
        DLSS 5 Feed                 <- o add-on dispara DLSS + Neural Rendering AQUI
        DLSS 5 Grade                <- ESTE ARQUIVO, sempre abaixo do Feed

    O add-on "DLSS 5 Feed" roda DLSS e o Neural Rendering logo depois que a
    tecnica DLSS5_Feed renderiza; tudo que estiver ABAIXO dela e aplicado em
    cima da saida neural. E esse o lugar deste efeito ("slot C").

    Se voce puser este efeito ACIMA do Kernel, o grade passa a ser a "cor
    original" que a rede neural recebe: uma S-curve forte esmaga preto e estoura
    branco ANTES do optical flow, e contraste e justamente o que o Kernel usa
    para casar pixels -- motion vector pior e mais fantasma. Nao faca isso.

    ------------------------------------------------------------------------
    ORDEM DOS ESTAGIOS (um passe so)
    ------------------------------------------------------------------------
      1. Exposicao + temperatura/tint .. em luz LINEAR (correcao de cena)
      2. Lift / Gamma / Gain .......... no espaco de exibicao (criativo)
      3. Contraste (S-curve + pivo) ... so na luma, matiz preservado
      4. Vibrance / saturacao
      5. LUT 3D opcional .............. o "look" por cima de tudo

    Cada estagio so e executado se estiver fora do neutro, entao com os valores
    padrao o efeito e identidade bit a bit e custa duas comparacoes.

    ------------------------------------------------------------------------
    LUT
    ------------------------------------------------------------------------
    A textura reshade-shaders\Textures\dlss5_grade_luts.png e gerada por
    gerar-luts.ps1 (na raiz do projeto) a partir das mesmas formulas que este
    shader aplica ao vivo. Layout: atlas de N fatias por LUT, LUTs empilhadas.

        largura = N*N, altura = N * DLSS5_GRADE_LUT_COUNT
        o pixel (t*N + x, k*N + y) guarda a cor graduada da entrada
        RGB = (x/(N-1), y/(N-1), t/(N-1)) pela LUT numero k

    O azul indexa a fatia e o shader interpola entre fatias vizinhas; o filtro
    bilinear cobre vermelho e verde. Como o mapeamento usa (c*(N-1)+0.5), as
    bordas caem no centro do texel e nao ha vazamento entre LUTs empilhadas.

    Se a textura nao existir, compile com -D DLSS5_GRADE_LUT=0 (ou acrescente
    DLSS5_GRADE_LUT=0 as PreprocessorDefinitions) para tirar o estagio de LUT.
*/

#include "ReShade.fxh"

#ifndef DLSS5_GRADE_LUT
    #define DLSS5_GRADE_LUT 1
#endif
#ifndef DLSS5_GRADE_LUT_SIZE
    #define DLSS5_GRADE_LUT_SIZE 64
#endif
#ifndef DLSS5_GRADE_LUT_COUNT
    #define DLSS5_GRADE_LUT_COUNT 5
#endif
#ifndef DLSS5_GRADE_LUT_TEXTURE
    #define DLSS5_GRADE_LUT_TEXTURE "dlss5_grade_luts.png"
#endif

// =============================================================== 1. exposicao

uniform float Exposure <
    ui_type = "slider";
    ui_min = -2.0; ui_max = 2.0; ui_step = 0.01;
    ui_category = "Exposicao e balanco de branco";
    ui_label = "Exposicao (stops)";
    ui_tooltip = "Multiplica a cena em luz linear por 2^valor.\n"
                 "+1.0 = um diafragma mais claro. 0 = neutro.";
> = 0.0;

uniform float Temperature <
    ui_type = "slider";
    ui_min = -1.0; ui_max = 1.0; ui_step = 0.01;
    ui_category = "Exposicao e balanco de branco";
    ui_label = "Temperatura (frio <-> quente)";
    ui_tooltip = "Negativo esfria (mais azul), positivo esquenta (mais laranja).\n"
                 "A escala inteira cobre mais ou menos 4500K a 9000K.\n"
                 "A luminancia e reposta depois, entao a imagem nao muda de brilho.";
> = 0.0;

uniform float Tint <
    ui_type = "slider";
    ui_min = -1.0; ui_max = 1.0; ui_step = 0.01;
    ui_category = "Exposicao e balanco de branco";
    ui_label = "Tint (verde <-> magenta)";
    ui_tooltip = "Eixo perpendicular ao da temperatura.\n"
                 "Negativo puxa para verde, positivo para magenta.";
> = 0.0;

// ====================================================== 2. lift / gamma / gain

uniform float3 Lift <
    ui_type = "slider";
    ui_min = -0.2; ui_max = 0.2; ui_step = 0.001;
    ui_category = "Lift / Gamma / Gain";
    ui_label = "Lift (nivel de preto RGB)";
    ui_tooltip = "Para onde o preto vai. 0 = neutro.\n"
                 "Positivo levanta o preto (look lavado, filme); negativo esmaga.\n"
                 "Desbalancear os canais tinge as sombras.";
> = float3(0.0, 0.0, 0.0);

uniform float3 Gamma <
    ui_type = "slider";
    ui_min = 0.5; ui_max = 2.0; ui_step = 0.01;
    ui_category = "Lift / Gamma / Gain";
    ui_label = "Gamma (meios-tons RGB)";
    ui_tooltip = "Acima de 1 clareia os meios-tons, abaixo escurece.\n"
                 "1 = neutro. Nao mexe em preto nem em branco.";
> = float3(1.0, 1.0, 1.0);

uniform float3 Gain <
    ui_type = "slider";
    ui_min = 0.5; ui_max = 1.5; ui_step = 0.001;
    ui_category = "Lift / Gamma / Gain";
    ui_label = "Gain (nivel de branco RGB)";
    ui_tooltip = "Para onde o branco vai. 1 = neutro.\n"
                 "Desbalancear os canais tinge as altas luzes.";
> = float3(1.0, 1.0, 1.0);

// ================================================================ 3. contraste

uniform float SCurve <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_category = "Contraste";
    ui_label = "S-curve";
    ui_tooltip = "Mistura a luma com uma curva em S (smoothstep).\n"
                 "Escurece as sombras e clareia as altas sem tocar no matiz.\n"
                 "0 = neutro. 0.15-0.25 costuma bastar.";
> = 0.0;

uniform float Contrast <
    ui_type = "slider";
    ui_min = 0.5; ui_max = 1.5; ui_step = 0.01;
    ui_category = "Contraste";
    ui_label = "Contraste linear";
    ui_tooltip = "Ganho da luma em torno do pivo. 1 = neutro.";
> = 1.0;

uniform float ContrastPivot <
    ui_type = "slider";
    ui_min = 0.1; ui_max = 0.9; ui_step = 0.01;
    ui_category = "Contraste";
    ui_label = "Pivo do contraste";
    ui_tooltip = "Nivel de luma que fica parado quando o contraste muda.\n"
                 "Abaixe para preservar as sombras, suba para preservar as altas.";
> = 0.5;

// ===================================================================== 4. cor

uniform float Vibrance <
    ui_type = "slider";
    ui_min = -0.5; ui_max = 1.0; ui_step = 0.01;
    ui_category = "Cor";
    ui_label = "Vibrance";
    ui_tooltip = "Satura mais o que esta pouco saturado e poupa o que ja esta.\n"
                 "E o jeito seguro de dar vida sem estourar pele e ceu. 0 = neutro.";
> = 0.0;

uniform float Saturation <
    ui_type = "slider";
    ui_min = -1.0; ui_max = 1.0; ui_step = 0.01;
    ui_category = "Cor";
    ui_label = "Saturacao global";
    ui_tooltip = "Empurra tudo por igual. -1 = preto e branco. 0 = neutro.";
> = 0.0;

// ===================================================================== 5. LUT

#if DLSS5_GRADE_LUT

uniform int LUT_SELECT <
    ui_type = "combo";
    ui_category = "LUT";
    ui_label = "LUT";
    ui_items = "Desligada\0"
               "Neutra (controle -- deve nao mudar nada)\0"
               "Quente cinematografica\0"
               "Fria noturna\0"
               "Contraste alto\0"
               "Saturacao suave\0";
    ui_tooltip = "A ordem tem de bater com a lista LUTS de gerar-luts.ps1,\n"
                 "deslocada de 1 porque o item 0 aqui e 'Desligada'.\n"
                 "'Neutra' e identidade byte a byte: use para conferir que o\n"
                 "estagio de LUT nao esta mexendo em cor sozinho.";
> = 0;

uniform float LUT_Strength <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_category = "LUT";
    ui_label = "Intensidade da LUT";
> = 1.0;

uniform float LUT_AmountLuma <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_category = "LUT";
    ui_label = "Peso da luma";
    ui_tooltip = "Quanto da mudanca de brilho da LUT entra.\n"
                 "Abaixe (0.6-0.8) para nao achatar o contraste que o\n"
                 "Neural Rendering criou.";
> = 1.0;

uniform float LUT_AmountChroma <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_category = "LUT";
    ui_label = "Peso da cor";
    ui_tooltip = "Quanto da mudanca de cor/matiz da LUT entra.";
> = 1.0;

texture tDLSS5GradeLUT < source = DLSS5_GRADE_LUT_TEXTURE; >
{
    Width  = DLSS5_GRADE_LUT_SIZE * DLSS5_GRADE_LUT_SIZE;
    Height = DLSS5_GRADE_LUT_SIZE * DLSS5_GRADE_LUT_COUNT;
    Format = RGBA8;
};

sampler sDLSS5GradeLUT
{
    Texture = tDLSS5GradeLUT;
    AddressU = CLAMP; AddressV = CLAMP; AddressW = CLAMP;
    MagFilter = LINEAR; MinFilter = LINEAR; MipFilter = LINEAR;
};

#endif // DLSS5_GRADE_LUT

// ================================================================== auxiliares

static const float3 DLSS5_LUMA_W = float3(0.2126, 0.7152, 0.0722);

float DLSS5_SrgbToLinear1(float c)
{
    return (c <= 0.04045) ? (c / 12.92) : pow(abs(c + 0.055) / 1.055, 2.4);
}

float DLSS5_LinearToSrgb1(float c)
{
    return (c <= 0.0031308) ? (c * 12.92) : (1.055 * pow(abs(c), 1.0 / 2.4) - 0.055);
}

float3 DLSS5_SrgbToLinear(float3 c)
{
    return float3(DLSS5_SrgbToLinear1(c.r), DLSS5_SrgbToLinear1(c.g), DLSS5_SrgbToLinear1(c.b));
}

float3 DLSS5_LinearToSrgb(float3 c)
{
    return float3(DLSS5_LinearToSrgb1(c.r), DLSS5_LinearToSrgb1(c.g), DLSS5_LinearToSrgb1(c.b));
}

// soma dos desvios absolutos em relacao a um neutro escalar -- serve de teste
// "esta fora do padrao?" sem depender de comparacao vetorial
float DLSS5_Deviation(float3 v, float neutral)
{
    return dot(abs(v - neutral), float3(1.0, 1.0, 1.0));
}

// reposiciona a luminancia de c para y_alvo, preservando a cromaticidade
float3 DLSS5_RetargetLuma(float3 c, float y_target)
{
    float y = dot(c, DLSS5_LUMA_W);
    float k = (y > 1e-6) ? (y_target / y) : 1.0;
    return c * k;
}

#if DLSS5_GRADE_LUT
float3 DLSS5_SampleLUT(float3 c, int row)
{
    float n  = float(DLSS5_GRADE_LUT_SIZE);
    float nl = float(DLSS5_GRADE_LUT_COUNT);

    // tamanho de um texel do atlas, em uv
    float2 texel = float2(1.0 / (n * n), 1.0 / (n * nl));
    // largura de uma fatia, em uv: n texels de uma linha de n*n
    float tile = 1.0 / n;

    float2 uv;
    uv.x = (c.r * (n - 1.0) + 0.5) * texel.x;
    uv.y = (c.g * (n - 1.0) + 0.5 + float(row) * n) * texel.y;

    float bz = c.b * (n - 1.0);
    float f  = frac(bz);
    uv.x += (bz - f) * tile;

    float3 lo = tex2Dlod(sDLSS5GradeLUT, float4(uv, 0.0, 0.0)).rgb;
    float3 hi = tex2Dlod(sDLSS5GradeLUT, float4(uv.x + tile, uv.y, 0.0, 0.0)).rgb;
    return lerp(lo, hi, f);
}
#endif

// ================================================================== pixel shader

float4 PS_DLSS5_Grade(float4 vpos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    float3 c = saturate(tex2D(ReShade::BackBuffer, texcoord).rgb);

    // ---- 1. exposicao + balanco de branco, em luz linear ----------------
    if (Exposure != 0.0 || Temperature != 0.0 || Tint != 0.0)
    {
        float3 lin = DLSS5_SrgbToLinear(c);
        float  y0  = dot(lin, DLSS5_LUMA_W);

        lin *= exp2(Exposure);

        if (Temperature != 0.0 || Tint != 0.0)
        {
            // quente = mais vermelho e menos azul; tint positivo = magenta
            float3 wb;
            wb.r = (1.0 + 0.20 * Temperature) * (1.0 + 0.05 * Tint);
            wb.g = (1.0                     ) * (1.0 - 0.15 * Tint);
            wb.b = (1.0 - 0.20 * Temperature) * (1.0 + 0.05 * Tint);
            lin *= max(wb, 0.0);
            // o balanco muda a cor, nao a exposicao: repoe a luminancia
            lin = DLSS5_RetargetLuma(lin, y0 * exp2(Exposure));
        }

        c = saturate(DLSS5_LinearToSrgb(max(lin, 0.0)));
    }

    // ---- 2. lift / gamma / gain, no espaco de exibicao -------------------
    if (DLSS5_Deviation(Lift, 0.0) > 0.0 || DLSS5_Deviation(Gain, 1.0) > 0.0)
    {
        c = saturate(Lift + c * (Gain - Lift));
    }
    if (DLSS5_Deviation(Gamma, 1.0) > 0.0)
    {
        c = pow(max(c, 0.0), float3(1.0, 1.0, 1.0) / max(Gamma, 0.001));
    }

    // ---- 3. contraste, so na luma (matiz preservado) --------------------
    if (SCurve != 0.0 || Contrast != 1.0)
    {
        float y = dot(c, DLSS5_LUMA_W);
        float t = saturate(y);
        float s = t * t * (3.0 - 2.0 * t);          // S fixa, pivo 0.5
        t = t + (s - t) * SCurve;
        t = saturate(ContrastPivot + (t - ContrastPivot) * Contrast);
        c = DLSS5_RetargetLuma(c, t);
    }

    // ---- 4. vibrance + saturacao ----------------------------------------
    if (Vibrance != 0.0 || Saturation != 0.0)
    {
        float y  = dot(c, DLSS5_LUMA_W);
        float mx = max(c.r, max(c.g, c.b));
        float mn = min(c.r, min(c.g, c.b));
        float sat = (mx > 1e-6) ? ((mx - mn) / mx) : 0.0;   // saturacao HSV
        float amt = Saturation + Vibrance * (1.0 - sat);
        c = y + (c - y) * (1.0 + amt);
    }

    c = saturate(c);

#if DLSS5_GRADE_LUT
    // ---- 5. LUT 3D -------------------------------------------------------
    if (LUT_SELECT > 0 && LUT_Strength > 0.0)
    {
        float3 lut = DLSS5_SampleLUT(c, LUT_SELECT - 1);

        // luma e croma separados, sem normalize(): pixel preto nao vira NaN
        float yc = dot(c,   DLSS5_LUMA_W);
        float yl = dot(lut, DLSS5_LUMA_W);
        float3 graded = lerp(yc, yl, LUT_AmountLuma)
                      + lerp(c - yc, lut - yl, LUT_AmountChroma);

        c = saturate(lerp(c, graded, LUT_Strength));
    }
#endif

    return float4(c, 1.0);
}

technique DLSS5_Grade
<
    ui_label   = "DLSS 5 Grade (cor -- ponha ABAIXO do DLSS 5 Feed)";
    ui_tooltip = "Exposicao, balanco de branco, lift/gamma/gain, contraste,\n"
                 "vibrance/saturacao e uma LUT 3D opcional, num passe so.\n\n"
                 "ORDEM: este efeito tem de ficar ABAIXO de 'DLSS 5 Feed'.\n"
                 "Acima do Kernel ele vira a cor de entrada da rede neural e\n"
                 "estraga o optical flow.\n\n"
                 "Com todos os valores no padrao a imagem passa intacta.";
>
{
    pass Grade
    {
        VertexShader = PostProcessVS;
        PixelShader  = PS_DLSS5_Grade;
    }
}
