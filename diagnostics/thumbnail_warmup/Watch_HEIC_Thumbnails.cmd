@echo off
setlocal
rem HEIC thumbnail warmup - finite foreground Explorer watcher
rem No admin privileges, registry modifications, or background installation.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Explorer_HEIC_Warmup.ps1" -Mode Watch -DurationSeconds 180 -MaxFiles 20
if errorlevel 1 (
  echo.
  echo HEIC thumbnail trial stopped with an error.
  echo Please take a screenshot of the error text if support is needed.
  pause
  exit /b 1
)
exit /b 0
