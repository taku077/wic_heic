#pragma once
// Read-only stage probe for HEIF container parsing. All six files are
// checked-in PUBLIC synthetic or codec samples. Not user photographs.
// Differentiates the unusually large decoder-open cost of the two-frame
// fixture from single-frame samples without changing HEVC or WIC code.
#include <windows.h>
#include <wincodec.h>
#include <atlbase.h>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
namespace WICHeifContainerLatencyProbe {
inline void Need(HRESULT hr,const char* stage) {
    if(FAILED(hr)){
        printf("WIC_HEIF_CONTAINER_FAILED stage=%s hr=0x%08X\n",
            stage,(unsigned)hr);
        throw std::runtime_error(stage);
    }
}
inline LARGE_INTEGER Tick() {
    LARGE_INTEGER t{};
    if(!QueryPerformanceCounter(&t))
        throw std::runtime_error("No QPC");
    return t;
}
inline double Millis(LARGE_INTEGER a,LARGE_INTEGER b) {
    LARGE_INTEGER f{};
    if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0)
        throw std::runtime_error("No QPC frequency");
    return double(b.QuadPart-a.QuadPart)*1000.0/double(f.QuadPart);
}
inline double Median(std::vector<double> m) {
    std::sort(m.begin(),m.end());
    return m[m.size()/2];
}
inline void Run() {
    const wchar_t* paths[]={
       L"samples\\fixture-irot3-exif6-noaux.heic",
       L"samples\\03.heic",
       L"samples\\04.heic",
       L"samples\\05.heic",
       L"samples\\06.heic",
       L"samples\\07.heic"
    };
    const char* names[]={"two-frame-irot","sample03","sample04",
                         "sample05","sample06","sample07"};
    constexpr int count=6,rounds=5;
    CComPtr<IWICImagingFactory> factory;
    Need(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
          CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"factory");
    std::vector<double> opens[count];
    UINT frameCounts[count]{};
    // Rotate test order each pass to reduce warm-cache and scheduling skew.
    for(int round=0;round<rounds;round++){
        for(int k=0;k<count;k++){
            const int i=(k+round)%count;
            const auto t0=Tick();
            CComPtr<IWICBitmapDecoder> decoder;
            Need(factory->CreateDecoderFromFilename(paths[i],nullptr,
                GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
                "CreateDecoderFromFilename");
            const auto t1=Tick();
            UINT frames=0;
            Need(decoder->GetFrameCount(&frames),"frame count");
            if(!frames || frames>16)
                throw std::runtime_error("Unexpected sample image count");
            frameCounts[i]=frames;
            CComPtr<IWICBitmapFrameDecode> primary;
            Need(decoder->GetFrame(0,&primary),"GetFrame");
            UINT w=0,h=0;
            Need(primary->GetSize(&w,&h),"GetSize");
            if(!w||!h||w>20000||h>20000)
                throw std::runtime_error("Invalid displayed dimensions");
            opens[i].push_back(Millis(t0,t1));
        }
    }
    if(frameCounts[0]!=2)
        throw std::runtime_error("Multi-frame fixture lost secondary");
    for(int i=0;i<count;i++){
        if(opens[i].size()!=rounds)throw std::runtime_error("Missing sample");
        printf("WIC_HEIF_CONTAINER_MEDIAN image=%s frames=%u "
               "open_ms=%.3f samples=5 codec=public-checked-in-only\n",
               names[i],frameCounts[i],Median(opens[i]));
    }
    // Same process, same checked-in HEIC, fresh streams. Distinguish
    // WIC's automatic decoder discovery/probing from the codec's own
    // CoCreateInstance + Initialize HEIF parser cost. This is read-only
    // and does NOT change COM registration or the Photos call path.
    std::vector<double> autodetect,direct;
    for(int round=0;round<12;round++){
        const bool viaFactory=(round%4==0||round%4==3);
        CComPtr<IWICStream> stream;
        Need(factory->CreateStream(&stream),"autodetect fixture stream");
        Need(stream->InitializeFromFilename(paths[0],GENERIC_READ),
            "read-only synthetic fixture stream");
        const auto t0=Tick();
        CComPtr<IWICBitmapDecoder> dec;
        if(viaFactory){
            Need(factory->CreateDecoderFromStream(stream,nullptr,
                 WICDecodeMetadataCacheOnDemand,&dec),
                 "WIC automatic decoder selection");
        } else {
            Need(CoCreateInstance(CLSID_HEICDecoder,nullptr,
                 CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dec)),
                 "direct registered HEIC codec COM activation");
            Need(dec->Initialize(stream,WICDecodeMetadataCacheOnDemand),
                 "direct codec container Initialize");
        }
        const auto t1=Tick();
        UINT frames=0;
        Need(dec->GetFrameCount(&frames),"A/B frame count");
        if(frames!=2)throw std::runtime_error("A/B lost secondary frame");
        (viaFactory?autodetect:direct).push_back(Millis(t0,t1));
    }
    if(autodetect.size()!=6||direct.size()!=6)
        throw std::runtime_error("A/B count incorrect");
    printf("WIC_HEIF_DECODER_SELECTION_AB public_fixture=two_frame "
           "samples_per_mode=6 automatic_ms=%.3f direct_codec_ms=%.3f "
           "pixels_not_decoded=1 same_host=1\n",
           Median(autodetect),Median(direct));
    printf("WIC_HEIF_SIX_SAMPLES_CONTAINER_LATENCY_PASS\n");
}
} // namespace WICHeifContainerLatencyProbe
