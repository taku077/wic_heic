#requires -version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$MsiPath,
      [Parameter(Mandatory=$true)][string]$FixturePath)
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true' -or -not $env:RUNNER_TEMP) {
 throw 'This MSI smoke test runs ONLY on a disposable GitHub Actions runner'
}
$cls='{D973196C-165C-4FCE-8838-17C37037D384}'
$thumb='{E357FCCD-A995-4576-B01F-234630154E96}'
$keys=@(
 "Software\Classes\CLSID\$cls",
 "Software\Classes\.heic\ShellEx\$thumb",
 "Software\Classes\SystemFileAssociations\.heic\ShellEx\$thumb"
)
$hkcu=[Microsoft.Win32.Registry]::CurrentUser
foreach($key in $keys) {
 $k=$hkcu.OpenSubKey($key)
 if($k){$k.Close();throw ('Preexisting per-user registration: '+$key)}
}
$msi=(Resolve-Path -LiteralPath $MsiPath).Path
$fixture=(Resolve-Path -LiteralPath $FixturePath).Path
$fixtureHash=(Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash
$folder=Join-Path $env:RUNNER_TEMP ('fastpreview_'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $folder | Out-Null
$installed=$false
$marker="Software\WicHeicFastPreviewCI\$cls"
try {
 $log=Join-Path $folder 'install.log'
 $p=Start-Process msiexec.exe -ArgumentList ('/i "{0}" /qn /norestart /l*v "{1}"' -f $msi,$log) -Wait -PassThru
 if($p.ExitCode -ne 0){throw ('Fast MSI install exit='+$p.ExitCode)}
 $installed=$true
 $key=$hkcu.OpenSubKey("Software\Classes\CLSID\$cls\InprocServer32")
 if(-not $key){throw 'Fast COM class was not registered'}
 try {
   $dll=[string]$key.GetValue('')
   if(-not $dll -or -not (Test-Path -LiteralPath $dll)){throw 'Fast DLL missing'}
   if($key.GetValue('ThreadingModel') -ne 'Apartment'){throw 'Unexpected COM threading'}
 } finally {$key.Close()}
 foreach($subkey in $keys[1..2]){
   $key=$hkcu.OpenSubKey($subkey)
   if(-not $key){throw ('No .heic association '+$subkey)}
   try {
     if([string]$key.GetValue('') -ne $cls){throw 'Wrong .heic CLSID'}
   } finally {$key.Close()}
 }
 # Import the same native Shell COM marshaller as the existing isolated tests.
 # Do NOT execute the registration or cleanup commands in that script.
 $reference=Get-Content -LiteralPath 'diagnostics/thumbnail_warmup/CI_IsolatedShellTrial.ps1' -Raw
 $match=[regex]::Match($reference,"(?s)\`$native=@'\s*(.*?)\s*'@")
 if(-not $match.Success){throw 'Could not locate inspected C# COM interface definitions'}
 Add-Type -TypeDefinition $match.Groups[1].Value -Language CSharp -ErrorAction Stop
 if([TrialShell.Shell]::ProbeComActivation($cls) -ne 0){throw 'Fast COM DLL cannot be activated'}
 $testKey=$hkcu.CreateSubKey($marker)
 try {$testKey.SetValue('Enable',1,[Microsoft.Win32.RegistryValueKind]::DWord)}
 finally {$testKey.Close()}
 [TrialShell.Shell]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero)
 $files=@()
 for($i=0;$i -lt 10;$i++){
   $copy=Join-Path $folder ('real_extension_{0:D2}.heic' -f $i)
   Copy-Item -LiteralPath $fixture -Destination $copy
   $files+=$copy
 }
 foreach($file in $files){
   $result=[TrialShell.Shell]::GetSyntheticThumbnail($file,$false,[uint32]256)
   if($result.HResult -lt 0 -or -not $result.HasBitmap) {
     throw ('Real .heic Shell thumbnail failed: '+$result.HResult)
   }
 }
 $key=$hkcu.OpenSubKey($marker)
 try {$calls=[int]$key.GetValue('Calls',0)}
 finally {$key.Close()}
 if($calls -ne 10){throw ('Fast handler not used for real .heic extraction: '+$calls)}
 foreach($file in $files){
   $result=[TrialShell.Shell]::GetSyntheticThumbnail($file,$true,[uint32]256)
   if($result.HResult -lt 0 -or -not $result.HasBitmap) {
     throw ('HEIC Shell cache-only failed: '+$result.HResult)
   }
 }
 $key=$hkcu.OpenSubKey($marker)
 try {$later=[int]$key.GetValue('Calls',0)}
 finally {$key.Close()}
 if($later -ne $calls){throw 'Shell cache revisit decoded again'}
 foreach($file in $files){
   if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $fixtureHash){
     throw 'Artificial HEIC copy modified'
   }
 }
 Write-Host 'FAST_PREVIEW_MSI_REAL_HEIC_FIRST_EXTRACTION_PASSED'
 Write-Host 'FAST_PREVIEW_MSI_REAL_HEIC_CACHE_ONLY_PASSED'
 Write-Host 'FAST_PREVIEW_MSI_SOURCE_HASHES_PASSED'
} finally {
 if($installed){
   $p=Start-Process msiexec.exe -ArgumentList ('/x "{0}" /qn /norestart' -f $msi) -Wait -PassThru
   if($p.ExitCode -ne 0){throw ('MSI uninstall failed: '+$p.ExitCode)}
 }
 foreach($subkey in $keys){
   $key=$hkcu.OpenSubKey($subkey)
   if($key){$key.Close();throw ('Residual per-user registry key: '+$subkey)}
 }
 $hkcu.DeleteSubKeyTree($marker,$false)
 Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue
 Write-Host 'FAST_PREVIEW_MSI_REGISTRY_UNINSTALL_RESTORE_PASSED'
}