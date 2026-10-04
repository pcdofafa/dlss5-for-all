// audio_captura.h - O SOM DO CONSOLE (J8, 18/09/2026).
//
// Quem joga PlayStation/Xbox pela placa de captura via a imagem e nao ouvia nada: precisava
// de outro programa so para o som. Este modulo acha a ENTRADA de audio WASAPI que pertence
// a placa (casa o nome com o do dispositivo de video) e a toca na SAIDA padrao do Windows,
// em modo compartilhado e por evento, com o buffer menor que o Windows aceita.
//
// Regras que valem aqui e em nenhum outro lugar:
//  - NADA disto pode derrubar o video. Toda falha vira uma linha de log, um cartaz unico
//    (PegarAviso) e o app segue mudo. Nenhuma excecao sai daqui, nenhum Fatal.
//  - A thread de audio e dona dos objetos COM. O laco principal so le atomics.
//  - Latencia propria pequena: o alvo e ~20 ms de fila na saida (kAlvoPadraoMs) e mais o
//    periodo do engine (10 ms). Se a fila cresce (os relogios da placa e da saida nao sao o
//    mesmo cristal), o excesso e descartado; se esvazia, recarrega com silencio ate o alvo.
//    Os dois eventos ficam contados e vao para o log a cada minuto.
//
// Usa so o que o main.cpp ja tem antes deste include: Log(), U8(), T().
#pragma once
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <propsys.h>
#include <propidl.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cmath>
#include <cwctype>

namespace AudioCap {

// GUIDs escritos aqui de proposito: nao dependem do libuuid do MinGW nem de INITGUID.
static const CLSID kClsidEnum   = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const IID   kIidEnum     = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const IID   kIidClient   = { 0x1CB9AD4C, 0xDBFA, 0x4C32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
static const IID   kIidCapture  = { 0xC8ADBD64, 0xE71E, 0x48A0, { 0xA4, 0xDE, 0x18, 0x5C, 0x39, 0x5C, 0xD3, 0x17 } };
static const IID   kIidRender   = { 0xF294ACFC, 0x3146, 0x4483, { 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2 } };
static const GUID  kSubFloat    = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID  kSubPcm      = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const PROPERTYKEY kPkeyNome = { { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } }, 14 };

enum { kAlvoPadraoMs = 20, kAlvoMinMs = 10, kAlvoMaxMs = 200 };
// O audioclient.h do MinGW nao traz as duas flags de conversao automatica (Win 8.1+).
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000u
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000u
#endif

// ------------------------------------------------------------------ estado compartilhado
static std::thread        g_th;
static std::atomic<bool>  g_rodando{ false };
static std::atomic<bool>  g_ligado{ true };       // a chave do painel ("Ouvir o som do console")
static std::atomic<bool>  g_disponivel{ false };  // achou entrada: o controle aparece
static std::atomic<bool>  g_tocando{ false };     // os dois lados abertos e correndo
static std::atomic<bool>  g_videoVivo{ true };    // o laco diz se ha quadro novo chegando
static std::atomic<int>   g_latenciaMs{ 0 };      // estimativa corrente (fila + periodo)
static std::atomic<int>   g_alvoMs{ kAlvoPadraoMs };
static HANDLE             g_evtAcorda = nullptr;  // liga/desliga/parar: acorda a espera
static std::mutex         g_mtxTexto;
static std::wstring       g_nomeVideo, g_nomePreferido, g_nomeEntrada, g_nomeSaida;
static std::wstring       g_aviso;                // cartaz pendente (uma vez so)
static bool               g_avisou = false;

template <class I> static void Solta(I*& p) { if (p) { p->Release(); p = nullptr; } }

// ------------------------------------------------------------------ nomes
static std::wstring Normaliza(const std::wstring& s) {
    std::wstring o; o.reserve(s.size());
    for (wchar_t c : s) {
        if (iswalnum(c)) o += (wchar_t)towlower(c);
        else if (!o.empty() && o.back() != L' ') o += L' ';
    }
    while (!o.empty() && o.back() == L' ') o.pop_back();
    return o;
}
static std::wstring SemEspaco(const std::wstring& s) {
    std::wstring o; for (wchar_t c : s) if (c != L' ') o += c; return o;
}
// Palavras que aparecem no nome de quase tudo e nao dizem QUAL placa e.
static bool TokenGenerico(const std::wstring& t) {
    static const wchar_t* g[] = { L"usb", L"video", L"audio", L"capture", L"captura", L"device",
                                  L"digital", L"interface", L"hd", L"camera", L"webcam", L"in",
                                  L"input", L"line", L"the", L"de", L"do", L"da", L"e", L"and" };
    if (t.size() < 2) return true;
    for (const wchar_t* w : g) if (t == w) return true;
    return false;
}
// 0..100: quanto o nome do endpoint de audio parece com o do dispositivo de video.
static int Parecido(const std::wstring& video, const std::wstring& endpoint) {
    const std::wstring v = Normaliza(video), e = Normaliza(endpoint);
    if (v.empty() || e.empty()) return 0;
    const std::wstring vs = SemEspaco(v), es = SemEspaco(e);
    if (es.find(vs) != std::wstring::npos || vs.find(es) != std::wstring::npos) return 100;
    int total = 0, achou = 0; size_t i = 0;
    while (i < v.size()) {
        size_t j = v.find(L' ', i); if (j == std::wstring::npos) j = v.size();
        const std::wstring tok = v.substr(i, j - i); i = j + 1;
        if (TokenGenerico(tok)) continue;
        total++;
        if (es.find(tok) != std::wstring::npos) achou++;
    }
    return total ? (achou * 100) / total : 0;
}
static bool ContemCI(const std::wstring& s, const std::wstring& parte) {
    if (parte.empty()) return false;
    return SemEspaco(Normaliza(s)).find(SemEspaco(Normaliza(parte))) != std::wstring::npos;
}
static bool PareceMicrofone(const std::wstring& nome) {
    const std::wstring n = Normaliza(nome);
    static const wchar_t* m[] = { L"mic", L"microfone", L"microphone", L"headset", L"fone", L"webcam", L"camera" };
    for (const wchar_t* w : m) if (n.find(w) != std::wstring::npos) return true;
    return false;
}

static std::wstring NomeDoEndpoint(IMMDevice* d) {
    std::wstring nome;
    IPropertyStore* ps = nullptr;
    if (d && SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)) && ps) {
        PROPVARIANT pv; PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(kPkeyNome, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) nome = pv.pwszVal;
        PropVariantClear(&pv);
        ps->Release();
    }
    return nome;
}
static std::wstring IdDoEndpoint(IMMDevice* d) {
    std::wstring id; LPWSTR s = nullptr;
    if (d && SUCCEEDED(d->GetId(&s)) && s) { id = s; CoTaskMemFree(s); }
    return id;
}

// ------------------------------------------------------------------ formato
struct Fmt {
    int  taxa = 0, canais = 0, bits = 0, bloco = 0;
    bool flutuante = false, ok = false;
};
static Fmt LerFmt(const WAVEFORMATEX* w) {
    Fmt f; if (!w) return f;
    f.taxa = (int)w->nSamplesPerSec; f.canais = w->nChannels; f.bits = w->wBitsPerSample; f.bloco = w->nBlockAlign;
    if (w->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) f.flutuante = true;
    else if (w->wFormatTag == WAVE_FORMAT_EXTENSIBLE && w->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE* x = (const WAVEFORMATEXTENSIBLE*)w;
        if (x->SubFormat == kSubFloat) f.flutuante = true;
        else if (x->SubFormat != kSubPcm) return f;
    } else if (w->wFormatTag != WAVE_FORMAT_PCM) return f;
    if (f.flutuante && f.bits != 32) return f;
    if (!f.flutuante && f.bits != 16 && f.bits != 24 && f.bits != 32) return f;
    f.ok = f.taxa > 0 && f.canais > 0 && f.bloco == f.canais * f.bits / 8;
    return f;
}
// Entrada -> float intercalado, do jeito que veio (canais da entrada).
static void ParaFloat(const Fmt& f, const BYTE* src, UINT32 frames, std::vector<float>& out) {
    const size_t n = (size_t)frames * f.canais;
    out.resize(n);
    if (f.flutuante) { memcpy(out.data(), src, n * 4); return; }
    if (f.bits == 16) { const int16_t* s = (const int16_t*)src; for (size_t i = 0; i < n; i++) out[i] = s[i] / 32768.f; }
    else if (f.bits == 32) { const int32_t* s = (const int32_t*)src; for (size_t i = 0; i < n; i++) out[i] = (float)(s[i] / 2147483648.0); }
    else { for (size_t i = 0; i < n; i++) { const BYTE* p = src + i * 3; int32_t v = (int32_t)((p[2] << 24) | (p[1] << 16) | (p[0] << 8)); out[i] = (float)(v / 2147483648.0); } }
}
// float intercalado (canais da saida) -> formato da saida.
static void DeFloat(const Fmt& f, const float* src, UINT32 frames, BYTE* dst) {
    const size_t n = (size_t)frames * f.canais;
    if (f.flutuante) { memcpy(dst, src, n * 4); return; }
    auto clamp = [](float v) { return v > 1.f ? 1.f : (v < -1.f ? -1.f : v); };
    if (f.bits == 16) { int16_t* d = (int16_t*)dst; for (size_t i = 0; i < n; i++) d[i] = (int16_t)(clamp(src[i]) * 32767.f); }
    else if (f.bits == 32) { int32_t* d = (int32_t*)dst; for (size_t i = 0; i < n; i++) d[i] = (int32_t)(clamp(src[i]) * 2147483647.0); }
    else { for (size_t i = 0; i < n; i++) { int32_t v = (int32_t)(clamp(src[i]) * 8388607.f); BYTE* p = dst + i * 3; p[0] = (BYTE)(v & 0xFF); p[1] = (BYTE)((v >> 8) & 0xFF); p[2] = (BYTE)((v >> 16) & 0xFF); } }
}

// ------------------------------------------------------------------ a thread
struct Lado {                       // um IAudioClient (entrada ou saida)
    IMMDevice*    dev = nullptr;
    IAudioClient* cli = nullptr;
    HANDLE        evt = nullptr;
    WAVEFORMATEX* wf  = nullptr;
    Fmt           fmt;
    UINT32        bufFrames = 0;
    std::wstring  id, nome;
    void Fecha() {
        if (cli) cli->Stop();
        Solta(cli); Solta(dev);
        if (wf) { CoTaskMemFree(wf); wf = nullptr; }
        if (evt) { CloseHandle(evt); evt = nullptr; }
        bufFrames = 0; id.clear(); nome.clear(); fmt = Fmt();
    }
};

struct Motor {
    IMMDeviceEnumerator* en = nullptr;
    Lado cap, ren;
    IAudioCaptureClient* capC = nullptr;
    IAudioRenderClient*  renC = nullptr;
    bool  autoConv = false;            // o engine do Windows converte taxa/canais por nos
    std::vector<float> bufIn, tmpIn, bufOut, tmpOut;   // pacote lido / conversao / saida / ganho
    double fase = 0.0;                 // reamostragem linear (posicao fracionaria na entrada)
    std::vector<float> resto;          // ultimo quadro da entrada, para interpolar na emenda
    float  ganho = 0.f, ganhoAlvo = 1.f;
    bool   jaTocou = false, primeiroPacote = true;
    ULONGLONG ultDado = 0, ultChecagem = 0, ultLog = 0, ultFalhaCap = 0, ultFalhaRen = 0, ultSonda = 0;
    UINT   ultN = (UINT)-1;            // quantas entradas havia na ultima listagem (loga so quando muda)
    unsigned descartes = 0, recargas = 0, pacotes = 0, timeouts = 0;
    double somaPadMs = 0.0; unsigned nPad = 0;
    int    periodoMs = 10;

    void FechaCaptura() { Solta(capC); cap.Fecha(); g_tocando = false; jaTocou = false; primeiroPacote = true; resto.clear(); fase = 0.0; }
    void FechaRender()  { Solta(renC); ren.Fecha(); g_tocando = false; jaTocou = false; primeiroPacote = true; }

    // A entrada: preferencia do ini > nome parecido com o do video > padrao de captura (se
    // nao for microfone: tocar o microfone nas caixas realimenta e apita).
    IMMDevice* AcharEntrada(std::wstring* nome, std::wstring* como) {
        IMMDeviceCollection* col = nullptr;
        if (FAILED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col)) || !col) return nullptr;
        UINT n = 0; col->GetCount(&n);
        const bool logar = (n != ultN); ultN = n;
        std::wstring pref, video;
        { std::lock_guard<std::mutex> lk(g_mtxTexto); pref = g_nomePreferido; video = g_nomeVideo; }
        IMMDevice* melhor = nullptr; int melhorNota = 0; std::wstring melhorNome;
        for (UINT i = 0; i < n; i++) {
            IMMDevice* d = nullptr;
            if (FAILED(col->Item(i, &d)) || !d) continue;
            const std::wstring nm = NomeDoEndpoint(d);
            int nota = 0;
            if (!pref.empty() && ContemCI(nm, pref)) nota = 200;    // a pessoa escolheu
            else nota = Parecido(video, nm);
            if (logar) Log("audio: entrada [%u] '%s' (parecido=%d)", (unsigned)i, U8(nm).c_str(), nota);
            if (nota > melhorNota) { Solta(melhor); melhor = d; melhorNota = nota; melhorNome = nm; }
            else d->Release();
        }
        col->Release();
        if (melhor && melhorNota >= 50) {
            *nome = melhorNome; *como = (melhorNota >= 200) ? L"chave audio_dispositivo" : L"nome parecido com o do video";
            return melhor;
        }
        Solta(melhor);
        if (!pref.empty() && logar) Log("audio: nenhuma entrada contem '%s' (chave audio_dispositivo) - tentando pelo nome do video / padrao", U8(pref).c_str());
        IMMDevice* d = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eCapture, eConsole, &d)) && d) {
            const std::wstring nm = NomeDoEndpoint(d);
            if (PareceMicrofone(nm) && !ContemCI(nm, pref)) {
                if (logar) Log("audio: a entrada padrao e '%s' - parece microfone; nao vou toca-la nas caixas (sem som do console)", U8(nm).c_str());
                d->Release(); return nullptr;
            }
            if (logar) Log("audio: nenhuma entrada casou com '%s' - usando a entrada padrao '%s'", U8(video).c_str(), U8(nm).c_str());
            *nome = nm; *como = L"entrada padrao de captura";
            return d;
        }
        return nullptr;
    }

    bool AbreCaptura() {
        std::wstring nome, como;
        cap.dev = AcharEntrada(&nome, &como);
        if (!cap.dev) { g_disponivel = false; return false; }
        g_disponivel = true;   // a entrada existe: o controle aparece (mesmo que abrir falhe, ai e cartaz)
        cap.nome = nome; cap.id = IdDoEndpoint(cap.dev);
        HRESULT hr = cap.dev->Activate(kIidClient, CLSCTX_ALL, nullptr, (void**)&cap.cli);
        if (FAILED(hr) || !cap.cli) { Log("audio: Activate(entrada) hr=0x%08X", (unsigned)hr); cap.Fecha(); return false; }
        hr = cap.cli->GetMixFormat(&cap.wf);
        if (FAILED(hr) || !cap.wf) { Log("audio: GetMixFormat(entrada) hr=0x%08X", (unsigned)hr); cap.Fecha(); return false; }
        cap.fmt = LerFmt(cap.wf);
        if (!cap.fmt.ok) { Log("audio: formato da entrada nao suportado (tag=%u bits=%u)", (unsigned)cap.wf->wFormatTag, (unsigned)cap.wf->wBitsPerSample); cap.Fecha(); return false; }
        REFERENCE_TIME per = 0, perMin = 0;
        cap.cli->GetDevicePeriod(&per, &perMin);
        periodoMs = (int)((per + 5000) / 10000); if (periodoMs < 1) periodoMs = 1;
        // Buffer de 2 periodos: o menor que o modo compartilhado por evento aceita com folga.
        hr = cap.cli->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, per * 2, 0, cap.wf, nullptr);
        if (FAILED(hr)) { Log("audio: Initialize(entrada) hr=0x%08X", (unsigned)hr); cap.Fecha(); return false; }
        cap.evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        cap.cli->SetEventHandle(cap.evt);
        cap.cli->GetBufferSize(&cap.bufFrames);
        hr = cap.cli->GetService(kIidCapture, (void**)&capC);
        if (FAILED(hr) || !capC) { Log("audio: GetService(captura) hr=0x%08X", (unsigned)hr); cap.Fecha(); return false; }
        hr = cap.cli->Start();
        if (FAILED(hr)) { Log("audio: Start(entrada) hr=0x%08X", (unsigned)hr); Solta(capC); cap.Fecha(); return false; }
        { std::lock_guard<std::mutex> lk(g_mtxTexto); g_nomeEntrada = cap.nome; }
        Log("audio: entrada '%s' (%s) %d Hz %d canal(is) %s%d bits, periodo %d ms, buffer %u quadros (%.1f ms)",
            U8(cap.nome).c_str(), U8(como).c_str(), cap.fmt.taxa, cap.fmt.canais, cap.fmt.flutuante ? "float" : "pcm",
            cap.fmt.bits, periodoMs, (unsigned)cap.bufFrames, cap.bufFrames * 1000.0 / cap.fmt.taxa);
        ultDado = GetTickCount64();
        return true;
    }

    bool AbreRender() {
        HRESULT hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &ren.dev);
        if (FAILED(hr) || !ren.dev) { Log("audio: sem saida padrao (hr=0x%08X)", (unsigned)hr); ren.Fecha(); return false; }
        ren.nome = NomeDoEndpoint(ren.dev); ren.id = IdDoEndpoint(ren.dev);
        hr = ren.dev->Activate(kIidClient, CLSCTX_ALL, nullptr, (void**)&ren.cli);
        if (FAILED(hr) || !ren.cli) { Log("audio: Activate(saida) hr=0x%08X", (unsigned)hr); ren.Fecha(); return false; }
        REFERENCE_TIME per = 0, perMin = 0;
        ren.cli->GetDevicePeriod(&per, &perMin);
        const int alvo = g_alvoMs.load();
        // O buffer da saida precisa caber o alvo + 2 periodos (uma entrega inteira de folga).
        const REFERENCE_TIME dur = (REFERENCE_TIME)alvo * 10000 + per * 2;
        // 1) Deixa o engine converter taxa e canais (Win 10+): o formato da saida = o da entrada.
        autoConv = false;
        const DWORD flagsAuto = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        hr = ren.cli->Initialize(AUDCLNT_SHAREMODE_SHARED, flagsAuto, dur, 0, cap.wf, nullptr);
        if (SUCCEEDED(hr)) {
            autoConv = true; ren.fmt = cap.fmt;
        } else {
            // 2) O formato de mistura da saida, e a conversao e nossa (ParaFloat/DeFloat + linear).
            Log("audio: saida nao aceitou o formato da entrada com conversao automatica (hr=0x%08X) - convertendo aqui", (unsigned)hr);
            Solta(ren.cli);
            hr = ren.dev->Activate(kIidClient, CLSCTX_ALL, nullptr, (void**)&ren.cli);
            if (FAILED(hr) || !ren.cli) { Log("audio: Activate(saida, 2a) hr=0x%08X", (unsigned)hr); ren.Fecha(); return false; }
            hr = ren.cli->GetMixFormat(&ren.wf);
            if (FAILED(hr) || !ren.wf) { Log("audio: GetMixFormat(saida) hr=0x%08X", (unsigned)hr); ren.Fecha(); return false; }
            ren.fmt = LerFmt(ren.wf);
            if (!ren.fmt.ok) { Log("audio: formato da saida nao suportado (tag=%u bits=%u)", (unsigned)ren.wf->wFormatTag, (unsigned)ren.wf->wBitsPerSample); ren.Fecha(); return false; }
            hr = ren.cli->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, dur, 0, ren.wf, nullptr);
            if (FAILED(hr)) { Log("audio: Initialize(saida) hr=0x%08X", (unsigned)hr); ren.Fecha(); return false; }
        }
        ren.evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ren.cli->SetEventHandle(ren.evt);   // exigido pelo modo por evento, mesmo que a gente escreva no ritmo da entrada
        ren.cli->GetBufferSize(&ren.bufFrames);
        hr = ren.cli->GetService(kIidRender, (void**)&renC);
        if (FAILED(hr) || !renC) { Log("audio: GetService(render) hr=0x%08X", (unsigned)hr); ren.Fecha(); return false; }
        // Recarga inicial: silencio ate o alvo, para o primeiro pacote nao chegar num buffer vazio.
        Recarrega();
        hr = ren.cli->Start();
        if (FAILED(hr)) { Log("audio: Start(saida) hr=0x%08X", (unsigned)hr); Solta(renC); ren.Fecha(); return false; }
        REFERENCE_TIME latCap = 0, latRen = 0;
        if (cap.cli) cap.cli->GetStreamLatency(&latCap);
        ren.cli->GetStreamLatency(&latRen);
        { std::lock_guard<std::mutex> lk(g_mtxTexto); g_nomeSaida = ren.nome; }
        g_tocando = true; jaTocou = false; primeiroPacote = true; ganho = 0.f;
        Log("audio: saida '%s' %d Hz %d canal(is) %s, buffer %u quadros (%.1f ms), alvo de fila %d ms, conversao %s; "
            "latencia declarada entrada %.1f ms + saida %.1f ms; estimativa propria ~%d ms",
            U8(ren.nome).c_str(), ren.fmt.taxa, ren.fmt.canais, autoConv ? "(engine)" : (ren.fmt.flutuante ? "float" : "pcm"),
            (unsigned)ren.bufFrames, ren.bufFrames * 1000.0 / ren.fmt.taxa, alvo, autoConv ? "pelo Windows" : "nossa",
            latCap / 10000.0, latRen / 10000.0, alvo + periodoMs);
        g_latenciaMs = alvo + periodoMs;
        return true;
    }

    void Recarrega() {
        UINT32 pad = 0; if (FAILED(ren.cli->GetCurrentPadding(&pad))) return;
        const UINT32 alvoFrames = (UINT32)((long long)g_alvoMs.load() * ren.fmt.taxa / 1000);
        if (pad >= alvoFrames) return;
        UINT32 n = alvoFrames - pad; if (n > ren.bufFrames - pad) n = ren.bufFrames - pad;
        BYTE* d = nullptr;
        if (n && SUCCEEDED(renC->GetBuffer(n, &d)) && d) { renC->ReleaseBuffer(n, AUDCLNT_BUFFERFLAGS_SILENT); recargas++; }
    }

    // Converte um bloco da entrada (float, canais da entrada) para a saida (float, canais da
    // saida, taxa da saida). Devolve o ponteiro para o resultado (in, tmpIn ou bufOut) e
    // quantos quadros ha nele. Quando autoConv esta ligado, e a propria entrada.
    const float* Converte(const std::vector<float>& in, UINT32 framesIn, UINT32* framesOut) {
        const int ci = cap.fmt.canais, co = ren.fmt.canais;
        if (autoConv) { *framesOut = framesIn; return in.data(); }
        // 1) canais (tmpIn: co canais, taxa da entrada)
        tmpIn.resize((size_t)framesIn * co);
        for (UINT32 f = 0; f < framesIn; f++) {
            const float* s = &in[(size_t)f * ci]; float* d = &tmpIn[(size_t)f * co];
            for (int c = 0; c < co; c++) d[c] = (c < ci) ? s[c] : (ci == 1 ? s[0] : 0.f);
        }
        if (cap.fmt.taxa == ren.fmt.taxa) { *framesOut = framesIn; return tmpIn.data(); }
        // 2) taxa: interpolacao linear com a fase atravessando os pacotes (resto = ultimo quadro
        //    do pacote anterior, indice virtual 0; tmpIn[k] e o indice virtual k+1)
        const double passo = (double)cap.fmt.taxa / (double)ren.fmt.taxa;
        if (resto.size() != (size_t)co) { resto.assign((size_t)co, 0.f); fase = 1.0; }
        bufOut.clear(); bufOut.reserve((size_t)(framesIn / passo + 2) * co);
        UINT32 n = 0;
        while (fase < (double)framesIn) {
            const UINT32 i0 = (UINT32)fase; const float t = (float)(fase - i0);
            const float* a = (i0 == 0) ? resto.data() : &tmpIn[(size_t)(i0 - 1) * co];
            const float* b = &tmpIn[(size_t)i0 * co];
            for (int c = 0; c < co; c++) bufOut.push_back(a[c] + (b[c] - a[c]) * t);
            fase += passo; n++;
        }
        memcpy(resto.data(), &tmpIn[(size_t)(framesIn - 1) * co], (size_t)co * 4);
        fase -= (double)framesIn;   // o novo indice 0 e o ultimo quadro deste pacote
        *framesOut = n;
        return bufOut.data();
    }

    // Escreve na saida com a regra de fila: sem espaco = descarta o mais velho; fila esvaziou
    // = recarrega ate o alvo; fila acima de alvo+2 periodos = pula o excesso.
    bool Escreve(const float* out, UINT32 frames) {
        if (!frames || !out) return true;
        UINT32 pad = 0;
        HRESULT hr = ren.cli->GetCurrentPadding(&pad);
        if (FAILED(hr)) { Log("audio: GetCurrentPadding hr=0x%08X - a saida sumiu?", (unsigned)hr); return false; }
        const UINT32 alvoFrames = (UINT32)((long long)g_alvoMs.load() * ren.fmt.taxa / 1000);
        const UINT32 folga = (UINT32)((long long)periodoMs * 2 * ren.fmt.taxa / 1000);
        if (jaTocou && pad == 0) { Recarrega(); ren.cli->GetCurrentPadding(&pad); }
        somaPadMs += pad * 1000.0 / ren.fmt.taxa; nPad++;
        UINT32 pular = 0;
        if (pad > alvoFrames + folga) { pular = pad - alvoFrames; if (pular > frames) pular = frames; descartes++; }
        UINT32 n = frames - pular;
        const UINT32 espaco = ren.bufFrames - pad;
        if (n > espaco) { pular += n - espaco; n = espaco; descartes++; }
        if (!n) return true;
        BYTE* d = nullptr;
        hr = renC->GetBuffer(n, &d);
        if (FAILED(hr) || !d) { Log("audio: GetBuffer(saida) hr=0x%08X", (unsigned)hr); return false; }
        // ganho: rampa curta (~5 ms) entre som e silencio, para nao estalar ao pausar/retomar
        const int co = ren.fmt.canais;
        const float passoG = 1.f / (0.005f * ren.fmt.taxa);
        tmpOut.resize((size_t)n * co);
        const float* s = &out[(size_t)pular * co];
        for (UINT32 f = 0; f < n; f++) {
            if (ganho < ganhoAlvo) { ganho += passoG; if (ganho > ganhoAlvo) ganho = ganhoAlvo; }
            else if (ganho > ganhoAlvo) { ganho -= passoG; if (ganho < ganhoAlvo) ganho = ganhoAlvo; }
            for (int c = 0; c < co; c++) tmpOut[(size_t)f * co + c] = s[(size_t)f * co + c] * ganho;
        }
        if (ren.fmt.flutuante) memcpy(d, tmpOut.data(), (size_t)n * co * 4);
        else DeFloat(ren.fmt, tmpOut.data(), n, d);
        renC->ReleaseBuffer(n, 0);
        jaTocou = true;
        return true;
    }

    // Um evento da entrada: le todos os pacotes prontos e empurra para a saida.
    bool Bombeia() {
        UINT32 prox = 0;
        HRESULT hr = capC->GetNextPacketSize(&prox);
        if (FAILED(hr)) { Log("audio: GetNextPacketSize hr=0x%08X - a entrada sumiu?", (unsigned)hr); return false; }
        while (prox) {
            BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
            hr = capC->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) { Log("audio: GetBuffer(entrada) hr=0x%08X", (unsigned)hr); return false; }
            if (frames) {
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) bufIn.assign((size_t)frames * cap.fmt.canais, 0.f);
                else ParaFloat(cap.fmt, data, frames, bufIn);
                UINT32 fo = 0;
                const float* out = Converte(bufIn, frames, &fo);
                ganhoAlvo = (g_videoVivo.load() && g_ligado.load()) ? 1.f : 0.f;
                if (!Escreve(out, fo)) { capC->ReleaseBuffer(frames); return false; }
                pacotes++;
            }
            capC->ReleaseBuffer(frames);
            ultDado = GetTickCount64();
            if (FAILED(capC->GetNextPacketSize(&prox))) break;
        }
        return true;
    }

    // A saida padrao mudou (fone ligado, HDMI trocado)? A cada segundo, barato.
    bool SaidaMudou() {
        IMMDevice* d = nullptr;
        if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &d)) || !d) return false;
        const std::wstring id = IdDoEndpoint(d); d->Release();
        return !id.empty() && id != ren.id;
    }

    void Estatistica(bool final) {
        const double media = nPad ? somaPadMs / nPad : 0.0;
        g_latenciaMs = (int)(media + periodoMs + 0.5);
        Log("audio: %s fila media %.1f ms (+%d ms de periodo = ~%d ms de atraso proprio), pacotes %u, "
            "descartes %u, recargas %u, esperas vazias %u", final ? "fim:" : "", media, periodoMs,
            g_latenciaMs.load(), pacotes, descartes, recargas, timeouts);
        somaPadMs = 0.0; nPad = 0;
    }
};

static void Avisa(const wchar_t* pt, const wchar_t* en) {
    std::lock_guard<std::mutex> lk(g_mtxTexto);
    if (g_avisou) return;
    g_avisou = true; g_aviso = T(pt, en);
}

static void Loop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // Prioridade de audio pelo MMCSS, se o Windows der; carregado a mao para o build nao
    // precisar de -lavrt.
    HMODULE avrt = LoadLibraryW(L"avrt.dll"); HANDLE mm = nullptr;
    if (avrt) {
        typedef HANDLE (WINAPI *PFN)(LPCWSTR, LPDWORD);
        PFN f = (PFN)(void*)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW");
        DWORD idx = 0; if (f) mm = f(L"Pro Audio", &idx);
    }
    Motor m;
    HRESULT hr = CoCreateInstance(kClsidEnum, nullptr, CLSCTX_ALL, kIidEnum, (void**)&m.en);
    if (FAILED(hr) || !m.en) {
        Log("audio: MMDeviceEnumerator hr=0x%08X - sem som do console nesta sessao", (unsigned)hr);
        Avisa(L"Som do console: não deu para tocar\no vídeo segue normal", L"Console sound: could not play\nthe video keeps going");
    }
    int falhasCap = 0, falhasRen = 0;
    while (g_rodando && m.en) {
        const ULONGLONG agora = GetTickCount64();
        if (!g_ligado) {
            // Chave desligada: solta os dois lados (a placa fica livre) e dorme ate religar.
            if (m.cap.cli || m.ren.cli) { m.FechaRender(); m.FechaCaptura(); Log("audio: desligado pela chave"); }
            // ...mas continua sabendo se a entrada EXISTE: e isso que mantem o controle na tela.
            if (agora - m.ultSonda >= 5000) {
                m.ultSonda = agora;
                std::wstring nm, como; IMMDevice* d = m.AcharEntrada(&nm, &como);
                g_disponivel = (d != nullptr); Solta(d);
            }
            WaitForSingleObject(g_evtAcorda, 250);
            continue;
        }
        if (!m.cap.cli) {
            if (agora - m.ultFalhaCap < (g_disponivel ? 2000u : 5000u)) { WaitForSingleObject(g_evtAcorda, 250); continue; }
            if (!m.AbreCaptura()) {
                m.ultFalhaCap = agora;
                if (++falhasCap == 1) {
                    if (g_disponivel) Avisa(L"Som do console: não deu para tocar\no vídeo segue normal", L"Console sound: could not play\nthe video keeps going");
                    Log("audio: sem som do console por enquanto - o video segue (tento de novo a cada %d s, sem alarde)", g_disponivel ? 2 : 5);
                }
                continue;
            }
            falhasCap = 0;
        }
        if (!m.ren.cli) {
            if (agora - m.ultFalhaRen < 2000) { WaitForSingleObject(g_evtAcorda, 250); continue; }
            if (!m.AbreRender()) {
                m.ultFalhaRen = agora;
                if (++falhasRen == 1) Avisa(L"Som do console: não deu para tocar\no vídeo segue normal", L"Console sound: could not play\nthe video keeps going");
                continue;
            }
            falhasRen = 0;
        }
        HANDLE hs[2] = { m.cap.evt, g_evtAcorda };
        const DWORD r = WaitForMultipleObjects(2, hs, FALSE, 500);
        if (!g_rodando) break;
        if (r == WAIT_OBJECT_0) {
            if (!m.Bombeia()) {
                // Qual lado morreu? A entrada e testada pelo GetNextPacketSize, a saida pelo padding.
                UINT32 p = 0;
                const bool saidaViva = m.ren.cli && SUCCEEDED(m.ren.cli->GetCurrentPadding(&p));
                if (!saidaViva) { Log("audio: a saida caiu - reabrindo na saida padrao atual"); m.FechaRender(); }
                else { Log("audio: a entrada caiu (placa desligada?) - procurando de novo"); m.FechaCaptura(); }
                continue;
            }
        } else if (r == WAIT_TIMEOUT) {
            m.timeouts++;
            // 2 s sem um unico evento da placa: ela sumiu ou parou. Procura de novo.
            if (GetTickCount64() - m.ultDado > 2000) {
                Log("audio: a entrada ficou 2 s sem entregar nada - reabrindo");
                m.FechaCaptura(); m.ultFalhaCap = 0;
                continue;
            }
        }
        const ULONGLONG t = GetTickCount64();
        if (t - m.ultChecagem >= 1000) {
            m.ultChecagem = t;
            if (m.SaidaMudou()) { Log("audio: a saida padrao do Windows mudou - trocando"); m.FechaRender(); m.ultFalhaRen = 0; continue; }
        }
        if (t - m.ultLog >= 60000) { m.ultLog = t; m.Estatistica(false); }
    }
    if (m.ren.cli || m.cap.cli) m.Estatistica(true);
    m.FechaRender(); m.FechaCaptura();
    Solta(m.en);
    g_tocando = false;
    if (mm && avrt) {
        typedef BOOL (WINAPI *PFNR)(HANDLE);
        PFNR f = (PFNR)(void*)GetProcAddress(avrt, "AvRevertMmThreadCharacteristics");
        if (f) f(mm);
    }
    if (avrt) FreeLibrary(avrt);
    CoUninitialize();
    Log("audio: thread encerrada");
}

// ------------------------------------------------------------------ a face para o app
// nomeVideo = o dispositivo escolhido no seletor; preferido = chave `audio_dispositivo` (parte
// do nome, pode ser vazia); ligado = chave `audio_captura` (padrao ligado); alvoMs = chave
// `audio_buffer_ms` (0 = padrao). Nunca falha para quem chama: tudo acontece na thread.
inline void Iniciar(const std::wstring& nomeVideo, const std::wstring& preferido, bool ligado, int alvoMs) {
    if (g_rodando) return;
    { std::lock_guard<std::mutex> lk(g_mtxTexto); g_nomeVideo = nomeVideo; g_nomePreferido = preferido; g_aviso.clear(); g_avisou = false; }
    if (alvoMs <= 0) alvoMs = kAlvoPadraoMs;
    if (alvoMs < kAlvoMinMs) alvoMs = kAlvoMinMs;
    if (alvoMs > kAlvoMaxMs) alvoMs = kAlvoMaxMs;
    g_alvoMs = alvoMs; g_ligado = ligado; g_disponivel = false; g_tocando = false; g_videoVivo = true;
    g_evtAcorda = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_rodando = true;
    Log("audio: procurando a entrada da placa '%s' (preferencia '%s', chave %s, alvo de fila %d ms)",
        U8(nomeVideo).c_str(), U8(preferido).c_str(), ligado ? "ligada" : "desligada", alvoMs);
    g_th = std::thread(&Loop);
}
inline void Parar() {
    if (!g_rodando) return;
    g_rodando = false;
    if (g_evtAcorda) SetEvent(g_evtAcorda);
    if (g_th.joinable()) {
        // Como a captura de video: se a thread travou dentro do WASAPI, nao esperamos para sempre.
        if (WaitForSingleObject((HANDLE)g_th.native_handle(), 3000) == WAIT_OBJECT_0) g_th.join();
        else { g_th.detach(); Log("audio: a thread nao terminou em 3 s - abandonada"); return; }
    }
    if (g_evtAcorda) { CloseHandle(g_evtAcorda); g_evtAcorda = nullptr; }
}
inline void Ligar(bool on)   { g_ligado = on; if (g_evtAcorda) SetEvent(g_evtAcorda); }
inline bool Ligado()         { return g_ligado; }
inline bool Disponivel()     { return g_disponivel; }
inline bool Tocando()        { return g_tocando; }
inline int  LatenciaMs()     { return g_latenciaMs; }
inline void VideoVivo(bool v) { g_videoVivo = v; }
// O cartaz pendente, uma vez so. Devolve false quando nao ha nada a dizer.
inline bool PegarAviso(std::wstring* txt) {
    std::lock_guard<std::mutex> lk(g_mtxTexto);
    if (g_aviso.empty()) return false;
    *txt = g_aviso; g_aviso.clear();
    return true;
}
inline std::wstring NomeEntrada() { std::lock_guard<std::mutex> lk(g_mtxTexto); return g_nomeEntrada; }

}   // namespace AudioCap
