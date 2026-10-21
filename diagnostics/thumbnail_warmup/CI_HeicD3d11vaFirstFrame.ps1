# Research-only first-frame D3D11VA attempt over the native libheif Annex B bridge.
# Runs exclusively on disposable Windows GitHub runners; never registers a codec,
# edits HEIC originals, or silently calls software decoding a hardware success.
# D3D11 surfaces MUST be downloaded to system-memory BGRA for success.
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'CI only: no workstation execution'}
$ffmpeg=(Get-Command ffmpeg -ErrorAction Stop).Source
$native=(Resolve-Path 'wictest/isolated_shell/(Build)/x64/Release/HeifPrimaryTransforms.exe').Path
$caps=(Resolve-Path 'wictest/isolated_shell/(Build)/x64/Release/D3D11HevcCaps.exe').Path
$bridge=Join-Path $env:RUNNER_TEMP 'heic_native_bridge'
if(-not (Test-Path -LiteralPath $bridge -PathType Container)){throw 'Native Annex B bridge has not been verified'}
$work=Join-Path $env:RUNNER_TEMP 'heic_d3d11_firstframe'
if(Test-Path -LiteralPath $work){throw 'Refusing to reuse GPU result folder'}
New-Item -ItemType Directory -Path $work | Out-Null

# Require existing independent HEIF property audit before the GPU attempt.
$records=@(& $native)
if($LASTEXITCODE -ne 0){throw 'Primary HEIF properties unavailable'}
$routes=@{}
foreach($line in $records){
  if($line -match '^HEIC_PRIMARY_ROUTE file=(\S+) effect=(\w+) allowed=([01])$'){
    if($routes.ContainsKey($Matches[1])){throw 'Duplicate HEIF primary rotation route'}
    $routes[$Matches[1]]=[pscustomobject]@{effect=$Matches[2];allowed=($Matches[3] -eq '1')}
  }
}
if($routes.Count -ne 7){throw 'Incomplete HEIF orientation assessment'}
$candidates=@()
foreach($line in $records){
  if($line -match '^HEIC_GPU_PRECHECK file=(\S+) width=(\d+) height=(\d+) bpp=(\d+)/(\d+) alpha=(\d+) rawICCbytes=(\d+) profile=(\d+) transform_allowed=([01]) route=(\w+) candidate=([01]) rejection_mask=(\d+)$'){
    $name=$Matches[1];$width=[int]$Matches[2];$height=[int]$Matches[3]
    $depth=[int]$Matches[4];$chroma=[int]$Matches[5]
    $alpha=[int]$Matches[6];$icc=[int]$Matches[7]
    $orientationAllowed=$Matches[9] -eq '1'
    $codec=$Matches[10];$eligible=$Matches[11] -eq '1';$reject=[int]$Matches[12]
    if(-not $routes.ContainsKey($name)){throw "Missing independent orientation decision: $name"}
    if($eligible){
      if($reject -ne 0 -or $alpha -ne 0 -or $icc -ne 0 -or -not $orientationAllowed -or -not $routes[$name].allowed){
        throw "Unsafe HEIC classified GPU eligible: $name"
      }
      if(($depth -eq 8 -and $chroma -eq 8 -and $codec -eq 'main8')){
        $surface='nv12'
      } elseif($depth -eq 10 -and $chroma -eq 10 -and $codec -eq 'main10'){
        $surface='p010le'
      } else {throw "Unsupported bit depth classified GPU eligible: $name"}
      if($width -le 0 -or $height -le 0 -or $width -gt 8192 -or $height -gt 8192){throw 'Unsafe geometry'}
      $candidates+= [pscustomobject]@{name=$name;width=$width;height=$height;surface=$surface;route=$routes[$name].effect}
    }
  }
}
Write-Host "HEIC_GPU_FIRSTFRAME_PREFLIGHT candidates=$($candidates.Count) independent_transforms=$($routes.Count)"
$capLines=@(& $caps)
if($LASTEXITCODE -ne 0){throw 'D3D11 native capability query failed'}
$capSummary=@($capLines | Where-Object {$_ -match '^HEIC_GPU_CAPS_SUMMARY hardware_adapters=\d+ supported_12mp_profiles=\d+$'})
if($capSummary.Count -ne 1){throw 'No unique D3D11 hardware summary'}
$capSummary[0] -match 'hardware_adapters=(\d+) supported_12mp_profiles=(\d+)' | Out-Null
$hardware=[int]$Matches[1];$profiles=[int]$Matches[2]
# Match FFmpeg adapter selection to the native D3D11 profile/format/geometry.
# A different adapter with HEVC support cannot authorize adapter 0.
$adaptersByRoute=@{main8=@();main10=@()}
foreach($line in $capLines){
  if($line -match '^HEIC_GPU_PROFILE adapter=(\d+) profile=hevc_(main8|main10) exposed=1 output_format=1 decoder_config12mp=1(& $ffmpeg -hide_banner -hwaccels 2>&1) -join [char]10
$hasD3D11=($LASTEXITCODE -eq 0) -and ($accels -match '(?m)^d3d11va\s*$')
Write-Host "HEIC_GPU_FIRSTFRAME_CAPS hardware_adapters=$hardware profiles_12mp=$profiles ffmpeg_d3d11va=$hasD3D11"
if($hardware -eq 0 -or $profiles -eq 0 -or -not $hasD3D11){
  Write-Host 'HEIC_GPU_FIRSTFRAME_NOT_PROVEN hardware_or_decoder_absent_on_runner=1 fallback_required=1'
  Write-Host 'HEIC_GPU_FIRSTFRAME_CI_NO_GPU_SUCCESS_CLAIM_READONLY_PASS'
  exit 0
}
function Quote-Argument([string]$value){
  if($value -notmatch '[\s"]'){return $value}
  return '"' + $value.Replace('"','\"') + '"'
}
function Invoke-TimedFFmpeg([string]$name,[string[]]$arguments,[string]$target){
  if(Test-Path -LiteralPath $target){throw 'No overwrite permitted'}
  $standard=Join-Path $work "$name.stdout.log"
  $errorLog=Join-Path $work "$name.stderr.log"
  $quoted=($arguments | ForEach-Object {Quote-Argument $_}) -join ' '
  $watch=[Diagnostics.Stopwatch]::StartNew()
  $proc=Start-Process -FilePath $ffmpeg -ArgumentList $quoted -NoNewWindow -Wait -PassThru -RedirectStandardOutput $standard -RedirectStandardError $errorLog
  $watch.Stop()
  $cpuMs=-1
  try{$cpuMs=[math]::Round($proc.TotalProcessorTime.TotalMilliseconds,1)}catch{}
  $peak=0
  try{$peak=$proc.PeakWorkingSet64}catch{}
  $size=if(Test-Path -LiteralPath $target){(Get-Item -LiteralPath $target).Length}else{0}
  Write-Host "HEIC_GPU_FIRSTFRAME_TIMING phase=$name wall_ms=$([math]::Round($watch.Elapsed.TotalMilliseconds,1)) cpu_ms=$cpuMs peak_workingset_bytes=$peak exit=$($proc.ExitCode) output_bytes=$size"
  return [pscustomobject]@{exit=$proc.ExitCode;size=$size;wall=$watch.Elapsed.TotalMilliseconds;cpu=$cpuMs;stderr=$errorLog}
}

# A proof requires an actual D3D11 surface. The hwdownload filter cannot accept
# software frames; its success prevents accidental CPU-decoder success claims.
# This is a hardware feasibility probe, NOT a fast-path intended for Explorer.
$success=0;$incompatible=0;$mismatched=0
foreach($item in $candidates){
  $stem=[IO.Path]::GetFileNameWithoutExtension($item.name)
  $original=(Resolve-Path (Join-Path 'wictest/samples' $item.name)).Path
  $before=(Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
  $annex=Join-Path $bridge "$stem.h265"
  if(-not (Test-Path -LiteralPath $annex -PathType Leaf)){throw "Missing byte-verified native Annex B stream $stem"}
  $software=Join-Path $work "$stem.software.bgra"
  $gpu=Join-Path $work "$stem.d3d11.bgra"
  $profile=if($item.surface -eq 'nv12'){'main8'}else{'main10'}
  $adapters=@($adaptersByRoute[$profile])
  if($adapters.Count -eq 0){
    $incompatible++
    Write-Host "HEIC_GPU_FIRSTFRAME_FALLBACK file=$($item.name) reason=no_size_compatible_decoder_profile"
    continue
  }
  # Check the first matching adapter; attempting all adapters is a separate
  # performance/scalability experiment and must not happen implicitly.
  $adapter=$adapters[0]
  $baseArgs=@('-hide_banner','-nostdin','-loglevel','warning','-n','-threads','2','-f','hevc','-i',$annex,
              '-map','0:v:0','-frames:v','1','-pix_fmt','bgra','-f','rawvideo',$software)
  $sw=Invoke-TimedFFmpeg "$stem-sw" $baseArgs $software
  if($sw.exit -ne 0 -or $sw.size -le 0){throw "Software control did not decode $stem"}
  # Force FFmpeg D3D11VA device, decoder surface and GPU->CPU transfer.
  $args=@('-hide_banner','-nostdin','-loglevel','warning','-n',
          '-init_hw_device',"d3d11va=hw:$adapter",
          '-hwaccel','d3d11va','-hwaccel_device','hw',
          '-hwaccel_output_format','d3d11',
          '-threads','1','-c:v','hevc','-f','hevc','-i',$annex,
          '-map','0:v:0','-frames:v','1',
          '-vf',"hwdownload,format=$($item.surface),format=bgra",
          '-pix_fmt','bgra','-f','rawvideo',$gpu)
  $hw=Invoke-TimedFFmpeg "$stem-hw" $args $gpu
  if($hw.exit -ne 0 -or $hw.size -ne $sw.size){
    $incompatible++
    Write-Host "HEIC_GPU_FIRSTFRAME_FALLBACK file=$($item.name) reason=hwdecode_or_download_failed software_bytes=$($sw.size) hardware_bytes=$($hw.size)"
  } else {
    # Pixel identity is assessed on unrotated raw decoded images. Independent
    # HEIF/FFprobe parity separately guards applying the display transform.
    $shaSW=(Get-FileHash -LiteralPath $software -Algorithm SHA256).Hash
    $shaHW=(Get-FileHash -LiteralPath $gpu -Algorithm SHA256).Hash
    $exact=($shaSW -eq $shaHW)
    if($exact){$success++}else{$mismatched++}
    Write-Host "HEIC_GPU_FIRSTFRAME_RESULT file=$($item.name) adapter=$adapter mode=$($item.surface) display_transform=$($item.route) downloaded_pixels=1 raw_bgra_exact=$exact sw_wall_ms=$([math]::Round($sw.wall,1)) hw_wall_ms=$([math]::Round($hw.wall,1))"
    if(-not $exact){Write-Host "HEIC_GPU_FIRSTFRAME_FALLBACK file=$($item.name) reason=unapproved_gpu_pixel_delta"}
  }
  if((Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash -ne $before){throw "Original HEIC changed: $stem"}
}
Write-Host "HEIC_GPU_FIRSTFRAME_SUMMARY attempted=$($candidates.Count) exact=$success incompatible=$incompatible pixel_mismatch=$mismatched"
Write-Host 'HEIC_GPU_FIRSTFRAME_RESEARCH_ONLY_NO_EXPLORER_SWITCH_NO_MSI_PASS'
){
    $index=[int]$Matches[1];$route=$Matches[2]
    if($index -lt 0 -or $index -gt 31){throw 'Unsafe DXGI adapter index'}
    $adaptersByRoute[$route]+= $index
  }
}
$accels=(& $ffmpeg -hide_banner -hwaccels 2>&1) -join [char]10
$hasD3D11=($LASTEXITCODE -eq 0) -and ($accels -match '(?m)^d3d11va\s*$')
Write-Host "HEIC_GPU_FIRSTFRAME_CAPS hardware_adapters=$hardware profiles_12mp=$profiles ffmpeg_d3d11va=$hasD3D11"
if($hardware -eq 0 -or $profiles -eq 0 -or -not $hasD3D11){
  Write-Host 'HEIC_GPU_FIRSTFRAME_NOT_PROVEN hardware_or_decoder_absent_on_runner=1 fallback_required=1'
  Write-Host 'HEIC_GPU_FIRSTFRAME_CI_NO_GPU_SUCCESS_CLAIM_READONLY_PASS'
  exit 0
}
function Quote-Argument([string]$value){
  if($value -notmatch '[\s"]'){return $value}
  return '"' + $value.Replace('"','\"') + '"'
}
function Invoke-TimedFFmpeg([string]$name,[string[]]$arguments,[string]$target){
  if(Test-Path -LiteralPath $target){throw 'No overwrite permitted'}
  $standard=Join-Path $work "$name.stdout.log"
  $errorLog=Join-Path $work "$name.stderr.log"
  $quoted=($arguments | ForEach-Object {Quote-Argument $_}) -join ' '
  $watch=[Diagnostics.Stopwatch]::StartNew()
  $proc=Start-Process -FilePath $ffmpeg -ArgumentList $quoted -NoNewWindow -Wait -PassThru -RedirectStandardOutput $standard -RedirectStandardError $errorLog
  $watch.Stop()
  $cpuMs=-1
  try{$cpuMs=[math]::Round($proc.TotalProcessorTime.TotalMilliseconds,1)}catch{}
  $peak=0
  try{$peak=$proc.PeakWorkingSet64}catch{}
  $size=if(Test-Path -LiteralPath $target){(Get-Item -LiteralPath $target).Length}else{0}
  Write-Host "HEIC_GPU_FIRSTFRAME_TIMING phase=$name wall_ms=$([math]::Round($watch.Elapsed.TotalMilliseconds,1)) cpu_ms=$cpuMs peak_workingset_bytes=$peak exit=$($proc.ExitCode) output_bytes=$size"
  return [pscustomobject]@{exit=$proc.ExitCode;size=$size;wall=$watch.Elapsed.TotalMilliseconds;cpu=$cpuMs;stderr=$errorLog}
}

# A proof requires an actual D3D11 surface. The hwdownload filter cannot accept
# software frames; its success prevents accidental CPU-decoder success claims.
# This is a hardware feasibility probe, NOT a fast-path intended for Explorer.
$success=0;$incompatible=0;$mismatched=0
foreach($item in $candidates){
  $stem=[IO.Path]::GetFileNameWithoutExtension($item.name)
  $original=(Resolve-Path (Join-Path 'wictest/samples' $item.name)).Path
  $before=(Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
  $annex=Join-Path $bridge "$stem.h265"
  if(-not (Test-Path -LiteralPath $annex -PathType Leaf)){throw "Missing byte-verified native Annex B stream $stem"}
  $software=Join-Path $work "$stem.software.bgra"
  $gpu=Join-Path $work "$stem.d3d11.bgra"
  $baseArgs=@('-hide_banner','-nostdin','-loglevel','warning','-y','-threads','2','-f','hevc','-i',$annex,
              '-map','0:v:0','-frames:v','1','-pix_fmt','bgra','-f','rawvideo',$software)
  $sw=Invoke-TimedFFmpeg "$stem-sw" $baseArgs $software
  if($sw.exit -ne 0 -or $sw.size -le 0){throw "Software control did not decode $stem"}
  # Force FFmpeg D3D11VA device, decoder surface and GPU->CPU transfer.
  $args=@('-hide_banner','-nostdin','-loglevel','warning','-y',
          '-init_hw_device','d3d11va=hw:0',
          '-hwaccel','d3d11va','-hwaccel_device','hw',
          '-hwaccel_output_format','d3d11',
          '-threads','1','-c:v','hevc','-f','hevc','-i',$annex,
          '-map','0:v:0','-frames:v','1',
          '-vf',"hwdownload,format=$($item.surface),format=bgra",
          '-pix_fmt','bgra','-f','rawvideo',$gpu)
  $hw=Invoke-TimedFFmpeg "$stem-hw" $args $gpu
  if($hw.exit -ne 0 -or $hw.size -ne $sw.size){
    $incompatible++
    Write-Host "HEIC_GPU_FIRSTFRAME_FALLBACK file=$($item.name) reason=hwdecode_or_download_failed software_bytes=$($sw.size) hardware_bytes=$($hw.size)"
  } else {
    # Pixel identity is assessed on unrotated raw decoded images. Independent
    # HEIF/FFprobe parity separately guards applying the display transform.
    $shaSW=(Get-FileHash -LiteralPath $software -Algorithm SHA256).Hash
    $shaHW=(Get-FileHash -LiteralPath $gpu -Algorithm SHA256).Hash
    $exact=($shaSW -eq $shaHW)
    if($exact){$success++}else{$mismatched++}
    Write-Host "HEIC_GPU_FIRSTFRAME_RESULT file=$($item.name) mode=$($item.surface) display_transform=$($item.route) downloaded_pixels=1 raw_bgra_exact=$exact sw_wall_ms=$([math]::Round($sw.wall,1)) hw_wall_ms=$([math]::Round($hw.wall,1))"
    if(-not $exact){Write-Host "HEIC_GPU_FIRSTFRAME_FALLBACK file=$($item.name) reason=unapproved_gpu_pixel_delta"}
  }
  if((Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash -ne $before){throw "Original HEIC changed: $stem"}
}
Write-Host "HEIC_GPU_FIRSTFRAME_SUMMARY attempted=$($candidates.Count) exact=$success incompatible=$incompatible pixel_mismatch=$mismatched"
Write-Host 'HEIC_GPU_FIRSTFRAME_RESEARCH_ONLY_NO_EXPLORER_SWITCH_NO_MSI_PASS'
