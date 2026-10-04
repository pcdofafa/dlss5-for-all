/* =====================================================================
   ponte_lav.h -- o nivel "Codec" da ponte entre placas: HEVC pelo motor de
   video da placa de ORIGEM (NVENC na GeForce, AMF/VCN na Radeon), pacote
   minusculo copiado pela CPU, decodificacao D3D11VA na placa de DESTINO.

   POR QUE ISTO EXISTE
   -------------------
   Num slot PCIe ruim (x1, riser: 2.0 x1 ~0,5 GB/s) nem o NV12 4K/60 (750
   MB/s) cabe. O codec leva o quadro para ~0,2 MB (60x menos que o NV12) e
   passa a caber - ao preco de +~15 ms (encode 3-4 + decode 9-13, p95 19 na
   Radeon) e de uma perda pequena (HUD/texto 48-51 dB a 100 Mbps em quadros
   reais do Hogwarts; prova em scratchpad/codec, 12/09/2026). E OPCIONAL: os
   niveis Max (cru) e Alto (NV12) continuam como estavam.

   COMO
   ---
   libavcodec/libavutil (build LGPL do BtbN) carregadas em tempo de execucao
   por LoadLibrary/GetProcAddress - o app continua funcionando sem as DLLs,
   o nivel so fica "indisponivel" com o motivo. A licenca e conferida em
   avcodec_license(): qualquer build GPL e recusado.
     - Entrada do encoder: UMA textura NV12 (pool D3D11 do proprio libav, no
       device da origem, com UAV): o compute shader da ponte escreve os dois
       planos direto nela (zero copia).
     - Saida do decoder: NV12 no pool D3D11VA do device de destino, lida por
       SRV de plano (ou copiada para uma NV12 propria se o driver recusar SRV
       no pool) e reconvertida para BGRA pelo shader NV12->BGRA da ponte.
     - Cor: BT.709 faixa LIMITADA (16-235) fixa nos dois lados - o shader da
       ponte codifica/decodifica com essa faixa quando o codec esta no meio.
     - Resync sem IDR: intra-refresh (GOP infinito, sem B, sem lookahead).
       O descarte do anel acontece ANTES do encoder, entao a cadeia de
       referencias nunca quebra por descarte.
   ===================================================================== */
#pragma once

#include <windows.h>
#include <d3d11_4.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
}

namespace PonteGpu {

/* ------------------------------------------------------------ o carregador */
struct Lav {
#define LAV_FN(nome) decltype(&::nome) nome = nullptr;
    LAV_FN(avcodec_version) LAV_FN(avcodec_license) LAV_FN(avcodec_configuration)
    LAV_FN(avcodec_find_encoder_by_name) LAV_FN(avcodec_find_decoder_by_name)
    LAV_FN(avcodec_alloc_context3) LAV_FN(avcodec_free_context) LAV_FN(avcodec_open2)
    LAV_FN(avcodec_send_frame) LAV_FN(avcodec_receive_packet)
    LAV_FN(avcodec_send_packet) LAV_FN(avcodec_receive_frame)
    LAV_FN(avcodec_get_hw_frames_parameters)
    LAV_FN(av_packet_alloc) LAV_FN(av_packet_free) LAV_FN(av_packet_unref) LAV_FN(av_new_packet)
    LAV_FN(avutil_version)
    LAV_FN(av_frame_alloc) LAV_FN(av_frame_free) LAV_FN(av_frame_unref) LAV_FN(av_frame_ref)
    LAV_FN(av_hwdevice_ctx_alloc) LAV_FN(av_hwdevice_ctx_init)
    LAV_FN(av_hwframe_ctx_alloc) LAV_FN(av_hwframe_ctx_init) LAV_FN(av_hwframe_get_buffer)
    LAV_FN(av_buffer_ref) LAV_FN(av_buffer_unref)
    LAV_FN(av_opt_set) LAV_FN(av_strerror) LAV_FN(av_log_set_level)
    LAV_FN(av_mallocz)
#undef LAV_FN
    HMODULE hUtil = nullptr, hSwr = nullptr, hCodec = nullptr;
    bool ok = false, tentou = false;
    std::wstring motivo;     // por que nao carregou (vazio se ok)
    std::string  licenca, versao;

    // B5: carregar leva ~100 ms e agora comeca numa thread no arranque; call_once faz quem
    // perguntar no meio esperar essa carga em vez de carregar duas vezes.
    static Lav& I() { static Lav l; static std::once_flag uma; std::call_once(uma, [] { l.Carregar(); }); return l; }

    static std::wstring PastaDoExe() {
        wchar_t p[MAX_PATH]{}; GetModuleFileNameW(nullptr, p, MAX_PATH);
        wchar_t* b = wcsrchr(p, L'\\'); if (b) b[1] = 0;
        return p;
    }
    std::string Erro(int r) const { char b[128] = {}; if (av_strerror) av_strerror(r, b, sizeof b); else snprintf(b, sizeof b, "%d", r); return b; }

private:
    HMODULE Abrir(const wchar_t* nome) {
        const std::wstring cheio = PastaDoExe() + nome;
        HMODULE h = LoadLibraryExW(cheio.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!h) h = LoadLibraryW(nome);   // ultima chance: PATH do sistema
        return h;
    }
    template <class F> bool Pega(HMODULE h, const char* nome, F& out) {
        out = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(h, nome)));
        if (!out) { motivo = L"funcao ausente na DLL: " + std::wstring(nome, nome + strlen(nome)); return false; }
        return true;
    }
    void Carregar() {
        tentou = true; ok = false;
        hUtil = Abrir(L"avutil-" L"61" L".dll");
        if (!hUtil) { motivo = L"avutil-61.dll nao encontrada ao lado do exe"; return; }
        hSwr = Abrir(L"swresample-7.dll");
        if (!hSwr) { motivo = L"swresample-7.dll nao encontrada ao lado do exe"; return; }
        hCodec = Abrir(L"avcodec-63.dll");
        if (!hCodec) { motivo = L"avcodec-63.dll nao encontrada ao lado do exe"; return; }
#define LAV_GET(h, nome) if (!Pega(h, #nome, nome)) return;
        LAV_GET(hCodec, avcodec_version) LAV_GET(hCodec, avcodec_license) LAV_GET(hCodec, avcodec_configuration)
        LAV_GET(hCodec, avcodec_find_encoder_by_name) LAV_GET(hCodec, avcodec_find_decoder_by_name)
        LAV_GET(hCodec, avcodec_alloc_context3) LAV_GET(hCodec, avcodec_free_context) LAV_GET(hCodec, avcodec_open2)
        LAV_GET(hCodec, avcodec_send_frame) LAV_GET(hCodec, avcodec_receive_packet)
        LAV_GET(hCodec, avcodec_send_packet) LAV_GET(hCodec, avcodec_receive_frame)
        LAV_GET(hCodec, avcodec_get_hw_frames_parameters)
        LAV_GET(hCodec, av_packet_alloc) LAV_GET(hCodec, av_packet_free) LAV_GET(hCodec, av_packet_unref) LAV_GET(hCodec, av_new_packet)
        LAV_GET(hUtil, avutil_version)
        LAV_GET(hUtil, av_frame_alloc) LAV_GET(hUtil, av_frame_free) LAV_GET(hUtil, av_frame_unref) LAV_GET(hUtil, av_frame_ref)
        LAV_GET(hUtil, av_hwdevice_ctx_alloc) LAV_GET(hUtil, av_hwdevice_ctx_init)
        LAV_GET(hUtil, av_hwframe_ctx_alloc) LAV_GET(hUtil, av_hwframe_ctx_init) LAV_GET(hUtil, av_hwframe_get_buffer)
        LAV_GET(hUtil, av_buffer_ref) LAV_GET(hUtil, av_buffer_unref)
        LAV_GET(hUtil, av_opt_set) LAV_GET(hUtil, av_strerror) LAV_GET(hUtil, av_log_set_level)
        // O EXTRADATA DO DECODER TEM DE SAIR DO HEAP DO LIBAV, nao do nosso: estas DLLs
        // (BtbN) sao ligadas ao ucrtbase e o app ao msvcrt do mingw, entao um calloc nosso
        // liberado pelo av_freep dentro do avcodec_free_context derruba o processo. Foi
        // exatamente o crash de 13/09/2026 em avcodec_free_context(m_dec).
        LAV_GET(hUtil, av_mallocz)
#undef LAV_GET
        // A ABI so vale com o MESMO major dos cabecalhos com que o app foi compilado.
        const unsigned vc = avcodec_version(), vu = avutil_version();
        char vb[64]; snprintf(vb, sizeof vb, "avcodec %u.%u.%u avutil %u.%u.%u", vc >> 16, (vc >> 8) & 255, vc & 255, vu >> 16, (vu >> 8) & 255, vu & 255);
        versao = vb;
        if ((vc >> 16) != LIBAVCODEC_VERSION_MAJOR || (vu >> 16) != LIBAVUTIL_VERSION_MAJOR) {
            wchar_t b[160]; _snwprintf(b, 160, L"versao das DLLs (%hs) diferente da compilada (avcodec %d, avutil %d)", vb, LIBAVCODEC_VERSION_MAJOR, LIBAVUTIL_VERSION_MAJOR); b[159] = 0;
            motivo = b; return;
        }
        // LICENCA: so LGPL. O build GPL do BtbN responde "GPL version 2 or later"; o LGPL,
        // "LGPL version 2.1 or later". Qualquer "GPL" sem o "L" na frente e recusado.
        licenca = avcodec_license() ? avcodec_license() : "";
        { std::string l = licenca; for (auto& c : l) c = (char)toupper((unsigned char)c);
          const size_t p = l.find("GPL");
          const bool lgpl = (p != std::string::npos && p > 0 && l[p - 1] == 'L');
          if (!lgpl) { motivo = L"a DLL do libavcodec nao e LGPL (licenca: " + std::wstring(licenca.begin(), licenca.end()) + L") - recusada"; return; } }
        av_log_set_level(AV_LOG_ERROR);
        ok = true;
    }
};

/* Esta cor de entrada/saida da ponte serve para o codec? (SDR 8 bits so) */
static inline bool CodecAceitaFormato(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_R8G8B8A8_UNORM ||
           f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
}

/* O nivel Codec esta disponivel nesta instalacao? (DLLs + licenca; o hardware so se
   descobre ao abrir o encoder). `motivo` recebe o porque quando nao. */
static inline bool CodecDisponivel(std::wstring* motivo) {
    Lav& l = Lav::I();
    if (!l.ok && motivo) *motivo = l.motivo;
    return l.ok;
}

static inline double AgoraMs() { LARGE_INTEGER t, f; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / (double)f.QuadPart; }

/* ------------------------------------------------------------ o codec em si
   Um encoder no device A e um decoder no device B, para w x h, com `slots` texturas
   de entrada (uma por slot do anel da ponte). Thread: quem chama decide; a classe
   nao tranca nada por conta propria (a ponte usa uma thread por sentido). */
class CodecVideo {
public:
    static constexpr int kMaxSlots = 8;

    /* `faixaLimitada` TEM de casar com o que o shader da ponte escreve na NV12: se o shader
       escreve 16-235 e o encoder acha que recebe 0-255, ele comprime DE NOVO e a imagem sai
       lavada (medido em 12/09/2026: preto 9 -> 23, branco 232 -> 218 com o AMF). */
    bool Iniciar(ID3D11Device* devA, ID3D11Device* devB, UINT w, UINT h, int slots, int mbps, bool faixaLimitada, std::wstring* erro) {
        Parar();
        Lav& L = Lav::I();
        if (!L.ok) { if (erro) *erro = L.motivo; return false; }
        if (slots < 1 || slots > kMaxSlots || !devA || !devB || !w || !h) { if (erro) *erro = L"codec: parametros invalidos"; return false; }
        m_w = w; m_h = h; m_slots = slots; m_devA = devA; m_devB = devB; devA->AddRef(); devB->AddRef();
        devA->GetImmediateContext(&m_ctxA); devB->GetImmediateContext(&m_ctxB);

        // Bitrate proporcional aos pixels (100 Mbps e o alvo em 4K; 1440p ~44, 1080p ~25).
        m_mbps = mbps;
        const double escala = (double)w * h / (3840.0 * 2160.0);
        const int64_t bps = std::max<int64_t>(10, (int64_t)(mbps * escala + 0.5)) * 1000000;

        m_vendorA = Vendor(devA); m_vendorB = Vendor(devB);
        const char* nomeEnc = (m_vendorA == 0x10DE) ? "hevc_nvenc" : (m_vendorA == 0x1002) ? "hevc_amf" : nullptr;
        if (!nomeEnc) { if (erro) *erro = L"codec: a placa de origem nao e NVIDIA nem AMD (sem NVENC/AMF)"; Parar(); return false; }
        m_nomeEnc = nomeEnc;

        // ---- device do libav = o device do app (zero copia). A protecao multithread do
        // contexto imediato e obrigatoria: o decoder submete video na thread do codec
        // enquanto quem apresenta usa o mesmo contexto (D3D11 nao protege sozinho).
        ProtegerContexto(m_ctxA); ProtegerContexto(m_ctxB);
        if (!CriarDeviceLav(devA, &m_hwA, erro) || !CriarDeviceLav(devB, &m_hwB, erro)) { Parar(); return false; }

        // ---- encoder
        const AVCodec* ec = L.avcodec_find_encoder_by_name(nomeEnc);
        if (!ec) { if (erro) *erro = L"codec: encoder " + std::wstring(m_nomeEnc.begin(), m_nomeEnc.end()) + L" nao existe nesta DLL"; Parar(); return false; }
        m_enc = L.avcodec_alloc_context3(ec);
        m_enc->width = (int)w; m_enc->height = (int)h;
        m_enc->time_base = { 1, 60 }; m_enc->framerate = { 60, 1 };
        m_enc->bit_rate = bps; m_enc->rc_max_rate = bps; m_enc->rc_min_rate = bps;
        m_enc->rc_buffer_size = (int)(bps / 60 * 2);   // VBV de 2 quadros (critico, 12/09/2026)
        m_enc->gop_size = 1 << 20;                      // GOP "infinito": resync por intra-refresh
        m_enc->max_b_frames = 0; m_enc->refs = 1;
        m_enc->flags |= AV_CODEC_FLAG_LOW_DELAY;
        m_enc->pix_fmt = AV_PIX_FMT_D3D11;
        m_faixaLim = faixaLimitada;
        const enum AVColorRange faixa = faixaLimitada ? AVCOL_RANGE_MPEG : AVCOL_RANGE_JPEG;
        m_enc->color_range = faixa; m_enc->colorspace = AVCOL_SPC_BT709;
        m_enc->color_primaries = AVCOL_PRI_BT709; m_enc->color_trc = AVCOL_TRC_BT709;
        auto opt = [&](const char* k, const char* v) { if (L.av_opt_set(m_enc->priv_data, k, v, 0) < 0) m_opcoesRecusadas += std::string(k) + "=" + v + " "; };
        if (m_vendorA == 0x10DE) {
            opt("preset", "p1"); opt("tune", "ull"); opt("rc", "cbr"); opt("delay", "0"); opt("zerolatency", "1");
            opt("rc-lookahead", "0"); opt("multipass", "0"); opt("bf", "0"); opt("intra-refresh", "1"); opt("forced-idr", "1");
        } else {
            // O AMF nao tem intra-refresh no libav. Com GOP infinito a imagem CONGELA na
            // qualidade do primeiro quadro quando a cena para de mudar (medido 12/09/2026:
            // padrao parado a 44 e a 400 Mbps saiu identico, com pacotes de ~0 byte). Dai o
            // GOP de 2 s + cabecalhos por GOP (que tambem matam o "VPS 0 does not exist"
            // do decodificador no primeiro pacote).
            m_enc->gop_size = 120;
            opt("header_insertion_mode", "gop"); opt("gops_per_idr", "1");
            opt("usage", "ultralowlatency"); opt("rc", "cbr"); opt("latency", "1"); opt("async_depth", "1");
            opt("preencode", "0"); opt("preanalysis", "0"); opt("enforce_hrd", "1"); opt("filler_data", "0"); opt("skip_frame", "0");
            // AMF ignora bitrate alto se o QP minimo prende: solta o piso para ele gastar o alvo.
            opt("min_qp_i", "0"); opt("min_qp_p", "0"); opt("max_qp_i", "51"); opt("max_qp_p", "51");
        }
        // pool de entrada: NV12 no device A, com UAV para o shader da ponte escrever os planos
        m_frA = L.av_hwframe_ctx_alloc(m_hwA);
        if (!m_frA) { if (erro) *erro = L"codec: av_hwframe_ctx_alloc (entrada) falhou"; Parar(); return false; }
        {
            AVHWFramesContext* fc = (AVHWFramesContext*)m_frA->data;
            fc->format = AV_PIX_FMT_D3D11; fc->sw_format = AV_PIX_FMT_NV12; fc->width = (int)w; fc->height = (int)h;
            // initial_pool_size = 0 DE PROPOSITO: com pool o libav cria UMA textura NV12 em
            // ARRAY, e nenhuma das duas placas aceita array de NV12 com bind que nao seja
            // DECODER (E_INVALIDARG nas duas, medido em 12/09/2026 com scratchpad/nv12_views).
            // Com pool 0 ele aloca uma textura ArraySize=1 por quadro, e ai a UAV do plano
            // sai - que e o que o shader precisa para escrever a NV12 sem copia nenhuma.
            fc->initial_pool_size = 0;
            AVD3D11VAFramesContext* d3 = (AVD3D11VAFramesContext*)fc->hwctx;
            d3->BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            int r = L.av_hwframe_ctx_init(m_frA);
            if (r < 0) {
                if (erro) *erro = L"codec: o driver da origem nao cria NV12 com UAV (" + std::wstring(L.Erro(r).begin(), L.Erro(r).end()) + L")";
                Parar(); return false;
            }
        }
        m_enc->hw_device_ctx = L.av_buffer_ref(m_hwA);
        m_enc->hw_frames_ctx = L.av_buffer_ref(m_frA);
        int r = L.avcodec_open2(m_enc, ec, nullptr);
        if (r < 0) { if (erro) *erro = L"codec: avcodec_open2 (" + std::wstring(m_nomeEnc.begin(), m_nomeEnc.end()) + L") falhou: " + std::wstring(L.Erro(r).begin(), L.Erro(r).end()); Parar(); return false; }
        for (int i = 0; i < slots; i++) {
            m_frameEnc[i] = L.av_frame_alloc();
            r = L.av_hwframe_get_buffer(m_frA, m_frameEnc[i], 0);
            if (r < 0 || !m_frameEnc[i]->data[0]) { if (erro) *erro = L"codec: av_hwframe_get_buffer (entrada) falhou"; Parar(); return false; }
            m_frameEnc[i]->color_range = faixa; m_frameEnc[i]->colorspace = AVCOL_SPC_BT709;
            m_frameEnc[i]->color_primaries = AVCOL_PRI_BT709; m_frameEnc[i]->color_trc = AVCOL_TRC_BT709;
            ID3D11Texture2D* tex = (ID3D11Texture2D*)m_frameEnc[i]->data[0];
            const UINT idx = (UINT)(intptr_t)m_frameEnc[i]->data[1];
            if (!CriarUavPlano(devA, tex, idx, DXGI_FORMAT_R8_UNORM, &m_uavY[i], erro) ||
                !CriarUavPlano(devA, tex, idx, DXGI_FORMAT_R8G8_UNORM, &m_uavUV[i], erro)) { Parar(); return false; }
        }
        m_pkt = L.av_packet_alloc();

        // ---- decoder (hevc + hwaccel d3d11va no device B)
        const AVCodec* dc = L.avcodec_find_decoder_by_name("hevc");
        if (!dc) { if (erro) *erro = L"codec: decoder hevc nao existe nesta DLL"; Parar(); return false; }
        m_dec = L.avcodec_alloc_context3(dc);
        m_dec->flags |= AV_CODEC_FLAG_LOW_DELAY;
        m_dec->thread_count = 1; m_dec->thread_type = 0;
        m_dec->hw_device_ctx = L.av_buffer_ref(m_hwB);
        m_dec->opaque = this;
        m_dec->get_format = &EscolherFormato;
        if (m_enc->extradata_size) {
            // O EXTRADATA SAI DO HEAP DO LIBAV (av_mallocz), NUNCA DO NOSSO. Quem o libera e o
            // av_freep de dentro do avcodec_free_context, e estas DLLs (BtbN) estao ligadas ao
            // ucrtbase enquanto o app usa o msvcrt do mingw: um calloc nosso liberado la dentro
            // e heap corrompido na certa. Era esse o crash de 13/09/2026 - fatal quando a guarda
            // de runtime passou a derrubar a ponte de Codec em pleno uso, e antes disso apenas
            // escondido no encerramento (log de 12/09 23:23, mesma pilha: avcodec -> free).
            // O VPS/SPS/PPS tambem vai no primeiro pacote (forced-idr/intra-refresh no NVENC,
            // header_insertion_mode=gop no AMF), entao sem extradata o decoder ainda sincroniza.
            if (void* ed = L.av_mallocz((size_t)m_enc->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE)) {
                memcpy(ed, m_enc->extradata, m_enc->extradata_size);
                m_dec->extradata = (uint8_t*)ed;
                m_dec->extradata_size = m_enc->extradata_size;
            }
        }
        r = L.avcodec_open2(m_dec, dc, nullptr);
        if (r < 0) { if (erro) *erro = L"codec: avcodec_open2 (hevc/d3d11va) falhou: " + std::wstring(L.Erro(r).begin(), L.Erro(r).end()); Parar(); return false; }
        m_dpkt = L.av_packet_alloc();
        m_frameTmp = L.av_frame_alloc();
        for (int i = 0; i < slots; i++) m_frameDec[i] = L.av_frame_alloc();
        { D3D11_QUERY_DESC q{ D3D11_QUERY_EVENT, 0 }; devB->CreateQuery(&q, &m_qDec); }
        m_ok = true;
        return true;
    }

    bool Ativo() const { return m_ok; }
    const std::string& NomeEncoder() const { return m_nomeEnc; }
    const std::string& OpcoesRecusadas() const { return m_opcoesRecusadas; }
    int Mbps() const { return m_mbps; }
    bool DecoderCopia() const { return m_decCopia; }
    ID3D11UnorderedAccessView* UavY(int slot)  { return m_uavY[slot]; }
    ID3D11UnorderedAccessView* UavUV(int slot) { return m_uavUV[slot]; }
    ID3D11Texture2D* TexEntrada(int slot) { return m_frameEnc[slot] ? (ID3D11Texture2D*)m_frameEnc[slot]->data[0] : nullptr; }
    UINT IndiceEntrada(int slot) { return m_frameEnc[slot] ? (UINT)(intptr_t)m_frameEnc[slot]->data[1] : 0; }

    /* Codifica o slot (a textura ja escrita e visivel para a GPU). Devolve os pacotes
       concatenados em `out` (normalmente um). `forcarI` pede um quadro chave (so em
       recuperacao de erro do decoder; nunca no fluxo normal). */
    bool Codificar(int slot, uint64_t n, bool forcarI, std::vector<uint8_t>& out, double* ms, std::wstring* erro) {
        Lav& L = Lav::I();
        AVFrame* f = m_frameEnc[slot];
        f->pts = (int64_t)n;
        f->pict_type = forcarI ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
        if (forcarI) f->flags |= AV_FRAME_FLAG_KEY; else f->flags &= ~AV_FRAME_FLAG_KEY;
        const double t0 = AgoraMs();
        int r = L.avcodec_send_frame(m_enc, f);
        if (r < 0) { if (erro) *erro = L"send_frame: " + std::wstring(L.Erro(r).begin(), L.Erro(r).end()); return false; }
        out.clear();
        int got = 0;
        for (;;) {
            r = L.avcodec_receive_packet(m_enc, m_pkt);
            if (r == AVERROR(EAGAIN)) {
                if (got) break;
                // AMF as vezes entrega no ciclo seguinte: espera curta (o bench mediu ~6 ms)
                if (AgoraMs() - t0 > 200.0) { if (erro) *erro = L"o encoder nao devolveu pacote em 200 ms"; return false; }
                Sleep(0); continue;
            }
            if (r == AVERROR_EOF) break;
            if (r < 0) { if (erro) *erro = L"receive_packet: " + std::wstring(L.Erro(r).begin(), L.Erro(r).end()); return false; }
            out.insert(out.end(), m_pkt->data, m_pkt->data + m_pkt->size);
            got++;
            L.av_packet_unref(m_pkt);
        }
        if (ms) *ms = AgoraMs() - t0;
        return got > 0;
    }

    /* Decodifica um pacote no device B; o quadro fica preso no slot ate o proximo uso do
       slot. `msSubmit` = ate o libav devolver o quadro; `msGpu` = ate a GPU terminar
       (query de evento, espera por polling - a thread do codec nao tem mais o que fazer). */
    bool Decodificar(const uint8_t* p, size_t tam, int slot, uint64_t n, double* msSubmit, double* msGpu, std::wstring* erro) {
        Lav& L = Lav::I();
        if (tam == 0 || tam > (6u << 20)) { if (erro) *erro = L"pacote vazio ou grande demais para o D3D11VA (>6 MB)"; return false; }
        const double t0 = AgoraMs();
        L.av_packet_unref(m_dpkt);
        if (L.av_new_packet(m_dpkt, (int)tam) < 0) { if (erro) *erro = L"av_new_packet"; return false; }
        memcpy(m_dpkt->data, p, tam);
        m_dpkt->pts = (int64_t)n; m_dpkt->dts = m_dpkt->pts;
        int r = L.avcodec_send_packet(m_dec, m_dpkt);
        if (r < 0) { if (erro) *erro = L"send_packet: " + std::wstring(L.Erro(r).begin(), L.Erro(r).end()); return false; }
        int got = 0;
        for (;;) {
            r = L.avcodec_receive_frame(m_dec, m_frameTmp);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
            if (r < 0) { if (erro) *erro = L"receive_frame: " + std::wstring(L.Erro(r).begin(), L.Erro(r).end()); return false; }
            // o mais novo fica; se vieram dois (decoder atrasado), o velho e largado
            L.av_frame_unref(m_frameDec[slot]);
            L.av_frame_ref(m_frameDec[slot], m_frameTmp);
            L.av_frame_unref(m_frameTmp);
            got++;
        }
        if (!got) { if (erro) *erro = L"o decoder segurou o quadro (fila > 0)"; return false; }
        if (msSubmit) *msSubmit = AgoraMs() - t0;
        // as views do plano (ou a copia para a NV12 propria)
        if (!PrepararSaida(slot, erro)) return false;
        // ate a GPU terminar de verdade
        if (m_qDec) {
            m_ctxB->End(m_qDec); m_ctxB->Flush();
            const double tq = AgoraMs();
            for (;;) {
                const HRESULT hq = m_ctxB->GetData(m_qDec, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (hq == S_OK) break;
                if (FAILED(hq) || AgoraMs() - tq > 500.0) break;
                Sleep(0);
            }
        }
        if (msGpu) *msGpu = AgoraMs() - t0;
        return true;
    }
    ID3D11ShaderResourceView* SrvY(int slot)  { return m_srvY[slot]; }
    ID3D11ShaderResourceView* SrvUV(int slot) { return m_srvUV[slot]; }

    /* Escreve um quadro sintetico (NV12) no slot - so a sonda usa. */
    void EscreverSintetico(int slot, int semente) {
        ID3D11Texture2D* tex = TexEntrada(slot); if (!tex) return;
        std::vector<uint8_t> buf((size_t)m_w * m_h * 3 / 2);
        uint32_t s = 0x9E3779B9u * (uint32_t)(semente + 1);
        auto rnd = [&]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
        for (UINT y = 0; y < m_h; y++) for (UINT x = 0; x < m_w; x++) {
            // gradiente + xadrez + ruido fino: nem "limpo" (P-frames vazios) nem ruido branco (adversarial)
            const int xad = (((x + semente * 7) / 32) ^ (y / 32)) & 1;
            int v = 40 + (int)(x * 150 / m_w) + xad * 30 + (int)(rnd() % 12);
            buf[(size_t)y * m_w + x] = (uint8_t)std::min(235, std::max(16, v));
        }
        for (size_t i = (size_t)m_w * m_h; i < buf.size(); i++) buf[i] = (uint8_t)(120 + (rnd() % 16));
        m_ctxA->UpdateSubresource(tex, IndiceEntrada(slot), nullptr, buf.data(), m_w, 0);
    }

    void Parar() {
        Lav& L = Lav::I();
        m_ok = false;
        for (int i = 0; i < kMaxSlots; i++) {
            if (m_uavY[i]) { m_uavY[i]->Release(); m_uavY[i] = nullptr; }
            if (m_uavUV[i]) { m_uavUV[i]->Release(); m_uavUV[i] = nullptr; }
            if (m_srvY[i]) { m_srvY[i]->Release(); m_srvY[i] = nullptr; }
            if (m_srvUV[i]) { m_srvUV[i]->Release(); m_srvUV[i] = nullptr; }
            if (m_nv12Copia[i]) { m_nv12Copia[i]->Release(); m_nv12Copia[i] = nullptr; }
            if (L.ok) { if (m_frameEnc[i]) L.av_frame_free(&m_frameEnc[i]); if (m_frameDec[i]) L.av_frame_free(&m_frameDec[i]); }
        }
        if (L.ok) {
            if (m_frameTmp) L.av_frame_free(&m_frameTmp);
            if (m_pkt) L.av_packet_free(&m_pkt);
            if (m_dpkt) L.av_packet_free(&m_dpkt);
            if (m_enc) L.avcodec_free_context(&m_enc);
            if (m_dec) L.avcodec_free_context(&m_dec);
            if (m_frA) L.av_buffer_unref(&m_frA);
            if (m_frB) L.av_buffer_unref(&m_frB);
            if (m_hwA) L.av_buffer_unref(&m_hwA);
            if (m_hwB) L.av_buffer_unref(&m_hwB);
        }
        if (m_qDec) { m_qDec->Release(); m_qDec = nullptr; }
        if (m_ctxA) { m_ctxA->Release(); m_ctxA = nullptr; }
        if (m_ctxB) { m_ctxB->Release(); m_ctxB = nullptr; }
        if (m_devA) { m_devA->Release(); m_devA = nullptr; }
        if (m_devB) { m_devB->Release(); m_devB = nullptr; }
        m_decCopia = false; m_srvTex = nullptr;
    }
    ~CodecVideo() { Parar(); }

private:
    static UINT Vendor(ID3D11Device* d) {
        IDXGIDevice* dx = nullptr; IDXGIAdapter* ad = nullptr; UINT v = 0;
        if (SUCCEEDED(d->QueryInterface(__uuidof(IDXGIDevice), (void**)&dx)) && dx) {
            if (SUCCEEDED(dx->GetAdapter(&ad)) && ad) { DXGI_ADAPTER_DESC dd{}; ad->GetDesc(&dd); v = dd.VendorId; ad->Release(); }
            dx->Release();
        }
        return v;
    }
    static void ProtegerContexto(ID3D11DeviceContext* c) {
        ID3D11Multithread* mt = nullptr;
        if (SUCCEEDED(c->QueryInterface(__uuidof(ID3D11Multithread), (void**)&mt)) && mt) { mt->SetMultithreadProtected(TRUE); mt->Release(); }
    }
    bool CriarDeviceLav(ID3D11Device* dev, AVBufferRef** out, std::wstring* erro) {
        Lav& L = Lav::I();
        AVBufferRef* ref = L.av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!ref) { if (erro) *erro = L"codec: av_hwdevice_ctx_alloc falhou"; return false; }
        AVHWDeviceContext* hc = (AVHWDeviceContext*)ref->data;
        AVD3D11VADeviceContext* dc = (AVD3D11VADeviceContext*)hc->hwctx;
        dc->device = dev; dev->AddRef();   // o libav solta esta referencia ao morrer
        const int r = L.av_hwdevice_ctx_init(ref);
        if (r < 0) { if (erro) *erro = L"codec: av_hwdevice_ctx_init (device do app) falhou: " + std::wstring(L.Erro(r).begin(), L.Erro(r).end()); L.av_buffer_unref(&ref); return false; }
        *out = ref;
        return true;
    }
    static bool CriarUavPlano(ID3D11Device* d, ID3D11Texture2D* tex, UINT idx, DXGI_FORMAT f, ID3D11UnorderedAccessView** out, std::wstring* erro) {
        D3D11_TEXTURE2D_DESC td{}; tex->GetDesc(&td);
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud{}; ud.Format = f;
        if (td.ArraySize > 1) { ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY; ud.Texture2DArray.FirstArraySlice = idx; ud.Texture2DArray.ArraySize = 1; }
        else ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        const HRESULT hr = d->CreateUnorderedAccessView(tex, &ud, out);
        if (FAILED(hr) || !*out) { if (erro) { wchar_t b[128]; _snwprintf(b, 128, L"codec: UAV do plano %s da NV12 recusada (0x%08lX)", f == DXGI_FORMAT_R8_UNORM ? L"Y" : L"UV", (unsigned long)hr); b[127] = 0; *erro = b; } return false; }
        return true;
    }
    static bool CriarSrvPlano(ID3D11Device* d, ID3D11Texture2D* tex, UINT idx, DXGI_FORMAT f, ID3D11ShaderResourceView** out) {
        D3D11_TEXTURE2D_DESC td{}; tex->GetDesc(&td);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{}; sd.Format = f;
        if (td.ArraySize > 1) { sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY; sd.Texture2DArray.FirstArraySlice = idx; sd.Texture2DArray.ArraySize = 1; sd.Texture2DArray.MipLevels = 1; }
        else { sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1; }
        return SUCCEEDED(d->CreateShaderResourceView(tex, &sd, out)) && *out;
    }
    // get_format do decoder: escolhe D3D11 e monta o pool com SRV (ou so DECODER + copia)
    static enum AVPixelFormat EscolherFormato(AVCodecContext* c, const enum AVPixelFormat* fmts) {
        CodecVideo* self = (CodecVideo*)c->opaque;
        Lav& L = Lav::I();
        for (const enum AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; p++) {
            if (*p != AV_PIX_FMT_D3D11) continue;
            for (int tentativa = 0; tentativa < 2; tentativa++) {
                AVBufferRef* fr = nullptr;
                if (L.avcodec_get_hw_frames_parameters(c, c->hw_device_ctx, AV_PIX_FMT_D3D11, &fr) < 0 || !fr) break;
                AVHWFramesContext* fc = (AVHWFramesContext*)fr->data;
                AVD3D11VAFramesContext* d3 = (AVD3D11VAFramesContext*)fc->hwctx;
                fc->initial_pool_size += self->m_slots + 2;   // os quadros presos nos slots da ponte
                if (tentativa == 0) d3->BindFlags |= D3D11_BIND_SHADER_RESOURCE;
                if (L.av_hwframe_ctx_init(fr) < 0) { L.av_buffer_unref(&fr); continue; }
                if (c->hw_frames_ctx) L.av_buffer_unref(&c->hw_frames_ctx);
                c->hw_frames_ctx = fr;
                if (self->m_frB) L.av_buffer_unref(&self->m_frB);
                self->m_frB = L.av_buffer_ref(fr);
                self->m_decCopia = (tentativa == 1);
                return AV_PIX_FMT_D3D11;
            }
            break;
        }
        return AV_PIX_FMT_NONE;   // sem hwaccel nao interessa: melhor falhar e cair para Alto
    }
    bool PrepararSaida(int slot, std::wstring* erro) {
        AVFrame* f = m_frameDec[slot];
        ID3D11Texture2D* tex = (ID3D11Texture2D*)f->data[0];
        const UINT idx = (UINT)(intptr_t)f->data[1];
        if (!tex) { if (erro) *erro = L"quadro decodificado sem textura D3D11"; return false; }
        if (m_srvY[slot]) { m_srvY[slot]->Release(); m_srvY[slot] = nullptr; }
        if (m_srvUV[slot]) { m_srvUV[slot]->Release(); m_srvUV[slot] = nullptr; }
        if (!m_decCopia) {
            if (CriarSrvPlano(m_devB, tex, idx, DXGI_FORMAT_R8_UNORM, &m_srvY[slot]) &&
                CriarSrvPlano(m_devB, tex, idx, DXGI_FORMAT_R8G8_UNORM, &m_srvUV[slot])) return true;
            // o driver aceitou o bind mas recusou a view: cai para a copia daqui em diante
            m_decCopia = true;
        }
        if (!m_nv12Copia[slot]) {
            D3D11_TEXTURE2D_DESC td{}; tex->GetDesc(&td);
            td.ArraySize = 1; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.MiscFlags = 0; td.Usage = D3D11_USAGE_DEFAULT; td.CPUAccessFlags = 0;
            if (FAILED(m_devB->CreateTexture2D(&td, nullptr, &m_nv12Copia[slot])) || !m_nv12Copia[slot]) { if (erro) *erro = L"NV12 propria (copia do decoder) nao criada"; return false; }
        }
        m_ctxB->CopySubresourceRegion(m_nv12Copia[slot], 0, 0, 0, 0, tex, idx, nullptr);
        if (!CriarSrvPlano(m_devB, m_nv12Copia[slot], 0, DXGI_FORMAT_R8_UNORM, &m_srvY[slot]) ||
            !CriarSrvPlano(m_devB, m_nv12Copia[slot], 0, DXGI_FORMAT_R8G8_UNORM, &m_srvUV[slot])) { if (erro) *erro = L"SRV dos planos da NV12 propria recusada"; return false; }
        return true;
    }

    bool m_ok = false;
    UINT m_w = 0, m_h = 0; int m_slots = 0, m_mbps = 0;
    UINT m_vendorA = 0, m_vendorB = 0;
    std::string m_nomeEnc, m_opcoesRecusadas;
    ID3D11Device* m_devA = nullptr; ID3D11Device* m_devB = nullptr;
    ID3D11DeviceContext* m_ctxA = nullptr; ID3D11DeviceContext* m_ctxB = nullptr;
    AVBufferRef* m_hwA = nullptr; AVBufferRef* m_hwB = nullptr; AVBufferRef* m_frA = nullptr; AVBufferRef* m_frB = nullptr;
    AVCodecContext* m_enc = nullptr; AVCodecContext* m_dec = nullptr;
    AVPacket* m_pkt = nullptr; AVPacket* m_dpkt = nullptr;
    AVFrame* m_frameEnc[kMaxSlots] = {}; AVFrame* m_frameDec[kMaxSlots] = {}; AVFrame* m_frameTmp = nullptr;
    ID3D11UnorderedAccessView* m_uavY[kMaxSlots] = {}; ID3D11UnorderedAccessView* m_uavUV[kMaxSlots] = {};
    ID3D11ShaderResourceView* m_srvY[kMaxSlots] = {}; ID3D11ShaderResourceView* m_srvUV[kMaxSlots] = {};
    ID3D11Texture2D* m_nv12Copia[kMaxSlots] = {};
    ID3D11Texture2D* m_srvTex = nullptr;
    ID3D11Query* m_qDec = nullptr;
    bool m_decCopia = false, m_faixaLim = true;
};

} // namespace PonteGpu
