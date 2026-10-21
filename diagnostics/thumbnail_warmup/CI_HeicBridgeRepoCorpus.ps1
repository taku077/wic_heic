# Read-only HEIF -> HEVC Annex B -> BGRA parity study using pinned repository fixtures.
# Strictly no use of user photographs; all outputs are in a new CI RUNNER_TEMP directory.
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'CI only'}
$ffmpeg=Get-Command ffmpeg -ErrorAction SilentlyContinue
if(-not $ffmpeg){throw 'FFmpeg executable not available in CI'}
$root=(Resolve-Path 'wictest/samples').Path
$dest=Join-Path $env:RUNNER_TEMP 'heic_annexb_repo_fixture_study'
if(Test-Path -LiteralPath $dest){throw 'Output directory already exists'}
New-Item -ItemType Directory -Path $dest|Out-Null
$files=@('03.heic','04.heic','05.heic','06.heic','07.heic',
         'fixture-irot3-exif6-noaux.heic','fixture-irot3-exif6.heic')
$total=0; $copied=0; $equal=0; $mismatch=0; $unavailable=0
foreach($file in $files){
  $source=Join-Path $root $file
  if(-not (Test-Path -LiteralPath $source)){throw "Pinned sample missing: $file"}
  $originalHash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
  $base=[IO.Path]::GetFileNameWithoutExtension($file)
  $compressed=Join-Path $dest ($base+'.h265')
  $primary=Join-Path $dest ($base+'.main.bgra')
  $elementary=Join-Path $dest ($base+'.annexb.bgra')
  foreach($path in @($compressed,$primary,$elementary)){
    if(Test-Path -LiteralPath $path){throw 'Refusing to overwrite output'}
  }
  & $ffmpeg.Source -hide_banner -nostdin -loglevel error -i $source -map 0:v:0 -c:v copy -bsf:v hevc_mp4toannexb -f hevc $compressed
  $copiedOK=($LASTEXITCODE -eq 0) -and (Test-Path $compressed) -and ((Get-Item $compressed).Length -gt 64)
  if($copiedOK){
    $copied++
    & $ffmpeg.Source -hide_banner -nostdin -loglevel error -i $source -frames:v 1 -pix_fmt bgra -f rawvideo $primary
    $fromHeic=($LASTEXITCODE -eq 0) -and (Test-Path $primary)
    & $ffmpeg.Source -hide_banner -nostdin -loglevel error -f hevc -i $compressed -frames:v 1 -pix_fmt bgra -f rawvideo $elementary
    $fromAnnex=($LASTEXITCODE -eq 0) -and (Test-Path $elementary)
    if($fromHeic -and $fromAnnex){
      $h1=(Get-FileHash $primary -Algorithm SHA256).Hash
      $h2=(Get-FileHash $elementary -Algorithm SHA256).Hash
      $eq=($h1 -eq $h2) -and ((Get-Item $primary).Length -eq (Get-Item $elementary).Length)
      if($eq){$equal++}else{
        $mismatch++
        Write-Host "HEIC_HW_BRIDGE_CORPUS_MISMATCH_ANALYSIS file=$file"
        $variantIndex=0
        foreach($filter in @('transpose=clock','transpose=cclock','hflip,vflip','hflip','vflip','transpose=clock,hflip','transpose=cclock,hflip')){
          $result=Join-Path $dest ($base+".candidate$variantIndex.bgra")
          if(Test-Path $result){throw 'Refuse overwriting rotation candidate'}
          & $ffmpeg.Source -hide_banner -nostdin -loglevel error -f hevc -i $compressed -frames:v 1 -vf $filter -pix_fmt bgra -f rawvideo $result
          if($LASTEXITCODE -eq 0 -and (Test-Path $result)){
            $wanted=(Get-FileHash $primary -Algorithm SHA256).Hash
            $candidate=(Get-FileHash $result -Algorithm SHA256).Hash
            $matches=($wanted -eq $candidate) -and ((Get-Item $primary).Length -eq (Get-Item $result).Length)
            Write-Host "HEIC_HW_BRIDGE_ROTATION_CANDIDATE file=$file filter=$filter pixel_exact=$matches"
          } else {
            Write-Host "HEIC_HW_BRIDGE_ROTATION_CANDIDATE file=$file filter=$filter unavailable=1"
          }
          $variantIndex++
        }
        $noRotate=Join-Path $dest ($base+'.noautorotate.bgra')
        & $ffmpeg.Source -hide_banner -nostdin -loglevel error -noautorotate -i $source -frames:v 1 -pix_fmt bgra -f rawvideo $noRotate
        if($LASTEXITCODE -eq 0 -and (Test-Path $noRotate)){
          $noRotateMatches=(Get-FileHash $noRotate -Algorithm SHA256).Hash -eq (Get-FileHash $elementary -Algorithm SHA256).Hash
          Write-Host "HEIC_HW_BRIDGE_NOAUTOROTATE_MATCH file=$file pixel_exact=$noRotateMatches"
        }
      }
      Write-Host "HEIC_HW_BRIDGE_CORPUS_SAMPLE file=$file extract=1 decoded=1 pixels_equal=$eq heic_bytes=$((Get-Item $primary).Length) annexb_bytes=$((Get-Item $elementary).Length)"
    }else{
      $unavailable++
      Write-Host "HEIC_HW_BRIDGE_CORPUS_SAMPLE file=$file extract=1 decoded=0 pixels_equal=unknown"
    }
  }else{
    $unavailable++
    Write-Host "HEIC_HW_BRIDGE_CORPUS_SAMPLE file=$file extract=0 decoded=0 pixels_equal=unknown"
  }
  if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $originalHash){throw "Pinned source file modified: $file"}
  $total++
}
Write-Host "HEIC_HW_BRIDGE_CORPUS_SUMMARY samples=$total extraction_success=$copied pixel_exact=$equal mismatch=$mismatch unavailable=$unavailable"
Write-Host 'HEIC_HW_BRIDGE_CORPUS_ORIGINAL_SHA256_UNCHANGED'
# A mismatched fixture is a RED FLAG for image parity, not test-harness failure.
# It must stop hardware fast-path adoption until HEIF transformation metadata
# and color pipeline differences are understood.
