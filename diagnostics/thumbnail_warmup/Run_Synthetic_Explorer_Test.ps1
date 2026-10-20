#requires -version 5.1
# Synthetic HEIC only; never installs WIC, clears the Shell cache or edits photos.
[CmdletBinding()]
param(
 [ValidateSet('Run','SelfTest')][string]$Mode='Run',
 [string]$FixturePath
)
$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot 'Explorer_HEIC_Warmup.ps1'
if(-not $FixturePath) {$FixturePath=Join-Path $PSScriptRoot 'fixture-irot3-exif6-noaux.heic'}
if(-not (Test-Path -LiteralPath $helper -PathType Leaf) -or
   -not (Test-Path -LiteralPath $FixturePath -PathType Leaf)) {
 throw 'Synthetic-only test: fixture or warmup helper missing'
}
# .NET SHA-256 is available even on Windows PowerShell installations
# where the Get-FileHash cmdlet is absent from the session.
function Get-FileSha256 {
 param([string]$Path)
 $sha=[System.Security.Cryptography.SHA256]::Create()
 $stream=[System.IO.File]::OpenRead($Path)
 try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','') }
 finally { $stream.Dispose(); $sha.Dispose() }
}
$sourceHash=Get-FileSha256 -Path $FixturePath
if($sourceHash -ne 'D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95') {
 throw 'Artificial fixture SHA256 mismatch: refusing to operate on an unknown image'
}
$root=Join-Path $PSScriptRoot 'Results'
New-Item -ItemType Directory -Force -Path $root | Out-Null
$test=Join-Path $root ('ExplorerTrial_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'_'+$PID)
$folder=Join-Path $test 'Synthetic'
New-Item -ItemType Directory -Force -Path $folder | Out-Null
$stdout=Join-Path $test 'watch_stdout.txt'
$stderr=Join-Path $test 'watch_stderr.txt'
$summary=Join-Path $test 'summary.txt'
$zip=$test+'.zip'
$rows=New-Object System.Collections.Generic.List[string]
$watch=$null
$before=@{}
$opened=@()
try {
 for($i=0;$i -lt 10;$i++) {
   Copy-Item -LiteralPath $FixturePath -Destination (Join-Path $folder ('test_'+$i.ToString('00')+'.heic'))
 }
 $rows.Add('MODE='+$Mode)
 $rows.Add('OS='+[Environment]::OSVersion.Version.ToString())
 $rows.Add('ARTIFICIAL_IMAGES=10')
 $rows.Add('SOURCE_SHA256='+$sourceHash)
 if($Mode -eq 'SelfTest') {
   $first=@(& $helper -Mode Folder -Folder $folder -MaxFiles 10 -DurationSeconds 30)
   if(-not ($first -match 'attempted=10 cached=0 generated=10 failed=0')) {
     throw ('First-run Shell cache failed: '+($first -join ' '))
   }
   $second=@(& $helper -Mode Folder -Folder $folder -MaxFiles 10 -DurationSeconds 30)
   if(-not ($second -match 'attempted=10 cached=10 generated=0 failed=0')) {
     throw ('Shell revisit test failed: '+($second -join ' '))
   }
   $rows.Add('CACHE_GENERATE_AND_REVISIT=PASS')
 } else {
   # Enumerate IDs first; do not close any Explorer window that predates test.
   $shell=New-Object -ComObject Shell.Application
   try {
     foreach($w in @($shell.Windows())) {try {$before[[string]$w.HWND]=$true}catch{}}
   } finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell) }
   $exe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
   $args='-NoProfile -ExecutionPolicy Bypass -File "'+$helper+'" -Mode Watch -DurationSeconds 20 -MaxFiles 10 -ExitWhenDone'
   $watch=Start-Process -FilePath $exe -ArgumentList $args -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
   Start-Sleep -Milliseconds 1500
   Start-Process -FilePath (Join-Path $env:SystemRoot 'explorer.exe') -ArgumentList ('"'+$folder+'"') | Out-Null
   $seen=$false
   $until=(Get-Date).AddSeconds(8)
   while((Get-Date) -lt $until -and -not $seen) {
     $app=New-Object -ComObject Shell.Application
     try {
       foreach($w in @($app.Windows())) {
         try {
           $path=[string]$w.Document.Folder.Self.Path
           if($path.TrimEnd('\') -ieq $folder.TrimEnd('\')) {
             $seen=$true
             if(-not $before.ContainsKey([string]$w.HWND)) {$opened+= [string]$w.HWND}
           }
         }catch{}
       }
     } finally { [void][Runtime.InteropServices.Marshal]::ReleaseComObject($app) }
     if(-not $seen) {Start-Sleep -Milliseconds 500}
   }
   $rows.Add('EXPLORER_FOLDER_DETECTED='+$seen)
   $exitSignaled=$watch.WaitForExit(32000)
   $watch.Refresh()
   $processExited=($exitSignaled -and $watch.HasExited)
   $rows.Add('WATCH_AUTO_EXIT='+$processExited)
   if($processExited) {
     $watchCode=$watch.ExitCode
     $rows.Add('WATCH_EXIT_CODE='+$(if($null -eq $watchCode){'UNAVAILABLE'}else{$watchCode}))
   } else {
     # Distinguish a completed script that fails to exit from a still-running
     # script; never call the former a clean auto-exit.
     $rows.Add('WATCH_EXIT_CODE=PROCESS_TIMEOUT')
     Stop-Process -Id $watch.Id -Force -ErrorAction SilentlyContinue
   }
   if(Test-Path -LiteralPath $stdout) {
     $log=Get-Content -LiteralPath $stdout -Raw
     $hit=[regex]::Match($log,'WARMUP_WATCH_COMPLETED folders=(\d+) generated=(\d+) reused=(\d+) errors=(\d+)')
     if($hit.Success) {
       $rows.Add('WATCH_FOLDERS='+$hit.Groups[1].Value)
       $rows.Add('WATCH_GENERATED='+$hit.Groups[2].Value)
       $rows.Add('WATCH_REUSED='+$hit.Groups[3].Value)
       $rows.Add('WATCH_ERRORS='+$hit.Groups[4].Value)
     }else{$rows.Add('WATCH_SUMMARY_MISSING=true')}
   }
   $completed=($null -ne $hit -and $hit.Success)
   $rows.Add('WATCH_COMPLETION_MARKER='+$completed)
   $rows.Add('EXPLORER_VISIBLE_THUMBNAILS_VERIFIED=false')
 }
 $ok=$true
 $copies=@(Get-ChildItem -LiteralPath $folder -File)
 if($copies.Count -ne 10){$ok=$false}
 foreach($f in $copies) {
   if((Get-FileSha256 -Path $f.FullName) -ne $sourceHash){$ok=$false}
 }
 $rows.Add('COPIES_HASH_UNCHANGED='+$ok)
 $rows.Add('FIXTURE_HASH_UNCHANGED='+((Get-FileSha256 -Path $FixturePath) -eq $sourceHash))
 if(-not $ok){throw 'Synthetic HEIC file unexpectedly changed'}
 if($Mode -eq 'SelfTest') {$rows.Add('ONECLICK_SELFTEST_PASSED=true')}
} catch {
 $rows.Add('TEST_FAILED='+$_.Exception.Message)
} finally {
 if($null -ne $watch) {
   try{$watch.Refresh();if(-not $watch.HasExited){Stop-Process -Id $watch.Id -Force}}catch{}
 }
 # Only close genuinely new window displaying our own artificial directory.
 if($opened.Count -gt 0){
   $app=New-Object -ComObject Shell.Application
   try {
     foreach($w in @($app.Windows())) {
       try {
         if(($opened -contains [string]$w.HWND) -and
            ([string]$w.Document.Folder.Self.Path).TrimEnd('\') -ieq $folder.TrimEnd('\')) {
           $w.Quit()
         }
       }catch{}
     }
   }finally{[void][Runtime.InteropServices.Marshal]::ReleaseComObject($app)}
 }
 # Delete only this session's copies. Keep diagnostic text and ZIP.
 Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue
 $rows.Add('NO_CODEC_INSTALL_NO_CACHE_RESET=true')
 [IO.File]::WriteAllLines($summary,$rows.ToArray(),([Text.UTF8Encoding]::new($false)))
 $includes=@($summary)
 if(Test-Path -LiteralPath $stdout){$includes+=$stdout}
 if(Test-Path -LiteralPath $stderr){$includes+=$stderr}
 Compress-Archive -LiteralPath $includes -DestinationPath $zip -CompressionLevel Optimal
 Write-Output ('TEST_REPORT='+$zip)
 Write-Output ('TEST_STATUS='+$(if($rows -match '^TEST_FAILED='){'FAILED'}else{'RECORDED'}))
 if($rows -match '^TEST_FAILED=') { throw 'Synthetic Explorer test incomplete; see the generated ZIP report' }
}
