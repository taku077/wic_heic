@echo off
setlocal
cd /d "%~dp0"
echo One-pass CPU stack + WIC diagnosis (Windows 10)
echo.
echo Windows UAC permission is needed to collect sampled CPU stacks.
echo No HEIC files will be changed. ETL may include process paths.
echo.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0COM_CPU_WPR_OnePass.ps1"
if errorlevel 1 echo ERROR: Diagnostic did not finish. Review the PowerShell error above.
echo.
echo The result is saved as COM_CPU_WPR_Result_*.zip in this folder.
pause
