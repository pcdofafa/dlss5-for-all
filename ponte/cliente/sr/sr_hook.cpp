/* =====================================================================
   sr_hook.cpp -- degrau 2 (Q2, 16/09/2026): FSR 2/3.1 e XeSS -> DLSS SR
   real pelo app. Ver sr_hook.h e relatorios\Q2-fio.md.

   O desenho e o do ngx_upstream.cpp (G1/Q1), com tres diferencas:
     1. o que se intercepta e o DISPATCH de um upscaler de terceiro, lido
        da struct dele (nao do objeto de parametros do NGX);
     2. o Output volta no tamanho de SAIDA e e copiado no destino do
        upscaler, DENTRO da command list do jogo, depois de um Wait(out,n)
        na fila; a chamada original e pulada;
     3. MV e depth em qualquer formato float/unorm sao convertidos por
        compute shader para R16G16_FLOAT / R32_FLOAT (o Q1 so convertia a
        depth). Formatos inteiros ficam de fora, com motivo.
   Enquanto a prova de prontidao corre (D3D12), a original e chamada e o
   resultado do app e descartado (serve de aquecimento da feature).
   ===================================================================== */
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <atomic>
#include <mutex>

#include "../ponte_comum.h"   /* protocolo v9 (structs e constantes); o Log daqui nao e usado */
#include "../minhook/include/MinHook.h"
#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_api_types.h"
#include "ffx_api/ffx_upscale.h"
#include "sr_contrato.h"
#include "sr_hook.h"
#include "../fg/fg_hook.h"   /* J3: depth e MV do jogo tambem alimentam o frame generation */
#include "../ponte_dados.h"  /* J5: ... e a ponte da fase 2 (o app, pela pelicula), em D3D11 e D3D12 */
#include "../pipe_up.h"      /* X-FG-6: o pipe .up e um so e passa de mao entre este modulo e o ngx_upstream */
#include "../dados/colheita.h"   /* X-FG-6: depth+MV para o FG e para a pelicula SEM sessao .up (Nativo comum) */

namespace Sr {
namespace {

using Ponte::FeedHello; using Ponte::FeedHelloAck; using Ponte::FeedBuild; using Ponte::FeedBuildAck; using Ponte::FeedFrameMsg;
using Ponte::kMagic; using Ponte::kVersao;
using Ponte::SLOT_COLOR; using Ponte::SLOT_OUTPUT; using Ponte::SLOT_DEPTH; using Ponte::SLOT_MV; using Ponte::SLOTS;
using Ponte::ComoValor; using Ponte::ComoHandle; using Ponte::Soltar; using Ponte::FecharHandle;

const uint32_t kBuildSr  = 0x200u;   /* FEED_BUILD_SR_REQUEST */
const uint32_t kAckUp    = 0x100u;   /* FEED_ACK_UPSTREAM: o host entende 'X' */
const uint32_t kAckSr    = 0x200u;   /* FEED_ACK_SR: o host entende 'S' e cria a feature DLSS SR */

#pragma pack(push, 1)
struct FeedExtraMsg { uint64_t n; float jitter_x, jitter_y, mv_scale_x, mv_scale_y, pre_exposure, exposure_scale, frame_dt_ms; uint32_t flags; };
struct FeedSrMsg    { uint64_t n; uint32_t render_w, render_h; float sharpness; uint32_t flags; };
#pragma pack(pop)
static_assert(sizeof(FeedExtraMsg) == 40, "FeedExtraMsg = 40 bytes");
static_assert(sizeof(FeedSrMsg) == 24, "FeedSrMsg = 24 bytes");

LogFn L = nullptr;
std::mutex mu;
int64_t qpcFreq = 1;
int64_t Agora() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
double  Ms(int64_t de, int64_t ate) { return 1000.0 * double(ate - de) / double(qpcFreq); }

enum Fase { E_DESLIGADO, E_ESPERANDO, E_CONECTANDO, E_CONECTADO, E_PRONTO, E_FALHOU };
std::atomic<int> estado{ E_ESPERANDO };
std::atomic<bool> trocando{ false };   /* a troca esta ligada (prova passou, ou D3D11) */
std::atomic<bool> parado{ false };
std::atomic<int>  emVoo{ 0 };
struct Voo { Voo() { ++emVoo; } ~Voo() { --emVoo; } };
char motivoFalha[160] = "";
std::atomic<unsigned> estadoVersao{ 1 };
unsigned estadoEscrito = 0;

enum Api { API_NENHUMA = 0, API_FFX, API_FSR2, API_XESS };
const char* NomeApi(int a) { return a == API_FFX ? "FSR3.1" : a == API_FSR2 ? "FSR2" : a == API_XESS ? "XeSS" : "-"; }

/* ------------------------------------------------------------------ */
/* Controle (%TEMP%\dlss5-ponte.<pid>.ngx, o mesmo arquivo do G1)      */
/* ------------------------------------------------------------------ */
struct Controle
{
    bool lido = false; FILETIME mtime = {};
    bool sr = true;                 /* sr=0 desliga o degrau 2 */
    bool prova = true;
    unsigned estadoIn = 0, estadoOut = 0;   /* 0 = o que o dispatch declarar (ou o padrao da API) */
    wchar_t pipe[128] = L"";
} ctl;

void CaminhoTemp(wchar_t* out, size_t n, const wchar_t* sufixo)
{
    wchar_t tmp[MAX_PATH] = L"";
    DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (k == 0 || k >= MAX_PATH) { tmp[0] = L'.'; tmp[1] = L'\\'; tmp[2] = 0; }
    _snwprintf(out, n, L"%sdlss5-ponte.%lu.%ls", tmp, GetCurrentProcessId(), sufixo);
}

bool LerControle()
{
    wchar_t caminho[MAX_PATH]; CaminhoTemp(caminho, MAX_PATH, L"ngx");
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    if (!GetFileAttributesExW(caminho, GetFileExInfoStandard, &fa)) { ctl.lido = true; return false; }
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
        if (!strcmp(k, "sr")) ctl.sr = atoi(v) != 0;
        else if (!strcmp(k, "prova")) ctl.prova = atoi(v) != 0;
        else if (!strcmp(k, "sr_estado_in")) ctl.estadoIn = (unsigned)strtoul(v, nullptr, 0);
        else if (!strcmp(k, "sr_estado_out")) ctl.estadoOut = (unsigned)strtoul(v, nullptr, 0);
        else if (!strcmp(k, "pipe_up")) { for (int i = 0; i < 127; ++i) { ctl.pipe[i] = (wchar_t)(unsigned char)v[i]; if (!v[i]) break; } ctl.pipe[127] = 0; }
    }
    fclose(f);
    return true;
}

/* ------------------------------------------------------------------ */
/* Pipe: X-FG-6 (18/09/2026) - a conexao mora em pipe_up.cpp e e UMA SO, */
/* compartilhada com o ngx_upstream (um dono por vez; ver pipe_up.h).    */
/* ------------------------------------------------------------------ */
bool hostEntendeX = false, hostEntendeS = false;
bool Escrever(const void* b, DWORD n) { return Ponte::PipeUp::Escrever(b, n); }
bool LerPipe(void* b, DWORD n)        { return Ponte::PipeUp::Ler(b, n); }

void DestravarFila();
void Falhar(const char* motivo)
{
    if (estado.load() == E_FALHOU) return;
    snprintf(motivoFalha, sizeof motivoFalha, "%s", motivo);
    trocando = false;
    estado = E_FALHOU;
    ++estadoVersao;
    L("sr: desligado (%s)", motivo);
    DestravarFila();
}
unsigned avisos = 0;
void AvisoUmaVez(const char* m) { if (avisos < 16) { ++avisos; L("sr: %s", m); } }
/* C7: excecao na nossa parte de um hook; o hook segue para a chamada original do jogo */
std::atomic<unsigned> excecoes{ 0 };
void Excecao(const char* onde) { if (excecoes++ < 8) L("sr: excecao no hook %s; seguindo com a chamada original", onde); }

/* ------------------------------------------------------------------ */
/* O que um dispatch traz (unificado das tres APIs)                    */
/* ------------------------------------------------------------------ */
struct Desc
{
    int api = API_NENHUMA;
    bool d3d12 = true;
    void* lista = nullptr;                  /* ID3D12GraphicsCommandList* | ID3D11DeviceContext* (pode ser nulo em D3D11) */
    IUnknown* cor = nullptr, *mv = nullptr, *depth = nullptr, *expo = nullptr, *out = nullptr;
    D3D12_RESOURCE_STATES eCor = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, eMv = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES eDepth = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, eExpo = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES eOut = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    unsigned rw = 0, rh = 0;                /* renderSize */
    unsigned ow = 0, oh = 0;                /* upscaleSize (0 = tamanho do recurso de saida) */
    unsigned bxCor = 0, byCor = 0, bxMv = 0, byMv = 0, bxDepth = 0, byDepth = 0, bxOut = 0, byOut = 0;
    float jx = 0, jy = 0, msx = 1, msy = 1, dt = 0, pre = 1, escExpo = 1, sharp = 0;
    bool reset = false, sharpen = false;
    unsigned flagsNgx = 0;                  /* proposta de DLSS.Feature.Create.Flags */
    int depthInv = -1;                      /* -1 = desconhecido (contexto criado antes do hook) */
    bool ctxVisto = false;
    /* J3: o FSR (2.x e 3.1) carrega a camera no proprio dispatch (ffx_upscale.h:104-107,
       sr_contrato.h:102-104); o XeSS nao tem esses campos. 0 = o jogo nao informou. */
    float camNear = 0, camFar = 0, camFov = 0, viewToM = 0;
    bool mvNdc = false;                     /* XeSS com XESS_NDC_VELOCITY: convencao nao resolvida */
    void* ctx = nullptr;                    /* X-FG-6: o contexto do upscaler deste dispatch (para saber quando o jogo o destroi) */
};

/* contextos vistos na criacao (ffx_api / FSR2 / XeSS) */
struct CtxRec { void* ctx = nullptr; int api = 0; unsigned flags = 0; unsigned maxRw = 0, maxRh = 0, outW = 0, outH = 0; float msx = 1, msy = 1, jsx = 1, jsy = 1; bool d3d11 = false; };
CtxRec ctxs[16];
CtxRec* AcharCtx(void* c) { for (auto& r : ctxs) if (r.ctx == c && c) return &r; return nullptr; }
CtxRec* RegistrarCtx(void* c, int api)
{
    if (CtxRec* r = AcharCtx(c)) { *r = CtxRec(); r->ctx = c; r->api = api; return r; }
    for (auto& r : ctxs) if (r.ctx == nullptr) { r = CtxRec(); r.ctx = c; r.api = api; return &r; }
    ctxs[0] = CtxRec(); ctxs[0].ctx = c; ctxs[0].api = api; return &ctxs[0];
}
void EsquecerCtx(void* c) { if (CtxRec* r = AcharCtx(c)) *r = CtxRec(); }

/* ------------------------------------------------------------------ */
/* Sessao                                                              */
/* ------------------------------------------------------------------ */
DXGI_FORMAT Tipado(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default: return f;
    }
}
bool CorAceita(DXGI_FORMAT f)
{
    switch (Tipado(f))
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R11G11B10_FLOAT: return true;
    default: return false;
    }
}
bool SaidaAceita(DXGI_FORMAT f) { return CorAceita(f) || Tipado(f) == DXGI_FORMAT_R32G32B32A32_FLOAT; }
bool EhHdr(DXGI_FORMAT f) { f = Tipado(f); return f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R11G11B10_FLOAT || f == DXGI_FORMAT_R32G32B32A32_FLOAT; }
unsigned BytesPorPixel(DXGI_FORMAT f)
{
    switch (Tipado(f))
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
    default: return 4;
    }
}
bool DepthCopiavel(DXGI_FORMAT f) { return f == DXGI_FORMAT_R32_FLOAT || f == DXGI_FORMAT_D32_FLOAT || f == DXGI_FORMAT_R32_TYPELESS; }
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
/* MV: o formato tipado float/unorm/snorm da familia, para a SRV; UNKNOWN = inteiro (fora). */
DXGI_FORMAT MvSrvFmt(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R16G16_TYPELESS: case DXGI_FORMAT_R16G16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R16G16_UNORM: case DXGI_FORMAT_R16G16_SNORM: return f;
    case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R8G8_TYPELESS: case DXGI_FORMAT_R8G8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
    case DXGI_FORMAT_R8G8_SNORM: return f;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_SNORM: return f;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R11G11B10_FLOAT: return f;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

/* conversao por compute (D3D12 e D3D11): 0 = depth (float), 1 = mv (float2) */
const char kConvHlsl[] =
    "#define RS \"RootFlags(0), RootConstants(num32BitConstants=4, b0), DescriptorTable(SRV(t0, numDescriptors=1)), DescriptorTable(UAV(u0, numDescriptors=1))\"\n"
    "Texture2D<float4> src : register(t0);\n"
    "RWTexture2D<DST_T> dst : register(u0);\n"
    "cbuffer C : register(b0) { uint bx, by, w, h; };\n"
    "#ifdef RS12\n[RootSignature(RS)]\n#endif\n"
    "[numthreads(8,8,1)]\n"
    "void cs(uint3 id : SV_DispatchThreadID) { if (id.x >= w || id.y >= h) return; float4 v = src.Load(int3(bx + id.x, by + id.y, 0)); dst[id.xy] = DST_EXPR; }\n";

struct Conv   /* um conversor: depth ou mv */
{
    bool ativo = false;
    DXGI_FORMAT srvFmt = DXGI_FORMAT_UNKNOWN, interFmt = DXGI_FORMAT_UNKNOWN;   /* interFmt: formato da intermediaria */
    bool depthStencil = false;
    /* D3D12 */
    ID3D12PipelineState* pso = nullptr;
    ID3D12Resource* inter = nullptr;
    /* D3D11 */
    ID3D11ComputeShader* cs11 = nullptr;
    ID3D11Texture2D* inter11 = nullptr; ID3D11ShaderResourceView* srv11 = nullptr; ID3D11UnorderedAccessView* uav11 = nullptr;
    unsigned logadas = 0;
};

struct Sessao
{
    int api = API_NENHUMA;
    bool d3d12 = true;
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* fila = nullptr;
    ID3D11Device* dev11 = nullptr; ID3D11DeviceContext* ctx11 = nullptr; ID3D11DeviceContext4* ctx4 = nullptr; ID3D11Device5* dev5 = nullptr;

    unsigned w = 0, h = 0;              /* tamanho interno (do Build) */
    unsigned ow = 0, oh = 0;            /* tamanho de saida */
    unsigned mvW = 0, mvH = 0;          /* tamanho do slot de MV (interno, ou saida quando MV em resolucao de tela) */
    DXGI_FORMAT corFmt = DXGI_FORMAT_UNKNOWN, outFmt = DXGI_FORMAT_UNKNOWN, mvFmtJogo = DXGI_FORMAT_UNKNOWN, depthFmtJogo = DXGI_FORMAT_UNKNOWN, expoFmtJogo = DXGI_FORMAT_UNKNOWN;
    unsigned flagsNgx = 0; int depthInv = -1;
    float mvScaleX = 1, mvScaleY = 1;
    bool copiaMv = false, copiaDepth = false, copiaExpo = false;
    Conv convDepth, convMv;
    ID3D12RootSignature* convRS = nullptr; ID3D12DescriptorHeap* convHeap = nullptr; UINT convIncr = 0;
    ID3D11Buffer* convCB11 = nullptr;

    /* D3D12 */
    ID3D12Resource* tex[SLOTS] = {}; ID3D12Resource* texExpo = nullptr;
    HANDLE htex[SLOTS] = {}; HANDLE hExpo = nullptr;
    ID3D12Fence* fIn = nullptr, *fOut = nullptr, *fRecicla = nullptr;
    HANDLE evRecicla = nullptr;
    uint64_t reciclaVal = 0, fenceDoBuf[3] = {};
    ID3D12CommandAllocator* alloc[3] = {};
    ID3D12GraphicsCommandList* lista = nullptr;
    /* prova de prontidao */
    ID3D12Resource* provaA = nullptr, *provaB = nullptr, *rbA = nullptr, *rbB = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {}; UINT64 rbBytes = 0;
    ID3D12Fence* fProva = nullptr; uint64_t provaVal = 0;
    int provaEtapa = 0;   /* 0 = pode copiar A/B; 1 = copiado (proximo quadro faz o readback); 2 = readback em voo */
    unsigned provaIguais = 0, provaTotal = 0; bool provaFeita = false;

    /* D3D11 */
    ID3D11Texture2D* tex11[SLOTS] = {}; ID3D11Texture2D* texExpo11 = nullptr;
    ID3D11Fence* fIn11 = nullptr, *fOut11 = nullptr;
    ID3D12Device* devAux = nullptr; ID3D12Fence* fOutAux = nullptr;

    uint64_t n = 0;
    void* ultimaLista = nullptr;   /* a command list do ultimo dispatch (para AoExecutar achar a fila) */
    /* X-FG-6: a troca de upscaler ao vivo. `ctxAtual` e o contexto que a sessao serve; o jogo o
       destruindo (HkFfxDestroy/HkFsr2Destroy/HkXessDestroy) liga `ctxSolto`. `qpcUltimo` e o
       relogio do ultimo dispatch servido. O Servico larga o pipe para o ngx_upstream quando ele
       pede E (o contexto foi destruido OU nao ha dispatch ha 1 s). */
    void* ctxAtual = nullptr;
    std::atomic<bool> ctxSolto{ false };
    std::atomic<int64_t> qpcUltimo{ 0 };
} S;

struct Janela { unsigned n = 0; double copia = 0, espera = 0, troca = 0; int64_t inicio = 0; } jan;
std::atomic<unsigned> atrasadosTotal{ 0 };
std::atomic<unsigned> dispatches{ 0 }, trocados{ 0 };

/* vigia */
HANDLE evVigia = nullptr, evVigiaIn = nullptr, evVigiaOut = nullptr, thVigia = nullptr;
std::atomic<uint64_t> vigiaN{ 0 };
std::atomic<bool> vigiaSai{ false };
std::atomic<double> esperaMedidaMs{ 0 };
bool primeiroRetorno = false;

/* o primeiro dispatch visto, guardado para o Build */
Desc primeiro; bool temPrimeiro = false;

/* X-FG-6: o jogo destruiu o contexto que a sessao serve (trocou de upscaler, ou de qualidade -
   neste caso o proximo dispatch chega com outro contexto e a sessao segue/refaz como sempre). */
void ContextoDestruido(void* c)
{
    if (c == nullptr || c != S.ctxAtual || estado.load() != E_PRONTO) return;
    S.ctxSolto = true;
    L("sr: o jogo destruiu o contexto %p que a sessao servia (trocou de upscaler ou de qualidade?)", c);
}

/* ------------------------------------------------------------------ */
/* Recursos                                                            */
/* ------------------------------------------------------------------ */
ID3D12Resource* Criar12(UINT w, UINT h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, HANDLE* hOut)
{
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; rd.Flags = flags;
    ID3D12Resource* r = nullptr;
    HRESULT hr = S.dev->CreateCommittedResource(&hp, hOut ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&r));
    if (FAILED(hr)) { L("sr: CreateCommittedResource %ux%u fmt=%u falhou 0x%08X", w, h, (unsigned)fmt, (unsigned)hr); return nullptr; }
    if (hOut && FAILED(hr = S.dev->CreateSharedHandle(r, nullptr, GENERIC_ALL, nullptr, hOut))) { L("sr: CreateSharedHandle falhou 0x%08X", (unsigned)hr); r->Release(); return nullptr; }
    return r;
}
ID3D11Texture2D* Criar11(UINT w, UINT h, DXGI_FORMAT fmt, UINT bind, HANDLE* hOut)
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = bind; td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
    ID3D11Texture2D* t = nullptr;
    HRESULT hr = S.dev11->CreateTexture2D(&td, nullptr, &t);
    if (FAILED(hr)) { L("sr: D3D11 CreateTexture2D %ux%u fmt=%u falhou 0x%08X", w, h, (unsigned)fmt, (unsigned)hr); return nullptr; }
    IDXGIResource1* r = nullptr;
    hr = t->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(&r));
    if (SUCCEEDED(hr)) { hr = r->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, hOut); r->Release(); }
    if (FAILED(hr)) { L("sr: D3D11 CreateSharedHandle falhou 0x%08X", (unsigned)hr); t->Release(); return nullptr; }
    return t;
}

void LiberarConv(Conv& c)
{
    Soltar(c.pso); Soltar(c.inter); Soltar(c.cs11); Soltar(c.inter11); Soltar(c.srv11); Soltar(c.uav11);
    c.ativo = false; c.logadas = 0;
}
void LiberarTexturas()
{
    Fg::LimparDados();   /* J3: o FG larga a referencia dele antes de os slots sumirem */
    Ponte::Dados::Limpar();   /* J5: a ponte da fase 2 idem */
    for (int i = 0; i < SLOTS; ++i) { Soltar(S.tex[i]); Soltar(S.tex11[i]); FecharHandle(S.htex[i]); }
    Soltar(S.texExpo); Soltar(S.texExpo11); FecharHandle(S.hExpo);
    Soltar(S.provaA); Soltar(S.provaB); Soltar(S.rbA); Soltar(S.rbB);
    Soltar(S.fIn); Soltar(S.fOut); Soltar(S.fIn11); Soltar(S.fOut11); Soltar(S.fOutAux);
    LiberarConv(S.convDepth); LiberarConv(S.convMv);
    Soltar(S.convRS); Soltar(S.convHeap); Soltar(S.convCB11);
}

bool CompilarConv(bool d3d12, bool mv, ID3DBlob** cs)
{
    ID3DBlob* err = nullptr;
    const D3D_SHADER_MACRO defs12[] = { { "RS12", "1" }, { "DST_T", mv ? "float2" : "float" }, { "DST_EXPR", mv ? "v.xy" : "v.x" }, { nullptr, nullptr } };
    const D3D_SHADER_MACRO defs11[] = { { "DST_T", mv ? "float2" : "float" }, { "DST_EXPR", mv ? "v.xy" : "v.x" }, { nullptr, nullptr } };
    HRESULT hr = D3DCompile(kConvHlsl, sizeof(kConvHlsl) - 1, mv ? "mv" : "depth", d3d12 ? defs12 : defs11, nullptr, "cs", "cs_5_0", 0, 0, cs, &err);
    if (FAILED(hr)) { L("sr: shader de conversao (%s) nao compilou 0x%08X: %s", mv ? "mv" : "depth", (unsigned)hr, err ? (const char*)err->GetBufferPointer() : ""); Soltar(err); return false; }
    Soltar(err);
    return true;
}

/* D3D12: RS + heap (4 descritores: depth SRV/UAV, mv SRV/UAV) + PSOs. Depois dos slots. */
bool Conv12Criar()
{
    ID3DBlob* cs = nullptr, *rs = nullptr;
    if (!CompilarConv(true, false, &cs)) return false;
    HRESULT hr = D3DGetBlobPart(cs->GetBufferPointer(), cs->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &rs);
    if (SUCCEEDED(hr)) hr = S.dev->CreateRootSignature(0, rs->GetBufferPointer(), rs->GetBufferSize(), __uuidof(ID3D12RootSignature), reinterpret_cast<void**>(&S.convRS));
    Soltar(rs);
    if (SUCCEEDED(hr)) { D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = S.convRS; pd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() }; hr = S.dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), reinterpret_cast<void**>(&S.convDepth.pso)); }
    Soltar(cs);
    if (SUCCEEDED(hr) && CompilarConv(true, true, &cs)) { D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = S.convRS; pd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() }; hr = S.dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), reinterpret_cast<void**>(&S.convMv.pso)); Soltar(cs); }
    if (SUCCEEDED(hr)) { D3D12_DESCRIPTOR_HEAP_DESC hd = {}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 4; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; hr = S.dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void**>(&S.convHeap)); }
    if (FAILED(hr)) { L("sr: conversao por compute (D3D12) nao montou 0x%08X; MV/depth ficam zeradas quando precisam de conversao", (unsigned)hr); return false; }
    S.convIncr = S.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE h0 = S.convHeap->GetCPUDescriptorHandleForHeapStart();
    { D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R32_FLOAT; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; D3D12_CPU_DESCRIPTOR_HANDLE h = h0; h.ptr += S.convIncr * 1; S.dev->CreateUnorderedAccessView(S.tex[SLOT_DEPTH], nullptr, &ud, h); }
    { D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R16G16_FLOAT; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; D3D12_CPU_DESCRIPTOR_HANDLE h = h0; h.ptr += S.convIncr * 3; S.dev->CreateUnorderedAccessView(S.tex[SLOT_MV], nullptr, &ud, h); }
    return true;
}
/* D3D12: a intermediaria (copia identica do recurso do jogo) + SRV tipada no heap. slot: 0 = depth, 2 = mv. */
bool Inter12(Conv& c, ID3D12Resource* src, unsigned slotHeap)
{
    D3D12_RESOURCE_DESC dd = src->GetDesc();
    if (c.inter) { D3D12_RESOURCE_DESC id = c.inter->GetDesc(); if (id.Width == dd.Width && id.Height == dd.Height && id.Format == dd.Format) return true; Soltar(c.inter); }
    if (dd.SampleDesc.Count != 1 || dd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) { AvisoUmaVez("recurso do jogo e MSAA ou nao e 2D: conversao impossivel"); return false; }
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = dd.Width; rd.Height = dd.Height; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = dd.Format; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; rd.Flags = c.depthStencil ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_NONE;
    HRESULT hr = S.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&c.inter));
    if (FAILED(hr)) { char m[160]; snprintf(m, sizeof m, "intermediaria (fmt %u %ux%u) falhou 0x%08X", (unsigned)dd.Format, (unsigned)dd.Width, dd.Height, (unsigned)hr); AvisoUmaVez(m); return false; }
    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = c.srvFmt; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sd.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE h = S.convHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr += S.convIncr * slotHeap;
    S.dev->CreateShaderResourceView(c.inter, &sd, h);
    return true;
}
/* D3D12: grava na nossa lista a conversao src -> slot (rw x rh a partir de bx,by). */
void Converter12(Conv& c, ID3D12Resource* src, D3D12_RESOURCE_STATES eSrc, ID3D12Resource* slot, unsigned slotHeap, unsigned bx, unsigned by, unsigned rw, unsigned rh)
{
    if (!c.pso || !Inter12(c, src, slotHeap)) return;
    D3D12_RESOURCE_BARRIER b1[3];
    b1[0] = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, { src, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, eSrc, D3D12_RESOURCE_STATE_COPY_SOURCE } };
    b1[1] = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, { c.inter, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST } };
    b1[2] = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, { slot, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS } };
    S.lista->ResourceBarrier(3, b1);
    D3D12_TEXTURE_COPY_LOCATION d = {}; d.pResource = c.inter; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION s = {}; s.pResource = src; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.SubresourceIndex = 0;
    S.lista->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
    D3D12_RESOURCE_BARRIER b2[2];
    b2[0] = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, { src, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, eSrc } };
    b2[1] = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, { c.inter, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE } };
    S.lista->ResourceBarrier(2, b2);
    ID3D12DescriptorHeap* heaps[1] = { S.convHeap };
    S.lista->SetDescriptorHeaps(1, heaps);
    S.lista->SetComputeRootSignature(S.convRS);
    S.lista->SetPipelineState(c.pso);
    const unsigned k[4] = { bx, by, rw, rh };
    S.lista->SetComputeRoot32BitConstants(0, 4, k, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE g0 = S.convHeap->GetGPUDescriptorHandleForHeapStart(), gs = g0, gu = g0; gs.ptr += S.convIncr * slotHeap; gu.ptr += S.convIncr * (slotHeap + 1);
    S.lista->SetComputeRootDescriptorTable(1, gs);
    S.lista->SetComputeRootDescriptorTable(2, gu);
    S.lista->Dispatch((rw + 7) / 8, (rh + 7) / 8, 1);
    D3D12_RESOURCE_BARRIER b3[2];
    b3[0] = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, { c.inter, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON } };
    b3[1] = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, { slot, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON } };
    S.lista->ResourceBarrier(2, b3);
    if (c.logadas++ == 0) { D3D12_RESOURCE_DESC dd = src->GetDesc(); L("sr: %s convertida fmt=%u -> %s (D3D12: %ux%u de %ux%u, base %u,%u, srv fmt=%u)", slotHeap == 0 ? "depth" : "mv", (unsigned)dd.Format, slotHeap == 0 ? "R32F" : "R16G16F", rw, rh, (unsigned)dd.Width, dd.Height, bx, by, (unsigned)c.srvFmt); }
}

/* D3D11 */
bool Conv11Criar()
{
    ID3DBlob* cs = nullptr;
    HRESULT hr = E_FAIL;
    if (CompilarConv(false, false, &cs)) { hr = S.dev11->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &S.convDepth.cs11); Soltar(cs); }
    if (SUCCEEDED(hr) && CompilarConv(false, true, &cs)) { hr = S.dev11->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &S.convMv.cs11); Soltar(cs); }
    if (SUCCEEDED(hr)) { D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; hr = S.dev11->CreateBuffer(&bd, nullptr, &S.convCB11); }
    if (SUCCEEDED(hr)) { D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R32_FLOAT; ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D; hr = S.dev11->CreateUnorderedAccessView(S.tex11[SLOT_DEPTH], &ud, &S.convDepth.uav11); }
    if (SUCCEEDED(hr)) { D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R16G16_FLOAT; ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D; hr = S.dev11->CreateUnorderedAccessView(S.tex11[SLOT_MV], &ud, &S.convMv.uav11); }
    if (FAILED(hr)) { L("sr: conversao por compute (D3D11) nao montou 0x%08X", (unsigned)hr); return false; }
    return true;
}
bool Inter11(Conv& c, ID3D11Resource* src)
{
    ID3D11Texture2D* t = nullptr;
    if (FAILED(src->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t))) || t == nullptr) return false;
    D3D11_TEXTURE2D_DESC dd; t->GetDesc(&dd); t->Release();
    if (c.inter11) { D3D11_TEXTURE2D_DESC id; c.inter11->GetDesc(&id); if (id.Width == dd.Width && id.Height == dd.Height) return true; Soltar(c.srv11); Soltar(c.inter11); }
    if (dd.SampleDesc.Count != 1) { AvisoUmaVez("recurso do jogo e MSAA (D3D11): conversao impossivel"); return false; }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = dd.Width; td.Height = dd.Height; td.MipLevels = 1; td.ArraySize = 1; td.Format = c.interFmt != DXGI_FORMAT_UNKNOWN ? c.interFmt : dd.Format; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (c.depthStencil ? D3D11_BIND_DEPTH_STENCIL : 0);
    HRESULT hr = S.dev11->CreateTexture2D(&td, nullptr, &c.inter11);
    if (SUCCEEDED(hr)) { D3D11_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = c.srvFmt; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1; hr = S.dev11->CreateShaderResourceView(c.inter11, &sd, &c.srv11); }
    if (FAILED(hr)) { char m[160]; snprintf(m, sizeof m, "intermediaria D3D11 (fmt %u %ux%u) falhou 0x%08X", (unsigned)dd.Format, dd.Width, dd.Height, (unsigned)hr); AvisoUmaVez(m); Soltar(c.inter11); return false; }
    return true;
}
void Converter11(ID3D11DeviceContext* ctx, Conv& c, ID3D11Resource* src, unsigned bx, unsigned by, unsigned rw, unsigned rh, const char* nome)
{
    if (!c.cs11 || !Inter11(c, src)) return;
    ctx->CopySubresourceRegion(c.inter11, 0, 0, 0, 0, src, 0, nullptr);
    const unsigned k[4] = { bx, by, rw, rh };
    ctx->UpdateSubresource(S.convCB11, 0, nullptr, k, 0, 0);
    ctx->CSSetShader(c.cs11, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &S.convCB11);
    ctx->CSSetShaderResources(0, 1, &c.srv11);
    UINT ini = (UINT)-1; ctx->CSSetUnorderedAccessViews(0, 1, &c.uav11, &ini);
    ctx->Dispatch((rw + 7) / 8, (rh + 7) / 8, 1);
    ID3D11ShaderResourceView* ns = nullptr; ID3D11UnorderedAccessView* nu = nullptr; ID3D11Buffer* nb = nullptr;
    ctx->CSSetShaderResources(0, 1, &ns); ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr); ctx->CSSetConstantBuffers(0, 1, &nb); ctx->CSSetShader(nullptr, nullptr, 0);
    if (c.logadas++ == 0) L("sr: %s convertida (D3D11: %ux%u, base %u,%u, srv fmt=%u)", nome, rw, rh, bx, by, (unsigned)c.srvFmt);
}

/* ------------------------------------------------------------------ */
/* Conectar / Build                                                    */
/* ------------------------------------------------------------------ */
/* X-FG-6: toma o pipe compartilhado. *ocupado = o ngx_upstream esta com ele (pedido registrado;
   volta a tentar a cada volta do Servico, sem os 2 s). false sem motivo = o app nao serve o .up. */
bool Conectar(bool* ocupado)
{
    char motivo[160] = "";
    if (Ponte::PipeUp::Tomar(Ponte::PipeUp::DONO_SR, ctl.pipe, ocupado, motivo, sizeof motivo)) { L("sr: com o pipe .up (host pid=%lu)", Ponte::PipeUp::HostPid()); return true; }
    if (motivo[0]) Falhar(motivo);
    return false;
}

bool Build()
{
    const D3D12_RESOURCE_FLAGS kAbrivel = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    const D3D12_RESOURCE_FLAGS kUav = kAbrivel | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    FeedBuild b = {};
    if (S.d3d12)
    {
        S.tex[SLOT_COLOR]  = Criar12(S.w, S.h, S.corFmt, kAbrivel, &S.htex[SLOT_COLOR]);
        S.tex[SLOT_OUTPUT] = Criar12(S.ow, S.oh, S.outFmt, kUav, &S.htex[SLOT_OUTPUT]);
        S.tex[SLOT_DEPTH]  = Criar12(S.w, S.h, DXGI_FORMAT_R32_FLOAT, kUav, &S.htex[SLOT_DEPTH]);
        S.tex[SLOT_MV]     = Criar12(S.mvW, S.mvH, DXGI_FORMAT_R16G16_FLOAT, kUav, &S.htex[SLOT_MV]);
        for (int i = 0; i < SLOTS; ++i) if (S.tex[i] == nullptr) { Falhar("criar texturas D3D12 falhou"); return false; }
        if (S.copiaExpo) { S.texExpo = Criar12(1, 1, DXGI_FORMAT_R32_FLOAT, kAbrivel, &S.hExpo); if (!S.texExpo) S.copiaExpo = false; }
        if ((S.convDepth.ativo || S.convMv.ativo) && !Conv12Criar()) { S.convDepth.ativo = false; S.convMv.ativo = false; if (!DepthCopiavel(S.depthFmtJogo)) S.copiaDepth = false; if (S.mvFmtJogo != DXGI_FORMAT_R16G16_FLOAT) S.copiaMv = false; }
        if (S.lista == nullptr)
        {
            for (int i = 0; i < 3; ++i)
                if (FAILED(S.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), reinterpret_cast<void**>(&S.alloc[i])))) { Falhar("CreateCommandAllocator"); return false; }
            if (FAILED(S.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, S.alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&S.lista)))) { Falhar("CreateCommandList"); return false; }
            S.lista->Close();
            if (FAILED(S.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&S.fRecicla)))) { Falhar("fence de reciclagem"); return false; }
            S.evRecicla = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (FAILED(S.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&S.fProva)))) { Falhar("fence da prova"); return false; }
        }
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
            { L("sr: sem recursos para a prova de prontidao; seguindo SEM prova (troca so por controle prova=0)"); Soltar(S.provaA); Soltar(S.provaB); Soltar(S.rbA); Soltar(S.rbB); }
        }
    }
    else
    {
        S.tex11[SLOT_COLOR]  = Criar11(S.w, S.h, S.corFmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &S.htex[SLOT_COLOR]);
        S.tex11[SLOT_OUTPUT] = Criar11(S.ow, S.oh, S.outFmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, &S.htex[SLOT_OUTPUT]);
        S.tex11[SLOT_DEPTH]  = Criar11(S.w, S.h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS, &S.htex[SLOT_DEPTH]);
        S.tex11[SLOT_MV]     = Criar11(S.mvW, S.mvH, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS, &S.htex[SLOT_MV]);
        for (int i = 0; i < SLOTS; ++i) if (S.tex11[i] == nullptr) { Falhar("criar texturas D3D11 falhou"); return false; }
        if (S.copiaExpo) { S.texExpo11 = Criar11(1, 1, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &S.hExpo); if (!S.texExpo11) S.copiaExpo = false; }
        if ((S.convDepth.ativo || S.convMv.ativo) && !Conv11Criar()) { S.convDepth.ativo = false; S.convMv.ativo = false; if (!DepthCopiavel(S.depthFmtJogo)) S.copiaDepth = false; if (S.mvFmtJogo != DXGI_FORMAT_R16G16_FLOAT) S.copiaMv = false; }
    }
    b.width = S.w; b.height = S.h; b.color_fmt = S.corFmt; b.output_fmt = S.outFmt;
    b.target_width = S.ow; b.target_height = S.oh;
    b.hdr = (S.flagsNgx & NGX_HDR) ? 1 : 0;
    b.depth_inverted = S.depthInv;
    b.flags_override = (int32_t)S.flagsNgx;
    b.mv_scale_x = S.mvScaleX; b.mv_scale_y = S.mvScaleY;
    for (int i = 0; i < SLOTS; ++i) b.tex[i] = ComoValor(S.htex[i]);
    b.panel_tex = S.copiaExpo ? ComoValor(S.hExpo) : 0;
    b.client_flags = kBuildSr;   /* sem ASYNC_HOME: sincrono */

    const BYTE tag = 'B'; FeedBuildAck ack = {};
    { std::lock_guard<std::mutex> l(Ponte::PipeUp::Mutex()); if (!Escrever(&tag, 1) || !Escrever(&b, sizeof b) || !LerPipe(&ack, sizeof ack)) { Falhar("troca do Build falhou"); return false; } }
    if (ack.ok != 1) { Falhar("host recusou o Build 0x200 (app antigo, ou nativo_sr=0)"); return false; }
    hostEntendeX = (ack.flags & kAckUp) != 0;
    hostEntendeS = (ack.flags & kAckSr) != 0;
    if (!hostEntendeS) { Falhar("host aceitou o Build mas nao entende 0x200 (sem FEED_ACK_SR)"); return false; }

    HRESULT h1, h2;
    if (S.d3d12)
    {
        h1 = S.dev->OpenSharedHandle(ComoHandle(ack.fence_in), __uuidof(ID3D12Fence), reinterpret_cast<void**>(&S.fIn));
        h2 = S.dev->OpenSharedHandle(ComoHandle(ack.fence_out), __uuidof(ID3D12Fence), reinterpret_cast<void**>(&S.fOut));
    }
    else
    {
        if (S.dev5 == nullptr && FAILED(S.dev11->QueryInterface(__uuidof(ID3D11Device5), reinterpret_cast<void**>(&S.dev5)))) { Falhar("sem ID3D11Device5"); return false; }
        h1 = S.dev5->OpenSharedFence(ComoHandle(ack.fence_in), __uuidof(ID3D11Fence), reinterpret_cast<void**>(&S.fIn11));
        h2 = S.dev5->OpenSharedFence(ComoHandle(ack.fence_out), __uuidof(ID3D11Fence), reinterpret_cast<void**>(&S.fOut11));
        if (S.devAux == nullptr) D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(&S.devAux));
        if (S.devAux && FAILED(S.devAux->OpenSharedHandle(ComoHandle(ack.fence_out), __uuidof(ID3D12Fence), reinterpret_cast<void**>(&S.fOutAux))))
        { S.fOutAux = nullptr; L("sr: sem fence auxiliar D3D12; o vigia nao podera destravar um atraso em D3D11"); }
    }
    if (FAILED(h1) || FAILED(h2)) { Falhar("abrir as fences do host falhou"); return false; }

    L("sr: sessao %ux%u -> %ux%u api=%s %s cor=%u saida=%u mv=%s(%ux%u fmt %u) depth=%s(fmt %u) expo=%s flags_ngx=0x%X depth_inv=%d mvscale=%.3f,%.3f prova=%s",
      S.w, S.h, S.ow, S.oh, NomeApi(S.api), S.d3d12 ? "D3D12" : "D3D11", (unsigned)S.corFmt, (unsigned)S.outFmt,
      S.copiaMv ? (S.convMv.ativo ? "convertida" : "copiada") : "ZERADA", S.mvW, S.mvH, (unsigned)S.mvFmtJogo,
      S.copiaDepth ? (S.convDepth.ativo ? "convertida" : "copiada") : "ZERADA", (unsigned)S.depthFmtJogo,
      S.copiaExpo ? "textura 1x1" : "auto", S.flagsNgx, S.depthInv, S.mvScaleX, S.mvScaleY,
      (S.d3d12 && S.provaA) ? "8 quadros antes de trocar" : (S.d3d12 ? "SEM prova" : "nao precisa (D3D11)"));
    S.provaEtapa = 0; S.provaIguais = 0; S.provaTotal = 0; S.provaFeita = !(S.d3d12 && S.provaA);
    trocando = S.provaFeita;
    primeiroRetorno = false;
    ++estadoVersao;
    return true;
}

/* ------------------------------------------------------------------ */
/* Vigia                                                               */
/* ------------------------------------------------------------------ */
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
        if (estado.load() != E_PRONTO) { DestravarFila(); continue; }
        bool inOk = false;
        if (S.d3d12 && S.fIn) { if (S.fIn->GetCompletedValue() >= n) inOk = true; else if (SUCCEEDED(S.fIn->SetEventOnCompletion(n, evVigiaIn))) inOk = WaitForSingleObject(evVigiaIn, 1000) == WAIT_OBJECT_0; }
        else if (!S.d3d12 && S.fIn11) { if (S.fIn11->GetCompletedValue() >= n) inOk = true; else if (SUCCEEDED(S.fIn11->SetEventOnCompletion(n, evVigiaIn))) inOk = WaitForSingleObject(evVigiaIn, 1000) == WAIT_OBJECT_0; }
        if (!inOk) continue;
        const int64_t t0 = Agora();
        const bool conta = trocando.load() && primeiroRetorno;
        const DWORD limite = primeiroRetorno ? 50 : 5000;   /* a feature DLSS SR do app leva ~1-2 s para nascer */
        bool outOk = false;
        if (S.d3d12 && S.fOut) { if (S.fOut->GetCompletedValue() >= n) outOk = true; else if (SUCCEEDED(S.fOut->SetEventOnCompletion(n, evVigiaOut))) outOk = WaitForSingleObject(evVigiaOut, limite) == WAIT_OBJECT_0; }
        else if (!S.d3d12 && S.fOut11) { if (S.fOut11->GetCompletedValue() >= n) outOk = true; else if (SUCCEEDED(S.fOut11->SetEventOnCompletion(n, evVigiaOut))) outOk = WaitForSingleObject(evVigiaOut, limite) == WAIT_OBJECT_0; }
        esperaMedidaMs = Ms(t0, Agora());
        if (outOk && !primeiroRetorno) { primeiroRetorno = true; L("sr: primeiro quadro devolvido pelo app (n=%llu) em %.1f ms", (unsigned long long)n, esperaMedidaMs.load()); }
        if (!outOk && !conta) { if (S.d3d12 && S.fOut) S.fOut->Signal(n); else if (S.fOutAux) S.fOutAux->Signal(n); }
        else if (!outOk)
        {
            if (S.d3d12 && S.fOut) S.fOut->Signal(n); else if (S.fOutAux) S.fOutAux->Signal(n);
            const unsigned a = ++atrasadosTotal;
            L("sr: app nao devolveu o quadro %llu em 50 ms (atraso %u); fence de saida sinalizada por CPU", (unsigned long long)n, a);
            if (a >= 5) Falhar("app nao devolveu a tempo 5 vezes");
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Por quadro                                                          */
/* ------------------------------------------------------------------ */
D3D12_RESOURCE_BARRIER Barr(ID3D12Resource* r, D3D12_RESOURCE_STATES de, D3D12_RESOURCE_STATES para)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = de; b.Transition.StateAfter = para; return b;
}
void CopiarRegiao(ID3D12GraphicsCommandList* l, ID3D12Resource* dst, unsigned dx, unsigned dy, ID3D12Resource* src, unsigned bx, unsigned by, unsigned w, unsigned h)
{
    D3D12_TEXTURE_COPY_LOCATION d = {}; d.pResource = dst; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION s = {}; s.pResource = src; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.SubresourceIndex = 0;
    D3D12_BOX box = { bx, by, 0, bx + w, by + h, 1 };
    l->CopyTextureRegion(&d, dx, dy, 0, &s, &box);
}
void MedirJanela(int64_t agora)
{
    if (jan.n < 300) return;
    const double seg = Ms(jan.inicio, agora) / 1000.0;
    L("sr: 300 dispatches | copia-in %.3f ms | espera %.3f ms | troca %.3f ms | %.1f disp/s | atrasados %u | troca=%s | total %u (trocados %u)",
      jan.copia / jan.n, jan.espera / jan.n, jan.troca / jan.n, seg > 0 ? jan.n / seg : 0.0, atrasadosTotal.load(), trocando.load() ? "ligada" : "desligada", dispatches.load(), trocados.load());
    jan = Janela();
}
bool MandarQuadro(const Desc& d, uint64_t n)
{
    unsigned flags = 0;
    if (d.reset) flags |= 1;
    if (S.copiaExpo && d.expo) flags |= 2;
    if (d.sharpen) flags |= 4;
    std::lock_guard<std::mutex> l(Ponte::PipeUp::Mutex());
    if (hostEntendeX) { const BYTE tx = 'X'; FeedExtraMsg x = { n, d.jx, d.jy, d.msx, d.msy, d.pre, d.escExpo, d.dt, flags }; if (!Escrever(&tx, 1) || !Escrever(&x, sizeof x)) { Falhar("pipe fechado"); return false; } }
    if (hostEntendeS) { const BYTE ts = 'S'; FeedSrMsg s = { n, d.rw, d.rh, d.sharp, d.sharpen ? 1u : 0u }; if (!Escrever(&ts, 1) || !Escrever(&s, sizeof s)) { Falhar("pipe fechado"); return false; } }
    const BYTE tf = 'F'; FeedFrameMsg fm = { n, (uint32_t)(d.reset ? 1 : 0), d.jx, d.jy };
    if (!Escrever(&tf, 1) || !Escrever(&fm, sizeof fm)) { Falhar("pipe fechado"); return false; }
    return true;
}
/* o dispatch cabe na sessao? (resolucao dinamica, formatos) */
bool CabeNaSessao(const Desc& d, IUnknown* corRes, DXGI_FORMAT corFmt, unsigned cw, unsigned ch, DXGI_FORMAT outFmt, unsigned outW, unsigned outH)
{
    (void)corRes;
    if (d.rw == 0 || d.rh == 0 || d.rw > S.w || d.rh > S.h) { AvisoUmaVez("renderSize mudou para fora da sessao (resolucao dinamica); este quadro passa direto e a sessao sera refeita"); estado = E_CONECTADO; return false; }
    if (Tipado(corFmt) != S.corFmt || cw < d.bxCor + d.rw || ch < d.byCor + d.rh) { AvisoUmaVez("cor do jogo com formato/tamanho diferente da sessao; passa direto e a sessao sera refeita"); estado = E_CONECTADO; return false; }
    const unsigned ow = d.ow ? d.ow : outW, oh = d.oh ? d.oh : outH;
    if (Tipado(outFmt) != S.outFmt || ow != S.ow || oh != S.oh || outW < d.bxOut + ow || outH < d.byOut + oh) { AvisoUmaVez("saida do jogo com formato/tamanho diferente da sessao; passa direto e a sessao sera refeita"); estado = E_CONECTADO; return false; }
    return true;
}

/* J3 (18/09/2026): as mesmas copias que vao para o app tambem servem ao frame generation da DLL,
   que roda no MESMO processo, device e fila. Nada de novo por quadro: os slots ja foram escritos
   logo acima; aqui so se avisa onde eles estao e com que convencao. */
void PublicarParaFg(const Desc& d)
{
    if (!Fg::QuerDados() || !S.d3d12) return;
    if (!(S.copiaMv && d.mv) || !(S.copiaDepth && d.depth)) return;   /* sem os dois, o FG fica no fluxo optico */
    if (!S.tex[SLOT_DEPTH] || !S.tex[SLOT_MV] || !S.fila) return;
    Fg::DadosJogo p;
    p.depth = S.tex[SLOT_DEPTH]; p.mv = S.tex[SLOT_MV]; p.fila = S.fila;
    p.renderW = d.rw; p.renderH = d.rh;
    const bool mvTela = (S.flagsNgx & NGX_MV_LOW_RES) == 0;
    /* com MV em resolucao de render o slot e escrito no tamanho DESTE quadro (resolucao dinamica) */
    p.mvW = mvTela ? S.mvW : d.rw; p.mvH = mvTela ? S.mvH : d.rh;
    p.jitterX = d.jx; p.jitterY = d.jy; p.mvScaleX = d.msx; p.mvScaleY = d.msy; p.dtMs = d.dt;
    p.camNear = d.camNear; p.camFar = d.camFar; p.camFov = d.camFov; p.viewToM = d.viewToM;
    p.depthInv = d.depthInv;
    p.mvDisplayRes = mvTela; p.mvJittered = (S.flagsNgx & NGX_MV_JITTERED) != 0; p.mvNdc = d.mvNdc;
    p.reset = d.reset;
    p.origem = d.api == API_FFX ? "fsr31" : d.api == API_FSR2 ? "fsr2" : d.api == API_XESS ? "xess" : "-";
    Fg::PublicarDados(p);
}

/* J5 (18/09/2026): o mesmo aviso, para a PONTE DA FASE 2 (o app, pela pelicula), nos dois
   backends. Mesmos slots, mesma convencao do J3; quem copia para os slots da fase 2 e o Present
   da ponte (ponte_cliente.cpp), no mesmo contexto/fila em que estas copias foram gravadas. */
void PublicarParaPonte(const Desc& d)
{
    if (!Ponte::Dados::Quer()) return;
    if (!(S.copiaMv && d.mv) || !(S.copiaDepth && d.depth)) return;   /* sem os dois, o app fica no fluxo optico */
    Ponte::Dados::Pacote p;
    if (S.d3d12) { if (!S.tex[SLOT_DEPTH] || !S.tex[SLOT_MV] || !S.fila) return; p.depth12 = S.tex[SLOT_DEPTH]; p.mv12 = S.tex[SLOT_MV]; p.fila = S.fila; }
    else         { if (!S.tex11[SLOT_DEPTH] || !S.tex11[SLOT_MV]) return; p.depth11 = S.tex11[SLOT_DEPTH]; p.mv11 = S.tex11[SLOT_MV]; }
    p.renderW = d.rw; p.renderH = d.rh;
    const bool mvTela = (S.flagsNgx & NGX_MV_LOW_RES) == 0;
    p.mvW = mvTela ? S.mvW : d.rw; p.mvH = mvTela ? S.mvH : d.rh;
    p.jitterX = d.jx; p.jitterY = d.jy; p.mvScaleX = d.msx; p.mvScaleY = d.msy; p.dtMs = d.dt;
    p.camNear = d.camNear; p.camFar = d.camFar; p.camFov = d.camFov; p.viewToM = d.viewToM;
    p.depthInv = d.depthInv;
    p.mvDisplayRes = mvTela; p.mvJittered = (S.flagsNgx & NGX_MV_JITTERED) != 0; p.mvNdc = d.mvNdc;
    p.reset = d.reset;
    p.origem = d.api == API_FFX ? 2u : d.api == API_FSR2 ? 3u : d.api == API_XESS ? 4u : 0u;
    Ponte::Dados::Publicar(p);
}

bool AntesD12(const Desc& d)
{
    if (estado.load() != E_PRONTO || !S.d3d12 || S.fila == nullptr || d.lista == nullptr || d.cor == nullptr || d.out == nullptr) return false;
    ID3D12GraphicsCommandList* lista = static_cast<ID3D12GraphicsCommandList*>(d.lista);
    ID3D12Resource* cor = static_cast<ID3D12Resource*>(d.cor); ID3D12Resource* out = static_cast<ID3D12Resource*>(d.out);
    ID3D12Resource* mv = static_cast<ID3D12Resource*>(d.mv); ID3D12Resource* depth = static_cast<ID3D12Resource*>(d.depth); ID3D12Resource* expo = static_cast<ID3D12Resource*>(d.expo);
    const D3D12_RESOURCE_DESC cd = cor->GetDesc(), od = out->GetDesc();
    if (!CabeNaSessao(d, cor, cd.Format, (unsigned)cd.Width, cd.Height, od.Format, (unsigned)od.Width, od.Height)) return false;
    const D3D12_RESOURCE_STATES eCor = ctl.estadoIn ? (D3D12_RESOURCE_STATES)ctl.estadoIn : d.eCor;
    const D3D12_RESOURCE_STATES eMv = ctl.estadoIn ? (D3D12_RESOURCE_STATES)ctl.estadoIn : d.eMv;
    const D3D12_RESOURCE_STATES eDp = ctl.estadoIn ? (D3D12_RESOURCE_STATES)ctl.estadoIn : d.eDepth;
    const D3D12_RESOURCE_STATES eEx = ctl.estadoIn ? (D3D12_RESOURCE_STATES)ctl.estadoIn : d.eExpo;
    const D3D12_RESOURCE_STATES eOut = ctl.estadoOut ? (D3D12_RESOURCE_STATES)ctl.estadoOut : d.eOut;

    const int64_t t0 = Agora();
    S.ctxAtual = d.ctx; S.ctxSolto = false; S.qpcUltimo = t0;   /* X-FG-6: este contexto esta vivo e servido */
    const uint64_t n = S.n = Ponte::PipeUp::ProximoN();   /* X-FG-6: contador unico das duas sessoes */
    const unsigned idx = (unsigned)(n % 3);
    if (S.fenceDoBuf[idx] != 0 && S.fRecicla->GetCompletedValue() < S.fenceDoBuf[idx])
    { if (SUCCEEDED(S.fRecicla->SetEventOnCompletion(S.fenceDoBuf[idx], S.evRecicla))) WaitForSingleObject(S.evRecicla, 1000); }
    if (FAILED(S.alloc[idx]->Reset()) || FAILED(S.lista->Reset(S.alloc[idx], nullptr))) { Falhar("Reset da lista de copia falhou"); return false; }

    const bool provaAgora = !trocando.load() && S.provaA != nullptr && !S.provaFeita && S.provaEtapa == 0;
    const bool copiaMvDireta = S.copiaMv && mv && !S.convMv.ativo;
    const bool copiaDepthDireta = S.copiaDepth && depth && !S.convDepth.ativo;
    const bool copiaExpo = S.copiaExpo && expo != nullptr;

    /* --- nossa lista: cor (e MV, depth, expo) do jogo -> slots --- */
    D3D12_RESOURCE_BARRIER b1[6]; UINT nb = 0;
    b1[nb++] = Barr(cor, eCor, D3D12_RESOURCE_STATE_COPY_SOURCE);
    b1[nb++] = Barr(S.tex[SLOT_COLOR], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    if (copiaMvDireta) { b1[nb++] = Barr(mv, eMv, D3D12_RESOURCE_STATE_COPY_SOURCE); b1[nb++] = Barr(S.tex[SLOT_MV], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST); }
    if (copiaDepthDireta) { b1[nb++] = Barr(depth, eDp, D3D12_RESOURCE_STATE_COPY_SOURCE); b1[nb++] = Barr(S.tex[SLOT_DEPTH], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST); }
    S.lista->ResourceBarrier(nb, b1);
    CopiarRegiao(S.lista, S.tex[SLOT_COLOR], 0, 0, cor, d.bxCor, d.byCor, d.rw, d.rh);
    if (provaAgora)
    {
        D3D12_RESOURCE_BARRIER pa = Barr(S.provaA, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        S.lista->ResourceBarrier(1, &pa);
        CopiarRegiao(S.lista, S.provaA, 0, 0, cor, d.bxCor, d.byCor, d.rw, d.rh);
        D3D12_RESOURCE_BARRIER pa2 = Barr(S.provaA, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        S.lista->ResourceBarrier(1, &pa2);
    }
    if (copiaMvDireta) { const D3D12_RESOURCE_DESC md = mv->GetDesc(); const unsigned mw = S.mvW < md.Width - d.bxMv ? S.mvW : (unsigned)md.Width - d.bxMv, mh = S.mvH < md.Height - d.byMv ? S.mvH : md.Height - d.byMv; CopiarRegiao(S.lista, S.tex[SLOT_MV], 0, 0, mv, d.bxMv, d.byMv, mw, mh); }
    if (copiaDepthDireta) CopiarRegiao(S.lista, S.tex[SLOT_DEPTH], 0, 0, depth, d.bxDepth, d.byDepth, d.rw, d.rh);
    D3D12_RESOURCE_BARRIER b2[6]; nb = 0;
    b2[nb++] = Barr(cor, D3D12_RESOURCE_STATE_COPY_SOURCE, eCor);
    b2[nb++] = Barr(S.tex[SLOT_COLOR], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    if (copiaMvDireta) { b2[nb++] = Barr(mv, D3D12_RESOURCE_STATE_COPY_SOURCE, eMv); b2[nb++] = Barr(S.tex[SLOT_MV], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON); }
    if (copiaDepthDireta) { b2[nb++] = Barr(depth, D3D12_RESOURCE_STATE_COPY_SOURCE, eDp); b2[nb++] = Barr(S.tex[SLOT_DEPTH], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON); }
    S.lista->ResourceBarrier(nb, b2);
    if (S.copiaMv && mv && S.convMv.ativo) Converter12(S.convMv, mv, eMv, S.tex[SLOT_MV], 2, d.bxMv, d.byMv, S.mvW, S.mvH);
    if (S.copiaDepth && depth && S.convDepth.ativo) Converter12(S.convDepth, depth, eDp, S.tex[SLOT_DEPTH], 0, d.bxDepth, d.byDepth, d.rw, d.rh);
    if (copiaExpo)
    {
        D3D12_RESOURCE_BARRIER e1[2] = { Barr(expo, eEx, D3D12_RESOURCE_STATE_COPY_SOURCE), Barr(S.texExpo, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST) };
        S.lista->ResourceBarrier(2, e1);
        CopiarRegiao(S.lista, S.texExpo, 0, 0, expo, 0, 0, 1, 1);
        D3D12_RESOURCE_BARRIER e2[2] = { Barr(expo, D3D12_RESOURCE_STATE_COPY_SOURCE, eEx), Barr(S.texExpo, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON) };
        S.lista->ResourceBarrier(2, e2);
    }
    /* readback das duas copias do quadro ANTERIOR (a de dentro da lista do jogo ja executou) */
    if (S.provaEtapa == 1 && S.rbA)
    {
        D3D12_TEXTURE_COPY_LOCATION sa = {}; sa.pResource = S.provaA; sa.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION sb = {}; sb.pResource = S.provaB; sb.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION da = {}; da.pResource = S.rbA; da.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; da.PlacedFootprint = S.fp;
        D3D12_TEXTURE_COPY_LOCATION db = {}; db.pResource = S.rbB; db.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; db.PlacedFootprint = S.fp;
        S.lista->CopyTextureRegion(&da, 0, 0, 0, &sa, nullptr);
        S.lista->CopyTextureRegion(&db, 0, 0, 0, &sb, nullptr);
    }
    if (FAILED(S.lista->Close())) { Falhar("Close da lista de copia falhou"); return false; }
    ID3D12CommandList* ls[1] = { S.lista };
    S.fila->ExecuteCommandLists(1, ls);
    if (S.provaEtapa == 1) { S.fila->Signal(S.fProva, ++S.provaVal); S.provaEtapa = 2; }
    S.fila->Signal(S.fIn, n);
    S.fila->Signal(S.fRecicla, ++S.reciclaVal); S.fenceDoBuf[idx] = S.reciclaVal;
    PublicarParaFg(d);   /* J3: os slots deste quadro estao submetidos; o FG pode le-los */
    PublicarParaPonte(d);   /* J5: idem para a ponte da fase 2 */
    const int64_t t1 = Agora();

    /* --- dentro da lista do jogo (prova): segunda copia da cor, antes do dispatch original --- */
    if (provaAgora && S.provaB)
    {
        D3D12_RESOURCE_BARRIER pb[2] = { Barr(cor, eCor, D3D12_RESOURCE_STATE_COPY_SOURCE), Barr(S.provaB, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST) };
        lista->ResourceBarrier(2, pb);
        CopiarRegiao(lista, S.provaB, 0, 0, cor, d.bxCor, d.byCor, d.rw, d.rh);
        D3D12_RESOURCE_BARRIER pb2[2] = { Barr(cor, D3D12_RESOURCE_STATE_COPY_SOURCE, eCor), Barr(S.provaB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON) };
        lista->ResourceBarrier(2, pb2);
        S.provaEtapa = 1;
    }

    if (!MandarQuadro(d, n)) return false;
    vigiaN = n; SetEvent(evVigia);
    jan.n++; jan.copia += Ms(t0, t1); jan.espera += esperaMedidaMs.load();
    if (jan.n == 1) jan.inicio = t0;

    if (!trocando.load()) { MedirJanela(t1); return false; }   /* prova em curso: original intocada */

    /* --- espera de GPU na fila; a lista do jogo (submetida depois) so roda com o Output pronto --- */
    S.fila->Wait(S.fOut, n);
    const int64_t t2 = Agora();
    /* --- na lista do jogo: nosso Output (tamanho de saida) -> destino do upscaler --- */
    D3D12_RESOURCE_BARRIER o1[2] = { Barr(out, eOut, D3D12_RESOURCE_STATE_COPY_DEST), Barr(S.tex[SLOT_OUTPUT], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE) };
    lista->ResourceBarrier(2, o1);
    CopiarRegiao(lista, out, d.bxOut, d.byOut, S.tex[SLOT_OUTPUT], 0, 0, S.ow, S.oh);
    D3D12_RESOURCE_BARRIER o2[2] = { Barr(out, D3D12_RESOURCE_STATE_COPY_DEST, eOut), Barr(S.tex[SLOT_OUTPUT], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON) };
    lista->ResourceBarrier(2, o2);
    ++trocados;
    jan.troca += Ms(t2, Agora());
    MedirJanela(Agora());
    return true;
}

bool AntesD11(const Desc& d)
{
    if (estado.load() != E_PRONTO || S.d3d12 || S.ctx11 == nullptr || S.ctx4 == nullptr || d.cor == nullptr || d.out == nullptr) return false;
    ID3D11DeviceContext* ctx = S.ctx11;
    ID3D11Resource* cor = static_cast<ID3D11Resource*>(d.cor); ID3D11Resource* out = static_cast<ID3D11Resource*>(d.out);
    ID3D11Resource* mv = static_cast<ID3D11Resource*>(d.mv); ID3D11Resource* depth = static_cast<ID3D11Resource*>(d.depth); ID3D11Resource* expo = static_cast<ID3D11Resource*>(d.expo);
    D3D11_TEXTURE2D_DESC cd = {}, od = {};
    { ID3D11Texture2D* t = nullptr; if (SUCCEEDED(cor->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t))) && t) { t->GetDesc(&cd); t->Release(); } }
    { ID3D11Texture2D* t = nullptr; if (SUCCEEDED(out->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t))) && t) { t->GetDesc(&od); t->Release(); } }
    if (!CabeNaSessao(d, cor, cd.Format, cd.Width, cd.Height, od.Format, od.Width, od.Height)) return false;
    const int64_t t0 = Agora();
    S.ctxAtual = d.ctx; S.ctxSolto = false; S.qpcUltimo = t0;   /* X-FG-6: este contexto esta vivo e servido */
    const uint64_t n = S.n = Ponte::PipeUp::ProximoN();   /* X-FG-6: contador unico das duas sessoes */
    D3D11_BOX box = { d.bxCor, d.byCor, 0, d.bxCor + d.rw, d.byCor + d.rh, 1 };
    ctx->CopySubresourceRegion(S.tex11[SLOT_COLOR], 0, 0, 0, 0, cor, 0, &box);
    if (S.copiaMv && mv) { if (S.convMv.ativo) Converter11(ctx, S.convMv, mv, d.bxMv, d.byMv, S.mvW, S.mvH, "mv"); else { D3D11_BOX b = { d.bxMv, d.byMv, 0, d.bxMv + S.mvW, d.byMv + S.mvH, 1 }; ctx->CopySubresourceRegion(S.tex11[SLOT_MV], 0, 0, 0, 0, mv, 0, &b); } }
    if (S.copiaDepth && depth) { if (S.convDepth.ativo) Converter11(ctx, S.convDepth, depth, d.bxDepth, d.byDepth, d.rw, d.rh, "depth"); else { D3D11_BOX b = { d.bxDepth, d.byDepth, 0, d.bxDepth + d.rw, d.byDepth + d.rh, 1 }; ctx->CopySubresourceRegion(S.tex11[SLOT_DEPTH], 0, 0, 0, 0, depth, 0, &b); } }
    if (S.copiaExpo && expo) { D3D11_BOX b = { 0, 0, 0, 1, 1, 1 }; ctx->CopySubresourceRegion(S.texExpo11, 0, 0, 0, 0, expo, 0, &b); }
    S.ctx4->Signal(S.fIn11, n);
    ctx->Flush();
    PublicarParaPonte(d);   /* J5: os slots deste quadro estao gravados no contexto imediato; a ponte os copia no Present */
    const int64_t t1 = Agora();
    if (!MandarQuadro(d, n)) return false;
    vigiaN = n; SetEvent(evVigia);
    jan.n++; jan.copia += Ms(t0, t1); jan.espera += esperaMedidaMs.load(); if (jan.n == 1) jan.inicio = t0;
    if (!trocando.load()) { MedirJanela(t1); return false; }
    S.ctx4->Wait(S.fOut11, n);
    const int64_t t2 = Agora();
    D3D11_BOX ob = { 0, 0, 0, S.ow, S.oh, 1 };
    ctx->CopySubresourceRegion(out, 0, d.bxOut, d.byOut, 0, S.tex11[SLOT_OUTPUT], 0, &ob);
    ++trocados;
    jan.troca += Ms(t2, Agora()); MedirJanela(Agora());
    return true;
}

/* prova de prontidao (thread da ponte, estado PRONTO) */
void ConferirProva()
{
    if (S.provaFeita || !S.provaA || !S.rbA) return;
    if (S.provaEtapa != 2 || S.fProva->GetCompletedValue() < S.provaVal) return;
    void* pa = nullptr; void* pb = nullptr;
    D3D12_RANGE r = { 0, (SIZE_T)S.rbBytes };
    bool iguais = false;
    if (SUCCEEDED(S.rbA->Map(0, &r, &pa)) && SUCCEEDED(S.rbB->Map(0, &r, &pb)))
    {
        const size_t linha = (size_t)S.w * BytesPorPixel(S.corFmt);
        iguais = true;
        for (unsigned y = 0; y < S.h && iguais; ++y)
            if (memcmp((const BYTE*)pa + (size_t)y * S.fp.Footprint.RowPitch, (const BYTE*)pb + (size_t)y * S.fp.Footprint.RowPitch, linha) != 0) iguais = false;
    }
    D3D12_RANGE nada = { 0, 0 };
    if (pa) S.rbA->Unmap(0, &nada);
    if (pb) S.rbB->Unmap(0, &nada);
    ++S.provaTotal; if (iguais) ++S.provaIguais;
    L("sr: prova de prontidao quadro %u/8: %s", S.provaTotal, iguais ? "iguais" : "DIFERENTE (a cena ainda nao tinha sido submetida quando o dispatch foi gravado)");
    S.provaEtapa = 0;
    if (S.provaTotal >= 8)
    {
        S.provaFeita = true;
        if (S.provaIguais == S.provaTotal) { trocando = true; ++estadoVersao; L("sr: prova de prontidao: %u/%u iguais -> sincrono POSSIVEL neste jogo; troca do Output LIGADA (a chamada original do upscaler passa a ser pulada)", S.provaIguais, S.provaTotal); }
        else Falhar("cena e dispatch na mesma command list: sincrono impossivel (prova de prontidao falhou)");
    }
}

/* ------------------------------------------------------------------ */
/* Colher do primeiro dispatch o que a sessao precisa                  */
/* ------------------------------------------------------------------ */
bool DescRes(IUnknown* r, bool d3d12, DXGI_FORMAT* fmt, unsigned* w, unsigned* h)
{
    if (r == nullptr) return false;
    if (d3d12) { ID3D12Resource* t = nullptr; if (FAILED(r->QueryInterface(__uuidof(ID3D12Resource), reinterpret_cast<void**>(&t))) || !t) return false; D3D12_RESOURCE_DESC d = t->GetDesc(); t->Release(); *fmt = d.Format; *w = (unsigned)d.Width; *h = d.Height; return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D; }
    ID3D11Texture2D* t = nullptr; if (FAILED(r->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t))) || !t) return false; D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); t->Release(); *fmt = d.Format; *w = d.Width; *h = d.Height; return true;
}

bool ColherDoDispatch()
{
    if (!temPrimeiro) return false;
    const Desc d = primeiro;
    DXGI_FORMAT cf = DXGI_FORMAT_UNKNOWN, of = DXGI_FORMAT_UNKNOWN, mf = DXGI_FORMAT_UNKNOWN, df = DXGI_FORMAT_UNKNOWN, ef = DXGI_FORMAT_UNKNOWN;
    unsigned cw = 0, ch = 0, ow = 0, oh = 0, mw = 0, mh = 0, dw = 0, dh = 0, ew = 0, eh = 0;
    if (!DescRes(d.cor, d.d3d12, &cf, &cw, &ch) || !DescRes(d.out, d.d3d12, &of, &ow, &oh)) { Falhar("cor/saida do dispatch nao sao texturas 2D"); return false; }
    S.api = d.api; S.d3d12 = d.d3d12;
    S.w = d.rw ? d.rw : cw; S.h = d.rh ? d.rh : ch;
    S.ow = d.ow ? d.ow : ow; S.oh = d.oh ? d.oh : oh;
    S.corFmt = Tipado(cf); S.outFmt = Tipado(of);
    if (!CorAceita(cf)) { char m[160]; snprintf(m, sizeof m, "formato da cor do jogo (%u) fora do aceito (28/87/24/10/26)", (unsigned)cf); Falhar(m); return false; }
    if (!SaidaAceita(of)) { char m[160]; snprintf(m, sizeof m, "formato da saida do jogo (%u) fora do aceito", (unsigned)of); Falhar(m); return false; }
    if (S.w == 0 || S.h == 0 || S.ow == 0 || S.oh == 0 || S.w > S.ow || S.h > S.oh) { Falhar("tamanhos interno/saida invalidos (interno maior que a saida?)"); return false; }
    S.flagsNgx = d.flagsNgx; S.depthInv = d.depthInv;
    if (EhHdr(cf)) S.flagsNgx |= NGX_HDR;
    S.mvScaleX = d.msx; S.mvScaleY = d.msy;
    /* MV */
    S.copiaMv = false; S.convMv = Conv(); S.mvW = S.w; S.mvH = S.h; S.mvFmtJogo = DXGI_FORMAT_UNKNOWN;
    if (DescRes(d.mv, d.d3d12, &mf, &mw, &mh))
    {
        S.mvFmtJogo = mf;
        const bool telaRes = (mw >= S.ow && mh >= S.oh && (S.ow > S.w || S.oh > S.h)) && !(S.flagsNgx & NGX_MV_LOW_RES);
        if (telaRes) { S.mvW = S.ow; S.mvH = S.oh; S.flagsNgx &= ~NGX_MV_LOW_RES; } else S.flagsNgx |= NGX_MV_LOW_RES;
        if (mw >= S.mvW && mh >= S.mvH)
        {
            if (mf == DXGI_FORMAT_R16G16_FLOAT) S.copiaMv = true;
            else if (MvSrvFmt(mf) != DXGI_FORMAT_UNKNOWN) { S.copiaMv = true; S.convMv.ativo = true; S.convMv.srvFmt = MvSrvFmt(mf); S.convMv.interFmt = mf; }
        }
    }
    /* depth */
    S.copiaDepth = false; S.convDepth = Conv(); S.depthFmtJogo = DXGI_FORMAT_UNKNOWN;
    if (DescRes(d.depth, d.d3d12, &df, &dw, &dh) && dw >= S.w && dh >= S.h)
    {
        S.depthFmtJogo = df;
        DXGI_FORMAT srv, tl;
        if (DepthCopiavel(df)) S.copiaDepth = true;
        else if (DepthConversivel(df, &srv, &tl)) { S.copiaDepth = true; S.convDepth.ativo = true; S.convDepth.srvFmt = srv; S.convDepth.interFmt = tl; S.convDepth.depthStencil = true; }
    }
    /* exposicao 1x1 (so R32) */
    S.copiaExpo = DescRes(d.expo, d.d3d12, &ef, &ew, &eh) && ew >= 1 && eh >= 1 && (ef == DXGI_FORMAT_R32_FLOAT || ef == DXGI_FORMAT_R32_TYPELESS);
    S.expoFmtJogo = ef;
    if (!S.copiaExpo) S.flagsNgx |= NGX_AUTO_EXPOSURE; else S.flagsNgx &= ~NGX_AUTO_EXPOSURE;
    /* device */
    if (S.d3d12)
    {
        ID3D12Resource* t = nullptr; if (FAILED(d.cor->QueryInterface(__uuidof(ID3D12Resource), reinterpret_cast<void**>(&t))) || !t) return false;
        Soltar(S.dev); t->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&S.dev)); t->Release();
        if (S.dev == nullptr) return false;
        if (S.fila == nullptr) { AvisoUmaVez("fila do dispatch ainda nao vista (ExecuteCommandLists); esperando"); return false; }
    }
    else
    {
        ID3D11Resource* t = nullptr; if (FAILED(d.cor->QueryInterface(__uuidof(ID3D11Resource), reinterpret_cast<void**>(&t))) || !t) return false;
        Soltar(S.ctx4); Soltar(S.ctx11); Soltar(S.dev11);
        t->GetDevice(&S.dev11); t->Release();
        if (S.dev11 == nullptr) return false;
        S.dev11->GetImmediateContext(&S.ctx11);
        if (S.ctx11) S.ctx11->QueryInterface(__uuidof(ID3D11DeviceContext4), reinterpret_cast<void**>(&S.ctx4));
        if (S.ctx4 == nullptr) { Falhar("D3D11 sem ID3D11DeviceContext4 (fences)"); return false; }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Os hooks                                                            */
/* ------------------------------------------------------------------ */
Desc ultimo;   /* o ultimo dispatch visto (para refazer a sessao sem esperar o proximo) */
void Observar(const Desc& d, void* lista)
{
    const unsigned k = ++dispatches;
    S.ultimaLista = lista;
    { std::lock_guard<std::mutex> g(mu); ultimo = d; if (!temPrimeiro || estado.load() == E_ESPERANDO) { primeiro = d; temPrimeiro = true; } }
    if (k == 1 || k == 100 || (k % 1000) == 0)
    {
        DXGI_FORMAT cf = DXGI_FORMAT_UNKNOWN, of = DXGI_FORMAT_UNKNOWN, mf = DXGI_FORMAT_UNKNOWN, df = DXGI_FORMAT_UNKNOWN; unsigned cw = 0, ch = 0, ow = 0, oh = 0, mw = 0, mh = 0, dw = 0, dh = 0;
        DescRes(d.cor, d.d3d12, &cf, &cw, &ch); DescRes(d.out, d.d3d12, &of, &ow, &oh); DescRes(d.mv, d.d3d12, &mf, &mw, &mh); DescRes(d.depth, d.d3d12, &df, &dw, &dh);
        L("sr: dispatch #%u api=%s %s render=%ux%u cor=%u@%ux%u mv=%u@%ux%u depth=%u@%ux%u out=%u@%ux%u expo=%s jitter=%.4f,%.4f mvscale=%.3f,%.3f dt=%.2f pre=%.3f reset=%d sharp=%d/%.2f flags_ctx=0x%X(%s) depth_inv=%d",
          k, NomeApi(d.api), d.d3d12 ? "D3D12" : "D3D11", d.rw, d.rh, (unsigned)cf, cw, ch, (unsigned)mf, mw, mh, (unsigned)df, dw, dh, (unsigned)of, ow, oh, d.expo ? "sim" : "-",
          d.jx, d.jy, d.msx, d.msy, d.dt, d.pre, d.reset ? 1 : 0, d.sharpen ? 1 : 0, d.sharp, d.flagsNgx, d.ctxVisto ? "criacao vista" : "contexto anterior ao hook", d.depthInv);
    }
}
/* X-FG-6: SEM a sessao .up (Nativo comum, app sem servir o .up, sessao caida ou passada ao
   ngx_upstream, sr=0), depth e MV do jogo vao mesmo assim para o FG da DLL e para a pelicula -
   pela colheita (dados/colheita.h), que grava as copias NA LISTA DO JOGO antes do dispatch e
   nao troca imagem nenhuma. A convencao e a mesma do PublicarParaPonte. */
ID3D11DeviceContext* ctxColheita11 = nullptr;   /* o contexto imediato, quando o dispatch D3D11 nao o traz */
void ColherSemSessao(const Desc& d)
{
    if (!Ponte::Colheita::AlguemQuer() || d.depth == nullptr || d.mv == nullptr || d.cor == nullptr) return;
    Ponte::Colheita::Entrada e;
    e.d3d12 = d.d3d12;
    if (d.d3d12)
    {
        e.lista = static_cast<ID3D12GraphicsCommandList*>(d.lista);
        e.fila = S.fila;   /* nulo antes do primeiro ExecuteCommandLists visto: a colheita recusa */
        e.eDepth = ctl.estadoIn ? (D3D12_RESOURCE_STATES)ctl.estadoIn : d.eDepth;
        e.eMv = ctl.estadoIn ? (D3D12_RESOURCE_STATES)ctl.estadoIn : d.eMv;
    }
    else
    {
        e.ctx = static_cast<ID3D11DeviceContext*>(d.lista);
        if (e.ctx == nullptr)
        {
            if (ctxColheita11 == nullptr) { ID3D11Resource* t = nullptr; ID3D11Device* dev = nullptr; if (SUCCEEDED(d.cor->QueryInterface(__uuidof(ID3D11Resource), reinterpret_cast<void**>(&t))) && t) { t->GetDevice(&dev); t->Release(); } if (dev) { dev->GetImmediateContext(&ctxColheita11); dev->Release(); } }
            e.ctx = ctxColheita11;
        }
    }
    e.depth = d.depth; e.mv = d.mv;
    e.rw = d.rw; e.rh = d.rh;
    DXGI_FORMAT f = DXGI_FORMAT_UNKNOWN; unsigned ow = d.ow, oh = d.oh, mw = 0, mh = 0;
    if ((ow == 0 || oh == 0) && !DescRes(d.out, d.d3d12, &f, &ow, &oh)) return;
    if (!DescRes(d.mv, d.d3d12, &f, &mw, &mh)) return;
    e.outW = ow; e.outH = oh;
    e.bxDepth = d.bxDepth; e.byDepth = d.byDepth; e.bxMv = d.bxMv; e.byMv = d.byMv;
    /* MV em resolucao de tela: a mesma regra do ColherDoDispatch (o recurso cabe na saida e a saida e maior que o render) */
    e.mvDisplayRes = (mw >= ow && mh >= oh && (ow > d.rw || oh > d.rh)) && !(d.flagsNgx & NGX_MV_LOW_RES);
    e.mvJittered = (d.flagsNgx & NGX_MV_JITTERED) != 0; e.mvNdc = d.mvNdc; e.reset = d.reset;
    e.depthInv = d.depthInv;
    e.jx = d.jx; e.jy = d.jy; e.msx = d.msx; e.msy = d.msy; e.dtMs = d.dt;
    e.camNear = d.camNear; e.camFar = d.camFar; e.camFov = d.camFov; e.viewToM = d.viewToM;
    e.origem = d.api == API_FFX ? 2u : d.api == API_FSR2 ? 3u : d.api == API_XESS ? 4u : 0u;
    e.origemTxt = d.api == API_FFX ? "fsr31" : d.api == API_FSR2 ? "fsr2" : d.api == API_XESS ? "xess" : "-";
    Ponte::Colheita::Publicar(e);
}

bool Antes(const Desc& d)
{
    if (!ctl.sr || estado.load() != E_PRONTO) { ColherSemSessao(d); return false; }
    return d.d3d12 ? AntesD12(d) : AntesD11(d);
}

/* --- ffx_api (FSR 3.1+), um jogo de trampolins por modulo ---
   H8 (17/09/2026): o FidelityFX SDK 2.x (RE requiem: loader 2.1.0.604) partiu a DLL unica do
   SDK 1.1 em um LOADER pequeno (`amd_fidelityfx_loader_dx12.dll`, o que o jogo chama) e um
   modulo por efeito (`amd_fidelityfx_upscaler_dx12.dll`, `..._framegeneration_dx12.dll`). Os
   cinco exports do ffx_api sao os MESMOS nos dois (ffxConfigure, ffxCreateContext,
   ffxDestroyContext, ffxDispatch, ffxQuery) e o layout do `ffxDispatchDescUpscale` tambem: no
   upscaler 2.1 o validador le renderSize em +0x178/+0x17C, upscaleSize em +0x180/+0x184,
   jitterOffset em +0x168, motionVectorScale em +0x170, sharpness em +0x18C e frameTimeDelta em
   +0x190 - exatamente os deslocamentos do cabecalho do SDK 1.1 que este arquivo usa.
   Enganchamos o LOADER (a camada que o jogo chama, uma so passagem). O modulo do upscaler entra
   na lista para o caso de um jogo que o carregue sem o loader, e e PULADO quando o loader esta
   no processo - senao o mesmo dispatch passaria duas vezes pelo hook.
   Slots: 0 = SDK 1.1 dx12, 1 = SDK 1.1 dx11, 2 = SDK 2.x loader dx12, 3 = SDK 2.x upscaler dx12. */
const int kFfxSlots = 4;
const bool kFfxD3d11[kFfxSlots] = { false, true, false, false };
PfnFfxCreateContext oFfxCreate[kFfxSlots] = {}; PfnFfxDestroyContext oFfxDestroy[kFfxSlots] = {}; PfnFfxDispatch oFfxDispatch[kFfxSlots] = {};
unsigned FlagsNgxDeFfx(unsigned f)
{
    unsigned n = 0;
    if (f & FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE) n |= NGX_HDR;
    if (!(f & FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS)) n |= NGX_MV_LOW_RES;
    if (f & FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION) n |= NGX_MV_JITTERED;
    if (f & FFX_UPSCALE_ENABLE_DEPTH_INVERTED) n |= NGX_DEPTH_INVERTED;
    if (f & FFX_UPSCALE_ENABLE_AUTO_EXPOSURE) n |= NGX_AUTO_EXPOSURE;
    return n;
}
template <int M> ffxReturnCode_t HkFfxCreate(ffxContext* ctx, ffxCreateContextDescHeader* desc, const ffxAllocationCallbacks* cb)
{
    Voo v;
    const ffxReturnCode_t r = oFfxCreate[M](ctx, desc, cb);
    try
    {
    if (r == FFX_API_RETURN_OK && ctx && *ctx && desc)
        for (ffxApiHeader* h = desc; h; h = h->pNext)
            if (h->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE)
            {
                const ffxCreateContextDescUpscale* u = reinterpret_cast<const ffxCreateContextDescUpscale*>(h);
                std::lock_guard<std::mutex> g(mu);
                CtxRec* c = RegistrarCtx(*ctx, API_FFX);
                c->flags = u->flags; c->maxRw = u->maxRenderSize.width; c->maxRh = u->maxRenderSize.height; c->outW = u->maxUpscaleSize.width; c->outH = u->maxUpscaleSize.height; c->d3d11 = kFfxD3d11[M];
                L("sr: ffxCreateContext(UPSCALE) %s ctx=%p flags=0x%X maxRender=%ux%u maxUpscale=%ux%u", kFfxD3d11[M] ? "D3D11" : "D3D12", *ctx, u->flags, c->maxRw, c->maxRh, c->outW, c->outH);
            }
    }
    catch (...) { Excecao("ffxCreateContext"); }   /* C7 */
    return r;
}
template <int M> ffxReturnCode_t HkFfxDestroy(ffxContext* ctx, const ffxAllocationCallbacks* cb)
{
    Voo v;
    try { if (ctx && *ctx) { std::lock_guard<std::mutex> g(mu); EsquecerCtx(*ctx); ContextoDestruido(*ctx); } }
    catch (...) { Excecao("ffxDestroyContext"); }   /* C7 */
    return oFfxDestroy[M](ctx, cb);
}
template <int M> ffxReturnCode_t HkFfxDispatch(ffxContext* ctx, const ffxDispatchDescHeader* desc)
{
    Voo v;
    try   /* C7: so a nossa parte; excecao cai na chamada original */
    {
    if (!parado.load() && desc && desc->type == FFX_API_DISPATCH_DESC_TYPE_UPSCALE && ctx)
    {
        const ffxDispatchDescUpscale* u = reinterpret_cast<const ffxDispatchDescUpscale*>(desc);
        Desc d; d.api = API_FFX; d.d3d12 = !kFfxD3d11[M]; d.lista = u->commandList; d.ctx = *ctx;
        d.cor = (IUnknown*)u->color.resource; d.depth = (IUnknown*)u->depth.resource; d.mv = (IUnknown*)u->motionVectors.resource; d.expo = (IUnknown*)u->exposure.resource; d.out = (IUnknown*)u->output.resource;
        d.eCor = EstadoFfxApi(u->color.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); d.eDepth = EstadoFfxApi(u->depth.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        d.eMv = EstadoFfxApi(u->motionVectors.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); d.eExpo = EstadoFfxApi(u->exposure.state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        d.eOut = EstadoFfxApi(u->output.state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        d.rw = u->renderSize.width; d.rh = u->renderSize.height; d.ow = u->upscaleSize.width; d.oh = u->upscaleSize.height;
        d.jx = u->jitterOffset.x; d.jy = u->jitterOffset.y; d.msx = u->motionVectorScale.x; d.msy = u->motionVectorScale.y;
        d.dt = u->frameTimeDelta; d.pre = u->preExposure > 0.f ? u->preExposure : 1.f; d.sharp = u->sharpness; d.sharpen = u->enableSharpening; d.reset = u->reset;
        d.camNear = u->cameraNear; d.camFar = u->cameraFar; d.camFov = u->cameraFovAngleVertical; d.viewToM = u->viewSpaceToMetersFactor;   /* J3 */
        { std::lock_guard<std::mutex> g(mu); if (CtxRec* c = AcharCtx(*ctx)) { d.ctxVisto = true; d.flagsNgx = FlagsNgxDeFfx(c->flags); d.depthInv = (c->flags & FFX_UPSCALE_ENABLE_DEPTH_INVERTED) ? 1 : 0; if (!d.ow) { d.ow = c->outW; d.oh = c->outH; } } }
        if (u->flags & (FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB | FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ)) AvisoUmaVez("FSR 3.1: cor nao-linear (sRGB/PQ) declarada no dispatch; o DLSS vai receber como esta");
        Observar(d, d.lista);
        if (Antes(d)) return FFX_API_RETURN_OK;
    }
    }
    catch (...) { Excecao("ffxDispatch"); }
    return oFfxDispatch[M](ctx, desc);
}

/* --- FSR 2.x --- */
FnFsr2Dispatch oFsr2Dispatch = nullptr; FnFsr2Create oFsr2Create = nullptr; FnFsr2Destroy oFsr2Destroy = nullptr;
bool fsr2Layout22 = true;   /* state @168 (2.2) ou @164 (2.0/2.1) */
uint32_t Fsr2Estado(const Fsr2Resource& r) { return fsr2Layout22 ? r.state : r.description.usage; }
uint32_t HkFsr2Create(void* ctx, const Fsr2ContextDesc* desc)
{
    Voo v;
    const uint32_t r = oFsr2Create(ctx, desc);
    try
    {
    if (r == kFsr2Ok && ctx && desc)
    {
        std::lock_guard<std::mutex> g(mu);
        CtxRec* c = RegistrarCtx(ctx, API_FSR2);
        c->flags = desc->flags; c->maxRw = desc->maxRenderSize.width; c->maxRh = desc->maxRenderSize.height; c->outW = desc->displaySize.width; c->outH = desc->displaySize.height;
        L("sr: ffxFsr2ContextCreate ctx=%p flags=0x%X maxRender=%ux%u display=%ux%u", ctx, c->flags, c->maxRw, c->maxRh, c->outW, c->outH);
    }
    }
    catch (...) { Excecao("ffxFsr2ContextCreate"); }   /* C7 */
    return r;
}
uint32_t HkFsr2Destroy(void* ctx) { Voo v; try { std::lock_guard<std::mutex> g(mu); EsquecerCtx(ctx); ContextoDestruido(ctx); } catch (...) { Excecao("ffxFsr2ContextDestroy"); } return oFsr2Destroy(ctx); }
uint32_t HkFsr2Dispatch(void* ctx, const Fsr2DispatchDesc* u)
{
    Voo v;
    try   /* C7 */
    {
    if (!parado.load() && u && ctx)
    {
        Desc d; d.api = API_FSR2; d.d3d12 = true; d.lista = u->commandList; d.ctx = ctx;
        d.cor = (IUnknown*)u->color.resource; d.depth = (IUnknown*)u->depth.resource; d.mv = (IUnknown*)u->motionVectors.resource; d.expo = (IUnknown*)u->exposure.resource; d.out = (IUnknown*)u->output.resource;
        d.eCor = EstadoFsr2(Fsr2Estado(u->color), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); d.eDepth = EstadoFsr2(Fsr2Estado(u->depth), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        d.eMv = EstadoFsr2(Fsr2Estado(u->motionVectors), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); d.eExpo = EstadoFsr2(Fsr2Estado(u->exposure), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        d.eOut = EstadoFsr2(Fsr2Estado(u->output), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        d.rw = u->renderSize.width; d.rh = u->renderSize.height;
        d.jx = u->jitterOffset.x; d.jy = u->jitterOffset.y; d.msx = u->motionVectorScale.x; d.msy = u->motionVectorScale.y;
        d.dt = u->frameTimeDelta; d.pre = u->preExposure > 0.f ? u->preExposure : 1.f; d.sharp = u->sharpness; d.sharpen = u->enableSharpening; d.reset = u->reset;
        d.camNear = u->cameraNear; d.camFar = u->cameraFar; d.camFov = u->cameraFovAngleVertical;   /* J3: viewSpaceToMetersFactor so existe na 2.2, fora do nosso recorte */
        { std::lock_guard<std::mutex> g(mu); if (CtxRec* c = AcharCtx(ctx)) { d.ctxVisto = true; d.flagsNgx = FlagsNgxDeFfx(c->flags); d.depthInv = (c->flags & FSR2_DEPTH_INVERTED) ? 1 : 0; d.ow = c->outW; d.oh = c->outH; } }
        Observar(d, d.lista);
        if (Antes(d)) return kFsr2Ok;
    }
    }
    catch (...) { Excecao("ffxFsr2ContextDispatch"); }
    return oFsr2Dispatch(ctx, u);
}

/* --- XeSS --- */
FnXessInit oXessInit = nullptr; FnXessExecute oXessExecute = nullptr; FnXessSetScale oXessVel = nullptr, oXessJit = nullptr; FnXessDestroy oXessDestroy = nullptr;
XessResult HkXessInit(XessContext ctx, const XessD3D12InitParams* p)
{
    Voo v;
    const XessResult r = oXessInit(ctx, p);
    try
    {
    if (r == 0 && ctx && p)
    {
        std::lock_guard<std::mutex> g(mu);
        CtxRec* c = AcharCtx(ctx); float msx = c ? c->msx : 1.f, msy = c ? c->msy : 1.f, jsx = c ? c->jsx : 1.f, jsy = c ? c->jsy : 1.f;
        c = RegistrarCtx(ctx, API_XESS); c->msx = msx; c->msy = msy; c->jsx = jsx; c->jsy = jsy;
        c->flags = p->initFlags; c->outW = p->outputResolution.x; c->outH = p->outputResolution.y;
        L("sr: xessD3D12Init ctx=%p flags=0x%X saida=%ux%u qualidade=%d", ctx, c->flags, c->outW, c->outH, p->qualitySetting);
    }
    }
    catch (...) { Excecao("xessD3D12Init"); }   /* C7 */
    return r;
}
XessResult HkXessVel(XessContext ctx, float x, float y) { Voo v; try { std::lock_guard<std::mutex> g(mu); CtxRec* c = AcharCtx(ctx); if (!c) c = RegistrarCtx(ctx, API_XESS); c->msx = x; c->msy = y; } catch (...) { Excecao("xessSetVelocityScale"); } return oXessVel(ctx, x, y); }
XessResult HkXessJit(XessContext ctx, float x, float y) { Voo v; try { std::lock_guard<std::mutex> g(mu); CtxRec* c = AcharCtx(ctx); if (!c) c = RegistrarCtx(ctx, API_XESS); c->jsx = x; c->jsy = y; } catch (...) { Excecao("xessSetJitterScale"); } return oXessJit(ctx, x, y); }
XessResult HkXessDestroy(XessContext ctx) { Voo v; try { std::lock_guard<std::mutex> g(mu); EsquecerCtx(ctx); ContextoDestruido(ctx); } catch (...) { Excecao("xessDestroyContext"); } return oXessDestroy(ctx); }
XessResult HkXessExecute(XessContext ctx, ID3D12GraphicsCommandList* lista, const XessD3D12ExecParams* p)
{
    Voo v;
    try   /* C7 */
    {
    if (!parado.load() && p && lista)
    {
        Desc d; d.api = API_XESS; d.d3d12 = true; d.lista = lista; d.ctx = ctx;
        d.cor = p->pColorTexture; d.mv = p->pVelocityTexture; d.depth = p->pDepthTexture; d.expo = p->pExposureScaleTexture; d.out = p->pOutputTexture;
        d.rw = p->inputWidth; d.rh = p->inputHeight;
        d.bxCor = p->inputColorBase.x; d.byCor = p->inputColorBase.y; d.bxMv = p->inputMotionVectorBase.x; d.byMv = p->inputMotionVectorBase.y;
        d.bxDepth = p->inputDepthBase.x; d.byDepth = p->inputDepthBase.y; d.bxOut = p->outputColorBase.x; d.byOut = p->outputColorBase.y;
        d.jx = p->jitterOffsetX; d.jy = p->jitterOffsetY; d.escExpo = p->exposureScale > 0.f ? p->exposureScale : 1.f; d.reset = p->resetHistory != 0;
        d.flagsNgx = NGX_MV_LOW_RES;
        { std::lock_guard<std::mutex> g(mu); if (CtxRec* c = AcharCtx(ctx)) { d.msx = c->msx; d.msy = c->msy; d.jx *= c->jsx; d.jy *= c->jsy; if (c->outW) { d.ctxVisto = true; d.ow = c->outW; d.oh = c->outH; d.flagsNgx = 0; if (!(c->flags & XESS_HIGH_RES_MV)) d.flagsNgx |= NGX_MV_LOW_RES; if (c->flags & XESS_JITTERED_MV) d.flagsNgx |= NGX_MV_JITTERED; if (c->flags & XESS_INVERTED_DEPTH) d.flagsNgx |= NGX_DEPTH_INVERTED; if (c->flags & XESS_AUTO_EXPOSURE) d.flagsNgx |= NGX_AUTO_EXPOSURE; d.depthInv = (c->flags & XESS_INVERTED_DEPTH) ? 1 : 0; if (c->flags & XESS_NDC_VELOCITY) { d.mvNdc = true; AvisoUmaVez("XeSS: velocidade em NDC (XESS_INIT_FLAG_USE_NDC_VELOCITY); o DLSS espera pixels - a escala de MV pode sair errada, e o frame generation nao usa este MV"); } } } }
        Observar(d, lista);
        if (Antes(d)) return 0;
    }
    }
    catch (...) { Excecao("xessD3D12Execute"); }
    return oXessExecute(ctx, lista, p);
}

/* --- instalacao --- */
struct Gancho { const char* nome; void* detour; void** orig; bool obrigatorio; bool ok; };
bool mhIniciado = false;
/* `pulaSe`: nome de outro modulo que, se estiver no processo, faz este ser ignorado (H8: o
   modulo de efeito do SDK 2.x so entra quando nao ha loader, senao o dispatch passaria duas
   vezes pelo hook - uma no loader, outra no efeito que o loader chama). */
struct Modulo { const wchar_t* nome; int api; int idx; const wchar_t* pulaSe; HMODULE h; bool hookado; bool avisado; };
Modulo modulos[] = {
    { L"amd_fidelityfx_dx12.dll",          API_FFX,  0, nullptr,                          nullptr, false, false },
    { L"amd_fidelityfx_dx11.dll",          API_FFX,  1, nullptr,                          nullptr, false, false },
    { L"amd_fidelityfx_loader_dx12.dll",   API_FFX,  2, nullptr,                          nullptr, false, false },   /* H8: SDK 2.x */
    { L"amd_fidelityfx_upscaler_dx12.dll", API_FFX,  3, L"amd_fidelityfx_loader_dx12.dll", nullptr, false, false },   /* H8: so sem o loader */
    { L"ffx_fsr2_api_x64.dll",             API_FSR2, 0, nullptr,                          nullptr, false, false },
    { L"libxess.dll",                      API_XESS, 0, nullptr,                          nullptr, false, false },
};
std::atomic<bool> algumHook{ false };
void* alvosHook[32]; int nAlvos = 0;

bool Enganchar(HMODULE m, Gancho& g)
{
    void* alvo = reinterpret_cast<void*>(GetProcAddress(m, g.nome));
    if (alvo == nullptr) { if (g.obrigatorio) L("sr: export %s ausente", g.nome); return false; }
    MH_STATUS s = MH_CreateHook(alvo, g.detour, g.orig);
    if (s != MH_OK) { L("sr: MH_CreateHook(%s) = %d", g.nome, (int)s); return false; }
    s = MH_EnableHook(alvo);
    if (s != MH_OK) { L("sr: MH_EnableHook(%s) = %d", g.nome, (int)s); return false; }
    if (nAlvos < 32) alvosHook[nAlvos++] = alvo;
    g.ok = true;
    return true;
}
unsigned VersaoMenor(HMODULE m, unsigned* maior)
{
    wchar_t caminho[MAX_PATH] = L""; GetModuleFileNameW(m, caminho, MAX_PATH);
    DWORD z = 0; const DWORD tam = GetFileVersionInfoSizeW(caminho, &z);
    if (tam == 0) return 0xFFFF;
    BYTE* buf = static_cast<BYTE*>(malloc(tam)); if (!buf) return 0xFFFF;
    unsigned menor = 0xFFFF; *maior = 0;
    VS_FIXEDFILEINFO* fi = nullptr; UINT n = 0;
    if (GetFileVersionInfoW(caminho, 0, tam, buf) && VerQueryValueW(buf, L"\\", reinterpret_cast<void**>(&fi), &n) && fi) { *maior = HIWORD(fi->dwFileVersionMS); menor = LOWORD(fi->dwFileVersionMS); }
    free(buf);
    return menor;
}
void Instalar(Modulo& mod)
{
    if (!mhIniciado)
    {
        const MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { L("sr: MH_Initialize = %d; degrau 2 desligado", (int)s); parado = true; return; }
        mhIniciado = true;
    }
    int ok = 0, total = 0; const char* api = NomeApi(mod.api);
    if (mod.api == API_FFX)
    {
        static void* const kDispatch[kFfxSlots] = { (void*)&HkFfxDispatch<0>, (void*)&HkFfxDispatch<1>, (void*)&HkFfxDispatch<2>, (void*)&HkFfxDispatch<3> };
        static void* const kCriar[kFfxSlots]    = { (void*)&HkFfxCreate<0>,   (void*)&HkFfxCreate<1>,   (void*)&HkFfxCreate<2>,   (void*)&HkFfxCreate<3> };
        static void* const kDestruir[kFfxSlots] = { (void*)&HkFfxDestroy<0>,  (void*)&HkFfxDestroy<1>,  (void*)&HkFfxDestroy<2>,  (void*)&HkFfxDestroy<3> };
        const int i = mod.idx >= 0 && mod.idx < kFfxSlots ? mod.idx : 0;
        Gancho g[3] = {
            { "ffxDispatch",       kDispatch[i], (void**)&oFfxDispatch[i], true,  false },
            { "ffxCreateContext",  kCriar[i],    (void**)&oFfxCreate[i],   false, false },
            { "ffxDestroyContext", kDestruir[i], (void**)&oFfxDestroy[i],  false, false } };
        total = 3; for (auto& x : g) if (Enganchar(mod.h, x)) ++ok;
        mod.hookado = g[0].ok;
        unsigned maior = 0; const unsigned menor = VersaoMenor(mod.h, &maior);
        if (menor != 0xFFFF) L("sr: %ls versao %u.%u", mod.nome, maior, menor);
    }
    else if (mod.api == API_FSR2)
    {
        Gancho g[3] = {
            { "ffxFsr2ContextDispatch", (void*)&HkFsr2Dispatch, (void**)&oFsr2Dispatch, true,  false },
            { "ffxFsr2ContextCreate",   (void*)&HkFsr2Create,   (void**)&oFsr2Create,   false, false },
            { "ffxFsr2ContextDestroy",  (void*)&HkFsr2Destroy,  (void**)&oFsr2Destroy,  false, false } };
        total = 3; for (auto& x : g) if (Enganchar(mod.h, x)) ++ok;
        mod.hookado = g[0].ok;
        unsigned maior = 0; const unsigned menor = VersaoMenor(mod.h, &maior);
        fsr2Layout22 = !(maior == 2 && menor < 2);
        L("sr: ffx_fsr2_api_x64.dll versao %u.%u -> layout %s (state @%u)", maior, menor == 0xFFFF ? 0u : menor, fsr2Layout22 ? "2.2" : "2.0/2.1", fsr2Layout22 ? 168u : 164u);
        if (menor == 0xFFFF) L("sr: (sem VERSIONINFO no modulo; assumindo 2.2 - se as barreiras falharem, use sr_estado_in/sr_estado_out no controle)");
    }
    else if (mod.api == API_XESS)
    {
        Gancho g[5] = {
            { "xessD3D12Execute",     (void*)&HkXessExecute, (void**)&oXessExecute, true,  false },
            { "xessD3D12Init",        (void*)&HkXessInit,    (void**)&oXessInit,    false, false },
            { "xessSetVelocityScale", (void*)&HkXessVel,     (void**)&oXessVel,     false, false },
            { "xessSetJitterScale",   (void*)&HkXessJit,     (void**)&oXessJit,     false, false },
            { "xessDestroyContext",   (void*)&HkXessDestroy, (void**)&oXessDestroy, false, false } };
        total = 5; for (auto& x : g) if (Enganchar(mod.h, x)) ++ok;
        mod.hookado = g[0].ok;
    }
    wchar_t caminho[MAX_PATH] = L""; GetModuleFileNameW(mod.h, caminho, MAX_PATH);
    if (mod.hookado) { algumHook = true; L("sr: hook instalado api=%s %d/%d exports, modulo=%ls base=0x%p", api, ok, total, caminho, (void*)mod.h); ++estadoVersao; }
    else L("sr: %ls esta no processo mas o dispatch nao foi enganchado (%d/%d)", mod.nome, ok, total);
}

void CaminhoEstadoSr(wchar_t* out, size_t n) { CaminhoTemp(out, n, L"sr-estado"); }
void EscreverEstadoSr()
{
    const unsigned v = estadoVersao.load();
    if (v == estadoEscrito) return;
    estadoEscrito = v;
    wchar_t caminho[MAX_PATH]; CaminhoEstadoSr(caminho, MAX_PATH);
    char corpo[512];
    int api = S.api; for (auto& m : modulos) if (m.hookado && api == API_NENHUMA) api = m.api;
    snprintf(corpo, sizeof corpo, "hook=%d\napi=%s\nativa=%d\ntroca=%d\nsessao=%ux%u->%ux%u\ndispatches=%u\nmotivo=%s\n",
             algumHook.load() ? 1 : 0, NomeApi(api), estado.load() == E_PRONTO ? 1 : 0, trocando.load() ? 1 : 0, S.w, S.h, S.ow, S.oh, dispatches.load(), motivoFalha);
    FILE* f = _wfopen(caminho, L"wb");
    if (f == nullptr) return;
    fputs(corpo, f); fclose(f);
}

int64_t ultimaTentativa = 0;
bool avisouSemPipe = false, avisouNgx = false;

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
    Servico();
}

void AoExecutar(ID3D12CommandQueue* fila, UINT n, ID3D12CommandList* const* listas)
{
    if (!algumHook.load() || listas == nullptr || fila == nullptr || S.ultimaLista == nullptr) return;
    for (UINT i = 0; i < n; ++i)
    {
        if (listas[i] != S.ultimaLista) continue;
        D3D12_COMMAND_QUEUE_DESC qd = fila->GetDesc();
        if (qd.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
        if (S.fila == fila) return;
        std::lock_guard<std::mutex> g(mu);
        if (S.fila) S.fila->Release();
        fila->AddRef(); S.fila = fila;
        L("sr: a lista do dispatch executa na fila 0x%p tipo=DIRECT", (void*)fila);
        return;
    }
}

/* A CHAVE DE DESLIGAMENTO DO H8 (18/09/2026): `[ultima] sr_hook_loader=0` no
   dlss5forall.ini faz esta DLL NAO enganchar o FidelityFX SDK 2.x (o loader e o modulo do
   upscaler, slots 2 e 3). O SDK 1.1, o FSR2 e o XeSS continuam enganchados como sempre.

   POR QUE ELA EXISTE: o H8 e novo e se apoia numa leitura de deslocamentos do
   `ffxDispatchDescUpscale` do SDK 2.1 conferida no validador do upscaler, nao medida em
   jogo. Se algum deslocamento estiver errado, o hook le ponteiro de recurso no lugar
   errado e manda handle invalido para o app - e quem cai e o app. Enquanto isso nao
   estiver medido com o jogo na tela, quem estiver com o app fechando precisa de uma linha
   no ini para voltar ao comportamento anterior, sem perder o resto da build.

   O ini e o do APP, que fica ao lado desta DLL (o dlss5-ponte64.dll e injetado a partir do
   bin do app) - o mesmo arquivo que o ponte_cliente le para o modo da ponte. Lido uma vez
   e guardado: isto roda no Servico, que passa por aqui a cada volta. */
static bool LoaderSdk2Ligado()
{
    static int v = -1;
    if (v >= 0) return v != 0;
    v = 1;   /* ausente = ligado, o comportamento da build */
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&LoaderSdk2Ligado), &self) && self != nullptr)
    {
        wchar_t p[MAX_PATH]{};
        if (GetModuleFileNameW(self, p, MAX_PATH) != 0)
        {
            if (wchar_t* s = wcsrchr(p, L'\\')) *(s + 1) = 0;
            wchar_t ini[MAX_PATH];
            _snwprintf(ini, MAX_PATH, L"%sdlss5forall.ini", p);
            v = GetPrivateProfileIntW(L"ultima", L"sr_hook_loader", 1, ini) != 0 ? 1 : 0;
        }
    }
    return v != 0;
}
/* Slots 2 e 3 = FidelityFX SDK 2.x (ver a tabela `modulos`). */
static bool EhSdk2(const Modulo& m) { return m.api == API_FFX && (m.idx == 2 || m.idx == 3); }

void SoltarPipe();   /* X-FG-6, abaixo */
void Servico()
{
    if (parado.load() || L == nullptr) return;
    for (auto& m : modulos)
    {
        if (m.hookado) continue;
        HMODULE h = GetModuleHandleW(m.nome);
        if (h == nullptr) continue;
        if (EhSdk2(m) && !LoaderSdk2Ligado())
        {
            if (!m.avisado)
            {
                m.avisado = true;
                L("sr: %ls NAO sera enganchado - [ultima] sr_hook_loader=0 no dlss5forall.ini "
                  "(FidelityFX SDK 2.x desligado a pedido; o SDK 1.1, o FSR2 e o XeSS seguem)", m.nome);
            }
            continue;
        }
        if (m.pulaSe != nullptr && GetModuleHandleW(m.pulaSe) != nullptr)   /* H8 */
        {
            if (!m.avisado) { m.avisado = true; L("sr: %ls esta no processo, mas quem o jogo chama e o %ls: engancho so o loader", m.nome, m.pulaSe); }
            continue;
        }
        if (m.h == h && m.avisado) continue;   /* ja tentamos e falhou */
        m.h = h; m.avisado = true;
        Instalar(m);
    }
    if (!algumHook.load()) { EscreverEstadoSr(); return; }
    const bool mudou = LerControle();
    const int e = estado.load();
    if (mudou && !ctl.sr && (e == E_PRONTO || e == E_CONECTADO)) Falhar("sr=0 no controle");
    if (mudou && ctl.sr && e == E_FALHOU && strstr(motivoFalha, "controle")) { estado = E_ESPERANDO; motivoFalha[0] = 0; L("sr: religado pelo controle"); }
    if (!ctl.sr) { EscreverEstadoSr(); return; }
    switch (estado.load())
    {
    case E_ESPERANDO:
    {
        std::lock_guard<std::mutex> g(mu);
        if (!ColherDoDispatch()) break;
        estado = E_CONECTANDO; ultimaTentativa = 0;
    }
    /* fallthrough */
    case E_CONECTANDO:
    {
        const int64_t agora = Agora();
        if (ultimaTentativa != 0 && Ms(ultimaTentativa, agora) < 2000.0) break;
        if (GetModuleHandleW(L"_nvngx.dll") != nullptr && !avisouNgx) { avisouNgx = true; L("sr: o _nvngx.dll tambem esta no processo (o jogo tem DLSS?); o degrau 2 so vale se o upscaler ativo for FSR/XeSS"); }
        bool ocupado = false;
        if (!Conectar(&ocupado))
        {
            /* X-FG-6: o pipe esta com o ngx_upstream (o jogo acabou de trocar DLSS -> FSR/XeSS): o
               pedido ficou registrado e a proxima volta do Servico (~250 ms) tenta de novo, sem os 2 s */
            if (ocupado) break;
            ultimaTentativa = agora;
            if (!avisouSemPipe) { avisouSemPipe = true; L("sr: o app nao serve o pipe .up (nativo_upstream=0 e nativo_sr=0, ou app antigo); tentando a cada 2 s"); }
            break;
        }
        ultimaTentativa = 0;
        estado = E_CONECTADO;
    }
    /* fallthrough */
    case E_CONECTADO:
    {
        std::lock_guard<std::mutex> g(mu);
        if (S.tex[SLOT_COLOR] || S.tex11[SLOT_COLOR])
        {
            /* rebuild por mudanca de tamanho/formato: o pipe fica, vai um Build novo (o host
               refaz o conjunto); os dados vem do ultimo dispatch visto */
            L("sr: refazendo a sessao (o dispatch mudou de tamanho ou formato)");
            Sleep(60);
            DestravarFila();
            if (S.d3d12 && S.fila && S.fRecicla && S.reciclaVal && S.fRecicla->GetCompletedValue() < S.reciclaVal && S.evRecicla && SUCCEEDED(S.fRecicla->SetEventOnCompletion(S.reciclaVal, S.evRecicla))) WaitForSingleObject(S.evRecicla, 1000);
            LiberarTexturas();
            primeiro = ultimo; temPrimeiro = true;
            if (!ColherDoDispatch()) { if (estado.load() == E_CONECTADO) estado = E_ESPERANDO; break; }
        }
        if (!Build()) break;
        if (thVigia == nullptr) thVigia = CreateThread(nullptr, 0, Vigia, nullptr, 0, nullptr);
        estado = E_PRONTO;
        break;
    }
    case E_PRONTO:
        ConferirProva();
        if (Ponte::PipeUp::AlguemQuer(Ponte::PipeUp::DONO_SR)) SoltarPipe();   /* X-FG-6: o jogo trocou para o DLSS? */
        break;
    default: break;
    }
    EscreverEstadoSr();
}

/* X-FG-6: PASSAGEM DE BASTAO. O ngx_upstream pediu o pipe (o jogo esta chamando o DLSS). Se o
   contexto que serviamos foi destruido, ou nao ha dispatch ha 1 s, esta sessao acabou: solta o
   que esta na fila (DestravarFila), espera a GPU terminar as nossas copias, libera o conjunto e
   LARGA o pipe sem fechar - o outro modulo manda o Build dele na mesma conexao e o app refaz o
   conjunto como numa troca de resolucao. A sessao volta a ESPERANDO: o proximo dispatch de
   FSR/XeSS (o jogador voltou) pede o pipe de novo, e o ngx_upstream larga do mesmo jeito.
   Com o contexto vivo e dispatches chegando, nada acontece (os dois upscalers ao mesmo tempo
   nao e caso real; se for, quem chegou primeiro fica). */
void SoltarPipe()
{
    const int64_t agora = Agora();
    const bool semDispatch = S.qpcUltimo.load() != 0 && Ms(S.qpcUltimo.load(), agora) >= 1000.0;
    if (!S.ctxSolto.load() && !semDispatch) return;
    L("sr: o jogo trocou de upscaler (%s): soltando a sessao %ux%u->%ux%u e passando o pipe .up ao DLSS (ngx_upstream) apos %u dispatches",
      S.ctxSolto.load() ? "o contexto foi destruido" : "sem dispatch ha 1 s e o DLSS esta sendo chamado", S.w, S.h, S.ow, S.oh, dispatches.load());
    estado = E_ESPERANDO;   /* AntesD12/D11 param de tocar nos slots a partir daqui */
    trocando = false;
    Sleep(60);              /* um dispatch em voo termina antes de soltarmos o que ele usa */
    DestravarFila();
    if (S.d3d12 && S.fila && S.fRecicla && S.reciclaVal && S.fRecicla->GetCompletedValue() < S.reciclaVal && S.evRecicla && SUCCEEDED(S.fRecicla->SetEventOnCompletion(S.reciclaVal, S.evRecicla))) WaitForSingleObject(S.evRecicla, 1000);
    {
        std::lock_guard<std::mutex> g(mu);
        LiberarTexturas();
        temPrimeiro = false;   /* a proxima sessao nasce do proximo dispatch, nao deste */
        S.ctxAtual = nullptr; S.ctxSolto = false; S.qpcUltimo = 0;
        primeiroRetorno = false;
    }
    Ponte::PipeUp::Largar(Ponte::PipeUp::DONO_SR);
    ++estadoVersao;
}

bool Ativo() { return estado.load() == E_PRONTO; }
bool HookInstalado() { return algumHook.load(); }

void Parar()
{
    if (parado.exchange(true)) return;
    if (estado.load() == E_PRONTO || estado.load() == E_CONECTADO) L("sr: desligado (ponte encerrando) apos %u dispatches (%u trocados), atrasados %u", dispatches.load(), trocados.load(), atrasadosTotal.load());
    estado = E_DESLIGADO; trocando = false;
    Sleep(60);
    DestravarFila();
    vigiaSai = true; if (evVigia) SetEvent(evVigia);
    if (thVigia) { WaitForSingleObject(thVigia, 1500); CloseHandle(thVigia); thVigia = nullptr; }
    if (S.fila && S.fRecicla && S.reciclaVal && S.fRecicla->GetCompletedValue() < S.reciclaVal && S.evRecicla && SUCCEEDED(S.fRecicla->SetEventOnCompletion(S.reciclaVal, S.evRecicla))) WaitForSingleObject(S.evRecicla, 1000);
    for (int i = 0; i < nAlvos; ++i) MH_DisableHook(alvosHook[i]);
    for (int i = 0; i < 50 && emVoo.load() > 0; ++i) Sleep(10);
    { wchar_t c[MAX_PATH]; CaminhoEstadoSr(c, MAX_PATH); DeleteFileW(c); }
    std::lock_guard<std::mutex> g(mu);
    LiberarTexturas();
    Soltar(S.lista); for (auto& a : S.alloc) Soltar(a);
    Soltar(S.fRecicla); Soltar(S.fProva); FecharHandle(S.evRecicla);
    Soltar(S.ctx4); Soltar(S.ctx11); Soltar(S.dev5); Soltar(S.dev11); Soltar(S.devAux); Soltar(ctxColheita11);
    Soltar(S.fila); Soltar(S.dev);
    /* X-FG-6: o pipe e compartilhado - se e nosso, fecha; senao so larga o pedido (o ngx_upstream fecha o dele) */
    if (Ponte::PipeUp::Dono() == Ponte::PipeUp::DONO_SR) Ponte::PipeUp::Fechar(); else Ponte::PipeUp::Largar(Ponte::PipeUp::DONO_SR);
    FecharHandle(evVigia); FecharHandle(evVigiaIn); FecharHandle(evVigiaOut);
}

} // namespace Sr
