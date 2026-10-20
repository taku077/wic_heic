# Safe stop: removes only the TraceFile setting owned by THIS kit.
# Preserves logs, synthetic test images, installed codecs and unrelated registry values.
$ErrorActionPreference='Stop'
$state=Join-Path $PSScriptRoot 'WIC_Photos_Trace_ACTIVE.txt'
$key='HKCU:\Software\WicHeic'
if (-not (Test-Path -LiteralPath $state)) {
    Write-Host 'No active trace session belongs to this folder.'
    exit 0
}
$expected=[System.IO.File]::ReadAllText($state,[System.Text.Encoding]::UTF8).Trim()
$current=Get-ItemProperty -Path $key -Name TraceFile -ErrorAction SilentlyContinue
if ($current -and $current.TraceFile -and
    (-not [string]::Equals($expected,$current.TraceFile,[System.StringComparison]::OrdinalIgnoreCase))) {
    throw 'Another trace configuration is active; refusing to modify it.'
}
if ($current -and $current.TraceFile) {
    Remove-ItemProperty -Path $key -Name TraceFile -ErrorAction Stop
    Remove-ItemProperty -Path $key -Name TraceUntilFileTime -ErrorAction SilentlyContinue
}
Remove-Item -LiteralPath $state -Force -ErrorAction Stop
Write-Host 'WIC diagnostic capture stopped. Existing log files were retained.' -ForegroundColor Green
Write-Host "Log: $expected"
