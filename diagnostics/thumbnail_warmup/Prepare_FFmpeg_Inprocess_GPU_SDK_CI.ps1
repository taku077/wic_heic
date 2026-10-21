# Disposable GitHub Actions only; never installs codecs or edits system registry.
# Immutable dated LGPL shared FFmpeg 8.1 release tag plus GitHub asset digest.
# A moving 'latest' tag must never authorize a research dependency.
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -ne 'true'){throw 'CI ONLY'}
$asset='ffmpeg-n8.1.3-20-gc8de997cb1-win64-lgpl-shared-8.1.zip'
$sha='2eec333253518c6deb0a8b6dff6d615e03f254baa1b04f44356de2725e5fb60f'
$download=Join-Path $env:RUNNER_TEMP 'heic_ffmpeg_sdk_download'
New-Item -ItemType Directory -Force -Path $download|Out-Null
gh release download autobuild-2026-10-21-13-06 -R BtbN/FFmpeg-Builds --pattern $asset --dir $download --clobber
if($LASTEXITCODE -ne 0){throw 'Pinned FFmpeg research asset download failed'}
$zip=Join-Path $download $asset
$actual=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
if($actual -ne $sha){throw "FFmpeg release changed: SHA256 does not match pinned digest: $actual"}
Write-Host "HEIC_FFMPEG_DEVELOPMENT_ASSET_PINNED_SHA256=$actual"
$expanded=Join-Path $env:RUNNER_TEMP 'heic_ffmpeg_sdk'
New-Item -ItemType Directory -Force -Path $expanded|Out-Null
Expand-Archive -LiteralPath $zip -DestinationPath $expanded -Force
$headers=@(Get-ChildItem -LiteralPath $expanded -Filter avcodec.h -File -Recurse)
if($headers.Count -ne 1){throw "FFmpeg development headers missing or ambiguous: $($headers.Count)"}
$header=$headers[0]
if((Split-Path -Leaf $header.DirectoryName) -ne 'libavcodec'){throw 'Unexpected FFmpeg SDK layout'}
$include=Split-Path -Parent $header.DirectoryName
$sdk=Split-Path -Parent $include
$bin=Join-Path $sdk 'bin'
$libs=Join-Path $sdk 'lib'
if(-not (Test-Path -LiteralPath $bin) -or -not (Test-Path -LiteralPath $libs)){throw 'SDK bin/lib not available'}
$vcPath='C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC'
$tools=@(Get-ChildItem -LiteralPath $vcPath -Directory|Sort-Object Name -Descending)
if($tools.Count -lt 1){throw 'VS2022 MSVC toolset not found'}
$compilerBin=Join-Path $tools[0].FullName 'bin\Hostx64\x64'
$dumpbin=Join-Path $compilerBin 'dumpbin.exe'
$libtool=Join-Path $compilerBin 'lib.exe'
if(-not (Test-Path $dumpbin) -or -not (Test-Path $libtool)){throw 'MSVC import library tools unavailable'}
foreach($component in @('avcodec','avutil','swscale')){
  $dlls=@(Get-ChildItem -LiteralPath $bin -Filter "$component-*.dll" -File)
  if($dlls.Count -ne 1){throw "$component DLL missing or ambiguous"}
  $dll=$dlls[0]
  $exports=& $dumpbin /nologo /exports $dll.FullName
  if($LASTEXITCODE -ne 0){throw "dumpbin failed for $component"}
  $symbols=@($exports | ForEach-Object {
    if($_ -match '^\s*\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+([A-Za-z_?][A-Za-z0-9_?@.]*)'){
      $Matches[1]
    }
  } | Sort-Object -Unique)
  if($symbols.Count -lt 30){throw "Only $($symbols.Count) exported functions found for $component"}
  $definition=Join-Path $libs "$component.def"
  $lines=@("LIBRARY $($dll.Name)","EXPORTS")+@($symbols)
  [IO.File]::WriteAllLines($definition,$lines,[Text.Encoding]::ASCII)
  & $libtool "/def:$definition" "/out:$(Join-Path $libs "$component.lib")" '/machine:x64' /nologo
  if($LASTEXITCODE -ne 0){throw "MSVC import lib creation failed for $component"}
  Write-Host "HEIC_FFMPEG_MSVC_IMPORTS_PASS library=$component exports=$($symbols.Count)"
}
"FFMPEG_RESEARCH_ROOT=$sdk" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"FFMPEG_LIB_DIR=$libs" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
"FFMPEG_INCLUDE_DIR=$include" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
$bin | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
Write-Host "HEIC_FFMPEG_WIN64_SDK_PINNED_AND_MSVC_LINKABLE"
