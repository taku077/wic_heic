@echo off
setlocal
cd /d "%~dp0"
echo One-pass CPU stack and WIC diagnosis - Windows 10
echo.
echo This tool starts a 60-second Windows Performance Recorder trace.
echo Administrator approval is required for CPU stack sampling.
echo No HEIC files will be changed. A diagnostic ETL can contain system paths.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath 'powershell.exe' -Verb RunAs -Wait -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('\"'+(Join-Path (Get-Location).Path 'COM_CPU_WPR_OnePass.ps1')+'\"'))"
if errorlevel 1 echo The diagnostic was not completed; check the PowerShell window for details.
echo.
echo Look in this folder for COM_CPU_WPR_Result_*.zip
pause
