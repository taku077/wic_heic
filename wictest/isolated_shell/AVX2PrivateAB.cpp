// GitHub Actions only: HEIC first-thumbnail CPU/WIC A/B benchmark.
// Never registers a codec, never writes or modifies image files.
// Only accepts artificial 4032x3024 HEIC generated under RUNNER_TEMP.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../wic_heic/wic_heic.h"
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"psapi.lib")
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
namespace {
void Check(HRESULT code,const char* stage) {
 if(FAILED(code)) { std::printf("AVX2_BENCH_FAILED stage=%s hr=%08X\n",stage,unsigned(code)); throw std::runtime_error(stage); }
}
void Require(bool ok,const char* stage) {
 if(!ok) { std::printf("AVX2_BENCH_FAILED stage=%s\n",stage); throw std::runtime_error(stage); }
}
std::wstring Env(const wchar_t* name) {
 DWORD n=GetEnvironmentVariableW(name,nullptr,0);
 if(n==0||n>32000)return {};
 std::wstring result(n,L'\0');
 DWORD count=GetEnvironmentVariableW(name,&result[0],n);
 if(!count||count>=n)return {};
 result.resize(count);return result;
}
uint64_t CpuTicks() {
 FILETIME created{},ended{},kernel{},user{};
 Require(GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user)!=0,"CPU timing");
 auto q=[](FILETIME f)->uint64_t{return uint64_t(f.dwLowDateTime)|(uint64_t(f.dwHighDateTime)<<32);};
 return q(kernel)+q(user);
}
uint64_t Hash(const std::vector<BYTE>& image) {
 uint64_t h=14695981039346656037ull;
 for(BYTE b:image){h^=b;h*=1099511628211ull;}
 return h;
}
using Entry=HRESULT(STDAPICALLTYPE*)(REFCLSID,REFIID,LPVOID*);
struct Sample{double ms=0,cpu=0;uint64_t hash=0;size_t privateUse=0;};
Sample Run(Entry entry,const std::wstring& file) {
 ComPtr<IClassFactory> component;
 Check(entry(CLSID_HEICDecoder,IID_IClassFactory,
       reinterpret_cast<LPVOID*>(component.GetAddressOf())),"Private factory");
 ComPtr<IWICBitmapDecoder> decoder;
 Check(component->CreateInstance(nullptr,IID_PPV_ARGS(decoder.GetAddressOf())),"Decoder instance");
 ComPtr<IWICImagingFactory> imaging;
 Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(imaging.GetAddressOf())),"WIC factory");
 ComPtr<IWICStream> input;
 Check(imaging->CreateStream(input.GetAddressOf()),"IStream");
 Check(input->InitializeFromFilename(file.c_str(),GENERIC_READ),"input read");
 Check(decoder->Initialize(input.Get(),WICDecodeMetadataCacheOnDemand),"WIC Initialize");
 ComPtr<IWICBitmapFrameDecode> frame;
 Check(decoder->GetFrame(0,frame.GetAddressOf()),"WIC GetFrame");
 UINT w=0,h=0;
 Check(frame->GetSize(&w,&h),"Frame size");
 Require(w==4032&&h==3024,"unexpected synthetic dimensions");
 ComPtr<IWICBitmapScaler> scaler;
 Check(imaging->CreateBitmapScaler(scaler.GetAddressOf()),"WIC Scaler");
 Check(scaler->Initialize(frame.Get(),256,192,WICBitmapInterpolationModeFant),"scaler Initialize");
 ComPtr<IWICFormatConverter> converter;
 Check(imaging->CreateFormatConverter(converter.GetAddressOf()),"WIC Converter");
 Check(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,
    WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"pixel converter Initialize");
 std::vector<BYTE> bytes(256u*192u*4u);
 const uint64_t cpuBefore=CpuTicks();
 const auto started=Clock::now();
 Check(converter->CopyPixels(nullptr,256u*4u,UINT(bytes.size()),bytes.data()),"WIC CopyPixels");
 const auto ended=Clock::now();
 const uint64_t cpuAfter=CpuTicks();
 PROCESS_MEMORY_COUNTERS_EX memory{};
 memory.cb=sizeof(memory);
 Require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
          sizeof(memory))!=0,"Process memory");
 Sample s;
 s.ms=std::chrono::duration<double,std::milli>(ended-started).count();
 s.cpu=double(cpuAfter-cpuBefore)/10000.;
 s.hash=Hash(bytes);
 s.privateUse=size_t(memory.PrivateUsage);
 return s;
}
double Median(std::vector<double> a){
 std::sort(a.begin(),a.end());return (a[a.size()/2-1]+a[a.size()/2])/2;
}
}
int wmain(int argc,wchar_t** argv){
 try{
  Require(argc==4,"usage: baselinePrivateDLL optimizedPrivateDLL generatedSyntheticHEIC");
  const auto work=Env(L"RUNNER_TEMP");
  const std::wstring input=argv[3];
  Require(Env(L"GITHUB_ACTIONS")==L"true" && !work.empty() &&
       input==work+L"\\wicfast_synthetic_4032x3024.heic",
       "only RUNNER_TEMP artificial input allowed");
  Require(GetFileAttributesW(input.c_str())!=INVALID_FILE_ATTRIBUTES,"input missing");
  Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM initialize");
  HMODULE baseline=LoadLibraryExW(argv[1],nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  Require(baseline!=nullptr,"baseline private DLL load");
  HMODULE optimized=LoadLibraryExW(argv[2],nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  Require(optimized!=nullptr,"optimized private DLL load");
  const auto baseEntry=reinterpret_cast<Entry>(GetProcAddress(baseline,"DllGetClassObject"));
  const auto avxEntry=reinterpret_cast<Entry>(GetProcAddress(optimized,"DllGetClassObject"));
  Require(baseEntry && avxEntry,"one of two COM factories missing");
  int cpuInfo[4]={};__cpuid(cpuInfo,1);
  const bool osxsave=(cpuInfo[2]&(1<<27))!=0;
  const bool avx=(cpuInfo[2]&(1<<28))!=0;
  bool avx2=false;
  if(osxsave&&avx&&((_xgetbv(0)&6u)==6u)){
    __cpuidex(cpuInfo,7,0);
    avx2=(cpuInfo[1]&(1<<5))!=0;
  }
  std::printf("AVX2_BENCH_CPU_AVAILABLE=%d\n",int(avx2));
  const int order[]={0,1,1,0,1,0,0,1,0,1,1,0};
  std::vector<double> elapsed[2],cpuTime[2];
  uint64_t reference=0;
  for(int i=0;i<12;++i){
    const int mode=order[i];
    Sample sample=Run(mode?avxEntry:baseEntry,input);
    if(i==0)reference=sample.hash;
    Require(sample.hash==reference,"AVX2 pixel parity failure against baseline");
    elapsed[mode].push_back(sample.ms);
    cpuTime[mode].push_back(sample.cpu);
    std::printf("AVX2_BENCH_SAMPLE mode=%s index=%d copy_ms=%.2f cpu_ms=%.2f private_mib=%.2f pixel_hash=%016llX\n",
        mode?"avx2":"baseline",i,sample.ms,sample.cpu,
        double(sample.privateUse)/(1024*1024),
        static_cast<unsigned long long>(sample.hash));
  }
  Require(elapsed[0].size()==6&&elapsed[1].size()==6,"unbalanced A/B trials");
  const double baseWall=Median(elapsed[0]),optWall=Median(elapsed[1]);
  const double baseCpu=Median(cpuTime[0]),optCpu=Median(cpuTime[1]);
  std::printf("AVX2_BENCH_MEDIAN baseline_ms=%.2f avx2_ms=%.2f improvement_pct=%.2f baseline_cpu_ms=%.2f avx2_cpu_ms=%.2f\n",
       baseWall,optWall,100.0*(baseWall-optWall)/baseWall,baseCpu,optCpu);
  std::printf("AVX2_BENCH_PIXEL_PARITY_PASSED pixel_hash=%016llX\n",
       static_cast<unsigned long long>(reference));
  FreeLibrary(optimized);FreeLibrary(baseline);CoUninitialize();
  std::puts("AVX2_BENCH_SYNTHETIC_READONLY_PASS");
  return 0;
 }catch(const std::exception& e){
  std::printf("AVX2_BENCH_FATAL stage=%s\n",e.what());return 1;
 }catch(...){std::puts("AVX2_BENCH_FATAL unknown");return 2;}
}
