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
    - 64 chaves de estado desejado. Passou disso, o comando ainda e aplicado,
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

extern "C" __declspec(dllexport) const char *NAME        = "DLSS 5 Bridge";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Ponte de controle do DLSS5 For All: deixa o painel do app trocar LUT, "
    "ajustar o grading e ligar/desligar tecnicas sem abrir o overlay.";

namespace
{
    constexpr size_t kQueueCap = 128;   /* uniforms  */
    constexpr size_t kTechCap  = 32;    /* tecnicas  */
    constexpr size_t kStateCap = 64;

    /* o efeito de cor que o painel controla (ver reshade_bridge.h) */
    constexpr const char *kGradeEffect    = "DLSS5_Grade.fx";
    constexpr const char *kGradeTechnique = "DLSS5_Grade";

    SRWLOCK g_lock = SRWLOCK_INIT;

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

                char line[192];
                snprintf(line, sizeof(line), "[DLSS 5 Bridge] tecnica '%s' -> %s",
                         c.name, want ? "ligada" : "desligada");
                log_info(line);
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

    void on_begin_effects(reshade::api::effect_runtime *runtime,
                          reshade::api::command_list *,
                          reshade::api::resource_view,
                          reshade::api::resource_view)
    {
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
            const uint64_t srv = g_ext_mv.load(std::memory_order_acquire);
            if (srv != 0)
            {
                const reshade::api::resource_view view = { srv };
                runtime->update_texture_bindings("DLSS5_EXTMV", view, view);
            }
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

extern "C" __declspec(dllexport) int DLSS5Bridge_Submit(const DLSS5BridgeCommand *cmd)
{
    if (cmd == nullptr || cmd->struct_size != sizeof(DLSS5BridgeCommand))
        return 0;
    if (cmd->type < DLSS5_BRIDGE_CMD_UNIFORM_INT || cmd->type > DLSS5_BRIDGE_CMD_NR_TOGGLE)
        return 0;
    if (cmd->name[0] == '\0')
        return 0;

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
        log_info("[DLSS 5 Bridge] add-on registrado (protocolo 2; fala tambem com apps de protocolo 1).");
        break;
    case DLL_PROCESS_DETACH:
        /* reserved != nullptr quer dizer que o PROCESSO esta terminando: as
           outras DLLs ja podem ter saido, e mexer na IAT do RenoDX seria
           escrever em memoria que talvez nao exista mais. So desfazemos o
           hook num FreeLibrary de verdade (reserved == nullptr). */
        if (reserved == nullptr)
            remove_nr_hook();
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
