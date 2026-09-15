// =====================================================================================
//  fpslimit_shared.h - o CONTRATO entre o DLSS5ForAll.exe e a peca que roda dentro do jogo.
//
//  Este arquivo e incluido pelos DOIS lados e nao depende de mais nada alem de <windows.h>.
//  Se ele mudar, os dois lados precisam ser recompilados juntos: por isso existe o campo
//  `versao`, e por isso o lado de fora RECUSA um bloco de versao diferente em vez de tentar
//  interpretar. Um contrato lido errado e pior do que contrato nenhum.
//
//  POR QUE MEMORIA COMPARTILHADA E NAO UM CANAL DE VERDADE. O valor do limite e lido UMA VEZ
//  POR QUADRO, dentro do caminho quente do jogo. Qualquer coisa que possa bloquear ali - um
//  named pipe, um mutex disputado, uma alocacao - vira travadinha no jogo da pessoa. Um bloco
//  de memoria com leituras atomicas de 32 bits nao bloqueia, nao aloca e nao pode falhar no
//  meio. O preco e que so cabem tipos simples, e isso e aceitavel: o que atravessa e um
//  numero e um punhado de sinalizadores.
//
//  NADA AQUI E SEGREDO. A peca se identifica, publica o que encontrou e escreve o motivo de
//  ter desistido. Isso e deliberado: o desenho inteiro e transparente, e se um sistema
//  antitrapaça olhar para dentro do processo, ele deve encontrar exatamente o que esta escrito
//  aqui. Ver fpslimit_guard.h.
// =====================================================================================
#pragma once
#include <windows.h>
#include <stdint.h>

namespace FpsLimit {

// 'D5FL' em ASCII. Primeira coisa conferida dos dois lados; se nao bater, o bloco nao e nosso.
static const uint32_t kMagic   = 0x4C463544u;
static const uint32_t kVersao  = 1u;

// Os unicos limites que a interface oferece. Valor fora desta lista e tratado como 0 (livre),
// porque memoria compartilhada e escrita por outro processo e nao se confia em numero que
// chegou de fora sem conferir. Ver ValorPermitido().
static const uint32_t kLimitesPermitidos[] = { 0, 30, 45, 60, 120 };

// Quantos segundos sem o app dar sinal de vida ate a peca se desativar sozinha. E a defesa
// contra o app morrer com o limite aplicado: sem ela, o jogo ficaria preso ate ser fechado.
// Cinco segundos e folgado para um app que pulsa a cada segundo, e curto o bastante para a
// pessoa nao achar que travou de vez.
static const uint32_t kSegundosSemAppAteDesistir = 5;

// O que a peca esta fazendo. So ela escreve; o app so le.
enum Estado : uint32_t {
    EST_INICIANDO     = 0,   // carregou, ainda nao decidiu nada
    EST_ATIVO         = 1,   // enganchou e esta limitando (ou pronta para limitar, com limite 0)
    EST_SEM_API       = 2,   // nao reconheceu a API grafica do jogo: repasse puro
    EST_GANCHO_FALHOU = 3,   // reconheceu mas nao conseguiu enganchar: repasse puro
    EST_ANTITRAPACA   = 4,   // achou protecao dentro do processo: RECUSOU enganchar
    EST_APP_SUMIU     = 5,   // o app parou de dar sinal de vida: desativou sozinha
    EST_DESLIGADO     = 6,   // o app pediu para parar: repasse puro
};

// Qual API grafica a peca encontrou. Informativo, para o log e para o painel.
enum Api : uint32_t {
    API_NENHUMA = 0,
    API_D3D11   = 1,
    API_D3D12   = 2,
    API_D3D9    = 3,   // ainda nao implementado
    API_VULKAN  = 4,   // ainda nao implementado
    API_OPENGL  = 5,   // ainda nao implementado
};

// O que a peca achou de protecao DENTRO do processo. Mascara de bits: pode haver mais de um.
// Qualquer bit diferente de zero significa recusa. Ver fpslimit_guard.h.
enum Protecao : uint32_t {
    PROT_NENHUMA      = 0,
    PROT_EASYANTICHEAT = 1u << 0,
    PROT_BATTLEYE      = 1u << 1,
    PROT_VANGUARD      = 1u << 2,
    PROT_XIGNCODE      = 1u << 3,
    PROT_GAMEGUARD     = 1u << 4,
    PROT_PUNKBUSTER    = 1u << 5,
    PROT_DENUVO_AC     = 1u << 6,
    PROT_OUTRA         = 1u << 31,   // casou por padrao generico, sem nome certo
};

// O bloco. Campos de 32 bits alinhados, para leitura e escrita atomicas naturais no x64.
// A ordem NAO muda entre versoes: campo novo entra no fim e a versao sobe.
struct Bloco {
    uint32_t magic;        // kMagic
    uint32_t versao;       // kVersao
    uint32_t tamanho;      // sizeof(Bloco), confere layout alem da versao
    uint32_t pid_jogo;     // quem hospeda a peca (ela mesma escreve)

    // ---- do APP para a PECA
    volatile uint32_t limite_fps;      // 0 = livre; 30/45/60/120
    volatile uint32_t pulso_app;       // o app incrementa a cada segundo; a peca vigia
    volatile uint32_t desligar;        // 1 = repasse puro, e nao volte a limitar

    // ---- da PECA para o APP
    volatile uint32_t estado;          // enum Estado
    volatile uint32_t api;             // enum Api
    volatile uint32_t protecao;        // mascara de Protecao; != 0 significa recusa
    volatile uint32_t pulso_peca;      // a peca incrementa a cada quadro apresentado
    volatile uint32_t quadros_segurados; // quantos quadros ela de fato atrasou (prova de efeito)

    // Motivo da desistencia, em texto, para o painel mostrar sem traduzir codigo de erro.
    // Escrito UMA VEZ, quando o estado vira algo diferente de ATIVO. UTF-16, terminado em 0.
    wchar_t  motivo[128];
};

// Nome do bloco. UM POR PROCESSO HOSPEDEIRO, com o pid no nome: a pessoa pode ter dois jogos
// abertos, e o app precisa falar com a peca certa. "Local\" mantem o objeto dentro da sessao
// de logon, que e o que queremos - nada disso atravessa contas de usuario.
//
// LIMITACAO CONHECIDA, e ela e do Windows e nao nossa: se o jogo rodar ELEVADO e o app nao, o
// app nao consegue abrir o bloco criado pelo jogo. Nesse caso o limitador simplesmente nao
// fica disponivel, e o painel diz isso em vez de fingir que funcionou. Rodar o app elevado
// para contornar seria pedir privilegio por causa de um teto de fps, o que nao se justifica.
inline void NomeDoBloco(wchar_t* dest, size_t n, DWORD pid) {
    _snwprintf(dest, n, L"Local\\DLSS5FpsLimit_%lu", (unsigned long)pid);
    dest[n - 1] = 0;
}

// Um numero que chegou pela memoria compartilhada so vale se estiver na lista. Qualquer outra
// coisa vira 0 (livre), NUNCA um limite inventado: no pior caso o jogo roda solto, que e o
// comportamento seguro.
inline uint32_t ValorPermitido(uint32_t v) {
    for (size_t i = 0; i < sizeof kLimitesPermitidos / sizeof kLimitesPermitidos[0]; i++)
        if (kLimitesPermitidos[i] == v) return v;
    return 0;
}

// O bloco e nosso e desta versao? Os dois lados conferem antes de tocar em qualquer campo.
inline bool BlocoValido(const Bloco* b) {
    return b && b->magic == kMagic && b->versao == kVersao && b->tamanho == (uint32_t)sizeof(Bloco);
}

} // namespace FpsLimit
