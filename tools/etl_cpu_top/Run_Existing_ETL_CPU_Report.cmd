@echo off
setlocal
REM Read-only ETL CPU report; Windows 10 PowerShell 5.1 file picker.
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0Run_Existing_ETL_CPU_Report.ps1"
set "rc=%errorlevel%"
if not "%rc%"=="0" (
  echo [ERROR] Existing ETL CPU report was not produced. Original ETL never edited.
  pause
)
exit /b %rc%
