# Rede neural em resolução reduzida + composição residual, e a passagem segura

Data: 2026-09-11. Tudo aqui foi **medido nesta máquina** (RTX 4080, Windows 11), com o
código na fonte compilando e rodando numa cópia isolada — `DLSS5ForAll-AMD\bin-teste-residual\`.
Nada em `bin\` foi tocado: o usuário estava jogando Resident Evil Requiem no monitor
principal durante toda a sessão, e por isso **a GPU estava compartilhada** — ver §6 antes
de citar qualquer número absoluto.

O trabalho tem duas frentes, pedidas juntas porque se encontram no mesmo arquivo:

1. **Rede em resolução reduzida + composição residual** — a rede processa menos pixels e
   só o *delta* dela volta à imagem nativa (o item 1 do `RELATORIO-NEURAL-SCREEN.md`, e a
   mesma ideia do "Apply before Super Resolution" que o OptiScaler ganhou).
2. **Passagem segura + DRED + falha dura sem retry** — quando alguma coisa do NGX falha,
   a imagem continua andando, o log diz *onde* a GPU morreu, e o que não tem conserto não
   é tentado três vezes.

---

## 1. Onde a rede neural roda HOJE — com evidência

Esta era a primeira pergunta, e a resposta tem duas metades que precisam ser ditas juntas.

### 1.1 Em produção (`bin\`), quem roda a rede é o add-on de terceiros

`DLSS5ForAll-AMD\bin\dlss5-feed.cfg` traz `nr_spike=0` — ou seja, **o Neural Rendering
próprio do feeder está desligado**. E o `ReShade.log` da sessão do usuário mostra quem está
fazendo o trabalho:

```
04:20:08:420 [RenoDX DLSS] [RenoDX] DLSS-NR direct: CreateFeature(Reserved18) succeeded:
             handle=0x1a6f9bf1310 size=1920x1080 performance=6 preset=1
04:20:08:422 [RenoDX DLSS] [RenoDX] DLSS-NR direct: EvaluateFeature succeeded: evaluation=1
04:20:08:423 [RenoDX DLSS] RenoDX DLSS-NR source evaluation completed: source=1 ...
             size=1920x1080 replace_source=true return_output=false
```

É o `renodx-dlss.addon64` (ShortFuse), com `[RENODX-DLSS] DirectNeuralRenderingHookPoint=2`
(Upscaled) e `[RENODX-DLSS-preset1] DirectNeuralRenderingPassCount=1` no `ReShade.ini`.
Repare no `size=1920x1080`: **a rede roda na resolução de SAÍDA, o quadro inteiro** — que é
exatamente o custo que este trabalho ataca.

### 1.2 No feeder, o caminho próprio existe, está completo, e é o que controlamos

`amd\feeder-amd\src-feeder\src\dlss5-feed.cpp` já cria, avalia e encadeia passes do
`Reserved18` por conta própria (`NrEnsure` / `NrEvaluateFrame`, ARQUITETURA §10). Ele
alimenta `DLSSNR.Color`, `DLSSNR.Output`, `DLSSNR.MVec`, `DLSSNR.Depth` e roda **dentro do
bloco de comandos do quadro, na mesma command list do evaluate do SuperSampling**, no
device privado do feed. Basta `nr_spike=1` para ligá-lo.

**Conclusão de projeto:** não há razão para interpor shader nosso em volta do add-on de
terceiros nem para forkar o add-on MIT. O ponto de controle já é nosso, as texturas são
nossas, e a redução + o resíduo cabem inteiros em `NrEvaluateFrame`. Foi onde entrou.

**A consequência que precisa ficar escrita:** o modo reduzido só age com `nr_spike=1` E com
o `renodx-dlss.addon64` fora da pasta. Com os dois ligados haveria duas redes neurais sobre
o mesmo quadro — que é a briga de 07/09 de novo. Na cópia de teste o add-on de terceiros
foi renomeado para `.off`.

---

## 2. O desenho implementado

### 2.1 A conta

```
    nr_in  = reduz_bilinear(saída do SuperSampling)          ← a rede vê isto
    nr_out = rede(nr_in), nr_passes vezes, em cascata
    saída  = nativo + (amplia(nr_out) − amplia(nr_in)) × força
```

O quadro nativo é a **âncora**: ele nunca é reamostrado (entra por `Load`, índice exato),
então texto, bordas e HUD ficam 1:1. A rede fornece só o **delta** — o relighting, a
estrutura local, o que ela de fato acrescenta — e esse delta é a única coisa que sobe de
resolução. É por isso que a esticada não custa o detalhe fino: o que é esticado é a
*diferença*, que é suave por natureza, e não a imagem.

A aritmética é em **espaço de display** (os mesmos bytes UNORM que a rede consumiu), não em
linear. É deliberado e vale o parágrafo: o delta precisa ser somado no mesmo espaço em que
foi medido, senão a soma não cancela o que deveria cancelar. O NeuralScreen documenta a
mesma escolha.

### 2.2 Como isso vira comandos de GPU

Dois compute shaders novos (`cs_down` e `cs_resid`, HLSL `cs_5_0` compilado em tempo de
execução), uma root signature, dois PSO e um heap de descritores — tudo no device e na
command list do feed, sem recurso compartilhado e sem sincronização entre filas. Por quadro:

| etapa | o que acontece |
|---|---|
| 1 | `SLOT_OUTPUT` (saída do SR) vira leitura |
| 2 | `cs_down`: reduz para `g_nr_in` (uma amostra bilinear no centro de cada texel) |
| 3 | a cascata de `nr_passes` roda **inteira** na resolução reduzida, ping-pong entre duas texturas |
| 4 | `cs_resid`: nativo + (nr_out − nr_in) × força, em resolução cheia, para `g_nr_comp` |
| 5 | `CopyResource(SLOT_OUTPUT, g_nr_comp)` e o `SLOT_OUTPUT` volta ao estado que o caminho de casa espera |

Três decisões que não são óbvias e que custariam tempo a quem retomasse:

- **A composição escreve numa textura nossa e volta por cópia, em vez de escrever direto no
  `SLOT_OUTPUT`.** Escrever no `SLOT_OUTPUT` exigiria lê-lo *e* escrevê-lo no mesmo
  dispatch, e ler um UAV tipado depende do suporte opcional "typed UAV load" do formato.
  Escrever não depende. A cópia de tela cheia custa uma fração do que custaria descobrir
  isso na máquina de outra pessoa.
- **A tabela de SRV da redução aponta três vezes para o nativo.** Parece desperdício e não
  é: a tabela declara três descritores e, no momento da redução, o `g_nr_in` está em
  `UNORDERED_ACCESS` (é o destino). Um descritor de leitura apontando para ele ali é um
  recurso em estado incompatível dentro da tabela — a camada de depuração acusa e a
  validação por GPU pode recusar.
- **O estado D3D12 das texturas da cascata passou a ser rastreado.** O código anterior
  declarava, na segunda volta do ping-pong, `NON_PIXEL_SHADER_RESOURCE → UNORDERED_ACCESS`
  numa textura que estava em `COMMON`: um estado-antes mentido que o driver engoliu em
  silêncio e que a camada de depuração teria acusado. Agora toda transição parte do estado
  real (`NrTexTo` / `NrInTo` / `NrCompTo`). **É correção de um defeito latente que já
  existia**, não parte do recurso novo.
- **Os descritores são amarrados ao recurso com que foram feitos.** O caminho reduzido
  escreve os descritores UMA vez, na montagem, e eles apontam para o `SLOT_OUTPUT` do feed
  — que o feed recria em `ReleaseFrameResources()` sem avisar ninguém. Um rebuild de
  texturas que mantivesse o **mesmo tamanho de saída** (uma troca de `work_upscale`, por
  exemplo) deixaria os descritores apontando para memória liberada. `NrEnsure` agora
  compara também o ponteiro do recurso e o formato, e `NrEvaluateFrame` desliga o NR se
  encontrar um recurso diferente do que montou. É o tipo de defeito que só apareceria na
  máquina de outra pessoa.
- **A remontagem limpa o que existe, e não o que está "vivo".** A condição para desmontar
  antes de refazer não pode ser `g_nr_live`: um monte que se desligou sozinho (evaluate que
  falhou, textura trocada por baixo) deixa handle, mapa, texturas e o snippet de 165 MB de
  pé com `g_nr_live` já em `false` — e remontar por cima disso vazaria tudo.

### 2.3 A armadilha que a memória do projeto mandou vigiar — e o que foi feito

Há registro de 07/09/2026 de que **mudar a resolução de trabalho ao vivo deixou o NGX em
hold permanente**, com `evaluate 0.00 ms` e nenhuma linha de erro. O aviso vale: este
recurso também muda uma resolução ao vivo.

O desenho evita aquele caminho por construção. Quando `nr_reduzida` ou `nr_escala` mudam,
o `CfgReload` marca `g_nr_rebuild_pending` e **não** entra no conjunto `rebuild` que derruba
o feature de SuperSampling. A remontagem acontece no `FeedFrame11`, antes do bloco de
comandos do quadro, e toca **só** o Neural Rendering:

```cpp
if (ok && !needs_build11 && g_nr_rebuild_pending && g_nr_target_w != 0) {
    g_nr_rebuild_pending = false;
    NrShutdown();
    g_nr_failed = false;
    NrEnsure(g_nr_target_w, g_nr_target_h);
}
```

Nada de `g.frame_ready = false`, nada de `create_grace`, nenhum `holding the feature
(re)build` — que é a linha que precedia o hold de 07/09.

**Testado de propósito, com o app rodando e o cfg editado ao vivo** (`nr_reduzida` 0→1,
depois `nr_escala` 65→50), que é exatamente o gesto que travou o NGX naquele dia:

```
05:42:24  holding the feature (re)build (1/59 frames; ...)      ← do ARRANQUE
05:42:25  holding the feature (re)build (59/59 frames; ...)     ← do ARRANQUE
05:42:25  building: 1276x711 ... / feature ready: 1276x711 DLAA
05:42:25  nr: ready at 1276x711, 1 pass per frame
05:42:41  config (neural): ... nr_reduzida=1 nr_escala=65%
05:42:41  nr: rebuilding the neural path only (nr_reduzida=1 nr_escala=65%) -- the DLSS feature stays up
05:42:41  nr: reduced-resolution path ready -- network at 830x462 (65% de 1276x711)
05:42:44  nr: 600 frames: 0.42 ms/frame ... network at 830x462 (reduced + residual)
05:42:55  config (neural): ... nr_escala=50%
05:42:55  nr: rebuilding the neural path only (nr_reduzida=1 nr_escala=50%) -- the DLSS feature stays up
05:42:55  nr: reduced-resolution path ready -- network at 638x356 (50% de 1276x711)
05:43:04  nr: 600 frames: 0.45 ms/frame ... network at 638x356 (reduced + residual)
```

**Duas** linhas de `holding` na sessão inteira, as duas do arranque. Nenhum `building:` novo,
nenhuma recriação do feature de SuperSampling, e — o que importa — **o NR volta a avaliar
depois de cada troca**, com o tamanho novo no log e as janelas de 600 quadros continuando a
sair. A remontagem custa ~300 ms e acontece uma vez.

O que o hold de 07/09 deixou como sintoma foi `evaluate 0.00 ms` sem nenhuma linha de erro.
Aqui a prova de que não aconteceu é a linha de 600 quadros continuar saindo com tempo de
avaliação não-nulo, e é por isso que ela agora imprime em que resolução a rede está.

---

## 3. Antes e depois — alvo sintético

Bancada: `tests\alvo_animado.cpp` (ruído multi-oitava + grade fina + áreas chapadas) no
monitor secundário, app em `bin-teste-residual` no modo espelho, `DLSS5_DUMP` gravando a
saída real da cadeia no quadro 300. Fonte 1600×900, área útil capturada 1596×891, a rede
reduzida a 65% = **1038×580** (42% dos pixels). Script: `tests\medir-residual.ps1`.

Nitidez pela **variância do laplaciano** (`tests\nitidez_png.exe`, novo) — maior é mais
detalhe fino. A cena está **parada** de propósito: com ela andando, o arrasto temporal da
rede entra na conta e a medida deixa de ser sobre nitidez.

| configuração | rede roda em | laplaciano | gradiente | leitura |
|---|---|---|---|---|
| sem rede nenhuma (referência) | — | **1222,4** | 44,36 | o teto: é a imagem que o DLAA entregou |
| **reduzida 50% + residual** | 798×444 | **1208,1** | 42,41 | 98,8% da referência |
| **reduzida 65% + residual** | 1038×580 | **1171,7** | 41,67 | 95,9% da referência |
| rede em resolução cheia | 1596×891 | 1071,2 | 39,68 | 87,6% — **a rede cheia perde mais detalhe que a reduzida** |
| rede cheia, 4 passes | 1596×891 | 857,7 | 37,83 | 70,2% |
| reduzida 65%, 4 passes | 1038×580 | 708,6 | 36,61 | 58,0% |
| **reduzida 65%, modo INGÊNUO** | 1038×580 | **565,1** | 34,32 | 46,2% — a saída da rede simplesmente esticada |

Três leituras, e a terceira é a que muda uma decisão:

1. **O residual funciona, e o modo ingênuo é a prova.** A mesma rede, no mesmo tamanho, no
   mesmo quadro: **1171,7 com o resíduo contra 565,1 esticando a saída dela**. Mais que o
   dobro. É o análogo do "983 contra 88" que o NeuralScreen publica — os números não são
   comparáveis (alvo e métrica diferentes), a razão é. O modo ingênuo existe no código
   (`nr_residual=-1`) só para esta comparação poder ser refeita.
2. **A rede reduzida preserva MAIS detalhe que a rede em resolução cheia** (1171,7 contra
   1071,2). Não é erro de medição e tem explicação direta: a rede cheia reescreve todos os
   pixels, e o que ela escreve é mais suave que o original; o residual mantém o nativo e só
   soma o delta. Ou seja, neste alvo o modo reduzido é **mais barato e mais nítido**.
3. **Com cascata profunda a conta inverte.** Em 4 passes o reduzido (708,6) fica abaixo do
   cheio (857,7): a suavização se acumula a cada passe na imagem pequena e o resíduo carrega
   esse borrão para a resolução cheia. **O modo reduzido é para poucos passes**; de 4 para
   cima ele troca nitidez por fps, e isso tem de ser dito a quem for ligar o botão.

Diferença de imagem entre a rede cheia e a reduzida a 65%, no mesmo quadro
(`tests\comparar_png.exe`): **PSNR 34,0 dB, MAE 3,68/255, pior pixel 61/255**. É um efeito
espalhado e de baixa amplitude — a mesma assinatura que o NR já tinha contra o DLAA.

---

## 4. Antes e depois — custo de GPU (fps)

Alvo **animado e destravado** (8 px/quadro, sem vsync), lendo a linha `600 frames:` do
`dlss5-feed.log`. Com a fonte presa ao vblank a folga esconde o custo e tudo empata — por
isso destravada.

**A medição é A/B INTERCALADA (A,B,A,B,A,B) e auto-validada**, e as duas coisas foram
necessárias. Ver §6.1: a primeira tentativa de tabela, com as configurações medidas em
sequência, **não valia nada**.

| passes | fonte | rede cheia | reduzida 65% | ganho |
|---|---|---|---|---|
| 4 | 2556×1291 | 49,9 · 49,9 · 54,6 → **49,9** | 60,4 · 59,8 → **60,4** | **+21%** |
| 10 | 1596×891 | 27,2 · 26,0 → **26,6** | 33,3 · 31,8 → **32,6** | **+22%** |
| 4 | 2556×1291 (binário final, GPU mais carregada) | 41,2 · 42,2 → **42,2** | **56,7** | **+34%** |

(cada célula traz as amostras válidas e a mediana; rodadas em que o feed não leu a
configuração pedida, ou em que a cadeia de shaders não subiu, são descartadas pelo próprio
script em vez de entrarem na conta)

A terceira linha é a confirmação com o binário que ficou, meia hora depois, com a placa mais
apertada — e é justamente aí que a diferença cresce. Faz sentido: quanto mais a rede pesa no
orçamento do quadro, mais vale processar 42% dos pixels.

**Com 1 passe as três configurações empatam em ~100 fps** — a captura pelo WGC é o teto
nesta máquina, e a rede não é o gargalo. Esse empate não é um resultado negativo: ele diz
que o modo reduzido **não custa nada quando não é preciso**. O ganho aparece exatamente onde
deveria aparecer: quando a rede é a conta dominante.

O contador `ms/pass` do log é de **CPU** (o tempo de gravar as chamadas na command list), e
passou a dizer isso no próprio log. A 4 passes ele mede 0,98 ms (cheia) contra 1,06 ms
(reduzida) — praticamente igual, porque o número de chamadas é o mesmo. **O que muda é a
GPU, e a GPU se mede pelo fps.** Foi esse exato engano que produziu a tabela errada de
"11× mais barato" na ARQUITETURA §10, e não se repete aqui.

---

## 5. Antes e depois — segundo alvo, de textura diferente

> **CORREÇÃO (mesma sessão).** Esta seção dizia "cena de jogo real". **Está errado, e o erro
> é meu:** `cena-do-teste-perfil.png` é **outro alvo sintético** — texto denso, linhas finas
> coloridas, retângulos e uma escada de cinza sobre um fundo de ruído. Não é captura de jogo
> nenhuma. Fui conferir a imagem só quando precisei dela para outra pergunta, e ela é o que
> é. **Nesta árvore não há captura de jogo real disponível**, e nenhuma conclusão deste
> relatório pode ser lida como "medido em jogo".
>
> O que a seção sustenta continua valendo, e é isto: um **segundo** alvo, com conteúdo de
> natureza diferente do primeiro (texto e bordas duras, contra ruído multi-oitava), repete o
> mesmo resultado. Dois alvos sintéticos concordando é mais do que um; não é uma cena real.

Mesma bancada, com `docs\pesquisa-2026-09-11\cena-do-teste-perfil.png` mostrado parado por
`alvo_animado --img`. Área útil capturada 1276×711; a rede a 65% roda em 830×462.

| configuração | laplaciano | gradiente | contra a referência |
|---|---|---|---|
| sem rede nenhuma (referência) | **431,9** | 39,51 | 100% |
| reduzida 77% + residual | 396,1 | 34,64 | 91,7% |
| reduzida 50% + residual | 395,6 | 34,32 | 91,6% |
| **reduzida 65% + residual** | **373,7** | 36,50 | **86,5%** |
| rede em resolução cheia | 341,3 | 36,66 | 79,0% |
| **reduzida 65%, modo INGÊNUO** | **74,2** | 28,12 | **17,2%** |

O segundo alvo repete o padrão do primeiro e amplia a diferença que importa: o residual a
65% entrega **373,7 contra 74,2 do modo ingênuo — cinco vezes mais detalhe fino com a mesma
rede, no mesmo tamanho, no mesmo quadro.** E continua acima da rede em resolução cheia
(373,7 contra 341,3).

---

## 6. O que estes números NÃO sustentam

### 6.1 A primeira tabela de fps estava errada, e vale contar por quê

A medição foi refeita do zero porque a primeira rodada mediu duas coisas que não eram o
código novo. Ficam escritas porque qualquer pessoa que use esta bancada vai tropeçar nelas:

- **O app REABRE A SI MESMO** quando a janela capturada some (`ShellExecute` +
  `--reabrindo`), com um PID novo que o script de medição não conhecia. Fechar "o meu PID"
  não fechava nada: ao fim de três rodadas havia **três instâncias** disputando a placa, e a
  mesma configuração media **100 fps numa rodada e 25 na seguinte**. A varredura agora é por
  caminho da pasta de teste, não por PID guardado.
- **Escrever o `.cfg` não garante que o feed leu o que se escreveu.** Rodadas inteiras
  rodaram com `nr_spike=0` enquanto o arquivo no disco dizia `nr_spike=1`. A correção não
  foi descobrir a causa da corrida: foi **conferir, no log, a linha `config (neural)` que o
  feed imprimiu, e descartar a rodada quando ela não bate com a pedida**. Uma medição que
  não consegue provar em que configuração rodou não deve ser usada para decidir nada.

Os números da §4 são só de rodadas que passaram nas duas verificações.

### 6.2 As ressalvas que continuam de pé

- **A GPU estava compartilhada.** O usuário jogava Resident Evil Requiem no monitor
  principal e um segundo agente rodava o app em `bin-teste-requiem` durante parte da
  sessão. Os valores absolutos de fps não valem para comparar com nada fora daqui. O que
  vale é a comparação **A/B intercalada**, que cancela a deriva lenta da carga alheia: as
  duas configurações são medidas alternadamente, minutos uma da outra, e o que se compara
  são as medianas. Para remedir com a máquina limpa:
  `powershell -ExecutionPolicy Bypass -File tests\medir-residual.ps1`, que refaz as duas
  fases inteiras sozinho.
- **O alvo é sintético e a cena está parada.** Nitidez de cena parada não diz nada sobre
  arrasto, fantasma ou cintilação em movimento — e é justamente em movimento que a rede tem
  componente temporal. A §5 acrescenta um segundo alvo, mas ele também é sintético e também está parado — **nenhuma medida deste relatório foi feita em jogo**.
- **Ninguém jogou com isto ligado.** O que falta para fechar o assunto é uma sessão de jogo
  de verdade com `nr_reduzida=1`, olhando movimento de câmera com paralaxe. É o mesmo
  limite que o modo `sintetizar` tem hoje.

---

## 7. Item 2 — passagem segura, DRED, falha dura

### 7.1 Passagem segura: o que já existia e o que faltava

O feed já tinha a metade boa: `FeedFrame` volta cedo enquanto `g.disabled` estiver de pé, a
técnica do ReShade não substitui o backbuffer, e o que aparece é o quadro capturado, sem
DLSS e sem neural. **A cadeia nunca ficou congelada.** O que faltava era não ser silencioso,
e é o que entrou:

- `FeedDisable` agora escreve, sempre, `safe passthrough: the chain keeps running WITHOUT
  DLSS/neural -- the picture is the untouched capture`;
- a página do overlay do feed ganhou a linha *"Safe passthrough: the picture is the untouched
  capture -- nothing is frozen"* embaixo do motivo da parada;
- e uma linha de estado do Neural Rendering próprio: em que resolução ele está, se está
  reduzido, quantos passes — ou, se caiu, **por quê**. Sem isso "a rede parou" é
  indistinguível de "a rede nunca subiu", que foi como um `evaluate 0,00 ms` passou seis
  minutos sem ser notado em 07/09.

O `g.dev12_owned` do fix de hoje mais cedo (não reinicializar NGX em sessão alheia) **não
foi tocado** — o novo caminho de falha dura entra *antes* dele e só desvia casos que aquele
fix já não tentava salvar.

### 7.2 Falha dura sem retry

`NgxFalhaDura()` classifica o código de retorno. São duros `0xBAD00001`
(FeatureNotSupported), `0xBAD00008` (UnsupportedInputFormat), `0xBAD0000C` (OutOfDate),
`0xBAD0000E` (UnsupportedFormat) e `0xBAD00012` (NotImplemented) — os casos em que a
resposta é sobre **o que a placa/driver sabem fazer**, e portanto não muda tentando de novo.
Tudo o mais continua transitório e continua ganhando as tentativas que já tinha.

| onde | antes | agora |
|---|---|---|
| `CreateFeature` do SuperSampling | 3 tentativas, com `ReinitNgx` na segunda, e "repeated failures" no fim | código duro ⇒ para na primeira, diz o nome do código, não chama `ReinitNgx` |
| `CreateFeature(Reserved18)` do NR | uma falha desligava o NR para sempre, em silêncio | duro ⇒ desliga com motivo no log, no `Warn` e no overlay; transitório ⇒ **3 tentativas** ao longo dos rebuilds seguintes |
| `EvaluateFeature` do NR | desligava o NR, log só no arquivo | idem, e o motivo fica visível no overlay |
| botão "Re-enable" do overlay | limpava só o estado do feed | limpa também a falha dura do NR: um clique é um pedido deliberado |

### 7.3 DRED

`dred=1` (padrão) liga `SetAutoBreadcrumbsEnablement` e `SetPageFaultEnablement`
**antes** de `D3D12CreateDevice`, e só no caminho do device **privado** — ligar por processo
num caminho em que o device é do jogo seria decidir pelo jogo, e breadcrumbs custam alguma
coisa. Confirmado no log da cópia de teste:

```
[feed] DRED on: GPU breadcrumbs and page-fault capture armed for the private D3D12 device
```

`DredRelatorio()` é chamado nos três pontos em que o feed já descobre que a GPU morreu: a
espera de fence em `BeginCommands`, a criação do feature, e o `FeedFail`. Ele imprime, por
command list, quantas operações completaram e **qual era a próxima** (é a suspeita), mais o
endereço virtual da falha de página e as alocações vivas e recém-liberadas em volta.

**O que não foi exercitado, e por quê:** a *leitura* do DRED exige um device removido de
verdade. Provocar um TDR com o usuário jogando é inaceitável, e não foi feito. O que está
verificado é que o DRED **liga** (linha acima) e que a leitura está exatamente nos pontos em
que a linha `the graphics device is gone` já é impressa hoje. O primeiro TDR real vai
escrever as linhas — ou revelar que não escreve, e aí sabe-se disso pelo silêncio delas.

### 7.4 As falhas provocadas de propósito

Quatro casos, provocados na cópia isolada com o alvo rodando, um atrás do outro, e o quarto
existe para provar que nenhum dos três anteriores deixou estrago
(`scratchpad\falhas.ps1`, logs guardados):

| # | o que foi feito | o que aconteceu |
|---|---|---|
| 1 | **`nvngx_dlssnr.dll` retirado da pasta** | `nr_spike: nvngx_dlssnr.dll not found next to the game executable or next to the add-on`. O NR não sobe, **o DLSS continua rodando**, o quadro continua chegando. Nenhuma tentativa repetida. |
| 2 | **`flags=1431655765`** (flags de criação do DLSS absurdos) | **o create NÃO falhou** — o driver ignorou os bits que não conhece, e o NR subiu normal a 830×462. O teste não provocou o que se queria; fica registrado para ninguém refazer. |
| 3 | **`nvngx_dlss.dll` retirado da pasta** | `safe passthrough: the chain keeps running WITHOUT DLSS/neural -- the picture is the untouched capture` seguido de `stopped: the D3D12/NGX session failed to start. The game renders normally.` **É o caso que interessa**: a sessão NGX inteira não abre, e a imagem continua na tela. |
| 4 | **tudo de volta ao lugar** | `nr: reduced-resolution path ready -- network at 830x462`, 600 quadros, 0,42 ms/quadro. Nada ficou quebrado. |

**O que NÃO foi exercitado, e é honesto dizer:** o ramo de **falha dura por código NGX**
(`0xBAD00001` e companhia). Não há como produzir esse retorno numa RTX 4080 com driver
atual — é justamente o código de uma placa que não tem o recurso. O que está verificado é a
outra metade do mesmo caminho: os dois casos em que o NGX não sobe (1 e 3) passam pela
passagem segura, com a imagem andando e o motivo no log. A classificação em si
(`NgxFalhaDura`) é uma tabela de cinco códigos, revisada contra `NgxResultName`.

**Nem o DRED foi lido de verdade**, pela mesma razão invertida: ler o DRED exige um device
removido, e provocar um TDR com o usuário jogando é inaceitável. Ele **liga** (linha no log,
§7.3) e a leitura está nos três pontos em que o feed já descobre que a GPU morreu.

---

## 8. Chaves de configuração novas (`dlss5-feed.cfg`)

| chave | valores | padrão | o que faz |
|---|---|---|---|
| `nr_reduzida` | 0 / 1 | **0** | 1 = a rede roda reduzida e a composição é residual |
| `nr_escala` | 50..90, ou fração 0.50..0.90 | 65 | porcento da resolução de SAÍDA em que a rede roda |
| `nr_residual` | 0..200, e **−1** | 100 | força do delta em porcento; −1 é o modo ingênuo de diagnóstico |
| `dred` | 0 / 1 | **1** | breadcrumbs de GPU e captura de falha de página no device privado |

`nr_escala` aceita fração **e** porcento porque a literatura do assunto fala em 0,5–0,77 e
digitar `0.65` num arquivo cujas outras chaves são inteiras não pode virar "0%", em
silêncio. `nr_residual` **não** aceita fração, e por um motivo: `1` ali seria ambíguo entre
1% e 100%.

Todas as quatro entram no `CfgSave` (a reescrita a partir do overlay não as apaga) e saem
numa linha própria do log a cada recarga:

```
[feed] config (neural): nr_spike=1 nr_passes=1 nr_reduzida=1 nr_escala=65% nr_residual=100% dred=1
```

---

## 9. O que precisa entrar no painel (para o `painel-requiem`)

Mandado por mensagem, e repetido aqui para não se perder:

- **Um interruptor só**, rótulo **"Rede leve"**, frase de ajuda *"a rede neural trabalha
  numa imagem menor e só o retoque dela volta em tamanho cheio — menos placa, mesmo
  detalhe"*. Ligado grava `nr_reduzida=1`; desligado, `nr_reduzida=0`. A escala fica fixa em
  65%, que é o ponto medido; `nr_escala` e `nr_residual` **não** vão para o painel.
- **Só faz sentido depois da troca para o NR próprio.** Hoje `bin\` roda com `nr_spike=0` e
  quem faz o neural é o `renodx-dlss.addon64`; enquanto for assim, o interruptor não teria
  efeito nenhum — e interruptor visível que não faz nada é pior que interruptor nenhum.
- Se e quando o painel oferecer passes acima de 3 junto com a "Rede leve", vale a frase de
  aviso da §3: em cascata profunda o modo reduzido troca nitidez por fps.

---

## 10. O que precisa ser recompilado para a `bin\` (eu NÃO levo)

| arquivo | de onde | por quê |
|---|---|---|
| `bin\dlss5-feed.addon64` | `amd\feeder-amd\build\dlss5-feed-amd.addon64` (via `bash amd/feeder-amd/build-feed-amd.sh`) | é onde está tudo deste relatório |

Nada mais mudou: o `main.cpp`, o add-on da ponte e os shaders `.fx` estão como estavam.
Para o recurso realmente agir em produção ainda são necessários, e são decisão de quem
integra, não minha:

1. `nr_spike=1` no `bin\dlss5-feed.cfg`;
2. o `renodx-dlss.addon64` fora da pasta (senão são duas redes sobre o mesmo quadro);
3. `nr_reduzida=1`, se e quando o botão do painel existir.

---

## 11. Arquivos tocados

| arquivo | o que mudou |
|---|---|
| `amd\feeder-amd\src-feeder\src\dlss5-feed.cpp` | tudo acima (backup em `.bak-antes-residual`) |
| `amd\tests\nitidez_png.cpp` (novo) | mede laplaciano e gradiente de um PNG |
| `amd\tests\medir-residual.ps1` (novo) | a bateria inteira, as duas fases, sozinha |
| `DLSS5ForAll-AMD\bin-teste-residual\` (novo) | a cópia isolada, com o add-on novo e o de terceiros desligado |
