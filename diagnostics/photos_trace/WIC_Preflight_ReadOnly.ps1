# Read-only Windows 10 WIC codec inventory. Does not install, unregister or modify anything.
# PowerShell 5.1. Output is local JSON adjacent to this script; contains system DLL paths.
$ErrorActionPreference='Stop'
$clsids=[ordered]@{
  Decoder='{6215667c-74ca-4076-a769-3f834c3f25f5}'
  Encoder='{0e8ab687-73a5-455b-8c19-b93ca9ac0eeb}'
}
$registrations=New-Object 'System.Collections.Generic.List[object]'
foreach($hive in @('ClassesRoot','LocalMachine','CurrentUser')) {
  foreach($view in @('Registry64','Registry32')) {
    $base=[Microsoft.Win32.RegistryKey]::OpenBaseKey(
      [Microsoft.Win32.RegistryHive]$hive,[Microsoft.Win32.RegistryView]$view)
    try {
      foreach($type in @('Decoder','Encoder')) {
        $relative='SOFTWARE\Classes\CLSID\'+$clsids[$type]+'\InprocServer32'
        if($hive -eq 'ClassesRoot') {
          $relative='CLSID\'+$clsids[$type]+'\InprocServer32'
        }
        $key=$base.OpenSubKey($relative,$false)
        $original=$null; $fileHash=$null
        if($null -ne $key) {
          try {$original=[string]$key.GetValue('')} finally {$key.Close()}
          if(-not [string]::IsNullOrWhiteSpace($original)) {
            $expanded=[Environment]::ExpandEnvironmentVariables($original).Trim('"')
            if(Test-Path -LiteralPath $expanded -PathType Leaf) {
              try { $fileHash=(Get-FileHash -LiteralPath $expanded -Algorithm SHA256).Hash }
              catch { $fileHash='HASH_UNAVAILABLE' }
            }
          }
        }
        $registrations.Add([pscustomobject][ordered]@{
          Hive=$hive; View=$view; Codec=$type; CLSID=$clsids[$type]
          InprocServer32=$original; SHA256=$fileHash
        })
      }
    } finally {$base.Close()}
  }
}
$msiMatches=New-Object 'System.Collections.Generic.List[object]'
foreach($basePath in @(
  'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
  'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
  'Registry::HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
)) {
  foreach($entry in @(Get-ItemProperty -Path $basePath -ErrorAction SilentlyContinue)) {
    if([string]$entry.DisplayName -match '(?i)^Wic.?Heic(?:\s|$)') {
      $msiMatches.Add([pscustomobject][ordered]@{
        Name=[string]$entry.DisplayName
        Version=[string]$entry.DisplayVersion
        ProductKey=[string]$entry.PSChildName
      })
    }
  }
}
$os=Get-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction SilentlyContinue
$photos=@()
try {
  if(Get-Command Get-AppxPackage -ErrorAction SilentlyContinue) {
    $photos=@(Get-AppxPackage -Name Microsoft.Windows.Photos -ErrorAction Stop |
      ForEach-Object { [pscustomobject]@{Name=[string]$_.Name;Version=[string]$_.Version} })
  }
} catch { $photos=@() }
$data=[ordered]@{
  Schema=1
  CollectedAtUtc=[DateTime]::UtcNow.ToString('o')
  Windows=[ordered]@{
    ProductName=[string]$os.ProductName
    CurrentBuild=[string]$os.CurrentBuild
    DisplayVersion=[string]$os.DisplayVersion
  }
  Photos=$photos
  Registrations=@($registrations.ToArray())
  WicHeicMsi=@($msiMatches.ToArray())
}
$path=Join-Path $PSScriptRoot ('WIC_Preflight_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'_'+$PID+'.json')
$payload=($data|ConvertTo-Json -Depth 6)
$utf8=New-Object System.Text.UTF8Encoding($false)
$bytes=$utf8.GetBytes($payload)
$file=[System.IO.File]::Open($path,[System.IO.FileMode]::CreateNew,[System.IO.FileAccess]::Write,[System.IO.FileShare]::Read)
try { $file.Write($bytes,0,$bytes.Length) } finally { $file.Dispose() }
Write-Host "Read-only inventory saved: $path"
Write-Output $path
