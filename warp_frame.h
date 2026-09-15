// =====================================================================================
//  warp_frame.h -- o quadro sintetizado: deforma o ULTIMO QUADRO PROCESSADO pelo campo de
//                  movimento e desenha o instante intermediario que o pulo de quadro pulou.
//
//  O PROBLEMA QUE ISTO RESOLVE. Com a camada intermitente ligada, um quadro em cada N nao e
//  apresentado. O modo `segurar` deixa o ultimo quadro processado na tela nesse intervalo:
//  acaba o piscar e sobra JUDDER, porque o movimento anda na metade da taxa. Aqui o quadro
//  do intervalo passa a ser DESENHADO -- o quadro anterior deformado pelo campo de
//  movimento --, que e o que um frame generation faz por dentro.
//
//  A CONTA, e ela e uma linha so. O campo do motion_estimate.h e BACKWARD e esta em delta
//  UV: `prev_uv = uv + mv`. O conteudo que hoje esta em `uv` estava em `uv + mv` no quadro
//  anterior; no instante intermediario `t` (0,5 = meio caminho) estava em `uv + t * mv`.
//  Entao amostrar o quadro anterior em `uv + t * mv` DA o quadro do meio. Nao ha mais nada.
//
//  QUANTO ISSO VALE, medido contra verdade-terreno em tests/warp_test.cpp (3440x1440,
//  translacao em pixel fracionario, que e o caso real):
//
//      px/quadro |  segurar (hoje)  |  sintetizar  |  ganho
//           2,5  |     23,21 dB     |   36,38 dB   |  +13,2
//          20,5  |     20,79 dB     |   37,73 dB   |  +16,9
//          48,5  |     17,96 dB     |   36,78 dB   |  +18,8
//
//  O erro do `segurar` CRESCE com a velocidade; o do warp fica plano. E a descricao exata
//  do judder: o defeito aparece quando ha movimento e piora com ele.
//
//  POR QUE O QUADRO DE ORIGEM E O PROCESSADO, E NAO A CAPTURA. Se a camada desenhasse o
//  warp da imagem CRUA, a tela alternaria entre a imagem processada e a crua -- que e
//  exatamente o flicker que o modo `segurar` existe para eliminar (ver §12 da ARQUITETURA).
//  Deformando a SAIDA da cadeia, o quadro sintetizado ja vem com DLSS, neural e efeitos
//  aplicados: os dois quadros alternados tem o mesmo tratamento de imagem.
//
//  E POR QUE ISSO NAO PAGA A CADEIA DE NOVO: no quadro sintetizado as tecnicas do ReShade
//  ficam desligadas, e foi MEDIDO que sem tecnicas nada da cadeia roda -- nem os .fx nem o
//  neural do RenoDX (dois dumps com NeuralUplift ligado e desligado sairam identicos bit a
//  bit). Ver docs/ARQUITETURA.md §12.1.
//
//  O QUE ESTE MODULO NAO FAZ, de proposito: ele nao decide quando sintetizar, nao liga nem
//  desliga tecnica nenhuma e nao fala com a ponte. Ele recebe uma textura, um campo e um
//  instante, e desenha. Quem orquestra e o main.cpp -- um modulo de desenho que tambem
//  decide quando desenhar e um modulo que esconde a politica dentro do mecanismo.
// =====================================================================================

#pragma once

#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <cstdio>

namespace dlss5 {

// ------------------------------------------------------------------------------------
// Shaders. Dois passes triviais; nada roda na fila 3D alem disto.
// ------------------------------------------------------------------------------------

static const char kWarp_VS[] = R"(
void main(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv  = float2((id << 1) & 2, id & 2);
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
)";

// O canal .z do campo e a VALIDADE (0 = o vetor foi descartado por magnitude). Multiplicar
// por ele faz o bloco descartado ficar PARADO em vez de saltar para um vetor que o hardware
// nao soube medir. Repetir e o erro menor, e e o mesmo criterio que o DLSS5_MV_Hardware.fx
// ja usa para zerar a confianca.
//
// ISTO IMPORTA MAIS AQUI DO QUE NA CADEIA, e vale saber por que: o DLSS5_Feed.fx roda
// `ValidateTests()` e DESCARTA o vetor que nao passa (`mv = flow * (1.0 - zero_vector)`).
// Um warp desenhado direto na tela nao tem para onde descartar -- cada vetor ruim vira
// pixel errado. E por isso que a grade do campo pesa mais no warp do que no DLSS.
static const char kWarp_PS[] = R"(
Texture2D    src   : register(t0);   // o ultimo quadro PROCESSADO
Texture2D    mvTex : register(t1);   // campo backward em delta UV (.xy) + validade (.z)
SamplerState smp   : register(s0);
cbuffer Params : register(b0)
{
    float2 cobertura;   // fracao do quadro coberta pela grade do campo
    float  t;           // instante intermediario: 0,5 = meio caminho
    float  _pad;
};
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float4 m  = mvTex.SampleLevel(smp, uv * cobertura, 0);
    float2 mv = m.xy * m.z;
    return src.SampleLevel(smp, uv + t * mv, 0);
}
)";

// ------------------------------------------------------------------------------------

class WarpFrame
{
public:
    WarpFrame() = default;
    ~WarpFrame() { Shutdown(); }
    WarpFrame(const WarpFrame&) = delete;
    WarpFrame& operator=(const WarpFrame&) = delete;

    // Uma vez, com o device da camada. `w`/`h` sao o tamanho do backbuffer: e o tamanho da
    // copia do quadro processado que este modulo guarda.
    bool Init(ID3D11Device* dev, UINT w, UINT h)
    {
        Shutdown();
        if (!dev || !w || !h) return Falhar("parametros invalidos");
        m_dev = dev; m_dev->AddRef();
        m_w = w; m_h = h;

        if (!CompilarShaders()) return false;

        // A copia do ultimo quadro processado. Precisa ser DEFAULT + SHADER_RESOURCE: e
        // destino de CopyResource e origem de amostragem, nunca tocada pela CPU.
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(m_dev->CreateTexture2D(&td, nullptr, &m_hist))) return Falhar("CreateTexture2D(historico)");
        if (FAILED(m_dev->CreateShaderResourceView(m_hist, nullptr, &m_histSrv))) return Falhar("CreateShaderResourceView(historico)");

        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(m_dev->CreateSamplerState(&sd, &m_samp))) return Falhar("CreateSamplerState");

        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_cb))) return Falhar("CreateBuffer(params)");

        m_pronto = true;
        m_temHistorico = false;
        return true;
    }

    // Guarda o quadro PROCESSADO que acabou de ir para a tela. Chamada depois de cada
    // Present que rodou a cadeia -- e so nesses: guardar um quadro sintetizado faria o
    // proximo warp deformar um warp, e o erro viraria bola de neve ao longo da sequencia.
    bool Guardar(ID3D11DeviceContext* ctx, ID3D11Texture2D* processado)
    {
        if (!m_pronto || !ctx || !processado) return false;
        D3D11_TEXTURE2D_DESC d{}; processado->GetDesc(&d);
        // Tamanho diferente nao e erro: a fonte pode ter mudado com a cadeia viva (ver
        // FonteMudou no main.cpp). Sem historico do tamanho certo, o modo apenas nao
        // sintetiza neste quadro -- e o chamador cai no `segurar`, que nunca falha.
        if (d.Width != m_w || d.Height != m_h) { m_temHistorico = false; return false; }
        ctx->CopyResource(m_hist, processado);
        m_temHistorico = true;
        return true;
    }

    // Desenha o quadro sintetizado no alvo. `mvSrv` e o campo do motion_estimate.h;
    // `covU`/`covV` vem do CoverageU()/CoverageV() dele; `t` e o instante (0,5 = meio).
    //
    // O VIEWPORT vem de fora porque a camada pode estar com letterbox (fonte de outra
    // proporcao que o backbuffer). Desenhar em tela cheia aqui esticaria a imagem no
    // exato caso em que o resto do app teve o cuidado de nao esticar.
    bool Desenhar(ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv,
                  ID3D11ShaderResourceView* mvSrv,
                  float covU, float covV, float t,
                  const D3D11_VIEWPORT& vp)
    {
        if (!m_pronto || !m_temHistorico || !ctx || !rtv || !mvSrv) return false;

        struct { float covU, covV, t, pad; } p{ covU, covV, t, 0.0f };
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, &p, sizeof(p));
            ctx->Unmap(m_cb, 0);
        }

        ID3D11ShaderResourceView* srvs[2] = { m_histSrv, mvSrv };
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_vs, nullptr, 0);
        ctx->PSSetShader(m_ps, nullptr, 0);
        ctx->PSSetShaderResources(0, 2, srvs);
        ctx->PSSetSamplers(0, 1, &m_samp);
        ctx->PSSetConstantBuffers(0, 1, &m_cb);
        ctx->Draw(3, 0);

        // Soltar os SRVs: o historico e alvo de CopyResource no proximo quadro processado,
        // e um recurso ainda ligado como shader resource nao pode ser destino de copia.
        ID3D11ShaderResourceView* nulos[2] = { nullptr, nullptr };
        ctx->PSSetShaderResources(0, 2, nulos);
        return true;
    }

    void Shutdown()
    {
        Rel(m_cb); Rel(m_samp); Rel(m_histSrv); Rel(m_hist);
        Rel(m_ps); Rel(m_vs); Rel(m_dev);
        m_pronto = false; m_temHistorico = false; m_w = m_h = 0;
    }

    bool Pronto() const { return m_pronto; }
    // Ha um quadro processado guardado? Enquanto nao houver (primeiro quadro da sessao,
    // troca de resolucao), o chamador tem de cair no `segurar`.
    bool TemHistorico() const { return m_temHistorico; }
    const char* UltimoErro() const { return m_err[0] ? m_err : nullptr; }

private:
    template <class T> static void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

    bool Falhar(const char* o)
    {
        snprintf(m_err, sizeof(m_err), "%s", o);
        Shutdown();
        return false;
    }

    // D3DCompile carregado em tempo de execucao, como o motion_estimate.h ja faz: assim o
    // app nao ganha dependencia de -ld3dcompiler no link.
    typedef HRESULT(WINAPI* PFN_D3DCOMPILE)(LPCVOID, SIZE_T, LPCSTR, const void*, void*,
                                            LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    bool CompilarShaders()
    {
        static PFN_D3DCOMPILE fn = nullptr;
        static bool tentou = false;
        if (!tentou) {
            tentou = true;
            for (const wchar_t* nome : { L"d3dcompiler_47.dll", L"d3dcompiler_46.dll" }) {
                if (HMODULE mod = LoadLibraryW(nome)) {
                    fn = (PFN_D3DCOMPILE)(void*)GetProcAddress(mod, "D3DCompile");
                    if (fn) break;
                }
            }
        }
        if (!fn) return Falhar("d3dcompiler nao encontrada");

        ID3DBlob* bvs = nullptr; ID3DBlob* bps = nullptr; ID3DBlob* erro = nullptr;
        HRESULT hr = fn(kWarp_VS, strlen(kWarp_VS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &bvs, &erro);
        if (FAILED(hr)) { Rel(erro); Rel(bvs); return Falhar("compilar VS do warp"); }
        Rel(erro);
        hr = fn(kWarp_PS, strlen(kWarp_PS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &bps, &erro);
        if (FAILED(hr)) { Rel(erro); Rel(bvs); Rel(bps); return Falhar("compilar PS do warp"); }
        Rel(erro);

        hr = m_dev->CreateVertexShader(bvs->GetBufferPointer(), bvs->GetBufferSize(), nullptr, &m_vs);
        if (SUCCEEDED(hr))
            hr = m_dev->CreatePixelShader(bps->GetBufferPointer(), bps->GetBufferSize(), nullptr, &m_ps);
        Rel(bvs); Rel(bps);
        if (FAILED(hr)) return Falhar("criar shaders do warp");
        return true;
    }

    ID3D11Device*             m_dev     = nullptr;
    ID3D11VertexShader*       m_vs      = nullptr;
    ID3D11PixelShader*        m_ps      = nullptr;
    ID3D11Texture2D*          m_hist    = nullptr;   // o ultimo quadro processado
    ID3D11ShaderResourceView* m_histSrv = nullptr;
    ID3D11SamplerState*       m_samp    = nullptr;
    ID3D11Buffer*             m_cb      = nullptr;
    UINT m_w = 0, m_h = 0;
    bool m_pronto = false;
    bool m_temHistorico = false;
    char m_err[160] = { 0 };
};

} // namespace dlss5
