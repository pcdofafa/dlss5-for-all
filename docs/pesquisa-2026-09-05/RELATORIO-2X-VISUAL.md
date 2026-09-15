# DLSS 2x sem diferença visual: por quê, como a comunidade testa duas passadas e o que mudar na segunda camada

Pesquisa feita em 2026-09-05, somente leitura do projeto. Nenhum arquivo do app foi alterado, nada foi executado.

Convenções:

- **[FATO]** — verificado em log, código, documento oficial ou página pública citada.
- **[INFERÊNCIA]** — raciocínio meu a partir dos fatos; marcado como tal.
- Caminhos: `amd/main.cpp` = `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\amd\main.cpp` (é a fonte que tem o modo DLSS 2x; o `main.cpp` da raiz não tem). `bin\` e `bin-camada2\` = `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\DLSS5ForAll-AMD\bin\` e `...\bin-camada2\`. Documentos baixados nesta pasta: `dfc-README.md`, `dfc-QUICK-START.md`, `dfc-COMPATIBILITY-TEST.md`, `dfc-FEEDER-COMPATIBILITY.md` (Deep Fried Chicken 1.4.0, espelhados no repositório do DLSS5-Feeder) e `conv-README-main.md` (DLSS5-Image-Converter). O material anterior está em `..\pesquisa-dlss10\`.

---

## Resumo executivo

1. **A segunda camada nunca rodou a rede neural em nenhum dos cinco testes do autor.** [FATO] O `bin-camada2\dlss5-feed.log` para em `holding the feature (re)build for 60 frames` (linha 16) e o processo morre 1,2 s depois (linhas 17-18). Não há `building`, `feature ready` nem `frame N delivered`. O `bin-camada2\ReShade.log` não tem `NGX feature create intercepted`, `feature 18 created` nem `inline feature 18 evaluation succeeded` (compare com o da primeira camada, `bin\ReShade.log:144-156`, que tem todos). Quatro dos cinco lançamentos duraram entre 1 e 8 segundos (`bin-camada2\dlss5forall.log:2-32, 68-97, 99-129, 131-161`); o único longo (105 s, linhas 34-66) subiu com o neural **desligado** (`estado=0`, linha 58; `aviso: neural nao esta ativo`, linha 64). Ou seja: o autor comparou a primeira camada com **ela mesma passando por um bypass**. "Não dá diferença" é o resultado correto desse teste, não uma propriedade das duas passadas.
2. **Por que morreu tão cedo:** a segunda camada nasce em bypass (`amd/main.cpp` "aquecimento: a camada comeca em bypass") e o Feeder segura a criação do feature por **60 frames apresentados** (`create_delay=60`, `bin-camada2\dlss5-feed.cfg`). A segunda camada só apresenta quando a primeira apresenta, e a primeira estava a **7,6 fps** naquele trecho (`bin\dlss5-feed.log:30`; `LAT 107s ... present=2.8`, `bin\dlss5forall.log:77`) porque a fonte era um vídeo do YouTube parado (intervalo máximo entre frames do WGC: 37 s, linha 78). Na própria primeira camada, do `holding` ao `frame 1 delivered` levou 7,3 s (`bin\dlss5-feed.log:16-27`) e a camada só foi liberada após **10,5 s** (`bin\dlss5forall.log:47`). Na segunda, a ~3-8 fps, isso vira 15-30 s. O autor fechou com 1 a 6 s. A segunda camada é muda (nenhum cartaz, `amd/main.cpp:2954`) e o painel da primeira só diz "ATIVA" quando lê `camada liberada` no log dela (`amd/main.cpp:8175-8185`) — linha que nunca chegou a aparecer nos testes de 01:59-02:00.
3. **Duas passadas com os mesmos parâmetros mudam a imagem sim, mas menos que a primeira.** [FATO, medido por terceiros] DLSS5-Image-Converter, alimentando a saída de volta como entrada com força máxima: a 1ª passada move a imagem 0,057 (diferença média) da fonte, a 2ª move **mais 0,035** ("roughly as much again") e é "the quickest way to make a portrait look plastic"; recomendação do autor: **"Lower the strengths for the second pass rather than repeating the first"** (`conv-README-main.md:220-231`). Webcam-demo: estrutura 1,29x → 1,41x, luminância +5,4 → +10,1 (relatório anterior). O modelo é treinado para ficar preso à entrada (NVIDIA: "remains firmly grounded in the engine's base frame"), então a segunda passada não dobra o efeito: acrescenta ~60% do primeiro salto e empurra o tom mais para claro e frio.
4. **Onde a diferença aparece:** close de rosto/pele (subsurface scattering), sombras de contato, dobras de tecido, cabelo; **cena parada, zoom 100% ou mais**. Com a câmera afastada em jogo "é mais fácil perceber pelo fps do que pela imagem" (HotHardware). No conversor, "at 6K the things this tool changes are invisible at fit-to-window" (`conv-README-main.md:176-179`). A comunidade compara com **F6** (liga/desliga ao vivo), **F5** do RenoDX (par de screenshots `_NR_OFF`/NR na pasta `DLSS5 Screenshots\`), wipe/lado a lado no mesmo frame (webcam-demo tecla `S`, conversor), e vídeo com o flicker como critério (Pandorian Grey).
5. **Parâmetros:** as ferramentas de multipass nasceram com os mesmos parâmetros em todas as passadas (Alex's Toolkit não tem controle por passada; Deep Fried Chicken expõe `Pass N` mas o padrão "golden" é tudo em 2,0 em todas, `dfc-README.md:297-300`). O que a comunidade faz para a 2ª passada render algo **bom**, não só algo: baixar as forças na 2ª (conversor), ligar o "Clean Fry" (0,35/0,85) para tirar halo e acúmulo de cor (`dfc-QUICK-START.md:141-144`) e o "restore native tone & color, keep neural detail" para devolver o tom do jogo (`dfc-README.md:320-329`). `NRStyle` é o botão de maior efeito (Cinematic fica ~50% mais longe da fonte que Natural); `NRPreset` medido como **inerte** em DLAA (bit-idêntico nos 4 valores) — e a nossa cadeia é DLAA 1:1.
6. **Segunda varredura (YouTube, fóruns, GitHub, §1.4 e §2.5):** existem quatro vídeos públicos de "DLSS 5 x2 / Double Pass / 5 passes (DLSS 10?)" com 7 a 68 mil views (GTA V, CS2, tutorial francês com pacote no Drive, Direcreep), um tópico do ResetEra citando "10 instances of DLSS 5 on top of itself" e dois issues pedindo passes em ferramentas offline. **Nenhum publica parâmetros por camada**; todos usam os mesmos parâmetros nas duas passadas e comparam em cutscene/close. A promessa deles é "imagem mais limpa" e as queixas são flicker, artefatos e fps. Não há relato público de NR reaplicado em vídeo que já tinha DLSS 5; o que explica o "ficou bem melhor" do autor é a medição do conversor ("worth having on a flat render") e o fato de que a rede remove ruído de compressão antes de acrescentar estrutura.
7. **Bloqueio de implementação a corrigir antes de qualquer ajuste:** `Camada2Espelhar` **sobrescreve** `ReShade.ini`, `FafaDLSS5.ini`, `dlss5-feed.cfg` e `dlss5forall.ini` da `bin-camada2\` a cada clique em "DLSS 2x" (`amd/main.cpp:3346-3350, 3371`). Qualquer valor diferente escrito à mão na cópia some no próximo lançamento. O único valor que sobrevive é `NeuralUplift=1`, que o próprio código força depois da cópia (`amd/main.cpp:3396-3400`). Os ajustes da seção 3 têm de entrar **ali**, do mesmo jeito.

---

## 1. Como a comunidade compara 1 vs 2 passadas

### 1.1 Método [FATO]

| Ferramenta | Como compara | Fonte |
|---|---|---|
| RenoDX DLSS5 Generic (o nosso add-on) | **F6** liga/desliga o NR ao vivo ("NR toggle F6", `bin\ReShade.log:48`); **F5** salva um par de screenshots, um com NR e outro `_NR_OFF`, em `DLSS5 Screenshots\` ao lado do exe (snippet de busca do guia Stellar Blade/1-Click; a pasta `DLSS5 Screenshots` existe vazia na raiz do projeto) | Nexus 3706, 1-Click-DLSS5 releases |
| Deep Fried Chicken (multipass) | muda o `Passes` **ao vivo** na aba Chicken do overlay do ReShade e tira o F9 duas vezes: "That is the reliable way to get a same-frame A/B" | `webcam-demo-README.md` (seção "ReShade + an NR consumer") |
| Webcam-demo | tecla `S`: wipe (metade/metade) → lado a lado (mesmo frame, crop central); `F9` grava `_A_input`, `_B_dlss`, `_C_screen`; correção de cor só no lado neural para não enviesar | `webcam-demo-README.md` ("Comparison views") |
| DLSS5-Image-Converter | wipe com divisor arrastável, **Difference view**, zoom compartilhado entre painéis, "Compare styles" com 3 painéis (Original/Natural/Cinematic) — "answers ... 'is the pass helping at all'"; "Use result as input" gera `name_pass2.png`, `name_pass3.png` para comparar passadas | `conv-README-main.md:14-30, 176-235` |
| Teste de conformidade do DFC | "compare Clean Fry off/on ... Check high-contrast edges, fine text, hair, foliage, skin pores, and slow camera motion"; "Test one pass, then two passes" | `dfc-COMPATIBILITY-TEST.md:175-178, 207` |
| Reviews (Guru3D, HotHardware, Wccftech) | toggle ao vivo em cena parada, imagem ampliada e alternada com setas do teclado; CapFrameX para o custo | Guru3D p.3, HotHardware |
| Vídeo (Pandorian Grey) | grava o mesmo trecho com 4.55 / 4.6 / 4.7 / DFC em várias passadas e julga **flicker** e estabilidade, não detalhe | YouTube `MEP4D99RNJ0` (descrição, relatório anterior) |

### 1.2 Em que cena a diferença aparece [FATO]

- NVIDIA (NBA 2K27): "natural skin subsurface scattering", "tighter contact shadows ... along his neck and jersey collar", "light transmission elevates his hair definition", "shadow definition across the folds of his compression arm sleeve". Estrutura = "ambient occlusion, reflections, and subsurface scattering"; tom = "broader lighting and color response".
- HotHardware: "close-ups and zoom shots" é onde "DLSS 5 can look incredible"; com a câmera afastada do jogo "often easier to tell by observing the frame rate and motion smoothness than the image quality".
- Guru3D: "forehead, cheeks and area around the mouth contain more visible tonal variation, and the eyes, eyebrows, nose and lips are more clearly separated".
- MindStudio (mods): "Faces and lighting see the biggest jump"; os sliders de pele/estrutura mudam "lighting interaction more than the base texture itself"; presets "altered color grading and perceived realism rather than adding new geometry".
- ComfyUI-DLSS5-Enhancer (medido): "The model reconstructs materials rather than sharpening edges. Overall detail energy drops, because generator noise disappears, while skin, hair and fabric gain structure." — ou seja, em fonte com ruído/compressão (vídeo do YouTube!) a rede **limpa** antes de acrescentar. [INFERÊNCIA] Numa fonte de YouTube a segunda passada tende a parecer "mais lisa/limpa" e mais clara, não "mais detalhada".
- Conversor: em imagem grande "invisible at fit-to-window"; é preciso zoom.

### 1.3 Quanto de diferença esperar da 2ª passada [FATO]

| Medição | 1 passada | 2 passadas | Fonte |
|---|---|---|---|
| Diferença média da fonte (força máx., saída realimentada) | 0,057 | +0,035 (total ≈ 0,092) | `conv-README-main.md:227-229` |
| Estrutura vs bicúbico (webcam 720p→1440p, DFC) | 1,29x | 1,41x | webcam-demo |
| Deriva de cor R−B / luminância | −14,7 / +5,4 | −15,9 / +10,1 | webcam-demo |
| Convergência temporal na mesma imagem (não é empilhar) | 0,0530 | 0,0547 (8 iterações) | `conv-README-main.md:403-404` |
| Estilos, à mesma força | Natural 0,0105 · Cinematic 0,0143 da fonte; Natural vs Cinematic 0,0140 | — | `conv-README-main.md:211-218` |

Leitura: a 2ª passada com os mesmos parâmetros vale ~60% da 1ª em "distância da fonte", o brilho **acumula** (dobra), e trocar o estilo muda tanto quanto a passada inteira. Nenhuma fonte diz que duas passadas iguais **não** mudam nada; o que todos dizem é que muda **menos**, que **clareia** e que passa a "plástico" rápido. Em jogo em tempo real, os relatos são de flicker e borrão em movimento (relatório anterior, §1.3).

### 1.4 Segunda varredura (YouTube, fóruns, repositórios): quem publicou "DLSS 5 duas vezes"

Busquei por título, descrição e texto indexado em 2026-09-05. Comentários do YouTube e páginas do ResetEra/Guru3D/HardForum não puderam ser lidos por máquina (bloqueio 403; o endpoint de legendas do YouTube exige token). O que existe público:

| Vídeo / post | Autor, data, alcance | O que diz (descrição literal) | O que dá para usar |
|---|---|---|---|
| **"GTA V DLSS 5 x2 Stacked double render experiment"**, `youtube.com/watch?v=4PaLeter4sY` | Papryk Szadolowski, 2026-08-31, 68 mil views, 24 min | "Testing GTA 5 with stacked double rendering using DLSS 5. This video demonstrates how image quality and overall smoothness look with DLSS 5 2x mode." | [FATO] o termo "DLSS 5 2x" existe na comunidade; é o vídeo mais visto do tema. Sem parâmetros na descrição. GTA V: rostos e materiais de jogo antigo, exatamente o conteúdo que a comunidade escolhe para mostrar diferença. |
| **"DLSS 5 - x2 (Double Pass) - Counter-Strike 2"**, `youtube.com/watch?v=xOy0lwCWsiw` | Exozze, 2026-09-03, 47 mil views, 4m50 | "PC Specs: RTX4090, 7800X3D, DDR5 64GB" | [FATO] só o título; sem parâmetros. |
| **"DLSS 5 Double Pass X2 : INSTALLATION FACILE \| RTX 5070 + 9800X3D"**, `youtube.com/watch?v=oWbTiHO1HY0` | K3RIA (francês), 2026-09-04, 7 mil views, 28 min | Distribui um pacote "DLSS 5 Double Pass X2.rar" (Google Drive). Capítulos: "05:27 Réglage", "23:15 Cinematique Comparaison AVEC ou SANS". Aviso: *"Saviez-vous qu'il est possible de doubler plusieurs fois le DLSS 5 ? En appliquant plusieurs passes, on peut potentiellement obtenir une image encore plus propre et une meilleure qualité visuelle. Mais attention : plus on multiplie les passes, plus cela peut avoir un impact sur les performances et parfois créer des artefacts."* | [FATO] a promessa da comunidade para 2 passadas é "imagem **mais limpa**", não "mais detalhe"; o custo citado é fps e artefatos. Método de comparação dele: **cutscene** (cena de diálogo, câmera controlada) com/sem. Conteúdo do `.rar` não verificável sem baixar; [INFERÊNCIA] é o combo RenoDX v4.55 + `alexs-toolkit` (o único empacotável que faz "double pass" com um clique; o DFC proíbe rehospedagem). |
| **"DLSS 5 - 5 times pass over one game...DLSS 10 ?"**, `youtube.com/watch?v=GGI4wmsuoU4` | Direcreep, 2026-09-02, 21 mil views, 2m34 | descrição vazia | [FATO] "DLSS 10" aparece como **piada de título** para 5 passadas (corrige o relatório anterior, que não achou o termo). Sem parâmetros. |
| **Pandorian Grey**, `MEP4D99RNJ0` | 2026-09-02, 172 views, 28 min | *"I was trying to reduce the flicker from the high level of multipass NR, but it seems some games adopt better than others ... The most graphically stable in this case seemed to be v4.55 or Shortfuse Multi at 1x but with a reduced NR realistic look."* | [FATO] único vídeo que compara versões e passadas lado a lado; conclusão dele é **voltar a 1x com força reduzida** por causa do flicker. |
| **ResetEra**, tópico "DLSS 5 releases to all users this week", p.18 | snippet de busca (página bloqueada) | usuários citam "really deep fried examples" e "people literally running **10 instances of DLSS 5 on top of itself**" | [FATO, snippet] o empilhamento é visto como curiosidade extrema, não como preset recomendado. |
| **X (Twitter)**, Stamos, `x.com/Stamosxd/status/2094647012847218854` | — | "renodx discord has a 'deep fried chicken' addon for 3 passes but idk any more" | [FATO] confirma que a distribuição é pelo Discord do RenoDX. |
| **Merserk/dlss5-visual-enhancer issue #7** (Koaxz, 2026-09-01) | aberto, sem resposta | pede slider "1x–10x passes" para batch/offline; sem parâmetros por passada | [FATO] |
| **1-Click-DLSS5 issue #11** (mcghjbcg, 2026-09-03) | aberto, sem resposta | "stacking several layers ... is excellent"; anexa imagem e vídeo; "It doesn't work for me no matter what I do" | [FATO] |

Repositórios varridos sem menção a segunda passada: `2600th/dlss5-video-player` (YouTube por URL, tecla D alterna original/neural, sem `passes`), `Zonnery/dlss5-nr-player` (`--preset 3 --intensity --tone --structure --skin --mask --style natural|cinematic`, lado a lado original|NR, sem `passes`), `Blueforcer/ComfyUI-DLSS5-Enhancer`, `lisitskyaa/ComfyUI-DLSS5-NR`, `faisalkindi/DLSS5oneclick`, `NIGos/dlss5-bridge`, `Saganaki22/obs-dlss5-nr`. Nos guias de imprensa (DSOGaming, Tech4Gamers, GameRant, MobiDevices/GamerInVoid) não há "double pass".

**Conclusão da varredura [FATO]:** ninguém publicou, em texto indexável, **parâmetros por camada** para duas passadas. Os únicos números são os do conversor (§1.3) e do webcam-demo. Os vídeos de "x2" usam parâmetros iguais nas duas passadas (é o que o toolkit permite) e mostram GTA V, CS2 e cutscenes. As duas críticas recorrentes são flicker (Pandorian Grey) e artefatos/custo (K3RIA); a promessa é "imagem mais limpa".

---

## 2. Que parâmetros a comunidade muda na segunda passada

### 2.1 Alex's Toolkit (Extreme / Extreme Plus) [FATO]

- Só três chaves: `enabled=1`, `two_pass=1`, `three_pass=0` (`feeder-DEPLOY-DEV.md:303-307`). Interpõe o NGX e roda o **mesmo** RenoDX, com os **mesmos** sliders, 2 ou 3 vezes. Não há parâmetro por passada.
- Os "adaptive blend controls" da v0.9 eram experimentos que o sucessor descartou: na migração do DFC, "removed blend, material, and environment experiments are ignored" (`dfc-README.md:466-469`).
- Logo, o "Extreme" da comunidade = **parâmetros idênticos**, e a diferença que reportaram (mais "detalhe/profundidade" offline; flicker e borrão em jogo) veio disso mesmo.

### 2.2 Deep Fried Chicken (layers / Passes) [FATO]

- Cada `Pass N` tem: NR Preset (Default/#1/#2/#3), NR Style (Default/Natural/Cinematic), NR Intensity, Local Tone Strength, Local Structure Strength, Skin Structure Strength, Automatic Mask, NR UI Correction, Depth Convention, Motion Scale X/Y e um botão de reset temporal (`dfc-README.md:249-266`).
- Padrão "golden": `Passes=1`, tudo Default, **as quatro forças em 2,0**, Auto Mask e UI Correction off (`dfc-README.md:297-300`). Ao subir para 2+, cada passada nova nasce com esses mesmos valores; o Quick-Start manda "Increase passes one at a time" (`dfc-QUICK-START.md:135`).
- Cor: Paper White Scale / HDR Transfer / Color Strength são **uma seção só**, envolvendo a cadeia inteira, "not duplicated under each pass" (`dfc-README.md:305-312`). Profundidade e MV "are never cascaded, fabricated, or rewritten" (`dfc-README.md:287-288`) — a 2ª passada recebe cor da 1ª e guias do jogo.
- As duas ferramentas que o DFC criou para multipass **existirem** dizem o que dá errado com parâmetros iguais:
  - **Clean Fry** (só a partir de 2 passadas): "suppresses broad halos or accumulated colour drift", "can reduce per-frame halo/ringing and colour buildup, but it cannot fully repair temporal ghosting"; começar em Cleanup 0,35 / Detail Retention 0,85 e subir "while watching fine text, hair, foliage, and moving edges" (`dfc-README.md:339-360`; `dfc-QUICK-START.md:141-144`).
  - **Post-process: restore native tone & color, keep neural detail**: pega só a luminância neural (limitada a ±0,50 stop) e aplica sobre o frame nativo, mantendo as razões RGB do jogo (`dfc-README.md:320-329`).
- Memória: cada intermediário privado 4K RGBA16F = 63 MiB; "Passes 11 through 30 are explicitly an extreme VRAM/TDR stress mode" (`dfc-README.md:406-411`).

### 2.3 Relatos com RenoDX e ferramentas offline [FATO]

- DLSS5-Image-Converter (roda o RenoDX real num harness D3D12): "**Lower the strengths for the second pass** rather than repeating the first" (`conv-README-main.md:230-231`). Também: "Settings are read **once, at add-on load**. Flipping the ini mid-run does nothing" (linha 405) — o RenoDX lê `[RenoDX.DLSS5]` na carga; confirmado no nosso log (`bin-camada2\ReShade.log:45`, "active settings ... intensity=2.000000 ... preset=0 style=0").
- 1-Click-DLSS5 (perfis publicados nos releases): v1.2.2 usa `NRIntensity=0.85` ("community sweet-spot"), `NRSkinStructure=-0.50` ("facial smoothing to reduce harsh pores"), `NRAutoMask=1`, `NRUICorrection=1`; v2.5.1 usa `NRGlobalTone=0.9, NRLocalStructure=0.44, NRLocalTone=1.22, NRSkinStructure=1.16`. Ou seja, a comunidade de **uma** passada já roda bem abaixo de 2,0; o app roda tudo no teto (`bin\ReShade.ini` `[RenoDX.DLSS5]`).
- FF7R-DLSS5: "NR Intensity: 1.00 to 1.05 (default 1.01). Higher is more aggressive relighting".
- paradrew (guia de instalação): valores iniciais "NR Intensity at 0.5, enable Automatic Mask, cycle through NR Preset"; verificação: `signed DLSSNR 310.8.0 D3D12 runtime initialized`, `created inline NR resources`, `inline feature 18 evaluation succeeded (count=60)` "count climbing confirms active operation".

### 2.4 "Constrain output to input": a 2ª passada igual muda pouco? [FATO + INFERÊNCIA]

- [FATO] NVIDIA: "The neural rendering result remains firmly grounded in the engine's base frame"; página de pesquisa: "consistency supervision ... grounds generation to remain faithful to the authored scene"; webcam-demo: "designed to constrain its output to the input".
- [FATO] Medido: a força satura ("Output keeps changing all the way up [to 2.0] and is identical at 3.0", `conv-README-main.md:390-392`), e a 2ª passada acrescenta 0,035 sobre 0,057.
- [INFERÊNCIA] Com a 1ª camada já em 2,0 em tudo, a 1ª passada já entregou o máximo que o modelo faz **daquela** entrada. A 2ª passada recebe uma imagem "já fotográfica", com menos ruído e mais contraste local; o modelo tem menos para corrigir e repete a projeção: resultado modesto em estrutura, cumulativo em brilho/frio. Isso bate com todas as medições acima. Não bate com "diferença zero" — zero só sai quando a rede não roda, que foi o caso.

### 2.5 NR reaplicado em vídeo que já tinha DLSS 5 (o caso "ficou bem melhor" do autor)

O autor relata que pegou um vídeo gravado com DLSS 5 e passou o app por cima, e o ganho foi claro. Não achei relato público idêntico (busca por "already had DLSS 5", "run it again", "re-processed", trailer da NVIDIA reprocessado etc. não devolveu nada). O que existe e explica o resultado:

- [FATO] O conversor mede exatamente isso em imagem: realimentar a saída "compounds" (+0,035 sobre 0,057) e "That is what makes it worth having **on a flat render**" (`conv-README-main.md:227-231`). Um vídeo gravado, comprimido e reescalado é uma entrada "achatada": perdeu microcontraste no encoder.
- [FATO] ComfyUI-DLSS5-Enhancer: "Overall detail energy drops, because generator noise disappears, while skin, hair and fabric gain structure". A rede **remove ruído/artefato de compressão** e devolve estrutura em pele/cabelo/tecido.
- [FATO] K3RIA promete das passadas extras "une image encore plus **propre**".
- [FATO] Todas as ferramentas de vídeo da cena (Merserk Live mode para YouTube/Twitch, 2600th player por URL do YouTube, Zonnery player, OBS filter) rodam NR **sobre vídeo já codificado**, e é onde a comunidade acha o efeito mais fácil de mostrar; o webcam-demo explica o porquê pelo lado oposto: "a raw webcam frame is already a photograph, so there is far less for NR to add" — e um vídeo de jogo comprimido está entre os dois.
- [INFERÊNCIA] Por que o vídeo "com DLSS 5" melhora de novo e o jogo ao vivo pareceu não melhorar: (1) no vídeo, o 2º DLSS 5 é na prática a **1ª passada sobre aquela entrada** (a passada original ficou "assada" no arquivo, com blocos de compressão por cima), então o app está fazendo o trabalho de denoise + estrutura que mede 0,057, não o de 0,035; (2) no teste em jogo, a 2ª camada nunca chegou a rodar (§4). Não é evidência de que a 2ª passada ao vivo será tão visível quanto no vídeo; a expectativa correta para jogo é a da §1.3 (~60% do 1º salto, em close).
- [INFERÊNCIA] Consequência prática: a fonte que melhor mostra a 2ª camada do app é **vídeo comprimido** (YouTube/gravação), não o jogo cru moderno. Para o roteiro da §5, um vídeo 1080p de entrevista/diálogo serve melhor que um jogo em 4K nativo.

---

## 3. Mapeamento para as chaves `[RenoDX.DLSS5]` da segunda camada

### 3.1 Faixas e significado de cada chave

Fontes das faixas: sliders do próprio add-on espelhados por Merserk/dlss5-visual-enhancer e ComfyUI-DLSS5-Enhancer (Intensity/Local Tone/Local Structure 0,00-2,00, Skin −1,00-2,00, Preset Default/#1/#2/#3, Style Default/Natural/Cinematic, Auto Mask on/off), clamps medidos pelo webcam-demo (2,00 nas quatro forças; 1,00 em Color/Transfer), conversor (Paper White 0-16, HDR Transfer 0-1, Colour Strength 0-1), Feeder README (v4.7 renomeou sliders e adicionou `NRGlobalTone`; aviso sobre `NRStyle=2`), NVIDIA (estrutura vs tom).

| Chave | Faixa | Efeito | Observação para a 2ª camada |
|---|---|---|---|
| `NeuralUplift` | 0/1 | liga o NR; 0 = "bit-identical to a plain DLAA resolve" | já forçado em 1 (`amd/main.cpp:3400`) |
| `NRIntensity` | 0,00-2,00 (satura em 2) | força geral | **principal botão a baixar** na 2ª |
| `NRLocalStructure` | 0,00-2,00 | alta frequência: AO, reflexos, SSS, microdetalhe | é o que dá "textura"; manter alto se o objetivo é detalhe |
| `NRLocalTone` | 0,00-2,00 | baixa frequência: iluminação local, contraste | é o que **acumula brilho**; baixar na 2ª |
| `NRGlobalTone` | 0-2 (v4.7; app usa 2) | tom global / resposta de cor ampla | acumula; baixar na 2ª |
| `NRSkinStructure` | −1,00-2,00 (−1 = padrão do runtime) | estrutura de pele; precisa de `NRAutoMask=1` para saber onde há pele | 2,0 em duas passadas = poros/rugas exagerados ("plastic"); comunidade usa −0,5 a 1,16 |
| `NRStyle` | 0 Default · 1 Natural · 2 Cinematic | "large effect": Cinematic ~50% mais longe da fonte; Natural suaviza, Cinematic aprofunda sombras | **`NRStyle=2` tem relato de crash no boot seguinte** (Feeder README, v4.6+); 1 é seguro |
| `NRPreset` | 0-3 | "inert with upscaling off: all four presets measured bit-identical" | inútil na nossa cadeia DLAA 1:1; deixar 0 |
| `NRAutoMask` | 0/1 | máscara semântica (pele etc.) | manter 1 |
| `NRUICorrection` | 0/1 | protege HUD/UI de deslocamento de cor | manter 1 |
| `NRColorStrength`, `NRTransferStrength` | 0-1 | codec de cor reversível (ponte HDR) | manter 1; não são "força do efeito" |
| `NRPaperWhiteScale`, `NRDiffuseWhiteNits` | 0-16 / nits | só HDR | irrelevante em SDR |
| `NRDepthMode` | 0 flag do jogo · 1 normal · 2 invertida (nomenclatura do DFC "Depth Convention") | convenção de profundidade | manter igual à 1ª |
| `NREnableUpscaling` | 0/1 | upscaling do NR | inócuo: "NR upscaling is not applicable ... native path" (`bin\ReShade.log:151`) |

Nota sobre a numeração de `NRStyle`: a ordem Default/Natural/Cinematic = 0/1/2 é a das listas do DFC, do Merserk, do OBS e do ComfyUI. Um release do 1-Click chama `NRStyle=1` de "Cinematic", o que contradiz as demais fontes; [INFERÊNCIA] é rótulo trocado no changelog. Trato 1 = Natural.

### 3.2 Bloco proposto para `bin-camada2\ReShade.ini`

Dois blocos, para dois momentos. O **A** é para o autor **ver que funciona** (mesma força, diferença máxima). O **B** é o que eu colocaria como padrão da segunda camada, seguindo o que a comunidade faz para não virar plástico.

**Bloco A — prova de vida (mesmos parâmetros da 1ª):** não mudar nada. É o que está hoje. Diferença esperada: ~60% da 1ª passada em estrutura, +5 de luminância, tom um pouco mais frio. Visível em close de rosto/tecido a 100% de zoom; imperceptível em plano geral.

**Bloco B — segunda camada "de acabamento" (recomendado como padrão):**

```ini
[RenoDX.DLSS5]
EnableHooks=2
NeuralUplift=1
NRIntensity=1.2
NRLocalStructure=2
NRLocalTone=0.6
NRGlobalTone=0.6
NRSkinStructure=0.8
NRStyle=1
NRPreset=0
NRAutoMask=1
NRUICorrection=1
NRColorStrength=1
NRTransferStrength=1
NRDepthMode=0
NREnableUpscaling=1
NRDiffuseWhiteNits=500
NRPaperWhiteScale=16
```

Justificativa por chave:

- `NRLocalStructure=2` mantido: é a única coisa que a 2ª passada pode acrescentar de útil (microestrutura: 1,29x → 1,41x no webcam-demo). Estrutura não é o que deriva.
- `NRLocalTone=0.6`, `NRGlobalTone=0.6`: são as chaves de baixa frequência, e é aí que a deriva acumula (+5,4 → +10,1 de luminância). Cortar o tom na 2ª é o equivalente barato do "restore native tone & color, keep neural detail" do DFC.
- `NRIntensity=1.2`: "Lower the strengths for the second pass rather than repeating the first" (conversor). 1,2 fica acima do "sweet spot" de uma passada da comunidade (0,85-1,05) porque a nossa entrada já está filtrada e o objetivo é ainda mostrar diferença.
- `NRSkinStructure=0.8`: 2,0 duas vezes seguidas em pele é o modo de falha que o webcam-demo manda vigiar ("facial change is a failure mode"). 0,8 é o meio do que a comunidade publica (−0,5 a 1,16).
- `NRStyle=1` (Natural): dá à 2ª passada um **caráter** diferente da 1ª (Default) em vez de "mais do mesmo"; Natural é a direção que suaviza, o que compensa o acúmulo. Não usar 2 (Cinematic) por causa do relato de crash no boot seguinte (Feeder README) e porque aprofunda sombras, somando à deriva.
- `NRPreset=0`: inerte em DLAA; mexer não faz nada e confunde a comparação.
- Resto igual à 1ª camada.

**Bloco C — variante "só limpar/assentar", para quem achar o B ainda forte:** `NRIntensity=0.8`, `NRLocalStructure=1.5`, `NRLocalTone=0.3`, `NRGlobalTone=0.3`, `NRSkinStructure=0`, `NRStyle=1`. [INFERÊNCIA] deve ficar quase invisível em cena parada e servir só para estabilidade temporal; é a versão que menos faz sentido pagar duas redes por.

### 3.3 Onde isso entra no código (sem eu mexer)

- [FATO] `Camada2Espelhar` copia por cima `reshade.ini` a cada `Camada2Iniciar` (`amd/main.cpp:3346-3350, 3371, 3391`). `Camada2Iniciar` já escreve `NeuralUplift=1` depois da cópia (`amd/main.cpp:3396-3400`). O lugar dos valores do bloco B é **logo abaixo dessa linha**, com `WritePrivateProfileStringW(L"RenoDX.DLSS5", ...)` para cada chave.
- [FATO] O RenoDX lê as chaves na carga do add-on (`conv-README-main.md:405`; `bin-camada2\ReShade.log:45`). Escrever antes do `CreateProcessW` basta; mudar depois exige reabrir a 2ª camada.
- [INFERÊNCIA] Vale um chip no painel "2ª camada: igual / acabamento" que só troca o bloco escrito, porque o teste A (prova de vida) e o B (uso) precisam dos dois.
- Deriva de cor residual: o webcam-demo corrige com ganho por canal **depois** da rede e mostra que isso não custa estrutura (1,30x → 1,32x). Aqui o candidato é o `DLSS5_Grade.fx` da **segunda** camada (a primeira mantém o dela). Não mexer no que a rede vê.

---

## 4. Sinais nos logs da segunda camada de que a rede NÃO transformou a imagem

Tabela dos cinco lançamentos (`bin-camada2\dlss5forall.log`):

| # | Início → fim | Duração | Frames apresentados | `neural na abertura` | Liberada do bypass? | Rede rodou? |
|---|---|---|---|---|---|---|
| 1 | 01:48:06 → 01:48:15 (l. 2-32) | 8 s | 77 | estado=1 (l. 26) | não | não |
| 2 | 01:48:20 → 01:50:05 (l. 34-66) | 105 s | 241 | **estado=0, neural desligado** (l. 58, 64) | sim, aos 7,6 s (l. 65) | **não (NR off)** |
| 3 | 01:59:52 → 01:59:54 (l. 68-97) | 1 s | 12 | estado=1 | não | não |
| 4 | 01:59:56 → 02:00:02 (l. 99-129) | 6 s | 62 | estado=1 | não | não |
| 5 | 02:00:11 → 02:00:14 (l. 131-161) | 3 s | 20 | estado=1 | não | não |

Os lançamentos 3, 4 e 5 (01:59:52, 01:59:56 e 02:00:11) são o teste em jogo relatado pelo autor: a segunda instância foi fechada pelo botão do painel **1, 6 e 3 segundos** depois de abrir (`bin\dlss5forall.log:52-58, 71-74`), **antes** de qualquer `aquecimento: camada liberada` e sem nenhum `frame N delivered` no `dlss5-feed.log` dela. A segunda passada nunca chegou a aparecer na tela; o que o autor viu foi a 1ª camada através do bypass da 2ª.

Evidências diretas [FATO]:

- `bin-camada2\dlss5-feed.log` (só o último lançamento sobrevive, o Feeder trunca): sessão D3D12 abre às 02:00:13.378, `holding the feature (re)build for 60 frames` (l. 16), `effect runtime destroyed` às 02:00:14.626 (l. 17). **Nunca houve `building`, `feature ready`, `frame N delivered`.** Compare com `bin\dlss5-feed.log:16-27`: `holding` 01:59:08 → `building` 01:59:15 → `feature ready` → `frame 1 delivered` 01:59:16.
- `bin-camada2\ReShade.log` (idem, só o último): RenoDX carregou (l. 44-46, `intensity=2.000000 ... enabled=ON`), instalou hooks (l. 116-126), mas **não há** `NGX feature create intercepted`, `feature 18 created`, `inline feature 18 evaluation succeeded`, `created inline NR resources`. Na 1ª camada essas linhas estão em `bin\ReShade.log:144-157`.
- `Encerrando (apresentados=20)` em 3 s: o Feeder precisava de 60 presents só para começar a construir o feature.
- Lançamento 2: `estado=0 (o add-on está carregado, mas o neural está desligado)` e `aviso: neural nao esta ativo`. [INFERÊNCIA] É exatamente o bug que o comentário em `amd/main.cpp:3396-3399` descreve ("se a pessoa estava com o DLSS 5 desligado (F6), a segunda camada nasceria com o neural desligado tambem e nao faria nada"); o exe foi recompilado às 01:52 (timestamp de `bin\DLSS5ForAll.exe`), depois desse teste. Nos lançamentos 3-5 o `estado=1` confirma a correção — mas aí o tempo não deu.
- Todas as cópias de `ReShade.ini` (`bin-camada2\ReShade.ini` e os três `.bak-keyoverlay-*`) têm `NeuralUplift=1`, `NRIntensity=2`, `NRStyle=0`, `NRPreset=0`: nenhuma pista de parâmetro zerado. O problema não é configuração; é tempo.

Por que 60 frames demoram tanto na 2ª camada [FATO + INFERÊNCIA]:

- [FATO] `create_delay=60` em `bin-camada2\dlss5-feed.cfg`, contado em presents ("frames to hold a feature (re)build after a runtime (re)init", Feeder README).
- [FATO] A 1ª camada estava a 7,6 fps naquele trecho (`bin\dlss5-feed.log:30`; `bin\dlss5forall.log:77-78`: chegada 3,3 fps, WGC com intervalo médio de 224 ms e máximo de 37 s — fonte parada).
- [INFERÊNCIA] A 2ª camada só recebe frame novo quando a 1ª apresenta; a 3-8 fps, 60 presents são 8-20 s, mais ~1,5 s de sessão D3D12 e ~1 s para o primeiro frame. O autor nunca esperou mais de 8 s com o neural ligado.
- [FATO] Nada avisa: a 2ª camada não mostra cartaz (`amd/main.cpp:2954`); o painel da 1ª só muda para "ATIVA" ao ler `aquecimento: camada liberada` ou `overlay colada` no log da 2ª (`amd/main.cpp:8175-8185`), e a linha do painel diz "aquecendo" até lá (`amd/main.cpp:4340-4341`).

Dois detalhes correlatos que valem correção:

- [FATO] A detecção de "ATIVA" casa também com `aquecimento: camada liberada apos N ms (TETO DE 30 s - a cadeia nao respondeu)`: se a 2ª camada estourar os 30 s **sem** a rede, o painel diria "ATIVA" mostrando bypass. Checar `(a cadeia entregou)` em vez do prefixo.
- [FATO] `CadeiaEntregando()` aceita ` delivered (` do Feeder (feature 1, DLAA) como "entregou" (`amd/main.cpp:3742-3748`). No lançamento 2 isso liberou a camada com o NR desligado. Para a 2ª camada o critério deveria ser `evaluation succeeded` do RenoDX no `ReShade.log` dela — é o único sinal de que a **rede** rodou.
- [INFERÊNCIA] F6 físico: o RenoDX v4.6+ lê a tecla por conta própria ("O RenoDX le a tecla fisica TAMBEM por fora da nossa hotkey", comentário em `amd/main.cpp` no bloco "o neural ficou como pedimos?"; Feeder README: no `host64` ele desliga os "global hotkeys" para "a gameplay keypress cannot silently toggle neural rendering in a background process"). Com duas instâncias, um F6 no teclado alterna o NR das **duas**; o botão do painel (ponte, sem teclado) alterna só a 1ª. Bom para "tudo vs nada", ruim para "1 vs 2 passadas". O mesmo vale para F5: um toque deve gerar pares em `bin\DLSS5 Screenshots\` (0 vs 1 passada) **e** em `bin-camada2\DLSS5 Screenshots\` (1 vs 2 passadas) — se confirmado, é a melhor escada de comparação possível com uma tecla.

---

## 5. Roteiro de teste de 10 minutos

Pré-requisitos: fonte **Janela**, "Esconder esta janela" desmarcado (é o que `camada2Ok` exige, `amd/main.cpp:7437`); GPU RTX 4080 a 1080p aguenta as duas redes com folga. Use uma fonte com **rosto humano em close, parado ou quase** (cena de diálogo de jogo, ou um vídeo 1080p de entrevista no YouTube em tela cheia dentro da janela). Fonte comprimida vale, e pela §2.5 é até a que mais mostra; espere "mais limpo e claro", não "mais detalhado". Dois testes em sequência dão a resposta completa: (a) o **vídeo gravado com DLSS 5** que o autor já viu melhorar, agora com a 2ª camada armada, para ter uma referência de "diferença grande"; (b) o **jogo**, para ver a diferença real de 2 passadas ao vivo.

1. **(0-1 min) Subir a 1ª camada** e esperar o cartaz "FILTRO LIGADO". Confirmar no `bin\ReShade.log` a linha `inline feature 18 evaluation succeeded (count=...)` com o count subindo.
2. **(1-2 min) Manter a fonte em movimento** (vídeo tocando) antes de ligar o DLSS 2x: o Feeder da 2ª camada conta 60 **presents** e a 2ª só apresenta quando a 1ª apresenta. Fonte parada = espera longa.
3. **(2-3 min) Clicar "DLSS 2x"** e **não mexer por pelo menos 15 s; 40 s se a fonte estiver parada** (no teste do autor a 2ª camada foi fechada com 1, 3 e 6 s, antes de armar). O sinal mínimo é a linha `aquecimento: camada liberada apos N ms (a cadeia entregou)` no `bin-camada2\dlss5forall.log`; o sinal completo é `frame 1 delivered` no `bin-camada2\dlss5-feed.log` **e** `inline feature 18 evaluation succeeded (count=1)` no `bin-camada2\ReShade.log`. Só a partir daí existe segunda passada. O painel da 1ª deve passar de "aquecendo" para "segunda camada ATIVA" e o cartaz "DLSS 2x ATIVO" aparecer. Se em 40 s não vier: o teste é inválido, olhar o log da 2ª (procurar `holding`, `create_delay`, fps). Se a linha vier com `TETO DE 30 s`, também é inválido: a camada foi liberada sem rede.
4. **(3-4 min) Pausar a fonte** num close de rosto (ou tecido/cabelo). Deixar 2-3 s para os históricos das duas redes assentarem (a 2ª tem histórico próprio e converge depois da 1ª).
5. **(4-6 min) Capturar a escada de imagens**, mesmo frame:
   - Print da tela (Win+Shift+S ou PrintScreen do Windows): o que se vê = **2 passadas** (a janela da 2ª cobre a 1ª).
   - Apertar **F5** uma vez e recolher os pares em `bin\DLSS5 Screenshots\` (0 e 1 passada) e em `bin-camada2\DLSS5 Screenshots\` (1 e 2 passadas). Se a 2ª pasta não receber par, usar o print da tela como "2".
   - Alternativa sem F5: fechar o DLSS 2x pelo painel, print (= 1 passada), religar e esperar de novo os 40 s. Mais lento, mas inequívoco.
6. **(6-8 min) Comparar a 100% ou 200% de zoom**, alternando as imagens na mesma posição (visualizador de fotos com setas, ou duas abas). Olhar, nesta ordem: poros/textura da pele na testa e bochecha; separação de olhos, sobrancelha e lábios; sombra de contato no pescoço/gola; dobras de tecido; fios de cabelo contra fundo. Depois olhar o **brilho médio** (a 2ª deve estar visivelmente mais clara e um pouco mais fria) e halos em bordas de alto contraste (texto, HUD).
7. **(8-9 min) Movimento:** voltar a tocar o vídeo, fazer um corte de cena. Esperado: a imagem "assenta" em dois tempos (1ª rede e depois 2ª), rastro atrás de movimento rápido maior que com uma camada, possível flicker em superfícies planas de baixo contraste.
8. **(9-10 min) Repetir o passo 5-6 com o bloco B** (quando estiver implementado no `Camada2Iniciar`) e julgar: a estrutura ganhou o mesmo, o brilho acumulou menos?

O que esperar, em números de referência: a 2ª passada muda cerca de 60% do que a 1ª mudou (0,035 vs 0,057 de diferença média), sobe a luminância mais ~5 pontos e a estrutura em ~10% sobre a 1ª. A olho nu: perceptível em close a 100%, nulo em plano geral em fit-to-window. Se nem em close aparecer nada **com** `evaluation succeeded` subindo na 2ª, aí sim vale investigar formato/cor da entrada da 2ª (B8G8R8A8 SDR, `bin\dlss5-feed.log:26`) e o `DLSS5_Feed.fx` dela; hoje não há motivo para suspeitar disso.

---

## 6. Recomendações objetivas para o time

1. **Antes de tudo, refazer o teste esperando a 2ª camada armar** (passo 3). Todo o resto é secundário a isso.
2. **Corrigir o tempo de aquecimento da 2ª camada:** (a) enquanto `aquecendo`, a 2ª camada apresentar num ritmo mínimo (30 Hz) independente da fonte, para o `create_delay` de 60 frames passar em 2 s em vez de 20; ou (b) gravar `create_delay` menor no `dlss5-feed.cfg` da `bin-camada2\` depois do `Camada2Espelhar` — com risco: o Feeder avisa que "calling in too early can crash", embora o v4.7 seja "per-present rescan, lazy feature adoption". Prefiro (a).
3. **Feedback:** o painel da 1ª deveria mostrar "2ª camada: aquecendo (N s, esperando 60 frames)" e só "ATIVA" com `evaluation succeeded` no `bin-camada2\ReShade.log`; tratar `TETO DE 30 s` como falha.
4. **Parâmetros da 2ª camada:** escrever o bloco B em `Camada2Iniciar` logo após o `NeuralUplift=1`; oferecer o bloco A como opção "igual à primeira" para quem quer o máximo. Não usar `NRStyle=2`. Não perder tempo com `NRPreset`.
5. **Deriva de cor:** compensar no `DLSS5_Grade.fx` da 2ª camada (ganho por canal pós-rede), como o webcam-demo, depois de medir a diferença de luminância com o par de screenshots.
6. **Comparação embutida:** um botão "comparar 1x/2x" que esconde a janela da 2ª camada por 1 s (sem fechar o processo, preservando o histórico) daria o antes/depois que hoje não existe sem reiniciar a 2ª e esperar de novo.

---

## Fontes

Locais (somente leitura):
- `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\amd\main.cpp`: 748-757 (comentário do modo), 2954, 3333-3418 (`Camada2Pasta/EhConfig/Ignorar/Espelhar/Iniciar`), 3396-3400 (força `NeuralUplift=1`), 3420-3455, 3626-3660 (`LerEstadoNeural`), 3742-3748 (`CadeiaEntregando`), 4340-4341 (texto do painel), 5763, 6472-6474, 7437, 7632-7635, 8158-8188 (estado da 2ª camada pelo log).
- `...\DLSS5ForAll-AMD\bin-camada2\dlss5forall.log` (161 linhas; linhas citadas na tabela da §4), `dlss5-feed.log` (19 linhas; 16-18), `ReShade.log` (149 linhas; 44-52, 116-126, 129), `ReShade.ini` e `ReShade.ini.bak-keyoverlay-20260905-0148/0159/0200` (`[RenoDX.DLSS5]`), `dlss5-feed.cfg`, `dlss5forall.ini`, `FafaDLSS5.ini`.
- `...\DLSS5ForAll-AMD\bin\dlss5-feed.log` (16-30), `dlss5forall.log` (47-88), `ReShade.log` (46-52, 144-171), `ReShade.ini`.
- `..\pesquisa-dlss10\RELATORIO-DLSS10.md`, `webcam-demo-README.md`, `feeder-README.md` (85-110, 165-200, 380-425, 840-900), `feeder-DEPLOY-DEV.md` (293-330).
- Nesta pasta: `dfc-README.md` (1-20, 225-300, 305-360, 404-430, 460-470), `dfc-QUICK-START.md` (125-145), `dfc-COMPATIBILITY-TEST.md` (170-215), `conv-README-main.md` (14-30, 150-165, 176-235, 255-262, 385-410).

Web:
- Deep Fried Chicken 1.4.0 (docs espelhados): https://github.com/jlrouzies-fr/DLSS5-Feeder/tree/main/external/deepfried/v1.4.0-alpha
- DLSS5-Feeder: https://github.com/jlrouzies-fr/DLSS5-Feeder (README, DEPLOY-DEV.md §8, FEEDBACK-DFC.md)
- DLSS5-Image-Converter (medições de estilo, preset, força, segunda passada): https://github.com/criso2hd-alt/DLSS5-Image-Converter
- jpneagle/dlss5-webcam-demo: https://github.com/jpneagle/dlss5-webcam-demo
- ComfyUI-DLSS5-Enhancer (faixas dos sliders, "detail energy drops"): https://github.com/Blueforcer/ComfyUI-DLSS5-Enhancer
- ComfyUI-DLSS5-NR (skin −1 = padrão do runtime): https://github.com/lisitskyaa/ComfyUI-DLSS5-NR
- Merserk/dlss5-visual-enhancer (faixas 0-2, skin −1-2; issue #7): https://github.com/Merserk/dlss5-visual-enhancer ; https://github.com/Merserk/dlss5-visual-enhancer/issues/7
- 1-Click-DLSS5 releases (perfis `NRIntensity=0.85`, `NRSkinStructure=-0.50`, `NRGlobalTone=0.9` etc.; F5/F6): https://github.com/reiluisii/1-Click-DLSS5/releases ; issues #1 e #11
- FF7R-DLSS5 (NR Intensity 1,00-1,05): https://github.com/zhubaohi/FF7R-DLSS5
- obs-dlss5-nr (Style/Preset 0-3/Reset History): https://github.com/Saganaki22/obs-dlss5-nr
- paradrew, "How to install DLSS 5: five traps" (verificação pelo log, valores iniciais): https://paradrew.com/en/blog/kak-ustanovit-dlss-5/
- NVIDIA, DLSS 5 em NBA 2K27 (o que muda; Structure/Tone Intensity; masks): https://www.nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/
- NVIDIA Research, DLSS 5: https://research.nvidia.com/labs/adlr/DLSS5/
- igor'sLAB, Gamescom 2026 (Structure/Tone Intensity, masks, model variants): https://www.igorslab.de/en/dlss-5-gamescom-2026-3d-guided-neural-rendering/
- HotHardware (método, close-ups, "easier to tell by the frame rate"): https://hothardware.com/news/nvidia-dlss-5-neural-rendering-tested
- Guru3D p.3 (rostos, método de comparação): https://www.guru3d.com/review/nvidia-dlss-5-review-with-nba-2k27/page-3/
- MindStudio hands-on (mods; sliders; presets): https://www.mindstudio.ai/blog/dlss-5-neural-rendering-hands-on
- Vortex Gaming (descrição dos estilos): https://vortexgaming.io/en/postdetail/1254244
- Pandorian Grey, 4.55/4.6/4.7/DFC (flicker em multipass): https://www.youtube.com/watch?v=MEP4D99RNJ0
- Papryk Szadolowski, "GTA V DLSS 5 x2 Stacked double render experiment" (68 mil views): https://www.youtube.com/watch?v=4PaLeter4sY
- Exozze, "DLSS 5 - x2 (Double Pass) - Counter-Strike 2": https://www.youtube.com/watch?v=xOy0lwCWsiw
- K3RIA, "DLSS 5 Double Pass X2 : INSTALLATION FACILE" (pacote no Drive, aviso sobre passes/artefatos, comparação em cutscene aos 23:15): https://www.youtube.com/watch?v=oWbTiHO1HY0
- Direcreep, "DLSS 5 - 5 times pass over one game...DLSS 10 ?": https://www.youtube.com/watch?v=GGI4wmsuoU4
- ResetEra, "DLSS 5 releases to all users this week" p.18 ("10 instances of DLSS 5 on top of itself", snippet): https://www.resetera.com/threads/dlss-5-releases-to-all-users-this-week-with-supported-hardware.1620217/page-18
- Guru3D, "DLSS 5 in games be looking sick" p.4/p.10 (bloqueado; só snippet): https://forums.guru3d.com/threads/dlss-5-in-games-be-looking-sick.461576/page-10
- Stamos no X ("deep fried chicken addon for 3 passes"): https://x.com/Stamosxd/status/2094647012847218854
- 2600th/dlss5-video-player (YouTube por URL, tecla D original/neural): https://github.com/2600th/dlss5-video-player
- Zonnery/dlss5-nr-player: https://github.com/Zonnery/dlss5-nr-player
- faisalkindi/DLSS5oneclick: https://github.com/faisalkindi/DLSS5oneclick
- Notebookcheck, análise do WhizzDumbPlayz (método quadro a quadro): https://www.notebookcheck.com/YouTuber-zeigt-warum-DLSS-5-kein-KI-Slop-ist.1264316.0.html
- MobiDevices, teste do GamerInVoid em 5 jogos ("high neural processing intensity", rostos envelhecidos): https://mobidevices.com/en/dlss-5-tested-games
- Nexus 3706 (Stellar Blade) e 2224 (bloquearam a leitura; usados só pelo snippet sobre F5/F6 e perfis): https://www.nexusmods.com/stellarblade/mods/3706 ; https://www.nexusmods.com/site/mods/2224

Páginas que bloquearam a leitura automática (403/404): TechPowerUp p.2/p.3, Wccftech, Nexus 2224/3706, `renodxdlss5/RR-and-DLSS-5-RenoDX-for-the-Games`, descrição do YouTube. O que veio delas está marcado como snippet.
