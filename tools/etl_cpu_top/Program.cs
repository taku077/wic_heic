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
        public long TotalSamples;
        public long NoThreadIdSamples;
        public void AddNoThreadId() {
            TotalSamples++;
            NoThreadIdSamples++;
        }
        public readonly Dictionary<string,long> Names =
            new Dictionary<string,long>(StringComparer.OrdinalIgnoreCase);
        // Separate same-named instances (especially multiple dllhost.exe/Photos PIDs).
        // No command lines, paths, image names, memory addresses, or TIDs leave the PC.
        public readonly Dictionary<string,long> Instances =
            new Dictionary<string,long>(StringComparer.OrdinalIgnoreCase);
        public void Add(string n, int pid, long count=1) {
            TotalSamples+=count;
            Samples+=count;
            long prior;
            Names.TryGetValue(n,out prior);
            Names[n]=prior+count;
            var instance = n + (pid>0 ? " [PID " + pid + "]" : " [PID unknown]");
            Instances.TryGetValue(instance,out prior);
            Instances[instance]=prior+count;
        }
    }
    // A TID may be reused by a DIFFERENT process after a thread exits.
    // Never attribute such a TID from a whole-trace map.
    private static void TrackUniqueTid(Dictionary<int,int> map,int tid,int pid) {
        if(tid<=0 || pid<=0)return;
        int old;
        if(!map.TryGetValue(tid,out old))map[tid]=pid;
        else if(old!=pid)map[tid]=-1;
    }
    private static bool Known(string name) {
        return !string.IsNullOrWhiteSpace(name) &&
            name!="(unknown)" &&
            !name.StartsWith("Process(",StringComparison.OrdinalIgnoreCase) &&
            !name.StartsWith("PID_",StringComparison.OrdinalIgnoreCase);
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
            b.AddNoThreadId();
            var tidMap=new Dictionary<int,int>();
            TrackUniqueTid(tidMap,10,936);
            TrackUniqueTid(tidMap,10,936);
            TrackUniqueTid(tidMap,11,936);
            TrackUniqueTid(tidMap,11,3284);
            if(tidMap[10]!=936 || tidMap[11]!=-1 ||
               b.Samples!=3 || b.TotalSamples!=4 || b.NoThreadIdSamples!=1 ||
               b.Names["dllhost.exe"]!=3 ||
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
        var sampleNamesByPid=new Dictionary<int,string>();
        // Collect aggregated sample counts first: process/thread rundown may be
        // emitted AFTER CPU samples. This uses no extra ETL, no intermediate file.
        var uniquePidByTid=new Dictionary<int,int>();
        var rawSamples=new Dictionary<Tuple<int,int,long>,long>();
        var all=new Bucket();
        var windows=new SortedDictionary<long,Bucket>();
        long total=0,idle=0,unknown=0;
        long rawPidSamples=0, recoveredTidSamples=0, unresolvedPidSamples=0;
        DateTime start=DateTime.MaxValue,end=DateTime.MinValue;
        int cpus=0;
        try {
            using(var source=new ETWTraceEventSource(path)) {
                cpus=source.NumberOfProcessors;
                // ProcessStartGroup includes process rundown for programs
                // already running when WPR began.
                source.Kernel.ProcessStartGroup += e => {
                    if(e.ProcessID<=0)return;
                    var n=Short(e.KernelImageFileName);
                    if(!Known(n))n=Short(e.ProcessName);
                    if(Known(n))namesByPid[e.ProcessID]=n;
                };
                // Also learn thread mappings from rundown events even when
                // those events occur after the sampled CPU workload.
                source.Kernel.ThreadStartGroup += e => {
                    TrackUniqueTid(uniquePidByTid,e.ThreadID,e.ProcessID);
                };
                source.Kernel.ThreadEndGroup += e => {
                    TrackUniqueTid(uniquePidByTid,e.ThreadID,e.ProcessID);
                };
                source.Kernel.PerfInfoSample += e => {
                    total++;
                    // Account for ALL samples before rejecting samples without
                    // a usable thread ID; never silently change the denominator.
                    var utc=e.TimeStamp.ToUniversalTime();
                    if(utc<start)start=utc;
                    if(utc>end)end=utc;
                    var windowId=(utc.Ticks/TimeSpan.TicksPerSecond)/10;
                    Bucket b;
                    if(!windows.TryGetValue(windowId,out b)){
                        b=new Bucket();windows[windowId]=b;
                    }
                    if(e.ThreadID<=0){
                        idle++;
                        b.AddNoThreadId();
                        return;
                    }
                    int pid=e.ProcessID;
                    if(pid<0)pid=0;
                    if(pid>0) {
                        var n=Short(e.ProcessName);
                        if(Known(n) && !sampleNamesByPid.ContainsKey(pid))
                            sampleNamesByPid[pid]=n;
                    }
                    var key=Tuple.Create(pid,e.ThreadID,windowId);
                    long prev;
                    rawSamples.TryGetValue(key,out prev);
                    rawSamples[key]=prev+1;
                };
                source.Process();
            }
            // Resolve AFTER the entire ETL has been read. Avoid treating a TID
            // reused by multiple PIDs as a reliable fallback.
            foreach(var group in rawSamples) {
                var pid=group.Key.Item1;
                var tid=group.Key.Item2;
                var win=group.Key.Item3;
                var count=group.Value;
                if(pid>0)rawPidSamples+=count;
                else {
                    int candidate;
                    if(uniquePidByTid.TryGetValue(tid,out candidate) && candidate>0){
                        pid=candidate;
                        recoveredTidSamples+=count;
                    } else unresolvedPidSamples+=count;
                }
                string name=null;
                if(pid>0) {
                    if(!namesByPid.TryGetValue(pid,out name) || !Known(name)) {
                        sampleNamesByPid.TryGetValue(pid,out name);
                    }
                }
                if(!Known(name)) {
                    name="(unknown)";
                    unknown+=count;
                }
                all.Add(name,pid,count);
                windows[win].Add(name,pid,count);
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
            sb.AppendLine("Shares below are percent of samples WITH THREAD IDs, NOT total PC CPU %.");
            sb.AppendLine("ThreadID<=0 is counted separately (not assumed to measure all idle time).");
            sb.AppendLine("ETW may omit idle / DPC / ISR or lose samples: never infer total CPU %.");
            sb.AppendLine("Kernel samples, unresolved PID, and samples lost in ETW can affect attribution.");
            sb.AppendLine("ETW sampling is not a per-image CPU timer. Do not infer per-image speed.");
            sb.AppendLine("LogicalProcessors="+cpus);
            sb.AppendLine("SamplesAll="+total+" WithThreadID="+all.Samples+
                          " NoThreadID="+idle+" Unnamed="+unknown);
            sb.AppendLine("AttributionRawPID="+rawPidSamples+
                " DeferredUniqueTIDRecovered="+recoveredTidSamples+
                " PIDUnresolved="+unresolvedPidSamples+
                " ThreadIDsAmbiguous="+uniquePidByTid.Values.Count(x=>x<0));
            sb.AppendLine("Attribution is conservative: reused TIDs are NOT backfilled.");
            sb.AppendLine("Unknown PID/name must not be treated as idle or System.");
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
            sb.AppendLine("TOP 10-SECOND WINDOWS BY SAMPLES WITH THREAD ID");
            sb.AppendLine("10-second bins use UTC epoch boundaries; first/last bins may be partial.");
            sb.AppendLine("WITH THREAD ID / ALL-RECEIVED is event coverage, NOT total CPU %.");
            foreach(var win in windows.OrderByDescending(x=>x.Value.Samples).Take(16)) {
                var at=new DateTime(win.Key*10*TimeSpan.TicksPerSecond,DateTimeKind.Utc);
                sb.Append(at.ToString("o"))
                    .Append(" withThreadID=").Append(win.Value.Samples)
                    .Append(" allReceived=").Append(win.Value.TotalSamples)
                    .Append(" noThreadID=").Append(win.Value.NoThreadIdSamples)
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
