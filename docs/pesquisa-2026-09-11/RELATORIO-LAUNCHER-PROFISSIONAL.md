# Launcher profissional — o estudo, as três variações e a que ficou

**Data:** 11/09/2026 · **Arquivos mexidos:** `amd\splash_ofertas.h`, `amd\portable-launcher\launcher.cpp`
**Fotos:** `amd\docs\pesquisa-2026-09-11\launcher\`
**Nada foi para `bin\`.** O launcher novo existe só em `amd\portable-launcher\DLSS5ForAll.exe` e nas
cópias de teste. A cópia que você está usando agora não foi tocada.

---

## 0. Decidido: a variação B, com os ajustes que vieram depois

**Você escolheu a B (Faixa)**, e ela é o padrão do código desde 11/09/2026 (`LAY_PADRAO` em
`splash_ofertas.h`). As outras duas continuam compiladas porque são o material de comparação
deste relatório — e porque a C cai sozinha na B quando não há rede, então ela nunca foi um
caminho separado de verdade. `DLSS5_LAUNCHER_LAYOUT=0|1|2` (com `DLSS5_DIAG=1`) ainda troca,
só para refazer as fotos lado a lado.

Os ajustes que você pediu depois da escolha estão **todos feitos**:

| # | Ajuste | Como ficou | Foto |
|---|---|---|---|
| 1 | Capivara como ilustração | 72 unidades à esquerda do nome, recurso 1 do exe, recarregada a cada DPI | `B-recorrente.png` |
| 2 | Chave "ocultar a faixa" | "ocultar" no rótulo; vira "Ofertas do canal ocultas · reativar"; grava no ini; emite `ocultou_faixa` / `reativou_faixa`; com ela oculta, **ninguém vai à rede** | `B-faixa-oculta.png` |
| 3 | Tirar a escolha de placa | o seletor sumiu; o launcher decide em ordem fixa e a linha de estado fica | `B-recorrente.png` |
| 4 | "AMD experimental" | substitui o "(beta)", com a ajuda curta logo abaixo | `B-amd-experimental.png`, `B-amd-experimental-en.png` |
| 5 | Faixa sempre com a última oferta, atualizando sozinha | busca a cada 60 s em thread própria; só troca quando o conteúdo **muda de verdade** (SHA-256 do JSON) | `B-troca-animacao.png` |
| 6 | Animação a cada troca | ~350 ms: o conteúdo entra deslizando 12 px e clareando, o rótulo pisca em ciano; respeita "reduzir animações" do Windows | `B-troca-animacao.png` |
| 7 | Texto adaptado ao tipo do item | oferta / cupom / aviso, com rótulo e apoio próprios; nunca fala em preço num aviso | `B-tipo-oferta.png`, `B-tipo-cupom.png`, `B-tipo-aviso.png` |
| 8 | Em inglês, sem ofertas | a faixa chama o canal do YouTube; somem a aba lateral, a coluna de ofertas, o cupom e o polling | `B-ingles.png` |
| 9 | Remover "Continuar de onde parou" | o cartão e o botão "Repetir" saíram; a janela encolheu 111 px | `B-recorrente.png` |

Mais um décimo item que você pediu em seguida: a atualização puxada pelo mesmo `versao.json` da
página de download — seção 0.6.

Onde cada um está explicado: **0.2** o 3, o 4 e o 9 · **0.3** o 1 · **0.4** o 5, o 6 e o 7 ·
**0.5** o 8 · **0.6** a atualização · a ideia 4 da seção 5 explica o 2.

---

## 0.1 Como as três se comparam (o registro do estudo)

Há **três desenhos prontos e funcionando**, todos fotografados. Eles diferem em **uma coisa só**:
quanto espaço o convite do grupo ocupa na tela principal. O resto — nome do produto no alto,
INICIAR como única ação, a linha de estado da placa, os chips de idioma — é igual nos três.

| | Variação | Como a oferta aparece | Foto |
|---|---|---|---|
| **A** | **Gaveta** | Rodapé de uma linha + aba lateral; o QR vive num cartão que abre por cima | `launcher\A-recorrente.png` |
| **B** | **Faixa** | Um bloco largo rotulado "OFERTA DE HOJE", com QR, preço e botão âmbar | `launcher\B-recorrente.png` |
| **C** | **Vitrine** | A faixa da B **mais** a coluna de ofertas sempre aberta ao lado | `launcher\C-recorrente.png` |

**Como ver as três, sem instalar nada:** abra uma das pastas de teste do laboratório
(`…\scratchpad\lab-B-recorrente\`) e rode o `DLSS5ForAll.exe` de lá com `DLSS5_DIAG=1` e
`DLSS5_LAUNCHER_LAYOUT=0|1|2`. Elas apontam para um servidor falso local, não para o de verdade.

---

## 0.2 O launcher deixou de perguntar qual placa usar

O seletor ("Rodar em: NVIDIA / AMD") **saiu da tela**. O launcher decide sozinho, em ordem fixa:
**NVIDIA pronta → NVIDIA; senão AMD pronta → módulo AMD; senão sem DLSS 5** — que passou a ser
uma *consequência*, nunca uma opção oferecida. A escolha manual continua existindo na janela
seguinte, o seletor do próprio app.

**Por que isso é melhor, e não só mais curto:** a lei de Hick diz que cada opção a mais custa
tempo de decisão, e essa decisão específica era pedida *antes de a pessoa ver qualquer coisa* —
no momento em que ela tem menos informação para decidir. Pior: ela já era pedida de novo na
janela seguinte. Duas perguntas iguais, a primeira no pior momento possível.

**O que ficou no lugar** é a **linha de estado**, que informa sem pedir nada: "RTX 4080 detectada
· pronta". Ela responde "o meu computador serve?", que é a dúvida real de quem abre o programa
pela primeira vez — e é *recognition over recall*: a pessoa reconhece a própria placa em vez de
ter que lembrar qual ela tem.

**"AMD experimental"** substituiu o "(beta)": o mod da AMD é desenvolvido em paralelo, já funciona
e ainda pesa mais. "Beta" promete uma versão quase pronta; "experimental" não promete. Logo
abaixo, uma linha explica o que isso custa: *"Modo AMD em desenvolvimento: funciona, mas ainda
pesa mais."* (en: *"AMD mode is still in development: it works, but it is heavier."*). Ela só
aparece quando o caminho AMD é o que vai valer — ninguém precisa ser avisado disso numa máquina
que vai rodar pela NVIDIA.

**A última sessão não decide mais nada.** O INICIAR abre sempre pela ordem fixa acima. Ele
chegou a respeitar a placa da sessão anterior enquanto existia o cartão "Continuar de onde
parou" com o botão "Repetir" — mas o cartão **também saiu**, a pedido seu ("não é tão
necessário"), e sem ele não haveria onde a pessoa ver que o launcher escolheu AMD por causa de
ontem, nem como desfazer. A janela encolheu 111 px com a saída do cartão.

**`--sem-dlss` deixou de existir para o usuário.** O argumento continua no código porque a
atualização o usa para reabrir o launcher na mesma escolha, mas nenhuma parte da tela o oferece.

---

## 0.4 A faixa virou uma janela viva para o grupo

Ela deixou de ser um texto fixo lido uma vez na abertura. Agora mostra **o item mais recente do
grupo** e se atualiza sozinha enquanto o launcher está na tela.

**Como funciona.** Uma thread busca o `ofertas.json` a cada 60 segundos — a primeira vez na hora,
sem esperar. Ela compara o **SHA-256 do JSON inteiro** com o que já está na tela e só avisa a
janela quando há algo diferente. Comparar o conteúdo em vez de reagir a cada resposta é o que
impede a faixa de piscar de minuto em minuto mostrando exatamente a mesma coisa — e o hash pega
qualquer mudança (título, preço, cupom, endereço), sem precisar comparar campo a campo.

**A animação da troca** dura ~350 ms: o conteúdo novo entra deslizando 12 px de baixo e clareando,
enquanto o rótulo pisca uma vez em ciano. A moldura, o QR e o rótulo não se mexem — a faixa não
"pula" na tela. Não entrou biblioteca nenhuma: é um timer de 16 ms e uma cor interpolada a cada
quadro (`MisturaCor`), porque GDI não tem transparência de texto. **Com "mostrar animações no
Windows" desligado (`SPI_GETCLIENTAREAANIMATION`), a troca é seca** — quem desliga isso costuma ter
um motivo, enjoo de movimento ou uma máquina que engasga, e não é a nossa faixa que vai discutir.

**O texto se adapta ao tipo do item.** O grupo publica oferta de peça, mas também cupom e recado —
e "a mais recente" pode ser qualquer um dos três. Se o `ofertas.json` trouxer `tipo`, ele manda;
se não trouxer, o tipo é deduzido (tem código de cupom → cupom; o título fala em dinheiro →
oferta; senão → aviso).

| Tipo | Rótulo | Linha de apoio |
|---|---|---|
| oferta | OFERTA DE HOJE | "Publicada há N min no grupo. O preço muda rápido — o grupo recebe primeiro." |
| cupom | CUPOM NOVO | "Cupom novo no grupo: vale enquanto durar." (+ a pílula com o código, que copia ao clicar) |
| aviso | NO GRUPO AGORA | "Novidade no grupo há N min." |

**O "há N min" só aparece quando existe carimbo de publicação** no JSON. Sem ele, a frase sai sem
a parte temporal — a tela cala a boca sobre tempo em vez de inventar um. E **nenhuma frase fixa
fala em preço, frete ou peça**: era exatamente isso que ficava estranho embaixo de um aviso de
live ou de um cupom de mercado.

**O título cortado tem como ser lido.** O nome do produto vem do servidor e pode não caber; o
corte sai com reticências e, com o mouse sobre a faixa, uma caixinha mostra o texto inteiro
(`B-titulo-tooltip.png`). Saber que existe um resto e não ter como vê-lo seria pior do que cortar.

## 0.5 Em inglês não há ofertas — há o canal

O grupo é brasileiro: os preços são em real, as lojas entregam no Brasil e os cupons só funcionam
aqui. Mostrar isso a quem trocou a tela para inglês seria oferecer uma coisa que a pessoa não tem
como comprar, e ocupar com ela o espaço de algo que ela **pode** usar.

Em inglês, então: a faixa vira "FROM THE CREATOR · PC do Fafa on YouTube", com o botão âmbar
"Open the channel" no mesmo lugar do "VER NO GRUPO" e o QR apontando para o canal
(`canal=` do `ofertas.ini`, sem chave nova). Somem a aba lateral "OFERTAS · N novas", a coluna de
ofertas, a pílula do cupom e **o polling** — ninguém deve pagar tráfego por uma faixa que não vai
mostrar oferta nenhuma. O "Support the channel" continua no rodapé.

A troca de idioma vale **na hora**: a janela é remedida, a coluna fecha se estava aberta, o QR é
refeito para o novo destino e o evento passa a ser `clique_canal` em vez de `clique_grupo` —
senão a contagem do servidor diria que o grupo recebeu a visita de quem foi para o YouTube.

## 0.6 A atualização, de ponta a ponta

Você vai mexer no link de download da página `/dlss` e quer que o programa **entenda** e avise.
A cadeia foi auditada inteira e testada com um pacote de verdade.

**O `versao.json` é a única fonte.** O launcher nunca lê a página de download — ele consulta
`[atualizacao] url_manifesto=` (ou `url_base` + `/versao.json`). Se a página mostrar um link
diferente do que está no manifesto, **quem manda é o manifesto**. Isso não mudou; está escrito
agora no LEIA-ME para não depender de memória de ninguém.

**Quando ele checa:** na abertura, sempre. E **a cada 30 minutos enquanto a janela ficar aberta** —
isto é novo. Antes, publicar com o launcher na tela não produzia efeito nenhum até a pessoa
fechar e abrir. Agora a pílula "atualização disponível" aparece sozinha ao lado do nome. A
**caixa que pergunta continua só depois do INICIAR**: nada pula na frente de quem está fazendo
outra coisa.

**A caixa ficou curta, a pedido seu.** Ela existe para uma decisão de dois caminhos, e quanto
mais texto tem, mais tempo a pessoa gasta decidindo uma coisa que já decidiu ao abrir o
programa. Ficou: título **"Nova atualização disponível"**, corpo **"Versão 1.2 do DLSS5 For
All."**, botões **"Atualizar agora" / "Depois"** e a caixinha **"Atualizar automaticamente"**.

O que saiu de dentro dela foi para o **rodapé**, na letra pequena do TaskDialog: o tamanho, a
data e o link **"o que mudou"** — "1010 KB · 2026-09-11 · o que mudou". As notas de versão
deixaram de aparecer na caixa e vivem atrás desse link (o mesmo `url_novidades` do `ofertas.ini`
que a tela já usava). Quem quer a lista clica; quem só quer atualizar não passa por ela. O link
abre o navegador **sem fechar a caixa** — a pessoa lê e volta para responder. Nada do rodapé é
obrigatório: sem tamanho e sem link, ele simplesmente não aparece.

O `publicar-versao.ps1` já gravava `tamanho_bytes` no `versao.json` (a página de download usava);
o launcher é que o ignorava. Fotos: `B-aviso-atualizacao.png` e `B-aviso-atualizacao-en.png`.

**O teste de verdade**, com o servidor falso servindo um `pacote.zip` real e um `versao.json` com
o SHA-256 e o tamanho calculados dele:

| Passo | Resultado |
|---|---|
| o launcher vê a versão 1.3 (instalada 1.1) | `versao: NOVA VERSAO 1.3 (instalada 1.1)` no log |
| INICIAR → a caixa aparece com versão, tamanho, data e notas | foto |
| "Sim, atualizar" → baixa | janela de progresso com "x de y MB" |
| confere o SHA-256 | bateu; se não batesse, nada seria trocado |
| troca os arquivos e reabre | `ok - reabrindo o launcher (--nvidia)` |
| a prova | o `versao.txt` da raiz passou de ausente para **1.3**, e o módulo abriu |

**Como você publica** (as 5 linhas que entraram no LEIA-ME): `amd\publicar-versao.ps1 -Versao 1.3
-Publicar` monta o zip, calcula o hash, escreve o `versao.json` e envia os dois. Um comando.

## 0.3 O ícone novo entrou nas três

A capivara meio pixel-art meio realista com a moldura azul neon está **no cabeçalho das três
variações**, 72 unidades lógicas, à esquerda do nome do produto e do estado da placa.

**Por que no cabeçalho, e não só na barra de tarefas:** a imagem *conta o que o programa faz* —
de um lado a imagem crua, de pixel grosso; do outro, a imagem que o DLSS devolve. Isso é a
explicação do produto numa figura, e o lugar de uma explicação é onde o olho chega primeiro.
Ela também dá ao launcher a única coisa que faltava para ele parecer um produto e não um
utilitário: uma identidade visual própria. (Steam, GOG, Battle.net e Riot Client todos abrem com
a marca no alto à esquerda.)

**A paleta não precisou mudar.** A moldura neon do desenho é da mesma família do ciano que a tela
já usava (`#22d3ee`, da `marca\PALETA.md`), que por sua vez veio do site do grupo. O ícone e o
botão INICIAR são a mesma cor de luz — foi coincidência feliz, não ajuste.

**Detalhes técnicos:** o ícone é o recurso 1 do próprio exe (`marca\DLSS5ForAll.ico`, para onde o
`launcher.rc` já apontava — então a **barra de tarefas e as Propriedades do arquivo também ganharam
o desenho novo**, sem nenhuma mudança). Ele é desenhado com `DrawIconEx`, que é de `user32` e faz a
mistura com o fundo sozinho, inclusive o canal alfa das bordas; não entrou nenhuma biblioteca nova
no build. Ele é **recarregado quando o DPI muda**, nunca esticado — esticar borraria justamente o
lado pixel-art, que é a metade que carrega o significado. Se o recurso faltar, o cabeçalho volta a
começar na margem e nada quebra.

Confirmei que a pílula "atualização disponível 1.3" continua cabendo ao lado do nome mesmo com o
ícone ocupando o começo da linha (`launcher\B-versao-nova.png`), e que o desenho fica correto a
100% (`B-dpi100.png`) e a 125% (`B-dpi125.png`).

---

## 1. De onde eu parti: o que já existia quando peguei o trabalho

Um agente anterior tinha começado o redesenho e a sessão dele caiu no meio. Achei no disco,
**sem relatório nenhum**, uma implementação já bem adiantada: `launcher.cpp` com +216 linhas e
`splash_ofertas.h` reescrito em cerca de 1.160 linhas, mais um `.exe` compilado às 04:11 e um
`launcher.log` de 04:13 provando que ele chegou a abrir uma vez. **Não joguei fora.** Li o código,
confirmei que ele estava coerente, e ele virou a **variação A** deste relatório.

O que ele já tinha resolvido (e que eu mantive nas três variações):

- O nome do produto grande no alto, a versão ao lado, a linha de valor e o **estado da placa**
  ("RTX 4080 detectada · pronta", com bolinha verde/amarela/cinza).
- **Um** botão de ação — INICIAR — grande, na largura toda, em ciano.
- O seletor de placa virou **chips pequenos** abaixo do INICIAR (que depois saíram de vez, a
  pedido seu — seção 0.2).
- O cartão **"Continuar de onde parou"**, que lê a última sessão do `dlss5forall.ini` do módulo.
- Os **chips de idioma** (Português/English) no cabeçalho, que gravam a escolha nos dois `bin`
  e avisam a vitrine para recarregar na língua nova.
- A vitrine virou **gaveta** com aba lateral e contador ("OFERTAS · 3 novas").
- A pílula de "atualização disponível" só avisa; **a caixa que pergunta continua sendo o TaskDialog
  padrão, e só depois do INICIAR**.

**Onde ele divergiu do pedido final:** ele empurrou a propaganda para um rodapé de uma linha, uma
aba lateral e um cartão escondido. Isso resolve "não descaracterizar o app", mas **não cumpre a
segunda metade da sua regra** — "pode ser um banner bem evidente, estilo GOG/Steam/Epic". Ele
provavelmente trabalhou com o briefing anterior à sua decisão. As variações **B** e **C** são
minhas, e existem exatamente para atender às duas metades ao mesmo tempo.

---

## 2. A fase de estudo: checklist de referência, item a item

O dossiê completo (fichas de cada launcher, links, o que não foi encontrado) foi produzido por
uma pesquisa dedicada. Aqui está o **checklist verificável**, com o resultado das três variações
lido nas fotos. Cada item se responde olhando a foto.

Legenda: ✅ cumpre · ⚠️ cumpre em parte · ❌ não cumpre · — não se aplica

### 2.1 Hierarquia e ação principal

| # | Item | Origem | A | B | C |
|---|---|---|:-:|:-:|:-:|
| 1 | Existe **exatamente um** botão visivelmente maior e mais colorido | NN/g, *Top 10 Application-Design Mistakes* | ✅ | ✅ | ✅ |
| 2 | Esse botão é o **INICIAR**, e não um botão de loja/grupo/doação | Revolta pública do Battle.net: *"the teeny tiny 'play' button"* | ✅ | ✅ | ✅ |
| 3 | Nenhum outro elemento usa a **mesma cor sólida** do botão principal | NN/g, *Banner Blindness Revisited* | ✅ | ✅ | ✅ |
| 4 | A ação principal está acima da dobra, sem rolar | NN/g, *The Fold Manifesto* (102% mais fixações nos 100 px de cima) | ✅ | ✅ | ✅ |
| 5 | Não há dois centros de gravidade visuais disputando | NVIDIA App como contraexemplo (abre na aba de notícias) | ✅ | ✅ | ⚠️ |
| 6 | O botão principal tem ao menos o dobro da área de qualquer secundário | Lei de Fitts | ✅ | ✅ | ✅ |
| 7 | A área do INICIAR nunca é menor que a do título do banner | regra derivada da queixa da Blizzard | ✅ | ✅ | ✅ |
| 7b | A marca do produto aparece no alto à esquerda, como imagem | Steam, GOG, Battle.net, Riot Client | ✅ | ✅ | ✅ |

Item 5 na C: a coluna de ofertas é grande e colorida. Ela não compete com o INICIAR em *cor*
(nenhum ciano), mas compete em *área*. É o preço da variação C, e está declarado.

### 2.2 Grade e espaçamento

| # | Item | Origem | A | B | C |
|---|---|---|:-:|:-:|:-:|
| 8 | Todas as medidas em múltiplos de 8 (respiro lateral 32, faixa 128, INICIAR 60) | Fluent 2, *Layout* | ✅ | ✅ | ✅ |
| 9 | O respiro lateral é o mesmo em cima, embaixo e nos lados do bloco | Fluent 2 | ✅ | ✅ | ✅ |
| 10 | Nada encosta na borda da janela sem intenção | — | ✅ | ✅ | ✅ |

### 2.3 Tipografia

| # | Item | Origem | A | B | C |
|---|---|---|:-:|:-:|:-:|
| 11 | No máximo 5 tamanhos de fonte na tela | Windows, *Typography* | ✅ (5) | ✅ (5) | ✅ (5) |
| 12 | Uma família só (Segoe UI, a do sistema) | Windows, *Typography* | ✅ | ✅ | ✅ |
| 13 | Hierarquia clara por tamanho: nome 34 > INICIAR 28 > corpo 14 > rótulos 12 | Windows, *Typography* | ✅ | ✅ | ✅ |
| 14 | Nenhum texto abaixo de 12 px lógicos | WCAG 1.4.3 na prática | ✅ | ✅ | ✅ |

### 2.4 Paleta

| # | Item | Origem | A | B | C |
|---|---|---|:-:|:-:|:-:|
| 15 | Uma cor de destaque para AÇÃO (ciano) | NN/g | ✅ | ✅ | ✅ |
| 16 | Se houver segunda cor, ela tem papel **declarado e exclusivo** | NN/g, "escolha cores… cuidadosamente" | — | ✅ âmbar = oferta | ✅ |
| 17 | O texto sobre a cor de destaque passa em contraste | WCAG 1.4.3 | ✅ | ✅ 8,6:1 (preto sobre âmbar) | ✅ |
| 18 | A cor da marca da placa (verde/vermelho) aparece só como bolinha, nunca como botão inteiro | — | ✅ | ✅ | ✅ |

### 2.5 Estados de controle

| # | Item | Origem | A | B | C |
|---|---|---|:-:|:-:|:-:|
| 19 | Repouso, **hover** e pressionado distintos no INICIAR | NN/g | ✅ | ✅ | ✅ |
| 20 | Uma ressalva do sistema vem **escrita**, não como controle cinza mudo | NN/g heurística 9; Smashing, *disabled buttons* | ✅ | ✅ `B-amd-experimental.png` | ✅ |
| 21 | Estado de **carregando** nomeado ("verificando a versão…") | NN/g, *Progress Indicators* | ✅ | ✅ | ✅ |
| 22 | Estado **offline** que não parece defeito | NN/g, *Empty States* | ✅ `B-sem-rede.png` | ✅ | ✅ |
| 23 | Estado **vazio** (sem última sessão) simplesmente some, não deixa buraco | NN/g, *Empty States* | ✅ `B-primeiro.png` | ✅ | ✅ |
| 24 | O alvo grande acende inteiro no hover, não só o botão dentro dele | Lei de Fitts | — | ✅ `B-hover-banner.png` | ✅ |

### 2.6 Primeira execução vs. recorrente

| # | Item | Origem | A | B | C |
|---|---|---|:-:|:-:|:-:|
| 25 | **Nenhuma** sequência de telas de tutorial | NN/g: *"avoid creating app onboarding whenever possible"* | ✅ | ✅ | ✅ |
| 26 | Placa e idioma detectados sozinhos; ninguém é obrigado a responder nada | NN/g, *recognition over recall* | ✅ | ✅ | ✅ |
| 27 | A primeira abertura é diferente da recorrente (a gaveta nasce aberta uma vez) | Steam, *Start Up Location* | ✅ | ✅ | ✅ |
| 28 | Na recorrente, o que a pessoa usou por último aparece pronto | NN/g, *recognition over recall* | ✅ | ✅ | ✅ |

### 2.7 Microcópia e respeito

| # | Item | Origem | A | B | C |
|---|---|---|:-:|:-:|:-:|
| 29 | Zero jargão na tela (nada de "backend", "hook", "swapchain") | Microsoft, *Writing style* | ✅ | ✅ | ✅ |
| 30 | Os botões dizem o que **fazem**, em voz ativa | Microsoft, *Writing style* | ✅ | ✅ | ✅ |
| 31 | A ordem dos botões **não muda** entre aberturas | anti-*dark pattern* | ✅ | ✅ | ✅ |
| 32 | Nada de contagem regressiva, urgência falsa ou animação | NN/g, *Deceptive Patterns* | ✅ | ✅ | ✅ |
| 33 | Fechar no ✕ **não** abre nada | anti-*dark pattern* | ✅ | ✅ | ✅ |
| 34 | Um clique sem querer no bloco da oferta **não** fecha o launcher | lei de Fitts + prevenção de erro | ✅ | ✅ (corrigido, ver 6.2) | ✅ |
| 35 | A promoção tem **rótulo honesto** dizendo de quem ela é | NN/g: "não misture conteúdo e anúncios na mesma seção" | ⚠️ | ✅ "OFERTA DE HOJE" / "DO CANAL" | ✅ |
| 35b | Dá para **desligar** a promoção, e desligada ela fica | Steam e Battle.net; anti-"Nagging" da NN/g | — | ✅ "ocultar" / "reativar" | ✅ |

### 2.8 Acessibilidade

| # | Item | Origem | A | B | C |
|---|---|---|:-:|:-:|:-:|
| 36 | Alvos de clique com no mínimo 24×24 px lógicos | WCAG 2.5.8 | ✅ (menor: 30) | ✅ | ✅ |
| 37 | Contraste de texto ≥ 4,5:1 | WCAG 1.4.3 | ✅ | ✅ | ✅ |
| 38 | Contraste de borda de controle ≥ 3:1 | WCAG 1.4.11 | ✅ | ✅ | ✅ |
| 39 | Funciona em 100%, 125% e 150% sem cortar nada | PerMonitorV2 | ✅ | ✅ `B-dpi100.png`, `B-dpi125.png` | ✅ |
| 40 | Enter aciona o INICIAR; Esc fecha | — | ✅ | ✅ | ✅ |
| 41 | **Foco de teclado visível** (Tab percorrendo os controles) | WCAG 2.4.7 | ❌ | ❌ | ❌ |

**O item 41 é a falha real das três**, e é herdada: a janela é desenhada à mão em GDI e não tem
navegação por Tab. Enter e Esc funcionam, o resto é mouse. Está na seção 7 como pendência.

---

## 3. O problema central: como ter um banner evidente sem o app virar anúncio

Esta é a parte que a sua regra pediu para resolver, e a pesquisa respondeu com números.

**O risco não é o banner ser ignorado — é a região morrer.** O eyetracking original da NN/g (2007)
achou que usuários **quase nunca olham para o que parece anúncio**; a revisão de 2018 mediu o
efeito "batata quente": depois de avistar um anúncio numa área, a pessoa **nunca mais olha
naquela região**. O número que dói: numa página analisada, a coluna direita ocupava **25% da área**
e recebeu **0,8% da atenção** — 33 vezes menos atenção do que área.
([2007](https://www.nngroup.com/articles/banner-blindness-original-eyetracking/) ·
[2018](https://www.nngroup.com/articles/banner-blindness-old-and-new-findings/))

**Área grande não compra atenção.** Esse dado mata a ideia de que "banner evidente = banner
grande". E o atalho que funcionaria — disfarçar a promoção de função do app — é justamente o que
o Nielsen alerta que *"backfires and misleads users"*. Num app grátis que injeta DLLs em jogos,
onde o projeto concorrente (OptiScaler) **precisa avisar no README que versão paga é golpe**,
essa troca sai caríssima.

**Onde colocar.** A análise de 57.453 fixações da NN/g mostra 102% mais atenção nos 100 px acima
da dobra, e o artigo conclui que *"os usuários rolam quando há razão para isso"*.
([The Fold Manifesto](https://www.nngroup.com/articles/page-fold-manifesto/)) Traduzindo: a oferta
vai **abaixo do INICIAR e acima da dobra** — visível sem rolar, mas nunca antes da função.

**Não achei — e não inventei.** Nenhum design system (Fluent, Windows, WCAG, NN/g) publica um
**percentual máximo de tela para promoção**. Nem o PDF oficial de merchandising da Epic entregou
esse número. No lugar dele, uma regra derivada das duas evidências acima e **verificável numa
foto**: *a área do botão INICIAR nunca pode ser menor que a área do título do banner*. É a
tradução operacional exata da queixa da Blizzard.

**O que fura a cegueira, segundo o estudo de 2007, é texto simples e específico — não arte de
banner.** Foi isso que definiu o desenho da faixa: ela mostra **a oferta de verdade, com produto e
preço** ("RTX 4070 Super por R$ 3.499"), vinda do `ofertas.json` do servidor, e o rótulo acima
dela muda junto — "OFERTA DE HOJE" quando o servidor respondeu, "DO CANAL" quando é o texto fixo
do `ofertas.ini`. A faixa nunca promete "oferta de hoje" mostrando uma manchete de sempre.

**A receita que saiu disso, e que as variações B e C seguem:**

1. Rótulo honesto em cima ("OFERTA DE HOJE" / "DO CANAL") — separação editorial.
2. Cor própria (âmbar), que **nunca** é a cor do INICIAR.
3. Lugar: depois de tudo o que é do programa, acima da dobra.
4. Silêncio: sem carrossel, sem animação, sem contagem regressiva.
5. Conteúdo específico e fresco, não manchete genérica.
6. Clique errado não custa nada (ver 6.2).

---

## 4. As três variações, com embasamento

### Variação A — "Gaveta"
**Foto:** `launcher\A-recorrente.png`

O convite cabe numa linha de rodapé ("Feito pelo PC do Fafa · Entrar no grupo · Apoiar o canal ·
CUPOM FAFA10") e numa aba vertical na lateral ("OFERTAS · 3 novas"). O QR e o texto de venda vivem
num cartão que abre por cima quando a pessoa pede.

- **De onde veio:** os utilitários do nicho — Lossless Scaling, Magpie, OptiScaler, ReShade,
  DLSS Swapper — que têm **zero** promoção na tela de trabalho. E a gaveta lateral é o padrão de
  *drawer* do Fluent.
- **Princípio:** *progressive disclosure* — a tela inicial mostra o que 100% das pessoas precisam;
  o resto fica disponível para quem pedir. ([NN/g](https://www.nngroup.com/articles/progressive-disclosure/))
- **Resolve:** o app fica impecável em qualquer print, vídeo ou review. Ninguém nunca vai escrever
  "esse programa tem anúncio".
- **Prós:** a tela mais limpa das três; a menor janela (930×621 px a 150%); zero risco de o app ser
  descaracterizado.
- **Contras:** **não cumpre a sua regra do banner evidente.** Quem abre por 2 segundos e clica em
  INICIAR **não vê o grupo** — vê duas palavras de rodapé. O alcance do grupo depende de a pessoa
  reparar numa aba vertical.
- **Custo:** zero — já está pronto e testado.

### Variação B — "Faixa" ← **a escolhida**
**Fotos:** todas as da tabela da seção 6.3 — `B-recorrente.png`, `B-primeiro.png`,
`B-ingles.png`, `B-sem-rede.png`, `B-hover-banner.png`, `B-amd-experimental.png`,
`B-nenhuma-placa.png`, `B-uma-placa.png`, `B-faixa-oculta.png`, `B-popover.png`,
`B-dpi100.png`, `B-dpi125.png`

A mesma coluna de produto, mais um **bloco largo de 128 unidades de altura** entre o programa e o
rodapé: rótulo de seção, filete âmbar na lateral, QR de verdade, título com o produto e o preço,
duas linhas de texto e um botão âmbar. Quando há cupom, ele aparece como pílula ao lado do botão.
O botão "Entrar no grupo" sai do rodapé (ele estaria pedindo a mesma coisa duas vezes).

- **De onde veio:** os banners de destaque da **GOG Galaxy** e da **Steam** — bloco largo, arte,
  título, botão, espaço de verdade — combinados com o padrão de **seção rotulada** que a Steam
  usa no menu novo (Browse, Recommendations, Categories & Tags).
- **Princípio:** hierarquia visual (o INICIAR continua o maior e o único ciano) + lei de Fitts (o
  bloco inteiro é alvo) + a separação editorial da NN/g (o rótulo).
- **Resolve:** as duas metades da sua regra ao mesmo tempo. Quem olha 2 segundos vê **um app de
  DLSS 5 com um botão de iniciar óbvio** *e* **um bloco de oferta com preço e QR**. Nenhum dos dois
  apaga o outro.
- **Prós:** a oferta é específica e fresca (vem do servidor), que é a forma que a pesquisa aponta
  como a que **não** dispara a cegueira; o QR fica visível sem nenhum clique; funciona sem rede
  (cai no texto do `ofertas.ini` e troca o rótulo).
- **Contras:** a janela cresce de 621 para 867 px de altura a 150% — em um notebook de 768 px de
  altura real ela vai precisar de rolagem ou de corte (não testei em tela baixa). E a segunda cor
  de destaque precisa de disciplina: se o âmbar vazar para outro lugar da interface, o efeito
  acaba.
- **Custo:** já implementado e testado. Zero para adotar.

### Variação C — "Vitrine"
**Foto:** `launcher\C-recorrente.png`

A faixa da B **mais** a coluna de ofertas sempre aberta à esquerda — sem gaveta, sem aba. Janela
de 1608 px de largura a 150%. Sem rede ou sem WebView2, ela **cai sozinha** na variação B.

- **De onde veio:** Epic Games Launcher e GOG Galaxy, onde a loja é uma coluna permanente ao lado
  da biblioteca.
- **Princípio:** *recognition over recall* levado ao limite — a pessoa não precisa nem lembrar que
  existe um grupo; as ofertas estão lá.
- **Resolve:** o alcance máximo do grupo. É a variação que mais parece "launcher de loja".
- **Prós:** a mais vistosa; a que mais gera clique no grupo; a coluna atualiza sozinha.
- **Contras:** **é a que mais se aproxima de descaracterizar o app** — metade da tela é conteúdo
  comercial, e o item 5 do checklist (dois centros de gravidade) fica ⚠️. Exige monitor largo; num
  monitor estreito ela vira a B de qualquer jeito. E paga o WebView2 na abertura toda (4-6 s de
  cache já medidos em relatório anterior), que a A e a B só pagam quando a gaveta abre.
- **Custo:** já implementado. Zero para adotar.

### Tabela comparativa

| Critério | A — Gaveta | B — Faixa | C — Vitrine |
|---|---|---|---|
| **Clareza do produto em 2 segundos** | máxima | alta | média |
| **Visibilidade do grupo sem descaracterizar** | baixa (não cumpre a regra) | **alta e rotulada** | máxima, mas no limite |
| **Cumpre "banner evidente estilo GOG"?** | ❌ | ✅ | ✅ |
| **Risco de parecer adware** | nenhum | baixo (rótulo + cor própria + lugar) | médio |
| **Altura da janela (150%)** | 621 px | 867 px | 867 px |
| **Largura da janela (150%)** | 930 px | 930 px | 1608 px |
| **Custo de abertura** | leve | leve | paga o WebView2 sempre |
| **Funciona sem rede** | ✅ | ✅ (troca o rótulo) | ✅ (vira a B) |
| **Esforço para adotar** | zero | zero | zero |
| **O que há de novo aqui** | gaveta com contador de novas | **faixa viva: a última oferta do grupo, atualizando sozinha com animação; texto por tipo de item; em inglês, o canal no lugar das ofertas; alvo grande que não fecha a janela** | queda automática para a B |

---

## 5. Ideias novas que ninguém pediu

Três já estão **implementadas e nas fotos**; as outras são propostas, com custo estimado.

### Implementadas

**1. A faixa mostra a oferta de verdade, não uma manchete.** O título e o texto da faixa vêm do
`ofertas.json` do servidor, com produto e preço do dia; o `ofertas.ini` é só o que fica quando o
servidor não responde.
*De onde veio:* o achado central do eyetracking de 2007 — o que atrai o olho em espaço
publicitário é **texto simples e específico**, não arte de banner.
*Resolve:* o dilema inteiro da seção 3. Uma linha com "RTX 4070 Super por R$ 3.499" tem mais
chance de ser lida do que um bloco bonito com uma frase genérica.
*Contra honesto:* depende de você publicar oferta fresca. Oferta velha na faixa é pior que
nenhuma faixa. *Custo: zero* — o mecanismo do `ofertas.json` já existia; eu só liguei a faixa nele.

**2. O rótulo da faixa muda conforme a origem do texto.** "OFERTA DE HOJE" quando o servidor
respondeu; "DO CANAL" quando é o texto fixo do ini. *Resolve:* a faixa nunca promete frescor que
não tem. *Custo: zero.*

**3. O alvo grande não cobra caro pelo clique errado.** O **botão âmbar** abre o grupo e fecha o
launcher (pedido explícito). **O resto da faixa** abre o cartão com o QR grande — nada fecha.
*De onde veio:* lei de Fitts diz para fazer o alvo grande; prevenção de erro (heurística 5) diz
que um alvo grande é grande também para o engano.
*Resolve:* antes, um clique em qualquer ponto da faixa fechava o launcher, abria o navegador **e
iniciava o programa** — três coisas que a pessoa não pediu. *Custo: zero.*

### Propostas (não implementadas)

**4. Chave para desligar a faixa, e uma vez desligada não volta.** ✅ **Implementada** — você
aprovou. Um "ocultar" discreto à direita do rótulo; no lugar da faixa fica a linha "Ofertas do
canal ocultas · reativar", e a janela encolhe 201 px na hora. O estado vai para
`[atualizacao] faixa=` no `dlss5forall.ini` da raiz, ao lado do da gaveta, e vale nas próximas
aberturas. Fotos: `launcher\B-recorrente.png` (com o "ocultar") e `launcher\B-faixa-oculta.png`.
*De onde veio:* Steam (`Settings > Interface > Start Up Location`) e Battle.net (`Settings > App >
On startup, view`) — dois dos oito grandes têm essa chave documentada, e nos outros seis a ausência
é a principal fonte de reclamação pública.
*Por que importa aqui:* no seu nicho, **cinco de cinco concorrentes não têm promoção nenhuma**. A
chave de desligar é o que converte um banner em convite, e é a diferença entre isso e o *dark
pattern* "Nagging" da NN/g.
*Detalhe que ficou melhor do que eu esperava:* com a faixa oculta, o botão "Entrar no grupo" e a
pílula do cupom **voltam ao rodapé** sozinhos (era o comportamento da variação A, que a faixa
suprimia para não pedir a mesma coisa duas vezes). Ou seja: ocultar tira o bloco grande, não o
acesso ao grupo. Quem desligou continua conseguindo entrar em um clique.
*Contra honesto:* uma parte vai desligar e o alcance cai.

**5. Painel de iniciar fixo no rodapé.** O INICIAR ancorado numa faixa de altura constante no pé da
janela, que nada empurra — nem a faixa, nem a explicação de "sem placa", nem um cartão futuro.
*De onde veio:* o *"persistent Play panel"* do Riot Client, mais o "alvo infinito" da borda da tela
(lei de Fitts). *Resolve:* hoje o INICIAR muda de lugar conforme a tela cresce (compare
`B-recorrente.png` com `B-nenhuma-placa.png`: 90 px de diferença). *Contra:* come altura fixa.
*Custo: baixo.*

**6. Diagnóstico no lugar do vazio da vitrine.** Enquanto a página não carregou, o mesmo retângulo
mostra três linhas que o app já sabe: "Placa: RTX 4080 ✓ · Modo de janela: sem bordas ✓ · Rede
neural: carregada ✓". *De onde veio:* NN/g, *Designing Empty States* — um estado vazio deve trazer
status + pista + caminho para a tarefa. *Resolve:* o buraco preto vira informação e dá motivo para
confiar antes de clicar. *Contra:* um falso negativo assusta sem necessidade. *Custo: baixo a
médio* — o desenho é trivial, o custo está em ter os três sinais prontos e confiáveis.

**7. Três marcadores de "o que mudou" dentro do TaskDialog de atualização.** Em voz ativa, no
máximo oito palavras cada. *De onde veio:* o NVIDIA App declarou ter posto *"easy-to-scan bullet
points"* na aba de drivers. *Resolve:* hoje a caixa pergunta "tem versão nova, quer?" sem conteúdo,
e num app que injeta DLLs isso é decidido por medo ou por hábito. *Contra:* obriga você a escrever
três marcadores honestos por versão, em pt e en. *Custo: baixo em código, médio em disciplina.*

---

## 6. O que foi testado, e o que os testes acharam

Todo o teste roda num **laboratório isolado** no scratchpad: uma raiz por cenário, com um
`DLSS5ForAll.exe` falso no lugar do módulo (ele só anota qual pasta foi aberta e com qual
argumento) e um servidor falso em `127.0.0.1:8793` que serve a vitrine, o `ofertas.json` e recebe
os eventos. **A sua cópia em `bin\` não foi tocada, e nenhum processo seu foi encerrado** — as
fotos saem por `PrintWindow` no monitor secundário, sem trazer janela para a frente e sem mexer no
mouse.

### 6.1 O INICIAR chama quem deve, e os eventos saem certos

Com o seletor fora da tela, a conferência mais importante passou a ser esta: **o launcher decide
certo sozinho?** As quatro primeiras linhas são a ordem fixa da seção 0.2, exercitada uma a uma.

| Cena | O que o app falso registrou | Evento enviado |
|---|---|---|
| INICIAR, só NVIDIA na máquina | abriu `bin\` sem argumento | `clique_nvidia` ✅ |
| INICIAR, **NVIDIA e AMD** na máquina | abriu `bin\` sem argumento (a NVIDIA ganha) | `clique_nvidia` ✅ |
| INICIAR, **só AMD** | abriu `bin-amd\` com `--amd` | `clique_amd` ✅ |
| INICIAR, AMD detectada mas **sem a pasta do módulo** | abriu `bin\` sem argumento (cai para sem DLSS) | `clique_sem_dlss` ✅ |
| INICIAR, nenhuma placa compatível | abriu `bin\` sem argumento | `clique_sem_dlss` ✅ |
| botão âmbar da faixa | abriu `bin\` (comportamento herdado) | `clique_grupo` ✅ |
| **pílula do cupom na faixa** | copiou o código e abriu `bin\` | `clique_cupom` ✅ |
| corpo da faixa | **não abriu nada** (abre o cartão do QR) | — |
| Apoiar o canal | abriu `bin\` (comportamento herdado) | `clique_canal` ✅ |
| **ocultar a faixa** | não abriu nada, não fechou | `ocultou_faixa` ✅ |
| fechar no ✕ | não abriu nada | `fechou_sem_escolher` ✅ |
| trocar para English | não abriu nada, não fechou | — |

Mais `abriu_vitrine` quando a gaveta chega a ficar aberta, `clique_cupom` na pílula do cupom e
`reativou_faixa` no caminho de volta. **Os sete nomes de evento do pedido estão preservados**; os
dois nomes novos (`ocultou_faixa`, `reativou_faixa`) foram acrescentados a pedido, pelo mesmo
caminho anônimo dos outros — saber quanta gente desliga a faixa é o único jeito de descobrir se
ela incomoda, e é para isso que a chave existe.

**Sobre os argumentos:** `--nvidia` e `--sem-dlss` **não chegam ao `bin\`** — só o `--amd` é
passado. Isso é **de propósito e anterior a este trabalho**: o módulo NVIDIA é o padrão e não
precisa de flag, e as três flags existem para **reabrir o launcher** depois de uma atualização
(`BaixarEAplicar(…, flag, …)`), não para o app. Com o seletor fora da tela a questão deixou de
ter consequência prática: "sem DLSS 5" não é mais uma escolha da pessoa, é o que sobra quando
nenhuma placa serve — e aí o app chegaria à mesma conclusão sozinho.

### 6.2 Defeitos que eu achei e corrigi no caminho

| O que estava errado | Onde aparecia | Correção |
|---|---|---|
| O título da oferta saía cortado no cartão do QR: "RTX 4070 SUPER POR R$ **3.4**" | popover, com publi remota | `DT_CALCRECT` não soma o espaçamento entre letras que o desenho soma; a medida agora desconta isso |
| A versão encostava no nome: "DLSS 5 FOR **ALL**v1.1" | cabeçalho, em todas as telas | a mesma causa, na medição de texto em linha única |
| A pílula do cupom **sumia** do rodapé por poucos pixels | variação A, com cupom | com cupom, o rótulo do grupo encolhe para "Entrar no grupo" e a pílula cabe |
| "source **janela**" | versão em inglês | o nome da fonte agora é traduzido (janela/window, monitor/display, dispositivo/device) |
| "AMD: **módulo AMD** em desenvolvimento" | chip desabilitado | a nota deixou de repetir o nome da placa (e depois o chip inteiro saiu) |
| "PC do **Fafá**", "PC DO **FAFÁ**" — 7 no `splash_ofertas.h`, 1 no `launcher.rc`, 5 no `ofertas.ini` | rodapé, marca, nome do grupo, Propriedades do arquivo | a marca é **"PC do Fafa"**, sem acento |
| O `vitrine.h` tinha uma função JS que **punha** o acento: reescrevia a página do grupo trocando "Fafa" por "Fafá" a cada carga | coluna da vitrine | função removida — uma tela não corrige a ortografia de uma marca por conta própria |
| A mesma frase com **duas traduções** ("QUERO AS OFERTAS" → "I WANT THE DEALS" e "SHOW ME THE DEALS") | faixa vs. botão | ficou "SHOW ME THE DEALS" nos dois |
| "DLSS 5 **in** any game" | slogan em inglês | "**on** any game" — o DLSS é aplicado por cima, não instalado dentro |
| "point your phone camera **here**" | rodapé do cartão | "point your phone camera" |
| O texto da faixa passava por baixo do botão | faixa, texto de 2 linhas | faixa de 112 → 128, e as caixas reposicionadas |
| Um clique em qualquer ponto da faixa fechava o launcher, abria o navegador **e** iniciava o app | faixa | só o botão âmbar faz isso; o resto abre o cartão |

### 6.3 As fotos

Todas em `amd\docs\pesquisa-2026-09-11\launcher\`, tiradas no monitor secundário a 150%
(exceto as de DPI):

| Cenário do pedido | Arquivo |
|---|---|
| (a) primeiro uso, com a oferta em destaque | `B-primeiro.png` |
| (b) uso normal / recorrente | `B-recorrente.png` |
| (c) só uma placa compatível | `B-uma-placa.png` |
| (d) placa AMD — "AMD experimental" e a linha de ajuda | `B-amd-experimental.png` |
| (e) nenhuma placa compatível | `B-nenhuma-placa.png` |
| (f) o cartão da oferta com o QR | `B-popover.png` |
| (g) versão em inglês | `B-ingles.png` (e `B-amd-experimental-en.png`) |
| (h) 100% e 125% de DPI | `B-dpi100.png`, `B-dpi125.png` |
| (i) a faixa ocultada pela pessoa, com o "reativar" | `B-faixa-oculta.png` |
| (j) a troca de oferta, em três momentos | `B-troca-animacao.png` |
| (k) os três tipos de item | `B-tipo-oferta.png`, `B-tipo-cupom.png`, `B-tipo-aviso.png` |
| extra: título longo cortado, com o texto inteiro no hover | `B-titulo-tooltip.png` |
| (l) o aviso de versão nova, pt e en | `B-aviso-atualizacao.png`, `B-aviso-atualizacao-en.png` |
| extra: sem rede — a faixa cai no texto do ini e o rótulo vira "DO CANAL" | `B-sem-rede.png` |
| extra: hover na faixa (o bloco inteiro acende) | `B-hover-banner.png` |
| comparação: a variação A (gaveta) | `A-recorrente.png` |
| comparação: a variação C (vitrine) | `C-recorrente.png` |

Não há mais foto de "duas placas com seletor" nem de "chip desabilitado com motivo": **o seletor
deixou de existir** (seção 0.2). O que era o motivo ao lado do chip virou a linha de ajuda do
modo AMD, em `B-amd-experimental.png`.

---

## 7. O que continua pendente

- **Foco de teclado (item 41).** As três variações não têm navegação por Tab. Enter aciona o
  INICIAR e Esc fecha; o resto exige mouse. Herdado, não introduzido aqui.
- **Tela baixa.** A variação B tem 867 px de altura a 150%. Não testei num monitor de 768 px de
  altura útil. A janela já sabe encolher (ela mede a área de trabalho antes de nascer), mas o
  comportamento exato nessa faixa não foi fotografado.
- **`--sem-dlss` não chega ao app** (6.1). Decisão sua.
- **A faixa atualizando sozinha.** Você pediu depois ("gostei do launcher"): que a faixa mostre
  sempre a última oferta do servidor, trocando sozinha o tempo todo, com uma animaçãozinha a cada
  troca. Ainda **não** está feito — hoje a faixa busca a publi uma vez, na abertura. Fazer isso é
  uma thread que repete a busca de tempos em tempos e um repintar suave na troca; é o próximo
  item, e mexe só na faixa.
- **Nada foi para `bin\`.** Depois que você escolher a variação, o passo é recompilar e copiar com
  o app fechado, pelo método "remover antes de copiar" do `RELATORIO-MIGRACAO-BIN.md`.

---

## 8. Tabela completa de strings (pt | en)

Todas as frases que podem aparecer no launcher, revisadas uma a uma. As de código estão em
`splash_ofertas.h` e `portable-launcher\launcher.cpp`, sempre no par `T(pt, en)` / `TS(pt, en)`;
as de conteúdo vêm do `ofertas.ini` (chave `x=` em português, `x_en=` em inglês) e têm um padrão
embutido para quando a chave falta.

### 8.1 Cabeçalho e produto

| Português | English | Onde |
|---|---|---|
| Português | English | chips de idioma (nome nativo de cada língua, nunca traduzido) |
| *(sem texto)* | *(sem texto)* | ícone do app à esquerda do nome — imagem, não string |
| DLSS 5 FOR ALL | DLSS 5 FOR ALL | nome do produto (não se traduz) |
| DLSS 5 em qualquer jogo ou vídeo | DLSS 5 on any game or video | slogan padrão (chave `slogan=` / `slogan_en=`) |
| atualização disponível | update available | pílula ao lado da versão |

### 8.2 Estado da placa

| Português | English | Onde |
|---|---|---|
| *(nome da placa)* detectada · pronta | *(card name)* detected · ready | linha de estado, verde |
| *(nome da placa)* · AMD experimental | *(card name)* · AMD experimental | linha de estado, amarelo |
| Modo AMD em desenvolvimento: funciona, mas ainda pesa mais. | AMD mode is still in development: it works, but it is heavier. | ajuda curta, abaixo da linha de estado |
| nenhuma placa compatível — o app abre sem DLSS 5 | no compatible card — the app opens without DLSS 5 | linha de estado, cinza |
| não compatível com seu computador | not compatible with your computer | motivo, na linha de estado |
| módulo não encontrado (pasta bin) | module not found (bin folder) | motivo, na linha de estado |
| módulo em desenvolvimento | module in development | motivo, na linha de estado (AMD) |
| atualize o driver Adrenalin (26.1 ou mais novo) | update the Adrenalin driver (26.1 or newer) | motivo, na linha de estado (AMD) |

### 8.3 Ação

O seletor de placa saiu da tela (seção 0.2), e com ele saíram as strings "Rodar em:" / "Run on:"
e "Sem DLSS 5" / "No DLSS 5". Sobrou uma ação e a explicação de quando ela roda sem DLSS.

| Português | English | Onde |
|---|---|---|
| INICIAR | START | o botão principal, a única ação da tela |
| Sem uma placa compatível o DLSS 5 não liga, mas o programa continua útil: ele aplica os efeitos do ReShade em qualquer coisa que apareça na tela — jogo, emulador, vídeo, navegador ou placa de captura — sem instalar nem injetar nada no jogo. | Without a compatible card DLSS 5 stays off, but the program is still useful: it applies ReShade effects to anything on your screen — a game, an emulator, a video, a browser or a capture card — without installing or injecting anything into the game. | explicação, só sem placa |

### 8.4 Continuar de onde parou

O cartão foi removido a pedido do usuário em 11/09/2026, e com ele as strings "CONTINUAR DE ONDE
PAROU", "Repetir" e a frase da última sessão ("fonte janela · 2 passes · RTX 4080"). Nenhuma delas
existe mais no código.


### 8.5 Versão e novidades

| Português | English | Onde |
|---|---|---|
| verificando a versão… | checking for updates… | linha de status |
| versão mais nova instalada | you have the newest version | linha de status |
| atualização disponível | update available | linha de status |
| o que mudou | what changed | link à direita (só com `url_novidades=`) e no rodapé da caixa |
| · atualizado agora (antes: *v*) | · just updated (was: *v*) | assinatura, após uma atualização |

E a caixa que pergunta (TaskDialog padrão do Windows), agora curta:

| Português | English | Onde |
|---|---|---|
| Nova atualização disponível | New update available | título da caixa |
| Versão *X* do DLSS5 For All. | DLSS5 For All version *X*. | o corpo inteiro — e só ele |
| Atualizar agora | Update now | botão padrão (Enter) |
| Depois | Later | botão secundário (Esc) |
| Atualizar automaticamente | Update automatically | caixa de verificação |
| *tamanho* · *data* · o que mudou | *size* · *date* · what changed | rodapé, em letra menor |

### 8.6 A oferta (faixa, rodapé, cartão, gaveta)

| Português | English | Onde |
|---|---|---|
| OFERTA DE HOJE | TODAY'S DEAL | rótulo da faixa, item do tipo oferta |
| CUPOM NOVO | NEW COUPON | rótulo da faixa, item do tipo cupom |
| NO GRUPO AGORA | IN THE GROUP NOW | rótulo da faixa, item do tipo aviso |
| DO CANAL | FROM THE CHANNEL | rótulo da faixa, sem publi do servidor |
| Publicada há *N* min no grupo. O preço muda rápido — o grupo recebe primeiro. | Posted *N* min ago in the group. Prices move fast — the group hears first. | apoio, tipo oferta |
| O preço muda rápido — o grupo recebe primeiro. | Prices move fast — the group hears first. | apoio, tipo oferta sem carimbo de hora |
| Cupom novo no grupo: vale enquanto durar. | New coupon in the group: good while it lasts. | apoio, tipo cupom |
| Novidade no grupo há *N* min. | Something new in the group *N* min ago. | apoio, tipo aviso |
| Novidade no grupo. | Something new in the group. | apoio, tipo aviso sem carimbo de hora |
| agora | just now | "há quanto tempo", abaixo de 2 min |
| há *N* min / há *N* h / há *N* dias | *N* min ago / *N* h ago / *N* days ago | "há quanto tempo" |
| ontem | yesterday | "há quanto tempo", 1 dia |
| FROM THE CREATOR | FROM THE CREATOR | rótulo da faixa em inglês (não se traduz: só existe em inglês) |
| PC do Fafa on YouTube | PC do Fafa on YouTube | título da faixa em inglês |
| Tests, builds and DLSS 5 in practice — subscribe to follow the project. | *(idem)* | apoio da faixa em inglês |
| Open the channel | Open the channel | botão âmbar em inglês |
| ocultar | hide | link à direita do rótulo da faixa |
| Ofertas do canal ocultas | Channel deals hidden | a linha que fica no lugar da faixa |
| reativar | turn back on | link à direita dessa linha |
| Peça de PC pelo menor preço | PC parts at the lowest price | título padrão (`titulo=` / `titulo_en=`) |
| A gente mostra onde comprar barato no Brasil e como importar pagando menos. O app é grátis - o grupo é como você retribui. | We show you where to buy cheap in Brazil and how to import for less. The app is free - the group is how you pay it back. | mensagem padrão (`mensagem=` / `mensagem_en=`) |
| QUERO AS OFERTAS | SHOW ME THE DEALS | botão âmbar / botão do cartão (`botao=` / `botao_en=`) |
| Aponta a câmera no código ou clica no botão | Point your camera or click the button | chamada do cartão (`cta=` / `cta_en=`) |
| aponte a câmera do celular | point your phone camera | rodapé do cartão (`rodape=` / `rodape_en=`) |
| cupom *CÓDIGO* | coupon *CODE* | pílula ao lado do botão âmbar |
| CUPOM *CÓDIGO* | COUPON *CODE* | pílula no rodapé (variação A) |
| Entrar no grupo de ofertas | Join the deals group | botão do rodapé |
| Entrar no grupo | Join the group | o mesmo botão, quando divide a linha com o cupom |
| Apoiar o canal | Support the channel | botão do rodapé |
| Feito pelo PC do Fafa | Made by PC do Fafa | assinatura do rodapé (nome do canal não se traduz) |
| OFERTAS | DEALS | aba da gaveta, fechada |
| OFERTAS · *n* novas | DEALS · *n* new | aba da gaveta, com contador |
| FECHAR | CLOSE | aba da gaveta, aberta |
| OFERTAS DE AGORA | DEALS RIGHT NOW | título da coluna da vitrine |
| DO GRUPO PC DO FAFA | FROM THE PC DO FAFA GROUP | subtítulo da coluna |
| atualiza sozinho | updates on its own | selo da coluna |
| carregando ofertas… | loading deals… | coluna, enquanto a página carrega |
| sem conexão com as ofertas | no connection to the deals | coluna, sem rede |

### 8.7 Erros

| Português | English | Onde |
|---|---|---|
| Não encontrei o programa em:\n*caminho* | Could not find the program at:\n*path* | caixa de erro ao abrir o módulo |
| Não consegui abrir:\n*caminho* | Could not open:\n*path* | caixa de erro ao abrir o módulo |
| \n\nErro do Windows: *n* | \n\nWindows error: *n* | complemento das duas acima |
| \n\nDetalhes em:\n*caminho* | \n\nDetails in:\n*path* | complemento do erro de atualização |

### 8.8 Cartão de relatório de uso (só na sua cópia, com `relatorio_url=`)

| Português | English |
|---|---|
| RELATÓRIO DE USO | USAGE REPORT |
| buscando… | loading… |
| relatório indisponível (sem rede) | report unavailable (no network) |
| último registro guardado: *data* | last saved record: *date* |
| Hoje *n* pessoas · 7 dias *n* · 30 dias *n* | Today *n* people · 7 days *n* · 30 days *n* |
| pessoas por dia · máx. *n* | people per day · max *n* |
| cliques hoje | clicks today |
| cliques em 7 dias | clicks in 7 days |
| (sem cliques) | (no clicks) |
| NVIDIA *n*% / AMD *n*% / sem *n*% | NVIDIA *n*% / AMD *n*% / none *n*% |
| Botão NVIDIA | NVIDIA button |
| Botão AMD | AMD button |
| Abrir sem DLSS 5 | Open without DLSS 5 |
| Quero as ofertas | Show me the deals |
| Apoiar o canal | Support the channel |
| Cupom | Coupon |
| Atualizar | Update |
| Atualização ok | Update ok |
| Atualização falhou | Update failed |
| Fechou sem escolher | Closed without choosing |

### 8.9 Chaves do `ofertas.ini` usadas pelo launcher

Nenhuma chave nova foi inventada por mim; as duas abaixo já tinham sido acrescentadas pelo
trabalho anterior e estão documentadas aqui porque ainda não estavam em lugar nenhum.

| Chave | Para que serve | Padrão |
|---|---|---|
| `slogan=` / `slogan_en=` | a linha de valor abaixo do nome do produto | "DLSS 5 em qualquer jogo ou vídeo" |
| `url_novidades=` | destino do link "o que mudou" | vazio — sem a chave o link **não aparece** |
| `[atualizacao] vitrine_lembrar_a_cada=` | de quantas em quantas aberturas a gaveta volta a nascer aberta | 10 (0 desliga) |

E no `dlss5forall.ini` **da raiz** (estado, não configuração — o launcher escreve nele):

| Chave | O que guarda |
|---|---|
| `[atualizacao] faixa=` | 0 depois que a pessoa clica em "ocultar"; 1 (ou ausente) = faixa visível |
| `[atualizacao] gaveta=` | se a gaveta da vitrine estava aberta na última vez |
| `[atualizacao] aberturas=` | quantas vezes o launcher já abriu (a gaveta volta de N em N) |

---

## 9. Chaves de diagnóstico criadas para estes testes

Todas exigem `DLSS5_DIAG=1` junto, como as que já existiam na base. Nenhuma muda o comportamento
de quem não as define.

| Variável | O que faz |
|---|---|
| `DLSS5_LAUNCHER_LAYOUT=0\|1\|2` | escolhe a variação (Gaveta / Faixa / Vitrine) |
| `DLSS5_LAUNCHER_AREA=l,t,r,b` | centra a janela nesse retângulo em vez da área de trabalho primária |
| `DLSS5_PLACAS=rtx\|radeon\|ambas\|nenhuma` | finge o resultado da detecção, para fotografar cenários que esta máquina não tem |
| `DLSS5_LOG_ITENS=1` | despeja `itens-launcher.txt` com cada área clicável em pixel — é o que permite conferir um clique pelo ponto que a tela recebeu |
| `DLSS5_SEGURA_HOVER=1` | não apaga o realce quando o cursor real está noutro lugar, para fotografar estados de hover |
| `DLSS5_SPLASH_DPI=<n>`, `DLSS5_VITRINE_ESTREITO=1` | já existiam |
