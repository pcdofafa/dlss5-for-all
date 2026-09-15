/*
    DLSS5_HUD.fx -- devolve o HUD original por cima da saida da rede, para a cadeia DLSS5 For All.

    Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).
    Codigo original: nao contem nem deriva de shader de terceiro. A unica dependencia e o
    ReShade.fxh do proprio ReShade (CC0).

    ------------------------------------------------------------------------
    O PROBLEMA, em uma frase
    ------------------------------------------------------------------------
    A cadeia captura a JANELA do jogo por Windows Graphics Capture e roda DLAA sobre o
    quadro capturado. Nessa captura nao existe profundidade do jogo (o depth chega sempre
    zero -- e estrutural, ver docs\ARQUITETURA.md) e os motion vectors sao fluxo optico
    estimado na propria imagem, que colapsa em cena escura. Texto de HUD e o pior caso
    possivel para reconstrucao temporal nessas condicoes: alto contraste, borda dura,
    imovel em espaco de tela e sem vetor que o descreva. A rede o borra.

    ------------------------------------------------------------------------
    POR QUE A MASCARA SOZINHA NAO BASTAVA
    ------------------------------------------------------------------------
    O DLSS5_Feed.fx ja sabe marcar HUD e ja entrega essa marca ao NGX pelo
    pInBiasCurrentColorMask. Mas esse parametro ENVIESA a rede na direcao do quadro atual;
    ele nao a desliga. O pixel continua atravessando o modelo e continua saindo filtrado --
    menos borrado que sem a marca, ainda borrado.

    Para um traco de fonte de 1 pixel nao existe "um pouco menos de reconstrucao" que sirva:
    ou o pixel original chega inteiro na tela, ou nao chega. Este efeito e o caminho que
    entrega o pixel original inteiro.

    ------------------------------------------------------------------------
    COMO ELE CONSEGUE FAZER ISSO
    ------------------------------------------------------------------------
    O add-on dispara o DLSS depois que a tecnica "DLSS 5 Feed" TERMINOU de desenhar (o
    evento reshade_render_technique e chamado apos a tecnica), e devolve o resultado no
    backbuffer. Entao qualquer efeito ABAIXO dela na lista ja le a saida da rede.

    O DLSS5_Feed.fx guardou, no passe KeepColor, o backbuffer como estava antes -- que e
    exatamente a entrada do DLSS. Aqui o backbuffer ja e a saida. Basta escolher, pixel a
    pixel, de qual dos dois ler.

    Em DLAA (dlss5-feed.cfg com work_resolution=100) nao ha reescala entre a entrada e a
    saida, entao a recolagem e pixel a pixel exata: o HUD volta identico ao capturado, sem
    reamostragem, sem meio pixel de deslocamento. Abaixo de 100% a entrada da rede e menor
    que a saida e a correspondencia deixa de ser exata -- ver o aviso na chave abaixo.

    ------------------------------------------------------------------------
    ONDE ESTE EFEITO TEM DE FICAR NA LISTA
    ------------------------------------------------------------------------
        DLSS 5 HUD - capture        <- ESTE ARQUIVO (so com "fonte 1"; opcional)
        SMAA                        <- antialiasing morfologico
        LUMENITE: Kernel 2.0        <- provedor de motion vectors
        DLSS 5 Feed                 <- o add-on dispara DLSS + Neural Rendering AQUI
        DLSS 5 HUD                  <- ESTE ARQUIVO
        DLSS 5 Sharpen              <- nitidez
        DLSS 5 Grade                <- cor, por ultimo

    A tecnica de captura e a unica que fica ACIMA de tudo, e o motivo e que ela e a unica
    coisa nesta cadeia que quer o quadro ANTES de qualquer efeito. Ela so serve a opcao
    "fonte 1" (recolar o pixel cru da captura, desfazendo tambem o SMAA); na fonte 0, que e
    o padrao, ela nao escreve nada e pode ficar fora da lista.

    Logo ABAIXO do Feed e ACIMA do Sharpen, de proposito: o HUD recolado passa pelos mesmos
    estagios de pos que o resto da imagem e nao destoa dela. O unico estagio que ele pula e
    a rede, que e o objetivo inteiro. (O Sharpen tem limite de halo proprio, entao passar
    por ele nao contorna o texto.)

    ------------------------------------------------------------------------
    O QUE ESPERAR AO LIGAR
    ------------------------------------------------------------------------
    Regiao de HUD: muda bastante -- ela volta a ser o pixel capturado.
    Regiao de cenario: nao muda nada. Se mudar, a deteccao esta pegando cenario, e o lugar
    de corrigir isso e HUD_MIN_CONTRAST / HUD_STILL_LUMA no DLSS5_Feed.fx (vista de
    depuracao 9 do DLSS5_Feed_Debug pinta de verde o que sera recolado).
*/

#include "ReShade.fxh"

uniform bool HUD_RESTORE_ENABLE <
    ui_category = "HUD restore";
    ui_label = "Put the original HUD back on top of the network's output";
    ui_tooltip = "Off by default: nothing changes until it is measured.\n\n"
                 "Needs the HUD mask ON in DLSS5_Feed.fx -- that is what fills the mask and keeps\n"
                 "the input colour. With it off this effect has nothing to paste and does nothing.\n\n"
                 "Meant for DLAA (dlss5-feed.cfg work_resolution=100). Below 100% the network's\n"
                 "input is smaller than its output and the pasted pixels no longer line up exactly.";
> = false;

uniform float HUD_RESTORE_STRENGTH <
    ui_category = "HUD restore";
    ui_type = "drag"; ui_min = 0.0; ui_max = 1.0; ui_step = 0.05;
    ui_label = "Restore strength";
    ui_tooltip = "1 = a fully masked pixel comes back exactly as captured. Lower values blend it\n"
                 "with the network's version -- useful to see how much of the difference is the HUD\n"
                 "and how much is the mask overreaching.";
> = 1.0;

uniform int HUD_RESTORE_SOURCE <
    ui_category = "HUD restore";
    ui_type = "combo";
    ui_items = "0 - The network's input (after SMAA and anything above the Feed)\0"
               "1 - The raw capture (needs the 'DLSS 5 HUD - capture' technique at the TOP)\0";
    ui_label = "Put back which version";
    ui_tooltip = "0 undoes ONLY the neural stage: the HUD comes back exactly as it was handed to\n"
                 "DLSS, so whatever the person chose to run above the Feed (SMAA, for one) is kept.\n"
                 "That is the default because it changes one thing at a time.\n\n"
                 "1 also undoes everything above the Feed, putting back the pixel as the window\n"
                 "capture delivered it. It needs the 'DLSS 5 HUD - capture' technique enabled ABOVE\n"
                 "SMAA in the list -- that is the only place where the raw frame still exists.\n"
                 "Without it there is nothing captured and this falls back to 0 on its own.";
> = 0;

uniform int HUD_RESTORE_DILATE <
    ui_category = "HUD restore";
    ui_type = "combo";
    ui_items = "0 - none (mask as it is)\0"
               "1 - 1 pixel\0"
               "2 - 2 pixels\0";
    ui_label = "Grow the mask by";
    ui_tooltip = "A font stroke has an antialiased fringe one or two pixels wide around it, and that\n"
                 "fringe is as blurred as the stroke. Growing the mask brings it back with the stroke.\n"
                 "Too much and a ring of scenery around the HUD also stops being reconstructed.";
> = 1;

uniform bool HUD_RESTORE_DEBUG <
    ui_category = "HUD restore";
    ui_label = "Debug: paint what is being pasted";
    ui_tooltip = "Red where the original is coming back, over a darkened image. Checking only.";
> = false;

// Declaradas byte a byte como o DLSS5_Feed.fx as declara: e assim que o ReShade entrega o
// MESMO recurso a dois efeitos (o mesmo mecanismo que o Feed usa para ler o texMotionVectors
// do provedor). Se o DLSS5_Feed.fx nao estiver carregado elas ficam vazias e este efeito,
// lendo mascara zero, nao muda um bit.
texture DLSS5_HudMask { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8; };
texture DLSS5_HudKeep { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA16F; };
sampler sDLSS5_HudMask { Texture = DLSS5_HudMask; AddressU = Clamp; AddressV = Clamp; MinFilter = POINT; MagFilter = POINT; MipFilter = POINT; };
sampler sDLSS5_HudKeep { Texture = DLSS5_HudKeep; AddressU = Clamp; AddressV = Clamp; MinFilter = POINT; MagFilter = POINT; MipFilter = POINT; };

// O quadro CRU, antes de qualquer efeito -- so existe se a tecnica de captura abaixo estiver
// ligada no TOPO da lista, acima do SMAA. Alpha e a bandeira de "isto foi mesmo capturado":
// o ReShade zera a textura ao criar, entao sem a tecnica o alpha fica 0 para sempre e a
// recolagem cai sozinha para a fonte 0, em vez de colar preto na tela.
texture DLSS5_HudRaw { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA16F; };
sampler sDLSS5_HudRaw { Texture = DLSS5_HudRaw; AddressU = Clamp; AddressV = Clamp; MinFilter = POINT; MagFilter = POINT; MipFilter = POINT; };

// Copia o quadro cru. Um passe, uma leitura, uma escrita; desligado nao escreve nada.
float4 PS_HudCapture(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!HUD_RESTORE_ENABLE || HUD_RESTORE_SOURCE != 1) discard;
    return float4(tex2Dlod(ReShade::BackBuffer, float4(uv, 0.0, 0.0)).rgb, 1.0);
}

float3 PS_HudRestore(float4 vpos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    // Desligado o passe nao escreve nada -- so a rasterizacao de dois triangulos, que nao
    // aparece em medicao. E o que mantem o padrao igual bit a bit ao de antes.
    if (!HUD_RESTORE_ENABLE) discard;

    float m = tex2Dlod(sDLSS5_HudMask, float4(uv, 0.0, 0.0)).x;

    // Dilatacao por maximo: a franja antialiasada da letra e tao borrada quanto o traco, e
    // o traco sem a franja volta com um contorno da rede em volta, que fica pior que o
    // borrao inteiro. O raio e pequeno de proposito -- crescer demais e parar de
    // reconstruir um anel de cenario em volta do HUD.
    if (HUD_RESTORE_DILATE > 0)
    {
        const float2 px = BUFFER_PIXEL_SIZE * float(HUD_RESTORE_DILATE);
        [unroll] for (int i = 0; i < 9; ++i)
        {
            const float2 o = float2(i % 3 - 1, i / 3 - 1) * px;
            m = max(m, tex2Dlod(sDLSS5_HudMask, float4(uv + o, 0.0, 0.0)).x);
        }
    }

    m = saturate(m * HUD_RESTORE_STRENGTH);

    const float3 rede = tex2Dlod(ReShade::BackBuffer, float4(uv, 0.0, 0.0)).rgb;
    if (HUD_RESTORE_DEBUG) return saturate(rede * 0.4 + m * float3(1.0, 0.15, 0.1));
    if (m <= 0.0) return rede;   // a esmagadora maioria da tela sai por aqui

    float3 original = tex2Dlod(sDLSS5_HudKeep, float4(uv, 0.0, 0.0)).rgb;
    if (HUD_RESTORE_SOURCE == 1)
    {
        // Cai de volta para a entrada da rede se a tecnica de captura nao estiver na lista:
        // sem o alpha nao houve captura nenhuma, e colar o preto de uma textura zerada por
        // cima do HUD seria trocar um borrao por um buraco.
        const float4 cru = tex2Dlod(sDLSS5_HudRaw, float4(uv, 0.0, 0.0));
        if (cru.a > 0.5) original = cru.rgb;
    }
    return lerp(rede, original, m);
}

// So faz sentido no TOPO da lista, acima do SMAA: e o unico ponto em que o quadro ainda e o
// que a captura de janela entregou. Opcional -- sem ela a fonte 1 volta sozinha para a 0.
technique DLSS5_HUD_Capture
<
    ui_label   = "DLSS 5 HUD - capture (place at the TOP, above SMAA)";
    ui_tooltip = "Keeps a copy of the raw frame so the HUD can be put back the way the window\n"
                 "capture delivered it -- before SMAA, before everything.\n\n"
                 "Only needed with 'Put back which version' = 1 in DLSS5_HUD.fx. Costs one\n"
                 "full-screen copy; with it off, or on source 0, the pass writes nothing.";
>
{
    pass Capture { VertexShader = PostProcessVS; PixelShader = PS_HudCapture; RenderTarget = DLSS5_HudRaw; }
}

technique DLSS5_HUD
<
    ui_label   = "DLSS 5 HUD (place right below DLSS 5 Feed)";
    ui_tooltip = "Puts the original HUD back, untouched, over the network's output.\n\n"
                 "Needs the HUD mask enabled in DLSS5_Feed.fx: that effect is what decides what\n"
                 "counts as HUD and what keeps the colour to paste back.";
>
{
    pass Restore { VertexShader = PostProcessVS; PixelShader = PS_HudRestore; }
}
