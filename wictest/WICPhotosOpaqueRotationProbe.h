#pragma once
// Windows CI reproduction of the OBSERVED default-option Photos HEIC save:
// WIC original -> rotated bitmap -> opaque WIC raster -> HEIC encoder.
// Uses only checked-in public artificial HEIC, never user photographs.
#include <fstream>
#include <iterator>
#include <vector>
#include <stdexcept>

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
inline void RunOne(IWICImagingFactory* factory,bool editOnePixel,BYTE rotation=1, bool halfTurnFixture=false) {
    const wchar_t* outputPath=halfTurnFixture?
        L"samples\\out-photos-opaque-halfturn-exif3-cw90.heic":editOnePixel?
        L"samples\\out-photos-opaque-edited-cw90.heic":
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
    CComPtr<IWICBitmapFlipRotator> rotator;
    Must(factory->CreateBitmapFlipRotator(&rotator),"create 90 degree rotator");
    const WICBitmapTransformOptions transform=rotation==1?
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
        L"samples\\fixture-lossless-cw180.heic":rotation==1?
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
inline void Run(){
    CComPtr<IWICImagingFactory> factory;
    Must(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");
    // Verify first, before a competing source enters the short-lived cache.
    RunOne(factory,false,1,true);
    RunOne(factory,false,1);
    RunOne(factory,false,2);
    RunOne(factory,false,3);
    RunOne(factory,true,1);
    VerifyOutput(factory,L"samples\\out-photos-opaque-halfturn-exif3-cw90.heic",1080,1920);
    VerifyOutput(factory,L"samples\\out-photos-opaque-pure-cw90.heic",1920,1080);
    VerifyOutput(factory,L"samples\\out-photos-opaque-pure-cw180.heic",1080,1920);
    VerifyOutput(factory,L"samples\\out-photos-opaque-pure-cw270.heic",1920,1080);
    VerifyOutput(factory,L"samples\\out-photos-opaque-edited-cw90.heic",1920,1080);
}
} // namespace WICPhotosOpaqueRotationProbe
