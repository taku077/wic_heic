@echo off
setlocal
cd /d "%~dp0"
echo Windows 10 Photos UI time correlation, 90 seconds.
echo This temporarily enables the existing WIC numeric trace in HKCU.
echo No installer, no screenshot, no picture content captured.
echo Press F9 in the Photos window only when display corruption appears.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Photos_UI_Timeline_Run.ps1"
if errorlevel 1 (
  echo Error encountered. Verify any diagnostic status using PhotosTrace_Stop.ps1.
)
echo.
pause
