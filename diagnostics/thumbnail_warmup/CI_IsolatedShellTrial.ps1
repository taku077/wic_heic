#requires -version 5.1
<#
CI-ONLY. Registers an isolated synthetic .wictrial file association for the
current runner user, checks Shell IThumbnailCache -> standalone COM DLL, and
rolls back all owned HKCU entries. MUST NOT be run on a user's PC.
Never modifies .heic, .heif, real-photo files or their associations.
#>
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$TrialDllPath,
 [Parameter(Mandatory=$true)][string]$FixturePath
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$providerId='{94DA5CB1-F0E4-42C2-B5D8-B1F3CE6D782B}'
$handlerId='{E357FCCD-A995-4576-B01F-234630154E96}'
$progid='WicHeicSyntheticTrial.1'
$extension='.wictrial'
$subkeys=@(
 'Software\Classes\CLSID\'+$providerId,
 'Software\Classes\'+$progid,
 'Software\Classes\'+$extension,
 'Software\Classes\SystemFileAssociations\'+$extension,
 'Software\WicHeicSyntheticTrial\'+$providerId
)
$registry=[Microsoft.Win32.Registry]::CurrentUser
function Open-NewRegistryKey([string]$subkey) {
 $key=$registry.CreateSubKey($subkey)
 if($null -eq $key){throw ('Cannot create isolated registry key: '+$subkey)}
 return $key
}
function Sha256([string]$path) {
 return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
}
$src=(Resolve-Path -LiteralPath $FixturePath).Path
$dll=(Resolve-Path -LiteralPath $TrialDllPath).Path
$hash=Sha256 $src
# Only touch an extension and CLSID whose complete registrations did not exist.
foreach($subkey in $subkeys) {
 $key=$registry.OpenSubKey($subkey)
 if($key) { $key.Close();throw ('Refusing existing registry key: '+$subkey) }
}
foreach($check in @('CLSID\'+$providerId,$extension,$progid)) {
 $key=[Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey($check)
 if($key) {$key.Close(); throw ('Refusing active global class: '+$check)}
}
$work=Join-Path $env:RUNNER_TEMP ('wic_shell_trial_'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -ErrorAction Stop | Out-Null
$file=Join-Path $work ('synthetic'+$extension)
Copy-Item -LiteralPath $src -Destination $file
if((Sha256 $file) -ne $hash){throw 'Synthetic fixture changed during copy'}

# All native calls use documented Windows interfaces, never enumerate private
# images and never write photo data. The exact test extension is newly created.
$native=@'
using System;
using System.Runtime.InteropServices;
namespace TrialShell {
 [ComImport,Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE"),
  InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
 public interface IShellItem {
  [PreserveSig]
  int BindToHandler(IntPtr context,ref Guid handler,ref Guid interfaceId,
                    out IntPtr instance);
 }
 [ComImport,Guid("091162A4-BC96-411F-AAE8-C5122CD03363"),
  InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
 public interface ISharedBitmap {}
 [StructLayout(LayoutKind.Sequential)]
 public struct ThumbId { public Guid Value; }
 [ComImport,Guid("F676C15D-596A-4CE2-8234-33996F445DB1"),
  InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
 public interface IThumbnailCache {
  [PreserveSig]
  int GetThumbnail(IShellItem item,uint size,uint flags,
   [MarshalAs(UnmanagedType.Interface)]out ISharedBitmap bitmap,
   out uint cacheFlags,out ThumbId id);
 }
 public sealed class ThumbnailStatus {
   public int HResult;
   public bool HasBitmap;
   public uint CacheFlags;
 }
 public static class Shell {
  public static int ProbeComActivation(string guid) {
    object obj=null;
    try {
      obj=Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid(guid),true));
      return 0;
    } catch(COMException e) {
      return e.ErrorCode;
    } finally {
      if(obj!=null && Marshal.IsComObject(obj))Marshal.ReleaseComObject(obj);
    }
  }
  // Keep COM invocation and out-interface marshalling entirely inside C#.
  // PowerShell treats imported interfaces as System.__ComObject and cannot
  // reliably dispatch GetThumbnail or cast to a ComImport interface.
  public static int BindThumbnailHandler(string file) {
    Guid itemIID=new Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE");
    Guid bindId=new Guid("7B2E650A-8E20-4F4A-B09E-6597AFC72FB0");
    Guid thumbIID=new Guid("E357FCCD-A995-4576-B01F-234630154E96");
    IShellItem item=null;
    IntPtr instance=IntPtr.Zero;
    try {
      int hr=SHCreateItemFromParsingName(file,IntPtr.Zero,ref itemIID,out item);
      if(hr<0)return hr;
      return item.BindToHandler(IntPtr.Zero,ref bindId,ref thumbIID,out instance);
    } finally {
      if(instance!=IntPtr.Zero) Marshal.Release(instance);
      if(item!=null)Marshal.ReleaseComObject(item);
    }
  }
  public static ThumbnailStatus GetSyntheticThumbnail(string file,bool cacheOnly) {
    Guid clsid=new Guid("50EF4544-AC9F-4A8E-B21B-8A26180DB13F");
    Guid iid=new Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE");
    IThumbnailCache cache=null;
    IShellItem item=null;
    ISharedBitmap bitmap=null;
    try {
      cache=(IThumbnailCache)Activator.CreateInstance(Type.GetTypeFromCLSID(clsid,true));
      int hr=SHCreateItemFromParsingName(file,IntPtr.Zero,ref iid,out item);
      if(hr<0) return new ThumbnailStatus{HResult=hr};
      uint flags=0;
      ThumbId thumbId;
      hr=cache.GetThumbnail(item,256,cacheOnly?1u:0u,
                            out bitmap,out flags,out thumbId);
      return new ThumbnailStatus{HResult=hr,HasBitmap=(bitmap!=null),CacheFlags=flags};
    } finally {
      if(bitmap!=null)Marshal.ReleaseComObject(bitmap);
      if(item!=null)Marshal.ReleaseComObject(item);
      if(cache!=null)Marshal.ReleaseComObject(cache);
    }
  }
  [DllImport("shell32.dll",CharSet=CharSet.Unicode)]
  public static extern int SHCreateItemFromParsingName(
    string path,IntPtr bind,ref Guid iid,
    [MarshalAs(UnmanagedType.Interface)]out IShellItem item);
  [DllImport("shell32.dll")]
  public static extern void SHChangeNotify(uint evt,uint flags,IntPtr a,IntPtr b);
 }
}
'@
Add-Type -TypeDefinition $native -Language CSharp -ErrorAction Stop
function Release-Com($item) {
 if($null -ne $item -and [Runtime.InteropServices.Marshal]::IsComObject($item)) {
   [void][Runtime.InteropServices.Marshal]::ReleaseComObject($item)
 }
}
$registered=@()
$registeredOk=$false
try {
 # Shell host isolation stays ON: no DisableProcessIsolation key/value.
 $key=Open-NewRegistryKey ('Software\Classes\CLSID\'+$providerId+'\InprocServer32')
 $key.SetValue('',$dll,[Microsoft.Win32.RegistryValueKind]::String)
 $key.SetValue('ThreadingModel','Both',[Microsoft.Win32.RegistryValueKind]::String)
 $key.Close()
 $registered+=('Software\Classes\CLSID\'+$providerId)
 $key=Open-NewRegistryKey ('Software\Classes\'+$progid+'\ShellEx\'+$handlerId)
 $key.SetValue('',$providerId,[Microsoft.Win32.RegistryValueKind]::String)
 $key.Close()
 $registered+=('Software\Classes\'+$progid)
 $key=Open-NewRegistryKey ('Software\Classes\'+$extension)
 $key.SetValue('',$progid,[Microsoft.Win32.RegistryValueKind]::String)
 $key.SetValue('PerceivedType','image',[Microsoft.Win32.RegistryValueKind]::String)
 $key.Close()
 $registered+=('Software\Classes\'+$extension)
 # Apply both the extension and SystemFileAssociations Shell handler paths;
 # these are unique .wictrial entries, never the actual HEIC association.
 $key=Open-NewRegistryKey ('Software\Classes\'+$extension+'\ShellEx\'+$handlerId)
 $key.SetValue('',$providerId,[Microsoft.Win32.RegistryValueKind]::String)
 $key.Close()
 $key=Open-NewRegistryKey ('Software\Classes\SystemFileAssociations\'+$extension+'\ShellEx\'+$handlerId)
 $key.SetValue('',$providerId,[Microsoft.Win32.RegistryValueKind]::String)
 $key.Close()
 $registered+=('Software\Classes\SystemFileAssociations\'+$extension)
 $key=Open-NewRegistryKey ('Software\WicHeicSyntheticTrial\'+$providerId)
 $key.SetValue('Enable',1,[Microsoft.Win32.RegistryValueKind]::DWord)
 $key.SetValue('Calls',0,[Microsoft.Win32.RegistryValueKind]::DWord)
 $key.SetValue('Attempts',0,[Microsoft.Win32.RegistryValueKind]::DWord)
 $key.SetValue('Activated',0,[Microsoft.Win32.RegistryValueKind]::DWord)
 $key.Close()
 $registered+=('Software\WicHeicSyntheticTrial\'+$providerId)
 [TrialShell.Shell]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero)
 # Verify the COM class can be instantiated directly before asking the
 # Shell's isolated thumbnail process to activate it.
 $directHr=[TrialShell.Shell]::ProbeComActivation($providerId)
 Write-Host ('SHELL_TRIAL_DIRECT_COM_HRESULT=0x'+$directHr.ToString('X8'))
 if($directHr -ne 0){throw 'Synthetic thumbnail provider class not directly registered'}
 # Exercise the registered handler through actual Windows IThumbnailCache.
 $timer=[Diagnostics.Stopwatch]::StartNew()
 $first=[TrialShell.Shell]::GetSyntheticThumbnail($file,$false)
 $timer.Stop()
 $snapKey=$registry.OpenSubKey('Software\WicHeicSyntheticTrial\'+$providerId)
 try {
   $attempts=[int]$snapKey.GetValue('Attempts',0)
   $activated=[int]$snapKey.GetValue('Activated',0)
 } finally { $snapKey.Close() }
 Write-Host ('SHELL_TRIAL_PROVIDER_CONSTRUCTED='+$activated)
 Write-Host ('SHELL_TRIAL_HANDLER_ENTRY_ATTEMPTS='+$attempts)
 if($first.HResult -lt 0 -or -not $first.HasBitmap) {
   $bound=[TrialShell.Shell]::BindThumbnailHandler($file)
   Write-Host ('SHELL_TRIAL_BIND_TO_THUMBNAIL_HANDLER_HRESULT=0x'+$bound.ToString('X8'))
   throw ('Shell thumbnail extraction failed for synthetic association: 0x'+$first.HResult.ToString('X8'))
 }
 $again=[TrialShell.Shell]::GetSyntheticThumbnail($file,$true)
 if($again.HResult -lt 0 -or -not $again.HasBitmap) {
   throw 'Shell cache-only revisit did not find generated thumbnail'
 }
 $regKey=$registry.OpenSubKey('Software\WicHeicSyntheticTrial\'+$providerId)
 try { $calls=[int]$regKey.GetValue('Calls',0) } finally { $regKey.Close() }
 if($calls -lt 1) {
   throw 'Shell generated a thumbnail but never invoked the isolated provider DLL'
 }
 Write-Host ('HEIC_SHELL_ISOLATED_PROVIDER_CALLED='+$calls)
 Write-Host ('HEIC_SHELL_ISOLATED_THUMBNAIL_MS='+[Math]::Round($timer.Elapsed.TotalMilliseconds,2))
 Write-Host 'HEIC_SHELL_ISOLATED_CACHE_REVISIT_PASSED'
 $registeredOk=$true
} finally {
 # Always remove only newly-owned isolated CLSID/ProgID/extension keys;
 # the actual WIC codec registration is managed by the separate CI step.
 foreach($subkey in @(
     'Software\WicHeicSyntheticTrial\'+$providerId,
     'Software\Classes\SystemFileAssociations\'+$extension,
     'Software\Classes\'+$extension,
     'Software\Classes\'+$progid,
     'Software\Classes\CLSID\'+$providerId
 )) {
   if($registered -contains $subkey) {
     $registry.DeleteSubKeyTree($subkey,$false)
   }
 }
 [TrialShell.Shell]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero)
 if(Test-Path -LiteralPath $work) {
    Remove-Item -LiteralPath $work -Recurse -Force
 }
 foreach($subkey in $subkeys) {
   $check=$registry.OpenSubKey($subkey)
   if($check){$check.Close();throw ('Registry rollback was not complete: '+$subkey)}
 }
 if((Sha256 $src) -ne $hash){throw 'Original synthetic fixture was modified'}
 Write-Host 'HEIC_SHELL_ISOLATED_REGISTRY_ROLLBACK_PASSED'
}
if(-not $registeredOk){throw 'Shell integration test did not finish successfully'}
Write-Host 'HEIC_SHELL_ISOLATED_EXPERIMENT_PASSED'
