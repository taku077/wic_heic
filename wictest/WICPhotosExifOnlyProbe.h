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
// Regression based on the user's 2026-10-26 *artificial* v0.3 trace.
// Observed irot transitions: 3,2,3,2,1,0,3,2,1,0,3 with Exif normalized
// to 1 after first save. Verify the WIC decoder never returns stale geometry
// when reopened for each state, even when a rotation appears to go backward.
// Does not use the user's Photos logs or real images in CI.
inline void RunObservedIrotSequence() {
    const auto original=WICPhotosOpaqueRotationProbe::Read(
        L"samples\\fixture-irot3-exif6-noaux.heic");
    constexpr size_t irotAt=567, exifAt=10589;
    if(original.size()!=10597 || original[irotAt]!=3 || original[exifAt]!=6)
        throw std::runtime_error("Public synthetic irot fixture changed");
    constexpr BYTE trace[]={3,2,3,2,1,0,3,2,1,0,3};
    const std::wstring root=L"samples\\artificial_logged_irot_"+
        std::to_wstring(GetCurrentProcessId())+L"_"+
        std::to_wstring(GetTickCount64());
    if(!CreateDirectoryW(root.c_str(),nullptr))
        throw std::runtime_error("Could not create fresh irot-sequence folder");
    CComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
          IID_PPV_ARGS(&factory)),"irot sequence WIC factory");
    for(size_t i=0;i<sizeof(trace);++i) {
        auto content=original;
        content[irotAt]=trace[i];
        if(i>0)content[exifAt]=1;
        const std::wstring path=root+L"\\state-"+std::to_wstring(i)+L".heic";
        HANDLE out=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,
            CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(out==INVALID_HANDLE_VALUE)
            throw std::runtime_error("Refused to overwrite synthetic state");
        DWORD written=0;
        BOOL ok=WriteFile(out,content.data(),DWORD(content.size()),&written,
                          nullptr);
        CloseHandle(out);
        if(!ok || written!=content.size())
            throw std::runtime_error("Synthetic irot state write failed");
        UINT w=0,h=0;
        DecodedRGB(factory,path.c_str(),&w,&h);
        const bool portrait=(trace[i]&1)!=0;
        if(w!=(portrait?1080u:1920u) ||
           h!=(portrait?1920u:1080u)) {
            printf("WIC_IROT_SEQUENCE_STALE_GEOMETRY state=%zu irot=%u "
                   "actual=%ux%u\n",i,UINT(trace[i]),w,h);
            throw std::runtime_error("WIC returned stale or stretched geometry");
        }
        if(i && WICPhotosOpaqueRotationProbe::Read(path.c_str())!=content)
            throw std::runtime_error("Source artificial metadata altered");
    }
    printf("WIC_PHOTOS_OBSERVED_11_IROT_STATES_MATCH_GEOMETRY_PASS "
           "last_irot=3 last_dims=1080x1920\n");
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
    RunObservedIrotSequence();
}
} // namespace WICPhotosExifOnlyProbe
