/* =====================================================================
   fg_ffx.h -- "Fluidez: Interpolada": um quadro REAL entre dois reais, pelo
   FidelityFX Frame Interpolation (FSR 3.1, SDK 1.1.4, amd_fidelityfx_dx12.dll
   que o feeder ja embarca), alimentado com o campo de movimento do NOSSO
   estimador (motion_estimate.h, D3D12 Video Motion Estimation).

   EXPERIMENTAL, ATRAS DE CHAVE: so entra com [ultima] fluidez_modo=interpolada
   E DLSS5_FG_EXPERIMENTAL=1 (ver FluidezModo em main.cpp). Com a chave fora,
   nada daqui e sequer instanciado - o caminho de sempre fica intacto.

   ONDE RODA. A cadeia (ReShade + rede) apresenta num swapchain OCULTO e o
   quadro FINAL publicado pelo add-on cruza a ponte (ponte_gpu.h) ate o
   APRESENTADOR (apresentador_gpu.h), que mostra a janela visivel. E a mesma
   arquitetura das duas placas, aqui na MESMA placa: e o unico jeito de
   ATRASAR o quadro B - o Present da cadeia e o que produz B, entao B so pode
   ser segurado se a cadeia nao apresenta na janela visivel. Consequencia
   importante: nenhum Present e suprimido para mostrar G, e G nunca passa
   pela cadeia - a "generation"/o quadro anterior do feed nao veem G por
   construcao (ver o relatorio de 12/09/2026).

   O QUE ESTE ARQUIVO FAZ, por quadro B (n) que chega ao apresentador:
     lado da REDE (thread do laco, PublicarCampo): copia o campo do VME para
       um anel compartilhado e sinaliza a fence do campo com n;
     lado do APRESENTADOR (Gerar):
       1. copia B para inB[n%2] (compartilhada com o D3D12) e A = inB[(n-1)%2];
       2. PASSO A (compute, por bloco da grade do VME): gates por bloco,
          amostrando em meia-res -
            (a) variancia do bloco em B abaixo do limiar     => MV=0 (HUD chapado, ceu)
            (b) ambiguidade: SAD no vetor vs SEGUNDO minimo a >2 px AO LONGO
                da direcao do vetor; razao ~1 => periodico     => MV=0 (xadrez, grades)
            (c) mascara de HUD aprendida: bloco com SAD-sem-deslocar ~0 por
                >= 8 quadros enquanto a cena se move             => MV=0; saida
                instantanea (um par que se mexe zera o contador)
          e estatisticas por atomicos (corte de cena, fracao movente...);
       3. PASSO A2: dilatacao para tras SO de vetores > 60 px (fora do alcance);
       4. PASSO B (por pixel): campo em pixels, sinal backward (o do VME ja e
          o do FI), nearest bloco -> pixel, para a textura R16G16_FLOAT que o
          FI le; com margem de 1 bloco em volta da mascara quando a cena e um pan;
       5. D3D12: espera a fence de entrada, PREPARE + FRAMEGENERATION manual
          (depth constante 1,0, jitter 0, frameID +1 estrito), saida em outG[n%2]
          - textura que NASCE no D3D11 (UAV|SRV, NTHANDLE) e e aberta no D3D12
          (o inverso devolve E_INVALIDARG - provado em 12/09/2026);
       6. D3D11 espera a fence de saida NA GPU; o apresentador copia G para o
          backbuffer. Corte de cena (mais de metade dos blocos mudou de media)
          => o chamador mostra B em vez de G, sem reset.
       7. MASCARA DE HUD DEPOIS DO FI (X-HUD-PISCA, 19/09/2026): pixel que nao
          mudou entre A e B ha N quadros e copiado de B para G (mira, minimapa,
          texto ficam firmes; a cena em volta segue interpolada). Ver CriarHud.
       0. (antes de tudo, X-FG-QUALIDADE, 19/09/2026) QUADRO REPETIDO: B igual a A
          por conteudo (a captura do monitor entrega o mesmo quadro do jogo mais de
          uma vez) nao entra no FI - nem no historico dele, nem no ritmo. Ver CriarDedup.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "ponte_gpu.h"          // Soltar, MedidorGpu
#include "motion_estimate.h"    // o VME LOCAL (duas placas): roda no device do apresentador

#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_api_types.h"
#include "ffx_api/ffx_framegeneration.h"
#include "ffx_api/dx12/ffx_api_dx12.h"

namespace FrameGen {

using PonteGpu::Soltar;
typedef void (*LogFn)(const char*, ...);

/* ===== FLUIDEZ1 (22/09/2026): O INTERPOLADOR PODE CORRER EM COMPUTE ASSINCRONO =====

   `ffxConfigureDescFrameGeneration::allowAsyncWorkloads` (ffx_framegeneration.h:112) existe
   desde sempre e ficava `false` por omissao nos CINCO pontos onde montamos o `cf` - nunca foi
   uma escolha: e o zero do `{}`. A propria AMD oferece esse campo para o caso exato do nosso
   gargalo, e o nosso gargalo esta medido: com a rede neural ligada, a fila GRAFICA e quem
   esta saturada (STUTTER-TDR-2209 §2.6: o quadro custa ~1 ms a mais que os 16,67 ms do
   orcamento, e o p99 fica preso em 21 ms = 5 refreshes de 240 Hz). O trabalho do interpolador
   nao depende do resultado da rede no mesmo quadro: mandado para a fila de COMPUTE, ele corre
   ao lado em vez de esperar a vez na fila que ja esta cheia.

   POR QUE UMA CHAVE, E NAO UMA LINHA CRAVADA: async compute com recursos compartilhados entre
   o D3D11 do apresentador e o D3D12 do FI pode expor corrida nas fences (FRAMEGEN-ABERTO-2209,
   passo 1, item "Risco"). Se aparecer 0x887A0006 ou artefato no G, `[ultima] fg_async=0`
   devolve o comportamento de antes SEM recompilar nada - que e a regra do plano de execucao
   (comparacoes por chave no ini, nunca por recompilar).

   Quem le a chave e o app (main.cpp, no arranque, antes de o apresentador existir); aqui fica
   so o valor em uso, porque o `cf` e montado em cinco lugares e um deles roda depois de uma
   pausa por VRAM, longe de qualquer ini. */
static bool g_async = true;

struct Resultado {
    ID3D11Texture2D* G = nullptr;      // o quadro gerado (do device do apresentador), espera ja enfileirada
    ID3D11Texture2D* B = nullptr;      // a copia propria de B (vale ate o proximo Gerar+1)
    /* X-FG-6 (3x/4x): os quadros gerados EM ORDEM DE TEMPO entre A e B. 2x: {G}; 4x: {G1/4, G1/2,
       G3/4}; 3x: {G1/4, G3/4} mostrados em 1/3 e 2/3 (ver Multiplicador). `G` continua sendo o
       de sempre (G1/2) para quem nao olha `Gs`. */
    ID3D11Texture2D* Gs[7] = {};   /* META: ate 7 (8x = {1/8 .. 7/8}, 3o contexto) */
    int nG = 0;
    /* O1-TELA (20/09/2026): a FRACAO REAL de cada `Gs`, entre A (0) e B (1). Ate aqui o
       apresentador assumia que os gerados estavam repartidos por igual (K tiques) - o que e
       verdade em 2x e 4x, e uma aproximacao anotada em 3x (G1/4 e G3/4 mostrados em 1/3 e 2/3).
       O modo Tela precisa do numero de verdade, porque ele nao reparte por igual: para cada
       vblank ele calcula a fracao que QUERIA e escolhe o gerado mais proximo dela. E so
       frações diádicas existem - o provedor FFX so escreve no ponto medio (X-FG-VIDEO §3) e os
       outros nascem de chamadas encadeadas: 1/4 = FI(A, G1/2), 3/4 = FI(G1/2, B).
       META (21/09/2026): e 1/8, 3/8, 5/8, 7/8 de um 3o contexto que refina os quatro pares
       (A,G1/4), (G1/4,G1/2), (G1/2,G3/4), (G3/4,B) - 7 despachos por intervalo em 8x. */
    float fracs[7] = {};
    bool corte = false;                // corte de cena: nao mostre G, mostre B
    /* X-FG-QUALIDADE (19/09/2026): B e igual ao quadro real anterior (a captura entregou o mesmo
       conteudo de novo). Nada foi despachado, nenhum G existe e o par continua sendo (A, proximo).
       O chamador NAO apresenta nada - a tela ja mostra esta imagem. */
    bool repetido = false;
    bool doJogo = false;               // J5: este G usou profundidade e MV reais do jogo (pela ponte)
    bool pan = false, mv0 = false;     // diagnostico
    uint32_t nMov = 0, nKept = 0, nStatic = 0, nMask = 0, nAmbig = 0, nFlat = 0, nDiffBig = 0, nBlocos = 0;
};

class Interpolador {
public:
    /* ===== X-GERADO (20/09/2026): TRES SLOTS, e nao dois (o K-DEDUP §4 ja pedia) =====
       O FI alterna os slots inB/inBFi/outG por quadro. Com DOIS, o par do quadro n e reescrito
       pelo Gerar de n+2, entao um pendente NAO pode sobreviver a uma chegada: o apresentador
       tinha de solta-lo na hora em que o quadro novo entrava, a folga na chegada era zero por
       construcao e o tremor da captura (j = 12,5 ms; `chegada(carimbo) p05 29,2 p95 45,8`) ia
       inteiro para a tela - `G fora de hora` de 0 a 45%%. Com TRES, o par de n so e reescrito por
       n+3: a fila pode segurar o pendente por uma chegada e absorver o `j`, que e exatamente o
       `ceil(D / (T/2))` = 1 par a mais do alvo de fase novo (D = c + j).
       CUSTO: um inBFi + um outG por slot (mais um outG1/4 e um outG3/4 em 3x/4x), em resolucao
       cheia, e um inB quando a entrada e reduzida. O numero sai no log ao ligar e passa pela
       guarda de VRAM de sempre (OrcamentoVram/SoltarContextoVram). */
    static constexpr int kSlots = 3;
    /* CORTE DE CENA SEM CAMPO - os tres numeros, e de onde vieram (13/09/2026).
       Estatistica: fracao dos blocos da grade cuja MEDIA DE LUMA muda mais que kCorteNivel.
       Medida na sequencia de corte sintetico do engenheiro de qualidade (31 quadros de pan +
       10 de combate, D:\claude-scratch\qualidade\corte) e nos 87 passos de C:\tripl:
         passo do corte ............ 0,119     |  maior passo de pan .......... 0,014
         menu em movimento pesado .. ate 0,478 |  maior passo de combate ...... 0,015
       So a MAGNITUDE nao separa corte de cena muito movimentada (o menu passa do corte). O que
       separa e o SALTO: o corte e uma descontinuidade (0,0075 -> 0,119 -> 0,008, 16x a media
       movel), o menu e uma rampa sustentada (nunca passa de 7x). Dai o criterio duplo. Com
       kCorteSalto=8 e kCorteFracMin=0,06: acerta o corte, 0 falsos em pan e combate, 1 falso em
       menu (passo 15) - e esse passo e justamente um em que o G PERDE do quadro repetido
       (24,13 vs 24,29 dB), ou seja, o "falso" ainda ganha 0,16 dB. */
    static constexpr float  kCorteNivel   = 8.0f;    // niveis de luma (0-255) de mudanca da media do bloco
    static constexpr double kCorteSalto   = 8.0;     // quantas vezes acima da media movel = corte
    static constexpr double kCorteFracMin = 0.06;    // e ainda assim tem de mexer 6% dos blocos
    static constexpr double kCortePiso    = 0.004;   // piso da media movel (cena parada nao vira gatilho)
    /* Segunda regra, em paralelo: o corte OBVIO. Quando quase toda a tela muda de media de luma,
       nao ha cena movimentada que explique - nos 87 passos reais de C:\tripl a fracao NUNCA passa
       de 0,478 (menu, 57% da tela em movimento), enquanto os cortes da bancada dao 0,78 a 0,99.
       Com isso o salto exigido pode ser pequeno. Medido em 13/09/2026 (ver o relatorio). */
    static constexpr double kCorteFracAlta  = 0.70;
    static constexpr double kCorteSaltoAlta = 2.5;
    /* OS DOIS SALTOS SAO FUNCAO DO INTERVALO ENTRE QUADROS, NAO CONSTANTES (13/09/2026, medido em
       D:\claude-scratch\corte3\). Motivo, com numero: a media movel (`base`) e a fracao de blocos
       que muda entre DOIS QUADROS QUE CHEGAM - e ela cresce com o espacamento, enquanto a fracao
       de um CORTE nao (o corte e a mesma descontinuidade, chegue rapido ou devagar). Medida da
       base mediana em pan/combate: 0,0040 a 60 fps -> 0,0446 a 7,5 fps (11x). Com salto FIXO em 8
       o limiar efetivo (8*base) sobe junto: 0,06 a 60 fps -> 0,36 a 7,5. Resultado medido:
       um corte que mexe 15% dos blocos (o corte real medido mexe 11,9%) e pego em 100% dos
       estados a 60/30/20 fps e em 0% a 15 fps ou menos.
       A correcao mantem a MARGEM, nao o multiplicador: a maior razao vista SEM corte tambem cai
       com o espacamento (11,3x a 60 fps -> 3,5x a 7,5), entao ha folga para baixar o salto.
       Calibracao (D:\claude-scratch\corte3\sweep.txt, 1026 passos por taxa nas 3 cenas):
         chegada 30 fps -> salto 8,0 (8 falsos, os mesmos de hoje)  20 -> 7,0 (0 falsos)
                 15 -> 6,0 (0)   12 -> 5,0 (0)   10 -> 4,0 (0)   7,5 -> 4,0 (0)
       Revalidado (tab_novo.txt / curvaf_novo.txt): ZERO falsos a mais em qualquer taxa e a
       deteccao de um corte de 15-20% dos blocos volta a 100% ate 10 fps de chegada.
       O piso de saltoAlta (1,5) deixa a regra do corte OBVIO valer como "frac > 0,70" em qualquer
       taxa: a maior fracao vista SEM corte e 0,578 (menu a 7,5 fps), entao a folga fica 1,21x. */
    static constexpr double kCorteSaltoMin     = 4.0;    // piso do salto (chegada <= 10 fps)
    static constexpr double kCorteSaltoAltaMin = 2.0;    // piso do salto da regra do corte obvio
    static constexpr double kCorteDtRef        = 33.3;   // ms: acima desta chegada nada muda
    static constexpr double kCorteSaltoPorMs   = 0.060;  // quanto o salto cai por ms de intervalo
    static constexpr double kCorteSaltoAltaPorMs = 0.015;
    /* 1) no ARRANQUE (quando o modo esta ligado): a DLL, o device D3D12 no adaptador da rede
       e o contexto de FG no tamanho do backbuffer. E o passo lento (0,5-0,8 s na 1a vez). */
    /* O2-QUALIDADE (20/09/2026): `internaAltura` era um bool "cai para 1440p". Virou a ALTURA
       alvo do FI (0 = nativa, 1440, 1080, 720) - e o que sustenta a opcao Qualidade/Desempenho
       da interface. A saida continua na resolucao da tela: quem amplia o G e o apresentador.
       O custo do FI cai com a area, entao 1080p custa ~2x menos que 1440p e ~4x menos que 4K;
       o preco e borrao no gerado (medido em 13/09: o ida-e-volta 4K->1440->4K tira 2,8 dB). */
    bool CriarContexto(IDXGIAdapter1* ad, UINT w, UINT h, DXGI_FORMAT fmt, const wchar_t* dllPath, LogFn log, std::wstring* erro, UINT internaAltura = 0) {
        m_log = log; m_fw = w; m_fh = h; m_fmt = fmt;
        if (!ad || !w || !h) { if (erro) *erro = L"fg: parametros vazios"; return false; }
        // RESOLUCAO INTERNA (duas placas): o FI a 4K custa ~8 ms na placa do JOGO - derruba um jogo de
        // 60 para ~40 e some com o ganho. Acima da altura alvo o G e gerado nela (mesma proporcao) e
        // o apresentador amplia ao mostrar; A/B continuam inteiros para o Present.
        m_w = w; m_h = h;
        if (internaAltura && h > internaAltura) { m_h = internaAltura & ~1u; m_w = (UINT)((UINT64)w * m_h / h) & ~1u; }
        m_reduz = (m_w != m_fw || m_h != m_fh);
        w = m_w; h = m_h;
        m_dll = LoadLibraryW(dllPath);
        if (!m_dll) { if (erro) *erro = std::wstring(L"fg: nao carreguei ") + dllPath; return false; }
        m_Create  = (PfnFfxCreateContext)(void*)GetProcAddress(m_dll, "ffxCreateContext");
        m_Destroy = (PfnFfxDestroyContext)(void*)GetProcAddress(m_dll, "ffxDestroyContext");
        m_Dispatch= (PfnFfxDispatch)(void*)GetProcAddress(m_dll, "ffxDispatch");
        m_Query   = (PfnFfxQuery)(void*)GetProcAddress(m_dll, "ffxQuery");
        m_Config  = (PfnFfxConfigure)(void*)GetProcAddress(m_dll, "ffxConfigure");
        if (!m_Create || !m_Destroy || !m_Dispatch || !m_Query || !m_Config) { if (erro) *erro = L"fg: a DLL nao tem os exports ffx-api"; return false; }
        HRESULT hr = D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&m_dev12);
        if (FAILED(hr) || !m_dev12) { Hr(erro, L"D3D12CreateDevice", hr); return false; }
        // Fila de prioridade ALTA: o FI divide a placa com a rede (30-40 ms por quadro em 4K) e,
        // em prioridade normal, so terminava depois dela - o G saia um quadro atrasado.
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
        hr = m_dev12->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&m_fila);
        if (FAILED(hr)) { qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL; hr = m_dev12->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&m_fila); }
        if (FAILED(hr)) { Hr(erro, L"CreateCommandQueue", hr); return false; }
        for (int i = 0; i < kSlots; i++) {
            hr = m_dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&m_alloc[i]);
            if (FAILED(hr)) { Hr(erro, L"CreateCommandAllocator", hr); return false; }
        }
        hr = m_dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&m_lista);
        if (FAILED(hr)) { Hr(erro, L"CreateCommandList", hr); return false; }
        m_lista->Close();
        hr = m_dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&m_fIn);
        if (SUCCEEDED(hr)) hr = m_dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&m_fOut);
        if (FAILED(hr)) { Hr(erro, L"CreateFence", hr); return false; }
        m_evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_fila->GetTimestampFrequency(&m_tsFreq);
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count = (UINT)kSlots * 2;   // X-GERADO: duas consultas por slot (antes era 4 = dois slots)
        m_dev12->CreateQueryHeap(&qh, __uuidof(ID3D12QueryHeap), (void**)&m_qh);
        CriarBuffer12((UINT64)kSlots * 16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, &m_qbuf);

        // depth constante 1,0 (o FI exige uma textura de profundidade; nao temos nenhuma)
        {
            D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1;
            d.Format = DXGI_FORMAT_R32_FLOAT; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            hr = m_dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&m_depth);
            if (FAILED(hr)) { Hr(erro, L"depth", hr); return false; }
            const UINT pitch = (w * 4 + 255) & ~255u;
            ID3D12Resource* up = nullptr;
            if (!CriarBuffer12((UINT64)pitch * h, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, &up)) { if (erro) *erro = L"fg: upload da profundidade"; return false; }
            uint8_t* m = nullptr; up->Map(0, nullptr, (void**)&m);
            std::vector<float> linha(w, 1.0f);
            for (UINT y = 0; y < h; y++) memcpy(m + (size_t)y * pitch, linha.data(), (size_t)w * 4);
            up->Unmap(0, nullptr);
            m_alloc[0]->Reset(); m_lista->Reset(m_alloc[0], nullptr);
            D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
            dst.pResource = m_depth; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.pResource = up; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT; src.PlacedFootprint.Footprint.Width = w; src.PlacedFootprint.Footprint.Height = h;
            src.PlacedFootprint.Footprint.Depth = 1; src.PlacedFootprint.Footprint.RowPitch = pitch;
            D3D12_RESOURCE_BARRIER b1 = Bar(m_depth, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST); m_lista->ResourceBarrier(1, &b1);
            m_lista->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            D3D12_RESOURCE_BARRIER b2 = Bar(m_depth, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); m_lista->ResourceBarrier(1, &b2);
            m_lista->Close();
            ID3D12CommandList* ls[] = { m_lista }; m_fila->ExecuteCommandLists(1, ls);
            EsperarFila(); up->Release();
        }

        ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = m_dev12;
        ffxCreateContextDescFrameGeneration cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; cd.header.pNext = &be.header;
        cd.flags = 0; cd.displaySize = { w, h }; cd.maxRenderSize = { w, h }; cd.backBufferFormat = ffxApiGetSurfaceFormatDX12(fmt);
        const double t0 = Ms();
        ffxReturnCode_t rc = m_Create(&m_fg, &cd.header, nullptr);
        if (rc != FFX_API_RETURN_OK || !m_fg) { if (erro) { wchar_t b[96]; _snwprintf(b, 96, L"fg: ffxCreateContext(FRAMEGENERATION) devolveu %u", (unsigned)rc); b[95] = 0; *erro = b; } m_fg = nullptr; return false; }
        ffxConfigureDescFrameGeneration cf{}; cf.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION; cf.frameGenerationEnabled = true; cf.allowAsyncWorkloads = g_async;
        cf.generationRect = { 0, 0, (int32_t)w, (int32_t)h }; cf.frameID = 0; m_Config(&m_fg, &cf.header);
        ffxQueryDescFrameGenerationGetGPUMemoryUsage mu{}; FfxApiEffectMemoryUsage use{};
        mu.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE; mu.gpuMemoryUsageFrameGeneration = &use;
        m_Query(&m_fg, &mu.header);
        m_memMB = use.totalUsageInBytes / 1048576.0;
        ffxQueryGetProviderVersion pv{}; pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION; m_Query(&m_fg, &pv.header);
        if (m_log) m_log("fg: contexto FidelityFX FG %ux%u criado em %.0f ms (provedor '%s', %.0f MB de VRAM)%s", w, h, Ms() - t0, pv.versionName ? pv.versionName : "?", m_memMB, m_reduz ? " - resolucao INTERNA reduzida (o quadro e maior; o apresentador amplia o G)" : "");
        m_contexto = true;
        return true;
    }

    /* 2) quando o APRESENTADOR existe: os recursos D3D11 no device dele, compartilhados com o
       D3D12, os shaders dos passes e as fences abertas no D3D11. */
    bool LigarApresentador(ID3D11Device* dev11, std::wstring* erro) {
        if (!m_contexto) { if (erro) *erro = L"fg: sem contexto"; return false; }
        if (FAILED(dev11->QueryInterface(__uuidof(ID3D11Device5), (void**)&m_dev)) || !m_dev) { if (erro) *erro = L"fg: device do apresentador sem ID3D11Device5"; return false; }
        for (int i = 0; i < kSlots; i++) {
            // inBFi: o que o FI le (resolucao interna, compartilhada com o D3D12). inB: o quadro inteiro
            // para o Present (so existe a parte quando ha reducao; senao e a mesma textura).
            if (!CriarTex11(m_w, m_h, m_fmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, true, &m_inBFi[i], &m_inBFi12[i], erro, L"inBFi")) return false;
            if (FAILED(m_dev->CreateShaderResourceView(m_inBFi[i], nullptr, &m_inBFiSrv[i]))) { if (erro) *erro = L"fg: SRV de inBFi"; return false; }
            if (m_reduz) {
                if (!CriarTex11(m_fw, m_fh, m_fmt, D3D11_BIND_SHADER_RESOURCE, false, &m_inB[i], nullptr, erro, L"inB")) return false;
                if (FAILED(m_dev->CreateShaderResourceView(m_inB[i], nullptr, &m_inBSrv[i]))) { if (erro) *erro = L"fg: SRV de inB"; return false; }
            } else { m_inB[i] = m_inBFi[i]; m_inB[i]->AddRef(); }
            if (!CriarTex11(m_w, m_h, m_fmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, true, &m_outG[i], &m_outG12[i], erro, L"outG")) return false;
            // X-GERADO: o G tambem e LIDO (metrica de prova: o gerado difere dos DOIS reais?)
            if (FAILED(m_dev->CreateShaderResourceView(m_outG[i], nullptr, &m_outGSrv[i]))) { if (erro) *erro = L"fg: SRV do G (metrica de prova)"; return false; }
        }
        /* X-GERADO: o custo do slot a mais, em resolucao cheia, dito no log. Passa pela guarda de
           VRAM de sempre (OrcamentoVram / SoltarContextoVram): o contexto inteiro do FI e solto
           quando a placa aperta, e o terceiro slot vai junto. */
        if (m_log) {
            const double bpp = (m_fmt == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8.0 : 4.0;
            const double porSlot = (double)m_w * (double)m_h * bpp / (1024.0 * 1024.0) * (m_reduz ? 3.0 : 2.0);
            m_log("fg: %d slots no interpolador (era 2) - o par de n so e reescrito por n+%d, entao a fila do apresentador pode segurar o pendente por uma chegada e absorver o tremor da captura. Custo do slot a mais: %.0f MB em %ux%u (%.0f MB com 3x/4x); sai junto na pausa por VRAM",
                  kSlots, kSlots, porSlot, m_w, m_h, porSlot + 2.0 * (double)m_w * (double)m_h * bpp / (1024.0 * 1024.0));
        }
        if (m_reduz) {
            if (!CompilarCS(HlslReduz(), "PassoDown", &m_csDown, erro)) return false;
            D3D11_SAMPLER_DESC smp{}; smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; smp.MaxLOD = D3D11_FLOAT32_MAX;
            if (FAILED(m_dev->CreateSamplerState(&smp, &m_samp))) { if (erro) *erro = L"fg: sampler da reducao"; return false; }
            D3D11_BUFFER_DESC bd{}; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.ByteWidth = 16;
            const UINT c[4] = { m_w, m_h, 0, 0 }; D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = c;
            if (FAILED(m_dev->CreateBuffer(&bd, &sd, &m_cbDown))) { if (erro) *erro = L"fg: cb da reducao"; return false; }
            if (m_log) m_log("fg: reducao %ux%u -> %ux%u antes do FI (bilinear)", m_fw, m_fh, m_w, m_h);
        }
        if (!CriarTex11(m_w, m_h, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, true, &m_mv, &m_mv12, erro, L"mv")) return false;
        if (FAILED(m_dev->CreateUnorderedAccessView(m_mv, nullptr, &m_mvUav))) { if (erro) *erro = L"fg: UAV do campo por pixel"; return false; }
        // fences: In (D3D11 sinaliza, D3D12 espera) e Out (D3D12 sinaliza, D3D11 espera)
        if (!AbrirFence(m_fIn, &m_fIn11, erro) || !AbrirFence(m_fOut, &m_fOut11, erro)) return false;
        m_medPasses.Iniciar(m_dev);
        {   // X-GERADO: bancada - volta o gate de ambiguidade de ate 20/09 (ver PassoA)
            wchar_t e[8]{};
            if (GetEnvironmentVariableW(L"DLSS5_FG_AMBIG_ANTIGO", e, 8) > 0 && e[0] == L'1') {
                m_ambigAntigo = true;
                if (m_log) m_log("fg: BANCADA - DLSS5_FG_AMBIG_ANTIGO=1: o gate de ambiguidade volta a zerar o vetor por QUALQUER minimo ao longo dele (o de ate 20/09, que fazia o gerado repetir o real)");
            }
            /* FG-QUALIDADE2: o campo do VME JA vai ao FI como `motionVectors` (ver o Gerar: o
               texMv do caminho sem jogo e o m_mv12, que o PassoB escreve). Isto aqui e o
               interruptor para MEDIR o que ele vale: com DLSS5_FG_MV_FI=0 o PassoB escreve zero
               e o FI fica so com o fluxo optico dele, TUDO o mais igual - os gates, a mascara de
               interface e a desoclusao continuam rodando sobre o mesmo campo. Sem isto, "o campo
               ajuda" e opiniao. */
            if (GetEnvironmentVariableW(L"DLSS5_FG_MV_FI", e, 8) > 0 && e[0] == L'0') {
                m_mvParaFi = false;
                if (m_log) m_log("fg: BANCADA - DLSS5_FG_MV_FI=0: o campo do VME NAO vai ao interpolador (MV=0, so o fluxo optico do SDK); os passes e as mascaras seguem iguais");
            }
        }
        if (!CriarCorte(erro)) return false;   // detector de corte que NAO depende do campo
        if (!CriarHud(erro)) return false;     // mascara de HUD depois do FI (X-HUD-PISCA)
        if (!CriarDedup(erro)) return false;   // quadro repetido nao entra no FI (X-FG-QUALIDADE)
        // as texturas da grade so nascem quando o campo for ligado (tamanho da grade)
        return true;
    }

    /* 3) o CAMPO do VME: anel de 3 no device da REDE (compartilhado por NTHANDLE) e uma fence
       D3D11 compartilhada; aberto no device do apresentador. `srcW/srcH` = tamanho da captura
       (o campo e delta UV relativo a ela), `cov` = fracao coberta pela grade. */
    bool LigarCampo(ID3D11Device* devRede, UINT gridW, UINT gridH, float covU, float covV, std::wstring* erro) {
        if (!m_dev) { if (erro) *erro = L"fg: apresentador ainda nao ligado"; return false; }
        m_campoTentou = true;
        ID3D11Device5* dr = nullptr;
        if (FAILED(devRede->QueryInterface(__uuidof(ID3D11Device5), (void**)&dr)) || !dr) { if (erro) *erro = L"fg: device da rede sem ID3D11Device5"; return false; }
        m_devRede = dr;
        m_gw = gridW; m_gh = gridH; m_covU = covU; m_covV = covV;
        ID3D11Device1* d1 = nullptr; m_dev->QueryInterface(__uuidof(ID3D11Device1), (void**)&d1);
        for (int i = 0; i < 3; i++) {
            D3D11_TEXTURE2D_DESC td{}; td.Width = gridW; td.Height = gridH; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
            HRESULT hr = dr->CreateTexture2D(&td, nullptr, &m_campoRede[i]);
            if (FAILED(hr)) { Soltar(d1); Hr(erro, L"campo (rede)", hr); return false; }
            IDXGIResource1* r1 = nullptr; HANDLE h = nullptr;
            m_campoRede[i]->QueryInterface(__uuidof(IDXGIResource1), (void**)&r1);
            hr = r1 ? r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &h) : E_FAIL; Soltar(r1);
            if (FAILED(hr) || !h) { Soltar(d1); Hr(erro, L"campo CreateSharedHandle", hr); return false; }
            hr = d1 ? d1->OpenSharedResource1(h, __uuidof(ID3D11Texture2D), (void**)&m_campoApres[i]) : E_FAIL; CloseHandle(h);
            if (FAILED(hr)) { Soltar(d1); Hr(erro, L"campo OpenSharedResource1", hr); return false; }
            if (FAILED(m_dev->CreateShaderResourceView(m_campoApres[i], nullptr, &m_campoSrv[i]))) { Soltar(d1); if (erro) *erro = L"fg: SRV do campo"; return false; }
        }
        Soltar(d1);
        HRESULT hr = dr->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void**)&m_fCampoRede);
        if (FAILED(hr)) { Hr(erro, L"campo CreateFence", hr); return false; }
        HANDLE h = nullptr; hr = m_fCampoRede->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &h);
        if (FAILED(hr) || !h) { Hr(erro, L"campo fence CreateSharedHandle", hr); return false; }
        hr = m_dev->OpenSharedFence(h, __uuidof(ID3D11Fence), (void**)&m_fCampoApres); CloseHandle(h);
        if (FAILED(hr)) { Hr(erro, L"campo OpenSharedFence", hr); return false; }
        if (!CriarGrade(gridW, gridH, covU, covV, erro)) return false;
        m_campoOk = true;
        if (m_log) m_log("fg: campo do VME ligado (REMOTO, publicado pela rede) - grade %ux%u (cobertura %.3f x %.3f), bloco %.1fx%.1f px de saida", gridW, gridH, covU, covV, (float)m_w * covU / gridW, (float)m_h * covV / gridH);
        return true;
    }

    /* 3b) o CAMPO LOCAL (duas placas): o VME roda NO DEVICE DO APRESENTADOR sobre os proprios
       quadros finais A/B - a entrada certa para interpolar quadros finais, e nada cruza a
       PCIe. Provado na RX 9060 XT em 12/09/2026 (harness: grade 160x90, 43 dB na faixa). */
    bool LigarCampoLocal(UINT divisor, UINT bloco, std::wstring* erro) {
        if (!m_dev) { if (erro) *erro = L"fg: apresentador ainda nao ligado"; return false; }
        m_campoTentou = true;
        if (!m_me.Init(m_dev, m_w, m_h, divisor, bloco)) {
            if (erro) { const char* e = m_me.LastError() ? m_me.LastError() : "motivo desconhecido"; *erro = L"fg: VME local nao subiu: " + std::wstring(e, e + strlen(e)); }
            return false;
        }
        m_gw = m_me.MvWidth(); m_gh = m_me.MvHeight(); m_covU = m_me.CoverageU(); m_covV = m_me.CoverageV();
        if (!CriarGrade(m_gw, m_gh, m_covU, m_covV, erro)) return false;
        m_campoLocal = true; m_campoOk = true;
        if (m_log) m_log("fg: campo do VME ligado (LOCAL, no device do apresentador) - grade %ux%u, bloco %u, divisor %u (cobertura %.3f x %.3f)", m_gw, m_gh, bloco, divisor, m_covU, m_covV);
        return true;
    }
    bool CampoLocal() const { return m_campoLocal; }
    /* Duas placas sem VME: fica em MV=0 (so Optical Flow) e nao tenta mais. */
    void DesistirDoCampo() { m_campoTentou = true; }
    double VmeMs() const { return m_medVme.Media(); }

private:
    /* Recursos do detector de corte SEM campo: uma grade de blocos de ~24 px (160x90 em 4K,
       107x60 em 1440p - a mesma ordem da grade do VME), um buffer de 2 contadores e dois
       stagings. Nascem SEMPRE (custam alguns KB), porque em MV=0 a CriarGrade nunca roda. */
    bool CriarCorte(std::wstring* erro) {
        {   // kill-switch / ajuste de bancada: DLSS5_FG_CORTE=0 desliga o detector
            wchar_t e[8]{};
            if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE", e, 8) > 0 && e[0] == L'0') {
                m_corteLigado = false;
                if (m_log) m_log("fg: BANCADA - DLSS5_FG_CORTE=0: o detector de corte sem campo fica DESLIGADO");
                return true;
            }
        }
        /* BARATEAR (item 3m): a grade (px por bloco) e quantas amostras por lado o bloco usa
           ficam ajustaveis para a bancada - o custo do passo cai com as duas, mas o acerto
           depende delas. Padrao = o que foi validado em 13/09 (24 px, 4x4). */
        {
            wchar_t e[16]{};
            if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_GRADE", e, 16) > 0 && e[0]) {
                const long v = wcstol(e, nullptr, 10);
                if (v >= 8 && v <= 128) m_corteGrade = (UINT)v;
            }
            if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_AMOSTRA", e, 16) > 0 && e[0]) {
                const long v = wcstol(e, nullptr, 10);
                if (v == 1 || v == 2 || v == 4 || v == 8) m_corteAmostra = (UINT)v;
            }
            if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_TELEM", e, 16) > 0 && e[0] == L'0') m_corteTelem = false;
            if (m_log && (m_corteGrade != 24 || m_corteAmostra != 4 || !m_corteTelem))
                m_log("fg: BANCADA - detector de corte com grade=%u px, amostra=%ux%u, telemetria=%s (padrao 24 px, 4x4, ligada)",
                      m_corteGrade, m_corteAmostra, m_corteAmostra, m_corteTelem ? "ligada" : "DESLIGADA");
        }
        m_cgw = (m_w + m_corteGrade - 1) / m_corteGrade; m_cgh = (m_h + m_corteGrade - 1) / m_corteGrade;
        if (!m_cgw || !m_cgh) { m_corteLigado = false; return true; }
        // O passo de aplicacao escreve num UAV tipado do formato do backbuffer (BGRA8). Nem todo
        // driver/formato aceita ESCRITA tipada em UAV; sem isso o caminho todo nao se sustenta
        // (a alternativa seria decidir na CPU, que custa 68 ms de Map - medido). Confere antes.
        {
            D3D11_FEATURE_DATA_FORMAT_SUPPORT2 f2{}; f2.InFormat = m_fmt;
            const HRESULT hf = m_dev->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &f2, sizeof(f2));
            if (FAILED(hf) || !(f2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE)) {
                m_corteLigado = false;
                if (m_log) m_log("fg: o detector de corte sem campo NAO subiu - esta placa nao aceita escrita tipada em UAV no formato %u; o FI segue como antes (sem deteccao de corte em MV=0)", (unsigned)m_fmt);
                return true;
            }
        }
        // est: [0]=media movel  [4]=quadros  [8]=cortes acumulados  [12]=ultima fracao
        //      [16]=fracao NO corte  [20]=media movel NO corte   (os dois ultimos so para o log)
        const UINT est0[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        const UINT arg0[8] = { 0, 0, 1, 0, 0, 0, 0, 0 };
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = 8; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 32; bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        D3D11_SUBRESOURCE_DATA sdE{}; sdE.pSysMem = est0;
        if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_corteStats)) ||
            FAILED(m_dev->CreateBuffer(&bd, &sdE, &m_corteEst))) { if (erro) *erro = L"fg: buffers do detector de corte"; return false; }
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS | D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        D3D11_SUBRESOURCE_DATA sdA{}; sdA.pSysMem = arg0;
        if (FAILED(m_dev->CreateBuffer(&bd, &sdA, &m_corteArgs))) { if (erro) *erro = L"fg: buffer de argumentos do detector de corte"; return false; }
        if (FAILED(m_dev->CreateUnorderedAccessView(m_corteStats, &ud, &m_corteUav)) ||
            FAILED(m_dev->CreateUnorderedAccessView(m_corteEst, &ud, &m_corteEstUav)) ||
            FAILED(m_dev->CreateUnorderedAccessView(m_corteArgs, &ud, &m_corteArgsUav))) { if (erro) *erro = L"fg: UAVs do detector de corte"; return false; }
        // medias de luma por bloco do quadro anterior (ver o cabecalho de HlslCorte, item 1)
        {
            D3D11_BUFFER_DESC bm{}; bm.ByteWidth = m_cgw * m_cgh * 4; bm.Usage = D3D11_USAGE_DEFAULT;
            bm.BindFlags = D3D11_BIND_UNORDERED_ACCESS; bm.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            std::vector<UINT> zeros((size_t)m_cgw * m_cgh, 0u);
            D3D11_SUBRESOURCE_DATA sdM{}; sdM.pSysMem = zeros.data();
            D3D11_UNORDERED_ACCESS_VIEW_DESC um{}; um.Format = DXGI_FORMAT_R32_TYPELESS; um.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            um.Buffer.NumElements = m_cgw * m_cgh; um.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            if (FAILED(m_dev->CreateBuffer(&bm, &sdM, &m_corteMed)) ||
                FAILED(m_dev->CreateUnorderedAccessView(m_corteMed, &um, &m_corteMedUav))) { if (erro) *erro = L"fg: buffer das medias do detector de corte"; return false; }
        }
        bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.MiscFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (int i = 0; i < kSlots; i++) if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_corteStg[i]))) { if (erro) *erro = L"fg: staging do detector de corte"; return false; }
        for (int i = 0; i < kSlots; i++) if (FAILED(m_dev->CreateUnorderedAccessView(m_outG[i], nullptr, &m_outGUav[i]))) { if (erro) *erro = L"fg: UAV do G (aplicacao do corte)"; return false; }
        // os tres numeros podem ser afinados na bancada sem recompilar
        double salto = kCorteSalto, fracMin = kCorteFracMin; wchar_t e[32]{};
        if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_SALTO", e, 32) > 0 && e[0]) { const double v = wcstod(e, nullptr); if (v > 1.0) salto = v; }
        if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_FRACMIN", e, 32) > 0 && e[0]) { const double v = wcstod(e, nullptr); if (v > 0.0 && v < 1.0) fracMin = v; }
        m_corteSalto = salto; m_corteFracMin = fracMin;
        // Se a bancada fixou o salto por env, ele NAO passa a variar com a taxa (a bancada manda).
        m_corteSaltoFixo = (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_SALTO", e, 32) > 0 && e[0]);
        CorteCB& cc = m_corteCc; cc = CorteCB{};
        cc.W = m_w; cc.H = m_h; cc.GW = m_cgw; cc.GH = m_cgh;
        cc.bw = (float)m_w / m_cgw; cc.bh = (float)m_h / m_cgh; cc.thr = kCorteNivel / 255.0f;
        cc.salto = (float)salto; cc.fracMin = (float)fracMin; cc.piso = (float)kCortePiso;
        cc.fracAlta = (float)kCorteFracAlta; cc.saltoAlta = (float)kCorteSaltoAlta;
        if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_FRACALTA", e, 32) > 0 && e[0]) { const double v = wcstod(e, nullptr); if (v > 0.0 && v <= 1.0) cc.fracAlta = (float)v; }
        m_corteSaltoAltaBase = cc.saltoAlta;
        // pisos dos dois saltos ajustaveis na bancada (a varredura que os escolheu esta no relatorio)
        m_corteSaltoMin = kCorteSaltoMin; m_corteSaltoAltaMin = kCorteSaltoAltaMin;
        if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_SALTOMIN", e, 32) > 0 && e[0]) { const double v = wcstod(e, nullptr); if (v >= 1.0 && v <= 20.0) m_corteSaltoMin = v; }
        if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_SALTOALTAMIN", e, 32) > 0 && e[0]) { const double v = wcstod(e, nullptr); if (v >= 1.0 && v <= 20.0) m_corteSaltoAltaMin = v; }
        cc.GX = (m_w + 7) / 8; cc.GY = (m_h + 7) / 8;
        // DINAMICO (era IMMUTABLE): os dois saltos sao reescritos a cada quadro a partir do
        // intervalo medido de chegada - ver o bloco de constantes acima.
        bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; bd.MiscFlags = 0; bd.ByteWidth = sizeof(CorteCB);
        D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = &cc;
        if (FAILED(m_dev->CreateBuffer(&bd, &sd, &m_cbCorte))) { if (erro) *erro = L"fg: cb do detector de corte"; return false; }
        const std::string hlslCorte = HlslCorte(m_corteAmostra);
        if (!CompilarCS(hlslCorte.c_str(), "PassoCorte", &m_csCorte, erro)) return false;
        if (!CompilarCS(hlslCorte.c_str(), "PassoCorteDecide", &m_csCorteDec, erro)) return false;
        if (!CompilarCS(hlslCorte.c_str(), "PassoCorteAplica", &m_csCorteAp, erro)) return false;
        m_medCorte.Iniciar(m_dev);
        if (m_log) {
            m_log("fg: detector de corte SEM campo ligado (decisao 100%% na GPU, sem leitura na CPU) - grade %ux%u (bloco %.1f px, %ux%u amostras), limiar %.0f niveis de luma; corte = mais de %.0f%% dos blocos E %.1fx a media movel, OU mais de %.0f%% e %.1fx",
                  m_cgw, m_cgh, (float)m_w / m_cgw, m_corteAmostra, m_corteAmostra, (double)kCorteNivel, fracMin * 100.0, salto, cc.fracAlta * 100.0, kCorteSaltoAlta);
            if (salto != kCorteSalto || fracMin != kCorteFracMin || cc.fracAlta != (float)kCorteFracAlta)
                m_log("fg: BANCADA - os limiares do detector de corte vieram de DLSS5_FG_CORTE_SALTO/_FRACMIN/_FRACALTA (padrao: %.1fx, %.2f, %.2f)", kCorteSalto, kCorteFracMin, kCorteFracAlta);
            if (m_corteSaltoFixo) m_log("fg: BANCADA - salto FIXO em %.2fx (nao acompanha a taxa de chegada)", salto);
            else m_log("fg: os saltos do detector acompanham o INTERVALO de chegada - %.1fx/%.1fx acima de %.0f fps, caindo ate %.1fx/%.1fx em %.0f fps (a media movel cresce com o espacamento e o corte nao; medido em D:\\claude-scratch\\corte3)",
                       kCorteSalto, m_corteSaltoAltaBase, 1000.0 / kCorteDtRef, m_corteSaltoMin, m_corteSaltoAltaMin,
                       1000.0 / (kCorteDtRef + (kCorteSalto - m_corteSaltoMin) / kCorteSaltoPorMs));
        }
        return true;
    }
    /* ======================= MASCARA DE HUD DEPOIS DO FI (X-HUD-PISCA, 19/09/2026) =======================
       O BUG DO DONO (COD Vanguard, fonte monitor, 2x): "o HUD fica piscando". O que acontece: o FI
       interpola a IMAGEM FINAL, e a mira, o minimapa e o texto sao pixels parados em cima de um
       fundo que anda. O fluxo optico do FSR e por bloco - o bloco que contem a borda do minimapa
       recebe o vetor do fundo e o quadro gerado (G) arrasta/esborrata o HUD; o quadro real (B)
       vem limpo; A -> G -> B alterna limpo/borrado/limpo = pisca. A mascara de HUD que ja existia
       (PassoA, "mascara aprendida") so ZERA VETORES DO VME - e no COD o VME devolve o campo
       inteiro zerado ("100% campo zerado" em todas as janelas do log), entao ela nao tinha o
       que zerar e o fluxo optico interno do FI fazia tudo sozinho. Sem campo de vetores nao ha
       como "corrigir o vetor" do HUD: o jeito e o do mercado (LSFG, AFMF 2, PESQ-FG §2.2):
       DEPOIS do FI, os pixels que nao mudaram entre A e B ha N quadros seguidos sao copiados
       de B para G em vez de ficarem com o valor interpolado. O HUD fica firme; a cena em volta
       continua interpolada.

       COMO: dois passes de compute na resolucao do FI, por pixel.
         PassoHudConta: d = max(|A-B|) por canal; d <= limiar -> run[p]++ (satura em 255), senao
           run[p] = 0. Conta os pixels com run >= N.
         PassoHudAplica: run >= N -> G[p] = B[p]. Pixel que mudou POUCO (d <= 3 x limiar) e tem
           >= 5 dos 8 vizinhos parados tambem e copiado: e o serrilhado da borda do texto sobre
           um fundo que anda - sem isso sobra um halo de 1 px piscando em volta de cada letra.
         Custo: ~0,1-0,2 ms em 3440x1440 (medido pelo MedidorGpu; entra em PassesMs e no gate).
       O QUE NAO FAZ: HUD semitransparente sobre fundo em movimento muda entre A e B e continua
       interpolado (o correto exige o buffer hudless do jogo - PESQ-FG §2.3). Contador que acaba
       de mudar leva N quadros para voltar a ser protegido (N = 3 no padrao: 2 quadros gerados).
       Fundo PARADO (ceu chapado, menu) tambem e "protegido" - e inocuo (G ja seria igual a B
       ali) e por isso a porcentagem do log e a area do HUD so com a camera em movimento.
       TETO (X-FG-QUALIDADE, 19/09/2026): no COD do dono (21:23-21:28) o log dizia "HUD protegido
       100,0% dos pixels" quadro apos quadro - a tela INTEIRA "parada ha 3 quadros" com a camera
       andando. Nao era HUD: eram quadros REPETIDOS da captura do monitor (A == B), e a mascara
       fazia G := B em 100% dos pixels - nenhuma interpolacao de fato, so o quadro real repetido
       ("nao parece que da fluidez"). O repetido agora nem chega aqui (CriarDedup); e como
       garantia, se mais de kHudMaxFrac da tela estiver "parada" o passo de aplicacao NAO copia
       nada (decidido na GPU, sem leitura na CPU): uma cena quase toda parada nao precisa de
       protecao - o FI ja devolve o proprio B onde nada se moveu -, e a mascara so faz sentido
       quando o HUD e uma ilha parada num fundo que anda. O log passa a dizer as duas fracoes:
       "parados" (medida) e "protegido" (aplicada).
       BANCADA: DLSS5_FG_HUD=0 desliga; DLSS5_FG_HUD_LIMIAR=<niveis 1..32> (padrao 3);
       DLSS5_FG_HUD_QUADROS=<1..60> (padrao 3); DLSS5_FG_HUD_MAX=<5..100> (% da tela, padrao 40). */
    static constexpr double kHudMaxFrac = 0.40;
    /* ---- O2-QUALIDADE (20/09/2026): DE "PARADO" PARA "TEM CARA DE INTERFACE".
       O detector de ate hoje era SO temporal: pixel que nao mudou por N quadros virava copia do
       quadro real. Medido na maquina do dono em 19/09 (COD Vanguard, fonte monitor): `HUD
       protegido 100,0% dos pixels` em 47 de 70 janelas - a tela INTEIRA, porque a captura do
       monitor repetia quadros e "nada mudou" nao distingue interface de fundo liso. O teto de 40%
       tapou o pior, mas o criterio continuou sendo o errado: um HUD sobre ceu liso que nao se
       mexe nao e diferente, para ele, do proprio ceu.
       AGORA a APARENCIA manda e o temporal so reforca (ver HlslHud, passo 1): contraste local
       alto (3x3) vezes CRISTA (o pixel destoa dos dois lados a 2 px - traco, nao borda de cena).
       A saida e uma confianca CONTINUA 0..1 por pixel, misturada com a media do bloco de 8x8, e
       o gerado recebe `lerp(G, B, peso)` com peso rampando a partir do corte - nada de binario.
       Corte 0,5 (ajustavel). Mira/centro: bonus multiplicativo num raio pequeno, entao so pega
       onde ha traco - um centro liso nao congela.
       BANCADA: DLSS5_FG_HUD=0 desliga a mascara (a desoclusao continua); DLSS5_FG_HUD_APAR=0
       volta o binario temporal de ate 20/09 com o MESMO exe; DLSS5_FG_HUD_CORTE=<5..95> (% da
       confianca, padrao 50); DLSS5_FG_HUD_MIRA=<0..500 px> (0 desliga; padrao 4,5% da altura);
       DLSS5_FG_HUD_LIMIAR / _QUADROS / _MAX continuam valendo. */
    static constexpr float kHudCorte    = 0.50f;   // confianca minima para comecar a misturar
    static constexpr float kHudApCon    = 0.25f;   // contraste local 3x3 que vale 1 (64 niveis)
    static constexpr float kHudApFino   = 0.10f;   // altura da crista que vale 1 (25 niveis)
    static constexpr float kHudMiraFrac = 0.045f;  // raio da mira, em fracao da altura interna
    static constexpr float kHudMiraBonus= 0.60f;   // bonus MULTIPLICATIVO no centro da mira
    /* ---- DESOCLUSAO (O2-QUALIDADE): ver PassoA. Onde o campo A->B e o do destino discordam, o
       FI esta interpolando um pedaco de fundo que sera coberto (ou descoberto) e arrasta um por
       cima do outro. Nao da para consertar o meio caminho sem saber o que ha atras; da para
       PESAR o gerado para o quadro real cuja reprojecao explica o bloco - e o que o passo faz,
       com forca proporcional ao tamanho da discordancia.
       BANCADA: DLSS5_FG_DESOC=0 desliga (1..100 = forca em %); DLSS5_FG_DESOC_DISC=<10..300>
       (% de discordancia do vetor a partir do qual o bloco conta como desoclusao, padrao 60). */
    static constexpr float kDesocDisc  = 0.60f;
    static constexpr float kDesocForca = 0.60f;
    /* FG-QUALIDADE2: "nenhum dos dois reais explica este bloco" (ver o PassoA). O limiar e a
       escala sao em NIVEIS DE LUMA por amostra, normalizados 0..1: 0,06 = ~15 niveis de 255, que
       e onde a diferenca deixa de ser ruido de captura e vira conteudo diferente; a escala diz em
       quanto acima disso a forca chega ao maximo. O peso 0 mantem a regra da O2 sozinha.
       BANCADA: DLSS5_FG_INC=<0..100> (peso em %), _INC_LIM e _INC_ESC em niveis de 255. */
    static constexpr float kIncLim  = 0.06f;
    static constexpr float kIncEsc  = 0.12f;
    static constexpr float kIncPeso = 0.70f;
    DXGI_FORMAT FmtEscala() {
        if (m_fmtEscala != DXGI_FORMAT_UNKNOWN) return m_fmtEscala;
        m_fmtEscala = DXGI_FORMAT_R32_FLOAT;   // o unico garantido pelo D3D11.0
        if (m_dev) {
            D3D11_FEATURE_DATA_FORMAT_SUPPORT2 f{}; f.InFormat = DXGI_FORMAT_R8_UNORM;
            if (SUCCEEDED(m_dev->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &f, sizeof(f))) &&
                (f.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE) &&
                (f.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD)) m_fmtEscala = DXGI_FORMAT_R8_UNORM;
        }
        return m_fmtEscala;
    }
    bool CriarHud(std::wstring* erro) {
        {
            wchar_t e[16]{};
            if (GetEnvironmentVariableW(L"DLSS5_FG_HUD", e, 16) > 0 && e[0] == L'0') {
                m_hudMascara = false;
                if (m_log) m_log("fg: BANCADA - DLSS5_FG_HUD=0: a mascara de interface depois do FI fica DESLIGADA (a desoclusao continua)");
            }
            if (GetEnvironmentVariableW(L"DLSS5_FG_HUD_LIMIAR", e, 16) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 1 && v <= 32) m_hudLimiar = (unsigned)v; }
            if (GetEnvironmentVariableW(L"DLSS5_FG_HUD_QUADROS", e, 16) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 1 && v <= 60) m_hudN = (unsigned)v; }
            if (GetEnvironmentVariableW(L"DLSS5_FG_HUD_MAX", e, 16) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 5 && v <= 100) m_hudMaxFrac = (double)v / 100.0; }
            if (GetEnvironmentVariableW(L"DLSS5_FG_HUD_CORTE", e, 16) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 5 && v <= 95) m_hudCorte = (float)v / 100.0f; }
            if (GetEnvironmentVariableW(L"DLSS5_FG_HUD_APAR", e, 16) > 0 && e[0] == L'0') m_hudApar = false;
            m_hudMiraR = kHudMiraFrac * (float)m_h;
            if (GetEnvironmentVariableW(L"DLSS5_FG_HUD_MIRA", e, 16) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 0 && v <= 500) m_hudMiraR = (float)v; }
            if (GetEnvironmentVariableW(L"DLSS5_FG_DESOC", e, 16) > 0 && e[0]) {
                const long v = wcstol(e, nullptr, 10);
                if (v == 0) m_desocLigado = false; else if (v >= 1 && v <= 100) m_desocForca = (float)v / 100.0f;
            }
            if (GetEnvironmentVariableW(L"DLSS5_FG_DESOC_DISC", e, 16) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 10 && v <= 300) m_desocDisc = (float)v / 100.0f; }
            // FG-QUALIDADE2: a mistura para o real quando nenhum dos dois explica o bloco
            if (GetEnvironmentVariableW(L"DLSS5_FG_INC", e, 16) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 0 && v <= 100) m_incPeso = (float)v / 100.0f; }
            if (GetEnvironmentVariableW(L"DLSS5_FG_INC_LIM", e, 16) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 1 && v <= 128) m_incLim = (float)v / 255.0f; }
            if (GetEnvironmentVariableW(L"DLSS5_FG_INC_ESC", e, 16) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 1 && v <= 128) m_incEsc = (float)v / 255.0f; }
        }
        if (!m_hudMascara && !m_desocLigado) { m_hudLigado = false; return true; }
        // escrita tipada em UAV no formato do G (a mesma exigencia do PassoCorteAplica)
        {
            D3D11_FEATURE_DATA_FORMAT_SUPPORT2 f2{}; f2.InFormat = m_fmt;
            const HRESULT hf = m_dev->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &f2, sizeof(f2));
            if (FAILED(hf) || !(f2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE)) {
                m_hudLigado = false;
                if (m_log) m_log("fg: a mascara de interface NAO subiu - esta placa nao aceita escrita tipada em UAV no formato %u; o quadro gerado segue sem protecao de interface nem desoclusao", (unsigned)m_fmt);
                return true;
            }
            // LEITURA tipada: sem ela nao da para fazer lerp com o que ja esta no G, e a mistura
            // continua (binaria no corte) pelo segundo shader. Toda placa recente declara.
            m_hudMistura = (f2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0;
        }
        // run: quadros seguidos em que cada pixel nao mudou (UAV no passo 1, SRV no passo 2). O passo 1
        // LE o UAV (run + 1): leitura tipada em UAV so e garantida em R32_UINT; R8_UINT (4x menos
        // VRAM) so se a placa declarar UAV_TYPED_LOAD nele (toda placa recente declara).
        {
            DXGI_FORMAT fr = DXGI_FORMAT_R32_UINT;
            D3D11_FEATURE_DATA_FORMAT_SUPPORT2 f8{}; f8.InFormat = DXGI_FORMAT_R8_UINT;
            if (SUCCEEDED(m_dev->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &f8, sizeof(f8))) && (f8.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD)) fr = DXGI_FORMAT_R8_UINT;
            D3D11_TEXTURE2D_DESC td{}; td.Width = m_w; td.Height = m_h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = fr; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(m_dev->CreateTexture2D(&td, nullptr, &m_hudRun)) ||
                FAILED(m_dev->CreateUnorderedAccessView(m_hudRun, nullptr, &m_hudRunUav)) ||
                FAILED(m_dev->CreateShaderResourceView(m_hudRun, nullptr, &m_hudRunSrv))) { if (erro) *erro = L"fg: textura de quadros parados da mascara de interface"; return false; }
            m_hudLimpar = true;   // conteudo indefinido ao nascer: zerada no primeiro Gerar
        }
        // conf: a confianca continua 0..1 por pixel (UAV no passo 1, SRV no passo 2)
        {
            D3D11_TEXTURE2D_DESC td{}; td.Width = m_w; td.Height = m_h; td.MipLevels = 1; td.ArraySize = 1;
            td.Format = FmtEscala(); td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            if (FAILED(m_dev->CreateTexture2D(&td, nullptr, &m_hudConf)) ||
                FAILED(m_dev->CreateUnorderedAccessView(m_hudConf, nullptr, &m_hudConfUav)) ||
                FAILED(m_dev->CreateShaderResourceView(m_hudConf, nullptr, &m_hudConfSrv))) { if (erro) *erro = L"fg: textura de confianca da mascara de interface"; return false; }
        }
        for (int i = 0; i < kSlots; i++) if (FAILED(m_dev->CreateUnorderedAccessView(m_outG[i], nullptr, &m_hudGUav[i]))) { if (erro) *erro = L"fg: UAV do G (mascara de interface)"; return false; }
        // stats: [0] = pixels com confianca acima do corte, [4] = peso aplicado (em 1024 avos)
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = 4; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_hudStats)) ||
                FAILED(m_dev->CreateUnorderedAccessView(m_hudStats, &ud, &m_hudStatsUav))) { if (erro) *erro = L"fg: stats da mascara de interface"; return false; }
            bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.MiscFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            for (int i = 0; i < kSlots; i++) if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_hudStg[i]))) { if (erro) *erro = L"fg: staging da mascara de interface"; return false; }
        }
        // o cb agora e DINAMICO: bw/bh (a grade do campo) so existem depois de LigarCampoLocal, e
        // corte/mira/forca sao de bancada. Reescrito a cada quadro, como o cb dos passes.
        {
            D3D11_BUFFER_DESC bd{}; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; bd.ByteWidth = sizeof(HudCB);
            if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_cbHud))) { if (erro) *erro = L"fg: cb da mascara de interface"; return false; }
        }
        if (!CompilarCS(HlslHud(), "PassoHudConta", &m_csHudConta, erro)) return false;
        if (!CompilarCS(HlslHud(), m_hudMistura ? "PassoHudAplica" : "PassoHudAplicaBin", &m_csHudAplica, erro)) return false;
        m_medHud.Iniciar(m_dev);
        if (m_log) m_log("fg: mascara de interface por APARENCIA ligada - confianca 0..1 = contraste local x crista (traco fino), com a estabilidade temporal (>= %u quadros, |A-B| <= %u niveis) como reforco; corte %.2f, mistura %s por regiao de 8x8, mira r=%.0f px; TETO: acima de %.0f%% da tela acima do corte nada e aplicado. Desoclusao: %s (forca %.0f%%, discordancia %.0f%%, so-meio %.0f%% acima de %.0f niveis). DLSS5_FG_HUD=0 / _APAR=0 / _CORTE / _MIRA / DLSS5_FG_DESOC na bancada",
                         m_hudN, m_hudLimiar, m_hudCorte, m_hudMistura ? "CONTINUA" : "binaria (placa sem leitura tipada em UAV)",
                         m_hudMiraR, 100.0 * m_hudMaxFrac, m_desocLigado ? "ligada" : "desligada", 100.0f * m_desocForca, 100.0f * m_desocDisc, 100.0f * m_incPeso, 255.0f * m_incLim);
        return true;
    }
    /* ======================= QUADRO REPETIDO NAO ENTRA NO FI (X-FG-QUALIDADE, 19/09/2026) =======================
       O BUG DO DONO (COD Vanguard, fonte monitor, Interpolada 2x, 21:23-21:28): "nao parece que o
       frame gen ta me dando fluidez" e "no 2x ja noto muitos defeitos na tela". O que o log prova:
         - `HUD protegido 100,0% dos pixels (parados >= 3 quadros)` em 47 de 70 janelas: A e B iguais
           em TODOS os pixels (ate 3 niveis), tres quadros seguidos, com o jogo andando;
         - `mv-hw: ... 55-70% campo zerado` com `600 novas, 0-17 repetidas`: o VME da rede ve campo
           ZERO na maior parte dos quadros, e o dedup dele diz que sao "novos" - porque ele
           deduplica pelo carimbo (SystemRelativeTime do WGC), e cada entrega tem um carimbo novo;
         - `LAT ritmo do WGC: p05 0,01 ms`: a captura do monitor entrega em RAJADA (dois quadros
           colados) - o compositor entrega um quadro por composicao, e nem toda composicao tem um
           quadro NOVO do jogo (a nossa propria pelicula, excluida da captura, tambem compoe);
         - na unica janela em que o WGC veio limpo (21:26:07, p05 25 ms) o HUD caiu para 24-49% e o
           VME viu 58% de blocos com movimento: o par distinto se comporta como esperado.
       Resultado: o FI recebia (A, A), gerava G = A e o apresentador mostrava A, A, A, G(A,B), B em
       passos de meio intervalo: quadro real repetido por tres presents (nenhuma fluidez) e o
       gerado entre A e B espremido num present so, com o campo do VME zerado e o intervalo errado
       (defeitos).

       O QUE FAZ: antes de qualquer despacho, um passo de compute compara B com o A anterior em
       uma grade de amostras (1 a cada 4x4 px = 310 k amostras em 3440x1440) e conta as que mudaram
       mais que kDedupLimiar niveis (max |A-B| por canal). O contador volta para a CPU por um
       staging com Map(DO_NOT_WAIT) - ver a LEITURA ATRASADA mais abaixo, que e como ele volta hoje.
       Fracao mudada abaixo de kDedupFracMin => REPETIDO: nada e
       despachado, m_ger nao avanca (o proximo B cai no mesmo slot e o par passa a ser (A, proximo)),
       o historico do FI nao ve o duplicado, e o chamador nao apresenta nada (a tela ja mostra este
       conteudo).
       O intervalo que o FI recebe (frameTimeDelta) e o que o apresentador mede entre quadros
       DISTINTOS - ver apresentador_gpu.h, LacoInterpolado.
       Nao roda com o caminho J5 (dados do jogo pela ponte: um quadro por quadro do jogo, nunca repete).

       ---- LEITURA ATRASADA (K-DEDUP, 20/09/2026): POR QUE O DE CIMA ESTAVA INERTE ----
       Ate 20/09 o veredito era esperado NA HORA: Map(DO_NOT_WAIT) num laco de espera ATIVA ate
       kDedupEsperaMs. Na maquina do dono (COD Vanguard na mesma placa) a resposta NUNCA chegou
       nesse teto - o despacho das 310 k amostras entra na fila do contexto imediato ATRAS do
       `c4->Wait(m_fOut11)` do quadro anterior (espera na GPU pelo FI), dos passes de HUD e do
       Present. Medido em 6 min de sessao (LS-QUALIDADE 6.3): `dedup 3,00/3,00 ms (SEM RESPOSTA)`
       em 100% das janelas - o dedup inerte, `repetidos 0` sem significado nenhum, a rajada do WGC
       (pares a 0,01 ms, metade da cadeia crua repetida) voltando inteira para dentro do FI, e
       ainda 3 ms de espera ativa de CPU por quadro na thread de ritmo.
       AGORA: o PassoDif continua na GPU, mas o contador vai para um ANEL de tres stagings e e
       lido nas voltas SEGUINTES, com Map(DO_NOT_WAIT) so no que ja esta pronto e sem nunca
       esperar (o mesmo idioma do detector de corte, :1309, e da mascara de HUD, :1366; nenhum dos
       dois faz Flush). O veredito do quadro n fica pronto na volta n+1 (as vezes n+2) - a tempo de
       o apresentador tirar o par (G, B) DELE da fila antes de ele ir a tela, que e onde mora o
       prejuizo visivel (dois presents da mesma imagem e o ritmo empurrado). Ver QuadroRepetido()
       para o gancho e o ponto de chamada. Se o veredito de um quadro nunca chegar, ele conta em
       DedupDesconhecidos() e nada e descartado (o de sempre: trata como distinto).
       BANCADA: DLSS5_FG_DEDUP=0 desliga; DLSS5_FG_DEDUP_LIMIAR=<niveis 1..64> (padrao 6);
       DLSS5_FG_DEDUP_FRAC=<0.01..20> (% de amostras mudadas abaixo do qual e repetido, padrao 0,5). */
    static constexpr unsigned kDedupLimiar  = 6;      // niveis (0-255) de mudanca por amostra
    static constexpr double   kDedupFracMin = 0.005;  // < 0,5% das amostras mudaram => repetido
    static constexpr UINT     kDedupPasso   = 4;      // 1 amostra a cada 4x4 px
    static constexpr int      kDedupAnel    = 3;      // stagings de readback em anel (leitura atrasada)
    static constexpr int      kDedupVer     = 16;     // vereditos guardados, por id de quadro
    bool CriarDedup(std::wstring* erro) {
        // N5 - o retangulo ativo nasce sendo a tela inteira, e tem de valer mesmo por estes
        // caminhos de saida antecipada: e ele que vai no generationRect de todo despacho.
        m_lbX = m_lbY = 0; m_lbW = (int)m_w; m_lbH = (int)m_h; m_lbAtivo = false; m_lbCandN = 0;
        {
            wchar_t e[32]{};
            if (GetEnvironmentVariableW(L"DLSS5_FG_DEDUP", e, 32) > 0 && e[0] == L'0') {
                m_dedupLigado = false;
                if (m_log) m_log("fg: BANCADA - DLSS5_FG_DEDUP=0: quadro repetido da captura ENTRA no FI (o de antes de 19/09)");
                return true;
            }
            if (GetEnvironmentVariableW(L"DLSS5_FG_DEDUP_LIMIAR", e, 32) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 1 && v <= 64) m_dedupLimiar = (unsigned)v; }
            if (GetEnvironmentVariableW(L"DLSS5_FG_DEDUP_FRAC", e, 32) > 0 && e[0]) { const double v = wcstod(e, nullptr); if (v >= 0.01 && v <= 20.0) m_dedupFracMin = v / 100.0; }
            // K-DEDUP: DLSS5_FG_DEDUP_ESPERA nao existe mais - nao ha espera nenhuma para regular.
            if (GetEnvironmentVariableW(L"DLSS5_FG_DEDUP_ESPERA", e, 32) > 0 && e[0] && m_log)
                m_log("fg: DLSS5_FG_DEDUP_ESPERA nao vale mais (K-DEDUP, 20/09/2026): a resposta da GPU e lida um quadro depois, sem espera");
            // N2 - corte de cena pela fracao de amostras que mudaram MUITO
            if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_SAD", e, 32) > 0 && e[0] == L'0') m_corteSadLigado = false;
            if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_SAD_FRAC", e, 32) > 0 && e[0]) { const double v = wcstod(e, nullptr); if (v >= 10.0 && v <= 99.0) m_corteSadFrac = v / 100.0; }
            if (GetEnvironmentVariableW(L"DLSS5_FG_CORTE_SAD_NIVEL", e, 32) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 8 && v <= 200) m_corteNivel = (unsigned)v; }
            // N5 - barras pretas
            if (GetEnvironmentVariableW(L"DLSS5_FG_LETTERBOX", e, 32) > 0 && e[0] == L'0') m_lbLigado = false;
            if (GetEnvironmentVariableW(L"DLSS5_FG_LETTERBOX_NIVEL", e, 32) > 0 && e[0]) { const long v = wcstol(e, nullptr, 10); if (v >= 1 && v <= 64) m_lbNivel = (unsigned)v; }
        }
        m_dedupGX = (m_w + kDedupPasso - 1) / kDedupPasso; m_dedupGY = (m_h + kDedupPasso - 1) / kDedupPasso;
        m_dedupAmostras = (double)m_dedupGX * (double)m_dedupGY;
        if (m_dedupAmostras < 1.0) { m_dedupLigado = false; return true; }
        /* N2/N5 - O MESMO PASSO, MAIS DUAS RESPOSTAS. O PassoDif ja visita cada amostra dos dois
           quadros; medir mais duas coisas ali custa dois atomicos de grupo e nenhuma leitura a
           mais de textura, e as duas voltam pelo MESMO anel de stagings (nenhuma espera nova):
             [1]  quantas amostras mudaram MUITO (limiar alto) -> corte de cena (N2);
             bits de LINHA e de COLUNA que tem luz no quadro novo -> barras pretas (N5).
           Os bits sao empacotados 32 por palavra; um grupo de 8x8 cobre 8 linhas consecutivas
           comecando num multiplo de 8, que cabem SEMPRE numa palavra so - dai um InterlockedOr
           por grupo em vez de um por amostra. */
        m_lbNRW = (m_dedupGY + 31) / 32; m_lbNCW = (m_dedupGX + 31) / 32;
        m_dedupPalavras = 4 + m_lbNRW + m_lbNCW;
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = m_dedupPalavras; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 4 * m_dedupPalavras; bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_dedupStats)) ||
                FAILED(m_dev->CreateUnorderedAccessView(m_dedupStats, &ud, &m_dedupStatsUav))) { if (erro) *erro = L"fg: stats do dedup"; return false; }
            bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.MiscFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            for (int i = 0; i < kDedupAnel; i++)
                if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_dedupStg[i]))) { if (erro) *erro = L"fg: staging do dedup"; return false; }
        }
        /* X-GERADO: a METRICA DE PROVA usa o MESMO shader e o mesmo cb (so as duas texturas de
           entrada mudam). Um buffer com DUAS metades - (A,G) na primeira, (B,G) na segunda - e uma
           UAV crua por metade: num UAV RAW os enderecos do shader sao relativos ao FirstElement da
           view, entao o PassoDif escreve no lugar certo sem saber de nada. Se a prova nao nascer,
           so ela deixa de existir: `if (m_provaStats)` em todo ponto de uso. */
        {
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = 4; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            if (SUCCEEDED(m_dev->CreateBuffer(&bd, nullptr, &m_provaStats)) &&
                SUCCEEDED(m_dev->CreateUnorderedAccessView(m_provaStats, &ud, &m_provaUav))) {
                bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.MiscFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                for (int i = 0; i < kDedupAnel; i++)
                    if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_provaStg[i]))) { Soltar(m_provaStats); break; }
            } else Soltar(m_provaStats);
            std::wstring eP;
            if (m_provaStats && !CompilarCS(HlslProva(), "PassoProva", &m_csProva, &eP)) Soltar(m_provaStats);
            if (!m_provaStats && m_log) m_log("fg: a metrica de prova do gerado (distinto de A / de B / da mistura) nao pode ser criada - o resto segue igual");
        }
        for (int i = 0; i < kDedupVer; i++) { m_dedupVerN[i] = ~0ull; m_dedupVerR[i] = 0; m_dedupVerC[i] = 0; }
        {
            struct DedupCB { UINT W, H, SX, SY; float thr, thrAlto, thrLuma, p0; UINT offLin, offCol, p1, p2; }
            cb{ m_w, m_h, kDedupPasso, kDedupPasso,
                (float)m_dedupLimiar / 255.0f, (float)m_corteNivel / 255.0f, (float)m_lbNivel / 255.0f, 0.f,
                16u, 16u + 4u * m_lbNRW, 0u, 0u };
            D3D11_BUFFER_DESC bd{}; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.ByteWidth = sizeof(cb);
            D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = &cb;
            if (FAILED(m_dev->CreateBuffer(&bd, &sd, &m_cbDedup))) { if (erro) *erro = L"fg: cb do dedup"; return false; }
        }
        if (!CompilarCS(HlslDedup(), "PassoDif", &m_csDedup, erro)) return false;
        if (m_log) m_log("fg: no MESMO passo (N2/N5) - CORTE DE CENA quando mais de %.0f%% das amostras mudam acima de %u niveis (so na transicao: o quadro anterior tem de estar abaixo da metade disso, senao e fade ou movimento rapido) -> reset no FI; LETTERBOX pelas linhas/colunas com luminancia abaixo de %u, aplicado depois de %d medidas iguais para encolher e na hora para crescer. DLSS5_FG_CORTE_SAD=0 e DLSS5_FG_LETTERBOX=0 desligam%s%s",
                         100.0 * m_corteSadFrac, m_corteNivel, m_lbNivel, kLbEstavel,
                         m_corteSadLigado ? "" : " (corte DESLIGADO)", m_lbLigado ? "" : " (letterbox DESLIGADO)");
        if (m_log) m_log("fg: quadro REPETIDO nao entra no FI - B e comparado com o A anterior em %ux%u amostras (1 a cada %ux%u px); menos de %.2f%% mudadas (> %u niveis) = repetido. A resposta da GPU e lida UM QUADRO DEPOIS, num anel de %d stagings, sem nunca esperar (K-DEDUP). DLSS5_FG_DEDUP=0 desliga",
                         m_dedupGX, m_dedupGY, kDedupPasso, kDedupPasso, 100.0 * m_dedupFracMin, m_dedupLimiar, kDedupAnel);
        return true;
    }
    /* K-DEDUP - despacho. Roda no Gerar, logo depois da copia de B: compara B com o A anterior e
       manda o contador para um staging livre do anel. NAO espera, NAO da Flush e NAO devolve
       veredito - quem colhe e DedupColher(), nas voltas seguintes. */
    void DedupDespachar(ID3D11DeviceContext* ctx, int cur, int prev, uint64_t n) {
        int k = -1;
        for (int i = 0; i < kDedupAnel; i++) if (!m_dedupPend[i]) { k = i; break; }
        if (k < 0) {   // anel cheio (a GPU nao respondeu por 3 voltas): o mais velho perde a vez
            k = 0;
            for (int i = 1; i < kDedupAnel; i++) if (m_dedupPendN[i] < m_dedupPendN[k]) k = i;
            m_dedupPend[k] = false; m_dedupDesconhecidos++;
        }
        const UINT zero[4] = { 0, 0, 0, 0 };
        ctx->ClearUnorderedAccessViewUint(m_dedupStatsUav, zero);
        ID3D11ShaderResourceView* s2[2] = { m_inBFiSrv[prev], m_inBFiSrv[cur] };
        ctx->CSSetConstantBuffers(0, 1, &m_cbDedup);
        ctx->CSSetShader(m_csDedup, nullptr, 0);
        ctx->CSSetShaderResources(0, 2, s2);
        ctx->CSSetUnorderedAccessViews(0, 1, &m_dedupStatsUav, nullptr);
        ctx->Dispatch((m_dedupGX + 7) / 8, (m_dedupGY + 7) / 8, 1);
        ID3D11UnorderedAccessView* nu = nullptr; ID3D11ShaderResourceView* ns2[2] = {}; ID3D11Buffer* nb = nullptr;
        ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr); ctx->CSSetShaderResources(0, 2, ns2);
        ctx->CSSetConstantBuffers(0, 1, &nb); ctx->CSSetShader(nullptr, nullptr, 0);
        /* um so m_dedupStats para todos: a ordem do contexto imediato garante que esta copia leva o
           valor DESTE quadro antes do Clear da volta seguinte. */
        ctx->CopyResource(m_dedupStg[k], m_dedupStats);
        m_dedupPend[k] = true; m_dedupPendN[k] = n; m_dedupPendMs[k] = Ms();
        m_dedupN++;
    }
    /* ===== X-GERADO (20/09/2026): A METRICA DE PROVA =====
       Roda no fim do Gerar, com o G ja pronto (depois do corte e da mascara de HUD): duas passadas
       do MESMO PassoDif do dedup, (A, G) e (B, G), sobre a mesma grade de amostras. Sai no log de
       5 s como `gerado: distinto de A X% / de B Y%`. E a unica medida que separa "o FI interpolou"
       de "o FI devolveu o quadro real": G copia de B da Y = 0,0%, e uma interpolacao de verdade da
       X e Y parecidos, na ordem da fracao da tela que se move. NAO ESPERA: anel de stagings e
       Map(DO_NOT_WAIT), o mesmo idioma do dedup e do detector de corte. */
    void ProvaDespachar(ID3D11DeviceContext* ctx, int cur, int prev) {
        if (!m_provaStats || !m_csProva || !m_outGSrv[cur]) return;
        int k = -1;
        for (int i = 0; i < kDedupAnel; i++) if (!m_provaPend[i]) { k = i; break; }
        if (k < 0) return;   // a GPU nao respondeu por 3 voltas: esta medida perde a vez (e so log)
        const UINT zero[4] = { 0, 0, 0, 0 };
        ctx->ClearUnorderedAccessViewUint(m_provaUav, zero);
        ID3D11ShaderResourceView* s3[3] = { m_inBFiSrv[prev], m_inBFiSrv[cur], m_outGSrv[cur] };
        ctx->CSSetConstantBuffers(0, 1, &m_cbDedup);
        ctx->CSSetShader(m_csProva, nullptr, 0);
        ctx->CSSetShaderResources(0, 3, s3);
        ctx->CSSetUnorderedAccessViews(0, 1, &m_provaUav, nullptr);
        ctx->Dispatch((m_dedupGX + 7) / 8, (m_dedupGY + 7) / 8, 1);
        ID3D11UnorderedAccessView* nu = nullptr; ID3D11ShaderResourceView* ns3[3] = {}; ID3D11Buffer* nb = nullptr;
        ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr); ctx->CSSetShaderResources(0, 3, ns3);
        ctx->CSSetConstantBuffers(0, 1, &nb); ctx->CSSetShader(nullptr, nullptr, 0);
        ctx->CopyResource(m_provaStg[k], m_provaStats);
        m_provaPend[k] = true;
    }
    void ProvaColher(ID3D11DeviceContext* ctx) {
        if (!m_provaStats || m_dedupAmostras < 1.0) return;
        for (int k = 0; k < kDedupAnel; k++) {
            if (!m_provaPend[k]) continue;
            D3D11_MAPPED_SUBRESOURCE m{};
            if (FAILED(ctx->Map(m_provaStg[k], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) continue;
            const uint32_t* w32 = (const uint32_t*)m.pData;
            const double fa = (double)w32[0] / m_dedupAmostras;
            const double fb = (double)w32[1] / m_dedupAmostras;
            const double fm = (double)w32[2] / m_dedupAmostras;
            ctx->Unmap(m_provaStg[k], 0);
            m_provaPend[k] = false;
            m_provaA = m_provaA < 0.0 ? fa : 0.9 * m_provaA + 0.1 * fa;
            m_provaB = m_provaB < 0.0 ? fb : 0.9 * m_provaB + 0.1 * fb;
            m_provaM = m_provaM < 0.0 ? fm : 0.9 * m_provaM + 0.1 * fm;
        }
    }
    /* K-DEDUP - colheita. Um Map(DO_NOT_WAIT) por staging pendente: o que ja esta pronto vira
       veredito do quadro que ele mede, o que nao esta fica para a proxima volta (nenhuma espera).
       `nAgora` e o quadro desta volta, so para medir o atraso em quadros. */
    void DedupColher(ID3D11DeviceContext* ctx, uint64_t nAgora) {
        for (int k = 0; k < kDedupAnel; k++) {
            if (!m_dedupPend[k]) continue;
            D3D11_MAPPED_SUBRESOURCE m{};
            if (FAILED(ctx->Map(m_dedupStg[k], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) continue;
            const uint32_t* w32 = (const uint32_t*)m.pData;
            const uint32_t mudadas = w32[0];
            const uint32_t altas   = w32[1];
            /* N5 - os bits de linha/coluna saem daqui antes do Unmap; a conta e na CPU porque
               sao algumas dezenas de palavras (39 em 3440x1440) e nenhuma delas por pixel. */
            int lbX = 0, lbY = 0, lbW = (int)m_w, lbH = (int)m_h;
            const bool lbLido = m_lbLigado && LetterboxDoQuadro(w32 + 4, &lbX, &lbY, &lbW, &lbH);
            ctx->Unmap(m_dedupStg[k], 0);
            m_dedupPend[k] = false;
            const uint64_t id = m_dedupPendN[k];
            const double frac = (double)mudadas / m_dedupAmostras;
            const int rep = (frac < m_dedupFracMin) ? 1 : 0;
            /* ---- N2: CORTE DE CENA NO UNIVERSAL. O `reset` que o FI espera so chegava do jogo
               (caminho J5); no Universal nao ha quem o declare e todo corte de camera atravessa
               borrado, porque o interpolador segue achando que os dois quadros sao vizinhos.
               O criterio e a fracao de amostras que mudaram MUITO (m_corteNivel niveis, bem acima
               do limiar do dedup): num corte ela vai a quase 1, e em movimento rapido normal fica
               abaixo. HISTERESE, e ela e o que separa corte de FADE: um corte e uma TRANSICAO -
               o quadro anterior nao pode ter estado igualmente alto. Um fade lento nunca chega
               ao limiar, e um fade rapido chega em varios quadros seguidos, dos quais so o
               primeiro conta. Depois de um corte a histerese so rearma quando a fracao cai
               abaixo de m_corteSadFrac/2. */
            const double fracA = (double)altas / m_dedupAmostras;
            int corte = 0;
            if (m_corteSadLigado && !rep && id > m_dedupUltId) {
                if (fracA > m_corteSadFrac) { if (m_corteSadArmado) { corte = 1; m_corteSadArmado = false; } }
                else if (fracA < 0.5 * m_corteSadFrac) m_corteSadArmado = true;
            }
            const int v = (int)(id % kDedupVer);
            m_dedupVerN[v] = id; m_dedupVerR[v] = (int8_t)rep; m_dedupVerC[v] = (int8_t)corte;
            if (corte) {
                m_cortesSad++; m_cortesSadJan++;
                /* O veredito de `id` chega na volta `id+1`: o par que ATRAVESSA o corte
                   (A = id-1, B = id) ja foi despachado. O que ainda da para consertar - e e o
                   que falta hoje - e a HISTORIA do FI: sem reset, o par seguinte (id, id+1)
                   nasce com o quadro de antes do corte no passado do interpolador. O `dd.reset`
                   do proximo despacho e o mesmo que o jogo manda no J5. */
                m_corteSadArmar = true;
            }
            if (lbLido && id > m_dedupUltId) LetterboxDecidir(lbX, lbY, lbW, lbH);
            if (id > m_dedupUltId) m_dedupUltId = id;
            m_dedupUltFrac = frac;
            m_dedupJanN++; if (rep) { m_repetidos++; m_dedupJanRep++; }
            m_dedupRespAcc += Ms() - m_dedupPendMs[k];
            m_dedupAtrasoAcc += (double)(nAgora >= id ? nAgora - id : 0); m_dedupRespN++;
            if (m_log && m_repetidosLog < 3 && (m_repetidos == 1 || m_repetidos == 100 || m_repetidos == 1000)) {
                m_repetidosLog++;
                m_log("fg: quadro %llu REPETIDO por conteudo (%.3f%% das amostras mudaram, limiar %.2f%%) - a captura entregou a mesma imagem de novo; %llu repetidos ate agora",
                      (unsigned long long)id, 100.0 * frac, 100.0 * m_dedupFracMin, (unsigned long long)m_repetidos);
            }
        }
    }
    /* ===== N5 - LETTERBOX: O RETANGULO ATIVO =====
       `generationRect` era a tela inteira, sempre. Num video 2,39:1, num jogo ultrawide numa
       tela 16:9, ou numa cutscene com barras, o interpolador gasta passe e inventa movimento em
       faixas que sao PRETAS nos dois quadros - e e nas bordas dessas faixas que o artefato
       aparece. A AMD chama isto de `interpolationRect`; a ideia e a mesma.
       COMO SE DETECTA, e de graca: o passo do dedup ja visita a grade de amostras, e um bit por
       linha e por coluna diz se ALGUMA amostra dali tem luz. Barra preta = nenhuma tem.
       Devolve false quando nao ha linha nenhuma com luz (quadro inteiro escuro - um fade, uma
       tela de carregamento): ai nao ha o que concluir e o retangulo de antes fica como estava. */
    static constexpr double kLbFracMin  = 0.50;   // o retangulo ativo nao pode ser menor que isto
    static constexpr int    kLbEstavel  = 30;     // quadros com a mesma medida antes de encolher
    bool LetterboxDoQuadro(const uint32_t* bits, int* x, int* y, int* w, int* h) const {
        const uint32_t* lin = bits; const uint32_t* col = bits + m_lbNRW;
        int r0 = -1, r1 = -1, c0 = -1, c1 = -1;
        for (UINT r = 0; r < m_dedupGY; r++) if (lin[r >> 5] & (1u << (r & 31u))) { if (r0 < 0) r0 = (int)r; r1 = (int)r; }
        for (UINT c = 0; c < m_dedupGX; c++) if (col[c >> 5] & (1u << (c & 31u))) { if (c0 < 0) c0 = (int)c; c1 = (int)c; }
        if (r0 < 0 || c0 < 0) return false;
        const int px = (int)kDedupPasso;
        int xx = c0 * px, yy = r0 * px;
        int ww = std::min((int)m_w, (c1 + 1) * px) - xx;
        int hh = std::min((int)m_h, (r1 + 1) * px) - yy;
        /* TETO DAS BARRAS. Acima disto nao e letterbox: e cena escura, fade, ou o jogo de fato
           quase todo preto - e recortar ali jogaria fora imagem de verdade. 4:3 numa tela 21:9
           (o caso mais estreito que existe de fato) fica em 56% da largura, acima do piso. */
        if (ww < (int)(kLbFracMin * (double)m_w) || hh < (int)(kLbFracMin * (double)m_h)) { ww = (int)m_w; hh = (int)m_h; xx = yy = 0; }
        *x = xx; *y = yy; *w = ww; *h = hh;
        return true;
    }
    /* A DECISAO, com as duas direcoes tratadas de propositos diferentes:
       ENCOLHER o retangulo (barras aparecendo) so depois de kLbEstavel medidas iguais - recortar
       por engano custa imagem, entao a barra tem de estar mesmo la;
       CRESCER (barras sumindo) vale NA HORA - deixar de recortar nunca perde nada, e esperar 30
       quadros para voltar ao inteiro deixaria a primeira meia-segunda depois da cutscene sem
       interpolacao nas beiradas. */
    void LetterboxDecidir(int x, int y, int w, int h) {
        if (x == m_lbCx && y == m_lbCy && w == m_lbCw && h == m_lbCh) { if (m_lbCandN < 1000000) m_lbCandN++; }
        else { m_lbCx = x; m_lbCy = y; m_lbCw = w; m_lbCh = h; m_lbCandN = 1; }
        const bool cresceu = (w > m_lbW) || (h > m_lbH);
        if (!cresceu && m_lbCandN < kLbEstavel) return;
        if (x == m_lbX && y == m_lbY && w == m_lbW && h == m_lbH) return;
        m_lbX = x; m_lbY = y; m_lbW = w; m_lbH = h;
        m_lbAtivo = (w < (int)m_w) || (h < (int)m_h);
        m_lbTrocas++;
        if (m_log) m_log("fg: letterbox: %d,%d,%d,%d%s", m_lbX, m_lbY, m_lbW, m_lbH,
                         m_lbAtivo ? " - as barras pretas ficam fora da interpolacao e saem do quadro real" : " (tela inteira: sem barras)");
    }
    /* K-DEDUP - o veredito de um quadro: 1 repetido, 0 distinto, -1 ainda sem resposta (ou velho
       demais: o anel de vereditos guarda os ultimos kDedupVer). */
    int DedupVeredito(uint64_t id) const {
        const int v = (int)(id % kDedupVer);
        return m_dedupVerN[v] == id ? (int)m_dedupVerR[v] : -1;
    }
    /* K-DEDUP - a linha de 5 s. Sai daqui (e nao do relatorio do apresentador) porque quem sabe
       quantos vereditos chegaram, com que atraso, e este anel. "repetidos X/Y" = de Y quadros com
       veredito na janela, X eram a mesma imagem do anterior; em rajada do WGC o esperado e ~50%. */
    void DedupLog5s(uint64_t n) {
        const double agora = Ms();
        if (m_dedupLogMs == 0.0) { m_dedupLogMs = agora; return; }
        if (agora - m_dedupLogMs < 5000.0) return;
        m_dedupLogMs = agora;
        if (m_log)
            m_log("dedup: repetidos %llu/%llu (%.0f%%) | desoclusao %.0f%% dos blocos (so-meio %.1f%%) | interface %.1f%% (candidatos %.1f%%, %.3f ms) | resposta %.1f ms atrasada %.1f quadros | sem resposta %llu | cortes %llu (%llu no total) | letterbox %d,%d,%d,%d | quadro %llu",
                  (unsigned long long)m_dedupJanRep, (unsigned long long)m_dedupJanN,
                  m_dedupJanN ? 100.0 * (double)m_dedupJanRep / (double)m_dedupJanN : 0.0,
                  100.0 * m_desocFrac, 100.0 * m_incFrac, 100.0 * m_hudFrac, 100.0 * m_hudParados, m_medHud.Media(),
                  m_dedupRespN ? m_dedupRespAcc / (double)m_dedupRespN : 0.0,
                  m_dedupRespN ? m_dedupAtrasoAcc / (double)m_dedupRespN : 0.0,
                  (unsigned long long)m_dedupDesconhecidos,
                  (unsigned long long)m_cortesSadJan, (unsigned long long)m_cortesSad,
                  m_lbX, m_lbY, m_lbW, m_lbH, (unsigned long long)n);
        m_dedupJanRep = 0; m_dedupJanN = 0; m_dedupRespAcc = 0.0; m_dedupAtrasoAcc = 0.0; m_dedupRespN = 0;
        m_cortesSadJan = 0;
    }
    bool CriarGrade(UINT gridW, UINT gridH, float covU, float covV, std::wstring* erro) {
        m_gw = gridW; m_gh = gridH; m_covU = covU; m_covV = covV;
        // grade: bloco (gated), bloco2 (dilatado), runEst (persistente), stats
        if (!CriarTex11(gridW, gridH, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, false, &m_bloco, nullptr, erro, L"bloco")) return false;
        if (!CriarTex11(gridW, gridH, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, false, &m_bloco2, nullptr, erro, L"bloco2")) return false;
        if (!CriarTex11(gridW, gridH, DXGI_FORMAT_R32_UINT, D3D11_BIND_UNORDERED_ACCESS, false, &m_run, nullptr, erro, L"runEst")) return false;
        // O2-QUALIDADE: desoclusao por bloco (ver PassoA e o passo de aplicacao)
        if (!CriarTex11(gridW, gridH, FmtEscala(), D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, false, &m_desoc, nullptr, erro, L"desoc")) return false;
        m_dev->CreateUnorderedAccessView(m_desoc, nullptr, &m_desocUav); m_dev->CreateShaderResourceView(m_desoc, nullptr, &m_desocSrv);
        m_dev->CreateUnorderedAccessView(m_bloco, nullptr, &m_blocoUav);  m_dev->CreateShaderResourceView(m_bloco, nullptr, &m_blocoSrv);
        m_dev->CreateUnorderedAccessView(m_bloco2, nullptr, &m_bloco2Uav); m_dev->CreateShaderResourceView(m_bloco2, nullptr, &m_bloco2Srv);
        m_dev->CreateUnorderedAccessView(m_run, nullptr, &m_runUav);
        {
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 64; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_stats))) { if (erro) *erro = L"fg: buffer de estatisticas"; return false; }
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = 16; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            if (FAILED(m_dev->CreateUnorderedAccessView(m_stats, &ud, &m_statsUav))) { if (erro) *erro = L"fg: UAV de estatisticas"; return false; }
            bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.MiscFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            for (int i = 0; i < kSlots; i++) if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_statsStg[i]))) { if (erro) *erro = L"fg: staging de estatisticas"; return false; }
            bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; bd.ByteWidth = 64;
            if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_cb))) { if (erro) *erro = L"fg: constant buffer"; return false; }
        }
        if (!CriarShaders(erro)) return false;
        m_medVme.Iniciar(m_dev);
        return true;
    }
public:

    /* Thread do LACO (device da rede): o campo do VME que vale para o quadro n. Chame ANTES de
       mandar o quadro n pela ponte, para o apresentador nunca esperar por uma sinalizacao que
       ainda nao foi enfileirada. */
    void PublicarCampo(ID3D11DeviceContext* ctxRede, ID3D11ShaderResourceView* srvCampo, uint64_t n) {
        if (!m_campoOk || !ctxRede || !srvCampo || n == 0 || n == m_campoN.load()) return;
        ID3D11Resource* r = nullptr; srvCampo->GetResource(&r);
        if (!r) return;
        ctxRede->CopyResource(m_campoRede[n % 3], r);
        r->Release();
        ID3D11DeviceContext4* c4 = nullptr;
        if (SUCCEEDED(ctxRede->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&c4)) && c4) { c4->Signal(m_fCampoRede, n); c4->Release(); }
        /* OCLUSAO-2309 §8: o Signal do D3D11 fica no lote do contexto ate alguem dar Flush. Quem
           dava era o Enviar da ponte, logo depois - MAS o Enviar sai sem Flush quando o anel esta
           cheio (quadro descartado), e ai o m_campoN ja dizia "publicado" com o Signal ainda na
           CPU. O apresentador enfileira `c4->Wait(m_fCampoApres, n)` NA GPU por esse numero: uma
           espera de GPU por um sinal que nao foi submetido so se resolve no proximo Flush/Present
           da rede. Nao e deadlock, mas e uma fila de GPU parada esperando outra thread - o tipo de
           dependencia que nao pode existir num laco que ja esta perto do watchdog. O Flush aqui
           custa uma submissao a mais, e a do Enviar logo a seguir sai vazia. */
        ctxRede->Flush();
        m_campoN.store(n, std::memory_order_release);
    }

    /* =====================================================================================
       J5 (18/09/2026): PROFUNDIDADE E VETORES DE MOVIMENTO REAIS DO JOGO, PELA PONTE.
       A DLL dentro do jogo copia depth e MV do upscaler para os slots da fase 2 e avisa com
       'D' (ponte_feeder_host.h, DadosJogo); o laco recebe pelo FontePonte::DadosDoQuadro e
       publica aqui, para o quadro n da ponte entre placas - o mesmo n com que o apresentador
       vai chamar Gerar. O caminho e o do campo do VME (PublicarCampo), com uma diferenca que
       importa: os slots do jogo sao REESCRITOS depois da out_fence deste quadro, e o FI so
       roda depois, noutra thread e noutro device. Por isso a publicacao COPIA depth e MV
       para um anel de 3 no device da rede (compartilhado com o D3D12 do FI, a direcao que
       o driver aceita: nasce no D3D11, abre no D3D12) e sinaliza uma fence; a copia entra
       no contexto imediato ANTES do Devolver, que e quem enfileira o Signal da out_fence.
       Quem le e o Gerar(n): Wait na fence, renderSize = resolucao de render, jitter e escala
       do jogo, depth e MV do anel. A cor continua em displaySize (o caso canonico da API:
       ffx_framegeneration.h, "renderSize - dilatedDepth, dilatedMotionVectors are expected
       to be of ths size").
       O anel de 3 e seguro por construcao: antes de escrever no slot i o laco espera, NA GPU
       da rede, a fence de saida do FI que leu esse slot pela ultima vez (m_dadosGerLido).
       ===================================================================================== */
    struct MetaJogo {
        bool valido = false; uint64_t n = 0;
        UINT renderW = 0, renderH = 0, mvW = 0, mvH = 0;
        float jitterX = 0, jitterY = 0, mvScaleX = 1, mvScaleY = 1, dtMs = 0;
        float camNear = 0, camFar = 0, camFov = 0, viewToM = 0;
        uint32_t flags = 0, origem = 0;   /* flags: kDados* do ponte_feeder_host.h (1 reset, 2 depth inv, 4 inv desconhecida, 8 mv tela, 16 mv jitter, 32 ndc) */
        int64_t qpcDispatch = 0, qpcChegada = 0;
    };
    /* Thread do LACO (device da rede), ANTES de mandar o quadro n pela ponte e ANTES do
       Devolver. `d` e o PonteHost::DadosJogo do quadro (duck-typed para nao amarrar este
       cabecalho ao da ponte). Falha aqui = quadro sem dado (fluxo optico), nunca erro. */
    template <class Meta>
    void PublicarDadosJogo(ID3D11Device* devRede, ID3D11DeviceContext* ctxRede, ID3D11Texture2D* depth, ID3D11Texture2D* mv,
                           const Meta& d, int64_t qpcChegada, uint64_t n) {
        if (!Pronto() || !devRede || !ctxRede || !depth || !mv || n == 0 || !d.valido) return;
        if (m_dadosDesistiu) return;
        D3D11_TEXTURE2D_DESC dd{}; depth->GetDesc(&dd);
        D3D11_TEXTURE2D_DESC md{}; mv->GetDesc(&md);
        if (dd.Format != DXGI_FORMAT_R32_FLOAT || md.Format != DXGI_FORMAT_R16G16_FLOAT) {
            m_dadosDesistiu = true;
            if (m_log) m_log("fg: depth/mv da ponte em formato inesperado (%u/%u) - o FI segue no fluxo optico", (unsigned)dd.Format, (unsigned)md.Format);
            return;
        }
        if (!m_dadosOk || m_dadosW != dd.Width || m_dadosH != dd.Height) {
            std::wstring erro;
            if (!LigarDadosJogo(devRede, dd.Width, dd.Height, &erro)) {
                m_dadosDesistiu = true;
                if (m_log) m_log("fg: %s - o FI segue no fluxo optico (sem profundidade/MV do jogo)", std::string(erro.begin(), erro.end()).c_str());
                return;
            }
        }
        if (d.renderW == 0 || d.renderH == 0 || d.renderW > dd.Width || d.renderH > dd.Height ||
            d.mvW == 0 || d.mvH == 0 || d.mvW > md.Width || d.mvH > md.Height) return;
        const int i = (int)(n % 3);
        ID3D11DeviceContext4* c4 = nullptr;
        if (FAILED(ctxRede->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&c4)) || !c4) return;
        /* o FI que leu este slot pela ultima vez ja terminou? (espera na GPU da rede; na pratica
           nunca espera: e o FI de 3 quadros atras) */
        const uint64_t lido = m_dadosGerLido[i].load(std::memory_order_acquire);
        if (lido != 0 && m_fOutRede) c4->Wait(m_fOutRede, lido);
        const D3D11_BOX bd = { 0, 0, 0, d.renderW, d.renderH, 1 };
        const D3D11_BOX bm = { 0, 0, 0, d.mvW, d.mvH, 1 };
        ctxRede->CopySubresourceRegion(m_dadosDepthRede[i], 0, 0, 0, 0, depth, 0, &bd);
        ctxRede->CopySubresourceRegion(m_dadosMvRede[i], 0, 0, 0, 0, mv, 0, &bm);
        c4->Signal(m_fDadosRede, n);
        c4->Release();
        MetaJogo m; m.valido = true; m.n = n;
        m.renderW = d.renderW; m.renderH = d.renderH; m.mvW = d.mvW; m.mvH = d.mvH;
        m.jitterX = d.jitterX; m.jitterY = d.jitterY; m.mvScaleX = d.mvScaleX; m.mvScaleY = d.mvScaleY; m.dtMs = d.dtMs;
        m.camNear = d.camNear; m.camFar = d.camFar; m.camFov = d.camFov; m.viewToM = d.viewToM;
        m.flags = d.flags; m.origem = d.origem; m.qpcDispatch = d.qpcDispatch; m.qpcChegada = qpcChegada;
        { std::lock_guard<std::mutex> g(m_muDados); m_dadosMeta[i] = m; }
        m_dadosN.store(n, std::memory_order_release);
        m_dadosPublicados++;
    }
    /* Para o card: o movimento vem do jogo? (algum G com dado real no ultimo segundo) */
    bool MovimentoDoJogo() const { const int64_t q = m_movJogoQpc.load(); return q != 0 && (Ms() - QpcMs(q)) < 1000.0; }
    uint64_t GeradosComJogo() const { return m_geradosJogo; }
    double IdadeDadosMs() const { return m_nIdade ? m_somaIdade / (double)m_nIdade : 0.0; }   /* dispatch do upscaler -> FI, media */

    bool Pronto() const { return m_contexto && m_dev != nullptr && !m_ctxSolto && !m_morto; }
    bool TemContexto() const { return m_contexto; }
    /* RESET-DRENO (23/09/2026): espera a fila do FI terminar o que ja foi mandado (Signal numa fence
       propria, que nao mexe na contagem de m_fIn/m_fOut). Chamado com o apresentador PAUSADO, entre
       dois quadros. Devolve os ms que levou; -1 = o prazo estourou (a placa nao devolveu). */
    double DrenarGpu(DWORD tetoMs) {
        if (!m_dev12 || !m_fila || m_morto) return 0.0;
        if (!m_fDreno && FAILED(m_dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&m_fDreno))) { m_fDreno = nullptr; return 0.0; }
        if (!m_evtDreno) m_evtDreno = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_evtDreno) return 0.0;
        LARGE_INTEGER a, b, f; QueryPerformanceCounter(&a); QueryPerformanceFrequency(&f);
        const uint64_t v = ++m_vDreno;
        if (FAILED(m_fila->Signal(m_fDreno, v))) return 0.0;
        bool ok = m_fDreno->GetCompletedValue() >= v;
        if (!ok) { ResetEvent(m_evtDreno); m_fDreno->SetEventOnCompletion(v, m_evtDreno); ok = WaitForSingleObject(m_evtDreno, tetoMs) == WAIT_OBJECT_0 || m_fDreno->GetCompletedValue() >= v; }
        QueryPerformanceCounter(&b);
        return ok ? 1000.0 * (double)(b.QuadPart - a.QuadPart) / (double)f.QuadPart : -1.0;
    }
    /* ======================= VRAM ACIMA DO ORCAMENTO (X-HUD-PISCA, 19/09/2026) =======================
       COD Vanguard do dono, fonte monitor: o dlss5-feed.log diz "vram: 2902 MB in use of a 2690 MB
       budget (108%) -- OVER BUDGET" e logo depois "the GPU did not retire allocator slot within
       2000 ms" -> device HUNG (0x887A0006) e 12 eventos nvlddmkm 153 no Windows. O jogo ocupa a placa e
       o Windows deixa ~2,7 GB para o nosso processo; a Interpolada soma ~400 MB (2x) a ~780 MB (3x com
       o 2o contexto) e o processo passa do orcamento -> o gerenciador de memoria de video pagina os
       nossos recursos a cada quadro -> Present de 1-2 s -> watchdog. As sessoes sem Interpolada do
       mesmo dia tem ZERO 153. O main sonda o orcamento (QueryVideoMemoryInfo) e, acima dele, o
       apresentador pausa por VRAM e chama SoltarContextoVram() na thread dele: o contexto do FI (288 MB)
       e o segundo (3x/4x) vao embora; texturas, fences e o VME ficam. Pronto() vira falso e so o quadro
       real sai. Com folga de volta, RestaurarContextoVram() refaz o contexto (0,3-0,8 s, uma vez). */
    bool ContextoSolto() const { return m_ctxSolto; }
    void SoltarContextoVram() {
        if (m_ctxSolto || !m_contexto) return;
        EsperarFila();
        if (m_fg) { m_Destroy(&m_fg, nullptr); m_fg = nullptr; }
        SoltarCtx3(); SoltarCtx2(); m_mult = 2; m_ctx2Quente = false;
        m_ctxSolto = true; m_frameId = 0;
        if (m_log) m_log("fg: contexto do FI SOLTO por memoria de video acima do orcamento (~%.0f MB devolvidos%s) - so o quadro real sai ate haver folga", m_memMB, m_multQuer.load() > 2 ? ", mais o 2o contexto do 3x/4x" : "");
    }
    bool RestaurarContextoVram() {
        if (!m_ctxSolto) return true;
        if (!m_contexto || !m_dev12 || m_morto) return false;
        if (m_dev12->GetDeviceRemovedReason() != S_OK) { m_morto = true; if (m_log) m_log("fg: o device do FI foi removido durante a pausa por VRAM - fluidez interpolada MORTA nesta sessao"); return false; }
        EsperarFila();   // X-TDR: contexto nasce com a fila em dia
        const double t0 = Ms();
        ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = m_dev12;
        ffxCreateContextDescFrameGeneration cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; cd.header.pNext = &be.header;
        cd.flags = m_ctxFlagsJogo; cd.displaySize = { m_w, m_h }; cd.maxRenderSize = { m_w, m_h }; cd.backBufferFormat = ffxApiGetSurfaceFormatDX12(m_fmt);
        ffxReturnCode_t rc = m_Create(&m_fg, &cd.header, nullptr);
        if ((rc != FFX_API_RETURN_OK || !m_fg) && m_ctxFlagsJogo != 0) { m_fg = nullptr; cd.flags = 0; m_ctxFlagsJogo = 0; rc = m_Create(&m_fg, &cd.header, nullptr); }
        if (rc != FFX_API_RETURN_OK || !m_fg) { m_fg = nullptr; m_contexto = false; m_morto = true; if (m_log) m_log("fg: o contexto do FI nao voltou depois da pausa por VRAM (%u) - fluidez interpolada MORTA nesta sessao", (unsigned)rc); return false; }
        ffxConfigureDescFrameGeneration cf{}; cf.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION; cf.frameGenerationEnabled = true; cf.allowAsyncWorkloads = g_async;
        cf.generationRect = { 0, 0, (int32_t)m_w, (int32_t)m_h }; cf.frameID = 0; m_Config(&m_fg, &cf.header);
        m_ctxSolto = false; m_frameId = 0;
        if (m_log) m_log("fg: contexto do FI refeito depois da pausa por VRAM em %.0f ms", Ms() - t0);
        return true;
    }
    /* O device D3D12 do FI foi removido (placa reiniciou / watchdog) ou a fila dele parou de responder
       (3 esperas de 2 s seguidas): o interpolador esta MORTO nesta sessao - o apresentador segue so com
       o quadro real e o painel diz por que. Nada aqui reabre o app: reabrir em laco era o que o dono via. */
    bool Morto() const { return m_morto; }
    /* ===== X-FONTE-SOLTA (20/09/2026): O PAR ANTERIOR AINDA ESTA NA GPU? =====
       Quem pergunta e a admissao do apresentador, ANTES de chamar Gerar(). `m_fSubmetida` e o
       ultimo valor de fato sinalizado na m_fOut (e nao m_ger, que o Alimentar() da pausa avanca
       sem submeter nada - era esse o engano que o X-TDR ja tinha corrigido na espera do alocador).
       Ler a fence custa uma leitura de memoria compartilhada: pode ser por chegada. */
    bool FiEmVoo() const { return m_fOut && m_fSubmetida != 0 && m_fOut->GetCompletedValue() < m_fSubmetida; }
    double FiEmVooMs() const { return (m_fSubmetidaMs > 0.0 && FiEmVoo()) ? Ms() - m_fSubmetidaMs : 0.0; }
    /* O teto do vigia da admissao, no mesmo papel do `gpu_timeout_ms` do feeder: quanto tempo a
       fence pode ficar sem sinalizar antes de se desconfiar do device. Na pratica quem manda e o
       4*T do apresentador (menor que isto em qualquer taxa normal); este e so o teto de cima. */
    static constexpr double TetoVigiaMs() { return kFiVigiaMs; }
    /* X-FONTE-SOLTA: o device do FI ainda responde? O Gerar() ja pergunta isto na entrada dele,
       mas com a admissao recusando o quadro ANTES do Gerar ninguem mais perguntaria - e um device
       removido ficaria "em voo" para sempre, com a admissao recusando tudo e a tela parada. */
    bool ConferirDevice() {
        if (m_morto) return false;
        if (!m_dev12) return true;
        const HRESULT hr = m_dev12->GetDeviceRemovedReason();
        if (hr != S_OK) {
            m_morto = true;
            if (m_log) m_log("fg: o device D3D12 do interpolador foi REMOVIDO (0x%08X) - visto pelo vigia da admissao; fluidez interpolada morta nesta sessao (o app nao reabre por isso)", (unsigned)hr);
            return false;
        }
        return true;
    }
    /* "Reportar quadro" (thread do laco): o proximo Gerar grava A, G e B em `pasta`. */
    void PedirRelato(const wchar_t* pasta) { wcsncpy(m_relatoPasta, pasta, MAX_PATH - 1); m_relatoPasta[MAX_PATH - 1] = 0; m_relatoPedido.store(true); }
    bool CampoLigado() const { return m_campoOk || m_campoTentou; }   // (tentou e falhou tambem conta: nao insiste todo quadro)
    double MemoriaMB() const { return m_memMB; }
    double FiMs() const { return m_nFi ? m_somaFi / m_nFi : 0.0; }
    double PassesMs() const { return m_medPasses.Media() + m_medCorte.Media() + m_medHud.Media(); }
    double CorteMs() const { return m_medCorte.Media(); }            // custo do detector de corte sem campo
    /* MASCARA DE HUD DEPOIS DO FI (X-HUD-PISCA, 19/09/2026) - ver CriarHud/PassoHud. */
    bool     HudLigado() const { return m_hudLigado && m_csHudAplica != nullptr; }
    double   HudFrac() const { return m_hudFrac; }                 // fracao dos pixels de G copiados de B (media movel)
    double   HudParadosFrac() const { return m_hudParados; }       // fracao dos pixels parados ha >= N quadros (medida, antes do teto)
    double   HudMaxFrac() const { return m_hudMaxFrac; }
    unsigned HudQuadros() const { return m_hudN; }
    double   HudMs() const { return m_medHud.Media(); }
    /* QUADRO REPETIDO (X-FG-QUALIDADE, 19/09/2026) - ver CriarDedup. Contadores da sessao; o
       apresentador zera os por segundo lendo a diferenca. */
    bool     DedupLigado() const { return m_dedupLigado && m_csDedup != nullptr; }
    uint64_t Repetidos() const { return m_repetidos; }
    uint64_t DedupDesconhecidos() const { return m_dedupDesconhecidos; }
    uint64_t DedupConferidos() const { return m_dedupN; }
    double   DedupEsperaMediaMs() const { return m_dedupN ? m_dedupEsperaAcc / (double)m_dedupN : 0.0; }
    double   DedupEsperaMaxMs() const { return m_dedupEsperaMax; }
    double   DedupUltFrac() const { return m_dedupUltFrac; }
    /* X-GERADO - a prova do gerado. Fracao de amostras em que o G FINAL difere do quadro real
       anterior (A) e do seguinte (B), acima do mesmo limiar do dedup. Negativo = a GPU ainda nao
       respondeu nenhuma vez. `distinto de B` em 0,0% quer dizer que o gerado E o quadro real. */
    double   ProvaDistA() const { return m_provaA; }
    double   ProvaDistB() const { return m_provaB; }
    double   ProvaDistMistura() const { return m_provaM; }
    // N2/N5 - para o painel e para quem quiser conferir de fora (o log de 5 s ja traz os dois)
    uint64_t CortesSad() const { return m_cortesSad; }
    bool     LetterboxAtivo() const { return m_lbAtivo; }
    /* N2 - o quadro `id` abriu uma cena nova? Mesmo contrato do QuadroRepetido: so responde true
       quando a resposta da GPU JA chegou e disse que sim. */
    bool     QuadroCorte(uint64_t id) const {
        const int v = (int)(id % kDedupVer);
        return m_dedupVerN[v] == id && m_dedupVerC[v] != 0;
    }
    void     DedupZerarJanela() { m_dedupEsperaAcc = 0.0; m_dedupN = 0; m_dedupEsperaMax = 0.0; }
    /* K-DEDUP (20/09/2026) - O GANCHO. "O quadro `id` era a mesma imagem do quadro real anterior?"
       true so quando a resposta da GPU JA CHEGOU e disse que sim; false enquanto nao chegou (nada
       se descarta em cima de duvida). O veredito do quadro n fica pronto na volta n+1.
       PONTO DE CHAMADA (apresentador_gpu.h, LacoInterpolado, logo depois de
       `ID3D11Texture2D* t = p->Receber(&n);` e ANTES do bloco de CAPACIDADE que solta pendentes):
           if (m_fila > 0 && m_pendI < m_pendN && fg->QuadroRepetido(m_ultPar)) {
               while (const Pend* pd = PendProximo()) {      // o par do quadro repetido nao vai a tela
                   if (pd->par != m_ultPar) break;
                   p->...                                    // nada a liberar: o Pend e do FI
                   m_pendI++; m_repetidos++; m_repetidosTotal++; m_ultRepetidoMs = agora;
               }
               if (!PendProximo()) PendLimpar();
           }
       com `m_ultPar` = o `n` da volta anterior. Por que isso basta: quando o quadro n e repetido,
       o par (G, B) que o FI produziu para ele e G == B == a imagem que JA esta na tela; tira-lo da
       fila economiza dois presents da mesma imagem e devolve o ritmo (era o `25/8/25/8 ms` do
       LS-RITMO 6.1). O par do quadro n+1 continua certo sem nenhuma correcao: o slot `prev` dele
       guarda B(n), que e identico a B(n-1). Ver tambem relatorios\K-DEDUP.md. */
    bool     QuadroRepetido(uint64_t id) const { return DedupVeredito(id) == 1; }
    int      VereditoQuadro(uint64_t id) const { return DedupVeredito(id); }
    double   DedupRespostaMs() const { return m_dedupRespN ? m_dedupRespAcc / (double)m_dedupRespN : 0.0; }
    double   DedupAtrasoQuadros() const { return m_dedupRespN ? m_dedupAtrasoAcc / (double)m_dedupRespN : 0.0; }
    uint64_t CortesSemCampo() const { return m_cortesSemCampo; }
    double CorteFrac() const { return m_corteFrac; }
    uint64_t Gerados() const { return m_gerados; }
    uint64_t Cortes() const { return m_cortes; }
    uint64_t Pans() const { return m_pans; }
    uint64_t Mv0s() const { return m_mv0s; }
    uint64_t SemCampo() const { return m_semCampo; }

    /* Thread do APRESENTADOR: gera G entre o quadro anterior e `B` (quadro n da ponte).
       Devolve false se ainda nao ha quadro anterior (primeiro quadro) - o chamador mostra B. */
    /* EM PAUSA: guarda o quadro e NAO DESPACHA NADA (nem o FI, nem os passes, nem o VME).
       ------------------------------------------------------------------------------------
       Ate 13/09/2026 o apresentador continuava chamando o Gerar mesmo pausado, "para o
       historico do FI ficar quente". MEDIDO: isso faz a pausa custar quase tanto quanto o FG
       ligado - na receita "60 fps com qualidade" o gate pausava e a cadeia SEGUIA em 26,18 ms
       (contra 17,22 com a fluidez desligada), ou seja, a pessoa pagava o interpolador inteiro
       e nao recebia um unico quadro gerado. Era metade da razao de a linha E da TABELA.txt
       entregar 38,6 apresentados contra 58,1 do FG desligado.
       O preco de nao alimentar: ao voltar, o primeiro despacho vai com `reset` (m_frameId=0) e
       o Optical Flow do SDK ignora os ~10 primeiros quadros - cerca de 0,25 s de quadros
       gerados fracos UMA VEZ por retomada, contra ~5,5 ms POR QUADRO enquanto durar a pausa.
       A copia de B continua (A e B tem de seguir andando, senao a retomada interpola sobre um
       quadro velho de segundos atras). */
    bool Alimentar(ID3D11DeviceContext* ctx, ID3D11Texture2D* B) {
        if (!Pronto() || !ctx || !B) return false;
        const int cur = (int)(m_ger % kSlots);
        ctx->CopyResource(m_inB[cur], B);
        m_ger++;
        m_frameId = 0;   // a proxima geracao vai com dd.reset = true
        m_ctx2Quente = false;
        return true;
    }
    /* X-FG-6 (18/09/2026): MAIS DE UM QUADRO POR INTERVALO (3x / 4x). O provedor FidelityFX so
       escreve outputs[0] e sempre na fase 0,5 (X-FG-VIDEO §3: numGeneratedFrames e ignorado). Entao
       o que existe e o PONTO MEDIO de dois quadros, e mais de um quadro sai por chamadas
       encadeadas: G1/2 = FI(A, B) como sempre; G1/4 = FI(A, G1/2); G3/4 = FI(G1/2, B).
       O FI guarda o quadro anterior por contexto, e por isso sao DOIS contextos: o primeiro ve a
       sequencia real (A, B, B', ...) e da G1/2; o segundo ve (A, G1/2, B, G1/2', B', ...) - o seu
       "anterior" ja e o A certo quando G1/2 chega, e ja e o B certo quando o proximo G1/2' chega.
       Custo: 3 despachos do FI por intervalo em vez de 1 (os dois contextos vivem na mesma lista e
       nas mesmas fences; FiMs() mede o total). O segundo contexto nasce na primeira vez que se pede
       (0,3-0,8 s, um engasgo, uma vez) e morre quando se volta a 2x.
       3x: nao ha tercos com pontos medios (so fracoes diadicas). Vai G1/4 em 1/3 e G3/4 em 2/3 -
       cada um a 1/12 do intervalo da posicao ideal (1,4 ms a 60 fps de chegada), custando o mesmo
       que o 4x; e o que da para fazer com este provedor, e fica registrado. */
    /* META (21/09/2026): 8x = sete gerados por intervalo, pelo 3o contexto (GarantirCtx3). 5..7
       nao existem como cadeia diadica: quem pede acima de 4 recebe 8 e escolhe as fracoes. */
    void Multiplicador(int m) { m_multQuer.store(m < 2 ? 2 : m > 4 ? 8 : m); }
    int  Multiplicador() const { return m_multQuer.load(); }
    /* RODADA8 (fg_saida=2x): o dedup POR AMOSTRAGEM (CriarDedup) pode ser desligado pelo apresentador - no modo 2x so vale o
       dedup EXATO antes da rede (main.cpp Quadros5); um "repetido" por amostragem aqui seria um quadro real do jogo fora. */
    void DedupPermitir(bool v) { m_dedupPermitido.store(v); }
    /* RODADA8: o indice de geracao que o PROXIMO Gerar/Alimentar vai usar (o slot e ProximoGer() % kSlots). Um par devolvido
       por um Gerar feito com ProximoGer() == g fica valido ate o Gerar feito com ProximoGer() == g + kSlots. */
    uint64_t ProximoGer() const { return m_ger; }
    uint64_t GeradosExtra() const { return m_geradosExtra; }
    bool Gerar(ID3D11DeviceContext* ctx, ID3D11Texture2D* B, uint64_t n, float intervaloMs, Resultado* r) {
        *r = Resultado{};
        // X-HUD-PISCA: device do FI removido -> morto, sem tentar despachar (cada tentativa era uma
        // espera de 2 s na thread do apresentador: 1 present a cada 2 s, o que o dono viu as 12:14)
        if (!m_morto && m_dev12) {
            const HRESULT hr = m_dev12->GetDeviceRemovedReason();
            if (hr != S_OK) { m_morto = true; if (m_log) m_log("fg: o device D3D12 do interpolador foi REMOVIDO (0x%08X) - fluidez interpolada morta nesta sessao; so o quadro real sai (o app nao reabre por isso)", (unsigned)hr); }
        }
        if (!Pronto() || !ctx || !B) return false;
        ID3D11DeviceContext4* c4 = nullptr;
        if (FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&c4)) || !c4) return false;
        /* X-FG-6: o multiplicador pedido passa a valer neste quadro; o 2o contexto nasce ou morre
           aqui, fora de qualquer lista aberta (e com a fila em dia ao morrer). */
        {
            int quer = m_multQuer.load();
            if (quer != m_mult) {
                if (quer > 2 && !m_fg2 && !GarantirCtx2()) { quer = 2; m_multQuer.store(2); }
                if (quer > 4 && !m_fg3 && !GarantirCtx3()) { quer = 4; m_multQuer.store(4); }   /* META: o 3o contexto */
                /* w-153 (21/09): o 3o contexto NAO morre ao voltar a 4x - fica vivo enquanto a fluidez estiver
                   ligada (Parar/VRAM). Cada ida e volta custava criar/destruir o contexto (0,3-0,8 s) e foi
                   isso que a oscilacao 8x <-> 4x do arranque transformou em nove engasgos em 20 s. */
                if (quer <= 2 && m_fg2) { EsperarFila(); SoltarCtx3(); SoltarCtx2(); }
                m_mult = m_fg2 ? quer : 2;
                if (m_log) m_log("fg: multiplicador %dx (%d quadro(s) gerado(s) por intervalo)", m_mult, m_mult - 1);
            }
        }
        EnterrarVelhas(false);   // W-153: os gerados extras aposentados ha kSlots geracoes ja sairam da fila do apresentador
        const int cur = (int)(m_ger % kSlots), prev = (cur + kSlots - 1) % kSlots;
        ctx->CopyResource(m_inB[cur], B);
        r->B = m_inB[cur];
        if (m_reduz) {   // o FI (e o VME local) leem a versao reduzida
            ctx->CSSetShader(m_csDown, nullptr, 0);
            ctx->CSSetConstantBuffers(0, 1, &m_cbDown);
            ctx->CSSetSamplers(0, 1, &m_samp);
            ctx->CSSetShaderResources(0, 1, &m_inBSrv[cur]);
            ID3D11UnorderedAccessView* uavFi = nullptr; m_dev->CreateUnorderedAccessView(m_inBFi[cur], nullptr, &uavFi);
            ctx->CSSetUnorderedAccessViews(0, 1, &uavFi, nullptr);
            ctx->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
            ID3D11UnorderedAccessView* nu = nullptr; ID3D11ShaderResourceView* ns = nullptr; ID3D11SamplerState* nsm = nullptr; ID3D11Buffer* nb = nullptr;
            ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr); ctx->CSSetShaderResources(0, 1, &ns); ctx->CSSetSamplers(0, 1, &nsm); ctx->CSSetConstantBuffers(0, 1, &nb); ctx->CSSetShader(nullptr, nullptr, 0);
            Soltar(uavFi);
        }
        if (m_ger == 0) { m_ger++; c4->Release(); return false; }   // sem A ainda

        /* ---- X-FG-QUALIDADE: B igual a A por conteudo? Entao a captura entregou o mesmo quadro do
           jogo de novo (ver CriarDedup). Nada e despachado e m_ger NAO avanca: o proximo B cai neste
           mesmo slot e o par do FI continua sendo (A, o proximo distinto). O chamador ve r->repetido
           e nao apresenta nada. Com o caminho J5 (dados do jogo) nao roda: la cada quadro e do jogo. */
        if (m_dedupLigado && m_dedupPermitido.load(std::memory_order_relaxed) && m_csDedup && !m_dadosOk) {
            const double tCpu = Ms();
            DedupColher(ctx, n);              // vereditos das voltas anteriores (nunca espera)
            DedupDespachar(ctx, cur, prev, n);  // o deste quadro: resposta na proxima volta
            const double gasto = Ms() - tCpu;
            m_dedupEsperaAcc += gasto; if (gasto > m_dedupEsperaMax) m_dedupEsperaMax = gasto;
            DedupLog5s(n);
            /* Se a resposta DESTE quadro ja estiver na mao (GPU folgada, ou o apresentador tendo
               despachado antes), o caminho de sempre vale: nada e despachado, m_ger nao avanca e o
               chamador nao apresenta nada. Em jogo pesado a resposta chega uma volta depois - e ai
               quem descarta e o apresentador, pela fila (ver QuadroRepetido). */
            if (DedupVeredito(n) == 1) { r->repetido = true; c4->Release(); return false; }
        }

        // ---- J5: o pacote do jogo DESTE quadro (profundidade e MV reais pela ponte), ou nada.
        // Com ele, o VME e os passes nao rodam (o campo vem pronto do jogo) e o detector de corte
        // sem campo continua de guarda. As flags do material entram no contexto (recriado uma
        // vez; ver RecriarContexto); pacote que nao case com o contexto atual e recusado.
        MetaJogo mj; bool usaJogo = PegarDadosJogo(n, &mj);
        if (usaJogo) {
            const uint32_t flagsQuer = FlagsDoMaterial(mj);
            if (flagsQuer != m_ctxFlagsJogo && !RecriarContexto(flagsQuer)) usaJogo = false;
            if (!m_contexto) { c4->Release(); return false; }
        }
        if (usaJogo && !m_dadosLogou) {
            m_dadosLogou = true;
            if (m_log) {
                m_log("fg: movimento do jogo pela ponte (origem %s) render=%ux%u mv=%ux%u%s mvscale=%.4f,%.4f jitter=%.4f,%.4f depth_inv=%d mv_com_jitter=%d camera=%s | cor em %ux%u",
                      mj.origem == 1 ? "dlss" : mj.origem == 2 ? "fsr31" : mj.origem == 3 ? "fsr2" : mj.origem == 4 ? "xess" : "?",
                      mj.renderW, mj.renderH, mj.mvW, mj.mvH, (mj.flags & 8u) ? " (tela)" : " (render)", mj.mvScaleX, mj.mvScaleY, mj.jitterX, mj.jitterY,
                      (mj.flags & 2u) ? 1 : 0, (mj.flags & 16u) ? 1 : 0, mj.camNear > 0 ? "do jogo" : "suposta (0,1/1000/1rad)", m_w, m_h);
                m_log("fg: convencao usada: MV em pixels de render depois de mvscale, +x direita, +y baixo, do quadro atual para o anterior (a mesma do J3 dentro do jogo; SUPOSICAO ate ser vista em jogo)");
            }
        }

        // ---- o campo por pixel (passes A, A2, B) no device do apresentador
        // LOCAL: o VME roda aqui mesmo sobre B (backward B -> A, a convencao do FI); REMOTO: espera
        // a fence do campo publicado pela rede para o quadro n. J5: com o campo do jogo, nenhum.
        const bool temCampo = !usaJogo && m_campoOk && (m_campoLocal || m_campoN.load(std::memory_order_acquire) >= n);
        if (temCampo) {
            if (m_campoLocal) {
                m_medVme.Antes(ctx, cur); m_me.Update(ctx, m_inBFiSrv[cur]); m_medVme.Depois(ctx, cur);
                // REGRA DO CRITICO (12/09/2026): VME local acima de 3 ms na placa do jogo nao se paga
                // -> MV=0 no FI (so Optical Flow; em pan real MV=0 deu 33 dB >= copiar 30,8).
                if (m_ger == 90 && m_medVme.Media() > 3.0) {
                    m_campoLocal = false; m_campoOk = false; m_mvZerado = false;
                    if (m_log) m_log("fg: o VME local custa %.2f ms por quadro (> 3 ms) - desligado; o FI segue com MV=0 (so Optical Flow)", m_medVme.Media());
                }
            }
            else c4->Wait(m_fCampoApres, n);
            m_medPasses.Antes(ctx, cur);
            const UINT zero[4] = { 0, 0, 0, 0 };
            ctx->ClearUnorderedAccessViewUint(m_statsUav, zero);
            {
                D3D11_MAPPED_SUBRESOURCE m{};
                if (SUCCEEDED(ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                    Constantes c{};
                    c.W = m_w; c.H = m_h; c.GW = m_gw; c.GH = m_gh;
                    c.bw = (float)m_w * m_covU / m_gw; c.bh = (float)m_h * m_covV / m_gh;
                    // FG-QUALIDADE2: com DLSS5_FG_MV_FI=0 o PassoB escreve zero - zera so o que vai ao FI
                    c.movFracPrev = m_movFracPrev; c.mv0 = (m_mv0Prox || !m_mvParaFi) ? 1u : 0u; c.pan = m_panPrev ? 1u : 0u;
                    c.desocDisc = m_desocDisc; c.desocOn = (m_desocLigado && m_desocUav) ? 1u : 0u;   // O2-QUALIDADE
                    c.incLim = m_incLim; c.incEsc = m_incEsc; c.incPeso = m_incPeso;   // FG-QUALIDADE2
                    c.ambigAntigo = m_ambigAntigo ? 1u : 0u;
                    memcpy(m.pData, &c, sizeof(c)); ctx->Unmap(m_cb, 0);
                }
            }
            ID3D11ShaderResourceView* srvA[3] = { m_inBFiSrv[prev], m_inBFiSrv[cur], m_campoLocal ? m_me.MotionSRV() : m_campoSrv[n % 3] };
            ID3D11UnorderedAccessView* uavA[4] = { m_blocoUav, m_runUav, m_statsUav, m_desocUav };   // O2-QUALIDADE: u3 = desoclusao por bloco
            ctx->CSSetConstantBuffers(0, 1, &m_cb);
            ctx->CSSetShader(m_csA, nullptr, 0);
            ctx->CSSetShaderResources(0, 3, srvA);
            ctx->CSSetUnorderedAccessViews(0, 4, uavA, nullptr);
            ctx->Dispatch((m_gw + 7) / 8, (m_gh + 7) / 8, 1);
            ID3D11UnorderedAccessView* nu3[4] = {}; ID3D11ShaderResourceView* ns3[3] = {};
            ctx->CSSetUnorderedAccessViews(0, 4, nu3, nullptr); ctx->CSSetShaderResources(0, 3, ns3);
            // A2: dilatacao dos vetores grandes
            ctx->CSSetShader(m_csA2, nullptr, 0);
            ctx->CSSetShaderResources(0, 1, &m_blocoSrv);
            ctx->CSSetUnorderedAccessViews(0, 1, &m_bloco2Uav, nullptr);
            ctx->Dispatch((m_gw + 7) / 8, (m_gh + 7) / 8, 1);
            ctx->CSSetUnorderedAccessViews(0, 1, nu3, nullptr); ctx->CSSetShaderResources(0, 1, ns3);
            // B: por pixel
            ctx->CSSetShader(m_csB, nullptr, 0);
            ctx->CSSetShaderResources(0, 1, &m_bloco2Srv);
            ctx->CSSetUnorderedAccessViews(0, 1, &m_mvUav, nullptr);
            ctx->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
            ctx->CSSetUnorderedAccessViews(0, 1, nu3, nullptr); ctx->CSSetShaderResources(0, 1, ns3);
            ID3D11Buffer* nb = nullptr; ctx->CSSetConstantBuffers(0, 1, &nb);
            ctx->CSSetShader(nullptr, nullptr, 0);
            ctx->CopyResource(m_statsStg[cur], m_stats);
            m_statsPend[cur] = true;
            m_medPasses.Depois(ctx, cur);
        } else {
            // sem campo: MV = 0 (so o Optical Flow do SDK). Limpa a textura uma vez.
            // J5: com o campo do jogo a textura do VME nem entra no FI; o contador "sem campo" nao conta.
            if (!m_mvZerado) { const float z[4] = { 0, 0, 0, 0 }; ctx->ClearUnorderedAccessViewFloat(m_mvUav, z); m_mvZerado = true; }
            if (!usaJogo) m_semCampo++;
            /* POR QUE OS GATES NAO RODAM AQUI - nao e bug, nao reabrir (13/09/2026, item 4).
               Os gates de VARIANCIA e de AMBIGUIDADE e a MASCARA DE HUD moram todos dentro do
               PassoA, e o que eles fazem e UM: decidir, bloco a bloco, se o vetor que o VME
               devolveu vale ou deve ser ZERADO. Sem campo de MV nao ha vetor nenhum para gatear
               - a textura ja esta zerada acima -, entao rodar os tres aqui seria gastar passe
               de 4K para escrever zero onde ja ha zero. E consequencia da estrutura, nao falta
               de implementacao. Medida que confirma pelo outro lado: a mascara de HUD aplicada
               DEPOIS do FI rende 0,00 dB em 4K nativo na varredura de 60 combinacoes de limiares
               (D:\claude-scratch\qualidade\RESULTADOS.txt, 13/09).
               DOIS EFEITOS COLATERAIS conhecidos e aceitos deste caminho:
                 - r.nBlocos/nMov/nDiffBig ficam em 0, entao a PAUSA DE "MENU" do apresentador
                   (apresentador_gpu.h, LacoInterpolado) nunca dispara em duas placas. Aceito:
                   em MV=0 o quadro gerado no menu ainda bate repetir o quadro em +2,42 dB (§3.3b),
                   ou seja, a pausa protegia contra um problema que o caminho VME tinha e este nao.
                 - r.corte tambem fica em 0; quem detecta corte aqui e o passo abaixo, 100% na GPU.
               O UNICO dos blocos inertes que custava dB de verdade era o corte (-11,55 dB no
               quadro do corte) - e e o que roda logo abaixo. */
            // ... mas o CORTE DE CENA continua valendo aqui: e o unico dos tres blocos inertes em
            // MV=0 que custa dB de verdade (-11,55 no quadro do corte). Um passo so, de media de
            // luma por bloco - nem gate nem mascara, que em 4K rendem 0,00 dB (RESULTADOS.txt).
            if (m_corteLigado && m_csCorte) {
                m_medCorte.Antes(ctx, cur);
                // LIMIARES POR TAXA DE CHEGADA (ver kCorteSaltoMin e vizinhos): a base cresce com o
                // espacamento entre quadros, o corte nao. Sem isto o detector fica cego abaixo de
                // 15 fps de chegada - medido. `intervaloMs` e o intervalo real entre quadros que
                // CHEGAM (o mesmo que alimenta o frameTimeDelta do FI).
                if (!m_corteSaltoFixo) {
                    const double dt = std::min(200.0, std::max(8.0, (double)intervaloMs));
                    const double s  = std::min((double)kCorteSalto,     std::max(m_corteSaltoMin,     kCorteSalto     - kCorteSaltoPorMs     * (dt - kCorteDtRef)));
                    const double sa = std::min((double)m_corteSaltoAltaBase, std::max(m_corteSaltoAltaMin, m_corteSaltoAltaBase - kCorteSaltoAltaPorMs * (dt - kCorteDtRef)));
                    if (fabs(s - m_corteCc.salto) > 1e-4 || fabs(sa - m_corteCc.saltoAlta) > 1e-4) {
                        m_corteCc.salto = (float)s; m_corteCc.saltoAlta = (float)sa;
                        m_corteSalto = s; m_corteSaltoAltaAtual = sa;
                    }
                }
                {   // o cb do corte e DYNAMIC: reescrito por quadro (16 bytes de write, sem custo medivel)
                    D3D11_MAPPED_SUBRESOURCE mc{};
                    if (SUCCEEDED(ctx->Map(m_cbCorte, 0, D3D11_MAP_WRITE_DISCARD, 0, &mc))) { memcpy(mc.pData, &m_corteCc, sizeof(m_corteCc)); ctx->Unmap(m_cbCorte, 0); }
                }
                const UINT zero[4] = { 0, 0, 0, 0 };
                ctx->ClearUnorderedAccessViewUint(m_corteUav, zero);
                ID3D11ShaderResourceView* srvC[2] = { m_inBFiSrv[prev], m_inBFiSrv[cur] };
                ID3D11UnorderedAccessView* uavC[4] = { m_corteUav, m_corteEstUav, m_corteArgsUav, m_corteMedUav };
                ctx->CSSetConstantBuffers(0, 1, &m_cbCorte);
                ctx->CSSetShader(m_csCorte, nullptr, 0);
                ctx->CSSetShaderResources(0, 2, srvC);
                ctx->CSSetUnorderedAccessViews(0, 4, uavC, nullptr);
                ctx->Dispatch((m_cgw + 7) / 8, (m_cgh + 7) / 8, 1);
                ctx->CSSetShader(m_csCorteDec, nullptr, 0);   // uma thread: media movel + decisao + args do indirect
                ctx->Dispatch(1, 1, 1);
                ID3D11UnorderedAccessView* nu3[4] = {}; ID3D11ShaderResourceView* ns2[2] = {}; ID3D11Buffer* nb = nullptr;
                ctx->CSSetUnorderedAccessViews(0, 4, nu3, nullptr); ctx->CSSetShaderResources(0, 2, ns2);
                ctx->CSSetConstantBuffers(0, 1, &nb); ctx->CSSetShader(nullptr, nullptr, 0);
                if (m_corteTelem) {   // so para o log; lido sem esperar (DLSS5_FG_CORTE_TELEM=0 tira da conta)
                    ctx->CopyResource(m_corteStg[cur], m_corteEst);
                    m_cortePend[cur] = true;
                }
                m_medCorte.Depois(ctx, cur);
            }
        }
        /* N5 - AS BARRAS SAEM DO QUADRO REAL. Com o generationRect menor que a textura, o FI nao
           escreve fora dele e o que sobrasse ali seria o lixo do par anterior naquele slot. A
           barra, por definicao, e PRETA nos dois quadros - entao basta zerar o G antes: um
           ClearUnorderedAccessView, sem copia de textura inteira e sem barreira nenhuma no lado
           do D3D12. Roda antes do Signal, que e o que solta a lista do interpolador.
           DESVIO ANOTADO: uma barra que nao seja exatamente preta (2 ou 3 niveis) sai preta no
           quadro GERADO e certa no real. Abaixo do limiar de percepcao e so nas barras. */
        if (m_lbAtivo) {
            const float preto[4] = { 0, 0, 0, 1 };
            if (m_outGUav[cur]) ctx->ClearUnorderedAccessViewFloat(m_outGUav[cur], preto);
            if (m_mult > 2 && m_outG14Uav[cur] && m_outG34Uav[cur]) {
                ctx->ClearUnorderedAccessViewFloat(m_outG14Uav[cur], preto);
                ctx->ClearUnorderedAccessViewFloat(m_outG34Uav[cur], preto);
            }
            if (m_mult > 4) for (int i = 0; i < 4; i++) if (m_outG8Uav[i][cur]) ctx->ClearUnorderedAccessViewFloat(m_outG8Uav[i][cur], preto);   /* META */
        }
        c4->Signal(m_fIn11, n);
        ctx->Flush();

        // ---- D3D12: PREPARE + FG manual
        /* O ALOCADOR DESTE SLOT (X-TDR, 19/09/2026). Espera-se a fence da ULTIMA SUBMISSAO que usou o
           alocador `cur` (m_alocFence[cur]) - e nao "m_ger - 2": o Alimentar() da pausa do gate avanca
           m_ger SEM submeter nada, e a conta antiga esperava 2 s por um valor de fence que nunca seria
           sinalizado, A CADA RETOMADA do FG (com a regra "3 esperas = morto" do X-HUD-PISCA, tres
           retomadas matariam o interpolador). Teto de kFiEsperaMs: a placa atrasada (paginando por VRAM
           acima do orcamento, ou ja reiniciada pelo watchdog) nao prende a thread de ritmo - o quadro
           sai REAL, o FI recomeca com reset quando alcancar, e so um atraso continuo de kFiMortoMs (ou
           o device removido) da o interpolador como MORTO nesta sessao. */
        if (m_alocFence[cur] != 0 && m_fOut->GetCompletedValue() < m_alocFence[cur]) {
            EsperarFence(m_fOut, m_alocFence[cur], kFiEsperaMs);
            if (m_fOut->GetCompletedValue() < m_alocFence[cur]) {
                const double t = Ms();
                m_fiAtrasos++;
                if (m_fiAtrasoDesde == 0.0) {
                    m_fiAtrasoDesde = t;
                    if (m_log && m_fiAtrasosLog < 5) { m_fiAtrasosLog++; m_log("fg: a fila do FI nao terminou o quadro %llu em %u ms (placa atrasada) - quadro real no lugar do gerado ate ela alcancar (morre se durar %.0f s)", (unsigned long long)m_alocFence[cur], (unsigned)kFiEsperaMs, kFiMortoMs / 1000.0); }
                }
                const HRESULT rem = m_dev12 ? m_dev12->GetDeviceRemovedReason() : S_OK;
                if (rem != S_OK || t - m_fiAtrasoDesde >= kFiMortoMs) {
                    m_morto = true;
                    if (m_log) m_log("fg: a fila do FI parou de responder (%s; %.1f s sem terminar o quadro %llu) - fluidez interpolada MORTA nesta sessao; so o quadro real sai (o app nao reabre por isso)",
                                     rem != S_OK ? "device removido" : "sem progresso", (t - m_fiAtrasoDesde) / 1000.0, (unsigned long long)m_alocFence[cur]);
                }
                m_frameId = 0; m_ctx2Quente = false; m_ctx3Quente = false;   // ao voltar, o FI recomeca (A ficou velho)
                c4->Release(); return false;
            }
        }
        if (m_fiAtrasoDesde != 0.0) {
            if (m_log && m_fiAtrasosLog <= 5) m_log("fg: a fila do FI alcancou depois de %.0f ms (%llu quadros sairam reais)", Ms() - m_fiAtrasoDesde, (unsigned long long)m_fiAtrasos);
            m_fiAtrasoDesde = 0.0; m_fiAtrasos = 0;
        }
        ID3D12CommandAllocator* al = m_alloc[cur];
        al->Reset(); m_lista->Reset(al, nullptr);
        // J5: depth e MV DESTE quadro - do anel do jogo (slot n%3) ou os de sempre (1,0 constante e MV=0)
        const int sj = (int)(n % 3);
        ID3D12Resource* texDepth = usaJogo ? m_dadosDepth12[sj] : m_depth;
        ID3D12Resource* texMv    = usaJogo ? m_dadosMv12[sj]    : m_mv12;
        {
            D3D12_RESOURCE_BARRIER b[4] = {
                Bar(m_inBFi12[cur], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                Bar(texMv, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                Bar(m_outG12[cur], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                Bar(texDepth, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) };
            m_lista->ResourceBarrier(usaJogo ? 4 : 3, b);   // a depth constante ja vive em NON_PIXEL_SHADER_RESOURCE (CriarContexto)
        }
        LerTimestamp(cur);
        m_lista->EndQuery(m_qh, D3D12_QUERY_TYPE_TIMESTAMP, cur * 2);
        ffxDispatchDescFrameGenerationPrepareCameraInfo cam{}; cam.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
        cam.cameraUp[1] = 1; cam.cameraRight[0] = 1; cam.cameraForward[2] = 1;
        ffxDispatchDescFrameGenerationPrepare pp{}; pp.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE; pp.header.pNext = &cam.header;
        pp.frameID = m_frameId; pp.commandList = m_lista;
        /* J5: renderSize e o tamanho de depth e MV, NAO o da cor (ffx_framegeneration.h:127); a cor
           segue em displaySize no dispatch de baixo. jitter e escala vem do upscaler do jogo sem
           conversao (os dois structs da mesma API tem os mesmos campos: ffx_upscale.h:93-94 e
           ffx_framegeneration.h:128-129). Sem pacote, o de sempre: renderSize = tela, jitter 0, escala 1. */
        pp.renderSize = usaJogo ? FfxApiDimensions2D{ mj.renderW, mj.renderH } : FfxApiDimensions2D{ m_w, m_h };
        pp.jitterOffset = usaJogo ? FfxApiFloatCoords2D{ mj.jitterX, mj.jitterY } : FfxApiFloatCoords2D{ 0, 0 };
        pp.motionVectorScale = usaJogo ? FfxApiFloatCoords2D{ mj.mvScaleX, mj.mvScaleY } : FfxApiFloatCoords2D{ 1, 1 };
        pp.frameTimeDelta = (intervaloMs > 8.0f && intervaloMs < 50.0f) ? intervaloMs : (intervaloMs >= 50.0f ? 50.0f : 16.667f);   // limitado: um intervalo inflado por quadros perdidos nao pode virar "pausa" para o SDK
        pp.unused_reset = (m_frameId == 0);   // (o comentario de cima engolia esta linha ate 18/09/2026; o SDK declara o campo como nao usado, entao nada mudava)
        // camera: o FSR 2/3.1 do jogo carrega near/far/fov no dispatch e ai usamos os numeros dele; DLSS/XeSS nao tem, fica a suposicao
        pp.cameraNear = (usaJogo && mj.camNear > 0) ? mj.camNear : 0.1f;
        pp.cameraFar = (usaJogo && mj.camFar > 0) ? mj.camFar : 1000.0f;
        pp.cameraFovAngleVertical = (usaJogo && mj.camFov > 0) ? mj.camFov : 1.0f;
        pp.viewSpaceToMetersFactor = (usaJogo && mj.viewToM > 0) ? mj.viewToM : 1.0f;
        pp.depth = ffxApiGetResourceDX12(texDepth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        pp.motionVectors = ffxApiGetResourceDX12(texMv, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        const ffxReturnCode_t rcP = m_Dispatch(&m_fg, &pp.header);
        /* J5: corte de cena declarado pelo jogo (o mesmo reset que ele manda ao upscaler) e a troca
           de fonte de movimento (jogo <-> optico) zeram a historia do FI: a semantica da entrada mudou. */
        const bool corteJogo = usaJogo && (mj.flags & 1u) != 0;
        const bool trocouFonte = (m_frameId != 0) && (usaJogo != m_usouJogoAnt);
        if (trocouFonte && m_log && m_dadosRecusas < 10) { m_dadosRecusas++; m_log("fg: fonte do movimento mudou para %s no quadro %llu - historia do interpolador zerada", usaJogo ? "o jogo" : "fluxo optico", (unsigned long long)n); }
        m_usouJogoAnt = usaJogo;
        ffxDispatchDescFrameGeneration dd{}; dd.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION; dd.commandList = m_lista;
        dd.presentColor = ffxApiGetResourceDX12(m_inBFi12[cur], FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.outputs[0] = ffxApiGetResourceDX12(m_outG12[cur], FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        /* N2: o corte que NINGUEM declara. No Universal nao ha `reset` do jogo; quem o declara e
           o passo do dedup, um quadro depois (ver DedupColher). `m_corteSadArmar` e consumido
           aqui, no primeiro despacho depois da deteccao: e o par que nasce DEPOIS do corte, e e
           ele que nao pode carregar o passado de antes dele. */
        const bool corteSad = m_corteSadArmar; m_corteSadArmar = false;
        dd.numGeneratedFrames = 1; dd.reset = (m_frameId == 0) || corteJogo || trocouFonte || corteSad; dd.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        // N5: so o retangulo ativo e interpolado; as barras pretas ficam de fora (ver LetterboxDoQuadro)
        dd.minMaxLuminance[0] = 0; dd.minMaxLuminance[1] = 1; dd.generationRect = { m_lbX, m_lbY, m_lbW, m_lbH }; dd.frameID = m_frameId;
        const ffxReturnCode_t rcD = m_Dispatch(&m_fg, &dd.header);
        if (dd.reset) { m_ctx2Quente = false; m_ctx3Quente = false; }   /* X-FG-6: historia zerada no 1o contexto = zerada no 2o (e no 3o) */
        m_frameId++;
        /* ---- X-FG-6: 3x/4x - o segundo contexto refina o par: G1/4 = FI2(A -> G1/2), G3/4 = FI2(G1/2 -> B).
           Na MESMA lista, entre as mesmas fences; o FiMs() mede os tres despachos juntos. */
        bool extra = false, extra8 = false;
        if (m_mult > 2 && m_fg2) {
            const float dt2 = pp.frameTimeDelta * 0.5f;
            D3D12_RESOURCE_BARRIER b1[3] = {
                Bar(m_outG12[cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),   // G1/2 vira entrada
                Bar(m_outG14_12[cur], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                Bar(m_mvZero12, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) };
            m_lista->ResourceBarrier(3, b1);
            if (!m_ctx2Quente) {
                /* frio (primeira vez, ou depois de um reset): o 2o contexto ainda nao viu A - um despacho
                   com A como "atual" (saida descartada, sobrescrita logo abaixo) deixa A como anterior */
                D3D12_RESOURCE_BARRIER a1 = Bar(m_inBFi12[prev], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                m_lista->ResourceBarrier(1, &a1);
                DespacharCtx2(m_inBFi12[prev], m_outG14_12[cur], dt2, true);
                D3D12_RESOURCE_BARRIER a2[2] = { Bar(m_inBFi12[prev], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON), D3D12_RESOURCE_BARRIER{} };
                a2[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; a2[1].UAV.pResource = m_outG14_12[cur];
                m_lista->ResourceBarrier(2, a2);
                m_ctx2Quente = true;
            }
            DespacharCtx2(m_outG12[cur], m_outG14_12[cur], dt2, false);   /* anterior = A, atual = G1/2 -> G1/4 */
            D3D12_RESOURCE_BARRIER b2[2] = {
                Bar(m_outG14_12[cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
                Bar(m_outG34_12[cur], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) };
            m_lista->ResourceBarrier(2, b2);
            DespacharCtx2(m_inBFi12[cur], m_outG34_12[cur], dt2, false);   /* anterior = G1/2, atual = B -> G3/4; o anterior fica B (= o proximo A) */
            /* ---- META (21/09/2026): 8x - o TERCEIRO contexto refina os quatro pares, na mesma lista.
               A sequencia que ele ve e (A, G1/4, G1/2, G3/4, B, ...): cada despacho da o ponto medio
               entre o anterior visto por ele e o atual, e o "anterior" fica B = o proximo A. Frio,
               um despacho com A (saida descartada) deixa A como anterior - como no 2o contexto. */
            if (m_mult > 4 && m_fg3) {
                const float dt4 = dt2 * 0.5f;
                D3D12_RESOURCE_BARRIER c1[3] = {
                    Bar(m_outG14_12[cur], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                    Bar(m_outG34_12[cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                    Bar(m_outG8_12[0][cur], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) };
                m_lista->ResourceBarrier(3, c1);
                if (!m_ctx3Quente) {
                    D3D12_RESOURCE_BARRIER a1 = Bar(m_inBFi12[prev], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    m_lista->ResourceBarrier(1, &a1);
                    DespacharCtxN(m_fg3, m_frameId3, m_inBFi12[prev], m_outG8_12[0][cur], dt4, true, "3o");
                    D3D12_RESOURCE_BARRIER a2[2] = { Bar(m_inBFi12[prev], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON), D3D12_RESOURCE_BARRIER{} };
                    a2[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; a2[1].UAV.pResource = m_outG8_12[0][cur];
                    m_lista->ResourceBarrier(2, a2);
                    m_ctx3Quente = true;
                }
                /* as quatro entradas em ordem de tempo; a saida i e o ponto medio entre a entrada i-1 (ou A) e a i */
                ID3D12Resource* ent[4] = { m_outG14_12[cur], m_outG12[cur], m_outG34_12[cur], m_inBFi12[cur] };
                for (int i = 0; i < 4; i++) {
                    DespacharCtxN(m_fg3, m_frameId3, ent[i], m_outG8_12[i][cur], dt4, false, "3o");   /* -> G(2i+1)/8 */
                    if (i < 3) {
                        D3D12_RESOURCE_BARRIER cb[2] = {
                            Bar(m_outG8_12[i][cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
                            Bar(m_outG8_12[i + 1][cur], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) };
                        m_lista->ResourceBarrier(2, cb);
                    }
                }
                D3D12_RESOURCE_BARRIER c2[3] = {
                    Bar(m_outG8_12[3][cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
                    Bar(m_outG14_12[cur], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
                    Bar(m_outG34_12[cur], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON) };
                m_lista->ResourceBarrier(3, c2);
                extra8 = true;
            } else {
                D3D12_RESOURCE_BARRIER c0 = Bar(m_outG34_12[cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
                m_lista->ResourceBarrier(1, &c0);
            }
            D3D12_RESOURCE_BARRIER b3[2] = {
                Bar(m_outG12[cur], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
                Bar(m_mvZero12, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON) };
            m_lista->ResourceBarrier(2, b3);
            extra = true;
        }
        m_lista->EndQuery(m_qh, D3D12_QUERY_TYPE_TIMESTAMP, cur * 2 + 1);
        m_lista->ResolveQueryData(m_qh, D3D12_QUERY_TYPE_TIMESTAMP, cur * 2, 2, m_qbuf, cur * 16);
        {
            D3D12_RESOURCE_BARRIER b[4] = {
                Bar(m_inBFi12[cur], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
                Bar(texMv, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
                Bar(m_outG12[cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
                Bar(texDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON) };
            if (extra) { b[2] = b[3]; m_lista->ResourceBarrier(usaJogo ? 3 : 2, b); }   /* o G1/2 ja voltou a COMMON no bloco de cima */
            else m_lista->ResourceBarrier(usaJogo ? 4 : 3, b);
        }
        m_lista->Close();
        ID3D12CommandList* ls[] = { m_lista };
        m_fila->Wait(m_fIn, n);
        if (usaJogo) m_fila->Wait(m_fDados12, n);   // J5: as copias do anel (device da rede) para o quadro n ja terminaram
        m_fila->ExecuteCommandLists(1, ls);
        m_fila->Signal(m_fOut, m_ger);
        m_alocFence[cur] = m_ger;   // X-TDR: a fence que libera ESTE alocador (ver a espera acima)
        m_fSubmetida = m_ger; m_fSubmetidaMs = Ms();   // X-FONTE-SOLTA: o par em voo, para a admissao (ver FiEmVoo)
        if (usaJogo) {
            m_dadosGerLido[sj].store(m_ger, std::memory_order_release);   // o laco so reescreve o slot depois desta fence
            m_geradosJogo++;
            { LARGE_INTEGER q; QueryPerformanceCounter(&q); m_movJogoQpc.store(q.QuadPart, std::memory_order_relaxed);
              /* LATENCIA MEDIDA (o numero que faltava no X-FG-PLANO §7.3): do dispatch do upscaler no
                 jogo ate o FI ser submetido aqui, no mesmo relogio (QPC e do sistema). */
              if (mj.qpcDispatch != 0) { m_somaIdade += QpcMs(q.QuadPart) - QpcMs(mj.qpcDispatch); m_nIdade++; }
              if (m_nIdade == 300 && m_log) m_log("fg: idade do dado do jogo na hora do FI: media %.2f ms em 300 quadros (dispatch do upscaler -> submissao do FI; inclui a travessia da ponte e a cadeia do app)", m_somaIdade / (double)m_nIdade); }
            r->doJogo = true;
            if (corteJogo) { r->corte = true; m_cortes++; }
        }
        m_tsPend[cur] = true;
        if ((rcP != FFX_API_RETURN_OK || rcD != FFX_API_RETURN_OK) && m_log && m_errosLog < 5) { m_errosLog++; m_log("fg: ffxDispatch devolveu prepare=%u fg=%u no quadro %llu", (unsigned)rcP, (unsigned)rcD, (unsigned long long)n); }

        // ---- as estatisticas PRIMEIRO (o Map so espera os passes A/A2/B, que sao pequenos), e
        //      SO DEPOIS a espera pela saida do FI - enfileirada na GPU do D3D11, sem prender a
        //      CPU: o Present de G fica atras dela. (Esperar o FI aqui na CPU custava um quadro
        //      inteiro da cadeia, porque a fila do FI divide a placa com a rede.)
        // As estatisticas lidas sao as do quadro ANTERIOR (o staging do outro slot), SEM esperar:
        // um Map bloqueante aqui prendia a CPU atras de tudo que esta na frente no contexto
        // imediato (inclusive a espera pelo FI do quadro passado). Custo: a decisao de corte
        // de cena vale com um quadro de atraso - num corte, UM quadro gerado entre as duas cenas
        // ainda sai (documentado; a versao sem atraso exige um segundo contexto).
        // SEM ATRASO quando da: com a fila do FI em prioridade alta, o Map das estatisticas DESTE
        // quadro so espera os passes (0,2 ms) e o FI do quadro passado (ja pronto ha um quadro).
        // O tempo do Map e medido; se a media passar de 6 ms (a placa nao esta dando prioridade),
        // cai para o modo com atraso de 1 quadro (staging do outro slot, sem esperar) e diz no log.
        // B12: hoje o atraso de 1 quadro e o ponto de partida (m_statsLag = true na declaracao).
        const int slotStats = m_statsLag ? prev : cur;
        if (temCampo && m_statsPend[slotStats]) {
            D3D11_MAPPED_SUBRESOURCE m{};
            const double tm0 = Ms();
            const HRESULT hm = ctx->Map(m_statsStg[slotStats], 0, D3D11_MAP_READ, m_statsLag ? D3D11_MAP_FLAG_DO_NOT_WAIT : 0, &m);
            if (!m_statsLag) {
                m_mapMs = m_mapMs * 0.9 + (Ms() - tm0) * 0.1;
                if (m_mapMs > 6.0 && m_ger > 30) { m_statsLag = true; if (m_log) m_log("fg: o Map das estatisticas custa %.1f ms de CPU por quadro - corte de cena passa a valer com 1 quadro de atraso", m_mapMs); }
            }
            if (SUCCEEDED(hm)) {
                m_statsPend[slotStats] = false;
                const uint32_t* s = (const uint32_t*)m.pData;
                r->nBlocos = s[0]; r->nDiffBig = s[1]; r->nMov = s[2]; r->nStatic = s[3]; r->nKept = s[4]; r->nFlat = s[5]; r->nMask = s[6]; r->nAmbig = s[7];
                const uint32_t nDesoc = s[8];   // O2-QUALIDADE: blocos em desoclusao (PassoA, offset 32)
                const uint32_t nInc   = s[9];   // FG-QUALIDADE2: blocos que nenhum dos dois reais explica (offset 36)
                ctx->Unmap(m_statsStg[slotStats], 0);
                const double nb = r->nBlocos ? (double)r->nBlocos : 1.0;
                m_desocFrac = m_desocFrac * 0.9 + ((double)nDesoc / nb) * 0.1;
                m_incFrac = m_incFrac * 0.9 + ((double)nInc / nb) * 0.1;
                // CORTE DE CENA: a maioria dos blocos mudou de media E nao ha movimento coerente que
                // explique (um pan rapido de alto contraste tambem muda as medias, mas o campo
                // continua valido nele; num corte de verdade o VME devolve lixo e os gates zeram).
                r->corte = r->nDiffBig > 0.5 * nb && r->nKept < 0.2 * nb;
                r->pan = m_panPrev; r->mv0 = m_mv0Prox;
                m_movFracPrev = (float)(r->nMov / nb);
                // pan: a maior parte dos blocos se move e ha blocos estaticos (HUD) - a margem da mascara entra
                m_panPrev = (r->nKept > 0.7 * nb) && r->nStatic > 0;
                /* X-GERADO (20/09/2026): a regra "a ambiguidade derrubou a maior parte do que se
                   movia -> MV=0 na TELA INTEIRA no proximo quadro" continua MEDIDA, mas nao e mais
                   APLICADA. E uma decisao estatistica: nenhum bloco foi visto errando, e mesmo
                   assim o quadro seguinte inteiro perde o campo e o FI devolve copia do real. Um
                   gate so pode trocar o gerado pelo real onde o FI errou de forma evidente, nunca
                   por uma contagem de blocos. O contador segue no log (`mv0 N`) para se ver quantas
                   vezes a regra TERIA disparado. */
                m_mv0Prox = false;
                const bool mv0Cond = (r->nMov > 0.5 * nb) && (r->nAmbig > 0.5 * r->nMov);
                if (r->corte) m_cortes++;
                if (m_panPrev) m_pans++;
                if (mv0Cond) m_mv0s++;
            }
        }
        c4->Wait(m_fOut11, m_ger);   // G pronto (na GPU) antes de qualquer uso pelo chamador
        // ---- CORTE: G := B. Indireto, com os grupos que o PassoCorteDecide escreveu - zero quando
        //      nao houve corte, e ai este Dispatch nao executa thread nenhuma. Vem DEPOIS da espera
        //      pelo FI porque e o G recem-escrito que esta sendo substituido.
        if (!temCampo && m_corteLigado && m_csCorteAp) {
            ctx->CSSetConstantBuffers(0, 1, &m_cbCorte);
            ctx->CSSetShader(m_csCorteAp, nullptr, 0);
            ctx->CSSetShaderResources(0, 1, &m_inBFiSrv[cur]);
            ctx->CSSetUnorderedAccessViews(0, 1, &m_outGUav[cur], nullptr);
            ctx->DispatchIndirect(m_corteArgs, 0);
            if (extra && m_outG14Uav[cur] && m_outG34Uav[cur]) {   /* X-FG-6: num corte, G1/4 e G3/4 tambem viram B */
                ctx->CSSetUnorderedAccessViews(0, 1, &m_outG14Uav[cur], nullptr); ctx->DispatchIndirect(m_corteArgs, 0);
                ctx->CSSetUnorderedAccessViews(0, 1, &m_outG34Uav[cur], nullptr); ctx->DispatchIndirect(m_corteArgs, 0);
            }
            if (extra8) for (int i = 0; i < 4; i++) if (m_outG8Uav[i][cur]) { ctx->CSSetUnorderedAccessViews(0, 1, &m_outG8Uav[i][cur], nullptr); ctx->DispatchIndirect(m_corteArgs, 0); }   /* META: os de 1/8 tambem */
            ID3D11UnorderedAccessView* nu = nullptr; ID3D11ShaderResourceView* ns = nullptr; ID3D11Buffer* nb = nullptr;
            ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr); ctx->CSSetShaderResources(0, 1, &ns);
            ctx->CSSetConstantBuffers(0, 1, &nb); ctx->CSSetShader(nullptr, nullptr, 0);
            // telemetria SEM esperar: o que chegar chegou (o Map bloqueante aqui custa ~68 ms - medido)
            const int slot = prev;
            if (m_cortePend[slot]) {
                D3D11_MAPPED_SUBRESOURCE m{};
                if (SUCCEEDED(ctx->Map(m_corteStg[slot], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
                    m_cortePend[slot] = false;
                    const uint32_t* s = (const uint32_t*)m.pData;
                    const uint64_t cortes = s[2]; float fr = 0, frC = 0, baseC = 0;
                    memcpy(&fr, &s[3], 4); memcpy(&frC, &s[4], 4); memcpy(&baseC, &s[5], 4);
                    ctx->Unmap(m_corteStg[slot], 0);
                    m_corteFrac = fr;
                    if (cortes > m_cortesSemCampo) {
                        const uint64_t novos = cortes - m_cortesSemCampo;
                        m_cortes += novos; m_cortesSemCampo = cortes;
                        if (m_log && m_cortesVistos < 8) { m_cortesVistos++; m_log("fg: CORTE DE CENA perto do quadro %llu (%.1f%% dos blocos mudaram de media de luma, %.1fx a media movel de %.2f%%) - o G foi trocado pelo quadro real", (unsigned long long)n, frC * 100.0, baseC > 0 ? frC / baseC : 0.0, baseC * 100.0); }
                    }
                }
            }
        }
        /* ---- MASCARA DE HUD (X-HUD-PISCA): depois do FI e depois do corte (num corte G ja e B e a
           copia e inocua). Roda COM ou SEM campo do VME - o fluxo optico interno do FI arrasta o HUD
           nos dois casos. Ver CriarHud. */
        if (m_hudLigado && m_csHudConta && m_csHudAplica) {
            m_medHud.Antes(ctx, cur);
            const UINT zero[4] = { 0, 0, 0, 0 };
            if (m_hudLimpar) { ctx->ClearUnorderedAccessViewUint(m_hudRunUav, zero); m_hudLimpar = false; }
            ctx->ClearUnorderedAccessViewUint(m_hudStatsUav, zero);
            // O2-QUALIDADE: o cb virou dinamico - bw/bh so existem depois que a grade do campo
            // sobe, e desocOn cai sozinho quando nao ha campo neste quadro (nada a reprojetar).
            const bool desocAgora = m_desocLigado && temCampo && m_desocSrv != nullptr;
            {
                D3D11_MAPPED_SUBRESOURCE mh{};
                if (SUCCEEDED(ctx->Map(m_cbHud, 0, D3D11_MAP_WRITE_DISCARD, 0, &mh))) {
                    HudCB c{};
                    c.W = m_w; c.H = m_h; c.N = m_hudN;
                    c.maxProt = (UINT)std::min<double>((double)m_w * (double)m_h, m_hudMaxFrac * (double)m_w * (double)m_h);
                    c.thr = (float)m_hudLimiar / 255.0f;
                    c.corte = m_hudMascara ? m_hudCorte : 2.0f;   // acima de 1: nenhuma confianca alcanca (mascara desligada)
                    c.apCon = kHudApCon; c.apFino = kHudApFino;
                    c.miraR = m_hudMiraR; c.miraBonus = kHudMiraBonus;
                    c.desocForca = m_desocForca;
                    c.bw = m_gw ? (float)m_w * m_covU / m_gw : (float)m_w; c.bh = m_gh ? (float)m_h * m_covV / m_gh : (float)m_h;
                    c.apar = m_hudApar ? 1u : 0u; c.desocOn = desocAgora ? 1u : 0u;
                    c.cx = m_w / 2; c.cy = m_h / 2;
                    memcpy(mh.pData, &c, sizeof(c)); ctx->Unmap(m_cbHud, 0);
                }
            }
            ctx->CSSetConstantBuffers(0, 1, &m_cbHud);
            ID3D11UnorderedAccessView* nu3[3] = {}; ID3D11ShaderResourceView* ns4[4] = {};
            // 1) confianca de interface por pixel (e o `run` de estabilidade temporal)
            {
                ID3D11ShaderResourceView* s2[2] = { m_inBFiSrv[prev], m_inBFiSrv[cur] };
                ID3D11UnorderedAccessView* u3[3] = { m_hudRunUav, m_hudStatsUav, m_hudConfUav };
                ctx->CSSetShader(m_csHudConta, nullptr, 0);
                ctx->CSSetShaderResources(0, 2, s2);
                ctx->CSSetUnorderedAccessViews(0, 3, u3, nullptr);
                ctx->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
                ctx->CSSetUnorderedAccessViews(0, 3, nu3, nullptr);   // run/conf saem do UAV antes de entrar como SRV
            }
            // 2) aplica no G: desoclusao (peso por bloco) e depois a interface (peso por pixel),
            //    nessa ordem - a interface manda mais. Em 3x/4x os G1/4 e G3/4 levam o mesmo
            //    tratamento; so o G1/2 conta nas estatisticas.
            {
                ID3D11ShaderResourceView* s4[4] = { m_inBFiSrv[prev], m_inBFiSrv[cur], m_hudConfSrv, desocAgora ? m_desocSrv : nullptr };
                ctx->CSSetShader(m_csHudAplica, nullptr, 0);
                ctx->CSSetShaderResources(0, 4, s4);
                ID3D11UnorderedAccessView* u2[2] = { m_hudGUav[cur], m_hudStatsUav };
                ctx->CSSetUnorderedAccessViews(0, 2, u2, nullptr);
                ctx->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
                if (extra && m_outG14Uav[cur] && m_outG34Uav[cur]) {
                    ID3D11UnorderedAccessView* u14[2] = { m_outG14Uav[cur], nullptr };   // stats nulo: escrita descartada, nao conta duas vezes
                    ctx->CSSetUnorderedAccessViews(0, 2, u14, nullptr); ctx->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
                    ID3D11UnorderedAccessView* u34[2] = { m_outG34Uav[cur], nullptr };
                    ctx->CSSetUnorderedAccessViews(0, 2, u34, nullptr); ctx->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
                }
                if (extra8) for (int i = 0; i < 4; i++) if (m_outG8Uav[i][cur]) {   /* META: a mascara tambem nos de 1/8 */
                    ID3D11UnorderedAccessView* u8[2] = { m_outG8Uav[i][cur], nullptr };
                    ctx->CSSetUnorderedAccessViews(0, 2, u8, nullptr); ctx->Dispatch((m_w + 7) / 8, (m_h + 7) / 8, 1);
                }
                ctx->CSSetUnorderedAccessViews(0, 3, nu3, nullptr); ctx->CSSetShaderResources(0, 4, ns4);
            }
            ID3D11Buffer* nb = nullptr; ctx->CSSetConstantBuffers(0, 1, &nb); ctx->CSSetShader(nullptr, nullptr, 0);
            ctx->CopyResource(m_hudStg[cur], m_hudStats); m_hudPend[cur] = true;
            m_medHud.Depois(ctx, cur);
            // telemetria do quadro ANTERIOR, sem esperar (o Map bloqueante custa dezenas de ms - armadilha 22)
            if (m_hudPend[prev]) {
                D3D11_MAPPED_SUBRESOURCE m{};
                if (SUCCEEDED(ctx->Map(m_hudStg[prev], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m))) {
                    m_hudPend[prev] = false;
                    const uint32_t* s = (const uint32_t*)m.pData;
                    const double px = (double)m_w * (double)m_h;
                    // [4] agora e a soma dos PESOS em 1024 avos (a mistura nao e binaria): a fracao
                    // e "quanto de quadro real entrou no gerado", nao "quantos pixels foram copiados".
                    const double frac = px > 0.0 ? (double)s[1] / (px * 1024.0) : 0.0;
                    const double acima = px > 0.0 ? (double)s[0] / px : 0.0;
                    ctx->Unmap(m_hudStg[prev], 0);
                    m_hudFrac = m_hudFrac * 0.9 + frac * 0.1;
                    m_hudParados = m_hudParados * 0.9 + acima * 0.1;
                }
            }
        }
        // X-GERADO: a prova do gerado - com o G FINAL (corte e mascara de HUD ja aplicados)
        ProvaColher(ctx);
        ProvaDespachar(ctx, cur, prev);
        r->G = m_outG[cur];
        /* X-FG-6: os gerados em ordem de tempo (o corte G := B de cima so cobre o G1/2; num corte o
           chamador mostra B e nao usa nenhum) */
        if (extra8)                    { /* META: 8x, os sete em ordem de tempo */
                                         ID3D11Texture2D* g8[7] = { m_outG8[0][cur], m_outG14[cur], m_outG8[1][cur], m_outG[cur], m_outG8[2][cur], m_outG34[cur], m_outG8[3][cur] };
                                         for (int i = 0; i < 7; i++) { r->Gs[i] = g8[i]; r->fracs[i] = (float)(i + 1) / 8.0f; }
                                         r->nG = 7; m_geradosExtra += 6; }
        else if (extra && m_mult == 4) { r->Gs[0] = m_outG14[cur]; r->Gs[1] = m_outG[cur]; r->Gs[2] = m_outG34[cur]; r->nG = 3; m_geradosExtra += 2;
                                         r->fracs[0] = 0.25f; r->fracs[1] = 0.50f; r->fracs[2] = 0.75f; }
        else if (extra && m_mult == 3) { r->Gs[0] = m_outG14[cur]; r->Gs[1] = m_outG34[cur]; r->nG = 2; m_geradosExtra += 1;
                                         r->fracs[0] = 0.25f; r->fracs[1] = 0.75f; }   /* mostrados em 1/3 e 2/3; a fracao REAL e esta */
        else                           { r->Gs[0] = m_outG[cur]; r->nG = 1; r->fracs[0] = 0.50f; }
        m_gerados++;
        Dump(ctx, cur, prev, n);
        m_ger++;
        c4->Release();
        return true;
    }

    void Parar() {
        if (m_fila && m_fOut) EsperarFila();
        if (m_fg) { m_Destroy(&m_fg, nullptr); m_fg = nullptr; }
        SoltarCtx3(); SoltarCtx2(); m_mult = 2; m_ctx2Falhou = false; m_ctx3Falhou = false;   /* X-FG-6 / META */
        EnterrarVelhas(true);   // W-153: no Parar() ninguem mais apresenta
        m_contexto = false; m_campoOk = false;
        for (int i = 0; i < kSlots; i++) { Soltar(m_inBSrv[i]); Soltar(m_inB[i]); Soltar(m_inBFiSrv[i]); Soltar(m_inBFi12[i]); Soltar(m_inBFi[i]); Soltar(m_outG12[i]); Soltar(m_outG[i]); Soltar(m_statsStg[i]); Soltar(m_alloc[i]); }
        Soltar(m_csDown); Soltar(m_samp); Soltar(m_cbDown); m_reduz = false;
        for (int i = 0; i < 3; i++) { Soltar(m_campoSrv[i]); Soltar(m_campoApres[i]); Soltar(m_campoRede[i]); }
        Soltar(m_mvUav); Soltar(m_mv12); Soltar(m_mv);
        Soltar(m_blocoUav); Soltar(m_blocoSrv); Soltar(m_bloco); Soltar(m_bloco2Uav); Soltar(m_bloco2Srv); Soltar(m_bloco2); Soltar(m_runUav); Soltar(m_run);
        Soltar(m_statsUav); Soltar(m_stats); Soltar(m_cb);
        Soltar(m_csA); Soltar(m_csA2); Soltar(m_csB);
        for (int i = 0; i < kSlots; i++) { Soltar(m_corteStg[i]); Soltar(m_outGUav[i]); m_cortePend[i] = false; }
        Soltar(m_corteUav); Soltar(m_corteStats); Soltar(m_corteEstUav); Soltar(m_corteEst);
        Soltar(m_corteArgsUav); Soltar(m_corteArgs); Soltar(m_corteMedUav); Soltar(m_corteMed); Soltar(m_cbCorte);
        Soltar(m_csCorte); Soltar(m_csCorteDec); Soltar(m_csCorteAp); m_medCorte.Parar();
        m_corteFrac = 0.0; m_cortesSemCampo = 0; m_cortesVistos = 0;
        // mascara de HUD (X-HUD-PISCA)
        for (int i = 0; i < kSlots; i++) { Soltar(m_hudStg[i]); Soltar(m_hudGUav[i]); m_hudPend[i] = false; }
        Soltar(m_hudRunSrv); Soltar(m_hudRunUav); Soltar(m_hudRun); Soltar(m_hudStatsUav); Soltar(m_hudStats); Soltar(m_cbHud);
        Soltar(m_hudConfSrv); Soltar(m_hudConfUav); Soltar(m_hudConf);
        Soltar(m_desocSrv); Soltar(m_desocUav); Soltar(m_desoc); m_desocFrac = 0.0; m_incFrac = 0.0;
        Soltar(m_csHudConta); Soltar(m_csHudAplica); m_medHud.Parar(); m_hudFrac = 0.0; m_hudParados = 0.0; m_hudLimpar = true;
        // quadro repetido (X-FG-QUALIDADE)
        Soltar(m_csDedup); Soltar(m_dedupStatsUav); Soltar(m_dedupStats); Soltar(m_cbDedup);
        for (int i = 0; i < kDedupAnel; i++) { Soltar(m_dedupStg[i]); m_dedupPend[i] = false; m_dedupPendN[i] = 0; }
        for (int i = 0; i < kSlots; i++) Soltar(m_outGSrv[i]);
        Soltar(m_provaUav); Soltar(m_provaStats); Soltar(m_csProva);
        for (int i = 0; i < kDedupAnel; i++) { Soltar(m_provaStg[i]); m_provaPend[i] = false; }
        for (int i = 0; i < kDedupVer; i++) m_dedupVerN[i] = ~0ull;
        m_dedupEsperaAcc = 0.0; m_dedupN = 0; m_dedupEsperaMax = 0.0; m_dedupUltFrac = -1.0;
        m_dedupJanRep = 0; m_dedupJanN = 0; m_dedupRespN = 0; m_dedupRespAcc = 0.0; m_dedupAtrasoAcc = 0.0; m_dedupLogMs = 0.0;
        m_medPasses.Parar(); m_medVme.Parar(); m_me.Shutdown(); m_campoLocal = false;
        Soltar(m_fIn11); Soltar(m_fOut11); Soltar(m_fCampoApres); Soltar(m_fCampoRede);
        SoltarDadosJogo(false);   /* J5: o anel de depth/MV do jogo e as fences dele (a fila ja foi esperada acima) */
        Soltar(m_fDreno); if (m_evtDreno) { CloseHandle(m_evtDreno); m_evtDreno = nullptr; } m_vDreno = 0;   // RESET-DRENO
        Soltar(m_depth); Soltar(m_qbuf); Soltar(m_qh); Soltar(m_lista); Soltar(m_fIn); Soltar(m_fOut); Soltar(m_fila); Soltar(m_dev12);
        Soltar(m_devRede); Soltar(m_dev);
        if (m_evt) { CloseHandle(m_evt); m_evt = nullptr; }
        if (m_dll) { FreeLibrary(m_dll); m_dll = nullptr; }
        m_ger = 0; m_frameId = 0; m_campoN.store(0);
        m_ctxSolto = false; m_morto = false; for (int i = 0; i < kSlots; i++) m_alocFence[i] = 0; m_fiAtrasoDesde = 0.0; m_fiAtrasos = 0; m_fiAtrasosLog = 0;
        m_fSubmetida = 0; m_fSubmetidaMs = 0.0;   // X-FONTE-SOLTA
    }
    ~Interpolador() { Parar(); }

private:
    struct Constantes { UINT W, H, GW, GH; float bw, bh, movFracPrev, desocDisc; UINT mv0, pan, ambigAntigo, desocOn; float incLim, incEsc, incPeso, pad3; };

    // ---- os shaders dos passes (ver o cabecalho). Luma em 0..1.
    static const char* Hlsl() {
        return
            "cbuffer C : register(b0) { uint W, H, GW, GH; float bw, bh, movFracPrev, desocDisc; uint mv0, pan, ambigAntigo, desocOn; float incLim, incEsc, incPeso, pad3; };\n"
            "Texture2D<float4> A : register(t0); Texture2D<float4> B : register(t1); Texture2D<float4> campo : register(t2);\n"
            "RWTexture2D<float4> bloco : register(u0); RWTexture2D<uint> runEst : register(u1); RWByteAddressBuffer stats : register(u2); RWTexture2D<float> desoc : register(u3);\n"
            "float Luma(float4 c) { return dot(c.rgb, float3(0.299, 0.587, 0.114)); }\n"
            "float LumaA(float2 p) { int2 ip = int2(clamp(p, float2(0, 0), float2(W - 1, H - 1))); return Luma(A.Load(int3(ip, 0))); }\n"
            "float LumaB(float2 p) { int2 ip = int2(clamp(p, float2(0, 0), float2(W - 1, H - 1))); return Luma(B.Load(int3(ip, 0))); }\n"
            "[numthreads(8,8,1)]\n"
            "void PassoA(uint3 id : SV_DispatchThreadID) {\n"
            "  if (id.x >= GW || id.y >= GH) return;\n"
            "  float4 cf = campo.Load(int3(id.xy, 0)); float2 v = cf.xy * float2(W, H); bool ok = cf.z >= 0.5;\n"
            "  bool mov = ok && (abs(v.x) >= 0.5 || abs(v.y) >= 0.5);\n"
            "  float2 org = float2(id.xy) * float2(bw, bh); float2 st = float2(bw, bh) / 8.0;\n"
            "  float lb[64]; float soma = 0, soma2 = 0, sadS = 0, mA = 0, mB = 0;\n"
            "  [unroll] for (int j = 0; j < 8; j++) [unroll] for (int i = 0; i < 8; i++) {\n"
            "    float2 p = org + (float2(i, j) + 0.5) * st; int2 ip = int2(clamp(p, float2(0, 0), float2(W - 1, H - 1)));\n"
            "    float lB = Luma(B.Load(int3(ip, 0))), lA = Luma(A.Load(int3(ip, 0)));\n"
            "    lb[j * 8 + i] = lB; soma += lB; soma2 += lB * lB; sadS += abs(lB - lA); mA += lA; mB += lB; }\n"
            "  float var = soma2 / 64.0 - (soma / 64.0) * (soma / 64.0);\n"
            "  bool estatico = sadS < 64.0 * (2.0 / 255.0);\n"
            "  uint run = estatico ? min(runEst[id.xy] + 1u, 1000u) : 0u; runEst[id.xy] = run;\n"
            "  uint flags = estatico ? 16u : 0u; uint tmp;\n"
            "  stats.InterlockedAdd(0, 1u, tmp);\n"
            "  if (abs(mA - mB) / 64.0 > 40.0 / 255.0) stats.InterlockedAdd(4, 1u, tmp);\n"
            "  if (estatico) stats.InterlockedAdd(12, 1u, tmp);\n"
            "  if (mov) {\n"
            "    flags |= 1u; stats.InterlockedAdd(8, 1u, tmp); bool zera = false;\n"
            "    if (var < (2.0 / 255.0) * (2.0 / 255.0)) { zera = true; flags |= 2u; stats.InterlockedAdd(20, 1u, tmp); }\n"
            // GUARDA DA MASCARA DE HUD: so vale como "HUD" um bloco parado ENQUANTO A CENA SE MEXE
            // DE VERDADE. Com 0,10 (o valor ate 13/09/2026) bastava 10% da tela em movimento -
            // e pan e combate reais movem ~30% (RESULTADOS.txt secao 5), entao o detector pintava
            // metade do CENARIO parado junto com a UI (precisao 32%). Em cena de movimento real
            // (menu, 57%) ele acerta a UI. 0,40 e o valor que a medicao pediu.
            "    if (!zera && run >= 8u && movFracPrev > 0.40) { zera = true; flags |= 4u; stats.InterlockedAdd(24, 1u, tmp); }\n"
            /* AMBIGUIDADE = SEGUNDO MINIMO DESTACADO, NAO VALE LARGO (X-GERADO, 20/09/2026).
               Ate hoje bastava existir ALGUM deslocamento ao longo do vetor com SAD tao bom
               quanto o do proprio vetor para o bloco perder o vetor (MV=0). Isso confunde duas
               coisas opostas:
                 - GRADE/XADREZ (o que o gate foi feito para pegar): o SAD sobe ao sair do vetor
                   e desce DE NOVO um periodo adiante. O segundo minimo e destacado, o vetor pode
                   estar fora de fase por um periodo inteiro e o meio caminho sai com a trama
                   deslocada meia celula - piscada no quadro gerado;
                 - ABERTURA (bloco liso na direcao do movimento - o interior de um objeto com
                   gradiente suave, uma mira, um HUD andando): o SAD fica BAIXO num vale LARGO em
                   volta do vetor. Nao ha ambiguidade visivel nenhuma: qualquer vetor do vale
                   interpola a mesma imagem, e zerar e a UNICA escolha garantidamente errada -
                   MV=0 num bloco que se move faz o FI devolver copia do quadro real.
               MEDIDO (FG-PROVA §3, alvo64 a 30 fps, quadrado de gradiente sobre fundo parado):
               `ambiguidade 96% dos 2% blocos moventes` e o quadrado saltando sempre um passo
               INTEIRO na tela, 31,2 imagens distintas/s com 60 presents/s - o gerado era o real.
               AGORA: separa-se o minimo PERTO (|d| <= 8 px, a vizinhanca do proprio vetor) do
               minimo LONGE. So zera quando o perto NAO alcanca o limiar (ha crista em volta do
               vetor) e o longe alcanca (existe um segundo minimo de verdade). Vale largo -> o
               vetor fica. */
            "    if (!zera) {\n"
            "      float2 u = normalize(v); float sadV = 0, perto = 1e30, longe = 1e30;\n"
            "      [unroll] for (int j = 0; j < 8; j += 2) [unroll] for (int i = 0; i < 8; i += 2) { float2 p = org + (float2(i, j) + 0.5) * st; sadV += abs(lb[j * 8 + i] - LumaA(p + v)); }\n"
            "      for (int k = -36; k <= 36; k++) { float d = 2.0 * k; if (abs(d) <= 2.0) continue; float2 off = v + u * d; float sd = 0;\n"
            "        [unroll] for (int j = 0; j < 8; j += 2) [unroll] for (int i = 0; i < 8; i += 2) { float2 p = org + (float2(i, j) + 0.5) * st; sd += abs(lb[j * 8 + i] - LumaA(p + off)); }\n"
            "        if (abs(d) <= 8.0) perto = min(perto, sd); else longe = min(longe, sd); }\n"
            "      float lim = 1.2 * sadV + 16.0 / 255.0;\n"
            // BANCADA: DLSS5_FG_AMBIG_ANTIGO=1 volta a regra de ate 20/09 (qualquer minimo serve),
            // para medir antes x depois com o MESMO exe - a metrica de prova so existe neste.
            "      bool ambiguo = ambigAntigo != 0u ? (min(perto, longe) <= lim) : (perto > lim && longe <= lim);\n"
            "      if (ambiguo) { zera = true; flags |= 8u; stats.InterlockedAdd(28, 1u, tmp); }\n"
            "    }\n"
            "    if (zera) { v = 0; mov = false; } else stats.InterlockedAdd(16, 1u, tmp);\n"
            "  }\n"
            /* ---- DESOCLUSAO (O2-QUALIDADE, 20/09/2026), tres linhas de ideia:
               1. CONSISTENCIA IDA/VOLTA SEM UM SEGUNDO CAMPO. O campo que alimentamos o FI e
                  BACKWARD (B -> A): B(p) ~= A(p + v). Em vez de estimar tudo de novo no outro
                  sentido (caro), le-se o vetor do bloco de DESTINO (id + v/bloco). Numa area que
                  so se move, o destino tem o mesmo vetor; numa DESOCLUSAO o destino pertence ao
                  objeto que cobre (ou ao fundo que aparece) e o vetor la e outro - ou nem existe.
                  `disc` e essa discordancia, relativa ao tamanho do proprio vetor.
               2. QUAL DOS DOIS QUADROS REAIS EXPLICA O BLOCO. Dois residuos de 16 amostras:
                  rB = |B(p) - A(p+v)| (o vetor explica B) e rA = |A(p) - B(p-v)| (explica A).
                  lado = (rA - rB)/(rA + rB): positivo => a reprojecao de B e a consistente.
               3. O QUE SE FAZ COM ISSO. Nada no meio do FI - nao da para inventar o que ha atras
                  do objeto. O que da e PESAR o gerado para o lado consistente, com forca
                  proporcional a disc e a |lado|; o passo de aplicacao (depois do FI) faz o lerp.
                  Onde os dois lados explicam igual (lado ~ 0) a forca vai a zero sozinha: sem
                  evidencia, nao se mexe no gerado. */
            /* FG-QUALIDADE2 (20/09/2026) - O SEGUNDO CASO, o que faltava.
               A regra da O2 acima pesa o gerado para o lado CONSISTENTE, com forca proporcional a
               |lado| = quanto um dos dois explica melhor que o outro. Ela tem um buraco por
               construcao: quando NENHUM dos dois explica o bloco (rA e rB os DOIS altos), |lado|
               vai a zero e a forca some - e e exatamente ai que mora o fantasma, porque e ai que o
               FI esta inventando conteudo que nao existe em quadro real nenhum.
               Entao a forca passa a ser o MAIOR entre duas coisas:
                 fw  = a da O2 (um lado explica melhor);
                 inc = min(rA, rB) acima de um limiar - "nenhum dos dois explica", em niveis de
                       luma por amostra. O destino continua sendo o de menor residuo; no empate
                       (|lado| pequeno) vai para B, o quadro real mais proximo no tempo do lado de
                       quem olha (o G e apresentado e logo em seguida vem o B).
               O QUE SE COMPRA E O QUE SE PAGA: troca-se fantasma (conteudo inventado) por dobro
               leve (o mesmo real aparecendo duas vezes). O dono pediu esse cambio nesta ordem.
               incPeso = 0 desliga e devolve exatamente a regra da O2 - e o antes x depois da
               bancada, no mesmo exe (DLSS5_FG_INC).
               CUSTO: os residuos passam a ser calculados em TODO bloco movente, nao so nos de
               discordancia alta - 32 Loads a mais por bloco movente. E medido no `c`. */
            "  float dz = 0.5;\n"
            "  if (mov && desocOn != 0u) {\n"
            "    int2 gd = clamp(int2(id.xy) + int2(round(v / float2(max(bw, 1e-3), max(bh, 1e-3)))), int2(0, 0), int2((int)GW - 1, (int)GH - 1));\n"
            "    float4 cd = campo.Load(int3(gd, 0)); float2 vd = cd.xy * float2(W, H);\n"
            "    float disc = (cd.z >= 0.5) ? length(vd - v) / max(length(v), 2.0) : 1.0;\n"
            "    bool querInc = incPeso > 0.001;\n"
            "    if (disc > desocDisc || querInc) {\n"
            "      float rB = 0, rA = 0;\n"
            "      [unroll] for (int j2 = 0; j2 < 8; j2 += 2) [unroll] for (int i2 = 0; i2 < 8; i2 += 2) {\n"
            "        float2 q = org + (float2(i2, j2) + 0.5) * st;\n"
            "        rB += abs(lb[j2 * 8 + i2] - LumaA(q + v));\n"
            "        rA += abs(LumaA(q) - LumaB(q - v)); }\n"
            "      float lado = (rA - rB) / max(rA + rB, 1e-4);\n"
            "      float fw = (disc > desocDisc) ? saturate((disc - desocDisc) / max(desocDisc, 1e-3)) * saturate(abs(lado) * 2.0) : 0.0;\n"
            "      float inc = querInc ? saturate(((min(rA, rB) / 16.0) - incLim) / max(incEsc, 1e-4)) * incPeso : 0.0;\n"
            "      float f = max(fw, inc);\n"
            "      float sinal = (abs(lado) < 0.05) ? 1.0 : (lado >= 0.0 ? 1.0 : -1.0);\n"
            "      dz = 0.5 + 0.5 * f * sinal;\n"
            "      uint td;\n"
            "      if (disc > desocDisc) stats.InterlockedAdd(32, 1u, td);\n"
            "      if (inc > fw && inc > 0.02) stats.InterlockedAdd(36, 1u, td);\n"
            "    }\n"
            "  }\n"
            "  desoc[id.xy] = dz;\n"
            "  bloco[id.xy] = float4(v, mov ? 1.0 : 0.0, asfloat(flags));\n"
            "}\n"
            "Texture2D<float4> blocoIn : register(t0); RWTexture2D<float4> blocoOut : register(u0);\n"
            "[numthreads(8,8,1)]\n"
            "void PassoA2(uint3 id : SV_DispatchThreadID) {\n"
            "  if (id.x >= GW || id.y >= GH) return;\n"
            "  float4 b = blocoIn.Load(int3(id.xy, 0)); float4 best = b;\n"
            "  if (b.z < 0.5) {\n"
            "    for (int dy = -6; dy <= 6 && best.z < 0.5; dy++) for (int dx = -6; dx <= 6; dx++) {\n"
            "      int2 s = int2(id.xy) + int2(dx, dy); if (s.x < 0 || s.y < 0 || s.x >= (int)GW || s.y >= (int)GH) continue;\n"
            "      float4 c = blocoIn.Load(int3(s, 0)); if (c.z < 0.5 || length(c.xy) <= 60.0) continue;\n"
            "      float2 vb = c.xy / float2(bw, bh); float2 d = float2(int2(id.xy) - s); float t = dot(d, vb) / max(dot(vb, vb), 1e-6);\n"
            "      if (t < 0.0 || t > 1.0) continue; float2 perp = d - t * vb;\n"
            "      if (dot(perp, perp) <= 0.36) { best = float4(c.xy, 0.75, c.w); break; }\n"
            "    }\n"
            "  }\n"
            "  blocoOut[id.xy] = best;\n"
            "}\n"
            "Texture2D<float4> bloco2 : register(t0); RWTexture2D<float2> mvOut : register(u0);\n"
            "[numthreads(8,8,1)]\n"
            "void PassoB(uint3 id : SV_DispatchThreadID) {\n"
            "  if (id.x >= W || id.y >= H) return;\n"
            "  int2 g = int2(min(uint(id.x / bw), GW - 1), min(uint(id.y / bh), GH - 1));\n"
            "  float4 b = bloco2.Load(int3(g, 0)); float2 v = b.z > 0.5 ? b.xy : float2(0, 0);\n"
            "  if (mv0 != 0) v = 0;\n"
            "  if (pan != 0) { for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { int2 q = g + int2(dx, dy);\n"
            "    if (q.x < 0 || q.y < 0 || q.x >= (int)GW || q.y >= (int)GH) continue; if (asuint(bloco2.Load(int3(q, 0)).w) & 4u) v = 0; } }\n"
            "  mvOut[id.xy] = v;\n"
            "}\n";
    }
    /* CORTE DE CENA SEM CAMPO DE MOVIMENTO (fg_ffx.h, 13/09/2026).
       O detector de corte "de verdade" (nDiffBig/nKept) mora no PassoA, que so e despachado
       quando ha campo do VME - ou seja, NUNCA no modo de duas placas (MV=0). Medido pelo
       engenheiro de qualidade: no quadro de um corte o G da 30,18 dB contra 41,73 dB de
       simplesmente mostrar um quadro real (-11,55 dB). Este passo existe para nao pagar isso.
       E um passo SO de media de luma por bloco (nada de SAD deslocado, nada de historico de
       bloco): 4x4 amostras por bloco de uma grade de 24 px, dois Load por amostra. */
    /* TUDO ACONTECE NA GPU, de proposito. A primeira versao lia os contadores na CPU (um Map do
       staging, como o PassoA faz) e MEDIDO no app em 13/09/2026: esse Map custa ~68 ms - um
       quadro inteiro do apresentador -, porque o contexto imediato do D3D11 dele ja tem o Present
       e a espera pelo FI do quadro anterior na frente. Com isso a decisao sairia com UM QUADRO DE
       ATRASO, que e o pior dos mundos num corte: paga-se o G ruim E ainda se descarta o G bom do
       quadro seguinte. Entao: o passo de grade conta, um passo de UMA thread decide e escreve os
       argumentos de um DispatchIndirect, e o passo de aplicacao (que so existe se o corte foi
       detectado - zero grupos quando nao foi) copia B por cima do G depois que o FI termina.
       A CPU so le a estatistica para o log, sem esperar (DO_NOT_WAIT). */
    /* `amostra` = quantas amostras por lado dentro do bloco (4 => 4x4 = 16, o validado).
       BARATO (item 3m, 13/09/2026) - tres mudancas, TODAS com o mesmo resultado numerico:
        1. a media de luma do bloco no quadro ANTERIOR nao e reamostrada: fica guardada em `med`
           pelo proprio passo no quadro passado (mesmo slot de textura, mesmas posicoes de
           amostra), entao o valor e o MESMO que reamostrar A. Isso corta METADE dos Loads de
           textura, que sao o custo dominante do passo;
        2. a contagem e reduzida no GRUPO (groupshared): um atomico global por grupo de 8x8 em vez
           de dois por bloco (14400 blocos em 4K = 28800 atomicos no mesmo endereco);
        3. o total de blocos nao e contado - e GW*GH por construcao.
       MEDIDO no app (4K, duas placas, jogo falso modo 3, media de 3+ rodadas por config; o
       numero e ruidoso - +-40% entre rodadas da MESMA config, armadilha 13b):
           antes .................. 0,46 a 1,23 ms (media 0,84)
           com as tres mudancas ... 0,43 a 0,61 ms (media 0,55)   <- e o que esta ligado
       O que NAO foi adotado, e por que (numeros em D:\claude-scratch\corte3m\):
        - AMOSTRA 2x2: chega a 0,31-0,51 ms, mas cobra acerto - a razao do corte sobre a media
          movel cai de 18,8x para 9,3x contra um limiar de 8x (sobra so 16% de folga), e no app
          a recuperacao de cortes cai de 11-17 para 10-12 em 27 cortes gerados;
        - GRADE 32 px: NAO ficou mais barata de forma mensuravel (0,45-1,03 ms) e a folga do
          limiar cai para 15,6x. Curiosidade a investigar: nessa grade o app pegou 19-24 dos 27
          cortes (contra 11-17 em 24 px) - pode ser propriedade do conteudo sintetico do jogo
          falso, nao foi visto nos tripletos reais.
       As duas ficam so como ajuste de bancada: DLSS5_FG_CORTE_AMOSTRA / _GRADE. */
    static std::string HlslCorte(unsigned amostra) {
        const unsigned S = amostra ? amostra : 4u;
        char cab[256];
        snprintf(cab, sizeof(cab), "static const int kS = %u; static const float kSS = %u.0;\n", S, S * S);
        return std::string(
            "cbuffer C : register(b0) { uint W, H, GW, GH; float bw, bh, thr, salto; float fracMin, piso, fracAlta, saltoAlta; uint GX, GY, u0_, u1_; };\n"
            "Texture2D<float4> A : register(t0); Texture2D<float4> B : register(t1);\n"
            "RWByteAddressBuffer stats : register(u0); RWByteAddressBuffer est : register(u1); RWByteAddressBuffer args : register(u2);\n"
            "float Luma(float4 c) { return dot(c.rgb, float3(0.299, 0.587, 0.114)); }\n") + cab +
            "RWByteAddressBuffer med : register(u3);\n"
            "groupshared uint gCnt;\n"
            "[numthreads(8,8,1)]\n"
            "void PassoCorte(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {\n"
            "  if (gi == 0u) gCnt = 0u;\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  if (id.x < GW && id.y < GH) {\n"
            "    float2 org = float2(id.xy) * float2(bw, bh); float2 st = float2(bw, bh) / (float)kS;\n"
            "    float mB = 0;\n"
            "    [unroll] for (int j = 0; j < kS; j++) [unroll] for (int i = 0; i < kS; i++) {\n"
            "      float2 p = org + (float2(i, j) + 0.5) * st; int2 ip = int2(clamp(p, float2(0, 0), float2(W - 1, H - 1)));\n"
            "      mB += Luma(B.Load(int3(ip, 0))); }\n"
            // a media do quadro ANTERIOR e a que este mesmo passo guardou no quadro passado
            // (mesmo slot, mesmas posicoes de amostra) - identica a reamostrar A, metade dos Loads
            "    uint b = id.y * GW + id.x; float mA = asfloat(med.Load(b * 4)); med.Store(b * 4, asuint(mB));\n"
            "    if (est.Load(4) > 0u && abs(mA - mB) / kSS > thr) { uint t; InterlockedAdd(gCnt, 1u, t); }\n"
            "  }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  if (gi == 0u && gCnt > 0u) { uint t2; stats.InterlockedAdd(4, gCnt, t2); }\n"
            "}\n"
            // uma thread so: a media movel, a decisao e os argumentos do DispatchIndirect
            "[numthreads(1,1,1)]\n"
            "void PassoCorteDecide(uint3 id : SV_DispatchThreadID) {\n"
            "  uint nb = GW * GH, nd = stats.Load(4);\n"
            "  float frac = nb > 0u ? (float)nd / (float)nb : 0.0;\n"
            "  uint quadros = est.Load(4); float ema = quadros == 0u ? frac : asfloat(est.Load(0));\n"
            "  float base = max(ema, piso);\n"
            // duas regras: o SALTO (o que separa corte de cena movimentada) e, em paralelo, o
            // corte OBVIO - quase a tela toda mudou de media de luma; ai basta um salto pequeno.
            "  bool corte = (quadros > 12u) && ((frac > fracMin && frac > salto * base) || (frac > fracAlta && frac > saltoAlta * base));\n"
            "  est.Store(0, asuint(0.85 * ema + 0.15 * frac));\n"
            "  est.Store(4, quadros + 1u);\n"
            "  if (corte) { est.Store(8, est.Load(8) + 1u); est.Store(16, asuint(frac)); est.Store(20, asuint(base)); }\n"
            "  est.Store(12, asuint(frac));\n"
            "  args.Store(0, corte ? GX : 0u); args.Store(4, corte ? GY : 0u); args.Store(8, 1u);\n"
            "}\n"
            // so roda quando houve corte (zero grupos no caso normal): G := B, o quadro real
            "Texture2D<float4> src : register(t0); RWTexture2D<float4> dst : register(u0);\n"
            "[numthreads(8,8,1)]\n"
            "void PassoCorteAplica(uint3 id : SV_DispatchThreadID) {\n"
            "  if (id.x >= W || id.y >= H) return;\n"
            "  dst[id.xy] = src.Load(int3(id.xy, 0));\n"
            "}\n";
    }
    /* ---- A MASCARA DE INTERFACE POR APARENCIA (O2-QUALIDADE, 20/09/2026) - ver CriarHud.
       Cor em 0..1; `thr` = niveis/255. Um so par de passos faz DUAS coisas por cima do G:
       1) DESOCLUSAO (peso por bloco, vindo do PassoA);  2) INTERFACE (confianca por pixel).
       A ordem importa: a interface manda mais que a desoclusao, entao ela vem depois. */
    static const char* HlslHud() {
        return
            "cbuffer C : register(b0) { uint W, H, N, maxProt; float thr, corte, apCon, apFino;\n"
            "  float miraR, miraBonus, desocForca, bw; float bh, p0, p1, p2; uint apar, desocOn, cx, cy; };\n"
            "Texture2D<float4> A : register(t0); Texture2D<float4> B : register(t1);\n"
            "Texture2D<float> confR : register(t2); Texture2D<float> desocR : register(t3);\n"
            "RWTexture2D<uint> run : register(u0); RWByteAddressBuffer stats : register(u1); RWTexture2D<float> conf : register(u2);\n"
            "RWTexture2D<float4> G : register(u0);\n"
            "float Luma(float4 c) { return dot(c.rgb, float3(0.299, 0.587, 0.114)); }\n"
            "int2 Cl(int2 p) { return clamp(p, int2(0, 0), int2((int)W - 1, (int)H - 1)); }\n"
            "float LumB(int2 p) { return Luma(B.Load(int3(Cl(p), 0))); }\n"
            "float Dif(int2 p) { float3 d = abs(A.Load(int3(p, 0)).rgb - B.Load(int3(p, 0)).rgb); return max(d.r, max(d.g, d.b)); }\n"
            "groupshared uint gCnt; groupshared uint gSoma;\n"
            /* 1) CONFIANCA DE INTERFACE, 0..1, POR PIXEL (era: binario "parado ha >= N quadros").
               Tres evidencias, e a APARENCIA manda:
                 con  = contraste local 3x3 (texto e mira tem contraste alto num raio de 1 px);
                 fino = CRISTA: o pixel destoa dos DOIS lados, no eixo x OU no y. E o que separa um
                        TRACO (texto, mira, borda de caixa) de uma BORDA de cena, que so destoa de
                        um lado. Mesma ideia da crista do gate de ambiguidade (PassoA). DUAS
                        ESCALAS, +-2 px e +-4 px: a primeira pega traco de 1 a 3 px (texto de HUD
                        em 1080p/1440p), a segunda de 4 a 7 px (texto grande, HUD de console, 4K).
                        So com a de 2 px um traco grosso tem crista ZERO - os vizinhos caem dentro
                        do proprio traco - e a interface passava batida;
                 tmp  = estabilidade temporal (o `run` de antes), como REFORCO.
               conf = saturate(0,70*ap + 0,30*tmp) * saturate(3*ap), com ap = con*fino.
               O fator final e o que corrige o bug medido em 19/09 (`HUD protegido 100,0% dos
               pixels`): sem aparencia nenhuma - fundo liso parado, ou quadro repetido inteiro -
               a confianca e ZERO por construcao, por mais quadros que ele fique parado. Antes,
               "parado ha 3 quadros" bastava e a tela inteira virava copia do quadro real.
               Repare que a aparencia SOZINHA (ap = 1, tmp = 0) da 0,70 e ja passa do corte 0,5:
               a estabilidade e reforco, nao criterio unico - nos dois sentidos.
               BANCADA: com apar=0 volta o binario temporal de ate 20/09, para medir antes x
               depois com o MESMO exe. */
            "[numthreads(8,8,1)]\n"
            "void PassoHudConta(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {\n"
            "  if (gi == 0u) gCnt = 0u;\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  if (id.x < W && id.y < H) {\n"
            "    int2 p = int2(id.xy);\n"
            "    uint r = (Dif(p) <= thr) ? min(run[p] + 1u, 255u) : 0u;\n"
            "    run[p] = r;\n"
            "    float tmp = saturate((float)r / max((float)N, 1.0));\n"
            "    float c;\n"
            "    if (apar == 0u) { c = (r >= N) ? 1.0 : 0.0; }\n"
            "    else {\n"
            "      float l = LumB(p), lmin = l, lmax = l;\n"
            "      [unroll] for (int j = -1; j <= 1; j++) [unroll] for (int i = -1; i <= 1; i++) {\n"
            "        float q = LumB(p + int2(i, j)); lmin = min(lmin, q); lmax = max(lmax, q); }\n"
            "      float con = saturate((lmax - lmin) / max(apCon, 1e-4));\n"
            "      float xa = LumB(p + int2(-2, 0)), xb = LumB(p + int2(2, 0));\n"
            "      float ya = LumB(p + int2(0, -2)), yb = LumB(p + int2(0, 2));\n"
            "      float rx = max(min(l - xa, l - xb), min(xa - l, xb - l));\n"
            "      float ry = max(min(l - ya, l - yb), min(ya - l, yb - l));\n"
            "      float xc = LumB(p + int2(-4, 0)), xd = LumB(p + int2(4, 0));\n"
            "      float yc = LumB(p + int2(0, -4)), yd = LumB(p + int2(0, 4));\n"
            "      float rx2 = max(min(l - xc, l - xd), min(xc - l, xd - l));\n"
            "      float ry2 = max(min(l - yc, l - yd), min(yc - l, yd - l));\n"
            "      float fino = saturate(max(max(max(rx, ry), max(rx2, ry2)), 0.0) / max(apFino, 1e-4));\n"
            "      float ap = con * fino;\n"
            "      c = saturate(0.70 * ap + 0.30 * tmp) * saturate(ap * 3.0);\n"
            "    }\n"
            "    conf[p] = c;\n"
            "    if (c >= corte) { uint t; InterlockedAdd(gCnt, 1u, t); }\n"
            "  }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  if (gi == 0u && gCnt > 0u) { uint t2; stats.InterlockedAdd(0, gCnt, t2); }\n"
            "}\n"
            /* 2) APLICA. MISTURA POR REGIAO, nao binaria: a confianca do pixel entra junto com a
               MEDIA do bloco de 8x8 (`max` das duas) - e o que faz a borda antisserrilhada do
               texto vir junto com o miolo, sem o remendo de "5 vizinhos parados" de antes.
               peso = saturate((c - corte) / (1 - corte)); G := lerp(G, B, peso).
               MIRA/CENTRO: dentro de um raio pequeno a confianca ganha um bonus MULTIPLICATIVO -
               so ajuda onde ja ha aparencia de traco, entao um centro de tela liso nao congela.
               TETO (X-FG-QUALIDADE): acima de maxProt pixels acima do corte nada e aplicado.
               DESOCLUSAO: ver PassoA. `desoc` guarda, por bloco, 0,5 = nada, > 0,5 = pese para B,
               < 0,5 = pese para A, com a forca em |d - 0,5| * 2. Bilinear na mao (4 Loads numa
               textura de grade) para nao aparecer o quadriculado do bloco. */
            "float DesocEm(int2 p) {\n"
            "  float2 g = (float2(p) + 0.5) / float2(max(bw, 1e-3), max(bh, 1e-3)) - 0.5;\n"
            "  float2 f = frac(g); int2 g0 = int2(floor(g));\n"
            "  float d00 = desocR.Load(int3(g0, 0)), d10 = desocR.Load(int3(g0 + int2(1, 0), 0));\n"
            "  float d01 = desocR.Load(int3(g0 + int2(0, 1), 0)), d11 = desocR.Load(int3(g0 + int2(1, 1), 0));\n"
            "  return lerp(lerp(d00, d10, f.x), lerp(d01, d11, f.x), f.y);\n"
            "}\n"
            "[numthreads(8,8,1)]\n"
            "void PassoHudAplica(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {\n"
            "  if (gi == 0u) { gCnt = 0u; gSoma = 0u; }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  const bool dentro = id.x < W && id.y < H;\n"
            "  float c = 0.0;\n"
            "  if (dentro) { c = confR.Load(int3(id.xy, 0)); uint t0; InterlockedAdd(gSoma, (uint)(c * 255.0 + 0.5), t0); }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  const float cReg = (float)gSoma / (64.0 * 255.0);\n"
            "  const bool teto = stats.Load(0) > maxProt;\n"
            "  if (dentro) {\n"
            "    int2 p = int2(id.xy);\n"
            "    if (desocOn != 0u) {\n"
            "      float d = DesocEm(p); float f = saturate(abs(d - 0.5) * 2.0) * desocForca;\n"
            "      if (f > 0.004) { float4 alvo = (d > 0.5) ? B.Load(int3(p, 0)) : A.Load(int3(p, 0)); G[p] = lerp(G[p], alvo, f); }\n"
            "    }\n"
            "    if (!teto) {\n"
            "      float cc = max(c, cReg);\n"
            "      if (miraR > 0.5) { float2 dd = (float2(p) - float2((float)cx, (float)cy)) / miraR; float r2 = dot(dd, dd);\n"
            "        if (r2 < 1.0) cc = saturate(cc * (1.0 + miraBonus * (1.0 - r2))); }\n"
            "      float w = saturate((cc - corte) / max(1.0 - corte, 1e-3));\n"
            "      if (w > 0.004) { G[p] = lerp(G[p], B.Load(int3(p, 0)), w); uint t; InterlockedAdd(gCnt, (uint)(w * 1024.0 + 0.5), t); }\n"
            "    }\n"
            "  }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  if (gi == 0u && gCnt > 0u) { uint t2; stats.InterlockedAdd(4, gCnt, t2); }\n"
            "}\n"
            /* 3) O MESMO, SEM LER O G: placa sem UAV_TYPED_LOAD no formato do backbuffer nao pode
               fazer lerp com o que ja esta la, entao a mistura vira binaria no corte. Quem escolhe
               o shader e o C++ (ver CriarHud); o resto do caminho e identico. */
            "[numthreads(8,8,1)]\n"
            "void PassoHudAplicaBin(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {\n"
            "  if (gi == 0u) { gCnt = 0u; gSoma = 0u; }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  const bool dentro = id.x < W && id.y < H;\n"
            "  float c = 0.0;\n"
            "  if (dentro) { c = confR.Load(int3(id.xy, 0)); uint t0; InterlockedAdd(gSoma, (uint)(c * 255.0 + 0.5), t0); }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  const float cReg = (float)gSoma / (64.0 * 255.0);\n"
            "  const bool teto = stats.Load(0) > maxProt;\n"
            "  if (dentro) {\n"
            "    int2 p = int2(id.xy);\n"
            "    if (desocOn != 0u) {\n"
            "      float d = DesocEm(p); float f = saturate(abs(d - 0.5) * 2.0) * desocForca;\n"
            "      if (f > 0.5) G[p] = (d > 0.5) ? B.Load(int3(p, 0)) : A.Load(int3(p, 0));\n"
            "    }\n"
            "    if (!teto) {\n"
            "      float cc = max(c, cReg);\n"
            "      if (miraR > 0.5) { float2 dd = (float2(p) - float2((float)cx, (float)cy)) / miraR; float r2 = dot(dd, dd);\n"
            "        if (r2 < 1.0) cc = saturate(cc * (1.0 + miraBonus * (1.0 - r2))); }\n"
            "      if (cc >= corte) { G[p] = B.Load(int3(p, 0)); uint t; InterlockedAdd(gCnt, 1024u, t); }\n"
            "    }\n"
            "  }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  if (gi == 0u && gCnt > 0u) { uint t2; stats.InterlockedAdd(4, gCnt, t2); }\n"
            "}\n";
    }
    // quadro repetido (X-FG-QUALIDADE) - ver CriarDedup. Uma amostra por celula SXxSY (o centro);
    // mudou = max |A-B| por canal acima de thr. Um atomico por grupo de 64 amostras.
    static const char* HlslDedup() {
        return
            "cbuffer C : register(b0) { uint W, H, SX, SY; float thr, thrAlto, thrLuma, p0; uint offLin, offCol, p1, p2; };\n"
            "Texture2D<float4> A : register(t0); Texture2D<float4> B : register(t1);\n"
            "RWByteAddressBuffer stats : register(u0);\n"
            "groupshared uint gCnt, gAlt, gLin, gCol;\n"
            "[numthreads(8,8,1)]\n"
            "void PassoDif(uint3 id : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint gi : SV_GroupIndex) {\n"
            "  if (gi == 0u) { gCnt = 0u; gAlt = 0u; gLin = 0u; gCol = 0u; }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  uint2 p = uint2(id.x * SX + (SX >> 1), id.y * SY + (SY >> 1));\n"
            "  if (p.x < W && p.y < H) {\n"
            "    float3 b = B.Load(int3(p, 0)).rgb;\n"
            "    float3 d = abs(A.Load(int3(p, 0)).rgb - b);\n"
            "    float m = max(d.r, max(d.g, d.b));\n"
            "    uint t;\n"
            "    if (m > thr) InterlockedAdd(gCnt, 1u, t);\n"
            "    if (m > thrAlto) InterlockedAdd(gAlt, 1u, t);\n"
            "    if (dot(b, float3(0.2126, 0.7152, 0.0722)) > thrLuma) {\n"
            "      InterlockedOr(gLin, 1u << (id.y & 7u), t);\n"
            "      InterlockedOr(gCol, 1u << (id.x & 7u), t);\n"
            "    }\n"
            "  }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  if (gi == 0u) {\n"
            "    uint t2;\n"
            "    if (gCnt > 0u) stats.InterlockedAdd(0, gCnt, t2);\n"
            "    if (gAlt > 0u) stats.InterlockedAdd(4, gAlt, t2);\n"
            "    if (gLin > 0u) { uint r0 = gid.y * 8u; stats.InterlockedOr(offLin + ((r0 >> 5u) << 2u), gLin << (r0 & 31u), t2); }\n"
            "    if (gCol > 0u) { uint c0 = gid.x * 8u; stats.InterlockedOr(offCol + ((c0 >> 5u) << 2u), gCol << (c0 & 31u), t2); }\n"
            "  }\n"
            "}\n";
    }
    /* X-GERADO (20/09/2026) - A PROVA DO GERADO, um passo so. Mesma grade de amostras do dedup e
       o MESMO cb (le so W, H, SX, SY, thr). Tres contagens por amostra, tres atomicos por grupo:
         [0] G difere de A   [1] G difere de B   [2] G difere da MISTURA (A+B)/2
       O terceiro e o que separa interpolacao de fusao: um FI que so mistura os dois reais (o que
       sobra quando ele nao tem campo de movimento) da G == (A+B)/2 e o numero [2] vai a ZERO,
       mesmo com [0] e [1] altos. Um FI que de fato ARRASTA o objeto para o meio do caminho poe o
       objeto onde nem A nem a mistura o tem, e [2] sobe junto com os outros dois.
       DESVIO ANOTADO: o pedido era duas passadas do PassoDif do dedup. Uma passada so com tres
       contadores custa metade das leituras de textura e e a unica forma de medir a mistura -
       o PassoDif so olha duas texturas. */
    static const char* HlslProva() {
        return
            "cbuffer C : register(b0) { uint W, H, SX, SY; float thr, p0, p1, p2; };\n"
            "Texture2D<float4> A : register(t0); Texture2D<float4> B : register(t1); Texture2D<float4> G : register(t2);\n"
            "RWByteAddressBuffer stats : register(u0);\n"
            "groupshared uint gA, gB, gM;\n"
            "float Max3(float3 d) { return max(d.r, max(d.g, d.b)); }\n"
            "[numthreads(8,8,1)]\n"
            "void PassoProva(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex) {\n"
            "  if (gi == 0u) { gA = 0u; gB = 0u; gM = 0u; }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  uint2 p = uint2(id.x * SX + (SX >> 1), id.y * SY + (SY >> 1));\n"
            "  if (p.x < W && p.y < H) {\n"
            "    float3 a = A.Load(int3(p, 0)).rgb, b = B.Load(int3(p, 0)).rgb, g = G.Load(int3(p, 0)).rgb;\n"
            "    uint t;\n"
            "    if (Max3(abs(g - a)) > thr) InterlockedAdd(gA, 1u, t);\n"
            "    if (Max3(abs(g - b)) > thr) InterlockedAdd(gB, 1u, t);\n"
            "    if (Max3(abs(g - 0.5 * (a + b))) > thr) InterlockedAdd(gM, 1u, t);\n"
            "  }\n"
            "  GroupMemoryBarrierWithGroupSync();\n"
            "  if (gi == 0u) { uint t2;\n"
            "    if (gA > 0u) stats.InterlockedAdd(0, gA, t2);\n"
            "    if (gB > 0u) stats.InterlockedAdd(4, gB, t2);\n"
            "    if (gM > 0u) stats.InterlockedAdd(8, gM, t2); }\n"
            "}\n";
    }
    // a reducao (duas placas, saida > 1440p): bilinear no centro do texel de destino
    static const char* HlslReduz() {
        return
            "cbuffer C : register(b0) { uint W, H, p0, p1; };\n"
            "Texture2D<float4> srcFull : register(t0); SamplerState samp : register(s0); RWTexture2D<float4> dstFi : register(u0);\n"
            "[numthreads(8,8,1)]\n"
            "void PassoDown(uint3 id : SV_DispatchThreadID) { if (id.x >= W || id.y >= H) return;\n"
            "  dstFi[id.xy] = srcFull.SampleLevel(samp, (float2(id.xy) + 0.5) / float2(W, H), 0); }\n";
    }
    bool CompilarCS(const char* src, const char* entrada, ID3D11ComputeShader** out, std::wstring* erro) {
        typedef HRESULT (WINAPI *PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
        static HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
        PFN_Compile compile = dc ? (PFN_Compile)(void*)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!compile) { if (erro) *erro = L"fg: d3dcompiler_47.dll indisponivel"; return false; }
        ID3DBlob *b = nullptr, *er = nullptr;
        const HRESULT hr = compile(src, strlen(src), "fg", nullptr, nullptr, entrada, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &er);
        if (FAILED(hr) || !b) {
            if (erro) { std::string msg = er ? std::string((const char*)er->GetBufferPointer(), er->GetBufferSize()) : ""; *erro = L"fg: shader nao compilou: " + std::wstring(msg.begin(), msg.end()); }
            Soltar(er); Soltar(b); return false;
        }
        Soltar(er);
        const HRESULT hc = m_dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, out); b->Release();
        if (FAILED(hc)) { Hr(erro, L"CreateComputeShader", hc); return false; }
        return true;
    }
    bool CriarShaders(std::wstring* erro) {
        return CompilarCS(Hlsl(), "PassoA", &m_csA, erro) && CompilarCS(Hlsl(), "PassoA2", &m_csA2, erro) && CompilarCS(Hlsl(), "PassoB", &m_csB, erro);
    }
    bool CriarTex11(UINT w, UINT h, DXGI_FORMAT fmt, UINT bind, bool compartilhar, ID3D11Texture2D** tex, ID3D12Resource** res12, std::wstring* erro, const wchar_t* nome) {
        D3D11_TEXTURE2D_DESC td{}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = bind; td.MiscFlags = compartilhar ? (D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED) : 0;
        HRESULT hr = m_dev->CreateTexture2D(&td, nullptr, tex);
        if (FAILED(hr) || !*tex) { Hr(erro, nome, hr); return false; }
        if (!compartilhar || !res12) return true;
        IDXGIResource1* r1 = nullptr; HANDLE hh = nullptr;
        (*tex)->QueryInterface(__uuidof(IDXGIResource1), (void**)&r1);
        hr = r1 ? r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &hh) : E_FAIL; Soltar(r1);
        if (FAILED(hr) || !hh) { Hr(erro, nome, hr); return false; }
        hr = m_dev12->OpenSharedHandle(hh, __uuidof(ID3D12Resource), (void**)res12); CloseHandle(hh);
        if (FAILED(hr)) { Hr(erro, nome, hr); return false; }
        return true;
    }
    bool AbrirFence(ID3D12Fence* f, ID3D11Fence** out, std::wstring* erro) {
        HANDLE h = nullptr;
        HRESULT hr = m_dev12->CreateSharedHandle(f, nullptr, GENERIC_ALL, nullptr, &h);
        if (FAILED(hr) || !h) { Hr(erro, L"fence CreateSharedHandle", hr); return false; }
        hr = m_dev->OpenSharedFence(h, __uuidof(ID3D11Fence), (void**)out); CloseHandle(h);
        if (FAILED(hr)) { Hr(erro, L"OpenSharedFence", hr); return false; }
        return true;
    }
    bool CriarBuffer12(UINT64 size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES st, ID3D12Resource** out) {
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = heap;
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = size; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
        d.Format = DXGI_FORMAT_UNKNOWN; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return SUCCEEDED(m_dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, st, nullptr, __uuidof(ID3D12Resource), (void**)out));
    }
    static D3D12_RESOURCE_BARRIER Bar(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
        D3D12_RESOURCE_BARRIER x{}; x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.pResource = r; x.Transition.StateBefore = a; x.Transition.StateAfter = b; x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; return x;
    }
    /* X-TDR: teto de espera no caminho do Gerar (thread de ritmo do apresentador) e quanto atraso
       continuo da fila do FI vira "interpolador morto". 50 ms = a mesma regra do Present. */
    static constexpr DWORD  kFiEsperaMs = 50;
    static constexpr double kFiMortoMs  = 10000.0;
    /* X-FONTE-SOLTA: teto de cima do vigia da admissao - o mesmo papel e o mesmo valor do
       `gpu_timeout_ms` do feeder (dlss5-feed.cpp), que e quanto o BeginCommands de la espera a
       placa devolver um alocador. Quem costuma mandar e o 4*T medido no apresentador. */
    static constexpr double kFiVigiaMs  = 2000.0;
    void EsperarFence(ID3D12Fence* f, uint64_t v, DWORD tetoMs = 2000) {
        if (f->GetCompletedValue() >= v || !m_evt) return;
        ResetEvent(m_evt);   // uma espera anterior que estourou pode deixar o evento armado para outro valor
        f->SetEventOnCompletion(v, m_evt);
        // NS-2309 item 5(a): o ResetEvent limpa o sinal VELHO, mas o registro velho ainda pode
        // disparar DURANTE esta espera e acorda-la antes de `v` (NeuralScreen v1.7.1, #33 - ideia,
        // codigo proprio). So a fence decide: reespera o que sobra do teto.
        const ULONGLONG fim = GetTickCount64() + Saida::Teto(tetoMs);
        for (ULONGLONG ag = GetTickCount64(); f->GetCompletedValue() < v && ag < fim; ag = GetTickCount64())
            WaitForSingleObject(m_evt, (DWORD)(fim - ag));
    }
    /* Uso e orcamento de memoria de video LOCAL deste processo, pelo adaptador do device do FI
       (IDXGIAdapter3::QueryVideoMemoryInfo). false = nao deu para ler. */
    bool OrcamentoVram(double* usoMB, double* orcMB) {
        if (!m_dev12) return false;
        const LUID luid = m_dev12->GetAdapterLuid();
        IDXGIFactory4* f4 = nullptr; IDXGIAdapter3* a3 = nullptr; bool ok = false;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&f4)) && f4) {
            if (SUCCEEDED(f4->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter3), (void**)&a3)) && a3) {
                DXGI_QUERY_VIDEO_MEMORY_INFO mi{};
                if (SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mi)) && mi.Budget > 0) {
                    *usoMB = (double)mi.CurrentUsage / 1048576.0; *orcMB = (double)mi.Budget / 1048576.0; ok = true;
                }
                a3->Release();
            }
            f4->Release();
        }
        return ok;
    }
    void EsperarFila() {
        ID3D12Fence* f = nullptr;
        if (FAILED(m_dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&f)) || !f) return;
        m_fila->Signal(f, 1); EsperarFence(f, 1); f->Release();
    }
    void LerTimestamp(int slot) {
        if (!m_tsPend[slot] || !m_qbuf || !m_tsFreq) return;
        m_tsPend[slot] = false;
        // o slot volta a ser usado dois quadros depois: a fOut do quadro em que foi escrito ja passou (esperada acima)
        void* m = nullptr; D3D12_RANGE rg{ (SIZE_T)slot * 16, (SIZE_T)slot * 16 + 16 };
        if (SUCCEEDED(m_qbuf->Map(0, &rg, &m))) {
            const UINT64* t = (const UINT64*)((const uint8_t*)m + slot * 16);
            if (t[1] >= t[0]) { m_somaFi += (double)(t[1] - t[0]) * 1000.0 / (double)m_tsFreq; m_nFi++; }
            D3D12_RANGE none{ 0, 0 }; m_qbuf->Unmap(0, &none);
        }
    }
    static double Ms() { LARGE_INTEGER t, f; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / (double)f.QuadPart; }
    static double QpcMs(int64_t q) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)q * 1000.0 / (double)f.QuadPart; }

    // ---- J5: o anel de depth/MV do jogo (device da rede, aberto no D3D12 do FI) e as fences
    bool LigarDadosJogo(ID3D11Device* devRede, UINT w, UINT h, std::wstring* erro) {
        if (m_dadosOk) {
            /* anel de outro tamanho (sessao renegociada): trocar o anel com o apresentador
               possivelmente dentro do Gerar e soltar textura em uso. A troca de resolucao
               reabre o app de qualquer jeito (ponte: backbuffer no tamanho do quadro); ate la,
               fluxo optico. */
            if (erro) *erro = L"fg: o slot de depth/mv do jogo mudou de tamanho (sessao renegociada)";
            return false;
        }
        SoltarDadosJogo(true);
        ID3D11Device5* d5 = nullptr;
        if (FAILED(devRede->QueryInterface(__uuidof(ID3D11Device5), (void**)&d5)) || !d5) { if (erro) *erro = L"fg: device da rede sem ID3D11Device5 (fences)"; return false; }
        m_devDados = d5;
        for (int i = 0; i < 3; i++) {
            const DXGI_FORMAT fmt[2] = { DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_FLOAT };
            ID3D11Texture2D** t11[2] = { &m_dadosDepthRede[i], &m_dadosMvRede[i] };
            ID3D12Resource** t12[2] = { &m_dadosDepth12[i], &m_dadosMv12[i] };
            for (int k = 0; k < 2; k++) {
                D3D11_TEXTURE2D_DESC td{}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt[k]; td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
                HRESULT hr = devRede->CreateTexture2D(&td, nullptr, t11[k]);
                if (FAILED(hr) || !*t11[k]) { Hr(erro, k ? L"anel de MV do jogo" : L"anel de profundidade do jogo", hr); return false; }
                IDXGIResource1* r1 = nullptr; HANDLE hh = nullptr;
                (*t11[k])->QueryInterface(__uuidof(IDXGIResource1), (void**)&r1);
                hr = r1 ? r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &hh) : E_FAIL; Soltar(r1);
                if (FAILED(hr) || !hh) { Hr(erro, L"anel CreateSharedHandle", hr); return false; }
                hr = m_dev12->OpenSharedHandle(hh, __uuidof(ID3D12Resource), (void**)t12[k]); CloseHandle(hh);
                if (FAILED(hr)) { Hr(erro, L"anel OpenSharedHandle (D3D12)", hr); return false; }
            }
        }
        // fence de ida (a rede sinaliza n quando as copias do slot n%3 estao enfileiradas; o FI espera)
        HRESULT hr = d5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void**)&m_fDadosRede);
        if (FAILED(hr)) { Hr(erro, L"fence dos dados do jogo", hr); return false; }
        HANDLE hf = nullptr; hr = m_fDadosRede->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &hf);
        if (FAILED(hr) || !hf) { Hr(erro, L"fence dos dados CreateSharedHandle", hr); return false; }
        hr = m_dev12->OpenSharedHandle(hf, __uuidof(ID3D12Fence), (void**)&m_fDados12); CloseHandle(hf);
        if (FAILED(hr)) { Hr(erro, L"fence dos dados OpenSharedHandle (D3D12)", hr); return false; }
        // fence de volta: a m_fOut do FI aberta no device da rede (o slot so e reescrito depois de o FI que o leu terminar)
        hf = nullptr; hr = m_dev12->CreateSharedHandle(m_fOut, nullptr, GENERIC_ALL, nullptr, &hf);
        if (FAILED(hr) || !hf) { Hr(erro, L"fOut CreateSharedHandle", hr); return false; }
        hr = d5->OpenSharedFence(hf, __uuidof(ID3D11Fence), (void**)&m_fOutRede); CloseHandle(hf);
        if (FAILED(hr)) { Hr(erro, L"fOut OpenSharedFence (rede)", hr); return false; }
        for (int i = 0; i < 3; i++) m_dadosGerLido[i].store(0);
        m_dadosW = w; m_dadosH = h; m_dadosOk = true;
        if (m_log) m_log("fg: anel de profundidade/MV do jogo ligado (3 x %ux%u, R32F + R16G16F, no device da rede, aberto no D3D12 do FI) - o Gerar usa quando o quadro trouxer 'D'", w, h);
        return true;
    }
    void SoltarDadosJogo(bool soAnel) {
        for (int i = 0; i < 3; i++) { Soltar(m_dadosDepth12[i]); Soltar(m_dadosMv12[i]); Soltar(m_dadosDepthRede[i]); Soltar(m_dadosMvRede[i]); m_dadosGerLido[i].store(0); }
        Soltar(m_fDados12); Soltar(m_fDadosRede); Soltar(m_fOutRede); Soltar(m_devDados);
        m_dadosOk = false; m_dadosW = m_dadosH = 0; m_dadosN.store(0);
        if (!soAnel) { m_dadosDesistiu = false; m_dadosPublicados = 0; m_geradosJogo = 0; m_movJogoQpc.store(0); m_ctxFlagsJogo = 0; m_ctxRecriacoes = 0; m_usouJogoAnt = false; m_somaIdade = 0; m_nIdade = 0; m_dadosRecusas = 0; m_dadosLogou = false; }
    }
    /* as flags de criacao do contexto que ESTE material pede (ffx_framegeneration.h:33-36) */
    static uint32_t FlagsDoMaterial(const MetaJogo& m) {
        uint32_t f = 0;
        if (m.flags & 2u)  f |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
        if (m.flags & 8u)  f |= FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
        if (m.flags & 16u) f |= FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
        return f;
    }
    /* Thread do APRESENTADOR: o pacote do quadro n serve? (mesmas recusas do fg_hook, X-FG-3 §1.5) */
    bool PegarDadosJogo(uint64_t n, MetaJogo* out) {
        if (!m_dadosOk || m_dadosN.load(std::memory_order_acquire) < n) return false;
        MetaJogo m; { std::lock_guard<std::mutex> g(m_muDados); m = m_dadosMeta[n % 3]; }
        if (!m.valido || m.n != n) return false;
        const char* porque = nullptr;
        if (m.flags & 32u)                                   porque = "MV em NDC (XeSS): convencao nao resolvida";
        else if (m.flags & 4u)                               porque = "profundidade invertida desconhecida (contexto do upscaler criado antes do gancho)";
        else if (m.renderW > m_w || m.renderH > m_h)         porque = "resolucao interna maior que o tamanho do FI";
        else if (m.mvW > m_w || m.mvH > m_h)                 porque = "MV maior que o tamanho do FI";
        else if ((m.flags & 8u) && (m.mvW != m_w || m.mvH != m_h)) porque = "MV dito de tela mas com outro tamanho";
        else if (!(m.flags & 8u) && (m.mvW != m.renderW || m.mvH != m.renderH)) porque = "MV nem de render nem de tela";
        else if (!(m.mvScaleX == m.mvScaleX) || !(m.mvScaleY == m.mvScaleY) || m.mvScaleX == 0 || m.mvScaleY == 0) porque = "escala de MV invalida";
        if (porque) { if (m_log && m_dadosRecusas < 5) { m_dadosRecusas++; m_log("fg: dados do jogo recusados (%s) - este quadro vai no fluxo optico", porque); } return false; }
        *out = m;
        return true;
    }
    /* O material do jogo vem descrito nas flags de CRIACAO do contexto (profundidade invertida,
       MV em tela, jitter embutido), nao no dispatch. Aqui o contexto e do app, nao do jogo, e a
       unica thread que o usa e esta (a do apresentador): recriar e seguro depois de esperar a
       fila - custa ~0,5 s UMA vez, na primeira chegada do dado, e e o que o fg_hook nao podia
       fazer (la havia proxies em voo). Limite de 3 recriacoes por sessao; um material que fica
       trocando cai no fluxo optico. */
    /* X-FG-6: o segundo contexto e as saidas de 1/4 e 3/4, na primeira vez que o 3x/4x e pedido.
       Thread do apresentador, fora de qualquer lista aberta. Falhou uma vez = fica em 2x. */
    bool GarantirCtx2() {
        if (m_fg2) return true;
        if (m_ctx2Falhou || !m_dev || !m_dev12) return false;
        const double t0 = Ms();
        std::wstring erro;
        /* X-TDR (19/09/2026): o 2o contexto nasce com o DEVICE VIVO, a FILA DO FI EM DIA e MEMORIA DE
           VIDEO para ele. No COD do dono ele nasceu 60 ms depois de um "present travou 2377 ms" com a
           cadeia inteira em voo, e o processo ja estava a 108% do orcamento de VRAM (o contexto do FI
           sao 288 MB, o 2o mais ~300 MB com as saidas G1/4 e G3/4). Sem folga: fica em 2x nesta sessao
           (o main avisa uma vez), e o log diz quanto faltou. */
        if (m_dev12->GetDeviceRemovedReason() != S_OK) { m_ctx2Falhou = true; if (m_log) m_log("fg: 3x/4x indisponivel (device do FI removido) - seguindo em 2x"); return false; }
        EsperarFila();   // nada do FI em voo enquanto o contexto nasce (<= 2 s; em regime e ~0)
        {
            double usoMB = 0.0, orcMB = 0.0;
            if (OrcamentoVram(&usoMB, &orcMB)) {
                const double precisaMB = m_memMB + 4.0 * ((double)m_w * m_h * 4.0 / 1048576.0) + 16.0;   // 2o contexto + outG14/outG34 x2 + mvZero
                if (usoMB + precisaMB > orcMB * 0.92) {
                    m_ctx2Falhou = true;
                    if (m_log) m_log("fg: 3x/4x indisponivel (memoria de video: %.0f MB usados + ~%.0f MB do 2o contexto passariam de 92%% do orcamento de %.0f MB do processo) - seguindo em 2x nesta sessao", usoMB, precisaMB, orcMB);
                    return false;
                }
                if (m_log) m_log("fg: memoria de video antes do 2o contexto: %.0f MB usados de %.0f MB de orcamento (+~%.0f MB)", usoMB, orcMB, precisaMB);
            }
        }
        for (int i = 0; i < kSlots && erro.empty(); i++) {
            if (!CriarTex11(m_w, m_h, m_fmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, true, &m_outG14[i], &m_outG14_12[i], &erro, L"outG14")) break;
            if (!CriarTex11(m_w, m_h, m_fmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, true, &m_outG34[i], &m_outG34_12[i], &erro, L"outG34")) break;
        }
        if (erro.empty() && !CriarTex11(m_w, m_h, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, true, &m_mvZero, &m_mvZero12, &erro, L"mvZero")) {}
        for (int i = 0; i < kSlots && erro.empty(); i++)
            if (FAILED(m_dev->CreateUnorderedAccessView(m_outG14[i], nullptr, &m_outG14Uav[i])) || FAILED(m_dev->CreateUnorderedAccessView(m_outG34[i], nullptr, &m_outG34Uav[i]))) erro = L"UAV de outG14/outG34";
        if (erro.empty()) {
            ID3D11UnorderedAccessView* u = nullptr; ID3D11DeviceContext* c = nullptr;
            m_dev->GetImmediateContext(&c);
            if (c && SUCCEEDED(m_dev->CreateUnorderedAccessView(m_mvZero, nullptr, &u)) && u) { const float z[4] = { 0, 0, 0, 0 }; c->ClearUnorderedAccessViewFloat(u, z); c->Flush(); }
            Soltar(u); Soltar(c);
            ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = m_dev12;
            ffxCreateContextDescFrameGeneration cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; cd.header.pNext = &be.header;
            cd.flags = 0; cd.displaySize = { m_w, m_h }; cd.maxRenderSize = { m_w, m_h }; cd.backBufferFormat = ffxApiGetSurfaceFormatDX12(m_fmt);
            const ffxReturnCode_t rc = m_Create(&m_fg2, &cd.header, nullptr);
            if (rc != FFX_API_RETURN_OK || !m_fg2) { m_fg2 = nullptr; wchar_t b[96]; _snwprintf(b, 96, L"ffxCreateContext (2o contexto) devolveu %u", (unsigned)rc); b[95] = 0; erro = b; }
            else {
                ffxConfigureDescFrameGeneration cf{}; cf.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION; cf.frameGenerationEnabled = true; cf.allowAsyncWorkloads = g_async;
                cf.generationRect = { 0, 0, (int32_t)m_w, (int32_t)m_h }; cf.frameID = 0; m_Config(&m_fg2, &cf.header);
            }
        }
        if (!erro.empty()) {
            m_ctx2Falhou = true; SoltarCtx2();
            if (m_log) m_log("fg: 3x/4x indisponivel (%ls) - seguindo em 2x", erro.c_str());
            return false;
        }
        m_frameId2 = 0; m_ctx2Quente = false;
        if (m_log) m_log("fg: segundo contexto do FI criado em %.0f ms (3x/4x: G1/4 e G3/4 por chamadas encadeadas, 3 despachos por intervalo)", Ms() - t0);
        return true;
    }
    /* W-153 (21/09/2026): AS TEXTURAS DOS GERADOS EXTRAS NAO MORREM NA HORA. O apresentador guarda
       em `m_pend` ponteiros crus para G1/4, G3/4 (e os de 1/8) do par ANTERIOR e os mostra depois de
       Gerar() voltar - e e dentro de Gerar() que 4x/Tela -> 2x solta o 2o contexto. Soltar as
       texturas ali era um Mostrar() com textura destruida: 0xC0000005 em nvwgf2umx.dll (D5-APP-202,
       bancada 14:41). O contexto do FFX e as UAVs/aliases D3D12 vao embora ja (ninguem fora daqui
       os usa); as texturas D3D11 esperam kSlots geracoes no cemiterio - a mesma garantia do anel de
       slots, que o apresentador ja respeita - e saem no Gerar() seguinte ou no Parar(). */
    void Aposentar(ID3D11Texture2D*& t) { if (t) { m_velhas.push_back(t); t = nullptr; m_velhasEm = m_ger; } }
    void EnterrarVelhas(bool agora) {
        if (m_velhas.empty() || (!agora && m_ger < m_velhasEm + kSlots)) return;
        for (auto* p : m_velhas) p->Release();
        m_velhas.clear();
    }
    void SoltarCtx2() {
        if (m_fg2) { m_Destroy(&m_fg2, nullptr); m_fg2 = nullptr; }
        for (int i = 0; i < kSlots; i++) { Soltar(m_outG14Uav[i]); Soltar(m_outG34Uav[i]); Soltar(m_outG14_12[i]); Aposentar(m_outG14[i]); Soltar(m_outG34_12[i]); Aposentar(m_outG34[i]); }
        Soltar(m_mvZero12); Soltar(m_mvZero);
        m_frameId2 = 0; m_ctx2Quente = false;
    }
    /* um despacho do segundo contexto: prepare (depth constante, MV=0) + FI com `cor` como quadro
       "atual" e `out` como saida (o ponto medio entre o anterior visto por ele e `cor`) */
    void DespacharCtx2(ID3D12Resource* cor, ID3D12Resource* out, float dtMs, bool reset) { DespacharCtxN(m_fg2, m_frameId2, cor, out, dtMs, reset, "2o"); }
    void DespacharCtxN(ffxContext& fg, uint32_t& frameId, ID3D12Resource* cor, ID3D12Resource* out, float dtMs, bool reset, const char* nome) {
        ffxDispatchDescFrameGenerationPrepareCameraInfo cam{}; cam.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
        cam.cameraUp[1] = 1; cam.cameraRight[0] = 1; cam.cameraForward[2] = 1;
        ffxDispatchDescFrameGenerationPrepare pp{}; pp.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE; pp.header.pNext = &cam.header;
        pp.frameID = frameId; pp.commandList = m_lista;
        pp.renderSize = { m_w, m_h }; pp.jitterOffset = { 0, 0 }; pp.motionVectorScale = { 1, 1 };
        pp.frameTimeDelta = dtMs; pp.unused_reset = reset;
        pp.cameraNear = 0.1f; pp.cameraFar = 1000.0f; pp.cameraFovAngleVertical = 1.0f; pp.viewSpaceToMetersFactor = 1.0f;
        pp.depth = ffxApiGetResourceDX12(m_depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        pp.motionVectors = ffxApiGetResourceDX12(m_mvZero12, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        const ffxReturnCode_t rcP = m_Dispatch(&fg, &pp.header);
        ffxDispatchDescFrameGeneration dd{}; dd.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION; dd.commandList = m_lista;
        dd.presentColor = ffxApiGetResourceDX12(cor, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.outputs[0] = ffxApiGetResourceDX12(out, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dd.numGeneratedFrames = 1; dd.reset = reset; dd.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        dd.minMaxLuminance[0] = 0; dd.minMaxLuminance[1] = 1; dd.generationRect = { m_lbX, m_lbY, m_lbW, m_lbH }; dd.frameID = frameId;   // N5: o mesmo retangulo ativo do 1o contexto
        const ffxReturnCode_t rcD = m_Dispatch(&fg, &dd.header);
        frameId++;
        if ((rcP != FFX_API_RETURN_OK || rcD != FFX_API_RETURN_OK) && m_log && m_errosLog < 5) { m_errosLog++; m_log("fg: ffxDispatch (%s contexto) devolveu prepare=%u fg=%u", nome, (unsigned)rcP, (unsigned)rcD); }
    }
    /* META (21/09/2026): o TERCEIRO contexto e as quatro saidas de 1/8, 3/8, 5/8 e 7/8 - na
       primeira vez que 8x e pedido. Mesmas regras do 2o: device vivo, fila em dia, VRAM com
       folga; falhou uma vez = fica em 4x nesta sessao. */
    bool GarantirCtx3() {
        if (m_fg3) return true;
        if (m_ctx3Falhou || !m_dev || !m_dev12 || !m_fg2 || !m_mvZero12) return false;
        const double t0 = Ms();
        std::wstring erro;
        if (m_dev12->GetDeviceRemovedReason() != S_OK) { m_ctx3Falhou = true; if (m_log) m_log("fg: 8x indisponivel (device do FI removido) - seguindo em 4x"); return false; }
        EsperarFila();
        {
            double usoMB = 0.0, orcMB = 0.0;
            if (OrcamentoVram(&usoMB, &orcMB)) {
                const double precisaMB = m_memMB + 4.0 * (double)kSlots * ((double)m_w * m_h * 4.0 / 1048576.0) + 16.0;   // 3o contexto + 4 saidas x kSlots
                if (usoMB + precisaMB > orcMB * 0.92) {
                    m_ctx3Falhou = true;
                    if (m_log) m_log("fg: 8x indisponivel (memoria de video: %.0f MB usados + ~%.0f MB do 3o contexto passariam de 92%% do orcamento de %.0f MB do processo) - seguindo em 4x nesta sessao", usoMB, precisaMB, orcMB);
                    return false;
                }
                if (m_log) m_log("fg: memoria de video antes do 3o contexto: %.0f MB usados de %.0f MB de orcamento (+~%.0f MB)", usoMB, orcMB, precisaMB);
            }
        }
        static const wchar_t* const nomes[4] = { L"outG18", L"outG38", L"outG58", L"outG78" };
        for (int i = 0; i < 4 && erro.empty(); i++)
            for (int s = 0; s < kSlots && erro.empty(); s++)
                if (!CriarTex11(m_w, m_h, m_fmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, true, &m_outG8[i][s], &m_outG8_12[i][s], &erro, nomes[i])) break;
        for (int i = 0; i < 4 && erro.empty(); i++)
            for (int s = 0; s < kSlots && erro.empty(); s++)
                if (FAILED(m_dev->CreateUnorderedAccessView(m_outG8[i][s], nullptr, &m_outG8Uav[i][s]))) erro = L"UAV de outG18..78";
        if (erro.empty()) {
            ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = m_dev12;
            ffxCreateContextDescFrameGeneration cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; cd.header.pNext = &be.header;
            cd.flags = 0; cd.displaySize = { m_w, m_h }; cd.maxRenderSize = { m_w, m_h }; cd.backBufferFormat = ffxApiGetSurfaceFormatDX12(m_fmt);
            const ffxReturnCode_t rc = m_Create(&m_fg3, &cd.header, nullptr);
            if (rc != FFX_API_RETURN_OK || !m_fg3) { m_fg3 = nullptr; wchar_t b[96]; _snwprintf(b, 96, L"ffxCreateContext (3o contexto) devolveu %u", (unsigned)rc); b[95] = 0; erro = b; }
            else {
                ffxConfigureDescFrameGeneration cf{}; cf.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION; cf.frameGenerationEnabled = true; cf.allowAsyncWorkloads = g_async;
                cf.generationRect = { 0, 0, (int32_t)m_w, (int32_t)m_h }; cf.frameID = 0; m_Config(&m_fg3, &cf.header);
            }
        }
        if (!erro.empty()) {
            m_ctx3Falhou = true; SoltarCtx3();
            if (m_log) m_log("fg: 8x indisponivel (%ls) - seguindo em 4x", erro.c_str());
            return false;
        }
        m_frameId3 = 0; m_ctx3Quente = false;
        if (m_log) m_log("fg: terceiro contexto do FI criado em %.0f ms (8x: G1/8, G3/8, G5/8 e G7/8 por chamadas encadeadas, 7 despachos por intervalo)", Ms() - t0);
        return true;
    }
    void SoltarCtx3() {
        if (m_fg3) { m_Destroy(&m_fg3, nullptr); m_fg3 = nullptr; }
        for (int i = 0; i < 4; i++) for (int s = 0; s < kSlots; s++) { Soltar(m_outG8Uav[i][s]); Soltar(m_outG8_12[i][s]); Aposentar(m_outG8[i][s]); }
        m_frameId3 = 0; m_ctx3Quente = false;
    }

    bool RecriarContexto(uint32_t flags) {
        if (m_ctxRecriacoes >= 3) return false;
        m_ctxRecriacoes++;
        const double t0 = Ms();
        EsperarFila();
        if (m_fg) { m_Destroy(&m_fg, nullptr); m_fg = nullptr; }
        ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = m_dev12;
        ffxCreateContextDescFrameGeneration cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; cd.header.pNext = &be.header;
        cd.flags = flags; cd.displaySize = { m_w, m_h }; cd.maxRenderSize = { m_w, m_h }; cd.backBufferFormat = ffxApiGetSurfaceFormatDX12(m_fmt);
        ffxReturnCode_t rc = m_Create(&m_fg, &cd.header, nullptr);
        if (rc != FFX_API_RETURN_OK || !m_fg) {
            m_fg = nullptr;
            if (m_log) m_log("fg: ffxCreateContext com as flags do jogo (0x%X) devolveu %u; refazendo o contexto de sempre", flags, (unsigned)rc);
            cd.flags = 0; rc = m_Create(&m_fg, &cd.header, nullptr);
            if (rc != FFX_API_RETURN_OK || !m_fg) { m_fg = nullptr; m_contexto = false; if (m_log) m_log("fg: o contexto nao voltou (%u) - fluidez interpolada MORTA nesta sessao", (unsigned)rc); return false; }
            m_ctxFlagsJogo = 0; m_dadosDesistiu = true; m_frameId = 0;
            return false;
        }
        ffxConfigureDescFrameGeneration cf{}; cf.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION; cf.frameGenerationEnabled = true; cf.allowAsyncWorkloads = g_async;
        cf.generationRect = { 0, 0, (int32_t)m_w, (int32_t)m_h }; cf.frameID = 0; m_Config(&m_fg, &cf.header);
        m_ctxFlagsJogo = flags; m_frameId = 0;   /* historia zerada: a semantica da entrada mudou */
        if (m_log) m_log("fg: contexto do FI recriado com as flags do material do jogo (0x%X: depth_inv=%d, mv_tela=%d, mv_com_jitter=%d) em %.0f ms",
                         flags, (flags & FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED) ? 1 : 0, (flags & FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) ? 1 : 0,
                         (flags & FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION) ? 1 : 0, Ms() - t0);
        return true;
    }
    static void Hr(std::wstring* erro, const wchar_t* onde, HRESULT hr) {
        if (!erro) return; wchar_t b[160]; _snwprintf(b, 160, L"fg: %ls falhou (0x%08lX)", onde, (unsigned long)hr); b[159] = 0; *erro = b;
    }
    /* DLSS5_FG_DUMP=<pasta> DLSS5_FG_DUMP_FRAME=N: grava A, B e G do quadro N em PPM.
       FG-QUALIDADE2: uma amostra so nao e medida - dela nao sai nem mediana nem pior caso. Duas
       chaves novas fazem o despejo virar uma SERIE, com o numero do quadro no nome (que e o que
       o cmp-qualidade usa para casar cada trio com a verdade do alvo):
         DLSS5_FG_DUMP_N=<K>      quantos trios (padrao 1 = o de sempre);
         DLSS5_FG_DUMP_PASSO=<M>  de quantos em quantos quadros (padrao 30, ~1 s a 30 fps).
       O ESPACAMENTO NAO E ENFEITE. Cada trio sao tres Map sincronos no contexto imediato do
       apresentador, que ja tem o Present e a espera do FI na frente: ~200 ms de pausa (o mesmo
       custo medido em 13/09 para o Map do corte). A 30 quadros de distancia isso acontece uma
       vez por segundo e o par SEGUINTE e que sofre - e esse par sai da conta sozinho, porque a
       captura pula um quadro e o comparador descarta a amostra cujo par nao e iB = iA + 2.
       Apertar o passo nao faz mais amostras, faz mais descartes. */
    void Dump(ID3D11DeviceContext* ctx, int cur, int prev, uint64_t n) {
        wchar_t pasta[MAX_PATH]{}, b[16]{};
        const bool relato = m_relatoPedido.exchange(false);
        if (relato) wcsncpy(pasta, m_relatoPasta, MAX_PATH - 1);
        else {
            if (m_dumpFeito) return;
            if (!GetEnvironmentVariableW(L"DLSS5_FG_DUMP", pasta, MAX_PATH)) { m_dumpFeito = true; return; }
            const uint64_t alvo = GetEnvironmentVariableW(L"DLSS5_FG_DUMP_FRAME", b, 16) ? (uint64_t)_wtoi(b) : 120;
            wchar_t e[16]{};
            const unsigned quantos = GetEnvironmentVariableW(L"DLSS5_FG_DUMP_N", e, 16) ? (unsigned)std::max(1, _wtoi(e)) : 1u;
            const unsigned passo   = GetEnvironmentVariableW(L"DLSS5_FG_DUMP_PASSO", e, 16) ? (unsigned)std::max(1, _wtoi(e)) : 30u;
            if (m_dumpProx == 0) m_dumpProx = alvo;
            if (n < m_dumpProx) return;
            m_dumpProx = n + passo;   // conta a partir do quadro que SAIU, nao do que se pediu
            if (++m_dumpFeitos >= quantos) m_dumpFeito = true;
        }
        // (relato: cria bin\relatos e a pasta do dia)
        if (relato) { std::wstring p = pasta; const size_t k = p.rfind(L'\\'); if (k != std::wstring::npos) CreateDirectoryW(p.substr(0, k).c_str(), nullptr); }
        CreateDirectoryW(pasta, nullptr);
        struct { const wchar_t* nome; ID3D11Texture2D* t; } q[3] = { { L"A", m_inB[prev] }, { L"B", m_inB[cur] }, { L"G", m_outG[cur] } };
        for (auto& e : q) {
            D3D11_TEXTURE2D_DESC td{}; e.t->GetDesc(&td); td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.MiscFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ID3D11Texture2D* st = nullptr; if (FAILED(m_dev->CreateTexture2D(&td, nullptr, &st))) continue;
            ctx->CopyResource(st, e.t);
            D3D11_MAPPED_SUBRESOURCE m{};
            if (SUCCEEDED(ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
                wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%s\\fg_%llu_%s.ppm", pasta, (unsigned long long)n, e.nome); p[MAX_PATH - 1] = 0;
                FILE* f = _wfopen(p, L"wb");
                if (f) {
                    fprintf(f, "P6\n%u %u\n255\n", td.Width, td.Height);
                    std::vector<uint8_t> row((size_t)td.Width * 3);
                    for (UINT y = 0; y < td.Height; y++) { const uint8_t* s = (const uint8_t*)m.pData + (size_t)y * m.RowPitch; for (UINT x = 0; x < td.Width; x++) { row[x * 3] = s[x * 4 + 2]; row[x * 3 + 1] = s[x * 4 + 1]; row[x * 3 + 2] = s[x * 4]; } fwrite(row.data(), 1, row.size(), f); }
                    fclose(f);
                }
                ctx->Unmap(st, 0);
            }
            st->Release();
        }
        if (m_log) m_log("fg: DUMP do quadro %llu (A, B, G) em %s", (unsigned long long)n, std::string(pasta, pasta + wcslen(pasta)).c_str());
    }

    LogFn m_log = nullptr;
    bool m_contexto = false, m_campoOk = false, m_campoTentou = false, m_mvZerado = false, m_dumpFeito = false;
    UINT m_w = 0, m_h = 0, m_gw = 0, m_gh = 0; float m_covU = 1.f, m_covV = 1.f; DXGI_FORMAT m_fmt = DXGI_FORMAT_UNKNOWN;
    HMODULE m_dll = nullptr;
    PfnFfxCreateContext m_Create = nullptr; PfnFfxDestroyContext m_Destroy = nullptr; PfnFfxDispatch m_Dispatch = nullptr; PfnFfxQuery m_Query = nullptr; PfnFfxConfigure m_Config = nullptr;
    ffxContext m_fg = nullptr; double m_memMB = 0;
    ID3D12Device* m_dev12 = nullptr; ID3D12CommandQueue* m_fila = nullptr; ID3D12CommandAllocator* m_alloc[kSlots] = {}; ID3D12GraphicsCommandList* m_lista = nullptr;
    ID3D12Fence* m_fIn = nullptr; ID3D12Fence* m_fOut = nullptr; HANDLE m_evt = nullptr;
    ID3D12Fence* m_fDreno = nullptr; HANDLE m_evtDreno = nullptr; uint64_t m_vDreno = 0;   // RESET-DRENO: so a drenagem antes de uma troca
    ID3D12QueryHeap* m_qh = nullptr; ID3D12Resource* m_qbuf = nullptr; UINT64 m_tsFreq = 0; bool m_tsPend[kSlots] = {}; double m_somaFi = 0; uint64_t m_nFi = 0;
    ID3D12Resource* m_depth = nullptr;
    ID3D11Device5* m_dev = nullptr; ID3D11Device5* m_devRede = nullptr;
    UINT m_fw = 0, m_fh = 0; bool m_reduz = false;   // tamanho do QUADRO (m_w/m_h = resolucao interna do FI)
    ID3D11Texture2D* m_inB[kSlots] = {}; ID3D11ShaderResourceView* m_inBSrv[kSlots] = {};                                   // quadro inteiro (Present)
    ID3D11Texture2D* m_inBFi[kSlots] = {}; ID3D12Resource* m_inBFi12[kSlots] = {}; ID3D11ShaderResourceView* m_inBFiSrv[kSlots] = {};   // o que o FI le
    ID3D11ComputeShader* m_csDown = nullptr; ID3D11SamplerState* m_samp = nullptr; ID3D11Buffer* m_cbDown = nullptr;
    // O2-QUALIDADE: peso de desoclusao POR BLOCO, escrito pelo PassoA e lido pelo passo de
    // aplicacao. 0,5 = nada; > 0,5 = pese o gerado para B; < 0,5 = para A.
    ID3D11Texture2D* m_desoc = nullptr; ID3D11UnorderedAccessView* m_desocUav = nullptr; ID3D11ShaderResourceView* m_desocSrv = nullptr;
    ID3D11Texture2D* m_outG[kSlots] = {}; ID3D12Resource* m_outG12[kSlots] = {};
    ID3D11Texture2D* m_mv = nullptr; ID3D12Resource* m_mv12 = nullptr; ID3D11UnorderedAccessView* m_mvUav = nullptr;
    // ---- X-FG-6 (3x/4x): o SEGUNDO contexto do FI e as saidas de 1/4 e 3/4 (ver Multiplicador)
    std::atomic<int> m_multQuer{ 2 };                                        // o que o apresentador pediu (2..4)
    int m_mult = 2;                                                          // o que vale neste Gerar
    ffxContext m_fg2 = nullptr; uint32_t m_frameId2 = 0; bool m_ctx2Quente = false, m_ctx2Falhou = false;
    ID3D11Texture2D* m_outG14[kSlots] = {}; ID3D12Resource* m_outG14_12[kSlots] = {};
    ID3D11Texture2D* m_outG34[kSlots] = {}; ID3D12Resource* m_outG34_12[kSlots] = {};
    ID3D11UnorderedAccessView* m_outG14Uav[kSlots] = {}; ID3D11UnorderedAccessView* m_outG34Uav[kSlots] = {};   // o corte G := B tambem neles
    ID3D11Texture2D* m_mvZero = nullptr; ID3D12Resource* m_mvZero12 = nullptr;   // MV = 0 para os sub-quadros (o campo do VME e de B, nao de G1/2)
    /* META (21/09/2026): o 3o contexto (8x) e as saidas de 1/8, 3/8, 5/8, 7/8 (indice 0..3) */
    ffxContext m_fg3 = nullptr; uint32_t m_frameId3 = 0; bool m_ctx3Quente = false, m_ctx3Falhou = false;
    ID3D11Texture2D* m_outG8[4][kSlots] = {}; ID3D12Resource* m_outG8_12[4][kSlots] = {}; ID3D11UnorderedAccessView* m_outG8Uav[4][kSlots] = {};
    uint64_t m_geradosExtra = 0;
    ID3D11Texture2D* m_campoRede[3] = {}; ID3D11Texture2D* m_campoApres[3] = {}; ID3D11ShaderResourceView* m_campoSrv[3] = {};
    ID3D11Fence* m_fCampoRede = nullptr; ID3D11Fence* m_fCampoApres = nullptr; std::atomic<uint64_t> m_campoN{ 0 };
    // ---- J5: profundidade/MV reais do jogo, pela ponte (ver PublicarDadosJogo)
    ID3D11Device5* m_devDados = nullptr;                                     // o device da rede (ref nossa)
    ID3D11Texture2D* m_dadosDepthRede[3] = {}; ID3D11Texture2D* m_dadosMvRede[3] = {};   // o anel, no device da rede
    ID3D12Resource* m_dadosDepth12[3] = {}; ID3D12Resource* m_dadosMv12[3] = {};         // o mesmo anel, aberto no D3D12 do FI
    ID3D11Fence* m_fDadosRede = nullptr; ID3D12Fence* m_fDados12 = nullptr;              // ida: rede sinaliza n, FI espera
    ID3D11Fence* m_fOutRede = nullptr;                                                   // volta: a m_fOut do FI, no device da rede
    std::atomic<uint64_t> m_dadosGerLido[3] = {};                                        // m_ger do FI que leu cada slot por ultimo
    std::mutex m_muDados; MetaJogo m_dadosMeta[3];
    std::atomic<uint64_t> m_dadosN{ 0 };
    UINT m_dadosW = 0, m_dadosH = 0; bool m_dadosOk = false, m_dadosDesistiu = false, m_dadosLogou = false, m_usouJogoAnt = false;
    uint32_t m_ctxFlagsJogo = 0; int m_ctxRecriacoes = 0, m_dadosRecusas = 0;
    uint64_t m_dadosPublicados = 0, m_geradosJogo = 0;
    std::atomic<int64_t> m_movJogoQpc{ 0 };
    double m_somaIdade = 0.0; uint64_t m_nIdade = 0;                                     // dispatch do upscaler -> FI (ms)
    ID3D11Texture2D* m_bloco = nullptr; ID3D11UnorderedAccessView* m_blocoUav = nullptr; ID3D11ShaderResourceView* m_blocoSrv = nullptr;
    ID3D11Texture2D* m_bloco2 = nullptr; ID3D11UnorderedAccessView* m_bloco2Uav = nullptr; ID3D11ShaderResourceView* m_bloco2Srv = nullptr;
    ID3D11Texture2D* m_run = nullptr; ID3D11UnorderedAccessView* m_runUav = nullptr;
    ID3D11Buffer* m_stats = nullptr; ID3D11UnorderedAccessView* m_statsUav = nullptr; ID3D11Buffer* m_statsStg[kSlots] = {}; bool m_statsPend[kSlots] = {}; ID3D11Buffer* m_cb = nullptr;
    ID3D11ComputeShader* m_csA = nullptr; ID3D11ComputeShader* m_csA2 = nullptr; ID3D11ComputeShader* m_csB = nullptr;
    ID3D11Fence* m_fIn11 = nullptr; ID3D11Fence* m_fOut11 = nullptr;
    PonteGpu::MedidorGpu m_medPasses;
    // ---- CORTE DE CENA SEM CAMPO (MV=0, o modo de DUAS PLACAS). Ver o bloco "PassoCorte".
    ID3D11ComputeShader* m_csCorte = nullptr; ID3D11ComputeShader* m_csCorteDec = nullptr; ID3D11ComputeShader* m_csCorteAp = nullptr;
    ID3D11Buffer* m_cbCorte = nullptr;
    ID3D11Buffer* m_corteStats = nullptr; ID3D11UnorderedAccessView* m_corteUav = nullptr;
    ID3D11Buffer* m_corteEst = nullptr;   ID3D11UnorderedAccessView* m_corteEstUav = nullptr;
    ID3D11Buffer* m_corteArgs = nullptr;  ID3D11UnorderedAccessView* m_corteArgsUav = nullptr;
    ID3D11Buffer* m_corteMed = nullptr;   ID3D11UnorderedAccessView* m_corteMedUav = nullptr;
    ID3D11UnorderedAccessView* m_outGUav[kSlots] = {};
    ID3D11Buffer* m_corteStg[kSlots] = {}; bool m_cortePend[kSlots] = {};
    UINT m_cgw = 0, m_cgh = 0; double m_corteFrac = 0.0, m_corteSalto = 0.0, m_corteFracMin = 0.0;
    struct CorteCB { UINT W, H, GW, GH; float bw, bh, thr, salto; float fracMin, piso, fracAlta, saltoAlta; UINT GX, GY, u0, u1; };
    CorteCB m_corteCc{};                       // copia na CPU do cb do detector (reescrita por quadro)
    bool   m_corteSaltoFixo = false;           // a bancada fixou o salto por env -> nao varia com a taxa
    double m_corteSaltoAltaBase = 2.5, m_corteSaltoAltaAtual = 2.5;
    double m_corteSaltoMin = 4.0, m_corteSaltoAltaMin = 2.0;
    UINT m_corteGrade = 24, m_corteAmostra = 4; bool m_corteTelem = true;
    bool m_corteLigado = true; uint64_t m_cortesSemCampo = 0, m_cortesVistos = 0;
    PonteGpu::MedidorGpu m_medCorte;
    // ---- MASCARA DE HUD DEPOIS DO FI (X-HUD-PISCA). Ver CriarHud / HlslHud.
    ID3D11ComputeShader* m_csHudConta = nullptr; ID3D11ComputeShader* m_csHudAplica = nullptr;
    ID3D11Texture2D* m_hudRun = nullptr; ID3D11UnorderedAccessView* m_hudRunUav = nullptr; ID3D11ShaderResourceView* m_hudRunSrv = nullptr;
    // O2-QUALIDADE: a confianca continua 0..1 por pixel (aparencia + reforco temporal)
    ID3D11Texture2D* m_hudConf = nullptr; ID3D11UnorderedAccessView* m_hudConfUav = nullptr; ID3D11ShaderResourceView* m_hudConfSrv = nullptr;
    ID3D11UnorderedAccessView* m_hudGUav[kSlots] = {};
    ID3D11Buffer* m_hudStats = nullptr; ID3D11UnorderedAccessView* m_hudStatsUav = nullptr;
    ID3D11Buffer* m_hudStg[kSlots] = {}; bool m_hudPend[kSlots] = {};
    ID3D11Buffer* m_cbHud = nullptr;
    bool m_hudLigado = true, m_hudLimpar = true; unsigned m_hudLimiar = 3, m_hudN = 3; double m_hudFrac = 0.0;
    double m_hudParados = 0.0, m_hudMaxFrac = kHudMaxFrac;   // X-FG-QUALIDADE: fracao acima do corte; teto de aplicacao
    // O2-QUALIDADE: m_hudLigado = o par de passos existe; m_hudMascara = a parte de interface;
    // m_desocLigado = a parte de desoclusao. Os dois desligados => o par nem e criado.
    bool m_hudMascara = true, m_hudApar = true, m_hudMistura = true, m_desocLigado = true;
    float m_hudCorte = kHudCorte, m_hudMiraR = 0.0f;
    DXGI_FORMAT m_fmtEscala = DXGI_FORMAT_UNKNOWN;   // R8_UNORM onde a placa deixa; R32_FLOAT se nao
    float m_desocForca = kDesocForca, m_desocDisc = kDesocDisc;
    float m_incLim = kIncLim, m_incEsc = kIncEsc, m_incPeso = kIncPeso;   // FG-QUALIDADE2
    double m_incFrac = 0.0;   // media movel dos blocos em que nenhum dos dois reais explica
    double m_desocFrac = 0.0;   // fracao dos blocos em desoclusao (media movel; vai no log de 5 s)
    struct HudCB { UINT W, H, N, maxProt; float thr, corte, apCon, apFino;
                   float miraR, miraBonus, desocForca, bw; float bh, p0, p1, p2; UINT apar, desocOn, cx, cy; };
    PonteGpu::MedidorGpu m_medHud;
    // ---- QUADRO REPETIDO NAO ENTRA NO FI (X-FG-QUALIDADE; leitura atrasada em K-DEDUP).
    //      Ver CriarDedup / DedupDespachar / DedupColher / DedupVeredito / HlslDedup.
    ID3D11ComputeShader* m_csDedup = nullptr; ID3D11Buffer* m_cbDedup = nullptr;
    ID3D11Buffer* m_dedupStats = nullptr; ID3D11UnorderedAccessView* m_dedupStatsUav = nullptr;
    ID3D11Buffer* m_dedupStg[kDedupAnel] = {}; bool m_dedupPend[kDedupAnel] = {};
    uint64_t m_dedupPendN[kDedupAnel] = {}; double m_dedupPendMs[kDedupAnel] = {};
    uint64_t m_dedupVerN[kDedupVer] = {}; int8_t m_dedupVerR[kDedupVer] = {};
    std::atomic<bool> m_dedupPermitido{ true };   // RODADA8
    bool m_dedupLigado = true; unsigned m_dedupLimiar = kDedupLimiar; double m_dedupFracMin = kDedupFracMin;
    UINT m_dedupGX = 0, m_dedupGY = 0; double m_dedupAmostras = 0.0;
    uint64_t m_repetidos = 0, m_dedupDesconhecidos = 0, m_dedupN = 0; int m_repetidosLog = 0;
    double m_dedupEsperaAcc = 0.0, m_dedupEsperaMax = 0.0, m_dedupUltFrac = -1.0;
    uint64_t m_dedupJanRep = 0, m_dedupJanN = 0, m_dedupRespN = 0;   // janela de 5 s
    double m_dedupRespAcc = 0.0, m_dedupAtrasoAcc = 0.0, m_dedupLogMs = 0.0;
    UINT m_dedupPalavras = 4; uint64_t m_dedupUltId = 0;   // o buffer agora leva tambem N2 e N5
    /* ---- METRICA DE PROVA: O GERADO E MESMO DIFERENTE DOS DOIS REAIS? (X-GERADO, 20/09/2026)
       O log dizia quantos G saiam e em que hora, nunca se o G era uma IMAGEM NOVA. Com o gate de
       ambiguidade zerando o campo (FG-PROVA §3) o FI devolvia copia do real e nenhum numero do log
       acusava - so a camera fotografando a tela. Agora o MESMO PassoDif do dedup roda mais duas
       vezes por quadro, sobre o G ja pronto (depois do corte e da mascara de HUD): (A, G) e (B, G).
       Os dois contadores voltam pelo mesmo idioma do dedup - anel de stagings, Map(DO_NOT_WAIT),
       nenhuma espera. G copia de B da `distinto de B 0%`; interpolacao de verdade da os dois
       parecidos com a fracao de tela que se move. Custo: dois despachos de 310 k amostras
       (o dedup inteiro mede 0,01 ms). Ver ProvaDespachar / ProvaColher. */
    ID3D11ShaderResourceView* m_outGSrv[kSlots] = {};
    ID3D11ComputeShader* m_csProva = nullptr;
    ID3D11Buffer* m_provaStats = nullptr; ID3D11UnorderedAccessView* m_provaUav = nullptr;
    ID3D11Buffer* m_provaStg[kDedupAnel] = {}; bool m_provaPend[kDedupAnel] = {};
    // fracao de amostras em que G difere de A, de B e da mistura (A+B)/2; -1 = sem resposta ainda
    double m_provaA = -1.0, m_provaB = -1.0, m_provaM = -1.0;
    // ---- N2: corte de cena pelo mesmo passo (veredito por id, como o do repetido)
    int8_t m_dedupVerC[kDedupVer] = {};
    bool m_corteSadLigado = true, m_corteSadArmado = true, m_corteSadArmar = false;
    unsigned m_corteNivel = 48;          // niveis (0-255) acima dos quais a amostra "mudou muito"
    double m_corteSadFrac = 0.60;        // fracao de amostras assim que declara corte
    uint64_t m_cortesSad = 0, m_cortesSadJan = 0;
    // ---- N5: letterbox (retangulo ativo). Aplicado, candidato, e quantas medidas iguais ele tem.
    bool m_lbLigado = true, m_lbAtivo = false;
    unsigned m_lbNivel = 6;              // luminancia (0-255) abaixo da qual a amostra e "preta"
    UINT m_lbNRW = 0, m_lbNCW = 0;       // palavras de bits de linha e de coluna
    int m_lbX = 0, m_lbY = 0, m_lbW = 0, m_lbH = 0;
    int m_lbCx = -1, m_lbCy = -1, m_lbCw = -1, m_lbCh = -1; int m_lbCandN = 0;
    uint64_t m_lbTrocas = 0;
    // ---- VRAM acima do orcamento / interpolador morto (X-HUD-PISCA). Ver SoltarContextoVram, Morto.
    bool m_ctxSolto = false, m_morto = false;
    // X-TDR: fence que libera cada alocador; atraso continuo da fila do FI (ver a espera no Gerar)
    uint64_t m_alocFence[kSlots] = {}; double m_fiAtrasoDesde = 0.0; uint64_t m_fiAtrasos = 0; int m_fiAtrasosLog = 0;
    std::vector<ID3D11Texture2D*> m_velhas; uint64_t m_velhasEm = 0;   // W-153: gerados extras aposentados (ver Aposentar)
    uint64_t m_ger = 0, m_frameId = 0, m_gerados = 0, m_cortes = 0, m_pans = 0, m_mv0s = 0, m_semCampo = 0; int m_errosLog = 0;
    uint64_t m_fSubmetida = 0; double m_fSubmetidaMs = 0.0;   // X-FONTE-SOLTA: ultimo valor SINALIZADO na m_fOut e quando (ver FiEmVoo)
    float m_movFracPrev = 0; bool m_panPrev = false, m_mv0Prox = false;
    bool m_ambigAntigo = false;   // X-GERADO: DLSS5_FG_AMBIG_ANTIGO=1 volta o gate de ambiguidade de ate 20/09
    bool m_mvParaFi = true;       // FG-QUALIDADE2: DLSS5_FG_MV_FI=0 manda MV=0 ao FI (o resto igual)
    unsigned m_dumpFeitos = 0; uint64_t m_dumpProx = 0;   // FG-QUALIDADE2: o despejo em serie
    // B12 (16/09/2026): comeca COM atraso de 1 quadro (Map sem esperar). O modo sem atraso so
    // saia pela media de 6 ms, e um pico isolado de placa nao mexe na media: travava a CPU ali.
    bool m_statsLag = true; double m_mapMs = 0.0;
    dlss5::MotionEstimator m_me; bool m_campoLocal = false; PonteGpu::MedidorGpu m_medVme;   // campo LOCAL (duas placas)
    std::atomic<bool> m_relatoPedido{ false }; wchar_t m_relatoPasta[MAX_PATH] = {};   // Ctrl+F12: gravar A/G/B
};

} // namespace FrameGen
