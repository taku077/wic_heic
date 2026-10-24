#pragma once
// WIC HEIC primary frame direct rotations. Only checked-in artificial HEIC;
// no real photos, registry edits, additional output files or MSI installation.
#include <vector>
#include <stdexcept>
namespace WICSourceTransformProbe {
inline void Require(HRESULT hr,const char* stage) {
    if(FAILED(hr)) {
        printf("WIC_SOURCE_TRANSFORM_ERROR %s hr=0x%08X\n",stage,unsigned(hr));
        throw std::runtime_error(stage);
    }
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory,(void**)&factory),"factory");
    CComPtr<IWICBitmapDecoder> decoder;
    Require(factory->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"public synthetic HEIC");
    CComPtr<IWICBitmapFrameDecode> frame;
    Require(decoder->GetFrame(0,&frame),"synthetic HEIC frame");
    CComPtr<IWICBitmapSourceTransform> transformed;
    Require(frame->QueryInterface(IID_IWICBitmapSourceTransform,
        reinterpret_cast<void**>(&transformed)),"native frame transform QI");
    CComPtr<IUnknown> frameIdentity,transformIdentity;
    Require(frame->QueryInterface(IID_IUnknown,
        reinterpret_cast<void**>(&frameIdentity)),"frame COM identity");
    Require(transformed->QueryInterface(IID_IUnknown,
        reinterpret_cast<void**>(&transformIdentity)),"transform COM identity");
    if(frameIdentity!=transformIdentity)
        throw std::runtime_error("Native HEIC transform breaks frame IUnknown identity");
    UINT naturalWidth=17,naturalHeight=9;
    Require(transformed->GetClosestSize(&naturalWidth,&naturalHeight),
        "native closest size");
    if(naturalWidth!=1080||naturalHeight!=1920)
        throw std::runtime_error("HEIC transform incorrectly claims resizing");
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;
    Require(transformed->GetClosestPixelFormat(&format),"closest pixel format");
    WICPixelFormatGUID actual{};
    Require(frame->GetPixelFormat(&actual),"frame pixel format");
    if(!IsEqualGUID(format,actual))
        throw std::runtime_error("Native HEIC frame transform falsely claimed color conversion");
    const WICRect roi{11,17,13,9};
    for(const WICBitmapTransformOptions rotation :
        {WICBitmapTransformRotate0,WICBitmapTransformRotate90,
         WICBitmapTransformRotate180,WICBitmapTransformRotate270}) {
        BOOL supported=FALSE;
        Require(transformed->DoesSupportTransform(rotation,&supported),
            "rotation capability");
        if(!supported)throw std::runtime_error("Missing native HEIC rotation capability");
        CComPtr<IWICBitmapClipper> clip;
        Require(factory->CreateBitmapClipper(&clip),"expected WIC clipper");
        Require(clip->Initialize(frame,&roi),"expected crop");
        CComPtr<IWICBitmapFlipRotator> rotator;
        Require(factory->CreateBitmapFlipRotator(&rotator),"expected WIC rotation object");
        Require(rotator->Initialize(clip,rotation),"rotate expected crop");
        UINT outputWidth=0,outputHeight=0;
        Require(rotator->GetSize(&outputWidth,&outputHeight),
            "expected rotated dimensions");
        const UINT bpp=24/8,stride=outputWidth*bpp;
        std::vector<BYTE> reference(size_t(stride)*outputHeight);
        std::vector<BYTE> direct(reference.size(),0xAF);
        Require(rotator->CopyPixels(nullptr,stride,UINT(reference.size()),reference.data()),
            "expected rotator pixels");
        WICPixelFormatGUID requested=actual;
        Require(transformed->CopyPixels(&roi,naturalWidth,naturalHeight,&requested,
                rotation,stride,UINT(direct.size()),direct.data()),
            "native HEIC transform pixels");
        if(reference!=direct)
            throw std::runtime_error("Native HEIC transform pixels differ from WIC rotator");
        printf("WIC_HEIC_SOURCE_TRANSFORM_ROTATION_%u_CROP_PIXEL_EQUAL_PASS\n",
            unsigned(rotation));
    }
    BOOL support=TRUE;
    Require(transformed->DoesSupportTransform(WICBitmapTransformFlipHorizontal,&support),
        "unsupported horizontal flip");
    if(support)throw std::runtime_error("HEIC native transform falsely promises flip support");
    WICPixelFormatGUID originalFormat=actual;
    BYTE empty[24]{};
    const WICRect bad{-1,0,2,2};
    if(transformed->CopyPixels(&bad,naturalWidth,naturalHeight,&originalFormat,
            WICBitmapTransformRotate0,6,12,empty)!=E_INVALIDARG)
        throw std::runtime_error("Invalid HEIC crop incorrectly accepted");
    if(transformed->CopyPixels(&roi,naturalWidth,naturalHeight,&originalFormat,
            WICBitmapTransformFlipHorizontal,13*3,13*9*3,empty)!=
            WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("Unsupported flip unexpectedly accepted");
    const WICRect small{0,0,2,2};
    if(transformed->CopyPixels(&small,naturalWidth,naturalHeight,&originalFormat,
            WICBitmapTransformRotate0,5,12,empty)!=E_INVALIDARG)
        throw std::runtime_error("Undersized transform stride incorrectly accepted");
    printf("WIC_HEIC_SOURCE_TRANSFORM_COM_AND_SAFE_PIXEL_API_PASS\n");
}
}
