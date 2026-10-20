# Windows 10, PowerShell 5.1. Only pinned public synthetic HEIC.
$ErrorActionPreference='Stop'
$expectedBlob='126b24780e8c36a0e1521df982067ab12f971470'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
if($root -notmatch '^[A-Za-z]:\\') { throw 'Local drive required' }
function Assert-NoReparse([string]$path) {
  $node=Get-Item -LiteralPath $path -ErrorAction Stop
  while($null -ne $node) {
    if(($node.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse point forbidden' }
    $node=$node.Parent
  }
}
Assert-NoReparse $root
$fixture=Get-Item -LiteralPath (Join-Path $root '..\..\wictest\samples\fixture-irot3-exif6-noaux.heic') -ErrorAction Stop
if($fixture.PSIsContainer) { throw 'Fixture must be a regular file' }
Assert-NoReparse $fixture.FullName
$bytes=[IO.File]::ReadAllBytes($fixture.FullName)
# Validate the exact checked-in Git blob, not merely a filename/extension.
$prefix=[Text.Encoding]::ASCII.GetBytes(('blob '+$bytes.Length+[char]0))
$blobBytes=New-Object byte[] ($prefix.Length+$bytes.Length)
[Array]::Copy($prefix,0,$blobBytes,0,$prefix.Length)
[Array]::Copy($bytes,0,$blobBytes,$prefix.Length,$bytes.Length)
$sha1=[Security.Cryptography.SHA1]::Create()
try {$blob=[BitConverter]::ToString($sha1.ComputeHash($blobBytes)).Replace('-','').ToLowerInvariant()}
finally {$sha1.Dispose()}
if($blob -cne $expectedBlob) { throw 'Fixture is not the approved public synthetic HEIC' }
$sha256=[Security.Cryptography.SHA256]::Create()
try {$initial=[BitConverter]::ToString($sha256.ComputeHash($bytes)).Replace('-','')}
finally {$sha256.Dispose()}
$session=Join-Path $root ('PhotosSynthetic_'+[guid]::NewGuid().ToString('N'))
if(Test-Path -LiteralPath $session) { throw 'Unexpected existing session' }
# Once created, contents are exclusively written to new paths.
[IO.Directory]::CreateDirectory($session) | Out-Null
$refDir=Join-Path $session 'reference'
$workDir=Join-Path $session 'work'
[IO.Directory]::CreateDirectory($refDir) | Out-Null
[IO.Directory]::CreateDirectory($workDir) | Out-Null
function WriteNew([string]$target,[byte[]]$data) {
  $stream=[IO.File]::Open($target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try {$stream.Write($data,0,$data.Length)} finally {$stream.Dispose()}
}
$reference=Join-Path $refDir 'synthetic-reference.heic'
$working=Join-Path $workDir 'synthetic-working.heic'
WriteNew $reference $bytes
WriteNew $working $bytes
[IO.File]::SetAttributes($reference,[IO.FileAttributes]::ReadOnly)
$manifest=[ordered]@{
 Schema=1; FixtureBlobSha1=$expectedBlob; InitialSha256=$initial
 CreatedUtc=[DateTime]::UtcNow.ToString('o')
}
$json=$manifest|ConvertTo-Json -Depth 3
$utf8=New-Object Text.UTF8Encoding($false)
WriteNew (Join-Path $session 'Session.json') ($utf8.GetBytes($json))
Write-Host 'SYNTHETIC_PREPARE_PASS: pinned synthetic fixture verified.'
Write-Host "Only open this disposable work file in Photos: $working"
Write-Host "Reference (do not edit): $reference"
Write-Output $session
