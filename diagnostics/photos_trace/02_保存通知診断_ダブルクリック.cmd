@echo off
setlocal
cd /d "%~dp0"
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" (
 echo Windows PowerShell 5.1 was not found.
 pause
 exit /b 2
)
if not exist "%~dp0HEIC_SaveShell_PassiveMonitor.ps1" (
 echo Diagnostic PowerShell file is missing. Extract all ZIP contents first.
 pause
 exit /b 3
)
if /I "%~1"=="--ci-selftest" (
 if /I not "%GITHUB_ACTIONS%"=="true" exit /b 6
 "%PS%" -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0HEIC_SaveShell_PassiveMonitor.ps1" -CiSelfTest -DurationSeconds 4
 exit /b %ERRORLEVEL%
)
"%PS%" -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0HEIC_SaveShell_PassiveMonitor.ps1"
set "CODE=%ERRORLEVEL%"
if not "%CODE%"=="0" (
 echo.
 echo Diagnostic failed. Exit code: %CODE%
 pause
)
exit /b %CODE%
