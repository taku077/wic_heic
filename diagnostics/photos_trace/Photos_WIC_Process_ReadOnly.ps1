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
 $processes.Add([pscustomobject][ordered]@{
  Name=[string]$p.ProcessName
  PID=[int]$p.Id
  ModuleScan=$moduleStatus
  RelevantModuleBasenames=@($identified.ToArray())
 })
}
$report=[ordered]@{
 Schema=1
 CollectedAtUtc=[DateTime]::UtcNow.ToString('o')
 WindowsBuild=[string]$os.CurrentBuild
 PhotosProcessModuleSnapshot=@($processes.ToArray())
 RegisteredCustomWicDllHashes=$wics
 Interpretation='Loaded module proves a sampled process had the DLL mapped. An absent module or inaccessible module list DOES NOT prove Photos never used WIC or HEVC.'
 FileContentsOrPhotosAccessed=$false
 PersistentSystemChanges=$false
}
$file=Join-Path $dir ('Photos_WIC_Process_ReadOnly_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+$PID+'.json')
$payload=$report|ConvertTo-Json -Depth 6
$buf=(New-Object Text.UTF8Encoding($false)).GetBytes($payload)
$stream=[IO.File]::Open($file,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try {$stream.Write($buf,0,$buf.Length)}finally {$stream.Dispose()}
Write-Host 'PHOTOS_WIC_PROCESS_MODULE_SNAPSHOT_READONLY_PASS'
Write-Host ('PhotosProcessCandidates='+$processes.Count)
Write-Host "Report=$file"
Write-Host 'This report cannot by itself establish why editing is disabled.'
