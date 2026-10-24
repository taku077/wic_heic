@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo This only observes the installed WIC and a synthetic HEIC. No MSI changes.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Photos_AB_Capture.ps1" -Phase B
if errorlevel 1 (
  echo [STOP] B capture did not finish; no files removed. See error above.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0B_Result_Collect.ps1"
if errorlevel 1 echo [STOP] Result packaging failed. The original JSON and synthetic work image remain untouched.
pause
