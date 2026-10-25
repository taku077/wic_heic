# PowerShell 5.1, text-only. Summarizes privacy-minimal WIC trace events.
# Never opens photos, checks EXIF, edits files, or launches Windows Photos.
param([string]$TracePath)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($TracePath)) {
    $latest = Get-ChildItem -LiteralPath $PSScriptRoot -Filter 'WIC_Photos_Trace_*.txt' -File |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw 'No WIC_Photos_Trace text file found in this folder.' }
    $TracePath = $latest.FullName
}
if (-not (Test-Path -LiteralPath $TracePath -PathType Leaf)) { throw 'Trace text file not found.' }
if ((Get-Item -LiteralPath $TracePath).Length -gt 2.1MB) {
    throw 'Trace exceeds size cap; refusing unexpectedly large file.'
}
$lines = @(Get-Content -LiteralPath $TracePath -Encoding UTF8 |
    Where-Object { $_ -match '^WIC_HEIC_TRACE v1 ' })
$events = @()
foreach ($line in $lines) {
    if ($line -match '\bevent=([A-Za-z][A-Za-z0-9]*)\b' -and
        $line -match '\bpid=(\d+)\b' -and
        $line -match '\bsource=([a-zA-Z0-9-]+)\b') {
        $e = [regex]::Match($line,'\bevent=([A-Za-z][A-Za-z0-9]*)\b').Groups[1].Value
        $p = [regex]::Match($line,'\bpid=(\d+)\b').Groups[1].Value
        $source = [regex]::Match($line,'\bsource=([a-zA-Z0-9-]+)\b').Groups[1].Value
        # Extra numeric fields exist only in newer traces. Keep full
        # compatibility with the earlier WIC_HEIC_TRACE v1 lines.
        $operation = [regex]::Match($line,'\boperation=(\d+)\b')
        $elapsed = [regex]::Match($line,'\belapsed_ms=(\d+)\b')
        $tick = [regex]::Match($line,'\btick=(\d+)\b')
        $utc = [regex]::Match($line,'\butc_filetime=(\d+)\b')
        $tid = [regex]::Match($line,'\btid=(\d+)\b')
        $events += [pscustomobject]@{
            Event=$e; PID=$p; Source=$source
            Operation=$(if ($operation.Success) {[long]$operation.Groups[1].Value} else {[long]0})
            ElapsedMs=$(if ($elapsed.Success) {[long]$elapsed.Groups[1].Value} else {[long]0})
            Tick=$(if ($tick.Success) {[long]$tick.Groups[1].Value} else {[long]0})
            UtcFileTime=$(if ($utc.Success) {[long]$utc.Groups[1].Value} else {[long]0})
            TID=$(if ($tid.Success) {[long]$tid.Groups[1].Value} else {[long]0})
        }
    }
}
$names=@('DecoderOpen','DecoderGetFrame','DecoderFrameGetSize','DecoderCopyPixels',
         'EncoderInitialize','CreateNewFrame','FrameInitializeEnter','FrameInitializeOptions',
         'WriteSourceEnter','WriteSourceInputClass','WriteSourceOriginalExact',
         'WriteSourceLossless','WriteSourceRaster','WriteSourceRejected',
         'WritePixelsEnter','SetColorContexts','SetThumbnail','MetadataWriterUnavailable',
         'EncoderSetColorContexts','EncoderSetThumbnail','EncoderSetPreview','EncoderMetadataWriterUnavailable',
         'EncoderCommitEnter','EncoderCommitLossless','EncoderCommitRecompressed',
         'EncoderCommitRejected','EncoderCommitOverlap','EncoderCommitExit')
$linesOut=New-Object 'System.Collections.Generic.List[string]'
$linesOut.Add('WIC HEIC / Windows Photos call-path audit (text-only)')
$linesOut.Add('-----------------------------------------------')
$linesOut.Add('Log events parsed: '+$events.Count)
foreach ($name in $names) {
    $count=@($events | Where-Object { $_.Event -eq $name }).Count
    $linesOut.Add(('{0}={1}' -f $name,$count))
}
$linesOut.Add('Process IDs (not proof of application identity):')
foreach($group in ($events | Group-Object PID)) {
    $linesOut.Add(('  pid={0} events={1}' -f $group.Name,$group.Count))
}
# Narrow root-cause report: link each WIC source kind to the save branch
# within the same process. Does NOT assume that any PID is Windows Photos.
$linesOut.Add('ENCODE PATHS BY PID (correlate PID with Photos separately):')
foreach($group in ($events | Group-Object PID)) {
    $kinds=@($group.Group | Where-Object {$_.Event -eq 'WriteSourceInputClass'} |
        ForEach-Object {$_.Source} | Select-Object -Unique)
    $losslessN=@($group.Group | Where-Object {$_.Event -eq 'EncoderCommitLossless'}).Count
    $rasterN=@($group.Group | Where-Object {$_.Event -eq 'EncoderCommitRecompressed'}).Count
    if($losslessN -eq 0 -and $rasterN -eq 0 -and $kinds.Count -eq 0) {continue}
    $choice='NO_COMPLETED_SAVE'
    if($losslessN -gt 0 -and $rasterN -gt 0) {$choice='MIXED_SEPARATE_SAVES'}
    elseif($losslessN -gt 0) {$choice='LOSSLESS_HEIF_SOURCE_PRESERVED'}
    elseif($rasterN -gt 0) {$choice='REENCODED_RASTER_HEVC'}
    $linesOut.Add(('  pid={0} decision={1} losslessCommits={2} rasterCommits={3} sourceKinds={4}' -f
        $group.Name,$choice,$losslessN,$rasterN,($kinds -join ',')))
}
$linesOut.Add('ENCODER COMMIT TIMING (WIC-internal duration, not disk replacement):')
foreach ($group in ($events | Group-Object PID)) {
    $begins=@($group.Group | Where-Object {
        $_.Event -eq 'EncoderCommitEnter' -and $_.Operation -gt 0 })
    $exits=@($group.Group | Where-Object {
        $_.Event -eq 'EncoderCommitExit' -and $_.Operation -gt 0 })
    if ($begins.Count -eq 0 -and $exits.Count -eq 0) { continue }
    $ids=@{}
    foreach ($b in $begins) { $ids[[string]$b.Operation]=$true }
    $completed=@($exits | Where-Object { $ids.ContainsKey([string]$_.Operation) })
    $overlaps=@($group.Group | Where-Object { $_.Event -eq 'EncoderCommitOverlap' })
    $line=('  pid={0} begins={1} matchedExits={2} overlaps={3}' -f
        $group.Name,$begins.Count,$completed.Count,$overlaps.Count)
    if ($completed.Count -gt 0) {
        $durations=@($completed | ForEach-Object { [long]$_.ElapsedMs } | Sort-Object)
        $min=[long]$durations[0]
        $max=[long]$durations[$durations.Count-1]
        $mid=[long]$durations[[int][math]::Floor(($durations.Count-1)/2)]
        $line += (' elapsedMsMin={0} elapsedMsMedianLower={1} elapsedMsMax={2}' -f
            $min,$mid,$max)
    }
    $linesOut.Add($line)
}
$linesOut.Add('CAUTION: EncoderCommitExit means codec Write/encode returned; Photos may close/replace the file later.')
$linesOut.Add('CAUTION: A thumbnail orientation change is NOT a verified disk-save completion event.')
$linesOut.Add('CAUTION: Compare utc_filetime with separate FileSystemWatcher UTC events; no file identity is in this summary.')
$linesOut.Add('An empty or unrelated PID is INCONCLUSIVE, not proof that Photos bypassed WIC.')
$decoded=@($events | Where-Object { $_.Event -eq 'DecoderOpen' }).Count
$encoding=@($events | Where-Object { $_.Event -eq 'EncoderInitialize' }).Count
$lossless=@($events | Where-Object { $_.Event -eq 'EncoderCommitLossless' }).Count
$lossy=@($events | Where-Object { $_.Event -eq 'EncoderCommitRecompressed' }).Count
$raster=@($events | Where-Object { $_.Event -eq 'WriteSourceRaster' }).Count
$blocked=@($events | Where-Object { $_.Event -eq 'WriteSourceRejected' }).Count
if($events.Count -eq 0) {
    $linesOut.Add('INTERPRETATION: No readable codec trace; this does NOT prove Photos bypassed WIC.')
} elseif($encoding -eq 0) {
    $linesOut.Add('INTERPRETATION: Decode events but no encoder events; check whether Photos used another encoder.')
} elseif($lossy -gt 0) {
    $linesOut.Add('INTERPRETATION: HEVC re-encoding was observed in at least one traced process.')
} elseif($lossless -gt 0) {
    $linesOut.Add('INTERPRETATION: At least one WIC transaction used metadata-only rotation.')
} elseif($blocked -gt 0) {
    $linesOut.Add('INTERPRETATION: WIC refused an edit without a suitable lossless source.')
} else {
    $linesOut.Add('INTERPRETATION: No completed encoder transaction captured.')
}
$linesOut.Add('CAUTION: Process IDs and isolated events cannot alone identify a Photos UI action.')
$linesOut.Add('CAUTION: Trace-only findings do not prove preservation of EXIF, ICC or HDR in real files.')
$out = Join-Path $PSScriptRoot ('WIC_Photos_Trace_Summary_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'.txt')
$payload=($linesOut -join [Environment]::NewLine)+[Environment]::NewLine
$bytes=[System.Text.Encoding]::UTF8.GetBytes($payload)
$f=[System.IO.File]::Open($out,[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::Read)
try {$f.Write($bytes,0,$bytes.Length)} finally {$f.Dispose()}
$linesOut | ForEach-Object { Write-Host $_ }
Write-Host "Summary saved: $out"
