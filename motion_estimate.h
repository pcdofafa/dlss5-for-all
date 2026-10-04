// motion_estimate.h -- vetores de movimento pelo bloco de video da GPU.
//
// O app captura a imagem FINAL do jogo (WGC) e roda a cadeia ReShade em cima. O DLSS
// (DLAA) e acumulacao temporal: sem motion vectors ele nao consegue reprojetar o
// historico e tudo que se move deixa rastro. Como nao existe motion vector do jogo,
// este modulo ESTIMA um, usando o D3D12 Video Motion Estimation -- o mesmo bloco de
// hardware que o encoder de video usa para achar deslocamento entre quadros.
//
// Por que o bloco de video: ele nao disputa a fila 3D. A cadeia ja satura a GPU a
// 3440x1440; um estimador em pixel shader (o Lumenite_Kernel faz isso hoje) custa
// tempo de render que nao temos. Medido nesta maquina (RTX 4080):
//
//     3440x1440 bloco 8x8   -> grade 430x180   2.90 ms/quadro (fila de video)
//     3440x1440 bloco 16x16 -> grade 215x90    1.86 ms/quadro
//     1720x720  bloco 8x8   -> grade 215x90    0.89 ms/quadro   <-- padrao
//      860x360  bloco 8x8   -> grade 108x45    0.46 ms/quadro
//
// Meia resolucao com bloco 8x8 da a MESMA grade que resolucao plena com 16x16 pela
// metade do custo, e DOBRA o alcance de busca em pixels de saida. Por isso o divisor
// padrao e 2.
//
// Precisao verificada com deslocamento conhecido (me_test.cpp, divisor 2):
//     +7 px em X          -> mediana (-6.99, +0.00) px, 100% dos blocos
//     -13,+5 px           -> mediana (+13.00, -5.00) px, 100% dos blocos
//     0 px (quadro igual) -> exatamente zero, 0% de blocos nao-zero
//
// ALCANCE, de varredura fina (o numero que mais importa na pratica):
//     divisor 1: ok ate  40 px de saida, 1a falha em  48
//     divisor 2: ok ate  80 px de saida, 1a falha em  90
//     divisor 4: ok ate 160 px de saida, 1a falha em 192
// Convertido para a resolucao de estimativa isso e sempre o mesmo: acerta ate 40 px,
// primeira falha entre 45 e 48. E propriedade do hardware, nao dos divisores.
//
// Duas coisas para levar a serio:
//   - O VME NAO inventa movimento numa cena parada. Quadro identico -> exatamente zero.
//   - Acima do alcance ele NAO devolve um vetor grande e errado: devolve um PEQUENO e
//     errado (a 130 px de saida o campo sai com -3.5 px). Nenhum limiar de magnitude
//     distingue isso de movimento lento real.
// MV errado e pior que MV zero, entao o consumidor TEM de usar a validacao do
// DLSS5_Feed.fx (MV_VALIDATE, MV_CONSISTENCY, VALIDATE_STATIC, VALIDATE_LUMA) em cima
// disto. Este modulo entrega materia-prima, nao verdade.
//
// Sinal: o VME devolve BACKWARD MV (do quadro atual para onde o conteudo estava no
// anterior), que e exatamente a convencao do DLSS e do DLSS5_Feed.fx. Nao inverta.
//
// Saida: uma textura RGBA16F na grade de blocos, em DELTA UV (prev_uv = uv + mv), que e
// a convencao do provider do DLSS5_Feed.fx. Entregue o SRV ao add-on ponte com
// reshade::api::effect_runtime::update_texture_bindings().
//
// Uso:
//     dlss5::MotionEstimator me;
//     me.Init(d3d11Device, 3440, 1440);          // uma vez
//     ...por quadro, com a textura BGRA capturada:
//     me.Update(ctx, capturedBGRA);
//     ID3D11ShaderResourceView* mv = me.MotionSRV();   // RGBA16F: .xy delta UV, .z validade
//
// Compilar: precisa de -ld3d11 -ld3d12 -ldxgi -ld3dcompiler.

#pragma once

#include "saida_rapida.h"   // Saida::Teto: nenhuma espera de GPU passa de 200 ms no encerramento
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <d3d12video.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

namespace dlss5 {

// ------------------------------------------------------------------------------------
// Shaders. Sao dois passes triviais em D3D11; nada roda na fila 3D alem disto.
// ------------------------------------------------------------------------------------

// Vertex shader de tela cheia sem vertex buffer.
static const char kME_VS[] = R"(
void main(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv  = float2((id << 1) & 2, id & 2);
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
)";

// BGRA -> luma 8 bits, escrito no plano Y (R8_UNORM) da textura NV12.
// O croma fica constante em 128 e nunca e reescrito: o casamento de blocos do VME se
// apoia na luma, e croma constante contribui zero para o SAD. Isso corta metade da
// conversao por quadro.
static const char kME_PS_LUMA[] = R"(
Texture2D    src : register(t0);
SamplerState smp : register(s0);
float main(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    float3 c = src.SampleLevel(smp, uv, 0).rgb;
    // Rec.601 limited range: o VME espera video, nao full range.
    float y = dot(c, float3(0.299, 0.587, 0.114));
    return saturate(y) * (219.0 / 255.0) + (16.0 / 255.0);
}
)";

// Vetores resolvidos (R16G16_SINT, quarter-pel) -> RGBA16F.
//   .xy = delta UV (divide por 4 para ter pixels, depois pelo tamanho do buffer)
//   .z  = validade: 1 = o hardware entregou este vetor, 0 = foi descartado aqui
//
// O DLSS5_Feed.fx multiplica .xy de volta por BUFFER_WIDTH/HEIGHT no PS_MotionVectors.
//
// POR QUE EXISTE O CANAL .z, E POR QUE ELE NAO PODE FALTAR.
// O descarte por magnitude (rejectPx) escreve (0,0) no vetor. Do lado do Feed isso e
// indistinguivel de "este pixel nao se moveu" -- e a diferenca importa muito:
//
//   - "nao se moveu"  -> reprojetar o historico da propria posicao esta CERTO
//   - "eu nao sei"    -> reprojetar da propria posicao PRENDE o historico ali
//
// Sem este canal, o segundo caso passa em silencio: o teste da hipotese estatica do
// Feed nem chega a rodar, porque ele so roda para vetor acima de meio pixel
// (`length(mv * BUFFER_SCREEN_SIZE) > 0.5`), e um vetor descartado tem magnitude zero.
// Ou seja, o mecanismo que existe justamente para avisar o DLSS quando um vetor foi
// zerado nao enxerga o zeramento feito aqui.
//
// A medicao mostra que isso nao e teorico: a 90 px de deslocamento com divisor 2, o
// descarte zera 45,4% dos blocos. Quase metade do quadro prenderia o historico sem
// aviso, e a outra metade entregaria vetor errado -- o pior dos dois mundos.
//
// Com o .z, o DLSS5_MV_Hardware.fx zera a confianca desses blocos e o Feed transforma
// isso em desconfianca explicita, que e o que faz o DLSS se apoiar no quadro atual ali.
static const char kME_PS_CONVERT[] = R"(
Texture2D<int2> mvSint : register(t0);
cbuffer Params : register(b0)
{
    float2 invBuffer;   // 1 / (largura, altura) do buffer de saida
    float  pxScale;     // quarter-pel -> pixels de saida (0.25 * divisor)
    float  rejectPx;    // descarta acima disto, em pixels de saida (0 = nao descarta)
};
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    int2   raw = mvSint.Load(int3((int2)pos.xy, 0));
    float2 px  = float2(raw) * pxScale;
    if (rejectPx > 0.0 && dot(px, px) > rejectPx * rejectPx)
        return float4(0.0, 0.0, 0.0, 0.0);   // vetor zerado E marcado como nao confiavel
    return float4(px * invBuffer, 1.0, 0.0);
}
)";

// ------------------------------------------------------------------------------------

class MotionEstimator
{
public:
    MotionEstimator() = default;
    ~MotionEstimator() { Shutdown(); }

    MotionEstimator(const MotionEstimator&) = delete;
    MotionEstimator& operator=(const MotionEstimator&) = delete;

    // srcW/srcH: tamanho do quadro capturado (e do backbuffer da cadeia).
    // divisor:   1 = estima na resolucao plena, 2 = meia (padrao, metade do custo e
    //            o dobro do alcance de busca em pixels de saida).
    // blockSize: 8 ou 16. Com divisor 2 e bloco 8 a grade sai em 1/16 do buffer.
    // rejectPx:  descarta vetores acima disto (pixels de saida por quadro).
    //            <= 0 = automatico (32 * divisor). Passe um valor gigante para desligar.
    bool Init(ID3D11Device* dev11, UINT srcW, UINT srcH,
              UINT divisor = 2, UINT blockSize = 8, float rejectPx = -1.0f);

    // Uma vez por quadro, com o quadro recem-capturado. O primeiro quadro so
    // preenche a referencia e devolve vetores zerados (correto: nao ha anterior).
    // Prefira a versao com SRV quando o app ja tem um: a versao com textura cria e
    // destroi um SRV por quadro so para chegar no mesmo lugar.
    bool Update(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* frameSRV);
    bool Update(ID3D11DeviceContext* ctx, ID3D11Texture2D* frameBGRA);

    // Resultado da sonda, agregado sobre uma JANELA de amostras.
    struct ProbeStats
    {
        float    meanPx      = 0.0f;  // |mv| medio, sobre todas as amostras
        float    medianPx    = 0.0f;  // mediana das medias por amostra
        float    maxPx       = 0.0f;  // maior |mv| visto em qualquer amostra
        float    pctNonZero  = 0.0f;  // % medio de blocos com vetor
        float    pctRejected = 0.0f;  // % medio de blocos descartados por magnitude
        float    pctFieldZero= 0.0f;  // % de AMOSTRAS com o campo inteiro zerado
        unsigned samples     = 0;
    };

    // Diagnostico. Amostra o campo agora e acumula; devolve true e preenche `out`
    // SO quando a janela de `window` amostras fecha.
    //
    // POR QUE ACUMULAR, E NAO MEDIR UM QUADRO.
    // A sonda do dlss5-feed mede um quadro so (o de numero 1200) e isso produziu um
    // numero que parecia catastrofico e nao era: no run 13:26 ela leu "0,000 px, 0%
    // nao-zero" a 40 px de deslocamento, enquanto a reproducao offline do mesmo
    // cenario da 40,000 px com 100% dos blocos. A explicacao esta no ritmo do WGC do
    // mesmo log -- "min 0.01 p05 9.49 media 20.32 max 747.06 ms": ha entregas
    // duplicadas e buracos de quase um segundo. Um quadro repetido produz campo
    // exatamente zero (medido: quadros identicos dao 0,000 px e 0% de blocos), entao
    // um instantaneo que calhe de cair num repetido le zero e mente sobre o resto.
    //
    // `pctFieldZero` existe justamente para isso: mede quantas amostras vieram com o
    // campo inteiro zerado. NAO distingue as duas causas possiveis -- quadro repetido
    // pelo WGC ou cena genuinamente parada --, mas com conteudo em movimento ele e a
    // taxa de repeticao, e e o numero que diz se vale confiar na media.
    //
    // Nao trava a fila: a leitura de volta usa duas staging alternadas e mapeia a que
    // foi copiada na amostra ANTERIOR, ja concluida ha dezenas de quadros.
    bool ProbeSample(ID3D11DeviceContext* ctx, unsigned window, ProbeStats* out);

    // DIAGNOSTICO (so com DLSS5_MV_DIAG=1): fracao das amostras do plano Y em que as duas
    // NV12 -- o par que o VME compara -- diferem por mais de 2 niveis. Separa de vez as duas
    // causas de campo zerado: ENTRADA IGUAL (devolve ~0) ou o hardware nao achou nada
    // (devolve a fracao real de mudanca). Faz Map com espera; e so para diagnostico.
    float EntradaDiferenca(ID3D11DeviceContext* ctx);

    // DIAGNOSTICO: o que o app deixou ligado no contexto imediato ANTES da nossa higiene.
    // Devolve um texto de uma linha (buffer interno). Serve para nomear a causa quando o
    // campo vier zerado - ver o comentario dos estados em InitShaders.
    const char* EstadoHerdado(ID3D11DeviceContext* ctx);

    void Shutdown();

    bool Ready() const { return m_ready; }
    // RGBA16F na grade de blocos: .xy = delta UV, .z = validade (0 = descartado). Entregue isto ao update_texture_bindings.
    ID3D11ShaderResourceView* MotionSRV() const { return m_mvFloatSRV; }
    UINT MvWidth()  const { return m_mvW; }
    UINT MvHeight() const { return m_mvH; }
    // Fracao do quadro coberta pela grade: a resolucao de conversao e arredondada para
    // baixo ate um multiplo do bloco, entao a ultima faixa a direita/embaixo pode ficar
    // de fora. Quem amostra a textura deve multiplicar o uv por isto (ou ignorar: com
    // 3440x1440, divisor 2 e bloco 8 da exatamente 1.0).
    float CoverageU() const { return m_srcW ? float(m_convW * m_divisor) / float(m_srcW) : 1.0f; }
    float CoverageV() const { return m_srcH ? float(m_convH * m_divisor) / float(m_srcH) : 1.0f; }
    UINT BlockSize() const { return m_block; }
    // Diagnostico: ultima mensagem de falha, ou nullptr.
    const char* LastError() const { return m_err[0] ? m_err : nullptr; }

private:
    template <class T> static void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }
    bool Fail(const char* what, HRESULT hr)
    {
        std::snprintf(m_err, sizeof(m_err), "%s -> 0x%08lX", what, (unsigned long)hr);
        return false;
    }

    bool InitD3D12(IDXGIAdapter* adapter);
    bool InitShared(UINT convW, UINT convH);
    bool InitShaders();

    // --- D3D11 ---
    ID3D11Device*             m_dev11 = nullptr;
    ID3D11Device5*            m_dev11_5 = nullptr;
    ID3D11VertexShader*       m_vs = nullptr;
    ID3D11PixelShader*        m_psLuma = nullptr;
    ID3D11PixelShader*        m_psConvert = nullptr;
    ID3D11SamplerState*       m_sampler = nullptr;
    ID3D11RasterizerState*    m_rs = nullptr;       // nosso: sem tesoura, sem descarte de face
    ID3D11BlendState*         m_bs = nullptr;       // nosso: sem mistura, mascara de escrita inteira
    ID3D11DepthStencilState*  m_dss = nullptr;      // nosso: sem teste de profundidade
    ID3D11Texture2D*          m_yStage[2]{};        // diagnostico: le o plano Y das duas NV12
    ID3D11Buffer*             m_cb = nullptr;
    ID3D11Texture2D*          m_nv12[2]{};          // ping-pong, criadas em D3D11
    ID3D11RenderTargetView*   m_nv12YRTV[2]{};      // vista R8_UNORM do plano Y
    ID3D11Texture2D*          m_mvSint11 = nullptr; // a resolvida do D3D12, aberta aqui
    ID3D11ShaderResourceView* m_mvSintSRV = nullptr;
    ID3D11Texture2D*          m_mvFloat = nullptr;  // RGBA16F entregue ao ReShade
    ID3D11RenderTargetView*   m_mvFloatRTV = nullptr;
    ID3D11ShaderResourceView* m_mvFloatSRV = nullptr;
    ID3D11Fence*              m_fence11 = nullptr;

    // --- D3D12 ---
    ID3D12Device*                m_dev12 = nullptr;
    ID3D12VideoDevice1*          m_vdev = nullptr;
    ID3D12VideoMotionEstimator*  m_est = nullptr;
    ID3D12VideoMotionVectorHeap* m_heap = nullptr;
    ID3D12CommandQueue*          m_qVideo = nullptr;
    ID3D12CommandAllocator*      m_alloc = nullptr;
    ID3D12VideoEncodeCommandList* m_clVideo = nullptr;
    ID3D12Resource*              m_nv12_12[2]{};    // as mesmas NV12, abertas aqui
    ID3D12Resource*              m_mvSint = nullptr;
    ID3D12Fence*                 m_fence12 = nullptr;

    // --- sonda acumulada ---
    ID3D11Texture2D* m_probeStage[2]{};   // alternadas: copia numa, le a outra
    int   m_probeSlot = 0;
    bool  m_probePending = false;         // ha copia esperando na slot oposta
    std::vector<float> m_probeMeans;      // media por amostra, para a mediana
    double m_probeSumMean = 0.0, m_probeSumNz = 0.0, m_probeSumRej = 0.0;
    float  m_probeMax = 0.0f;
    unsigned m_probeZeroFields = 0;

    HANDLE m_sharedNv12[2]{};
    HANDLE m_sharedMv = nullptr;
    HANDLE m_sharedFence11 = nullptr;
    HANDLE m_sharedFence12 = nullptr;
    ID3D11Fence* m_fence12_11 = nullptr;   // a fence do D3D12 vista pelo D3D11
    ID3D12Fence* m_fence11_12 = nullptr;   // a fence do D3D11 vista pelo D3D12

    UINT   m_srcW = 0, m_srcH = 0, m_convW = 0, m_convH = 0;
    UINT   m_mvW = 0, m_mvH = 0, m_block = 8, m_divisor = 2;
    float  m_rejectPx = 32.0f;
    UINT   m_cur = 0;
    UINT64 m_fv = 0;
    bool   m_ready = false;
    bool   m_havePrev = false;
    char   m_err[192]{};
};

// ------------------------------------------------------------------------------------

inline bool MotionEstimator::Init(ID3D11Device* dev11, UINT srcW, UINT srcH,
                                  UINT divisor, UINT blockSize, float rejectPx)
{
    Shutdown();
    if (!dev11 || !srcW || !srcH) return Fail("argumentos invalidos", E_INVALIDARG);

    m_dev11 = dev11; m_dev11->AddRef();
    m_srcW = srcW; m_srcH = srcH;
    m_divisor = (divisor < 1) ? 1 : divisor;
    m_block = (blockSize == 16) ? 16 : 8;
    m_rejectPx = rejectPx;

    // A resolucao de conversao TEM de ser multipla do tamanho do bloco. Com 1720x720 e
    // bloco 16 (1720 / 16 = 107.5) o VME devolve a grade inteira zerada e ainda custa 4x
    // mais -- falha silenciosa, medida nesta maquina. Arredondar para baixo resolve.
    // O preco e nao cobrir a ultima faixa do quadro; CoverageU/V dizem o quanto.
    m_convW = (srcW / m_divisor) / m_block * m_block;
    m_convH = (srcH / m_divisor) / m_block * m_block;
    if (m_convW < 32 || m_convH < 32) return Fail("resolucao de conversao pequena demais", E_INVALIDARG);

    // rejectPx <= 0 = automatico: 40 px na RESOLUCAO DE ESTIMATIVA, ou seja
    // 40 * divisor pixels do quadro de saida.
    //
    // O 40 vem de varredura fina de deslocamento imposto a 3440x1440 (me_test.exe).
    // O VME acerta na mosca ate 40 px de conversao, e a PRIMEIRA falha de cada divisor
    // cai sempre entre 45 e 48 px de conversao:
    //
    //     divisor 1: ok ate 40 out (40 conv), falha em  48 out (48 conv)
    //     divisor 2: ok ate 80 out (40 conv), falha em  90 out (45 conv)
    //     divisor 4: ok ate 160 out (40 conv), falha em 192 out (48 conv)
    //
    // Acima disso ele NAO falha em degrau limpo: 96 px de saida com divisor 2 acerta,
    // mesmo com 90 errando. Esse acerto e alinhamento de padrao, nao alcance -- por
    // isso o corte fica no ultimo valor CONSISTENTE, e nao no maior que ja passou.
    //
    // Este e o UNICO limiar de magnitude do caminho; o do shader fica desligado. Ele
    // pega o caso "tao rapido que muitos blocos devolvem vetor grande e errado", e nao
    // pega o caso "casou o bloco errado e devolveu vetor pequeno e errado" -- para esse
    // nao existe limiar, e quem pega e a validacao por patch do DLSS5_Feed.fx.
    if (m_rejectPx <= 0.0f) m_rejectPx = 40.0f * float(m_divisor);

    HRESULT hr = m_dev11->QueryInterface(__uuidof(ID3D11Device5), (void**)&m_dev11_5);
    if (FAILED(hr)) return Fail("ID3D11Device5 (precisa de Windows 10 1703+)", hr);

    IDXGIDevice* dxgiDev = nullptr;
    hr = m_dev11->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev);
    if (FAILED(hr)) return Fail("IDXGIDevice", hr);
    IDXGIAdapter* adapter = nullptr;
    hr = dxgiDev->GetAdapter(&adapter);
    Rel(dxgiDev);
    if (FAILED(hr)) return Fail("GetAdapter", hr);

    bool ok = InitD3D12(adapter);
    Rel(adapter);
    if (!ok) return false;

    if (!InitShared(m_convW, m_convH)) return false;
    if (!InitShaders()) return false;

    m_ready = true;
    m_havePrev = false;
    m_cur = 0;
    return true;
}

inline bool MotionEstimator::InitD3D12(IDXGIAdapter* adapter)
{
    HRESULT hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                   __uuidof(ID3D12Device), (void**)&m_dev12);
    if (FAILED(hr)) return Fail("D3D12CreateDevice", hr);

    hr = m_dev12->QueryInterface(__uuidof(ID3D12VideoDevice1), (void**)&m_vdev);
    if (FAILED(hr)) return Fail("ID3D12VideoDevice1", hr);

    D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR sup{};
    sup.NodeIndex = 0;
    sup.InputFormat = DXGI_FORMAT_NV12;
    hr = m_vdev->CheckFeatureSupport(D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR, &sup, sizeof(sup));
    if (FAILED(hr)) return Fail("CheckFeatureSupport(MOTION_ESTIMATOR)", hr);

    const UINT want8  = D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAG_8X8;
    const UINT want16 = D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAG_16X16;
    if (sup.BlockSizeFlags == D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAG_NONE)
        return Fail("a GPU nao suporta VME para NV12", E_NOTIMPL);
    if (m_block == 8 && !(sup.BlockSizeFlags & want8))  m_block = 16;
    if (m_block == 16 && !(sup.BlockSizeFlags & want16)) m_block = 8;
    // O bloco pode ter mudado acima; a resolucao de conversao tem de continuar multipla dele.
    m_convW = (m_srcW / m_divisor) / m_block * m_block;
    m_convH = (m_srcH / m_divisor) / m_block * m_block;
    if (m_convW < sup.SizeRange.MinWidth || m_convH < sup.SizeRange.MinHeight ||
        m_convW > sup.SizeRange.MaxWidth || m_convH > sup.SizeRange.MaxHeight)
        return Fail("resolucao de conversao fora da faixa suportada pelo VME", E_INVALIDARG);

    const auto blockEnum = (m_block == 16)
        ? D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_16X16
        : D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_8X8;
    D3D12_VIDEO_SIZE_RANGE range{ m_convW, m_convH, m_convW, m_convH };

    D3D12_VIDEO_MOTION_ESTIMATOR_DESC meDesc{};
    meDesc.NodeMask = 0;
    meDesc.InputFormat = DXGI_FORMAT_NV12;
    meDesc.BlockSize = blockEnum;
    meDesc.Precision = D3D12_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_QUARTER_PEL;
    meDesc.SizeRange = range;
    hr = m_vdev->CreateVideoMotionEstimator(&meDesc, nullptr,
            __uuidof(ID3D12VideoMotionEstimator), (void**)&m_est);
    if (FAILED(hr)) return Fail("CreateVideoMotionEstimator", hr);

    D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC hd{};
    hd.NodeMask = 0;
    hd.InputFormat = DXGI_FORMAT_NV12;
    hd.BlockSize = blockEnum;
    hd.Precision = D3D12_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_QUARTER_PEL;
    hd.SizeRange = range;
    hr = m_vdev->CreateVideoMotionVectorHeap(&hd, nullptr,
            __uuidof(ID3D12VideoMotionVectorHeap), (void**)&m_heap);
    if (FAILED(hr)) return Fail("CreateVideoMotionVectorHeap", hr);

    m_mvW = (m_convW + m_block - 1) / m_block;
    m_mvH = (m_convH + m_block - 1) / m_block;

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE;
    hr = m_dev12->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&m_qVideo);
    if (FAILED(hr)) return Fail("fila VIDEO_ENCODE", hr);

    hr = m_dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
            __uuidof(ID3D12CommandAllocator), (void**)&m_alloc);
    if (FAILED(hr)) return Fail("allocator de video", hr);

    hr = m_dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE, m_alloc, nullptr,
            __uuidof(ID3D12VideoEncodeCommandList), (void**)&m_clVideo);
    if (FAILED(hr)) return Fail("lista de video", hr);
    m_clVideo->Close();

    hr = m_dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&m_fence12);
    if (FAILED(hr)) return Fail("fence D3D12", hr);
    return true;
}

inline bool MotionEstimator::InitShared(UINT convW, UINT convH)
{
    HRESULT hr;
    ID3D11Device1* dev11_1 = nullptr;
    hr = m_dev11->QueryInterface(__uuidof(ID3D11Device1), (void**)&dev11_1);
    if (FAILED(hr)) return Fail("ID3D11Device1", hr);

    // --- NV12 ping-pong: criadas em D3D11 (onde o passe de luma escreve), abertas em D3D12.
    for (int i = 0; i < 2; ++i)
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = convW; td.Height = convH; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_NV12;
        td.SampleDesc = { 1, 0 };
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
        hr = m_dev11->CreateTexture2D(&td, nullptr, &m_nv12[i]);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("criar NV12", hr); }

        // RTV do plano Y: vista R8_UNORM sobre a NV12.
        D3D11_RENDER_TARGET_VIEW_DESC rtvd{};
        rtvd.Format = DXGI_FORMAT_R8_UNORM;
        rtvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvd.Texture2D.MipSlice = 0;
        hr = m_dev11->CreateRenderTargetView(m_nv12[i], &rtvd, &m_nv12YRTV[i]);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("RTV do plano Y", hr); }

        IDXGIResource1* dr = nullptr;
        hr = m_nv12[i]->QueryInterface(__uuidof(IDXGIResource1), (void**)&dr);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("IDXGIResource1 da NV12", hr); }
        hr = dr->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                    nullptr, &m_sharedNv12[i]);
        Rel(dr);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("CreateSharedHandle da NV12", hr); }

        hr = m_dev12->OpenSharedHandle(m_sharedNv12[i], __uuidof(ID3D12Resource), (void**)&m_nv12_12[i]);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("OpenSharedHandle da NV12 no D3D12", hr); }
    }

    // --- textura resolvida: criada em D3D11 e aberta em D3D12, nao o contrario.
    // Nesta maquina o sentido D3D12 -> D3D11 falha com 0x80070057 (o proprio
    // dlss5-feed registra o mesmo erro no log e faz a mesma inversao). Criar do lado
    // D3D11 e abrir no D3D12 funciona; o Resolve escreve nela por la.
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = m_mvW; td.Height = m_mvH; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16_SINT;
        td.SampleDesc = { 1, 0 };
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
        hr = m_dev11->CreateTexture2D(&td, nullptr, &m_mvSint11);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("criar textura resolvida", hr); }

        IDXGIResource1* dr = nullptr;
        hr = m_mvSint11->QueryInterface(__uuidof(IDXGIResource1), (void**)&dr);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("IDXGIResource1 da resolvida", hr); }
        hr = dr->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                    nullptr, &m_sharedMv);
        Rel(dr);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("CreateSharedHandle da resolvida", hr); }

        hr = m_dev12->OpenSharedHandle(m_sharedMv, __uuidof(ID3D12Resource), (void**)&m_mvSint);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("OpenSharedHandle da resolvida no D3D12", hr); }

        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R16G16_SINT;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        hr = m_dev11->CreateShaderResourceView(m_mvSint11, &sd, &m_mvSintSRV);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("SRV da resolvida", hr); }
    }

    // --- saida RGBA16F: .xy delta UV, .z validade (pura D3D11; e o que o ReShade recebe).
    {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = m_mvW; td.Height = m_mvH; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc = { 1, 0 };
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hr = m_dev11->CreateTexture2D(&td, nullptr, &m_mvFloat);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("criar RGBA16F de saida", hr); }
        hr = m_dev11->CreateRenderTargetView(m_mvFloat, nullptr, &m_mvFloatRTV);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("RTV de saida", hr); }
        hr = m_dev11->CreateShaderResourceView(m_mvFloat, nullptr, &m_mvFloatSRV);
        if (FAILED(hr)) { Rel(dev11_1); return Fail("SRV de saida", hr); }
    }

    // --- fences compartilhadas nos dois sentidos.
    hr = m_dev12->CreateSharedHandle(m_fence12, nullptr, GENERIC_ALL, nullptr, &m_sharedFence12);
    if (FAILED(hr)) { Rel(dev11_1); return Fail("compartilhar fence D3D12", hr); }
    hr = m_dev11_5->OpenSharedFence(m_sharedFence12, __uuidof(ID3D11Fence), (void**)&m_fence12_11);
    if (FAILED(hr)) { Rel(dev11_1); return Fail("abrir fence D3D12 no D3D11", hr); }

    hr = m_dev11_5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void**)&m_fence11);
    if (FAILED(hr)) { Rel(dev11_1); return Fail("criar fence D3D11", hr); }
    hr = m_fence11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &m_sharedFence11);
    if (FAILED(hr)) { Rel(dev11_1); return Fail("compartilhar fence D3D11", hr); }
    hr = m_dev12->OpenSharedHandle(m_sharedFence11, __uuidof(ID3D12Fence), (void**)&m_fence11_12);
    if (FAILED(hr)) { Rel(dev11_1); return Fail("abrir fence D3D11 no D3D12", hr); }

    Rel(dev11_1);

    // Croma neutro uma unica vez: o VME casa por luma, e croma constante nao altera o SAD.
    {
        ID3D11DeviceContext* ctx = nullptr;
        m_dev11->GetImmediateContext(&ctx);
        for (int i = 0; i < 2; ++i)
        {
            D3D11_RENDER_TARGET_VIEW_DESC rtvd{};
            rtvd.Format = DXGI_FORMAT_R8G8_UNORM;
            rtvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            ID3D11RenderTargetView* uvRTV = nullptr;
            if (SUCCEEDED(m_dev11->CreateRenderTargetView(m_nv12[i], &rtvd, &uvRTV)))
            {
                const float grey[4] = { 128.0f / 255.0f, 128.0f / 255.0f, 0.0f, 0.0f };
                ctx->ClearRenderTargetView(uvRTV, grey);
                Rel(uvRTV);
            }
        }
        Rel(ctx);
    }
    return true;
}

inline bool MotionEstimator::InitShaders()
{
    // D3DCompile por GetProcAddress, e nao por link: e o mesmo padrao que o
    // Renderer::InitRest do app usa. Linkar criaria dependencia dura de
    // d3dcompiler_47.dll no carregamento do exe -- uma DLL faltando deixaria o app
    // inteiro sem abrir por causa de um recurso opcional.
    typedef HRESULT (WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR,
        const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT,
        ID3DBlob **, ID3DBlob **);
    static PFN_D3DCompile s_compile = nullptr;
    if (!s_compile)
    {
        HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
        if (dc) s_compile = reinterpret_cast<PFN_D3DCompile>(
            reinterpret_cast<void *>(GetProcAddress(dc, "D3DCompile")));
    }
    if (!s_compile) return Fail("d3dcompiler_47.dll nao encontrado", E_NOINTERFACE);

    auto Compile = [&](const char* src, size_t len, const char* target, ID3DBlob** out) -> bool {
        ID3DBlob* err = nullptr;
        HRESULT hr = s_compile(src, len, nullptr, nullptr, nullptr, "main", target,
                               D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
        if (FAILED(hr))
        {
            std::snprintf(m_err, sizeof(m_err), "D3DCompile(%s): %s", target,
                          err ? (const char*)err->GetBufferPointer() : "?");
            Rel(err);
            return false;
        }
        Rel(err);
        return true;
    };

    ID3DBlob* blob = nullptr;
    if (!Compile(kME_VS, sizeof(kME_VS) - 1, "vs_5_0", &blob)) return false;
    HRESULT hr = m_dev11->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_vs);
    Rel(blob);
    if (FAILED(hr)) return Fail("CreateVertexShader", hr);

    if (!Compile(kME_PS_LUMA, sizeof(kME_PS_LUMA) - 1, "ps_5_0", &blob)) return false;
    hr = m_dev11->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_psLuma);
    Rel(blob);
    if (FAILED(hr)) return Fail("CreatePixelShader(luma)", hr);

    if (!Compile(kME_PS_CONVERT, sizeof(kME_PS_CONVERT) - 1, "ps_5_0", &blob)) return false;
    hr = m_dev11->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_psConvert);
    Rel(blob);
    if (FAILED(hr)) return Fail("CreatePixelShader(convert)", hr);

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    hr = m_dev11->CreateSamplerState(&sd, &m_sampler);
    if (FAILED(hr)) return Fail("CreateSamplerState", hr);

    // ESTADOS PROPRIOS, E POR QUE ELES EXISTEM (VME-FLUXO, 20/09/2026).
    // O Update roda no contexto IMEDIATO do app, no meio do laco, depois da cadeia do
    // ReShade e dos passes do apresentador. Ate hoje ele so trocava VS/PS/RTV/viewport e
    // herdava TODO o resto do que estivesse ligado: rasterizador (tesoura!), mistura,
    // profundidade e os estagios de geometria. Basta um desses sobrar ligado para o
    // triangulo de tela cheia da conversao de luma nao escrever nada -- e uma NV12 que
    // nunca muda faz o VME devolver, corretamente, campo ZERO. Foi exatamente o que a
    // bancada mediu: o me_test isolado le 7,998 px para 8 px reais com 100% dos blocos,
    // e o MESMO codigo dentro do app lia "campo zerado 100%" com o fundo inteiro andando.
    // Estado herdado nao se conserta com validacao do lado de fora; conserta-se aqui.
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE; rd.ScissorEnable = FALSE;
    hr = m_dev11->CreateRasterizerState(&rd, &m_rs);
    if (FAILED(hr)) return Fail("CreateRasterizerState", hr);

    D3D11_BLEND_DESC bld{};
    bld.RenderTarget[0].BlendEnable = FALSE;
    bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = m_dev11->CreateBlendState(&bld, &m_bs);
    if (FAILED(hr)) return Fail("CreateBlendState", hr);

    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = FALSE; dsd.StencilEnable = FALSE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    hr = m_dev11->CreateDepthStencilState(&dsd, &m_dss);
    if (FAILED(hr)) return Fail("CreateDepthStencilState", hr);

    struct CB { float invW, invH, pxScale, rejectPx; };
    // quarter-pel na resolucao de conversao -> pixels de saida.
    CB cb{ 1.0f / float(m_srcW), 1.0f / float(m_srcH),
           0.25f * float(m_divisor), m_rejectPx };
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(CB);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA init{ &cb, 0, 0 };
    hr = m_dev11->CreateBuffer(&bd, &init, &m_cb);
    if (FAILED(hr)) return Fail("constant buffer", hr);
    return true;
}

inline bool MotionEstimator::Update(ID3D11DeviceContext* ctx, ID3D11Texture2D* frameBGRA)
{
    if (!m_ready || !ctx || !frameBGRA) return false;
    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = m_dev11->CreateShaderResourceView(frameBGRA, nullptr, &srv);
    if (FAILED(hr)) return Fail("SRV do quadro", hr);
    const bool ok = Update(ctx, srv);
    Rel(srv);
    return ok;
}

inline bool MotionEstimator::Update(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* srcSRV)
{
    if (!m_ready || !ctx || !srcSRV) return false;

    ID3D11DeviceContext4* ctx4 = nullptr;
    if (FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&ctx4)))
        return Fail("ID3D11DeviceContext4", E_NOINTERFACE);

    // Tudo que o triangulo de tela cheia precisa e que o app pode ter deixado ligado de
    // outro jeito. Ver o comentario em InitShaders: sem isto o passe de luma pode nao
    // escrever nada e o campo sai zerado sem nenhum erro aparecer.
    auto EstadoNosso = [&] {
        const float bf[4] = { 0, 0, 0, 0 };
        ctx->RSSetState(m_rs);
        ctx->OMSetBlendState(m_bs, bf, 0xFFFFFFFFu);
        ctx->OMSetDepthStencilState(m_dss, 0);
        ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->HSSetShader(nullptr, nullptr, 0);
        ctx->DSSetShader(nullptr, nullptr, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->IASetInputLayout(nullptr);
        ID3D11Buffer* vb = nullptr; UINT z = 0;
        ctx->IASetVertexBuffers(0, 1, &vb, &z, &z);
        ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    };

    // --- 1) D3D11: quadro BGRA -> plano Y da NV12 atual (com reescala se divisor > 1).
    {
        D3D11_VIEWPORT vp{ 0.0f, 0.0f, float(m_convW), float(m_convH), 0.0f, 1.0f };
        ctx->OMSetRenderTargets(1, &m_nv12YRTV[m_cur], nullptr);
        ctx->RSSetViewports(1, &vp);
        EstadoNosso();
        ctx->VSSetShader(m_vs, nullptr, 0);
        ctx->PSSetShader(m_psLuma, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &srcSRV);
        ctx->PSSetSamplers(0, 1, &m_sampler);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx->PSSetShaderResources(0, 1, &nullSRV);
        ID3D11RenderTargetView* nullRTV = nullptr;
        ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    // Primeiro quadro: so temos referencia a partir do segundo.
    if (!m_havePrev)
    {
        m_havePrev = true;
        m_cur ^= 1;
        const float zero[4] = { 0, 0, 0, 0 };
        ctx->ClearRenderTargetView(m_mvFloatRTV, zero);
        Rel(ctx4);
        return true;
    }

    // --- 2) D3D11 sinaliza, D3D12 espera: a NV12 atual esta pronta.
    ++m_fv;
    ctx4->Signal(m_fence11, m_fv);
    ctx->Flush();
    m_qVideo->Wait(m_fence11_12, m_fv);

    // --- 3) fila de video: EstimateMotion + Resolve.
    m_alloc->Reset();
    m_clVideo->Reset(m_alloc);

    auto Barrier = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
        D3D12_RESOURCE_BARRIER br{};
        br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        br.Transition.pResource = r;
        br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        br.Transition.StateBefore = a;
        br.Transition.StateAfter = b;
        m_clVideo->ResourceBarrier(1, &br);
    };

    const UINT prev = m_cur ^ 1;
    Barrier(m_nv12_12[m_cur], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);
    Barrier(m_nv12_12[prev], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);
    Barrier(m_mvSint, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE);

    D3D12_VIDEO_MOTION_ESTIMATOR_OUTPUT out{};
    out.pMotionVectorHeap = m_heap;
    D3D12_VIDEO_MOTION_ESTIMATOR_INPUT in{};
    in.pInputTexture2D = m_nv12_12[m_cur];        // quadro ATUAL
    in.InputSubresourceIndex = 0;
    in.pReferenceTexture2D = m_nv12_12[prev];     // quadro ANTERIOR
    in.ReferenceSubresourceIndex = 0;
    in.pHintMotionVectorHeap = nullptr;
    m_clVideo->EstimateMotion(m_est, &out, &in);

    D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_INPUT rin{};
    rin.pMotionVectorHeap = m_heap;
    rin.PixelWidth = m_convW;
    rin.PixelHeight = m_convH;
    D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_OUTPUT rout{};
    rout.pMotionVectorTexture2D = m_mvSint;
    rout.MotionVectorCoordinate = { 0, 0, 0, 0 };
    m_clVideo->ResolveMotionVectorHeap(&rout, &rin);

    Barrier(m_mvSint, D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE, D3D12_RESOURCE_STATE_COMMON);
    Barrier(m_nv12_12[prev], D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ, D3D12_RESOURCE_STATE_COMMON);
    Barrier(m_nv12_12[m_cur], D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ, D3D12_RESOURCE_STATE_COMMON);
    m_clVideo->Close();

    ID3D12CommandList* lists[] = { m_clVideo };
    m_qVideo->ExecuteCommandLists(1, lists);
    m_qVideo->Signal(m_fence12, m_fv);

    // --- 4) D3D11 espera o video e converte quarter-pel -> delta UV.
    ctx4->Wait(m_fence12_11, m_fv);
    {
        D3D11_VIEWPORT vp{ 0.0f, 0.0f, float(m_mvW), float(m_mvH), 0.0f, 1.0f };
        ctx->OMSetRenderTargets(1, &m_mvFloatRTV, nullptr);
        ctx->RSSetViewports(1, &vp);
        EstadoNosso();
        ctx->VSSetShader(m_vs, nullptr, 0);
        ctx->PSSetShader(m_psConvert, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &m_mvSintSRV);
        ctx->PSSetConstantBuffers(0, 1, &m_cb);
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx->PSSetShaderResources(0, 1, &nullSRV);
        ID3D11RenderTargetView* nullRTV = nullptr;
        ctx->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    Rel(ctx4);
    m_cur ^= 1;
    return true;
}

inline bool MotionEstimator::ProbeSample(ID3D11DeviceContext* ctx, unsigned window,
                                         ProbeStats* out)
{
    if (!m_ready || !ctx || !m_mvFloat || window == 0) return false;

    // Duas staging alternadas. Copiamos para a de agora e lemos a da amostra ANTERIOR,
    // que ficou dezenas de quadros para tras e ha muito terminou -- assim o Map nao
    // trava a fila. Com uma so, cada amostra custaria um flush do pipeline.
    for (int i = 0; i < 2; ++i)
    {
        if (m_probeStage[i]) continue;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = m_mvW; td.Height = m_mvH; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc = { 1, 0 };
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_dev11->CreateTexture2D(&td, nullptr, &m_probeStage[i]))) return false;
    }

    // half -> float. O 2^-24 pertence SO ao ramo dos subnormais; aplica-lo tambem no
    // ramo normal (que ja tem o 2^(e-25) abaixo) foi o defeito que fez esta sonda
    // reportar "mean 0.000 px, 99% nao-zero, 100% descartado" no run 13:13, no mesmo
    // quadro em que o dlss5-feed lia 7,996 px.
    auto Half = [](uint16_t h) -> float {
        const uint32_t s = (h >> 15) & 1u, e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
        if (e == 0) return (m == 0) ? (s ? -0.0f : 0.0f)
                                    : (s ? -1.0f : 1.0f) * float(m) * 5.9604645e-8f;
        if (e == 31) return s ? -3.4e38f : 3.4e38f;
        float v = float(m | 0x400u);
        int sh = int(e) - 25;
        while (sh > 0) { v *= 2.0f; --sh; }
        while (sh < 0) { v *= 0.5f; ++sh; }
        return s ? -v : v;
    };

    // 1) le a amostra anterior, se houver
    if (m_probePending)
    {
        ID3D11Texture2D* old = m_probeStage[m_probeSlot ^ 1];
        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(ctx->Map(old, 0, D3D11_MAP_READ, 0, &map)))
        {
            double sum = 0.0; float mx = 0.0f; unsigned nz = 0, rej = 0, n = 0;
            for (UINT y = 0; y < m_mvH; ++y)
            {
                const uint16_t* row = (const uint16_t*)((const uint8_t*)map.pData
                                                        + (size_t)y * map.RowPitch);
                for (UINT x = 0; x < m_mvW; ++x)
                {
                    const float px = Half(row[x * 4 + 0]) * float(m_srcW);
                    const float py = Half(row[x * 4 + 1]) * float(m_srcH);
                    const float valid = Half(row[x * 4 + 2]);
                    const float mag = std::sqrt(px * px + py * py);
                    sum += mag;
                    if (mag > mx) mx = mag;
                    if (mag > 0.0f) ++nz;
                    if (valid < 0.5f) ++rej;
                    ++n;
                }
            }
            ctx->Unmap(old, 0);
            if (n)
            {
                const float mean = float(sum / n);
                m_probeMeans.push_back(mean);
                m_probeSumMean += mean;
                m_probeSumNz   += 100.0 * nz / n;
                m_probeSumRej  += 100.0 * rej / n;
                if (mx > m_probeMax) m_probeMax = mx;
                if (nz == 0) ++m_probeZeroFields;   // campo inteiro zerado
            }
        }
    }

    // 2) agenda a amostra de agora
    ctx->CopyResource(m_probeStage[m_probeSlot], m_mvFloat);
    m_probePending = true;
    m_probeSlot ^= 1;

    // 3) janela fechou?
    if (m_probeMeans.size() < window) return false;

    const unsigned n = (unsigned)m_probeMeans.size();
    std::vector<float> sorted = m_probeMeans;
    std::sort(sorted.begin(), sorted.end());

    if (out)
    {
        out->meanPx       = float(m_probeSumMean / n);
        out->medianPx     = sorted[n / 2];
        out->maxPx        = m_probeMax;
        out->pctNonZero   = float(m_probeSumNz / n);
        out->pctRejected  = float(m_probeSumRej / n);
        out->pctFieldZero = 100.0f * float(m_probeZeroFields) / float(n);
        out->samples      = n;
    }

    m_probeMeans.clear();
    m_probeSumMean = m_probeSumNz = m_probeSumRej = 0.0;
    m_probeMax = 0.0f;
    m_probeZeroFields = 0;
    return true;
}

inline const char* MotionEstimator::EstadoHerdado(ID3D11DeviceContext* ctx)
{
    static char txt[320];
    if (!ctx) { std::snprintf(txt, sizeof(txt), "sem contexto"); return txt; }
    char rsTxt[96] = "rasterizador padrao";
    ID3D11RasterizerState* rs = nullptr; ctx->RSGetState(&rs);
    if (rs) {
        D3D11_RASTERIZER_DESC d{}; rs->GetDesc(&d);
        std::snprintf(rsTxt, sizeof(rsTxt), "rasterizador tesoura=%d corte-de-face=%u preenchimento=%u",
                      d.ScissorEnable ? 1 : 0, (unsigned)d.CullMode, (unsigned)d.FillMode);
        rs->Release();
    }
    char bsTxt[110] = "mistura padrao";
    ID3D11BlendState* bs = nullptr; float bf[4]{}; UINT sm = 0;
    ctx->OMGetBlendState(&bs, bf, &sm);
    if (bs) {
        D3D11_BLEND_DESC d{}; bs->GetDesc(&d);
        std::snprintf(bsTxt, sizeof(bsTxt), "mistura ligada=%d mascara-de-escrita=0x%X amostra=0x%X",
                      d.RenderTarget[0].BlendEnable ? 1 : 0,
                      (unsigned)d.RenderTarget[0].RenderTargetWriteMask, sm);
        bs->Release();
    } else if (sm != 0xFFFFFFFFu) {
        std::snprintf(bsTxt, sizeof(bsTxt), "mistura padrao, amostra=0x%X", sm);
    }
    ID3D11GeometryShader* gs = nullptr; ctx->GSGetShader(&gs, nullptr, nullptr);
    ID3D11HullShader* hs = nullptr; ctx->HSGetShader(&hs, nullptr, nullptr);
    D3D11_RECT sc[8]{}; UINT nsc = 8; ctx->RSGetScissorRects(&nsc, sc);
    std::snprintf(txt, sizeof(txt), "%s | %s | geometria=%s tesselacao=%s | tesouras=%u (1a %ld,%ld,%ld,%ld)",
                  rsTxt, bsTxt, gs ? "SIM" : "nao", hs ? "SIM" : "nao", nsc,
                  nsc ? (long)sc[0].left : 0L, nsc ? (long)sc[0].top : 0L,
                  nsc ? (long)sc[0].right : 0L, nsc ? (long)sc[0].bottom : 0L);
    if (gs) gs->Release();
    if (hs) hs->Release();
    return txt;
}

inline float MotionEstimator::EntradaDiferenca(ID3D11DeviceContext* ctx)
{
    if (!m_ready || !ctx || !m_havePrev) return -1.0f;
    for (int i = 0; i < 2; ++i) {
        if (m_yStage[i]) continue;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = m_convW; td.Height = m_convH; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_NV12; td.SampleDesc = { 1, 0 };
        td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(m_dev11->CreateTexture2D(&td, nullptr, &m_yStage[i]))) return -1.0f;
    }
    // m_cur ja virou no fim do Update: o par comparado pelo VME e (m_cur ^ 1, m_cur).
    const UINT b = m_cur ^ 1, a = m_cur;
    ctx->CopyResource(m_yStage[0], m_nv12[a]);
    ctx->CopyResource(m_yStage[1], m_nv12[b]);
    D3D11_MAPPED_SUBRESOURCE m0{}, m1{};
    if (FAILED(ctx->Map(m_yStage[0], 0, D3D11_MAP_READ, 0, &m0))) return -1.0f;
    if (FAILED(ctx->Map(m_yStage[1], 0, D3D11_MAP_READ, 0, &m1))) { ctx->Unmap(m_yStage[0], 0); return -1.0f; }
    unsigned dif = 0, n = 0;
    for (UINT y = 0; y < m_convH; y += 4) {
        const uint8_t* r0 = (const uint8_t*)m0.pData + (size_t)y * m0.RowPitch;
        const uint8_t* r1 = (const uint8_t*)m1.pData + (size_t)y * m1.RowPitch;
        for (UINT x = 0; x < m_convW; x += 4) {
            const int d = int(r0[x]) - int(r1[x]);
            if (d > 2 || d < -2) ++dif;
            ++n;
        }
    }
    ctx->Unmap(m_yStage[1], 0); ctx->Unmap(m_yStage[0], 0);
    return n ? float(dif) / float(n) : -1.0f;
}

inline void MotionEstimator::Shutdown()
{
    if (m_qVideo && m_fence12)
    {
        ++m_fv;
        m_qVideo->Signal(m_fence12, m_fv);
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (ev)
        {
            if (m_fence12->GetCompletedValue() < m_fv)
            {
                m_fence12->SetEventOnCompletion(m_fv, ev);
                WaitForSingleObject(ev, Saida::Teto(2000));
            }
            CloseHandle(ev);
        }
    }

    for (int i = 0; i < 2; ++i) Rel(m_probeStage[i]);
    m_probeMeans.clear();
    m_probePending = false; m_probeSlot = 0;
    m_probeSumMean = m_probeSumNz = m_probeSumRej = 0.0;
    m_probeMax = 0.0f; m_probeZeroFields = 0;

    Rel(m_mvFloatSRV); Rel(m_mvFloatRTV); Rel(m_mvFloat);
    Rel(m_mvSintSRV);  Rel(m_mvSint11);
    for (int i = 0; i < 2; ++i) { Rel(m_nv12YRTV[i]); Rel(m_nv12[i]); Rel(m_nv12_12[i]); }
    Rel(m_mvSint);
    for (int i = 0; i < 2; ++i) Rel(m_yStage[i]);
    Rel(m_dss); Rel(m_bs); Rel(m_rs);
    Rel(m_cb); Rel(m_sampler); Rel(m_psConvert); Rel(m_psLuma); Rel(m_vs);
    Rel(m_fence11_12); Rel(m_fence12_11); Rel(m_fence11); Rel(m_fence12);
    Rel(m_clVideo); Rel(m_alloc); Rel(m_qVideo);
    Rel(m_heap); Rel(m_est); Rel(m_vdev); Rel(m_dev12);
    Rel(m_dev11_5); Rel(m_dev11);

    for (int i = 0; i < 2; ++i) if (m_sharedNv12[i]) { CloseHandle(m_sharedNv12[i]); m_sharedNv12[i] = nullptr; }
    if (m_sharedMv) { CloseHandle(m_sharedMv); m_sharedMv = nullptr; }
    if (m_sharedFence11) { CloseHandle(m_sharedFence11); m_sharedFence11 = nullptr; }
    if (m_sharedFence12) { CloseHandle(m_sharedFence12); m_sharedFence12 = nullptr; }

    m_ready = false; m_havePrev = false; m_cur = 0; m_fv = 0;
}

} // namespace dlss5
