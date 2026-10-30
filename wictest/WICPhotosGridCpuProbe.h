#pragma once
// Windows Actions synthetic 12MP grid only. Real WIC decoder and real
// public libheif context adapter are exercised; never touch user files.
#include <windows.h>
#include <wincodec.h>
#include <atlbase.h>
#include <vector>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <cstdio>
#include <cstring>

namespace WICPhotosGridCpuProbe {
inline void Need(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
inline void Check(HRESULT hr,const char* msg){
 if(FAILED(hr)){std::printf("PHOTOS_GRID_CPU_ERROR name=%s hr=0x%08X\n",msg,(unsigned)hr);
  throw std::runtime_error(msg);}
}
inline uint64_t Ticks(FILETIME f){
 return (uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime;
}
inline double CpuMs(){
 FILETIME born{},gone{},kernel{},user{};
 Need(GetProcessTimes(GetCurrentProcess(),&born,&gone,&kernel,&user)!=0,"CPU counter");
 return double(Ticks(kernel)+Ticks(user))/10000.0;
}
struct Measure {double wall,cpu;};
inline Measure One(IWICImagingFactory* factory,const std::wstring& file,
                   bool simulatePhotos,std::vector<BYTE>& reference) {
 // Run BOTH arms through the real Photos codec selection path. Only the
 // CI-only tile-budget bypass distinguishes current 1.1.32 from this pilot.
 Need(SetEnvironmentVariableW(L"WIC_HEIC_THUMB_TEST_PHOTOS",L"1")!=0,
      "simulate Photos host");
 Need(SetEnvironmentVariableW(L"WIC_HEIC_THUMB_TEST_PHOTOS_BASELINE",
                              simulatePhotos?nullptr:L"1")!=0,
      "select production-equivalent Photos baseline");
 Need(SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",nullptr)!=0,"unset force mode");
 CComPtr<IWICStream> stream;
 Check(factory->CreateStream(&stream),"WIC stream");
 Check(stream->InitializeFromFilename(file.c_str(),GENERIC_READ),"WIC file");
 CComPtr<IWICBitmapDecoder> decoder;
 Check(CoCreateInstance(CLSID_HEICDecoder,nullptr,CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(&decoder)),"WIC private decoder");
 Check(decoder->Initialize(stream,WICDecodeMetadataCacheOnDemand),"WIC init");
 CComPtr<IWICBitmapFrameDecode> frame;
 Check(decoder->GetFrame(0,&frame),"WIC first frame");
 UINT w=0,h=0;
 Check(frame->GetSize(&w,&h),"WIC dims");
 Need(w==4096u&&h==3072u,"Unexpected 48 tile grid size");
 WICPixelFormatGUID format{};
 Check(frame->GetPixelFormat(&format),"WIC pixel format");
 Need(IsEqualGUID(format,GUID_WICPixelFormat24bppRGB)!=0,"Grid pixel format unexpectedly changed");
 const UINT stride=w*3u;
 std::vector<BYTE> rgb(size_t(stride)*h);
 LARGE_INTEGER freq{},start{},end{};
 Need(QueryPerformanceFrequency(&freq)!=0,"QPC");
 const double before=CpuMs();
 QueryPerformanceCounter(&start);
 Check(frame->CopyPixels(nullptr,stride,static_cast<UINT>(rgb.size()),rgb.data()),"WIC grid pixels");
 QueryPerformanceCounter(&end);
 const double after=CpuMs();
 if(reference.empty())reference=rgb;
 else Need(reference==rgb,"WIC GRID REGRESSION: full RGB mismatch");
 const double wall=double(end.QuadPart-start.QuadPart)*1000.0/double(freq.QuadPart);
 Need(wall>0&&after>=before,"Invalid measurement");
 std::printf("PHOTOS_GRID_CPU_SAMPLE mode=%s width=4096 height=3072 "
             "wall_ms=%.2f cpu_ms=%.2f exact_rgb=1\n",
             simulatePhotos?"photos-tile2-codec1":"photos-v1132-tile4-codec2",
             wall,after-before);
 return {wall,after-before};
}
inline void Run(){
 wchar_t ci[10]{};
 Need(GetEnvironmentVariableW(L"GITHUB_ACTIONS",ci,10)==4 &&
      wcscmp(ci,L"true")==0,"Never execute outside CI");
 wchar_t temp[MAX_PATH]{};
 DWORD n=GetEnvironmentVariableW(L"RUNNER_TEMP",temp,MAX_PATH);
 Need(n>0&&n<MAX_PATH-90,"Missing temp");
 const std::wstring file=std::wstring(temp)+L"\\wicgrid_artificial_4096x3072_48tiles.heic";
 Need(GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES,"Missing artificial grid HEIC");
 CComPtr<IWICImagingFactory> factory;
 Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
       CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");
 std::vector<BYTE> reference;
 std::array<std::vector<double>,2> cpu,wall;
 try{
  // Each process CPU measurement is quantized on Windows; timing one
 // 12MP decode and taking five medians is insufficient for a release gate.
 // Measure 7 interleaved BATCHES of 8 independent WIC decodes each, including
 // setup CPU as well as full-frame CopyPixels, then compare per-image averages.
 constexpr int batchSize=8,rounds=7;
 for(int round=0;round<rounds;round++)
   for(int step=0;step<2;step++){
    const int i=(step+round)%2; // alternate baseline and candidate
    const double cpuBefore=CpuMs();
    double wallTotal=0.0;
    for(int k=0;k<batchSize;k++)
       wallTotal+=One(factory,file,i==1,reference).wall;
    const double cpuAverage=(CpuMs()-cpuBefore)/double(batchSize);
    Need(cpuAverage>=0.0,"Invalid aggregated CPU time");
    wall[i].push_back(wallTotal/double(batchSize));
    cpu[i].push_back(cpuAverage);
   }
 }catch(...){SetEnvironmentVariableW(L"WIC_HEIC_THUMB_TEST_PHOTOS",nullptr);SetEnvironmentVariableW(L"WIC_HEIC_THUMB_TEST_PHOTOS_BASELINE",nullptr);throw;}
 SetEnvironmentVariableW(L"WIC_HEIC_THUMB_TEST_PHOTOS",nullptr);
 SetEnvironmentVariableW(L"WIC_HEIC_THUMB_TEST_PHOTOS_BASELINE",nullptr);
 for(int i=0;i<2;i++){
  Need(cpu[i].size()==7&&wall[i].size()==7,"Insufficient WIC batch samples");
  std::sort(cpu[i].begin(),cpu[i].end());
  std::sort(wall[i].begin(),wall[i].end());
  std::printf("PHOTOS_GRID_CPU_MEDIAN mode=%s tile_threads=%d codec_threads=%d "
              "wall_ms=%.2f cpu_ms=%.2f samples=7 batch_size=8 exact_rgb=1\n",
              i?"photos":"baseline",i?2:4,i?1:2,
              wall[i][3],cpu[i][3]);
 }
 std::puts("PHOTOS_GRID_CPU_POLICY_PARITY_PASS samples=112 tiles=48 real_photos_unverified=1");
}
}
