@echo off
setlocal
rem Synthetic HEIC only. Nothing is installed, deleted from the user's photos,
rem or changed in the global Windows thumbnail cache settings.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run_Synthetic_Explorer_Test.ps1" -Mode Run
if errorlevel 1 (
 echo.
 echo Test stopped with an error. Check the Results folder.
 pause
)
exit /b %errorlevel%
