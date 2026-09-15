// Prova de vida do Optical Flow da NVIDIA, sem o SDK: so a DLL que ja esta na maquina.
// Se isto imprimir uma versao e uma function list preenchida, o caminho existe de verdade.
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdint.h>

// O bastante do contrato do NvOFAPI para provar vida. NV_OF_SUCCESS == 0.
typedef int NV_OF_STATUS;
typedef NV_OF_STATUS (__stdcall *PFNGETMAXVER)(uint32_t* version);
// A function list e uma tabela de ponteiros; 64 slots cobrem com folga a v5.
// A function list do NvOFAPI comeca com um uint32 de VERSAO - sem ele a criacao devolve
// status 7 (NV_OF_ERR_INVALID_VERSION), que foi o que aconteceu na primeira tentativa.
struct FuncList { uint32_t version; void* fn[63]; };
// A assinatura real do SDK: a VERSAO vem primeiro, e o device so entra depois, no nvOFInit.
// Passar o device onde ele espera o uint32 da versao devolve status 7 (INVALID_VERSION).
typedef NV_OF_STATUS (__stdcall *PFNCREATE11)(uint32_t apiVer, FuncList*);

int main() {
    HMODULE m = LoadLibraryA("NvOFAPI64.dll");
    printf("LoadLibrary NvOFAPI64.dll -> %p\n", (void*)m);
    if (!m) { printf("  erro %lu\n", GetLastError()); return 1; }

    PFNGETMAXVER getver = (PFNGETMAXVER)GetProcAddress(m, "NvOFGetMaxSupportedApiVersion");
    PFNCREATE11  cr11   = (PFNCREATE11)GetProcAddress(m, "NvOFAPICreateInstanceD3D11");
    printf("NvOFGetMaxSupportedApiVersion = %p\nNvOFAPICreateInstanceD3D11    = %p\n",
           (void*)getver, (void*)cr11);
    if (!getver || !cr11) return 2;

    uint32_t ver = 0;
    NV_OF_STATUS st = getver(&ver);
    printf("versao maxima suportada: status=%d  version=0x%08X (major %u, minor %u)\n",
           st, ver, ver >> 16, ver & 0xFFFF);

    // Um device D3D11 na placa: o mesmo caminho que a camada usa.
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
    printf("D3D11CreateDevice hr=0x%08X dev=%p\n", (unsigned)hr, (void*)dev);
    if (FAILED(hr) || !dev) return 3;

    FuncList fl2{};
    fl2.version = ver;   // a maxima que a propria DLL declarou suportar
    st = cr11(ver, &fl2);
    int preenchidos = 0;
    for (int i = 0; i < 63; i++) if (fl2.fn[i]) preenchidos++;
    printf("NvOFAPICreateInstanceD3D11: status=%d  ponteiros preenchidos=%d\n", st, preenchidos);
    if (st == 0 && preenchidos > 0) printf("\n>>> O OPTICAL FLOW ESTA DISPONIVEL E RESPONDE NESTA MAQUINA.\n");
    else printf("\n>>> nao criou a instancia (status %d)\n", st);

    if (ctx) ctx->Release();
    if (dev) dev->Release();
    return 0;
}
