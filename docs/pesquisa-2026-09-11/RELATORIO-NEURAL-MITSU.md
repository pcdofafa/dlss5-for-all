# Relatório: o concorrente "Neural Mitsu" (Preview, build multilang-20260910)

Data: 2026-09-11. **FASE 1 — estudo remoto, sem o instalador.** Nada foi baixado do produto além de
página, folha de estilo, dois scripts e imagens públicas do site. **O `NeuralMitsuSetup.exe` não foi
baixado nem executado** — isso é a Fase 2, quando o usuário trouxer o arquivo para
`C:\Users\PCDOFAFA\Downloads\neural mitsu\`.

Fontes desta fase: `https://neuralmitsu.com/` (HTML, `app.js`, `site-config.js`, `robots.txt`,
`assets/*.webp`), a API pública de convites do Discord, a página do LumeniteFX, a documentação
pública do DLSS5-Feeder e a nossa própria árvore (`amd\docs\ARQUITETURA.md`, `main.cpp`, memórias).
O modelo de estudo é o `RELATORIO-NEURAL-SCREEN.md`, do mesmo dia.

**O achado que decide o relatório:** a captura de tela oficial do produto (`assets/app-preview.webp`)
mostra o card **Frame Gen** com `Tipo: LSFG 3.1`, `Modo: Fixo`, `Multiplicador: 2x`, `Escala de
Fluxos: 70%` e `Desempenho: Ativado`. **LSFG 3.1 é o Lossless Scaling Frame Generation 3.1**, o motor
do aplicativo pago *Lossless Scaling* (THS, Steam) — `Flow Scale`, `Performance`, `Fixed/Adaptive` e
o multiplicador são os nomes literais dos controles dele. O "Frame Gen com renderização neural"
anunciado no card **não passa pela rede da NVIDIA**: é interpolação de fluxo óptico de terceiro,
embutida no aplicativo deles.

> ### ⚠️ CORREÇÃO — a Fase 2 desmentiu este achado
>
> A análise do binário (Parte II, abaixo) **não encontrou nenhum vestígio** de LSFG, Lossless
> Scaling, FSR, DLSSG, Streamline ou OFA em nenhum arquivo do instalador. O frame gen da build
> distribuída (**0.21**) é **implementação própria**, em HLSL legível dentro do
> `MitsuFGPresenterLite.exe`, e o "Dimensionamento" é **bilinear com nitidez adaptativa**, não FSR.
> A captura de tela publicada no site **não corresponde à build que o site entrega**. O que
> permanece válido da Fase 1: o frame gen não é neural e não usa a rede da NVIDIA. Leia a Parte II
> antes de usar qualquer número ou conclusão da Parte I sobre o motor de frame gen.

---

## Tabela-resumo

| Aspecto | Neural Mitsu (Preview) | DLSS5 For All | Quem leva |
|---|---|---|---|
| Natureza | **Montador**: interface que orquestra 4 peças de terceiros (DFC, DLSS, DLSS-NR, motion shader) + motor de frame gen de terceiro | **Cadeia própria**: captura WGC + ReShade + `dlss5-feed` + NR próprio (`Reserved18`) + ponte | Nós (controle do que roda) |
| Neural Rendering | **Deep Fried Chicken v1.6.8** (add-on de terceiro, baixado por Discord, `.7z` com senha `chicken`) | **NR próprio dentro do feed**, 1 a 10 passes, `0,35 ms/passe` medido (`ARQUITETURA.md` §10) | Nós (11× mais barato por passe, sem dependência externa) |
| Passes | Os do DFC (1 a 30 na doc v1.4.8; v1.6.8 não verificada) | 1 a 10 no painel (`main.cpp:3905`) | Empate no alcance; nós no custo |
| Frame generation | **LSFG 3.1 (Lossless Scaling)** — interpolação, `Fixo/Adaptativo`, multiplicador até 20×, `Escala de Fluxos`, modo `Desempenho` | **Fluidez 1×/2×/3×** por **extrapolação** (`t` negativo, `main.cpp:16001`) + **DLSS Frame Skipping** com quadro sintetizado por warp de MV | Eles em maturidade do modelo; **nós em latência** (ver §3) |
| Latência do frame gen | Interpolação **segura um quadro** por construção (+1 quadro de atraso) | Extrapolação **não segura nada**; recuo automático se o extra custar mais que a fatia (`main.cpp:16017`) | Nós |
| Upscaling | **FSR** com nitidez, "Versão otimizada", modo Automático e proporção de imagem | DLSS/DLAA pelo feed + `work_resolution` + `DLSS5_Sharpen.fx` | Empate (caminhos diferentes) |
| Motion vectors | `lumenite_QuantMotion.fx` — LumeniteFX, "superfast para hardware fraco", pirâmide ZMSAD, **1/8 da resolução**, saída `RG16F` | `Lumenite_Kernel` (−0,7% a 45 px) e **VME por hardware** (~1 ms na fila de vídeo) | Nós |
| AMD | **Roadmap, "prioridade principal"** — texto admite que será "uma solução não oficial modificada pela comunidade" (é o `DLSS-NR-on-AMD` que já usamos) | **Funciona hoje**: módulo AMD, RDNA3/RDNA4 (`main.cpp:4228-4251`), feeder com portador FSR | Nós |
| Captura | Janela/borderless; tela cheia exclusiva quebra (aviso repetido duas vezes no site) | WGC de janela **ou monitor**; mesma limitação de tela cheia exclusiva | Empate |
| Origem das dependências | 3 de 4 por **MediaFire** e 1 por **Discord**; o site admite que são "cópias das versões compatíveis já testadas" | O nosso setup baixa `dxgi.dll`, `dlss5-feed` e os shaders; **não distribuímos `nvngx_dlssnr.dll`** (`empacotar.ps1:2`) | Nós (procedência) |
| Runtime NR | `nvngx_dlssnr.dll` **310.8.0 oficial** (release `RankFTW/rhi-repo`, zip 104 MB) | `310.8.SF.0` customizada no módulo AMD; `310.8.0.0` em `bin\` | Eles (procedência mais limpa) |
| Instalação | Instalador fechado (Inno/NSIS a confirmar) + importação guiada com validação (chip "SHA-256" no site) | Portable zip + instalador Inno + launcher que escolhe módulo | Empate; a **validação guiada** deles é boa ideia (§4.1) |
| Atualização | Não há sinal de atualização automática; o roadmap pede "reconhecer novas versões do DFC" | Launcher com `versao.json`, SHA-256 e `atualizar.cmd` (`atualizacao.h`) | Nós |
| Comparativo antes/depois | Não visto | Split fixo 50/50 (`addon\comparar_lado_a_lado.h`) | Nós |
| Seletor de placa / HDR / prioridade de GPU / limitador de fps | Não vistos | Todos existem (`main.cpp:4717`, `hdr_control.h`, `desempenho.h`) | Nós |
| Idiomas | Site PT/EN; build chamada "multilang" | Painel e launcher PT/EN (`idioma.h`) | Empate |
| Apresentação ao público | Site profissional, tutorial em 6 passos com prints, contador de downloads, FAQ, roadmap | Vitrine + launcher; sem landing page equivalente | **Eles** |

---

## 1. Identidade

| Item | Evidência |
|---|---|
| Nome / versão | `Neural Mitsu`, `version: "Preview"`, instalador `downloads/NeuralMitsuSetup.exe?v=multilang-20260910` (`site-config.js:2-3`) |
| Autor | canal YouTube **`@MitoGouken`**, apoio em `livepix.gg/mitsu` (`site-config.js:9-10`). O rodapé do app diz `MitsuNeural`; o site, `Neural Mitsu` |
| Marca | gato preto meio a meio (metade "crua", metade realçada) + monograma `NM`; tema ciano sobre azul-escuro |
| Hospedagem | Cloudflare (`CF-Cache-Status: HIT`, `Server: cloudflare`), site **estático** (sem API própria) |
| Telemetria | contador de downloads por serviço público de terceiro: `abacus.jasoncameron.dev/{get,hit}/neuralmitsu-official/setup-downloads` (`app.js:7-10`), e o beacon do Cloudflare Insights (`index.html:343`). Nada além disso na página |
| `robots.txt` | Cloudflare Managed, `Content-Signal: search=yes, ai-train=no, use=reference` — leitura para referência é o que esta pesquisa faz |
| Páginas | **uma só**. `sitemap.xml`, `downloads/`, `assets/` e `changelog.html` devolvem 404. Não há changelog público, não há issues, não há repositório |
| Instalador | `Content-Type: application/octet-stream`, `ETag: "024d1fa0681f0a6799ccbfb79f452be3"` (32 hex — provavelmente o **MD5** do arquivo; serve de conferência na Fase 2). O `Content-Length` não veio na resposta ao HEAD |

Não existe código-fonte público, nem licença publicada, nem rastro de repositório. Diferente do
NeuralScreen (MIT, GitHub, 200 commits legíveis), aqui **não há nada auditável sem o binário**.

## 2. Arquitetura, pelo que o site declara

A cadeia é montada com quatro peças de fora, que o usuário busca uma a uma:

| # | Peça | Origem real | Papel |
|---|---|---|---|
| 1 | **Deep Fried Chicken v1.6.8** | Discord `discord.gg/g2v2XGqvR` → servidor **"Deep Fried Chicken"**, 12.418 membros, canal `📢・new-releases`; convite criado por `alexmurch7` ("Alex"), expira 2026-10-01 | consumidor de Neural Rendering: cascata de passes da `Reserved18` |
| 2 | `nvngx_dlss.dll` | cópia no MediaFire (`wjvzjd92p1l0mk1`) | DLSS super resolution |
| 3 | `lumenite_QuantMotion.fx` | LumeniteFX (umar-afzaal), cópia no MediaFire (`l13640rmfw0826x`) | motion vectors para a rede |
| 4 | `nvngx_dlssnr.dll` | release `RankFTW/rhi-repo` → `DLSS NR 310.8.0`, `nvngx_dlssnr_310.8.0.zip`, 104 MB, `sha256:388c0a7912e15ec911b9c9e11a692…`; o botão do site aponta para cópia no MediaFire (`nx1iyedrxghtc5v`) | a runtime neural |

Somam-se a isso, pelos chips da seção "O projeto": `DFC Provider`, `SHA-256`, `ReShade local`,
`Mitsu Enhancement` (`index.html:86`). "ReShade local" indica o mesmo desenho que o nosso: **ReShade
no processo da camada**, não injetado no jogo — o que é coerente com o FAQ ("processa externamente a
imagem, sem modificar arquivos do jogo") e com o aviso, repetido duas vezes, de usar **Janela ou
Janela sem bordas** porque tela cheia exclusiva "pode impedir a captura ou causar erro de
DXGI/captura".

### 2.1 O Deep Fried Chicken v1.6.8 — o que mudou desde a nossa memória

A memória `o-2x-do-video-e-o-deep-fried-chicken` (07/09) registra a v1.4.8-alpha com **documentação
sem binário** e os binários "indisponíveis". Isso **caducou**:

- O pacote `Deep-Fried-Chicken-v1.6.8.7z` (**17,97 MB**) está postado no canal público de releases,
  em 06/09/2026 20:09, por "Alex". O convite do site é para esse servidor, e ele está aberto.
- O post diz, literalmente, que o `.7z` pede **senha `chicken`**.
- E diz que o autor **começou a construir um feeder próprio**: *"my advice would be to use
  @winterelfeas's feeder for older games, I have only began building out my own … but its far less
  sophisticated, Feeder is still the king"*. Ou seja, o DFC v1.6.8 já tem um caminho próprio para
  jogos sem DLSS nativo, e o próprio autor o considera inferior ao DLSS5-Feeder.
- **Nada na documentação pública do DFC menciona frame generation** — nem interpolação, nem
  extrapolação. Confere com o `FEEDBACK-DFC.md` do feeder, que o descreve como interceptador de NGX
  nativo com pipeline de até 10 passes. **O frame gen do Neural Mitsu não vem do DFC.**

### 2.2 O `lumenite_QuantMotion.fx` — é do mesmo repositório que já usamos

É o LumeniteFX de **umar-afzaal**, o mesmo autor do `lumenite_Kernel.fx` que está no nosso preset.
O cabeçalho do arquivo se descreve como *"Superfast motion vectors for low-end hardware"*: pirâmide
grosso-para-fino com busca ZMSAD começando em 128×, subindo por 64/32/16/8×, refino parabólico
sub-pixel, mediana bilateral e A-Trous, **resolução de trabalho 1/8**, saída `RG16F` na textura
`tFlow`. Licença "AGNYA".

No **nosso** `DLSS5_Feed.fx` ele já existe como provedor **#4** (`DLSS5_MV_PROVIDER 4`,
`DLSS5_Feed.fx:152` e `:1173`), com a mesma nota de 1/8 de resolução (`:120-128`) — só que **não é o
que carregamos**: o preset em uso traz o `Lumenite_Kernel`, e o VME por hardware entra sozinho
quando ligamos síntese ou fluidez. Portanto: **é da mesma família que a nossa, na variante mais
barata e mais grosseira.**

## 3. O Frame Gen: o que é, e se funciona

### 3.1 A evidência

`assets/app-preview.webp`, a captura oficial publicada na seção "O projeto", mostra a tela inicial
com dois cards, ambos marcados `PRÉ-JOGO — Configure antes de iniciar o jogo`, e um botão `ATIVAR`:

| Card **Frame Gen** ("Gere mais frames com renderização neural") | Card **Dimensionamento** ("Aumente a resolução com alta qualidade") |
|---|---|
| `Tipo: LSFG 3.1` | `Tipo: FSR` |
| `Modo: Fixo` | `Nitidez: 60%` |
| `Multiplicador: 2x` | `Versão otimizada: Ativado` ("usa parâmetros otimizados para mais qualidade") |
| `Escala de Fluxos: 70%` | `Modo: Automático` |
| `Desempenho: Ativado` ("Reduz a latência potencial (recomendado)") | `Proporção de imagem: Manter proporção` |

`LSFG 3.1`, `Escala de Fluxos` (*Flow Scale*), `Desempenho` (*Performance*), `Fixo` (*Fixed*, contra
*Adaptive*) e o multiplicador são, um a um, os controles do **Lossless Scaling Frame Generation
3.1**, do aplicativo pago *Lossless Scaling* (THS). A documentação do LSFG 3.1 descreve exatamente
esses parâmetros: Flow Scale como compromisso entre artefato e custo (recomendado 50–75%),
Performance com até 2× menos carga de GPU, e Fixed com teto de multiplicador até 20×.

A aba de Dimensionamento (tipo de escala, nitidez, modo automático, proporção de imagem) também
reproduz a aba de *Scaling* do mesmo aplicativo.

### 3.2 Então: o frame gen funciona?

**Provavelmente sim, e bem — porque o motor não é deles.** O LSFG 3.1 é um produto maduro, testado
por muita gente, com ganho real de fluidez. O que o Neural Mitsu acrescenta é a **interface** e o
fato de rodar na mesma janela em que a renderização neural já foi aplicada.

Três consequências que a leitura já garante, sem executar nada:

1. **Não é neural, e não é DLSS.** O card diz "Gere mais frames com renderização neural"; o motor
   selecionado é interpolação de fluxo óptico de terceiro. Não há `nvngx_dlssg.dll` nem Streamline na
   lista de dependências — o Frame Generation da NVIDIA está fora da cadeia. O texto do card, como
   está, **induz a erro**.
2. **Interpolação custa latência, por construção.** Para inserir um quadro entre A e B é preciso já
   ter B, isto é, **segurar um quadro**. Isso soma ao atraso que a captura externa já tem. O nosso
   caminho de Fluidez escolheu o oposto — **extrapolação** com `t` negativo (`main.cpp:16001`), que
   projeta para a frente e não segura nada; a decisão está documentada em `main.cpp:3660-3672`. O
   rótulo deles "Desempenho … reduz a latência potencial" é o modo Performance do LSFG, que reduz
   custo de GPU, não o atraso estrutural da interpolação.
3. **O roadmap deles confirma tudo.** "Estamos trabalhando em **frame pacing**, estabilidade,
   qualidade da **interpolação** e desempenho até que o Frame Gen possa deixar de ser tratado como
   experimental" (`app.js`, `roadmap.fgText`, idêntico em inglês: *"interpolation quality"*). Frame
   pacing e interpolação são precisamente os problemas de quem empilha um interpolador sobre uma
   janela capturada.

### 3.3 A pergunta que só a Fase 2 responde

**De onde vem o LSFG dentro do instalador?** Há três cenários, com consequências muito diferentes:

| Cenário | Como reconhecer no binário | Consequência |
|---|---|---|
| (a) O app usa o `Lossless.dll` da cópia comprada pelo usuário | pedido de caminho/detecção da pasta do Steam; nenhum `Lossless.dll` no pacote | legítimo, é o caminho do `lsfg-vk` |
| (b) O app **redistribui** o `Lossless.dll` ou os shaders extraídos | `Lossless.dll`, SPIR-V/DXIL do LSFG ou blobs de shader dentro do setup | redistribuição de software pago — **nunca copiar** |
| (c) Reimplementação própria com o mesmo nome | modelo/shaders próprios, mas o nome "LSFG 3.1" seria enganoso | zona cinzenta |

O site **não lista o Lossless Scaling entre as dependências** e não menciona compra, o que empurra a
suspeita para (b). Os projetos que fazem isso direito (`lsfg-vk`, `LSFG-Android`) extraem os shaders
da cópia legítima do próprio usuário e afirmam, com todas as letras, que o `Lossless.dll` é
propriedade da THS e **não é redistribuído**. Registro como **pendência da Fase 2**, não como fato.

## 4. O que eles têm que nós não temos

### 4.1 Importação guiada com validação por hash — a ideia que vale copiar

O produto inteiro se organiza em torno de um fluxo que nós não temos: o app mostra **quatro itens com
estado** (DFC, DLSS Runtime, DLSS Neural Runtime, Motion Shader), aceita `IMPORTAR DFC` e `LOCALIZAR
ARQUIVOS` **em qualquer ordem**, guarda cada peça validada dentro da própria instalação, e só então
anuncia `NEURAL CONFIGURADO E PRONTO PARA USO`. O chip "SHA-256" indica conferência de hash por
componente. É a resposta certa para o problema que temos hoje: as peças que não podemos distribuir
(`nvngx_dlss.dll`, `nvngx_dlssnr.dll`) ficam por conta do usuário, sem nenhuma tela que diga se a
cópia dele é a certa.

- Esforço nosso: **1–2 dias** (uma tela de estado com quatro linhas, `Get-FileHash` equivalente em
  C++, tabela de hashes conhecidos por versão, e a cópia para a pasta de dados).
- Ganho: **alto em suporte** — hoje uma cópia errada do `nvngx_dlssnr.dll` vira "não funciona" sem
  diagnóstico. E abre caminho para aceitar a 310.8.0 oficial **e** a nossa `310.8.SF.0` com aviso, em
  vez do alerta silencioso do ReShade que ninguém lê (memória `runtime-nr-em-uso-e-build-customizado`).

### 4.2 Um interpolador maduro como opção, ao lado da nossa extrapolação

Não é para trocar: o nosso caminho sem latência é melhor para jogo. Mas o LSFG mostra que existe
público para o modo "multiplicar fps aceitando atraso", com controle de **escala de fluxo** (rodar a
estimativa de movimento abaixo da resolução de tela para baratear). **Essa alavanca nós não temos**:
o nosso warp usa o campo de MV na resolução em que ele nasce. Um controle de escala do campo de
movimento é barato de implementar e ataca direto o custo do `Lumenite_Kernel` (15 passes de tela
cheia na fila 3D).

### 4.3 FSR como opção de ampliação de apresentação

A aba Dimensionamento deles oferece FSR com nitidez, modo automático e proporção — uma ampliação de
apresentação independente do DLSS. Nós resolvemos ampliação por `work_resolution` + DLAA + blit
bilinear (e medimos que forçar DLSS SR **piora**, 15,58 → 14,46 de nitidez, `ARQUITETURA.md` §8). Um
FSR de apresentação é uma terceira opção que não temos, e é especialmente relevante **no módulo AMD**.

### 4.4 Apresentação pública

Landing page com tutorial de seis passos, prints reais de cada download, FAQ, roadmap honesto
("experimental", "sem data prometida"), contador de downloads, PT/EN com um clique. É o que faz um
programa parecer produto. A nossa vitrine e o launcher cobrem parte disso; a página não.

## 5. O que nós temos que eles não têm

1. **Neural Rendering próprio dentro do feed**, 1 a 10 passes a **0,35 ms/passe** (`ARQUITETURA.md`
   §10) — contra ~4 ms/passe do add-on de terceiro. Eles dependem inteiramente do DFC, que se baixa
   por Discord, num `.7z` com senha, sem versionamento público.
2. **AMD funcionando hoje** (RDNA3/RDNA4, `main.cpp:4228-4251`, feeder com portador FSR). Neles é
   roadmap — e o texto do roadmap descreve **exatamente a peça que já usamos**: "uma solução não
   oficial de Renderização Neural modificada pela comunidade para placas AMD".
3. **Fluidez por extrapolação** (quadros novos acima do fps de entrada, sem segurar quadro) e **DLSS
   Frame Skipping** com quadro sintetizado por warp de MV — medido +13,2 a +18,8 dB de PSNR contra
   repetir o quadro (`warp_frame.h:19-22`) — com recuo automático quando o extra não cabe no orçamento.
4. **Motion vectors melhores**: `Lumenite_Kernel` (−0,7% a 45 px) e **VME por hardware** (~1 ms na
   fila de vídeo, que não disputa com o render). Eles usam a variante "superfast" a 1/8 da resolução.
5. **Seletor de placa** (rede numa GPU, jogo em outra), **controle de HDR** com restauração garantida,
   **prioridade de GPU** (película de 0,9 → 80 fps com o jogo saturando a placa), **dois limitadores
   de fps**, **comparativo lado a lado**, **captura de monitor** além de janela.
6. **Atualização automática** com manifesto, SHA-256 e notas; **launcher** que escolhe módulo
   NVIDIA/AMD e explica por que um deles está cinza.
7. **Não distribuímos as DLLs da NVIDIA** (`empacotar.ps1:2`). Eles hospedam cópias no MediaFire e
   chamam isso de "links temporários".
8. **Código auditável por nós**, com logs que atribuem culpa (`[feed] STALL … INSIDE the NGX
   evaluate`). Deles não há fonte, nem changelog, nem issues.

## 6. "Copiaram o meu?" — o que a evidência sustenta

Sustenta **convergência**, não cópia. O que é igual:

- a categoria e a frase de venda ("renderização neural externa", "sem modificar arquivos do jogo");
- o desenho de captura por fora com ReShade local, e o mesmo aviso de Janela/Janela sem bordas;
- público brasileiro primeiro (site em PT-BR, canal e LivePix), gratuito com doação;
- as mesmas quatro peças de base que qualquer cadeia DLSS 5 fora do jogo precisa ter.

O que **não** é igual, e é o que importa: a nossa cadeia é construída (feed próprio, NR próprio,
ponte, add-on de comparação, limitador, VME); a deles é **montada** com peças de terceiros —
DFC para o neural, LSFG para o frame gen, FSR para a escala. E o mesmo desenho externo já aparecia no
NeuralScreen (estudado hoje) e no OptiScaler; é o formato natural do problema, não uma assinatura.

O ponto em que eles claramente vão atrás de nós é o **AMD**: o que anunciam como "próximo grande
passo, prioridade principal" é o mod que a nossa árvore já integra desde o módulo AMD.

E o ponto em que **acrescentaram** algo — o frame gen — não é tecnologia nova deles: é o motor pago
de outra pessoa, com a etiqueta "renderização neural" por cima.

## 7. Sinais de alerta (registro, sem julgamento)

- **LSFG dentro de um instalador gratuito.** O Lossless Scaling é software pago. Se o motor vier
  embutido, é redistribuição de produto comercial — e é o bastante para **não copiarmos nada** desse
  caminho. A verificar na Fase 2 (§3.3).
- **DLLs da NVIDIA hospedadas em MediaFire** pelo próprio site (duas das quatro dependências), com o
  texto "cópias das versões compatíveis já testadas".
- **Dependência distribuída por Discord, em `.7z` com senha** (`chicken`) — canal que exige conta,
  convite que expira em 2026-10-01, sem hash publicado, sem changelog.
- **Instalador fechado, sem fonte, sem licença, sem changelog, sem repositório.** Nada auditável
  antes de abrir o `.exe`.
- **Descrição do Frame Gen como "renderização neural"** quando o motor selecionado é interpolação de
  fluxo óptico.
- Do lado bom: o site é estático, a única chamada de rede é um contador público de cliques e o beacon
  do Cloudflare; não há formulário, login, chave nem coleta de dados na página.

## 8. Fase 2 — o que verificar quando o arquivo chegar

Tudo estático primeiro; execução só em Windows Sandbox com vGPU, ou pelo usuário com entrega de logs.

1. **Identificação**: SHA-256 e MD5 do setup (conferir o MD5 com o `ETag "024d1fa0…"` do servidor),
   assinatura Authenticode, recurso de versão, e consulta do SHA-256 no VirusTotal **pela página web
   do hash** — sem enviar o arquivo.
2. **Desempacotar**: `innoextract` se for Inno, `7z` se for NSIS. Listar a árvore inteira com
   tamanhos e hashes.
3. **Caçar o LSFG** (a pergunta central): procurar `Lossless.dll`, `LSFG`, `losslessscaling`, blobs
   SPIR-V/DXIL, e qualquer leitura do registro/pasta do Steam. Decidir entre os cenários (a), (b) e
   (c) de §3.3.
4. **Mapear a cadeia**: `dxgi.dll`/ReShade embutido? add-ons? `.ini`, `.cfg`, `.json` de configuração,
   shaders `.fx`. Comparar o `ReShade.ini` deles com as armadilhas do nosso `ARQUITETURA.md` §3.
5. **Como o DFC é "importado"**: que arquivos o app espera do `.7z`, se valida hash, e se reescreve
   configuração do DFC — é aí que se aprende a receita de primeiro teste do Chicken que a nossa
   memória diz estar pendente (`warmup_rebuild=0`, 1 passe, 100%).
6. **Imports e strings dos binários próprios**: NGX, D3D11/12, WGC/DXGI Duplication, `RegisterHotKey`,
   `AttachThreadInput`, hooks, `VirtualProtect`. Anotar tudo que toque processo alheio.
7. **Se for executar** (Sandbox com vGPU, alvo `amd\tests\alvo_animado.exe` copiado para dentro):
   medir se o frame gen **produz quadro novo** (comparar pixels do quadro par contra os vizinhos:
   repetição dá diferença zero, warp/interpolação dá diferença estruturada), o **atraso** acrescido e
   o **custo de GPU** por multiplicador. Mesmo protocolo de movimento do `warp_test.cpp`: pan
   sintético a 2/8/20/45 px por quadro.

---

### Apêndice: método e evidências

- Arquivos salvos em
  `…\scratchpad\neural-mitsu\`: `index.html` (23.824 bytes), `app.js` (26.726), `site-config.js`
  (533), `styles.css` (36.073), `app-preview.webp/png` (1500×1132), `dfc-download`,
  `nvidia-download`, `lumenite-download`, `dlssnr-download` (1600×~780 cada).
- WebP convertido para PNG por WIC (`System.Windows.Media.Imaging`) só para leitura.
- Convite do Discord lido por `https://discord.com/api/v10/invites/g2v2XGqvR?with_counts=true`
  (metadados públicos; nenhuma conta usada, nenhum arquivo baixado do servidor).
- `HEAD` no instalador apenas para cabeçalhos; **o corpo não foi baixado**.
- Nada em `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\` foi alterado além deste relatório, e
  nada em `bin\` foi tocado.

Fontes externas citadas:
[Neural Mitsu](https://neuralmitsu.com/) ·
[LSFG 3.1 — Lossless Scaling](https://losslessscaling.com/lsfg-3-1/) ·
[lsfg-vk](https://github.com/PancakeTAS/lsfg-vk) ·
[LumeniteFX](https://github.com/umar-afzaal/LumeniteFX) ·
[DLSS5-Feeder — FEEDBACK-DFC.md](https://github.com/jlrouzies-fr/DLSS5-Feeder/blob/main/FEEDBACK-DFC.md)

---
---

# PARTE II — FASE 2: análise estática do instalador

Data: 2026-09-11. **Nada foi executado**: nem o instalador, nem os três executáveis, nem o add-on,
nem em Sandbox — por ordem direta do usuário ("analisa só o código"). Tudo abaixo vem de arquivos
extraídos, código-fonte em claro que veio dentro do pacote, strings e tabelas de importação.

Arquivo analisado: `C:\Users\PCDOFAFA\Downloads\neural mitsu\NeuralMitsuSetup.exe`, 7.237.561 bytes,
`SHA-256 30CACE5E0DC088E1C92F1039956EE603C3DFD2FA8F653100B1220B315B89E174`,
`MD5 E00BB80096757F5094AC7DBE0E022DE7` (o MD5 confere com o `ETag` que o servidor deles publicava na
Fase 1). `ProductName Neural Mitsu`, `ProductVersion 0.21`, `CompanyName MitoGouken`, **sem assinatura
digital**. Inno Setup **6.7.0** (Unicode).

## 9. As três conclusões da Fase 2

1. **O frame gen é próprio e funciona por warp de motion vectors.** O shader está em texto claro
   dentro do `MitsuFGPresenterLite.exe` (compilado em execução por `D3DCOMPILER_47`). Tem três
   modos: passagem direta, **interpolação no ponto médio** e **extrapolação de meio quadro**. É a
   mesma família do nosso `warp_frame.h`. Grau de certeza: **alto** — o código-fonte do shader está
   legível, não é inferência.
2. **Não há LSFG, nem Lossless Scaling, nem FSR, nem nada da NVIDIA para geração de quadro.** A
   busca por `LSFG`, `Lossless`, `FSR`, `DLSSG`, `Streamline`, `OFA`, `interpol` e `extrapol` em
   ASCII e UTF-16, em todos os 111 arquivos extraídos, deu **zero ocorrências**. Grau de certeza:
   **alto** para a build 0.21; a divergência com a captura publicada no site fica sem explicação
   (versão diferente, ou imagem de divulgação que não corresponde ao produto).
3. **Não há nenhum vestígio de código, texto ou nome nosso.** Grau de certeza: **alto** (§13).

## 10. Método e ferramentas

| Passo | Resultado |
|---|---|
| `7z l` no instalador | "Cannot open the file as archive" — não é SFX comum |
| `7z l -tPE` | PE 32-bit Delphi (linker 2.25), 914.432 bytes de loader + **6.323.129 bytes de cauda** |
| strings do loader | `Inno Setup Setup Data (6.7.0)`, `Inno Setup Messages (6.5.0) (u)` |
| `innoextract 1.9` e snapshot `1.10-dev-2025-01-07` | **falharam** — suportam até Inno 6.3.3; erro `Unexpected setup loader revision: 2` e `block header CRC32 mismatch` |
| `innounp 2.67.11` | **funcionou** — 111 arquivos + `install_script.iss` |

Ferramentas baixadas, com procedência registrada:

- `innoextract-1.10-dev-2025-01-07-windows.zip`, de `constexpr.org` (site oficial do autor);
  `SHA-256 8c472a72b944e9848f75c4fb1f47b4907f0a7924ad226cac1beb51d31621abd2`, **conferido contra o
  `SHA256SUMS` publicado no mesmo diretório** — bate.
- `innounp-2.zip` da release `oi_2_2_11` (2026-07-12) de `jrathlev/InnoUnpacker-Windows-GUI`;
  `SHA-256 851772538A041229102AD9964542D49DC00C74002E3091A70469C079AE368F52`; o executável
  `innounp.exe` tem `SHA-256 1C8453C198DD3FE7947F3705E59D2294DDCE3DFA9F96A737E94449B119F44181` e
  **não é assinado**. Foi a única ferramenta pública encontrada que declara suporte a Inno 6.7.

### Reputação: o que deu para fazer e o que não deu

- **VirusTotal: NÃO consultado.** A página do hash exige **reCAPTCHA** para visitante anônimo, e o
  endpoint `ui/files/<hash>` responde `429 RecaptchaRequiredError`. Resolver CAPTCHA é proibido, e
  não foi feito. **Fica pendente**: só com uma chave de API do VirusTotal, ou com o usuário abrindo
  `https://www.virustotal.com/gui/file/30cace5e0dc088e1c92f1039956ee603c3dfd2fa8f653100b1220b315b89e174`
  no navegador dele.
- **MalwareBazaar (abuse.ch)**: hoje exige chave (`401`).
- **Windows Defender local**: varredura sob demanda (`MpCmdRun -Scan -ScanType 3`, assinaturas
  `1.459.160.0` de 11/09/2026) no instalador **e** na pasta extraída — **nenhuma ameaça**. É um motor
  só; não substitui o VirusTotal.

## 11. O que o instalador instala

Dez arquivos próprios e o de sempre. Destino `{autopf}\Neural Mitsu` (Program Files, logo pede
elevação).

| Arquivo | Tamanho | O que é, pelos imports |
|---|---|---|
| `MitsuNeural.exe` | 449.024 | A interface. `d2d1` + `DWrite` + `GDI32` (Direct2D). Importa `nvngx_dlss.dll`, `nvngx_dlssnr.dll` e `deep-fried-chicken-nvngx.dll`. String interna: `Neural Mitsu v0.21 DirectWrite / Golden engine` |
| `NeuralScalerRender.exe` | 251.904 | O renderizador. **C++/WinRT 2.0.250303.5 + `Windows.Graphics.Capture`** (WGC), `d3d11`, `D3DCOMPILER_47`, `dwmapi`. Carrega `ReShade/ReShade64.dll` e os add-ons do DFC |
| `MitsuFGPresenterLite.exe` | 98.304 | O frame gen. `d3d11`, `dxgi`, `D3DCOMPILER_47`, `WINMM`. **Nada de NGX, CUDA, OFA ou Lossless** |
| `MitsuFGBridge.addon64` | 46.080 | Add-on ReShade minúsculo; só CRT e `KERNEL32`. Publica o campo de movimento para o presenter |
| `ReShade\ReShade64.dll` | 5.592.064 | ReShade redistribuído, com hash esperado fixado em `Core\ReShadeCompatibility.h` |
| VC++ runtime (11 DLLs) | ~2,2 MB | `msvcp140`, `vcruntime140`, `concrt140`, `vccorlib140` |
| `assets\` | ~2,2 MB | Fundo, logos, ícone e **seis bandeiras** (PT, EN, ES, FR, DE, IT), com `flags-source.txt` creditando o flagcdn |
| Código-fonte `.h` e `.fxh` | ~12 KB | **Empacotados em claro** (§12) |
| Shaders do DFC | 69 KB | `DFC_Universal_Feed.fx` e `lumenite_QuantMotion.fx`, ambos **modificados por eles** |
| `reshade-shaders\` | ~1,2 MB | LumeniteFX completo + Lilium + `MotionEstimation.fx`. Pasta marcada `Managed by RDXC.txt` (RenoDXCommander) |
| `ReShade.ini`, `ReShadePreset.ini`, `MitsuNeural.ini` | — | §14 |

**O `install_script.iss` veio junto** e é curto de ler: seções `[Setup] [Files] [Dirs] [Icons]
[CustomMessages] [Languages]`. **Não existe `[Run]`, `[Registry]` nem `[Code]`** — o instalador não
escreve no registro, não cria autostart, não executa nada ao terminar e não baixa nada.

## 12. A arquitetura, declarada no próprio código

Eles empacotaram os headers C++ em claro, então a arquitetura não precisa ser inferida:

```
MitsuNeural.exe  (interface Direct2D, 6 idiomas, hotkeys)
   │  lança:  "<exe>" --hwnd 0x<HWND> --sharpness N --flow N --aspect N --performance N
   ├──► NeuralScalerRender.exe   WGC na janela do jogo → ReShade64.dll no próprio processo
   │         └── add-ons: deep-fried-chicken.addon64 + dfc-universal-feed.addon64 (+ MitsuFGBridge)
   │         └── shaders: lumenite_QuantMotion.fx → DFC_Universal_Feed.fx → DLSS/NR
   └──► MitsuFGPresenterLite.exe  janela layered input-transparent, swapchain DXGI, warp
             ◄── campo de movimento pela memória compartilhada Local\MitsuNeuralFG_v021_abi22_
```

**É a mesma arquitetura da nossa camada**: captura por WGC, ReShade hospedado no nosso processo (não
injetado no jogo), add-ons fazendo o trabalho neural, janela sobreposta apresentando o resultado. Até
a limitação estrutural é a mesma, e eles a documentam no código com a mesma consciência que o nosso
`ARQUITETURA.md` §8:

```
o.d = 1.0; // intentionally flat: lets DFC fail-safe/color-only paths decide what to do
```

Isto é o **depth plano** — o nosso `main.cpp:454` grava `0.5` pelo mesmo motivo: o WGC entrega cor, e
só cor.

`Core\INeuralProvider.h` define uma interface de provedor com `NeuralCapabilities{neuralProcessing,
frameGeneration, reshadeAddon, mitsuEnhancement, enhancementContract}`. Só existe um provedor,
o `DfcProvider`, e ele declara `{true, false, true, true, 1}` — ou seja, **o próprio código diz que o
DFC não faz frame generation**. Confirma a leitura da Fase 1 pela via mais direta possível.

### O "Mitsu Enhancement" é uma otimização real, não marketing

`Enhancements\MitsuEnhancement.h` diz, no comentário: *"Compile-time integration only. No frame
processing, textures or runtime patching."* O que ele faz é escrever `EffectSearchPaths` no
`ReShade.ini` colocando `..\Providers\DFC\Shaders` **antes** de `.\reshade-shaders\Shaders\**`. As
cópias deles dos dois shaders ganham precedência, e essas cópias trazem um `#include` a mais:

| Shader modificado | Linha | O que entra |
|---|---|---|
| `lumenite_QuantMotion.fx` | 150 | `#include "../../../Enhancements/Shaders/MitsuMotion.fxh"` |
| `DFC_Universal_Feed.fx` | 435 | `#include "../../../Enhancements/Shaders/MitsuStructure.fxh"` |

`MitsuMotion.fxh` substitui a busca ZMSAD por uma versão que **calcula o patch atual uma vez**
(`CacheCurrentPatch`, 9 amostras, média removida) e o reaproveita em cada candidato
(`ZMSADCached`) — no original cada candidato reamostra os dois patches. Numa busca piramidal com
vários candidatos por nível, é uma economia grande de amostras de textura.
`MitsuStructure.fxh` avalia, com as mesmas amostras, as duas hipóteses "o pixel está parado" e "o
pixel veio de onde o fluxo diz", e devolve `static_error`, `flow_error` e `contrast` — é a validação
estática do feed com metade do custo. **É engenharia de verdade, e é o item mais interessante do
pacote depois do frame gen.**

## 13. O frame gen, linha por linha

O `MitsuFGPresenterLite.exe` carrega o HLSL como texto e compila em execução, então o algoritmo está
em claro. Entradas: `PrevTex`, `CurrTex`, `MotionTex`, `ConfTex`; constantes `Mode`, `UseMotion`,
`UseConf`, `Sharpness`, `FlowScale`, `Optimized`, `InvTex`.

```hlsl
float Mode;   // 0=real, 1=quality midpoint, 2=low-latency prediction
```

**Modo 0 — real.** `return float4(SharpSample(CurrTex, i.uv),1);` — nenhum quadro gerado, só a
nitidez adaptativa.

**Modo 1 — interpolação no ponto médio** (o "Quality" da interface):

```hlsl
float2 uvC = i.uv - flow*0.5;
float2 uvP = i.uv + flow*0.5;
float3 warped = lerp(SharpSample(PrevTex,uvP), SharpSample(CurrTex,uvC), 0.5);
return float4(lerp(fallback, warped, conf), 1);
```

Os dois quadros vizinhos são deformados para o meio do caminho e misturados meio a meio. O
comentário no próprio arquivo assume o preço: **"half-frame extra latency"**. É interpolação
clássica, e confirma o que a Fase 1 deduziu do roadmap deles — só que com código à vista em vez de
inferência.

**Modo 2 — extrapolação de meio quadro**, com o comentário deles:

```
// Low-latency: the real current frame was already shown. Predict half a
// frame forward from CURRENT using current->previous flow.
float2 uv = i.uv + flow*0.5;
return float4(lerp(cur, pred, conf), 1);
```

**É exatamente a decisão do nosso `main.cpp:3660-3672`**: projetar para a frente, a partir do quadro
que já foi mostrado, para não segurar nada. Chegaram ao mesmo lugar que nós, pelo mesmo motivo.

Três salvaguardas, todas legíveis:

- **Confiança**: `ConfTex` (vem do `QuantMotion::tConfidence`) pesa a mistura. O comentário: *"Unreliable
  motion must not cross-fade two unaligned object positions"* — com confiança zero, devolve o quadro
  atual, sem imagem dupla.
- **Fora de tela**: `if(any(uv<0.0)||any(uv>1.0)) return SharpSample(CurrTex, i.uv);`
- **Nitidez adaptativa por borda**: `SharpSample` mede o alcance local (`localRange`) e reduz o
  reforço onde há borda (`adapt = saturate(1.0 - edge*1.15)`) — é o mesmo shader que serve de
  "Dimensionamento".

E há **autotestes embutidos** no binário, o que é um bom sinal de engenharia:

```
Known motion: prev center 39.5, current center 59.5, synthetic center
Zero confidence uses current frame without double image:
Out-of-bounds motion uses current frame:
```

O presenter mantém contadores (`realFramesPresented`, `syntheticFramesGenerated`,
`syntheticFramesPresented`, `presentGapP95Ms`, `frameAgeMs`, `maxFrameLatency`) e escreve
`MitsuFG.log` e `MitsuFG.status`. A interface mostra `BASE FPS`, `GENERATED FPS` e `OUTPUT FPS`. Uma
string do presenter merece registro pela honestidade: *"layered input-transparent presenter; DXGI
submissions (not display scanouts)"* — eles avisam que o número é de submissão, não de quadro
mostrado.

### Contra o nosso

| | Neural Mitsu 0.21 | DLSS5 For All |
|---|---|---|
| Técnica | warp por MV: interpolação (Modo 1) **ou** extrapolação (Modo 2) | warp por MV: extrapolação (`Fluidez`, `t` negativo) e síntese do quadro pulado (`t=0,5`) |
| Latência | Modo 1 custa meio quadro, declarado no código; Modo 2 não custa | nunca segura quadro |
| Fonte do movimento | `QuantMotion` (1/8 de resolução) via ReShade, com confiança | `Lumenite_Kernel` (−0,7% a 45 px) ou **VME por hardware** (~1 ms na fila de vídeo) |
| Validação | confiança por textura, descarte fora de tela, autoteste embutido | PSNR medido contra verdade-terreno: **+13,2 a +18,8 dB** sobre repetir (`warp_frame.h:19-22`) |
| Proteção de orçamento | não vista | recuo automático se o extra custar mais que a fatia; 3 recuos desligam na sessão |
| Nitidez | integrada ao mesmo shader, adaptativa por borda | `DLSS5_Sharpen.fx`, separado |
| Medição exposta | BASE / GENERATED / OUTPUT FPS na interface | log e painel |

**Eles não têm nada que nós não tenhamos no frame gen.** O que têm de melhor é a **apresentação**
(os três contadores de fps na cara do usuário) e duas ideias pequenas que valem: a **nitidez
adaptativa por borda dentro do próprio passe de warp** e o **campo de confiança como textura**
separada, que nós resolvemos com validação no shader de MV.

## 14. Configuração, e o que ela revela

`MitsuNeural.ini` é minúsculo e não menciona LSFG nem FSR:

```ini
[FrameGen]  Enabled=0  Multiplier=2  FlowScale=80  Performance=0
[Scaling]   Enabled=0  Sharpness=30  Optimized=1   KeepAspect=1
```

Na interface, o "Tipo" do Dimensionamento é **`Bilinear + nitidez adaptativa`** — texto literal do
binário, nos seis idiomas. Não há FSR nesta build.

O `ReShade.ini` deles aponta `EffectSearchPaths=..\Providers\DFC\Shaders,.\reshade-shaders\Shaders\**`,
`AddonPath=.\Addons`, `KeyOverlay=36` (Home) e `DepthCopyBeforeClears=2`.

O `ReShadePreset.ini` ativa **duas técnicas**:
`Techniques=Lumenite_QuantMotion@lumenite_QuantMotion.fx,DFC_Universal_Feed@DFC_Universal_Feed.fx`,
com `DFC_Universal_MV_PROVIDER=4` (QuantMotion).

### O achado que parece cópia e não é

O `ReShadePreset.ini` deles tem uma seção **`[DLSS5_Feed.fx]`**, com `DLSS5_MV_PROVIDER=4` e os
mesmos uniformes que usamos. **Não é o nosso arquivo.** O `DLSS5_Feed.fx` é o shader do
**DLSS5-Feeder** (jlrouzies-fr, MIT), componente de terceiro que a nossa cadeia também carrega —
o nosso inventário o localiza em `reshade-shaders\Shaders\DLSS5_Feed.fx:152`. A seção no preset
deles é resíduo de uma instalação do Feeder na máquina do autor; o ReShade guarda seções de efeitos
que já viu.

O mesmo vale para o `DFC_Universal_Feed.fx`: ele declara `sampler sDLSS5_ProviderMV`,
`sDLSS5_ProviderConfidence`, `sDLSS5_PrevLuma`. Os prefixos `sDLSS5_` denunciam que o **shader do
DFC é derivado do `DLSS5_Feed.fx` do Feeder**, com renomeação incompleta — coerente com o que o
autor do DFC escreveu no Discord ao lançar a 1.6.8 ("comecei a construir o meu próprio [feeder]…
bem menos sofisticado, o Feeder ainda é o rei"). É uma questão entre o DFC e o Feeder, **não entre
eles e nós**.

O preset ainda carrega `[RenoDX.DLSS5]` com `EnableHooks=2`, `NRIntensity=2`, `NRStyle=1`,
`NRMVecScaleX=0.09`, `NRMVecScaleY=0.08`, `NRPaperWhiteScale=8.663` — resíduo do add-on RenoDX antigo
(Krish/Lecram) na máquina do autor.

## 15. Cópia do nosso projeto: não há

Varredura de todos os 111 arquivos extraídos, ASCII e UTF-16, por: `Fafa`, `DLSS5ForAll`,
`dlss5forall`, `FafaDLSS5`, `dlss5-feed`, `dlss5-bridge`, `DLSS5Bridge`, `DLSS5_Sharpen`,
`dlssnr_on_amd`, `DLSS-NR-on-AMD`, `Frame Skipping`, `Fluidez`, `warp_frame`, `camada`,
`NeuralScreen`, `perseval`, `OptiScaler`.

**Zero ocorrências de todos.** As únicas coincidências foram `DLSS5_Feed` e `Lumenite_Kernel`, que
são nomes de **componentes públicos de terceiros** que os dois projetos usam (§14).

Também não há nenhum texto de interface nosso, nenhuma chave de ini nossa, nenhum nome de técnica
nosso. A conclusão da Fase 1 fica confirmada pelo binário: **convergência de categoria, não cópia**.
Os dois projetos chegaram à mesma arquitetura porque ela é a única que resolve o problema sem
injetar DLL no jogo — e ambos herdaram as mesmas peças de terceiros (ReShade, LumeniteFX, o feeder,
a runtime da NVIDIA).

Dois pontos em que eles chegaram, sozinhos, às nossas conclusões: o **depth plano deliberado** (§12)
e a **extrapolação para não segurar quadro** (§13, Modo 2).

## 16. Sinais de alerta e riscos, revisados com o binário na mão

**O que a Fase 1 temia e não se confirmou:**

- Não há `Lossless.dll`, nem shader do LSFG, nem leitura da pasta do Steam. A suspeita de
  redistribuição de software pago **cai**.
- Não há telemetria, updater, socket, `winhttp`, `wininet` nem `ws2_32` em nenhum dos binários. As
  **únicas** URLs em todo o pacote são `https://www.youtube.com/@MitoGouken` e
  `https://livepix.gg/mitsu`, abertas por clique. A interface diz, em seis idiomas, *"Nada é enviado
  automaticamente"*, e o binário sustenta a frase.
- O instalador não mexe no registro, não cria autostart e não executa nada ao final.

**O que permanece:**

- **Nada é assinado** — nem o instalador, nem os três executáveis, nem o add-on. SmartScreen vai
  reclamar, e o usuário não tem como verificar procedência.
- **Redistribuem o `lumenite_QuantMotion.fx` modificado** (Copyright Afzaal, licença AGNYA) e o
  `DFC_Universal_Feed.fx` modificado, além do `ReShade64.dll`. Os cabeçalhos de copyright foram
  preservados; se a AGNYA permite obra derivada redistribuída é questão que não verifiquei.
- **Dependem de download por Discord** (`.7z` com senha) e de cópias em MediaFire das DLLs da NVIDIA
  — isso não mudou, e o instalador em si não as distribui (correto).
- **A captura de tela publicada no site não corresponde ao produto entregue.** Mostra "LSFG 3.1" e
  "FSR"; o programa tem frame gen próprio e escala bilinear. Seja imagem antiga ou peça de
  divulgação, **induz o usuário a erro sobre o que está baixando**.
- Mensagem de erro que vaza o ambiente de desenvolvimento do autor: *"MitsuFGBridge.addon64 não
  existe. Rode BUILD_MITSUFG_LITE.bat"* — um script de build dele que o usuário final nunca terá.
- Instala em `Program Files` e, por isso, pede elevação.

## 17. O que copiar, agora com fundamento

1. **Validação de dependências por hash, com mapa de versões** (`Providers\DFC\Compatibility.h` +
   `compatibility.json`): um `std::map<nome, sha256>` que diz, antes de qualquer coisa, se a cópia do
   usuário é a certa — e um segundo hash aceito para a variante conhecida (eles fazem isso com o
   `LumeniteHashOk`, que aceita o original **e** a build compatível). É a resposta para a nossa
   memória sobre o runtime customizado passar despercebido: aceitaríamos a `310.8.0.0` e a
   `310.8.SF.0` **com aviso**, em vez de silêncio. Esforço: **1 dia**; ganho: **alto em suporte**.
2. **Os três contadores na cara do usuário** — `BASE FPS`, `GENERATED FPS`, `OUTPUT FPS` — quando a
   Fluidez está ligada. Hoje isso vive no nosso log. Esforço: **horas**; ganho: médio, e alto para
   demonstração no canal.
3. **Nitidez adaptativa por borda dentro do passe de warp** (`adapt = saturate(1 - edge*1.15)`):
   reforça onde é liso e recua onde há borda, no mesmo passe, sem shader extra. Vale testar contra o
   nosso `DLSS5_Sharpen` no quadro sintetizado. Esforço: **2–4 h**; ganho: a medir.
4. **Autotestes de warp embutidos no binário** (movimento conhecido, confiança zero, vetor fora de
   tela). Nós temos o `warp_test.cpp` fora do produto; ter três asserções dentro do executável, que
   qualquer log de usuário revele, é barato e economiza diagnóstico remoto.
5. **Cache do patch na busca ZMSAD** (`MitsuMotion.fxh`): se um dia ligarmos o QuantMotion como
   provedor (ele já existe no nosso `DLSS5_Feed.fx` como provedor 4), essa otimização vem de graça —
   e a ideia serve para o `Lumenite_Kernel` também.

**Não copiar**: a captura de tela que não corresponde ao produto; a dependência de Discord com senha;
distribuir shader de terceiro modificado sem checar a licença.

## 18. O que ficou pendente

- **VirusTotal** (§10): bloqueado por reCAPTCHA para anônimo. Precisa de chave de API ou de o próprio
  usuário abrir a página. O Defender local não acusou nada.
- **Os três componentes externos** (DFC 1.6.8, `nvngx_dlss.dll`, `nvngx_dlssnr.dll`) não foram
  baixados: o DFC exige conta no Discord e senha, e os outros dois vêm de MediaFire. Os hashes que o
  Mitsu exige estão registrados em `compatibility.json`; **nenhum bate com as nossas cópias**:

| Arquivo | Hash exigido pelo Mitsu | A nossa cópia |
|---|---|---|
| `nvngx_dlssnr.dll` | `4c5bd117…c319d74` | `bin\`: `e67dee20…1c989a` · `amd\`: `6eb209e7…30b3927` |
| `nvngx_dlss.dll` | `d202b024…66d7cf72` | `c85f971c…360b0b7e` (as duas cópias) |

- **O motor do `deep-fried-chicken.addon64`** continua sem análise: não temos o binário. O que se
  sabe dele vem do shader companheiro e da configuração (`safe_neutral_start`,
  `motion_stability_enabled`, que o Mitsu zera na primeira importação).
- **Medição**: nada de desempenho, latência ou qualidade foi medido, porque nada foi executado. Os
  números do §13 que são nossos vêm dos nossos docs; do lado deles, só o que o código declara.

### Apêndice II: onde estão os arquivos

- Extração: `…\scratchpad\nm2\ext\` (111 arquivos + `install_script.iss`).
- Strings: `…\scratchpad\nm2\strings\{MitsuNeural.exe,NeuralScalerRender.exe,MitsuFGPresenterLite.exe,MitsuFGBridge.addon64}.txt`.
- Ferramentas: `…\scratchpad\tools\ie\` (innoextract) e `…\scratchpad\tools\innounp\`.
- O instalador original permanece intocado em `C:\Users\PCDOFAFA\Downloads\neural mitsu\`.
- Nada em `C:\Users\PCDOFAFA\Desktop\dlss5 for all portable\` foi alterado além deste relatório;
  `bin\` só foi lido para calcular dois hashes.
