# Disposable GitHub Windows CI runner ONLY. Never run on users' PCs.
# Test controlled, temporary WIC arbitration settings on PUBLIC synthetic HEIC.
param(
    [Parameter(Mandatory=$true)][string]$TestExe,
    [Parameter(Mandatory=$true)][string]$CodecDll
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows' -or
    -not $env:RUNNER_TEMP) {
    throw 'REFUSED: temporary WIC registry experiments require a GitHub Windows CI runner'
}
if ([IntPtr]::Size -ne 8) { throw 'REFUSED: x64 runner required' }
if (-not (Test-Path -LiteralPath $TestExe -PathType Leaf) -or
    -not (Test-Path -LiteralPath $CodecDll -PathType Leaf)) {
    throw 'CI test exe or codec DLL missing'
}
$cat='{7ED96837-96F0-4812-B211-F13C24117ED3}'
$ours='{6215667c-74ca-4076-a769-3f834c3f25f5}'
$oursReg="HKCR\CLSID\$ours"
$oursPs="Registry::HKEY_CLASSES_ROOT\CLSID\$ours"
$instanceRoot="HKCR\CLSID\$cat\Instance"
$backupFolder=Join-Path $env:RUNNER_TEMP ('WIC_AB_'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backupFolder -ErrorAction Stop | Out-Null
$oursBackup=Join-Path $backupFolder 'original-custom.reg'
$otherBackup=Join-Path $backupFolder 'original-other-instance.reg'
$registered=$false
$pushed=$false
$competitorDetached=$false
$otherReg=$null
$baselinePriority=0
$restored=$false
function CheckExit([string]$label) {
    if ($LASTEXITCODE -ne 0) { throw "$label failed with exit code $LASTEXITCODE" }
}
function Bench([string]$label) {
    Write-Host "WIC_ARBITRATION_EXPERIMENT_PHASE=$label"
    & $TestExe --arbitration-bench
    CheckExit "Benchmark phase $label"
}
try {
    & "$env:WINDIR\System32\regsvr32.exe" /s $CodecDll
    CheckExit 'Register disposable-runner codec'
    $registered=$true
    Push-Location -LiteralPath 'wictest'
    $pushed=$true
    $audit=@(& $TestExe --arbitration-audit)
    CheckExit 'Enumerate competing decoders'
    $audit | ForEach-Object { Write-Host $_ }
    $otherMatches=@($audit | Where-Object {
        $_ -match '^WIC_ARBITRATION_OTHER_CLSID=\{[a-fA-F0-9-]{36}\}$'
    })
    if ($otherMatches.Count -ne 1) {
        throw "Expected exactly one competing decoder; got $($otherMatches.Count)"
    }
    $other=[regex]::Match($otherMatches[0],'\{[a-fA-F0-9-]{36}\}').Value
    if ($other -ieq $ours) { throw 'Refuse own codec as competing entry' }
    $otherReg="$instanceRoot\$other"
    & reg.exe export $oursReg $oursBackup /y | Out-Null
    CheckExit 'Backup custom decoder registry key'
    & reg.exe export $otherReg $otherBackup /y | Out-Null
    CheckExit 'Backup other decoder category instance'
    $baselineOther=@(& reg.exe query $otherReg /s)
    CheckExit 'Inspect original other decoder'
    $baselinePriority=[int](Get-ItemProperty -LiteralPath $oursPs -Name ArbitrationPriority).ArbitrationPriority
    if ($baselinePriority -ne 0) {
        throw "Custom decoder priority no longer baseline 0: $baselinePriority"
    }
    Bench 'BASELINE_BOTH_DECODERS'
    try {
        # WIC numeric 10 means LESS preferred than 0.
        & reg.exe add $oursReg /v ArbitrationPriority /t REG_DWORD /d 10 /f | Out-Null
        CheckExit 'Set custom decoder priority 10'
        $priority=[int](Get-ItemProperty -LiteralPath $oursPs -Name ArbitrationPriority).ArbitrationPriority
        if ($priority -ne 10) { throw 'Custom priority not updated' }
        Bench 'CUSTOM_PRIORITY_10_COMPETITOR_PREFERRED'
    } finally {
        & reg.exe import $oursBackup | Out-Null
        CheckExit 'Restore custom decoder priority and registry'
        $priority=[int](Get-ItemProperty -LiteralPath $oursPs -Name ArbitrationPriority).ArbitrationPriority
        if ($priority -ne $baselinePriority) { throw 'Priority restore mismatch' }
    }
    try {
        # Detach ONLY the WIC category instance, never foreign COM CLSID.
        & reg.exe delete $otherReg /f | Out-Null
        CheckExit 'Temporarily detach competing decoder from WIC category'
        $competitorDetached=$true
        & reg.exe query $otherReg *> $null
        if ($LASTEXITCODE -eq 0) { throw 'Competing category instance still present' }
        Bench 'ONLY_CUSTOM_MATCHING_DECODER'
    } finally {
        if ($competitorDetached) {
            & reg.exe import $otherBackup | Out-Null
            CheckExit 'Restore competing WIC category instance'
            $competitorDetached=$false
            $restoredOther=@(& reg.exe query $otherReg /s)
            CheckExit 'Verify restored competitor registration'
            if ([string]::Join(';',$baselineOther) -cne [string]::Join(';',$restoredOther)) {
                throw 'Competing decoder registration differs from backup'
            }
        }
    }
    Bench 'RESTORED_BOTH_DECODERS'
    $restored=$true
    Write-Host 'WIC_ARBITRATION_DISPOSABLE_AB_PASS scenarios=4 priority_restored=1 foreign_instance_restored=1'
} finally {
    if ($competitorDetached -and (Test-Path -LiteralPath $otherBackup)) {
        & reg.exe import $otherBackup | Out-Null
        CheckExit 'Emergency foreign WIC instance restore'
    }
    if (Test-Path -LiteralPath $oursBackup) {
        & reg.exe import $oursBackup | Out-Null
        CheckExit 'Emergency custom decoder registry restore'
    }
    if ($pushed) { Pop-Location }
    if ($registered) {
        & "$env:WINDIR\System32\regsvr32.exe" /u /s $CodecDll
        CheckExit 'Unregister disposable CI codec'
    }
}
if (-not $restored) { throw 'Registry A/B did not reach verified restoration' }
