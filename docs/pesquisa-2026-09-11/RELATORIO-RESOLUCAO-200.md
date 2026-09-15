# A rede neural acima de 100% da resolução de saída — implementado, medido, e não recomendado

Data: 2026-09-11. Pedido: *"escalar a resolução em até 200% — a da rede neural"*, isto é, a
rede trabalhando numa imagem **maior** que a tela e o resultado reduzido para o backbuffer
(supersampling da rede).

**O recurso está implementado e funciona.** `nr_escala` agora aceita 50..200 e a rede sobe
em qualquer degrau: medido rodando a **3192×1782** com a tela em 1596×891, 71 MB de
texturas, imagem correta.

**E a medição diz para não expor isso no painel.** Não porque custe caro — custa, 2,4× a
200% —, mas porque **nesse custo a rede faz MENOS, não mais**. O número que decide está na
§3: a 200% a rede altera a imagem 35% menos que a 100%. As duas frentes do relatório (o
custo e a ação da rede) apontam para o mesmo lado, e a §6 traz o que fazer no lugar.

---

## 1. Como a resolução é escolhida hoje, e onde o >100% cabe

A cadeia tem **duas** resoluções, e confundi-las é o começo de qualquer erro aqui:

| | quem decide | o que é |
|---|---|---|
| `work_resolution` (50..100) | `dlss5-feed.cfg` / painel | a resolução em que **a cadeia inteira** trabalha: SLOT_COLOR, depth, MV, o feature DLSS/DLAA. Abaixo de 100% tudo encolhe e o resultado sobe por blit (ou FSR 1) até o backbuffer. |
| `nr_escala` (50..200) | `dlss5-feed.cfg` | a resolução em que **só a rede neural** roda, dentro do NR próprio do feeder. Não toca em nada da cadeia. |

O `work_resolution` **não é o lugar** para o >100%, e a razão é estrutural. Levá-lo acima de
100 significaria criar o feature DLSS em modo Super Resolution com saída maior que o
backbuffer, e a super-resolução depende de três insumos que esta cadeia não tem:
profundidade real (aqui é um plano em zero), jitter de projeção (aqui é sintético) e
vetores de movimento do motor (aqui são estimados na imagem). O `ARQUITETURA.md` §8 já
mediu o resultado disso na direção mais fácil — `work_upscale=2` deu **14,46 de nitidez
contra 15,58** do blit simples, "o SR engrossa as maiúsculas em vez de reconstruí-las".

**O que foi confirmado, e não suposto:** o NGX *oferece* a razão 2,0. Medido pedindo ao
feed exatamente essa razão pelo caminho que já existe:

```
[feed] DLSS Ultra Quality at 1596x891: not offered (0x00000001, optimal 0x0)
[feed] DLSS Quality at 1596x891: optimal 1064x594, render range 798x446 .. 1596x891
[feed] work_upscale=2: DLSS Quality, 798x446 -> 1596x891, Halton(2,3) over 32 phases
[feed] feature ready: 798x446 -> 1596x891 DLSS Quality (synthetic jitter)
```

O preset Quality cobre render de 50% a 100% da saída — ou seja, o 2,0 está dentro da faixa,
e uma feature `1596×891 → 3192×1782` seria criada sem reclamação. **Viável, portanto; o que
falta não é a API, são os insumos.** Fica registrado como caminho futuro, caso um dia
apareça depth ou MV de verdade.

Então o >100% entrou onde o controle é nosso e o risco é nenhum: no **NR próprio**
(`nr_spike=1`), na mesma máquina que o modo reduzido usa. Ver `RELATORIO-RESIDUAL-NR.md`.

---

## 2. O desenho implementado

A mesma conta do modo reduzido, com o sinal da escala invertido:

```
    nr_in  = reamostra(saída do SuperSampling, escala)     ← a rede vê isto
    nr_out = rede(nr_in), nr_passes vezes
    saída  = nativo + (área(nr_out) − área(nr_in)) × força
```

- **Ampliando** (escala > 100), `reamostra` é bilinear — o filtro certo para esticar, e o
  único honesto: não há informação nova para inventar, e inventar seria dar à rede um
  detalhe que não veio da imagem.
- **Na volta**, `área` integra exatamente o retângulo de origem que corresponde a um pixel
  de destino: `n = ceil(passo)` amostras bilineares por eixo, espaçadas de `passo/n`. A 2:1
  são duas amostras a ±0,5 texel, e como cada bilinear já é a média de dois texels, o
  resultado é o box 2×2 exato.

**Um erro de kernel que custou uma bateria inteira, e fica escrito.** A primeira versão
parametrizava o filtro por "raio em texels inteiros": a 200% isso virava 3×3 amostras
espaçadas de **um** texel, e como cada amostra bilinear já cobre um texel, o kernel efetivo
saiu com o dobro da largura devida. O modo direto a 200% mediu **100 de laplaciano contra
236 da versão certa** — sete vezes menos detalhe que o caminho residual, e nada no log
denunciava. Filtro de reamostragem não se parametriza por raio; parametriza-se pelo passo.

### Os dois tetos, e por que existem

| teto | valor | motivo |
|---|---|---|
| lado da textura | 8192 px | o limite duro do D3D12 é 16384, mas o `Reserved18` **não tem contrato público** — há relato de outro projeto de que o feature "fica em silêncio" acima de 4K. 8192 é folgado para 200% de qualquer monitor desta máquina e fica longe do limite da API. |
| memória de vídeo | 1/5 do orçamento livre | são **quatro** texturas (duas da cascata e a entrada na escala da rede, mais a composição no tamanho de saída). `CreateCommittedResource` numa placa cheia **não falha limpo**: o driver pagina para a RAM e o que se vê é o jogo do usuário engasgando, sem uma linha de erro. Perguntar antes é o ponto. |

Medido: 200% de 1596×891 = **71 MB**; 200% de 3440×1440 seria ~317 MB. Os dois tetos
**caem para a resolução de saída** e escrevem o motivo, em vez de falhar.

```
[feed] nr: 3192x1782 needs 71 MB of textures (15002 MB of video-memory budget free)
[feed] nr: supersampled path ready -- network at 3192x1782 (200% of 1596x891), residual composite at 100% strength
[feed] nr: ready at 3192x1782 B8G8R8A8_UNORM, 1 pass per frame (supersampled + downfilter)
```

---

## 3. A medição que decide: nitidez **e** quanto a rede age

**Laplaciano sozinho não serve aqui, e quase produziu a conclusão errada.** No modo
residual a saída é `nativo + delta`: quanto **menor** o delta, mais a saída se parece com o
nativo e **maior** o laplaciano. Uma configuração em que a rede não fizesse nada ganharia a
tabela de nitidez. Por isso toda linha traz também o **MAE contra a cadeia sem rede** — o
quanto a rede realmente mexeu. As duas juntas é que decidem.

### Alvo sintético (1596×891, parado)

| configuração | laplaciano | MAE vs sem rede | leitura |
|---|---|---|---|
| sem rede (referência) | 834,1 | — | — |
| **rede a 100%, 1 passe** | 677,8 | **3,71** | o ponto de partida |
| rede a 125%, 1 passe | 745,0 | 2,89 | mais "nítido", −22% de ação |
| rede a 150%, 1 passe | 727,1 | 2,74 | −26% de ação |
| **rede a 200%, 1 passe** | 689,9 | **2,42** | **−35% de ação, por 2,4× o custo** |
| rede a 200%, modo direto | 235,4 | 3,40 | sem âncora nativa: a imagem desmonta |
| rede a 100%, 4 passes | 815,7 | 19,33 | — |
| rede a 200%, 4 passes | 364,6 | 21,89 | borra e ainda custa 2,7× |

### Segundo alvo sintético (`cena-do-teste-perfil.png`, parado)

> **CORREÇÃO (mesma sessão).** Esta seção dizia "cena real de jogo". **Está errado, e o erro
> é meu:** esse arquivo é outro alvo sintético — texto denso, linhas finas, retângulos e uma
> escada de cinza sobre ruído. **Nesta árvore não há captura de jogo real**, e nada aqui
> pode ser lido como "medido em jogo". O que a tabela sustenta é que um segundo alvo, com
> conteúdo de natureza diferente, repete o resultado do primeiro.

| configuração | laplaciano | MAE vs sem rede |
|---|---|---|
| sem rede (referência) | 3020,5 | — |
| **rede a 100%, 1 passe** | **2924,1** | **2,33** |
| rede a 125%, 1 passe | 2732,8 | 2,06 |
| rede a 150%, 1 passe | 2742,2 | 2,05 |
| rede a 200%, 1 passe | 2784,2 | **1,95** |
| rede a 200%, modo direto | 978,2 | 3,05 |
| **rede a 100%, 4 passes** | **3636,7** | **6,48** |
| rede a 200%, 4 passes | 3167,4 | 5,25 |

**No segundo alvo o supersampling perde nos dois eixos**: menos nítido que a rede a 100%
(2784 contra 2924) *e* agindo menos (1,95 contra 2,33). O pequeno ganho de laplaciano que
aparecia no alvo sintético não sobrevive a uma imagem de verdade.

### Por que, e a explicação é física

A fonte desta cadeia é uma **captura de tela na resolução da tela**. Ampliar não cria
informação: a rede passa a olhar pixels interpolados. E ela não é neutra à escala — numa
imagem ampliada 2×, as estruturas ficam duas vezes maiores em pixels, e a rede, que foi
treinada para uma escala de detalhe, encontra menos do que reconhecer. O MAE menor é
exatamente isso medido.

Supersampling de *render* funciona porque o rasterizador gera detalhe novo a 200%. Aqui não
há rasterizador: há um PNG do tamanho da tela. **O nome é o mesmo; o mecanismo, não.**

---

## 4. Custo de GPU

A/B intercalado e auto-validado (as três travas do `medir-residual.ps1`), alvo destravado,
fonte 1596×891, mediana das rodadas válidas:

| escala da rede | 1 passe | contra 100% | 4 passes | contra 100% |
|---|---|---|---|---|
| 100% | **193,6 fps** | — | **62,0 fps** | — |
| 125% | 156,0 fps | −19% | — | — |
| 150% | 119,6 fps | −38% | — | — |
| **200%** | **80,9 fps** | **−58%** | **23,0 fps** | **−63%** |

As amostras de 1 passe a 200% (80,2 · 80,9 · 81,0 · 79,4) e de 4 passes (22,9 · 23,0) são
notavelmente estáveis — é o par mais limpo desta bancada até agora. O quadro passa de
5,17 ms para 12,36 ms a 1 passe: **+7,2 ms** para a rede agir 35% menos.

---

## 5. O que fica implementado, e como está de fábrica

| chave | valores | padrão | o que muda |
|---|---|---|---|
| `nr_escala` | **50..200** (era 50..90) | 65 | <100 reduz a rede, >100 amplia. Só age com `nr_reduzida=1`. |

`nr_reduzida=0` de fábrica: **nada disto liga sozinho**. Os tetos de lado e de memória, o
fallback para a resolução de saída e a linha de log que diz o sentido (`reduced-resolution`
/ `supersampled`) valem para os dois lados.

O caminho >100% **fica no código**, e isso é deliberado: ele é correto, é barato de manter
(é a mesma máquina do modo reduzido, com o sinal trocado), e a resposta "não vale a pena"
só tem valor enquanto puder ser refeita. `tests\medir-supersample.ps1` refaz a tabela
inteira sozinho.

---

## 6. Recomendação para o painel

**Não expor degraus acima de 100%.** Nenhum deles: nem 125, nem 150, nem 200. O segundo alvo
mostra que todos pioram a nitidez *e* reduzem a ação da rede, e o mais barato deles já custa
19% dos quadros.

Um chip que custa fps e entrega menos efeito é pior que chip nenhum — e a pessoa que o
ligasse veria a taxa cair e concluiria, com razão, que o programa piorou.

**O que oferecer no lugar, e este é o achado útil da medição:** o que o pedido quer — mais
qualidade da rede — está em **mais passes na resolução nativa**, não em mais pixels. Na cena
real, `100% com 4 passes` deu o **maior laplaciano de toda a tabela (3636,7)** e a **maior
ação (6,48)**, a 62 fps. É o dobro do efeito da rede a 200% por um terço do custo dela.

Isso não pede nada novo no painel: o controle de passes 1..10 já existe.

---

## 7. O que estes números NÃO sustentam

- **A GPU estava compartilhada** (o usuário jogando, outro agente medindo). Os absolutos não
  valem fora daqui; a comparação A/B intercalada vale, e as rodadas em que o feed não leu a
  configuração pedida foram descartadas pelo script.
- **As duas cenas estão paradas.** Nitidez de cena parada não diz nada sobre arrasto ou
  cintilação em movimento, e a rede tem componente temporal. Um supersampling poderia, em
  tese, estabilizar o movimento sem melhorar o quadro parado — não foi medido, e é a única
  porta que este relatório deixa aberta.
- **Ninguém jogou com isto ligado.**
- A leitura do §3 ("a rede age menos em escala maior") é uma inferência a partir do MAE, não
  uma medida de dentro da rede. Ela é consistente nas duas cenas e nos quatro degraus, mas
  o `Reserved18` não tem contrato público e ninguém pode afirmar o que ele faz por dentro.

---

## 8. Arquivos

| arquivo | o que mudou |
|---|---|
| `amd\feeder-amd\src-feeder\src\dlss5-feed.cpp` | `nr_escala` até 200, reamostragem nos dois sentidos, filtro de área por passo, tetos de lado e de VRAM |
| `amd\tests\medir-supersample.ps1` (novo) | a bateria inteira: nitidez, quanto a rede alterou, e fps A/B |
| `amd\tests\nitidez_png.exe` | (já existia, do relatório do residual) |

Para a `bin\`: recompilar `dlss5-feed.addon64` de `amd\feeder-amd\build\`. Nada mais muda, e
**nada liga sozinho**.
