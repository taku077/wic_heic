# Windows PowerShell 5.1 / Windows 10, read-only image observation.
# Uses the existing opt-in WIC trace scripts; no MSI, service or codec changes.
param([ValidateRange(5,180)][int]$DurationSeconds=90)
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot).FullName
$start=Join-Path $root 'PhotosTrace_Start.ps1'
$stop=Join-Path $root 'PhotosTrace_Stop.ps1'
$analyze=Join-Path $root 'COM_CPU_WIC_Analyze.ps1'
foreach($f in @($start,$stop,$analyze)) {
    if(-not (Test-Path -LiteralPath $f -PathType Leaf)) {
        throw "Diagnostic kit is incomplete: $f"
    }
}
$session=Join-Path $root ('COM_CPU_WIC_Result_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff'))
$null=New-Item -ItemType Directory -Path $session -ErrorAction Stop
$csv=Join-Path $session 'Process_CPU_1s.csv'
$traceCopy=Join-Path $session 'WIC_Decode_Events.txt'
$report=Join-Path $session 'CPU_WIC_Summary.txt'
$owned=$false
$traceSource=$null
$cpuRows=New-Object 'System.Collections.Generic.List[object]'
$previous=@{}
$logical=[Environment]::ProcessorCount
if($logical -lt 1){throw 'Invalid logical CPU count'}
$began=[DateTime]::UtcNow
try {
    & $start
    $owned=$true
    $settings=Get-ItemProperty -LiteralPath 'HKCU:\Software\WicHeic' -ErrorAction Stop
    $traceSource=[string]$settings.TraceFile
    if(-not $traceSource -or -not(Test-Path -LiteralPath $traceSource -PathType Leaf)) {
        throw 'WIC trace file was not created'
    }
    Write-Host ('Capture active for {0}s. Open a disposable HEIC in Photos.' -f $DurationSeconds)
    while(([DateTime]::UtcNow-$began).TotalSeconds -lt $DurationSeconds) {
        $now=[DateTime]::UtcNow
        foreach($p in [System.Diagnostics.Process]::GetProcesses()) {
            try {
                $name=$p.ProcessName
                if($name -notmatch '^(dllhost|Photos|Microsoft\.Photos|PhotosApp|PhotoApp|explorer)$') {continue}
                $pidNum=[int]$p.Id
                $total=[double]$p.TotalProcessorTime.TotalSeconds
                $ram=[double]$p.WorkingSet64/1MB
                $nThreads=$p.Threads.Count
                $key=[string]$pidNum
                $pct=0.0
                if($previous.ContainsKey($key)) {
                    $last=$previous[$key]
                    $seconds=($now-$last.Utc).TotalSeconds
                    $delta=$total-$last.Cpu
                    if($seconds -gt 0 -and $delta -ge 0) {
                        $pct=[Math]::Min(100.0,[Math]::Max(0.0,
                              (100.0*$delta/($logical*$seconds))))
                    }
                }
                $previous[$key]=@{Utc=$now;Cpu=$total}
                $cpuRows.Add([pscustomobject]@{
                    UtcTime=$now.ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
                    PID=$pidNum; Name=$name
                    CpuPctTotal=[Math]::Round($pct,2)
                    WorkingSetMB=[Math]::Round($ram,1)
                    Threads=$nThreads
                })
            } catch {
                # Access-denied or exit races must not terminate the capture.
            } finally { $p.Dispose() }
        }
        Start-Sleep -Milliseconds 850
    }
} finally {
    if($owned) {
        # The existing Stop verifies ownership before removing only its keys.
        & $stop
    }
}
if($cpuRows.Count -gt 0) {
    $cpuRows | Export-Csv -LiteralPath $csv -NoTypeInformation -Encoding UTF8
} else {
    'UtcTime,PID,Name,CpuPctTotal,WorkingSetMB,Threads' |
        Set-Content -LiteralPath $csv -Encoding ASCII
}
Copy-Item -LiteralPath $traceSource -Destination $traceCopy -ErrorAction Stop
& $analyze -CpuFile $csv -TraceFile $traceCopy -OutFile $report
$zip=$session+'.zip'
Compress-Archive -LiteralPath @($csv,$traceCopy,$report) -DestinationPath $zip -CompressionLevel Optimal -ErrorAction Stop
Write-Host "RESULT_ZIP $zip"
Write-Host 'CPU_WIC_CAPTURE_PASS (no HEIC files modified)'
