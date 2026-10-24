@echo off
chcp 65001 >nul
cd /d "%~dp0"
rem PowerShell safety gate checks real administrator token; do not use net session.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0WIC120_OneTimeTrial.ps1" -Install
if errorlevel 1 echo [STOP] Installation was not confirmed. Keep all logs and session JSON. Do not edit images.
pause
