// nr_device_probe.cpp -- responde uma pergunta so: o D3D12CreateDevice devolve um
// objeto NOVO a cada chamada, ou recicla o mesmo device por adaptador?
//
// Motivo: o spike do Neural Rendering criou um "device privado" e logou o mesmo
// ponteiro do device do feed. Ou o log estava errado (nao estava) ou o runtime
// reciclou. Isto e API pura -- nao precisa do jogo nem do ReShade.
//
//   g++ -std=c++20 -O2 tests/nr_device_probe.cpp -o build/nr_device_probe.exe \
//       -ld3d12 -ldxgi -ldxguid
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>

static void Line() { printf("--------------------------------------------------------\n"); }

int main()
{
    IDXGIFactory4 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void **>(&factory))) || factory == nullptr)
    { printf("CreateDXGIFactory1 falhou\n"); return 1; }

    IDXGIAdapter1 *a0 = nullptr;
    if (FAILED(factory->EnumAdapters1(0, &a0)) || a0 == nullptr)
    { printf("EnumAdapters1(0) falhou\n"); return 1; }

    DXGI_ADAPTER_DESC1 desc = {};
    a0->GetDesc1(&desc);
    printf("adaptador: %ls\n", desc.Description);
    printf("LUID     : HighPart=%08lX LowPart=%08lX  (HighPart 0 e normal)\n",
           static_cast<unsigned long>(desc.AdapterLuid.HighPart),
           static_cast<unsigned long>(desc.AdapterLuid.LowPart));
    Line();

    // 1) dois creates seguidos, MESMO objeto de adaptador
    ID3D12Device *dev1 = nullptr, *dev2 = nullptr;
    const HRESULT hr1 = D3D12CreateDevice(a0, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void **>(&dev1));
    const HRESULT hr2 = D3D12CreateDevice(a0, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void **>(&dev2));
    printf("create #1 (adapter a0) hr=0x%08lX ptr=%p\n", static_cast<unsigned long>(hr1), static_cast<void *>(dev1));
    printf("create #2 (adapter a0) hr=0x%08lX ptr=%p  -> MESMO OBJETO? %s\n",
           static_cast<unsigned long>(hr2), static_cast<void *>(dev2), dev1 == dev2 ? "SIM" : "nao");
    Line();

    // 2) create por um adaptador obtido via EnumAdapterByLuid -- o caminho do spike
    IDXGIAdapter1 *a1 = nullptr;
    const HRESULT hra = factory->EnumAdapterByLuid(desc.AdapterLuid, __uuidof(IDXGIAdapter1), reinterpret_cast<void **>(&a1));
    printf("EnumAdapterByLuid hr=0x%08lX ptr=%p  (o mesmo objeto de a0? %s)\n",
           static_cast<unsigned long>(hra), static_cast<void *>(a1), a0 == a1 ? "SIM" : "nao");
    ID3D12Device *dev3 = nullptr;
    const HRESULT hr3 = a1 ? D3D12CreateDevice(a1, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void **>(&dev3))
                           : E_FAIL;
    printf("create #3 (adapter por LUID) hr=0x%08lX ptr=%p  -> MESMO QUE #1? %s\n",
           static_cast<unsigned long>(hr3), static_cast<void *>(dev3), dev1 == dev3 ? "SIM" : "nao");
    Line();

    // 3) identidade COM por IUnknown (a regra formal do COM), e refcount
    IUnknown *u1 = nullptr, *u3 = nullptr;
    if (dev1) dev1->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&u1));
    if (dev3) dev3->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&u3));
    printf("IUnknown de #1 = %p, de #3 = %p  -> mesma identidade COM? %s\n",
           static_cast<void *>(u1), static_cast<void *>(u3), (u1 != nullptr && u1 == u3) ? "SIM" : "nao");
    if (dev1)
    {
        const ULONG rc = dev1->AddRef();
        dev1->Release();
        printf("refcount de #1 depois de AddRef: %lu  (alto = varias referencias vivas ao mesmo objeto)\n", rc);
    }
    Line();

    // 4) o teste que importa para o spike: soltar o "privado" mata o outro?
    if (dev3 != nullptr && dev1 == dev3)
        printf("VEREDITO: o runtime RECICLA o device por adaptador. Um Release() no\n"
               "          'device privado' apenas decrementa a referencia do device do\n"
               "          feed. Nao existe isolamento -- a sessao NGX e a mesma.\n");
    else if (dev3 != nullptr)
        printf("VEREDITO: sao objetos DISTINTOS. O device privado e real e o bug do log\n"
               "          esta noutro lugar.\n");

    if (u3) u3->Release();
    if (u1) u1->Release();
    if (dev3) dev3->Release();
    if (dev2) dev2->Release();
    if (dev1) dev1->Release();
    if (a1) a1->Release();
    a0->Release();
    factory->Release();
    return 0;
}
