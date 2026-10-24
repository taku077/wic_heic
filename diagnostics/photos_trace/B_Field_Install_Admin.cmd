@echo off
chcp 65001 >nul
cd /d "%~dp0"
net session >nul 2>&1
if errorlevel 1 (
  echo [STOP] Right-click this CMD and select Run as administrator.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0WIC120_OneTimeTrial.ps1" -Install
if errorlevel 1 echo [STOP] Installation was not confirmed. Keep all logs and session JSON. Do not edit images.
pause
