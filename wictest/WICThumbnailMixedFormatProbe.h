#pragma once
// CI-only mixed-source three-frame COM MTA thumbnail parity test.
// Verifies opaque 8bit, HEIF-alpha 8bit and high-depth 10bit HEIC in
// parallel. The WIC 1.1.28 frame currently exposes 24bpp RGB; this test
// DOES NOT claim to preserve alpha samples or 10-bit precision.
#include <windows.h>
#include <wincodec.h>
#include <atlbase.h>
#include <array>
#include <atomic>
#include <thread>
#include <vector>
#include <exception>
#include <stdexcept>
#include <string>
#include <cstdio>
namespace WICThumbnailMixedFormatProbe {
struct Fixture {
    std::wstring path;
    UINT imageW,imageH,thumbW,thumbH;
};
inline void Check(HRESULT hr,const char* s) {
    if(FAILED(hr)) {
        printf("THUMB_MIXED_WIC_FAIL stage=%s hr=0x%08X\n",s,
            static_cast<unsigned>(hr));
        throw std::runtime_error(s);
    }
}
inline std::vector<BYTE> Decode(const Fixture& input,
                               std::atomic<int>* ready,
                               std::atomic<bool>* release) {
    const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(apartment)){
        printf("THUMB_MIXED_COM_INIT_FAIL hr=0x%08X\n",
               static_cast<unsigned>(apartment));
        throw std::runtime_error("Mixed MTA COM init");
    }
    // Scope guard is declared before COM pointers so all COM releases
    // happen BEFORE CoUninitialize, including return and exceptions.
    struct CoExit { ~CoExit(){CoUninitialize();} } apartmentGuard;
        CComPtr<IWICImagingFactory> factory;
        Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
            CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"mixed WIC factory");
        CComPtr<IWICStream> stream;
        Check(factory->CreateStream(&stream),"mixed stream create");
        Check(stream->InitializeFromFilename(input.path.c_str(),GENERIC_READ),
            "mixed fixture stream");
        CComPtr<IWICBitmapDecoder> decoder;
        Check(CoCreateInstance(CLSID_HEICDecoder,nullptr,
            CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&decoder)),"mixed direct codec");
        Check(decoder->Initialize(stream,WICDecodeMetadataCacheOnDemand),
            "mixed codec initialize");
        CComPtr<IWICBitmapFrameDecode> frame;
        Check(decoder->GetFrame(0,&frame),"mixed get frame");
        UINT w=0,h=0;
        Check(frame->GetSize(&w,&h),"mixed size");
        if(w!=input.imageW||h!=input.imageH)
            throw std::runtime_error("Mixed fixture size mismatch");
        WICPixelFormatGUID format{};
        Check(frame->GetPixelFormat(&format),"mixed pixel format");
        // Existing decoder flattens HDR and alpha into 24bpp RGB.
        // Check the *actual exposed format*, not an intended new capability.
        if(!IsEqualGUID(format,GUID_WICPixelFormat24bppRGB))
            throw std::runtime_error("Unexpected existing WIC format");
        CComPtr<IWICBitmapScaler> scaler;
        Check(factory->CreateBitmapScaler(&scaler),"mixed scaler");
        Check(scaler->Initialize(frame,input.thumbW,input.thumbH,
            WICBitmapInterpolationModeFant),"mixed Fant");
        if(ready && release) {
            ready->fetch_add(1,std::memory_order_release);
            while(!release->load(std::memory_order_acquire))Sleep(1);
        }
        const UINT rowBytes=input.thumbW*3u;
        std::vector<BYTE> pixels(size_t(rowBytes)*input.thumbH);
        Check(scaler->CopyPixels(nullptr,rowBytes,UINT(pixels.size()),
            pixels.data()),"mixed thumbnail CopyPixels");
        return pixels;
}
inline void Run(){
    wchar_t val[12]{};
    if(GetEnvironmentVariableW(L"GITHUB_ACTIONS",val,12)!=4||
       wcscmp(val,L"true")!=0)
        throw std::runtime_error("Mixed test runs in CI only");
    wchar_t tmp[32768]{};
    const DWORD n=GetEnvironmentVariableW(L"RUNNER_TEMP",tmp,32768);
    if(n==0||n>=32768)throw std::runtime_error("No RUNNER_TEMP");
    const std::wstring root=std::wstring(tmp,n);
    const std::array<Fixture,3> fixture{{
        {root+L"\\wicfast_synthetic_4032x3024.heic",4032,3024,256,192},
        {root+L"\\wicfast_synthetic_4032x3024_alpha.heic",4032,3024,256,192},
        {L"samples\\fixture-irot3-exif6-noaux.heic",1080,1920,144,256}
    }};
    for(const auto& source:fixture){
        const DWORD flags=GetFileAttributesW(source.path.c_str());
        if(flags==INVALID_FILE_ATTRIBUTES||(flags&FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("Missing alpha/10-bit/opaque CI fixture");
    }
    std::array<std::vector<BYTE>,3> reference,concurrent;
    if(!SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",L"0"))
        throw std::runtime_error("Cannot set default HEVC mode");
    for(int i=0;i<3;i++)
        reference[i]=Decode(fixture[i],nullptr,nullptr);
    if(!SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",L"a"))
        throw std::runtime_error("Cannot set auto HEVC mode");
    std::atomic<int> ready{0};
    std::atomic<bool> release{false};
    std::array<std::exception_ptr,3> failure{};
    std::array<std::thread,3> threads;
    try {
        for(int i=0;i<3;i++){
            threads[i]=std::thread([&,i] {
                try {concurrent[i]=Decode(fixture[i],&ready,&release);}
                catch(...) {
                    failure[i]=std::current_exception();
                    // Deadline + release ensures every other thread can exit.
                }
            });
        }
    }catch(...){
        release.store(true,std::memory_order_release);
        for(auto& thread:threads)if(thread.joinable())thread.join();
        SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",nullptr);
        throw;
    }
    const ULONGLONG end=GetTickCount64()+30000u;
    while(ready.load(std::memory_order_acquire)<3 &&
          GetTickCount64()<end)Sleep(5);
    const bool launched=ready.load(std::memory_order_acquire)==3;
    release.store(true,std::memory_order_release);
    for(auto& thread:threads)if(thread.joinable())thread.join();
    SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",nullptr);
    for(const auto& error:failure)if(error)std::rethrow_exception(error);
    if(!launched)throw std::runtime_error("Mixed parallel setup timeout");
    for(int i=0;i<3;i++){
        if(reference[i].empty()||reference[i]!=concurrent[i])
            throw std::runtime_error("Mixed HEIC WIC output RGB mismatch");
    }
    printf("THUMB_MIXED_PARALLEL_RGB_PARITY_PASS fixtures=3 "
           "opaque8=1 alpha8=1 depth10=1 parallel=3 "
           "thumbnail_rgb_exact=1 source_alpha_precision_unverified=1\n");
}
} // namespace WICThumbnailMixedFormatProbe
