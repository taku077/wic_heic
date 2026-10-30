// PRIVACY: runs entirely on the local Windows 10 PC; does not upload ETL
// files, photos, paths, command lines, process arguments or memory addresses.
// Safely summarizes SAMPLE COUNTS by executable filename across ALL processes.
// Windows 10 already includes .NET Framework 4.8; no WPR re-recording needed.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Microsoft.Diagnostics.Tracing;

internal static class Program {
    private sealed class Bucket {
        public long Samples;
        public readonly Dictionary<string,long> Names =
            new Dictionary<string,long>(StringComparer.OrdinalIgnoreCase);
        // Separate same-named instances (especially multiple dllhost.exe/Photos PIDs).
        // No command lines, paths, image names, memory addresses, or TIDs leave the PC.
        public readonly Dictionary<string,long> Instances =
            new Dictionary<string,long>(StringComparer.OrdinalIgnoreCase);
        public void Add(string n, int pid) {
            Samples++;
            long prior;
            Names.TryGetValue(n,out prior);
            Names[n]=prior+1;
            var instance = n + (pid>0 ? " [PID " + pid + "]" : " [PID unknown]");
            Instances.TryGetValue(instance,out prior);
            Instances[instance]=prior+1;
        }
    }
    private static string Short(string name) {
        if(string.IsNullOrWhiteSpace(name))return "(unknown)";
        name=name.Replace('/','\\');
        int slash=name.LastIndexOf('\\');
        if(slash>=0)name=name.Substring(slash+1);
        name=name.Replace("\r","").Replace("\n","");
        return name.Length>96?name.Substring(0,96):name;
    }
    private static int Main(string[] args) {
        if(args.Length==1 && args[0]=="--self-test") {
            var b=new Bucket();
            b.Add("dllhost.exe",111);
            b.Add("dllhost.exe",222);
            b.Add("dllhost.exe",111);
            if(b.Samples!=3 || b.Names["dllhost.exe"]!=3 ||
               b.Instances["dllhost.exe [PID 111]"]!=2 ||
               b.Instances["dllhost.exe [PID 222]"]!=1 ||
               b.Instances.Count!=2) {
                Console.Error.WriteLine("PID_BUCKET_SELFTEST_FAIL");
                return 10;
            }
            Console.WriteLine("PID_BUCKET_SELFTEST_PASS");
            return 0;
        }
        if(args.Length==1&&(args[0]=="--help"||args[0]=="-h")) {
            Console.WriteLine("Read-only offline ETL CPU sample attribution; all processes.");
            Console.WriteLine("Usage: WicHeic_EtlCpuTop_ReadOnly.exe <CPU_Stacks.etl> <new_report.txt>");
            Console.WriteLine("Report MUST NOT already exist. No network; no original image access.");
            return 0;
        }
        if(args.Length!=2) {
            Console.Error.WriteLine("Expected exactly ETL path and NEW report file path.");
            return 2;
        }
        var path=args[0];var outPath=args[1];
        if(!File.Exists(path)||Path.GetExtension(path).ToLowerInvariant()!=".etl") {
            Console.Error.WriteLine("Input is not an existing .etl trace.");return 3;
        }
        if(File.Exists(outPath)) {
            Console.Error.WriteLine("Output exists; refusing to overwrite.");return 4;
        }
        if(string.Equals(Path.GetFullPath(path),Path.GetFullPath(outPath),
             StringComparison.OrdinalIgnoreCase)) {
            Console.Error.WriteLine("Input and output cannot be the same.");return 4;
        }
        var namesByPid=new Dictionary<int,string>();
        var pidByTid=new Dictionary<int,int>();
        var all=new Bucket();
        var windows=new SortedDictionary<long,Bucket>();
        long total=0,idle=0,unknown=0;
        DateTime start=DateTime.MaxValue,end=DateTime.MinValue;
        int cpus=0;
        try {
            using(var source=new ETWTraceEventSource(path)) {
                cpus=source.NumberOfProcessors;
                // ProcessStartGroup includes process rundown for programs
                // already running when WPR began.
                source.Kernel.ProcessStartGroup += e => {
                    if(e.ProcessID>0) namesByPid[e.ProcessID]=Short(e.ProcessName);
                };
                source.Kernel.ThreadStartGroup += e => {
                    if(e.ThreadID>0 && e.ProcessID>0)pidByTid[e.ThreadID]=e.ProcessID;
                };
                source.Kernel.ThreadEndGroup += e => {
                    if(e.ThreadID>0)pidByTid.Remove(e.ThreadID);
                };
                source.Kernel.PerfInfoSample += e => {
                    total++;
                    if(e.ThreadID<=0){idle++;return;}
                    int pid=e.ProcessID;
                    if(pid<=0)pidByTid.TryGetValue(e.ThreadID,out pid);
                    string name;
                    if(pid>0 && namesByPid.TryGetValue(pid,out name)){}
                    else name=Short(e.ProcessName);
                    if(name.StartsWith("Process(",StringComparison.OrdinalIgnoreCase)
                        &&pid>0)name="PID_"+pid;
                    if(string.IsNullOrWhiteSpace(name)){unknown++;name="(unknown)";}
                    all.Add(name,pid);
                    var utc=e.TimeStamp.ToUniversalTime();
                    if(utc<start)start=utc;
                    if(utc>end)end=utc;
                    var bucket=(utc.Ticks/TimeSpan.TicksPerSecond)/10;
                    Bucket b;
                    if(!windows.TryGetValue(bucket,out b)){
                        b=new Bucket();windows[bucket]=b;
                    }
                    b.Add(name,pid);
                };
                source.Process();
            }
            if(total==0 || all.Samples==0) {
                Console.Error.WriteLine("No usable CPU sampling events in ETL. Leave original unchanged.");
                return 5;
            }
            var sb=new StringBuilder();
            sb.AppendLine("WIC HEIC Windows10 CPU100 — OFFLINE ALL-PROCESS ETL REPORT");
            sb.AppendLine("Only executable basenames + aggregate CPU sampling counts. No paths.");
            sb.AppendLine("Trace: [local ETL; filename omitted]");
            sb.AppendLine("Native ETW process sampling: NOT Task Manager's overall CPU utility.");
            sb.AppendLine("Shares below are percent of NON-IDLE sampled CPU work, NOT total PC CPU %.");
            sb.AppendLine("Kernel samples, unresolved PID, and samples lost in ETW can affect attribution.");
            sb.AppendLine("ETW sampling is not a per-image CPU timer. Do not infer per-image speed.");
            sb.AppendLine("LogicalProcessors="+cpus);
            sb.AppendLine("SamplesAll="+total+" NonIdle="+all.Samples+
                          " IdleOrTid0="+idle+" Unnamed="+unknown);
            sb.AppendLine("SampleTimeWindowUTC="+start.ToString("o")+" to "+end.ToString("o"));
            sb.AppendLine();
            sb.AppendLine("TOP EXECUTABLE NAMES BY SAMPLED ACTIVE CPU WORK");
            foreach(var n in all.Names.OrderByDescending(x=>x.Value).Take(30))
                sb.AppendLine(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                    "{0,7:F2}%  {1,10} samples  {2}",
                    100.0*n.Value/all.Samples,n.Value,n.Key));
            sb.AppendLine();
            sb.AppendLine("TOP PROCESS INSTANCES BY SAMPLED ACTIVE CPU WORK (name + PID)");
            sb.AppendLine("PIDs are local to this recording and may be reused after process exit.");
            foreach(var n in all.Instances.OrderByDescending(x=>x.Value).Take(40))
                sb.AppendLine(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                    "{0,7:F2}%  {1,10} samples  {2}",
                    100.0*n.Value/all.Samples,n.Value,n.Key));
            sb.AppendLine();
            sb.AppendLine("TOP 10-SECOND WINDOWS BY ACTIVE CPU SAMPLE COUNT");
            foreach(var win in windows.OrderByDescending(x=>x.Value.Samples).Take(16)) {
                var at=new DateTime(win.Key*10*TimeSpan.TicksPerSecond,DateTimeKind.Utc);
                sb.Append(at.ToString("o")).Append(" samples=").Append(win.Value.Samples)
                    .Append(" | ");
                foreach(var n in win.Value.Names.OrderByDescending(x=>x.Value).Take(5))
                    sb.Append(Short(n.Key)).Append('=').Append(n.Value).Append(' ');
                sb.Append(" | instances: ");
                foreach(var n in win.Value.Instances.OrderByDescending(x=>x.Value).Take(5))
                    sb.Append(Short(n.Key)).Append('=').Append(n.Value).Append(' ');
                sb.AppendLine();
            }
            sb.AppendLine();
            sb.AppendLine("Interpretation: compare 1.1.32/1.1.33 traces under similar");
            sb.AppendLine("operation windows. This report never includes raw ETL or command lines.");
            // CreateNew is deliberate: never overwrite any user report.
            using(var stream=new FileStream(outPath,FileMode.CreateNew,FileAccess.Write,FileShare.None))
            using(var writer=new StreamWriter(stream,new UTF8Encoding(false)))
                writer.Write(sb.ToString());
            Console.WriteLine("REPORT_CREATED. Existing ETL never modified. Lines="+
                              sb.ToString().Split('\n').Length);
            return 0;
        } catch(Exception ex) {
            // Never print user paths / command-line / OS environment from exception text.
            Console.Error.WriteLine("Offline ETL analysis failed: "+ex.GetType().Name);
            return 6;
        }
    }
}
