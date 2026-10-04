/*
    DLSS5_Sharpen.fx -- nitidez adaptativa ao contraste para a cadeia DLSS5 For All.

    Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).
    Codigo original: nao contem nem deriva de shader de terceiro. O algoritmo
    segue o MESMO PRINCIPIO do FidelityFX CAS da AMD (amplificar o reforco onde
    o contraste local deixa espaco e recuar onde nao deixa), mas foi escrito do
    zero aqui -- nenhuma linha, constante ou tabela do CAS foi copiada. A unica
    dependencia e o ReShade.fxh do proprio ReShade (CC0).

    ------------------------------------------------------------------------
    ONDE ESTE EFEITO TEM DE FICAR NA LISTA -- LEIA ANTES DE ARRASTAR
    ------------------------------------------------------------------------
    Ordem correta na aba Home do ReShade:

        LUMENITE: Kernel 2.0        <- provedor de motion vectors
        DLSS 5 Feed                 <- o add-on dispara DLSS + Neural Rendering AQUI
        DLSS 5 Sharpen              <- ESTE ARQUIVO
        DLSS 5 Grade                <- cor, por ultimo

    NITIDEZ SO PODE FICAR ABAIXO DO "DLSS 5 Feed". Nao existe excecao:

      - acima do Kernel, a alta frequencia injetada vira o sinal que o optical
        flow tenta casar; o Kernel casa o realce em vez do conteudo e o motion
        vector apodrece;
      - entre o Kernel e o Feed, a validacao por luma do Feed reamostra o
        backbuffer no pass dela e passa a comparar um frame que ja nao e o que
        o Kernel viu;
      - em qualquer posicao acima do Feed, o DLSS trata o realce (e o grao que
        ele realca) como detalhe real e o acumula no historico temporal --
        cintilacao que nenhum ajuste aqui conserta.

    Este efeito fica ACIMA do DLSS 5 Grade de proposito. Razao no
    docs\nitidez.md, em uma linha: o estagio de LUT e a S-curve do Grade criam
    degraus finos em gradiente e amplificam a amplitude do grao; realcar depois
    disso transforma os degraus em contorno visivel e multiplica o ruido. Na
    ordem escolhida a nitidez trabalha sobre a saida neural, que e um sinal
    display-referred previsivel -- e o grading, sendo suave e monotonico, so
    remapeia o contraste do realce, nunca o desfaz.

    ------------------------------------------------------------------------
    COMO FUNCIONA (um passe, 9 amostras, tudo em luma)
    ------------------------------------------------------------------------
      1. le a vizinhanca 3x3 e tira a luma BT.709 de cada amostra;
      2. detalhe = luma central - passa-baixa gaussiano 3x3 (unsharp classico);
      3. ganho adaptativo = quanto de espaco o contraste local deixa:
             amp = sqrt( min(mn, 1 - mx) / mx )
         mn/mx sao o minimo e o maximo de luma do bloco. Em bloco de contraste
         medio amp -> 1 (detalhe fino de baixa amplitude, e ai que o realce
         rende); em bloco que ja vai do preto ao branco amp -> 0 (a borda ja
         esta definida e nao ha para onde crescer sem clipar);
      4. porta de ruido: contraste local abaixo de 'Denoise' nao e detalhe, e
         grao de compressao/captura -- o ganho vai a zero suavemente;
      5. limite de halo: a luma final e presa ao intervalo [mn, mx] dos
         vizinhos, alargado por (1 - HaloProtect) * contraste. Com protecao
         maxima nenhum pixel pode ultrapassar seus vizinhos -- overshoot zero,
         que e a definicao de "sem halo";
      6. o delta vai para a cor preservando a cromaticidade (multiplicativo),
         com transicao para aditivo nos pretos, onde dividir pela luma seria
         instavel.

    Com Sharpness = 0 o efeito e identidade e custa uma comparacao.
*/

#include "ReShade.fxh"

// ============================================================== controles

uniform float Sharpness <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_category = "Nitidez";
    ui_label = "Nitidez";
    ui_tooltip = "Intensidade do reforco de detalhe. 0 = desligado (identidade).\n"
                 "0.30-0.45 e a faixa util em captura de console/emulador;\n"
                 "acima de 0.7 o realce comeca a aparecer como textura propria.";
> = 0.35;

uniform float HaloProtect <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
    ui_category = "Nitidez";
    ui_label = "Protecao contra halo";
    ui_tooltip = "Quanto o pixel pode ultrapassar o mais claro e o mais escuro\n"
                 "dos seus oito vizinhos.\n"
                 "1.00 = nao pode ultrapassar nada (overshoot zero, sem halo).\n"
                 "0.00 = pode ultrapassar em ate um contraste local inteiro\n"
                 "       (aspecto 'gravado', com aureola nas bordas).\n"
                 "0.70-0.85 mantem a borda viva sem contorno visivel.";
> = 0.80;

uniform float Denoise <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 0.20; ui_step = 0.005;
    ui_category = "Nitidez";
    ui_label = "Limiar de ruido (contraste minimo)";
    ui_tooltip = "Contraste local, em luma 0..1, abaixo do qual nada e realcado.\n"
                 "E o que impede a nitidez de virar realce de grao em fonte\n"
                 "comprimida (placa de captura, console por HDMI, video).\n"
                 "0.000 = realca tudo, inclusive o grao.\n"
                 "0.020-0.040 = descarta o grao tipico de captura 8 bits.\n"
                 "Suba se a imagem 'fervilhar' em ceu, parede ou sombra lisa.";
> = 0.025;

uniform int DebugView <
    ui_type = "combo";
    ui_category = "Diagnostico";
    ui_label = "Vista de diagnostico";
    ui_items = "Normal\0"
               "Mapa de ganho (branco = realce cheio)\0"
               "Delta aplicado (cinza = zero)\0";
    ui_tooltip = "Mapa de ganho: mostra onde o efeito esta agindo depois da\n"
                 "adaptacao e da porta de ruido. Ajuste 'Limiar de ruido' ate\n"
                 "o ceu/parede lisa ficarem pretos e a textura continuar clara.\n"
                 "Delta aplicado: o que esta sendo somado, x8, em torno de 0.5.";
> = 0;

// ============================================================== auxiliares

static const float3 DLSS5_SHARP_LUMA_W = float3(0.2126, 0.7152, 0.0722);

// F0 (15/09/2026, HDR de 16 bits na ponte): num backbuffer de ponto flutuante
// (BUFFER_COLOR_BIT_DEPTH == 16, R16G16B16A16_FLOAT scRGB) a cor passa de 1.0 e o
// saturate() de sempre a CORTARIA no branco SDR -- era o unico clipping da cadeia
// (medido: cor 2.0 na entrada, 1.0 na saida). Ali so o piso em 0 e mantido. A
// adaptacao ao contraste (min(mn, 1 - mx)) fica como esta: acima de 1.0 ela vai
// a zero sozinha, entao o realce nao toca nas altas luzes HDR -- sem clipping e
// sem inventar detalhe onde o intervalo SDR nao existe. Em 8 e 10 bits nada muda.
#if BUFFER_COLOR_BIT_DEPTH >= 16
    #define DLSS5_SHARP_RANGE(x) max((x), 0.0)
#else
    #define DLSS5_SHARP_RANGE(x) saturate(x)
#endif

float3 DLSS5_SharpTap(float2 uv)
{
    return DLSS5_SHARP_RANGE(tex2Dlod(ReShade::BackBuffer, float4(uv, 0.0, 0.0)).rgb);
}

// ============================================================== pixel shader

float4 PS_DLSS5_Sharpen(float4 vpos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    float3 c = DLSS5_SharpTap(texcoord);

    if (Sharpness <= 0.0)
        return float4(c, 1.0);

    const float2 px = ReShade::PixelSize;

    //   a b c
    //   d e f      e = centro (ja lido em 'c')
    //   g h i
    float3 ta = DLSS5_SharpTap(texcoord + float2(-px.x, -px.y));
    float3 tb = DLSS5_SharpTap(texcoord + float2( 0.0,  -px.y));
    float3 tc = DLSS5_SharpTap(texcoord + float2( px.x, -px.y));
    float3 td = DLSS5_SharpTap(texcoord + float2(-px.x,  0.0 ));
    float3 tf = DLSS5_SharpTap(texcoord + float2( px.x,  0.0 ));
    float3 tg = DLSS5_SharpTap(texcoord + float2(-px.x,  px.y));
    float3 th = DLSS5_SharpTap(texcoord + float2( 0.0,   px.y));
    float3 ti = DLSS5_SharpTap(texcoord + float2( px.x,  px.y));

    float ya = dot(ta, DLSS5_SHARP_LUMA_W);
    float yb = dot(tb, DLSS5_SHARP_LUMA_W);
    float yc = dot(tc, DLSS5_SHARP_LUMA_W);
    float yd = dot(td, DLSS5_SHARP_LUMA_W);
    float ye = dot(c,  DLSS5_SHARP_LUMA_W);
    float yf = dot(tf, DLSS5_SHARP_LUMA_W);
    float yg = dot(tg, DLSS5_SHARP_LUMA_W);
    float yh = dot(th, DLSS5_SHARP_LUMA_W);
    float yi = dot(ti, DLSS5_SHARP_LUMA_W);

    // ---- 1. detalhe = centro - passa-baixa gaussiano 3x3 (1 2 1 / 2 4 2 / 1 2 1)
    float lowpass = (ya + yc + yg + yi
                  + (yb + yd + yf + yh) * 2.0
                  +  ye * 4.0) * (1.0 / 16.0);
    float detail = ye - lowpass;

    // ---- 2. estatistica do bloco: minimo, maximo e contraste em luma
    float mnCross = min(min(yb, yd), min(yf, yh));
    float mxCross = max(max(yb, yd), max(yf, yh));
    float mnDiag  = min(min(ya, yc), min(yg, yi));
    float mxDiag  = max(max(ya, yc), max(yg, yi));

    // as diagonais estao a raiz(2) do centro: entram com meio peso na
    // estatistica, para que uma quina isolada nao abra o intervalo inteiro
    float mn = min(ye, min(mnCross, lerp(mnCross, mnDiag, 0.5)));
    float mx = max(ye, max(mxCross, lerp(mxCross, mxDiag, 0.5)));
    float contrast = mx - mn;

    // ---- 3. adaptacao ao contraste: quanto espaco o bloco deixa para crescer
    //        (o principio do CAS). Bloco de meio-tom -> 1; bloco que ja vai do
    //        preto ao branco -> 0.
    float amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1.0 / 255.0)));

    // ---- 4. porta de ruido: contraste abaixo de 'Denoise' nao e detalhe
    float gate = (Denoise > 0.0)
               ? smoothstep(Denoise, Denoise * 2.0 + (1.0 / 255.0), contrast)
               : 1.0;

    // ganho final. O 1.6 e a escala que faz Sharpness=1 chegar ao limite util
    // (acima disso so o clamp de halo trabalha).
    float gain = Sharpness * 1.6 * amp * gate;
    float delta = detail * gain;

    // ---- 5. limite de halo: prende a luma ao intervalo dos vizinhos,
    //        alargado pelo que a protecao permitir
    float slack = (1.0 - HaloProtect) * contrast;
    float ynew = clamp(ye + delta, mn - slack, mx + slack);
    delta = ynew - ye;

    if (DebugView == 1)
        return float4(gain, gain, gain, 1.0);
    if (DebugView == 2)
    {
        float d = saturate(0.5 + delta * 8.0);
        return float4(d, d, d, 1.0);
    }

    // ---- 6. leva o delta para a cor sem deslocar o matiz.
    //        multiplicativo preserva as razoes entre canais (cromaticidade
    //        intacta); em luma muito baixa a divisao e instavel, entao a
    //        transicao para aditivo acontece abaixo de ~0.05 de luma.
    float3 mulc = c * (ynew / max(ye, 1e-4));
    float3 addc = c + delta;
    c = DLSS5_SHARP_RANGE(lerp(addc, mulc, saturate(ye * 20.0)));   // F0: sem teto em 16F

    return float4(c, 1.0);
}

technique DLSS5_Sharpen
<
    ui_label   = "DLSS 5 Sharpen (nitidez -- ponha ABAIXO do DLSS 5 Feed)";
    ui_tooltip = "Nitidez adaptativa ao contraste, num passe so, trabalhando\n"
                 "em luma para nao deslocar cor.\n\n"
                 "ORDEM: tem de ficar ABAIXO de 'DLSS 5 Feed' e ACIMA de\n"
                 "'DLSS 5 Grade'. Acima do Feed o realce entra no optical flow\n"
                 "e no historico temporal do DLSS -- cintilacao garantida.\n\n"
                 "Com Nitidez = 0 a imagem passa intacta.";
>
{
    pass Sharpen
    {
        VertexShader = PostProcessVS;
        PixelShader  = PS_DLSS5_Sharpen;
    }
}
