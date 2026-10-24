@echo off
chcp 65001 >nul
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Restore118_NoSession.ps1"
if errorlevel 1 echo [STOP] Preflight failed. Keep logs; do NOT modify MSI.
pause
