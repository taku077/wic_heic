@echo off
chcp 65001 >nul
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Photos_AB_Compare.ps1"
if errorlevel 1 echo ERROR: A/B compare did not complete.
pause
