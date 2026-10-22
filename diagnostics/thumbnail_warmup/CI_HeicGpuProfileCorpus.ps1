# CI only, fixed public HEIC corpus. Never enumerate user photos or edit sources.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'CI only'}
if(-not $env:GITHUB_WORKSPACE -or -not $env:RUNNER_TEMP){throw 'CI root missing'}
$workspace=(Resolve-Path -LiteralPath $env:GITHUB_WORKSPACE).Path
$root=Join-Path $workspace 'wictest\samples'
$ffmpeg=(Get-Command ffmpeg -ErrorAction Stop).Source
$ffprobe=(Get-Command ffprobe -ErrorAction Stop).Source
$work=Join-Path $env:RUNNER_TEMP 'heic_gpu_corpus_readonly'
if(Test-Path -LiteralPath $work){throw 'Refuse overwrite'}
New-Item -ItemType Directory -Path $work | Out-Null
$names=@('03','04','05','06','07','fixture-irot3-exif6','fixture-irot3-exif6-noaux')
$records=New-Object 'System.Collections.Generic.List[object]'
foreach($stem in $names){
  $source=Join-Path $root ($stem+'.heic')
  if(-not (Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing pinned HEIC $stem"}
  $before=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
  $h265=Join-Path $work ($stem+'.h265')
  & $ffmpeg -hide_banner -nostdin -loglevel error -i $source -map 0:v:0 -c:v copy -bsf:v hevc_mp4toannexb -f hevc $h265
  if($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $h265)){throw "HEVC primary extraction failed $stem"}
  $json=(& $ffprobe -v error -f hevc -i $h265 -select_streams v:0 -show_entries stream=codec_name,profile,width,height,pix_fmt,color_range,color_space -of json) -join [Environment]::NewLine
  if($LASTEXITCODE -ne 0 -or -not $json){throw "HEVC profile probe failed $stem"}
  $info=ConvertFrom-Json -InputObject $json
  if(@($info.streams).Count -ne 1){throw "Ambiguous HEVC streams $stem"}
  $st=@($info.streams)[0]
  if($st.codec_name -ne 'hevc'){throw "Non HEVC source $stem"}
  $width=[int]$st.width;$height=[int]$st.height
  if($width -lt 32 -or $height -lt 32 -or $width -gt 8192 -or $height -gt 8192){throw "HEVC coded geometry out of bounds $stem"}
  $profile=[string]$st.profile;$format=[string]$st.pix_fmt
  $route='cpu_fallback';$reason='unsupported_or_unverified_profile'
  if($profile -eq 'Main' -and $format -eq 'yuv420p'){
    $route='gpu_main8_profile_probe_only'
    $reason='pending_heif_metadata_pixel_and_explorer_checks'
  }elseif($profile -eq 'Main 10' -and $format -eq 'yuv420p10le'){
    $route='gpu_main10_profile_probe_only'
    $reason='pending_48bpp_heif_metadata_pixel_and_explorer_checks'
  }elseif($profile -eq 'Rext'){
    $reason='range_extensions_not_intel_hd620_proven'
  }
  $after=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
  if($before -ne $after){throw "Source HEIC mutated $stem"}
  $records.Add([pscustomobject]@{
    name=$stem;profile=$profile;pix_fmt=$format;coded_width=$width;coded_height=$height
    compressed_bytes=(Get-Item -LiteralPath $h265).Length
    codec_route=$route;reject_or_pending=$reason;original_sha256_unchanged=$true
  })
  Write-Host ("HEIC_GPU_CORPUS_SAMPLE name={0} profile={1} format={2} width={3} height={4} codec_route={5} source_unchanged=1" -f $stem,($profile -replace ' ','_'),$format,$width,$height,$route)
}
if($records.Count -ne 7){throw 'Corpus incomplete'}
$main8=@($records|Where-Object {$_.codec_route -eq 'gpu_main8_profile_probe_only'}).Count
$main10=@($records|Where-Object {$_.codec_route -eq 'gpu_main10_profile_probe_only'}).Count
$rext=@($records|Where-Object {$_.profile -eq 'Rext'}).Count
$other=7-$main8-$main10-$rext
if($other -lt 0){throw 'Bad corpus profile summary'}
$report=Join-Path $work 'public_sample_heic_profile_census.json'
$records | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $report -Encoding UTF8
Write-Host "HEIC_GPU_CORPUS_SUMMARY total=7 main8=$main8 main10=$main10 rext=$rext other=$other original_sha256_unchanged=7 product_enabled=0"
Write-Host 'HEIC_GPU_CORPUS_FIXED_READONLY_7_OF_7_PASS'
