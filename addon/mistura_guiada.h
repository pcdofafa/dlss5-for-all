/* =====================================================================
   mistura_guiada.h -- reinjeta o detalhe fino do quadro NATIVO na saida da
   rede, por regressao local (fast guided filter em modo joint-upsampling).

   A IDEIA
   -------
   A rede roda numa resolucao de trabalho menor, o que economiza placa e custa
   nitidez. Em vez de aceitar a troca, ajusta-se em BAIXA resolucao um par de
   coeficientes (a, b) sobre o par (nativo reduzido, saida da rede), e aplica-se
   em resolucao CHEIA sobre o nativo:

       a = cov(I,p) / (var(I) + eps)      b = mean_p - a * mean_I
       q = a_barra * I + b_barra

   Baixa frequencia e estabilidade temporal vem da rede; alta frequencia vem do
   nativo. Interpolam-se SO os coeficientes, que sao suaves por construcao -- a
   imagem cheia nunca e reamostrada.

   POR QUE SE ESPERA QUE O SERRILHADO NAO VOLTE, E POR QUE ISSO NAO E FATO
   ----------------------------------------------------------------------
   Numa borda serrilhada var(I) e alta por causa do proprio serrilhado, e
   cov(I,p) e baixa porque a rede o removeu. Entao 'a' cai e a saida degrada
   para 'b', que e a rede. A regressao so transfere o que as duas imagens
   CONCORDAM.

   Isso e derivacao da formula, NAO e medicao, e depende inteiramente de eps.
   O mapa de 'a' nunca foi medido: a varredura foi cancelada em favor de o
   proprio dono olhar a tela. Por isso eps e ajustavel pelo ini e nao constante.

   O QUE NAO ESTA AQUI, DE PROPOSITO
   ---------------------------------
   Nenhuma mitigacao. Sem suavizacao temporal de a,b; sem eps elevado por
   mascara de movimento; sem pos-tratamento. A formula nua, por decisao do
   dono: mitigacao antes da linha de base esconde o tamanho do problema, e
   quando alguem mexer na muleta daqui a um mes o defeito reaparece sem
   ninguem entender de onde veio. As mitigacoes estao na fila, nao descartadas.

   ONDE ELA ENTRA, E A PROVA QUE VEM JUNTO
   ---------------------------------------
   Dois momentos do mesmo quadro, os dois entregues pelo ReShade como ARGUMENTO
   (a unica forma que a ABI do MinGW acerta -- ver o cabecalho de shared_frame.h):

     reshade_begin_effects   -> o back buffer ainda e o NATIVO. Copiamos.
     reshade_finish_effects  -> o back buffer e a saida da cadeia. Misturamos.

   ESTA ORDEM E UMA HIPOTESE, e o codigo NAO confia nela: o RenoDX aplica o
   Neural Rendering por hooks de NGX (EnableHooks=2 no ReShade.ini), nao como
   tecnica na fila do ReShade, e ninguem verificou em que ponto do quadro o
   hook dispara. Se ele disparar ANTES do begin_effects, o que copiamos como
   "nativo" ja e a saida da rede, a mistura vira a identidade e o passe custa
   placa sem fazer nada -- que e exatamente o modo de falha que produz numeros
   bons e nao levanta suspeita.

   Por isso a classe MEDE a diferenca entre os dois quadros e a escreve no log
   (`diff`). diff ~ 0 significa que os dois momentos veem a mesma imagem, e a
   premissa de posicionamento esta errada. A implementacao carrega a coluna que
   pode reprova-la.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <string.h>

namespace dlss5_mistura {

/* Os shaders. Sao os MESMOS que a bancada mediu (0,120 ms por quadro a
   3440x1440 numa RTX 4080, contra 6,34 ms de economia da escada de resolucao);
   nao existe versao "de teste" que depois diverge da de producao. */
static const char *kVS = R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint id : SV_VertexID) {
    VSOut o;
    o.uv  = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)";

/* 1. reduz o par e escreve os quatro momentos de uma vez. Um passe so, quatro
      alvos: reler o par quatro vezes custaria quatro varreduras da resolucao
      cheia, e a resolucao cheia e o unico lugar caro deste pipeline (medido:
      os dois passes cheios sao 0,074 dos 0,120 ms; os cinco de baixa
      resolucao, juntos, 0,046). */
static const char *kPS_REDUZ = R"(
Texture2D<float4> texI : register(t0);
Texture2D<float4> texP : register(t1);
SamplerState sLin : register(s0);
cbuffer CB : register(b0) { float2 v0; float2 v1; float Eps; float p0; float2 p1; };
struct Saida { float4 mI : SV_Target0; float4 mP : SV_Target1; float4 mIP : SV_Target2; float4 mII : SV_Target3; };
Saida main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    float3 sI = 0, sP = 0, sIP = 0, sII = 0;
    [unroll] for (int y = 0; y < 2; y++)
    [unroll] for (int x = 0; x < 2; x++) {
        float2 o = (float2(x, y) * 2.0 - 1.0) * v0;
        float3 I = texI.SampleLevel(sLin, uv + o, 0).rgb;
        float3 P = texP.SampleLevel(sLin, uv + o, 0).rgb;
        sI += I; sP += P; sIP += I * P; sII += I * I;
    }
    Saida o;
    o.mI = float4(sI * 0.25, 0); o.mP  = float4(sP  * 0.25, 0);
    o.mIP = float4(sIP * 0.25, 0); o.mII = float4(sII * 0.25, 0);
    return o;
}
)";

/* 2. box separavel sobre os quatro momentos. O MESMO shader serve as duas
      direcoes; quem decide e o vetor que vem no constant buffer. Dois shaders
      identicos com um sinal trocado e o tipo de duplicacao que depois diverge
      sem ninguem notar. */
static const char *kPS_BOX4 = R"(
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);
SamplerState sLin : register(s0);
cbuffer CB : register(b0) { float2 Passo; float2 p1; float Eps; float p2; float2 p3; };
struct Saida { float4 a : SV_Target0; float4 b : SV_Target1; float4 c : SV_Target2; float4 d : SV_Target3; };
Saida main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    float4 s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    [unroll] for (int i = -2; i <= 2; i++) {
        float2 q = uv + Passo * i;
        s0 += t0.SampleLevel(sLin, q, 0); s1 += t1.SampleLevel(sLin, q, 0);
        s2 += t2.SampleLevel(sLin, q, 0); s3 += t3.SampleLevel(sLin, q, 0);
    }
    const float k = 0.2;
    Saida o; o.a = s0 * k; o.b = s1 * k; o.c = s2 * k; o.d = s3 * k;
    return o;
}
)";

/* 3. a regressao. O unico lugar onde eps entra.

      var e cov saem de subtracao de numeros proximos, que e onde a precisao
      morre. Por isso os alvos sao RGBA16F mas a conta e em float, e var leva
      um max(0): ruido numerico negativo em var viraria 'a' negativo, que e
      ganho invertido, que e uma borda preta em volta de cada detalhe. */
static const char *kPS_SOLVE = R"(
Texture2D<float4> mI : register(t0);
Texture2D<float4> mP : register(t1);
Texture2D<float4> mIP : register(t2);
Texture2D<float4> mII : register(t3);
cbuffer CB : register(b0) { float2 p0; float2 p1; float Eps; float p2; float2 p3; };
struct Saida { float4 A : SV_Target0; float4 B : SV_Target1; };
Saida main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    int3 p = int3(pos.xy, 0);
    float3 uI = mI.Load(p).rgb, uP = mP.Load(p).rgb;
    float3 uIP = mIP.Load(p).rgb, uII = mII.Load(p).rgb;
    float3 cov = uIP - uI * uP;
    float3 var = max(uII - uI * uI, 0.0);
    float3 a = cov / (var + Eps);
    float3 b = uP - a * uI;
    Saida o; o.A = float4(a, 0); o.B = float4(b, 0);
    return o;
}
)";

/* 4. box dos coeficientes. E o que torna a saida continua entre blocos
      vizinhos: sem isto o resultado ganha degraus nas fronteiras do ajuste, e
      degrau em superficie lisa e pior que o serrilhado que se foi remover. */
static const char *kPS_BOX2 = R"(
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
SamplerState sLin : register(s0);
cbuffer CB : register(b0) { float2 Passo; float2 p1; float Eps; float p2; float2 p3; };
struct Saida { float4 a : SV_Target0; float4 b : SV_Target1; };
Saida main(float4 pos : SV_Position, float2 uv : TEXCOORD0) {
    float4 s0 = 0, s1 = 0;
    [unroll] for (int i = -2; i <= 2; i++) {
        float2 q = uv + Passo * i;
        s0 += t0.SampleLevel(sLin, q, 0); s1 += t1.SampleLevel(sLin, q, 0);
    }
    const float k = 0.2;
    Saida o; o.a = s0 * k; o.b = s1 * k;
    return o;
}
)";

/* 5. a aplicacao, o unico passe em resolucao cheia da segunda metade.
      I entra por Load (1:1, sem filtro) e os coeficientes por bilinear -- e
      exatamente o "interpola so os coeficientes" da ideia. */
static const char *kPS_APLICA = R"(
Texture2D<float4> texI : register(t0);
Texture2D<float4> texA : register(t1);
Texture2D<float4> texB : register(t2);
SamplerState sLin : register(s0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 I = texI.Load(int3(pos.xy, 0));
    float3 a = texA.SampleLevel(sLin, uv, 0).rgb;
    float3 b = texB.SampleLevel(sLin, uv, 0).rgb;
    return float4(saturate(I.rgb * a + b), I.a);
}
)";

/* O MARCADOR. Cor fixa, escrita num canto do back buffer no begin_effects.
   Existe para separar duas explicacoes de um `diff` zero que a comparacao de
   imagens sozinha NAO separa:

     marcador SOBREVIVE ate o finish -> ninguem escreveu no back buffer entre os
                                        dois eventos: nenhuma tecnica rodou. O
                                        zero nao diz nada sobre posicionamento.
     marcador SUMIU                  -> alguma coisa escreveu, e ainda assim as
                                        duas copias sairam identicas: o problema
                                        e meu, nao da cena.

   Sem ele, "a cena estava vazia" e "eu copiei a mesma coisa duas vezes"
   produzem exatamente o mesmo numero. */
static const char *kPS_MARCA = R"(
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return float4(1, 0, 1, 1);   // magenta: nao ocorre por acaso numa cena
}
)";

/* A PROVA. Reduz o par a um unico texel acumulando |I - p|, para o log poder
   dizer se os dois momentos do quadro veem imagens diferentes. Roda so quando
   pedido (uma vez a cada N quadros), porque le a resolucao cheia. */
/* Devolve TRES numeros, e os dois ultimos e que decidem onde a rede roda:

     .r  diff       media de |nativo - rede|, a medida DIFERENCIAL
     .g  nitidez do NATIVO   (bb no begin_effects)
     .b  nitidez da REDE     (bb no finish_effects)

   POR QUE A DIFERENCIAL SOZINHA NAO BASTA, e isso foi um furo meu:
   `diff` compara os dois momentos. Ele fica insensivel a work_resolution tanto
   se a rede rodar ANTES do begin (os dois ja tem a rede) quanto se rodar DEPOIS
   do finish (nenhum tem). **As duas hipoteses dao o mesmo numero**, e eu conclui
   a segunda a partir de um dado que nao separa as duas.

   Nitidez e ABSOLUTA e separa os tres casos. Uma imagem reconstruida de 34% dos
   pixels (wr=58) tem menos energia de alta frequencia que DLAA a 100%:

     nitidez do NATIVO muda com wr  -> a rede roda ANTES do begin_effects
     so a da REDE muda com wr       -> a rede roda ENTRE os dois eventos
     nenhuma das duas muda          -> a rede roda DEPOIS do finish_effects
                                       (e o que publicamos para a camada 2 nao
                                        tem o neural)

   Amostragem em grade 8x8 com passo de um texel: gradiente local, nao global. */
static const char *kPS_DIFF = R"(
Texture2D<float4> texI : register(t0);
Texture2D<float4> texP : register(t1);
SamplerState sLin : register(s0);
cbuffer CB : register(b0) { float2 InvFull; float2 v1; float Eps; float p0; float2 p1; };

float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float acc = 0, nI = 0, nP = 0;
    [unroll] for (int y = 0; y < 8; y++)
    [unroll] for (int x = 0; x < 8; x++) {
        float2 q = (float2(x, y) + 0.5) / 8.0;
        float3 I = texI.SampleLevel(sLin, q, 0).rgb;
        float3 P = texP.SampleLevel(sLin, q, 0).rgb;
        acc += dot(abs(I - P), 1.0.xxx) / 3.0;

        // gradiente de um texel em cada eixo: energia de alta frequencia, que e
        // o que a reconstrucao a partir de menos pixels perde
        float2 dx = float2(InvFull.x, 0), dy = float2(0, InvFull.y);
        float lI = luma(I), lP = luma(P);
        nI += abs(luma(texI.SampleLevel(sLin, q + dx, 0).rgb) - lI)
            + abs(luma(texI.SampleLevel(sLin, q + dy, 0).rgb) - lI);
        nP += abs(luma(texP.SampleLevel(sLin, q + dx, 0).rgb) - lP)
            + abs(luma(texP.SampleLevel(sLin, q + dy, 0).rgb) - lP);
    }
    return float4(acc / 64.0, nI / 64.0, nP / 64.0, 0);
}
)";

struct CB { float v0[2]; float v1[2]; float eps; float pad; float pad2[2]; };

typedef HRESULT(WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const void *, void *,
                                        LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

class Mistura
{
public:
    /* `wr` e a resolucao de trabalho da rede em % LINEAR (a area cai pelo
       quadrado). So serve para a linha de log dizer contra o que a economia
       esta sendo contada; nao entra em conta nenhuma do passe. */
    void configurar(bool ligada, float eps, int wr)
    {
        m_quer = ligada;
        m_eps = (eps > 0.0f) ? eps : 1e-4f;
        m_wr = (wr >= 25 && wr <= 100) ? wr : 100;
    }
    bool ligada() const { return m_quer && m_ok; }

    /* reshade_begin_effects: o back buffer ainda e o nativo. */
    void capturar_nativo(void *rtvHandle, void (*log)(const char *))
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
                ctx->CopyResource(m_nativo, bb);
                /* Guarda o recurso que o begin entregou, para o log poder dizer
                   se o finish entrega o MESMO. Se forem recursos diferentes, as
                   duas copias sao de buffers diferentes do swapchain e a
                   comparacao nunca foi entre dois momentos do mesmo quadro. */
                m_resBegin = (void *)bb;
                if (m_marcarAgora) { marcar(ctx, rtvHandle); m_marcarAgora = false; m_marcado = true; }
                ctx->Release();
            }
            m_temNativo = true;
        }
        bb->Release(); dev->Release();
    }

    /* reshade_finish_effects: o back buffer e a saida da cadeia. Mistura e
       escreve de volta. */
    void aplicar(void *rtvHandle, void (*log)(const char *))
    {
        if (!m_quer || !m_temNativo || rtvHandle == nullptr) return;
        ID3D11Texture2D *bb = nullptr; ID3D11Device *dev = nullptr;
        if (!abrir(rtvHandle, &bb, &dev)) return;
        D3D11_TEXTURE2D_DESC d{}; bb->GetDesc(&d);
        if (!preparar(dev, d, log)) { bb->Release(); dev->Release(); return; }

        ID3D11DeviceContext *ctx = nullptr; dev->GetImmediateContext(&ctx);
        if (ctx == nullptr) { bb->Release(); dev->Release(); return; }

        /* A saida da rede vai para um temporario: nao da para ler e escrever o
           mesmo recurso no mesmo passe, e o D3D desligaria a leitura EM
           SILENCIO -- o passe rodaria lendo preto e a imagem sairia errada sem
           erro nenhum. */
        ctx->CopyResource(m_rede, bb);
        m_resFinish = (void *)bb;

        /* O marcador: le o canto do que o finish entregou, ANTES de misturar.
           Magenta ainda la = ninguem escreveu no back buffer desde o begin. */
        if (m_marcado)
        {
            m_marcado = false;
            const UINT cxp = m_w / 2, cyp = m_h / 2;   /* o mesmo centro do marcador */
            D3D11_BOX cx{ cxp, cyp, 0, cxp + 1, cyp + 1, 1 };
            ctx->CopySubresourceRegion(m_pixCpu, 0, 0, 0, 0, m_rede, 0, &cx);
            D3D11_MAPPED_SUBRESOURCE mp{};
            if (SUCCEEDED(ctx->Map(m_pixCpu, 0, D3D11_MAP_READ, 0, &mp)))
            {
                const unsigned char *px = reinterpret_cast<const unsigned char *>(mp.pData);
                /* B8G8R8A8: magenta = B alto, G baixo, R alto */
                m_marcaSobreviveu = (px[0] > 200 && px[1] < 60 && px[2] > 200);
                ctx->Unmap(m_pixCpu, 0);
            }
        }

        ID3D11RenderTargetView *rtvBB = reinterpret_cast<ID3D11RenderTargetView *>(rtvHandle);
        passes(ctx, rtvBB);

        /* A prova de posicionamento, a cada 120 quadros: custa uma varredura da
           resolucao cheia (uma vez a cada dois segundos, nao por quadro) e
           responde se os dois momentos do quadro veem imagens diferentes.
           Comecou em 240 e nao aparecia nenhuma vez numa sessao curta de
           teste -- uma prova que so sai depois de quatro segundos de imagem
           continua nao serve para quem esta ligando e desligando na tela. */
        ++m_conta;
        /* No quadro ANTERIOR ao da medicao pede-se o marcador, para que ele
           seja escrito no begin do quadro seguinte e lido no finish do mesmo. */
        if (m_conta % 120 == 119) m_marcarAgora = true;
        if (m_conta % 120 == 0)
        {
            /* A SAIDA JA MISTURADA, copiada depois do passe 7. E a unica prova
               de que a mistura teve EFEITO: sem ela, um passe que rodasse
               perfeitamente e devolvesse a identidade daria exatamente o mesmo
               log -- roda, nao da erro, e nao faz nada. */
            ctx->CopyResource(m_saida, bb);
            medir_diff(ctx, log);
        }

        ctx->Release(); bb->Release(); dev->Release();
    }

    void desligar()
    {
        soltar();
        m_ok = false; m_temNativo = false;
    }

private:
    static bool abrir(void *rtvHandle, ID3D11Texture2D **tex, ID3D11Device **dev)
    {
        /* Em D3D, handle DE resource_view E o ponteiro da ID3D11View -- o mesmo
           caminho que o shared_frame.h usa e pela mesma razao de ABI. */
        ID3D11RenderTargetView *rtv = reinterpret_cast<ID3D11RenderTargetView *>(rtvHandle);
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
        if (m_ok && d.Width == m_w && d.Height == m_h && d.Format == m_fmt) return true;
        soltar();
        m_w = d.Width; m_h = d.Height; m_fmt = d.Format;
        m_lw = (m_w + 3) / 4; m_lh = (m_h + 3) / 4;   /* s = 4 */
        if (m_lw == 0 || m_lh == 0) return false;

        if (!compilar(dev, log)) return false;

        auto nova = [&](UINT w, UINT h, DXGI_FORMAT f, UINT bind, ID3D11Texture2D **t,
                        ID3D11ShaderResourceView **srv, ID3D11RenderTargetView **rtv) -> bool {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = f;
            td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = bind;
            if (FAILED(dev->CreateTexture2D(&td, nullptr, t))) return false;
            if (srv && (bind & D3D11_BIND_SHADER_RESOURCE) &&
                FAILED(dev->CreateShaderResourceView(*t, nullptr, srv))) return false;
            if (rtv && (bind & D3D11_BIND_RENDER_TARGET) &&
                FAILED(dev->CreateRenderTargetView(*t, nullptr, rtv))) return false;
            return true;
        };
        const UINT SR = D3D11_BIND_SHADER_RESOURCE, RT = D3D11_BIND_RENDER_TARGET;
        const DXGI_FORMAT F16 = DXGI_FORMAT_R16G16B16A16_FLOAT;
        bool ok = nova(m_w, m_h, m_fmt, SR, &m_nativo, &m_srvNativo, nullptr)
               && nova(m_w, m_h, m_fmt, SR, &m_rede,   &m_srvRede,   nullptr)
               && nova(m_w, m_h, m_fmt, SR, &m_saida,  &m_srvSaida,  nullptr);
        for (int j = 0; j < 2 && ok; j++)
            for (int i = 0; i < 4 && ok; i++)
                ok = nova(m_lw, m_lh, F16, SR | RT, &m_mom[j][i], &m_srvMom[j][i], &m_rtvMom[j][i]);
        for (int j = 0; j < 2 && ok; j++)
            for (int i = 0; i < 2 && ok; i++)
                ok = nova(m_lw, m_lh, F16, SR | RT, &m_coef[j][i], &m_srvCoef[j][i], &m_rtvCoef[j][i]);
        ok = ok && nova(1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, RT, &m_diff, nullptr, &m_rtvDiff);
        if (ok)
        {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = 1; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ok = SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &m_diffCpu));
            /* staging 1x1 no formato do back buffer, para ler o marcador */
            td.Format = m_fmt;
            ok = ok && SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &m_pixCpu));
        }
        if (ok)
        {
            D3D11_SAMPLER_DESC sd{};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            ok = SUCCEEDED(dev->CreateSamplerState(&sd, &m_samp));
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = sizeof(CB); bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            ok = ok && SUCCEEDED(dev->CreateBuffer(&bd, nullptr, &m_cb));
        }
        if (!ok) { soltar(); if (log) log("[DLSS 5 Bridge] mistura: recursos falharam - desligando"); return false; }

        if (log)
        {
            char m[192];
            _snprintf(m, sizeof(m), "[DLSS 5 Bridge] mistura pronta: %ux%u, ajuste %ux%u (s=4), eps=%.2e",
                      m_w, m_h, m_lw, m_lh, (double)m_eps);
            m[sizeof(m) - 1] = '\0'; log(m);
        }
        m_ok = true;
        return true;
    }

    bool compilar(ID3D11Device *dev, void (*log)(const char *))
    {
        if (m_vs != nullptr) return true;
        /* d3dcompiler por LoadLibrary em vez de linkagem: o build do add-on
           linka so kernel32, e mexer nele para um recurso desligado por padrao
           seria arriscar o binario inteiro por uma coisa opcional. */
        if (m_d3dc == nullptr)
        {
            m_d3dc = LoadLibraryA("d3dcompiler_47.dll");
            if (m_d3dc == nullptr) m_d3dc = LoadLibraryA("d3dcompiler_46.dll");
            if (m_d3dc == nullptr) { if (log) log("[DLSS 5 Bridge] mistura: d3dcompiler nao encontrado - desligando"); return false; }
        }
        PFN_D3DCompile compile = (PFN_D3DCompile)GetProcAddress(m_d3dc, "D3DCompile");
        if (compile == nullptr) { if (log) log("[DLSS 5 Bridge] mistura: D3DCompile ausente - desligando"); return false; }

        auto faz = [&](const char *src, const char *alvo, ID3DBlob **bc) -> bool {
            ID3DBlob *err = nullptr;
            HRESULT hr = compile(src, strlen(src), nullptr, nullptr, nullptr, "main", alvo, 0, 0, bc, &err);
            if (FAILED(hr))
            {
                if (log)
                {
                    char m[512];
                    _snprintf(m, sizeof(m), "[DLSS 5 Bridge] mistura: shader falhou: %s",
                              err ? (const char *)err->GetBufferPointer() : "sem detalhe");
                    m[sizeof(m) - 1] = '\0'; log(m);
                }
                if (err) err->Release();
                return false;
            }
            if (err) err->Release();
            return true;
        };
        ID3DBlob *b = nullptr;
        if (!faz(kVS, "vs_5_0", &b)) return false;
        bool ok = SUCCEEDED(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &m_vs));
        b->Release();
        struct { const char *src; ID3D11PixelShader **dst; } lista[] = {
            { kPS_REDUZ, &m_psReduz }, { kPS_BOX4, &m_psBox4 }, { kPS_SOLVE, &m_psSolve },
            { kPS_BOX2, &m_psBox2 }, { kPS_APLICA, &m_psAplica }, { kPS_DIFF, &m_psDiff },
            { kPS_MARCA, &m_psMarca },
        };
        for (auto &e : lista)
        {
            if (!ok) break;
            if (!faz(e.src, "ps_5_0", &b)) { ok = false; break; }
            ok = SUCCEEDED(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, e.dst));
            b->Release();
        }
        if (!ok && log) log("[DLSS 5 Bridge] mistura: criacao de shader falhou - desligando");
        return ok;
    }

    void poeCB(ID3D11DeviceContext *ctx, float a0, float a1, float b0, float b1)
    {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        CB c{}; c.v0[0] = a0; c.v0[1] = a1; c.v1[0] = b0; c.v1[1] = b1; c.eps = m_eps;
        memcpy(m.pData, &c, sizeof(c));
        ctx->Unmap(m_cb, 0);
    }

    void passes(ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtvBB)
    {
        /* Estado salvo e devolvido: o ReShade continua o quadro depois de nos,
           e devolver o contexto sujo e a forma de quebrar o produto inteiro
           por um recurso que estava desligado ontem. */
        Guarda g(ctx);

        ID3D11ShaderResourceView *nulo[4] = { nullptr, nullptr, nullptr, nullptr };
        ID3D11RenderTargetView *rtNulo[4] = { nullptr, nullptr, nullptr, nullptr };
        D3D11_VIEWPORT vpLow{ 0, 0, (float)m_lw, (float)m_lh, 0, 1 };
        D3D11_VIEWPORT vpFull{ 0, 0, (float)m_w, (float)m_h, 0, 1 };

        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_vs, nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->PSSetSamplers(0, 1, &m_samp);
        ctx->PSSetConstantBuffers(0, 1, &m_cb);
        ctx->RSSetState(nullptr);
        ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(nullptr, 0);

        /* 1. momentos */
        ctx->RSSetViewports(1, &vpLow);
        ctx->PSSetShader(m_psReduz, nullptr, 0);
        poeCB(ctx, 1.0f / m_w, 1.0f / m_h, 1.0f / m_lw, 1.0f / m_lh);
        { ID3D11ShaderResourceView *s[2] = { m_srvNativo, m_srvRede }; ctx->PSSetShaderResources(0, 2, s); }
        ctx->OMSetRenderTargets(4, m_rtvMom[0], nullptr);
        ctx->Draw(3, 0);
        ctx->OMSetRenderTargets(4, rtNulo, nullptr); ctx->PSSetShaderResources(0, 4, nulo);

        /* 2-3. box separavel dos momentos */
        ctx->PSSetShader(m_psBox4, nullptr, 0);
        poeCB(ctx, 1.0f / m_lw, 0.0f, 0.0f, 0.0f);
        ctx->PSSetShaderResources(0, 4, m_srvMom[0]);
        ctx->OMSetRenderTargets(4, m_rtvMom[1], nullptr);
        ctx->Draw(3, 0);
        ctx->OMSetRenderTargets(4, rtNulo, nullptr); ctx->PSSetShaderResources(0, 4, nulo);

        poeCB(ctx, 0.0f, 1.0f / m_lh, 0.0f, 0.0f);
        ctx->PSSetShaderResources(0, 4, m_srvMom[1]);
        ctx->OMSetRenderTargets(4, m_rtvMom[0], nullptr);
        ctx->Draw(3, 0);
        ctx->OMSetRenderTargets(4, rtNulo, nullptr); ctx->PSSetShaderResources(0, 4, nulo);

        /* 4. regressao */
        ctx->PSSetShader(m_psSolve, nullptr, 0);
        poeCB(ctx, 0, 0, 0, 0);
        ctx->PSSetShaderResources(0, 4, m_srvMom[0]);
        ctx->OMSetRenderTargets(2, m_rtvCoef[0], nullptr);
        ctx->Draw(3, 0);
        ctx->OMSetRenderTargets(4, rtNulo, nullptr); ctx->PSSetShaderResources(0, 4, nulo);

        /* 5-6. box dos coeficientes */
        ctx->PSSetShader(m_psBox2, nullptr, 0);
        poeCB(ctx, 1.0f / m_lw, 0.0f, 0.0f, 0.0f);
        ctx->PSSetShaderResources(0, 2, m_srvCoef[0]);
        ctx->OMSetRenderTargets(2, m_rtvCoef[1], nullptr);
        ctx->Draw(3, 0);
        ctx->OMSetRenderTargets(4, rtNulo, nullptr); ctx->PSSetShaderResources(0, 4, nulo);

        poeCB(ctx, 0.0f, 1.0f / m_lh, 0.0f, 0.0f);
        ctx->PSSetShaderResources(0, 2, m_srvCoef[1]);
        ctx->OMSetRenderTargets(2, m_rtvCoef[0], nullptr);
        ctx->Draw(3, 0);
        ctx->OMSetRenderTargets(4, rtNulo, nullptr); ctx->PSSetShaderResources(0, 4, nulo);

        /* 7. aplica sobre o NATIVO, escrevendo no back buffer */
        ctx->RSSetViewports(1, &vpFull);
        ctx->PSSetShader(m_psAplica, nullptr, 0);
        { ID3D11ShaderResourceView *s[3] = { m_srvNativo, m_srvCoef[0][0], m_srvCoef[0][1] };
          ctx->PSSetShaderResources(0, 3, s); }
        ctx->OMSetRenderTargets(1, &rtvBB, nullptr);
        ctx->Draw(3, 0);
        ctx->OMSetRenderTargets(4, rtNulo, nullptr); ctx->PSSetShaderResources(0, 4, nulo);
    }

    void medir_diff(ID3D11DeviceContext *ctx, void (*log)(const char *))
    {
        ID3D11ShaderResourceView *nulo[4] = { nullptr, nullptr, nullptr, nullptr };
        ID3D11RenderTargetView *rtNulo[4] = { nullptr, nullptr, nullptr, nullptr };
        D3D11_VIEWPORT vp1{ 0, 0, 1, 1, 0, 1 };
        ctx->RSSetViewports(1, &vp1);
        ctx->PSSetShader(m_psDiff, nullptr, 0);
        /* o passo de um texel, para o gradiente. Sem isto o shader herdaria o
           vetor do ultimo box, que esta em unidades de BAIXA resolucao, e a
           "nitidez" mediria um passo quatro vezes maior sem avisar. */
        poeCB(ctx, 1.0f / m_w, 1.0f / m_h, 0.0f, 0.0f);
        /* Roda o shader 1x1 sobre um par e devolve (diff, nitidez de A,
           nitidez de B). Chamado duas vezes: uma para o par que entra na
           mistura, outra para o que sai dela. */
        auto amostra = [&](ID3D11ShaderResourceView *a, ID3D11ShaderResourceView *b, float out[3]) {
            ID3D11ShaderResourceView *s[2] = { a, b };
            ctx->PSSetShaderResources(0, 2, s);
            ctx->OMSetRenderTargets(1, &m_rtvDiff, nullptr);
            ctx->Draw(3, 0);
            ctx->OMSetRenderTargets(4, rtNulo, nullptr); ctx->PSSetShaderResources(0, 4, nulo);
            ctx->CopyResource(m_diffCpu, m_diff);
            D3D11_MAPPED_SUBRESOURCE mm{};
            out[0] = out[1] = out[2] = 0.0f;
            if (SUCCEEDED(ctx->Map(m_diffCpu, 0, D3D11_MAP_READ, 0, &mm)))
            {
                const float *p = reinterpret_cast<const float *>(mm.pData);
                out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
                ctx->Unmap(m_diffCpu, 0);
            }
        };

        float ent[3], sai[3];
        amostra(m_srvNativo, m_srvRede,  ent);   /* nativo vs rede: o que ENTRA */
        amostra(m_srvNativo, m_srvSaida, sai);   /* nativo vs saida: o que SAI  */

        {
            const float d = ent[0], nitI = ent[1], nitP = ent[2];
            if (log)
            {
                /* O GANHO AO LADO DA IMAGEM. Quem esta olhando a tela precisa
                   do numero na mesma linha, nao numa planilha.

                   O custo da rede vem do modelo medido nesta bancada
                   (0,142 + 0,193 x Mpx, em pontos percentuais de GPU por
                   quadro; x10 = ms), aplicado a area de trabalho REAL. E
                   MODELO, nao medida deste quadro, e a linha diz isso: dar-lhe
                   cara de medicao seria o erro que esta bancada ja catalogou.

                   O custo do PASSE e o medido na bancada da Porta 1 para esta
                   area (0,120 ms a 4,95 Mpx, escalando com a area), e nao um
                   cronometro vivo -- medir por quadro exigiria timestamp query
                   aqui dentro, que e trabalho que ninguem pediu para uma linha
                   de log. Tambem esta marcado. */
                const double mpCheio = (double)m_w * (double)m_h / 1e6;
                const double f = (double)m_wr / 100.0;
                const double msCheio = (0.142 + 0.193 * mpCheio) * 10.0;
                const double msRede  = (0.142 + 0.193 * mpCheio * f * f) * 10.0;
                const double msPasse = 0.120 * (mpCheio / 4.95);
                const double liquido = (msCheio - msRede) - msPasse;

                char s[420];
                /* diff ~ 0 quer dizer que os dois momentos do quadro veem a
                   MESMA imagem: a rede nao roda entre eles e a mistura esta
                   fazendo nada por dinheiro. E o unico jeito de esse modo de
                   falha aparecer, porque ele nao gera erro. */
                _snprintf(s, sizeof(s),
                          "[DLSS 5 Bridge] mistura: rede a %d%% (%ux%u) | eps=%.2e | "
                          "passe ~%.3f ms, economia da escada ~%.2f ms, liquido ~%+.2f ms/quadro "
                          "(modelo, nao medida deste quadro) | diff nativo-vs-rede=%.3e | %s",
                          m_wr, (unsigned)(m_w * f), (unsigned)(m_h * f), (double)m_eps,
                          msPasse, msCheio - msRede, liquido, (double)d,
                          /* NOTACAO CIENTIFICA, e a primeira versao usava %.5f.
                             Com cinco casas, qualquer diferenca abaixo de 1e-5
                             imprime "0.00000", e eu li isso como zero exato e
                             quase conclui que o passe estava no lugar errado. A
                             precisao da IMPRESSAO virou a conclusao da medida.

                             O limiar tambem baixou de 5e-4 para 1e-6: 5e-4 e
                             maior que a mudanca que SMAA e nitidez produzem na
                             media absoluta de uma imagem simples, entao ele
                             reprovaria um posicionamento correto. O que se quer
                             detectar e imagem IDENTICA, e identica quer dizer
                             zero de verdade, nao "pouco". */
                          (d < 1e-6f) ? "SUSPEITO: os dois momentos veem a mesma imagem"
                                      : "ok: a cadeia mudou a imagem entre os dois momentos");
                s[sizeof(s) - 1] = '\0'; log(s);

                /* Os DOIS testes que separam as explicacoes de um diff zero, em
                   numeros e nao em conclusao: o recurso entregue nos dois
                   eventos, e o destino do marcador. */
                char t[300];
                _snprintf(t, sizeof(t),
                          "[DLSS 5 Bridge] mistura/diagnostico: recurso begin=%p finish=%p (%s) | "
                          "marcador magenta no begin %s no finish -> %s",
                          m_resBegin, m_resFinish,
                          (m_resBegin == m_resFinish) ? "MESMO recurso" : "recursos DIFERENTES",
                          m_marcaSobreviveu ? "SOBREVIVEU" : "sumiu",
                          m_marcaSobreviveu
                              ? "NINGUEM escreveu no back buffer entre os dois eventos: nenhuma tecnica rodou, o diff nao julga posicionamento"
                              : "alguem escreveu entre os dois eventos: a cena nao estava vazia");
                t[sizeof(t) - 1] = '\0'; log(t);

                /* AS DUAS NITIDEZES ABSOLUTAS. Sao elas que dizem ONDE a rede
                   roda, coisa que o `diff` diferencial nao separa: ele fica
                   igual tanto se a rede rodar antes do begin quanto depois do
                   finish. Compare estes dois numeros entre work_resolution 100
                   e 58 -- quem se mover denuncia o lado em que a rede esta. */
                char u[240];
                _snprintf(u, sizeof(u),
                          "[DLSS 5 Bridge] mistura/nitidez: wr=%d | nativo(begin)=%.5e | rede(finish)=%.5e | "
                          "razao=%.3f",
                          m_wr, (double)nitI, (double)nitP,
                          (nitI > 0.0f) ? (double)(nitP / nitI) : 0.0);
                u[sizeof(u) - 1] = '\0'; log(u);

                /* A PROVA DE EFEITO DA MISTURA, e ela e a que faltava.
                   Compara a nitidez que ENTROU (a rede) com a que SAIU (depois
                   do passe 7), as duas contra o nativo:

                     recuperado = (nit_saida - nit_rede) / (nit_nativo - nit_rede)

                   0%   = a mistura devolveu a rede intacta: roda, nao da erro,
                          e nao faz nada -- o modo de falha que nenhum outro log
                          desta classe pegaria.
                   100% = a saida tem tanta alta frequencia quanto o nativo.

                   Nao diz se a IMAGEM ficou boa: alta frequencia recuperada
                   pode ser detalhe ou pode ser serrilhado de volta. Quem julga
                   isso e o olho, e e por isso que este numero fica ao lado da
                   tela em vez de virar criterio. */
                const float nitS = sai[2];
                const float den = nitI - nitP;
                char w[260];
                _snprintf(w, sizeof(w),
                          "[DLSS 5 Bridge] mistura/efeito: rede=%.5e -> saida=%.5e (nativo=%.5e) | "
                          "recuperado=%.1f%% | %s",
                          (double)nitP, (double)nitS, (double)nitI,
                          (den > 1e-9f) ? 100.0 * (double)((nitS - nitP) / den) : 0.0,
                          (nitS <= nitP * 1.02f)
                              ? "SUSPEITO: a saida nao ganhou detalhe - a mistura pode estar devolvendo a identidade"
                              : "ok: a saida tem mais detalhe que a rede sozinha");
                w[sizeof(w) - 1] = '\0'; log(w);
            }
        }
    }

    /* Pinta 8x8 magenta no canto do back buffer. So no quadro de medicao, uma
       vez a cada 120 -- e visivel, mas oito pixels durante um quadro a cada dois
       segundos e o preco de saber onde o passe esta. */
    void marcar(ID3D11DeviceContext *ctx, void *rtvHandle)
    {
        if (m_psMarca == nullptr) return;
        Guarda g(ctx);
        ID3D11RenderTargetView *rtv = reinterpret_cast<ID3D11RenderTargetView *>(rtvHandle);
        /* NO CENTRO, e a primeira versao punha no canto (0,0). O canto mentiu:
           a fonte tem proporcao diferente da janela, entao ali e borda, e borda
           nenhum efeito escreve. O marcador sobrevivia e eu concluia "nenhuma
           tecnica rodou" enquanto o diff dizia o contrario -- duas provas
           discordando porque uma media um lugar onde nada acontece.
           O centro esta dentro da imagem em qualquer proporcao. */
        D3D11_VIEWPORT vp{ (float)(m_w / 2 - 4), (float)(m_h / 2 - 4), 8, 8, 0, 1 };
        ctx->RSSetViewports(1, &vp);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_vs, nullptr, 0);
        ctx->PSSetShader(m_psMarca, nullptr, 0);
        ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(nullptr, 0);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->Draw(3, 0);
        ID3D11RenderTargetView *rn[1] = { nullptr };
        ctx->OMSetRenderTargets(1, rn, nullptr);
    }

    /* Salva e devolve o pouco de estado do contexto que mexemos. */
    struct Guarda
    {
        explicit Guarda(ID3D11DeviceContext *c) : ctx(c)
        {
            ctx->RSGetViewports(&nvp, vp);
            ctx->OMGetRenderTargets(1, &rtv, &dsv);
        }
        ~Guarda()
        {
            if (nvp) ctx->RSSetViewports(nvp, vp);
            ctx->OMSetRenderTargets(1, &rtv, dsv);
            if (rtv) rtv->Release();
            if (dsv) dsv->Release();
        }
        ID3D11DeviceContext *ctx;
        UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
        ID3D11RenderTargetView *rtv = nullptr;
        ID3D11DepthStencilView *dsv = nullptr;
    };

    template <class T> static void solta(T *&p) { if (p) { p->Release(); p = nullptr; } }

    void soltar()
    {
        solta(m_nativo); solta(m_srvNativo); solta(m_rede); solta(m_srvRede); solta(m_saida); solta(m_srvSaida);
        for (int j = 0; j < 2; j++) for (int i = 0; i < 4; i++)
            { solta(m_mom[j][i]); solta(m_srvMom[j][i]); solta(m_rtvMom[j][i]); }
        for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++)
            { solta(m_coef[j][i]); solta(m_srvCoef[j][i]); solta(m_rtvCoef[j][i]); }
        solta(m_diff); solta(m_rtvDiff); solta(m_diffCpu); solta(m_pixCpu);
        solta(m_psMarca);
        solta(m_samp); solta(m_cb);
        solta(m_vs); solta(m_psReduz); solta(m_psBox4); solta(m_psSolve);
        solta(m_psBox2); solta(m_psAplica); solta(m_psDiff);
        m_ok = false; m_temNativo = false;
    }

    bool m_quer = false, m_ok = false, m_temNativo = false;
    bool m_marcarAgora = false, m_marcado = false, m_marcaSobreviveu = false;
    void *m_resBegin = nullptr, *m_resFinish = nullptr;
    float m_eps = 1e-4f;
    int m_wr = 100;
    UINT m_w = 0, m_h = 0, m_lw = 0, m_lh = 0;
    DXGI_FORMAT m_fmt = DXGI_FORMAT_UNKNOWN;
    unsigned long long m_conta = 0;
    HMODULE m_d3dc = nullptr;

    ID3D11Texture2D *m_saida = nullptr;
    ID3D11ShaderResourceView *m_srvSaida = nullptr;
    ID3D11Texture2D *m_nativo = nullptr, *m_rede = nullptr, *m_diff = nullptr, *m_diffCpu = nullptr;
    ID3D11ShaderResourceView *m_srvNativo = nullptr, *m_srvRede = nullptr;
    ID3D11RenderTargetView *m_rtvDiff = nullptr;
    ID3D11Texture2D *m_mom[2][4] = {};
    ID3D11ShaderResourceView *m_srvMom[2][4] = {};
    ID3D11RenderTargetView *m_rtvMom[2][4] = {};
    ID3D11Texture2D *m_coef[2][2] = {};
    ID3D11ShaderResourceView *m_srvCoef[2][2] = {};
    ID3D11RenderTargetView *m_rtvCoef[2][2] = {};
    ID3D11SamplerState *m_samp = nullptr;
    ID3D11Buffer *m_cb = nullptr;
    ID3D11VertexShader *m_vs = nullptr;
    ID3D11PixelShader *m_psReduz = nullptr, *m_psBox4 = nullptr, *m_psSolve = nullptr;
    ID3D11PixelShader *m_psBox2 = nullptr, *m_psAplica = nullptr, *m_psDiff = nullptr;
    ID3D11PixelShader *m_psMarca = nullptr;
    ID3D11Texture2D *m_pixCpu = nullptr;   /* 1x1 staging, formato do back buffer */
};

} // namespace dlss5_mistura
