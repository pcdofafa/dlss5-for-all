/* =====================================================================
   ngx_upstream.cpp -- ver ngx_upstream.h. Agente G1, fase 3, 15/09/2026.

   Estrutura:
     1. estado, arquivo de controle (%TEMP%\dlss5-ponte.<pid>.ngx), pipe;
     2. sessao: texturas compartilhadas no tamanho interno, hello/build;
     3. D3D12 por quadro (copia, prova, troca) e D3D11 por quadro;
     4. vigia (fences por CPU, so para nao travar o jogo);
     5. interface.
   ===================================================================== */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <mutex>

#include "../ponte_comum.h"   /* protocolo v9 (structs e constantes); o Log daqui nao e usado */
#include "ngx_upstream.h"
#include "../fg/fg_hook.h"    /* J3: depth e MV do jogo tambem alimentam o frame generation */
#include "../ponte_dados.h"   /* J5: ... e a ponte da fase 2 (o app, pela pelicula), em D3D11 e D3D12 */
#include "../pipe_up.h"       /* X-FG-6: o pipe .up e um so e passa de mao entre este modulo e o sr_hook */
#include "../dados/colheita.h"   /* X-FG-6: depth+MV para o FG e para a pelicula SEM sessao .up (Nativo comum) */

namespace Ngx { namespace Up {
namespace {

using Ponte::FeedHello; using Ponte::FeedHelloAck; using Ponte::FeedBuild; using Ponte::FeedBuildAck; using Ponte::FeedFrameMsg;
using Ponte::kMagic; using Ponte::kVersao;
using Ponte::SLOT_COLOR; using Ponte::SLOT_OUTPUT; using Ponte::SLOT_DEPTH; using Ponte::SLOT_MV; using Ponte::SLOTS;
using Ponte::ComoValor; using Ponte::ComoHandle; using Ponte::Soltar; using Ponte::FecharHandle;

/* bits novos do fio (proposta G1 -> G2) */
const uint32_t kBuildUpstream = 0x100u;   /* FEED_BUILD_UPSTREAM: sessao "so a rede", tamanho interno */
const uint32_t kAckUpstream   = 0x100u;   /* FEED_ACK_UPSTREAM: o host entende 'X' + FeedExtraMsg */

#pragma pack(push, 1)
struct FeedExtraMsg   /* 'X', 36 bytes: vai ANTES do 'F' do mesmo n */
{
    uint64_t n;
    float jitter_x, jitter_y, mv_scale_x, mv_scale_y, pre_exposure, exposure_scale, frame_dt_ms;
    uint32_t flags;   /* 1 = reset, 2 = exposure texture presente, 4 = sharpen pedido */
};
#pragma pack(pop)
static_assert(sizeof(FeedExtraMsg) == 40, "FeedExtraMsg = 40 bytes");

LogFn L = nullptr;
std::mutex mu;
int64_t qpcFreq = 1;
int64_t Agora() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
double  Ms(int64_t de, int64_t ate) { return 1000.0 * double(ate - de) / double(qpcFreq); }

enum Fase { E_DESLIGADO, E_ESPERANDO, E_CONECTANDO, E_CONECTADO, E_PRONTO, E_FALHOU };
void MedirJanela(int64_t agora);
std::atomic<int> estado{ E_ESPERANDO };
std::atomic<bool> trocando{ false };   /* a troca esta ligada (prova passou) */
char motivoFalha[160] = "";
bool primeiroRetorno = false;   /* o app ja devolveu um quadro desta sessao (vigia) */

/* ------------------------------------------------------------------ */
/* Arquivo de controle                                                 */
/* ------------------------------------------------------------------ */
struct Controle
{
    bool upstream = true;
    bool prova = true;
    unsigned estadoCor = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    unsigned estadoMv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    unsigned estadoDepth = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    wchar_t pipe[128] = L"";
    FILETIME mtime = {};
    bool lido = false;
} ctl;

void CaminhoTemp(wchar_t* out, size_t n, const wchar_t* sufixo)
{
    wchar_t tmp[MAX_PATH] = L"";
    DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (k == 0 || k >= MAX_PATH) { tmp[0] = L'.'; tmp[1] = L'\\'; tmp[2] = 0; }
    _snwprintf(out, n, L"%sdlss5-ponte.%lu.%ls", tmp, GetCurrentProcessId(), sufixo);
}

/* Le %TEMP%\dlss5-ponte.<pid>.ngx se mudou. Linhas chave=valor. */
bool LerControle()
{
    wchar_t caminho[MAX_PATH]; CaminhoTemp(caminho, MAX_PATH, L"ngx");
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    if (!GetFileAttributesExW(caminho, GetFileExInfoStandard, &fa)) { if (!ctl.lido) { ctl.lido = true; } return false; }
    if (ctl.lido && CompareFileTime(&fa.ftLastWriteTime, &ctl.mtime) == 0) return false;
    ctl.mtime = fa.ftLastWriteTime; ctl.lido = true;
    FILE* f = _wfopen(caminho, L"rb");
    if (f == nullptr) return false;
    char linha[256];
    while (fgets(linha, sizeof linha, f))
    {
        char* eq = strchr(linha, '='); if (eq == nullptr) continue;
        *eq = 0; const char* k = linha; char* v = eq + 1;
        char* fim = v + strlen(v); while (fim > v && (fim[-1] == '\r' || fim[-1] == '\n' || fim[-1] == ' ')) *--fim = 0;
        if (!strcmp(k, "upstream")) ctl.upstream = atoi(v) != 0;
        else if (!strcmp(k, "prova")) ctl.prova = atoi(v) != 0;
        else if (!strcmp(k, "estado_cor")) ctl.estadoCor = (unsigned)strtoul(v, nullptr, 0);
        else if (!strcmp(k, "estado_mv")) ctl.estadoMv = (unsigned)strtoul(v, nullptr, 0);
        else if (!strcmp(k, "estado_depth")) ctl.estadoDepth = (unsigned)strtoul(v, nullptr, 0);
        else if (!strcmp(k, "pipe_up")) { for (int i = 0; i < 127; ++i) { ctl.pipe[i] = (wchar_t)(unsigned char)v[i]; if (!v[i]) break; } ctl.pipe[127] = 0; }
    }
    fclose(f);
    L("ngx: controle lido de %ls: upstream=%d prova=%d estado_cor=0x%X pipe_up=%ls", caminho, ctl.upstream ? 1 : 0, ctl.prova ? 1 : 0, ctl.estadoCor, ctl.pipe[0] ? ctl.pipe : L"(padrao)");
    return true;
}

/* ------------------------------------------------------------------ */
/* Pipe: X-FG-6 (18/09/2026) - a conexao mora em pipe_up.cpp e e UMA SO, */
/* compartilhada com o sr_hook (um dono por vez; ver pipe_up.h).         */
/* ------------------------------------------------------------------ */
bool   hostEntendeX = false;
bool Escrever(const void* b, DWORD n) { return Ponte::PipeUp::Escrever(b, n); }
bool LerPipe(void* b, DWORD n)        { return Ponte::PipeUp::Ler(b, n); }

void DestravarFila();

void Falhar(const char* motivo)
{
    if (estado.load() == E_FALHOU) return;
    snprintf(motivoFalha, sizeof motivoFalha, "%s", motivo);
    trocando = false;
    estado = E_FALHOU;
    L("ngx: desligado (%s)", motivo);
    DestravarFila();   /* H2: um Wait(out,n) ja enfileirado na fila do jogo nao pode ficar sem sinal */
}

/* ------------------------------------------------------------------ */
/* Sessao                                                              */
/* ------------------------------------------------------------------ */
struct Sessao
{
    bool d3d12 = true;
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* fila = nullptr;
    ID3D11Device* dev11 = nullptr; ID3D11DeviceContext* ctx11 = nullptr; ID3D11DeviceContext4* ctx4 = nullptr; ID3D11Device5* dev5 = nullptr;

    unsigned w = 0, h = 0;              /* tamanho interno */
    DXGI_FORMAT corFmt = DXGI_FORMAT_UNKNOWN, mvFmtJogo = DXGI_FORMAT_UNKNOWN, depthFmtJogo = DXGI_FORMAT_UNKNOWN;
    unsigned flagsFeature = 0;
    float mvScaleX = 1, mvScaleY = 1;
    bool copiaMv = false, copiaDepth = false;

    /* Q1 (16/09/2026): depth do jogo em formato depth-stencil (D24S8, D32S8, D16) -> R32F por
       compute shader. converteDepth implica copiaDepth. Recursos criados no Build (shader, UAV do
       slot) e no primeiro quadro (a textura intermediaria, no tamanho/formato da depth do jogo). */
    bool converteDepth = false;
    DXGI_FORMAT depthSrvFmt = DXGI_FORMAT_UNKNOWN;      /* a SRV que le so a profundidade (R24_UNORM_X8, R32_FLOAT_X8X24, R16_UNORM) */
    DXGI_FORMAT depthTypeless = DXGI_FORMAT_UNKNOWN;    /* a familia typeless (D3D11: formato da intermediaria) */
    ID3D12Resource* depthInter = nullptr;               /* copia do plano 0 da depth do jogo, no formato dele */
    ID3D12RootSignature* depthRS = nullptr; ID3D12PipelineState* depthPSO = nullptr; ID3D12DescriptorHeap* depthHeap = nullptr;
    UINT depthIncr = 0;
    ID3D11Texture2D* depthInter11 = nullptr; ID3D11ShaderResourceView* depthSrv11 = nullptr; ID3D11UnorderedAccessView* depthUav11 = nullptr;
    ID3D11ComputeShader* depthCS11 = nullptr; ID3D11Buffer* depthCB11 = nullptr;
    unsigned depthConvLogadas = 0;

    /* D3D12 */
    ID3D12Resource* tex[SLOTS] = {};
    HANDLE htex[SLOTS] = {};
    ID3D12Fence* fIn = nullptr, *fOut = nullptr, *fRecicla = nullptr;
    HANDLE evRecicla = nullptr;
    uint64_t reciclaVal = 0, fenceDoBuf[3] = {};
    ID3D12CommandAllocator* alloc[3] = {};
    ID3D12GraphicsCommandList* lista = nullptr;
    /* prova de prontidao */
    ID3D12Resource* provaA = nullptr, *provaB = nullptr;   /* A: nossa lista; B: dentro da lista do jogo */
    ID3D12Resource* rbA = nullptr, *rbB = nullptr;         /* readback */
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {}; UINT64 rbBytes = 0;
    ID3D12Fence* fProva = nullptr; HANDLE evProva = nullptr; uint64_t provaVal = 0;
    unsigned provaQuadros = 0, provaIguais = 0, provaTotal = 0; bool provaPendente = false, provaFeita = false;

    /* D3D11 */
    ID3D11Texture2D* tex11[SLOTS] = {};
    ID3D11Fence* fIn11 = nullptr, *fOut11 = nullptr;
    ID3D12Device* devAux = nullptr;       /* so para o vigia sinalizar a fence de saida por CPU (ID3D11Fence nao tem Signal) */
    ID3D12Fence*  fOutAux = nullptr;

    uint64_t n = 0;
    /* troca */
    ID3D12Resource* corOriginal = nullptr; ID3D11Resource* corOriginal11 = nullptr;
    unsigned baseXOrig = 0, baseYOrig = 0; bool trocou = false;
    /* X-FG-6: a troca de upscaler ao vivo. `handleAtual` e a feature que a sessao serve; o jogo a
       soltando (ReleaseFeature) liga `featureSolta`. `qpcUltimo` e o relogio do ultimo evaluate
       servido. O Servico larga o pipe para o sr_hook quando ele pede E (a feature foi solta OU
       nao ha evaluate ha 1 s). `avaliacoesSRNaSoltura`: a sessao seguinte so nasce de um
       evaluate NOVO (a sonda continua dizendo viuSR). */
    const Handle* handleAtual = nullptr;
    std::atomic<bool> featureSolta{ false };
    std::atomic<int64_t> qpcUltimo{ 0 };
    unsigned avaliacoesSRNaSoltura = 0;
} S;

/* tempos (janela de 300) */
struct Janela { unsigned n = 0; double copia = 0, espera = 0, troca = 0; unsigned atrasados = 0; int64_t inicio = 0; } jan;
std::atomic<unsigned> atrasadosTotal{ 0 };

/* vigia */
HANDLE evVigia = nullptr, evVigiaIn = nullptr, evVigiaOut = nullptr, thVigia = nullptr;
std::atomic<uint64_t> vigiaN{ 0 };
std::atomic<bool> vigiaSai{ false };
std::atomic<double> esperaMedidaMs{ 0 };

bool FormatoCorAceito(DXGI_FORMAT f) { return f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_R10G10B10A2_UNORM || f == DXGI_FORMAT_R16G16B16A16_FLOAT; }
bool DepthCopiavel(DXGI_FORMAT f) { return f == DXGI_FORMAT_R32_FLOAT || f == DXGI_FORMAT_D32_FLOAT || f == DXGI_FORMAT_R32_TYPELESS; }

/* ------------------------------------------------------------------ */
/* Q1 (16/09/2026): depth depth-stencil -> R32_FLOAT por shader          */
/* ------------------------------------------------------------------ */
/* O host so entende R32_FLOAT na Depth, e CopyTextureRegion nao converte bits: a D24S8 do RDR
   (e a D32S8/D16 de outros motores) ia ZERADA. Aqui: (1) copia do plano 0 da depth do jogo para
   uma textura NOSSA no mesmo formato -- copia identica, sem duvida de compatibilidade, e a nossa
   nunca tem DENY_SHADER_RESOURCE; (2) SRV tipada da familia (R24_UNORM_X8 le so a profundidade);
   (3) um compute shader escreve o R32F no slot, aplicando o subrect (Depth.Subrect.Base). Falha
   em qualquer ponto = depth zerada, como antes; o log diz. */
bool DepthConversivel(DXGI_FORMAT f, DXGI_FORMAT* srv, DXGI_FORMAT* typeless)
{
    switch (f)
    {
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        *srv = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; *typeless = DXGI_FORMAT_R24G8_TYPELESS; return true;
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        *srv = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; *typeless = DXGI_FORMAT_R32G8X24_TYPELESS; return true;
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM:
        *srv = DXGI_FORMAT_R16_UNORM; *typeless = DXGI_FORMAT_R16_TYPELESS; return true;
    default: return false;
    }
}

/* cs_5_0 nos dois lados; a root signature embutida so no D3D12 (RS12). */
const char kDepthHlsl[] =
    "#define RS \"RootFlags(0), RootConstants(num32BitConstants=4, b0), DescriptorTable(SRV(t0, numDescriptors=1)), DescriptorTable(UAV(u0, numDescriptors=1))\"\n"
    "Texture2D<float> src : register(t0);\n"
    "RWTexture2D<float> dst : register(u0);\n"
    "cbuffer C : register(b0) { uint bx, by, w, h; };\n"
    "#ifdef RS12\n[RootSignature(RS)]\n#endif\n"
    "[numthreads(8,8,1)]\n"
    "void cs(uint3 id : SV_DispatchThreadID) { if (id.x >= w || id.y >= h) return; dst[id.xy] = src.Load(int3(bx + id.x, by + id.y, 0)); }\n";

unsigned avisos = 0;
void AvisoUmaVez(const char* m) { if (avisos < 12) { ++avisos; L("ngx: upstream: %s", m); } }

bool CompilarDepthCS(bool d3d12, ID3DBlob** cs)
{
    ID3DBlob* err = nullptr;
    const D3D_SHADER_MACRO defs[] = { { "RS12", "1" }, { nullptr, nullptr } };
    HRESULT hr = D3DCompile(kDepthHlsl, sizeof(kDepthHlsl) - 1, "depth", d3d12 ? defs : nullptr, nullptr, "cs", "cs_5_0", 0, 0, cs, &err);
    if (FAILED(hr)) { L("ngx: upstream: shader de conversao da depth nao compilou 0x%08X: %s", (unsigned)hr, err ? (const char*)err->GetBufferPointer() : ""); Soltar(err); return false; }
    Soltar(err);
    return true;
}

/* D3D12: root signature, PSO, heap (SRV no 0, UAV do slot no 1). Depois de S.tex[SLOT_DEPTH]. */
bool DepthConv12Criar()
{
    ID3DBlob* cs = nullptr, *rs = nullptr;
    if (!CompilarDepthCS(true, &cs)) return false;
    HRESULT hr = D3DGetBlobPart(cs->GetBufferPointer(), cs->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &rs);
    if (SUCCEEDED(hr)) hr = S.dev->CreateRootSignature(0, rs->GetBufferPointer(), rs->GetBufferSize(), __uuidof(ID3D12RootSignature), reinterpret_cast<void**>(&S.depthRS));
    Soltar(rs);
    if (SUCCEEDED(hr)) { D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = S.depthRS; pd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() }; hr = S.dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), reinterpret_cast<void**>(&S.depthPSO)); }
    Soltar(cs);
    if (SUCCEEDED(hr)) { D3D12_DESCRIPTOR_HEAP_DESC hd = {}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 2; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; hr = S.dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void**>(&S.depthHeap)); }
    if (FAILED(hr)) { L("ngx: upstream: conversao da depth (D3D12) nao montou 0x%08X; depth zerada", (unsigned)hr); return false; }
    S.depthIncr = S.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R32_FLOAT; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE h = S.depthHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += S.depthIncr;
    S.dev->CreateUnorderedAccessView(S.tex[SLOT_DEPTH], nullptr, &ud, h);
    return true;
}

/* D3D12: a intermediaria no tamanho/formato da depth do jogo (refeita se ele mudar). */
bool DepthInter12(ID3D12Resource* depth)
{
    D3D12_RESOURCE_DESC dd = depth->GetDesc();
    if (S.depthInter) { D3D12_RESOURCE_DESC id = S.depthInter->GetDesc(); if (id.Width == dd.Width && id.Height == dd.Height && id.Format == dd.Format) return true; Soltar(S.depthInter); }
    if (dd.SampleDesc.Count != 1 || dd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) { AvisoUmaVez("depth do jogo e MSAA ou nao e 2D: conversao impossivel, depth zerada"); return false; }
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = dd.Width; rd.Height = dd.Height; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = dd.Format; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    HRESULT hr = S.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&S.depthInter));
    if (FAILED(hr)) { char m[160]; snprintf(m, sizeof m, "intermediaria da depth (fmt %u %ux%u) falhou 0x%08X; depth zerada", (unsigned)dd.Format, (unsigned)dd.Width, dd.Height, (unsigned)hr); AvisoUmaVez(m); return false; }
    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = S.depthSrvFmt; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sd.Texture2D.MipLevels = 1;
    S.dev->CreateShaderResourceView(S.depthInter, &sd, S.depthHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

/* D3D11: shader, constante e UAV do slot. Depois de S.tex11[SLOT_DEPTH]. */
bool DepthConv11Criar()
{
    ID3DBlob* cs = nullptr;
    if (!CompilarDepthCS(false, &cs)) return false;
    HRESULT hr = S.dev11->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &S.depthCS11);
    Soltar(cs);
    if (SUCCEEDED(hr)) { D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; hr = S.dev11->CreateBuffer(&bd, nullptr, &S.depthCB11); }
    if (SUCCEEDED(hr)) { D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R32_FLOAT; ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D; hr = S.dev11->CreateUnorderedAccessView(S.tex11[SLOT_DEPTH], &ud, &S.depthUav11); }
    if (FAILED(hr)) { L("ngx: upstream: conversao da depth (D3D11) nao montou 0x%08X; depth zerada", (unsigned)hr); return false; }
    return true;
}

/* D3D11: a intermediaria typeless da familia, no tamanho da depth do jogo (a copia de um
   depth-stencil tem de ser do subrecurso inteiro: pSrcBox nulo, destino em 0,0). */
bool DepthInter11(ID3D11Resource* depth)
{
    ID3D11Texture2D* t = nullptr;
    if (FAILED(depth->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t))) || t == nullptr) return false;
    D3D11_TEXTURE2D_DESC dd; t->GetDesc(&dd); t->Release();
    if (S.depthInter11) { D3D11_TEXTURE2D_DESC id; S.depthInter11->GetDesc(&id); if (id.Width == dd.Width && id.Height == dd.Height) return true; Soltar(S.depthSrv11); Soltar(S.depthInter11); }
    if (dd.SampleDesc.Count != 1) { AvisoUmaVez("depth do jogo e MSAA (D3D11): conversao impossivel, depth zerada"); return false; }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = dd.Width; td.Height = dd.Height; td.MipLevels = 1; td.ArraySize = 1; td.Format = S.depthTypeless; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
    HRESULT hr = S.dev11->CreateTexture2D(&td, nullptr, &S.depthInter11);
    if (SUCCEEDED(hr)) { D3D11_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = S.depthSrvFmt; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1; hr = S.dev11->CreateShaderResourceView(S.depthInter11, &sd, &S.depthSrv11); }
    if (FAILED(hr)) { char m[160]; snprintf(m, sizeof m, "intermediaria da depth D3D11 (fmt %u %ux%u) falhou 0x%08X; depth zerada", (unsigned)dd.Format, dd.Width, dd.Height, (unsigned)hr); AvisoUmaVez(m); Soltar(S.depthInter11); return false; }
    return true;
}

ID3D12Resource* Criar12(UINT w, UINT h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, HANDLE* hOut)
{
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; rd.Flags = flags;
    ID3D12Resource* r = nullptr;
    HRESULT hr = S.dev->CreateCommittedResource(&hp, hOut ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&r));
    if (FAILED(hr)) { L("ngx: upstream: CreateCommittedResource %ux%u fmt=%u falhou 0x%08X", w, h, (unsigned)fmt, (unsigned)hr); return nullptr; }
    if (hOut && FAILED(hr = S.dev->CreateSharedHandle(r, nullptr, GENERIC_ALL, nullptr, hOut))) { L("ngx: upstream: CreateSharedHandle falhou 0x%08X", (unsigned)hr); r->Release(); return nullptr; }
    return r;
}

ID3D11Texture2D* Criar11(UINT w, UINT h, DXGI_FORMAT fmt, UINT bind, HANDLE* hOut)
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = bind; td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    ID3D11Texture2D* t = nullptr;
    HRESULT hr = S.dev11->CreateTexture2D(&td, nullptr, &t);
    if (FAILED(hr)) { L("ngx: upstream: D3D11 CreateTexture2D %ux%u fmt=%u falhou 0x%08X", w, h, (unsigned)fmt, (unsigned)hr); return nullptr; }
    IDXGIResource1* r = nullptr;
    hr = t->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(&r));
    if (SUCCEEDED(hr)) { hr = r->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, hOut); r->Release(); }
    if (FAILED(hr)) { L("ngx: upstream: D3D11 CreateSharedHandle falhou 0x%08X", (unsigned)hr); t->Release(); return nullptr; }
    return t;
}

void LiberarTexturas()
{
    Fg::LimparDados();   /* J3: o FG larga a referencia dele antes de os slots sumirem */
    Ponte::Dados::Limpar();   /* J5: a ponte da fase 2 idem */
    for (int i = 0; i < SLOTS; ++i) { Soltar(S.tex[i]); Soltar(S.tex11[i]); FecharHandle(S.htex[i]); }
    Soltar(S.provaA); Soltar(S.provaB); Soltar(S.rbA); Soltar(S.rbB);
    Soltar(S.fIn); Soltar(S.fOut); Soltar(S.fIn11); Soltar(S.fOut11); Soltar(S.fOutAux);
    /* Q1: conversao da depth */
    Soltar(S.depthInter); Soltar(S.depthRS); Soltar(S.depthPSO); Soltar(S.depthHeap);
    Soltar(S.depthSrv11); Soltar(S.depthUav11); Soltar(S.depthInter11); Soltar(S.depthCS11); Soltar(S.depthCB11);
    S.depthConvLogadas = 0;
}

/* X-FG-6: toma o pipe compartilhado. *ocupado = o sr_hook esta com ele (pedido registrado; volta a
   tentar a cada volta do Servico, sem os 2 s). false sem motivo = o app nao serve o .up. */
bool Conectar(bool* ocupado)
{
    char motivo[160] = "";
    if (Ponte::PipeUp::Tomar(Ponte::PipeUp::DONO_NGX, ctl.pipe, ocupado, motivo, sizeof motivo)) { L("ngx: upstream: com o pipe .up (host pid=%lu)", Ponte::PipeUp::HostPid()); return true; }
    if (motivo[0]) { char m[200]; snprintf(m, sizeof m, "upstream: %s", motivo); Falhar(m); }
    return false;
}

bool Build()
{
    const D3D12_RESOURCE_FLAGS kAbrivel = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    FeedBuild b = {};
    if (S.d3d12)
    {
        S.tex[SLOT_COLOR]  = Criar12(S.w, S.h, S.corFmt, kAbrivel, &S.htex[SLOT_COLOR]);
        S.tex[SLOT_OUTPUT] = Criar12(S.w, S.h, S.corFmt, kAbrivel | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &S.htex[SLOT_OUTPUT]);
        /* Q1: com conversao, o slot da depth e escrito por UAV (RT|SIM|UAV abre no D3D11 do app, medido pelo B5) */
        S.tex[SLOT_DEPTH]  = Criar12(S.w, S.h, DXGI_FORMAT_R32_FLOAT, S.converteDepth ? (kAbrivel | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) : kAbrivel, &S.htex[SLOT_DEPTH]);
        S.tex[SLOT_MV]     = Criar12(S.w, S.h, DXGI_FORMAT_R16G16_FLOAT, kAbrivel, &S.htex[SLOT_MV]);
        for (int i = 0; i < SLOTS; ++i) if (S.tex[i] == nullptr) { Falhar("upstream: criar texturas D3D12 falhou"); return false; }
        if (S.converteDepth && !DepthConv12Criar()) { S.converteDepth = false; S.copiaDepth = false; }
        if (S.lista == nullptr)
        {
            for (int i = 0; i < 3; ++i)
                if (FAILED(S.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&S.alloc[i])))) { Falhar("upstream: CreateCommandAllocator"); return false; }
            if (FAILED(S.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, S.alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&S.lista)))) { Falhar("upstream: CreateCommandList"); return false; }
            S.lista->Close();
            if (FAILED(S.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&S.fRecicla)))) { Falhar("upstream: fence de reciclagem"); return false; }
            S.evRecicla = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (FAILED(S.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&S.fProva)))) { Falhar("upstream: fence da prova"); return false; }
            S.evProva = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        }
        /* prova de prontidao: duas copias da Color + readback */
        if (ctl.prova)
        {
            S.provaA = Criar12(S.w, S.h, S.corFmt, D3D12_RESOURCE_FLAG_NONE, nullptr);
            S.provaB = Criar12(S.w, S.h, S.corFmt, D3D12_RESOURCE_FLAG_NONE, nullptr);
            D3D12_RESOURCE_DESC rd = S.provaA ? S.provaA->GetDesc() : D3D12_RESOURCE_DESC{};
            UINT linhas = 0; UINT64 pitchBytes = 0;
            S.dev->GetCopyableFootprints(&rd, 0, 1, 0, &S.fp, &linhas, &pitchBytes, &S.rbBytes);
            D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC bd = {}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = S.rbBytes; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (S.provaA == nullptr || S.provaB == nullptr ||
                FAILED(S.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&S.rbA))) ||
                FAILED(S.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&S.rbB))))
            { L("ngx: upstream: sem recursos para a prova de prontidao; seguindo SEM prova (troca so por controle prova=0)"); Soltar(S.provaA); Soltar(S.provaB); Soltar(S.rbA); Soltar(S.rbB); }
        }
    }
    else
    {
        S.tex11[SLOT_COLOR]  = Criar11(S.w, S.h, S.corFmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &S.htex[SLOT_COLOR]);
        S.tex11[SLOT_OUTPUT] = Criar11(S.w, S.h, S.corFmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, &S.htex[SLOT_OUTPUT]);
        S.tex11[SLOT_DEPTH]  = Criar11(S.w, S.h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | (S.converteDepth ? D3D11_BIND_UNORDERED_ACCESS : 0), &S.htex[SLOT_DEPTH]);
        S.tex11[SLOT_MV]     = Criar11(S.w, S.h, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &S.htex[SLOT_MV]);
        for (int i = 0; i < SLOTS; ++i) if (S.tex11[i] == nullptr) { Falhar("upstream: criar texturas D3D11 falhou"); return false; }
        if (S.converteDepth && !DepthConv11Criar()) { S.converteDepth = false; S.copiaDepth = false; }
    }
    b.width = S.w; b.height = S.h; b.color_fmt = S.corFmt; b.output_fmt = S.corFmt;
    b.hdr = (S.flagsFeature & CF_HDR) ? 1 : 0;
    b.depth_inverted = (S.flagsFeature & CF_DEPTH_INVERTED) ? 1 : 0;
    b.flags_override = -1;
    b.mv_scale_x = S.mvScaleX; b.mv_scale_y = S.mvScaleY;
    for (int i = 0; i < SLOTS; ++i) b.tex[i] = ComoValor(S.htex[i]);
    b.client_flags = kBuildUpstream;   /* sem ASYNC_HOME: sincrono */

    const BYTE tag = 'B'; FeedBuildAck ack = {};
    { std::lock_guard<std::mutex> l(Ponte::PipeUp::Mutex()); if (!Escrever(&tag, 1) || !Escrever(&b, sizeof b) || !LerPipe(&ack, sizeof ack)) { Falhar("upstream: troca do Build falhou"); return false; } }
    if (ack.ok != 1) { Falhar("upstream: host recusou o Build"); return false; }
    hostEntendeX = (ack.flags & kAckUpstream) != 0;
    if (ack.output_fmt != 0 && (DXGI_FORMAT)ack.output_fmt != S.corFmt) L("ngx: upstream: host ecoou output=%u (Output criado em %u); seguindo com o criado", ack.output_fmt, (unsigned)S.corFmt);

    HRESULT h1, h2;
    if (S.d3d12)
    {
        h1 = S.dev->OpenSharedHandle(ComoHandle(ack.fence_in), __uuidof(ID3D12Fence), reinterpret_cast<void**>(&S.fIn));
        h2 = S.dev->OpenSharedHandle(ComoHandle(ack.fence_out), __uuidof(ID3D12Fence), reinterpret_cast<void**>(&S.fOut));
    }
    else
    {
        if (S.dev5 == nullptr && FAILED(S.dev11->QueryInterface(__uuidof(ID3D11Device5), reinterpret_cast<void**>(&S.dev5)))) { Falhar("upstream: sem ID3D11Device5"); return false; }
        h1 = S.dev5->OpenSharedFence(ComoHandle(ack.fence_in), __uuidof(ID3D11Fence), reinterpret_cast<void**>(&S.fIn11));
        h2 = S.dev5->OpenSharedFence(ComoHandle(ack.fence_out), __uuidof(ID3D11Fence), reinterpret_cast<void**>(&S.fOut11));
        /* a mesma fence de saida aberta num D3D12 auxiliar: e a unica forma de a sinalizar por CPU */
        if (S.devAux == nullptr) D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&S.devAux));
        if (S.devAux && FAILED(S.devAux->OpenSharedHandle(ComoHandle(ack.fence_out), __uuidof(ID3D12Fence), reinterpret_cast<void**>(&S.fOutAux))))
        { S.fOutAux = nullptr; L("ngx: upstream: sem fence auxiliar D3D12; o vigia nao podera destravar um atraso em D3D11"); }
    }
    if (FAILED(h1) || FAILED(h2)) { Falhar("upstream: abrir as fences do host falhou"); return false; }

    char depthTxt[96];
    if (S.converteDepth) snprintf(depthTxt, sizeof depthTxt, "convertida por shader (fmt %u depth-stencil -> R32F)", (unsigned)S.depthFmtJogo);
    else if (S.copiaDepth) snprintf(depthTxt, sizeof depthTxt, "copiada");
    else snprintf(depthTxt, sizeof depthTxt, "ZERADA (fmt %u: fora da familia R32 e sem conversao)", (unsigned)S.depthFmtJogo);
    L("ngx: upstream ligado interno=%ux%u cor=%u mv=%s depth=%s hdr=%d depth_inv=%d mvscale=%.3f,%.3f api=%s host_entende_X=%s prova=%s",
      S.w, S.h, (unsigned)S.corFmt, S.copiaMv ? "copiada" : "ZERADA (formato do jogo nao e R16G16_FLOAT ou MV em resolucao de saida)",
      depthTxt, b.hdr, b.depth_inverted, S.mvScaleX, S.mvScaleY,
      S.d3d12 ? "D3D12" : "D3D11", hostEntendeX ? "sim" : "nao", (S.d3d12 && S.provaA) ? "8 quadros antes de trocar" : (S.d3d12 ? "SEM prova" : "nao precisa (D3D11)"));
    S.provaQuadros = 0; S.provaIguais = 0; S.provaTotal = 0; S.provaFeita = !(S.d3d12 && S.provaA);
    trocando = S.provaFeita;   /* D3D11 (ou sem prova): troca desde o primeiro quadro */
    primeiroRetorno = false;   /* X-FG-6: sessao nova (rebuild ou volta do pipe): o app leva ~1-2 s no primeiro quadro; o vigia nao conta */
    return true;
}

/* ------------------------------------------------------------------ */
/* Vigia: mede in->out por quadro e destrava a fila se o app atrasar    */
/* ------------------------------------------------------------------ */

/* H2 (16/09): a fila do jogo espera a fence de saida (Wait(out,n)) e so o app a sinaliza. Se o
   app cai, o pipe fecha ou a ponte encerra ANTES de o app sinalizar o ultimo n, a fila do jogo
   fica travada para sempre, o driver da TDR e o device e removido (0x887A0005) -- foi o que a
   transicao upstream -> fase 2 no mesmo processo mostrou. Esta funcao sinaliza por CPU a fence
   de saida ate o ultimo n enfileirado, em qualquer estado, antes de qualquer liberacao. */
void DestravarFila()
{
    const uint64_t n = S.n;
    if (n == 0) return;
    if (S.d3d12) { if (S.fOut && S.fOut->GetCompletedValue() < n) S.fOut->Signal(n); }
    else if (S.fOutAux && S.fOutAux->GetCompletedValue() < n) S.fOutAux->Signal(n);
}

/* C3: nenhuma excecao sai da thread (std::terminate); destrava a fila e volta ao laco */
DWORD VigiaLaco();
DWORD WINAPI Vigia(void*)
{
    for (;;)
    {
        try { return VigiaLaco(); }
        catch (...) { DestravarFila(); Sleep(100); }
    }
}

DWORD VigiaLaco()
{
    uint64_t feito = 0;
    while (!vigiaSai.load())
    {
        if (WaitForSingleObject(evVigia, 100) != WAIT_OBJECT_0) continue;
        const uint64_t n = vigiaN.load();
        if (n <= feito) continue;
        feito = n;
        /* fora de PRONTO (falhou/encerrando) nao ha app para esperar: destrava e segue */
        if (estado.load() != E_PRONTO) { DestravarFila(); continue; }
        /* espera a entrada ficar pronta na GPU (a nossa copia executou) */
        bool inOk = false;
        if (S.d3d12 && S.fIn) { if (S.fIn->GetCompletedValue() >= n) inOk = true; else if (SUCCEEDED(S.fIn->SetEventOnCompletion(n, evVigiaIn))) inOk = WaitForSingleObject(evVigiaIn, 1000) == WAIT_OBJECT_0; }
        else if (!S.d3d12 && S.fIn11) { if (S.fIn11->GetCompletedValue() >= n) inOk = true; else if (SUCCEEDED(S.fIn11->SetEventOnCompletion(n, evVigiaIn))) inOk = WaitForSingleObject(evVigiaIn, 1000) == WAIT_OBJECT_0; }
        if (!inOk) continue;
        const int64_t t0 = Agora();
        /* Sem troca ligada (prova em curso) a fila nao espera o app: aqui so medimos. Ate o
           primeiro retorno do app (ele ainda cria a feature: ~1,3 s no host de referencia) o
           limite e de 3 s e nada e contado; depois, 50 ms e contagem. */
        const bool conta = trocando.load() && primeiroRetorno;
        const DWORD limite = primeiroRetorno ? 50 : 3000;
        bool outOk = false;
        if (S.d3d12 && S.fOut) { if (S.fOut->GetCompletedValue() >= n) outOk = true; else if (SUCCEEDED(S.fOut->SetEventOnCompletion(n, evVigiaOut))) outOk = WaitForSingleObject(evVigiaOut, limite) == WAIT_OBJECT_0; }
        else if (!S.d3d12 && S.fOut11) { if (S.fOut11->GetCompletedValue() >= n) outOk = true; else if (SUCCEEDED(S.fOut11->SetEventOnCompletion(n, evVigiaOut))) outOk = WaitForSingleObject(evVigiaOut, limite) == WAIT_OBJECT_0; }
        esperaMedidaMs = Ms(t0, Agora());
        if (outOk && !primeiroRetorno) { primeiroRetorno = true; L("ngx: upstream: primeiro quadro devolvido pelo app (n=%llu) em %.1f ms", (unsigned long long)n, esperaMedidaMs.load()); }
        if (!outOk && !conta)
        {
            /* destrava sem contar (prova em curso ou aquecimento) */
            if (S.d3d12 && S.fOut) S.fOut->Signal(n); else if (S.fOutAux) S.fOutAux->Signal(n);
        }
        else if (!outOk)
        {
            /* destrava a fila do jogo: sinaliza a fence de saida por CPU (o Output deste quadro
               e o que estiver la; o app foi avisado pelo contrato de copiar Color->Output se atrasar) */
            if (S.d3d12 && S.fOut) S.fOut->Signal(n); else if (S.fOutAux) S.fOutAux->Signal(n);
            const unsigned a = ++atrasadosTotal;
            L("ngx: upstream: app nao devolveu o quadro %llu em 50 ms (atraso %u); fence de saida sinalizada por CPU", (unsigned long long)n, a);
            if (a >= 5) Falhar("app nao devolveu a tempo 5 vezes");
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* D3D12 por quadro                                                    */
/* ------------------------------------------------------------------ */
D3D12_RESOURCE_BARRIER Barr(ID3D12Resource* r, D3D12_RESOURCE_STATES de, D3D12_RESOURCE_STATES para)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = de; b.Transition.StateAfter = para; return b;
}
void CopiarRegiao(ID3D12GraphicsCommandList* l, ID3D12Resource* dst, ID3D12Resource* src, unsigned bx, unsigned by, unsigned w, unsigned h)
{
    D3D12_TEXTURE_COPY_LOCATION d = {}; d.pResource = dst; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION s = {}; s.pResource = src; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.SubresourceIndex = 0;
    D3D12_BOX box = { bx, by, 0, bx + w, by + h, 1 };
    l->CopyTextureRegion(&d, 0, 0, 0, &s, &box);
}

/* J5 (18/09/2026): o mesmo aviso do J3, para a PONTE DA FASE 2 (o app, pela pelicula), nos dois
   backends. O upstream so copia MV em resolucao de render (S.w x S.h), sem NDC. Quem copia para os
   slots da fase 2 e o Present da ponte (ponte_cliente.cpp), no mesmo contexto/fila destas copias. */
void PublicarParaPonte(float jx, float jy, float dt, bool reset)
{
    if (!Ponte::Dados::Quer()) return;
    Ponte::Dados::Pacote p;
    if (S.d3d12) { if (!S.tex[SLOT_DEPTH] || !S.tex[SLOT_MV] || !S.fila) return; p.depth12 = S.tex[SLOT_DEPTH]; p.mv12 = S.tex[SLOT_MV]; p.fila = S.fila; }
    else         { if (!S.tex11[SLOT_DEPTH] || !S.tex11[SLOT_MV]) return; p.depth11 = S.tex11[SLOT_DEPTH]; p.mv11 = S.tex11[SLOT_MV]; }
    p.renderW = S.w; p.renderH = S.h; p.mvW = S.w; p.mvH = S.h;
    p.jitterX = jx; p.jitterY = jy; p.mvScaleX = S.mvScaleX; p.mvScaleY = S.mvScaleY; p.dtMs = dt;
    p.depthInv = (S.flagsFeature & CF_DEPTH_INVERTED) ? 1 : 0;
    p.mvDisplayRes = false; p.mvJittered = (S.flagsFeature & CF_MV_JITTERED) != 0; p.mvNdc = false;
    p.reset = reset;
    p.origem = 1u;   /* dlss */
    Ponte::Dados::Publicar(p);
}

/* X-FG-6: SEM a sessao .up (Upstream desligado no modo Nativo, app sem servir o .up, sessao caida
   ou passada ao sr_hook), depth e MV do DLSS vao mesmo assim para o FG da DLL e para a pelicula -
   pela colheita (dados/colheita.h): as copias vao NA LISTA DO JOGO (ou no contexto imediato)
   antes do evaluate, e a Color nao e trocada. Flags e escala de MV vem da sonda (ja viu esta
   feature no evaluate anterior); renderSize e subrects, do parametro deste evaluate. */
void ColherSemSessao(ID3D12GraphicsCommandList* lista, ID3D11DeviceContext* ctx, const Parameter* pc)
{
    if (!Ponte::Colheita::AlguemQuer()) return;
    const bool d3d12 = lista != nullptr;
    if (d3d12 && S.fila == nullptr) return;   /* a fila do evaluate ainda nao foi vista (FilaVista) */
    if (!d3d12 && ctx == nullptr) return;
    Parameter* p = const_cast<Parameter*>(pc);
    Estado e = Ler();
    if (!e.viuSR) return;
    Ponte::Colheita::Entrada c;
    c.d3d12 = d3d12; c.lista = lista; c.fila = S.fila; c.ctx = ctx;
    ID3D12Resource* mv12 = nullptr; ID3D12Resource* dp12 = nullptr; ID3D11Resource* mv11 = nullptr; ID3D11Resource* dp11 = nullptr;
    if (d3d12) { if (!LerD3D12(p, kMotionVectors, &mv12) || !LerD3D12(p, kDepth, &dp12) || !mv12 || !dp12) return; c.depth = dp12; c.mv = mv12; }
    else       { if (!LerD3D11(p, kMotionVectors, &mv11) || !LerD3D11(p, kDepth, &dp11) || !mv11 || !dp11) return; c.depth = dp11; c.mv = mv11; }
    c.eDepth = (D3D12_RESOURCE_STATES)ctl.estadoDepth; c.eMv = (D3D12_RESOURCE_STATES)ctl.estadoMv;
    unsigned rw = 0, rh = 0;
    if (!(LerUI(p, kRenderW, &rw) && LerUI(p, kRenderH, &rh) && rw && rh)) { LerUI(p, kWidth, &rw); LerUI(p, kHeight, &rh); }
    if (rw == 0 || rh == 0) return;
    c.rw = rw; c.rh = rh;
    c.outW = e.saidaW; c.outH = e.saidaH;
    LerUI(p, kMvBaseX, &c.bxMv); LerUI(p, kMvBaseY, &c.byMv); LerUI(p, kDepthBaseX, &c.bxDepth); LerUI(p, kDepthBaseY, &c.byDepth);
    c.mvDisplayRes = (e.flags & CF_MV_LOW_RES) == 0 && e.mvW >= e.saidaW && e.mvH >= e.saidaH && (e.saidaW > rw || e.saidaH > rh);
    c.mvJittered = (e.flags & CF_MV_JITTERED) != 0; c.mvNdc = false;
    c.depthInv = (e.flags & CF_DEPTH_INVERTED) ? 1 : 0;
    float jx = 0, jy = 0, dt = 0; int reset = 0;
    LerF(p, kJitterX, &jx); LerF(p, kJitterY, &jy); LerF(p, kFrameTimeDelta, &dt); LerI(p, kReset, &reset);
    c.jx = jx; c.jy = jy; c.dtMs = dt; c.reset = reset != 0;
    float sx = e.mvScaleX, sy = e.mvScaleY;
    LerF(p, kMvScaleX, &sx); LerF(p, kMvScaleY, &sy);
    c.msx = sx != 0.f ? sx : 1.f; c.msy = sy != 0.f ? sy : 1.f;
    c.origem = 1u; c.origemTxt = "dlss";
    Ponte::Colheita::Publicar(c);
}

bool AntesD12Impl(ID3D12GraphicsCommandList* lista, const Handle* h, const Parameter* pc)
{
    (void)h;
    if (estado.load() != E_PRONTO) { ColherSemSessao(lista, nullptr, pc); return false; }   /* X-FG-6 */
    if (!S.d3d12 || S.fila == nullptr || lista == nullptr) return false;
    Parameter* p = const_cast<Parameter*>(pc);
    ID3D12Resource* cor = nullptr, *mv = nullptr, *depth = nullptr;
    if (!LerD3D12(p, kColor, &cor)) return false;
    LerD3D12(p, kMotionVectors, &mv); LerD3D12(p, kDepth, &depth);
    unsigned rw = 0, rh = 0, bx = 0, by = 0, mbx = 0, mby = 0, dbx = 0, dby = 0;
    if (!(LerUI(p, kRenderW, &rw) && LerUI(p, kRenderH, &rh) && rw && rh)) { LerUI(p, kWidth, &rw); LerUI(p, kHeight, &rh); }
    LerUI(p, kColorBaseX, &bx); LerUI(p, kColorBaseY, &by); LerUI(p, kMvBaseX, &mbx); LerUI(p, kMvBaseY, &mby); LerUI(p, kDepthBaseX, &dbx); LerUI(p, kDepthBaseY, &dby);
    if (rw != S.w || rh != S.h) { AvisoUmaVez("tamanho interno mudou (nova resolucao ou resolucao dinamica); este quadro passa direto e a sessao sera refeita"); estado = E_CONECTADO; return false; }
    D3D12_RESOURCE_DESC cd = cor->GetDesc();
    if (cd.Format != S.corFmt || cd.Width < bx + rw || cd.Height < by + rh) { AvisoUmaVez("Color do jogo com formato/tamanho diferente da sessao; passa direto"); return false; }

    const int64_t t0 = Agora();
    S.handleAtual = h; S.featureSolta = false; S.qpcUltimo = t0;   /* X-FG-6: esta feature esta viva e servida */
    const uint64_t n = S.n = Ponte::PipeUp::ProximoN();   /* X-FG-6: contador unico das duas sessoes */
    const unsigned idx = (unsigned)(n % 3);
    /* recicla o allocator deste slot (espera de GPU curta; nunca mais que ~1 quadro) */
    if (S.fenceDoBuf[idx] != 0 && S.fRecicla->GetCompletedValue() < S.fenceDoBuf[idx])
    { if (SUCCEEDED(S.fRecicla->SetEventOnCompletion(S.fenceDoBuf[idx], S.evRecicla))) WaitForSingleObject(S.evRecicla, 1000); }
    if (FAILED(S.alloc[idx]->Reset()) || FAILED(S.lista->Reset(S.alloc[idx], nullptr))) { Falhar("upstream: Reset da lista de copia falhou"); return false; }

    const D3D12_RESOURCE_STATES eCor = (D3D12_RESOURCE_STATES)ctl.estadoCor, eMv = (D3D12_RESOURCE_STATES)ctl.estadoMv, eDp = (D3D12_RESOURCE_STATES)ctl.estadoDepth;
    const bool provaAgora = trocando.load() == false && S.provaA != nullptr && !S.provaFeita;
    /* --- nossa lista: Color (e MV, Depth) do jogo -> slots --- */
    D3D12_RESOURCE_BARRIER b1[4]; UINT nb = 0;
    b1[nb++] = Barr(cor, eCor, D3D12_RESOURCE_STATE_COPY_SOURCE);
    b1[nb++] = Barr(S.tex[SLOT_COLOR], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    if (S.copiaMv && mv) b1[nb++] = Barr(mv, eMv, D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (S.copiaDepth && depth) b1[nb++] = Barr(depth, eDp, D3D12_RESOURCE_STATE_COPY_SOURCE);
    S.lista->ResourceBarrier(nb, b1);
    CopiarRegiao(S.lista, S.tex[SLOT_COLOR], cor, bx, by, rw, rh);
    if (provaAgora)
    {
        D3D12_RESOURCE_BARRIER pa = Barr(S.provaA, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        S.lista->ResourceBarrier(1, &pa);
        CopiarRegiao(S.lista, S.provaA, cor, bx, by, rw, rh);
        D3D12_RESOURCE_BARRIER pa2 = Barr(S.provaA, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        S.lista->ResourceBarrier(1, &pa2);
    }
    if (S.copiaMv && mv) { D3D12_RESOURCE_BARRIER t = Barr(S.tex[SLOT_MV], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST); S.lista->ResourceBarrier(1, &t); CopiarRegiao(S.lista, S.tex[SLOT_MV], mv, mbx, mby, rw, rh); D3D12_RESOURCE_BARRIER u = Barr(S.tex[SLOT_MV], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON); S.lista->ResourceBarrier(1, &u); }
    if (S.copiaDepth && depth && S.converteDepth)
    {
        /* Q1: plano 0 da depth do jogo -> intermediaria (mesmo formato) -> compute -> slot R32F.
           A depth do jogo ja esta em COPY_SOURCE (b1). */
        if (S.depthPSO && DepthInter12(depth))
        {
            D3D12_RESOURCE_BARRIER t1[2] = { Barr(S.depthInter, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST), Barr(S.tex[SLOT_DEPTH], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) };
            S.lista->ResourceBarrier(2, t1);
            D3D12_TEXTURE_COPY_LOCATION d = {}; d.pResource = S.depthInter; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION s = {}; s.pResource = depth; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.SubresourceIndex = 0;
            S.lista->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
            D3D12_RESOURCE_BARRIER t2 = Barr(S.depthInter, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            S.lista->ResourceBarrier(1, &t2);
            ID3D12DescriptorHeap* heaps[1] = { S.depthHeap };
            S.lista->SetDescriptorHeaps(1, heaps);
            S.lista->SetComputeRootSignature(S.depthRS);
            S.lista->SetPipelineState(S.depthPSO);
            const unsigned c[4] = { dbx, dby, rw, rh };
            S.lista->SetComputeRoot32BitConstants(0, 4, c, 0);
            D3D12_GPU_DESCRIPTOR_HANDLE g0 = S.depthHeap->GetGPUDescriptorHandleForHeapStart(), g1 = g0; g1.ptr += S.depthIncr;
            S.lista->SetComputeRootDescriptorTable(1, g0);
            S.lista->SetComputeRootDescriptorTable(2, g1);
            S.lista->Dispatch((rw + 7) / 8, (rh + 7) / 8, 1);
            D3D12_RESOURCE_BARRIER t3[2] = { Barr(S.depthInter, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON), Barr(S.tex[SLOT_DEPTH], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON) };
            S.lista->ResourceBarrier(2, t3);
            if (S.depthConvLogadas++ == 0) { D3D12_RESOURCE_DESC dd = depth->GetDesc(); L("ngx: depth convertida fmt=%u -> R32F (D3D12: %ux%u de %ux%u, subrect %u,%u, srv fmt=%u)", (unsigned)dd.Format, rw, rh, (unsigned)dd.Width, dd.Height, dbx, dby, (unsigned)S.depthSrvFmt); }
        }
    }
    else if (S.copiaDepth && depth) { D3D12_RESOURCE_BARRIER t = Barr(S.tex[SLOT_DEPTH], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST); S.lista->ResourceBarrier(1, &t); CopiarRegiao(S.lista, S.tex[SLOT_DEPTH], depth, dbx, dby, rw, rh); D3D12_RESOURCE_BARRIER u = Barr(S.tex[SLOT_DEPTH], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON); S.lista->ResourceBarrier(1, &u); }
    /* readback das duas copias do quadro ANTERIOR (a de dentro da lista do jogo ja executou) */
    if (S.provaPendente && S.provaA && S.rbA)
    {
        D3D12_TEXTURE_COPY_LOCATION sa = {}; sa.pResource = S.provaA; sa.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION sb = {}; sb.pResource = S.provaB; sb.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION da = {}; da.pResource = S.rbA; da.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; da.PlacedFootprint = S.fp;
        D3D12_TEXTURE_COPY_LOCATION db = {}; db.pResource = S.rbB; db.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; db.PlacedFootprint = S.fp;
        S.lista->CopyTextureRegion(&da, 0, 0, 0, &sa, nullptr);
        S.lista->CopyTextureRegion(&db, 0, 0, 0, &sb, nullptr);
    }
    D3D12_RESOURCE_BARRIER b2[4]; nb = 0;
    b2[nb++] = Barr(cor, D3D12_RESOURCE_STATE_COPY_SOURCE, eCor);
    b2[nb++] = Barr(S.tex[SLOT_COLOR], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    if (S.copiaMv && mv) b2[nb++] = Barr(mv, D3D12_RESOURCE_STATE_COPY_SOURCE, eMv);
    if (S.copiaDepth && depth) b2[nb++] = Barr(depth, D3D12_RESOURCE_STATE_COPY_SOURCE, eDp);
    S.lista->ResourceBarrier(nb, b2);
    if (FAILED(S.lista->Close())) { Falhar("upstream: Close da lista de copia falhou"); return false; }
    ID3D12CommandList* ls[1] = { S.lista };
    S.fila->ExecuteCommandLists(1, ls);
    if (S.provaPendente) { S.fila->Signal(S.fProva, ++S.provaVal); S.provaPendente = false; }
    S.fila->Signal(S.fIn, n);
    S.fila->Signal(S.fRecicla, ++S.reciclaVal); S.fenceDoBuf[idx] = S.reciclaVal;
    const int64_t t1 = Agora();

    /* --- dentro da lista do jogo (prova): segunda copia da Color, antes do evaluate --- */
    if (provaAgora && S.provaB)
    {
        D3D12_RESOURCE_BARRIER pb[2] = { Barr(cor, eCor, D3D12_RESOURCE_STATE_COPY_SOURCE), Barr(S.provaB, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST) };
        lista->ResourceBarrier(2, pb);
        CopiarRegiao(lista, S.provaB, cor, bx, by, rw, rh);
        D3D12_RESOURCE_BARRIER pb2[2] = { Barr(cor, D3D12_RESOURCE_STATE_COPY_SOURCE, eCor), Barr(S.provaB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON) };
        lista->ResourceBarrier(2, pb2);
        S.provaPendente = true; ++S.provaQuadros;
    }

    /* --- manda o quadro: 'X' (se o host entende) e 'F' --- */
    float jx = 0, jy = 0, dt = 0, pre = 1, esc = 1; int reset = 0; ID3D12Resource* expo = nullptr; unsigned flags = 0;
    LerF(p, kJitterX, &jx); LerF(p, kJitterY, &jy); LerF(p, kFrameTimeDelta, &dt); LerF(p, kPreExposure, &pre); LerF(p, kExposureScale, &esc); LerI(p, kReset, &reset);
    if (LerD3D12(p, kExposureTexture, &expo)) flags |= 2;
    if (reset) flags |= 1;
    {
        std::lock_guard<std::mutex> l(Ponte::PipeUp::Mutex());
        if (hostEntendeX) { const BYTE tx = 'X'; FeedExtraMsg x = { n, jx, jy, S.mvScaleX, S.mvScaleY, pre, esc, dt, flags }; if (!Escrever(&tx, 1) || !Escrever(&x, sizeof x)) { Falhar("upstream: pipe fechado"); return false; } }
        const BYTE tf = 'F'; FeedFrameMsg fm = { n, (uint32_t)(reset ? 1 : 0), jx, jy };
        if (!Escrever(&tf, 1) || !Escrever(&fm, sizeof fm)) { Falhar("upstream: pipe fechado"); return false; }
    }
    vigiaN = n; SetEvent(evVigia);

    /* J3 (18/09/2026): as mesmas copias servem ao frame generation da DLL, que roda no MESMO
       processo, device e fila. Nada de novo por quadro: os slots ja foram escritos e submetidos
       acima; aqui so se diz onde estao e com que convencao. O DLSS entrega MV em pixels da
       resolucao de render depois de MV.Scale.X/Y (que e o que vai em mvScaleX/Y), +x para a
       direita, +y para baixo, do quadro atual para o anterior -- a mesma convencao que o
       ffxDispatchDescFrameGenerationPrepare pede depois de motionVectorScale. O NGX nao carrega
       near/far/fov, entao a camera fica com a suposicao do fg_hook. */
    if (Fg::QuerDados() && S.copiaMv && mv && S.copiaDepth && depth && S.tex[SLOT_DEPTH] && S.tex[SLOT_MV] && S.fila)
    {
        Fg::DadosJogo fp;
        fp.depth = S.tex[SLOT_DEPTH]; fp.mv = S.tex[SLOT_MV]; fp.fila = S.fila;
        fp.renderW = S.w; fp.renderH = S.h; fp.mvW = S.w; fp.mvH = S.h;   /* o upstream so copia MV em resolucao de render */
        fp.jitterX = jx; fp.jitterY = jy; fp.mvScaleX = S.mvScaleX; fp.mvScaleY = S.mvScaleY; fp.dtMs = dt;
        fp.depthInv = (S.flagsFeature & CF_DEPTH_INVERTED) ? 1 : 0;
        fp.mvDisplayRes = false;
        fp.mvJittered = (S.flagsFeature & CF_MV_JITTERED) != 0;
        fp.reset = reset != 0;
        fp.origem = "dlss";
        Fg::PublicarDados(fp);
    }
    if (S.copiaMv && mv && S.copiaDepth && depth) PublicarParaPonte(jx, jy, dt, reset != 0);   /* J5 */

    jan.n++; jan.copia += Ms(t0, t1); jan.espera += esperaMedidaMs.load();
    if (jan.n == 1) jan.inicio = t0;

    /* prova em curso ou troca desligada: o jogo NAO espera o app (nada e trocado); original intocada */
    if (!trocando.load()) { S.trocou = false; MedirJanela(t1); return false; }

    /* --- espera de GPU na fila: a lista do jogo (submetida depois) so roda com o Output pronto --- */
    S.fila->Wait(S.fOut, n);
    const int64_t t2 = Agora();

    /* --- a troca: Color = nosso Output (pequeno), bases zeradas --- */
    S.corOriginal = cor; S.baseXOrig = bx; S.baseYOrig = by;
    PorD3D12(p, kColor, S.tex[SLOT_OUTPUT]);
    PorUI(p, kColorBaseX, 0); PorUI(p, kColorBaseY, 0);
    S.trocou = true;
    jan.troca += Ms(t2, Agora());
    MedirJanela(Agora());
    return true;
}

void MedirJanela(int64_t agora)
{
    if (jan.n < 300) return;
    const double seg = Ms(jan.inicio, agora) / 1000.0;
    L("ngx: 300 avaliacoes | copia-in %.3f ms | espera %.3f ms | troca %.3f ms | %.1f aval/s | atrasados %u | troca=%s",
      jan.copia / jan.n, jan.espera / jan.n, jan.troca / jan.n, seg > 0 ? jan.n / seg : 0.0, atrasadosTotal.load(), trocando.load() ? "ligada" : "desligada");
    jan = Janela();
}

void DepoisD12Impl(const Parameter* pc)
{
    if (!S.trocou) return;
    Parameter* p = const_cast<Parameter*>(pc);
    PorD3D12(p, kColor, S.corOriginal);
    PorUI(p, kColorBaseX, S.baseXOrig); PorUI(p, kColorBaseY, S.baseYOrig);
    S.trocou = false; S.corOriginal = nullptr;
}

/* Compara as duas copias do quadro anterior (thread da ponte). */
void ConferirProva()
{
    if (!S.d3d12 || S.provaA == nullptr || S.provaFeita || S.provaVal == 0) return;
    if (S.fProva->GetCompletedValue() < S.provaVal) return;   /* readback ainda na GPU */
    static uint64_t conferido = 0;
    if (conferido == S.provaVal) return;
    conferido = S.provaVal;
    void* a = nullptr; void* b = nullptr;
    D3D12_RANGE r = { 0, (SIZE_T)S.rbBytes };
    if (FAILED(S.rbA->Map(0, &r, &a)) || FAILED(S.rbB->Map(0, &r, &b))) { if (a) S.rbA->Unmap(0, nullptr); L("ngx: upstream: Map do readback da prova falhou"); return; }
    /* compara linha a linha (pitch alinhado) */
    bool iguais = true; unsigned long long dif = 0;
    const unsigned bpp = (S.corFmt == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8 : 4;
    for (unsigned y = 0; y < S.h && iguais; ++y)
    {
        const BYTE* la = (const BYTE*)a + (size_t)y * S.fp.Footprint.RowPitch;
        const BYTE* lb = (const BYTE*)b + (size_t)y * S.fp.Footprint.RowPitch;
        if (memcmp(la, lb, (size_t)S.w * bpp) != 0) { iguais = false; for (unsigned i = 0; i < S.w * bpp; ++i) if (la[i] != lb[i]) ++dif; }
    }
    D3D12_RANGE nada = { 0, 0 };
    S.rbA->Unmap(0, &nada); S.rbB->Unmap(0, &nada);
    ++S.provaTotal; if (iguais) ++S.provaIguais;
    L("ngx: prova de prontidao quadro %u/8: copia na nossa lista %s a copia dentro da lista do jogo%s", S.provaTotal, iguais ? "IGUAL" : "DIFERENTE de",
      iguais ? "" : " (a cena ainda nao tinha sido submetida quando o evaluate foi gravado)");
    if (S.provaTotal >= 8)
    {
        S.provaFeita = true;
        if (S.provaIguais == S.provaTotal) { trocando = true; L("ngx: prova de prontidao: %u/%u iguais -> sincrono POSSIVEL neste jogo; troca da Color LIGADA", S.provaIguais, S.provaTotal); }
        else Falhar("cena e evaluate na mesma command list: sincrono impossivel (prova de prontidao falhou)");
    }
}

/* ------------------------------------------------------------------ */
/* D3D11 por quadro                                                    */
/* ------------------------------------------------------------------ */
bool AntesD11Impl(ID3D11DeviceContext* ctx, const Handle* h, const Parameter* pc)
{
    (void)h;
    if (estado.load() != E_PRONTO) { ColherSemSessao(nullptr, ctx, pc); return false; }   /* X-FG-6 */
    if (S.d3d12 || ctx == nullptr || S.ctx4 == nullptr) return false;
    Parameter* p = const_cast<Parameter*>(pc);
    ID3D11Resource* cor = nullptr, *mv = nullptr, *depth = nullptr;
    if (!LerD3D11(p, kColor, &cor)) return false;
    LerD3D11(p, kMotionVectors, &mv); LerD3D11(p, kDepth, &depth);
    unsigned rw = 0, rh = 0, bx = 0, by = 0, mbx = 0, mby = 0, dbx = 0, dby = 0;
    if (!(LerUI(p, kRenderW, &rw) && LerUI(p, kRenderH, &rh) && rw && rh)) { LerUI(p, kWidth, &rw); LerUI(p, kHeight, &rh); }
    LerUI(p, kColorBaseX, &bx); LerUI(p, kColorBaseY, &by); LerUI(p, kMvBaseX, &mbx); LerUI(p, kMvBaseY, &mby); LerUI(p, kDepthBaseX, &dbx); LerUI(p, kDepthBaseY, &dby);
    if (rw != S.w || rh != S.h) { AvisoUmaVez("tamanho interno mudou (D3D11); sessao sera refeita"); estado = E_CONECTADO; return false; }
    const int64_t t0 = Agora();
    S.handleAtual = h; S.featureSolta = false; S.qpcUltimo = t0;   /* X-FG-6: esta feature esta viva e servida */
    const uint64_t n = S.n = Ponte::PipeUp::ProximoN();   /* X-FG-6: contador unico das duas sessoes */
    D3D11_BOX box = { bx, by, 0, bx + rw, by + rh, 1 };
    ctx->CopySubresourceRegion(S.tex11[SLOT_COLOR], 0, 0, 0, 0, cor, 0, &box);
    if (S.copiaMv && mv) { D3D11_BOX b = { mbx, mby, 0, mbx + rw, mby + rh, 1 }; ctx->CopySubresourceRegion(S.tex11[SLOT_MV], 0, 0, 0, 0, mv, 0, &b); }
    if (S.copiaDepth && depth && S.converteDepth)
    {
        /* Q1: subrecurso inteiro (regra do D3D11 para depth-stencil) -> intermediaria typeless -> compute -> slot R32F */
        if (S.depthCS11 && DepthInter11(depth))
        {
            ctx->CopySubresourceRegion(S.depthInter11, 0, 0, 0, 0, depth, 0, nullptr);
            const unsigned c[4] = { dbx, dby, rw, rh };
            ctx->UpdateSubresource(S.depthCB11, 0, nullptr, c, 0, 0);
            ctx->CSSetShader(S.depthCS11, nullptr, 0);
            ctx->CSSetConstantBuffers(0, 1, &S.depthCB11);
            ctx->CSSetShaderResources(0, 1, &S.depthSrv11);
            UINT ini = (UINT)-1; ctx->CSSetUnorderedAccessViews(0, 1, &S.depthUav11, &ini);
            ctx->Dispatch((rw + 7) / 8, (rh + 7) / 8, 1);
            ID3D11ShaderResourceView* ns = nullptr; ID3D11UnorderedAccessView* nu = nullptr; ID3D11Buffer* nb = nullptr;
            ctx->CSSetShaderResources(0, 1, &ns); ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr); ctx->CSSetConstantBuffers(0, 1, &nb); ctx->CSSetShader(nullptr, nullptr, 0);
            if (S.depthConvLogadas++ == 0) L("ngx: depth convertida fmt=%u -> R32F (D3D11: %ux%u, subrect %u,%u, srv fmt=%u)", (unsigned)S.depthFmtJogo, rw, rh, dbx, dby, (unsigned)S.depthSrvFmt);
        }
    }
    else if (S.copiaDepth && depth) { D3D11_BOX b = { dbx, dby, 0, dbx + rw, dby + rh, 1 }; ctx->CopySubresourceRegion(S.tex11[SLOT_DEPTH], 0, 0, 0, 0, depth, 0, &b); }
    S.ctx4->Signal(S.fIn11, n);
    ctx->Flush();
    const int64_t t1 = Agora();
    float jx = 0, jy = 0, dt = 0, pre = 1, esc = 1; int reset = 0; ID3D11Resource* expo = nullptr; unsigned flags = 0;
    LerF(p, kJitterX, &jx); LerF(p, kJitterY, &jy); LerF(p, kFrameTimeDelta, &dt); LerF(p, kPreExposure, &pre); LerF(p, kExposureScale, &esc); LerI(p, kReset, &reset);
    if (LerD3D11(p, kExposureTexture, &expo)) flags |= 2;
    if (reset) flags |= 1;
    if (S.copiaMv && mv && S.copiaDepth && depth) PublicarParaPonte(jx, jy, dt, reset != 0);   /* J5: slots gravados no contexto imediato */
    {
        std::lock_guard<std::mutex> l(Ponte::PipeUp::Mutex());
        if (hostEntendeX) { const BYTE tx = 'X'; FeedExtraMsg x = { n, jx, jy, S.mvScaleX, S.mvScaleY, pre, esc, dt, flags }; if (!Escrever(&tx, 1) || !Escrever(&x, sizeof x)) { Falhar("upstream: pipe fechado"); return false; } }
        const BYTE tf = 'F'; FeedFrameMsg fm = { n, (uint32_t)(reset ? 1 : 0), jx, jy };
        if (!Escrever(&tf, 1) || !Escrever(&fm, sizeof fm)) { Falhar("upstream: pipe fechado"); return false; }
    }
    vigiaN = n; SetEvent(evVigia);
    jan.n++; jan.copia += Ms(t0, t1); jan.espera += esperaMedidaMs.load(); if (jan.n == 1) jan.inicio = t0;
    if (!trocando.load()) { S.trocou = false; MedirJanela(t1); return false; }
    S.ctx4->Wait(S.fOut11, n);   /* espera de GPU no contexto imediato */
    const int64_t t2 = Agora();
    S.corOriginal11 = cor; S.baseXOrig = bx; S.baseYOrig = by;
    PorD3D11(p, kColor, S.tex11[SLOT_OUTPUT]);
    PorUI(p, kColorBaseX, 0); PorUI(p, kColorBaseY, 0);
    S.trocou = true;
    jan.troca += Ms(t2, Agora()); MedirJanela(Agora());
    return true;
}

void DepoisD11Impl(const Parameter* pc)
{
    if (!S.trocou) return;
    Parameter* p = const_cast<Parameter*>(pc);
    PorD3D11(p, kColor, S.corOriginal11);
    PorUI(p, kColorBaseX, S.baseXOrig); PorUI(p, kColorBaseY, S.baseYOrig);
    S.trocou = false; S.corOriginal11 = nullptr;
}

/* Captura, do primeiro evaluate de SR com os hooks ativos, o que a sessao precisa. */
bool ColherDaSonda()
{
    Estado e = Ler();
    if (!e.viuSR || e.internoW == 0 || e.internoH == 0) return false;
    S.w = e.internoW; S.h = e.internoH;
    S.corFmt = (DXGI_FORMAT)e.corFmt; S.mvFmtJogo = (DXGI_FORMAT)e.mvFmt; S.depthFmtJogo = (DXGI_FORMAT)e.depthFmt;
    if (!FormatoCorAceito(S.corFmt)) { char m[128]; snprintf(m, sizeof m, "formato da Color do jogo (%u) nao e aceito pelo host (28/87/24/10); conversao fica para a proxima rodada", (unsigned)S.corFmt); Falhar(m); return false; }
    S.copiaMv = (S.mvFmtJogo == DXGI_FORMAT_R16G16_FLOAT) && (e.mvW == S.w && e.mvH == S.h);
    /* Q1: a depth pode ser maior que o interno (subrect); familia R32 copia direto, depth-stencil converte por shader */
    const bool depthCabe = (e.depthW >= S.w && e.depthH >= S.h);
    S.converteDepth = depthCabe && !DepthCopiavel(S.depthFmtJogo) && DepthConversivel(S.depthFmtJogo, &S.depthSrvFmt, &S.depthTypeless);
    S.copiaDepth = (DepthCopiavel(S.depthFmtJogo) && depthCabe) || S.converteDepth;
    S.flagsFeature = e.flags; S.mvScaleX = e.mvScaleX; S.mvScaleY = e.mvScaleY;
    S.d3d12 = (e.api == 12);
    if (S.d3d12) { if (e.dev12 == nullptr) return false; S.dev = (ID3D12Device*)e.dev12; S.dev->AddRef(); if (S.fila == nullptr) { AvisoUmaVez("fila do evaluate ainda nao vista (ExecuteCommandLists); esperando"); S.dev->Release(); S.dev = nullptr; return false; } }
    else { if (e.dev11 == nullptr) return false; S.dev11 = (ID3D11Device*)e.dev11; S.dev11->AddRef(); S.dev11->GetImmediateContext(&S.ctx11); if (S.ctx11) S.ctx11->QueryInterface(__uuidof(ID3D11DeviceContext4), reinterpret_cast<void**>(&S.ctx4)); }
    return true;
}

int64_t ultimaTentativa = 0;

} // namespace anonimo

/* ================================================================== */
/* Interface                                                           */
/* ================================================================== */
void Iniciar(LogFn log)
{
    L = log;
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpcFreq = f.QuadPart > 0 ? f.QuadPart : 1;
    evVigia = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    evVigiaIn = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    evVigiaOut = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    LerControle();
}

void FilaVista(ID3D12CommandQueue* fila)
{
    if (fila == nullptr || S.fila == fila) return;
    D3D12_COMMAND_QUEUE_DESC qd = fila->GetDesc();
    if (qd.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
    std::lock_guard<std::mutex> g(mu);
    if (S.fila) S.fila->Release();
    fila->AddRef(); S.fila = fila;
}

void SoltarPipe();   /* X-FG-6, abaixo */
void Servico()
{
    if (L == nullptr) return;
    const bool mudou = LerControle();
    const int e = estado.load();
    if (mudou && !ctl.upstream && (e == E_PRONTO || e == E_CONECTADO)) { Falhar("upstream=0 no controle"); }
    if (mudou && ctl.upstream && e == E_FALHOU && strstr(motivoFalha, "controle")) { estado = E_ESPERANDO; motivoFalha[0] = 0; L("ngx: upstream: religado pelo controle"); }
    if (!ctl.upstream) return;
    switch (estado.load())
    {
    case E_ESPERANDO:
    {
        std::lock_guard<std::mutex> g(mu);
        /* X-FG-6: depois de largar o pipe a sonda continua dizendo viuSR; so um evaluate NOVO reabre */
        if (S.avaliacoesSRNaSoltura != 0 && Ler().avaliacoesSR <= S.avaliacoesSRNaSoltura) return;
        if (!ColherDaSonda()) return;
        estado = E_CONECTANDO; ultimaTentativa = 0;
        /* fallthrough para tentar ja */
    }
    /* fallthrough */
    case E_CONECTANDO:
    {
        const int64_t agora = Agora();
        if (ultimaTentativa != 0 && Ms(ultimaTentativa, agora) < 2000.0) return;
        bool ocupado = false;
        if (!Conectar(&ocupado))
        {
            /* X-FG-6: o pipe esta com o sr_hook (o jogo acabou de trocar FSR/XeSS -> DLSS): o pedido
               ficou registrado e a proxima volta do Servico (~250 ms) tenta de novo, sem os 2 s */
            if (ocupado) return;
            ultimaTentativa = agora;
            static bool avisou = false; if (!avisou) { avisou = true; L("ngx: upstream: o app nao serve o pipe .up (nativo_upstream=0 ou app antigo); tentando a cada 2 s"); }
            return;
        }
        ultimaTentativa = 0;
        estado = E_CONECTADO;
    }
    /* fallthrough */
    case E_CONECTADO:
    {
        std::lock_guard<std::mutex> g(mu);
        if (S.tex[SLOT_COLOR] || S.tex11[SLOT_COLOR]) { /* rebuild por mudanca de tamanho */ Estado ev = Ler(); if (ev.internoW == S.w && ev.internoH == S.h) { estado = E_PRONTO; return; } LiberarTexturas(); S.w = ev.internoW; S.h = ev.internoH; }
        if (!Build()) return;
        if (thVigia == nullptr) thVigia = CreateThread(nullptr, 0, Vigia, nullptr, 0, nullptr);
        estado = E_PRONTO;
        return;
    }
    case E_PRONTO:
        ConferirProva();
        if (Ponte::PipeUp::AlguemQuer(Ponte::PipeUp::DONO_NGX)) SoltarPipe();   /* X-FG-6: o jogo trocou para FSR/XeSS? */
        return;
    default: return;
    }
}

/* X-FG-6: PASSAGEM DE BASTAO (o espelho do SoltarPipe do sr_hook). O sr_hook pediu o pipe (o
   jogo esta chamando o FSR/XeSS). Se a feature que serviamos foi solta (ReleaseFeature), ou nao
   ha evaluate ha 1 s, esta sessao acabou: DestravarFila, espera da GPU, libera o conjunto e LARGA
   o pipe sem fechar. Volta a ESPERANDO e so renasce de um evaluate NOVO de DLSS SR. */
void SoltarPipe()
{
    const int64_t agora = Agora();
    const bool semEvaluate = S.qpcUltimo.load() != 0 && Ms(S.qpcUltimo.load(), agora) >= 1000.0;
    if (!S.featureSolta.load() && !semEvaluate) return;
    L("ngx: upstream: o jogo trocou de upscaler (%s): soltando a sessao %ux%u e passando o pipe .up ao FSR/XeSS (sr_hook) apos %llu quadros",
      S.featureSolta.load() ? "a feature DLSS foi solta" : "sem evaluate ha 1 s e o FSR/XeSS esta sendo chamado", S.w, S.h, (unsigned long long)S.n);
    estado = E_ESPERANDO;   /* AntesD12/D11 param de tocar nos slots a partir daqui */
    trocando = false;
    Sleep(60);              /* um evaluate em voo termina antes de soltarmos o que ele usa */
    DestravarFila();
    if (S.fila && S.fRecicla && S.reciclaVal && S.fRecicla->GetCompletedValue() < S.reciclaVal && S.evRecicla && SUCCEEDED(S.fRecicla->SetEventOnCompletion(S.reciclaVal, S.evRecicla))) WaitForSingleObject(S.evRecicla, 1000);
    {
        std::lock_guard<std::mutex> g(mu);
        LiberarTexturas();
        Soltar(S.ctx4); Soltar(S.ctx11); Soltar(S.dev11); Soltar(S.dev);   /* o ColherDaSonda da proxima sessao toma as referencias de novo */
        S.handleAtual = nullptr; S.featureSolta = false; S.qpcUltimo = 0; S.trocou = false;
        S.avaliacoesSRNaSoltura = Ler().avaliacoesSR;
        primeiroRetorno = false;
    }
    Ponte::PipeUp::Largar(Ponte::PipeUp::DONO_NGX);
}

/* X-FG-6: do HkD12Release/HkD11Release (ngx_hook.cpp): o jogo soltou uma feature. */
void FeatureSolta(const Handle* h)
{
    if (h == nullptr || h != S.handleAtual || estado.load() != E_PRONTO) return;
    S.featureSolta = true;
    L("ngx: upstream: o jogo soltou a feature %u que a sessao servia (trocou de upscaler ou de qualidade?)", h->Id);
}

bool Ativo() { return estado.load() == E_PRONTO; }

bool AntesD12(ID3D12GraphicsCommandList* lista, const Handle* h, const Parameter* p) { return AntesD12Impl(lista, h, p); }
void DepoisD12(const Parameter* p) { DepoisD12Impl(p); }
bool AntesD11(ID3D11DeviceContext* ctx, const Handle* h, const Parameter* p) { return AntesD11Impl(ctx, h, p); }
void DepoisD11(const Parameter* p) { DepoisD11Impl(p); }

void Parar()
{
    if (estado.load() == E_PRONTO || estado.load() == E_CONECTADO) L("ngx: upstream desligado (ponte encerrando) apos %llu quadros, atrasados %u", (unsigned long long)S.n, atrasadosTotal.load());
    estado = E_DESLIGADO; trocando = false;
    Sleep(60);   /* um AntesD12 em voo (nao trava mutex por desempenho) termina antes de soltarmos o que ele usa */
    DestravarFila();   /* H2: nenhum Wait(out,n) pode sobreviver a esta funcao sem sinal */
    vigiaSai = true; if (evVigia) SetEvent(evVigia);
    if (thVigia) { WaitForSingleObject(thVigia, 1500); CloseHandle(thVigia); thVigia = nullptr; }
    /* deixa a GPU terminar o nosso trabalho antes de soltar */
    if (S.fila && S.fRecicla && S.reciclaVal && S.fRecicla->GetCompletedValue() < S.reciclaVal && S.evRecicla && SUCCEEDED(S.fRecicla->SetEventOnCompletion(S.reciclaVal, S.evRecicla))) WaitForSingleObject(S.evRecicla, 1000);
    std::lock_guard<std::mutex> g(mu);
    LiberarTexturas();
    Soltar(S.lista); for (auto& a : S.alloc) Soltar(a);
    Soltar(S.fRecicla); Soltar(S.fProva); FecharHandle(S.evRecicla); FecharHandle(S.evProva);
    Soltar(S.ctx4); Soltar(S.ctx11); Soltar(S.dev5); Soltar(S.dev11); Soltar(S.devAux);
    Soltar(S.fila); Soltar(S.dev);
    /* X-FG-6: o pipe e compartilhado - se e nosso, fecha; senao so larga o pedido (o sr_hook fecha o dele) */
    if (Ponte::PipeUp::Dono() == Ponte::PipeUp::DONO_NGX) Ponte::PipeUp::Fechar(); else Ponte::PipeUp::Largar(Ponte::PipeUp::DONO_NGX);
    FecharHandle(evVigia); FecharHandle(evVigiaIn); FecharHandle(evVigiaOut);
}

} } // namespace Ngx::Up
