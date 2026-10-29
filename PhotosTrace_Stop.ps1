# Windows 10 / PowerShell 5.1. Stop only the diagnostic session owned by this folder.
$ErrorActionPreference = 'Stop'
$state = Join-Path $PSScriptRoot 'WIC_Photos_Trace_ACTIVE.txt'
$key = 'HKCU:\Software\WicHeic'
if (-not (Test-Path -LiteralPath $state)) {
    Write-Host 'No trace session belongs to this folder.'
    exit 0
}
$raw = [System.IO.File]::ReadAllText($state,[System.Text.Encoding]::UTF8).Trim()
$expected = $raw
$expectedDeadline = $null
# Legacy plain-path state files are accepted, but deadline ownership is unknown.
if ($raw.StartsWith('{')) {
    $record = $raw | ConvertFrom-Json -ErrorAction Stop
    $expected = [string]$record.TraceFile
    $expectedDeadline = [long]$record.TraceUntilFileTime
}
if ([string]::IsNullOrWhiteSpace($expected) -or $expected -notmatch '^[A-Za-z]:\\') {
    throw 'Invalid diagnostic state; refusing registry modification.'
}
$current = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
$hasFile = ($current -and (@($current.PSObject.Properties.Name) -contains 'TraceFile'))
if ($hasFile) {
    if (-not [string]::Equals($expected,[string]$current.TraceFile,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Another trace configuration is active; refusing to modify it.'
    }
    if ($null -ne $expectedDeadline -and
        [long]$current.TraceUntilFileTime -ne $expectedDeadline) {
        throw 'Trace deadline changed during this session; refusing to modify it.'
    }
    Remove-ItemProperty -Path $key -Name 'TraceFile' -ErrorAction Stop
    if ($null -ne $expectedDeadline) {
        Remove-ItemProperty -Path $key -Name 'TraceUntilFileTime' -ErrorAction Stop
    } elseif ($current -and (@($current.PSObject.Properties.Name) -contains 'TraceUntilFileTime')) {
        # Pre-existing old-format state did not record ownership of this value.
        Write-Warning 'Legacy state: deadline left untouched; inspect HKCU\Software\WicHeic.'
    }
} else {
    Write-Warning 'TraceFile is no longer configured; preserving unrelated registry values.'
}
Remove-Item -LiteralPath $state -Force -ErrorAction Stop
Write-Host 'WIC diagnostic capture stopped; logs retained.' -ForegroundColor Green
Write-Host "Log: $expected"
