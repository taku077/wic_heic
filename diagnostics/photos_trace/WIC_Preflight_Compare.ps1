# Read-only comparison of two local WIC_Preflight JSON snapshots.
param(
 [Parameter(Mandatory=$true)][string]$Before,
 [Parameter(Mandatory=$true)][string]$After
)
$ErrorActionPreference='Stop'
$a=Get-Content -LiteralPath $Before -Raw -Encoding UTF8 | ConvertFrom-Json
$b=Get-Content -LiteralPath $After -Raw -Encoding UTF8 | ConvertFrom-Json
if($a.Schema -ne 1 -or $b.Schema -ne 1) { throw 'Unknown WIC inventory schema.' }
function Signature($record) {
  $reg=@($record.Registrations | Sort-Object Hive,View,Codec |
    ForEach-Object {
      '{0}|{1}|{2}|{3}|{4}' -f $_.Hive,$_.View,$_.Codec,$_.InprocServer32,$_.SHA256
    })
  $msi=@($record.WicHeicMsi | Sort-Object ProductKey |
    ForEach-Object { '{0}|{1}|{2}' -f $_.Name,$_.Version,$_.ProductKey })
  return (($reg -join "`n") + "`nMSI`n" + ($msi -join "`n"))
}
if((Signature $a) -cne (Signature $b)) {
  Write-Host 'WIC_RESTORE_COMPARISON=DIFFERENT' -ForegroundColor Red
  throw 'Installed WIC registration/DLL/MSI state differs from the saved baseline. Rollback is NOT verified.'
}
Write-Host 'WIC_RESTORE_COMPARISON=PASS: registered codec paths, hashes and WicHeic MSI entries match.'
