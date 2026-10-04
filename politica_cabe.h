// politica_cabe.h - AGENTE D do PLANO-FLUIDEZ-2309, item 4: "QUANDO NAO CABE".
//
// A regra do dono: nenhum quadro do jogo descartado, nenhum quadro cru, nenhum passe cortado. Quando a
// fonte (o jogo) entrega mais quadros por segundo do que a rede consegue processar dividindo a placa com
// ele, sobram so duas saidas honestas:
//   - MODO JOGO / com a peca do limitador viva no jogo (src\limitador\, motor "ao vivo"): o app LIMITA O
//     JOGO no maior divisor do refresh que deixe ~15% de folga para a rede (PESQ-GPU-CONVIVENCIA §3c/§5.4),
//     com histerese. Painel: "jogo limitado a X fps (rede)".
//   - MODO MONITOR sem limitador utilizavel: nao ha o que limitar sem injetar nada. Painel: "fonte a X fps,
//     rede sustenta Y - limite o jogo/fonte a Z" e a latencia da fila. Nada e descartado (a captura em
//     ordem do Agente C faz o DWM esperar; o excedente nunca vira quadro nosso).
//
// DE ONDE SAI A "TAXA SUSTENTAVEL":
//   - monitor: a linha `captura FILA` (screen_capture.h, janela de 5 s). Com a fila no teto e excedentes,
//     `entregues/s` E a taxa que a rede sustenta com o jogo dividindo a placa (medida, nao estimada);
//   - custo: `gpu_pacote_ms` do dlss5-feed.status (tempo de GPU do nosso pacote = todos os passes do
//     perfil + copias). 1000/custo e um TETO otimista (nao conta o jogo na mesma placa: na bancada 185
//     contra 148 medidos pela fila). Serve para o modo jogo (sem fila de captura) e para decidir SUBIR.
//
// O limitador so aceita os valores do contrato da peca (fpslimit_shared.h: 30, 45, 60, 120). Sem mudar a
// peca (ela mora na pasta do jogo e e outra versao), o "maior divisor do refresh" e escolhido entre eles;
// se nenhum for divisor do refresh (144, 165 Hz...), vale o maior permitido que caiba, e o log diz que nao
// e divisor (a cadencia AUTO do apresentador cobre a razao nao inteira).
//
// Logica pura (sem Win32 alem de tipos), chamada 1x/s do laco principal (main.cpp). O main aplica o pedido.
#pragma once
#include <cmath>
#include <cstdio>
#include <string>

namespace PoliticaCabe {

static const int    kPermitidos[] = { 120, 60, 45, 30 };   // do maior para o menor (= FpsLimit::kLimitesPermitidos sem o 0)
static const double kFolga        = 0.85;                  // ~15% de folga
static const int    kSegParaDescer = 6;                    // "nao cabe" por 6 s seguidos (= 2 janelas de 5 s da fila) -> limita / desce um degrau
static const int    kSegAvisoSome  = 10;                   // aviso (sem agir) some depois de 10 s cabendo
static const int    kSubidaBaseS   = 20;                   // subir um degrau exige 20 s com folga confirmada...
static const int    kSubidaMaxS    = 160;                  // ...e dobra (ate 160 s) se recair em <= 30 s depois de subir

struct Entrada {
    double refreshHz   = 0.0;   // tela
    double fonteFps    = 0.0;   // o que chegou no ultimo segundo (o numero que o painel mostra)
    bool   filaValida  = false; // houve janela de 5 s da fila de captura nos ultimos ~7 s
    bool   filaAviso   = false; // a fila bateu no teto com excedentes (a fonte passou da rede)
    double filaEntregues = 0.0; // entregues/s nessa janela (= taxa sustentavel quando filaAviso)
    double filaExced   = 0.0;   // excedentes estimados/s
    double filaP95Ms   = 0.0;   // idade do quadro na fila, p95
    double custoMs     = 0.0;   // gpu_pacote_ms do feed (<= 0 = nao publicado)
    bool   nativo      = false; // modo jogo (DLL injetada / ponte)
    bool   motorVivo   = false; // a peca do limitador esta no jogo e respondendo (MOTOR_AO_VIVO)
    bool   ligado      = true;  // [ultima] limitar_auto
    int    limitePessoa = 0;    // o limite que a PESSOA aplicou na pagina Desempenho (0 = nenhum)
};

struct Estado {
    int  limite       = 0;      // o que NOS pedimos agora (0 = nada nosso)
    int  segNaoCabe   = 0, segCabe = 0, segFolgaAcima = 0;
    int  subidaHoldS  = kSubidaBaseS;
    long long msUltSubida = -1;
    bool avisoVisivel = false;
    double ultSust = 0.0, ultFonte = 0.0, ultP95 = 0.0;
    int  ultRecomendado = 0;
};

// Divisor inteiro do refresh (tolerancia 1%)?
inline bool Divisor(double refresh, int v) {
    if (refresh <= 1.0 || v <= 0) return false;
    const double r = refresh / (double)v;
    return std::fabs(r - std::floor(r + 0.5)) <= 0.01 * r;
}
// O maior valor permitido <= teto, preferindo divisor do refresh. `abaixoDe` > 0 restringe a < abaixoDe.
inline int Alvo(double refresh, double teto, int abaixoDe, bool* ehDivisor) {
    if (ehDivisor) *ehDivisor = false;
    for (int v : kPermitidos) {
        if ((double)v > teto * 1.02) continue;   // "~15%": 120 com 14,3% de folga ainda vale
        if (abaixoDe > 0 && v >= abaixoDe) continue;
        if (Divisor(refresh, v)) { if (ehDivisor) *ehDivisor = true; return v; }
    }
    for (int v : kPermitidos) {
        if ((double)v > teto * 1.02) continue;   // "~15%": 120 com 14,3% de folga ainda vale
        if (abaixoDe > 0 && v >= abaixoDe) continue;
        return v;
    }
    // nem o menor cabe com folga: o menor que ainda esteja abaixo do atual (limitar a 30 ainda e melhor do que nao limitar)
    const int menor = kPermitidos[sizeof kPermitidos / sizeof kPermitidos[0] - 1];
    if (abaixoDe > 0 && menor >= abaixoDe) return 0;
    return menor;
}
// O degrau acima de `atual` (0 = soltar). O topo so solta quando o custo sustenta o refresh com folga.
inline int Acima(int atual) {
    int ant = 0;
    for (int v : kPermitidos) { if (v == atual) return ant; ant = v; }
    return 0;
}

// A taxa sustentavel que o painel/decisao usam: a fila (medida) quando ela bateu no teto; senao o teto do custo.
inline double Sustentavel(const Entrada& e) {
    if (e.filaValida && e.filaAviso && e.filaEntregues > 1.0) return e.filaEntregues;
    if (e.custoMs > 0.05) return 1000.0 / e.custoMs;
    return 0.0;
}

// Um passo por segundo. Devolve o que pedir a peca: -1 = nao mexer; 0 = soltar (volta ao limite da pessoa);
// > 0 = pedir esse limite. `log` recebe no maximo uma linha por decisao. `agoraMs` e o relogio do laco.
inline int Passo(Estado& s, const Entrada& e, long long agoraMs, std::string* log) {
    if (log) log->clear();
    const double S = Sustentavel(e);
    const double Scusto = (e.custoMs > 0.05) ? 1000.0 / e.custoMs : 0.0;
    s.ultSust = S; s.ultFonte = e.fonteFps; s.ultP95 = e.filaP95Ms;

    // NAO CABE:
    //  - com fila de captura: ela bateu no teto com excedentes (medido);
    //  - modo jogo (sem fila): a fonte ja passa de 85% do que o custo sustenta e ainda esta acima do nosso limite.
    bool naoCabe = false;
    // fila: so conta com >= 1 excedente/s (a janela de abertura bate no teto por 10 voltas com ~0,6/s - medido, rodada9 p1b)
    if (e.filaValida) naoCabe = e.filaAviso && e.filaExced >= 1.0;
    else if (e.nativo && Scusto > 0.0) naoCabe = e.fonteFps > kFolga * Scusto + 1.0 && (s.limite == 0 || e.fonteFps > s.limite + 2.0);
    if (naoCabe) { s.segNaoCabe++; s.segCabe = 0; } else { s.segCabe++; s.segNaoCabe = 0; }

    // Recomendacao (para o aviso e para agir): o maior permitido <= 85% do sustentavel, abaixo do limite atual se houver.
    bool div = false;
    const int rec = (S > 0.0) ? Alvo(e.refreshHz, kFolga * S, 0, &div) : 0;
    s.ultRecomendado = rec;

    const bool podeAgir = e.ligado && e.motorVivo;
    int pedir = -1;
    char b[400];

    if (!podeAgir) {
        // Sem limitador utilizavel (modo monitor sem peca, ou a chave desligada): so o aviso, com histerese.
        if (s.limite != 0) {   // a chave foi desligada / a peca sumiu com um limite nosso de pe: soltar
            snprintf(b, sizeof b, "quando nao cabe: limitador indisponivel/desligado - soltando o limite nosso de %d fps", s.limite);
            if (log) *log = b;
            s.limite = 0; pedir = 0;
        }
        if (!s.avisoVisivel && s.segNaoCabe >= kSegParaDescer) {
            s.avisoVisivel = true;
            snprintf(b, sizeof b, "quando nao cabe: AVISO no painel - fonte a %.0f fps, a rede sustenta ~%.0f (fila p95 %.1f ms, %.1f excedentes/s); recomendado limitar a fonte a %d fps%s. %s",
                     e.fonteFps + e.filaExced, S, e.filaP95Ms, e.filaExced, rec, div ? " (divisor do refresh)" : " (nao e divisor do refresh)",
                     e.ligado ? "Sem limitador utilizavel neste modo: nada e descartado, o excedente fica com a fonte" : "[ultima] limitar_auto=0: so o aviso");
            if (log) *log = b;
        } else if (s.avisoVisivel && s.segCabe >= kSegAvisoSome) {
            s.avisoVisivel = false;
            if (log) *log = "quando nao cabe: a fonte voltou a caber na rede ha 10 s - aviso retirado";
        }
        return pedir;
    }

    // COM limitador vivo.
    s.avisoVisivel = false;
    if (s.segNaoCabe >= kSegParaDescer) {
        const int novo = (S > 0.0) ? Alvo(e.refreshHz, kFolga * S, s.limite, &div) : 0;
        if (novo > 0 && (s.limite == 0 || novo < s.limite) && (s.limite != 0 || (double)novo < e.fonteFps + e.filaExced)) {
            // recaida logo depois de subir: a espera para a proxima subida dobra
            if (s.msUltSubida >= 0 && agoraMs - s.msUltSubida <= 30000) s.subidaHoldS = (s.subidaHoldS * 2 > kSubidaMaxS) ? kSubidaMaxS : s.subidaHoldS * 2;
            snprintf(b, sizeof b, "quando nao cabe: fonte %.0f fps, rede sustenta ~%.0f (%s) -> JOGO LIMITADO a %d fps (%s de %.0f Hz, folga %.0f%%)%s | proxima subida so apos %d s com folga",
                     e.fonteFps + e.filaExced, S, (e.filaValida && e.filaAviso) ? "fila de captura no teto" : "custo do pacote",
                     novo, div ? "divisor" : "NAO divisor", e.refreshHz, (1.0 - (double)novo / S) * 100.0,
                     s.limite ? " (desceu um degrau)" : "", s.subidaHoldS);
            if (log) *log = b;
            s.limite = novo; pedir = novo; s.segNaoCabe = 0; s.segFolgaAcima = 0;
        }
        return pedir;
    }
    if (s.limite > 0) {
        // Subir um degrau so com folga CONFIRMADA pelo custo por `subidaHoldS` segundos e sem a fila no teto.
        const int acima = Acima(s.limite);
        const bool folga = Scusto > 0.0 && !(e.filaValida && e.filaAviso) &&
                           (acima > 0 ? (double)acima <= kFolga * Scusto : e.refreshHz > 1.0 && e.refreshHz <= kFolga * Scusto);
        s.segFolgaAcima = folga ? s.segFolgaAcima + 1 : 0;
        if (s.segFolgaAcima >= s.subidaHoldS) {
            snprintf(b, sizeof b, "quando nao cabe: %d s com folga (custo %.2f ms -> teto %.0f fps) - limite %d -> %s",
                     s.segFolgaAcima, e.custoMs, Scusto, s.limite, acima > 0 ? std::to_string(acima).c_str() : "SOLTO");
            if (log) *log = b;
            s.limite = acima; pedir = acima; s.segFolgaAcima = 0; s.msUltSubida = agoraMs;
        }
    }
    return pedir;
}

} // namespace PoliticaCabe
