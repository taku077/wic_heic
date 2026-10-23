@echo off
setlocal EnableExtensions DisableDelayedExpansion
title Photos WIC codec load - READ ONLY (no administrator needed)
set "BASE=%~dp0"
if not exist "%BASE%Photos_WIC_Process_ReadOnly.ps1" (
 echo ERROR: The matching read-only PowerShell script was not found.
 echo No Windows setting or image was changed.
 pause
 exit /b 2
)
echo Checking Photos process names and module basenames ONLY.
echo No images are opened, modified, converted, or deleted by this script.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%BASE%Photos_WIC_Process_ReadOnly.ps1"
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
 echo Diagnostic returned error %RESULT%. No installer or registry changes made.
) else (
 echo Read-only report saved next to this CMD. Inspect before sharing.
)
if /I not "%GITHUB_ACTIONS%"=="true" pause
exit /b %RESULT%
