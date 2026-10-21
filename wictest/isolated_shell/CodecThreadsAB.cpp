// CI-only private HEVC decoder thread A/B benchmark.
// No registry modifications, no Explorer GUI, no user HEIC content.
// Artificial 4032x3024 HEIC in RUNNER_TEMP; compare exact PBGRA pixels.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../wic_heic/wic_heic.h"
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"psapi.lib")
using Microsoft::WRL::ComPtr;
namespace {
using Clock=std::chrono::steady_clock;
const int kRuns=8;
int kModes[kRuns]={0,1,2,4,4,2,1,0};
void Check(HRESULT hr,const char* step) {
 if(FAILED(hr)) {
   std::printf("CODEC_THREADS_AB_FAILED step=%s hr=0x%08X\n",step,unsigned(hr));
   throw std::runtime_error(step);
 }
}
void Require(bool ok,const char* step) {
 if(!ok) {
   std::printf("CODEC_THREADS_AB_FAILED step=%s\n",step);
   throw std::runtime_error(step);
 }
}
std::wstring Env(const wchar_t* name) {
 DWORD n=GetEnvironmentVariableW(name,nullptr,0);
 if(n==0||n>32767)return {};
 std::wstring out(n,L'\0');
 DWORD actual=GetEnvironmentVariableW(name,&out[0],n);
 if(actual==0||actual>=n)return {};
 out.resize(actual);
 return out;
}
void Guard(const std::wstring& path) {
 Require(Env(L"GITHUB_ACTIONS")==L"true","GitHub Actions exclusively");
 const auto root=Env(L"RUNNER_TEMP");
 Require(!root.empty()&&path==root+L"\\wicfast_synthetic_4032x3024.heic",
    "only generated temporary 12MP fixture allowed");
 Require(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES,
    "synthetic 12MP source missing");
}
uint64_t FileTimeTo100ns(const FILETIME& t) {
 return uint64_t(t.dwLowDateTime)|(uint64_t(t.dwHighDateTime)<<32);
}
uint64_t CpuTime100ns() {
 FILETIME create{},exit{},kernel{},user{};
 Require(GetProcessTimes(GetCurrentProcess(),&create,&exit,&kernel,&user)!=0,"CPU times unavailable");
 return FileTimeTo100ns(kernel)+FileTimeTo100ns(user);
}
uint64_t HashPixels(const std::vector<BYTE>& bytes) {
 uint64_t hash=14695981039346656037ull;
 for(BYTE b:bytes){hash^=b;hash*=1099511628211ull;}
 return hash;
}
struct Sample {
 int threads=0;
 double copyMs=0;
 double cpuMs=0;
 size_t privateBytes=0;
 uint64_t hash=0;
};
Sample Test(IClassFactory* component, const std::wstring& fixture,int threads) {
 const auto v=std::to_wstring(threads);
 Require(SetEnvironmentVariableW(L"WIC_HEIC_PRIVATE_CODEC_THREADS",v.c_str())!=0,
         "failed to set isolated trial mode");
 ComPtr<IWICBitmapDecoder> decoder;
 Check(component->CreateInstance(nullptr,IID_PPV_ARGS(decoder.GetAddressOf())),"decode COM instance");
 ComPtr<IWICImagingFactory> factory;
 Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(factory.GetAddressOf())),"factory");
 ComPtr<IWICStream> stream;
 Check(factory->CreateStream(stream.GetAddressOf()),"stream");
 Check(stream->InitializeFromFilename(fixture.c_str(),GENERIC_READ),"open");
 Check(decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand),"initialize");
 ComPtr<IWICBitmapFrameDecode> frame;
 Check(decoder->GetFrame(0,frame.GetAddressOf()),"frame");
 UINT w=0,h=0;
 Check(frame->GetSize(&w,&h),"dimensions");
 Require(w==4032&&h==3024,"unexpected synthetic dimensions");
 const UINT target=256;
 const UINT tw=256;
 const UINT th=UINT(uint64_t(h)*target/w);
 const uint64_t n=uint64_t(tw)*th*4u;
 Require(n<1024*1024u,"thumbnail size limit");
 ComPtr<IWICBitmapScaler> scaler;
 Check(factory->CreateBitmapScaler(scaler.GetAddressOf()),"scaler");
 Check(scaler->Initialize(frame.Get(),tw,th,WICBitmapInterpolationModeFant),"scaler init");
 ComPtr<IWICFormatConverter> converter;
 Check(factory->CreateFormatConverter(converter.GetAddressOf()),"converter");
 Check(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,
           WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"converter init");
 std::vector<BYTE> bytes(size_t(n),0);
 const uint64_t cpuBefore=CpuTime100ns();
 const auto before=Clock::now();
 Check(converter->CopyPixels(nullptr,tw*4u,UINT(n),bytes.data()),"CopyPixels");
 const auto after=Clock::now();
 const uint64_t cpuAfter=CpuTime100ns();
 PROCESS_MEMORY_COUNTERS_EX memory{};
 memory.cb=sizeof(memory);
 Require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                               sizeof(memory))!=0,"process memory");
 Sample sample;
 sample.threads=threads;
 sample.copyMs=std::chrono::duration<double,std::milli>(after-before).count();
 sample.cpuMs=double(cpuAfter-cpuBefore)/10000.0;
 sample.privateBytes=size_t(memory.PrivateUsage);
 sample.hash=HashPixels(bytes);
 return sample;
}
}
int wmain(int argc,wchar_t** argv) {
 try {
  Require(argc==3,"usage: codecDLL generatedSynthetic12MP");
  const std::wstring input=argv[2];
  Guard(input);
  Check(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"CoInitializeEx");
  const HMODULE module=LoadLibraryExW(argv[1],nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  Require(module!=nullptr,"private codec DLL not loadable");
  using FactoryFn=HRESULT(STDAPICALLTYPE*)(REFCLSID,REFIID,LPVOID*);
  auto entry=reinterpret_cast<FactoryFn>(GetProcAddress(module,"DllGetClassObject"));
  Require(entry!=nullptr,"private codec class factory unavailable");
  ComPtr<IClassFactory> component;
  Check(entry(CLSID_HEICDecoder,IID_IClassFactory,
        reinterpret_cast<LPVOID*>(component.GetAddressOf())),"private HEIC factory");
  std::vector<Sample> samples;
  for(int t:kModes) {
    const auto s=Test(component.Get(),input,t);
    if(!samples.empty())Require(s.hash==samples[0].hash,
          "HEIC thumbnail pixel-by-pixel hash differs across decode threads");
    std::printf("CODEC_THREADS_AB_SAMPLE threads=%d copy_ms=%.2f cpu_ms=%.2f private_mib=%.1f pixel_hash=%016llX\n",
      t,s.copyMs,s.cpuMs,double(s.privateBytes)/(1024.0*1024.0),
      static_cast<unsigned long long>(s.hash));
    samples.push_back(s);
  }
  for(int threads:{0,1,2,4}) {
    std::vector<double> times,cpus;
    for(const auto& s:samples) {
      if(s.threads==threads){times.push_back(s.copyMs);cpus.push_back(s.cpuMs);}
    }
    Require(times.size()==2,"unbalanced A/B sample counts");
    std::sort(times.begin(),times.end());
    std::sort(cpus.begin(),cpus.end());
    const double median=(times[0]+times[1])/2;
    const double cpuMedian=(cpus[0]+cpus[1])/2;
    std::printf("CODEC_THREADS_AB_SUMMARY threads=%d count=2 median_ms=%.2f min_ms=%.2f max_ms=%.2f cpu_median_ms=%.2f\n",
       threads,median,times.front(),times.back(),cpuMedian);
  }
  Require(SetEnvironmentVariableW(L"WIC_HEIC_PRIVATE_CODEC_THREADS",nullptr)!=0,
          "trial env cleanup failed");
  component.Reset();
  FreeLibrary(module);
  CoUninitialize();
  std::puts("CODEC_THREADS_AB_PIXEL_PARITY_PASS");
  std::puts("CODEC_THREADS_AB_ISOLATED_PERF_RECORDED");
  return 0;
 } catch(const std::exception& e) {
  std::printf("CODEC_THREADS_AB_FATAL step=%s\n",e.what());
  return 1;
 } catch(...) {std::puts("CODEC_THREADS_AB_FATAL unknown");return 2;}
}
