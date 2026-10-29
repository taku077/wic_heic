@echo off
setlocal
cd /d "%~dp0"
echo Windows 10 HEIC: REAL folder / WPR CPU stacks / MANUAL stop.
echo UAC administrator permission required. HEIC photos are NOT edited.
echo WPR ETL may contain personal file paths. Inspect before sharing.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0COM_CPU_WPR_Manual.ps1"
if errorlevel 1 echo ERROR: The trace could not be verified. Read the error above.
echo.
echo Result ZIP is COM_CPU_WPR_Manual_*.zip in this directory.
pause
