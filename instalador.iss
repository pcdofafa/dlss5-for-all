; =====================================================================================
;  instalador.iss  -  instalador do DLSS5 For All (Inno Setup 6)
;
;  NAO compile este arquivo direto: compile com
;      powershell -ExecutionPolicy Bypass -File .\gerar-instalador.ps1
;  Ele chama o empacotar.ps1 -SomenteStaging, monta a pasta de distribuicao e passa o
;  caminho dela aqui em /DStaging=... O #error logo abaixo garante isso.
;
;  POR QUE A LISTA DE ARQUIVOS NAO ESTA AQUI
;  O que entra no pacote e o que NAO pode ser redistribuido (nvngx_dlssnr.dll e
;  renodx-dlss5.addon64, que sao codigo vazado; nvngx_dlss.dll, que e da NVIDIA; dxgi.dll,
;  que o setup baixa) e decisao do empacotar.ps1, com o porque escrito la, arquivo por
;  arquivo. Duas listas divergiriam no primeiro arquivo novo e alguem publicaria um pacote
;  errado. Entao aqui e "copie a staging inteira" e mais nada.
;
;  INSTALA EM PROGRAM FILES, E A PASTA E GRAVAVEL DE PROPOSITO
;  Ler o bloco de [Dirs] antes de mexer em PrivilegesRequired, DefaultDirName ou nas
;  permissoes: os tres andam juntos e mexer em um sozinho quebra o app em silencio.
; =====================================================================================

#ifndef Staging
  #error Compile pelo gerar-instalador.ps1 (ele monta a staging com o empacotar.ps1 e passa /DStaging=...).
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
  #define SaidaNome "DLSS5ForAll-setup"
#endif
#ifndef Creditos
  #define Creditos "CREDITOS.md"
#endif

#define MeuApp     "DLSS5 For All"
#define MeuAutor   "PC do Fafa"
#define MeuExe     "DLSS5ForAll.exe"

[Setup]
; AppId FIXO e para sempre: e por ele que o Windows sabe que uma instalacao nova SUBSTITUI
; a anterior em vez de criar uma segunda entrada em "Aplicativos instalados". Trocar este
; GUID depois de publicar deixa desinstaladores orfaos na maquina de quem ja instalou.
AppId={{7B2F4C1A-9D53-4E88-A6C0-3F5E1D9B7A24}
AppName={#MeuApp}
AppVersion={#Versao}
AppVerName={#MeuApp} {#Versao}
AppPublisher={#MeuAutor}
AppCopyright=Copyright (c) 2026 {#MeuAutor}
VersionInfoVersion={#VersaoNum}
VersionInfoCompany={#MeuAutor}
VersionInfoDescription=Instalador do {#MeuApp}

; --- instalacao normal, para todos os usuarios, com elevacao.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=
DefaultDirName={autopf}\DLSS5 For All
; a pasta e escolhivel de proposito: quem quiser em D:\Jogos\... pode.
; "no" (e nao "auto") para que dê para MUDAR de pasta ao reinstalar.
DisableDirPage=no
DefaultGroupName={#MeuApp}
DisableProgramGroupPage=yes
UsePreviousAppDir=yes

; --- requisitos reais do app: x64 e Windows 10 1903 (build 18362), a versao em que a
; Windows Graphics Capture ganhou a captura de janela/monitor que o app usa.
ArchitecturesAllowed=x64compatible
; SEM esta linha, {autopf} resolveria para "C:\Program Files (x86)" - o instalador roda
; como processo 32 bits e e ele quem expande a constante. O app e 64 bits; ir parar na
; pasta dos 32 e so confusao para quem for procurar os arquivos.
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.18362

; --- se o app estiver aberto, o instalador pede para fechar em vez de falhar copiando o
; exe. O nome vem do proprio main.cpp (DLSS5_MUTEX_INSTANCIA).
AppMutex=Local\DLSS5ForAll.instancia
CloseApplications=yes
RestartApplications=no

; --- licenca e creditos na cara de quem instala (o CREDITOS deixa claro o que e de
; terceiros e o que a pessoa traz por conta propria).
LicenseFile={#Staging}\LICENSE
InfoBeforeFile={#Creditos}

; --- a tela do fim, sobre os dois arquivos que NAO vem no pacote.
;
; POR QUE ELA EXISTE. Sem o renodx-dlss5.addon64 e o nvngx_dlssnr.dll o app abre, mostra a
; imagem e nao liga o DLSS 5 -- e o sintoma que a pessoa vê e "instalei e ficou igual",
; que le como "o programa nao funciona". A informacao existe no CREDITOS.md, mas ninguem a
; procura antes de concluir a instalacao. InfoAfterFile entra na sequencia DEPOIS de
; instalar e ANTES da pagina de conclusao: a tela explica, e a proxima oferece a acao.
; O arquivo e curto de proposito -- quem esta clicando "Concluir" nao le paragrafo longo -
; e o app repete o aviso na hora que importa (o launcher detecta a falta dos dois).
; Ele e UTF-8 COM BOM, como o .iss: sem o BOM o Inno le como ANSI e os acentos quebram.
InfoAfterFile=instalador\aviso-neural.txt

SetupIconFile=marca\DLSS5ForAll.ico
UninstallDisplayIcon={app}\DLSS5ForAll.ico
UninstallDisplayName={#MeuApp}
; O padrao do Inno com mais de um idioma e ABRIR uma janela "Selecione o Idioma" antes de
; tudo - um passo a mais, em ingles, para quem esta em portugues (medido: ela aparecia).
; "auto" so pergunta quando nao da para deduzir; com o Windows em pt-BR o assistente ja
; comeca em portugues, e quem estiver com o Windows em outro idioma cai no ingles.
ShowLanguageDialog=auto
LanguageDetectionMethod=uilanguage
WizardStyle=modern
WizardSmallImageFile=marca\icone-png\dlss5forall-128.png
Compression=lzma2/max
SolidCompression=yes
OutputDir={#SaidaDir}
OutputBaseFilename={#SaidaNome}

[Languages]
Name: "br"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
br.ConfigurarAgora=Baixar e configurar a cadeia agora (precisa de internet)
en.ConfigurarAgora=Download and set up the chain now (needs internet)
br.AtalhoConfigurar=Configurar a cadeia (baixa ReShade e componentes)
en.AtalhoConfigurar=Set up the chain (downloads ReShade and components)
br.AtalhoPasta=Abrir a pasta do DLSS5 For All
en.AtalhoPasta=Open the DLSS5 For All folder
br.AbrirRHI=Abrir a página onde pegar os dois arquivos do DLSS 5 (instalador RHI)
en.AbrirRHI=Open the page to get the two DLSS 5 files (RHI installer)
br.CaixaPasta=Abrir a pasta do DLSS5 For All (é para cá que os dois arquivos vão)
en.CaixaPasta=Open the DLSS5 For All folder (this is where the two files go)
br.AtalhoLeiame=Leia-me
en.AtalhoLeiame=Read me
; A pergunta da desinstalacao. O que ela oferece apagar e SO o que se recupera rodando o
; setup de novo; os arquivos que a pessoa trouxe ficam de fora dela de proposito.
br.PerguntaLimpar=Remover também as configurações, os logs e os componentes que o setup baixou (ReShade, shaders, cache)?%n%nEscolha Não para manter tudo — reinstalar em cima fica pronto na hora, sem baixar nada de novo.%n%nSeja qual for a resposta, os arquivos que você trouxe por conta própria (nvngx_dlssnr.dll, renodx-dlss5.addon64 e nvngx_dlss.dll) NÃO são apagados.
en.PerguntaLimpar=Also remove your settings, the logs and the components the setup downloaded (ReShade, shaders, cache)?%n%nChoose No to keep everything — reinstalling on top will be ready right away, with nothing to download again.%n%nEither way, the files you brought yourself (nvngx_dlssnr.dll, renodx-dlss5.addon64 and nvngx_dlss.dll) are NOT deleted.
br.AvisoSobrou=Estes arquivos continuam em:%n%n%1%n%n%2%n%nSão código da NVIDIA / RenoDX que você obteve por conta própria — apagar é decisão sua, e reinstalar o DLSS5 For All nessa mesma pasta reaproveita eles.
en.AvisoSobrou=These files are still in:%n%n%1%n%n%2%n%nThey are NVIDIA / RenoDX code you obtained yourself — deleting them is your call, and reinstalling DLSS5 For All into the same folder will reuse them.

[Dirs]
; =====================================================================================
;  A PASTA INSTALADA E GRAVAVEL PELO USUARIO COMUM. ISTO E DELIBERADO, NAO DESCUIDO.
;
;  O QUE OBRIGA:
;  Esta cadeia guarda tudo AO LADO DO EXE, e nao em %APPDATA% - nao e escolha nossa, e
;  como o ReShade funciona. Gravam na pasta, em uso normal:
;     ReShade.ini      o ReShade reescreve a cada sessao (AutoSave)
;     FafaDLSS5.ini    o preset, idem
;     reshade-cache\   os shaders compilados, reescritos quando o preset muda
;     dlss5-feed.cfg   o add-on do feed
;     dlss5forall.ini  o app (fonte escolhida, atalhos)
;     *.log            app, feeder e ReShade
;  E, depois de instalar, a PESSOA deposita aqui os dois arquivos que este projeto nao
;  redistribui - nvngx_dlssnr.dll e renodx-dlss5.addon64 - ou o instalador RHI os deposita
;  por ela.
;
;  POR QUE NAO DA PARA CONTAR COM A VIRTUALIZACAO DO UAC:
;  Ela so vale para processos de 32 bits. O app, o ReShade e o RHI sao 64 bits: uma escrita
;  negada e negada, sem redirecionamento e - pior - o ReShade nao mostra erro. O sintoma
;  seria "ajustei e ao reabrir voltou tudo", que ninguem liga a permissao de pasta.
;
;  O CUSTO, NA CARA:
;  Uma pasta gravavel dentro de Program Files e MENOS SEGURA que o padrao. Qualquer
;  processo rodando com a conta do usuario - inclusive um malware sem privilegio nenhum -
;  pode substituir o DLSS5ForAll.exe, o dxgi.dll ou os add-ons daqui. O que limita o dano e
;  que nada neste conjunto roda elevado nem como servico: quem abrir o exe trocado tera os
;  privilegios de quem clicou, que e o mesmo que o atacante ja tinha. Nao ha ganho de
;  privilegio, ha risco de persistencia.
;  A alternativa seria instalar em %LOCALAPPDATA%, onde essa mesma escrita e o padrao do
;  sistema e nao precisa de ACL nenhuma. Foi a decisao anterior; o usuario preferiu
;  "programa normal, em Arquivos de Programas", e esta e a conta que vem junto.
;
;  users-modify = criar, ler, alterar e apagar. NAO inclui trocar o dono nem as permissoes.
; =====================================================================================
Name: "{app}";                          Permissions: users-modify
; As subpastas herdam a ACE de {app}, mas o reshade-cache e criado pelo ReShade DEPOIS da
; instalacao e e o que mais escreve: declara-lo aqui garante que ele nasca ja gravavel e
; poupa o caso em que a heranca nao pegou.
Name: "{app}\reshade-cache";            Permissions: users-modify
Name: "{app}\reshade-shaders\Shaders";  Permissions: users-modify
Name: "{app}\reshade-shaders\Textures"; Permissions: users-modify

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; 1. a staging inteira, exatamente como o empacotar.ps1 montou.
;    As tres excecoes abaixo saem daqui e voltam logo em seguida com outras flags.
Source: "{#Staging}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "dlss5forall.ini,ReShade.ini,FafaDLSS5.ini"

; 2. os tres arquivos de configuracao.
;    onlyifdoesntexist: numa reinstalacao/atualizacao, quem manda e o que a pessoa ajustou.
;      O app grava a fonte escolhida e os atalhos no dlss5forall.ini; o ReShade regrava o
;      ReShade.ini e o preset a cada sessao (AutoSave). Sobrescrever seria jogar fora a
;      tecla do overlay, os sliders do Neural Rendering e o preset em uso.
;      A afinacao NOVA de cada versao nao se perde por isso: o setup-dlss5.auto.ps1, que
;      roda no fim da instalacao, regrava chave por chave as que importam (Set-IniValue).
;    uninsneveruninstall: na desinstalacao eles NAO somem junto - ver [Code].
Source: "{#Staging}\dlss5forall.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#Staging}\ReShade.ini";     DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#Staging}\FafaDLSS5.ini";   DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

; 3. o que e do instalador e nao do zip.
;    O .ico vai solto de proposito: os atalhos apontam para ELE, e nao para o icone de
;    dentro do exe, para que o icone certo apareca mesmo quando o exe da pasta ainda for
;    um build anterior ao icone.
Source: "marca\DLSS5ForAll.ico";           DestDir: "{app}"; Flags: ignoreversion
Source: "instalador\configurar-cadeia.cmd"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MeuApp}";                Filename: "{app}\{#MeuExe}";              WorkingDir: "{app}"; IconFilename: "{app}\DLSS5ForAll.ico"
Name: "{group}\{cm:AtalhoConfigurar}";    Filename: "{app}\configurar-cadeia.cmd";  WorkingDir: "{app}"; IconFilename: "{app}\DLSS5ForAll.ico"
; a pasta e o lugar onde a pessoa larga o nvngx_dlssnr.dll e o renodx-dlss5.addon64 -
; e o atalho que mais economiza explicacao no suporte.
Name: "{group}\{cm:AtalhoPasta}";         Filename: "{app}"
Name: "{group}\{cm:AtalhoLeiame}";        Filename: "{win}\notepad.exe"; Parameters: """{app}\README.md"""; IconFilename: "{app}\DLSS5ForAll.ico"
Name: "{autodesktop}\{#MeuApp}";          Filename: "{app}\{#MeuExe}";              WorkingDir: "{app}"; IconFilename: "{app}\DLSS5ForAll.ico"; Tasks: desktopicon

[Run]
; A ORDEM DAQUI E A ORDEM DAS CAIXAS NA PAGINA DE CONCLUSAO, e ela e uma hierarquia:
; primeiro o que faz o programa FUNCIONAR (a cadeia), depois o que faz o DLSS 5 LIGAR (os
; dois arquivos), por ultimo a conveniencia (a pasta).
;
; 1. Caixa marcada por padrao; quem estiver sem internet desmarca e roda depois pelo atalho
;    "Configurar a cadeia" do Menu Iniciar - e o mesmo .cmd.
;    nowait: o setup baixa ReShade, shaders e o VC++ runtime e pode demorar minutos. Sem
;    isso o instalador ficaria congelado em "Concluindo" esse tempo todo.
;    AQUI ele herda a elevacao do instalador; pelo atalho, depois, roda como usuario comum
;    e escreve na pasta gracas ao users-modify do [Dirs]. Os dois caminhos precisam
;    funcionar, e e o segundo que depende da permissao.
Filename: "{app}\configurar-cadeia.cmd"; WorkingDir: "{app}"; Description: "{cm:ConfigurarAgora}"; Flags: postinstall shellexec nowait skipifsilent

; 2. A pagina do RHI, marcada por padrao: e o par da tela InfoAfter, que acabou de dizer
;    que faltam dois arquivos. Dizer e nao oferecer o caminho seria pior que nao dizer.
Filename: "https://github.com/RankFTW/RHI/releases"; Description: "{cm:AbrirRHI}"; Flags: postinstall shellexec nowait skipifsilent

; 3. A pasta, DESMARCADA. Nao por ser menos util - o RHI vai pedir justamente esta pasta -
;    mas porque as duas de cima ja abrem um console e um navegador, e tres janelas de uma
;    vez viram bagunca em vez de ajuda. Quem quiser tem ela aqui, no atalho do Menu Iniciar
;    e no botao que o proprio app mostra quando sente falta dos dois arquivos.
Filename: "{app}"; Description: "{cm:CaixaPasta}"; Flags: postinstall shellexec nowait skipifsilent unchecked

[Code]

procedure AddStr(var A: TArrayOfString; const S: String);
begin
  SetArrayLength(A, GetArrayLength(A) + 1);
  A[GetArrayLength(A) - 1] := S;
end;

// ---------------------------------------------------------------------------------
// O QUE A DESINSTALACAO FAZ COM O QUE NAO FOI ELA QUE INSTALOU
//
// Sobra na pasta, depois que o Inno remove o que instalou, tres tipos de arquivo:
//
//   (a) configuracao e logs (dlss5forall.ini, ReShade.ini, FafaDLSS5.ini, dlss5-feed.cfg,
//       *.log, os ReShade.ini.bak-* do app) e
//   (b) a cadeia que o setup BAIXOU (dxgi.dll, dlss5-feed.addon64, reshade-shaders\,
//       reshade-cache\)  -> os dois se recuperam rodando o setup de novo, entao vale
//       perguntar. Padrao NAO: reinstalar em cima fica pronto na hora.
//
//   (c) o que a PESSOA trouxe: nvngx_dlssnr.dll (165 MB), renodx-dlss5.addon64 e
//       nvngx_dlss.dll  -> esses nao entram nem na pergunta. Um "sim" clicado sem ler
//       custaria um download que pode nem existir mais (o add-on neural circula em canal
//       fechado), e o prejuizo e irreversivel do lado de quem clicou. Ficam na pasta, e a
//       mensagem final diz onde - apagar e trabalho de dois segundos para quem quiser.
// ---------------------------------------------------------------------------------
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App, Lista: String;
  Gerados, Trazidos: TArrayOfString;
  I: Integer;
  TemGerado: Boolean;
begin
  if CurUninstallStep <> usPostUninstall then Exit;
  App := ExpandConstant('{app}');
  if not DirExists(App) then Exit;

  // (a) + (b): oferecidos para remocao
  AddStr(Gerados, 'dlss5forall.ini');
  AddStr(Gerados, 'dlss5forall.log');
  AddStr(Gerados, 'ReShade.ini');
  AddStr(Gerados, 'ReShade.log');
  AddStr(Gerados, 'ReShade.ini.bak-*');
  AddStr(Gerados, 'FafaDLSS5.ini');
  AddStr(Gerados, 'dxgi.dll');
  AddStr(Gerados, 'dlss5-feed.addon64');
  AddStr(Gerados, 'dlss5-feed.cfg');
  AddStr(Gerados, 'dlss5-feed.log');

  // (c) trazidos pela pessoa: so para saber se sobraram, nunca para apagar
  AddStr(Trazidos, 'nvngx_dlssnr.dll');
  AddStr(Trazidos, 'renodx-dlss5.addon64');
  AddStr(Trazidos, 'nvngx_dlss.dll');

  TemGerado := DirExists(App + '\reshade-cache') or DirExists(App + '\reshade-shaders');
  for I := 0 to GetArrayLength(Gerados) - 1 do
    if (Pos('*', Gerados[I]) = 0) and FileExists(App + '\' + Gerados[I]) then
      TemGerado := True;

  // SuppressibleMsgBox e nao MsgBox: numa desinstalacao silenciosa (/VERYSILENT
  // /SUPPRESSMSGBOXES - o que uma ferramenta de gerenciamento ou um script faz) o MsgBox
  // comum APARECE e trava o desinstalador esperando um clique que ninguem vai dar.
  // Medido aqui: o processo ficou parado ate alguem clicar. O Suppressible devolve a
  // resposta que se passa como padrao (IDNO = preservar) e segue.
  if TemGerado and (SuppressibleMsgBox(CustomMessage('PerguntaLimpar'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2, IDNO) = IDYES) then
  begin
    // curinga incluido: DelTree com IsDir=False aceita mascara e nao toca em subpastas.
    for I := 0 to GetArrayLength(Gerados) - 1 do
      DelTree(App + '\' + Gerados[I], False, True, False);
    DelTree(App + '\reshade-cache', True, True, True);
    DelTree(App + '\reshade-shaders', True, True, True);
  end;

  // sobrou algo de (c)? diz onde esta, e so entao.
  Lista := '';
  for I := 0 to GetArrayLength(Trazidos) - 1 do
    if FileExists(App + '\' + Trazidos[I]) then
      Lista := Lista + '    ' + Trazidos[I] + #13#10;
  if Lista <> '' then
    SuppressibleMsgBox(FmtMessage(CustomMessage('AvisoSobrou'), [App, Lista]), mbInformation, MB_OK, IDOK);

  // RemoveDir so apaga pasta vazia: se ficou qualquer coisa, a pasta fica com ela.
  RemoveDir(App);
end;
