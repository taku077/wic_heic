#requires -version 5.1
<#
Experimental low-priority Explorer HEIC thumbnail cache warmer.
No installation, registry edits, global cache invalidation or file writes.
Runs only for the requested time; user photographs are read only.
Windows 10 Explorer integration is unverified until a real user test.
#>
[CmdletBinding()]
param(
    [ValidateSet('Watch','Folder','SelfTest')][string]$Mode = 'Watch',
    [ValidateRange(5,600)][int]$DurationSeconds = 180,
    [ValidateRange(1,30)][int]$MaxFiles = 20,
    [string]$Folder
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$code = @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;

namespace WicHeicShellWarm {
    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe")]
    public interface IShellItem { }
    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("091162a4-bc96-411f-aae8-c5122cd03363")]
    public interface ISharedBitmap { }
    [StructLayout(LayoutKind.Sequential)]
    public struct WTS_THUMBNAILID { public Guid Value; }
    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    [Guid("F676C15D-596A-4ce2-8234-33996F445DB1")]
    public interface IThumbnailCache {
        [PreserveSig]
        int GetThumbnail(IShellItem item, uint size, uint flags,
           [MarshalAs(UnmanagedType.Interface)] out ISharedBitmap bitmap,
           out uint outFlags, out WTS_THUMBNAILID id);
    }

    public sealed class WarmResult {
        public int Attempted;
        public int Cached;
        public int Generated;
        public int Failed;
    }
    public static class Native {
        [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
        [DllImport("shell32.dll", CharSet=CharSet.Unicode)]
        private static extern int SHCreateItemFromParsingName(
           string path, IntPtr bind, ref Guid iid,
           [MarshalAs(UnmanagedType.Interface)] out IShellItem item);
        [DllImport("kernel32.dll")]
        private static extern IntPtr GetCurrentThread();
        [DllImport("kernel32.dll")]
        private static extern bool SetThreadPriority(IntPtr thread, int priority);

        private const uint INCACHEONLY = 1;
        private const uint EXTRACT = 0;
        private const int THREAD_MODE_BACKGROUND_BEGIN = 0x10000;
        private const int THREAD_MODE_BACKGROUND_END = 0x20000;
        private static readonly Guid ShellItemGuid = new Guid("43826d1e-e718-42ee-bc55-a1e261c37bfe");
        private static readonly Guid ThumbCacheClsid = new Guid("50ef4544-ac9f-4a8e-b21b-8a26180db13f");

        private static void ReleaseCom(object instance) {
            if(instance != null && Marshal.IsComObject(instance))
                Marshal.ReleaseComObject(instance);
        }
        public static bool IsSafeFolder(string folder) {
            if(String.IsNullOrWhiteSpace(folder)) return false;
            try {
                string canonical=Path.GetFullPath(folder);
                if(canonical.StartsWith(@"\\", StringComparison.Ordinal) ||
                   !Path.IsPathRooted(canonical)) return false;
                DirectoryInfo directory=new DirectoryInfo(canonical);
                if(!directory.Exists ||
                   (directory.Attributes & FileAttributes.ReparsePoint)!=0)
                    return false;
                string root=Path.GetPathRoot(canonical);
                DriveInfo drive=new DriveInfo(root);
                return drive.DriveType!=DriveType.Network;
            } catch { return false; }
        }
        public static bool IsHeic(string path) {
            string ext=Path.GetExtension(path);
            return ext.Equals(".heic",StringComparison.OrdinalIgnoreCase) ||
                   ext.Equals(".heif",StringComparison.OrdinalIgnoreCase);
        }
        public static WarmResult RunFolder(string path,int maxFiles,DateTime deadlineUtc) {
            WarmResult result=new WarmResult();
            if(!IsSafeFolder(path)) return result;
            maxFiles=Math.Max(1,Math.Min(30,maxFiles));
            // Avoid recursive scanning and linked files; no user file is mutated.
            List<string> files=new List<string>();
            try {
                foreach(string entry in Directory.EnumerateFiles(path)) {
                    if(!IsHeic(entry)) continue;
                    if((File.GetAttributes(entry)&FileAttributes.ReparsePoint)!=0)
                        continue;
                    files.Add(entry);
                    if(files.Count>=maxFiles) break;
                }
            } catch { return result; }

            bool background=SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_BEGIN);
            object obj=null;
            try {
                Type type=Type.GetTypeFromCLSID(ThumbCacheClsid, true);
                obj=Activator.CreateInstance(type);
                IThumbnailCache cache=(IThumbnailCache)obj;
                foreach(string filename in files) {
                    if(DateTime.UtcNow>=deadlineUtc) break;
                    result.Attempted++;
                    IShellItem item=null;
                    ISharedBitmap bitmap=null;
                    try {
                        Guid iid=ShellItemGuid;
                        int hr=SHCreateItemFromParsingName(filename,IntPtr.Zero,ref iid,out item);
                        if(hr<0 || item==null){result.Failed++;continue;}
                        uint flags=0;
                        WTS_THUMBNAILID unused;
                        hr=cache.GetThumbnail(item,256,INCACHEONLY,out bitmap,out flags,out unused);
                        if(hr>=0 && bitmap!=null) {
                            result.Cached++;
                        } else {
                            ReleaseCom(bitmap); bitmap=null;
                            hr=cache.GetThumbnail(item,256,EXTRACT,out bitmap,out flags,out unused);
                            if(hr>=0 && bitmap!=null) result.Generated++;
                            else result.Failed++;
                        }
                    } catch { result.Failed++; }
                    finally {
                        ReleaseCom(bitmap);
                        ReleaseCom(item);
                    }
                    Thread.Sleep(40);
                }
            } catch {
                // Failure is recorded; no hidden success claim.
                if(result.Attempted==0) result.Failed++;
            } finally {
                ReleaseCom(obj);
                if(background) SetThreadPriority(GetCurrentThread(),THREAD_MODE_BACKGROUND_END);
            }
            return result;
        }
    }
}
'@
Add-Type -TypeDefinition $code -Language CSharp -ErrorAction Stop

if ($Mode -eq 'SelfTest') {
    if (-not [WicHeicShellWarm.Native]::IsHeic('sample.HEIC') -or
        -not [WicHeicShellWarm.Native]::IsHeic('sample.HEIF') -or
        [WicHeicShellWarm.Native]::IsHeic('sample.JPG') -or
        [WicHeicShellWarm.Native]::IsSafeFolder('\\server\share\folder')) {
        throw 'WARMUP_SELFTEST_FAILED'
    }
    'WARMUP_SELFTEST_PASSED'
    return
}
$limit = [DateTime]::UtcNow.AddSeconds($DurationSeconds)
if ($Mode -eq 'Folder') {
    if (-not [WicHeicShellWarm.Native]::IsSafeFolder($Folder)) {
        throw 'WARMUP_INVALID_FOLDER'
    }
    $result = [WicHeicShellWarm.Native]::RunFolder($Folder, $MaxFiles, $limit)
    "WARMUP_FOLDER_RESULT attempted=$($result.Attempted) cached=$($result.Cached) generated=$($result.Generated) failed=$($result.Failed)"
    if ($result.Failed -gt 0) { throw 'WARMUP_FOLDER_FAILED' }
    return
}
function Get-ForegroundExplorerFolder {
    $hwnd = [WicHeicShellWarm.Native]::GetForegroundWindow().ToInt64()
    if ($hwnd -eq 0) { return $null }
    $shell = $null
    $windows = $null
    try {
        $shell = New-Object -ComObject Shell.Application
        $windows = $shell.Windows()
        $n = [int]$windows.Count
        for ($i = 0; $i -lt $n; $i++) {
            $window = $null
            try {
                $window = $windows.Item($i)
                if ($null -eq $window -or [int64]$window.HWND -ne $hwnd) { continue }
                if ([IO.Path]::GetFileName([string]$window.FullName) -ine 'explorer.exe') {
                    continue
                }
                $path = [string]$window.Document.Folder.Self.Path
                if ([WicHeicShellWarm.Native]::IsSafeFolder($path)) { return $path }
            } catch {
                # Shell virtual folders and windows being closed are ignored.
            } finally {
                if ($null -ne $window -and [Runtime.InteropServices.Marshal]::IsComObject($window)) {
                    [void][Runtime.InteropServices.Marshal]::ReleaseComObject($window)
                }
            }
        }
    } catch {
        # Explorer is not available or window enumeration failed.
    } finally {
        if ($null -ne $windows -and [Runtime.InteropServices.Marshal]::IsComObject($windows)) {
            [void][Runtime.InteropServices.Marshal]::ReleaseComObject($windows)
        }
        if ($null -ne $shell -and [Runtime.InteropServices.Marshal]::IsComObject($shell)) {
            [void][Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
        }
    }
    return $null
}
$seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$visits = 0
$generated = 0
$cached = 0
$errors = 0
'WARMUP_WATCH_STARTED auto_exit_seconds=' + $DurationSeconds
while ([DateTime]::UtcNow -lt $limit) {
    $folder = Get-ForegroundExplorerFolder
    if ($folder -and $seen.Add($folder)) {
        $visits++
        $result = [WicHeicShellWarm.Native]::RunFolder($folder, $MaxFiles, $limit)
        $generated += $result.Generated
        $cached += $result.Cached
        $errors += $result.Failed
    }
    Start-Sleep -Milliseconds 1500
}
"WARMUP_WATCH_COMPLETED folders=$visits generated=$generated reused=$cached errors=$errors"
