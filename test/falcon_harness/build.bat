@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c11 /D_CRT_SECURE_NO_WARNINGS /W4 /O2 /Fe:falcon_harness.exe falcon_harness.c ..\..\src\mods\falcon\falcon_locomotion.c ..\..\src\mods\falcon\captain_falcon_controller.c
if errorlevel 1 exit /b %errorlevel%
for %%S in (run_right dash_turn jump fast_fall short_hop host_launch host_ledge_fall jump_height_split host_impulse combat downb_transitions upb audio) do (
    falcon_harness.exe %%S.script --csv trace_%%S.csv || exit /b !errorlevel!
)
