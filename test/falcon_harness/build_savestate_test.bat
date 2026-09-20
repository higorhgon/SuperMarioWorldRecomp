@echo off
setlocal
cd /d "%~dp0\..\.."
python tools\test_smw_savestates.py
exit /b %errorlevel%
