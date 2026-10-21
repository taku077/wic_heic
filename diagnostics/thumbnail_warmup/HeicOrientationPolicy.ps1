# Shared, fail-closed research policy for FFprobe HEIC Display Matrix.
# Never infer an orientation from a filename or EXIF alone.
# This policy has no I/O or OS effects.
function Get-HeicDisplayTransform {
  param([Parameter(Mandatory=$true)]$Stream)
  if($Stream.codec_name -ne 'hevc'){throw 'Unexpected codec for HEIC primary image'}
  $matrixSides=@($Stream.side_data_list | Where-Object {
    $null -ne $_ -and $_.side_data_type -eq 'Display Matrix'
  })
  if($matrixSides.Count -gt 1){throw 'More than one display matrix'}
  $tag=0
  if($null -ne $Stream.tags -and $Stream.tags.PSObject.Properties['rotate']){
    $tag=[double]$Stream.tags.rotate
    if([math]::Abs($tag) -gt 0.01){throw 'Unexpected legacy rotate tag'}
  }
  if($matrixSides.Count -eq 0){return 'identity'}
  $item=$matrixSides[0]
  if($null -eq $item.PSObject.Properties['rotation'] -or
     $null -eq $item.PSObject.Properties['displaymatrix']){
    throw 'Display matrix has no numeric rotation or matrix'
  }
  $angle=[double]$item.rotation
  $kind=$null
  if([math]::Abs($angle) -lt 0.01){$kind='identity'}
  elseif([math]::Abs($angle+90) -lt 0.01){$kind='clockwise90'}
  elseif([math]::Abs($angle-90) -lt 0.01){$kind='counterclockwise90'}
  elseif([math]::Abs([math]::Abs($angle)-180) -lt 0.01){$kind='rotate180'}
  else {throw 'Unrecognized rotation angle'}
  # Do not trust rotation number by itself: the 3x3 matrix may also mirror,
  # shear, translate or scale the image. Only exact 16.16 canonical rotations
  # with a 2.30 homogeneous final component are allowed.
  $patterns=@{
    identity=@(65536,0,0,0,65536,0,0,0,1073741824)
    clockwise90=@(0,65536,0,-65536,0,0,0,0,1073741824)
    counterclockwise90=@(0,-65536,0,65536,0,0,0,0,1073741824)
    rotate180=@(-65536,0,0,0,-65536,0,0,0,1073741824)
  }
  $matrix=[string]$item.displaymatrix
  $lines=@($matrix -split '\r?\n' | Where-Object {$_.Trim().Length -gt 0})
  if($lines.Count -ne 3){throw 'Unexpected number of display matrix rows'}
  $got=New-Object 'System.Collections.Generic.List[long]'
  for($row=0;$row -lt 3;$row++){
    $match=[regex]::Match($lines[$row], '^\s*0000000([012]):\s*(-?\d+)\s+(-?\d+)\s+(-?\d+)\s*$')
    if(-not $match.Success -or [int]$match.Groups[1].Value -ne $row){
      throw 'Unexpected display matrix layout'
    }
    for($col=2;$col -le 4;$col++){$got.Add([long]::Parse($match.Groups[$col].Value))}
  }
  $expected=$patterns[$kind]
  if($got.Count -ne 9){throw 'Display matrix incomplete'}
  for($i=0;$i -lt 9;$i++){
    if($got[$i] -ne $expected[$i]){
      throw 'Display matrix contains a noncanonical transform (mirror/shear/scale/translate)'
    }
  }
  return $kind
}
