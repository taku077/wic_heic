# Compare REAL Windows 10 Microsoft Photos GUI A/B, not Windows Server CI.
# Read-only on saved, sanitized synthetic-image result files.
[CmdletBinding()]
param([string]$Root=$PSScriptRoot,[switch]$NoWrite)
$ErrorActionPreference='Stop'
$root=(Get-Item -LiteralPath $Root -ErrorAction Stop).FullName
$folder=Join-Path $root 'Photos_AB_Results'
$oldFile=Join-Path $folder 'Photos_AB_A.json'
$newFile=Join-Path $folder 'Photos_AB_B.json'
foreach($f in @($oldFile,$newFile)){
  if(-not (Test-Path -LiteralPath $f -PathType Leaf)){throw ('Missing A/B capture: '+[IO.Path]::GetFileName($f))}
  if((Get-Item -LiteralPath $f).Length -gt 65536){throw 'Unexpectedly large diagnostic report'}
}
$a=Get-Content -LiteralPath $oldFile -Raw -Encoding UTF8|ConvertFrom-Json
$b=Get-Content -LiteralPath $newFile -Raw -Encoding UTF8|ConvertFrom-Json
if($a.Schema -ne 1 -or $b.Schema -ne 1 -or $a.Phase -cne 'A' -or $b.Phase -cne 'B'){
  throw 'A/B report identity or schema invalid'
}
$expected='D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95'
if($a.SyntheticReferenceSha256 -cne $expected -or $b.SyntheticReferenceSha256 -cne $expected){
  throw 'A/B did not use the identical verified public HEIC fixture'
}
function Say([string]$code){
  switch($code){
    'y' {'はい'} 'n' {'いいえ'} 's' {'成功'} 'f' {'失敗'} 'x' {'未実施'}
    default {'不明'}
  }
}
function LoadedHashes($record) {
  $hashes=@($record.PhotosModules|ForEach-Object {$_.WicHeicDllSha256}|
    Where-Object {$_ -and $_ -ne 'UNREADABLE'}|Sort-Object -Unique)
  if(-not $hashes.Count){return '未取得'}
  return ($hashes -join ',')
}
function ProbeSummary($record) {
  $rows=@($record.CodecProbeEvents|Where-Object {$_ -match '^HEVC_SYNTHETIC_'})
  return @($rows|Sort-Object -Unique)
}
$results=New-Object 'System.Collections.Generic.List[string]'
$results.Add('Windows 10 標準フォト / HEIC A-B 実機比較')
$results.Add('A=旧1.1.18 / B=研究版（手動の申告。導入状態はDLLハッシュで別途照合）')
$results.Add('----------------------------------------------')
$rows=@(
  @('HEVC警告',$a.GuiObservation.HevcWarning,$b.GuiObservation.HevcWarning),
  @('編集ボタン使用可',$a.GuiObservation.EditUsable,$b.GuiObservation.EditUsable),
  @('回転ボタン使用可',$a.GuiObservation.RotateUsable,$b.GuiObservation.RotateUsable),
  @('回転保存',$a.GuiObservation.SaveResult,$b.GuiObservation.SaveResult),
  @('表示の向き・縦横比崩れ',$a.GuiObservation.VisibleDistortion,$b.GuiObservation.VisibleDistortion)
)
$results.Add('【実際のPhotos画面】')
foreach($item in $rows){
  $results.Add(('{0}: A={1} / B={2}' -f $item[0],(Say ([string]$item[1])),(Say ([string]$item[2]))))
}
$results.Add('【条件が同じか】')
$results.Add(('WindowsBuild A={0} B={1}' -f $a.WindowsBuild,$b.WindowsBuild))
$allPackages=@($a.Packages.PSObject.Properties.Name + $b.Packages.PSObject.Properties.Name|Sort-Object -Unique)
foreach($name in $allPackages){
  $left=@($a.Packages.$name) -join ','
  $right=@($b.Packages.$name) -join ','
  $results.Add(('AppX {0}: A={1} B={2}{3}' -f $name,$left,$right,
    $(if($left -cne $right){' [CHANGED]'}else{''})))
}
$oldHashes=LoadedHashes $a
$newHashes=LoadedHashes $b
$results.Add(('Photosプロセス内wic_heic.dll SHA256 A={0} B={1}' -f $oldHashes,$newHashes))
$results.Add(('Photos検出プロセス数 A={0} B={1}' -f $a.PhotosProcessCount,$b.PhotosProcessCount))
$results.Add('【固定人工HEICに対するWIC実行】')
$results.Add(('WIC診断完了 A={0} B={1}' -f $a.CodecProbeStatus,$b.CodecProbeStatus))
foreach($pair in @(@('A',$a),@('B',$b))){
  $events=ProbeSummary $pair[1]
  foreach($evt in $events){
    if($evt -match 'stage=(create_decoder|decoder_clsid|copy_pixels|write_source|encoder_commit|readback|rgb24)' -or
       $evt -match 'stage=(create|create_frame|initialize|frame_initialize)'){
      $results.Add(($pair[0]+' '+$evt))
    }
  }
}
$before=ProbeSummary $a
$after=ProbeSummary $b
$delta=@(Compare-Object -ReferenceObject $before -DifferenceObject $after |
  Select-Object -First 25)
$results.Add(('WIC段階出力差分数（最大25表示）='+$delta.Count))
foreach($diff in $delta){$results.Add(('  '+$diff.SideIndicator+' '+$diff.InputObject))}
$results.Add('【作業コピーの変化】')
foreach($pair in @(@('A',$a),@('B',$b))){
  $item=$pair[1].WorkingCopy
  $state=if(-not $item.Exists){'作業コピーなし'}
     elseif($item.Sha256 -ceq $expected){'原人工HEICと同一'}
     else{'作業コピーに変化あり（成功やメタ保存の証明ではない）'}
  $results.Add(($pair[0]+' '+$state+' / '+$item.Bytes+' bytes'))
}
$results.Add('【原因切り分け：次に調べる範囲】')
$uiChanged=([string]$a.GuiObservation.HevcWarning -cne
            [string]$b.GuiObservation.HevcWarning -and
            $a.GuiObservation.HevcWarning -ne '?' -and
            $b.GuiObservation.HevcWarning -ne '?')
$packageChanged=$false
foreach($n in $allPackages){
  if((@($a.Packages.$n) -join ',') -cne (@($b.Packages.$n) -join ',')){$packageChanged=$true}
}
if($oldHashes -ceq $newHashes -or
   $oldHashes -ceq '未取得' -or $newHashes -ceq '未取得'){
  $results.Add('重要：旧版と研究版のDLL切替がハッシュから確認できない。GUI差分からWIC原因を特定しない。')
}
if($uiChanged -and -not $packageChanged -and
   $oldHashes -cne $newHashes -and
   $oldHashes -cne '未取得' -and $newHashes -cne '未取得'){
  $results.Add('最優先：Photos/HEIF拡張のバージョンは同じでWIC DLLだけが異なり警告状態が変化。')
  $results.Add('Photos内のWIC呼び出し分岐を実測する次の限定診断が必要。復号速度・サムネイル・一般的な追加修正はしない。')
}elseif($packageChanged){
  $results.Add('重要：アプリまたは拡張のバージョンが同時に変化。WIC単独原因のA/Bと判定できない。')
}elseif($b.GuiObservation.HevcWarning -eq 'n' -and $b.GuiObservation.SaveResult -eq 'f'){
  $results.Add('警告は消えたが保存が失敗。PhotosのWIC保存呼出し経路に対象を絞る。')
}else{
  $results.Add('警告と編集ボタンの実機状態に基づき、未特定のWIC/Photos分岐だけを次に調べる。')
}
$results.Add('注意：画面の回答はユーザーの実操作結果。WIC診断は実際のPhotos COM呼び出しトレースではない。')
$results.Add('注意：実写真・ファイルパス・EXIF値・原画像は収集していない。')
$lines=$results.ToArray()
$lines|ForEach-Object {Write-Host $_}
if(-not $NoWrite){
  $output=Join-Path $folder 'Photos_AB_Comparison.txt'
  if(Test-Path -LiteralPath $output){throw 'Comparison already exists; refuse overwriting'}
  $bytes=[Text.Encoding]::UTF8.GetBytes(($lines -join [Environment]::NewLine)+[Environment]::NewLine)
  $file=[IO.File]::Open($output,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try{$file.Write($bytes,0,$bytes.Length)}finally{$file.Dispose()}
  Write-Host 'Comparison saved in Photos_AB_Results\Photos_AB_Comparison.txt'
}
