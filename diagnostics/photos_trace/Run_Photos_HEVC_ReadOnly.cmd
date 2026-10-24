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
echo No codecs or settings will be intentionally installed, removed, or changed.
echo The optional image-free WIC COM check may load registered codec DLLs.
echo New report files will be saved in this diagnostic folder.
echo Report: "%REPORT%"
echo ---
>"%REPORT%" echo Photos and HEVC read-only report
>>"%REPORT%" echo No user image was opened or modified; WIC COM activation may load codec DLLs
>>"%REPORT%" echo ---
>>"%REPORT%" echo [Media Foundation codec inventory]
"%NATIVE%" --wic-activate-no-image >>"%REPORT%" 2>&1
set "NATIVE_RC=%ERRORLEVEL%"
>>"%REPORT%" echo MF probe return code: %NATIVE_RC%
>>"%REPORT%" echo.
>>"%REPORT%" echo [WIC and current-user HEIF/HEVC inventory]
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CHECK%" >>"%REPORT%" 2>&1
set "SCRIPT_RC=%ERRORLEVEL%"
>>"%REPORT%" echo Preflight return code: %SCRIPT_RC%
set "RESULT=0"
if not "%NATIVE_RC%"=="0" set "RESULT=1"
if not "%SCRIPT_RC%"=="0" set "RESULT=1"
if "%RESULT%"=="0" (
 echo Diagnostics finished. Please inspect the report before sharing.
) else (
 echo One or more diagnostics failed. Inspect the report for details.
)
echo Report saved at: "%REPORT%"
echo NOTE: the separate JSON created by the preflight contains local DLL paths.
echo Check personal paths before sending diagnostic files to anyone.
pause
exit /b %RESULT%
