// =====================================================================================
//  mensagens.h - O CATALOGO: os CODIGOS DE ERRO (X-CODIGOS-ERRO, 18/09/2026) e as mensagens
//                que chegam de FORA do app (I8, 18/09/2026)
//
//  A REGRA DA CASA (idioma.h): o texto que a pessoa le fica no ponto de uso, T(pt, en),
//  legivel e revisavel onde e usado. Este catalogo NAO contradiz isso - ele existe para dois
//  casos que a regra nao cobre:
//   1. TODA FALHA, QUEDA OU LIMITACAO tem um CODIGO (D5-NAT-104) que aparece pequeno no canto
//      do cartaz e na mesma linha do log, para o jogador ditar e o dono achar. O codigo e o
//      contrato; o texto e a camada localizada (RFC 9457: `type` fixo, `title` traduzivel).
//   2. Mensagens que NASCEM em outro binario (o feeder, a DLL da ponte) e chegavam CRUAS ao
//      cartaz, em ingles tecnico ("the Vulkan interop extensions are missing on this device
//      -- see dlss5-feed.log"). O X-CODIGOS-ERRO chamou isso de "o maior descasamento do
//      projeto entre linguagem de log e linguagem de jogador" (X-CE-3).
//
//  O FORMATO DO CODIGO: `D5-NAT-104` = produto fixo + AREA (interpretavel sozinha) + numero de
//  3 digitos cuja CENTENA e a classe:
//     0xx ambiente (a maquina, o Windows, o driver, a permissao - o jogador pode agir)
//     1xx limitacao (recusa prevista e correta do nosso desenho - ninguem conserta)
//     2xx falha nossa (algo do DLSS5 For All nao funcionou - nos, pelo relato)
//     9xx desconhecido (nao casou com assinatura conhecida - nos, com prioridade)
//     3xx-8xx reservadas para crescimento.
//  A severidade e campo separado (info/aviso/parada/queda), nunca vai no numero.
//
//  AS REGRAS DE ESTABILIDADE (Chromium net_error_list.h, MS-ERREF, Rust E0xxx):
//   1. Um codigo por causa detectavel. Duas situacoes que pedem acoes diferentes do jogador
//      sao dois codigos, mesmo com texto parecido.
//   2. O NUMERO NUNCA MUDA E NUNCA SE RECICLA. Codigo aposentado vira LAPIDE (lista no fim
//      deste bloco) com o significado antigo preservado; o numero morre com ele.
//   3. Codigo novo = sempre o PROXIMO LIVRE da faixa. Buraco fica buraco.
//   4. O texto pode ser reescrito a vontade; so o numero e contrato.
//   5. `9xx` e gatilho de trabalho, nao destino: todo `D5-<area>-9xx` num relato vira item
//      novo no PLANO-DE-CORRECOES.md. `D5-APP-999` e o ultimo recurso (nem a area e conhecida).
//
//  FONTE UNICA (R6 do relatorio): cada codigo existe UMA vez, na X-macro D5_CATALOGO, que gera
//  o enum, a string do codigo, a area, a severidade, a acao sugerida e os textos PT/EN. Nada
//  de tabela paralela. Os mapas de assinatura (sinal do vigia, string do feeder, string do
//  Upstream) apontam para o enum, e e assim que o programa PRE-IDENTIFICA a falha sem adivinhar.
//
//  Cada texto segue a regua do I8: primeira linha o que aconteceu, segunda so quando ha uma
//  acao possivel; nome dos recursos como o jogador ve no menu do jogo; zero termo tecnico
//  (fence, device, NGX, swapchain ficam no log); sem culpar o jogador; dizer que a imagem
//  continua quando ela continua.
// =====================================================================================
#pragma once

namespace Msg {

enum Area  { AREA_NAT = 0, AREA_UNI, AREA_UPS, AREA_FG, AREA_NEU, AREA_GPU, AREA_HDR, AREA_IO, AREA_APP, AREA_NET, AREA_N };
enum Sev   { SEV_INFO = 0, SEV_AVISO, SEV_PARADA, SEV_QUEDA };

// ------------------------------------------------------------------------------ A TABELA
//  X( nome, "codigo", AREA, SEV, "acao sugerida", L"pt linha 1", L"pt linha 2", L"en line 1", L"en line 2" )
//  Linha 2 vazia (L"") = so a primeira linha vai para a tela.
#define D5_CATALOGO(X) \
/* ---- NAT: modo Nativo (injecao, ponte, DLL no jogo, vigia) ---- */ \
X(NAT_001, "D5-NAT-001", AREA_NAT, SEV_AVISO,  "reabrir elevado", \
  L"Este jogo roda como administrador.", L"Abra o DLSS 5 como administrador para usá-lo aqui.", \
  L"This game runs as administrator.", L"Open DLSS 5 as administrator to use it here.") \
X(NAT_002, "D5-NAT-002", AREA_NAT, SEV_AVISO,  "liberar no antivirus", \
  L"O DLSS 5 não conseguiu entrar neste jogo.", L"O antivírus pode estar bloqueando. Seguindo no Universal.", \
  L"DLSS 5 could not attach to this game.", L"Your antivirus may be blocking it. Staying on Universal.") \
X(NAT_101, "D5-NAT-101", AREA_NAT, SEV_INFO,   "nenhuma", \
  L"O modo anti-cheat está ligado.", L"Só o Universal funciona com ele.", \
  L"Anti-cheat mode is on.", L"Only Universal works with it.") \
X(NAT_102, "D5-NAT-102", AREA_NAT, SEV_INFO,   "nenhuma", \
  L"Este jogo tem proteção anti-cheat.", L"Seguindo no Universal, que é seguro aqui.", \
  L"This game has anti-cheat protection.", L"Staying on Universal, which is safe here.") \
X(NAT_103, "D5-NAT-103", AREA_NAT, SEV_INFO,   "nenhuma", \
  L"O modo Nativo não funciona neste jogo.", L"Seguindo no Universal — o seu jogo não foi afetado.", \
  L"Native mode does not work in this game.", L"Staying on Universal — your game is unaffected.") \
X(NAT_104, "D5-NAT-104", AREA_NAT, SEV_INFO,   "nenhuma", \
  L"Este jogo usa uma tecnologia gráfica que o modo Nativo ainda não suporta.", L"Seguindo no Universal.", \
  L"This game uses a graphics technology Native mode does not support yet.", L"Staying on Universal.") \
X(NAT_105, "D5-NAT-105", AREA_NAT, SEV_INFO,   "escolher um jogo", \
  L"O modo Nativo precisa de um jogo aberto como fonte.", L"Escolha a janela do jogo na tela de escolha.", \
  L"Native mode needs an open game as the source.", L"Pick the game window on the selection screen.") \
X(NAT_201, "D5-NAT-201", AREA_NAT, SEV_PARADA, "F8", \
  L"O modo Nativo não abriu neste jogo.", L"Seguindo no Universal. F8 tenta de novo.", \
  L"Native mode did not start in this game.", L"Staying on Universal. Press F8 to retry.") \
X(NAT_202, "D5-NAT-202", AREA_NAT, SEV_PARADA, "F8", \
  L"O jogo não respondeu ao modo Nativo.", L"Seguindo no Universal. F8 tenta de novo.", \
  L"The game did not respond to Native mode.", L"Staying on Universal. Press F8 to retry.") \
X(NAT_203, "D5-NAT-203", AREA_NAT, SEV_PARADA, "enviar relato", \
  L"O modo Nativo parou durante o jogo.", L"Continuamos no Universal, sem fechar o jogo.", \
  L"Native mode stopped during play.", L"We continued on Universal, without closing the game.") \
X(NAT_204, "D5-NAT-204", AREA_NAT, SEV_PARADA, "enviar relato", \
  L"A imagem parou de atualizar.", L"Reiniciamos o DLSS 5. O seu jogo não foi afetado.", \
  L"The picture stopped updating.", L"We restarted DLSS 5. Your game is unaffected.") \
X(NAT_205, "D5-NAT-205", AREA_NAT, SEV_PARADA, "enviar relato", \
  L"Modo Universal ativado.", L"A imagem do modo Nativo parou. O seu jogo não foi afetado.", \
  L"Universal mode on.", L"The Native picture stopped. Your game is unaffected.") \
X(NAT_206, "D5-NAT-206", AREA_NAT, SEV_PARADA, "enviar relato", \
  L"Modo Universal ativado.", L"Tivemos um problema dentro do jogo e saímos na hora. O jogo continua normal.", \
  L"Universal mode on.", L"We hit a problem inside the game and stepped out at once. The game keeps running.") \
X(NAT_207, "D5-NAT-207", AREA_NAT, SEV_AVISO,  "nenhuma", \
  L"O modo Nativo estava deixando este jogo mais lento.", L"O Universal vai render melhor aqui.", \
  L"Native mode was slowing this game down.", L"Universal will do better here.") \
X(NAT_208, "D5-NAT-208", AREA_NAT, SEV_AVISO,  "nenhuma", \
  L"Tentamos duas vezes e não funcionou.", L"Você pode continuar no Universal.", \
  L"We tried twice and it did not work.", L"You can carry on with Universal.") \
X(NAT_209, "D5-NAT-209", AREA_NAT, SEV_PARADA, "enviar relato", \
  L"O modo Nativo não abriu nesta placa de vídeo.", L"Seguindo no Universal.", \
  L"Native mode did not start on this graphics card.", L"Staying on Universal.") \
X(NAT_210, "D5-NAT-210", AREA_NAT, SEV_AVISO,  "nenhuma", \
  L"O modo Nativo já teve problemas neste jogo.", L"Se acontecer de novo, use o Universal aqui.", \
  L"Native mode has had trouble in this game before.", L"If it happens again, use Universal here.") \
X(NAT_999, "D5-NAT-999", AREA_NAT, SEV_PARADA, "enviar relato", \
  L"O modo Nativo parou por um motivo que ainda não conhecemos.", L"Continuamos no Universal. O relato ajuda a corrigir.", \
  L"Native mode stopped for a reason we do not know yet.", L"We continued on Universal. A report helps us fix it.") \
/* ---- UNI: modo Universal (captura e apresentador) ---- */ \
X(UNI_001, "D5-UNI-001", AREA_UNI, SEV_PARADA, "verificar politica", \
  L"O Windows não deixou capturar a tela.", L"Uma política do sistema ou do antivírus pode estar bloqueando.", \
  L"Windows would not allow screen capture.", L"A system or antivirus policy may be blocking it.") \
X(UNI_002, "D5-UNI-002", AREA_UNI, SEV_PARADA, "atualizar Windows", \
  L"Esta versão do Windows não tem a captura de tela que o DLSS 5 usa.", L"Atualize o Windows para usar o programa.", \
  L"This version of Windows lacks the screen capture DLSS 5 needs.", L"Update Windows to use the program.") \
X(UNI_003, "D5-UNI-003", AREA_UNI, SEV_PARADA, "instalar pacote", \
  L"Faltam os componentes de vídeo do Windows.", L"Instale o Media Feature Pack (Windows N).", \
  L"Windows media components are missing.", L"Install the Media Feature Pack (Windows N).") \
X(UNI_101, "D5-UNI-101", AREA_UNI, SEV_INFO,   "nenhuma", \
  L"O jogo foi fechado.", L"", \
  L"The game was closed.", L"") \
X(UNI_102, "D5-UNI-102", AREA_UNI, SEV_AVISO,  "reescolher fonte", \
  L"Não foi possível ler o tamanho da imagem do jogo.", L"Tente escolher o jogo de novo.", \
  L"The game picture size could not be read.", L"Try picking the game again.") \
X(UNI_103, "D5-UNI-103", AREA_UNI, SEV_AVISO,  "abrir o jogo", \
  L"Não encontramos esse jogo aberto.", L"Abra o jogo e escolha de novo.", \
  L"We could not find that game running.", L"Open the game and pick it again.") \
X(UNI_201, "D5-UNI-201", AREA_UNI, SEV_PARADA, "reabrir", \
  L"Não conseguimos abrir a imagem do jogo.", L"Feche e abra o DLSS 5.", \
  L"We could not open the game picture.", L"Close and reopen DLSS 5.") \
X(UNI_202, "D5-UNI-202", AREA_UNI, SEV_PARADA, "reabrir", \
  L"A imagem do jogo parou de chegar.", L"Feche e abra o DLSS 5.", \
  L"The game picture stopped arriving.", L"Close and reopen DLSS 5.") \
X(UNI_203, "D5-UNI-203", AREA_UNI, SEV_PARADA, "reabrir", \
  L"O DLSS 5 não acompanhou a troca de tamanho da tela.", L"Feche e abra o DLSS 5.", \
  L"DLSS 5 could not follow the screen size change.", L"Close and reopen DLSS 5.") \
X(UNI_204, "D5-UNI-204", AREA_UNI, SEV_QUEDA,  "reabrir", \
  L"O DLSS 5 não conseguiu abrir a própria janela.", L"Feche e abra o programa.", \
  L"DLSS 5 could not open its own window.", L"Close and reopen the program.") \
X(UNI_999, "D5-UNI-999", AREA_UNI, SEV_PARADA, "enviar relato", \
  L"A imagem do jogo parou por um motivo que ainda não conhecemos.", L"O relato ajuda a corrigir.", \
  L"The game picture stopped for a reason we do not know yet.", L"A report helps us fix it.") \
/* ---- UPS: modo Upstream ---- */ \
X(UPS_101, "D5-UPS-101", AREA_UPS, SEV_INFO,   "ligar upscaler no jogo", \
  L"DLSS 5 Upstream pausado — seguindo na imagem final.", L"Ligue o DLSS, o FSR ou o XeSS no menu do jogo para usá-lo.", \
  L"DLSS 5 Upstream paused — running on the final picture.", L"Turn on DLSS, FSR or XeSS in the game menu to use it.") \
X(UPS_102, "D5-UPS-102", AREA_UPS, SEV_INFO,   "ligar nas opcoes", \
  L"O Upstream está desligado nas configurações.", L"Ligue-o na tela de escolha para usar neste jogo.", \
  L"Upstream is off in settings.", L"Turn it on in the selection screen to use it here.") \
X(UPS_103, "D5-UPS-103", AREA_UPS, SEV_INFO,   "nenhuma", \
  L"Este jogo não aceita o DLSS 5 Upstream.", L"Seguindo no Nativo.", \
  L"This game does not accept DLSS 5 Upstream.", L"Staying on Native.") \
X(UPS_104, "D5-UPS-104", AREA_UPS, SEV_INFO,   "nenhuma", \
  L"O Upstream não funciona com este jogo.", L"Seguindo no Nativo.", \
  L"Upstream does not work with this game.", L"Staying on Native.") \
X(UPS_201, "D5-UPS-201", AREA_UPS, SEV_PARADA, "enviar relato", \
  L"O Upstream não conseguiu começar neste jogo.", L"Seguindo no Nativo — o seu jogo não foi afetado.", \
  L"Upstream could not start in this game.", L"Staying on Native — your game is unaffected.") \
X(UPS_202, "D5-UPS-202", AREA_UPS, SEV_PARADA, "enviar relato", \
  L"O Upstream não conseguiu começar neste jogo.", L"Seguindo no Nativo — o seu jogo não foi afetado.", \
  L"Upstream could not start in this game.", L"Staying on Native — your game is unaffected.") \
X(UPS_203, "D5-UPS-203", AREA_UPS, SEV_PARADA, "enviar relato", \
  L"O Upstream deixaria a imagem com as cores erradas neste jogo.", L"Seguindo no Nativo.", \
  L"Upstream would give this game wrong colours.", L"Staying on Native.") \
X(UPS_204, "D5-UPS-204", AREA_UPS, SEV_PARADA, "reinstalar", \
  L"Há uma versão antiga do DLSS 5 dentro deste jogo.", L"Reinstale o DLSS 5 neste jogo.", \
  L"An older DLSS 5 is installed in this game.", L"Reinstall DLSS 5 for this game.") \
X(UPS_205, "D5-UPS-205", AREA_UPS, SEV_PARADA, "enviar relato", \
  L"O Upstream perdeu a ligação com o jogo.", L"Seguindo no Nativo — o seu jogo não foi afetado.", \
  L"Upstream lost its link to the game.", L"Staying on Native — your game is unaffected.") \
X(UPS_206, "D5-UPS-206", AREA_UPS, SEV_PARADA, "enviar relato", \
  L"O Upstream não abriu nesta placa de vídeo.", L"Seguindo no Nativo.", \
  L"Upstream did not start on this graphics card.", L"Staying on Native.") \
X(UPS_207, "D5-UPS-207", AREA_UPS, SEV_AVISO,  "baixar qualidade", \
  L"O DLSS 5 não está acompanhando o ritmo deste jogo.", L"Reduza a qualidade do DLSS 5 ou a resolução.", \
  L"DLSS 5 is not keeping up with this game.", L"Lower the DLSS 5 quality or the resolution.") \
X(UPS_208, "D5-UPS-208", AREA_UPS, SEV_AVISO,  "nenhuma", \
  L"Um quadro passou sem o DLSS 5.", L"", \
  L"A frame went through without DLSS 5.", L"") \
X(UPS_209, "D5-UPS-209", AREA_UPS, SEV_PARADA, "enviar relato", \
  L"O Upstream recebeu algo que não entendeu.", L"Seguindo no Nativo.", \
  L"Upstream received something it did not understand.", L"Staying on Native.") \
X(UPS_210, "D5-UPS-210", AREA_UPS, SEV_AVISO,  "enviar relato", \
  L"O Upstream não pôde entregar a imagem neste tamanho.", L"Passamos a imagem do jogo sem mudar.", \
  L"Upstream could not deliver the picture at this size.", L"We passed the game picture through unchanged.") \
X(UPS_211, "D5-UPS-211", AREA_UPS, SEV_INFO,   "religar upscaler", \
  L"DLSS 5 Upstream pausado — seguindo na imagem final.", L"Religue o DLSS, o FSR ou o XeSS para voltar.", \
  L"DLSS 5 Upstream paused — running on the final picture.", L"Turn DLSS, FSR or XeSS back on to resume.") \
X(UPS_999, "D5-UPS-999", AREA_UPS, SEV_PARADA, "enviar relato", \
  L"O Upstream parou por um motivo que ainda não conhecemos.", L"Seguindo no Nativo. O relato ajuda a corrigir.", \
  L"Upstream stopped for a reason we do not know yet.", L"Staying on Native. A report helps us fix it.") \
/* ---- FG: frame generation ---- */ \
X(FG_001,  "D5-FG-001",  AREA_FG,  SEV_AVISO,  "atualizar driver", \
  L"Os quadros extras não estão disponíveis nesta máquina.", L"Atualize o driver da placa de vídeo.", \
  L"Extra frames are not available on this machine.", L"Update your graphics driver.") \
X(FG_002,  "D5-FG-002",  AREA_FG,  SEV_AVISO,  "atualizar driver", \
  L"Os quadros extras não estão disponíveis nesta máquina.", L"Atualize o driver da placa de vídeo.", \
  L"Extra frames are not available on this machine.", L"Update your graphics driver.") \
X(FG_003,  "D5-FG-003",  AREA_FG,  SEV_AVISO,  "reinstalar", \
  L"Faltam peças dos quadros extras no pacote.", L"Reinstale o DLSS 5.", \
  L"Parts of the extra frames feature are missing.", L"Reinstall DLSS 5.") \
X(FG_101,  "D5-FG-101",  AREA_FG,  SEV_INFO,   "nenhuma", \
  L"Este jogo não aceita quadros extras.", L"", \
  L"This game does not support extra frames.", L"") \
X(FG_102,  "D5-FG-102",  AREA_FG,  SEV_INFO,   "nenhuma", \
  L"Este jogo já gera quadros extras sozinho.", L"Use o controle no menu do jogo.", \
  L"This game already generates extra frames on its own.", L"Use the control in the game menu.") \
X(FG_103,  "D5-FG-103",  AREA_FG,  SEV_INFO,   "esperar", \
  L"Preparando os quadros extras…", L"", \
  L"Getting extra frames ready…", L"") \
X(FG_104,  "D5-FG-104",  AREA_FG,  SEV_INFO,   "esperar", \
  L"Os quadros extras voltam quando a tela terminar de trocar.", L"", \
  L"Extra frames will return once the screen finishes switching.", L"") \
X(FG_105,  "D5-FG-105",  AREA_FG,  SEV_INFO,   "nenhuma", \
  L"Os quadros extras estão desligados.", L"", \
  L"Extra frames are off.", L"") \
X(FG_201,  "D5-FG-201",  AREA_FG,  SEV_PARADA, "enviar relato", \
  L"Os quadros extras não abriram neste jogo.", L"O jogo continua normal, sem eles.", \
  L"Extra frames did not start in this game.", L"The game keeps running normally without them.") \
X(FG_202,  "D5-FG-202",  AREA_FG,  SEV_PARADA, "enviar relato", \
  L"Os quadros extras não abriram neste jogo.", L"O jogo continua normal, sem eles.", \
  L"Extra frames did not start in this game.", L"The game keeps running normally without them.") \
X(FG_203,  "D5-FG-203",  AREA_FG,  SEV_PARADA, "enviar relato", \
  L"Os quadros extras não abriram neste jogo.", L"O jogo continua normal, sem eles.", \
  L"Extra frames did not start in this game.", L"The game keeps running normally without them.") \
X(FG_204,  "D5-FG-204",  AREA_FG,  SEV_PARADA, "nenhuma", \
  L"Os quadros extras pararam nesta tela.", L"Eles voltam na próxima troca de tela.", \
  L"Extra frames stopped on this screen.", L"They return on the next screen change.") \
X(FG_205,  "D5-FG-205",  AREA_FG,  SEV_PARADA, "nenhuma", \
  L"A placa de vídeo teve uma falha momentânea.", L"Os quadros extras voltam sozinhos.", \
  L"The graphics card had a brief failure.", L"Extra frames will come back on their own.") \
X(FG_206,  "D5-FG-206",  AREA_FG,  SEV_PARADA, "enviar relato", \
  L"Os quadros extras pararam.", L"O jogo continua normal. O relato ajuda a corrigir.", \
  L"Extra frames stopped.", L"The game keeps running normally. A report helps us fix it.") \
X(FG_207,  "D5-FG-207",  AREA_FG,  SEV_PARADA, "nenhuma", \
  L"Os quadros extras pararam.", L"Eles voltam na próxima troca de tela.", \
  L"Extra frames stopped.", L"They return on the next screen change.") \
X(FG_208,  "D5-FG-208",  AREA_FG,  SEV_PARADA, "enviar relato", \
  L"Os quadros extras não abriram nesta placa de vídeo.", L"O jogo continua normal, sem eles.", \
  L"Extra frames did not start on this graphics card.", L"The game keeps running normally without them.") \
X(FG_209,  "D5-FG-209",  AREA_FG,  SEV_PARADA, "enviar relato", \
  L"Os quadros extras não abriram neste jogo.", L"O jogo continua normal, sem eles.", \
  L"Extra frames did not start in this game.", L"The game keeps running normally without them.") \
X(FG_999,  "D5-FG-999",  AREA_FG,  SEV_PARADA, "enviar relato", \
  L"Os quadros extras pararam por um motivo que ainda não conhecemos.", L"O relato ajuda a corrigir.", \
  L"Extra frames stopped for a reason we do not know yet.", L"A report helps us fix it.") \
/* ---- NEU: rede neural (DLSS, NGX, feed, add-on) ---- */ \
X(NEU_001, "D5-NEU-001", AREA_NEU, SEV_PARADA, "atualizar driver", \
  L"A placa de vídeo recusou o DLSS.", L"Atualize o driver da placa de vídeo.", \
  L"The graphics card refused DLSS.", L"Update your graphics driver.") \
X(NEU_002, "D5-NEU-002", AREA_NEU, SEV_PARADA, "nenhuma", \
  L"Esta placa de vídeo não aceita o DLSS.", L"O DLSS 5 segue funcionando sem ele.", \
  L"This graphics card does not support DLSS.", L"DLSS 5 keeps working without it.") \
X(NEU_101, "D5-NEU-101", AREA_NEU, SEV_INFO,   "instalar", \
  L"O Neural Rendering não está instalado.", L"O DLSS 5 continua funcionando sem ele.", \
  L"Neural Rendering is not installed.", L"DLSS 5 keeps working without it.") \
X(NEU_102, "D5-NEU-102", AREA_NEU, SEV_AVISO,  "instalar no jogo", \
  L"O DLSS 5 não está instalado neste jogo.", L"Use o botão de instalar na tela de escolha.", \
  L"DLSS 5 is not installed in this game.", L"Use the install button on the selection screen.") \
X(NEU_103, "D5-NEU-103", AREA_NEU, SEV_AVISO,  "desligar o outro", \
  L"Outro programa de imagem está no caminho neste jogo.", L"Desligue o outro add-on para usar o DLSS 5 aqui.", \
  L"Another picture add-on is in the way in this game.", L"Turn the other add-on off to use DLSS 5 here.") \
X(NEU_104, "D5-NEU-104", AREA_NEU, SEV_INFO,   "nenhuma", \
  L"Este jogo usa uma tecnologia gráfica que ainda não suportamos.", L"", \
  L"This game uses a graphics technology we do not support yet.", L"") \
X(NEU_105, "D5-NEU-105", AREA_NEU, SEV_INFO,   "nenhuma", \
  L"Este jogo controla o próprio DLSS.", L"A rede ficou de fora para não atrapalhar o jogo.", \
  L"This game drives its own DLSS.", L"The network stayed out so as not to disturb the game.") \
X(NEU_201, "D5-NEU-201", AREA_NEU, SEV_PARADA, "enviar relato", \
  L"O DLSS não abriu neste jogo.", L"A imagem continua, sem ele.", \
  L"DLSS did not start in this game.", L"The picture keeps running without it.") \
X(NEU_202, "D5-NEU-202", AREA_NEU, SEV_PARADA, "atualizar driver", \
  L"O DLSS não abriu nesta placa de vídeo.", L"A imagem continua, sem ele.", \
  L"DLSS did not start on this graphics card.", L"The picture keeps running without it.") \
X(NEU_203, "D5-NEU-203", AREA_NEU, SEV_PARADA, "enviar relato", \
  L"O DLSS não abriu nesta placa de vídeo.", L"A imagem continua, sem ele.", \
  L"DLSS did not start on this graphics card.", L"The picture keeps running without it.") \
X(NEU_204, "D5-NEU-204", AREA_NEU, SEV_PARADA, "enviar relato", \
  L"O DLSS parou neste jogo.", L"A imagem continua, sem ele. O relato ajuda a corrigir.", \
  L"DLSS stopped in this game.", L"The picture keeps running without it. A report helps us fix it.") \
X(NEU_205, "D5-NEU-205", AREA_NEU, SEV_PARADA, "enviar relato", \
  L"O DLSS não abriu neste jogo.", L"A imagem continua, sem ele.", \
  L"DLSS did not start in this game.", L"The picture keeps running without it.") \
X(NEU_206, "D5-NEU-206", AREA_NEU, SEV_PARADA, "enviar relato", \
  L"Não conseguimos alcançar a placa de vídeo dentro deste jogo.", L"A imagem continua, sem o DLSS.", \
  L"We could not reach the graphics card inside this game.", L"The picture keeps running without DLSS.") \
X(NEU_207, "D5-NEU-207", AREA_NEU, SEV_PARADA, "atualizar Windows", \
  L"Faltam componentes gráficos do Windows.", L"Atualize o Windows.", \
  L"Windows graphics components are missing.", L"Update Windows.") \
X(NEU_208, "D5-NEU-208", AREA_NEU, SEV_PARADA, "atualizar driver", \
  L"Esta placa de vídeo não aceita o DLSS neste jogo.", L"Atualize o driver da placa de vídeo.", \
  L"This graphics card does not support DLSS in this game.", L"Update your graphics driver.") \
X(NEU_209, "D5-NEU-209", AREA_NEU, SEV_PARADA, "enviar relato", \
  L"O DLSS 5 não conseguiu entrar neste jogo.", L"A imagem continua, sem ele.", \
  L"DLSS 5 could not attach to this game.", L"The picture keeps running without it.") \
X(NEU_210, "D5-NEU-210", AREA_NEU, SEV_PARADA, "enviar relato", \
  L"O DLSS parou depois de várias tentativas.", L"A imagem continua, sem ele.", \
  L"DLSS stopped after several attempts.", L"The picture keeps running without it.") \
X(NEU_211, "D5-NEU-211", AREA_NEU, SEV_PARADA, "reabrir o jogo", \
  L"O DLSS parou neste jogo.", L"Feche e abra o jogo para tentar de novo.", \
  L"DLSS stopped in this game.", L"Close and reopen the game to try again.") \
X(NEU_212, "D5-NEU-212", AREA_NEU, SEV_AVISO,  "enviar relato", \
  L"O DLSS 5 está rodando sem a parte neural.", L"A imagem melhora menos do que poderia.", \
  L"DLSS 5 is running without the neural part.", L"The picture improves less than it could.") \
X(NEU_999, "D5-NEU-999", AREA_NEU, SEV_PARADA, "enviar relato", \
  L"O DLSS parou por um motivo que ainda não conhecemos.", L"A imagem continua. O relato ajuda a corrigir.", \
  L"DLSS stopped for a reason we do not know yet.", L"The picture keeps running. A report helps us fix it.") \
/* ---- GPU: placa e driver ---- */ \
X(GPU_001, "D5-GPU-001", AREA_GPU, SEV_PARADA, "nenhuma", \
  L"A placa de vídeo teve uma falha momentânea.", L"Reabrimos o DLSS 5 automaticamente.", \
  L"The graphics card had a brief failure.", L"We restarted DLSS 5 automatically.") \
X(GPU_002, "D5-GPU-002", AREA_GPU, SEV_AVISO,  "atualizar driver", \
  L"O driver da placa de vídeo está desatualizado.", L"Atualize o driver para o DLSS 5 render melhor.", \
  L"Your graphics driver is out of date.", L"Update it for DLSS 5 to do better.") \
X(GPU_003, "D5-GPU-003", AREA_GPU, SEV_PARADA, "baixar resolucao", \
  L"A memória da placa de vídeo acabou.", L"Baixe a resolução ou a qualidade no jogo.", \
  L"The graphics card ran out of memory.", L"Lower the resolution or quality in the game.") \
X(GPU_004, "D5-GPU-004", AREA_GPU, SEV_QUEDA,  "atualizar driver", \
  L"O DLSS 5 não conseguiu usar a placa de vídeo.", L"Atualize o driver e abra o programa de novo.", \
  L"DLSS 5 could not use the graphics card.", L"Update the driver and reopen the program.") \
X(GPU_005, "D5-GPU-005", AREA_GPU, SEV_QUEDA,  "atualizar driver", \
  L"O DLSS 5 não conseguiu usar a placa do monitor.", L"Atualize o driver e abra o programa de novo.", \
  L"DLSS 5 could not use the monitor's graphics card.", L"Update the driver and reopen the program.") \
X(GPU_006, "D5-GPU-006", AREA_GPU, SEV_AVISO,  "escolher a placa", \
  L"O DLSS 5 não ligou as duas placas de vídeo desta máquina.", L"Ligue o jogo na placa dedicada nas opções do Windows.", \
  L"DLSS 5 could not link the two graphics cards.", L"Set the game to the dedicated card in Windows settings.") \
X(GPU_101, "D5-GPU-101", AREA_GPU, SEV_INFO,   "nenhuma", \
  L"Este jogo usa um formato de imagem que ainda não suportamos.", L"", \
  L"This game uses a picture format we do not support yet.", L"") \
X(GPU_201, "D5-GPU-201", AREA_GPU, SEV_PARADA, "enviar relato", \
  L"O DLSS 5 não conseguiu preparar a placa de vídeo.", L"A imagem continua, sem ele.", \
  L"DLSS 5 could not set up the graphics card.", L"The picture keeps running without it.") \
X(GPU_202, "D5-GPU-202", AREA_GPU, SEV_PARADA, "enviar relato", \
  L"O DLSS 5 não conseguiu preparar a placa de vídeo.", L"A imagem continua, sem ele.", \
  L"DLSS 5 could not set up the graphics card.", L"The picture keeps running without it.") \
X(GPU_203, "D5-GPU-203", AREA_GPU, SEV_QUEDA,  "reinstalar", \
  L"O DLSS 5 não conseguiu preparar a imagem.", L"Reinstale o programa.", \
  L"DLSS 5 could not prepare the picture.", L"Reinstall the program.") \
X(GPU_999, "D5-GPU-999", AREA_GPU, SEV_PARADA, "enviar relato", \
  L"A placa de vídeo teve um problema que ainda não conhecemos.", L"O relato ajuda a corrigir.", \
  L"The graphics card had a problem we do not know yet.", L"A report helps us fix it.") \
/* ---- HDR ---- */ \
X(HDR_001, "D5-HDR-001", AREA_HDR, SEV_AVISO,  "marcar a caixa", \
  L"O monitor está em HDR e a imagem chega lavada.", L"Marque \"Desligar o HDR do monitor\" ou aperte Win+Alt+B.", \
  L"The monitor is in HDR and the picture looks washed out.", L"Tick \"Turn off monitor HDR\" or press Win+Alt+B.") \
X(HDR_002, "D5-HDR-002", AREA_HDR, SEV_AVISO,  "marcar a caixa", \
  L"O console está mandando HDR e a imagem chega lavada.", L"Marque a caixa de HDR ou ponha o console em SDR.", \
  L"The console is sending HDR and the picture looks washed out.", L"Tick the HDR box or set the console to SDR.") \
X(HDR_101, "D5-HDR-101", AREA_HDR, SEV_AVISO,  "nenhuma", \
  L"Não identificamos o seu monitor.", L"O HDR fica como está.", \
  L"We could not identify your monitor.", L"HDR is left as it is.") \
X(HDR_201, "D5-HDR-201", AREA_HDR, SEV_AVISO,  "Win+Alt+B", \
  L"Não conseguimos desligar o HDR do monitor.", L"Aperte Win+Alt+B para desligar na mão.", \
  L"We could not turn off monitor HDR.", L"Press Win+Alt+B to turn it off by hand.") \
X(HDR_202, "D5-HDR-202", AREA_HDR, SEV_AVISO,  "Win+Alt+B", \
  L"Não conseguimos religar o HDR do monitor.", L"Aperte Win+Alt+B para religar na mão.", \
  L"We could not turn monitor HDR back on.", L"Press Win+Alt+B to turn it back on by hand.") \
X(HDR_999, "D5-HDR-999", AREA_HDR, SEV_AVISO,  "enviar relato", \
  L"O HDR do monitor teve um problema que ainda não conhecemos.", L"Win+Alt+B liga e desliga o HDR na mão.", \
  L"Monitor HDR had a problem we do not know yet.", L"Win+Alt+B toggles HDR by hand.") \
/* ---- IO: arquivo, pasta e permissao ---- */ \
X(IO_001,  "D5-IO-001",  AREA_IO,  SEV_PARADA, "mover a pasta", \
  L"O DLSS 5 não consegue guardar as suas configurações.", L"Mova o programa para Documentos ou a Área de Trabalho.", \
  L"DLSS 5 cannot save your settings.", L"Move the program to Documents or the Desktop.") \
X(IO_002,  "D5-IO-002",  AREA_IO,  SEV_AVISO,  "nenhuma", \
  L"As suas configurações vão para a pasta de dados do usuário.", L"", \
  L"Your settings are saved to your user data folder.", L"") \
X(IO_003,  "D5-IO-003",  AREA_IO,  SEV_AVISO,  "rodar elevado", \
  L"A pasta deste jogo não aceita gravação.", L"Abra o DLSS 5 como administrador para instalar aqui.", \
  L"This game's folder does not accept writing.", L"Open DLSS 5 as administrator to install here.") \
X(IO_004,  "D5-IO-004",  AREA_IO,  SEV_AVISO,  "remover o outro", \
  L"Já existe outro programa de imagem nesta pasta.", L"Não vamos substituir o que você instalou.", \
  L"Another picture program is already in this folder.", L"We will not replace what you installed.") \
X(IO_005,  "D5-IO-005",  AREA_IO,  SEV_AVISO,  "escolher pasta", \
  L"Não encontramos a pasta deste jogo.", L"Escolha a pasta do jogo à mão.", \
  L"We could not find this game's folder.", L"Pick the game folder by hand.") \
X(IO_101,  "D5-IO-101",  AREA_IO,  SEV_AVISO,  "reinstalar", \
  L"Faltam peças no pacote do DLSS 5.", L"Reinstale o programa.", \
  L"Parts of the DLSS 5 package are missing.", L"Reinstall the program.") \
X(IO_102,  "D5-IO-102",  AREA_IO,  SEV_AVISO,  "fechar o jogo", \
  L"O jogo está aberto e o arquivo está em uso.", L"Feche o jogo e tente de novo.", \
  L"The game is open and the file is in use.", L"Close the game and try again.") \
X(IO_103,  "D5-IO-103",  AREA_IO,  SEV_INFO,   "nenhuma", \
  L"Já estava instalado neste jogo.", L"", \
  L"Already installed in this game.", L"") \
X(IO_201,  "D5-IO-201",  AREA_IO,  SEV_AVISO,  "enviar relato", \
  L"O DLSS 5 não conseguiu guardar a configuração deste jogo.", L"Ela volta ao padrão na próxima vez.", \
  L"DLSS 5 could not save this game's settings.", L"They go back to default next time.") \
X(IO_202,  "D5-IO-202",  AREA_IO,  SEV_AVISO,  "liberar disco", \
  L"O DLSS 5 não conseguiu guardar as suas configurações.", L"Verifique o espaço em disco.", \
  L"DLSS 5 could not save your settings.", L"Check your free disk space.") \
X(IO_999,  "D5-IO-999",  AREA_IO,  SEV_AVISO,  "enviar relato", \
  L"Não conseguimos gravar um arquivo, por um motivo que ainda não conhecemos.", L"O relato ajuda a corrigir.", \
  L"We could not write a file, for a reason we do not know yet.", L"A report helps us fix it.") \
/* ---- APP: o programa ---- */ \
X(APP_001, "D5-APP-001", AREA_APP, SEV_INFO,   "nenhuma", \
  L"O DLSS 5 já está aberto.", L"", \
  L"DLSS 5 is already running.", L"") \
X(APP_002, "D5-APP-002", AREA_APP, SEV_AVISO,  "reabrir na mao", \
  L"Não conseguimos reabrir o DLSS 5 sozinhos.", L"Feche e abra o programa do jeito de sempre.", \
  L"We could not reopen DLSS 5 on our own.", L"Close and reopen the program as usual.") \
X(APP_003, "D5-APP-003", AREA_APP, SEV_AVISO,  "reabrir na mao", \
  L"Não conseguimos reabrir o DLSS 5 sozinhos.", L"Feche e abra para a mudança valer.", \
  L"We could not reopen DLSS 5 on our own.", L"Close and reopen for the change to take effect.") \
X(APP_004, "D5-APP-004", AREA_APP, SEV_AVISO,  "reabrir", \
  L"Uma das camadas do DLSS 5 não subiu.", L"Feche e abra o programa.", \
  L"One of the DLSS 5 layers did not start.", L"Close and reopen the program.") \
X(APP_201, "D5-APP-201", AREA_APP, SEV_QUEDA,  "enviar relato", \
  L"O DLSS 5 fechou sozinho da última vez.", L"O seu jogo não foi afetado. O relato ajuda a corrigir.", \
  L"DLSS 5 closed on its own last time.", L"Your game was unaffected. A report helps us fix it.") \
X(APP_202, "D5-APP-202", AREA_APP, SEV_QUEDA,  "enviar relato", \
  L"O DLSS 5 fechou sozinho da última vez.", L"O seu jogo não foi afetado. O relato ajuda a corrigir.", \
  L"DLSS 5 closed on its own last time.", L"Your game was unaffected. A report helps us fix it.") \
X(APP_203, "D5-APP-203", AREA_APP, SEV_QUEDA,  "reabrir", \
  L"O DLSS 5 precisou parar.", L"O jogo continua. Feche e abra o programa; se repetir, mande um relato.", \
  L"DLSS 5 had to stop.", L"The game keeps running. Close and reopen the program; if it repeats, send a report.") \
X(APP_999, "D5-APP-999", AREA_APP, SEV_PARADA, "enviar relato", \
  L"Algo não funcionou e ainda não sabemos o quê.", L"O seu jogo não foi afetado. O relato ajuda a corrigir.", \
  L"Something did not work and we do not know what yet.", L"Your game was unaffected. A report helps us fix it.") \
/* ---- NET: internet ---- */ \
X(NET_001, "D5-NET-001", AREA_NET, SEV_INFO,   "nenhuma", \
  L"Não deu para procurar atualizações agora.", L"Tentaremos de novo mais tarde.", \
  L"We could not check for updates right now.", L"We will try again later.") \
X(NET_002, "D5-NET-002", AREA_NET, SEV_AVISO,  "tentar depois", \
  L"A atualização não terminou de baixar.", L"Tente de novo quando a internet estiver estável.", \
  L"The update did not finish downloading.", L"Try again when your connection is stable.") \
X(NET_003, "D5-NET-003", AREA_NET, SEV_AVISO,  "baixar de novo", \
  L"O arquivo da atualização não confere.", L"Baixe de novo pelo botão de atualizar.", \
  L"The update file does not check out.", L"Download it again with the update button.") \
X(NET_101, "D5-NET-101", AREA_NET, SEV_INFO,   "nenhuma", \
  L"O relato ficou guardado no seu computador.", L"Ele vai sozinho quando a internet voltar.", \
  L"The report was saved on your computer.", L"It will be sent when your connection returns.") \
X(NET_999, "D5-NET-999", AREA_NET, SEV_AVISO,  "nenhuma", \
  L"Não conseguimos falar com o servidor, por um motivo que ainda não conhecemos.", L"", \
  L"We could not reach the server, for a reason we do not know yet.", L"")
/* LAPIDES - numero queimado, nunca reutilizar (o significado antigo fica aqui):
   (nenhuma ainda) */

// O enum: D5_NENHUM = 0 e "sem codigo" (cartaz de estado, nada a registrar).
enum Codigo {
    D5_NENHUM = 0,
#define X(nome, cod, area, sev, acao, pt1, pt2, en1, en2) D5_##nome,
    D5_CATALOGO(X)
#undef X
    D5_TOTAL
};

struct Erro {
    Codigo         cod;
    const char*    codigo;   // "D5-NAT-104": o que o jogador dita e o que vai no log
    Area           area;
    Sev            sev;
    const char*    acao;     // acao sugerida, em uma palavra ou duas (so no relato, nunca na tela)
    const wchar_t* pt1; const wchar_t* pt2;
    const wchar_t* en1; const wchar_t* en2;
};
static constexpr Erro kCatalogo[] = {
    { D5_NENHUM, "", AREA_APP, SEV_INFO, "", L"", L"", L"", L"" },
#define X(nome, cod, area, sev, acao, pt1, pt2, en1, en2) { D5_##nome, cod, area, sev, acao, pt1, pt2, en1, en2 },
    D5_CATALOGO(X)
#undef X
};
static constexpr int kNCatalogo = (int)(sizeof(kCatalogo) / sizeof(kCatalogo[0]));
static_assert(kNCatalogo == (int)D5_TOTAL, "catalogo e enum desalinhados");

// O verificador em tempo de compilacao: nenhum numero repetido, todo codigo com o formato
// D5-AAA-NNN e os quatro textos (linha 1 PT/EN nunca vazia). Falha aqui = o build nao sai.
constexpr bool IgualStr(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
constexpr bool CatalogoValido() {
    for (int i = 1; i < kNCatalogo; i++) {
        const Erro& e = kCatalogo[i];
        if (e.cod != (Codigo)i) return false;
        if (e.codigo[0] != 'D' || e.codigo[1] != '5' || e.codigo[2] != '-') return false;
        if (e.pt1[0] == 0 || e.en1[0] == 0) return false;
        for (int j = 1; j < i; j++) if (IgualStr(kCatalogo[j].codigo, e.codigo)) return false;
    }
    return true;
}
static_assert(CatalogoValido(), "catalogo de codigos: numero repetido, formato errado ou texto vazio");

// Obter por enum e por string ("D5-NAT-104" -> entrada; desconhecido -> D5_NENHUM).
static const Erro& Catalogo(Codigo c) { return (c > D5_NENHUM && c < D5_TOTAL) ? kCatalogo[(int)c] : kCatalogo[0]; }
[[maybe_unused]] static Codigo PorCodigo(const char* s) {
    if (!s || !*s) return D5_NENHUM;
    for (int i = 1; i < kNCatalogo; i++) if (IgualStr(kCatalogo[i].codigo, s)) return (Codigo)i;
    return D5_NENHUM;
}
static const char* CodigoStr(Codigo c) { return Catalogo(c).codigo; }
[[maybe_unused]] static const char* NomeArea(Area a) {
    static const char* k[AREA_N] = { "NAT", "UNI", "UPS", "FG", "NEU", "GPU", "HDR", "IO", "APP", "NET" };
    return (a >= 0 && a < AREA_N) ? k[a] : "?";
}
static const char* NomeSev(Sev s) {
    return s == SEV_INFO ? "info" : s == SEV_AVISO ? "aviso" : s == SEV_PARADA ? "parada" : "queda";
}
// O 9xx da area: quando a assinatura nao casou com nada mas se sabe em que subsistema estava.
[[maybe_unused]] static Codigo DesconhecidoDa(Area a) {
    switch (a) {
    case AREA_NAT: return D5_NAT_999; case AREA_UNI: return D5_UNI_999; case AREA_UPS: return D5_UPS_999;
    case AREA_FG:  return D5_FG_999;  case AREA_NEU: return D5_NEU_999; case AREA_GPU: return D5_GPU_999;
    case AREA_HDR: return D5_HDR_999; case AREA_IO:  return D5_IO_999;  case AREA_NET: return D5_NET_999;
    default: return D5_APP_999;
    }
}
// O texto do cartaz para um codigo: "linha 1\nlinha 2" no idioma da interface.
// sec. 22: o "F8" do catalogo (NAT_201/202) e a tecla de INTEGRAR (Nativo <-> Universal), que a pessoa pode trocar ou
// tirar em Atalhos. O main diz qual e agora (vazio = sem tecla): a tela nunca promete uma tecla que nao existe.
static std::wstring (*g_teclaIntegrar)() = nullptr;
static std::wstring TrocarF8(std::wstring t) {
    if (!g_teclaIntegrar || t.find(L"F8") == std::wstring::npos) return t;
    const std::wstring k = g_teclaIntegrar();
    if (k == L"F8") return t;
    if (k.empty()) {
        const struct { const wchar_t* de; const wchar_t* para; } frases[] = {
            { L"F8 tenta de novo.", L"O chip Nativo do painel tenta de novo." },
            { L"Press F8 to retry.", L"The Native chip in the panel retries." },
        };
        for (const auto& f : frases) { const size_t p = t.find(f.de); if (p != std::wstring::npos) t.replace(p, wcslen(f.de), f.para); }
        return t;
    }
    for (size_t p = t.find(L"F8"); p != std::wstring::npos; p = t.find(L"F8", p + k.size())) t.replace(p, 2, k);
    return t;
}
[[maybe_unused]] static std::wstring TextoDe(Codigo c) {
    const Erro& e = Catalogo(c);
    std::wstring t = TS(e.pt1, e.en1);
    const wchar_t* l2 = T(e.pt2, e.en2);
    if (l2 && *l2) { t += L'\n'; t += l2; }
    return TrocarF8(t);
}

// ------------------------------------------------------------ O ID DA OCORRENCIA
// O codigo identifica o TIPO; este id identifica AQUELE acontecimento (RFC 9457: type x
// instance), para casar o que o jogador dita com o que esta no arquivo. Seis simbolos em
// Crockford Base32 (sem I, L, O, U - os que se confundem com 1 e 0), sorteados; nao identifica
// a maquina. Sem alocacao, para poder rodar dentro do filtro de queda (Queda::Gravar).
static const char kBase32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
static void NovoId(char out[7]) {
    static unsigned long long s = 0;
    if (s == 0) {
        LARGE_INTEGER q{}; QueryPerformanceCounter(&q);
        s = (unsigned long long)q.QuadPart ^ ((unsigned long long)GetCurrentProcessId() << 32) ^ GetTickCount64();
        if (s == 0) s = 0x9E3779B97F4A7C15ull;
    }
    // xorshift64*: bom o bastante para um id de 30 bits que so precisa nao repetir na sessao
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    unsigned long long v = s * 2685821657736338717ull;
    for (int i = 0; i < 6; i++) { out[i] = kBase32[v & 31]; v >>= 5; }
    out[6] = 0;
}

// ------------------------------------------------------------ AS ASSINATURAS
// O programa NAO adivinha: casa a falha contra estes mapas, na ordem, e para no primeiro
// que bater. O que nao bate vira o 9xx da area, com a assinatura crua no log.

// 1. Sinal do vigia de saude do Nativo (exato - ja e um numero). Ver VigiaNativo no main.cpp.
static Codigo DoVigia(int sinal) {
    switch (sinal) {
    case 1: return D5_NAT_104;   // a DLL nao viu Present em 10 s (Vulkan/OpenGL)
    case 2: return D5_NAT_204;   // in_fence nao chegou N vezes - tela preta
    case 3: return D5_NAT_205;   // saida do app parada com o jogo mudando - congelou
    case 4: return D5_GPU_001;   // device do app removido
    case 5: return D5_NAT_207;   // jogo abaixo de 50 % do fps de antes por 5 s
    case 6: return D5_NAT_206;   // a DLL do Nativo ficou inerte (estouro num hook nosso)
    }
    return D5_NAT_999;
}

// 2. String de motivo do feeder (dlss5-feed.status: motivo=). A CHAVE e o trecho do texto
// tecnico que identifica a situacao (`sinal`), casado por substring contra o que o feeder
// escreve - as 44 strings do FeedDisable mais as 7 do g_nr_why (X-CODIGOS-ERRO §2.6). A ORDEM
// IMPORTA: o primeiro sinal que casar vence, entao os mais especificos vem antes.
// Cada entrada leva o CODIGO do catalogo e o texto do I8 para AQUELA situacao (o catalogo
// tem o texto generico do codigo; aqui a frase e a mais precisa, revisada pelo dono no I8).
// O texto cru vai para o log, sempre; para a tela vai o daqui.
struct Entrada {
    const char*    id;      // nome legivel da situacao
    Codigo         codigo;  // o codigo do catalogo
    const char*    sinal;   // substring do texto tecnico (ASCII, como o feeder escreve)
    const wchar_t* pt;      // "o que aconteceu\nacao (se houver)"
    const wchar_t* en;
};
static const Entrada kFeed[] = {
    { "NEU_GPU_REINICIOU", D5_GPU_001, "the GPU was reset by Windows",
      L"A placa de vídeo reiniciou\nFeche e abra o programa de novo.",
      L"The graphics card reset\nClose and open the program again." },
    { "NEU_PLACA_SUMIU", D5_GPU_001, "the D3D12 device was removed",
      L"A placa de vídeo perdeu a conexão com o programa\nFeche e abra o programa de novo.",
      L"The graphics card lost its connection to the program\nClose and open the program again." },
    { "NEU_DRIVER_RECUSOU", D5_NEU_001, "DLSS is not available here: the driver answered",
      L"O driver da placa recusou o DLSS nesta configuração\nA imagem continua sem a rede. Atualize o driver da NVIDIA.",
      L"The graphics driver refused DLSS in this setup\nThe picture keeps running without the network. Update the NVIDIA driver." },
    { "NEU_JOGO_TEM_DLSS", D5_NEU_105, "this game most likely drives DLSS itself",
      L"Este jogo controla o próprio DLSS\nA imagem continua; a rede ficou de fora para não atrapalhar o jogo.",
      L"This game drives its own DLSS\nThe picture keeps running; the network stayed out so as not to disturb the game." },
    { "NEU_TRAVOU_AO_INICIAR", D5_NEU_204, "creating the DLSS feature crashed",
      L"O DLSS travou ao iniciar\nA imagem continua sem a rede. Este jogo pode não ser compatível.",
      L"DLSS crashed while starting\nThe picture keeps running without the network. This game may not be compatible." },
    { "NEU_TRAVOU_AO_INICIAR", D5_NEU_204, "creating the DLSS feature hung",
      L"O DLSS travou ao iniciar\nA imagem continua sem a rede. Este jogo pode não ser compatível.",
      L"DLSS crashed while starting\nThe picture keeps running without the network. This game may not be compatible." },
    { "NEU_NAO_INICIOU", D5_NEU_201, "creating the DLSS feature keeps failing",
      L"O DLSS não conseguiu iniciar\nA imagem continua sem a rede. Reabra o programa; se repetir, mande um relato.",
      L"DLSS could not start\nThe picture keeps running without the network. Reopen the program; if it repeats, send a report." },
    { "NEU_NAO_INICIOU", D5_NEU_202, "NGX would not re-initialise",
      L"O DLSS não conseguiu iniciar\nA imagem continua sem a rede. Reabra o programa; se repetir, mande um relato.",
      L"DLSS could not start\nThe picture keeps running without the network. Reopen the program; if it repeats, send a report." },
    { "NEU_NAO_INICIOU", D5_NEU_205, "the D3D12/NGX session failed to start",
      L"O DLSS não conseguiu iniciar\nA imagem continua sem a rede. Reabra o programa; se repetir, mande um relato.",
      L"DLSS could not start\nThe picture keeps running without the network. Reopen the program; if it repeats, send a report." },
    { "NEU_NAO_INICIOU", D5_NEU_203, "NGX parameter allocation failed",
      L"O DLSS não conseguiu iniciar\nA imagem continua sem a rede. Reabra o programa; se repetir, mande um relato.",
      L"DLSS could not start\nThe picture keeps running without the network. Reopen the program; if it repeats, send a report." },
    { "NEU_FALHAS_SEGUIDAS", D5_NEU_210, "repeated failures",
      L"A rede neural parou depois de falhar várias vezes\nA imagem continua sem ela. Reabrir o programa costuma resolver.",
      L"The neural network stopped after failing several times\nThe picture keeps running without it. Reopening the program usually fixes it." },
    // X-TDR-2 (19/09/2026): a guarda de memoria de video do feed, ANTES de montar a rede. O jogo
    // reservou a placa (o COD deixa ~2,7 GB para todo o resto) e a rede em 3440x1440 nao cabe:
    // montar assim mesmo era o que punha o processo a paginar a cada pacote ate o driver
    // reiniciar o canal (nvlddmkm 153). O feed segura e pergunta de novo a cada 5 s; a acao que
    // resolve esta com a pessoa, no jogo. Os dois sinais sao substrings do mesmo motivo.
    { "NEU_VRAM_ESPERA", D5_GPU_003, "waiting for room",
      L"A memória de vídeo está curta: a rede espera folga\nO jogo reservou a placa. Baixe o uso de VRAM ou as texturas no jogo; a rede entra sozinha quando couber.",
      L"Video memory is short: the network is waiting for room\nThe game reserved the card. Lower the game's VRAM use or textures; the network starts by itself when it fits." },
    { "NEU_VRAM_REDUZIDA", D5_GPU_003, "running it reduced",
      L"A memória de vídeo está curta: a rede vai rodar menor\nO jogo reservou a placa. Para a rede inteira, baixe o uso de VRAM ou as texturas no jogo.",
      L"Video memory is short: the network will run smaller\nThe game reserved the card. For the full network, lower the game's VRAM use or textures." },
    { "NEU_FORMATO", D5_GPU_101, "unsupported backbuffer format",
      L"O formato de imagem deste jogo não é aceito\nA imagem continua sem a rede.",
      L"This game's picture format is not supported\nThe picture keeps running without the network." },
    { "NEU_PLACA_INALCANCAVEL", D5_NEU_206, "device/queue is not reachable",
      L"Não consegui alcançar a placa de vídeo do jogo\nA imagem continua sem a rede.",
      L"Could not reach the game's graphics card\nThe picture keeps running without the network." },
    { "NEU_PLACA_INALCANCAVEL", D5_NEU_206, "the ReShade device is not reachable",
      L"Não consegui alcançar a placa de vídeo do jogo\nA imagem continua sem a rede.",
      L"Could not reach the game's graphics card\nThe picture keeps running without the network." },
    { "NEU_SEM_DLSS_NA_PLACA", D5_NEU_002, "DLSS is not available on this GPU/driver",
      L"Esta placa ou este driver não têm DLSS\nA imagem continua sem a rede. Precisa de uma GeForce RTX com driver atual.",
      L"This card or driver has no DLSS\nThe picture keeps running without the network. It needs a GeForce RTX with a current driver." },
    { "NEU_NAO_INICIOU_PLACA", D5_NEU_202, "NGX would not initialise",
      L"O DLSS não iniciou nesta placa\nA imagem continua sem a rede. Confira o driver da NVIDIA.",
      L"DLSS did not start on this card\nThe picture keeps running without the network. Check the NVIDIA driver." },
    { "NEU_VULKAN", D5_NEU_208, "the Vulkan interop extensions are missing",
      L"Este driver não deixa o DLSS trabalhar em jogos Vulkan\nA imagem continua sem a rede. Atualize o driver.",
      L"This driver does not let DLSS work with Vulkan games\nThe picture keeps running without the network. Update the driver." },
    { "NEU_OPENGL", D5_NEU_208, "the OpenGL interop extensions are missing",
      L"Este driver não deixa o DLSS trabalhar em jogos OpenGL\nA imagem continua sem a rede. Atualize o driver.",
      L"This driver does not let DLSS work with OpenGL games\nThe picture keeps running without the network. Update the driver." },
    { "NEU_PREPARO_PLACA", D5_GPU_201, "could not create the D3D12 objects",
      L"Não consegui preparar a placa de vídeo para a rede\nA imagem continua sem ela.",
      L"Could not prepare the graphics card for the network\nThe picture keeps running without it." },
    { "NEU_PREPARO_PLACA", D5_GPU_201, "the private D3D12 device failed",
      L"Não consegui preparar a placa de vídeo para a rede\nA imagem continua sem ela.",
      L"Could not prepare the graphics card for the network\nThe picture keeps running without it." },
    { "NEU_PREPARO_PLACA", D5_NEU_207, "d3d12.dll unavailable",
      L"Não consegui preparar a placa de vídeo para a rede\nA imagem continua sem ela.",
      L"Could not prepare the graphics card for the network\nThe picture keeps running without it." },
    { "NEU_PREPARO_PLACA", D5_GPU_201, "could not create the GL framebuffer objects",
      L"Não consegui preparar a placa de vídeo para a rede\nA imagem continua sem ela.",
      L"Could not prepare the graphics card for the network\nThe picture keeps running without it." },
    { "NEU_TROCA_DE_QUADROS", D5_GPU_202, "fence creation failed",
      L"A troca de quadros com o jogo falhou\nA imagem continua sem a rede.",
      L"The frame hand-off with the game failed\nThe picture keeps running without the network." },
    { "NEU_TROCA_DE_QUADROS", D5_GPU_202, "fence import failed",
      L"A troca de quadros com o jogo falhou\nA imagem continua sem a rede.",
      L"The frame hand-off with the game failed\nThe picture keeps running without the network." },
    { "NEU_TRAVOU_NO_QUADRO", D5_NEU_204, "the DLSS evaluate crashed",
      L"O DLSS travou no meio de um quadro\nA imagem continua sem a rede. Tente outra resolução; se repetir, mande um relato.",
      L"DLSS crashed in the middle of a frame\nThe picture keeps running without the network. Try another resolution; if it repeats, send a report." },
    { "NEU_API_DO_JOGO", D5_NEU_104, "only Direct3D 11/12, Vulkan and OpenGL games are supported",
      L"Este jogo usa um tipo de gráfico que não é aceito\nA imagem continua sem a rede.",
      L"This game uses a kind of graphics that is not supported\nThe picture keeps running without the network." },
    // os motivos da REDE NEURAL (g_nr_why): estado=ok, a cadeia filtra, so a rede ficou fora
    { "NEU_OUTRO_MOTOR", D5_NEU_212, "the other neural engine is on",
      L"A rede neural está rodando pelo outro motor\nEste ficou de fora. Nada a fazer.",
      L"The neural network is running through the other engine\nThis one stayed off. Nothing to do." },
    { "NEU_SEM_MODELO", D5_IO_101, "nvngx_dlssnr.dll (the neural model) is not next to the program",
      L"O arquivo do modelo neural não está na pasta do programa\nA imagem continua sem a rede. Reinstale o pacote.",
      L"The neural model file is not in the program's folder\nThe picture keeps running without the network. Reinstall the package." },
    { "NEU_INDISPONIVEL", D5_NEU_002, "Neural Rendering unavailable on this GPU/driver",
      L"A rede neural não está disponível nesta placa ou driver\nA imagem continua sem ela. Atualize o driver da NVIDIA.",
      L"The neural network is not available on this card or driver\nThe picture keeps running without it. Update the NVIDIA driver." },
    { "NEU_DESISTIU", D5_NEU_210, "Neural Rendering gave up",
      L"A rede neural não conseguiu iniciar\nA imagem continua sem ela. Reabra o programa; se repetir, mande um relato.",
      L"The neural network could not start\nThe picture keeps running without it. Reopen the program; if it repeats, send a report." },
    { "NEU_HDR", D5_NEU_212, "Neural Rendering off in this HDR",
      L"A rede neural ficou de fora nesta sessão em HDR\nA imagem continua sem ela. Desligue o HDR do jogo para usá-la.",
      L"The neural network stayed off in this HDR session\nThe picture keeps running without it. Turn the game's HDR off to use it." },
    { "NEU_PAROU", D5_NEU_204, "Neural Rendering stopped",
      L"A rede neural parou no meio de um quadro\nA imagem continua sem ela. Se repetir, mande um relato.",
      L"The neural network stopped in the middle of a frame\nThe picture keeps running without it. If it repeats, send a report." },
};
static const int kNFeed = (int)(sizeof(kFeed) / sizeof(kFeed[0]));

// Motivo sem sinal conhecido: nunca o texto cru. D5-NEU-999, e a assinatura crua vai ao log.
static const Entrada kFeedDesconhecido = { "NEU_DESCONHECIDO", D5_NEU_999, "",
    L"O DLSS parou por um motivo que ainda não conhecemos\nA imagem continua. Mandar um relato ajuda a corrigir.",
    L"DLSS stopped for a reason we do not know yet\nThe picture keeps running. Sending a report helps us fix it." };

// A entrada para um motivo cru do feeder (ou a generica).
static const Entrada& DoFeed(const std::wstring& motivoCru) {
    std::string cru;
    cru.reserve(motivoCru.size());
    for (wchar_t c : motivoCru) cru.push_back(c < 128 ? (char)c : '?');
    for (int i = 0; i < kNFeed; i++)
        if (kFeed[i].sinal[0] && cru.find(kFeed[i].sinal) != std::string::npos) return kFeed[i];
    return kFeedDesconhecido;
}
static std::wstring Texto(const Entrada& e) { return TS(e.pt, e.en); }

// 3. String de motivo do Upstream (ponte_feeder_up.h: MotivoUp / g_upFalhaMotivo). Substring,
// os especificos antes. O texto da tela e o do catalogo (TextoDe).
struct SinalUp { const char* sinal; Codigo codigo; };
static const SinalUp kUpstream[] = {
    { "a DLL fala v",                 D5_UPS_204 },   // versao da DLL diferente do host
    { "o app nao acompanhou",         D5_UPS_207 },   // mais de 5 % dos quadros soltos
    { "tag desconhecida",             D5_UPS_209 },
    { "as fences nao abriram",        D5_UPS_206 },
    { "pipe fechado",                 D5_UPS_205 },
    { "timeout no pipe",              D5_UPS_205 },
    { "erro de pipe",                 D5_UPS_205 },
    { "ConnectNamedPipe",             D5_UPS_205 },
    { "hello sem magic",              D5_UPS_201 },
    { "sem handle do processo",       D5_UPS_201 },
    { "self_process nao nomeia",      D5_UPS_201 },
    { "DuplicateHandle das fences",   D5_UPS_201 },
    { "nao mandou nenhum quadro",     D5_UPS_201 },   // sessao negociada sem quadro em 10 s
};
static Codigo DoUpstream(const char* motivoCru) {
    if (!motivoCru || !*motivoCru) return D5_UPS_999;
    for (const SinalUp& s : kUpstream) if (strstr(motivoCru, s.sinal)) return s.codigo;
    return D5_UPS_999;
}

// Falha fatal do proprio app (X-CE-2): o cartaz que sobe antes de o processo sair, para o
// modo Nativo, onde a caixa de dialogo nao aparece por cima do jogo. E o D5-APP-203.
static const Entrada kFatal = { "APP_FATAL", D5_APP_203, "",
    L"O DLSS5 For All encontrou um erro e vai fechar\nO jogo continua. Abra o programa de novo; se repetir, mande um relato.",
    L"DLSS5 For All hit an error and will close\nThe game keeps running. Open the program again; if it repeats, send a report." };

} // namespace Msg
