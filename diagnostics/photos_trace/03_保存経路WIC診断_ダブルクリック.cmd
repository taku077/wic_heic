@echo off
setlocal
cd /d "%~dp0"
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" (
 echo Windows PowerShell 5.1 not found.
 pause
 exit /b 2
)
if not exist "%~dp0HEIC_Save_WIC_OneClick.ps1" (
 echo Save WIC diagnostic script missing. Extract all files from ZIP.
 pause
 exit /b 3
)
if /I "%~1"=="--ci-selftest" (
 if /I not "%GITHUB_ACTIONS%"=="true" exit /b 6
 "%PS%" -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0HEIC_Save_WIC_OneClick.ps1" -CiSelfTest
 exit /b %ERRORLEVEL%
)
"%PS%" -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0HEIC_Save_WIC_OneClick.ps1"
set "CODE=%ERRORLEVEL%"
if not "%CODE%"=="0" (
 echo Diagnostic failed with exit code %CODE%.
 echo Confirm WIC trace cleanup using PhotosTrace_Stop.ps1 if required.
 pause
)
exit /b %CODE%
