# Windows PowerShell 5.1. Passive analysis of numeric CPU/WIC logs.
# Does not infer causation from DLL presence or same-second correlation.
param(
 [Parameter(Mandatory=$true)][string]$CpuFile,
 [Parameter(Mandatory=$true)][string]$TraceFile,
 [Parameter(Mandatory=$true)][string]$OutFile
)
$ErrorActionPreference='Stop'
foreach($file in @($CpuFile,$TraceFile)) {
 if(-not(Test-Path -LiteralPath $file -PathType Leaf)) {throw "Missing input: $file"}
}
$rows=@(Import-Csv -LiteralPath $CpuFile | Where-Object { $_.PID -match '^\d+$' })
$events=New-Object 'System.Collections.Generic.List[object]'
$recognized=0
foreach($rawLine in [IO.File]::ReadAllLines($TraceFile)) {
 $line=$rawLine.TrimStart([char]0xFEFF).Trim()
 if(-not $line.Contains('WIC_HEIC_TRACE v1 ')) {continue}
 $recognized++
 $pidMatch=[regex]::Match($line,'(?:^|\s)pid=(\d+)(?:\s|$)')
 $eventMatch=[regex]::Match($line,'(?:^|\s)event=([A-Za-z_]+)(?:\s|$)')
 $timeMatch=[regex]::Match($line,'(?:^|\s)utc_filetime=(\d+)(?:\s|$)')
 $elapsedMatch=[regex]::Match($line,'(?:^|\s)elapsed_ms=(\d+)(?:\s|$)')
 if(-not ($pidMatch.Success -and $eventMatch.Success -and $timeMatch.Success)) {
     throw 'Malformed WIC trace: PID, event or UTC field missing'
 }
 try {
     $pidNum=[int]::Parse($pidMatch.Groups[1].Value)
     $at=[DateTime]::FromFileTimeUtc([long]::Parse($timeMatch.Groups[1].Value))
     $duration=0L
     if($elapsedMatch.Success){$duration=[long]::Parse($elapsedMatch.Groups[1].Value)}
     $events.Add([pscustomobject]@{
       PID=$pidNum;Event=$eventMatch.Groups[1].Value;Utc=$at;ElapsedMs=$duration
     })
 } catch {
     throw ('Malformed WIC event: '+$_.Exception.Message)
 }
}
$decode=@($events | Where-Object {$_.Event -eq 'DecoderFullDecode'})
$lines=New-Object 'System.Collections.Generic.List[string]'
$lines.Add('COM CPU / WIC time correlation - informational, not module CPU attribution')
$lines.Add('CpuSamplingRows='+$rows.Count)
$lines.Add('WicEvents='+$events.Count)
$lines.Add('WicTraceRecognizedLines='+$recognized)
$lines.Add('WicFullDecodes='+$decode.Count)
$lines.Add('NOTE: An empty WIC trace does not imply that no HEIC decode occurred.')
$lines.Add('NOTE: WIC elapsed_ms is wall time; do not compare it directly with CPU seconds.')
$lines.Add('NOTE: Only WPR/WPA sampled stacks can establish which DLL/function consumes CPU.')
$lines.Add('PID|Process|Samples|MeanCpuTotalPct|PeakCpuTotalPct|WicFullDecodeCount|WicFullDecodeWallMs|DecodeNearCpuSamples|MaxNearCpuPct')
foreach($grp in @($rows | Group-Object PID)) {
 $pidNum=[int]$grp.Name
 $vals=@($grp.Group | ForEach-Object {[double]$_.CpuPctTotal})
 $peak=0.0;$sum=0.0
 foreach($v in $vals){$sum+=$v;if($v -gt $peak){$peak=$v}}
 $mean=if($vals.Count){[Math]::Round($sum/$vals.Count,2)}else{0}
 $pidDecode=@($decode | Where-Object {$_.PID -eq $pidNum})
 $ms=0L
 $nearCount=0;$maxNear=0.0
 foreach($d in $pidDecode) {
    $ms+=$d.ElapsedMs
    # Decode event timestamp is END of call, not its start. Allow a 1.5s
    # sampling tolerance. Distinct same-PID overlapping events are counted
    # separately but do not equal distinct CPU usage.
    $from=$d.Utc.AddMilliseconds(-$d.ElapsedMs-1500)
    $until=$d.Utc.AddMilliseconds(1500)
    foreach($r in $grp.Group) {
      try {
        $t=[datetime]::Parse([string]$r.UtcTime,
             [Globalization.CultureInfo]::InvariantCulture,
             [Globalization.DateTimeStyles]::AdjustToUniversal)
        if($t -ge $from -and $t -le $until) {
          $nearCount++
          $c=[double]$r.CpuPctTotal
          if($c -gt $maxNear){$maxNear=$c}
        }
      } catch {}
    }
 }
 $name=[string]$grp.Group[0].Name
 $lines.Add(('{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}' -f
  $pidNum,$name,$vals.Count,$mean,$peak,$pidDecode.Count,$ms,
  $nearCount,[Math]::Round($maxNear,2)))
}
$tracedPids=@($events | Select-Object -ExpandProperty PID -Unique)
$cpuPids=@($rows | ForEach-Object {[int]$_.PID} | Select-Object -Unique)
$lines.Add('WicTracePids='+($tracedPids -join ','))
$lines.Add('CpuSamplePids='+($cpuPids -join ','))
$lines.Add('CONCLUSION_LIMIT: PID/time overlap supports correlation only, not proof that wic_heic.dll consumed sampled CPU.')
[IO.File]::WriteAllLines($OutFile,$lines.ToArray(),([System.Text.UTF8Encoding]::new($false)))
$lines | ForEach-Object {Write-Host $_}
Write-Host 'CPU_WIC_ANALYZE_PASS'
