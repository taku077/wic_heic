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
#include "../wic_heic/HeicThumbnailWorkerPolicy.h"
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
// Measures process user+kernel CPU time, not instantaneous Windows CPU %.
// In this single-process controlled A/B, CPU milliseconds complement wall
// milliseconds; no claim is made about user's Explorer/Photos UI responsiveness.
inline uint64_t FileTime100ns(FILETIME x) {
    return (uint64_t(x.dwHighDateTime)<<32) | x.dwLowDateTime;
}
inline double CpuProcessMs(FILETIME beforeKernel,FILETIME beforeUser,
                           FILETIME afterKernel,FILETIME afterUser) {
    const uint64_t a=FileTime100ns(beforeKernel)+FileTime100ns(beforeUser);
    const uint64_t b=FileTime100ns(afterKernel)+FileTime100ns(afterUser);
    if(b<a)throw std::runtime_error("CPU time moved backward");
    return double(b-a)/10000.0;
}
inline void RunPolicyCases() {
    struct Case {
        unsigned w,h;
        int luma,chroma;
        bool alpha;
        unsigned cpus,active,expected;
    };
    constexpr std::array<Case,18> cases{{
        {4032,3024,8,8,false,4,0,2},     // 4 logical CPUs, protect UI headroom
        {4032,3024,8,8,false,4,1,0},     // concurrent fallback
        {4032,3024,8,8,false,4,2,0},     // cap CPU saturation
        {4032,3024,8,8,false,6,0,4},     // larger CPU fast path retained
        {4032,3024,8,8,false,6,1,2},     // concurrent larger CPU
        {4032,3024,8,8,false,5,0,2},     // 5 logical CPU reserves headroom
        {4032,3024,8,8,false,5,1,0},     // 5 logical CPU concurrent fallback
        {4032,3024,8,8,false,2,0,2},     // 2 logical CPU laptop
        {4032,3024,8,8,false,2,1,0},     // 2 logical CPU concurrent
        {4032,3024,8,8,false,1,0,0},     // no parallelism
        {1080,1920,8,8,false,4,0,0},     // small photo
        {3000,2000,8,8,false,4,0,2},     // exact 6MP threshold
        {2999,2000,8,8,false,4,0,0},     // just below 6MP
        {4032,3024,10,10,false,4,0,0},   // high bit depth
        {4032,3024,12,12,false,4,0,0},   // range-extension
        {4032,3024,8,8,true,4,0,0},     // alpha
        {4032,3024,-1,8,false,4,0,0},   // undefined luma depth
        {4032,3024,8,-1,false,4,0,0}    // undefined chroma depth
    }};
    for(const auto& c : cases) {
        const unsigned got=HeicThumbnailWorkerPolicy::Select(
            c.w,c.h,c.luma,c.chroma,c.alpha,c.cpus,c.active);
        if(got!=c.expected) {
            printf("THUMB_WORKERS_POLICY_CASE_FAIL w=%u h=%u "
                   "luma=%d chroma=%d alpha=%d cpu=%u concurrent=%u "
                   "expected=%u actual=%u\n",
                   c.w,c.h,c.luma,c.chroma,c.alpha?1:0,
                   c.cpus,c.active,c.expected,got);
            throw std::runtime_error("Unsafe thumbnail worker selection");
        }
    }
    printf("THUMB_WORKERS_POLICY_CASES_PASS count=%zu "
           "low_res=0 high_depth=0 alpha=0 concurrency_capped=1\n",
           cases.size());
}
inline void RunSmallFallback(IWICImagingFactory* factory) {
    const wchar_t* path=L"samples\\fixture-irot3-exif6-noaux.heic";
    std::vector<BYTE> baseline,actual(144u*256u*3u);
    for(const wchar_t* choice : {L"0",L"a"}) {
        if(!SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",choice))
            throw std::runtime_error("Cannot set CI small image mode");
        CComPtr<IWICBitmapDecoder> decoder;
        Check(factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,&decoder),"small fixture open");
        CComPtr<IWICBitmapFrameDecode> frame;
        Check(decoder->GetFrame(0,&frame),"small fixture frame");
        UINT w=0,h=0;
        Check(frame->GetSize(&w,&h),"small fixture dimensions");
        if(w!=1080 || h!=1920)
            throw std::runtime_error("Unexpected small fixture geometry");
        CComPtr<IWICBitmapScaler> scaler;
        Check(factory->CreateBitmapScaler(&scaler),"small fixture scaler");
        Check(scaler->Initialize(frame,144,256,
            WICBitmapInterpolationModeFant),"small fixture scaler init");
        Check(scaler->CopyPixels(nullptr,144u*3u,UINT(actual.size()),
            actual.data()),"small fallback thumbnail");
        if(baseline.empty())baseline=actual;
        else if(baseline!=actual)
            throw std::runtime_error("Small image auto fallback pixel mismatch");
    }
    SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",nullptr);
    printf("THUMB_WORKERS_SMALL_FALLBACK_PARITY_PASS small=1080x1920 "
           "thumb=144x256 exact_rgb=1\n");
}
// Verify the PILOT production path without the CI-only worker override.
inline void RunPilotAutoWithoutOverride(IWICImagingFactory* factory,
    const std::wstring& path,const std::vector<BYTE>& baseline) {
    SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",nullptr);
    CComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream),"pilot stream");
    Check(stream->InitializeFromFilename(path.c_str(),GENERIC_READ),
        "pilot artificial fixture");
    CComPtr<IWICBitmapDecoder> decoder;
    Check(CoCreateInstance(CLSID_HEICDecoder,nullptr,
        CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&decoder)),"pilot direct decoder");
    Check(decoder->Initialize(stream,WICDecodeMetadataCacheOnDemand),
        "pilot decoder init");
    CComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0,&frame),"pilot frame");
    CComPtr<IWICBitmapScaler> scaler;
    Check(factory->CreateBitmapScaler(&scaler),"pilot scaler");
    Check(scaler->Initialize(frame,256,192,WICBitmapInterpolationModeFant),
        "pilot 256px Fant");
    std::vector<BYTE> result(256u*192u*3u);
    Check(scaler->CopyPixels(nullptr,256u*3u,UINT(result.size()),
        result.data()),"pilot RGB");
    if(result!=baseline)throw std::runtime_error(
        "Pilot production path changed thumbnail RGB");
    printf("THUMB_PILOT_NOENV_RGB_PARITY_PASS source=12MP thumb=256x192\n");
}
inline void Run() {
    wchar_t enabled[12]{};
    if(GetEnvironmentVariableW(L"GITHUB_ACTIONS",enabled,12)!=4 ||
       wcscmp(enabled,L"true")!=0) {
        throw std::runtime_error("Worker lab is CI-only");
    }
    RunPolicyCases();
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
    const std::array<int,4> modes{{0,2,4,-1}};
    std::array<std::vector<double>,4> readings;
    std::array<std::vector<double>,4> cpuReadings;
    constexpr UINT thumbW=256,thumbH=192;
    constexpr UINT thumbStride=thumbW*channels;
    std::vector<BYTE> referenceFull,outputFull(planeBytes);
    std::vector<BYTE> referenceThumb,outputThumb(size_t(thumbStride)*thumbH);
    // Interleaved same-run measurements, freshly constructed WIC frames.
    // Trigger cold HEVC decode through an actual 256px Fant scaler and
    // verify BOTH its pixels and the untouched original full RGB raster.
    for(int round=0;round<5;round++) {
        for(int slot=0;slot<4;slot++) {
            const int index=(round+slot)%4;
            const int workers=modes[index];
            const wchar_t* value=workers==0?L"0":
                (workers==2?L"2":(workers==4?L"4":L"a"));
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
            FILETIME birth{},death{},beforeKernel{},beforeUser{};
            if(!GetProcessTimes(GetCurrentProcess(),&birth,&death,
                                &beforeKernel,&beforeUser))
                throw std::runtime_error("CPU clock unavailable");
            QueryPerformanceCounter(&start);
            CComPtr<IWICBitmapScaler> scaler;
            Check(factory->CreateBitmapScaler(&scaler),"create scaler");
            Check(scaler->Initialize(frame,thumbW,thumbH,
                  WICBitmapInterpolationModeFant),"256px Fant scaler");
            Check(scaler->CopyPixels(nullptr,thumbStride,
                  UINT(outputThumb.size()),outputThumb.data()),"first thumbnail pixels");
            QueryPerformanceCounter(&finish);
            FILETIME afterKernel{},afterUser{};
            if(!GetProcessTimes(GetCurrentProcess(),&birth,&death,
                                &afterKernel,&afterUser))
                throw std::runtime_error("CPU clock measurement failed");
            const double cpuMs=CpuProcessMs(beforeKernel,beforeUser,
                                             afterKernel,afterUser);
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
            cpuReadings[index].push_back(cpuMs);
            printf("THUMB_WORKERS_AB_SAMPLE workers=%d pass=%d source=%ux%u "
                   "thumb=256x192 first_thumbnail_ms=%.3f cpu_ms=%.3f "
                   "exact_full_rgb=1 exact_thumbnail_rgb=1\n",
                   workers,round,w,h,ms,cpuMs);
        }
    }
    SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",nullptr);
    for(int index=0;index<4;index++){
        auto& v=readings[index];
        if(v.size()!=5)throw std::runtime_error("Incomplete worker samples");
        std::sort(v.begin(),v.end());
        auto& c=cpuReadings[index];
        if(c.size()!=5)throw std::runtime_error("Incomplete CPU time samples");
        std::sort(c.begin(),c.end());
        printf("THUMB_WORKERS_AB_MEDIAN workers=%d samples=5 "
               "first_thumbnail_256_ms=%.3f process_cpu_ms=%.3f "
               "no_explorer_claim=1\n",
               modes[index],v[2],c[2]);
    }
    printf("THUMB_WORKERS_AB_PIXEL_PARITY_PASS source=4032x3024 thumb=256x192 modes=0,2,4,auto samples=20 "
           "fixture=artificial_12MP no_msi_release=1\n");
    RunSmallFallback(factory);
    RunPilotAutoWithoutOverride(factory,path,referenceThumb);
}
}
