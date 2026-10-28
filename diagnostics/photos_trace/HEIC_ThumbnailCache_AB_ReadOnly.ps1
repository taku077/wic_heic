# Windows PowerShell 5.1. Compare the existing Shell thumbnail cache with
# extraction without adding a thumbnail to the cache. No MSI/registry changes.
# Never writes to the HEIC, modifies original pixels, or prints file paths/EXIF.
[CmdletBinding()]
param(
 [string]$ImagePath,
 [ValidateRange(64,512)][int]$Size=256,
 [switch]$CiSyntheticOnly
)
$ErrorActionPreference='Stop'
if($CiSyntheticOnly -and $env:GITHUB_ACTIONS -ne 'true') {
 throw 'Synthetic-only mode is restricted to GitHub Actions.'
}
if([string]::IsNullOrWhiteSpace($ImagePath)){
 Add-Type -AssemblyName System.Windows.Forms
 $dlg=New-Object System.Windows.Forms.OpenFileDialog
 try {
  $dlg.Filter='HEIC/HEIF copy (*.heic;*.heif)|*.heic;*.heif'
  $dlg.CheckFileExists=$true; $dlg.Multiselect=$false
  $dlg.Title='Select a COPY of the HEIC for read-only diagnosis'
  if($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
   throw 'File selection cancelled'
  }
  $ImagePath=$dlg.FileName
 } finally { $dlg.Dispose() }
}
$file=Get-Item -LiteralPath $ImagePath -ErrorAction Stop
if($CiSyntheticOnly){ $allowed=@('.png','.jpg','.jpeg') }
else { $allowed=@('.heic','.heif') }
if($file.PSIsContainer -or $file.Extension -notin $allowed) {
 throw 'Not an expected image file'
}
$node=$file
while($null -ne $node) {
 if(($node.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
  throw 'Symlink/junction images or folders not supported'
 }
 if($node -is [IO.FileInfo]){ $node=$node.Directory }
 else { $node=$node.Parent }
}
if($file.FullName -notmatch '^[A-Za-z]:\\'){throw 'Local drive required'}
$originalLength=[long]$file.Length
$originalWriteTicks=$file.LastWriteTimeUtc.Ticks
if(-not ('WicShellCacheAB20261028.Inspect' -as [type])) {
 Add-Type -ReferencedAssemblies @('System.Drawing','System.Core') -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
namespace WicShellCacheAB20261028 {
 [ComImport, Guid("091162a4-bc96-411f-aae8-c5122cd03363"),
  InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
 public interface ISharedBitmap {
  [PreserveSig] int GetSharedBitmap(out IntPtr bitmap);
  [PreserveSig] int GetSize(out SIZE size);
  [PreserveSig] int GetFormat(out uint alphaType);
  [PreserveSig] int InitializeBitmap(IntPtr bitmap,uint alphaType);
  [PreserveSig] int Detach(out IntPtr bitmap);
 }
 [StructLayout(LayoutKind.Sequential)]
 public struct SIZE { public int cx,cy; }
 [ComImport, Guid("f676c15d-596a-4ce2-8234-33996f445db1"),
  InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
 public interface IThumbnailCache {
  [PreserveSig] int GetThumbnail(IntPtr item,uint pixels,uint flags,
   [MarshalAs(UnmanagedType.Interface)] out ISharedBitmap bitmap,
   out uint cacheFlags,IntPtr unusedThumbnailId);
  [PreserveSig] int GetThumbnailByID(IntPtr id,uint pixels,
   [MarshalAs(UnmanagedType.Interface)] out ISharedBitmap bitmap,
   out uint cacheFlags);
 }
 [ComImport, Guid("50ef4544-ac9f-4a8e-b21b-8a26180db13f"),
  ClassInterface(ClassInterfaceType.None)]
 public class LocalThumbnailCache {}
 public sealed class Result {
  public string Mode {get;set;}
  public string Status {get;set;}
  public string Digest {get;set;}
  public int Width {get;set;}
  public int Height {get;set;}
  public int Flags {get;set;}
  public long ElapsedMs {get;set;}
 }
 public static class Inspect {
  [DllImport("shell32.dll",CharSet=CharSet.Unicode,PreserveSig=true)]
  private static extern int SHCreateItemFromParsingName(string path,
   IntPtr bindContext,ref Guid iid,out IntPtr item);
  public static Result One(string path,uint pixels,uint flags,string label) {
   var r=new Result{Mode=label,Status="UNAVAILABLE",Digest="NONE"};
   IntPtr item=IntPtr.Zero;
   IThumbnailCache cache=null;
   ISharedBitmap shared=null;
   var clock=System.Diagnostics.Stopwatch.StartNew();
   try {
    Guid iid=new Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE");
    int hr=SHCreateItemFromParsingName(path,IntPtr.Zero,ref iid,out item);
    if(hr<0){
     r.Status=String.Format("SHELL_ITEM_HRESULT_0x{0:X8}",unchecked((uint)hr));
     return r;
    }
    cache=(IThumbnailCache)new LocalThumbnailCache();
    uint outFlags=0;
    hr=cache.GetThumbnail(item,pixels,flags,out shared,out outFlags,IntPtr.Zero);
    r.Flags=(int)outFlags;
    if(hr<0 || shared==null){
     r.Status=String.Format("THUMBNAIL_HRESULT_0x{0:X8}",unchecked((uint)hr));
     return r;
    }
    IntPtr hbitmap;
    hr=shared.GetSharedBitmap(out hbitmap);
    if(hr<0 || hbitmap==IntPtr.Zero) {
     r.Status=String.Format("BITMAP_HRESULT_0x{0:X8}",unchecked((uint)hr));
     return r;
    }
    // BORROWED HBITMAP. Do not DeleteObject; ISharedBitmap retains ownership.
    using(var bitmap=Bitmap.FromHbitmap(hbitmap))
    using(var memory=new MemoryStream())
    using(var sha=SHA256.Create()) {
     bitmap.Save(memory,ImageFormat.Png); // Memory only: no image files saved.
     r.Digest=BitConverter.ToString(sha.ComputeHash(memory.ToArray()))
      .Replace("-","").Substring(0,20);
     r.Width=bitmap.Width;r.Height=bitmap.Height;
    }
    r.Status="OK";return r;
   } catch(Exception ex) {
    // Never expose exception messages: they may contain filenames or paths.
    r.Status="EXCEPTION_"+ex.GetType().Name;
    return r;
   } finally {
    if(shared!=null)Marshal.ReleaseComObject(shared);
    if(cache!=null)Marshal.ReleaseComObject(cache);
    if(item!=IntPtr.Zero)Marshal.Release(item);
    clock.Stop();r.ElapsedMs=clock.ElapsedMilliseconds;
   }
  }
 }
}
'@ -ErrorAction Stop
}
Write-Output 'HEIC_THUMBNAIL_CACHE_AB_V1'
Write-Output ('image_bytes_before={0} requested_thumbnail_size={1}' -f $originalLength,$Size)
# 0x01: read only an already cached thumbnail. No extraction.
$a=[WicShellCacheAB20261028.Inspect]::One($file.FullName,[uint32]$Size,1,'CACHE_ONLY')
Write-Output ('CACHE_ONLY status={0} dims={1}x{2} flags=0x{3:X8} pixels_sha20={4} elapsed_ms={5}' -f $a.Status,$a.Width,$a.Height,$a.Flags,$a.Digest,$a.ElapsedMs)
# 0x20: extract, but do not add thumbnail to system cache.
$b=[WicShellCacheAB20261028.Inspect]::One($file.FullName,[uint32]$Size,32,'EXTRACT_NO_CACHE')
Write-Output ('EXTRACT_NO_CACHE status={0} dims={1}x{2} flags=0x{3:X8} pixels_sha20={4} elapsed_ms={5}' -f $b.Status,$b.Width,$b.Height,$b.Flags,$b.Digest,$b.ElapsedMs)
if($a.Status -eq 'OK' -and $b.Status -eq 'OK') {
 if($a.Width -eq $b.Width -and $a.Height -eq $b.Height -and $a.Digest -eq $b.Digest) {
  Write-Output 'COMPARE=IDENTICAL_AT_REQUESTED_SIZE'
 } else {
  Write-Output 'COMPARE=DIFFERENT_CACHED_AND_EXTRACTED_BITMAPS'
 }
} else { Write-Output 'COMPARE=INCONCLUSIVE_CHECK_HRESULTS' }
$file.Refresh()
$unchanged=($file.Length -eq $originalLength -and
            $file.LastWriteTimeUtc.Ticks -eq $originalWriteTicks)
Write-Output ('INPUT_LENGTH_AND_MTIME_UNCHANGED={0}' -f $unchanged)
if(-not $unchanged){throw 'File changed during observation; result invalid'}
Write-Output 'NOTE=No cache deletion, registry change or image-file write was performed.'
Write-Output 'NOTE=Different output may reflect independent extraction paths, image scaling or stale cache; not proof of corrupt HEIC.'