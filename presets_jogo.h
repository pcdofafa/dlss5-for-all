// =====================================================================================
//  PRESETS POR JOGO/PROGRAMA (Rodada 26, v8, 25/09/2026 - decisao do dono, item 8)
// =====================================================================================
//  Incluido por main.cpp DEPOIS do primeiro bloco do namespace Hud (usa g_hud, IniSet, o
//  perfil por jogo e as funcoes do cfg do feed, que ja existem ali) e ANTES do painel.
//
//  O QUE E: tres slots por jogo/programa, no rodape do menu End ("1 · 2 · 3", Salvar,
//  Carregar). Cada slot e uma FOTO da configuracao inteira do app - as alavancas do perfil
//  por jogo (kPerfilChaves, que ja trazem os filtros um a um), o ajuste fino da rede
//  (kPerfilExtras: modelo, intensidade, estrutura, pele, tons, mascara e as listas por
//  passe), as escalas por passe, a linha dos filtros (Filtros::Serializar), a exposicao
//  adaptativa e as escolhas da sessao em kGlobais (ritmo, limite de fps, folga de GPU,
//  captura, ponteiro, fps na tela).
//
//  QUEM E O DONO: o exe do jogo ([perfil:<exe>], o mesmo do "Salvar para este jogo"). Fonte
//  sem exe (monitor, camera, janela sem processo legivel) tem um dono fixo por tipo de fonte,
//  para os tres slots existirem tambem ali.
//
//  ONDE MORA, no mesmo dlss5forall.ini:
//    [preset:<dono>]      ativo = 1..3   (o ultimo salvo/carregado; ausente = "padrao")
//    [preset:<dono>#N]    as chaves da foto + filtros=... + cfg_exposicao_adaptativa + salvo_em
//
//  O ATIVO CARREGA SOZINHO QUANDO O JOGO ABRE, e pelo caminho que ja existia: salvar um slot
//  tambem tira a foto inteira para [perfil:<exe>] (PerfilSalvarTudo, o antigo "Salvar para
//  este jogo") - e e o [perfil:<exe>] que o arranque aplica por cima do [ultima]. Carregar um
//  slot grava o slot no [ultima] e no perfil e reabre o app (menos quando so os filtros mudam:
//  esses valem na hora). O perfil que ja existia vira o slot 1 na primeira abertura do jogo
//  com esta versao (Migrar).
namespace PresetJogo {

// As escolhas que valem para a sessao inteira e NAO entram no perfil por jogo (sao do PC),
// mas entram no preset: o dono pediu "a configuracao inteira do app" no slot.
static const wchar_t* const kGlobais[] = {
    L"ritmo", L"fps_teto_pelicula", L"fps_teto_anterior", L"esteira_guarda",
    L"captura", L"ponteiro", L"fps_tela",
};
static const int kNGlobais = (int)(sizeof(kGlobais) / sizeof(kGlobais[0]));

template <class F> static void ParaCada(F fn) {
    for (int i = 0; i < kNPerfilTodas; i++) fn(ChavePerfilN(i));
    for (int i = 0; i < kNGlobais; i++) fn(kGlobais[i]);
}
// A verdade destas mora no dlss5-feed.cfg (o feed le de la; o [ultima] so semeia): as listas
// por passe (`<controle>_passe`) e a das escalas (`nr_escala_passe`).
static bool ChaveDoCfg(const wchar_t* k) {
    const size_t n = wcslen(k);
    return n > 6 && wcscmp(k + n - 6, L"_passe") == 0;
}
static bool EhFiltro(const wchar_t* k) {
    return wcsncmp(k, L"filtro_", 7) == 0 || wcscmp(k, L"filtros_ligados") == 0;
}

static std::wstring Dono() {
    if (!g_perfilExe.empty()) return g_perfilExe;
    if (g_hud.camera) return L"fonte-camera";
    return g_hud.fonteJanela ? L"fonte-janela" : L"fonte-monitor";
}
static std::wstring SecaoBase()  { return L"preset:" + Dono(); }
static std::wstring Secao(int n) { return SecaoBase() + L"#" + std::to_wstring(n); }

// Um cache de uma entrada: o painel pergunta "qual o ativo / qual slot tem foto" a cada
// pintura, e isso seriam quatro leituras de ini por desenho.
struct Cache { std::wstring dono; int ativo = 0; bool tem[4] = { false, false, false, false }; bool ok = false; };
static Cache g_c;
static void Reler() {
    g_c.dono  = Dono();
    g_c.ativo = (int)GetPrivateProfileIntW(SecaoBase().c_str(), L"ativo", 0, IniPath().c_str());
    if (g_c.ativo < 0 || g_c.ativo > 3) g_c.ativo = 0;
    for (int n = 1; n <= 3; n++) {
        wchar_t b[8]{};
        GetPrivateProfileStringW(Secao(n).c_str(), L"salvo_em", L"", b, 8, IniPath().c_str());
        g_c.tem[n] = (b[0] != 0);
    }
    g_c.ok = true;
}
static void Garantir() { if (!g_c.ok || g_c.dono != Dono()) Reler(); }
static int  Ativo()      { Garantir(); return g_c.ativo; }
static bool Tem(int n)   { Garantir(); return n >= 1 && n <= 3 && g_c.tem[n]; }
static void DefinirAtivo(int n) {
    WritePrivateProfileStringW(SecaoBase().c_str(), L"ativo", n > 0 ? std::to_wstring(n).c_str() : nullptr, IniPath().c_str());
    g_c.ok = false;
}
// O nome que o rodape e o cabecalho mostram: o titulo da fonte (o jogo), ou o exe.
static std::wstring NomeDoDono() {
    if (!g_hud.src.empty()) return g_hud.src;
    if (!g_perfilExe.empty()) return g_perfilExe;
    return TS(L"esta fonte", L"this source");
}

static std::wstring Largo(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // so ASCII (numeros e virgulas)

// SALVAR: a foto de agora vai para o slot N (refeito do zero - sem sobra do anterior), o
// slot vira o ativo e, com um exe na frente, a mesma foto vai para [perfil:<exe>] - que e o
// que o arranque aplica quando o jogo abre (o antigo "Salvar para este jogo").
static int Salvar(int n, bool fotoNoPerfil) {
    if (n < 1 || n > 3) return -1;
    const std::wstring sec = Secao(n), ini = IniPath(), cfg = g_exeDir + L"dlss5-feed.cfg";
    WritePrivateProfileStringW(sec.c_str(), nullptr, nullptr, ini.c_str());
    int q = 0;
    ParaCada([&](const wchar_t* k) {
        std::wstring v; bool tem = false;
        if (ChaveDoCfg(k)) {
            const std::string c = LerChaveCfgTexto(cfg, U8(k).c_str());
            if (!c.empty()) { v = Largo(c); tem = true; }
        }
        if (!tem) {
            wchar_t b[512];
            GetPrivateProfileStringW(L"ultima", k, L"\x01", b, 512, ini.c_str());
            if (!(b[0] == L'\x01' && b[1] == 0)) { v = b; tem = true; }
        }
        if (tem) { WritePrivateProfileStringW(sec.c_str(), k, v.c_str(), ini.c_str()); q++; }
    });
    WritePrivateProfileStringW(sec.c_str(), L"filtros", Filtros::Serializar().c_str(), ini.c_str());
    WritePrivateProfileStringW(sec.c_str(), L"cfg_exposicao_adaptativa",
                               LerChaveCfgInt(cfg, "exposicao_adaptativa", 0) != 0 ? L"1" : L"0", ini.c_str());
    SYSTEMTIME st{}; GetLocalTime(&st);
    wchar_t quando[32];
    _snwprintf(quando, 32, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute); quando[31] = 0;
    WritePrivateProfileStringW(sec.c_str(), L"salvo_em", quando, ini.c_str());
    DefinirAtivo(n);
    if (fotoNoPerfil && !g_perfilExe.empty()) {
        const int np = PerfilSalvarTudo();
        g_hud.perfilChaves = np > 0 ? np : 0;
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini.c_str());   // descarrega o cache do ini
    g_c.ok = false;
    Log("preset: slot %d de [%s] salvo (%d chaves + filtros + exposicao adaptativa)%s", n, U8(Dono()).c_str(), q,
        (fotoNoPerfil && !g_perfilExe.empty()) ? " - e a mesma foto em [perfil:<exe>], que o arranque aplica quando o jogo abre" : "");
    return q;
}

// CARREGAR: o slot N volta para o [ultima] (e, por IniSet, para o perfil do jogo); as listas
// do feed vao tambem para o cfg; os filtros e a exposicao adaptativa valem NA HORA. Devolve
// quantas chaves mudaram; `*precisaReabrir` diz se mudou alguma coisa alem dos filtros - o
// resto so vale quando o app reabre (o add-on le o perfil ao carregar, e varias chaves sao
// lidas so no arranque), e quem reabre e o laco principal.
static int Carregar(int n, bool* precisaReabrir, int* puloN, int* alternaN) {
    if (precisaReabrir) *precisaReabrir = false;
    if (!Tem(n)) return -1;
    const std::wstring sec = Secao(n), ini = IniPath(), cfg = g_exeDir + L"dlss5-feed.cfg";
    int mud = 0; bool outro = false;
    ParaCada([&](const wchar_t* k) {
        wchar_t b[512], a[512];
        GetPrivateProfileStringW(sec.c_str(), k, L"\x01", b, 512, ini.c_str());
        GetPrivateProfileStringW(L"ultima", k, L"\x01", a, 512, ini.c_str());
        const bool ausente = (b[0] == L'\x01' && b[1] == 0);
        const bool ausenteA = (a[0] == L'\x01' && a[1] == 0);
        const std::wstring novo = ausente ? std::wstring() : std::wstring(b);
        std::wstring atual = ausenteA ? std::wstring() : std::wstring(a);
        if (ChaveDoCfg(k)) {
            const std::string c = LerChaveCfgTexto(cfg, U8(k).c_str());
            if (!c.empty()) atual = Largo(c);
        }
        if (novo == atual) return;   // ausente e vazio contam igual: nos dois o arranque usa o padrao
        mud++;
        if (!EhFiltro(k)) outro = true;
        if (ausente) {
            WritePrivateProfileStringW(L"ultima", k, nullptr, ini.c_str());
            if (!g_perfilExe.empty() && ChaveDePerfil(k))
                WritePrivateProfileStringW((L"perfil:" + g_perfilExe).c_str(), k, nullptr, ini.c_str());
        } else {
            IniSet(k, novo);
        }
        if (ChaveDoCfg(k)) {
            GravarChaveCfg(cfg, U8(k).c_str(), U8(novo).c_str());
            if (wcscmp(k, L"nr_escala_passe") == 0) {
                // as duas chaves antigas saem coerentes com o passe 1, como em GravarEscalasPasseNoCfg
                const int p1 = novo.empty() ? 100 : _wtoi(novo.c_str());
                bool reduz = false;
                for (size_t i = 0; i < novo.size(); ) {
                    if (_wtoi(novo.c_str() + i) != 100) reduz = true;
                    const size_t c = novo.find(L',', i);
                    if (c == std::wstring::npos) break;
                    i = c + 1;
                }
                char v[16]; snprintf(v, sizeof(v), "%d", p1 > 0 ? p1 : 100);
                GravarChaveCfg(cfg, "nr_escala", v);
                GravarChaveCfg(cfg, "nr_reduzida", reduz ? "1" : "0");
            }
        }
        // a nitidez da barra vale qualquer centesimo, e o arranque so reaplica os degraus: vai direto ao cfg
        if (wcscmp(k, L"nitidez_ia") == 0 && !ausente) {
            char v[16]; snprintf(v, sizeof(v), "%.2f", _wtoi(novo.c_str()) / 100.0);
            GravarChaveCfg(cfg, "work_sharpness", v);
        }
    });
    // OS FILTROS, na hora: as chaves filtro_* ja foram escritas acima; a linha serializada do
    // slot e aplicada por cima (Desserializar grava e manda para o ReShade), e o interruptor
    // mestre e relido.
    {
        wchar_t f[2048]{};
        GetPrivateProfileStringW(sec.c_str(), L"filtros", L"", f, 2048, ini.c_str());
        Filtros::Recarregar();
        if (f[0]) Filtros::Desserializar(f); else Filtros::AplicarTodos();
        Filtros::RelerLigado();
    }
    {
        wchar_t e[8]{};
        GetPrivateProfileStringW(sec.c_str(), L"cfg_exposicao_adaptativa", L"", e, 8, ini.c_str());
        if (e[0]) {
            const int quer = (e[0] == L'1') ? 1 : 0;
            if (LerChaveCfgInt(cfg, "exposicao_adaptativa", 0) != quer) {
                GravarChaveCfg(cfg, "exposicao_adaptativa", quer ? "1" : "0");
                mud++;
            }
            Hud::g_expoAdapt = quer;
        }
    }
    if (puloN)    { const int v = _wtoi(IniGet(L"pulo_quadros").c_str());    *puloN    = (v >= 1 && v <= 4) ? v : 1; }
    if (alternaN) { const int v = _wtoi(IniGet(L"alterna_quadros").c_str()); *alternaN = (v >= 1 && v <= 4) ? v : 1; }
    DefinirAtivo(n);
    if (!g_perfilExe.empty()) { const int np = PerfilSalvarTudo(); g_hud.perfilChaves = np > 0 ? np : 0; }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini.c_str());
    g_c.ok = false;
    if (precisaReabrir) *precisaReabrir = outro;
    Log("preset: slot %d de [%s] carregado - %d chave(s) mudaram%s", n, U8(Dono()).c_str(), mud,
        outro ? " (alem dos filtros: vale ao reabrir)" : " (so filtros/exposicao: valem na hora)");
    return mud;
}

// VOLTAR AO PADRAO: tudo o que um preset guarda volta ao padrao de fabrica - as chaves saem
// do [ultima] e do perfil do jogo (o arranque usa o padrao de cada uma quando ela falta), as
// listas do feed sao limpas no cfg, os filtros vao para o padrao de fabrica da janela deles
// e nenhum slot fica ativo. Os TRES SLOTS FICAM (sao escolha guardada da pessoa), e as marcas
// do jogo (nativo_falhas, upstream_ok...) tambem - sao historico, nao ajuste.
static int VoltarAoPadrao() {
    const std::wstring ini = IniPath(), cfg = g_exeDir + L"dlss5-feed.cfg";
    int n = 0;
    ParaCada([&](const wchar_t* k) {
        if (EhFiltro(k)) return;   // os filtros vao pelo padrao de fabrica deles, logo abaixo
        wchar_t a[8];
        GetPrivateProfileStringW(L"ultima", k, L"\x01", a, 8, ini.c_str());
        if (!(a[0] == L'\x01' && a[1] == 0)) { WritePrivateProfileStringW(L"ultima", k, nullptr, ini.c_str()); n++; }
        if (!g_perfilExe.empty() && ChaveDePerfil(k))
            WritePrivateProfileStringW((L"perfil:" + g_perfilExe).c_str(), k, nullptr, ini.c_str());
        if (ChaveDoCfg(k)) GravarChaveCfg(cfg, U8(k).c_str(), "");
    });
    GravarChaveCfg(cfg, "nr_escala", "100");
    GravarChaveCfg(cfg, "nr_reduzida", "0");
    GravarChaveCfg(cfg, "exposicao_adaptativa", "0");
    Hud::g_expoAdapt = 0;
    IniSet(L"filtros_ligados", L"1");
    Filtros::PadraoDeFabrica();
    Filtros::RelerLigado();
    DefinirAtivo(0);
    if (!g_perfilExe.empty()) { PerfilApagar(); g_hud.perfilChaves = 0; }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini.c_str());
    g_c.ok = false;
    Log("preset: 'Voltar ao padrao' - %d chave(s) de [%s] voltaram ao padrao de fabrica (os 3 slots ficam guardados)", n, U8(Dono()).c_str());
    return n;
}

// MIGRACAO (item 8): o perfil que o "Salvar para este jogo" ja tinha gravado vira o slot 1,
// ativo. Uma vez so por jogo: com qualquer slot ou "ativo" gravado, nao faz nada. Chamado
// pelo arranque logo depois de o perfil ser aplicado por cima do [ultima] - a foto e, entao,
// exatamente o que o perfil trazia (mais o resto do [ultima] da sessao).
static void Migrar(int nPerfil) {
    if (g_perfilExe.empty() || nPerfil <= 0) return;
    Reler();
    if (g_c.ativo > 0 || g_c.tem[1] || g_c.tem[2] || g_c.tem[3]) return;
    const int q = Salvar(1, false);
    Log("preset: o perfil salvo de %s (%d chaves) virou o preset 1 (ativo) - migracao da v7 para os 3 slots", U8(g_perfilExe).c_str(), nPerfil);
    (void)q;
}

}  // namespace PresetJogo
