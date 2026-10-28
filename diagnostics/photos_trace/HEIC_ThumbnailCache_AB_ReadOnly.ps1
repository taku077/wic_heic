# Windows PowerShell 5.1. Snapshot cached thumbnail without mutating image.
# Optional forced extraction intentionally updates the Shell thumbnail cache.
# The forced phase runs only with the explicit user-supplied switch.
# Never writes to the HEIC, modifies original pixels, or prints file paths/EXIF.
[CmdletBinding()]
param(
 [string]$ImagePath,
 [ValidateRange(64,512)][int]$Size=256,
 [switch]$CiSyntheticOnly,
 [switch]$CiPrimeCache,
 [switch]$ForceReextractAfterSnapshot
)
$ErrorActionPreference='Stop'
if(($CiSyntheticOnly -or $CiPrimeCache) -and $env:GITHUB_ACTIONS -ne 'true') {
 throw 'CI synthetic mode and cache priming are restricted to GitHub Actions.'
}
if($CiPrimeCache -and -not $CiSyntheticOnly) {
 throw 'Cache priming is allowed only on an artificial image in CI.'
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
  // 64x64 RGB analytical samples remain IN MEMORY and are never logged.
  public byte[] SampleRgb {get;set;}
 }
 public static class Inspect {
  // Return bounded numerical similarity only. No sampled pixel array is
  // persisted, transmitted or printed.
  public static string CompareSamples(Result a,Result b) {
   if(a==null||b==null||a.SampleRgb==null||b.SampleRgb==null)
    return "PIXEL_COMPARE=UNAVAILABLE";
   if(a.Width!=b.Width||a.Height!=b.Height)
    return "PIXEL_COMPARE=DIFFERENT_DIMENSIONS";
   const int side=64;
   double direct=0,rot180=0;int pixelsChanged=0;
   for(int y=0;y<side;y++)for(int x=0;x<side;x++){
    int i=3*(y*side+x);
    int j=3*((side-1-y)*side+(side-1-x));
    int delta=0;
    for(int c=0;c<3;c++){
     int d=Math.Abs((int)a.SampleRgb[i+c]-(int)b.SampleRgb[i+c]);
     direct+=d;delta+=d;
     rot180+=Math.Abs((int)a.SampleRgb[i+c]-(int)b.SampleRgb[j+c]);
    }
    if(delta>60)pixelsChanged++;
   }
   string fmt="F2";
   var culture=System.Globalization.CultureInfo.InvariantCulture;
   return String.Format(culture,
    "PIXEL_COMPARE=OK rgb_mae_64={0} rgb_mae_if_180deg_64={1} pixels_gt20_percent_64={2}",
    (direct/(side*side*3)).ToString(fmt,culture),
    (rot180/(side*side*3)).ToString(fmt,culture),
    (100.0*pixelsChanged/(side*side)).ToString(fmt,culture));
  }
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
     // Compare visible colors rather than only the exact PNG SHA256.
     // Windows may store cached thumbs as compressed JPEG/PNG/BMP.
     using(var sample=new Bitmap(64,64,PixelFormat.Format24bppRgb))
     using(var draw=Graphics.FromImage(sample)){
      draw.Clear(Color.White);
      draw.InterpolationMode=System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
      draw.DrawImage(bitmap,new Rectangle(0,0,64,64),
        new Rectangle(0,0,bitmap.Width,bitmap.Height),GraphicsUnit.Pixel);
      byte[] vals=new byte[64*64*3];
      for(int y=0;y<64;y++)for(int x=0;x<64;x++){
       Color c=sample.GetPixel(x,y);
       int i=3*(y*64+x);
       vals[i]=c.R;vals[i+1]=c.G;vals[i+2]=c.B;
      }
      r.SampleRgb=vals;
     }
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
Write-Output 'HEIC_THUMBNAIL_CACHE_SNAPSHOT_V3'
Write-Output ('image_bytes_before={0} requested_thumbnail_size={1}' -f $originalLength,$Size)
# Artificial CI-only priming is required to test a true cached hit.
# No real HEIC can be primed through this parameter.
if($CiPrimeCache) {
 $primed=[WicShellCacheAB20261028.Inspect]::One($file.FullName,[uint32]$Size,0,'CI_SYNTHETIC_PRIME')
 if($primed.Status -ne 'OK') { throw 'Synthetic cache priming failed' }
 Write-Output 'CI_SYNTHETIC_PRIME=OK'
}
# 0x01: read only an already cached thumbnail. No extraction.
$a=[WicShellCacheAB20261028.Inspect]::One($file.FullName,[uint32]$Size,1,'CACHE_ONLY')
Write-Output ('CACHE_ONLY status={0} dims={1}x{2} flags=0x{3:X8} pixels_sha20={4} elapsed_ms={5}' -f $a.Status,$a.Width,$a.Height,$a.Flags,$a.Digest,$a.ElapsedMs)
# 0x20 is NOT used as a 'fresh' reference: CI showed it can return
# WTS_CACHED, so a naive comparison gives a false sense of correctness.
# True bypass requires WTS_FORCEEXTRACTION=0x04. This explicitly changes
# the thumbnail cache; never run it without the opt-in switch.
if($ForceReextractAfterSnapshot) {
 Write-Output 'WARNING=FORCE_REEXTRACTION_WILL_REFRESH_WINDOWS_SHELL_THUMBNAIL_CACHE'
 $b=[WicShellCacheAB20261028.Inspect]::One($file.FullName,[uint32]$Size,4,'FORCE_REEXTRACT')
 Write-Output ('FORCE_REEXTRACT status={0} dims={1}x{2} flags=0x{3:X8} pixels_sha20={4} elapsed_ms={5}' -f $b.Status,$b.Width,$b.Height,$b.Flags,$b.Digest,$b.ElapsedMs)
 if($a.Status -eq 'OK' -and $b.Status -eq 'OK') {
  if($a.Width -eq $b.Width -and $a.Height -eq $b.Height -and $a.Digest -eq $b.Digest) {
   Write-Output 'COMPARE=IDENTICAL_CACHED_VS_FORCED_EXTRACTION'
  } else {
   Write-Output 'COMPARE=DIFFERENT_CACHED_VS_FORCED_EXTRACTION'
  }
  Write-Output ([WicShellCacheAB20261028.Inspect]::CompareSamples($a,$b))
 } else {
  Write-Output 'COMPARE=INCONCLUSIVE_CHECK_HRESULTS'
  Write-Output 'PIXEL_COMPARE=UNAVAILABLE'
 }
} else {
 Write-Output 'COMPARE=NOT_PERFORMED_CACHE_SNAPSHOT_ONLY'
 Write-Output 'NOTE=Use -ForceReextractAfterSnapshot only if Shell cache refresh is acceptable.'
}
$file.Refresh()
$unchanged=($file.Length -eq $originalLength -and
            $file.LastWriteTimeUtc.Ticks -eq $originalWriteTicks)
Write-Output ('INPUT_LENGTH_AND_MTIME_UNCHANGED={0}' -f $unchanged)
if(-not $unchanged){throw 'File changed during observation; result invalid'}
Write-Output 'NOTE=No cache deletion, registry change or image-file write was performed.'
if($ForceReextractAfterSnapshot) {
 Write-Output 'NOTE=Shell thumbnail cache may have changed due to forced extraction.'
}
Write-Output 'NOTE=Hash differences can result from cached-image recompression; inspect RGB sampled errors.'
Write-Output 'NOTE=rgb_mae_64 near 0 means nearly the same visible content. A much smaller rgb_mae_if_180deg_64 than rgb_mae_64 can indicate 180-degree disagreement.'
Write-Output 'NOTE=Different bitmaps do not prove a rotation, stale cache, or corrupt HEIC.'