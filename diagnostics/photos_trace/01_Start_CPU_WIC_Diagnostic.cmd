@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0COM_CPU_WIC_Capture.ps1"
set "err=%ERRORLEVEL%"
echo.
if not "%err%"=="0" echo ERROR: Diagnostic failed. No MSI was modified.
echo Output ZIP is in this folder if capture completed.
pause
exit /b %err%
