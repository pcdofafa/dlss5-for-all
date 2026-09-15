# Melhorias fáceis e de valor — DLSS5 For All

Data: 2026-09-05. Somente leitura: nenhum arquivo do projeto foi alterado e o app não foi executado.

Base: `amd\README.md`, `amd\portable-launcher\LEIA-ME.txt`, `amd\docs\` (em especial `polimento-produto.md`, `revisao-critica.md` e os quatro relatórios de `docs\pesquisa-2026-09-05\`), `amd\main.cpp` (leitura; outro agente está editando, então os números de linha podem deslizar), `amd\portable-launcher\launcher.cpp`, `setup-dlss5.ps1`, `montar-portable.ps1`, `ofertas.ini`; e a comunidade: issues do DLSS5-Feeder, DLSS5oneclick, DLSS5-Swapper, RHI, 1-Click-DLSS5, DLSS5-Autopilot, obs-dlss5-nr, dlss5-webcam-demo, FAQ do Magpie, fóruns do Lossless Scaling, artigos sobre riscos do mod.

Marcação: **[FATO]** = verificado no código, no doc ou na fonte citada. **[INFERÊNCIA]** = conclusão minha a partir dos fatos; pede confirmação.

Regras de interface respeitadas em toda proposta: no máximo três decisões por tela, nenhuma palavra técnica onde a pessoa decide, tudo reversível por uma tecla (F6), só entra o que funciona sem ajuste, nada que polua; o ReShade completo continua atrás da Home.

---

## Resumo em dez linhas

1. O que a comunidade mais reclama nas ferramentas irmãs é "instalei e não faz nada" (DLSS5oneclick #10, #39; Swapper #148, #143; Nexus). O app já resolve a causa número um (faixa de arquivos faltando), mas ainda deixa passar: tela cheia exclusiva sem aviso, antivírus removendo outros arquivos da cadeia, driver novo derrubando o neural sem explicação.
2. As duas melhorias de maior valor por hora são um **aviso na tela quando o jogo está em tela cheia exclusiva** e a **faixa de arquivos faltando cobrindo a cadeia inteira** (hoje só três arquivos).
3. Para suporte no grupo, um botão **"Pedir ajuda: copiar relatório"** que junta versão, placa, driver e o fim dos três logs vale mais do que qualquer FAQ.
4. Para a comunidade e o canal, **foto antes/depois com um botão** (a base já existe no código: `DLSS5_DUMP`) transforma cada usuário num divulgador.
5. Robustez: o app não grava minidump ao travar, trunca o log a cada abertura e ainda tem variáveis de teste ativas no build de release.
6. O único controle novo de imagem que merece ser planejado é **Estilo (Padrão / Natural / Cinema)**: é o botão de maior efeito da rede, responde direto à queixa "rosto de boneca" e cabe no padrão de três opções. Só entra se der para trocar sem reabrir.
7. Controle de videogame (um acorde de botões = F6) é pedido recorrente do público de sofá e de portátil (Lossless Scaling no ROG Ally e Legion Go); custa um a dois dias.
8. **Não faça**: scanner de biblioteca de jogos, rotas de injeção, três ou mais passadas, sliders da rede no painel, "esconder do anti-cheat", telemetria, atualização silenciosa, controles de latência.
9. A segurança do pacote (SmartScreen e Defender) é o problema de imagem de todas as ferramentas do gênero (Swapper #145, RHI #45). Hash publicado e certificado de assinatura são o caminho; não há atalho.
10. Nenhuma proposta abaixo adiciona uma decisão nova à primeira tela nem ao painel além de um botão ou um aviso; a única exceção é o chip de Estilo, que fica em "vale planejar" e depende de teste.

---

## 1. Faça esta semana (até 1 dia cada, ordenado por valor/esforço)

### 1.1 Aviso "o jogo não está entregando imagem" (tela cheia exclusiva)

- **Problema real.** Tela cheia exclusiva é a limitação número um do README (`README.md`, "Limitações honestas") e o LEIA-ME repete três vezes. Na tela, porém, o único sintoma é `0.0 fps (alvo parado)` no título da janela [FATO, `main.cpp`, função que monta o título; `docs/polimento-produto.md` item 29, ainda aberto]. É também a falha mais comum nas ferramentas irmãs: o README do DLSS5-Feeder lista "exclusive fullscreen" entre as limitações e o 1-Click-DLSS5 documenta crash ao alternar em tela cheia ([Feeder README](https://github.com/jlrouzies-fr/DLSS5-Feeder/blob/main/README.md), [1-Click-DLSS5](https://github.com/reiluisii/1-Click-DLSS5)).
- **O que muda para quem usa.** Se o jogo estiver em tela cheia exclusiva, aparece um cartaz dizendo o que fazer, em vez de uma tela parada.
- **Como.** Fonte Janela, alvo vivo e nenhum quadro do WGC por 5 s: cartaz `O jogo não está entregando imagem / se ele estiver em tela cheia, mude para "janela" ou "sem bordas" nas opções do jogo`. Uma vez por sessão. O app já tem o mesmo padrão para monitor desconectado [FATO, comentário "FONTE MONITOR: nao existe janela para morrer" no laço principal].
- **Esforço.** 2 a 3 h. **Risco.** Baixo: só `Log` + `ShowToast`. Falso positivo em janela parada de verdade (vídeo pausado); por isso o texto diz "se".
- **Regras.** Nenhuma decisão nova; é um aviso.

### 1.2 Faixa de "arquivos faltando" cobrindo a cadeia inteira, com a frase do antivírus

- **Problema real.** `VerFaltaDlss()` confere só `renodx-dlss5.addon64`, `nvngx_dlssnr.dll` e `dxgi.dll`/`d3d11.dll` [FATO, `main.cpp` ~1808-1823]. Antivírus removem justamente os outros: o build de add-ons do ReShade e DLLs que fazem hook de D3D12 são "o perfil clássico de falso positivo" ([Stellar Blade Nexus, guia do instalador](https://www.nexusmods.com/stellarblade/mods/3706); [Swapper #145 "Windows Defender: Trojan Detected"](https://github.com/rakanki911/DLSS5-Swapper/issues); [RHI #45](https://github.com/RankFTW/RHI/issues)). Sem `dlss5-feed.addon64`, `dlss5-bridge.addon64`, `nvngx_dlss.dll`, `DLSS5_Feed.fx` ou `lumenite_kernel*.fx` o app abre, mostra a imagem crua e não avisa nada [INFERÊNCIA a partir da lista do `VerFaltaDlss`].
- **O que muda para quem usa.** A faixa amarela do seletor diz exatamente qual arquivo sumiu e, se for um dos que o antivírus costuma pegar, diz "restaure da quarentena e coloque a pasta nas exclusões".
- **Como.** Estender a lista da faixa com os sete arquivos da cadeia; para os de terceiros redistribuídos no pacote, o botão "Abrir a pasta" já existe; texto da faixa ganha a segunda linha do antivírus quando o arquivo faltante é `dxgi.dll`, `*.addon64` ou `nvngx_*.dll`.
- **Esforço.** 3 h. **Risco.** Baixo. A faixa já tem lógica de mostrar/esconder e de reavaliar ao voltar o foco.
- **Regras.** É a faixa que já existe, com mais casos.

### 1.3 Botão "Pedir ajuda: copiar relatório"

- **Problema real.** O suporte hoje depende de a pessoa achar e abrir `bin\dlss5forall.log`, `dlss5-feed.log` e `ReShade.log` (LEIA-ME, "Nada abre"). Nas ferramentas irmãs, metade das issues chega sem log e vira ping-pong (Feeder #55, #57; Swapper #140, #141; DLSS5oneclick #23, #32). O Feeder pede explicitamente `dlss5-feed.log` e dumps (#44).
- **O que muda para quem usa.** Um clique no painel (e outro na faixa do seletor) monta um texto pronto para colar no grupo: versão do app, placa, driver, fonte, estado do neural, e as últimas 60 linhas de cada log. Salva `bin\relatorio-ajuda.txt` e copia para a área de transferência.
- **Como.** O app já lê o fim do `dlss5-feed.log` (`TailArquivo`) [FATO, `main.cpp` ~3638-3705]. Falta juntar com o próprio log, o `ReShade.log`, a versão do driver (versão de arquivo de `System32\nvngx.dll` ou `nvlddmkm`) e a descrição do adaptador DXGI. Sem envio automático de nada.
- **Esforço.** 4 a 6 h. **Risco.** Baixo. Cuidar de não incluir caminhos com nome de usuário além do necessário.
- **Regras.** Um botão na seção "Como usar", rótulo sem termo técnico.

### 1.4 Foto antes/depois com um botão (e abre a pasta)

- **Problema real.** A comunidade compara com par de screenshots: o RenoDX grava par `_NR_OFF`/NR em `DLSS5 Screenshots\` e o webcam-demo tem F9 que salva quatro imagens ([dlss5-webcam-demo](https://github.com/jpneagle/dlss5-webcam-demo); `docs/pesquisa-2026-09-05/RELATORIO-2X-VISUAL.md` §1). No app não há botão nem tecla para isso; o F6 é ao vivo, não deixa arquivo. A pasta `DLSS5 Screenshots` existe na raiz do projeto [FATO, listagem], então o F5 do RenoDX já funciona aqui, mas ninguém sabe.
- **O que muda para quem usa.** Botão "Salvar foto antes e depois" no painel: grava `antes.png` e `depois.png` com o nome do jogo e a hora, e abre a pasta. É o material que vai para o grupo e para o vídeo.
- **Como.** A gravação da saída da cadeia em PNG já existe atrás de `DLSS5_DUMP=<arquivo.png>` [FATO, comentário "Diagnostico de cor: DLSS5_DUMP" no laço principal]. Sequência: grava "depois", alterna o neural pela ponte (o mesmo caminho do F6), espera os quadros que o F6 já espera, grava "antes", religa. Alternativa mais barata: enviar o F5 ao RenoDX pela ponte e só abrir a pasta.
- **Esforço.** 0,5 a 1 dia. **Risco.** Médio-baixo: reutiliza o caminho do F6, que foi medido e revisado (`docs/revisao-critica.md`, rodada 5). Em HDR a foto sai do tone mapping do app, o que é o correto para comparar.
- **Regras.** Um botão; nenhuma opção.

### 1.5 Nota calma de anti-cheat quando o alvo tem EAC, BattlEye ou Vanguard

- **Problema real.** É o medo número um da comunidade e há relatos de banimento com ferramentas que injetam ([Held Games, riscos](https://heldgames.com/guides/dlss-5-mod-risks); [Sportskeeda](https://tech.sportskeeda.com/gaming-news/modders-built-dlss-5-mod-override-tool-use-it); RHI #50 "Squad banned ReShade"). O DLSS5-Autopilot detecta os três e marca o jogo como bloqueado. O app não injeta nada e diz isso no README, mas na hora de usar não avisa nada [FATO: nenhuma menção a EAC/BattlEye no `main.cpp`].
- **O que muda para quem usa.** Ao escolher um jogo online com anti-cheat, um aviso único: `Este jogo tem anti-cheat. O app não mexe no jogo, só mostra uma camada por cima, mas em jogo online é por sua conta.`
- **Como.** Do HWND do alvo, obter o exe (`QueryFullProcessImageNameW`) e conferir se existe pasta `EasyAntiCheat\`, `BattlEye\` ou o serviço `vgc` rodando. Só texto e log.
- **Esforço.** 3 h. **Risco.** Baixo. Não muda comportamento.
- **Regras.** Aviso, sem decisão. Não é um "modo".

### 1.6 Lista de janelas em ordem "parece jogo" e sem pré-seleção cega na primeira vez

- **Problema real.** Sem escolha salva, `FillTargets` seleciona o índice 0 e a lista vem na ordem do Z-order; em máquina limpa veio um terminal pré-selecionado e quem clica INICIAR filtra a janela errada e conclui que o app não funciona [FATO, `main.cpp` `FillTargets` (`int sel = 0`), `docs/polimento-produto.md` item 26, ainda aberto; não há filtro de `explorer`/terminal no `EnumCaptureWindows`].
- **O que muda para quem usa.** O jogo aparece em primeiro na lista. Se não houver nenhum candidato óbvio, o campo vem vazio e o INICIAR espera a escolha.
- **Como.** Ordenar por área visível decrescente; empurrar para o fim `explorer.exe`, `WindowsTerminal.exe`, `ApplicationFrameHost.exe`, navegadores e o próprio launcher. Na primeira execução, se o primeiro da lista ainda for um desses, `CB_SETCURSEL -1` com dica "Escolha ali em cima a janela do jogo".
- **Esforço.** 3 h. **Risco.** Baixo.
- **Regras.** Menos uma decisão errada na primeira tela.

### 1.7 Aviso único "esta janela não aparece para gravadores" (fonte Monitor)

- **Problema real.** Com fonte Monitor e "esconder" ligado (padrão), o OBS não vê a janela em nenhum método; a pessoa relata "OBS não captura" [FATO, `docs/pesquisa-2026-09-05/RELATORIO-OBS.md` §0 e §4(c), que recomenda exatamente este aviso como opcional]. A documentação já foi corrigida; o aviso na hora evita a ida ao LEIA-ME.
- **O que muda para quem usa.** Ao iniciar com essa combinação, um cartaz de 5 s: `Esta janela fica invisível para OBS e gravadores. Para gravar, use a fonte Janela.`
- **Esforço.** 1 a 2 h. **Risco.** Mínimo.

### 1.8 "Novidades desta versão" uma vez por versão, com o vídeo do canal

- **Problema real.** O app não tem canal de novidades; a tela do grupo é a mesma sempre. Ferramentas irmãs recebem pedido de changelog no app (Swapper #139). Hoje o `ofertas.ini` tem `canal=` e o botão "APOIAR O CANAL" [FATO, `ofertas.ini`, `splash_ofertas.h` linha 12].
- **O que muda para quem usa.** Na primeira abertura após atualizar, o launcher mostra uma linha discreta "Novo na versão 1.2: …" com um botão "Ver o vídeo (2 min)". Nas seguintes, some.
- **Como.** Duas chaves novas em `ofertas.ini` (`novidades=` texto de uma linha, `video=` URL), comparação de `APP_VERSION` com `[ultima] versao_vista` no `dlss5forall.ini`. Sem rede.
- **Esforço.** 3 h. **Risco.** Baixo. Cuidado: uma linha, não um segundo cartaz.
- **Regras.** Sem decisão nova; a tela do grupo continua a única tela do launcher.

### 1.9 Botão "Ver se há versão nova" (abre a página; nada em segundo plano)

- **Problema real.** Não existe caminho de atualização a partir do app [FATO: nenhuma URL de release própria no `main.cpp`; só RHI, mod AMD e o grupo]. A comunidade pede atualizador (Swapper #139), mas também desconfia de qualquer coisa que "liga para casa" (Defender, "trojan").
- **O que muda para quem usa.** Em "Como usar", um botão que abre a página de download do grupo/canal e mostra a versão atual ao lado. Zero rede sem clique.
- **Esforço.** 1 h. **Risco.** Nenhum. Ver também 2.4 para a versão passiva.

### 1.10 Lembrar em que driver o neural subiu pela última vez

- **Problema real.** Driver novo derruba a cadeia sem explicação: Feeder #54 ("com o driver novo o add-on diz que não está funcionando"), #47 (`NVSDK_NGX_D3D12_Init` devolve `0xBAD00001` no 616.56 em três máquinas) [FATO, issues]. O app trata `DEVICE_REMOVED` [FATO, `main.cpp` ~1155], mas quando o neural fica em "INICIANDO" não diz que o driver mudou.
- **O que muda para quem usa.** Se o neural não subir e o driver for diferente do que funcionava, o cartaz diz: `O driver da placa mudou desde a última vez que funcionou (antes 5xx.xx, agora 6xx.xx).` É a pista que faltava para o suporte.
- **Como.** Ler a versão de arquivo de `System32\nvngx.dll` na abertura, gravar em `[ultima] driver_ok=` quando o neural chegar a ATIVO, comparar no caminho do prazo de 25 s [FATO, `main.cpp` ~3893 e ~4022].
- **Esforço.** 4 h. **Risco.** Baixo.

### 1.11 Uma frase de posicionamento: "jogo antigo, 32 bits, DX9, emulador: funciona porque nada entra no jogo"

- **Problema real.** Um terço das issues do Feeder e do DLSS5oneclick é sobre 32 bits, DX9 e emulador (Feeder #43, #54, #33, #42; DLSS5oneclick #17). O app resolve isso por construção (captura, não injeção) [FATO, README linha 11-12], mas o LEIA-ME não vende esse ponto e o launcher também não.
- **O que muda para quem usa.** Quem vem dessas ferramentas entende em uma linha por que aqui funciona.
- **Esforço.** 30 min (LEIA-ME, README.txt, e a legenda do botão verde/vermelho no launcher se couber). **Risco.** Nenhum.

### 1.12 Conferir item a item o que ainda está aberto do `polimento-produto.md`

- Verifiquei [FATO]: KeyOverlay já é Home nos dois setups (item 1 corrigido); seletor já escala com DPI (item 18 corrigido, `E(kSelW)`); `TemAtalho` é usado em 20 pontos (item 2 provavelmente corrigido). Ainda abertos: 26 (pré-seleção, ver 1.6), 29 (tela cheia, ver 1.1), 30 (variáveis de teste, ver 4.4). Não conferi 6 a 17 e 19 a 25; vale meia hora com a lista na mão antes de publicar.

---

## 2. Vale planejar (2 a 5 dias)

### 2.1 Chip "Estilo: Padrão / Natural / Cinema"

- **Problema real.** A queixa pública número um do DLSS 5 é o rosto homogêneo ("yassified") ([Notebookcheck](https://www.notebookcheck.net/Nvidia-CEO-Jensen-Huang-says-gamers-completely-wrong-about-DLSS-5.1252892.0.html)). O único controle da rede que muda isso de verdade é o `NRStyle` [FATO, `RELATORIO-2X-VISUAL.md` §2: "botão de maior efeito; Cinematic fica ~50% mais longe da fonte que Natural"]. O plugin do OBS expõe exatamente um seletor Default/Natural/Cinematic como controle principal ([obs-dlss5-nr](https://github.com/Saganaki22/obs-dlss5-nr)). No app o estilo só se troca pela Home ou pela chave `dlss2x_perfil`, e "vale na próxima abertura" [FATO, LEIA-ME linhas 103-107].
- **O que muda para quem usa.** Três palavras num chip; Natural para quem acha o rosto artificial, Cinema para vídeo.
- **Pré-condição que decide se entra.** Descobrir se a ponte consegue trocar `NRStyle` ao vivo (o RenoDX relê o ini ou aceita o valor pelo overlay?). Se só valer na próxima abertura, o chip vai para o seletor, não para o painel, com a frase "vale ao iniciar".
- **Esforço.** 2 a 3 dias com teste em janela, vídeo e câmera. **Risco.** Médio: deriva de cor no Cinema é medida; talvez só Padrão/Natural passem na regra "funciona sem ajuste".
- **Regras.** Formato já aprovado (três opções, F6 reverte). É a única decisão nova proposta neste relatório.

### 2.2 Controle de videogame: um acorde = F6, outro = painel

- **Problema real.** Público de sofá e portátil quer ligar e desligar sem teclado; no Lossless Scaling isso é feito com mapeamento de botão traseiro no ROG Ally e no Legion Go, e há relatos de controle "parar de funcionar" quando o app rouba o foco ([discussões do LS](https://steamcommunity.com/app/993090/discussions/0/7130962134691941565/), [ROG Ally](https://rogallylife.com/2025/01/29/how-to-use-lossless-scaling-on-rog-ally/)). O app não lê controle [FATO: nenhum XInput no `main.cpp`]. Como a camada é click-through, o jogo continua recebendo o controle normalmente, o que é uma vantagem.
- **O que muda para quem usa.** Segurar Back+Start (ou LB+RB+Y) faz o mesmo que F6. Sem foco, sem teclado.
- **Como.** `XInputGetState` no laço principal a cada 50 ms, só o acorde; linha "Botões do controle" na seção ATALHOS com "restaurar padrões". Painel navegável por controle fica de fora.
- **Esforço.** 1 a 2 dias. **Risco.** Baixo; polling barato. Não usar Steam Input.

### 2.3 "O DLSS 5 está mesmo mudando a imagem?" — medida de diferença na tela

- **Problema real.** "Instalei e não vi diferença" é a queixa mais frequente do gênero (DLSS5oneclick #10 "it does NOTHING", Swapper #148 "no effect", #143). O painel mostra o estado do neural, mas estado ATIVO não prova que a imagem mudou; o próprio teste do DLSS 2x do autor comparou a camada com ela mesma em bypass [FATO, `RELATORIO-2X-VISUAL.md` §1]. A comunidade mede diferença média entre entrada e saída (0,057 na primeira passada) [FATO, mesmo relatório].
- **O que muda para quem usa.** No card do DLSS 5 uma linha: `alterando a imagem: sim (forte)` / `sim (leve)` / `não`. Em vídeo parado ou com neural morto, diz "não".
- **Como.** Redução na GPU da diferença absoluta média entre a textura capturada e o backbuffer apresentado, a cada 1 s; três faixas fixas. O F6 vira também a prova de que a medida funciona.
- **Esforço.** 2 dias. **Risco.** Médio-baixo; custo de GPU pequeno (uma redução por segundo).
- **Regras.** Informação, não decisão.

### 2.4 Assinatura de código e hash publicado

- **Problema real.** Exe sem assinatura = SmartScreen na primeira abertura e chance de quarentena. Todas as ferramentas do gênero sofrem (Swapper #145, RHI #45, DLSS5-Autopilot explica "More info → Run anyway"). O pacote tem VERSIONINFO consistente [FATO, `app.rc`, `launcher.rc`], mas não há assinatura nem hash publicado [FATO: nenhum `signtool` nos scripts].
- **O que muda para quem usa.** Menos tela azul do SmartScreen; no LEIA-ME e no post, o SHA-256 dos dois exes para conferir.
- **Como.** Curto prazo (1 h): `empacotar.ps1` e `montar-portable.ps1` gravam `HASHES.txt` e o post do grupo copia. Médio prazo (2 a 3 dias de processo, tem custo): certificado OV e `signtool` no build. Reputação do SmartScreen também cresce com nome de exe e editor estáveis.
- **Risco.** Nenhum técnico. O custo é dinheiro e burocracia.

### 2.5 Lembrar ajustes por jogo, sem interface

- **Problema real.** Quem alterna emulador e jogo pesado refaz "Resolução do DLSS" e "Teto de fps" a cada troca; o ini guarda um conjunto só [FATO, `dlss5forall.ini` `[ultima]`]. Ferramentas irmãs têm perfis por jogo com telas inteiras para isso (Swapper), o que aqui seria poluição.
- **O que muda para quem usa.** Nada visível: ao escolher de novo a mesma janela (mesmo exe), os últimos ajustes daquele jogo voltam sozinhos.
- **Como.** Seção `[jogo:<exe>]` no ini com as quatro chaves; fallback para `[ultima]`.
- **Esforço.** 1 a 2 dias. **Risco.** Baixo.

### 2.6 Painel acompanhando troca de monitor com DPI diferente

- **Problema real.** `g_scale` do painel é calculado ao abrir [FATO, `main.cpp` ~4325] e não há `WM_DPICHANGED` [FATO, contagem 0]. Multi-monitor com escalas diferentes é queixa clássica de Magpie (#1401) e Lossless Scaling. Aqui o efeito provável é painel pequeno ou grande demais até reabrir [INFERÊNCIA].
- **Esforço.** 1 a 2 dias com teste em dois monitores. Prioridade baixa; entra quando alguém relatar.

---

## 3. Não faça (pedido pela comunidade, mas armadilha ou quebra a simplicidade)

| Pedido | Quem pede | Por que não |
|---|---|---|
| Scanner de biblioteca (Steam, Epic, GOG) e "centro de jogos" | 1-Click-DLSS5, Swapper, Autopilot | O app é captura, não injeção; não precisa saber onde o jogo está. Metade das issues dessas ferramentas é detecção de jogo (Swapper #140, #146; DLSS5oneclick #16). |
| Rota de injeção dentro do jogo (OptiScaler, bridge, REFramework) | Autopilot (8 rotas), Swapper | É exatamente o que o app evita: anti-cheat, crash em RE Engine, driver quebra 32 bits. A regra "nada entra no jogo" é o produto. |
| Três ou mais passadas ("DLSS 10", `layers=3..30`) | dlss5-visual-enhancer #7, DFC | Retorno decrescente medido, borrão em movimento, deriva de cor, fps de um terço [FATO, `RELATORIO-DLSS10.md` §5]. O DLSS 2x já existe; pare nele. |
| Sliders da rede no painel (Intensidade, Tom local, Estrutura, Pele) | obs-dlss5-nr expõe todos | Viola "nenhuma palavra técnica onde a pessoa decide" e "só efeito que funciona sem ajuste". Continuam atrás da Home. |
| "Esconder a camada do anti-cheat" / modo furtivo | fóruns | Isso é evasão de detecção. Além de eticamente errado, é o que transforma "por sua conta" em banimento. Aviso honesto (1.5) é o limite. |
| Telemetria, estatísticas de uso, atualização silenciosa | (ninguém pede; equipes querem) | O público já desconfia de "trojan" em cada release; qualquer conexão sem clique vira post negativo. Só rede por clique (1.9) ou opt-in explícito. |
| Geração de quadros dentro do app | Steam do LS, r/nvidia | O Magpie respondeu "não planejado" pela mesma razão; a cadeia com o Lossless Scaling já está documentada e medida. |
| Mais botões de latência (modo baixa latência, sem V-Sync, prioridade) | LS, Magpie | Medido: nenhum knob melhora o piso de 18-21 ms [FATO, README "Latência", `docs/latencia-nr.md`]. Só criaria decisão sem efeito. |
| Renomear a classe da janela para o OBS "Automático" funcionar, ou voltar ao swapchain bitblt | (para gravação) | Recusado no `RELATORIO-OBS.md` §4: mente sobre a janela e destrói a latência medida. |
| Resolução interna / supersampling | OptiScaler_DLSSNR | Construído, medido e retirado pelo autor [FATO, README, `docs/supersampling.md`]. |
| Desligar o HDR do monitor por padrão | LS (cursor cinza, cores lavadas) | A conversão automática já resolve; o plano B existe e é opt-in. |
| Painel inteiro navegável por controle | público de portátil | Grande, e a camada é click-through por projeto. O acorde (2.2) resolve 90% do caso. |
| "Reparar tudo em 1 clique" que mata processos e mexe em atributos de pasta | 1-Click-DLSS5 | Faz coisas no sistema sem explicar; no nosso modelo não há o que reparar fora da própria pasta. A faixa (1.2) e o relatório (1.3) são a versão honesta. |

---

## 4. Robustez (crash, atualização, arquivos, pastas, antivírus, primeira execução)

### 4.1 Minidump ao travar e aviso na abertura seguinte

- **Problema real.** O único filtro de exceção do processo é o `CrashHook` do `hdr_control.h`, que restaura o HDR e mais nada [FATO, `hdr_control.h:606`]. Não há `MiniDumpWriteDump` [FATO, grep]. Quando o app cai, o log para no meio e não há como saber onde; o Feeder pede dumps nas issues (#44).
- **O que fazer.** Filtro encadeado (chamar o anterior, para não quebrar a restauração do HDR) que grava `bin\crash-<data>.dmp` com `MiniDumpWithIndirectlyReferencedMemory` e uma linha `FATAL: exceção 0x… em módulo X` no log. Na abertura seguinte, se houver `.dmp` novo: cartaz `O app fechou sozinho da última vez. "Pedir ajuda" já inclui o arquivo.`
- **Esforço.** 0,5 a 1 dia. **Risco.** Baixo; `dbghelp.dll` vem com o Windows.

### 4.2 Guardar o log anterior

- **Problema real.** `dlss5forall.log` é aberto em `"w"` e truncado a cada abertura [FATO, `main.cpp` ~7387-7409]; o ReShade rotaciona o dele [FATO, comentário ~3786]. Quem teve o problema e reabriu para "ver se volta" apaga a evidência.
- **O que fazer.** Antes de abrir, renomear o anterior para `dlss5forall.log.1` (um nível basta). Mesmo para `dlss5-feed.log` se o Feeder não rotacionar.
- **Esforço.** 1 h. **Risco.** Nenhum.

### 4.3 Primeira execução de dentro do zip, do OneDrive ou de pasta com acento

- **Problema real.** Abrir o exe direto da prévia do zip do Explorer extrai só o exe para `%TEMP%` e nada funciona; é a falha clássica de portable [INFERÊNCIA a partir de DLSS5oneclick #23/#32 "não abre"; não há checagem de `%TEMP%` no launcher, FATO]. Pasta sem escrita já cai para `%LOCALAPPDATA%` [FATO, `main.cpp` ~7399-7416, ~8243]. Acento no caminho: o código é todo Unicode [FATO, `docs/revisao-critica.md` §5]; a recomendação do README existe pelo ReShade, não por nós, e nunca foi testada aqui.
- **O que fazer.** (a) Launcher: se o próprio caminho estiver sob `%TEMP%`, ou não houver `bin\` ao lado, cartaz `Extraia o zip inteiro para uma pasta (por exemplo C:\DLSS5ForAll) e abra de lá`. (b) Testar uma vez em `C:\Testeção ãé\` com a cadeia completa; se subir, tirar o aviso de acento do LEIA-ME; se não, o launcher avisa antes de abrir.
- **Esforço.** 2 h (a) + 1 h de teste (b). **Risco.** Nenhum.

### 4.4 Tirar as variáveis de teste do build de release

- **Problema real.** `DLSS5_TESTA_QUEDA` (derruba a captura de propósito) e `DLSS5_TESTA_POKE` continuam lidas do ambiente sem guarda de compilação [FATO, `main.cpp` ~8343, ~6792; `docs/polimento-produto.md` item 30, aberto]. Inofensivas por obscuras, mas aparecem no log do usuário e confundem suporte.
- **O que fazer.** `#ifdef DLSS5_DEV` em volta das três (QUEDA, POKE, AVISO) ou não logar quando não estão definidas.
- **Esforço.** 30 min. **Risco.** Nenhum.

### 4.5 Atualizar sem perder configuração (e sem perder os arquivos que a pessoa trouxe)

- **Problema real.** A atualização é "baixe o zip novo". Quem apaga a pasta velha perde `nvngx_dlssnr.dll`, `renodx-dlss5.addon64` (que não vêm no pacote) e os três ini [FATO, LEIA-ME "MOVER OU COPIAR"]. O `montar-portable.ps1` preserva configs, mas isso é do lado de quem monta, não de quem usa.
- **O que fazer.** Um `atualizar.cmd` na raiz do pacote novo: pede a pasta antiga (ou acha `..\DLSS5ForAll*` ao lado), copia os dois arquivos da NVIDIA/RenoDX e os ini para `bin\`, e abre o app. Seção "ATUALIZAR" no LEIA-ME com a lista de cinco arquivos para quem preferir fazer à mão.
- **Esforço.** 3 a 4 h. **Risco.** Baixo; só copia, nunca apaga.

---

## 5. Comunidade (ajudar o canal e o grupo sem virar propaganda)

### 5.1 Foto antes/depois pronta para postar (1.4) com nome do jogo no arquivo

- Cada foto sai como `DLSS5ForAll - <jogo> - antes.png` / `depois.png`. Pasta aberta na hora. Quem posta no grupo já vem com o nome do jogo e o nome do app na imagem do post, sem marca d'água e sem pedir nada. [INFERÊNCIA: é o que as ferramentas com par de screenshots geram de tráfego orgânico; o webcam-demo e o RenoDX funcionam assim.]

### 5.2 "Pedir ajuda: copiar relatório" (1.3) com uma linha final: "cole no grupo do Fafa"

- O texto do relatório termina com a URL do grupo. O suporte acontece dentro do grupo, o que é o objetivo, e a pessoa chega com tudo o que o suporte precisa. Uma linha, sem botão extra.

### 5.3 Vídeo de 2 minutos ligado ao "Como usar" e às novidades (1.8)

- Chave `video=` no `ofertas.ini` (mesmo padrão da `canal=`, que já condiciona o botão "APOIAR O CANAL" à existência do link [FATO, `ofertas.ini`]). Botão "Ver o vídeo (2 min)" no cartaz de "Como usar" e na linha de novidades. Quando não houver link, o botão não existe. É conteúdo do canal onde a pessoa mais precisa, sem repetir a tela do grupo.

---

## Apêndice A — O que já está bom e não precisa mexer (para não redescobrir)

- Faixa de arquivos faltando com botão para o RHI e para a pasta; recuperação automática de sessão WGC; detecção de monitor desconectado; conversão automática de HDR; reserva de tecla quando o Windows recusa; fallback para `%LOCALAPPDATA%`; segunda instância detectada por mutex com mensagem; restauração do HDR mesmo em `TerminateProcess`; DPI do seletor e do painel; `KeyOverlay=Home` nos setups; diálogo de atalhos com detecção de conflito nos dois sentidos [FATO, código e docs].

## Apêndice B — Fontes

- DLSS5-Feeder: [issues](https://github.com/jlrouzies-fr/DLSS5-Feeder/issues), [#47 NGX Init 0xBAD00001](https://github.com/jlrouzies-fr/DLSS5-Feeder/issues/47), [#54 driver novo quebra 32 bits](https://github.com/jlrouzies-fr/DLSS5-Feeder/issues/54), [#25 crash no Alt-Tab](https://github.com/jlrouzies-fr/DLSS5-Feeder/issues/25), [README](https://github.com/jlrouzies-fr/DLSS5-Feeder/blob/main/README.md)
- DLSS5oneclick: [issues](https://github.com/faisalkindi/DLSS5oneclick/issues) (#10, #16, #17, #23, #32, #39)
- DLSS5-Swapper: [issues](https://github.com/rakanki911/DLSS5-Swapper/issues) (#139, #140, #141, #143, #145, #146, #148); [Nexus](https://www.nexusmods.com/site/mods/2228?tab=posts)
- RHI: [issues](https://github.com/RankFTW/RHI/issues) (#44, #45, #46, #50, #52, #55)
- 1-Click-DLSS5: [README](https://github.com/reiluisii/1-Click-DLSS5); [Nexus](https://www.nexusmods.com/site/mods/2241?tab=posts)
- DLSS5-Autopilot: [README](https://github.com/Kizzuwatnaa/DLSS5-Autopilot) (anti-cheat, SHA-256, SmartScreen, quarentena)
- obs-dlss5-nr: [README](https://github.com/Saganaki22/obs-dlss5-nr) (seletor de estilo, teto de fps da rede, reset de histórico)
- dlss5-webcam-demo: [README](https://github.com/jpneagle/dlss5-webcam-demo) (wipe, lado a lado, F9)
- dlss5-visual-enhancer: [#7 multi-pass](https://github.com/Merserk/dlss5-visual-enhancer/issues/7)
- OptiScaler_DLSSNR: [releases](https://github.com/Dagherbou/OptiScaler_DLSSNR/releases)
- Riscos do mod: [Held Games](https://heldgames.com/guides/dlss-5-mod-risks), [Sportskeeda](https://tech.sportskeeda.com/gaming-news/modders-built-dlss-5-mod-override-tool-use-it), [Stellar Blade Nexus (falso positivo, VirusTotal)](https://www.nexusmods.com/stellarblade/mods/3706)
- Lossless Scaling: [controle para de funcionar](https://steamcommunity.com/app/993090/discussions/0/7130962134691941565/), [cursor cinza em HDR](https://steamcommunity.com/app/993090/discussions/0/598523169276430568/), [ROG Ally](https://rogallylife.com/2025/01/29/how-to-use-lossless-scaling-on-rog-ally/), [Legion Go](https://legiongolife.com/how-to-use-lossless-scaling-on-the-legion-go/)
- Magpie: [FAQ](https://github.com/Blinue/Magpie/wiki/FAQ-(EN)), [#1401 multi-monitor](https://github.com/Blinue/Magpie/issues/1401), [#1014 detectar DPI da janela](https://github.com/Blinue/Magpie/issues/1014)
- Queixa "rosto homogêneo": [Notebookcheck](https://www.notebookcheck.net/Nvidia-CEO-Jensen-Huang-says-gamers-completely-wrong-about-DLSS-5.1252892.0.html)
- Emuladores e DX9/32 bits com DLSS 5: [Wccftech](https://wccftech.com/dlss-5-now-works-on-rtx-20-30-gpus-older-graphics-apis-and-even-pcsx2/), [Held Games PCSX2](https://heldgames.com/guides/dlss-5-emulators-pcsx2)
- Projeto: `amd\README.md`, `amd\portable-launcher\LEIA-ME.txt`, `amd\docs\polimento-produto.md`, `amd\docs\revisao-critica.md`, `amd\docs\pesquisa-2026-09-05\RELATORIO-OBS.md`, `RELATORIO-DLSS10.md`, `RELATORIO-2X-VISUAL.md`, `PLANO-AMD.md`
