# Windows 10 PowerShell 5.1; manual stop with ENTER, real photos/folders.
param([switch]$CiSmoke,[string]$StatusLog,[switch]$CiForceStopFailure)
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot).FullName
foreach($f in @('PhotosTrace_Start.ps1','PhotosTrace_Stop.ps1','COM_CPU_WIC_Analyze.ps1')){
 if(-not(Test-Path -LiteralPath (Join-Path $root $f))){throw "Missing file $f"}
}
$id=[Security.Principal.WindowsIdentity]::GetCurrent()
$admin=(New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(-not $admin){
 if($CiSmoke){throw 'CI runner not elevated'}
 # UAC creates a different console. Its failures MUST survive closing it.
 $StatusLog=Join-Path $root ('WPR_Child_Status_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'.txt')
 'Parent requested elevation; child may write failure details below.' | Set-Content -LiteralPath $StatusLog -Encoding UTF8
 $argsText='-NoLogo -NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -StatusLog "'+$StatusLog+'"'
 $child=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList $argsText -Wait -PassThru
 if($child.ExitCode -ne 0){
   Write-Warning ('Elevated diagnostic failed. Detailed log: '+$StatusLog)
   if(Test-Path -LiteralPath $StatusLog){
     Get-Content -LiteralPath $StatusLog -Tail 55 | ForEach-Object {Write-Host $_}
   }
   throw ('Elevated diagnostic failed with exit '+$child.ExitCode+'. See '+$StatusLog)
 }
 Write-Host ('Elevated diagnostic completed. Status: '+$StatusLog)
 return
}
if(($CiSmoke -or $CiForceStopFailure) -and $env:GITHUB_ACTIONS -ne 'true'){
 throw 'CI switches require GitHub Actions'
}
if($CiForceStopFailure -and -not $CiSmoke){throw 'Fault injection requires CI smoke mode'}
if([string]::IsNullOrWhiteSpace($StatusLog)){
 $StatusLog=Join-Path $root ('WPR_Child_Status_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'.txt')
}
# Append durable progress/errors independent of a UAC child console lifetime.
function Record([string]$message){
 try {Add-Content -LiteralPath $StatusLog -Value ((Get-Date -Format o)+' '+$message) -Encoding UTF8}
 catch {Write-Warning ('Failed to write status log: '+$_.Exception.Message)}
}
Record 'Elevated worker entered; script starts; no changes to user HEIC images.'
$wpr=Join-Path $env:WINDIR 'System32\wpr.exe'
if(-not(Test-Path -LiteralPath $wpr)){throw 'WPR not available'}
if((New-Object IO.DriveInfo([IO.Path]::GetPathRoot($root))).AvailableFreeSpace -lt 2147483648){
 throw 'Need at least 2 GiB disk space to merge WPR ETL'
}
$status=(& $wpr -status 2>&1 | Out-String)
if($status -match '(?i)recording is in progress|currently recording|記録中|収集中'){throw 'Another WPR session already running'}
if($status -notmatch '(?i)not recording|no trace profiles running|実行されていません|記録していません|記録されていません'){
 throw "Unknown WPR status, refusing to risk another session: $status"
}
$folder=Join-Path $root ('COM_CPU_WPR_Manual_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$null=New-Item -ItemType Directory -Path $folder -ErrorAction Stop
$etl=Join-Path $folder 'CPU_Stacks.etl'
# Isolate WPR's native ETL merge output from percent signs, Japanese
# characters and very long ZIP extraction paths. Move only after success.
$staging=Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) ('WicHeicWPR_'+[guid]::NewGuid().ToString('N')+'.etl')
Record ('Stage ETL destination: '+$staging)
Record ('Result folder: '+$folder)
$csv=Join-Path $folder 'Process_CPU_1s.csv'
$wic=Join-Path $folder 'WIC_Decode_Events.txt'
$sum=Join-Path $folder 'CPU_WIC_Summary.txt'
$manifest=Join-Path $folder 'Capture_Manifest.txt'
$flag=Join-Path $folder 'STOP.flag'
$instance='WicHeicCPU'+(Get-Date -Format 'HHmmssfff')
$wicOwned=$false;$wprOwned=$false;$wprSaved=$false;$job=$null;$source=$null
$startUtc=[DateTime]::UtcNow;$endUtc=$null
$problems=New-Object 'System.Collections.Generic.List[string]'
$wprStopOutput=''
$wprStopExitCode=-999
$stopPhaseReached=$false
$sampleJob={
 param($stopFile,$outputFile)
 $ErrorActionPreference='Stop'
 $previous=@{};$cpuCount=[Environment]::ProcessorCount
 $samples=New-Object 'System.Collections.Generic.List[object]'
 while(-not(Test-Path -LiteralPath $stopFile)){
  $utc=[DateTime]::UtcNow
  foreach($p in [Diagnostics.Process]::GetProcesses()){
   try{
    if($p.ProcessName -notmatch '^(dllhost|Photos|Microsoft\.Photos|PhotosApp|PhotoApp|explorer)$'){continue}
    $pidValue=[int]$p.Id;$key=[string]$pidValue
    $total=[double]$p.TotalProcessorTime.TotalSeconds
    if($previous.ContainsKey($key)){
     $before=$previous[$key];$seconds=($utc-$before.When).TotalSeconds;$delta=$total-$before.Cpu
     if($seconds -gt 0 -and $delta -ge 0){
      $samples.Add([pscustomobject]@{
       UtcTime=$utc.ToString('yyyy-MM-ddTHH:mm:ss.fffZ');PID=$pidValue;Name=$p.ProcessName
       CpuPctTotal=[Math]::Round([Math]::Min(100,[Math]::Max(0,100*$delta/($cpuCount*$seconds))),2)
       WorkingSetMB=[Math]::Round([double]$p.WorkingSet64/1MB,1);Threads=$p.Threads.Count
      })
     }
    }
    $previous[$key]=@{When=$utc;Cpu=$total}
   } catch{} finally{$p.Dispose()}
  }
  if($samples.Count -ge 250000){break}
  Start-Sleep -Milliseconds 850
 }
 if($samples.Count -gt 0){$samples | Export-Csv -LiteralPath $outputFile -Encoding UTF8 -NoTypeInformation}
 else{'UtcTime,PID,Name,CpuPctTotal,WorkingSetMB,Threads' | Set-Content -LiteralPath $outputFile -Encoding ASCII}
 Write-Output ('CPU_SAMPLER_OK rows='+$samples.Count)
}
try{
 & (Join-Path $root 'PhotosTrace_Start.ps1')
 $wicOwned=$true
 $source=[string](Get-ItemProperty -Path 'HKCU:\Software\WicHeic' -ErrorAction Stop).TraceFile
 if(-not(Test-Path -LiteralPath $source)){throw 'WIC trace not initialized'}
 $begin=(& $wpr -start GeneralProfile.Verbose -start CPU.Verbose -instancename $instance 2>&1 | Out-String)
 if($LASTEXITCODE -ne 0){throw "WPR start failure: $begin"}
 $wprOwned=$true;$startUtc=[DateTime]::UtcNow
 Record ('WPR recording STARTED: instance='+$instance)
 $details=(& $wpr -status collectors -instancename $instance 2>&1 | Out-String)
 if($LASTEXITCODE -ne 0 -or $details -notmatch 'SampledProfile|サンプル'){
  throw 'CPU sampling collector was not confirmed'
 }
 $job=Start-Job -ScriptBlock $sampleJob -ArgumentList $flag,$csv
 Record 'CPU sampling job started; enter manually when finished.'
 Write-Host ''
 Write-Host '=== RECORDING / YOUR CHOICE WHEN TO END ===' -ForegroundColor Green
 Write-Host 'Open your REAL HEIC folder in Explorer; scroll, show thumbnails and use Photos.'
 Write-Host 'Do NOT save changes to original photos. No artificial images required.'
 Write-Host 'No 60/90s limit. Return here and press Enter when you have reproduced the lag.'
 Write-Host 'Memory ring will overwrite old samples if kept running a long time.'
 if($CiSmoke){Start-Sleep -Seconds 5}
 else{$null=Read-Host 'Press ENTER to stop and SAVE'}
} catch{
 $problems.Add($_.Exception.Message)
 Record ('Capture exception: '+$_.Exception.ToString())
 Write-Warning $_.Exception.Message
} finally {
 $endUtc=[DateTime]::UtcNow
 if($wprOwned){
  try {
   $stopPhaseReached=$true
   Record ('WPR STOP requested; instance='+$instance)
   $stdout=Join-Path $folder 'WPR_Stop_Stdout.txt'
   $stderr=Join-Path $folder 'WPR_Stop_Stderr.txt'
   $argsText='-stop "'+$staging+'" -instancename '+$instance+' -skipPdbGen'
   # Win10 built-in WPR 10.0.19041 can fail during managed NGEN/PDB generation.
   # Native kernel sampled stacks remain available after -skipPdbGen.
   if($CiForceStopFailure) {
     $wprStopExitCode=777
     $wprStopOutput='CI deliberate WPR stop failure simulation (no real stop attempt)'
   } else {
     # Separate files keep full native diagnostics even if PowerShell
     # 5.1 would otherwise translate stderr to a terminating error.
     $native=Start-Process -FilePath $wpr -ArgumentList $argsText -NoNewWindow -Wait -PassThru `
       -RedirectStandardOutput $stdout -RedirectStandardError $stderr -ErrorAction Stop
     $wprStopExitCode=$native.ExitCode
     $wprStopOutput=((Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue) +
          "`n" + (Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue))
   }
   Record ('WPR STOP args include -skipPdbGen (avoid old managed PDB generator)')
   Record ('WPR STOP exit='+$wprStopExitCode+' response='+$wprStopOutput)
   if($wprStopExitCode -ne 0){throw ('WPR stop failed (exit '+$wprStopExitCode+'): '+$wprStopOutput)}
   if(-not(Test-Path -LiteralPath $staging -PathType Leaf)){throw 'WPR returned success but no staged ETL exists'}
   if((Get-Item -LiteralPath $staging).Length -lt 1024){throw 'WPR returned empty staged ETL'}
   Move-Item -LiteralPath $staging -Destination $etl -ErrorAction Stop
   $wprSaved=$true
   Record ('WPR ETL safely saved, bytes='+(Get-Item -LiteralPath $etl).Length)
  } catch {
   $problems.Add('WPR stop: '+$_.Exception.Message)
   if($wprStopExitCode -eq -2147417850){
    $problems.Add('Known WPR 0x80010106 on Windows 10: install a newer Microsoft Windows Performance Toolkit WPR; do not repeat unchanged capture')
    Record 'OLD_WPR_RPC_E_CHANGED_MODE: Windows Performance Toolkit upgrade advised; collected CPU/WIC data retained'
   }
   Record ('WPR STOP FAILURE: '+$_.Exception.ToString())
   # Preserve evidence before trying to end our uniquely named trace.
   $statusOutput=''
   try {$statusOutput=(& $wpr -status -instancename $instance 2>&1 | Out-String)} catch {
     $statusOutput='Status query error: '+$_.Exception.ToString()
   }
   Record ('WPR own instance status after stop failure: '+$statusOutput)
   # Do not cancel an unrelated WPR session. This one is owned by this run.
   if($wprOwned){
     try {
       $cancelOutput=(& $wpr -cancel -instancename $instance 2>&1 | Out-String)
       Record ('WPR own instance cancel: '+$cancelOutput)
     } catch {Record ('WPR own instance cancel error: '+$_.Exception.ToString())}
   }
  }
 }
 if($job){
  try {
   $null=New-Item -ItemType File -Path $flag -Force
   $null=Wait-Job -Job $job -Timeout 25
   if($job.State -ne 'Completed'){throw 'CPU sampling job unfinished'}
   Receive-Job $job | Write-Host
  } catch{$problems.Add('CPU sampler: '+$_.Exception.Message);Stop-Job $job -ErrorAction SilentlyContinue}
  finally{Remove-Job $job -Force -ErrorAction SilentlyContinue}
 }
 if($wicOwned){
  try{& (Join-Path $root 'PhotosTrace_Stop.ps1')}
  catch{$problems.Add('WIC cleanup: '+$_.Exception.Message)}
 }
}
# A WPR save failure must not discard all useful CPU/WIC information.
try {
 if($source -and (Test-Path -LiteralPath $source -PathType Leaf)){
  Copy-Item -LiteralPath $source -Destination $wic -ErrorAction Stop
 }
} catch { $problems.Add('WIC copy: '+$_.Exception.Message);Record $_.Exception.ToString() }
try {
 if((Test-Path -LiteralPath $csv -PathType Leaf) -and
    (Test-Path -LiteralPath $wic -PathType Leaf)){
  & (Join-Path $root 'COM_CPU_WIC_Analyze.ps1') -CpuFile $csv -TraceFile $wic -OutFile $sum
 }
} catch { $problems.Add('Correlation: '+$_.Exception.Message);Record $_.Exception.ToString() }
if(-not $wprSaved){$problems.Add('CPU_Stacks.etl unavailable; do NOT claim complete CPU stack recording')}
if(-not(Test-Path -LiteralPath $csv -PathType Leaf)){$problems.Add('CPU sample CSV missing')}
$state=if($problems.Count){'FAILED_PARTIAL'}else{'COMPLETE'}
$manifestLines=@(
 'WicHeic 1.1.30: real-folder manual-stop CPU/WIC collection'
 ('ResultStatus='+$state)
 ('WPRStopWasReached='+$stopPhaseReached)
 ('WPRStopExitCode='+$wprStopExitCode)
 ('WPRInstance='+$instance)
 ('WPRStagePath='+$staging)
 ('StartUtc='+$startUtc.ToString('o'))
 ('EndUtc='+$endUtc.ToString('o'))
 ('LogicalProcessors='+[Environment]::ProcessorCount)
 ('ETLBytes='+$(if($wprSaved){(Get-Item $etl).Length}else{0}))
 'WPR=GeneralProfile.Verbose + CPU.Verbose; Memory, manual Enter'
 'WPRStopSkipPdbGen=True (avoid Win10 WPR managed symbol generation failure)'
 'WIC trace settings expire after 12h; maximum WIC log 2MiB'
 'PRIVACY: ETL can contain user file paths and other identifying details'
)
foreach($problem in $problems){$manifestLines+=('ERROR='+$problem)}
if($wprSaved){$manifestLines+=('ETL_SHA256='+(Get-FileHash -LiteralPath $etl -Algorithm SHA256).Hash)}
$manifestLines | Set-Content -LiteralPath $manifest -Encoding UTF8
Record ('Final status='+$state+' problems='+$problems.Count)
$collected=@($manifest,$StatusLog)
foreach($file in @($etl,$csv,$wic,$sum,(Join-Path $folder 'WPR_Stop_Stdout.txt'),(Join-Path $folder 'WPR_Stop_Stderr.txt'))){
 if(Test-Path -LiteralPath $file -PathType Leaf){$collected+= $file}
}
$zip=$folder+'.zip'
Compress-Archive -LiteralPath $collected -DestinationPath $zip -CompressionLevel Optimal -ErrorAction Stop
if(-not(Test-Path -LiteralPath $zip -PathType Leaf)){throw 'ZIP creation failed'}
Write-Host ('RESULT_ZIP '+$zip)
if($problems.Count){
 Write-Warning ('INCOMPLETE diagnosis; open '+$StatusLog)
 throw ('Capture incomplete: '+($problems -join '; '))
}
Write-Host 'CPU_WPR_MANUAL_ONEPASS_PASS' -ForegroundColor Green
