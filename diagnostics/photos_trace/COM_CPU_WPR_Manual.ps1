# Windows 10 PowerShell 5.1; manual stop with ENTER, real photos/folders.
param([switch]$CiSmoke)
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot).FullName
foreach($f in @('PhotosTrace_Start.ps1','PhotosTrace_Stop.ps1','COM_CPU_WIC_Analyze.ps1')){
 if(-not(Test-Path -LiteralPath (Join-Path $root $f))){throw "Missing file $f"}
}
$id=[Security.Principal.WindowsIdentity]::GetCurrent()
$admin=(New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(-not $admin){
 if($CiSmoke){throw 'CI runner not elevated'}
 $argsText='-NoProfile -ExecutionPolicy Bypass -File "'+$PSCommandPath+'"'
 $child=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -Verb RunAs -ArgumentList $argsText -Wait -PassThru
 if($child.ExitCode -ne 0){throw "Elevated capture exited $($child.ExitCode)"}
 return
}
if($CiSmoke -and $env:GITHUB_ACTIONS -ne 'true'){throw 'CI switch requires GitHub Actions'}
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
$csv=Join-Path $folder 'Process_CPU_1s.csv'
$wic=Join-Path $folder 'WIC_Decode_Events.txt'
$sum=Join-Path $folder 'CPU_WIC_Summary.txt'
$manifest=Join-Path $folder 'Capture_Manifest.txt'
$flag=Join-Path $folder 'STOP.flag'
$instance='WicHeicCPU'+(Get-Date -Format 'HHmmssfff')
$wicOwned=$false;$wprOwned=$false;$wprSaved=$false;$job=$null;$source=$null
$startUtc=[DateTime]::UtcNow;$endUtc=$null
$problems=New-Object 'System.Collections.Generic.List[string]'
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
 $details=(& $wpr -status collectors -instancename $instance 2>&1 | Out-String)
 if($LASTEXITCODE -ne 0 -or $details -notmatch 'SampledProfile|サンプル'){
  throw 'CPU sampling collector was not confirmed'
 }
 $job=Start-Job -ScriptBlock $sampleJob -ArgumentList $flag,$csv
 Write-Host ''
 Write-Host '=== RECORDING / YOUR CHOICE WHEN TO END ===' -ForegroundColor Green
 Write-Host 'Open your REAL HEIC folder in Explorer; scroll, show thumbnails and use Photos.'
 Write-Host 'Do NOT save changes to original photos. No artificial images required.'
 Write-Host 'No 60/90s limit. Return here and press Enter when you have reproduced the lag.'
 Write-Host 'Memory ring will overwrite old samples if kept running a long time.'
 if($CiSmoke){Start-Sleep -Seconds 5}
 else{$null=Read-Host 'Press ENTER to stop and SAVE'}
} catch{
 $problems.Add($_.Exception.Message);Write-Warning $_.Exception.Message
} finally {
 $endUtc=[DateTime]::UtcNow
 if($wprOwned){
  try {
   $stopResult=(& $wpr -stop $etl -instancename $instance 2>&1 | Out-String)
   if($LASTEXITCODE -ne 0){throw "WPR stop failure: $stopResult"}
   if(-not(Test-Path -LiteralPath $etl) -or (Get-Item $etl).Length -lt 1024){throw 'Missing or empty ETL'}
   $wprSaved=$true
  } catch {
   $problems.Add('WPR stop: '+$_.Exception.Message)
   try {& $wpr -cancel -instancename $instance | Out-Null} catch{}
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
if($problems.Count){throw ('Capture incomplete: '+($problems -join '; '))}
if(-not $wprSaved -or -not(Test-Path $csv)){throw 'Missing ETL or CPU CSV'}
Copy-Item -LiteralPath $source -Destination $wic -ErrorAction Stop
& (Join-Path $root 'COM_CPU_WIC_Analyze.ps1') -CpuFile $csv -TraceFile $wic -OutFile $sum
$etlInfo=Get-Item -LiteralPath $etl
@(
 'WicHeic 1.1.30: manual-stop real-folder CPU/WIC collection'
 'WPR=GeneralProfile.Verbose+CPU.Verbose; Memory ring'
 'Stop=Manual Enter; WPR no fixed time cap'
 "StartUtc=$($startUtc.ToString('o'))"
 "EndUtc=$($endUtc.ToString('o'))"
 "LogicalProcessors=$([Environment]::ProcessorCount)"
 "ETLBytes=$($etlInfo.Length)"
 "ETL_SHA256=$((Get-FileHash $etl -Algorithm SHA256).Hash)"
 'WIC opt-in settings expire after 12h for safety; trace max 2MiB'
 'NOTE: WPR ring buffer overwrites old events; stop soon after reproduction'
 'PRIVACY: ETL may contain paths, names, process commands and other system details'
) | Set-Content -LiteralPath $manifest -Encoding UTF8
$zip=$folder+'.zip'
Compress-Archive -LiteralPath @($etl,$csv,$wic,$sum,$manifest) -DestinationPath $zip -CompressionLevel Optimal -ErrorAction Stop
if(-not(Test-Path -LiteralPath $zip)){throw 'ZIP missing'}
Write-Host 'CPU_WPR_MANUAL_ONEPASS_PASS' -ForegroundColor Green
Write-Host "RESULT_ZIP $zip"
