# Windows PowerShell 5.1 CI-only validation of 1.1.31 installer registry policy.
# Disposable HKCU fake association; NEVER touches .heic or actual ShellEx.
# Installation policy: do not replace unknown handlers, persist predecessor,
# compare-before-restoring, preserve predecessor across upgrades.
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'CI-only registry model'}
$root='HKCU:\Software\WicHeic\CI_ShellRegistry_'+[guid]::NewGuid().ToString('N')
$association=Join-Path $root 'FakeAssociation'
$journal=Join-Path $root 'Journal'
$known='{C7657C4A-9F68-40FA-A4DF-96BC08EB3551}' # v1.1.30 registration
$ours='{98E34B24-DAFA-44C7-89A4-5FB39F72B791}'
$other='{AAAA0000-BBBB-CCCC-DDDD-EEEEEEEEEEEE}'
function Assert([bool]$okay,[string]$message){if(-not $okay){throw $message}}
function ReadValue([string]$path) {
 if(-not(Test-Path -LiteralPath $path)){return $null}
 return (Get-Item -LiteralPath $path).GetValue('',$null)
}
function WriteValue([string]$path,[string]$val) {
 New-Item -Path $path -Force | Out-Null
 (Get-Item -LiteralPath $path).SetValue('',$val,[Microsoft.Win32.RegistryValueKind]::String)
}
function ClearTrial {
 if(Test-Path -LiteralPath $association){Remove-Item -LiteralPath $association -Recurse -Force}
 if(Test-Path -LiteralPath $journal){Remove-Item -LiteralPath $journal -Recurse -Force}
}
function RegisterTrial {
 $current=ReadValue $association
 if($current -eq $ours) {
   # A product already installed must have a valid persistent backup.
   if(-not(Test-Path -LiteralPath $journal)){return 'SKIP_ORPHANED_OWN'}
   return 'EXISTING'
 }
 if($null -ne $current -and $current -ine $known){return 'SKIP_FOREIGN'}
 # IMPORTANT: preserve original backup on MSI upgrade (never overwrite it).
 if(-not(Test-Path -LiteralPath $journal)){
   New-Item -Path $journal -Force | Out-Null
   Set-ItemProperty -LiteralPath $journal -Name PriorExists -Value ([int]($null -ne $current))
   if($null -ne $current){Set-ItemProperty -LiteralPath $journal -Name PriorValue -Value $current}
 }
 WriteValue $association $ours
 return 'REGISTERED'
}
function UnregisterTrial {
 if(-not(Test-Path -LiteralPath $journal)){return 'SKIP_NO_JOURNAL'}
 if((ReadValue $association) -ine $ours) {
   # Someone changed the association after installation. Do not undo them.
   Remove-Item -LiteralPath $journal -Recurse -Force
   return 'SKIP_USER_CHANGED'
 }
 $state=Get-ItemProperty -LiteralPath $journal
 Assert ($null -ne $state.PriorExists) 'Missing prior-value sentinel'
 if([int]$state.PriorExists -eq 1) {
   Assert (-not [string]::IsNullOrEmpty([string]$state.PriorValue)) 'Missing predecessor'
   WriteValue $association ([string]$state.PriorValue)
 } else {
   if(Test-Path -LiteralPath $association){Remove-Item -LiteralPath $association -Recurse -Force}
 }
 Remove-Item -LiteralPath $journal -Recurse -Force
 return 'RESTORED'
}
try {
 New-Item -Path $root -Force | Out-Null
 Assert ((RegisterTrial) -eq 'REGISTERED') 'Empty association registration failed'
 Assert ((ReadValue $association) -eq $ours) 'New class registration absent'
 Assert ((UnregisterTrial) -eq 'RESTORED') 'Empty association uninstall failed'
 Assert (-not(Test-Path -LiteralPath $association)) 'Empty association unexpectedly left behind'
 Write-Host 'SHELL_REGISTRY_ABSENT_RESTORED_PASS'

 ClearTrial
 WriteValue $association $known
 Assert ((RegisterTrial) -eq 'REGISTERED') 'Known provider registration failed'
 Assert ((RegisterTrial) -eq 'EXISTING') 'Idempotent upgrade path failed'
 Assert ((UnregisterTrial) -eq 'RESTORED') 'Upgrade unregister failed'
 Assert ((ReadValue $association) -eq $known) 'Prior known association lost'
 Write-Host 'SHELL_REGISTRY_KNOWN_PROVIDER_UPGRADE_RESTORED_PASS'

 ClearTrial
 WriteValue $association $other
 Assert ((RegisterTrial) -eq 'SKIP_FOREIGN') 'Third party provider overwritten'
 Assert ((ReadValue $association) -eq $other) 'Third party provider modified'
 Assert (-not(Test-Path -LiteralPath $journal)) 'Foreign provider journal created'
 Write-Host 'SHELL_REGISTRY_THIRD_PARTY_PRESERVED_PASS'

 ClearTrial
 WriteValue $association $known
 Assert ((RegisterTrial) -eq 'REGISTERED') 'User-change fixture start failed'
 WriteValue $association $other
 Assert ((UnregisterTrial) -eq 'SKIP_USER_CHANGED') 'User-changed provider clobbered'
 Assert ((ReadValue $association) -eq $other) 'User-change not preserved'
 Write-Host 'SHELL_REGISTRY_POST_INSTALL_USER_CHANGE_PRESERVED_PASS'

 ClearTrial
 WriteValue $association $known
 Assert ((RegisterTrial) -eq 'REGISTERED') 'Rollback setup failed'
 Assert ((UnregisterTrial) -eq 'RESTORED') 'Simulated MSI rollback failed'
 Assert ((ReadValue $association) -eq $known) 'Rollback did not restore predecessor'
 Write-Host 'SHELL_REGISTRY_ROLLBACK_CONTRACT_PASS'

 ClearTrial
 WriteValue $association $ours
 Assert ((RegisterTrial) -eq 'SKIP_ORPHANED_OWN') 'Unknown previous registration overwritten'
 Assert ((ReadValue $association) -eq $ours) 'Orphaned own association unexpectedly changed'
 Write-Host 'SHELL_REGISTRY_ORPHANED_ASSOCIATION_FAIL_CLOSED_PASS'
 Write-Host 'SHELL_REGISTRY_MODEL_ALL_PASS'
} finally {
 if(Test-Path -LiteralPath $root){Remove-Item -LiteralPath $root -Recurse -Force}
}
