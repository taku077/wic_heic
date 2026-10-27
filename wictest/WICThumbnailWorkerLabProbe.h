#pragma once
// CI-only same-run A/B: 0/default, 2 and 4 libde265 codec worker hints.
// This fixture and mode MUST NOT run against user photos or production PCs.
// This is not a proof of Windows 10 Explorer initial thumbnail latency.
#include <windows.h>
#include <wincodec.h>
#include <atlbase.h>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace WICThumbnailWorkerLabProbe {
inline void Check(HRESULT hr,const char* s) {
    if(FAILED(hr)){
        printf("THUMB_WORKERS_AB_FAIL stage=%s hr=0x%08x\n",s,(unsigned)hr);
        throw std::runtime_error(s);
    }
}
inline double Ms(LARGE_INTEGER a,LARGE_INTEGER b,LARGE_INTEGER f){
    return double(b.QuadPart-a.QuadPart)*1000.0/double(f.QuadPart);
}
inline void Run() {
    wchar_t enabled[12]{};
    if(GetEnvironmentVariableW(L"GITHUB_ACTIONS",enabled,12)!=4 ||
       wcscmp(enabled,L"true")!=0) {
        throw std::runtime_error("Worker lab is CI-only");
    }
    wchar_t runnerTemp[32768]{};
    const DWORD n=GetEnvironmentVariableW(L"RUNNER_TEMP",runnerTemp,32768);
    if(n==0 || n>=32768)throw std::runtime_error("No CI temp directory");
    const std::wstring path=std::wstring(runnerTemp,n)+
        L"\\wicfast_synthetic_4032x3024.heic";
    const DWORD flags=GetFileAttributesW(path.c_str());
    if(flags==INVALID_FILE_ATTRIBUTES ||
       (flags&FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error("No 12MP fixture");
    LARGE_INTEGER freq{};
    if(!QueryPerformanceFrequency(&freq)||freq.QuadPart<=0)
        throw std::runtime_error("No QPC");
    CComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"factory");
    constexpr UINT width=4032,height=3024,channels=3;
    constexpr UINT stride=width*channels;
    const size_t planeBytes=size_t(stride)*height;
    const std::array<int,3> modes{{0,2,4}};
    std::array<std::vector<double>,3> readings;
    constexpr UINT thumbW=256,thumbH=192;
    constexpr UINT thumbStride=thumbW*channels;
    std::vector<BYTE> referenceFull,outputFull(planeBytes);
    std::vector<BYTE> referenceThumb,outputThumb(size_t(thumbStride)*thumbH);
    // Interleaved same-run measurements, freshly constructed WIC frames.
    // Trigger cold HEVC decode through an actual 256px Fant scaler and
    // verify BOTH its pixels and the untouched original full RGB raster.
    for(int round=0;round<5;round++) {
        for(int slot=0;slot<3;slot++) {
            const int index=(round+slot)%3;
            const int workers=modes[index];
            const wchar_t* value=workers==0?L"0":
                (workers==2?L"2":L"4");
            if(!SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",value))
                throw std::runtime_error("Unable to set CI worker mode");
            CComPtr<IWICStream> stream;
            Check(factory->CreateStream(&stream),"stream");
            Check(stream->InitializeFromFilename(path.c_str(),GENERIC_READ),
                  "read artificial fixture");
            CComPtr<IWICBitmapDecoder> decoder;
            Check(CoCreateInstance(CLSID_HEICDecoder,nullptr,
                CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&decoder)),"direct codec COM");
            Check(decoder->Initialize(stream,WICDecodeMetadataCacheOnDemand),
                  "direct decoder init");
            CComPtr<IWICBitmapFrameDecode> frame;
            Check(decoder->GetFrame(0,&frame),"GetFrame");
            UINT w=0,h=0;
            Check(frame->GetSize(&w,&h),"size");
            if(w!=width || h!=height)
                throw std::runtime_error("Unexpected 12MP dimensions");
            WICPixelFormatGUID fmt{};
            Check(frame->GetPixelFormat(&fmt),"format");
            if(!IsEqualGUID(fmt,GUID_WICPixelFormat24bppRGB))
                throw std::runtime_error("Unexpected 12MP pixel type");
            LARGE_INTEGER start{},finish{};
            QueryPerformanceCounter(&start);
            CComPtr<IWICBitmapScaler> scaler;
            Check(factory->CreateBitmapScaler(&scaler),"create scaler");
            Check(scaler->Initialize(frame,thumbW,thumbH,
                  WICBitmapInterpolationModeFant),"256px Fant scaler");
            Check(scaler->CopyPixels(nullptr,thumbStride,
                  UINT(outputThumb.size()),outputThumb.data()),"first thumbnail pixels");
            QueryPerformanceCounter(&finish);
            if(referenceThumb.empty())referenceThumb=outputThumb;
            else if(referenceThumb!=outputThumb)
                throw std::runtime_error("THUMB_WORKERS_AB_THUMB_RGB_MISMATCH");
            // Decode already happened through scaler; full-sized readback is
            // now warm and is used for exact source RGB parity, NOT timing.
            Check(frame->CopyPixels(nullptr,stride,UINT(planeBytes),outputFull.data()),
                "full RGB readback after thumbnail");
            if(referenceFull.empty())referenceFull=outputFull;
            else if(referenceFull!=outputFull)
                throw std::runtime_error("THUMB_WORKERS_AB_FULL_RGB_MISMATCH");
            const double ms=Ms(start,finish,freq);
            readings[index].push_back(ms);
            printf("THUMB_WORKERS_AB_SAMPLE workers=%d pass=%d source=%ux%u "
                   "thumb=256x192 first_thumbnail_ms=%.3f "
                   "exact_full_rgb=1 exact_thumbnail_rgb=1\n",
                   workers,round,w,h,ms);
        }
    }
    SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",nullptr);
    for(int index=0;index<3;index++){
        auto& v=readings[index];
        if(v.size()!=5)throw std::runtime_error("Incomplete worker samples");
        std::sort(v.begin(),v.end());
        printf("THUMB_WORKERS_AB_MEDIAN workers=%d samples=5 "
               "first_thumbnail_256_ms=%.3f no_explorer_claim=1\n",
               modes[index],v[2]);
    }
    printf("THUMB_WORKERS_AB_PIXEL_PARITY_PASS source=4032x3024 thumb=256x192 modes=0,2,4 samples=15 "
           "fixture=artificial_12MP no_msi_release=1\n");
}
}
