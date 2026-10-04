// =====================================================================================
//  DLSS5 For All - o ponteiro do mouse na pelicula
//
//  TRES JEITOS, e a pessoa escolhe no painel:
//    Esconder        - a captura vai sem ponteiro nenhum (bom para gravar).
//    Desenhar (padrao) - o ponteiro DO SISTEMA, com a forma que ele tem AGORA (seta,
//                      I-beam, mao, redimensionar) e com o HOTSPOT no lugar certo, e
//                      desenhado por nos na imagem.
//    Prender ao jogo - o de cima, mais ClipCursor no retangulo da janela da fonte
//                      enquanto a pelicula estiver em primeiro plano.
//
//  POR QUE DESENHAR EM VEZ DE DEIXAR O WGC COMPOR O CURSOR (IsCursorCaptureEnabled):
//    1. O cursor que o WGC compoe e o do INSTANTE DA COMPOSICAO, nao o de agora. Entre a
//       composicao e a nossa apresentacao passa pelo menos um vblank, e com a fluidez
//       ligada passam QUADROS GERADOS inteiros - nos quais o cursor ficaria congelado.
//       Desenhado por nos, ele esta sempre onde a mao da pessoa esta.
//    2. A duplicacao DXGI nao compoe cursor nenhum (o desktop vem sem ele), entao sem isto
//       o backend novo nao teria ponteiro.
//    3. A posicao e a forma sao lidas com GetCursorInfo, que e o estado real do sistema,
//       incluindo a forma que o JOGO escolheu.
//
//  ONDE ELE E DESENHADO: na TEXTURA DA CAPTURA, logo depois do recorte. E antes da rede,
//  entao com a reconstrucao ligada o ponteiro passa por ela junto com a imagem. O caminho
//  "depois da rede" existe e e o do add-on (addon\ponteiro_desenhado.h, usado pelo modo
//  Ponteiro); este aqui e o que funciona nos dois backends sem add-on nenhum.
//
//  A TEXTURA DO CURSOR: cursor do Windows nao e um PNG. Ele tem mascara AND e mascara XOR,
//  e ha cursores monocromaticos em que o XOR INVERTE o fundo. O jeito que funciona para
//  todos e desenhar o mesmo cursor DUAS VEZES, uma sobre preto e outra sobre branco, e
//  derivar o alfa da diferenca: onde o fundo passou inteiro (branco - preto == 255) o pixel
//  e transparente; onde nao passou nada, e opaco. A cor sai do desenho sobre preto.
//
//  Depende de Log(const char*, ...).
// =====================================================================================
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cmath>

static void Log(const char* fmt, ...);

namespace Ponteiro {

enum { MODO_ESCONDER = 0, MODO_DESENHAR = 1, MODO_PRENDER = 2 };

// O padrao e DESENHAR: e o unico dos tres em que a pessoa ve para onde esta apontando sem
// ter de saber que existe uma opcao.
inline int g_modo = MODO_DESENHAR;

/* RODADA8 (cursor proprio, PESQ-CURSOR-2309): com os cursores de sistema trocados pelo vazio (main.cpp CursorSoOcultar, receita
   do Magpie) o handle COMPARTILHADO continua o mesmo - GetCursorInfo segue dizendo "seta" -, mas o DESENHO dele passou a ser o
   vazio. O main copia cada um (CopyCursor) ANTES da troca e registra aqui o par (compartilhado -> copia); o desenho abaixo
   desenha sempre a copia quando ela existe. A textura continua indexada pelo handle compartilhado. */
inline SRWLOCK g_origLk = SRWLOCK_INIT;
inline HCURSOR g_origH[16] = {}, g_origC[16] = {};
inline int     g_origN = 0;
inline void RegistrarOriginal(HCURSOR compartilhado, HCURSOR copia) {
    if (!compartilhado || !copia) return;
    AcquireSRWLockExclusive(&g_origLk);
    int i = 0; for (; i < g_origN; i++) if (g_origH[i] == compartilhado) break;
    if (i < g_origN) g_origC[i] = copia;
    else if (g_origN < 16) { g_origH[g_origN] = compartilhado; g_origC[g_origN] = copia; g_origN++; }
    ReleaseSRWLockExclusive(&g_origLk);
}
inline HCURSOR ParaDesenhar(HCURSOR h) {
    HCURSOR r = h;
    AcquireSRWLockShared(&g_origLk);
    for (int i = 0; i < g_origN; i++) if (g_origH[i] == h) { r = g_origC[i]; break; }
    ReleaseSRWLockShared(&g_origLk);
    return r;
}

typedef HRESULT(WINAPI* PFN_CompileCur)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                        ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

static const char* kShaderPonteiro = R"(
Texture2D    Cur : register(t0);
SamplerState Smp : register(s0);
cbuffer CurCB : register(b0)
{
    float4 Rect;     // x, y, largura, altura do ponteiro em coordenadas de clip (-1..1)
    float  Linear;   // 1 = destino em scRGB linear (HDR): converter de sRGB
    float  Escala;   // paper white / 80: o branco do ponteiro no brilho do monitor
    float2 Pad;
};
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VS(uint id : SV_VertexID)
{
    // dois triangulos sem vertex buffer: 0,1,2,3 viram o retangulo do ponteiro
    float2 q = float2((id == 1 || id == 3) ? 1.0 : 0.0, (id >= 2) ? 1.0 : 0.0);
    VSOut o;
    o.uv  = q;
    o.pos = float4(Rect.x + q.x * Rect.z, Rect.y - q.y * Rect.w, 0.0, 1.0);
    return o;
}

float3 SrgbParaLinear(float3 c)
{
    return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float4 PS(VSOut i) : SV_Target
{
    float4 c = Cur.Sample(Smp, i.uv);
    if (c.a <= 0.003) discard;
    if (Linear > 0.5) c.rgb = SrgbParaLinear(c.rgb) * Escala;
    return c;
}
)";

// Uma textura de ponteiro, com a forma e o hotspot que ela tinha quando foi montada.
struct Forma {
    HCURSOR           handle = nullptr;
    ID3D11Texture2D*  tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0, hotX = 0, hotY = 0;
};

struct Desenho {
    ID3D11Device*        dev = nullptr;
    ID3D11VertexShader*  vs = nullptr;
    ID3D11PixelShader*   ps = nullptr;
    ID3D11SamplerState*  samp = nullptr;
    ID3D11BlendState*    blend = nullptr;
    ID3D11Buffer*        cb = nullptr;
    Forma                forma;
    ID3D11RenderTargetView* rtv = nullptr;   // o RTV da textura de captura, cacheado
    ID3D11Texture2D*     rtvDe = nullptr;    // de qual textura ele e (para refazer no resize)
    bool                 falhou = false;     // ja tentamos montar e nao deu: nao insistir

    bool Montar(ID3D11Device* d) {
        if (falhou) return false;
        if (vs && ps && samp && blend && cb && dev == d) return true;
        Soltar();
        dev = d;
        if (!dev) return false;
        HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
        PFN_CompileCur compile = dc ? (PFN_CompileCur)(void*)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!compile) { falhou = true; Log("ponteiro: d3dcompiler_47.dll indisponivel - sem ponteiro desenhado"); return false; }
        ID3DBlob *vsb = nullptr, *psb = nullptr, *eb = nullptr;
        HRESULT hr = compile(kShaderPonteiro, strlen(kShaderPonteiro), "ponteiro", nullptr, nullptr,
                             "VS", "vs_4_0", 0, 0, &vsb, &eb);
        if (FAILED(hr)) { if (eb) eb->Release(); if (vsb) vsb->Release(); falhou = true; return false; }
        if (eb) { eb->Release(); eb = nullptr; }
        hr = compile(kShaderPonteiro, strlen(kShaderPonteiro), "ponteiro", nullptr, nullptr,
                     "PS", "ps_4_0", 0, 0, &psb, &eb);
        if (FAILED(hr)) { if (eb) eb->Release(); if (psb) psb->Release(); vsb->Release(); falhou = true; return false; }
        if (eb) { eb->Release(); eb = nullptr; }
        dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
        dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
        vsb->Release(); psb->Release();
        if (!vs || !ps) { falhou = true; return false; }

        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;   // ponteiro 1:1: nada de borrar a seta
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        dev->CreateSamplerState(&sd, &samp);

        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        dev->CreateBlendState(&bd, &blend);

        D3D11_BUFFER_DESC cbd{};
        cbd.ByteWidth = 32;   // float4 + float + float + float2
        cbd.Usage = D3D11_USAGE_DYNAMIC; cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&cbd, nullptr, &cb);

        if (!samp || !blend || !cb) { falhou = true; return false; }
        Log("ponteiro: desenho proprio montado (forma e hotspot do sistema, sobre a captura)");
        return true;
    }

    void SoltarForma() {
        if (forma.srv) { forma.srv->Release(); forma.srv = nullptr; }
        if (forma.tex) { forma.tex->Release(); forma.tex = nullptr; }
        forma.handle = nullptr; forma.w = forma.h = 0;
    }

    void Soltar() {
        SoltarForma();
        if (rtv)   { rtv->Release();   rtv = nullptr; }
        rtvDe = nullptr;
        if (cb)    { cb->Release();    cb = nullptr; }
        if (blend) { blend->Release(); blend = nullptr; }
        if (samp)  { samp->Release();  samp = nullptr; }
        if (ps)    { ps->Release();    ps = nullptr; }
        if (vs)    { vs->Release();    vs = nullptr; }
        dev = nullptr;
    }

    // Monta a textura do cursor `hc` (ver o bloco "A TEXTURA DO CURSOR" la em cima).
    bool GarantirForma(HCURSOR hc) {
        if (!hc || !dev) return false;
        if (forma.handle == hc && forma.srv) return true;
        SoltarForma();
        const HCURSOR hcKey = hc;
        hc = ParaDesenhar(hc);   // RODADA8: a copia original, se o cursor de sistema estiver trocado pelo vazio

        ICONINFO ii{};
        if (!GetIconInfo(hc, &ii)) return false;
        BITMAP bm{};
        int w = 0, h = 0;
        if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof(bm), &bm)) { w = bm.bmWidth; h = bm.bmHeight; }
        else if (ii.hbmMask && GetObjectW(ii.hbmMask, sizeof(bm), &bm)) {
            // cursor monocromatico: a mascara tem o dobro da altura (AND em cima, XOR embaixo)
            w = bm.bmWidth; h = bm.bmHeight / 2;
        }
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask)  DeleteObject(ii.hbmMask);
        if (w <= 0 || h <= 0 || w > 256 || h > 256) return false;

        HDC dcTela = GetDC(nullptr);
        HDC dcMem = CreateCompatibleDC(dcTela);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;   // top-down
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bitsPreto = nullptr; void* bitsBranco = nullptr;
        HBITMAP bmPreto  = CreateDIBSection(dcMem, &bi, DIB_RGB_COLORS, &bitsPreto, nullptr, 0);
        HBITMAP bmBranco = CreateDIBSection(dcMem, &bi, DIB_RGB_COLORS, &bitsBranco, nullptr, 0);
        bool ok = false;
        std::vector<unsigned char> px;
        if (bmPreto && bmBranco && bitsPreto && bitsBranco) {
            RECT r{ 0, 0, w, h };
            HGDIOBJ velho = SelectObject(dcMem, bmPreto);
            FillRect(dcMem, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
            DrawIconEx(dcMem, 0, 0, hc, w, h, 0, nullptr, DI_NORMAL);
            SelectObject(dcMem, bmBranco);
            FillRect(dcMem, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));
            DrawIconEx(dcMem, 0, 0, hc, w, h, 0, nullptr, DI_NORMAL);
            SelectObject(dcMem, velho);
            GdiFlush();

            const unsigned char* p = (const unsigned char*)bitsPreto;
            const unsigned char* b = (const unsigned char*)bitsBranco;
            px.resize((size_t)w * (size_t)h * 4);
            for (int i = 0; i < w * h; i++) {
                // o fundo que passou = quanto do branco sobrou por cima do preto
                int passou = (int)b[i * 4 + 0] - (int)p[i * 4 + 0];
                if (passou < 0) passou = 0;
                if (passou > 255) passou = 255;
                int a = 255 - passou;
                px[i * 4 + 0] = p[i * 4 + 0];   // B
                px[i * 4 + 1] = p[i * 4 + 1];   // G
                px[i * 4 + 2] = p[i * 4 + 2];   // R
                px[i * 4 + 3] = (unsigned char)a;
                if (a > 0 && a < 255) {
                    // desfaz a pre-multiplicacao que o desenho sobre preto deixou
                    for (int c = 0; c < 3; c++) {
                        int v = (int)px[i * 4 + c] * 255 / a;
                        px[i * 4 + c] = (unsigned char)(v > 255 ? 255 : v);
                    }
                }
            }
            ok = true;
        }
        if (bmPreto)  DeleteObject(bmPreto);
        if (bmBranco) DeleteObject(bmBranco);
        DeleteDC(dcMem);
        ReleaseDC(nullptr, dcTela);
        if (!ok) return false;

        D3D11_TEXTURE2D_DESC td{};
        td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sr{}; sr.pSysMem = px.data(); sr.SysMemPitch = (UINT)w * 4;
        if (FAILED(dev->CreateTexture2D(&td, &sr, &forma.tex)) || !forma.tex) return false;
        if (FAILED(dev->CreateShaderResourceView(forma.tex, nullptr, &forma.srv)) || !forma.srv) {
            forma.tex->Release(); forma.tex = nullptr; return false;
        }
        ICONINFO ii2{};
        if (GetIconInfo(hc, &ii2)) {
            forma.hotX = (int)ii2.xHotspot; forma.hotY = (int)ii2.yHotspot;
            if (ii2.hbmColor) DeleteObject(ii2.hbmColor);
            if (ii2.hbmMask)  DeleteObject(ii2.hbmMask);
        }
        forma.handle = hcKey; forma.w = w; forma.h = h;
        return true;
    }
};

inline Desenho g_des;

// -------------------------------------------------------------------------------------
//  RESET-CURSOR (23/09/2026): ONDE O PONTEIRO E DESENHADO. `[ultima] cursor_desenho`:
//    apresentacao (padrao) - o APRESENTADOR desenha o ponteiro por cima do quadro PRONTO, logo
//        antes do Present, com a posicao de GetCursorPos NAQUELE instante. Nao passa pela rede
//        e nao carrega a latencia dela (captura -> rede -> ponte -> fila da cadencia).
//    captura - o de antes: desenhado na textura da captura, antes da rede.
//  O relato do dono (teste de 23/09, YouTube 60 fps, modo monitor): "a seta do mouse sofre
//  influencia do neural render no modo monitor, dando a sensacao de que esta travada". Na
//  captura a seta passava pela rede (a rede temporal a borrava/segurava) e aparecia com o atraso
//  inteiro do caminho, e ainda so andava na taxa da FONTE (60/s num video de 60 fps).
//  Quando o apresentador esta de pe e desenhando, a captura NAO desenha (um ponteiro so).
// -------------------------------------------------------------------------------------
enum { DESENHO_CAPTURA = 0, DESENHO_APRESENTACAO = 1 };
inline std::atomic<int>       g_desenhoOnde{ DESENHO_APRESENTACAO };
inline std::atomic<ULONGLONG> g_apresDesenhaMs{ 0 };      // ultima vez que o apresentador tentou desenhar (GetTickCount64)
inline std::atomic<float>     g_paperWhiteNits{ 80.f };   // publicado pela captura (HDR); o apresentador so le
// sec. 27 (v3, item 9): O APRESENTADOR DE PE JA BASTA. Antes valia so "desenhou nos ultimos 500 ms": com a tela parada
// e o mouse parado nao ha Present nenhum, a marca vencia, e o PROXIMO quadro capturado (um video que volta a andar, um
// menu que abre) saia com a seta desenhada NA CAPTURA - ela passava pela rede e ficava "assada" na imagem, parada no
// meio do monitor, enquanto o apresentador desenhava a de verdade por cima: o "ponteiro que se perde" do relato do dono.
inline std::atomic<bool> g_apresVivo{ false };   // o apresentador (Iniciar .. Parar) esta de pe e e quem vai a tela
inline bool ApresentadorCuida() {
    if (g_desenhoOnde.load(std::memory_order_relaxed) != DESENHO_APRESENTACAO) return false;
    if (g_apresVivo.load(std::memory_order_relaxed)) return true;
    const ULONGLONG t = g_apresDesenhaMs.load(std::memory_order_relaxed);
    return t != 0 && GetTickCount64() - t < 500;
}

// -------------------------------------------------------------------------------------
//  O desenho, chamado pela captura logo depois do recorte.
//  `orgX/orgY` e o canto do recorte em coordenadas de TELA (e o mesmo `want` que a captura
//  usa); `destW/destH` e o tamanho da textura. `linear/escala` so valem em HDR.
// -------------------------------------------------------------------------------------
inline bool DesenharNaCaptura(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* dest,
                              UINT destW, UINT destH, LONG orgX, LONG orgY,
                              bool destinoLinear, float paperWhiteNits) {
    if (g_modo == MODO_ESCONDER) return false;
    if (!dev || !ctx || !dest || !destW || !destH) return false;
    g_paperWhiteNits.store(paperWhiteNits > 0.f ? paperWhiteNits : 80.f, std::memory_order_relaxed);
    if (ApresentadorCuida()) return false;   // RESET-CURSOR: quem desenha e o apresentador, depois da rede

    CURSORINFO ci{}; ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor) return false;

    if (!g_des.Montar(dev)) return false;
    if (!g_des.GarantirForma(ci.hCursor)) return false;

    // posicao do canto superior esquerdo do desenho, em pixeis da textura
    const float px = (float)(ci.ptScreenPos.x - orgX - g_des.forma.hotX);
    const float py = (float)(ci.ptScreenPos.y - orgY - g_des.forma.hotY);
    if (px + g_des.forma.w < 0 || py + g_des.forma.h < 0 ||
        px > (float)destW || py > (float)destH) return false;   // fora do recorte

    if (g_des.rtvDe != dest || !g_des.rtv) {
        if (g_des.rtv) { g_des.rtv->Release(); g_des.rtv = nullptr; }
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        D3D11_TEXTURE2D_DESC td{}; dest->GetDesc(&td);
        rd.Format = td.Format; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (FAILED(dev->CreateRenderTargetView(dest, &rd, &g_des.rtv)) || !g_des.rtv) {
            g_des.rtv = nullptr; g_des.falhou = true;
            Log("ponteiro: a textura da captura nao aceita alvo de render - sem ponteiro desenhado");
            return false;
        }
        g_des.rtvDe = dest;
    }

    // constantes: o retangulo em coordenadas de clip
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(ctx->Map(g_des.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return false;
    float* c = (float*)ms.pData;
    c[0] = (px / (float)destW) * 2.0f - 1.0f;              // x em clip
    c[1] = 1.0f - (py / (float)destH) * 2.0f;              // y em clip
    c[2] = ((float)g_des.forma.w / (float)destW) * 2.0f;   // largura em clip
    c[3] = ((float)g_des.forma.h / (float)destH) * 2.0f;   // altura em clip
    c[4] = destinoLinear ? 1.0f : 0.0f;
    c[5] = (paperWhiteNits > 0.f ? paperWhiteNits : 80.f) / 80.f;
    c[6] = c[7] = 0.f;
    ctx->Unmap(g_des.cb, 0);

    // O QUE ESTE DESENHO MEXE NO CONTEXTO fica guardado e volta: quem chama e o meio do
    // laco da captura, e o Renderer depois dele conta com o que ele mesmo deixou montado.
    ID3D11RenderTargetView* rtvVelho = nullptr;
    ID3D11DepthStencilView* dsvVelho = nullptr;
    ctx->OMGetRenderTargets(1, &rtvVelho, &dsvVelho);
    UINT nvp = 1; D3D11_VIEWPORT vpVelho{};
    ctx->RSGetViewports(&nvp, &vpVelho);

    D3D11_VIEWPORT vp{}; vp.Width = (float)destW; vp.Height = (float)destH; vp.MaxDepth = 1.f;
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, &g_des.rtv, nullptr);
    // A MISTURA TEM DE VOLTAR (VME-FLUXO, 20/09/2026). Este passe restaurava alvo e
    // viewport mas deixava o blend state LIGADO no contexto imediato, e quem desenhasse
    // depois herdava mistura alfa sem pedir. Foi assim que o campo de movimento do VME
    // zerou por inteiro: o passe de luma dele escreve num R8_UNORM e, com mistura ligada,
    // o resultado ficava no destino antigo - as duas NV12 iguais, campo exatamente zero
    // ("campo zerado 100%" na bancada, com o fundo inteiro andando).
    ID3D11BlendState* blendVelho = nullptr; float bfVelho[4]{}; UINT mascaraVelha = 0xFFFFFFFFu;
    ctx->OMGetBlendState(&blendVelho, bfVelho, &mascaraVelha);
    const float bf[4] = { 0, 0, 0, 0 };
    ctx->OMSetBlendState(g_des.blend, bf, 0xFFFFFFFF);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->VSSetShader(g_des.vs, nullptr, 0);
    ctx->PSSetShader(g_des.ps, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &g_des.cb);
    ctx->PSSetConstantBuffers(0, 1, &g_des.cb);
    ctx->PSSetShaderResources(0, 1, &g_des.forma.srv);
    ctx->PSSetSamplers(0, 1, &g_des.samp);
    ctx->Draw(4, 0);

    ID3D11ShaderResourceView* nulo = nullptr;
    ctx->PSSetShaderResources(0, 1, &nulo);
    ctx->OMSetRenderTargets(1, &rtvVelho, dsvVelho);
    if (nvp) ctx->RSSetViewports(1, &vpVelho);
    ctx->OMSetBlendState(blendVelho, bfVelho, mascaraVelha);
    if (blendVelho) blendVelho->Release();
    if (rtvVelho) rtvVelho->Release();
    if (dsvVelho) dsvVelho->Release();
    return true;
}

// -------------------------------------------------------------------------------------
//  RESET-CURSOR: O PONTEIRO NA APRESENTACAO. Chamado pelo apresentador (thread dele) no
//  Mostrar, depois de o quadro pronto ir para o backbuffer e logo antes do Present.
//  O backbuffer NAO e render target (BufferUsage de proposito, ver apresentador_gpu.h), entao a
//  receita e a do contador de fps: copia o recorte sob o ponteiro para um alvo pequeno, desenha
//  a seta nele com mistura alfa e devolve o recorte. Custa duas copias de ~32x32 e um Draw.
//  A MEDIDA ("latencia do ponteiro"): a posicao desenhada contra GetCursorPos lido de novo logo
//  depois do Present, e o tempo entre a leitura e o Present. Uma linha por segundo no log.
// -------------------------------------------------------------------------------------
struct NaApresentacao {
    Desenho d;                          // shaders/forma proprios, no device do APRESENTADOR
    ID3D11Texture2D*        alvo = nullptr;
    ID3D11RenderTargetView* alvoRtv = nullptr;
    int  alvoW = 0, alvoH = 0; DXGI_FORMAT alvoFmt = DXGI_FORMAT_UNKNOWN;
    bool desenhou = false;              // este quadro levou ponteiro (para a medida do DepoisDoPresent)
    bool posDentro = false;             // o ultimo quadro apresentado levou a seta (ela estava sobre a pelicula)
    unsigned nSoPonteiro = 0;           // re-apresentacoes so do ponteiro (tela parada) na janela de 1 s
    POINT posDesenhada{};               // em coordenadas de tela
    LARGE_INTEGER tLeitura{};           // QPC da leitura do GetCursorPos usada no desenho
    // janela de 1 s da medida
    ULONGLONG janIni = 0; unsigned nDes = 0, nMov = 0; std::vector<double> idades, desvios;
    bool avisouFalha = false, avisouLigado = false;

    void Soltar() {
        if (alvoRtv) { alvoRtv->Release(); alvoRtv = nullptr; }
        if (alvo) { alvo->Release(); alvo = nullptr; }
        alvoW = alvoH = 0; alvoFmt = DXGI_FORMAT_UNKNOWN;
        d.Soltar(); d.falhou = false;
    }

    bool Desenhar(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* bb, const D3D11_TEXTURE2D_DESC& bd, HWND janela) {
        desenhou = false;
        if (g_modo == MODO_ESCONDER || g_desenhoOnde.load(std::memory_order_relaxed) != DESENHO_APRESENTACAO) return false;
        if (!dev || !ctx || !bb || !janela || !bd.Width || !bd.Height) return false;
        g_apresDesenhaMs.store(GetTickCount64(), std::memory_order_relaxed);   // a captura para de desenhar
        if (!avisouLigado) {
            avisouLigado = true;
            Log("ponteiro: desenhado NA APRESENTACAO (cursor_desenho=apresentacao) - por cima do quadro pronto, posicao de GetCursorPos no instante do Present; nao passa pela rede. A captura deixa de desenhar enquanto o apresentador estiver de pe");
        }
        static int teste = -1; if (teste < 0) { char e[8]{}; teste = GetEnvironmentVariableA("DLSS5_PONTEIRO_TESTE", e, 8) > 0 ? atoi(e) : 0; }   // DIAGNOSTICO de bancada
        if (teste == 4) return false;
        CURSORINFO ci{}; ci.cbSize = sizeof(ci);
        if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor) return false;   // o jogo escondeu: nada a desenhar
        if (teste == 1) return false;
        POINT pos{};
        if (!GetCursorPos(&pos)) pos = ci.ptScreenPos;
        LARGE_INTEGER tl; QueryPerformanceCounter(&tl);
        // cliente da janela visivel em coordenadas de tela, e a escala cliente -> backbuffer
        RECT rc{};
        if (!GetClientRect(janela, &rc) || rc.right <= 0 || rc.bottom <= 0) return false;
        POINT org{ 0, 0 }; ClientToScreen(janela, &org);
        const float sx = (float)bd.Width / (float)rc.right, sy = (float)bd.Height / (float)rc.bottom;
        if (pos.x < org.x || pos.y < org.y || pos.x >= org.x + rc.right || pos.y >= org.y + rc.bottom) { posDentro = false; return false; }   // fora da pelicula
        if (!d.Montar(dev)) { if (!avisouFalha) { avisouFalha = true; Log("ponteiro: o desenho na apresentacao nao montou - sem ponteiro desenhado"); } return false; }
        if (!d.GarantirForma(ci.hCursor)) return false;
        const int fw = d.forma.w, fh = d.forma.h;
        const int px = (int)floorf((float)(pos.x - org.x) * sx) - d.forma.hotX;
        const int py = (int)floorf((float)(pos.y - org.y) * sy) - d.forma.hotY;
        // o recorte do backbuffer sob o ponteiro (cortado na borda)
        const int bx0 = (std::max)(px, 0), by0 = (std::max)(py, 0);
        const int bx1 = (std::min)(px + fw, (int)bd.Width), by1 = (std::min)(py + fh, (int)bd.Height);
        if (bx1 <= bx0 || by1 <= by0) return false;
        if (!alvo || alvoW != fw || alvoH != fh || alvoFmt != bd.Format) {
            if (alvoRtv) { alvoRtv->Release(); alvoRtv = nullptr; }
            if (alvo) { alvo->Release(); alvo = nullptr; }
            D3D11_TEXTURE2D_DESC ad{};
            ad.Width = (UINT)fw; ad.Height = (UINT)fh; ad.MipLevels = 1; ad.ArraySize = 1; ad.Format = bd.Format;
            ad.SampleDesc.Count = 1; ad.Usage = D3D11_USAGE_DEFAULT; ad.BindFlags = D3D11_BIND_RENDER_TARGET;
            if (FAILED(dev->CreateTexture2D(&ad, nullptr, &alvo)) || !alvo) { alvo = nullptr; return false; }
            if (FAILED(dev->CreateRenderTargetView(alvo, nullptr, &alvoRtv)) || !alvoRtv) { alvo->Release(); alvo = nullptr; alvoRtv = nullptr; return false; }
            alvoW = fw; alvoH = fh; alvoFmt = bd.Format;
        }
        D3D11_BOX caixa{ (UINT)bx0, (UINT)by0, 0, (UINT)bx1, (UINT)by1, 1 };
        ctx->CopySubresourceRegion(alvo, 0, (UINT)(bx0 - px), (UINT)(by0 - py), 0, bb, 0, &caixa);
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (FAILED(ctx->Map(d.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return false;
        float* c = (float*)ms.pData;
        c[0] = -1.f; c[1] = 1.f; c[2] = 2.f; c[3] = 2.f;   // o alvo inteiro e o ponteiro
        const bool linear = (bd.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
        c[4] = linear ? 1.f : 0.f;
        c[5] = g_paperWhiteNits.load(std::memory_order_relaxed) / 80.f;
        c[6] = c[7] = 0.f;
        ctx->Unmap(d.cb, 0);
        ID3D11RenderTargetView* rtvVelho = nullptr; ID3D11DepthStencilView* dsvVelho = nullptr;
        ctx->OMGetRenderTargets(1, &rtvVelho, &dsvVelho);
        UINT nvp = 1; D3D11_VIEWPORT vpVelho{}; ctx->RSGetViewports(&nvp, &vpVelho);
        ID3D11BlendState* blendVelho = nullptr; float bfVelho[4]{}; UINT mascaraVelha = 0xFFFFFFFFu;
        ctx->OMGetBlendState(&blendVelho, bfVelho, &mascaraVelha);
        D3D11_VIEWPORT vp{}; vp.Width = (float)fw; vp.Height = (float)fh; vp.MaxDepth = 1.f;
        ctx->RSSetViewports(1, &vp);
        ctx->OMSetRenderTargets(1, &alvoRtv, nullptr);
        const float bf[4] = { 0, 0, 0, 0 };
        ctx->OMSetBlendState(d.blend, bf, 0xFFFFFFFF);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ctx->VSSetShader(d.vs, nullptr, 0);
        ctx->PSSetShader(d.ps, nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, &d.cb);
        ctx->PSSetConstantBuffers(0, 1, &d.cb);
        ctx->PSSetShaderResources(0, 1, &d.forma.srv);
        ctx->PSSetSamplers(0, 1, &d.samp);
        ctx->Draw(4, 0);
        ID3D11ShaderResourceView* nulo = nullptr; ctx->PSSetShaderResources(0, 1, &nulo);
        ctx->OMSetRenderTargets(1, &rtvVelho, dsvVelho);
        if (nvp) ctx->RSSetViewports(1, &vpVelho);
        ctx->OMSetBlendState(blendVelho, bfVelho, mascaraVelha);
        if (blendVelho) blendVelho->Release();
        if (rtvVelho) rtvVelho->Release();
        if (dsvVelho) dsvVelho->Release();
        D3D11_BOX volta{ (UINT)(bx0 - px), (UINT)(by0 - py), 0, (UINT)(bx1 - px), (UINT)(by1 - py), 1 };
        if (teste != 2) ctx->CopySubresourceRegion(bb, 0, (UINT)bx0, (UINT)by0, 0, alvo, 0, &volta);
        desenhou = true; posDesenhada = pos; tLeitura = tl; posDentro = true;
        return true;
    }

    // Logo depois do Present: onde o ponteiro esta AGORA contra onde foi desenhado, e quanto
    // tempo passou entre a leitura e o fim do Present. 1 linha por segundo.
    void DepoisDoPresent() {
        const ULONGLONG ag = GetTickCount64();
        if (!janIni) janIni = ag;
        if (desenhou) {
            desenhou = false;
            POINT agora{};
            if (GetCursorPos(&agora)) {
                LARGE_INTEGER t1, f; QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f);
                const double ms = f.QuadPart ? 1000.0 * (double)(t1.QuadPart - tLeitura.QuadPart) / (double)f.QuadPart : 0.0;
                const double dx = (double)(agora.x - posDesenhada.x), dy = (double)(agora.y - posDesenhada.y);
                idades.push_back(ms); desvios.push_back(sqrt(dx * dx + dy * dy));
                nDes++;
                static POINT ult{ -99999, -99999 };
                if (posDesenhada.x != ult.x || posDesenhada.y != ult.y) nMov++;
                ult = posDesenhada;
            }
        }
        if (ag - janIni >= 1000) {
            if (nDes > 0) {
                auto pct = [](std::vector<double>& v, double p) { std::sort(v.begin(), v.end()); return v[(size_t)((v.size() - 1) * p)]; };
                const double i50 = pct(idades, 0.5), i95 = pct(idades, 0.95), d50 = pct(desvios, 0.5), d95 = pct(desvios, 0.95), dmax = desvios.back();
                Log("ponteiro(apresentacao): %u desenhos em 1 s (%u com posicao nova; %u re-apresentacoes so do ponteiro com a tela parada) | leitura->fim do Present p50 %.2f / p95 %.2f ms | "
                    "desvio posicao desenhada x GetCursorPos depois do Present p50 %.1f / p95 %.1f / max %.1f px",
                    nDes, nMov, nSoPonteiro, i50, i95, d50, d95, dmax);
            }
            janIni = ag; nDes = nMov = 0; nSoPonteiro = 0; idades.clear(); desvios.clear();
        }
    }
};

// -------------------------------------------------------------------------------------
//  PRENDER AO JOGO. ClipCursor no retangulo da janela da fonte enquanto a pelicula estiver
//  em primeiro plano. A REGRA DURA e soltar: um cursor preso sobre um app que perdeu o foco
//  (ou que caiu) e a pessoa sem mouse na maquina inteira. Por isso a soltura acontece em
//  toda transicao e o modulo lembra se ELE foi quem prendeu.
// -------------------------------------------------------------------------------------
inline bool g_preso = false;
inline RECT g_presoEm{};

inline void Soltar(const char* porque) {
    if (!g_preso) return;
    ClipCursor(nullptr);
    g_preso = false;
    g_presoEm = RECT{};
    Log("ponteiro: cursor solto (%s)", porque ? porque : "sem motivo dito");
}

// Chamar no laco de 1 s. `alvo` e a janela da fonte; `emPrimeiroPlano` diz se a pelicula
// (ou o jogo) esta na frente.
inline void Prender(HWND alvo, bool emPrimeiroPlano) {
    if (g_modo != MODO_PRENDER || !alvo || !IsWindow(alvo) || !emPrimeiroPlano) {
        Soltar(g_modo != MODO_PRENDER ? "modo mudou" : "a pelicula saiu da frente");
        return;
    }
    RECT r{};
    if (!GetClientRect(alvo, &r)) return;
    POINT tl{ r.left, r.top }, br{ r.right, r.bottom };
    ClientToScreen(alvo, &tl); ClientToScreen(alvo, &br);
    RECT alvoR{ tl.x, tl.y, br.x, br.y };
    if (alvoR.right - alvoR.left < 16 || alvoR.bottom - alvoR.top < 16) return;
    if (g_preso && memcmp(&alvoR, &g_presoEm, sizeof(RECT)) == 0) return;   // ja esta certo
    if (!ClipCursor(&alvoR)) return;
    g_preso = true; g_presoEm = alvoR;
    Log("ponteiro: cursor preso a janela da fonte %ldx%ld em %ld,%ld",
        (long)(alvoR.right - alvoR.left), (long)(alvoR.bottom - alvoR.top),
        (long)alvoR.left, (long)alvoR.top);
}

} // namespace Ponteiro
