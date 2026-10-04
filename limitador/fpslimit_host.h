// =====================================================================================
//  fpslimit_host.h - o lado do APP: instalar, pulsar, ler o estado, remover.
//
//  Contraparte de fpslimit_dll.cpp. Header-only, no estilo do desempenho.h, e nao depende de
//  nada do main.cpp: o que precisa do app (onde estao as pecas, onde gravar o registro do que
//  foi instalado, como escrever no log) chega por parametro.
//
//  A REGRA DE OURO, a mesma dos dois lados: nada aqui pode impedir o jogo de abrir, e nada
//  aqui sobrescreve arquivo de outra pessoa. Toda falha termina em "nao instalou, e explica".
//
//  O QUE FICA NA MAQUINA DE QUEM USA, porque isto merece ser dito com todas as letras: dois
//  arquivos na pasta do jogo. Eles sao INERTES sem o app rodando - a peca nao encontra a
//  memoria compartilhada, nao engancha nada e o jogo roda como se ela nao existisse. Mesmo
//  esquecidos para sempre, nao fazem nada. Ainda assim sao rastreados no nosso ini e removiveis
//  por um botao, pelo desinstalador, ou a mao com a ajuda do LEIA-ME que vai junto.
// =====================================================================================
#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include "fpslimit_shared.h"

namespace FpsLimit {

// O host tambem escreve no log do app; o ponteiro e ligado pelo main.cpp.
static void (*g_logHost)(const char*) = nullptr;
static void LogH(const char* fmt, ...) {
    char b[1024]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (g_logHost) g_logHost(b);
}

static const wchar_t* kNomeCasca = L"dxgi.dll";
static const wchar_t* kNomePeca  = L"dlss5-fpslimit.dll";
static const wchar_t* kNomeLeiaMe = L"LEIA-ME-dlss5-fpslimit.txt";

// O SUFIXO DA REMOCAO ADIADA. O Windows nao apaga DLL que esta carregada, e nao ha o que
// argumentar com isso. Mas ele DEIXA RENOMEAR: o nome e a entrada no diretorio, e o processo
// que carregou continua com o arquivo aberto pelo handle dele. Entao, quando apagar nao da, a
// gente renomeia - o jogo nao vai mais achar "dxgi.dll" na proxima abertura, que e o efeito que
// a pessoa pediu, e o arquivo renomeado some na proxima vez que o app abrir (LimparPendencias).
//
// O nome carrega o do produto de proposito: se um dia sobrar, quem abrir a pasta descobre de
// onde veio sem precisar procurar.
static const wchar_t* kSufixoRemover = L".dlss5-remover";

// Por que o app recusou instalar. Cada um vira uma frase diferente na tela: a pessoa so
// consegue agir se souber qual dos casos e o dela.
enum ResultadoInstalar {
    INST_OK = 0,
    INST_JA_ESTAVA,          // as duas ja estao la, e sao nossas
    INST_OCUPADO,            // ja existe um dxgi.dll que NAO e nosso: ReShade, mod, outra coisa
    INST_SEM_PERMISSAO,      // a pasta do jogo nao aceita escrita
    INST_SEM_ORIGEM,         // as pecas nao vieram no pacote
    INST_SEM_PASTA,          // nao sabemos onde o jogo mora
};

// ------------------------------------------------------------------ utilidades de disco
static bool Existe(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// A casca que esta na pasta do jogo e NOSSA? A pergunta importa porque um dxgi.dll ali pode ser
// um ReShade que a pessoa instalou, e sobrescrever aquilo seria apagar o trabalho dela sem
// perguntar. Conferimos pela presenca da nossa peca ao lado E do nosso LEIA-ME: dois arquivos
// nossos ao lado de um dxgi.dll e assinatura suficiente, e nao depende de ler o binario.
//
// CUIDADO AO REUSAR ESTA FUNCAO: ela responde "a instalacao esta INTEIRA", e nao "ha coisa
// nossa aqui". Sao perguntas diferentes e a confusao entre as duas foi um defeito real: se uma
// atualizacao do jogo apagasse SO a peca, esta funcao passava a dizer nao, o app esquecia a
// instalacao e a casca ficava orfa na pasta da pessoa para sempre. Para a outra pergunta ha
// AlgoNossoNaPasta() abaixo.
static bool CascaEhNossa(const std::wstring& pastaJogo) {
    return Existe(pastaJogo + kNomePeca) && Existe(pastaJogo + kNomeLeiaMe);
}

// Dois arquivos com o mesmo conteudo? Le em blocos e compara; o tamanho descarta a maioria dos
// casos antes de ler um byte. Abre com FILE_SHARE_DELETE alem de leitura e escrita porque o
// arquivo pode estar CARREGADO por um jogo aberto - e o objetivo aqui e justamente decidir o que
// fazer com ele.
static bool ArquivosIguais(const std::wstring& a, const std::wstring& b) {
    const DWORD comp = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE ha = CreateFileW(a.c_str(), GENERIC_READ, comp, nullptr, OPEN_EXISTING, 0, nullptr);
    if (ha == INVALID_HANDLE_VALUE) return false;
    HANDLE hb = CreateFileW(b.c_str(), GENERIC_READ, comp, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hb == INVALID_HANDLE_VALUE) { CloseHandle(ha); return false; }
    LARGE_INTEGER ta{}, tb{};
    bool ok = GetFileSizeEx(ha, &ta) && GetFileSizeEx(hb, &tb) && ta.QuadPart == tb.QuadPart;
    if (ok) {
        std::vector<char> ba(16384), bb(16384);
        for (;;) {
            DWORD na = 0, nb = 0;
            if (!ReadFile(ha, ba.data(), (DWORD)ba.size(), &na, nullptr) ||
                !ReadFile(hb, bb.data(), (DWORD)bb.size(), &nb, nullptr)) { ok = false; break; }
            if (na != nb) { ok = false; break; }
            if (na == 0) break;
            if (memcmp(ba.data(), bb.data(), na) != 0) { ok = false; break; }
        }
    }
    CloseHandle(ha); CloseHandle(hb);
    return ok;
}

// A CASCA NAQUELA PASTA E COMPROVADAMENTE A NOSSA? Compara byte a byte com a que veio no pacote.
// E a prova mais forte que da para ter sem assinar binario, e e ela que autoriza APAGAR: a regra
// de ouro do arquivo e nunca remover arquivo de outra pessoa, e "parece nosso" nao basta para
// isso. Quando a casca de origem nao esta disponivel (pacote incompleto), cai para a assinatura
// dos arquivos ao lado - que e mais fraca, entao nesse caso so vale se o LEIA-ME estiver la.
static bool CascaEhNossaMesmo(const std::wstring& pastaJogo, const std::wstring& pastaOrigem) {
    const std::wstring aqui = pastaJogo + kNomeCasca;
    if (!Existe(aqui)) return false;
    const std::wstring orig = pastaOrigem + kNomeCasca;
    if (!pastaOrigem.empty() && Existe(orig)) return ArquivosIguais(aqui, orig);
    return Existe(pastaJogo + kNomeLeiaMe);
}

// HA QUALQUER VESTIGIO NOSSO NAQUELA PASTA? Esta e a pergunta que o app faz na abertura, e ela
// e de proposito mais larga que CascaEhNossa(): meia instalacao TAMBEM conta. Uma casca sozinha,
// uma peca sozinha, um LEIA-ME esquecido ou um arquivo ja renomeado para remocao - qualquer um
// deles significa que ha coisa nossa na maquina de quem usa, e o botao de remover tem de
// continuar existindo.
static bool AlgoNossoNaPasta(const std::wstring& pastaJogo, const std::wstring& pastaOrigem) {
    if (pastaJogo.empty()) return false;
    if (Existe(pastaJogo + kNomePeca))  return true;
    if (Existe(pastaJogo + kNomeLeiaMe)) return true;
    if (Existe(pastaJogo + kNomeCasca + kSufixoRemover)) return true;
    if (Existe(pastaJogo + kNomePeca  + kSufixoRemover)) return true;
    if (Existe(pastaJogo + kNomeLeiaMe + kSufixoRemover)) return true;
    return CascaEhNossaMesmo(pastaJogo, pastaOrigem);
}

// HA ALGUM PROCESSO RODANDO DE DENTRO DAQUELA PASTA? E como se pergunta "o jogo esta aberto?"
// sem depender de o app ainda estar mirando nele: a pessoa pode ter trocado de alvo, ou fechado
// e reaberto o jogo, e o PID que o app guardou nao vale mais.
//
// POR QUE PERGUNTAR ANTES DE TENTAR, e nao simplesmente tentar e ver o erro: o erro de apagar
// uma DLL carregada e ACCESS_DENIED, o mesmo de pasta protegida e de falta de permissao. Tratar
// os tres como um so foi o que produziu a mensagem errada que este conserto resolve. Perguntando
// antes, a frase que a pessoa le nomeia a causa certa e diz o que fazer.
static bool JogoAbertoNaPasta(const std::wstring& pastaJogo, std::wstring* quem = nullptr) {
    if (pastaJogo.empty()) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    bool achou = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (!p) continue;
            wchar_t buf[MAX_PATH * 2]; DWORD n = MAX_PATH * 2;
            if (QueryFullProcessImageNameW(p, 0, buf, &n)) {
                const std::wstring cam(buf);
                if (cam.size() > pastaJogo.size() &&
                    _wcsnicmp(cam.c_str(), pastaJogo.c_str(), pastaJogo.size()) == 0) {
                    achou = true;
                    if (quem) *quem = pe.szExeFile;
                }
            }
            CloseHandle(p);
        } while (!achou && Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return achou;
}

// A pasta do executavel do jogo, a partir do caminho completo dele.
static std::wstring PastaDe(const std::wstring& exeCompleto) {
    const size_t b = exeCompleto.find_last_of(L"\\/");
    return b == std::wstring::npos ? L"" : exeCompleto.substr(0, b + 1);
}

// O texto que fica ao lado das pecas, para quem abrir a pasta do jogo daqui a um ano entender o
// que aquilo e sem precisar investigar - e saber que apagar e seguro.
static const char* kTextoLeiaMe =
    "Estes dois arquivos sao do DLSS5 For All e servem para travar o fps deste jogo:\r\n"
    "\r\n"
    "    dxgi.dll             uma casca que repassa tudo para a dxgi.dll do Windows\r\n"
    "    dlss5-fpslimit.dll   quem segura o quadro no ritmo escolhido\r\n"
    "\r\n"
    "APAGAR OS DOIS E SEGURO e nao quebra o jogo: sem eles o jogo simplesmente volta a rodar\r\n"
    "sem limite de fps. Eles tambem nao fazem nada sozinhos - se o DLSS5 For All nao estiver\r\n"
    "aberto, ficam inertes e o jogo roda como se nao existissem.\r\n"
    "\r\n"
    "Pelo programa: painel > FPS DO JOGO > Remover daqui.\r\n"
    "\r\n"
    "FECHE O JOGO ANTES DE REMOVER. Com o jogo aberto o Windows nao apaga estes arquivos,\r\n"
    "porque eles estao carregados dentro dele. O programa avisa quando for esse o caso.\r\n"
    "\r\n"
    "SE VOCE VIR UM ARQUIVO TERMINADO EM \".dlss5-remover\": e um destes dois, ja desligado.\r\n"
    "O jogo nao o carrega mais. Ele sai sozinho da proxima vez que o DLSS5 For All abrir, e\r\n"
    "apagar a mao tambem e seguro.\r\n";

// ------------------------------------------------------------------ instalar e remover
// `pastaOrigem` e onde as pecas vieram no pacote (ao lado do app, em limitador\).
static int Instalar(const std::wstring& pastaOrigem, const std::wstring& exeJogoCompleto,
                    std::wstring* pastaUsada = nullptr) {
    const std::wstring pasta = PastaDe(exeJogoCompleto);
    if (pasta.empty()) return INST_SEM_PASTA;
    if (pastaUsada) *pastaUsada = pasta;

    const std::wstring origCasca = pastaOrigem + kNomeCasca;
    const std::wstring origPeca  = pastaOrigem + kNomePeca;
    if (!Existe(origCasca) || !Existe(origPeca)) {
        LogH("limitador: as pecas nao estao em %ls - nada a instalar", pastaOrigem.c_str());
        return INST_SEM_ORIGEM;
    }

    // A RECUSA MAIS IMPORTANTE DESTE ARQUIVO. Um dxgi.dll que nao e nosso na pasta do jogo e,
    // quase sempre, um ReShade ou um mod que a pessoa instalou de proposito. Sobrescrever
    // aquilo quebraria o que ela montou, sem aviso e sem volta. Preferimos nao instalar.
    //
    // A PROVA E BYTE A BYTE, e nao "tem nossos vizinhos ao lado". A versao anterior perguntava
    // por CascaEhNossa(), que exige a peca e o LEIA-ME presentes - entao uma casca NOSSA que
    // tivesse ficado sozinha (jogo atualizado por cima, peca apagada) era classificada como
    // dxgi.dll de outra pessoa, e o app recusava reinstalar por cima do proprio arquivo. A
    // pessoa ficava sem saida: nao instalava e, pelo mesmo engano, tambem nao removia.
    if (Existe(pasta + kNomeCasca) && !CascaEhNossaMesmo(pasta, pastaOrigem)) {
        LogH("limitador: ja existe um dxgi.dll que nao e nosso em %ls - recusando", pasta.c_str());
        return INST_OCUPADO;
    }
    /* Ja estava so quando esta INTEIRO **e a peca e a DESTA versao do app**. A casca quase nunca
       muda; a peca muda a cada versao do contrato (fpslimit_shared.h, `kVersao`), e uma peca
       velha na pasta do jogo faz o app RECUSAR o bloco (Conversa::Abrir) - o limitador ao vivo
       some sem que ninguem tenha desinstalado nada. Entao aqui a peca tambem e comparada byte a
       byte, e so ela e recopiada quando difere.
       Se a copia falhar (o jogo esta aberto e segura a DLL), isto NAO vira erro: a instalacao
       antiga continua de pe, o limitador ao vivo fica indisponivel ate o jogo reabrir, e e
       exatamente o que a pessoa ja teria sem este remendo. */
    if (CascaEhNossaMesmo(pasta, pastaOrigem) && Existe(pasta + kNomePeca)) {
        if (ArquivosIguais(pasta + kNomePeca, origPeca)) return INST_JA_ESTAVA;
        if (CopyFileW(origPeca.c_str(), (pasta + kNomePeca).c_str(), FALSE)) {
            LogH("limitador: a peca em %ls era de outra versao - atualizada (vale na proxima abertura do jogo)", pasta.c_str());
            return INST_OK;
        }
        LogH("limitador: a peca em %ls e de outra versao e nao deu para trocar (erro %lu; o jogo esta aberto?) - feche o jogo e instale de novo", pasta.c_str(), GetLastError());
        return INST_JA_ESTAVA;
    }

    // A peca PRIMEIRO, a casca por ultimo. A ordem importa: a casca e quem faz o jogo carregar a
    // peca, entao enquanto ela nao existir nao ha meio caminho em que o jogo procure uma peca
    // que ainda nao chegou.
    if (!CopyFileW(origPeca.c_str(), (pasta + kNomePeca).c_str(), FALSE)) {
        LogH("limitador: nao consegui escrever em %ls (erro %lu)", pasta.c_str(), GetLastError());
        return INST_SEM_PERMISSAO;
    }
    HANDLE h = CreateFileW((pasta + kNomeLeiaMe).c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD n = 0; WriteFile(h, kTextoLeiaMe, (DWORD)strlen(kTextoLeiaMe), &n, nullptr);
        CloseHandle(h);
    }
    if (!CopyFileW(origCasca.c_str(), (pasta + kNomeCasca).c_str(), FALSE)) {
        DeleteFileW((pasta + kNomePeca).c_str());
        DeleteFileW((pasta + kNomeLeiaMe).c_str());
        LogH("limitador: falhei ao escrever a casca - desfiz o que ja tinha copiado");
        return INST_SEM_PERMISSAO;
    }
    LogH("limitador: instalado em %ls (vale quando o jogo for aberto de novo)", pasta.c_str());
    return INST_OK;
}

// ------------------------------------------------------------------ remover, e dizer a verdade
// POR QUE ESTA PARTE E TAO MAIOR QUE A DE INSTALAR. Instalar falha de um jeito so, e a pessoa ve
// o efeito na hora. Remover falha de tres jeitos diferentes que se parecem entre si no codigo de
// erro do Windows, e o defeito que isto conserta era exatamente esse: o app dizia "nao havia nada
// nosso para remover ali" quando o que tinha acontecido era o jogo estar ABERTO segurando a DLL.
// Mensagem errada e causa errada ao mesmo tempo: a pessoa fechava o programa achando que tinha
// removido, e o arquivo continuava na pasta do jogo dela.
//
//   NUNCA REPORTAR SUCESSO QUANDO NADA SAIU. E a regra desta secao inteira.
enum ResultadoRemover {
    REM_NADA_LA = 0,      // nao havia nada nosso ali - agora isto e uma conclusao, nao um palpite
    REM_OK,               // saiu tudo o que era nosso
    REM_JOGO_ABERTO,      // o jogo esta rodando; nao tentamos, e dissemos por que
    REM_ADIADO,           // renomeado: some na proxima abertura do app
    REM_FALHOU,           // nem apagar nem renomear deu
};

struct Remocao {
    int          resultado = REM_NADA_LA;
    int          apagados  = 0;
    int          adiados   = 0;
    bool         cascaAlheiaMantida = false;  // havia um dxgi.dll de outra pessoa; nao tocamos
    std::wstring quemSegura;                  // exe que esta rodando de dentro da pasta
};

// Tenta apagar; se nao der, renomeia. Devolve 1 se apagou, 2 se adiou, 0 se nao havia ou falhou
// de vez. O renomear e a segunda tentativa e nao a primeira: apagar de fato e sempre melhor,
// porque nao deixa nada para a proxima abertura lembrar de fazer.
static int TirarArquivo(const std::wstring& caminho) {
    if (!Existe(caminho)) return 0;
    if (DeleteFileW(caminho.c_str())) return 1;
    const DWORD e1 = GetLastError();
    const std::wstring adiado = caminho + kSufixoRemover;
    DeleteFileW(adiado.c_str());   // sobra de uma tentativa anterior atrapalharia o rename
    if (MoveFileExW(caminho.c_str(), adiado.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        LogH("limitador: nao deu para apagar %ls (erro %lu) - renomeado, sai na proxima abertura",
             caminho.c_str(), e1);
        return 2;
    }
    LogH("limitador: nao deu para apagar nem renomear %ls (erros %lu / %lu)",
         caminho.c_str(), e1, GetLastError());
    return 0;
}

// Apaga o que ficou renomeado por uma remocao adiada. Roda na abertura do app, quando o jogo
// quase sempre esta fechado e o arquivo ja nao esta carregado por ninguem.
static int LimparPendencias(const std::wstring& pastaJogo) {
    if (pastaJogo.empty()) return 0;
    int n = 0;
    for (const wchar_t* nome : { kNomeCasca, kNomePeca, kNomeLeiaMe }) {
        const std::wstring p = pastaJogo + nome + kSufixoRemover;
        if (Existe(p) && DeleteFileW(p.c_str())) n++;
    }
    if (n) LogH("limitador: %d arquivo(s) que tinham ficado para tras sairam de %ls", n, pastaJogo.c_str());
    return n;
}

// Remove o que for NOSSO. Nunca remove um dxgi.dll que nao seja, pelo mesmo motivo da recusa la
// em cima - e agora "ser nosso" e provado byte a byte contra a casca do pacote, nao deduzido dos
// vizinhos.
static Remocao Remover(const std::wstring& pastaJogo, const std::wstring& pastaOrigem) {
    Remocao r;
    if (pastaJogo.empty()) return r;

    // Sobras de uma remocao adiada anterior contam como trabalho feito: se so isso existia, a
    // pasta fica limpa e o resultado e OK, nao "nao havia nada".
    const int limpos = LimparPendencias(pastaJogo);
    r.apagados += limpos;

    const bool temPeca   = Existe(pastaJogo + kNomePeca);
    const bool temLeiaMe = Existe(pastaJogo + kNomeLeiaMe);
    const bool temCasca  = Existe(pastaJogo + kNomeCasca);
    const bool cascaNossa = temCasca && CascaEhNossaMesmo(pastaJogo, pastaOrigem);
    r.cascaAlheiaMantida = temCasca && !cascaNossa;

    if (!temPeca && !temLeiaMe && !cascaNossa) {
        r.resultado = limpos ? REM_OK : REM_NADA_LA;
        return r;
    }

    // PERGUNTAR ANTES DE TENTAR. Com o jogo aberto a DLL esta carregada e nao sai; tentar aqui so
    // produziria um ACCESS_DENIED indistinguivel de falta de permissao. Melhor nao mexer em nada
    // e dizer a causa certa: assim a pasta tambem nao fica pela metade.
    if (JogoAbertoNaPasta(pastaJogo, &r.quemSegura)) {
        LogH("limitador: %ls esta aberto - nao removi nada (o Windows nao apaga DLL carregada)",
             r.quemSegura.c_str());
        r.resultado = REM_JOGO_ABERTO;
        return r;
    }

    // A CASCA PRIMEIRO, ao contrario da instalacao. E ela que faz o jogo carregar a peca: saindo
    // primeiro, nao existe momento em que o jogo ache a casca e nao ache a peca.
    if (cascaNossa) { const int q = TirarArquivo(pastaJogo + kNomeCasca); if (q == 1) r.apagados++; else if (q == 2) r.adiados++; }
    { const int q = TirarArquivo(pastaJogo + kNomePeca);   if (q == 1) r.apagados++; else if (q == 2) r.adiados++; }
    { const int q = TirarArquivo(pastaJogo + kNomeLeiaMe); if (q == 1) r.apagados++; else if (q == 2) r.adiados++; }

    const bool sobrou = Existe(pastaJogo + kNomePeca) || Existe(pastaJogo + kNomeLeiaMe) ||
                        (cascaNossa && Existe(pastaJogo + kNomeCasca));
    if (sobrou)            r.resultado = REM_FALHOU;
    else if (r.adiados)    r.resultado = REM_ADIADO;
    else                   r.resultado = REM_OK;

    LogH("limitador: remocao em %ls - resultado %d, %d apagado(s), %d adiado(s)%ls",
         pastaJogo.c_str(), r.resultado, r.apagados, r.adiados,
         r.cascaAlheiaMantida ? L" (havia um dxgi.dll de outra pessoa: mantido)" : L"");
    return r;
}

// ------------------------------------------------------------------ conversa com a peca
// O app ABRE o bloco que a peca criou; nao cria. Se a peca nao esta la, nao ha bloco, e isso
// e a resposta: o limitador ao vivo nao esta disponivel neste jogo.
struct Conversa {
    HANDLE mapa = nullptr;
    Bloco* b = nullptr;
    DWORD  pid = 0;

    bool Abrir(DWORD pidJogo) {
        Fechar();
        if (!pidJogo) return false;
        wchar_t nome[128];
        NomeDoBloco(nome, 128, pidJogo);
        mapa = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, nome);
        if (!mapa) return false;
        b = (Bloco*)MapViewOfFile(mapa, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Bloco));
        if (!b) { CloseHandle(mapa); mapa = nullptr; return false; }
        // CONFERIR ANTES DE TOCAR. O bloco pode ser de uma versao antiga da peca, que ficou na
        // pasta de um jogo depois de uma atualizacao do app. Interpretar um contrato de outra
        // versao e pior do que nao falar com ele.
        if (!BlocoValido(b)) {
            LogH("limitador: a peca no jogo e de outra versao (magic %08X versao %u) - ignorando",
                 b->magic, b->versao);
            Fechar();
            return false;
        }
        pid = pidJogo;
        return true;
    }
    void Fechar() {
        if (b) { UnmapViewOfFile(b); b = nullptr; }
        if (mapa) { CloseHandle(mapa); mapa = nullptr; }
        pid = 0;
    }
    bool Ativa() const { return b != nullptr; }

    // UMA VEZ POR SEGUNDO, do laco do app. E o que diz a peca que ainda estamos vivos: se este
    // pulso parar, ela solta o limite sozinha em poucos segundos e o jogo destrava. Sem isto,
    // um travamento nosso deixaria o jogo da pessoa preso ate ser fechado.
    void Pulsar() { if (b) b->pulso_app++; }

    void Pedir(uint32_t fps) { if (b) b->limite_fps = ValorPermitido(fps); }
    void Desligar()          { if (b) { b->desligar = 1; b->limite_fps = 0; } }

    /* N1 - A FONTE EM FASE COM A SAIDA. Chamada uma vez por segundo, do mesmo ponto do Pulsar:
       a fase nao muda a cada quadro (ela sai de `D`, que e uma media movel), e escrever mais
       vezes so poria o caminho quente do jogo para ler um valor mudando debaixo dele.
       `faseMs` e o phi em milissegundos, negativo quando o quadro tem de ser solto ANTES do
       vblank - que e o caso normal, porque o apresentador precisa dele `D` antes do prazo. */
    void Fase(bool ancora, double faseMs) {
        if (!b) return;
        if (faseMs >  1000.0) faseMs =  1000.0;
        if (faseMs < -1000.0) faseMs = -1000.0;
        b->fase_us = (int32_t)(faseMs * 1000.0);
        b->ancora  = ancora ? 1u : 0u;
    }

    uint32_t Estado() const   { return b ? b->estado : (uint32_t)EST_INICIANDO; }
    uint32_t Protecao() const { return b ? b->protecao : 0u; }
    uint32_t Segurados() const{ return b ? b->quadros_segurados : 0u; }
    std::wstring Motivo() const { return b ? std::wstring((const wchar_t*)b->motivo) : std::wstring(); }
};

} // namespace FpsLimit
