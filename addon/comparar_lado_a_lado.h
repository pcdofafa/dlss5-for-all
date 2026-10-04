/*
    comparar_lado_a_lado.h -- o MODO COMPARATIVO da camada: metade esquerda com
    o DLSS 5 aplicado, metade direita com a imagem ORIGINAL.

    OS LADOS FORAM INVERTIDOS EM 14/09/2026 (ver o bloco LADOS INVERTIDOS, em
    aplicar). Este cabecalho e o do begin/finish abaixo ficaram descrevendo o
    arranjo antigo por quatro dias, e o log do proprio modo tambem - corrigidos
    em 18/09/2026, junto com a metade direita preta.

    Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).

    ------------------------------------------------------------------------
    PARA QUE SERVE
    ------------------------------------------------------------------------
    Pedido do usuario em 11/09/2026, para gravar video de comparacao: "a tela
    dividida ao meio, esquerda sem DLSS 5 e direita com". Sem isto, comparar
    exige apertar F6 e cortar o video -- e ninguem consegue mostrar as duas
    metades do MESMO quadro, que e o que convence.

    ------------------------------------------------------------------------
    ONDE ISTO ENTRA, E POR QUE E EXATAMENTE AQUI
    ------------------------------------------------------------------------
    Dois ganchos, os mesmos da mistura_guiada.h:

        reshade_begin_effects  -> o back buffer ainda e o NATIVO (nada rodou).
                                  Guardamos uma copia inteira.
        reshade_finish_effects -> o back buffer JA e a saida da cadeia.
                                  Devolvemos a METADE DIREITA da copia.

    O finish e o ponto certo, e isto foi conferido e nao suposto: o Neural
    Rendering do RenoDX nao e uma tecnica do ReShade -- ele intercepta o
    NVSDK_NGX_*_EvaluateFeature com replace_source=true, ou seja, roda DENTRO
    da chamada NGX do nosso DLSS5_Feed, que E uma tecnica. Logo, quando o
    finish_effects acontece, tudo ja passou: SMAA, Lumenite_Kernel, o Feed com
    o NR dentro dele, e o Sharpen.

    ------------------------------------------------------------------------
    O QUE ESTE MODO NAO MOSTRA, e esta escrito porque a pessoa VAI perguntar
    ------------------------------------------------------------------------
    No quadro SINTETIZADO (o "DLSS Frame Skipping") a cadeia inteira esta
    suprimida: nada roda entre o begin e o finish, e as duas metades ficam
    iguais -- as duas com o quadro deformado pelo campo de movimento. Nao e
    defeito e nao da para contornar aqui: naquele quadro nao existe "com DLSS"
    e "sem DLSS", existe um quadro desenhado por nos. Para gravar comparacao,
    deixe o DLSS FRAME SKIPPING em "Desligado".

    ------------------------------------------------------------------------
    CUSTO
    ------------------------------------------------------------------------
    Duas copias de GPU por quadro: uma inteira no begin e uma de meia tela no
    finish. A 1080p sao ~8 MB + ~4 MB de trafego, na casa de 0,1 a 0,3 ms --
    e so quando o modo esta LIGADO (desligado, a primeira linha de cada funcao
    devolve).
*/
#pragma once

#include <d3d11.h>
#include <cstdio>
#include <cstring>

#include "rotulos_comparar.h"

namespace dlss5_comparar
{

/* A GEOMETRIA DA FONTE (X-CONSOLE / I11, 18/09/2026), escrita pelo app em
   [comparar] do dlss5forall.ini (main.cpp, ComparadorGravarRetangulo):
     bbW/bbH  o back buffer a que os numeros se referem - se nao bater com o
              back buffer vivo (troca de tamanho no meio), a geometria e ignorada;
     fx/fy/fw/fh  onde a IMAGEM esta no begin_effects, em pixels do back buffer;
     cw/ch    a caixa [0,0,cw,ch[ que a cadeia AMPLIA ate o back buffer inteiro
              (= bbW/bbH quando a cadeia e 1:1; menor no caminho curto).
   Tudo zero = app antigo, sem as chaves: vale a regra anterior (copia inteira e
   suspensao no caminho curto). */
struct Geometria
{
    int bbW = 0, bbH = 0;
    int fx = 0, fy = 0, fw = 0, fh = 0;
    int cw = 0, ch = 0;
    bool operator==(const Geometria &o) const
    {
        return bbW == o.bbW && bbH == o.bbH && fx == o.fx && fy == o.fy && fw == o.fw && fh == o.fh &&
               cw == o.cw && ch == o.ch;
    }
    bool vale(UINT w, UINT h) const
    {
        return bbW == (int)w && bbH == (int)h && fw > 0 && fh > 0 && cw > 0 && ch > 0 &&
               cw <= (int)w && ch <= (int)h && fx >= 0 && fy >= 0 &&
               fx + fw <= cw && fy + fh <= ch;
    }
};

typedef HRESULT(WINAPI *PFN_D3DCompile_cmp)(LPCVOID, SIZE_T, LPCSTR, const void *, void *,
                                            LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

/* O passe de ampliacao da copia guardada (so no caminho curto): o mesmo
   triangulo de tela cheia da mistura_guiada.h e um pixel shader que le a copia
   com uv = a + b * uv, onde a/b levam o viewport (a regiao direita da imagem,
   em pixels do back buffer) para a caixa [0,0,cw,ch[ da copia. */
static const char *kCmpVS = R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint id : SV_VertexID) {
    VSOut o;
    o.uv  = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)";
static const char *kCmpPS = R"(
Texture2D    tex  : register(t0);
SamplerState sLin : register(s0);
cbuffer C : register(b0) { float2 a; float2 b; };
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return tex.SampleLevel(sLin, a + b * uv, 0);
}
)";

class Comparador
{
public:
    Comparador() = default;
    ~Comparador() { soltar(); soltar_shaders(); if (m_d3dc) { FreeLibrary(m_d3dc); m_d3dc = nullptr; } }
    Comparador(const Comparador &) = delete;
    Comparador &operator=(const Comparador &) = delete;

    /* divisor em porcento (0..100, com fracao): onde fica a linha, medido
       dentro da IMAGEM. 50 = meio a meio; 0 = tudo original; 100 = tudo com
       DLSS 5. LINHA ARRASTAVEL (25/09/2026, pedido do dono): o app move a
       linha com o mouse e manda a posicao AO VIVO por posicionar() (export
       DLSS5Bridge_CompararAoVivo); o ini guarda a posicao ao soltar.
       rotulos: 0 = sem marcas; 1 = etiquetas pequenas "DLSS 5" / "Original"
       coladas na linha (padrao desde 25/09/2026); 2 = as artes de
       <exeDir>\marca\ (ver rotulos_comparar.h), tambem coladas na linha.
       rotuloAltura: altura das marcas em porcento da altura da tela (3 a 15).
       atraso: quantos quadros o lado ESQUERDO recua para casar com o direito
               (ver o bloco A COMPENSACAO DO ATRASO). 0 desliga.
       exeDir: a pasta do executavel, COM a barra final.
       curtoW/curtoH: short_w/short_h do dlss5-feed.cfg, o RETANGULO do caminho
               curto. Zero = caminho curto desligado. Ver o bloco A METADE
               DIREITA PRETA, em aplicar(). So vale quando o app nao mandou a
               geometria (app antigo).
       geo: a geometria da fonte, ver struct Geometria. nullptr = sem ela. */
    void configurar(bool ligado, float divisor, int rotulos, int rotuloAltura,
                    int atraso, const wchar_t *exeDir, const wchar_t *atalho = nullptr,
                    int curtoW = 0, int curtoH = 0, const Geometria *geo = nullptr, bool ingles = false)
    {
        /* Geometria nova = motivo e log de novo (o que da para desenhar mudou). */
        const Geometria g = geo ? *geo : Geometria{};
        if (!(g == m_geo)) { m_geo = g; m_motivoDito = 0; m_disse = false; m_rotVale = false; }
        /* A TECLA NO CABECALHO (14/09/2026). Trocar o texto obriga a refazer a
           arte, entao so invalida quando ele de fato muda - a pessoa pode
           remapear o atalho com o app aberto. */
        if (atalho != nullptr && wcsncmp(atalho, m_atalho, 63) != 0)
        { wcsncpy(m_atalho, atalho, 63); m_atalho[63] = L'\0'; m_rotVale = false; }
        posicionar(ligado ? 1 : 0, divisor);
        /* IDIOMA ([idioma] lingua do ini do app): troca o texto da dica, refaz a arte */
        if (ingles != m_ingles) { m_ingles = ingles; m_rotVale = false; }
        const int rot = (rotulos >= 0 && rotulos <= 2) ? rotulos : 1;
        if (rot != m_rotulos) { m_rotulos = rot; m_rotVale = false; }
        const int alt = (rotuloAltura >= 3 && rotuloAltura <= 15) ? rotuloAltura : 4;
        if (alt != m_rotPct) { m_rotPct = alt; m_rotVale = false; }
        const int atr = (atraso >= 0 && atraso <= (kGuardados - 1)) ? atraso : 0;
        /* Mudar o atraso muda QUANTAS copias precisam existir; refazer e mais
           simples (e acontece uma vez) do que manter texturas que ninguem le. */
        if (atr != m_atraso) { m_atraso = atr; soltar(); }
        if (exeDir != nullptr && wcsncmp(exeDir, m_dir, MAX_PATH - 1) != 0)
        { wcsncpy(m_dir, exeDir, MAX_PATH - 1); m_dir[MAX_PATH - 1] = L'\0'; m_rotVale = false; }
        /* Trocar de caminho curto muda O QUE DA PARA DESENHAR: deixa o motivo
           dizer de novo, senao o log fica com a explicacao da rodada passada. */
        if (curtoW != m_curtoW || curtoH != m_curtoH)
        { m_curtoW = curtoW; m_curtoH = curtoH; m_motivoDito = 0; }
    }
    bool ligado() const { return m_quer; }

    /* O CAMINHO RAPIDO (25/09/2026): so liga/desliga e a posicao da linha, sem
       mexer em mais nada. E o que o arraste chama a cada movimento do mouse -
       por isso nao refaz textura nenhuma: as marcas nao dependem mais da
       posicao da linha (sao coladas nela na hora de desenhar).
       ligado: -1 = nao mexe, 0 = desliga, 1 = liga.
       divisor: < 0 (ou nao finito) = nao mexe; senao 0..100. */
    void posicionar(int ligado, float divisor)
    {
        if (divisor >= 0.f && divisor == divisor)   /* divisor == divisor: descarta NaN */
        {
            if (divisor > 100.f) divisor = 100.f;
            /* sem refazer a linha do log: arrastar muda isto 60 vezes por segundo */
            m_div = divisor;
        }
        if (ligado == 0 || ligado == 1)
        {
            m_quer = (ligado == 1);
            if (!m_quer) { m_guardados = 0; m_motivoDito = 0; m_sondas = 0; m_quadros = 0; m_disse = false; }
        }
    }
    /* O app tem a linha arrastavel (chamou o caminho ao vivo ao menos uma vez):
       a dica do cabecalho passa a dizer que da para arrastar. */
    void marcar_arrastavel()
    {
        if (!m_arrastavel) { m_arrastavel = true; m_rotVale = false; }
    }
    float divisor() const { return m_div; }

    /* reshade_begin_effects: guarda o quadro ANTES da cadeia. */
    void capturar_original(void *rtvHandle, void (*log)(const char *))
    {
        if (!m_quer || rtvHandle == nullptr) return;
        ID3D11Texture2D *bb = nullptr; ID3D11Device *dev = nullptr;
        if (!abrir(rtvHandle, &bb, &dev)) return;
        D3D11_TEXTURE2D_DESC d{}; bb->GetDesc(&d);
        if (preparar(dev, d, log))
        {
            ID3D11DeviceContext *ctx = nullptr; dev->GetImmediateContext(&ctx);
            if (ctx)
            {
                /* Roda de (atraso + 1) posicoes: escrevemos sempre na seguinte
                   e a leitura escolhe quantas voltar. Com o padrao medido
                   (atraso 0) a roda tem uma posicao so e isto e exatamente o
                   que era antes -- nenhuma textura a mais na VRAM. */
                m_escreve = (m_escreve + 1) % nTex();
                ctx->CopyResource(m_original[m_escreve], bb);
                ctx->Release();
                if (m_guardados < nTex()) m_guardados++;
            }
        }
        bb->Release(); dev->Release();
    }

    /* reshade_finish_effects: devolve a metade direita do que foi guardado. */
    void aplicar(void *rtvHandle, void (*log)(const char *))
    {
        if (!m_quer || m_guardados == 0 || rtvHandle == nullptr) return;
        ID3D11Texture2D *bb = nullptr; ID3D11Device *dev = nullptr;
        if (!abrir(rtvHandle, &bb, &dev)) return;
        D3D11_TEXTURE2D_DESC d{}; bb->GetDesc(&d);
        /* O back buffer mudou de tamanho entre o begin e o finish? Entao a
           copia guardada e de outro quadro e nao serve. Acontece na troca de
           resolucao; o proximo begin refaz tudo.

           O FORMATO ENTRA NA CONFERENCIA (18/09/2026). Antes so o tamanho era
           conferido. Trocar HDR liga/desliga troca o formato do back buffer sem
           mudar o tamanho (R8G8B8A8 <-> R10G10B10A2 / R16G16B16A16), e uma
           CopySubresourceRegion entre grupos de formato diferentes e recusada
           pelo D3D11 SEM devolver erro: o quadro fica como estava e ninguem
           fica sabendo. Desistir aqui, com m_guardados zerado, faz o proximo
           begin refazer a textura no formato novo. */
        if (d.Width != m_w || d.Height != m_h || d.Format != m_fmt)
        { m_guardados = 0; bb->Release(); dev->Release(); return; }

        ID3D11DeviceContext *ctx = nullptr; dev->GetImmediateContext(&ctx);
        if (ctx == nullptr) { bb->Release(); dev->Release(); return; }

        /* ------------------------------------------------------------------
           A COMPENSACAO DO ATRASO, E POR QUE ELA E ZERO
           ------------------------------------------------------------------
           A suspeita era que a cadeia entregasse no finish deste quadro uma
           imagem construida a partir do quadro ANTERIOR, e que por isso a
           emenda saltasse com a cena em movimento.

           MEDIDO, e a suspeita estava ERRADA. Bateria de 45 fotos (alvo
           diagonal a 8 px/quadro, tres condicoes x tres valores de atraso x
           cinco fotos), metrica = degrau na emenda dividido pelo degrau tipico
           da propria imagem no mesmo passo de 3 px (1,00 = a emenda e tao
           suave quanto o resto):

               condicao          atraso 0   atraso 1   atraso 2
               parado              1,00       0,99       1,00
               movimento           0,87       1,78       2,31
               skipping 1 de 2     0,40       2,97       2,23

           Ou seja: sem compensacao JA esta alinhado, e compensar PIORA na
           medida exata de um quadro de deslocamento. O begin e o finish sao o
           mesmo Present e a cadeia nao defasa nada.

           O salto que aparecia em fotos avulsas (uma delas com razao 4,24)
           nao e defasagem do pipeline: se fosse, apareceria nas cinco fotos da
           mesma condicao, e apareceu em uma. E rasgo da CAPTURA - o
           PrintWindow pegando a janela no meio de uma troca de quadro; a foto
           campea mostrava, alem da emenda, uma segunda costura no meio da
           metade direita, que a divisao ao meio nao explica.

           A chave [comparar] atraso fica, com padrao 0, para o caso de alguem
           montar uma cadeia que de fato defase. Recuar nao quebra nada - so
           mostra a esquerda mais velha. */
        int quantos = m_atraso;
        if (quantos > m_guardados - 1) quantos = m_guardados - 1;   /* ainda enchendo a roda */
        const int slot = (m_escreve - quantos + nTex() * 2) % nTex();

        /* ------------------------------------------------------------------
           A GEOMETRIA DA FONTE (X-CONSOLE / I11, 18/09/2026) - a correcao
           definitiva da metade direita preta. O app agora CONTA onde a imagem
           esta no begin e que caixa a cadeia amplia (struct Geometria). Com
           ela, os tres casos do relatorio X-COMPARADOR viram uma regra so:
             - a imagem no espaco do FINISH e a caixa ampliada: img = fonte *
               (back buffer / caixa). Na cadeia 1:1 (letterbox, moldura do
               upstream) e a propria fonte; no caminho curto e ela esticada;
             - a linha divisoria fica no meio da IMAGEM, nao do back buffer -
               com a imagem no canto (moldura) o corte a 50% do back buffer caia
               no preto puro, e era esse o "lado direito preto" do dono;
             - a copia so cobre a imagem: a tarja de letterbox ja e igual dos
               dois lados e nao entra;
             - no caminho curto a copia guardada esta 1:1 e a saida ampliada:
               em vez de suspender, um passe de shader AMPLIA a copia para a
               regiao direita (ampliar()). Sem shader (d3dcompiler ausente,
               formato sem SRV) volta a suspender, com o motivo no log.
           Sem geometria (app antigo, ou os numeros sao de outro tamanho de back
           buffer) vale a regra anterior: copia inteira, suspensao no curto. */
        const bool geoOk = m_geo.vale(m_w, m_h);
        const bool amplia = geoOk && ((UINT)m_geo.cw != m_w || (UINT)m_geo.ch != m_h);
        UINT imgX = 0, imgY = 0, imgR = m_w, imgB = m_h;   /* a imagem, no espaco do finish */
        if (geoOk)
        {
            const double sx = (double)m_w / (double)m_geo.cw, sy = (double)m_h / (double)m_geo.ch;
            imgX = (UINT)((double)m_geo.fx * sx + 0.5);
            imgY = (UINT)((double)m_geo.fy * sy + 0.5);
            imgR = (UINT)((double)(m_geo.fx + m_geo.fw) * sx + 0.5);
            imgB = (UINT)((double)(m_geo.fy + m_geo.fh) * sy + 0.5);
            if (imgR > m_w) imgR = m_w;
            if (imgB > m_h) imgB = m_h;
            if (imgX >= imgR || imgY >= imgB) { imgX = imgY = 0; imgR = m_w; imgB = m_h; }
        }
        /* 0..100 inteiro (25/09/2026): antes o divisor ia so de 5 a 95. Com a
           linha arrastavel a pessoa pode encostar num canto - 0 = imagem inteira
           original, 100 = inteira com DLSS 5 -, e a conta abaixo ja cobre os dois
           extremos (corte == imgX copia tudo; corte == imgR nao copia nada). */
        UINT corte = imgX + (UINT)((double)(imgR - imgX) * (double)m_div / 100.0 + 0.5);
        if (corte > imgR) corte = imgR;
        /* LADOS INVERTIDOS EM 14/09/2026, a pedido do usuario: "nesse modo o
           preset com dlss5 deveria ser do lado esquerdo e o sem do lado
           direito". Antes era o contrario (esquerda original, direita com).
           Ele tem razao de produto: quem le da esquerda para a direita ve
           primeiro o que o programa ENTREGA, e o original entra como
           referencia depois - e nao o inverso, que faz a peca parecer um
           "antes" com um "depois" escondido no canto.
           A CONTA: o back buffer ja chega com DLSS 5 aplicado no quadro
           inteiro, e este bloco devolve o ORIGINAL por cima de uma metade.
           Para o original ficar a DIREITA, copia-se a metade [corte, m_w[ da
           textura guardada e cola-se na MESMA coordenada x = corte (recorte e
           destino andam juntos; colar em 0 espelharia a imagem de lado). */
        /* ------------------------------------------------------------------
           A METADE DIREITA PRETA (18/09/2026, relato do dono: "o comparador tem
           falhado da tela preta do lado direito")
           ------------------------------------------------------------------
           Este arquivo copia BIT A BIT: ele supoe que no begin a imagem ocupa o
           back buffer INTEIRO, do mesmo jeito que a saida do finish ocupa. No
           app essa suposicao nem sempre vale, e quando ela nao vale a metade
           direita copiada e tarja preta, nao imagem:

             CAMINHO CURTO (main.cpp, DrawAndPresent). "A captura e desenhada
             1:1 no CANTO SUPERIOR ESQUERDO" de um retangulo short_w x short_h e
             a rede o amplia ate o back buffer inteiro. O back buffer ja nasceu
             limpo em PRETO (ClearRenderTargetView), entao no begin tudo o que
             esta a direita de short_w e preto puro - e e exatamente de la que
             sai a metade direita. Com 1080p no canto de um back buffer 4K, o
             corte de 50% (x=1920) cai inteiro na tarja.

             MOLDURA DO UPSTREAM (fonte_ponte.h: ClearRenderTargetView preto +
             a cor interna 1:1 no canto). Mesmo desenho, mesmo resultado.

             LETTERBOX (DrawAndPresent, viewport de proporcao): a imagem fica
             centrada e sobra tarja preta simetrica nas laterais - aqui so a
             beirada da metade direita fica preta, nao a metade toda.

           CONSERTAR DE VERDADE pedia ESCALAR a copia guardada (retangulo ->
           back buffer inteiro), e escalar e um passe de shader: este arquivo so
           sabe copiar. Entao a regra aqui e a do menor risco: quando se sabe
           que a metade direita cairia fora da imagem, NAO DESENHA NADA - a
           pessoa fica com o quadro inteiro com DLSS 5, que e o ultimo estado
           bom, em vez de meia tela preta - e o log diz por que. */
        const char *motivo = nullptr;
        int motivoId = 0;
        char motivoBuf[300];
        /* MOVER A LINHA NAO SALVA: a faixa [short_w, m_w[ e preta sempre, e ela
           esta a direita de qualquer corte. E cortar so ate short_w tambem nao:
           a saida do finish e o retangulo AMPLIADO ate o back buffer inteiro,
           enquanto a copia guardada esta 1:1 - colar 1:1 por cima da ampliada
           mostraria um pedaco ERRADO da cena, em outra escala, o que e pior do
           que preto porque parece certo. So resta suspender. */
        /* (Esta suspensao so vale SEM a geometria do app - ver o bloco acima.) */
        if (!geoOk && m_curtoW > 0 && m_curtoH > 0 &&
            ((UINT)m_curtoW < m_w || (UINT)m_curtoH < m_h))
        {
            motivoId = 1;
            snprintf(motivoBuf, sizeof(motivoBuf),
                     "[DLSS 5 Bridge] comparativo SUSPENSO (o quadro sai inteiro com DLSS 5, sem a "
                     "divisao): o caminho curto esta ligado (short_w=%d short_h=%d no dlss5-feed.cfg) "
                     "e no begin_effects a imagem original ocupa so esse retangulo no canto do back "
                     "buffer %ux%u - o resto e tarja preta, e a rede e quem amplia. A metade direita "
                     "sairia PRETA. Para gravar a comparacao, desligue [ultima] caminho_curto.",
                     m_curtoW, m_curtoH, m_w, m_h);
            motivo = motivoBuf;
        }
        /* Com a geometria e ampliacao, o passe de shader precisa existir. */
        if (motivo == nullptr && amplia && (m_srv[slot] == nullptr || !shaders_garantir(dev, log)))
        {
            motivoId = 2;
            snprintf(motivoBuf, sizeof(motivoBuf),
                     "[DLSS 5 Bridge] comparativo SUSPENSO (o quadro sai inteiro com DLSS 5): o caminho "
                     "curto esta ligado (caixa %dx%d ampliada para %ux%u) e o passe que ampliaria a copia "
                     "original nao montou (%s). Para gravar a comparacao, desligue [ultima] caminho_curto.",
                     m_geo.cw, m_geo.ch, m_w, m_h,
                     m_srv[slot] == nullptr ? "sem SRV neste formato de back buffer" : "d3dcompiler/shader indisponivel");
            motivo = motivoBuf;
        }

        if (motivo == nullptr && corte < imgR && imgY < imgB)
        {
            if (!amplia)
            {
                /* Cadeia 1:1: a copia e a propria imagem, so a regiao direita DELA. */
                D3D11_BOX box{};
                box.left = corte; box.right = imgR;
                box.top = imgY;   box.bottom = imgB;
                box.front = 0;    box.back = 1;
                ctx->CopySubresourceRegion(bb, 0, corte, imgY, 0, m_original[slot], 0, &box);
            }
            else
                ampliar(ctx, reinterpret_cast<ID3D11RenderTargetView *>(rtvHandle), slot, corte, imgY, imgR, imgB);
        }

        /* A SONDA, e ela SO FALA - nao impede desenho nenhum. Os outros dois
           casos de tarja (moldura do upstream, letterbox) o add-on nao tem como
           deduzir sozinho: nada no ini nem no cfg conta o retangulo. Entao le
           128 pixels da metade direita da copia, tres vezes por sessao, e se
           estiverem todos IGUAIS entre si escreve isso no log. Nao bloqueia
           porque um ceu liso ou um fade tambem sao uniformes, e sumir com a
           comparacao no meio de uma gravacao por causa de um palpite seria pior
           do que o defeito. O que ela entrega e a frase que faltava no relato:
           qual dos casos e. */
        /* (So sem a geometria do app: com ela os tres casos sao deterministicos.) */
        if (motivo == nullptr && !geoOk && m_sondas < 3 && ++m_quadros >= sondaEm(m_sondas))
        {
            const int r = sonda_uniforme(dev, ctx, m_original[slot], corte);
            m_sondas++;
            if (r == 1 && log)
            {
                char l[260];
                snprintf(l, sizeof(l),
                         "[DLSS 5 Bridge] comparativo: os 128 pixels lidos na metade direita da copia "
                         "sao todos IGUAIS (back buffer %ux%u, corte x=%u, formato %d). Se o lado direito "
                         "estiver preto, a imagem original nao ocupa o back buffer inteiro no "
                         "begin_effects - moldura da ponte ou tarja de letterbox.",
                         m_w, m_h, corte, (int)m_fmt);
                log(l);
            }
        }

        /* A LINHA DIVISORIA. Uma textura de 2 px de largura pela altura da
           tela, branca: sem ela, numa cena de cor parecida dos dois lados,
           nao da para ver onde uma metade acaba.

           A PRIMEIRA VERSAO COPIAVA PIXEL A PIXEL de uma textura 1x1 --
           2 x altura chamadas de CopySubresourceRegion por quadro, 2160 a
           1080p. Cada chamada custa perto de um microssegundo de CPU: a
           risca sozinha comia mais tempo do que as duas copias de quadro
           somadas. Agora e uma chamada. */
        /* A LINHA ARRASTAVEL (25/09/2026). A risca passou de 2 px brancos a
           kLinhaW px com borda escura dos dois lados - branca pura some num ceu
           claro, escura pura some numa noite; o par aparece nos dois. No meio da
           altura vai a ALCA (m_alca): o sinal de "isto se arrasta", no mesmo
           lugar onde a janela fina do app (comparativo.h) pega o mouse. Nos
           extremos (0% e 100%) a linha encosta DENTRO da imagem, para continuar
           visivel e agarravel. */
        if (motivo == nullptr && m_marca && imgY < imgB && imgR - imgX >= (UINT)kLinhaW)
        {
            UINT lx = (corte >= imgX + kLinhaW / 2) ? corte - kLinhaW / 2 : imgX;
            if (lx + kLinhaW > imgR) lx = imgR - kLinhaW;
            /* so a altura da imagem (com letterbox, a risca nao entra na tarja) */
            D3D11_BOX rb{};
            rb.left = 0; rb.right = kLinhaW; rb.top = imgY; rb.bottom = imgB; rb.front = 0; rb.back = 1;
            ctx->CopySubresourceRegion(bb, 0, lx, imgY, 0, m_marca, 0, &rb);
            if (m_alca && imgB - imgY >= (UINT)kAlcaH && imgR - imgX >= (UINT)kAlcaW)
            {
                UINT ax = (corte >= imgX + kAlcaW / 2) ? corte - kAlcaW / 2 : imgX;
                if (ax + kAlcaW > imgR) ax = imgR - kAlcaW;
                const UINT ay = imgY + (imgB - imgY - kAlcaH) / 2;
                ctx->CopySubresourceRegion(bb, 0, ax, ay, 0, m_alca, 0, nullptr);
            }
        }

        /* AS MARCAS. Depois da risca de proposito: elas ficam POR CIMA de
           tudo, inclusive da metade copiada. Com o modo suspenso nao entram:
           marca sem metade comparada e so um adesivo mentindo sobre o quadro. */
        if (motivo == nullptr)
        {
            rotulos_garantir(dev, log);
            rotulos_desenhar(ctx, bb, corte, imgX, imgY, imgR, imgB);
        }

        ctx->Release();
        bb->Release(); dev->Release();

        /* O MOTIVO VAI PARA O LOG UMA VEZ POR CASO, e nao uma vez por sessao:
           se a razao mudar (o caminho curto engatou no meio, o divisor mudou), a
           frase nova sai. Por quadro seria um log de 60 linhas por segundo. */
        if (motivo != nullptr)
        {
            if (m_motivoDito != motivoId) { m_motivoDito = motivoId; m_disse = false; if (log) log(motivo); }
            return;
        }

        if (!m_disse)
        {
            m_disse = true;
            /* LADOS CORRIGIDOS NO TEXTO (18/09/2026). Esta linha continuava
               dizendo "esquerda = ORIGINAL" depois da inversao de 14/09, que e o
               contrario do que o codigo faz - e e por esta linha que se lia o log
               para saber qual lado falhou. */
            char line[400];
            snprintf(line, sizeof(line),
                     "[DLSS 5 Bridge] modo comparativo ligado: esquerda %u px = com DLSS 5, "
                     "direita %u px = imagem ORIGINAL (divisor %.1f%%, atraso compensado %d quadro(s), "
                     "back buffer %ux%u, imagem em %u,%u %ux%u%s). No quadro sintetizado do frame "
                     "skipping os dois lados sao iguais - desligue o skipping para gravar.",
                     corte - imgX, imgR - corte, (double)m_div, m_atraso, m_w, m_h,
                     imgX, imgY, imgR - imgX, imgB - imgY,
                     !geoOk ? ", sem geometria do app: copia inteira"
                            : amplia ? ", caminho curto: a copia original e AMPLIADA por shader no lado direito"
                                     : ", cadeia 1:1: copia so dentro da imagem");
            if (log) log(line);
        }
    }

private:
    /* ---------------------------------------------------------------- */
    /* O PASSE DE AMPLIACAO (caminho curto com a geometria do app)       */
    /* ---------------------------------------------------------------- */

    /* Estado do contexto que o passe mexe, devolvido no fim: o ReShade refaz o
       estado dele depois do finish_effects, mas o ponteiro desenhado e a
       publicacao do 2x vem DEPOIS de nos no mesmo gancho e leem o que
       deixamos. */
    struct Guarda
    {
        explicit Guarda(ID3D11DeviceContext *c) : ctx(c)
        {
            ctx->RSGetViewports(&nvp, vp);
            ctx->OMGetRenderTargets(1, &rtv, &dsv);
            ctx->VSGetShader(&vs, nullptr, nullptr);
            ctx->PSGetShader(&ps, nullptr, nullptr);
            ctx->GSGetShader(&gs, nullptr, nullptr);
            ctx->PSGetShaderResources(0, 1, &srv);
            ctx->PSGetSamplers(0, 1, &samp);
            ctx->PSGetConstantBuffers(0, 1, &cb);
            ctx->IAGetInputLayout(&il);
            ctx->IAGetPrimitiveTopology(&topo);
            ctx->RSGetState(&rs);
            ctx->OMGetBlendState(&bs, bf, &bm);
            ctx->OMGetDepthStencilState(&dss, &sref);
        }
        ~Guarda()
        {
            if (nvp) ctx->RSSetViewports(nvp, vp);
            ctx->OMSetRenderTargets(1, &rtv, dsv);
            ctx->VSSetShader(vs, nullptr, 0);
            ctx->PSSetShader(ps, nullptr, 0);
            ctx->GSSetShader(gs, nullptr, 0);
            ctx->PSSetShaderResources(0, 1, &srv);
            ctx->PSSetSamplers(0, 1, &samp);
            ctx->PSSetConstantBuffers(0, 1, &cb);
            ctx->IASetInputLayout(il);
            ctx->IASetPrimitiveTopology(topo);
            ctx->RSSetState(rs);
            ctx->OMSetBlendState(bs, bf, bm);
            ctx->OMSetDepthStencilState(dss, sref);
            if (rtv) rtv->Release(); if (dsv) dsv->Release();
            if (vs) vs->Release();   if (ps) ps->Release();   if (gs) gs->Release();
            if (srv) srv->Release(); if (samp) samp->Release(); if (cb) cb->Release();
            if (il) il->Release();   if (rs) rs->Release();   if (bs) bs->Release();
            if (dss) dss->Release();
        }
        ID3D11DeviceContext *ctx;
        UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        ID3D11RenderTargetView *rtv = nullptr; ID3D11DepthStencilView *dsv = nullptr;
        ID3D11VertexShader *vs = nullptr; ID3D11PixelShader *ps = nullptr; ID3D11GeometryShader *gs = nullptr;
        ID3D11ShaderResourceView *srv = nullptr; ID3D11SamplerState *samp = nullptr; ID3D11Buffer *cb = nullptr;
        ID3D11InputLayout *il = nullptr; D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ID3D11RasterizerState *rs = nullptr; ID3D11BlendState *bs = nullptr; float bf[4] = { 0, 0, 0, 0 }; UINT bm = 0xFFFFFFFF;
        ID3D11DepthStencilState *dss = nullptr; UINT sref = 0;
    };

    /* Compila e cria o que o passe precisa, uma vez. d3dcompiler por
       LoadLibrary, como a mistura_guiada.h: o build do add-on nao linka com
       ele, e falhar aqui so suspende o comparativo no caminho curto. */
    bool shaders_garantir(ID3D11Device *dev, void (*log)(const char *))
    {
        if (m_vs && m_ps && m_samp && m_cb) return true;
        if (m_shaderFalhou) return false;
        m_shaderFalhou = true;   /* so volta a false se tudo montar */
        if (m_d3dc == nullptr)
        {
            m_d3dc = LoadLibraryA("d3dcompiler_47.dll");
            if (m_d3dc == nullptr) m_d3dc = LoadLibraryA("d3dcompiler_46.dll");
            if (m_d3dc == nullptr) { if (log) log("[DLSS 5 Bridge] comparativo: d3dcompiler nao encontrado - sem ampliacao"); return false; }
        }
        PFN_D3DCompile_cmp compile = (PFN_D3DCompile_cmp)GetProcAddress(m_d3dc, "D3DCompile");
        if (compile == nullptr) { if (log) log("[DLSS 5 Bridge] comparativo: D3DCompile ausente - sem ampliacao"); return false; }
        auto faz = [&](const char *src, const char *alvo, ID3DBlob **bc) -> bool {
            ID3DBlob *err = nullptr;
            const HRESULT hr = compile(src, strlen(src), nullptr, nullptr, nullptr, "main", alvo, 0, 0, bc, &err);
            if (FAILED(hr) && log)
            {
                char m[400];
                snprintf(m, sizeof(m), "[DLSS 5 Bridge] comparativo: shader falhou: %s",
                         err ? (const char *)err->GetBufferPointer() : "sem detalhe");
                log(m);
            }
            if (err) err->Release();
            return SUCCEEDED(hr);
        };
        ID3DBlob *b = nullptr;
        if (!faz(kCmpVS, "vs_5_0", &b)) return false;
        bool ok = SUCCEEDED(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &m_vs));
        b->Release(); b = nullptr;
        if (ok && faz(kCmpPS, "ps_5_0", &b))
        { ok = SUCCEEDED(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &m_ps)); b->Release(); }
        else ok = false;
        if (ok)
        {
            D3D11_SAMPLER_DESC s{};
            s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            s.MaxLOD = D3D11_FLOAT32_MAX;
            ok = SUCCEEDED(dev->CreateSamplerState(&s, &m_samp));
        }
        if (ok)
        {
            D3D11_BUFFER_DESC c{};
            c.ByteWidth = 16; c.Usage = D3D11_USAGE_DYNAMIC;
            c.BindFlags = D3D11_BIND_CONSTANT_BUFFER; c.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            ok = SUCCEEDED(dev->CreateBuffer(&c, nullptr, &m_cb));
        }
        if (!ok) { soltar_shaders(); if (log) log("[DLSS 5 Bridge] comparativo: o passe de ampliacao nao montou - sem ampliacao"); return false; }
        m_shaderFalhou = false;
        if (log) log("[DLSS 5 Bridge] comparativo: passe de ampliacao pronto (caminho curto: a copia original e ampliada no lado direito)");
        return true;
    }

    /* Desenha a copia guardada AMPLIADA na regiao [x0,x1[ x [y0,y1[ do back
       buffer. A copia e 1:1 na caixa [0,0,cw,ch[; o back buffer e essa caixa
       esticada, entao o pixel p do back buffer le a copia em p * (caixa / bb),
       e em uv da textura (que e do tamanho do bb) isso e p * caixa / bb^2. */
    void ampliar(ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv, int slot,
                 UINT x0, UINT y0, UINT x1, UINT y1)
    {
        if (rtv == nullptr || m_srv[slot] == nullptr || x1 <= x0 || y1 <= y0) return;
        Guarda g(ctx);
        {
            D3D11_MAPPED_SUBRESOURCE m{};
            if (FAILED(ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
            const float kx = (float)m_geo.cw / ((float)m_w * (float)m_w);
            const float ky = (float)m_geo.ch / ((float)m_h * (float)m_h);
            const float c[4] = { (float)x0 * kx, (float)y0 * ky, (float)(x1 - x0) * kx, (float)(y1 - y0) * ky };
            memcpy(m.pData, c, sizeof(c));
            ctx->Unmap(m_cb, 0);
        }
        ID3D11ShaderResourceView *nulo = nullptr;
        ID3D11RenderTargetView *rtNulo = nullptr;
        D3D11_VIEWPORT vp{ (float)x0, (float)y0, (float)(x1 - x0), (float)(y1 - y0), 0.f, 1.f };
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_vs, nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->PSSetShader(m_ps, nullptr, 0);
        ctx->PSSetSamplers(0, 1, &m_samp);
        ctx->PSSetConstantBuffers(0, 1, &m_cb);
        ctx->RSSetState(nullptr);
        ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(nullptr, 0);
        ctx->RSSetViewports(1, &vp);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->PSSetShaderResources(0, 1, &m_srv[slot]);
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(0, 1, &nulo);
        ctx->OMSetRenderTargets(1, &rtNulo, nullptr);
    }

    void soltar_shaders()
    {
        if (m_vs) { m_vs->Release(); m_vs = nullptr; }
        if (m_ps) { m_ps->Release(); m_ps = nullptr; }
        if (m_samp) { m_samp->Release(); m_samp = nullptr; }
        if (m_cb) { m_cb->Release(); m_cb = nullptr; }
    }

    static bool abrir(void *rtvHandle, ID3D11Texture2D **tex, ID3D11Device **dev)
    {
        /* Em D3D, o handle de resource_view E o ponteiro da ID3D11View -- o
           mesmo atalho que a mistura_guiada.h usa e documenta. */
        ID3D11RenderTargetView *rtv = reinterpret_cast<ID3D11RenderTargetView *>(rtvHandle);
        if (rtv == nullptr) return false;
        ID3D11Resource *res = nullptr;
        rtv->GetResource(&res);
        if (res == nullptr) return false;
        HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(tex));
        res->Release();
        if (FAILED(hr) || *tex == nullptr) return false;
        (*tex)->GetDevice(dev);
        if (*dev == nullptr) { (*tex)->Release(); *tex = nullptr; return false; }
        return true;
    }

    bool preparar(ID3D11Device *dev, const D3D11_TEXTURE2D_DESC &d, void (*log)(const char *))
    {
        if (m_original[0] && d.Width == m_w && d.Height == m_h && d.Format == m_fmt) return true;
        soltar();
        /* shaders sao do DEVICE: outro device (runtime refeito) = refazer */
        if (m_devShaders != dev) { soltar_shaders(); m_shaderFalhou = false; m_devShaders = dev; }
        D3D11_TEXTURE2D_DESC t{};
        t.Width = d.Width; t.Height = d.Height;
        t.MipLevels = 1; t.ArraySize = 1;
        t.Format = d.Format;
        t.SampleDesc.Count = 1;
        t.Usage = D3D11_USAGE_DEFAULT;
        /* SRV alem da copia (X-CONSOLE / I11): o passe de ampliacao do caminho
           curto le a copia como textura. Sem SRV (formato typeless, por
           exemplo) a copia continua servindo e so a ampliacao fica de fora. */
        t.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        for (int i = 0; i < nTex(); i++)
        {
            if (FAILED(dev->CreateTexture2D(&t, nullptr, &m_original[i])) || m_original[i] == nullptr)
            {
                /* um device que recusa o bind de SRV neste formato: tenta como antes, so copia */
                t.BindFlags = 0;
                if (FAILED(dev->CreateTexture2D(&t, nullptr, &m_original[i])) || m_original[i] == nullptr)
                {
                    if (log) log("[DLSS 5 Bridge] modo comparativo: nao consegui criar a textura da copia");
                    soltar();
                    return false;
                }
            }
            if (t.BindFlags != 0)
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                sd.Format = d.Format;
                sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                sd.Texture2D.MipLevels = 1;
                if (FAILED(dev->CreateShaderResourceView(m_original[i], &sd, &m_srv[i]))) m_srv[i] = nullptr;
            }
        }
        /* A risca: 2 px de largura pela altura inteira, branca. Falhar aqui
           NAO desliga o modo -- sem ela a divisao continua valendo, so nao
           tem a linha. Em formato de 10 ou 16 bits nao ha conversao e a
           risca fica de fora, junto com as marcas. */
        /* 25/09/2026: kLinhaW px - borda escura, miolo branco, borda escura. */
        dlss5_rotulos::ImagemBGRA risca;
        risca.w = kLinhaW; risca.h = d.Height;
        risca.px.assign((size_t)risca.w * risca.h * 4, 0xFF);
        for (UINT y = 0; y < risca.h; y++)
            for (UINT x : { 0u, (UINT)kLinhaW - 1u })
            {
                uint8_t *p = &risca.px[((size_t)y * risca.w + x) * 4];
                p[0] = p[1] = p[2] = 20;   /* quase preto; alfa fica 255 */
            }
        m_marca = dlss5_rotulos::subir(dev, d.Format, risca);
        /* A ALCA: um cartao escuro com borda branca e dois tracos verticais
           (o desenho universal de "puxador"). Opaca, como tudo que vai por
           copia de bloco. */
        dlss5_rotulos::ImagemBGRA alca;
        alca.w = kAlcaW; alca.h = kAlcaH;
        alca.px.assign((size_t)alca.w * alca.h * 4, 0xFF);
        for (UINT y = 0; y < alca.h; y++)
            for (UINT x = 0; x < alca.w; x++)
            {
                uint8_t *p = &alca.px[((size_t)y * alca.w + x) * 4];
                const bool borda = x < 2 || y < 2 || x >= alca.w - 2 || y >= alca.h - 2;
                const bool traco = (y >= alca.h / 4 && y < alca.h - alca.h / 4) &&
                                   (x == alca.w / 2 - 3 || x == alca.w / 2 - 2 ||
                                    x == alca.w / 2 + 1 || x == alca.w / 2 + 2);
                const uint8_t v = (borda || traco) ? 245 : 28;
                p[0] = p[1] = p[2] = v;
            }
        m_alca = dlss5_rotulos::subir(dev, d.Format, alca);

        m_w = d.Width; m_h = d.Height; m_fmt = d.Format;
        m_rotVale = false;
        return true;
    }

    /* ---------------------------------------------------------------- */
    /* AS MARCAS "ORIGINAL" e "DLSS 5 FOR ALL"                           */
    /* ---------------------------------------------------------------- */

    /* Cria as duas texturas quando faltam, e refaz quando a tela muda de
       tamanho ou a linha muda de lugar (o tamanho das marcas depende do
       espaco de cada metade). Roda no maximo uma vez por mudanca -- nunca
       por quadro. */
    /* 25/09/2026: NAO DEPENDE MAIS DA POSICAO DA LINHA. Antes as marcas eram
       refeitas (PNG lido do disco pelo WIC + duas texturas novas) toda vez que
       o corte mudava - aceitavel com a linha parada, um desastre com ela sendo
       arrastada (60 releituras por segundo dentro do finish_effects). Agora o
       tamanho sai so da altura da tela e de 45% da largura, e a posicao e
       decidida na hora de desenhar, colada na linha. */
    void rotulos_garantir(ID3D11Device *dev, void (*log)(const char *))
    {
        if (m_rotVale) return;
        if (m_rotulos == 0) { rotulos_soltar(); m_rotVale = true; return; }

        rotulos_soltar();
        m_rotVale = true;              /* tentamos uma vez; nao repetir por quadro */

        if (!dlss5_rotulos::formato_serve(m_fmt, nullptr))
        {
            if (!m_rotDisse && log)
            {
                m_rotDisse = true;
                char l[200];
                snprintf(l, sizeof(l),
                         "[DLSS 5 Bridge] comparativo: back buffer no formato %d (HDR) - as marcas "
                         "ORIGINAL/DLSS 5 nao sao desenhadas neste formato.", (int)m_fmt);
                log(l);
            }
            return;
        }

        /* A ALTURA E CONFIGURAVEL ([comparar] rotulo_altura, 5 a 15% da altura
           da tela). Comecou fixa em 7% e o resultado ficou pequeno demais para
           video -- num quadro de 1080p o selo tinha 75 px, e num corte de
           YouTube a palavra sumia. O padrao passou a 9%. A margem e 1,5% da
           altura, igual dos dois lados. */
        UINT alturaPedida = (UINT)((double)m_h * (double)m_rotPct / 100.0 + 0.5);
        if (alturaPedida < 18) alturaPedida = 18;
        const UINT margem = (UINT)((double)m_h * 0.012 + 0.5);

        /* cada marca cabe em 45% da largura: as duas lado a lado com a linha no
           meio, sem nunca se sobreporem com o divisor em 50% */
        const UINT dispEsq = (UINT)((double)m_w * 0.45);
        const UINT dispDir = dispEsq;
        if (dispEsq < 16) return;

        dlss5_rotulos::ImagemBGRA a, b;
        bool usouReserva = false;
        for (int tentativa = 0; tentativa < 2; tentativa++)
        {
            wchar_t caminho[MAX_PATH];
            /* LADOS INVERTIDOS EM 14/09/2026 (ver o bloco da copia): agora a
               ESQUERDA e a imagem COM DLSS 5 e a DIREITA e a original. `a` e
               sempre o rotulo da esquerda e `b` o da direita, entao quem troca
               de lugar sao os arquivos carregados - e nao o desenho, que
               continua colando `a` na esquerda e `b` na direita. */
            if (m_rotulos == 1)
            {
                /* ETIQUETAS PEQUENAS (25/09/2026, pedido do dono): texto "DLSS 5"
                   e "Original", sem as artes. Iguais em pt e en: "Original" e a
                   mesma palavra nas duas linguas. */
                dlss5_rotulos::desenhar_etiqueta(L"DLSS 5", alturaPedida, a);
                dlss5_rotulos::desenhar_etiqueta(L"Original", alturaPedida, b);
            }
            else
            {
                _snwprintf(caminho, MAX_PATH, L"%smarca\\comparar-dlss5.png", m_dir);
                caminho[MAX_PATH - 1] = L'\0';
                if (!dlss5_rotulos::carregar_png(caminho, alturaPedida, a))
                { usouReserva = true; dlss5_rotulos::desenhar_texto(L"DLSS 5", alturaPedida, a); }

                _snwprintf(caminho, MAX_PATH, L"%smarca\\comparar-original.png", m_dir);
                caminho[MAX_PATH - 1] = L'\0';
                if (!dlss5_rotulos::carregar_png(caminho, alturaPedida, b))
                { usouReserva = true; dlss5_rotulos::desenhar_texto(L"Original", alturaPedida, b); }
            }

            if (!a.valida() || !b.valida()) return;

            /* As duas TEM de sair com a mesma altura -- e o que faz parecer um
               par e nao dois adesivos. Entao, se alguma nao couber na sua
               metade, as DUAS encolhem pelo mesmo fator e recarregamos. */
            double fator = 1.0;
            if (a.w > dispEsq) fator = (double)dispEsq / (double)a.w;
            if (b.w > dispDir) { const double f2 = (double)dispDir / (double)b.w; if (f2 < fator) fator = f2; }
            if (fator > 0.995 || tentativa == 1) break;

            UINT nova = (UINT)((double)alturaPedida * fator);
            if (nova < 16) nova = 16;
            if (nova >= alturaPedida) break;
            alturaPedida = nova;
        }

        m_rotEsq = dlss5_rotulos::subir(dev, m_fmt, a);
        m_rotDir = dlss5_rotulos::subir(dev, m_fmt, b);
        m_rotEsqW = a.w; m_rotEsqH = a.h;
        m_rotDirW = b.w; m_rotDirH = b.h;
        m_rotMargem = margem;

        /* A TECLA. Texto desenhado (nao ha arte para isso, e nem deveria haver:
           a pessoa pode remapear o atalho). Metade da altura das marcas - e
           informacao de servico, nao titulo; competir com as marcas tiraria o
           olho da imagem, que e o que se quer comparar. */
        if (m_atalho[0] != L'\0')
        {
            dlss5_rotulos::ImagemBGRA t;
            wchar_t frase[128];
            /* pt/en como o T(pt, en) do app; o idioma vem de [idioma] lingua */
            const wchar_t *fmt = m_ingles
                ? (m_arrastavel ? L"%s on/off · drag the line" : L"%s on/off")
                : (m_arrastavel ? L"%s liga/desliga · arraste a linha" : L"%s liga/desliga");
            _snwprintf(frase, 128, fmt, m_atalho);
            frase[127] = L'\0';
            UINT hTec = (alturaPedida * 3) / 4; if (hTec < 14) hTec = 14;
            if (dlss5_rotulos::desenhar_etiqueta(frase, hTec, t) && t.valida())
            {
                m_rotTec = dlss5_rotulos::subir(dev, m_fmt, t);
                m_rotTecW = t.w; m_rotTecH = t.h;
            }
        }

        if (!m_rotDisse && log)
        {
            m_rotDisse = true;
            char l[260];
            snprintf(l, sizeof(l),
                     "[DLSS 5 Bridge] comparativo: marcas %s, %ux%u e %ux%u px, margem %u px.",
                     m_rotulos == 1 ? "etiquetas de texto (DLSS 5 / Original)"
                                    : usouReserva ? "de RESERVA (falta algum PNG em marca\\)" : "de marca\\*.png",
                     m_rotEsqW, m_rotEsqH, m_rotDirW, m_rotDirH, margem);
            log(l);
        }
    }

    /* 25/09/2026: as marcas vao COLADAS NA LINHA - "DLSS 5" terminando logo a
       esquerda dela, "Original" comecando logo a direita -, no alto da IMAGEM
       (e nao do back buffer: com letterbox o alto do back buffer e tarja).
       Seguem a linha quando ela e arrastada, e a que nao couber no seu lado
       (linha encostada num canto) simplesmente nao aparece. */
    void rotulos_desenhar(ID3D11DeviceContext *ctx, ID3D11Texture2D *bb, UINT corte,
                          UINT imgX, UINT imgY, UINT imgR, UINT imgB)
    {
        if (m_rotulos == 0) return;
        const UINT meia = kLinhaW / 2 + 1;
        const UINT y = imgY + m_rotMargem;
        if (m_rotEsq && corte >= imgX + m_rotMargem + meia + m_rotEsqW && y + m_rotEsqH <= imgB)
            ctx->CopySubresourceRegion(bb, 0, corte - meia - m_rotMargem - m_rotEsqW, y, 0, m_rotEsq, 0, nullptr);
        if (m_rotDir && corte + meia + m_rotMargem + m_rotDirW + m_rotMargem <= imgR && y + m_rotDirH <= imgB)
            ctx->CopySubresourceRegion(bb, 0, corte + meia + m_rotMargem, y, 0, m_rotDir, 0, nullptr);
        /* A TECLA, CENTRADA NA LINHA, logo abaixo das marcas. E para onde o olho
           vai quando se compara. Se nao couber, simplesmente nao desenha: melhor
           faltar a dica do que tapar a imagem que a pessoa quer avaliar. */
        if (m_rotTec && m_rotTecW > 0 && imgR > imgX && m_rotTecW + 2 * m_rotMargem <= imgR - imgX)
        {
            UINT x = (corte > imgX + m_rotTecW / 2 + m_rotMargem) ? corte - m_rotTecW / 2 : imgX + m_rotMargem;
            if (x + m_rotTecW + m_rotMargem > imgR) x = imgR - m_rotTecW - m_rotMargem;
            const UINT yt = y + (m_rotEsqH > m_rotDirH ? m_rotEsqH : m_rotDirH) + m_rotMargem;
            if (yt + m_rotTecH <= imgB)
                ctx->CopySubresourceRegion(bb, 0, x, yt, 0, m_rotTec, 0, nullptr);
        }
    }

    void rotulos_soltar()
    {
        if (m_rotEsq) { m_rotEsq->Release(); m_rotEsq = nullptr; }
        if (m_rotDir) { m_rotDir->Release(); m_rotDir = nullptr; }
        if (m_rotTec) { m_rotTec->Release(); m_rotTec = nullptr; }
        m_rotEsqW = m_rotEsqH = m_rotDirW = m_rotDirH = 0;
        m_rotTecW = m_rotTecH = 0;
    }

    /* ---------------------------------------------------------------- */
    /* A SONDA DA METADE DIREITA                                         */
    /* ---------------------------------------------------------------- */

    /* Quantos bytes tem um pixel, para percorrer o que o Map devolve. So os
       formatos que um back buffer usa de verdade; 0 = desconheco e a sonda
       desiste em silencio (ela e diagnostico, nao pode custar nada nem
       arriscar ler fora). */
    static UINT bpp(DXGI_FORMAT f)
    {
        switch (f)
        {
        case DXGI_FORMAT_B8G8R8A8_UNORM:      case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:   case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:      case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_UNORM:   case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R11G11B10_FLOAT:
            return 4;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:  case DXGI_FORMAT_R16G16B16A16_UNORM:
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return 8;
        default:
            return 0;
        }
    }

    /* Le dois blocos 8x8 bem afastados dentro de [corte, m_w[ e diz se os 128
       pixels sao todos iguais entre si. -1 = nao deu para sondar.
       O Map e sincrono e custa uma parada de GPU: por isso roda TRES VEZES por
       sessao de comparacao (kSondaEm) e nunca por quadro. */
    int sonda_uniforme(ID3D11Device *dev, ID3D11DeviceContext *ctx,
                       ID3D11Texture2D *src, UINT corte)
    {
        const UINT b = bpp(m_fmt);
        if (b == 0 || src == nullptr || corte >= m_w) return -1;
        if (m_w - corte < kSondaW * 2 || m_h < kSondaH * 4) return -1;

        if (m_sonda == nullptr)
        {
            D3D11_TEXTURE2D_DESC t{};
            t.Width = kSondaW * 2; t.Height = kSondaH;
            t.MipLevels = 1; t.ArraySize = 1;
            t.Format = m_fmt;
            t.SampleDesc.Count = 1;
            t.Usage = D3D11_USAGE_STAGING;
            t.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(dev->CreateTexture2D(&t, nullptr, &m_sonda))) { m_sonda = nullptr; return -1; }
        }

        /* Um bloco no meio da metade direita, outro encostado na borda direita e
           noutra altura: uma tarja acerta os dois, uma imagem quase nunca. */
        const UINT x1 = corte + (m_w - corte) / 2 - kSondaW / 2;
        const UINT y1 = m_h / 2 - kSondaH / 2;
        const UINT x2 = m_w - kSondaW - 1;
        const UINT y2 = m_h / 4;
        D3D11_BOX cx{}; cx.front = 0; cx.back = 1;
        cx.left = x1; cx.right = x1 + kSondaW; cx.top = y1; cx.bottom = y1 + kSondaH;
        ctx->CopySubresourceRegion(m_sonda, 0, 0, 0, 0, src, 0, &cx);
        cx.left = x2; cx.right = x2 + kSondaW; cx.top = y2; cx.bottom = y2 + kSondaH;
        ctx->CopySubresourceRegion(m_sonda, 0, kSondaW, 0, 0, src, 0, &cx);

        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(m_sonda, 0, D3D11_MAP_READ, 0, &m)) || m.pData == nullptr) return -1;
        const unsigned char *base = (const unsigned char *)m.pData;
        const UINT linha = b * kSondaW * 2;
        bool igual = true;
        for (UINT y = 0; y < kSondaH && igual; y++)
        {
            const unsigned char *p = base + (size_t)y * m.RowPitch;
            for (UINT i = 0; i < linha && igual; i++)
                if (p[i] != base[i % b]) igual = false;
        }
        ctx->Unmap(m_sonda, 0);
        return igual ? 1 : 0;
    }

    void soltar()
    {
        for (int i = 0; i < kGuardados; i++)
        {
            if (m_srv[i]) { m_srv[i]->Release(); m_srv[i] = nullptr; }
            if (m_original[i]) { m_original[i]->Release(); m_original[i] = nullptr; }
        }
        if (m_marca) { m_marca->Release(); m_marca = nullptr; }
        if (m_alca) { m_alca->Release(); m_alca = nullptr; }
        if (m_sonda) { m_sonda->Release(); m_sonda = nullptr; }
        rotulos_soltar();
        m_rotVale = false;
        m_w = m_h = 0; m_guardados = 0; m_escreve = 0;
    }

    /* Teto de copias guardadas. O que existe de verdade e nTex() = atraso + 1:
       com o padrao medido (atraso 0) e UMA textura, como sempre foi. */
    enum { kGuardados = 3 };
    int nTex() const { return m_atraso + 1; }

    /* A sonda: dois blocos de 8x8 (128 pixels) e tres leituras por sessao, nos
       quadros abaixo depois de o modo ligar. O primeiro e tardio de proposito:
       ligar o modo costuma coincidir com um quadro de transicao. */
    enum { kSondaW = 8, kSondaH = 8 };
    /* A linha arrastavel (25/09/2026): largura da risca e tamanho da alca. */
    enum { kLinhaW = 4, kAlcaW = 16, kAlcaH = 56 };
    static int sondaEm(int i) { const int q[3] = { 30, 120, 600 }; return (i >= 0 && i < 3) ? q[i] : 0x7fffffff; }

    bool  m_quer = false;
    int   m_rotulos = 1;       /* 0 sem, 1 etiquetas de texto, 2 artes PNG */
    float m_div = 50.f;        /* 0..100, dentro da imagem */
    bool  m_arrastavel = false; /* o app mandou a posicao ao vivo ao menos uma vez */
    bool  m_ingles = false;     /* [idioma] lingua=en */
    int   m_rotPct = 4;        /* altura das marcas, em % da altura da tela */
    int   m_atraso = 0;        /* quantos quadros a esquerda recua (medido: 0) */
    int   m_guardados = 0;     /* quantas copias validas ja existem na roda */
    int   m_escreve = 0;       /* onde o ultimo begin escreveu */
    bool  m_disse = false;
    UINT  m_w = 0, m_h = 0;
    DXGI_FORMAT m_fmt = DXGI_FORMAT_UNKNOWN;
    ID3D11Texture2D *m_original[kGuardados] = { nullptr, nullptr, nullptr };
    ID3D11ShaderResourceView *m_srv[kGuardados] = { nullptr, nullptr, nullptr };   /* X-CONSOLE: p/ ampliar */
    ID3D11Texture2D *m_marca = nullptr;
    ID3D11Texture2D *m_alca = nullptr;   /* o puxador no meio da linha */

    /* A geometria da fonte (X-CONSOLE / I11) e o passe de ampliacao do
       caminho curto. Os shaders vivem alem do soltar() (que e por tamanho):
       so morrem com o objeto. */
    Geometria m_geo;
    ID3D11VertexShader *m_vs = nullptr;
    ID3D11PixelShader  *m_ps = nullptr;
    ID3D11SamplerState *m_samp = nullptr;
    ID3D11Buffer       *m_cb = nullptr;
    HMODULE m_d3dc = nullptr;
    bool    m_shaderFalhou = false;
    ID3D11Device *m_devShaders = nullptr;   /* sem AddRef: so identidade, nunca desreferenciado */

    /* O retangulo do caminho curto (short_w/short_h do dlss5-feed.cfg) e o
       estado do log de motivo. Ver A METADE DIREITA PRETA, em aplicar(). */
    int   m_curtoW = 0, m_curtoH = 0;
    int   m_motivoDito = 0;    /* qual motivo ja foi para o log */
    int   m_quadros = 0;       /* quadros aplicados desde que o modo ligou */
    int   m_sondas = 0;        /* quantas leituras de sonda ja foram feitas */
    ID3D11Texture2D *m_sonda = nullptr;   /* staging 16x8 da sonda */

    wchar_t m_dir[MAX_PATH] = { 0 };
    bool  m_rotVale = false;
    bool  m_rotDisse = false;
    UINT  m_rotMargem = 0;
    UINT  m_rotEsqW = 0, m_rotEsqH = 0, m_rotDirW = 0, m_rotDirH = 0;
    ID3D11Texture2D *m_rotEsq = nullptr;
    ID3D11Texture2D *m_rotDir = nullptr;
    /* A tecla no cabecalho: texto vindo do app por [comparar] atalho. */
    wchar_t m_atalho[64] = { 0 };
    UINT  m_rotTecW = 0, m_rotTecH = 0;
    ID3D11Texture2D *m_rotTec = nullptr;
};

}   // namespace dlss5_comparar
