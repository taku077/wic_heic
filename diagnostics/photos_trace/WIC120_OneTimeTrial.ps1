#requires -version 5.1
# One-time trial only. Default mode performs a read-only inventory and writes
# a uniquely named JSON report. Neither install nor restore runs by default.
# Never accesses photos, alters HEIC Manager, v0.4.0, Store or HEVC packages.
[CmdletBinding(DefaultParameterSetName='Check')]
param(
 [Parameter(ParameterSetName='Check')][switch]$CheckOnly,
 [Parameter(ParameterSetName='Install',Mandatory=$true)][switch]$Install,
 [Parameter(ParameterSetName='Restore',Mandatory=$true)][switch]$Restore,
 [Parameter(ParameterSetName='Restore',Mandatory=$true)][string]$Session
)
Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
if($root -notmatch '^[A-Za-z]:\\'){throw 'Local NTFS drive path required'}
function Assert-RealPath([string]$p){
 $node=Get-Item -LiteralPath $p -ErrorAction Stop
 while($null -ne $node) {
  if(($node.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
   throw 'Symlink/junction refused'
  }
  if($node -is [IO.FileInfo]){$node=$node.Directory}else{$node=$node.Parent}
 }
}
Assert-RealPath $root
if(-not [Environment]::Is64BitOperatingSystem){throw 'Windows x64 required'}
$legacy=Join-Path $root 'WicHeic-Repair-1.1.18-ORIGINAL-x64.msi'
$trial=Join-Path $root 'WicHeic-Repair-1.1.20-TRIAL-x64.msi'
$digestPath=Join-Path $root 'WIC120_MSI_SHA256.txt'
$inventory=Join-Path $root 'WIC_Preflight_ReadOnly.ps1'
$comparison=Join-Path $root 'WIC_Preflight_Compare.ps1'
foreach($p in @($legacy,$trial,$digestPath,$inventory,$comparison)){
 if(!(Test-Path -LiteralPath $p -PathType Leaf)){throw "Trial kit file missing: $p"}
 Assert-RealPath $p
}
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToUpperInvariant()}
$oldMsiSha='4FE1809A98C0B7EB7E7A505A6105BE57A34052DE74D79A13DB0853A9EC2AACE6'
$oldDllSha='9E9D2F17038C03A5BF8840A8BD43BC4FDA19C91E947C4CD5904A8209051F93D4'
$newSha=(Get-Content -LiteralPath $digestPath -Raw -Encoding ASCII).Trim().ToUpperInvariant()
if($newSha -notmatch '^[0-9A-F]{64}$'){throw 'Invalid candidate MSI digest'}
if((Hash $legacy) -cne $oldMsiSha){throw 'Original 1.1.18 MSI is NOT the known exact restore copy'}
if((Hash $trial) -cne $newSha){throw '1.1.20 trial MSI differs from its qualified SHA-256'}
function ReadState {
 $path=(& $inventory | Select-Object -Last 1)
 if(!($path -and (Test-Path -LiteralPath $path -PathType Leaf))){throw 'WIC read-only inventory unavailable'}
 $j=Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
 if($j.Schema -ne 1 -or @($j.Registrations).Count -ne 12){throw 'WIC inventory incomplete'}
 return [pscustomobject]@{Path=[string]$path;Data=$j}
}
function GlobalState([object]$report,[string]$version,[string]$expectedDllHash) {
 $isIsolatedCI=($env:GITHUB_ACTIONS -eq 'true' -and
    -not [string]::IsNullOrWhiteSpace($env:RUNNER_TEMP))
 if([string]$report.Windows.CurrentBuild -ne '19045' -and -not $isIsolatedCI){
  throw 'Trial qualified only for Windows 10 build 19045'
 }
 $others=@($report.WicHeicMsi | Where-Object {
  -not ($_.Name -eq 'WicHeic Fast Thumbnail Preview (Experimental)' -and $_.Version -eq '0.4.0')
 })
 if($others.Count -ne 1 -or $others[0].Name -ne 'WicHeic Repair Test' -or
    $others[0].Version -ne $version){
  throw "Unexpected global WIC/MSI state, expected $version. No changes allowed"
 }
 $preview=@($report.WicHeicMsi | Where-Object {$_.Name -eq 'WicHeic Fast Thumbnail Preview (Experimental)'})
 if($preview.Count -gt 1 -or @($preview | Where-Object {$_.Version -ne '0.4.0'}).Count){
  throw 'Unknown FastPreview MSI: cannot prove independent coexistence'
 }
 $entries=@($report.Registrations | Where-Object {
  $_.Hive -eq 'LocalMachine' -and $_.View -eq 'Registry64'
 })
 if($entries.Count -ne 2){throw 'WIC x64 decoder/encoder registration incomplete'}
 if(@($entries | Where-Object {
  [string]::IsNullOrWhiteSpace($_.InprocServer32) -or
  $_.SHA256 -cne $expectedDllHash
 }).Count -ne 0){throw "Expected $version WIC DLL SHA-256 not registered"}
 # Prevent unrecognized per-user / x86 registrations from being changed by MSI.
 foreach($e in $report.Registrations) {
  if($e.Hive -eq 'LocalMachine' -and $e.View -eq 'Registry64'){continue}
  if($e.Hive -eq 'ClassesRoot' -and $e.View -eq 'Registry64' -and
     $e.SHA256 -ceq $expectedDllHash){continue}
  if(-not [string]::IsNullOrWhiteSpace($e.InprocServer32)){
   throw 'Unrecognized additional WIC registration; stop before modifying Windows'
  }
 }
}
function PreviewState {
 $keys=@(
  'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{D973196C-165C-4FCE-8838-17C37037D384}\InprocServer32',
  'Registry::HKEY_CURRENT_USER\Software\Classes\.heic\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}',
  'Registry::HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\.heic\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}'
 )
 $values=@()
 foreach($p in $keys){
  $k=Get-Item -LiteralPath $p -ErrorAction SilentlyContinue
  $values+=if($k){[string]$k.GetValue('')}else{'ABSENT'}
 }
 if($values[0] -ne 'ABSENT'){
  $dll=[Environment]::ExpandEnvironmentVariables($values[0]).Trim('"')
  if(!(Test-Path -LiteralPath $dll -PathType Leaf)){throw 'Preview handler DLL missing'}
  $codec=Join-Path (Split-Path -Parent $dll) 'WicHeicPrivateCodec.dll'
  if(!(Test-Path -LiteralPath $codec -PathType Leaf)){throw 'Private preview codec missing'}
  $values+=@((Hash $dll),(Hash $codec))
 }
 return ($values -join '|')
}
function AdminRequired {
 $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
 $principal=New-Object Security.Principal.WindowsPrincipal($identity)
 if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
  throw 'Run this PowerShell as administrator ONLY after explicitly choosing -Install or -Restore'
 }
}
function Msi([string]$op,[string]$file,[string]$tag){
 $log=Join-Path $root ('WIC120_'+$tag+'_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'.log')
 $ui=if($env:GITHUB_ACTIONS -eq 'true' -and $env:RUNNER_TEMP){'/qn'}else{'/passive'}
 $argsText='/'+$op+' "'+$file+'" '+$ui+' /norestart /l*v "'+$log+'"'
 $proc=Start-Process -FilePath 'msiexec.exe' -ArgumentList $argsText -Wait -PassThru
 if($proc.ExitCode -notin @(0,3010)){throw "MSI $op failed ($($proc.ExitCode)); inspect $log"}
 Write-Host "MSI_$tag EXIT=$($proc.ExitCode) LOG=$log"
 if($proc.ExitCode -eq 3010){Write-Warning 'Restart is required, do not edit photos before reboot'}
}
$state=ReadState
$preview=PreviewState
if($PSCmdlet.ParameterSetName -eq 'Check') {
 GlobalState $state.Data '1.1.18' $oldDllSha
 Write-Host 'WIC120_PRECHECK_OLD118_EXACT_MATCH_PASS'
 Write-Host 'NO_INSTALL_NO_REGISTRY_CHANGES_NO_PHOTOS_READ'
 Write-Host ('PRECHECK_JSON='+$state.Path)
 return
}
AdminRequired
if($Install) {
 GlobalState $state.Data '1.1.18' $oldDllSha
 $sessionPath=Join-Path $root ('WIC120_Session_'+[guid]::NewGuid().ToString('N')+'.json')
 $sessionData=[ordered]@{
  Schema=1;OriginalSnapshot=$state.Path;OriginalPreview=$preview
  ExpectedOldDllSha=$oldDllSha;ExpectedNewMsiSha=$newSha
  StartedUtc=[DateTime]::UtcNow.ToString('o')
 }
 $data=($sessionData | ConvertTo-Json -Depth 5)
 $buf=(New-Object System.Text.UTF8Encoding($false)).GetBytes($data)
 $f=[IO.File]::Open($sessionPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
 try{$f.Write($buf,0,$buf.Length)}finally{$f.Dispose()}
 Write-Host "SESSION_SAVED=$sessionPath"
 Msi 'i' $trial 'Install120'
 $after=ReadState
 # Candidate may have a different install location but must retain its
 # exact 1.1.20 version and match its WIC DLL present in the installation.
 $newRegs=@($after.Data.Registrations | Where-Object {
  $_.Hive -eq 'LocalMachine' -and $_.View -eq 'Registry64'
 })
 if($newRegs.Count -ne 2 -or -not $newRegs[0].SHA256 -or
    $newRegs[0].SHA256 -cne $newRegs[1].SHA256){
  throw "1.1.20 installed but decoder/encoder DLLs not equal. Restore using -Restore -Session $sessionPath"
 }
 GlobalState $after.Data '1.1.20' ([string]$newRegs[0].SHA256)
 if((PreviewState) -cne $preview){throw 'v0.4 preview changed; trial NOT qualified'}
 Write-Host 'WIC120_INSTALL_VERIFIED_WINDOWS10_PHOTOS_UI_UNTESTED'
 Write-Host "Restore (after testing): .\WIC120_OneTimeTrial.ps1 -Restore -Session '$sessionPath'"
 return
}
if($Restore){
 if(-not (Test-Path -LiteralPath $Session -PathType Leaf)){throw 'Session JSON missing'}
 Assert-RealPath $Session
 if((Split-Path -Parent (Resolve-Path -LiteralPath $Session).Path) -cne $root){
  throw 'Session must be original JSON inside this exact trial kit'
 }
 $saved=Get-Content -LiteralPath $Session -Raw -Encoding UTF8 | ConvertFrom-Json
 if($saved.Schema -ne 1 -or $saved.ExpectedOldDllSha -cne $oldDllSha -or
    $saved.ExpectedNewMsiSha -cne $newSha){throw 'Unknown or mismatched trial session'}
 if(-not (Test-Path -LiteralPath $saved.OriginalSnapshot -PathType Leaf)){
  throw 'Original baseline report missing; restoration blocked'
 }
 if($preview -cne $saved.OriginalPreview){throw 'FastPreview differs from session baseline; abort'}
 $newRegs=@($state.Data.Registrations | Where-Object {
  $_.Hive -eq 'LocalMachine' -and $_.View -eq 'Registry64'
 })
 if($newRegs.Count -ne 2 -or !$newRegs[0].SHA256 -or
    $newRegs[0].SHA256 -cne $newRegs[1].SHA256){
  throw 'Unexpected decoder/encoder state; restoration requires manual review'
 }
 GlobalState $state.Data '1.1.20' ([string]$newRegs[0].SHA256)
 Msi 'x' $trial 'Remove120'
 $none=ReadState
 $others=@($none.Data.WicHeicMsi | Where-Object {$_.Name -ne 'WicHeic Fast Thumbnail Preview (Experimental)'})
 if($others.Count -ne 0 -or
    @($none.Data.Registrations|Where-Object {
      -not [string]::IsNullOrWhiteSpace($_.InprocServer32)
    }).Count -ne 0){throw 'Unknown WIC present after removal. Do not reinstall over it'}
 Msi 'i' $legacy 'Restore118'
 $final=ReadState
 GlobalState $final.Data '1.1.18' $oldDllSha
 if((PreviewState) -cne $saved.OriginalPreview){throw 'Preview changed during restoration'}
 & $comparison -Before $saved.OriginalSnapshot -After $final.Path
 if(-not $?){throw 'Exact registry/DLL/MSI restore comparison failed'}
 Write-Host 'WIC120_USER_BASELINE_118_FULL_RESTORE_PASS'
 Write-Host ('RESTORED_SNAPSHOT='+$final.Path)
 return
}
