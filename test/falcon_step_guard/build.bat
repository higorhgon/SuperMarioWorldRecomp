@echo off
setlocal
cd /d "%~dp0"
set CC=gcc
if not "%1"=="" set CC=%1
%CC% -std=c11 -Wall -Wextra -Werror -I..\.. -I..\..\src -I..\..\snesrecomp\runner\src -I..\..\recomp falcon_step_guard_test.c ..\..\overrides\falcon\falcon_smw_adapter.c ..\..\src\mods\falcon\falcon_locomotion.c ..\..\src\mods\falcon\captain_falcon_foreign.c ..\..\src\mods\falcon\smw_falcon_combat_policy.c ..\..\src\mods\falcon\smw_falcon_combat_apply.c ..\..\snesrecomp\runner\src\foreign_controller.c -o falcon_step_guard_test.exe
if errorlevel 1 exit /b 1
falcon_step_guard_test.exe
