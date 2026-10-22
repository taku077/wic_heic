#requires -version 5.1
# Explicit opt-in rollback of ONLY the experimental global HEIC WIC MSI.
# Preserves independent Fast Thumbnail Preview v0.4.0. No user images accessed.
param([switch]$CheckOnly)
$ErrorActionPreference='Stop'
$dir=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
if($dir -notmatch '^[A-Za-z]:\\') {throw 'Extract this candidate to a local disk folder.'}
$p=Get-Item -LiteralPath $dir
while($p){
 if(($p.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
  throw 'Symlink/junction paths are not supported.'
 }
 $p=$p.Parent
}
$msi=Join-Path $dir 'WicHeic-Repair-EXPERIMENTAL-x64.msi'
$hashFile=Join-Path $dir 'EXPECTED_GLOBAL_WIC_DLL_SHA256.txt'
$preflight=Join-Path $dir 'WIC_Preflight_ReadOnly.ps1'
foreach($file in @($msi,$hashFile,$preflight)){
 if(!(Test-Path -LiteralPath $file -PathType Leaf)){
  throw "Missing rollback prerequisite: $([IO.Path]::GetFileName($file))"
 }
 if(((Get-Item -LiteralPath $file).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
  throw 'Linked rollback input refused.'
 }
}
$expected=(Get-Content -LiteralPath $hashFile -Encoding ASCII -Raw).Trim()
if($expected -notmatch '^[A-Fa-f0-9]{64}$'){throw 'Invalid expected global DLL SHA256.'}
function PreviewState {
 $cls='{D973196C-165C-4FCE-8838-17C37037D384}'
 $shell='{E357FCCD-A995-4576-B01F-234630154E96}'
 $path="Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\$cls\InprocServer32"
 $k=Get-Item -LiteralPath $path -ErrorAction Stop
 $provider=[string]$k.GetValue('')
 if(!$provider -or !(Test-Path -LiteralPath $provider -PathType Leaf)){
  throw 'Current v0.4 thumbnail provider not found; do not assume rollback is safe.'
 }
 $private=Join-Path (Split-Path -Parent $provider) 'WicHeicPrivateCodec.dll'
 if(!(Test-Path -LiteralPath $private -PathType Leaf)){
  throw 'Private thumbnail decoder absent.'
 }
 $associated=@()
 foreach($key in @(
  "Registry::HKEY_CURRENT_USER\Software\Classes\.heic\ShellEx\$shell",
  "Registry::HKEY_CURRENT_USER\Software\Classes\SystemFileAssociations\.heic\ShellEx\$shell"
 )){
  $item=Get-Item -LiteralPath $key -ErrorAction Stop
  $value=[string]$item.GetValue('')
  if($value -cne $cls){throw 'Existing thumbnail handler association unexpectedly differs.'}
  $associated+=$value
 }
 return (($provider+'|'+(Get-FileHash -LiteralPath $provider -Algorithm SHA256).Hash+
  '|'+(Get-FileHash -LiteralPath $private -Algorithm SHA256).Hash+
  '|'+($associated -join '|')))
}
$previewBefore=PreviewState
$beforePath=(& $preflight | Select-Object -Last 1)
if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $beforePath)){
 throw 'Read-only WIC inventory failed.'
}
$before=Get-Content -LiteralPath $beforePath -Encoding UTF8 -Raw | ConvertFrom-Json
$entries=@($before.Registrations | Where-Object {
 $_.Hive -eq 'LocalMachine' -and $_.View -eq 'Registry64' -and
 $_.Codec -in @('Decoder','Encoder')
})
if($entries.Count -ne 2){throw 'Global WIC registration inventory incomplete.'}
foreach($entry in $entries){
 if(!$entry.InprocServer32 -or $entry.SHA256 -cne $expected){
  throw 'Global WIC DLL differs from the exact trial build: rollback refused.'
 }
}
$installed=@($before.WicHeicMsi | Where-Object {
 $_.Name -eq 'WicHeic Repair Test' -and $_.Version -eq '1.1.18'
})
if($installed.Count -ne 1 -or $before.WicHeicMsi.Count -ne 1){
 throw 'Unexpected WIC MSI installation state: do not uninstall.'
}
Write-Host 'GLOBAL_WIC_TRIAL_UNINSTALL_PREFLIGHT_PASS; v0.4 baseline preserved.'
if($CheckOnly){return}
$log=Join-Path $dir ('WIC_Trial_Uninstall_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'.log')
$proc=Start-Process -FilePath 'msiexec.exe' -ArgumentList ('/x "{0}" /passive /norestart /l*v "{1}"' -f $msi,$log) -Wait -PassThru
if($proc.ExitCode -ne 0){throw "MSI rollback failed: exit $($proc.ExitCode). Inspect log: $log"}
$afterPath=(& $preflight | Select-Object -Last 1)
if($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $afterPath)){
 throw 'Rollback postflight WIC inventory unavailable.'
}
$after=Get-Content -LiteralPath $afterPath -Encoding UTF8 -Raw | ConvertFrom-Json
$occupied=@($after.Registrations | Where-Object {-not [string]::IsNullOrWhiteSpace($_.InprocServer32)})
if($occupied.Count -ne 0 -or $after.WicHeicMsi.Count -ne 0){
 throw 'Unexpected remaining WIC registration after rollback. Do not edit HEIC originals.'
}
$previewAfter=PreviewState
if($previewBefore -cne $previewAfter){
 throw 'v0.4 preview registration or DLL changed during rollback. Recovery NOT verified.'
}
Write-Host 'GLOBAL_WIC_TRIAL_UNINSTALLED_V04_PREVIEW_BIT_IDENTICAL_PASS'
Write-Host "Before/after reports: $beforePath ; $afterPath"
Write-Host "Uninstall diagnostic log: $log"
