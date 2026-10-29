# Windows 10 / PowerShell 5.1. No image file is ever opened or modified.
$ErrorActionPreference = 'Stop'
$root = (Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
$state = Join-Path $root 'WIC_Photos_Trace_ACTIVE.txt'
$key = 'HKCU:\Software\WicHeic'
if ($root -notmatch '^[A-Za-z]:\\') { throw 'Extract this kit to a local drive. Network or relative paths are not supported.' }
# Reject junctions/symlinks in ANY ancestor, not only the final directory.
$parent = Get-Item -LiteralPath $root -ErrorAction Stop
while ($null -ne $parent) {
    if (($parent.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Reparse-point directories are prohibited for diagnostic logs.'
    }
    $parent = $parent.Parent
}
if (Test-Path -LiteralPath $state) { throw 'An active session is already recorded; stop it first.' }
# Protect BOTH pre-existing values (even an empty TraceFile value). No takeover.
$existing = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
if ($existing -and ((@($existing.PSObject.Properties.Name) -contains 'TraceFile') -or
                   (@($existing.PSObject.Properties.Name) -contains 'TraceUntilFileTime'))) {
    throw 'Existing WicHeic trace registry values detected. No changes made.'
}
$name = 'WIC_Photos_Trace_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff') + '.txt'
$trace = Join-Path $root $name
if ($trace.Length -ge 500) { throw 'Trace path is too long.' }
$file = [System.IO.File]::Open($trace,[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::ReadWrite)
$file.Dispose()
$until = [DateTime]::UtcNow.AddHours(12).ToFileTimeUtc()
$createdUntil = $false
$createdFile = $false
try {
    if (-not (Test-Path -LiteralPath $key)) { New-Item -Path $key -Force | Out-Null }
    New-ItemProperty -Path $key -Name 'TraceUntilFileTime' -PropertyType QWord -Value $until -ErrorAction Stop | Out-Null
    $createdUntil = $true
    New-ItemProperty -Path $key -Name 'TraceFile' -PropertyType String -Value $trace -ErrorAction Stop | Out-Null
    $createdFile = $true
    # Save the deadline as an ownership token so Stop never removes a changed one.
    $record = @{ TraceFile = $trace; TraceUntilFileTime = $until } | ConvertTo-Json -Compress
    [System.IO.File]::WriteAllText($state,$record,[System.Text.Encoding]::UTF8)
} catch {
    # Roll back only values that THIS invocation created and that still match.
    $current = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
    if ($createdFile -and $current -and
        [string]::Equals([string]$current.TraceFile,$trace,[System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-ItemProperty -Path $key -Name 'TraceFile' -ErrorAction SilentlyContinue
    }
    if ($createdUntil -and $current -and $current.TraceUntilFileTime -eq $until) {
        Remove-ItemProperty -Path $key -Name 'TraceUntilFileTime' -ErrorAction SilentlyContinue
    }
    throw
}
Write-Host 'Diagnostic capture opt-in valid for 12 hours (safety), WPR manual stop.' -ForegroundColor Green
Write-Host "Log file: $trace"
Write-Host 'Use your real existing HEIC folder. Do not save over original images.'
Write-Host 'After testing, run PhotosTrace_Stop.ps1.'
