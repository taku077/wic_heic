#requires -version 5.1
# Restore the exact 2026-10-20 global WicHeic 1.1.18 DLL, SHA256-pinned.
# Intended only for the previously diagnosed Windows 10 + independent preview 0.4.0.
# Never opens, overwrites, scans or transforms HEIC user photographs.
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2.0
$want='9E9D2F17038C03A5BF8840A8BD43BC4FDA19C91E947C4CD5904A8209051F93D4'
$root=$PSScriptRoot
$msi=Join-Path $root 'WicHeic-ORIGINAL-20261020-1.1.18.msi'
$manifest=Join-Path $root 'ORIGINAL_MSI_SHA256.txt'
$inventory=Join-Path $root 'WIC_Preflight_ReadOnly.ps1'
$previewName='WicHeic Fast Thumbnail Preview (Experimental)'
$globalName='WicHeic Repair Test'
$provider='{D973196C-165C-4FCE-8838-17C37037D384}'
$shell='{E357FCCD-A995-4576-B01F-234630154E96}'
function Fail([string]$message){
    throw "RESTORE_STOPPED: $message"
}
function Snapshot{
    $report=& $inventory | Select-Object -Last 1
    if(!$report -or !(Test-Path -LiteralPath $report -PathType Leaf)){Fail 'Read-only WIC registry inventory unavailable'}
    $x=Get-Content -LiteralPath $report -Encoding UTF8 -Raw | ConvertFrom-Json
    if($x.Schema -ne 1 -or @($x.Registrations).Count -ne 12){Fail 'WIC registry inventory incomplete'}
    return $x
}
function WicGlobal64($s){
    return @($s.Registrations | Where-Object {
        $_.Hive -eq 'LocalMachine' -and $_.View -eq 'Registry64' -and $_.Codec -in @('Decoder','Encoder')
    })
}
function PreviewFingerprint {
    $reg=[Microsoft.Win32.Registry]::LocalMachine.OpenSubKey("SOFTWARE\Classes\CLSID\$provider\InprocServer32")
    if(!$reg){Fail 'v0.4 preview COM registration missing'}
    try{$server=[string]$reg.GetValue('')}finally{$reg.Close()}
    if(!$server -or !(Test-Path -LiteralPath $server -PathType Leaf)){Fail 'v0.4 preview server DLL missing'}
    $private=Join-Path (Split-Path -Parent $server) 'WicHeicPrivateCodec.dll'
    if(!(Test-Path -LiteralPath $private -PathType Leaf)){Fail 'v0.4 private decoder DLL missing'}
    $associations=@()
    foreach($relative in @("Software\Classes\.heic\ShellEx\$shell",
                           "Software\Classes\SystemFileAssociations\.heic\ShellEx\$shell")){
        $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($relative)
        if(!$key){Fail 'v0.4 thumbnail handler association missing'}
        try{$actual=[string]$key.GetValue('')}finally{$key.Close()}
        if($actual -cne $provider){Fail 'v0.4 thumbnail association modified'}
        $associations+=$actual
    }
    return (($server+'|'+(Get-FileHash -LiteralPath $server -Algorithm SHA256).Hash+
        '|'+(Get-FileHash -LiteralPath $private -Algorithm SHA256).Hash+
        '|'+($associations -join '|')))
}
function CheckPreviewMsi($snap){
    $entries=@($snap.WicHeicMsi | Where-Object {
        $_.Name -eq $previewName -and $_.Version -eq '0.4.0'
    })
    if($entries.Count -ne 1){Fail 'Independent thumbnail Preview v0.4.0 MSI not found exactly once'}
}
function MsiRun([string]$mode,[string]$target,[string]$name){
    $log=Join-Path $root ('Restore_WIC_'+$name+'_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'.log')
    $argsText='/'+$mode+' "'+$target+'" /passive /norestart /l*v "'+$log+'"'
    $p=Start-Process -FilePath 'msiexec.exe' -ArgumentList $argsText -Wait -PassThru
    if($p.ExitCode -ne 0){Fail ("MSI operation $name failed with code "+$p.ExitCode+". Log: "+$log)}
}
Write-Host '=== Restore original 2026-10-20 WIC HEIC 1.1.18 ==='
if(![Environment]::Is64BitOperatingSystem -or ![Environment]::Is64BitProcess){Fail 'Windows x64 PowerShell required'}
if(!([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)){Fail 'Please launch the included CMD file (it requests UAC once)'}
if(!(Test-Path -LiteralPath $msi -PathType Leaf) -or
   !(Test-Path -LiteralPath $manifest -PathType Leaf) -or
   !(Test-Path -LiteralPath $inventory -PathType Leaf)){Fail 'Restoration package incomplete'}
foreach($file in @($msi,$manifest,$inventory)){
    if(((Get-Item -LiteralPath $file).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
        Fail 'Linked restoration input refused'
    }
}
$msiExpected=(Get-Content -LiteralPath $manifest -Encoding ASCII -Raw).Trim()
if($msiExpected -notmatch '^[A-Fa-f0-9]{64}$' -or
   (Get-FileHash -LiteralPath $msi -Algorithm SHA256).Hash -ne $msiExpected){
    Fail 'Original MSI SHA256 mismatch. Nothing has been installed or removed.'
}
# Read-only MSI database verification; this NEVER runs an installer.
$installer=New-Object -ComObject WindowsInstaller.Installer
$db=$installer.OpenDatabase($msi,0)
function MsiProperty([string]$name){
    $view=$db.OpenView("SELECT Value FROM Property WHERE Property = '$name'")
    try{
        $view.Execute()
        $rec=$view.Fetch()
        if(!$rec){return ''}
        return [string]$rec.StringData(1)
    }finally{$view.Close()}
}
if((MsiProperty 'ProductName') -ne $globalName -or
   (MsiProperty 'ProductVersion') -ne '1.1.18'){Fail 'Original WIC MSI identity unexpected'}
$oldProductCode=MsiProperty 'ProductCode'
if($oldProductCode -notmatch '^\{[0-9A-Fa-f-]{36}\}$'){Fail 'Original WIC ProductCode invalid'}
$db=$null
$installer=$null
$before=Snapshot
if($before.Windows.CurrentBuild -ne '19045'){Fail 'Only the documented Windows 10 22H2 machine is supported'}
CheckPreviewMsi $before
$previewBefore=PreviewFingerprint
$wics=WicGlobal64 $before
if($wics.Count -ne 2){Fail 'Global WIC registry inventory missing'}
$globalMsis=@($before.WicHeicMsi | Where-Object {$_.Name -eq $globalName})
$unrecognized=@($before.WicHeicMsi | Where-Object {$_.Name -notin @($previewName,$globalName)})
if($unrecognized.Count -ne 0){Fail 'Unexpected additional WicHeic package is present'}
if($globalMsis.Count -eq 0){
    if(@($before.Registrations | Where-Object {$_.InprocServer32}).Count -ne 0){
        Fail 'Global WIC registrations present without expected MSI; refuse destructive action'
    }
    Write-Host 'Previous test WIC is already absent. Installing the original WIC only.'
}elseif($globalMsis.Count -eq 1){
    $existing=$globalMsis[0]
    if($existing.Version -ne '1.1.18' -or $existing.ProductKey -notmatch '^\{[0-9A-Fa-f-]{36}\}$'){
        Fail 'Installed global WIC product identity unexpected'
    }
    if(@($before.WicHeicMsi).Count -ne 2){Fail 'Unexpected WIC installer count'}
    if(!$wics[0].InprocServer32 -or !$wics[1].InprocServer32 -or
       $wics[0].InprocServer32 -cne $wics[1].InprocServer32 -or
       $wics[0].SHA256 -cne $wics[1].SHA256){Fail 'Encoder/decoder DLL registry disagreement'}
    $dllpath=[string]$wics[0].InprocServer32
    $expectedDefault=Join-Path $env:ProgramFiles 'WicHeic Repair Test\wic_heic.dll'
    if([IO.Path]::GetFullPath($dllpath) -ine [IO.Path]::GetFullPath($expectedDefault)){
        Fail 'Unexpected global WIC installation location; refusing to uninstall it'
    }
    if($wics[0].SHA256 -ceq $want){
        if((PreviewFingerprint) -cne $previewBefore){Fail 'Unexpected thumbnail modification'}
        Write-Host 'EXACT_20261020_WIC_ALREADY_RESTORED; v0.4.0 untouched.'
        return
    }
    Write-Host 'Removing only the current experimental global WIC. v0.4.0 is retained.'
    MsiRun 'x' ([string]$existing.ProductKey) 'UninstallTrial'
    $cleared=Snapshot
    CheckPreviewMsi $cleared
    if(@($cleared.WicHeicMsi | Where-Object {$_.Name -eq $globalName}).Count -ne 0 -or
       @($cleared.Registrations | Where-Object {$_.InprocServer32}).Count -ne 0){
        Fail 'Old global WIC registration remains after uninstall; safe restore stopped'
    }
    if((PreviewFingerprint) -cne $previewBefore){Fail 'Preview v0.4.0 changed during trial removal; safe restore stopped'}
}else{
    Fail 'Several global WIC MSI registrations exist'
}
Write-Host 'Installing exact 2026-10-20 original WIC build...'
MsiRun 'i' $msi 'InstallOriginal'
$after=Snapshot
CheckPreviewMsi $after
$final=WicGlobal64 $after
if($final.Count -ne 2 -or
   @($final | Where-Object {$_.SHA256 -ceq $want}).Count -ne 2){
    Fail 'Original WIC DLL SHA256 does not match the working Oct20 decoder. Photos test prohibited.'
}
if((PreviewFingerprint) -cne $previewBefore){Fail 'v0.4 changed during WIC restore'}
if(@($after.WicHeicMsi | Where-Object {
    $_.Name -eq $globalName -and $_.Version -eq '1.1.18'
}).Count -ne 1){Fail 'Restored WIC MSI registry absent'}
Write-Host 'RESTORE_EXACT_20261020_WIC_AND_V040_PRESERVED_PASS'
Write-Host 'Windows 10 Photos GUI is not provable by this installer; use only copied synthetic HEIC for any edit test.'
Write-Host 'Do not rotate or overwrite real photos until edit/save metadata safety has been verified.'
