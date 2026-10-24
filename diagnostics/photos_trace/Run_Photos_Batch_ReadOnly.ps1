# Windows 10 PowerShell 5.1. Single user action: read-only WIC, MFT,
# SHA-pinned public synthetic HEIC decoder and memory-only encoder surveys.
# No Photos GUI, MSIs, other photos, registration or existing output touched.
# Shareable ZIP contains ONLY sanitized diagnostics, not raw DLL/user paths.
$ErrorActionPreference='Stop'
if($args.Count -ne 0){throw 'No input arguments or arbitrary photo paths are allowed.'}
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
if($root -notmatch '^[A-Za-z]:\\'){throw 'Extract to a local Windows drive.'}
function Assert-NoReparse([string]$path) {
  $node=Get-Item -LiteralPath $path -ErrorAction Stop
  while($null -ne $node) {
    if(($node.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
      throw 'Reparse points/symlinks are forbidden.'
    }
    if($node -is [IO.FileInfo]){$node=$node.Directory}
    else{$node=$node.Parent}
  }
}
Assert-NoReparse $root
$exe=Join-Path $root 'HevcMfReadOnlyProbe.exe'
$fixture=Join-Path $root 'fixture-irot3-exif6-noaux.heic'
$check=Join-Path $root 'WIC_Preflight_ReadOnly.ps1'
foreach($p in @($exe,$fixture,$check)){
  if(-not (Test-Path -LiteralPath $p -PathType Leaf)){
    throw ('Required test bundle file missing: '+[IO.Path]::GetFileName($p))
  }
  Assert-NoReparse $p
}
$expected='D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95'
if((Get-Item -LiteralPath $fixture).Length -ne 10597 -or
   (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -cne $expected){
  throw 'Synthetic HEIC size or SHA256 mismatch; no codec tests were run.'
}
$sessionName='WIC_Batch_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+[guid]::NewGuid().ToString('N')
$folder=Join-Path $root $sessionName
[IO.Directory]::CreateDirectory($folder)|Out-Null
$summary=New-Object 'System.Collections.Generic.List[string]'
$summary.Add('Windows 10 Photos - ONE-ACTION WIC/HEVC diagnostic')
$summary.Add('Schema=1')
$summary.Add('No user photo loaded, no image modified, no codec/registry updates.')
$summary.Add('Photos editor, HEVC license and HEIC save were NOT tested.')
$summary.Add('---')
function Capture([string]$option,[string]$label){
  $data=@();$code=-1
  try {
    $data=@(& $exe $option 2>&1)
    $code=$LASTEXITCODE
  } catch { $data=@('HEVC_WIC_INVOCATION_ERROR') }
  $clean=@($data | ForEach-Object { [string]$_ } | Where-Object {
      $_ -match '^HEVC_(WIC_|MF_|SYNTHETIC_)' -and
      $_.Length -le 512 -and $_ -notmatch '[\\/:]' })
  $summary.Add('['+$label+'] exit='+$code)
  foreach($entry in $clean){$summary.Add($entry)}
  if($clean.Count -eq 0){$summary.Add('NO_SAFE_DIAGNOSTIC_ROWS')}
  return [pscustomobject]@{Exit=$code;Lines=$clean}
}
# Exactly two native calls: WIC/MFT inventory and the one pinned artificial HEIC.
$inventory=Capture '--wic-activate-no-image' 'WIC_AND_MFT_INVENTORY'
$synthetic=Capture '--synthetic-wic-test' 'SYNTHETIC_WIC_EXPLICIT_AND_AUTO'
# The legacy inventory may write a detailed JSON alongside this kit, but
# only a reduced nonidentifying summary goes into the shareable ZIP.
$preflightOutput=@();$preflightCode=-1
try {
  $preflightOutput=@(& powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $check 2>&1)
  $preflightCode=$LASTEXITCODE
} catch { $preflightOutput=@('PRECHECK_ERROR') }
$summary.Add('[INSTALLED_CODECS_AND_PHOTOS] exit='+$preflightCode)
if($preflightCode -eq 0){
  $possible=@($preflightOutput | ForEach-Object {[string]$_} |
    Where-Object {$_ -match '^[A-Za-z]:\\.*WIC_Preflight_\d{8}_\d{6}_\d+_\d+\.json$'})
  if($possible.Count -gt 0) {
    $item=Get-Item -LiteralPath $possible[-1] -ErrorAction Stop
    Assert-NoReparse $item.FullName
    if($item.Directory.FullName -ieq $root -and $item.Length -le 1048576){
      $parsed=Get-Content -LiteralPath $item.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
      $summary.Add('Windows='+[string]$parsed.Windows.ProductName+
        ' build='+[string]$parsed.Windows.CurrentBuild+
        ' release='+[string]$parsed.Windows.DisplayVersion)
      foreach($v in @($parsed.Photos)){$summary.Add('PhotosVersion='+[string]$v.Version)}
      foreach($v in @($parsed.MediaCodecAppxCurrentUser)){
        $summary.Add('Appx='+[string]$v.Name+' version='+[string]$v.Version+
          ' status='+[string]$v.Status)
      }
      foreach($v in @($parsed.WicHeicMsi)){
        $summary.Add('WicHeicMsi='+[string]$v.Name+' version='+[string]$v.Version)
      }
    } else {$preflightCode=6;$summary.Add('PRECHECK_PATH_OR_SIZE_REJECTED')}
  } else {$preflightCode=6;$summary.Add('PRECHECK_JSON_NOT_LOCATED')}
}
function FindHr([string[]]$records,[string]$pattern){
  foreach($line in $records){
    $match=[regex]::Match($line,$pattern)
    if($match.Success){return $match.Groups[1].Value}
  }
  return 'NOT_REACHED'
}
$nativeHr=FindHr $synthetic.Lines '^HEVC_SYNTHETIC_WIC role=microsoft_heif stage=copy_pixels hr=(0x[0-9A-F]{8})'
$customHr=FindHr $synthetic.Lines '^HEVC_SYNTHETIC_WIC role=custom_heic stage=copy_pixels hr=(0x[0-9A-F]{8})'
$autoHr=FindHr $synthetic.Lines '^HEVC_SYNTHETIC_AUTO stage=copy_pixels hr=(0x[0-9A-F]{8})'
$chosen='UNKNOWN_OR_NOT_SELECTED'
foreach($line in $synthetic.Lines){
  if($line -match '^HEVC_SYNTHETIC_AUTO stage=decoder_clsid hr=0x00000000 custom=(\d) microsoft=(\d)'){
    if($Matches[1] -eq '1'){$chosen='CUSTOM_WIC'}
    elseif($Matches[2] -eq '1'){$chosen='MICROSOFT_HEIF'}
    else{$chosen='OTHER_WIC_CODEC'}
    break
  }
}
$summary.Add('--- [INTERPRETATION: WIC ONLY, NOT PHOTOS APP] ---')
$summary.Add('NativeMicrosoftCopyPixels='+$nativeHr)
$summary.Add('CustomWicCopyPixels='+$customHr)
$summary.Add('AutoSelectedWicDecoder='+$chosen)
$summary.Add('AutoSelectedCopyPixels='+$autoHr)

# Differentiates mere WIC encoder COM creation from acceptance and actual
# memory-stream completion on the same unchanged, SHA-pinned HEIC input.
# These fields NEVER claim an app UI save or user-picture metadata retention.
foreach($pair in @(
  @('microsoft_heif','NativeHeifEncoder'),
  @('custom_heic','CustomHeicEncoder'),
  @('microsoft_heif_explicit','NativeHeifExplicitEncoder'),
  @('custom_heic_explicit','CustomHeicExplicitEncoder')
)) {
  $role=$pair[0];$label=$pair[1]
  $sourceHr=FindHr $synthetic.Lines ('^HEVC_SYNTHETIC_ENCODER role='+$role+' stage=write_source hr=(0x[0-9A-F]{8})')
  $commitHr=FindHr $synthetic.Lines ('^HEVC_SYNTHETIC_ENCODER role='+$role+' stage=encoder_commit hr=(0x[0-9A-F]{8})')
  $exact='NOT_REACHED'
  foreach($line in $synthetic.Lines) {
    if($line -match ('^HEVC_SYNTHETIC_ENCODER role='+$role+' stage=readback hr=0x[0-9A-F]{8} bytes=[0-9]+ exact_original_bytes=([01]) photos_not_tested=1')) {
      $exact=$Matches[1];break
    }
  }
  $summary.Add($label+'WriteSource='+$sourceHr)
  $summary.Add($label+'Commit='+$commitHr)
  $summary.Add($label+'ExactSourceBytes='+$exact)
}
$summary.Add('Encoder trial scope=memory_only; Photos GUI/HEVC license not tested.')

$hasPin=@($synthetic.Lines | Where-Object {$_ -match '^HEVC_SYNTHETIC_GIT_BLOB_VERIFIED_PASS bytes=10597'}).Count -eq 1
$hasEnd=@($synthetic.Lines | Where-Object {$_ -match '^HEVC_SYNTHETIC_WIC_COMPARISON_COMPLETE'}).Count -eq 1
$complete=($inventory.Exit -eq 0 -and $synthetic.Exit -eq 0 -and
  $preflightCode -eq 0 -and $hasPin -and $hasEnd)
$summary.Add('BATCH_COLLECTION_STATUS='+$(if($complete){'COMPLETE'}else{'PARTIAL'}))
$summary.Add('No proof of Windows Photos edit buttons, EXIF/ICC or safe rotation.')
$txt=Join-Path $folder 'WIC_Batch_Sanitized_Report.txt'
$bytes=(New-Object System.Text.UTF8Encoding($false)).GetBytes(
  ($summary -join [Environment]::NewLine)+[Environment]::NewLine)
$stream=[IO.File]::Open($txt,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=Join-Path $root ($sessionName+'.zip')
if(Test-Path -LiteralPath $zip){throw 'Refusing existing output overwrite.'}
[IO.Compression.ZipFile]::CreateFromDirectory($folder,$zip)
Write-Host ('Batch collection status: '+$(if($complete){'COMPLETE'}else{'PARTIAL'}))
Write-Host 'Share only the single WIC_Batch_*.zip after reviewing its contents.'
Write-Host ('New report ZIP: '+$zip)
if(-not $complete){exit 1}
exit 0
