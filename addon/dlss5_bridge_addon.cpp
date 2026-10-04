/*
    dlss5_bridge_addon.cpp -- add-on do ReShade que deixa o painel do
    DLSS5 For All mexer em uniforms e tecnicas em runtime.

    Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).
    Usa os headers da API de add-on do ReShade (include/reshade/), de
    Patrick Mours, sob "BSD-3-Clause OR MIT" -- aqui sob os termos do MIT.

    ------------------------------------------------------------------------
    O DESENHO
    ------------------------------------------------------------------------
    Add-on e app moram no MESMO processo: o ReShade (dxgi.dll) e carregado
    dentro do DLSS5ForAll.exe e e ele quem faz LoadLibrary neste .addon64.
    Entao nao existe IPC: o app resolve os exports daqui com GetProcAddress
    (ver reshade_bridge.h) e chama direto.

    O que precisa de cuidado e a THREAD. O painel roda na thread da UI; a API
    do ReShade so pode ser tocada na thread que renderiza, e so quando ha um
    effect_runtime valido. Por isso:

        thread da UI       DLSS5Bridge_Submit    -> enfileira (SRWLOCK)
        thread de render   reshade_begin_effects -> aplica os UNIFORMS
                           reshade_present       -> aplica as TECNICAS

    Uniform vai em reshade_begin_effects: e chamado no comeco de
    render_effects, antes de qualquer tecnica rodar, entao o valor novo ja
    vale para o frame corrente.

    Tecnica NAO pode ir ali -- ligar uma tecnica desligada faz o ReShade criar
    os recursos do efeito no meio da iteracao de render_effects e o processo
    morre. Vai em reshade_present, que roda depois de tudo. Custa um frame.

    ------------------------------------------------------------------------
    ESTADO DESEJADO x FILA
    ------------------------------------------------------------------------
    Alem da fila, guardamos o ULTIMO comando de cada chave (efeito+nome+tipo).
    Quando o ReShade recompila os efeitos (edicao de shader, troca de preset,
    resolucao nova), os uniforms voltam ao valor do preset -- e reaplicamos o
    estado desejado no primeiro frame depois de reshade_reloaded_effects. Sem
    isso o painel e o que esta na tela sairiam de sincronia sozinhos.

    ------------------------------------------------------------------------
    LIMITES CONSCIENTES
    ------------------------------------------------------------------------
    - Fila de 128 comandos. Se estourar (painel arrastando slider enquanto o
      render esta travado), os NOVOS sao descartados e 'dropped' sobe; o
      estado desejado continua correto, entao o proximo reapply conserta.
    - 128 chaves de estado desejado. Passou disso, o comando ainda e aplicado,
      so nao sobrevive a um reload.
    - Um effect_runtime por vez (o app so tem uma swapchain).

    ------------------------------------------------------------------------
    POR QUE NAO USAMOS find_uniform_variable() NEM find_technique()
    ------------------------------------------------------------------------
    Este add-on e compilado com MinGW-w64 (g++); o ReShade e compilado com
    MSVC. A vtable de effect_runtime bate slot a slot entre os dois (foi
    verificado: get_hwnd, get_device, get_effects_state, get_current_preset_path,
    enumerate_* e get_uniform_variable_name respondem todos corretamente).

    Mas os DOIS metodos que devolvem um handle POR VALOR nao batem. Os
    handles sao 'typedef struct { uint64_t handle; } ...' -- 8 bytes, POD --
    e o GCC devolve isso em RAX. O binario do ReShade nao: desmontando o slot
    de find_technique no dxgi.dll 6.8.0.2155 aparece

        mov [rsp+0x20], r9      ; 4 registradores de argumento
        mov rax, [rcx+0x1d0]    ; RCX e o 'this' (mesmos offsets de membro
        ...                     ;  que enumerate_techniques usa)
        mov rbx, r9  / mov r12, r8 / mov r15, rdx
        test rbx, rbx / je  ...  ; o ULTIMO argumento tem de ser nao-nulo

    isto e: this em RCX, ponteiro oculto de retorno em RDX, e so entao
    effect_name em R8 e o nome em R9. O g++ chama com effect_name em RDX, e o
    ReShade escreve 8 bytes de handle em cima do ponteiro da string literal:
    violacao de acesso na hora.

    Chumbar indices de vtable e chamar com a convencao certa funcionaria, mas
    quebraria em silencio a cada atualizacao do ReShade. Em vez disso achamos
    os handles com enumerate_uniform_variables / enumerate_techniques, que
    retornam void, recebem o handle como PARAMETRO (o que a ABI acerta) e sao
    exatamente o que a API oferece para isso. Custa um strcmp por uniform --
    o DLSS5_Grade.fx tem 15.
*/

#define DLSS5_BRIDGE_ADDON_SIDE 1
#include "../reshade_bridge.h"

#include <windows.h>
#include <reshade.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "shared_frame.h"   // publica a saida ja com efeitos numa textura compartilhada
#include "mistura_guiada.h" // reinjeta o detalhe do nativo na saida da rede (desligada por padrao)
#include "comparar_lado_a_lado.h" // metade esquerda original / direita com DLSS 5 (desligado por padrao)
#include "ponteiro_desenhado.h"   // a seta do modo Ponteiro, desenhada DEPOIS da rede

extern "C" __declspec(dllexport) const char *NAME        = "DLSS 5 Bridge";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Ponte de controle do DLSS5 For All: deixa o painel do app trocar LUT, "
    "ajustar o grading e ligar/desligar tecnicas sem abrir o overlay.";

namespace
{
    constexpr size_t kQueueCap = 128;   /* uniforms  */
    constexpr size_t kTechCap  = 64;    /* tecnicas (I22: 14 tecnicas do card Filtros, ligadas e desligadas a cada quadro sintetizado) */
    constexpr size_t kStateCap = 128;   /* I22: 14 tecnicas + ~40 uniforms dos filtros, mais o resto do app */

    /* o efeito de cor que o painel controla (ver reshade_bridge.h) */
    constexpr const char *kGradeEffect    = "DLSS5_Grade.fx";
    constexpr const char *kGradeTechnique = "DLSS5_Grade";

    SRWLOCK g_lock = SRWLOCK_INIT;

    /* Saida compartilhada (ver shared_frame.h). O nome chega pela thread da UI
       do app e e consumido na thread de render, dentro do finish_effects -- por
       isso passa pelo mesmo SRWLOCK das outras filas, com uma flag atomica para
       o caminho comum (todo frame) nao pegar o lock a toa. */
    dlss5_shared::Producer g_shared;
    wchar_t          g_shared_name[64] = { 0 };
    std::atomic<int> g_shared_want{ 0 };

    /* MISTURA GUIADA (ver mistura_guiada.h). DESLIGADA POR PADRAO: quem nao
       ligar nao paga nada e nao ve diferenca nenhuma -- os dois eventos saem
       na primeira linha quando `ligada` e falso.

       O estado vem do dlss5forall.ini ao lado do executavel (o do exeDir; a
       arvore tem oito arquivos com esse nome e este e o que o app usa):

           [mistura]
           ligada=0        0/1, pode mudar com o jogo rodando
           eps=0.0001      o botao que decide se o serrilhado volta

       Aplicado no on_present, que e onde o add-on ja faz o trabalho fora da
       iteracao de efeitos. B10 (16/09/2026): a LEITURA do ini nao e mais feita
       ali (eram ~10 aberturas de arquivo por segundo na thread do Present): uma
       thread propria confere a data do arquivo e le so quando muda - ver
       ini_thread. */
    dlss5_mistura::Mistura g_mistura;

    /* MODO COMPARATIVO (ver comparar_lado_a_lado.h). Desligado por padrao; a
       chave e [comparar] ligado=1 no dlss5forall.ini, lida na mesma passada de
       um segundo da mistura. */
    dlss5_comparar::Comparador g_comparar;
    dlss5_ponteiro::Ponteiro    g_ponteiro;

    /* Duas filas, porque os dois tipos de comando so podem ser aplicados em
       momentos diferentes do frame -- ver o comentario de on_present. */
    DLSS5BridgeCommand g_queue[kQueueCap];
    size_t             g_queue_count = 0;   /* protegido por g_lock */

    DLSS5BridgeCommand g_tech_queue[kTechCap];
    size_t             g_tech_count = 0;    /* protegido por g_lock */

    DLSS5BridgeCommand g_state[kStateCap];
    size_t             g_state_count = 0;   /* protegido por g_lock */

    std::atomic<reshade::api::effect_runtime *> g_runtime { nullptr };
    std::atomic<uint32_t> g_effects_loaded { 0 };
    std::atomic<uint32_t> g_need_reapply   { 0 };   /* uniforms */
    std::atomic<uint32_t> g_need_retech    { 0 };   /* tecnicas */
    std::atomic<uint32_t> g_applied        { 0 };
    std::atomic<uint32_t> g_failed         { 0 };
    std::atomic<uint32_t> g_dropped        { 0 };

    /* espelho do estado do grading, atualizado na thread de render */
    std::atomic<uint32_t> g_grade_loaded  { 0 };
    std::atomic<uint32_t> g_grade_enabled { 0 };
    std::atomic<int32_t>  g_current_lut   { -1 };

    /* Motion vector externo, estimado pelo bloco de video da GPU (motion_estimate.h
       do app). Guardado como uint64 de proposito: em D3D11 o handle de
       resource_view do ReShade E o ponteiro nativo do ID3D11ShaderResourceView,
       entao o add-on nao precisa dos headers do D3D11 para repassa-lo.
       Zero = o app nao tem MV de hardware; a cadeia volta ao Lumenite_Kernel. */
    std::atomic<uint64_t> g_ext_mv { 0 };
    /* -1 = ainda nao decidido; 0/1 = estado de tecnica ja aplicado. Volta a -1
       em todo reload, porque o reload devolve as tecnicas ao que diz o preset. */
    std::atomic<int32_t>  g_ext_mv_applied { -1 };
    /* Presents em que procuramos o DLSS5_MV_Hardware sem achar. Ver o comentario
       em on_present: nao se desiste na primeira varredura. */
    std::atomic<int32_t>  g_ext_mv_wait { 0 };

    void log_info(const char *msg)
    {
        reshade::log::message(reshade::log::level::info, msg);
    }

/* Compile com -DDLSS5_BRIDGE_TRACE para carimbar cada passo no ReShade.log.
   E o unico jeito pratico de achar onde um add-on morre: o ReShade escreve e
   descarrega cada linha na hora, entao a ultima linha do log e o ultimo passo
   que executou. */
#ifdef DLSS5_BRIDGE_TRACE
    #define BRIDGE_TRACE(msg) log_info("[DLSS 5 Bridge][trace] " msg)
#else
    #define BRIDGE_TRACE(msg) ((void)0)
#endif

    /* ==================================================================
       NEURAL RENDERING SEM TECLADO -- hook da IAT do RenoDX
       ==================================================================
       O RenoDX le a hotkey de NR com GetAsyncKeyState, e essa e a UNICA
       funcao de teclado que ele importa: nao usa is_key_pressed do ReShade,
       nem RegisterHotKey, nem hook de teclado. Reescrevendo a entrada da IAT
       DELE, servimos a tecla so para ele -- nada e injetado no sistema, nada
       vaza para o jogo, nenhum outro add-on e afetado.

       E preciso porque o teclado sintetico nao funciona nesta maquina: a
       janela em primeiro plano e a GameInputServiceWindow, de um processo de
       integridade mais alta, e o UIPI descarta o SendInput sem sinalizar erro
       (a chamada ainda devolve 1). Medido; ver docs/neural-sem-teclado.md.

       Se o RenoDX mudar e parar de importar GetAsyncKeyState pelo nome, o
       hook simplesmente nao instala: nr_hook_ready fica 0 e o app cai no
       caminho antigo. Nunca trava, so fica indisponivel. */

    typedef SHORT (WINAPI *GetAsyncKeyStateFn)(int);

    GetAsyncKeyStateFn  g_nr_original = nullptr;
    void              **g_nr_slot     = nullptr;

    std::atomic<int>      g_nr_left      { 0 };      /* leituras ainda "pressionada" */
    std::atomic<bool>     g_nr_edge      { false };  /* bit 0x0001 pendente          */
    std::atomic<uint32_t> g_nr_vk        { VK_F6 };
    std::atomic<uint32_t> g_nr_pulses    { 0 };
    std::atomic<uint32_t> g_nr_delivered { 0 };
    std::atomic<uint32_t> g_nr_ready     { 0 };

    /* Quantas leituras o toque dura. O RenoDX le a tecla dele uma vez por
       frame; 6 cobre um engasgo ou uma troca de resolucao, e como ele faz
       deteccao de borda o excesso nao vira repeticao (medido: um unico
       "NR toggled" por toque de 6). */
    constexpr int kNrPulseReads = 6;

    /* Devolve o que o Windows devolveria para uma tecla real sendo segurada e
       depois solta, para funcionar tanto se o RenoDX testar o bit 0x8000 com
       deteccao de borda propria quanto se testar o bit 0x0001 ("pressionada
       desde a ultima chamada"). Roda na thread de render do RenoDX. */
    SHORT WINAPI GetAsyncKeyState_bridge(int vKey)
    {
        if (static_cast<uint32_t>(vKey) == g_nr_vk.load(std::memory_order_relaxed))
        {
            const int left = g_nr_left.load(std::memory_order_relaxed);
            if (left > 0)
            {
                g_nr_left.store(left - 1, std::memory_order_relaxed);
                SHORT r = static_cast<SHORT>(0x8000);
                if (g_nr_edge.exchange(false, std::memory_order_relaxed))
                {
                    /* primeira leitura deste toque: o RenoDX viu de fato */
                    r |= static_cast<SHORT>(0x0001);
                    g_nr_delivered.fetch_add(1, std::memory_order_relaxed);
                }
                return r;
            }
            if (g_nr_edge.exchange(false, std::memory_order_relaxed))
            {
                g_nr_delivered.fetch_add(1, std::memory_order_relaxed);
                return static_cast<SHORT>(0x0001);
            }
        }
        return g_nr_original ? g_nr_original(vKey) : static_cast<SHORT>(0);
    }

    /* Acha o ponteiro da IAT de 'mod' para dll!func. nullptr se nao houver. */
    void **find_iat_slot(HMODULE mod, const char *dll, const char *func)
    {
        BYTE *base = reinterpret_cast<BYTE *>(mod);
        auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return nullptr;
        auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return nullptr;

        const IMAGE_DATA_DIRECTORY &dir =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (dir.VirtualAddress == 0 || dir.Size == 0)
            return nullptr;

        auto *imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);
        for (; imp->Name != 0; ++imp)
        {
            if (_stricmp(reinterpret_cast<const char *>(base + imp->Name), dll) != 0)
                continue;

            /* OriginalFirstThunk guarda os nomes; FirstThunk, os ponteiros ja
               resolvidos pelo loader -- e o segundo que trocamos. */
            auto *orig = reinterpret_cast<IMAGE_THUNK_DATA *>(
                base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
            auto *iat = reinterpret_cast<IMAGE_THUNK_DATA *>(base + imp->FirstThunk);

            for (; orig->u1.AddressOfData != 0; ++orig, ++iat)
            {
                if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal))
                    continue;                 /* importado por ordinal: sem nome */
                auto *byname = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(
                    base + orig->u1.AddressOfData);
                if (strcmp(reinterpret_cast<const char *>(byname->Name), func) == 0)
                    return reinterpret_cast<void **>(&iat->u1.Function);
            }
        }
        return nullptr;
    }

    void install_nr_hook(bool explicar)
    {
        /* GET_MODULE_HANDLE_EX_FLAG_PIN: enquanto o nosso ponteiro apontar
           para dentro do RenoDX, ele nao pode ser descarregado debaixo de nos. */
        HMODULE renodx = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,
                                L"renodx-dlss5.addon64", &renodx) || renodx == nullptr)
        {
            /* O add-on novo (renodx-dlss.addon64, sem o "5") nao tem hotkey: nao
               importa USER32!GetAsyncKeyState, nao tem chave de tecla e ignora o
               F6. Nao ha IAT onde se pendurar, e nao ha o que consertar aqui -- a
               alternancia dele e o DirectNeuralRenderingHookPoint do ReShade.ini,
               que o proprio app grava e o add-on rele em tempo real.

               A distincao importa no log: por meses esta funcao anunciou "RenoDX
               nao carregado" com o RenoDX carregadissimo, so com outro nome de
               arquivo -- e quem lesse o ReShade.log procuraria o add-on errado. */
            HMODULE novo = nullptr;
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,
                                   L"renodx-dlss.addon64", &novo) && novo != nullptr)
            {
                if (explicar)
                    log_info("[DLSS 5 Bridge] RenoDX novo (renodx-dlss.addon64): sem hotkey "
                             "para enganchar; a alternancia do Neural Rendering vai pelo "
                             "DirectNeuralRenderingHookPoint do ReShade.ini, pelo app.");
                return;
            }
            if (explicar)
                log_info("[DLSS 5 Bridge] RenoDX nao carregado: alternancia do "
                         "Neural Rendering indisponivel pela ponte.");
            return;
        }

        g_nr_slot = find_iat_slot(renodx, "USER32.dll", "GetAsyncKeyState");
        if (g_nr_slot == nullptr)
        {
            if (explicar)
                log_info("[DLSS 5 Bridge] o RenoDX nao importa USER32!GetAsyncKeyState "
                         "pelo nome (versao nova?): alternancia do NR indisponivel.");
            return;
        }

        /* A tecla e configuravel em [RenoDX.DLSS5] NRToggleKey do ReShade.ini,
           como o codigo decimal do virtual-key -- verificado: com 118 o RenoDX
           passa a responder F7 e ignora F6. Sem a chave, o padrao dele e F6. */
        uint32_t vk = VK_F6;
        int cfg = 0;
        if (reshade::get_config_value<int>(nullptr, "RenoDX.DLSS5", "NRToggleKey", cfg)
            && cfg > 0 && cfg < 256)
            vk = static_cast<uint32_t>(cfg);
        g_nr_vk.store(vk, std::memory_order_relaxed);

        g_nr_original = reinterpret_cast<GetAsyncKeyStateFn>(*g_nr_slot);

        DWORD old = 0;
        if (!VirtualProtect(g_nr_slot, sizeof(void *), PAGE_READWRITE, &old))
        {
            g_nr_slot = nullptr;
            log_info("[DLSS 5 Bridge] VirtualProtect na IAT do RenoDX falhou: "
                     "alternancia do NR indisponivel.");
            return;
        }
        /* Ponteiro alinhado de 8 bytes: a troca e atomica no x64, entao o
           RenoDX lendo em paralelo ve o valor velho ou o novo, nunca lixo. */
        *g_nr_slot = reinterpret_cast<void *>(&GetAsyncKeyState_bridge);
        VirtualProtect(g_nr_slot, sizeof(void *), old, &old);

        g_nr_ready.store(1, std::memory_order_release);

        char line[176];
        snprintf(line, sizeof(line),
                 "[DLSS 5 Bridge] alternancia do Neural Rendering pronta "
                 "(tecla do RenoDX = 0x%02X, sem teclado e sem foco).", vk);
        log_info(line);
    }

    void remove_nr_hook()
    {
        if (g_nr_slot == nullptr || g_nr_original == nullptr)
            return;
        DWORD old = 0;
        if (VirtualProtect(g_nr_slot, sizeof(void *), PAGE_READWRITE, &old))
        {
            *g_nr_slot = reinterpret_cast<void *>(g_nr_original);
            VirtualProtect(g_nr_slot, sizeof(void *), old, &old);
        }
        g_nr_slot = nullptr;
        g_nr_ready.store(0, std::memory_order_release);
    }

    /* effect[0] == 0 quer dizer "procure em todos os efeitos carregados" */
    const char *effect_or_null(const DLSS5BridgeCommand &c)
    {
        return c.effect[0] != '\0' ? c.effect : nullptr;
    }

    /* Duas entradas do estado desejado sao "a mesma coisa" quando miram o
       mesmo alvo. Efeito vazio e curinga ("procure em todos"), entao
       SetGradeEnabled("DLSS5_Grade.fx","DLSS5_Grade") e
       SetTechniqueEnabled(nullptr,"DLSS5_Grade") tem de colidir -- senao o
       estado guarda as duas e o reapply pos-reload aplica valores opostos em
       sequencia, e a tecnica pisca. */
    bool same_key(const DLSS5BridgeCommand &a, const DLSS5BridgeCommand &b)
    {
        const bool a_tech = (a.type == DLSS5_BRIDGE_CMD_TECHNIQUE);
        const bool b_tech = (b.type == DLSS5_BRIDGE_CMD_TECHNIQUE);
        if (a_tech != b_tech)
            return false;
        if (strncmp(a.name, b.name, DLSS5_BRIDGE_NAME_MAX) != 0)
            return false;
        if (a.effect[0] == '\0' || b.effect[0] == '\0')
            return true;
        return strncmp(a.effect, b.effect, DLSS5_BRIDGE_NAME_MAX) == 0;
    }

    /* Chamado com g_lock exclusivo. Substitui o comando de mesma chave para
       que o estado desejado tenha uma entrada por uniform, nao um historico. */
    void remember(const DLSS5BridgeCommand &c)
    {
        for (size_t i = 0; i < g_state_count; ++i)
        {
            if (same_key(g_state[i], c))
            {
                g_state[i] = c;
                return;
            }
        }
        if (g_state_count < kStateCap)
            g_state[g_state_count++] = c;
    }

    /* ---------------------------------------------------- thread de render */

    /* Busca por enumeracao em vez de find_*; ver o comentario do cabecalho
       sobre a ABI de retorno. Handle 0 = nao encontrado. */

    struct Lookup
    {
        const char *wanted;
        uint64_t    found;
    };

    void uniform_probe(reshade::api::effect_runtime *runtime,
                       reshade::api::effect_uniform_variable variable, void *user_data)
    {
        Lookup *look = static_cast<Lookup *>(user_data);
        if (look->found != 0)
            return;
        char name[128];
        size_t size = sizeof(name);
        runtime->get_uniform_variable_name(variable, name, &size);
        if (strcmp(name, look->wanted) == 0)
            look->found = variable.handle;
    }

    void technique_probe(reshade::api::effect_runtime *runtime,
                         reshade::api::effect_technique technique, void *user_data)
    {
        Lookup *look = static_cast<Lookup *>(user_data);
        if (look->found != 0)
            return;
        char name[128];
        size_t size = sizeof(name);
        runtime->get_technique_name(technique, name, &size);
        if (strcmp(name, look->wanted) == 0)
            look->found = technique.handle;
    }

    reshade::api::effect_uniform_variable
    lookup_uniform(reshade::api::effect_runtime *runtime, const char *effect, const char *name)
    {
        Lookup look { name, 0 };
        runtime->enumerate_uniform_variables(effect, uniform_probe, &look);
        reshade::api::effect_uniform_variable out;
        out.handle = look.found;
        return out;
    }

    reshade::api::effect_technique
    lookup_technique(reshade::api::effect_runtime *runtime, const char *effect, const char *name)
    {
        Lookup look { name, 0 };
        runtime->enumerate_techniques(effect, technique_probe, &look);
        reshade::api::effect_technique out;
        out.handle = look.found;
        return out;
    }

    /* ------------------------------------------------------------------
       LIGAR E DESLIGAR TECNICA NAO PODE ENCHER O LOG.

       Medido em 11/09/2026, sessao real do usuario com o Resident Evil
       Requiem: o ReShade.log chegou a 2,8 MB em tres minutos, e 8.526
       daquelas linhas eram estas quatro aqui ("tecnica X -> ligada" /
       "-> desligada"). Nao e defeito de logica: no modo "quadro
       sintetizado" o app SUPRIME a cadeia no quadro pulado e a devolve no
       seguinte, entao a troca acontece de verdade ~50 vezes por segundo.
       A linha esta certa; o que esta errado e imprimi-la toda vez.

       (Para dimensionar: o add-on de terceiro RenoDX DLSS despejou 21.074
       linhas na mesma sessao, uma por present. Essa nao e nossa e nao da
       para calar daqui - mas a nossa da, e fica.)

       O criterio: as primeiras trocas saem inteiras, porque sao elas que
       provam que o mecanismo subiu. Passado isso, o log vira um resumo a
       cada 30 s, que diz a mesma coisa sem crescer sem limite.
       ------------------------------------------------------------------ */
    constexpr int      kTrocasDetalhadas = 12;
    constexpr uint64_t kResumoMs         = 30000;
    unsigned g_trocas_total   = 0;   /* thread de render, sem concorrencia */
    unsigned g_trocas_resumo  = 0;
    uint64_t g_trocas_desde   = 0;

    void log_troca_de_tecnica(const char *nome, bool ligada)
    {
        const uint64_t agora = GetTickCount64();
        if (g_trocas_desde == 0)
            g_trocas_desde = agora;

        if (g_trocas_total < kTrocasDetalhadas)
        {
            g_trocas_total++;
            char line[192];
            snprintf(line, sizeof(line), "[DLSS 5 Bridge] tecnica '%s' -> %s%s",
                     nome, ligada ? "ligada" : "desligada",
                     g_trocas_total == kTrocasDetalhadas
                         ? "  (daqui em diante so um resumo a cada 30 s)" : "");
            log_info(line);
            return;
        }

        g_trocas_total++;
        g_trocas_resumo++;
        if (agora - g_trocas_desde < kResumoMs)
            return;

        char line[192];
        snprintf(line, sizeof(line),
                 "[DLSS 5 Bridge] tecnicas ligadas/desligadas %u vezes nos ultimos %.0f s"
                 " (o quadro sintetizado suprime e devolve a cadeia a cada quadro)",
                 g_trocas_resumo, (double)(agora - g_trocas_desde) / 1000.0);
        log_info(line);
        g_trocas_resumo = 0;
        g_trocas_desde  = agora;
    }

    void apply(reshade::api::effect_runtime *runtime, const DLSS5BridgeCommand &c)
    {
        using namespace reshade::api;

        const uint32_t count = (c.count >= 1 && c.count <= 4) ? c.count : 1;

        if (c.type == DLSS5_BRIDGE_CMD_TECHNIQUE)
        {
            const effect_technique tech = lookup_technique(runtime, effect_or_null(c), c.name);
            if (tech == 0)
            {
                g_failed.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            const bool want = (c.ivalue[0] != 0);
            if (runtime->get_technique_state(tech) != want)
            {
                runtime->set_technique_state(tech, want);
                log_troca_de_tecnica(c.name, want);
            }
            g_applied.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const effect_uniform_variable var =
            lookup_uniform(runtime, effect_or_null(c), c.name);
        if (var == 0)
        {
            g_failed.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        switch (c.type)
        {
        case DLSS5_BRIDGE_CMD_UNIFORM_INT:
            runtime->set_uniform_value_int(var, c.ivalue, count, 0);
            break;
        case DLSS5_BRIDGE_CMD_UNIFORM_FLOAT:
            runtime->set_uniform_value_float(var, c.fvalue, count, 0);
            break;
        case DLSS5_BRIDGE_CMD_UNIFORM_BOOL:
        {
            bool values[4];
            for (uint32_t i = 0; i < count; ++i)
                values[i] = (c.ivalue[i] != 0);
            runtime->set_uniform_value_bool(var, values, count, 0);
            break;
        }
        case DLSS5_BRIDGE_CMD_RESET_UNIFORM:
            runtime->reset_uniform_value(var);
            break;
        default:
            g_failed.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        g_applied.fetch_add(1, std::memory_order_relaxed);
    }

    /* Le de volta o que o painel precisa mostrar: o efeito de cor existe? a
       tecnica esta ligada? qual LUT esta ativa? */
    void refresh_status(reshade::api::effect_runtime *runtime)
    {
        using namespace reshade::api;

        BRIDGE_TRACE("refresh: antes de lookup_uniform");
        const effect_uniform_variable lut = lookup_uniform(runtime, kGradeEffect, "LUT_SELECT");
        BRIDGE_TRACE("refresh: depois de lookup_uniform");
        if (lut != 0)
        {
            g_grade_loaded.store(1, std::memory_order_relaxed);
            int32_t value = -1;
            runtime->get_uniform_value_int(lut, &value, 1, 0);
            BRIDGE_TRACE("refresh: depois de get_uniform_value_int");
            g_current_lut.store(value, std::memory_order_relaxed);
        }
        else
        {
            g_grade_loaded.store(0, std::memory_order_relaxed);
            g_current_lut.store(-1, std::memory_order_relaxed);
        }

        const effect_technique tech = lookup_technique(runtime, kGradeEffect, kGradeTechnique);
        BRIDGE_TRACE("refresh: depois de lookup_technique");
        g_grade_enabled.store(
            (tech != 0 && runtime->get_technique_state(tech)) ? 1u : 0u,
            std::memory_order_relaxed);
        BRIDGE_TRACE("refresh: fim");
    }

    /* B10 (16/09/2026): O QUE O INI DIZ, lido FORA da thread do Present. Eram ~10
       GetPrivateProfile* (cada um abre e fecha o arquivo) por segundo dentro do
       on_present. Agora a thread de ini (mais abaixo) le isto so quando o
       dlss5forall.ini ou o dlss5-feed.cfg mudam de data ou tamanho, e publica por um
       ponteiro atomico; o on_present so troca o ponteiro e chama os configurar(). */
    struct IniMistura
    {
        bool ligada = false; float eps = 1e-4f; int wr = 0;
        bool comp = false; float divi = 50.f; int rot = 1; int rotH = 4; int atr = 0;
        bool en = false;   /* [idioma] lingua=en: textos desenhados no quadro em ingles */
        /* O RETANGULO DO CAMINHO CURTO (short_w/short_h do dlss5-feed.cfg).
           Zero = desligado. So o comparativo o usa, para nao colar tarja preta
           na metade direita - ver A METADE DIREITA PRETA em
           comparar_lado_a_lado.h. */
        int shW = 0, shH = 0;
        /* X-CONSOLE / I11: a geometria da fonte, escrita pelo app em [comparar]
           (bb_w/bb_h, fonte_x/y/w/h, caixa_w/caixa_h). Ver comparar_lado_a_lado.h. */
        dlss5_comparar::Geometria geo;
        wchar_t exe[MAX_PATH] = { 0 };
        wchar_t atalho[64] = { 0 };
    };
    std::atomic<IniMistura *> g_ini_pendente{ nullptr };

    /* QUAL dlss5forall.ini (25/09/2026, diagnostico do comparativo). O app NAO
       grava sempre ao lado do exe: quando a pasta do exe nao aceita escrita
       (instalado em Program Files), ele passa a usar
       %LOCALAPPDATA%\DLSS5ForAll\dlss5forall.ini (main.cpp, g_dadosDir /
       IniPath). Este add-on lia so o do exeDir - e ai o Ctrl+F6 gravava
       [comparar] ligado=1 num arquivo que ninguem daqui lia: o modo nunca ligava,
       sem erro em lugar nenhum. Agora: se o do LOCALAPPDATA existe e e MAIS NOVO
       que o do exeDir (ou o do exeDir nao existe), vale ele. Barato: dois
       GetFileAttributesEx, e a thread de ini ja faz isso a cada 250 ms.
       `exeDir` com a barra final; `ini` recebe o caminho escolhido. */
    void caminho_ini(const wchar_t *exeDir, wchar_t *ini, size_t n)
    {
        _snwprintf(ini, n, L"%sdlss5forall.ini", exeDir);
        ini[n - 1] = L'\0';
        wchar_t base[MAX_PATH] = { 0 };
        const DWORD r = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
        if (r == 0 || r >= MAX_PATH) return;
        wchar_t alt[MAX_PATH];
        _snwprintf(alt, MAX_PATH, L"%s\\DLSS5ForAll\\dlss5forall.ini", base);
        alt[MAX_PATH - 1] = L'\0';
        WIN32_FILE_ATTRIBUTE_DATA a{}, b{};
        if (!GetFileAttributesExW(alt, GetFileExInfoStandard, &b)) return;
        if (GetFileAttributesExW(ini, GetFileExInfoStandard, &a) &&
            CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) >= 0) return;
        wcsncpy(ini, alt, n - 1);
        ini[n - 1] = L'\0';
    }

    /* A LINHA ARRASTAVEL, AO VIVO (25/09/2026). O app (comparativo.h) chama o
       export DLSS5Bridge_CompararAoVivo a cada movimento do mouse; o ini so
       recebe a posicao quando a pessoa solta. Passar pelo ini a 60 Hz seria
       reescrever o arquivo inteiro 60 vezes por segundo e ainda esperar os
       250 ms da thread de ini - a linha andaria aos saltos, atras do mouse.
       seq muda a cada chamada; on = -1/0/1; div em centesimos de ponto
       percentual (0..10000), -1 = nao mexe. */
    std::atomic<uint32_t> g_cmp_vivo_seq{ 0 };
    std::atomic<int>      g_cmp_vivo_on{ -1 };
    std::atomic<int>      g_cmp_vivo_div{ -1 };

    /* Le [mistura] do dlss5forall.ini do exeDir. Barato e sem alocar: duas
       chaves curtas. Devolve tudo para a classe, que decide sozinha se precisa
       recriar recursos. */
    bool mistura_ler_ini(IniMistura *out)
    {
        wchar_t exe[MAX_PATH] = { 0 };
        if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) return false;
        wchar_t *barra = wcsrchr(exe, L'\\');
        if (barra == nullptr) return false;
        *(barra + 1) = L'\0';
        wchar_t ini[MAX_PATH];
        caminho_ini(exe, ini, MAX_PATH);

        const bool ligada = GetPrivateProfileIntW(L"mistura", L"ligada", 0, ini) != 0;
        wchar_t buf[32] = { 0 };
        GetPrivateProfileStringW(L"mistura", L"eps", L"0.0001", buf, 32, ini);
        const float eps = (float)_wtof(buf);

        /* A resolucao de trabalho da rede, para a linha de log dizer contra o
           que a economia esta sendo contada.

           A PRIMEIRA VERSAO LIA SO O dlss5-feed.cfg E MOSTRAVA SEMPRE 100. O
           app nem sempre grava a chave la (o cfg que eu inspecionei tinha
           enabled, mode, hdr e nenhum work_resolution), e ausencia virava o
           padrao em silencio -- o log dizia "wr=100" numa rodada que estava a
           58, que e a etiqueta errada em cima do numero certo, de novo.

           Agora a fonte primaria e o [cadeia] do ini, que e onde a escolha da
           pessoa mora, e o cfg entra so como reserva. */
        /* SECAO [ultima], nao [cadeia]. O comentario do main.cpp diz "[cadeia]
           work_resolution no dlss5forall.ini", mas o IniGet que le a chave
           (main.cpp:2380) usa GetPrivateProfileStringW com a secao L"ultima".
           Escrever em [cadeia] nao tem efeito nenhum -- e o app segue com o
           valor automatico registrando "escolha do ini", o que faz parecer que
           obedeceu. Custou-me duas matrizes comparando duas rodadas iguais. */
        int wr = GetPrivateProfileIntW(L"ultima", L"work_resolution", 0, ini);
        if (wr < 25 || wr > 100) {
            wchar_t cfg[MAX_PATH];
            _snwprintf(cfg, MAX_PATH, L"%sdlss5-feed.cfg", exe);
            cfg[MAX_PATH - 1] = L'\0';
            FILE *f = _wfopen(cfg, L"rb");
            if (f != nullptr)
            {
                char linha[256];
                while (fgets(linha, sizeof(linha), f) != nullptr)
                {
                    const char *p = strstr(linha, "work_resolution");
                    if (p == nullptr) continue;
                    const char *eq = strchr(p, '=');
                    if (eq != nullptr) { wr = atoi(eq + 1); break; }
                }
                fclose(f);
            }
        }

        out->ligada = ligada; out->eps = eps; out->wr = wr;

        /* O modo comparativo vem na mesma leitura: e o mesmo ini e o mesmo
           ritmo de um segundo, e abrir o arquivo duas vezes por segundo para
           ler duas chaves nao se justifica. */
        const bool comp = GetPrivateProfileIntW(L"comparar", L"ligado", 0, ini) != 0;
        /* divisor com fracao (25/09/2026, linha arrastavel): "37.5" vale. */
        float divi = 50.f;
        {
            wchar_t d[32] = { 0 };
            GetPrivateProfileStringW(L"comparar", L"divisor", L"50", d, 32, ini);
            divi = (float)_wtof(d);
            if (!(divi >= 0.f && divi <= 100.f)) divi = 50.f;
        }
        /* rotulos: 0 sem, 1 etiquetas "DLSS 5"/"Original" (padrao), 2 artes PNG */
        const int  rot  = (int)GetPrivateProfileIntW(L"comparar", L"rotulos", 1, ini);
        const int  rotH = (int)GetPrivateProfileIntW(L"comparar", L"rotulo_altura", 4, ini);
        /* atraso: quantos quadros o lado esquerdo recua para casar com o
           direito. ZERO e o valor MEDIDO -- a bateria de 45 fotos em
           comparar_lado_a_lado.h mostra que compensar PIORA. A chave existe
           para uma cadeia que de fato defase. */
        const int  atr  = (int)GetPrivateProfileIntW(L"comparar", L"atraso", 0, ini);
        /* `exe` ja vem com a barra final: as artes das marcas estao em
           <exeDir>\marca\comparar-original.png e comparar-dlss5.png. */
        /* O ATALHO, escrito pelo app em [comparar] atalho (ele e quem tem o mapa
           de teclas, e a pessoa pode trocar). Vai para o cabecalho: este modo
           nao tem entrada no painel, entao a tecla e o unico caminho de volta -
           quem liga sem querer precisa ver como desligar sem adivinhar. */
        wchar_t atalho[64] = L"";
        GetPrivateProfileStringW(L"comparar", L"atalho", L"", atalho, 64, ini);
        /* short_w/short_h saem do dlss5-feed.cfg, que e onde o app os grava
           (main.cpp, o gravador do retangulo do caminho curto). O ini nao os
           tem: quem le esse retangulo do outro lado e o feeder, nao o app. */
        int shW = 0, shH = 0;
        {
            wchar_t cfg[MAX_PATH];
            _snwprintf(cfg, MAX_PATH, L"%sdlss5-feed.cfg", exe);
            cfg[MAX_PATH - 1] = L'\0';
            FILE *f = _wfopen(cfg, L"rb");
            if (f != nullptr)
            {
                char linha[256];
                while (fgets(linha, sizeof(linha), f) != nullptr)
                {
                    if (linha[0] == '#') continue;
                    const char *eq = strchr(linha, '=');
                    if (eq == nullptr) continue;
                    if (strncmp(linha, "short_w", 7) == 0 && linha[7] == '=') shW = atoi(eq + 1);
                    else if (strncmp(linha, "short_h", 7) == 0 && linha[7] == '=') shH = atoi(eq + 1);
                }
                fclose(f);
            }
        }

        /* X-CONSOLE / I11: a geometria da fonte (main.cpp, ComparadorGravarRetangulo).
           Ausente = zeros = app antigo, e o comparador segue a regra anterior. */
        out->geo.bbW = (int)GetPrivateProfileIntW(L"comparar", L"bb_w", 0, ini);
        out->geo.bbH = (int)GetPrivateProfileIntW(L"comparar", L"bb_h", 0, ini);
        out->geo.fx  = (int)GetPrivateProfileIntW(L"comparar", L"fonte_x", 0, ini);
        out->geo.fy  = (int)GetPrivateProfileIntW(L"comparar", L"fonte_y", 0, ini);
        out->geo.fw  = (int)GetPrivateProfileIntW(L"comparar", L"fonte_w", 0, ini);
        out->geo.fh  = (int)GetPrivateProfileIntW(L"comparar", L"fonte_h", 0, ini);
        out->geo.cw  = (int)GetPrivateProfileIntW(L"comparar", L"caixa_w", 0, ini);
        out->geo.ch  = (int)GetPrivateProfileIntW(L"comparar", L"caixa_h", 0, ini);

        out->comp = comp; out->divi = divi; out->rot = rot; out->rotH = rotH; out->atr = atr;
        {
            wchar_t lg[8] = { 0 };
            GetPrivateProfileStringW(L"idioma", L"lingua", L"pt", lg, 8, ini);
            out->en = (lg[0] == L'e' || lg[0] == L'E');
        }
        out->shW = shW; out->shH = shH;
        wcsncpy(out->exe, exe, MAX_PATH - 1);
        wcsncpy(out->atalho, atalho, 63);
        return true;
    }

    /* Na thread do Present: o que a thread de ini publicou, se publicou. */
    /* QUEM MANDA NO LIGADO E NA POSICAO DA LINHA (25/09/2026): quem mudou por
       ultimo. O ini e o caminho de sempre (o Ctrl+F6 do app grava la) e o
       export ao vivo e o do arraste. Aplicar o ini inteiro a cada releitura
       faria uma releitura provocada por OUTRA chave (o app grava o ini o tempo
       todo) devolver a linha para a posicao velha no meio do arraste. Entao do
       ini so vale o que MUDOU em relacao a ultima leitura dele. */
    int      g_cmp_ini_on  = -1;      /* ultimo ligado lido do ini; -1 = nunca leu */
    float    g_cmp_ini_div = -1.f;    /* ultimo divisor lido do ini */
    int      g_cmp_ef_on   = 0;       /* o que esta valendo */
    float    g_cmp_ef_div  = 50.f;
    uint32_t g_cmp_vivo_visto = 0;

    void comparar_aplicar_vivo()
    {
        const uint32_t seq = g_cmp_vivo_seq.load(std::memory_order_acquire);
        if (seq == g_cmp_vivo_visto) return;
        g_cmp_vivo_visto = seq;
        const int on  = g_cmp_vivo_on.load(std::memory_order_relaxed);
        const int div = g_cmp_vivo_div.load(std::memory_order_relaxed);
        if (on == 0 || on == 1) g_cmp_ef_on = on;
        if (div >= 0 && div <= 10000) g_cmp_ef_div = (float)div / 100.f;
        g_comparar.marcar_arrastavel();
        g_comparar.posicionar(g_cmp_ef_on, g_cmp_ef_div);
    }

    void mistura_aplicar_pendente()
    {
        comparar_aplicar_vivo();
        IniMistura *m = g_ini_pendente.exchange(nullptr, std::memory_order_acq_rel);
        if (m == nullptr) return;
        g_mistura.configurar(m->ligada, m->eps, m->wr);
        if (!m->ligada) g_mistura.desligar();
        const int iniOn = m->comp ? 1 : 0;
        if (iniOn != g_cmp_ini_on) { g_cmp_ini_on = iniOn; g_cmp_ef_on = iniOn; }
        if (m->divi != g_cmp_ini_div) { g_cmp_ini_div = m->divi; g_cmp_ef_div = m->divi; }
        g_comparar.configurar(g_cmp_ef_on != 0, g_cmp_ef_div, m->rot, m->rotH, m->atr, m->exe, m->atalho,
                              m->shW, m->shH, &m->geo, m->en);
        delete m;
    }

    /* B10: A THREAD DE INI. Confere data e tamanho dos dois arquivos a cada 250 ms (so
       metadado, sem abrir) e le quando algum mudou - a primeira volta le sempre.
       Vida: sobe no primeiro on_present e para (com espera) no destroy_effect_runtime,
       que o ReShade chama antes de descarregar os add-ons: o codigo nunca sai da memoria
       com a thread andando. O DllMain so faz uma parada de seguranca no FreeLibrary. */
    SRWLOCK           g_ini_trava = SRWLOCK_INIT;
    HANDLE            g_ini_thread = nullptr;
    HANDLE            g_ini_parar  = nullptr;   /* reset manual */
    HANDLE            g_ini_saiu   = nullptr;   /* reset manual; a thread liga ao sair */
    std::atomic<bool> g_ini_rodando{ false };

    bool carimbo_de(const wchar_t *caminho, WIN32_FILE_ATTRIBUTE_DATA *out)
    {
        if (GetFileAttributesExW(caminho, GetFileExInfoStandard, out)) return true;
        *out = WIN32_FILE_ATTRIBUTE_DATA{};   /* arquivo ausente tambem e um estado */
        return false;
    }
    bool carimbo_igual(const WIN32_FILE_ATTRIBUTE_DATA &a, const WIN32_FILE_ATTRIBUTE_DATA &b)
    {
        return CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) == 0 &&
               a.nFileSizeLow == b.nFileSizeLow && a.nFileSizeHigh == b.nFileSizeHigh;
    }

    DWORD WINAPI ini_thread(void *)
    {
        wchar_t exe[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (wchar_t *barra = wcsrchr(exe, L'\\')) *(barra + 1) = L'\0';
        wchar_t ini[MAX_PATH], cfg[MAX_PATH];
        _snwprintf(cfg, MAX_PATH, L"%sdlss5-feed.cfg", exe);  cfg[MAX_PATH - 1] = L'\0';
        WIN32_FILE_ATTRIBUTE_DATA vistoIni{}, vistoCfg{};
        bool primeira = true;
        for (;;)
        {
            /* a escolha do arquivo e refeita a cada volta: ver caminho_ini */
            caminho_ini(exe, ini, MAX_PATH);
            WIN32_FILE_ATTRIBUTE_DATA agoraIni{}, agoraCfg{};
            carimbo_de(ini, &agoraIni);
            carimbo_de(cfg, &agoraCfg);
            if (primeira || !carimbo_igual(agoraIni, vistoIni) || !carimbo_igual(agoraCfg, vistoCfg))
            {
                IniMistura *m = new IniMistura();
                if (mistura_ler_ini(m))
                {
                    delete g_ini_pendente.exchange(m, std::memory_order_acq_rel);
                    m = nullptr;
                }
                delete m;
                vistoIni = agoraIni; vistoCfg = agoraCfg; primeira = false;
            }
            if (WaitForSingleObject(g_ini_parar, 250) == WAIT_OBJECT_0) break;
        }
        SetEvent(g_ini_saiu);
        return 0;
    }

    void ini_thread_garantir()
    {
        if (g_ini_rodando.load(std::memory_order_acquire)) return;
        AcquireSRWLockExclusive(&g_ini_trava);
        if (!g_ini_rodando.load(std::memory_order_acquire))
        {
            if (g_ini_parar == nullptr) g_ini_parar = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (g_ini_saiu == nullptr)  g_ini_saiu  = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (g_ini_parar != nullptr && g_ini_saiu != nullptr)
            {
                ResetEvent(g_ini_parar);
                ResetEvent(g_ini_saiu);
                g_ini_thread = CreateThread(nullptr, 0, &ini_thread, nullptr, 0, nullptr);
            }
            if (g_ini_thread == nullptr)
            {
                /* Sem thread: le uma vez aqui mesmo, como antes, para o estado do ini valer. */
                IniMistura *m = new IniMistura();
                if (mistura_ler_ini(m)) { delete g_ini_pendente.exchange(m, std::memory_order_acq_rel); m = nullptr; }
                delete m;
            }
            g_ini_rodando.store(true, std::memory_order_release);
        }
        ReleaseSRWLockExclusive(&g_ini_trava);
    }

    /* noDllMain: com a trava do loader na mao a thread nao termina de sair; espera o
       evento dela em vez do handle. */
    void ini_thread_parar(bool noDllMain)
    {
        AcquireSRWLockExclusive(&g_ini_trava);
        if (g_ini_thread != nullptr)
        {
            SetEvent(g_ini_parar);
            WaitForSingleObject(noDllMain ? g_ini_saiu : g_ini_thread, noDllMain ? 2000 : 3000);
            CloseHandle(g_ini_thread);
            g_ini_thread = nullptr;
        }
        g_ini_rodando.store(false, std::memory_order_release);
        ReleaseSRWLockExclusive(&g_ini_trava);
        delete g_ini_pendente.exchange(nullptr, std::memory_order_acq_rel);
    }

    void on_begin_effects(reshade::api::effect_runtime *runtime,
                          reshade::api::command_list *,
                          reshade::api::resource_view rtv,
                          reshade::api::resource_view)
    {
        /* PRIMEIRA COISA DO QUADRO: aqui o back buffer ainda e o NATIVO, antes
           de qualquer tecnica. Se a mistura estiver desligada isto sai na
           primeira linha e o caminho de hoje segue intocado. */
        g_mistura.capturar_nativo(reinterpret_cast<void *>(rtv.handle), log_info);
        /* O comparativo guarda o MESMO instante: aqui o back buffer ainda e a
           imagem original, sem nenhuma tecnica e sem o Neural Rendering (que
           roda dentro do EvaluateFeature do DLSS5_Feed, uma tecnica). */
        g_comparar.capturar_original(reinterpret_cast<void *>(rtv.handle), log_info);

        /* Copia fila e estado sob o lock e solta antes de tocar no ReShade:
           chamar a API com um SRWLOCK na mao seria pedir para um evento
           reentrante travar tudo. */
        BRIDGE_TRACE("begin_effects: entrada");

        /* MV externo primeiro: o DLSS5_MV_Hardware.fx le esta semantica no
           primeiro passe da cadeia, e reshade_begin_effects e o unico momento
           em que a ligacao ainda chega a tempo deste frame. Rebindar todo frame
           e o esperado -- o SRV nao muda, mas o ReShade nao guarda a ligacao
           entre reloads e refazer custa uma escrita de ponteiro. */
        {
            /* PUBLICAR O ZERO TAMBEM, e nao apenas parar de publicar (18/09/2026, a
               queda do Nativo com o Resident Evil 2). O ReShade guarda este handle --
               que em D3D11 e o ponteiro cru do ID3D11ShaderResourceView -- ligado aos
               descriptor tables do passe, SEM AddRef. Quando o app soltava o MV
               (`DLSS5Bridge_SetExternalMV(nullptr)`, o que ele faz em toda troca de
               resolucao da fonte), o `if (srv != 0)` daqui fazia o add-on apenas PARAR
               de republicar -- e o runtime continuava com o ponteiro anterior ligado.
               O app destruia o SRV logo em seguida e o Present do quadro seguinte caia
               dentro do d3d11.dll, num ID3D11View::GetResource sobre memoria ja
               reciclada (0xC0000005 em d3d11.dll+0x5E08C).

               Publicar o zero e o caminho da propria API: handle nulo desliga a
               semantica e o ReShade volta a textura vazia dele. A partir daqui, soltar
               o MV do lado do app solta a ligacao aqui no MESMO frame. */
            const uint64_t srv = g_ext_mv.load(std::memory_order_acquire);
            const reshade::api::resource_view view = { srv };
            runtime->update_texture_bindings("DLSS5_EXTMV", view, view);
        }

        DLSS5BridgeCommand batch[kQueueCap + kStateCap];
        size_t batch_count = 0;

        const bool reapply = g_need_reapply.exchange(0, std::memory_order_acq_rel) != 0;

        AcquireSRWLockExclusive(&g_lock);
        if (reapply)
        {
            for (size_t i = 0; i < g_state_count; ++i)
                if (g_state[i].type != DLSS5_BRIDGE_CMD_TECHNIQUE)
                {
                    if (batch_count >= kQueueCap + kStateCap) break;   /* nunca escrever fora */
                    batch[batch_count++] = g_state[i];
                }
        }
        for (size_t i = 0; i < g_queue_count; ++i)
        {
            /* Os contadores sao validados em quem enfileira, mas 'batch' e array de pilha:
               a checagem custa nada e transforma um bug futuro de contagem numa ordem
               perdida, em vez de corrupcao de pilha dentro do processo do jogo. */
            if (batch_count >= kQueueCap + kStateCap) break;
            batch[batch_count++] = g_queue[i];
        }
        g_queue_count = 0;
        ReleaseSRWLockExclusive(&g_lock);

        BRIDGE_TRACE("begin_effects: fila drenada");

        for (size_t i = 0; i < batch_count; ++i)
            apply(runtime, batch[i]);

        BRIDGE_TRACE("begin_effects: lote aplicado");

        /* Refresca o espelho quando algo mudou, e periodicamente para pegar
           mudancas feitas pelo overlay do proprio ReShade. */
        static uint32_t tick = 0;
        if (batch_count != 0 || (++tick % 30) == 0)
            refresh_status(runtime);
    }

    /* Ligar uma tecnica que estava desligada faz o ReShade criar os recursos
       do efeito. Fazer isso de dentro de reshade_begin_effects derruba o
       processo -- o ReShade esta no meio de render_effects, iterando a lista
       que a ativacao mexe (confirmado: crash reproduzivel ao ligar do zero).
       reshade_present roda depois de tudo, inclusive do overlay, e e onde
       essa troca e segura. Por isso as tecnicas tem fila propria.

       O preco e um frame de latencia entre "ligar" e "aparecer", invisivel
       para quem clica num botao. */
    void on_present(reshade::api::effect_runtime *runtime)
    {
        /* O liga/desliga AO VIVO da mistura. Fica aqui, e nao nos eventos de
           efeito, porque este e o ponto que ja roda uma vez por quadro fora da
           iteracao de render_effects -- mexer em recursos de dentro dela
           derruba o processo, como o comentario do on_present ja documenta
           para as tecnicas. B10: a leitura do ini saiu daqui (thread de ini); aqui so
           se aplica o que ela publicou, quando publicou. */
        ini_thread_garantir();
        mistura_aplicar_pendente();

        /* O ReShade carrega os add-ons numa varredura so, e a ordem alfabetica
           nao garante que o RenoDX ja esteja no lugar quando o nosso DllMain
           roda. No primeiro present todos ja estao carregados.

           Tentamos por alguns segundos em vez de uma vez so: uma unica
           tentativa que caisse num instante ruim desligaria a alternancia do
           neural para o resto da sessao, sem chance de se recuperar. Depois
           de instalado nao ha mais custo -- e apenas um teste de inteiro por
           frame ate desistir. */
        constexpr int kNrMaxAttempts = 6;      /* a 1a imediata, depois 1 por segundo */
        static int nr_attempts = 0;
        static int nr_wait     = 0;
        if (nr_attempts < kNrMaxAttempts && g_nr_ready.load(std::memory_order_relaxed) == 0)
        {
            if (nr_wait > 0)
                --nr_wait;
            else
            {
                ++nr_attempts;
                /* so a ultima tentativa explica o motivo, para nao repetir a
                   mesma linha seis vezes no log de quem nao usa o RenoDX */
                install_nr_hook(nr_attempts == kNrMaxAttempts);
                nr_wait = 60;
            }
        }

        /* Troca automatica do provider de motion vector. O app so avisa "tenho MV
           de hardware" (ou parou de ter); quem decide qual tecnica roda e aqui,
           para o usuario nunca precisar mexer em preprocessor no overlay.
           So mexe na transicao, e so em reshade_present: ligar tecnica de dentro
           de begin_effects derruba o processo -- ver o comentario acima. */
        if (g_effects_loaded.load(std::memory_order_acquire) != 0)
        {
            const int32_t want = (g_ext_mv.load(std::memory_order_acquire) != 0) ? 1 : 0;
            if (g_ext_mv_applied.load(std::memory_order_relaxed) != want)
            {
                const auto hw = lookup_technique(runtime, "DLSS5_MV_Hardware.fx", "DLSS5_MV_Hardware");
                if (hw.handle != 0)
                {
                    runtime->set_technique_state(hw, want != 0);
                    /* Os dois escrevem em Kernel::tFlow; deixar os dois ligados
                       faria a ordem da lista decidir quem ganha, em silencio. */
                    const auto kn = lookup_technique(runtime, "lumenite_Kernel.fx", "Lumenite_Kernel");
                    if (kn.handle != 0)
                        runtime->set_technique_state(kn, want == 0);
                    g_ext_mv_applied.store(want, std::memory_order_relaxed);
                    log_info(want != 0
                        ? "[DLSS 5 Bridge] MV de hardware ativo: DLSS5_MV_Hardware ligado, Lumenite_Kernel desligado."
                        : "[DLSS 5 Bridge] sem MV de hardware: de volta ao Lumenite_Kernel.");
                }
                else if (want != 0)
                {
                    /* NAO desistir na primeira varredura. O ReShade compila os efeitos
                       de forma assincrona e passa por aqui mais de uma vez: na primeira
                       o DLSS5_MV_Hardware.fx pode ainda nao estar na lista de tecnicas.
                       Medido no run das 13:13 -- este aviso saiu 37 ms ANTES de o shader
                       compilar, e 2 s depois a tecnica foi ligada normalmente. Ou seja,
                       a mensagem mentia, e uma mensagem que mente no log custa mais do
                       que ela vale: manda quem le perseguir um problema inexistente.
                       Espera ~5 s de presents antes de dizer qualquer coisa. */
                    constexpr int32_t kHwMaxWait = 300;
                    if (g_ext_mv_wait.fetch_add(1, std::memory_order_relaxed) + 1 >= kHwMaxWait)
                    {
                        /* Agora sim: 5 s de presents procurando e nao achando. Marca
                           como decidido para nao repetir a linha todo frame. */
                        g_ext_mv_applied.store(want, std::memory_order_relaxed);
                        log_info("[DLSS 5 Bridge] o app tem MV de hardware, mas DLSS5_MV_Hardware.fx "
                                 "nao esta instalado em reshade-shaders\\Shaders; seguindo com o Lumenite_Kernel.");
                    }
                }
            }

            /* O CASO SILENCIOSO, que e o pior de todos: a tecnica esta LIGADA (o preset
               a listou, ou alguem a ligou pela overlay) mas ninguem chamou
               DLSS5Bridge_SetExternalMV. A textura externa fica preta, o mv sai zero, o
               teste estatico nem roda -- e o resultado e indistinguivel do bug que este
               caminho inteiro existe para consertar. Sem esta linha, so se descobre
               comparando pixel.

               Checa de vez em quando, e nao todo frame, porque get_technique_state
               atravessa a vtable. Avisa uma vez por sessao. */
            static uint32_t orphan_tick = 0;
            static bool     orphan_said = false;
            if (!orphan_said && g_ext_mv.load(std::memory_order_acquire) == 0 &&
                (++orphan_tick % 120) == 0)
            {
                const auto hw = lookup_technique(runtime, "DLSS5_MV_Hardware.fx", "DLSS5_MV_Hardware");
                if (hw.handle != 0 && runtime->get_technique_state(hw))
                {
                    orphan_said = true;
                    log_info("[DLSS 5 Bridge] AVISO: DLSS5_MV_Hardware esta ligado mas o app nunca "
                             "entregou a semantica DLSS5_EXTMV -- a textura esta preta e o DLSS "
                             "esta recebendo motion vector ZERO. Ligue [captura] mv_hardware=1 no "
                             "dlss5forall.ini, ou desligue esta tecnica e use o Lumenite_Kernel.");
                }
            }
        }

        DLSS5BridgeCommand batch[kTechCap + kStateCap];
        size_t batch_count = 0;

        const bool reapply = g_need_retech.exchange(0, std::memory_order_acq_rel) != 0;

        AcquireSRWLockExclusive(&g_lock);
        if (reapply)
        {
            for (size_t i = 0; i < g_state_count; ++i)
                if (g_state[i].type == DLSS5_BRIDGE_CMD_TECHNIQUE)
                {
                    if (batch_count >= kTechCap + kStateCap) break;
                    batch[batch_count++] = g_state[i];
                }
        }
        for (size_t i = 0; i < g_tech_count; ++i)
        {
            if (batch_count >= kTechCap + kStateCap) break;
            batch[batch_count++] = g_tech_queue[i];
        }
        g_tech_count = 0;
        ReleaseSRWLockExclusive(&g_lock);

        if (batch_count == 0)
            return;

        BRIDGE_TRACE("present: aplicando tecnicas");
        for (size_t i = 0; i < batch_count; ++i)
            apply(runtime, batch[i]);

        refresh_status(runtime);
    }

    /* DEPOIS de todos os efeitos, e antes de o quadro virar imagem apresentada:
       o unico ponto em que a saida final da camada existe e ainda esta viva.
       Ver o cabecalho do shared_frame.h para o porque de nao dar para pegar o
       back buffer pela API do ReShade com o MinGW.

       `rtv` (e nao `rtv_srgb`) porque os dois apontam para o MESMO recurso, e e
       o recurso que copiamos: a variante sRGB so muda como o shader le. */
    void on_finish_effects(reshade::api::effect_runtime *, reshade::api::command_list *,
                           reshade::api::resource_view rtv, reshade::api::resource_view)
    {
        if (g_shared_want.load(std::memory_order_acquire) != 0)
        {
            wchar_t nome[64];
            AcquireSRWLockExclusive(&g_lock);
            wcsncpy(nome, g_shared_name, 64); nome[63] = L'\0';
            g_shared_want.store(0, std::memory_order_relaxed);
            ReleaseSRWLockExclusive(&g_lock);
            g_shared.enable(nome, log_info);
        }
        /* A MISTURA VEM ANTES DE PUBLICAR, e a ordem importa: a camada de cima
           tem de receber a mesma imagem que a tela recebe. Publicar primeiro
           mandaria para o 2x a saida sem mistura, e as duas camadas ficariam
           mostrando coisas diferentes -- um defeito que so apareceria no 2x e
           que ninguem ligaria a este arquivo. */
        g_mistura.aplicar(reinterpret_cast<void *>(rtv.handle), log_info);

        /* O COMPARATIVO VEM DEPOIS DA MISTURA E ANTES DE PUBLICAR, e a ordem e
           deliberada: a metade direita tem de ser a imagem ORIGINAL (os lados
           foram invertidos em 14/09/2026; este comentario ficou para tras), sem
           mistura nenhuma (a mistura e parte do que se quer comparar), e a
           camada de cima (o 2x) e quem grava tem de receber a tela dividida,
           que e o que a pessoa esta vendo. */
        g_comparar.aplicar(reinterpret_cast<void *>(rtv.handle), log_info);

        /* A SETA POR ULTIMO, depois ate do comparativo: ela e a unica coisa
           aqui que NAO faz parte da imagem -- e informacao de controle. Se
           fosse antes, o comparativo a copiaria para dentro de uma das
           metades e ela apareceria duplicada numa e sumida na outra. */
        g_ponteiro.aplicar(reinterpret_cast<void *>(rtv.handle), log_info);

        if (!g_shared.enabled()) return;
        g_shared.publish(reinterpret_cast<void *>(rtv.handle));
    }

    void on_init_runtime(reshade::api::effect_runtime *runtime)
    {
        g_runtime.store(runtime, std::memory_order_release);
        g_need_reapply.store(1, std::memory_order_release);
        g_need_retech.store(1, std::memory_order_release);
        g_ext_mv_applied.store(-1, std::memory_order_relaxed);
        g_ext_mv_wait.store(0, std::memory_order_relaxed);
        log_info("[DLSS 5 Bridge] effect_runtime pronto; ponte de controle ativa.");
    }

    void on_destroy_runtime(reshade::api::effect_runtime *runtime)
    {
        /* B10: a thread de ini para antes de o ReShade poder descarregar o add-on; o
           proximo on_present a sobe de novo. */
        ini_thread_parar(false);
        reshade::api::effect_runtime *expected = runtime;
        g_runtime.compare_exchange_strong(expected, nullptr);
        g_effects_loaded.store(0, std::memory_order_release);
        g_grade_loaded.store(0, std::memory_order_release);
        /* O runtime morreu: o que o app leu como "LUT 2 ligada, grade ativa" nao vale mais.
           Sem zerar, o painel continuava mostrando o estado do runtime anterior ate alguem
           mexer - e um jogo que troca de API (DX11 -> DX12 no menu) passa por aqui. */
        g_current_lut.store(-1, std::memory_order_relaxed);
        g_grade_enabled.store(0, std::memory_order_relaxed);
    }

    void on_reloaded_effects(reshade::api::effect_runtime *runtime)
    {
        g_runtime.store(runtime, std::memory_order_release);
        g_effects_loaded.store(1, std::memory_order_release);
        /* os uniforms voltaram ao preset: reaplicar no proximo frame */
        g_need_reapply.store(1, std::memory_order_release);
        g_need_retech.store(1, std::memory_order_release);
        /* o reload tambem devolveu as tecnicas ao que diz o preset, entao a
           escolha de provider de MV precisa ser refeita do zero */
        g_ext_mv_applied.store(-1, std::memory_order_relaxed);
        g_ext_mv_wait.store(0, std::memory_order_relaxed);
        refresh_status(runtime);
    }
}

/* ======================================================================
   API exportada -- chamada pela thread da UI do app
   ====================================================================== */

/* CONGELADO EM 1, PARA SEMPRE. Este export significa "a versao mais VELHA que
   eu ainda falo", e nao a atual. O exe ja instalado tem 'if (ver() != 1)
   recusa' gravado no binario: devolver 2 aqui faria ele descartar a ponte
   inteira e perder ate a LUT e a nitidez, que nada tem a ver com o neural.
   A versao real vai em DLSS5Bridge_Protocol(), logo abaixo. */
extern "C" __declspec(dllexport) uint32_t DLSS5Bridge_Version(void)
{
    return DLSS5_BRIDGE_PROTOCOL_MIN;
}

/* A versao real. Nao existe nos add-ons da v1 -- e a ausencia dele que o
   cliente usa para reconhecer um add-on antigo. */
extern "C" __declspec(dllexport) uint32_t DLSS5Bridge_Protocol(void)
{
    return DLSS5_BRIDGE_PROTOCOL;
}

/* Motion vector estimado pelo bloco de video da GPU (motion_estimate.h, do lado
   do app). Recebe o ID3D11ShaderResourceView como void*: o add-on nao inclui os
   headers do D3D11, e em D3D11 o handle de resource_view do ReShade E esse mesmo
   ponteiro. Passar nullptr desliga e devolve a cadeia ao Lumenite_Kernel.

   Export NOVO de proposito: nao mexe em nenhuma struct nem no numero do
   protocolo. Um exe antigo simplesmente nao o resolve; um add-on antigo nao o
   tem, e o exe trata a ausencia como "sem MV de hardware" e segue funcionando.

   O ponteiro tem de continuar vivo enquanto estiver ligado -- quem chama e dono
   do SRV e precisa passar nullptr ANTES de solta-lo. */
extern "C" __declspec(dllexport) int DLSS5Bridge_SetExternalMV(void *srv)
{
    g_ext_mv.store(reinterpret_cast<uint64_t>(srv), std::memory_order_release);
    return 1;
}

/* MODO COMPARATIVO AO VIVO (25/09/2026): liga/desliga e posicao da linha sem
   passar pelo ini. Quem chama e a linha arrastavel do app (comparativo.h), por
   GetProcAddress - um exe antigo nao chama, e um add-on antigo nao tem o export
   (o app entao cai para gravar o ini, que continua valendo).
   ligado: -1 = nao mexe, 0 = desliga, 1 = liga.
   divisorPct: 0..100 = onde fica a linha dentro da imagem; < 0 = nao mexe.
   Vale no proximo Present (aplicado no on_present, fora da iteracao de efeitos). */
extern "C" __declspec(dllexport) int DLSS5Bridge_CompararAoVivo(int ligado, float divisorPct)
{
    int div = -1;
    if (divisorPct >= 0.f && divisorPct == divisorPct)
        div = (int)((divisorPct > 100.f ? 100.f : divisorPct) * 100.f + 0.5f);
    g_cmp_vivo_on.store((ligado == 0 || ligado == 1) ? ligado : -1, std::memory_order_relaxed);
    g_cmp_vivo_div.store(div, std::memory_order_relaxed);
    g_cmp_vivo_seq.fetch_add(1, std::memory_order_acq_rel);
    return 1;
}

/* Liga a saida compartilhada: a camada de cima passa a receber a nossa imagem
   por uma textura de video em vez de capturar a nossa janela pelo compositor.
   `name` vazio (ou nulo) desliga e libera tudo. O app chama isto por
   GetProcAddress, como ja faz com DLSS5Bridge_SetExternalMV: um .addon64 velho
   simplesmente nao tem o export, e a camada de cima segue pelo compositor.

   Devolve 1 quando o pedido foi aceito. Aceito nao quer dizer publicando: a
   textura so nasce no primeiro finish_effects, e se o formato nao for SDR de 8
   bits o produtor se desliga sozinho com uma linha no log. Quem sabe se esta
   valendo e o consumidor, pelo cabecalho na memoria compartilhada. */
extern "C" __declspec(dllexport) int DLSS5Bridge_SetSharedOutput(const wchar_t *name)
{
    AcquireSRWLockExclusive(&g_lock);
    g_shared_name[0] = L'\0';
    if (name != nullptr) { wcsncpy(g_shared_name, name, 63); g_shared_name[63] = L'\0'; }
    g_shared_want.store(1, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_lock);
    return 1;
}

/* Quantos quadros ja foram publicados e quantos foram pulados porque a camada
   de cima estava com a textura na mao. O app loga isso de vez em quando: dois
   numeros que dizem, sem adivinhacao, se o transporte esta 1 para 1. */
extern "C" __declspec(dllexport) int DLSS5Bridge_SharedStats(uint64_t *frames, uint64_t *skipped)
{
    if (frames  != nullptr) *frames  = g_shared.frames();
    if (skipped != nullptr) *skipped = g_shared.pulados();
    return g_shared.enabled() ? 1 : 0;
}

/* O modo EFETIVO do transporte, para o painel poder dizer o que esta valendo.
   Devolve 1 = mutex (o de sempre), 2 = rotacao, 0 = nao esta publicando.
   Serve para o caso em que a pessoa escolheu o experimental, ele nao nasceu e o
   programa caiu para o de sempre: sem isto ela acha que escolheu e pegou. */
extern "C" __declspec(dllexport) int DLSS5Bridge_SharedModo(uint32_t *slots)
{
    if (slots != nullptr) *slots = g_shared.slots();
    return g_shared.enabled() ? (int)g_shared.modo() : 0;
}

extern "C" __declspec(dllexport) int DLSS5Bridge_Submit(const DLSS5BridgeCommand *cmd)
{
    if (cmd == nullptr || cmd->struct_size != sizeof(DLSS5BridgeCommand))
        return 0;
    if (cmd->type < DLSS5_BRIDGE_CMD_UNIFORM_INT || cmd->type > DLSS5_BRIDGE_CMD_SET_CONFIG_FLOAT)
        return 0;
    if (cmd->name[0] == '\0')
        return 0;

    /* Gravar config NAO passa pela fila de efeitos: nao depende de
       effect_runtime nem de shader compilado, e o valor precisa valer AGORA --
       enfileirar so atrasaria o unico caminho que o add-on novo do RenoDX tem
       para ser ligado. Tambem nao entra no estado desejado que o reload
       reaplica: config e valor de uma vez, nao algo a reafirmar por frame. */
    if (cmd->type == DLSS5_BRIDGE_CMD_SET_CONFIG_INT)
    {
        if (cmd->effect[0] == '\0')
            return 0;
        char secao[DLSS5_BRIDGE_NAME_MAX], chave[DLSS5_BRIDGE_NAME_MAX];
        memcpy(secao, cmd->effect, sizeof(secao)); secao[DLSS5_BRIDGE_NAME_MAX - 1] = '\0';
        memcpy(chave, cmd->name,   sizeof(chave)); chave[DLSS5_BRIDGE_NAME_MAX - 1] = '\0';
        reshade::set_config_value<int>(nullptr, secao, chave, cmd->ivalue[0]);
        char line[192];
        snprintf(line, sizeof(line), "[DLSS 5 Bridge] config [%s] %s = %d",
                 secao, chave, (int)cmd->ivalue[0]);
        log_info(line);
        g_applied.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }

    /* O gemeo float do de cima. O set_config_value do ReShade e um template
       tipado: gravar 0.55 pelo caminho <int> guardaria 0, e o ajuste de imagem
       do RenoDX (Intensity, tons, estrutura) e todo float de 0 a 1. */
    if (cmd->type == DLSS5_BRIDGE_CMD_SET_CONFIG_FLOAT)
    {
        if (cmd->effect[0] == '\0')
            return 0;
        char secao[DLSS5_BRIDGE_NAME_MAX], chave[DLSS5_BRIDGE_NAME_MAX];
        memcpy(secao, cmd->effect, sizeof(secao)); secao[DLSS5_BRIDGE_NAME_MAX - 1] = '\0';
        memcpy(chave, cmd->name,   sizeof(chave)); chave[DLSS5_BRIDGE_NAME_MAX - 1] = '\0';
        reshade::set_config_value<float>(nullptr, secao, chave, cmd->fvalue[0]);
        char line[192];
        snprintf(line, sizeof(line), "[DLSS 5 Bridge] config [%s] %s = %.4f",
                 secao, chave, (double)cmd->fvalue[0]);
        log_info(line);
        g_applied.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }

    /* O toque no Neural Rendering nao passa pelas filas: nao toca na API do
       ReShade, so arma dois atomicos que o nosso GetAsyncKeyState le na thread
       do RenoDX. E de proposito que ele tambem NAO entra no estado desejado --
       e um pulso, nao um valor; reaplicar depois de um reload de efeitos
       alternaria o neural sem ninguem ter pedido. */
    if (cmd->type == DLSS5_BRIDGE_CMD_NR_TOGGLE)
    {
        if (g_nr_ready.load(std::memory_order_acquire) == 0)
            return 0;
        g_nr_edge.store(true, std::memory_order_relaxed);
        g_nr_left.store(kNrPulseReads, std::memory_order_relaxed);
        g_nr_pulses.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }

    /* copia defensiva com os campos de texto garantidamente terminados */
    DLSS5BridgeCommand c = *cmd;
    c.effect[DLSS5_BRIDGE_NAME_MAX - 1] = '\0';
    c.name[DLSS5_BRIDGE_NAME_MAX - 1]   = '\0';

    const bool is_tech = (c.type == DLSS5_BRIDGE_CMD_TECHNIQUE);

    AcquireSRWLockExclusive(&g_lock);
    remember(c);
    bool room;
    if (is_tech)
    {
        room = (g_tech_count < kTechCap);
        if (room)
            g_tech_queue[g_tech_count++] = c;
    }
    else
    {
        room = (g_queue_count < kQueueCap);
        if (room)
            g_queue[g_queue_count++] = c;
    }
    ReleaseSRWLockExclusive(&g_lock);

    if (!room)
    {
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        /* O estado desejado guardou o valor; forcamos um reapply para que ele
           chegue na tela mesmo tendo perdido a vaga na fila. */
        if (is_tech)
            g_need_retech.store(1, std::memory_order_release);
        else
            g_need_reapply.store(1, std::memory_order_release);
    }

    return 1;
}

/* Aceita qualquer struct_size entre o tamanho da v1 e o atual, e preenche so
   o que couber -- o padrao cbSize do Win32. E o que deixa o exe ja instalado,
   que pede 44 bytes, continuar recebendo LUT e nitidez em vez de ter o status
   inteiro recusado por causa de campos de neural que ele nem conhece. */
extern "C" __declspec(dllexport) int DLSS5Bridge_Query(DLSS5BridgeStatus *out)
{
    if (out == nullptr)
        return 0;
    const uint32_t want = out->struct_size;
    if (want < DLSS5_BRIDGE_STATUS_SIZE_V1 || want > sizeof(DLSS5BridgeStatus))
        return 0;

    AcquireSRWLockShared(&g_lock);
    const uint32_t pending = static_cast<uint32_t>(g_queue_count + g_tech_count);
    ReleaseSRWLockShared(&g_lock);

    DLSS5BridgeStatus s {};
    s.struct_size    = want;
    s.protocol       = DLSS5_BRIDGE_PROTOCOL;
    s.runtime_ready  = (g_runtime.load(std::memory_order_acquire) != nullptr) ? 1u : 0u;
    s.effects_loaded = g_effects_loaded.load(std::memory_order_acquire);
    s.grade_loaded   = g_grade_loaded.load(std::memory_order_relaxed);
    s.grade_enabled  = g_grade_enabled.load(std::memory_order_relaxed);
    s.current_lut    = g_current_lut.load(std::memory_order_relaxed);
    s.applied        = g_applied.load(std::memory_order_relaxed);
    s.failed         = g_failed.load(std::memory_order_relaxed);
    s.dropped        = g_dropped.load(std::memory_order_relaxed);
    s.pending        = pending;
    s.nr_hook_ready  = g_nr_ready.load(std::memory_order_acquire);
    s.nr_toggle_vk   = g_nr_vk.load(std::memory_order_relaxed);
    s.nr_pulses      = g_nr_pulses.load(std::memory_order_relaxed);
    s.nr_delivered   = g_nr_delivered.load(std::memory_order_relaxed);
    s.config_ready   = 1u;   /* protocolo 3: sabemos gravar config */
    /* O Hook Method do add-on novo, da config VIVA do ReShade. E esta a leitura
       que o app precisa: o ReShade.ini so recebe o valor no AutoSave, entao
       durante a sessao o arquivo mente e a config em memoria nao. Sem a chave
       (add-on antigo do RenoDX, ou primeira execucao) fica -1 = nao sei. */
    {
        int hp = -1;
        if (!reshade::get_config_value<int>(nullptr, "RENODX-DLSS",
                                            "DirectNeuralRenderingHookPoint", hp))
            hp = -1;
        s.nr_hookpoint = hp;
    }

    memcpy(out, &s, want);
    return 1;
}

/* ====================================================================== */

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(module))
            return FALSE;
        reshade::register_event<reshade::addon_event::init_effect_runtime>(on_init_runtime);
        reshade::register_event<reshade::addon_event::destroy_effect_runtime>(on_destroy_runtime);
        reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(on_reloaded_effects);
        reshade::register_event<reshade::addon_event::reshade_begin_effects>(on_begin_effects);
        reshade::register_event<reshade::addon_event::reshade_present>(on_present);
        reshade::register_event<reshade::addon_event::reshade_finish_effects>(on_finish_effects);
        log_info("[DLSS 5 Bridge] add-on registrado (protocolo 2; fala tambem com apps de protocolo 1).");
        break;
    case DLL_PROCESS_DETACH:
        /* reserved != nullptr quer dizer que o PROCESSO esta terminando: as
           outras DLLs ja podem ter saido, e mexer na IAT do RenoDX seria
           escrever em memoria que talvez nao exista mais. So desfazemos o
           hook num FreeLibrary de verdade (reserved == nullptr). */
        if (reserved == nullptr)
        {
            ini_thread_parar(true);   /* B10: FreeLibrary sem runtime destruido antes */
            remove_nr_hook();
        }
        reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(on_finish_effects);
        g_shared.shutdown();   // solta a textura compartilhada e marca "nao publicando" no cabecalho
        g_mistura.desligar();  // solta as texturas da mistura antes de o device morrer
        reshade::unregister_event<reshade::addon_event::reshade_present>(on_present);
        reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(on_begin_effects);
        reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(on_reloaded_effects);
        reshade::unregister_event<reshade::addon_event::destroy_effect_runtime>(on_destroy_runtime);
        reshade::unregister_event<reshade::addon_event::init_effect_runtime>(on_init_runtime);
        reshade::unregister_addon(module);
        break;
    }
    return TRUE;
}
