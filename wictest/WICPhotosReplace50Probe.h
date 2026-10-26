#pragma once
// Windows 10 Photos-like save-to-the-SAME-path stress, 50 iterations.
// Only the CHECKED-IN ARTIFICIAL HEIC is used, copied into a brand-new
// CI-only directory. Every prior working state is preserved as a separate
// backup. No user photos, no deletion, no overwrite of original fixture.
// Each save goes into a fresh temp path, then Windows ReplaceFileW swaps the
// scratch copy with a backup snapshot. Windows Explorer/Photos UI is NOT used.
#include <string>
#include <vector>
#include <cstdio>
#include <stdexcept>

namespace WICPhotosReplace50Probe {
inline void Must(HRESULT hr,const char* op) {
    if(FAILED(hr)) {
        printf("WIC_REPLACE50_FAILURE=%s hr=0x%08X\n",op,(unsigned)hr);
        throw std::runtime_error(op);
    }
}
inline std::wstring FreshRoot() {
    auto path=std::wstring(L"samples\\synthetic_replace50_")+
        std::to_wstring(GetCurrentProcessId())+L"_"+
        std::to_wstring(GetTickCount64());
    if(!CreateDirectoryW(path.c_str(),nullptr))
        throw std::runtime_error("Could not create new scratch folder");
    return path;
}
inline void OneCopy(IWICImagingFactory* factory,
                    const std::wstring& working,
                    const std::wstring& temporary) {
    // All source decoder COM objects are released BEFORE file replacement:
    // no dangling old file handles should interfere with ReplaceFileW.
    CComPtr<IWICBitmapDecoder> decoder;
    Must(factory->CreateDecoderFromFilename(working.c_str(),nullptr,
         GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
         "open working synthetic file read-only");
    UINT frameCount=0;
    Must(decoder->GetFrameCount(&frameCount),"working frame count");
    if(frameCount!=2)
        throw std::runtime_error("Working HEIF has lost its second frame");
    CComPtr<IWICBitmapFrameDecode> primary,secondary;
    Must(decoder->GetFrame(0,&primary),"working primary");
    Must(decoder->GetFrame(1,&secondary),"working secondary");
    UINT width=0,height=0,sw=0,sh=0;
    Must(primary->GetSize(&width,&height),"working main dimensions");
    Must(secondary->GetSize(&sw,&sh),"working secondary dimensions");
    if(!width || !height || !sw || !sh)
        throw std::runtime_error("Zero working HEIF dimensions");
    CComPtr<IWICBitmapFlipRotator> rotated;
    Must(factory->CreateBitmapFlipRotator(&rotated),"working flip");
    Must(rotated->Initialize(primary,WICBitmapTransformRotate90),
         "working clockwise 90");
    CComPtr<IWICBitmap> pixels;
    Must(factory->CreateBitmapFromSource(rotated,WICBitmapCacheOnLoad,&pixels),
         "working opaque RGB");
    CComPtr<IWICBitmapEncoder> encoder;
    Must(factory->CreateEncoder(GUID_ContainerFormatHEIC,
         &GUID_VendorPrsyahmi,&encoder),"new WIC HEIC encoder");
    CComPtr<IWICStream> output;
    Must(factory->CreateStream(&output),"working output stream");
    if(GetFileAttributesW(temporary.c_str())!=INVALID_FILE_ATTRIBUTES)
        throw std::runtime_error("Refused to overwrite working temporary");
    Must(output->InitializeFromFilename(temporary.c_str(),GENERIC_WRITE),
         "new scratch temporary");
    Must(encoder->Initialize(output,WICBitmapEncoderNoCache),
         "scratch HEIF encode init");
    for(int index=0;index<2;index++) {
        CComPtr<IWICBitmapFrameEncode> frame;
        Must(encoder->CreateNewFrame(&frame,nullptr),"working frame create");
        Must(frame->Initialize(nullptr),"working frame initialize");
        Must(frame->SetSize(index?sw:height,index?sh:width),
             "working output geometry");
        WICPixelFormatGUID format=GUID_WICPixelFormat24bppRGB;
        Must(frame->SetPixelFormat(&format),"working WIC RGB");
        if(!IsEqualGUID(format,GUID_WICPixelFormat24bppRGB))
            throw std::runtime_error("Working WIC pixel format changed");
        Must(frame->WriteSource(index?
              static_cast<IWICBitmapSource*>(secondary.p):
              static_cast<IWICBitmapSource*>(pixels.p),nullptr),
             "working Photos two-frame WriteSource");
        Must(frame->Commit(),"working frame Commit");
    }
    Must(encoder->Commit(),"working encoder Commit");
}
inline void Run() {
    constexpr int kTurns=50;
    const std::wstring root=FreshRoot();
    const std::wstring working=root+L"\\synthetic-working.heic";
    const wchar_t* fixture=L"samples\\fixture-irot3-exif6-noaux.heic";
    if(!CopyFileW(fixture,working.c_str(),TRUE))
        throw std::runtime_error("Could not copy artificial HEIC into fresh scratch");
    const auto untouched=WICPhotosOpaqueRotationProbe::Read(fixture);
    if(WICPhotosOpaqueRotationProbe::Read(working.c_str())!=untouched)
        throw std::runtime_error("Fresh scratch differs from original fixture");
    CComPtr<IWICImagingFactory> factory;
    Must(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
         IID_PPV_ARGS(&factory)),"scratch WIC factory");
    UINT originalW=0,originalH=0;
    for(int i=1;i<=kTurns;i++) {
        const std::wstring temp=root+L"\\candidate-"+std::to_wstring(i)+L".heic";
        const std::wstring backup=root+L"\\backup-before-"+std::to_wstring(i)+L".heic";
        if(GetFileAttributesW(backup.c_str())!=INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("Unexpected existing unique history backup");
        const auto previous=WICPhotosOpaqueRotationProbe::Read(working.c_str());
        const auto wanted=HeicLosslessNative::Transform(previous,1);
        // Release all decoder/encoder/stream instances before replacing file.
        OneCopy(factory,working,temp);
        if(WICPhotosOpaqueRotationProbe::Read(temp.c_str())!=wanted)
            throw std::runtime_error("Scratch temporary not byte-exact 90 turn");
        if(!ReplaceFileW(working.c_str(),temp.c_str(),backup.c_str(),0,
                         nullptr,nullptr)) {
            printf("WIC_REPLACE50_REPLACEFILE_FAILURE turn=%d win32=%lu\n",
                   i,static_cast<unsigned long>(GetLastError()));
            throw std::runtime_error("Scratch ReplaceFileW failed");
        }
        // Verify original previous version still survives in full, plus
        // newest version visible at the SAME stable working filename.
        if(WICPhotosOpaqueRotationProbe::Read(backup.c_str())!=previous ||
           WICPhotosOpaqueRotationProbe::Read(working.c_str())!=wanted)
            throw std::runtime_error("Scratch replacement lost old/new HEIF");
        CComPtr<IWICBitmapDecoder> decoder;
        Must(factory->CreateDecoderFromFilename(working.c_str(),nullptr,
             GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
             "reopen exact same scratch filename");
        CComPtr<IWICBitmapFrameDecode> frame;
        Must(decoder->GetFrame(0,&frame),"reopen scratch main");
        UINT w=0,h=0;
        Must(frame->GetSize(&w,&h),"reopen scratch geometry");
        if(i==1) {originalW=h;originalH=w;}
        if(w!=(i%2?originalH:originalW) ||
           h!=(i%2?originalW:originalH))
            throw std::runtime_error("Same-path reopen returned stale HEIF dimensions");
        if(i%10==0)
            printf("WIC_PHOTOS_REPLACE_SAME_PATH_TURN_%d_BYTE_EXACT_PASS\n",i);
    }
    // Fifty 90-degree turns = initial 180-degree orientation, as a bytewise
    // HEIF transform with unchanged HEVC and all supplemental items retained.
    const auto wantFinal=HeicLosslessNative::Transform(
        HeicLosslessNative::Transform(untouched,1),1);
    if(WICPhotosOpaqueRotationProbe::Read(working.c_str())!=wantFinal)
        throw std::runtime_error("50 same-path snapshots ended in wrong orientation");
    if(WICPhotosOpaqueRotationProbe::Read(fixture)!=untouched)
        throw std::runtime_error("Original public fixture modified");
    printf("WIC_PHOTOS_REPLACE_SAME_WORKING_COPY_FIFTY_PRESERVED_PASS "
           "operations=50 retainedBackups=50\n");
}
} // namespace WICPhotosReplace50Probe
