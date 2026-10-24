@echo off
setlocal
title Windows 10 Photos - ONE RUN WIC and HEVC diagnosis
if not "%~1"=="" (
 echo ERROR: Arbitrary arguments and image paths are refused.
 pause
 exit /b 2
)
set "BASE=%~dp0"
set "SCRIPT=%BASE%Run_Photos_Batch_ReadOnly.ps1"
if not exist "%SCRIPT%" (
 echo ERROR: The single-run PowerShell script is missing.
 pause
 exit /b 2
)
echo WIC for HEIC - integrated read-only diagnostic
echo.
echo One execution covers native WIC and HEVC inventory, custom/native/auto
echo HEIC decoding, synthetic full-frame digests and installed codec status.
echo Only a SHA-verified synthetic file is decoded. Existing photos,
echo WIC installations and Windows settings will NOT be changed.
echo.
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%"
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" (
 echo Some checks failed. A partial ZIP may have been created for analysis.
) else (
 echo Completed. Share ONLY the new WIC_Batch_*.zip file.
)
echo Do not run previous individual diagnostic scripts after this.
pause
exit /b %RESULT%
