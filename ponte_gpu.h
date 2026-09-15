/* =====================================================================
   ponte_gpu.h -- leva uma textura de UMA placa para OUTRA sem passar pela
   CPU e sem o compositor do Windows no meio.

   POR QUE ISTO EXISTE
   -------------------
   Com duas placas (o jogo e o monitor numa Radeon, o DLSS 5 numa GeForce)
   o app deixava o compositor fazer as duas travessias: a captura do jogo
   (Radeon -> GeForce, dentro do WGC) e a exibicao da nossa janela (GeForce
   -> Radeon, na composicao do DWM). Medido em 11/09/2026 num PC com RX 9060
   XT + RTX 5060: com o monitor na GeForce o Present do JOGO virava
   cross-adapter e ele travava em ~40 fps; com o monitor na Radeon o DWM
   gastava 57% da placa do jogo compondo a janela estrangeira. Nos dois
   casos quem pagava era o jogo, e nada de grafico mudava o teto.

   O QUE ISTO FAZ
   --------------
   A travessia passa a ser NOSSA, pelo caminho que o D3D12 oferece para
   isso: um heap "cross-adapter" (memoria de sistema que as duas placas
   enxergam), copiado pelo motor de copia de cada placa, com fences
   compartilhadas entre as placas e entre D3D11 e D3D12. E o desenho do
   sample D3D12HeterogeneousMultiadapter da Microsoft. Nenhum byte passa
   pela CPU; a CPU so enfileira.

       origem (D3D11, placa A)         destino (D3D11, placa B)
       CopyResource -> texOrig[i]      texDest[i] -> quem usa
             | Signal fA                      ^ Wait fB (na GPU)
       D3D12 A: Wait fA, copia            D3D12 B: Wait fX, copia
       texOrig[i] -> cross[i]             cross[i] -> texDest[i]
             | Signal fX (cross-adapter)      | Signal fB

   Anel de kSlots quadros: o produtor nunca espera. Se o anel esta cheio
   (o destino ainda usa o slot), o quadro e DESCARTADO e contado - nunca
   se trava o laco que a pessoa esta vendo.

   Duas direcoes sao duas instancias: uma leva a captura (Radeon -> GeForce),
   outra leva a saida da cadeia (GeForce -> Radeon). A classe nao sabe qual
   e qual.

   BITRATE DA TRAVESSIA (Formato)
   ------------------------------
   Medido em 12/09/2026 (scratchpad/ponte_bench.cpp): o link da RX 9060 XT
   (PCIe 2.0 x4) anda a ~1,3-1,5 GB/s NOS DOIS SENTIDOS - um quadro 4K BGRA
   (31,6 MiB) custa ~21 ms so para entrar ou sair dela, e a RTX 5060 faz a
   parte dela a ~5-6 GB/s. O que manda e o NUMERO DE BYTES que cruza. Dai o
   formato de travessia:
     Max  - BGRA8 como sempre, 4 B/px, sem perda (o caminho de antes, intacto).
     Alto - NV12 4:2:0, 1,5 B/px: a ORIGEM converte por compute shader para
            dois planos (Y em R8, UV em R8G8 na metade da resolucao), cada
            plano cruza como um trecho do MESMO buffer cross-adapter (um
            PLACED_SUBRESOURCE_FOOTPRINT por plano, nas duas pontas - nada de
            textura NV12 cross-adapter, que e territorio de driver), e o
            DESTINO reconverte para BGRA8 por compute na textura que o
            consumidor ja conhece. So o croma perde resolucao (como num
            stream de video); o luma e integral. Se o formato de entrada nao
            for 8 bits UNORM, ou a placa nao escrever esse formato por UAV,
            cai para Max sozinho e diz por que (MotivoMax).

   THREADS: Enviar() roda na thread do contexto imediato da ORIGEM;
   Receber()/Liberar() na thread do contexto imediato do DESTINO. Filas e
   fences do D3D12 sao livres de thread; as listas de comando sao por slot
   e so o Enviar as toca. Os compute shaders so tocam o estado CS* dos
   contextos, e desligam tudo ao sair - o Renderer nao usa compute.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <atomic>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <algorithm>

// O nivel "Codec" (HEVC pelo motor de video da placa). So cabecalhos: as DLLs do libav
// sao carregadas em tempo de execucao e so quando alguem pede Codec.
#include "ponte_lav.h"

namespace PonteGpu {

constexpr int kSlots = 3;

// O formato em que o quadro CRUZA entre as placas (ver o bloco no cabecalho).
//   Max   - BGRA8 cru (4 B/px)
//   Alto  - NV12 4:2:0 por compute (1,5 B/px)
//   Codec - HEVC pelo motor de video da placa (ver ponte_lav.h): ~0,2 MB por quadro 4K,
//           ao preco de ~15 ms. So para PCIe ruim (x1/riser), so SDR 8 bits.
//   Auto  - NAO e um formato: e o pedido "escolha por mim", resolvido pela sonda de banda
//           no arranque. Nunca aparece em Compactacao(), so em Pedido().
enum Formato { Max = 0, Alto = 1, Codec = 2, AutoFmt = 3 };
static inline const char* NomeFormato(Formato f) {
    return f == Codec ? "HEVC (codec)" : f == Alto ? "NV12 4:2:0 (1,5 B/px)" : f == AutoFmt ? "auto" : "BGRA8 (4 B/px)";
}
// o mesmo nome, em wide - o painel precisa dele e nao ha conversor utf8->wide no main.cpp
static inline const wchar_t* NomeFormatoW(Formato f) {
    return f == Codec ? L"HEVC (codec)" : f == Alto ? L"NV12 4:2:0 (1,5 B/px)" : f == AutoFmt ? L"auto" : L"BGRA8 (4 B/px)";
}
static inline const wchar_t* ChaveFormato(Formato f) {
    return f == Codec ? L"codec" : f == Alto ? L"alto" : f == AutoFmt ? L"auto" : L"max";
}
static inline Formato FormatoDeTexto(const wchar_t* s) {
    if (!s) return Max;
    std::wstring q = s; for (auto& c : q) c = (wchar_t)towlower(c);
    if (q == L"codec" || q == L"hevc" || q == L"baixo") return Codec;
    if (q == L"auto") return AutoFmt;
    return (q == L"alto" || q == L"nv12" || q == L"high") ? Alto : Max;
}

/* FILAS MISTAS (medido 12/09/2026, scratchpad/ponte_duplex.cpp, PCIe 2.0 x4 da RX 9060 XT)
   -------------------------------------------------------------------------------------
   O link PCIe E full-duplex, mas o ENGINE da placa nao: quando os dois sentidos usam
   filas do MESMO tipo na Radeon (COPY+COPY ou DIRECT+DIRECT, que e o que o app fazia),
   as duas copias se SOMAM - agregado travado em 1,3-1,4 GB/s, 4K NV12 custando 16-18 ms
   por sentido quando os dois correm juntos. Com engines DIFERENTES (um sentido na fila
   COPY, o outro na DIRECT) fica quase full-duplex: 4K NV12 = 8,7 + 9,9 ms SIMULTANEOS,
   2,51 GB/s agregado (somam so 19%). O tipo vale para as DUAS pontas de um sentido (e
   como o microbench mediu).

   QUAL DAS DUAS MISTURAS: o microbench SOLTO (sem jogo, sem apresentador) prefere DIRECT
   na ENTRADA + COPY na SAIDA (2,51 GB/s contra 2,15 da inversa). No APP INTEIRO, medido
   no mesmo PC com o jogo falso na Radeon (12/09/2026, 4K, duas rodadas), o resultado se
   INVERTE - COPY na entrada + DIRECT na saida:
       filas      saida trav/s   apresentados/s   PRESENT   fps do jogo falso
       iguais      11,9-12,6       8,4-9,4        39-42 ms      16,4-17,3
       mista       12,9-13,2       9,3-9,6        37,9 ms       16,8-17,0
       mistainv    15,0            10,5-10,8      36,0 ms       17,2
   e em 1440p a diferenca aparece no jogo: mista custa 12% do fps dele (26,2 contra 29,8
   de iguais e 31,6 de mistainv), porque a DIRECT da entrada disputa o 3D com quem esta
   desenhando. Por isso o PADRAO (auto) e a INVERSA: COPY na entrada, DIRECT na saida.
   Ressalva honesta: o teste do app foi com a ponte FORCADA (monitor na GeForce), onde o
   Present do proprio jogo ainda cruza pelo compositor no sentido da entrada - com o cabo
   na Radeon esse peso some e a preferencia pode voltar para `mista`. As duas misturas
   ganham de `iguais` nos dois experimentos; e por isso que a escolha fica exposta em
   [ultima] ponte_filas = auto | mista | mistainv | iguais. */
enum Filas { FilasAuto = 0, FilasMista = 1, FilasMistaInv = 2, FilasIguais = 3 };
static inline const wchar_t* ChaveFilas(Filas f) {
    return f == FilasMista ? L"mista" : f == FilasMistaInv ? L"mistainv" : f == FilasIguais ? L"iguais" : L"auto";
}
static inline Filas FilasDeTexto(const wchar_t* s) {
    if (!s || !*s) return FilasAuto;
    std::wstring q = s; for (auto& c : q) c = (wchar_t)towlower(c);
    if (q == L"mista") return FilasMista;
    if (q == L"mistainv" || q == L"mista_inv" || q == L"invertida") return FilasMistaInv;
    if (q == L"iguais" || q == L"igual" || q == L"copy") return FilasIguais;
    return FilasAuto;
}
/* O tipo de fila D3D12 de um sentido, dada a escolha. `entrada` = o sentido que leva a
   captura para a placa da rede (Radeon -> GeForce). Auto = a melhor combinacao medida no
   APP INTEIRO (= mistainv: COPY na entrada, DIRECT na saida). `mista` e a inversa, que e
   a que o microbench solto prefere. */
static inline D3D12_COMMAND_LIST_TYPE TipoFila(Filas f, bool entrada) {
    if (f == FilasIguais) return D3D12_COMMAND_LIST_TYPE_COPY;
    const bool direct = (f == FilasMista) ? entrada : !entrada;   // auto/mistainv: DIRECT na saida
    return direct ? D3D12_COMMAND_LIST_TYPE_DIRECT : D3D12_COMMAND_LIST_TYPE_COPY;
}
static inline const char* NomeTipoFila(D3D12_COMMAND_LIST_TYPE t) {
    return t == D3D12_COMMAND_LIST_TYPE_DIRECT ? "DIRECT" : t == D3D12_COMMAND_LIST_TYPE_COPY ? "COPY" : "?";
}

// DIAG-SOBREPOSICAO: janela [inicio,fim] (QPC) do ultimo Present do swapchain OCULTO (a
// cadeia neural, na placa da rede). O apresentador compara o proprio Present com isto para
// saber se ele bloqueia justamente enquanto o Present oculto roda (lock global do ReShade?).
inline std::atomic<long long> g_hpIni{ 0 }, g_hpFim{ 0 };

template <class T> static inline void Soltar(T*& p) { if (p) { p->Release(); p = nullptr; } }
static inline void FecharHandle(HANDLE& h) { if (h) { CloseHandle(h); h = nullptr; } }

/* Um lado D3D12 (uma placa): device, fila de copia e uma lista por slot. */
struct Lado12 {
    ID3D12Device*               dev  = nullptr;
    ID3D12CommandQueue*         fila = nullptr;
    ID3D12CommandAllocator*     alloc[kSlots] = {};
    ID3D12GraphicsCommandList*  lista[kSlots] = {};
    bool                        filaDeCopia = true;
    D3D12_COMMAND_LIST_TYPE     tipo = D3D12_COMMAND_LIST_TYPE_COPY;

    bool Iniciar(IDXGIAdapter* ad, std::wstring* erro, const wchar_t* rotulo,
                 D3D12_COMMAND_LIST_TYPE pedida = D3D12_COMMAND_LIST_TYPE_COPY) {
        HRESULT hr = D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev);
        if (FAILED(hr) || !dev) { Erro(erro, rotulo, L"D3D12CreateDevice", hr); return false; }
        // A fila de COPIA e o motor de DMA da placa: roda em paralelo ao 3D e nao disputa os
        // shaders da cadeia. A DIRECT usa o 3D - mais rapida no link desta Radeon, mas divide
        // hardware com o jogo. Quem escolhe e quem chama (ver "FILAS MISTAS" no cabecalho);
        // se a placa recusar o tipo pedido, cai para o outro.
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = pedida;
        hr = dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&fila);
        if (FAILED(hr) || !fila) {
            qd.Type = (pedida == D3D12_COMMAND_LIST_TYPE_COPY) ? D3D12_COMMAND_LIST_TYPE_DIRECT
                                                               : D3D12_COMMAND_LIST_TYPE_COPY;
            hr = dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&fila);
            if (FAILED(hr) || !fila) { Erro(erro, rotulo, L"CreateCommandQueue", hr); return false; }
        }
        tipo = qd.Type;
        filaDeCopia = (qd.Type == D3D12_COMMAND_LIST_TYPE_COPY);
        for (int i = 0; i < kSlots; i++) {
            hr = dev->CreateCommandAllocator(qd.Type, __uuidof(ID3D12CommandAllocator), (void**)&alloc[i]);
            if (FAILED(hr)) { Erro(erro, rotulo, L"CreateCommandAllocator", hr); return false; }
            hr = dev->CreateCommandList(0, qd.Type, alloc[i], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&lista[i]);
            if (FAILED(hr)) { Erro(erro, rotulo, L"CreateCommandList", hr); return false; }
            lista[i]->Close();   // nasce aberta; fica fechada ate o primeiro uso
        }
        return true;
    }
    void Parar() {
        for (int i = 0; i < kSlots; i++) { Soltar(lista[i]); Soltar(alloc[i]); }
        Soltar(fila); Soltar(dev);
    }
    static void Erro(std::wstring* erro, const wchar_t* rotulo, const wchar_t* onde, HRESULT hr) {
        if (!erro) return;
        wchar_t b[160];
        _snwprintf(b, 160, L"%ls: %ls falhou (0x%08lX)", rotulo, onde, (unsigned long)hr);
        b[159] = 0; *erro = b;
    }
};

/* Cronometro de GPU (timestamp queries) para um dispatch por slot. Le o resultado do slot
   quando ele volta a ser usado (kSlots quadros depois), sem nunca esperar. Diagnostico:
   quanto custa o codificador/decodificador de verdade, na placa. */
struct MedidorGpu {
    ID3D11Query* disj[kSlots] = {}; ID3D11Query* t0[kSlots] = {}; ID3D11Query* t1[kSlots] = {};
    bool emVoo[kSlots] = {};
    double soma = 0; uint64_t n = 0;
    bool Iniciar(ID3D11Device* d) {
        D3D11_QUERY_DESC q{};
        for (int i = 0; i < kSlots; i++) {
            q.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; if (FAILED(d->CreateQuery(&q, &disj[i]))) return false;
            q.Query = D3D11_QUERY_TIMESTAMP;
            if (FAILED(d->CreateQuery(&q, &t0[i])) || FAILED(d->CreateQuery(&q, &t1[i]))) return false;
        }
        return true;
    }
    void Antes(ID3D11DeviceContext* c, int i) {
        if (!disj[i]) return;
        if (emVoo[i]) {
            emVoo[i] = false;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{}; UINT64 a = 0, b = 0;
            if (c->GetData(disj[i], &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                c->GetData(t0[i], &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                c->GetData(t1[i], &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                !dj.Disjoint && dj.Frequency && b >= a) {
                soma += (double)(b - a) * 1000.0 / (double)dj.Frequency; n++;
            }
        }
        c->Begin(disj[i]); c->End(t0[i]);
    }
    void Depois(ID3D11DeviceContext* c, int i) {
        if (!disj[i]) return;
        c->End(t1[i]); c->End(disj[i]); emVoo[i] = true;
    }
    double Media() const { return n ? soma / (double)n : 0.0; }
    void Parar() { for (int i = 0; i < kSlots; i++) { Soltar(disj[i]); Soltar(t0[i]); Soltar(t1[i]); emVoo[i] = false; } soma = 0; n = 0; }
};

class Ponte {
public:
    /* Monta a ponte de `devOrigem` para `devDestino`, para texturas w x h no formato dado.
       Os dois devices sao D3D11 (o da origem pode ser o do ReShade - so precisa responder
       a ID3D11Device5). `compacto` e o formato de TRAVESSIA (Max = BGRA8 como sempre;
       Alto = NV12 - ver o cabecalho); quem chama e quem recebe continuam vendo texturas
       `fmt`. Devolve false com o motivo em `erro`; nesse caso nada fica de pe. */
    bool Iniciar(ID3D11Device* devOrigem, ID3D11Device* devDestino,
                 UINT w, UINT h, DXGI_FORMAT fmt, std::wstring* erro,
                 PonteGpu::Formato compacto = PonteGpu::Max,
                 D3D12_COMMAND_LIST_TYPE tipoFila = D3D12_COMMAND_LIST_TYPE_COPY) {
        Parar();
        m_w = w; m_h = h; m_fmt = fmt; m_pedido = compacto; m_modo = PonteGpu::Max;
        m_motivoQueda.clear();
        m_errosCodec.store(0); m_codecQuebrou.store(false); m_mbQuadro = m_msEnc = m_msDec = 0;
        if (!devOrigem || !devDestino || !w || !h) { if (erro) *erro = L"ponte: parametros vazios"; return false; }

        // ---- os dois lados D3D11, com fences (11.4)
        if (FAILED(devOrigem->QueryInterface(__uuidof(ID3D11Device5), (void**)&m_devA)) || !m_devA)
        { if (erro) *erro = L"ponte: o device de origem nao tem ID3D11Device5 (Windows 10 1703+)"; Parar(); return false; }
        if (FAILED(devDestino->QueryInterface(__uuidof(ID3D11Device5), (void**)&m_devB)) || !m_devB)
        { if (erro) *erro = L"ponte: o device de destino nao tem ID3D11Device5"; Parar(); return false; }
        {
            ID3D11DeviceContext* c = nullptr;
            m_devA->GetImmediateContext(&c);
            if (!c || FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&m_ctxA)) || !m_ctxA)
            { Soltar(c); if (erro) *erro = L"ponte: contexto de origem sem ID3D11DeviceContext4"; Parar(); return false; }
            c->Release();
            c = nullptr;
            m_devB->GetImmediateContext(&c);
            if (!c || FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&m_ctxB)) || !m_ctxB)
            { Soltar(c); if (erro) *erro = L"ponte: contexto de destino sem ID3D11DeviceContext4"; Parar(); return false; }
            c->Release();
        }

        // ---- os adaptadores, pelo DXGI de cada device
        IDXGIAdapter* adA = AdaptadorDe(m_devA);
        IDXGIAdapter* adB = AdaptadorDe(m_devB);
        if (!adA || !adB) { Soltar(adA); Soltar(adB); if (erro) *erro = L"ponte: nao achei o adaptador de um dos devices"; Parar(); return false; }
        {
            DXGI_ADAPTER_DESC da{}, db{};
            adA->GetDesc(&da); adB->GetDesc(&db);
            m_mesmaPlaca = (da.AdapterLuid.LowPart == db.AdapterLuid.LowPart && da.AdapterLuid.HighPart == db.AdapterLuid.HighPart);
        }
        // Uma placa so: nada cruza o PCIe e a fila DIRECT so tiraria 3D de quem esta
        // desenhando. O caminho de uma placa fica exatamente como era (COPY).
        if (m_mesmaPlaca) tipoFila = D3D12_COMMAND_LIST_TYPE_COPY;

        /* ---------------- TRAVESSIA DIRETA (os dois devices na MESMA placa) ----------------
           MEDIDO 13/09/2026 (D:\claude-scratch\gate\): com fluidez interpolada e UMA placa a
           ponte era forcada (janela oculta + apresentador), e mesmo assim o quadro saia pelo
           MESMO caminho das duas placas: heap D3D12 SHARED_CROSS_ADAPTER. Heap cross-adapter
           mora na MEMORIA DO SISTEMA por definicao, entao cada quadro fazia VRAM -> sysmem ->
           VRAM pelo PCIe SEM PRECISAR - a propria sonda do Auto media 19,8/19,9 ms por quadro
           BGRA 4K entre dois devices da mesma RTX 5060 (log app_E_nr50_fg.log 12:49:59), e o
           Auto ainda escolhia NV12 4:2:0 por causa disso: perda de croma de graca.
           Na mesma placa nao ha o que atravessar: uma textura D3D11 compartilhada por
           NTHANDLE (sem CROSS_ADAPTER) fica em VRAM e o device de destino a le direto. Fica
           uma copia VRAM->VRAM, duas fences e nenhum D3D12.
           Se qualquer passo falhar, cai no caminho de sempre com o motivo no log - nunca
           tela preta. DLSS5_PONTE_DIRETO=0 desliga (bancada: serve para medir o antes). */
        if (m_mesmaPlaca && compacto != PonteGpu::Codec) {
            wchar_t ed[8]{};
            const bool desligado = (GetEnvironmentVariableW(L"DLSS5_PONTE_DIRETO", ed, 8) > 0 && ed[0] == L'0');
            if (desligado) {
                m_motivoDireto = L"DLSS5_PONTE_DIRETO=0 (BANCADA)";
            } else {
                std::wstring e2;
                if (IniciarDireto(w, h, fmt, &e2)) {
                    adA->Release(); adB->Release();
                    m_modo = PonteGpu::Max;      // BGRA8 inteiro: nada e subamostrado
                    m_nPlanos = 0;
                    m_enviados = 0; m_descartados = 0; m_recebidos = 0;
                    m_pronto.store(0); m_recebidoUltimo = 0;
                    m_direto = true; m_ok = true;
                    return true;
                }
                m_motivoDireto = e2;
                SoltarDireto();
                if (erro) erro->clear();
            }
        }
        const bool okA = m_A.Iniciar(adA, erro, L"placa de origem (D3D12)", tipoFila);
        const bool okB = okA && m_B.Iniciar(adB, erro, L"placa de destino (D3D12)", tipoFila);
        adA->Release(); adB->Release();
        if (!okA || !okB) { Parar(); return false; }

        // ---- o formato de travessia que da para cumprir. Quem pede Alto num formato que
        // nao e 8 bits (HDR em FP16, por exemplo) ou numa placa que nao escreve o formato
        // por UAV recebe Max e o motivo - nunca um erro, porque Max sempre funciona.
        // Quem pede Codec e nao tem as DLLs (ou esta em HDR) cai para ALTO, nao para Max.
        std::wstring motivoAlto;
        const bool podeAlto = PodeAlto(fmt, &motivoAlto);
        if (compacto == PonteGpu::Codec) {
            std::wstring m2;
            if (!CodecAceitaFormato(fmt)) m2 = L"o nivel Codec so vale em SDR 8 bits (a ponte esta em HDR/FP16)";
            else if (!podeAlto) m2 = motivoAlto;
            else if (!CodecDisponivel(&m2)) { /* m2 ja diz por que */ }
            else m_modo = PonteGpu::Codec;
            if (m_modo != PonteGpu::Codec) { m_motivoQueda = m2; if (podeAlto) m_modo = PonteGpu::Alto; }
        } else if (compacto == PonteGpu::Alto) {
            if (podeAlto) m_modo = PonteGpu::Alto;
            else m_motivoQueda = motivoAlto;
        }

        // ---- texturas de origem e destino (D3D11), e os planos compactos
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        if (m_modo == PonteGpu::Max) {
            // (placa A, D3D11, compartilhadas com o D3D12 de A; idem em B)
            // NTHANDLE + SHARED: e a combinacao que o D3D11 aceita aqui (NTHANDLE sozinho devolve
            // E_INVALIDARG - medido em 12/09/2026) e que o D3D12 abre com OpenSharedHandle.
            td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
            for (int i = 0; i < kSlots; i++) {
                if (!CriarTexturaCompartilhada(m_devA, m_A.dev, td, &m_texOrig[i], &m_resOrig12[i], erro, L"origem")) { Parar(); return false; }
                if (!CriarTexturaCompartilhada(m_devB, m_B.dev, td, &m_texDest[i], &m_resDest12[i], erro, L"destino")) { Parar(); return false; }
            }
            m_nPlanos = 1;
            m_plano[0].desc = td;
        } else {
            // O destino e uma textura LOCAL de B com UAV: quem a escreve e o decodificador,
            // nao o motor de copia. A origem so ganha textura propria se a recebida nao tiver
            // SRV (ver SrvOrigem). Os planos e que sao compartilhados com o D3D12.
            D3D11_TEXTURE2D_DESC dd = td;
            dd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
            for (int i = 0; i < kSlots; i++) {
                HRESULT hr = m_devB->CreateTexture2D(&dd, nullptr, &m_texDest[i]);
                if (FAILED(hr) || !m_texDest[i]) { Lado12::Erro(erro, L"destino", L"CreateTexture2D (BGRA com UAV)", hr); Parar(); return false; }
                hr = m_devB->CreateUnorderedAccessView(m_texDest[i], nullptr, &m_uavDest[i]);
                if (FAILED(hr) || !m_uavDest[i]) { Lado12::Erro(erro, L"destino", L"CreateUnorderedAccessView (BGRA)", hr); Parar(); return false; }
            }
            m_uw = (w + 1) / 2; m_uh = (h + 1) / 2;
            // ---- CODEC: nenhum plano cruza e nao ha heap nenhum. Quem leva o quadro e um
            // pacote HEVC de ~0,2 MB na memoria do processo. O shader de codificacao escreve
            // direto na textura NV12 do pool do encoder (zero copia) e o de decodificacao le
            // a NV12 que saiu do decodificador. Qualquer falha aqui cai para Alto.
            if (m_modo == PonteGpu::Codec) {
                std::wstring e2;
                // A faixa (16-235 x 0-255) tem de ser A MESMA no shader e no encoder - ver
                // ponte_lav.h. DLSS5_PONTE_FAIXA=0 troca para faixa cheia (diagnostico).
                { wchar_t e[8]{}; if (GetEnvironmentVariableW(L"DLSS5_PONTE_FAIXA", e, 8) && e[0] == L'0') m_faixaLim = false; }
                if (!m_codec.Iniciar(devOrigem, devDestino, w, h, kSlots, m_mbps, m_faixaLim, &e2) || !CriarShaders(erro)) {
                    m_codec.Parar();
                    m_motivoQueda = e2.empty() ? (erro ? *erro : std::wstring(L"shaders do codec")) : e2;
                    if (erro) erro->clear();
                    m_modo = PonteGpu::Alto;
                    Soltar(m_csEnc); Soltar(m_csDec); Soltar(m_cbA); Soltar(m_cbB); Soltar(m_sampB);
                    m_medEnc.Parar(); m_medDec.Parar();
                }
            }
            if (m_modo == PonteGpu::Codec) {
                // fence de "o destino largou o slot" sem D3D12 (o caminho cru usa a do D3D12)
                if (FAILED(m_devB->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), (void**)&m_fLivre11)) || !m_fLivre11)
                { if (erro) *erro = L"ponte: CreateFence (D3D11) do slot livre falhou"; Parar(); return false; }
                m_nPlanos = 0;
                IniciarThreadCodec();
                m_enviados = 0; m_descartados = 0; m_recebidos = 0;
                m_pronto.store(0); m_recebidoUltimo = 0;
                m_ok = true;
                return true;
            }
            m_nPlanos = 2;
            D3D11_TEXTURE2D_DESC pd = td;
            pd.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
            pd.Format = DXGI_FORMAT_R8_UNORM;   m_plano[0].desc = pd;
            pd.Format = DXGI_FORMAT_R8G8_UNORM; pd.Width = m_uw; pd.Height = m_uh; m_plano[1].desc = pd;
            for (int p = 0; p < m_nPlanos; p++) {
                Plano& P = m_plano[p];
                D3D11_TEXTURE2D_DESC da = P.desc; da.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                D3D11_TEXTURE2D_DESC db = P.desc; db.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                for (int i = 0; i < kSlots; i++) {
                    if (!CriarTexturaCompartilhada(m_devA, m_A.dev, da, &P.texA[i], &P.resA[i], erro, p ? L"plano UV (origem)" : L"plano Y (origem)")) { Parar(); return false; }
                    if (!CriarTexturaCompartilhada(m_devB, m_B.dev, db, &P.texB[i], &P.resB[i], erro, p ? L"plano UV (destino)" : L"plano Y (destino)")) { Parar(); return false; }
                    HRESULT hr = m_devA->CreateUnorderedAccessView(P.texA[i], nullptr, &P.uavA[i]);
                    if (FAILED(hr) || !P.uavA[i]) { Lado12::Erro(erro, L"origem", L"CreateUnorderedAccessView (plano)", hr); Parar(); return false; }
                    hr = m_devB->CreateShaderResourceView(P.texB[i], nullptr, &P.srvB[i]);
                    if (FAILED(hr) || !P.srvB[i]) { Lado12::Erro(erro, L"destino", L"CreateShaderResourceView (plano)", hr); Parar(); return false; }
                }
            }
            if (!CriarShaders(erro)) { Parar(); return false; }
        }

        // ---- o heap cross-adapter, em A, aberto em B. Um buffer com o footprint de cada
        // plano, um atras do outro: e o caminho que TODA placa aceita (a textura row-major
        // cross-adapter e opcional). Em Max ha um plano so, o BGRA inteiro, como sempre.
        {
            UINT64 total = 0;
            for (int p = 0; p < m_nPlanos; p++) {
                D3D12_RESOURCE_DESC tex{};
                tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                tex.Width = m_plano[p].desc.Width; tex.Height = m_plano[p].desc.Height;
                tex.DepthOrArraySize = 1; tex.MipLevels = 1;
                tex.Format = m_plano[p].desc.Format; tex.SampleDesc.Count = 1;
                tex.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
                UINT linhas = 0; UINT64 bytesLinha = 0, bytes = 0;
                // (o footprint so depende do desc e das constantes do D3D12: e o mesmo nas duas placas)
                m_A.dev->GetCopyableFootprints(&tex, 0, 1, total, &m_plano[p].peg, &linhas, &bytesLinha, &bytes);
                if (p == 0) { m_linhas = linhas; m_bytesLinha = bytesLinha; }
                total += (bytes + (D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1)) & ~(UINT64)(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
            }

            D3D12_RESOURCE_DESC bd{};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
            bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1;
            bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
            // O tamanho do slot e o que o proprio device diz para este buffer (e nao so o
            // footprint): e o que o CreatePlacedResource vai cobrar.
            const D3D12_RESOURCE_ALLOCATION_INFO ai = m_A.dev->GetResourceAllocationInfo(0, 1, &bd);
            if (ai.SizeInBytes == UINT64(-1)) { if (erro) *erro = L"ponte: GetResourceAllocationInfo recusou o buffer cross-adapter"; Parar(); return false; }
            m_tamSlot = (ai.SizeInBytes + (D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT - 1)) & ~(UINT64)(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT - 1);

            D3D12_HEAP_DESC hd{};
            hd.SizeInBytes = m_tamSlot * kSlots;
            hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            hd.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
            // Sem ALLOW_ONLY_*: com ALLOW_ONLY_BUFFERS o CreateHeap devolveu E_INVALIDARG aqui
            // (12/09/2026). Fica como segunda tentativa, para uma placa de tier 1 que exija.
            hd.Flags = D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
            HRESULT hr = m_A.dev->CreateHeap(&hd, __uuidof(ID3D12Heap), (void**)&m_heapA);
            if (FAILED(hr) || !m_heapA) {
                hd.Flags |= D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
                hr = m_A.dev->CreateHeap(&hd, __uuidof(ID3D12Heap), (void**)&m_heapA);
            }
            if (FAILED(hr) || !m_heapA) { Lado12::Erro(erro, L"ponte", L"CreateHeap (cross-adapter)", hr); Parar(); return false; }
            HANDLE hh = nullptr;
            hr = m_A.dev->CreateSharedHandle(m_heapA, nullptr, GENERIC_ALL, nullptr, &hh);
            if (FAILED(hr) || !hh) { Lado12::Erro(erro, L"ponte", L"CreateSharedHandle (heap)", hr); Parar(); return false; }
            hr = m_B.dev->OpenSharedHandle(hh, __uuidof(ID3D12Heap), (void**)&m_heapB);
            CloseHandle(hh);
            if (FAILED(hr) || !m_heapB) { Lado12::Erro(erro, L"ponte", L"OpenSharedHandle (heap na outra placa)", hr); Parar(); return false; }

            // os slots: o MESMO desc nos dois lados, a passos de m_tamSlot dentro do heap
            for (int i = 0; i < kSlots; i++) {
                hr = m_A.dev->CreatePlacedResource(m_heapA, m_tamSlot * i, &bd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                   __uuidof(ID3D12Resource), (void**)&m_crossA[i]);
                if (FAILED(hr)) { Lado12::Erro(erro, L"ponte", L"CreatePlacedResource (A)", hr); Parar(); return false; }
                hr = m_B.dev->CreatePlacedResource(m_heapB, m_tamSlot * i, &bd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                   __uuidof(ID3D12Resource), (void**)&m_crossB[i]);
                if (FAILED(hr)) { Lado12::Erro(erro, L"ponte", L"CreatePlacedResource (B)", hr); Parar(); return false; }
            }
        }

        // ---- fences
        //   fA: origem D3D11 -> D3D12 de A   (compartilhada, mesma placa)
        //   fX: D3D12 de A -> D3D12 de B     (compartilhada ENTRE placas)
        //   fB: D3D12 de B -> destino D3D11  (compartilhada, mesma placa)
        //   fLivre: destino D3D11 -> D3D12 de B ("acabei de usar o slot")
        if (!CriarFence(m_A.dev, false, &m_fA, erro, L"fA")) { Parar(); return false; }
        if (!CriarFence(m_A.dev, true,  &m_fX, erro, L"fX")) { Parar(); return false; }
        if (!CriarFence(m_B.dev, false, &m_fB, erro, L"fB")) { Parar(); return false; }
        if (!CriarFence(m_B.dev, false, &m_fLivre, erro, L"fLivre")) { Parar(); return false; }
        if (!AbrirFenceEm11(m_A.dev, m_fA, m_devA, &m_fA11, erro, L"fA")) { Parar(); return false; }
        if (!AbrirFenceEm12(m_A.dev, m_fX, m_B.dev, &m_fXB, erro, L"fX")) { Parar(); return false; }
        if (!AbrirFenceEm11(m_B.dev, m_fB, m_devB, &m_fB11, erro, L"fB")) { Parar(); return false; }
        if (!AbrirFenceEm11(m_B.dev, m_fLivre, m_devB, &m_fLivre11, erro, L"fLivre")) { Parar(); return false; }

        m_enviados = 0; m_descartados = 0; m_recebidos = 0;
        m_pronto.store(0); m_recebidoUltimo = 0;
        m_ok = true;
        return true;
    }

    bool Ativa() const { return m_ok; }
    UINT Largura() const { return m_w; }
    UINT Altura()  const { return m_h; }
    DXGI_FORMAT Formato() const { return m_fmt; }
    bool MesmaPlaca() const { return m_mesmaPlaca; }
    uint64_t Enviados()    const { return m_enviados; }
    uint64_t Descartados() const { return m_descartados; }
    uint64_t Recebidos()   const { return m_recebidos; }
    UINT64   BytesPorQuadro() const { return m_tamSlot; }
    /* Quanto atravessa por quadro, em texto (em Codec nao ha tamanho fixo: e o pacote). */
    std::string ResumoBytes() const {
        char b[96];
        if (m_direto) snprintf(b, sizeof b, "%.1f MB em VRAM (nada atravessa)", (double)m_w * m_h * 4.0 / 1048576.0);
        else if (m_modo == PonteGpu::Codec) snprintf(b, sizeof b, "pacote HEVC (~0,2 MB em 4K)");
        else snprintf(b, sizeof b, "%.1f MB por quadro", m_tamSlot / 1048576.0);
        return b;
    }
    bool FilasDeCopia() const { return m_A.filaDeCopia && m_B.filaDeCopia; }
    /* O tipo de fila D3D12 em uso nas duas pontas deste sentido ("COPY" / "DIRECT"). */
    const char* NomeFila() const { return m_direto ? "-(direta)" : NomeTipoFila(m_A.tipo); }
    D3D12_COMMAND_LIST_TYPE TipoDaFila() const { return m_A.tipo; }
    /* O formato de travessia EM USO (pode ser Max mesmo com Alto pedido - ver MotivoMax). */
    PonteGpu::Formato Compactacao() const { return m_modo; }
    PonteGpu::Formato Pedido() const { return m_pedido; }
    const std::wstring& MotivoMax() const { return m_motivoQueda; }   // por que o nivel pedido nao saiu
    /* Custo medio, NA GPU, do codificador (origem) e do decodificador (destino), em ms. */
    double CodificarMs()   const { return m_medEnc.Media(); }
    double DecodificarMs() const { return m_medDec.Media(); }

    /* Lado da ORIGEM. Copia `tex` (mesmo tamanho e formato) para o proximo slot e enfileira
       as duas travessias. Devolve false quando o anel esta cheio (quadro descartado) ou
       quando a ponte nao esta de pe. Nunca espera a GPU. */
    bool Enviar(ID3D11Texture2D* tex) {
        if (!m_ok || !tex) return false;
        if (m_modo == PonteGpu::Codec) return EnviarCodec(tex);
        // ---- travessia DIRETA (mesma placa): uma copia VRAM->VRAM e uma fence. Nada de
        //      D3D12, nada de memoria do sistema, nada de subamostragem de croma.
        if (m_direto) {
            const uint64_t n = m_enviados + 1;
            const int i = (int)(n % kSlots);
            if (n > (uint64_t)kSlots && m_fLivreA11->GetCompletedValue() < n - kSlots) { m_descartados++; return false; }
            m_ctxA->CopyResource(m_texOrig[i], tex);
            m_ctxA->Signal(m_fA11, n);
            m_ctxA->Flush();   // sem o Flush o Signal so sai no proximo Present: 1 quadro de atraso
            m_enviados = n;
            m_pronto.store(n, std::memory_order_release);
            return true;
        }
        const uint64_t n = m_enviados + 1;
        const int i = (int)(n % kSlots);
        if (n > (uint64_t)kSlots) {
            // o slot i foi do quadro n-kSlots: as duas copias dele ja sairam e o destino ja
            // o largou? Se nao, este quadro cai fora - conferido na CPU, sem esperar.
            const uint64_t velho = n - kSlots;
            if (m_fX->GetCompletedValue() < velho || m_fB->GetCompletedValue() < velho ||
                m_fLivre->GetCompletedValue() < velho) {
                m_descartados++;
                return false;
            }
        }
        // 1. origem D3D11: copia (ou codifica) e sinaliza. O Flush e obrigatorio: sem ele o
        //    Signal fica no lote do contexto ate o proximo Present, e a travessia atrasaria
        //    um quadro.
        if (m_modo == PonteGpu::Max) m_ctxA->CopyResource(m_texOrig[i], tex);
        else if (!Codificar(tex, i)) return false;
        m_ctxA->Signal(m_fA11, n);
        m_ctxA->Flush();

        // 2. D3D12 de A: espera a origem, copia cada plano -> seu trecho do buffer
        //    cross-adapter, sinaliza X
        {
            ID3D12GraphicsCommandList* l = m_A.lista[i];
            m_A.alloc[i]->Reset();
            l->Reset(m_A.alloc[i], nullptr);
            for (int p = 0; p < m_nPlanos; p++) {
                D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
                dst.pResource = m_crossA[i]; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = m_plano[p].peg;
                src.pResource = (m_modo == PonteGpu::Max) ? m_resOrig12[i] : m_plano[p].resA[i];
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
                l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }
            l->Close();
            ID3D12CommandList* ls[] = { l };
            m_A.fila->Wait(m_fA, n);
            m_A.fila->ExecuteCommandLists(1, ls);
            m_A.fila->Signal(m_fX, n);
        }
        // 3. D3D12 de B: espera X, copia cada trecho do buffer -> plano de destino, sinaliza B
        {
            ID3D12GraphicsCommandList* l = m_B.lista[i];
            m_B.alloc[i]->Reset();
            l->Reset(m_B.alloc[i], nullptr);
            for (int p = 0; p < m_nPlanos; p++) {
                D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
                dst.pResource = (m_modo == PonteGpu::Max) ? m_resDest12[i] : m_plano[p].resB[i];
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = 0;
                src.pResource = m_crossB[i]; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = m_plano[p].peg;
                l->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }
            l->Close();
            ID3D12CommandList* ls[] = { l };
            m_B.fila->Wait(m_fXB, n);
            m_B.fila->ExecuteCommandLists(1, ls);
            m_B.fila->Signal(m_fB, n);
        }
        m_enviados = n;
        m_pronto.store(n, std::memory_order_release);
        return true;
    }

    /* Lado do DESTINO. Devolve a textura do quadro mais novo enviado (ou nulo se nao ha
       quadro novo desde o ultimo Receber), ja com a espera pela copia enfileirada NA GPU
       do destino - a CPU nao para. Quem chama usa a textura no contexto do destino e
       depois chama Liberar(quadro). Em Alto, a reconversao NV12 -> BGRA e enfileirada aqui,
       logo depois da espera, no mesmo contexto - quem usa a textura ja a ve pronta. */
    ID3D11Texture2D* Receber(uint64_t* quadro) {
        if (!m_ok) return nullptr;
        const uint64_t n = m_pronto.load(std::memory_order_acquire);
        if (n == 0 || n == m_recebidoUltimo) return nullptr;
        m_recebidoUltimo = n;
        m_recebidos++;
        // Em Codec nao ha fence entre placas: a thread do codec so publica `n` DEPOIS de o
        // decodificador ter terminado na GPU do destino (ela mede isso com uma query).
        if (m_modo != PonteGpu::Codec) m_ctxB->Wait(m_fB11, n);
        const int i = (int)(n % kSlots);
        if (!m_direto && m_modo != PonteGpu::Max) Decodificar(i);
        if (quadro) *quadro = n;
        return m_texDest[i];
    }
    /* Depois de enfileirar o uso da textura no contexto do destino: avisa que o slot pode
       ser reescrito quando esse uso terminar (na GPU). */
    void Liberar(uint64_t quadro) {
        if (!m_ok || !quadro) return;
        m_ctxB->Signal(m_fLivre11, quadro);
    }
    /* Quantos quadros ja atravessaram por completo (fB sinalizada). Diagnostico. */
    uint64_t Atravessados() const {
        if (m_direto) return m_fA11 ? m_fA11->GetCompletedValue() : 0;
        if (m_modo == PonteGpu::Codec) return m_pronto.load(std::memory_order_relaxed);
        return m_fB ? m_fB->GetCompletedValue() : 0;
    }
    /* A travessia esta no caminho DIRETO (mesma placa, textura em VRAM)? E, se nao esta e
       devia estar, por que nao. */
    bool Direta() const { return m_direto; }
    const std::wstring& MotivoDireta() const { return m_motivoDireto; }

    /* ---------------- telemetria do nivel Codec (0 nos outros niveis) ---------------- */
    double   CodecMbPorQuadro() const { return m_mbQuadro; }
    double   CodecEncMs()       const { return m_msEnc; }
    double   CodecDecMs()       const { return m_msDec; }
    int      CodecMbps()        const { return m_modo == PonteGpu::Codec ? m_codec.Mbps() : 0; }
    std::string NomeEncoder()   const { return m_modo == PonteGpu::Codec ? m_codec.NomeEncoder() : std::string(); }
    uint64_t CodecErros()       const { return m_errosCodec.load(std::memory_order_relaxed); }
    bool     CodecQuebrou()     const { return m_codecQuebrou.load(std::memory_order_relaxed); }
    /* Por que o nivel PEDIDO nao saiu (vazio quando saiu). */
    const std::wstring& MotivoQueda() const { return m_motivoQueda; }
    std::wstring UltimoErroCodec() { std::lock_guard<std::mutex> lk(m_mxErro); return m_erroCodec; }
    /* Bitrate alvo do codec, em Mbps a 4K (escalado por pixels la dentro). So antes de Iniciar. */
    void DefinirMbps(int mbps) { m_mbps = mbps; }

    void Parar() {
        m_ok = false;
        PararThreadCodec();
        m_codec.Parar();
        SoltarDireto();
        // Espera o que esta em voo terminar antes de soltar: uma copia pendente num
        // recurso liberado e device removed na certa.
        if (m_A.fila && m_fX) EsperarFence(m_A.fila, m_A.dev, m_enviados);
        if (m_B.fila && m_fB) EsperarFence(m_B.fila, m_B.dev, m_enviados);
        Soltar(m_fA11); Soltar(m_fB11); Soltar(m_fLivre11);
        Soltar(m_fXB);
        Soltar(m_fA); Soltar(m_fX); Soltar(m_fB); Soltar(m_fLivre);
        for (int i = 0; i < kSlots; i++) {
            Soltar(m_crossA[i]); Soltar(m_crossB[i]);
            Soltar(m_resOrig12[i]); Soltar(m_resDest12[i]);
            Soltar(m_srvOrig[i]); Soltar(m_uavDest[i]);
            Soltar(m_texOrig[i]); Soltar(m_texDest[i]);
        }
        for (int p = 0; p < 2; p++) for (int i = 0; i < kSlots; i++) {
            Soltar(m_plano[p].uavA[i]); Soltar(m_plano[p].srvB[i]);
            Soltar(m_plano[p].resA[i]); Soltar(m_plano[p].resB[i]);
            Soltar(m_plano[p].texA[i]); Soltar(m_plano[p].texB[i]);
        }
        m_nPlanos = 0;
        Soltar(m_srvTex); Soltar(m_srvTexPtr);
        Soltar(m_csEnc); Soltar(m_csDec); Soltar(m_cbA); Soltar(m_cbB); Soltar(m_sampB);
        m_medEnc.Parar(); m_medDec.Parar();
        Soltar(m_heapA); Soltar(m_heapB);
        m_A.Parar(); m_B.Parar();
        Soltar(m_ctxA); Soltar(m_ctxB);
        Soltar(m_devA); Soltar(m_devB);
        m_pronto.store(0); m_recebidoUltimo = 0;
    }
    ~Ponte() { Parar(); }

private:
    /* ============================ o caminho do nivel Codec ============================
       O quadro nao cruza como bytes de imagem: o shader de codificacao escreve a NV12 do
       pool do encoder, uma THREAD PROPRIA chama o encoder (NVENC/AMF sao sincronos, 3-6 ms)
       e o decodificador do outro lado, e so entao o quadro e publicado. A thread existe
       para que NEM o laco que captura NEM o que apresenta parem nesses milissegundos.
       Um quadro por vez: enquanto a thread trabalha, o proximo quadro e DESCARTADO antes
       de ser codificado (descarte pre-encode - com VBV curto, codificar para jogar fora
       estragaria a cadeia de referencias do proximo). */
    void IniciarThreadCodec() {
        m_pararThread = false; m_temPed = false; m_forcarI = true;
        m_thrCodec = std::thread([this] { LacoCodec(); });
    }
    void PararThreadCodec() {
        if (!m_thrCodec.joinable()) return;
        { std::lock_guard<std::mutex> lk(m_mxCodec); m_pararThread = true; }
        m_cvCodec.notify_all();
        m_thrCodec.join();
    }
    void LacoCodec() {
        std::vector<uint8_t> pacote;
        for (;;) {
            PedidoCodec p{};
            {
                std::unique_lock<std::mutex> lk(m_mxCodec);
                m_cvCodec.wait(lk, [this] { return m_temPed || m_pararThread; });
                if (m_pararThread) return;
                p = m_ped;
            }
            double msE = 0, msD = 0; std::wstring e;
            bool ok = m_codec.Codificar(p.slot, p.n, m_forcarI, pacote, &msE, &e);
            if (ok) {
                m_forcarI = false;
                m_mbQuadro = pacote.size() / 1048576.0;
                ok = m_codec.Decodificar(pacote.data(), pacote.size(), p.slot, p.n, nullptr, &msD, &e);
            }
            if (ok) {
                m_msEnc = m_msEnc > 0 ? m_msEnc * 0.9 + msE * 0.1 : msE;
                m_msDec = m_msDec > 0 ? m_msDec * 0.9 + msD * 0.1 : msD;
                m_pronto.store(p.n, std::memory_order_release);
            } else {
                // falhou: o proximo quadro vai com chave, e se insistir em falhar o nivel
                // se declara quebrado (quem chama cai para Alto - nunca tela preta).
                m_forcarI = true;
                { std::lock_guard<std::mutex> lk(m_mxErro); m_erroCodec = e; }
                if (m_errosCodec.fetch_add(1, std::memory_order_relaxed) + 1 >= 30)
                    m_codecQuebrou.store(true, std::memory_order_relaxed);
            }
            { std::lock_guard<std::mutex> lk(m_mxCodec); m_temPed = false; }
        }
    }
    bool EnviarCodec(ID3D11Texture2D* tex) {
        const uint64_t n = m_enviados + 1;
        const int i = (int)(n % kSlots);
        // o slot i foi do quadro n-kSlots: o destino ja o largou?
        if (n > (uint64_t)kSlots && m_fLivre11 && m_fLivre11->GetCompletedValue() < n - (uint64_t)kSlots)
        { m_descartados++; return false; }
        // DESCARTE PRE-ENCODE: se a thread ainda esta no quadro anterior, este nao chega a
        // ser codificado. Nada entra no encoder para depois ser jogado fora.
        { std::lock_guard<std::mutex> lk(m_mxCodec); if (m_temPed) { m_descartados++; return false; } }
        if (!Codificar(tex, i)) { m_descartados++; return false; }
        m_ctxA->Flush();
        { std::lock_guard<std::mutex> lk(m_mxCodec); m_ped = { i, n }; m_temPed = true; }
        m_cvCodec.notify_one();
        m_enviados = n;
        return true;
    }

    struct Plano {
        D3D11_TEXTURE2D_DESC desc{};                       // o mesmo nas duas pontas
        ID3D11Texture2D* texA[kSlots] = {}; ID3D12Resource* resA[kSlots] = {};
        ID3D11Texture2D* texB[kSlots] = {}; ID3D12Resource* resB[kSlots] = {};
        ID3D11UnorderedAccessView* uavA[kSlots] = {};      // o codificador escreve (origem)
        ID3D11ShaderResourceView*  srvB[kSlots] = {};      // o decodificador le (destino)
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT peg{};          // o trecho do buffer cross-adapter
    };
    // faixa: 0 = cheia (0-255), 1 = LIMITADA 16-235/16-240. O NV12 puro usa a cheia (a
    // decodificacao e nossa); com o codec no meio a faixa TEM de ser a limitada de BT.709,
    // porque e o que NVENC e AMF assumem por padrao - misturar as duas troca os niveis.
    struct Constantes { UINT w, h, uw, uh; UINT faixa, pad0, pad1, pad2; };

    // ---- os shaders do formato Alto (NV12). BT.709 em faixa cheia (0..255): a
    // decodificacao e nossa, nao ha por que jogar fora os 16 niveis da faixa limitada.
    // Codificador: uma thread por par 2x2 - escreve 4 lumas e 1 croma (media dos 4).
    // Decodificador: uma thread por pixel; o croma e reamostrado bilinear (amostra no
    // centro do pixel, no plano de meia resolucao) para nao virar bloquinhos 2x2.
    static const char* HlslCodificar() {
        return
            "cbuffer C : register(b0) { uint W; uint H; uint UW; uint UH; uint FAIXA; };\n"
            "Texture2D<float4> src : register(t0);\n"
            "RWTexture2D<float>  planoY  : register(u0);\n"
            "RWTexture2D<float2> planoUV : register(u1);\n"
            "float3 ParaYuv(float3 c) { float y = dot(c, float3(0.2126, 0.7152, 0.0722));\n"
            "  float3 v = float3(y, (c.b - y) / 1.8556 + 0.5, (c.r - y) / 1.5748 + 0.5);\n"
            "  if (FAIXA != 0) v = float3(v.x * (219.0/255.0) + (16.0/255.0),\n"
            "                             (v.yz - 0.5) * (224.0/255.0) + (128.0/255.0));\n"
            "  return v; }\n"
            "[numthreads(8,8,1)]\n"
            "void Enc(uint3 id : SV_DispatchThreadID) {\n"
            "  if (id.x >= UW || id.y >= UH) return;\n"
            "  uint2 p = id.xy * 2, m = uint2(W - 1, H - 1);\n"
            "  float3 a = ParaYuv(src.Load(int3(p, 0)).rgb);\n"
            "  float3 b = ParaYuv(src.Load(int3(min(p + uint2(1, 0), m), 0)).rgb);\n"
            "  float3 c = ParaYuv(src.Load(int3(min(p + uint2(0, 1), m), 0)).rgb);\n"
            "  float3 d = ParaYuv(src.Load(int3(min(p + uint2(1, 1), m), 0)).rgb);\n"
            "  planoY[p] = a.x; planoY[p + uint2(1, 0)] = b.x;\n"
            "  planoY[p + uint2(0, 1)] = c.x; planoY[p + uint2(1, 1)] = d.x;\n"
            "  planoUV[id.xy] = (a.yz + b.yz + c.yz + d.yz) * 0.25;\n"
            "}\n";
    }
    static const char* HlslDecodificar() {
        return
            "cbuffer C : register(b0) { uint W; uint H; uint UW; uint UH; uint FAIXA; };\n"
            "Texture2D<float>  inY  : register(t0);\n"
            "Texture2D<float2> inUV : register(t1);\n"
            "SamplerState samp : register(s0);\n"
            "RWTexture2D<float4> dst : register(u0);\n"
            "[numthreads(8,8,1)]\n"
            "void Dec(uint3 id : SV_DispatchThreadID) {\n"
            "  if (id.x >= W || id.y >= H) return;\n"
            "  float y = inY[id.xy];\n"
            "  float2 uv = inUV.SampleLevel(samp, (float2(id.xy) + 0.5) / float2(UW * 2, UH * 2), 0);\n"
            "  if (FAIXA != 0) { y = (y - 16.0/255.0) * (255.0/219.0); uv = (uv - 128.0/255.0) * (255.0/224.0); }\n"
            "  else uv -= 0.5;\n"
            "  float r = y + 1.5748 * uv.y, b = y + 1.8556 * uv.x;\n"
            "  float g = (y - 0.2126 * r - 0.0722 * b) / 0.7152;\n"
            "  dst[id.xy] = float4(saturate(float3(r, g, b)), 1.0);\n"
            "}\n";
    }
    static bool CompilarCS(ID3D11Device* d, const char* src, const char* entrada, ID3D11ComputeShader** out, std::wstring* erro) {
        typedef HRESULT (WINAPI *PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
        static HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
        PFN_Compile compile = dc ? (PFN_Compile)(void*)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!compile) { if (erro) *erro = L"ponte: d3dcompiler_47.dll indisponivel"; return false; }
        ID3DBlob *b = nullptr, *e = nullptr;
        const HRESULT hr = compile(src, strlen(src), "ponte", nullptr, nullptr, entrada, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &e);
        if (FAILED(hr) || !b) {
            if (erro) {
                std::string msg = e ? std::string((const char*)e->GetBufferPointer(), e->GetBufferSize()) : std::string();
                *erro = L"ponte: o compute shader nao compilou: " + std::wstring(msg.begin(), msg.end());
            }
            Soltar(e); Soltar(b); return false;
        }
        Soltar(e);
        const HRESULT hc = d->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, out);
        b->Release();
        if (FAILED(hc) || !*out) { Lado12::Erro(erro, L"ponte", L"CreateComputeShader", hc); return false; }
        return true;
    }
    bool CriarShaders(std::wstring* erro) {
        if (!CompilarCS(m_devA, HlslCodificar(), "Enc", &m_csEnc, erro)) return false;
        if (!CompilarCS(m_devB, HlslDecodificar(), "Dec", &m_csDec, erro)) return false;
        // faixa LIMITADA (BT.709 16-235) so quando o codec esta no meio - e o que NVENC e
        // AMF assumem. Sem codec a decodificacao e nossa e a faixa cheia guarda 16 niveis.
        const Constantes c{ m_w, m_h, m_uw, m_uh, (m_modo == PonteGpu::Codec && m_faixaLim) ? 1u : 0u, 0, 0, 0 };
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth = sizeof(c); bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = &c;
        if (FAILED(m_devA->CreateBuffer(&bd, &sd, &m_cbA)) || FAILED(m_devB->CreateBuffer(&bd, &sd, &m_cbB)))
        { if (erro) *erro = L"ponte: constant buffer dos shaders nao criado"; return false; }
        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(m_devB->CreateSamplerState(&smp, &m_sampB)) || !m_sampB) { if (erro) *erro = L"ponte: sampler do decodificador nao criado"; return false; }
        m_gxA = (m_uw + 7) / 8; m_gyA = (m_uh + 7) / 8;
        m_gxB = (m_w + 7) / 8;  m_gyB = (m_h + 7) / 8;
        m_medEnc.Iniciar(m_devA); m_medDec.Iniciar(m_devB);   // opcional: sem query, so nao mede
        return true;
    }
    /* Uma SRV da textura recebida (cache por ponteiro). Se ela nao tiver o bind de SRV,
       copia para uma textura propria do slot - so entao se paga a copia local. */
    ID3D11ShaderResourceView* SrvOrigem(ID3D11Texture2D* tex, int i) {
        if (tex == m_srvTexPtr && m_srvTex) return m_srvTex;
        Soltar(m_srvTex); Soltar(m_srvTexPtr);
        if (SUCCEEDED(m_devA->CreateShaderResourceView(tex, nullptr, &m_srvTex)) && m_srvTex) {
            m_srvTexPtr = tex; tex->AddRef();
            return m_srvTex;
        }
        if (!m_texOrig[i]) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = m_w; td.Height = m_h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = m_fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            if (FAILED(m_devA->CreateTexture2D(&td, nullptr, &m_texOrig[i])) || !m_texOrig[i]) return nullptr;
            if (FAILED(m_devA->CreateShaderResourceView(m_texOrig[i], nullptr, &m_srvOrig[i]))) { Soltar(m_texOrig[i]); return nullptr; }
        }
        m_ctxA->CopyResource(m_texOrig[i], tex);
        return m_srvOrig[i];
    }
    bool Codificar(ID3D11Texture2D* tex, int i) {
        ID3D11ShaderResourceView* srv = SrvOrigem(tex, i);
        if (!srv) return false;
        m_medEnc.Antes(m_ctxA, i);
        m_ctxA->CSSetShader(m_csEnc, nullptr, 0);
        m_ctxA->CSSetConstantBuffers(0, 1, &m_cbA);
        m_ctxA->CSSetShaderResources(0, 1, &srv);
        // Em Codec os planos NV12 sao os do POOL DO ENCODER (zero copia); em Alto, os planos
        // proprios que cruzam pelo heap.
        ID3D11UnorderedAccessView* uavs[2];
        if (m_modo == PonteGpu::Codec) { uavs[0] = m_codec.UavY(i);      uavs[1] = m_codec.UavUV(i); }
        else                           { uavs[0] = m_plano[0].uavA[i];   uavs[1] = m_plano[1].uavA[i]; }
        if (!uavs[0] || !uavs[1]) return false;
        m_ctxA->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
        m_ctxA->Dispatch(m_gxA, m_gyA, 1);
        ID3D11UnorderedAccessView* nu[2] = {}; ID3D11ShaderResourceView* ns = nullptr; ID3D11Buffer* nb = nullptr;
        m_ctxA->CSSetUnorderedAccessViews(0, 2, nu, nullptr);
        m_ctxA->CSSetShaderResources(0, 1, &ns);
        m_ctxA->CSSetConstantBuffers(0, 1, &nb);
        m_ctxA->CSSetShader(nullptr, nullptr, 0);
        m_medEnc.Depois(m_ctxA, i);
        return true;
    }
    void Decodificar(int i) {
        ID3D11ShaderResourceView* srvs[2];
        if (m_modo == PonteGpu::Codec) { srvs[0] = m_codec.SrvY(i);    srvs[1] = m_codec.SrvUV(i); }
        else                           { srvs[0] = m_plano[0].srvB[i]; srvs[1] = m_plano[1].srvB[i]; }
        if (!srvs[0] || !srvs[1]) return;
        m_medDec.Antes(m_ctxB, i);
        m_ctxB->CSSetShader(m_csDec, nullptr, 0);
        m_ctxB->CSSetConstantBuffers(0, 1, &m_cbB);
        m_ctxB->CSSetShaderResources(0, 2, srvs);
        m_ctxB->CSSetSamplers(0, 1, &m_sampB);
        m_ctxB->CSSetUnorderedAccessViews(0, 1, &m_uavDest[i], nullptr);
        m_ctxB->Dispatch(m_gxB, m_gyB, 1);
        ID3D11UnorderedAccessView* nu = nullptr; ID3D11ShaderResourceView* ns[2] = {}; ID3D11SamplerState* nsm = nullptr; ID3D11Buffer* nb = nullptr;
        m_ctxB->CSSetUnorderedAccessViews(0, 1, &nu, nullptr);
        m_ctxB->CSSetShaderResources(0, 2, ns);
        m_ctxB->CSSetSamplers(0, 1, &nsm);
        m_ctxB->CSSetConstantBuffers(0, 1, &nb);
        m_ctxB->CSSetShader(nullptr, nullptr, 0);
        m_medDec.Depois(m_ctxB, i);
    }
    /* Da para fazer NV12 por compute nesta dupla de placas e neste formato? */
    bool PodeAlto(DXGI_FORMAT fmt, std::wstring* motivo) const {
        if (fmt != DXGI_FORMAT_B8G8R8A8_UNORM && fmt != DXGI_FORMAT_R8G8B8A8_UNORM)
        { if (motivo) *motivo = L"o formato de entrada nao e 8 bits UNORM"; return false; }
        if (!SuportaUav(m_devB, fmt))
        { if (motivo) *motivo = L"a placa de destino nao escreve BGRA8 por UAV"; return false; }
        if (!SuportaUav(m_devA, DXGI_FORMAT_R8_UNORM) || !SuportaUav(m_devA, DXGI_FORMAT_R8G8_UNORM))
        { if (motivo) *motivo = L"a placa de origem nao escreve R8/R8G8 por UAV"; return false; }
        return true;
    }
    static bool SuportaUav(ID3D11Device* d, DXGI_FORMAT f) {
        UINT s = 0;
        return SUCCEEDED(d->CheckFormatSupport(f, &s)) && (s & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW);
    }
    static IDXGIAdapter* AdaptadorDe(ID3D11Device* d) {
        IDXGIDevice* dx = nullptr; IDXGIAdapter* ad = nullptr;
        if (SUCCEEDED(d->QueryInterface(__uuidof(IDXGIDevice), (void**)&dx)) && dx) { dx->GetAdapter(&ad); dx->Release(); }
        return ad;
    }
    /* ---------------- TRAVESSIA DIRETA: montagem (ver o bloco no Iniciar) ----------------
       Tudo D3D11: kSlots texturas criadas em A com NTHANDLE e ABERTAS em B (mesma placa =
       mesma VRAM, nada e copiado para a memoria do sistema), mais duas fences compartilhadas:
         fA  - A sinaliza "a copia deste quadro terminou"; B espera nela antes de usar.
         fL  - B sinaliza "larguei o slot"; A le na CPU antes de reescrever (sem esperar). */
    bool IniciarDireto(UINT w, UINT h, DXGI_FORMAT fmt, std::wstring* erro) {
        // BANCADA - DLSS5_PONTE_DIRETO=falha: finge que a montagem da direta nao deu, para
        // provar que a queda para o caminho de duas placas acontece de verdade (e nao so no
        // papel). `=0` nem tenta; `=falha` tenta e falha aqui.
        { wchar_t e[16]{}; if (GetEnvironmentVariableW(L"DLSS5_PONTE_DIRETO", e, 16) > 0 && (e[0] == L'f' || e[0] == L'F'))
          { if (erro) *erro = L"BANCADA: DLSS5_PONTE_DIRETO=falha"; return false; } }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
        ID3D11Device1* d1B = nullptr;
        if (FAILED(m_devB->QueryInterface(__uuidof(ID3D11Device1), (void**)&d1B)) || !d1B)
        { if (erro) *erro = L"o device de destino nao tem ID3D11Device1"; return false; }
        bool ok = true;
        for (int i = 0; i < kSlots && ok; i++) {
            HRESULT hr = m_devA->CreateTexture2D(&td, nullptr, &m_texOrig[i]);
            if (FAILED(hr) || !m_texOrig[i]) { Lado12::Erro(erro, L"direta", L"CreateTexture2D (compartilhada em VRAM)", hr); ok = false; break; }
            IDXGIResource1* r1 = nullptr; HANDLE h = nullptr;
            if (SUCCEEDED(m_texOrig[i]->QueryInterface(__uuidof(IDXGIResource1), (void**)&r1)) && r1) {
                hr = r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h);
                r1->Release();
            } else hr = E_NOINTERFACE;
            if (FAILED(hr) || !h) { Lado12::Erro(erro, L"direta", L"CreateSharedHandle (textura)", hr); ok = false; break; }
            hr = d1B->OpenSharedResource1(h, __uuidof(ID3D11Texture2D), (void**)&m_texDest[i]);
            CloseHandle(h);
            if (FAILED(hr) || !m_texDest[i]) { Lado12::Erro(erro, L"direta", L"OpenSharedResource1 (textura no device do apresentador)", hr); ok = false; break; }
        }
        d1B->Release();
        if (!ok) return false;
        // fences: a de A vai aberta em B; a de B vai aberta em A (so para a CPU ler o valor)
        HRESULT hr = m_devA->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void**)&m_fA11);
        if (FAILED(hr) || !m_fA11) { Lado12::Erro(erro, L"direta", L"CreateFence (origem)", hr); return false; }
        hr = m_devB->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void**)&m_fLivre11);
        if (FAILED(hr) || !m_fLivre11) { Lado12::Erro(erro, L"direta", L"CreateFence (destino)", hr); return false; }
        if (!AbrirFence11Em11(m_fA11, m_devB, &m_fB11, erro, L"fA->B")) return false;
        if (!AbrirFence11Em11(m_fLivre11, m_devA, &m_fLivreA11, erro, L"fLivre->A")) return false;
        return true;
    }
    static bool AbrirFence11Em11(ID3D11Fence* f, ID3D11Device5* outro, ID3D11Fence** out, std::wstring* erro, const wchar_t* nome) {
        HANDLE h = nullptr;
        HRESULT hr = f->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &h);
        if (FAILED(hr) || !h) { Lado12::Erro(erro, nome, L"CreateSharedHandle (fence D3D11)", hr); return false; }
        hr = outro->OpenSharedFence(h, __uuidof(ID3D11Fence), (void**)out);
        CloseHandle(h);
        if (FAILED(hr) || !*out) { Lado12::Erro(erro, nome, L"OpenSharedFence (D3D11)", hr); return false; }
        return true;
    }
    /* Espera a GPU terminar o que esta em voo e solta tudo o que a direta criou. */
    void SoltarDireto() {
        if (m_direto && m_fA11 && m_enviados) {
            // nada de Release com copia pendente: espera a ultima copia e o ultimo uso
            HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (ev) {
                if (m_fA11->GetCompletedValue() < m_enviados && SUCCEEDED(m_fA11->SetEventOnCompletion(m_enviados, ev)))
                    WaitForSingleObject(ev, 2000);
                CloseHandle(ev);
            }
        }
        if (m_ctxB) { m_ctxB->Flush(); }
        Soltar(m_fLivreA11);
        m_direto = false;
    }
    static bool CriarTexturaCompartilhada(ID3D11Device* d11, ID3D12Device* d12, const D3D11_TEXTURE2D_DESC& td,
                                          ID3D11Texture2D** tex, ID3D12Resource** res, std::wstring* erro, const wchar_t* lado) {
        HRESULT hr = d11->CreateTexture2D(&td, nullptr, tex);
        if (FAILED(hr) || !*tex) { Lado12::Erro(erro, lado, L"CreateTexture2D (compartilhada)", hr); return false; }
        IDXGIResource1* r1 = nullptr; HANDLE h = nullptr;
        if (SUCCEEDED((*tex)->QueryInterface(__uuidof(IDXGIResource1), (void**)&r1)) && r1) {
            hr = r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h);
            r1->Release();
        } else hr = E_NOINTERFACE;
        if (FAILED(hr) || !h) { Lado12::Erro(erro, lado, L"CreateSharedHandle (textura)", hr); return false; }
        hr = d12->OpenSharedHandle(h, __uuidof(ID3D12Resource), (void**)res);
        CloseHandle(h);
        if (FAILED(hr) || !*res) { Lado12::Erro(erro, lado, L"OpenSharedHandle (textura no D3D12)", hr); return false; }
        return true;
    }
    static bool CriarFence(ID3D12Device* d, bool entrePlacas, ID3D12Fence** f, std::wstring* erro, const wchar_t* nome) {
        D3D12_FENCE_FLAGS fl = D3D12_FENCE_FLAG_SHARED;
        if (entrePlacas) fl |= D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER;
        const HRESULT hr = d->CreateFence(0, fl, __uuidof(ID3D12Fence), (void**)f);
        if (FAILED(hr) || !*f) { Lado12::Erro(erro, nome, L"CreateFence", hr); return false; }
        return true;
    }
    static bool AbrirFenceEm11(ID3D12Device* dono, ID3D12Fence* f, ID3D11Device5* d11, ID3D11Fence** out, std::wstring* erro, const wchar_t* nome) {
        HANDLE h = nullptr;
        HRESULT hr = dono->CreateSharedHandle(f, nullptr, GENERIC_ALL, nullptr, &h);
        if (FAILED(hr) || !h) { Lado12::Erro(erro, nome, L"CreateSharedHandle (fence)", hr); return false; }
        hr = d11->OpenSharedFence(h, __uuidof(ID3D11Fence), (void**)out);
        CloseHandle(h);
        if (FAILED(hr) || !*out) { Lado12::Erro(erro, nome, L"OpenSharedFence (D3D11)", hr); return false; }
        return true;
    }
    static bool AbrirFenceEm12(ID3D12Device* dono, ID3D12Fence* f, ID3D12Device* outro, ID3D12Fence** out, std::wstring* erro, const wchar_t* nome) {
        HANDLE h = nullptr;
        HRESULT hr = dono->CreateSharedHandle(f, nullptr, GENERIC_ALL, nullptr, &h);
        if (FAILED(hr) || !h) { Lado12::Erro(erro, nome, L"CreateSharedHandle (fence entre placas)", hr); return false; }
        hr = outro->OpenSharedHandle(h, __uuidof(ID3D12Fence), (void**)out);
        CloseHandle(h);
        if (FAILED(hr) || !*out) { Lado12::Erro(erro, nome, L"OpenSharedHandle (fence na outra placa)", hr); return false; }
        return true;
    }
    static void EsperarFence(ID3D12CommandQueue* fila, ID3D12Device* dev, uint64_t) {
        // Uma fence local so para saber que a fila esvaziou (tudo que foi enfileirado antes
        // desta sinalizacao ja rodou, inclusive as esperas).
        ID3D12Fence* f = nullptr;
        if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&f)) || !f) return;
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (ev) {
            fila->Signal(f, 1);
            if (f->GetCompletedValue() < 1) { f->SetEventOnCompletion(1, ev); WaitForSingleObject(ev, 2000); }
            CloseHandle(ev);
        }
        f->Release();
    }

    bool        m_ok = false;
    bool        m_mesmaPlaca = false;
    bool        m_direto = false;          // mesma placa: textura compartilhada em VRAM, sem D3D12
    std::wstring m_motivoDireto;           // por que a direta NAO entrou (vazio = entrou ou nao se aplica)
    ID3D11Fence* m_fLivreA11 = nullptr;    // a fence "larguei o slot" de B, aberta em A (so leitura na CPU)
    UINT        m_w = 0, m_h = 0;
    UINT        m_uw = 0, m_uh = 0;          // o plano de croma (metade, arredondada para cima)
    DXGI_FORMAT m_fmt = DXGI_FORMAT_UNKNOWN;
    PonteGpu::Formato m_pedido = PonteGpu::Max, m_modo = PonteGpu::Max;
    std::wstring m_motivoQueda;

    // ---- nivel Codec
    struct PedidoCodec { int slot; uint64_t n; };
    CodecVideo   m_codec;
    int          m_mbps = 100;
    bool         m_faixaLim = true;   // BT.709 16-235 no shader E no encoder (ver ponte_lav.h)
    std::thread  m_thrCodec;
    std::mutex   m_mxCodec;  std::condition_variable m_cvCodec;
    PedidoCodec  m_ped{};    bool m_temPed = false, m_pararThread = false, m_forcarI = true;
    std::mutex   m_mxErro;   std::wstring m_erroCodec;
    std::atomic<uint64_t> m_errosCodec{ 0 };
    std::atomic<bool>     m_codecQuebrou{ false };
    double m_mbQuadro = 0, m_msEnc = 0, m_msDec = 0;   // so a thread escreve; leitura e diagnostico

    ID3D11Device5*        m_devA = nullptr;  ID3D11DeviceContext4* m_ctxA = nullptr;
    ID3D11Device5*        m_devB = nullptr;  ID3D11DeviceContext4* m_ctxB = nullptr;
    Lado12 m_A, m_B;

    ID3D11Texture2D* m_texOrig[kSlots] = {};   ID3D12Resource* m_resOrig12[kSlots] = {};
    ID3D11Texture2D* m_texDest[kSlots] = {};   ID3D12Resource* m_resDest12[kSlots] = {};
    ID3D11ShaderResourceView*  m_srvOrig[kSlots] = {};   // Alto: SRV da copia local (so se a recebida nao tiver SRV)
    ID3D11UnorderedAccessView* m_uavDest[kSlots] = {};   // Alto: o decodificador escreve aqui
    ID3D11ShaderResourceView*  m_srvTex = nullptr; ID3D11Texture2D* m_srvTexPtr = nullptr;   // Alto: SRV direta da recebida
    Plano  m_plano[2]; int m_nPlanos = 0;
    ID3D11ComputeShader* m_csEnc = nullptr; ID3D11ComputeShader* m_csDec = nullptr;
    ID3D11Buffer* m_cbA = nullptr; ID3D11Buffer* m_cbB = nullptr;
    ID3D11SamplerState* m_sampB = nullptr;
    UINT m_gxA = 0, m_gyA = 0, m_gxB = 0, m_gyB = 0;
    MedidorGpu m_medEnc, m_medDec;
    ID3D12Heap*      m_heapA = nullptr;        ID3D12Heap*     m_heapB = nullptr;
    ID3D12Resource*  m_crossA[kSlots] = {};    ID3D12Resource* m_crossB[kSlots] = {};
    UINT   m_linhas = 0; UINT64 m_bytesLinha = 0; UINT64 m_tamSlot = 0;

    ID3D12Fence* m_fA = nullptr;     ID3D11Fence* m_fA11 = nullptr;
    ID3D12Fence* m_fX = nullptr;     ID3D12Fence* m_fXB = nullptr;
    ID3D12Fence* m_fB = nullptr;     ID3D11Fence* m_fB11 = nullptr;
    ID3D12Fence* m_fLivre = nullptr; ID3D11Fence* m_fLivre11 = nullptr;

    uint64_t m_enviados = 0, m_descartados = 0, m_recebidos = 0;
    std::atomic<uint64_t> m_pronto{ 0 };
    uint64_t m_recebidoUltimo = 0;
};

/* ======================= SONDA DE BANDA (o modo "Auto" da ponte) =======================
   Mede o que este PC faz DE VERDADE, em vez de adivinhar pelo nome do slot (o PnP mente:
   ele reportou "Gen4 x16" numa placa que anda a 1,5 GB/s). Copia um quadro do tamanho
   real pelos DOIS SENTIDOS AO MESMO TEMPO - que e como a ponte trabalha - ja com as FILAS
   MISTAS, aquece 10 copias (a primeira pagina o heap cross-adapter e mente feio) e tira a
   mediana de 20. E o mesmo desenho do microbench (scratchpad/ponte_duplex.cpp), so que
   dentro do app e so em BGRA: o custo e proporcional aos bytes (4K BGRA 22,5 ms / NV12
   8,7 ms = 0,385, contra 0,375 de bytes), entao o NV12 sai por regra de tres. */
struct ResultadoSonda {
    bool   ok = false;
    double msEntrada = 0, msSaida = 0;   // BGRA, os dois sentidos AO MESMO TEMPO
    double gbsAgregado = 0;
    double mb = 0;
    std::wstring erro;
};

class SondaBanda {
    struct Canal {
        ID3D12Device* devS = nullptr; ID3D12Device* devD = nullptr;   // emprestados
        ID3D12CommandQueue *qS = nullptr, *qD = nullptr;
        ID3D12CommandAllocator *aS[3] = {}, *aD[3] = {};
        ID3D12GraphicsCommandList *lS[3] = {}, *lD[3] = {};
        ID3D12Resource *texS = nullptr, *texD = nullptr, *bufS = nullptr, *bufD = nullptr;
        ID3D12Heap *heapS = nullptr, *heapD = nullptr;
        ID3D12Fence *fX = nullptr, *fXD = nullptr, *fD = nullptr;
        HANDLE ev = nullptr;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT peg{}; UINT64 total = 0; double mb = 0;
        UINT64 n = 0;

        bool Criar(ID3D12Device* s, ID3D12Device* d, UINT w, UINT h, D3D12_COMMAND_LIST_TYPE tipo) {
            devS = s; devD = d;
            D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = tipo;
            if (FAILED(s->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&qS))) return false;
            if (FAILED(d->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&qD))) return false;
            for (int i = 0; i < 3; i++) {
                if (FAILED(s->CreateCommandAllocator(tipo, __uuidof(ID3D12CommandAllocator), (void**)&aS[i]))) return false;
                if (FAILED(d->CreateCommandAllocator(tipo, __uuidof(ID3D12CommandAllocator), (void**)&aD[i]))) return false;
                if (FAILED(s->CreateCommandList(0, tipo, aS[i], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&lS[i]))) return false;
                lS[i]->Close();
                if (FAILED(d->CreateCommandList(0, tipo, aD[i], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&lD[i]))) return false;
                lD[i]->Close();
            }
            D3D12_RESOURCE_DESC tex{};
            tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; tex.Width = w; tex.Height = h;
            tex.DepthOrArraySize = 1; tex.MipLevels = 1; tex.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            tex.SampleDesc.Count = 1; tex.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            UINT nl = 0; UINT64 rb = 0;
            s->GetCopyableFootprints(&tex, 0, 1, 0, &peg, &nl, &rb, &total);
            mb = total / 1048576.0;
            D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            if (FAILED(s->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &tex, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&texS))) return false;
            if (FAILED(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &tex, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&texD))) return false;
            D3D12_RESOURCE_DESC bd{};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
            bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
            const D3D12_RESOURCE_ALLOCATION_INFO ai = s->GetResourceAllocationInfo(0, 1, &bd);
            if (ai.SizeInBytes == UINT64(-1)) return false;
            D3D12_HEAP_DESC hd{}; hd.SizeInBytes = (ai.SizeInBytes + 65535) & ~(UINT64)65535;
            hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT; hd.Alignment = 65536;
            hd.Flags = D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
            HRESULT hr = s->CreateHeap(&hd, __uuidof(ID3D12Heap), (void**)&heapS);
            if (FAILED(hr)) { hd.Flags |= D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS; if (FAILED(s->CreateHeap(&hd, __uuidof(ID3D12Heap), (void**)&heapS))) return false; }
            HANDLE hh = nullptr;
            if (FAILED(s->CreateSharedHandle(heapS, nullptr, GENERIC_ALL, nullptr, &hh)) || !hh) return false;
            hr = d->OpenSharedHandle(hh, __uuidof(ID3D12Heap), (void**)&heapD); CloseHandle(hh);
            if (FAILED(hr)) return false;
            if (FAILED(s->CreatePlacedResource(heapS, 0, &bd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&bufS))) return false;
            if (FAILED(d->CreatePlacedResource(heapD, 0, &bd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&bufD))) return false;
            if (FAILED(s->CreateFence(0, D3D12_FENCE_FLAG_SHARED | D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER, __uuidof(ID3D12Fence), (void**)&fX))) return false;
            if (FAILED(s->CreateSharedHandle(fX, nullptr, GENERIC_ALL, nullptr, &hh)) || !hh) return false;
            hr = d->OpenSharedHandle(hh, __uuidof(ID3D12Fence), (void**)&fXD); CloseHandle(hh);
            if (FAILED(hr)) return false;
            if (FAILED(d->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fD))) return false;
            ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            return ev != nullptr;
        }
        UINT64 Quadro() {
            const int i = (int)(n % 3);
            n++;
            aS[i]->Reset(); lS[i]->Reset(aS[i], nullptr);
            { D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
              dst.pResource = bufS; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = peg;
              src.pResource = texS; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
              lS[i]->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr); lS[i]->Close();
              ID3D12CommandList* ls[] = { lS[i] }; qS->ExecuteCommandLists(1, ls); qS->Signal(fX, n); }
            aD[i]->Reset(); lD[i]->Reset(aD[i], nullptr);
            { D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
              dst.pResource = texD; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = 0;
              src.pResource = bufD; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = peg;
              lD[i]->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr); lD[i]->Close();
              ID3D12CommandList* ls[] = { lD[i] }; qD->Wait(fXD, n); qD->ExecuteCommandLists(1, ls); qD->Signal(fD, n); }
            return n;
        }
        void Esperar(UINT64 v) { if (fD && fD->GetCompletedValue() < v) { fD->SetEventOnCompletion(v, ev); WaitForSingleObject(ev, 5000); } }
        void Destruir() {
            if (fD && n) Esperar(n);
            if (ev) { CloseHandle(ev); ev = nullptr; }
            Soltar(fD); Soltar(fXD); Soltar(fX); Soltar(bufD); Soltar(bufS); Soltar(heapD); Soltar(heapS);
            Soltar(texD); Soltar(texS);
            for (int i = 0; i < 3; i++) { Soltar(lD[i]); Soltar(lS[i]); Soltar(aD[i]); Soltar(aS[i]); }
            Soltar(qD); Soltar(qS);
        }
    };
    static double Agora() { LARGE_INTEGER t, f; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / (double)f.QuadPart; }
    static double Mediana(std::vector<double> v) {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    }
    // Um sentido em anel de 3, aquecendo `aquece` quadros e medindo `mede`. Larga junto com
    // o outro sentido (evento de largada) para que a medida seja SIMULTANEA de verdade.
    static void Rodar(Canal& c, int aquece, int mede, HANDLE largada, std::vector<double>* saida) {
        for (int i = 0; i < aquece; i++) c.Esperar(c.Quadro());
        WaitForSingleObject(largada, 10000);
        UINT64 emVoo[3] = {};
        int cheio = 0;
        double t0 = Agora();
        for (int i = 0; i < mede + 2; i++) {
            if (cheio == 3) { c.Esperar(emVoo[0]); emVoo[0] = emVoo[1]; emVoo[1] = emVoo[2]; cheio--; const double t = Agora(); saida->push_back(t - t0); t0 = t; }
            emVoo[cheio++] = c.Quadro();
        }
        while (cheio) { c.Esperar(emVoo[0]); emVoo[0] = emVoo[1]; emVoo[1] = emVoo[2]; cheio--; const double t = Agora(); saida->push_back(t - t0); t0 = t; }
    }

public:
    /* `adJogo` = a placa onde o jogo/monitor esta (a origem da ENTRADA); `adRede` = a da
       rede neural. Mede ENTRADA (jogo -> rede) e SAIDA (rede -> jogo) ao mesmo tempo. */
    static ResultadoSonda Sondar(IDXGIAdapter* adJogo, IDXGIAdapter* adRede, UINT w, UINT h, Filas filas) {
        ResultadoSonda r;
        if (!adJogo || !adRede || !w || !h) { r.erro = L"sonda: parametros vazios"; return r; }
        ID3D12Device *dJ = nullptr, *dR = nullptr;
        if (FAILED(D3D12CreateDevice(adJogo, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dJ)) ||
            FAILED(D3D12CreateDevice(adRede, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dR))) {
            Soltar(dJ); Soltar(dR); r.erro = L"sonda: D3D12CreateDevice falhou"; return r;
        }
        Canal cIn, cOut;
        const bool ok = cIn.Criar(dJ, dR, w, h, TipoFila(filas, true)) &&
                        cOut.Criar(dR, dJ, w, h, TipoFila(filas, false));
        if (ok) {
            HANDLE largada = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            std::vector<double> vIn, vOut;
            std::thread tIn([&] { Rodar(cIn, 10, 20, largada, &vIn); });
            std::thread tOut([&] { Rodar(cOut, 10, 20, largada, &vOut); });
            Sleep(60);            // tempo de as duas chegarem na largada
            SetEvent(largada);
            tIn.join(); tOut.join();
            CloseHandle(largada);
            r.msEntrada = Mediana(vIn); r.msSaida = Mediana(vOut);
            r.mb = cIn.mb;
            if (r.msEntrada > 0 && r.msSaida > 0) {
                r.gbsAgregado = (cIn.mb / 1024.0) / (r.msEntrada / 1000.0) + (cOut.mb / 1024.0) / (r.msSaida / 1000.0);
                r.ok = true;
            } else r.erro = L"sonda: as copias nao completaram";
        } else r.erro = L"sonda: nao consegui montar o heap cross-adapter";
        cIn.Destruir(); cOut.Destruir();
        Soltar(dJ); Soltar(dR);
        return r;
    }

    /* Quanto custa o CODEC (encode + decode) nesta dupla de placas, em ms por quadro, com
       um quadro sintetico do tamanho real. p95 de ~20 - e o p95 que vira judder, nao a
       media. Devolve 0 se nao deu (quem chama usa a constante conservadora). */
    static double SondarCodec(ID3D11Device* devA, ID3D11Device* devB, UINT w, UINT h, int mbps, std::wstring* erro) {
        CodecVideo c;
        if (!c.Iniciar(devA, devB, w, h, 2, mbps, true, erro)) return 0.0;
        std::vector<double> v;
        std::vector<uint8_t> pac;
        for (int i = 0; i < 24; i++) {
            const int slot = i % 2;
            c.EscreverSintetico(slot, i);
            double msE = 0, msD = 0; std::wstring e;
            if (!c.Codificar(slot, (uint64_t)i + 1, i == 0, pac, &msE, &e)) { if (erro) *erro = e; break; }
            if (!c.Decodificar(pac.data(), pac.size(), slot, (uint64_t)i + 1, nullptr, &msD, &e)) { if (erro) *erro = e; break; }
            if (i >= 4) v.push_back(msE + msD);   // os 4 primeiros pagam a montagem
        }
        c.Parar();
        if (v.size() < 8) return 0.0;
        std::sort(v.begin(), v.end());
        return v[(size_t)(v.size() * 95 / 100)];   // p95
    }
};

/* ------------------------------------------------------------ utilidades de adaptador */

/* O LUID da placa dona do monitor: percorre os adaptadores e as saidas de cada um ate
   achar o HMONITOR. Devolve false se nenhuma saida bate (monitor virtual, por exemplo). */
static inline bool PlacaDoMonitor(HMONITOR mon, LUID* luid, std::wstring* nome) {
    if (!mon) return false;
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f)) || !f) return false;
    bool achou = false;
    for (UINT i = 0; !achou; i++) {
        IDXGIAdapter1* a = nullptr;
        if (f->EnumAdapters1(i, &a) != S_OK || !a) break;
        for (UINT j = 0; !achou; j++) {
            IDXGIOutput* o = nullptr;
            if (a->EnumOutputs(j, &o) != S_OK || !o) break;
            DXGI_OUTPUT_DESC od{};
            if (SUCCEEDED(o->GetDesc(&od)) && od.Monitor == mon) {
                DXGI_ADAPTER_DESC1 ad{};
                a->GetDesc1(&ad);
                if (luid) *luid = ad.AdapterLuid;
                if (nome) *nome = ad.Description;
                achou = true;
            }
            o->Release();
        }
        a->Release();
    }
    f->Release();
    return achou;
}

/* O IDXGIAdapter1 de um LUID (com AddRef; quem chama solta). */
static inline IDXGIAdapter1* AdaptadorPorLuid(LUID luid) {
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f)) || !f) return nullptr;
    IDXGIAdapter1* achado = nullptr;
    for (UINT i = 0; !achado; i++) {
        IDXGIAdapter1* a = nullptr;
        if (f->EnumAdapters1(i, &a) != S_OK || !a) break;
        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(a->GetDesc1(&d)) && d.AdapterLuid.LowPart == luid.LowPart && d.AdapterLuid.HighPart == luid.HighPart) achado = a;
        else a->Release();
    }
    f->Release();
    return achado;
}

} // namespace PonteGpu
