@echo off
setlocal
set OUT=%~dp0host_runtime_test.exe
gcc -std=gnu11 -I..\..\src -I..\..\snesrecomp\runner\src ^
  %~dp0falcon_host_runtime_test.c ^
  %~dp0..\..\src\mods\falcon\smw_falcon_presentation_runtime.c ^
  %~dp0..\..\src\mods\falcon\falcon_presentation.c ^
  %~dp0..\..\snesrecomp\runner\src\sha256.c -lm -o "%OUT%"
if errorlevel 1 exit /b 1
"%OUT%"
