/*
    ponteiro_desenhado.h -- a SETA do modo Ponteiro, desenhada DEPOIS da rede.

    Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).

    ------------------------------------------------------------------------
    POR QUE ISTO EXISTE, E POR QUE NAO PODIA FICAR NO APP
    ------------------------------------------------------------------------
    A primeira versao desenhava a seta no pixel shader do app (main.cpp, no
    ScaleCB). Funcionava no papel e NAO funcionava na tela, e o motivo e a
    ordem das coisas:

        1. o app desenha a captura 1:1 no CANTO do backbuffer (caminho curto)
        2. a rede le esse retangulo e REESCREVE O BACKBUFFER INTEIRO em 4K
        3. so entao o quadro vai para a tela

    Ou seja: tudo que o shader do app desenha ou some no passo 2, ou vira
    ENTRADA da rede -- a seta era reconstruida junto com a imagem, saindo
    borrada e fora de lugar. O relato do usuario foi exatamente "o ponteiro
    n aparece direito".

    Aqui e o unico lugar certo: reshade_finish_effects, depois da rede e do
    comparativo, antes de publicar. E a mesma razao pela qual o comparativo
    mora no add-on e nao no app.

    ------------------------------------------------------------------------
    COMO A POSICAO CHEGA ATE AQUI
    ------------------------------------------------------------------------
    O add-on roda DENTRO do processo do app, mas nao ve as variaveis dele.
    A posicao muda a cada quadro (e a mao da pessoa), entao ler de arquivo
    ini -- como o comparativo faz com as chaves dele -- esta fora de questao:
    aquilo atualiza uma vez por segundo.

    A ponte e uma secao de memoria compartilhada nomeada, escrita pelo app a
    cada quadro e lida aqui. Quatro valores, sem trava: escrita e leitura de
    campos alinhados de 4 bytes sao atomicas na pratica em x86, e o pior caso
    de uma leitura rasgada e UM quadro com a seta um pixel fora do lugar --
    invisivel, e barato demais para justificar um mutex no caminho do quadro.

    ------------------------------------------------------------------------
    A ARTE
    ------------------------------------------------------------------------
    Gerada em codigo, nao carregada de PNG, por dois motivos: a seta precisa
    acompanhar a ampliacao (numa pelicula 4K vinda de 720p, um cursor de
    tamanho normal vira um ponto), e um PNG a mais seria mais um arquivo para
    faltar na pasta de quem instalou errado. O desenho e a seta classica:
    triangulo branco com contorno preto, para nao sumir sobre fundo claro.

    ------------------------------------------------------------------------
    O QUADRADO PRETO (14/09, ~12:05) E O QUE MUDOU
    ------------------------------------------------------------------------
    A versao anterior era um CopySubresourceRegion da textura para o back
    buffer. Duas coisas erradas, e as duas davam "quadrado preto":

      1. copia nao tem alpha: o que na textura era "transparente" (0,0,0,0)
         chegava a tela como preto opaco. Toda a caixa 48x66 ficava pintada.
      2. os meia-planos do triangulo estavam com o sinal trocado em duas das
         tres arestas (o triangulo e horario em coordenadas com y para baixo,
         e a conta assumia anti-horario). Nenhum pixel satisfazia "dentro",
         entao nem o branco aparecia -- so preto, por toda a caixa.

    Agora a seta e DESENHADA, nao copiada: um triangulo de tela inteira com o
    viewport reduzido a caixa da seta, um pixel shader que amostra a textura,
    e blend SrcAlpha/InvSrcAlpha. Os shaders sao dois de uma linha, compilados
    em tempo de execucao pelo d3dcompiler_47.dll (que vem com o Windows 10/11
    e ja e o caminho que a mistura guiada usa). Fora da seta o alpha e zero e
    o jogo aparece; a borda tem 1 px de suavizacao para nao serrilhar.

    A mascara e gerada por uma funcao livre (pintar_seta) sem D3D nenhum,
    justamente para dar para testa-la fora do jogo: um programa de 40 linhas
    inclui este header, chama a funcao e grava um PNG.
*/
#pragma once

#include <d3d11.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <new>

namespace dlss5_ponteiro
{

/* O MESMO LAYOUT ESCRITO PELO APP (main.cpp, PonteiroCompartilhado). Se um
   lado mudar, o outro tem de mudar junto -- por isso o campo `versao`: um
   app velho com um add-on novo (ou o contrario) simplesmente nao desenha,
   em vez de desenhar lixo em cima do jogo. */
struct Estado
{
    unsigned versao;   /* 1 */
    unsigned ligado;   /* 0 = modo camera: nao desenha nada */
    float    x, y;     /* ponta da seta, em pixeis do backbuffer */
    float    tamanho;  /* altura da seta em pixeis */
};

static const wchar_t *kNome = L"Local\\DLSS5ForAll_Ponteiro_v1";

/* MARGEM da textura em volta do triangulo, em pixeis: cabe o contorno (2 px)
   e a suavizacao (1 px) tambem do lado esquerdo e de cima da ponta. A ponta
   do triangulo fica no texel (kMargem, kMargem), entao a caixa e desenhada em
   (x - kMargem, y - kMargem) para a ponta cair exatamente no ponto publicado. */
static const int kMargem = 4;

/* Tamanho da caixa para uma seta de altura `tam` (px). */
static inline void medir_seta(unsigned tam, unsigned *larg, unsigned *alt)
{
    *larg = (unsigned)(tam * 0.62f + 0.5f) + 2u * kMargem;
    *alt  = tam + 2u * kMargem;
}

/* A SETA, pixel a pixel, em BGRA de 8 bits, alpha nao pre-multiplicado.
   Triangulo (0,0)-(0,1)-(0.62,0.70) em coordenadas locais (y para baixo),
   branco por dentro, contorno preto de 2 px por fora, e 1 px de transicao
   suave em cada fronteira. `px` tem de ter larg*alt*4 bytes.

   As tres funcoes de aresta sao DISTANCIAS com sinal (normalizadas pelo
   comprimento da aresta), positivas para dentro. Sem normalizar, o contorno
   ficava mais fino numa aresta do que na outra; com o sinal errado, ficava
   tudo preto -- ver o bloco "O QUADRADO PRETO" la em cima. */
static inline void pintar_seta(unsigned char *px, unsigned larg, unsigned alt, unsigned tam)
{
    const float esc = (float)tam;                 /* px por unidade local */
    const float invEsc = 1.0f / esc;
    /* A=(0,0) B=(0,1) C=(0.62,0.70) */
    const float lenBC = 0.6708204f;               /* sqrt(0.62^2 + 0.30^2) */
    const float lenCA = 0.9349332f;               /* sqrt(0.62^2 + 0.70^2) */
    const float contorno = 2.0f * invEsc;         /* 2 px, em unidades locais */

    for (unsigned y = 0; y < alt; y++)
        for (unsigned x = 0; x < larg; x++)
        {
            /* centro do texel, relativo a ponta */
            const float u = ((float)x + 0.5f - (float)kMargem) * invEsc;
            const float v = ((float)y + 0.5f - (float)kMargem) * invEsc;

            const float dAB = u;                                              /* aresta x = 0 */
            const float dBC = -(0.62f * (v - 1.0f) + 0.30f * u) / lenBC;      /* B -> C */
            const float dCA = -(-0.62f * (v - 0.70f) + 0.70f * (u - 0.62f)) / lenCA; /* C -> A */
            float m = dAB < dBC ? dAB : dBC; if (dCA < m) m = dCA;

            /* cobertura (0..1) do branco e do conjunto branco+preto, com 1 px
               de rampa em cada fronteira */
            float cBranco = m * esc + 0.5f;
            float cTudo   = (m + contorno) * esc + 0.5f;
            if (cBranco < 0.0f) cBranco = 0.0f;
            if (cBranco > 1.0f) cBranco = 1.0f;
            if (cTudo   < 0.0f) cTudo   = 0.0f;
            if (cTudo   > 1.0f) cTudo   = 1.0f;

            unsigned char *p = px + ((size_t)y * larg + x) * 4;
            const unsigned char cor = (unsigned char)(cBranco * 255.0f + 0.5f);
            p[0] = cor; p[1] = cor; p[2] = cor;                 /* B G R: 0 = preto, 255 = branco */
            p[3] = (unsigned char)(cTudo * 255.0f + 0.5f);      /* A */
        }
}

typedef HRESULT(WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const void *, void *,
                                        LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

/* Triangulo de tela inteira; o VIEWPORT e quem o reduz a caixa da seta, entao
   uv 0..1 cobre exatamente a textura, e nao precisa de constant buffer. */
static const char *kVS_Seta = R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut main(uint id : SV_VertexID) {
    VSOut o;
    o.uv  = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)";
static const char *kPS_Seta = R"(
Texture2D t : register(t0); SamplerState s : register(s0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target { return t.Sample(s, uv); }
)";

class Ponteiro
{
public:
    ~Ponteiro() { soltar(); soltar_pipeline(); fechar_mapa(); }

    void aplicar(void *rtvHandle, void (*log)(const char *))
    {
        const Estado *e = ler();
        if (e == nullptr || e->versao != 1u || e->ligado == 0u || m_morto) return;

        ID3D11RenderTargetView *rtv = reinterpret_cast<ID3D11RenderTargetView *>(rtvHandle);
        ID3D11Texture2D *bb = nullptr; ID3D11Device *dev = nullptr;
        if (!abrir(rtv, &bb, &dev)) return;

        D3D11_TEXTURE2D_DESC d{}; bb->GetDesc(&d);
        bb->Release();
        if (d.Width == 0 || d.Height == 0) { dev->Release(); return; }

        /* O TAMANHO E TRAVADO numa faixa sensata antes de virar textura: um
           valor absurdo vindo da memoria compartilhada (app antigo, escrita
           rasgada) nao pode virar uma alocacao gigante nem um cursor que
           cobre a tela. */
        UINT tam = (UINT)(e->tamanho + 0.5f);
        if (tam < 12) tam = 12;
        if (tam > 160) tam = 160;

        if (!preparar_pipeline(dev, log)) { dev->Release(); return; }
        if (m_tex == nullptr || m_tam != tam)
        {
            soltar();
            if (!criar(dev, tam, log)) { dev->Release(); return; }
            m_tam = tam;
        }

        /* A PONTA DA SETA E O PONTO: o (x,y) que o app manda e onde a pessoa
           esta apontando, e a ponta fica kMargem texeis para dentro da caixa.
           A caixa pode comecar em coordenada negativa ou passar da borda:
           o viewport aceita, e o rasterizador corta no back buffer. Assim o
           cursor encostado num canto e cortado, nao some. */
        const long px = (long)(e->x + 0.5f) - kMargem;
        const long py = (long)(e->y + 0.5f) - kMargem;
        if (px + (long)m_larg <= 0 || py + (long)m_alt <= 0 ||
            px >= (long)d.Width || py >= (long)d.Height) { dev->Release(); return; }

        ID3D11DeviceContext *ctx = nullptr;
        dev->GetImmediateContext(&ctx);
        if (ctx != nullptr)
        {
            desenhar(ctx, rtv, (float)px, (float)py);
            ctx->Release();
        }
        dev->Release();

        if (!m_disse && log != nullptr)
        {
            m_disse = true;
            char l[200];
            snprintf(l, sizeof(l),
                     "[DLSS 5 Bridge] ponteiro desenhado DEPOIS da rede (com alpha): %ux%u px em %ld,%ld "
                     "(o shader do app nao serve - a rede reescreve o backbuffer inteiro).",
                     m_larg, m_alt, px, py);
            log(l);
        }
        /* CONTADOR POR SEGUNDO (14/09, bancada do clique): o teste de 10:29 teve UMA
           linha "desenhado" e nada na tela, e nao dava para saber se a seta foi
           desenhada uma vez so ou se so o LOG era de estreia. Agora, enquanto o modo
           Ponteiro estiver ligado, sai uma linha por segundo com quantas vezes a seta
           entrou no backbuffer e onde. Custo: um GetTickCount64 por quadro desenhado. */
        m_n++;
        const unsigned long long agora = GetTickCount64();
        if (m_t0 == 0) m_t0 = agora;
        if (agora - m_t0 >= 1000 && log != nullptr)
        {
            char l[160];
            snprintf(l, sizeof(l), "[DLSS 5 Bridge] ponteiro desenhado %u vezes no ultimo segundo, ultimo em %ld,%ld (%ux%u px)",
                     m_n, px, py, m_larg, m_alt);
            log(l);
            m_n = 0; m_t0 = agora;
        }
    }

    void soltar()
    {
        if (m_srv) { m_srv->Release(); m_srv = nullptr; }
        if (m_tex) { m_tex->Release(); m_tex = nullptr; }
        m_larg = m_alt = 0; m_tam = 0;
    }

    void soltar_pipeline()
    {
        if (m_vs)    { m_vs->Release();    m_vs = nullptr; }
        if (m_ps)    { m_ps->Release();    m_ps = nullptr; }
        if (m_blend) { m_blend->Release(); m_blend = nullptr; }
        if (m_samp)  { m_samp->Release();  m_samp = nullptr; }
        if (m_d3dc)  { FreeLibrary(m_d3dc); m_d3dc = nullptr; }
    }

private:
    const Estado *ler()
    {
        if (m_vista != nullptr) return m_vista;
        /* TENTA DE NOVO, DE TEMPOS EM TEMPOS - e nao uma vez so. A primeira versao
           desistia para sempre depois da primeira falha, e foi exatamente isso que
           impediu a seta de aparecer: o add-on roda no PRIMEIRO quadro e o app so
           cria a secao compartilhada quando o laco do mouse roda pela primeira vez,
           alguns quadros depois. O add-on chegava antes, falhava e nunca mais
           tentava - com tudo o resto certo (a conta do mapeamento, o warp, o
           desenho). Uma tentativa a cada ~120 quadros custa nada e nao deixa a
           funcionalidade inteira depender de quem acordou primeiro. */
        if (++m_espera < 120) return nullptr;
        m_espera = 0;
        m_mapa = OpenFileMappingW(FILE_MAP_READ, FALSE, kNome);
        if (m_mapa == nullptr) return nullptr;
        m_vista = (const Estado *)MapViewOfFile(m_mapa, FILE_MAP_READ, 0, 0, sizeof(Estado));
        return m_vista;
    }

    void fechar_mapa()
    {
        if (m_vista) { UnmapViewOfFile((LPCVOID)m_vista); m_vista = nullptr; }
        if (m_mapa)  { CloseHandle(m_mapa); m_mapa = nullptr; }
    }

    static bool abrir(ID3D11RenderTargetView *rtv, ID3D11Texture2D **tex, ID3D11Device **dev)
    {
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

    void falhar(const char *msg, HRESULT hr, void (*log)(const char *))
    {
        m_morto = true;   /* nao tenta de novo a cada quadro: um log e pronto */
        if (log == nullptr) return;
        char l[300];
        snprintf(l, sizeof(l), "[DLSS 5 Bridge] ponteiro: %s (0x%08X) - seta nao sera desenhada; "
                               "o modo Ponteiro continua funcionando", msg, (unsigned)hr);
        log(l);
    }

    /* Shaders, blend e sampler: uma vez por dispositivo. */
    bool preparar_pipeline(ID3D11Device *dev, void (*log)(const char *))
    {
        if (m_vs != nullptr) return true;

        if (m_d3dc == nullptr)
        {
            m_d3dc = LoadLibraryA("d3dcompiler_47.dll");
            if (m_d3dc == nullptr) m_d3dc = LoadLibraryA("d3dcompiler_46.dll");
            if (m_d3dc == nullptr) { falhar("d3dcompiler nao encontrado", 0, log); return false; }
        }
        PFN_D3DCompile compile = (PFN_D3DCompile)(void *)GetProcAddress(m_d3dc, "D3DCompile");
        if (compile == nullptr) { falhar("D3DCompile ausente", 0, log); return false; }

        ID3DBlob *b = nullptr, *err = nullptr;
        HRESULT hr = compile(kVS_Seta, strlen(kVS_Seta), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &b, &err);
        if (err) err->Release();
        if (FAILED(hr) || b == nullptr) { falhar("VS da seta nao compilou", hr, log); return false; }
        hr = dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &m_vs);
        b->Release(); b = nullptr;
        if (FAILED(hr)) { falhar("CreateVertexShader", hr, log); return false; }

        err = nullptr;
        hr = compile(kPS_Seta, strlen(kPS_Seta), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &b, &err);
        if (err) err->Release();
        if (FAILED(hr) || b == nullptr) { falhar("PS da seta nao compilou", hr, log); soltar_pipeline(); return false; }
        hr = dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &m_ps);
        b->Release();
        if (FAILED(hr)) { falhar("CreatePixelShader", hr, log); soltar_pipeline(); return false; }

        /* blend classico de sprite: saida = seta*a + fundo*(1-a) */
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend  = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp   = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = dev->CreateBlendState(&bd, &m_blend);
        if (FAILED(hr)) { falhar("CreateBlendState", hr, log); soltar_pipeline(); return false; }

        /* ponto, nao linear: a caixa tem o tamanho exato da textura, entao
           cada pixel cai no centro de um texel; a suavizacao ja esta na arte */
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr = dev->CreateSamplerState(&sd, &m_samp);
        if (FAILED(hr)) { falhar("CreateSamplerState", hr, log); soltar_pipeline(); return false; }
        return true;
    }

    bool criar(ID3D11Device *dev, UINT tam, void (*log)(const char *))
    {
        UINT larg = 0, alt = 0;
        medir_seta(tam, &larg, &alt);

        unsigned char *px = new (std::nothrow) unsigned char[(size_t)larg * alt * 4];
        if (px == nullptr) return false;
        pintar_seta(px, larg, alt, tam);

        /* Formato PROPRIO (BGRA 8 bits), independente do back buffer: quem
           casa os dois e o pixel shader, nao uma copia byte a byte. */
        D3D11_TEXTURE2D_DESC d{};
        d.Width = larg; d.Height = alt; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_IMMUTABLE; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = px; sd.SysMemPitch = larg * 4;
        HRESULT hr = dev->CreateTexture2D(&d, &sd, &m_tex);
        delete[] px;
        if (FAILED(hr) || m_tex == nullptr) { m_tex = nullptr; falhar("CreateTexture2D da seta", hr, log); return false; }
        hr = dev->CreateShaderResourceView(m_tex, nullptr, &m_srv);
        if (FAILED(hr) || m_srv == nullptr) { soltar(); falhar("CreateShaderResourceView da seta", hr, log); return false; }
        m_larg = larg; m_alt = alt;
        return true;
    }

    /* Um Draw de 3 vertices com o viewport na caixa da seta. Tudo que e
       tocado no contexto e salvo antes e devolvido depois: o ReShade continua
       o quadro depois de nos. */
    void desenhar(ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv, float x, float y)
    {
        UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        D3D11_VIEWPORT vpAnt[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        ctx->RSGetViewports(&nvp, vpAnt);
        ID3D11RenderTargetView *rtAnt = nullptr; ID3D11DepthStencilView *dsAnt = nullptr;
        ctx->OMGetRenderTargets(1, &rtAnt, &dsAnt);
        ID3D11BlendState *blAnt = nullptr; float bfAnt[4] = { 0, 0, 0, 0 }; UINT smAnt = 0xFFFFFFFF;
        ctx->OMGetBlendState(&blAnt, bfAnt, &smAnt);
        ID3D11DepthStencilState *dsstAnt = nullptr; UINT refAnt = 0;
        ctx->OMGetDepthStencilState(&dsstAnt, &refAnt);
        ID3D11RasterizerState *rsAnt = nullptr; ctx->RSGetState(&rsAnt);
        ID3D11VertexShader *vsAnt = nullptr; ctx->VSGetShader(&vsAnt, nullptr, nullptr);
        ID3D11PixelShader  *psAnt = nullptr; ctx->PSGetShader(&psAnt, nullptr, nullptr);
        ID3D11GeometryShader *gsAnt = nullptr; ctx->GSGetShader(&gsAnt, nullptr, nullptr);
        ID3D11InputLayout *ilAnt = nullptr; ctx->IAGetInputLayout(&ilAnt);
        D3D11_PRIMITIVE_TOPOLOGY topAnt = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED; ctx->IAGetPrimitiveTopology(&topAnt);
        ID3D11ShaderResourceView *srvAnt = nullptr; ctx->PSGetShaderResources(0, 1, &srvAnt);
        ID3D11SamplerState *sampAnt = nullptr; ctx->PSGetSamplers(0, 1, &sampAnt);

        D3D11_VIEWPORT vp{ x, y, (float)m_larg, (float)m_alt, 0.0f, 1.0f };
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(nullptr);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->OMSetBlendState(m_blend, nullptr, 0xFFFFFFFF);
        ctx->OMSetDepthStencilState(nullptr, 0);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_vs, nullptr, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->PSSetShader(m_ps, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &m_srv);
        ctx->PSSetSamplers(0, 1, &m_samp);
        ctx->Draw(3, 0);

        ID3D11ShaderResourceView *nulo = nullptr;
        ctx->PSSetShaderResources(0, 1, &nulo);
        ctx->PSSetSamplers(0, 1, &sampAnt);
        ctx->PSSetShaderResources(0, 1, &srvAnt);
        ctx->IASetPrimitiveTopology(topAnt);
        ctx->IASetInputLayout(ilAnt);
        ctx->GSSetShader(gsAnt, nullptr, 0);
        ctx->PSSetShader(psAnt, nullptr, 0);
        ctx->VSSetShader(vsAnt, nullptr, 0);
        ctx->RSSetState(rsAnt);
        ctx->OMSetDepthStencilState(dsstAnt, refAnt);
        ctx->OMSetBlendState(blAnt, bfAnt, smAnt);
        ctx->OMSetRenderTargets(1, &rtAnt, dsAnt);
        if (nvp) ctx->RSSetViewports(nvp, vpAnt);

        if (sampAnt) sampAnt->Release();
        if (srvAnt)  srvAnt->Release();
        if (ilAnt)   ilAnt->Release();
        if (gsAnt)   gsAnt->Release();
        if (psAnt)   psAnt->Release();
        if (vsAnt)   vsAnt->Release();
        if (rsAnt)   rsAnt->Release();
        if (dsstAnt) dsstAnt->Release();
        if (blAnt)   blAnt->Release();
        if (rtAnt)   rtAnt->Release();
        if (dsAnt)   dsAnt->Release();
    }

    HANDLE        m_mapa  = nullptr;
    const Estado *m_vista = nullptr;
    int   m_espera = 200;   /* forca uma tentativa logo no primeiro quadro */
    bool          m_disse = false;
    bool          m_morto = false;   /* pipeline falhou: nao insiste */
    unsigned      m_n  = 0;   /* seta desenhada N vezes desde a ultima linha do contador */
    unsigned long long m_t0 = 0;
    HMODULE       m_d3dc = nullptr;
    ID3D11VertexShader       *m_vs    = nullptr;
    ID3D11PixelShader        *m_ps    = nullptr;
    ID3D11BlendState         *m_blend = nullptr;
    ID3D11SamplerState       *m_samp  = nullptr;
    ID3D11Texture2D          *m_tex   = nullptr;
    ID3D11ShaderResourceView *m_srv   = nullptr;
    UINT m_larg = 0, m_alt = 0, m_tam = 0;
};

}   /* namespace dlss5_ponteiro */
