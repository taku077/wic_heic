# Windows PowerShell 5.1. Short, opt-in session with safe automatic trace cleanup.
# No installer or Windows Photos modifications. Does temporarily configure
# the two documented HKCU WIC numeric trace values for at most 20 minutes.
param(
    [ValidateRange(5,180)][int]$DurationSeconds=90,
    [ValidateRange(100,1000)][int]$IntervalMs=200
)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$start=Join-Path $root 'PhotosTrace_Start.ps1'
$stop=Join-Path $root 'PhotosTrace_Stop.ps1'
$capture=Join-Path $root 'Photos_UI_Timeline_Capture.ps1'
$correlate=Join-Path $root 'Photos_UI_Timeline_Correlate.ps1'
foreach($needed in @($start,$stop,$capture,$correlate)){
    if(-not (Test-Path -LiteralPath $needed -PathType Leaf)){
        throw 'UI timing kit is incomplete. No diagnostic was started.'
    }
}
$owned=$false
$tracePath=$null
try {
    & $start
    $owned=$true
    $settings=Get-ItemProperty -LiteralPath 'HKCU:\Software\WicHeic' -ErrorAction Stop
    $tracePath=[string]$settings.TraceFile
    & $capture -DurationSeconds $DurationSeconds -IntervalMs $IntervalMs
} finally {
    if($owned){ & $stop }
}
$timeline=Get-ChildItem -LiteralPath $root -Filter 'Photos_UI_Timeline_*.csv' -File |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if(-not $timeline){throw 'No UI timeline was saved.'}
& $correlate -TimelinePath $timeline.FullName -TracePath $tracePath -WindowSeconds 10
Write-Host 'Capture and correlation finished. Keep the CSV and text report for diagnosis.'
