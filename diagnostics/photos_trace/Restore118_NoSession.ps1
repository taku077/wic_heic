#requires -version 5.1
# SESSIONLESS INCIDENT RECOVERY for known research WIC 1.1.20 to exact old 1.1.18.
# Never auto-run. Uses only SHA256-pinned MSI/DLL and constrained WIC registry/MSI identity.
# No photo reads. No registry direct edits. No other application package changes.
[CmdletBinding()]
param([switch]$Restore)
Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
if($root -notmatch '^[A-Za-z]:\\'){throw 'Local drive required'}
$node=Get-Item -LiteralPath $root
while($null -ne $node){
 if(($node.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
  throw 'Folder is a junction or symlink; abort'
 }
 $node=$node.Parent
}
if(-not [Environment]::Is64BitOperatingSystem){throw 'Windows x64 required'}
$original=Join-Path $root 'WicHeic-Repair-1.1.18-ORIGINAL-x64.msi'
$inventory=Join-Path $root 'WIC_Preflight_ReadOnly.ps1'
if(!(Test-Path -LiteralPath $original -PathType Leaf) -or
   !(Test-Path -LiteralPath $inventory -PathType Leaf)){
 throw 'Recovery files missing; no MSI changes'
}
$sourceMsiHash='4FE1809A98C0B7EB7E7A505A6105BE57A34052DE74D79A13DB0853A9EC2AACE6'
$oldDllHash='9E9D2F17038C03A5BF8840A8BD43BC4FDA19C91E947C4CD5904A8209051F93D4'
$acceptedTrialHashes=@(
 'A38440C81DAF406B188DBCB3CB28E8CC85BA7312F076A2C8F3E03E7F108687E5',
 'F46D0BE0B4F9CA9636C7C3C3BFE546E9F1AD056CE44DC9F6FAF84F725F468AFE'
)
$oldCode='{A9F264DD-92D3-419B-8CA0-A88B00FA42F3}'
function Hash([string]$file) {
 return (Get-FileHash -LiteralPath $file -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant()
}
if((Hash $original) -cne $sourceMsiHash){
 throw 'Exact old 1.1.18 MSI hash mismatch. Nothing will be changed.'
}
function ReadState {
 $path=(& $inventory|Select-Object -Last 1)
 if(-not $path -or -not (Test-Path -LiteralPath $path -PathType Leaf)){
  throw 'Preflight inventory failed'
 }
 $data=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json
 if($data.Schema -ne 1 -or @($data.Registrations).Count -ne 12){
  throw 'Preflight incomplete; do not change MSI'
 }
 return [pscustomobject]@{Path=$path; Data=$data}
}
function GlobalEntries($s) {
 return @($s.Registrations|Where-Object {
  $_.Hive -eq 'LocalMachine' -and $_.View -eq 'Registry64'
 })
}
function CheckGlobal($s,[string]$version,[string]$expected) {
 if([string]$s.Windows.CurrentBuild -cne '19045' -and
    $env:GITHUB_ACTIONS -cne 'true'){
  throw 'Only Windows 10 build 19045 is supported'
 }
 $global=@($s.WicHeicMsi|Where-Object {
   $_.Name -ne 'WicHeic Fast Thumbnail Preview (Experimental)'
 })
 if($global.Count -ne 1 -or
   [string]$global[0].Name -cne 'WicHeic Repair Test' -or
   [string]$global[0].Version -cne $version){
  throw ('WIC MSI name/version mismatch; expected '+$version)
 }
 if(([string]$global[0].ProductKey) -notmatch '^\{[0-9a-fA-F-]{36}\}$'){
  throw 'Unknown MSI ProductCode'
 }
 $preview=@($s.WicHeicMsi|Where-Object {
   $_.Name -eq 'WicHeic Fast Thumbnail Preview (Experimental)'
 })
 if($preview.Count -gt 1 -or
    @($preview|Where-Object {$_.Version -ne '0.4.0'}).Count){
  throw 'Unknown private thumbnail handler; refuse MSI operation'
 }
 $entries=GlobalEntries $s
 if($entries.Count -ne 2){throw 'Missing x64 WIC registration'}
 foreach($e in $entries) {
  if($e.SHA256 -cne $expected -or
     [string]::IsNullOrWhiteSpace([string]$e.InprocServer32)){
   throw ('Installed WIC DLL does not match expected '+$version)
  }
 }
 foreach($e in $s.Registrations) {
  if($e.Hive -eq 'LocalMachine' -and $e.View -eq 'Registry64'){continue}
  if($e.Hive -eq 'ClassesRoot' -and $e.View -eq 'Registry64' -and
     $e.SHA256 -ceq $expected){continue}
  if(-not [string]::IsNullOrWhiteSpace([string]$e.InprocServer32)){
   throw 'Additional x86/HKCU WIC registration is present; abort'
  }
 }
 if($version -eq '1.1.18' -and
    ([string]$global[0].ProductKey).ToUpperInvariant() -cne $oldCode){
  throw 'Restored old WIC has wrong ProductCode'
 }
 return [string]$global[0].ProductKey
}
function PreviewFingerprint {
 # Preserve independent preview/Shell settings exactly, including absence.
 $keys=@(
  'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{D973196C-165C-4FCE-8838-17C37037D384}\InprocServer32',
  'Registry::HKEY_CURRENT_USER\Software\Classes\.heic\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}',
  'Registry::HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\.heic\ShellEx\{E357FCCD-A995-4576-B01F-234630154E96}'
 )
 $r=@()
 foreach($k in $keys) {
  $item=Get-Item -LiteralPath $k -ErrorAction SilentlyContinue
  $r+=if($item){[string]$item.GetValue('')}else{'ABSENT'}
 }
 if($r[0] -ne 'ABSENT') {
  $dll=[Environment]::ExpandEnvironmentVariables($r[0]).Trim('"')
  $codec=Join-Path (Split-Path -Parent $dll) 'WicHeicPrivateCodec.dll'
  if(!(Test-Path -LiteralPath $dll -PathType Leaf) -or
     !(Test-Path -LiteralPath $codec -PathType Leaf)){
   throw 'Unknown private preview DLL state'
  }
  $r+=@((Hash $dll),(Hash $codec))
 }
 return ($r -join '|')
}
function InteractiveShell {
 if($env:GITHUB_ACTIONS -eq 'true' -and $env:RUNNER_TEMP){return}
 $session=[Diagnostics.Process]::GetCurrentProcess().SessionId
 $explorer=@(Get-Process -Name explorer -ErrorAction SilentlyContinue|
   Where-Object {$_.SessionId -eq $session})
 if($explorer.Count -eq 0) {
  throw 'Explorer is not running. Restore it using Task Manager before MSI.'
 }
 if(@(Get-Process -Name Photos,Microsoft.Photos -ErrorAction SilentlyContinue).Count){
  throw 'Close Microsoft Photos windows before recovery'
 }
}
function RunMsi([string]$mode,[string]$target,[string]$name) {
 $log=Join-Path $root ('WIC_Recovery_'+$name+'_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'.log')
 $ui=if($env:GITHUB_ACTIONS -eq 'true' -and $env:RUNNER_TEMP){'/qn'}else{'/passive'}
 # No Restart Manager automatic Explorer termination, including uninstall.
 $args='/'+$mode+' "'+$target+'" '+$ui+' MSIRESTARTMANAGERCONTROL=Disable /norestart /l*v "'+$log+'"'
 $p=Start-Process -FilePath 'msiexec.exe' -ArgumentList $args -PassThru -Wait
 Write-Host ('MSI_'+$name+'_EXIT='+$p.ExitCode+' LOG='+$log)
 if($p.ExitCode -eq 3010) {
  throw 'MSI_REBOOT_REQUIRED: do not continue. Restart Windows manually and inspect installation.'
 }
 if($p.ExitCode -ne 0) {
  throw ('MSI_'+$name+'_FAILED '+$p.ExitCode+'. Do not retry arbitrary MSI installs.')
 }
 InteractiveShell
}
$before=ReadState
$preview=PreviewFingerprint
$globals=@($before.Data.WicHeicMsi|Where-Object {
 $_.Name -ne 'WicHeic Fast Thumbnail Preview (Experimental)'
})
if($globals.Count -ne 1){throw 'Unexpected WIC MSI count. No changes made.'}
if($globals[0].Version -eq '1.1.18'){
 $null=CheckGlobal $before.Data '1.1.18' $oldDllHash
 Write-Output 'WIC_118_ALREADY_RESTORED_NO_CHANGES'
 return
}
if($globals[0].Version -ne '1.1.20'){
 throw 'Neither exact old118 nor supported B120 installed. No changes.'
}
$registry=GlobalEntries $before.Data
if($registry.Count -ne 2 -or
   $registry[0].SHA256 -cne $registry[1].SHA256 -or
   $acceptedTrialHashes -cnotcontains [string]$registry[0].SHA256){
 throw 'Unknown B DLL hash. Restore refused. No changes.'
}
$trialHash=[string]$registry[0].SHA256
$product=CheckGlobal $before.Data '1.1.20' $trialHash
Write-Output ('QUALIFIED_B_1.1.20_INSTALLED_HASH='+$trialHash)
if(-not $Restore) {
 Write-Output 'READ_ONLY_PRECHECK_PASS - To restore, run 01_Restore_Original118_Admin.cmd as administrator.'
 return
}
$principal=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
 throw 'Administrator rights required for approved MSI recovery'
}
InteractiveShell
# Before changing anything, re-check exact original package identity.
$engine=New-Object -ComObject WindowsInstaller.Installer
$db=$engine.OpenDatabase($original,0)
function MSIProperty([string]$name) {
 $q=$db.OpenView("SELECT Value FROM Property WHERE Property = '$name'")
 try {$null=$q.Execute(); $x=$q.Fetch();return $(if($x){[string]$x.StringData(1)}else{''})}
 finally {$q.Close()}
}
$msiName=[string](MSIProperty 'ProductName')
$msiVersion=[string](MSIProperty 'ProductVersion')
$msiCode=([string](MSIProperty 'ProductCode')).ToUpperInvariant()
Write-Output ('RESTORE_MSI_READONLY_IDENTITY name='+$msiName+' version='+$msiVersion+' code='+$msiCode)
if($msiName -cne 'WicHeic Repair Test' -or $msiVersion -cne '1.1.18' -or
   $msiCode -cne $oldCode){
 throw 'Original MSI identity mismatch; no changes'
}
$db=$null;$engine=$null
Write-Host ('SAFE_RESTORE_SOURCE='+$before.Path)
Write-Host 'Starting exact B removal, then original 1.1.18 install.'
RunMsi 'x' $product 'RemoveKnownB120'
$afterRemove=ReadState
$remaining=@($afterRemove.Data.WicHeicMsi|Where-Object {
 $_.Name -ne 'WicHeic Fast Thumbnail Preview (Experimental)'
})
if($remaining.Count -ne 0){throw 'Known B removal incomplete; stop before old installation'}
if(@($afterRemove.Data.Registrations|Where-Object {
 -not [string]::IsNullOrWhiteSpace([string]$_.InprocServer32)
}).Count -ne 0){throw 'WIC COM registration remains after uninstall; stop'}
if((PreviewFingerprint) -cne $preview){throw 'Preview state changed unexpectedly; stop'}
RunMsi 'i' $original 'InstallOriginal118'
$after=ReadState
$null=CheckGlobal $after.Data '1.1.18' $oldDllHash
if((PreviewFingerprint) -cne $preview){throw 'Preview state changed during recovery'}
Write-Host 'WIC118_SESSIONLESS_RESTORE_VERIFIED_PASS' -ForegroundColor Green
Write-Host ('ORIGINAL118_RESTORED_SNAPSHOT='+$after.Path)
