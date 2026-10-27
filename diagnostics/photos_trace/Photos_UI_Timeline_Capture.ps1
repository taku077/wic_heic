# Windows 10 / Windows PowerShell 5.1. No install, elevation, hooks, images or registry writes.
# Records numeric timestamps, foreground window geometry and optional F9 marks.
param(
    [ValidateRange(5,180)][int]$DurationSeconds = 90,
    [ValidateRange(100,1000)][int]$IntervalMs = 200
)
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
if($root -notmatch '^[A-Za-z]:\\') { throw 'Use a local drive folder, not a network path.' }
$dir=Get-Item -LiteralPath $root -ErrorAction Stop
while($null -ne $dir) {
    if(($dir.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Junctions and symbolic links are prohibited for capture.'
    }
    $dir=$dir.Parent
}
if(-not ('PhotosUiTimelineWin32' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PhotosUiTimelineWin32 {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("kernel32.dll")] public static extern ulong GetTickCount64();
    [DllImport("kernel32.dll")] public static extern void GetSystemTimeAsFileTime(out long fileTime);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd,out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd,out RECT rect);
    [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vkey);
}
'@ -ErrorAction Stop
}
$out=Join-Path $root ('Photos_UI_Timeline_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+[guid]::NewGuid().ToString('N').Substring(0,8)+'.csv')
$fs=[System.IO.File]::Open($out,[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::Read)
$writer=New-Object System.IO.StreamWriter($fs,([System.Text.UTF8Encoding]::new($false)))
$writer.AutoFlush=$true
try {
    $writer.WriteLine('utc_filetime,tick,event,foreground_pid,window_w,window_h,client_w,client_h,process_cpu_ms,working_set_kib')
    $until=[PhotosUiTimelineWin32]::GetTickCount64()+[ulong]($DurationSeconds*1000)
    $lastPressed=$false
    Write-Host 'Photo UI numeric timeline capture started; switch to Windows Photos.'
    Write-Host 'While Photos is foreground, tap F9 when the visible glitch occurs (hold briefly).'
    Write-Host 'F9 marks only an instant; the app itself might also react to the key.'
    Write-Host 'No pixels, screenshot, window title, image path or image metadata are captured.'
    Write-Host ('Duration={0}s; interval={1}ms; output={2}' -f $DurationSeconds,$IntervalMs,$out)
    while([PhotosUiTimelineWin32]::GetTickCount64() -lt $until) {
        $stamp=[long]0
        [PhotosUiTimelineWin32]::GetSystemTimeAsFileTime([ref]$stamp)
        $tick=[PhotosUiTimelineWin32]::GetTickCount64()
        $win=[PhotosUiTimelineWin32]::GetForegroundWindow()
        $pidNative=[uint32]0
        $w=0; $h=0; $cw=0; $ch=0
        if($win -ne [IntPtr]::Zero) {
            [void][PhotosUiTimelineWin32]::GetWindowThreadProcessId($win,[ref]$pidNative)
            $rectangle=New-Object PhotosUiTimelineWin32+RECT
            if([PhotosUiTimelineWin32]::GetWindowRect($win,[ref]$rectangle)) {
                $w=[math]::Max(0,$rectangle.Right-$rectangle.Left)
                $h=[math]::Max(0,$rectangle.Bottom-$rectangle.Top)
            }
            if([PhotosUiTimelineWin32]::GetClientRect($win,[ref]$rectangle)) {
                $cw=[math]::Max(0,$rectangle.Right-$rectangle.Left)
                $ch=[math]::Max(0,$rectangle.Bottom-$rectangle.Top)
            }
        }
        $cpu=0; $memory=0
        if($pidNative -gt 0) {
            try {
                $proc=[System.Diagnostics.Process]::GetProcessById([int]$pidNative)
                $cpu=[math]::Round($proc.TotalProcessorTime.TotalMilliseconds)
                $memory=[math]::Round($proc.WorkingSet64/1024.0)
                $proc.Dispose()
            } catch [System.ArgumentException] {
            } catch [System.InvalidOperationException] {
            } catch [System.ComponentModel.Win32Exception] {
            }
        }
        # Edge-detect F9; no hook, input recording, or interception.
        $pressed=(([int][PhotosUiTimelineWin32]::GetAsyncKeyState(0x78) -band 0x8000) -ne 0)
        $event=if($pressed -and -not $lastPressed){'MARK_F9'} else {'SAMPLE'}
        $lastPressed=$pressed
        $writer.WriteLine(('{0},{1},{2},{3},{4},{5},{6},{7},{8},{9}' -f
            $stamp,$tick,$event,$pidNative,$w,$h,$cw,$ch,$cpu,$memory))
        if($event -eq 'MARK_F9') { Write-Host ('UI_GLITCH_MARK tick={0}' -f $tick) }
        Start-Sleep -Milliseconds $IntervalMs
    }
} finally {
    $writer.Dispose()
}
Write-Host ('Timeline saved: '+$out)
Write-Host 'This measures foreground WINDOW geometry only, NOT the picture region inside Photos.'
Write-Host 'Use Photos_UI_Timeline_Correlate.ps1 with this CSV and the WIC trace.'
