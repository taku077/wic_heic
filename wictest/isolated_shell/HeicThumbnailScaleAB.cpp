// CI-only thumbnail scale A/B: WIC Fant of full-resolution BGRA vs
// direct YUV420P10LE -> 256px BGRA through swscale SWS_AREA.
// Fixed synthetic HEVC test input only; no registration, MSI, HEIC writes,
// external picture upload, or change to Windows Photos/Explorer/HEIC Manager.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
}
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"swscale.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"psapi.lib")
namespace {
using Clock=std::chrono::steady_clock;
using Bytes=std::vector<uint8_t>;
using Microsoft::WRL::ComPtr;
constexpr int W=4032,H=3024,TW=256,TH=192;
void Need(bool ok,const char* reason){
 if(!ok){std::printf("HEIC_THUMB_SCALE_FATAL reason=%s\n",reason);throw std::runtime_error(reason);}
}
void Ok(HRESULT hr,const char* step) {Need(SUCCEEDED(hr),step);}
std::wstring Env(const wchar_t* name){
 const DWORD n=GetEnvironmentVariableW(name,nullptr,0);
 if(!n||n>32000)return {};
 std::wstring s(n,L'\0');
 const DWORD r=GetEnvironmentVariableW(name,&s[0],n);
 if(!r||r>=n)return {};
 s.resize(r);return s;
}
Bytes ReadExact(const std::wstring& path,size_t expected){
 FILE* raw=nullptr;
 Need(_wfopen_s(&raw,path.c_str(),L"rb")==0 && raw,"reference open");
 const std::unique_ptr<FILE,decltype(&fclose)> file(raw,&fclose);
 Need(_fseeki64(file.get(),0,SEEK_END)==0,"reference size seek");
 const auto len=_ftelli64(file.get());
 Need(len>0 && uint64_t(len)==expected,"reference input dimensions mismatch");
 Need(_fseeki64(file.get(),0,SEEK_SET)==0,"reference rewind");
 Bytes b(expected);
 Need(fread(b.data(),1,b.size(),file.get())==b.size(),"reference read failed");
 return b;
}
uint64_t PrivateBytes(){
 PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);
 Need(GetProcessMemoryInfo(GetCurrentProcess(),
   reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m))!=0,"memory counters");
 return m.PrivateUsage;
}
struct SwsDel {void operator()(SwsContext* p)const{sws_freeContext(p);}};
using Sws=std::unique_ptr<SwsContext,SwsDel>;
struct Timed {Bytes output;double ms=0;uint64_t privateBytes=0;};
Timed WicFant(const Bytes& bgra, IWICImagingFactory* factory){
 auto start=Clock::now();
 ComPtr<IWICBitmap> source;
 Ok(factory->CreateBitmapFromMemory(W,H,GUID_WICPixelFormat32bppBGRA,W*4,
   UINT(bgra.size()),const_cast<BYTE*>(bgra.data()),source.GetAddressOf()),
   "WIC bitmap from BGRA");
 ComPtr<IWICBitmapScaler> scaler;
 Ok(factory->CreateBitmapScaler(scaler.GetAddressOf()),"WIC scaler create");
 Ok(scaler->Initialize(source.Get(),TW,TH,WICBitmapInterpolationModeFant),
   "WIC Fant initialize");
 Timed result;result.output.resize(size_t(TW)*TH*4);
 Ok(scaler->CopyPixels(nullptr,TW*4,UINT(result.output.size()),result.output.data()),
   "WIC Fant CopyPixels");
 result.ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
 result.privateBytes=PrivateBytes();return result;
}
Timed DirectYuvArea(const Bytes& yuv10){
 const size_t luma=size_t(W)*H*2, chroma=size_t(W/2)*(H/2)*2;
 Need(yuv10.size()==luma+chroma*2,"unexpected YUV420P10 plane sizes");
 const uint8_t* src[4]={yuv10.data(),yuv10.data()+luma,
   yuv10.data()+luma+chroma,nullptr};
 const int strides[4]={W*2,W,W,0};
 auto start=Clock::now();
 Sws ctx(sws_getContext(W,H,AV_PIX_FMT_YUV420P10LE,TW,TH,
   AV_PIX_FMT_BGRA,SWS_AREA,nullptr,nullptr,nullptr));
 Need(ctx!=nullptr,"swscale area context");
 const int* srcMatrix=sws_getCoefficients(SWS_CS_ITU601);
 const int* dstMatrix=sws_getCoefficients(SWS_CS_DEFAULT);
 Need(srcMatrix && dstMatrix &&
   sws_setColorspaceDetails(ctx.get(),srcMatrix,0,dstMatrix,1,0,1<<16,1<<16)>=0,
   "swscale limited-range ITU601 coefficients");
 Timed out;out.output.resize(size_t(TW)*TH*4);
 uint8_t* dest[4]={out.output.data(),nullptr,nullptr,nullptr};
 int destStride[4]={TW*4,0,0,0};
 Need(sws_scale(ctx.get(),src,strides,0,H,dest,destStride)==TH,
   "direct YUV to 256px BGRA conversion");
 out.ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
 out.privateBytes=PrivateBytes();return out;
}
double Median(std::vector<double> v){
 Need(v.size()==4,"median requires 4 paired repetitions");
 std::sort(v.begin(),v.end());return (v[1]+v[2])/2.0;
}
void Audit(const Bytes& wic,const Bytes& area){
 Need(wic.size()==area.size() && wic.size()==size_t(TW)*TH*4,
   "thumbnail output geometry mismatch");
 uint64_t absSum=0,squareSum=0,changed=0,over4=0,over16=0;
 unsigned maxError=0,nonOpaqueWic=0,nonOpaqueArea=0;
 for(size_t p=0;p<wic.size();p+=4){
  if(wic[p+3]!=255)++nonOpaqueWic;
  if(area[p+3]!=255)++nonOpaqueArea;
  for(unsigned c=0;c<3;++c){
   const int dif=std::abs(int(wic[p+c])-int(area[p+c]));
   absSum+=unsigned(dif);squareSum+=uint64_t(dif)*dif;
   changed+=dif!=0;over4+=dif>4;over16+=dif>16;
   maxError=std::max(maxError,unsigned(dif));
  }
 }
 const double samples=double(TW)*TH*3.0;
 const double mse=double(squareSum)/samples;
 const double mae=double(absSum)/samples;
 const double psnr=mse==0?99.0:10.0*std::log10(255.0*255.0/mse);
 std::printf("HEIC_THUMB_SCALE_QUALITY w=%d h=%d all_pixels_exact=%d mae_rgb=%.6f psnr_db=%.3f max_error=%u channels_changed=%llu over4=%llu over16=%llu alpha_nonopaque_wic=%u alpha_nonopaque_area=%u\n",
   TW,TH,int(absSum==0),mae,psnr,maxError,
   static_cast<unsigned long long>(changed),
   static_cast<unsigned long long>(over4),
   static_cast<unsigned long long>(over16),nonOpaqueWic,nonOpaqueArea);
 // No production selection is made on synthetic measurements alone.
 std::puts("HEIC_THUMB_SCALE_RESEARCH_ONLY_NOT_PRODUCTION_QUALITY_APPROVED");
 Need(nonOpaqueWic==0 && nonOpaqueArea==0,"opaque Main10 fixture unexpectedly has alpha");
}
}
int wmain(int argc,wchar_t** argv){
 try{
  Need(argc==2 && std::wstring(argv[1])==L"--synthetic12","CI-only fixed fixture mode");
  Need(Env(L"GITHUB_ACTIONS")==L"true","CI only, not local test or installer");
  const auto temp=Env(L"RUNNER_TEMP");Need(!temp.empty(),"runner temp missing");
  const std::wstring base=temp+L"\\heic_inprocess_software12\\synthetic12mp";
  const Bytes bgra=ReadExact(base+L".bgra",size_t(W)*H*4);
  const Bytes yuv=ReadExact(base+L".yuv420p10le",size_t(W)*H*3);
  const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  Need(SUCCEEDED(init),"CoInitializeEx");
  struct Uninit{~Uninit(){CoUninitialize();}} cleanup;
  ComPtr<IWICImagingFactory> factory;
  Ok(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
       IID_PPV_ARGS(factory.GetAddressOf())),"Windows WIC factory");
  // Warm up each path once before ABBA. No process startup or HEVC decode
  // in the timed region; times are scaler/color conversion only.
  const auto warmWic=WicFant(bgra,factory.Get());
  const auto warmArea=DirectYuvArea(yuv);
  std::vector<double> wicTimes,areaTimes;
  Bytes ref=warmWic.output, direct=warmArea.output;
  uint64_t maxPrivateBytes=std::max(warmWic.privateBytes,warmArea.privateBytes);
  const int order[8]={0,1,1,0,0,1,1,0};
  for(int i=0;i<8;++i){
   const bool fast=order[i]==1;
   Timed run=fast?DirectYuvArea(yuv):WicFant(bgra,factory.Get());
   if(fast) {
    Need(run.output==direct,"nondeterministic direct Area scaling");
    areaTimes.push_back(run.ms);
   }else{
    Need(run.output==ref,"nondeterministic WIC Fant reference");
    wicTimes.push_back(run.ms);
   }
   maxPrivateBytes=std::max(maxPrivateBytes,run.privateBytes);
   std::printf("HEIC_THUMB_SCALE_TRIAL method=%s repetition=%d wall_ms=%.3f private_bytes=%llu\n",
     fast?"YUV_AREA":"WIC_FANT",i,run.ms,
     static_cast<unsigned long long>(run.privateBytes));
  }
  Audit(ref,direct);
  const double mW=Median(wicTimes),mA=Median(areaTimes);
  std::printf("HEIC_THUMB_SCALE_MEDIAN wic_fant_ms=%.3f yuv_area_ms=%.3f delta_percent=%.2f max_private_bytes=%llu\n",
    mW,mA,100.0*(mW-mA)/mW,
    static_cast<unsigned long long>(maxPrivateBytes));
  std::puts("HEIC_THUMB_SCALE_CI_ABBA_COMPLETE_NO_MSI_NO_REGISTRATION");
  return 0;
 }catch(const std::exception& e){
  std::printf("HEIC_THUMB_SCALE_FATAL_DETAIL %s\n",e.what());return 2;
 }
}
