@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'powershell.exe' -ArgumentList '-NoProfile -ExecutionPolicy Bypass -NoExit -File ""%~dp0Restore_Original_WicHeic_OneClick.ps1""' -Verb RunAs"
if errorlevel 1 (
  echo Administrator approval failed; no install requested.
  pause
)
