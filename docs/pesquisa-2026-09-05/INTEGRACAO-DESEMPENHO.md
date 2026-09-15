# Integração do `desempenho.h` no `main.cpp` (passo a passo)

Módulo: `amd\desempenho.h` (header-only, namespace `Desempenho`). Medições e decisões: `RELATORIO-DESEMPENHO-JOGO.md` (mesma pasta). Teste isolado que compila o header e prova aplicar/restaurar: `scratchpad\otimizar-desempenho\medidor.cpp` (`probe`, `escada`, `medir`, `medir2`).

As âncoras abaixo são trechos textuais do `main.cpp` de 2026-09-05 15:46 (9.3 mil linhas). Os números de linha podem ter andado; procure pelo texto.

O que o módulo faz sozinho e o que precisa do host:

| Coisa | Quem faz |
|---|---|
| NvAPI (perfil do exe, FRL, salvar/restaurar, ini `[desempenho]`) | módulo |
| Prioridade de GPU do nosso processo (REALTIME, HIGH de reserva) | módulo |
| Leitura do HAGS (só log) | módulo |
| Escada automática (5 s por degrau, cartazes, resumo) | módulo, alimentado 1×/s pelo host |
| Gravar `work_resolution` no `dlss5-feed.cfg` | host (`GravarWorkResolution`, já existe) via callback |
| Cartaz, repintar painel, exe do alvo, camadas, teto, idioma | host, via `Contexto` e argumentos |

## 1. Include

Junto dos outros módulos, depois de `idioma.h` e antes do `namespace Hud`:

```cpp
#include "hdr_control.h"      // deteccao e tratamento de HDR (display e placa de captura)
#include "desempenho.h"       // limite de fps do jogo pelo driver, prioridade de GPU e o botao OTIMIZAR DESEMPENHO
```

## 2. Comandos

No `enum` dos `CMD_*` (âncora: `CMD_IDIOMA_BASE   = 600,`), a faixa 700 está vaga:

```cpp
       CMD_IDIOMA_BASE   = 600,  // + indice em kLinguas        -> 600..601
       CMD_JOGO_BASE     = 700,  // + indice em kModosJogo      -> 700..703  (Livre / 60 / 30 / Igual à película)
       CMD_OTIMIZAR      = 710,  // o botao OTIMIZAR DESEMPENHO (clicar de novo enquanto roda = parar)
```

E nos `static_assert` (âncora: `"idioma invade os atalhos"`):

```cpp
static_assert(CMD_IDIOMA_BASE   +   8 < CMD_JOGO_BASE,       "idioma invade o limite do jogo");
static_assert(CMD_JOGO_BASE     +   8 < CMD_OTIMIZAR,        "limite do jogo invade o otimizar");
static_assert(CMD_OTIMIZAR      +   1 < CMD_ATALHO_BASE,     "otimizar invade os atalhos");
```

Logo depois de `kWorkResNames[]`:

```cpp
// os chips "Jogo:" do painel, na ordem em que aparecem (valores de Desempenho::ModoJogo)
static const int kModosJogo[] = { Desempenho::JOGO_LIVRE, Desempenho::JOGO_60, Desempenho::JOGO_30, Desempenho::JOGO_IGUAL };
static const wchar_t* NomeModoJogo(int i) {
    return i == 0 ? Desempenho::Txt::ChipLivre(EmIngles()) : i == 1 ? L"60" : i == 2 ? L"30" : Desempenho::Txt::ChipIgual(EmIngles());
}
```

## 3. Abertura

Logo depois de `Log("DLSS5 For All %s iniciando", U8(APP_VERSION).c_str());` (o ini já está resolvido aí, `g_dadosDir` foi definido junto com o log):

```cpp
    // Limite do jogo / prioridade de GPU (desempenho.h). Iniciar() desfaz um limite que
    // tenha sobrado de uma queda (fica no ini, secao [desempenho]) e loga HAGS e NvAPI.
    Desempenho::g_logFn = [](const char* s) { Log("%s", s); };
    Desempenho::Iniciar(IniPath());
```

Recomendação medida (ver relatório, linha 1a'): aplicar a prioridade de GPU já na abertura, sempre. É a única alavanca que resolve sozinha ao vivo e não tem custo quando a placa está folgada. Se o líder aceitar, logo depois do `R.Init` bem-sucedido (âncora: `Log("D3D11 ok, swapchain waitable %ux%u FLIP_DISCARD", w, h);` é dentro do Init; no `wWinMain`, o ponto é logo após a chamada que cria a cadeia):

```cpp
    if (Desempenho::PrioridadeGpuMaxima()) Desempenho::g_est.prioAlta = true;
```

Se preferir deixar só para a escada, pule este trecho: o botão aplica no primeiro degrau.

## 4. Depois que o alvo é conhecido (sincronizar o modo "Jogo:")

Onde `scap.targetWin` passa a valer e `g_hud.camadas` já foi lido do ini (depois do seletor; âncora: `g_hud.camadas = n;` perto de `if (g_hud.camadas >= 3) { g_tetoAntesDo3x = g_hud.fpsCap; g_hud.fpsCap = 30; }`), uma função auxiliar e a primeira chamada:

```cpp
// O exe do alvo, so na fonte Janela (na fonte Monitor nao ha "o jogo"; camera idem).
static std::wstring ExeDoAlvo() {
    return (C.kind == SRC_WINDOW && scap.targetWin) ? Desempenho::ExeDaJanela(scap.targetWin) : L"";
}
// Poe o driver de acordo com o chip "Jogo:" e o teto da camada. Chamar quando o teto
// muda (CMD_FPS_*, Teto3x), quando as camadas mudam e quando a pessoa clica num chip.
static void SincronizarLimiteDoJogo(HINSTANCE hi) {
    const std::wstring cartaz = Desempenho::Sincronizar(ExeDoAlvo(), g_hud.camadas, C.fpsCap, EmIngles());
    if (!cartaz.empty()) { ShowToast(hi, C.mon, cartaz, 3500); Hud::Refresh(); }
}
```

Chamadas:

- uma vez depois do seletor/abertura (quando `C.fpsCap` e `g_hud.camadas` estão prontos): `SincronizarLimiteDoJogo(hi);`
- no `CMD_FPS_BASE` (âncora: `Log("teto de fps do filtro: %d", C.fpsCap);`): acrescentar `SincronizarLimiteDoJogo(hi);` depois do `Hud::Refresh();`
- no `CMD_CAMADAS_BASE` (âncora: `g_hud.camadas = n;` dentro do `else if (cmd > CMD_CAMADAS_BASE ...`), depois de `Teto3x(...)` ter ajustado `C.fpsCap`: `SincronizarLimiteDoJogo(hi);`
- nas quedas automáticas de camada (âncoras: `g_hud.camadas = k - 1; GuardarCamadas(k - 1);` e `g_hud.camadas = k - 1; Hud::Refresh();`): idem.

Com o 3x ligado e a pessoa sem escolha gravada, o modo padrão é "Igual à película" e o jogo vai a 30 no driver. O cartaz já diz "vale quando o jogo for reaberto" (medido: o FRL do perfil não pega no jogo já aberto).

## 5. Comandos no laço principal

Logo antes de `} else if (cmd >= CMD_FPS_BASE && cmd < CMD_FPS_BASE + ...` (o bloco dos comandos do painel):

```cpp
        } else if (cmd >= CMD_JOGO_BASE && cmd < CMD_JOGO_BASE + 4) {
            // chip "Jogo: Livre / 60 / 30 / Igual à película": grava a escolha e sincroniza o driver
            const int modo = kModosJogo[cmd - CMD_JOGO_BASE];
            const std::wstring cartaz = Desempenho::Manual(ExeDoAlvo(), modo, g_hud.camadas, C.fpsCap, EmIngles());
            ShowToast(hi, C.mon, cartaz, 3500);
            Hud::Refresh();
        } else if (cmd == CMD_OTIMIZAR) {
            if (Desempenho::g_otim.Ativo()) {
                Desempenho::g_otim.Cancelar();
                ShowToast(hi, C.mon, TS(L"Otimização parada", L"Optimization stopped"), 1600);
            } else {
                Desempenho::Contexto ctx;
                ctx.exeAlvo       = ExeDoAlvo();
                ctx.nvidia        = (g_gpu.vendor == 0x10DE) && Desempenho::LimiteJogoDisponivel();
                ctx.alvoFps       = (g_hud.camadas >= 2) ? 30 : 60;          // 1x mira 60; 2x/3x miram 30
                if (C.fpsCap && C.fpsCap < ctx.alvoFps) ctx.alvoFps = C.fpsCap;  // com teto, o alvo e o teto
                ctx.en            = EmIngles();
                ctx.limiteAoVivo  = Desempenho::kLimiteAoVivo;   // false (medido): o FRL vale na reabertura
                ctx.workResAoVivo = Desempenho::kWorkResAoVivo;  // true (medido): o Feeder rele o cfg e reconstroi
                {   // o work_resolution em vigor (mesma conta do CMD_WORKRES_BASE)
                    int efetivo = g_hud.workRes;
                    if (!efetivo) { const double mp = (double)R.srcW * (double)R.srcH / 1e6; efetivo = (mp <= 2.4) ? 100 : (mp <= 4.0) ? 77 : (mp <= 6.0) ? 67 : 58; }
                    ctx.workResAtual = efetivo;
                }
                ctx.toast         = [hi](const std::wstring& s, UINT ms) { ShowToast(hi, C.mon, s, ms); };
                ctx.aplicarWorkRes = [](int v) {
                    g_hud.workRes = v; IniSet(L"work_resolution", std::to_wstring(v));
                    GravarWorkResolution(v);               // o Feeder rele o cfg em ~60 quadros e reconstroi
                    return true;
                };
                ctx.repintar      = []() { Hud::Refresh(); };
                Desempenho::g_otim.Iniciar(ctx);
            }
            Hud::Refresh();
```

Observação: `ShowToast` já ignora chamadas na segunda camada (`g_camada2`), então a escada só fala pela primeira.

## 6. Alimentar a escada: 1 vez por segundo

No tique de 1 s, logo depois de `g_hud.fpsSrc = fps; g_hud.fpsPresent = pfps;`:

```cpp
            Desempenho::g_otim.Amostra1s(pfps);   // a escada mede 5 s por degrau com o fps apresentado
```

(`pfps` é o fps apresentado pela nossa cadeia, o mesmo número do painel. Nada a fazer quando a escada não está ativa: a função sai na primeira linha.)

## 7. Encerramento

No encerramento normal, logo depois de `CamadasParar();   // nenhuma camada de cima fica orfa`:

```cpp
    Desempenho::Encerrar();   // devolve o perfil do driver como estava (o jogo continua limitado ate ser reaberto)
```

Queda sem passar por aqui: o `Iniciar()` da próxima abertura restaura pelo ini. Isso foi testado no `medidor.exe escada` (simula a queda e a abertura seguinte).

## 8. Painel

### 8.1 Tipos de item

No `enum EKind` acrescentar `E_JOGO, E_OTIMIZAR`. No `Activate`:

```cpp
    case E_JOGO:     g_trayCmd = CMD_JOGO_BASE + it.val; break;
    case E_OTIMIZAR: g_trayCmd = CMD_OTIMIZAR; break;
```

### 8.2 A seção "FPS DO JOGO" na coluna "Mais opções"

Dentro de `colunaMaisA`, logo depois do bloco `AJUSTES` (antes de `colunaMaisB` ser chamado). Segue o padrão dos chips do teto de fps:

```cpp
    // -------- FPS DO JOGO: limitar o jogo pelo driver e o botao OTIMIZAR DESEMPENHO (desempenho.h)
    {
        const bool en = EmIngles();
        const std::wstring exe = ExeDoAlvo();
        const bool navegador = Desempenho::EhNavegador(exe);
        const bool nvidia = (g_gpu.vendor == 0x10DE) && Desempenho::LimiteJogoDisponivel();
        const bool podeLimitar = nvidia && !exe.empty() && !navegador;
        if (g) Txt(g, Desempenho::Txt::RotuloSecao(en), fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;
        // a frase curta, sem jargao (regra da interface): so o efeito garantido
        if (g) Txt(g, Desempenho::Txt::Explicacao(en), fItemSub, C_BODY, RectF(L, cy, IW, 34), 0, true);
        cy += 38;
        const int modoAtual = Desempenho::ModoAtual(g_hud.camadas);
        const REAL gap = 8, bw = (IW - gap * 3) / 4, bh = 34;
        for (int i = 0; i < 4; i++) {
            RectF r(L + i * (bw + gap), cy, bw, bh);
            const int idx = (int)g_items.size();
            const bool sel = (modoAtual == kModosJogo[i]);
            Item it{ r, E_JOGO, i, podeLimitar, sel }; g_items.push_back(it);
            if (g) {
                FillRound(g, r, 9.f, sel ? C_ACC : (g_hot == idx && podeLimitar ? C_ROWH : C_ROW));
                Txt(g, NomeModoJogo(i), fChip, sel ? C_DARK : (podeLimitar ? C_BODY : C_FAINT), r, 1, true);
            }
        }
        cy += bh + 7;
        if (g) {
            const wchar_t* sub =
                !nvidia    ? Desempenho::Txt::SemNvidia(en)                                   // AMD/Intel: "não disponível nesta placa ainda"
              : navegador  ? Desempenho::Txt::SemLimiteNavegador(en)                          // "vídeo no navegador: sem limite disponível"
              : exe.empty()? T(L"só na fonte Janela", L"window source only")
              : Desempenho::g_est.jogoLimite
                           ? T(L"vale quando o jogo for reaberto — “Livre” também só vale ao reabrir",
                               L"takes effect when the game is reopened — “Free” also needs a reopen")
                           : T(L"“Igual à película” segue o teto de fps acima (padrão com 3x)",
                               L"“Same as layer” follows the fps cap above (default with 3x)");
            Txt(g, sub, fItemSub, C_FAINT, RectF(L, cy, IW, 18), 0, true);
        }
        cy += 24;
        {   // o botao
            RectF r(L, cy, IW, 46);
            const int idx = (int)g_items.size();
            const bool rodando = Desempenho::g_est.otimizando;
            Item it{ r, E_OTIMIZAR, 0, true, rodando }; g_items.push_back(it);
            if (g) {
                FillRound(g, r, 12.f, rodando ? C_ACC : (g_hot == idx ? C_ROWH : C_ROW));
                Txt(g, rodando ? Desempenho::Txt::BotaoParar(en) : Desempenho::Txt::Botao(en),
                    fItem, rodando ? C_DARK : C_TXT, RectF(r.X + 16, r.Y + 3, r.Width - 32, 22), 0, true);
                Txt(g, Desempenho::Txt::BotaoSub(en), fItemSub, rodando ? C_DARK : C_BODY, RectF(r.X + 16, r.Y + 24, r.Width - 32, 18), 0, true);
            }
            cy += 46 + 6;
        }
        if (g && !Desempenho::g_est.linha.empty())   // o resumo da ultima rodada ("Otimizado: ...", "Aplicado: ...")
            Txt(g, Desempenho::g_est.linha.c_str(), fItemSub, Desempenho::g_est.linhaAviso ? C_WARN : C_ACC2, RectF(L, cy, IW, 34), 0, true);
        cy += Desempenho::g_est.linha.empty() ? 4 : 38;
    }
```

Se `g_gpu` não estiver visível dentro do `namespace Hud` nesse ponto, troque `g_gpu.vendor == 0x10DE` por um `bool` que o host preenche em `HudState` (por exemplo `g_hud.placaNvidia`), do mesmo jeito que `camada2Ok` é preenchido.

### 8.3 Assinatura visível (obrigatório, ver o comentário `REGRA PARA QUEM MEXER NO Render()`)

Em `AssinaturaVisivel()`, junto de `SigMix(h, (uint64_t)g_hud.fpsCap);`:

```cpp
    SigMix(h, (uint64_t)(Desempenho::g_est.jogoLimite + 1000));
    SigMix(h, Desempenho::g_est.otimizando ? 1u : 0u);
    SigMix(h, Desempenho::g_est.prioAlta ? 1u : 0u);
    SigMix(h, (uint64_t)(Desempenho::ModoAtual(g_hud.camadas) + 100));
    SigStr(h, Desempenho::g_est.linha);
```

E em `AssinaturaAltura()` a linha de resumo muda a altura: `SigMix(h, Desempenho::g_est.linha.empty() ? 0u : 1u);`.

## 9. Textos (pt / en) que o módulo já traz

| Onde | pt | en |
|---|---|---|
| explicação | Se o jogo usa a placa toda, a película fica lenta. Limitar o jogo a 60 ou 30 libera a placa. | If the game uses the whole card, the layer gets slow. Capping the game at 60 or 30 frees the card. |
| rótulo | FPS DO JOGO | GAME FPS |
| chips | Livre · 60 · 30 · Igual à película | Free · 60 · 30 · Same as layer |
| botão | OTIMIZAR DESEMPENHO / mede 5 s por degrau e só vai até onde precisa | OPTIMIZE PERFORMANCE / measures 5 s per step and only goes as far as needed |
| rodando | Otimizando… clique para parar | Optimizing… click to stop |
| AMD | não disponível nesta placa ainda | not available on this card yet |
| navegador | vídeo no navegador: sem limite disponível | video in the browser: no cap available |
| pendente | vale quando o jogo for reaberto | takes effect when the game is reopened |
| cartazes da escada | Otimizando… película com prioridade na placa · Otimizando… resolução do DLSS 80% · Aplicado: jogo a 30 fps — reabra o jogo para ver o efeito · Otimizado: prioridade da placa, película a 42 fps · Não deu para melhorar: película a 12 fps (…). Tente menos camadas. | idem em inglês |

## 10. Uma correção de texto que a medição pede

O cartaz do `CMD_WORKRES_BASE` diz que a resolução do DLSS "não vale agora, só na próxima abertura". Medido em 2026-09-06 com o `dlss5-feed.addon64` de 2026-09-01: o Feeder relê o `dlss5-feed.cfg` com a camada aberta e reconstrói a feature na hora (`[feed] config: ... work_resolution=67%` seguido de `building: ... (67%)`, ~1 s depois da gravação). O cartaz e o comentário podem passar a dizer que vale na hora.

## 11. Checklist de teste depois de integrar

1. Abrir com um jogo na fonte Janela e o 3x ligado: cartaz "Jogo limitado a 30 fps (igual à película) — vale quando o jogo for reaberto"; `dlss5forall.ini` ganha `[desempenho] exe=... original=-1 perfil_criado=1 aplicado=30`; o Painel de Controle da NVIDIA mostra o perfil "DLSS5ForAll - <exe>" com Taxa máxima de quadros 30.
2. Sair do app: o perfil some (ou volta ao valor original) e o ini fica sem a seção.
3. Matar o app pelo Gerenciador de Tarefas com o limite aplicado; abrir de novo: log "limite do jogo: sobrou ... restaurando".
4. Botão OTIMIZAR com a placa saturada: cartazes a cada 6 s; no fim uma linha no painel.
5. Fonte Monitor / câmera: chips desabilitados com "só na fonte Janela".
6. Alvo Edge/Chrome: chips desabilitados com "vídeo no navegador: sem limite disponível".
