/* =====================================================================
   sr_contrato.h -- os contratos dos upscalers de TERCEIROS vistos de
   dentro do jogo (degrau 2, Q2, 16/09/2026): FSR 3.1 (ffx_api), FSR 2.x
   (ffx_fsr2_api_x64.dll) e XeSS (libxess.dll).

   Escrito do zero a partir dos nomes publicos dos SDKs (os nomes dos
   exports, os enums e a ORDEM dos campos sao o contrato; o codigo e nosso).
   Nada do OptiScaler (GPL) entra aqui: so a lista do que interceptar, que
   esta em pesquisa-modo-integrado\E-dados-do-jogo.md.

   - ffx_api (FSR 3.1+): os cabecalhos MIT do FidelityFX SDK 1.1 ja estao em
     feeder-amd\src-feeder\external\ffx\ffx_api\ (mesmos que o fg_ffx.h do
     app usa). Aqui so se declara o que o cabecalho nao tem: o tipo do
     backend D3D11 (o SDK 1.1.x o define em ffx_api_dx11.h, que nao esta na
     arvore) e as constantes de estado.
   - FSR 2.x: `FfxResource` tem 184 bytes nas versoes 2.0/2.1/2.2. A 2.2 poe
     um `usage` a mais em `FfxResourceDescription`, o que empurra `state` e
     `isDepth` 4 bytes para a frente SEM mudar o tamanho total (o padding
     antes de `descriptorData` absorve). Logo `FfxFsr2DispatchDescription`
     tem os MESMOS deslocamentos nas tres versoes para tudo que precisamos,
     menos o `state`, que a DLL le pela versao de arquivo do modulo. O
     ponteiro do recurso (`resource`) e sempre o primeiro campo, e o
     tamanho/formato vem de ID3D12Resource::GetDesc(), nunca do enum FFX.
   - XeSS: `xess_d3d12_execute_params_t` e `xess_d3d12_init_params_t` da
     serie 1.x/2.x (estaveis desde a 1.0).
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <cstdint>
#include <cstddef>

namespace Sr {

/* ------------------------------------------------------------------ */
/* ffx_api (FSR 3.1+) -- o que falta nos cabecalhos do SDK             */
/* ------------------------------------------------------------------ */
static const uint64_t kFfxBackendDx12 = 0x0000002u;   /* FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12 */
static const uint64_t kFfxBackendDx11 = 0x0000003u;   /* FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX11 (ffx_api_dx11.h) */

/* FfxApiResourceState -> D3D12_RESOURCE_STATES. 0/desconhecido -> `padrao`. */
static inline D3D12_RESOURCE_STATES EstadoFfxApi(uint32_t s, D3D12_RESOURCE_STATES padrao)
{
    switch (s)
    {
    case 1u << 0: return D3D12_RESOURCE_STATE_COMMON;
    case 1u << 1: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case 1u << 2: return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case 1u << 3: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case (1u << 2) | (1u << 3): return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case 1u << 4: return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case 1u << 5: return D3D12_RESOURCE_STATE_COPY_DEST;
    case (1u << 4) | (1u << 2): return D3D12_RESOURCE_STATE_GENERIC_READ;
    case 1u << 6: return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case 1u << 7: return D3D12_RESOURCE_STATE_PRESENT;
    case 1u << 8: return D3D12_RESOURCE_STATE_RENDER_TARGET;
    default: return padrao;
    }
}

/* ------------------------------------------------------------------ */
/* FSR 2.x (ffx_fsr2_api_x64.dll)                                      */
/* ------------------------------------------------------------------ */
static const unsigned kFsr2NomeTam = 64;   /* FFX_RESOURCE_NAME_SIZE */

/* FfxResourceDescription da 2.2 (a 2.0/2.1 nao tem `usage`; ver cabecalho). */
struct Fsr2ResourceDesc22 { uint32_t type, format, width, height, depth, mipCount, flags, usage; };

/* FfxResource (2.2). Nas 2.0/2.1 `state` esta 4 bytes antes (no lugar de `usage`). */
struct Fsr2Resource
{
    void*             resource;                 /* ID3D12Resource* no backend DX12 */
    wchar_t           name[kFsr2NomeTam];
    Fsr2ResourceDesc22 description;
    uint32_t          state;                    /* 2.2; na 2.0/2.1 e `description.usage` */
    bool              isDepth;
    uint64_t          descriptorData;
};
static_assert(sizeof(Fsr2Resource) == 184, "FfxResource = 184 bytes (2.0/2.1/2.2)");
static_assert(offsetof(Fsr2Resource, description) == 136, "description @136");
static_assert(offsetof(Fsr2Resource, state) == 168, "state @168 (2.2) / @164 (2.0-2.1)");

struct Fsr2Float2 { float x, y; };
struct Fsr2Dim2   { uint32_t width, height; };

/* FfxFsr2DispatchDescription: os campos que usamos tem o mesmo deslocamento
   nas tres versoes (a 2.2 acrescenta campos DEPOIS de cameraFovAngleVertical). */
struct Fsr2DispatchDesc
{
    void*        commandList;      /* ID3D12CommandList* (DX12) */
    Fsr2Resource color, depth, motionVectors, exposure, reactive, transparencyAndComposition, output;
    Fsr2Float2   jitterOffset;
    Fsr2Float2   motionVectorScale;
    Fsr2Dim2     renderSize;
    bool         enableSharpening;
    float        sharpness;
    float        frameTimeDelta;
    float        preExposure;
    bool         reset;
    float        cameraNear;
    float        cameraFar;
    float        cameraFovAngleVertical;
    /* 2.2: viewSpaceToMetersFactor, enableAutoReactive, colorOpaqueOnly, autoTc*, autoReactive* */
};
static_assert(offsetof(Fsr2DispatchDesc, color) == 8, "color @8");
static_assert(offsetof(Fsr2DispatchDesc, output) == 8 + 6 * 184, "output @1112");
static_assert(offsetof(Fsr2DispatchDesc, jitterOffset) == 8 + 7 * 184, "jitter @1296");
static_assert(offsetof(Fsr2DispatchDesc, sharpness) == 1296 + 8 + 8 + 8 + 4, "sharpness @1324");
static_assert(offsetof(Fsr2DispatchDesc, reset) == 1336, "reset @1336");

/* FfxFsr2ContextDescription: so o primeiro campo nos interessa. */
struct Fsr2ContextDesc { uint32_t flags; Fsr2Dim2 maxRenderSize; Fsr2Dim2 displaySize; /* callbacks, device, fpMessage */ };

/* FfxFsr2InitializationFlagBits (iguais em espirito aos do ffx_api) */
enum Fsr2Flags
{
    FSR2_HDR = 1 << 0, FSR2_MV_DISPLAY_RES = 1 << 1, FSR2_MV_JITTER_CANCEL = 1 << 2,
    FSR2_DEPTH_INVERTED = 1 << 3, FSR2_DEPTH_INFINITE = 1 << 4, FSR2_AUTO_EXPOSURE = 1 << 5,
    FSR2_DYNAMIC_RES = 1 << 6
};

/* FfxResourceStates (2.x) -> D3D12 */
static inline D3D12_RESOURCE_STATES EstadoFsr2(uint32_t s, D3D12_RESOURCE_STATES padrao)
{
    switch (s)
    {
    case 1u << 0: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case 1u << 1: return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case 1u << 2: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case (1u << 1) | (1u << 2): return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case 1u << 3: return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case 1u << 4: return D3D12_RESOURCE_STATE_COPY_DEST;
    case (1u << 3) | (1u << 1): return D3D12_RESOURCE_STATE_GENERIC_READ;
    default: return padrao;
    }
}

typedef uint32_t (*FnFsr2Dispatch)(void* contexto, const Fsr2DispatchDesc* desc);
typedef uint32_t (*FnFsr2Create)(void* contexto, const Fsr2ContextDesc* desc);
typedef uint32_t (*FnFsr2Destroy)(void* contexto);
static const uint32_t kFsr2Ok = 0;   /* FFX_OK */

/* ------------------------------------------------------------------ */
/* XeSS (libxess.dll), serie 1.x/2.x                                   */
/* ------------------------------------------------------------------ */
typedef void* XessContext;            /* xess_context_handle_t */
typedef int32_t XessResult;           /* xess_result_t; XESS_RESULT_SUCCESS = 0 */
struct XessCoord { uint32_t x, y; };  /* xess_coord_t / xess_2d_t */

struct XessD3D12InitParams
{
    XessCoord             outputResolution;
    int32_t               qualitySetting;     /* xess_quality_settings_t */
    uint32_t              initFlags;
    uint32_t              creationNodeMask;
    uint32_t              visibleNodeMask;
    ID3D12Heap*           pTempBufferHeap;
    uint64_t              bufferHeapOffset;
    ID3D12Heap*           pTempTextureHeap;
    uint64_t              textureHeapOffset;
    ID3D12DescriptorHeap* pPipelineLibrary;
};

struct XessD3D12ExecParams
{
    ID3D12Resource* pColorTexture;
    ID3D12Resource* pVelocityTexture;
    ID3D12Resource* pDepthTexture;
    ID3D12Resource* pExposureScaleTexture;
    ID3D12Resource* pResponsivePixelMaskTexture;
    ID3D12Resource* pOutputTexture;
    float           jitterOffsetX;
    float           jitterOffsetY;
    float           exposureScale;
    uint32_t        resetHistory;
    uint32_t        inputWidth;
    uint32_t        inputHeight;
    XessCoord       inputColorBase;
    XessCoord       inputMotionVectorBase;
    XessCoord       inputDepthBase;
    XessCoord       inputResponsiveMaskBase;
    XessCoord       outputColorBase;
    ID3D12DescriptorHeap* pDescriptorHeap;
    uint32_t        descriptorHeapOffset;
};

enum XessInitFlags
{
    XESS_HIGH_RES_MV = 1 << 0, XESS_LDR_INPUT = 1 << 1, XESS_EXPOSURE_SCALE_TEX = 1 << 2,
    XESS_JITTERED_MV = 1 << 3, XESS_INVERTED_DEPTH = 1 << 4, XESS_AUTO_EXPOSURE = 1 << 5,
    XESS_RESPONSIVE_MASK = 1 << 6, XESS_NDC_VELOCITY = 1 << 7
};

typedef XessResult (*FnXessInit)(XessContext, const XessD3D12InitParams*);
typedef XessResult (*FnXessExecute)(XessContext, ID3D12GraphicsCommandList*, const XessD3D12ExecParams*);
typedef XessResult (*FnXessSetScale)(XessContext, float, float);
typedef XessResult (*FnXessDestroy)(XessContext);

/* ------------------------------------------------------------------ */
/* Flags NGX que a DLL propoe ao app (DLSS.Feature.Create.Flags)       */
/* ------------------------------------------------------------------ */
enum NgxFlags
{
    NGX_HDR = 1 << 0, NGX_MV_LOW_RES = 1 << 1, NGX_MV_JITTERED = 1 << 2,
    NGX_DEPTH_INVERTED = 1 << 3, NGX_SHARPENING = 1 << 5, NGX_AUTO_EXPOSURE = 1 << 6
};

} // namespace Sr
