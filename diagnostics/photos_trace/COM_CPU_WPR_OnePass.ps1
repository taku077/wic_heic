# Windows 10 / Windows PowerShell 5.1. One-pass CPU stacks + numeric WIC.
# WPR uses MEMORY MODE (not unbounded -filemode). No image read/write.
param([ValidateRange(5,90)][int]$DurationSeconds=60)
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
foreach($name in @('PhotosTrace_Start.ps1','PhotosTrace_Stop.ps1','COM_CPU_WIC_Analyze.ps1')) {
 if(-not(Test-Path -LiteralPath (Join-Path $root $name) -PathType Leaf)) {
   throw "Required component missing: $name"
 }
}
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$principal=New-Object Security.Principal.WindowsPrincipal($identity)
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){
 throw 'WPR CPU stack sampling requires Administrator. Use the double-click CMD.'
}
$wpr=Join-Path $env:WINDIR 'System32\wpr.exe'
if(-not(Test-Path -LiteralPath $wpr -PathType Leaf)){
 throw 'Windows Performance Recorder (wpr.exe) not present; nothing recorded.'
}
$drive=[IO.Path]::GetPathRoot($root)
$disk=New-Object IO.DriveInfo($drive)
if($disk.AvailableFreeSpace -lt 1073741824) {
 throw 'At least 1 GiB free on the output drive is required. No trace started.'
}
$session=Join-Path $root ('COM_CPU_WPR_Result_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$null=New-Item -Path $session -ItemType Directory -ErrorAction Stop
$traceSource=$null
$wicOwned=$false
$wprOwned=$false
$wprStopOK=$false
$etl=Join-Path $session 'CPU_Stacks.etl'
$csv=Join-Path $session 'Process_CPU_1s.csv'
$wic=Join-Path $session 'WIC_Decode_Events.txt'
$report=Join-Path $session 'CPU_WIC_Summary.txt'
$manifest=Join-Path $session 'Capture_Manifest.txt'
$rows=New-Object 'System.Collections.Generic.List[object]'
$last=@{}
$logical=[Environment]::ProcessorCount
$startUtc=[DateTime]::UtcNow
$endUtc=$null
$errorMessage=$null
try {
  & (Join-Path $root 'PhotosTrace_Start.ps1')
  $wicOwned=$true
  $opt=Get-ItemProperty -LiteralPath 'HKCU:\Software\WicHeic' -ErrorAction Stop
  $traceSource=[string]$opt.TraceFile
  if(-not $traceSource -or -not(Test-Path -LiteralPath $traceSource -PathType Leaf)) {
    throw 'WIC trace initialization failed'
  }
  # Refuse if another WPR recorder is already active; start never overrides.
  $statusText=(& $wpr -status 2>&1 | Out-String)
  if($statusText -match 'recording is in progress|currently recording') {
    throw 'An existing WPR recording is active; refusing to interfere.'
  }
  $output=(& $wpr -start CPU.Verbose -start GeneralProfile.Verbose 2>&1 | Out-String)
  $code=$LASTEXITCODE
  if($code -ne 0){throw "WPR start failed (code=$code): $output"}
  $wprOwned=$true
  $startUtc=[DateTime]::UtcNow
  Write-Host "Recording CPU stacks and WIC for $DurationSeconds seconds. Open a disposable HEIC in Photos."
  Write-Host 'Do not close this console until the result is saved.'
  while(([DateTime]::UtcNow-$startUtc).TotalSeconds -lt $DurationSeconds) {
    $now=[DateTime]::UtcNow
    foreach($proc in [Diagnostics.Process]::GetProcesses()) {
      try {
        $name=$proc.ProcessName
        if($name -notmatch '^(dllhost|Photos|Microsoft\.Photos|PhotosApp|PhotoApp|explorer)$'){continue}
        $pidnum=[int]$proc.Id
        $key=[string]$pidnum
        $total=[double]$proc.TotalProcessorTime.TotalSeconds
        $mem=[double]$proc.WorkingSet64/1MB
        $nthreads=$proc.Threads.Count
        # The first observation of a PID is a baseline, not a zero-CPU sample.
        if($last.ContainsKey($key)){
          $earlier=$last[$key]
          $dt=($now-$earlier.Time).TotalSeconds
          $delta=$total-$earlier.Cpu
          if($dt -gt 0 -and $delta -ge 0) {
            $pct=[Math]::Min(100.0,[Math]::Max(0.0,100.0*$delta/$dt/$logical))
            $rows.Add([pscustomobject]@{
              UtcTime=$now.ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
              PID=$pidnum;Name=$name;CpuPctTotal=[Math]::Round($pct,2)
              WorkingSetMB=[Math]::Round($mem,2);Threads=$nthreads
            })
          }
        }
        $last[$key]=@{Time=$now;Cpu=$total}
      } catch {
        # Some Windows protected processes may disappear or reject inspection.
      } finally { $proc.Dispose() }
    }
    Start-Sleep -Milliseconds 850
  }
} catch {
  $errorMessage=$_.Exception.Message
  Write-Warning $errorMessage
} finally {
  $endUtc=[DateTime]::UtcNow
  if($wprOwned) {
    try {
      $stopOutput=(& $wpr -stop $etl 2>&1 | Out-String)
      $stopCode=$LASTEXITCODE
      if($stopCode -ne 0) {throw "WPR stop failed code=$stopCode : $stopOutput"}
      if(-not(Test-Path -LiteralPath $etl -PathType Leaf) -or
         (Get-Item -LiteralPath $etl).Length -lt 1024){
        throw 'WPR stopped but ETL is empty or missing'
      }
      $wprStopOK=$true
    } catch {
      Write-Warning ('WPR stop error: '+$_.Exception.Message)
      if(-not $errorMessage){$errorMessage='WPR stop failed: '+$_.Exception.Message}
      # Cancel ONLY a recording that THIS script successfully started.
      & $wpr -cancel | Out-Null
    }
  }
  if($wicOwned) {
    try {
      & (Join-Path $root 'PhotosTrace_Stop.ps1')
    } catch {
      Write-Warning ('WIC trace cleanup issue: '+$_.Exception.Message)
      if(-not $errorMessage){$errorMessage='WIC cleanup failed: '+$_.Exception.Message}
    }
  }
}
if($errorMessage){throw $errorMessage}
if(-not $wprStopOK){throw 'No complete WPR ETL captured'}
if($rows.Count -lt 3){throw 'Too few CPU samples for a useful result'}
$rows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8
Copy-Item -LiteralPath $traceSource -Destination $wic -ErrorAction Stop
& (Join-Path $root 'COM_CPU_WIC_Analyze.ps1') -CpuFile $csv -TraceFile $wic -OutFile $report
$etlInfo=Get-Item -LiteralPath $etl
@(
 'WicHeic CPU/WIC single-pass diagnostic, build 2026-10-29'
 "StartUtc=$($startUtc.ToString('o'))"
 "EndUtc=$($endUtc.ToString('o'))"
 "DurationSeconds=$DurationSeconds"
 "LogicalProcessors=$logical"
 'WprProfiles=CPU.Verbose+GeneralProfile.Verbose'
 'WprMode=Memory; ETL exported after capture'
 "WprETLBytes=$($etlInfo.Length)"
 "WprETLSHA256=$((Get-FileHash -LiteralPath $etl -Algorithm SHA256).Hash)"
 "Rows=$($rows.Count)"
 'Privacy=ETL can contain process details, paths and other personal system metadata. Inspect before sharing.'
 'Interpretation=WPR CPU stacks can identify modules; full private symbols may be needed for internal function names.'
 'Files=CPU_Stacks.etl; Process_CPU_1s.csv; WIC_Decode_Events.txt; CPU_WIC_Summary.txt'
) | Set-Content -LiteralPath $manifest -Encoding UTF8
$archive=$session+'.zip'
Compress-Archive -LiteralPath @($etl,$csv,$wic,$report,$manifest) -DestinationPath $archive -CompressionLevel Optimal -ErrorAction Stop
$size=(Get-Item -LiteralPath $archive).Length
Write-Host "CPU_WPR_ONEPASS_PASS"
Write-Host "RESULT_ZIP $archive"
Write-Host "ZIP_BYTES $size"
Write-Host 'Only this ZIP is needed for the next analysis; the ETL remains available for re-analysis without re-recording.'
