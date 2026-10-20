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
        $events += [pscustomobject]@{ Event=$e; PID=$p; Source=$source }
    }
}
$names=@('DecoderOpen','DecoderGetFrame','DecoderFrameGetSize','DecoderCopyPixels',
         'EncoderInitialize','CreateNewFrame','FrameInitializeEnter','FrameInitializeOptions',
         'WriteSourceEnter','WriteSourceLossless','WriteSourceRaster','WriteSourceRejected',
         'WritePixelsEnter','SetColorContexts','SetThumbnail','MetadataWriterUnavailable',
         'EncoderCommitLossless','EncoderCommitRecompressed','EncoderCommitRejected')
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
