# CI-only: HEIC stream metadata directs a safe rotation of extracted HEVC frames.
# No photo modification and no unverified silent fast-path fallback.
$ErrorActionPreference='Stop'
. ./diagnostics/thumbnail_warmup/HeicOrientationPolicy.ps1
if($env:GITHUB_ACTIONS -ne 'true'){throw 'Only allowed on disposable CI'}
$ffmpeg=Get-Command ffmpeg -ErrorAction Stop
$ffprobe=Get-Command ffprobe -ErrorAction Stop
$files=@('03.heic','04.heic','05.heic','06.heic','07.heic',
         'fixture-irot3-exif6-noaux.heic','fixture-irot3-exif6.heic')
$root=(Resolve-Path 'wictest/samples').Path
$out=Join-Path $env:RUNNER_TEMP 'heic_metadata_routed_parity'
if(Test-Path $out){throw 'Refusing to overwrite existing results'}
New-Item -ItemType Directory -Path $out|Out-Null
$exact=0; $eligible=0; $rejected=0; $failed=0
foreach($file in $files){
  $source=Join-Path $root $file
  $originalHash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
  $json=(& $ffprobe.Source -v error -select_streams v:0 -show_entries 'stream=codec_name,width,height:stream_tags=rotate:stream_side_data=side_data_type,rotation,displaymatrix' -of json $source) -join [char]10
  if($LASTEXITCODE -ne 0){throw "FFprobe failed $file"}
  $stream=@((ConvertFrom-Json $json).streams)[0]
  if($null -eq $stream -or $stream.codec_name -ne 'hevc'){throw "Not an HEVC image $file"}
  try {
    $effect=Get-HeicDisplayTransform -Stream $stream
  } catch {
    $rejected++
    Write-Host "HEIC_METADATA_ROUTE_REJECT file=$file reason=$($_.Exception.Message -replace '\s+','_')"
    continue
  }
  $rotation=if($effect -eq 'clockwise90'){-90}
            elseif($effect -eq 'counterclockwise90'){90}
            elseif($effect -eq 'rotate180'){180}
            else{0}
  $base=[IO.Path]::GetFileNameWithoutExtension($file)
  $annex=Join-Path $out ($base+'.h265')
  $fromHeic=Join-Path $out ($base+'.heic.bgra')
  $fromHevc=Join-Path $out ($base+'.hevc.bgra')
  foreach($target in @($annex,$fromHeic,$fromHevc)){
    if(Test-Path $target){throw 'Output overwrite prevented'}
  }
  & $ffmpeg.Source -hide_banner -nostdin -loglevel error -i $source -map 0:v:0 -c:v copy -bsf:v hevc_mp4toannexb -f hevc $annex
  if($LASTEXITCODE -ne 0 -or -not(Test-Path $annex)){$failed++;Write-Host "HEIC_METADATA_ROUTE_FAILED file=$file stage=extract";continue}
  & $ffmpeg.Source -hide_banner -nostdin -loglevel error -i $source -frames:v 1 -pix_fmt bgra -f rawvideo $fromHeic
  if($LASTEXITCODE -ne 0 -or -not(Test-Path $fromHeic)){$failed++;Write-Host "HEIC_METADATA_ROUTE_FAILED file=$file stage=reference";continue}
  $argsList=@('-hide_banner','-nostdin','-loglevel','error','-f','hevc','-i',$annex,'-frames:v','1')
  switch($effect){
    'clockwise90'        { $argsList+=@('-vf','transpose=clock') }
    'counterclockwise90' { $argsList+=@('-vf','transpose=cclock') }
    'rotate180'          { $argsList+=@('-vf','hflip,vflip') }
  }
  $argsList+=@('-pix_fmt','bgra','-f','rawvideo',$fromHevc)
  & $ffmpeg.Source @argsList
  if($LASTEXITCODE -ne 0 -or -not(Test-Path $fromHevc)){$failed++;Write-Host "HEIC_METADATA_ROUTE_FAILED file=$file stage=render";continue}
  $one=(Get-FileHash -LiteralPath $fromHeic -Algorithm SHA256).Hash
  $two=(Get-FileHash -LiteralPath $fromHevc -Algorithm SHA256).Hash
  $isSame=($one -eq $two) -and ((Get-Item $fromHeic).Length -eq (Get-Item $fromHevc).Length)
  $eligible++
  if($isSame){$exact++}else{$failed++}
  Write-Host "HEIC_METADATA_ROUTE_RESULT file=$file metadata_rotation=$rotation effect=$effect pixel_exact=$isSame output_bytes=$((Get-Item $fromHevc).Length)"
  if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $originalHash){throw "HEIC original changed: $file"}
}
Write-Host "HEIC_METADATA_ROUTE_SUMMARY total=$($files.Count) accepted=$eligible pixel_exact=$exact rejected=$rejected failed=$failed"
if($exact -ne $files.Count -or $rejected -ne 0 -or $failed -ne 0){
  throw 'Metadata-driven routing did not preserve all pinned reference pixels'
}
Write-Host 'HEIC_METADATA_ROUTE_ALL_PINNED_FILES_EXACT_READONLY_PASS'
