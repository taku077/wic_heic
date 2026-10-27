#pragma once
// Synthetic Windows WIC regression: simultaneous first CopyPixels on ONE
// HEIC frame in the same COM MTA. Detects races during lazy libheif decode.
// No real photographs and no files are opened for writing.
#include <array>
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <cstdio>

namespace WICDecoderConcurrentProbe {
inline void Check(HRESULT hr,const char* why){
    if(FAILED(hr)){
        printf("WIC_SAME_FRAME_MTA_FAILURE=%s HRESULT=0x%08X\n",
               why,static_cast<unsigned>(hr));
        throw std::runtime_error(why);
    }
}
// Concurrently request *new* frames from the same decoder. Unlike the
// existing CopyPixels stress test, this starts with an empty per-image handle
// cache so simultaneous first lookups must not allocate or publish twice.
inline void RunConcurrentGetFrame(IWICImagingFactory* factory) {
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",
        nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,
        &decoder),"open concurrent frame source");
    constexpr int workers=4;
    constexpr int perWorker=32;
    std::array<HRESULT,workers> outcomes{};
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    std::array<std::thread,workers> threads;
    IWICBitmapDecoder* const shared=decoder;
    for(int i=0;i<workers;i++) {
        threads[i]=std::thread([&,i] {
            HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            if(FAILED(hr)) {outcomes[i]=hr;ready.fetch_add(1);return;}
            ready.fetch_add(1);
            while(!go.load(std::memory_order_acquire))
                std::this_thread::yield();
            for(int j=0;j<perWorker;j++) {
                CComPtr<IWICBitmapFrameDecode> frame;
                hr=shared->GetFrame(0,&frame);
                if(FAILED(hr))break;
                UINT w=0,h=0;
                hr=frame->GetSize(&w,&h);
                if(FAILED(hr))break;
                WICPixelFormatGUID fmt{};
                hr=frame->GetPixelFormat(&fmt);
                if(FAILED(hr))break;
                if(w!=1080 || h!=1920 ||
                   !IsEqualGUID(fmt,GUID_WICPixelFormat24bppRGB)) {
                    hr=E_FAIL;
                    break;
                }
            }
            outcomes[i]=hr;
            CoUninitialize();
        });
    }
    while(ready.load()!=workers)std::this_thread::yield();
    go.store(true,std::memory_order_release);
    for(auto& thread:threads)thread.join();
    for(auto hr:outcomes)
        Check(hr,"concurrent fresh GetFrame/dimensions/pixel format");
    printf("WIC_HEIF_CONCURRENT_GETFRAME_HANDLE_REUSE_PASS workers=4 calls=128\n");
}
// Same-context, alternating A/B microbenchmark of the *native* work that
// the new GetFrame cache avoids. This is NOT an end-to-end Photos benchmark.
// No WIC diagnostic I/O is involved in these two pure libheif paths.
// Report medians only; never fail CI on speed measurements.
inline void RunNativeHandleAB() {
    heif::Context context;
    context.read_from_file("samples\\fixture-irot3-exif6-noaux.heic");
    const auto ids=context.get_list_of_top_level_image_IDs();
    if(ids.size()!=2)throw std::runtime_error("Native A/B fixture frame count");
    const heif_item_id id=ids[0];
    const heif::ImageHandle shared=context.get_image_handle(id);
    if(shared.empty())throw std::runtime_error("Native A/B handle missing");
    constexpr int rounds=6, repetitions=3000;
    std::vector<double> uncached, reused;
    long long guardA=0,guardB=0;
    LARGE_INTEGER frequency{};
    if(!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0)
        throw std::runtime_error("Native A/B QPC unavailable");
    const auto measure=[&](bool useCache,long long& checksum)->double {
        LARGE_INTEGER start{},end{};
        QueryPerformanceCounter(&start);
        for(int i=0;i<repetitions;i++){
            const heif::ImageHandle frame=useCache
                ?shared:context.get_image_handle(id);
            if(frame.empty())throw std::runtime_error("Native A/B empty handle");
            checksum+=frame.get_width()+frame.get_height();
        }
        QueryPerformanceCounter(&end);
        return 1000.0*double(end.QuadPart-start.QuadPart)/
                      double(frequency.QuadPart);
    };
    for(int round=0;round<rounds;round++){
        if((round&1)==0){
            uncached.push_back(measure(false,guardA));
            reused.push_back(measure(true,guardB));
        }else{
            reused.push_back(measure(true,guardB));
            uncached.push_back(measure(false,guardA));
        }
    }
    if(guardA!=guardB||guardA==0)
        throw std::runtime_error("Native A/B handle geometry mismatch");
    std::sort(uncached.begin(),uncached.end());
    std::sort(reused.begin(),reused.end());
    const double a=(uncached[rounds/2-1]+uncached[rounds/2])/2.0;
    const double b=(reused[rounds/2-1]+reused[rounds/2])/2.0;
    printf("WIC_HEIF_NATIVE_HANDLE_AB same_context=1 image_calls=%d "
           "rounds=%d uncached_median_ms=%.5f shared_median_ms=%.5f "
           "checksum_equal=1 no_photos_speed_claim=1\n",
           repetitions,rounds,a,b);
}
inline void RunInMta() {
    Check(CoInitializeEx(nullptr,COINIT_MULTITHREADED),"init MTA coordinator");
    try {
        CComPtr<IWICImagingFactory> factory;
        Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
             CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"create factory");
        RunNativeHandleAB();
        RunConcurrentGetFrame(factory);
        CComPtr<IWICBitmapDecoder> decoder;
        Check(factory->CreateDecoderFromFilename(
             L"samples\\fixture-irot3-exif6-noaux.heic",
             nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,
             &decoder),"open same-frame source");
        CComPtr<IWICBitmapFrameDecode> frame;
        Check(decoder->GetFrame(0,&frame),"fetch same frame");
        UINT w=0,h=0;
        Check(frame->GetSize(&w,&h),"same frame dimensions");
        if(w!=1080 || h!=1920)
            throw std::runtime_error("unexpected WIC concurrent fixture geometry");
        const UINT stride=w*3;
        const size_t size=size_t(stride)*h;
        constexpr int workers=4;
        std::array<std::vector<BYTE>,workers> outputs;
        std::array<HRESULT,workers> results{};
        for(auto& out:outputs)out.resize(size,0xAD);
        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        std::array<std::thread,workers> threads;
        IWICBitmapFrameDecode* const shared=frame;
        for(int i=0;i<workers;i++){
            threads[i]=std::thread([&,i]{
                const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
                if(FAILED(init)) {
                    results[i]=init;ready.fetch_add(1);
                    return;
                }
                ready.fetch_add(1);
                while(!go.load())std::this_thread::yield();
                results[i]=shared->CopyPixels(nullptr,stride,UINT(size),
                                               outputs[i].data());
                CoUninitialize();
            });
        }
        while(ready.load()!=workers)std::this_thread::yield();
        go.store(true);
        for(auto& t:threads)t.join();
        for(int i=0;i<workers;i++){
            Check(results[i],"concurrent first CopyPixels");
            if(std::memcmp(outputs[0].data(),outputs[i].data(),size)!=0)
                throw std::runtime_error("same HEIC frame produced inconsistent RGB");
        }
        // Verify a small offset rectangle on the already-published plane,
        // including zero changes to caller padding.
        WICRect rc{17,29,41,31};
        const UINT pitch=UINT(rc.Width*3+9);
        std::vector<BYTE> cropped(size_t(pitch)*rc.Height,0xAC);
        Check(shared->CopyPixels(&rc,pitch,UINT(cropped.size()),cropped.data()),
              "post-concurrency rectangle");
        for(int y=0;y<rc.Height;y++){
            const BYTE* expected=outputs[0].data()+
                size_t(rc.Y+y)*stride+size_t(rc.X)*3;
            const BYTE* actual=cropped.data()+size_t(y)*pitch;
            if(std::memcmp(expected,actual,size_t(rc.Width)*3)!=0)
                throw std::runtime_error("concurrent decoded crop differs");
            for(UINT x=UINT(rc.Width)*3;x<pitch;x++)
                if(actual[x]!=0xAC)
                    throw std::runtime_error("concurrent frame touched destination padding");
        }
        printf("WIC_SAME_FRAME_CONCURRENT_FIRST_DECODE_RGB_EQUAL_PASS workers=4\n");
    } catch(...) {
        CoUninitialize();
        throw;
    }
    CoUninitialize();
}
inline void Run() {
    // wictest's main thread is STA. The WIC object and every worker here
    // share the single COM MTA, so no cross-apartment raw pointers are used.
    std::exception_ptr failure;
    std::thread worker([&]{
        try {RunInMta();}
        catch(...) {failure=std::current_exception();}
    });
    worker.join();
    if(failure)std::rethrow_exception(failure);
}
}
