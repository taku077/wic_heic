#pragma once
// Demonstrate a real 1.1.27 Photos observation: one-byte Exif Orientation
// normalization does not itself rotate HEVC image pixels or change irot.
// Public artificial two-frame HEIC ONLY, written as a fresh derivative.
// WIC's own decoder, NOT a linked libheif dependency, compares the planes.
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include <cstdio>

namespace WICPhotosExifOnlyProbe {
inline void Check(HRESULT hr,const char* what) {
    if(FAILED(hr)) {
        printf("WIC_PHOTOS_EXIF_ONLY_PROBE_FAILURE=%s hr=0x%08X\n",
               what,static_cast<unsigned>(hr));
        throw std::runtime_error(what);
    }
}
inline std::vector<BYTE> DecodedRGB(IWICImagingFactory* factory,
                     const wchar_t* filename,UINT* w,UINT* h) {
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(filename,nullptr,GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,&decoder),"read artificial HEIF");
    UINT frames=0;
    Check(decoder->GetFrameCount(&frames),"ExIf-only WIC GetFrameCount");
    if(frames!=2)
        throw std::runtime_error("Exif-only test lost original secondary item");
    CComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0,&frame),"ExIf-only primary");
    Check(frame->GetSize(w,h),"ExIf-only dimensions");
    if(!*w || !*h || *w>20000 || *h>20000)
        throw std::runtime_error("Exif-only test invalid geometry");
    CComPtr<IWICBitmapSource> rgb;
    Check(WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,
          frame,&rgb),"Exif-only WIC RGB conversion");
    const auto count=size_t(*w)*(*h)*3;
    if(count>UINT_MAX) throw std::runtime_error("Exif-only RGB too large");
    std::vector<BYTE> result(count);
    Check(rgb->CopyPixels(nullptr,(*w)*3,UINT(count),result.data()),
          "Exif-only WIC RGB CopyPixels");
    return result;
}
inline void Run() {
    const auto source=WICPhotosOpaqueRotationProbe::Read(
        L"samples\\fixture-irot3-exif6-noaux.heic");
    // Fixture-only byte positions, not a general-purpose Exif/HEIF parser.
    constexpr size_t irotAngleAt=567, exifOrientationAt=10589;
    if(source.size()!=10597 || source[irotAngleAt]!=3 ||
       source[exifOrientationAt]!=6)
        throw std::runtime_error("Unexpected public synthetic fixture layout");
    auto exifOnly=source;
    exifOnly[exifOrientationAt]=1; // actual observed synthetic Photos save
    const std::wstring output=L"samples\\synthetic_exif_only_"+
        std::to_wstring(GetCurrentProcessId())+L"_"+
        std::to_wstring(GetTickCount64())+L".heic";
    HANDLE handle=CreateFileW(output.c_str(),GENERIC_WRITE,0,nullptr,
                              CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)
        throw std::runtime_error("Refused to overwrite artificial Exif derivative");
    DWORD written=0;
    const BOOL ok=WriteFile(handle,exifOnly.data(),DWORD(exifOnly.size()),
                            &written,nullptr);
    CloseHandle(handle);
    if(!ok || written!=exifOnly.size())
        throw std::runtime_error("Failed writing new synthetic Exif test file");
    CComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
         IID_PPV_ARGS(&factory)),"create Exif-only WIC factory");
    UINT w0=0,h0=0,w1=0,h1=0;
    const auto p0=DecodedRGB(factory,
        L"samples\\fixture-irot3-exif6-noaux.heic",&w0,&h0);
    const auto p1=DecodedRGB(factory,output.c_str(),&w1,&h1);
    if(w0!=w1 || h0!=h1 || p0!=p1)
        throw std::runtime_error("Exif-only change altered WIC visible RGB or geometry");
    if(source[irotAngleAt]!=exifOnly[irotAngleAt])
        throw std::runtime_error("Exif-only synthetic test changed irot");
    printf("WIC_PHOTOS_EXIF_ONLY_NOT_ACTUAL_90_ROTATION_PASS "
           "size=%zu irot=3 exif=6_to_1 same_geometry=1 same_WIC_RGB=1\n",
           source.size());
}
} // namespace WICPhotosExifOnlyProbe
