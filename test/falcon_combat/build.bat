@echo off
setlocal
gcc -std=c11 -Wall -Wextra -Werror -pedantic falcon_combat_policy_test.c ..\..\src\mods\falcon\smw_falcon_combat_policy.c -I..\..\snesrecomp\runner\src -o falcon_combat_policy_test.exe
if errorlevel 1 exit /b %errorlevel%
falcon_combat_policy_test.exe
