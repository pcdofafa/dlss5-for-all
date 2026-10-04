/* =====================================================================
   tela.cpp -- C2 (contador de fps dentro do jogo) e E8 (modo de exibicao).
   Ver tela.h para o desenho.
   ===================================================================== */

#include "tela.h"
#include "../fg/fg_hook.h"   /* Fg::DonoDaApresentacao / PresentsNaTela: fps na tela com o FG */

#include <atomic>
#include <mutex>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Tela {
namespace {

LogFn L = nullptr;
void Log(const char* fmt, ...)
{
    if (!L) return;
    char b[400]; va_list ap; va_start(ap, fmt); _vsnprintf(b, sizeof b - 1, fmt, ap); va_end(ap); b[sizeof b - 1] = 0;
    L("%s", b);
}
template <class T> void Soltar(T*& p) { if (p) { p->Release(); p = nullptr; } }

int64_t Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
const int64_t kFreq = []{ LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart > 0 ? f.QuadPart : 1; }();

std::atomic<bool> parado{ false };
std::atomic<unsigned> versao{ 1 };

/* ------------------------------------------------------------------ controle (.ngx) */
FILETIME ctlMtime = {}; bool ctlLido = false;          /* thread da ponte */
std::atomic<bool> fpsLigado{ false };
unsigned long long ultimoPedido = 0;                    /* thread da ponte */
std::atomic<int> pedidoExib{ 0 };                       /* ponte -> jogo: 1 janela, 2 tela cheia */

void LerControle()
{
    wchar_t tmp[MAX_PATH] = L"";
    DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (k == 0 || k >= MAX_PATH) { tmp[0] = L'.'; tmp[1] = L'\\'; tmp[2] = 0; }
    wchar_t caminho[MAX_PATH];
    _snwprintf(caminho, MAX_PATH, L"%sdlss5-ponte.%lu.ngx", tmp, GetCurrentProcessId()); caminho[MAX_PATH - 1] = 0;
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    if (!GetFileAttributesExW(caminho, GetFileExInfoStandard, &fa)) return;
    if (ctlLido && CompareFileTime(&fa.ftLastWriteTime, &ctlMtime) == 0) return;
    FILE* f = _wfopen(caminho, L"rb");
    if (!f) return;
    ctlMtime = fa.ftLastWriteTime; ctlLido = true;
    bool fps = false; int exib = 0; unsigned long long pedido = 0;
    char linha[512];
    while (fgets(linha, sizeof linha, f))
    {
        char* eq = strchr(linha, '='); if (!eq) continue;
        *eq = 0; const char* chave = linha; char* v = eq + 1;
        char* fim = v + strlen(v); while (fim > v && (fim[-1] == '\r' || fim[-1] == '\n' || fim[-1] == ' ')) *--fim = 0;
        if (!strcmp(chave, "fps_tela")) fps = atoi(v) != 0;
        else if (!strcmp(chave, "exibicao")) exib = !strcmp(v, "tela_cheia") ? 2 : !strcmp(v, "janela") ? 1 : 0;
        else if (!strcmp(chave, "exibicao_pedido")) pedido = _strtoui64(v, nullptr, 10);
    }
    fclose(f);
    if (fpsLigado.exchange(fps) != fps) Log("tela: contador de fps %s", fps ? "ligado" : "desligado");
    /* um pedido por clique: o numero e o GetTickCount64 do clique; o arquivo e regravado por
       outras chaves com o mesmo numero, e um pedido de outra injecao (mais de 10 s) nao vale */
    if (pedido != 0 && pedido != ultimoPedido && exib != 0)
    {
        ultimoPedido = pedido;
        const ULONGLONG agora = GetTickCount64();
        if (agora >= pedido && agora - pedido < 10000) { pedidoExib = exib; Log("tela: pedido de exibicao %s", exib == 2 ? "tela_cheia" : "janela"); }
        else Log("tela: pedido de exibicao antigo ignorado");
    }
}

/* ------------------------------------------------------------------ modo de exibicao (E8) */
std::atomic<int>  exibAtual{ -1 };    /* -1 nao visto, 0 janela, 1 sem_bordas, 2 tela_cheia */
std::atomic<bool> okAtual{ false };   /* tela_cheia_ok publicado */
std::atomic<bool> recusado{ false };  /* um pedido de tela cheia nao pegou: a opcao some */
std::atomic<bool> releer{ false };

enum { PE_NADA = 0, PE_PRONTO, PE_POSTADO, PE_EXECUTANDO, PE_FEITO };
std::atomic<int> pedidoFase{ PE_NADA };
std::atomic<IDXGISwapChain*> pedidoSc{ nullptr };   /* AddRef do jogo; solto por quem executa ou desiste */
std::atomic<int> pedidoQuer{ 0 };
std::atomic<HWND> pedidoHwnd{ nullptr };
std::atomic<long> pedidoHr{ 0 };
std::atomic<ULONGLONG> verificarEm{ 0 };
std::atomic<int> verificarQuer{ 0 };
HHOOK gancho = nullptr; ULONGLONG postadoEm = 0;     /* thread da ponte */
std::atomic<UINT> msgPedido{ 0 };

const char* NomeExib(int e) { return e == 2 ? "tela_cheia" : e == 1 ? "sem_bordas" : e == 0 ? "janela" : "-"; }

/* thread da janela do jogo: o mesmo lugar em que o DXGI trata o Alt+Enter */
LRESULT CALLBACK ProcMsg(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && wp == PM_REMOVE && lp)
    {
        MSG* m = (MSG*)lp;
        const UINT nossa = msgPedido.load();
        if (nossa != 0 && m->message == nossa)
        {
            m->message = WM_NULL;   /* a janela do jogo nao ve a nossa mensagem */
            int esp = PE_POSTADO;
            if (pedidoFase.compare_exchange_strong(esp, PE_EXECUTANDO))
            {
                HRESULT hr = E_FAIL;
                if (IDXGISwapChain* sc = pedidoSc.exchange(nullptr))
                {
                    hr = sc->SetFullscreenState(pedidoQuer.load() == 2 ? TRUE : FALSE, nullptr);
                    sc->Release();
                }
                pedidoHr = (long)hr;
                pedidoFase = PE_FEITO;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void SoltarGancho() { if (gancho) { UnhookWindowsHookEx(gancho); gancho = nullptr; } }
void SoltarPedido()
{
    if (IDXGISwapChain* sc = pedidoSc.exchange(nullptr)) sc->Release();
    SoltarGancho();
    pedidoFase = PE_NADA;
}

/* ------------------------------------------------------------------ contador (C2) */
struct Layout
{
    UINT x = 0, y = 0, w = 0, h = 0, pitch = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    bool ok = false;
    ID3D12Resource* upload = nullptr;   /* so D3D12; vive ate Parar */
};
std::mutex muLay; Layout lay;          /* lida pelo ritmo do FG (GravarNaCopia) */

const UINT kMaxW = 360, kMaxH = 64, kMaxBpp = 8;
const UINT64 kBytes = (UINT64)((kMaxW * kMaxBpp + 3) & ~3u) * kMaxH;

/* thread do jogo */
IDXGISwapChain* scPrincipal = nullptr;
int64_t ultimoPrincipal = 0, janelaInicio = 0, proxExib = 0;
uint64_t nQuadros = 0, ptAnterior = 0;
int apiPrincipal = 0;                  /* 0 nao visto, 11, 12, -1 nenhuma */
int fpsJogo = 0, fpsTela = 0;
char txtFeito[32] = ""; UINT wFeito = 0, hFeito = 0; DXGI_FORMAT fmtFeito = DXGI_FORMAT_UNKNOWN;
std::vector<uint8_t> px;

std::atomic<ID3D12CommandQueue*> fila{ nullptr };
IDXGISwapChain* scFila = nullptr;      /* C11: swapchain de onde a fila ja foi tirada */
ID3D12Device* dev12 = nullptr;
ID3D12Resource* upload = nullptr; uint8_t* upMap = nullptr;
ID3D12CommandAllocator* alloc[3] = {}; uint64_t feito[3] = {}; UINT proxAlloc = 0;
ID3D12GraphicsCommandList* lista = nullptr;
ID3D12Fence* fence = nullptr; uint64_t fenceVal = 0;
bool falhou12 = false;

UINT Bpp(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM: return 4;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
    default: return 0;
    }
}

/* texto claro em caixa escura; em HDR (10 bits PQ / 16F scRGB) valores moderados para nao ofuscar */
void Pixel(uint8_t* p, DXGI_FORMAT f, bool texto)
{
    if (f == DXGI_FORMAT_R10G10B10A2_UNORM)
    {
        const uint32_t c = texto ? 720u : 40u;
        const uint32_t v = c | (c << 10) | (c << 20) | (3u << 30);
        memcpy(p, &v, 4);
    }
    else if (f == DXGI_FORMAT_R16G16B16A16_FLOAT)
    {
        const uint16_t c = texto ? 0x4100 : 0x251F;   /* 2,5 e 0,02 em meio float */
        const uint16_t v[4] = { c, c, c, 0x3C00 };
        memcpy(p, v, 8);
    }
    else
    {
        const uint8_t c = texto ? 240 : 16;
        p[0] = p[1] = p[2] = c; p[3] = 255;
    }
}

/* glifos 3x5, bit 14 = canto superior esquerdo */
const char     kLetras[] = "0123456789FPS>";
const uint16_t kGlifo[]  = { 0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF, 0x79A4, 0x6BA4, 0x388E, 0x4454 };

uint16_t Glifo(char c)
{
    for (int i = 0; kLetras[i]; ++i) if (kLetras[i] == c) return kGlifo[i];
    return 0;
}

bool CriarD12(IDXGISwapChain* sc)
{
    if (dev12 || falhou12) return dev12 != nullptr && lista != nullptr;
    ID3D12Resource* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D12Resource), (void**)&bb)) || !bb) { falhou12 = true; return false; }
    bb->GetDevice(__uuidof(ID3D12Device), (void**)&dev12);
    bb->Release();
    const char* erro = nullptr;
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = kBytes; rd.Height = 1;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (!dev12) erro = "sem device";
    else if (FAILED(dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), (void**)&upload)))
        erro = "buffer de upload";
    else
    {
        D3D12_RANGE nada = { 0, 0 };
        if (FAILED(upload->Map(0, &nada, (void**)&upMap)) || !upMap) erro = "Map do upload";
    }
    for (int i = 0; i < 3 && !erro; ++i)
        if (FAILED(dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&alloc[i]))) erro = "allocator";
    if (!erro && FAILED(dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&lista)))
        erro = "lista";
    if (!erro) { lista->Close(); lista->SetName(L"dlss5-contador-fps"); }
    if (!erro && FAILED(dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fence))) erro = "fence";
    if (erro)
    {
        Log("tela: contador D3D12 indisponivel (%s)", erro);
        falhou12 = true;
        Soltar(lista); for (auto*& a : alloc) Soltar(a); Soltar(fence);
        if (upload && upMap) upload->Unmap(0, nullptr);
        upMap = nullptr; Soltar(upload); Soltar(dev12);
        return false;
    }
    return true;
}

/* C11: a fila em que o swapchain apresenta (Fg::FilaDoSwapchain); sem ela fica a primeira fila
   DIRECT vista no ExecuteCommandLists, de reserva (tenta de novo a cada Preparar, avisa uma vez) */
void FilaDoSwapchain(IDXGISwapChain* sc)
{
    static bool avisou = false;
    if (sc == scFila) return;
    ID3D12CommandQueue* q = Fg::FilaDoSwapchain(sc);
    if (!q)
    {
        if (!avisou) { avisou = true; Log("tela: fila do swapchain nao encontrada; contador na primeira fila DIRECT vista (reserva)"); }
        return;
    }
    scFila = sc;
    ID3D12CommandQueue* velha = fila.exchange(q);
    Log("tela: fila do contador pelo swapchain=0x%p%s", (void*)q, (velha && velha != q) ? " (a primeira DIRECT vista era outra)" : "");
    if (velha) velha->Release();
}

/* monta a caixa do contador (thread do jogo); so reescreve quando texto/tamanho/formato mudam */
void Preparar(IDXGISwapChain* sc)
{
    DXGI_SWAP_CHAIN_DESC d = {};
    if (FAILED(sc->GetDesc(&d))) return;
    if (apiPrincipal == 0)
    {
        ID3D12Resource* r12 = nullptr; ID3D11Texture2D* r11 = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D12Resource), (void**)&r12)) && r12) { apiPrincipal = 12; r12->Release(); }
        else if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&r11)) && r11) { apiPrincipal = 11; r11->Release(); }
        else apiPrincipal = -1;
    }
    const DXGI_FORMAT fmt = d.BufferDesc.Format;
    const UINT bpp = Bpp(fmt), W = d.BufferDesc.Width, H = d.BufferDesc.Height;
    char txt[32];
    if (fpsTela > 0) snprintf(txt, sizeof txt, "%d>%d FPS", fpsJogo, fpsTela);
    else snprintf(txt, sizeof txt, "%d FPS", fpsJogo);
    if (!strcmp(txt, txtFeito) && W == wFeito && H == hFeito && fmt == fmtFeito) return;
    snprintf(txtFeito, sizeof txtFeito, "%s", txt); wFeito = W; hFeito = H; fmtFeito = fmt;

    Layout nova;
    const UINT n = (UINT)strlen(txt), pad = 2;
    UINT s = H / 360; if (s < 2) s = 2; if (s > 6) s = 6;   /* 1080p: glifo 9x15 px */
    while (s > 1 && (n * 4 - 1 + 2 * pad) * s > kMaxW) --s;
    const UINT w = (n * 4 - 1 + 2 * pad) * s, h = (5 + 2 * pad) * s, m = 4 * s;
    if (apiPrincipal == 12 && !CriarD12(sc)) apiPrincipal = -1;
    if (apiPrincipal == 12) FilaDoSwapchain(sc);   /* C11 */
    if (bpp == 0 || apiPrincipal < 0 || w > kMaxW || h > kMaxH || W < w + 2 * m || H < h + 2 * m)
    {
        std::lock_guard<std::mutex> g(muLay); lay = nova;   /* formato ou tamanho sem contador */
        return;
    }
    nova.x = W - w - m; nova.y = m; nova.w = w; nova.h = h; nova.fmt = fmt;
    nova.pitch = (w * bpp + 3) & ~3u;
    px.assign((size_t)nova.pitch * h, 0);
    for (UINT y = 0; y < h; ++y)
        for (UINT x = 0; x < w; ++x)
        {
            bool on = false;
            const UINT ux = x / s, uy = y / s;
            if (uy >= pad && uy < pad + 5 && ux >= pad)
            {
                const UINT i = (ux - pad) / 4, c = (ux - pad) % 4, r = uy - pad;
                if (i < n && c < 3) on = ((Glifo(txt[i]) >> (14 - (r * 3 + c))) & 1) != 0;
            }
            Pixel(&px[(size_t)y * nova.pitch + (size_t)x * bpp], fmt, on);
        }
    if (apiPrincipal == 12)
    {
        memcpy(upMap, px.data(), px.size());
        nova.upload = upload;
    }
    nova.ok = true;
    std::lock_guard<std::mutex> g(muLay); lay = nova;
}

void Copiar(ID3D12GraphicsCommandList* l, ID3D12Resource* bb, const Layout& ly)
{
    D3D12_TEXTURE_COPY_LOCATION dst = {}; dst.pResource = bb; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src = {}; src.pResource = ly.upload; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Format = ly.fmt; src.PlacedFootprint.Footprint.Width = ly.w;
    src.PlacedFootprint.Footprint.Height = ly.h; src.PlacedFootprint.Footprint.Depth = 1; src.PlacedFootprint.Footprint.RowPitch = ly.pitch;
    l->CopyTextureRegion(&dst, ly.x, ly.y, 0, &src, nullptr);
}

/* o bb e do mesmo device, no formato e com tamanho para a caixa */
bool Cabe12(ID3D12Resource* bb, const Layout& ly)
{
    const D3D12_RESOURCE_DESC rd = bb->GetDesc();
    if (rd.Format != ly.fmt || rd.SampleDesc.Count != 1 || rd.Width < (UINT64)ly.x + ly.w || rd.Height < ly.y + ly.h) return false;
    ID3D12Device* d = nullptr;
    bb->GetDevice(__uuidof(ID3D12Device), (void**)&d);
    const bool mesmo = (d != nullptr && d == dev12);
    Soltar(d);
    return mesmo;
}

void DesenharD12(IDXGISwapChain* sc, const Layout& ly)
{
    ID3D12CommandQueue* q = fila.load();
    if (!q || !lista || !ly.upload) return;
    IDXGISwapChain3* sc3 = nullptr;
    if (FAILED(sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3)) || !sc3) return;
    const UINT idx = sc3->GetCurrentBackBufferIndex();
    sc3->Release();
    ID3D12Resource* bb = nullptr;
    if (FAILED(sc->GetBuffer(idx, __uuidof(ID3D12Resource), (void**)&bb)) || !bb) return;
    const UINT k = proxAlloc; proxAlloc = (k + 1) % 3;
    /* C4: nunca Reset com a lista ainda na GPU; sem esperar: so pula este quadro */
    if (Cabe12(bb, ly) && fence->GetCompletedValue() >= feito[k] &&
        SUCCEEDED(alloc[k]->Reset()) && SUCCEEDED(lista->Reset(alloc[k], nullptr)))
    {
        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = bb;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        lista->ResourceBarrier(1, &b);
        Copiar(lista, bb, ly);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        lista->ResourceBarrier(1, &b);
        if (SUCCEEDED(lista->Close()))
        {
            ID3D12CommandList* ls[1] = { lista };
            q->ExecuteCommandLists(1, ls);
            q->Signal(fence, ++fenceVal);
            feito[k] = fenceVal;
        }
    }
    bb->Release();
}

void DesenharD11(IDXGISwapChain* sc, const Layout& ly)
{
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) return;
    D3D11_TEXTURE2D_DESC td = {}; bb->GetDesc(&td);
    if (td.Format == ly.fmt && td.SampleDesc.Count == 1 && td.Width >= ly.x + ly.w && td.Height >= ly.y + ly.h &&
        px.size() >= (size_t)ly.pitch * ly.h)
    {
        ID3D11Device* d = nullptr; bb->GetDevice(&d);
        ID3D11DeviceContext* c = nullptr; if (d) d->GetImmediateContext(&c);
        if (c)
        {
            D3D11_BOX box = { ly.x, ly.y, 0, ly.x + ly.w, ly.y + ly.h, 1 };
            c->UpdateSubresource(bb, 0, &box, px.data(), ly.pitch, 0);
        }
        Soltar(c); Soltar(d);
    }
    bb->Release();
}

/* E8: modo atual e se a tela cheia exclusiva e aceita (thread do jogo) */
void LerExibicao(IDXGISwapChain* sc)
{
    DXGI_SWAP_CHAIN_DESC d = {};
    if (FAILED(sc->GetDesc(&d))) return;
    BOOL fs = FALSE;
    const bool fsOk = SUCCEEDED(sc->GetFullscreenState(&fs, nullptr));
    const HWND h = d.OutputWindow;
    int ex = fs ? 2 : 0;
    if (!fs && h) ex = ((GetWindowLongPtrW(h, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) ? 0 : 1;
    const bool ok = fsOk && h && GetAncestor(h, GA_ROOT) == h && !recusado.load();
    const int exAntes = exibAtual.exchange(ex);
    const bool okAntes = okAtual.exchange(ok);
    if (exAntes != ex || okAntes != ok) { ++versao; Log("tela: exibicao=%s tela_cheia_ok=%d", NomeExib(ex), ok ? 1 : 0); }

    ULONGLONG quando = verificarEm.load();
    if (quando != 0 && GetTickCount64() >= quando && verificarEm.compare_exchange_strong(quando, 0))
    {
        const int quer = verificarQuer.load();
        if (quer == 2 && ex != 2)
        {
            recusado = true; okAtual = false; ++versao;   /* sem laco: nao tenta de novo */
            Log("tela: o jogo nao ficou em tela cheia; a opcao some nesta sessao");
        }
        else Log("tela: exibicao pedida confirmada (%s)", NomeExib(ex));
    }
}

void IniciarPedido(IDXGISwapChain* sc, int quer)
{
    BOOL fs = FALSE;
    if (FAILED(sc->GetFullscreenState(&fs, nullptr))) { Log("tela: pedido de exibicao sem GetFullscreenState; ignorado"); return; }
    if ((quer == 2) == (fs != FALSE)) { Log("tela: pedido de exibicao ja atendido"); return; }
    if (quer == 2 && !okAtual.load()) { Log("tela: tela cheia pedida, mas o jogo nao aceita; ignorado"); return; }
    DXGI_SWAP_CHAIN_DESC d = {};
    if (FAILED(sc->GetDesc(&d)) || !d.OutputWindow) return;
    int esp = PE_NADA;
    if (!pedidoFase.compare_exchange_strong(esp, PE_PRONTO)) { Log("tela: pedido anterior ainda em curso; ignorado"); return; }
    sc->AddRef();
    pedidoSc = sc; pedidoQuer = quer; pedidoHwnd = d.OutputWindow;
}

} // namespace anonimo

/* ------------------------------------------------------------------ API */
void Iniciar(LogFn log) { L = log; }

void FilaVista(ID3D12CommandQueue* q)
{
    if (!q || fila.load()) return;
    if (q->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
    q->AddRef();
    ID3D12CommandQueue* nada = nullptr;
    if (!fila.compare_exchange_strong(nada, q)) q->Release();
}

void Servico()
{
    if (parado.load()) return;
    LerControle();
    const int fase = pedidoFase.load();
    if (fase == PE_PRONTO)
    {
        const HWND h = pedidoHwnd.load();
        const DWORD tid = h ? GetWindowThreadProcessId(h, nullptr) : 0;
        if (!msgPedido.load()) msgPedido = RegisterWindowMessageW(L"dlss5-ponte-exibicao");
        if (tid) gancho = SetWindowsHookExW(WH_GETMESSAGE, ProcMsg, nullptr, tid);
        /* POSTADO antes do PostMessage: a janela pode tratar a mensagem antes de voltarmos */
        pedidoFase = PE_POSTADO; postadoEm = GetTickCount64();
        if (!gancho || !msgPedido.load() || !PostMessageW(h, msgPedido.load(), 0, 0))
        {
            int esp = PE_POSTADO;
            if (pedidoFase.compare_exchange_strong(esp, PE_NADA))
            {
                Log("tela: nao consegui chegar a thread da janela do jogo (%lu)", GetLastError());
                SoltarPedido();
            }
        }
    }
    else if (fase == PE_POSTADO && GetTickCount64() - postadoEm > 3000)
    {
        int esp = PE_POSTADO;
        if (pedidoFase.compare_exchange_strong(esp, PE_NADA))
        {
            Log("tela: a janela do jogo nao tratou o pedido em 3 s; desisti (sem nova tentativa)");
            SoltarPedido();
        }
    }
    else if (fase == PE_FEITO)
    {
        SoltarGancho();
        const int quer = pedidoQuer.load();
        const HRESULT hr = (HRESULT)pedidoHr.load();
        Log("tela: SetFullscreenState(%d) na thread da janela -> 0x%08lX", quer == 2 ? 1 : 0, (unsigned long)hr);
        if (FAILED(hr) && quer == 2) { recusado = true; okAtual = false; ++versao; Log("tela: o jogo recusou a tela cheia; a opcao some nesta sessao"); }
        else { verificarQuer = quer; verificarEm = GetTickCount64() + 3000; releer = true; }
        pedidoFase = PE_NADA;
    }
}

void TelaCheiaMudou() { releer = true; }

void AoApresentar(IDXGISwapChain* sc)
{
    if (parado.load() || !sc) return;
    const int64_t agora = Qpc();
    if (sc != scPrincipal)
    {
        /* o jogo pode ter mais de um swapchain: fica no que apresenta; troca se o antigo parou ha 1 s */
        if (scPrincipal && agora - ultimoPrincipal < kFreq) return;
        scPrincipal = sc; nQuadros = 0; janelaInicio = agora; apiPrincipal = 0; releer = true;
        txtFeito[0] = 0; ptAnterior = Fg::PresentsNaTela();
        { std::lock_guard<std::mutex> g(muLay); lay.ok = false; }
    }
    ultimoPrincipal = agora;
    ++nQuadros;

    if (const int pe = pedidoExib.exchange(0)) IniciarPedido(sc, pe);
    if (releer.exchange(false) || agora >= proxExib) { proxExib = agora + kFreq / 2; LerExibicao(sc); }

    if (agora - janelaInicio >= kFreq / 2)
    {
        const double seg = double(agora - janelaInicio) / double(kFreq);
        const uint64_t pt = Fg::PresentsNaTela();
        fpsJogo = (int)(nQuadros / seg + 0.5);
        fpsTela = Fg::DonoDaApresentacao() ? (int)((pt - ptAnterior) / seg + 0.5) : 0;
        if (fpsJogo > 9999) fpsJogo = 9999;
        if (fpsTela > 9999) fpsTela = 9999;
        ptAnterior = pt; nQuadros = 0; janelaInicio = agora;
        if (fpsLigado.load()) Preparar(sc);
    }
}

void Desenhar(IDXGISwapChain* sc)
{
    if (parado.load() || !fpsLigado.load() || sc != scPrincipal) return;
    Layout ly; { std::lock_guard<std::mutex> g(muLay); ly = lay; }
    if (!ly.ok) return;
    if (apiPrincipal == 12) DesenharD12(sc, ly);
    else if (apiPrincipal == 11) DesenharD11(sc, ly);
}

void GravarNaCopia(ID3D12GraphicsCommandList* l, ID3D12Resource* bb)
{
    if (parado.load() || !fpsLigado.load() || !l || !bb) return;
    try
    {
        Layout ly; { std::lock_guard<std::mutex> g(muLay); ly = lay; }
        if (!ly.ok || !ly.upload || !Cabe12(bb, ly)) return;
        Copiar(l, bb, ly);
    }
    catch (...) {}
}

unsigned EstadoVersao() { return versao.load(); }

void EstadoTexto(char* buf, size_t n)
{
    if (!n) return;
    snprintf(buf, n, "exibicao=%s\ntela_cheia_ok=%d\n", NomeExib(exibAtual.load()), okAtual.load() ? 1 : 0);
}

void Parar()
{
    parado = true;
    /* SetFullscreenState em curso na thread da janela (codigo nosso): espera sair antes de descarregar */
    for (int i = 0; i < 300 && pedidoFase.load() == PE_EXECUTANDO; ++i) Sleep(10);
    SoltarGancho();
    if (IDXGISwapChain* sc = pedidoSc.exchange(nullptr)) sc->Release();
    Sleep(50);   /* um Present em voo sai do Desenhar */
    { std::lock_guard<std::mutex> g(muLay); lay = Layout(); }
    if (fence && fila.load()) { /* a GPU pode ter a ultima copia na fila: espera curta antes de soltar */
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (ev && fence->GetCompletedValue() < fenceVal && SUCCEEDED(fence->SetEventOnCompletion(fenceVal, ev))) WaitForSingleObject(ev, 1000);
        if (ev) CloseHandle(ev);
    }
    Soltar(lista); for (auto*& a : alloc) Soltar(a); Soltar(fence);
    if (upload && upMap) upload->Unmap(0, nullptr);
    upMap = nullptr; Soltar(upload); Soltar(dev12);
    if (ID3D12CommandQueue* q = fila.exchange(nullptr)) q->Release();
}

} // namespace Tela
