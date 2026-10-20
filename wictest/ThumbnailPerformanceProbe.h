#pragma once
// Thumbnail pipeline measurement on a Windows runner. The same pinned HEIC
// image is exported once to JPEG, then the same 256px WIC pipeline runs
// against both sources. The timings are warm OS-file-cache/uncached codec
// instance timings, NOT Explorer's uncached shell thumbnail latency.
#include <chrono>
#include <algorithm>
#include <numeric>
namespace ThumbnailPerformanceProbe {
using Clock = std::chrono::steady_clock;
inline double Millis(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double,std::milli>(end-begin).count();
}
inline void Check(HRESULT hr,const char* stage) {
    if(FAILED(hr)) {
        printf("THUMB_PERF_ERROR %s: 0x%08X\n",stage,(unsigned)hr);
        throw std::runtime_error(stage);
    }
}
struct Stats {
    std::vector<double> open, thumbnail, render;
    int embedded=0,absent=0;
};
inline double Median(std::vector<double> v) {
    std::sort(v.begin(),v.end());
    return v[v.size()/2];
}
inline void ExportJpegFromHeic(IWICImagingFactory* factory,
                               const wchar_t* heicPath,
                               const wchar_t* jpegPath) {
    CComPtr<IWICBitmapDecoder> src;
    Check(factory->CreateDecoderFromFilename(heicPath,nullptr,GENERIC_READ,
          WICDecodeMetadataCacheOnDemand,&src),"JPEG test source");
    CComPtr<IWICBitmapFrameDecode> frame;
    Check(src->GetFrame(0,&frame),"JPEG test source frame");
    UINT w=0,h=0;
    Check(frame->GetSize(&w,&h),"JPEG test source size");
    CComPtr<IWICFormatConverter> rgb;
    Check(factory->CreateFormatConverter(&rgb),"JPEG test converter");
    Check(rgb->Initialize(frame,GUID_WICPixelFormat24bppBGR,WICBitmapDitherTypeNone,
          nullptr,0,WICBitmapPaletteTypeCustom),"JPEG test BGR conversion");
    CComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream),"JPEG test output stream");
    Check(stream->InitializeFromFilename(jpegPath,GENERIC_WRITE),"JPEG test output file");
    CComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,&encoder),"JPEG native encoder");
    Check(encoder->Initialize(stream,WICBitmapEncoderNoCache),"JPEG encoder init");
    CComPtr<IWICBitmapFrameEncode> output;
    Check(encoder->CreateNewFrame(&output,nullptr),"JPEG frame");
    Check(output->Initialize(nullptr),"JPEG frame init");
    Check(output->SetSize(w,h),"JPEG size");
    GUID format=GUID_WICPixelFormat24bppBGR;
    Check(output->SetPixelFormat(&format),"JPEG format");
    Check(output->WriteSource(rgb,nullptr),"JPEG write");
    Check(output->Commit(),"JPEG frame commit");
    Check(encoder->Commit(),"JPEG commit");
}
inline Stats Measure(IWICImagingFactory* factory, const wchar_t* path, int n) {
    Stats result;
    for(int run=0;run<n;++run) {
        auto started=Clock::now();
        CComPtr<IWICBitmapDecoder> decoder;
        Check(factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,
              WICDecodeMetadataCacheOnDemand,&decoder),"benchmark open");
        auto opened=Clock::now();
        CComPtr<IWICBitmapSource> preview;
        HRESULT hr=decoder->GetThumbnail(&preview);
        if(hr==WINCODEC_ERR_CODECNOTHUMBNAIL) {
            CComPtr<IWICBitmapFrameDecode> frame;
            Check(decoder->GetFrame(0,&frame),"benchmark frame");
            hr=frame->GetThumbnail(&preview);
        }
        if(hr==WINCODEC_ERR_CODECNOTHUMBNAIL) ++result.absent;
        else if(SUCCEEDED(hr) && preview) ++result.embedded;
        else Check(hr,"benchmark GetThumbnail");
        auto thumbnailRead=Clock::now();
        if(!preview) {
            CComPtr<IWICBitmapFrameDecode> frame;
            Check(decoder->GetFrame(0,&frame),"benchmark missing preview frame");
            preview=frame;
        }
        UINT w=0,h=0;
        Check(preview->GetSize(&w,&h),"benchmark dimensions");
        if(!w||!h)throw std::runtime_error("benchmark zero dimensions");
        const UINT targetW=std::max<UINT>(1,UINT((uint64_t(w)*256) / std::max(w,h)));
        const UINT targetH=std::max<UINT>(1,UINT((uint64_t(h)*256) / std::max(w,h)));
        CComPtr<IWICBitmapScaler> scaler;
        Check(factory->CreateBitmapScaler(&scaler),"benchmark scaler");
        Check(scaler->Initialize(preview,targetW,targetH,WICBitmapInterpolationModeFant),"benchmark initialize scale");
        CComPtr<IWICFormatConverter> rgb;
        Check(factory->CreateFormatConverter(&rgb),"benchmark format converter");
        Check(rgb->Initialize(scaler,GUID_WICPixelFormat24bppBGR,
              WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),
              "benchmark format init");
        const size_t stride=size_t(targetW)*3u;
        std::vector<BYTE> thumb(stride*targetH);
        Check(rgb->CopyPixels(nullptr,UINT(stride),UINT(thumb.size()),thumb.data()),
              "benchmark raster decode and resize");
        auto rendered=Clock::now();
        result.open.push_back(Millis(started,opened));
        result.thumbnail.push_back(Millis(opened,thumbnailRead));
        result.render.push_back(Millis(thumbnailRead,rendered));
    }
    return result;
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
       IID_IWICImagingFactory,(void**)&factory),"benchmark WIC factory");
    constexpr const wchar_t* heic=L"samples\\fixture-irot3-exif6-noaux.heic";
    constexpr const wchar_t* jpeg=L"samples\\thumbnail-perf-reference.jpg";
    ExportJpegFromHeic(factory,heic,jpeg);
    constexpr int iterations=10;
    const Stats heicTimes=Measure(factory,heic,iterations);
    const Stats jpegTimes=Measure(factory,jpeg,iterations);
    printf("THUMB_WIC_HEIC_MEASURED samples=%d embedded=%d absent=%d open_median_ms=%.3f getthumb_median_ms=%.3f resize_median_ms=%.3f\n",
        iterations,heicTimes.embedded,heicTimes.absent,Median(heicTimes.open),
        Median(heicTimes.thumbnail),Median(heicTimes.render));
    printf("THUMB_WIC_JPEG_MEASURED samples=%d embedded=%d absent=%d open_median_ms=%.3f getthumb_median_ms=%.3f resize_median_ms=%.3f\n",
        iterations,jpegTimes.embedded,jpegTimes.absent,Median(jpegTimes.open),
        Median(jpegTimes.thumbnail),Median(jpegTimes.render));
    // This is a diagnostic, not a Windows 10 Explorer benchmark.
    printf("THUMB_WIC_BENCHMARK_WARM_CACHE_NATIVE_ONLY\n");
}
}
