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
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <atomic>
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

struct Resultado {
    ID3D11Texture2D* G = nullptr;      // o quadro gerado (do device do apresentador), espera ja enfileirada
    ID3D11Texture2D* B = nullptr;      // a copia propria de B (vale ate o proximo Gerar+1)
    bool corte = false;                // corte de cena: nao mostre G, mostre B
    bool pan = false, mv0 = false;     // diagnostico
    uint32_t nMov = 0, nKept = 0, nStatic = 0, nMask = 0, nAmbig = 0, nFlat = 0, nDiffBig = 0, nBlocos = 0;
};

class Interpolador {
public:
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
    bool CriarContexto(IDXGIAdapter1* ad, UINT w, UINT h, DXGI_FORMAT fmt, const wchar_t* dllPath, LogFn log, std::wstring* erro, bool interna1440 = false) {
        m_log = log; m_fw = w; m_fh = h; m_fmt = fmt;
        if (!ad || !w || !h) { if (erro) *erro = L"fg: parametros vazios"; return false; }
        // RESOLUCAO INTERNA (duas placas): o FI a 4K custa ~8 ms na placa do JOGO - derruba um jogo de
        // 60 para ~40 e some com o ganho. Acima de 1440p o G e gerado em 1440p (mesma proporcao) e
        // o apresentador amplia ao mostrar; A/B continuam inteiros para o Present.
        m_w = w; m_h = h;
        if (interna1440 && (UINT64)w * h > 2560ull * 1440ull) { m_w = 2560; m_h = (UINT)((UINT64)h * 2560 / w) & ~1u; }
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
        for (int i = 0; i < 2; i++) {
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
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count = 4;
        m_dev12->CreateQueryHeap(&qh, __uuidof(ID3D12QueryHeap), (void**)&m_qh);
        CriarBuffer12(64, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, &m_qbuf);

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
        ffxConfigureDescFrameGeneration cf{}; cf.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION; cf.frameGenerationEnabled = true;
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
        for (int i = 0; i < 2; i++) {
            // inBFi: o que o FI le (resolucao interna, compartilhada com o D3D12). inB: o quadro inteiro
            // para o Present (so existe a parte quando ha reducao; senao e a mesma textura).
            if (!CriarTex11(m_w, m_h, m_fmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, true, &m_inBFi[i], &m_inBFi12[i], erro, L"inBFi")) return false;
            if (FAILED(m_dev->CreateShaderResourceView(m_inBFi[i], nullptr, &m_inBFiSrv[i]))) { if (erro) *erro = L"fg: SRV de inBFi"; return false; }
            if (m_reduz) {
                if (!CriarTex11(m_fw, m_fh, m_fmt, D3D11_BIND_SHADER_RESOURCE, false, &m_inB[i], nullptr, erro, L"inB")) return false;
                if (FAILED(m_dev->CreateShaderResourceView(m_inB[i], nullptr, &m_inBSrv[i]))) { if (erro) *erro = L"fg: SRV de inB"; return false; }
            } else { m_inB[i] = m_inBFi[i]; m_inB[i]->AddRef(); }
            if (!CriarTex11(m_w, m_h, m_fmt, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, true, &m_outG[i], &m_outG12[i], erro, L"outG")) return false;
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
        if (!CriarCorte(erro)) return false;   // detector de corte que NAO depende do campo
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
        for (int i = 0; i < 2; i++) if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_corteStg[i]))) { if (erro) *erro = L"fg: staging do detector de corte"; return false; }
        for (int i = 0; i < 2; i++) if (FAILED(m_dev->CreateUnorderedAccessView(m_outG[i], nullptr, &m_outGUav[i]))) { if (erro) *erro = L"fg: UAV do G (aplicacao do corte)"; return false; }
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
    bool CriarGrade(UINT gridW, UINT gridH, float covU, float covV, std::wstring* erro) {
        m_gw = gridW; m_gh = gridH; m_covU = covU; m_covV = covV;
        // grade: bloco (gated), bloco2 (dilatado), runEst (persistente), stats
        if (!CriarTex11(gridW, gridH, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, false, &m_bloco, nullptr, erro, L"bloco")) return false;
        if (!CriarTex11(gridW, gridH, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, false, &m_bloco2, nullptr, erro, L"bloco2")) return false;
        if (!CriarTex11(gridW, gridH, DXGI_FORMAT_R32_UINT, D3D11_BIND_UNORDERED_ACCESS, false, &m_run, nullptr, erro, L"runEst")) return false;
        m_dev->CreateUnorderedAccessView(m_bloco, nullptr, &m_blocoUav);  m_dev->CreateShaderResourceView(m_bloco, nullptr, &m_blocoSrv);
        m_dev->CreateUnorderedAccessView(m_bloco2, nullptr, &m_bloco2Uav); m_dev->CreateShaderResourceView(m_bloco2, nullptr, &m_bloco2Srv);
        m_dev->CreateUnorderedAccessView(m_run, nullptr, &m_runUav);
        {
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth = 64; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS; bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_stats))) { if (erro) *erro = L"fg: buffer de estatisticas"; return false; }
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = 16; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            if (FAILED(m_dev->CreateUnorderedAccessView(m_stats, &ud, &m_statsUav))) { if (erro) *erro = L"fg: UAV de estatisticas"; return false; }
            bd.Usage = D3D11_USAGE_STAGING; bd.BindFlags = 0; bd.MiscFlags = 0; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            for (int i = 0; i < 2; i++) if (FAILED(m_dev->CreateBuffer(&bd, nullptr, &m_statsStg[i]))) { if (erro) *erro = L"fg: staging de estatisticas"; return false; }
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
        m_campoN.store(n, std::memory_order_release);
    }

    bool Pronto() const { return m_contexto && m_dev != nullptr; }
    bool TemContexto() const { return m_contexto; }
    /* "Reportar quadro" (thread do laco): o proximo Gerar grava A, G e B em `pasta`. */
    void PedirRelato(const wchar_t* pasta) { wcsncpy(m_relatoPasta, pasta, MAX_PATH - 1); m_relatoPasta[MAX_PATH - 1] = 0; m_relatoPedido.store(true); }
    bool CampoLigado() const { return m_campoOk || m_campoTentou; }   // (tentou e falhou tambem conta: nao insiste todo quadro)
    double MemoriaMB() const { return m_memMB; }
    double FiMs() const { return m_nFi ? m_somaFi / m_nFi : 0.0; }
    double PassesMs() const { return m_medPasses.Media() + m_medCorte.Media(); }
    double CorteMs() const { return m_medCorte.Media(); }            // custo do detector de corte sem campo
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
        const int cur = (int)(m_ger % 2);
        ctx->CopyResource(m_inB[cur], B);
        m_ger++;
        m_frameId = 0;   // a proxima geracao vai com dd.reset = true
        return true;
    }
    bool Gerar(ID3D11DeviceContext* ctx, ID3D11Texture2D* B, uint64_t n, float intervaloMs, Resultado* r) {
        *r = Resultado{};
        if (!Pronto() || !ctx || !B) return false;
        ID3D11DeviceContext4* c4 = nullptr;
        if (FAILED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&c4)) || !c4) return false;
        const int cur = (int)(m_ger % 2), prev = 1 - cur;
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

        // ---- o campo por pixel (passes A, A2, B) no device do apresentador
        // LOCAL: o VME roda aqui mesmo sobre B (backward B -> A, a convencao do FI); REMOTO: espera
        // a fence do campo publicado pela rede para o quadro n.
        const bool temCampo = m_campoOk && (m_campoLocal || m_campoN.load(std::memory_order_acquire) >= n);
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
                    c.movFracPrev = m_movFracPrev; c.mv0 = m_mv0Prox ? 1u : 0u; c.pan = m_panPrev ? 1u : 0u;
                    memcpy(m.pData, &c, sizeof(c)); ctx->Unmap(m_cb, 0);
                }
            }
            ID3D11ShaderResourceView* srvA[3] = { m_inBFiSrv[prev], m_inBFiSrv[cur], m_campoLocal ? m_me.MotionSRV() : m_campoSrv[n % 3] };
            ID3D11UnorderedAccessView* uavA[3] = { m_blocoUav, m_runUav, m_statsUav };
            ctx->CSSetConstantBuffers(0, 1, &m_cb);
            ctx->CSSetShader(m_csA, nullptr, 0);
            ctx->CSSetShaderResources(0, 3, srvA);
            ctx->CSSetUnorderedAccessViews(0, 3, uavA, nullptr);
            ctx->Dispatch((m_gw + 7) / 8, (m_gh + 7) / 8, 1);
            ID3D11UnorderedAccessView* nu3[3] = {}; ID3D11ShaderResourceView* ns3[3] = {};
            ctx->CSSetUnorderedAccessViews(0, 3, nu3, nullptr); ctx->CSSetShaderResources(0, 3, ns3);
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
            if (!m_mvZerado) { const float z[4] = { 0, 0, 0, 0 }; ctx->ClearUnorderedAccessViewFloat(m_mvUav, z); m_mvZerado = true; }
            m_semCampo++;
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
        c4->Signal(m_fIn11, n);
        ctx->Flush();

        // ---- D3D12: PREPARE + FG manual
        if (m_fOut->GetCompletedValue() + 2 < m_ger) { /* o alocador deste slot ainda pode estar em uso */ EsperarFence(m_fOut, m_ger - 2); }
        ID3D12CommandAllocator* al = m_alloc[cur];
        al->Reset(); m_lista->Reset(al, nullptr);
        {
            D3D12_RESOURCE_BARRIER b[3] = {
                Bar(m_inBFi12[cur], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                Bar(m_mv12, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                Bar(m_outG12[cur], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) };
            m_lista->ResourceBarrier(3, b);
        }
        LerTimestamp(cur);
        m_lista->EndQuery(m_qh, D3D12_QUERY_TYPE_TIMESTAMP, cur * 2);
        ffxDispatchDescFrameGenerationPrepareCameraInfo cam{}; cam.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
        cam.cameraUp[1] = 1; cam.cameraRight[0] = 1; cam.cameraForward[2] = 1;
        ffxDispatchDescFrameGenerationPrepare pp{}; pp.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE; pp.header.pNext = &cam.header;
        pp.frameID = m_frameId; pp.commandList = m_lista; pp.renderSize = { m_w, m_h }; pp.jitterOffset = { 0, 0 }; pp.motionVectorScale = { 1, 1 };
        pp.frameTimeDelta = (intervaloMs > 8.0f && intervaloMs < 50.0f) ? intervaloMs : (intervaloMs >= 50.0f ? 50.0f : 16.667f);   // limitado: um intervalo inflado por quadros perdidos nao pode virar "pausa" para o SDK pp.unused_reset = (m_frameId == 0);
        pp.cameraNear = 0.1f; pp.cameraFar = 1000.0f; pp.cameraFovAngleVertical = 1.0f; pp.viewSpaceToMetersFactor = 1.0f;
        pp.depth = ffxApiGetResourceDX12(m_depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        pp.motionVectors = ffxApiGetResourceDX12(m_mv12, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        const ffxReturnCode_t rcP = m_Dispatch(&m_fg, &pp.header);
        ffxDispatchDescFrameGeneration dd{}; dd.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION; dd.commandList = m_lista;
        dd.presentColor = ffxApiGetResourceDX12(m_inBFi12[cur], FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.outputs[0] = ffxApiGetResourceDX12(m_outG12[cur], FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dd.numGeneratedFrames = 1; dd.reset = (m_frameId == 0); dd.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
        dd.minMaxLuminance[0] = 0; dd.minMaxLuminance[1] = 1; dd.generationRect = { 0, 0, (int32_t)m_w, (int32_t)m_h }; dd.frameID = m_frameId;
        const ffxReturnCode_t rcD = m_Dispatch(&m_fg, &dd.header);
        m_frameId++;
        m_lista->EndQuery(m_qh, D3D12_QUERY_TYPE_TIMESTAMP, cur * 2 + 1);
        m_lista->ResolveQueryData(m_qh, D3D12_QUERY_TYPE_TIMESTAMP, cur * 2, 2, m_qbuf, cur * 16);
        {
            D3D12_RESOURCE_BARRIER b[3] = {
                Bar(m_inBFi12[cur], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
                Bar(m_mv12, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
                Bar(m_outG12[cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON) };
            m_lista->ResourceBarrier(3, b);
        }
        m_lista->Close();
        ID3D12CommandList* ls[] = { m_lista };
        m_fila->Wait(m_fIn, n);
        m_fila->ExecuteCommandLists(1, ls);
        m_fila->Signal(m_fOut, m_ger);
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
                ctx->Unmap(m_statsStg[slotStats], 0);
                const double nb = r->nBlocos ? (double)r->nBlocos : 1.0;
                // CORTE DE CENA: a maioria dos blocos mudou de media E nao ha movimento coerente que
                // explique (um pan rapido de alto contraste tambem muda as medias, mas o campo
                // continua valido nele; num corte de verdade o VME devolve lixo e os gates zeram).
                r->corte = r->nDiffBig > 0.5 * nb && r->nKept < 0.2 * nb;
                r->pan = m_panPrev; r->mv0 = m_mv0Prox;
                m_movFracPrev = (float)(r->nMov / nb);
                // pan: a maior parte dos blocos se move e ha blocos estaticos (HUD) - a margem da mascara entra
                m_panPrev = (r->nKept > 0.7 * nb) && r->nStatic > 0;
                // aliasing: a AMBIGUIDADE derrubou a maior parte do que se movia (xadrez/grade) -> MV=0 no proximo
                m_mv0Prox = (r->nMov > 0.5 * nb) && (r->nAmbig > 0.5 * r->nMov);
                if (r->corte) m_cortes++;
                if (m_panPrev) m_pans++;
                if (m_mv0Prox) m_mv0s++;
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
        r->G = m_outG[cur];
        m_gerados++;
        Dump(ctx, cur, prev, n);
        m_ger++;
        c4->Release();
        return true;
    }

    void Parar() {
        if (m_fila && m_fOut) EsperarFila();
        if (m_fg) { m_Destroy(&m_fg, nullptr); m_fg = nullptr; }
        m_contexto = false; m_campoOk = false;
        for (int i = 0; i < 2; i++) { Soltar(m_inBSrv[i]); Soltar(m_inB[i]); Soltar(m_inBFiSrv[i]); Soltar(m_inBFi12[i]); Soltar(m_inBFi[i]); Soltar(m_outG12[i]); Soltar(m_outG[i]); Soltar(m_statsStg[i]); Soltar(m_alloc[i]); }
        Soltar(m_csDown); Soltar(m_samp); Soltar(m_cbDown); m_reduz = false;
        for (int i = 0; i < 3; i++) { Soltar(m_campoSrv[i]); Soltar(m_campoApres[i]); Soltar(m_campoRede[i]); }
        Soltar(m_mvUav); Soltar(m_mv12); Soltar(m_mv);
        Soltar(m_blocoUav); Soltar(m_blocoSrv); Soltar(m_bloco); Soltar(m_bloco2Uav); Soltar(m_bloco2Srv); Soltar(m_bloco2); Soltar(m_runUav); Soltar(m_run);
        Soltar(m_statsUav); Soltar(m_stats); Soltar(m_cb);
        Soltar(m_csA); Soltar(m_csA2); Soltar(m_csB);
        for (int i = 0; i < 2; i++) { Soltar(m_corteStg[i]); Soltar(m_outGUav[i]); m_cortePend[i] = false; }
        Soltar(m_corteUav); Soltar(m_corteStats); Soltar(m_corteEstUav); Soltar(m_corteEst);
        Soltar(m_corteArgsUav); Soltar(m_corteArgs); Soltar(m_corteMedUav); Soltar(m_corteMed); Soltar(m_cbCorte);
        Soltar(m_csCorte); Soltar(m_csCorteDec); Soltar(m_csCorteAp); m_medCorte.Parar();
        m_corteFrac = 0.0; m_cortesSemCampo = 0; m_cortesVistos = 0;
        m_medPasses.Parar(); m_medVme.Parar(); m_me.Shutdown(); m_campoLocal = false;
        Soltar(m_fIn11); Soltar(m_fOut11); Soltar(m_fCampoApres); Soltar(m_fCampoRede);
        Soltar(m_depth); Soltar(m_qbuf); Soltar(m_qh); Soltar(m_lista); Soltar(m_fIn); Soltar(m_fOut); Soltar(m_fila); Soltar(m_dev12);
        Soltar(m_devRede); Soltar(m_dev);
        if (m_evt) { CloseHandle(m_evt); m_evt = nullptr; }
        if (m_dll) { FreeLibrary(m_dll); m_dll = nullptr; }
        m_ger = 0; m_frameId = 0; m_campoN.store(0);
    }
    ~Interpolador() { Parar(); }

private:
    struct Constantes { UINT W, H, GW, GH; float bw, bh, movFracPrev, pad0; UINT mv0, pan, pad1, pad2; float pad3[4]; };

    // ---- os shaders dos passes (ver o cabecalho). Luma em 0..1.
    static const char* Hlsl() {
        return
            "cbuffer C : register(b0) { uint W, H, GW, GH; float bw, bh, movFracPrev, pad0; uint mv0, pan, pad1, pad2; float4 pad3; };\n"
            "Texture2D<float4> A : register(t0); Texture2D<float4> B : register(t1); Texture2D<float4> campo : register(t2);\n"
            "RWTexture2D<float4> bloco : register(u0); RWTexture2D<uint> runEst : register(u1); RWByteAddressBuffer stats : register(u2);\n"
            "float Luma(float4 c) { return dot(c.rgb, float3(0.299, 0.587, 0.114)); }\n"
            "float LumaA(float2 p) { int2 ip = int2(clamp(p, float2(0, 0), float2(W - 1, H - 1))); return Luma(A.Load(int3(ip, 0))); }\n"
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
            "    if (!zera) {\n"
            "      float2 u = normalize(v); float sadV = 0, best2 = 1e30;\n"
            "      [unroll] for (int j = 0; j < 8; j += 2) [unroll] for (int i = 0; i < 8; i += 2) { float2 p = org + (float2(i, j) + 0.5) * st; sadV += abs(lb[j * 8 + i] - LumaA(p + v)); }\n"
            "      for (int k = -36; k <= 36; k++) { float d = 2.0 * k; if (abs(d) <= 2.0) continue; float2 off = v + u * d; float sd = 0;\n"
            "        [unroll] for (int j = 0; j < 8; j += 2) [unroll] for (int i = 0; i < 8; i += 2) { float2 p = org + (float2(i, j) + 0.5) * st; sd += abs(lb[j * 8 + i] - LumaA(p + off)); }\n"
            "        best2 = min(best2, sd); }\n"
            "      if (best2 <= 1.2 * sadV + 16.0 / 255.0) { zera = true; flags |= 8u; stats.InterlockedAdd(28, 1u, tmp); }\n"
            "    }\n"
            "    if (zera) { v = 0; mov = false; } else stats.InterlockedAdd(16, 1u, tmp);\n"
            "  }\n"
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
    void EsperarFence(ID3D12Fence* f, uint64_t v) {
        if (f->GetCompletedValue() >= v || !m_evt) return;
        f->SetEventOnCompletion(v, m_evt);
        WaitForSingleObject(m_evt, 2000);
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
    static void Hr(std::wstring* erro, const wchar_t* onde, HRESULT hr) {
        if (!erro) return; wchar_t b[160]; _snwprintf(b, 160, L"fg: %ls falhou (0x%08lX)", onde, (unsigned long)hr); b[159] = 0; *erro = b;
    }
    // DLSS5_FG_DUMP=<pasta> DLSS5_FG_DUMP_FRAME=N: grava A, B e G do quadro N em PPM (uma vez)
    void Dump(ID3D11DeviceContext* ctx, int cur, int prev, uint64_t n) {
        wchar_t pasta[MAX_PATH]{}, b[16]{};
        const bool relato = m_relatoPedido.exchange(false);
        if (relato) wcsncpy(pasta, m_relatoPasta, MAX_PATH - 1);
        else {
            if (m_dumpFeito) return;
            if (!GetEnvironmentVariableW(L"DLSS5_FG_DUMP", pasta, MAX_PATH)) { m_dumpFeito = true; return; }
            const uint64_t alvo = GetEnvironmentVariableW(L"DLSS5_FG_DUMP_FRAME", b, 16) ? (uint64_t)_wtoi(b) : 120;
            if (n < alvo) return;
            m_dumpFeito = true;
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
    ID3D12Device* m_dev12 = nullptr; ID3D12CommandQueue* m_fila = nullptr; ID3D12CommandAllocator* m_alloc[2] = {}; ID3D12GraphicsCommandList* m_lista = nullptr;
    ID3D12Fence* m_fIn = nullptr; ID3D12Fence* m_fOut = nullptr; HANDLE m_evt = nullptr;
    ID3D12QueryHeap* m_qh = nullptr; ID3D12Resource* m_qbuf = nullptr; UINT64 m_tsFreq = 0; bool m_tsPend[2] = {}; double m_somaFi = 0; uint64_t m_nFi = 0;
    ID3D12Resource* m_depth = nullptr;
    ID3D11Device5* m_dev = nullptr; ID3D11Device5* m_devRede = nullptr;
    UINT m_fw = 0, m_fh = 0; bool m_reduz = false;   // tamanho do QUADRO (m_w/m_h = resolucao interna do FI)
    ID3D11Texture2D* m_inB[2] = {}; ID3D11ShaderResourceView* m_inBSrv[2] = {};                                   // quadro inteiro (Present)
    ID3D11Texture2D* m_inBFi[2] = {}; ID3D12Resource* m_inBFi12[2] = {}; ID3D11ShaderResourceView* m_inBFiSrv[2] = {};   // o que o FI le
    ID3D11ComputeShader* m_csDown = nullptr; ID3D11SamplerState* m_samp = nullptr; ID3D11Buffer* m_cbDown = nullptr;
    ID3D11Texture2D* m_outG[2] = {}; ID3D12Resource* m_outG12[2] = {};
    ID3D11Texture2D* m_mv = nullptr; ID3D12Resource* m_mv12 = nullptr; ID3D11UnorderedAccessView* m_mvUav = nullptr;
    ID3D11Texture2D* m_campoRede[3] = {}; ID3D11Texture2D* m_campoApres[3] = {}; ID3D11ShaderResourceView* m_campoSrv[3] = {};
    ID3D11Fence* m_fCampoRede = nullptr; ID3D11Fence* m_fCampoApres = nullptr; std::atomic<uint64_t> m_campoN{ 0 };
    ID3D11Texture2D* m_bloco = nullptr; ID3D11UnorderedAccessView* m_blocoUav = nullptr; ID3D11ShaderResourceView* m_blocoSrv = nullptr;
    ID3D11Texture2D* m_bloco2 = nullptr; ID3D11UnorderedAccessView* m_bloco2Uav = nullptr; ID3D11ShaderResourceView* m_bloco2Srv = nullptr;
    ID3D11Texture2D* m_run = nullptr; ID3D11UnorderedAccessView* m_runUav = nullptr;
    ID3D11Buffer* m_stats = nullptr; ID3D11UnorderedAccessView* m_statsUav = nullptr; ID3D11Buffer* m_statsStg[2] = {}; bool m_statsPend[2] = {}; ID3D11Buffer* m_cb = nullptr;
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
    ID3D11UnorderedAccessView* m_outGUav[2] = {};
    ID3D11Buffer* m_corteStg[2] = {}; bool m_cortePend[2] = {};
    UINT m_cgw = 0, m_cgh = 0; double m_corteFrac = 0.0, m_corteSalto = 0.0, m_corteFracMin = 0.0;
    struct CorteCB { UINT W, H, GW, GH; float bw, bh, thr, salto; float fracMin, piso, fracAlta, saltoAlta; UINT GX, GY, u0, u1; };
    CorteCB m_corteCc{};                       // copia na CPU do cb do detector (reescrita por quadro)
    bool   m_corteSaltoFixo = false;           // a bancada fixou o salto por env -> nao varia com a taxa
    double m_corteSaltoAltaBase = 2.5, m_corteSaltoAltaAtual = 2.5;
    double m_corteSaltoMin = 4.0, m_corteSaltoAltaMin = 2.0;
    UINT m_corteGrade = 24, m_corteAmostra = 4; bool m_corteTelem = true;
    bool m_corteLigado = true; uint64_t m_cortesSemCampo = 0, m_cortesVistos = 0;
    PonteGpu::MedidorGpu m_medCorte;
    uint64_t m_ger = 0, m_frameId = 0, m_gerados = 0, m_cortes = 0, m_pans = 0, m_mv0s = 0, m_semCampo = 0; int m_errosLog = 0;
    float m_movFracPrev = 0; bool m_panPrev = false, m_mv0Prox = false;
    bool m_statsLag = false; double m_mapMs = 0.0;          // corte de cena: sem atraso enquanto o Map for barato
    dlss5::MotionEstimator m_me; bool m_campoLocal = false; PonteGpu::MedidorGpu m_medVme;   // campo LOCAL (duas placas)
    std::atomic<bool> m_relatoPedido{ false }; wchar_t m_relatoPasta[MAX_PATH] = {};   // Ctrl+F12: gravar A/G/B
};

} // namespace FrameGen
