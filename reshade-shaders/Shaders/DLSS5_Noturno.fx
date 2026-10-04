/*
    DLSS5_Noturno.fx -- "modo noturno" para a cadeia DLSS5 For All.

    Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).
    Codigo original: nao contem nem deriva de shader de terceiro. A unica
    dependencia e o ReShade.fxh do proprio ReShade (CC0).

    ------------------------------------------------------------------------
    O QUE FAZ
    ------------------------------------------------------------------------
    E o filtro "Modo noturno" do card Filtros: tira azul (a luz que mais cansa
    a vista a noite) e baixa um pouco o brilho, num passe so, em luz LINEAR
    para a mistura de cor sair certa. Com Strength = 1 o branco vira algo
    proximo de uma lampada quente (~3000 K) a 65% do brilho; com Strength = 0
    a imagem passa intacta.

    ------------------------------------------------------------------------
    ONDE FICA NA LISTA
    ------------------------------------------------------------------------
    Abaixo do "DLSS 5 Feed", junto dos outros filtros de cor.
*/

#include "ReShade.fxh"

uniform float Strength <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_label = "Strength";
    ui_tooltip = "Quanto azul sai e quanto o brilho baixa. 0 = passa intacto.";
> = 0.5;

// por componente, como no DLSS5_Grade.fx (o ?: vetorial nao e garantido no compilador do ReShade)
float DLSS5_NotSrgbToLinear1(float c) { return (c <= 0.04045) ? (c / 12.92) : pow(abs(c + 0.055) / 1.055, 2.4); }
float DLSS5_NotLinearToSrgb1(float c) { return (c <= 0.0031308) ? (c * 12.92) : (1.055 * pow(abs(c), 1.0 / 2.4) - 0.055); }
float3 DLSS5_NotSrgbToLinear(float3 c) { return float3(DLSS5_NotSrgbToLinear1(c.r), DLSS5_NotSrgbToLinear1(c.g), DLSS5_NotSrgbToLinear1(c.b)); }
float3 DLSS5_NotLinearToSrgb(float3 c) { return float3(DLSS5_NotLinearToSrgb1(c.r), DLSS5_NotLinearToSrgb1(c.g), DLSS5_NotLinearToSrgb1(c.b)); }

float4 PS_DLSS5_Noturno(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 c = saturate(tex2D(ReShade::BackBuffer, uv).rgb);
    if (Strength <= 0.0)
        return float4(c, 1.0);

    float3 lin = DLSS5_NotSrgbToLinear(c);

    // o branco a 100%: lampada quente (mais vermelho, menos verde, bem menos azul)
    const float3 quente = float3(1.00, 0.78, 0.50);
    const float3 m = lerp(float3(1.0, 1.0, 1.0), quente, Strength);
    lin *= m;

    // e mais escuro: ate 35% a menos de luz
    lin *= 1.0 - 0.35 * Strength;

    return float4(saturate(DLSS5_NotLinearToSrgb(max(lin, 0.0))), 1.0);
}

technique DLSS5_Noturno
<
    ui_label   = "DLSS 5 Modo noturno (menos azul -- ponha ABAIXO do DLSS 5 Feed)";
    ui_tooltip = "Tira azul e baixa o brilho para descansar a vista a noite.\n\n"
                 "ORDEM: abaixo de 'DLSS 5 Feed'.";
>
{
    pass Noturno { VertexShader = PostProcessVS; PixelShader = PS_DLSS5_Noturno; }
}
