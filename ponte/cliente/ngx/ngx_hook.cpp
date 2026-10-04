/* =====================================================================
   ngx_hook.cpp -- hook dos exports NGX do _nvngx.dll dentro do jogo.

   Fase 3 do Modo Integrado (CONTRATO-FASE3.md, 15/09/2026). Agente G1.

   ENTREGA 1 (sonda): engancha, com MinHook (BSD-2, vendorado em minhook\),
   NVSDK_NGX_D3D12_{CreateFeature,EvaluateFeature,ReleaseFeature,Init*,
   GetParameters,AllocateParameters,GetCapabilityParameters} e os D3D11
   equivalentes, e SO LOGA -- uma vez por mudanca -- o que o contrato pede:
   feature, tamanho interno e de saida, PerfQualityValue, presets, formatos e
   dimensoes de Color/MotionVectors/Depth/Output/ExposureTexture, escala de MV,
   jitter dos 16 primeiros quadros, pre-exposicao, reset, delta de tempo, flags
   dos recursos, e de qual command list/fila a chamada vem (cruzando com o
   hook de ExecuteCommandLists da ponte, via AoExecutar).

   Nada aqui muda um parametro nem um recurso. Todo hook chama a original
   intocada e devolve o que ela devolveu. Nada lanca. Erro = log uma vez.

   Como o _nvngx.dll pode ainda nao estar no processo quando a DLL entra
   (jogo que so inicializa o NGX ao ligar o DLSS nas opcoes), Servico() volta
   a procurar o modulo a cada chamada (barato: GetModuleHandleW).

   I15 (18/09/2026, RDR2): a GEOMETRIA de uma feature (Width/Height = render
   maximo, OutWidth/OutHeight = saida) e do jogo e nao se mexe. O jogo dimensiona
   os render targets pela consulta de optimal settings, e o Evaluate manda o
   render real em DLSS.Render.Subrect.Dimensions.*; o driver aceita render <=
   Width/Height criado (resolucao dinamica e implicita no NGX: nao existe flag de
   DRS em NVSDK_NGX_DLSS_Feature_Flags) mas recusa com InvalidParameter (0xBAD00005)
   um render fora da faixa do PerfQualityValue criado. "qualidade=dlaa" fazia
   Width=OutWidth no Create do JOGO: o jogo seguia renderizando 640x360 para
   1920x1080 e TODO Evaluate falhava, da nossa cobertura e da feature do jogo.
   Agora: (1) o PerfQualityValue so e trocado quando cabe na geometria do jogo;
   (2) a cobertura e criada com a geometria e as flags do jogo; (3) na primeira
   falha do Evaluate da cobertura ela e solta e o quadro e refeito na feature do
   jogo; render maior que o criado passa direto; 3 coberturas falhadas na sessao
   desligam a cobertura; um aviso ao app pelo .ngx-estado (chave aviso=).
   ===================================================================== */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <atomic>
#include <mutex>
#include <cstdarg>

#include "../minhook/include/MinHook.h"
#include "ngx_contrato.h"
#include "ngx_hook.h"
#include "ngx_upstream.h"   /* entrega 2: a troca da Color dentro do EvaluateFeature */
#include "../fg/fg_hook.h"  /* P1: o estado do FG vai no mesmo .ngx-estado */
#include "../tela/tela.h"    /* E8: modo de exibicao no mesmo .ngx-estado */

namespace Ngx {
namespace {

/* ================================================================== */
/* Estado                                                              */
/* ================================================================== */
LogFn  L = nullptr;
std::mutex mu;                      /* protege tabelas e assinaturas */
std::atomic<bool> hooksAtivos{ false };
std::atomic<int>  emVoo{ 0 };       /* chamadas dentro dos nossos hooks */
bool   mhIniciado = false;
bool   avisouSemModulo = false;
bool   parado = false;
HMODULE modulo = nullptr;
Estado E = {};
std::atomic<unsigned> estadoVersao{ 1 };   /* Q1: versao do estado publicado ao app (ver EscreverEstadoNgx) */

int64_t qpcFreq = 1;
int64_t Agora() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
double  Ms(int64_t de, int64_t ate) { return 1000.0 * double(ate - de) / double(qpcFreq); }

/* originais (trampolins do MinHook) */
FnD3D12CreateFeature   oD12Create = nullptr;
FnD3D12EvaluateFeature oD12Evaluate = nullptr;
FnD3D12ReleaseFeature  oD12Release = nullptr;
FnD3D11CreateFeature   oD11Create = nullptr;
FnD3D11EvaluateFeature oD11Evaluate = nullptr;
FnD3D11ReleaseFeature  oD11Release = nullptr;
FnInit                 oD12Init = nullptr, oD12InitExt = nullptr, oD11Init = nullptr, oD11InitExt = nullptr;
FnInitProjectID        oD12InitProj = nullptr, oD11InitProj = nullptr;
FnGetParameters        oD12GetParams = nullptr, oD12AllocParams = nullptr, oD12CapParams = nullptr;
FnGetParameters        oD11GetParams = nullptr, oD11AllocParams = nullptr, oD11CapParams = nullptr;
FnOptimalSettings      oOptimal = nullptr;     /* o callback original do objeto de parametros */
void*                  oOptimalDe = nullptr;   /* de que objeto o lemos (log) */

/* ------------------------------------------------------------------ */
/* Features vistas (por handle)                                        */
/* ------------------------------------------------------------------ */
struct FeatureRec
{
    const Handle* handle = nullptr;
    unsigned id = 0;            /* 32 bits do handle */
    unsigned feature = 0;       /* NVSDK_NGX_Feature */
    bool     d3d12 = true;
    unsigned w = 0, h = 0, outW = 0, outH = 0, pqv = 0, flags = 0;
    unsigned presets[6] = {};
    unsigned avaliacoes = 0;
    bool     vivo = false;
    bool     preHook = false;   /* I15: registrada num Evaluate (criada antes do hook): a geometria vem dos
                                   recursos do primeiro Evaluate, nao das chaves Width/Height/OutWidth/OutHeight
                                   do objeto (que guardam a ULTIMA consulta de optimal settings: Width = alvo,
                                   OutWidth = render otimo -- ao contrario do Create) */
};
const int kMaxFeatures = 16;
FeatureRec feats[kMaxFeatures];

FeatureRec* Achar(const Handle* h)
{
    for (auto& f : feats) if (f.vivo && f.handle == h) return &f;
    return nullptr;
}
FeatureRec* Registrar(const Handle* h)
{
    FeatureRec* f = Achar(h);
    if (f != nullptr) return f;
    for (auto& r : feats) if (!r.vivo) { r = FeatureRec(); r.handle = h; r.vivo = true; r.id = h ? h->Id : 0; return &r; }
    return nullptr;
}

/* ------------------------------------------------------------------ */
/* Descricao de recursos                                               */
/* ------------------------------------------------------------------ */
struct DescRec { unsigned fmt = 0, w = 0, h = 0, flags = 0, dim = 0; bool ok = false; };

DescRec Desc12(ID3D12Resource* r)
{
    DescRec d;
    if (r == nullptr) return d;
    D3D12_RESOURCE_DESC rd = r->GetDesc();
    d.fmt = (unsigned)rd.Format; d.w = (unsigned)rd.Width; d.h = rd.Height; d.flags = (unsigned)rd.Flags; d.dim = (unsigned)rd.Dimension; d.ok = true;
    return d;
}
DescRec Desc11(ID3D11Resource* r)
{
    DescRec d;
    if (r == nullptr) return d;
    ID3D11Texture2D* t = nullptr;
    if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&t))) && t != nullptr)
    {
        D3D11_TEXTURE2D_DESC td; t->GetDesc(&td);
        d.fmt = (unsigned)td.Format; d.w = td.Width; d.h = td.Height; d.flags = td.BindFlags; d.dim = 3; d.ok = true;
        t->Release();
    }
    return d;
}

/* Texto curto "fmt WxH" ou "-" */
void TxtDesc(char* out, size_t n, const DescRec& d)
{
    if (!d.ok) { snprintf(out, n, "-"); return; }
    snprintf(out, n, "%u@%ux%u", d.fmt, d.w, d.h);
}

/* Flags de criacao em texto */
void TxtFlags(char* out, size_t n, unsigned f)
{
    snprintf(out, n, "0x%X(%s%s%s%s%s%s%s)", f,
             (f & CF_HDR) ? "hdr " : "", (f & CF_MV_LOW_RES) ? "mv-lowres " : "", (f & CF_MV_JITTERED) ? "mv-jittered " : "",
             (f & CF_DEPTH_INVERTED) ? "depth-inv " : "", (f & CF_SHARPENING) ? "sharpen " : "",
             (f & CF_AUTO_EXPOSURE) ? "auto-expo " : "", (f & CF_ALPHA_UPSCALING) ? "alpha" : "");
}

/* ------------------------------------------------------------------ */
/* Assinatura da avaliacao (o que e logado "uma vez por mudanca")       */
/* ------------------------------------------------------------------ */
struct Assinatura
{
    unsigned feature, id, api;
    unsigned internoW, internoH, saidaW, saidaH, larguraParam, alturaParam;
    DescRec cor, mv, depth, out, expo, tm, bias;
    float mvScaleX, mvScaleY, preExpo, expoScale, sharp;
    unsigned baseColorX, baseColorY, baseDepthX, baseDepthY, baseMvX, baseMvY, baseOutX, baseOutY;
    unsigned listaTipo; bool imediato;
    unsigned flags;
};
Assinatura ultima = {};
bool temUltima = false;
int64_t ultimoLogAssinatura = 0;
int linhasAssinatura = 0;
const int kMaxLinhasAssinatura = 200;

/* recursos distintos vistos por slot (double buffering do jogo?) */
struct Vistos { void* p[8] = {}; int n = 0; bool Ver(void* x) { if (!x) return false; for (int i = 0; i < n; ++i) if (p[i] == x) return false; if (n < 8) p[n++] = x; return true; } };
Vistos vCor, vMv, vDepth, vOut, vExpo;

/* estatisticas por janela de 300 avaliacoes */
struct Janela
{
    unsigned n = 0, resets = 0;
    double origMs = 0, dtSoma = 0; float dtMin = 1e9f, dtMax = 0, preMin = 1e9f, preMax = -1e9f, jxMax = 0, jyMax = 0;
    int64_t inicio = 0;
} jan;
unsigned jitterLogados = 0;
unsigned totalAvaliacoes = 0;

/* ciclo: contagem de ExecuteCommandLists para saber onde o evaluate cai */
std::atomic<unsigned> eclTotal{ 0 };
std::atomic<unsigned> eclNoPresent{ 0 };
struct ListaEval { ID3D12CommandList* lista = nullptr; unsigned nAval = 0; unsigned eclNoRegistro = 0; int64_t t = 0; };
ListaEval listasEval[4]; int listasEvalIdx = 0;
int  cicloLogados = 0;
const int kMaxCiclo = 6;
unsigned presentes = 0;

/* ------------------------------------------------------------------ */
/* Log das avaliacoes                                                  */
/* ------------------------------------------------------------------ */
void LogAssinatura(const Assinatura& a, const FeatureRec* f)
{
    char cor[40], mv[40], dp[40], ou[40], ex[40], tm[40], bi[40], fl[96];
    TxtDesc(cor, sizeof cor, a.cor); TxtDesc(mv, sizeof mv, a.mv); TxtDesc(dp, sizeof dp, a.depth); TxtDesc(ou, sizeof ou, a.out);
    TxtDesc(ex, sizeof ex, a.expo); TxtDesc(tm, sizeof tm, a.tm); TxtDesc(bi, sizeof bi, a.bias);
    TxtFlags(fl, sizeof fl, a.flags);
    L("ngx: evaluate feature=%u (%s) api=%s handle=%u interno=%ux%u saida=%ux%u cor=%s mv=%s depth=%s out=%s expo=%s tm=%s bias=%s "
      "mvscale=%.3f,%.3f preexpo=%.4f exposcale=%.4f sharp=%.2f param(w,h)=%ux%u subrect(cor %u,%u depth %u,%u mv %u,%u out %u,%u) "
      "flags=%s lista=%s pqv=%s presets(dlaa/q/b/p/up/uq)=%c%c%c%c%c%c",
      a.feature, NomeFeature(a.feature), a.api == 12 ? "D3D12" : "D3D11", a.id,
      a.internoW, a.internoH, a.saidaW, a.saidaH, cor, mv, dp, ou, ex, tm, bi,
      a.mvScaleX, a.mvScaleY, a.preExpo, a.expoScale, a.sharp, a.larguraParam, a.alturaParam,
      a.baseColorX, a.baseColorY, a.baseDepthX, a.baseDepthY, a.baseMvX, a.baseMvY, a.baseOutX, a.baseOutY,
      fl, a.api == 12 ? (a.listaTipo == 0 ? "DIRECT" : a.listaTipo == 2 ? "COMPUTE" : "outro") : (a.imediato ? "ctx-imediato" : "ctx-deferido"),
      f ? NomePerfQuality(f->pqv) : "?",
      f ? LetraPreset(f->presets[0]) : '?', f ? LetraPreset(f->presets[1]) : '?', f ? LetraPreset(f->presets[2]) : '?',
      f ? LetraPreset(f->presets[3]) : '?', f ? LetraPreset(f->presets[4]) : '?', f ? LetraPreset(f->presets[5]) : '?');
    /* flags dos recursos (D3D12: ALLOW_*; D3D11: bind flags) -- uma vez por mudanca tambem */
    L("ngx: recursos flags cor=0x%X mv=0x%X depth=0x%X out=0x%X expo=0x%X (D3D12: 1=RT 2=DS 4=UAV 8=noSRV 0x10=cross 0x20=simult; D3D11: bind)",
      a.cor.flags, a.mv.flags, a.depth.flags, a.out.flags, a.expo.flags);
}

/* Le tudo do objeto de parametros e decide se loga. Chamada com o mutex. */
void SondarEvaluate(const Handle* h, const Parameter* p, bool d3d12, unsigned listaTipo, bool imediato, double origMs, Result r)
{
    FeatureRec* f = Achar(h);
    if (f == nullptr)
    {
        /* feature criada antes de o hook existir: registra pelo que da para ler */
        f = Registrar(h);
        if (f != nullptr)
        {
            f->feature = FEATURE_SUPER_SAMPLING;   /* suposicao: e a que tem Color/MV/Depth; o log diz "(suposto)" */
            f->d3d12 = d3d12; f->preHook = true;
            /* I15: Width/Height/OutWidth/OutHeight NAO sao a geometria da feature aqui (ver FeatureRec::preHook);
               a geometria e preenchida abaixo, dos recursos deste Evaluate. pqv e flags o jogo repoe a cada Create
               e ficam validos no objeto. */
            unsigned cw = 0, ch = 0, cow = 0, coh = 0;
            LerUI(p, kWidth, &cw); LerUI(p, kHeight, &ch); LerUI(p, kOutWidth, &cow); LerUI(p, kOutHeight, &coh);
            LerUI(p, kPerfQuality, &f->pqv); LerUI(p, kCreateFlags, &f->flags);
            L("ngx: evaluate com handle %u criado ANTES do hook: feature suposta DLSS-SR, pqv=%s flags=0x%X; a geometria vem dos recursos deste Evaluate (as chaves Width/Height=%ux%u OutWidth/OutHeight=%ux%u guardam a ultima consulta de optimal settings, nao o Create)",
              f->id, NomePerfQuality(f->pqv), f->flags, cw, ch, cow, coh);
        }
    }
    if (f != nullptr) ++f->avaliacoes;

    Assinatura a = {};
    a.feature = f ? f->feature : 0; a.id = h ? h->Id : 0; a.api = d3d12 ? 12 : 11;
    a.listaTipo = listaTipo; a.imediato = imediato; a.flags = f ? f->flags : 0;
    unsigned rw = 0, rh = 0;
    const bool temSub = LerUI(p, kRenderW, &rw) && LerUI(p, kRenderH, &rh) && rw > 0 && rh > 0;
    LerUI(p, kWidth, &a.larguraParam); LerUI(p, kHeight, &a.alturaParam);

    ID3D12Resource* r12 = nullptr; ID3D11Resource* r11 = nullptr;
    #define SLOT(chave, campo, vistos) \
        if (d3d12) { if (LerD3D12(p, chave, &r12)) { a.campo = Desc12(r12); vistos.Ver(r12); } } \
        else       { if (LerD3D11(p, chave, &r11)) { a.campo = Desc11(r11); vistos.Ver(r11); } }
    SLOT(kColor, cor, vCor); SLOT(kMotionVectors, mv, vMv); SLOT(kDepth, depth, vDepth); SLOT(kOutput, out, vOut);
    SLOT(kExposureTexture, expo, vExpo);
    { Vistos tmpv; SLOT(kTransparencyMask, tm, tmpv); SLOT(kBiasMask, bias, tmpv); }
    #undef SLOT

    /* render: subrect do Evaluate > Width/Height do Create > (pre-hook) a Color > chave Width;
       saida: OutWidth/OutHeight do Create > (pre-hook) o Output > chave OutWidth */
    const bool geomDoCreate = f != nullptr && f->w != 0 && f->outW != 0;
    a.internoW = temSub ? rw : (geomDoCreate ? f->w : (a.cor.ok ? a.cor.w : a.larguraParam));
    a.internoH = temSub ? rh : (geomDoCreate ? f->h : (a.cor.ok ? a.cor.h : a.alturaParam));
    if (geomDoCreate) { a.saidaW = f->outW; a.saidaH = f->outH; }
    else if (a.out.ok) { a.saidaW = a.out.w; a.saidaH = a.out.h; }
    else { LerUI(p, kOutWidth, &a.saidaW); LerUI(p, kOutHeight, &a.saidaH); }
    if (f != nullptr && f->preHook && !geomDoCreate && a.internoW && a.saidaW)
    {
        f->w = a.internoW; f->h = a.internoH; f->outW = a.saidaW; f->outH = a.saidaH;
        L("ngx: handle %u (criado antes do hook): geometria inferida render=%ux%u saida=%ux%u (%s)", f->id, f->w, f->h, f->outW, f->outH,
          temSub ? "subrect do Evaluate + Output" : "Color + Output");
    }

    LerF(p, kMvScaleX, &a.mvScaleX); LerF(p, kMvScaleY, &a.mvScaleY);
    LerF(p, kPreExposure, &a.preExpo); LerF(p, kExposureScale, &a.expoScale); LerF(p, kSharpness, &a.sharp);
    LerUI(p, kColorBaseX, &a.baseColorX); LerUI(p, kColorBaseY, &a.baseColorY);
    LerUI(p, kDepthBaseX, &a.baseDepthX); LerUI(p, kDepthBaseY, &a.baseDepthY);
    LerUI(p, kMvBaseX, &a.baseMvX); LerUI(p, kMvBaseY, &a.baseMvY);
    LerUI(p, kOutBaseX, &a.baseOutX); LerUI(p, kOutBaseY, &a.baseOutY);

    float jx = 0, jy = 0, dt = 0; int reset = 0;
    const bool temJ = LerF(p, kJitterX, &jx) && LerF(p, kJitterY, &jy);
    const bool temDt = LerF(p, kFrameTimeDelta, &dt);
    LerI(p, kReset, &reset);

    ++totalAvaliacoes;
    const bool ehSR = (a.feature == FEATURE_SUPER_SAMPLING) || (a.cor.ok && a.mv.ok && a.depth.ok);

    /* Conferencia da vtable (uma vez): Width por Get(uint*), Color por Get(ID3D12Resource**|ID3D11Resource**),
       MV.Scale.X por Get(float*). Se a leitura tipada falhar, a ordem dos slots esta errada para
       este _nvngx.dll e a linha abaixo denuncia antes de alguem confiar nos numeros. */
    if (totalAvaliacoes == 1)
    {
        unsigned w = 0; float sx = 0; bool okW = LerUI(p, kWidth, &w), okS = LerF(p, kMvScaleX, &sx);
        L("ngx: contrato: leitura tipada Width=%s(%u) Color=%s MV.Scale.X=%s(%.3f) -- %s",
          okW ? "ok" : "FALHOU", w, a.cor.ok ? "ok" : "FALHOU", okS ? "ok" : "ausente/FALHOU", sx,
          (okW && a.cor.ok) ? "vtable do NVSDK_NGX_Parameter conferida" : "ATENCAO: ordem da vtable suspeita, numeros abaixo nao valem");
    }

    /* estado consultavel pela ponte */
    if (ehSR)
    {
        if (!E.viuSR) ++estadoVersao;   /* Q1: o app fica sabendo que ha DLSS SR vivo */
        E.viuSR = true; E.api = a.api;
        E.internoW = a.internoW; E.internoH = a.internoH; E.saidaW = a.saidaW; E.saidaH = a.saidaH;
        E.corFmt = a.cor.fmt; E.mvFmt = a.mv.fmt; E.depthFmt = a.depth.fmt; E.saidaFmt = a.out.fmt;
        E.mvW = a.mv.w; E.mvH = a.mv.h; E.depthW = a.depth.w; E.depthH = a.depth.h;
        E.flags = a.flags; E.mvScaleX = a.mvScaleX; E.mvScaleY = a.mvScaleY;
        ++E.avaliacoesSR;   /* X-FG-6 */
    }
    E.avaliacoes = totalAvaliacoes;

    /* uma vez por mudanca (com teto de linhas e de taxa) */
    const bool mudou = !temUltima || memcmp(&a, &ultima, sizeof(a)) != 0;
    const int64_t agora = Agora();
    if (mudou && linhasAssinatura < kMaxLinhasAssinatura && (linhasAssinatura == 0 || Ms(ultimoLogAssinatura, agora) > 200.0))
    {
        LogAssinatura(a, f);
        ultima = a; temUltima = true; ultimoLogAssinatura = agora; ++linhasAssinatura;
        if (linhasAssinatura == kMaxLinhasAssinatura) L("ngx: teto de %d linhas de assinatura atingido; parando de logar mudancas", kMaxLinhasAssinatura);
    }
    /* I15: nunca em serie (foram 2.799 linhas iguais no RDR2): as 3 primeiras e depois uma a cada 300 */
    if (Falhou(r))
    {
        static unsigned falhasLogadas = 0;
        ++falhasLogadas;
        if (falhasLogadas <= 3 || falhasLogadas % 300 == 0)
            L("ngx: evaluate feature=%u handle=%u DEVOLVEU 0x%08X (%s) render=%ux%u saida=%ux%u criada=%ux%u->%ux%u pqv=%s (falha %u desta sessao)",
              a.feature, a.id, r, NomeResultado(r), a.internoW, a.internoH, a.saidaW, a.saidaH,
              f ? f->w : 0u, f ? f->h : 0u, f ? f->outW : 0u, f ? f->outH : 0u, f ? NomePerfQuality(f->pqv) : "?", falhasLogadas);
    }

    /* jitter dos 16 primeiros quadros */
    if (ehSR && jitterLogados < 16)
    {
        ++jitterLogados;
        L("ngx: jitter quadro=%u x=%.5f y=%.5f dt=%.3f ms reset=%d preexpo=%.4f (%s)", jitterLogados, jx, jy, dt, reset, a.preExpo,
          temJ ? "chaves Jitter.Offset.X/Y presentes" : "SEM chaves de jitter");
    }

    /* janela de 300 */
    if (jan.n == 0) jan.inicio = agora;
    ++jan.n; jan.origMs += origMs; if (reset) ++jan.resets;
    if (temDt) { jan.dtSoma += dt; if (dt < jan.dtMin) jan.dtMin = dt; if (dt > jan.dtMax) jan.dtMax = dt; }
    if (a.preExpo < jan.preMin) jan.preMin = a.preExpo; if (a.preExpo > jan.preMax) jan.preMax = a.preExpo;
    if (fabsf(jx) > jan.jxMax) jan.jxMax = fabsf(jx); if (fabsf(jy) > jan.jyMax) jan.jyMax = fabsf(jy);
    if (jan.n >= 300)
    {
        const double seg = Ms(jan.inicio, agora) / 1000.0;
        const bool temDtJan = jan.dtMin <= jan.dtMax;
        L("ngx: 300 avaliacoes | copia-in 0.00 ms | espera 0.00 ms | troca 0.00 ms | original %.3f ms | %.1f aval/s | dt %.2f..%.2f med %.2f ms%s | reset %u | preexpo %.4f..%.4f | |jitter| <= %.3f,%.3f | recursos distintos cor=%d mv=%d depth=%d out=%d expo=%d",
          jan.origMs / jan.n, seg > 0 ? jan.n / seg : 0.0, temDtJan ? jan.dtMin : 0.0f, temDtJan ? jan.dtMax : 0.0f, jan.dtSoma / jan.n, temDtJan ? "" : " (sem FrameTimeDeltaInMsec)", jan.resets,
          jan.preMin, jan.preMax, jan.jxMax, jan.jyMax, vCor.n, vMv.n, vDepth.n, vOut.n, vExpo.n);
        jan = Janela();
    }
}

void SondarCreate(const Handle* h, unsigned feature, const Parameter* p, bool d3d12, void* lista, Result r)
{
    FeatureRec* f = Registrar(h);
    if (f == nullptr) { L("ngx: create feature=%u: tabela de features cheia (16)", feature); return; }
    f->feature = feature; f->d3d12 = d3d12;
    LerUI(p, kWidth, &f->w); LerUI(p, kHeight, &f->h); LerUI(p, kOutWidth, &f->outW); LerUI(p, kOutHeight, &f->outH);
    LerUI(p, kPerfQuality, &f->pqv); LerUI(p, kCreateFlags, &f->flags);
    const char* chaves[6] = { kPresetDLAA, kPresetQuality, kPresetBalanced, kPresetPerformance, kPresetUltraPerf, kPresetUltraQuality };
    for (int i = 0; i < 6; ++i) { f->presets[i] = 0; LerUI(p, chaves[i], &f->presets[i]); }
    unsigned subrects = 0; LerUI(p, kEnableOutputSubrects, &subrects);
    char fl[96]; TxtFlags(fl, sizeof fl, f->flags);
    L("ngx: create feature=%u (%s) api=%s handle=%u interno=%ux%u saida=%ux%u pqv=%s flags=%s presets(dlaa/q/b/p/up/uq)=%c%c%c%c%c%c (bruto %u/%u/%u/%u/%u/%u) subrects=%u lista=0x%p thread=%lu -> %s (0x%08X)",
      feature, NomeFeature(feature), d3d12 ? "D3D12" : "D3D11", f->id, f->w, f->h, f->outW, f->outH, NomePerfQuality(f->pqv), fl,
      LetraPreset(f->presets[0]), LetraPreset(f->presets[1]), LetraPreset(f->presets[2]), LetraPreset(f->presets[3]), LetraPreset(f->presets[4]), LetraPreset(f->presets[5]),
      f->presets[0], f->presets[1], f->presets[2], f->presets[3], f->presets[4], f->presets[5],
      subrects, lista, GetCurrentThreadId(), NomeResultado(r), r);
    if (Falhou(r)) f->vivo = false;
    ++estadoVersao;
}

/* ================================================================== */
/* Entrega 3: controle do DLSS do jogo (preset, qualidade, DLAA)        */
/* ------------------------------------------------------------------ */
struct ControleNgx
{
    unsigned preset = 0;      /* 0 = nao mexer; 10=J 11=K 12=L 13=M */
    int qualidade = -2;       /* -2 = nao mexer; -1 = "jogo"; 0..5 = PerfQuality */
    bool ligado = false;
    FILETIME mtime = {};
    bool lido = false;
    unsigned geracao = 0;
} cn;
char avisoApp[200] = "";   /* I15: um aviso ao app pelo .ngx-estado (aviso=); "" = nenhum. Ver Avisar(). */

unsigned PresetDaLetra(const char* v)
{
    if (!v || !*v) return 0;
    if (!_stricmp(v, "default") || !_stricmp(v, "jogo") || !_stricmp(v, "padrao")) return 0;
    char c = (char)toupper((unsigned char)v[0]);
    if (c >= 'A' && c <= 'O') return (unsigned)(c - 'A' + 1);   /* K=11 */
    return (unsigned)strtoul(v, nullptr, 0);
}
int QualidadeDoTexto(const char* v)
{
    if (!v || !*v || !_stricmp(v, "jogo")) return -1;
    if (!_stricmp(v, "dlaa")) return PQ_DLAA;
    if (!_stricmp(v, "quality") || !_stricmp(v, "qualidade")) return PQ_MAX_QUALITY;
    if (!_stricmp(v, "balanced") || !_stricmp(v, "equilibrado")) return PQ_BALANCED;
    if (!_stricmp(v, "performance") || !_stricmp(v, "desempenho")) return PQ_MAX_PERF;
    if (!_stricmp(v, "ultra-performance")) return PQ_ULTRA_PERF;
    if (!_stricmp(v, "ultra-quality")) return PQ_ULTRA_QUALITY;
    return -2;
}
void CaminhoNgx(wchar_t* out, size_t n)
{
    wchar_t tmp[MAX_PATH] = L"";
    DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (k == 0 || k >= MAX_PATH) { tmp[0] = L'.'; tmp[1] = L'\\'; tmp[2] = 0; }
    _snwprintf(out, n, L"%sdlss5-ponte.%lu.ngx", tmp, GetCurrentProcessId());
}
void LerControleNgx()
{
    wchar_t caminho[MAX_PATH]; CaminhoNgx(caminho, MAX_PATH);
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    if (!GetFileAttributesExW(caminho, GetFileExInfoStandard, &fa)) return;
    if (cn.lido && CompareFileTime(&fa.ftLastWriteTime, &cn.mtime) == 0) return;
    cn.mtime = fa.ftLastWriteTime; cn.lido = true;
    FILE* f = _wfopen(caminho, L"rb");
    if (!f) return;
    unsigned presetAntes = cn.preset; int qAntes = cn.qualidade;
    char linha[256];
    while (fgets(linha, sizeof linha, f))
    {
        char* eq = strchr(linha, '='); if (!eq) continue;
        *eq = 0; const char* k = linha; char* v = eq + 1;
        char* fim = v + strlen(v); while (fim > v && (fim[-1] == '\r' || fim[-1] == '\n' || fim[-1] == ' ')) *--fim = 0;
        if (!strcmp(k, "dlss_preset")) { cn.preset = PresetDaLetra(v); cn.ligado = true; }
        else if (!strcmp(k, "dlss_qualidade")) { cn.qualidade = QualidadeDoTexto(v); cn.ligado = true; }
    }
    fclose(f);
    if (cn.preset != presetAntes || cn.qualidade != qAntes)
    {
        ++cn.geracao; ++estadoVersao;
        avisoApp[0] = 0;   /* I15: pedido novo, aviso novo (se ainda couber) */
        L("ngx: controle: preset=%c(%u) qualidade=%s -> vale na proxima CreateFeature do jogo; se ha feature viva, recrio no proximo evaluate (geracao %u)",
          LetraPreset(cn.preset), cn.preset, cn.qualidade == -1 ? "jogo" : cn.qualidade == -2 ? "(nao mexer)" : NomePerfQuality(cn.qualidade), cn.geracao);
    }
}
/* I15: razao nominal render/saida de cada PerfQualityValue do DLSS SR (o que a consulta de
   optimal settings devolve para cada um). O PerfQualityValue de um Create tem de casar com
   a geometria: e o jogo quem escolhe o tamanho de render, e um valor que nao casa faz o
   driver recusar o Evaluate (InvalidParameter). */
float RazaoNominal(unsigned pqv)
{
    switch (pqv)
    {
    case PQ_ULTRA_PERF: return 1.0f / 3.0f;
    case PQ_MAX_PERF: return 0.5f;
    case PQ_BALANCED: return 0.58f;
    case PQ_MAX_QUALITY: return 2.0f / 3.0f;
    case PQ_ULTRA_QUALITY: return 0.77f;
    case PQ_DLAA: return 1.0f;
    default: return -1.0f;
    }
}
bool QualidadeCabe(unsigned pqv, unsigned w, unsigned h, unsigned ow, unsigned oh)
{
    if (w == 0 || h == 0 || ow == 0 || oh == 0) return false;
    if (pqv == PQ_DLAA) return w == ow && h == oh;
    const float rz = RazaoNominal(pqv);
    if (rz <= 0.0f) return false;
    return fabsf((float)w / (float)ow - rz) <= 0.03f && fabsf((float)h / (float)oh - rz) <= 0.03f;
}
/* o nome da qualidade cuja razao nominal mais se aproxima de w/ow (para o log e o aviso) */
const char* NomeDaRazao(unsigned w, unsigned ow)
{
    if (w == 0 || ow == 0) return "?";
    const float r = (float)w / (float)ow;
    const unsigned cands[6] = { PQ_ULTRA_PERF, PQ_MAX_PERF, PQ_BALANCED, PQ_MAX_QUALITY, PQ_ULTRA_QUALITY, PQ_DLAA };
    unsigned melhor = PQ_DLAA; float dist = 1e9f;
    for (unsigned c : cands) { const float d = fabsf(RazaoNominal(c) - r); if (d < dist) { dist = d; melhor = c; } }
    return dist <= 0.03f ? NomePerfQuality(melhor) : "razao propria do jogo";
}

/* I15: o aviso ao app (chave aviso= do .ngx-estado, lido a cada 1 s pelo card "DLSS do jogo").
   Um so por vez; muda so quando o texto muda. */
void Avisar(const char* fmt, ...)
{
    char t[200];
    va_list va; va_start(va, fmt); vsnprintf(t, sizeof t, fmt, va); va_end(va);
    if (strcmp(t, avisoApp) == 0) return;
    strncpy(avisoApp, t, sizeof avisoApp - 1); avisoApp[sizeof avisoApp - 1] = 0;
    ++estadoVersao;
}
/* uma linha por (qualidade pedida, geometria) */
struct { int qualidade = -2; unsigned w = 0, h = 0, ow = 0, oh = 0; } qualAvisada;
void AvisarQualidadeNaoCabe(const char* onde, unsigned w, unsigned h, unsigned ow, unsigned oh)
{
    if (qualAvisada.qualidade == cn.qualidade && qualAvisada.w == w && qualAvisada.h == h && qualAvisada.ow == ow && qualAvisada.oh == oh) return;
    qualAvisada.qualidade = cn.qualidade; qualAvisada.w = w; qualAvisada.h = h; qualAvisada.ow = ow; qualAvisada.oh = oh;
    L("ngx: controle: qualidade=%s pedida (%s), mas o jogo renderiza %ux%u para %ux%u (razao %.3f = %s): a feature fica com a geometria e o PerfQualityValue do jogo. "
      "O tamanho de render e decidido pelo jogo na consulta de optimal settings; trocar Width/Height ou o PerfQualityValue no Create faz o driver recusar todo Evaluate com InvalidParameter (RDR2, 18/09/2026)",
      NomePerfQuality((unsigned)cn.qualidade), onde, w, h, ow, oh, ow ? (float)w / (float)ow : 0.0f, NomeDaRazao(w, ow));
    Avisar("qualidade %s nao se aplica: o jogo renderiza %ux%u para %ux%u (%s)", NomePerfQuality((unsigned)cn.qualidade), w, h, ow, oh, NomeDaRazao(w, ow));
}
/* Preset: hint, sempre aplicavel. Qualidade: so o PerfQualityValue, e so quando cabe na
   geometria que o objeto ja traz (I15). Nunca toca Width/Height/OutWidth/OutHeight. */
void AplicarOverride(Parameter* p, const char* onde)
{
    if (!cn.ligado || p == nullptr) return;
    if (cn.preset != 0)
    {
        const char* chaves[6] = { kPresetDLAA, kPresetQuality, kPresetBalanced, kPresetPerformance, kPresetUltraPerf, kPresetUltraQuality };
        for (const char* c : chaves) PorUI(p, c, cn.preset);
    }
    if (cn.qualidade >= 0)
    {
        unsigned w = 0, h = 0, ow = 0, oh = 0;
        LerUI(p, kWidth, &w); LerUI(p, kHeight, &h); LerUI(p, kOutWidth, &ow); LerUI(p, kOutHeight, &oh);
        if (QualidadeCabe((unsigned)cn.qualidade, w, h, ow, oh)) PorUI(p, kPerfQuality, (unsigned)cn.qualidade);
        else AvisarQualidadeNaoCabe(onde, w, h, ow, oh);
    }
}
/* I15: `desligadaPara`/`desligadaGeracao`: a cobertura falhou (ou nao coube) para este handle do
   jogo nesta geracao do controle; nao se tenta de novo ate o proximo Create do jogo ou uma
   mudanca no controle. `coberturasFalhadas`: por sessao; em kMaxCoberturasFalhadas a cobertura
   fica desligada ate o fim da sessao. */
const unsigned kMaxCoberturasFalhadas = 3;
struct Recreate
{
    Handle* nosso = nullptr; Handle* aLiberar = nullptr; unsigned liberarEm = 0; unsigned geracaoAtiva = 0; const Handle* baseJogo = nullptr;
    const Handle* desligadaPara = nullptr; unsigned desligadaGeracao = 0; unsigned coberturasFalhadas = 0; bool avisouSessao = false;
} rc;

/* I15: solta a cobertura (3 quadros depois, como sempre: pode haver quadro em voo com ela) e
   deixa o handle do jogo em uso. Chamada com o mutex. */
void SoltarCobertura(const char* motivo)
{
    if (rc.nosso == nullptr) return;
    if (rc.aLiberar) oD12Release(rc.aLiberar);
    if (FeatureRec* nf = Achar(rc.nosso)) nf->vivo = false;
    const unsigned idNosso = rc.nosso->Id, idJogo = rc.baseJogo ? rc.baseJogo->Id : 0u;
    rc.aLiberar = rc.nosso; rc.liberarEm = 3; rc.nosso = nullptr;
    rc.desligadaPara = rc.baseJogo; rc.desligadaGeracao = cn.geracao; rc.baseJogo = nullptr;
    ++rc.coberturasFalhadas; ++estadoVersao;
    L("ngx: controle: cobertura (handle %u) solta: %s; a feature do jogo (handle %u) volta a ser a avaliada; a cobertura so e refeita no proximo Create do jogo ou numa mudanca do controle (%u/%u coberturas falhadas nesta sessao)",
      idNosso, motivo, idJogo, rc.coberturasFalhadas, kMaxCoberturasFalhadas);
    if (rc.coberturasFalhadas >= kMaxCoberturasFalhadas && !rc.avisouSessao)
    {
        rc.avisouSessao = true;
        L("ngx: controle: %u coberturas falhadas; preset/qualidade por cobertura desligados ate o fim desta sessao (o preset ainda vale no proximo Create do jogo)", rc.coberturasFalhadas);
        Avisar("preset/qualidade por cobertura desligados nesta sessao (%u falhas); o DLSS do jogo segue como o jogo criou", rc.coberturasFalhadas);
    }
    else Avisar("cobertura do DLSS solta (%s); o DLSS do jogo segue como o jogo criou", motivo);
}

/* Q1 (16/09/2026): o ESTADO do NGX para o app, em %TEMP%\dlss5-ponte.<pid>.ngx-estado. O card
   "DLSS do jogo" do painel le este arquivo a cada segundo: ngx=0 (o _nvngx.dll nao esta no
   processo) esmaece o card; com feature de SR viva, mostra o preset/qualidade em uso. Escrito
   so pela thread da ponte (Servico) e so quando a versao muda; apagado em Parar. */
unsigned estadoEscrito = 0;
unsigned fgEscrito = 0;   /* P1: versao do estado do FG ja escrita */
unsigned telaEscrito = 0; /* E8: versao do estado de exibicao ja escrita */
void CaminhoEstadoNgx(wchar_t* out, size_t n)
{
    wchar_t tmp[MAX_PATH] = L"";
    DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (k == 0 || k >= MAX_PATH) { tmp[0] = L'.'; tmp[1] = L'\\'; tmp[2] = 0; }
    _snwprintf(out, n, L"%sdlss5-ponte.%lu.ngx-estado", tmp, GetCurrentProcessId());
}
void EscreverEstadoNgx()
{
    const unsigned v = estadoVersao.load();
    const unsigned vf = Fg::EstadoVersao();
    const unsigned vt = Tela::EstadoVersao();
    if (v == estadoEscrito && vf == fgEscrito && vt == telaEscrito) return;
    estadoEscrito = v; fgEscrito = vf; telaEscrito = vt;
    wchar_t caminho[MAX_PATH]; CaminhoEstadoNgx(caminho, MAX_PATH);
    char corpo[512];
    {
        std::lock_guard<std::mutex> g(mu);
        /* a feature de SR em uso: a nossa (recriada) ou a do jogo com mais avaliacoes */
        const FeatureRec* sr = rc.nosso ? Achar(rc.nosso) : nullptr;
        if (sr == nullptr) for (auto& r : feats) if (r.vivo && r.feature == FEATURE_SUPER_SAMPLING && (sr == nullptr || r.avaliacoes > sr->avaliacoes)) sr = &r;
        char presetUso = '-'; const char* qualUso = "-";
        if (sr)
        {
            const unsigned idx = sr->pqv == PQ_DLAA ? 0 : sr->pqv == PQ_MAX_QUALITY ? 1 : sr->pqv == PQ_BALANCED ? 2 : sr->pqv == PQ_MAX_PERF ? 3 : sr->pqv == PQ_ULTRA_PERF ? 4 : 5;
            presetUso = LetraPreset(sr->presets[idx]); qualUso = NomePerfQuality(sr->pqv);
        }
        /* I15: aviso= (uma linha, "" = nenhum) para o card/cartaz do app: cobertura solta ou qualidade que nao cabe */
        snprintf(corpo, sizeof corpo, "ngx=%d\nsr=%d\napi=%u\npreset=%c\nqualidade=%s\nrecriada=%u\npreset_pedido=%c\nqualidade_pedida=%s\ngeracao=%u\naviso=%s\n",
                 hooksAtivos.load() ? 1 : 0, E.viuSR ? 1 : 0, E.api, presetUso, qualUso, rc.nosso ? rc.nosso->Id : 0u,
                 cn.preset ? LetraPreset(cn.preset) : '-', cn.qualidade >= 0 ? NomePerfQuality((unsigned)cn.qualidade) : "jogo", cn.geracao, avisoApp);
    }
    char fg[256]; Fg::EstadoTexto(fg, sizeof fg);   /* P1; J1 acrescentou fg_dados e os dois fps */
    char tela[80]; Tela::EstadoTexto(tela, sizeof tela);   /* E8 */
    FILE* f = _wfopen(caminho, L"wb");
    if (f == nullptr) return;
    fputs(corpo, f); fputs(fg, f); fputs(tela, f); fclose(f);
}

const Handle* TalvezRecriar(ID3D12GraphicsCommandList* lista, const Handle* hJogo, const Parameter* p)
{
    if (!cn.ligado || lista == nullptr || oD12Create == nullptr || oD12Release == nullptr) return hJogo;
    /* Q1: o controle voltou a "jogo" (preset 0 e qualidade jogo): a nossa feature sai de cena e o
       handle do jogo volta a ser o avaliado; a nossa e liberada 3 quadros depois, como sempre. */
    if (rc.nosso != nullptr && rc.geracaoAtiva != cn.geracao && cn.preset == 0 && cn.qualidade < 0)
    {
        if (rc.aLiberar) oD12Release(rc.aLiberar);
        if (FeatureRec* nf = Achar(rc.nosso)) nf->vivo = false;
        rc.aLiberar = rc.nosso; rc.liberarEm = 3; rc.nosso = nullptr; rc.geracaoAtiva = cn.geracao;
        L("ngx: controle: voltando a feature do jogo (handle %u); a nossa (handle %u) e liberada em 3 quadros", hJogo ? hJogo->Id : 0u, rc.aLiberar->Id);
        ++estadoVersao;
    }
    /* I15: cobertura desligada para este handle nesta geracao (falhou ou nao coube), ou para a sessao */
    const bool podeCobrir = !(rc.desligadaPara == hJogo && rc.desligadaGeracao == cn.geracao) && rc.coberturasFalhadas < kMaxCoberturasFalhadas;
    if (!(rc.nosso != nullptr && rc.geracaoAtiva == cn.geracao && rc.baseJogo == hJogo) && (cn.preset != 0 || cn.qualidade >= 0) && podeCobrir)
    {
        FeatureRec* f = Achar(hJogo);
        if (f == nullptr || f->w == 0 || f->outW == 0) return hJogo;
        /* I15: a cobertura so vale se muda algo: um preset, ou uma qualidade que cabe na geometria do
           jogo. Uma copia identica da feature do jogo e risco sem ganho. */
        const bool qualCabe = cn.qualidade >= 0 && QualidadeCabe((unsigned)cn.qualidade, f->w, f->h, f->outW, f->outH);
        if (cn.qualidade >= 0 && !qualCabe) AvisarQualidadeNaoCabe("cobertura", f->w, f->h, f->outW, f->outH);
        if (cn.preset == 0 && !qualCabe) { rc.desligadaPara = hJogo; rc.desligadaGeracao = cn.geracao; return hJogo; }
        Parameter* pc = nullptr;   /* GetParameters devolve objeto persistente (sem DestroyParameters); evita vazamento */
        FnGetParameters aloc = oD12GetParams ? oD12GetParams : oD12AllocParams;
        if (aloc == nullptr || Falhou(aloc(&pc)) || pc == nullptr) return hJogo;
        /* I15: a geometria, o PerfQualityValue e as flags sao os do jogo (Width/Height = render maximo
           do jogo; com resolucao dinamica o Evaluate manda um render <= isso e o driver aceita) */
        PorUI(pc, kWidth, f->w); PorUI(pc, kHeight, f->h);
        PorUI(pc, kOutWidth, f->outW); PorUI(pc, kOutHeight, f->outH);
        PorUI(pc, kPerfQuality, f->pqv);
        PorUI(pc, kCreateFlags, f->flags);
        AplicarOverride(pc, "cobertura");
        Handle* novo = nullptr;
        const Result r = oD12Create(lista, FEATURE_SUPER_SAMPLING, pc, &novo);
        if (Falhou(r) || novo == nullptr)
        {
            rc.desligadaPara = hJogo; rc.desligadaGeracao = cn.geracao; ++rc.coberturasFalhadas;
            L("ngx: controle: recreate falhou (%s); seguindo com a feature do jogo (%u/%u)", NomeResultado(r), rc.coberturasFalhadas, kMaxCoberturasFalhadas);
            return hJogo;
        }
        if (rc.nosso) { if (rc.aLiberar) oD12Release(rc.aLiberar); if (FeatureRec* vf = Achar(rc.nosso)) vf->vivo = false; rc.aLiberar = rc.nosso; rc.liberarEm = 3; }
        rc.nosso = novo; rc.geracaoAtiva = cn.geracao; rc.baseJogo = hJogo;
        FeatureRec* nf = Registrar(novo);
        if (nf)
        {
            *nf = *f; nf->handle = novo; nf->id = novo->Id; nf->preHook = false; nf->avaliacoes = 0; nf->vivo = true;
            if (qualCabe) nf->pqv = (unsigned)cn.qualidade;
            if (cn.preset != 0) for (auto& pr : nf->presets) pr = cn.preset;   /* Q1: o estado para o app mostra o preset em uso */
        }
        if (avisoApp[0]) { avisoApp[0] = 0; }   /* a cobertura nova apaga o aviso; se falhar, ele volta */
        ++estadoVersao;
        L("ngx: controle: feature recriada handle=%u preset=%c qualidade=%s geometria=%ux%u->%ux%u flags=0x%X (cobre o handle %u do jogo)",
          novo->Id, LetraPreset(cn.preset), qualCabe ? NomePerfQuality((unsigned)cn.qualidade) : "jogo", f->w, f->h, f->outW, f->outH, f->flags, f->id);
    }
    if (rc.aLiberar && rc.liberarEm > 0 && --rc.liberarEm == 0) { oD12Release(rc.aLiberar); rc.aLiberar = nullptr; }
    if (rc.nosso && rc.baseJogo == hJogo)
    {
        /* I15: o render deste Evaluate cabe na cobertura? Sem subrect, o render e o Width/Height do
           Create por contrato; com subrect maior que o criado, o driver recusaria: este quadro vai
           na feature do jogo e a cobertura e refeita no proximo Create. */
        unsigned rw = 0, rh = 0;
        const FeatureRec* nf = Achar(rc.nosso);
        if (nf != nullptr && LerUI(p, kRenderW, &rw) && LerUI(p, kRenderH, &rh) && rw && rh && (rw > nf->w || rh > nf->h))
        {
            char m[120]; snprintf(m, sizeof m, "Evaluate com render %ux%u maior que o criado %ux%u", rw, rh, nf->w, nf->h);
            SoltarCobertura(m);
            return hJogo;
        }
        return rc.nosso;
    }
    return hJogo;
}

/* ================================================================== */
/* Hooks                                                               */
/* ================================================================== */
struct Voo { Voo() { ++emVoo; } ~Voo() { --emVoo; } };
/* C7: excecao na nossa parte de um hook; o hook segue com a chamada original do jogo */
std::atomic<unsigned> excecoes{ 0 };
void Excecao(const char* onde) { if (excecoes++ < 8) L("ngx: excecao no hook %s; seguindo com a chamada original", onde); }

Result HkD12Create(ID3D12GraphicsCommandList* lista, unsigned feature, Parameter* p, Handle** out)
{
    try { if (hooksAtivos.load() && feature == FEATURE_SUPER_SAMPLING) { std::lock_guard<std::mutex> g(mu); AplicarOverride(p, "CreateFeature do jogo"); } }
    catch (...) { Excecao("D3D12_CreateFeature"); }
    Voo v;
    const Result r = oD12Create(lista, feature, p, out);
    try { if (hooksAtivos.load() && out != nullptr) { std::lock_guard<std::mutex> g(mu); SondarCreate(*out, feature, p, true, lista, r); } }
    catch (...) { Excecao("D3D12_CreateFeature"); }
    return r;
}
Result HkD11Create(ID3D11DeviceContext* ctx, unsigned feature, Parameter* p, Handle** out)
{
    Voo v;
    try { if (hooksAtivos.load() && feature == FEATURE_SUPER_SAMPLING) { std::lock_guard<std::mutex> g(mu); AplicarOverride(p, "CreateFeature do jogo (D3D11)"); } }
    catch (...) { Excecao("D3D11_CreateFeature"); }
    const Result r = oD11Create(ctx, feature, p, out);
    try { if (hooksAtivos.load() && out != nullptr) { std::lock_guard<std::mutex> g(mu); SondarCreate(*out, feature, p, false, ctx, r); } }
    catch (...) { Excecao("D3D11_CreateFeature"); }
    return r;
}
Result HkD12Evaluate(ID3D12GraphicsCommandList* lista, const Handle* h, const Parameter* p, ProgressCallback cb)
{
    Voo v;
    /* entrega 3: se o controle mudou o preset/qualidade e ha feature viva, recria e cobre o handle */
    const Handle* hUsar = h;
    bool trocou = false;
    try   /* C7: excecao antes da original: segue com o que ja estiver valido */
    {
        if (hooksAtivos.load() && cn.ligado) { std::lock_guard<std::mutex> g(mu); hUsar = TalvezRecriar(lista, h, p); }
        /* entrega 2: copia/espera/troca ANTES da original; restaura DEPOIS. Falha = original intocada. */
        trocou = hooksAtivos.load() && Up::AntesD12(lista, hUsar, p);
    }
    catch (...) { Excecao("D3D12_EvaluateFeature (antes)"); }
    const int64_t a = Agora();
    Result r = oD12Evaluate(lista, hUsar, p, cb);
    /* I15: qualquer erro do Evaluate na COBERTURA conta como falha dela: na primeira, a cobertura e
       solta (Release em 3 quadros) e este mesmo quadro e refeito na feature do proprio jogo -- o
       driver recusa antes de gravar, o quadro nao se perde e o DLSS do jogo segue. Nunca se insiste. */
    if (hUsar != h && Falhou(r))
    {
        try
        {
            std::lock_guard<std::mutex> g(mu);
            char m[96]; snprintf(m, sizeof m, "Evaluate devolveu 0x%08X (%s)", r, NomeResultado(r));
            SoltarCobertura(m);
        }
        catch (...) { Excecao("D3D12_EvaluateFeature (cobertura)"); }
        hUsar = h;
        r = oD12Evaluate(lista, h, p, cb);
        if (Falhou(r)) { static bool avisou = false; if (!avisou) { avisou = true; L("ngx: a feature do proprio jogo (handle %u) tambem devolveu 0x%08X (%s): nao e da cobertura", h ? h->Id : 0u, r, NomeResultado(r)); } }
    }
    const double ms = Ms(a, Agora());
    try
    {
    if (trocou) Up::DepoisD12(p);
    if (hooksAtivos.load())
    {
        std::lock_guard<std::mutex> g(mu);
        if (E.dev12 == nullptr && lista) { ID3D12Device* d = nullptr; if (SUCCEEDED(lista->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void**>(&d))) && d) { E.dev12 = d; d->Release(); } }
        unsigned tipo = lista ? (unsigned)lista->GetType() : 99u;
        SondarEvaluate(hUsar, p, true, tipo, false, ms, r);
        ListaEval& le = listasEval[listasEvalIdx++ & 3];
        le.lista = lista; le.nAval = totalAvaliacoes; le.eclNoRegistro = eclTotal.load(); le.t = Agora();
    }
    }
    catch (...) { Excecao("D3D12_EvaluateFeature (depois)"); }
    return r;
}
Result HkD11Evaluate(ID3D11DeviceContext* ctx, const Handle* h, const Parameter* p, ProgressCallback cb)
{
    Voo v;
    bool trocou = false;
    try { trocou = hooksAtivos.load() && Up::AntesD11(ctx, h, p); }
    catch (...) { Excecao("D3D11_EvaluateFeature (antes)"); }   /* C7 */
    const int64_t a = Agora();
    const Result r = oD11Evaluate(ctx, h, p, cb);
    const double ms = Ms(a, Agora());
    try
    {
    if (trocou) Up::DepoisD11(p);
    if (hooksAtivos.load())
    {
        std::lock_guard<std::mutex> g(mu);
        if (E.dev11 == nullptr && ctx) { ID3D11Device* d = nullptr; ctx->GetDevice(&d); if (d) { E.dev11 = d; d->Release(); } }
        const bool imediato = ctx ? (ctx->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) : false;
        SondarEvaluate(h, p, false, 0, imediato, ms, r);
    }
    }
    catch (...) { Excecao("D3D11_EvaluateFeature (depois)"); }
    return r;
}
Result HkD12Release(Handle* h)
{
    Voo v;
    try
    {
        std::lock_guard<std::mutex> g(mu);
        if (FeatureRec* f = Achar(h)) { L("ngx: release feature=%u (%s) handle=%u apos %u avaliacoes", f->feature, NomeFeature(f->feature), f->id, f->avaliacoes); f->vivo = false; }
        Up::FeatureSolta(h);   /* X-FG-6: se era a feature servida pelo upstream, a sessao pode passar o pipe ao FSR/XeSS */
        /* I15: o jogo soltou a feature que cobriamos: a cobertura vai junto (3 quadros depois, como
           sempre), em vez de esperar uma cobertura nova que talvez nao venha (nao cabe, desligada) */
        if (rc.nosso != nullptr && rc.baseJogo == h && oD12Release != nullptr)
        {
            if (rc.aLiberar) oD12Release(rc.aLiberar);
            if (FeatureRec* nf = Achar(rc.nosso)) nf->vivo = false;
            L("ngx: controle: o jogo soltou o handle %u coberto; a cobertura (handle %u) e liberada em 3 quadros", h ? h->Id : 0u, rc.nosso->Id);
            Up::FeatureSolta(rc.nosso);   /* X-FG-6: o upstream servia a cobertura, nao o handle do jogo */
            rc.aLiberar = rc.nosso; rc.liberarEm = 3; rc.nosso = nullptr; rc.baseJogo = nullptr;
            ++estadoVersao;
        }
    }
    catch (...) { Excecao("ReleaseFeature"); }   /* C7 */
    return oD12Release(h);
}
Result HkD11Release(Handle* h)
{
    Voo v;
    try { std::lock_guard<std::mutex> g(mu); if (FeatureRec* f = Achar(h)) { L("ngx: release feature=%u (%s) handle=%u apos %u avaliacoes", f->feature, NomeFeature(f->feature), f->id, f->avaliacoes); f->vivo = false; } Up::FeatureSolta(h); }
    catch (...) { Excecao("ReleaseFeature"); }   /* C7 */
    return oD11Release(h);
}

/* Init*: so logamos appId/caminho/device (os argumentos alem do 3o sao repassados as cegas). */
Result HkD12Init(unsigned long long appId, const wchar_t* dados, void* dev, void* a4, void* a5, void* a6, void* a7)
{ Voo v; const Result r = oD12Init(appId, dados, dev, a4, a5, a6, a7); L("ngx: D3D12_Init appid=0x%llX dados=%ls device=0x%p -> %s", appId, dados ? dados : L"", dev, NomeResultado(r)); return r; }
Result HkD12InitExt(unsigned long long appId, const wchar_t* dados, void* dev, void* a4, void* a5, void* a6, void* a7)
{ Voo v; const Result r = oD12InitExt(appId, dados, dev, a4, a5, a6, a7); L("ngx: D3D12_Init_Ext appid=0x%llX dados=%ls device=0x%p -> %s", appId, dados ? dados : L"", dev, NomeResultado(r)); return r; }
Result HkD12InitProj(const char* proj, unsigned eng, const char* ver, const wchar_t* dados, void* dev, void* a6, void* a7, void* a8)
{ Voo v; const Result r = oD12InitProj(proj, eng, ver, dados, dev, a6, a7, a8); L("ngx: D3D12_Init_ProjectID projeto=%s engine=%u versao=%s device=0x%p -> %s", proj ? proj : "", eng, ver ? ver : "", dev, NomeResultado(r)); return r; }
Result HkD11Init(unsigned long long appId, const wchar_t* dados, void* dev, void* a4, void* a5, void* a6, void* a7)
{ Voo v; const Result r = oD11Init(appId, dados, dev, a4, a5, a6, a7); L("ngx: D3D11_Init appid=0x%llX dados=%ls device=0x%p -> %s", appId, dados ? dados : L"", dev, NomeResultado(r)); return r; }
Result HkD11InitExt(unsigned long long appId, const wchar_t* dados, void* dev, void* a4, void* a5, void* a6, void* a7)
{ Voo v; const Result r = oD11InitExt(appId, dados, dev, a4, a5, a6, a7); L("ngx: D3D11_Init_Ext appid=0x%llX dados=%ls device=0x%p -> %s", appId, dados ? dados : L"", dev, NomeResultado(r)); return r; }
Result HkD11InitProj(const char* proj, unsigned eng, const char* ver, const wchar_t* dados, void* dev, void* a6, void* a7, void* a8)
{ Voo v; const Result r = oD11InitProj(proj, eng, ver, dados, dev, a6, a7, a8); L("ngx: D3D11_Init_ProjectID projeto=%s engine=%u versao=%s device=0x%p -> %s", proj ? proj : "", eng, ver ? ver : "", dev, NomeResultado(r)); return r; }

/* O callback de optimal settings: o jogo o tira do objeto de parametros e chama
   direto. Trocamos pelo nosso (que chama o original) para ver o que o jogo pede
   e o que recebe -- e e aqui que a entrega 3 muda a resolucao de render. */
struct OptVisto { unsigned pqv, w, h; };
OptVisto optVistos[8]; int optN = 0;

Result HkOptimal(Parameter* p)
{
    Voo v;
    if (oOptimal == nullptr) return kFalha | 7;
    const Result r = oOptimal(p);
    if (!hooksAtivos.load()) return r;
    try   /* C7: so leitura e log depois da original */
    {
    unsigned pqv = 0, w = 0, h = 0, ow = 0, oh = 0, maxW = 0, maxH = 0, minW = 0, minH = 0; float sharp = 0;
    LerUI(p, kPerfQuality, &pqv); LerUI(p, kWidth, &w); LerUI(p, kHeight, &h);
    LerUI(p, kOutWidth, &ow); LerUI(p, kOutHeight, &oh);
    LerUI(p, kDynMaxW, &maxW); LerUI(p, kDynMaxH, &maxH); LerUI(p, kDynMinW, &minW); LerUI(p, kDynMinH, &minH);
    LerF(p, kSharpness, &sharp);
    std::lock_guard<std::mutex> g(mu);
    bool novo = true;
    for (int i = 0; i < optN; ++i) if (optVistos[i].pqv == pqv && optVistos[i].w == w && optVistos[i].h == h) novo = false;
    if (novo && optN < 8) { optVistos[optN++] = { pqv, w, h }; }
    if (novo)
        L("ngx: optimal settings pqv=%s alvo=%ux%u -> render=%ux%u (dinamico min %ux%u max %ux%u) sharp=%.2f -> %s",
          NomePerfQuality(pqv), w, h, ow, oh, minW, minH, maxW, maxH, sharp, NomeResultado(r));
    }
    catch (...) { Excecao("DLSSOptimalSettingsCallback"); }
    return r;
}

void EnganchaOptimal(Parameter* p, const char* de)
{
    if (p == nullptr) return;
    void* cb = nullptr;
    if (!LerVoid(p, kOptimalCallback, &cb) || cb == nullptr) return;
    if (cb == (void*)&HkOptimal) return;
    std::lock_guard<std::mutex> g(mu);
    if (oOptimal == nullptr) { oOptimal = (FnOptimalSettings)cb; oOptimalDe = p; L("ngx: %s: DLSSOptimalSettingsCallback=0x%p enganchado (params=0x%p)", de, cb, (void*)p); }
    else if ((void*)oOptimal != cb) { L("ngx: %s: outro DLSSOptimalSettingsCallback (0x%p != 0x%p); nao enganchado", de, cb, (void*)oOptimal); return; }
    PorVoid(p, kOptimalCallback, (void*)&HkOptimal);
}
Result HkD12GetParams(Parameter** out)   { Voo v; const Result r = oD12GetParams(out);   if (!Falhou(r) && out) try { EnganchaOptimal(*out, "D3D12_GetParameters"); } catch (...) { Excecao("D3D12_GetParameters"); } return r; }
Result HkD12AllocParams(Parameter** out) { Voo v; const Result r = oD12AllocParams(out); if (!Falhou(r) && out) try { EnganchaOptimal(*out, "D3D12_AllocateParameters"); } catch (...) { Excecao("D3D12_AllocateParameters"); } return r; }
Result HkD12CapParams(Parameter** out)   { Voo v; const Result r = oD12CapParams(out);   if (!Falhou(r) && out) try { EnganchaOptimal(*out, "D3D12_GetCapabilityParameters"); } catch (...) { Excecao("D3D12_GetCapabilityParameters"); } return r; }
Result HkD11GetParams(Parameter** out)   { Voo v; const Result r = oD11GetParams(out);   if (!Falhou(r) && out) try { EnganchaOptimal(*out, "D3D11_GetParameters"); } catch (...) { Excecao("D3D11_GetParameters"); } return r; }
Result HkD11AllocParams(Parameter** out) { Voo v; const Result r = oD11AllocParams(out); if (!Falhou(r) && out) try { EnganchaOptimal(*out, "D3D11_AllocateParameters"); } catch (...) { Excecao("D3D11_AllocateParameters"); } return r; }
Result HkD11CapParams(Parameter** out)   { Voo v; const Result r = oD11CapParams(out);   if (!Falhou(r) && out) try { EnganchaOptimal(*out, "D3D11_GetCapabilityParameters"); } catch (...) { Excecao("D3D11_GetCapabilityParameters"); } return r; }

/* ================================================================== */
/* Instalacao                                                          */
/* ================================================================== */
struct Gancho { const char* nome; void* detour; void** orig; bool obrigatorio; bool ok; };

bool Enganchar(Gancho& g)
{
    void* alvo = reinterpret_cast<void*>(GetProcAddress(modulo, g.nome));
    if (alvo == nullptr) { if (g.obrigatorio) L("ngx: export %s ausente no _nvngx.dll", g.nome); return false; }
    MH_STATUS s = MH_CreateHook(alvo, g.detour, g.orig);
    if (s != MH_OK) { L("ngx: MH_CreateHook(%s) = %d", g.nome, (int)s); return false; }
    s = MH_EnableHook(alvo);
    if (s != MH_OK) { L("ngx: MH_EnableHook(%s) = %d", g.nome, (int)s); return false; }
    g.ok = true;
    return true;
}

void Instalar()
{
    if (!mhIniciado)
    {
        const MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) { L("ngx: MH_Initialize = %d; sonda desligada", (int)s); parado = true; return; }
        mhIniciado = true;
    }
    Gancho g12[] = {
        { "NVSDK_NGX_D3D12_CreateFeature",   (void*)&HkD12Create,   (void**)&oD12Create,   true,  false },
        { "NVSDK_NGX_D3D12_EvaluateFeature", (void*)&HkD12Evaluate, (void**)&oD12Evaluate, true,  false },
        { "NVSDK_NGX_D3D12_ReleaseFeature",  (void*)&HkD12Release,  (void**)&oD12Release,  true,  false },
        { "NVSDK_NGX_D3D12_Init",            (void*)&HkD12Init,     (void**)&oD12Init,     false, false },
        { "NVSDK_NGX_D3D12_Init_Ext",        (void*)&HkD12InitExt,  (void**)&oD12InitExt,  false, false },
        { "NVSDK_NGX_D3D12_Init_ProjectID",  (void*)&HkD12InitProj, (void**)&oD12InitProj, false, false },
        { "NVSDK_NGX_D3D12_GetParameters",   (void*)&HkD12GetParams,   (void**)&oD12GetParams,   false, false },
        { "NVSDK_NGX_D3D12_AllocateParameters", (void*)&HkD12AllocParams, (void**)&oD12AllocParams, false, false },
        { "NVSDK_NGX_D3D12_GetCapabilityParameters", (void*)&HkD12CapParams, (void**)&oD12CapParams, false, false },
    };
    Gancho g11[] = {
        { "NVSDK_NGX_D3D11_CreateFeature",   (void*)&HkD11Create,   (void**)&oD11Create,   true,  false },
        { "NVSDK_NGX_D3D11_EvaluateFeature", (void*)&HkD11Evaluate, (void**)&oD11Evaluate, true,  false },
        { "NVSDK_NGX_D3D11_ReleaseFeature",  (void*)&HkD11Release,  (void**)&oD11Release,  true,  false },
        { "NVSDK_NGX_D3D11_Init",            (void*)&HkD11Init,     (void**)&oD11Init,     false, false },
        { "NVSDK_NGX_D3D11_Init_Ext",        (void*)&HkD11InitExt,  (void**)&oD11InitExt,  false, false },
        { "NVSDK_NGX_D3D11_Init_ProjectID",  (void*)&HkD11InitProj, (void**)&oD11InitProj, false, false },
        { "NVSDK_NGX_D3D11_GetParameters",   (void*)&HkD11GetParams,   (void**)&oD11GetParams,   false, false },
        { "NVSDK_NGX_D3D11_AllocateParameters", (void*)&HkD11AllocParams, (void**)&oD11AllocParams, false, false },
        { "NVSDK_NGX_D3D11_GetCapabilityParameters", (void*)&HkD11CapParams, (void**)&oD11CapParams, false, false },
    };
    int ok12 = 0, ok11 = 0;
    for (auto& g : g12) if (Enganchar(g)) ++ok12;
    for (auto& g : g11) if (Enganchar(g)) ++ok11;
    E.hookD3D12 = g12[0].ok && g12[1].ok && g12[2].ok;
    E.hookD3D11 = g11[0].ok && g11[1].ok && g11[2].ok;
    hooksAtivos = E.hookD3D12 || E.hookD3D11;
    ++estadoVersao;

    wchar_t caminho[MAX_PATH] = L"";
    GetModuleFileNameW(modulo, caminho, MAX_PATH);
    if (E.hookD3D12) L("ngx: hook instalado (_nvngx.dll D3D12) %d/%d exports, modulo=%ls base=0x%p", ok12, (int)(sizeof g12 / sizeof g12[0]), caminho, (void*)modulo);
    if (E.hookD3D11) L("ngx: hook instalado (_nvngx.dll D3D11) %d/%d exports", ok11, (int)(sizeof g11 / sizeof g11[0]));
    if (!hooksAtivos.load()) { L("ngx: desligado (nenhum export obrigatorio enganchado)"); parado = true; }

    /* quem mais esta no processo (so informacao) */
    HMODULE snip = GetModuleHandleW(L"nvngx_dlss.dll");
    HMODULE sl = GetModuleHandleW(L"sl.interposer.dll");
    HMODULE slc = GetModuleHandleW(L"sl.common.dll");
    HMODULE fake = GetModuleHandleW(L"nvngx.dll");
    L("ngx: modulos: nvngx_dlss.dll=%s sl.interposer.dll=%s sl.common.dll=%s nvngx.dll(terceiro)=%s",
      snip ? "sim" : "nao", sl ? "sim" : "nao", slc ? "sim" : "nao", fake ? "SIM (outro hook NGX no processo)" : "nao");

    /* Se o jogo ja tinha o objeto de parametros antes do hook, o callback de optimal
       settings dele nao passa por nos: pedimos um objeto ao driver so para ler o
       ponteiro do callback e conseguir compara-lo (nao usamos esse objeto). */
    if (oD12GetParams != nullptr)
    {
        Parameter* p = nullptr;
        if (!Falhou(oD12GetParams(&p)) && p != nullptr) EnganchaOptimal(p, "GetParameters (sonda)");
    }
}

} // namespace anonimo

/* ================================================================== */
/* Interface                                                           */
/* ================================================================== */
void Iniciar(LogFn log)
{
    L = log;
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); qpcFreq = f.QuadPart > 0 ? f.QuadPart : 1;
    Up::Iniciar(log);
    Servico();
}

void Servico()
{
    if (parado || L == nullptr) return;
    if (modulo == nullptr)
    {
        modulo = GetModuleHandleW(L"_nvngx.dll");
        if (modulo == nullptr)
        {
            if (!avisouSemModulo) { avisouSemModulo = true; L("ngx: _nvngx.dll ainda nao esta no processo; vou continuar procurando (o jogo carrega ao ligar o DLSS)"); }
            EscreverEstadoNgx();
            return;
        }
        Instalar();
    }
    if (hooksAtivos.load()) { LerControleNgx(); Up::Servico(); }
    EscreverEstadoNgx();   /* Q1: tambem com ngx=0, para o card do app saber que nao ha NGX aqui */
}

void AoExecutar(ID3D12CommandQueue* fila, UINT n, ID3D12CommandList* const* listas)
{
    const unsigned total = ++eclTotal;
    if (!hooksAtivos.load() || listas == nullptr) return;
    /* a lista do evaluate esta nesta submissao? */
    for (UINT i = 0; i < n; ++i)
    {
        for (auto& le : listasEval)
        {
            if (le.lista == nullptr || le.lista != listas[i]) continue;
            std::lock_guard<std::mutex> g(mu);
            if (le.lista != listas[i]) continue;
            Up::FilaVista(fila);   /* e nesta fila que o upstream faz Signal/Wait */
            if (cicloLogados < kMaxCiclo)
            {
                D3D12_COMMAND_QUEUE_DESC qd = fila ? fila->GetDesc() : D3D12_COMMAND_QUEUE_DESC{};
                L("ngx: ciclo: a lista do evaluate #%u (0x%p) executou na fila 0x%p tipo=%s, %.2f ms apos o registro; submissoes desde o Present anterior ate o registro=%u, entre o registro e esta submissao=%u, listas nesta submissao=%u (posicao %u)",
                  le.nAval, (void*)le.lista, (void*)fila, qd.Type == D3D12_COMMAND_LIST_TYPE_DIRECT ? "DIRECT" : qd.Type == D3D12_COMMAND_LIST_TYPE_COMPUTE ? "COMPUTE" : "outro",
                  Ms(le.t, Agora()), le.eclNoRegistro - eclNoPresent.load(), total - 1 - le.eclNoRegistro, n, i);
                ++cicloLogados;
                if (cicloLogados == kMaxCiclo) L("ngx: ciclo: %d amostras logadas; parando", kMaxCiclo);
            }
            le.lista = nullptr;
            return;
        }
    }
}

void AoApresentar()
{
    ++presentes;
    eclNoPresent = eclTotal.load();
}

void Parar()
{
    if (parado && !hooksAtivos.load()) return;
    parado = true;
    { wchar_t c[MAX_PATH]; CaminhoEstadoNgx(c, MAX_PATH); DeleteFileW(c); }   /* Q1: o app nao deve ler um estado de sessao morta */
    Up::Parar();   /* primeiro para de trocar; depois os hooks saem */
    /* entrega 3: libera as features que criamos (ainda com os hooks ativos, para o Release passar pela original) */
    if (hooksAtivos.load() && oD12Release) { std::lock_guard<std::mutex> g(mu); if (rc.aLiberar) { oD12Release(rc.aLiberar); rc.aLiberar = nullptr; } if (rc.nosso) { oD12Release(rc.nosso); rc.nosso = nullptr; } }
    if (hooksAtivos.exchange(false))
    {
        MH_DisableHook(MH_ALL_HOOKS);
        /* espera quem ainda estiver dentro de um hook nosso sair (cada chamada e curta) */
        for (int i = 0; i < 50 && emVoo.load() > 0; ++i) Sleep(10);
        if (L) L("ngx: desligado (ponte encerrando) apos %u avaliacoes; %d chamadas em voo", totalAvaliacoes, emVoo.load());
        /* NAO chamamos MH_Uninitialize: os trampolins ficam alocados (alguns KB) para que
           uma thread que ainda esteja passando por um deles nao caia. E o preco de nunca
           derrubar o jogo. */
    }
}

Estado Ler() { std::lock_guard<std::mutex> g(mu); return E; }

} // namespace Ngx
