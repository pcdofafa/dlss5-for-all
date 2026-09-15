; =====================================================================================
;  instalador-completo.iss  -  instalador do PACOTE COMPLETO do DLSS5 For All (Inno 6)
;
;  NAO compile este arquivo direto: compile com
;      powershell -ExecutionPolicy Bypass -File .\gerar-instalador-completo.ps1
;
;  QUAL A DIFERENCA PARA O instalador.iss
;    instalador.iss           -> o pacote publicavel: so o que e nosso, instala e BAIXA a
;                                cadeia (ReShade, shaders, dxgi.dll) no fim. A pessoa ainda
;                                precisa buscar por fora os dois arquivos do DLSS 5.
;    instalador-completo.iss  -> este: instala em "Arquivos de Programas\DLSS for All" e,
;                                logo depois de copiar, roda o "dlss5 for all setup.exe"
;                                (auto-extraivel WinRAR com a pasta INTEIRA: 7924 arquivos,
;                                ~812 MB) POR CIMA, na MESMA pasta. Nada para baixar e nada
;                                para buscar: a cadeia e os dois arquivos do DLSS 5 ja vem
;                                dentro.
;
;  O "MESMO ENDERECO" DO REQUISITO
;    A pagina de pasta continua habilitada: quem quiser trocar de C:\Arquivos de
;    Programas\DLSS for All para D:\Jogos\DLSS pode. O que NAO pode e o auto-extraivel ir
;    parar em outro lugar que nao esse. Ele recebe -d"{app}", entao segue a pasta escolhida
;    seja ela qual for. E o -d que garante que o destino do extrator nunca divirja do
;    destino do instalador.
;
;  =============  O "-s", E POR QUE ELE NAO PODE VIRAR "-s2"  =============
;    ESTE E O PONTO QUE JA CUSTOU UMA VERSAO. O modulo SFX do WinRAR tem tres niveis:
;       (sem switch)  mostra a janela de abertura E pergunta o que fazer com cada arquivo
;                     que ja existe;
;       -s2           esconde SO a janela de abertura. A caixa "confirmar substituicao"
;                     CONTINUA aparecendo - e ela aparece sempre, porque a etapa anterior
;                     (o [Files] deste instalador) acabou de gravar na pasta os arquivos
;                     que o extrator vai reescrever. O sintoma e uma janela "WinRAR
;                     self-extracting archive" parada no meio da instalacao, esperando um
;                     "Yes to all". Medido: com -s2 o processo fica travado ate alguem
;                     clicar.
;       -s            esconde tudo e sobrescreve sem perguntar. Medido: 7924 arquivos,
;                     ~25 s, codigo 0, com a pasta ja populada.
;    O requisito e "sem nenhum clique extra", entao e -s SEMPRE - inclusive na instalacao
;    normal, nao so na silenciosa. O preco e nao existir barra de progresso do WinRAR; quem
;    cobre isso e a barra do proprio Inno, posta em marquee no [Code].
;
;    (Nota para quem for testar: o "-o+" que acompanha e switch do RAR de console e o
;    modulo SFX o ignora. Ele nao atrapalha e fica so como documentacao da intencao - quem
;    sobrescreve de fato e o -s. E teste SEMPRE com a pasta ja populada: com pasta vazia
;    nao existe conflito, e qualquer combinacao de switches parece funcionar.)
;
;  POR QUE ESTE INSTALA EM PROGRAM FILES E POR QUE A PASTA FICA GRAVAVEL
;    O app e o ReShade gravam ReShade.ini, FafaDLSS5.ini, dlss5-feed.cfg, os logs e o
;    reshade-cache\ AO LADO DO EXE - nao e escolha nossa, e como o ReShade funciona. Em
;    Program Files, sem a [Dirs] mais abaixo, essas gravacoes seriam negadas: o app e o
;    ReShade sao 64 bits, e a virtualizacao do UAC (o %LOCALAPPDATA%\VirtualStore) so vale
;    para processos de 32 bits. O sintoma seria "ajustei e ao reabrir voltou tudo", que
;    ninguem liga a permissao de pasta. Ler o bloco de [Dirs] antes de mexer em
;    PrivilegesRequired, DefaultDirName ou nas permissoes: os tres andam juntos.
; =====================================================================================

#ifndef Staging
  #error Compile pelo gerar-instalador-completo.ps1 (ele monta a staging com o empacotar.ps1 e passa /DStaging=...).
#endif
#ifndef Sfx
  #error Falta /DSfx=<caminho do "dlss5 for all setup.exe">. O gerar-instalador-completo.ps1 passa isso.
#endif
#ifndef Versao
  #define Versao "0.0.0"
#endif
#ifndef VersaoNum
  #define VersaoNum "0.0.0.0"
#endif
#ifndef SaidaDir
  #define SaidaDir "."
#endif
#ifndef SaidaNome
  #define SaidaNome "DLSS5ForAll-completo-setup"
#endif
#ifndef Creditos
  #define Creditos "CREDITOS.md"
#endif
; Quanto o pacote ocupa DEPOIS de extraido. O Inno so sabe somar o que ele proprio copia (a
; staging, alguns MB, mais o SFX ainda comprimido): sem esta linha ele anunciaria "requer
; 320 MB" para uma instalacao que ocupa quase 1 GB, e quem estivesse no limite do disco
; descobriria isso no meio da extracao, com a pasta pela metade. O gerador mede e passa.
#ifndef EspacoExtra
  #define EspacoExtra "900000000"
#endif

#define MeuApp     "DLSS5 For All"
#define MeuAutor   "PC do Fafa"
#define MeuExe     "DLSS5ForAll.exe"
#define NomeSfx    "dlss5 for all setup.exe"

[Setup]
; AppId PROPRIO, diferente do instalador.iss, de proposito: os dois sao produtos distintos
; (um baixa a cadeia, o outro traz tudo). Com o mesmo GUID o Windows trataria um como
; "atualizacao" do outro e o UsePreviousAppDir arrastaria a pasta do primeiro. FIXO e para
; sempre: trocar depois de publicar deixa desinstalador orfao na maquina de quem ja
; instalou.
AppId={{3E9C6D47-58B1-4A2F-9C10-8D74B2E6F05A}
AppName={#MeuApp}
AppVersion={#Versao}
AppVerName={#MeuApp} {#Versao}
AppPublisher={#MeuAutor}
AppCopyright=Copyright (c) 2026 {#MeuAutor}
VersionInfoVersion={#VersaoNum}
VersionInfoCompany={#MeuAutor}
VersionInfoDescription=Instalador do {#MeuApp} (pacote completo)

; --- Program Files exige elevacao. Ver o cabecalho da [Dirs]: e ela que impede as
;     configuracoes de sumirem depois.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=
; "DLSS for All", sem o 5: e o nome da pasta que ja esta em uso. O nome do PROGRAMA nas
; telas, nos atalhos e no Menu Iniciar e outro - "DLSS5 For All", com o 5 - e e assim de
; proposito: um e produto, o outro e pasta.
DefaultDirName={commonpf}\DLSS for All
; A pasta continua escolhivel; o auto-extraivel segue ela via -d"{app}" (ver [Code]).
; "no" (e nao "auto") para que de para MUDAR de pasta ao reinstalar.
DisableDirPage=no
UsePreviousAppDir=yes
DefaultGroupName={#MeuApp}
DisableProgramGroupPage=yes
ExtraDiskSpaceRequired={#EspacoExtra}

; --- requisitos reais do app: x64 e Windows 10 1903 (build 18362), a versao em que a
; Windows Graphics Capture ganhou a captura de janela/monitor que o app usa.
; ArchitecturesInstallIn64BitMode e o que faz {commonpf} ser "C:\Arquivos de Programas" e
; nao "...(x86)": o instalador roda como processo 32 bits e e ele quem expande a constante.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362

; --- se o app estiver aberto, o instalador pede para fechar em vez de falhar copiando o
; exe. Vale duas vezes aqui: o auto-extraivel tambem regrava o DLSS5ForAll.exe, e um exe em
; uso o faria falhar depois, ja fora do alcance desta protecao. O nome do mutex vem do
; proprio main.cpp (DLSS5_MUTEX_INSTANCIA).
AppMutex=Local\DLSS5ForAll.instancia
CloseApplications=yes
RestartApplications=no

LicenseFile={#Staging}\LICENSE
InfoBeforeFile={#Creditos}
; SEM InfoAfterFile, de proposito. O instalador.iss tem um, avisando que faltam o
; renodx-dlss5.addon64 e o nvngx_dlssnr.dll; neste pacote os dois VEM DENTRO do
; auto-extraivel (conferido no conteudo dele), entao a tela seria uma mentira e um clique
; a mais.

SetupIconFile=marca\DLSS5ForAll.ico
UninstallDisplayIcon={app}\DLSS5ForAll.ico
UninstallDisplayName={#MeuApp} (pacote completo)

; --- SELETOR DE IDIOMA: portugues e ingles, SEMPRE perguntado.
; "yes" e nao "auto": o auto so pergunta quando nao consegue deduzir pelo idioma do
; Windows, ou seja, quase nunca aparecia - com o Windows em pt-BR ninguem conseguia
; escolher ingles. UsePreviousLanguage=no para a pergunta continuar valendo tambem numa
; reinstalacao; senao o Inno reaproveita a escolha antiga e o seletor some da segunda vez
; em diante.
ShowLanguageDialog=yes
UsePreviousLanguage=no

WizardStyle=modern
WizardSmallImageFile=marca\icone-png\dlss5forall-128.png
; O grosso do peso e o "dlss5 for all setup.exe", que ja e um RAR comprimido: recomprimir
; 302 MB de dados comprimidos gasta minutos de compilacao para economizar quase nada (por
; isso o nocompression na entrada dele, no [Files]). O lzma2/max vale para o resto.
Compression=lzma2/max
SolidCompression=yes
OutputDir={#SaidaDir}
OutputBaseFilename={#SaidaNome}

[Languages]
Name: "br"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
br.ExtraindoPacote=Aplicando o pacote completo na pasta instalada (pode levar alguns minutos)...
en.ExtraindoPacote=Applying the complete package to the installed folder (this can take a few minutes)...
br.ExecutarApp=Executar o {#MeuApp} agora
en.ExecutarApp=Run {#MeuApp} now
br.AtalhoPasta=Abrir a pasta do DLSS5 For All
en.AtalhoPasta=Open the DLSS5 For All folder
br.AtalhoLeiame=Leia-me
en.AtalhoLeiame=Read me
br.AtalhoDesinstalar=Desinstalar o DLSS5 For All
en.AtalhoDesinstalar=Uninstall DLSS5 For All
br.AtalhoConfigurar=Reconfigurar a cadeia (rebaixa ReShade e componentes)
en.AtalhoConfigurar=Re-run chain setup (re-downloads ReShade and components)
br.CaixaConfigurar=Rebaixar a cadeia (só é preciso se algo do ReShade tiver quebrado)
en.CaixaConfigurar=Re-download the chain (only needed if something in ReShade broke)
br.CaixaPasta=Abrir a pasta do DLSS5 For All
en.CaixaPasta=Open the DLSS5 For All folder
br.FalhouSfx=Não foi possível aplicar o pacote completo (o extrator terminou com o código %1).%n%nO DLSS5 For All foi instalado, mas sem os componentes que vêm nesse pacote — entre eles o ReShade e os dois arquivos que ligam o DLSS 5. Rode o "dlss5 for all setup.exe" à mão, apontando o destino para:%n%n%2
en.FalhouSfx=The complete package could not be applied (the extractor exited with code %1).%n%nDLSS5 For All was installed, but without the components that come in that package — including ReShade and the two files that turn DLSS 5 on. Run "dlss5 for all setup.exe" by hand, pointing its destination at:%n%n%2
br.PerguntaLimparTudo=Remover também tudo o que sobrou na pasta abaixo?%n%n%1%n%nSão os arquivos que o pacote completo gravou (componentes, shaders, documentação) mais as suas configurações e logs. Escolha Não para deixar a pasta como está.
en.PerguntaLimparTudo=Also remove everything left in the folder below?%n%n%1%n%nThese are the files the complete package wrote (components, shaders, documentation) plus your settings and logs. Choose No to leave the folder as it is.

[Files]
; 1. a staging do empacotar.ps1 - a MESMA lista de arquivos do zip e do instalador enxuto.
;    Ela entra primeiro por dois motivos: o desinstalador passa a conhecer o nucleo do app
;    arquivo por arquivo, e se o auto-extraivel falhar a pasta ainda tem um app que abre.
;    O auto-extraivel do [Code] passa por cima logo em seguida - e e por causa desta etapa
;    que a pasta NUNCA esta vazia quando ele roda, o que torna o "-s" obrigatorio.
Source: "{#Staging}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "dlss5forall.ini,ReShade.ini,FafaDLSS5.ini"

; 2. os tres arquivos de configuracao, com onlyifdoesntexist: numa reinstalacao quem manda
;    e o que a pessoa ajustou (a tecla do overlay, os sliders, o preset em uso).
;    uninsneveruninstall: na desinstalacao eles nao somem junto - ver [Code].
;    (O auto-extraivel, logo depois, sobrescreve os tres com os do autor: e o comportamento
;    pedido para este pacote. Estas flags valem para o caso em que o extrator nao roda.)
Source: "{#Staging}\dlss5forall.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#Staging}\ReShade.ini";     DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#Staging}\FafaDLSS5.ini";   DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

; 3. o que e do instalador e nao do zip.
;    O .ico vai solto de proposito: os atalhos apontam para ELE, e nao para o icone de
;    dentro do exe, para que o icone certo apareca mesmo quando o exe da pasta for um build
;    anterior ao icone.
Source: "marca\DLSS5ForAll.ico";            DestDir: "{app}"; Flags: ignoreversion
Source: "instalador\configurar-cadeia.cmd"; DestDir: "{app}"; Flags: ignoreversion

; 4. o auto-extraivel, que o [Code] dispara logo depois de tudo isto ser copiado.
;    nocompression: ja e um RAR, recomprimir so gasta tempo de compilacao.
;    {tmp} e nao {app}: se ele ficasse na pasta do programa, alguem rerodaria o
;      auto-extraivel por engano achando que e o instalador - e sobrariam 302 MB
;      permanentes dentro de Arquivos de Programas. deleteafterinstall limpa o {tmp}.
Source: "{#Sfx}"; DestDir: "{tmp}"; DestName: "{#NomeSfx}"; Flags: nocompression deleteafterinstall

[Dirs]
; =====================================================================================
;  A PASTA INSTALADA E GRAVAVEL PELO USUARIO COMUM. ISTO E DELIBERADO, NAO DESCUIDO.
;
;  O QUE OBRIGA: esta cadeia guarda tudo AO LADO DO EXE. Gravam na pasta, em uso normal,
;  o ReShade (ReShade.ini a cada sessao, reshade-cache\ a cada troca de preset), o preset
;  (FafaDLSS5.ini), o add-on do feed (dlss5-feed.cfg), o app (dlss5forall.ini) e os logs
;  dos tres.
;
;  POR QUE NAO DA PARA CONTAR COM A VIRTUALIZACAO DO UAC: ela so vale para processos de
;  32 bits. O app e o ReShade sao 64 bits - uma escrita negada e negada, sem
;  redirecionamento e, pior, o ReShade nao mostra erro.
;
;  O CUSTO, NA CARA: uma pasta gravavel dentro de Program Files e MENOS SEGURA que o
;  padrao. Qualquer processo rodando com a conta do usuario - inclusive um malware sem
;  privilegio nenhum - pode substituir o DLSS5ForAll.exe, o dxgi.dll ou os add-ons daqui.
;  O que limita o dano e que nada neste conjunto roda elevado nem como servico: quem abrir
;  o exe trocado tera os privilegios de quem clicou, que e o mesmo que o atacante ja tinha.
;  Nao ha ganho de privilegio; ha risco de persistencia. A alternativa seria instalar em
;  %LOCALAPPDATA%, onde essa escrita e o padrao do sistema - foi a decisao anterior, e
;  Arquivos de Programas e o que foi pedido. Esta e a conta que vem junto.
;
;  users-modify = criar, ler, alterar e apagar. NAO inclui trocar o dono nem as permissoes.
;  Os arquivos que o auto-extraivel criar depois herdam esta permissao da pasta.
; =====================================================================================
Name: "{app}";                          Permissions: users-modify
; As subpastas herdam a ACE de {app}, mas estas tres sao as que mais escrevem e podem ser
; recriadas depois pelo proprio ReShade: declara-las aqui garante que nascam gravaveis.
Name: "{app}\reshade-cache";            Permissions: users-modify
Name: "{app}\reshade-shaders\Shaders";  Permissions: users-modify
Name: "{app}\reshade-shaders\Textures"; Permissions: users-modify

[Icons]
; Area de trabalho e Menu Iniciar, os dois OBRIGATORIOS neste instalador: nao ha [Tasks]
; com caixa para desmarcar, foi pedido assim.
;
; {commondesktop} e nao {userdesktop}: a instalacao e elevada e vale para a maquina toda.
; Com {userdesktop} o atalho iria para a area de trabalho da conta de administrador que
; elevou o instalador - que pode nem ser a conta de quem vai usar o programa, e ai o atalho
; simplesmente "nao aparece".
Name: "{commondesktop}\{#MeuApp}";      Filename: "{app}\{#MeuExe}"; WorkingDir: "{app}"; IconFilename: "{app}\DLSS5ForAll.ico"
Name: "{group}\{#MeuApp}";              Filename: "{app}\{#MeuExe}"; WorkingDir: "{app}"; IconFilename: "{app}\DLSS5ForAll.ico"
Name: "{group}\{cm:AtalhoConfigurar}";  Filename: "{app}\configurar-cadeia.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\DLSS5ForAll.ico"
Name: "{group}\{cm:AtalhoPasta}";       Filename: "{app}"
Name: "{group}\{cm:AtalhoLeiame}";      Filename: "{win}\notepad.exe"; Parameters: """{app}\README.md"""; IconFilename: "{app}\DLSS5ForAll.ico"
Name: "{group}\{cm:AtalhoDesinstalar}"; Filename: "{uninstallexe}"

[Run]
; A ORDEM DAQUI E A ORDEM DAS CAIXAS NA PAGINA DE CONCLUSAO, e ela e o inverso da do
; instalador.iss por um motivo: la a cadeia AINDA PRECISAVA ser baixada, entao "Configurar
; a cadeia" era a primeira caixa e vinha marcada. Neste pacote a cadeia ja veio pronta
; dentro do auto-extraivel - rebaixar tudo seria refazer do zero o que acabou de ser
; instalado. Entao a primeira caixa, e a unica marcada, e simplesmente abrir o programa.
;
; runasoriginaluser: o instalador roda elevado e, sem isso, o DLSS5 For All herdaria a
; elevacao. Um app grafico como administrador nao recebe arrastar-e-soltar do Explorer e
; grava seus inis como administrador, estragando justamente a permissao que a [Dirs]
; acabou de ajustar. Ele tem de abrir como usuario comum, igual ao atalho.
Filename: "{app}\{#MeuExe}"; WorkingDir: "{app}"; Description: "{cm:ExecutarApp}"; Flags: postinstall nowait skipifsilent runasoriginaluser
Filename: "{app}\configurar-cadeia.cmd"; WorkingDir: "{app}"; Description: "{cm:CaixaConfigurar}"; Flags: postinstall shellexec nowait skipifsilent unchecked
Filename: "{app}"; Description: "{cm:CaixaPasta}"; Flags: postinstall shellexec nowait skipifsilent unchecked

[Code]

// ---------------------------------------------------------------------------------
//  O PACOTE COMPLETO POR CIMA
//
//  POR QUE AQUI E NAO NUMA ENTRADA [Run]. Uma entrada [Run] roda o programa e ignora o
//  que ele devolve: se o auto-extraivel falhasse (disco cheio, antivirus segurando o
//  arquivo, RAR corrompido), o Inno seguiria em frente e mostraria "instalado com
//  sucesso" para uma pasta sem os componentes - o pior desfecho possivel, porque so
//  aparece muito depois, como "o programa nao liga o DLSS". Chamado daqui, o Exec
//  devolve o codigo de saida e da para dizer o que aconteceu na hora.
//
//  ssPostInstall: depois de os arquivos estarem copiados (e o [Files] que traz o proprio
//  auto-extraivel para o {tmp}) e antes da pagina de conclusao.
//
//  OS PARAMETROS
//    -s         esconde a janela do WinRAR E sobrescreve sem perguntar. NAO TROCAR POR
//               -s2: ver o bloco no cabecalho deste arquivo. O -s2 esconde so a janela de
//               abertura e deixa a caixa de confirmar substituicao aparecer no meio da
//               instalacao - que e exatamente o clique que este passo existe para evitar.
//               Nao ha "if WizardSilent" aqui de proposito: instalacao normal e silenciosa
//               usam o mesmo -s, porque nas duas o requisito e o mesmo (nenhum clique).
//    -o+        switch do RAR de console; o modulo SFX o ignora. Fica como documentacao da
//               intencao - quem sobrescreve de fato e o -s.
//    -y         responde "sim" a qualquer pergunta que ainda sobre.
//    -d"{app}"  manda o conteudo para a pasta escolhida no assistente. E o que garante que
//               o extrator nunca divirja do destino do instalador, inclusive quando a
//               pessoa troca a pasta.
//
//  A barra do proprio Inno vira marquee (aquela que so vai e volta) enquanto isso, e e a
//  UNICA indicacao de progresso que sobra: com -s o WinRAR nao mostra barra nenhuma. Sao
//  ~25 s de extracao medidos aqui (7924 arquivos), mais em disco lento.
//
//  Se falhar, o aviso e so um aviso e nao um abort: o nucleo do app JA foi copiado pela
//  staging e ja abre; abortar desfaria uma instalacao que serve. O que falta e o extra, e
//  a mensagem diz onde reaplica-lo. SuppressibleMsgBox e nao MsgBox porque numa
//  instalacao silenciosa o MsgBox comum APARECE e trava tudo esperando um clique que
//  ninguem vai dar.
// ---------------------------------------------------------------------------------
procedure CurStepChanged(CurStep: TSetupStep);
var
  Params: String;
  Codigo: Integer;
  Rodou: Boolean;
begin
  if CurStep <> ssPostInstall then Exit;

  Params := '-s -o+ -y -d"' + ExpandConstant('{app}') + '"';

  WizardForm.StatusLabel.Caption := CustomMessage('ExtraindoPacote');
  WizardForm.ProgressGauge.Style := npbstMarquee;
  try
    Rodou := Exec(ExpandConstant('{tmp}\{#NomeSfx}'), Params, ExpandConstant('{tmp}'),
                  SW_HIDE, ewWaitUntilTerminated, Codigo);
  finally
    WizardForm.ProgressGauge.Style := npbstNormal;
  end;

  if (not Rodou) or (Codigo <> 0) then
  begin
    if not Rodou then Codigo := -1;
    // A lista de argumentos fica na MESMA linha do FmtMessage de proposito: o Inno decide
    // o que e cabecalho de secao olhando o primeiro caractere nao-branco da linha, entao
    // uma quebra que deixasse o "[" comecando a linha viraria "Invalid section tag".
    SuppressibleMsgBox(FmtMessage(CustomMessage('FalhouSfx'), [IntToStr(Codigo), ExpandConstant('{app}')]),
                       mbError, MB_OK, IDOK);
  end;
end;

// ---------------------------------------------------------------------------------
// O QUE A DESINSTALACAO FAZ COM O QUE O AUTO-EXTRAIVEL DEIXOU
//
// O Inno so sabe apagar o que ELE copiou - ou seja, a staging. Os 7924 arquivos que o
// "dlss5 for all setup.exe" extraiu depois nao estao no log de desinstalacao e ficariam
// para tras: quase 1 GB dentro de Arquivos de Programas, invisivel para quem desinstalou
// pelo painel do Windows e achou que tinha acabado.
//
// Por isso a pergunta aqui e o contrario da do instalador.iss (que preservava por padrao):
// naquele, o que sobrava a pessoa tinha trazido ou baixado por conta propria. Neste, tudo
// o que sobra veio deste mesmo instalador - inclusive o nvngx_dlssnr.dll e o
// renodx-dlss5.addon64, que vieram dentro do pacote - entao o padrao e SIM, remover. Quem
// quiser guardar as configuracoes ou os componentes responde Nao e a pasta fica intacta.
// ---------------------------------------------------------------------------------
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App: String;
begin
  if CurUninstallStep <> usPostUninstall then Exit;
  App := ExpandConstant('{app}');
  if not DirExists(App) then Exit;

  // RemoveDir so apaga pasta vazia: se o Inno ja tiver levado tudo, some sem perguntar.
  if RemoveDir(App) then Exit;

  if SuppressibleMsgBox(FmtMessage(CustomMessage('PerguntaLimparTudo'), [App]),
                        mbConfirmation, MB_YESNO or MB_DEFBUTTON1, IDYES) = IDYES then
    DelTree(App, True, True, True);
end;
