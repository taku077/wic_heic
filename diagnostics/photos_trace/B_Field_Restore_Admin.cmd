@echo off
chcp 65001 >nul
cd /d "%~dp0"
rem PowerShell safety gate checks real administrator token; do not use net session.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0B_Field_Restore_Latest.ps1"
if errorlevel 1 echo [STOP] Restoration NOT verified. Keep all logs; do not manually edit registry.
pause
