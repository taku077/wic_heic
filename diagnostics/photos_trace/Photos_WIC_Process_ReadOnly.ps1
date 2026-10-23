#requires -version 5.1
# Windows 10 Photos HEIC investigation: observe process module names ONLY.
# No photos, file contents, browser histories or user documents are read.
# No registry/AppX/MSI/codec modifications and no elevation request.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$dir=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
if($dir -notmatch '^[A-Za-z]:\\'){throw 'Extract to an ordinary local folder'}
$node=Get-Item -LiteralPath $dir
while($node) {
 if(($node.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
  throw 'Refusing to run from a link or junction'
 }
 $node=$node.Parent
}
$os=Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
$wics=@()
foreach($type in @(
 @{kind='Decoder';clsid='{6215667C-74CA-4076-A769-3F834C3F25F5}'},
 @{kind='Encoder';clsid='{0E8AB687-73A5-455B-8C19-B93CA9AC0EEB}'}
)){
 $p="Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$($type.clsid)\InprocServer32"
 $k=Get-Item -LiteralPath $p -ErrorAction SilentlyContinue
 $hash='NOT_REGISTERED'
 if($null -ne $k){
  $location=[string]$k.GetValue('')
  if(-not [string]::IsNullOrWhiteSpace($location)){
   $expanded=[Environment]::ExpandEnvironmentVariables($location).Trim('"')
   if(Test-Path -LiteralPath $expanded -PathType Leaf){
    try {$hash=(Get-FileHash -LiteralPath $expanded -Algorithm SHA256).Hash}
    catch {$hash='READ_FAILED'}
   } else {$hash='FILE_MISSING'}
  }
 }
 $wics+=([ordered]@{Role=$type.kind;Sha256=$hash})
}
# This optional USER32 lookup enumerates window handles ONLY. It never
# obtains window titles/text, pixels, class names, image paths or documents.
# Modern Windows 10 Photos can have separate Photos.exe processes, with the
# visible app window hosted by ApplicationFrameHost.exe. Relate a visible
# host's CHILD windows to their actual Photos process IDs to avoid assuming
# the process with the codec DLL is the one showing the HEVC warning.
$windowScan='OK'
$windowCounts=$null
try {
 $source=@'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class PhotosReadonlyWindowProbe {
    private delegate bool EnumWindowProc(IntPtr hwnd, IntPtr ctx);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowProc cb, IntPtr ctx);
    [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr parent, EnumWindowProc cb, IntPtr ctx);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    private static void Add(Dictionary<int,int[]> d, int pid, int index) {
        if(pid<=0) return;
        int[] values;
        if(!d.TryGetValue(pid,out values)) {
            values=new int[] {0,0};
            d.Add(pid,values);
        }
        if(values[index]<100000) values[index]++;
    }
    public static Dictionary<int,int[]> Snapshot() {
        var result=new Dictionary<int,int[]>();
        bool ok=EnumWindows(delegate(IntPtr top, IntPtr context) {
            if(!IsWindowVisible(top)) return true;
            uint topPid=0;
            GetWindowThreadProcessId(top,out topPid);
            Add(result,(int)topPid,0);
            // Count each distinct visible child-process PID ONCE for this
            // host window. Multiple UI controls from one app aren't distinct
            // Photos processes.
            var childPids=new HashSet<int>();
            EnumChildWindows(top,delegate(IntPtr child,IntPtr ignored){
                if(IsWindowVisible(child)) {
                    uint childPid=0;
                    GetWindowThreadProcessId(child,out childPid);
                    if(childPid!=0) childPids.Add((int)childPid);
                }
                return true;
            },IntPtr.Zero);
            foreach(int pid in childPids) Add(result,pid,1);
            return true;
        },IntPtr.Zero);
        if(!ok) throw new InvalidOperationException("Window enumeration unavailable");
        return result;
    }
}
'@
 Add-Type -TypeDefinition $source -Language CSharp -ErrorAction Stop
 $windowCounts=[PhotosReadonlyWindowProbe]::Snapshot()
} catch {
 $windowScan='UNAVAILABLE'
}
# Record only module basenames matching codecs / relevant imaging infrastructure.
# An absent module is not evidence that Photos did NOT use that codec at
# another point; modules may be unloaded or hosted in a broker process.
$processes=New-Object 'System.Collections.Generic.List[object]'
$candidates=@(Get-Process -ErrorAction SilentlyContinue | Where-Object {
 $_.ProcessName -match '^(Microsoft[.]Photos|Photos|PhotosApp|ApplicationFrameHost)$'
})
foreach($p in $candidates) {
 $identified=New-Object 'System.Collections.Generic.List[string]'
 $moduleStatus='OK'
 try {
  foreach($m in $p.Modules) {
   $nm=[IO.Path]::GetFileName([string]$m.ModuleName).ToLowerInvariant()
   if($nm -match '^(wic_heic[.]dll|wicheicprivatecodec[.]dll|windowscodecs[.]dll|heif[.]dll|hevc[.]dll|mshevcdec[.]dll)$'){
    if(-not $identified.Contains($nm)){$identified.Add($nm)}
   }
  }
 } catch {
  $moduleStatus='UNAVAILABLE_ACCESS_DENIED_OR_PROCESS_EXITED'
 }
 $topWindows=0;$hostedWindows=0
 if($null -ne $windowCounts -and $windowCounts.ContainsKey([int]$p.Id)) {
  $topWindows=[int]$windowCounts[[int]$p.Id][0]
  $hostedWindows=[int]$windowCounts[[int]$p.Id][1]
 }
 $processes.Add([pscustomobject][ordered]@{
  Name=[string]$p.ProcessName
  PID=[int]$p.Id
  ModuleScan=$moduleStatus
  RelevantModuleBasenames=@($identified.ToArray())
  VisibleTopLevelWindowCount=$topWindows
  VisibleHostedChildWindowCount=$hostedWindows
 })
}
# This classification deliberately does not claim which process handled
# the open image or which API controls Photos' edit buttons. It only relates
# live, visible Photos process/window PIDs to mapped custom WIC modules.
$visiblePhotos=@($processes | Where-Object {
 ($_.Name -match '^(Microsoft[.]Photos|Photos|PhotosApp)
 Schema=2
 CollectedAtUtc=[DateTime]::UtcNow.ToString('o')
 WindowsBuild=[string]$os.CurrentBuild
 WindowScan=$windowScan
 VisiblePhotosWicCorrelation=$correlation
 PhotosProcessModuleSnapshot=@($processes.ToArray())
 RegisteredCustomWicDllHashes=$wics
 Interpretation='A codec module mapped in one Photos process does not prove it belongs to the visible editor. Window counts are an instantaneous PID correlation, not proof of which codec the editor used or why controls are disabled. No module loaded now is NOT proof that none was used before.'
 FileContentsOrPhotosAccessed=$false
 PersistentSystemChanges=$false
}
$file=Join-Path $dir ('Photos_WIC_Process_ReadOnly_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+$PID+'.json')
$payload=$report|ConvertTo-Json -Depth 6
$buf=(New-Object Text.UTF8Encoding($false)).GetBytes($payload)
$stream=[IO.File]::Open($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try {$stream.Write($buf,0,$buf.Length)}finally {$stream.Dispose()}
Write-Host 'PHOTOS_WIC_PROCESS_WINDOW_CORRELATION_READONLY_PASS'
Write-Host ('PhotosProcessCandidates='+$processes.Count)
Write-Host ('WindowScan='+$windowScan)
Write-Host ('VisiblePhotosWicCorrelation='+$correlation)
Write-Host "Report=$file"
Write-Host 'This report cannot by itself establish why editing is disabled.'
) -and
 ($_.VisibleTopLevelWindowCount -gt 0 -or $_.VisibleHostedChildWindowCount -gt 0)
})
$visibleWithCustom=@($visiblePhotos | Where-Object {
 @($_.RelevantModuleBasenames) -contains 'wic_heic.dll'
})
$visibleWithoutCustom=@($visiblePhotos | Where-Object {
 @($_.RelevantModuleBasenames) -notcontains 'wic_heic.dll'
})
$correlation='NO_VISIBLE_PHOTOS_PROCESS_OBSERVED'
if($windowScan -ne 'OK') {
 $correlation='WINDOW_SCAN_UNAVAILABLE'
} elseif($visibleWithCustom.Count -gt 0 -and $visibleWithoutCustom.Count -gt 0) {
 $correlation='MIXED_VISIBLE_PHOTOS_PROCESSES'
} elseif($visibleWithCustom.Count -gt 0) {
 $correlation='VISIBLE_PHOTOS_PROCESS_HAS_CUSTOM_WIC_MODULE'
} elseif($visibleWithoutCustom.Count -gt 0) {
 $correlation='VISIBLE_PHOTOS_PROCESS_WITHOUT_CUSTOM_WIC_MODULE'
}
$report=[ordered]@{
 Schema=2
 CollectedAtUtc=[DateTime]::UtcNow.ToString('o')
 WindowsBuild=[string]$os.CurrentBuild
 WindowScan=$windowScan
 PhotosProcessModuleSnapshot=@($processes.ToArray())
 RegisteredCustomWicDllHashes=$wics
 Interpretation='A codec module mapped in one Photos process does not prove it belongs to the visible editor. Window counts are an instantaneous PID correlation, not proof of which codec the editor used or why controls are disabled. No module loaded now is NOT proof that none was used before.'
 FileContentsOrPhotosAccessed=$false
 PersistentSystemChanges=$false
}
$file=Join-Path $dir ('Photos_WIC_Process_ReadOnly_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+$PID+'.json')
$payload=$report|ConvertTo-Json -Depth 6
$buf=(New-Object Text.UTF8Encoding($false)).GetBytes($payload)
$stream=[IO.File]::Open($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try {$stream.Write($buf,0,$buf.Length)}finally {$stream.Dispose()}
Write-Host 'PHOTOS_WIC_PROCESS_WINDOW_CORRELATION_READONLY_PASS'
Write-Host ('PhotosProcessCandidates='+$processes.Count)
Write-Host ('WindowScan='+$windowScan)
Write-Host "Report=$file"
Write-Host 'This report cannot by itself establish why editing is disabled.'
