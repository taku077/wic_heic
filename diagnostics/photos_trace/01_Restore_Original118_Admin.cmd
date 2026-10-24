@echo off
chcp 65001 >nul
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Restore118_NoSession.ps1" -Restore
if errorlevel 1 echo [STOP] Restore not verified. Keep logs; do NOT try any other MSI.
pause
