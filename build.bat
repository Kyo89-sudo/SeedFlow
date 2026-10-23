@echo off
rem SeedFlow 0.8.7.2 - compilazione Windows x64 (Visual Studio 2026/2022 + vcpkg)
rem Uso:  build.bat            compila e crea SeedFlow-Pronto\<data-ora>\SeedFlow.exe
rem       build.bat -CheckOnly verifica solo i prerequisiti
rem       build.bat -NoPause   nessuna pausa finale (deve essere il primo argomento)
setlocal EnableExtensions DisableDelayedExpansion
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" %*
set "RESULT=%ERRORLEVEL%"
if /i not "%~1"=="-NoPause" pause
exit /b %RESULT%

