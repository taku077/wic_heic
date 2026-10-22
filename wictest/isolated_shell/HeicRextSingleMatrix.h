// CI only. Distinguish already-existing v0.4 single-image HEVC workers
// from new image-per-worker batch concurrency. In ONE original WIC Fant
// pipeline, cross product real single-image 1/2/3/4 libheif codec workers with
// scalar vs exact 10/12bit normalization, all originals read-only.
#pragma once
#include <array>
#include <map>
namespace RextSingleMatrix {
using Clock=std::chrono::steady_clock;
using Entry=RextDirectCorpus::PrivateEntry;
using SetWorkers=unsigned(__stdcall*)(unsigned);
using LookupFlag=RextDirectCorpus::LookupFlagFn;
using TimingReset=RextDirectCorpus::ResetFn;
using TimingSnapshot=RextDirectCorpus::SnapshotFn;
struct Codec {
 HMODULE dll=nullptr;
 Entry entry=nullptr;
 SetWorkers set=nullptr;
 LookupFlag lut=nullptr;
 TimingReset reset=nullptr;
 TimingSnapshot snapshot=nullptr;
};
struct Point {
 double elapsed=0,cpu=0;
};
double CpuMillis() {
 FILETIME a{},b{},k{},u{};
 Require(GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u)!=0,
         "RExt WIC CPU time unavailable");
 auto ticks=[](FILETIME f)->uint64_t{
  return uint64_t(f.dwLowDateTime)|(uint64_t(f.dwHighDateTime)<<32);
 };
 return double(ticks(k)+ticks(u))/10000.;
}
double Median4(std::vector<double> x){
 Require(x.size()==4,"RExt single-image ABBA requires 4 repeats per mode");
 std::sort(x.begin(),x.end());
 return (x[1]+x[2])*0.5;
}
void Run(const std::wstring& scalarPath,const std::wstring& lutPath,
         const std::wstring& root){
 const std::wstring temp=Env(L"RUNNER_TEMP"),workspace=Env(L"GITHUB_WORKSPACE");
 Require(Env(L"GITHUB_ACTIONS")==L"true" && !temp.empty() && !workspace.empty(),
         "Never benchmark untrusted photos or alter user WIC");
 const std::wstring dir=temp+L"\\heic_rext_direct_ab";
 Require(scalarPath==dir+L"\\WicHeicPrivateCodecScalar.dll" &&
         lutPath==dir+L"\\WicHeicPrivateCodec.dll" &&
         root==workspace+L"\\wictest\\samples",
         "Only fixed research DLLs and repo-owned HEIC sources permitted");
 Require(RextDirectCorpus::IsExactCiDll(scalarPath) &&
         RextDirectCorpus::IsExactCiDll(lutPath),
         "Private RExt research codec DLL absent");
 RextBatch::ComScope com;
 Codec codecs[2]{};
 auto init=[&](Codec& c,const std::wstring& path,unsigned flag){
  c.dll=LoadLibraryExW(path.c_str(),nullptr,
       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  Require(c.dll!=nullptr,"Cannot isolate private WIC decoder DLL");
  c.entry=reinterpret_cast<Entry>(GetProcAddress(c.dll,"DllGetClassObject"));
  c.set=reinterpret_cast<SetWorkers>(
       GetProcAddress(c.dll,"WicHeicRextResearchSetWorkers"));
  c.lut=reinterpret_cast<LookupFlag>(
       GetProcAddress(c.dll,"WicHeicRextLookupEnabled"));
  c.reset=reinterpret_cast<TimingReset>(
       GetProcAddress(c.dll,"WicHeicRextTimingReset"));
  c.snapshot=reinterpret_cast<TimingSnapshot>(
       GetProcAddress(c.dll,"WicHeicRextTimingSnapshot"));
  Require(c.entry&&c.set&&c.lut&&c.reset&&c.snapshot&&c.lut()==flag,
          "Scalar and LUT must be two truly different researched codec DLLs");
 };
 init(codecs[0],scalarPath,0);
 init(codecs[1],lutPath,1);
 ComPtr<IWICImagingFactory> wic;
 Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
      CLSCTX_INPROC_SERVER,IID_PPV_ARGS(wic.GetAddressOf())),
      "RExt single-image WIC factory");
 struct TestFile {const wchar_t* stem;const char* label;};
 constexpr TestFile files[]={
  {L"04","04_2048x2048_10bit"},
  {L"fixture-irot3-exif6-noaux","irot_1080x1920_10bit"},
  {L"03","03_alpha_1920x1080_10bit"},
 };
 for(const auto& file:files){
  const std::wstring path=root+L"\\"+file.stem+L".heic";
  const DWORD attributes=GetFileAttributesW(path.c_str());
  Require(attributes!=INVALID_FILE_ATTRIBUTES &&
          (attributes&FILE_ATTRIBUTE_DIRECTORY)==0 &&
          (attributes&FILE_ATTRIBUTE_REPARSE_POINT)==0,
          "Unrecognized HEIC input in single WIC test");
  std::vector<BYTE> reference;
  UINT width=0,height=0;
  for(unsigned threads:{1u,2u,3u,4u}){
   // Each source is instantiated afresh; WIC frame decode is never reused.
   // Alternate baseline and LUT in ABBA+BAAB order to reduce drift.
   const unsigned scheme[]={0,1,1,0,1,0,0,1};
   std::vector<double> wall[2],cpu[2],decode[2],nativeCopy[2],remaining[2],privateAfter[2];
   for(unsigned turn:scheme){
    Codec& codec=codecs[turn];
    Require(codec.set(threads)==1,
            "Refused CI-only 10bit HEVC worker selection");
    codec.reset(); // a fresh private WIC frame and one image at a time
    const double cpuStart=CpuMillis();
    const auto start=Clock::now();
    auto image=RextDirectCorpus::FromUnregisteredWic(codec.entry,path,wic.Get());
    const double wallMs=std::chrono::duration<double,std::milli>(
                       Clock::now()-start).count();
    const double cpuMs=CpuMillis()-cpuStart;
    unsigned long long decodeUs=0,copyUs=0,decodeCalls=0,copyCalls=0;
    codec.snapshot(&decodeUs,&copyUs,&decodeCalls,&copyCalls);
    Require(decodeCalls==1 && copyCalls>=1 && copyCalls<=65536,
            "One private WIC thumbnail must decode HEVC exactly once");
    const double decodeMs=double(decodeUs)/1000.;
    const double nativeCopyMs=double(copyUs)/1000.;
    // Outer wall and nested QPC counters may differ by clock overhead.
    Require(decodeMs+nativeCopyMs<=wallMs+3.0,
            "Invalid nested HEVC RGB / WIC copy stage counters");
    const double residualMs=wallMs-decodeMs-nativeCopyMs;
    const double restMs=(residualMs>0.0)?residualMs:0.0;
    // Snapshot AFTER the request; never call this a process peak.
    const double privateMiB=RextBatch::PrivateCommitMiB();
    Require(privateMiB>0.0,"Invalid private committed memory snapshot");
    Require(image.width>0 && image.height>0 &&
            image.pixels.size()==size_t(image.width)*image.height*4,
            "Bad WIC PBGRA buffer under threading");
    if(reference.empty()){
     reference=image.pixels;width=image.width;height=image.height;
    }else{
     Require(image.width==width&&image.height==height &&
             image.pixels==reference,
             "RExt threading plus LUT changed 256px WIC Fant PBGRA pixels");
    }
    wall[turn].push_back(wallMs);
    cpu[turn].push_back(cpuMs);
    decode[turn].push_back(decodeMs);
    nativeCopy[turn].push_back(nativeCopyMs);
    remaining[turn].push_back(restMs);
    privateAfter[turn].push_back(privateMiB);
   }
   for(unsigned turn:{0u,1u}){
    const double duration=Median4(wall[turn]);
    const double cpuWork=Median4(cpu[turn]);
    std::printf("HEIC_REXT_SINGLE_MATRIX sample=%s libheif_threads=%u color=%s repeats=4 wic_fant_pbgra_ms=%.3f process_cpu_ms=%.3f bytes=%llu exact_original_256px_pbgra=1 same_file_single_image=1 no_shell_queue=1 research_only=1\n",
      file.label,threads,turn?"lut":"scalar",duration,cpuWork,
      static_cast<unsigned long long>(reference.size()));
    std::printf("HEIC_REXT_SINGLE_STAGES sample=%s libheif_threads=%u color=%s repeats=4 hevc_rgb_ms=%.3f native_wic_copy_ms=%.3f remaining_setup_fant_pbgra_ms=%.3f process_private_committed_after_mib=%.3f all_three_stages_separate_medians=1 decoded_once_each=1 byte_exact_pbgra=1 research_only=1 explorer_not_measured=1\n",
      file.label,threads,turn?"lut":"scalar",
      Median4(decode[turn]),Median4(nativeCopy[turn]),
      Median4(remaining[turn]),Median4(privateAfter[turn]));
   }
  }
 }
 for(auto& c:codecs)if(c.dll)FreeLibrary(c.dll);
 std::puts("HEIC_REXT_SINGLE_3_REAL_HEIC_6_MODE_ALL_WIC_PBGRA_BYTE_EXACT_PASS");
}
} // namespace RextSingleMatrix