# "O filtro do DLSS está muito claro no The Witcher 3"

Diagnóstico do estouro de brilho, feito **sem rodar o app**: leitura do HLSL embutido no
`renodx-dlss5.addon64`, do `ReShade.log`, do `dlss5-feed.log`, do `ReShade.ini`, do
`screen_capture.h`, do `main.cpp` e dos `.fx` instalados. Complementado com a documentação
pública do add-on RenoDX DLSS5.

**Resumo em uma linha:** os quatro sliders neurais do RenoDX estão travados no **máximo**
(`2.0`), enquanto os padrões são `1 / 1 / 1 / -1`. Corrigir isso é a causa raiz. O segundo
suspeito, independente do filtro, é o **HDR do Windows** — com ele ligado o WGC entrega uma
imagem já lavada, e nenhum slider de NR resolve.

---

## 1. Primeiro, a matemática — o que o codec faz de fato em SDR

Extraí de novo o HLSL do `renodx-dlss5.addon64` (3616 strings ASCII; o shader está em texto
puro, o add-on compila com `D3DCompile` em tempo de execução). Confirmo e **corrijo em um
ponto** o que `docs\cor-hdr-e-filtros.md` §7.1 diz.

O ramo SDR do estágio de decode, literal:

```hlsl
} else {                                  // HdrMode == 0
  original = source.rgb;                  // saída do DLSS do jogo (pre-NR)
  proxy    = Proxy.Load(int3(proxy_pixel, 0)).rgb;
  neural   = Neural[pixel].rgb;
  upgraded = lerp(original, neural, TransferStrength);
}
const float original_y = Luminance(original);
const float upgraded_y = Luminance(upgraded);
const float ratio = original_y == 0.0 ? 1.0 : clamp(upgraded_y / original_y, 0.0, 4.0);
const float3 luminance_only = original * ratio;
const float3 result = lerp(luminance_only, upgraded, ColorStrength);
```

Três consequências que decidem tudo o que vem abaixo:

1. **`UpgradeToneMap` nem é chamado em SDR.** Ele só existe nos ramos `HdrMode == 1` e
   `HdrMode == 2`. Em SDR o cálculo é o `lerp` simples acima. (`cor-hdr-e-filtros.md` §7.1
   dizia que ele "colapsa em `lerp`" — o efeito é o mesmo, mas o codec nem entra na função.)

2. **Com `ColorStrength = 1`, o `ratio` é código morto.** `lerp(a, b, 1)` devolve `b`. Logo
   `result == upgraded`. E com `TransferStrength = 1`, `upgraded == neural`.
   **A saída atual é 100% a saída crua da rede neural**, sem uma fração sequer do frame
   original para segurá-la. Não existe nada no codec atenuando brilho hoje.

3. **`ColorStrength` NÃO atenua brilho.** Isso é contra-intuitivo e vale ser explícito: em
   `ColorStrength = 0` o resultado é `original * (Y(neural)/Y(original))`, cuja luminância é
   **exatamente `Y(neural)`**. Ou seja: `ColorStrength` decide de quem vêm matiz e croma, e
   entrega a luminância da rede em qualquer valor. Se a rede está clareando, baixar
   `ColorStrength` não muda o brilho — só troca a cor pela do original.
   **O único dial do codec que dilui brilho é `TransferStrength`.**

### `NRPaperWhiteScale=16` — descartado, com a linha do código

Hipótese 3 do enunciado está descartada. `PaperWhiteScale` aparece em exatamente quatro
lugares no shader, todos dentro de `if (HdrMode == 1)`:

```hlsl
// encode:  proxy /= PaperWhiteScale;
// decode:  original = max(source.rgb, 0.0) / PaperWhiteScale;
// saída:   Output[pixel] = float4(result * PaperWhiteScale, source.a);
```

No ramo `else` (SDR) ele não é lido. E o `dlss5-feed.log` confirma o ramo:
`feature ready: ... flags=66 (SDR MVLowRes AutoExposure)` — o bit `IsHDR` (0x1) está
**desligado**, então o NGX não marca HDR e `HdrMode` é 0. `paper_white=16.000000` no log é
inócuo hoje.

Ressalva: se alguém forçar `hdr=1` no `dlss5-feed.cfg`, o `16` passa a valer. Aí o
soft-clip do proxy (joelho em `0.75`) cairia em 12× o paper white, ou seja, praticamente
não haveria soft-clip. Não force `hdr=1` sem baixar esse valor.

### O clamp em 4 — descartado

Hipótese 2 do enunciado também está descartada pelo mesmo motivo do item 2 acima: com
`ColorStrength = 1` o termo `original * ratio` é multiplicado por zero. O clamp de 4 só
começa a existir conforme `ColorStrength` cai — e ele é um *teto*, não uma amplificação:
protege contra estouro, não o causa.

---

## 2. Causa provável nº 1 — os quatro sliders neurais estão no máximo

Do `ReShade.ini`, seção `[RenoDX.DLSS5]`, comparado com os padrões documentados do add-on:

| Chave | Rótulo na overlay | Faixa | Padrão | **Está em** |
|---|---|---|---|---|
| `NRIntensity` | NR Intensity | 0.00–2.00 | **1.00** | **2.00 — máximo** |
| `NRLocalTone` | Local Tone Strength | 0.00–2.00 | **1.00** | **2.00 — máximo** |
| `NRLocalStructure` | Local Structure Strength | 0.00–2.00 | **1.00** | **2.00 — máximo** |
| `NRSkinStructure` | Skin Structure Strength | −1.00–2.00 | **−1.00** (padrão nativo) | **2.00 — máximo** |

Os quatro. Todos no topo do curso. Nenhum script deste projeto escreve essas chaves
(`grep` em `setup-dlss5.ps1`, `setup-dlss5.auto.ps1` e nos presets 01/02/03 não retorna
nada) — foram postos à mão na overlay, e `AutoSavePreset=1` gravou.

**Mecanismo.** As quatro chaves viram parâmetros NGX passados direto ao
`nvngx_dlssnr.dll` — as strings `DLSSNR.Intensity`, `DLSSNR.LocalToneStrength`,
`DLSSNR.LocalStructureStrength`, `DLSSNR.SkinStructureStrength` estão no binário ao lado
de `DLSSNR.Color`, `DLSSNR.Output`, `DLSSNR.MVec`. O add-on não interpreta esses valores:
ele os repassa, e o brilho do frame passa a ser o que a rede decidir.

Dentro desse conjunto, o culpado direto de "claro demais" é **`NRLocalTone`**. "Local
Tone Strength" é a força do operador de *tone mapping local* da rede — o que ele faz é
redistribuir exposição por região, levantando sombras e meios-tons para revelar detalhe.
Em `2.0` (dobro do padrão) o resultado é exatamente a descrição do sintoma: sombras
levantadas, contraste global achatado, imagem lavada. `NRIntensity` em `2.0` multiplica a
dose de todo o efeito por cima disso.

E, pelo item 1.2 da seção anterior, **não há nada segurando**: `TransferStrength=1` +
`ColorStrength=1` entregam essa saída inteira, sem mistura com o frame original.

O Witcher 3 é o pior caso possível para isso. É um jogo de muita cena escura (interiores à
luz de vela, noite, floresta fechada) — precisamente onde um tone map local agressivo tem
mais margem para levantar e mais visibilidade quando levanta.

### Teste que confirma ou descarta, em segundos

**F5 — o par de screenshots.** O add-on grava um par do **mesmo frame**, um marcado
`[pre-NR]` e outro `[NR on]` (strings `NR screenshot pair written:`, ` [NR on]`,
` [pre-NR]` no binário; os arquivos vão para a pasta do app). É o A/B perfeito.

1. Jogue com o app capturando o Witcher 3, com o NR ligado (F6).
2. Aperte **F5** durante o gameplay — precisa ter uma avaliação DLSS ativa; se não houver,
   o log avisa `NR screenshot disarmed: no DLSS evaluation ran for about 10 seconds`.
3. Abra os dois PNGs lado a lado.

- **`[pre-NR]` com brilho normal e `[NR on]` lavado** → é o NR. Aplique a seção 5.
- **Os dois igualmente lavados** → o filtro é inocente, o problema é a captura. Vá à seção 3.

**F6 — liga/desliga.** Mais grosseiro mas instantâneo: alternar o NR e ver se o brilho
acompanha. Serve para a mesma bifurcação.

---

## 3. Causa provável nº 2 — HDR do Windows, e aí não é o filtro

Esta hipótese é independente da anterior e pode estar somada a ela.

A cadeia de captura é 8 bits SDR de ponta a ponta, e verifiquei que não há **nenhum**
tratamento de espaço de cor em lugar nenhum:

| Etapa | Formato | Onde |
|---|---|---|
| frame pool do WGC | `DirectXPixelFormat_B8G8R8A8UIntNormalized` | `screen_capture.h:427` e `:491` |
| upload da câmera / swapchain | `DXGI_FORMAT_B8G8R8A8_UNORM` | `main.cpp`, confirmado no `ReShade.log` |

`grep -i "ColorSpace|HDR|Float"` em `screen_capture.h` retorna só as duas linhas do pool.
`grep -i "SRGB|gamma|pow\(|LinearTo|ToLinear"` em `main.cpp` **não retorna nada** — o app
não converte cor, é cópia crua. Ou seja: o app não clareia nada por conta própria, e
também não tem como recuperar HDR.

**O que acontece com o HDR do Windows ligado.** Com o frame pool em BGRA8, o Windows
Graphics Capture não entrega o buffer HDR da janela: o compositor **converte para SDR** e
entrega o resultado em 8 bits. Essa conversão é o tone map genérico do sistema, sem
calibração de conteúdo, e o resultado clássico é exatamente cinza, chapado e dessaturado —
o "washed out" de screenshot de HDR. A informação se perde **antes** do ReShade ver o
frame; nada na cadeia LumeniteFX → DLSS → NR → Grade pode desfazer isso, só compensar.

E há um agravante específico do Witcher 3: além do HDR nativo da versão next-gen, o
**Auto HDR** do Windows 11 pode ligar HDR num jogo que você configurou como SDR, sem você
pedir. Vale checar os dois.

### Testes, em segundos

1. **Win + Alt + B** liga/desliga o HDR do Windows na hora. Com o app capturando: se a
   imagem "endireitar" no instante em que o HDR desliga, a causa é a captura, ponto final.
2. `Configurações → Sistema → Tela → HDR`: confirme **Usar HDR** desligado **e**
   **Auto HDR** desligado.
3. No jogo: `Vídeo → HDR` desligado. (Se o HDR do jogo estiver ligado com o do Windows
   desligado, o próprio jogo já sai lavado na tela — nem chega a ser problema de captura.)
4. Controle cruzado: capture uma janela comprovadamente SDR (um navegador, o Bloco de
   Notas). Se ela também sair clara, o problema não é o jogo.

**O que fazer se for isso.** Jogar em SDR enquanto usa o app é a resposta correta e sem
perda. A alternativa — capturar HDR de verdade — exige mudar o frame pool para
`R16G16B16A16Float` e declarar scRGB no swapchain; o diff literal já está escrito em
`docs\cor-hdr-e-filtros.md` §8 e **não foi aplicado**. Paliativo imediato:
`preset-04-brilho-contido.ini` (seção 6).

---

## 4. Causa nº 3 (baixa) — a flag AutoExposure do DLSS

`dlss5-feed.log`: `flags=66 (SDR MVLowRes AutoExposure)`. `66 = 0x42 = 0x40 | 0x02` =
`AutoExposure | MVLowRes`. Vem de `flags=-1` (auto) no `dlss5-feed.cfg`.

Com `AutoExposure`, o DLSS calcula a própria exposição a partir do frame em vez de receber
uma textura de exposição do jogo. É a escolha certa aqui (não temos como fornecer uma), e
normalmente não desloca o brilho de saída. Mencionado por completude, não por suspeita.

**Teste:** trocar `flags=-1` por `flags=2` no `dlss5-feed.cfg` (só `MVLowRes`) e reabrir o
app. Se o brilho não mudar — o esperado — descarte e **volte para `-1`**. Faça este teste
por último, depois dos dois de cima.

---

## 5. A correção na raiz — linhas para o `ReShade.ini`

Não editei o `ReShade.ini`. Aplique estas linhas na seção `[RenoDX.DLSS5]`, **com o app
fechado** (`AutoSavePreset=1` reescreve o arquivo ao sair).

### Passo 1 — devolver os quatro sliders ao padrão

Quatro linhas **mudam**:

```ini
NRIntensity=1
NRLocalTone=1
NRLocalStructure=1
NRSkinStructure=-1
```

`NRSkinStructure=-1` é o "padrão nativo": significa *não sobrescrever*, deixar a rede usar
o valor dela. Se por algum motivo o `-1` não pegar, use `1`.

Só isso já deve resolver, e é a única mudança que ataca a causa. Nenhuma linha nova
precisa ser adicionada — as quatro já existem no arquivo.

### Passo 2 — se ainda estiver claro, aí sim dosar o NR

Uma linha **muda** (ela já existe no arquivo; o `docs\cor-hdr-e-filtros.md` §7.1 dizia que
estava ausente — não está mais, o `AutoSavePreset` já a gravou):

```ini
NRTransferStrength=0.8
```

Pela matemática da seção 1, isso faz `result = lerp(original, neural, 0.8)`: 80% de rede,
20% do frame original, com o brilho interpolando linearmente entre os dois. **É o único
dial do codec que dilui brilho.** `0.7` se `0.8` não bastar; abaixo de `0.6` o NR começa a
não valer o custo.

**Deixe `NRColorStrength=1`.** Baixá-lo não escurece nada (seção 1, item 3) — só troca a
cor da rede pela cor original mantendo a mesma luminância. É um knob de cor, não de brilho.

**Deixe `NRPaperWhiteScale=16`.** Inerte em SDR, comprovado na seção 1.

### Bloco final, para conferência

```ini
[RenoDX.DLSS5]
EnableHooks=2
NeuralUplift=1
NRAutoMask=0
NRColorStrength=1
NREnableUpscaling=1
NRIntensity=1
NRLocalStructure=1
NRLocalTone=1
NRPaperWhiteScale=16
NRPreset=0
NRSkinStructure=-1
NRStyle=0
NRTransferStrength=1
NRUICorrection=0
```

Duas observações sobre esse bloco:

- **`NeuralUplift`** é a chave de ligar/desligar o NR (o `ReShade.log` imprime
  `enabled=OFF` com ela em `0`; é o mesmo estado que o F6 alterna). Está em `0` no arquivo
  atual, o que significa que na última sessão registrada o NR nunca chegou a ligar. Ponha
  `1` para ele já subir ligado.
- **`NRStyle=0`** é *Default*. Se depois de tudo a imagem ainda parecer estilizada demais,
  `NRStyle=1` (*Natural*) é o estilo menos interventivo — mas mexa nisso só depois de
  fechar o brilho.

---

## 6. A maquiagem — `preset-04-brilho-contido.ini`

Criado ao lado dos existentes; **os presets 01, 02 e 03 não foram tocados**.

Base: `preset-01` (Kernel → Feed, `IMAGE_SPACE=1`, validação ajustada) + `DLSS5_Grade`
abaixo do Feed, configurado para conter uma imagem lavada.

```ini
[DLSS5_Grade.fx]
Exposure=0.000000
Lift=-0.008000,-0.008000,-0.008000
Gamma=0.920000,0.920000,0.920000
Gain=1.000000,1.000000,1.000000
SCurve=0.120000
Contrast=1.060000
ContrastPivot=0.420000
Vibrance=0.120000
Saturation=0.040000
LUT_SELECT=0
```

Escolhas, e por quê:

- **`Gamma=0.92`** é o motor principal. No shader o estágio é `c = pow(c, 1/Gamma)`, então
  `0.92` dá expoente `1.087`: escurece os meios-tons e **deixa preto e branco parados**.
  É o jeito certo de tirar brilho sem clipar nada. `Exposure` fica em `0` de propósito —
  além de ser a ferramenta errada aqui, com ele em zero o estágio 1 do shader é pulado
  inteiro (ida e volta a luz linear economizada).
- **`Lift=-0.008`** devolve o nível de preto. Uma captura lavada tem preto em ~0.15 quando
  deveria estar em ~0.05; o estágio faz `c' = Lift + c*(Gain - Lift)`, ou seja subtrai o
  pedestal e reescala para o branco continuar em 1.0.
- **`Contrast=1.06` com `ContrastPivot=0.42`** — pivô abaixo de 0.5 porque a imagem lavada
  tem a luma média empurrada para cima; ancorar em 0.42 puxa mais o que está embaixo.
- **`SCurve=0.12`** dá um pouco de punch nas altas sem tocar em matiz (o estágio age só na
  luma e recompõe o RGB pela razão).
- **`Vibrance=0.12`, `Saturation=0.04`** — o tone map SDR do compositor dessatura junto com
  o achatamento; a vibrance repõe cor no que ficou fosco sem estourar pele e céu.

Curva resultante, medida na conta do shader:

| entrada | saída | efeito |
|---|---|---|
| 0.15 | 0.093 | sombras recuperadas (−38%) |
| 0.50 | 0.467 | meios-tons ~0.10 stop mais escuros |
| 0.85 | 0.874 | altas levemente realçadas |
| 1.00 | 1.000 | branco intacto |

Se ficar escuro demais: `Gamma=0.96`, `SCurve=0.06`. Se ainda estiver lavado: `Gamma=0.88`,
`Lift=-0.014`, `Contrast=1.10`.

**Sendo honesto sobre o que isso é.** É compensação a jusante, e tem dois custos reais:

1. Se o brilho vier do NR (seção 2), o grade escurece **depois** que a rede já jogou fora a
   separação entre sombra e meio-tom. Recuperar contraste global não recupera o detalhe que
   o tone map local achatou. A correção certa é a seção 5.
2. Se o brilho vier do HDR (seção 3), o grade está esticando 8 bits já tone-mapados — vai
   aparecer banding em céu e gradiente. A correção certa é jogar em SDR.

O preset-04 é para quando nenhuma das duas está disponível na hora.

---

## 7. Ordem de execução recomendada

1. **F5** durante o gameplay → compare `[pre-NR]` e `[NR on]`. Bifurca todo o diagnóstico.
2. Se o `[NR on]` for o claro: aplique o **Passo 1** da seção 5 (os quatro sliders para o
   padrão). Reteste com F5.
3. Se ainda claro: **Passo 2**, `NRTransferStrength=0.8`.
4. Se os **dois** screenshots forem claros: **Win + Alt + B**, e confira Auto HDR. Jogue em
   SDR.
5. Só se nada acima resolver: carregue `preset-04-brilho-contido.ini`.
6. Por último e por eliminação: `flags=2` no `dlss5-feed.cfg` (e volte para `-1`).

---

## 8. O que não foi verificado

- **Nada foi executado.** Sem app, sem jogo, sem medição — o PC está em uso e há histórico
  de tela piscando. Todo o diagnóstico é leitura de código, de log e das strings dos
  binários.
- **Os padrões dos sliders do NR (1 / 1 / 1 / −1) não vêm do binário.** Extraí do
  `renodx-dlss5.addon64` os *rótulos* ("NR Intensity", "Local Tone Strength", …) e as
  *chaves* (`NRIntensity`, `DLSSNR.Intensity`, …), mas os padrões e as faixas são
  constantes de código, não strings. Os valores da tabela da seção 2 vêm de duas fontes
  públicas independentes do add-on RenoDX DLSS5, que concordam entre si (0.00–2.00 com
  padrão 1.00 para Intensity / Local Tone / Local Structure; −1.00 como padrão nativo de
  Skin Structure). Se você quiser confirmação de primeira mão em cinco segundos: abra a
  overlay em *Add-ons → DLSS 5 Neural Rendering* — no ReShade um slider no fim do curso é
  visível de imediato, e é isso que os quatro devem estar mostrando.
- **O que exatamente "Local Tone Strength" faz dentro do `nvngx_dlssnr.dll` é caixa-preta.**
  A atribuição do sintoma a ele é inferência a partir do nome e do parâmetro NGX; o que
  está *provado* pelo código é que os quatro valores são repassados crus à rede e que o
  codec não atenua nada em `TransferStrength=1` + `ColorStrength=1`.
- **A curva do preset-04 foi calculada, não vista.** Reproduzi à mão a aritmética exata do
  `PS_DLSS5_Grade` (lift → gamma → S-curve → pivô → retarget de luma); os números da tabela
  são dessa conta. A aparência na tela depende da cena.
- **`DLSS5_Grade.fx` compila.** Não é dedução: `ReShade.log` 20:36:40 registra
  `Successfully compiled '...\DLSS5_Grade.fx' in 0.004000 s`.

## 9. Arquivos

| Arquivo | O quê |
|---|---|
| `docs\brilho-witcher3.md` | este documento |
| `preset-04-brilho-contido.ini` | preset novo, grade compensando o brilho (seção 6) |

Nada mais foi criado ou modificado. `main.cpp`, `screen_capture.h`, `ReShade.ini`,
`FafaDLSS5.ini`, `dlss5-feed.cfg` e os presets 01/02/03 continuam intactos.
