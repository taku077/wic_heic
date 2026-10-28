@echo off
setlocal
cd /d "%~dp0"
set "PSEXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PSEXE%" (
  echo Windows PowerShell 5.1 was not found.
  pause
  exit /b 2
)
if not exist "%~dp0HEIC_Thumbnail_DoubleClick_GUI.ps1" (
  echo Required diagnostic GUI script is missing. Extract ALL ZIP files first.
  pause
  exit /b 3
)
if not exist "%~dp0HEIC_ThumbnailCache_AB_ReadOnly.ps1" (
  echo Required diagnostic engine is missing. Extract ALL ZIP files first.
  pause
  exit /b 4
)
if /I "%~1"=="--ci-selftest" (
  if /I not "%GITHUB_ACTIONS%"=="true" exit /b 6
  "%PSEXE%" -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0HEIC_Thumbnail_DoubleClick_GUI.ps1" -CiSelfTest
  exit /b %ERRORLEVEL%
)
"%PSEXE%" -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0HEIC_Thumbnail_DoubleClick_GUI.ps1"
set "EXITCODE=%ERRORLEVEL%"
if not "%EXITCODE%"=="0" (
  echo.
  echo Diagnostic launcher exited with an error. Code: %EXITCODE%
)
if not "%EXITCODE%"=="0" (
  echo.
  echo Press any key to close this error window.
  pause >nul
)
exit /b %EXITCODE%
