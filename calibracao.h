// =====================================================================================
//  calibracao.h - sec. 23 (OCLUSAO-2309): CALIBRACAO POR PLACA e MEMORIA DE RESETS
// =====================================================================================
//
//  (1) CALIBRACAO. O veto por custo (sec. 21/22) e as estimativas do painel usavam numeros
//  medidos numa RTX 4080 a 1080p (passe a 100% = 4,5 ms, a 50% = 2,4 ms, parte fixa 2,5 ms).
//  Noutra placa, ou noutra resolucao, eles erram para os dois lados. Aqui o custo de UM PASSE
//  da rede e modelado como
//        custo(passe) = piso + inclinacao x area        (ms; area em megapixels)
//  com piso e inclinacao MEDIDOS na placa do usuario, a partir do perfil fino do feed
//  (`nr_ms_passe` do .status: carimbos de GPU por passe, media movel por quadro no feed) e da
//  escala que cada passe de fato rodou (`nr_escala_passe` do .status, a lista APLICADA).
//
//  - Chave: nome da placa + vendor:device (PCI) + resolucao de trabalho da rede. Ficam no ini,
//    secao [calibracao], uma linha por chave: piso, inclinacao, faixa de area medida, parte
//    fixa do pacote (pacote - passes), amostras, a ultima taxa da fonte e a DATA.
//  - Calibracao curta: a 1a vez que a chave aparece (placa ou resolucao nova), a primeira
//    medida do feed (~120 quadros depois de a rede montar: 1-2 s a 60-120/s) calibra e grava.
//  - Refinamento continuo: cada medida publicada entra numa media movel LENTA por escala
//    (alfa 1/n ate 0,03: constante de ~30 medidas, ~1 min); o ajuste e refeito e regravado
//    no ini a cada 60 s se mudou.
//  - Com UMA escala medida so (ex.: 1 passe a 100%), piso e inclinacao nao se separam. Nesse
//    caso (decisao do coordenador, opcao b) vale a FORMA da curva de reserva passando pelo ponto
//    medido - "estimativa parcial (1 escala medida)"; com a 2a escala, so numeros desta placa.
//  - As constantes da 4080 ficam SO como reserva antes da 1a calibracao, e o painel diz
//    "estimativa não calibrada" enquanto elas estiverem valendo.
//
//  (2) MEMORIA DE RESETS. Quando o GpuReset registra um reinicio da placa ("reinicio N"),
//  grava tambem a configuracao em vigor (placa, passes, escalas, FG, fonte, resolucao) em
//  [resets] reset_N. Escolher de novo a MESMA combinacao naquela placa so mostra um TEXTO
//  discreto sob a barra "Rede neural" (pedido do dono: nenhum popup); a mudanca aplica normalmente.
#pragma once

namespace Calib {

static const int kPasses = 10;
// RESERVA (RTX 4080, 1080p, sessao B do reset de 23/09): SO antes da 1a calibracao nesta placa.
// 100% = 4,5 ms e 50% = 2,4 ms -> piso 1,7 ms + 2,8 ms por 2,07 MP = 1,35 ms/MP; fixo 2,5 ms.
static const double kResPisoMs = 1.7, kResInclMsMp = 1.35, kResFixoMs = 2.5;
static const double kResAreaMp = 1920.0 * 1080.0 / 1e6;

struct Balde { double ms = 0.0; double n = 0.0; };
static Balde g_b[41];                 // por escala/5 (0..200%)
static std::wstring g_placaNome;
static UINT g_ven = 0, g_dev = 0;
static unsigned g_w = 0, g_h = 0;     // resolucao de trabalho da rede (feature ready)
static std::wstring g_chave;          // chave no ini (vazia = ainda nao ha placa+resolucao)
static int g_estado = 0;              // 0 sem chave, 1 calibrando, 2 calibrada
static bool g_modelo = false;         // ha piso/inclinacao desta chave
static double g_piso = 0.0, g_incl = 0.0, g_aMin = 0.0, g_aMax = 0.0;
static double g_fixo = -1.0, g_fixoN = 0.0;
static unsigned long long g_amostras = 0;
static std::wstring g_data;           // "2026-09-23 22:40" (quando calibrou)
static double g_fonteFps = 0.0;       // a ultima taxa de fonte vista (a tela de escolha usa)
static FILETIME g_ultMtime{};
static ULONGLONG g_ultGravou = 0;
static bool g_sujo = false;
static std::vector<int> g_escRodando; // a lista de escalas que o feed disse ter rodado (ultimo .status)

static std::wstring AgoraTexto() {
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t b[32]; _snwprintf(b, 32, L"%04u-%02u-%02u %02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute); b[31] = 0;
    return b;
}
// "2026-09-23 22:40" -> "23/09 22:40" (o painel e curto)
static std::wstring DataCurta(const std::wstring& d) {
    if (d.size() < 16) return d;
    return d.substr(8, 2) + L"/" + d.substr(5, 2) + L" " + d.substr(11, 5);
}
static std::wstring Limpo(const std::wstring& s) {
    std::wstring r;
    for (wchar_t c : s) r += (iswalnum(c) ? c : L'_');
    while (r.find(L"__") != std::wstring::npos) r.replace(r.find(L"__"), 2, L"_");
    return r;
}
static std::wstring PrefixoPlaca(const std::wstring& nome, UINT ven, UINT dev) {
    wchar_t b[32]; _snwprintf(b, 32, L"_%04X-%04X", ven, dev); b[31] = 0;
    return Limpo(nome) + b;
}
static double AreaMp(int escala) {
    const double f = (escala > 0 ? escala : 100) / 100.0;
    const double base = (g_w && g_h) ? (double)g_w * (double)g_h / 1e6 : kResAreaMp;
    return base * f * f;
}
static int EscalaDeArea(double a) {
    const double base = (g_w && g_h) ? (double)g_w * (double)g_h / 1e6 : kResAreaMp;
    if (base <= 0.0 || a <= 0.0) return 100;
    return (int)(100.0 * sqrt(a / base) + 0.5);
}

// O ajuste: minimos quadrados ponderados sobre as escalas medidas (peso = min(n, 30)).
static void Ajustar() {
    double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0, xMin = 1e9, xMax = 0, melhorN = 0, melhorX = 0, melhorY = 0;
    int k = 0;
    for (int i = 0; i < 41; i++) {
        if (g_b[i].n <= 0.0) continue;
        const double x = AreaMp(i * 5), y = g_b[i].ms, w = g_b[i].n < 30.0 ? g_b[i].n : 30.0;
        sw += w; sx += w * x; sy += w * y; sxx += w * x * x; sxy += w * x * y; k++;
        if (x < xMin) xMin = x;
        if (x > xMax) xMax = x;
        if (g_b[i].n > melhorN) { melhorN = g_b[i].n; melhorX = x; melhorY = y; }
    }
    if (k == 0) return;
    if (k >= 2 && xMax > 1.3 * xMin) {
        const double mx = sx / sw, my = sy / sw, vx = sxx / sw - mx * mx, cxy = sxy / sw - mx * my;
        double incl = vx > 1e-9 ? cxy / vx : 0.0, piso = my - incl * mx;
        if (incl < 0.0) { incl = 0.0; piso = my; }
        if (piso < 0.0) { piso = 0.0; incl = sxx > 1e-9 ? sxy / sxx : 0.0; }
        g_piso = piso; g_incl = incl; g_aMin = xMin; g_aMax = xMax;
    } else {
        g_piso = melhorY; g_incl = 0.0; g_aMin = g_aMax = melhorX;   // uma escala so: forma da curva de reserva pelo ponto medido (CustoPasse)
    }
    g_modelo = true;
}

// O custo de UM passe na escala dada (ms de GPU so da rede).
static double CustoPasse(int escala) {
    const double a = AreaMp(escala);
    if (!g_modelo) return kResPisoMs + kResInclMsMp * a;
    if (g_aMax > 1.3 * g_aMin) { const double c = g_piso + g_incl * a; return c > 0.05 ? c : 0.05; }
    // UMA ESCALA MEDIDA (g_aMin = g_aMax = a0, g_piso = custo medido nela). Decisao do coordenador (sec. 23, opcao b):
    // usa-se so a FORMA da curva de reserva (a fracao do custo que e piso por chamada vs a que e proporcional a area),
    // escalada para passar EXATAMENTE pelo ponto medido nesta placa: c(a) = c0 x (piso_r + incl_r x a) / (piso_r + incl_r x a0).
    // Nenhum numero absoluto de outra placa entra no resultado; com a 2a escala medida, a curva passa a ser so desta placa.
    if (g_aMin <= 0.0) return g_piso;
    const double r0 = kResPisoMs + kResInclMsMp * g_aMin;
    return r0 > 1e-6 ? g_piso * (kResPisoMs + kResInclMsMp * a) / r0 : g_piso;
}
static double RedeMs(const std::vector<int>& esc) {
    double s = 0.0;
    for (int e : esc) s += CustoPasse(e);
    return s;
}
// CustoPasseArea()/RedeMsArea() (a mesma conta de CustoPasse, mas com a AREA BASE escolhida
// por quem chama) SAIRAM em 24/09/2026 (item 8 do dono): so a BarraDaTela() da tela de escolha
// (seletor_hud.h, tambem removida) usava a estimativa antes de a sessao comecar.
static double FixoMs() { return g_fixo >= 0.0 ? g_fixo : kResFixoMs; }
static bool Calibrado() { return g_modelo; }
static bool UmaEscala() { return g_modelo && !(g_aMax > 1.3 * g_aMin); }
static std::wstring Rotulo() {
    if (g_estado == 1 && !g_modelo) return TS(L"calibrando nesta placa…", L"calibrating on this card…");
    if (!g_modelo) return TS(L"estimativa não calibrada", L"uncalibrated estimate");
    std::wstring r = TS(L"calibrado nesta placa", L"calibrated on this card");
    if (!g_data.empty()) r += TS(L" em ", L" on ") + DataCurta(g_data);
    if (UmaEscala()) r += TS(L" — estimativa parcial (1 escala medida)", L" — partial estimate (1 scale measured)");
    return r;
}

static void Gravar(const char* motivo) {
    if (g_chave.empty() || !g_modelo) return;
    wchar_t v[512];
    _snwprintf(v, 512, L"piso_ms=%.3f;incl_ms_mp=%.3f;area_min_mp=%.3f;area_max_mp=%.3f;fixo_ms=%.3f;amostras=%llu;fonte_fps=%.1f;placa=%ls;res=%ux%u;data=%ls",
               g_piso, g_incl, g_aMin, g_aMax, g_fixo, g_amostras, g_fonteFps, g_placaNome.c_str(), g_w, g_h, g_data.c_str());
    v[511] = 0;
    WritePrivateProfileStringW(L"calibracao", g_chave.c_str(), v, IniPath().c_str());
    WritePrivateProfileStringW(L"calibracao", L"formato", L"custo por passe = piso_ms + incl_ms_mp x area (MP); fixo_ms = pacote - passes", IniPath().c_str());
    g_ultGravou = GetTickCount64(); g_sujo = false;
    Log("calibracao: %s - [calibracao] %s: piso %.2f ms + %.3f ms/MP (area medida %.2f-%.2f MP%s), fixo %.2f ms, %llu medidas, desde %s",
        motivo, U8(g_chave).c_str(), g_piso, g_incl, g_aMin, g_aMax, g_aMax > 1.3 * g_aMin ? "" : ", 1 escala: estimativa PARCIAL (forma da curva de reserva pelo ponto medido)",
        g_fixo, g_amostras, U8(g_data).c_str());
}
static double Campo(const std::wstring& v, const wchar_t* nome, double padrao) {
    const std::wstring k = std::wstring(nome) + L"=";
    const size_t p = v.find(k);
    return p == std::wstring::npos ? padrao : _wtof(v.c_str() + p + k.size());
}
static std::wstring CampoTxt(const std::wstring& v, const wchar_t* nome) {
    const std::wstring k = std::wstring(nome) + L"=";
    const size_t p = v.find(k);
    if (p == std::wstring::npos) return L"";
    const size_t f = v.find(L';', p);
    return v.substr(p + k.size(), f == std::wstring::npos ? std::wstring::npos : f - p - k.size());
}
static std::wstring LerLinha(const std::wstring& chave) {
    wchar_t b[1024]{};
    GetPrivateProfileStringW(L"calibracao", chave.c_str(), L"", b, 1024, IniPath().c_str());
    return b;
}
// Chaves() (todas as chaves da secao [calibracao], para achar a mais recente de uma placa)
// SAIU em 24/09/2026 junto com CarregarMaisRecenteDaPlaca() - era a unica chamadora.

// Placa e resolucao de trabalho (1x/s). Chave nova -> carrega do ini ou comeca a calibrar.
static void Definir(const std::wstring& placa, UINT ven, UINT dev, unsigned w, unsigned h) {
    if (placa.empty() || !w || !h) return;
    wchar_t r[32]; _snwprintf(r, 32, L"_%ux%u", w, h); r[31] = 0;
    const std::wstring chave = PrefixoPlaca(placa, ven, dev) + r;
    if (chave == g_chave) return;
    if (g_sujo) Gravar("troca de placa/resolucao");
    const bool tinha = !g_chave.empty();
    g_chave = chave; g_placaNome = placa; g_ven = ven; g_dev = dev; g_w = w; g_h = h;
    for (auto& b : g_b) b = Balde{};
    g_modelo = false; g_piso = g_incl = g_aMin = g_aMax = 0.0; g_fixo = -1.0; g_fixoN = 0.0; g_amostras = 0; g_data.clear();
    const std::wstring v = LerLinha(chave);
    if (!v.empty() && Campo(v, L"piso_ms", -1.0) >= 0.0) {
        g_piso = Campo(v, L"piso_ms", 0.0); g_incl = Campo(v, L"incl_ms_mp", 0.0);
        g_aMin = Campo(v, L"area_min_mp", 0.0); g_aMax = Campo(v, L"area_max_mp", 0.0);
        g_fixo = Campo(v, L"fixo_ms", -1.0); g_amostras = (unsigned long long)Campo(v, L"amostras", 0.0);
        g_fonteFps = Campo(v, L"fonte_fps", 0.0); g_data = CampoTxt(v, L"data");
        g_modelo = g_aMin > 0.0;
        // semente das medias por escala: o ajuste guardado, com peso limitado (o refinamento o move)
        const double peso = g_amostras < 10 ? (double)(g_amostras ? g_amostras : 1) : 10.0;
        if (g_modelo) {
            const int s0 = EscalaDeArea(g_aMin) / 5;
            if (s0 >= 0 && s0 < 41) { g_b[s0].ms = g_piso + g_incl * g_aMin; g_b[s0].n = peso; }
            if (g_aMax > 1.3 * g_aMin) {
                const int s1 = EscalaDeArea(g_aMax) / 5;
                if (s1 >= 0 && s1 < 41 && s1 != s0) { g_b[s1].ms = g_piso + g_incl * g_aMax; g_b[s1].n = peso; }
            }
            if (g_fixo >= 0.0) g_fixoN = peso;
        }
        g_estado = g_modelo ? 2 : 1;
        Log("calibracao: %s%s - calibrada em %s: piso %.2f ms + %.3f ms/MP, fixo %.2f ms (%llu medidas); o refinamento continua a cada medida do feed",
            tinha ? "placa/resolucao MUDOU -> " : "", U8(chave).c_str(), U8(g_data).c_str(), g_piso, g_incl, g_fixo, g_amostras);
    } else {
        g_estado = 1;
        Log("calibracao: %s%s SEM calibracao - calibracao curta na primeira medida do perfil fino do feed (~120 quadros); ate la o veto e o painel usam a RESERVA (RTX 4080 a 1080p) marcada como 'estimativa nao calibrada'",
            tinha ? "placa/resolucao MUDOU -> " : "", U8(chave).c_str());
    }
}

// Uma medida publicada pelo feed. `mtime` do .status evita contar a mesma medida duas vezes.
static void Amostra(const FILETIME& mtime, bool passagem, const int* esc, int nEsc, const double* ms, int nMs,
                    double msTotal, double pacoteMs, double fonteFps) {
    if (fonteFps > 5.0) g_fonteFps = fonteFps;
    g_escRodando.assign(esc, esc + (nEsc > 0 ? nEsc : 0));
    if (g_chave.empty() || passagem || nEsc < 1 || nEsc != nMs || nEsc > kPasses) return;
    if (CompareFileTime(&mtime, &g_ultMtime) == 0) return;
    g_ultMtime = mtime;
    double soma = 0.0;
    for (int i = 0; i < nMs; i++) { if (!(ms[i] > 0.01) || ms[i] > 500.0) return; soma += ms[i]; }
    for (int i = 0; i < nMs; i++) {
        int b = (esc[i] + 2) / 5; if (b < 0) b = 0; if (b > 40) b = 40;
        Balde& k = g_b[b];
        k.n += 1.0;
        const double a = 1.0 / k.n > 0.03 ? 1.0 / k.n : 0.03;
        k.ms = (k.n <= 1.0) ? ms[i] : k.ms + a * (ms[i] - k.ms);
    }
    const double tot = msTotal > 0.0 ? msTotal : soma;
    if (pacoteMs > tot && tot > 0.0) {
        const double f = pacoteMs - tot;
        g_fixoN += 1.0;
        const double a = 1.0 / g_fixoN > 0.03 ? 1.0 / g_fixoN : 0.03;
        g_fixo = (g_fixo < 0.0 || g_fixoN <= 1.0) ? f : g_fixo + a * (f - g_fixo);
    }
    g_amostras++;
    Ajustar();
    g_sujo = true;
    if (g_estado == 1) {
        g_estado = 2; g_data = AgoraTexto();
        Gravar("CALIBRACAO CURTA concluida (1a medida do feed nesta placa/resolucao)");
    } else if (GetTickCount64() - g_ultGravou >= 60000) Gravar("refinamento");
}
static void Encerrar() { if (g_sujo) Gravar("fim da sessao"); }

// CarregarMaisRecenteDaPlaca() (a calibracao mais recente desta placa, qualquer resolucao)
// SAIU em 24/09/2026 (item 8 do dono): so a BarraDaTela() da tela de escolha (removida junto,
// ver seletor_hud.h) chamava isto antes de a sessao comecar.

}   // namespace Calib

// ------------------------------------------------------------------ memoria de resets
namespace ResetMem {

struct Combo {
    std::wstring placa;        // "NVIDIA GeForce RTX 4080 (10DE:2704)"
    int passes = 0;
    std::wstring escalas;      // "100,50,50"
    int fg = 0;                // 1 = geracao de quadros (interpolada) ligada
    std::wstring fonte;        // "janela:msedge.exe", "monitor:DISPLAY2 ...", "camera:..."
    unsigned w = 0, h = 0;     // resolucao de trabalho da rede (0 = ainda nao sabida)
};
static Combo g_vigor;          // a configuracao EM VIGOR, atualizada 1x/s pelo laco

static std::wstring Placa(const std::wstring& nome, UINT ven, UINT dev) {
    wchar_t b[24]; _snwprintf(b, 24, L" (%04X:%04X)", ven, dev); b[23] = 0;
    return nome + b;
}
// A fonte como a memoria guarda: janela pelo EXE (o titulo muda sozinho), monitor pelo rotulo,
// camera/placa de captura pelo nome do dispositivo.
static std::wstring RotuloFonte(const Choice& C) {
    if (C.kind == SRC_WINDOW) {
        std::wstring e = (C.win && IsWindow(C.win)) ? Desempenho::ExeDaJanela(C.win) : std::wstring();
        const size_t b = e.find_last_of(L"\\/");
        if (b != std::wstring::npos) e = e.substr(b + 1);
        for (auto& ch : e) ch = (wchar_t)towlower(ch);
        return L"janela:" + (e.empty() ? C.winTitle : e);
    }
    if (C.kind == SRC_MONITOR) return L"monitor:" + C.monLabel;
    return L"camera:" + g_fonteDisp;
}
static std::wstring Texto(const Combo& c) {
    wchar_t r[32]; _snwprintf(r, 32, L"%ux%u", c.w, c.h); r[31] = 0;
    std::wstring f = c.fonte; for (auto& ch : f) if (ch == L'|' || ch == L';') ch = L' ';
    return L"placa=" + c.placa + L"|passes=" + std::to_wstring(c.passes) + L"|escalas=" + c.escalas +
           L"|fg=" + std::to_wstring(c.fg) + L"|fonte=" + f + L"|res=" + r;
}
static std::wstring Campo(const std::wstring& v, const wchar_t* nome) {
    const std::wstring k = std::wstring(nome) + L"=";
    size_t p = 0;
    for (;;) {
        p = v.find(k, p);
        if (p == std::wstring::npos) return L"";
        if (p == 0 || v[p - 1] == L'|') break;
        p++;
    }
    const size_t f = v.find(L'|', p);
    return v.substr(p + k.size(), f == std::wstring::npos ? std::wstring::npos : f - p - k.size());
}
// Chamado pelo GpuReset::Registrar: reset_N = data + a configuracao em vigor.
static void Gravar(int n) {
    const std::wstring v = L"data=" + Calib::AgoraTexto() + L"|" + Texto(g_vigor);
    WritePrivateProfileStringW(L"resets", (L"reset_" + std::to_wstring(n)).c_str(), v.c_str(), IniPath().c_str());
    Log("resets: reset_%d gravado com a configuracao em vigor: %s", n, U8(v).c_str());
}
// A combinacao ja causou um reset nesta placa? Devolve a data do mais recente (vazio = nao).
// Placa, passes, escalas, FG e fonte tem de bater; a resolucao so conta quando os dois lados a sabem.
static std::wstring CausouReset(const Combo& c) {
    if (c.placa.empty() || c.passes <= 0) return L"";
    std::vector<wchar_t> buf(32768);
    const DWORD n = GetPrivateProfileStringW(L"resets", nullptr, L"", buf.data(), (DWORD)buf.size(), IniPath().c_str());
    std::wstring melhor;
    for (DWORD i = 0; i < n; ) {
        const std::wstring k(buf.data() + i);
        if (k.empty()) break;
        i += (DWORD)k.size() + 1;
        wchar_t b[1024]{};
        GetPrivateProfileStringW(L"resets", k.c_str(), L"", b, 1024, IniPath().c_str());
        const std::wstring v = b;
        if (Campo(v, L"placa") != c.placa || _wtoi(Campo(v, L"passes").c_str()) != c.passes || Campo(v, L"escalas") != c.escalas ||
            _wtoi(Campo(v, L"fg").c_str()) != c.fg) continue;
        std::wstring f = c.fonte; for (auto& ch : f) if (ch == L'|' || ch == L';') ch = L' ';
        if (Campo(v, L"fonte") != f) continue;
        unsigned w = 0, h = 0; swscanf(Campo(v, L"res").c_str(), L"%ux%u", &w, &h);
        if (c.w && w && (c.w != w || c.h != h)) continue;
        const std::wstring d = Campo(v, L"data");
        if (d > melhor) melhor = d;
    }
    return melhor;
}
// sec. 23 (dono: sem popup): so o TEXTO discreto sob a barra. Cache de 2 s por combinacao (a barra redesenha muito).
static std::wstring CausouResetCache(const Combo& c) {
    static std::wstring s_chave, s_res; static ULONGLONG s_em = 0;
    const std::wstring t = Texto(c); const ULONGLONG ag = GetTickCount64();
    if (t == s_chave && ag - s_em < 2000) return s_res;
    s_chave = t; s_em = ag; s_res = CausouReset(c);
    return s_res;
}
}   // namespace ResetMem

// ------------------------------------------------------------------ sec. 23: TETO APRENDIDO DA PLACA INTEIRA
// A ocupacao da placa inteira (NVML) e so REDE DE SEGURANCA: o teto nasce em 95% e so cai quando ESTA placa resetou.
// No reset, X = a maior leitura dos ultimos 8 s; o teto passa a X - 5 (minimo 75) e fica gravado em [calibracao]
// teto_placa_<placa>. Volta +2 pontos por sessao limpa >= 30 min, ate 95 (sec. 24, TetoPlaca::Tick). A abertura seguinte (inclusive a do reinicio automatico) usa esse teto.
namespace TetoPlaca {
static std::wstring Chave() {
    if (g_placaIdx < 0 || g_placaIdx >= (int)g_placas.size()) return L"";
    return L"teto_placa_" + Calib::PrefixoPlaca(g_placas[g_placaIdx].nome, g_placas[g_placaIdx].vendor, g_placas[g_placaIdx].device);
}
static int Lido() {
    const std::wstring k = Chave(); if (k.empty()) return 0;
    wchar_t b[32]{}; GetPrivateProfileStringW(L"calibracao", k.c_str(), L"", b, 32, IniPath().c_str());
    return _wtoi(b);
}
static void Aplicar() {
    const int t = Lido();
    if (t >= 75 && t <= 95 && Esteira::kPlacaLim > 0.0 && t / 100.0 < Esteira::kPlacaLim) {
        Log("esteira: teto APRENDIDO da placa inteira desta placa: %d%% (caiu depois de um reset; [calibracao] %s) - no lugar de %.0f%%",
            t, U8(Chave()).c_str(), 100.0 * Esteira::kPlacaLim);
        Esteira::kPlacaLim = t / 100.0;
    }
}
static bool g_resetNestaSessao = false;   // sec. 24: a sessao com reset nunca devolve pontos ao teto
static int  g_segLimpos = 0; static bool g_devolveu = false;
static void AprenderNoReset() {
    g_resetNestaSessao = true;
    const std::wstring k = Chave(); if (k.empty()) return;
    const int x = Esteira::PlacaMaxRecente(8000.0);
    if (x < 0) { Log("esteira: reset sem leitura recente da placa inteira (NVML) - o teto aprendido NAO muda"); return; }
    int atual = Lido(); if (atual <= 0) atual = (int)(100.0 * Esteira::kPlacaLim + 0.5);
    int novo = x - 5; if (novo < 75) novo = 75;
    if (atual > 0 && novo >= atual) { Log("esteira: reset com a placa inteira em %d%% - o teto aprendido fica em %d%% (so cai)", x, atual); return; }
    WritePrivateProfileStringW(L"calibracao", k.c_str(), std::to_wstring(novo).c_str(), IniPath().c_str());
    Esteira::kPlacaLim = novo / 100.0;
    Log("esteira: reset com a placa inteira em %d%% - teto APRENDIDO desta placa: %d%% -> %d%% ([calibracao] %s; volta +%d pontos por sessao limpa >= %.0f min)",
        x, atual, novo, U8(k).c_str(), Esteira::g_ctl.placaVoltaPts, Esteira::g_ctl.placaVoltaMin);
}
// sec. 24 (PESQ-CONTROLE-CARGA 3.6): o teto aprendido VOLTA devagar - +guarda_placa_volta_pts (2) por sessao LIMPA (sem reset)
// com >= guarda_placa_volta_min (30) minutos de medida, ate 95. Grava no ini ao completar os 30 min; vale na PROXIMA abertura
// (esta sessao segue com o teto com que abriu). Um reset por outro motivo (driver, jogo) deixa de rebaixar a placa para sempre.
static void Tick() {
    if (g_devolveu || g_resetNestaSessao || Esteira::g_ctl.placaVoltaPts <= 0) return;
    if (Esteira::Ms() - Esteira::g_ultRelMs > 2500.0) return;   // so conta segundo com a esteira medindo (a sessao de verdade)
    if (++g_segLimpos < (int)(60.0 * Esteira::g_ctl.placaVoltaMin + 0.5)) return;
    g_devolveu = true;
    const std::wstring k = Chave(); if (k.empty()) return;
    const int t = Lido();
    if (t <= 0 || t >= Esteira::g_ctl.placaVoltaMax) return;   // nunca caiu (ou ja voltou): nada a devolver
    const int novo = std::min(Esteira::g_ctl.placaVoltaMax, t + Esteira::g_ctl.placaVoltaPts);
    WritePrivateProfileStringW(L"calibracao", k.c_str(), std::to_wstring(novo).c_str(), IniPath().c_str());
    Log("esteira: %.0f min sem reset nesta sessao - teto aprendido da placa inteira VOLTA %d%% -> %d%% ([calibracao] %s; vale na proxima abertura; ate %d%%)",
        Esteira::g_ctl.placaVoltaMin, t, novo, U8(k).c_str(), Esteira::g_ctl.placaVoltaMax);
}
}   // namespace TetoPlaca

// ------------------------------------------------------------------ sec. 24: MEMORIA DA TAXA POR CONFIGURACAO ([taxa_estavel])
// (substitui a da sec. 23; PESQ-CONTROLE-CARGA 3.5)
// Chave: placa (nome + vendor:device) + VERSAO DO DRIVER (UMD, DXGI) + resolucao de trabalho + escalas que rodam (uma por
// passe = passes e escalas) + TIPO DA FONTE (video / jogo / desktop) + classe da fonte livre (30/60/120/240).
// Guarda um HISTOGRAMA de SEGUNDOS ESTAVEIS por degrau (Esteira::g_estavelAgora: fora do HOLD, da prova da sonda, dos
// guarda_mem_pos_descida_s depois de uma descida e dos guarda_mem_ini_s iniciais da chave) - o "tempo preso embaixo" de uma
// cena pesada nao entra. Entre sessoes: nova x guarda_mem_alfa (0,3) + anterior x (1 - alfa). Sessao com menos de
// guarda_mem_min_s (60) segundos estaveis NAO grava. Uso: a guarda comeca na MODA ponderada do histograma e a regra de
// subida vale desde o 1o segundo - a memoria e ponto de partida, nunca teto.
namespace TaxaMem {
static std::wstring g_chave, g_pend; static int g_pendSeg = 0;
static int g_taxa[64] = {}; static int g_seg[64] = {}; static int g_n = 0, g_estaveis = 0, g_segChave = 0, g_tick = 0;
static int g_hTaxa[32] = {}; static double g_hPeso[32] = {}; static int g_hN = 0, g_hSessoes = 0;   // histograma das sessoes anteriores (lido no inicio da chave)
static std::wstring g_driver;
static std::wstring Driver() {
    if (!g_driver.empty()) return g_driver;
    IDXGIAdapter1* a = ObterAdaptadorEscolhido();
    if (!a) return L"";
    LARGE_INTEGER v{};
    if (SUCCEEDED(a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &v))) {
        wchar_t b[48]; _snwprintf(b, 48, L"%u.%u.%u.%u", (unsigned)HIWORD((DWORD)v.HighPart), (unsigned)LOWORD((DWORD)v.HighPart),
                                  (unsigned)HIWORD(v.LowPart), (unsigned)LOWORD(v.LowPart)); b[47] = 0;
        g_driver = b;
    } else g_driver = L"desconhecido";
    a->Release();
    return g_driver;
}
// video / jogo / desktop, pela fonte em vigor (ResetMem::g_vigor.fonte: "janela:<exe>", "monitor:<rotulo>", "camera:<disp>")
static const wchar_t* TipoFonte() {
    const std::wstring& f = ResetMem::g_vigor.fonte;
    if (f.rfind(L"monitor:", 0) == 0) return L"desktop";
    if (f.rfind(L"camera:", 0) == 0) return L"video";
    if (f.rfind(L"janela:", 0) == 0) {
        const std::wstring e = f.substr(7);
        static const wchar_t* kVideo[] = { L"msedge.exe", L"chrome.exe", L"firefox.exe", L"brave.exe", L"opera.exe", L"opera_gx.exe", L"vivaldi.exe",
                                           L"vlc.exe", L"mpc-hc64.exe", L"mpc-hc.exe", L"mpc-be64.exe", L"mpc-be.exe", L"mpv.exe", L"potplayermini64.exe",
                                           L"potplayermini.exe", L"wmplayer.exe", L"video.ui.exe", L"microsoft.media.player.exe", L"kodi.exe", L"plex.exe" };
        for (const wchar_t* x : kVideo) if (e == x) return L"video";
        return L"jogo";
    }
    return nullptr;
}
static int ClasseFps(double f) { return f <= 45.0 ? 30 : f <= 90.0 ? 60 : f <= 180.0 ? 120 : 240; }
static std::wstring ChaveAtual() {
    if (g_placaIdx < 0 || g_placaIdx >= (int)g_placas.size() || !Calib::g_w || Calib::g_escRodando.empty() || Esteira::g_fonteLivreFps < 5.0) return L"";
    const wchar_t* tipo = TipoFonte(); if (!tipo) return L"";
    const std::wstring drv = Driver(); if (drv.empty()) return L"";
    std::wstring k = Calib::PrefixoPlaca(g_placas[g_placaIdx].nome, g_placas[g_placaIdx].vendor, g_placas[g_placaIdx].device) +
                     L"_drv" + drv + L"_" + std::to_wstring(Calib::g_w) + L"x" + std::to_wstring(Calib::g_h) + L"_p";
    for (size_t i = 0; i < Calib::g_escRodando.size(); i++) k += (i ? L"-" : L"") + std::to_wstring(Calib::g_escRodando[i]);
    k += L"_" + std::wstring(tipo) + L"_c" + std::to_wstring(ClasseFps(Esteira::g_fonteLivreFps));
    return k;
}
// "hist=240:0.550,60:0.450;..." -> g_hTaxa/g_hPeso
static void LerHist(const std::wstring& linha) {
    g_hN = 0; g_hSessoes = (int)Calib::Campo(linha, L"sessoes", 0.0);
    const std::wstring h = Calib::CampoTxt(linha, L"hist");
    size_t p = 0;
    while (p < h.size() && g_hN < 32) {
        const size_t f = h.find(L',', p);
        const std::wstring par = h.substr(p, f == std::wstring::npos ? std::wstring::npos : f - p);
        const size_t c = par.find(L':');
        if (c != std::wstring::npos) { g_hTaxa[g_hN] = _wtoi(par.c_str()); g_hPeso[g_hN] = _wtof(par.c_str() + c + 1); if (g_hPeso[g_hN] > 0.0) g_hN++; }
        if (f == std::wstring::npos) break;
        p = f + 1;
    }
}
static void Gravar(const char* motivo) {
    if (g_chave.empty() || g_estaveis <= 0) return;
    if (g_estaveis < (int)(Esteira::g_ctl.memMinS + 0.5)) {
        if (strcmp(motivo, "a cada 60 s") != 0)
            Log("guarda: memoria da configuracao (%s) - NAO grava: so %d s estaveis nesta sessao (< %.0f s; a sessao curta nao reescreve a memoria)", motivo, g_estaveis, Esteira::g_ctl.memMinS);
        return;
    }
    // mistura: nova x alfa + anterior x (1 - alfa); sem anterior, a nova inteira
    int t[96]; double w[96]; int m = 0;
    const double a = g_hN > 0 ? Esteira::g_ctl.memAlfa : 1.0;
    for (int i = 0; i < g_hN; i++) { t[m] = g_hTaxa[i]; w[m] = (1.0 - a) * g_hPeso[i]; m++; }
    for (int i = 0; i < g_n; i++) {
        if (g_seg[i] <= 0) continue;
        const double wn = a * (double)g_seg[i] / (double)g_estaveis;
        int j = 0; while (j < m && t[j] != g_taxa[i]) j++;
        if (j == m) { t[m] = g_taxa[i]; w[m] = 0.0; m++; }
        w[j] += wn;
    }
    int mo = 0; for (int i = 1; i < m; i++) if (w[i] > w[mo]) mo = i;
    std::wstring hist;
    for (int i = 0; i < m; i++) { if (w[i] < 0.0005) continue; wchar_t b[32]; _snwprintf(b, 32, L"%s%d:%.3f", hist.empty() ? L"" : L",", t[i], w[i]); b[31] = 0; hist += b; }
    wchar_t v[600];
    _snwprintf(v, 600, L"fps=%d;hist=%ls;estaveis=%d;sessoes=%d;data=%ls", t[mo], hist.c_str(), g_estaveis, g_hSessoes + 1, Calib::AgoraTexto().c_str()); v[599] = 0;
    WritePrivateProfileStringW(L"taxa_estavel_v3", g_chave.c_str(), v, IniPath().c_str());
    Log("guarda: memoria da configuracao (%s) - [taxa_estavel] %s: moda %d/s%s, hist %s (%d s estaveis nesta sessao, mistura %.2f nova; a menor desta sessao, so log: %.0f/s)",
        motivo, U8(g_chave).c_str(), t[mo], t[mo] == 0 ? " (livre, sem guarda)" : "", U8(hist).c_str(), g_estaveis, a, Esteira::g_taxaMin);
}
static void Tick() {
    const std::wstring k = ChaveAtual();
    if (k.empty()) return;
    if (k != g_chave) {
        if (k != g_pend) { g_pend = k; g_pendSeg = 0; }
        if (!g_chave.empty() && ++g_pendSeg < 5) return;   // a chave nova tem de ficar 5 s (fonte oscilando nao zera a sessao)
        Gravar("configuracao/fonte mudou");
        g_chave = k; g_n = g_estaveis = g_segChave = 0; g_pend.clear();
        Esteira::ResetarSessaoTaxa("configuracao/fonte mudou");
        wchar_t b[600]{}; GetPrivateProfileStringW(L"taxa_estavel_v3", k.c_str(), L"", b, 600, IniPath().c_str());
        const std::wstring linha = b;
        LerHist(linha);
        int fps = 0;
        if (g_hN > 0) { int mo = 0; for (int i = 1; i < g_hN; i++) if (g_hPeso[i] > g_hPeso[mo]) mo = i; fps = g_hTaxa[mo]; }
        if (fps > 0) { Esteira::g_pedeTaxa = fps; Log("guarda: memoria da configuracao %s: moda dos segundos estaveis = %d/s (%d sessao(oes)) - comeca nela e sobe pela regra", U8(k).c_str(), fps, g_hSessoes); }
        else Log("guarda: memoria da configuracao %s: %ls", U8(k).c_str(), g_hN > 0 ? L"livre (sem guarda) na maior parte do tempo estavel" : linha.empty() ? L"nova (sem memoria)" : L"formato antigo (sec. 23) ou vazia: comeca livre");
    } else g_pend.clear();
    ++g_segChave;
    if (g_segChave > (int)(Esteira::g_ctl.memIniS + 0.5) && Esteira::g_estavelAgora) {
        const int t = (int)(Esteira::g_taxaAgora + 0.5);
        int i = 0; while (i < g_n && g_taxa[i] != t) i++;
        if (i == g_n && g_n < 64) { g_taxa[g_n] = t; g_seg[g_n] = 0; g_n++; }
        if (i < 64) { g_seg[i]++; g_estaveis++; }
    }
    if (++g_tick % 60 == 0) Gravar("a cada 60 s");
}
}   // namespace TaxaMem
