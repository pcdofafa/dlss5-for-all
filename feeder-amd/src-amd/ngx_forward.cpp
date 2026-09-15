// ngx_forward.cpp -- substituto da nvsdk_ngx_d.lib (MSVC, nao linka com MinGW) para a
// variante AMD do DLSS5-Feeder.
//
// A lib estatica da NVIDIA e um carregador fino: acha o _nvngx.dll do driver e repassa
// cada NVSDK_NGX_D3D12_* para o export de mesmo nome. Este arquivo faz o mesmo, em
// tempo de execucao:
//   - com driver NVIDIA (o _nvngx.dll existe): a variante se comporta como o Feeder
//     oficial, o DLSS roda de verdade;
//   - sem driver NVIDIA (Radeon): tudo devolve NVSDK_NGX_Result_FAIL_FeatureNotSupported,
//     que e exatamente o que o NGX real devolveria, e o modo portador FSR assume.
// Nenhuma funcao daqui e chamada no modo portador (amd_carrier=1).
//
// Ordem de busca do _nvngx.dll: ja carregado no processo; HKLM\SOFTWARE\NVIDIA
// Corporation\Global\NGXCore (FullPath ou NGXPath); por fim o caminho de busca padrao.
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <nvsdk_ngx.h>

static HMODULE g_nvngx        = nullptr;
static bool    g_nvngx_tried  = false;
static char    g_nvngx_status[512] = "not tried yet";

static void NgxForwardLoad()
{
    if (g_nvngx_tried) return;
    g_nvngx_tried = true;

    g_nvngx = GetModuleHandleW(L"_nvngx.dll");
    if (g_nvngx != nullptr)
    {
        wchar_t p[MAX_PATH] = {};
        GetModuleFileNameW(g_nvngx, p, MAX_PATH);
        _snprintf_s(g_nvngx_status, sizeof(g_nvngx_status), _TRUNCATE, "_nvngx.dll already in the process: %ls", p);
        return;
    }

    static const wchar_t *kValues[] = { L"FullPath", L"NGXPath" };
    for (const wchar_t *value : kValues)
    {
        wchar_t dir[MAX_PATH] = {};
        DWORD size = sizeof(dir);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", value,
                         RRF_RT_REG_SZ, nullptr, dir, &size) != ERROR_SUCCESS || dir[0] == L'\0')
            continue;
        wchar_t path[MAX_PATH] = {};
        _snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\_nvngx.dll", dir);
        g_nvngx = LoadLibraryW(path);
        if (g_nvngx != nullptr)
        {
            _snprintf_s(g_nvngx_status, sizeof(g_nvngx_status), _TRUNCATE, "_nvngx.dll loaded via NGXCore\\%ls: %ls", value, path);
            return;
        }
    }

    g_nvngx = LoadLibraryW(L"_nvngx.dll");
    if (g_nvngx != nullptr)
    {
        wchar_t p[MAX_PATH] = {};
        GetModuleFileNameW(g_nvngx, p, MAX_PATH);
        _snprintf_s(g_nvngx_status, sizeof(g_nvngx_status), _TRUNCATE, "_nvngx.dll loaded from the search path: %ls", p);
        return;
    }
    _snprintf_s(g_nvngx_status, sizeof(g_nvngx_status), _TRUNCATE,
                "_nvngx.dll not found (no NVIDIA driver NGX core in this system); every NGX call fails with FeatureNotSupported");
}

template <typename Fn> static Fn NgxForwardResolve(const char *name)
{
    NgxForwardLoad();
    if (g_nvngx == nullptr) return nullptr;
    return reinterpret_cast<Fn>(reinterpret_cast<void *>(GetProcAddress(g_nvngx, name)));
}

extern "C" const char *NgxForwardDescribe()
{
    NgxForwardLoad();
    return g_nvngx_status;
}

// O modulo do NGX core do driver, para quem precisa resolver nele um export que este
// arquivo nao repassa. Hoje so o spike do Neural Rendering (nr_spike): o snippet do
// DLSS-NR exige que o core tenha sido inicializado pela sobrecarga de QUATRO argumentos
// (a do NGX_SNIPPET_BUILD do header), e a assinatura publica repassada aqui e a de cinco.
extern "C" HMODULE NgxForwardModule()
{
    NgxForwardLoad();
    return g_nvngx;
}

extern "C" bool NgxForwardAvailable()
{
    NgxForwardLoad();
    return g_nvngx != nullptr;
}

static const NVSDK_NGX_Result kNoNgx = NVSDK_NGX_Result_FAIL_FeatureNotSupported;

// ---- assinaturas dos exports do _nvngx.dll (as que a lib estatica chama) -------------
typedef NVSDK_NGX_Result (NVSDK_CONV *PfnInitExt)(unsigned long long, const wchar_t *, ID3D12Device *, NVSDK_NGX_Version, const NVSDK_NGX_FeatureCommonInfo *);
typedef NVSDK_NGX_Result (NVSDK_CONV *PfnInitPlain)(unsigned long long, const wchar_t *, ID3D12Device *, const NVSDK_NGX_FeatureCommonInfo *, NVSDK_NGX_Version);
typedef NVSDK_NGX_Result (NVSDK_CONV *PfnInitProject)(const char *, NVSDK_NGX_EngineType, const char *, const wchar_t *, ID3D12Device *, NVSDK_NGX_Version, const NVSDK_NGX_FeatureCommonInfo *);
typedef NVSDK_NGX_Result (NVSDK_CONV *PfnShutdown1)(ID3D12Device *);
typedef NVSDK_NGX_Result (NVSDK_CONV *PfnParams)(NVSDK_NGX_Parameter **);
typedef NVSDK_NGX_Result (NVSDK_CONV *PfnDestroyParams)(NVSDK_NGX_Parameter *);
typedef NVSDK_NGX_Result (NVSDK_CONV *PfnCreate)(ID3D12GraphicsCommandList *, NVSDK_NGX_Feature, NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
typedef NVSDK_NGX_Result (NVSDK_CONV *PfnRelease)(NVSDK_NGX_Handle *);
typedef NVSDK_NGX_Result (NVSDK_CONV *PfnEvaluate)(ID3D12GraphicsCommandList *, const NVSDK_NGX_Handle *, const NVSDK_NGX_Parameter *, PFN_NVSDK_NGX_ProgressCallback);

// ---- as funcoes que o dlss5-feed.cpp referencia (mesmos prototipos do nvsdk_ngx.h) ----

NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath,
                                                 ID3D12Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo,
                                                 NVSDK_NGX_Version InSDKVersion)
{
    // A lib estatica chama Init_Ext (versao do SDK antes do FeatureCommonInfo); o export
    // Init simples fica de reserva.
    if (auto f = NgxForwardResolve<PfnInitExt>("NVSDK_NGX_D3D12_Init_Ext"))
    {
        const NVSDK_NGX_Result r = f(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
        if (NVSDK_NGX_SUCCEED(r)) return r;
    }
    if (auto f = NgxForwardResolve<PfnInitPlain>("NVSDK_NGX_D3D12_Init"))
        return f(InApplicationId, InApplicationDataPath, InDevice, InFeatureInfo, InSDKVersion);
    return kNoNgx;
}

NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init_with_ProjectID(const char *InProjectId, NVSDK_NGX_EngineType InEngineType,
                                                                const char *InEngineVersion, const wchar_t *InApplicationDataPath,
                                                                ID3D12Device *InDevice, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo,
                                                                NVSDK_NGX_Version InSDKVersion)
{
    if (auto f = NgxForwardResolve<PfnInitProject>("NVSDK_NGX_D3D12_Init_ProjectID"))
        return f(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
    return kNoNgx;
}

extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Shutdown1(ID3D12Device *InDevice)
{
    if (auto f = NgxForwardResolve<PfnShutdown1>("NVSDK_NGX_D3D12_Shutdown1")) return f(InDevice);
    return kNoNgx;
}

extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_GetCapabilityParameters(NVSDK_NGX_Parameter **OutParameters)
{
    if (auto f = NgxForwardResolve<PfnParams>("NVSDK_NGX_D3D12_GetCapabilityParameters")) return f(OutParameters);
    if (OutParameters) *OutParameters = nullptr;
    return kNoNgx;
}

extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_AllocateParameters(NVSDK_NGX_Parameter **OutParameters)
{
    if (auto f = NgxForwardResolve<PfnParams>("NVSDK_NGX_D3D12_AllocateParameters")) return f(OutParameters);
    if (OutParameters) *OutParameters = nullptr;
    return kNoNgx;
}

extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_DestroyParameters(NVSDK_NGX_Parameter *InParameters)
{
    if (auto f = NgxForwardResolve<PfnDestroyParams>("NVSDK_NGX_D3D12_DestroyParameters")) return f(InParameters);
    return kNoNgx;
}

extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_CreateFeature(ID3D12GraphicsCommandList *InCmdList, NVSDK_NGX_Feature InFeatureID,
                                                                     NVSDK_NGX_Parameter *InParameters, NVSDK_NGX_Handle **OutHandle)
{
    if (auto f = NgxForwardResolve<PfnCreate>("NVSDK_NGX_D3D12_CreateFeature")) return f(InCmdList, InFeatureID, InParameters, OutHandle);
    if (OutHandle) *OutHandle = nullptr;
    return kNoNgx;
}

extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle *InHandle)
{
    if (auto f = NgxForwardResolve<PfnRelease>("NVSDK_NGX_D3D12_ReleaseFeature")) return f(InHandle);
    return kNoNgx;
}

extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_EvaluateFeature(ID3D12GraphicsCommandList *InCmdList, const NVSDK_NGX_Handle *InFeatureHandle,
                                                                       const NVSDK_NGX_Parameter *InParameters, PFN_NVSDK_NGX_ProgressCallback InCallback)
{
    if (auto f = NgxForwardResolve<PfnEvaluate>("NVSDK_NGX_D3D12_EvaluateFeature")) return f(InCmdList, InFeatureHandle, InParameters, InCallback);
    return kNoNgx;
}

// O helper NGX_D3D12_EVALUATE_DLSS_EXT chama a variante _C (callback com bool*). O
// _nvngx.dll nao exporta _C; a lib estatica adapta o callback. O Feeder passa NULL,
// entao repassamos sem callback (um callback nao nulo seria ignorado -- e nunca ha).
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_EvaluateFeature_C(ID3D12GraphicsCommandList *InCmdList, const NVSDK_NGX_Handle *InFeatureHandle,
                                                                         const NVSDK_NGX_Parameter *InParameters, PFN_NVSDK_NGX_ProgressCallback_C /*InCallback*/)
{
    if (auto f = NgxForwardResolve<PfnEvaluate>("NVSDK_NGX_D3D12_EvaluateFeature")) return f(InCmdList, InFeatureHandle, InParameters, nullptr);
    return kNoNgx;
}

// ---- NVSDK_NGX_Parameter_* : na lib estatica sao so a chamada virtual correspondente ----
// (o objeto vem do _nvngx.dll; com parametro nulo -- forwarder sem NGX -- nao ha o que fazer)

extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetULL(NVSDK_NGX_Parameter *p, const char *n, unsigned long long v) { if (p) p->Set(n, v); }
extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetF(NVSDK_NGX_Parameter *p, const char *n, float v)                { if (p) p->Set(n, v); }
extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetD(NVSDK_NGX_Parameter *p, const char *n, double v)               { if (p) p->Set(n, v); }
extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetUI(NVSDK_NGX_Parameter *p, const char *n, unsigned int v)        { if (p) p->Set(n, v); }
extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetI(NVSDK_NGX_Parameter *p, const char *n, int v)                  { if (p) p->Set(n, v); }
extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetD3d11Resource(NVSDK_NGX_Parameter *p, const char *n, ID3D11Resource *v) { if (p) p->Set(n, v); }
extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetD3d12Resource(NVSDK_NGX_Parameter *p, const char *n, ID3D12Resource *v) { if (p) p->Set(n, v); }
extern "C" void NVSDK_CONV NVSDK_NGX_Parameter_SetVoidPointer(NVSDK_NGX_Parameter *p, const char *n, void *v)      { if (p) p->Set(n, v); }
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetULL(NVSDK_NGX_Parameter *p, const char *n, unsigned long long *o) { return p ? p->Get(n, o) : kNoNgx; }
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetF(NVSDK_NGX_Parameter *p, const char *n, float *o)                { return p ? p->Get(n, o) : kNoNgx; }
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetD(NVSDK_NGX_Parameter *p, const char *n, double *o)               { return p ? p->Get(n, o) : kNoNgx; }
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetUI(NVSDK_NGX_Parameter *p, const char *n, unsigned int *o)        { return p ? p->Get(n, o) : kNoNgx; }
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetI(NVSDK_NGX_Parameter *p, const char *n, int *o)                  { return p ? p->Get(n, o) : kNoNgx; }
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetD3d11Resource(NVSDK_NGX_Parameter *p, const char *n, ID3D11Resource **o) { return p ? p->Get(n, o) : kNoNgx; }
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetD3d12Resource(NVSDK_NGX_Parameter *p, const char *n, ID3D12Resource **o) { return p ? p->Get(n, o) : kNoNgx; }
extern "C" NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_Parameter_GetVoidPointer(NVSDK_NGX_Parameter *p, const char *n, void **o)      { return p ? p->Get(n, o) : kNoNgx; }
