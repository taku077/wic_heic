#requires -version 5.1
# Restore THIS kit or a previous kit's exact pre-install state using
# current safe MSI flags. Never guess between multiple stored sessions.
[CmdletBinding()]
param([string]$PreviousKitDirectory)
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
function FindSessions([string]$directory) {
  if(-not (Test-Path -LiteralPath $directory -PathType Container)){return @()}
  return @(Get-ChildItem -LiteralPath $directory -File -Filter 'WIC120_Session_*.json' -ErrorAction Stop)
}
$found=@(FindSessions $root)
$fromPrior=$false
if($found.Count -eq 0) {
  $prior=@()
  if(-not [string]::IsNullOrWhiteSpace($PreviousKitDirectory)){
    $prior=@(FindSessions $PreviousKitDirectory)
  } else {
    # A former B kit may be in a *different extracted directory* in Downloads.
    # Search by filename and validate the complete kit before using anything.
    $downloads=Join-Path $env:USERPROFILE 'Downloads'
    if(Test-Path -LiteralPath $downloads -PathType Container){
      $prior=@(Get-ChildItem -LiteralPath $downloads -File -Filter 'WIC120_Session_*.json' -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.DirectoryName -ne $root })
    }
  }
  # Only consider sessions with both original and research trial MSI.
  $found=@($prior|Where-Object {
    (Test-Path -LiteralPath (Join-Path $_.DirectoryName 'WicHeic-Repair-1.1.18-ORIGINAL-x64.msi') -PathType Leaf) -and
    (Test-Path -LiteralPath (Join-Path $_.DirectoryName 'WicHeic-Repair-1.1.20-TRIAL-x64.msi') -PathType Leaf)
  })
  $fromPrior=$true
}
if($found.Count -ne 1){
  Write-Host 'Restore NOT started. Expected exactly one matching saved trial session.'
  Write-Host ('Found sessions: '+$found.Count)
  $found|ForEach-Object {Write-Host $_.FullName}
  throw 'NO_AUTOMATIC_GUESS: keep your former kit and its WIC120_Session_*.json unchanged'
}
$session=$found[0]
if($fromPrior) {
  Write-Host '以前の比較用キットの復元記録が見つかりました。' -ForegroundColor Yellow
  Write-Host ('復元対象のキット: '+$session.DirectoryName)
  if(-not ($env:GITHUB_ACTIONS -eq 'true' -and $env:RUNNER_TEMP)) {
    $answer=Read-Host 'このキットの旧1.1.18へ復元しますか？ YES と入力'
    if($answer -cne 'YES'){throw 'Cancelled. No installer started.'}
  }
}
$script=Join-Path $root 'WIC120_OneTimeTrial.ps1'
& $script -Restore -Session $session.FullName -KitRoot $session.DirectoryName
if(-not $?){throw 'RESTORE_NOT_VERIFIED: stop all MSI operations and preserve the logs'}
