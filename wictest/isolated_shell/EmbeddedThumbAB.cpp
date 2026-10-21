// CI-only side-by-side test for *existing* HEIC embedded thumbnails.
// No Explorer, no registry, no installer, no access to real photographs.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../wic_heic/wic_heic.h"
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"windowscodecs.lib")
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
namespace {
void Check(HRESULT hr,const char* label){
 if(FAILED(hr)){std::printf("EMBEDDED_AB_FAILED %s hr=0x%08X\n",label,unsigned(hr));throw std::runtime_error(label);}
}
void Require(bool x,const char* label){if(!x){std::printf("EMBEDDED_AB_FAILED %s\n",label);throw std::runtime_error(label);}}
std::wstring Env(const wchar_t* name){
 DWORD n=GetEnvironmentVariableW(name,nullptr,0);
 if(!n||n>32000)return {};
 std::wstring s(n,L'\0');
 DWORD k=GetEnvironmentVariableW(name,&s[0],n);
 if(!k||k>=n)return {};
 s.resize(k);return s;
}
void Guard(const std::wstring& file,const wchar_t* expected){
 const auto root=Env(L"RUNNER_TEMP");
 Require(Env(L"GITHUB_ACTIONS")==L"true" && file==root+L"\\"+expected,
       "CI-only generated input path");
 Require(GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES,"fixture absent");
}
using FactoryFn=HRESULT (STDAPICALLTYPE*)(REFCLSID,REFIID,LPVOID*);
struct Sample {
 double wallMs=0;
 UINT w=0,h=0;
 bool shortcut=false;
 bool embeddedAvailable=false;
 std::vector<BYTE> pixels;
};
Sample Capture(FactoryFn entry,const std::wstring& path,bool allowThumb,UINT cx){
 ComPtr<IClassFactory> creator;
 Check(entry(CLSID_HEICDecoder,IID_IClassFactory,
          reinterpret_cast<void**>(creator.GetAddressOf())),"create decoder factory");
 ComPtr<IWICBitmapDecoder> decoder;
 Check(creator->CreateInstance(nullptr,IID_PPV_ARGS(decoder.GetAddressOf())),"decoder instance");
 ComPtr<IWICImagingFactory> factory;
 Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(factory.GetAddressOf())),"wic factory");
 ComPtr<IWICStream> stream;
 Check(factory->CreateStream(stream.GetAddressOf()),"wic stream");
 Check(stream->InitializeFromFilename(path.c_str(),GENERIC_READ),"wic input open");
 const auto started=Clock::now();
 Check(decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand),"initialize");
 ComPtr<IWICBitmapFrameDecode> frame;
 Check(decoder->GetFrame(0,frame.GetAddressOf()),"get frame");
 UINT width=0,height=0;
 Check(frame->GetSize(&width,&height),"frame size");
 Require(width==4032 && height==3024,"paired 12MP geometry");
 const UINT tw=UINT(uint64_t(width)*cx/(std::max)(width,height));
 const UINT th=UINT(uint64_t(height)*cx/(std::max)(width,height));
 ComPtr<IWICBitmapSource> source;
 Check(frame.As(&source),"main bitmap source");
 Sample output;
 if(allowThumb){
   ComPtr<IWICBitmapSource> embedded;
   HRESULT hr=frame->GetThumbnail(embedded.GetAddressOf());
   if(SUCCEEDED(hr)&&embedded){
     output.embeddedAvailable=true;
     UINT ew=0,eh=0;
     Check(embedded->GetSize(&ew,&eh),"embedded geometry");
     output.w=ew;output.h=eh;
     WICPixelFormatGUID first{},second{};
     Check(frame->GetPixelFormat(&first),"main pixel format");
     Check(embedded->GetPixelFormat(&second),"thumb pixel format");
     UINT profileCount=0;
     Check(frame->GetColorContexts(0,nullptr,&profileCount),"ICC profile count");
     const uint64_t lhs=uint64_t(ew)*height,rhs=uint64_t(eh)*width;
     const uint64_t delta=(lhs>rhs)?lhs-rhs:rhs-lhs;
     const bool eligible=ew>=tw && eh>=th && ew<=width && eh<=height &&
       delta*100<=uint64_t(width)*uint64_t(eh) &&
       IsEqualGUID(first,GUID_WICPixelFormat24bppRGB) &&
       IsEqualGUID(second,first) && profileCount==0;
     if(eligible){source=embedded;output.shortcut=true;}
   }
 }
 ComPtr<IWICBitmapScaler> scaler;
 Check(factory->CreateBitmapScaler(scaler.GetAddressOf()),"create Fant scaler");
 Check(scaler->Initialize(source.Get(),tw,th,WICBitmapInterpolationModeFant),"Fant scale");
 ComPtr<IWICFormatConverter> converter;
 Check(factory->CreateFormatConverter(converter.GetAddressOf()),"create converter");
 Check(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,
          WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"PBGRA conversion");
 output.pixels.resize(size_t(tw)*size_t(th)*4u);
 Check(converter->CopyPixels(nullptr,tw*4u,UINT(output.pixels.size()),
      output.pixels.data()),"Get pixels");
 output.wallMs=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
 return output;
}
double Median(std::vector<double> nums){
 std::sort(nums.begin(),nums.end());
 return (nums[nums.size()/2-1]+nums[nums.size()/2])/2.;
}
}
int wmain(int argc,wchar_t** argv){
 try{
  Require(argc==4,"usage: CODEC pairedMain pairedEmbedded");
  const std::wstring base=argv[2],embedded=argv[3];
  Guard(base,L"wicfast_baseline_4032x3024.heic");
  Guard(embedded,L"wicfast_embedded_4032x3024.heic");
  Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"COM init");
  const HMODULE module=LoadLibraryExW(argv[1],nullptr,
       LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  Require(module!=nullptr,"private HEIC codec load");
  const auto entry=reinterpret_cast<FactoryFn>(GetProcAddress(module,"DllGetClassObject"));
  Require(entry!=nullptr,"private codec class entry");
  // Order balanced to avoid thermal, cache and OS scheduling bias.
  std::vector<double> fullTimes,thumbTimes;
  for(int iteration=0;iteration<8;++iteration){
    const bool testThumb=iteration%4==1||iteration%4==2;
    Sample s=Capture(entry,testThumb?embedded:base,testThumb,256);
    if(testThumb) {
      Require(s.embeddedAvailable && s.shortcut,"thumbnail >=256 must be used");
      thumbTimes.push_back(s.wallMs);
    } else {
      fullTimes.push_back(s.wallMs);
    }
    std::printf("EMBEDDED_AB_SAMPLE route=%s total_ms=%.3f source=%ux%u\n",
          testThumb?"embedded":"full",s.wallMs,s.w,s.h);
  }
  Require(fullTimes.size()==4&&thumbTimes.size()==4,"balanced groups");
  const double full=Median(fullTimes),shortTime=Median(thumbTimes);
  std::printf("EMBEDDED_AB_MEDIAN full_ms=%.3f embedded_ms=%.3f gain_pct=%.2f\n",
       full,shortTime,100.*(full-shortTime)/full);
  // Pixel difference is a required report, *not* a parity claim. The embedded
  // image is re-encoded and can have a different transfer curve or scaling.
  Sample a=Capture(entry,base,false,256);
  Sample b=Capture(entry,embedded,true,256);
  Require(b.shortcut && a.pixels.size()==b.pixels.size(),"sample geometry or eligibility");
  uint64_t sum=0,square=0;
  BYTE peak=0;
  for(size_t i=0;i<a.pixels.size();++i){
    const int delta=int(a.pixels[i])-int(b.pixels[i]);
    const unsigned absd=unsigned(delta<0?-delta:delta);
    sum+=absd;square+=uint64_t(delta)*delta;
    peak=BYTE((std::max)(unsigned(peak),absd));
  }
  const double n=double(a.pixels.size());
  const double mae=double(sum)/n;
  const double mse=double(square)/n;
  const double psnr=mse>0?10*std::log10(255*255/mse):99.0;
  std::printf("EMBEDDED_AB_QUALITY mae=%.4f mse=%.4f psnr_db=%.3f max_pixel_error=%u\n",
      mae,mse,psnr,unsigned(peak));
  // Thumbnail cannot be upscaled to a larger-than-native request. Benchmark
  // must explicitly verify the guard falls back to full-image decode at 768px.
  Sample large=Capture(entry,embedded,true,768);
  Require(large.embeddedAvailable && !large.shortcut,"768px upsizing guard");
  std::printf("EMBEDDED_AB_FALLBACK_PASS cx=768 total_ms=%.3f\n",large.wallMs);
  if(shortTime>=full) std::puts("EMBEDDED_AB_NO_SPEEDUP");
  else std::puts("EMBEDDED_AB_SYNTHETIC_SPEEDUP_OBSERVED");
  FreeLibrary(module);
  CoUninitialize();
  std::puts("EMBEDDED_AB_NO_INSTALLER_NO_SYSTEM_ASSOCIATIONS_PASSED");
  return 0;
 }catch(const std::exception& e){
  std::printf("EMBEDDED_AB_FATAL %s\n",e.what());return 1;
 }catch(...){std::puts("EMBEDDED_AB_FATAL unknown");return 2;}
}
