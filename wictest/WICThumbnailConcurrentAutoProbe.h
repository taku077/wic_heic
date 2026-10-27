#pragma once
// Disposable CI only: stress two different HEIC COM frames decoding 12MP
// in parallel. Compare BOTH output thumbnails against a serial baseline.
// No device changes, no user photos, no Explorer invocation.
#include <windows.h>
#include <wincodec.h>
#include <atlbase.h>
#include <array>
#include <atomic>
#include <thread>
#include <vector>
#include <exception>
#include <stdexcept>
#include <cstdio>
#include <string>
namespace WICThumbnailConcurrentAutoProbe {
inline void Check(HRESULT hr,const char* stage) {
    if(FAILED(hr)){
        printf("THUMB_CONCURRENT_AUTO_FAIL stage=%s hr=0x%08X\n",
               stage,unsigned(hr));
        throw std::runtime_error(stage);
    }
}
inline void Run(){
    wchar_t ci[12]{};
    if(GetEnvironmentVariableW(L"GITHUB_ACTIONS",ci,12)!=4 ||
       wcscmp(ci,L"true")!=0)
        throw std::runtime_error("Concurrent HEVC worker test is CI-only");
    wchar_t temp[32768]{};
    const DWORD n=GetEnvironmentVariableW(L"RUNNER_TEMP",temp,32768);
    if(n==0 || n>=32768)
        throw std::runtime_error("Missing runner temporary directory");
    const std::wstring fixture=std::wstring(temp,n)+
        L"\\wicfast_synthetic_4032x3024.heic";
    if(GetFileAttributesW(fixture.c_str())==INVALID_FILE_ATTRIBUTES)
        throw std::runtime_error("Missing 12MP artificial fixture");
    std::array<std::vector<BYTE>,3> images;
    std::array<std::exception_ptr,3> errors{};
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    auto decode=[&](int i,bool parallel){
        const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        bool initialized=SUCCEEDED(com);
        bool announced=false;
        try {
            Check(com,"COM MTA worker");
            CComPtr<IWICImagingFactory> factory;
            Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
                CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"MTA WIC factory");
            CComPtr<IWICStream> stream;
            Check(factory->CreateStream(&stream),"MTA stream");
            Check(stream->InitializeFromFilename(fixture.c_str(),GENERIC_READ),
                "MTA stream read");
            CComPtr<IWICBitmapDecoder> decoder;
            Check(CoCreateInstance(CLSID_HEICDecoder,nullptr,
                CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&decoder)),"MTA codec");
            Check(decoder->Initialize(stream,WICDecodeMetadataCacheOnDemand),
                "MTA decoder init");
            CComPtr<IWICBitmapFrameDecode> frame;
            Check(decoder->GetFrame(0,&frame),"MTA first frame");
            CComPtr<IWICBitmapScaler> scaler;
            Check(factory->CreateBitmapScaler(&scaler),"MTA scaler");
            Check(scaler->Initialize(frame,256,192,WICBitmapInterpolationModeFant),
                "MTA scaler initialize");
            // All WIC objects above are thread-local COM instances.
            // The barrier simultaneously begins the slow HEVC decode.
            if(parallel){
                ready.fetch_add(1,std::memory_order_release);
                announced=true;
                while(!start.load(std::memory_order_acquire)) Sleep(1);
            }
            images[i].resize(size_t(256)*192u*3u);
            Check(scaler->CopyPixels(nullptr,256u*3u,UINT(images[i].size()),
                images[i].data()),"MTA 12MP first thumbnail");
        }catch(...){
            errors[i]=std::current_exception();
            // Always release a waiting other worker after setup failure.
            if(parallel && !announced)ready.fetch_add(1,std::memory_order_release);
        }
        if(initialized)CoUninitialize();
    };
    // A fresh, default nonparallel decoding run establishes RGB truth.
    if(!SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",L"0"))
        throw std::runtime_error("Cannot select serial lab baseline");
    {
        std::thread baseline([&]{decode(0,false);});
        baseline.join();
    }
    if(errors[0])std::rethrow_exception(errors[0]);
    if(!SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",L"a"))
        throw std::runtime_error("Cannot select adaptive lab mode");
    std::thread first([&]{decode(1,true);});
    std::thread second([&]{decode(2,true);});
    const ULONGLONG deadline=GetTickCount64()+30000u;
    while(ready.load(std::memory_order_acquire)<2 &&
          GetTickCount64()<deadline) Sleep(5);
    const bool launched=ready.load(std::memory_order_acquire)==2;
    start.store(true,std::memory_order_release);
    first.join();second.join();
    SetEnvironmentVariableW(L"WIC_HEIC_THUMB_LAB_WORKERS",nullptr);
    if(!launched)throw std::runtime_error("Parallel decoder setup timeout");
    for(const auto& failure:errors)
        if(failure)std::rethrow_exception(failure);
    if(images[0].size()!=size_t(256)*192u*3u ||
       images[1]!=images[0] || images[2]!=images[0])
        throw std::runtime_error("Concurrent adaptive HEVC thumbnail RGB mismatch");
    printf("THUMB_CONCURRENT_AUTO_RGB_PARITY_PASS parallel_frames=2 "
           "source=4032x3024 thumb=256x192 exact_rgb=1\n");
}
} // namespace WICThumbnailConcurrentAutoProbe
