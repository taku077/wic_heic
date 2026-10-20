# Requires only ordinary user permissions. Windows 10 PowerShell 5.1.
# Enable privacy-minimal trace for at most 20 minutes. Never opens/modifies images.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$state = Join-Path $root 'WIC_Photos_Trace_ACTIVE.txt'
$key = 'HKCU:\Software\WicHeic'
if (Test-Path -LiteralPath $state) { throw 'An active session is already recorded; stop it first.' }
if ($root -notmatch '^[A-Za-z]:\\') { throw 'Extract this kit to a local drive (e.g. C:\). Network paths are not supported.' }
$existing = Get-ItemProperty -Path $key -Name TraceFile -ErrorAction SilentlyContinue
if ($existing -and $existing.TraceFile) { throw 'An existing WicHeic trace configuration was found. No changes made.' }

$name = 'WIC_Photos_Trace_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff') + '.txt'
$trace = Join-Path $root $name
if ($trace.Length -ge 500) { throw 'Trace path is too long.' }
$file = [System.IO.File]::Open($trace,[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::ReadWrite)
$file.Dispose()
try {
    # Trace expires even if the operator forgets to run Stop.
    $until = [DateTime]::UtcNow.AddMinutes(20).ToFileTimeUtc()
    if (-not (Test-Path -LiteralPath $key)) { New-Item -Path $key -Force | Out-Null }
    New-ItemProperty -Path $key -Name 'TraceUntilFileTime' -PropertyType QWord -Value $until -Force | Out-Null
    New-ItemProperty -Path $key -Name 'TraceFile' -PropertyType String -Value $trace -Force | Out-Null
    [System.IO.File]::WriteAllText($state,$trace,[System.Text.Encoding]::UTF8)
} catch {
    Remove-ItemProperty -Path $key -Name 'TraceFile' -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path $key -Name 'TraceUntilFileTime' -ErrorAction SilentlyContinue
    throw
}
Write-Host "Diagnostic capture active for up to 20 minutes." -ForegroundColor Green
Write-Host "Log file: $trace"
Write-Host "Use only disposable SYNTHETIC HEIC test images in Windows Photos."
Write-Host "When finished, run PhotosTrace_Stop.ps1. Original image editing is prohibited."
