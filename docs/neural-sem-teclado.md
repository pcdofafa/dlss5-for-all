# Alternar o Neural Rendering sem teclado e sem foco

Investigação pedida depois do §14.11 do `painel.md`, que deixou em aberto se o F6 do app
alguma vez alternou o Neural Rendering do RenoDX. **Alternou nunca, e agora sabemos por quê —
e existe um caminho que funciona, provado no `ReShade.log`.**

Tudo aqui foi medido nesta máquina, em ambiente isolado no scratchpad (cópias do `dxgi.dll` e
do `renodx-dlss5.addon64`, arnês D3D11 próprio). Nada do projeto foi alterado.

---

## 1. A causa real não é o foreground: é o UIPI

O §14.11 atribuiu a falha ao `SetForegroundWindow` recusado. Está certo no sintoma e incompleto
na causa. Medi os dois lados separadamente:

```
foreground hwnd=0x40b3a pid=13216
  classe='GameInputServiceWindow'
  exe=?                            <- OpenProcess(QUERY_LIMITED) negado
nossa integridade = 0x2000 (media)
```

O `exe=?` é o dado novo: nem a consulta mais fraca de processo é permitida, então a
`GameInputServiceWindow` roda com integridade **acima** da nossa. E aí vale a regra do UIPI:
um processo de integridade média não injeta entrada quando o foreground pertence a um processo
de integridade mais alta.

O teste direto, com dois processos meus e sem tocar em foreground nenhum — um injeta `VK_F24`
com `SendInput` e segura 120 ms, o outro lê `GetAsyncKeyState(VK_F24)` a cada 8 ms:

```
injector: down=1 up=1   (1 = aceito pelo Windows)
watcher:  fim, deteccoes=0
```

`SendInput` **devolveu sucesso e não fez nada**. Isso é documentado: quando o UIPI bloqueia,
nem o retorno nem o `GetLastError` indicam a falha. É exatamente o que o `PokeReShade` não tem
como detectar.

Consequência prática, e é o ponto que muda o plano: **mesmo que o `SetForegroundWindow` fosse
aceito, o `SendInput` continuaria sendo descartado enquanto essa janela detiver o foreground.**
O teste humano do §14.11 continua valendo para o caso do F6 físico (aí não há `SendInput`
nenhum — o teclado real é lido direto), mas não salva o caminho sintético.

## 2. Como o RenoDX lê a hotkey

Evidência estática, do `renodx-dlss5.addon64`:

- DLLs importadas: `KERNEL32`, `USER32`, `bcrypt`.
- Única função de teclado importada: **`GetAsyncKeyState`**. Não há `RegisterHotKey`, nem
  `SetWindowsHookEx`, nem `PeekMessage`/`GetMessage`.
- Também não usa `is_key_pressed` do ReShade — o que a prova funcional do §3 confirma, porque
  interceptar só o `GetAsyncKeyState` basta para controlar 100% do que ele enxerga.
- Strings relevantes: `NRToggleKey`, `NRScreenshotKey`, `hotkey rebound: `, `NR toggled `.

Medido em execução: ele chama `GetAsyncKeyState` **2× por frame** (a tecla de toggle e a de
screenshot) e faz **detecção de borda própria** — um pulso de 6 leituras gera um único
`NR toggled`, não seis.

### Candidato 1 — `PostMessage(WM_KEYDOWN)`: descartado

`GetAsyncKeyState` lê o estado assíncrono do teclado, que mensagens postadas na fila de uma
janela não alteram. Não há caminho: o `VIA_MENSAGEM` funciona para o overlay do ReShade (que lê
a fila) e não pode funcionar para o RenoDX.

### Candidato 2 — mexer no ini / `NeuralUplift`: descartado, com experimento

O add-on tem acesso à API oficial de config do ReShade. Escrevi e reli com sucesso:

```
[nrhook] set_config_value NeuralUplift=1, releitura ok=1 valor='1'
```

Para saber se o RenoDX reagiu, comparei duas execuções com o ini começando em `NeuralUplift=0`:

| execução | `set_config_value(1)` antes | resultado do toggle seguinte |
|---|---|---|
| controle A | não | `NR toggled ON` |
| teste B | sim, gravado e relido | `NR toggled ON` |

Se ele tivesse lido o `1`, o toggle teria dito `OFF`. Disse `ON` nos dois: **o RenoDX lê
`NeuralUplift` só no boot.** (Uma medição anterior parecia positiva; era o ini persistido da
execução anterior — daí o controle A.)

### Candidato 3 — `WH_KEYBOARD_LL`: desnecessário e não resolveria

Um hook low-level *observa* a entrada, não alimenta o estado assíncrono que o
`GetAsyncKeyState` lê. Para alimentar seria preciso `SendInput` de novo — de volta ao UIPI.
Além disso é um hook global do sistema, custo e risco maiores que a alternativa abaixo.

## 3. O que funciona: interceptar a IAT do RenoDX

O RenoDX vive **no mesmo processo** que nós. Em vez de mexer no teclado do sistema, o add-on da
ponte reescreve a entrada da tabela de imports **dele** para `USER32!GetAsyncKeyState` e serve a
tecla apenas para ele. Nada é injetado no sistema, nada vaza para o jogo, nenhum outro add-on é
afetado.

A substituta devolve o que o Windows devolveria para uma tecla real sendo segurada e solta
(bit `0x8000` durante o pulso, mais `0x0001` na primeira leitura), então funciona tanto se o
RenoDX testar nível com borda própria quanto se testar o bit "pressionada desde a última
chamada".

**Prova, com a ponte real (não a prova de conceito), sem foco e sem teclado:**

```
[DLSS 5 Bridge] alternancia do Neural Rendering pronta (tecla do RenoDX = 0x75, sem teclado e sem foco).
[DLSS 5 Neural Rendering] DLSS5 Generic: NR toggled ON via F6
[DLSS 5 Neural Rendering] DLSS5 Generic: NR toggled OFF via F6
```

e do lado do app:

```
[inicial]        nr_hook_ready=1 nr_toggle_vk=0x75 nr_pulses=0
-> ToggleNeuralRendering() = 1
[depois do 1o]   nr_hook_ready=1 nr_toggle_vk=0x75 nr_pulses=1
-> ToggleNeuralRendering() = 1
[depois do 2o]   nr_hook_ready=1 nr_toggle_vk=0x75 nr_pulses=2
```

Dois pedidos, dois pulsos, dois toggles. É a primeira vez que `NR toggled` aparece num teste
automatizado nesta máquina.

### Rebind

`NRToggleKey`, em `[RenoDX.DLSS5]` do `ReShade.ini`, é o **código decimal do virtual-key**.
Verificado: com `NRToggleKey=118`, pulsar F6 (117) não produz nada e pulsar F7 (118) produz
`NR toggled ON via F7`. O add-on lê essa chave e cai em F6 quando ela não existe.

### Degradação

Sem o RenoDX carregado:

```
[DLSS 5 Bridge] RenoDX nao carregado: alternancia do Neural Rendering indisponivel pela ponte.
[inicial] nr_hook_ready=0 ...
-> ToggleNeuralRendering() = 0
```

O resto da ponte (LUT, grading, nitidez) continua normal.

### Riscos, honestamente

- **É um hook em código de terceiro.** Se uma versão nova do RenoDX deixar de importar
  `GetAsyncKeyState` pelo nome — ou passar a resolvê-la por `GetProcAddress` em runtime —, o
  hook não instala, `nr_hook_ready` fica 0 e o painel cai no caminho antigo. Falha detectável e
  silenciosa no bom sentido: não trava, não corrompe, só fica indisponível.
- **Instalação tardia.** O ReShade carrega os add-ons numa varredura só, e a ordem alfabética
  não garante que o RenoDX já esteja no lugar durante o nosso `DllMain`. O hook é instalado no
  primeiro `reshade_present`, quando todos já estão carregados, e a tentativa é repetida uma vez
  por segundo por até 6 tentativas — uma tentativa única que caísse num instante ruim desligaria
  a alternância pelo resto da sessão. Só a última tentativa escreve o motivo no log, para não
  repetir a mesma linha seis vezes para quem não usa o RenoDX (verificado: 1 linha, não 6).
- **`NeuralToggleAvailable()` não cai sozinho.** Depois do primeiro `true` ele fica `true`:
  `on_destroy_runtime` (que é por onde passa uma troca DX11→DX12) não mexe no estado do hook, e
  nem o add-on nem o RenoDX são recarregados nesse caminho. Pode passar de `false` para `true`
  nos primeiros segundos, nunca o contrário — o único caminho de volta a `false` é o
  `FreeLibrary` do próprio add-on, que na prática é o fim do processo.
- **Descarregamento.** O módulo do RenoDX é fixado com `GET_MODULE_HANDLE_EX_FLAG_PIN`, e o
  hook só é desfeito num `FreeLibrary` de verdade (`lpReserved == nullptr`); no encerramento do
  processo não se mexe na IAT dele, que pode já ter saído.
- **Concorrência.** Um ponteiro alinhado de 8 bytes: a troca é atômica no x64, então o RenoDX
  lendo em paralelo vê o valor velho ou o novo, nunca lixo.
- **Continua sendo "alternar", não "ligar".** O RenoDX não expõe o estado atual pela API. Quem
  precisa de estado absoluto segue conferindo pelo `NeuralPeloLog()`, como o `AlternarTudo` já
  faz.

## 4. Implementado — protocolo 2 da ponte

Está no `addon/dlss5_bridge_addon.cpp` e no `reshade_bridge.h`.

### API

```cpp
bool rsbridge::NeuralToggleAvailable();  // da para alternar agora?
bool rsbridge::ToggleNeural();           // arma o toque; false = caia no caminho antigo
uint32_t rsbridge::NeuralDelivered();    // quantos toques o RenoDX ja LEU
int  rsbridge::NeuralState();            // SEMPRE NEURAL_DESCONHECIDO -- ver abaixo
uint32_t rsbridge::AddonProtocol();      // 1, 2, ou 0 se nao ha ponte
```

Mais quatro campos no fim do `Status`: `nr_hook_ready`, `nr_toggle_vk`, `nr_pulses` (toques
armados) e `nr_delivered` (toques que o RenoDX **efetivamente leu**).

### Latência, medida

A entrega sai no **primeiro Present seguinte**, sempre: 9,9 / 19,9 / 19,9 ms em três disparos —
que é apenas o vsync a 60 Hz. O `ToggleNeural()` em si não enfileira nada: ele arma dois
atômicos na hora; o que espera um quadro é o RenoDX lendo.

O `NeuralUplift` do `ReShade.ini` também acompanha, e corretamente (0→1→0→1), mas só depois de
**1,3 a 2,0 s**. Serve como fonte de "pedido" se for cômodo, nunca como confirmação rápida.

### Por que não há uma versão bloqueante "confirmada"

Eu escrevi uma e ela não funciona — vale registrar para ninguém tentar de novo. Quem lê o toque
é o RenoDX, **dentro do Present**; esperar pela confirmação na thread que apresenta impede o
próprio Present que faria a entrega acontecer. Medido no mesmo binário:

| onde a espera acontece | resultado |
|---|---|
| na thread de render | `false` após 254 ms de timeout — e o toggle acontece logo depois |
| fora dela, com o loop apresentando | `true` em 28 ms |

Como o `AlternarTudo` roda na thread do loop de mensagens, que é a que apresenta, uma API
bloqueante ali devolveria `false` justamente quando funcionou. O padrão correto é assíncrono:

```cpp
uint32_t alvo = rsbridge::NeuralDelivered();
if (!rsbridge::ToggleNeural()) { /* fallback */ }
// ... siga apresentando; dois ou tres quadros depois:
bool entregue = rsbridge::NeuralDelivered() > alvo;
```

Com 1 Present de latência, dois quadros já são folga. A verificação de 2,5 s pelo `ReShade.log`
cobre a mesma coisa e continua válida.

### `NeuralState()` devolve sempre desconhecido, e isso é definitivo

O RenoDX não publica o estado do NR em lugar nenhum que a ponte alcance: não há uniform, nem
técnica, nem export (ele exporta só `NAME` e `DESCRIPTION`), nem evento — ele nem usa a API 20,
registra-se com a **18**. O checkbox vive dentro do ImGui dele, e o `NeuralUplift` só chega ao
`ReShade.ini` quando o AutoSave do ReShade resolve gravar. A fonte imediata continua sendo o
`NeuralPeloLog()`. A função existe para ninguém perder tempo procurando de novo.

### Compatibilidade: por que há dois números de versão

O exe já instalado tem `if (ver() != 1) recusa` **gravado no binário** — e ele conhece um
`Status` de 44 bytes. Se `DLSS5Bridge_Version()` passasse a devolver 2, esse exe descartaria a
ponte inteira e perderia até a LUT e a nitidez, que nada têm a ver com o neural. Então:

- `DLSS5Bridge_Version()` está **congelado em 1, para sempre** — significa "a versão mais velha
  que eu ainda falo".
- `DLSS5Bridge_Protocol()` é o export novo com a versão real (2). Ele **não existe** nos add-ons
  da v1, e é essa ausência que o cliente usa para reconhecer um add-on antigo.
- `DLSS5Bridge_Query` aceita qualquer `struct_size` entre 44 e o atual e preenche só o que
  couber — o padrão `cbSize` do Win32. Campos novos entram sempre no fim (verificado:
  `offsetof(nr_hook_ready) == 44`).
- Do lado do cliente, `resolve()` deixou de exigir igualdade: aceita qualquer add-on cuja versão
  mínima seja `<=` a nossa, e pede à `Query` só o tamanho que aquele add-on sabe preencher.

Isso faz as quatro combinações andarem, e cada uma foi medida.

### Testes

| cenário | resultado |
|---|---|
| cadeia completa (feed + RenoDX + ponte), 2 toggles | 2 × `NR toggled` no log; `pulsos=2 entregues=2`; LUT 6/6, nitidez 4/4 |
| sem o RenoDX | `nr_hook_ready=0`, `ToggleNeural()=0` limpo; LUT 6/6, nitidez 4/4 |
| cliente protocolo 1 (binário antigo, `ver()!=1`, Status 44) + add-on novo | LUT 6/6, nitidez 4/4, `applied=15 failed=1` |
| o mesmo cliente + add-on **instalado**, como referência | `applied=15 failed=1` — idêntico, zero regressão |
| `DLSS5ForAll.exe` instalado (só exporta `Version`/`Submit`/`Query`) + add-on novo, 22 s | os 3 add-ons carregam, hook instala, nenhum erro |

O `failed=1` é pré-existente e aparece igual nos dois add-ons: é o reapply de uma técnica antes
de o efeito dela ter terminado de compilar; o reapply seguinte conserta.

**Instalação:** o arquivo tem de se chamar `dlss5-bridge.addon64`. O cliente resolve o módulo
por esse nome — com qualquer outro (`dlss5-bridge-r5.addon64`, por exemplo) o add-on carrega
normalmente no ReShade e mesmo assim a ponte fica invisível para o app. Custou uma rodada de
teste descobrir isso.

## 5. Sugestão para o `main.cpp` (outro dono)

Trocar as duas chamadas de `PokeReShade(VK_F6, false, VIA_TECLADO)` do `AlternarTudo` por:

```cpp
// A ponte alterna o NR dentro do processo, sem foco e sem teclado; o poke fica de
// reserva para quando ela nao estiver disponivel (ver docs/neural-sem-teclado.md).
static bool AlternarNeural() {
    if (rsbridge::ToggleNeural()) return true;
    return PokeReShade(VK_F6, false, VIA_TECLADO);
}
```

A verificação de 2,5 s pelo `ReShade.log` continua igual, e o guard de tudo-ou-nada do §14.10
continua valendo sem mudança: `AlternarNeural()` devolve false só quando nenhum dos dois
caminhos funcionou. O cartaz de erro atual só aparece nesse caso.
