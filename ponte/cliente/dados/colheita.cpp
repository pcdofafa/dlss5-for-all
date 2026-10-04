/* =====================================================================
   colheita.cpp -- ver colheita.h. X-FG-6 (18/09/2026).

   A conversao por compute e a mesma do sr_hook (kConvHlsl: depth -> R32F,
   MV -> R16G16F), repetida aqui de proposito: este modulo tem os SEUS
   recursos e o seu ciclo de vida, e nao mexe nos da sessao .up.
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
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>

#include "colheita.h"
#include "../fg/fg_hook.h"
#include "../ponte_dados.h"
#include "../ponte_seh.h"   /* X-CRASH-3: a mesma rede de seguranca dos hooks (I10) na thread do jogo */

namespace Ponte { namespace Colheita {
namespace {

LogFn L = nullptr;
std::mutex mu;
std::atomic<bool> parado{ false };
std::atomic<bool> falhou{ false };   /* X-CRASH-3: estouro de hardware dentro da colheita = desligada para sempre nesta sessao */
int64_t qpcFreq = 1;
int64_t Agora() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
double  Ms(int64_t de, int64_t ate) { return 1000.0 * double(ate - de) / double(qpcFreq); }
template <class T> void Soltar(T*& p) { if (p) { p->Release(); p = nullptr; } }
unsigned avisos = 0;
void AvisoUmaVez(const char* m) { if (avisos < 16) { ++avisos; L("colheita: %s", m); } }
std::atomic<unsigned> excecoes{ 0 };
unsigned publicados = 0;

/* --- a chave do ini, lida uma vez (a mesma tecnica do sr_hook_loader) --- */
bool Chave()
{
    static int v = -1;
    if (v >= 0) return v != 0;
    v = 1;
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Chave), &self) && self)
    {
        wchar_t p[MAX_PATH]{};
        if (GetModuleFileNameW(self, p, MAX_PATH) != 0)
        {
            if (wchar_t* s = wcsrchr(p, L'\\')) *(s + 1) = 0;
            wchar_t ini[MAX_PATH]; _snwprintf(ini, MAX_PATH, L"%sdlss5forall.ini", p);
            v = GetPrivateProfileIntW(L"ultima", L"dados_sem_upstream", 1, ini) != 0 ? 1 : 0;
        }
    }
    if (v == 0 && L) L("colheita: desligada ([ultima] dados_sem_upstream=0): depth/MV so com a sessao .up");
    return v != 0;
}

/* --- formatos (os mesmos criterios do sr_hook) --- */
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
    bool ativo = false;                          /* D3D11: precisa do compute (formato fora da familia do slot) */
    DXGI_FORMAT jogoFmt = DXGI_FORMAT_UNKNOWN;   /* o formato do jogo para o qual foi montado */
    DXGI_FORMAT srvFmt = DXGI_FORMAT_UNKNOWN, interFmt = DXGI_FORMAT_UNKNOWN;   /* srvFmt: a vista tipada do recurso DO JOGO (D3D12) ou da intermediaria (D3D11) */
    bool depthStencil = false;
    ID3D12PipelineState* pso = nullptr;
    /* X-CRASH-3: D3D12 le o recurso do jogo DIRETO por SRV (nada de copia intermediaria, nada de
       barreira nele). A SRV mora num anel de descritores; estes tres dizem para qual recurso ela foi
       escrita, para so reescrever quando o jogo trocar o recurso (e nunca em cima de um descritor
       que uma lista em voo ainda le - o anel avanca a cada troca). */
    ID3D12Resource* srvDe = nullptr; UINT64 srvW = 0; UINT srvH = 0; DXGI_FORMAT srvDeFmt = DXGI_FORMAT_UNKNOWN;
    ID3D11ComputeShader* cs11 = nullptr; ID3D11Texture2D* inter11 = nullptr; ID3D11ShaderResourceView* srv11 = nullptr; ID3D11UnorderedAccessView* uav11 = nullptr;
    unsigned logadas = 0;
};
void LiberarConv(Conv& c) { Soltar(c.pso); Soltar(c.cs11); Soltar(c.inter11); Soltar(c.srv11); Soltar(c.uav11); c.ativo = false; c.logadas = 0; c.jogoFmt = DXGI_FORMAT_UNKNOWN; c.srvDe = nullptr; }

/* Anel de descritores (D3D12): kAneis conjuntos de 4 (0 depth SRV, 1 depth UAV, 2 mv SRV, 3 mv UAV).
   O jogo segura no maximo 2-3 quadros em voo; 4 conjuntos, avancando so quando um recurso do jogo
   troca, e o que garante que a lista em voo nunca ve o seu descritor reescrito. */
const UINT kAneis = 4, kPorAnel = 4;

struct Conjunto
{
    bool d3d12 = true;
    ID3D12Device* dev = nullptr; ID3D11Device* dev11 = nullptr;
    unsigned w = 0, h = 0;                                   /* tamanho dos slots */
    ID3D12Resource* depth12 = nullptr; ID3D12Resource* mv12 = nullptr;
    ID3D11Texture2D* depth11 = nullptr; ID3D11Texture2D* mv11 = nullptr;
    ID3D12RootSignature* rs = nullptr; ID3D12DescriptorHeap* heap = nullptr; UINT incr = 0;   /* heap: kAneis x (0 depth SRV, 1 depth UAV, 2 mv SRV, 3 mv UAV) */
    UINT anel = 0;                                           /* o conjunto de descritores em uso */
    ID3D11Buffer* cb11 = nullptr;
    bool convMontada = false, convFalhou = false;
    Conv cDepth, cMv;
    ID3D12Fence* fVida = nullptr; uint64_t vida = 0;        /* sinalizada na fila a cada Publicar */
    ID3D12CommandQueue* fila = nullptr;   /* X-CRASH-3: a fila do jogo (AddRef), para o Signal de encerramento */
    /* no cemiterio */
    int64_t qpcAposentado = 0; uint64_t vidaNecessaria = 0;
};
Conjunto* atual = nullptr;
Conjunto* velhos[8] = {};

void Liberar(Conjunto* c)
{
    if (c == nullptr) return;
    LiberarConv(c->cDepth); LiberarConv(c->cMv);
    Soltar(c->depth12); Soltar(c->mv12); Soltar(c->depth11); Soltar(c->mv11);
    Soltar(c->rs); Soltar(c->heap); Soltar(c->cb11); Soltar(c->fVida); Soltar(c->fila);
    Soltar(c->dev); Soltar(c->dev11);
    delete c;
}
/* Com mu. O atual vai para o cemiterio (ou e liberado na hora, em D3D11). */
void Aposentar()
{
    if (atual == nullptr) return;
    Conjunto* c = atual; atual = nullptr;
    if (!c->d3d12) { Liberar(c); return; }
    c->qpcAposentado = Agora(); c->vidaNecessaria = c->vida + 2;
    for (auto& v : velhos) if (v == nullptr) { v = c; return; }
    /* cemiterio cheio (8 trocas de tamanho sem vaga): o mais antigo vai, com o aviso */
    AvisoUmaVez("cemiterio de conjuntos cheio; liberando o mais antigo");
    Liberar(velhos[0]); for (int i = 0; i < 7; ++i) velhos[i] = velhos[i + 1]; velhos[7] = c;
}
/* Com mu. Libera os aposentados que a fence (dois quadros depois) e 1 s dizem que ninguem usa. */
void Cemiterio(bool tudo)
{
    const int64_t agora = Agora();
    for (auto& v : velhos)
    {
        if (v == nullptr) continue;
        const bool fenceOk = v->fVida == nullptr || v->fVida->GetCompletedValue() >= v->vidaNecessaria;
        if (tudo || (fenceOk && Ms(v->qpcAposentado, agora) >= 1000.0)) { Liberar(v); v = nullptr; }
    }
}

bool DescRes(IUnknown* r, bool d3d12, DXGI_FORMAT* fmt, unsigned* w, unsigned* h)
{
    if (r == nullptr) return false;
    if (d3d12) { ID3D12Resource* t = nullptr; if (FAILED(r->QueryInterface(__uuidof(ID3D12Resource), reinterpret_cast<void**>(&t))) || !t) return false; D3D12_RESOURCE_DESC d = t->GetDesc(); t->Release(); *fmt = d.Format; *w = (unsigned)d.Width; *h = d.Height; return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.SampleDesc.Count == 1; }
    ID3D11Texture2D* t = nullptr; if (FAILED(r->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t))) || !t) return false; D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); t->Release(); *fmt = d.Format; *w = d.Width; *h = d.Height; return d.SampleDesc.Count == 1;
}

bool CompilarConv(bool d3d12, bool mv, ID3DBlob** cs)
{
    ID3DBlob* err = nullptr;
    const D3D_SHADER_MACRO defs12[] = { { "RS12", "1" }, { "DST_T", mv ? "float2" : "float" }, { "DST_EXPR", mv ? "v.xy" : "v.x" }, { nullptr, nullptr } };
    const D3D_SHADER_MACRO defs11[] = { { "DST_T", mv ? "float2" : "float" }, { "DST_EXPR", mv ? "v.xy" : "v.x" }, { nullptr, nullptr } };
    HRESULT hr = D3DCompile(kConvHlsl, sizeof(kConvHlsl) - 1, mv ? "mv" : "depth", d3d12 ? defs12 : defs11, nullptr, "cs", "cs_5_0", 0, 0, cs, &err);
    if (FAILED(hr)) { L("colheita: shader de conversao (%s) nao compilou 0x%08X: %s", mv ? "mv" : "depth", (unsigned)hr, err ? (const char*)err->GetBufferPointer() : ""); Soltar(err); return false; }
    Soltar(err);
    return true;
}

/* ------------------------------------------------------------------ */
/* D3D12                                                                */
/* ------------------------------------------------------------------ */
ID3D12Resource* Criar12(Conjunto* c, UINT w, UINT h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; rd.Flags = flags;
    ID3D12Resource* r = nullptr;
    HRESULT hr = c->dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), reinterpret_cast<void**>(&r));
    if (FAILED(hr)) { L("colheita: CreateCommittedResource %ux%u fmt=%u falhou 0x%08X", w, h, (unsigned)fmt, (unsigned)hr); return nullptr; }
    return r;
}
D3D12_RESOURCE_BARRIER Barr(ID3D12Resource* r, D3D12_RESOURCE_STATES de, D3D12_RESOURCE_STATES para)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = r; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = de; b.Transition.StateAfter = para; return b;
}
/* X-CRASH-3 (18/09, principio do dono: "a gente nunca toca no jogo"). O D3D12 so LE o recurso do
   jogo, por uma SRV criada sobre ELE, no estado em que o upscaler ja o exige (o contrato do NGX/
   FSR/XeSS: entradas em NON_PIXEL_SHADER_RESOURCE no Evaluate/Dispatch). Nada de barreira nele,
   nada de copia dele em COPY_SOURCE, nada de intermediaria: o compute le a SRV e escreve no NOSSO
   slot (UAV), e as unicas barreiras sao nas nossas texturas. Se o recurso nega SRV (DENY_SHADER_
   RESOURCE), a colheita nao roda neste quadro. O que ainda fica na lista do jogo: heaps, root
   signature e PSO de compute - o upscaler que roda logo a seguir monta os dele e nao restaura
   nada (o jogo ja e obrigado a restaurar depois do Evaluate); e o residuo inevitavel de ler
   dentro da lista, unico ponto em que o estado do recurso e certo. */
bool Conv12Montar(Conjunto* c)
{
    if (c->convMontada) return true;
    if (c->convFalhou) return false;
    ID3DBlob* cs = nullptr, *rs = nullptr;
    HRESULT hr = E_FAIL;
    if (CompilarConv(true, false, &cs))
    {
        hr = D3DGetBlobPart(cs->GetBufferPointer(), cs->GetBufferSize(), D3D_BLOB_ROOT_SIGNATURE, 0, &rs);
        if (SUCCEEDED(hr)) hr = c->dev->CreateRootSignature(0, rs->GetBufferPointer(), rs->GetBufferSize(), __uuidof(ID3D12RootSignature), reinterpret_cast<void**>(&c->rs));
        Soltar(rs);
        if (SUCCEEDED(hr)) { D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = c->rs; pd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() }; hr = c->dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), reinterpret_cast<void**>(&c->cDepth.pso)); }
        Soltar(cs);
    }
    if (SUCCEEDED(hr) && CompilarConv(true, true, &cs)) { D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = c->rs; pd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() }; hr = c->dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), reinterpret_cast<void**>(&c->cMv.pso)); Soltar(cs); }
    if (SUCCEEDED(hr)) { D3D12_DESCRIPTOR_HEAP_DESC hd = {}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = kAneis * kPorAnel; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; hr = c->dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void**>(&c->heap)); }
    if (FAILED(hr)) { L("colheita: conversao por compute (D3D12) nao montou 0x%08X; sem dado", (unsigned)hr); c->convFalhou = true; return false; }
    c->incr = c->dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    /* as UAVs dos nossos slots sao iguais em todos os aneis (os slots so mudam com o conjunto) */
    const D3D12_CPU_DESCRIPTOR_HANDLE h0 = c->heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT a = 0; a < kAneis; ++a)
    {
        { D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R32_FLOAT; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; D3D12_CPU_DESCRIPTOR_HANDLE h = h0; h.ptr += c->incr * (a * kPorAnel + 1); c->dev->CreateUnorderedAccessView(c->depth12, nullptr, &ud, h); }
        { D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R16G16_FLOAT; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; D3D12_CPU_DESCRIPTOR_HANDLE h = h0; h.ptr += c->incr * (a * kPorAnel + 3); c->dev->CreateUnorderedAccessView(c->mv12, nullptr, &ud, h); }
    }
    c->convMontada = true;
    return true;
}
/* A SRV tipada sobre o recurso DO JOGO, no anel atual. true = precisa escrever (o recurso trocou). */
bool Srv12Precisa(const Conv& cv, ID3D12Resource* src, const D3D12_RESOURCE_DESC& dd)
{
    return cv.srvDe != src || cv.srvW != dd.Width || cv.srvH != dd.Height || cv.srvDeFmt != dd.Format;
}
void Srv12Escrever(Conjunto* c, Conv& cv, ID3D12Resource* src, const D3D12_RESOURCE_DESC& dd, unsigned slotHeap)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = cv.srvFmt; sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sd.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE h = c->heap->GetCPUDescriptorHandleForHeapStart(); h.ptr += c->incr * (c->anel * kPorAnel + slotHeap);
    c->dev->CreateShaderResourceView(src, &sd, h);
    cv.srvDe = src; cv.srvW = dd.Width; cv.srvH = dd.Height; cv.srvDeFmt = dd.Format;
}
/* NA LISTA DO JOGO, so leitura do recurso dele: SRV(jogo) -> compute -> slot nosso (rw x rh a partir de bx,by). */
bool Ler12(Conjunto* c, Conv& cv, ID3D12GraphicsCommandList* l, ID3D12Resource* src, ID3D12Resource* slot, unsigned slotHeap, unsigned bx, unsigned by, unsigned rw, unsigned rh)
{
    if (!Conv12Montar(c) || !cv.pso) return false;
    D3D12_RESOURCE_BARRIER b1 = Barr(slot, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    l->ResourceBarrier(1, &b1);
    ID3D12DescriptorHeap* heaps[1] = { c->heap };
    l->SetDescriptorHeaps(1, heaps);
    l->SetComputeRootSignature(c->rs);
    l->SetPipelineState(cv.pso);
    const unsigned k[4] = { bx, by, rw, rh };
    l->SetComputeRoot32BitConstants(0, 4, k, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE g0 = c->heap->GetGPUDescriptorHandleForHeapStart(), gs = g0, gu = g0;
    gs.ptr += c->incr * (c->anel * kPorAnel + slotHeap); gu.ptr += c->incr * (c->anel * kPorAnel + slotHeap + 1);
    l->SetComputeRootDescriptorTable(1, gs);
    l->SetComputeRootDescriptorTable(2, gu);
    l->Dispatch((rw + 7) / 8, (rh + 7) / 8, 1);
    D3D12_RESOURCE_BARRIER b2 = Barr(slot, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    l->ResourceBarrier(1, &b2);
    if (cv.logadas++ == 0) { D3D12_RESOURCE_DESC dd = src->GetDesc(); L("colheita: %s lida do recurso do jogo por SRV fmt=%u (recurso fmt=%u) -> %s, na lista do jogo, sem tocar nele (D3D12: %ux%u de %ux%u, base %u,%u)", slotHeap == 0 ? "depth" : "mv", (unsigned)cv.srvFmt, (unsigned)dd.Format, slotHeap == 0 ? "R32F" : "R16G16F", rw, rh, (unsigned)dd.Width, dd.Height, bx, by); }
    return true;
}
/* O quadro inteiro em D3D12: decide o anel (avanca se qualquer SRV precisa ser reescrita, para
   nunca reescrever um descritor que uma lista em voo ainda le), escreve as SRVs e le os dois. */
bool Quadro12(Conjunto* c, const Entrada& e, ID3D12Resource* depth, ID3D12Resource* mv, unsigned mvW, unsigned mvH)
{
    if (!Conv12Montar(c)) return false;
    const D3D12_RESOURCE_DESC dd = depth->GetDesc(), md = mv->GetDesc();
    if ((dd.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) || (md.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) { AvisoUmaVez("depth/MV do jogo negam SRV (DENY_SHADER_RESOURCE): nao ha como ler sem tocar; sem dado"); return false; }
    const bool pd = Srv12Precisa(c->cDepth, depth, dd), pm = Srv12Precisa(c->cMv, mv, md);
    if (pd || pm)
    {
        c->anel = (c->anel + 1) % kAneis;
        Srv12Escrever(c, c->cDepth, depth, dd, 0);
        Srv12Escrever(c, c->cMv, mv, md, 2);
    }
    if (!Ler12(c, c->cDepth, e.lista, depth, c->depth12, 0, e.bxDepth, e.byDepth, e.rw, e.rh)) return false;
    return Ler12(c, c->cMv, e.lista, mv, c->mv12, 2, e.bxMv, e.byMv, mvW, mvH);
}

/* ------------------------------------------------------------------ */
/* D3D11                                                                */
/* ------------------------------------------------------------------ */
ID3D11Texture2D* Criar11(Conjunto* c, UINT w, UINT h, DXGI_FORMAT fmt, UINT bind)
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = bind;
    ID3D11Texture2D* t = nullptr;
    HRESULT hr = c->dev11->CreateTexture2D(&td, nullptr, &t);
    if (FAILED(hr)) { L("colheita: D3D11 CreateTexture2D %ux%u fmt=%u falhou 0x%08X", w, h, (unsigned)fmt, (unsigned)hr); return nullptr; }
    return t;
}
bool Conv11Montar(Conjunto* c)
{
    if (c->convMontada) return true;
    if (c->convFalhou) return false;
    ID3DBlob* cs = nullptr;
    HRESULT hr = E_FAIL;
    if (CompilarConv(false, false, &cs)) { hr = c->dev11->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &c->cDepth.cs11); Soltar(cs); }
    if (SUCCEEDED(hr) && CompilarConv(false, true, &cs)) { hr = c->dev11->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &c->cMv.cs11); Soltar(cs); }
    if (SUCCEEDED(hr)) { D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; hr = c->dev11->CreateBuffer(&bd, nullptr, &c->cb11); }
    if (SUCCEEDED(hr)) { D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R32_FLOAT; ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D; hr = c->dev11->CreateUnorderedAccessView(c->depth11, &ud, &c->cDepth.uav11); }
    if (SUCCEEDED(hr)) { D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R16G16_FLOAT; ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D; hr = c->dev11->CreateUnorderedAccessView(c->mv11, &ud, &c->cMv.uav11); }
    if (FAILED(hr)) { L("colheita: conversao por compute (D3D11) nao montou 0x%08X", (unsigned)hr); c->convFalhou = true; return false; }
    c->convMontada = true;
    return true;
}
bool Inter11(Conjunto* c, Conv& cv, ID3D11Resource* src)
{
    ID3D11Texture2D* t = nullptr;
    if (FAILED(src->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t))) || t == nullptr) return false;
    D3D11_TEXTURE2D_DESC dd; t->GetDesc(&dd); t->Release();
    if (cv.inter11) { D3D11_TEXTURE2D_DESC id; cv.inter11->GetDesc(&id); if (id.Width == dd.Width && id.Height == dd.Height) return true; Soltar(cv.srv11); Soltar(cv.inter11); }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = dd.Width; td.Height = dd.Height; td.MipLevels = 1; td.ArraySize = 1; td.Format = cv.interFmt != DXGI_FORMAT_UNKNOWN ? cv.interFmt : dd.Format; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (cv.depthStencil ? D3D11_BIND_DEPTH_STENCIL : 0);
    HRESULT hr = c->dev11->CreateTexture2D(&td, nullptr, &cv.inter11);
    if (SUCCEEDED(hr)) { D3D11_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = cv.srvFmt; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1; hr = c->dev11->CreateShaderResourceView(cv.inter11, &sd, &cv.srv11); }
    if (FAILED(hr)) { char m[160]; snprintf(m, sizeof m, "intermediaria D3D11 (fmt %u %ux%u) falhou 0x%08X", (unsigned)dd.Format, dd.Width, dd.Height, (unsigned)hr); AvisoUmaVez(m); Soltar(cv.inter11); return false; }
    return true;
}
bool Converter11(Conjunto* c, Conv& cv, ID3D11DeviceContext* ctx, ID3D11Resource* src, unsigned bx, unsigned by, unsigned rw, unsigned rh, const char* nome)
{
    if (!Conv11Montar(c) || !cv.cs11 || !Inter11(c, cv, src)) return false;
    /* a copia e so LEITURA do recurso do jogo (D3D11 nao tem estados); a conversao roda sobre a
       copia, em recursos nossos */
    ctx->CopySubresourceRegion(cv.inter11, 0, 0, 0, 0, src, 0, nullptr);
    const unsigned k[4] = { bx, by, rw, rh };
    ctx->UpdateSubresource(c->cb11, 0, nullptr, k, 0, 0);
    /* X-CRASH-3: o contexto do jogo volta EXATAMENTE como estava (antes ficava zerado) */
    ID3D11ComputeShader* csAntes = nullptr; ID3D11Buffer* cbAntes = nullptr; ID3D11ShaderResourceView* srvAntes = nullptr; ID3D11UnorderedAccessView* uavAntes = nullptr;
    ctx->CSGetShader(&csAntes, nullptr, nullptr); ctx->CSGetConstantBuffers(0, 1, &cbAntes); ctx->CSGetShaderResources(0, 1, &srvAntes); ctx->CSGetUnorderedAccessViews(0, 1, &uavAntes);
    ctx->CSSetShader(cv.cs11, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &c->cb11);
    ctx->CSSetShaderResources(0, 1, &cv.srv11);
    UINT ini = (UINT)-1; ctx->CSSetUnorderedAccessViews(0, 1, &cv.uav11, &ini);
    ctx->Dispatch((rw + 7) / 8, (rh + 7) / 8, 1);
    ID3D11ShaderResourceView* ns = nullptr; ID3D11UnorderedAccessView* nu = nullptr;
    ctx->CSSetShaderResources(0, 1, &ns); ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr);   /* solta as nossas vistas antes de repor as dele */
    ctx->CSSetShader(csAntes, nullptr, 0); ctx->CSSetConstantBuffers(0, 1, &cbAntes); ctx->CSSetShaderResources(0, 1, &srvAntes); ctx->CSSetUnorderedAccessViews(0, 1, &uavAntes, nullptr);
    Soltar(csAntes); Soltar(cbAntes); Soltar(srvAntes); Soltar(uavAntes);
    if (cv.logadas++ == 0) L("colheita: %s convertida (D3D11: %ux%u, base %u,%u, srv fmt=%u)", nome, rw, rh, bx, by, (unsigned)cv.srvFmt);
    return true;
}

/* ------------------------------------------------------------------ */
/* O conjunto                                                           */
/* ------------------------------------------------------------------ */
/* Com mu. Garante um conjunto no device certo e com slots >= w x h. */
bool Garantir(const Entrada& e, IUnknown* depth, unsigned w, unsigned h)
{
    if (atual)
    {
        bool mesmoDev = atual->d3d12 == e.d3d12;
        if (mesmoDev && e.d3d12) { ID3D12Resource* t = nullptr; ID3D12Device* d = nullptr; if (SUCCEEDED(depth->QueryInterface(__uuidof(ID3D12Resource), reinterpret_cast<void**>(&t))) && t) { t->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&d)); t->Release(); } mesmoDev = d == atual->dev; if (d) d->Release(); }
        else if (mesmoDev) { ID3D11Resource* t = nullptr; ID3D11Device* d = nullptr; if (SUCCEEDED(depth->QueryInterface(__uuidof(ID3D11Resource), reinterpret_cast<void**>(&t))) && t) { t->GetDevice(&d); t->Release(); } mesmoDev = d == atual->dev11; if (d) d->Release(); }
        if (mesmoDev && atual->w >= w && atual->h >= h) return true;
        L("colheita: conjunto %ux%u aposentado (%s); refazendo em %ux%u", atual->w, atual->h, mesmoDev ? "cresceu" : "outro device", w, h);
        Aposentar();
    }
    Conjunto* c = new Conjunto();
    c->d3d12 = e.d3d12; c->w = w; c->h = h;
    if (e.d3d12)
    {
        ID3D12Resource* t = nullptr; if (FAILED(depth->QueryInterface(__uuidof(ID3D12Resource), reinterpret_cast<void**>(&t))) || !t) { delete c; return false; }
        t->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&c->dev)); t->Release();
        if (c->dev == nullptr) { delete c; return false; }
        c->depth12 = Criar12(c, w, h, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        c->mv12    = Criar12(c, w, h, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (!c->depth12 || !c->mv12 || FAILED(c->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&c->fVida)))) { Liberar(c); return false; }
    }
    else
    {
        ID3D11Resource* t = nullptr; if (FAILED(depth->QueryInterface(__uuidof(ID3D11Resource), reinterpret_cast<void**>(&t))) || !t) { delete c; return false; }
        t->GetDevice(&c->dev11); t->Release();
        if (c->dev11 == nullptr) { delete c; return false; }
        c->depth11 = Criar11(c, w, h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        c->mv11    = Criar11(c, w, h, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS);
        if (!c->depth11 || !c->mv11) { Liberar(c); return false; }
    }
    atual = c;
    L("colheita: conjunto %s %ux%u (depth R32F + MV R16G16F) para o FG/pelicula sem sessao .up", e.d3d12 ? "D3D12" : "D3D11", w, h);
    return true;
}

/* Prepara os conversores para os formatos do jogo (uma vez por formato). false = formato fora. */
bool PrepararConv(Conv& cv, DXGI_FORMAT jogoFmt, bool depth)
{
    if (cv.jogoFmt == jogoFmt) return true;
    if (depth)
    {
        /* familia R32: em D3D11 copia direta; em D3D12 a SRV tipada R32_FLOAT sobre o recurso do jogo */
        if (DepthCopiavel(jogoFmt)) { cv.ativo = false; cv.srvFmt = DXGI_FORMAT_R32_FLOAT; cv.jogoFmt = jogoFmt; cv.srvDe = nullptr; cv.logadas = 0; return true; }
        DXGI_FORMAT srv, tl;
        if (!DepthConversivel(jogoFmt, &srv, &tl)) return false;
        Soltar(cv.srv11); Soltar(cv.inter11); cv.srvDe = nullptr;
        cv.ativo = true; cv.srvFmt = srv; cv.interFmt = tl; cv.depthStencil = true; cv.jogoFmt = jogoFmt; cv.logadas = 0;
        return true;
    }
    if (jogoFmt == DXGI_FORMAT_R16G16_FLOAT) { cv.ativo = false; cv.srvFmt = jogoFmt; cv.jogoFmt = jogoFmt; cv.srvDe = nullptr; cv.logadas = 0; return true; }
    const DXGI_FORMAT srv = MvSrvFmt(jogoFmt);
    if (srv == DXGI_FORMAT_UNKNOWN) return false;
    Soltar(cv.srv11); Soltar(cv.inter11); cv.srvDe = nullptr;
    cv.ativo = true; cv.srvFmt = srv; cv.interFmt = jogoFmt; cv.depthStencil = false; cv.jogoFmt = jogoFmt; cv.logadas = 0;
    return true;
}

void PublicarImpl(const Entrada& e)
{
    if (e.depth == nullptr || e.mv == nullptr || e.rw == 0 || e.rh == 0) return;
    if (e.d3d12 && (e.lista == nullptr || e.fila == nullptr)) return;
    if (!e.d3d12 && e.ctx == nullptr) return;
    DXGI_FORMAT df = DXGI_FORMAT_UNKNOWN, mf = DXGI_FORMAT_UNKNOWN; unsigned dw = 0, dh = 0, mw = 0, mh = 0;
    if (!DescRes(e.depth, e.d3d12, &df, &dw, &dh) || !DescRes(e.mv, e.d3d12, &mf, &mw, &mh)) { AvisoUmaVez("depth/MV do jogo nao sao texturas 2D sem MSAA; sem dado"); return; }
    if (dw < e.bxDepth + e.rw || dh < e.byDepth + e.rh) { AvisoUmaVez("a depth do jogo e menor que o renderSize; sem dado"); return; }
    const unsigned mvW = e.mvDisplayRes ? e.outW : e.rw, mvH = e.mvDisplayRes ? e.outH : e.rh;
    if (mvW == 0 || mvH == 0 || mw < e.bxMv + mvW || mh < e.byMv + mvH) { AvisoUmaVez("o MV do jogo e menor que o esperado (render ou tela); sem dado"); return; }
    if (e.mvNdc) { AvisoUmaVez("MV em NDC (XeSS): convencao nao resolvida; sem dado"); return; }
    unsigned W = e.outW > dw ? e.outW : dw, H = e.outH > dh ? e.outH : dh;
    if (mw > W) W = mw;
    if (mh > H) H = mh;
    if (W < e.rw) W = e.rw;
    if (H < e.rh) H = e.rh;

    std::lock_guard<std::mutex> g(mu);
    if (parado.load()) return;
    if (!Garantir(e, e.depth, W, H)) { AvisoUmaVez("nao montou o conjunto; sem dado"); return; }
    Conjunto* c = atual;
    if (!PrepararConv(c->cDepth, df, true)) { char m[128]; snprintf(m, sizeof m, "depth do jogo em formato %u sem copia nem conversao; sem dado", (unsigned)df); AvisoUmaVez(m); return; }
    if (!PrepararConv(c->cMv, mf, false)) { char m[128]; snprintf(m, sizeof m, "MV do jogo em formato %u (inteiro?) sem conversao; sem dado", (unsigned)mf); AvisoUmaVez(m); return; }

    bool ok = true;
    if (e.d3d12)
    {
        ID3D12Resource* depth = static_cast<ID3D12Resource*>(e.depth); ID3D12Resource* mv = static_cast<ID3D12Resource*>(e.mv);
        /* X-CRASH-3: so leitura por SRV, sem barreira e sem copia do recurso do jogo (e.eDepth/e.eMv
           deixam de importar: nao presumimos estado nenhum alem do que o upscaler ja exige) */
        ok = Quadro12(c, e, depth, mv, mvW, mvH);
        if (!ok) return;
        /* X-CRASH-3: guarda a fila do jogo (AddRef uma vez) para o Signal de encerramento em Parar():
           um sinal enfileirado LA, depois da ultima lista do jogo que le os slots, e o que garante
           que a GPU largou os recursos antes de Liberar() - sem depender do Sleep(100). */
        if (c->fila == nullptr) { c->fila = e.fila; c->fila->AddRef(); }
        /* a fence de vida: sinalizada na fila AGORA (antes da lista do jogo); o cemiterio exige +2 */
        e.fila->Signal(c->fVida, ++c->vida);
        for (auto& v : velhos) if (v && v->d3d12 && v->dev == c->dev && v->fVida) e.fila->Signal(v->fVida, ++v->vida);   /* os aposentados tambem andam */
    }
    else
    {
        ID3D11Resource* depth = static_cast<ID3D11Resource*>(e.depth); ID3D11Resource* mv = static_cast<ID3D11Resource*>(e.mv);
        if (c->cDepth.ativo) ok = Converter11(c, c->cDepth, e.ctx, depth, e.bxDepth, e.byDepth, e.rw, e.rh, "depth");
        else { D3D11_BOX b = { e.bxDepth, e.byDepth, 0, e.bxDepth + e.rw, e.byDepth + e.rh, 1 }; e.ctx->CopySubresourceRegion(c->depth11, 0, 0, 0, 0, depth, 0, &b); }
        if (ok)
        {
            if (c->cMv.ativo) ok = Converter11(c, c->cMv, e.ctx, mv, e.bxMv, e.byMv, mvW, mvH, "mv");
            else { D3D11_BOX b = { e.bxMv, e.byMv, 0, e.bxMv + mvW, e.byMv + mvH, 1 }; e.ctx->CopySubresourceRegion(c->mv11, 0, 0, 0, 0, mv, 0, &b); }
        }
        if (!ok) return;
    }

    /* os dois balcoes de sempre */
    if (e.d3d12 && Fg::QuerDados())
    {
        Fg::DadosJogo p;
        p.depth = c->depth12; p.mv = c->mv12; p.fila = e.fila;
        p.renderW = e.rw; p.renderH = e.rh; p.mvW = mvW; p.mvH = mvH;
        p.jitterX = e.jx; p.jitterY = e.jy; p.mvScaleX = e.msx; p.mvScaleY = e.msy; p.dtMs = e.dtMs;
        p.camNear = e.camNear; p.camFar = e.camFar; p.camFov = e.camFov; p.viewToM = e.viewToM;
        p.depthInv = e.depthInv; p.mvDisplayRes = e.mvDisplayRes; p.mvJittered = e.mvJittered; p.mvNdc = e.mvNdc; p.reset = e.reset;
        p.origem = e.origemTxt;
        Fg::PublicarDados(p);
    }
    if (Ponte::Dados::Quer())
    {
        Ponte::Dados::Pacote p;
        if (e.d3d12) { p.depth12 = c->depth12; p.mv12 = c->mv12; p.fila = e.fila; }
        else { p.depth11 = c->depth11; p.mv11 = c->mv11; }
        p.renderW = e.rw; p.renderH = e.rh; p.mvW = mvW; p.mvH = mvH;
        p.jitterX = e.jx; p.jitterY = e.jy; p.mvScaleX = e.msx; p.mvScaleY = e.msy; p.dtMs = e.dtMs;
        p.camNear = e.camNear; p.camFar = e.camFar; p.camFov = e.camFov; p.viewToM = e.viewToM;
        p.depthInv = e.depthInv; p.mvDisplayRes = e.mvDisplayRes; p.mvJittered = e.mvJittered; p.mvNdc = e.mvNdc; p.reset = e.reset;
        p.origem = e.origem;
        Ponte::Dados::Publicar(p);
    }
    ++publicados;
    if (publicados == 1 || publicados == 300 || (publicados % 3000) == 0)
        L("colheita: depth+MV do jogo publicados sem sessao .up (%s %s, render %ux%u, mv %ux%u%s, depth %s, mv %s, depth_inv=%d; %u dispatches)",
          e.origemTxt, e.d3d12 ? "D3D12" : "D3D11", e.rw, e.rh, mvW, mvH, e.mvDisplayRes ? " em resolucao de tela" : "",
          c->cDepth.ativo ? "convertida" : "copiada", c->cMv.ativo ? "convertida" : "copiada", e.depthInv, publicados);
}

} // namespace anonimo

void Iniciar(LogFn log)
{
    L = log;
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpcFreq = f.QuadPart > 0 ? f.QuadPart : 1;
}
bool Ligada() { return L != nullptr && !parado.load() && !falhou.load() && Chave(); }
bool AlguemQuer() { return Ligada() && (Fg::QuerDados() || Ponte::Dados::Quer()); }

/* Thread do jogo, no dispatch do upscaler. Regra dura do modulo (colheita.h): NADA aqui derruba o
   jogo. O try/catch pega a excecao C++; a guarda do ponte_seh.h (I10) pega a falha de HARDWARE
   (violacao de acesso, instrucao ilegal...) que o try/catch NAO pega - o que fecharia o jogo. Na
   PRIMEIRA falha de hardware a colheita se DESLIGA para sempre nesta sessao (nunca insiste): o
   longjmp da guarda nao desenrola frame nenhum e pode ter deixado `mu` presa (ver ponte_seh.h), e
   nao voltar a Publicar e a garantia de que ninguem tenta pegar de novo essa trava. */
void Publicar(const Entrada& e)
{
    if (!AlguemQuer()) return;
    PtGuarda g;
    if (PT_GUARDA_ARMAR(g))
    {
        falhou.store(true);   /* uma vez desligada, nunca rearma (Ligada/AlguemQuer ja barram) */
        if (excecoes++ < 1 && L)
            L("colheita: ESTOURO 0x%08X em %p ao publicar depth/MV - colheita DESLIGADA nesta sessao (nao insiste; o jogo segue)", (unsigned)g.code, g.addr);
        return;
    }
    try { PublicarImpl(e); }
    catch (...) { if (excecoes++ < 8) L("colheita: excecao ao publicar depth/MV; este quadro fica sem dado"); }
    PtGuardaDesarmar(&g);
}

void Servico()
{
    if (parado.load() || falhou.load()) return;   /* X-CRASH-3: `mu` pode estar orfa apos um estouro */
    std::lock_guard<std::mutex> g(mu);
    Cemiterio(false);
}

/* Depois de os hooks dos upscalers sairem (nenhum Publicar em voo) e com o FG e a ponte
   parados (as referencias deles aos slots ja foram largadas por LimparDados/Limpar). */
void Parar()
{
    if (parado.exchange(true)) return;
    Fg::LimparDados(); Ponte::Dados::Limpar();
    /* X-CRASH-3: se um estouro desligou a colheita, `mu` pode ter ficado presa (o longjmp nao
       desenrola). Nao tentamos peg-la: preferimos vazar o conjunto a travar a thread da ponte -
       a GPU pode estar lendo os slots e a DLL ja esta se descarregando de qualquer modo. */
    if (falhou.load()) { if (publicados) L("colheita: encerrada apos %u dispatches (havia sido desligada por estouro; conjunto vazado de proposito)", publicados); return; }
    std::lock_guard<std::mutex> g(mu);
    if (atual)
    {
        Conjunto* c = atual; atual = nullptr;
        /* A ultima lista do jogo que LE os slots foi submetida na fila do jogo ANTES desta chamada
           (o gancho do upscaler ja saiu). O Signal de vida em Publicar e enfileirado ANTES da lista
           do jogo, entao concluir `vida` nao prova que a lista rodou. X-CRASH-3: enfileiramos um
           sinal NOVO na mesma fila AGORA - ele fica DEPOIS daquela lista, e conclui-lo prova que a
           GPU largou depth12/mv12. Sem a fila (nada publicado), o Sleep cobre. */
        if (c->d3d12 && c->fila && c->fVida)
        {
            const uint64_t alvo = ++c->vida;
            c->fila->Signal(c->fVida, alvo);
            if (c->fVida->GetCompletedValue() < alvo)
            {
                HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (ev) { if (SUCCEEDED(c->fVida->SetEventOnCompletion(alvo, ev))) WaitForSingleObject(ev, 1000); CloseHandle(ev); }
            }
        }
        else if (c->d3d12 && c->fVida && c->fVida->GetCompletedValue() < c->vida)
        {
            HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (ev) { if (SUCCEEDED(c->fVida->SetEventOnCompletion(c->vida, ev))) WaitForSingleObject(ev, 1000); CloseHandle(ev); }
        }
        if (c->d3d12) Sleep(100);   /* backstop: cobre o intervalo entre o sinal e o fim real da lista */
        Liberar(c);
    }
    Cemiterio(true);
    if (publicados) L("colheita: encerrada apos %u dispatches com depth+MV publicados sem sessao .up", publicados);
}

} } // namespace Ponte::Colheita
