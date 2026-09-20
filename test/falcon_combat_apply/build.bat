@echo off
setlocal
cd /d "%~dp0"
set CC=gcc
if not "%1"=="" set CC=%1
%CC% -std=c99 -Wall -Wextra -Werror -I..\..\src\mods\falcon -I..\..\snesrecomp\runner\src -I..\..\recomp falcon_combat_apply_test.c ..\..\src\mods\falcon\smw_falcon_combat_apply.c ..\..\src\mods\falcon\smw_falcon_combat_policy.c -o falcon_combat_apply_test.exe
if errorlevel 1 exit /b 1
falcon_combat_apply_test.exe
