/* =====================================================================
   ngx_contrato.h -- o contrato NGX (DLSS) visto de dentro do jogo, escrito
   do zero a partir dos nomes publicos do SDK NGX da NVIDIA (as chaves de
   texto e os enums sao o contrato; o codigo e nosso).

   Fase 3 do Modo Integrado (CONTRATO-FASE3.md, 15/09/2026). Agente G1.

   O que o jogo faz: linka a lib estatica do SDK, que carrega o _nvngx.dll
   do driver (registro HKLM\SOFTWARE\NVIDIA Corporation\Global\NGXCore,
   FullPath) e chama os exports NVSDK_NGX_D3D12_* / D3D11_* dele. O snippet
   nvngx_dlss.dll (ao lado do jogo ou no cache OTA do driver) e carregado por
   baixo, pelo _nvngx.dll. Streamline (sl.dlss.dll/sl.common.dll) tambem
   acaba no _nvngx.dll. Logo: enganchar os exports do _nvngx.dll pega tudo.

   O objeto de parametros (NVSDK_NGX_Parameter) e uma classe C++ com vtable,
   compilada pelo MSVC. Fato que importa e que custou ao feeder-amd descobrir:
   o MSVC poe as SOBRECARGAS de um mesmo nome na vtable em ORDEM INVERSA de
   declaracao (agrupadas na primeira aparicao do nome). A ordem de declaracao
   no header da NVIDIA e Set(unsigned long long), Set(float), Set(double),
   Set(unsigned), Set(int), Set(ID3D11Resource*), Set(ID3D12Resource*),
   Set(void*), idem Get, depois Reset(). Na vtable real fica:

     0 Set(void*)  1 Set(ID3D12Resource*)  2 Set(ID3D11Resource*)  3 Set(int)
     4 Set(uint)  5 Set(double)  6 Set(float)  7 Set(ull)
     8 Get(void**) 9 Get(ID3D12Resource**) 10 Get(ID3D11Resource**) 11 Get(int*)
     12 Get(uint*) 13 Get(double*) 14 Get(float*) 15 Get(ull*)
     16 Reset()

   Conferido na sonda (15/09/2026, host de referencia do Feeder, _nvngx.dll
   32.0.15.9649): com a ordem trocada, Get(uint*) de "Width" passava (o slot
   errado era Get(int*), que converte) e "Color"/"MV.Scale.X" falhavam; com
   esta ordem, tudo le.

   Aqui a vtable e descrita como uma tabela de ponteiros de funcao livres com
   o `this` no primeiro argumento: no x64 do Windows ha UMA convencao de
   chamada, e um metodo (this em RCX) e identico a uma funcao livre com o
   objeto no primeiro parametro. Nao dependemos do layout de classe do g++.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>

namespace Ngx {

/* ------------------------------------------------------------------ */
/* Resultados                                                          */
/* ------------------------------------------------------------------ */
typedef unsigned int Result;
static const Result kOk        = 0x00000001u;
static const Result kFalha     = 0xBAD00000u;   /* base: os erros sao kFalha | n */
static inline bool  Falhou(Result r) { return (r & 0xFFF00000u) == kFalha; }

static inline const char* NomeResultado(Result r)
{
    if (!Falhou(r)) return "ok";
    switch (r & 0xFF)
    {
    case 1:  return "FeatureNotSupported";
    case 2:  return "PlatformError";
    case 3:  return "FeatureAlreadyExists";
    case 4:  return "FeatureNotFound";
    case 5:  return "InvalidParameter";
    case 6:  return "ScratchBufferTooSmall";
    case 7:  return "NotInitialized";
    case 8:  return "UnsupportedInputFormat";
    case 9:  return "RWFlagMissing";
    case 10: return "MissingInput";
    case 11: return "UnableToInitializeFeature";
    case 12: return "OutOfDate";
    case 13: return "OutOfGPUMemory";
    case 14: return "UnsupportedFormat";
    case 15: return "UnableToWriteToAppDataPath";
    case 16: return "UnsupportedParameter";
    case 17: return "Denied";
    case 18: return "NotImplemented";
    default: return "Fail";
    }
}

/* ------------------------------------------------------------------ */
/* Enums do contrato                                                   */
/* ------------------------------------------------------------------ */
enum Feature
{
    FEATURE_RESERVADO      = 0,
    FEATURE_SUPER_SAMPLING = 1,   /* DLSS SR (e DLAA) */
    FEATURE_INPAINTING     = 2,
    FEATURE_IMAGE_SR       = 3,
    FEATURE_SLOW_MOTION    = 4,
    FEATURE_VIDEO_SR       = 5,
    FEATURE_RESERVADO_6    = 6,
    FEATURE_RESERVADO_7    = 7,
    FEATURE_RESERVADO_8    = 8,
    FEATURE_IMAGE_SIGNAL   = 9,
    FEATURE_FRAME_GEN      = 10,  /* DLSS-G */
    FEATURE_DEEP_RESOLVE   = 11,
    FEATURE_RAY_RECONSTRUCTION = 12   /* DLSS-D */
};
static inline const char* NomeFeature(unsigned f)
{
    switch (f)
    {
    case FEATURE_SUPER_SAMPLING: return "DLSS-SR";
    case FEATURE_FRAME_GEN:      return "DLSS-G";
    case FEATURE_RAY_RECONSTRUCTION: return "DLSS-D";
    case FEATURE_IMAGE_SR:       return "ImageSR";
    default: return "outra";
    }
}

enum PerfQuality   /* NVSDK_NGX_PerfQuality_Value */
{
    PQ_MAX_PERF = 0, PQ_BALANCED = 1, PQ_MAX_QUALITY = 2,
    PQ_ULTRA_PERF = 3, PQ_ULTRA_QUALITY = 4, PQ_DLAA = 5
};
static inline const char* NomePerfQuality(unsigned v)
{
    switch (v)
    {
    case PQ_MAX_PERF: return "performance";
    case PQ_BALANCED: return "balanced";
    case PQ_MAX_QUALITY: return "quality";
    case PQ_ULTRA_PERF: return "ultra-performance";
    case PQ_ULTRA_QUALITY: return "ultra-quality";
    case PQ_DLAA: return "dlaa";
    default: return "?";
    }
}

/* DLSS.Feature.Create.Flags */
enum CreateFlags
{
    CF_HDR = 1 << 0, CF_MV_LOW_RES = 1 << 1, CF_MV_JITTERED = 1 << 2,
    CF_DEPTH_INVERTED = 1 << 3, CF_SHARPENING = 1 << 5, CF_AUTO_EXPOSURE = 1 << 6,
    CF_ALPHA_UPSCALING = 1 << 7
};

/* DLSS.Hint.Render.Preset.* : 0 = default, 10 = J, 11 = K, 12 = L, 13 = M (1..9 antigos/reservados).
   Visto na sonda (host do Feeder): o valor chega com o bit 31 ligado (0x8000000B = K); a letra
   ignora esse bit e o log mostra o valor bruto ao lado. */
static inline char LetraPreset(unsigned p) { p &= 0x7FFFFFFFu; return p == 0 ? '-' : (p >= 1 && p <= 15 ? (char)('A' + p - 1) : '?'); }

/* ------------------------------------------------------------------ */
/* Chaves de texto do objeto de parametros                             */
/* ------------------------------------------------------------------ */
static const char* const kWidth       = "Width";
static const char* const kHeight      = "Height";
static const char* const kOutWidth    = "OutWidth";
static const char* const kOutHeight   = "OutHeight";
static const char* const kPerfQuality = "PerfQualityValue";
static const char* const kCreateFlags = "DLSS.Feature.Create.Flags";
static const char* const kEnableOutputSubrects = "DLSS.Enable.Output.Subrects";
static const char* const kColor       = "Color";
static const char* const kOutput      = "Output";
static const char* const kDepth       = "Depth";
static const char* const kMotionVectors = "MotionVectors";
static const char* const kExposureTexture = "ExposureTexture";
static const char* const kTransparencyMask = "TransparencyMask";
static const char* const kBiasMask    = "DLSS.Input.Bias.Current.Color.Mask";
static const char* const kJitterX     = "Jitter.Offset.X";
static const char* const kJitterY     = "Jitter.Offset.Y";
static const char* const kMvScaleX    = "MV.Scale.X";
static const char* const kMvScaleY    = "MV.Scale.Y";
static const char* const kReset       = "Reset";
static const char* const kSharpness   = "Sharpness";
static const char* const kFrameTimeDelta = "FrameTimeDeltaInMsec";
static const char* const kPreExposure = "DLSS.Pre.Exposure";
static const char* const kExposureScale = "DLSS.Exposure.Scale";
static const char* const kRenderW     = "DLSS.Render.Subrect.Dimensions.Width";
static const char* const kRenderH     = "DLSS.Render.Subrect.Dimensions.Height";
static const char* const kColorBaseX  = "DLSS.Input.Color.Subrect.Base.X";
static const char* const kColorBaseY  = "DLSS.Input.Color.Subrect.Base.Y";
static const char* const kDepthBaseX  = "DLSS.Input.Depth.Subrect.Base.X";
static const char* const kDepthBaseY  = "DLSS.Input.Depth.Subrect.Base.Y";
static const char* const kMvBaseX     = "DLSS.Input.MV.Subrect.Base.X";
static const char* const kMvBaseY     = "DLSS.Input.MV.Subrect.Base.Y";
static const char* const kOutBaseX    = "DLSS.Output.Subrect.Base.X";
static const char* const kOutBaseY    = "DLSS.Output.Subrect.Base.Y";
static const char* const kPresetDLAA  = "DLSS.Hint.Render.Preset.DLAA";
static const char* const kPresetQuality = "DLSS.Hint.Render.Preset.Quality";
static const char* const kPresetBalanced = "DLSS.Hint.Render.Preset.Balanced";
static const char* const kPresetPerformance = "DLSS.Hint.Render.Preset.Performance";
static const char* const kPresetUltraPerf = "DLSS.Hint.Render.Preset.UltraPerformance";
static const char* const kPresetUltraQuality = "DLSS.Hint.Render.Preset.UltraQuality";
static const char* const kOptimalCallback = "DLSSOptimalSettingsCallback";
static const char* const kDynMaxW     = "DLSS.Get.Dynamic.Max.Render.Width";
static const char* const kDynMaxH     = "DLSS.Get.Dynamic.Max.Render.Height";
static const char* const kDynMinW     = "DLSS.Get.Dynamic.Min.Render.Width";
static const char* const kDynMinH     = "DLSS.Get.Dynamic.Min.Render.Height";

/* ------------------------------------------------------------------ */
/* O objeto de parametros: vtable na ordem do MSVC (ver cabecalho)     */
/* ------------------------------------------------------------------ */
struct Parameter;   /* opaco: so acessamos pela vtable */

struct ParameterVt
{
    void   (*SetVoid)(Parameter*, const char*, void*);
    void   (*SetD3D12)(Parameter*, const char*, ID3D12Resource*);
    void   (*SetD3D11)(Parameter*, const char*, ID3D11Resource*);
    void   (*SetI)  (Parameter*, const char*, int);
    void   (*SetUI) (Parameter*, const char*, unsigned int);
    void   (*SetD)  (Parameter*, const char*, double);
    void   (*SetF)  (Parameter*, const char*, float);
    void   (*SetULL)(Parameter*, const char*, unsigned long long);
    Result (*GetVoid)(const Parameter*, const char*, void**);
    Result (*GetD3D12)(const Parameter*, const char*, ID3D12Resource**);
    Result (*GetD3D11)(const Parameter*, const char*, ID3D11Resource**);
    Result (*GetI)  (const Parameter*, const char*, int*);
    Result (*GetUI) (const Parameter*, const char*, unsigned int*);
    Result (*GetD)  (const Parameter*, const char*, double*);
    Result (*GetF)  (const Parameter*, const char*, float*);
    Result (*GetULL)(const Parameter*, const char*, unsigned long long*);
    void   (*Reset)(Parameter*);
};
static inline const ParameterVt* Vt(const Parameter* p) { return *reinterpret_cast<const ParameterVt* const*>(p); }

/* Leituras tolerantes: false quando a chave nao existe (o Get devolve falha
   sem tocar no destino). Nada aqui lanca. */
static inline bool LerUI(const Parameter* p, const char* k, unsigned* v)
{ if (p == nullptr) return false; return !Falhou(Vt(p)->GetUI(p, k, v)); }
static inline bool LerI(const Parameter* p, const char* k, int* v)
{ if (p == nullptr) return false; return !Falhou(Vt(p)->GetI(p, k, v)); }
static inline bool LerF(const Parameter* p, const char* k, float* v)
{ if (p == nullptr) return false; return !Falhou(Vt(p)->GetF(p, k, v)); }
static inline bool LerD3D12(const Parameter* p, const char* k, ID3D12Resource** v)
{ if (p == nullptr) return false; *v = nullptr; return !Falhou(Vt(p)->GetD3D12(p, k, v)) && *v != nullptr; }
static inline bool LerD3D11(const Parameter* p, const char* k, ID3D11Resource** v)
{ if (p == nullptr) return false; *v = nullptr; return !Falhou(Vt(p)->GetD3D11(p, k, v)) && *v != nullptr; }
static inline bool LerVoid(const Parameter* p, const char* k, void** v)
{ if (p == nullptr) return false; *v = nullptr; return !Falhou(Vt(p)->GetVoid(p, k, v)); }

static inline void PorUI(Parameter* p, const char* k, unsigned v)      { if (p) Vt(p)->SetUI(p, k, v); }
static inline void PorI(Parameter* p, const char* k, int v)            { if (p) Vt(p)->SetI(p, k, v); }
static inline void PorF(Parameter* p, const char* k, float v)          { if (p) Vt(p)->SetF(p, k, v); }
static inline void PorD3D12(Parameter* p, const char* k, ID3D12Resource* v) { if (p) Vt(p)->SetD3D12(p, k, v); }
static inline void PorD3D11(Parameter* p, const char* k, ID3D11Resource* v) { if (p) Vt(p)->SetD3D11(p, k, v); }
static inline void PorVoid(Parameter* p, const char* k, void* v)       { if (p) Vt(p)->SetVoid(p, k, v); }

/* ------------------------------------------------------------------ */
/* Handle de feature e os exports que enganchamos                      */
/* ------------------------------------------------------------------ */
struct Handle { unsigned int Id; };

typedef void (*ProgressCallback)(float, bool*);

/* Os exports do _nvngx.dll (NVSDK_CONV = __cdecl; no x64 e a convencao unica).
   Os Init* sao declarados com MAIS argumentos do que qualquer variante real
   tem, e todos sao repassados: assim o hook nunca perde um registrador ou uma
   posicao de pilha que a variante real use, e nos so lemos os tres primeiros. */
typedef Result (*FnD3D12CreateFeature)(ID3D12GraphicsCommandList*, unsigned feature, Parameter*, Handle**);
typedef Result (*FnD3D12EvaluateFeature)(ID3D12GraphicsCommandList*, const Handle*, const Parameter*, ProgressCallback);
typedef Result (*FnD3D12ReleaseFeature)(Handle*);
typedef Result (*FnD3D11CreateFeature)(ID3D11DeviceContext*, unsigned feature, Parameter*, Handle**);
typedef Result (*FnD3D11EvaluateFeature)(ID3D11DeviceContext*, const Handle*, const Parameter*, ProgressCallback);
typedef Result (*FnD3D11ReleaseFeature)(Handle*);
typedef Result (*FnGetParameters)(Parameter**);
typedef Result (*FnInit)(unsigned long long appId, const wchar_t* dados, void* device, void* a4, void* a5, void* a6, void* a7);
typedef Result (*FnInitProjectID)(const char* projeto, unsigned engine, const char* versao, const wchar_t* dados, void* device, void* a6, void* a7, void* a8);

/* O callback de "optimal settings" que o jogo tira do objeto de parametros
   (chave DLSSOptimalSettingsCallback) e chama para saber em que resolucao
   renderizar para um dado PerfQualityValue. Assinatura: Result f(Parameter*). */
typedef Result (*FnOptimalSettings)(Parameter*);

} // namespace Ngx
