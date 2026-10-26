#pragma once
// Artificial HEIC only. 50 Photos-style WIC saves as synchronized bursts of
// eight concurrent Encoder::Commit operations, each to a unique NEW output.
// This models *overlapping* fast clicks, unlike the sequential --stress50.
// No source ever opened writable. Fail closed on any ambiguous/partial output.
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <exception>
#include <stdexcept>
#include <cstdio>

namespace WICPhotosBurst50Probe {
inline void Require(HRESULT hr,const char* label) {
    if(FAILED(hr)) {
        printf("WIC_PHOTOS_BURST_FAILURE=%s hr=0x%08X\n",label,(unsigned)hr);
        throw std::runtime_error(label);
    }
}
inline void One(int ordinal,const std::wstring& root,
                std::atomic<int>& ready,std::atomic<bool>& release) {
    bool joined=false;
    const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(init)) {ready.fetch_add(1);throw std::runtime_error("Burst worker MTA failed");}
    try {
        CComPtr<IWICImagingFactory> factory;
        Require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
             CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"burst factory");
        CComPtr<IWICBitmapDecoder> source;
        Require(factory->CreateDecoderFromFilename(
             L"samples\\fixture-irot3-exif6-noaux.heic",
             nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&source),
             "burst open shared public synthetic file");
        UINT count=0;
        Require(source->GetFrameCount(&count),"burst frame count");
        if(count!=2)throw std::runtime_error("Burst fixture frame count changed");
        CComPtr<IWICBitmapFrameDecode> primary,aux;
        Require(source->GetFrame(0,&primary),"burst primary");
        Require(source->GetFrame(1,&aux),"burst second");
        UINT w=0,h=0,aw=0,ah=0;
        Require(primary->GetSize(&w,&h),"burst primary dimensions");
        Require(aux->GetSize(&aw,&ah),"burst second dimensions");
        CComPtr<IWICBitmapFlipRotator> rotator;
        Require(factory->CreateBitmapFlipRotator(&rotator),"burst flip");
        Require(rotator->Initialize(primary,WICBitmapTransformRotate90),
                "burst 90 rotation");
        CComPtr<IWICBitmap> opaque;
        Require(factory->CreateBitmapFromSource(rotator,
             WICBitmapCacheOnLoad,&opaque),"burst cache rotated image");
        const std::wstring out=root+L"\\operation-"+std::to_wstring(ordinal)+L".heic";
        if(GetFileAttributesW(out.c_str())!=INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("Burst output already exists");
        CComPtr<IWICBitmapEncoder> encoder;
        Require(factory->CreateEncoder(GUID_ContainerFormatHEIC,
             &GUID_VendorPrsyahmi,&encoder),"burst encoder");
        CComPtr<IWICStream> stream;
        Require(factory->CreateStream(&stream),"burst stream");
        Require(stream->InitializeFromFilename(out.c_str(),GENERIC_WRITE),
                "burst NEW stream");
        Require(encoder->Initialize(stream,WICBitmapEncoderNoCache),
                "burst encoder Initialize");
        for(int i=0;i<2;i++){
            CComPtr<IWICBitmapFrameEncode> dest;
            Require(encoder->CreateNewFrame(&dest,nullptr),"burst frame create");
            Require(dest->Initialize(nullptr),"burst frame initialize");
            Require(dest->SetSize(i==0?h:aw,i==0?w:ah),"burst frame SetSize");
            WICPixelFormatGUID fmt=GUID_WICPixelFormat24bppRGB;
            Require(dest->SetPixelFormat(&fmt),"burst frame SetPixelFormat");
            if(!IsEqualGUID(fmt,GUID_WICPixelFormat24bppRGB))
                throw std::runtime_error("Burst pixel format changed");
            Require(dest->WriteSource(i==0?
                static_cast<IWICBitmapSource*>(opaque.p):
                static_cast<IWICBitmapSource*>(aux.p),nullptr),
                "burst WriteSource");
            Require(dest->Commit(),"burst frame Commit");
        }
        // Every worker has prepared its entire WIC save pipeline but has NOT
        // invoked Encoder::Commit. Trigger all eight in the same wave.
        joined=true;
        ready.fetch_add(1);
        while(!release.load())std::this_thread::yield();
        Require(encoder->Commit(),"burst overlapping Encoder Commit");
        encoder.Release();stream.Release();
    } catch(...) {
        if(!joined)ready.fetch_add(1);
        CoUninitialize();
        throw;
    }
    CoUninitialize();
}
inline void Run() {
    constexpr int total=50;
    constexpr int waveSize=8;
    const std::wstring root=L"samples\\synthetic_burst50_"+
        std::to_wstring(GetCurrentProcessId())+L"_"+
        std::to_wstring(GetTickCount64());
    if(!CreateDirectoryW(root.c_str(),nullptr))
        throw std::runtime_error("Cannot create fresh burst folder");
    const auto original=WICPhotosOpaqueRotationProbe::Read(
        L"samples\\fixture-irot3-exif6-noaux.heic");
    const auto expected=HeicLosslessNative::Transform(original,1);
    for(int start=0;start<total;start+=waveSize){
        const int count=(total-start < waveSize)?total-start:waveSize;
        std::atomic<int> ready{0};
        std::atomic<bool> release{false};
        std::vector<std::thread> threads;
        std::vector<std::exception_ptr> errors(count);
        threads.reserve(count);
        for(int i=0;i<count;i++){
            threads.emplace_back([&,i]{
                try {One(start+i+1,root,ready,release);}
                catch(...) {errors[i]=std::current_exception();}
            });
        }
        while(ready.load()!=count)std::this_thread::yield();
        release.store(true);
        for(auto& t:threads)t.join();
        for(int i=0;i<count;i++){
            if(errors[i])std::rethrow_exception(errors[i]);
            const std::wstring file=root+L"\\operation-"+
                std::to_wstring(start+i+1)+L".heic";
            const auto saved=WICPhotosOpaqueRotationProbe::Read(file.c_str());
            if(saved!=expected)
                throw std::runtime_error("Concurrent HEIF was not byte-exact: metadata loss/partial output");
        }
        printf("WIC_PHOTOS_BURST_WAVE_PASS completed=%d\n",start+count);
    }
    printf("WIC_PHOTOS_FIFTY_OVERLAPPING_COMMIT_BYTE_EXACT_PASS count=50 maxConcurrent=8\n");
}
} // namespace WICPhotosBurst50Probe
