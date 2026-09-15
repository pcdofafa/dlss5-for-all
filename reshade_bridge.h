/*
    reshade_bridge.h -- ponte de controle entre o painel do DLSS5 For All e o
    ReShade, em runtime, sem abrir o overlay.

    Copyright (c) 2026 DLSS5 For All. Licenca MIT (a mesma do app).

    ------------------------------------------------------------------------
    COMO FUNCIONA
    ------------------------------------------------------------------------
    O ReShade nao aceita ser pilotado de fora do processo. Mas ele carrega
    add-ons DENTRO do processo do app (o dxgi.dll e o ReShade; o exe do app e
    quem o hospeda). Entao a ponte e um add-on nosso:

        dlss5-bridge.addon64   <- fica na raiz, ao lado do DLSS5ForAll.exe
                                  o ReShade carrega sozinho (AddonPath=.\)

    Esse add-on exporta tres funcoes C. Este header as resolve com
    GetModuleHandleW + GetProcAddress e as embrulha em chamadas simples.
    Como app e add-on vivem no MESMO processo, nao ha IPC nenhum: e uma
    chamada de funcao direta. O que atravessa threads (a thread da UI escreve,
    a thread de render aplica) e resolvido dentro do add-on com uma fila
    protegida por SRWLOCK, drenada no evento reshade_begin_effects.

    ------------------------------------------------------------------------
    DEGRADACAO LIMPA
    ------------------------------------------------------------------------
    Sem o .addon64 na pasta (ou com ele desabilitado no ReShade.ini) TODAS as
    funcoes retornam false e nada quebra. O painel deve chamar Available()
    para decidir se mostra a secao de cor habilitada ou acinzentada.

    Available() so cacheia o resultado POSITIVO. Enquanto o add-on nao
    aparecer, ela retenta a cada 500 ms -- necessario porque o ReShade so
    carrega add-ons quando o dispositivo grafico e criado, o que acontece
    depois de a janela do app subir.

    ------------------------------------------------------------------------
    USO MINIMO NO PAINEL
    ------------------------------------------------------------------------
        #include "reshade_bridge.h"

        if (SetLut(2))            // 0 = desligada, 1..5 = as LUTs
            ;                     // ok, comando enfileirado
        SetTechniqueEnabled("DLSS5_Grade", true);
        SetGradeStrength(0.75f);  // LUT_Strength
        SetExposure(0.25f);       // Exposure, em stops

        rsbridge::Status st;
        if (rsbridge::GetStatus(st) && st.grade_loaded)
            ;                     // o DLSS5_Grade.fx compilou e esta acessivel

    Um retorno true quer dizer "a ponte existe e o comando foi enfileirado",
    nao "o valor ja esta na tela" -- ele entra no proximo frame. Para saber se
    o uniform realmente existe, olhe st.grade_loaded / st.failed.

    Chame estas funcoes da thread da UI. Sao seguras de qualquer thread
    (a fila e travada), mas o cache de resolucao do modulo assume um chamador
    de cada vez -- e ele e atomico, entao o pior caso e resolver duas vezes.

    ------------------------------------------------------------------------
    UNIFORMS DO DLSS5_Grade.fx (nomes exatos, para SetUniform*)
    ------------------------------------------------------------------------
      int   LUT_SELECT        0=desligada 1=neutra 2=quente 3=fria
                              4=contraste alto 5=saturacao suave
      float LUT_Strength      0..1
      float LUT_AmountLuma    0..1
      float LUT_AmountChroma  0..1
      float Exposure          -2..2 (stops)
      float Temperature       -1..1
      float Tint              -1..1
      float3 Lift / Gamma / Gain
      float SCurve / Contrast / ContrastPivot / Vibrance / Saturation

    Tecnica: "DLSS5_Grade" no arquivo "DLSS5_Grade.fx".

    ------------------------------------------------------------------------
    UNIFORMS DO DLSS5_Sharpen.fx (nomes exatos, para SetUniform*)
    ------------------------------------------------------------------------
      float Sharpness     0..1   intensidade da nitidez (0 = identidade)
      float HaloProtect   0..1   1 = overshoot zero, 0 = halo liberado
      float Denoise       0..0.2 contraste minimo em luma para haver realce
      int   DebugView     0=normal 1=mapa de ganho 2=delta aplicado

    Tecnica: "DLSS5_Sharpen" no arquivo "DLSS5_Sharpen.fx".
    Ordem na lista: abaixo de DLSS5_Feed e ACIMA de DLSS5_Grade.
*/

#pragma once

#include <stdint.h>

/* ======================================================================
   PROTOCOLO -- compartilhado entre o app e o add-on.
   O add-on inclui este header com DLSS5_BRIDGE_ADDON_SIDE definido, o que
   deixa de fora a parte cliente (que depende de <windows.h> e de C++).
   Qualquer mudanca aqui exige recompilar OS DOIS lados; por isso o
   struct_size viaja junto em cada chamada e o add-on rejeita tamanhos que
   nao reconhece, em vez de ler lixo.
   ====================================================================== */

/* Versao do protocolo.
     1  LUT, grading, nitidez, tecnicas.
     2  + alternancia do Neural Rendering (ver ToggleNeural em rsbridge).

   COMPATIBILIDADE, e o motivo de haver dois numeros aqui.

   O exe e o add-on sao distribuidos juntos, mas nada impede alguem de trocar
   so um dos dois -- e o exe ja instalado tem 'if (ver() != 1) recusa' gravado
   no binario. Se o export DLSS5Bridge_Version passasse a devolver 2, esse exe
   descartaria a ponte INTEIRA e perderia ate a LUT e a nitidez, que nada tem a
   ver com o neural.

   Entao DLSS5Bridge_Version() esta congelado em DLSS5_BRIDGE_PROTOCOL_MIN e
   assim fica para sempre: ele significa "a versao mais VELHA que eu ainda
   falo". A versao real vem pelo export DLSS5Bridge_Protocol(), que simplesmente
   nao existe nos add-ons antigos -- e a ausencia dele ja e a resposta.

   Regra para quem mexer aqui: campos novos de status entram sempre NO FIM do
   struct, e o add-on aceita qualquer struct_size entre o tamanho da v1 e o
   atual, preenchendo so o que couber. E o padrao cbSize do Win32, e e o que
   deixa as quatro combinacoes (exe velho/novo x add-on velho/novo) andarem. */
#define DLSS5_BRIDGE_PROTOCOL     4u
#define DLSS5_BRIDGE_PROTOCOL_MIN 1u

/* tamanho do DLSS5BridgeStatus na v1 -- congelado, nao recalcule com sizeof */
#define DLSS5_BRIDGE_STATUS_SIZE_V1 44u
#define DLSS5_BRIDGE_MODULE_NAMEW L"dlss5-bridge.addon64"

/* nomes dos exports do add-on */
#define DLSS5_BRIDGE_EXPORT_VERSION "DLSS5Bridge_Version"
#define DLSS5_BRIDGE_EXPORT_PROTOCOL "DLSS5Bridge_Protocol"  /* ausente na v1 */
#define DLSS5_BRIDGE_EXPORT_SUBMIT  "DLSS5Bridge_Submit"
#define DLSS5_BRIDGE_EXPORT_QUERY   "DLSS5Bridge_Query"

/* tipos de comando */
#define DLSS5_BRIDGE_CMD_UNIFORM_INT   1u
#define DLSS5_BRIDGE_CMD_UNIFORM_FLOAT 2u
#define DLSS5_BRIDGE_CMD_UNIFORM_BOOL  3u
#define DLSS5_BRIDGE_CMD_TECHNIQUE     4u
#define DLSS5_BRIDGE_CMD_RESET_UNIFORM 5u
#define DLSS5_BRIDGE_CMD_NR_TOGGLE     6u   /* protocolo 2: alterna o Neural Rendering */
/* protocolo 3: grava um inteiro na config do ReShade, de DENTRO do processo.
   effect = secao, name = chave, ivalue[0] = valor.

   POR QUE ISTO PRECISOU EXISTIR. O add-on novo do RenoDX nao tem hotkey, e o
   liga/desliga dele e uma chave do ReShade.ini. O exe tentou gravar de fora,
   com WritePrivateProfileStringW, e o valor durava ate o AutoSave: o ReShade
   guarda a config INTEIRA em memoria e regrava o arquivo por cima, entao a
   escrita de fora era apagada minutos depois (medido: gravamos as 00:16:44,
   o ReShade reescreveu as 00:19:28 e o valor voltou ao que era). De dentro
   nao ha essa corrida -- quem grava e o mesmo dono da copia em memoria. */
#define DLSS5_BRIDGE_CMD_SET_CONFIG_INT 7u

/* protocolo 4: o mesmo que o 7, mas com FLOAT. effect = secao, name = chave,
   fvalue[0] = valor.

   POR QUE UM COMANDO SEPARADO. O perfil de imagem do RenoDX novo e todo float
   normalizado (Intensity, LocalToneStrength, SkinStructureStrength... de 0 a 1),
   e o set_config_value do ReShade e tipado: gravar 0.55 como inteiro guarda 0 e
   apaga o ajuste. Com o int a gente so alcancava HookPoint, Encoding e Style. */
#define DLSS5_BRIDGE_CMD_SET_CONFIG_FLOAT 8u

#define DLSS5_BRIDGE_NAME_MAX 64

typedef struct DLSS5BridgeCommand
{
    uint32_t struct_size;                    /* = sizeof(DLSS5BridgeCommand) */
    uint32_t type;                           /* DLSS5_BRIDGE_CMD_*           */
    char     effect[DLSS5_BRIDGE_NAME_MAX];  /* "DLSS5_Grade.fx"; vazio = qualquer efeito */
    char     name[DLSS5_BRIDGE_NAME_MAX];    /* uniform ou tecnica           */
    uint32_t count;                          /* 1..4 componentes             */
    int32_t  ivalue[4];
    float    fvalue[4];
} DLSS5BridgeCommand;

typedef struct DLSS5BridgeStatus
{
    uint32_t struct_size;     /* = sizeof(DLSS5BridgeStatus)                     */
    uint32_t protocol;        /* DLSS5_BRIDGE_PROTOCOL do add-on                 */
    uint32_t runtime_ready;   /* ha um effect_runtime vivo                       */
    uint32_t effects_loaded;  /* os efeitos ja terminaram de compilar             */
    uint32_t grade_loaded;    /* DLSS5_Grade.fx acessivel (LUT_SELECT encontrado) */
    uint32_t grade_enabled;   /* a tecnica DLSS5_Grade esta ligada agora          */
    int32_t  current_lut;     /* valor atual de LUT_SELECT (-1 = desconhecido)    */
    uint32_t applied;         /* comandos aplicados com sucesso                   */
    uint32_t failed;          /* comandos cujo uniform/tecnica nao existe         */
    uint32_t dropped;         /* comandos perdidos por fila cheia                 */
    uint32_t pending;         /* comandos na fila agora                           */
    /* ---- daqui para baixo so existe no protocolo 2; com um add-on v1 estes
           campos voltam zerados e protocol vale 1. Campos novos SEMPRE no fim. */
    uint32_t nr_hook_ready;   /* 1 = da para alternar o Neural Rendering          */
    uint32_t nr_toggle_vk;    /* a tecla que o RenoDX espera (NRToggleKey, ou F6) */
    uint32_t nr_pulses;       /* toques que armamos                               */
    uint32_t nr_delivered;    /* toques que o RenoDX efetivamente leu             */
    /* ---- protocolo 3 ---- */
    uint32_t config_ready;    /* 1 = o add-on sabe gravar config (CMD_SET_CONFIG_INT) */
    int32_t  nr_hookpoint;    /* [RENODX-DLSS] DirectNeuralRenderingHookPoint, lido da
                                 config VIVA do ReShade (nao do arquivo, que fica para
                                 tras). -1 = a chave nao existe / add-on antigo.       */
} DLSS5BridgeStatus;

/* ======================================================================
   LADO CLIENTE (app / painel)
   ====================================================================== */
#ifndef DLSS5_BRIDGE_ADDON_SIDE

#include <windows.h>
#include <atomic>
#include <cstring>

namespace rsbridge
{
    typedef DLSS5BridgeStatus Status;

    typedef uint32_t (*VersionFn)(void);
    typedef int      (*SubmitFn)(const DLSS5BridgeCommand *);
    typedef int      (*QueryFn)(DLSS5BridgeStatus *);

    namespace detail
    {
        inline std::atomic<SubmitFn> g_submit { nullptr };
        inline std::atomic<QueryFn>  g_query  { nullptr };
        inline std::atomic<uint64_t> g_next_try { 0 };
        /* qual protocolo o add-on que achamos fala (1 quando e um add-on antigo,
           que nao tem o export DLSS5Bridge_Protocol) */
        inline std::atomic<uint32_t> g_addon_protocol { 0 };

        /* Resolve os exports do add-on. Positivo e cacheado para sempre;
           negativo e retentado a cada 500 ms, porque o ReShade so carrega
           add-ons quando cria o dispositivo grafico. */
        inline bool resolve()
        {
            if (g_submit.load(std::memory_order_acquire) != nullptr)
                return true;

            const uint64_t now = GetTickCount64();
            if (now < g_next_try.load(std::memory_order_relaxed))
                return false;
            g_next_try.store(now + 500, std::memory_order_relaxed);

            HMODULE mod = GetModuleHandleW(DLSS5_BRIDGE_MODULE_NAMEW);
            if (mod == nullptr)
                return false;

            VersionFn ver = reinterpret_cast<VersionFn>(
                reinterpret_cast<void *>(GetProcAddress(mod, DLSS5_BRIDGE_EXPORT_VERSION)));
            SubmitFn sub = reinterpret_cast<SubmitFn>(
                reinterpret_cast<void *>(GetProcAddress(mod, DLSS5_BRIDGE_EXPORT_SUBMIT)));
            QueryFn qry = reinterpret_cast<QueryFn>(
                reinterpret_cast<void *>(GetProcAddress(mod, DLSS5_BRIDGE_EXPORT_QUERY)));

            if (ver == nullptr || sub == nullptr || qry == nullptr)
                return false;

            /* ver() e a versao MAIS VELHA que o add-on fala. Aceitamos qualquer
               add-on que ainda saiba conversar em algo que entendemos, em vez
               de exigir igualdade -- exigir igualdade e o que faria um add-on
               novo derrubar a LUT e a nitidez de um exe velho. */
            if (ver() > DLSS5_BRIDGE_PROTOCOL)
                return false;

            /* Este export so existe a partir do protocolo 2; a ausencia dele e
               justamente como se reconhece um add-on da v1. */
            uint32_t proto = DLSS5_BRIDGE_PROTOCOL_MIN;
            if (VersionFn pro = reinterpret_cast<VersionFn>(
                    reinterpret_cast<void *>(GetProcAddress(mod, DLSS5_BRIDGE_EXPORT_PROTOCOL))))
            {
                proto = pro();
                if (proto > DLSS5_BRIDGE_PROTOCOL)
                    proto = DLSS5_BRIDGE_PROTOCOL;  /* add-on mais novo que nos */
            }

            g_addon_protocol.store(proto, std::memory_order_release);
            g_query.store(qry, std::memory_order_release);
            g_submit.store(sub, std::memory_order_release);
            return true;
        }

        inline void put(char (&dst)[DLSS5_BRIDGE_NAME_MAX], const char *src)
        {
            memset(dst, 0, DLSS5_BRIDGE_NAME_MAX);
            if (src == nullptr)
                return;
            size_t n = strlen(src);
            if (n > DLSS5_BRIDGE_NAME_MAX - 1)
                n = DLSS5_BRIDGE_NAME_MAX - 1;
            memcpy(dst, src, n);
        }

        inline bool send(DLSS5BridgeCommand &cmd)
        {
            if (!resolve())
                return false;
            cmd.struct_size = static_cast<uint32_t>(sizeof(DLSS5BridgeCommand));
            SubmitFn fn = g_submit.load(std::memory_order_acquire);
            return fn != nullptr && fn(&cmd) != 0;
        }
    }

    /* Nome do efeito e da tecnica de cor. */
    inline const char *kGradeEffect    = "DLSS5_Grade.fx";
    inline const char *kGradeTechnique = "DLSS5_Grade";

    /* Nome do efeito e da tecnica de nitidez. */
    inline const char *kSharpenEffect    = "DLSS5_Sharpen.fx";
    inline const char *kSharpenTechnique = "DLSS5_Sharpen";

    /* A ponte esta presente e utilizavel? Use para acinzentar o painel. */
    inline bool Available()
    {
        return detail::resolve();
    }

    /* Qual protocolo o add-on carregado fala. 0 se a ponte nao esta presente. */
    inline uint32_t AddonProtocol()
    {
        if (!detail::resolve())
            return 0;
        return detail::g_addon_protocol.load(std::memory_order_acquire);
    }

    inline bool GetStatus(Status &out)
    {
        if (!detail::resolve())
            return false;
        memset(&out, 0, sizeof(out));

        /* Pedimos so o que o add-on sabe preencher. Um add-on da v1 recusa
           qualquer struct_size diferente de 44, entao mandar o struct novo
           para ele derrubaria o status inteiro em vez de perder so os campos
           do neural. Os campos nao preenchidos ficam em zero, que ja e a
           resposta certa ("sem neural pela ponte"). */
        const uint32_t proto = detail::g_addon_protocol.load(std::memory_order_acquire);
        out.struct_size = (proto >= 2u)
            ? static_cast<uint32_t>(sizeof(Status))
            : DLSS5_BRIDGE_STATUS_SIZE_V1;

        QueryFn fn = detail::g_query.load(std::memory_order_acquire);
        return fn != nullptr && fn(&out) != 0;
    }

    /* ---- genericos ---- */

    inline bool SetUniformInt(const char *effect, const char *name,
                              const int32_t *values, int count)
    {
        if (values == nullptr || count < 1 || count > 4)
            return false;
        DLSS5BridgeCommand c {};
        c.type  = DLSS5_BRIDGE_CMD_UNIFORM_INT;
        c.count = static_cast<uint32_t>(count);
        detail::put(c.effect, effect);
        detail::put(c.name, name);
        for (int i = 0; i < count; ++i)
            c.ivalue[i] = values[i];
        return detail::send(c);
    }

    inline bool SetUniformInt(const char *effect, const char *name, int32_t value)
    {
        return SetUniformInt(effect, name, &value, 1);
    }

    inline bool SetUniformFloat(const char *effect, const char *name,
                                const float *values, int count)
    {
        if (values == nullptr || count < 1 || count > 4)
            return false;
        DLSS5BridgeCommand c {};
        c.type  = DLSS5_BRIDGE_CMD_UNIFORM_FLOAT;
        c.count = static_cast<uint32_t>(count);
        detail::put(c.effect, effect);
        detail::put(c.name, name);
        for (int i = 0; i < count; ++i)
            c.fvalue[i] = values[i];
        return detail::send(c);
    }

    inline bool SetUniformFloat(const char *effect, const char *name, float value)
    {
        return SetUniformFloat(effect, name, &value, 1);
    }

    inline bool SetUniformFloat3(const char *effect, const char *name,
                                 float x, float y, float z)
    {
        const float v[3] = { x, y, z };
        return SetUniformFloat(effect, name, v, 3);
    }

    inline bool SetUniformBool(const char *effect, const char *name, bool value)
    {
        DLSS5BridgeCommand c {};
        c.type      = DLSS5_BRIDGE_CMD_UNIFORM_BOOL;
        c.count     = 1;
        c.ivalue[0] = value ? 1 : 0;
        detail::put(c.effect, effect);
        detail::put(c.name, name);
        return detail::send(c);
    }

    /* Devolve o uniform ao valor padrao do shader. */
    inline bool ResetUniform(const char *effect, const char *name)
    {
        DLSS5BridgeCommand c {};
        c.type = DLSS5_BRIDGE_CMD_RESET_UNIFORM;
        detail::put(c.effect, effect);
        detail::put(c.name, name);
        return detail::send(c);
    }

    /* Liga/desliga uma tecnica. 'effect' vazio ou nulo procura em todos os
       efeitos carregados -- suficiente porque nomes de tecnica sao unicos. */
    inline bool SetTechniqueEnabled(const char *technique, bool enabled,
                                    const char *effect = nullptr)
    {
        DLSS5BridgeCommand c {};
        c.type      = DLSS5_BRIDGE_CMD_TECHNIQUE;
        c.count     = 1;
        c.ivalue[0] = enabled ? 1 : 0;
        detail::put(c.effect, effect);
        detail::put(c.name, technique);
        return detail::send(c);
    }

    /* ---- atalhos do grading ---- */

    /* 0 = LUT desligada, 1..5 = as cinco LUTs do atlas. */
    inline bool SetLut(int index)
    {
        if (index < 0 || index > 5)
            return false;
        return SetUniformInt(kGradeEffect, "LUT_SELECT", index);
    }

    inline bool SetLutStrength(float v)   { return SetUniformFloat(kGradeEffect, "LUT_Strength", v); }
    inline bool SetLutLuma(float v)       { return SetUniformFloat(kGradeEffect, "LUT_AmountLuma", v); }
    inline bool SetLutChroma(float v)     { return SetUniformFloat(kGradeEffect, "LUT_AmountChroma", v); }
    inline bool SetExposure(float stops)  { return SetUniformFloat(kGradeEffect, "Exposure", stops); }
    inline bool SetTemperature(float v)   { return SetUniformFloat(kGradeEffect, "Temperature", v); }
    inline bool SetTint(float v)          { return SetUniformFloat(kGradeEffect, "Tint", v); }
    inline bool SetContrast(float v)      { return SetUniformFloat(kGradeEffect, "Contrast", v); }
    inline bool SetSCurve(float v)        { return SetUniformFloat(kGradeEffect, "SCurve", v); }
    inline bool SetVibrance(float v)      { return SetUniformFloat(kGradeEffect, "Vibrance", v); }
    inline bool SetSaturation(float v)    { return SetUniformFloat(kGradeEffect, "Saturation", v); }

    inline bool SetGradeEnabled(bool on)
    {
        return SetTechniqueEnabled(kGradeTechnique, on, kGradeEffect);
    }

    /* ================= Neural Rendering (RenoDX) =================

       Alterna o NR sem teclado, sem foco e sem SendInput. O RenoDX le a
       hotkey dele com GetAsyncKeyState -- a unica funcao de teclado que ele
       importa --, entao o add-on reescreve a entrada da IAT DELE e serve a
       tecla so para ele. Nada e injetado no sistema e nada vaza para o jogo.

       Existe porque o teclado sintetico nao funciona nesta maquina: a janela
       em primeiro plano e a GameInputServiceWindow, de um processo de
       integridade mais alta, e o UIPI descarta o SendInput sem sinalizar erro
       (ele ainda devolve 1). Ver docs/neural-sem-teclado.md.

       ALTERNA, nao "liga" nem "desliga" -- veja NeuralState() abaixo. */

    /* Da para alternar o NR pela ponte agora? false quando falta o add-on, o
       add-on e da v1, o RenoDX nao esta carregado, ou ele mudou e nao importa
       mais GetAsyncKeyState pelo nome. Nesses casos use o caminho antigo.

       NAO use isto para decidir "o neural esta instalado". Eu cheguei a
       sugerir isso ao painel e estava errado: com um add-on da v1 no lugar
       esta funcao e false enquanto o Neural Rendering pode estar instalado e
       funcionando perfeitamente -- so nao da para alterna-lo por aqui. O card
       diria "nao instalado" justamente para quem tem tudo certo e um
       .addon64 de uma versao anterior. Presenca de arquivos se verifica
       olhando os arquivos. Ver docs/painel.md 15.6. */
    inline bool NeuralToggleAvailable()
    {
        Status st;
        return GetStatus(st) && st.nr_hook_ready != 0;
    }

    /* Arma um toque na tecla do RenoDX. true = o toque foi armado e o RenoDX
       vai le-lo no proximo frame dele; false = a ponte nao pode fazer isso
       agora (caia no PokeReShade).

       true nao quer dizer "ja alternou": quem entrega e o proximo frame. Para
       confirmar de fato, compare Status::nr_delivered antes e depois -- ele so
       sobe quando o RenoDX efetivamente leu o toque -- ou continue conferindo
       o "NR toggled" no ReShade.log, que e a confirmacao de ponta a ponta. */
    inline bool ToggleNeural()
    {
        DLSS5BridgeCommand c {};
        c.type  = DLSS5_BRIDGE_CMD_NR_TOGGLE;
        c.count = 1;
        detail::put(c.name, "NeuralRendering");   /* o Submit exige um nome */
        return detail::send(c);
    }

    /* Grava um inteiro na config do ReShade, de dentro do processo dele.
       true = o add-on aceitou; false = add-on velho (protocolo < 3) ou fila
       cheia, e ai o chamador cai no ReShade.ini -- que funciona quando o
       ReShade ainda vai LER o arquivo (proxima abertura), mas perde a corrida
       com o AutoSave enquanto ele esta rodando. Ver CMD_SET_CONFIG_INT. */
    inline bool SetConfigInt(const char *secao, const char *chave, int valor)
    {
        if (secao == nullptr || chave == nullptr)
            return false;
        DLSS5BridgeCommand c {};
        c.type      = DLSS5_BRIDGE_CMD_SET_CONFIG_INT;
        c.count     = 1;
        c.ivalue[0] = valor;
        detail::put(c.effect, secao);
        detail::put(c.name, chave);
        return detail::send(c);
    }


    /* Grava um float na config do ReShade, de dentro do processo dele. Mesmo
       contrato do SetConfigInt: true = aceitou; false = add-on com protocolo
       < 4, e ai so resta o ReShade.ini (que vale na proxima abertura). */
    inline bool SetConfigFloat(const char *secao, const char *chave, float valor)
    {
        if (secao == nullptr || chave == nullptr)
            return false;
        DLSS5BridgeCommand c {};
        c.type      = DLSS5_BRIDGE_CMD_SET_CONFIG_FLOAT;
        c.count     = 1;
        c.fvalue[0] = valor;
        detail::put(c.effect, secao);
        detail::put(c.name, chave);
        return detail::send(c);
    }
    /* O Hook Method do add-on novo do RenoDX, lido da config VIVA: 0 Off,
       1 Auto, 2 Upscaled, 3 Hudless, 4 Present. -1 = nao sei (add-on antigo,
       ponte antiga, ou a chave ainda nao existe). E esta a resposta que vale
       enquanto o ReShade esta no ar; o arquivo so alcanca depois do AutoSave. */
    inline int NeuralHookPoint()
    {
        Status st;
        if (!GetStatus(st) || st.protocol < 3u)
            return -1;
        return st.nr_hookpoint;
    }

    /* Quantos toques o RenoDX ja LEU. E com isto que se confirma a entrega.

       NAO EXISTE uma versao "confirmada" bloqueante, e nao e esquecimento:
       eu escrevi uma e ela nao funciona. Quem lê o toque e o RenoDX, dentro
       do Present; esperar pela confirmacao na thread que apresenta impede o
       proprio Present que faria a entrega acontecer. Medido: chamada na
       thread de render devolve false depois de 254 ms de espera -- e o toggle
       acontece logo em seguida, quando o loop volta a apresentar. A mesma
       chamada de OUTRA thread devolve true em 28 ms.

       Como o AlternarTudo roda na thread do loop de mensagens, que e a que
       apresenta, o padrao certo e assincrono:

           uint32_t alvo = rsbridge::NeuralDelivered();
           if (!rsbridge::ToggleNeural())  { ...fallback... }
           // ... siga apresentando ...
           // dois ou tres quadros depois:
           bool entregue = rsbridge::NeuralDelivered() > alvo;

       Medido: a entrega sai no PRIMEIRO Present seguinte (9,9 / 19,9 / 19,9 ms
       em tres disparos -- e so o vsync a 60 Hz), entao dois quadros ja sao
       folga. Se preferir nao mudar o fluxo, a verificacao de 2,5 s pelo
       ReShade.log continua valendo e cobre a mesma coisa. */
    inline uint32_t NeuralDelivered()
    {
        Status st;
        return GetStatus(st) ? st.nr_delivered : 0u;
    }

    enum NeuralStateValue { NEURAL_DESCONHECIDO = -1, NEURAL_DESLIGADO = 0, NEURAL_LIGADO = 1 };

    /* SEMPRE NEURAL_DESCONHECIDO, e isto e uma constatacao, nao um esboco.

       O RenoDX nao publica o estado do NR em lugar nenhum que a ponte alcance:
       nao ha uniform, nem tecnica, nem export, nem evento da API de add-ons
       (ele nem usa a API 20 -- registra-se com a 18). O checkbox vive dentro
       do ImGui dele e o NeuralUplift so vai para o ReShade.ini quando o
       AutoSave do ReShade resolve gravar, com atraso de segundos.

       A fonte imediata continua sendo o ReShade.log, que e o que o
       NeuralPeloLog() do main.cpp ja faz. Esta funcao existe para que ninguem
       perca tempo procurando de novo. */
    inline int NeuralState()
    {
        return NEURAL_DESCONHECIDO;
    }

    /* ---- atalhos da nitidez ---- */

    /* Intensidade da nitidez, 0..1. 0 deixa o shader em identidade, mas a
       tecnica continua rodando (um passe barato); para tirar o custo use
       SetSharpenEnabled(false). Valores fora da faixa sao recusados aqui em
       vez de virarem clamp silencioso do lado do shader. */
    inline bool SetSharpen(float v)
    {
        if (!(v >= 0.0f) || v > 1.0f)
            return false;
        return SetUniformFloat(kSharpenEffect, "Sharpness", v);
    }

    /* 1 = nenhum overshoot (sem halo), 0 = halo liberado. */
    inline bool SetSharpenHalo(float v)
    {
        if (!(v >= 0.0f) || v > 1.0f)
            return false;
        return SetUniformFloat(kSharpenEffect, "HaloProtect", v);
    }

    /* Contraste local minimo (em luma 0..1) para haver realce -- e o que
       impede a nitidez de realcar grao de captura. */
    inline bool SetSharpenDenoise(float v)
    {
        if (!(v >= 0.0f) || v > 0.2f)
            return false;
        return SetUniformFloat(kSharpenEffect, "Denoise", v);
    }

    /* 0 = normal, 1 = mapa de ganho, 2 = delta aplicado. */
    inline bool SetSharpenDebugView(int mode)
    {
        if (mode < 0 || mode > 2)
            return false;
        return SetUniformInt(kSharpenEffect, "DebugView", mode);
    }

    inline bool SetSharpenEnabled(bool on)
    {
        return SetTechniqueEnabled(kSharpenTechnique, on, kSharpenEffect);
    }
}

/* Aliases no escopo global, que e a forma que o painel pediu.
   Defina DLSS5_BRIDGE_NO_GLOBAL_ALIASES antes do include para nao te-los. */
#ifndef DLSS5_BRIDGE_NO_GLOBAL_ALIASES
inline bool SetLut(int index)                                   { return rsbridge::SetLut(index); }
inline bool SetGradeEnabled(bool on)                            { return rsbridge::SetGradeEnabled(on); }
inline bool SetSharpen(float v)                                 { return rsbridge::SetSharpen(v); }
inline bool SetSharpenEnabled(bool on)                          { return rsbridge::SetSharpenEnabled(on); }
inline bool SetTechniqueEnabled(const char *tech, bool enabled) { return rsbridge::SetTechniqueEnabled(tech, enabled); }
inline bool ReShadeBridgeAvailable()                            { return rsbridge::Available(); }
inline bool ToggleNeural()                                      { return rsbridge::ToggleNeural(); }
inline bool NeuralToggleAvailable()                             { return rsbridge::NeuralToggleAvailable(); }
#endif

#endif /* !DLSS5_BRIDGE_ADDON_SIDE */
