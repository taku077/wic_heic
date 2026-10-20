# PowerShell 5.1. Read-only integrity verification for a generated session.
param([Parameter(Mandatory=$true)][string]$SessionDirectory)
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
$session=Get-Item -LiteralPath $SessionDirectory -ErrorAction Stop
if(-not $session.PSIsContainer -or $session.Parent.FullName -ine $root -or
   $session.Name -cnotmatch '^PhotosSynthetic_[0-9a-f]{32}$') {
  throw 'Session must be a generated child of this diagnostic folder'
}
function Assert-NoReparse([string]$path) {
  $node=Get-Item -LiteralPath $path -ErrorAction Stop
  while($null -ne $node) {
    if(($node.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse point forbidden' }
    if($node -is [IO.FileInfo]) { $node=$node.Directory }
    else { $node=$node.Parent }
  }
}
Assert-NoReparse $session.FullName
$manifestPath=Join-Path $session.FullName 'Session.json'
Assert-NoReparse $manifestPath
$manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if($manifest.Schema -ne 1 -or
   $manifest.FixtureBlobSha1 -cne '126b24780e8c36a0e1521df982067ab12f971470' -or
   $manifest.InitialSha256 -cnotmatch '^[0-9A-F]{64}$') { throw 'Invalid synthetic manifest' }
$fixture=Get-Item -LiteralPath (Join-Path $root '..\..\wictest\samples\fixture-irot3-exif6-noaux.heic') -ErrorAction Stop
$reference=Get-Item -LiteralPath (Join-Path $session.FullName 'reference\synthetic-reference.heic') -ErrorAction Stop
$working=Get-Item -LiteralPath (Join-Path $session.FullName 'work\synthetic-working.heic') -ErrorAction Stop
Assert-NoReparse $fixture.FullName
Assert-NoReparse $reference.FullName
Assert-NoReparse $working.FullName
$fixtureHash=(Get-FileHash -LiteralPath $fixture.FullName -Algorithm SHA256).Hash
$referenceHash=(Get-FileHash -LiteralPath $reference.FullName -Algorithm SHA256).Hash
$workingHash=(Get-FileHash -LiteralPath $working.FullName -Algorithm SHA256).Hash
if($fixtureHash -cne $manifest.InitialSha256 -or $referenceHash -cne $manifest.InitialSha256) {
  throw 'SYNTHETIC_REFERENCE_FAILURE: source or reference changed'
}
Write-Output 'SYNTHETIC_REFERENCE_PASS: fixture and preserved reference unchanged.'
if($workingHash -ceq $manifest.InitialSha256) {
  Write-Output 'SYNTHETIC_WORKING_STATUS=UNCHANGED'
} else {
  Write-Output 'SYNTHETIC_WORKING_STATUS=MODIFIED'
}
Write-Host 'This does not prove correct rotation or metadata preservation.'
