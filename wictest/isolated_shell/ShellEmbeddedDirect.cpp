// CI-only direct Shell COM test. Does NOT register a ShellEx or install MSI.
// Uses only artificial RUNNER_TEMP HEIC and pinned, repo-owned orientation sample.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <thumbcache.h>
#include <shobjidl.h>
#include <wrl/client.h>
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
#pragma comment(lib,"gdi32.lib")
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
namespace {
constexpr CLSID kPreviewClsid =
 {0xd973196c,0x165c,0x4fce,{0x88,0x38,0x17,0xc3,0x70,0x37,0xd3,0x84}};
using FactoryFn=HRESULT(STDAPICALLTYPE*)(REFCLSID,REFIID,LPVOID*);
void Require(bool condition,const char* what){
 if(!condition){std::printf("SHELL_EMBEDDED_FAILED %s\n",what);throw std::runtime_error(what);}
}
void Check(HRESULT code,const char* what) {
 if(FAILED(code)){
  std::printf("SHELL_EMBEDDED_FAILED %s hr=0x%08X\n",what,unsigned(code));
  throw std::runtime_error(what);
 }
}
std::wstring Env(const wchar_t* n){
 const DWORD capacity=GetEnvironmentVariableW(n,nullptr,0);
 if(!capacity||capacity>32767)return {};
 std::wstring v(capacity,L'\0');
 const DWORD nread=GetEnvironmentVariableW(n,&v[0],capacity);
 if(!nread||nread>=capacity)return {};
 v.resize(nread);return v;
}
void Guard(const std::wstring& path,const wchar_t* name) {
 const auto runner=Env(L"RUNNER_TEMP");
 Require(Env(L"GITHUB_ACTIONS")==L"true" &&
   !runner.empty() && path==runner+L"\\"+name,
   "only CI-generated artificial HEIC can be opened");
 Require(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES,"source missing");
}
struct Result{
 double elapsedMs=0;
 UINT width=0,height=0;
 WTS_ALPHATYPE alpha=WTSAT_UNKNOWN;
 uint64_t pixelHash=0;
 std::vector<BYTE> pixels;
};
Result Capture(FactoryFn entry,const std::wstring& path,UINT size) {
 ComPtr<IClassFactory> factory;
 Check(entry(kPreviewClsid,IID_IClassFactory,
         reinterpret_cast<void**>(factory.GetAddressOf())),"shell class factory");
 ComPtr<IThumbnailProvider> provider;
 Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(provider.GetAddressOf())),"shell provider instance");
 ComPtr<IInitializeWithStream> initialize;
 Check(provider.As(&initialize),"Shell IInitializeWithStream QI");
 ComPtr<IWICImagingFactory> imaging;
 Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(imaging.GetAddressOf())),"WIC factory");
 ComPtr<IWICStream> source;
 Check(imaging->CreateStream(source.GetAddressOf()),"stream create");
 Check(source->InitializeFromFilename(path.c_str(),GENERIC_READ),"read-only source");
 Check(initialize->Initialize(source.Get(),STGM_READ),"initialize Shell stream");
 HBITMAP bitmap=nullptr;
 WTS_ALPHATYPE alpha=WTSAT_UNKNOWN;
 const auto started=Clock::now();
 const HRESULT hr=provider->GetThumbnail(size,&bitmap,&alpha);
 const double elapsed=std::chrono::duration<double,std::milli>(
                   Clock::now()-started).count();
 if(FAILED(hr)) {
   if(bitmap)DeleteObject(bitmap);
   Check(hr,"Shell IThumbnailProvider::GetThumbnail");
 }
 Require(bitmap!=nullptr,"thumbnail HBITMAP missing");
 DIBSECTION dib{};
 const int copied=GetObjectW(bitmap,sizeof(dib),&dib);
 if(copied!=sizeof(dib) || !dib.dsBm.bmBits ||
    dib.dsBm.bmBitsPixel!=32 || dib.dsBm.bmWidth<=0 ||
    dib.dsBm.bmHeight<=0 || dib.dsBm.bmWidth>1024 ||
    dib.dsBm.bmHeight>1024){
   DeleteObject(bitmap);
   throw std::runtime_error("Invalid DIB thumbnail");
 }
 Result result;
 result.elapsedMs=elapsed;
 result.width=UINT(dib.dsBm.bmWidth);
 result.height=UINT(dib.dsBm.bmHeight);
 result.alpha=alpha;
 const size_t bytes=size_t(dib.dsBm.bmWidthBytes)*size_t(dib.dsBm.bmHeight);
 Require(bytes>0 && bytes<16*1024*1024,"DIB byte size invalid");
 const auto data=static_cast<const BYTE*>(dib.dsBm.bmBits);
 result.pixels.assign(data,data+bytes);
 result.pixelHash=14695981039346656037ull;
 for(BYTE b:result.pixels){result.pixelHash^=b;result.pixelHash*=1099511628211ull;}
 DeleteObject(bitmap);
 return result;
}
double Median(std::vector<double> a) {
 std::sort(a.begin(),a.end());
 return (a[a.size()/2]+a[a.size()/2-1])/2.;
}
}
int wmain(int argc,wchar_t** argv){
 try{
  Require(argc==4,"usage: ShellDll syntheticBase syntheticWithThumb");
  const std::wstring base=argv[2],with=argv[3];
  Guard(base,L"wicfast_baseline_4032x3024.heic");
  Guard(with,L"wicfast_embedded_4032x3024.heic");
  Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM initialization");
  HMODULE module=LoadLibraryExW(argv[1],nullptr,
       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  Require(module!=nullptr,"Shell DLL cannot load");
  auto entry=reinterpret_cast<FactoryFn>(GetProcAddress(module,"DllGetClassObject"));
  Require(entry!=nullptr,"Shell COM export missing");
  std::vector<double> full,embeddedTimes;
  for(int i=0;i<8;++i){
   bool embed=(i%4==1||i%4==2);
   auto got=Capture(entry,embed?with:base,256);
   Require(got.width==256 && got.height==192,"bad 256px geometry");
   Require(got.alpha==WTSAT_ARGB || got.alpha==WTSAT_RGB,
           "invalid Shell WTS_ALPHATYPE");
   (embed?embeddedTimes:full).push_back(got.elapsedMs);
   std::printf("SHELL_EMBEDDED_SAMPLE route=%s time_ms=%.2f hash=%016llx\n",
       embed?"embedded":"full",got.elapsedMs,
       static_cast<unsigned long long>(got.pixelHash));
  }
  Require(full.size()==4&&embeddedTimes.size()==4,"unbalanced A/B trial");
  std::printf("SHELL_EMBEDDED_MEDIAN full_ms=%.2f embedded_ms=%.2f saving_pct=%.2f\n",
        Median(full),Median(embeddedTimes),100.0*(1.0-Median(embeddedTimes)/Median(full)));
  const auto original=Capture(entry,base,256);
  const auto accelerated=Capture(entry,with,256);
  Require(original.pixels.size()==accelerated.pixels.size(),"Shell output dimensions differ");
  uint64_t absSum=0;
  for(size_t k=0;k<original.pixels.size();++k){
    int delta=int(original.pixels[k])-int(accelerated.pixels[k]);
    absSum+=unsigned(delta<0?-delta:delta);
  }
  std::printf("SHELL_EMBEDDED_QUALITY average_byte_abs_diff=%.3f\n",
        double(absSum)/double(original.pixels.size()));
  // When request exceeds the experiment's 512px fast-path cap, the original
  // image decode route must be used even if a large embedded image exists.
  const auto bigMain=Capture(entry,base,768);
  const auto bigThumb=Capture(entry,with,768);
  Require(bigMain.pixels==bigThumb.pixels,
       "768px cap fallback failed to preserve original rendering");
  std::puts("SHELL_EMBEDDED_768PX_FULL_FALLBACK_PIXEL_PARITY_PASSED");
  FreeLibrary(module);
  CoUninitialize();
  std::puts("SHELL_EMBEDDED_CI_ONLY_NO_REGISTRY_NO_MSI_PASSED");
  return 0;
 }catch(const std::exception& e){
   std::printf("SHELL_EMBEDDED_FATAL %s\n",e.what());
   return 1;
 }catch(...){std::puts("SHELL_EMBEDDED_FATAL unknown");return 2;}
}
