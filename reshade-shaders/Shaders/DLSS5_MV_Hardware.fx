/*
    DLSS5_MV_Hardware.fx -- publica no Kernel::tFlow os vetores de movimento estimados
    pelo bloco de video da GPU (D3D12 Video Motion Estimation), calculados fora da
    cadeia por motion_estimate.h e entregues pelo add-on ponte.

    POR QUE ISTO EXISTE
    O app captura a imagem final do jogo; nao existe motion vector do jogo para pegar.
    O DLSS (DLAA) e acumulacao temporal: sem MV ele nao reprojeta o historico e tudo
    que se move borra. O Lumenite_Kernel estima MV em pixel shader, mas isso custa
    tempo da fila 3D, que e o recurso escasso a 3440x1440. O bloco de video da GPU faz
    a mesma estimativa de graca em relacao ao render.

    COMO LIGAR
      1. O app chama, uma vez por quadro, antes do ReShade rodar os efeitos:
             runtime->update_texture_bindings("DLSS5_EXTMV", srv, srv);
         com o SRV de dlss5::MotionEstimator::MotionSRV() (RG16F, delta UV).
      2. No ReShade.ini, PreprocessorDefinitions: DLSS5_MV_PROVIDER=3 (ja esta).
      3. No preset, ordem das tecnicas: DLSS5_MV_Hardware ANTES de DLSS5_Feed, e
         Lumenite_Kernel DESLIGADO (os dois escrevem no mesmo Kernel::tFlow).

    O DLSS5_Feed.fx nao muda: com DLSS5_MV_PROVIDER=3 ele le Kernel::tFlow em 1/8 e
    ja liga o caminho DLSS5_MV_LOWRES.

    IMPORTANTE -- LEIA ANTES DE CONFIAR NOS VETORES
    O VME e casamento de blocos na imagem final: nao conhece geometria, transparencia
    nem UI. Ele acerta muito bem ate ~32 px de deslocamento por quadro na resolucao de
    estimativa e nao inventa movimento numa cena parada (medido: quadro identico -> 0%
    de blocos nao-zero). Acima disso ele nao devolve um vetor grande e errado -- devolve
    um vetor PEQUENO e errado, que nenhum limiar de magnitude pega. Por isso a validacao
    do DLSS5_Feed.fx (MV_VALIDATE, MV_CONSISTENCY, VALIDATE_STATIC) tem de ficar ligada:
    ela compara os patches de imagem de verdade e e a unica coisa que pega esse caso.
*/

#include "ReShade.fxh"

uniform float MV_HW_SCALE <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 2.0; ui_step = 0.01;
    ui_label = "Ganho dos vetores";
    ui_tooltip = "1.0 = como o hardware mediu. Abaixo de 1 encolhe o vetor, o que suaviza\n"
                 "o rastro quando a estimativa esta ruidosa, ao custo de menos ganho temporal.";
> = 1.0;

uniform float MV_HW_MAX_PX <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 256.0; ui_step = 1.0;
    ui_label = "Descarte acima de (px/quadro)";
    ui_tooltip = "Zera vetores maiores que isto. 0 = desligado, que e o padrao.\n\n"
                 "Fica desligado de proposito, por dois motivos. Primeiro, o motion_estimate.h\n"
                 "ja descarta pelo alcance medido do divisor em uso, e dois limiares disputando\n"
                 "o mesmo trabalho so escondem qual dos dois cortou. Segundo, e mais importante:\n"
                 "quando o hardware perde o alvo ele NAO devolve um vetor grande e errado --\n"
                 "devolve um vetor pequeno e errado. Limiar de magnitude nao pega esse caso e\n"
                 "so corta sinal bom. Quem pega e a validacao por patch do DLSS5_Feed.fx.";
> = 0.0;

uniform float MV_HW_EDGE_SHARP <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
    ui_label = "Preservar bordas de movimento";
    ui_tooltip = "Quanto trocar a interpolacao bilinear pela pontual onde dois blocos vizinhos\n"
                 "descrevem movimentos diferentes (a borda de um objeto passando na frente do\n"
                 "fundo).\n\n"
                 "Bilinear ali entrega a MEDIA dos dois movimentos: um vetor que nao descreve nem\n"
                 "o objeto nem o fundo, e e onde nasce o fantasma de borda. Pontual entrega um dos\n"
                 "dois movimentos reais -- certo de um lado da borda, errado do outro, e o lado\n"
                 "errado a validacao por patch do Feed pega.\n\n"
                 "0 = so bilinear, e e o PADRAO. Medido no run 13:32: com 1 a nitidez de saida\n"
                 "caiu 23% (N=3, ~3 sigma). A amostragem pontual reduz o erro do campo de MV em\n"
                 "10-30%, mas o campo com DEGRAU parece servir pior ao DLSS que o suavizado --\n"
                 "ou seja, a metrica intermediaria melhora e a que importa piora. Nao suba isto\n"
                 "sem refazer a medicao de saida com N>=6.";
> = 0.0;

uniform float MV_HW_EDGE_PX <
    ui_type = "slider";
    ui_min = 0.5; ui_max = 32.0; ui_step = 0.5;
    ui_label = "Limiar da borda (px de desacordo)";
    ui_tooltip = "Desacordo entre a amostragem bilinear e a pontual, em pixels, a partir do qual\n"
                 "a troca e total. Abaixo disso a troca e proporcional.\n\n"
                 "2.0 saiu de medicao (edge_test): contra a verdade exata numa borda de movimento,\n"
                 "2.0 captura todo o ganho da amostragem pontual, enquanto 6.0 deixava metade dele\n"
                 "na mesa quando os dois movimentos diferiam pouco. Continua sendo mistura suave,\n"
                 "e nao um degrau, para nao chavear em cima de ruido.";
> = 2.0;

uniform float MV_HW_COHERENCE_PX <
    ui_type = "slider";
    ui_min = 0.5; ui_max = 16.0; ui_step = 0.5;
    ui_label = "Escala da confianca (px de desvio)";
    ui_tooltip = "Desvio, em pixels, entre o vetor de um bloco e a media dos oito vizinhos que\n"
                 "leva a confianca a zero. Bloco que concorda com a vizinhanca e casamento bom;\n"
                 "bloco isolado apontando para outro lado e ruido de block matching.\n\n"
                 "O DLSS5_Feed.fx usa este mapa para pesar o teste da hipotese estatica\n"
                 "(STATIC_DISTRUST): so vale marcar contradicao onde o provider estava seguro.";
> = 4.0;

uniform int MV_HW_DEBUG <
    ui_type = "combo";
    ui_items = "Desligado\0Direcao e velocidade\0Magnitude em px\0";
    ui_label = "Depuracao (tecnica DLSS5_MV_Hardware_Debug)";
> = 0;

// Textura externa: o add-on ponte liga o SRV do estimador nesta semantica. As
// dimensoes e o formato aqui sao so um marcador -- quem manda e o SRV ligado.
texture DLSS5_ExternalMV : DLSS5_EXTMV;
sampler sDLSS5_ExternalMV
{
    Texture = DLSS5_ExternalMV;
    AddressU = CLAMP; AddressV = CLAMP;
    MinFilter = LINEAR; MagFilter = LINEAR; MipFilter = POINT;
};
sampler sDLSS5_ExternalMVPoint
{
    Texture = DLSS5_ExternalMV;
    AddressU = CLAMP; AddressV = CLAMP;
    MinFilter = POINT; MagFilter = POINT; MipFilter = POINT;
};

// O mesmo alvo que o Lumenite_Kernel usaria. Declarado identico ao do
// lumenite_Kernel.fx e ao do DLSS5_Feed.fx para o ReShade compartilhar a textura.
namespace Kernel
{
    texture2D tFlow { Width = BUFFER_WIDTH / 8; Height = BUFFER_HEIGHT / 8; Format = RG16F; };
    texture2D tConfidence { Width = BUFFER_WIDTH / 8; Height = BUFFER_HEIGHT / 8; Format = R16F; };
}

// Le de volta o que o primeiro passe escreveu, para medir a coerencia local.
sampler sPublishedFlow
{
    Texture = Kernel::tFlow;
    AddressU = CLAMP; AddressV = CLAMP;
    MinFilter = POINT; MagFilter = POINT; MipFilter = POINT;
};

float2 PS_Publish(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    // A grade do hardware e mais grossa que a textura de destino, entao alguem tem de
    // interpolar. QUAL interpolacao depende do que ha embaixo, e a escolha importa:
    //
    // Onde a cena inteira se move junto, os blocos vizinhos concordam, bilinear e ponto
    // dao quase o mesmo valor, e o bilinear ganha por suavizar a costura entre blocos.
    //
    // Na DESCONTINUIDADE DE MOVIMENTO -- a borda de um objeto passando na frente do
    // fundo -- os dois blocos vizinhos descrevem movimentos diferentes, e o bilinear
    // entrega a MEDIA dos dois: um vetor que nao descreve nem o objeto nem o fundo, numa
    // faixa da largura de um bloco, com erro da ordem da DIFERENCA entre os dois
    // movimentos. E exatamente ali que nasce o fantasma de borda. O ponto entrega um dos
    // dois movimentos reais: certo para um lado da borda, errado para o outro -- e o
    // lado errado a validacao por patch do DLSS5_Feed.fx pega, porque o patch nao casa.
    // Um vetor certo pela metade e recuperavel; uma media que nao descreve nada, nao.
    //
    // O desacordo entre as duas amostragens mede a descontinuidade sem custo extra: sao
    // dois taps na mesma textura, e onde nao ha borda eles coincidem e o blend nao faz
    // nada.
    const float4 c = float4(uv, 0.0, 0.0);
    const float2 mvLin = tex2Dlod(sDLSS5_ExternalMV,      c).xy;
    const float2 mvPnt = tex2Dlod(sDLSS5_ExternalMVPoint, c).xy;

    const float disagree = length((mvLin - mvPnt) * BUFFER_SCREEN_SIZE);   // px
    const float edge = saturate(disagree / max(MV_HW_EDGE_PX, 0.001)) * MV_HW_EDGE_SHARP;

    float2 mv = lerp(mvLin, mvPnt, edge) * MV_HW_SCALE;

    if (MV_HW_MAX_PX > 0.0)
    {
        const float2 px = mv * BUFFER_SCREEN_SIZE;
        if (dot(px, px) > MV_HW_MAX_PX * MV_HW_MAX_PX) mv = float2(0.0, 0.0);
    }
    return mv;
}

// Confianca por COERENCIA LOCAL do campo.
//
// O D3D12 VME nao devolve custo nem SAD: o ResolveMotionVectorHeap entrega so os
// vetores. Nao ha, portanto, confianca vinda do hardware para publicar. Mas publicar
// 1.0 constante seria pior que nao publicar: o DLSS5_Feed.fx pesa o teste da hipotese
// estatica por este mapa exatamente para separar "provider seguro foi contrariado, isso
// e contradicao" de "provider inseguro, e provavelmente so uma superficie parada". Com
// 1.0 fixo esse discriminador some e todo pixel onde o estatico vence vira contradicao
// -- inclusive as superficies genuinamente paradas, que sao o caso que nao se deve
// marcar. O peso deixaria de pesar.
//
// O que da para medir honestamente e a coerencia: um bloco cujo vetor concorda com os
// oito vizinhos e um casamento confiavel; um bloco isolado apontando para outro lado e
// ruido de block matching. Na fronteira entre dois movimentos a confianca cai, o que
// tambem esta certo -- ali o vetor de bloco mistura conteudo e e mesmo pouco confiavel.
float PS_Confidence(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float2 t = 1.0 / float2(BUFFER_WIDTH / 8, BUFFER_HEIGHT / 8);
    const float2 c = tex2Dlod(sPublishedFlow, float4(uv, 0.0, 0.0)).xy;

    float2 acc = float2(0.0, 0.0);
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        if (x == 0 && y == 0) continue;
        acc += tex2Dlod(sPublishedFlow, float4(uv + float2(x, y) * t, 0.0, 0.0)).xy;
    }

    const float dev = length((c - acc / 8.0) * BUFFER_SCREEN_SIZE);   // em pixels
    const float coherence = saturate(1.0 - dev / max(MV_HW_COHERENCE_PX, 0.001));

    // .z da textura externa e a VALIDADE que o estimador escreveu: 0 marca um bloco que
    // ele proprio descartou por magnitude (deslocamento fora do alcance do hardware).
    //
    // Este bloco chega ao Feed como vetor (0,0), que e indistinguivel de "nao se moveu".
    // A diferenca importa: para um pixel parado, reprojetar o historico da propria
    // posicao esta certo; para um bloco descartado, prende o historico ali. E o teste da
    // hipotese estatica do Feed nao pega, porque ele so roda para vetor acima de meio
    // pixel -- um vetor descartado tem magnitude zero e escapa por baixo.
    //
    // Zerar a confianca aqui e o que da ao Feed o que ele precisa para distinguir os
    // dois casos. Medido: a 90 px de deslocamento com divisor 2, o descarte atinge 45,4%
    // dos blocos -- sem este canal, quase metade do quadro prenderia o historico calada.
    const float valid = tex2Dlod(sDLSS5_ExternalMVPoint, float4(uv, 0.0, 0.0)).z;

    return coherence * saturate(valid);
}

float3 PS_Debug(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float3 img = tex2Dlod(ReShade::BackBuffer, float4(uv, 0.0, 0.0)).rgb;
    if (MV_HW_DEBUG == 0) return img;

    const float2 px = tex2Dlod(sDLSS5_ExternalMV, float4(uv, 0.0, 0.0)).xy * BUFFER_SCREEN_SIZE;
    const float  speed = length(px);

    if (MV_HW_DEBUG == 2)
    {
        // preto = parado, branco = 32 px/quadro (o limite confiavel do hardware)
        return saturate(speed / 32.0).xxx;
    }
    // roda de cor por direcao, brilho por velocidade
    const float angle = atan2(px.y, px.x);
    const float3 rgb = saturate(3.0 * abs(2.0 * frac(angle / 6.283185
                       + float3(0.0, -1.0 / 3.0, 1.0 / 3.0)) - 1.0) - 1.0);
    return lerp(img * 0.35, rgb, saturate(speed / 16.0));
}

technique DLSS5_MV_Hardware <
    ui_label = "DLSS 5: vetores de movimento por hardware";
    ui_tooltip = "Publica em Kernel::tFlow os vetores estimados pelo bloco de video da GPU.\n"
                 "Ponha esta tecnica ACIMA de DLSS 5 Feed e DESLIGUE o LUMENITE: Kernel --\n"
                 "os dois escrevem na mesma textura.\n"
                 "Exige que o app entregue a semantica DLSS5_EXTMV pelo add-on ponte;\n"
                 "sem isso a textura fica preta e o resultado e identico a nao ter MV.";
>
{
    pass { VertexShader = PostProcessVS; PixelShader = PS_Publish;    RenderTarget = Kernel::tFlow; }
    pass { VertexShader = PostProcessVS; PixelShader = PS_Confidence; RenderTarget = Kernel::tConfidence; }
}

technique DLSS5_MV_Hardware_Debug <
    ui_label = "DLSS 5: vetores por hardware (depuracao)";
    ui_tooltip = "Mostra o campo de vetores na tela. Ligue no fim da cadeia.";
    enabled = false;
>
{
    pass { VertexShader = PostProcessVS; PixelShader = PS_Debug; }
}
