/* =====================================================================
   upscaler.h -- SELETOR MANUAL DE AMPLIADOR (upscaler) da SAIDA NO MONITOR.

   ONDE ENTRA. Com `saida_monitor=1` o backbuffer nasce no tamanho do MONITOR e
   a captura (menor: o jogo em 1440p) e desenhada nele. Quem faz essa ampliacao
   ate 13/09/2026 era SEMPRE o nosso ampliador direcional, dentro do PS
   principal (main.cpp, `UpFilter == 2` / `SampleDirecional`). Este arquivo
   troca ESSE passo - e so ele - por FSR 1 ou FSR 3.1, sem tocar em mais nada
   da cadeia (a rede neural do feeder continua igual nos tres casos).

   AS TRES OPCOES, e por que sao estas (banco de provas de 13/09/2026,
   D:\claude-scratch\upscaler\bench.cpp; dossie 3.3e):
     - "DLSS (atual)": o caminho de sempre. E o PADRAO e continua sendo.
     - "FSR 1" (EASU + RCAS): espacial puro, 0,52-0,60 ms medidos com GPU
       dedicada, ZERO VRAM, e - o que importa - SEM modo de falha temporal.
     - "FSR 3.1" (espacial, `reset=true` FORCADO todo quadro): o upscaler do
       amd_fidelityfx_dx12.dll que ja esta no bin.

   POR QUE O PADRAO NAO MUDOU. A vantagem de qualidade que o FSR1 mostrou no
   banco (39,21 dB contra 37,62 do bilinear) veio de um banco cuja entrada
   1440p foi FABRICADA por downsample do 4K (bench.cpp:846). O app NAO faz
   isso: ele captura o jogo renderizando NATIVO, com aliasing, TAA e o
   sharpening do proprio jogo. Num banco band-limitado o RCAS esta
   aproximadamente invertendo um box conhecido - e por isso sai mais nitido
   que a propria referencia 4K (acutancia 43,07 contra 43,96). Em entrada real
   ele pode virar amplificador de aliasing. NAO ESTA MEDIDO. Ate a sessao de
   gravacao com dado real (tarefa 5b do dossie), o padrao continua "DLSS
   (atual)" e a troca fica facil: ini, variavel de ambiente e chip no painel.

   FSR 3.1 SEM `reset` E PROIBIDO, e nao ha opcao para ligar. Medido: sem MV e
   sem jitter o historico temporal so acumula lixo - 31,55 dB, SSIM 0,9125,
   arrasto visivel (o xadrez vira listras) e a saida deixa de picar em (0,0).
   Com `reset=true` todo quadro sobe para 37,09 dB. Entao o `dd.reset` daqui e
   literal `true`, sem chave, sem env e sem ini.

   XeSS FICOU DE FORA por CONTRATO, nao por qualidade: `xessD3D12Execute`
   devolve -4 (XESS_RESULT_ERROR_INVALID_ARGUMENT) sem vetores de movimento
   dilatados e jitter a cada quadro, que o caminho por captura nao tem.

   NITIDEZ DO RCAS EXPOSTA (`[ultima] upscaler_nitidez`, 0..1). O prototipo
   mediu com 0,25 STOPS do FSR (que na escala 0..1 daqui e 0,875 - ver
   FsrFillConstants em feed_fsr1.h: stops = (1 - nitidez) * 2). Isso e MUITO
   realce, e foi medido justamente na entrada fabricada em que o realce era
   de graca. O padrao aqui e 0,5 (= 1,0 stop) por ser o meio da escala e o
   lado seguro; quem quiser o valor do banco poe 0,875.

   SEM DLL NOVA E SEM LICENCA NOVA: o EASU/RCAS vem do feed_fsr1.h que o
   feeder ja tem (HLSL puro, MIT, AMD), e o FSR 3.1 vem do
   amd_fidelityfx_dx12.dll que ja esta no bin para a fluidez interpolada.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ponte_gpu.h"                            // Soltar, MedidorGpu
#include "feeder-amd/src-feeder/src/feed_fsr1.h"  // EASU + RCAS em HLSL puro (MIT, AMD)

#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_api_types.h"
#include "ffx_api/ffx_upscale.h"
#include "ffx_api/dx12/ffx_api_dx12.h"

namespace Upsc {

using PonteGpu::Soltar;
typedef void (*LogFn)(const char*, ...);

enum Modo { Dlss = 0, Fsr1 = 1, Fsr31 = 2 };

static LogFn        g_log     = nullptr;
static Modo         g_pedido  = Dlss;       // o que a pessoa escolheu (ini/env/painel)
static Modo         g_efetivo = Dlss;       // o que esta rodando de fato (queda muda este)
static float        g_nitidez = 0.5f;       // RCAS, 0..1 (1 = o mais nitido que o FSR permite)
static std::wstring g_motivo;               // por que caiu para o caminho de sempre ("" = nao caiu)
static std::wstring g_dll;                  // caminho do amd_fidelityfx_dx12.dll (quem sabe e o main)
static bool         g_hdrBloqueia = false;  // a fonte esta em HDR: EASU/RCAS e o FSR sao SDR aqui

inline const wchar_t* Chave(Modo m) { return m == Fsr1 ? L"fsr1" : m == Fsr31 ? L"fsr31" : L"dlss"; }
inline const char*    NomeLog(Modo m) { return m == Fsr1 ? "FSR 1 (EASU+RCAS)" : m == Fsr31 ? "FSR 3.1 (espacial, reset=true)" : "DLSS (atual: ampliador direcional + cadeia NGX)"; }

inline Modo DeTexto(const wchar_t* s) {
    if (!s || !*s) return Dlss;
    if (!_wcsicmp(s, L"fsr1") || !_wcsicmp(s, L"fsr") || !_wcsicmp(s, L"fsr-1")) return Fsr1;
    if (!_wcsicmp(s, L"fsr31") || !_wcsicmp(s, L"fsr3") || !_wcsicmp(s, L"fsr3.1") || !_wcsicmp(s, L"fsr-3.1")) return Fsr31;
    return Dlss;
}

/* O MOTOR. Um so objeto, vivo no device da rede (o mesmo do Renderer). Cada modo
   monta o que precisa na primeira vez que roda e SOLTA TUDO quando o modo muda -
   e a regra "contexto criado/destruido junto com o modo" do pedido. */
class Motor {
public:
    void Config(LogFn log) { g_log = log; }

    /* Amplia `src` (sw x sh) para `tw x th` com o modo escolhido e devolve em *outSrv uma
       SRV do resultado, ja no tamanho de saida - o chamador so precisa desenhar 1:1.
       Devolve false quando o caminho de sempre tem de ser usado (modo DLSS, HDR, falha):
       nesse caso *outSrv nao e tocado e NADA muda no que ja funcionava. */
    bool Aplicar(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                 ID3D11ShaderResourceView* src, UINT sw, UINT sh, UINT tw, UINT th,
                 ID3D11VertexShader* vs, ID3D11RasterizerState* rs, UINT vtx,
                 ID3D11ShaderResourceView** outSrv) {
        if (!dev || !ctx || !src || !outSrv || !sw || !sh || !tw || !th) return false;
        const Modo quer = g_pedido;
        // A DESMONTAGEM VEM PRIMEIRO, e nao e detalhe: e por aqui que os ~300 MB e o device
        // D3D12 do FSR 3.1 sao devolvidos quando a pessoa volta para "DLSS (atual)". Na
        // primeira versao desta entrega o chamador so chamava o Aplicar quando o modo NAO era
        // DLSS - resultado medido em 13/09/2026: 10 trocas em voo sem cair, mas o log dizia
        // `pedido=dlss | efetivo=fsr31 | 294 MB` e o contexto nunca morria.
        if (quer == Dlss || tw <= sw || th <= sh) { TrocarPara(Dlss); return false; }
        // HDR: o EASU/RCAS e o FSR 3.1 aqui trabalham em cor perceptual SDR de 8 bits.
        // Em HDR (PQ/HLG/scRGB FP16) o resultado seria errado - cai com motivo, sem tela preta.
        if (g_hdrBloqueia) { Queda(Dlss, L"a fonte esta em HDR e este ampliador so trata SDR"); return false; }

        if (quer != m_modo) TrocarPara(quer);
        // JA FALHOU NESTE MODO: nao tenta de novo a cada quadro. Sem isto a bancada da queda
        // do FSR 3.1 escreveu 1.400 linhas de log em 35 s, e num caso real (driver sem D3D12,
        // VRAM cheia) seria uma tentativa de criar contexto por quadro. Quem religa a tentativa
        // e a PESSOA: trocar de modo e voltar limpa a marca (ver TrocarPara).
        if (quer == m_falhou) { g_efetivo = Dlss; return false; }

        if (quer == Fsr1) {
            std::wstring e;
            if (!GarantirFsr1(dev, sw, sh, tw, th, &e)) { Queda(Dlss, e); return false; }
            return RodarFsr1(ctx, src, sw, sh, tw, th, vs, rs, vtx, outSrv);
        }
        std::wstring e;
        if (!GarantirFsr31(dev, ctx, src, sw, sh, tw, th, &e)) { Queda(Dlss, e); return false; }
        return RodarFsr31(ctx, src, outSrv);
    }

    /* ms medidos NA GPU do passo de ampliacao (media desde a ultima troca de modo).
       Zero enquanto nao houver amostra. */
    double Ms() const {
        if (m_modo == Fsr1) return m_med11.Media();
        if (m_modo == Fsr31) return m_nFi ? m_somaFi / (double)m_nFi : 0.0;
        return 0.0;
    }
    double VramMb() const { return m_vramMb; }
    /* Zera a media. O painel e o log mostram uma JANELA (os ultimos 10 s) e nao a media desde
       o arranque: com media acumulada os primeiros quadros - contexto subindo, GPU em clock
       baixo - pesam no numero para sempre e ele leva minutos a convergir. Foi medido nesta
       entrega: o estagio de saida do FSR 3.1 aparecia como 12,1 ms e ainda estava caindo
       (3,79 ms) 50 s depois, quando o valor marginal ja era ~1,9 ms. */
    void ZerarMedia() { m_med11.soma = 0; m_med11.n = 0; m_somaFi = 0; m_nFi = 0; }

    void Parar() { TrocarPara(Dlss); m_med11.Parar(); }
    /* MGR-2309 item 3: espera a fila D3D12 do ampliador (FSR 3.1) terminar o que ja foi mandado, com
       prazo. Fence propria (nao mexe em m_fOut). Devolve ms; 0 = nada em voo/sem fila; -1 = prazo. */
    double DrenarGpu(DWORD tetoMs) {
        if (!m_dev12 || !m_fila) return 0.0;
        ID3D12Fence* f = nullptr;
        if (FAILED(m_dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&f)) || !f) return 0.0;
        HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        LARGE_INTEGER a, b, q; QueryPerformanceCounter(&a); QueryPerformanceFrequency(&q);
        bool ok = SUCCEEDED(m_fila->Signal(f, 1)) && f->GetCompletedValue() >= 1;
        if (!ok && ev && SUCCEEDED(f->SetEventOnCompletion(1, ev)))
            ok = WaitForSingleObject(ev, tetoMs) == WAIT_OBJECT_0 || f->GetCompletedValue() >= 1;
        if (ev) CloseHandle(ev);
        f->Release();
        QueryPerformanceCounter(&b);
        return ok ? 1000.0 * (double)(b.QuadPart - a.QuadPart) / (double)q.QuadPart : -1.0;
    }

private:
    // ---------------------------------------------------------------- comum
    void Queda(Modo para, const std::wstring& motivo) {
        if (!motivo.empty() && motivo != g_motivo) {
            g_motivo = motivo;
            if (g_log) {
                const std::string m(motivo.begin(), motivo.end());
                g_log("upscaler: %s NAO subiu (%s) - seguindo no caminho de sempre (DLSS/ampliador direcional)",
                      NomeLog(g_pedido), m.c_str());
            }
            m_falhou = g_pedido;   // nao tenta de novo neste modo ate a pessoa trocar
        }
        g_efetivo = para;
    }
    void TrocarPara(Modo m) {
        if (m == m_modo) return;
        SoltarFsr1();
        SoltarFsr31();
        m_modo = m;
        m_med11.Parar(); m_somaFi = 0; m_nFi = 0; m_vramMb = 0;
        if (m != m_falhou) { g_motivo.clear(); m_falhou = (Modo)-1; }
        g_efetivo = m;
        if (g_log) g_log("upscaler: caminho agora e %s", NomeLog(m));
    }
    static HRESULT Compilar(const char* src, const char* ep, const char* alvo, ID3DBlob** out) {
        typedef HRESULT (WINAPI *PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
        static HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
        static PFN_Compile fn = dc ? (PFN_Compile)(void*)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!fn) return E_FAIL;
        ID3DBlob* err = nullptr;
        const HRESULT hr = fn(src, strlen(src), "fsr1", nullptr, nullptr, ep, alvo, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
        if (FAILED(hr) && err && g_log) g_log("upscaler: %s/%s nao compilou: %.400s", ep, alvo, (const char*)err->GetBufferPointer());
        Soltar(err);
        return hr;
    }

    // ---------------------------------------------------------------- FSR 1
    bool GarantirFsr1(ID3D11Device* dev, UINT sw, UINT sh, UINT tw, UINT th, std::wstring* erro) {
        if (m_psEasu && m_easuTex && m_tw == tw && m_th == th) return true;
        SoltarFsr1();
        m_tw = tw; m_th = th;
        if (!m_psEasu) {
            ID3DBlob* b = nullptr;
            // ps_5_0 primeiro (rcp/ddx sem surpresa); ps_4_0 e a reserva das maquinas antigas -
            // o feed_fsr1.h foi escrito de proposito para compilar nos dois (so Loads, sem Gather).
            if (FAILED(Compilar(kFsr1Src, "ps_easu", "ps_5_0", &b)) && FAILED(Compilar(kFsr1Src, "ps_easu", "ps_4_0", &b)))
            { *erro = L"o shader EASU nao compilou"; return false; }
            dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &m_psEasu); b->Release();
            b = nullptr;
            if (FAILED(Compilar(kFsr1Src, "ps_rcas", "ps_5_0", &b)) && FAILED(Compilar(kFsr1Src, "ps_rcas", "ps_4_0", &b)))
            { *erro = L"o shader RCAS nao compilou"; return false; }
            dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &m_psRcas); b->Release();
            if (!m_psEasu || !m_psRcas) { *erro = L"CreatePixelShader do FSR 1 falhou"; return false; }
            D3D11_BUFFER_DESC cb{};
            cb.ByteWidth = sizeof(FsrConstants); cb.Usage = D3D11_USAGE_DYNAMIC;
            cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(dev->CreateBuffer(&cb, nullptr, &m_cb))) { *erro = L"cbuffer do FSR 1"; return false; }
            m_med11.Iniciar(dev);
        }
        if (!CriarAlvo(dev, tw, th, &m_easuTex, &m_easuRtv, &m_easuSrv) ||
            !CriarAlvo(dev, tw, th, &m_rcasTex, &m_rcasRtv, &m_rcasSrv))
        { *erro = L"texturas intermediarias do FSR 1"; return false; }
        if (g_log) g_log("upscaler: FSR 1 (EASU+RCAS) de pe, %ux%u -> %ux%u, nitidez %.2f (= %.2f stops do FSR), 2 alvos B8G8R8A8 (%.0f MB)",
                         sw, sh, tw, th, g_nitidez, (1.0f - g_nitidez) * 2.0f, 2.0 * tw * th * 4.0 / 1048576.0);
        return true;
    }
    static bool CriarAlvo(ID3D11Device* dev, UINT w, UINT h, ID3D11Texture2D** t, ID3D11RenderTargetView** rtv, ID3D11ShaderResourceView** srv) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, t))) return false;
        if (FAILED(dev->CreateRenderTargetView(*t, nullptr, rtv))) return false;
        return SUCCEEDED(dev->CreateShaderResourceView(*t, nullptr, srv));
    }
    bool RodarFsr1(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src, UINT sw, UINT sh, UINT tw, UINT th,
                   ID3D11VertexShader* vs, ID3D11RasterizerState* rs, UINT vtx, ID3D11ShaderResourceView** outSrv) {
        FsrConstants c{};
        FsrFillConstants(&c, sw, sh, tw, th, g_nitidez);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(ctx->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &c, sizeof(c)); ctx->Unmap(m_cb, 0); }

        const int slot = (int)(m_n++ % PonteGpu::kSlots);
        m_med11.Antes(ctx, slot);

        D3D11_VIEWPORT vp{ 0.f, 0.f, (FLOAT)tw, (FLOAT)th, 0.f, 1.f };
        ctx->RSSetViewports(1, &vp);
        ctx->RSSetState(rs);
        ctx->OMSetDepthStencilState(nullptr, 0);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs, nullptr, 0);
        ID3D11Buffer* cbs[1] = { m_cb };
        ctx->PSSetConstantBuffers(0, 1, cbs);

        ID3D11ShaderResourceView* nula = nullptr;
        // 1) EASU: a ampliacao propriamente dita (kernel lanczos-like girado pela borda)
        ctx->OMSetRenderTargets(1, &m_easuRtv, nullptr);
        ctx->PSSetShader(m_psEasu, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &src);
        ctx->Draw(vtx, 0);
        ctx->PSSetShaderResources(0, 1, &nula);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);

        // 2) RCAS: realce limitado por contraste, em espaco de SAIDA. Com nitidez 0 o passe
        //    nao existe (o proprio feed_fsr1.h diz que quem chama e que pula) e o EASU vai direto.
        if (g_nitidez > 0.001f) {
            ctx->OMSetRenderTargets(1, &m_rcasRtv, nullptr);
            ctx->PSSetShader(m_psRcas, nullptr, 0);
            ctx->PSSetShaderResources(0, 1, &m_easuSrv);
            ctx->Draw(vtx, 0);
            ctx->PSSetShaderResources(0, 1, &nula);
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            *outSrv = m_rcasSrv;
        } else {
            *outSrv = m_easuSrv;
        }
        m_med11.Depois(ctx, slot);
        g_efetivo = Fsr1;
        return true;
    }
    void SoltarFsr1() {
        Soltar(m_easuSrv); Soltar(m_easuRtv); Soltar(m_easuTex);
        Soltar(m_rcasSrv); Soltar(m_rcasRtv); Soltar(m_rcasTex);
        Soltar(m_psEasu); Soltar(m_psRcas); Soltar(m_cb);
        m_tw = m_th = 0;
    }

    // ---------------------------------------------------------------- FSR 3.1
    /* O ffx-api so tem backend D3D12 e o nosso Renderer e D3D11. E o mesmo arranjo que o
       fg_ffx.h ja usa e que esta provado nesta maquina: device D3D12 no MESMO adaptador,
       texturas que NASCEM no D3D11 (NTHANDLE) e sao ABERTAS no D3D12 - o inverso devolve
       E_INVALIDARG - e duas fences compartilhadas para o ping-pong. */
    bool GarantirFsr31(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src,
                       UINT sw, UINT sh, UINT tw, UINT th, std::wstring* erro) {
        (void)ctx;
        if (m_ctxFfx && m_sw == sw && m_sh == sh && m_tw == tw && m_th == th) return true;
        SoltarFsr31();
        m_sw = sw; m_sh = sh; m_tw = tw; m_th = th;
        {   // BANCADA: simula a falha de criacao para exercitar a queda (nada acontece sem a variavel)
            wchar_t e[8]{};
            if (GetEnvironmentVariableW(L"DLSS5_UPSCALER_FALHA", e, 8) > 0 && e[0] == L'1') {
                static bool disse = false;
                if (g_log && !disse) { disse = true; g_log("upscaler: BANCADA - DLSS5_UPSCALER_FALHA=1: fingindo que o contexto do FSR 3.1 nao subiu (so responde a esta variavel; ausente, nada acontece)"); }
                *erro = L"BANCADA: falha simulada por DLSS5_UPSCALER_FALHA=1"; return false;
            }
        }
        if (FAILED(dev->QueryInterface(__uuidof(ID3D11Device5), (void**)&m_dev5)) || !m_dev5) { *erro = L"o device da rede nao tem ID3D11Device5"; return false; }
        { ID3D11DeviceContext* c = nullptr; dev->GetImmediateContext(&c);
          if (!c || FAILED(c->QueryInterface(__uuidof(ID3D11DeviceContext4), (void**)&m_ctx4))) { Soltar(c); *erro = L"o contexto da rede nao tem ID3D11DeviceContext4"; return false; }
          Soltar(c); }

        // o adaptador do proprio device da rede (o D3D12 tem de nascer no MESMO silicio)
        IDXGIAdapter* ad = nullptr;
        { IDXGIDevice* dd = nullptr;
          if (SUCCEEDED(dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dd)) && dd) { dd->GetAdapter(&ad); dd->Release(); } }
        if (!ad) { *erro = L"nao achei o adaptador do device da rede"; return false; }

        m_dllMod = LoadLibraryW(g_dll.c_str());
        if (!m_dllMod) { ad->Release(); *erro = L"nao carreguei o amd_fidelityfx_dx12.dll"; return false; }
        m_Create   = (PfnFfxCreateContext)(void*)GetProcAddress(m_dllMod, "ffxCreateContext");
        m_Destroy  = (PfnFfxDestroyContext)(void*)GetProcAddress(m_dllMod, "ffxDestroyContext");
        m_Dispatch = (PfnFfxDispatch)(void*)GetProcAddress(m_dllMod, "ffxDispatch");
        m_Query    = (PfnFfxQuery)(void*)GetProcAddress(m_dllMod, "ffxQuery");
        if (!m_Create || !m_Destroy || !m_Dispatch || !m_Query) { ad->Release(); *erro = L"a DLL nao tem os exports do ffx-api"; return false; }

        HRESULT hr = D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&m_dev12);
        ad->Release();
        if (FAILED(hr) || !m_dev12) { *erro = L"D3D12CreateDevice falhou no adaptador da rede"; return false; }
        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
        if (FAILED(m_dev12->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&m_fila))) {
            qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            if (FAILED(m_dev12->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&m_fila))) { *erro = L"CreateCommandQueue"; return false; }
        }
        for (int i = 0; i < 2; i++)
            if (FAILED(m_dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&m_alloc[i]))) { *erro = L"CreateCommandAllocator"; return false; }
        if (FAILED(m_dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc[0], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&m_lista))) { *erro = L"CreateCommandList"; return false; }
        m_lista->Close();
        if (FAILED(m_dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&m_fIn)) ||
            FAILED(m_dev12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&m_fOut))) { *erro = L"CreateFence"; return false; }
        m_evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_fila->GetTimestampFrequency(&m_tsFreq);
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count = 4;
        m_dev12->CreateQueryHeap(&qh, __uuidof(ID3D12QueryHeap), (void**)&m_qh);
        CriarBuffer12(64, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, &m_qbuf);

        // a ENTRADA compartilhada: mesmo formato do recurso capturado, para um CopyResource seco
        {
            ID3D11Resource* r = nullptr; src->GetResource(&r);
            ID3D11Texture2D* t = nullptr;
            if (r) r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t);
            Soltar(r);
            if (!t) { *erro = L"a captura nao e uma Texture2D"; return false; }
            D3D11_TEXTURE2D_DESC td{}; t->GetDesc(&td); t->Release();
            m_fmtIn = td.Format;
            if (td.Width != sw || td.Height != sh) { *erro = L"a captura mudou de tamanho no meio do caminho"; return false; }
        }
        if (!CriarTex11(m_fmtIn, sw, sh, D3D11_BIND_SHADER_RESOURCE, &m_in11, &m_in12, erro, L"entrada")) return false;
        // a SAIDA: R8G8B8A8_UNORM porque e o formato que o FSR 3.1 escreve por UAV sem surpresa
        // (foi o do banco de provas); o PS principal le .rgb e o resultado e o mesmo do BGRA.
        if (!CriarTex11(DXGI_FORMAT_R8G8B8A8_UNORM, tw, th, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, &m_out11, &m_out12, erro, L"saida")) return false;
        if (FAILED(dev->CreateShaderResourceView(m_out11, nullptr, &m_outSrv))) { *erro = L"SRV da saida do FSR 3.1"; return false; }

        // Profundidade constante 1,0 e campo de movimento ZERADO. Nao e enfeite: MEDIDO em
        // 13/09/2026, o FSR 3.1 SEM passar depth/MV custa MAIS (7,2 / 11,5 ms) do que passando
        // as duas texturas (1,7 / 3,5 ms) - anomalia sem causa conhecida, mas estavel.
        if (!Preencher12(&m_depth12, DXGI_FORMAT_R32_FLOAT, sw, sh, 1.0f, erro)) return false;
        if (!Preencher12(&m_mv12, DXGI_FORMAT_R16G16_FLOAT, sw, sh, 0.0f, erro)) return false;
        if (!Preencher12(&m_expo12, DXGI_FORMAT_R32_FLOAT, 1, 1, 1.0f, erro)) return false;

        ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = m_dev12;
        ffxCreateContextDescUpscale cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE; cd.header.pNext = &be.header;
        cd.flags = FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;   // a nossa cor e perceptual (sRGB), nao linear
        cd.maxRenderSize = { sw, sh };
        cd.maxUpscaleSize = { tw, th };
        const double t0 = Agora();
        const ffxReturnCode_t rc = m_Create(&m_ctxFfx, &cd.header, nullptr);
        if (rc != FFX_API_RETURN_OK || !m_ctxFfx) {
            wchar_t b[96]; _snwprintf(b, 96, L"ffxCreateContext(UPSCALE) devolveu %u", (unsigned)rc); b[95] = 0;
            *erro = b; m_ctxFfx = nullptr; return false;
        }
        { ffxQueryDescUpscaleGetGPUMemoryUsage mu{}; FfxApiEffectMemoryUsage use{};
          mu.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GPU_MEMORY_USAGE; mu.gpuMemoryUsageUpscaler = &use;
          if (m_Query(&m_ctxFfx, &mu.header) == FFX_API_RETURN_OK) m_vramMb = use.totalUsageInBytes / 1048576.0; }
        if (g_log) g_log("upscaler: FSR 3.1 de pe, %ux%u -> %ux%u em %.0f ms (%.0f MB de VRAM) - reset=true FORCADO todo quadro, sem opcao de desligar",
                         sw, sh, tw, th, Agora() - t0, m_vramMb);
        return true;
    }
    bool RodarFsr31(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* src, ID3D11ShaderResourceView** outSrv) {
        ID3D11Resource* r = nullptr; src->GetResource(&r);
        if (!r) return false;
        ctx->CopyResource(m_in11, r);
        r->Release();
        const uint64_t n = ++m_n12;
        const int slot = (int)(n % 2);
        m_ctx4->Signal(m_fIn11, n);
        ctx->Flush();

        if (m_fOut->GetCompletedValue() + 2 < n) EsperarFence(m_fOut, n - 2);
        m_alloc[slot]->Reset(); m_lista->Reset(m_alloc[slot], nullptr);
        {
            D3D12_RESOURCE_BARRIER b[2] = {
                Bar(m_in12, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                Bar(m_out12, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) };
            m_lista->ResourceBarrier(2, b);
        }
        LerTimestamp(slot);
        m_lista->EndQuery(m_qh, D3D12_QUERY_TYPE_TIMESTAMP, slot * 2);

        ffxDispatchDescUpscale dd{};
        dd.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        dd.commandList = m_lista;
        dd.color = ffxApiGetResourceDX12(m_in12, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.depth = ffxApiGetResourceDX12(m_depth12, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.motionVectors = ffxApiGetResourceDX12(m_mv12, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.exposure = ffxApiGetResourceDX12(m_expo12, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.output = ffxApiGetResourceDX12(m_out12, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dd.jitterOffset = { 0.0f, 0.0f };          // captura nao tem jitter, e por isso o temporal nao acumula
        dd.motionVectorScale = { 1.0f, 1.0f };
        dd.renderSize = { m_sw, m_sh };
        dd.upscaleSize = { m_tw, m_th };
        dd.enableSharpening = (g_nitidez > 0.001f);
        dd.sharpness = g_nitidez;
        dd.frameTimeDelta = 16.6f;
        dd.preExposure = 1.0f;
        // ---------------------------------------------------------------------------
        //  ESTA LINHA E A REGRA, e nao ha chave para desliga-la. Sem MV e sem jitter o
        //  historico do FSR 3.1 so acumula lixo: 31,55 dB, SSIM 0,9125 e arrasto visivel
        //  (medido 13/09/2026). Com reset todo quadro sobe para 37,09 dB e vira um
        //  ampliador espacial honesto - que e exatamente o que pedimos dele aqui.
        dd.reset = true;
        // ---------------------------------------------------------------------------
        dd.cameraNear = 0.1f; dd.cameraFar = 10000.0f; dd.cameraFovAngleVertical = 1.0f;
        dd.viewSpaceToMetersFactor = 1.0f;
        dd.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
        const ffxReturnCode_t rd = m_Dispatch(&m_ctxFfx, &dd.header);

        m_lista->EndQuery(m_qh, D3D12_QUERY_TYPE_TIMESTAMP, slot * 2 + 1);
        m_lista->ResolveQueryData(m_qh, D3D12_QUERY_TYPE_TIMESTAMP, slot * 2, 2, m_qbuf, slot * 16);
        {
            D3D12_RESOURCE_BARRIER b[2] = {
                Bar(m_in12, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
                Bar(m_out12, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON) };
            m_lista->ResourceBarrier(2, b);
        }
        m_lista->Close();
        ID3D12CommandList* ls[] = { m_lista };
        m_fila->Wait(m_fIn, n);
        m_fila->ExecuteCommandLists(1, ls);
        m_fila->Signal(m_fOut, n);
        m_tsPend[slot] = true;
        if (rd != FFX_API_RETURN_OK) {
            if (++m_errosFfx <= 3 && g_log) g_log("upscaler: ffxDispatch(UPSCALE) devolveu %u no quadro %llu", (unsigned)rd, (unsigned long long)n);
            if (m_errosFfx >= 30) { Queda(Dlss, L"o ffxDispatch do FSR 3.1 falhou 30 vezes seguidas"); return false; }
        } else m_errosFfx = 0;
        // a espera pela saida fica NA GPU do D3D11 (o desenho do quadro fica atras dela);
        // esperar aqui na CPU custaria um quadro inteiro - ver a armadilha 22 do dossie.
        m_ctx4->Wait(m_fOut11, n);
        *outSrv = m_outSrv;
        g_efetivo = Fsr31;
        return true;
    }
    void SoltarFsr31() {
        if (m_ctxFfx && m_Destroy) { EsperarFila(); m_Destroy(&m_ctxFfx, nullptr); }
        m_ctxFfx = nullptr;
        Soltar(m_outSrv); Soltar(m_out11); Soltar(m_out12);
        Soltar(m_in11); Soltar(m_in12);
        Soltar(m_depth12); Soltar(m_mv12); Soltar(m_expo12);
        Soltar(m_qbuf); Soltar(m_qh);
        Soltar(m_lista); Soltar(m_alloc[0]); Soltar(m_alloc[1]);
        Soltar(m_fIn11); Soltar(m_fOut11); Soltar(m_fIn); Soltar(m_fOut);
        Soltar(m_fila); Soltar(m_dev12);
        Soltar(m_ctx4); Soltar(m_dev5);
        if (m_evt) { CloseHandle(m_evt); m_evt = nullptr; }
        if (m_dllMod) { FreeLibrary(m_dllMod); m_dllMod = nullptr; }
        m_n12 = 0; m_errosFfx = 0; m_tsPend[0] = m_tsPend[1] = false;
        m_sw = m_sh = 0;
    }
    bool CriarTex11(DXGI_FORMAT fmt, UINT w, UINT h, UINT bind, ID3D11Texture2D** t11, ID3D12Resource** t12, std::wstring* erro, const wchar_t* nome) {
        D3D11_TEXTURE2D_DESC td{}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = fmt;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = bind;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
        if (FAILED(m_dev5->CreateTexture2D(&td, nullptr, t11)) || !*t11) { *erro = std::wstring(L"CreateTexture2D da ") + nome; return false; }
        IDXGIResource1* r1 = nullptr; HANDLE hh = nullptr;
        (*t11)->QueryInterface(__uuidof(IDXGIResource1), (void**)&r1);
        const HRESULT hr = r1 ? r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &hh) : E_FAIL;
        Soltar(r1);
        if (FAILED(hr) || !hh) { *erro = std::wstring(L"CreateSharedHandle da ") + nome; return false; }
        const HRESULT ho = m_dev12->OpenSharedHandle(hh, __uuidof(ID3D12Resource), (void**)t12);
        CloseHandle(hh);
        if (FAILED(ho)) { *erro = std::wstring(L"OpenSharedHandle da ") + nome; return false; }
        // as fences so precisam ser abertas uma vez
        if (!m_fIn11 && !AbrirFence(m_fIn, &m_fIn11, erro)) return false;
        if (!m_fOut11 && !AbrirFence(m_fOut, &m_fOut11, erro)) return false;
        return true;
    }
    bool AbrirFence(ID3D12Fence* f, ID3D11Fence** out, std::wstring* erro) {
        HANDLE h = nullptr;
        if (FAILED(m_dev12->CreateSharedHandle(f, nullptr, GENERIC_ALL, nullptr, &h)) || !h) { *erro = L"CreateSharedHandle da fence"; return false; }
        const HRESULT hr = m_dev5->OpenSharedFence(h, __uuidof(ID3D11Fence), (void**)out);
        CloseHandle(h);
        if (FAILED(hr)) { *erro = L"OpenSharedFence"; return false; }
        return true;
    }
    // textura D3D12 preenchida com um valor constante (profundidade 1,0 / MV 0 / exposicao 1,0)
    bool Preencher12(ID3D12Resource** out, DXGI_FORMAT fmt, UINT w, UINT h, float v, std::wstring* erro) {
        const UINT bpp = (fmt == DXGI_FORMAT_R32_FLOAT) ? 4u : 4u;   // R16G16_FLOAT tambem sao 4 bytes
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width = w; d.Height = h;
        d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = fmt; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(m_dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)out)))
        { *erro = L"textura constante do FSR 3.1"; return false; }
        const UINT pitch = (w * bpp + 255) & ~255u;
        ID3D12Resource* up = nullptr;
        if (!CriarBuffer12((UINT64)pitch * h, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, &up)) { *erro = L"upload da textura constante"; return false; }
        uint8_t* m = nullptr; up->Map(0, nullptr, (void**)&m);
        for (UINT y = 0; y < h; y++) {
            uint8_t* linha = m + (size_t)y * pitch;
            if (fmt == DXGI_FORMAT_R32_FLOAT) { float* f = (float*)linha; for (UINT x = 0; x < w; x++) f[x] = v; }
            else memset(linha, 0, (size_t)w * bpp);   // half 0,0 = 0x0000 nos dois canais
        }
        up->Unmap(0, nullptr);
        m_alloc[0]->Reset(); m_lista->Reset(m_alloc[0], nullptr);
        D3D12_TEXTURE_COPY_LOCATION dst{}, s{};
        dst.pResource = *out; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        s.pResource = up; s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        s.PlacedFootprint.Footprint.Format = fmt; s.PlacedFootprint.Footprint.Width = w;
        s.PlacedFootprint.Footprint.Height = h; s.PlacedFootprint.Footprint.Depth = 1; s.PlacedFootprint.Footprint.RowPitch = pitch;
        D3D12_RESOURCE_BARRIER b1 = Bar(*out, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST); m_lista->ResourceBarrier(1, &b1);
        m_lista->CopyTextureRegion(&dst, 0, 0, 0, &s, nullptr);
        D3D12_RESOURCE_BARRIER b2 = Bar(*out, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON); m_lista->ResourceBarrier(1, &b2);
        m_lista->Close();
        ID3D12CommandList* ls[] = { m_lista }; m_fila->ExecuteCommandLists(1, ls);
        EsperarFila(); up->Release();
        return true;
    }
    bool CriarBuffer12(UINT64 size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES st, ID3D12Resource** out) {
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = heap;
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = size; d.Height = 1;
        d.DepthOrArraySize = 1; d.MipLevels = 1; d.Format = DXGI_FORMAT_UNKNOWN; d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return SUCCEEDED(m_dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, st, nullptr, __uuidof(ID3D12Resource), (void**)out));
    }
    static D3D12_RESOURCE_BARRIER Bar(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
        D3D12_RESOURCE_BARRIER x{}; x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; x.Transition.pResource = r;
        x.Transition.StateBefore = a; x.Transition.StateAfter = b; x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return x;
    }
    void EsperarFence(ID3D12Fence* f, uint64_t v) {
        if (!f || f->GetCompletedValue() >= v || !m_evt) return;
        // NS-2309 item 5(a): m_evt e auto-reset e serve a todas as esperas deste objeto. Uma espera
        // anterior que estourou deixa o registro armado e acorda ESTA mais cedo (o defeito do
        // NeuralScreen v1.7.1, #33 - ideia, codigo proprio). Limpa antes e so sai com a fence no valor.
        ResetEvent(m_evt);
        f->SetEventOnCompletion(v, m_evt);
        const ULONGLONG fim = GetTickCount64() + Saida::Teto(2000);
        for (ULONGLONG ag = GetTickCount64(); f->GetCompletedValue() < v && ag < fim; ag = GetTickCount64())
            WaitForSingleObject(m_evt, (DWORD)(fim - ag));
    }
    void EsperarFila() {
        if (!m_dev12 || !m_fila) return;
        ID3D12Fence* f = nullptr;
        if (FAILED(m_dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&f)) || !f) return;
        m_fila->Signal(f, 1); EsperarFence(f, 1); f->Release();
    }
    void LerTimestamp(int slot) {
        if (!m_tsPend[slot] || !m_qbuf || !m_tsFreq) return;
        m_tsPend[slot] = false;
        void* m = nullptr; D3D12_RANGE rg{ (SIZE_T)slot * 16, (SIZE_T)slot * 16 + 16 };
        if (SUCCEEDED(m_qbuf->Map(0, &rg, &m))) {
            const UINT64* t = (const UINT64*)((const uint8_t*)m + slot * 16);
            if (t[1] >= t[0]) { m_somaFi += (double)(t[1] - t[0]) * 1000.0 / (double)m_tsFreq; m_nFi++; }
            D3D12_RANGE none{ 0, 0 }; m_qbuf->Unmap(0, &none);
        }
    }
    static double Agora() { LARGE_INTEGER t, f; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / (double)f.QuadPart; }

    // ---- estado
    Modo m_modo = Dlss, m_falhou = (Modo)-1;
    UINT m_sw = 0, m_sh = 0, m_tw = 0, m_th = 0;
    uint64_t m_n = 0;
    double m_vramMb = 0;
    PonteGpu::MedidorGpu m_med11;
    // FSR 1
    ID3D11PixelShader *m_psEasu = nullptr, *m_psRcas = nullptr;
    ID3D11Buffer* m_cb = nullptr;
    ID3D11Texture2D *m_easuTex = nullptr, *m_rcasTex = nullptr;
    ID3D11RenderTargetView *m_easuRtv = nullptr, *m_rcasRtv = nullptr;
    ID3D11ShaderResourceView *m_easuSrv = nullptr, *m_rcasSrv = nullptr;
    // FSR 3.1
    HMODULE m_dllMod = nullptr;
    PfnFfxCreateContext m_Create = nullptr; PfnFfxDestroyContext m_Destroy = nullptr;
    PfnFfxDispatch m_Dispatch = nullptr; PfnFfxQuery m_Query = nullptr;
    ffxContext m_ctxFfx = nullptr;
    ID3D11Device5* m_dev5 = nullptr; ID3D11DeviceContext4* m_ctx4 = nullptr;
    ID3D12Device* m_dev12 = nullptr; ID3D12CommandQueue* m_fila = nullptr;
    ID3D12CommandAllocator* m_alloc[2] = {}; ID3D12GraphicsCommandList* m_lista = nullptr;
    ID3D12Fence *m_fIn = nullptr, *m_fOut = nullptr;
    ID3D11Fence *m_fIn11 = nullptr, *m_fOut11 = nullptr;
    HANDLE m_evt = nullptr;
    ID3D11Texture2D *m_in11 = nullptr, *m_out11 = nullptr;
    ID3D12Resource *m_in12 = nullptr, *m_out12 = nullptr;
    ID3D12Resource *m_depth12 = nullptr, *m_mv12 = nullptr, *m_expo12 = nullptr;
    ID3D11ShaderResourceView* m_outSrv = nullptr;
    DXGI_FORMAT m_fmtIn = DXGI_FORMAT_B8G8R8A8_UNORM;
    ID3D12QueryHeap* m_qh = nullptr; ID3D12Resource* m_qbuf = nullptr;
    UINT64 m_tsFreq = 0; bool m_tsPend[2] = {};
    double m_somaFi = 0; uint64_t m_nFi = 0, m_n12 = 0;
    int m_errosFfx = 0;
};

static Motor g_motor;

} // namespace Upsc
