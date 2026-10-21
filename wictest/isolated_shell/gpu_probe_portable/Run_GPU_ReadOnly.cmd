@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
:NEWLOG
set "LOG=%~dp0GPU_Readonly_Result_%RANDOM%_%RANDOM%.txt"
if exist "%LOG%" goto NEWLOG
> "%LOG%" echo HEIC GPU D3D11VA Main10 read-only research diagnostic
>> "%LOG%" echo This tool only opens two bundled synthetic HEVC samples.
>> "%LOG%" echo No user HEIC photos, no codec installation, no MSI, no registry changes.
>> "%LOG%" echo === CPU SELFTEST ===
"%~dp0HeicD3D11PortableProbe.exe" --software-selftest >> "%LOG%" 2>&1
if errorlevel 1 goto FAILED
>> "%LOG%" echo === GPU VIDEO HARDWARE DECODER CAPS ===
"%~dp0D3D11HevcCaps.exe" >> "%LOG%" 2>&1
if errorlevel 1 >> "%LOG%" echo GPU_DECODER_CAPS_CHECK_FAILED
>> "%LOG%" echo === GPU ADAPTER CHECKS ===
for %%I in (0 1 2 3) do (
  >> "%LOG%" echo === GPU INDEX %%I ===
  "%~dp0HeicD3D11PortableProbe.exe" %%I >> "%LOG%" 2>&1
  >> "%LOG%" echo EXIT_CODE=%%I:!ERRORLEVEL!
)
goto FINISHED
:FAILED
>> "%LOG%" echo CPU_SELFTEST_FAILED_ ABORT_GPU_TESTS
:FINISHED
type "%LOG%"
echo.
echo Diagnostic complete. Upload only the generated GPU_Readonly_Result_*.txt to this chat.
echo Saved log: "%LOG%"
echo The diagnostic changes no Windows codec or user HEIC files.
pause
endlocal
