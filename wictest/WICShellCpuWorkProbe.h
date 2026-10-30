#pragma once
// CI-only: measured CPU work to render 256px WIC full-frame thumbnail versus
// IThumbnailProvider embedded HEVC preview. Does NOT infer CPU from wall time.
// Uses synthetic HEIC only, never user photos or permanent Shell registration.
#include <windows.h>
#include <wincodec.h>
#include <thumbcache.h>
#include <atlbase.h>
#include <vector>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <cstdio>
#include "WICShellThumbnailProviderPilot.h"
namespace WICShellCpuWorkProbe {
inline void Need(bool ok,const char* reason) {
 if(!ok)throw std::runtime_error(reason);
}
inline void Check(HRESULT hr,const char* reason){
 if(FAILED(hr)) {
  std::printf("SHELL_CPU_WORK_ERROR phase=%s hr=0x%08X\n",reason,(unsigned)hr);
  throw std::runtime_error(reason);
 }
}
inline uint64_t Ticks(FILETIME f){
 return (uint64_t(f.dwHighDateTime)<<32) | f.dwLowDateTime;
}
inline double ProcessCpuMs(){
 FILETIME born{},exited{},kernel{},user{};
 Need(GetProcessTimes(GetCurrentProcess(),&born,&exited,&kernel,&user)!=0,
      "GetProcessTimes failed");
 return double(Ticks(kernel)+Ticks(user))/10000.0;
}
struct Sample{double wall,cpu;};
inline Sample RunOnce(IWICImagingFactory* factory,const std::wstring& file,bool fast){
 CComPtr<IWICStream> stream;
 Check(factory->CreateStream(&stream),"stream");
 Check(stream->InitializeFromFilename(file.c_str(),GENERIC_READ),"stream file");
 LARGE_INTEGER f{},start{},stop{};
 Need(QueryPerformanceFrequency(&f)!=0,"QPC frequency");
 const double before=ProcessCpuMs();
 QueryPerformanceCounter(&start);
 if(fast){
  CComPtr<IThumbnailProvider> provider;
  provider.Attach(new WICShellThumbnailProviderPilot::Provider());
  CComPtr<IInitializeWithStream> init;
  Check(provider->QueryInterface(IID_PPV_ARGS(&init)),"Init interface");
  Check(init->Initialize(stream,STGM_READ),"Init stream");
  HBITMAP out=nullptr;
  WTS_ALPHATYPE alpha=WTSAT_UNKNOWN;
  Check(provider->GetThumbnail(256,&out,&alpha),"embedded thumb provider");
  Need(out!=nullptr && alpha==WTSAT_RGB,"Invalid embedded RGB bitmap");
  DIBSECTION dib{};
  Need(GetObjectW(out,sizeof(dib),&dib)==sizeof(dib) &&
       dib.dsBm.bmWidth==256 && abs(dib.dsBm.bmHeight)==192 &&
       dib.dsBm.bmBitsPixel==32,"Unexpected embedded bitmap dimensions");
  DeleteObject(out);
 } else {
  CComPtr<IWICBitmapDecoder> decoder;
  Check(CoCreateInstance(CLSID_HEICDecoder,nullptr,CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&decoder)),"WIC decoder");
  Check(decoder->Initialize(stream,WICDecodeMetadataCacheOnDemand),
        "WIC decoder init");
  CComPtr<IWICBitmapFrameDecode> frame;
  Check(decoder->GetFrame(0,&frame),"WIC GetFrame");
  CComPtr<IWICBitmapScaler> scaler;
  Check(factory->CreateBitmapScaler(&scaler),"Create scaler");
  Check(scaler->Initialize(frame,256,192,WICBitmapInterpolationModeFant),
        "Full size WIC thumbnail scaler");
  CComPtr<IWICFormatConverter> rgba;
  Check(factory->CreateFormatConverter(&rgba),"Create converter");
  Check(rgba->Initialize(scaler,GUID_WICPixelFormat32bppBGRA,
                         WICBitmapDitherTypeNone,nullptr,0,
                         WICBitmapPaletteTypeCustom),"Convert BGRA");
  std::vector<BYTE> pixels(256u*192u*4u);
  Check(rgba->CopyPixels(nullptr,256u*4u,
                         static_cast<UINT>(pixels.size()),pixels.data()),
        "Full 12MP decode and shrink");
 }
 QueryPerformanceCounter(&stop);
 const double cpu=ProcessCpuMs()-before;
 const double wall=double(stop.QuadPart-start.QuadPart)*1000.0/double(f.QuadPart);
 Need(cpu>=0&&wall>0,"Invalid time measurements");
 std::printf("SHELL_CPU_WORK_SAMPLE path=%s cpu_ms=%.2f wall_ms=%.2f\n",
             fast?"embedded":"full-wic",cpu,wall);
 return {wall,cpu};
}
inline void Run(){
 wchar_t actions[12]{},dir[32768]{};
 Need(GetEnvironmentVariableW(L"GITHUB_ACTIONS",actions,12)==4 &&
      wcscmp(actions,L"true")==0,"CI only");
 DWORD n=GetEnvironmentVariableW(L"RUNNER_TEMP",dir,32768);
 Need(n>0&&n<32768,"Missing CI temp");
 const std::wstring path=std::wstring(dir,n)+L"\\wicfast_embedded_4032x3024.heic";
 Need(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES,
      "Missing artificial HEIC embedded thumbnail");
 CComPtr<IWICImagingFactory> factory;
 Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(&factory)),"WIC factory");
 std::array<std::vector<double>,2> cpu,wall;
 for(int round=0;round<7;round++){
  for(int off=0;off<2;off++){
   const int i=(round+off)%2;
   const Sample result=RunOnce(factory,path,i==1);
   cpu[i].push_back(result.cpu);
   wall[i].push_back(result.wall);
  }
 }
 for(int i=0;i<2;i++){
  std::sort(cpu[i].begin(),cpu[i].end());
  std::sort(wall[i].begin(),wall[i].end());
  std::printf("SHELL_CPU_WORK_MEDIAN path=%s cpu_ms=%.2f wall_ms=%.2f "
              "samples=7 original_unchanged=1\n",
              i?"embedded":"full-wic",cpu[i][3],wall[i][3]);
 }
 Need(cpu[1][3]<cpu[0][3]*0.90,
      "Embedded path did not reduce total CPU work by at least 10 percent");
 std::puts("SHELL_CPU_WORK_REDUCTION_GATE_PASS source=synthetic_12MP "
           "full_pixel_decode_avoided=1");
}
}
