// =====================================================================================
//  DIAGNOSTICO "FUNCIONOU?"                            (N3 / Q3, 16/09/2026)
// =====================================================================================
//
//  DE ONDE VEM A INFORMACAO: daqui nao sai medida nova nenhuma. Tudo o que este arquivo
//  usa ja esta escrito - o que o feeder e o ReShade publicam (dlss5-feed.log,
//  dlss5-feed.status, ReShade.log) e o que o Windows guarda sobre quem morreu (Event Log,
//  canal Application). Nenhum mecanismo novo dentro da cadeia: so leitura do que ja existe.
//
//  O DEFEITO QUE ISTO CONSERTA e o mais caro que este projeto tem: quando o DLSS 5 nao
//  sobe, NADA aparece na tela. A imagem continua, sem o filtro, e a pessoa conclui que o
//  programa quebrou - ou pior, que funcionou. A informacao existe, esta nos logs, e nunca
//  chega a quem precisa. Aqui ela vira uma frase.
//
//  A REGRA DE OURO E O SILENCIO. Regra sem evidencia nao fala. Um diagnostico que chuta
//  custa mais caro que nenhum: quem ouve "troque a build do nvngx_dlssnr" sem que esse
//  seja o problema passa a tarde trocando arquivo certo por arquivo certo. Por isso cada
//  regra abaixo exige uma linha LITERAL de um log, e as que cruzam duas fontes (a build e
//  a placa) so acusam quando as duas concordam.
//
// =====================================================================================
#pragma once

// Este header e incluido DE DENTRO do main.cpp, depois dos utilitarios de que depende:
// Log/U8, TailArquivo, LerFeedStatus, MotorProprio, g_exeDir, g_dlssnr e g_hud. Nao ha
// include proprio de nada disso de proposito: duplicar as declaracoes aqui criaria duas
// verdades sobre as mesmas funcoes.

// =====================================================================================
//  DIAGNOSTICO
// =====================================================================================
namespace Diag {

// ------------------------------------------------------ o que o Event Log tem a dizer
//
// POR QUE wevtutil E NAO EvtQuery/EvtRender: a consulta roda UMA VEZ por sessao, numa
// thread propria, e o que interessa dela sao tres campos. Linkar wevtapi (ou carrega-la a
// mao com GetProcAddress, que e o que o build exigiria) para tres campos seria mais codigo
// e mais coisa para dar errado do que um processo filho que ja vem no Windows.
//
// /f:xml E O DETALHE QUE FAZ ISSO FUNCIONAR EM QUALQUER MAQUINA. Em /f:text os campos vem
// com o nome traduzido ("Nome do modulo com falha:" aqui, "Faulting module name:" numa
// maquina em ingles) e o leitor teria de conhecer os dois. No XML nao ha nome nenhum: o
// Application Error escreve <Data> em ordem fixa, e a ordem e a mesma em todo idioma.
//     Data[0] = exe que morreu     Data[3] = modulo que o derrubou
//     Data[6] = codigo da excecao  Data[10]/[11] = caminhos completos
struct EvtFalha {
    bool         tem = false;
    std::wstring app;       // "DLSS5ForAll.exe"
    std::wstring modulo;    // "nvngx_dlssnr.dll"
    std::wstring codigo;    // "0xc0000005"
};
static std::mutex g_evtMtx;
static EvtFalha   g_evt;
static bool       g_evtPedido = false;

// Le o valor de um <Data> pela ORDEM em que ele aparece. Nao e um parser de XML e nao
// pretende ser: o que sai do wevtutil para este provider e sempre o mesmo formato, e um
// parser de verdade aqui seria peso morto.
static std::wstring EvtDado(const std::wstring& bloco, int indice) {
    size_t p = 0;
    for (int i = 0; ; i++) {
        // "<Data" e nao "<Data>": conforme a versao do Windows a tag vem com atributo
        // (<Data Name='...'>) ou sem nenhum. Procurar o '>' que a fecha cobre os dois.
        const size_t a = bloco.find(L"<Data", p);
        if (a == std::wstring::npos) return std::wstring();
        const size_t g = bloco.find(L'>', a);
        if (g == std::wstring::npos) return std::wstring();
        const size_t b = bloco.find(L"</Data>", g);
        if (b == std::wstring::npos) return std::wstring();
        if (i == indice) return bloco.substr(g + 1, b - g - 1);
        p = b + 7;
    }
}

// So o nome do arquivo, sem caminho: e o que vai para a tela.
static std::wstring SoNome(const std::wstring& caminho) {
    const size_t p = caminho.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? caminho : caminho.substr(p + 1);
}

// O exe do jogo desta sessao, copiado ANTES de a thread subir. A thread nunca toca em
// g_hud: ele e escrito pelo laco principal, e ler uma std::wstring enquanto outra thread a
// reescreve e corrida de dados, ainda que a chance pareca pequena.
static void EvtConsultarAgora(std::wstring exeDaSessao) {
    // A saida vai para um arquivo nosso no TEMP, e nao por pipe: com pipe seria preciso
    // drenar enquanto o filho escreve, senao ele trava no buffer cheio, e essa e a forma
    // classica de um diagnostico virar o problema que ele existe para achar.
    wchar_t tmp[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tmp)) return;
    wchar_t saida[MAX_PATH + 64];
    swprintf(saida, MAX_PATH + 64, L"%lsdlss5-evt.%lu.xml", tmp, (unsigned long)GetCurrentProcessId());

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE h = CreateFileW(saida, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    // Ultimos 10 minutos, os 6 eventos mais recentes, do mais novo para o mais velho.
    std::wstring cmd = L"wevtutil.exe qe Application "
        L"\"/q:*[System[Provider[@Name='Application Error'] and TimeCreated[timediff(@SystemTime)<=600000]]]\" "
        L"/c:6 /rd:true /f:xml";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);

    STARTUPINFOW si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = h; si.hStdError = h;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) { CloseHandle(h); DeleteFileW(saida); return; }
    // TETO DE 8 s. O Event Log desta maquina responde em menos de um segundo, mas um canal
    // grande num disco lento ja demorou muito mais em outras - e uma thread pendurada para
    // sempre num diagnostico opcional e o tipo de vazamento que ninguem encontra depois.
    if (WaitForSingleObject(pi.hProcess, 8000) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(h);

    const std::string xml = TailArquivo(saida, 256 * 1024);
    DeleteFileW(saida);
    if (xml.empty()) return;
    std::wstring w; w.reserve(xml.size());
    for (unsigned char c : xml) w += (c < 0x80) ? (wchar_t)c : L'?';

    // O EVENTO QUE INTERESSA NAO E SIMPLESMENTE O MAIS RECENTE. O canal Application recebe
    // a queda de qualquer programa da maquina, e acusar "o Photoshop caiu" numa faixa do
    // DLSS 5 seria ruido puro. Vale o primeiro evento cujo exe ou modulo pertenca a esta
    // historia - nosso app, o jogo capturado, ou uma das pecas da cadeia.
    size_t p = 0;
    while (true) {
        const size_t a = w.find(L"<EventData>", p);
        if (a == std::wstring::npos) break;
        const size_t b = w.find(L"</EventData>", a);
        if (b == std::wstring::npos) break;
        const std::wstring bloco = w.substr(a, b - a);
        p = b + 12;

        EvtFalha f;
        f.app    = SoNome(EvtDado(bloco, 0));
        f.modulo = SoNome(EvtDado(bloco, 3));
        f.codigo = EvtDado(bloco, 6);
        if (f.app.empty() && f.modulo.empty()) continue;

        std::wstring alvo = f.app + L"|" + f.modulo;
        for (wchar_t& c : alvo) c = (wchar_t)towlower(c);
        const wchar_t* const nossos[] = { L"dlss5forall", L"nvngx", L"renodx", L"reshade",
                                          L"dlss5-", L"dxgi.dll", L"amd_fidelityfx" };
        bool meu = false;
        for (const wchar_t* n : nossos) if (alvo.find(n) != std::wstring::npos) { meu = true; break; }
        // O jogo desta sessao tambem conta: se ele caiu com um modulo nosso dentro, quem
        // precisa saber disso e exatamente quem esta abrindo este app agora.
        if (!meu && !exeDaSessao.empty()) {
            std::wstring exe = exeDaSessao;
            for (wchar_t& c : exe) c = (wchar_t)towlower(c);
            if (!exe.empty() && alvo.find(exe) != std::wstring::npos) meu = true;
        }
        if (!meu) continue;

        f.tem = true;
        { std::lock_guard<std::mutex> lk(g_evtMtx); g_evt = f; }
        Log("diagnostico: Event Log - %ls caiu em %ls (%ls), nos ultimos 10 min",
            f.app.c_str(), f.modulo.c_str(), f.codigo.c_str());
        return;
    }
}

// UMA VEZ POR SESSAO, numa thread solta. A thread e detached de proposito: ninguem espera
// por ela, e o resultado so e usado se chegar a tempo. Diagnostico que atrasa o arranque
// do app e um mau negocio - a pessoa quer jogar, nao quer um relatorio.
static void EvtPedirUmaVez() {
    if (g_evtPedido) return;
    g_evtPedido = true;
    std::thread(EvtConsultarAgora, g_hud.exeAlvo).detach();
}

// ------------------------------------------------------------------- o veredito
enum {
    V_NADA = 0,
    V_DLSSNR_PLACA,   // CreateFeature 0xC0000005 + a build nao cobre a placa
    V_CREATE_FALHOU,  // CreateFeature estourou, mas a build CONFERE: a causa e outra
    V_DRIVER,         // o NGX pediu driver mais novo
    V_RENODX,         // o motor RenoDX nao carregou nesta sessao
    V_SEM_ADDON,      // o ReShade subiu sem nenhum add-on
    V_SEM_REDE,       // 10 s de cadeia e nenhuma linha `nr:` no log do feed
    V_MORREU,         // o app (ou o jogo) morreu antes, por um modulo nomeado
    V_NATIVO_FALHOU   // R1: o perfil do jogo registra falhas do modo Nativo (nativo_falhas)
};

struct Veredito {
    int          regra = V_NADA;
    std::wstring curto;   // a frase da faixa / do painel - uma linha, sem jargao
    std::wstring dica;    // titulo, porque, e o que fazer: SO na dica
};
static Veredito g_v;
static ULONGLONG g_cadeiaDesde = 0;    // quando a cadeia comecou a entregar (0 = ainda nao)
static ULONGLONG g_proxima = 0;        // quando roda a proxima avaliacao
static int       g_ultimaRegra = -1;   // para a linha do log sair na MUDANCA, e nao a cada 30 s

static const Veredito& Atual() { return g_v; }

static const char* NomeRegra(int r) {
    switch (r) {
        case V_DLSSNR_PLACA:  return "build do nvngx_dlssnr nao bate com a placa";
        case V_CREATE_FALHOU: return "CreateFeature da rede estourou (a build confere com a placa)";
        case V_DRIVER:        return "driver da NVIDIA antigo demais para o DLSS";
        case V_RENODX:        return "o motor RenoDX nao carregou";
        case V_SEM_ADDON:     return "o ReShade subiu sem add-ons";
        case V_SEM_REDE:      return "a rede neural nao esta rodando";
        case V_MORREU:        return "um processo desta cadeia caiu antes";
        case V_NATIVO_FALHOU: return "o modo Nativo ja falhou neste jogo (perfil)";
        default:              return "nada a relatar";
    }
}

// Monta os dois textos de uma regra. O CURTO cabe na faixa (uma linha de umas 80 letras) e
// nunca tem numero de erro; a DICA e onde moram o codigo, o nome do arquivo e a acao.
static void Por(int regra, const std::wstring& curto, const std::wstring& dica) {
    g_v.regra = regra; g_v.curto = curto; g_v.dica = dica;
}

// A AVALIACAO. Roda com a cadeia no ar (aos 12 s e depois de 30 em 30) e tambem uma vez no
// arranque do app, antes do seletor - ali so a regra do Event Log tem o que dizer, porque
// os logs desta sessao ainda nem existem.
//   redeLigada = a pessoa PEDIU a rede (neural ligado); sem isso a regra do `nr:` cala,
//                senao ela acusaria "a rede nao esta rodando" de quem a desligou.
static void Avaliar(bool comCadeia, bool redeLigada) {
    const std::string feed = comCadeia ? TailArquivo(g_exeDir + L"dlss5-feed.log", 48 * 1024) : std::string();
    const std::string rs   = comCadeia ? TailArquivo(g_exeDir + L"ReShade.log", 96 * 1024)   : std::string();
    const FeedStatus  st   = comCadeia ? LerFeedStatus() : FeedStatus();

    g_v = Veredito();

    // ---- REGRA 1: o CreateFeature da rede estourou.
    //
    // A linha vem do feeder, protegida por SEH: "nr: CreateFeature raised exception
    // 0xC0000005". Ela sozinha ja diz que a rede nao subiu; QUAL e a causa, quem responde e
    // o leitor de fatbin do P1, que sabe para quais arquiteturas a build foi compilada. Com
    // as duas fontes concordando, o veredito nomeia o arquivo e a build a baixar. Sem isso,
    // ele diz o que sabe e nao inventa a causa.
    const bool createEstourou =
        !feed.empty() && feed.find("CreateFeature") != std::string::npos &&
        (feed.find("raised exception") != std::string::npos ||
         feed.find("0xC0000005") != std::string::npos || feed.find("0xc0000005") != std::string::npos);
    if (createEstourou && g_dlssnr.resultado == DLSSNR_INCOMPATIVEL) {
        Por(V_DLSSNR_PLACA,
            TS(L"A rede neural não chega a ligar: o nvngx_dlssnr.dll não serve para esta placa.",
               L"The neural network never starts: nvngx_dlssnr.dll does not fit this card."),
            TS(L"A REDE NEURAL NÃO SUBIU — A BUILD NÃO BATE COM A PLACA\n"
               L"O feeder registrou CreateFeature 0xC0000005 (o código da rede estourou ao ser criado) e o "
               L"nvngx_dlssnr.dll desta pasta foi compilado só para ",
               L"THE NEURAL NETWORK DID NOT START — THE BUILD DOES NOT FIT THE CARD\n"
               L"The feeder logged CreateFeature 0xC0000005 (the network code faulted while being created) and the "
               L"nvngx_dlssnr.dll in this folder was compiled only for ")
            + g_dlssnr.lista + TS(L". A sua placa (", L". Your card (") + g_dlssnr.placa + L") = sm_"
            + std::to_wstring(g_dlssnr.sm) +
            TS(L".\nO que fazer: baixe a build ", L".\nWhat to do: download the ")
            + DlssnrBuildCerta(g_dlssnr.sm)
            + TS(L" no mesmo lugar de onde veio esta e troque o arquivo.",
                 L" build from the same place this one came from and replace the file."));
    } else if (createEstourou) {
        Por(V_CREATE_FALHOU,
            TS(L"A rede neural não chega a ligar nesta sessão.",
               L"The neural network never starts in this session."),
            TS(L"A REDE NEURAL NÃO SUBIU\n"
               L"O feeder registrou CreateFeature 0xC0000005 ao criar a rede. A build do nvngx_dlssnr.dll "
               L"desta pasta CONFERE com a sua placa, então o arquivo não é o culpado desta vez.\n"
               L"O que fazer: atualize o driver da NVIDIA e, no painel, troque o motor da rede (próprio ↔ "
               L"RenoDX) — são as duas causas que sobram quando a build está certa.",
               L"THE NEURAL NETWORK DID NOT START\n"
               L"The feeder logged CreateFeature 0xC0000005 while creating the network. The nvngx_dlssnr.dll "
               L"build in this folder DOES match your card, so the file is not to blame this time.\n"
               L"What to do: update the NVIDIA driver and, in the panel, switch the network engine (built-in ↔ "
               L"RenoDX) — the two causes left when the build is right."));
    }

    // ---- REGRA 2: o driver recusou o DLSS, ou pediu um mais novo.
    //
    // A linha literal do feeder: "[feed] NGX capabilities: SuperSampling.Available=1
    // NeedsUpdatedDriver=0 MinDriver=470.0". Sem ela, silencio - o proprio app pode estar
    // rodando com um feeder antigo que nao a imprime.
    if (g_v.regra == V_NADA && !feed.empty()) {
        const size_t c = feed.rfind("NGX capabilities:");
        if (c != std::string::npos) {
            const std::string linha = feed.substr(c, 200);
            const bool precisa = linha.find("NeedsUpdatedDriver=1") != std::string::npos;
            const bool semSr   = linha.find("SuperSampling.Available=0") != std::string::npos;
            if (precisa || semSr) {
                std::wstring minimo = L"470";
                const size_t m = linha.find("MinDriver=");
                if (m != std::string::npos) {
                    std::string v;
                    for (size_t i = m + 10; i < linha.size() && ((linha[i] >= '0' && linha[i] <= '9') || linha[i] == '.'); i++) v += linha[i];
                    if (!v.empty()) { minimo.clear(); for (char ch : v) minimo += (wchar_t)ch; }
                }
                Por(V_DRIVER,
                    TS(L"O driver da NVIDIA é antigo demais para o DLSS 5.",
                       L"The NVIDIA driver is too old for DLSS 5."),
                    TS(L"O DRIVER RECUSOU O DLSS\nO NGX respondeu que ",
                       L"THE DRIVER REFUSED DLSS\nNGX answered that ")
                    + (precisa ? TS(L"este driver precisa ser atualizado", L"this driver needs updating")
                               : TS(L"o Super Sampling não está disponível nesta placa com este driver",
                                    L"Super Sampling is not available on this card with this driver"))
                    + TS(L". A versão mínima que ele pede é ", L". The minimum version it asks for is ") + minimo
                    + TS(L".\nO que fazer: atualize o driver da NVIDIA pelo GeForce Experience ou pelo site da NVIDIA. "
                         L"Nenhum arquivo copiado para a pasta resolve isto.",
                         L".\nWhat to do: update the NVIDIA driver through GeForce Experience or NVIDIA's site. "
                         L"No file copied into the folder fixes this."));
            }
        }
    }

    // ---- REGRA 3: o ReShade subiu e nenhum add-on carregou.
    //
    // Duas linhas literais dele: "Searching for add-ons" sempre sai; "Registered add-on" sai
    // uma vez por add-on. A primeira sem nenhuma da segunda e o quadro exato de uma pasta em
    // que os .addon64 nao estao, ou em que o ReShade os recusou.
    if (g_v.regra == V_NADA && !rs.empty() &&
        rs.find("Searching for add-ons") != std::string::npos &&
        rs.find("Registered add-on") == std::string::npos) {
        Por(V_SEM_ADDON,
            TS(L"O ReShade subiu sem nenhum add-on: o DLSS 5 não entra na imagem.",
               L"ReShade started with no add-ons: DLSS 5 never reaches the image."),
            TS(L"NENHUM ADD-ON CARREGOU\n"
               L"O ReShade procurou os add-ons na pasta e não registrou nenhum. Sem o dlss5-feed.addon64 "
               L"a imagem passa inteira, sem o filtro — que é o que você está vendo.\n"
               L"O que fazer: confira se os arquivos .addon64 estão na pasta do programa e se o ReShade.ini "
               L"tem [ADDON] AddonPath apontando para ela. O botão \"Abrir a pasta\" desta tela leva ao lugar certo.",
               L"NO ADD-ON LOADED\n"
               L"ReShade searched the folder for add-ons and registered none. Without dlss5-feed.addon64 the "
               L"image passes through untouched — which is what you are seeing.\n"
               L"What to do: check that the .addon64 files are in the program folder and that ReShade.ini has "
               L"[ADDON] AddonPath pointing at it. The \"Open the folder\" button on this screen goes there."));
    }

    // ---- REGRA 4: o motor RenoDX nao carregou, e ele e o motor em uso.
    //
    // So acusa quando ha as DUAS evidencias: o ReShade registrou algum add-on (ou seja, o
    // log e util e nao esta vazio) mas nenhum deles e o RenoDX, E o motor em uso e o dele.
    // Com o motor proprio escolhido, a ausencia do RenoDX e o esperado, nao um defeito.
    if (g_v.regra == V_NADA && !rs.empty() && !MotorProprio() &&
        rs.find("Registered add-on") != std::string::npos &&
        rs.find("RenoDX") == std::string::npos) {
        Por(V_RENODX,
            TS(L"O motor RenoDX não carregou — troque para o motor próprio no painel.",
               L"The RenoDX engine did not load — switch to the built-in engine in the panel."),
            TS(L"O MOTOR DA REDE NÃO ESTÁ NO AR\n"
               L"O ReShade carregou add-ons nesta sessão, mas o RenoDX não está entre eles — e é ele o motor "
               L"escolhido. Sem o motor, a rede neural não tem onde rodar.\n"
               L"O que fazer: no painel (End), em \"Motor da rede\", escolha o motor próprio. Ele não depende "
               L"do RenoDX e aceita o tamanho de rede e os passes na hora.",
               L"THE NETWORK ENGINE IS NOT UP\n"
               L"ReShade loaded add-ons this session, but RenoDX is not among them — and RenoDX is the chosen "
               L"engine. With no engine the neural network has nowhere to run.\n"
               L"What to do: in the panel (End), under \"Network engine\", pick the built-in engine. It does not "
               L"depend on RenoDX and takes network size and passes right away."));
    }

    // ---- REGRA 5: dez segundos de cadeia e nenhuma linha `nr:` no log do feed.
    //
    // O feeder imprime "nr: ready at ..." assim que a rede sobe e "nr: NNN frames: ..." de
    // tempos em tempos. Com a rede PEDIDA, a cadeia entregando ha mais de 10 s e nenhuma
    // dessas linhas, a conclusao e direta: ela nao esta rodando. As duas condicoes sao o
    // que impede o falso positivo - antes dos 10 s a linha ainda nao teria saido mesmo.
    if (g_v.regra == V_NADA && comCadeia && redeLigada && !feed.empty() &&
        g_cadeiaDesde && GetTickCount64() - g_cadeiaDesde >= 10000 &&
        feed.find("nr: ") == std::string::npos && !st.passthrough) {
        Por(V_SEM_REDE,
            TS(L"A rede neural está ligada no painel, mas não está rodando.",
               L"The neural network is on in the panel, but it is not running."),
            TS(L"A REDE PEDIDA NÃO APARECE NO LOG DO FEEDER\n"
               L"Passaram mais de dez segundos com a cadeia entregando imagem e o dlss5-feed.log não tem "
               L"nenhuma linha \"nr:\" — a que o feeder escreve quando a rede sobe e quando ela roda. A imagem "
               L"está sendo ampliada, mas sem o realce da rede.\n"
               L"O que fazer: confira o nvngx_dlssnr.dll na pasta e, no painel, o motor da rede. Se você "
               L"acabou de trocar de motor, o programa precisa reabrir uma vez.",
               L"THE REQUESTED NETWORK DOES NOT SHOW UP IN THE FEEDER LOG\n"
               L"More than ten seconds passed with the chain delivering frames and dlss5-feed.log has no \"nr:\" "
               L"line — the one the feeder writes when the network comes up and while it runs. The image is "
               L"being upscaled, but without the network's work.\n"
               L"What to do: check nvngx_dlssnr.dll in the folder and the network engine in the panel. If you "
               L"just switched engines, the program has to reopen once."));
    }

    // ---- REGRA 6: alguem desta cadeia morreu antes (Event Log).
    //
    // Fica por ultimo de proposito: e a unica que fala do PASSADO. Qualquer regra sobre o
    // que esta acontecendo agora e mais util do que uma sobre o que aconteceu antes.
    if (g_v.regra == V_NADA) {
        EvtFalha f; { std::lock_guard<std::mutex> lk(g_evtMtx); f = g_evt; }
        if (f.tem) {
            Por(V_MORREU,
                TS(L"Antes desta sessão, ", L"Before this session, ") + f.app
                + TS(L" caiu em ", L" crashed in ") + f.modulo + L".",
                TS(L"UM PROCESSO DESTA CADEIA CAIU ANTES\nO Windows registrou, nos últimos dez minutos, que ",
                   L"A PROCESS IN THIS CHAIN CRASHED EARLIER\nWindows logged, in the last ten minutes, that ")
                + f.app + TS(L" terminou por uma falha dentro de ", L" ended on a fault inside ") + f.modulo
                + L" (" + f.codigo + TS(L").\nSe a queda se repetir, é esse arquivo que precisa ser trocado ou "
                                        L"desligado — não o programa inteiro. Este aviso some sozinho: ele só olha "
                                        L"os dez minutos anteriores.",
                                        L").\nIf the crash repeats, that is the file to replace or turn off — not "
                                        L"the whole program. This note clears itself: it only looks at the previous "
                                        L"ten minutes."));
        }
    }

    // ---- REGRA 7 (R1): o modo Nativo ja falhou neste jogo.
    //
    // A vigia de saude grava [perfil:<exe>] nativo_falhas / nativo_ultima_falha /
    // nativo_ultimo_motivo a cada erro. Tambem fala do passado, e por isso so fala quando nada
    // mais tem o que dizer. Veredito simples; o motivo tecnico fica no erros-nativo.log.
    if (g_v.regra == V_NADA && !g_perfilExe.empty()) {
        const std::wstring sec = L"perfil:" + g_perfilExe;
        const int falhas = (int)GetPrivateProfileIntW(sec.c_str(), L"nativo_falhas", 0, IniPath().c_str());
        if (falhas > 0) {
            wchar_t quando[64] = L"";
            GetPrivateProfileStringW(sec.c_str(), L"nativo_ultima_falha", L"", quando, 64, IniPath().c_str());
            Por(V_NATIVO_FALHOU,
                TS(L"O modo Nativo teve problemas neste jogo da última vez.",
                   L"Native mode had problems in this game last time."),
                TS(L"O MODO NATIVO JÁ FALHOU NESTE JOGO\nO programa registrou ", L"NATIVE MODE HAS FAILED IN THIS GAME\nThe program logged ")
                + std::to_wstring(falhas)
                + TS(L" problema(s) do modo Nativo aqui (o último em ", L" Native mode problem(s) here (the last on ")
                + quando
                + TS(L"). Em cada um ele trocou sozinho para o Universal ou reiniciou o DLSS 5 — o jogo não foi afetado.\n"
                     L"O que fazer: se voltar a acontecer, use o Universal neste jogo e mande o arquivo erros-nativo.log "
                     L"(ou o relato do Ctrl+F12).",
                     L"). Each time it switched to Universal or restarted DLSS 5 on its own — the game was not affected.\n"
                     L"What to do: if it happens again, use Universal in this game and send the erros-nativo.log file "
                     L"(or the Ctrl+F12 report)."));
        }
    }

    // A LINHA DO LOG SAI NA MUDANCA, e nao de 30 em 30 segundos. Um veredito repetido cem
    // vezes no log e a melhor forma de esconder o que mudou.
    if (g_v.regra != g_ultimaRegra) {
        g_ultimaRegra = g_v.regra;
        Log("diagnostico: %s", NomeRegra(g_v.regra));
        if (g_v.regra != V_NADA) Log("diagnostico: %s", U8(g_v.curto).c_str());
    }
}

// Chamada no arranque do APP (antes do seletor): pede o Event Log e ja publica o que der.
static void NoArranqueDoApp() {
    EvtPedirUmaVez();
    Avaliar(false, false);
}

// Chamada quando a cadeia entrega o primeiro quadro.
static void ArranqueDaCadeia(ULONGLONG now) {
    g_cadeiaDesde = now;
    g_proxima = now + 12000;    // 12 s: a regra do `nr:` precisa de 10 para nao mentir
    EvtPedirUmaVez();
    Log("diagnostico: cadeia no ar - primeira conferencia em 12 s, depois de 30 em 30");
}

// Chamada a cada segundo pelo laco; ela mesma decide quando ha o que fazer.
static void Tique(ULONGLONG now, bool redeLigada) {
    if (!g_cadeiaDesde || !g_proxima || now < g_proxima) return;
    g_proxima = now + 30000;
    Avaliar(true, redeLigada);
}

// Para a faixa do seletor: o texto curto e a dica. Vazios = nada a dizer.
static const std::wstring& FaixaTexto() { return g_v.curto; }
static const std::wstring& FaixaDica()  { return g_v.dica; }

}  // namespace Diag

// =====================================================================================
//  MIRA DE FPS (N3, 16/09/2026) REMOVIDA em 22/09/2026 por decisao do dono: ficou ambigua
//  com as metas de fluidez do card Frame Generation (Desligado | 60 | 80 | 100 | 120 |
//  Tela), que decidem sozinhas quanto entregar. O frame skipping continua manual, pelo
//  chip DLSS Frame Skipping - quem quiser liga a mao, sem ninguem mexendo por baixo.
// =====================================================================================
