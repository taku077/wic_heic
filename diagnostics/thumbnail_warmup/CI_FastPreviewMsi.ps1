#requires -version 5.1
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$MsiPath,
      [Parameter(Mandatory=$true)][string]$FixturePath,
      [string]$LargeFixturePath)
$ErrorActionPreference='Stop'
Write-Host 'FAST_PREVIEW_SMOKE_ENTERED'
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
$hklm=[Microsoft.Win32.Registry]::LocalMachine
foreach($key in $keys) {
 $root=if($key -eq $keys[0]){$hklm}else{$hkcu}
 $k=$root.OpenSubKey($key)
 if($k){$k.Close();throw ('Preexisting preview registration: '+$key)}
}
$msi=(Resolve-Path -LiteralPath $MsiPath).Path
$fixture=(Resolve-Path -LiteralPath $FixturePath).Path
$fixtureHash=(Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash
$largeFixture=''
$largeHash=''
if($LargeFixturePath) {
 $largeFixture=(Resolve-Path -LiteralPath $LargeFixturePath).Path
 if(-not $largeFixture.StartsWith($env:RUNNER_TEMP+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($largeFixture) -ne 'wicfast_synthetic_4032x3024.heic') {
   throw '12MP CI fixture must be generated inside RUNNER_TEMP'
 }
 $largeHash=(Get-FileHash -LiteralPath $largeFixture -Algorithm SHA256).Hash
}
$folder=Join-Path $env:RUNNER_TEMP ('fastpreview_'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $folder | Out-Null
$installed=$false
$marker="Software\WicHeicFastPreviewCI\$cls"
try {
 $log=Join-Path $folder 'install.log'
 Write-Host 'FAST_PREVIEW_SMOKE_BEFORE_INSTALL'
 $p=Start-Process msiexec.exe -ArgumentList ('/i "{0}" /qn /norestart /l*v "{1}"' -f $msi,$log) -Wait -PassThru
 Write-Host ('FAST_PREVIEW_SMOKE_INSTALL_CODE='+$p.ExitCode)
 if($p.ExitCode -ne 0){throw ('Fast MSI install exit='+$p.ExitCode)}
 $installed=$true
 Write-Host 'FAST_PREVIEW_SMOKE_INSTALLED'
 $key=$hklm.OpenSubKey("Software\Classes\CLSID\$cls\InprocServer32")
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
 Write-Host 'FAST_PREVIEW_SMOKE_BEFORE_ADDTYPE'
 Add-Type -TypeDefinition $match.Groups[1].Value -Language CSharp -ErrorAction Stop
 Write-Host 'FAST_PREVIEW_SMOKE_AFTER_ADDTYPE'
 if([TrialShell.Shell]::ProbeComActivation($cls) -ne 0){throw 'Fast COM DLL cannot be activated'}
 $testKey=[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($marker)
 try {$testKey.SetValue('Enable',1,[Microsoft.Win32.RegistryValueKind]::DWord)}
 finally {$testKey.Close()}
 [TrialShell.Shell]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero)
 $files=@()
 $expectedByPath=@{}
 $highResPaths=@()
 for($i=0;$i -lt 10;$i++){
   $copy=Join-Path $folder ('real_extension_{0:D2}.heic' -f $i)
   Copy-Item -LiteralPath $fixture -Destination $copy
   $files+=$copy
   $expectedByPath[$copy]=$fixtureHash
 }
 if($largeFixture){
   for($i=0;$i -lt 4;$i++){
     $copy=Join-Path $folder ('large_12mp_{0:D2}.heic' -f $i)
     Copy-Item -LiteralPath $largeFixture -Destination $copy
     $files+=$copy
     $highResPaths+=$copy
     $expectedByPath[$copy]=$largeHash
   }
 }
 $largeColdTimes=New-Object 'System.Collections.Generic.List[double]'
 Write-Host 'FAST_PREVIEW_SMOKE_BEFORE_FIRST_EXTRACTION'
 foreach($file in $files){
   $result=[TrialShell.Shell]::GetSyntheticThumbnail($file,$false,[uint32]256)
   Write-Host ('FAST_PREVIEW_FIRST_RESULT path='+[IO.Path]::GetFileName($file)+' hr='+$result.HResult+' bitmap='+$result.HasBitmap)
   if($result.HResult -lt 0 -or -not $result.HasBitmap) {
     throw ('Real .heic Shell thumbnail failed: '+$result.HResult)
   }
   if($highResPaths -contains $file){
     $phaseKey=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($marker)
     try {
       $copyUs=[int]$phaseKey.GetValue('DecodeCopyPixelsUs',-1)
       $totalUs=[int]$phaseKey.GetValue('AllRenderingUs',-1)
       if($copyUs -le 0 -or $totalUs -le 0){throw 'Invalid 12MP timing sample'}
       Write-Host ('PRIVATE_HEIC_12MP_SAMPLE copy_ms='+([math]::Round($copyUs/1000.0,2))+' total_ms='+([math]::Round($totalUs/1000.0,2)))
       $largeColdTimes.Add($totalUs/1000.0)
     } finally { $phaseKey.Close() }
   }
 }
 if($largeFixture){
   if($largeColdTimes.Count -ne 4){throw 'Expected 4 synthetic 12MP measurements'}
   $values=@($largeColdTimes | Sort-Object)
   $median=($values[1]+$values[2])/2
   Write-Host ('PRIVATE_HEIC_12MP_COLD_MEDIAN_MS='+([math]::Round($median,2)))
   Write-Host 'PRIVATE_HEIC_12MP_SYNTHETIC_BASELINE_PASSED'
 }
 $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($marker)
 try {$calls=[int]$key.GetValue('Calls',0)}
 finally {$key.Close()}
 if($calls -ne $files.Count){throw ('Fast handler not used for real .heic extraction: '+$calls)}
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
   if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $expectedByPath[$file]){
     throw 'Artificial HEIC copy modified'
   }
 }
 $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($marker)
 try {
   $phaseNames=@('DecodeCopyPixelsUs','DecoderSetupUs','FrameOpenUs','ScaleSetupUs','AllRenderingUs')
   foreach($phase in $phaseNames) {
     $microseconds=[int]$key.GetValue($phase,-1)
     if($microseconds -lt 0) {throw ('Missing private HEIC phase: '+$phase)}
     Write-Host ('PRIVATE_HEIC_PHASE_'+$phase.ToUpperInvariant()+'_LAST_MS='+([math]::Round($microseconds/1000.0,3)))
   }
 } finally {$key.Close()}
 Write-Host 'PRIVATE_HEIC_PHASE_TIMERS_PASSED'
 Write-Host 'FAST_PREVIEW_MSI_REAL_HEIC_FIRST_EXTRACTION_PASSED'
 Write-Host 'FAST_PREVIEW_MSI_REAL_HEIC_CACHE_ONLY_PASSED'
 Write-Host 'FAST_PREVIEW_MSI_SOURCE_HASHES_PASSED'
} catch {
 Write-Host ('FAST_PREVIEW_SMOKE_EXCEPTION='+$_.Exception.ToString())
 throw
} finally {
 if($installed){
   Write-Host 'FAST_PREVIEW_SMOKE_BEFORE_UNINSTALL'
   $p=$null
   for($attempt=0;$attempt -lt 8;$attempt++){
     $p=Start-Process msiexec.exe -ArgumentList ('/x "{0}" /qn /norestart' -f $msi) -Wait -PassThru
     Write-Host ('FAST_PREVIEW_SMOKE_UNINSTALL_CODE='+$p.ExitCode+' attempt='+$attempt)
     if($p.ExitCode -ne 1618){break}
     Start-Sleep -Seconds 2
   }
   if($p.ExitCode -ne 0){throw ('MSI uninstall failed: '+$p.ExitCode)}
 }
 foreach($subkey in $keys){
   $root=if($subkey -eq $keys[0]){$hklm}else{$hkcu}
   $key=$root.OpenSubKey($subkey)
   if($key){$key.Close();throw ('Residual per-user registry key: '+$subkey)}
 }
 [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($marker,$false)
 Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue
 Write-Host 'FAST_PREVIEW_MSI_HKLM_ROLLBACK_PASSED'
}