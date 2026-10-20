@echo off
setlocal
cd /d "%~dp0"
echo WicHeic EXPERIMENTAL x64 safe-install guard
echo Will refuse to replace any already registered WicHeic codec.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install_Experimental_WIC.ps1"
echo.
echo Review the result above. Press a key to close this window.
pause >nul
