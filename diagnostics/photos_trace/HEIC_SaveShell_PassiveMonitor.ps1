#requires -Version 5.1
# Windows 10/PowerShell 5.1. Passive observer of FileSystemWatcher and Shell
# change notifications. Never changes image, codec, Shell cache or registry.
param([ValidateRange(3,180)][int]$DurationSeconds=90,[switch]$CiSelfTest)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

if($CiSelfTest -and $env:GITHUB_ACTIONS -ne 'true'){
 throw 'CI self test is restricted to GitHub Actions.'
}
$testFolder=$null
$source=$null
try {
 if($CiSelfTest){
  $testFolder=Join-Path $env:RUNNER_TEMP ('heic_shell_ci_'+[guid]::NewGuid().ToString('N'))
  [void][IO.Directory]::CreateDirectory($testFolder)
  $source=Join-Path $testFolder 'synthetic.png'
  [IO.File]::WriteAllBytes($source,[byte[]](1,2,3,4,5,6,7))
 } else {
  # The file picker MUST be opened by the already-visible monitoring form.
  # In v1 the picker appeared before any form existed and ShowDialog could
  # subsequently be hidden under the foreground app.
  $source=''
 }
 if(-not ('HeicShellWatch20261028.Form' -as [type])){
  Add-Type -ReferencedAssemblies @('System.Windows.Forms','System.Drawing','System.Core') -TypeDefinition @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;
namespace HeicShellWatch20261028 {
 [StructLayout(LayoutKind.Sequential)]
 public struct SHNotifyEntry {
   public IntPtr pidl;
   [MarshalAs(UnmanagedType.Bool)] public bool recursive;
 }
 public sealed class Form : System.Windows.Forms.Form {
  const int WM_SHELL=0x8000+207;
  const int SHCNRF_INTERRUPT=0x0001, SHCNRF_SHELL=0x0002, SHCNRF_NEW=0x8000;
  const int SHCNE_RENAMEITEM=0x0001, SHCNE_CREATE=0x0002,SHCNE_DELETE=0x0004;
  const int SHCNE_UPDATEDIR=0x1000,SHCNE_UPDATEITEM=0x2000;
  [DllImport("shell32.dll",CharSet=CharSet.Unicode)]
  static extern int SHParseDisplayName(string name,IntPtr ctx,out IntPtr pidl,uint inMask,IntPtr outMask);
  [DllImport("shell32.dll")]
  static extern uint SHChangeNotifyRegister(IntPtr hwnd,int sources,int events,
     uint message,int entries,ref SHNotifyEntry entry);
  [DllImport("shell32.dll")]
  [return:MarshalAs(UnmanagedType.Bool)]
  static extern bool SHChangeNotifyDeregister(uint registration);
  [DllImport("shell32.dll")]
  static extern IntPtr SHChangeNotification_Lock(IntPtr change,uint pid,out IntPtr pidls,out int eventCode);
  [DllImport("shell32.dll")]
  [return:MarshalAs(UnmanagedType.Bool)]
  static extern bool SHChangeNotification_Unlock(IntPtr handle);
  [DllImport("shell32.dll",CharSet=CharSet.Unicode)]
  [return:MarshalAs(UnmanagedType.Bool)]
  static extern bool SHGetPathFromIDListW(IntPtr pidl,StringBuilder path);
  [DllImport("shell32.dll",CharSet=CharSet.Unicode)]
  static extern void SHChangeNotify(int eventCode,uint flags,string path,IntPtr unused);
  readonly List<string> records=new List<string>();
  readonly object sync=new object();
  string path,folder;
  readonly Label info=new Label();
  readonly Button stop=new Button();
  readonly System.Windows.Forms.Timer timer=new System.Windows.Forms.Timer();
  FileSystemWatcher watcher;
  uint registration=0;
  IntPtr folderPidl=IntPtr.Zero;
  string shellRegistration="NOT_ATTEMPTED";
  int fsTarget=0,fsOther=0,shellTarget=0,shellFolder=0,shellOther=0,watchErrors=0;
  string priorStat="";
  readonly Stopwatch clock=new Stopwatch();
  readonly int duration;
  bool finished=false;
  public string[] Output {get;private set;}
  public string SourcePath {get {return path;}}
  public string InitialSha256 {get;private set;}
  public Form(string file,int seconds){
   path=String.IsNullOrWhiteSpace(file)?"":Path.GetFullPath(file);
   folder=String.IsNullOrWhiteSpace(path)?"":Path.GetDirectoryName(path);
   duration=seconds;
   Text="HEIC 保存通知診断 - 監視画面";
   Width=660;Height=285;StartPosition=FormStartPosition.CenterScreen;
   ShowInTaskbar=true;
   TopMost=true;
   Font=new Font("Meiryo UI",10f);
   FormBorderStyle=FormBorderStyle.FixedDialog;
   MaximizeBox=false;
   info.Left=20;info.Top=20;info.Width=605;info.Height=155;
   info.Text="監視画面を開きました。写真の選択画面を表示します。";
   info.AutoSize=false;
   Controls.Add(info);
   stop.Text="終了して結果を保存";
   stop.Width=230;stop.Height=40;stop.Left=205;stop.Top=180;
   stop.Click+=(s,e)=>Close();
   Controls.Add(stop);
   timer.Interval=250;
   timer.Tick+=(s,e)=>Tick();
   Shown+=(s,e)=>SelectSourceAndStart();
   FormClosing+=(s,e)=>Finish();
  }
  void SelectSourceAndStart(){
   // Shown means the parent diagnostic window is already visible.
   // The native OpenFileDialog is owned by this window, preventing a
   // disappearing/behind-other-windows GUI after the selection closes.
   try {
    if(String.IsNullOrEmpty(path)) {
     using(var dialog=new OpenFileDialog()){
      dialog.Title="HEIC保存通知診断：監視する画像を選択";
      dialog.Filter="HEIC/HEIF (*.heic;*.heif)|*.heic;*.heif";
      dialog.CheckFileExists=true;dialog.Multiselect=false;
      dialog.RestoreDirectory=true;
      if(dialog.ShowDialog(this)!=DialogResult.OK){Close();return;}
      path=Path.GetFullPath(dialog.FileName);
      folder=Path.GetDirectoryName(path);
     }
    }
    var file=new FileInfo(path);
    if(!file.Exists || String.IsNullOrWhiteSpace(folder) ||
       path.Length<4 || path[1]!=':' || path[2]!='\\')
     throw new IOException("File missing or unsupported path");
    // Real input must be a HEIC. The CI-only synthetic PNG is allowed only
    // inside the runner's isolated temporary directory.
    if(!String.Equals(Path.GetExtension(path),".heic",StringComparison.OrdinalIgnoreCase) &&
       !String.Equals(Path.GetExtension(path),".heif",StringComparison.OrdinalIgnoreCase) &&
       !(Environment.GetEnvironmentVariable("GITHUB_ACTIONS")=="true" &&
         String.Equals(Path.GetExtension(path),".png",StringComparison.OrdinalIgnoreCase)))
      throw new IOException("Expected HEIC/HEIF");
    if((file.Attributes&FileAttributes.ReparsePoint)!=0)
      throw new IOException("Linked file not allowed");
    for(DirectoryInfo p=file.Directory;p!=null;p=p.Parent)
      if((p.Attributes&FileAttributes.ReparsePoint)!=0)
        throw new IOException("Linked folder not allowed");
    using(var sha=SHA256.Create())
    using(var fs=new FileStream(path,FileMode.Open,FileAccess.Read,
         FileShare.ReadWrite|FileShare.Delete))
     InitialSha256=BitConverter.ToString(sha.ComputeHash(fs)).Replace("-","");
    Record("UI","WINDOW_VISIBLE","target");
    Begin();
    info.Text="監視を開始しました。フォトで画像を90度回転し、保存してください。";
    TopMost=false;
    BringToFront();
    Activate();
   } catch(Exception ex) {
    MessageBox.Show(this,"診断を開始できませんでした（"+ex.GetType().Name+"）。"+
      "\r\n写真本体は変更していません。","診断エラー",
      MessageBoxButtons.OK,MessageBoxIcon.Error);
    Close();
   }
  }
  void Record(string category,string status,string scope) {
   string line=DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ss.fffZ")+
    " elapsed_ms="+clock.ElapsedMilliseconds+
    " channel="+category+" event="+status+" scope="+scope;
   lock(sync){if(records.Count<2500)records.Add(line);}
  }
  string Scope(string p){
   if(String.IsNullOrEmpty(p))return "unknown";
   if(String.Equals(p,path,StringComparison.OrdinalIgnoreCase))return "target";
   if(String.Equals(p,folder,StringComparison.OrdinalIgnoreCase))return "folder";
   return "other";
  }
  string PidlPath(IntPtr item){
   if(item==IntPtr.Zero)return "";
   var output=new StringBuilder(4096);
   return SHGetPathFromIDListW(item,output)?output.ToString():"";
  }
  protected override void WndProc(ref Message m){
   if(m.Msg==WM_SHELL){
    IntPtr list=IntPtr.Zero;int evt=0;
    IntPtr l=SHChangeNotification_Lock(m.WParam,
      unchecked((uint)m.LParam.ToInt64()),out list,out evt);
    if(l!=IntPtr.Zero){
     try{
      IntPtr first=list!=IntPtr.Zero?Marshal.ReadIntPtr(list):IntPtr.Zero;
      string kind=evt==SHCNE_UPDATEITEM?"UPDATEITEM":
       evt==SHCNE_UPDATEDIR?"UPDATEDIR":
       evt==SHCNE_CREATE?"CREATE":evt==SHCNE_DELETE?"DELETE":
       evt==SHCNE_RENAMEITEM?"RENAMEITEM":"OTHER_0x"+evt.ToString("X8");
      string scope=Scope(PidlPath(first));
      if(scope=="target")shellTarget++;
      else if(scope=="folder")shellFolder++;
      else shellOther++;
      Record("SHELL",kind,scope);
     } finally{SHChangeNotification_Unlock(l);}
    } else{Record("SHELL","LOCK_FAILED","unknown");}
    return;
   }
   base.WndProc(ref m);
  }
  void FsEvent(object sender,FileSystemEventArgs e){
   string scope=Scope(e.FullPath);
   if(scope=="target")System.Threading.Interlocked.Increment(ref fsTarget);
   else System.Threading.Interlocked.Increment(ref fsOther);
   Record("FS",e.ChangeType.ToString().ToUpperInvariant(),scope);
  }
  void FsRename(object sender,RenamedEventArgs e){
   string scope=Scope(e.FullPath);
   if(scope!="target")scope=Scope(e.OldFullPath);
   if(scope=="target")System.Threading.Interlocked.Increment(ref fsTarget);
   else System.Threading.Interlocked.Increment(ref fsOther);
   Record("FS","RENAMED",scope);
  }
  void FsError(object sender,ErrorEventArgs e){
   System.Threading.Interlocked.Increment(ref watchErrors);
   Record("FS","WATCHER_ERROR","unknown");
  }
  string Stat(){
   try{
    var f=new FileInfo(path);f.Refresh();
    return f.Exists?("exists=1 bytes="+f.Length+" write_ticks="+f.LastWriteTimeUtc.Ticks):"exists=0";
   }catch{return "unavailable";}
  }
  public void Begin(){
   clock.Start();
   Record("SESSION","START","target");
   priorStat=Stat();
   Record("FILE","STATE_"+priorStat.Replace(' ','_'),"target");
   try{
    watcher=new FileSystemWatcher(folder);
    watcher.Filter="*.*";watcher.IncludeSubdirectories=false;
    watcher.NotifyFilter=NotifyFilters.FileName|NotifyFilters.LastWrite|
      NotifyFilters.CreationTime|NotifyFilters.Size;
    watcher.InternalBufferSize=8192;
    watcher.Changed+=FsEvent;watcher.Created+=FsEvent;
    watcher.Deleted+=FsEvent;watcher.Renamed+=FsRename;watcher.Error+=FsError;
    watcher.EnableRaisingEvents=true;
   } catch(Exception e){Record("FS","REGISTRATION_FAILED_"+e.GetType().Name,"unknown");}
   int hr=SHParseDisplayName(folder,IntPtr.Zero,out folderPidl,0,IntPtr.Zero);
   if(hr==0 && folderPidl!=IntPtr.Zero){
    SHNotifyEntry entry=new SHNotifyEntry{pidl=folderPidl,recursive=false};
    int mask=SHCNE_UPDATEITEM|SHCNE_UPDATEDIR|SHCNE_CREATE|SHCNE_DELETE|SHCNE_RENAMEITEM;
    registration=SHChangeNotifyRegister(Handle,
      SHCNRF_INTERRUPT|SHCNRF_SHELL|SHCNRF_NEW,mask,(uint)WM_SHELL,1,ref entry);
    shellRegistration=registration==0?"FAILED":"OK";
   } else shellRegistration="PIDL_FAILED_0x"+hr.ToString("X8");
   Record("SESSION","SHELL_REGISTRATION_"+shellRegistration,"folder");
   timer.Start();
   RefreshInfo();
  }
  void Tick(){
   string stat=Stat();
   if(stat!=priorStat){
    Record("FILE","STATE_"+stat.Replace(' ','_'),"target");
    priorStat=stat;
   }
   RefreshInfo();
   if(clock.Elapsed.TotalSeconds>=duration)Close();
  }
  void RefreshInfo(){
   info.Text="● 監視中：フォトで画像を回転して「保存」してください"+
    "\r\n画像を開くだけでは変更通知は発生しないことがあります"+
    "\r\n経過: "+(clock.ElapsedMilliseconds/1000)+" / "+duration+"秒"+
    "\r\nファイル更新イベント: "+fsTarget+"  / ほか: "+fsOther+
    "\r\nShell通知: 対象 "+shellTarget+"  フォルダー "+shellFolder+"  ほか "+shellOther+
    "\r\nShell登録: "+shellRegistration;
  }
  void Finish(){
   if(finished)return;finished=true;
   timer.Stop();
   if(watcher!=null){watcher.EnableRaisingEvents=false;watcher.Dispose();}
   if(registration!=0){SHChangeNotifyDeregister(registration);registration=0;}
   if(folderPidl!=IntPtr.Zero){Marshal.FreeCoTaskMem(folderPidl);folderPidl=IntPtr.Zero;}
   Record("SESSION","END","target");
   var lines=new List<string>();
   lines.Add("HEIC_PASSIVE_FS_SHELL_MONITOR_V2");
   lines.Add("GUI_SHOWN="+(records.Exists(x=>x.Contains("channel=UI event=WINDOW_VISIBLE"))?"True":"False"));
   lines.Add("SHELL_REGISTRATION="+shellRegistration);
   lines.Add("FS_TARGET_EVENTS="+fsTarget);
   lines.Add("FS_OTHER_EVENTS="+fsOther);
   lines.Add("SHELL_TARGET_EVENTS="+shellTarget);
   lines.Add("SHELL_FOLDER_EVENTS="+shellFolder);
   lines.Add("SHELL_OTHER_EVENTS="+shellOther);
   lines.Add("FS_WATCHER_ERRORS="+watchErrors);
   lines.Add("DURATION_MS="+clock.ElapsedMilliseconds);
   lines.Add("NOTE=Notifications are directory-scoped; shell folder updates can combine multiple item changes.");
   lines.Add("NOTE=Absence of events does not prove the Explorer UI never saw or received notifications.");
   lines.Add("NOTE=No source filenames, folder paths, photo bytes, EXIF, keyboard events, or screen captures in log.");
   lock(sync){lines.AddRange(records);}
   Output=lines.ToArray();
  }
  public static void CiSyntheticNotify(string file){
   SHChangeNotify(SHCNE_UPDATEITEM,0x0005,file,IntPtr.Zero); // SHCNF_PATHW=0x5
  }
 }
}
'@ -ErrorAction Stop
 }
 # Both CI and real users enter through the same modal Form.ShowDialog().
 # In V1 the timer started BEFORE the GUI was shown; in V2 Shown owns startup.
 $window=New-Object -TypeName HeicShellWatch20261028.Form -ArgumentList @($source,$DurationSeconds)
 if($CiSelfTest) {
  $simTimer=New-Object System.Windows.Forms.Timer
  $simTimer.Interval=250
  $ticks=0
  $simTimer.add_Tick({
   $ticks++
   if($ticks -eq 4) {
    [IO.File]::AppendAllText($source,'abc')
    [HeicShellWatch20261028.Form]::CiSyntheticNotify($source)
   }
   if($ticks -ge 12){$simTimer.Stop();$window.Close()}
  })
  try {
   $simTimer.Start()
   [void]$window.ShowDialog()
  } finally {$simTimer.Stop();$simTimer.Dispose()}
 } else {
  [void]$window.ShowDialog()
 }
 $source=$window.SourcePath
 if([string]::IsNullOrWhiteSpace($source)){exit 0}
 $output=@($window.Output)
 $after=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
 $output+=('INPUT_SHA256_CHANGED='+[string]($window.InitialSha256 -ne $after))
 $outFolder=Join-Path $PSScriptRoot '診断結果'
 if(-not (Test-Path -LiteralPath $outFolder)){
  [void][IO.Directory]::CreateDirectory($outFolder)
 }
 $target=Join-Path $outFolder ('HEIC_ShellWatch_'+(Get-Date -Format 'yyyyMMdd_HHmmss_fff')+'.txt')
 [IO.File]::WriteAllLines($target,[string[]]$output,(New-Object System.Text.UTF8Encoding($true)))
 if($CiSelfTest){
  if(-not ($output -match '^GUI_SHOWN=True{throw 'Shell notification register failed.'}
  if(-not ($output -match 'channel=FS event=CHANGED scope=target')){
    throw 'FileSystemWatcher failed to observe synthetic update.'
  }
  if(-not ($output -match 'channel=SHELL event=UPDATEITEM scope=target')){
    throw 'Shell change watcher failed to observe synthetic update.'
  }
  Write-Host 'HEIC_SHELL_WATCH_SYNTHETIC_PASS'
  Write-Host ('FS_LOG_CREATED=True shell_events='+@($output|Where-Object {$_ -match 'channel=SHELL'}).Count)
 } else {
  Start-Process -FilePath 'explorer.exe' -ArgumentList ('"'+$outFolder+'"') -ErrorAction SilentlyContinue
  Start-Process -FilePath 'notepad.exe' -ArgumentList ('"'+$target+'"') -ErrorAction SilentlyContinue
  [System.Windows.Forms.MessageBox]::Show('診断結果を保存しました。'+[Environment]::NewLine+
   $target+[Environment]::NewLine+'この診断は画像もキャッシュも変更しません。',
   'HEIC保存通知診断','OK','Information')|Out-Null
 }
} finally {
 if($testFolder -and (Test-Path -LiteralPath $testFolder)){
  Remove-Item -LiteralPath $testFolder -Recurse -Force -ErrorAction SilentlyContinue
 }
}
)){throw 'Monitoring window was not visibly shown.'}
  if(-not ($output -match '^SHELL_REGISTRATION=OK{throw 'Shell notification register failed.'}
  if(-not ($output -match 'channel=FS event=CHANGED scope=target')){
    throw 'FileSystemWatcher failed to observe synthetic update.'
  }
  if(-not ($output -match 'channel=SHELL event=UPDATEITEM scope=target')){
    throw 'Shell change watcher failed to observe synthetic update.'
  }
  Write-Host 'HEIC_SHELL_WATCH_SYNTHETIC_PASS'
  Write-Host ('FS_LOG_CREATED=True shell_events='+@($output|Where-Object {$_ -match 'channel=SHELL'}).Count)
 } else {
  Start-Process -FilePath 'explorer.exe' -ArgumentList ('"'+$outFolder+'"') -ErrorAction SilentlyContinue
  Start-Process -FilePath 'notepad.exe' -ArgumentList ('"'+$target+'"') -ErrorAction SilentlyContinue
  [System.Windows.Forms.MessageBox]::Show('診断結果を保存しました。'+[Environment]::NewLine+
   $target+[Environment]::NewLine+'この診断は画像もキャッシュも変更しません。',
   'HEIC保存通知診断','OK','Information')|Out-Null
 }
} finally {
 if($testFolder -and (Test-Path -LiteralPath $testFolder)){
  Remove-Item -LiteralPath $testFolder -Recurse -Force -ErrorAction SilentlyContinue
 }
}
)){throw 'Shell notification register failed.'}
  if(-not ($output -match 'channel=FS event=CHANGED scope=target')){
    throw 'FileSystemWatcher failed to observe synthetic update.'
  }
  if(-not ($output -match 'channel=SHELL event=UPDATEITEM scope=target')){
    throw 'Shell change watcher failed to observe synthetic update.'
  }
  Write-Host 'HEIC_SHELL_WATCH_SYNTHETIC_PASS'
  Write-Host ('FS_LOG_CREATED=True shell_events='+@($output|Where-Object {$_ -match 'channel=SHELL'}).Count)
 } else {
  Start-Process -FilePath 'explorer.exe' -ArgumentList ('"'+$outFolder+'"') -ErrorAction SilentlyContinue
  Start-Process -FilePath 'notepad.exe' -ArgumentList ('"'+$target+'"') -ErrorAction SilentlyContinue
  [System.Windows.Forms.MessageBox]::Show('診断結果を保存しました。'+[Environment]::NewLine+
   $target+[Environment]::NewLine+'この診断は画像もキャッシュも変更しません。',
   'HEIC保存通知診断','OK','Information')|Out-Null
 }
} finally {
 if($testFolder -and (Test-Path -LiteralPath $testFolder)){
  Remove-Item -LiteralPath $testFolder -Recurse -Force -ErrorAction SilentlyContinue
 }
}
