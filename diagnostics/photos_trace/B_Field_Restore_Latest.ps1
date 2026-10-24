#requires -version 5.1
# Wrapper for an explicit, user-initiated, one-click A/B restore.
# Never guesses between multiple sessions; delegates hash/registry gate
# to WIC120_OneTimeTrial.ps1. Does not touch photos.
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
$sessions=@(Get-ChildItem -LiteralPath $root -Filter 'WIC120_Session_*.json' -File)
if($sessions.Count -ne 1) {
  throw ('Restore needs exactly ONE trial session in this folder. Found '+$sessions.Count+
    '. Keep all files and ask for help; no Windows changes made.')
}
$script=Join-Path $root 'WIC120_OneTimeTrial.ps1'
& $script -Restore -Session $sessions[0].FullName
if(-not $?){throw 'Trial restore did not complete'}
