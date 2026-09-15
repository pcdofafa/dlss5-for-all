// =====================================================================================
//  fpslimit_guard.h - a recusa. Roda DENTRO do processo do jogo, antes de enganchar nada.
//
//  ESTA E A PROTECAO DE VERDADE, e nao o aviso da tela. O aviso depende de a pessoa ler; isto
//  nao depende de nada. Se houver sistema antitrapaça carregado neste processo, a peca NAO
//  engancha, vira repasse puro e o jogo roda exatamente como rodaria sem nos.
//
//  POR QUE DE DENTRO E MELHOR QUE DE FORA. As checagens que o app faz antes de instalar - olhar
//  a pasta do jogo, os servicos da maquina, os modulos do processo alvo - todas podem falhar:
//  um jogo protegido recusa inspecao de fora, uma protecao carrega so depois do login, um jogo
//  novo usa algo que nao esta na lista. Daqui de dentro a pergunta e trivial e o sistema sempre
//  responde: GetModuleHandleW nao precisa de direito nenhum sobre o proprio processo. Esta e a
//  ultima linha de defesa e a unica que nao pode ser contornada por engano nem por pressa.
//
//  O QUE ESTE ARQUIVO DELIBERADAMENTE NAO FAZ. Nao esconde a peca, nao troca o nome dela, nao
//  a carrega por fora do mecanismo normal do Windows, nao adia a checagem para depois de o
//  antitrapaça terminar de subir. Tudo isso existe e serve para escapar de deteccao, e e
//  exatamente o que separa uma ferramenta legitima de uma trapaca. Se uma protecao detectar a
//  nossa peca e bloquear, esse e o resultado CERTO e a gente aceita. A lista abaixo existe para
//  proteger quem usa recusando, jamais para contornar.
//
//  LIMITE HONESTO: isto nao e completo e nao pode ser. Protecao que so roda no servidor, que
//  julga por comportamento, ou que ainda nao conhecemos passa despercebida. Por isso o aviso
//  na tela continua obrigatorio mesmo quando aqui nao acha nada.
// =====================================================================================
#pragma once
#include <windows.h>
#include "fpslimit_shared.h"

namespace FpsLimit {

// Os modulos que cada sistema carrega dentro do processo do jogo. Nomes publicos, os mesmos
// que aparecem em qualquer lista de processos. A busca e por modulo JA CARREGADO neste
// processo: GetModuleHandleW nao carrega nada, so pergunta.
struct AssinaturaProt {
    Protecao      bit;
    const wchar_t* modulos[6];   // terminado por nullptr
};

static const AssinaturaProt kAssinaturas[] = {
    { PROT_EASYANTICHEAT, { L"EasyAntiCheat.dll", L"EasyAntiCheat_x64.dll", L"EasyAntiCheat_EOS.dll",
                            L"EasyAntiCheat_EOS_x64.dll", nullptr } },
    { PROT_BATTLEYE,      { L"BEClient.dll", L"BEClient_x64.dll", L"BEDaisy.sys", nullptr } },
    { PROT_VANGUARD,      { L"vgc.dll", L"vgk.sys", nullptr } },
    { PROT_XIGNCODE,      { L"xhunter1.sys", L"XIGNCODE.dll", L"x3.xem", nullptr } },
    { PROT_GAMEGUARD,     { L"GameGuard.des", L"npggNT.des", L"GameMon.des", nullptr } },
    { PROT_PUNKBUSTER,    { L"pbcl.dll", L"pbsv.dll", nullptr } },
    { PROT_DENUVO_AC,     { L"denuvo-anti-cheat.dll", L"AntiCheatRuntime.dll", nullptr } },
};

// Fragmentos genericos, para o caso de um sistema que nao esta na lista acima. Casam por
// SUBSTRING no nome do modulo, entao sao propositalmente poucos e especificos: um fragmento
// solto demais (por exemplo "guard") daria falso positivo em biblioteca inocente, e um falso
// positivo aqui nao e inofensivo - ele desliga um recurso que a pessoa pediu, sem explicacao
// util. Na duvida entre recusar demais e recusar de menos, este arquivo recusa demais, porque
// o custo de errar para o lado seguro e um recurso indisponivel, e para o outro lado e a conta
// da pessoa no jogo.
static const wchar_t* kFragmentosGenericos[] = {
    L"anticheat", L"anti-cheat", L"battleye", L"easyanti", L"punkbuster", nullptr
};

static void MinusculasW(wchar_t* s) { for (; *s; ++s) *s = (wchar_t)towlower(*s); }

// Varre os modulos JA CARREGADOS neste processo. Devolve a mascara do que achou; 0 = limpo.
// Nao carrega nada, nao abre processo, nao precisa de privilegio: e a lista de modulos do
// proprio processo, que o carregador do Windows mantem e sempre responde.
inline uint32_t ProtecoesNesteProcesso() {
    uint32_t achou = 0;

    // 1. nomes exatos, o caminho confiavel
    for (const auto& a : kAssinaturas) {
        for (int i = 0; a.modulos[i]; i++) {
            if (GetModuleHandleW(a.modulos[i])) { achou |= (uint32_t)a.bit; break; }
        }
    }

    // 2. varredura generica pelos nomes dos modulos carregados, para o que nao esta na lista.
    //    EnumProcessModules sobre o proprio processo nao precisa de direito extra. Se ela
    //    falhar por qualquer motivo, seguimos com o que a etapa 1 achou - uma falha aqui NAO
    //    pode virar "limpo" por omissao, e por isso a etapa 1 vem primeiro e independe desta.
    HMODULE psapi = LoadLibraryW(L"psapi.dll");
    if (psapi) {
        typedef BOOL (WINAPI* PFN_Enum)(HANDLE, HMODULE*, DWORD, LPDWORD);
        typedef DWORD (WINAPI* PFN_Nome)(HANDLE, HMODULE, LPWSTR, DWORD);
        auto enumMod = (PFN_Enum)(void*)GetProcAddress(psapi, "EnumProcessModules");
        auto nomeMod = (PFN_Nome)(void*)GetProcAddress(psapi, "GetModuleBaseNameW");
        if (enumMod && nomeMod) {
            HMODULE mods[512]; DWORD precisa = 0;
            if (enumMod(GetCurrentProcess(), mods, sizeof mods, &precisa)) {
                const DWORD n = precisa / sizeof(HMODULE);
                const DWORD lim = n < 512 ? n : 512;
                for (DWORD i = 0; i < lim; i++) {
                    wchar_t nome[MAX_PATH] = {};
                    if (!nomeMod(GetCurrentProcess(), mods[i], nome, MAX_PATH)) continue;
                    MinusculasW(nome);
                    for (int f = 0; kFragmentosGenericos[f]; f++)
                        if (wcsstr(nome, kFragmentosGenericos[f])) { achou |= (uint32_t)PROT_OUTRA; break; }
                }
            }
        }
        FreeLibrary(psapi);
    }
    return achou;
}

// Texto curto para o painel e para o motivo do bloco. Sem jargao e sem codigo de erro.
inline const wchar_t* NomeDaProtecao(uint32_t mascara) {
    if (mascara & PROT_EASYANTICHEAT) return L"Easy Anti-Cheat";
    if (mascara & PROT_BATTLEYE)      return L"BattlEye";
    if (mascara & PROT_VANGUARD)      return L"Riot Vanguard";
    if (mascara & PROT_XIGNCODE)      return L"XIGNCODE3";
    if (mascara & PROT_GAMEGUARD)     return L"nProtect GameGuard";
    if (mascara & PROT_PUNKBUSTER)    return L"PunkBuster";
    if (mascara & PROT_DENUVO_AC)     return L"Denuvo Anti-Cheat";
    if (mascara & PROT_OUTRA)         return L"um sistema antitrapaca";
    return L"";
}

} // namespace FpsLimit
