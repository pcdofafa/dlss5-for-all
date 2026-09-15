// =====================================================================================
//  fpslimit_wrapper.cpp - a CASCA que o jogo carrega, e que nao faz nada alem de repassar.
//
//  POR QUE UMA CASCA E NAO INJECAO. Para o limitador agir, algum codigo nosso precisa estar
//  dentro do processo do jogo. Ha dois jeitos:
//
//    A) o jogo carrega sozinho uma biblioteca que esta na pasta dele, pela ordem de busca
//       normal do Windows - que e o que este arquivo faz;
//    B) a gente abre o processo do jogo, escreve na memoria dele e cria uma thread remota.
//
//  O (B) aplica sem reabrir o jogo, e e exatamente o padrao que os sistemas antitrapaca tratam
//  como ataque. O (A) custa UMA reabertura, e depois disso todo clique no painel vale na hora,
//  porque a peca ja esta dentro. A diferenca de risco e grande e a de experiencia e pequena: o
//  que o usuario recusou foi reabrir o jogo A CADA MUDANCA DE VALOR, que e o defeito do
//  caminho pelo driver, e nao reabrir uma vez.
//
//  O QUE ESTA CASCA PRECISA FAZER, e e mais delicado do que parece: ela toma o NOME de uma
//  biblioteca do sistema que o jogo espera encontrar. Se ela nao entregar exatamente o que a
//  original entregaria, o jogo NAO ABRE.
//
//  POR QUE O REPASSE E EXPLICITO E NAO PELO LINKADOR. A forma curta seria a diretiva de export
//  forwarding do compilador da Microsoft, uma linha por funcao. Este projeto compila com
//  MinGW, que nao tem essa diretiva; e o equivalente por arquivo .def encaminharia para "dxgi",
//  nome que o Windows resolveria procurando na PASTA DO JOGO primeiro - onde encontraria esta
//  propria casca, num laco que trava o jogo na abertura. Por isso cada funcao e repassada a
//  mao, para a original carregada por CAMINHO ABSOLUTO de System32.
//
//  Sao cinco funcoes: os pontos de entrada publicos da dxgi.dll que jogos usam. Os internos
//  (comecados por sublinhado) ficam de fora porque nenhum jogo os chama por nome.
//
//  ONDE ELA E INSTALADA. Ao lado do executavel do jogo, como "dxgi.dll". O app RECUSA instalar
//  se ja houver um arquivo com esse nome: pode ser um ReShade ou um mod que a pessoa colocou,
//  e sobrescrever aquilo seria destruir o trabalho de outra pessoa sem perguntar.
// =====================================================================================
#include <windows.h>

static HMODULE g_orig = nullptr;

// Carrega a dxgi VERDADEIRA, por caminho absoluto. Absoluto e obrigatorio: com o nome solto, a
// ordem de busca acharia esta casca de novo.
static HMODULE Original() {
    if (g_orig) return g_orig;
    wchar_t sys[MAX_PATH]{};
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (!n || n >= MAX_PATH - 12) return nullptr;
    wcscat(sys, L"\\dxgi.dll");
    g_orig = LoadLibraryW(sys);
    return g_orig;
}

// Pega o endereco de uma funcao na original, uma vez, e guarda.
static FARPROC Proc(const char* nome) {
    HMODULE m = Original();
    return m ? GetProcAddress(m, nome) : nullptr;
}

// Cada repasse segue o mesmo molde: resolve na primeira chamada, guarda, e chama. Se a original
// nao tiver a funcao (Windows mais antigo), devolve E_NOTIMPL em vez de estourar - o jogo trata
// isso como "recurso indisponivel", que e verdade, em vez de morrer com acesso invalido.
#define REPASSE(ret, nome, params, args, falha)                                   \
    typedef ret (WINAPI* PFN_##nome) params;                                      \
    extern "C" __declspec(dllexport) ret WINAPI nome params {                     \
        static PFN_##nome f = (PFN_##nome)(void*)Proc(#nome);                     \
        if (!f) return falha;                                                     \
        return f args;                                                            \
    }

REPASSE(HRESULT, CreateDXGIFactory,  (REFIID riid, void** pp), (riid, pp), E_NOTIMPL)
REPASSE(HRESULT, CreateDXGIFactory1, (REFIID riid, void** pp), (riid, pp), E_NOTIMPL)
REPASSE(HRESULT, CreateDXGIFactory2, (UINT flags, REFIID riid, void** pp), (flags, riid, pp), E_NOTIMPL)
REPASSE(HRESULT, DXGIGetDebugInterface1, (UINT flags, REFIID riid, void** pp), (flags, riid, pp), E_NOTIMPL)
REPASSE(HRESULT, DXGIDeclareAdapterRemovalSupport, (void), (), E_NOTIMPL)

// A casca so existe para que a NOSSA biblioteca entre junto. Ela e carregada aqui, e nao
// embutida nesta, por dois motivos:
//   - o nome fica honesto: quem abrir a pasta do jogo ve "dlss5-fpslimit.dll" ao lado da casca
//     e entende o que aquilo e, em vez de encontrar so uma dxgi.dll estranha;
//   - se a nossa biblioteca falhar ao carregar, a casca continua repassando e o jogo abre
//     normalmente, sem limite. A falha nao chega no jogo.
BOOL WINAPI DllMain(HINSTANCE inst, DWORD motivo, LPVOID) {
    if (motivo == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        // O caminho vem de ONDE ESTA CASCA ESTA, e nao do diretorio de trabalho do jogo, que
        // pode ser outro. Caminho completo tambem evita que a ordem de busca encontre outra
        // biblioteca de mesmo nome em outro lugar.
        wchar_t caminho[MAX_PATH]{};
        if (GetModuleFileNameW(inst, caminho, MAX_PATH)) {
            wchar_t* barra = wcsrchr(caminho, L'\\');
            if (barra) {
                wcscpy(barra + 1, L"dlss5-fpslimit.dll");
                // Sem conferir o retorno, de proposito: se nao carregar, o jogo roda sem limite,
                // que e o comportamento correto de falha. Nada aqui pode impedir o jogo de abrir.
                LoadLibraryW(caminho);
            }
        }
    }
    return TRUE;
}
