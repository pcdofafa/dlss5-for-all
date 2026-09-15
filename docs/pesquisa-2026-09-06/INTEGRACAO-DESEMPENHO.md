# Como plugar o `desempenho.h` no app

**Para quem edita o `amd\main.cpp`.** Este documento é o passo a passo completo: onde cortar,
o que colar e o que conferir depois. Nenhum trecho aqui foi aplicado — quem escreveu o módulo
não é dono do `main.cpp`.

Antes de começar, vale ler a decisão e os números em `RELATORIO-DESEMPENHO-JOGO.md`, nesta
mesma pasta. O resumo de uma linha: **a prioridade de GPU resolve ao vivo; o limite de fps do
jogo só vale quando o jogo for reaberto**, e a interface toda gira em torno dessa diferença.

---

## 0. O contrato do módulo

`amd\desempenho.h` é header-only, no estilo do `seletor_hud.h`. Ele **não conhece o `main.cpp`**:
tudo o que precisa do app chega por ponteiro de função ou callback. Por isso ele compila e é
testado sozinho (`scratchpad\otimizar-desempenho\teste-desempenho.cpp`, 56 verificações).

O que ele oferece:

| chamada | o que faz |
|---|---|
| `Desempenho::Iniciar(ini)` | liga o log ao ini e **desfaz limite que tenha sobrado de uma queda** |
| `Desempenho::Encerrar()` | devolve o driver como estava |
| `Desempenho::Sincronizar(exe, camadas, teto, en)` | põe o driver de acordo com o modo em vigor; devolve o texto do cartaz (vazio = nada mudou) |
| `Desempenho::Manual(exe, modo, camadas, teto, en)` | um chip foi clicado: grava a escolha e sincroniza |
| `Desempenho::g_otim.Iniciar(ctx)` | dispara a escada do botão |
| `Desempenho::g_otim.Amostra1s(fps)` | **uma vez por segundo**, com o fps da película |
| `Desempenho::g_otim.Cancelar()` | a pessoa clicou de novo no botão |
| `Desempenho::g_est` | o que o painel desenha: `jogoLimite`, `prioAlta`, `otimizando`, `linha`, `linhaAviso` |
| `Desempenho::Txt::*` | os textos, já em português e inglês |

E o que ele **nunca** faz sozinho: mexer em processo do jogo, mudar configuração do sistema
(o agendamento de GPU por hardware é só lido), gravar em ini que não seja o nosso, ou tocar no
perfil de um navegador.

---

## 1. O include

**Âncora:** `main.cpp:5286`, onde o `seletor_hud.h` entra.

```cpp
#include "seletor_hud.h"   // a primeira tela (escolha da fonte), desenhada como o painel
#include "desempenho.h"    // limite de fps do jogo, prioridade de GPU e o botao "Otimizar"
```

Ele precisa de `gdi32`, que a linha de build já tem. **Não** precisa de `.lib` da NVIDIA:
`nvapi64.dll` é carregada em tempo de execução, e se não existir o módulo escreve uma linha
no log e a alavanca some da interface.

---

## 2. Os números de comando

**Âncora:** o `enum` de comandos em `main.cpp:2690-2712`. Duas entradas novas, nas faixas
livres (700 está vago; a faixa 1000–2500 é reservada e continua reservada):

```cpp
       CMD_EFEITOS = 312,        // abre/fecha a janela de Efeitos (botao do painel ou tecla)
       CMD_OTIMIZAR = 314,       // o botao "OTIMIZAR DESEMPENHO" (dispara e cancela a escada)
       /* 313 foi CMD_CAMADA2 ... */
       CMD_CAMADAS_BASE = 320,
       ...
       CMD_IDIOMA_BASE   = 600,  // + indice em kLinguas        -> 600..601
       CMD_JOGOFPS_BASE  = 700,  // + indice em kJogoFps        -> 700..703
```

E o `static_assert` que trava a reincidência, junto dos outros em `main.cpp:2716-2726`:

```cpp
static_assert(CMD_OTIMIZAR      +   1 < CMD_FPS_BASE,        "otimizar invade o teto de fps");
static_assert(CMD_IDIOMA_BASE   +   8 < CMD_JOGOFPS_BASE,    "idioma invade o fps do jogo");
static_assert(CMD_JOGOFPS_BASE  +  16 < CMD_ATALHO_BASE,     "fps do jogo invade os atalhos");
```

A tabela dos chips, junto de `kFpsCaps` e `kWorkRes` (`main.cpp:2731-2736`):

```cpp
// Os chips "FPS DO JOGO". A ordem e a da tela; JOGO_IGUAL e -1 no modulo.
static const int kJogoFps[] = { Desempenho::JOGO_LIVRE, Desempenho::JOGO_IGUAL,
                                Desempenho::JOGO_60,    Desempenho::JOGO_30 };
static const wchar_t* NomeJogoFps(int i) {
    switch (i) {
    case 0:  return Desempenho::Txt::ChipLivre(g_en);
    case 1:  return Desempenho::Txt::ChipIgual(g_en);
    case 2:  return L"60";
    default: return L"30";
    }
}
```

> Troque `g_en` pelo que o `main.cpp` usa para saber o idioma no ponto de uso (o mesmo que
> alimenta o `T(pt, en)` do `idioma.h`).

---

## 3. O item de painel

**Âncora:** o `enum EKind` em `main.cpp:3928`.

```cpp
enum EKind { E_NONE = 0, E_FILTER, E_NEURAL, E_FPS, E_WORKRES,
             E_VSYNC, E_HDR, E_OFERTAS, E_CANAL, E_GUIA, E_INTERATIVO, E_SAIR,
             E_ATALHO, E_ATALHO_PADRAO, E_REABRIR, E_FPSTELA, E_IDIOMA, E_EFEITOS,
             E_CAMADAS, E_MAIS, E_JOGOFPS, E_OTIMIZAR };
```

**Âncora:** `Hud::Activate`, `main.cpp:5050`.

```cpp
    case E_FPS:      g_trayCmd = CMD_FPS_BASE + it.val; break;
    case E_JOGOFPS:  g_trayCmd = CMD_JOGOFPS_BASE + it.val; break;
    case E_OTIMIZAR: g_trayCmd = CMD_OTIMIZAR; break;
```

---

## 4. A seção no painel

**Onde:** na **coluna da direita**, dentro de `colunaMaisA` (`main.cpp:4420`), logo depois do
bloco "AJUSTES". A coluna esquerda não muda: a regra da casa é no máximo três decisões na
primeira tela, e "FPS do jogo" é a quarta.

**Mock em texto** (o que a pessoa vê ao abrir "Mais opções"):

```
 MAIS OPÇÕES
 ─────────────────────────────────────────────
 AJUSTES
   [ ] V-Sync
   [ ] Desligar o HDR do monitor (plano B)
   [ ] Mostrar o FPS na tela

 FPS DO JOGO
   ┌────────┬──────────────────┬──────┬──────┐
   │ Livre  │ Igual à película │  60  │  30  │
   └────────┴──────────────────┴──────┴──────┘
   Se o jogo usa a placa toda, a película fica lenta.
   Limitar o jogo a 60 ou 30 libera a placa.
   ⚠ vale quando o jogo for reaberto

   ╔═══════════════════════════════════════════╗
   ║          OTIMIZAR DESEMPENHO              ║
   ╚═══════════════════════════════════════════╝
   mede 5 s por degrau e só vai até onde precisa
```

Estados especiais dos chips:

- **Sem NvAPI (máquina AMD):** os quatro chips ficam apagados e a frase vira
  "não disponível nesta placa ainda". O botão continua, porque a prioridade de GPU e a
  resolução do DLSS funcionam em qualquer placa.
- **Alvo é navegador:** chips apagados e "vídeo no navegador: sem limite disponível".
- **Escada rodando:** o botão vira "Otimizando… clique para parar".

O trecho, colado logo antes do fecho `};   // colunaMaisA` em `main.cpp:4532`:

```cpp
    // -------- FPS DO JOGO (o limite do jogo pelo driver + o botao da escada)
    // A pelicula divide a placa com o jogo. Jogo com fps livre satura a GPU e a pelicula
    // some (medido: 9,3 fps contra 99 com a prioridade de GPU alta). Ver
    // docs\pesquisa-2026-09-06\RELATORIO-DESEMPENHO-JOGO.md.
    {
        const bool en = g_en;
        const std::wstring exeAlvo = Desempenho::ExeDaJanela(scap.targetWin);
        const bool navegador = Desempenho::EhNavegador(exeAlvo);
        const bool temDriver = Desempenho::LimiteJogoDisponivel();
        const bool podeLimitar = temDriver && !navegador && !exeAlvo.empty();
        const int  modo = Desempenho::ModoAtual(g_hud.camadas);

        if (g) Txt(g, Desempenho::Txt::RotuloSecao(en), fLbl, C_FAINT, RectF(L, cy, IW, 16), 0);
        cy += 22;
        {
            const REAL gap = 8, bw = (IW - gap * 3) / 4, bh = 34;
            for (int i = 0; i < 4; i++) {
                RectF r(L + i * (bw + gap), cy, bw, bh);
                const int idx = (int)g_items.size();
                const bool sel = (modo == kJogoFps[i]);
                // "Livre" continua clicavel sem driver: e como se desliga o que ficou preso
                const bool en_i = podeLimitar || kJogoFps[i] == Desempenho::JOGO_LIVRE;
                Item it{ r, E_JOGOFPS, i, en_i, sel }; g_items.push_back(it);
                if (g) {
                    FillRound(g, r, 9.f, sel ? (en_i ? C_ACC : C_FAINT)
                                             : ((g_hot == idx && en_i) ? C_ROWH : C_ROW));
                    Txt(g, NomeJogoFps(i), fChip, sel ? C_DARK : (en_i ? C_BODY : C_FAINT), r, 1);
                }
            }
            cy += bh + 7;
        }
        // a frase muda conforme o que impede o limite - nunca some, para a pessoa nao ficar
        // olhando quatro chips apagados sem explicacao
        const wchar_t* sub = !temDriver  ? Desempenho::Txt::SemNvidia(en)
                           : navegador   ? Desempenho::Txt::SemLimiteNavegador(en)
                                         : Desempenho::Txt::Explicacao(en);
        if (g) Txt(g, sub, fItemSub, C_FAINT, RectF(L, cy, IW, 32), 0, true);
        cy += 34;
        if (podeLimitar && Desempenho::g_est.jogoLimite > 0) {
            if (g) Txt(g, Desempenho::Txt::ProximaAbertura(en), fItemSub, C_WARN, RectF(L, cy, IW, 18), 0, true);
            cy += 22;
        }

        // -------- o botao da escada
        {
            const REAL bh = 40;
            RectF r(L, cy, IW, bh);
            const int idx = (int)g_items.size();
            const bool rodando = Desempenho::g_est.otimizando;
            Item it{ r, E_OTIMIZAR, 0, true, rodando }; g_items.push_back(it);
            if (g) {
                FillRound(g, r, 10.f, rodando ? C_ACC : (g_hot == idx ? C_ROWH : C_ROW));
                Txt(g, rodando ? Desempenho::Txt::BotaoParar(en) : Desempenho::Txt::Botao(en),
                    fChip, rodando ? C_DARK : C_BODY, r, 1);
            }
            cy += bh + 5;
            const wchar_t* bsub = (podeLimitar && !Desempenho::kLimiteAoVivo)
                                ? Desempenho::Txt::BotaoSubPendente(en)
                                : Desempenho::Txt::BotaoSub(en);
            if (g) Txt(g, bsub, fItemSub, C_FAINT, RectF(L, cy, IW, 18), 0, true);
            cy += 22;
        }
        // a linha de status da ultima escada (o resumo), enquanto houver o que dizer
        if (!Desempenho::g_est.linha.empty()) {
            if (g) Txt(g, Desempenho::g_est.linha.c_str(), fItemSub,
                       Desempenho::g_est.linhaAviso ? C_WARN : C_FAINT, RectF(L, cy, IW, 32), 0, true);
            cy += 34;
        }
        cy += 6;
    }
```

> **Uma coisa a não esquecer:** a assinatura de repintura, em `AssinaturaVisivel`
> (`main.cpp:4778`, junto do `SigMix(h, g_hud.maisAberto ...)` da linha 4848), precisa das
> entradas novas. Sem isso o painel não redesenha quando o estado muda, porque ele pula a
> repintura quando a assinatura não muda:

```cpp
    SigMix(h, (uint64_t)Desempenho::g_est.jogoLimite);
    SigMix(h, Desempenho::g_est.otimizando ? 1ull : 0ull);
    SigStr(h, Desempenho::g_est.linha);
```

`C_WARN` (`main.cpp:3982`) é o âmbar que o painel já usa nos avisos — o mesmo do cartaz de
"acima de 30 fps a placa satura".

---

## 5. Os cliques

**Âncora:** a cadeia de `else if` do `g_trayCmd`, junto do bloco `CMD_FPS_BASE` em
`main.cpp:8382`.

```cpp
        } else if (cmd >= CMD_JOGOFPS_BASE && cmd < CMD_JOGOFPS_BASE + (int)(sizeof(kJogoFps) / sizeof(kJogoFps[0]))) {
            const int i = cmd - CMD_JOGOFPS_BASE;
            const std::wstring msg = Desempenho::Manual(
                Desempenho::ExeDaJanela(scap.targetWin), kJogoFps[i],
                g_hud.camadas, C.fpsCap, g_en);
            Hud::Refresh();
            if (!msg.empty()) ShowToast(hi, C.mon, msg, 4000);
        } else if (cmd == CMD_OTIMIZAR) {
            if (Desempenho::g_otim.Ativo()) {
                Desempenho::g_otim.Cancelar();
            } else {
                Desempenho::Contexto ctx;
                ctx.exeAlvo       = Desempenho::ExeDaJanela(scap.targetWin);
                ctx.nvidia        = Desempenho::LimiteJogoDisponivel();
                // o alvo e o teto que a pelicula tem; sem teto, 60 e o que vale a pena
                ctx.alvoFps       = C.fpsCap > 0 ? C.fpsCap : 60;
                ctx.en            = g_en;
                ctx.limiteAoVivo  = Desempenho::kLimiteAoVivo;    // false: medido
                ctx.workResAoVivo = Desempenho::kWorkResAoVivo;   // true:  medido
                ctx.workResAtual  = LerWorkResolutionAtual();     // ver a nota abaixo
                ctx.aplicarWorkRes = [](int v) { GravarWorkResolution(v); return true; };
                ctx.toast    = [&](const std::wstring& s, UINT ms) { ShowToast(hi, C.mon, s, ms); };
                ctx.repintar = []() { Hud::Refresh(); };
                Desempenho::g_otim.Iniciar(ctx);
            }
            Hud::Refresh();
        }
```

**A nota do `workResAtual`:** `GravarWorkResolution` (`main.cpp:2213`) só escreve. A escada
precisa saber de onde está partindo, para não "descer" para um valor maior que o atual. O app já
calcula esse número na subida da cadeia (`main.cpp:7608-7619`): guarde-o numa variável do escopo
do `wWinMain` no momento em que ele é decidido e devolva-a aqui, em vez de reler o `.cfg`.

```cpp
    // junto de main.cpp:7619, onde GravarWorkResolution(wr) ja e chamado
    GravarWorkResolution(wr);
    g_workResAtual = wr;   // a escada de desempenho precisa saber de onde parte
```

---

## 6. O tique de um segundo

**Âncora:** `main.cpp:9091`, o bloco `if (now - lastTitle >= 1000)`, logo depois de `pfps` ser
calculado (`main.cpp:9106`). `pfps` é exatamente o fps que a película apresenta — é o que a
escada mede.

```cpp
            double pfps = (presented - lastPresented) * 1000.0 / (double)(now - lastTitle);
            lastFrames = f; lastPresented = presented;

            // a escada de desempenho mede a pelicula com o mesmo numero que o painel mostra
            Desempenho::g_otim.Amostra1s(pfps);
```

Ponha a chamada **antes** dos blocos de diagnóstico que podem dar `continue` ou sair do laço.
Um segundo perdido não quebra a escada (ela conta amostras, não relógio), mas atrasa o resultado.

---

## 7. Abertura e saída

**Abertura — âncora:** logo depois de `scap.targetWin` receber o alvo (`main.cpp:7528`) e do
`Log("Fonte: ...")`. Tem de ser depois, porque o módulo precisa do exe do alvo.

```cpp
    Desempenho::g_logFn = [](const char* s) { Log("%s", s); };
    Desempenho::Iniciar(IniPath());
    // a pelicula e o caminho da imagem ate a tela: ela pede a vez do compositor.
    // Medido: com o jogo saturando a placa, isso leva a pelicula de 9 para 99 fps.
    Desempenho::g_est.prioAlta = Desempenho::PrioridadeGpuMaxima();
```

Marcar `prioAlta` só quando o sistema aceitou não é detalhe: a escada do botão pula o degrau da
prioridade quando essa marca já está de pé, e pular um degrau que nunca foi aplicado deixaria o
melhor ganho na mesa.

`Iniciar` já desfaz sozinho qualquer limite que tenha sobrado de uma queda do app — é a única
proteção que existe contra o jogo do usuário ficar preso em 30 fps depois de um travamento.
**Não deixe essa chamada para depois de nenhum caminho que possa dar `Fatal`.**

Logo em seguida, a primeira sincronização (ela é quem aplica o padrão "igual à película" quando
o 3x já vem ligado da sessão anterior, em `main.cpp:7938`):

```cpp
    {
        const std::wstring m = Desempenho::Sincronizar(
            Desempenho::ExeDaJanela(scap.targetWin), g_hud.camadas, C.fpsCap, g_en);
        if (!m.empty()) ShowToast(hi, C.mon, m, 4000);
    }
```

**Saída — âncora:** o bloco de encerramento, junto do `Log("Encerrando (apresentados=%u)")`
(por volta de `main.cpp:9361`), **antes** do `R.Shutdown()`.

```cpp
    Desempenho::Encerrar();   // devolve o perfil do jogo no driver ao que era
```

A prioridade de GPU não precisa ser desfeita: ela morre com o processo.

---

## 8. Manter o driver em dia quando o teto muda

`Sincronizar` é idempotente: só toca no driver se o limite pedido for diferente do que já está
gravado. Chame em **todo** ponto onde o teto da película ou o número de camadas muda, senão o
modo "igual à película" fica desencontrado.

Uma função curta no escopo do `wWinMain` evita repetir o mesmo bloco quatro vezes:

```cpp
    auto sincDesempenho = [&]() {
        const std::wstring m = Desempenho::Sincronizar(
            Desempenho::ExeDaJanela(scap.targetWin), g_hud.camadas, C.fpsCap, g_en);
        if (!m.empty()) ShowToast(hi, C.mon, m, 4000);
        Hud::Refresh();
    };
```

Os quatro pontos de chamada:

| âncora | o que mudou ali |
|---|---|
| `main.cpp:8397`, fim do bloco `CMD_FPS_BASE` | a pessoa mudou o teto da película |
| `main.cpp:8398`, fim do bloco dos chips de camadas | 1x/2x/3x mudou (e o 3x liga o teto de 30) |
| `main.cpp:7938`, depois de `Teto3x(n >= 3, C.fpsCap)` | o 3x lembrado impôs o teto na abertura |
| `main.cpp:8324`, depois de `Teto3x(false, C.fpsCap)` | saiu do 3x e o teto anterior voltou |

Não é preciso chamar quando o alvo muda: o app relança para trocar de alvo, e a abertura já
sincroniza.

---

## 9. Os textos, em português e inglês

Todos já estão em `Desempenho::Txt` (`desempenho.h:520-535`), com a assinatura `(bool en)`.

| onde | português | inglês |
|---|---|---|
| título da seção | FPS DO JOGO | GAME FPS |
| explicação | Se o jogo usa a placa toda, a película fica lenta. Limitar o jogo a 60 ou 30 libera a placa. | If the game uses the whole card, the layer gets slow. Capping the game at 60 or 30 frees the card. |
| chips | Livre · Igual à película · 60 · 30 | Free · Same as layer · 60 · 30 |
| aviso do limite | vale quando o jogo for reaberto | takes effect when the game is reopened |
| navegador | vídeo no navegador: sem limite disponível | video in the browser: no cap available |
| sem NVIDIA | não disponível nesta placa ainda | not available on this card yet |
| botão | OTIMIZAR DESEMPENHO | OPTIMIZE PERFORMANCE |
| sob o botão | mede 5 s por degrau e só vai até onde precisa | measures 5 s per step and only goes as far as needed |
| sob o botão (limite pendente) | o limite do jogo só vale quando o jogo for reaberto | the game cap only applies when the game is reopened |
| botão rodando | Otimizando… clique para parar | Optimizing… click to stop |

Os cartazes da escada saem prontos do módulo, um por degrau. A sequência de uma rodada completa,
verbatim do teste:

```
Otimizando… medindo a película
Otimizando… película com prioridade na placa
Otimizando… resolução do DLSS 80%
Otimizando… resolução do DLSS 67%
Aplicado: película com prioridade, DLSS a 67%, jogo a 30 fps — reabra o jogo para ver o efeito
```

E os três finais possíveis:

- **Deu certo:** `Otimizado: película com prioridade, película a 42 fps`
- **Aplicado, falta reabrir:** `Aplicado: … — reabra o jogo para ver o efeito` (em âmbar)
- **Não deu:** `Não deu para melhorar: película a 25 fps (…). Tente menos camadas.` (em âmbar)

---

## 10. O que conferir depois de integrar

1. **Compila com `-Wall -Wextra` sem aviso novo.** O módulo tem funções `static` que só o
   `main.cpp` usa; até elas serem usadas, o g++ avisa "defined but not used". Depois da
   integração completa esses avisos somem sozinhos.
2. **Abra e feche o app sem tocar em nada.** No `dlss5forall.log` devem aparecer a linha
   `desempenho: nvapi=1 hags=... prioridade de gpu atual=...` e a subida da prioridade. No
   `dlss5forall.ini` **não** pode sobrar seção `[desempenho]` com `exe=`.
3. **Clique "30", feche o app à força** (Gerenciador de Tarefas). Reabra. O log tem de dizer
   `limite do jogo: sobrou ... - restaurando`, e o perfil no painel da NVIDIA tem de sumir.
   É o teste que mais importa: sem ele, um travamento nosso deixa o jogo da pessoa preso em
   30 fps sem ela saber por quê.
4. **Ligue o 3x com um jogo aberto.** O cartaz tem de dizer "Jogo limitado a 30 fps (igual à
   película) / vale quando o jogo for reaberto". Volte para 1x: "Jogo: livre de novo".
5. **Aponte para uma janela do Edge.** Os chips ficam apagados e a frase vira "vídeo no
   navegador: sem limite disponível". Confira no painel da NVIDIA que **nenhum** perfil de
   `msedge.exe` foi criado.
6. **Rode o teste do módulo** depois de qualquer mexida no `desempenho.h`:
   `scratchpad\otimizar-desempenho\teste-desempenho.exe` tem de terminar com
   `56 passaram, 0 falharam` e código de saída 0.

---

## 11. O que ficou de fora, de propósito

- **Limite de fps em AMD.** `LimiteJogoDisponivelAmd()` devolve `false` e a interface diz "não
  disponível nesta placa ainda". Quem for implementar mexe só nessa função e em
  `LimiteJogoAplicar`/`Restaurar`; o resto do módulo e o painel não mudam.
- **Limitar o jogo ao vivo.** Só há um jeito conhecido (injetar no processo do jogo, como o
  RTSS faz) e ele é incompatível com anticheat e com a licença. Ver §6 do relatório.
- **Baixar a prioridade de GPU do jogo.** Funciona (medido: película a 74 fps), mas exige abrir
  o processo do jogo com direito de escrita. Ganhamos o mesmo mexendo só no nosso.
- **Mexer no agendamento de GPU por hardware.** É configuração do sistema da pessoa e exige
  reiniciar o Windows. O módulo só lê, para o log.
