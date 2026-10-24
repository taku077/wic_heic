# Windows 10 Photos real-GUI A/B comparator, phase A=original 1.1.18, B=research.
# Built-in PowerShell 5.1, normal user. No MSI, registry writes, or personal images.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][ValidateSet('A','B')][string]$Phase,
      [switch]$CiDryRun)
if($CiDryRun -and $env:GITHUB_ACTIONS -cne 'true') {
  throw 'Simulation may only run in GitHub Actions CI'
}
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $PSScriptRoot -ErrorAction Stop).FullName
$rootNode=Get-Item -LiteralPath $root
while($null -ne $rootNode){
  if(($rootNode.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){
    throw 'Junction or symlink folder prohibited'
  }
  $rootNode=$rootNode.Parent
}
$fixture=Join-Path $root 'fixture-irot3-exif6-noaux.heic'
$exe=Join-Path $root 'HevcMfReadOnlyProbe.exe'
$expected='D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95'
if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){throw 'Probe executable missing'}
if(-not (Test-Path -LiteralPath $fixture -PathType Leaf)){throw 'Pinned synthetic HEIC missing'}
if((Get-Item -LiteralPath $fixture).Length -ne 10597 -or
   (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -cne $expected){
  throw 'Synthetic HEIC fixture failed exact hash check'
}
$sessionRoot=Join-Path $root 'Photos_AB_Results'
if(-not (Test-Path -LiteralPath $sessionRoot)){
  New-Item -ItemType Directory -Path $sessionRoot -ErrorAction Stop | Out-Null
}
if((Get-Item -LiteralPath $sessionRoot).Attributes -band [IO.FileAttributes]::ReparsePoint){
  throw 'Results directory cannot be a junction'
}
# Every attempt has an isolated run ID. Even an aborted older v0.1 session
# remains intact: NEVER delete old folders or overwrite any report.
$runId=(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+[guid]::NewGuid().ToString('N').Substring(0,8)
$reportFile=Join-Path $sessionRoot ('Photos_AB_'+$Phase+'_'+$runId+'.json')
$workDir=Join-Path $sessionRoot ('Photos_AB_'+$Phase+'_Synthetic_'+$runId)
$null=New-Item -ItemType Directory -Path $workDir -ErrorAction Stop
$work=Join-Path $workDir 'synthetic-working.heic'
$reference=Join-Path $workDir 'synthetic-original-READONLY.heic'
foreach($dest in @($work,$reference)){
  $source=[IO.File]::Open($fixture,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  $target=$null
  try {
    $target=[IO.File]::Open($dest,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $source.CopyTo($target)
  } finally {
    if($null -ne $target){$target.Dispose()}
    $source.Dispose()
  }
}
[IO.File]::SetAttributes($reference,[IO.FileAttributes]::ReadOnly)
Write-Host ''
Write-Host '------------------------------' -ForegroundColor Cyan
Write-Host ('フォトの実機比較 : '+ $(if($Phase -eq 'A'){'旧1.1.18'}else{'研究版'})) -ForegroundColor Cyan
Write-Host '------------------------------'
Write-Host '【手順 1】開いたフォルダにある「synthetic-working.heic」を'
Write-Host '右クリック →「プログラムから開く」→「フォト」で表示してください。'
Write-Host '本物の写真は一切操作しないでください。'
Write-Host ('人工HEICの場所: '+$work)
if(-not $CiDryRun) {
  try { Start-Process -FilePath 'explorer.exe' -ArgumentList ('/select,"'+$work+'"') -ErrorAction Stop }
  catch { Write-Host 'エクスプローラーを開けませんでした。上記の場所を開いてください。' }
}
function Choice([string]$message,[string[]]$valid) {
  while($true) {
    $value=(Read-Host $message).Trim().ToLowerInvariant()
    if($valid -contains $value){return $value}
    Write-Host ('入力できる値: '+($valid -join ' / ')) -ForegroundColor Yellow
  }
}
$warning='?';$editing='?';$rotation='?';$save='x';$distorted='?'
if($CiDryRun) {
  $warning='n';$editing='y';$rotation='y'
}else{
  $null=Read-Host '【手順 2】フォトで人工HEICを表示したら Enter を押す'
  Write-Host '【手順 3】実際の表示を選んで数字を入力してください。'
  Write-Host '  1 = HEVC警告なし・編集と回転ボタンが使える'
  Write-Host '  2 = HEVC警告あり・編集と回転ボタンが使えない'
  Write-Host '  3 = それ以外・判断できない'
  $mode=Choice '状態（1 / 2 / 3）' @('1','2','3')
  switch($mode){
    '1' {$warning='n';$editing='y';$rotation='y'}
    '2' {$warning='y';$editing='n';$rotation='n'}
    default {
      $warning=Choice 'HEVC警告はありますか？（y=ある / n=ない / ?=不明）' @('y','n','?')
      $editing=Choice '編集ボタンは使えますか？（y / n / ?）' @('y','n','?')
      $rotation=Choice '回転ボタンは使えますか？（y / n / ?）' @('y','n','?')
    }
  }
  Write-Host '【手順 4】回転保存の確認は任意です。今は実施しなくても構いません。'
  Write-Host '試す場合は表示中の人工作業コピーだけを操作してください。'
  Write-Host '  0 = 回転保存は試さない　1 = 保存成功　2 = 保存失敗'
  $status=Choice '保存結果（0 / 1 / 2）' @('0','1','2')
  if($status -eq '1'){$save='s'}
  if($status -eq '2'){$save='f'}
  if($status -ne '0') {
    $distorted=Choice '画像が伸びる・角度が崩れる等の異常はありますか？（y / n / ?）' @('y','n','?')
  }
  Write-Host '結果を自動的に保存します。以前の記録や作業フォルダは残します。'
}

# Work-copy hash is recorded AFTER the manual Photos action.
$referenceOk=(Get-FileHash -LiteralPath $reference -Algorithm SHA256).Hash -ceq $expected
$fixtureOk=(Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -ceq $expected
$working=if(Test-Path -LiteralPath $work -PathType Leaf) {
  $file=Get-Item -LiteralPath $work
  [ordered]@{Exists=$true;Bytes=[int64]$file.Length;
    Sha256=(Get-FileHash -LiteralPath $work -Algorithm SHA256).Hash}
} else {[ordered]@{Exists=$false;Bytes=0;Sha256=$null}}
if(-not $fixtureOk -or -not $referenceOk){throw 'Protected synthetic reference changed; refuse to report success'}

# Snapshot ONLY selected public app/package versions and DLL hashes, never paths.
$packages=[ordered]@{}
foreach($package in @('Microsoft.Windows.Photos','Microsoft.HEIFImageExtension',
  'Microsoft.HEVCVideoExtension','Microsoft.HEVCVideoExtensions')) {
  $found=@(Get-AppxPackage -Name $package -ErrorAction SilentlyContinue)
  $packages[$package]=if($found.Count){
    @($found|ForEach-Object { [string]$_.Version })
  }else{@()}
}
$photosModules=New-Object 'System.Collections.Generic.List[object]'
$photoProcesses=@(Get-Process -Name 'Photos','Microsoft.Photos' -ErrorAction SilentlyContinue)
foreach($process in $photoProcesses) {
  $modules=@()
  try{$modules=@($process.Modules)}catch{}
  $wic=@($modules|Where-Object {$_.ModuleName -ieq 'wic_heic.dll'})
  $codecs=@($modules|Where-Object {$_.ModuleName -ieq 'windowscodecs.dll'})
  $hashes=@()
  foreach($module in $wic) {
    try {
      $hashes+=((Get-FileHash -LiteralPath $module.FileName -Algorithm SHA256 -ErrorAction Stop).Hash)
    }catch{$hashes+=('UNREADABLE')}
  }
  $photosModules.Add([ordered]@{WicHeicLoaded=($wic.Count -gt 0);
    WindowsCodecsLoaded=($codecs.Count -gt 0);WicHeicDllSha256=@($hashes);
    ModulesReadable=($modules.Count -gt 0) })
}
# Run the same public HEIC through Windows' WIC/MF API. It does not read
# photos or write images; keep only allowlisted event codes.
$probeEvents=New-Object 'System.Collections.Generic.List[string]'
$probeStatus='NOT_RUN'
try {
  $raw=@(& $exe '--synthetic-wic-test' 2>&1)
  $code=$LASTEXITCODE
  foreach($line in $raw) {
    $item=[string]$line
    if($item.Length -le 350 -and
       $item -match '^HEVC_SYNTHETIC_(EXPLICIT|AUTO|ENCODER) ' -and
       $item -notmatch '(?i)(file|path|exif|metadata|fnv64|pid)=') {
      $probeEvents.Add($item)
    }
  }
  $auth=@($raw|Where-Object {
    [string]$_ -match '^HEVC_SYNTHETIC_GIT_BLOB_VERIFIED_PASS bytes=10597'})
  $probeStatus=if($code -eq 0 -and $auth.Count){'PASS'}else{'INCOMPLETE'}
}catch{$probeStatus='FAILED'}
$os=Get-CimInstance Win32_OperatingSystem
$report=[ordered]@{
  Schema=1;Phase=$Phase;CollectedUtc=[DateTime]::UtcNow.ToString('o')
  WindowsBuild=[string]$os.BuildNumber
  Packages=$packages
  PhotosProcessCount=$photoProcesses.Count
  PhotosModules=@($photosModules.ToArray())
  GuiObservation=[ordered]@{HevcWarning=$warning;EditUsable=$editing;
    RotateUsable=$rotation;SaveResult=$save;VisibleDistortion=$distorted}
  SyntheticReferenceSha256=$expected
  WorkingCopy=$working
  CodecProbeStatus=$probeStatus
  CodecProbeEvents=@($probeEvents.ToArray())
  UserFilesAccessed=$false
  RegistryOrAppxModified=$false
}
$json=$report|ConvertTo-Json -Depth 9
$bytes=[Text.Encoding]::UTF8.GetBytes($json)
$out=[IO.File]::Open($reportFile,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try {$out.Write($bytes,0,$bytes.Length)}finally{$out.Dispose()}
Write-Host ('完了：'+$Phase+' を新規保存しました。前の結果は消していません。') -ForegroundColor Green
Write-Host ('Report: '+$reportFile)
if($Phase -eq 'B'){Write-Host '旧版Aの結果があれば、03_Compare_AB.cmd で比較できます。'}
