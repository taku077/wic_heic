# FFprobe metadata-only audit of pinned repo HEIC fixtures. No photos modified.
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'CI ONLY'}
$probe=Get-Command ffprobe -ErrorAction SilentlyContinue
if(-not $probe){throw 'FFprobe unavailable'}
$dir=(Resolve-Path 'wictest/samples').Path
foreach($name in @('03.heic','04.heic','05.heic','06.heic','07.heic','fixture-irot3-exif6-noaux.heic','fixture-irot3-exif6.heic')){
  $path=Join-Path $dir $name
  $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
  $raw=(& $probe.Source -v error -select_streams v:0 -show_entries 'stream=index,codec_name,width,height:stream_tags=rotate:stream_side_data=side_data_type,rotation,displaymatrix' -of json $path) -join [char]10
  if($LASTEXITCODE -ne 0){throw "FFprobe failed: $name"}
  $json=ConvertFrom-Json -InputObject $raw
  if(-not $json.streams -or @($json.streams).Count -lt 1){throw "No stream for $name"}
  $stream=@($json.streams)[0]
  $sides=@($stream.side_data_list)
  $angles=@($sides|Where-Object {$null -ne $_ -and $_.PSObject.Properties['rotation']}|ForEach-Object { [string]$_.rotation })
  $tag=if($stream.tags -and $stream.tags.PSObject.Properties['rotate']){$stream.tags.rotate}else{'none'}
  Write-Host "HEIC_ROTATION_METADATA file=$name codec=$($stream.codec_name) size=$($stream.width)x$($stream.height) rotateTag=$tag sideRotations=$($angles -join ',') sideTypes=$((@($sides|ForEach-Object {$_.side_data_type})) -join ',')"
  if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $hash){throw "Modified HEIC $name"}
}
Write-Host 'HEIC_ROTATION_METADATA_READONLY_PASS'
