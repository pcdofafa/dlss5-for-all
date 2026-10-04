// =====================================================================================
//  alvo_animado.cpp -- uma janela que se mexe de um jeito CONHECIDO, para medir a camada.
//
//  POR QUE ISTO EXISTE. Medir o custo e o efeito do frame skipping precisa de uma fonte
//  em movimento continuo e com velocidade conhecida. Um jogo real serve, mas nao e
//  reproduzivel (e o Witcher 3 pausa fora de foco, o que ja produziu a leitura falsa de
//  "motion vectors zerados" -- ver docs/pesquisa-2026-09-11/RELATORIO-MV-DEPTH-ZERADOS.md).
//  Aqui a velocidade e um argumento, o conteudo e o mesmo sempre, e a janela nao pausa
//  quando perde o foco -- que e exatamente o que um alvo de medicao tem de fazer.
//
//  O conteudo e o mesmo do tests/warp_test.cpp: ruido multi-oitava, grade fina e areas
//  chapadas. Isso importa porque cor chapada nao da material nem para o estimador de
//  movimento nem para a rede neural, e um alvo assim mede o nada.
//
//  Uso:
//     alvo_animado.exe [largura] [altura] [px_por_quadro]
//     (padrao: 1280 720 8)
//
//  O titulo da janela e fixo -- ZEBRA7 -- para o app achar com --window.
//
//  Compilar (msys64) -- os dois detalhes da linha abaixo custaram tempo e ficam escritos:
//
//    g++ -std=c++17 -O2 -Wall -Wextra tests/alvo_animado.cpp -o tests/alvo_animado.exe
//        -ld3d11 -ldxgi -static -static-libgcc -static-libstdc++
//
//    - SEM `-mwindows`: com ele o link escolhe WinMainCRTStartup, o `main` nunca roda, e o
//      processo sobe, fica com 0% de CPU e NENHUMA janela -- sem erro em lugar nenhum;
//    - COM `-static`: sem isso o exe depende das DLLs de C:\msys64\mingw64\bin e so roda
//      de dentro do shell do msys. Iniciado pelo Explorer ou por Start-Process ele morre
//      no carregador, com o MESMO sintoma do item acima (vivo, 0% de CPU, sem janela) --
//      dois defeitos diferentes que se parecem, que foi o que fez a busca demorar. E a
//      mesma armadilha que o build-mingw.sh do app ja documenta.
// =====================================================================================

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

template <class T> static void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

typedef HRESULT(WINAPI* PFN_D3DCOMPILE)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR,
                                        LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
static PFN_D3DCOMPILE Compilador()
{
    static PFN_D3DCOMPILE fn = nullptr; static bool tentou = false;
    if (!tentou) {
        tentou = true;
        HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
        if (m) fn = (PFN_D3DCOMPILE)(void*)GetProcAddress(m, "D3DCompile");
    }
    return fn;
}

// ------------------------------------------------------------------ conteudo

static uint32_t Hash32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
}
static float Aleatorio(int x, int y, int s)
{
    return (float)(Hash32((uint32_t)(x * 374761393 + y * 668265263 + s * 362437)) & 0xFFFF) / 65535.0f;
}
static float RuidoOitava(float x, float y, int celula, int s)
{
    const float fx = x / (float)celula, fy = y / (float)celula;
    const int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    const float tx = fx - (float)x0, ty = fy - (float)y0;
    const float sx = tx * tx * (3.f - 2.f * tx), sy = ty * ty * (3.f - 2.f * ty);
    const float a = Aleatorio(x0, y0, s),     b = Aleatorio(x0 + 1, y0, s);
    const float c = Aleatorio(x0, y0 + 1, s), d = Aleatorio(x0 + 1, y0 + 1, s);
    const float ab = a + (b - a) * sx, cd = c + (d - c) * sx;
    return ab + (cd - ab) * sy;
}

// A textura e o DOBRO da janela em largura: assim a translacao pode correr sem fim,
// dando a volta pelo wrap do sampler sem emenda visivel no meio do movimento.
static std::vector<uint8_t> GerarTextura(int w, int h)
{
    std::vector<uint8_t> px((size_t)w * (size_t)h * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float n = 0.f, amp = 0.5f;
            for (int celula = 64; celula >= 8; celula /= 2) {
                n += RuidoOitava((float)x, (float)y, celula, celula) * amp; amp *= 0.5f;
            }
            float r = n, g = n * 0.85f + 0.10f, b = n * 0.70f + 0.20f;
            r += 0.15f * (float)x / (float)w; g += 0.10f * (float)y / (float)h;
            if ((x % 37) == 0 || (y % 43) == 0) { r += 0.35f; g += 0.35f; b += 0.35f; }
            if (((x / 97) + (y / 89)) % 7 == 0) { r *= 0.45f; g *= 0.55f; b *= 0.75f; }
            uint8_t* p = &px[((size_t)y * (size_t)w + (size_t)x) * 4];
            p[0] = (uint8_t)std::clamp((int)(b * 255.f), 0, 255);
            p[1] = (uint8_t)std::clamp((int)(g * 255.f), 0, 255);
            p[2] = (uint8_t)std::clamp((int)(r * 255.f), 0, 255);
            p[3] = 255;
        }
    }
    return px;
}

// MODO IMAGEM: mostra um PNG parado, do tamanho exato dele.
//
// POR QUE EXISTE. Serve para REALIMENTAR a saida da cadeia nela mesma. O modo `sintetizar`
// (docs/ARQUITETURA.md §12.1) desenharia, no quadro pulado, o quadro JA PROCESSADO
// deformado -- e o NR do RenoDX, que roda em todo Present, passaria por cima dele uma
// segunda vez. A tela alternaria entre "1x NR" e "2x NR", e se isso for visivel o recurso
// pisca por construcao. Para medir: grava-se a saida da cadeia com DLSS5_DUMP, mostra-se o
// PNG aqui, captura-se de novo, e comparam-se as duas imagens.
static std::vector<uint8_t> CarregarImagem(const wchar_t* caminho, int* w, int* h)
{
    std::vector<uint8_t> px;
    Gdiplus::GdiplusStartupInput gi;
    ULONG_PTR tok = 0;
    if (Gdiplus::GdiplusStartup(&tok, &gi, nullptr) != Gdiplus::Ok) return px;
    {
        Gdiplus::Bitmap bmp(caminho, FALSE);
        if (bmp.GetLastStatus() == Gdiplus::Ok) {
            const int iw = (int)bmp.GetWidth(), ih = (int)bmp.GetHeight();
            Gdiplus::Rect r(0, 0, iw, ih);
            Gdiplus::BitmapData bd{};
            if (bmp.LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
                px.resize((size_t)iw * (size_t)ih * 4);
                for (int y = 0; y < ih; y++)
                    memcpy(&px[(size_t)y * (size_t)iw * 4],
                           (const uint8_t*)bd.Scan0 + (INT_PTR)y * bd.Stride, (size_t)iw * 4);
                bmp.UnlockBits(&bd);
                *w = iw; *h = ih;
            }
        }
    }
    Gdiplus::GdiplusShutdown(tok);
    return px;
}

static const char kVS[] = R"(
cbuffer P : register(b0) { float2 desloc; float2 escala; };
void main(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    float2 t = float2((id << 1) & 2, id & 2);
    pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    uv  = t * escala + desloc;
}
)";
static const char kPS[] = R"(
Texture2D    src : register(t0);
SamplerState smp : register(s0);
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    return src.Sample(smp, uv);
}
)";

// ------------------------------------------------------------------ estado

static bool g_sair = false;

static LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CLOSE || m == WM_DESTROY) { g_sair = true; PostQuitMessage(0); return 0; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { g_sair = true; PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

// ENTRY ANSI DE PROPOSITO. Com `-municode` este arquivo linkou com WinMainCRTStartup (e
// nao wmainCRTStartup), e o resultado foi um processo que sobe, nao chama nada e fica
// parado sem janela -- vivo, com 0% de CPU, e sem nenhum erro em lugar nenhum. Os
// argumentos aqui sao tres numeros; nao ha o que ganhar com Unicode na linha de comando.
int main(int argc, char** argv)
{
    // MODO IMAGEM: `alvo_animado.exe --img <caminho.png> [posX] [posY]`. A janela nasce do
    // tamanho exato da imagem e nao anda -- e o modo de realimentar a cadeia nela mesma.
    const bool modoImg = (argc >= 3) && (strcmp(argv[1], "--img") == 0);
    std::vector<uint8_t> imgPx; int imgW = 0, imgH = 0;
    if (modoImg) {
        const int n = MultiByteToWideChar(CP_ACP, 0, argv[2], -1, nullptr, 0);
        std::wstring cam((size_t)(n > 0 ? n - 1 : 0), L'\0');
        if (n > 0) MultiByteToWideChar(CP_ACP, 0, argv[2], -1, &cam[0], n);
        imgPx = CarregarImagem(cam.c_str(), &imgW, &imgH);
        if (imgPx.empty()) { fprintf(stderr, "[alvo] nao consegui abrir a imagem '%s'\n", argv[2]); return 8; }
        fprintf(stderr, "[alvo] modo imagem: %dx%d de '%s'\n", imgW, imgH, argv[2]);
    }

    // W/H deixaram de ser const em 15/09/2026 (A5): o laco os atualiza quando a janela e
    // redimensionada (ver o bloco RESIZE, no laco). O tamanho de NASCIMENTO continua vindo daqui.
    int W  = modoImg ? imgW : ((argc >= 3) ? atoi(argv[1]) : 1280);
    int H  = modoImg ? imgH : ((argc >= 3) ? atoi(argv[2]) : 720);
    const float PXQ = modoImg ? 0.0f : ((argc >= 4) ? (float)atof(argv[3]) : 8.0f);   // pixels por quadro
    // vsync=0 destrava a fonte. Serve para MEDIR A CAMADA: preso ao vblank, o alvo entrega
    // 60 quadros por segundo e a camada acompanha sem esforco -- o custo real dela fica
    // escondido atras da folga. Destravado, a camada passa a ser o gargalo e o fps dela
    // vira a medida direta do custo por quadro.
    const int VSYNC = modoImg ? 1 : ((argc >= 5) ? atoi(argv[4]) : 1);
    // ONDE A JANELA NASCE. O padrao cai no monitor SECUNDARIO (o ultrawide, que comeca em
    // X=1536 nas coordenadas de tela desta maquina), e nao e capricho: o monitor principal
    // esta reservado para quem testa jogo com teclado e mouse de verdade, e um alvo animado
    // em primeiro plano no monitor errado atrapalha a medicao alheia. Da para mover por
    // argumento quando a divisao de monitores mudar.
    const int POSX = modoImg ? ((argc >= 4) ? atoi(argv[3]) : 1560) : ((argc >= 6) ? atoi(argv[5]) : 1560);
    const int POSY = modoImg ? ((argc >= 5) ? atoi(argv[4]) : 30)   : ((argc >= 7) ? atoi(argv[6]) : 30);

    setvbuf(stderr, nullptr, _IONBF, 0);

    // 1 UNIDADE = 1 PIXEL, e isto nao e detalhe. Sem consciencia de DPI, num monitor a 125%
    // uma janela pedida com 2200x900 nasce com 2750x1125 pixels de verdade e o Windows
    // ESTICA o conteudo -- o que, num alvo feito para medir imagem, inventa uma reamostragem
    // no meio da medicao. O app tambem e PER_MONITOR_AWARE_V2, pelo mesmo motivo.
    typedef BOOL(WINAPI * PFN_DPI)(HANDLE);
    if (HMODULE u32 = GetModuleHandleW(L"user32.dll"))
        if (PFN_DPI fn = (PFN_DPI)(void*)GetProcAddress(u32, "SetProcessDpiAwarenessContext"))
            fn((HANDLE)-4);   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2

    fprintf(stderr, "[alvo] inicio %dx%d %.1f px/quadro\n", W, H, (double)PXQ);

    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"AlvoAnimadoFluxo";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) { fprintf(stderr, "[alvo] RegisterClassExW falhou %lu\n", GetLastError()); return 7; }
    fprintf(stderr, "[alvo] classe registrada\n");

    RECT r{ 0, 0, W, H };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"ZEBRA7", WS_OVERLAPPEDWINDOW,
                                POSX, POSY, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { fprintf(stderr, "[alvo] CreateWindowExW falhou %lu\n", GetLastError()); return 1; }
    ShowWindow(hwnd, SW_SHOW);
    fprintf(stderr, "[alvo] janela criada hwnd=%p\n", (void*)hwnd);

    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain1* sc = nullptr;
    D3D_FEATURE_LEVEL fl{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &dev, &fl, &ctx))) return 2;

    IDXGIDevice* dxgiDev = nullptr; IDXGIAdapter* ada = nullptr; IDXGIFactory2* fac = nullptr;
    dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev);
    if (dxgiDev) dxgiDev->GetAdapter(&ada);
    if (ada) ada->GetParent(__uuidof(IDXGIFactory2), (void**)&fac);
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = (UINT)W; sd.Height = (UINT)H; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (!fac || FAILED(fac->CreateSwapChainForHwnd(dev, hwnd, &sd, nullptr, nullptr, &sc))) return 3;
    Rel(fac); Rel(ada); Rel(dxgiDev);

    ID3D11Texture2D* bb = nullptr; ID3D11RenderTargetView* rtv = nullptr;
    sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    dev->CreateRenderTargetView(bb, nullptr, &rtv);
    Rel(bb);

    // A textura: duas vezes a largura da janela, para a translacao correr no wrap. No modo
    // imagem ela e a propria imagem, 1:1 com a janela, e nada desliza.
    const int TW = modoImg ? imgW : W * 2, TH = modoImg ? imgH : H;
    std::vector<uint8_t> px = modoImg ? std::move(imgPx) : GerarTextura(TW, TH);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)TW; td.Height = (UINT)TH; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sr{}; sr.pSysMem = px.data(); sr.SysMemPitch = (UINT)TW * 4;
    ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, &sr, &tex))) return 4;
    dev->CreateShaderResourceView(tex, nullptr, &srv);

    PFN_D3DCOMPILE comp = Compilador();
    if (!comp) return 5;
    ID3DBlob *bvs = nullptr, *bps = nullptr, *err = nullptr;
    comp(kVS, strlen(kVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &bvs, &err); Rel(err);
    comp(kPS, strlen(kPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &bps, &err); Rel(err);
    if (!bvs || !bps) return 6;
    ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr;
    dev->CreateVertexShader(bvs->GetBufferPointer(), bvs->GetBufferSize(), nullptr, &vs);
    dev->CreatePixelShader(bps->GetBufferPointer(), bps->GetBufferSize(), nullptr, &ps);
    Rel(bvs); Rel(bps);

    D3D11_SAMPLER_DESC smd{};
    smd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smd.AddressU = smd.AddressV = smd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    smd.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11SamplerState* smp = nullptr; dev->CreateSamplerState(&smd, &smp);

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer* cb = nullptr; dev->CreateBuffer(&bd, nullptr, &cb);

    // A janela mostra METADE da textura (escala 0.5 em u): o resto e a pista por onde a
    // imagem desliza. `desloc` anda PXQ pixels por quadro, convertido para UV da textura.
    const float passoU = PXQ / (float)TW;
    float desloc = 0.0f;
    unsigned quadros = 0;
    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
    LARGE_INTEGER t0; QueryPerformanceCounter(&t0);

    wchar_t titulo[128];
    MSG msg{};
    while (!g_sair) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_sair = true; break; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (g_sair) break;

        // RESIZE (A5, 15/09/2026, para o T6 da ponte integrada): a janela mudou de tamanho ->
        // o swapchain acompanha (ResizeBuffers). Ate aqui o alvo ignorava o tamanho novo e so
        // esticava o backbuffer de nascimento; o ReShade (e o dlss5-feed.addon32 dentro dele)
        // nunca via resolucao nova, entao nao havia Build novo e a ponte nao tinha o que
        // renegociar. Com o ResizeBuffers o ReShade refaz o runtime, o add-on manda 'B' de novo
        // e o host renegocia (T6). A conferencia e por GetClientRect a cada volta: barata, e
        // dispensa tratar WM_SIZE no Proc (que nao tem acesso ao swapchain).
        {
            RECT cr{}; GetClientRect(hwnd, &cr);
            const int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
            if (cw > 0 && ch > 0 && (cw != W || ch != H)) {
                ctx->OMSetRenderTargets(0, nullptr, nullptr);
                Rel(rtv);
                const HRESULT hr = sc->ResizeBuffers(0, (UINT)cw, (UINT)ch, DXGI_FORMAT_UNKNOWN, 0);
                if (FAILED(hr)) { fprintf(stderr, "[alvo] ResizeBuffers %dx%d falhou 0x%08lX\n", cw, ch, (unsigned long)hr); return 9; }
                sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
                dev->CreateRenderTargetView(bb, nullptr, &rtv); Rel(bb);
                fprintf(stderr, "[alvo] janela %dx%d -> %dx%d: swapchain refeito\n", W, H, cw, ch);
                W = cw; H = ch;
            }
        }

        struct { float du, dv, eu, ev; } p{ desloc, 0.0f, modoImg ? 1.0f : 0.5f, 1.0f };
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, &p, sizeof(p)); ctx->Unmap(cb, 0);
        }
        D3D11_VIEWPORT vp{ 0.f, 0.f, (float)W, (float)H, 0.f, 1.f };
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->RSSetViewports(1, &vp);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &cb);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->PSSetSamplers(0, 1, &smp);
        ctx->Draw(3, 0);
        sc->Present(VSYNC ? 1 : 0, 0);   // preso ao vblank (padrao) ou destravado (medicao)

        desloc += passoU;
        if (desloc > 1.0f) desloc -= 1.0f;
        quadros++;

        // O titulo carrega a taxa real: quem le o log da camada precisa saber a que taxa
        // a FONTE andou, senao nao da para separar "a camada perdeu quadro" de "a fonte
        // nao entregou".
        if ((quadros % 60) == 0) {
            LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
            const double s = (double)(t1.QuadPart - t0.QuadPart) / (double)fq.QuadPart;
            swprintf(titulo, 128, L"ZEBRA7  -  %.1f fps  -  %.1f px/quadro  -  %u quadros",
                     60.0 / s, (double)PXQ, quadros);
            SetWindowTextW(hwnd, titulo);
            t0 = t1;
        }
    }

    Rel(cb); Rel(smp); Rel(ps); Rel(vs); Rel(srv); Rel(tex);
    Rel(rtv); Rel(sc);
    if (ctx) ctx->ClearState();
    Rel(ctx); Rel(dev);
    return 0;
}
