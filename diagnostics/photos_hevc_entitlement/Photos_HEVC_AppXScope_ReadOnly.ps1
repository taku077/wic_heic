# Windows PowerShell 5.1-compatible, read-only HEVC AppX recovery-scope inventory.
# This DOES NOT install, download, repair, activate licenses or modify Windows.
[CmdletBinding()]
param([switch]$SelfTest)
Set-StrictMode -Version 2.0
$ErrorActionPreference='Stop'
function Prop([object]$obj,[string]$name) {
  if($null -eq $obj){return $null}
  $p=$obj.PSObject.Properties[$name]
  if($null -eq $p){return $null}
  return $p.Value
}
function Classify([object[]]$current,[object[]]$other,[bool]$complete) {
  $hasCurrent=@($current|Where-Object {([string](Prop $_ 'Name')) -match '(?i)HEVC|H265'}).Count -gt 0
  $hasOther=@($other|Where-Object {([string](Prop $_ 'Name')) -match '(?i)HEVC|H265'}).Count -gt 0
  if($hasCurrent){return 'HEVC_APPX_CURRENT_USER_PRESENT'}
  if($complete -and $hasOther){return 'HEVC_APPX_OTHER_USER_OR_PROVISIONED_ONLY'}
  if($complete){return 'HEVC_APPX_NOT_REGISTERED_IN_CHECKED_SCOPES'}
  return 'HEVC_APPX_CURRENT_USER_MISSING_OTHER_SCOPES_UNKNOWN'
}
function ShortPackages([object[]]$packages) {
  @($packages | Where-Object {
    ([string](Prop $_ 'Name')) -match '(?i)HEVC|H265|HEIF'
  } | ForEach-Object {
    [pscustomobject][ordered]@{
      Name=[string](Prop $_ 'Name')
      Version=[string](Prop $_ 'Version')
      Status=[string](Prop $_ 'Status')
    }
  })
}
if($SelfTest) {
  $hevc=[pscustomobject]@{Name='Microsoft.HEVCVideoExtension'}
  $heif=[pscustomobject]@{Name='Microsoft.HEIFImageExtension'}
  if((Classify @() @() $true) -cne 'HEVC_APPX_NOT_REGISTERED_IN_CHECKED_SCOPES'){throw 'Empty scopes'}
  if((Classify @($heif) @() $false) -cne 'HEVC_APPX_CURRENT_USER_MISSING_OTHER_SCOPES_UNKNOWN'){throw 'Unknown scopes'}
  if((Classify @($hevc) @() $false) -cne 'HEVC_APPX_CURRENT_USER_PRESENT'){throw 'Current user'}
  if((Classify @() @($hevc) $true) -cne 'HEVC_APPX_OTHER_USER_OR_PROVISIONED_ONLY'){throw 'Other scope'}
  if($null -ne (Prop $hevc 'Nonexistent')){throw 'Missing property'}
  Write-Output 'HEVC_READONLY_CLASSIFICATION_SELFTEST_PASS'
  return
}
$warn=New-Object 'System.Collections.Generic.List[string]'
$current=@();$all=@();$provisioned=@();$photos=@()
$readCurrent=$false;$readAll=$false;$readProvisioned=$false
try{$current=@(Get-AppxPackage -ErrorAction Stop);$readCurrent=$true}
catch{$warn.Add('CURRENT_USER_APPX_UNREADABLE')}
try{$all=@(Get-AppxPackage -AllUsers -ErrorAction Stop);$readAll=$true}
catch{$warn.Add('ALL_USERS_APPX_UNREADABLE_OR_ACCESS_DENIED')}
try{$provisioned=@(Get-AppxProvisionedPackage -Online -ErrorAction Stop);$readProvisioned=$true}
catch{$warn.Add('PROVISIONED_APPX_UNREADABLE_OR_ACCESS_DENIED')}
try{$photos=@(Get-AppxPackage -Name Microsoft.Windows.Photos -ErrorAction Stop)}
catch{$warn.Add('PHOTOS_APPX_UNREADABLE')}
$other=@($all)+@($provisioned)
$status=Classify $current $other ($readAll -and $readProvisioned)
$os=Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction SilentlyContinue
$report=[ordered]@{
 Schema=1
 CollectedAtUtc=[DateTime]::UtcNow.ToString('o')
 Scope='HEVC AppX user/all-user/provisioning scope only. The previous MFT/WIC/photo tests are not repeated.'
 Windows=[ordered]@{
   Build=[string](Prop $os 'CurrentBuild')
   DisplayVersion=[string](Prop $os 'DisplayVersion')
 }
 PhotosVersion=@($photos|ForEach-Object{[string](Prop $_ 'Version')})
 Classification=$status
 CurrentUserAppx=@(ShortPackages $current)
 AllUsersAppx=@(ShortPackages $all)
 ProvisionedAppx=@(ShortPackages $provisioned)
 ScopeReadable=[ordered]@{CurrentUser=$readCurrent;AllUsers=$readAll;Provisioned=$readProvisioned}
 HEVCDllPresence=[ordered]@{
  InBox=(Test-Path -LiteralPath (Join-Path $env:WINDIR 'System32\hevcdecoder.dll') -PathType Leaf)
  Store=(Test-Path -LiteralPath (Join-Path $env:WINDIR 'System32\hevcdecoder_store.dll') -PathType Leaf)
 }
 PreviouslyObserved=[ordered]@{
   AtUtc='2026-10-22T17:08:26.1789087Z'
   MFTHEVCDecoder=0
   GlobalWic='1.1.18'
   Note='Previously uploaded diagnostic, NOT re-measured in this report'
 }
 Limits='Package enumeration cannot establish purchase rights, effective license, decoder activation, or Windows Photos GUI eligibility'
 Warnings=@($warn.ToArray())
 NoWindowsChanges=$true
}
$name='Photos_HEVC_AppXScope_ReadOnly_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+$PID+'.json'
$target=Join-Path $PSScriptRoot $name
$bytes=(New-Object System.Text.UTF8Encoding($false)).GetBytes(
  (ConvertTo-Json -InputObject $report -Depth 7))
$file=[System.IO.File]::Open($target,[System.IO.FileMode]::CreateNew,
  [System.IO.FileAccess]::Write,[System.IO.FileShare]::Read)
try{$file.Write($bytes,0,$bytes.Length)}finally{$file.Dispose()}
Write-Output $target
