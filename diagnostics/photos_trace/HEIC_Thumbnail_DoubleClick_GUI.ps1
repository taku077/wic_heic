#requires -Version 5.1
# Windows 10, Windows PowerShell 5.1, GUI launcher.
# User-controlled HEIC copy only. No installation, elevation, or codec changes.
[CmdletBinding()]
param([switch]$CiSelfTest)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

function ChooseLogDirectory {
    $root=[Environment]::GetFolderPath('MyDocuments')
    foreach($base in @($root,$env:TEMP)) {
        if([string]::IsNullOrWhiteSpace($base)){continue}
        try {
            $folder=Join-Path $base 'HEIC_Thumbnail_Diagnostics'
            if(-not [IO.Directory]::Exists($folder)){
                [void][IO.Directory]::CreateDirectory($folder)
            }
            return $folder
        } catch { }
    }
    throw 'Cannot create a diagnostics log folder.'
}
function EmitResult([string[]]$lines,[bool]$quiet) {
    $dir=ChooseLogDirectory
    $stamp=Get-Date -Format 'yyyyMMdd_HHmmss_fff'
    $suffix=[guid]::NewGuid().ToString('N').Substring(0,8)
    $dest=Join-Path $dir ('HEIC_Diagnosis_{0}_{1}.txt' -f $stamp,$suffix)
    [IO.File]::WriteAllLines($dest,$lines,(New-Object System.Text.UTF8Encoding($true)))
    if($quiet){
        $lines | ForEach-Object { Write-Output $_ }
        Write-Output ('LOG_SAVED=True')
    } else {
        Write-Host ('Log: '+$dest)
        try {
            Start-Process -FilePath 'notepad.exe' -ArgumentList ('"'+$dest+'"') -ErrorAction Stop
        } catch {
            [System.Windows.Forms.MessageBox]::Show(
                '診断結果は保存されました。メモ帳を起動できないため、結果フォルダーを開いてください。',
                'HEICサムネイル診断','OK','Warning') | Out-Null
        }
    }
    return $dest
}

$engine=Join-Path $PSScriptRoot 'HEIC_ThumbnailCache_AB_ReadOnly.ps1'
if(-not [IO.File]::Exists($engine)){
    if(-not $CiSelfTest){
        [System.Windows.Forms.MessageBox]::Show(
          '診断用スクリプトがありません。ZIP内のファイルを同じフォルダーに展開してください。',
          '診断を開始できません','OK','Error') | Out-Null
    }
    throw 'Diagnostic engine script is absent.'
}
$ciFile=$null
try {
    if($CiSelfTest) {
        if($env:GITHUB_ACTIONS -ne 'true') {throw 'CI-only mode prohibited outside GitHub Actions.'}
        if([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)){throw 'RUNNER_TEMP missing'}
        $ciFile=Join-Path $env:RUNNER_TEMP ('heic_gui_ci_'+[guid]::NewGuid().ToString('N')+'.png')
        $bitmap=New-Object System.Drawing.Bitmap(180,120)
        try {
            $bitmap.SetPixel(10,10,[System.Drawing.Color]::Red)
            $bitmap.Save($ciFile,[System.Drawing.Imaging.ImageFormat]::Png)
        } finally {$bitmap.Dispose()}
        $target=$ciFile
        $force=$true
    } else {
        $dlg=New-Object System.Windows.Forms.OpenFileDialog
        try {
            $dlg.Title='HEICサムネイル診断：元画像ではなくコピーを選択'
            $dlg.Filter='HEIC画像 (*.heic;*.heif)|*.heic;*.heif'
            $dlg.Multiselect=$false
            $dlg.CheckFileExists=$true
            $dlg.RestoreDirectory=$true
            if($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
                Write-Host 'User cancelled before selecting an image.'
                exit 0
            }
            $target=$dlg.FileName
        } finally {$dlg.Dispose()}
        $choice=[System.Windows.Forms.MessageBox]::Show(
          'まず古い向きのサムネイルを確認し、F5を押す前に実行してください。'+[Environment]::NewLine+
          '対象は必ずHEICのコピーにしてください。'+[Environment]::NewLine+[Environment]::NewLine+
          '「はい」：キャッシュの記録後に画像を強制再抽出して比較（Windowsのサムネイルキャッシュを更新する可能性あり）'+[Environment]::NewLine+
          '「いいえ」：既存キャッシュの読み取りだけ（既定・安全）'+[Environment]::NewLine+
          '「キャンセル」：診断しない',
          'HEICサムネイル診断の方法',
          [System.Windows.Forms.MessageBoxButtons]::YesNoCancel,
          [System.Windows.Forms.MessageBoxIcon]::Question,
          [System.Windows.Forms.MessageBoxDefaultButton]::Button2)
        if($choice -eq [System.Windows.Forms.DialogResult]::Cancel) {
            Write-Host 'User cancelled before diagnostic.'
            exit 0
        }
        $force=$choice -eq [System.Windows.Forms.DialogResult]::Yes
    }

    $originalHash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    $lines=New-Object 'System.Collections.Generic.List[string]'
    $lines.Add('HEIC_THUMBNAIL_DIAG_GUI_V1')
    $lines.Add('ExecutionMode='+$(if($force){'CACHE_THEN_FORCE_REEXTRACT'}else{'CACHE_ONLY'}))
    $lines.Add('OriginalFilePathAndExif=NOT_LOGGED')
    $lines.Add('TimestampUTC='+[DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))
    $engineData=@()
    $hadError=$false
    try {
        if($CiSelfTest) {
            $engineData=@(& $engine -ImagePath $target -Size 256 -CiSyntheticOnly -CiPrimeCache -ForceReextractAfterSnapshot)
        } elseif($force) {
            $engineData=@(& $engine -ImagePath $target -Size 256 -ForceReextractAfterSnapshot)
        } else {
            $engineData=@(& $engine -ImagePath $target -Size 256)
        }
        foreach($line in $engineData){$lines.Add([string]$line)}
    } catch {
        $hadError=$true
        $lines.Add('DIAGNOSTIC_ERROR_TYPE='+$_.Exception.GetType().Name)
        $lines.Add('DIAGNOSTIC_RESULT=FAILED_OR_INCOMPLETE')
    }
    $hashUnchanged=$false
    try {
        $hashUnchanged=((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $originalHash)
    } catch { $hadError=$true }
    $lines.Add('HEIC_FILE_SHA256_UNCHANGED='+$hashUnchanged)
    if(-not $hashUnchanged){$hadError=$true}
    if($CiSelfTest -and ($hadError -or -not ($lines -match 'COMPARE=IDENTICAL_CACHED_VS_FORCED_EXTRACTION'))){
        throw 'CI GUI runner self-test failed.'
    }
    $dest=EmitResult -lines $lines.ToArray() -quiet $CiSelfTest.IsPresent
    if(-not $CiSelfTest) {
        if($hadError) {
            $message='診断中にエラーまたはファイル変更が検出されました。開いた診断結果を確認してください。'
            $ico=[System.Windows.Forms.MessageBoxIcon]::Warning
        } else {
            $message='診断が終了しました。メモ帳に結果を表示しました。'+[Environment]::NewLine+
                    '保存先：ドキュメント内の HEIC_Thumbnail_Diagnostics フォルダー（書込不可の場合はTEMP）。'+[Environment]::NewLine+
                    '結果を確認後、必要ならExplorerでF5を押してください。'
            $ico=[System.Windows.Forms.MessageBoxIcon]::Information
        }
        [System.Windows.Forms.MessageBox]::Show($message,'HEICサムネイル診断',
          [System.Windows.Forms.MessageBoxButtons]::OK,$ico) | Out-Null
    } elseif(-not [IO.File]::Exists($dest)){
        throw 'Self-test output log missing.'
    }
} finally {
    if($ciFile -and [IO.File]::Exists($ciFile)) {
        Remove-Item -LiteralPath $ciFile -Force -ErrorAction Stop
    }
}
