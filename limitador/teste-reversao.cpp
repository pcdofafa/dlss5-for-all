// =====================================================================================
//  teste-reversao.cpp - exercita INSTALAR e REMOVER sem jogo, sem placa e sem o app.
//
//  POR QUE ESTE TESTE EXISTE, e a frase que o resume: guarda que nunca reprovou nada nao foi
//  verificada. Os tres defeitos que este teste cobre nao davam erro nenhum - davam a MENSAGEM
//  ERRADA, que e o unico modo de falha em que a pessoa se engana em vez de se frustrar:
//
//    1. remover com o jogo aberto dizia "nao havia nada nosso para remover ali", e o arquivo
//       continuava la;
//    2. meia instalacao (uma atualizacao do jogo apagando so a peca) fazia o app ESQUECER que
//       tinha instalado, e o resto ficava orfa na pasta da pessoa para sempre;
//    3. a mesma meia instalacao fazia o app tratar a PROPRIA casca como dxgi.dll de outra
//       pessoa e recusar reinstalar - a pessoa ficava sem instalar e sem remover.
//
//  Nada aqui toca jogo, placa de video ou a pasta de ninguem: o teste monta uma pasta de
//  mentira no temporario, faz tudo dentro dela e apaga no fim.
//
//  COMPILAR:  bash build-testes.sh
//  RODAR:     ./teste-reversao.exe          (sai com 0 se tudo passou)
//
//  ---------------------------------------------------------------------------------------
//  COMO CONFERIR O PROPRIO TESTE, e isto nao e opcional: um teste que so viu codigo consertado
//  nunca provou que enxerga o defeito. Antes de acreditar nos "ok" abaixo, reponha os defeitos
//  numa COPIA (nunca no original) e confirme que ele reprova:
//
//    1. copie limitador\ para uma pasta descartavel, com build\ dentro;
//    2. na copia de fpslimit_host.h, desligue a pergunta que abre a remocao:
//         if (JogoAbertoNaPasta(...))   ->   if (false && JogoAbertoNaPasta(...))
//       e faca AlgoNossoNaPasta() voltar a responder pela instalacao inteira:
//         primeira linha do corpo   ->   return CascaEhNossa(pastaJogo);
//    3. compile e rode o teste NA COPIA.
//
//  Feito em 07/09/2026, e o resultado foi 6 reprovacoes em 40 verificacoes, com saida 1: uma no
//  cenario 3 e cinco no cenario 5. Se um dia essa conferencia passar com os defeitos repostos,
//  o problema esta neste arquivo, e nao no codigo que ele examina.
//  ---------------------------------------------------------------------------------------
// =====================================================================================
#include <windows.h>
#include <cstdio>
#include <string>
#include "fpslimit_host.h"

static int g_falhas = 0, g_testes = 0;

static void Checar(bool ok, const char* oque) {
    g_testes++;
    if (!ok) { g_falhas++; printf("  FALHOU  %s\n", oque); }
    else       printf("  ok      %s\n", oque);
}

static std::wstring TempUnico() {
    wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t);
    std::wstring d = std::wstring(t) + L"dlss5-teste-reversao-" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

static void Escrever(const std::wstring& p, const char* txt) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD n = 0; WriteFile(h, txt, (DWORD)strlen(txt), &n, nullptr);
    CloseHandle(h);
}

static void ApagarPasta(const std::wstring& d) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((d + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ApagarPasta(d + n + L"\\");
            else DeleteFileW((d + n).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(d.c_str());
}

// Limpa a pasta do jogo de mentira entre um cenario e outro, sem passar pelo codigo que esta
// sendo testado - senao um defeito no Remover() esconderia o cenario seguinte.
static void ZerarJogo(const std::wstring& jogo) {
    for (const wchar_t* n : { FpsLimit::kNomeCasca, FpsLimit::kNomePeca, FpsLimit::kNomeLeiaMe }) {
        DeleteFileW((jogo + n).c_str());
        DeleteFileW((jogo + n + FpsLimit::kSufixoRemover).c_str());
    }
}

int wmain(int argc, wchar_t** argv) {
    // O PROPRIO TESTE FAZ AS VEZES DE JOGO ABERTO. Uma copia dele e posta na pasta de mentira e
    // aberta com --dormir; assim ha um processo REAL rodando de dentro daquela pasta, que e
    // exatamente o que JogoAbertoNaPasta() procura. Nada de simular: o caso que ja enganou a
    // gente uma vez merece ser reproduzido de verdade.
    if (argc > 1 && std::wstring(argv[1]) == L"--dormir") { Sleep(20000); return 0; }

    FpsLimit::g_logHost = [](const char* s) { printf("        | %s\n", s); };

    wchar_t meuExe[MAX_PATH]; GetModuleFileNameW(nullptr, meuExe, MAX_PATH);
    const std::wstring pastaEu = FpsLimit::PastaDe(meuExe);
    const std::wstring origem  = pastaEu + L"build\\";          // as pecas de verdade
    const std::wstring inerte  = pastaEu + L"teste-inerte.dll"; // a DLL que so ocupa lugar

    if (!FpsLimit::Existe(origem + FpsLimit::kNomeCasca) ||
        !FpsLimit::Existe(origem + FpsLimit::kNomePeca)) {
        printf("as pecas nao estao em build\\ - rode build-limitador.sh antes\n");
        return 2;
    }

    const std::wstring base = TempUnico();
    const std::wstring jogo = base + L"jogo\\";
    CreateDirectoryW(jogo.c_str(), nullptr);
    wprintf(L"pasta de mentira: %ls\n\n", jogo.c_str());

    // ---------------------------------------------------------------- 1. instalar do zero
    printf("1. instalar numa pasta limpa\n");
    {
        std::wstring usada;
        const int r = FpsLimit::Instalar(origem, jogo + L"jogo.exe", &usada);
        Checar(r == FpsLimit::INST_OK, "devolve INST_OK");
        Checar(FpsLimit::Existe(jogo + FpsLimit::kNomeCasca), "a casca ficou na pasta");
        Checar(FpsLimit::Existe(jogo + FpsLimit::kNomePeca), "a peca ficou na pasta");
        Checar(FpsLimit::Existe(jogo + FpsLimit::kNomeLeiaMe), "o LEIA-ME ficou na pasta");
        Checar(FpsLimit::CascaEhNossaMesmo(jogo, origem), "a casca e reconhecida como nossa, byte a byte");
        const int r2 = FpsLimit::Instalar(origem, jogo + L"jogo.exe", &usada);
        Checar(r2 == FpsLimit::INST_JA_ESTAVA, "instalar de novo devolve INST_JA_ESTAVA");
    }

    // ---------------------------------------------------------------- 2. remover normal
    printf("\n2. remover com o jogo fechado\n");
    {
        const FpsLimit::Remocao r = FpsLimit::Remover(jogo, origem);
        Checar(r.resultado == FpsLimit::REM_OK, "devolve REM_OK");
        Checar(r.apagados == 3, "saiu os tres arquivos");
        Checar(!FpsLimit::AlgoNossoNaPasta(jogo, origem), "nao sobrou nada nosso");
        const FpsLimit::Remocao r2 = FpsLimit::Remover(jogo, origem);
        Checar(r2.resultado == FpsLimit::REM_NADA_LA, "remover de novo devolve REM_NADA_LA");
    }

    // ---------------------------------------------------------------- 3. meia instalacao
    // O DEFEITO ORIGINAL. Uma atualizacao do jogo apaga so a peca e deixa a casca. Antes, o app
    // perguntava se a instalacao estava INTEIRA, ouvia "nao", esquecia o registro, e a casca
    // ficava orfa - alem de o proprio arquivo passar a ser tratado como de outra pessoa.
    printf("\n3. meia instalacao: a peca sumiu e a casca ficou\n");
    {
        ZerarJogo(jogo);
        std::wstring usada;
        FpsLimit::Instalar(origem, jogo + L"jogo.exe", &usada);
        DeleteFileW((jogo + FpsLimit::kNomePeca).c_str());     // o jogo se atualizou por cima

        Checar(!FpsLimit::CascaEhNossa(jogo), "a conferencia de instalacao INTEIRA diz nao, e esta certa");
        Checar(FpsLimit::AlgoNossoNaPasta(jogo, origem), "mas ha vestigio nosso, e ele e reconhecido");
        const int r = FpsLimit::Instalar(origem, jogo + L"jogo.exe", &usada);
        Checar(r != FpsLimit::INST_OCUPADO, "reinstalar NAO acusa dxgi.dll de outra pessoa");
        Checar(r == FpsLimit::INST_OK, "reinstalar completa a instalacao");
        Checar(FpsLimit::Existe(jogo + FpsLimit::kNomePeca), "a peca voltou");

        // e a limpeza tambem funciona a partir da meia instalacao
        DeleteFileW((jogo + FpsLimit::kNomePeca).c_str());
        const FpsLimit::Remocao rr = FpsLimit::Remover(jogo, origem);
        Checar(rr.resultado == FpsLimit::REM_OK, "remover a meia instalacao devolve REM_OK");
        Checar(!FpsLimit::AlgoNossoNaPasta(jogo, origem), "a pasta ficou limpa");
    }

    // ---------------------------------------------------------------- 4. dxgi.dll de outro
    printf("\n4. ha um dxgi.dll que nao e nosso (ReShade, um mod)\n");
    {
        ZerarJogo(jogo);
        Escrever(jogo + FpsLimit::kNomeCasca, "isto aqui e de outra pessoa, nao encoste");
        std::wstring usada;
        const int r = FpsLimit::Instalar(origem, jogo + L"jogo.exe", &usada);
        Checar(r == FpsLimit::INST_OCUPADO, "instalar recusa com INST_OCUPADO");
        Checar(!FpsLimit::CascaEhNossaMesmo(jogo, origem), "a casca alheia nao passa pela prova byte a byte");

        // com a nossa peca ao lado da casca alheia, a remocao tira a nossa e nao encosta na dela
        Escrever(jogo + FpsLimit::kNomePeca, "nossa");
        Escrever(jogo + FpsLimit::kNomeLeiaMe, "nosso");
        const FpsLimit::Remocao rr = FpsLimit::Remover(jogo, origem);
        Checar(rr.cascaAlheiaMantida, "a remocao avisa que havia uma casca de outra pessoa");
        Checar(FpsLimit::Existe(jogo + FpsLimit::kNomeCasca), "o dxgi.dll alheio CONTINUA LA");
        Checar(!FpsLimit::Existe(jogo + FpsLimit::kNomePeca), "a nossa peca saiu");
        DeleteFileW((jogo + FpsLimit::kNomeCasca).c_str());
    }

    // ---------------------------------------------------------------- 5. o jogo esta aberto
    // O PIOR DOS TRES DEFEITOS. Aqui o teste abre um processo DE VERDADE de dentro da pasta.
    printf("\n5. remover com o jogo aberto\n");
    {
        ZerarJogo(jogo);
        std::wstring usada;
        FpsLimit::Instalar(origem, jogo + L"jogo.exe", &usada);

        const std::wstring falso = jogo + L"jogo-de-mentira.exe";
        CopyFileW(meuExe, falso.c_str(), FALSE);
        std::wstring cmd = L"\"" + falso + L"\" --dormir";
        STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
        const BOOL subiu = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                                          CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        Checar(subiu == TRUE, "o jogo de mentira subiu");
        if (subiu) {
            Sleep(300);
            std::wstring quem;
            Checar(FpsLimit::JogoAbertoNaPasta(jogo, &quem), "o programa percebe que ha algo rodando dali");

            const FpsLimit::Remocao rr = FpsLimit::Remover(jogo, origem);
            Checar(rr.resultado == FpsLimit::REM_JOGO_ABERTO, "devolve REM_JOGO_ABERTO, e nao 'nao havia nada'");
            Checar(rr.apagados == 0, "NAO apagou nada - a pasta nao fica pela metade");
            Checar(!rr.quemSegura.empty(), "diz QUEM esta segurando, para a frase poder nomear");
            Checar(FpsLimit::Existe(jogo + FpsLimit::kNomeCasca), "a casca continua la, como a pessoa vai ver");

            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 3000);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            Sleep(200);

            const FpsLimit::Remocao r2 = FpsLimit::Remover(jogo, origem);
            Checar(r2.resultado == FpsLimit::REM_OK, "com o jogo fechado, a mesma remocao funciona");
        }
        DeleteFileW(falso.c_str());
    }

    // ---------------------------------------------------------------- 6. arquivo carregado
    // O Windows nao apaga imagem mapeada, mas deixa renomear. E o caminho da remocao adiada.
    printf("\n6. a peca esta CARREGADA: apagar nao da, renomear da\n");
    {
        ZerarJogo(jogo);
        std::wstring usada;
        FpsLimit::Instalar(origem, jogo + L"jogo.exe", &usada);
        if (!FpsLimit::Existe(inerte)) {
            printf("  (pulado: teste-inerte.dll nao foi compilada)\n");
        } else {
            CopyFileW(inerte.c_str(), (jogo + FpsLimit::kNomePeca).c_str(), FALSE);
            HMODULE m = LoadLibraryW((jogo + FpsLimit::kNomePeca).c_str());
            Checar(m != nullptr, "a peca de mentira foi carregada neste processo");
            if (m) {
                Checar(DeleteFileW((jogo + FpsLimit::kNomePeca).c_str()) == FALSE,
                       "o Windows realmente recusa apagar (a premissa do conserto)");
                const FpsLimit::Remocao rr = FpsLimit::Remover(jogo, origem);
                Checar(rr.resultado == FpsLimit::REM_ADIADO, "devolve REM_ADIADO");
                Checar(rr.adiados >= 1, "conta pelo menos um adiado");
                Checar(!FpsLimit::Existe(jogo + FpsLimit::kNomePeca),
                       "o nome antigo sumiu: o jogo nao carrega mais nada nosso");
                Checar(FpsLimit::Existe(jogo + FpsLimit::kNomePeca + FpsLimit::kSufixoRemover),
                       "o arquivo esta la com o sufixo de remocao");
                FreeLibrary(m);
                const int n = FpsLimit::LimparPendencias(jogo);
                Checar(n >= 1, "a abertura seguinte apaga o que ficou");
                Checar(!FpsLimit::AlgoNossoNaPasta(jogo, origem), "a pasta ficou limpa");
            }
        }
    }

    // ---------------------------------------------------------------- 7. queda no meio
    // O app grava o registro ANTES de tocar na pasta, entao o estado possivel depois de uma
    // queda e "ha meia coisa la e o registro sabe onde". A varredura da abertura chama
    // exatamente este Remover(); aqui se prova que ele da conta de cada meio estado.
    printf("\n7. o app morreu no meio da instalacao\n");
    {
        const wchar_t* nomes[3] = { FpsLimit::kNomePeca, FpsLimit::kNomeLeiaMe, FpsLimit::kNomeCasca };
        for (int ate = 1; ate <= 3; ate++) {
            ZerarJogo(jogo);
            // reproduz a ordem real de Instalar(): peca, LEIA-ME, casca
            for (int i = 0; i < ate; i++)
                CopyFileW((origem + FpsLimit::kNomeCasca).c_str(), (jogo + nomes[i]).c_str(), FALSE);
            char oque[96]; snprintf(oque, sizeof oque, "caiu apos %d arquivo(s): a limpeza da conta", ate);
            const FpsLimit::Remocao rr = FpsLimit::Remover(jogo, origem);
            Checar(rr.resultado == FpsLimit::REM_OK && !FpsLimit::AlgoNossoNaPasta(jogo, origem), oque);
        }
    }

    ApagarPasta(base);
    printf("\n%d verificacoes, %d falha(s)\n", g_testes, g_falhas);
    return g_falhas ? 1 : 0;
}
