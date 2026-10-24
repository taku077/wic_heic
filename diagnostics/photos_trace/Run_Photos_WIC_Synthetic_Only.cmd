@echo off
setlocal
title Windows 10 HEIC WIC - pinned public synthetic decode only
set "BASE=%~dp0"
set "EXE=%BASE%HevcMfReadOnlyProbe.exe"
set "FIXTURE=%BASE%fixture-irot3-exif6-noaux.heic"
set "REPORT=%BASE%Photos_WIC_Synthetic_%RANDOM%_%RANDOM%.txt"
echo Read-only WIC synthetic HEIC comparison.
echo No personal images will be opened. No Photo app or MSI will be launched.
echo Only a SHA-verified public HEIC next to the EXE will be decoded in memory.
echo The installed native Microsoft and private WIC codecs may be loaded.
if not exist "%EXE%" (
 echo ERROR: Native probe is missing.
 exit /b 2
)
if not exist "%FIXTURE%" (
 echo ERROR: Pinned artificial HEIC fixture is missing.
 exit /b 2
)
if exist "%REPORT%" (
 echo ERROR: Refusing to overwrite an existing report.
 exit /b 2
)
"%EXE%" --synthetic-wic-test >"%REPORT%" 2>&1
set "RC=%ERRORLEVEL%"
type "%REPORT%"
echo.
echo Result code: %RC%
echo Report: "%REPORT%"
echo No source file has been modified.
pause
exit /b %RC%
