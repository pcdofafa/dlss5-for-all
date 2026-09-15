---
name: backlog-pedidos
description: Lista viva de tudo que o usuário pediu e ainda não foi entregue (ou aguarda decisão dele); consultar no início de cada sessão e atualizar ao fechar itens
metadata: 
  node_type: memory
  type: project
  originSessionId: 744da3a1-8818-4a7e-8dbe-35c7e01a0223
  modified: 2026-09-11T07:12:08.091Z
---

Regra do usuário (2026-09-11): "cria uma forma de salvar implementações que pedi e ainda não
foram feitas, porque às vezes eu peço algo e você esquece; se estiver documentado você vai se
lembrar sempre". Este arquivo é essa lista. Ler no começo de toda sessão; mover para "Fechado"
com a data quando entregar; nunca apagar um pedido sem o usuário confirmar.

## BLOQUEADOR DE RELEASE (usuário, 2026-09-11 ~04:45)
- [x] FEITO e PROMOVIDO à bin\ em 11/09 06:42 (RELATORIO-PROMOCAO-BIN.md; backup em
      D:\backup-bin-antes-release-20260911-0634\): pulo de quadros com a rede rodando (sem
      cintilação: 0,05-0,5/255), botões do End medidos, Fluidez [−] 60 [+] + Destravar, seção
      única DLSS Frame Skipping, PASSES na primeira vista, tooltips, capivara, HABILITADO
      corrigido, feeder novo validado. Usuário: "gostei do menu End". AGUARDA: teste dele na
      bin\ e o "publica" do site; depois publicar a versão (pacote em preparação).

## Em fila / paralelo (11/09 ~06:50)
- [ ] OTIMIZAR O CAMINHO AMD (11/09 ~08:25): o usuário fez avanço num OUTRO PC (sessão
      "Setup AMD driver v0.2.18" / "Mod DLSS5 para AMD", claude.ai/code/session_016uyXhoLHZhVjQZuKDTg8gB)
      — "funcional, porém pesado". Este PC não tem placa AMD (só integrada) e a transcrição não
      está aqui; o outro PC está desligado. QUANDO ELE LIGAR: reabrir a sessão lá (vira peer
      remoto) ou copiar o .jsonl de ~\.claude\projects\ para cá → retomar o agente amd-otimizar
      (fase 2) com o avanço de lá. Enquanto isso: levantamento local + amd\tests\medir-amd.ps1.
- [x] LEIA-ME/README da release — feitos e promovidos (07:34).
- [ ] PÓS-1.2 (achado 11/09 08:35): VerFaltaDlss() (main.cpp:2218-2232) não confere o
      nvngx_dlss.dll — se faltar sozinho, o DLSS não liga em silêncio; incluir na checagem
      (uma linha) na próxima build. Instalação nova obtém renodx + nvngx pelo instalador RHI
      (kUrlRhi, faixa do seletor) — decisão deliberada, não redistribuir.
- [x] 1.2 PROMOVIDA à produção (08:24): app 852583C5…, bridge 2D951D73…, launcher 77c59e43…,
      docs, bin\marca\. Zip 1.2 em geração (sem publicar). Aguarda "publica" do usuário para
      site + versão juntos.
- [ ] CAIXA DE ATUALIZAÇÃO mais simples (11/09 ~07:50): só "Nova atualização disponível —
      Versão X" + Atualizar agora/Depois + automático — launcher-profissional.
- [ ] SITE /dlss, seção "O que ele faz" com TODAS as utilidades (nuvem, consoles via captura/
      Remote Play, vídeos/filmes, câmera, emuladores/apps sem suporte nativo, dual-GPU, modo
      comparativo…) + "aplicações ilimitadas" — deploy-cloudflare; preview → OK → publicar.
      + VÍDEO de apresentação embutido (youtu.be/cxjIGtErLq0): pt = áudio original + legenda pt;
      en = dublagem em inglês + legenda en (testar se o embed escolhe a dublagem; senão link).
- [ ] Comparativo: rótulos PNG do usuário (artes em amd\marca\comparar-*-original-do-usuario.png,
      normalizar tamanho) + compensação de 1 quadro medida — painel-requiem; depois app 1.2.
- [ ] PAINEL (11/09 ~06:55): botão "Trocar fonte ou placa" que volta ao seletor (tela anterior)
      com o estado pré-selecionado; e quando a janela escolhida fecha, o app volta sozinho ao
      seletor (não fica órfão nem fecha) — painel-requiem.
- [ ] MODO COMPARATIVO em "Mais opções" (11/09 ~07:00, "apenas se não for muito difícil"):
      tela dividida, esquerda original / direita com DLSS 5, para gravar vídeos —
      painel-requiem estima primeiro (restaurar metade esquerda no add-on após o NR).
- [ ] Pacote da próxima versão SEM publicar (publicar-versao.ps1 -Versao X, notas pt/en,
      zip + versao.json testados no servidor falso) — launcher-profissional.
- [ ] RESOLUÇÃO DA REDE NEURAL acima de 100% (125/150/200%: NR em resolução maior que a saída
      e redução para a tela) — residual-nr investiga/implementa no feeder e mede; painel-requiem
      põe os chips depois. Pedido: "seria legal escalar a resolução em até 200%, a da rede neural".

## Em andamento (agente responsável)
- [ ] Ícone novo (capivara; original em amd\assets\icone\capivara-original.png). FEITO no app e
      no launcher (amd\marca\DLSS5ForAll.ico 9 tamanhos, gerar-icone-capivara.ps1, PNG 512/256;
      exe compilados em amd\ e amd\portable-launcher\, ainda NÃO promovidos à bin\). Marca do
      canal (foto do autor) continua no site. SITE: commit local c4c5a8b em programa-dlss5
      (vitrine /app-ofertas + painel /dlss5 + favicon) AGUARDANDO O OK DO USUÁRIO para
      merge/push/deploy — prints preview-icone-*.png em amd\docs\pesquisa-2026-09-11\.
      NOVO (11/09 ~06:00): a página de download JÁ EXISTE em pcdofafa.com.br/dlss (repo
      Documents\dlss5-for-all\site\dlss.html, publicada por site\publicar.ps1 fase "pagina",
      servida pela rota /dlss do encurtador de produção — worker que NÃO se toca). Tarefa do
      deploy-cloudflare: ATUALIZAR essa página (capivara no hero + favicon; "Fafa" sem acento
      nas 16 ocorrências; "agora também AMD, modo experimental"; BAIXAR lendo versao.json;
      recursos alinhados ao LEIA-ME); preview → OK do usuário → publicar.
- [ ] Launcher profissional — B (Faixa) é o PADRÃO do código (LAY_PADRAO, splash_ofertas.h).
      TUDO O QUE ELE PEDIU ATÉ 11/09 ~06:10 ESTÁ FEITO e fotografado (21 fotos em
      amd\docs\pesquisa-2026-09-11\launcher\, relatório atualizado no mesmo diretório):
      [x] capivara no cabeçalho (72 un., recurso 1 do exe, DrawIconEx, recarregada a cada DPI);
      [x] chave "ocultar a faixa" (grava [atualizacao] faixa=0, emite ocultou_faixa/reativou_faixa,
          e com a faixa oculta o polling PARA);
      [x] seletor de placa removido (ordem fixa: NVIDIA pronta > AMD pronta > sem DLSS);
      [x] "AMD experimental" no lugar de "(beta)" + ajuda "Modo AMD em desenvolvimento:
          funciona, mas ainda pesa mais.";
      [x] faixa viva: thread busca o ofertas.json a cada 60 s, compara o SHA-256 e só troca
          quando muda de verdade; animação de ~350 ms (desliza 12 px + fade, rótulo pisca em
          ciano) que respeita SPI_GETCLIENTAREAANIMATION;
      [x] texto por TIPO do item (oferta/cupom/aviso), com rótulo e apoio próprios; "há N min"
          só com carimbo de publicação no JSON; título cortado mostra o inteiro no hover;
      [x] em INGLÊS a faixa vira o convite do canal do YouTube (ofertas.ini canal=), sem aba
          lateral, sem coluna de ofertas, sem cupom e sem polling; troca de idioma vale na hora;
      [x] cartão "Continuar de onde parou"/Repetir REMOVIDO (a janela encolheu 111 px);
      [x] marca "PC do Fafa" sem acento nos arquivos do launcher, e REMOVIDA do vitrine.h a
          função JS que reinjetava o acento a cada render (grep Fafá|FAFÁ = 0).
      Bancada de teste no scratchpad: servidor falso -TrocaS 10 troca a oferta a cada 10 s,
      DLSS5_POLL_MS acelera o polling, filmar-troca.ps1 captura a animação em quadros.
      [x] ITEM 5, atualização puxada pelo versao.json (11/09 ~06:25): o launcher checa na
          abertura E a cada 30 min com a janela aberta (a pílula "atualização disponível"
          aparece sozinha; a caixa continua só depois do INICIAR); a caixa passou a mostrar o
          TAMANHO do download e a data — o publicar-versao.ps1 já gravava tamanho_bytes e o
          launcher é que ignorava. Testado de ponta a ponta com pacote.zip real: aviso →
          "Sim, atualizar" → download → SHA-256 → troca → reabertura, e o versao.txt da raiz
          passou a 1.3. O versao.json é a ÚNICA fonte (o launcher nunca lê a página de
          download); isso e o "como publicar" estão em 5 linhas no LEIA-ME do portable.
      FALTA: o usuário aprovar as fotos (23 em amd\docs\pesquisa-2026-09-11\launcher\) e levar
      à bin\ (eu não promovo).
      Depois: fotos novas, grafia, e levar à `bin\` com aprovação. Relatório em
      `amd\docs\pesquisa-2026-09-11\RELATORIO-LAUNCHER-PROFISSIONAL.md`.
- [ ] Painel End e Resident Evil Requiem — agente `painel-requiem`:
      A) atalhos (End/F6/F7) funcionarem DENTRO de qualquer jogo (hoje no RE Requiem só pela bandeja);
      B) PASSES/MODELO/perfil valerem ao vivo, ou reabertura rápida automática como contorno;
      C) movimento sintetizado é o PADRÃO do pulo de quadros (sem checkbox);
      D) película poder passar do fps do jogo (multiplicador de quadros por warp) + status
         "chegada do jogo vs película" no painel; teto avançado continua mandando;
      E) painel legível: primeira vista só com o essencial, nunca encolher fonte abaixo do
         legível; "Perfil da imagem" (modelo + 6 intensidades) vira JANELA SEPARADA aberta por
         um cartão-resumo com botão "Ajustar" (modelo da janela Efeitos). NOVO (11/09, ~05:30):
         as explicações de "como funciona" saem da primeira vista e viram TOOLTIP flutuante
         que só aparece ao passar o mouse ("reduziria muito a poluição visual"). NOVO (11/09):
         capivara DISCRETA em todos os menus (seletor, painel End, Perfil da imagem, Efeitos,
         cartazes/toasts, botão flutuante) — launcher e bandeja já têm. NOVO (11/09): a seção
         da camada intermitente chama-se "DLSS Frame Skipping" (mesmo rótulo em pt e en) e a
         explicação (pulo de quadros da rede neural do DLSS 5) fica só no tooltip. NOVO (11/09):
         campo Fluidez = "FLUIDEZ DO DLSS 5 NA TELA" com seletor [−] 60 [+] (padrão 60) e
         botão "Destravar" (sem teto).
- [x] Neural Screen, itens 1 e 2 — FEITO no feeder (RELATORIO-RESIDUAL-NR.md, 11/09): rede
      reduzida + residual (`nr_reduzida=0` de fábrica, `nr_escala=65`, `nr_residual=100`),
      +21-34% fps com 4-10 passes e mais nítido que a rede cheia (mas acima de 3-4 passes a
      nitidez inverte); DRED + passthrough seguro + falha dura sem retry; 3 defeitos latentes do
      NR próprio corrigidos. PENDENTE DE DECISÃO DO USUÁRIO (pós-release): só faz efeito com o
      NR PRÓPRIO do feeder (`nr_spike=1` e renodx-dlss.addon64 fora) — hoje a bin\ usa o RenoDX.
      Por isso o interruptor "Rede leve" NÃO entra no painel desta release. O feed recompilado
      (com DRED/passthrough/correções) entra no pacote.
- [ ] Deploy do servidor na Cloudflare — agente `deploy-cloudflare`: worker fafa-dlss5-app,
      dashboard-api, Pages, bridge-page (/app-ofertas), publi inicial, url_base nos dois
      ofertas.ini, merge programa-dlss5→main + push. Token admin salvo em
      Documents\fafa-attribution\workers\app-dlss5\ADMIN-TOKEN.local.txt (nunca no chat).

## ATENÇÃO (11/09 ~14:40): amd\DLSS5ForAll.exe e dlss5-bridge.addon64 na FONTE já são o build 1.3
(motor da rede, 14:34) — MAIS NOVOS que a produção 1.2 (13:22). NÃO rodar montar-portable.ps1 nem
promover sem decisão: a 1.2 publicável é a que está em bin\ (C13671EE…/7B79BDF6…) e no zip cfb465c9….
Feeder fonte (feeder-amd\dlss5-feed-amd.addon64) já sincronizado com a produção; cfg NVIDIA em sincronia
pelo residual-nr.

## AUDITORIA INDEPENDENTE DOS BOTÕES (11/09 ~15:50) — 1.2 EM ESPERA, decisão do usuário
- FALHA: FLUIDEZ entre 70 e 119 não projeta (floor(alvo/chegada)=1) — precisa projeção fracionária.
- NÃO PROVADO: frame skipping "1 de cada 2" — cadeia roda 1 de 2 (provado), mas não se provou que o
  quadro pulado é SINTETIZADO (o dump não alcança) e o contador de present ficou em 30/s.
- PRODUTO: com 1 PASSE (padrão de fábrica) ligar/desligar o DLSS 5 entrega pixels IDÊNTICOS (MAE 0);
  a rede do RenoDX só age acima de 1 passe. Opções: padrão 2 passes e/ou investigar a semântica.
- Menor: chip já selecionado reabre o app à toa; caixa "Imagem Inválida" na tela (pré-existente, origem?).
- PEDIDO NOVO (11/09 ~15:55, revisto ~16:05): REMOVER o botão flutuante (pastilha da capivara)
  de cima do jogo — sem chave para ligar ("muito desnecessário na tela do jogo"). A capivara
  fica só nos menus de configuração; o caminho reserva de teclado (hook) continua.
- Regra: reportar ao usuário antes de qualquer iniciativa sobre botões (feedback gravado).

## ESTADO 11/09 18:00 — TUDO PRONTO, AGUARDANDO "PUBLICA" DO USUÁRIO
Produção final: exe 15A1E4C8…, bridge C40DD21E…, feeder bin 3D6C1964…, launcher 7FD14C6E….
Zip público: publicar\DLSS5ForAll-1.2.zip (ee037a52…, 19,7 MiB, 108 arq.). Zip privado completo:
publicar\privado\DLSS5ForAll-1.2-completo-PRIVADO.zip (f6cd8b4c…, 166,9 MiB). Pasta limpa:
Documents\DLSS5 For All\ (112 arq., com renodx + nvngx + instalador do mod). Publicar = subir-zip
→ publicar-versao -Publicar → página /dlss (publicar.ps1 pagina) → merge/push (deploy-cloudflare).
Aviso ao anúncio: o launcher 1.1 no ar (Drive 04/09) NÃO tem auto-update; quem está na 1.1 baixa pelo site.

## ESTADO 11/09 17:33 — 1.2 FINAL PROMOVIDA (backup D:\backup-bin-antes-1.2-final-1726\):
bin\ e bin-amd\ exe 7FDE5BA7…, bridge C40DD21E…; bin\ feeder novo 3D6C1964… (bin-amd mantém o
feeder AMD 207E173A…); docs com a marca. UI da 1.3 (motor/régua/AMD espera) oculta atrás de
[ultima] recursos_experimentais=1. Auditoria: tudo passa; Fluidez acima do jogo = experimental
(70-73 com alvo 90-120); Frame Skipping compõe com a Fluidez (74-80/s). Zips (público + privado)
em geração; depois: "publica" do usuário (site + versão) e a pasta limpa em Documentos.

## ZIP PRIVADO (pedido 11/09 ~17:25): além do zip público, um zip COMPLETO com as DLLs da NVIDIA
e o RenoDX, só para o backup/teste do usuário (nunca sobe ao servidor) — informar a pasta ao
final. Ordem: tudo correto antes (build final → promoção → conferência) e só então os zips.

## ÚLTIMO PASSO (pedido 11/09 ~14:30) — só depois que TUDO acabar
- [ ] Colocar o PROGRAMA COMPLETO, versão final, numa pasta em Documentos: só os arquivos
      necessários para funcionar, sem lixo (sem logs, .bak, pastas de teste, backups), com toda
      a estrutura pronta (raiz: launcher + LEIA-ME/README + ofertas.ini + versao.txt; bin\;
      bin-amd\; bin\marca\). Fonte = o conteúdo do zip 1.2 final MAIS os componentes que não
      vão no zip público: renodx-dlss.addon64 (RenoDX), nvngx_dlss.dll e nvngx_dlssnr.dll
      (NVIDIA) em bin\, e em bin-amd\ o que o mod gera (version.dll, pesos, nvngx_dlssnr.dll)
      se existir na produção — "claro que dentre os arquivos necessários vão estar o mod do
      RenoDX e da NVIDIA". Cópia COMPLETA dele, funcional de primeira; o zip público segue sem.
      Ele ainda vai verificar as atualizações lá. Sugestão: Documents\DLSS5 For All\.

## Decisões do usuário em 11/09 ~08:55
- [ ] Witcher 3: OK do usuário para renomear o nvngx_dlss.dll nativo — promover-bin executando
      (reversível; backup em D:\backup-witcher3-x64_dx12-20260911\).
- [x] Renomear a nossa dlss5-bridge.addon64: NÃO ("viraria problema se eu quisesse só atualizar
      trocando o arquivo do RenoDX compatível"). Item encerrado.
- [ ] REDE ACIMA DE 100% (125/150/200): "deixa como OPÇÃO" — chips no painel com tooltip de custo,
      só ativos quando o motor da rede for o PRÓPRIO do feeder (nr_spike=1); cinza com motivo
      no RenoDX. Vai junto com o item abaixo (1.3).
- [ ] MELHORIAS DO NEURAL SCREEN EM USO ("as melhorias que chegamos à conclusão seria interessante,
      com base no outro programa"): expor em "Mais opções" o interruptor "Motor da rede: RenoDX /
      Próprio (experimental)" — com Próprio: rede leve (nr_reduzida/nr_escala + residual), DRED/
      passthrough, chips >100%; troca via reabertura automática (nr_spike=1 + NR do RenoDX
      desligado no ReShade.ini). PASSES têm de continuar valendo no motor próprio. Pós-1.2
      (residual-nr + painel-requiem), sem atrasar a publicação.
- [ ] Publicar a primeira versão no servidor (versao.json + zip via amd\publicar-versao.ps1
      -Publicar) quando houver release aprovada (depois do launcher novo e dos fixes do painel).
- [ ] Implementar frame generation por warp acima do fps do jogo em geral (RELATORIO-FRAMEGEN:
      só vale com folga de GPU / monitor ≥100 Hz) — parcialmente coberto pelo item D acima.

## A fazer depois que os agentes fecharem (sem agente ainda)
- [ ] Levar à bin\ (produção) tudo que fechar: recompilar DLSS5ForAll.exe, dlss5-feed.addon64,
      dlss5-bridge.addon64 (com o export SetSharedOutput) e o launcher; só com o app do usuário
      fechado; método "remover antes de copiar" por causa dos hardlinks (RELATORIO-MIGRACAO-BIN.md).
- [ ] QA dos chips novos do teto de fps avançado (entraram depois da bateria de botões).
- [ ] Usuário testar no Hollow Knight o movimento sintetizado com a câmera andando (único
      teste que nenhum agente consegue fazer: o jogo ignora teclado sintético) e ler o
      dlss5-feed.log de lá depois (MV probe com movimento real).
- [ ] Limpar as pastas bin-teste-* (passes, passes-fix, botoes, fpsavancado, robustez,
      sintetizar, requiem, residual, launcher-teste) e os backups antigos, com o usuário de acordo.
- [ ] Atualizar LEIA-ME/README com: PASSES no lugar de 1x/2x/3x, teto de fps avançado, pulo de
      quadros + movimento sintetizado, 3 chaves do NR sem efeito nesta build (NRGlobalTone,
      NREnableUpscaling, NRTransferStrength), limitação do Edge (perfil real para de gerar
      quadros com camadas por cima).

## Herdado da sessão anterior (2026-09-05/06), ainda em aberto
- [ ] RELATORIO-MELHORIAS: botão "reportar problema" que empacota logs; cartão de aviso para
      tela cheia exclusiva; banner completo de arquivos faltando; minidump.
- [ ] Módulo AMD: quando a RX 9060 XT entrar no PC, precisa do mod v0.2.14+ (release pública
      ainda era v0.2.11); baixar pelo navegador do usuário, conferir SHA-256.
- [ ] Vitrine/launcher: tudo que dependia do servidor no ar (contagem, publi, cupom) passa a
      valer após o deploy — conferir com o usuário no painel do fafa-attribution.
- [ ] 3x (terceira camada) fica EM PAUSA até 1x e 2x estarem no melhor resultado (ordem dele).

## Fechado nesta sessão (2026-09-11)
- [x] ReShade.ini de bin-teste-passes revertido pelo ChatGPT → restaurado (preset, paths, docking).
- [x] MV/Depth zerados → não era bug (fonte parada; depth não existe em captura).
- [x] Crash do Witcher 3 → causa: feeder reinicializava NGX em sessão do jogo; fix g.dev12_owned;
      pasta do jogo saneada (dxgi 64 bits, sem duplicatas/dx11-bridge); Hollow Knight validado.
- [x] Botões do painel → duas causas (g_scale/HitTest e monitor de destino), corrigidas; ✕ não
      era bug (DPI do teste). Teto de fps avançado; idioma removido do painel; robustez de janela
      (resolução muda / alvo fecha); modo sintetizar (medido, sem cintilação).
- [x] bin\ migrado para a arquitetura nova (backup em D:\backup-bin-antes-migracao-20260911\).
- [x] Estudo do Neural Screen (RELATORIO-NEURAL-SCREEN.md).
