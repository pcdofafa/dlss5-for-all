/*
    DLSS5_Detalhes.fx -- contraste local ("clareza") para a cadeia DLSS5 For All.

    Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).
    Codigo original: nao contem nem deriva de shader de terceiro. A unica
    dependencia e o ReShade.fxh do proprio ReShade (CC0).

    ------------------------------------------------------------------------
    O QUE FAZ
    ------------------------------------------------------------------------
    E o filtro "Detalhes" do card Filtros: realca o contraste de MEDIA
    frequencia (relevo, textura, volume) sem tocar na alta frequencia, que e
    trabalho da nitidez, e sem mudar o brilho geral, que e trabalho do grade.
    Na pratica e uma mascara de desfoque (unsharp mask) com raio grande, so na
    luma, com a cromaticidade preservada.

    ------------------------------------------------------------------------
    COMO FUNCIONA (tres passes)
    ------------------------------------------------------------------------
      1. Luma       -> a luma BT.709 do backbuffer numa textura de 1/2 da tela.
      2. BlurH/V    -> gaussiano separavel de 9 amostras com passo largo. O passo
                       cresce com a altura da tela (BUFFER_HEIGHT / 1080) para o
                       raio VISUAL ser o mesmo em 1080p e em 4K.
      3. Detalhes   -> detalhe = luma - luma desfocada; a luma final e
                       luma + detalhe * ganho, presa a [0,1] com joelho suave nos
                       extremos para nao estourar branco nem afogar preto; a cor
                       e reescalada pela razao entre a luma nova e a antiga.

    Custo: dois passes em 1/4 dos pixels e um passe inteiro de 2 leituras.
    Com Strength = 0 a imagem passa intacta.

    ------------------------------------------------------------------------
    ONDE FICA NA LISTA
    ------------------------------------------------------------------------
    Abaixo do "DLSS 5 Feed" (trabalha sobre a saida da rede), depois dos
    filtros de cor e ANTES da nitidez: a nitidez realca o que este passe
    acabou de destacar, e nao o contrario.
*/

#include "ReShade.fxh"

uniform float Strength <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_label = "Strength";
    ui_tooltip = "Quanto contraste local entra. 0 = passa intacto.";
> = 0.5;

// ---------------------------------------------------------------- texturas

texture texDLSS5DetLuma < pooled = true; > { Width = BUFFER_WIDTH / 2; Height = BUFFER_HEIGHT / 2; Format = R8; };
texture texDLSS5DetBlurH < pooled = true; > { Width = BUFFER_WIDTH / 2; Height = BUFFER_HEIGHT / 2; Format = R8; };
texture texDLSS5DetBlur  < pooled = true; > { Width = BUFFER_WIDTH / 2; Height = BUFFER_HEIGHT / 2; Format = R8; };

sampler sDLSS5DetLuma  { Texture = texDLSS5DetLuma;  AddressU = CLAMP; AddressV = CLAMP; };
sampler sDLSS5DetBlurH { Texture = texDLSS5DetBlurH; AddressU = CLAMP; AddressV = CLAMP; };
sampler sDLSS5DetBlur  { Texture = texDLSS5DetBlur;  AddressU = CLAMP; AddressV = CLAMP; };

static const float3 DLSS5_DET_LUMA_W = float3(0.2126, 0.7152, 0.0722);

// gaussiano de 9 amostras (sigma ~ 2 passos)
static const float DLSS5_DET_W[5] = { 0.2270, 0.1945, 0.1216, 0.0540, 0.0162 };

// O passo em texels da textura de 1/2: ~3 texels em 1080p (= 6 px da tela), escala
// com a altura para o raio visual nao encolher em 4K.
float DLSS5_DetPasso() { return max(1.0, 3.0 * (BUFFER_HEIGHT / 1080.0)); }

// ---------------------------------------------------------------- passes

float PS_DLSS5_DetLuma(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return dot(saturate(tex2D(ReShade::BackBuffer, uv).rgb), DLSS5_DET_LUMA_W);
}

float PS_DLSS5_DetBlurH(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float2 px = float2(2.0 * BUFFER_RCP_WIDTH, 0.0) * DLSS5_DetPasso();
    float acc = tex2D(sDLSS5DetLuma, uv).r * DLSS5_DET_W[0];
    [unroll]
    for (int i = 1; i < 5; i++)
    {
        acc += tex2D(sDLSS5DetLuma, uv + px * i).r * DLSS5_DET_W[i];
        acc += tex2D(sDLSS5DetLuma, uv - px * i).r * DLSS5_DET_W[i];
    }
    return acc;
}

float PS_DLSS5_DetBlurV(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float2 px = float2(0.0, 2.0 * BUFFER_RCP_HEIGHT) * DLSS5_DetPasso();
    float acc = tex2D(sDLSS5DetBlurH, uv).r * DLSS5_DET_W[0];
    [unroll]
    for (int i = 1; i < 5; i++)
    {
        acc += tex2D(sDLSS5DetBlurH, uv + px * i).r * DLSS5_DET_W[i];
        acc += tex2D(sDLSS5DetBlurH, uv - px * i).r * DLSS5_DET_W[i];
    }
    return acc;
}

float4 PS_DLSS5_Detalhes(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 c = saturate(tex2D(ReShade::BackBuffer, uv).rgb);
    if (Strength <= 0.0)
        return float4(c, 1.0);

    const float y  = dot(c, DLSS5_DET_LUMA_W);
    const float yb = tex2D(sDLSS5DetBlur, uv).r;
    const float d  = y - yb;                       // a banda de media frequencia

    // ganho: ate 2x de contraste local no maximo do controle
    float yn = y + d * (Strength * 2.0);

    // joelho suave nos extremos: perto de 0 e de 1 o realce recua em vez de clipar
    const float room = min(y, 1.0 - y) * 2.0;      // 1 no meio-tom, 0 nos extremos
    yn = lerp(y, yn, saturate(room * 1.5));
    yn = saturate(yn);

    // reescala a cor pela luma, cromaticidade preservada; aditivo nos pretos
    const float eps = 1.0 / 255.0;
    float3 outc;
    if (y > eps) outc = c * (yn / y);
    else         outc = c + (yn - y);
    return float4(saturate(outc), 1.0);
}

technique DLSS5_Detalhes
<
    ui_label   = "DLSS 5 Detalhes (contraste local -- ponha ABAIXO do DLSS 5 Feed)";
    ui_tooltip = "Realca relevo e textura pelo contraste de media frequencia,\n"
                 "so na luma, sem mudar o brilho geral.\n\n"
                 "ORDEM: abaixo de 'DLSS 5 Feed', antes de 'DLSS 5 Sharpen'.";
>
{
    pass Luma  { VertexShader = PostProcessVS; PixelShader = PS_DLSS5_DetLuma;  RenderTarget = texDLSS5DetLuma;  }
    pass BlurH { VertexShader = PostProcessVS; PixelShader = PS_DLSS5_DetBlurH; RenderTarget = texDLSS5DetBlurH; }
    pass BlurV { VertexShader = PostProcessVS; PixelShader = PS_DLSS5_DetBlurV; RenderTarget = texDLSS5DetBlur;  }
    pass Detalhes { VertexShader = PostProcessVS; PixelShader = PS_DLSS5_Detalhes; }
}
