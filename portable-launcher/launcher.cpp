// =====================================================================================
//  launcher.cpp - o executavel de fora da versao PORTABLE do DLSS5 For All.
//
//  E A INTERFACE UNICA do pacote. Mostra a tela do grupo (a mesma de sempre: QR, "QUERO
//  AS OFERTAS", "APOIAR O CANAL") e, no lugar do botao "INICIAR PROGRAMA", dois botoes:
//    USAR O DLSS 5 EM PLACA NVIDIA  -> abre bin\DLSS5ForAll.exe      (cadeia NVIDIA)
//    USAR O DLSS 5 EM PLACA AMD     -> abre bin-amd\DLSS5ForAll.exe  (cadeia AMD)
//  O que nao serve nesta maquina fica cinza com o motivo escrito no botao. Com os dois
//  cinzas aparece "ABRIR SEM DLSS 5": o app continua servindo como captura + ReShade.
//
//  Quem sabe o que e compativel e este arquivo (enumera as placas pelo DXGI); a tela so
//  desenha e devolve a escolha (splash_ofertas.h, modo launcher). Cada bin tem a propria
//  tela do grupo DESLIGADA no ofertas.ini (mostrar=0), senao ela apareceria duas vezes.
//
//  A escolha abre o exe com a pasta dele como diretorio de trabalho e repassa a linha de
//  comando inteira, como o launcher antigo. Quatro flags sao DESTE launcher e nao seguem
//  adiante: --nvidia, --amd e --sem-dlss pulam a tela (para atalhos e scripts);
//  --atualizado=<versao> e como o atualizar.cmd reabre o launcher depois de uma
//  atualizacao (so muda a assinatura no pe da tela).
//
//  IDIOMA (idioma.h): esta e a PRIMEIRA janela do pacote portable, entao o seletor de
//  idioma tem de estar nela - quem nao le portugues nao chegava ao painel do app, que ate
//  agora era o unico lugar onde dava para trocar. A tela mostra dois chips no alto
//  ("Português" / "English"); o clique vale na hora e o launcher grava a escolha no
//  dlss5forall.ini dos DOIS modulos, que e onde o app a le depois. Sem escolha gravada,
//  vale o idioma do Windows - ninguem e obrigado a responder nada para abrir o programa.
//
//  ATUALIZACAO (atualizacao.h): antes de a tela abrir, uma thread pergunta ao endereco
//  de [atualizacao] url_manifesto= (ofertas.ini da raiz) se ha versao nova - no maximo
//  uma vez a cada 6 h, 3 s de teto, e a tela NAO espera. Se houver, a tela ganha o banner
//  com "BAIXAR E ATUALIZAR" e a caixa "atualizar automaticamente"; a escolha ESC_ATUALIZAR
//  baixa, confere e entrega a troca ao atualizar.cmd, e este launcher sai para ser trocado.
//
//  VITRINE (vitrine.h): a tela ganha uma coluna a ESQUERDA com a pagina de ofertas de agora
//  (https://grupo.pcdofafa.com.br/app-ofertas, rolavel, com o botao do grupo), mostrada pelo
//  WebView2. A coluna so existe quando tudo isto vale: o
//  runtime do WebView2 instalado, a rede respondeu em ate 1,5 s (ao versao.json quando ha
//  url_base; senao a uma sonda de cabecalho na propria vitrine) e o monitor comporta as duas
//  colunas. Faltando qualquer um, a tela e a de sempre (uma coluna) e o launcher.log diz o
//  motivo ("vitrine=off motivo=...").
//
//  A PORTA UNICA (v2.0, lancamento.h): o app confere que foi aberto por este launcher - um
//  bilhete com o PID dele, publicado com o app ainda suspenso - e, aberto direto pelo bin\,
//  devolve a pessoa para ca. Por isso Abrir() espera o "conferido" do app antes de sair.
//
//  Compilado por build-launcher.sh (MinGW-w64). O icone entra pelo launcher.rc.
// =====================================================================================
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <cstdio>
#include "../idioma.h"
#include "../versao.h"
#include "../splash_ofertas.h"
#include "../atualizacao.h"
#include "../vitrine.h"
#include "../lancamento.h"   // v2.0: o bilhete que o app confere (o app so abre por esta tela)

static std::wstring PastaDoExe() {
    wchar_t p[32768];
    const DWORD n = GetModuleFileNameW(nullptr, p, 32768);
    std::wstring s(p, n);
    const size_t k = s.find_last_of(L"\\/");
    return (k == std::wstring::npos) ? L"" : s.substr(0, k + 1);
}

// v2.0 (--raiz): onde moram o estado, o log e o cache deste launcher. Normalmente a raiz do pacote;
// rodando do LOCALAPPDATA com uma raiz sem escrita (ver wWinMain), a pasta deste exe.
static std::wstring g_pastaDados;

static bool Existe(const std::wstring& caminho) {
    return GetFileAttributesW(caminho.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// A linha de comando sem o primeiro token (o caminho deste launcher), com ou sem aspas.
static std::wstring ArgumentosSemPrograma() {
    const wchar_t* p = GetCommandLineW();
    if (*p == L'"') { ++p; while (*p && *p != L'"') ++p; if (*p) ++p; }
    else { while (*p && *p != L' ' && *p != L'\t') ++p; }
    while (*p == L' ' || *p == L'\t') ++p;
    return p;
}

// ------------------------------------------------------------------ as placas
// O MESMO criterio do app (main.cpp, PlacaComTensorCore): NVIDIA com "RTX" no nome. A
// GTX 16xx e NVIDIA e nao tem Tensor Core, entao o fabricante sozinho nao responde.
static bool EhRtx(UINT vendor, const std::wstring& nome) {
    if (vendor != 0x10DEu) return false;
    std::wstring up = nome;
    for (auto& c : up) c = (wchar_t)towupper(c);
    return up.find(L"RTX") != std::wstring::npos;
}
// AMD que o mod do Neural Rendering cobre: Radeon RX 7000 (RDNA3) e RX 9000 (RDNA4). A
// iGPU do Ryzen ("Radeon(TM) Graphics", "Radeon 780M") fica de fora de proposito.
static bool EhRadeonCompativel(UINT vendor, const std::wstring& nome) {
    if (vendor != 0x1002u) return false;
    std::wstring up = nome;
    for (auto& c : up) c = (wchar_t)towupper(c);
    const size_t k = up.find(L"RX ");
    if (k == std::wstring::npos || k + 6 >= up.size()) return false;
    const wchar_t g = up[k + 3];
    return (g == L'7' || g == L'9') && iswdigit(up[k + 4]) && iswdigit(up[k + 5]) && iswdigit(up[k + 6]);
}

struct Placas {
    bool rtx = false, radeon = false;   // ha pelo menos uma de cada tipo compativel
    std::wstring nomeRtx, nomeRadeon;
};
// SO PARA TESTE (DLSS5_DIAG=1, a mesma regra do DLSS5_SPLASH_DPI): DLSS5_PLACAS finge o
// resultado da enumeracao - "rtx", "radeon", "ambas" ou "nenhuma". Sem isto nao ha como
// fotografar o seletor de duas placas, nem a tela de quem nao tem placa compativel, numa
// maquina que so tem uma GeForce. Devolve falso quando a chave nao esta valendo.
static bool PlacasFingidas(Placas& r) {
    wchar_t d[8]{}, b[16]{};
    if (!GetEnvironmentVariableW(L"DLSS5_DIAG", d, 8) || d[0] != L'1') return false;
    if (!GetEnvironmentVariableW(L"DLSS5_PLACAS", b, 16)) return false;
    const std::wstring v = b;
    if (v == L"rtx" || v == L"ambas")    { r.rtx = true;    r.nomeRtx = L"NVIDIA GeForce RTX 4080"; }
    if (v == L"radeon" || v == L"ambas") { r.radeon = true; r.nomeRadeon = L"AMD Radeon RX 9060 XT"; }
    return v == L"rtx" || v == L"radeon" || v == L"ambas" || v == L"nenhuma";
}
static Placas EnumerarPlacas() {
    Placas r;
    if (PlacasFingidas(r)) return r;
    IDXGIFactory1* f = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f)) || !f) return r;
    for (UINT i = 0; ; i++) {
        IDXGIAdapter1* a = nullptr;
        if (f->EnumAdapters1(i, &a) != S_OK || !a) break;
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        a->Release();
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        const std::wstring nome = d.Description;
        if (!r.rtx && EhRtx(d.VendorId, nome))                  { r.rtx = true;    r.nomeRtx = nome; }
        if (!r.radeon && EhRadeonCompativel(d.VendorId, nome))  { r.radeon = true; r.nomeRadeon = nome; }
    }
    f->Release();
    return r;
}
// O runtime HIP vem com o driver Adrenalin recente; sem ele o mod nao roda.
static bool TemRuntimeHip() {
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);
    return Existe(std::wstring(sys) + L"\\amdhip64_7.dll");
}

// O nome da placa como uma pessoa a chama: "NVIDIA GeForce RTX 4080" -> "RTX 4080",
// "AMD Radeon RX 9060 XT" -> "Radeon RX 9060 XT". A linha de estado do hardware e uma
// frase curta no alto da tela, nao a string do driver.
static std::wstring NomeCurtoDaPlaca(std::wstring n) {
    for (const wchar_t* p : { L"NVIDIA GeForce ", L"NVIDIA ", L"AMD ", L"Advanced Micro Devices, Inc. " })
        if (n.size() > wcslen(p) && _wcsnicmp(n.c_str(), p, wcslen(p)) == 0) { n = n.substr(wcslen(p)); break; }
    if (n.size() > 40) n = n.substr(0, 40);
    return n;
}

// ------------------------------------------------------------ o contador da aba da gaveta
// A vitrine publica um JSON ao lado da pagina; dele sai SO um numero, o "3 novas" da aba.
// Se o endereco nao responder, nao for JSON ou nao trouxer numero nenhum, o contador fica
// em -1 e a aba diz so "Ofertas" - nada depende disto para funcionar.
struct NovasArgs { std::wstring url, log; std::atomic<int>* alvo; };
static DWORD WINAPI ThreadNovas(LPVOID p) {
    NovasArgs* a = (NovasArgs*)p;
    std::string corpo;
    std::wstring erro;
    int n = -1;
    if (atualizacao::GetTexto(a->url, corpo, erro, 2000)) {
        atualizacao::Json j;
        if (atualizacao::LerJson(corpo, j)) {
            long long v = j.Num("novas");
            if (v <= 0)
                for (const char* k : { "ofertas", "itens", "items", "deals" }) {
                    const atualizacao::Json* l = j.Get(k);
                    if (l && l->t == atualizacao::Json::ARR) { v = (long long)l->a.size(); break; }
                }
            if (v <= 0 && j.t == atualizacao::Json::ARR) v = (long long)j.a.size();
            if (v > 0 && v < 1000) n = (int)v;
        }
    }
    const std::wstring motivo = n >= 0 ? L"do JSON" : (erro.empty() ? L"sem número no JSON" : erro);
    atualizacao::Anotar(a->log, L"vitrine: contador da aba = %d (%ls)", n, motivo.c_str());
    a->alvo->store(n);
    delete a;
    return 0;
}

// ------------------------------------------------------------------ abrir o app
static int Abrir(const std::wstring& pasta, const std::wstring& args) {
    const std::wstring alvo = pasta + L"DLSS5ForAll.exe";
    if (!Existe(alvo)) {
        atualizacao::Anotar(g_pastaDados + L"launcher.log", L"ERRO DE INICIALIZACAO [componente: app] nao encontrei %ls", alvo.c_str());   // v2.1
        MessageBoxW(nullptr,
            (TS(L"Não encontrei o programa em:\n", L"Could not find the program at:\n") + alvo +
             TS(L"\n\nA pasta precisa ficar ao lado deste executável.",
                L"\n\nThe folder has to sit next to this executable.")).c_str(),
            L"DLSS5 For All", MB_ICONERROR | MB_OK);
        return 1;
    }
    std::wstring linha = L"\"" + alvo + L"\"";
    if (!args.empty()) linha += L" " + args;
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring buf = linha;   // CreateProcessW pode escrever no buffer
    // v2.0: O APP SO ABRE POR ESTA TELA (lancamento.h). Ele nasce SUSPENSO para o bilhete
    // existir, com o PID dele no nome, antes de a primeira linha do app rodar; depois a
    // thread segue e este processo espera o "conferido" (ate 10 s) para sair - o app confere
    // que o pai e este exe, entao o pai tem de estar vivo durante a conferencia.
    if (!CreateProcessW(alvo.c_str(), &buf[0], nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        pasta.c_str(), &si, &pi)) {
        const DWORD e = GetLastError();
        atualizacao::Anotar(g_pastaDados + L"launcher.log", L"ERRO DE INICIALIZACAO [componente: app] CreateProcess de %ls falhou (erro do Windows %lu)", alvo.c_str(), (unsigned long)e);   // v2.1
        MessageBoxW(nullptr,
            (TS(L"Não consegui abrir:\n", L"Could not open:\n") + alvo +
             TS(L"\n\nErro do Windows: ", L"\n\nWindows error: ") + std::to_wstring(e)).c_str(),
            L"DLSS5 For All", MB_ICONERROR | MB_OK);
        return 1;
    }
    HANDLE ev = nullptr, mapa = nullptr;
    DWORD erroBilhete = 0; const char* etapaBilhete = "";
    const bool bilhete = Lancamento::PublicarBilhete(pi.dwProcessId, &ev, &mapa, &erroBilhete, &etapaBilhete);
    // v2.1: o motivo ORIGINAL (etapa + codigo do Windows) vai para o launcher.log - antes so ficava "bilhete FALHOU"
    if (!bilhete)
        atualizacao::Anotar(g_pastaDados + L"launcher.log", L"ERRO DE INICIALIZACAO [componente: bilhete do launcher] PublicarBilhete falhou em %hs (erro do Windows %lu)",
                            etapaBilhete, (unsigned long)erroBilhete);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    const ULONGLONG t0 = GetTickCount64();
    const bool conferiu = bilhete && Lancamento::EsperarConferido(pi.hProcess, pi.dwProcessId, 10000);
    DWORD saida = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &saida);
    // v2.1: O APP SAIU SEM CONFERIR = a pessoa clicou em Iniciar e "nada aconteceu". Agora ha mensagem, com o codigo de saida e
    // onde estao os logs (o app grava o porque em bin\dlss5forall.log; este lado em launcher.log).
    if (!conferiu && saida != STILL_ACTIVE) {
        atualizacao::Anotar(g_pastaDados + L"launcher.log", L"ERRO DE INICIALIZACAO [componente: bilhete/abertura do app] o app saiu sem conferir (codigo de saida %lu, bilhete %ls)",
                            (unsigned long)saida, bilhete ? L"publicado" : L"FALHOU");
        MessageBoxW(nullptr,
            (TS(L"O programa fechou logo ao abrir.\n\nCódigo de saída: ", L"The program closed right after opening.\n\nExit code: ") +
             std::to_wstring((unsigned long)saida) +
             (bilhete ? std::wstring() : (TS(L"\nO Windows recusou o bilhete de abertura (erro ", L"\nWindows refused the launch ticket (error ") +
                                          std::to_wstring((unsigned long)erroBilhete) + L")")) +
             TS(L"\n\nO motivo está no dlss5forall.log (pasta bin) e no launcher.log.",
                L"\n\nThe reason is in dlss5forall.log (bin folder) and launcher.log.")).c_str(),
            L"DLSS5 For All", MB_ICONWARNING | MB_OK);
    }
    atualizacao::Anotar(g_pastaDados + L"launcher.log", L"app: pid %lu, bilhete %ls, conferido %ls em %llu ms%ls, codigo de saida %lu",
                        (unsigned long)pi.dwProcessId, bilhete ? L"publicado" : L"FALHOU", conferiu ? L"sim" : L"NAO",
                        (unsigned long long)(GetTickCount64() - t0), saida == STILL_ACTIVE ? L"" : L" (o app ja saiu)", (unsigned long)saida);
    if (ev) CloseHandle(ev);
    if (mapa) CloseHandle(mapa);
    CloseHandle(pi.hProcess);
    return 0;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    // COM de apartamento unico: e o que o WebView2 (vitrine.h) exige - e a tela ja tem o laco
    // de mensagens que ele precisa. Inofensivo para todo o resto.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // v2.0: --raiz "<pasta>" - este exe e a COPIA do launcher que o app guarda dentro dele, extraida
    // no LOCALAPPDATA porque a pasta do pacote nao aceita escrita (Arquivos de Programas sem admin,
    // pasta so leitura). O pacote de verdade (bin\, ofertas.ini, versao.txt) e o de --raiz; o estado,
    // o log e o cache ficam la se ela aceitar escrita, senao aqui. Sem --raiz: tudo como sempre.
    std::wstring raizApp = PastaDoExe();
    bool comRaiz = false;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i + 1 < argc; i++)
            if (std::wstring(argv[i]) == L"--raiz") {
                std::wstring r = argv[i + 1];
                while (!r.empty() && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
                if (!r.empty() && Existe(r + L"\\bin")) { raizApp = r + L"\\"; comRaiz = true; }
                break;
            }
        if (argv) LocalFree(argv);
    }
    bool raizGravavel = true;
    if (comRaiz) {
        const std::wstring teste = raizApp + L"dlss5-teste-escrita.tmp";
        HANDLE h = CreateFileW(teste.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        raizGravavel = (h != INVALID_HANDLE_VALUE);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    const std::wstring raiz   = raizGravavel ? raizApp : PastaDoExe();   // estado/log/cache/ofertas.ini lidos daqui
    g_pastaDados = raiz;
    // a raiz sem escrita: o ofertas.ini dela (a configuracao do pacote) vem para a pasta de dados a cada abertura
    if (comRaiz && !raizGravavel && Existe(raizApp + L"ofertas.ini"))
        CopyFileW((raizApp + L"ofertas.ini").c_str(), (raiz + L"ofertas.ini").c_str(), FALSE);
    const std::wstring binNv  = raizApp + L"bin\\";
    const std::wstring binAmd = raizApp + L"bin-amd\\";
    if (comRaiz)
        atualizacao::Anotar(raiz + L"launcher.log", L"--raiz %ls (%ls): estado/log/cache em %ls",
                            raizApp.c_str(), raizGravavel ? L"aceita escrita" : L"SEM escrita", raiz.c_str());

    // O IDIOMA. E o que a pessoa escolheu - no app ou nesta tela -, e ele fica no
    // dlss5forall.ini do modulo, na mesma chave que o app le ([idioma] lingua=). Sem escolha
    // gravada em lugar nenhum, vale o idioma do Windows: a mesma regra do app (idioma.h), que
    // NUNCA para para perguntar. Por isso o seletor da tela e um par de chips e nao uma
    // pergunta - quem ja escolheu antes nao e interrogado de novo.
    // Le do bin NVIDIA e, se nao houver chave la, do bin AMD: um pacote pode vir so com um
    // dos dois, e ai a escolha esta no ini do que veio.
    const std::wstring iniNv  = binNv  + L"dlss5forall.ini";
    const std::wstring iniAmd = binAmd + L"dlss5forall.ini";
    if (!CarregarIdioma(iniNv) && !CarregarIdioma(iniAmd)) g_lingua = IdiomaDoSistema();

    // flags DESTE launcher: pulam a tela e nao seguem para o app
    std::wstring args;
    int direto = 0;   // 1 nvidia, 2 amd, 3 sem dlss
    std::wstring versaoAnterior;   // --atualizado=<versao>: acabamos de ser trocados pelo atualizar.cmd
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::wstring resto;
        for (int i = 1; argv && i < argc; i++) {
            const std::wstring a = argv[i];
            if (a == L"--nvidia")   { direto = 1; continue; }
            if (a == L"--amd")      { direto = 2; continue; }
            if (a == L"--sem-dlss") { direto = 3; continue; }
            if (a.rfind(L"--atualizado=", 0) == 0) { versaoAnterior = a.substr(13); continue; }
            if (a == L"--raiz") { ++i; continue; }   // v2.0: deste launcher, nao segue para o app
            if (!resto.empty()) resto += L" ";
            const bool aspas = a.find(L' ') != std::wstring::npos;
            resto += aspas ? (L"\"" + a + L"\"") : a;
        }
        if (argv) LocalFree(argv);
        args = (direto || !versaoAnterior.empty() || comRaiz) ? resto : ArgumentosSemPrograma();
    }
    // O modulo AMD recebe --amd: o app tambem detecta o modulo pelos arquivos do mod ao lado
    // do exe, mas a flag deixa isso explicito antes mesmo de o mod estar instalado (faixa certa
    // no seletor, textos certos).
    const std::wstring argsAmd = args.empty() ? std::wstring(L"--amd") : (args + L" --amd");
    // v2.1: o botao "Sem DLSS 5" (e --sem-dlss) PASSA a flag ao app: ele pula a sonda e abre com o Neural Render indisponivel
    // (Frame Gen e filtros normais). Na 2.0 a flag so existia aqui e abria o MESMO app de sempre, que tentava montar a rede.
    const std::wstring argsSemDlss = args.empty() ? std::wstring(L"--sem-dlss") : (args + L" --sem-dlss");

    // O marcador que o atualizar.cmd deixa ao terminar ("ok"/"falhou") vira o evento
    // atualizacao_ok/atualizacao_falhou - e lido (e apagado) antes de qualquer coisa.
    const std::wstring resultadoAnterior = atualizacao::LerResultadoAnterior(raiz);
    // As buscas na web (versao nova, publi, resumo do autor): threads proprias, a tela nao
    // espera. Sem url_base no ofertas.ini nao ha rede nenhuma (ver atualizacao.h).
    atualizacao::Verificacao verif;
    atualizacao::IniciarVerificacao(verif, raiz);
    if (!resultadoAnterior.empty()) atualizacao::EnviarEvento(verif, resultadoAnterior == L"ok" ? L"atualizacao_ok" : L"atualizacao_falhou");

    // O contador da aba da gaveta ("3 novas"): a thread escreve NESTA variavel, entao ela
    // vive aqui em cima e o `sair` espera por ela junto com as outras. Ver ThreadNovas.
    std::atomic<int> novas{ -1 };
    HANDLE thNovas = nullptr;

    // Sai do launcher: espera os eventos ja disparados (ate 2 s; o app ja esta aberto ou a
    // pessoa ja fechou, ninguem percebe) e devolve o codigo.
    auto sair = [&](int codigo) {
        if (thNovas) { WaitForSingleObject(thNovas, 300); CloseHandle(thNovas); thNovas = nullptr; }
        atualizacao::EncerrarVerificacao(verif);
        return codigo;
    };

    if (direto == 1) { atualizacao::SalvarModulo(raiz, L"nvidia"); return sair(Abrir(binNv, args)); }
    if (direto == 2) { atualizacao::SalvarModulo(raiz, L"amd");    return sair(Abrir(binAmd, argsAmd)); }
    if (direto == 3) { atualizacao::SalvarModulo(raiz, L"sem");    return sair(Abrir(binNv, argsSemDlss)); }

    // o que esta maquina tem
    Placas pl = EnumerarPlacas();
    // So para conferir a tela sem trocar de placa: DLSS5_LAUNCHER_FINGIR=nenhuma | amd | nvidia | ambas
    {
        wchar_t env[16]{};
        if (GetEnvironmentVariableW(L"DLSS5_LAUNCHER_FINGIR", env, 16) && env[0]) {
            const std::wstring f = env;
            pl.rtx    = (f == L"nvidia" || f == L"ambas");
            pl.radeon = (f == L"amd"    || f == L"ambas");
        }
    }
    ofertas::detail::LauncherInfo info;
    // AS NOTAS NAO REPETEM O NOME DA PLACA. Elas aparecem na tela precedidas por ele
    // ("AMD: ..."), entao uma nota escrita como "modulo AMD em desenvolvimento" saía como
    // "AMD: módulo AMD em desenvolvimento". Quem escreve o nome e a tela; a nota diz so o
    // motivo.
    //
    // TUDO ISTO DEPENDE DO IDIOMA (TS() por dentro), e por isso virou funcao em vez de
    // codigo solto: sem isto, a linha do topo ("RTX 4080 detectada · pronta") ficava presa
    // no idioma da ABERTURA para sempre, porque era a UNICA string da tela calculada uma
    // vez so e nunca refeita - o resto do launcher troca na hora porque passa por T()/TS()
    // no proprio desenho. Defeito medido pelo usuario em 11/09/2026: launcher trocado para
    // English mostrando "RTX 4080 detectada" ainda em portugues. Chamada de novo sempre que
    // a pessoa troca de lingua (ver aoTrocarIdioma logo abaixo).
    auto montarEstadoHardware = [&]() {
        info.nvidiaOk = pl.rtx && Existe(binNv + L"DLSS5ForAll.exe");
        info.nvidiaNota.clear();
        if (!pl.rtx) info.nvidiaNota = TS(L"não compatível com seu computador", L"not compatible with your computer");
        else if (!info.nvidiaOk) info.nvidiaNota = TS(L"módulo não encontrado (pasta bin)", L"module not found (bin folder)");

        info.amdOk = false;
        info.amdNota.clear();
        if (!pl.radeon)                                info.amdNota = TS(L"não compatível com seu computador", L"not compatible with your computer");
        else if (!TemRuntimeHip())                     info.amdNota = TS(L"atualize o driver Adrenalin (26.1 ou mais novo)", L"update the Adrenalin driver (26.1 or newer)");
        else if (!Existe(binAmd + L"DLSS5ForAll.exe")) info.amdNota = TS(L"módulo não está nesta instalação", L"module is not in this install");
        else info.amdOk = true;

        // A LINHA DE ESTADO DO HARDWARE, no alto da tela: a resposta a "o meu computador
        // serve?" antes de a pessoa clicar em nada.
        if (info.nvidiaOk) {
            info.estadoHardware = NomeCurtoDaPlaca(pl.nomeRtx) + TS(L" detectada · pronta", L" detected · ready");
            info.hwSinal = 1;
        } else if (info.amdOk) {
            // "AMD experimental", e nao "(beta)": o mod da AMD e desenvolvido em paralelo, ja
            // funciona e ainda pesa mais. A palavra escolhida pelo autor do projeto e essa, nas
            // duas linguas - "beta" promete uma versao quase pronta, "experimental" nao promete.
            // A frase que explica o que isso custa esta logo abaixo, na tela (ver FormaL::notaModo).
            info.estadoHardware = NomeCurtoDaPlaca(pl.nomeRadeon) + TS(L" · AMD experimental", L" · AMD experimental");
            info.hwSinal = 2;
        } else if (pl.rtx || pl.radeon) {
            info.estadoHardware = NomeCurtoDaPlaca(pl.rtx ? pl.nomeRtx : pl.nomeRadeon) + L" · " +
                                  (pl.rtx ? info.nvidiaNota : info.amdNota);
            info.hwSinal = 2;
        } else {
            info.estadoHardware = TS(L"nenhuma placa compatível — o app abre sem DLSS 5",
                                     L"no compatible card — the app opens without DLSS 5");
            info.hwSinal = 3;
        }
    };
    montarEstadoHardware();

    // A escolha feita nos chips da tela tem de valer PARA O APP, e nao so para o launcher:
    // cada modulo abre com a pasta dele como diretorio de trabalho e le o proprio
    // dlss5forall.ini, entao gravar num so deixaria o outro no idioma antigo. Grava nos dois
    // que existirem (a pasta ausente e o pacote que nao traz aquele modulo).
    info.aoTrocarIdioma = [&](Lingua l) {
        if (Existe(raizApp + L"bin"))     GravarIdioma(iniNv, l);
        if (Existe(raizApp + L"bin-amd")) GravarIdioma(iniAmd, l);
        montarEstadoHardware();   // a linha do topo tambem e texto, e tambem tem que trocar
        // A COLUNA DA VITRINE TAMBEM. Ela e uma pagina web que recebeu o idioma na query
        // quando abriu; sem este aviso, trocar para ingles deixava metade da tela em
        // portugues - justamente para quem o seletor foi feito. info.vitrine so esta
        // preenchido quando a coluna existe (rede, monitor largo, runtime presente), e
        // quando nao esta este ramo simplesmente nao roda. A lambda captura info por
        // referencia e so corre no clique, muito depois de o motor ter sido preparado.
        if (info.vitrine) info.vitrine->TrocarIdioma(CodigoLingua(l));
        // EM INGLES NAO HA OFERTAS (ver OfertasValem em splash_ofertas.h): a faixa vira o
        // convite para o canal, e buscar publi de oferta deixa de fazer sentido. Voltar ao
        // portugues religa a busca, e a thread ja acorda no proximo intervalo.
        verif.pollLigado.store((l == LINGUA_PT && info.faixaVisivel) ? 1 : 0);
        atualizacao::Anotar(raiz + L"launcher.log", L"idioma: trocado para %ls na tela do launcher (busca de ofertas %ls)",
                            CodigoLingua(l), verif.pollLigado.load() ? L"ligada" : L"parada");
    };

    // DETECTADA e diferente de OK: uma placa que existe nesta maquina entra no seletor mesmo
    // desligada, com o motivo curto do lado ("atualize o driver Adrenalin"). Uma que nao
    // existe aqui nao aparece - e por isso quem tem uma opcao so nao ve seletor nenhum.
    // (Nao depende de idioma - fica fora da funcao, calculada uma vez so.)
    info.nvidiaDetectada = pl.rtx;
    info.amdDetectada    = pl.radeon;
    info.versaoApp = DLSS5_VERSAO;

    // A pilula "atualizacao disponivel" e a linha de status leem o MESMO sinal que a caixa de
    // perguntar usa depois do INICIAR - a tela so avisa, nunca pergunta.
    info.versaoPronta = &verif.pronto;
    info.versaoNova   = &verif.manifesto.versao;

    // Acabamos de ser trocados pelo atualizar.cmd: a assinatura no pe da tela diz isso. E o
    // unico sinal de "deu certo" alem do log - e a pessoa ve que a tela e a mesma de sempre.
    if (!versaoAnterior.empty())
        info.notaVersao = std::wstring(L"DLSS5 For All ") + DLSS5_VERSAO +
                          TS(L" · atualizado agora (antes: ", L" · just updated (was: ") + versaoAnterior + L")";

    // o que a web ja respondeu (ou vai responder) entra na tela por aqui (ver LauncherInfo)
    if (!verif.remotoCache.Vazio()) info.remotoCache = &verif.remotoCache;
    info.remotoPronto = &verif.remotoPronto;
    info.remoto = &verif.remoto;
    info.cartao = verif.cartao;
    info.resumoPronto = &verif.resumoPronto;
    info.resumo = &verif.resumo;

    // A VITRINE (coluna da esquerda). Tudo que nao depende da tela se decide aqui; a tela so
    // ainda pode desistir por monitor estreito. A rede: espera ATE 1,5 s pela resposta do
    // versao.json (ou da sonda na propria vitrine, quando nao ha url_base) - se ela nao vier
    // (ou falhar), a tela abre de uma coluna e fica assim; nao se redimensiona depois, para a
    // janela nao pular na frente da pessoa.
    vitrine::Motor motor;
    std::wstring vitrineMotivo;
    bool vitrineOn = vitrine::Preparar(motor, verif, raiz, vitrineMotivo);
    ULONGLONG esperaRede = 0;
    if (vitrineOn) {
        const ULONGLONG t = GetTickCount64();
        while (verif.rede.load() == 0 && GetTickCount64() - t < 1500) Sleep(15);
        esperaRede = GetTickCount64() - t;
        const int r = verif.rede.load();
        if (r != 1) { vitrineOn = false; vitrineMotivo = r == 2 ? L"sem_rede" : L"rede_lenta"; }
    }
    if (vitrineOn) info.vitrine = &motor;
    atualizacao::Anotar(raiz + L"launcher.log", L"vitrine: %ls (motivo=%ls, rede em %llu ms)%ls%ls",
                        vitrineOn ? L"ligada" : L"desligada", vitrineOn ? L"-" : vitrineMotivo.c_str(),
                        (unsigned long long)esperaRede, vitrineOn ? L" url=" : L"", vitrineOn ? motor.url.c_str() : L"");

    // ------------------------------------------------------------------ A GAVETA
    // A vitrine deixou de ser uma coluna sempre acesa e virou uma gaveta com aba na lateral.
    // A DOSAGEM: no PRIMEIRO uso ela vem aberta (e a unica vez em que a pessoa nao sabe que
    // ela existe) e depois a cada N aberturas (vitrine_lembrar_a_cada=, padrao 10); nas
    // demais vale o que ela deixou da ultima vez. Sem vitrine (sem rede, sem WebView2) nao ha
    // gaveta nem aba: a tela e a de uma coluna, como sempre foi.
    int aberturas = 0;
    bool porLembrete = false;
    if (vitrineOn) {
        aberturas = _wtoi(atualizacao::LerEstado(raiz, L"aberturas", L"0").c_str());
        if (aberturas < 0 || aberturas > 1000000) aberturas = 0;
        aberturas++;
        atualizacao::GravarEstado(raiz, L"aberturas", std::to_wstring(aberturas));
        const int n = verif.cfg.vitrineLembrar;
        porLembrete = (aberturas == 1) || (n > 0 && (aberturas - 1) % n == 0);
        info.gavetaAberta = porLembrete || atualizacao::LerEstado(raiz, L"gaveta", L"0") == L"1";
        info.aoTrocarGaveta = [&raiz](bool aberta) {
            atualizacao::GravarEstado(raiz, L"gaveta", aberta ? L"1" : L"0");
            atualizacao::Anotar(raiz + L"launcher.log", L"gaveta: %ls pela pessoa", aberta ? L"aberta" : L"fechada");
        };
        // o contador da aba ("3 novas"), em thread propria: a tela nunca espera por ele
        if (!verif.cfg.urlVitrineJson.empty() && !atualizacao::UrlVitrineEhLocal(verif.cfg.urlVitrineJson)) {
            info.vitrineNovas = &novas;
            NovasArgs* na = new NovasArgs{ verif.cfg.urlVitrineJson, raiz + L"launcher.log", &novas };
            thNovas = CreateThread(nullptr, 0, ThreadNovas, na, 0, nullptr);
            if (!thNovas) delete na;
        }
    }

    // ------------------------------------------------------------ OCULTAR A FAIXA DO CANAL
    // A faixa de ofertas pode ser desligada pela pessoa, e desligada ela fica - e o que
    // separa um convite de um anuncio (o padrao enganoso "Nagging" e exatamente o convite que
    // volta depois de recusado). O estado mora no dlss5forall.ini da RAIZ, ao lado do da
    // gaveta, porque e do pacote e nao de um modulo. Padrao: visivel.
    info.faixaVisivel = atualizacao::LerEstado(raiz, L"faixa", L"1") != L"0";
    // A FAIXA SE ATUALIZA SOZINHA enquanto esta na tela: a thread da publi repete a busca e,
    // quando o item mais recente do grupo muda, a tela anima a troca. Quem desligou a faixa -
    // ou esta em ingles, onde nao ha ofertas - nao paga trafego por ela.
    info.geracaoRemoto = &verif.geracaoRemoto;
    verif.pollLigado.store((info.faixaVisivel && g_lingua == LINGUA_PT) ? 1 : 0);
    info.aoTrocarFaixa = [&raiz, &verif](bool visivel) {
        verif.pollLigado.store((visivel && g_lingua == LINGUA_PT) ? 1 : 0);
        atualizacao::GravarEstado(raiz, L"faixa", visivel ? L"1" : L"0");
        atualizacao::Anotar(raiz + L"launcher.log", L"faixa do canal: %ls pela pessoa (busca %ls)",
                            visivel ? L"reativada" : L"ocultada",
                            verif.pollLigado.load() ? L"ligada" : L"parada");
        // Dois eventos NOVOS, pelo mesmo caminho anonimo dos outros (so com contar=1 e
        // url_base; o servidor descarta em silencio um nome que nao conheca). Saber quanta
        // gente desliga a faixa e o unico jeito de descobrir se ela incomoda - e e por isso
        // que a chave existe: se muita gente desligar, o problema e a faixa, nao a pessoa.
        atualizacao::EnviarEvento(verif, visivel ? L"reativou_faixa" : L"ocultou_faixa");
    };

    SplashOpcoes op;
    op.esconderDaCaptura = false;   // ainda nao ha captura nossa para ela ecoar; e propaganda do canal
    // SO PARA TESTE (como o DLSS5_SPLASH_DPI da tela): DLSS5_LAUNCHER_AREA=l,t,r,b centra a
    // janela naquele retangulo de tela em vez da area de trabalho do monitor primario. Serve
    // para tirar as fotos de conferencia num monitor que nao seja o que a pessoa esta usando.
    {
        wchar_t d[8]{}, b[64]{};
        if (GetEnvironmentVariableW(L"DLSS5_DIAG", d, 8) && d[0] == L'1' &&
            GetEnvironmentVariableW(L"DLSS5_LAUNCHER_AREA", b, 64)) {
            RECT a{};
            if (swscanf(b, L"%ld,%ld,%ld,%ld", &a.left, &a.top, &a.right, &a.bottom) == 4 &&
                a.right > a.left && a.bottom > a.top) op.area = a;
        }
    }
    const ULONGLONG t0 = GetTickCount64();
    const ofertas::detail::Escolha e = ShowLauncherSplash(hi, op, info);
    const int fechadoPor = ofertas::detail::g_ultimoFechadoPor;
    // a tela pode ter desligado a coluna (monitor estreito): ai e "off" de verdade. Se o motor
    // falhou no meio (ambiente/controle/pagina), a coluna existiu com o aviso: fica "on" com o motivo.
    if (vitrineOn && motor.motivoOff.rfind(L"monitor_estreito", 0) == 0) { vitrineOn = false; vitrineMotivo = motor.motivoOff; }
    // Um evento por abertura em que a vitrine esteve NA TELA (os cliques DENTRO da pagina -
    // ver oferta, entrar no grupo - a propria pagina registra). Com a gaveta, "na tela"
    // deixou de ser "a coluna existe" e passou a ser "a gaveta chegou a ficar aberta": quem
    // nunca abriu a gaveta nunca viu a vitrine, e contar isso como visualizacao seria mentira.
    // Como os outros, so com contar=1 e url_base; o servidor descarta um nome que nao conheca.
    const bool gavetaAberta = vitrineOn && ofertas::detail::g_ultimaGavetaAberta;
    if (gavetaAberta) atualizacao::EnviarEvento(verif, L"abriu_vitrine");
    // Um log de uma linha por abertura (launcher.log, ao lado deste exe): o que a tela viu e
    // o que a pessoa escolheu. E o unico jeito de saber por que o app abriu de um jeito.
    // "vitrine=on|off motivo=..." e o pedaco da coluna da esquerda (carga = a pagina carregou?).
    {
        FILE* f = _wfopen((raiz + L"launcher.log").c_str(), L"a, ccs=UTF-8");
        if (f) {
            fwprintf(f, L"v%ls rtx=%d radeon=%d hip=%d nvidiaOk=%d amdOk=%d | escolha=%d fechadoPor=%d apos %llu ms | nv=\"%ls\" amd=\"%ls\" | versao=%d publi=%d resumo=%d | vitrine=%ls motivo=%ls carga=%ls links_fora=%d | gaveta=%ls abertura=%d novas=%d | | lingua=%ls\n",
                     DLSS5_VERSAO, (int)pl.rtx, (int)pl.radeon, (int)TemRuntimeHip(), (int)info.nvidiaOk, (int)info.amdOk,
                     (int)e, fechadoPor, (unsigned long long)(GetTickCount64() - t0),
                     info.nvidiaNota.c_str(), info.amdNota.c_str(), verif.pronto.load(), verif.remotoPronto.load(), verif.resumoPronto.load(),
                     vitrineOn ? L"on" : L"off", vitrineOn ? (motor.motivoOff.empty() ? L"-" : motor.motivoOff.c_str()) : vitrineMotivo.c_str(),
                     vitrineOn ? motor.Carga() : L"-", motor.linksFora.load(),
                     !vitrineOn ? L"sem" : gavetaAberta ? (porLembrete ? L"aberta(lembrete)" : L"aberta") : L"fechada",
                     aberturas, novas.load(), CodigoLingua(g_lingua));
            fclose(f);
        }
    }

    // O clique vira um evento anonimo (so com contar=1 e url_base; ver atualizacao.h).
    const wchar_t* evento = nullptr;
    switch (e) {
    case ofertas::detail::ESC_NVIDIA:   evento = L"clique_nvidia"; break;
    case ofertas::detail::ESC_AMD:      evento = L"clique_amd"; break;
    case ofertas::detail::ESC_SEM_DLSS: evento = L"clique_sem_dlss"; break;
    case ofertas::detail::ESC_LINK:     evento = fechadoPor == 5 ? L"clique_canal" : fechadoPor == 12 ? L"clique_cupom" : L"clique_grupo"; break;
    default:                            evento = L"fechou_sem_escolher"; break;
    }
    atualizacao::EnviarEvento(verif, evento);

    // Um botao de INICIAR: e AQUI, e so aqui, que a pergunta de versao nova pode aparecer.
    // Se a verificacao ainda nao respondeu, nao espera - o programa abre sem pergunta.
    // Quem fechou sem escolher (X/Esc) nunca ve a pergunta.
    if (e == ofertas::detail::ESC_NVIDIA || e == ofertas::detail::ESC_AMD || e == ofertas::detail::ESC_SEM_DLSS) {
        const wchar_t* modulo = e == ofertas::detail::ESC_NVIDIA ? L"nvidia" : e == ofertas::detail::ESC_AMD ? L"amd" : L"sem";
        const wchar_t* flag   = e == ofertas::detail::ESC_NVIDIA ? L"--nvidia" : e == ofertas::detail::ESC_AMD ? L"--amd" : L"--sem-dlss";
        atualizacao::SalvarModulo(raiz, modulo);
        // v2.0: com --raiz sem escrita nao ha como trocar os arquivos do pacote: nao oferece a atualizacao
        if (verif.pronto.load() == 1 && raizGravavel) {
            bool automatica = verif.automatica;
            int resposta;
            if (automatica && verif.podeAuto) {
                // caixa marcada: nao pergunta; a janela de progresso e o aviso curto na tela
                resposta = 1;
            } else {
                // a caixa padrao do Windows: "Sim, atualizar" / "Agora nao" + a caixa de verificacao
                resposta = atualizacao::PerguntarAtualizar(verif.manifesto, automatica, verif.cfg.urlNovidades);
                atualizacao::SalvarAutomatica(raiz, automatica);
                if (resposta != 1) atualizacao::GravarEstado(raiz, L"adiada", verif.manifesto.versao);   // so registro: a proxima abertura pergunta de novo
            }
            if (resposta == 1) {
                atualizacao::EnviarEvento(verif, L"clique_atualizar");
                // Baixa, confere e entrega ao atualizar.cmd; se deu certo, SAIMOS ja - o .cmd
                // espera este processo terminar, troca os arquivos e reabre o launcher com a
                // MESMA escolha (a flag), que abre o programa direto. Se falhou, nada foi
                // trocado: a mensagem diz o motivo e onde esta o log, e o programa abre normal.
                std::wstring erro, log;
                if (atualizacao::BaixarEAplicar(hi, raiz, verif.manifesto, automatica && verif.podeAuto, flag, erro, log))
                    return sair(0);
                if (!erro.empty()) {
                    atualizacao::EnviarEvento(verif, L"atualizacao_falhou");
                    MessageBoxW(nullptr, (erro + TS(L"\n\nDetalhes em:\n", L"\n\nDetails in:\n") + log).c_str(),
                                L"DLSS5 For All", MB_ICONWARNING | MB_OK);
                }
            }
        }
        if (e == ofertas::detail::ESC_AMD) return sair(Abrir(binAmd, argsAmd));
        return sair(Abrir(binNv, e == ofertas::detail::ESC_SEM_DLSS ? argsSemDlss : args));   // v2.1: "Sem DLSS 5" leva --sem-dlss ao app
    }
    if (e == ofertas::detail::ESC_LINK) {
        // abriu o grupo, o canal ou o cupom: o app abre assim mesmo, do jeito que der, como a
        // tela classica sempre fez (ela fechava e o app seguia)
        return sair(info.nvidiaOk ? Abrir(binNv, args) : info.amdOk ? Abrir(binAmd, argsAmd) : Abrir(binNv, args));
    }
    return sair(0);   // X ou Esc: a pessoa desistiu, nada abre
}
