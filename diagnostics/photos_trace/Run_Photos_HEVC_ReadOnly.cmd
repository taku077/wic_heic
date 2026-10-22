@echo off
setlocal
title Windows 10 Photos - HEVC read-only diagnosis
set "BASE=%~dp0"
set "NATIVE=%BASE%HevcMfReadOnlyProbe.exe"
set "CHECK=%BASE%WIC_Preflight_ReadOnly.ps1"
set "REPORT=%BASE%Photos_HEVC_ReadOnly_%RANDOM%_%RANDOM%.txt"
if not exist "%NATIVE%" (
 echo ERROR: HEVC probe executable is missing. No changes have been made.
 pause
 exit /b 2
)
if not exist "%CHECK%" (
 echo ERROR: WIC read-only preflight script is missing. No changes have been made.
 pause
 exit /b 2
)
echo Windows 10 Photos HEVC investigation (READ ONLY)
echo Nothing will be installed, removed or changed.
echo Report: "%REPORT%"
echo ---
>"%REPORT%" echo Photos and HEVC read-only report
>>"%REPORT%" echo No user image was opened or modified
>>"%REPORT%" echo ---
>>"%REPORT%" echo [Media Foundation codec inventory]
"%NATIVE%" >>"%REPORT%" 2>&1
set "NATIVE_RC=%ERRORLEVEL%"
>>"%REPORT%" echo MF probe return code: %NATIVE_RC%
>>"%REPORT%" echo.
>>"%REPORT%" echo [WIC and current-user HEIF/HEVC inventory]
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CHECK%" >>"%REPORT%" 2>&1
set "SCRIPT_RC=%ERRORLEVEL%"
>>"%REPORT%" echo Preflight return code: %SCRIPT_RC%
if "%NATIVE_RC%"=="0" if "%SCRIPT_RC%"=="0" (
 echo Diagnostics finished. Please inspect the report before sharing.
) else (
 echo Read-only diagnostics partly failed. Inspect report for details.
)
echo Report saved at: "%REPORT%"
echo NOTE: the separate JSON created by the preflight contains local DLL paths.
echo Check personal paths before sending diagnostic files to anyone.
pause
exit /b 0
