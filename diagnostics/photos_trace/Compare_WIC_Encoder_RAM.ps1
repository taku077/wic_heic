# CI-only comparison: 1.1.18 versus research WIC, one pinned synthetic HEIC.
# Outputs: new RAM-only IStream. Never Photos, real images, registry or user PC.
param(
  [Parameter(Mandatory=$true)][ValidateSet('ORIGINAL118','RESEARCH')][string]$Label,
  [Parameter(Mandatory=$true)][string]$Probe
)
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -cne 'true') {throw 'This A/B integration is CI-only'}
$probeFile=Get-Item -LiteralPath $Probe -ErrorAction Stop
if($probeFile.Extension -ine '.exe') {throw 'Invalid probe'}
$fixture=Join-Path $env:GITHUB_WORKSPACE 'wictest/samples/fixture-irot3-exif6-noaux.heic'
$pinned='D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95'
if((Get-Item -LiteralPath $fixture).Length -ne 10597 -or
   (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -cne $pinned){
  throw 'Pinned public HEIC did not match fixture'
}
$staged=Join-Path $probeFile.DirectoryName 'fixture-irot3-exif6-noaux.heic'
if(Test-Path -LiteralPath $staged){
  if((Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash -cne $pinned){
    throw 'Existing fixture differs; do not overwrite'
  }
} else {
  Copy-Item -LiteralPath $fixture -Destination $staged
}
$log=@(& $probeFile.FullName '--synthetic-wic-test' 2>&1)
$code=$LASTEXITCODE
if($code -ne 0 -or
   -not ($log -match '^HEVC_SYNTHETIC_GIT_BLOB_VERIFIED_PASS bytes=10597') -or
   -not ($log -match '^HEVC_SYNTHETIC_WIC_COMPARISON_COMPLETE')){
  throw ('Synthetic HEIC WIC A/B incomplete, label='+$Label+' exit='+$code)
}
$full=$log -join ([string][char]10) # normalize to LF for anchored .NET regex
# Stage-only diagnostics: no paths, filenames, EXIF, image bytes or user data.
foreach($entry in $log){
  if([string]$entry -match '^HEVC_SYNTHETIC_(WIC|AUTO|ENCODER) ' -and
     [string]$entry -notmatch 'fnv64' -and [string]$entry -notmatch 'rgb24_fnv64'){
    Write-Host ('WIC_AB_SYNTHETIC_STAGE label='+$Label+' '+[string]$entry)
  }
}
$auto=[regex]::Match($full,'(?m)^HEVC_SYNTHETIC_AUTO stage=decoder_clsid hr=0x([0-9A-F]{8}) custom=([01]) microsoft=([01])$')
if($auto.Success) {
  Write-Host ('WIC_PHOTOS_SYNTHETIC_AUTO_DECODER label='+$Label+' hr='+$auto.Groups[1].Value+
    ' custom='+$auto.Groups[2].Value+' microsoft='+$auto.Groups[3].Value)
} else {
  Write-Host ('WIC_PHOTOS_SYNTHETIC_AUTO_DECODER label='+$Label+' result=NOT_REACHED')
}
function Get-Stage([string]$role,[string]$stage) {
  $re='(?m)^HEVC_SYNTHETIC_ENCODER role='+[regex]::Escape($role)+' stage='+[regex]::Escape($stage)+' hr=0x([0-9A-F]{8})$'
  $match=[regex]::Match($full,$re)
  if($match.Success){return $match.Groups[1].Value}
  return 'NOT_REACHED'
}
function Get-Exact([string]$role) {
  $re='(?m)^HEVC_SYNTHETIC_ENCODER role='+[regex]::Escape($role)+' stage=readback hr=0x([0-9A-F]{8}) bytes=([0-9]+) exact_original_bytes=([01]) photos_not_tested=1$'
  $match=[regex]::Match($full,$re)
  if($match.Success -and $match.Groups[1].Value -eq '00000000') {return $match.Groups[3].Value}
  return 'NOT_REACHED'
}
foreach($role in @('microsoft_heif_explicit','custom_heic_explicit')) {
  $created=Get-Stage $role 'create'
  $wrote=Get-Stage $role 'write_source'
  $committed=Get-Stage $role 'encoder_commit'
  $exact=Get-Exact $role
  Write-Host ('WIC_PHOTOS_SYNTHETIC_RAM_SAVE_AB label='+$Label+' encoder='+$role+
    ' create='+$created+' write='+$wrote+' commit='+$committed+' exact='+$exact)
  if($Label -eq 'RESEARCH' -and $role -eq 'custom_heic_explicit') {
    if($created -cne '00000000' -or $wrote -cne '00000000' -or
       $committed -cne '00000000' -or $exact -cne '1'){
      throw 'Research WIC failed byte-exact synthetic HEIC memory output'
    }
  }
}
Write-Host ('WIC_PHOTOS_SYNTHETIC_RAM_SAVE_AB_'+$Label+'_PASS')
