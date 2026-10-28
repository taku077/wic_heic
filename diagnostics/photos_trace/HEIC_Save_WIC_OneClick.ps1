#requires -Version 5.1
# One-click save-route investigation: user selects HEIC, the tool makes a
# unique WORK COPY in the same directory and never edits the original.
# Temporarily enables numeric WIC trace, then always attempts to remove it.
param([ValidateRange(10,180)][int]$DurationSeconds=90,[switch]$CiSelfTest)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
$root=$PSScriptRoot
$start=Join-Path $root 'PhotosTrace_Start.ps1'
$stop=Join-Path $root 'PhotosTrace_Stop.ps1'
$monitor=Join-Path $root 'HEIC_SaveShell_PassiveMonitor.ps1'
$analyzer=Join-Path $root 'HEIC_Save_CommitCorrelation.ps1'
foreach($script in @($start,$stop,$monitor,$analyzer)){
 if(-not [IO.File]::Exists($script)){throw 'Incomplete WIC save diagnostic package.'}
}
if($CiSelfTest -and $env:GITHUB_ACTIONS -ne 'true'){throw 'CI-only self test not allowed.'}
if($root -notmatch '^[A-Za-z]:\\'){throw 'Diagnostic tool must be on a local Windows drive.'}
$outFolder=Join-Path $root '診断結果'
[void][IO.Directory]::CreateDirectory($outFolder)
$origin=$null;$originalSha=$null;$workCopy=$null
$trace=$null;$owned=$false;$monitorLog=$null;$reportPath=$null
try {
 if(-not $CiSelfTest){
  $picker=New-Object System.Windows.Forms.OpenFileDialog
  try {
   $picker.Title='IMG_4425 保存経路診断：HEICを選択（自動で作業コピーを作ります）'
   $picker.Filter='HEIC/HEIF (*.heic;*.heif)|*.heic;*.heif'
   $picker.CheckFileExists=$true;$picker.Multiselect=$false
   $picker.RestoreDirectory=$true
   if($picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK){exit 0}
   $origin=$picker.FileName
  } finally {$picker.Dispose()}
  $info=Get-Item -LiteralPath $origin -ErrorAction Stop
  if($info.PSIsContainer -or $info.Extension -notin @('.heic','.heif')){
   throw 'Expected a HEIC/HEIF source file.'
  }
  $node=$info
  while($node){
   if(($node.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
    throw 'Source or parent directory is a link. Not supported.'
   }
   if($node -is [IO.FileInfo]){$node=$node.Directory}
   else {$node=$node.Parent}
  }
  $originalSha=(Get-FileHash -LiteralPath $origin -Algorithm SHA256).Hash
  $extension=$info.Extension
  $workCopy=Join-Path $info.Directory.FullName ('HEIC_SaveProbe_'+[guid]::NewGuid().ToString('N')+$extension)
  # Unique path, no overwrite, source opened read-only. Leave copy for inspection.
  [IO.File]::Copy($origin,$workCopy,$false)
  if((Get-FileHash -LiteralPath $workCopy -Algorithm SHA256).Hash -ne $originalSha){
   throw 'Prepared work copy differs from source before test.'
  }
  [System.Windows.Forms.MessageBox]::Show(
    '元のHEICから同じフォルダーに診断用コピーを自動作成しました。'+[Environment]::NewLine+
    '回転・保存するのはコピーのみです。原本は変更しません。'+[Environment]::NewLine+
    'この後、診断画面の案内に従い、フォトで回転して保存してください。'+[Environment]::NewLine+
    'フォトで保存エラーが出たら、その表示を確認してから「結果を保存」を押してください。',
    '保存経路診断の準備完了','OK','Information')|Out-Null
 }
 # Protect existing user trace settings: Start fails without replacing them.
 & $start
 $owned=$true
 $settings=Get-ItemProperty -LiteralPath 'HKCU:\Software\WicHeic' -ErrorAction Stop
 $trace=[string]$settings.TraceFile
 if(-not $trace -or -not [IO.File]::Exists($trace)){throw 'WIC trace file was not created.'}
 $begin=Get-Date
 try {
  if($CiSelfTest){
   & $monitor -CiSelfTest -QuietResult -DurationSeconds 10
  } else {
   & $monitor -ImagePath $workCopy -QuietResult -DurationSeconds $DurationSeconds
  }
 } finally {
  if($owned){ & $stop; $owned=$false }
 }
 $recent=@(Get-ChildItem -LiteralPath $outFolder -File -Filter 'HEIC_ShellWatch_*.txt' |
    Where-Object {$_.LastWriteTime -ge $begin.AddSeconds(-3)} |
    Sort-Object LastWriteTime -Descending)
 if($recent.Count -eq 0){throw 'The Shell monitor did not create a log.'}
 $monitorLog=$recent[0].FullName
 if($CiSelfTest){
  # Artificial records ONLY in ephemeral CI Windows runner.
  # These ARE NOT real observations from Photos or the installed WIC.
  $base=[DateTime]::UtcNow.ToFileTimeUtc()
  $fixture=@(
   "WIC_HEIC_TRACE v1 tick=100 pid=111 event=EncoderCommitEnter source=one-frame hr=0x00000000 operation=1 elapsed_ms=0 utc_filetime=$base",
   "WIC_HEIC_TRACE v1 tick=101 pid=111 event=EncoderCommitLossless source=heif-copied hr=0x00000000 operation=1 elapsed_ms=20 utc_filetime=$base",
   "WIC_HEIC_TRACE v1 tick=102 pid=111 event=EncoderCommitExit source=lossless hr=0x00000000 operation=1 elapsed_ms=23 utc_filetime=$base",
   "WIC_HEIC_TRACE v1 tick=200 pid=111 event=EncoderCommitEnter source=one-frame hr=0x00000000 operation=2 elapsed_ms=0 utc_filetime=$base",
   "WIC_HEIC_TRACE v1 tick=201 pid=111 event=EncoderCommitRecompressed source=hevc-encoded hr=0x00000000 operation=2 elapsed_ms=20 utc_filetime=$base",
   "WIC_HEIC_TRACE v1 tick=202 pid=111 event=EncoderCommitExit source=libheif-error hr=0x80070057 operation=2 elapsed_ms=25 utc_filetime=$base",
   "WIC_HEIC_TRACE v1 tick=300 pid=112 event=EncoderCommitEnter source=one-frame hr=0x00000000 operation=1 elapsed_ms=0 utc_filetime=$base",
   "WIC_HEIC_TRACE v1 tick=301 pid=112 event=EncoderCommitRejected source=related-visual-unsafe hr=0x88982F81 operation=1 elapsed_ms=9 utc_filetime=$base"
  )
  [IO.File]::AppendAllLines($trace,[string[]]$fixture,([Text.UTF8Encoding]::new($false)))
 }
 # Keep the raw numeric WIC trace beside Shell and summary logs.
 # The user normally ZIPs only the results folder. This fixes that omission.
 # Trace may contain process IDs and timestamps, but never image paths/pixels.
 $traceSnapshot=Join-Path $outFolder ([IO.Path]::GetFileName($trace))
 [IO.File]::Copy($trace,$traceSnapshot,$false)
 & $analyzer -TracePath $trace -ShellLogPath $monitorLog -OutputDirectory $outFolder -CiSelfTest:$CiSelfTest
 $reports=@(Get-ChildItem -LiteralPath $outFolder -File -Filter 'HEIC_Save_WIC_Report_*.txt'|
  Where-Object {$_.LastWriteTime -ge $begin.AddSeconds(-3)}|
  Sort-Object LastWriteTime -Descending)
 if($reports.Count -eq 0){throw 'WIC commit analysis report missing.'}
 $reportPath=$reports[0].FullName
 $sourceUnchanged='NOT_APPLICABLE_SYNTHETIC_CI'
 if($origin){
  $sourceUnchanged=[string]((Get-FileHash -LiteralPath $origin -Algorithm SHA256).Hash -eq $originalSha)
  [IO.File]::AppendAllLines($reportPath,
   [string[]]@('WORK_COPY_CREATED=True',('ORIGINAL_SHA256_UNCHANGED={0}' -f $sourceUnchanged)),
   ([Text.UTF8Encoding]::new($false)))
 }
 if($CiSelfTest){
  $reportData=Get-Content -LiteralPath $reportPath -Encoding UTF8
  if(-not ($reportData -match 'WIC_COMMIT_SUMMARY entered=3 succeeded=1 failed=1 incomplete=1')){
   throw 'Synthetic end-to-end analyzer did not produce correct outcomes.'
  }
  Write-Host 'WIC_SAVE_TRACE_SESSION_SYNTHETIC_PASS'
  Write-Host 'HKCU_CLEANUP_CHECK_PENDING'
 }else {
  Start-Process -FilePath 'explorer.exe' -ArgumentList ('"'+$outFolder+'"') -ErrorAction SilentlyContinue
  Start-Process -FilePath 'notepad.exe' -ArgumentList ('"'+$reportPath+'"') -ErrorAction SilentlyContinue
  [System.Windows.Forms.MessageBox]::Show(
   '診断を終了し、一時的なWICトレース設定を解除しました。'+[Environment]::NewLine+
   '診断結果：'+$outFolder+[Environment]::NewLine+
   '元画像SHA-256の不変：'+$sourceUnchanged+[Environment]::NewLine+
   'フォトの保存方式・エラーは集計テキストで確認できます。'+[Environment]::NewLine+
   '※WICログが空の場合は原因を確定できません。',
   'HEIC 保存経路診断','OK','Information')|Out-Null
 }
} finally {
 if($owned){& $stop}
}
