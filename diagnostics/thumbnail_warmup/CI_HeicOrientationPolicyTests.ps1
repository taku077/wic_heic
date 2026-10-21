# Negative/positive tests use only synthetic FFprobe objects; no files or registry.
$ErrorActionPreference='Stop'
. ./diagnostics/thumbnail_warmup/HeicOrientationPolicy.ps1
function Matrix([long[]]$values) {
  $lines=@()
  for($row=0;$row -lt 3;$row++){
    $i=$row*3
    $lines+=('{0:D8}: {1} {2} {3}' -f $row,$values[$i],$values[$i+1],$values[$i+2])
  }
  return $lines -join [char]10
}
function Stream([double]$rotation,[long[]]$values) {
  return [pscustomobject]@{
    codec_name='hevc'
    side_data_list=@([pscustomobject]@{side_data_type='Display Matrix';rotation=$rotation;displaymatrix=(Matrix $values)})
  }
}
function ExpectKind($stream,[string]$kind) {
  $actual=Get-HeicDisplayTransform -Stream $stream
  if($actual -ne $kind){throw "Expected $kind, got $actual"}
  Write-Host "HEIC_ORIENTATION_POLICY_ACCEPT kind=$kind"
}
function ExpectReject($stream,[string]$reason) {
  $rejected=$false
  try { $null=Get-HeicDisplayTransform -Stream $stream }
  catch { $rejected=$true;Write-Host "HEIC_ORIENTATION_POLICY_REJECT scenario=$reason" }
  if(-not $rejected){throw "Unsafe transform erroneously accepted: $reason"}
}
$identity=@(65536,0,0,0,65536,0,0,0,1073741824)
$clockwise=@(0,65536,0,-65536,0,0,0,0,1073741824)
$ccw=@(0,-65536,0,65536,0,0,0,0,1073741824)
$flip180=@(-65536,0,0,0,-65536,0,0,0,1073741824)
ExpectKind ([pscustomobject]@{codec_name='hevc';side_data_list=@()}) 'identity'
ExpectKind (Stream 0 $identity) 'identity'
ExpectKind (Stream -90 $clockwise) 'clockwise90'
ExpectKind (Stream 90 $ccw) 'counterclockwise90'
ExpectKind (Stream 180 $flip180) 'rotate180'
ExpectReject (Stream -90 @(0,-65536,0,-65536,0,0,0,0,1073741824)) 'mirroring'
ExpectReject (Stream -90 @(0,65536,1,-65536,0,0,0,0,1073741824)) 'translation'
ExpectReject (Stream -90 @(0,65535,0,-65536,0,0,0,0,1073741824)) 'scaled_or_noncanonical'
ExpectReject (Stream -45 $clockwise) 'unrecognized_angle'
ExpectReject ([pscustomobject]@{codec_name='hevc';side_data_list=@(
  [pscustomobject]@{side_data_type='Display Matrix';rotation=-90;displaymatrix=(Matrix $clockwise)},
  [pscustomobject]@{side_data_type='Display Matrix';rotation=-90;displaymatrix=(Matrix $clockwise)}
)}) 'duplicate_display_matrices'
ExpectReject ([pscustomobject]@{codec_name='hevc';side_data_list=@();tags=[pscustomobject]@{rotate='90'}}) 'conflicting_legacy_rotation_tag'
Write-Host 'HEIC_ORIENTATION_POLICY_GUARDED_TESTS_PASSED'
