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
inline void ProbeOne(IWICImagingFactory* factory,
                     const char* format,const std::wstring& path) {
    const auto t0=Now();
    CComPtr<IWICBitmapDecoder> decoder;
    Need(factory->CreateDecoderFromFilename(path.c_str(),nullptr,
         GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
         "latency cold new decoder");
    CComPtr<IWICBitmapFrameDecode> frame;
    Need(decoder->GetFrame(0,&frame),"latency cold new frame");
    UINT w=0,h=0;
    Need(frame->GetSize(&w,&h),"latency source dimensions");
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
           "first_full_copy_ms=%.3f warm_full_copy_avg_ms=%.3f "
           "scale_quarter_fant_ms=%.3f rotate_90_ms=%.3f "
           "timing=QPC fixture=artificial_only\n",
           format,w,h,Millis(t0,t1),Millis(t1,t2),
           Millis(t2,t3)/3.0,Millis(t3,t4),Millis(t4,t5));
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
    ProbeOne(factory,"HEIC",original);
    ProbeOne(factory,"PNG",png);
    ProbeOne(factory,"JPEG",jpg);
    printf("WIC_THREE_FORMAT_STAGE_LATENCY_COMPARISON_PASS "
           "synthetic_only=1 visual_dims=1080x1920\n");
}
} // namespace WICFormatLatencyProbe
