@echo off
chcp 65001 >nul
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Photos_AB_Capture.ps1" -Phase B
if errorlevel 1 echo ERROR: Phase B did not complete.
pause
