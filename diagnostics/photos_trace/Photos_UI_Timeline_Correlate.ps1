# Windows PowerShell 5.1. Read-only correlation of UI time markers and numeric WIC trace.
# Never reads, copies, edits, hashes, or inspects the user's photographs.
param(
    [string]$TimelinePath,
    [string]$TracePath,
    [ValidateRange(1,30)][int]$WindowSeconds=10
)
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot).FullName
if([string]::IsNullOrWhiteSpace($TimelinePath)) {
    $latest=Get-ChildItem -LiteralPath $root -File -Filter 'Photos_UI_Timeline_*.csv' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if(-not $latest){throw 'No Photos_UI_Timeline_*.csv found.'}
    $TimelinePath=$latest.FullName
}
if([string]::IsNullOrWhiteSpace($TracePath)) {
    $latestTrace=Get-ChildItem -LiteralPath $root -File -Filter 'WIC_Photos_Trace_*.txt' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if($latestTrace) { $TracePath=$latestTrace.FullName }
}
$timeline=Get-Item -LiteralPath $TimelinePath -ErrorAction Stop
if($timeline.Length -gt 1048576){throw 'UI timeline exceeds expected 1MiB safety limit.'}
$samples=@(Import-Csv -LiteralPath $timeline.FullName -Encoding UTF8)
if($samples.Count -lt 1){throw 'UI timeline is empty.'}
$marks=New-Object 'System.Collections.Generic.List[object]'
foreach($one in $samples) {
    if($one.event -notin @('MARK_F9','SAMPLE')) {
        throw 'Unexpected event kind in UI timeline.'
    }
    if($one.utc_filetime -notmatch '^\d{15,20}$' -or
       $one.tick -notmatch '^\d{1,20}$' -or
       $one.foreground_pid -notmatch '^\d+$') {
        throw 'Invalid numeric UI timeline stamp.'
    }
    if($one.event -eq 'MARK_F9') {$marks.Add($one)}
}
$events=New-Object 'System.Collections.Generic.List[object]'
$note='NO_TRACE_SPECIFIED'
if(-not [string]::IsNullOrWhiteSpace($TracePath)) {
    $trace=Get-Item -LiteralPath $TracePath -ErrorAction Stop
    if($trace.Length -gt 2097152){throw 'WIC trace exceeds the 2MiB hard cap.'}
    $note='TRACE_PRESENT'
    foreach($line in (Get-Content -LiteralPath $trace.FullName -Encoding UTF8)) {
        if($line -notmatch '^WIC_HEIC_TRACE v1 '){continue}
        $stamp=[regex]::Match($line,'\btick=(\d+)\b')
        $utc=[regex]::Match($line,'\butc_filetime=(\d+)\b')
        $pidMatch=[regex]::Match($line,'\bpid=(\d+)\b')
        $eventMatch=[regex]::Match($line,'\bevent=([A-Za-z][A-Za-z0-9]*)\b')
        $elapsedMatch=[regex]::Match($line,'\belapsed_ms=(\d+)\b')
        if(-not($stamp.Success -and $utc.Success -and
                $pidMatch.Success -and $eventMatch.Success)){continue}
        $events.Add([pscustomobject]@{
            tick=[long]$stamp.Groups[1].Value
            utc=[long]$utc.Groups[1].Value
            pid=[int]$pidMatch.Groups[1].Value
            event=$eventMatch.Groups[1].Value
            elapsed=$(if($elapsedMatch.Success){[long]$elapsedMatch.Groups[1].Value}else{[long]0})
        })
    }
}
$lines=New-Object 'System.Collections.Generic.List[string]'
$lines.Add('Windows Photos UI / numeric WIC event timing correlation')
$lines.Add('UI samples='+$samples.Count+' manual_F9_marks='+$marks.Count+' WIC_events='+$events.Count)
$lines.Add('WIC trace status='+$note+' within_window_seconds='+$WindowSeconds)
$lines.Add('No pictures, filenames, window titles, screenshots or EXIF were collected.')
if($marks.Count -eq 0) {
    $lines.Add('NO_F9_MARK: no marked visual glitch; timing causality remains UNKNOWN.')
}
$radius=[long]$WindowSeconds*1000
foreach($mark in $marks) {
    $mt=[long]$mark.tick
    $timestamp=[DateTime]::FromFileTimeUtc([long]$mark.utc_filetime).ToString('o')
    $pidValue=[int]$mark.foreground_pid
    $near=@($events | Where-Object {
        [math]::Abs([double]($_.tick-$mt)) -le $radius
    })
    $decoded=@($near | Where-Object {$_.event -eq 'DecoderFullDecode'})
    $copy=@($near | Where-Object {$_.event -eq 'DecoderCopyPixels'})
    $opens=@($near | Where-Object {$_.event -eq 'DecoderOpen'})
    $parallel=@($decoded | Where-Object {
        $_.elapsed -gt 0 -and $_.tick -ge $mt -and
        ($_.tick-$_.elapsed) -le $mt
    })
    $related=@($near | ForEach-Object {$_.pid} | Sort-Object -Unique)
    $lines.Add(('MARK utc={0} tick={1} foreground_pid={2} window={3}x{4} client={5}x{6}' -f
        $timestamp,$mt,$pidValue,$mark.window_w,$mark.window_h,$mark.client_w,$mark.client_h))
    $lines.Add(('  nearby_wic={0} opens={1} full_decodes={2} sampled_copy_pixels={3} approximate_decode_overlap={4} wic_pids={5}' -f
        $near.Count,$opens.Count,$decoded.Count,$copy.Count,$parallel.Count,($related -join ';')))
    foreach($item in ($decoded | Sort-Object tick | Select-Object -Last 8)) {
        $offset=[long]($item.tick-$mt)
        $lines.Add(('  DECODE_END offset_ms={0} duration_ms={1} pid={2} inferred_start_offset_ms={3}' -f
            $offset,$item.elapsed,$item.pid,($offset-$item.elapsed)))
    }
    if($near.Count -eq 0) {
        $lines.Add('  NO_WIC_EVENTS_NEAR_MARK: inconclusive; Photos may use another decoder, or trace may be disabled/expired/dropped.')
    }
}
$lines.Add('LIMITATION: window/client sizes DO NOT measure the image displayed inside Photos.')
$lines.Add('LIMITATION: foreground PID may be ApplicationFrameHost rather than the WIC codec process.')
$lines.Add('LIMITATION: decode overlap is approximate (post-decode logging, timer resolution and log contention).')
$lines.Add('LIMITATION: a marked event is manually observed; WIC event correlation is not proof of causality.')
$out=Join-Path $root ('Photos_UI_Correlation_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+[guid]::NewGuid().ToString('N').Substring(0,8)+'.txt')
$stream=[System.IO.File]::Open($out,[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::Read)
try {
    $text=($lines -join [Environment]::NewLine)+[Environment]::NewLine
    $data=[System.Text.Encoding]::UTF8.GetBytes($text)
    $stream.Write($data,0,$data.Length)
} finally { $stream.Dispose() }
$lines | ForEach-Object {Write-Host $_}
Write-Host ('Report saved: '+$out)
