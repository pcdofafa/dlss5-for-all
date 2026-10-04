// =====================================================================================
//  DLSS5 For All - captura alternativa por DXGI Desktop Duplication
//
//  POR QUE ELA EXISTE, se o WGC ja funciona: porque o WGC PULA COMPOSICOES. Medido em
//  20/09/2026 com uma fonte batendo 33,33 ms cravados (30 fps de video, sem jitter
//  nenhum na origem), o carimbo dos quadros que chegam ate nos sai com
//  `chegada(carimbo) p95 45-50 ms` e o `LAT ritmo do WGC` chega a `max 108 ms`. Ou seja:
//  o compositor compos, mas o frame pool nao nos entregou naquela volta - e o tremor que
//  o apresentador ve (`j`) nao e nosso, e da porta de entrada.
//
//  A Desktop Duplication entrega por outra porta:
//    - AcquireNextFrame devolve `DXGI_OUTDUPL_FRAME_INFO` com LastPresentTime (QPC do
//      present que gerou aquela imagem) e AccumulatedFrames (quantos presents couberam
//      naquela entrega). O carimbo nao e "quando eu acordei", e QUANDO A IMAGEM EXISTIU.
//    - LastPresentTime == 0 quer dizer "nada novo na imagem, so o cursor/metadados mudou".
//      Isso e DEDUP NA ORIGEM: o quadro nem sobe pela cadeia, em vez de subir e ser
//      descartado la na frente por comparacao de conteudo.
//
//  O QUE ELA NAO FAZ: janela ocluida (ela e o MONITOR inteiro, sempre), HDR (o output em
//  HDR entrega FP16 com outra colorimetria - fora do escopo daqui, o WGC ja resolve isso),
//  monitor girado (a imagem viria transposta) e monitor noutra placa que nao a do render
//  (a textura compartilhada exige o mesmo adaptador). Nesses casos a duplicacao se declara
//  INDISPONIVEL e quem manda continua sendo o WGC.
//
//  DESENHO DAS THREADS. A duplicacao e um par Acquire/Release que tem de acontecer na
//  MESMA thread, e segurar o frame trava a fila do DXGI - entao ela nao pode rodar no laco
//  do render (que dorme esperando o evento). Mas o contexto imediato do device do render
//  tambem nao pode ser tocado por duas threads. A saida e a de sempre nessa situacao:
//
//     thread da duplicacao            |   thread do render (o laco de sempre)
//     device PROPRIO (mesmo adaptador)|   device do app
//     Acquire -> copia p/ textura     |   AcquireSync(1) -> recorta p/ `tex` -> Release(0)
//     compartilhada -> Release        |
//     SetEvent(evt do ScreenCapture)  |
//
//  A textura compartilhada tem keyed mutex. O produtor tenta a chave 0 (o consumidor leu)
//  e, se falhar, tenta a 1 (o consumidor AINDA NAO leu) - nesse segundo caso ele sobrescreve
//  o quadro nao lido de proposito: o que vale e sempre o mais novo, exatamente como o
//  `optDrain` faz do lado do WGC.
//
//  Depende de Log(const char*, ...), declarada em screen_capture.h antes do include.
// =====================================================================================
#pragma once

#include "saida_rapida.h"   // Saida::Saindo: no fechamento a thread da duplicacao e abandonada em 200 ms
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

// ------------------------------------------------------------------ relogio
// Mesma base do resto da captura: QPC. O carimbo do quadro sai daqui em 100ns para
// casar com o SystemRelativeTime do WGC, que e o que o apresentador ja le.
static LONGLONG DxgiQpcFreq() {
    static LONGLONG f = [] { LARGE_INTEGER q{}; QueryPerformanceFrequency(&q); return q.QuadPart ? q.QuadPart : 1; }();
    return f;
}
static LONGLONG DxgiQpcTo100ns(LONGLONG qpc) {
    const LONGLONG f = DxgiQpcFreq();
    // (qpc / f) * 1e7, em duas contas para nao estourar 64 bits com qpc grande
    return (qpc / f) * 10000000LL + ((qpc % f) * 10000000LL) / f;
}

struct CapturaDxgi {
    // ---- lado produtor (thread da duplicacao)
    ID3D11Device*           devDup = nullptr;
    ID3D11DeviceContext*    ctxDup = nullptr;
    IDXGIOutputDuplication* dup    = nullptr;
    IDXGIOutput1*           out1   = nullptr;
    ID3D11Texture2D*        texProd = nullptr;
    IDXGIKeyedMutex*        kmProd  = nullptr;
    HANDLE                  hShared = nullptr;

    // ---- lado consumidor (thread do render)
    ID3D11Device*      devCons = nullptr;   // nao possuido: e o device do app
    ID3D11Texture2D*   texCons = nullptr;
    IDXGIKeyedMutex*   kmCons  = nullptr;

    // ---- alvo
    HMONITOR  mon = nullptr;
    RECT      monRect{};                 // o retangulo do monitor em coordenadas de tela
    UINT      texW = 0, texH = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_B8G8R8A8_UNORM;

    // ---- estado corrente
    std::thread            th;
    std::atomic<bool>      parar{ false };
    std::atomic<bool>      vivo{ false };
    std::atomic<UINT32>    seq{ 0 };          // sobe a cada quadro NOVO publicado
    UINT32                 seqVisto = 0;      // ultimo lido pelo consumidor
    std::atomic<LONGLONG>  ultPresentQpc{ 0 };
    std::atomic<LONGLONG>  publicadoQpc{ 0 };   // QPC de quando NOS publicamos (o "acordei")
    std::atomic<UINT32>    acumulados{ 0 };   // AccumulatedFrames do ultimo quadro
    std::atomic<UINT32>    pulados{ 0 };      // entregas sem present novo (dedup na origem)
    std::atomic<UINT32>    perdidos{ 0 };     // quadros sobrescritos antes de o render ler
    std::atomic<UINT32>    lidos{ 0 };        // quantos o render de fato travou e leu (diagnostico)
    std::atomic<UINT32>    semConteudo{ 0 };  // composicoes em que o RECORTE nao mudou um pixel

    // =================================================================================
    //  DEDUP POR CONTEUDO (O3, 20/09/2026 - achado do o1 na bancada)
    //
    //  `LastPresentTime` NAO e o present da fonte: e a hora da COMPOSICAO do DWM em que o
    //  desktop mudou. E quem forca composicao a 120/s nesta maquina somos NOS - a pelicula
    //  apresenta 2x por chegada. Ela esta fora da captura (EXCLUDEFROMCAPTURE), mas o DWM
    //  compoe do mesmo jeito, e a duplicacao entrega cada composicao com carimbo novo. Com
    //  a fonte a 30 fps, 3 de cada 4 entregas tinham o recorte IDENTICO ao anterior - e o
    //  dedup por carimbo deixava todas subir (o o1 mediu 120 "distintos"/s seja qual for o
    //  --fps do alvo; o carimbo so pegava 5/s).
    //
    //  ENTAO A PERGUNTA CERTA E "o RECORTE mudou?", e ela e respondida na GPU da propria
    //  duplicacao, sem shader nenhum: o recorte e copiado para uma textura com mips,
    //  GenerateMips reduz por media de caixa ate um nivel de ~80 px de largura, esse nivel
    //  (uns 14 KB) desce para a CPU e e comparado byte a byte com o do quadro anterior.
    //  Imagem digital parada da bytes iguais; um quadrado de 40 px andando 1 px muda a
    //  media de algum bloco. O Map bloqueia esta thread ate a GPU terminar - e trabalho de
    //  microssegundos, e a thread e nossa.
    //
    //  O recorte chega do render por quatro atomicos (o render e quem sabe onde a janela
    //  esta); sem recorte definido, compara o monitor inteiro.
    // =================================================================================
    std::atomic<LONG> recL{ 0 }, recT{ 0 }, recR{ 0 }, recB{ 0 };
    void DefinirRecorte(const RECT& r) {   // em coordenadas de TELA (como o `want` da captura)
        recL.store(r.left, std::memory_order_relaxed);  recT.store(r.top, std::memory_order_relaxed);
        recR.store(r.right, std::memory_order_relaxed); recB.store(r.bottom, std::memory_order_relaxed);
    }
    ID3D11Texture2D*          texMip = nullptr;
    ID3D11ShaderResourceView* srvMip = nullptr;
    ID3D11Texture2D*          texAss = nullptr;   // staging do nivel de assinatura
    UINT mipW = 0, mipH = 0, mipNivel = 0, assW = 0, assH = 0;
    std::vector<unsigned char> assAnterior;
    bool assValida = false;

    void SoltarAssinatura() {
        if (texAss) { texAss->Release(); texAss = nullptr; }
        if (srvMip) { srvMip->Release(); srvMip = nullptr; }
        if (texMip) { texMip->Release(); texMip = nullptr; }
        mipW = mipH = mipNivel = assW = assH = 0;
        assValida = false;
    }

    bool GarantirAssinatura(UINT w, UINT h) {
        if (texMip && texAss && w == mipW && h == mipH) return true;
        SoltarAssinatura();
        UINT niveis = 1; for (UINT s = (w > h ? w : h); s > 1; s >>= 1) niveis++;
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = niveis; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(devDup->CreateTexture2D(&td, nullptr, &texMip)) || !texMip) return false;
        if (FAILED(devDup->CreateShaderResourceView(texMip, nullptr, &srvMip)) || !srvMip) { SoltarAssinatura(); return false; }
        // o menor nivel que ainda tem ~64 px de largura: passo de ~20 px numa fonte de 1280
        mipNivel = 0; UINT aw = w, ah = h;
        while (mipNivel + 1 < niveis && (aw >> 1) >= 64) { mipNivel++; aw >>= 1; ah >>= 1; }
        if (!ah) ah = 1;
        D3D11_TEXTURE2D_DESC sd{};
        sd.Width = aw; sd.Height = ah; sd.MipLevels = 1; sd.ArraySize = 1;
        sd.Format = fmt; sd.SampleDesc.Count = 1; sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(devDup->CreateTexture2D(&sd, nullptr, &texAss)) || !texAss) { SoltarAssinatura(); return false; }
        mipW = w; mipH = h; assW = aw; assH = ah;
        assAnterior.assign((size_t)aw * (size_t)ah * 4, 0);
        assValida = false;
        return true;
    }

    // DIAGNOSTICO (DLSS5_CAPTURA_DIAG=1): grava o recorte de alguns quadros publicados em
    // BMP, para VER o que a duplicacao esta entregando - foi assim que se descobriu que
    // a nossa propria saida entrava na captura.
    unsigned diagDespejos = 0;
    bool descLogado = false;
    void DespejarRecorte(ID3D11Texture2D* src, unsigned n) {
        LONG l = recL.load() - monRect.left, t = recT.load() - monRect.top;
        LONG r = recR.load() - monRect.left, b = recB.load() - monRect.top;
        if (r - l < 16 || b - t < 16) { l = 0; t = 0; r = (LONG)texW; b = (LONG)texH; }
        if (l < 0) l = 0;
        if (t < 0) t = 0;
        if (r > (LONG)texW) r = (LONG)texW;
        if (b > (LONG)texH) b = (LONG)texH;
        const UINT w = (UINT)(r - l), h = (UINT)(b - t);
        D3D11_TEXTURE2D_DESC sd{};
        sd.Width = w; sd.Height = h; sd.MipLevels = 1; sd.ArraySize = 1;
        sd.Format = fmt; sd.SampleDesc.Count = 1; sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* st = nullptr;
        if (FAILED(devDup->CreateTexture2D(&sd, nullptr, &st)) || !st) return;
        D3D11_BOX box{ (UINT)l, (UINT)t, 0, (UINT)r, (UINT)b, 1 };
        ctxDup->CopySubresourceRegion(st, 0, 0, 0, 0, src, 0, &box);
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (SUCCEEDED(ctxDup->Map(st, 0, D3D11_MAP_READ, 0, &ms))) {
            char nome[64]; snprintf(nome, sizeof nome, "dxgi-recorte-%u.bmp", n);
            FILE* f = fopen(nome, "wb");
            if (f) {
                BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{};
                ih.biSize = sizeof(ih); ih.biWidth = (LONG)w; ih.biHeight = -(LONG)h;
                ih.biPlanes = 1; ih.biBitCount = 32; ih.biCompression = BI_RGB;
                fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih);
                fh.bfSize = fh.bfOffBits + w * h * 4;
                fwrite(&fh, sizeof fh, 1, f); fwrite(&ih, sizeof ih, 1, f);
                for (UINT y = 0; y < h; y++) fwrite((const char*)ms.pData + (size_t)y * ms.RowPitch, 4, w, f);
                fclose(f);
            }
            ctxDup->Unmap(st, 0);
        }
        st->Release();
    }

    // true = o recorte mudou desde a ultima vez (ou nao ha como saber: ai sobe, na duvida).
    bool ConteudoNovo(ID3D11Texture2D* src) {
        LONG l = recL.load(std::memory_order_relaxed) - monRect.left, t = recT.load(std::memory_order_relaxed) - monRect.top;
        LONG r = recR.load(std::memory_order_relaxed) - monRect.left, b = recB.load(std::memory_order_relaxed) - monRect.top;
        if (r - l < 16 || b - t < 16) { l = 0; t = 0; r = (LONG)texW; b = (LONG)texH; }   // sem recorte: o monitor
        if (l < 0) l = 0;
        if (t < 0) t = 0;
        if (r > (LONG)texW) r = (LONG)texW;
        if (b > (LONG)texH) b = (LONG)texH;
        if (r - l < 16 || b - t < 16) return true;
        if (!GarantirAssinatura((UINT)(r - l), (UINT)(b - t))) return true;
        D3D11_BOX box{ (UINT)l, (UINT)t, 0, (UINT)r, (UINT)b, 1 };
        ctxDup->CopySubresourceRegion(texMip, 0, 0, 0, 0, src, 0, &box);
        ctxDup->GenerateMips(srvMip);
        ctxDup->CopySubresourceRegion(texAss, 0, 0, 0, 0, texMip, mipNivel, nullptr);
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (FAILED(ctxDup->Map(texAss, 0, D3D11_MAP_READ, 0, &ms))) return true;
        bool mudou = !assValida;
        const size_t linha = (size_t)assW * 4;
        for (UINT y = 0; y < assH; y++) {
            const unsigned char* p = (const unsigned char*)ms.pData + (size_t)y * ms.RowPitch;
            unsigned char* q = assAnterior.data() + (size_t)y * linha;
            if (!mudou && memcmp(p, q, linha) != 0) mudou = true;
            if (mudou) memcpy(q, p, linha);
        }
        ctxDup->Unmap(texAss, 0);
        assValida = true;
        return mudou;
    }
    HANDLE                 evt = nullptr;     // o MESMO evento do ScreenCapture (nao e nosso)
    std::wstring           motivo;            // por que esta indisponivel, para o log

    // Chaves de mutex. 0 = livre para o produtor escrever; 1 = tem quadro esperando leitura.
    enum { KEY_LIVRE = 0, KEY_CHEIO = 1 };

    // ---------------------------------------------------------------- disponibilidade
    // Responde "da para usar a duplicacao neste alvo AGORA?" sem montar nada. As tres
    // recusas (outra placa, HDR, girado) tem de ser respondidas ANTES de trocar de backend:
    // trocar e descobrir que nao da seria um piscar na tela da pessoa.
    static bool Disponivel(ID3D11Device* devRender, HMONITOR m, bool monitorEmHdr, std::wstring* porque) {
        if (porque) porque->clear();
        if (!devRender || !m) { if (porque) *porque = L"sem device ou sem monitor"; return false; }
        if (monitorEmHdr)     { if (porque) *porque = L"monitor em HDR"; return false; }

        IDXGIDevice* dxdev = nullptr;
        if (FAILED(devRender->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev)) || !dxdev) {
            if (porque) *porque = L"device sem IDXGIDevice";
            return false;
        }
        IDXGIAdapter* ad = nullptr;
        HRESULT hr = dxdev->GetAdapter(&ad);
        dxdev->Release();
        if (FAILED(hr) || !ad) { if (porque) *porque = L"device sem adaptador"; return false; }

        bool achou = false;
        for (UINT i = 0; ; i++) {
            IDXGIOutput* o = nullptr;
            if (ad->EnumOutputs(i, &o) == DXGI_ERROR_NOT_FOUND || !o) break;
            DXGI_OUTPUT_DESC d{};
            if (SUCCEEDED(o->GetDesc(&d)) && d.Monitor == m) {
                achou = true;
                if (d.Rotation != DXGI_MODE_ROTATION_IDENTITY && d.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
                    if (porque) *porque = L"monitor girado";
                    achou = false;
                }
                o->Release();
                break;
            }
            o->Release();
        }
        ad->Release();
        // "O MONITOR NAO E DESTA PLACA" e o caso do notebook hibrido: a tela pendurada na
        // iGPU com o jogo na dedicada. A duplicacao so existe no adaptador dono da saida, e
        // a textura compartilhada de keyed mutex nao atravessa adaptador.
        if (!achou && porque && porque->empty()) *porque = L"o monitor nao esta na placa do render";
        return achou;
    }

    // ---------------------------------------------------------------- montagem
    bool Iniciar(ID3D11Device* devRender, HMONITOR m, HANDLE eventoAviso, std::wstring* err) {
        Parar();
        devCons = devRender; mon = m; evt = eventoAviso;
        motivo.clear();
        if (!devCons || !mon) { if (err) *err = L"sem device ou monitor"; return false; }

        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(mon, &mi)) { if (err) *err = L"GetMonitorInfo falhou"; return false; }
        monRect = mi.rcMonitor;

        // ---- adaptador do render (a duplicacao TEM de nascer nele, senao a textura
        //      compartilhada nao abre no device do app)
        IDXGIDevice* dxdev = nullptr;
        if (FAILED(devCons->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxdev)) || !dxdev) {
            if (err) *err = L"device sem IDXGIDevice";
            return false;
        }
        IDXGIAdapter* ad = nullptr;
        HRESULT hr = dxdev->GetAdapter(&ad);
        dxdev->Release();
        if (FAILED(hr) || !ad) { if (err) *err = L"device sem adaptador"; return false; }

        for (UINT i = 0; !out1; i++) {
            IDXGIOutput* o = nullptr;
            if (ad->EnumOutputs(i, &o) == DXGI_ERROR_NOT_FOUND || !o) break;
            DXGI_OUTPUT_DESC d{};
            if (SUCCEEDED(o->GetDesc(&d)) && d.Monitor == mon)
                o->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1);
            o->Release();
        }
        if (!out1) { ad->Release(); if (err) *err = L"o monitor nao esta nesta placa"; return false; }

        // Device proprio: a thread da duplicacao precisa de um contexto imediato so dela.
        // BGRA_SUPPORT porque o desktop chega em BGRA8; sem DEBUG, sem SINGLETHREADED.
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL fl{};
        hr = D3D11CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, nullptr, 0,
                               D3D11_SDK_VERSION, &devDup, &fl, &ctxDup);
        ad->Release();
        if (FAILED(hr) || !devDup) { if (err) *err = L"D3D11CreateDevice do lado da duplicacao falhou"; Desmontar(); return false; }

        hr = out1->DuplicateOutput(devDup, &dup);
        if (FAILED(hr) || !dup) {
            // E_ACCESSDENIED aqui quer dizer "ja existe um duplicador neste output" (outro
            // gravador de tela aberto) ou "o modo exclusivo esta com a saida". Nao e um
            // defeito nosso: e motivo para ficar no WGC e dizer por que.
            if (err) *err = (hr == E_ACCESSDENIED)
                ? L"outra aplicacao ja esta duplicando este monitor (ou ha tela cheia exclusiva)"
                : L"DuplicateOutput falhou";
            Desmontar(); return false;
        }

        DXGI_OUTDUPL_DESC dd{};
        dup->GetDesc(&dd);
        if (dd.Rotation != DXGI_MODE_ROTATION_IDENTITY && dd.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
            if (err) *err = L"monitor girado";
            Desmontar();
            return false;
        }
        texW = dd.ModeDesc.Width; texH = dd.ModeDesc.Height;
        fmt  = dd.ModeDesc.Format ? dd.ModeDesc.Format : DXGI_FORMAT_B8G8R8A8_UNORM;
        if (!texW || !texH) { if (err) *err = L"tamanho do output invalido"; Desmontar(); return false; }

        if (!CriarCompartilhada()) { if (err) *err = L"textura compartilhada falhou"; Desmontar(); return false; }

        parar.store(false);
        vivo.store(true);
        seq.store(0); seqVisto = 0;
        ultPresentQpc.store(0); acumulados.store(0); pulados.store(0); perdidos.store(0);
        th = std::thread([this] { Laco(); });
        Log("captura DXGI: duplicacao no ar - output %ux%u fmt %u, monitor %ldx%ld em %ld,%ld (%s)",
            texW, texH, (unsigned)fmt,
            (long)(monRect.right - monRect.left), (long)(monRect.bottom - monRect.top),
            (long)monRect.left, (long)monRect.top,
            dd.DesktopImageInSystemMemory ? "imagem na memoria do sistema" : "imagem na GPU");
        return true;
    }

    void Parar() {
        if (th.joinable()) {
            parar.store(true);
            // SAIDA RAPIDA: fechando, a thread tem 200 ms. Presa no AcquireNextFrame de uma
            // placa em TDR ela nao volta, e esperar aqui e tempo de GPU segurada - foi assim
            // que o COD perdeu o device. Abandonada, NADA e desmontado embaixo dela: o SO
            // recolhe as texturas e a duplicacao quando o processo sair.
            if (Saida::Saindo() &&
                WaitForSingleObject((HANDLE)th.native_handle(), Saida::kTetoMs) != WAIT_OBJECT_0) {
                th.detach(); vivo.store(false);
                Saida::Abandonei();   // ninguem mais solta nada: ela continua escrevendo na textura
                Log("captura DXGI: a thread nao saiu em %u ms no fechamento - abandonada (o SO recolhe)", (unsigned)Saida::kTetoMs);
                return;
            }
            th.join();
        }
        vivo.store(false);
        Desmontar();
    }

    ~CapturaDxgi() { Parar(); }

    bool Vivo() const { return vivo.load(std::memory_order_relaxed); }

    // Carimbo do ultimo quadro publicado, em 100ns na base do QPC - a MESMA unidade do
    // SystemRelativeTime do WGC, para o apresentador nao ter de saber quem capturou.
    LONGLONG Carimbo100ns() const {
        const LONGLONG q = ultPresentQpc.load(std::memory_order_relaxed);
        return q ? DxgiQpcTo100ns(q) : 0;
    }

    // Tem quadro novo esperando? (barato: so compara o contador)
    bool TemNovo() const { return seq.load(std::memory_order_acquire) != seqVisto; }

    // ---------------------------------------------------------------- consumo
    // Roda na thread do render. Devolve a textura travada para quem vai copiar dela; o
    // chamador TEM de chamar Destravar() logo em seguida (o par e obrigatorio).
    ID3D11Texture2D* Travar() {
        if (!kmCons || !texCons) return nullptr;
        const UINT32 s = seq.load(std::memory_order_acquire);
        if (s == seqVisto) return nullptr;
        if (kmCons->AcquireSync(KEY_CHEIO, 0) != S_OK) return nullptr;   // o produtor esta escrevendo
        seqVisto = s;
        lidos.fetch_add(1, std::memory_order_relaxed);
        return texCons;
    }
    void Destravar() { if (kmCons) kmCons->ReleaseSync(KEY_LIVRE); }

private:
    bool CriarCompartilhada() {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = texW; td.Height = texH; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = fmt; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        if (FAILED(devDup->CreateTexture2D(&td, nullptr, &texProd)) || !texProd) return false;
        if (FAILED(texProd->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&kmProd)) || !kmProd) return false;

        IDXGIResource1* r1 = nullptr;
        if (FAILED(texProd->QueryInterface(__uuidof(IDXGIResource1), (void**)&r1)) || !r1) return false;
        HRESULT hr = r1->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                            nullptr, &hShared);
        r1->Release();
        if (FAILED(hr) || !hShared) return false;

        ID3D11Device1* d1 = nullptr;
        if (FAILED(devCons->QueryInterface(__uuidof(ID3D11Device1), (void**)&d1)) || !d1) return false;
        hr = d1->OpenSharedResource1(hShared, __uuidof(ID3D11Texture2D), (void**)&texCons);
        d1->Release();
        if (FAILED(hr) || !texCons) return false;
        if (FAILED(texCons->QueryInterface(__uuidof(IDXGIKeyedMutex), (void**)&kmCons)) || !kmCons) return false;
        return true;
    }

    void Desmontar() {
        SoltarAssinatura();
        if (kmCons)  { kmCons->Release();  kmCons = nullptr; }
        if (texCons) { texCons->Release(); texCons = nullptr; }
        if (hShared) { CloseHandle(hShared); hShared = nullptr; }
        if (kmProd)  { kmProd->Release();  kmProd = nullptr; }
        if (texProd) { texProd->Release(); texProd = nullptr; }
        if (dup)     { dup->Release();     dup = nullptr; }
        if (out1)    { out1->Release();    out1 = nullptr; }
        if (ctxDup)  { ctxDup->Release();  ctxDup = nullptr; }
        if (devDup)  { devDup->Release();  devDup = nullptr; }
        texW = texH = 0;
    }

    // Recria a duplicacao depois de um ACCESS_LOST (troca de modo, UAC, jogo entrando em
    // tela cheia exclusiva). Sem isto, um Alt+Tab mataria a captura para sempre.
    bool Recriar() {
        if (dup) { dup->Release(); dup = nullptr; }
        if (!out1 || !devDup) return false;
        return SUCCEEDED(out1->DuplicateOutput(devDup, &dup)) && dup != nullptr;
    }

    void Laco() {
        LONGLONG ultPresent = 0;
        int seguidasSemQuadro = 0;
        // DIAGNOSTICO 1x/s. Sem isto, "a duplicacao subiu e nao chegou quadro nenhum" nao
        // tem onde ser investigado: tudo o que interessa acontece nesta thread.
        const bool diag = [] { char b[8]; return GetEnvironmentVariableA("DLSS5_CAPTURA_DIAG", b, 8) && b[0] == '1'; }();
        ULONGLONG ultRelato = GetTickCount64();
        unsigned pub = 0, semPresent = 0, timeouts = 0, semChave = 0, falhas = 0;
        HRESULT ultHr = S_OK;
        while (!parar.load(std::memory_order_relaxed)) {
            {
                const ULONGLONG ag = GetTickCount64();
                if (diag && ag - ultRelato >= 1000) {
                    ultRelato = ag;
                    Log("captura DXGI: publicados %u/s, sem present novo %u, recorte igual %u, espera vazia %u, sem chave %u, "
                        "falhas %u (ultimo hr 0x%08lX), lidos pelo render %u",
                        pub, semPresent, (unsigned)semConteudo.load(std::memory_order_relaxed), timeouts, semChave, falhas, (unsigned long)ultHr,
                        (unsigned)lidos.load(std::memory_order_relaxed));
                    semConteudo.store(0, std::memory_order_relaxed);
                    pub = semPresent = timeouts = semChave = falhas = 0;
                    lidos.store(0, std::memory_order_relaxed);
                }
            }
            if (!dup) {
                Sleep(200);
                if (!Recriar()) continue;
                Log("captura DXGI: duplicacao recriada depois de perder o acesso");
            }
            DXGI_OUTDUPL_FRAME_INFO info{};
            IDXGIResource* res = nullptr;
            // 16 ms de espera: acorda a tempo de um vblank de 60 Hz e devolve o controle
            // para checar `parar` sem nunca deixar a thread presa num Wait infinito.
            HRESULT hr = dup->AcquireNextFrame(16, &info, &res);
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) { timeouts++; if (++seguidasSemQuadro > 1000) seguidasSemQuadro = 1000; continue; }
            if (hr == DXGI_ERROR_ACCESS_LOST) {
                if (res) res->Release();
                dup->Release(); dup = nullptr;
                continue;
            }
            if (FAILED(hr) || !res) { falhas++; ultHr = hr; if (res) res->Release(); Sleep(2); continue; }
            seguidasSemQuadro = 0;

            // DEDUP NA ORIGEM. LastPresentTime zero = a imagem do desktop nao mudou (foi so
            // o cursor, ou metadados de regiao suja vazios). Igual ao carimbo anterior =
            // mesma composicao entregue de novo. Nos dois casos o quadro NAO sobe: a cadeia
            // nao gasta banda de GPU para descobrir, la na frente, que era repetido.
            const LONGLONG pres = info.LastPresentTime.QuadPart;
            if (!pres || pres == ultPresent) {
                semPresent++;
                pulados.fetch_add(1, std::memory_order_relaxed);
                dup->ReleaseFrame();
                res->Release();
                continue;
            }

            ID3D11Texture2D* src = nullptr;
            res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&src);
            if (diag && src && !descLogado) {
                descLogado = true;
                D3D11_TEXTURE2D_DESC sd{}; src->GetDesc(&sd);
                Log("captura DXGI: textura do desktop %ux%u fmt %u mips %u usage %u bind 0x%X cpu 0x%X misc 0x%X | "
                    "AccumulatedFrames %u, dirty %u bytes, move %u bytes, ponteiro visivel %d",
                    sd.Width, sd.Height, (unsigned)sd.Format, sd.MipLevels, (unsigned)sd.Usage, (unsigned)sd.BindFlags,
                    (unsigned)sd.CPUAccessFlags, (unsigned)sd.MiscFlags, (unsigned)info.AccumulatedFrames,
                    (unsigned)info.TotalMetadataBufferSize, 0u, (int)info.PointerPosition.Visible);
            }
            // (A textura do desktop vem com SHARED_KEYEDMUTEX no misc, mas o AcquireSync nela
            // falha e NAO e preciso: o runtime ja a segura entre Acquire e ReleaseFrame, e a
            // copia direta funciona - conferido com o despejo de recortes do DIAG.)
            // O RECORTE MUDOU? Composicao com o mesmo recorte de antes (a nossa pelicula, ou
            // outra janela, e que mexeu) nao sobe - ver o bloco do dedup por conteudo.
            if (src && !ConteudoNovo(src)) {
                semConteudo.fetch_add(1, std::memory_order_relaxed);
                pulados.fetch_add(1, std::memory_order_relaxed);
                src->Release();
                dup->ReleaseFrame();
                res->Release();
                continue;
            }
            bool publicou = false;
            if (src && kmProd && texProd) {
                // Chave 0 = o render ja leu o anterior. Chave 1 = ele ainda NAO leu, e o
                // quadro que esta la vai ser sobrescrito de proposito (o que vale e o mais
                // novo). So contamos o sobrescrito para o log saber o tamanho do desperdicio.
                HRESULT hk = kmProd->AcquireSync(KEY_LIVRE, 4);
                if (hk != S_OK) {
                    hk = kmProd->AcquireSync(KEY_CHEIO, 4);
                    if (hk == S_OK) perdidos.fetch_add(1, std::memory_order_relaxed);
                }
                if (hk != S_OK) { semChave++; ultHr = hk; }
                if (hk == S_OK) {
                    ctxDup->CopyResource(texProd, src);
                    ctxDup->Flush();   // o outro device so enxerga o que ja foi submetido
                    kmProd->ReleaseSync(KEY_CHEIO);
                    publicou = true;
                }
            }
            if (publicou && diag && src) {
                // 4 recortes: o 30o, 200o, 201o e 202o quadro publicado (tres seguidos = da para
                // ver o que muda entre composicoes consecutivas). AINDA com o frame na mao.
                const UINT32 n = seq.load(std::memory_order_relaxed) + 1;
                if (n == 30 || (n >= 200 && n <= 202)) DespejarRecorte(src, n);
            }
            if (src) src->Release();
            dup->ReleaseFrame();
            res->Release();

            if (publicou) {
                pub++;
                ultPresent = pres;
                LARGE_INTEGER agoraQpc{}; QueryPerformanceCounter(&agoraQpc);
                publicadoQpc.store(agoraQpc.QuadPart, std::memory_order_relaxed);
                ultPresentQpc.store(pres, std::memory_order_relaxed);
                acumulados.store(info.AccumulatedFrames, std::memory_order_relaxed);
                seq.fetch_add(1, std::memory_order_release);
                if (evt) SetEvent(evt);   // o laco do render dorme neste handle
            }
        }
    }
};
