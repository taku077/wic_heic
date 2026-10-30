# Read-only Windows 10 ETL all-process CPU analyzer GUI.
# No network, registry edits, WPR recording, image edits or file deletion.
param([switch]$SelfTest)
$ErrorActionPreference='Stop'
$here=Split-Path -Parent $MyInvocation.MyCommand.Path
$program=Join-Path $here 'WicHeic_EtlCpuTop_ReadOnly.exe'
# Test the actual Windows PowerShell 5.1 -File loading path without opening a GUI.
if($SelfTest){
  if(-not [System.IO.File]::Exists($program)){throw 'Missing analyzer executable'}
  $selfResult=& $program --self-test
  if($LASTEXITCODE -ne 0 -or (($selfResult -join ' ') -notmatch 'PID_BUCKET_SELFTEST_PASS')){
    throw 'CPU analyzer self-test failed'
  }
  Write-Output 'WINDOWS_POWERSHELL_51_LAUNCHER_SELFTEST_PASS'
  exit 0
}
Add-Type -AssemblyName System.Windows.Forms
if(-not [System.IO.File]::Exists($program)){
  [System.Windows.Forms.MessageBox]::Show('解析プログラムが見つかりません。同じフォルダー内のファイルを移動せず、ZIPをすべて展開してください。','CPU診断',[System.Windows.Forms.MessageBoxButtons]::OK,[System.Windows.Forms.MessageBoxIcon]::Warning)|Out-Null
  exit 1
}
$dialog=New-Object System.Windows.Forms.OpenFileDialog
$dialog.Title='以前に保存したCPU_Stacks.etlを選択（新たな録画は不要）'
$dialog.Filter='WPR CPU_Stacks.etl|CPU_Stacks.etl|ETL (*.etl)|*.etl'
$dialog.CheckFileExists=$true
$dialog.Multiselect=$false
$dialog.RestoreDirectory=$true
if($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK){exit 0}
$etl=$dialog.FileName
$outDir=[System.IO.Path]::GetDirectoryName($etl)
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss_fff'
$out=Join-Path $outDir ('CPU_AllProcesses_'+$stamp+'.txt')
if([System.IO.File]::Exists($out)){throw '出力ファイルが既に存在します。上書きしません。'}
$proc=Start-Process -FilePath $program -ArgumentList @(
  ('"{0}"' -f $etl),('"{0}"' -f $out)) -PassThru -Wait -NoNewWindow
if($proc.ExitCode -ne 0 -or -not(Test-Path -LiteralPath $out)){
  [System.Windows.Forms.MessageBox]::Show(('解析できませんでした。元のETLは変更していません。終了コード：'+$proc.ExitCode),'CPU診断',[System.Windows.Forms.MessageBoxButtons]::OK,[System.Windows.Forms.MessageBoxIcon]::Warning)|Out-Null
  exit 1
}
[System.Windows.Forms.MessageBox]::Show(('CPU集計レポートを作成しました（元のETLは変更していません）。'+[Environment]::NewLine+$out+[Environment]::NewLine+'共有前に内容を確認してください。'),'CPU診断',[System.Windows.Forms.MessageBoxButtons]::OK,[System.Windows.Forms.MessageBoxIcon]::Information)|Out-Null
