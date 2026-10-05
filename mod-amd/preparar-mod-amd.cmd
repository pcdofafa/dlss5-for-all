@echo off
REM =====================================================================================
REM  preparar-mod-amd.cmd  -  confere a pasta bin\mod-amd e explica o que falta para o
REM  modulo AMD (mod DLSS-NR-on-AMD) funcionar. NAO baixa nada e NAO roda o instalador
REM  do mod: isso quem faz e voce, com a sua copia. Toda a logica esta no .ps1 ao lado;
REM  este .cmd so existe para abrir com dois cliques e segurar a janela no fim.
REM =====================================================================================
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0preparar-mod-amd.ps1"
echo.
pause
