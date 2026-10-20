#pragma once
// Verify actual WIC-rendered pixels after applying the lossless irot metadata.
// Uses only small checked-in synthetic HEIC fixture and ephemeral generated copy.
// No original images are opened for writing.
#include <vector>
#include <cstring>
#include <stdexcept>
#include <cstdio>

namespace WICLosslessRotationProbe {
inline void Must(HRESULT hr,const char* where) {
    if(FAILED(hr)) {
        printf("LOSSLESS_WIC_FAILURE=%s HRESULT=0x%08X\n",where,(unsigned)hr);
        throw std::runtime_error(where);
    }
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Must(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,(void**)&factory),"WIC factory");

    CComPtr<IWICBitmapDecoder> sourceDecoder,outputDecoder;
    Must(factory->CreateDecoderFromFilename(L"samples\\fixture-irot3-exif6-noaux.heic",
        nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&sourceDecoder),
        "load synthetic source");
    Must(factory->CreateDecoderFromFilename(L"samples\\fixture-lossless-cw90.heic",
        nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&outputDecoder),
        "load synthetic lossless copy");
    CComPtr<IWICBitmapFrameDecode> source,output;
    Must(sourceDecoder->GetFrame(0,&source),"original frame");
    Must(outputDecoder->GetFrame(0,&output),"lossless frame");
    UINT ow=0,oh=0,w=0,h=0;
    Must(source->GetSize(&ow,&oh),"source dimensions");
    Must(output->GetSize(&w,&h),"output dimensions");
    printf("LOSSLESS_WIC_SOURCE_SIZE=%ux%u OUTPUT_SIZE=%ux%u\n",ow,oh,w,h);
    if(ow!=1080 || oh!=1920 || w!=1920 || h!=1080)
        throw std::runtime_error("Lossless synthetic HEIC orientation incorrect");

    CComPtr<IWICBitmapFlipRotator> expected;
    Must(factory->CreateBitmapFlipRotator(&expected),"create WIC rotator");
    Must(expected->Initialize(source,WICBitmapTransformRotate90),
        "WIC rotate source 90 clockwise");
    CComPtr<IWICBitmapSource> expectedRGB,outputRGB;
    Must(WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,expected,&expectedRGB),
        "convert reference RGB");
    Must(WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,output,&outputRGB),
        "convert lossless output RGB");

    const UINT stride=w*3;
    std::vector<BYTE> reference(size_t(stride)*h),actual(size_t(stride)*h);
    Must(expectedRGB->CopyPixels(nullptr,stride,
        static_cast<UINT>(reference.size()),reference.data()),"CopyPixels reference");
    Must(outputRGB->CopyPixels(nullptr,stride,
        static_cast<UINT>(actual.size()),actual.data()),"CopyPixels lossless");
    size_t changed=0;int maxError=0;
    for(size_t i=0;i<reference.size();++i) {
        int delta=std::abs(int(reference[i])-int(actual[i]));
        if(delta) ++changed;
        if(delta>maxError)maxError=delta;
    }
    printf("LOSSLESS_WIC_PIXEL_DIFF_COMPONENTS=%zu MAX_ERROR=%d\n",
        changed,maxError);
    if(changed!=0 || maxError!=0)
        throw std::runtime_error("Lossless HEIC pixels differ from exact WIC CW90 transform");

    CComPtr<IWICMetadataQueryReader> meta;
    Must(output->GetMetadataQueryReader(&meta),"output metadata");
    PROPVARIANT orientation;
    PropVariantInit(&orientation);
    HRESULT hr=meta->GetMetadataByName(L"/app1/ifd/{ushort=274}",&orientation);
    if(FAILED(hr)) {
        printf("LOSSLESS_WIC_METADATA_QUERY_HRESULT=0x%08X\n",(unsigned)hr);
        throw std::runtime_error("Output orientation query missing");
    }
    UINT orientationValue=orientation.vt==VT_UI2?orientation.uiVal:
        (orientation.vt==VT_UI4?orientation.ulVal:9999);
    PropVariantClear(&orientation);
    printf("LOSSLESS_WIC_EXIF_ORIENTATION=%u\n",orientationValue);
    if(orientationValue!=1)
        throw std::runtime_error("EXIF orientation double transform not fixed");
    printf("LOSSLESS_HEIC_WIC_PIXEL_EQUIVALENCE_PASSED\n");
}
}
