# Vetores de movimento para o DLSS

## Resumo do dia

A pergunta que abriu o trabalho foi: *"será que tem como inserir algo pra referenciar os
motion vectors, ou quem sabe até mesmo dar um jeito de interpretar eles porcamente?"*

**Sim, e não precisou ser porco.** O app agora estima os motion vectors no **bloco de
vídeo da GPU** — o mesmo hardware que o encoder usa para achar deslocamento entre quadros.
Ele não disputa com o render: custa 0,95 ms a 3440x1440 numa fila paralela, enquanto o
estimador antigo (`Lumenite_Kernel`) gastava tempo da fila 3D, que é justamente o que
falta.

### Funcionando, medido com o app aberto

A cadeia inteira foi exercitada com um alvo de deslocamento conhecido. O DLSS recebeu:

| deslocamento real | o DLSS recebeu | erro |
|---|---|---|
| 2 px por quadro | 1,999 px | −0,05 % |
| 8 px por quadro | 7,996 px | −0,05 % |
| 20 px por quadro | 20,000 px | 0 % |

Com 100 % dos blocos válidos. A troca de provider é automática: se o hardware estiver
disponível o app usa; se não, volta sozinho ao `Lumenite_Kernel` sem o usuário mexer em
nada.

### O que isso resolve, e o que não resolve

**Resolve o rastro.** Sem motion vector, o DLSS somava o histórico no lugar errado e tudo
que se movia arrastava. Era a causa do "menos nítido, com borrões".

**Não devolve nitidez, e isso é limite da arquitetura, não do trabalho.** O DLSS também
precisa saber *em que ponto dentro do pixel* cada amostra foi tirada — o "jitter". Como o
app captura a imagem já pronta do jogo, essa informação não existe mais: o jogo a usou e
descartou. Sem ela o DLSS só consegue filtrar, não reconstruir. **Nem um motion vector
perfeito faria o DLAA virar fonte de nitidez aqui**; o que ele entrega é a imagem parar de
arrastar. Tentar forjar o jitter foi avaliado e é pior que não tentar — está explicado na
seção do teto, mais abaixo.

### Qual estimador roda de fábrica: o Kernel

Aqui houve uma reviravolta, e ela veio de medição contra o caminho que eu mesmo construí.

O `Lumenite_Kernel` — o estimador em shader que parecia quebrado o dia inteiro — **nunca
esteve quebrado**. Os zeros que apareciam eram a validação da cadeia zerando os vetores,
com um ajuste (`STATIC_BIAS=0,15`) que já foi corrigido. Medido com o alvo de deslocamento
conhecido, a **45 px por quadro**:

| estimador | leitura | erro |
|---|---|---|
| **Lumenite_Kernel** | **44,668 px** | **−0,7 %** |
| bloco de vídeo (VME), melhor configuração | 42,2 px | −6 % |
| bloco de vídeo, configuração anterior | 31,9 px | −29 % |

O Kernel trabalha em resolução plena com pirâmide multi-escala e não tem a ambiguidade de
casamento que aparece no hardware entre ~15 e ~70 px por quadro. Como a prioridade
declarada é **melhoria gráfica acima de latência**, precisão ganha de custo, e **o padrão
de fábrica passou a ser o Kernel**.

O caminho de hardware continua inteiro e disponível. Ele custa ~1 ms numa fila que **não
disputa com o render**, contra 15 passes de tela cheia do Kernel na fila 3D — quem estiver
com a GPU no limite troca precisão por quadros ligando-o.

| peça | o que faz |
|---|---|
| `DLSS5ForAll.exe` | estima os MV no bloco de vídeo quando pedido |
| `dlss5-bridge.addon64` | publica os MV no ReShade e troca o provider sozinho |
| `DLSS5_MV_Hardware.fx` | leva os MV até o `DLSS5_Feed.fx` |
| `DLSS5_Feed.fx` | ganhou o aviso de desconfiança que faltava (`STATIC_DISTRUST`) |

Para ligar o modo leve: `mv_hardware=1` na seção `[captura]` do `dlss5forall.ini` (a chave
precisa ser criada à mão; o app só a lê, e só na abertura).

### O que ainda não se sabe

- **Se a imagem ficou visivelmente melhor.** A medição de nitidez de saída ainda não tem
  amostras suficientes para concluir. O que está provado é que o DLSS passou a receber
  motion vector correto; se isso se traduz em imagem melhor aos olhos, ainda não foi
  fechado.
- **O `Lumenite_Kernel` nunca foi visto funcionando.** Ele entrega quase zero desde o
  começo e a causa não foi encontrada. Como o caminho de hardware o substituiu, virou
  assunto de segunda prioridade.

## O problema

O app captura a imagem **final** do jogo (Windows Graphics Capture), apresenta numa
camada e roda a cadeia ReShade em cima. Não existe motion vector do jogo para pegar --
eles morreram dentro do processo do jogo.

O DLSS em modo DLAA é **acumulação temporal**: ele guarda o histórico dos quadros
anteriores e o reprojeta para o quadro atual usando os motion vectors. Sem MV não há
reprojeção: o histórico é somado no lugar errado e tudo que se move deixa rastro. É
exatamente o "menos nítido, com borrões" que o usuário relatou.

O `dlss5-feed.log` mede isso direto:

```
[feed] MV probe (centre 64x64, frame 1200): mean |mv| 0.000 px, max 0.00 px,
       0% non-zero  <-- DLSS is getting (almost) no motion vectors
```

## Estado da cadeia, conferido

- `FafaDLSS5.ini`: `Techniques=Lumenite_Kernel, DLSS5_Feed, DLSS5_Sharpen` -- o Kernel
  **está** ligado e **está** primeiro.
- `dlss5-feed.log`: `DLSS5_MV_PROVIDER=3 (LumeniteFX Kernel) -> Lumenite_Kernel (enabled)`
  -- o feed reconhece o provider e o encontra.
- `ReShade.log`: `Successfully compiled lumenite_Kernel.fx`, sem avisos de textura.

Ou seja, o Kernel **não está "mal ligado"**: ele roda, é reconhecido, e mesmo assim o
`Kernel::tFlow` chega zerado no feed. A causa está dentro do próprio Kernel ou nos
dados que ele recebe -- não na fiação. (Investigação em curso com o agente `nitidez`.)

Teste de 30 segundos para separar as duas hipóteses, se ainda for útil: compile o
Kernel com `DEBUG_KERNEL=1` e ponha `DEBUG_VIEW=2` (Optical Flow). Se a tela ficar
cinza uniforme, o `tFlow` é zero na origem; se aparecer campo colorido, o `tFlow` tem
conteúdo e o problema é o compartilhamento da textura entre os dois arquivos de efeito.

## Os cinco caminhos, ordenados por (ganho de imagem) / (custo de GPU + risco)

| # | Caminho | Ganho | Custo GPU | Risco | Veredito |
|---|---------|-------|-----------|-------|----------|
| 1 | **(E) `reset_every=1`** | acaba com o rastro, perde o ganho temporal | **zero** | nenhum | **fazer agora** |
| 2 | **(A) VME em hardware** | MV real, grade 215x90 | 0,95 ms na fila de **vídeo** | baixo, protótipo pronto | **fazer em seguida** |
| 3 | (D) consertar o Kernel | MV de melhor qualidade que o VME | 3-6 ms na fila **3D** | médio, causa desconhecida | vale se for barato |
| 4 | (C) estimador próprio em HLSL | igual ao Kernel | igual ao Kernel | alto (reescrever o que já existe) | não |
| 5 | (B) NVIDIA Optical Flow | melhor qualidade de todas | ~igual ao VME | alto (sem header) | depois, se o VME não bastar |

### (E) Desligar a acumulação temporal -- rede de segurança, entregável hoje

`dlss5-feed.cfg` já tem a chave. Está em `reset_every=0`:

```ini
reset_every=1
```

As strings do próprio `dlss5-feed.addon64` confirmam a semântica: *"Reset every frame
(diagnostic)"*. Com `reset=1` todo quadro o DLSS descarta o histórico e vira um filtro
**espacial**: perde o ganho temporal, mas **não deixa rastro nenhum**. Para quem está
vendo borrão hoje, "nítido sem fantasma" provavelmente ganha de "suave com fantasma".

Como é um inteiro, vale testar valores intermediários: `reset_every=4` limita o rastro
a quatro quadros e ainda acumula um pouco. Vale medir 1, 2 e 4 e deixar o usuário
escolher.

Ainda no mesmo arquivo: `preset=1`. A ajuda embutida do add-on diz que *"os presets
diferem em quão duro o DLSS prende o histórico contra o quadro atual; se o movimento
deforma em volta de transparências, tente E ou F"*. Um preset que prende mais o
histórico também reduz rastro sem desligar nada. Vale testar junto.

### (A) D3D12 Video Motion Estimation -- o caminho escolhido, com protótipo pronto

É o bloco de estimativa de movimento do **encoder de vídeo** da GPU. Roda na fila
`VIDEO_ENCODE`, que não disputa com a fila 3D -- e a fila 3D é exatamente o recurso que
falta (a cadeia já satura a GPU a 3440x1440).

Disponível nesta máquina: `C:\msys64\mingw64\include\d3d12video.h` compila com o nosso
toolchain, e a RTX 4080 reporta:

```
suporte NV12: blocos=8x8 16x16  precisao=quarter-pel  tamanho=32..4096 x 32..4096
```

3440x1440 cabe folgado.

**Precisão, com deslocamento conhecido** (`vme_test.exe`, VME cru):

| deslocamento imposto | mediana medida | blocos com vetor |
|---|---|---|
| +7, 0 px | **(-7,00, +0,00)** | 100 % |
| -13, +5 px | **(+13,00, -5,00)** | 100 % |
| +40, 0 px | (-40,00, 0,00), mas a média cai para -29,4 | 99,8 % |
| +96, 0 px | (-2,00, 0,00) -- **colapsa** | 99,3 % |
| 0, 0 px (quadro idêntico) | **(0,00, 0,00)** | **0 %** |

Duas conclusões que importam:

- **O sinal já é o certo.** O VME devolve *backward MV* (do pixel atual para onde ele
  estava no quadro anterior), que é a convenção do DLSS e do `DLSS5_Feed.fx`. Não
  precisa inverter, não precisa mexer em `MV_SIGN`.
- **Ele não inventa movimento numa cena parada.** Quadro idêntico dá exatamente zero.
  O comportamento atual (MV zero) é preservado onde deve ser preservado.

**Custo, caminho completo** (`me_test.exe`: BGRA → luma → NV12 → VME → resolve →
delta UV, com sincronização entre as APIs, 3440x1440):

| divisor | bloco | grade | ms/quadro |
|---|---|---|---|
| 1 | 8 | 430x180 (buffer/8) | 3,12 |
| 2 | 8 | 215x90 (buffer/16) | **0,95** |
| 2 | 16 | 107x45 | 0,67 |
| 4 | 8 | 107x45 | 0,40 |

**Recomendado: divisor 2, bloco 8.** Meia resolução com bloco 8x8 dá a mesma grade que
resolução plena com bloco 16x16 pela metade do custo, e **dobra o alcance de busca
medido em pixels de saída** (verificado: com divisor 2 um deslocamento de 60 px de
saída ainda sai mediana -59,996, enquanto em resolução plena já degradava aos 40 px).

Um defeito encontrado e corrigido no caminho: se a resolução de estimativa não for
múltipla do tamanho do bloco (1720 / 16 = 107,5), o VME devolve **a grade inteira
zerada e ainda custa 4x mais** -- falha silenciosa, sem erro nenhum. O
`motion_estimate.h` arredonda para baixo até um múltiplo do bloco.

#### Alcance de busca real, medido na RTX 4080

Varredura de deslocamento imposto, 3440x1440, lendo a mediana da grade. A coluna é o
maior deslocamento em que a mediana ainda bate com o imposto:

**Estes números substituem uma tabela anterior mais otimista.** A primeira varredura usou
passos grossos e li como "alcance" valores que só passaram por sorte de alinhamento do
padrão. A varredura fina abaixo é a que vale.

| divisor | grade | alcance confiável | 1ª falha | custo |
|---|---|---|---|---|
| 1 | 430x180 (buffer/8) | **±40 px** | 48 px | 3,12 ms |
| 2 | 215x90 (buffer/16) | **±80 px** | 90 px | 0,95 ms |
| 4 | 107x45 (buffer/32) | **±160 px** | 192 px | 0,40 ms |

Convertendo tudo para a **resolução de estimativa**, o resultado fica limpo e é uma
propriedade do hardware, não dos divisores:

> O VME acerta na mosca até **40 px de deslocamento na resolução em que ele estima**, e a
> primeira falha de cada divisor cai sempre entre **45 e 48 px** de estimativa.

Acima disso ele não falha em degrau limpo: com divisor 2, 90 px erra (dá -32,99) mas 96 px
acerta (dá -95,95). Esse acerto é alinhamento de padrão, não alcance — por isso o corte
automático fica no último valor **consistente** (`40 * divisor`) e não no maior que já
passou. Errar para o lado seguro aqui é barato; errar para o outro entrega vetor errado ao
DLSS, que é pior que vetor nenhum.

**Consequência direta para a escolha do divisor.** A chegada real medida com um alvo que
coopera é de 60,0 fps (`max 21,21 ms` de intervalo), e a essa taxa um giro de 90°/s em
21:9 desloca ~49 px/quadro. Confrontando com a tabela:

- **divisor 1 não cobre o movimento típico** (±40 px contra 49 px típicos). A densidade de
  grade de 1/8 que ele traz não serve de nada se ele perde o alvo na panorâmica comum.
- **divisor 2 cobre com margem de 60 %** (±80 px contra 49 px).

É o argumento que decide entre 1 e 2, e ele não estava disponível antes desta varredura.

**Sobre a especificação de ≥250 px/quadro.** O VME não alcança 250 px em nenhum divisor
que preserve densidade de grade útil. Duas observações honestas sobre isso:

- Com divisor 2 o VME **empata** com o alcance do `Lumenite_Kernel` (±96 px, que vem do
  `mip 5` com `SEARCH_RADIUS 3`), a uma fração do custo de fila 3D. Com divisor 4 ele
  **supera** o Kernel. Nenhum dos dois chega a 250.
- Os 167 px/quadro daquele cálculo saem de uma taxa de chegada de 17,5 fps, que é a
  captura passando fome, não o jogo. A 60 fps de chegada o mesmo giro de 90°/s dá 49
  px/quadro -- dentro do alcance do divisor 2 com folga. O conserto certo para o caso de
  167 px é a entrega de quadros, não um estimador de ±250 px: a 17,5 fps a acumulação
  temporal já não tem o que reprojetar de útil.

Por isso `mv_divisor` é chave de ini: se a medição com jogo real mostrar deslocamento
alto de verdade, `mv_divisor=4` troca densidade de grade por alcance sem recompilar.

**O limite honesto.** Com deslocamento de 200 px o VME não devolve um vetor grande e
errado -- devolve um vetor **pequeno e errado** (mediana -2,5 px, 96 % dos blocos
não-zero). Nenhum limiar de magnitude pega isso: o filtro de descarte por tamanho
elimina o caso "rápido demais", não o caso "casou o bloco errado". MV errado espalha o
histórico numa direção errada e é **pior que MV zero**.

**Sobre a especificação de densidade (full-res, ou 1/2 no pior caso).** Não é atingível
por nenhum estimador de bloco, e o alvo merece uma correção: o que o
`Kernel::tFlow` entrega em 1/8 não é "precisão de 8 px" -- é *um vetor a cada 8 px*, e
cada vetor tem precisão subpixel (fit parabólico no Kernel, quarter-pel no VME). São
coisas diferentes. O `PS_MotionVectors` do Feed roda em resolução plena e interpola o
campo, então o DLSS recebe MV full-res em ambos os casos; o que muda é quantas
descontinuidades de movimento o campo consegue representar. Com divisor 2 o VME fica em
1/16 -- metade da densidade do Kernel, com o dobro da precisão de vetor e alcance igual.
`mv_divisor=1` recupera exatamente a densidade do Kernel (1/8) por 3,12 ms. É um A/B de
uma linha de ini, e é o teste que decide.

**Sobre gates de magnitude mínima.** O `PS_ATrousPassB` do Kernel zera tudo abaixo de
0,2 px, e essa é a suspeita de por que ele entrega quase nada. Este caminho não tem
gate equivalente: o único descarte é por magnitude **máxima** (`rejectPx`, automático em
`32 * divisor`), e o `MV_HW_MAX_PX` do shader começa em 96 px. Nada aqui zera vetor
pequeno -- um deslocamento de 0,25 px sai como 0,25 px.

Por isso a entrega é **"MV grosseiro + validação forte"**, nunca MV cru. O
`DLSS5_Feed.fx` já tem exatamente as ferramentas para isso, e elas comparam os patches
de imagem de verdade -- é a única coisa que pega o caso do bloco errado:

- `MV_VALIDATE=1` e `VALIDATE_MV=1` -- liga a bateria de testes.
- `VALIDATE_STATIC=1` -- a hipótese "não se moveu" compete com o vetor pela estrutura
  do patch (média removida, então cintilação não vira movimento). Ganha um vetor
  inventado.
- `MV_CONSISTENCY=1.4` -- compara o vetor com o do quadro anterior no lugar de onde o
  pixel veio. Vetor incoerente entre quadros é o sintoma clássico do bloco errado.
  Com MV de bloco vale **apertar** este valor (1,0-1,2) e medir.
- `VALIDATE_LUMA=1`, `LUMA_TOLERANCE=0.25` -- luma reprojetada fora da faixa local.
- `MASK_STRENGTH=1.0` -- transforma a desconfiança em máscara para o DLSS.
- `VALIDATE_DEPTH=0` e `GEOM_ENABLE=0` -- **manter desligados**: não há depth real
  numa captura WGC, e o modelo geométrico de câmera precisa de depth.
- `STATIC_BIAS=0.0` -- subir isto faz o teste estático ganhar mais vezes, ou seja,
  zerar mais vetores. É o botão certo se o rastro continuar em superfícies paradas.

### (D) Consertar o `Lumenite_Kernel`

Continua sendo o caminho de **melhor qualidade de vetor**: é block matching hierárquico
(pirâmide 128→64→32→16→8), com filtro de mediana, à-trous e mapa de confiança. Isso é
algoritmicamente superior ao VME, que é bloco fixo, sem pirâmide e sem confiança.

O que ele **não** tem é o custo certo: são 16 passes de tela cheia a 3440x1440 na fila
3D, com `ZMSAD` amostrando mips em cada candidato. É o recurso que falta.

Veredito: se o `nitidez` achar a causa e for barata, vale ligar de volta e comparar
lado a lado com o VME. Mas mesmo funcionando, o VME provavelmente ganha na conta
final por rodar fora da fila 3D. Os dois não são exclusivos: dá para deixar o Kernel
como opção de qualidade para quem tem folga de GPU.

### (C) Estimador próprio em HLSL

Reescrever em casa o que o `lumenite_Kernel.fx` já faz, com o mesmo custo de fila 3D e
sem o benefício de já estar escrito e depurado. Só faria sentido como um estimador
deliberadamente **mais barato** que o Kernel (1/4 de resolução, busca em 3 passos) --
e nesse nicho o VME já ganha, porque custa zero da fila 3D. **Descartado.**

### (B) NVIDIA Optical Flow

`C:\Windows\System32\nvofapi64.dll` existe e a GPU é uma RTX 4080, então o acelerador
está lá. Qualidade bem melhor que o VME: granularidade 4x4 ou 1x1 em vez de 8x8, e um
mapa de custo por vetor (confiança de verdade, que o VME não dá).

O custo é de integração: não temos o header do SDK nesta máquina, então seria preciso
declarar a interface `NvOFAPI` à mão a partir da documentação e acertar o ABI por
tentativa -- com o agravante de que a assinatura pode mudar entre versões de driver.

Veredito: **não agora.** O VME dá 80 % do benefício com 10 % do risco e já está
funcionando. Reavaliar se, depois de calibrar a validação, o VME ainda deixar rastro
visível em movimento rápido -- aí o mapa de custo do NVOFA passa a valer o trabalho,
porque resolve justamente o caso "bloco errado" que hoje depende inteiramente da
validação do shader.

## O que está pronto

| Arquivo | O que é |
|---|---|
| `motion_estimate.h` | O estimador. Header autocontido, sem dependência do resto do app. |
| `reshade-shaders/Shaders/DLSS5_MV_Hardware.fx` | Publica os vetores em `Kernel::tFlow`, que é o que o `DLSS5_Feed.fx` já lê com `DLSS5_MV_PROVIDER=3`. |
| `scratchpad/vme_test.cpp` | Teste do VME cru com deslocamento conhecido. |
| `scratchpad/me_test.cpp` | Teste ponta a ponta do `motion_estimate.h`. |

Compilar os testes:

```sh
export PATH="/c/msys64/mingw64/bin:$PATH"
g++ -std=c++17 -O2 -o vme_test.exe vme_test.cpp -ld3d12 -ldxgi -luuid -lole32
g++ -std=c++17 -O2 -I"/c/Users/PCDOFAFA/Desktop/dlss cam" -o me_test.exe me_test.cpp \
    -ld3d11 -ld3d12 -ldxgi -ld3dcompiler -luuid -lole32
./me_test.exe 3440 1440 7 0 2 8       # deve imprimir mediana (-6,994, 0,000)
```

## Integração aplicada

Está tudo ligado. Três pontos no `main.cpp`, um export novo no add-on, um shader novo.

### `main.cpp` — quatro edições

| onde | o quê |
|---|---|
| linha 56 | `#include "motion_estimate.h"` |
| logo após `QpcMs()` | o namespace `MvHw` (o estimador, o resolvedor do export da ponte, a sonda periódica). Ficou aqui, e não junto do `Renderer`, porque precisa de `Log`, `QpcMs` e `IniPath`, e `IniPath` só existe depois do seletor. |
| após `scap.Start(...)` e o `Resize` inicial | `MvHw::Ensure(R.srcW, R.srcH, R.dev)` — só aqui a resolução da fonte está decidida |
| no laço, após o teste de `R.deviceMorreu`, dentro de `if (got)` | `MvHw::Ensure(...)` (pega troca de resolução) + `MvHw::Frame(R.ctx, R.extSrv ? R.extSrv : R.srv)` |
| antes de `R.Shutdown()` | `MvHw::Stop()` |

O `R.extSrv ? R.extSrv : R.srv` é a mesma expressão que o `DrawAndPresent` usa, então o
estimador enxerga exatamente o quadro que vai para a tela, nos dois caminhos de captura
(WGC zero-cópia e câmera via `Upload`).

### A invariante de ordem, que é fácil de quebrar depois

**O add-on guarda o ponteiro do nosso SRV e o republica em todo `begin_effects`. Toda vez
que esse SRV for destruído, a ponte tem de ser zerada ANTES.** São dois lugares, e os
dois já cumprem:

- `MvHw::Stop()` vem antes do `R.Shutdown()` no encerramento;
- `MvHw::Ensure()` chama `SetExternalMV(nullptr)` antes do `g_est.Shutdown()` na troca de
  resolução.

O segundo era um use-after-free de verdade, encontrado e corrigido depois da primeira
integração: numa troca de resolução (o `FonteMudou` dispara quando o jogo muda de modo) o
`Ensure` derrubava o estimador sem avisar a ponte, que seguia publicando memória morta até
o próximo `Frame` bem-sucedido — e, se o `Init` novo falhasse, **para sempre**, porque o
`Frame` sai cedo quando o estimador não está ativo.

### Chaves de ini (`dlss5forall.ini`, seção `[captura]`)

```ini
mv_hardware=1     # 0 desliga tudo e volta ao Lumenite_Kernel
mv_divisor=2      # 1 = grade 1/8 e ±64 px; 2 = 1/16 e ±96 px; 4 = 1/32 e ±160 px
mv_bloco=8        # 8 ou 16
```

### Linhas de log a procurar no `dlss5forall.log`

```
mv-hw: VME ok, grade 215x90, divisor 2, bloco 8 (fonte 3440x1440)
mv-hw: quadro 600 | Update 0.183 ms de CPU | 20 amostras: |mv| media 3.412 px, mediana 3.998 px, max 41.75 px | 87% nao-zero, 2% descartado, 5% campo zerado
```

O formato da sonda é o mesmo do `dlss5-feed.log` de propósito, para os dois números
poderem ser comparados linha a linha. Ela lê a grade de volta para a CPU e custa um
sincronismo, por isso roda a cada 600 quadros. **O tempo reportado é de CPU** (submissão
de comandos); o custo de GPU é o da tabela acima, medido em bancada — a CPU aqui é quase
nada porque a espera pela fila de vídeo acontece na linha do tempo da GPU.

### Fallback automático, sem o usuário mexer em nada

O usuário nunca precisa editar preprocessor no overlay. A escolha é feita em código, em
duas camadas:

1. **O app** só chama `DLSS5Bridge_SetExternalMV(srv)` quando o estimador subiu. Se o VME
   não existir, se o driver recusar, se `mv_hardware=0`, ou se o `.addon64` for antigo e
   não tiver o export, ninguém é avisado e nada muda.
2. **O add-on**, em `reshade_present`, liga `DLSS5_MV_Hardware` e desliga
   `Lumenite_Kernel` quando o SRV chega — e faz o inverso quando ele some. Só age na
   transição, e só em `present`: ligar técnica de dentro de `begin_effects` derruba o
   processo (já documentado no add-on).

O preset reflete essa divisão: `DLSS5_MV_Hardware` está no `TechniqueSorting` (que define
a **ordem**, antes do `DLSS5_Feed`) mas **fora** do `Techniques=` (que define quem está
**ligada**). Ou seja, o estado de repouso do preset é exatamente o comportamento de hoje,
e é para lá que tudo cai se qualquer peça faltar.

O `DLSS5_MV_PROVIDER` continua em `3` nos dois `.ini`. Não muda: os dois shaders escrevem
no mesmo `Kernel::tFlow`, e é por isso que só um deles pode estar ligado por vez.

### Add-on: um export novo, protocolo intocado

```cpp
extern "C" __declspec(dllexport) int DLSS5Bridge_SetExternalMV(void *srv);
```

Recebe o `ID3D11ShaderResourceView` como `void*`: em D3D11 o handle de `resource_view` do
ReShade **é** esse ponteiro, então o add-on não precisa dos headers do D3D11. Nenhuma
struct mudou e o número do protocolo continua 2 — um exe antigo simplesmente não resolve
o export, um add-on antigo não o tem, e os dois casos degradam para "sem MV de hardware".

Backup do binário anterior em `dlss5-bridge.addon64.bak-r6`.

### `build-mingw.sh`

Ganhou `-ld3d12` e, no topo, o `export PATH="/c/msys64/mingw64/bin:$PATH"` que o
`build-addon.sh` já tinha — sem ele o `cc1plus` morre em silêncio (sai 1, não imprime
nada).

**Não** ganhou `-ld3dcompiler`: o `motion_estimate.h` carrega o `D3DCompile` por
`GetProcAddress`, igual ao `Renderer::InitRest` do app já fazia. Linkar criaria uma
dependência dura de `d3dcompiler_47.dll` no carregamento do exe — uma DLL faltando
deixaria o app inteiro sem abrir por causa de um recurso opcional.

### Uso do `motion_estimate.h`

```cpp
#include "motion_estimate.h"

dlss5::MotionEstimator g_me;

// uma vez, quando a resolução de captura estiver conhecida:
g_me.Init(d3d11Device, capturedW, capturedH, /*divisor*/ 2, /*bloco*/ 8);

// por quadro, logo depois da captura:
g_me.Update(immediateContext, capturedBGRA);
```

E no add-on ponte, uma vez por quadro antes dos efeitos rodarem:

```cpp
runtime->update_texture_bindings("DLSS5_EXTMV", srv, srv);   // srv = g_me.MotionSRV()
```

`update_texture_bindings` está em `addon/include/reshade/reshade_api.hpp:523`. É o
mecanismo oficial do ReShade para ligar um recurso externo a uma semântica de textura
de efeito -- sem hack, sem tocar no `DLSS5_Feed.fx`.

### Detalhe de integração que vale ler

O `Update()` faz o D3D11 esperar a fila de vídeo **na linha do tempo da GPU** (não
trava a CPU), mas a fila 3D fica parada nesse intervalo. Chame o `Update()` **logo
depois da captura**, o mais cedo possível no quadro, e não imediatamente antes da
cadeia ReShade: assim o trabalho que o app faz no meio esconde a latência do bloco de
vídeo e o custo efetivo cai bem abaixo dos 0,95 ms medidos.

## Configuração

`reset_every` e o MV de hardware **se anulam** e não devem ser ligados juntos: reset por
quadro descarta o histórico, que é exatamente o que um MV bom serviria para reprojetar.
`reset_every=1` é o plano B para o caso do MV de hardware não entregar, não um
complemento. A medição do `reset_every` é do agente `nitidez`.

Validação no `[DLSS5_Feed.fx]` do `FafaDLSS5.ini` — o que está aplicado agora:

```ini
MV_VALIDATE=1
VALIDATE_MV=1
VALIDATE_STATIC=1
VALIDATE_LUMA=1
VALIDATE_DEPTH=0        # não há depth real numa captura WGC
GEOM_ENABLE=0           # o modelo geométrico de câmera precisa de depth
MV_CONSISTENCY=1.100000 # apertado de 1.4: com MV de bloco, vetor incoerente entre
                        # quadros é o sintoma clássico de "casou o bloco errado"
MASK_STRENGTH=1.000000
STATIC_BIAS=0.000000
```

`STATIC_BIAS` é o botão a subir se sobrar rastro em superfície parada: ele faz a
hipótese estática ganhar mais vezes, ou seja, zerar mais vetores.

### O mapa de confiança, e por que ele não pode ser constante

O `STATIC_DISTRUST` pesa a marcação pelo mapa de confiança do provider, e esse peso é o
que separa *"provider seguro foi contrariado pelo teste estático, isso é contradição"* de
*"provider inseguro, é provavelmente só uma superfície parada"*. A primeira versão do
`DLSS5_MV_Hardware.fx` publicava confiança **1.0 constante**, o que fazia o `saturate()`
sumir da conta e marcava todo pixel onde o estático vencesse — inclusive as superfícies
genuinamente paradas, exatamente o caso que não se deve marcar. O peso deixava de pesar.

O `ResolveMotionVectorHeap` não devolve custo nem SAD, então não há confiança vinda do
hardware. O que dá para medir honestamente é a **coerência local**: um bloco cujo vetor
concorda com os oito vizinhos é um casamento confiável; um bloco isolado apontando para
outro lado é ruído de block matching. É um 3x3 na própria `tFlow`, num passe depois do de
publicação, com a escala exposta em `MV_HW_COHERENCE_PX` (padrão: 4 px de desvio levam a
confiança a zero).

### O descarte não pode ser silencioso

O descarte por magnitude escreve `(0,0)` no vetor. Do lado do Feed isso é
**indistinguível de "este pixel não se moveu"**, e a diferença importa:

- *"não se moveu"* → reprojetar o histórico da própria posição está **certo**
- *"eu não sei"* → reprojetar da própria posição **prende** o histórico ali

E o mecanismo que existe justamente para avisar o DLSS quando um vetor foi zerado — o
teste da hipótese estática, com o `STATIC_DISTRUST` do P1 — **estruturalmente não vê
isto**, porque ele só roda para vetor acima de meio pixel (`length(mv * screen) > 0.5`) e
um vetor descartado tem magnitude zero. Escapa por baixo.

Não é teórico: a **90 px** de deslocamento com divisor 2, o descarte atinge **45,4 % dos
blocos** (medido). Quase metade do quadro prenderia o histórico calada, e a outra metade
entregaria vetor errado — o pior dos dois mundos.

A correção é um canal. A textura de saída virou **RGBA16F**: `.xy` é o delta UV e `.z` é a
**validade** (1 = o hardware entregou, 0 = descartado aqui). O `DLSS5_MV_Hardware.fx`
multiplica a confiança de coerência por essa validade, e o `DLSS5_Feed.fx` ganhou o termo
que faltava:

```hlsl
if (length(flow * BUFFER_SCREEN_SIZE) <= 0.5)
    distrust = max(distrust, (1.0 - conf) * STATIC_DISTRUST);
```

Ou seja: **confiança baixa num vetor zerado** é o provider dizendo "eu não sei", e é
exatamente aí que reprojetar da própria posição prende. Um pixel genuinamente parado fica
num campo coerente e mantém confiança alta, então não é afetado — é essa a discriminação.
O termo é escopado a vetores quase nulos de propósito: para um vetor que sobreviveu, a
confiança baixa já é tratada pelos testes de consistência e luma, e duplicar ali jogaria
histórico fora em toda borda de movimento.

**Armadilha para quem mexer depois:** o `VALIDATE_LUMA=1` também cobre boa parte deste caso
por efeito colateral — com vetor zero, o teste de luma compara o pixel atual com o quadro
anterior na **mesma posição**, o que destoa em qualquer coisa que se moveu. É útil, mas é
acaso. **Desligue `VALIDATE_LUMA` e o zeramento do provider volta a ser silencioso**, com
apenas o termo acima segurando.

A sonda do log ganhou a coluna correspondente: `%% descartado`. Se ela subir de forma
consistente, o conteúdo está se movendo mais rápido do que o divisor alcança, e a saída é
`mv_divisor=4`. É o único número do log que diz isso.

### Um só limiar de magnitude

`MV_HW_MAX_PX` fica em **0 (desligado)** e o descarte acontece só no `motion_estimate.h`,
em `40 * divisor`. Dois limiares disputando o mesmo trabalho apenas escondem qual dos dois
cortou.

O limiar que sobrou pega o caso "rápido demais": com divisor 2 e 90 px de deslocamento, o
hardware devolveria -32,99 px (errado) e o descarte transforma isso em zero. Ele **não**
pega o caso "casou o bloco errado e devolveu vetor pequeno e errado" — a 130 px o campo sai
com -3,5 px e nenhum limiar de magnitude o distingue de movimento lento real. Para esse
caso não existe limiar; quem pega é a validação por patch do `DLSS5_Feed.fx`.

## O teto: MV bom tira o fantasma, não devolve nitidez

`Jitter.Offset.X/Y` é como o renderizador diz à rede **em que ponto dentro do pixel** a
amostra foi tirada. Aqui esse par é `(0,0)` em todo quadro, e o `flags=66` do
`dlss5-feed.log` não traz o bit `MVJittered`. A rede acredita que toda amostra veio do
centro do pixel.

O motion vector **não** conserta isso, porque responde a outra pergunta: para onde o
conteúdo foi, não em que fase ele foi amostrado. Sem fase, não há reconstrução subpixel —
o DLAA só filtra.

**Consequência, e vale dizer com todas as letras: nem um MV perfeito faz o DLAA virar
fonte de nitidez nesta arquitetura.** O teto é o jitter. O que um MV bom entrega é outra
coisa, e é a coisa de que o usuário reclamou: **menos arrasto**.

### E não, injetar jitter não resolve

A saída óbvia é deslocar a imagem apresentada por uma fração de pixel a cada quadro e
declarar esse deslocamento como jitter. Não funciona, e é importante que esteja escrito
aqui para ninguém gastar uma rodada tentando: a imagem capturada **já está amostrada** na
resolução de saída — o jogo já resolveu o TAA dele e jogou a informação de subpixel fora.
Reamostrar essa imagem com meio pixel de deslocamento não recupera fase nenhuma, só move
o que já existe.

Pior: declarar esse deslocamento como jitter **mente para a rede**. Ela passaria a tratar
amostras que são reamostragens da mesma amostra central como se fossem observações
independentes em fases diferentes, e combiná-las como tal — o que amolece a imagem em vez
de reconstruí-la. A informação não está lá; fingir que está é ativamente pior que
declarar `(0,0)`.

## Onde a densidade de grade importa de verdade

Onde a cena inteira se move junto, um único vetor descreveria tudo e a densidade é
irrelevante — bilinear e pontual dão o mesmo valor.

Ela importa na **descontinuidade de movimento**: a borda de um objeto passando na frente
do fundo. Ali os dois blocos vizinhos descrevem movimentos diferentes, e a interpolação
bilinear entrega a **média** dos dois numa faixa da largura de um bloco — um vetor que não
descreve nem o objeto nem o fundo, com erro da ordem da *diferença* entre os dois
movimentos. É onde nasce o fantasma de borda.

O `PS_Publish` troca a interpolação pela pontual conforme as duas amostragens discordam
(`MV_HW_EDGE_SHARP`, `MV_HW_EDGE_PX`). Pontual entrega um dos dois movimentos **reais**:
certo de um lado da borda, errado do outro — e o lado errado a validação por patch do Feed
pega, porque o patch não casa. Um vetor certo pela metade é recuperável; uma média que não
descreve nada, não. Em movimento uniforme as duas amostragens coincidem e o blend não faz
nada, então isso não custa qualidade onde não há borda.

Isto é também o que o A/B de `mv_divisor` está medindo de verdade. **Um alvo de rolagem
uniforme não separa 1 de 2** — sem descontinuidade de movimento, os dois entregam o mesmo
campo. O teste precisa de um objeto se movendo em velocidade diferente do fundo.

### E aí a medição respondeu, sem precisar abrir o app

`scratchpad/edge_test.cpp` reproduz essa cena — fundo rolando a `shift`, uma janela fixa
na tela cujo conteúdo rola a `shift + dv`, mesma estatística dos dois lados, sem borda
desenhada — e mede o **campo de MV direto contra a verdade exata**, que aqui é conhecida em
todo pixel. É o mesmo cenário que o `mvtarget` monta para o app, mas medindo a coisa em si
em vez do proxy a três camadas de distância (MV → DLSS → nitidez da imagem).

Erro médio do campo, em pixels, na faixa de ±32 px em torno da borda (fundo a 4 px):

| janela | div 1 bilinear | div 2 bilinear | div 2 pontual |
|---|---|---|---|
| 12 px | 0,790 | 0,817 | **0,570** |
| 20 px | 3,070 | 3,111 | **2,703** |
| 28 px | 5,026 | 5,103 | **4,504** |
| 36 px | **12,480** | 7,629 | **6,897** |

Três conclusões, todas medidas:

1. **A densidade não compra nada na borda.** Divisor 1 e divisor 2 dão praticamente o
   mesmo erro (0,790 vs 0,817; 3,070 vs 3,111; 5,026 vs 5,103). O argumento de que a grade
   1/8 ajudaria justamente na descontinuidade **não sobrevive à medição** — dobrar a
   densidade estreita a faixa afetada, mas não reduz o erro dentro dela.
2. **Com movimento maior, o divisor 1 desaba**: na janela a 36 px ele vai a 12,48 px de
   erro na borda e **23,31 px no interior**, contra 7,6 e 3,9 do divisor 2. É o limite de
   alcance de ±40 px mordendo, exatamente como a varredura previu.
3. **A amostragem pontual na borda ganha de 10 a 30 %**, de forma consistente. O
   `MV_HW_EDGE_SHARP` é ganho real, ainda que modesto.

A calibração do `MV_HW_EDGE_PX` saiu daí também: com o padrão original de 6,0 a mistura
ficava em 0,733 quando o pontual puro dava 0,570 — metade do ganho na mesa. **2,0 captura
todo o ganho** e continua sendo mistura suave, não degrau.

**O limite honesto desta medição:** ela mede o erro do campo de MV, que está *a montante*
do DLSS. Um erro de 5 px numa faixa de borda pode ou não virar fantasma visível — quem
responde isso é o run com o app. Mas a implicação lógica é firme numa direção: **se o
divisor 1 não reduz o erro do campo, ele não pode reduzir o fantasma que vem daí.** O A/B
de divisor no app deixa de ser necessário para decidir, e vira confirmação.

## Run 13:13 — o caminho funciona, e o que ele custa

Primeira medição com o app rodando (alvo `mvtarget`, 1280x720, logs em
`scratchpad/run-mv-131347/`). Conduzida pelo agente `nitidez`.

### Funcionou

```
13:13:51:804  Successfully compiled 'DLSS5_MV_Hardware.fx' in 0.038000 s.
[13:13:51.736] mv-hw: VME ok, grade 80x45, divisor 2, bloco 8 (fonte 1280x720)
13:13:53:732  [Bridge] MV de hardware ativo: DLSS5_MV_Hardware ligado, Lumenite_Kernel desligado.
```

Os três shaders compilaram, o VME subiu, e a troca automática de provider funcionou sem
o usuário tocar em nada. A sonda do `dlss5-feed` — evidência independente, do outro lado
da cadeia inteira — leu **1,999 px para 2 px reais e 7,996 px para 8 px**, com 100 % dos
blocos. Erro de 0,05 %.

### O bug da sonda `mv-hw` (corrigido)

No mesmo quadro a nossa sonda imprimiu `mean |mv| 0.000 px, max 0.00 px, 99% nao-zero,
100% descartado`. O decodificador de half do `Probe()` aplicava `2^-24` duas vezes — o
fator pertence só ao ramo dos subnormais e sobrava no ramo dos números normais. Razão
medida entre o lido e o real: **5,960e-08**, exatamente 2⁻²⁴.

Isso explica os quatro números: a média e o máximo somem na precisão de impressão, o
`99% nao-zero` conta valores de 4e-7 que ainda são `> 0`, e a validade `1.0` vira
`5,96e-8`, que passa no teste `< 0.5` e faz **todo bloco válido contar como descartado**.

Corrigido e provado (`scratchpad/probe_test.cpp`, cenário idêntico ao do run):

| | mean px | max px | não-zero | descartado |
|---|---|---|---|---|
| verdade imposta | 8 | 8 | 100 % | 0 % |
| leitura independente | 7,998 | 8,00 | 100 % | 0 % |
| sonda `Probe()` | **7,998** | **8,00** | **100 %** | **0 %** |

O decodificador corrigido foi conferido contra os **65536 padrões de bits** possíveis
comparado com a referência: zero divergências. O bug era só de diagnóstico — o `Probe()`
não participa do caminho da imagem, e o DLSS recebeu MV correto o tempo todo.

### O alcance da CADEIA é metade do alcance do estimador

O run mediu, a 40 px: 46 % de blocos não-zero e o feed lendo 18,2 px. **Repare que
0,46 × 40 = 18,4** — não é subestimação, é zeramento binário: os blocos que sobrevivem
leem 40 px certo, os outros leem zero.

O estimador não é o culpado. Isolado, a 1280x720 com divisor 2, ele entrega **-40,000 px
com 100 % dos blocos**. A perda é toda de jusante.

`scratchpad/chain_test.cpp` simula na CPU o caminho depois do estimador — o `PS_Publish`
e o `ValidateTests` inteiro do `DLSS5_Feed.fx`, com os filtros de sampler corretos
(`ColorInput` POINT, `PrevLuma` LINEAR, `PrevMV` POINT). Ele reproduz o caso bom: a 8 px
dá 7,95 px e 100 % mantidos, contra os 7,996 do run.

Envelope efetivo, divisor 2, preset atual:

| deslocamento | mantidos | zerado por estática | por consistência | lido |
|---|---|---|---|---|
| 8 px | 100 % | 0 % | 0 % | 7,95 |
| 16 px | 98,2 % | 0,1 % | 1,3 % | 15,65 |
| 24 px | 94,2 % | 0,8 % | 3,9 % | 22,29 |
| 32 px | 88,8 % | 1,7 % | 7,0 % | 27,71 |
| 40 px | 83,2 % | 1,4 % | 11,9 % | 32,15 |
| 48 px | 78,8 % | 1,8 % | 14,9 % | 36,17 |
| 56 px | **47,7 %** | 1,5 % | **44,6 %** | 24,66 |

**Quem zera é a consistência (`MV_CONSISTENCY`), não a hipótese estática** — em toda a
faixa, por uma margem de 5 a 30 vezes. A suspeita do `nitidez` estava certa e a minha,
que apostava no `VALIDATE_STATIC`, estava errada.

### E mesmo assim: não mexer no `MV_CONSISTENCY`

Varrendo o knob a 40 px:

| `MV_CONSISTENCY` | 1,1 | 2 | 4 | 8 | 12 |
|---|---|---|---|---|---|
| mantidos | 83,2 % | 84,1 % | 86,5 % | 90,8 % | 93,3 % |
| lido | 32,15 | 32,31 | 32,76 | 33,67 | 34,30 |

Relaxar de 1,1 para **12** — onze vezes — recupera 10 pontos percentuais e 2 px de
leitura. Pouco, e não é de graça: a tolerância já é `MV_CONSISTENCY + 0,5 × |mv|`, ou
seja **21 px a 40 px de movimento**. Um vetor que difere do vizinho temporal em mais de
21 px é um vetor genuinamente ruim, e zerá-lo é o comportamento certo — MV errado é pior
que MV zero. E o `bad.z` já alimenta o `distrust`, então o DLSS **é avisado** e se apoia
no quadro atual ali. Isto é a degradação graciosa funcionando, não um defeito.

Mudar o divisor também não resolve: a 40 px, divisor 4 dá 85,1 % contra os 83,2 % do
divisor 2 — dentro do ruído. E divisor 1 dá **51,5 %**, mais uma confirmação contra ele.

**Conclusão operacional:** com divisor 2, a cadeia é praticamente sem perda até ~16 px
por quadro, perde ~10 % a 32 px, ~20 % a 40 px e desaba a partir de ~56 px. A 60 fps de
chegada isso é um giro de até ~30°/s sem perda e ~74°/s com 20 % de perda parcialmente
compensada pelo `distrust`.

**Ressalva de fidelidade:** o `chain_test` usa padrão de ruído em blocos 2x2, e o
`mvtarget` tem barras de 1 px. Conteúdo mais fino gera mais outliers no campo, então os
percentuais reais são piores que os simulados — o run mediu 46 % de sobreviventes a 40 px
onde a simulação dá 83 %. O que a simulação estabelece com segurança é o **mecanismo** e a
**ordem de importância** dos testes, não o valor absoluto.

### Corrida no aviso do add-on (corrigida)

O aviso `DLSS5_MV_Hardware.fx nao esta instalado` saía 37 ms **antes** de o shader
compilar, e 2 s depois a técnica era ligada normalmente. O ReShade compila os efeitos de
forma assíncrona e passa pelo `reshade_present` mais de uma vez; a primeira varredura
podia não ter a técnica ainda. Agora o add-on espera ~5 s de presents antes de concluir
qualquer coisa. Uma mensagem que mente no log custa mais do que vale.

## O "penhasco" dos 40 px era artefato de medição

Os runs 13:26/13:32 mediram, com o alvo `mvtarget`: 20 px lido exato com 100 % dos
blocos, **40 px lido 0,000 px com 0 %**, 80 px lido 4,4 px com 35 %. Parecia um limite de
alcance muito abaixo do medido isolado.

**Não é.** Duas coisas o denunciam.

**A curva não é monotônica.** Limite de alcance sempre é: se 40 px falha, 80 px falha
mais. Aqui 80 px vai melhor que 40. Zero absoluto no meio de uma curva que depois melhora
não é limite de nada.

**A reprodução offline acerta três dos quatro pontos.** `scratchpad/mvt_test.cpp` copia o
gerador de textura do `mvtarget` byte a byte (as quatro faixas: ruído, xadrez de 4 px,
barras de 32/16/8/4/3/2/1 px, gradiente), na resolução do run, com a grade do run, e mede
**a mesma região que a sonda do feed mede** — o centro 64x64:

| shift | offline, centro 64x64 | o run mediu |
|---|---|---|
| 8 px | 7,998 px · 100 % | 7,996 px · 100 % |
| 20 px | 20,000 px · 100 % | 20,000 px · 100 % |
| **40 px** | **40,000 px · 100 %** | **0,000 px · 0 %** |
| 80 px | 9,674 px · 80 % | 4,4 px · 35 % |

Bate em 8, bate em 20, bate no colapso de 80, e diverge só em 40. Um caminho quebrado não
acerta três pontos e erra um do meio.

### O mecanismo

`0,000 px com 0 % de blocos não-zero` é a **assinatura exata e única de dois quadros
idênticos** — medido: quadros iguais dão `(+0,000, +0,000)` e `0,0 %`. Nenhum outro modo
de falha produz isso; falha de alcance devolve vetor pequeno e errado (a 80 px o campo sai
com 9,7 px e 80 % de blocos não-zero), nunca zero absoluto.

E a sonda do feed é literalmente `MV probe (centre 64x64, frame %llu)` — **um quadro, o de
número 1200**, não uma média. O `dlss5forall.log` do run mostra `ritmo do WGC: media 20,32
max 747,06 ms` e `fps chegada=37,3`. Com `so-o-frame-mais-novo=1`, quando não chega quadro
novo o app reapresenta o mesmo conteúdo, e o VME corretamente reporta zero.

**Se o quadro 1200 daquela sessão calhou de ser uma repetição, a sonda lê exatamente
0,000/0 %.** É toda a explicação, e ela não exige defeito nenhum na cadeia.

### O que foi eliminado no caminho

Quatro hipóteses testadas e refutadas, todas com medição e não com argumento:

- **`rejectPx` disparando cedo demais.** Ligado e desligado dão resultado idêntico até
  56 px. A conta de unidades também confere por leitura: `raw` sai em quarter-pel da
  resolução de estimativa, `pxScale = 0,25 × divisor`, e o produto dá pixels de saída.
- **Fator de escala errado no teste de consistência do Feed.** `DLSS5_Feed.fx:500-501`:
  `diff` e `allow` multiplicam ambos por `BUFFER_SCREEN_SIZE`. Mesmas unidades.
- **Uniform contaminado por auto-save do preset.** O `FafaDLSS5.ini` não tem seção
  `[DLSS5_MV_Hardware.fx]`; os uniforms usam os defaults do shader.
- **Frequência do conteúdo.** Ruído de 1 px dá 85,5 % de vetores mantidos contra 83,2 %
  do padrão em blocos 2x2 — conteúdo mais fino é ligeiramente melhor, não pior.
- **Aliasing das barras periódicas.** 8 px e 40 px aliasam exatamente as mesmas faixas
  (larguras 1, 2, 4 e o xadrez), e 8 px funciona.

### A correção que isto exigiu, e ela era nossa

A sonda `mv-hw` tinha o mesmo defeito: media um quadro a cada 600. Um número que depende
de qual quadro calhou não serve para decidir `mv_divisor` em sessão real, que é para o que
ela existe.

Agora ela **amostra a cada 30 quadros e reporta a cada 20 amostras** — mesma cadência de
uma linha por 600 quadros, agregando 20 medições em vez de fotografar uma. A leitura de
volta usa duas staging alternadas e mapeia a que foi copiada na amostra anterior, já
concluída há dezenas de quadros, então não trava a fila.

Três colunas novas fazem o trabalho:

- **mediana**, que é imune aos quadros repetidos — a média não é;
- **`% campo zerado`**, a fração de amostras que vieram com o campo inteiro nulo. Com
  conteúdo em movimento é a taxa de repetição do WGC, e diz de imediato se vale confiar na
  média da linha. (Não distingue as duas causas possíveis: quadro repetido ou cena
  genuinamente parada.)
- **`% descartado`**, que já existia, agora agregado.

Verificado injetando repetições numa sequência de 200 quadros com deslocamento de 8 px
(`scratchpad/probe2_test.cpp`):

| repetidos injetados | média | mediana | `% campo zerado` |
|---|---|---|---|
| 0 % (controle) | 7,998 | **7,998** | **0 %** |
| 25 % | 5,999 | **7,998** | **25 %** |
| 50 % | 3,999 | **7,998** | **50 %** |

A média é puxada para baixo pelos zeros exatamente na proporção esperada, a mediana não se
move, e a coluna nova recupera a taxa injetada com precisão. **Com esta sonda, o falso
alarme dos 40 px teria aparecido como "mediana 40 px, N % campo zerado"** — dizendo na
mesma linha que o estimador está bem e que a captura é que está repetindo.

Uma peculiaridade benigna: a primeira janela de cada sessão mostra uma amostra a mais de
campo zerado e de descarte, porque o primeiro quadro não tem anterior e o estimador
publica um campo nulo marcado como não confiável. É uma amostra em vinte, e só na primeira
janela.

A Fase 1 do run também passa a usar **várias sondas por fase** em vez de uma. Se os 40 px
voltarem a dar zero em cinco sondas seguidas, com `% campo zerado` baixo, aí sim há algo
na cadeia.

## `MV_HW_EDGE_SHARP` volta ao padrão 0

O run 13:32 mediu a nitidez de saída com o uniform em 0 e em 1, conteúdo idêntico:

| | LapVar/RMS² | Tenengrad |
|---|---|---|
| `MV_HW_EDGE_SHARP=0` | 5,6327 | 119 811 |
| `MV_HW_EDGE_SHARP=1` | 4,3129 | 114 942 |
| | **−23,4 %** | −4,1 % |

N=3 com dispersão de 13 %, ou seja ~3σ: sugestivo, não fechado. Mas a assimetria de custo
manda no padrão de fábrica — deixar ligado algo que talvez piore 23 % custa mais que perder
um ganho numa métrica intermediária que não se traduziu.

**A explicação que reconcilia com a medição offline** (onde a amostragem pontual reduzia o
erro do campo em 10-30 %): um campo com **menos erro médio mas com degrau** na borda pode
servir pior ao DLSS que um campo suavizado. Eu media o erro do campo; o que decide é a
saída depois da rede.

E um erro de método meu, que vale registrar para não se repetir: calibrei o
`MV_HW_EDGE_PX` de 6 para 2 com base no erro do campo. Era a métrica errada para aquela
decisão, e afinar o limiar só fez o mecanismo agir mais.

Fica em **0** até haver N≥6 com o alvo corrigido. O slider continua lá para quem quiser
medir de novo.

## A zona ruim do VME entre 15 e 70 px — reproduzida, causa achada, meio consertada

Com o alvo corrigido (wrap toroidal de verdade, warp aperiódico, assinatura de ruído sobre
todas as faixas), a não-monotonicidade **reproduz no estimador cru**, sem validação nenhuma
no caminho. Divisor 2, bloco 8, campo inteiro:

| deslocamento | lido | erro |
|---|---|---|
| 8 px | 8,001 | 0 % |
| **15 px** | **16,616** | **+11 %** |
| 25 px | 25,003 | 0 % |
| 35 px | 33,016 | −6 % |
| 45 px | 40,408 | −10 % |
| 55 px | 45,742 | −17 % |
| 70 px | 54,799 | −22 % |
| 85 px | 18,908 (46 % zerado) | colapso |

O `+11 %` a 15 px é o sintoma que o `nitidez` isolou pelo `max` acima do real: **o
estimador encontra movimento maior do que existe.** Não é limite de alcance — é
ambiguidade de casamento de bloco.

### A causa: o downsample come o detalhe que desambigua

Varrendo o divisor no mesmo alvo, o padrão fica claro:

| deslocamento | divisor 1 | divisor 2 | divisor 4 |
|---|---|---|---|
| 15 px | **14,795** (−1 %) | 16,616 (+11 %) | 18,516 (**+23 %**) |
| 25 px | 23,932 (−4 %) | 25,003 | 26,708 (+7 %) |
| 35 px | 31,930 (−9 %) | 33,016 | 35,659 |
| 45 px | **colapsa** (17 % não-zero) | 40,408 | 43,890 |

**Quanto maior o divisor, pior a precisão em deslocamento pequeno e maior o alcance.** A
15 px com divisor 2 o deslocamento real vale 7,5 px na resolução de estimativa; o
downsample de 2x apaga as barras de 1 a 4 px e o xadrez, e o casamento passa a se apoiar só
na assinatura de ruído de célula 24 px — que sozinha admite mais de um mínimo local.

O divisor 1 não é a saída: ele é o mais preciso na faixa em que funciona, mas **colapsa a
45 px**, que é justamente a faixa em disputa.

### O conserto medido: bloco 16 em vez de 8

| deslocamento | divisor 2 + bloco 8 | divisor 2 + **bloco 16** |
|---|---|---|
| 15 px | 16,616 (**+11 %**) | **15,321 (+2 %)** |
| 25 px | 25,003 (0 %) | 24,769 (−1 %) |
| 35 px | 33,016 (−6 %) | 33,593 (−4 %) |
| 45 px | 40,408 (−10 %) | **42,247 (−6 %)** |
| 55 px | 45,742 (−17 %) | 49,364 (−10 %) |
| 70 px | 54,799 (−22 %) | 57,923 (−17 %) |

O bloco maior tem mais contexto para desambiguar, **elimina o overshoot** (o pior sintoma,
porque vetor maior que o real espalha o histórico numa direção errada) e reduz o erro pela
metade na faixa fraca. Custa densidade de grade — 1/32 do buffer em vez de 1/16 — e essa é
uma troca barata aqui: a medição de erro na borda de movimento já tinha mostrado que dobrar
a densidade não reduz o erro onde ele nasce (0,790 contra 0,817 px, empate).

**`mv_bloco=16` virou o padrão** para quem usar o caminho de hardware.

### E ainda assim não empata com o Kernel

O critério para manter o VME como padrão era ele empatar com o Kernel a 25 e 45 px. A 25 px
empata (−1 % contra ~0 %). **A 45 px não: −6 % contra −0,7 %, cerca de oito vezes pior.**
O conserto fechou a maior parte do buraco — de −29 % para −6 % — mas não o suficiente.

Por isso o padrão de fábrica passou a ser o Kernel. É a conclusão que a medição sustenta,
não a que eu esperava.

## Estado no fim do dia

### Instalado e rodando

| arquivo | quando | o que traz |
|---|---|---|
| `DLSS5ForAll.exe` | 13:35 | estimador VME, canal de validade, descarte em `40 × divisor` |
| `dlss5-bridge.addon64` | 13:35 | export `SetExternalMV`, troca automática de técnica, aviso com espera de 5 s |
| `DLSS5_Feed.fx` | 13:11 | `STATIC_DISTRUST` (P1) e o termo de confiança baixa em vetor zerado |
| `DLSS5_MV_Hardware.fx` | 13:46 | `MV_HW_EDGE_SHARP` de volta ao padrão 0 |

Knobs no `FafaDLSS5.ini`: `MV_CONSISTENCY=1.1`, `STATIC_DISTRUST=0.5`, `VALIDATE_LUMA=1`,
`VALIDATE_MV=1`, `VALIDATE_STATIC=1`, `VALIDATE_DEPTH=0`.

### Pronto, aguardando instalação

Um exe com a **sonda acumulada** (mediana e `% campo zerado` em vez de um instantâneo).
Muda **apenas a linha de diagnóstico no log** — nada do que o usuário vê. Espera o fim da
rodada de medição para não invalidá-la.

### Resolvido hoje

- **O caminho existe e funciona** ponta a ponta, medido com o app aberto: erro de 0,05 % a
  2 e 8 px, 100 % dos blocos, troca de provider automática.
- **Alcance caracterizado:** confiável até 40 px na resolução de estimativa, o que dá 80 px
  de saída com o divisor 2. Divisor 1 cobre só 40 px e não serve.
- **Divisor 2 é a escolha certa**, decidida por medição em duas frentes independentes: o
  divisor 1 não cobre o movimento típico, e a densidade extra dele não compra nada na
  borda de movimento (0,790 contra 0,817 px de erro — empate).
- **Três defeitos meus, achados e corrigidos:** o decodificador de half da sonda (fator
  2⁻²⁴), o use-after-free do SRV na troca de resolução, e o descarte silencioso que o P1
  não enxergava (resolvido com o canal de validade).
- **Dois falsos alarmes desmontados:** o "0 % de vetores" original era desktop parado, e o
  "penhasco dos 40 px" era instantâneo de um quadro caindo numa repetição do WGC.
- **`MV_HW_EDGE_SHARP` de volta a 0**, porque a métrica de saída contradisse a métrica
  intermediária em que eu o havia calibrado.

### Em aberto, em ordem de valor

1. **A imagem ficou melhor?** É a única pergunta que importa e a que continua sem resposta.
   Precisa da medição de nitidez de saída com o alvo corrigido e N≥6.
2. **`MV_HW_EDGE_SHARP` com N=6.** Hoje está em 0 por precaução, com 3σ sugerindo que 1 é
   pior. Vale a terceira célula (`sharp=1` com `EDGE_PX=6`) para separar "o mecanismo
   prejudica" de "eu calibrei o limiar errado".
3. **Por que o `Lumenite_Kernel` entrega quase zero.** A suspeita mais forte continua sendo
   os mips de `tCurrLuma`/`tPrevLuma` no `ZMSAD`. Exige `mv_hardware=0` para medir.
4. **`reset_every`**, a rede de segurança: se o rastro persistir, `reset_every=1` troca
   acumulação temporal por filtro espacial. Não deve ser ligado junto com o MV de
   hardware — os dois se anulam.

### O que NÃO vale a pena, e por quê

- **NVIDIA Optical Flow.** Melhor qualidade, mas sem header no PC e ABI por tentativa. O
  VME entrega o suficiente; reavaliar só se o rastro persistir depois de calibrada a
  validação.
- **Estimador próprio em HLSL.** Reescreveria o Kernel com o mesmo custo de fila 3D.
- **Forjar jitter.** Não recupera informação que não existe, e mente para a rede — pior
  que declarar zero.
- **Relaxar o `MV_CONSISTENCY`.** Medido: afrouxar onze vezes recupera 10 pontos
  percentuais e os vetores que ele zera são genuinamente ruins.
