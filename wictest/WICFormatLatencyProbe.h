#pragma once
// Objective, read-only WIC per-stage latency comparison between HEIC, JPEG,
// PNG made from EXACTLY THE SAME CHECKED-IN ARTIFICIAL HEIC's visible pixels.
// Never uses user images or originals. The scratch outputs are CREATE-NEW.
// No benchmarks are used as performance pass/fail thresholds: Windows CI
// runners are not comparable to the user's Win10 i5-7th-generation machine.
#include <windows.h>
#include <wincodec.h>
#include <atlbase.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <limits>

namespace WICFormatLatencyProbe {
inline void Need(HRESULT hr,const char* phase) {
    if(FAILED(hr)) {
        printf("WIC_LATENCY_FAILURE phase=%s hr=0x%08X\n",phase,(unsigned)hr);
        throw std::runtime_error(phase);
    }
}
inline double Millis(LARGE_INTEGER start,LARGE_INTEGER stop) {
    LARGE_INTEGER freq{};
    if(!QueryPerformanceFrequency(&freq) || freq.QuadPart<=0)
        throw std::runtime_error("High resolution timer missing");
    return (double(stop.QuadPart-start.QuadPart)*1000.0)/double(freq.QuadPart);
}
inline LARGE_INTEGER Now() {
    LARGE_INTEGER q{};
    if(!QueryPerformanceCounter(&q))
        throw std::runtime_error("High resolution timer read failed");
    return q;
}
inline void WriteComparable(IWICImagingFactory* factory,
                             IWICBitmapSource* src,REFGUID container,
                             const std::wstring& path) {
    if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)
        throw std::runtime_error("Refuse overwriting artificial comparator");
    UINT w=0,h=0;
    Need(src->GetSize(&w,&h),"comparator source size");
    CComPtr<IWICBitmapEncoder> enc;
    Need(factory->CreateEncoder(container,nullptr,&enc),
         "Windows native PNG/JPEG encoder");
    CComPtr<IWICStream> stream;
    Need(factory->CreateStream(&stream),"comparator new stream");
    Need(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),
         "comparator new destination");
    Need(enc->Initialize(stream,WICBitmapEncoderNoCache),"comparator init");
    CComPtr<IWICBitmapFrameEncode> dst;
    CComPtr<IPropertyBag2> options;
    Need(enc->CreateNewFrame(&dst,&options),"comparator frame");
    Need(dst->Initialize(options),"comparator frame init");
    Need(dst->SetSize(w,h),"comparator dimensions");
    WICPixelFormatGUID pixel=GUID_WICPixelFormat24bppBGR;
    Need(dst->SetPixelFormat(&pixel),"comparator format");
    if(!IsEqualGUID(pixel,GUID_WICPixelFormat24bppBGR))
        throw std::runtime_error("Native comparator changed pixel format");
    Need(dst->WriteSource(src,nullptr),"comparator write pixels");
    Need(dst->Commit(),"comparator frame commit");
    Need(enc->Commit(),"comparator encoder commit");
}
struct Timings {
    double prepareMs=0.0,firstPixelsMs=0.0,rotateMs=0.0;
};
inline Timings ProbeOne(IWICImagingFactory* factory,
                     const char* format,const std::wstring& path) {
    const auto t0=Now();
    CComPtr<IWICBitmapDecoder> decoder;
    Need(factory->CreateDecoderFromFilename(path.c_str(),nullptr,
         GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
         "latency cold new decoder");
    const auto tDecoder=Now();
    CComPtr<IWICBitmapFrameDecode> frame;
    Need(decoder->GetFrame(0,&frame),"latency cold new frame");
    const auto tFrame=Now();
    UINT w=0,h=0;
    Need(frame->GetSize(&w,&h),"latency source dimensions");
    const auto tSize=Now();
    if(w!=1080 || h!=1920)throw std::runtime_error(
        "Comparator rendered geometry not portrait 1080x1920");
    CComPtr<IWICBitmapSource> rgb;
    Need(WICConvertBitmapSource(GUID_WICPixelFormat24bppBGR,
         frame,&rgb),"latency common BGR pixel format");
    const auto t1=Now();
    const size_t size=size_t(w)*h*3;
    if(size>UINT_MAX)throw std::runtime_error("Comparator dimensions too large");
    std::vector<BYTE> copy(size);
    Need(rgb->CopyPixels(nullptr,w*3,UINT(size),copy.data()),
         "latency FIRST full RGB read");
    const auto t2=Now();
    // Same decoder and same frame. The libheif image should already have
    // been decompressed once. Measures repeated pixel consumption alone.
    for(int i=0;i<3;i++)
        Need(rgb->CopyPixels(nullptr,w*3,UINT(size),copy.data()),
             "latency WARM full RGB read");
    const auto t3=Now();
    CComPtr<IWICBitmapScaler> scaler;
    Need(factory->CreateBitmapScaler(&scaler),"latency scaler creation");
    Need(scaler->Initialize(frame,w/4,h/4,WICBitmapInterpolationModeFant),
         "latency one-quarter scaler");
    CComPtr<IWICBitmapSource> scaledRgb;
    Need(WICConvertBitmapSource(GUID_WICPixelFormat24bppBGR,
         scaler,&scaledRgb),"latency scaled BGR");
    const size_t smaller=size_t(w/4)*(h/4)*3;
    std::vector<BYTE> reduced(smaller);
    Need(scaledRgb->CopyPixels(nullptr,(w/4)*3,UINT(smaller),reduced.data()),
         "latency quarter pixels");
    const auto t4=Now();
    CComPtr<IWICBitmapFlipRotator> rotator;
    Need(factory->CreateBitmapFlipRotator(&rotator),"latency rotator");
    Need(rotator->Initialize(frame,WICBitmapTransformRotate90),
         "latency quarter turn");
    CComPtr<IWICBitmapSource> rotatedRgb;
    Need(WICConvertBitmapSource(GUID_WICPixelFormat24bppBGR,
         rotator,&rotatedRgb),"latency rotated BGR");
    UINT rw=0,rh=0;
    Need(rotatedRgb->GetSize(&rw,&rh),"latency rotated geometry");
    if(rw!=h||rh!=w)throw std::runtime_error("Quarter-turn geometry bad");
    Need(rotatedRgb->CopyPixels(nullptr,rw*3,UINT(size),copy.data()),
         "latency rotated pixels");
    const auto t5=Now();
    printf("WIC_FORMAT_LATENCY format=%s pixels=%ux%u open_ms=%.3f "
           "create_decoder_ms=%.3f get_frame_ms=%.3f "
           "get_size_ms=%.3f bgr_converter_init_ms=%.3f "
           "first_full_copy_ms=%.3f warm_full_copy_avg_ms=%.3f "
           "scale_quarter_fant_ms=%.3f rotate_90_ms=%.3f "
           "timing=QPC fixture=artificial_only\n",
           format,w,h,Millis(t0,t1),
           Millis(t0,tDecoder),Millis(tDecoder,tFrame),
           Millis(tFrame,tSize),Millis(tSize,t1),
           Millis(t1,t2),Millis(t2,t3)/3.0,
           Millis(t3,t4),Millis(t4,t5));
    return {Millis(t0,t1),Millis(t1,t2),Millis(t4,t5)};
}
// User's Win10 Photos repro: initial fit-to-window, zoom out further,
// then rotate. This tests only WIC source geometry/pixels; the Photos app's
// internal visual/layout transform is NOT exposed to this harness.
// Reuse the existing identical-visible-pixel HEIC/PNG/JPEG comparators,
// not another output fixture or expensive saved-image matrix.
inline void ProbeFitExtraZoomRotate(IWICImagingFactory* factory,
                                   const char* format,
                                   const wchar_t* path) {
    CComPtr<IWICBitmapDecoder> decoder;
    Need(factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,
         WICDecodeMetadataCacheOnDemand,&decoder),"fit/zoom decoder");
    CComPtr<IWICBitmapFrameDecode> frame;
    Need(decoder->GetFrame(0,&frame),"fit/zoom frame");
    UINT originalW=0,originalH=0;
    Need(frame->GetSize(&originalW,&originalH),"fit/zoom source size");
    if(originalW!=1080||originalH!=1920)
        throw std::runtime_error("fit/zoom source geometry differs");

    CComPtr<IWICBitmapScaler> fitted,extraZoom;
    Need(factory->CreateBitmapScaler(&fitted),"fit scaler");
    Need(fitted->Initialize(frame,540,960,WICBitmapInterpolationModeFant),
         "fit scaler init");
    Need(factory->CreateBitmapScaler(&extraZoom),"extra zoom scaler");
    Need(extraZoom->Initialize(fitted,405,720,
         WICBitmapInterpolationModeFant),"extra zoom init");
    CComPtr<IWICBitmapFlipRotator> rotated;
    Need(factory->CreateBitmapFlipRotator(&rotated),"fit/zoom rotator");
    Need(rotated->Initialize(extraZoom,WICBitmapTransformRotate90),
         "fit/zoom rotate 90");

    UINT fitW=0,fitH=0,zoomW=0,zoomH=0,rotW=0,rotH=0;
    Need(fitted->GetSize(&fitW,&fitH),"fit dimensions");
    Need(extraZoom->GetSize(&zoomW,&zoomH),"extra zoom dimensions");
    Need(rotated->GetSize(&rotW,&rotH),"post-rotation dimensions");
    if(fitW!=540||fitH!=960 || zoomW!=405||zoomH!=720 ||
       rotW!=720||rotH!=405)
        throw std::runtime_error("FIT_EXTRA_ZOOM_ROTATE_GEOMETRY_MISMATCH");

    CComPtr<IWICBitmapSource> zoomRGB,rotationRGB;
    Need(WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,
         extraZoom,&zoomRGB),"extra zoom RGB");
    Need(WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,
         rotated,&rotationRGB),"rotated RGB");
    std::vector<BYTE> before(size_t(zoomW)*zoomH*3);
    std::vector<BYTE> after(size_t(rotW)*rotH*3);
    Need(zoomRGB->CopyPixels(nullptr,zoomW*3,UINT(before.size()),
         before.data()),"fit/zoom pixels");
    Need(rotationRGB->CopyPixels(nullptr,rotW*3,UINT(after.size()),
         after.data()),"rotated fit/zoom pixels");

    // 90 degrees clockwise: source (x,y) -> output (H-1-y,x).
    // An aspect-stretched, transposed incorrectly, or shifted RGB plane
    // cannot pass this exact per-pixel mapping check.
    for(UINT y=0;y<zoomH;y++) {
        for(UINT x=0;x<zoomW;x++) {
            const size_t si=(size_t(y)*zoomW+x)*3;
            const UINT rx=zoomH-1-y,ry=x;
            const size_t di=(size_t(ry)*rotW+rx)*3;
            if(std::memcmp(before.data()+si,after.data()+di,3)!=0)
                throw std::runtime_error("FIT_EXTRA_ZOOM_ROTATE_RGB_MISMATCH");
        }
    }
    UINT repeatW=0,repeatH=0;
    Need(frame->GetSize(&repeatW,&repeatH),"post-rotation source dimensions");
    if(repeatW!=originalW||repeatH!=originalH)
        throw std::runtime_error("FIT_EXTRA_ZOOM_CHANGED_SOURCE_SIZE");
    printf("WIC_FIT_EXTRA_ZOOM_ROTATE_PASS format=%s "
           "original=1080x1920 fit=540x960 zoom=405x720 "
           "rotated=720x405 exact_rgb_map=1 photos_ui_unverified=1\n",
           format);
}
inline void Run() {
    const wchar_t* original=L"samples\\fixture-irot3-exif6-noaux.heic";
    const DWORD status=GetFileAttributesW(original);
    if(status==INVALID_FILE_ATTRIBUTES || (status&FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Pinned public HEIC fixture missing");
    const auto root=std::wstring(L"samples\\artificial_format_latency_")+
        std::to_wstring(GetCurrentProcessId())+L"_"+
        std::to_wstring(GetTickCount64());
    if(!CreateDirectoryW(root.c_str(),nullptr))
        throw std::runtime_error("Could not create fresh benchmark folder");
    CComPtr<IWICImagingFactory> factory;
    Need(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
         CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"latency factory");
    CComPtr<IWICBitmapDecoder> originalDecoder;
    Need(factory->CreateDecoderFromFilename(original,nullptr,GENERIC_READ,
         WICDecodeMetadataCacheOnDemand,&originalDecoder),
         "public fixture for comparators");
    CComPtr<IWICBitmapFrameDecode> source;
    Need(originalDecoder->GetFrame(0,&source),"public fixture frame");
    CComPtr<IWICBitmapSource> visualRgb;
    Need(WICConvertBitmapSource(GUID_WICPixelFormat24bppBGR,source,
         &visualRgb),"artificial visible pixels for comparators");
    const std::wstring png=root+L"\\same-visual.png";
    const std::wstring jpg=root+L"\\same-visual.jpg";
    WriteComparable(factory,visualRgb,GUID_ContainerFormatPng,png);
    WriteComparable(factory,visualRgb,GUID_ContainerFormatJpeg,jpg);
    // HEIC is checked-in artificial with its own irot/EXIF/aux.
    // PNG and JPEG are raster equivalents of its primary displayed image.
    // PNG is lossless; JPEG may differ slightly because its compression is
    // lossy. CI timing is guidance only, not a user's actual PC benchmark.
    // Five fresh decoder/frame instances per format, interleaved to
    // reduce warmed file-cache and short-lived CI CPU scheduling bias.
    // A median is less misleading than the previous one-shot metric.
    const char* labels[]={"HEIC","PNG","JPEG"};
    const wchar_t* paths[]={original,png.c_str(),jpg.c_str()};
    // Exercise the precise fit -> further zoom -> rotate WIC pixel path
    // once for each equivalent format before the timing comparison.
    for(int format=0;format<3;format++)
        ProbeFitExtraZoomRotate(factory,labels[format],paths[format]);
    std::vector<Timings> readings[3];
    for(int pass=0;pass<5;pass++) {
        for(int slot=0;slot<3;slot++) {
            const int format=(pass+slot)%3;
            readings[format].push_back(ProbeOne(
                factory,labels[format],paths[format]));
        }
    }
    const auto median=[](std::vector<double> values)->double {
        std::sort(values.begin(),values.end());
        return values[values.size()/2];
    };
    for(int format=0;format<3;format++) {
        std::vector<double> open,first,rotate;
        for(const auto& sample:readings[format]) {
            open.push_back(sample.prepareMs);
            first.push_back(sample.firstPixelsMs);
            rotate.push_back(sample.rotateMs);
        }
        printf("WIC_FORMAT_MEDIAN format=%s samples=5 "
               "open_ms=%.3f first_full_copy_ms=%.3f "
               "rotate_90_ms=%.3f warmup=interleaved "
               "timing=QPC fixture=artificial_only\n",
               labels[format],median(open),median(first),
               median(rotate));
    }
    printf("WIC_THREE_FORMAT_STAGE_LATENCY_COMPARISON_PASS "
           "synthetic_only=1 visual_dims=1080x1920\n");
}
} // namespace WICFormatLatencyProbe
