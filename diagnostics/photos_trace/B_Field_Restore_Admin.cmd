@echo off
chcp 65001 >nul
cd /d "%~dp0"
net session >nul 2>&1
if errorlevel 1 (
  echo [STOP] Right-click this CMD and select Run as administrator.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0B_Field_Restore_Latest.ps1"
if errorlevel 1 echo [STOP] Restoration NOT verified. Keep all logs; do not manually edit registry.
pause
