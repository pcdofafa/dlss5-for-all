/* =====================================================================
   fg_hook.cpp -- FASE 4 (Q4): frame generation D3D12 dentro do jogo.
   Ver fg_hook.h para o desenho. Referencia canonica: AMD
   FrameInterpolationSwapchainDX12 (MIT) -- proxy de backbuffers via GetBuffer,
   thread de ritmo apresentando N quadros por Present, espera por QPC entre
   eles. Aqui o motor de interpolacao e o mesmo FidelityFX 3.1 (ffx-api,
   FFX_API_EFFECT_ID_FRAMEGENERATION), como o modo Universal ja usa em
   src\fg_ffx.h, so que rodando na fila e no device DO JOGO.
   ===================================================================== */

#include "fg_hook.h"

#include <dxgi1_5.h>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>

#include "../ponte_seh.h"          /* J4: a guarda do I10 na thread de ritmo (e nossa; um estouro nela mataria o jogo) */
#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_framegeneration.h"
#include "ffx_api/dx12/ffx_api_dx12.h"
#include "../ngx/ngx_upstream.h"   /* Ngx::Up::Ativo(): ha MV/depth pela sessao .up? */
#include "../tela/tela.h"           /* C2: contador de fps nos quadros que o ritmo apresenta */

namespace Fg {

/* Desmontar tem ligacao externa (chamada tanto pela API publica quanto por
   ajudantes internos); declarada aqui, no escopo de Fg, para ser visivel dos
   dois lados sem duplicar a definicao. */
void Desmontar();

namespace {

LogFn L = nullptr;
EstouroFn E = nullptr;   /* J4: registro no erros-nativo.log da ponte (pode ser nulo) */

typedef HRESULT (STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE* GetBufferFn)(IDXGISwapChain*, UINT, REFIID, void**);
typedef UINT    (STDMETHODCALLTYPE* GetCurBBFn)(IDXGISwapChain3*);

/* ------------------------------------------------------------------ util */
int64_t Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
int64_t g_qpcFreq = []{ LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
double  Ms(int64_t de, int64_t ate) { return 1000.0 * double(ate - de) / double(g_qpcFreq); }
template <class T> void Soltar(T*& p) { if (p) { p->Release(); p = nullptr; } }
void FecharH(HANDLE& h) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = nullptr; }

DXGI_FORMAT Tipado(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

D3D12_RESOURCE_BARRIER Barr(ID3D12Resource* r, D3D12_RESOURCE_STATES de, D3D12_RESOURCE_STATES para)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = de; b.Transition.StateAfter = para;
    return b;
}

/* Espera precisa por QPC (spin com Sleep quando ha folga), portada do
   waitForPerformanceCount da AMD (FrameInterpolationSwapchainDX12_Helpers.cpp). */
void EsperarQpc(int64_t alvo)
{
    int64_t agora = Qpc();
    if (agora >= alvo) return;
    double ms = 1000.0 * double(alvo - agora) / double(g_qpcFreq);
    while (ms > 2.0)   /* dorme com folga de ~2 ms para nao passar do ponto */
    {
        timeBeginPeriod(1);
        Sleep((DWORD)(ms - 1.5));
        timeEndPeriod(1);
        agora = Qpc();
        ms = 1000.0 * double(alvo - agora) / double(g_qpcFreq);
    }
    while (Qpc() < alvo) { /* spin final */ }
}

/* ------------------------------------------------------------------ estado */
const UINT kMaxBuf = 8;

/* J4 (18/09/2026): A SONDA DE LUMA - corte de cena e imagem parada sem depender do jogo.
   O que ela e: a cada quadro do jogo, a lista de geracao copia kSondaFaixas faixas de
   kSondaLinhas linhas inteiras do quadro (espalhadas pela altura) para um buffer de leitura;
   a thread de ritmo, que JA espera a GPU terminar esse quadro antes de apresentar o gerado,
   le as faixas ali e decide DUAS coisas, a tempo de segurar o quadro gerado deste mesmo par:
     - IMAGEM PARADA: os bytes das faixas sao identicos aos do quadro anterior (hash). Entre dois
       quadros iguais o interpolado so pode ser o proprio quadro, com meio intervalo de atraso e
       o ruido do fluxo optico: nao gera, apresenta o real. Exato, sem limiar - grao de filme ou
       dither mudam os bytes e cai no comportamento de sempre (gerar). O `movendo` do .saude
       (R1) passa a vir daqui com o FG dono, com os mesmos 1,5 s da vigia.
     - CORTE DE CENA: fracao dos blocos (kSondaBloco x kSondaLinhas) cuja MEDIA DE LUMA mudou
       mais que kCorteNivel, contra a media movel dessa fracao - o criterio duplo calibrado no
       Universal (src\fg_ffx.h:83-126, 13/09/2026: 0 falsos em pan e combate, 1 em menu). Com o
       jogo no fio (J3) o `reset` dele continua mandando e chega primeiro; a sonda so acrescenta
       o corte que o jogo nao declarou. Nenhum dos dois zera a historia do FFX por conta da
       sonda: ela so segura o gerado, como o Universal faz.
   Custo: kSondaFaixas copias de uma faixa por quadro na lista que ja existe (GPU, microssegundos)
   e a leitura de ~kSondaFaixas*kSondaLinhas*largura texels na thread de ritmo, que ate aqui so
   dormia. O tempo e medido; passando de kSondaTetoMs na media de 300 quadros a sonda se desliga
   sozinha e diz no log. Formato de backbuffer que ela nao le = sonda fora, comportamento anterior. */
const UINT   kSondaFaixas = 6, kSondaLinhas = 4, kSondaBloco = 8;
const double kSondaTetoMs = 0.5;
const double kParadoMs    = 1500.0;   /* o mesmo da vigia do R1 para o movendo=0 */
/* os numeros do detector, copiados de src\fg_ffx.h:83-126 (calibrados la; ver a nota acima) */
const float  kCorteNivel        = 8.0f;   /* niveis de luma (0-255) de mudanca da media do bloco */
const double kCorteSalto        = 8.0;    /* quantas vezes acima da media movel = corte */
const double kCorteFracMin      = 0.06;   /* e ainda assim tem de mexer 6% dos blocos */
const double kCortePiso         = 0.004;  /* piso da media movel (cena parada nao vira gatilho) */
const double kCorteFracAlta     = 0.70;   /* o corte OBVIO: quase a tela toda mudou */
const double kCorteSaltoAlta    = 2.5;
const double kCorteSaltoMin     = 4.0;    /* os saltos caem com o intervalo entre quadros... */
const double kCorteSaltoAltaMin = 2.0;
const double kCorteDtRef        = 33.3;   /* ...a partir desta chegada (ms) */
const double kCorteSaltoPorMs   = 0.060;
const double kCorteSaltoAltaPorMs = 0.015;
const unsigned kCorteAquecer    = 12;     /* quadros antes de a media movel valer */

struct Entrada
{
    ID3D12Resource* interp = nullptr;   /* o quadro gerado (interpTex[idx]) */
    ID3D12Resource* real   = nullptr;   /* o proxy que o jogo desenhou (frame B) */
    uint64_t readyFence = 0;            /* valor de fReady a esperar antes de copiar */
    uint64_t consumaVal = 0;            /* valor a sinalizar em fConsumido quando o proxy sair de cena */
    double   dtMs = 16.6;              /* intervalo medido do jogo (para a metade) */
    bool     temInterp = false;
    UINT     idx = 0;                   /* J4: conjunto (proxy/interp/sonda) desta entrada */
    bool     temSonda = false;          /* J4: a lista de geracao copiou as faixas para sonda[idx] */
};

struct Estado
{
    /* controle */
    std::mutex muCtl; bool ctlLido = false; FILETIME ctlMtime = {};
    int  modo = 0;                      /* 0 off, 1 on, 2 auto */
    std::wstring dllPath;               /* caminho ABSOLUTO do amd_fidelityfx_dx12.dll (do nosso exe) */
    std::atomic<bool> desistido{ false };
    char motivo[160] = "";
    /* P1: estado publicado ao app (EstadoTexto) */
    std::mutex muEstado; char estadoPub[16] = "desligado"; char motivoPub[32] = "fg0"; int refs = -1;
    std::atomic<unsigned> estadoVersao{ 1 };
    std::atomic<int> apiJogo{ 0 };     /* 0 ainda nao visto, 11 D3D11, 12 D3D12 (fica apos o resize) */
    std::atomic<int> modoVisto{ 0 };   /* copia de modo para a thread do Present, sem trava */
    std::atomic<bool> sondou{ false };
    bool falhouMontar = false;         /* o ultimo AposResize tentou e nao assumiu */
    /* J1: os dois numeros que o card do app mostra - quadros do jogo -> quadros na tela.
       O caminho quente so incrementa contadores; a divisao e a janela de 1 s ficam na
       thread da ponte (MedirFps), que e quem publica. */
    std::atomic<uint64_t> presentsJogo{ 0 };
    int64_t medQpc = 0; uint64_t medJogo = 0, medTela = 0;
    std::atomic<int> fpsJogoPub{ 0 }, fpsTelaPub{ 0 };

    /* device/fila/swapchain do jogo */
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* fila = nullptr;   /* a fila DIRECT do jogo (AddRef) */
    IDXGISwapChain* sc = nullptr;
    GetBufferFn origGetBuffer = nullptr;
    GetCurBBFn  origGetCurBB  = nullptr;

    /* desc do backbuffer */
    UINT bbW = 0, bbH = 0, nBuf = 0;
    DXGI_FORMAT bbFmt = DXGI_FORMAT_UNKNOWN;
    bool tearing = false;              /* swapchain criada com ALLOW_TEARING? (present sync 0 exige a flag) */

    /* proxies que o jogo desenha (GetBuffer devolve estes) */
    ID3D12Resource* proxy[kMaxBuf] = {};
    std::atomic<UINT> gameIdx{ 0 };       /* indice que o jogo enxerga (GetCurrentBackBufferIndex) */
    uint64_t proxyLivreEm[kMaxBuf] = {};  /* valor de fConsumido em que o proxy volta a estar livre */

    /* interpolados: C4 um por proxy (no ping-pong, com 2 na fila, o ritmo copiava um ja sobrescrito) */
    ID3D12Resource* interpTex[kMaxBuf] = {};
    ID3D12Resource* depthConst = nullptr;   /* profundidade constante 1.0 (fluxo optico) */
    ID3D12Resource* mvZero = nullptr;       /* MV zerado (fluxo optico) */

    /* J3: profundidade e MV reais do jogo -------------------------------------------------- */
    std::mutex muDados;
    DadosJogo dados;                  /* depth/mv com referencia NOSSA enquanto publicados */
    uint64_t dadosSeq = 0;            /* +1 a cada publicacao do gancho do upscaler */
    int64_t  dadosQpc = 0;            /* quando o ultimo pacote chegou */
    uint64_t dadosUsada = 0;          /* seq do ultimo pacote ja consumido (nao repetir MV velho) */
    /* decidido UMA vez no Montar: o contexto do FidelityFX nasce com as flags do material
       (profundidade invertida, MV em resolucao de tela, jitter embutido) e elas nao mudam sem
       recriar o contexto -- coisa que nao se faz com quadro em voo. */
    bool ctxDoJogo = false; int ctxDepthInv = -1; bool ctxMvDisplay = false, ctxMvJit = false;
    ID3D12Resource* fgDepth = nullptr;   /* a NOSSA copia da profundidade do jogo (R32_FLOAT) */
    ID3D12Resource* fgMv = nullptr;      /* a NOSSA copia dos MV do jogo (R16G16_FLOAT) */
    ID3D12Resource* srcDepth[kMaxBuf] = {}, *srcMv[kMaxBuf] = {};   /* refs do conjunto em voo */
    /* QPC do ultimo quadro gerado com dado real. O card le isto uma vez por segundo, entao a
       resposta vale por 1 s: um quadro solto sem pacote nao faz o card piscar. */
    std::atomic<int64_t> movJogoQpc{ 0 };
    bool usouJogoAnt = false;         /* o quadro anterior usou dado real? (a troca e descontinuidade) */
    unsigned janJogo = 0;             /* quadros da janela de 300 que usaram dado real */

    /* fences */
    ID3D12Fence* fReady = nullptr;    uint64_t readyVal = 0;
    ID3D12Fence* fConsumido = nullptr; uint64_t consumaVal = 0; HANDLE evConsumido = nullptr;

    /* listas: uma para gerar (thread do jogo), uma para copiar+apresentar (thread de ritmo) */
    /* C4: um allocator por proxy/backbuffer e a fence de quando a GPU terminou a ultima lista dele;
       Reset com lista ainda na GPU corrompe o buffer de comandos (nvlddmkm 153, DEVICE_REMOVED) */
    ID3D12CommandAllocator* allocGer[kMaxBuf] = {}; uint64_t gerFeito[kMaxBuf] = {}; ID3D12GraphicsCommandList* listaGer = nullptr;
    ID3D12CommandAllocator* allocCopia[kMaxBuf] = {}; uint64_t copiaFeita[kMaxBuf] = {}; ID3D12GraphicsCommandList* listaCopia = nullptr;
    ID3D12Fence* fCopia = nullptr; HANDLE evCopia = nullptr; uint64_t copiaVal = 0;
    HANDLE evReady = nullptr;             /* C4: espera da fReady na thread do jogo */
    std::atomic<bool> removidoVisto{ false };

    /* FFX (ffx-api) no device do jogo */
    HMODULE hffx = nullptr;
    PfnFfxCreateContext  Create  = nullptr;
    PfnFfxDestroyContext Destroy = nullptr;
    PfnFfxDispatch       Dispatch= nullptr;
    PfnFfxQuery          Query   = nullptr;
    PfnFfxConfigure      Config  = nullptr;
    ffxContext ctx = nullptr;
    uint64_t frameId = 0;
    bool usaUp = false;   /* MV/depth pela .up (nesta rodada: so acende o log/auto; FFX roda em fluxo optico) */

    /* thread de ritmo */
    std::thread thRitmo;
    std::mutex muFila; std::condition_variable cvFila;
    std::deque<Entrada> fila_;
    std::atomic<bool> parar{ false };
    std::atomic<bool> dono{ false };     /* FG assumiu a apresentacao */
    std::atomic<DWORD> idRitmo{ 0 };     /* C3: id da thread de ritmo viva (0 = nenhuma) */
    std::atomic<bool> pendente{ false }; /* C3: o ritmo desistiu; a thread do Present desmonta */

    /* C9: tela cheia exclusiva */
    std::atomic<bool> telaCheia{ false };   /* swapchain em tela cheia: Present sem ALLOW_TEARING */
    std::atomic<bool> transicao{ false };   /* SetFullscreenState em curso: o ritmo nao apresenta */
    std::atomic<int>  emApresentar{ 0 };    /* o ritmo esta dentro de ApresentarUm */
    std::atomic<int64_t> falhaDesde{ 0 };   /* primeira falha seguida de apresentacao (0 = nenhuma) */
    std::atomic<bool> temporario{ false };  /* desistencia por troca de tela: remonta no proximo resize */
    std::atomic<uint64_t> presentsTela{ 0 };/* C2: quadros que o ritmo apresentou (fps na tela) */

    /* C10: depois de desistir, o jogo segue nos proxies ate o proximo ResizeBuffers e o Present direto copia */
    std::atomic<bool> espelho{ false };
    uint64_t espelhados = 0;
    /* C11: fila tirada do swapchain (senao, a primeira DIRECT vista, de reserva) */
    std::mutex muFilaJogo; bool filaDoSc = false; bool avisouReserva = false;
    /* C13: a ponte esta descarregando: o FG nao assume mais */
    std::atomic<bool> descarga{ false };

    /* pacing */
    int64_t ultimoPresentQpc = 0;
    int64_t ultimoJogo = 0;            /* J4: QPC do Present anterior do jogo; zerado no Montar (era static de funcao e
                                          o 1o dt de uma remontagem media o intervalo desde a sessao anterior) */
    HANDLE  evRitmo = nullptr;         /* J4: a espera da fReady no ritmo (era um CreateEvent/CloseHandle por quadro) */

    /* J4: a sonda de luma (ver a nota nas constantes) -------------------------------------- */
    ID3D12Resource* sonda[kMaxBuf] = {};   /* buffer de leitura (READBACK) por conjunto */
    bool sondaGravada[kMaxBuf] = {};       /* a lista de geracao deste conjunto copiou as faixas */
    bool sondaOk = false;                  /* nasceu e o formato e legivel */
    bool sondaDesligada = false;           /* custou mais que kSondaTetoMs por quadro: fora ate o proximo Montar */
    int  sondaTipo = 0;                    /* 1 RGBA8, 2 BGRA8, 3 R10G10B10A2, 4 RGBA16F */
    UINT sondaBpp = 0, sondaPitch = 0, sondaBlocosX = 0;
    UINT64 sondaFaixaBytes = 0, sondaBytes = 0;
    UINT sondaY0[kSondaFaixas] = {};
    std::vector<float> sondaMedia;         /* media de luma por bloco do quadro anterior */
    bool sondaTemMedia = false, sondaTemHash = false;
    uint64_t sondaHash = 0;
    double sondaEma = 0; unsigned sondaQuadros = 0;
    int64_t sondaIgualDesde = 0;           /* QPC do 1o quadro de uma serie de identicos (0 = mudando) */
    std::atomic<bool> parado{ false };     /* identicos ha mais de kParadoMs (o movendo=0 do .saude) */
    double sondaMs = 0; unsigned sondaN = 0;            /* custo, janela de 300 (thread de ritmo) */
    std::atomic<int> sondaUsPub{ 0 };                    /* ultima media publicada, em us */
    std::atomic<unsigned> cortesSonda{ 0 }, paradosSonda{ 0 };   /* segurados na janela de 300 */

    /* J4: a thread de ritmo estourou (falha de hardware) - as travas dela podem ter ficado presas */
    std::atomic<bool> estourou{ false };

    /* estatisticas (janela de 300 quadros do jogo) */
    uint64_t janN = 0; double janInterp = 0, janRitmo = 0, janDt = 0; int64_t janInicio = 0; uint64_t janPresents = 0;
} S;

void Log(const char* fmt, ...)
{
    if (!L) return;
    char b[400]; va_list ap; va_start(ap, fmt); _vsnprintf(b, sizeof b - 1, fmt, ap); va_end(ap); b[sizeof b - 1] = 0;
    L("%s", b);
}

/* C4: true quando a GPU passou de val (device removido tambem conta: a fence vira UINT64_MAX) */
bool EsperarFence(ID3D12Fence* f, uint64_t val, HANDLE ev, DWORD ms)
{
    if (!f || f->GetCompletedValue() >= val) return true;
    if (!ev || FAILED(f->SetEventOnCompletion(val, ev))) return false;
    WaitForSingleObject(ev, ms);
    return f->GetCompletedValue() >= val;
}

const char* NomeOpDred(D3D12_AUTO_BREADCRUMB_OP op)
{
    switch (op)
    {
    case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return "SetMarker";
    case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return "BeginEvent";
    case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return "EndEvent";
    case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED: return "DrawInstanced";
    case D3D12_AUTO_BREADCRUMB_OP_DISPATCH: return "Dispatch";
    case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION: return "CopyTextureRegion";
    case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE: return "CopyResource";
    case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER: return "ResourceBarrier";
    case D3D12_AUTO_BREADCRUMB_OP_PRESENT: return "Present";
    default: return "op";
    }
}

/* C4: uma vez, quando o device cai: motivo e, se o processo ligou o DRED antes de criar o device
   (so o alvo de bancada com ALVO_DRED=1), a ultima operacao de cada lista e a falta de pagina */
void RelatarRemocao(const char* onde)
{
    if (!S.dev) return;
    const HRESULT motivo = S.dev->GetDeviceRemovedReason();
    if (motivo == S_OK || S.removidoVisto.exchange(true)) return;
    Log("fg: device removido (0x%08lX) visto em %s", (unsigned long)motivo, onde);
    ID3D12DeviceRemovedExtendedData* dred = nullptr;
    if (FAILED(S.dev->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedData), (void**)&dred)) || !dred) return;
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bc = {};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&bc)))
    {
        int n = 0;
        for (const D3D12_AUTO_BREADCRUMB_NODE* no = bc.pHeadAutoBreadcrumbNode; no && n < 12; no = no->pNext, ++n)
        {
            const UINT32 feito = no->pLastBreadcrumbValue ? *no->pLastBreadcrumbValue : 0;
            if (feito >= no->BreadcrumbCount) continue;   /* lista completa: nao e suspeita */
            Log("fg: DRED lista '%s' %u/%u feitas; parou em %s", no->pCommandListDebugNameA ? no->pCommandListDebugNameA : "(sem nome)",
                feito, no->BreadcrumbCount, no->pCommandHistory ? NomeOpDred(no->pCommandHistory[feito]) : "?");
        }
        if (n == 0) Log("fg: DRED sem breadcrumbs (DRED desligado no processo?)");
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT pf = {};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pf)) && pf.PageFaultVA)
    {
        Log("fg: DRED falta de pagina em 0x%llX", (unsigned long long)pf.PageFaultVA);
        int n = 0;
        for (const D3D12_DRED_ALLOCATION_NODE* a = pf.pHeadRecentFreedAllocationNode; a && n < 6; a = a->pNext, ++n)
            Log("fg: DRED   liberado ha pouco: '%s' (tipo %d)", a->ObjectNameA ? a->ObjectNameA : "(sem nome)", (int)a->AllocationType);
        n = 0;
        for (const D3D12_DRED_ALLOCATION_NODE* a = pf.pHeadExistingAllocationNode; a && n < 6; a = a->pNext, ++n)
            Log("fg: DRED   vivo perto: '%s' (tipo %d)", a->ObjectNameA ? a->ObjectNameA : "(sem nome)", (int)a->AllocationType);
    }
    dred->Release();
}

/* ------------------------------------------------------------------ controle */
void CaminhoTemp(wchar_t* out, size_t n, const wchar_t* sufixo)
{
    wchar_t tmp[MAX_PATH] = L"";
    DWORD k = GetTempPathW(MAX_PATH, tmp);
    if (k == 0 || k >= MAX_PATH) { tmp[0] = L'.'; tmp[1] = L'\\'; tmp[2] = 0; }
    _snwprintf(out, n, L"%sdlss5-ponte.%lu.%ls", tmp, GetCurrentProcessId(), sufixo);
}

bool LerControle()
{
    wchar_t caminho[MAX_PATH]; CaminhoTemp(caminho, MAX_PATH, L"ngx");
    WIN32_FILE_ATTRIBUTE_DATA fa = {};
    if (!GetFileAttributesExW(caminho, GetFileExInfoStandard, &fa)) { S.ctlLido = true; return false; }
    if (S.ctlLido && CompareFileTime(&fa.ftLastWriteTime, &S.ctlMtime) == 0) return false;
    S.ctlMtime = fa.ftLastWriteTime; S.ctlLido = true;
    FILE* f = _wfopen(caminho, L"rb");
    if (!f) return false;
    int modo = 0; std::wstring dll;
    char linha[512];
    while (fgets(linha, sizeof linha, f))
    {
        char* eq = strchr(linha, '='); if (!eq) continue;
        *eq = 0; const char* k = linha; char* v = eq + 1;
        char* fim = v + strlen(v); while (fim > v && (fim[-1] == '\r' || fim[-1] == '\n' || fim[-1] == ' ')) *--fim = 0;
        if (!strcmp(k, "fg"))
        {
            if (!_stricmp(v, "auto")) modo = 2;
            else modo = atoi(v) != 0 ? 1 : 0;
        }
        else if (!strcmp(k, "fg_dll"))
        {
            dll.clear(); for (char* p = v; *p; ++p) dll.push_back((wchar_t)(unsigned char)*p);
        }
    }
    fclose(f);
    std::lock_guard<std::mutex> g(S.muCtl);
    S.modo = modo; if (!dll.empty()) S.dllPath = dll;
    S.modoVisto = modo;
    return true;
}

/* P1: publica so na mudanca (o Ngx regrava o .ngx-estado quando a versao anda) */
void PublicarEstado(const char* estado, const char* motivo)
{
    std::lock_guard<std::mutex> g(S.muEstado);
    if (!strcmp(S.estadoPub, estado) && !strcmp(S.motivoPub, motivo)) return;
    snprintf(S.estadoPub, sizeof S.estadoPub, "%s", estado);
    snprintf(S.motivoPub, sizeof S.motivoPub, "%s", motivo);
    ++S.estadoVersao;
    Log("fg: estado %s (%s)", estado, motivo);
}

/* J1 (18/09/2026): DE ONDE VEM O MOVIMENTO que o interpolador usa, em uma palavra, para o card.
   J3 (18/09/2026): deixou de ser sempre "optico". Vale "jogo" enquanto o Prepare do FFX estiver
   recebendo a profundidade e os vetores de movimento que o gancho do upscaler capturou; vira
   "optico" de volta no quadro em que o pacote faltar, porque ai quem estima o movimento e o
   fluxo optico do proprio interpolador. Nunca dizer "movimento do jogo" sem os dados na mao. */
static const char* FonteMovimento()
{
    const int64_t q = S.movJogoQpc.load();
    return (q != 0 && Ms(q, Qpc()) < 1000.0) ? "jogo" : "optico";
}

/* J2 (18/09/2026): O JOGO JA GERA QUADROS SOZINHO? Empilhar a nossa geracao sobre a dele poe
   dois interpoladores disputando a apresentacao. Olhamos os modulos que so entram no processo
   quando a geracao de quadros do proprio jogo esta em uso. Uma vez visto, fica visto.
   Devolve o nome do modulo (para o log) ou nullptr. */
static const char* FgDoJogo()
{
    static char achado[64] = "";
    if (achado[0]) return achado;
    static const wchar_t* const kNomes[] = {
        L"sl.dlss_g.dll",                             /* geracao de quadros do DLSS (Streamline) */
        L"amd_fidelityfx_framegeneration_dx12.dll",   /* geracao de quadros do FSR 3 (SDK 2.x) */
        L"libxess_fg.dll",                            /* geracao de quadros do XeSS */
    };
    for (const wchar_t* n : kNomes)
        if (GetModuleHandleW(n))
        {
            WideCharToMultiByte(CP_UTF8, 0, n, -1, achado, (int)sizeof achado, nullptr, nullptr);
            achado[sizeof achado - 1] = 0;
            return achado;
        }
    return nullptr;
}

/* ------------------------------------------------------------------ J3: dados do jogo */

/* O pacote serve para este contexto? Tudo o que o contexto do FidelityFX carimba na criacao
   (profundidade invertida, MV em resolucao de tela, jitter embutido) tem de continuar igual, e o
   material tem de caber no que foi alocado. Qualquer duvida -> fluxo optico. */
bool PacoteServe(const DadosJogo& d, char* porque, size_t n)
{
    const char* m = nullptr;
    if (!d.depth || !d.mv)                          m = "o upscaler do jogo nao entrega depth+mv";
    else if (d.mvNdc)                               m = "MV em NDC (XeSS): convencao nao resolvida";
    else if (d.depthInv < 0)                        m = "profundidade invertida desconhecida (contexto do upscaler criado antes do gancho)";
    else if (!d.fila || d.fila != S.fila)           m = "o upscaler grava numa fila e a apresentacao esta em outra";
    else if (!d.renderW || !d.renderH || d.renderW > S.bbW || d.renderH > S.bbH) m = "resolucao interna fora do tamanho da tela";
    else if (!d.mvW || !d.mvH || d.mvW > S.bbW || d.mvH > S.bbH) m = "MV maior que a tela";
    else if (d.mvDisplayRes && (d.mvW != S.bbW || d.mvH != S.bbH)) m = "MV dito de tela mas com outro tamanho";
    else if (!d.mvDisplayRes && (d.mvW != d.renderW || d.mvH != d.renderH)) m = "MV nem de render nem de tela";
    else if (!(d.mvScaleX == d.mvScaleX) || !(d.mvScaleY == d.mvScaleY) || d.mvScaleX == 0 || d.mvScaleY == 0) m = "escala de MV invalida";
    if (m) { if (porque && n) snprintf(porque, n, "%s", m); return false; }
    return true;
}

/* Montar: ha material do jogo agora? Decide as flags com que o contexto vai nascer. */
void EscolherDadosDoJogo()
{
    S.ctxDoJogo = false; S.ctxDepthInv = -1; S.ctxMvDisplay = false; S.ctxMvJit = false;
    S.dadosUsada = 0; S.movJogoQpc = 0; S.usouJogoAnt = false;
    DadosJogo d; uint64_t seq = 0; int64_t qpc = 0;
    { std::lock_guard<std::mutex> g(S.muDados); d = S.dados; seq = S.dadosSeq; qpc = S.dadosQpc; }
    if (seq == 0) { Log("fg: sem dados do jogo na montagem (jogo sem upscaler enganchado ou 'antes do upscaling' desligado) - fluxo optico"); return; }
    if (Ms(qpc, Qpc()) > 2000.0) { Log("fg: o ultimo pacote do upscaler tem mais de 2 s - fluxo optico"); return; }
    char porque[160] = "";
    if (!PacoteServe(d, porque, sizeof porque)) { Log("fg: dados do jogo recusados (%s) - fluxo optico", porque); return; }
    S.ctxDoJogo = true; S.ctxDepthInv = d.depthInv; S.ctxMvDisplay = d.mvDisplayRes; S.ctxMvJit = d.mvJittered;
}

/* GerarInterpolado: pega o pacote DESTE quadro e guarda as referencias no conjunto `idx`.
   Sem pacote novo (o jogo nao chamou o upscaler neste quadro) nao se reaproveita o anterior:
   MV velho move a imagem para o lado errado, que e pior do que nao ter MV. */
bool PegarDados(UINT idx, DadosJogo* out)
{
    if (!S.ctxDoJogo || !S.fgDepth || !S.fgMv) return false;
    DadosJogo d; uint64_t seq = 0; int64_t qpc = 0;
    {
        std::lock_guard<std::mutex> g(S.muDados);
        if (S.dadosSeq == S.dadosUsada) return false;   /* sem quadro novo do upscaler */
        d = S.dados; seq = S.dadosSeq; qpc = S.dadosQpc;
        if (!d.depth || !d.mv) return false;
        d.depth->AddRef(); d.mv->AddRef();
    }
    char porque[160] = "";
    const bool ok = Ms(qpc, Qpc()) <= 200.0 && PacoteServe(d, porque, sizeof porque) &&
                    d.depthInv == S.ctxDepthInv && d.mvDisplayRes == S.ctxMvDisplay && d.mvJittered == S.ctxMvJit;
    if (!ok)
    {
        d.depth->Release(); d.mv->Release();
        static int avisos = 0;
        if (avisos < 5) { ++avisos; Log("fg: pacote do upscaler mudou de material (%s); este quadro vai no fluxo optico", porque[0] ? porque : "flags do contexto ja carimbadas"); }
        return false;
    }
    S.dadosUsada = seq;
    S.srcDepth[idx] = d.depth; S.srcMv[idx] = d.mv;   /* soltos quando o conjunto `idx` reciclar */
    *out = d;
    return true;
}

void CopiarPedaco(ID3D12GraphicsCommandList* l, ID3D12Resource* dst, ID3D12Resource* src, unsigned w, unsigned h)
{
    D3D12_TEXTURE_COPY_LOCATION d = {}; d.pResource = dst; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION s = {}; s.pResource = src; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.SubresourceIndex = 0;
    D3D12_BOX box = { 0, 0, 0, w, h, 1 };
    l->CopyTextureRegion(&d, 0, 0, 0, &s, &box);
}

/* ------------------------------------------------------------------ J4: sonda de luma */
void SondaSoltar()
{
    for (UINT i = 0; i < kMaxBuf; ++i) { Soltar(S.sonda[i]); S.sondaGravada[i] = false; }
    S.sondaOk = false; S.sondaTemMedia = S.sondaTemHash = false; S.sondaIgualDesde = 0; S.parado = false;
    S.sondaMedia.clear(); S.sondaMedia.shrink_to_fit();
}

/* Montar: o formato e legivel? Entao um buffer de leitura por conjunto, com o layout que o proprio
   runtime calcula para UMA faixa (a licao do F0/R1: pitch fixo derrubou o device em 16F). */
bool SondaCriar()
{
    SondaSoltar();
    S.sondaDesligada = false; S.sondaEma = 0; S.sondaQuadros = 0; S.sondaMs = 0; S.sondaN = 0;
    S.sondaUsPub = 0; S.cortesSonda = 0; S.paradosSonda = 0;
    const DXGI_FORMAT ft = Tipado(S.bbFmt);
    switch (ft)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: S.sondaTipo = 1; S.sondaBpp = 4; break;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: S.sondaTipo = 2; S.sondaBpp = 4; break;
    case DXGI_FORMAT_R10G10B10A2_UNORM:  S.sondaTipo = 3; S.sondaBpp = 4; break;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: S.sondaTipo = 4; S.sondaBpp = 8; break;
    default:
        Log("fg: sonda de corte de cena/imagem parada fora (formato de backbuffer %u nao lido) - comportamento anterior", (unsigned)ft);
        return false;
    }
    if (S.bbH < kSondaLinhas * kSondaFaixas || S.bbW < kSondaBloco || !S.dev) return false;
    D3D12_RESOURCE_DESC td = {};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = S.bbW; td.Height = kSondaLinhas; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = ft; td.SampleDesc.Count = 1; td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {}; UINT64 total = 0;
    S.dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &total);
    if (total == 0 || total == UINT64_MAX || fp.Footprint.RowPitch < S.bbW * S.sondaBpp) return false;
    S.sondaPitch = fp.Footprint.RowPitch;
    S.sondaFaixaBytes = (total + (D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1)) & ~(UINT64)(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
    S.sondaBytes = S.sondaFaixaBytes * kSondaFaixas;
    for (UINT s = 0; s < kSondaFaixas; ++s)
    {
        const UINT yc = (UINT)(((UINT64)(2 * s + 1) * S.bbH) / (2 * kSondaFaixas));
        UINT y0 = yc >= kSondaLinhas / 2 ? yc - kSondaLinhas / 2 : 0;
        if (y0 + kSondaLinhas > S.bbH) y0 = S.bbH - kSondaLinhas;
        S.sondaY0[s] = y0;
    }
    S.sondaBlocosX = (S.bbW + kSondaBloco - 1) / kSondaBloco;
    S.sondaMedia.assign((size_t)S.sondaBlocosX * kSondaFaixas, 0.0f);
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = S.sondaBytes; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (UINT i = 0; i < S.nBuf; ++i)
        if (FAILED(S.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                  __uuidof(ID3D12Resource), (void**)&S.sonda[i])) || !S.sonda[i])
        { SondaSoltar(); Log("fg: sonda de corte de cena/imagem parada fora (buffer de leitura nao nasceu) - comportamento anterior"); return false; }
    S.sondaOk = true;
    Log("fg: sonda de corte de cena/imagem parada ligada (%u faixas de %u linhas, blocos de %ux%u, %llu KB por conjunto)",
        kSondaFaixas, kSondaLinhas, kSondaBloco, kSondaLinhas, (unsigned long long)(S.sondaBytes / 1024));
    return true;
}

/* Na lista de geracao, com frameB em COPY_SOURCE: as faixas vao para o buffer do conjunto. */
void SondaGravar(UINT idx, ID3D12Resource* frameB)
{
    for (UINT s = 0; s < kSondaFaixas; ++s)
    {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = S.sonda[idx]; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = (UINT64)s * S.sondaFaixaBytes;
        dst.PlacedFootprint.Footprint.Format = Tipado(S.bbFmt);
        dst.PlacedFootprint.Footprint.Width = S.bbW; dst.PlacedFootprint.Footprint.Height = kSondaLinhas;
        dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = S.sondaPitch;
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = frameB; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
        const D3D12_BOX box = { 0, S.sondaY0[s], 0, S.bbW, S.sondaY0[s] + kSondaLinhas, 1 };
        S.listaGer->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
    }
}

/* meia precisao -> float; subnormal vira zero (e 6e-5 de luma, nada) */
float MeiaParaFloat(uint16_t h)
{
    const uint32_t s = (uint32_t)(h & 0x8000u) << 16, e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
    uint32_t bits;
    if (e == 0) bits = s;
    else if (e == 31) bits = s | 0x7F800000u | (m << 13);
    else bits = s | ((e + 112u) << 23) | (m << 13);
    float f; memcpy(&f, &bits, 4);
    return f;
}

/* luma 0-255 do texel x de uma linha, por tipo */
template <int T> float SondaLuma(const uint8_t* row, UINT x)
{
    if (T == 1) { const uint8_t* q = row + (size_t)x * 4; return (float)((77u * q[0] + 150u * q[1] + 29u * q[2]) >> 8); }
    if (T == 2) { const uint8_t* q = row + (size_t)x * 4; return (float)((77u * q[2] + 150u * q[1] + 29u * q[0]) >> 8); }
    if (T == 3) { uint32_t v; memcpy(&v, row + (size_t)x * 4, 4); return (float)((77u * (v & 1023u) + 150u * ((v >> 10) & 1023u) + 29u * ((v >> 20) & 1023u)) >> 10); }
    uint16_t q[4]; memcpy(q, row + (size_t)x * 8, 8);
    float l = 0.299f * MeiaParaFloat(q[0]) + 0.587f * MeiaParaFloat(q[1]) + 0.114f * MeiaParaFloat(q[2]);
    if (!(l > 0.0f)) l = 0.0f;   /* NaN e negativo viram 0 */
    return 255.0f * l / (1.0f + l);   /* scRGB linear: um Reinhard leva ao 0-255 perceptivo do limiar */
}

/* le as faixas do conjunto: medias de luma por bloco (em S.sondaMedia, comparando com o quadro
   anterior) e o hash dos bytes. Devolve quantos blocos mudaram mais que kCorteNivel. */
template <int T> unsigned SondaPassar(const uint8_t* p, uint64_t* hash)
{
    unsigned nd = 0; uint64_t h = 1469598103934665603ull;
    float* med = S.sondaMedia.data();
    const UINT larguraBytes = S.bbW * S.sondaBpp;
    for (UINT s = 0; s < kSondaFaixas; ++s)
    {
        const uint8_t* faixa = p + (size_t)s * S.sondaFaixaBytes;
        for (UINT y = 0; y < kSondaLinhas; ++y)
        {
            const uint8_t* row = faixa + (size_t)y * S.sondaPitch;
            UINT i = 0;
            for (; i + 8 <= larguraBytes; i += 8) { uint64_t w; memcpy(&w, row + i, 8); h = (h ^ w) * 0x9E3779B97F4A7C15ull; h ^= h >> 29; }
            for (; i < larguraBytes; ++i) { h = (h ^ row[i]) * 1099511628211ull; }
        }
        for (UINT bx = 0; bx < S.sondaBlocosX; ++bx)
        {
            const UINT x0 = bx * kSondaBloco, x1 = (x0 + kSondaBloco < S.bbW) ? x0 + kSondaBloco : S.bbW;
            float soma = 0.0f;
            for (UINT y = 0; y < kSondaLinhas; ++y)
            {
                const uint8_t* row = faixa + (size_t)y * S.sondaPitch;
                for (UINT x = x0; x < x1; ++x) soma += SondaLuma<T>(row, x);
            }
            const float mB = soma / (float)((x1 - x0) * kSondaLinhas);
            float& mA = med[(size_t)s * S.sondaBlocosX + bx];
            if (S.sondaTemMedia && fabsf(mA - mB) > kCorteNivel) ++nd;
            mA = mB;
        }
    }
    *hash = h;
    return nd;
}

/* Thread de ritmo, com a GPU ja passada da fReady desta entrada. corte/igual valem para o par
   (anterior, este) - o que o interpolado desta entrada representaria. */
void SondaAnalisar(UINT idx, double dtMs, bool* corte, bool* igual)
{
    *corte = false; *igual = false;
    if (!S.sondaOk || S.sondaDesligada || !S.sonda[idx]) return;
    const int64_t t0 = Qpc();
    void* p = nullptr;
    const D3D12_RANGE ler = { 0, (SIZE_T)S.sondaBytes }, nada = { 0, 0 };
    if (FAILED(S.sonda[idx]->Map(0, &ler, &p)) || !p) return;
    uint64_t h = 0; unsigned nd = 0;
    switch (S.sondaTipo)
    {
    case 1: nd = SondaPassar<1>((const uint8_t*)p, &h); break;
    case 2: nd = SondaPassar<2>((const uint8_t*)p, &h); break;
    case 3: nd = SondaPassar<3>((const uint8_t*)p, &h); break;
    default: nd = SondaPassar<4>((const uint8_t*)p, &h); break;
    }
    S.sonda[idx]->Unmap(0, &nada);
    const int64_t agora = Qpc();

    /* imagem parada: bytes identicos ao quadro anterior */
    const bool ig = S.sondaTemHash && h == S.sondaHash;
    S.sondaHash = h; S.sondaTemHash = true;
    if (ig) { if (S.sondaIgualDesde == 0) S.sondaIgualDesde = agora; }
    else S.sondaIgualDesde = 0;
    S.parado = ig && Ms(S.sondaIgualDesde, agora) > kParadoMs;
    *igual = ig;

    /* corte de cena: o criterio duplo do Universal, com os saltos em funcao do intervalo */
    if (S.sondaTemMedia)
    {
        const double nb = (double)S.sondaMedia.size();
        const double frac = nb > 0 ? (double)nd / nb : 0.0;
        const double ema = S.sondaQuadros == 0 ? frac : S.sondaEma;
        const double base = ema > kCortePiso ? ema : kCortePiso;
        const double dt = dtMs < 8.0 ? 8.0 : (dtMs > 200.0 ? 200.0 : dtMs);
        double salto = kCorteSalto - kCorteSaltoPorMs * (dt - kCorteDtRef);
        double saltoAlta = kCorteSaltoAlta - kCorteSaltoAltaPorMs * (dt - kCorteDtRef);
        if (salto > kCorteSalto) salto = kCorteSalto;
        if (salto < kCorteSaltoMin) salto = kCorteSaltoMin;
        if (saltoAlta > kCorteSaltoAlta) saltoAlta = kCorteSaltoAlta;
        if (saltoAlta < kCorteSaltoAltaMin) saltoAlta = kCorteSaltoAltaMin;
        *corte = !ig && S.sondaQuadros > kCorteAquecer &&
                 ((frac > kCorteFracMin && frac > salto * base) || (frac > kCorteFracAlta && frac > saltoAlta * base));
        S.sondaEma = 0.85 * ema + 0.15 * frac;
        ++S.sondaQuadros;
    }
    S.sondaTemMedia = true;

    /* custo: media de 300; acima do teto a sonda sai de cena e o FG volta a ser o de antes */
    S.sondaMs += Ms(t0, Qpc());
    if (++S.sondaN >= 300)
    {
        const double media = S.sondaMs / S.sondaN;
        S.sondaUsPub = (int)(media * 1000.0 + 0.5);
        S.sondaMs = 0; S.sondaN = 0;
        if (media > kSondaTetoMs)
        {
            S.sondaDesligada = true; S.parado = false;
            Log("fg: a sonda custa %.2f ms por quadro na thread de ritmo (teto %.2f) - desligada; corte de cena e imagem parada voltam ao comportamento anterior", media, kSondaTetoMs);
        }
    }
}

/* ------------------------------------------------------------------ FFX */
uint32_t TransferFn(DXGI_FORMAT f)
{
    if (Tipado(f) == DXGI_FORMAT_R16G16B16A16_FLOAT) return FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SCRGB;
    if (Tipado(f) == DXGI_FORMAT_R10G10B10A2_UNORM)  return FFX_API_BACKBUFFER_TRANSFER_FUNCTION_PQ;
    return FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
}

bool CarregarFfx()
{
    std::wstring dll;
    { std::lock_guard<std::mutex> g(S.muCtl); dll = S.dllPath; }
    if (dll.empty()) { snprintf(S.motivo, sizeof S.motivo, "fg_dll ausente no controle"); return false; }
    /* C4: fica carregado entre resizes (so o Parar descarrega); o sr engancha exports deste modulo */
    if (!S.hffx) S.hffx = LoadLibraryW(dll.c_str());
    if (!S.hffx) { snprintf(S.motivo, sizeof S.motivo, "LoadLibrary do amd_fidelityfx_dx12.dll falhou (%lu)", GetLastError()); return false; }
    S.Create   = (PfnFfxCreateContext)(void*)GetProcAddress(S.hffx, "ffxCreateContext");
    S.Destroy  = (PfnFfxDestroyContext)(void*)GetProcAddress(S.hffx, "ffxDestroyContext");
    S.Dispatch = (PfnFfxDispatch)(void*)GetProcAddress(S.hffx, "ffxDispatch");
    S.Query    = (PfnFfxQuery)(void*)GetProcAddress(S.hffx, "ffxQuery");
    S.Config   = (PfnFfxConfigure)(void*)GetProcAddress(S.hffx, "ffxConfigure");
    if (!S.Create || !S.Destroy || !S.Dispatch || !S.Query || !S.Config)
    { snprintf(S.motivo, sizeof S.motivo, "ffx-api sem os exports esperados"); return false; }
    return true;
}

bool CriarContextoFfx()
{
    ffxCreateBackendDX12Desc be = {}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = S.dev;
    ffxCreateContextDescFrameGeneration cd = {};
    cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; cd.header.pNext = &be.header;
    cd.flags = 0;
    if (Tipado(S.bbFmt) == DXGI_FORMAT_R16G16B16A16_FLOAT) cd.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
    /* J3: o material do jogo vem descrito nas flags de CRIACAO, nao no dispatch
       (ffx_framegeneration.h:36-39). Sao as mesmas tres do upscaler (ffx_upscale.h:42-45), o que
       era de esperar: num jogo com FSR 3 o Prepare recebe exatamente o que o Upscale recebeu.
       Por isso elas sao decididas uma vez, no Montar, e o pacote que nao casar com elas cai no
       fluxo optico em vez de recriar contexto com quadro em voo. */
    if (S.ctxDoJogo)
    {
        if (S.ctxDepthInv == 1) cd.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
        if (S.ctxMvDisplay)     cd.flags |= FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
        if (S.ctxMvJit)         cd.flags |= FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
    }
    /* maxRenderSize = tela: a resolucao interna do jogo e sempre menor ou igual, e o Prepare
       manda o renderSize deste quadro (resolucao dinamica cabe sem recriar nada). */
    cd.displaySize = { S.bbW, S.bbH }; cd.maxRenderSize = { S.bbW, S.bbH };
    cd.backBufferFormat = ffxApiGetSurfaceFormatDX12(Tipado(S.bbFmt));
    const int64_t t0 = Qpc();
    ffxReturnCode_t rc = S.Create(&S.ctx, &cd.header, nullptr);
    if (rc != FFX_API_RETURN_OK || !S.ctx) { snprintf(S.motivo, sizeof S.motivo, "ffxCreateContext(FRAMEGENERATION) devolveu %u", (unsigned)rc); S.ctx = nullptr; return false; }
    /* Mesma configuracao provada no modo Universal (src\fg_ffx.h): geramos por Dispatch e
       apresentamos nos mesmos, sem a swapchain do FFX. */
    ffxConfigureDescFrameGeneration cf = {}; cf.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    cf.frameGenerationEnabled = true;
    cf.generationRect = { 0, 0, (int32_t)S.bbW, (int32_t)S.bbH }; cf.frameID = 0;
    S.Config(&S.ctx, &cf.header);
    const char* prov = "?";
    ffxQueryGetProviderVersion pv = {}; pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    if (S.Query(&S.ctx, &pv.header) == FFX_API_RETURN_OK && pv.versionName) prov = pv.versionName;
    Log("fg: contexto FidelityFX FG %ux%u criado em %.0f ms (provedor '%s')", S.bbW, S.bbH, Ms(t0, Qpc()), prov);
    return true;
}

/* ------------------------------------------------------------------ recursos */
ID3D12Resource* CriarTex(UINT w, UINT h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES estado)
{
    D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; rd.Flags = flags;
    ID3D12Resource* r = nullptr;
    if (FAILED(S.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, estado, nullptr, __uuidof(ID3D12Resource), (void**)&r)))
        return nullptr;
    return r;
}

/* profundidade constante 1.0 e MV zerado, para o caso "so fluxo optico". */
bool PreencherConstantes()
{
    S.depthConst = CriarTex(S.bbW, S.bbH, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    S.mvZero     = CriarTex(S.bbW, S.bbH, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
    if (!S.depthConst || !S.mvZero) return false;
    /* upload da profundidade 1.0 (o MV fica em zero, que e o conteudo inicial de uma textura DEFAULT
       apos a primeira transicao; a AMD tolera MV vazio no fluxo optico). */
    const UINT pitch = (S.bbW * 4 + 255) & ~255u;
    D3D12_HEAP_PROPERTIES up = {}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd = {}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = (UINT64)pitch * S.bbH;
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* buf = nullptr;
    if (FAILED(S.dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), (void**)&buf)))
        return false;
    uint8_t* m = nullptr; buf->Map(0, nullptr, (void**)&m);
    for (UINT y = 0; y < S.bbH; ++y) { float* row = (float*)(m + (size_t)y * pitch); for (UINT x = 0; x < S.bbW; ++x) row[x] = 1.0f; }
    buf->Unmap(0, nullptr);
    S.allocGer[0]->Reset(); S.listaGer->Reset(S.allocGer[0], nullptr);
    D3D12_TEXTURE_COPY_LOCATION dst = {}; dst.pResource = S.depthConst; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src = {}; src.pResource = buf; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT; src.PlacedFootprint.Footprint.Width = S.bbW;
    src.PlacedFootprint.Footprint.Height = S.bbH; src.PlacedFootprint.Footprint.Depth = 1; src.PlacedFootprint.Footprint.RowPitch = pitch;
    D3D12_RESOURCE_BARRIER b1 = Barr(S.depthConst, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    S.listaGer->ResourceBarrier(1, &b1);
    S.listaGer->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER b2 = Barr(S.depthConst, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    S.listaGer->ResourceBarrier(1, &b2);
    S.listaGer->Close();
    ID3D12CommandList* ls[1] = { S.listaGer };
    S.fila->ExecuteCommandLists(1, ls);
    /* espera curta pelo upload terminar antes de soltar o buffer */
    S.fila->Signal(S.fCopia, ++S.copiaVal);
    if (!EsperarFence(S.fCopia, S.copiaVal, S.evCopia, 5000)) return false;   /* C4: buffer em uso: vaza em vez de soltar */
    buf->Release();
    return true;
}

/* ------------------------------------------------------------------ montar */
bool Montar()
{
    /* device pela swapchain (GetBuffer original) */
    ID3D12Resource* bb0 = nullptr;
    if (!S.origGetBuffer || FAILED(S.origGetBuffer(S.sc, 0, __uuidof(ID3D12Resource), (void**)&bb0)) || !bb0)
    { snprintf(S.motivo, sizeof S.motivo, "GetBuffer(0) real falhou"); return false; }
    bb0->GetDevice(__uuidof(ID3D12Device), (void**)&S.dev);
    DXGI_SWAP_CHAIN_DESC scd = {}; S.sc->GetDesc(&scd);
    bb0->Release();
    if (!S.dev) { snprintf(S.motivo, sizeof S.motivo, "sem device D3D12"); return false; }

    S.bbW = scd.BufferDesc.Width; S.bbH = scd.BufferDesc.Height; S.bbFmt = scd.BufferDesc.Format;
    S.tearing = (scd.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;
    S.nBuf = scd.BufferCount; if (S.nBuf == 0 || S.nBuf > kMaxBuf) S.nBuf = 2;
    { BOOL fs = FALSE; S.telaCheia = SUCCEEDED(S.sc->GetFullscreenState(&fs, nullptr)) && fs; }   /* C9 */
    S.falhaDesde = 0;
    if (S.bbW == 0 || S.bbH == 0) { snprintf(S.motivo, sizeof S.motivo, "backbuffer 0x0"); return false; }

    if (!CarregarFfx()) return false;

    /* listas e fences */
    for (UINT i = 0; i < S.nBuf; ++i)
    {
        if (FAILED(S.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&S.allocGer[i])) ||
            FAILED(S.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&S.allocCopia[i])))
        { snprintf(S.motivo, sizeof S.motivo, "allocators"); return false; }
        S.gerFeito[i] = S.copiaFeita[i] = 0;
    }
    if (FAILED(S.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, S.allocGer[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&S.listaGer)))
    { snprintf(S.motivo, sizeof S.motivo, "lista de geracao"); return false; }
    S.listaGer->Close();
    if (FAILED(S.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, S.allocCopia[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&S.listaCopia)))
    { snprintf(S.motivo, sizeof S.motivo, "lista de copia"); return false; }
    S.listaCopia->Close();
    S.listaGer->SetName(L"dlss5-fg-geracao"); S.listaCopia->SetName(L"dlss5-fg-copia");   /* C4: nomes para o DRED */
    if (FAILED(S.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&S.fReady)) ||
        FAILED(S.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&S.fConsumido)) ||
        FAILED(S.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&S.fCopia)))
    { snprintf(S.motivo, sizeof S.motivo, "fences"); return false; }
    S.evConsumido = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    S.evCopia = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    S.evReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    S.evRitmo = CreateEventW(nullptr, FALSE, FALSE, nullptr);   /* J4: pre-criado, em vez de um por quadro no ritmo */
    S.readyVal = S.consumaVal = S.copiaVal = 0;

    /* proxies (o jogo desenha), interpolados e constantes */
    const DXGI_FORMAT ft = Tipado(S.bbFmt);
    for (UINT i = 0; i < S.nBuf; ++i)
    {
        S.proxy[i] = CriarTex(S.bbW, S.bbH, ft, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
        if (!S.proxy[i]) { snprintf(S.motivo, sizeof S.motivo, "proxy %u", i); return false; }
        S.proxyLivreEm[i] = 0;
    }
    for (UINT i = 0; i < S.nBuf; ++i)
    {
        S.interpTex[i] = CriarTex(S.bbW, S.bbH, ft, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        if (!S.interpTex[i]) { snprintf(S.motivo, sizeof S.motivo, "interp %u", i); return false; }
    }
    if (!PreencherConstantes()) { snprintf(S.motivo, sizeof S.motivo, "constantes depth/mv"); return false; }
    /* J3: ha profundidade e MV do jogo? A resposta carimba as flags do contexto logo abaixo. */
    EscolherDadosDoJogo();
    if (S.ctxDoJogo)
    {
        /* as NOSSAS copias, no maximo (tela). O FFX so le renderSize pixels; sobra de textura e
           o caso normal de resolucao dinamica. Ter copia propria e o que permite a sessao do
           upscaler nascer e morrer sem nunca deixar um recurso dela dentro de lista nossa. */
        S.fgDepth = CriarTex(S.bbW, S.bbH, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
        S.fgMv    = CriarTex(S.bbW, S.bbH, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON);
        if (!S.fgDepth || !S.fgMv) { Log("fg: sem memoria para as copias de depth/mv - fluxo optico"); Soltar(S.fgDepth); Soltar(S.fgMv); S.ctxDoJogo = false; }
    }
    if (!CriarContextoFfx()) return false;
    SondaCriar();   /* J4: sem ela o FG e o de antes; a funcao ja diz no log por que nao nasceu */

    S.gameIdx = 0; S.frameId = 0; S.ultimoPresentQpc = 0; S.ultimoJogo = 0;   /* J4: o 1o dt desta montagem nao e o da sessao anterior */
    S.janN = 0; S.janPresents = 0; S.janJogo = 0; S.janInterp = S.janRitmo = S.janDt = 0;
    return true;
}

/* ------------------------------------------------------------------ ritmo */
void ThreadRitmo();

/* C3: parar sob a trava da fila, para o wait da thread de ritmo nao perder o aviso */
void PedirParada()
{
    /* J4: depois de um estouro na thread de ritmo a muFila pode ter ficado presa com ela (o
       longjmp da guarda nao solta trava nenhuma); e ninguem mais espera na cv. Nao pegar. */
    if (S.estourou.load()) { S.parar = true; S.cvFila.notify_all(); return; }
    try { std::lock_guard<std::mutex> lk(S.muFila); S.parar = true; } catch (...) { S.parar = true; }
    S.cvFila.notify_all();
}

bool NaThreadRitmo() { return S.idRitmo.load() == GetCurrentThreadId(); }

/* C3: ha thread viva ou recurso montado (nao depende de S.dono) */
bool Montado()
{
    return S.thRitmo.joinable() || S.ctx || S.dev || S.listaGer || S.proxy[0] || S.fReady;   /* C4: hffx fica carregado */
}

void IniciarRitmo()
{
    /* C3: thread anterior ainda viva (dois ResizeBuffers seguidos): para e junta antes de criar
       outra; atribuir a um std::thread joinable chama std::terminate */
    if (S.thRitmo.joinable()) { PedirParada(); S.thRitmo.join(); }
    S.parar = false; S.pendente = false;
    S.thRitmo = std::thread(ThreadRitmo);
}

void Desistir(const char* motivo, bool temporario = false)
{
    if (S.desistido.exchange(true)) return;
    S.temporario = temporario;   /* C9: troca de tela -> tenta de novo no proximo ResizeBuffers */
    snprintf(S.motivo, sizeof S.motivo, "%s", motivo);
    Log("fg: desligado (%s) -- de volta ao Present direto%s", motivo, temporario ? "; tenta de novo na proxima troca de tela" : "");
    PedirParada();
    /* C10: antes de soltar o dono, para o GetBuffer do jogo nao trocar de textura no meio */
    S.espelho = (S.proxy[0] != nullptr); S.espelhados = 0;
    S.dono = false;
    /* C3: a thread de ritmo nunca se junta: so marca; quem desmonta e a thread do Present
       (Recolher) ou do resize (AntesResize/AposResize) */
    if (NaThreadRitmo()) { S.pendente = true; return; }
    Desmontar();
}

/* C5: chamado quando uma espera de GPU estoura; device caido -> relato e Present direto */
bool DeviceCaiu(const char* onde)
{
    if (!S.dev || S.dev->GetDeviceRemovedReason() == S_OK) return false;
    RelatarRemocao(onde);
    Desistir("device removido");
    return true;
}

/* Gera o quadro interpolado a partir do proxy que o jogo acabou de desenhar
   (frame B) e do ffx-api. Roda na fila do jogo (thread do jogo), submetido
   ANTES do Signal(fReady). Devolve o interpTex usado, ou nullptr se falhou. */
ID3D12Resource* GerarInterpolado(UINT idx, ID3D12Resource* frameB, double dtMs, bool* corteDeCena)
{
    ID3D12Resource* out = S.interpTex[idx];
    S.sondaGravada[idx] = false;   /* J4: qualquer saida antes da submissao deixa a entrada sem sonda */
    /* C4: so recicla o allocator depois de a GPU terminar a ultima geracao dele */
    if (!EsperarFence(S.fReady, S.gerFeito[idx], S.evReady, 2000)) return nullptr;
    /* J3: a GPU terminou o conjunto anterior DESTE indice, entao as referencias que ele segurava
       podem sair -- na mesma thread que as tomou e com nada em voo apontando para elas (o padrao
       da fase 2; a causa B do X-CRASH-REQUIEM foi exatamente soltar fora desse par). */
    Soltar(S.srcDepth[idx]); Soltar(S.srcMv[idx]);
    if (FAILED(S.allocGer[idx]->Reset()) || FAILED(S.listaGer->Reset(S.allocGer[idx], nullptr))) return nullptr;

    /* J3: o pacote do upscaler DESTE quadro (profundidade e MV reais), ou nada */
    DadosJogo dj; const bool usaJogo = PegarDados(idx, &dj);
    ID3D12Resource* texDepth = usaJogo ? S.fgDepth : S.depthConst;
    ID3D12Resource* texMv    = usaJogo ? S.fgMv    : S.mvZero;
    if (usaJogo)
    {
        ++S.janJogo; S.movJogoQpc = Qpc();
        /* Copiamos depth e MV do jogo para as NOSSAS texturas antes de o FFX ler. Os slots de
           origem sao devolvidos a COMMON pelo sr/upstream e foram criados com
           ALLOW_SIMULTANEOUS_ACCESS, entao a promocao implicita de COMMON para COPY_SOURCE cobre
           a leitura: nenhuma barreira nossa toca recurso de outro modulo. */
        D3D12_RESOURCE_BARRIER cp[2] = {
            Barr(S.fgDepth, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST),
            Barr(S.fgMv,    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST) };
        S.listaGer->ResourceBarrier(2, cp);
        CopiarPedaco(S.listaGer, S.fgDepth, dj.depth, dj.renderW, dj.renderH);
        CopiarPedaco(S.listaGer, S.fgMv, dj.mv, dj.mvW, dj.mvH);
    }

    /* barreiras para o dispatch do FFX ler B/depth/mv e escrever no interp */
    const D3D12_RESOURCE_STATES deCopia = usaJogo ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_BARRIER pre[4] = {
        Barr(frameB, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        Barr(out, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        Barr(texMv, deCopia, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        Barr(texDepth, deCopia, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) };
    S.listaGer->ResourceBarrier(4, pre);

    ffxDispatchDescFrameGenerationPrepareCameraInfo cam = {}; cam.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
    cam.cameraUp[1] = 1; cam.cameraRight[0] = 1; cam.cameraForward[2] = 1;
    ffxDispatchDescFrameGenerationPrepare pp = {}; pp.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE; pp.header.pNext = &cam.header;
    pp.frameID = S.frameId; pp.commandList = S.listaGer;
    /* J3. renderSize e o tamanho de depth e MV, NAO o da cor: o cabecalho diz isso com todas as
       letras ("dilatedDepth, dilatedMotionVectors are expected to be of ths size",
       ffx_framegeneration.h:127). A cor continua em tamanho de tela, no dispatch de baixo.
       jitterOffset e motionVectorScale saem do dispatch do upscaler do jogo sem tocar em nada:
       os dois structs da MESMA api tem os mesmos campos com a mesma descricao
       (ffx_upscale.h:93-94 contra ffx_framegeneration.h:128-129), e num jogo com FSR 3 o Prepare
       recebe do proprio jogo os mesmos valores que o Upscale recebeu. Depois de
       motionVectorScale, o MV esta em PIXELS DA RESOLUCAO DE RENDER, +x para a direita, +y para
       baixo, apontando do quadro atual para o anterior -- a mesma convencao do DLSS
       (MV.Scale.X/Y) e do XeSS em pixels, e por isso o caminho do ngx_upstream tambem entrega o
       par (textura, escala) sem conversao. XeSS em NDC e o unico caso torto e e recusado
       (PacoteServe), como o sr_hook ja avisava em sr_hook.cpp:1175. */
    pp.renderSize = usaJogo ? FfxApiDimensions2D{ dj.renderW, dj.renderH } : FfxApiDimensions2D{ S.bbW, S.bbH };
    pp.jitterOffset = usaJogo ? FfxApiFloatCoords2D{ dj.jitterX, dj.jitterY } : FfxApiFloatCoords2D{ 0, 0 };
    pp.motionVectorScale = usaJogo ? FfxApiFloatCoords2D{ dj.mvScaleX, dj.mvScaleY } : FfxApiFloatCoords2D{ 1, 1 };
    pp.frameTimeDelta = (float)((dtMs > 8.0 && dtMs < 50.0) ? dtMs : (dtMs >= 50.0 ? 50.0 : 16.667));
    /* camera: o FSR 2/3.1 do jogo carrega near/far/fov no proprio dispatch (ffx_upscale.h:104-107,
       sr_contrato.h:102-104) e ai usamos os numeros dele; o DLSS e o XeSS nao tem esses campos,
       e ai fica a suposicao de sempre (0,1 / 1000 / 1 rad). */
    pp.cameraNear = (usaJogo && dj.camNear > 0) ? dj.camNear : 0.1f;
    pp.cameraFar = (usaJogo && dj.camFar > 0) ? dj.camFar : 1000.0f;
    pp.cameraFovAngleVertical = (usaJogo && dj.camFov > 0) ? dj.camFov : 1.0f;
    pp.viewSpaceToMetersFactor = (usaJogo && dj.viewToM > 0) ? dj.viewToM : 1.0f;
    pp.depth = ffxApiGetResourceDX12(texDepth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    pp.motionVectors = ffxApiGetResourceDX12(texMv, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    const ffxReturnCode_t rcP = S.Dispatch(&S.ctx, &pp.header);

    /* J3: corte de cena. Com o jogo no fio nao ha o que adivinhar: o reset dele e o mesmo que ele
       manda ao upscaler. O FFX zera a historia com dd.reset, e o quadro gerado nessa troca nao vai
       para a tela (o Universal mediu -11,55 dB num corte interpolado; fg_ffx.h:888-889). */
    const bool corte = usaJogo && dj.reset;
    if (corteDeCena) *corteDeCena = corte;
    if (corte) { static unsigned n = 0; if (n < 20) { ++n; Log("fg: corte de cena avisado pelo jogo - quadro real apresentado, sem gerado"); } }
    /* trocar de fonte de movimento no meio do fluxo tambem e descontinuidade: a historia interna
       do FFX foi construida com a outra semantica de entrada. */
    const bool trocouFonte = (S.frameId != 0) && (usaJogo != S.usouJogoAnt);
    if (trocouFonte) { static unsigned n = 0; if (n < 10) { ++n; Log("fg: fonte do movimento mudou para %s - historia do interpolador zerada", usaJogo ? "o jogo" : "fluxo optico"); } }
    S.usouJogoAnt = usaJogo;

    ffxDispatchDescFrameGeneration dd = {}; dd.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION; dd.commandList = S.listaGer;
    dd.presentColor = ffxApiGetResourceDX12(frameB, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    dd.outputs[0] = ffxApiGetResourceDX12(out, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    dd.numGeneratedFrames = 1; dd.reset = (S.frameId == 0) || corte || trocouFonte;
    dd.backbufferTransferFunction = TransferFn(S.bbFmt);
    dd.minMaxLuminance[0] = 0.0f; dd.minMaxLuminance[1] = (dd.backbufferTransferFunction == FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB) ? 1.0f : 1000.0f;
    dd.generationRect = { 0, 0, (int32_t)S.bbW, (int32_t)S.bbH }; dd.frameID = S.frameId;
    const ffxReturnCode_t rcD = S.Dispatch(&S.ctx, &dd.header);
    S.frameId++;

    /* J3: uma linha so, na primeira vez que o dado real entra, com a convencao e a escala -- e o
       que permite o dono dizer se a imagem melhorou ou piorou. */
    if (usaJogo) { static bool disse = false; if (!disse) { disse = true;
        Log("fg: movimento do jogo (%s) render=%ux%u mv=%ux%u%s mvscale=%.4f,%.4f jitter=%.4f,%.4f depth_inv=%d mv_com_jitter=%d camera=%s",
            dj.origem, dj.renderW, dj.renderH, dj.mvW, dj.mvH, dj.mvDisplayRes ? " (tela)" : " (render)",
            dj.mvScaleX, dj.mvScaleY, dj.jitterX, dj.jitterY, dj.depthInv, dj.mvJittered ? 1 : 0,
            (dj.camNear > 0) ? "do jogo" : "suposta (0,1/1000/1rad)");
        Log("fg: convencao usada: MV em pixels de render depois de mvscale, +x direita, +y baixo, do quadro atual para o anterior"); } }

    /* J4: as faixas da sonda saem do frameB na mesma lista, com barreira explicita (NPSR ->
       COPY_SOURCE -> COMMON) em vez de contar com a promocao implicita depois de uma transicao
       explicita: uma barreira a mais e zero duvida sobre o estado em que o proxy volta ao jogo. */
    const bool sondar = S.sondaOk && !S.sondaDesligada && S.sonda[idx] != nullptr;
    D3D12_RESOURCE_BARRIER pos[4] = {
        Barr(frameB, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, sondar ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COMMON),
        Barr(out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
        Barr(texMv, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
        Barr(texDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON) };
    S.listaGer->ResourceBarrier(4, pos);
    if (sondar)
    {
        SondaGravar(idx, frameB);
        D3D12_RESOURCE_BARRIER volta = Barr(frameB, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        S.listaGer->ResourceBarrier(1, &volta);
    }
    if (FAILED(S.listaGer->Close())) return nullptr;
    ID3D12CommandList* ls[1] = { S.listaGer };
    S.fila->ExecuteCommandLists(1, ls);
    S.sondaGravada[idx] = sondar;       /* J4: so vale com a lista de fato submetida */
    S.gerFeito[idx] = S.readyVal + 1;   /* o Signal(fReady) logo apos esta chamada */
    if (rcP != FFX_API_RETURN_OK || rcD != FFX_API_RETURN_OK)
    {
        static int avisos = 0; if (avisos < 5) { ++avisos; Log("fg: ffxDispatch prepare=%u fg=%u no quadro %llu (quadro sem interpolado)", (unsigned)rcP, (unsigned)rcD, (unsigned long long)S.frameId); }
        return nullptr;
    }
    return out;
}

/* copia um quadro (interp ou proxy) para o backbuffer REAL e chama o Present
   original na fila do jogo. sync/flags: apresentamos com sync 0 para o pacing
   por QPC valer (a AMD faz o mesmo nos quadros pacejados). */
/* C9: resultado de ApresentarUm */
enum { AP_OK = 0, AP_PULOU, AP_DESISTIU };

/* C9: falha de apresentacao durante troca de tela: pula o quadro; so desiste (temporario) apos 2 s seguidos */
int FalhaTemporaria(const char* motivo)
{
    const int64_t agora = Qpc();
    int64_t desde = S.falhaDesde.load();
    if (desde == 0) { S.falhaDesde = agora; Log("fg: %s; pulando quadros ate a tela voltar", motivo); return AP_PULOU; }
    if (agora - desde < 2 * g_qpcFreq) return AP_PULOU;
    Desistir(motivo, true);
    return AP_DESISTIU;
}

struct EmApresentar { EmApresentar() { ++S.emApresentar; } ~EmApresentar() { --S.emApresentar; } };

int ApresentarUm(ID3D12Resource* quadro, void* origPresent, UINT flags)
{
    EmApresentar em;
    if (S.transicao.load()) return AP_PULOU;   /* C9: SetFullscreenState em curso */
    UINT realIdx = 0;
    IDXGISwapChain3* sc3 = nullptr;
    if (SUCCEEDED(S.sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3)) && sc3 != nullptr)
    { realIdx = S.origGetCurBB ? S.origGetCurBB(sc3) : sc3->GetCurrentBackBufferIndex(); sc3->Release(); }

    ID3D12Resource* realBB = nullptr;
    if (!S.origGetBuffer || FAILED(S.origGetBuffer(S.sc, realIdx, __uuidof(ID3D12Resource), (void**)&realBB)) || !realBB)
    return FalhaTemporaria("GetBuffer real no ritmo falhou");   /* C9: antes desistia de vez */

    /* C4: Reset so com a ultima copia deste allocator terminada na GPU */
    const UINT ia = realIdx % S.nBuf;
    if (!EsperarFence(S.fCopia, S.copiaFeita[ia], S.evCopia, 2000))
    { realBB->Release(); RelatarRemocao("copia"); Desistir("GPU nao terminou a copia anterior"); return AP_DESISTIU; }
    if (FAILED(S.allocCopia[ia]->Reset()) || FAILED(S.listaCopia->Reset(S.allocCopia[ia], nullptr)))
    { realBB->Release(); RelatarRemocao("copia"); Desistir("Reset da lista de copia falhou"); return AP_DESISTIU; }
    D3D12_RESOURCE_BARRIER b1[2] = {
        Barr(realBB, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST),
        Barr(quadro, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE) };
    S.listaCopia->ResourceBarrier(2, b1);
    S.listaCopia->CopyResource(realBB, quadro);
    Tela::GravarNaCopia(S.listaCopia, realBB);   /* C2: realBB em COPY_DEST */
    D3D12_RESOURCE_BARRIER b2[2] = {
        Barr(realBB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT),
        Barr(quadro, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON) };
    S.listaCopia->ResourceBarrier(2, b2);
    S.listaCopia->Close();
    ID3D12CommandList* ls[1] = { S.listaCopia };
    S.fila->ExecuteCommandLists(1, ls);
    S.fila->Signal(S.fCopia, ++S.copiaVal);
    S.copiaFeita[ia] = S.copiaVal;
    realBB->Release();

    /* sync 0 + tearing exige a flag; C9: em tela cheia exclusiva a flag e proibida (INVALID_CALL) */
    const UINT fl = flags | ((S.tearing && !S.telaCheia.load()) ? DXGI_PRESENT_ALLOW_TEARING : 0);
    HRESULT hr = ((PresentFn)origPresent)(S.sc, 0, fl);
    if (hr == DXGI_ERROR_INVALID_CALL && (fl & DXGI_PRESENT_ALLOW_TEARING))
    {
        S.telaCheia = true;   /* C9: entrou em tela cheia sem passar pelo nosso hook (Alt+Enter do DXGI) */
        Log("fg: Present com tearing recusado; apresentando sem a flag (tela cheia exclusiva)");
        hr = ((PresentFn)origPresent)(S.sc, 0, flags);
    }
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    { RelatarRemocao("Present"); Desistir("device removido no Present"); return AP_DESISTIU; }
    if (FAILED(hr)) return FalhaTemporaria("Present no ritmo falhou");   /* C9 */
    S.falhaDesde = 0;
    S.ultimoPresentQpc = Qpc();
    ++S.janPresents; ++S.presentsTela;
    return AP_OK;
}

/* C10: copia o proxy do indice real para o backbuffer real (Present direto, sem interpolar).
   Qualquer falha so pula a copia deste quadro. */
bool CopiarEspelho()
{
    if (!S.fila || !S.listaCopia || !S.fCopia || !S.sc || !S.origGetBuffer) return false;
    if (S.dev && S.dev->GetDeviceRemovedReason() != S_OK) { S.espelho = false; return false; }
    IDXGISwapChain3* sc3 = nullptr;
    if (FAILED(S.sc->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3)) || !sc3) return false;
    const UINT realIdx = S.origGetCurBB ? S.origGetCurBB(sc3) : sc3->GetCurrentBackBufferIndex();
    sc3->Release();
    if (realIdx >= S.nBuf || !S.proxy[realIdx] || !S.allocCopia[realIdx]) return false;
    if (!EsperarFence(S.fCopia, S.copiaFeita[realIdx], S.evCopia, 1000)) return false;   /* C4: Reset so com a copia anterior feita */
    ID3D12Resource* realBB = nullptr;
    if (FAILED(S.origGetBuffer(S.sc, realIdx, __uuidof(ID3D12Resource), (void**)&realBB)) || !realBB) return false;
    const bool ok = SUCCEEDED(S.allocCopia[realIdx]->Reset()) && SUCCEEDED(S.listaCopia->Reset(S.allocCopia[realIdx], nullptr));
    if (ok)
    {
        ID3D12Resource* quadro = S.proxy[realIdx];
        D3D12_RESOURCE_BARRIER b1[2] = {
            Barr(realBB, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST),
            Barr(quadro, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE) };
        S.listaCopia->ResourceBarrier(2, b1);
        S.listaCopia->CopyResource(realBB, quadro);
        Tela::GravarNaCopia(S.listaCopia, realBB);   /* C2: o contador vai junto (o Desenhar pegaria o proxy) */
        D3D12_RESOURCE_BARRIER b2[2] = {
            Barr(realBB, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT),
            Barr(quadro, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON) };
        S.listaCopia->ResourceBarrier(2, b2);
        if (SUCCEEDED(S.listaCopia->Close()))
        {
            ID3D12CommandList* ls[1] = { S.listaCopia };
            S.fila->ExecuteCommandLists(1, ls);
            S.fila->Signal(S.fCopia, ++S.copiaVal);
            S.copiaFeita[realIdx] = S.copiaVal;
            if (S.espelhados++ == 0) Log("fg: Present direto copiando o quadro do jogo dos proxies (ate o proximo ResizeBuffers)");
        }
        else { realBB->Release(); return false; }
    }
    realBB->Release();
    return ok;
}

void* g_origPresentParaRitmo = nullptr;

/* bancada (C10): DLSS5_FG_BANCADA_DESISTIR=<s> faz o ritmo desistir s segundos depois de assumir */
int64_t PrazoBancada()
{
    char v[16] = "";
    const DWORD n = GetEnvironmentVariableA("DLSS5_FG_BANCADA_DESISTIR", v, sizeof v);
    const int s = (n > 0 && n < sizeof v) ? atoi(v) : 0;
    return s > 0 ? Qpc() + (int64_t)s * g_qpcFreq : 0;
}

void LacoRitmo()
{
    const int64_t prazoBancada = PrazoBancada();
    while (!S.parar.load())
    {
        if (prazoBancada != 0 && Qpc() >= prazoBancada) { Desistir("bancada: desistencia forcada"); break; }
        Entrada e;
        {
            std::unique_lock<std::mutex> lk(S.muFila);
            S.cvFila.wait(lk, [] { return S.parar.load() || !S.fila_.empty(); });
            if (S.parar.load()) break;
            e = S.fila_.front(); S.fila_.pop_front();
        }
        const int64_t t0 = Qpc();
        /* o quadro so pode ser copiado depois de a GPU do jogo ter desenhado nele
           (e o FFX ter gerado o interpolado): espera de GPU pela fila de copia. */
        bool pronto = S.fReady->GetCompletedValue() >= e.readyFence;
        if (!pronto)
        {
            /* J4: evento pre-criado (auto-reset). Um sinal velho, de uma espera anterior que
               estourou o teto, so acorda esta cedo uma vez: a fence e conferida de novo. */
            if (S.evRitmo && SUCCEEDED(S.fReady->SetEventOnCompletion(e.readyFence, S.evRitmo)))
            {
                const int64_t limite = Qpc() + g_qpcFreq / 2;
                do { WaitForSingleObject(S.evRitmo, 500); pronto = S.fReady->GetCompletedValue() >= e.readyFence; }
                while (!pronto && Qpc() < limite);
            }
            if (!pronto && DeviceCaiu("ritmo")) break;   /* C5 */
        }

        /* J4: a sonda decide, com o quadro ja na GPU e ANTES de qualquer Present deste par, se o
           gerado sai. Corte declarado pelo jogo (J3) ja chegou com temInterp=false; aqui entram o
           corte que o jogo nao declarou e a imagem parada. A analise roda mesmo sem gerado, para
           a historia (medias, hash) nao pular quadro. */
        if (e.temSonda && pronto)
        {
            bool corteSonda = false, igual = false;
            SondaAnalisar(e.idx, e.dtMs, &corteSonda, &igual);
            /* o log de "parada" e por transicao (entrar em pausa), o de corte por ocorrencia; os dois com teto */
            static bool igualAnt = false; static unsigned nPar = 0, nCorte = 0;
            if (igual && !igualAnt && nPar < 20) { ++nPar; Log("fg: imagem parada - geracao em pausa (quadros identicos; o real e apresentado)"); }
            igualAnt = igual;
            if (e.temInterp && (corteSonda || igual))
            {
                e.temInterp = false;
                if (igual) ++S.paradosSonda;
                else { ++S.cortesSonda; if (nCorte < 20) { ++nCorte; Log("fg: corte de cena visto pela sonda - quadro real apresentado, sem gerado"); } }
            }
        }

        /* 1) o interpolado, no ritmo natural */
        if (e.temInterp && e.interp && ApresentarUm(e.interp, g_origPresentParaRitmo, 0) == AP_DESISTIU) break;

        /* 2) espera metade do intervalo medido do jogo, e apresenta o real */
        const int64_t alvo = (S.ultimoPresentQpc != 0)
            ? S.ultimoPresentQpc + (int64_t)(g_qpcFreq * ((e.dtMs < 50.0 ? e.dtMs : 50.0) / 2000.0))   /* C8: pausa longa do jogo nao faz o ritmo dormir segundos */
            : Qpc();
        EsperarQpc(alvo);
        if (ApresentarUm(e.real, g_origPresentParaRitmo, 0) == AP_DESISTIU) break;

        /* o proxy foi consumido: libera a contrapressao do jogo */
        S.fila->Signal(S.fConsumido, e.consumaVal);
        S.janRitmo += Ms(t0, Qpc());
    }
}

/* J4: falha de hardware na thread de ritmo. E thread NOSSA: sem a guarda o processo do jogo
   morria com ela. O efeito e o mesmo da excecao C++ logo abaixo - desistir do FG -, e nao
   "ficar inerte": o jogo esta desenhando nos proxies, e quem o mantem na tela e o espelho do
   Present direto (C10), que roda na thread do jogo sob a guarda do HkPresent. O que o longjmp
   pode ter deixado para tras (muFila presa, lista de comandos aberta) e tratado por `estourou`
   em PedirParada e Desmontar. Registrado no log e no erros-nativo.log, sem `inerte=1`. */
void RitmoEstourou(DWORD code, void* addr)
{
    S.estourou = true;
    HMODULE self = nullptr; unsigned long long desl = 0;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&RitmoEstourou), &self) && self && addr && addr >= (void*)self)
        desl = (unsigned long long)((const char*)addr - (const char*)self);
    Log("fg: ESTOURO na thread de ritmo - excecao 0x%08X em %p (dlss5-ponte64.dll+0x%llX); o FG desiste e o jogo segue pelo Present direto",
        (unsigned)code, addr, desl);
    if (E) E("FG thread de ritmo", code, addr, "o frame generation desistiu; o Present direto copia o quadro do jogo (espelho) e o resto da DLL segue de pe.");
    Desistir("estouro na thread de ritmo");
}

/* C3: nenhuma excecao sai da thread (std::terminate); desiste e deixa o desmonte para fora.
   J4: e nenhuma falha de hardware tambem (PtGuarda; ver ponte_seh.h - armar custa um _setjmp,
   uma vez por vida da thread). */
void ThreadRitmo()
{
    S.idRitmo = GetCurrentThreadId();
    PtGuarda g;
    if (PT_GUARDA_ARMAR(g)) RitmoEstourou(g.code, g.addr);
    else
    {
        try { LacoRitmo(); }
        catch (...) { Desistir("excecao na thread de ritmo"); }
        PtGuardaDesarmar(&g);
    }
    S.idRitmo = 0;
}

} // namespace anonimo

/* ------------------------------------------------------------------ API publica.
   Fica FORA do namespace anonimo (as declaracoes do fg_hook.h tem ligacao
   externa); os ajudantes acima, em namespace anonimo dentro de Fg, continuam
   visiveis daqui. */
void Iniciar(LogFn log, EstouroFn estouro) { L = log; E = estouro; }

bool ImagemParada() { return S.dono.load() && S.parado.load(); }   /* J4: so com o FG dono a sonda ve os quadros */

void Originais(void* gb, void* cbb) { S.origGetBuffer = (GetBufferFn)gb; S.origGetCurBB = (GetCurBBFn)cbb; }

/* ------------------------------------------------------------------ J3: o balcao dos dados.
   Chamado da THREAD DO JOGO, dentro do dispatch do upscaler, depois de a lista de copia ter sido
   submetida. Custo: uma trava sem disputa e um par de AddRef/Release. Com o FG desligado no
   controle (fg=0) nem isso: QuerDados corta antes. */
bool QuerDados() { return S.modoVisto.load() != 0 && !S.descarga.load(); }

void PublicarDados(const DadosJogo& d)
{
    if (!QuerDados()) return;
    try
    {
        ID3D12Resource* dp = d.depth; ID3D12Resource* mv = d.mv;
        if (dp) dp->AddRef();
        if (mv) mv->AddRef();
        ID3D12Resource* velhoD = nullptr; ID3D12Resource* velhoM = nullptr;
        {
            std::lock_guard<std::mutex> g(S.muDados);
            velhoD = S.dados.depth; velhoM = S.dados.mv;
            S.dados = d; S.dados.depth = dp; S.dados.mv = mv;
            ++S.dadosSeq; S.dadosQpc = Qpc();
        }
        Soltar(velhoD); Soltar(velhoM);   /* fora da trava: Release pode ser a ultima */
    }
    catch (...) {}
}

void LimparDados()
{
    try
    {
        ID3D12Resource* velhoD = nullptr; ID3D12Resource* velhoM = nullptr;
        {
            std::lock_guard<std::mutex> g(S.muDados);
            velhoD = S.dados.depth; velhoM = S.dados.mv;
            S.dados = DadosJogo();
        }
        Soltar(velhoD); Soltar(velhoM);
    }
    catch (...) {}
}

/* C11: filas DIRECT vistas no ExecuteCommandLists, com referencia (soltas no Parar) */
const int kMaxFilas = 4;
static std::atomic<ID3D12CommandQueue*> g_filas[kMaxFilas];

void FilaVista(ID3D12CommandQueue* fila)
{
    if (!fila) return;
    for (auto& v : g_filas) { const ID3D12CommandQueue* a = v.load(); if (a == fila) return; if (!a) break; }
    if (g_filas[kMaxFilas - 1].load() || fila->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
    std::lock_guard<std::mutex> g(S.muFilaJogo);
    for (auto& v : g_filas)
    {
        const ID3D12CommandQueue* a = v.load();
        if (a == fila) break;
        if (!a) { fila->AddRef(); v = fila; break; }
    }
    if (!S.fila) { fila->AddRef(); S.fila = fila; }   /* reserva: a primeira DIRECT vista */
}

/* C11: o objeto (memoria legivel ate 0x2000 bytes) guarda o ponteiro alvo? So compara valores. */
static bool GuardaPonteiro(IUnknown* obj, const void* alvo)
{
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(obj, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return false;
    const char* ini = (const char*)obj;
    const size_t resto = (size_t)((const char*)mbi.BaseAddress + mbi.RegionSize - ini);
    const size_t n = resto < 0x2000 ? resto : 0x2000;
    for (size_t o = 0; o + sizeof(void*) <= n; o += sizeof(void*))
    {
        void* p; memcpy(&p, ini + o, sizeof p);
        if (p == alvo) return true;
    }
    return false;
}

/* C11: a fila em que o swapchain apresenta, com referencia; nullptr = nao achou (fica a reserva).
   O GetDevice com ID3D12CommandQueue e o caminho documentado, mas no Windows 11 26200 devolve
   E_NOINTERFACE; entao, entre as filas DIRECT vistas, vale a unica cujo ponteiro o swapchain guarda. */
ID3D12CommandQueue* FilaDoSwapchain(IDXGISwapChain* sc)
{
    if (!sc) return nullptr;
    ID3D12CommandQueue* q = nullptr;
    if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D12CommandQueue), (void**)&q)) && q) return q;
    q = nullptr;
    int achadas = 0;
    for (auto& v : g_filas)
    {
        ID3D12CommandQueue* a = v.load();
        if (!a) break;
        if (GuardaPonteiro(sc, a)) { q = a; ++achadas; }
    }
    if (achadas != 1) return nullptr;
    q->AddRef();
    return q;
}

/* C11: so chamado com o FG desmontado (nenhuma thread usando S.fila).
   B19: filaPedida = a fila que o jogo passou no ResizeBuffers1 (a mesma para todos os buffers):
   e a fila em que o swapchain apresenta, sem precisar da busca. */
static void AdotarFilaDoSwapchain(IDXGISwapChain* sc, IUnknown* filaPedida)
{
    ID3D12CommandQueue* q = nullptr;
    if (filaPedida && SUCCEEDED(filaPedida->QueryInterface(__uuidof(ID3D12CommandQueue), (void**)&q)) && q &&
        q->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        Soltar(q);
    if (!q) q = FilaDoSwapchain(sc);
    if (!q)
    {
        if (S.fila && !S.filaDoSc && !S.avisouReserva)
        { S.avisouReserva = true; Log("fg: fila do swapchain nao encontrada; usando a primeira fila DIRECT vista (reserva)"); }
        return;
    }
    std::lock_guard<std::mutex> g(S.muFilaJogo);
    if (q != S.fila)
    {
        Log("fg: fila do jogo pelo swapchain=0x%p%s", (void*)q, S.fila ? " (a primeira DIRECT vista era outra)" : "");
        Soltar(S.fila); S.fila = q;
    }
    else
    {
        if (!S.filaDoSc) Log("fg: fila do jogo pelo swapchain=0x%p (igual a vista)", (void*)q);
        q->Release();
    }
    S.filaDoSc = true;
}

bool DonoDaApresentacao() { return S.dono.load(); }

void AntesResize()
{
    /* C3: desmonta sempre que houver thread viva ou recursos, nao so com S.dono (dois resizes
       seguidos, ou o ritmo que desistiu e deixou o desmonte pendente) */
    try
    {
        if (!S.dono.load() && !Montado()) return;
        const int64_t t0 = Qpc();
        Log("fg: resize - desmontando");
        S.dono = false; S.espelho = false;   /* C10: o jogo solta os proxies no resize */
        Desmontar();   /* sera remontado no AposResize */
        Log("fg: resize - desmontado em %.0f ms", Ms(t0, Qpc()));
    }
    catch (...) { Desistir("excecao ao desmontar no resize"); }
}

static void AposResizeCorpo(IDXGISwapChain* sc, IUnknown* filaPedida, bool filasMistas)
{
    /* C3: AposResize sem AntesResize (ou com o anterior ainda montado): desmonta antes */
    if (Montado()) { S.dono = false; S.espelho = false; Desmontar(); }
    if (S.descarga.load()) return;   /* C13 */
    if (S.desistido.load())
    {
        if (!S.temporario.load()) return;
        S.desistido = false; S.temporario = false;   /* C9: a tela voltou; tenta assumir de novo */
        Log("fg: troca de tela concluida; tentando assumir de novo");
    }
    LerControle();
    int modo; { std::lock_guard<std::mutex> g(S.muCtl); modo = S.modo; }
    if (modo == 0) { Log("fg: desligado (fg=0 no controle)"); return; }
    /* J2 (18/09/2026): O AUTO DECIDE POR DISPONIBILIDADE, NAO PELO UPSCALER DO JOGO.
       Ate aqui o Auto recusava quando a sessao .up nao estava de pe (`Ngx::Up::Ativo()`), isto
       e, quando o jogo nao chamava DLSS. So que o interpolador nao usa MV nem profundidade do
       jogo: ele roda em fluxo optico (ver FonteMovimento). O Auto recusava por falta de um dado
       que o FG nem consome, e por isso ficava parado PARA SEMPRE em jogo com FSR 1 (o RE2 desta
       maquina) e em placa GTX ou Radeon. Os dados reais do jogo, quando chegarem, mudam a
       QUALIDADE - e o card diz qual esta valendo -, nao o liga/desliga.
       O que ainda impede de verdade continua impedindo, e cada um com o seu motivo registrado:
       D3D11 (Servico), filas mistas, fila do jogo ainda desconhecida e falha ao montar - logo
       abaixo -, e a geracao de quadros do proprio jogo, aqui. */
    S.usaUp = Ngx::Up::Ativo();
    if (modo == 2)
        if (const char* quem = FgDoJogo())
        {
            /* Nao empilhamos geracao sobre geracao. So no Auto: escolher "Ligado" e decisao
               explicita do dono e continua valendo (o modulo pode estar carregado sem a
               geracao do jogo estar em uso). */
            Log("fg: auto -- o jogo ja gera quadros (%s); nao empilho. Escolha Ligado para usar a nossa mesmo assim", quem);
            return;
        }
    /* B19: o FG copia e apresenta numa fila so */
    if (filasMistas) { Log("fg: ResizeBuffers1 com filas ou nos diferentes por buffer; nao assume a apresentacao"); S.falhouMontar = true; return; }
    AdotarFilaDoSwapchain(sc, filaPedida);   /* C11 */
    if (!S.fila) { Log("fg: aguardando a fila do jogo (ExecuteCommandLists)"); return; }

    S.sc = sc;
    if (!Montar()) { Log("fg: nao assumiu a apresentacao (%s)", S.motivo); Desmontar(); S.falhouMontar = true; return; }
    S.falhouMontar = false;
    IniciarRitmo();
    S.dono = true;
    Log("fg: dono da apresentacao (buffers=%u fmt=%u %ux%u)", S.nBuf, (unsigned)S.bbFmt, S.bbW, S.bbH);
    Log("fg: ligado (movimento=%s)%s", S.ctxDoJogo
        ? "do jogo - profundidade e vetores reais do upscaler"
        : "optico - o interpolador estima pela propria imagem",
        (!S.ctxDoJogo && S.usaUp) ? " | a sessao .up esta de pe mas nao entregou material a tempo da montagem; alterne a tela cheia de novo para usar o movimento do jogo" : "");
}

void AposResize(IDXGISwapChain* sc, IUnknown* filaPedida, bool filasMistas)
{
    try { AposResizeCorpo(sc, filaPedida, filasMistas); }
    catch (...) { Desistir("excecao ao assumir no resize"); }
}

/* C3: o ritmo desistiu e nao pode se desmontar sozinho; a thread do Present (sem FG dono) desmonta */
void Recolher()
{
    if (!S.pendente.load() || S.dono.load()) return;
    try { Desmontar(); } catch (...) {}
}

/* C10: thread do Present, FG nao dono: depois de desistir, leva o quadro do proxy ao backbuffer real */
bool Espelhar(IDXGISwapChain* sc)
{
    if (!S.espelho.load() || S.dono.load() || S.pendente.load() || sc != S.sc) return false;
    try { return CopiarEspelho(); } catch (...) { return false; }
}

/* GetBuffer: quando FG e dono, o jogo recebe NOSSO proxy no lugar do backbuffer.
   C10: tambem depois de desistir, ate o proximo ResizeBuffers (o jogo guarda os proxies). */
bool GetBufferProxy(IDXGISwapChain* sc, UINT buf, REFIID riid, void** pp)
{
    if (!(S.dono.load() || S.espelho.load()) || sc != S.sc) return false;
    if (buf >= S.nBuf) return false;
    if (riid != __uuidof(ID3D12Resource) && riid != __uuidof(ID3D12Resource1) && riid != __uuidof(ID3D12Resource2)) return false;
    ID3D12Resource* p = S.proxy[buf];
    if (!p) return false;
    p->AddRef(); *pp = p;
    return true;
}

bool IndiceProxy(UINT* out)
{
    if (!S.dono.load()) return false;
    *out = S.gameIdx.load();
    return true;
}

/* C5/C6: o FG desistiu no meio do quadro (recursos ja soltos); o Presentar faz o Present direto */
static const HRESULT kDesistiu = E_ABORT;

static HRESULT PresentarDono(IDXGISwapChain* sc, UINT sync, UINT flags, void* origPresent)
{
    (void)sync;
    g_origPresentParaRitmo = origPresent;
    const int64_t t0 = Qpc();

    const UINT idx = S.gameIdx.load();
    ID3D12Resource* frameB = S.proxy[idx];

    /* intervalo medido do jogo (J4: em S, zerado no Montar) */
    double dtMs = 16.6;
    if (S.ultimoJogo != 0) dtMs = Ms(S.ultimoJogo, t0);
    S.ultimoJogo = t0;

    /* o jogo terminou de desenhar em frameB (submeteu antes do Present): marca "pronto" */
    bool corte = false;
    ID3D12Resource* interp = GerarInterpolado(idx, frameB, dtMs, &corte);
    if (!interp && DeviceCaiu("geracao")) return kDesistiu;   /* C5 */
    S.fila->Signal(S.fReady, ++S.readyVal);

    Entrada e;
    /* J3: num corte de cena o gerado sai (o FFX precisa do dispatch para zerar a historia), mas
       nao vai para a tela: interpolar duas cenas sem relacao e o pior artefato medido. */
    e.real = frameB; e.interp = interp; e.temInterp = (interp != nullptr) && !corte;
    e.readyFence = S.readyVal; e.dtMs = dtMs; e.consumaVal = ++S.consumaVal;
    e.idx = idx; e.temSonda = S.sondaGravada[idx];   /* J4: o ritmo le as faixas deste conjunto */
    S.proxyLivreEm[idx] = e.consumaVal;

    {
        std::unique_lock<std::mutex> lk(S.muFila);
        /* contrapressao: no maximo nBuf-1 quadros na fila; C6: com teto de 2 s (ritmo parado) */
        const int64_t limite = t0 + 2 * g_qpcFreq;
        bool parado = false;
        while (!S.parar.load() && S.fila_.size() >= (size_t)(S.nBuf > 1 ? S.nBuf - 1 : 1))
        {
            if (Qpc() > limite) { parado = true; break; }
            lk.unlock(); Sleep(0); lk.lock();
        }
        if (parado)
        {
            lk.unlock();
            if (!DeviceCaiu("contrapressao")) Desistir("ritmo parado");
            return kDesistiu;
        }
        S.fila_.push_back(e);
    }
    S.cvFila.notify_one();

    /* avanca o indice do jogo e espera o proxio proxy estar livre (consumido) */
    const UINT prox = (idx + 1) % S.nBuf;
    S.gameIdx = prox;
    if (S.proxyLivreEm[prox] != 0 && !EsperarFence(S.fConsumido, S.proxyLivreEm[prox], S.evConsumido, 500) &&
        DeviceCaiu("consumo"))   /* C5 */
        return kDesistiu;

    /* estatisticas */
    ++S.presentsJogo;   /* J1: um quadro do jogo; o par na tela e o presentsTela */
    S.janInterp += Ms(t0, Qpc()); S.janDt += dtMs;
    if (S.janN == 0) S.janInicio = t0;
    if (++S.janN >= 300)
    {
        const double seg = Ms(S.janInicio, Qpc()) / 1000.0;
        const double fpsJogo = seg > 0 ? S.janN / seg : 0.0;
        const double fpsTela = seg > 0 ? S.janPresents / seg : 0.0;
        /* J4: sonda = custo medio da leitura na thread de ritmo (us) e quantos gerados ela segurou
           nesta janela por corte de cena e por imagem parada (0 quando a sonda esta fora) */
        Log("fg: 300 quadros | interp %.2f ms | ritmo %.2f ms | latencia +%.2f ms | fps jogo %.1f -> tela %.1f | movimento do jogo em %u/%llu | sonda %d us, segurou %u por corte e %u parados",
            S.janInterp / S.janN, S.janRitmo / (S.janPresents ? S.janPresents : 1), (S.janDt / S.janN) / 2.0, fpsJogo, fpsTela,
            S.janJogo, (unsigned long long)S.janN, S.sondaUsPub.load(), S.cortesSonda.exchange(0), S.paradosSonda.exchange(0));
        S.janN = 0; S.janPresents = 0; S.janJogo = 0; S.janInterp = S.janRitmo = S.janDt = 0;
    }
    return S_OK;
}

HRESULT Presentar(IDXGISwapChain* sc, UINT sync, UINT flags, void* origPresent)
{
    if (!S.dono.load() || sc != S.sc) return ((PresentFn)origPresent)(sc, sync, flags);
    /* C3: excecao no nosso caminho: desiste e volta ao Present direto */
    try { const HRESULT hr = PresentarDono(sc, sync, flags, origPresent); if (hr != kDesistiu) return hr; }
    catch (...) { Desistir("excecao no Present"); }
    return ((PresentFn)origPresent)(sc, sync, flags);
}

void Desmontar()
{
    /* para a thread de ritmo; C3: nunca join da propria thread (std::system_error -> terminate) */
    if (S.thRitmo.joinable())
    {
        PedirParada();
        if (NaThreadRitmo()) { S.pendente = true; return; }
        try { S.thRitmo.join(); } catch (...) { try { S.thRitmo.detach(); } catch (...) {} }
        Log("fg: thread de ritmo parada");
    }
    S.pendente = false;
    if (S.estourou.load())
    {
        /* J4: o ritmo saiu por longjmp. A muFila pode estar presa com ele (nao pegar; a fila e
           abandonada) e a lista de copia pode ter ficado ABERTA no meio de uma gravacao: um Reset
           em lista aberta falha e o espelho (C10) nunca copiaria. Close em lista fechada so
           devolve erro. */
        if (S.listaCopia) S.listaCopia->Close();
        if (S.listaGer) S.listaGer->Close();
    }
    else { try { std::lock_guard<std::mutex> lk(S.muFila); S.fila_.clear(); } catch (...) {} }

    /* espera a GPU terminar o que enfileiramos, para soltar com seguranca */
    bool gpuLivre = true;
    if (S.fila && S.fCopia)
    {
        S.fila->Signal(S.fCopia, ++S.copiaVal);
        gpuLivre = EsperarFence(S.fCopia, S.copiaVal, S.evCopia, 5000);
    }
    RelatarRemocao("desmonte");
    if (!gpuLivre)
    {
        /* C4: soltar recurso ainda na GPU e uso-apos-liberar; vaza este conjunto e segue */
        Log("fg: GPU nao terminou em 5 s; recursos do FG deixados sem soltar");
        S.espelho = false;
        S.ctx = nullptr;
        for (UINT i = 0; i < kMaxBuf; ++i) { S.proxy[i] = nullptr; S.allocCopia[i] = nullptr; S.allocGer[i] = nullptr; S.interpTex[i] = nullptr; }
        /* J3: as referencias ao depth/MV do jogo tambem vazam junto -- soltar recurso com a GPU
           ainda lendo e o mesmo uso-apos-liberar, venha ele de onde vier. */
        for (UINT i = 0; i < kMaxBuf; ++i) { S.srcDepth[i] = nullptr; S.srcMv[i] = nullptr; S.sonda[i] = nullptr; }   /* J4: a sonda vaza junto */
        S.depthConst = S.mvZero = nullptr; S.fgDepth = S.fgMv = nullptr; S.listaGer = S.listaCopia = nullptr;
        S.fReady = S.fConsumido = S.fCopia = nullptr;
    }
    SondaSoltar();   /* J4: buffers de leitura (ja nulos no caminho acima) e o estado do detector */
    /* C10: depois de desistir ficam os proxies, a lista de copia e o device (o espelho do Present direto);
       o resize e o Parar desligam o espelho antes e soltam tudo */
    if (S.espelho.load() && S.dev && S.dev->GetDeviceRemovedReason() != S_OK) S.espelho = false;
    const bool manter = S.espelho.load() && S.proxy[0] && S.listaCopia && S.fCopia;
    if (S.espelho.load() && !manter) S.espelho = false;
    if (manter) Log("fg: proxies mantidos; o Present direto copia o quadro do jogo ate o proximo ResizeBuffers");
    if (S.ctx && S.Destroy) { S.Destroy(&S.ctx, nullptr); S.ctx = nullptr; }
    for (UINT i = 0; i < kMaxBuf; ++i)
    {
        if (!manter) { Soltar(S.proxy[i]); Soltar(S.allocCopia[i]); }
        Soltar(S.allocGer[i]); Soltar(S.interpTex[i]);
        Soltar(S.srcDepth[i]); Soltar(S.srcMv[i]);   /* J3: refs do conjunto, com a GPU ja parada */
        S.proxyLivreEm[i] = 0; S.gerFeito[i] = S.copiaFeita[i] = 0;
    }
    Soltar(S.depthConst); Soltar(S.mvZero);
    Soltar(S.fgDepth); Soltar(S.fgMv);
    S.ctxDoJogo = false; S.movJogoQpc = 0;
    Soltar(S.listaGer);
    Soltar(S.fReady); Soltar(S.fConsumido);
    FecharH(S.evConsumido); FecharH(S.evReady); FecharH(S.evRitmo);   /* J4 */
    if (!manter) { Soltar(S.listaCopia); Soltar(S.fCopia); FecharH(S.evCopia); }
    /* C4: o FidelityFX fica carregado (Parar descarrega); descarregar/recarregar a cada resize nao ganha nada */
    S.Create = nullptr; S.Destroy = nullptr; S.Dispatch = nullptr; S.Query = nullptr; S.Config = nullptr;
    if (!manter) Soltar(S.dev);   /* C4: solta so a referencia do GetDevice do Montar (antes vazava uma por remontagem) */
}

/* J1: a janela de 1 s dos dois numeros do card. Roda na thread da ponte (250 ms), e o app le
   o .ngx-estado uma vez por segundo - os dois ritmos casam. Sem FG dono, os numeros zeram:
   numero velho na tela e pior do que numero nenhum. */
static void MedirFps()
{
    const int64_t agora = Qpc();
    const uint64_t j = S.presentsJogo.load(), t = S.presentsTela.load();
    if (!S.dono.load())
    {
        S.medQpc = agora; S.medJogo = j; S.medTela = t;
        const bool mudou = S.fpsJogoPub.exchange(0) != 0 || S.fpsTelaPub.exchange(0) != 0;
        S.movJogoQpc = 0;   /* J3: sem FG dono nao ha fonte de movimento a declarar */
        if (mudou) ++S.estadoVersao;
        return;
    }
    if (S.medQpc == 0) { S.medQpc = agora; S.medJogo = j; S.medTela = t; return; }
    const double seg = Ms(S.medQpc, agora) / 1000.0;
    if (seg < 1.0) return;
    const int fj = (int)((double)(j - S.medJogo) / seg + 0.5);
    const int ft = (int)((double)(t - S.medTela) / seg + 0.5);
    S.medQpc = agora; S.medJogo = j; S.medTela = t;
    const bool mudou = S.fpsJogoPub.exchange(fj) != fj || S.fpsTelaPub.exchange(ft) != ft;
    if (mudou) ++S.estadoVersao;
}

static void ServicoCorpo(bool jogoD3D11)
{
    LerControle();
    MedirFps();   /* J1 */
    /* P1: o que o card do app mostra. "aguardando" = pedido, mas so assume num ResizeBuffers do
       jogo (Alt+Enter); desligar com o FG dono tambem so vale no proximo ResizeBuffers. */
    const int modo = S.modoVisto.load();
    if (S.dono.load())                    PublicarEstado("ligado", modo == 0 ? "desligar_alt_enter" : FonteMovimento());
    else if (S.desistido.load())          PublicarEstado(S.temporario.load() ? "aguardando" : "desligado", S.temporario.load() ? "tela" : "falha");
    else if (modo == 0)                   PublicarEstado("desligado", "fg0");
    else if (jogoD3D11)                   PublicarEstado("desligado", "d3d11");
    /* J2: o motivo "auto_sem_dados" morreu aqui - ele recusava por falta de um dado que o FG
       nem usa. O que sobra e a geracao de quadros do proprio jogo, e so no Auto. */
    else if (modo == 2 && FgDoJogo())     PublicarEstado("aguardando", "fg_do_jogo");
    else                                  PublicarEstado("aguardando", S.falhouMontar ? "falhou" : "alt_enter");
}

void Servico(bool jogoD3D11)
{
    try { ServicoCorpo(jogoD3D11); } catch (...) {}   /* C3: nada escapa para a thread da ponte */
}

unsigned EstadoVersao() { return S.estadoVersao.load(); }

/* C9: SetFullscreenState pelo nosso hook. O ritmo nao apresenta durante a troca (sem trava: so
   pula quadros, para nao prender a thread da janela contra o join de um resize) */
void AntesTelaCheia()
{
    if (!S.dono.load()) return;
    S.transicao = true;
    const int64_t limite = Qpc() + g_qpcFreq / 10;
    while (S.emApresentar.load() > 0 && Qpc() < limite) Sleep(1);
    Log("fg: troca de tela cheia - ritmo em pausa");
}

void AposTelaCheia(IDXGISwapChain* sc)
{
    if (!S.transicao.exchange(false)) return;
    BOOL fs = FALSE;
    if (sc && sc == S.sc && SUCCEEDED(sc->GetFullscreenState(&fs, nullptr))) S.telaCheia = fs != FALSE;
    S.falhaDesde = 0;
    Log("fg: troca de tela cheia concluida (tela cheia=%d); ritmo segue", S.telaCheia.load() ? 1 : 0);
}

uint64_t PresentsNaTela() { return S.presentsTela.load(); }

/* C13: so com os hooks drenados (nenhum Present do jogo dentro do FG) */
bool JogoNosProxies()
{
    if (!(S.dono.load() || S.espelho.load()) || !S.proxy[0]) return false;
    S.proxy[0]->AddRef();
    return S.proxy[0]->Release() > 1;   /* alem da nossa */
}

void Largar()
{
    S.descarga = true;
    try
    {
        if (S.dono.load()) Desistir("ponte descarregando");
        if (S.dono.load()) { S.espelho = (S.proxy[0] != nullptr); S.dono = false; Desmontar(); }   /* ja desistido antes */
        else if (S.pendente.load()) Desmontar();
    }
    catch (...) {}
}

bool Espelhando() { return S.espelho.load(); }

static_assert(sizeof(Espelho::proxy) / sizeof(Espelho::proxy[0]) == kMaxBuf, "Espelho com kMaxBuf proxies");

bool Entregar(Espelho* e)
{
    if (!S.espelho.load() || S.dono.load() || S.pendente.load() || S.thRitmo.joinable() ||
        !S.dev || !S.sc || !S.fila || !S.listaCopia || !S.fCopia || !S.proxy[0] || S.nBuf > kMaxBuf) return false;
    *e = Espelho();
    e->dev = S.dev; e->sc = S.sc; e->fila = S.fila; e->listaCopia = S.listaCopia; e->fCopia = S.fCopia;
    e->evCopia = S.evCopia; e->copiaVal = S.copiaVal; e->nBuf = S.nBuf;
    for (UINT i = 0; i < kMaxBuf; ++i)
    {
        e->proxy[i] = S.proxy[i]; e->allocCopia[i] = S.allocCopia[i]; e->copiaFeita[i] = S.copiaFeita[i];
        S.proxy[i] = nullptr; S.allocCopia[i] = nullptr;
    }
    S.espelho = false;
    S.dev = nullptr; S.fila = nullptr; S.listaCopia = nullptr; S.fCopia = nullptr; S.evCopia = nullptr;
    Log("fg: espelho entregue a copia da dll (%u proxies, %llu quadros espelhados)", e->nBuf, (unsigned long long)S.espelhados);
    return true;
}

void Herdar(const Espelho& e)
{
    S.dev = e.dev; S.sc = e.sc; S.fila = e.fila; S.listaCopia = e.listaCopia; S.fCopia = e.fCopia;
    S.evCopia = e.evCopia; S.copiaVal = e.copiaVal; S.nBuf = e.nBuf;
    for (UINT i = 0; i < kMaxBuf; ++i) { S.proxy[i] = e.proxy[i]; S.allocCopia[i] = e.allocCopia[i]; S.copiaFeita[i] = e.copiaFeita[i]; }
    S.espelhados = 0;
    S.espelho = true;
    Log("fg: espelho herdado (%u proxies); segue ate o proximo ResizeBuffers", S.nBuf);
}

/* X-PAINEL: a API do jogo vai sempre no .ngx-estado, com ou sem FG pedido; so muda a versao na troca */
void ApiDoJogo(bool d3d12)
{
    if (S.apiJogo.exchange(d3d12 ? 12 : 11) != (d3d12 ? 12 : 11)) ++S.estadoVersao;
}

void EstadoTexto(char* buf, size_t n)
{
    try
    {
        std::lock_guard<std::mutex> g(S.muEstado);
        const int api = S.apiJogo.load();
        /* J1: fg_dados = de onde vem o movimento; fg_fps_jogo/fg_fps_tela = os dois numeros do
           card (0 = sem FG dono, o card nao mostra numero nenhum). */
        snprintf(buf, n, "fg_estado=%s\nfg_motivo=%s\nfg_refs=%d\njogo_api=%s\nfg_dados=%s\nfg_fps_jogo=%d\nfg_fps_tela=%d\n",
                 S.estadoPub, S.motivoPub, S.refs,
                 api == 12 ? "D3D12" : api == 11 ? "D3D11" : "-",
                 FonteMovimento(), S.fpsJogoPub.load(), S.fpsTelaPub.load());
    }
    catch (...) { if (n) buf[0] = 0; }
}

/* P1: POR QUE A DLL NAO CHAMA ResizeBuffers SOZINHA. O DXGI so aceita o ResizeBuffers com TODAS as
   referencias aos backbuffers soltas (senao DXGI_ERROR_INVALID_CALL), e um jogo D3D12 costuma
   guardar os backbuffers (e os RTVs) a vida toda, soltando-os so no proprio caminho de resize. Se
   ele guarda, a chamada falha; se nao guarda mas tem trabalho na fila apontando para eles, o jogo
   fica com ponteiros velhos. Aqui so MEDIMOS, uma vez, quantas referencias o jogo segura: o numero
   vai para o log e para o estado (fg_refs), para decidir numa proxima rodada com dado de jogo real. */
void Sondar(IDXGISwapChain* sc)
{
    if (S.sondou.load() || S.dono.load() || S.modoVisto.load() == 0 || !S.origGetBuffer || !sc) return;
    S.sondou = true;
    ID3D12Resource* bb = nullptr;
    if (FAILED(S.origGetBuffer(sc, 0, __uuidof(ID3D12Resource), (void**)&bb)) || !bb) return;   /* D3D11: nao se aplica */
    bb->AddRef();
    const ULONG c = bb->Release();   /* inclui a nossa, do GetBuffer */
    bb->Release();
    const int outros = c > 0 ? (int)c - 1 : 0;
    try { std::lock_guard<std::mutex> g(S.muEstado); S.refs = outros; ++S.estadoVersao; } catch (...) {}
    Log("fg: aguardando ResizeBuffers do jogo; ele segura %d referencia(s) ao backbuffer 0 (%s)", outros,
        outros > 0 ? "um ResizeBuffers da DLL falharia - so o Alt+Enter do jogo serve" : "sem referencias guardadas");
}

void Parar()
{
    S.dono = false; S.espelho = false;
    S.descarga = true;   /* J3: nenhum gancho de upscaler publica dado depois daqui */
    LimparDados();
    try { if (Montado()) Desmontar(); } catch (...) {}
    if (S.hffx) { FreeLibrary(S.hffx); S.hffx = nullptr; }
    Soltar(S.fila);
    for (auto& v : g_filas) if (ID3D12CommandQueue* q = v.exchange(nullptr)) q->Release();   /* C11 */
}

} // namespace Fg
