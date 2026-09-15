# Caminho AMD: o que existe, onde está o peso, e o que fazer com isso

Data: 2026-09-11. Convenção do projeto: **[FATO]** = lido/medido aqui, com a fonte citada;
**[TERCEIRO]** = número publicado por outra pessoa, não conferido nesta bancada;
**[INFERÊNCIA]** = dedução minha, ainda não provada.

**Como ler este documento:** as seções 1 a 7 foram escritas **antes** de a pasta do outro PC
chegar, só com o que existe aqui. A **Fase 2 (seções 8 e 9)**, no fim, traz os arquivos e
logs de uma máquina com RX 9060 XT real — e **corrige três conclusões das seções anteriores**.
Onde houver conflito, vale a Fase 2. As correções estão marcadas onde acontecem.

**Aviso que vale para as seções 1 a 7:** nenhuma linha delas foi medida em hardware AMD.
**[FATO]** Este PC não tem Radeon discreta — `Get-CimInstance
Win32_VideoController` devolve `NVIDIA GeForce RTX 4080`, `AMD Radeon(TM) Graphics` (a
integrada do Ryzen) e três monitores virtuais. O mod DLSS-NR-on-AMD não roda na integrada:
as strings do próprio binário dizem `this build contains code for RDNA4 (gfx12) and RDNA3
(gfx11) only`. A Fase 2 pedida (medir) está, portanto, entregue como **bancada pronta**
(`amd\tests\medir-amd.ps1`) e não como tabela de números. Quem escrever números ali sem
rodar o script estará inventando.

---

## 1. Como o app roda em placa AMD hoje

### 1.1 São três camadas, e só a do meio é nossa

| # | camada | quem é | onde está |
|---|---|---|---|
| 1 | captura + cadeia ReShade | o app, igual ao caminho NVIDIA | `bin-amd\DLSS5ForAll.exe`, `dxgi.dll` |
| 2 | **portador FSR** | nossa variante do DLSS5-Feeder | `bin-amd\dlss5-feed.addon64` + `amd_fidelityfx_dx12.dll` |
| 3 | **a rede neural** | mod de terceiro **DLSS-NR-on-AMD**, de danielblnc | `version.dll` + `dlssnr_on_amd_weights.bin`, instalados pelo usuário |

**[FATO]** A camada 2 é o truque do projeto inteiro, e está documentada em
`docs\pesquisa-2026-09-05\RELATORIO-FEEDER-AMD.md`: numa Radeon o NGX da NVIDIA não
inicializa, então o Feeder carrega a `amd_fidelityfx_dx12.dll` (FidelityFX SDK 1.1.4, MIT),
cria um contexto FSR 3.1 no seu device D3D12 privado e despacha um `ffxDispatch` por quadro
com cor, profundidade e vetores de movimento. **[FATO]** O mod não implementa NGX em ponto
algum: ele intercepta `ffxFsr3UpscalerContextDispatch` (string literal do binário), pega
essas texturas, roda a rede reescrita em kernels HIP e escreve o resultado de volta. Ou
seja, **o FSR é o veículo; a rede pega carona nele.**

**[FATO]** Um só binário serve as duas placas: `ngx_forward.cpp` repassa ao `_nvngx.dll` do
driver quando ele existe, e devolve `FeatureNotSupported` quando não existe. O RenoDX fica
de fora do perfil AMD de propósito — sem NGX ele é inerte e o log dele confunde o app.

### 1.2 O que está empacotado, e a defasagem que ninguém tinha notado

**[FATO]** Comparando `DLSS5ForAll-AMD\bin-amd\` com a produção NVIDIA `bin\`, hoje:

| arquivo | bin-amd | bin (NVIDIA) |
|---|---|---|
| `DLSS5ForAll.exe` | 2.452.553 B · 06/09 · **v1.1.0.0** | 2.928.431 B · 11/09 · **v1.2.0.0** |
| `dlss5-feed.addon64` | 961.584 B · 05/09 | 994.756 B · 11/09 |
| `dlss5-bridge.addon64` | 453.569 B · 05/09 | 1.019.764 B · 11/09 |

**O caminho AMD parou em 06/09 e a produção andou cinco dias.** A consequência concreta,
medida pelas chaves que cada binário reconhece (`grep` nas strings dos dois `.addon64`):

- o addon do `bin-amd` entende `work_upscale`, `work_sharpness`, `buffer_home`,
  `gpu_timeout_ms` e as quatro chaves `amd_*`;
- o addon de hoje entende **tudo isso mais** `dred`, `hud_restore`, `nr_spike`, `nr_passes`,
  `nr_reduzida`, `nr_escala`, `nr_residual`.

Ou seja, o perfil AMD não tem o DRED (breadcrumbs de GPU), nem a falha dura sem retry, nem a
passagem segura falante — as três coisas do `RELATORIO-RESIDUAL-NR.md` §7 que existem
justamente para quando algo dá errado em máquina alheia, que é exatamente a situação de uma
Radeon.

**[FATO]** O `bin-amd\dlss5-feed.cfg` também ficou para trás do `perfil-amd\dlss5-feed.cfg`
da fonte: falta `work_upscale`, `work_sharpness`, `gpu_timeout_ms` e `stall_log_ms`, e ele
traz `warmup_rebuild=180` e `create_delay=60` contra `0` e `59` da produção.

### 1.3 O que ficou "funcional, porém pesado"

A sessão em que o usuário conseguiu isso rodou **em outro PC** e a transcrição não existe
aqui (confirmado pelo próprio usuário e pela lista de sessões: "Setup AMD driver v0.2.18
configuration and verification" aparece como sessão remota, offline). O que dá para afirmar
com fonte:

**[FATO]** O que o pacote carrega hoje é o instalador do mod **v0.2.11**
(`bin-amd\dlssnr_on_amd_setup.exe`, 4.737.763 B, de 05/09). As strings dele confirmam a
limitação: os alvos compilados são `gfx1100/1101/1102/1201` — **`gfx1200`, que é a RX 9060
XT, não está lá**, e a mensagem do mod para esse caso é `UNSUPPORTED GPU ARCHITECTURE`.

**[FATO]** A página de releases do mod, lida hoje, mostra que o mundo andou:

| versão | data | o que traz |
|---|---|---|
| 0.2.12 | 05/09 | *"6% performance improvement over v0.2.11"* |
| 0.2.13 | 06/09 | *"2% performance improvement over v0.2.12"* |
| **0.2.14** | 06/09 | **suporte à RX 9060 XT** (o gfx1200 que faltava) |
| 0.2.15 | 07/09 | corrige captura dupla (reduz cintilação); corrige travamento em RE Requiem |
| 0.2.16 | 08/09 | Spider-Man Remastered; override do nome da DLL proxy |
| 0.2.17 | 09/09 | exposição HDR; travamento em multi-GPU; engasgo com resolução dinâmica |
| **0.2.18** | 11/09 | latência e engasgo menores, ritmo de quadros, geração de quadros em GTA V |

Isto explica a frase do usuário por inteiro: **ele saiu da v0.2.11 (que nem liga na 9060 XT)
para a v0.2.18, e por isso "ficou funcional".** O "pesado" é o estado normal do mod nesta
fase, não um defeito da instalação dele.

**[TERCEIRO]** Os números que existem sobre esse peso, todos de fora desta bancada:

| fonte | número |
|---|---|
| autor do mod, sobre a RX 9060 XT | *"~60 ms por job na 9060 XT (15 a 20 fps em 1080p) é o esperado, metade da 9070 XT"* |
| README do mod | *"31 fps na 9070 XT"* |
| issue 45 do repositório | teto de ~30 fps reproduzido **igual** na RX 9070 e na RX 9060 XT |

O teto aparecer igual em duas placas de porte diferente é a assinatura de gargalo de
software, não de placa.

### 1.4 O que não foi recuperado

**[FATO]** Da sessão do outro PC não veio nada: nem log, nem `dlssnr_on_amd.ini` com os
valores que ficaram bons, nem fps medido lá. Três coisas específicas que só ela responde, e
que mudariam decisões deste relatório: se o mod adotou o device D3D12 do nosso Feeder sem a
swapchain-isca; se aceitou o formato de cor 87; e qual `Inline`/`Interop` ficou valendo.
**Pedir a pasta `bin-amd\` daquele PC resolve as três de uma vez** — o `dlssnr_on_amd.log` e
o `.ini` de lá valem mais que qualquer análise minha.

---

## 2. Onde está o peso — a decomposição

O quadro, no caminho AMD, custa em quatro lugares. Só um deles é grande.

| # | etapa | custo | de quem é |
|---|---|---|---|
| 1 | captura da tela e cadeia ReShade | igual ao caminho NVIDIA, já medido lá | nosso |
| 2 | cópias de/para o device privado e o `ffxDispatch` do FSR | **[FATO]** contexto criado em 385 ms na 1ª vez, 22-29 ms nas seguintes; o dispatch em si é um upscaler comum | nosso |
| 3 | **a rede HIP do mod** | **[TERCEIRO]** ~60 ms por job na 9060 XT | do mod |
| 4 | espera por ela | **[FATO]** com `Inline=1` o quadro não fecha antes do job | do mod |

**[FATO]** A linha 4 é o achado desta análise, e está escrita em inglês dentro do próprio
instalador do mod, no texto de ajuda da opção:

> `Inline applies the result to the frame it was computed from; the game runs at the network's pace.`

E a alternativa, também literal: `async (residual from an earlier frame)`.

**[INFERÊNCIA], mas de mecanismo, não de gosto:** com `Inline=1` e 60 ms por job, 16 fps não
é uma consequência do desempenho da placa — é uma consequência da definição. O jogo anda no
passo da rede. **É a primeira coisa a medir.** Mas atenção à §3.2 antes de tratar isso como
solução: o plano do projeto registrou, em 05/09, que o modo assíncrono não se aplica ao nosso
arranjo D3D11, e essa contradição continua sem árbitro até alguém rodar numa Radeon.

**[FATO]** As chaves do `[DlssNrOnAmd]` na v0.2.11, lidas das strings do instalador (a v0.2.18
pode ter outras — o painel do mod abre com **End**):

```
Enabled=1   Inline=1   Interop=1   HipDevice=-1   UseFsrInputs=1   UseDepth=1
UseAutoMask=1   Temporal=1   LocalStructure=1   LocalTone=0   SkinStructure=-1
ToneChannels=0   Tonemap=-1   Scale=0.03125
```

Duas delas têm efeito de desempenho conhecido pelo texto de ajuda do próprio mod:
`Inline` (acima) e `Interop` — *"inline mode needs zero-copy interop; running
asynchronously"*, isto é, sem interop zero-cópia o mod **já cai sozinho** para assíncrono.

---

## 3. As otimizações, em ordem de tamanho do ganho

Cada uma diz o mecanismo, quanto custa fazer, e como se prova. Nenhuma foi aplicada: o
padrão de fábrica do perfil AMD é decisão do líder, e a §5 traz a proposta pronta.

### 3.1 Atualizar o mod para a v0.2.18 — ganho grande, esforço nenhum
**Mecanismo:** +6% e +2% de desempenho publicados (0.2.12 e 0.2.13), gfx1200 nativo (0.2.14),
fim da captura dupla (0.2.15) e o pacote de latência/engasgo/ritmo da 0.2.17-0.2.18.
**O que muda no nosso lado:** o `bin-amd\dlssnr_on_amd_setup.exe` é da v0.2.11 e os textos
(`LEIA-ME-AMD.txt`, `README-AMD.txt`, `preparar-mod-amd.ps1`) ainda dizem que a 9060 XT
"precisa da v0.2.14 ou mais nova; hoje a release pública é a v0.2.11" — **isso envelheceu e
hoje está errado**, porque a 0.2.14 saiu em 06/09.
**Risco:** o setup valida a `nvngx_dlssnr.dll` por hash da build 310.8.0.0, e a nossa é a
310.8.SF.0 (customizada). Se recusar, o usuário precisa de uma cópia oficial.
**Download:** pelo navegador do usuário, conferindo o hash. Não baixei nada.

### 3.2 `Inline=0` — a alavanca de patamar, contra uma decisão já registrada do projeto

**Este item mudou depois que li o `PLANO-AMD.md`, e a contradição é o ponto.**

**[FATO]** O plano de 05/09 (§6.4) manda o ini do usuário ficar com `Inline=1`, e o
`perfil-amd\preparar-mod-amd.ps1:202` **impõe isso ao usuário**, com a justificativa escrita
na tela: *"obrigatório aqui: nossa janela é D3D11, o modo async do mod não se aplica"*. Se o
usuário tiver `Inline=0`, o script se oferece para trocar.

**O raciocínio por trás disso é sólido:** em assíncrono o mod pode escrever o resultado no
swapchain D3D12, e o nosso D3D12 é privado — não tem swapchain à vista (ou tem a isca 32x32
oculta). O efeito sairia num lugar que ninguém vê.

**O que me faz achar que vale remedir mesmo assim [INFERÊNCIA]:** as strings do mod mostram
um caminho assíncrono que aplica **resíduo sobre a textura de saída**, não só substituição de
quadro no swapchain — `async (residual from an earlier frame)`, `residual textures failed;
falling back to frame replacement`, `output has no UAV flag; residual apply unavailable`. E,
principalmente, o mod **cai sozinho para assíncrono** em duas condições (`inline mode needs
zero-copy interop; running asynchronously` e `inline mode unavailable (flag pipeline)`). Se o
assíncrono fosse inútil no nosso arranjo, essas quedas apagariam o efeito em silêncio.

**Conclusão honesta:** ou o plano está certo e `Inline=0` apaga o efeito (e aí a seção INLINE
do script devolve fps alto **sem** `network job` ou com a imagem sem a rede — o script
descarta o primeiro caso sozinho, o segundo exige olhar a tela), ou o plano está
desatualizado e ali está o patamar inteiro. **Só a Radeon decide, e é a primeira coisa a
rodar.** Enquanto não rodar, `Inline=1` continua sendo o padrão e o `preparar-mod-amd.ps1`
não deve ser mexido.

**[FATO] Reforço vindo do próprio script de preparo** (`:247`): quando o mod loga
`inline: N GPU wait timeouts`, a orientação já existente ao usuário é *"a rede está lenta
demais para o teto. Baixe a resolução do DLSS no painel (End)"*. Ou seja, o projeto já
sabia que a saída era resolução — que é a §3.3.

### 3.3 Resolução de trabalho abaixo de 100% — a alavanca que não precisa de código

**Escrevi este item errado na primeira versão e a leitura do código me derrubou. Fica a
versão certa, porque a diferença muda o que se recomenda.**

**[FATO]** O portador cria o contexto FSR com `maxRenderSize = {w,h}`, o **tamanho de
trabalho**, e `maxUpscaleSize = {output}` (`feed_amd.h:211-212`). Como a rede do mod lê as
texturas desse dispatch, **`work_resolution` é o que decide quantos pixels a rede processa**:
77% são 59% dos pixels, 58% são 34%. Isso vale em qualquer configuração e é a alavanca.

**[FATO] O que eu havia dito errado:** que `work_upscale=1` faria o contexto deixar de ser
1:1. Não faz. Em `dlss5-feed.cpp:2919-2924`, `g.output_width` só vira o tamanho do
backbuffer quando `sr_active` — e `sr_active` exige `work_upscale == 2`. Com
`work_upscale=1` o portador continua **1:1**, e quem devolve a imagem ao tamanho da tela é o
**FSR 1 (EASU+RCAS) do próprio Feeder**, que é espacial, não temporal.

Então há dois caminhos, e eles não valem a mesma coisa:

| caminho | a rede vê | quem amplia | precisa de código? |
|---|---|---|---|
| `work_resolution=77` + `work_upscale=1` | 59% dos pixels | FSR 1, espacial | **não** |
| `work_resolution=77` + `work_upscale=2` | 59% dos pixels | o próprio FSR 3.1 do portador, temporal | **sim, uma linha** |

**[FATO] A linha que falta para o segundo caminho:** `AmdDispatch` manda
`dd.jitterOffset = {0.0f, 0.0f}` fixo (`feed_amd.h:283`). Só que com `work_upscale=2` o
Feeder **desloca a grade de amostragem** por Halton a cada quadro (`dlss5-feed.cpp:7317`) e
informa esse deslocamento ao DLSS (`:7377`). Um upscaler temporal que recebe a imagem
jitterada e ouve "jitter zero" acumula a história no lugar errado. **[INFERÊNCIA]** O
resultado seria tremor ou borrão, não ganho. Passar `g.jitter_x/g.jitter_y` ali (como o
caminho NGX já faz, com `jitter_sign`) é o que destrava o modo — e é a única mudança de
código que este relatório propõe.

**[FATO] o que se perde reduzindo:** a bancada do projeto já mediu que a resolução de
trabalho a 87% custa 43% do detalhe fino, com volta por reamostragem simples. Pelo caminho
1 a volta é pelo FSR 1, que é melhor que bilinear e pior que temporal.
**[INFERÊNCIA]** o caminho 2 deve custar bem menos detalhe — e é medível aqui mesmo, com
`tests\nitidez_png.exe`, sem nenhuma Radeon.

**Detalhe a corrigir junto:** o log do portador imprime `(NativeAA 1:1)` fixo
(`feed_amd.h:238`), o que passa a mentir assim que render ≠ upscale.

### 3.4 A "rede leve" (`nr_reduzida`) **não** serve para AMD — e é importante dizer
**[FATO]** O residual em resolução reduzida entregue hoje (+21%, +22% e +34% de fps na RTX
4080) age no Neural Rendering **próprio do Feeder** (`nr_spike=1`, via NGX). No caminho AMD
não há NGX: quem roda a rede é o mod, dentro do `ffxDispatch`. Ligar `nr_reduzida` no perfil
AMD **não faz nada** — e, se um dia fizesse, seriam duas redes sobre o mesmo quadro.
O equivalente funcional para AMD é a §3.3. Não confundir as duas é o que evita um botão
"Rede leve" que não faz nada no módulo AMD.

### 3.5 Promover o addon e o app novos ao perfil AMD
**Mecanismo:** o addon de hoje traz DRED, falha dura sem retry e passagem segura falante —
diagnóstico para quando a Radeon do usuário fizer algo que esta máquina não faz. O app
v1.2.0.0 traz cinco dias de correções.
**Risco:** o binário de hoje foi exercitado só em NVIDIA. Precisa de uma rodada de sanidade
na Radeon antes de virar padrão. As chaves `amd_*` continuam todas presentes nele
(conferido nas strings), então não há regressão de configuração.

### 3.6 Higiene de configuração do perfil AMD (barato, sem risco)
**[FATO]** `bin-amd\dlss5-feed.cfg` tem `amd_carrier=1`. O `1` **força** o portador FSR e
desliga o NGX mesmo numa GeForce; a produção NVIDIA usa `-1` (automático pelo fabricante da
placa). Numa Radeon os dois valores fazem a mesma coisa, mas o `-1` é o que não estraga nada
se a pasta for aberta noutra máquina. **`-1` é o valor certo.**
`warmup_rebuild=180` e `create_delay=60` atrasam a subida da imagem; a 15 fps são segundos
de tela parada. A produção usa `0` e `59`.
E o `bin-amd\ReShade.ini` ainda diz `DLSS 5 Feed 0.7.0` no nome do add-on recolhido —
cosmético, mas é o tipo de coisa que faz um usuário achar que instalou a versão errada.

### 3.7 `amd_color_fmt`: manter 87 se o mod deixar
**Mecanismo:** 87 (BGRA8) são 4 bytes por pixel contra 8 do 10 (RGBA16F) — metade da banda
nas cópias e no staging do mod. **[FATO]** o mod tem a mensagem `unsupported colour format
%d`, então ele responde sozinho se não aceitar. Fica em 87 com o 10 documentado como plano B.

### 3.8 DLSS 2x no módulo AMD: nasce desligado
**[FATO]** O app copia o `dlssnr_on_amd.ini` para a pasta da segunda camada e a liga com
`Enabled=1` (`RELATORIO-APP-AMD.md` §1). Isso significa **duas redes HIP na mesma placa**.
Com ~60 ms por job, é um recurso que não deve estar ao alcance de um clique distraído no
módulo AMD. **[INFERÊNCIA]** Recomendo desligado de fábrica e com aviso no próprio botão.

---

## 4. O que eu NÃO recomendo, e por quê

- **Trocar o mod pelo OptiScaler.** A memória do projeto registra o OptiScaler como o lugar
  onde o "aplicar antes do upscale" apareceu, e o número de ~30 fps vem da narração de um
  vídeo, em RTX 4070 Ti Super, sobre um jogo. **Não é hardware AMD e não é medição nossa.**
  O que dá para aproveitar dele é a *ideia* — e ela já está no nosso portador (§3.3).
- **Mexer em `Temporal`, `LocalStructure`, `SkinStructure` para ganhar fps.** São canais de
  qualidade da rede; nada no texto do mod diz que desligá-los reduz o custo do kernel.
  Mexer neles sem medir é trocar imagem por nada.
- **Empilhar passes no módulo AMD.** A cascata do RenoDX não existe lá, e o equivalente
  seria a segunda camada — ver §3.8.

---

## 5. Proposta de padrão de fábrica para o "AMD experimental"

Para `bin-amd\dlss5-feed.cfg`. As três primeiras linhas do bloco AMD são as que mudam de
comportamento; o resto é alinhamento com a produção.

```
enabled=1
mode=2
hdr=-1
depth_inverted=-1
flags=-1
reset_every=0
warmup_rebuild=0          # era 180: a 15 fps isso era tela parada por segundos
rebuild=0
log_frames=3
create_delay=59           # era 60, alinhado com a producao
preset=5                  # ignorado pelo portador FSR; fica so para o caminho NVIDIA
work_resolution=100       # 77 e a alavanca da secao 3.3 (59% dos pixels na rede) -- SO depois de medir
work_upscale=0            # 1 devolve a imagem por FSR 1 quando work_resolution < 100
work_sharpness=0.30
gpu_timeout_ms=2000
buffer_home=1
mv_scale_x=1.000
mv_scale_y=1.000
stall_log_ms=0
nr_spike=0                # NAO ligar no perfil AMD: quem roda a rede e o mod (secao 3.4)
nr_passes=1
dred=1                    # diagnostico de GPU morta, so no device privado
amd_carrier=-1            # era 1: -1 decide pela placa e nao estraga nada noutra maquina
amd_color_fmt=87
amd_srgb=-1
amd_decoy_swapchain=0
```

E para o `dlssnr_on_amd.ini` (que é do mod, não nosso — o app só liga e desliga o `Enabled`):
**nada muda por ora.** `Inline=1` continua imposto pelo `preparar-mod-amd.ps1`, pela razão da
§3.2, e só se mexe nisso com fps **e** imagem conferidos numa Radeon. Trocar essa chave no
escuro é o tipo de mudança que entrega mais quadros com menos efeito na tela e ninguém nota.

---

## 6. A bancada: `amd\tests\medir-amd.ps1`

Escrito e com a sintaxe validada aqui; **nunca executado**, porque não há placa. O que ele faz:

1. **Recusa-se a rodar** se não houver uma Radeon RX 7000/9000 (lista as placas que viu e sai).
2. Monta/usa `DLSS5ForAll-AMD\bin-teste-amd\`, cópia de `bin-amd\`. **Não escreve em `bin\`
   nem em `bin-amd\`** — o mod grava ini e log ao lado do exe, e por isso medir na pasta de
   produção sujaria a instalação do usuário.
3. Exige o mod instalado *na cópia* e anota a versão da `version.dll` junto dos números.
4. Roda o alvo animado destravado no monitor secundário e compara, **A/B intercalado com
   mediana**: `Inline=1` contra `Inline=0`; 100% contra 77% e contra 58%; formato 87 contra 10.
5. **Descarta** a rodada quando o portador não subiu, quando o mod não escreveu
   `network job N done` (senão mede-se o FSR sozinho e a conta fica linda de mentira), ou
   quando o feed não fechou 600 quadros. Cada descarte diz o motivo, com a linha do log.
6. Reporta duas colunas por configuração: **fps da cadeia** e **ms por job da rede**.

```
powershell -ExecutionPolicy Bypass -File "amd\tests\medir-amd.ps1"
```

Ordem sugerida no PC da Radeon: instalar a v0.2.18 na cópia de teste, rodar a seção INLINE,
depois a de RESOLUÇÃO com o `Inline` vencedor, e só então a de FORMATO.

---

## 7. O que depende do líder ou do usuário

1. **A pasta `bin-amd\` do outro PC** (ou só o `dlssnr_on_amd.ini` e o `dlssnr_on_amd.log` de
   lá). Responde de graça três perguntas que hoje são inferência: swapchain-isca, formato de
   cor aceito, e o que ficou em `Inline`/`Interop`.
2. **Baixar a v0.2.18** pelo navegador do usuário e conferir o hash.
3. **Autorizar a promoção** do app v1.2.0.0 e do addon de hoje ao `bin-amd\`, com a rodada de
   sanidade na Radeon antes de virar pacote.
4. **Decidir os padrões da §5.** Eu não escrevi nada em `bin-amd\`: o cfg acima é proposta.
5. **Autorizar (ou não) o patch de uma linha da §3.3** — passar o jitter Halton ao
   `ffxDispatch` do portador. Não o fiz por conta própria por dois motivos: o arquivo do
   Feeder foi editado hoje por outro agente e mexer nele sem combinar cria conflito; e o
   efeito só se confirma na Radeon. **Mas o teste de que ele não quebra nada é possível
   aqui**, forçando `amd_carrier=1` numa GeForce, que foi como o portador FSR inteiro foi
   validado em 05/09. Se o líder quiser, faço patch, build e essa rodada de sanidade.

Arquivos criados por este trabalho: `amd\tests\medir-amd.ps1` e este relatório. Nada mais
foi tocado.

---

# Fase 2 — a pasta do outro PC

Chegou em `G:\programas de ia\dlss5 for all portable\DLSS5ForAll-AMD\bin-amd-TESTE-UNIFICADO\`
(só leitura; não escrevi nada lá). Máquina de lá: **RX 9060 XT 16 GB (gfx1200) + RTX 5060**,
Windows 11 build 26200, Adrenalin com HIP. Tudo abaixo é **[FATO]** lido nos arquivos, salvo
marcação em contrário.

## 8. O que eles descobriram — e o que isso derruba deste relatório

### 8.1 O padrão que embarcamos impede a rede de rodar

**A descoberta principal do handoff:** com `amd_decoy_swapchain=0` — **o valor que está no
nosso `bin-amd\dlss5-feed.cfg`** — a rede neural **nunca roda**. Como a janela é D3D11, o mod
engancha DXGI e D3D12 Present, mas não arma o hook do FFX; sem ele não há `engine init`, não
há HIP, não há `network job`, e a imagem sai como **FSR puro**. O handoff associa isso ao
relato de usuários de que "na AMD parece só um filtro".

O `dlssnr_on_amd.log` que veio na pasta mostra exatamente esse estado truncado:

```
dlssnr_amd v0.2.18 (build b7e0bab9) loaded into DLSS5ForAll.exe as version.dll
hooked ID3D12CommandQueue::ExecuteCommandLists
hooked IDXGIFactory2::CreateSwapChainForHwnd
hooked IDXGISwapChain::Present
<fim>
```

**Ressalva minha, importante:** esse log específico **não prova o defeito**, porque é de uma
sessão que morreu antes de capturar — o `dlss5forall.log` da mesma hora termina em
`Cancelado no seletor` e o `dlss5-feed.log` fecha com `shut down cleanly` meio segundo depois
de abrir, sem nenhuma linha `[amd] carrier`. **Os logs da rodada boa não estão na pasta**; o
que existe deles é a transcrição dentro do handoff. Isso não desmente a descoberta — o cfg
que sobrou na pasta está com `amd_decoy_swapchain=1`, que é a conclusão deles — mas quem
repetir o teste deve guardar o log inteiro.

### 8.2 O custo real da rede é 13 ms, não 60

**[FATO, transcrito no handoff]** `network job 1600 done in 14 ms (13.5 ms network on the
GPU)`, `warm-up: first network run took 22 ms`, `frames 600 dispatches 972 (1.62/frame) ...
timeouts 0 route fsr`, a 634x669. Os ~60 ms que este relatório citava na §1.3 são estimativa
do autor do mod para 1080p, e a conta bate com eles: a área de 634x669 é um sexto de 1080p.

**Isto confirma a §3.3 por medição, e é a coisa mais acionável do documento inteiro:** o custo
da rede escala com a área, e o próprio handoff fecha com a mesma recomendação — *"Aliviar pela
Resolução do DLSS no painel (77%/67%)"*.

### 8.3 `Inline` — o plano estava certo, e eu estava errado

Retifico a §3.2 pela terceira e última vez, agora com evidência de hardware.
**[FATO]** A configuração que rodou tem `Inline=1`, e o log diz
`interop: inputs shared (zero-copy), output shared (zero-copy); mode inline` com
`timeouts 0`. Inline funcionou, com interop zero-cópia, sem estouro. **`Inline=0` está fora
de cogitação e o `preparar-mod-amd.ps1` não deve ser mexido.**

**A alavanca real tem outro nome, e nós não a temos.** A v0.2.18 traz uma chave que **não
existe na v0.2.11 que empacotamos** (conferido nas strings dos dois binários):

```
InlineWaitMs=25        ← eles baixaram de 200 para 25 (há o .bak-antes-inlinewait provando)
```

É o **orçamento de espera** do modo inline. Pelas strings do mod ele é adaptativo e degrada
com elegância: `inline: N clean jobs; wait budget back to X ms` e, quando estoura,
`previous residual shown`. Ou seja: espera enquanto vale a pena, e mostra o resíduo do quadro
anterior quando não vale. **É o meio-termo entre `Inline=1` e `Inline=0` que eu havia
descrito como inexistente.** Com 200 ms, um quadro ruim pode segurar a cadeia por 200 ms;
com 25, o teto de dano é 25.

A v0.2.18 também ganhou uma linha de log que decompõe o custo inteiro, e que vale mais que
qualquer cronômetro nosso:

```
timing (avg of N jobs): worker wall = queued + inputs + wait-for-capture + network + flag+sync
 | game queue: capture copies, spin waiting on us, residual copy+apply | present period
```

### 8.4 A v0.2.11 nunca funcionaria na placa do usuário
**[FATO]** Alvos compilados na v0.2.11: `gfx1100/1101/1102/1201`. Na v0.2.18:
`gfx1201, gfx1200, gfx1100, gfx1101, gfx1102, gfx11-generic, gfx10-3-generic, gfx9-generic`.
`gfx1200` é a RX 9060 XT. O instalador que o nosso pacote distribui hoje é incapaz de rodar
na placa que o usuário tem.
**[FATO]** O setup da pasta deles tem SHA-256 `dad67cc649ad91ba28e83c30049fc899900ae532daf818803bd1123e6e2315c3`
e 7.570.162 bytes — **bate exatamente com o hash oficial da v0.2.18** citado no handoff.
O nosso embutido: `dbf7e40ca6940924ffe0960b350f3d55cb99ed27dde84aec47cdf8a0b7303a74`, 4.737.763 B (v0.2.11).

### 8.5 Duas coisas do handoff que os arquivos não sustentam
Ditas porque quem retomar vai tropeçar nelas:
- **[FATO]** O handoff §7 diz que o `DLSS5ForAll.exe` da pasta é *"o exe mais novo (08/09),
  vindo do bin-teste-passes"*. **Não é.** O arquivo é de **06/09**, 2.452.553 B, v1.1.0.0; o
  do `bin-teste-passes` tem 2.637.100 B e é de 08/09 18:13. São binários diferentes. A
  "ressalva importante" do handoff (validar exe 08/09 + feeder 05/09) se apoia nessa premissa
  errada: o que rodou foi **exe 06/09 + feeder 05/09**, que é a combinação que ele mesmo diz
  ter comprovado. Nada a validar.
- **[FATO]** O `dlss5-feed.addon64`, o `amd_fidelityfx_dx12.dll` e o `dxgi.dll` de lá são
  **byte a byte idênticos** aos do nosso `bin-amd\`. O `DLSS5ForAll.exe` e o
  `dlss5-bridge.addon64` diferem.

### 8.6 Um achado que nem eles nem nós tínhamos visto
**[FATO]** O `dlss5forall.ini` deles guarda os controles do painel (`nr_intensidade=2`,
`nr_tom_local=1`, `nr_estrutura_local=2`, `nr_estrutura_pele=2`, `nr_mascara_auto=0`), e o app
os gravou no `ReShade.ini` num bloco `[RenoDX.DLSS5]`. **No módulo AMD o RenoDX não está na
pasta** — o próprio `dlss5-feed.log` registra `renodx-dlss5*.addon64 not found`. E o
`dlssnr_on_amd.ini`, que é quem o mod lê, está com valores **diferentes** (`LocalTone=0`,
`LocalStructure=1`, `SkinStructure=-1`, `UseAutoMask=1`).

**Conclusão:** no módulo AMD os controles de intensidade, tom e estrutura do nosso painel
**não chegam à rede**. O mod tem chaves equivalentes, com outros nomes, no ini dele. É um
painel que parece funcionar e não funciona — o mesmo defeito que o `RELATORIO-RESIDUAL-NR.md`
§9 pediu para evitar com a "Rede leve".

### 8.6.1 O mapeamento, para quem for ligar o painel ao mod

Levantado nas strings do `version.dll` v0.2.18, a pedido do `painel-requiem`. **[FATO]** A
lista completa de chaves do `[DlssNrOnAmd]` é: `Enabled`, `Inline`, `InlineWaitMs`,
`Interop`, `HipDevice`, `UseFsrInputs`, `UseDepth`, `UseAutoMask`, `Temporal`,
`LocalStructure`, `LocalTone`, `SkinStructure`, `ToneChannels`, `Tonemap`, `Scale`.

| controle do painel | chave do mod | observação |
|---|---|---|
| TOM LOCAL | `LocalTone` | — |
| ESTRUTURA | `LocalStructure` | ajuda do mod: *"Detail the network adds: ambient occlusion, contact shadows, reflections, subsurface scattering (default 1)"* |
| PELE E ROSTOS | `SkinStructure` | `-1` = **auto** (segue a Structure), `0` = desliga o canal |
| máscara automática | `UseAutoMask` | — |
| INTENSIDADE | **não existe** | o único candidato, `Scale`, aparece colado a `block70.layer0.blend_scale`: é escala interna da rede, não controle de produto |
| MODELO (estilo) | **não existe** | nenhuma chave de estilo, preset ou modelo |
| TOM GLOBAL | `ToneChannels` | existe, **mas** a ajuda do mod diz *"This DLL build gates these channels off; raising it mostly darkens the frame"* |
| PASSES | **não existe** | o `nr_passes` é do caminho NGX; o log do mod confirma passe único |
| — | `Tonemap` | curva de tonemapping da entrada, `-1` = automático. **Não** é o tom global |

**[FATO] Releitura ao vivo, com prova, só para duas chaves:** o mod tem a linha
`ini changed Inline=%d Interop=%d; re-creating staging`. **[INFERÊNCIA]** as demais devem
seguir o mesmo caminho, mas nada no binário o diz. Quem for gravar nesse arquivo precisa
lembrar de duas coisas: ele é **CRLF**, e o **mod também escreve nele** pelo overlay (End) —
são dois escritores no mesmo arquivo.

---

## 9. Mudanças propostas, com origem e risco

Nada foi aplicado. `bin-amd\` continua intocado (25 itens, como estava).

| # | mudança | origem | risco |
|---|---|---|---|
| 1 | `amd_decoy_swapchain=0` → **1** em `bin-amd\dlss5-feed.cfg` | handoff §1 + `dlss5-feed.cfg` deles | **baixo**: numa GeForce o decoy é ignorado por fabricante (`carrier not wanted on vendor 0x10DE`). Sem isso a rede não roda em AMD |
| 2 | completar o cfg com as chaves que faltam (`work_upscale=0`, `work_sharpness=0.30`, `async_home`, `sync_home`, `stall_log_ms=50`) | `dlss5-feed.cfg` deles | baixo: são os valores já comprovados |
| 3 | trocar o `dlssnr_on_amd_setup.exe` embutido da v0.2.11 pela **v0.2.18** | hash oficial conferido na pasta deles | **médio**: é binário de terceiro; a política de embutir já existe. Sem isso, dono de RX 9060 XT não roda nada |
| 4 | acrescentar `InlineWaitMs` ao que o `preparar-mod-amd.ps1` confere e explica | `dlssnr_on_amd.ini` deles | baixo (texto e conferência) |
| 5 | **recompilar o launcher**: o binário barra a RX 9060 XT com `radeon=0`, o fonte já aceita | handoff §3.1, `launcher.log` deles | baixo, mas **é código** — hoje todo dono de 9060 XT é barrado na porta |
| 6 | `preparar-mod-amd.ps1`: acrescentar o hash da v0.2.18 à tabela `$SetupsConhecidos` | handoff §3.2 | baixo. Hoje o script chama a versão oficial de "arquivo que ninguém auditou" |
| 7 | `preparar-mod-amd.ps1`: o aviso "precisa do mod v0.2.14 ou mais novo" só disparar se a versão instalada for menor que a mínima da placa | handoff §3.3 | baixo |
| 8 | documentar no `LEIA-ME-AMD.txt`: no instalador, responder **`version`** ao "Install as dxgi.dll?" | handoff §2 | baixo — e evita que o usuário destrua a cadeia inteira |
| 9 | espelhar os controles do painel para o `dlssnr_on_amd.ini` no módulo AMD | §8.6 deste relatório | **médio**: é `main.cpp`, e o mapeamento de nomes precisa ser conferido no painel do mod (End) |
| 10 | `amd_carrier`: mantive a recomendação da §3.6 (`-1`) **suspensa** | eles rodaram com `1` | **a prova que existe usou `1`**. Não trocar sem remedir |
| 11 | reduzir a resolução de trabalho como recurso de alívio | handoff §8 e §3.3 daqui | baixo em código, **exige medir** o custo de imagem |

**Precisa de máquina AMD:** 1 (confirmar), 3, 5 e 9 (verificar), 10 e 11 (medir). **Não
precisa:** 2, 4, 6, 7, 8 — são texto e configuração já comprovados do outro lado.

---

# Fase 3 — a incorporação: `bin-amd-1.2-proposta\`

Montada em `DLSS5ForAll-AMD\bin-amd-1.2-proposta\` (51 arquivos, 27,8 MB). **`bin-amd\` de
produção não foi tocado, e nada foi escrito na pasta `G:`.** A regra que guiou tudo: **o que
é do mod e da AMD entra; o que é nosso e está velho lá não volta.**

## 10. Arquivo a arquivo

| arquivo (lá) | lá | nosso | decisão |
|---|---|---|---|
| `dlssnr_on_amd_setup.exe` | **v0.2.18**, 7.570.162 B | v0.2.11, 4.737.763 B | **INCORPORAR** — hash confere com o oficial da v0.2.18. A v0.2.11 não tem `gfx1200` |
| `dlssnr_on_amd.ini` | 225 B, com `InlineWaitMs=25` | não existia | **INCORPORAR byte a byte** — é a configuração que rodou |
| `dlss5-feed.cfg` | 386 B, `decoy=1` | 1.014 B, `decoy=0` | **MESCLAR** — ver §11 |
| `ReShade.ini` | 3.282 B | 3.079 B | **MESCLAR uma linha só** — ver §11.2 |
| `amd_fidelityfx_dx12.dll` | 6.675.664 B | idem | **idêntico byte a byte** — nada a fazer |
| `dxgi.dll` (ReShade) | 5.592.064 B | idem | **idêntico** |
| `CREDITOS.md`, `LICENSE`, `LICENSE-FidelityFX.txt`, `preparar-mod-amd.cmd` | 05-06/09 | idem | **idênticos** |
| `DLSS5ForAll.exe` | 06/09, v1.1.0.0 | **11/09, v1.2.0.0** | **IGNORAR** — o nosso é cinco dias mais novo |
| `dlss5-feed.addon64` | 05/09, 961.584 B | **11/09, 994.756 B** | **IGNORAR** — o nosso é a mesma variante AMD (tem `amd_carrier`, `amd_decoy_swapchain`) **mais** `dred`, `hud_restore` e o caminho `nr_*` |
| `dlss5-bridge.addon64` | 05/09, 453.569 B | **11/09, 1.019.764 B** | **IGNORAR** |
| `FafaDLSS5.ini` | 05/09, 2.238 B | **11/09, 2.444 B** | **IGNORAR** — o nosso traz o bloco do `DLSS5_HUD.fx` |
| `reshade-shaders\` | 26 arquivos | **27** | **IGNORAR** — só o nosso tem `DLSS5_HUD.fx`, que o `FafaDLSS5.ini` 1.2 referencia |
| `LEIA-ME-AMD.txt`, `README-AMD.txt` | 05/09 | idem | **REESCREVER** — ver §12 |
| `preparar-mod-amd.ps1` | 05/09 | idem | **CORRIGIR** — ver §12 |
| `dlss5forall.ini` | 459 B | 89 B | **IGNORAR** — o de lá é estado daquela máquina (a placa, o monitor, a janela do Cyberpunk, a posição do painel). Nada ali é padrão de fábrica |
| `version.dll` (o mod, 7,2 MB) | v0.2.18 | — | **NÃO REDISTRIBUIR** — é o mod; o `PLANO-AMD.md` já proibia, e o setup o gera na máquina do usuário |
| `dlssnr_on_amd_weights.bin` (147 MB) | — | — | **NÃO** — derivado da DLL da NVIDIA, gerado pelo setup |
| `nvngx_dlssnr.dll` (165 MB) | — | — | **NÃO** — o próprio handoff escreve "NÃO REDISTRIBUIR" |
| `ReShade2.ini` | 1.949 B | — | **IGNORAR** — arquivo de runtime, não é peça de configuração nossa |
| 6 `ReShade.ini.bak-keyoverlay-*`, `dlssnr_on_amd.ini.bak-antes-inlinewait` | — | — | **IGNORAR** (o `.bak-antes-inlinewait` foi a prova de que eles mexeram na chave; o valor entrou, o arquivo não) |
| `dlssnr_on_amd.log`, `dlss5-feed.log`, `dlss5forall.log*`, `ReShade.log` | — | — | **IGNORAR** — logs |
| `LEIA-PRIMEIRO-HANDOFF-AMD.md` | 9.323 B | — | **IGNORAR como arquivo** — o conteúdo virou a Fase 2 deste relatório |

Os três binários que vieram da produção e o preset foram re-hasheados depois de copiados e
batem com `bin\`: `DLSS5ForAll.exe` `C13671EE1131…`, `dlss5-bridge.addon64` `7B79BDF627FF…`,
`dlss5-feed.addon64` `207E173AA0EB…`, `FafaDLSS5.ini` `C2D9F8B78922…`.

## 11. A mescla, chave a chave

### 11.1 `dlss5-feed.cfg`

| chave | lá | produção 1.2 | ficou | por quê |
|---|---|---|---|---|
| `amd_decoy_swapchain` | **1** | 0 | **1** | a correção principal: com 0 a rede nunca roda nesta janela D3D11 |
| `amd_carrier` | **1** | -1 | **1** | é o valor da configuração comprovada; numa Radeon o -1 daria no mesmo, mas não se troca o que rodou sem remedir |
| `amd_color_fmt` | 87 | 87 | **87** | igual dos dois lados, e comprovado |
| `amd_srgb` | -1 | -1 | **-1** | automático |
| `warmup_rebuild` | **180** | 0 | **180** | o valor sob o qual a rede rodou |
| `create_delay` | **60** | 59 | **60** | idem |
| `work_sharpness` | **0,30** | 0,45 | **0,30** | idem. O 0,45 é afinação do caminho NVIDIA |
| `work_resolution` / `work_upscale` | 100 / 0 | 100 / 0 | **100 / 0** | iguais. A redução é a alavanca de alívio, mas entra medida, não de fábrica |
| `stall_log_ms` | 50 | 50 | **50** | iguais |
| `nr_spike`, `nr_passes` | não existiam | 0 / 1 | **0 / 1** | chaves 1.2. `nr_spike=0` é obrigatório aqui: quem roda a rede é o mod, ligar o nosso seriam duas redes no mesmo quadro |
| `hud_restore`, `hud_restore_strength` | não existiam | -1 / -1 | **-1 / -1** | chaves 1.2; o shader que elas usam veio junto |
| `dred` | não existia | ausente (padrão 1) | **1 explícito** | breadcrumbs de GPU valem mais numa placa que não temos aqui |

Cada bloco levou comentário no arquivo dizendo o que a chave faz e por que está nesse valor,
inclusive o parágrafo que explica o sintoma do `amd_decoy_swapchain=0`.

### 11.2 `ReShade.ini`

Só **uma** linha mudou: `OverlayCollapsed` passou a dizer `DLSS 5 Feed 0.12.0` em vez de
`0.7.0`, que é o nome que o add-on registra hoje. **Três coisas do arquivo de lá foram
recusadas de propósito:** o `IntermediateCachePath` **absoluto** apontando para o `D:` daquela
máquina (o nosso é `.\reshade-cache`, relativo, e tem de continuar), o bloco
`[RenoDX.DLSS5]` (o RenoDX não entra no perfil AMD, e o `dlss5-feed.log` de lá confirma
`renodx-dlss5*.addon64 not found`) e o `FontScale=1.5`, que é preferência de tela daquele PC.
`KeyOverlay=36` é igual dos dois lados.

### 11.3 `dlssnr_on_amd.ini`
Copiado **byte a byte**, CRLF preservado. É todo do mod — não há chave nossa para mesclar, e
mexer em qualquer valor desfaria a única configuração que se sabe funcionar.

## 12. O que mudou nos textos e no script

- **`preparar-mod-amd.ps1`** (sintaxe validada, BOM e LF preservados):
  o hash da v0.2.18 entrou na tabela `$SetupsConhecidos`, então a versão oficial deixa de ser
  chamada de "arquivo que ninguém auditou"; o aviso da RX 9060 XT deixou de ser incondicional
  e agora só sai se a versão instalada for velha demais; entrou a conferência de
  `InlineWaitMs`; a conferência do `amd_decoy_swapchain` passou a **checar o valor** e a falhar
  com explicação quando não é 1; entrou a detecção do sintoma exato no log (o log do mod que
  para nos `hooked IDXGI...`); e entrou o aviso, antes de o usuário rodar o instalador, de
  responder **`version`** ao "Install as dxgi.dll?".
- **`LEIA-ME-AMD.txt` e `README-AMD.txt`**: o título passou de "MÓDULO AMD (BETA)" para
  **"AMD EXPERIMENTAL"**; entrou o que foi testado e o que não foi (vídeo sim, jogo não);
  o texto da RX 9060 XT deixou de dizer que o suporte "ainda não saiu"; o instalador deixou de
  ser descrito como "não vem no pacote", porque vem; entrou o aviso do `dxgi.dll`; a seção de
  desempenho passou a abrir pelo mecanismo (o custo acompanha a área), com os 13 ms medidos e
  os 60 ms do autor lado a lado; e `amd_decoy_swapchain` virou "1 é o certo aqui".
  Marca escrita **"PC do Fafa"**, sem acento — `Faf[áÁ]` dá zero em toda a pasta.

## 13. Conferência estática da proposta

| conferência | resultado |
|---|---|
| log, `.bak`, dump, handoff, `ReShade2.ini` copiados | **nenhum** |
| `version.dll`, pesos ou `nvngx_dlssnr.dll` redistribuídos | **nenhum** |
| hashes dos quatro arquivos 1.2 | **idênticos a `bin\`** |
| `Faf[áÁ]` em txt/md/ps1/cmd/ini/cfg | **0** |
| codificação | `.txt`/`.ps1` com BOM UTF-8 e LF, `.cfg` sem BOM e LF, `.ini` do mod com CRLF — **iguais aos originais de cada um** |
| `reshade-cache\` | **removido** (é específico do par vendor/device e não pertence a um pacote) |
| sintaxe do `preparar-mod-amd.ps1` | **OK** |

## 14. Roteiro de validação na máquina AMD

Nada disto foi executado: aqui não há Radeon. Na ordem:

1. Copiar `bin-amd-1.2-proposta\` para a máquina da RX 9060 XT e rodar
   `dlssnr_on_amd_setup.exe` **dentro dela**, com o `nvngx_dlssnr.dll` ao lado, respondendo
   **`version`** ao prompt do `dxgi.dll`.
2. `preparar-mod-amd.ps1` — deve dar tudo verde, sem o falso alarme do hash e sem o aviso
   indevido da 9060.
3. `DLSS5ForAll.exe --amd --source window --window "<janela>"` e conferir
   `network job N done in X ms` no `dlssnr_on_amd.log`. **Guardar o log inteiro desta vez.**
4. **O ponto que só esta rodada responde:** o app 1.2 e o feeder 1.2 nunca encontraram o mod.
   O que se comprovou lá foi app 06/09 + feeder 05/09. Se a rede não engatar, trocar **só o
   `dlss5-feed.addon64`** pelo de 05/09 isola a causa em um arquivo.
5. `amd\tests\medir-amd.ps1` para as três varreduras: `InlineWaitMs` 200 contra 25,
   resolução 100 contra 77 e 58, formato 87 contra 10.
