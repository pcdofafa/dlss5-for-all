// =====================================================================================
//  fpslimit_dll.cpp - a peca que roda DENTRO do processo do jogo e segura o quadro.
//
//  COMO ELA CHEGA AQUI. Nao ha injecao: o app coloca esta biblioteca na pasta do jogo, com o
//  nome que o jogo ja procura, e o proprio jogo a carrega na abertura pela ordem de busca
//  normal do Windows. Nenhuma escrita na memoria de outro processo, nenhuma thread criada de
//  fora. Custa UMA reabertura do jogo, e depois disso todo clique no painel vale na hora,
//  porque a peca ja esta dentro. Ver o wrapper (fpslimit_wrapper.cpp).
//
//  A REGRA QUE MANDA EM TUDO AQUI: se algo der errado do nosso lado, o jogo continua rodando
//  SEM LIMITE. Nunca ao contrario. Toda falha desta biblioteca termina em "repasse puro": o
//  gancho nao entra, ou entra e deixa o quadro passar direto. Um limitador que derruba o jogo
//  da pessoa e pior que limitador nenhum.
//
//  A ORDEM DAS COISAS, e ela e deliberada:
//    1. DllMain nao faz nada alem de criar uma thread. Fazer trabalho ali dentro, com o
//       carregador do Windows segurando o proprio cadeado, e como se trava um processo.
//    2. A thread pergunta PRIMEIRO se ha antitrapaca neste processo. Se houver, nao engancha
//       nada e para por aqui. Esta e a ultima linha de defesa e vem antes de tudo.
//    3. So entao procura a API grafica, pega o endereco do Present e engancha.
//    4. No Present, le o limite da memoria compartilhada e segura o quadro ate a hora.
//
//  O QUE ESTA PECA NAO FAZ, de proposito: nao se esconde, nao troca de nome, nao se carrega
//  por fora do mecanismo do Windows, nao adia a checagem de antitrapaca para depois que ele
//  sobe. Tudo isso existe e serve para escapar de deteccao, e e o que separa uma ferramenta
//  legitima de uma trapaca. Se uma protecao detectar esta biblioteca e bloquear, esse e o
//  resultado CERTO.
// =====================================================================================
#include <windows.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <stdio.h>
#include "MinHook.h"
#include "fpslimit_shared.h"
#include "fpslimit_guard.h"
#include "fpslimit_ritmo.h"

using namespace FpsLimit;

// ------------------------------------------------------------------ estado do processo
static HANDLE   g_mapa      = nullptr;
static Bloco*   g_bloco     = nullptr;
static Ritmo    g_ritmo;
static bool     g_enganchou = false;
static volatile LONG g_saindo = 0;

// Indices na vtable de IDXGISwapChain. A tabela e IUnknown(0,1,2), IDXGIObject(3..6),
// IDXGIDeviceSubObject(7), e entao IDXGISwapChain: Present e o 8, GetBuffer o 9.
// IDXGISwapChain1 acrescenta Present1 no 22. Enganchar os dois cobre D3D11 e D3D12, porque
// os dois apresentam pelo MESMO objeto de swapchain do DXGI - nao ha um hook por API.
static const int kIdxPresent  = 8;
static const int kIdxPresent1 = 22;

typedef HRESULT (STDMETHODCALLTYPE* PFN_Present)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE* PFN_Present1)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
static PFN_Present  g_origPresent  = nullptr;
static PFN_Present1 g_origPresent1 = nullptr;

// ------------------------------------------------------------------ registro
// Log proprio, ao lado da biblioteca, e SO quando algo digno de nota acontece. Nao escreve por
// quadro nem por segundo: isto roda dentro do jogo da pessoa e um arquivo crescendo em disco
// durante a partida e custo que ninguem pediu.
static void Logf(const char* fmt, ...) {
    wchar_t caminho[MAX_PATH]{};
    GetModuleFileNameW(nullptr, caminho, MAX_PATH);
    wchar_t* barra = wcsrchr(caminho, L'\\');
    if (barra) wcscpy(barra + 1, L"dlss5-fpslimit.log"); else return;
    FILE* f = _wfopen(caminho, L"a");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

// Registra o motivo de ter desistido, para o app mostrar sem traduzir codigo de erro.
static void Desistir(uint32_t estado, const wchar_t* motivo) {
    if (g_bloco) {
        wcsncpy((wchar_t*)g_bloco->motivo, motivo, 127);
        ((wchar_t*)g_bloco->motivo)[127] = 0;
        g_bloco->estado = estado;
    }
    Logf("desistindo (estado %u): %ls", estado, motivo);
}

// ------------------------------------------------------------------ o gancho
// TUDO AQUI E CAMINHO QUENTE. Sem alocacao, sem excecao, sem chamada que possa bloquear.
// O caminho de "sem limite" custa uma leitura e uma comparacao.
static void SegurarQuadro() {
    if (!g_bloco) return;                          // sem contrato: quadro passa direto
    if (g_bloco->desligar) { g_bloco->estado = EST_DESLIGADO; return; }

    // O APP AINDA ESTA VIVO? Ele incrementa `pulso_app` a cada segundo. Se parar de mudar, ele
    // morreu ou travou, e a peca se desativa sozinha. SEM ISTO, um app que caisse deixaria o
    // jogo preso no limite ate ser fechado - e a pessoa nao teria como saber por que.
    static uint32_t ultimoPulso = 0;
    static ULONGLONG quandoMudou = 0;
    const ULONGLONG agora = GetTickCount64();
    if (g_bloco->pulso_app != ultimoPulso) { ultimoPulso = g_bloco->pulso_app; quandoMudou = agora; }
    else if (quandoMudou && agora - quandoMudou > kSegundosSemAppAteDesistir * 1000ull) {
        if (g_bloco->estado != EST_APP_SUMIU) {
            g_bloco->estado = EST_APP_SUMIU;
            Logf("o app parou de dar sinal de vida ha mais de %u s - soltando o limite", kSegundosSemAppAteDesistir);
        }
        return;                                     // quadro passa direto, jogo destravado
    }
    if (!quandoMudou) quandoMudou = agora;

    const uint32_t fps = ValorPermitido(g_bloco->limite_fps);
    g_bloco->pulso_peca++;
    if (g_ritmo.Esperar(fps)) g_bloco->quadros_segurados++;
}

static HRESULT STDMETHODCALLTYPE MeuPresent(IDXGISwapChain* sc, UINT intervalo, UINT flags) {
    if (!InterlockedCompareExchange(&g_saindo, 0, 0)) SegurarQuadro();
    return g_origPresent(sc, intervalo, flags);
}
static HRESULT STDMETHODCALLTYPE MeuPresent1(IDXGISwapChain1* sc, UINT intervalo, UINT flags,
                                             const DXGI_PRESENT_PARAMETERS* p) {
    if (!InterlockedCompareExchange(&g_saindo, 0, 0)) SegurarQuadro();
    return g_origPresent1(sc, intervalo, flags, p);
}

// ------------------------------------------------------------------ achar o Present
// Para enganchar um metodo de interface COM e preciso o endereco dele, e o endereco esta na
// vtable de UM OBJETO. Nao da para perguntar ao sistema: e preciso ter um swapchain na mao.
// Entao criamos um DESCARTAVEL, do tamanho minimo, numa janela invisivel, so para ler a
// tabela - e o destruimos em seguida. O swapchain do jogo, quando existir, tera a MESMA
// vtable, porque a implementacao vem da mesma dxgi.dll carregada no processo.
static bool EnderecosDoPresent(void** present, void** present1) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DLSS5FpsLimitSonda";
    if (!RegisterClassExW(&wc)) return false;
    HWND jan = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                               0, 0, 8, 8, nullptr, nullptr, wc.hInstance, nullptr);
    if (!jan) { UnregisterClassW(wc.lpszClassName, wc.hInstance); return false; }

    DXGI_SWAP_CHAIN_DESC d{};
    d.BufferCount       = 1;
    d.BufferDesc.Width  = 8;
    d.BufferDesc.Height = 8;
    d.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.OutputWindow      = jan;
    d.SampleDesc.Count  = 1;
    d.Windowed          = TRUE;
    d.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    // D3D_DRIVER_TYPE_NULL: nao queremos desenhar nada, so a tabela de funcoes. Ele nao toca
    // na placa, o que importa quando o jogo ja esta usando a placa toda.
    const D3D_FEATURE_LEVEL nivel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_NULL, nullptr, 0,
                                               &nivel, 1, D3D11_SDK_VERSION, &d, &sc, &dev, nullptr, &ctx);
    bool ok = false;
    if (SUCCEEDED(hr) && sc) {
        void** vt = *(void***)sc;
        *present = vt[kIdxPresent];
        // Present1 so existe se o objeto for IDXGISwapChain1. Perguntamos em vez de supor: em
        // Windows antigo a interface nao existe e ler o indice 22 seria ler fora da tabela.
        IDXGISwapChain1* sc1 = nullptr;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1)) && sc1) {
            *present1 = (*(void***)sc1)[kIdxPresent1];
            sc1->Release();
        }
        ok = true;
    }
    if (ctx) ctx->Release();
    if (dev) dev->Release();
    if (sc)  sc->Release();
    DestroyWindow(jan);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

// ------------------------------------------------------------------ o contrato
static bool AbrirBloco() {
    wchar_t nome[128];
    NomeDoBloco(nome, 128, GetCurrentProcessId());
    // A PECA CRIA o bloco, e o app o procura pelo pid do jogo. E nesta ordem porque a peca
    // sempre existe quando o jogo existe, e o app pode ser aberto e fechado no meio.
    g_mapa = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Bloco), nome);
    if (!g_mapa) return false;
    g_bloco = (Bloco*)MapViewOfFile(g_mapa, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Bloco));
    if (!g_bloco) { CloseHandle(g_mapa); g_mapa = nullptr; return false; }
    // Recem-criado vem zerado pelo Windows; se ja existia, os campos de identidade ja batem.
    g_bloco->magic   = kMagic;
    g_bloco->versao  = kVersao;
    g_bloco->tamanho = (uint32_t)sizeof(Bloco);
    g_bloco->pid_jogo = GetCurrentProcessId();
    g_bloco->estado  = EST_INICIANDO;
    return true;
}

// ------------------------------------------------------------------ a partida
static DWORD WINAPI Subir(LPVOID) {
    if (!AbrirBloco()) { Logf("nao consegui criar a memoria compartilhada - saindo sem enganchar"); return 0; }

    // PASSO 1, ANTES DE QUALQUER OUTRA COISA: ha antitrapaca neste processo?
    const uint32_t prot = ProtecoesNesteProcesso();
    if (prot) {
        g_bloco->protecao = prot;
        wchar_t m[128];
        _snwprintf(m, 128, L"encontrei %ls neste jogo - nao vou enganchar nada", NomeDaProtecao(prot));
        Desistir(EST_ANTITRAPACA, m);
        return 0;                                   // repasse puro, para sempre, neste processo
    }

    // PASSO 2: a API grafica esta carregada? Se dxgi nao esta no processo, o jogo nao apresenta
    // por DXGI e nao e caso nosso (D3D9, OpenGL e Vulkan ficaram para depois).
    if (!GetModuleHandleW(L"dxgi.dll")) {
        Desistir(EST_SEM_API, L"este jogo nao apresenta por DXGI (D3D9, OpenGL ou Vulkan)");
        return 0;
    }

    void* pPresent = nullptr; void* pPresent1 = nullptr;
    if (!EnderecosDoPresent(&pPresent, &pPresent1) || !pPresent) {
        Desistir(EST_SEM_API, L"nao consegui ler o endereco do Present");
        return 0;
    }

    if (MH_Initialize() != MH_OK) { Desistir(EST_GANCHO_FALHOU, L"MinHook nao iniciou"); return 0; }
    bool algum = false;
    if (MH_CreateHook(pPresent, (void*)&MeuPresent, (void**)&g_origPresent) == MH_OK &&
        MH_EnableHook(pPresent) == MH_OK) algum = true;
    if (pPresent1 &&
        MH_CreateHook(pPresent1, (void*)&MeuPresent1, (void**)&g_origPresent1) == MH_OK &&
        MH_EnableHook(pPresent1) == MH_OK) algum = true;
    if (!algum) { MH_Uninitialize(); Desistir(EST_GANCHO_FALHOU, L"o gancho nao entrou"); return 0; }

    g_ritmo.Iniciar();
    g_enganchou = true;
    g_bloco->api = API_D3D11;      // vale para D3D12 tambem: o objeto de apresentacao e o mesmo
    g_bloco->estado = EST_ATIVO;
    Logf("pronto: enganchado em Present%s, temporizador fino=%d, pid %lu",
         pPresent1 ? " e Present1" : "", g_ritmo.TemporizadorFino() ? 1 : 0,
         (unsigned long)GetCurrentProcessId());
    return 0;
}

// ------------------------------------------------------------------ entrada e saida
BOOL WINAPI DllMain(HINSTANCE inst, DWORD motivo, LPVOID) {
    if (motivo == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // NADA DE TRABALHO AQUI. O carregador do Windows segura um cadeado durante o DllMain, e
        // qualquer coisa que carregue outra biblioteca, crie device ou espere por thread pode
        // travar o processo inteiro. Uma thread e a unica coisa que se faz aqui.
        HANDLE t = CreateThread(nullptr, 0, Subir, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    } else if (motivo == DLL_PROCESS_DETACH) {
        // O jogo esta fechando. Sinaliza para o gancho parar de fazer trabalho, mas NAO
        // desengancha: tirar o gancho enquanto outra thread pode estar dentro dele e uma das
        // formas mais faceis de derrubar um processo na saida. O processo esta morrendo de
        // qualquer jeito, e deixar o trampolim no lugar e o caminho seguro.
        InterlockedExchange(&g_saindo, 1);
        if (g_bloco) { g_bloco->estado = EST_DESLIGADO; UnmapViewOfFile(g_bloco); g_bloco = nullptr; }
        if (g_mapa)  { CloseHandle(g_mapa); g_mapa = nullptr; }
    }
    return TRUE;
}
