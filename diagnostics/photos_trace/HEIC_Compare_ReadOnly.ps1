# Compare two ALREADY EXISTING HEIC files WITHOUT modifying them.
# Windows PowerShell 5.1; FFprobe already installed in the user's environment.
# Only size and selected video-stream fields are exported. No file names,
# paths, EXIF, GPS, timestamps, source bytes, or pixel data are exported.
# If parameters are absent, uses the standard Windows Open File dialog.
param(
    [string]$Before,
    [string]$After
)
$ErrorActionPreference='Stop'
function Choose-Existing([string]$title) {
    Add-Type -AssemblyName System.Windows.Forms
    $d=New-Object System.Windows.Forms.OpenFileDialog
    try {
        $d.Title=$title
        $d.Filter='HEIC images (*.heic;*.heif)|*.heic;*.heif'
        $d.CheckFileExists=$true
        $d.CheckPathExists=$true
        $d.Multiselect=$false
        if($d.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
            throw 'File selection cancelled. No changes were made.'
        }
        return $d.FileName
    } finally { $d.Dispose() }
}
if([string]::IsNullOrWhiteSpace($Before)) {
    $Before=Choose-Existing '回転前の既存HEICを選択（読取専用）'
}
if([string]::IsNullOrWhiteSpace($After)) {
    $After=Choose-Existing '回転保存後の既存HEICを選択（読取専用）'
}
function Validate-Input([string]$p) {
    $f=Get-Item -LiteralPath $p -ErrorAction Stop
    if($f.PSIsContainer -or $f.Extension -notin @('.heic','.heif')) {
        throw 'Expected an existing .heic or .heif file.'
    }
    # Never follow symlinks/reparse points into some other media location.
    $node=$f
    while($null -ne $node) {
        if(($node.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Linked files or directories are not supported.'
        }
        if($node -is [IO.FileInfo]) { $node=$node.Directory }
        else { $node=$node.Parent }
    }
    return $f
}
$first=Validate-Input $Before
$second=Validate-Input $After
if([string]::Equals($first.FullName,$second.FullName,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Select two distinct existing images.'
}
$probe=Get-Command ffprobe.exe -ErrorAction SilentlyContinue
function Inspect-Only([IO.FileInfo]$file) {
    $obj=[ordered]@{
        Bytes=[long]$file.Length
        ProbeStatus='UNAVAILABLE'
        Codec=$null
        Profile=$null
        Width=$null
        Height=$null
        PixelFormat=$null
        BitsPerRawSample=$null
        ColorPrimaries=$null
        ColorTransfer=$null
    }
    if($null -eq $probe) {return $obj}
    try {
        # Whitelist fields. Never request input names/tags/EXIF/GPS or raw pixels.
        $fields='stream=codec_name,profile,width,height,pix_fmt,bits_per_raw_sample,color_primaries,color_transfer'
        $json=@(& $probe.Source -v error -select_streams v:0 -show_entries $fields -of json -i $file.FullName 2>$null)
        if($LASTEXITCODE -ne 0) { $obj.ProbeStatus='UNSUPPORTED_OR_ERROR'; return $obj }
        $parsed=($json -join [Environment]::NewLine) | ConvertFrom-Json -ErrorAction Stop
        if(@($parsed.streams).Count -lt 1) {
            $obj.ProbeStatus='NO_VIDEO_STREAM'
            return $obj
        }
        $v=@($parsed.streams)[0]
        $obj.ProbeStatus='OK'
        foreach($pair in @(
            @('Codec','codec_name'),@('Profile','profile'),
            @('Width','width'),@('Height','height'),
            @('PixelFormat','pix_fmt'),@('BitsPerRawSample','bits_per_raw_sample'),
            @('ColorPrimaries','color_primaries'),@('ColorTransfer','color_transfer')
        )) {
            $value=$v.($pair[1])
            if($null -ne $value) {$obj[$pair[0]]=$value}
        }
    } catch {
        $obj.ProbeStatus='UNSUPPORTED_OR_ERROR'
    }
    return $obj
}
$beforeInfo=Inspect-Only $first
$afterInfo=Inspect-Only $second
$rotatedGeometry='UNKNOWN'
if($beforeInfo.Width -and $beforeInfo.Height -and
   $afterInfo.Width -and $afterInfo.Height) {
    if([long]$beforeInfo.Width -eq [long]$afterInfo.Height -and
       [long]$beforeInfo.Height -eq [long]$afterInfo.Width) {
        $rotatedGeometry='SWAPPED_90_OR_270'
    } elseif([long]$beforeInfo.Width -eq [long]$afterInfo.Width -and
             [long]$beforeInfo.Height -eq [long]$afterInfo.Height) {
        $rotatedGeometry='SAME_DIMENSIONS'
    } else { $rotatedGeometry='CHANGED_NOT_SIMPLE_ROTATION' }
}
$data=[ordered]@{
    Schema=1
    Meaning='READ_ONLY_EXISTING_FILES; no image editing performed'
    Before=$beforeInfo
    After=$afterInfo
    SavedSizePercent=[math]::Round(100.0*$second.Length/[math]::Max(1,$first.Length),2)
    Geometry=$rotatedGeometry
    Interpretation='Size difference alone does not prove quality loss or which WIC encoder path was used. Match this report with an opt-in WIC trace.'
    Note='Only selected codec/geometry fields exported. Input paths and media contents are excluded.'
}
$dest=Join-Path $PSScriptRoot ('HEIC_Compare_ReadOnly_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'.json')
$bytes=(New-Object Text.UTF8Encoding($false)).GetBytes(($data|ConvertTo-Json -Depth 4))
$f=[IO.File]::Open($dest,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try{$f.Write($bytes,0,$bytes.Length)}finally{$f.Dispose()}
Write-Host "READ_ONLY_HEIC_COMPARE_SAVED: $dest"
Write-Host ("Size retained: {0}% ; geometry: {1}" -f $data.SavedSizePercent,$rotatedGeometry)
Write-Host 'No HEIC was changed or deleted. Share the JSON report, not your photographs.'
