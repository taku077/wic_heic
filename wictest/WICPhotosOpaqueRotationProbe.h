#pragma once
// Windows CI reproduction of the OBSERVED default-option Photos HEIC save:
// WIC original -> rotated bitmap -> opaque WIC raster -> HEIC encoder.
// Uses only checked-in public artificial HEIC, never user photographs.
#include <fstream>
#include <iterator>
#include <vector>
#include <stdexcept>
#include <string>

namespace WICPhotosOpaqueRotationProbe {
inline void Must(HRESULT hr,const char* label) {
    if(FAILED(hr)){
        printf("WIC_PHOTOS_OPAQUE_FAILURE=%s HRESULT=0x%08X\n",
               label,static_cast<unsigned>(hr));
        throw std::runtime_error(label);
    }
}
inline std::vector<uint8_t> Read(const wchar_t* path) {
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("Artificial fixture/output absent");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}
inline void RunOne(IWICImagingFactory* factory,bool editOnePixel,BYTE rotation=1, bool halfTurnFixture=false,
                   bool preloadDifferentHEIC=false) {
    const wchar_t* outputPath=halfTurnFixture?
        L"samples\\out-photos-opaque-halfturn-exif3-cw90.heic":editOnePixel?
        rotation==0?L"samples\\out-photos-opaque-edited-cw0.heic":
                    L"samples\\out-photos-opaque-edited-cw90.heic":
        rotation==0 ? L"samples\\out-photos-opaque-pure-cw0.heic":
        rotation==1 ? L"samples\\out-photos-opaque-pure-cw90.heic":
        rotation==2 ? L"samples\\out-photos-opaque-pure-cw180.heic":
                      L"samples\\out-photos-opaque-pure-cw270.heic";
    CComPtr<IWICBitmapDecoder> decoder;
    Must(factory->CreateDecoderFromFilename(
        halfTurnFixture?L"samples\\fixture-irot2-exif3-noaux.heic":
        L"samples\\fixture-irot3-exif6-noaux.heic",
        nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
        "decode original HEIC");
    CComPtr<IWICBitmapFrameDecode> source;
    Must(decoder->GetFrame(0,&source),"get original frame");
    UINT inW=0,inH=0;
    Must(source->GetSize(&inW,&inH),"get original geometry");
    if(preloadDifferentHEIC) {
        // Windows Photos can prefetch an unrelated neighboring HEIC from a
        // folder. Use a distinct checked-in public sample, not another
        // rotation of the same synthetic HEVC image. The active HEIC must remain independently matchable.
        CComPtr<IWICBitmapDecoder> neighborDecoder;
        Must(factory->CreateDecoderFromFilename(
            L"samples\\04.heic",nullptr,GENERIC_READ,
            WICDecodeMetadataCacheOnDemand,&neighborDecoder),
            "decode synthetic preloaded neighbor");
        CComPtr<IWICBitmapFrameDecode> neighbor;
        Must(neighborDecoder->GetFrame(0,&neighbor),
             "register second source in Photos-style preload");
    }
    CComPtr<IWICBitmapFlipRotator> rotator;
    Must(factory->CreateBitmapFlipRotator(&rotator),"create 90 degree rotator");
    const WICBitmapTransformOptions transform=rotation==0?
        WICBitmapTransformRotate0:rotation==1?
        WICBitmapTransformRotate90:(rotation==2?
        WICBitmapTransformRotate180:WICBitmapTransformRotate270);
    Must(rotator->Initialize(source,transform),
         "render 90/180/270 synthetic WIC pixels");
    CComPtr<IWICBitmap> opaque;
    Must(factory->CreateBitmapFromSource(rotator,WICBitmapCacheOnLoad,&opaque),
         "cache opaque source bitmap");
    if(editOnePixel){
        WICRect area{0,0,1,1};
        CComPtr<IWICBitmapLock> lock;
        Must(opaque->Lock(&area,WICBitmapLockWrite,&lock),
             "modify exactly one synthetic test pixel");
        UINT size=0;BYTE* pixels=nullptr;
        Must(lock->GetDataPointer(&size,&pixels),"lock synthetic pixel data");
        if(size<3 || !pixels)throw std::runtime_error("Invalid synthetic lock");
        pixels[0]^=0x01; // this is not a pure 90 degree rotation anymore
        lock.Release();
    }
    UINT ow=0,oh=0;
    Must(opaque->GetSize(&ow,&oh),"opaque raster geometry");
    const UINT wantW=(rotation&1)?inH:inW;
    const UINT wantH=(rotation&1)?inW:inH;
    if(ow!=wantW || oh!=wantH)
        throw std::runtime_error("Synthetic rotation geometry incorrect");
    CComPtr<IWICBitmapEncoder> encoder;
    Must(factory->CreateEncoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,&encoder),"create tested HEIC encoder");
    CComPtr<IWICStream> stream;
    Must(factory->CreateStream(&stream),"new output stream");
    Must(stream->InitializeFromFilename(outputPath,GENERIC_WRITE),
         "write NEW public-synthetic output file");
    Must(encoder->Initialize(stream,WICBitmapEncoderNoCache),
         "initialize HEIC encoder");
    CComPtr<IWICBitmapFrameEncode> dest;
    Must(encoder->CreateNewFrame(&dest,nullptr),"Photos-style no-option frame");
    Must(dest->Initialize(nullptr),"Photos-style no-option frame initialize");
    Must(dest->SetSize(ow,oh),"set rotated destination geometry");
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppRGB;
    Must(dest->SetPixelFormat(&format),"set 24-bit synthetic raster format");
    if(!IsEqualGUID(format,GUID_WICPixelFormat24bppRGB))
        throw std::runtime_error("Unexpected destination format");
    Must(dest->WriteSource(opaque,nullptr),"write WIC opaque RGB pixels");
    Must(dest->Commit(),"commit WIC HEIC frame");
    Must(encoder->Commit(),"commit WIC HEIC file");
    dest.Release();encoder.Release();stream.Release();
    const auto bytes=Read(outputPath);
    const auto expected=Read(halfTurnFixture?
        L"samples\\fixture-lossless-cw180.heic":rotation==0?
        L"samples\\fixture-irot3-exif6-noaux.heic":rotation==1?
        L"samples\\fixture-lossless-cw90.heic":rotation==2?
        L"samples\\fixture-lossless-cw180.heic":
        L"samples\\fixture-lossless-cw270.heic");
    if(!editOnePixel){
        if(bytes!=expected)
            throw std::runtime_error(
                "Default Photos-style opaque CW90 missed bit-identical lossless route");
        printf("WIC_PHOTOS_OPAQUE_%sCW%u_LOSSLESS_EQUAL_PASS bytes=%zu\n",
               halfTurnFixture?"IROT2_EXIF3_":"",unsigned(rotation*90),bytes.size());
    } else {
        if(bytes==expected)
            throw std::runtime_error(
                "A deliberately edited raster was wrongly treated as lossless");
        printf("WIC_PHOTOS_OPAQUE_ONE_PIXEL_EDIT_SAFE_REENCODE_PASS\n");
    }

}
inline void VerifyOutput(IWICImagingFactory* factory,
                         const wchar_t* path,UINT expectedW,UINT expectedH){
    // Reopen after matching tests, so no new source is offered mid-sequence.
    CComPtr<IWICBitmapDecoder> outDecoder;
    Must(factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,
         WICDecodeMetadataCacheOnDemand,&outDecoder),
         "reopen newly written synthetic HEIC");
    CComPtr<IWICBitmapFrameDecode> saved;
    Must(outDecoder->GetFrame(0,&saved),"reopen synthetic HEIC primary frame");
    UINT sw=0,sh=0;
    Must(saved->GetSize(&sw,&sh),"validate final synthetic HEIC geometry");
    if(sw!=expectedW || sh!=expectedH)
        throw std::runtime_error("Written HEIC dimensions are incorrect");
}
// Reproduce the real Windows 10 Photos 1.1.26 log using the untouched
// checked-in HEIC whose hidden/unlinked second item is exposed by libheif
// as a second top-level WIC frame. No real user photos, no file overwrite.
inline void RunTwoFramePhotosSave(IWICImagingFactory* factory,bool editPrimary=false,bool unchangedPrimary=false) {
    const wchar_t* input=L"samples\\fixture-irot3-exif6-noaux.heic";
    const wchar_t* output=editPrimary?
        L"samples\\out-photos-two-frame-one-pixel-edit.heic":unchangedPrimary?
        L"samples\\out-photos-two-frame-unchanged.heic":
        L"samples\\out-photos-two-frame-cw90.heic";
    CComPtr<IWICBitmapDecoder> decoder;
    Must(factory->CreateDecoderFromFilename(input,nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"two-frame source open");
    UINT count=0;
    Must(decoder->GetFrameCount(&count),"two-frame count");
    if(count!=2) throw std::runtime_error("Two-frame regression fixture no longer has exactly 2 WIC frames");
    CComPtr<IWICBitmapFrameDecode> primary,second;
    Must(decoder->GetFrame(0,&primary),"two-frame primary");
    Must(decoder->GetFrame(1,&second),"two-frame secondary");
    UINT w=0,h=0,sw=0,sh=0;
    Must(primary->GetSize(&w,&h),"two-frame primary geometry");
    Must(second->GetSize(&sw,&sh),"two-frame secondary geometry");
    CComPtr<IWICBitmapFlipRotator> rotator;
    Must(factory->CreateBitmapFlipRotator(&rotator),"two-frame rotate");
    Must(rotator->Initialize(primary,WICBitmapTransformRotate90),"two-frame rotate initialize");
    CComPtr<IWICBitmap> opaque;
    Must(factory->CreateBitmapFromSource(rotator,WICBitmapCacheOnLoad,&opaque),"two-frame opaque");
    if(editPrimary){
        WICRect rect{0,0,1,1};
        CComPtr<IWICBitmapLock> lock;
        Must(opaque->Lock(&rect,WICBitmapLockWrite,&lock),"two-frame edit lock");
        UINT len=0;BYTE* px=nullptr;
        Must(lock->GetDataPointer(&len,&px),"two-frame edited data pointer");
        if(len<3 || !px)throw std::runtime_error("two-frame edit no pixels");
        px[0]^=1;
    }
    CComPtr<IWICBitmapEncoder> encoder;
    Must(factory->CreateEncoder(GUID_ContainerFormatHEIC,&GUID_VendorPrsyahmi,&encoder),
        "two-frame HEIC encoder");
    CComPtr<IWICStream> stream;
    Must(factory->CreateStream(&stream),"two-frame create stream");
    Must(stream->InitializeFromFilename(output,GENERIC_WRITE),"two-frame new output stream");
    Must(encoder->Initialize(stream,WICBitmapEncoderNoCache),"two-frame encoder initialize");
    for(int i=0;i<2;++i){
        CComPtr<IWICBitmapFrameEncode> dest;
        Must(encoder->CreateNewFrame(&dest,nullptr),"two-frame new frame");
        Must(dest->Initialize(nullptr),"two-frame initialize frame");
        Must(dest->SetSize(i==0?(unchangedPrimary?w:h):sw,
                           i==0?(unchangedPrimary?h:w):sh),"two-frame set size");
        WICPixelFormatGUID fmt=GUID_WICPixelFormat24bppRGB;
        Must(dest->SetPixelFormat(&fmt),"two-frame set pixel format");
        if(!IsEqualGUID(fmt,GUID_WICPixelFormat24bppRGB))
            throw std::runtime_error("two-frame pixel format changed");
        Must(dest->WriteSource(i==0?(unchangedPrimary?
                                    static_cast<IWICBitmapSource*>(primary.p):
                                    static_cast<IWICBitmapSource*>(opaque.p)):
                                  static_cast<IWICBitmapSource*>(second.p),nullptr),
             "two-frame WriteSource (original auxiliary must not abort)");
        Must(dest->Commit(),"two-frame frame Commit");
    }
    if(editPrimary){
        const HRESULT hr=encoder->Commit();
        if(SUCCEEDED(hr))throw std::runtime_error("two-frame edited primary was wrongly accepted");
        encoder.Release(); stream.Release();
        if(!Read(output).empty())throw std::runtime_error("two-frame unsafe save wrote partial output");
        printf("WIC_PHOTOS_TWO_FRAME_EDIT_FAIL_CLOSED_PASS hr=0x%08X\\n",(unsigned)hr);
        return;
    }
    Must(encoder->Commit(),"two-frame encoder Commit");
    encoder.Release(); stream.Release();
    const auto saved=Read(output);
    const auto expect=Read(unchangedPrimary?input:L"samples\\fixture-lossless-cw90.heic");
    if(saved!=expect)
        throw std::runtime_error("Two-frame Photos output differs from exact metadata-preserving HEIF rotation");
    printf("WIC_PHOTOS_TWO_FRAME_ORIGINAL_HEIF_BYTE_EXACT_PASS bytes=%zu\n",saved.size());
}

// Four successive Photos-style WIC saves on the very same artificial
// two-frame HEIF. Each pass reads PREVIOUS output from a NEW path, rotates
// primary RGB 90 deg, forwards unchanged second frame, and saves whole HEIF.
// Validate every HEIF against native metadata-only transform and the fourth
// decoded RGB frame against the original. Never overwrite sources.
inline void RunTwoFrameRotationSequence(IWICImagingFactory* factory,int turns=4) {
    if(turns!=4 && turns!=50) throw std::runtime_error("Only fixed four/fifty-turn regression is supported");
    // Stress 50 performs NO sleeps and uses a new, unique directory for
    // every WIC output. Input HEIF remains read-only and cannot be overwritten.
    std::vector<std::wstring> paths(size_t(turns)+1);
    paths[0]=L"samples\\fixture-irot3-exif6-noaux.heic";
    std::wstring root;
    if(turns==50) {
        root=L"samples\\synthetic_stress50_"+std::to_wstring(GetCurrentProcessId())+
            L"_"+std::to_wstring(GetTickCount64());
        if(!CreateDirectoryW(root.c_str(),nullptr))
            throw std::runtime_error("Cannot create a fresh stress-test folder");
    }
    for(int t=1;t<=turns;t++) {
        paths[t]=(turns==50 ? root+L"\\turn-" :
                  std::wstring(L"samples\\out-photos-two-frame-four-turn-"))+
            std::to_wstring(t)+L".heic";
        if(turns==50 && GetFileAttributesW(paths[t].c_str())!=INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error("Unexpected pre-existing stress output");
    }
    UINT initialW=0,initialH=0;
    std::vector<BYTE> initialRGB;
    for(int turn=1;turn<=turns;turn++){
        CComPtr<IWICBitmapDecoder> decoder;
        Must(factory->CreateDecoderFromFilename(paths[turn-1].c_str(),nullptr,
            GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
            "four-turn open previous HEIF");
        UINT count=0;
        Must(decoder->GetFrameCount(&count),"four-turn source frame count");
        if(count!=2)throw std::runtime_error("Four-turn HEIF lost a frame");
        CComPtr<IWICBitmapFrameDecode> first,second;
        Must(decoder->GetFrame(0,&first),"four-turn get primary");
        Must(decoder->GetFrame(1,&second),"four-turn get auxiliary");
        UINT w=0,h=0,aw=0,ah=0;
        Must(first->GetSize(&w,&h),"four-turn primary dimensions");
        Must(second->GetSize(&aw,&ah),"four-turn additional dimensions");
        if(turn==1){
            initialW=w;initialH=h;
            initialRGB.resize(size_t(w)*h*3);
            Must(first->CopyPixels(nullptr,w*3,UINT(initialRGB.size()),
                 initialRGB.data()),"four-turn original pixels");
        }
        if(turns==50) {
            // Distinguish shrinking the VIEW from resizing saved HEIC pixels.
            // CopyPixels on a temporary WIC scaler must not alter source
            // orientation, HEIF data, width or height before a full-size save.
            CComPtr<IWICBitmapScaler> view;
            Must(factory->CreateBitmapScaler(&view),"stress50 viewport scaler");
            const UINT vw=w/2,vh=h/2;
            Must(view->Initialize(first,vw,vh,WICBitmapInterpolationModeFant),
                 "stress50 shrink temporary viewport");
            UINT checkW=0,checkH=0;
            Must(view->GetSize(&checkW,&checkH),"stress50 view dimensions");
            if(checkW!=vw || checkH!=vh)
                throw std::runtime_error("Viewport downsize wrong geometry");
            std::vector<BYTE> displayed(size_t(vw)*vh*3);
            Must(view->CopyPixels(nullptr,vw*3,UINT(displayed.size()),
                displayed.data()),"stress50 viewport-only downscale");
            UINT unchangedW=0,unchangedH=0;
            Must(first->GetSize(&unchangedW,&unchangedH),
                 "stress50 original after viewport downscale");
            if(unchangedW!=w || unchangedH!=h)
                throw std::runtime_error("Viewing zoom altered original geometry");
        }
        CComPtr<IWICBitmapFlipRotator> rotator;
        Must(factory->CreateBitmapFlipRotator(&rotator),"four-turn rotator");
        Must(rotator->Initialize(first,WICBitmapTransformRotate90),
             "four-turn clockwise rotate");
        CComPtr<IWICBitmap> opaque;
        Must(factory->CreateBitmapFromSource(rotator,
             WICBitmapCacheOnLoad,&opaque),"four-turn cache RGB");
        CComPtr<IWICBitmapEncoder> encoder;
        Must(factory->CreateEncoder(GUID_ContainerFormatHEIC,
             &GUID_VendorPrsyahmi,&encoder),"four-turn HEIC encoder");
        CComPtr<IWICStream> stream;
        Must(factory->CreateStream(&stream),"four-turn create stream");
        Must(stream->InitializeFromFilename(paths[turn].c_str(),GENERIC_WRITE),
             "four-turn create NEW output file");
        Must(encoder->Initialize(stream,WICBitmapEncoderNoCache),
             "four-turn initialize HEIC");
        for(int i=0;i<2;i++){
            CComPtr<IWICBitmapFrameEncode> dest;
            Must(encoder->CreateNewFrame(&dest,nullptr),"four-turn create frame");
            Must(dest->Initialize(nullptr),"four-turn frame initialize");
            Must(dest->SetSize(i==0?h:aw,i==0?w:ah),
                 "four-turn frame geometry");
            WICPixelFormatGUID format=GUID_WICPixelFormat24bppRGB;
            Must(dest->SetPixelFormat(&format),"four-turn frame RGB");
            if(!IsEqualGUID(format,GUID_WICPixelFormat24bppRGB))
                throw std::runtime_error("Four-turn pixel format changed");
            Must(dest->WriteSource(i==0?static_cast<IWICBitmapSource*>(opaque.p):
                                       static_cast<IWICBitmapSource*>(second.p),
                                   nullptr),"four-turn WriteSource");
            Must(dest->Commit(),"four-turn frame Commit");
        }
        Must(encoder->Commit(),"four-turn HEIF Commit");
        encoder.Release();stream.Release();

        const auto previous=Read(paths[turn-1].c_str());
        const auto saved=Read(paths[turn].c_str());
        const auto expected=HeicLosslessNative::Transform(previous,1);
        if(saved!=expected || saved.size()!=previous.size())
            throw std::runtime_error("Four-turn saved HEIF was recompressed or metadata lost");
        CComPtr<IWICBitmapDecoder> readback;
        Must(factory->CreateDecoderFromFilename(paths[turn].c_str(),nullptr,
             GENERIC_READ,WICDecodeMetadataCacheOnDemand,&readback),
             "four-turn reopen saved");
        CComPtr<IWICBitmapFrameDecode> verified;
        Must(readback->GetFrame(0,&verified),"four-turn saved primary");
        UINT rw=0,rh=0;
        Must(verified->GetSize(&rw,&rh),"four-turn saved geometry");
        if(rw!=h || rh!=w)
            throw std::runtime_error("Four-turn saved geometry did not swap");
        if(turn%4==0){
            if(rw!=initialW || rh!=initialH)
                throw std::runtime_error("Four turns did not restore original dimensions");
            std::vector<BYTE> finalRGB(size_t(rw)*rh*3);
            Must(verified->CopyPixels(nullptr,rw*3,UINT(finalRGB.size()),
                 finalRGB.data()),"four-turn final pixels");
            if(finalRGB!=initialRGB)
                throw std::runtime_error("Four turns did not restore original RGB pixels");
        }
        if(turns==4 || turn==1 || turn%10==0)
            printf("WIC_PHOTOS_TWO_FRAME_SEQUENCE_TURN_%d_BYTE_EXACT_PASS bytes=%zu\n",
               turn,saved.size());
    }
    if(turns==50) {
        // 50 clockwise quarter-turns = a 180-degree orientation.
        // Check decoded RGB, not just metadata or file byte counts.
        CComPtr<IWICBitmapDecoder> endDecoder;
        Must(factory->CreateDecoderFromFilename(paths[50].c_str(),nullptr,
            GENERIC_READ,WICDecodeMetadataCacheOnDemand,&endDecoder),
            "fifty-turn final reopen");
        CComPtr<IWICBitmapFrameDecode> endFrame;
        Must(endDecoder->GetFrame(0,&endFrame),"fifty-turn final frame");
        UINT ew=0,eh=0;
        Must(endFrame->GetSize(&ew,&eh),"fifty-turn final geometry");
        if(ew!=initialW || eh!=initialH)
            throw std::runtime_error("Fifty turns stretched source dimensions");
        std::vector<BYTE> rgb(size_t(ew)*eh*3);
        Must(endFrame->CopyPixels(nullptr,ew*3,UINT(rgb.size()),rgb.data()),
            "fifty-turn final RGB");
        for(UINT y=0;y<eh;y++)for(UINT x=0;x<ew;x++) {
            const size_t out=(size_t(y)*ew+x)*3;
            const size_t src=(size_t(eh-1-y)*ew+(ew-1-x))*3;
            if(std::memcmp(rgb.data()+out,initialRGB.data()+src,3)!=0)
                throw std::runtime_error("Fifty turns returned incorrect 180 degree pixels");
        }
        printf("WIC_PHOTOS_TWO_FRAME_FIFTY_RAPID_TURNS_EXACT_180_RGB_PASS total=50\n");
    } else {
        printf("WIC_PHOTOS_TWO_FRAME_FOUR_TURNS_ORIGINAL_RGB_RESTORED_PASS\n");
    }
}

inline void Run(){
    CComPtr<IWICImagingFactory> factory;
    Must(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");
    // Verify first, before a competing source enters the short-lived cache.
    RunOne(factory,false,1,true,true);
    RunOne(factory,false,0);
    RunOne(factory,false,1);
    RunOne(factory,false,2);
    RunOne(factory,false,3);
    RunOne(factory,true,0);
    RunOne(factory,true,1);
    VerifyOutput(factory,L"samples\\out-photos-opaque-halfturn-exif3-cw90.heic",1080,1920);
    VerifyOutput(factory,L"samples\\out-photos-opaque-pure-cw0.heic",1080,1920);
    VerifyOutput(factory,L"samples\\out-photos-opaque-pure-cw90.heic",1920,1080);
    VerifyOutput(factory,L"samples\\out-photos-opaque-pure-cw180.heic",1080,1920);
    VerifyOutput(factory,L"samples\\out-photos-opaque-pure-cw270.heic",1920,1080);
    VerifyOutput(factory,L"samples\\out-photos-opaque-edited-cw0.heic",1080,1920);
    VerifyOutput(factory,L"samples\\out-photos-opaque-edited-cw90.heic",1920,1080);
    RunTwoFramePhotosSave(factory);
    RunTwoFramePhotosSave(factory,true);
    RunTwoFramePhotosSave(factory,false,true);
    RunTwoFrameRotationSequence(factory);
}
} // namespace WICPhotosOpaqueRotationProbe
