# Nitidez pós-DLSS — `DLSS5_Sharpen.fx`

Filtro de nitidez nosso, adaptativo ao contraste, feito para rodar **depois** do
Neural Rendering e ser pilotado pelo painel do app através da ponte
(`reshade_bridge.h`). Licença MIT, como o resto do projeto.

Este documento registra o que foi construído, a ordem escolhida na cadeia, os
parâmetros, o que protege contra halo e contra realce de ruído, e as medições
feitas com o ReShade rodando de verdade.

---

## 1. Onde ele entra na cadeia

```
LUMENITE: Kernel 2.0     motion vectors (optical flow)
DLSS 5 Feed              o add-on dispara DLSS + Neural Rendering AQUI
DLSS 5 Sharpen           <- este filtro
DLSS 5 Grade             cor, por último
```

**Abaixo do `DLSS5_Feed`, sempre.** É a regra do `docs\cor-hdr-e-filtros.md` §4 e
vale integralmente aqui: acima do Kernel a alta frequência injetada vira o sinal
que o optical flow tenta casar (motion vector estragado); entre Kernel e Feed a
validação por luma do Feed passa a comparar um frame que o Kernel não viu; e em
qualquer posição acima do Feed o DLSS trata o realce — e o grão que o realce
amplifica — como detalhe real, acumulando-o no histórico temporal.

### Por que **acima** do `DLSS5_Grade`, e não abaixo

As duas ordens são defensáveis; escolhi `Sharpen → Grade` por três motivos, em
ordem de peso:

1. **Banding.** O estágio de LUT do `DLSS5_Grade` é uma tabela 64³ com
   interpolação, e a S-curve é uma curva de transferência aplicada a um sinal de
   8 bits. As duas coisas criam degraus finos em gradiente liso. Um filtro de
   nitidez é, por definição, um amplificador de alta frequência: rodando *depois*
   do grading ele transforma esses degraus em contorno visível. Rodando antes,
   não os vê.
2. **Ruído.** Contraste e S-curve multiplicam a amplitude do grão da captura. O
   limiar `Denoise` é calibrado em unidades de luma; se ele agir sobre a saída
   neural — sinal display-referred sRGB, previsível — o mesmo valor vale para
   qualquer grading que o usuário escolha depois. Na ordem inversa, o limiar
   teria de ser reajustado toda vez que alguém mexesse no contraste.
3. **O grading não desfaz a nitidez.** Sendo uma transformação suave e
   praticamente monotônica ponto-a-ponto, ele remapeia o contraste do realce, mas
   não o apaga. Já a ordem inversa deixaria o realce à mercê de quanto contraste
   a LUT criou.

**O custo honesto dessa escolha:** a nitidez *percebida* muda um pouco quando o
usuário mexe no contraste do Grade, porque o grading vem depois. Quem quiser
ajustar nitidez olhando exatamente a imagem final deve fazê-lo com o grading já
configurado. Na prática, com a grade neutra do `preset-02` isso é irrelevante.

---

## 2. O algoritmo

Um passe, 9 amostras (3×3), tudo calculado em luma BT.709 e devolvido à cor sem
deslocar matiz.

**Princípio.** É o mesmo do FidelityFX CAS da AMD: amplificar o reforço onde o
contraste local deixa espaço e recuar onde não deixa. O código é nosso, escrito
do zero a partir desse princípio — **nenhuma linha, constante ou tabela do CAS
foi copiada**, e por isso não há aviso de copyright de terceiro a manter. (O CAS
é MIT; se um dia for portado de fato, o aviso tem de vir junto.) O que muda em
relação ao CAS: o passa-baixa é um gaussiano 3×3 explícito em vez do kernel em
cruz com peso implícito, o clamp de halo é exposto como controle, e existe uma
porta de ruído — que o CAS não tem, porque foi feito para saída de renderizador
limpo, não para captura comprimida de console.

Etapas, na ordem em que o shader as executa:

| # | Etapa | O que faz |
|---|---|---|
| 1 | `detail = ye - lowpass` | alta frequência: luma central menos gaussiano 3×3 `(1 2 1 / 2 4 2 / 1 2 1)/16` |
| 2 | `mn`, `mx`, `contrast` | mínimo/máximo de luma do bloco; as diagonais entram com **meio peso** (estão a √2 do centro), para uma quina isolada não abrir o intervalo inteiro |
| 3 | `amp = sqrt(min(mn, 1-mx) / mx)` | **adaptação ao contraste**: bloco de meio-tom → 1 (detalhe fino de baixa amplitude, é onde o realce rende); bloco que já vai do preto ao branco → 0 (a borda já está definida e não há para onde crescer sem clipar) |
| 4 | `gate = smoothstep(Denoise, 2·Denoise + 1/255, contrast)` | **porta de ruído**: contraste local abaixo do limiar não é detalhe, é grão |
| 5 | `clamp(ye + delta, mn - slack, mx + slack)` | **limite de halo**, com `slack = (1 - HaloProtect) · contrast` |
| 6 | volta para a cor | multiplicativo (`c · ynew/ye`, cromaticidade intacta), com transição para aditivo abaixo de ~0.05 de luma, onde dividir pela luma seria instável |

Com `Sharpness = 0` o shader retorna a cor lida e custa uma comparação.

### As duas proteções, e qual age onde

- Em borda de **contraste extremo** (preto contra branco) quem protege é a
  **adaptação** (etapa 3): `amp` vai a zero e o filtro simplesmente não toca ali.
  Medido: a borda dura preto/branco tem min/max idênticos antes e depois, mesmo
  com `HaloProtect = 0`.
- Em borda de **contraste médio** (texto, linha fina, anel) quem protege é o
  **clamp** (etapa 5). É aí que o `HaloProtect` faz diferença visível — ver §4.
- Em área **plana com grão** quem protege é a **porta de ruído** (etapa 4). Sem
  ela, com contraste local baixo o `amp` é alto e o grão viraria o detalhe que o
  filtro realça — exatamente o pior caso para fonte de captura.

---

## 3. Controles

| Uniform | Faixa | Padrão | O que é |
|---|---|---|---|
| `Sharpness` | 0..1 | 0.35 | intensidade. 0 = identidade. 0.30–0.45 é a faixa útil em captura de console; acima de 0.7 o realce começa a aparecer como textura própria |
| `HaloProtect` | 0..1 | 0.80 | quanto o pixel pode ultrapassar o mais claro e o mais escuro dos oito vizinhos. 1.00 = overshoot zero; 0.00 = até um contraste local inteiro |
| `Denoise` | 0..0.20 | 0.025 | contraste local mínimo, em luma, para haver realce. 0.020–0.040 descarta o grão típico de captura 8 bits |
| `DebugView` | 0/1/2 | 0 | 1 = mapa de ganho (ajuste o `Denoise` até céu e parede lisa ficarem pretos e a textura continuar clara); 2 = delta aplicado, ×8, em torno de 0.5 |

Custo: um passe fullscreen, 9 fetches e ~40 ALU. Ao lado do Neural Rendering é
ruído estatístico.

---

## 4. Validação — o que foi realmente medido

Três execuções curtas do app (janela empurrada para −32000,−32000 assim que
aparece; processo morto no fim; `ReShade.ini` restaurado de backup), espelhando
uma janela-alvo **estática** de 960×600 desenhada para o teste: texto de 9 a
28 px, linhas de 1 px, círculos concêntricos finos, gradiente liso, um bloco
cinza uniforme, um bloco cinza com grão e uma borda dura preto/branco. Alvo
estático porque só assim os frames de duas execuções são comparáveis pixel a
pixel. O screenshot é o do próprio ReShade (backbuffer final, sem passar pelo
DWM), disparado por `PostMessage(WM_KEYDOWN, VK_SNAPSHOT)`.

O app entregou o frame em 1440×900 (alvo de 960×600 com o upscaling do DLSS).

### 4.1 O ReShade compila o shader

```
21:37:06:010 [ 3352] | INFO  | Successfully compiled
  'C:\Users\PCDOFAFA\Desktop\dlss cam\reshade-shaders\Shaders\DLSS5_Sharpen.fx' in 0.039000 s.
```

Sem avisos — ao contrário de `DLSS5_Feed.fx` e `lumenite_AnamorphicBloom.fx`, que
compilam com warnings na mesma execução. As três execuções repetiram a linha
(0.039 s na primeira, 0.002–0.003 s nas seguintes, com o cache do ReShade).
O add-on da ponte continua carregando normalmente:
`Registered add-on "DLSS 5 Bridge" v0.0.0.0 using ReShade API version 20.` —
**o add-on não precisou ser recompilado**: as funções genéricas de uniform e de
técnica já cobrem a nitidez.

### 4.2 Antes × depois, com os padrões

`preset-02-grade-neutra` (sem nitidez) contra `preset-05-nitidez`
(`Sharpness 0.35 / HaloProtect 0.80 / Denoise 0.025`). 21,1% dos pixels mudaram;
diferença média de 15,6/765 nos pixels alterados.

Acutância medida como gradiente médio `|dx| + |dy|` da luma na região:

| Região | Gradiente antes → depois | |
|---|---|---|
| linhas de 1 px, verticais | 0.2257 → 0.2583 | **+14,4%** |
| linhas de 1 px, horizontais | 0.3039 → 0.3423 | **+12,6%** |
| círculos concêntricos finos | 0.1393 → 0.1529 | **+9,7%** |
| texto 9–28 px | 0.0747 → 0.0803 | **+7,5%** |
| **bloco com grão** | 0.0161 → 0.0161 | **+0,3%** |
| **cinza liso** | 0.0000 → 0.0000 | **0%** |
| **gradiente liso do topo** | 0.0005 → 0.0005 | **0%** |

É exatamente o comportamento pretendido: detalhe fino ganha de 8 a 14%, grão e
áreas planas ficam onde estavam. O par ampliado está em
`docs\img\nitidez-antes-depois.png`.

### 4.3 As proteções fazem o que dizem

Terceira execução com tudo desligado — `Sharpness 1.00 / HaloProtect 0 /
Denoise 0` — contra a mesma referência:

| Região | padrão | extremo |
|---|---|---|
| linhas de 1 px, verticais | +14,4% | +39,4% |
| círculos finos | +9,7% | +29,3% |
| **bloco com grão** | **+0,3%** | **+96,6%** |

O grão quase dobra de amplitude quando `Denoise = 0`. Esse é o parâmetro que
separa "nitidez" de "realce de ruído de compressão" numa fonte que vem de
console pela placa de captura.

Overshoot, medido como mínimo e máximo de luma na faixa das linhas de 1 px
(fundo do alvo em 0.0941):

| | mínimo | máximo |
|---|---|---|
| sem nitidez | 0.0941 | 0.7762 |
| padrão (`HaloProtect 0.80`) | 0.0701 | 0.8387 |
| extremo (`HaloProtect 0`) | 0.0269 | 0.9476 |

Com a proteção padrão o vale escurece 0.024 abaixo do fundo; sem proteção,
0.067 — quase três vezes mais, e é isso que se vê como auréola escura em volta
das linhas. A borda dura preto/branco, por outro lado, tem min/max **idênticos**
nas três execuções (0.0000/0.0787 no lado preto, 1.0000/1.0000 no branco): ali é
a adaptação ao contraste que segura, e ela não depende de nenhum slider.

O painel de três colunas está em `docs\img\nitidez-halo-e-ruido.png`.

### 4.4 O que não foi verificado

- **Não há medição em movimento.** O alvo é estático de propósito, para o par ser
  comparável. Cintilação temporal — se existir — só aparece com cena em
  movimento; o argumento de que ela não deveria existir é estrutural (o filtro
  está abaixo do Feed, então nada do que ele faz volta para o histórico do DLSS),
  não empírico.
- **O `DebugView` não foi conferido na tela.** Ele compila e é um branch trivial,
  mas as duas vistas não foram inspecionadas visualmente.
- **As funções da ponte não foram exercitadas em runtime.** Elas compilam
  (`g++ -std=c++17` sobre um `.cpp` que chama todas) e são idênticas em estrutura
  às de LUT, que já funcionam; mas quem vai fazer a primeira chamada real é o
  painel.
- **A ordem `Sharpen → Grade` não foi medida contra `Grade → Sharpen`.** A
  escolha é argumentativa (§1), não experimental.

---

## 5. A ponte — o que o painel deve chamar

Em `reshade_bridge.h`, no mesmo padrão das funções de LUT (nome do efeito e da
técnica como constantes, degradação limpa devolvendo `false` quando o add-on não
está carregado):

```cpp
rsbridge::kSharpenEffect      // "DLSS5_Sharpen.fx"
rsbridge::kSharpenTechnique   // "DLSS5_Sharpen"

bool rsbridge::SetSharpen(float v);          // Sharpness    0..1
bool rsbridge::SetSharpenHalo(float v);      // HaloProtect  0..1
bool rsbridge::SetSharpenDenoise(float v);   // Denoise      0..0.2
bool rsbridge::SetSharpenDebugView(int m);   // DebugView    0..2
bool rsbridge::SetSharpenEnabled(bool on);   // liga/desliga a técnica
```

`SetSharpen` e `SetSharpenEnabled` também existem como alias no escopo global,
como `SetLut` e `SetGradeEnabled` (some com `DLSS5_BRIDGE_NO_GLOBAL_ALIASES`).

Os setters **recusam** valores fora da faixa em vez de deixar o clamp silencioso
para o shader: um slider mal ligado devolve `false` e aparece, em vez de virar
comportamento estranho na tela.

`true` significa "a ponte existe e o comando foi enfileirado", não "o valor já
está na tela" — ele entra no próximo frame. Note que `DLSS5BridgeStatus` só tem
`grade_loaded`/`grade_enabled`; **não há campo de status para a nitidez**. Se o
painel precisar saber se o `DLSS5_Sharpen.fx` compilou, isso exige um campo novo
no add-on (e recompilá-lo, com bump de protocolo). Enquanto não houver, o critério
prático é o retorno de `SetSharpen`: `false` quer dizer que nem a ponte está lá.

---

## 6. Arquivos entregues

| Arquivo | O quê |
|---|---|
| `reshade-shaders\Shaders\DLSS5_Sharpen.fx` | o shader (§2) |
| `reshade_bridge.h` | acrescentadas as funções da nitidez (§5); nada existente foi alterado |
| `preset-05-nitidez.ini` | `preset-02` + `DLSS5_Sharpen` entre Feed e Grade, valores moderados |
| `docs\img\nitidez-antes-depois.png` | quatro regiões, antes × depois, ampliadas |
| `docs\img\nitidez-halo-e-ruido.png` | três regiões, sem nitidez × padrão × extremo |
| `docs\nitidez.md` | este documento |

`main.cpp` e `screen_capture.h` não foram tocados. Os presets 01 a 04 não foram
tocados. O add-on não foi recompilado (não precisou). O `ReShade.ini` foi
modificado durante os testes e restaurado de backup — está como estava.
