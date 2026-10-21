@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
:NEWLOG
set "LOG=%~dp0GPU_Thumbnail_Experimental_%RANDOM%_%RANDOM%.txt"
if exist "%LOG%" goto NEWLOG
> "%LOG%" echo Intel 12MP HEVC Main10 direct GPU VideoProcessor 256px thumbnail research
>> "%LOG%" echo Experimental mode; NOT an installer or Windows Explorer optimization.
>> "%LOG%" echo Only four bundled pinned HEVC streams (two HEIC-derived, two synthetic) are opened.
>> "%LOG%" echo Output BGRA thumbnail remains in RAM; only this report text is written.
>> "%LOG%" echo Compare against Windows WIC Fant and full-frame P010 readback.
>> "%LOG%" echo === CPU SELFTEST ===
"%~dp0HeicD3D11PortableProbe.exe" --software-selftest >> "%LOG%" 2>&1
if errorlevel 1 goto FAILED
>> "%LOG%" echo === REAL INTEL GPU ADAPTER SELECTION AND 256PX READBACK ===
for %%I in (0 1 2 3) do (
  >> "%LOG%" echo === EXPERIMENTAL GPU INDEX %%I ===
  "%~dp0HeicD3D11PortableProbe.exe" thumb:%%I >> "%LOG%" 2>&1
  >> "%LOG%" echo GPU_INDEX=%%I EXIT_CODE=!ERRORLEVEL!
)
goto COMPLETE
:FAILED
>> "%LOG%" echo FAILED_SELFTEST_STOPPING_NO_GPU_ATTEMPT
:COMPLETE
type "%LOG%"
echo.
echo Research test completed. Output log: "%LOG%"
echo Nothing was installed and no source image was modified.
if /I not "%HEIC_GPU_PROBE_CI%"=="1" pause
endlocal
