#requires -version 5.1
# WicHeic experimental MSI. A new installation is permitted ONLY when
# neither target WIC COM CLSID nor matching WicHeic MSI is already registered.
# All preflight/log files stay beside this script. No user photos are touched.
param([switch]$CheckOnly)
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
if($root -notmatch '^[A-Za-z]:\\') { throw 'Unpack this kit onto a local drive.' }
$node=Get-Item -LiteralPath $root -ErrorAction Stop
while($null -ne $node) {
  if(($node.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Refusing symlink or junction path.'
  }
  $node=$node.Parent
}
if(-not [Environment]::Is64BitOperatingSystem) { throw 'x64 Windows required.' }
$msi=Join-Path $root 'WicHeic-Repair-EXPERIMENTAL-x64.msi'
$inventory=Join-Path $root 'WIC_Preflight_ReadOnly.ps1'
if(-not (Test-Path -LiteralPath $msi -PathType Leaf)) { throw 'MSI missing from extracted ZIP.' }
if(-not (Test-Path -LiteralPath $inventory -PathType Leaf)) { throw 'Preflight script missing.' }
foreach($path in @($msi,$inventory)) {
  if(((Get-Item -LiteralPath $path).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw 'Refusing a linked MSI or inventory script.'
  }
}
# Snapshot BEFORE touching the installed codec; retain even after failure.
$beforePath=(& $inventory | Select-Object -Last 1)
if(-not (Test-Path -LiteralPath $beforePath -PathType Leaf)) {
  throw 'Read-only WIC preflight failed; installation blocked.'
}
$before=Get-Content -LiteralPath $beforePath -Raw -Encoding UTF8 | ConvertFrom-Json
if($before.Schema -ne 1 -or @($before.Registrations).Count -ne 12) {
  throw 'Incomplete WIC registry snapshot; installation blocked.'
}
$occupied=@($before.Registrations | Where-Object { -not [string]::IsNullOrWhiteSpace($_.InprocServer32) })
# The v0.4.0 Shell preview MSI is deliberately independent and must NOT
# trigger the collision guard for this system-wide decoder/encoder repair.
# Reject every other matching WicHeic MSI (including an old global WIC).
$allMsi=@($before.WicHeicMsi)
$knownPreview=@($allMsi | Where-Object {
 $_.Name -eq 'WicHeic Fast Thumbnail Preview (Experimental)' -and $_.Version -eq '0.4.0'
})
$otherMsi=@($allMsi | Where-Object {
 -not ($_.Name -eq 'WicHeic Fast Thumbnail Preview (Experimental)' -and $_.Version -eq '0.4.0')
})
if($knownPreview.Count -gt 1){throw 'Unexpected multiple v0.4.0 preview MSI entries.'}
if($occupied.Count -gt 0 -or $otherMsi.Count -gt 0) {
  Write-Warning 'INSTALL_BLOCKED_EXISTING_WIC: Existing WIC codec detected.'
  Write-Host 'Your original WIC installer/restore method must be verified BEFORE any replacement.'
  Write-Host 'No install/uninstall or WIC registry changes were made.'
  Write-Host "Read-only baseline: $beforePath"
  throw 'Existing codec: safe in-place upgrade cannot be guaranteed from this kit.'
}
Write-Host 'WIC_PREFLIGHT_CLEAR: target encoder/decoder registrations not occupied.'
Write-Host "Baseline retained: $beforePath"
if($CheckOnly) { Write-Output 'WIC_SAFE_INSTALL_CHECK_ONLY_PASS'; return }
$log=Join-Path $root ('WIC_Install_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'.log')
# System installer handles UAC; do not silently force removal or install other software.
$argsText='/i "'+$msi+'" /passive /norestart /l*v "'+$log+'"'
$proc=Start-Process -FilePath 'msiexec.exe' -ArgumentList $argsText -Wait -PassThru
if($proc.ExitCode -ne 0) {
  throw "MSI installer exit code $($proc.ExitCode); no automated rollback was attempted. Log: $log"
}
$afterPath=(& $inventory | Select-Object -Last 1)
$after=Get-Content -LiteralPath $afterPath -Raw -Encoding UTF8 | ConvertFrom-Json
$registered=@($after.Registrations | Where-Object {
  $_.Hive -eq 'LocalMachine' -and $_.View -eq 'Registry64' -and
  -not [string]::IsNullOrWhiteSpace($_.InprocServer32) -and
  $_.SHA256 -match '^[0-9A-F]{64}$'
})
if(@($registered | Where-Object {$_.Codec -eq 'Decoder'}).Count -ne 1 -or
   @($registered | Where-Object {$_.Codec -eq 'Encoder'}).Count -ne 1) {
  throw "INSTALL_NEEDS_ATTENTION: MSI returned success but encoder/decoder registration verification failed. Do not edit photos. Logs: $log"
}
Write-Host 'WIC_MSI_INSTALLED_AND_REGISTERED: decoder and encoder verified.'
Write-Host 'The Windows 10 Photos UI has NOT been tested.'
Write-Host "Before snapshot: $beforePath"
Write-Host "After snapshot: $afterPath"
Write-Host "Installer log: $log"
