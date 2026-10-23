#pragma once
#include <thread>
#include <atomic>
#include <array>
#include <exception>
// Windows Photos GUI owns zoom/next/previous controls. This regression
// verifies the WIC data contract those controls rely on: independent file
// opens, full-resolution CopyPixels and exact cropped regions. Synthetic
// samples only; no user photos or writable source handles.
namespace PhotosNavigationZoomProbe {
inline void Probe(IWICImagingFactory* fac,const wchar_t* path) {
    CComPtr<IWICBitmapDecoder> dec;
    HRESULT hr=fac->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,
         WICDecodeMetadataCacheOnDemand,&dec);
    if(FAILED(hr))throw std::runtime_error("Photos navigation WIC decoder unavailable");
    CComPtr<IWICBitmapFrameDecode> frame;
    if(FAILED(dec->GetFrame(0,&frame)))
        throw std::runtime_error("Photos navigation cannot open frame");
    UINT w=0,h=0;
    if(FAILED(frame->GetSize(&w,&h)) || !w || !h)
        throw std::runtime_error("Photos navigation wrong dimensions");
    WICPixelFormatGUID fmt{};
    if(FAILED(frame->GetPixelFormat(&fmt)))
        throw std::runtime_error("Photos navigation unknown pixel format");
    UINT bpp=0;
    if(IsEqualGUID(fmt,GUID_WICPixelFormat24bppRGB) ||
       IsEqualGUID(fmt,GUID_WICPixelFormat24bppBGR)) bpp=3;
    else if(IsEqualGUID(fmt,GUID_WICPixelFormat32bppRGBA) ||
            IsEqualGUID(fmt,GUID_WICPixelFormat32bppBGRA)) bpp=4;
    else if(IsEqualGUID(fmt,GUID_WICPixelFormat48bppRGB) ||
            IsEqualGUID(fmt,GUID_WICPixelFormat48bppBGR)) bpp=6;
    else if(IsEqualGUID(fmt,GUID_WICPixelFormat64bppRGBA) ||
            IsEqualGUID(fmt,GUID_WICPixelFormat64bppBGRA)) bpp=8;
    else throw std::runtime_error("Unexpected synthetic Photos pixel format");
    const uint64_t stride64=uint64_t(w)*bpp;
    const uint64_t bytes64=stride64*h;
    if(bytes64>64ull*1024*1024 || stride64>UINT_MAX)
        throw std::runtime_error("Unbounded WIC synthetic fixture");
    const UINT stride=UINT(stride64);
    std::vector<BYTE> full(static_cast<size_t>(bytes64));
    if(FAILED(frame->CopyPixels(nullptr,stride,UINT(full.size()),full.data())))
        throw std::runtime_error("Photos full-resolution pixels unavailable");
    const UINT pw=(std::min)(128u,w),ph=(std::min)(128u,h);
    WICRect region{INT((w-pw)/2),INT((h-ph)/2),INT(pw),INT(ph)};
    std::vector<BYTE> partial(size_t(pw)*ph*bpp);
    const UINT partialStride=pw*bpp;
    if(FAILED(frame->CopyPixels(&region,partialStride,
                UINT(partial.size()),partial.data())))
        throw std::runtime_error("Photos zoomed ROI pixels unavailable");
    for(UINT y=0;y<ph;y++) {
        const BYTE* expected=full.data()+
            (size_t(region.Y)+y)*stride+size_t(region.X)*bpp;
        const BYTE* got=partial.data()+size_t(y)*partialStride;
        if(memcmp(expected,got,partialStride)!=0)
            throw std::runtime_error("Photos zoom ROI differs from full-resolution image");
    }
    printf("WIC_PHOTOS_FULLRES_ZOOM_REGION_EXACT_PASS width=%u height=%u bpp=%u\n",
           w,h,bpp*8u);
}
// Synthetic-only MTA stress: all worker threads share ONE decoded WIC frame.
// Creating the frame inside an MTA thread avoids crossing the main test's
// STA boundary with an unmarshaled COM interface. Cold-start CopyPixels
// races are where the previous unprotected m_Decoded/m_Image writes occurred.
inline void VerifySharedFrameConcurrentMta() {
    std::exception_ptr failure;
    std::thread harness([&] {
        const HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(FAILED(apartment)) {
            failure=std::make_exception_ptr(
                std::runtime_error("MTA synthetic harness COM init failed"));
            return;
        }
        try {
            {
                CComPtr<IWICImagingFactory> factory;
                if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
                    CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
                    reinterpret_cast<void**>(&factory))))
                    throw std::runtime_error("MTA frame test factory unavailable");
                CComPtr<IWICBitmapDecoder> decoder;
                if(FAILED(factory->CreateDecoderFromFilename(
                    L"samples\\fixture-irot3-exif6-noaux.heic",nullptr,
                    GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder)))
                    throw std::runtime_error("MTA frame test HEIC decoder unavailable");
                CComPtr<IWICBitmapFrameDecode> shared;
                if(FAILED(decoder->GetFrame(0,&shared)))
                    throw std::runtime_error("MTA frame test synthetic frame missing");
                constexpr size_t n=8;
                constexpr UINT width=48,height=48,stride=width*3;
                const WICRect box{0,0,INT(width),INT(height)};
                std::array<std::vector<BYTE>,n> pixels;
                std::array<HRESULT,n> results{};
                std::atomic<unsigned> ready{0};
                std::atomic<bool> fire{false};
                std::vector<std::thread> workers;
                workers.reserve(n);
                for(size_t i=0;i<n;i++) {
                    pixels[i].resize(size_t(stride)*height);
                    workers.emplace_back([&,i] {
                        const HRESULT apartmentWorker=CoInitializeEx(
                            nullptr,COINIT_MULTITHREADED);
                        ready.fetch_add(1,std::memory_order_release);
                        while(!fire.load(std::memory_order_acquire))
                            std::this_thread::yield();
                        if(SUCCEEDED(apartmentWorker)) {
                            results[i]=shared->CopyPixels(&box,stride,
                                UINT(pixels[i].size()),pixels[i].data());
                            CoUninitialize();
                        } else {
                            results[i]=apartmentWorker;
                        }
                    });
                }
                while(ready.load(std::memory_order_acquire)!=n)
                    std::this_thread::yield();
                fire.store(true,std::memory_order_release);
                for(auto& worker:workers)worker.join();
                for(size_t i=0;i<n;i++) {
                    if(FAILED(results[i])||pixels[i]!=pixels[0])
                        throw std::runtime_error(
                            "Concurrent MTA CopyPixels corrupted common HEIC frame");
                }
                puts("WIC_PHOTOS_SHARED_FRAME_MTA_COPYPIXELS_8WAY_IDENTICAL_PASS");
            }
        } catch(...) {
            failure=std::current_exception();
        }
        CoUninitialize();
    });
    harness.join();
    if(failure)std::rethrow_exception(failure);
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
        (void**)&factory)))throw std::runtime_error("Zoom imaging factory");
    // Navigate image A -> B -> A with independent WIC decoder lifetimes;
    // the Photos GUI's actual keyboard controls cannot run on Server CI.
    Probe(factory,L"samples\\fixture-irot3-exif6-noaux.heic");
    Probe(factory,L"samples\\fixture-irot3-exif6.heic");
    Probe(factory,L"samples\\fixture-irot3-exif6-noaux.heic");
    VerifySharedFrameConcurrentMta();
    puts("WIC_PHOTOS_MULTIFILE_NAVIGATION_DECODE_REGRESSION_PASS");
}
}
