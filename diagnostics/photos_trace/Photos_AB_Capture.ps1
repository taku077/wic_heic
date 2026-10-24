# Windows 10 Photos real-GUI A/B comparator, phase A=original 1.1.18, B=research.
# Built-in PowerShell 5.1, normal user. No MSI, registry writes, or personal images.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('A','B')][string]$Phase)
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
$rootNode=Get-Item -LiteralPath $root
while($null -ne $rootNode){
  if(($rootNode.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
    throw 'Junction or symlink folder prohibited'
  }
  $rootNode=$rootNode.Parent
}
$fixture=Join-Path $root 'fixture-irot3-exif6-noaux.heic'
$exe=Join-Path $root 'HevcMfReadOnlyProbe.exe'
$expected='D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95'
if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){throw 'Probe executable missing'}
if(-not (Test-Path -LiteralPath $fixture -PathType Leaf)){throw 'Pinned synthetic HEIC missing'}
if((Get-Item -LiteralPath $fixture).Length -ne 10597 -or
   (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -cne $expected){
  throw 'Synthetic HEIC fixture failed exact hash check'
}
$sessionRoot=Join-Path $root 'Photos_AB_Results'
if(-not (Test-Path -LiteralPath $sessionRoot)){
  New-Item -ItemType Directory -Path $sessionRoot -ErrorAction Stop | Out-Null
}
if((Get-Item -LiteralPath $sessionRoot).Attributes -band [IO.FileAttributes]::ReparsePoint){
  throw 'Results directory cannot be a junction'
}
$reportFile=Join-Path $sessionRoot ('Photos_AB_'+$Phase+'.json')
$workDir=Join-Path $sessionRoot ('Photos_AB_'+$Phase+'_Synthetic')
if(Test-Path -LiteralPath $reportFile){throw "Phase $Phase already recorded. No overwrite; use a fresh kit for another comparison."}
if(Test-Path -LiteralPath $workDir){throw 'Existing synthetic session found; no overwrite'}
$null=New-Item -ItemType Directory -Path $workDir
$work=Join-Path $workDir 'synthetic-working.heic'
$reference=Join-Path $workDir 'synthetic-original-READONLY.heic'
foreach($dest in @($work,$reference)){
  $source=[IO.File]::Open($fixture,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  $target=$null
  try {
    $target=[IO.File]::Open($dest,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $source.CopyTo($target)
  } finally {
    if($null -ne $target){$target.Dispose()}
    $source.Dispose()
  }
}
[IO.File]::SetAttributes($reference,[IO.FileAttributes]::ReadOnly)
Write-Host ''
Write-Host ('=== Windows Photos A/B | phase '+$Phase+' ===') -ForegroundColor Cyan
if($Phase -eq 'A'){Write-Host 'A: historical WIC 1.1.18 should be installed'}
else{Write-Host 'B: RESEARCH WIC should be installed (changed separately with approved rollback)'}
Write-Host 'The tool will NOT install or remove anything.'
Write-Host 'Open only this artificial working copy in Microsoft Photos:'
Write-Host ('  '+$work) -ForegroundColor Yellow
Write-Host 'Do not open or rotate any of your own photographs.'
$null=Read-Host 'When Photos shows THIS synthetic HEIC, press Enter'

function Choice([string]$message,[string[]]$valid) {
  for($i=0;$i -lt 4;$i++){
    $value=(Read-Host $message).Trim().ToLowerInvariant()
    if($valid -contains $value){return $value}
    Write-Host ('Enter one of: '+($valid -join ', ')) -ForegroundColor Yellow
  }
  throw 'Unable to record an unambiguous result'
}
Write-Host 'Please answer from actual Photos UI; no screenshots or image data are collected.'
$warning=Choice 'HEVC warning visible? (y=yes,n=no,?=unknown)' @('y','n','?')
$editing=Choice 'Edit button usable? (y=yes,n=no,?=unknown)' @('y','n','?')
$rotation=Choice 'Rotate button usable? (y=yes,n=no,?=unknown)' @('y','n','?')
Write-Host 'If rotation is available, use it ONLY on the working synthetic copy.'
$null=Read-Host 'After optional rotate / save attempt, press Enter'
$save=Choice 'Rotate/save result? (s=success,f=failure,x=not attempted,?=unknown)' @('s','f','x','?')
$distorted=Choice 'Visible geometry/angle distortion? (y=yes,n=no,?=unknown)' @('y','n','?')

# Work-copy hash is recorded AFTER the manual Photos action.
$referenceOk=(Get-FileHash -LiteralPath $reference -Algorithm SHA256).Hash -ceq $expected
$fixtureOk=(Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -ceq $expected
$working=if(Test-Path -LiteralPath $work -PathType Leaf) {
  $file=Get-Item -LiteralPath $work
  [ordered]@{Exists=$true;Bytes=[int64]$file.Length;
    Sha256=(Get-FileHash -LiteralPath $work -Algorithm SHA256).Hash}
} else {[ordered]@{Exists=$false;Bytes=0;Sha256=$null}}
if(-not $fixtureOk -or -not $referenceOk){throw 'Protected synthetic reference changed; refuse to report success'}

# Snapshot ONLY selected public app/package versions and DLL hashes, never paths.
$packages=[ordered]@{}
foreach($package in @('Microsoft.Windows.Photos','Microsoft.HEIFImageExtension',
  'Microsoft.HEVCVideoExtension','Microsoft.HEVCVideoExtensions')) {
  $found=@(Get-AppxPackage -Name $package -ErrorAction SilentlyContinue)
  $packages[$package]=if($found.Count){
    @($found|ForEach-Object { [string]$_.Version })
  }else{@()}
}
$photosModules=New-Object 'System.Collections.Generic.List[object]'
$photoProcesses=@(Get-Process -Name 'Photos','Microsoft.Photos' -ErrorAction SilentlyContinue)
foreach($process in $photoProcesses) {
  $modules=@()
  try{$modules=@($process.Modules)}catch{}
  $wic=@($modules|Where-Object {$_.ModuleName -ieq 'wic_heic.dll'})
  $codecs=@($modules|Where-Object {$_.ModuleName -ieq 'windowscodecs.dll'})
  $hashes=@()
  foreach($module in $wic) {
    try {
      $hashes+=((Get-FileHash -LiteralPath $module.FileName -Algorithm SHA256 -ErrorAction Stop).Hash)
    }catch{$hashes+=('UNREADABLE')}
  }
  $photosModules.Add([ordered]@{WicHeicLoaded=($wic.Count -gt 0);
    WindowsCodecsLoaded=($codecs.Count -gt 0);WicHeicDllSha256=@($hashes);
    ModulesReadable=($modules.Count -gt 0) })
}
# Run the same public HEIC through Windows' WIC/MF API. It does not read
# photos or write images; keep only allowlisted event codes.
$probeEvents=New-Object 'System.Collections.Generic.List[string]'
$probeStatus='NOT_RUN'
try {
  $raw=@(& $exe '--synthetic-wic-test' 2>&1)
  $code=$LASTEXITCODE
  foreach($line in $raw) {
    $item=[string]$line
    if($item.Length -le 350 -and
       $item -match '^HEVC_SYNTHETIC_(EXPLICIT|AUTO|ENCODER) ' -and
       $item -notmatch '(?i)(file|path|exif|metadata|fnv64|pid)=') {
      $probeEvents.Add($item)
    }
  }
  $auth=@($raw|Where-Object {
    [string]$_ -match '^HEVC_SYNTHETIC_GIT_BLOB_VERIFIED_PASS bytes=10597'})
  $probeStatus=if($code -eq 0 -and $auth.Count){'PASS'}else{'INCOMPLETE'}
}catch{$probeStatus='FAILED'}
$os=Get-CimInstance Win32_OperatingSystem
$report=[ordered]@{
  Schema=1;Phase=$Phase;CollectedUtc=[DateTime]::UtcNow.ToString('o')
  WindowsBuild=[string]$os.BuildNumber
  Packages=$packages
  PhotosProcessCount=$photoProcesses.Count
  PhotosModules=@($photosModules.ToArray())
  GuiObservation=[ordered]@{HevcWarning=$warning;EditUsable=$editing;
    RotateUsable=$rotation;SaveResult=$save;VisibleDistortion=$distorted}
  SyntheticReferenceSha256=$expected
  WorkingCopy=$working
  CodecProbeStatus=$probeStatus
  CodecProbeEvents=@($probeEvents.ToArray())
  UserFilesAccessed=$false
  RegistryOrAppxModified=$false
}
$json=$report|ConvertTo-Json -Depth 9
$bytes=[Text.Encoding]::UTF8.GetBytes($json)
$out=[IO.File]::Open($reportFile,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try {$out.Write($bytes,0,$bytes.Length)}finally{$out.Dispose()}
Write-Host ('Phase '+$Phase+' saved; original synthetic verified. No MSI/registry modifications.') -ForegroundColor Green
Write-Host ('Report: '+$reportFile)
if($Phase -eq 'B'){Write-Host 'Now run 03_Compare.cmd for the A/B cause-difference report.'}
