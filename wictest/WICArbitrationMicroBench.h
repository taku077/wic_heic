#pragma once
// CI-only: process-isolated WIC auto-discovery timing of checked-in artificial
// HEIC under temporary registry test conditions. No user images or paths.
#include <windows.h>
#include <wincodec.h>
#include <atlbase.h>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <stdexcept>
namespace WICArbitrationMicroBench {
inline LARGE_INTEGER Tick() {
    LARGE_INTEGER t{};
    if(!QueryPerformanceCounter(&t))
        throw std::runtime_error("QPC failed");
    return t;
}
inline double ToMs(LARGE_INTEGER start,LARGE_INTEGER end) {
    LARGE_INTEGER freq{};
    if(!QueryPerformanceFrequency(&freq) || freq.QuadPart<=0)
        throw std::runtime_error("QPC frequency failed");
    return double(end.QuadPart-start.QuadPart)*1000.0/double(freq.QuadPart);
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(FAILED(hr))throw std::runtime_error("WIC factory failed");
    std::vector<double> times;
    UINT custom=0,foreign=0,failures=0;
    const wchar_t* source=L"samples\\fixture-irot3-exif6-noaux.heic";
    for(int i=0;i<8;i++){
        auto t0=Tick();
        CComPtr<IWICBitmapDecoder> decoder;
        hr=factory->CreateDecoderFromFilename(source,nullptr,GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,&decoder);
        auto t1=Tick();
        if(FAILED(hr)){
            failures++;
            printf("WIC_ARBITRATION_MEASURE_SAMPLE number=%d hr=0x%08X\n",
                   i,static_cast<unsigned>(hr));
            continue;
        }
        times.push_back(ToMs(t0,t1));
        CComPtr<IWICBitmapDecoderInfo> info;
        if(FAILED(decoder->GetDecoderInfo(&info)))
            throw std::runtime_error("GetDecoderInfo failed");
        CLSID id{};
        if(FAILED(info->GetCLSID(&id)))
            throw std::runtime_error("GetCLSID failed");
        if(IsEqualGUID(id,CLSID_HEICDecoder))custom++;
        else foreign++;
        UINT count=0;
        if(FAILED(decoder->GetFrameCount(&count)) || count==0)
            throw std::runtime_error("Selected decoder cannot read fixture");
    }
    std::sort(times.begin(),times.end());
    if(!times.empty()){
        printf("WIC_ARBITRATION_MEASURE samples=%u median_ms=%.3f "
               "selected_custom=%u selected_other=%u failures=%u "
               "registry_writes=0 fixture=public_only\n",
               static_cast<unsigned>(times.size()),times[times.size()/2],
               custom,foreign,failures);
    }else{
        printf("WIC_ARBITRATION_MEASURE samples=0 "
               "selected_custom=0 selected_other=0 failures=%u "
               "registry_writes=0 fixture=public_only\n",failures);
    }
    // Do NOT force a test failure if the runner's temporarily preferred
    // competing codec refuses the synthetic multi-frame HEIF. Report it.
    if(custom+foreign+failures!=8)
        throw std::runtime_error("WIC discovery test count mismatch");
    printf("WIC_ARBITRATION_MEASURE_COMPLETED\n");
}
} // namespace WICArbitrationMicroBench
