#requires -version 5.1
# Export ONLY latest B JSON and its SHA-pinned synthetic working HEIC.
# Does not read arbitrary user photos. No overwrite, deletion or MSI activity.
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
$dir=Join-Path $root 'Photos_AB_Results'
$reports=@(Get-ChildItem -LiteralPath $dir -File -ErrorAction Stop |
  Where-Object { $_.Name -match '^Photos_AB_B_([0-9]{8}_[0-9]{6}_[0-9]{3}_[0-9a-fA-F]{8})\.json$' } |
  Sort-Object LastWriteTimeUtc -Descending)
if(-not $reports.Count){throw 'No completed B capture to collect. Run B_GUI_Capture_and_Pack.cmd'}
$report=$reports[0]
if($report.Length -gt 65536){throw 'B report unexpectedly large'}
$match=[regex]::Match($report.Name,'^Photos_AB_B_([0-9]{8}_[0-9]{6}_[0-9]{3}_[0-9a-fA-F]{8})\.json$')
$id=$match.Groups[1].Value
$data=Get-Content -LiteralPath $report.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
$sha='D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95'
if($data.Schema -ne 1 -or $data.Phase -cne 'B' -or $data.SyntheticReferenceSha256 -cne $sha) {
  throw 'B report is not an approved synthetic HEIC comparison record'
}
$workDir=Join-Path $dir ('Photos_AB_B_Synthetic_'+$id)
if((Get-Item -LiteralPath $workDir -ErrorAction Stop).Attributes -band [IO.FileAttributes]::ReparsePoint) {
  throw 'Symlinked artificial HEIC folder rejected'
}
$work=Join-Path $workDir 'synthetic-working.heic'
$ref=Join-Path $workDir 'synthetic-original-READONLY.heic'
if(-not (Test-Path -LiteralPath $work -PathType Leaf) -or
   -not (Test-Path -LiteralPath $ref -PathType Leaf)){throw 'Synthetic source and working copy missing'}
if((Get-FileHash -LiteralPath $ref -Algorithm SHA256).Hash -cne $sha) {
  throw 'Synthetic source is modified; refuse packaging'
}
if((Get-Item -LiteralPath $work).Length -gt 25000000) {
  throw 'Synthetic output unexpectedly large; refuse packaging'
}
$zip=Join-Path $dir ('Photos_B_Result_'+$id+'_'+[guid]::NewGuid().ToString('N').Substring(0,8)+'.zip')
# Compress exactly two allowlisted synthetic artifacts, no personal paths.
Compress-Archive -LiteralPath @($report.FullName,$work) -DestinationPath $zip -CompressionLevel Optimal -ErrorAction Stop
Write-Host 'B側の診断結果を保存しました。次のZIPをChatGPTに添付してください。' -ForegroundColor Green
Write-Host $zip
Write-Host '元の人工HEIC、以前の記録、他の写真は変更していません。'
