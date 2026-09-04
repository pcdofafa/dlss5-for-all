# DLSS5 For All

Uma janela Direct3D 11 mínima que mostra **uma fonte de vídeo qualquer** — placa de captura, webcam,
**uma janela de jogo** ou **um monitor inteiro** — e existe por um único motivo: ser o alvo mais simples e
previsível possível pra cadeia **ReShade → DLSS5-Feeder → LumeniteFX → renodx-dlss5**.
É o mesmo truque que o EDMIRE usou no Switch 2 (Feeder dentro do OBS + projetor), só que sem o OBS no meio:
um exe, um swapchain, um depth buffer real, uma resolução.

O DLSS 5 (Neural Rendering) recebe **um frame 2D + motion vectors** e "fotorrealiza" o que vê.
O Feeder fabrica o resto do contrato (depth + motion vectors por optical flow).
Como nada é injetado no processo do jogo, isso funciona com **qualquer** jogo que rode em janela ou borderless
— inclusive emulador, vídeo do YouTube, jogo antigo em DirectX 9, o que for.

## Fontes de vídeo

| Fonte | O que é | Como abre |
|---|---|---|
| **Monitor** | Windows Graphics Capture do monitor inteiro | seletor (é o padrão), ou `--source monitor --monitor N` |
| **Placa de captura / câmera** | Media Foundation (webcam, ezcap, DroidCam…) | seletor, ou `--device N --mode M` |
| **Janela** | Windows Graphics Capture da janela escolhida — **mais leve** | seletor, ou `--source window --window <parte do título ou do exe>` |

**Monitor é o padrão, mas a Janela deixa o filtro mais leve** — e essa é a razão de ela existir no seletor. A rede
processa **área**: um jogo em 2560x1080 são 2,8 MP por quadro contra 5,0 MP de uma tela 3440x1440, quase metade
da conta. No modo monitor não há como fugir disso, porque a captura é a tela toda mesmo que o jogo ocupe um
pedaço dela. Em compensação, a Janela pede que você escolha o alvo numa lista, e a camada some quando ele perde
o foco.

E as fontes de tela (janela e monitor) têm **dois modos de operação**, escolhidos na caixa
*"SOBREPOR o alvo"* do seletor (ou `--overlay`):

- **Janela separada** (caixa desmarcada): o alvo aparece dentro da janela do DLSS5 For All, com o filtro
  aplicado. Bom pra gravar, comparar lado a lado e mexer no ReShade à vontade.
- **Sobreposto** (caixa marcada / `--overlay`): a janela vira um popup sem borda **colado exatamente em cima
  do alvo**, sempre no topo e **transparente ao mouse** — quem recebe clique e teclado continua sendo o que
  está embaixo. Parece que o DLSS 5 está rodando dentro do jogo.
  - sobre uma **janela**: segue posição e tamanho, e some sozinho quando o jogo perde o foco;
  - sobre um **monitor**: cobre a tela inteira, e como a nossa janela é excluída da captura
    (`WDA_EXCLUDEFROMCAPTURE`), não há espelho infinito — **tudo o que estiver naquela tela passa pelo filtro**.
    Se nada mudar na tela por vários segundos a imagem pode parar (a camada acaba tapando a própria fonte);
    o app detecta, recria a captura e, se ainda assim não voltar, devolve a imagem original sozinho.

> **Duas coisas fazem o modo sobreposto ser usável, e as duas foram descobertas medindo.**
>
> **1. A camada é `WS_EX_LAYERED`, e sem isso o clique não atravessa.** Só `WS_EX_TRANSPARENT` + responder
> `HTTRANSPARENT` no `WM_NCHITTEST` **não basta**, e a documentação diz por quê: `HTTRANSPARENT` manda o
> clique para as janelas de baixo *"in the same thread"* — ou seja, só para janelas do próprio app. Para o
> Explorer, o desktop ou o jogo, o clique morre na camada. Quem atravessa processos é a dupla
> `LAYERED + TRANSPARENT`. Medido aqui, mesma sessão e mesmo ponto, só essa variável mudando: sem layered,
> **bloqueado**; com layered, **passou**. `DLSS5_SEM_LAYERED=1` volta ao comportamento antigo, caso o layered
> brigue com o flip model em alguma máquina.
>
> **2. O layered também resolve um segundo problema, e por isso a janela NÃO precisa de uma thread só dela.**
> O `WM_NCHITTEST` é **síncrono**: quando o Windows precisa perguntar "o clique é seu?", ele espera a resposta
> da thread dona da janela antes de entregar o clique a quem quer que seja. Com a camada cobrindo a tela e o
> render na mesma thread, cada `Present` caro do DLSS segurava o mouse do sistema inteiro — medido: **1.864 ms**
> de espera, com o log marcando `present travou 841 ms` no mesmo instante. A janela chegou a ser movida para
> uma thread própria por isso (0,4 ms no pior caso), mas com `WS_EX_LAYERED` o sistema **descarta a camada no
> hit-test sem perguntar nada**, e a espera some pela raiz. A thread extra foi desfeita porque cobrava caro em
> outro lugar: o ReShade vive dentro deste processo e o `PokeReShade` (o F6) posta a tecla na janela contando
> com o `Present` da mesma thread — com a janela em outra, o F6 passou a demorar. Nada é interceptado nem
> reencaminhado: a camada só não é consultada, e o clique segue o caminho normal do sistema.

No modo sobreposto o app **não aparece na barra de tarefas nem no Alt+Tab**: ele fica na **bandeja do sistema**,
e o menu do ícone traz os comandos e os atalhos.

### Atalhos (só dois)

| Tecla | O quê |
|---|---|
| **End** | abre e fecha o painel — tudo o mais (filtro, neural, fps, HDR, ReShade, sair) é feito nele, com o mouse |
| **F6** | liga/desliga **tudo o que o painel configurou** (neural + a camada, no modo sobreposto) de uma vez — o antes/depois — e volta exatamente como estava; aviso na tela do estado real |
| **Alt+Esc** | sai do app (duas vezes em 2 s, para não fechar sem querer) |

Todas as teclas são configuráveis — no botão **Atalhos…** do seletor (antes de escolher a fonte) e na seção
ATALHOS do painel: painel, neural, filtro, teclado pro ReShade e sair, cada uma com a tecla que você quiser
(com detecção de conflito e "restaurar padrões"). Por padrão End, F6 e Alt+Esc têm tecla.

O botão **Atalhos…** do seletor também configura a tecla que **abre o overlay do ReShade** (padrão **Home**).
Essa é a única da lista que não é do app: ela vai para `[INPUT] KeyOverlay` do `ReShade.ini` — o app faz um
backup (`ReShade.ini.bak-keyoverlay-…`) antes da primeira gravação e recusa uma tecla já usada por um atalho
daqui, nos dois sentidos. Escolhida no seletor, vale já nesta abertura: o ReShade lê o `ReShade.ini` quando o
primeiro swapchain sobe, e isso acontece depois que você fecha o seletor.
Ao iniciar, um cartaz com as teclas fica na tela por 30 s (some ao apertar End; "não mostrar de novo" disponível,
e o painel tem "Como usar" para rever). Se outro programa já tiver a tecla,
o app pega a reserva e mostra qual valeu no cartaz inicial. **Esc nunca fecha o app** — só o painel ou a tela
que estiver aberta; sair é por Alt+Esc, pelo botão do painel, pelo X (modo janela separada) ou pela bandeja.

## Painel na tela (End)

A tecla **End** abre um painel por cima de tudo (fora da captura, então não entra no vídeo nem no filtro):

- **DLSS 5**: um card só, HABILITADO/DESABILITADO (F6), que liga e desliga a camada **e** o neural juntos. O estado do
  Neural Rendering aparece nele como informação — é como se percebe que a cadeia não subiu
- **Teto de fps do filtro** Máx/60/45/30/20 — devolve GPU ao jogo, não deixa o filtro mais barato
- **Resolução do DLSS** Auto/100%/77%/67%/58% — esta sim mexe no **custo da rede**. `Auto` escolhe pela área da fonte
  (≤2,4 MP: 100 · ≤4,0: 77 · ≤6,0: 67 · acima: 58). Vale ao reabrir: quem lê o `dlss5-feed.cfg` é o add-on do feed,
  uma vez só, quando o ReShade sobe
- **V-Sync** (padrão desligado — não rasga a imagem, só não espera o vblank)
- **Desligar o HDR do monitor (plano B)** (padrão desligado; o app detecta e avisa mesmo desmarcado)
- **Mostrar FPS na tela** (padrão ligado). O número no canto é o contador do **overlay do ReShade**, não do
  app — a caixa está também no seletor, e escrever nela mexe em `[OVERLAY] ShowFPS` do `ReShade.ini`. Se o
  contador não sumir na hora, ele some na próxima abertura: quem o desenha é o ReShade.
- **Idioma / Language** Português / English — vale na hora, sem reiniciar o app
- **Abrir o ReShade** (Home por padrão; o badge mostra a tecla que está no `ReShade.ini`) e o acesso ao
  **Grupo de ofertas — PC do Fafa**

## Idioma

A interface toda existe em **português e inglês**, e o app decide sozinho na primeira execução pelo idioma
do Windows (`GetUserDefaultUILanguage`): Windows em português abre em português, qualquer outro abre em
inglês. A partir do momento em que a pessoa escolhe, a escolha dela manda — fica em `[idioma] lingua=pt|en`
do `dlss5forall.ini`.

Troca-se em dois lugares, e nos dois **vale na hora**, sem reiniciar: na primeira linha do **seletor de
fonte** (a primeira tela, ao lado de *Fonte:*), num par `Português | English` que fica sempre à vista — sem
menu para abrir e sem clique intermediário —, e na seção **IDIOMA / LANGUAGE** do painel. No seletor a janela
se refaz inteira no idioma novo; no painel o cartaz, a barra de título e o menu da bandeja acompanham na
mesma hora.

O que **não** muda de idioma, de propósito: as mensagens do `dlss5forall.log` (são ferramenta de
diagnóstico e há documentação que depende do texto exato), as chaves do `.ini` e os nomes de técnica, shader
e arquivo do ReShade. A única exceção no log é a linha `FATAL:`, que é a cópia do que apareceu na caixa de
erro. Os textos do convite (`ofertas.ini`) têm as chaves de sempre para o português e as mesmas com sufixo
`_en` para o inglês; sem a chave do idioma em uso, vale o texto embutido, que já vem traduzido.

**Cor (LUT) e nitidez saíram do painel.** Eram chips que escreviam nos shaders `DLSS5_Grade.fx` e
`DLSS5_Sharpen.fx` pelo add-on `dlss5-bridge.addon64`. Os shaders, a textura das LUTs e os
`preset-02..05-*.ini` continuam no pacote: quem quiser mexer liga a técnica pelo overlay do ReShade
(a tecla do F7) e ajusta lá mesmo.

### O que roda de verdade na cadeia

O preset `FafaDLSS5.ini` carrega **três técnicas**, e elas não são intercambiáveis:

| Técnica | Papel | Dá pra tirar? |
|---|---|---|
| `Lumenite_Kernel` | fabrica o *depth* | **não** — sem ele o neural não sobe |
| `DLSS5_Feed` | fabrica os *motion vectors* e entrega o par ao DLSS | **não** — é o contrato do Neural Rendering |
| `DLSS5_Sharpen` | pós-nitidez (`Sharpness=0.35`) | **sim** — é gosto, não requisito |

O Neural Rendering em si não é técnica do preset: quem o roda é o add-on `renodx-dlss5.addon64`.
Ele é também **a parte cara** — é por isso que o painel tem um botão só para ele, separado do filtro:
a diferença de fps ao desligá-lo é a medida do que ele custa nesta máquina. O custo do resto está no
`dlss5-feed.log` de cada sessão, nesta forma: `600 frames: feed CPU 3.36 ms/frame … feed is 6% of the
frame`.

O **Neural Rendering** é o primeiro item do painel e o motivo do app existir: o estado real (ativo / iniciando /
desligado) aparece em destaque, e o app avisa alto se ele não subir.

**Ficou pesado?** O filtro custa GPU (o Neural Rendering é a parte cara). `F6` desliga na hora (e religa tudo do jeito que estava) e o jogo
continua rodando normal por baixo — ele nunca parou; o que você via era a nossa entrega. Baixar o teto de fps
no painel devolve GPU pro jogo. O app avisa sozinho, uma vez, quando percebe que está apresentando muito
abaixo do que captura.

**HDR**: quando o monitor do alvo está em HDR, o app captura em float16 e converte para SDR no próprio shader,
**automaticamente** — funciona com o jogo já aberto, sem mexer no display. Conteúdo HDR de verdade (jogo com HDR
ligado) deixou de sair estourado em branco. A caixa *Desligar o HDR do monitor enquanto captura (plano B)* é
opcional: desliga e restaura ao sair (inclusive se o app for morto), mas jogo já aberto costuma ler o HDR só na
inicialização. Na placa de captura a caixa confirma a conversão PQ/BT.2020. Medições em `docs/hdr.md`.

**Se o jogo mudar de tamanho** (janela redimensionada, troca de resolução): o filtro continua — o backbuffer fica no
tamanho em que nasceu (mexer nele com a cadeia NGX viva derruba o Feeder), a imagem nova é encaixada nele, a captura
é recriada se o Windows a encerrar, e o painel oferece "Reabrir o app na resolução nova".

**Resolução interna** — foi construída e medida (`docs/supersampling.md`): o neural **não** melhora rodando em backbuffer
maior; só texto/HUD ganha um pouco, ao custo de fps do jogo. Retirada do app por decisão do autor.

**Presets** (troque no overlay do ReShade): `preset-01` núcleo com `IMAGE_SPACE=1` · `02` grade neutra ·
`03` grade cinema · `04` brilho contido · `05` nitidez. Todos deixam `FafaDLSS5.ini` intacto. Do `02` ao `05`
são material para o overlay do ReShade — desde que cor e nitidez saíram do painel, nada no app os seleciona.

**Tela do grupo**: abre na hora em que o app é aberto (antes do seletor), a cada execução, com QR e botão. Não
fecha sozinha — a pessoa fecha em `Agora não`, no X ou com Esc — sem contagem regressiva, como o aviso do WinRAR. Tudo configurável em
`ofertas.ini` (link, textos, `mostrar=sempre|primeira_vez|0`).

## Requisitos

- Windows 10 1903+ / 11 64-bit (a captura de janela usa Windows Graphics Capture; câmera precisa do Media Feature Pack no Windows N)
- GeForce RTX com driver recente (a 4080 roda; em 1080p espera cair pra ~metade do FPS com o NR ligado)
- Visual C++ Runtime 2015-2022 x64 (o script instala se faltar)

## Instalação (uma vez)

1. Descompacta tudo numa pasta **sem acento e fora de Arquivos de Programas** (ex.: `C:\DLSS5ForAll\`).
2. Botão direito em `setup-dlss5.ps1` → **Executar com o PowerShell**
   (ou `powershell -ExecutionPolicy Bypass -File .\setup-dlss5.ps1`).
   Ele monta a cadeia inteira ao lado do exe:
   - ReShade (build com add-ons) instalado no `DLSS5ForAll.exe` → `dxgi.dll`
   - `ReShade.fxh`, `ReShadeUI.fxh`, `DrawText.fxh`
   - `dlss5-feed.addon64` + `DLSS5_Feed.fx` (repo legítimo: `jlrouzies-fr/DLSS5-Feeder`)
   - LumeniteFX (`lumenite_*.fx`, `include\`, blue noise)
   - `nvngx_dlss.dll` copiado do driver NVIDIA ou de um jogo
   - `ReShade.ini` + preset `FafaDLSS5.ini` já com `DLSS5_MV_PROVIDER=3` e as técnicas na ordem certa
3. **Você** coloca `renodx-dlss5.addon64` + `nvngx_dlssnr.dll` ao lado do exe (instalador RHI do RankFTW ou canal #DLSS5 do Discord do RenoDX).
   O script para e espera por eles. São o código vazado da NVIDIA — o script não baixa isso por você.
   **Se abrir o app sem eles**, o seletor mostra uma faixa dizendo o que falta, com um botão que abre as
   releases do RHI e outro que abre a pasta onde os arquivos vão. Instalou com o seletor aberto? Clique de
   volta na janela e a faixa some sozinha. O app continua abrindo e mostrando a imagem sem eles — o que não
   acontece é o realce do DLSS 5, e era exatamente essa a confusão: a imagem sai igual à original e parece
   que o programa não funciona.

## Uso

1. Abre `DLSS5ForAll.exe`. No seletor escolhe a **fonte** (câmera / janela / monitor), o alvo e as opções. **INICIAR**.
   A última escolha fica salva em `dlss5forall.ini`.
2. Na janela: **Home** abre o ReShade (no modo sobreposto, antes abre o painel com **End** e usa o botão de mouse/teclado, ou `Ctrl+Alt+I`).
   - Aba *Home*: `LUMENITE: Kernel 2.0` ligado **acima** de `DLSS 5 Feed` ligado (o preset já faz isso).
   - Aba *Add-ons* → `DLSS 5 Feed`: tem que mostrar `feature ready … DLAA` e `frame N delivered`.
   - Painel do add-on neural (`DLSS 5 Neural Rendering`): liga o NR. **F6** alterna liga/desliga — é o antes/depois.
3. Teclas do app (modo janela normal): **F11** tela cheia sem borda · Alt+Enter também alterna tela cheia · sair: **Alt+Esc** duas vezes, X da janela, bandeja ou painel (Esc nunca fecha o app).

Linha de comando:

```
DLSS5ForAll.exe --device 0 --mode 3 --fullscreen        # câmera / placa
DLSS5ForAll.exe --source window --window cyberpunk      # espelho de uma janela
DLSS5ForAll.exe --window cyberpunk --overlay            # colado por cima do jogo
DLSS5ForAll.exe --source monitor --monitor 1            # monitor numa janela
DLSS5ForAll.exe --source monitor --monitor 1 --overlay  # filtro por cima da tela inteira
DLSS5ForAll.exe --list-windows                          # lista janelas e monitores no log
DLSS5ForAll.exe --list                                  # lista câmeras e modos no log
```

Flags extras: `--full-window` (não recorta a barra de título), `--cursor` (desenha o mouse),
`--no-clickthrough` (overlay recebe o mouse), `--hide-self` / `--no-hide-self`
(tira a nossa janela da captura — ligado por padrão na fonte monitor), `--capture-border` (borda amarela do Windows).

## Frame generation com o Lossless Scaling

O DLSS 5 aqui faz Neural Rendering, **não** gera quadros. Quem pode dobrar o fps é o Lossless Scaling (LS),
rodando **por cima da nossa janela** — ele captura o `DLSS5ForAll.exe` já filtrado e interpola em cima disso.
Não precisa de nenhuma alteração no app; precisa da configuração certa.

Ordem da cadeia:

```
jogo (janela / borderless)
  -> nossa captura WGC da janela do jogo
  -> ReShade: Lumenite Kernel -> DLSS5_Feed -> RenoDX Neural Rendering
  -> Present do DLSS5ForAll.exe
  -> Lossless Scaling captura a NOSSA janela e interpola
```

As duas capturas não brigam: nós pegamos a janela do jogo, o LS pega a nossa. Não há espelho.

**Como usar**

1. Abre o DLSS5 For All com fonte **Janela**, apontando pro jogo, e **SOBREPOR desmarcado** (modo janela separada).
   Overlay e fonte monitor **não servem** aqui — veja as armadilhas abaixo.
2. Deixa a **nossa** janela em foco e aperta o atalho de *Scale* do LS. O alvo dele é o `DLSS5ForAll.exe`;
   se apontar pro jogo, ele interpola a imagem **sem** o filtro.
3. Desliga outras overlays (GeForce Experience, Discord, etc.) — qualquer coisa por cima da janela do LS quebra o FG.
4. Não usa limitador de fps externo. O limite, se houver, é no jogo.

**Armadilhas (as duas são do nosso lado)**

| Situação | Por que quebra |
|---|---|
| Fonte **monitor** (qualquer modo) | a nossa janela fica com `WDA_EXCLUDEFROMCAPTURE` e o LS enxerga tela preta |
| Modo **sobreposto** | a nossa camada reafirma o topo a cada 1 s e briga com a janela de saída do LS |
| Apontar o LS pro **jogo** | ele interpola o frame cru, sem passar pela cadeia DLSS 5 |

**O preço:** o LSFG em 2x custa cerca de **1,5 a 2,5 frames** a mais, somados aos 2-3 frames que a nossa cadeia
já tem. A 60 fps de entrada, algo entre +25 e +42 ms. Serve pra jogo lento e pra vídeo; não serve pra competitivo.

O LS é software proprietário que você comprou na Steam — nada dele é redistribuído aqui, só a receita de uso.

## Gravando o resultado

O ReShade aplica tudo **no Present** da nossa janela, então quem grava tem que capturar a janela já apresentada:

- **OBS → Captura de Janela** (método Windows 10 / WGC) apontando pro `DLSS5ForAll.exe`. Não usa *Captura de Jogo*.
- **NVIDIA App / ShadowPlay** também funciona.
- Se a fonte for **monitor** com "esconder esta janela da captura" ligado, o OBS **não vê** a nossa janela —
  use dois monitores (jogo num, DLSS5 For All no outro) ou capture por janela.

Latencia: medida nesta maquina, do frame capturado ao pixel na tela, **18-21 ms com a GPU livre** (era ~121 ms
antes do swapchain waitable com `SetMaximumFrameLatency(1)`). O Neural Rendering acrescenta +3 a 4,5 ms a 1080p e
~10 ms a 3440x1440 — e o custo dele, irredutivel sem perder imagem. Nenhum knob (V-Sync, teto de fps, sliders,
resolucao de trabalho, topmost, tela cheia) mede melhor que o padrao atual; medicoes em `docs/latencia-nr.md`. Da pra jogar coisa lenta e assistir video; pra
competitivo, nao. Medicoes e metodologia em `docs/latencia.md`.

**Se a captura estiver presa em 50 fps:** monitor virtual instalado (Meta Virtual Monitor, Parsec Virtual
Display, Virtual Desktop) faz o WGC entregar num ritmo fixo de 20 ms, independente do jogo. Custa ~10 ms de
latencia e metade da fluidez. Desativa os adaptadores virtuais no Gerenciador de Dispositivos (ou pelo menos
poe o monitor real como principal) e confere a linha `LAT ritmo do WGC` no `dlss5forall.log`.

## Limitações honestas

- **Fullscreen exclusivo não funciona.** Põe o jogo em janela ou borderless — é o modo que o WGC e a overlay enxergam.
- Nada é injetado no processo do jogo (nem hook, nem DLL): a cadeia roda toda dentro do `DLSS5ForAll.exe`.
  Mesmo assim, anti-cheats agressivos podem não gostar de overlay topmost — em jogo online, use por sua conta.
- Motion vectors são **estimados por optical flow**, não vêm da engine. Movimento rápido e HUD podem fantasmar.
- Resolução do backbuffer = resolução do recorte. 1080p é o ponto ideal na 4080; 4K quadruplica a carga.

## Problemas comuns

| Sintoma | Causa provável | Faz o quê |
|---|---|---|
| Overlay do ReShade não aparece com Home | a tecla foi trocada, ou o `dxgi.dll` não carregou | Confere a tecla em **Atalhos…** (linha "Abrir o ReShade"), ou `[INPUT] KeyOverlay` no `ReShade.ini`. Se estiver certa: renomeia `dxgi.dll` → `d3d11.dll` e confere o `ReShade.log`. |
| No modo sobreposto o teclado não faz nada | é click-through de propósito | painel (**End**) → botão de mouse/teclado, ou `Ctrl+Alt+I`; o mesmo de novo pra voltar a jogar |
| A overlay some quando clico fora | comportamento normal | ela só aparece com o jogo em primeiro plano |
| A overlay fica atrás do jogo | jogo topmost brigando | o app reafirma o topo a cada 1 s; se persistir, roda o jogo em janela normal |
| Imagem nítida parada mas borra ao mexer | motion vectors não chegando | Overlay → seção *Motion vectors* do Feeder. Kernel **acima** do Feed e `DLSS5_MV_PROVIDER=3`. |
| A imagem sai igual à original, "não faz nada" | faltam `renodx-dlss5.addon64` e/ou `nvngx_dlssnr.dll` | O seletor avisa numa faixa e leva às releases do RHI. Sem eles o app é só espelho: o neural não sobe. |
| Painel DLSS 5 em STANDBY | add-on perdeu o primeiro create | O Feeder recria sozinho depois de ~180 frames. Espera. |
| "0.0 fps (alvo parado)" | a janela capturada não redesenhou nada | normal em janela estática; num jogo o número sobe |
| Fatal: "dispositivo em uso" / nenhum modo (câmera) | outro app segurando a captura | Fecha OBS/Discord/navegador/app Câmera, clica em *Atualizar lista*. |
| Nada abre, sem janela | olha `dlss5forall.log` | Ele diz onde parou (MFStartup, D3D11, shaders, WGC…). |

## Sobre os arquivos

- `DLSS5ForAll.exe` — o app (C++ puro, Win32 + Media Foundation + D3D11 + WinRT/WGC, sem dependências além do Windows).
- `main.cpp` — janela, renderer, seletor, captura de câmera. `screen_capture.h` — módulo de captura de janela/monitor.
- `idioma.h` — os dois idiomas da interface: `T(L"português", L"english")` no ponto de uso, detecção e persistência.
- `build-mingw.sh` — recompila com o MinGW-w64 do msys2 (`./build-mingw.sh`).
- `setup-dlss5.ps1` — monta a cadeia.
- `dlss5forall.log` / `dlss5-feed.log` / `ReShade.log` — os três logs que importam quando algo dá errado.

**O que precisa ir no zip** (além do que o `setup-dlss5.ps1` baixa): `DLSS5ForAll.exe`, `dlss5-bridge.addon64` (ponte do
painel — é por ela que o F6 alterna o neural sem teclado), `reshade-shaders\Shaders\DLSS5_Grade.fx` e `DLSS5_Sharpen.fx`
(opcionais, para quem quiser grading pelo overlay do ReShade),
`reshade-shaders\Textures\dlss5_grade_luts.png`, os `preset-0*.ini`, `ofertas.ini`, `README.md` e `setup-dlss5.ps1`.
O `ReShade.ini` e o `FafaDLSS5.ini` o script gera; `dlss5forall.ini` nasce na primeira execução. Não distribua `nvngx_dlssnr.dll`.

O app é MIT e não contém nenhum código da NVIDIA. O que ele hospeda (ReShade, Feeder, LumeniteFX, RenoDX, o DLL vazado)
tem cada um sua licença e seu risco — o DLSS 5 nesse estado é um build interno que caiu do NBA 2K27, e a Take-Two já
derrubou coisa por copyright. Faz o vídeo, deixa claro que é experimento com build vazado, e não redistribui o `nvngx_dlssnr.dll`.

## Compilar do código-fonte

O app é C++17 puro sobre a API do Win32 — sem SDK externo, sem gerenciador de pacotes, sem CMake.

**Precisa de:** Windows 10/11 x64, **MinGW-w64 pelo [msys2](https://www.msys2.org/)**
(`pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils`) e um **bash** (o do msys2, o do Git for Windows ou o WSL).
O `build-mingw.sh` procura o `g++` em `C:\msys64\mingw64\bin` e põe essa pasta no `PATH` antes de chamá-lo — se o seu
msys2 estiver noutro caminho, exporta `CXX` apontando pro teu `g++`.

```bash
./build-mingw.sh                 # DLSS5ForAll.exe + o add-on da ponte (dlss5-bridge.addon64)
./build-mingw.sh MeuTeste.exe    # outro nome de saída
SKIP_ADDON=1 ./build-mingw.sh    # só o exe, pra iterar rápido
```

O script compila também o `app.rc` com o `windres` (ícone + VERSIONINFO) e, no fim, chama o `addon/build-addon.sh`,
que usa os headers do ReShade em `addon/include/reshade/`. Compila com `-Wall -Wextra`: aviso novo é defeito, não ruído.

Os binários gerados **não** são versionados. Pra montar a cadeia inteira numa pasta de teste, roda o `setup-dlss5.ps1`,
que baixa ReShade, Feeder, LumeniteFX e RenoDX de cada fonte oficial.

## Contribuindo

PRs são bem-vindos — o passo a passo (fork → branch → PR, revisão do dono antes de entrar) está em
[`CONTRIBUTING.md`](CONTRIBUTING.md). **Regra que não tem exceção: PR nenhum pode trazer `nvngx_dlssnr.dll`,
`nvngx_dlss.dll`, `renodx-*.addon64` ou `dxgi.dll`.** Este repositório distribui a receita, nunca os ingredientes
proprietários de terceiros.
