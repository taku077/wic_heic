# Cross-validate libheif ordered irot/imir/clap and FFprobe exact display matrices.
# This is a strict allowlist: no filename-based orientation lookup.
# No original HEIC is opened for writing or changed.
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'CI only'}
. ./diagnostics/thumbnail_warmup/HeicOrientationPolicy.ps1
$native=(Resolve-Path 'wictest/isolated_shell/(Build)/x64/Release/HeifPrimaryTransforms.exe').Path
$ffprobe=(Get-Command ffprobe -ErrorAction Stop).Source
$names=@('03.heic','04.heic','05.heic','06.heic','07.heic',
  'fixture-irot3-exif6-noaux.heic','fixture-irot3-exif6.heic')
$expected=@{}
$lines=@(& $native)
if($LASTEXITCODE -ne 0){throw 'Native HEIF item association parser failed'}
foreach($line in $lines){
  if($line -match '^HEIC_PRIMARY_ROUTE file=(\S+) effect=(\w+) allowed=([01])$'){
    $filename=$Matches[1]
    if($expected.ContainsKey($filename)){throw "Repeated HEIC primary image route: $filename"}
    $expected[$filename]=[pscustomobject]@{effect=$Matches[2];allowed=($Matches[3] -eq '1')}
  }
}
if($expected.Count -ne $names.Count){throw 'Incomplete independent HEIC metadata census'}
$accepted=0;$denied=0
foreach($filename in $names){
  if(-not $expected.ContainsKey($filename)){throw "Missing libheif transformation census $filename"}
  $record=$expected[$filename]
  $path=(Resolve-Path (Join-Path 'wictest/samples' $filename)).Path
  $before=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
  $json=(& $ffprobe -v error -select_streams v:0 -show_entries 'stream=codec_name,width,height:stream_tags=rotate:stream_side_data=side_data_type,rotation,displaymatrix' -of json $path) -join [char]10
  if($LASTEXITCODE -ne 0){throw "FFprobe failed on pinned HEIC $filename"}
  $stream=@((ConvertFrom-Json -InputObject $json).streams)[0]
  if(-not $stream){throw "No primary HEVC stream for $filename"}
  $ffmpegEffect='reject'
  try{$ffmpegEffect=Get-HeicDisplayTransform -Stream $stream}
  catch{Write-Host "HEIC_DUAL_ORIENTATION_FFPROBE_REJECT file=$filename"}
  if(-not $record.allowed){
    $denied++
    if($record.effect -ne 'reject'){throw "Native unsupported transform wrongly allowed for $filename"}
    Write-Host "HEIC_DUAL_ORIENTATION_FALLBACK file=$filename reason=unsupported_libheif_property"
  } elseif($record.effect -ne $ffmpegEffect){
    throw "HEIF/FFprobe disagree; GPU path MUST be rejected: $filename native=$($record.effect) ffprobe=$ffmpegEffect"
  } else {
    $accepted++
    Write-Host "HEIC_DUAL_ORIENTATION_MATCH file=$filename rotation=$($record.effect) pixel_contract=check_with_existing_CI"
  }
  if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $before){throw "HEIC source changed: $filename"}
}
if(($accepted+$denied) -ne 7){throw 'Unbalanced HEIC metadata routes'}
# All seven pinned items are canonical simple rotations or identity. New corpus
# entries containing clap/imir must fall back, not silently get an identity.
if($accepted -ne 7){throw 'Previously simple HEIC corpus unexpectedly changed'}
Write-Host "HEIC_DUAL_ORIENTATION_SUMMARY accepted=$accepted fallback=$denied source_modified=0"
Write-Host 'HEIC_DUAL_ORIENTATION_NO_REGISTRY_NO_MSI_READONLY_PASS'
