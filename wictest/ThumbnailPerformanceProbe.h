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
        const UINT targetW=((uint64_t(w)*256 / (w>h?w:h))>0 ? UINT(uint64_t(w)*256/(w>h?w:h)) : 1u);
        const UINT targetH=((uint64_t(h)*256 / (w>h?w:h))>0 ? UINT(uint64_t(h)*256/(w>h?w:h)) : 1u);
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
// Isolate the first HEVC decode from WIC scaler/format-converter cost.
// Reuse an individual frame only for the second CopyPixels, so the second
// read must not trigger a second HEVC decode. The actual WIC pixel format
// is respected; synthetic input only and no output images are written.
struct CopyPixelsTimes {
    std::vector<double> first, second;
};
inline CopyPixelsTimes MeasurePrimaryDecode(IWICImagingFactory* factory,
                                             const wchar_t* path, int n) {
    CopyPixelsTimes result;
    for(int run=0;run<n;++run) {
        CComPtr<IWICBitmapDecoder> decoder;
        Check(factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,&decoder),"copyperf open");
        CComPtr<IWICBitmapFrameDecode> frame;
        Check(decoder->GetFrame(0,&frame),"copyperf frame");
        UINT width=0,height=0;
        GUID pixelFormat{};
        Check(frame->GetSize(&width,&height),"copyperf size");
        Check(frame->GetPixelFormat(&pixelFormat),"copyperf format");
        if(!width || !height)throw std::runtime_error("copyperf empty image");
        const UINT bytesPerPixel=
            IsEqualGUID(pixelFormat,GUID_WICPixelFormat48bppRGB)?6u:
            IsEqualGUID(pixelFormat,GUID_WICPixelFormat64bppRGBA)?8u:
            IsEqualGUID(pixelFormat,GUID_WICPixelFormat24bppRGB)?3u:
            IsEqualGUID(pixelFormat,GUID_WICPixelFormat32bppRGBA)?4u:0u;
        if(!bytesPerPixel)throw std::runtime_error("copyperf unexpected native WIC format");
        const uint64_t stride64=uint64_t(width)*bytesPerPixel;
        if(stride64>8u*1024u*1024u)throw std::runtime_error("copyperf oversized row");
        const UINT stride=UINT(stride64);
        std::vector<BYTE> row(stride), checkRow(stride);
        WICRect rc{0,0,int(width),1};
        const auto start=Clock::now();
        Check(frame->CopyPixels(&rc,stride,stride,row.data()),"copyperf first pixel");
        const auto firstDone=Clock::now();
        Check(frame->CopyPixels(&rc,stride,stride,checkRow.data()),"copyperf cached pixel");
        const auto secondDone=Clock::now();
        if(row!=checkRow)throw std::runtime_error("copyperf first and cached row mismatch");
        result.first.push_back(Millis(start,firstDone));
        result.second.push_back(Millis(firstDone,secondDone));
    }
    return result;
}
// Compare WIC's automatic decoder discovery with a direct HEIC WIC
// decoder using the SAME registered codec and synthetic file. The direct
// path avoids repeated WIC format discovery, but is only diagnostic:
// Explorer normally chooses automatic discovery and cannot be claimed fast.
inline void RunOpenPathBenchmark() {
    CComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
         IID_IWICImagingFactory,(void**)&factory),"openpath WIC factory");
    constexpr const wchar_t* file=L"samples\\fixture-irot3-exif6-noaux.heic";
    constexpr int iterations=10;
    std::vector<double> automatic, explicitCodec;
    int automaticOurDecoder=0,automaticOtherDecoder=0;
    int directOurDecoder=0,directOtherDecoder=0;
    for(int i=0;i<iterations;++i) {
        // Interleave order to reduce order-dependent OS file cache effects.
        for(int mode=0;mode<2;++mode) {
            const bool isDirect=((mode + i)%2)==1;
            const auto started=Clock::now();
            CComPtr<IWICBitmapDecoder> decoder;
            if(isDirect) {
                CComPtr<IWICStream> stream;
                Check(factory->CreateStream(&stream),"openpath create stream");
                Check(stream->InitializeFromFilename(file,GENERIC_READ),"openpath stream file");
                Check(factory->CreateDecoder(GUID_ContainerFormatHEIC,
                    &GUID_VendorPrsyahmi,&decoder),"openpath codec create");
                Check(decoder->Initialize(stream,WICDecodeMetadataCacheOnDemand),
                    "openpath direct initialize");
            } else {
                Check(factory->CreateDecoderFromFilename(file,nullptr,GENERIC_READ,
                    WICDecodeMetadataCacheOnDemand,&decoder),
                    "openpath automatic initialize");
            }
            const auto completed=Clock::now();
            // Verify WHICH decoder WIC selected. The huge automatic/direct
            // timing gap is meaningless if the providers differ.
            CComPtr<IWICBitmapDecoderInfo> info;
            Check(decoder->GetDecoderInfo(&info),"openpath decoder info");
            CLSID chosen{};
            Check(info->GetCLSID(&chosen),"openpath decoder CLSID");
            const bool ourCodec=IsEqualCLSID(chosen,CLSID_HEICDecoder);
            if(isDirect) (ourCodec?directOurDecoder:directOtherDecoder)++;
            else (ourCodec?automaticOurDecoder:automaticOtherDecoder)++;
            UINT frames=0;
            Check(decoder->GetFrameCount(&frames),"openpath frame count");
            if(!frames)throw std::runtime_error("openpath HEIC image missing");
            if(isDirect) explicitCodec.push_back(Millis(started,completed));
            else automatic.push_back(Millis(started,completed));
        }
    }
    printf("HEIC_WIC_OPEN_PATHS_MEASURED samples=%d automatic_median_ms=%.3f direct_median_ms=%.3f\n",
        iterations,Median(automatic),Median(explicitCodec));
    printf("HEIC_WIC_OPEN_CODEC_IDENTITY samples=%d automatic_our_decoder=%d automatic_other_decoder=%d direct_our_decoder=%d direct_other_decoder=%d\n",
        iterations,automaticOurDecoder,automaticOtherDecoder,directOurDecoder,directOtherDecoder);
    printf("HEIC_WIC_OPEN_PATH_DIAGNOSTIC_ONLY_NOT_EXPLORER_PARITY\n");
}
// WIC Shell fast-path feasibility. The same codec must render byte-identical
// 256px thumbnail pixels whether WIC auto-discovers the codec or receives
// the HEIC decoder GUID directly. No registry, shell handler or source changes.
struct RenderAB {
    double openMs=0, totalMs=0;
    std::vector<BYTE> pixels;
};
inline RenderAB RenderSyntheticThumbnail(IWICImagingFactory* factory,bool direct) {
    constexpr const wchar_t* file=L"samples\\fixture-irot3-exif6-noaux.heic";
    RenderAB result;
    auto start=Clock::now();
    CComPtr<IWICBitmapDecoder> decoder;
    if(direct) {
        CComPtr<IWICStream> stream;
        Check(factory->CreateStream(&stream),"directthumb create stream");
        Check(stream->InitializeFromFilename(file,GENERIC_READ),"directthumb initialize stream");
        Check(factory->CreateDecoder(GUID_ContainerFormatHEIC,
             &GUID_VendorPrsyahmi,&decoder),"directthumb create codec");
        Check(decoder->Initialize(stream,WICDecodeMetadataCacheOnDemand),
             "directthumb decoder init");
    } else {
        Check(factory->CreateDecoderFromFilename(file,nullptr,GENERIC_READ,
             WICDecodeMetadataCacheOnDemand,&decoder),"autothumb create codec");
    }
    auto opened=Clock::now();
    CComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0,&frame),"directthumb frame");
    UINT w=0,height=0;
    Check(frame->GetSize(&w,&height),"directthumb size");
    if(!w||!height||w>10000||height>10000)
        throw std::runtime_error("directthumb dimensions");
    const UINT maxSide=(w>height)?w:height;
    const UINT targetW=UINT((uint64_t(w)*256)/maxSide) ?
        UINT((uint64_t(w)*256)/maxSide):1;
    const UINT targetH=UINT((uint64_t(height)*256)/maxSide) ?
        UINT((uint64_t(height)*256)/maxSide):1;
    CComPtr<IWICBitmapScaler> scaler;
    Check(factory->CreateBitmapScaler(&scaler),"directthumb scaler");
    Check(scaler->Initialize(frame,targetW,targetH,
          WICBitmapInterpolationModeFant),"directthumb resize");
    CComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(&converter),"directthumb converter");
    Check(converter->Initialize(scaler,GUID_WICPixelFormat24bppBGR,
          WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),
          "directthumb format");
    const UINT stride=targetW*3;
    result.pixels.resize(size_t(stride)*targetH);
    Check(converter->CopyPixels(nullptr,stride,UINT(result.pixels.size()),
          result.pixels.data()),"directthumb copy");
    auto ended=Clock::now();
    result.openMs=Millis(start,opened);
    result.totalMs=Millis(start,ended);
    return result;
}
inline void RunDirectThumbnailAB() {
    CComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
         IID_IWICImagingFactory,(void**)&factory),"directthumb WIC factory");
    constexpr int samples=10;
    std::vector<double> automaticOpen,directOpen,automaticTotal,directTotal;
    std::vector<BYTE> reference;
    for(int run=0;run<samples;++run) {
        for(int mode=0;mode<2;++mode) {
            const bool direct=((run+mode)%2)==1;
            const auto rendered=RenderSyntheticThumbnail(factory,direct);
            if(reference.empty())reference=rendered.pixels;
            if(reference!=rendered.pixels)
                throw std::runtime_error("Direct and automatic WIC thumbnail pixels differ");
            (direct?directOpen:automaticOpen).push_back(rendered.openMs);
            (direct?directTotal:automaticTotal).push_back(rendered.totalMs);
        }
    }
    printf("HEIC_DIRECT_THUMB_AB_MEASURED samples=%d auto_open_ms=%.3f direct_open_ms=%.3f auto_total_ms=%.3f direct_total_ms=%.3f pixels_equal=1\n",
        samples,Median(automaticOpen),Median(directOpen),
        Median(automaticTotal),Median(directTotal));
    printf("HEIC_DIRECT_THUMB_SYNTHETIC_ONLY_EXPLORER_NOT_TESTED\n");
}
inline void RunDecodeIsolation() {
    CComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory,(void**)&factory),"copyperf WIC factory");
    constexpr int iterations=10;
    const auto result=MeasurePrimaryDecode(factory,
        L"samples\\fixture-irot3-exif6-noaux.heic",iterations);
    printf("HEIC_PRIMARY_WIC_COPYPIXELS_MEASURED samples=%d first_median_ms=%.3f cached_median_ms=%.3f\n",
        iterations,Median(result.first),Median(result.second));
    // Must not claim a win from time measurements alone; this is a baseline.
    printf("HEIC_PRIMARY_DECODE_BASELINE_ONLY_NO_SPEEDUP_CLAIM\n");
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
