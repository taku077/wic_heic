#requires -Version 5.1
# Read-only analysis of a WIC numeric trace and a passive shell monitor.
# No image paths, pixels, metadata, names or app/window titles in output.
param(
  [Parameter(Mandatory=$true)][string]$TracePath,
  [Parameter(Mandatory=$true)][string]$ShellLogPath,
  [string]$OutputDirectory = '',
  [switch]$CiSelfTest
)
$ErrorActionPreference='Stop'
if($CiSelfTest -and $env:GITHUB_ACTIONS -ne 'true'){throw 'CI switch forbidden outside CI.'}
foreach($p in @($TracePath,$ShellLogPath)){
 if(-not [IO.File]::Exists($p)){throw 'Diagnostic input not found.'}
}
if((Get-Item -LiteralPath $TracePath).Length -gt 2097152){throw 'WIC trace over 2 MiB.'}
if((Get-Item -LiteralPath $ShellLogPath).Length -gt 1048576){throw 'Shell log over 1 MiB.'}
if([string]::IsNullOrWhiteSpace($OutputDirectory)){
 $OutputDirectory=Join-Path $PSScriptRoot '診断結果'
}
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$report=New-Object 'System.Collections.Generic.List[string]'
$report.Add('HEIC_SAVE_WIC_CORRELATION_V1')
$report.Add('WARNING=Only observable WIC trace events can be reported; absent events are inconclusive.')
$report.Add('WARNING=EncoderCommitExit success does not prove final Photos file replacement succeeded.')
$report.Add('WARNING=No photo filename, path, EXIF, pixels, titles or content were read.')
$all=New-Object 'System.Collections.Generic.List[object]'
foreach($line in [IO.File]::ReadAllLines($TracePath,[Text.Encoding]::UTF8)){
 if(-not $line.StartsWith('WIC_HEIC_TRACE v1 ')){continue}
 $pidm=[regex]::Match($line,'\bpid=(\d+)\b')
 $evm=[regex]::Match($line,'\bevent=([A-Za-z][A-Za-z0-9]*)\b')
 $opm=[regex]::Match($line,'\boperation=(\d+)\b')
 $hrm=[regex]::Match($line,'\bhr=0x([0-9a-fA-F]{8})\b')
 $tm=[regex]::Match($line,'\butc_filetime=(\d+)\b')
 $duration=[regex]::Match($line,'\belapsed_ms=(\d+)\b')
 $src=[regex]::Match($line,'\bsource=([A-Za-z0-9-]+)\b')
 if(-not($pidm.Success -and $evm.Success)){continue}
 $all.Add([pscustomobject]@{
   pid=[int]$pidm.Groups[1].Value
   event=$evm.Groups[1].Value
   op=$(if($opm.Success){[long]$opm.Groups[1].Value}else{[long]0})
   hr=$(if($hrm.Success){$hrm.Groups[1].Value.ToUpperInvariant()}else{'UNKNOWN'})
   utc=$(if($tm.Success){[long]$tm.Groups[1].Value}else{[long]0})
   elapsed=$(if($duration.Success){[long]$duration.Groups[1].Value}else{[long]0})
   source=$(if($src.Success){$src.Groups[1].Value}else{'unknown'})
 })
}
$report.Add('WIC_EVENT_COUNT='+$all.Count)
$commits=@($all | Where-Object {$_.event -match '^EncoderCommit'})
$report.Add('WIC_COMMIT_EVENT_COUNT='+$commits.Count)
$groups=@($commits | Where-Object {$_.op -gt 0} | Group-Object -Property { '{0}:{1}' -f $_.pid,$_.op })
$commitEnter=0;$commitOk=0;$commitBad=0;$commitIncomplete=0;$lossless=0;$recompressed=0;$rejected=0
$results=@()
foreach($group in $groups){
 $events=@($group.Group)
 $begin=@($events | Where-Object {$_.event -eq 'EncoderCommitEnter'})
 $end=@($events | Where-Object {$_.event -eq 'EncoderCommitExit'})
 $ln=@($events | Where-Object {$_.event -eq 'EncoderCommitLossless'})
 $rn=@($events | Where-Object {$_.event -eq 'EncoderCommitRecompressed'})
 $rej=@($events | Where-Object {$_.event -eq 'EncoderCommitRejected'})
 $over=@($events | Where-Object {$_.event -eq 'EncoderCommitOverlap'})
 $commitEnter+=$begin.Count;$lossless+=$ln.Count;$recompressed+=$rn.Count;$rejected+=$rej.Count
 $lastEnd=$end | Select-Object -Last 1
 $outcome='INCOMPLETE'
 $hr='UNKNOWN';$ms=0
 if($lastEnd){
  $hr=$lastEnd.hr;$ms=$lastEnd.elapsed
  if($hr -eq '00000000'){$outcome='WIC_COMMIT_OK';$commitOk++}
  else {$outcome='WIC_COMMIT_FAILED';$commitBad++}
 }else{$commitIncomplete++}
 $route='UNKNOWN'
 if($rej.Count){$route='REJECTED'}
 elseif($ln.Count){$route='LOSSLESS'}
 elseif($rn.Count){$route='RECOMPRESSED'}
 $time=@($events | Where-Object {$_.utc -gt 0} | Sort-Object utc | Select-Object -First 1)
 $ft=$(if($time.Count){$time[0].utc}else{[long]0})
 $results+= [pscustomobject]@{
  pid=$events[0].pid;op=$events[0].op;utc=$ft;route=$route;hr=$hr;result=$outcome
  elapsed=$ms;enter=$begin.Count;exit=$end.Count;rejected=$rej.Count;overlap=$over.Count
 }
}
$report.Add(('WIC_COMMIT_SUMMARY entered={0} succeeded={1} failed={2} incomplete={3} lossless={4} recompressed={5} rejected={6}' -f
 $commitEnter,$commitOk,$commitBad,$commitIncomplete,$lossless,$recompressed,$rejected))
foreach($one in ($results | Sort-Object utc,pid,op)){
 $stamp='NOT_RECORDED'
 if($one.utc -ge 116444736000000000){
  try {$stamp=[DateTime]::FromFileTimeUtc($one.utc).ToString('yyyy-MM-ddTHH:mm:ss.fffZ')}
  catch {$stamp='INVALID_FILETIME'}
 }
 $report.Add(('COMMIT utc={0} pid={1} op={2} outcome={3} hr=0x{4} route={5} elapsed_ms={6} enter={7} exit={8} rejected={9} overlap={10}' -f
 $stamp,$one.pid,$one.op,$one.result,$one.hr,$one.route,$one.elapsed,$one.enter,$one.exit,$one.rejected,$one.overlap))
}
$shellLines=@([IO.File]::ReadAllLines($ShellLogPath,[Text.Encoding]::UTF8))
foreach($name in @('GUI_SHOWN','PHOTOS_LAUNCH','SHELL_REGISTRATION','FS_TARGET_EVENTS','SHELL_TARGET_EVENTS','SHELL_FOLDER_EVENTS','FS_WATCHER_ERRORS','INPUT_SHA256_CHANGED')){
 $value=@($shellLines | Where-Object {$_ -match ('^'+$name+'=')} | Select-Object -First 1)
 if($value.Count){$report.Add('SHELL_'+$value[0])}
}
$fsEntries=@($shellLines | Where-Object {$_ -match 'channel=FS '}).Count
$shellEntries=@($shellLines | Where-Object {$_ -match 'channel=SHELL '}).Count
$report.Add("SHELL_LOG_FS_EVENT_LINES=$fsEntries")
$report.Add("SHELL_LOG_SHELL_EVENT_LINES=$shellEntries")
if($all.Count -eq 0){
 $report.Add('ASSESSMENT=NO_WIC_TRACE_EVENTS; installed codec trace support/access/routing unknown.')
}elseif($commitEnter -eq 0){
 $report.Add('ASSESSMENT=WIC_EVENTS_SEEN_BUT_NO_ENCODER_COMMIT; cannot attribute save to this codec.')
}elseif($commitBad -gt 0){
 $report.Add('ASSESSMENT=WIC_ENCODER_COMMIT_FAILED; inspect HRESULT and operation for root-cause candidates.')
}elseif($commitIncomplete -gt 0){
 $report.Add('ASSESSMENT=INCOMPLETE_COMMIT_BOUNDARIES; do not infer success or failure.')
}else{
 $report.Add('ASSESSMENT=WIC_COMMIT_SUCCESS_OBSERVED; Photos file replacement remains independent.')
}
$report.Add('LIMIT=PID is not verified as Windows Photos process.')
$report.Add('LIMIT=Trace entries can be dropped by concurrency, cap, access rights or codec version.')
$file=Join-Path $OutputDirectory ('HEIC_Save_WIC_Report_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+[guid]::NewGuid().ToString('N').Substring(0,6)+'.txt')
[IO.File]::WriteAllLines($file,$report.ToArray(),(New-Object [Text.UTF8Encoding]($true)))
if($CiSelfTest){
 if($commitEnter -ne 3 -or $commitOk -ne 1 -or $commitBad -ne 1 -or $commitIncomplete -ne 1 -or $lossless -ne 1 -or $recompressed -ne 1 -or $rejected -ne 1){
  throw 'Unexpected test fixture grouping or counts.'
 }
 if($fsEntries -lt 1 -or $shellEntries -lt 1){throw 'Missing synthetic Shell events.'}
}
Write-Output ('HEIC_COMMIT_REPORT='+$file)
$report | ForEach-Object {Write-Host $_}
