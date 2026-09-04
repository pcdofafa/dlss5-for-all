# Patch de latência para `main.cpp`

Blocos literais de busca e substituição. Conferidos byte a byte contra
`C:\Users\PCDOFAFA\Desktop\dlss cam\main.cpp` na versão de **2026-09-02 19:16** (69.949 bytes) —
a que já tem bandeja, atalhos em cascata, `ToastTick`, cartaz de aviso e os estilos da janela
sobreposta.

**São 9 substituições.** Aplique na ordem: a (3) e a (4) são as duas metades da mesma cirurgia
no `Renderer::Init`, e a (4) só casa depois que a (3) entrou.

Todas são **seguras aplicar já**: vsync mantido, sem tearing, sem mudança de resolução, de
formato de pixel ou de contagem de frames. Nenhuma exige teste visual.

Resultado medido: **~121 ms → ~42 ms** do frame na mão até o pixel na tela, com a cadeia
ReShade+DLSS a 1080p. Detalhes e metodologia em `docs\latencia.md`.

---

### 1. Includes — `IDXGIFactory2`, `IDXGISwapChain2` e a flag do waitable

`IDXGISwapChain2` e `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` vivem em `dxgi1_3.h`;
`IDXGIFactory2` e `DXGI_SWAP_CHAIN_DESC1` em `dxgi1_2.h`. `IDXGIDevice1` já vem do `dxgi.h`.

ANTES:
```cpp
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
```

DEPOIS:
```cpp
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <d3dcompiler.h>
```

---

### 2. Membros do `Renderer` + a função de espera

Guarda o `IDXGISwapChain2`, o objeto de espera, as flags do swapchain (que precisam voltar no
`ResizeBuffers`) e a trava que garante **1 espera por Present**.

ANTES:
```cpp
    bool cpuUpload = true;
    ID3D11ShaderResourceView* extSrv = nullptr;

    bool CreateTargets(std::wstring* err) {
```

DEPOIS:
```cpp
    bool cpuUpload = true;
    ID3D11ShaderResourceView* extSrv = nullptr;

    // --------- latencia de apresentacao
    // Por padrao o DXGI deixa o driver enfileirar 3 frames na nossa frente. Com a cadeia
    // DLSS pesando no Present isso vira ~110 ms de atraso puro de fila (medido). O swapchain
    // "waitable" com SetMaximumFrameLatency(1) troca a fila por uma espera explicita.
    //
    // ONDE a espera acontece e o que decide o ganho, e isso foi medido:
    //   - esperando ANTES do Tick (no loop principal):     42 ms  <- e o que fazemos
    //   - esperando dentro do DrawAndPresent:              78 ms  (igual a nao fazer nada)
    //   - waitable + SetMaximumFrameLatency do device:     duas esperas por frame, 50 -> 20 fps
    // Por isso o SetMaximumFrameLatency do IDXGIDevice1 so entra quando NAO ha waitable.
    IDXGISwapChain2* sc2  = nullptr;    // so existe no caminho waitable
    HANDLE  latWait       = nullptr;    // GetFrameLatencyWaitableObject
    UINT    scFlags       = 0;          // precisa ser repassado no ResizeBuffers
    bool    slotWaited    = false;      // exatamente 1 espera por Present

    // Chamada no TOPO do ciclo, antes de pegar o frame da fonte: espera a GPU/DXGI
    // aceitarem frame novo e so entao o loop vai buscar a imagem - que assim e a mais
    // nova que existe. Sem objeto de espera (fallback) vira no-op e nada muda.
    void WaitForPresentSlot() {
        if (latWait && !slotWaited) { WaitForSingleObjectEx(latWait, 100, FALSE); slotWaited = true; }
    }

    bool CreateTargets(std::wstring* err) {
```

---

### 3. `Renderer::Init` vira três funções — parte 1 de 2

O swapchain waitable **não pode** nascer do `D3D11CreateDeviceAndSwapChain` (ele não aceita a
flag): precisa de `IDXGIFactory2::CreateSwapChainForHwnd`. Então `Init` passa a ser um
despachante: tenta o waitable, e se qualquer passo falhar cai no `InitLegacy`, que é o código de
hoje inteirinho (FLIP_DISCARD com fallback pra DISCARD clássico) — **o fallback continua
funcionando igual em quem não suporta**.

ANTES:
```cpp
    bool Init(HWND hwnd, UINT cw, UINT ch, std::wstring* err) {
        w = cw; h = ch;
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
```

DEPOIS:
```cpp
    bool Init(HWND hwnd, UINT cw, UINT ch, std::wstring* err) {
        w = cw; h = ch;
        if (!InitWaitable(hwnd) && !InitLegacy(hwnd, err)) return false;
        if (!latWait) {
            // Sem objeto de espera: pelo menos corta a fila do driver de 3 pra 1.
            // So aqui - somar isso COM o waitable da duas esperas por frame e derruba
            // o throughput pela metade (medido: 50 -> 20 fps).
            IDXGIDevice1* d1 = nullptr;
            if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice1), (void**)&d1)) && d1) {
                d1->SetMaximumFrameLatency(1); d1->Release();
            }
        }
        Log("apresentacao: waitable=%d (objeto=%p) flags=0x%X", latWait ? 1 : 0, (void*)latWait, scFlags);
        return InitRest(err);
    }

    // Caminho preferido: FLIP_DISCARD + FRAME_LATENCY_WAITABLE_OBJECT + max latency 1.
    // Se qualquer passo falhar, devolve false com dev/ctx/sc zerados e o InitLegacy assume.
    bool InitWaitable(HWND hwnd) {
        D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0 }; D3D_FEATURE_LEVEL got{};
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, lv, 1,
                                       D3D11_SDK_VERSION, &dev, &got, &ctx);
        if (FAILED(hr)) {
            Log("D3D11CreateDevice (waitable) falhou 0x%08X", (unsigned)hr);
            SafeRelease(ctx); SafeRelease(dev); return false;
        }

        IDXGIDevice* dxdev = nullptr; IDXGIAdapter* ad = nullptr; IDXGIFactory2* f2 = nullptr;
        dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev);
        if (dxdev) { dxdev->GetAdapter(&ad); dxdev->Release(); }
        if (ad) { ad->GetParent(__uuidof(IDXGIFactory2), (void**)&f2); ad->Release(); }
        if (!f2) {
            Log("IDXGIFactory2 indisponivel - swapchain classico");
            SafeRelease(ctx); SafeRelease(dev); return false;
        }

        DXGI_SWAP_CHAIN_DESC1 d1{};
        d1.Width = w; d1.Height = h; d1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d1.SampleDesc.Count = 1; d1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        d1.BufferCount = 2; d1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        d1.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        d1.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        IDXGISwapChain1* s1 = nullptr;
        hr = f2->CreateSwapChainForHwnd(dev, hwnd, &d1, nullptr, nullptr, &s1);
        f2->Release();
        if (FAILED(hr) || !s1) {
            Log("CreateSwapChainForHwnd(waitable) falhou 0x%08X - swapchain classico", (unsigned)hr);
            SafeRelease(ctx); SafeRelease(dev); return false;
        }
        sc = s1; scFlags = d1.Flags;
        if (SUCCEEDED(s1->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&sc2)) && sc2) {
            sc2->SetMaximumFrameLatency(1);
            latWait = sc2->GetFrameLatencyWaitableObject();
        }
        Log("D3D11 ok, swapchain waitable %ux%u FLIP_DISCARD", w, h);
        return true;
    }

    bool InitLegacy(HWND hwnd, std::wstring* err) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
```

---

### 4. `Renderer::Init` vira três funções — parte 2 de 2

Fecha o `InitLegacy` no ponto onde o código de hoje terminava a criação do swapchain, e abre o
`InitRest` com o resto (shaders, sampler, depth state, rasterizer, `CreateTargets`) — que passa a
rodar depois de qualquer um dos dois caminhos. O `HRESULT hr = S_OK;` é necessário porque o `hr`
que o corpo usa ficou na função de cima.

ANTES:
```cpp
        if (FAILED(hr)) { *err = L"D3D11CreateDeviceAndSwapChain falhou " + Hex(hr); return false; }
        Log("D3D11 ok, swapchain %ux%u, swapeffect=%d", w, h, (int)sd.SwapEffect);

        // shaders (d3dcompiler_47.dll vem com o Windows 10/11)
        HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
```

DEPOIS:
```cpp
        if (FAILED(hr)) { *err = L"D3D11CreateDeviceAndSwapChain falhou " + Hex(hr); return false; }
        Log("D3D11 ok, swapchain %ux%u, swapeffect=%d", w, h, (int)sd.SwapEffect);
        return true;
    }

    bool InitRest(std::wstring* err) {
        HRESULT hr = S_OK;
        // shaders (d3dcompiler_47.dll vem com o Windows 10/11)
        HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
```

---

### 5. `Resize` — a flag do swapchain tem que voltar no `ResizeBuffers`

Sem isso o swapchain perde a característica de waitable no primeiro redimensionamento (que
acontece sempre, logo na primeira troca de tamanho do alvo) e a latência volta ao que era.
Com o fallback clássico `scFlags` vale 0 e o comportamento é idêntico ao de hoje.

ANTES:
```cpp
        HRESULT hr = sc->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
```

DEPOIS:
```cpp
        HRESULT hr = sc->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, scFlags);
```

---

### 6. `DrawAndPresent` — libera a trava da espera

O `Present` continua `(1, 0)`: **vsync mantido, sem tearing**. Só rearma a permissão de esperar
de novo no próximo ciclo.

ANTES:
```cpp
        ID3D11ShaderResourceView* n = nullptr; ctx->PSSetShaderResources(0, 1, &n);
        sc->Present(1, 0);
    }
```

DEPOIS:
```cpp
        ID3D11ShaderResourceView* n = nullptr; ctx->PSSetShaderResources(0, 1, &n);
        sc->Present(1, 0);
        slotWaited = false;
    }
```

---

### 7. `Shutdown` — solta o `IDXGISwapChain2`

ANTES:
```cpp
        SafeRelease(sc); SafeRelease(ctx); SafeRelease(dev);
```

DEPOIS:
```cpp
        SafeRelease(sc2); SafeRelease(sc); SafeRelease(ctx); SafeRelease(dev);
```

---

### 8. Loop principal — a espera no lugar CERTO (é aqui que mora o ganho)

Esta é a substituição que vale os 79 ms. A espera tem que ficar **antes** de `scap.Tick()` /
`cap.Grab()`. Medido: aqui dá 42 ms; a mesma espera colocada dentro do `DrawAndPresent` dá
78 ms (ou seja, não adianta nada — é o mesmo que deixar o `Present` bloquear).

O motivo: esperando aqui, o loop fica parado até a GPU liberar e **só então** vai buscar a
imagem, que por isso é a mais nova que existe. Esperando depois, a imagem é pega cedo e
envelhece na nossa mão enquanto o `Present` bloqueia.

ANTES:
```cpp
        if (C.overlay) FollowTarget(hwnd, scap, overlayVisible, bypass, !C.hideSelf);
        ToastTick();

        bool got = false;
```

DEPOIS:
```cpp
        if (C.overlay) FollowTarget(hwnd, scap, overlayVisible, bypass, !C.hideSelf);
        ToastTick();

        // Espera a GPU/DXGI aceitarem frame novo ANTES de pegar o frame da fonte.
        // Esperar depois (dentro do Present) faz o frame envelhecer na nossa mao:
        // medido 42 ms esperando aqui contra 78 ms esperando la.
        R.WaitForPresentSlot();

        bool got = false;
```

---

### 9. Loop principal — medição do Present no log

Alimenta as colunas `present` e `PRESENT->TELA` do relatório `LAT` que o `screen_capture.h`
já escreve no `dlss5forall.log`. Sem isso o relatório sai só com as colunas de captura.
Custo: 2 `QueryPerformanceCounter` por frame.

ANTES:
```cpp
        if ((got && !hidden) || now - lastPresent >= gap) { R.DrawAndPresent(); presented++; lastPresent = now; }
```

DEPOIS:
```cpp
        if ((got && !hidden) || now - lastPresent >= gap) {
            LONGLONG p0 = g_lat.Now();
            R.DrawAndPresent();
            if (got) g_lat.Present(p0, g_lat.Now(), R.sc);   // relatorio LAT no dlss5forall.log
            presented++; lastPresent = now;
        }
```

---

## Depois de aplicar

Compile normalmente (`./build-mingw.sh DLSS5ForAll.exe`) e confira as duas primeiras linhas
novas no `dlss5forall.log`:

```
D3D11 ok, swapchain waitable 1916x1071 FLIP_DISCARD
apresentacao: waitable=1 (objeto=00000000000000b48) flags=0x40
```

`waitable=1` e `flags=0x40` significam que o caminho rápido pegou. Se sair `waitable=0`, o
fallback entrou (e o `SetMaximumFrameLatency(1)` do device foi aplicado no lugar) — ainda é
melhor que hoje, mas vale investigar o motivo logado logo acima.

A cada 300 frames sai o relatório de latência:

```
LAT 10s | fps chegada=50.0 copiado=29.9 present=29.9 | srt -9.19/-8.55 | acordar 13.88/19.42 | tick 0.16/0.28 | present 1.16/1.67 | PRESENT->TELA 47.76/60.94 (n=272)  [ms media/p95]
LAT ritmo do WGC (intervalo entre FrameArrived): min 18.15 p05 18.96 media 20.00 p95 20.96 max 21.77 ms
```

## Cópia já pronta

O `main.cpp` com as 9 substituições aplicadas e **compilando** está em:

```
C:\Users\PCDOFAFA\AppData\Local\Temp\claude\C--Users-PCDOFAFA-Desktop-dlss-cam\c9f972eb-7d23-432a-b980-290e431b1700\scratchpad\final\main.cpp
```

Serve de conferência se alguma substituição não casar. O diff unificado equivalente está ao lado,
em `final\main.diff`.
