@echo off
REM =====================================================================================
REM  configurar-cadeia.cmd  -  roda o setup-dlss5.auto.ps1 DESTA pasta e segura a janela.
REM
REM  Existe por dois motivos, os dois praticos:
REM   1. o instalador precisa de UM alvo para a caixa "Baixar e configurar a cadeia agora"
REM      e para o atalho "Configurar a cadeia" do Menu Iniciar. Um arquivo so, dois usos,
REM      e nada de linha de comando gigante de powershell dentro do .iss (que quebra na
REM      primeira aspas fora do lugar).
REM   2. chamado direto, o powershell FECHA a janela ao terminar - e o setup termina
REM      justamente com o checklist do que ficou FALTANDO na pasta. Sem o pause aqui,
REM      ninguem le o unico texto do setup que realmente importa.
REM
REM  Usa o .auto: ele nao tem Read-Host no meio (o interativo trava esperando ENTER em
REM  pontos onde ninguem esta olhando quando isso roda ao fim da instalacao).
REM =====================================================================================
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-dlss5.auto.ps1"
echo.
pause
