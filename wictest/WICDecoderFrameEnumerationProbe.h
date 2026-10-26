#pragma once
// Read-only regression for cached HEIF top-level IDs and seek position.
// Repeated frame enumeration must not lose the secondary item, reinterpret
// irot, or change RGB while synthetic Photos operations request GetFrame.
#include <stdexcept>
#include <vector>
#include <cstdio>
#include <cstring>
namespace WICDecoderFrameEnumerationProbe {
inline void Require(HRESULT hr,const char* what) {
    if(FAILED(hr)){
        printf("WIC_FRAME_ENUM_FAILURE phase=%s hr=0x%08X\n",
               what,(unsigned)hr);
        throw std::runtime_error(what);
    }
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"factory");
    CComPtr<IWICBitmapDecoder> decoder;
    Require(factory->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",nullptr,
        GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
        "open synthetic HEIC");
    UINT count=0,auxW=0,auxH=0;
    Require(decoder->GetFrameCount(&count),"top-level count");
    if(count!=2)throw std::runtime_error("Lost secondary HEIF item");
    {
        CComPtr<IWICBitmapFrameDecode> auxiliary;
        Require(decoder->GetFrame(1,&auxiliary),"initial aux frame");
        Require(auxiliary->GetSize(&auxW,&auxH),"initial aux size");
        if(!auxW||!auxH)throw std::runtime_error("Zero-sized aux");
    }
    std::vector<BYTE> reference;
    for(int k=0;k<300;k++){
        UINT n=0;
        Require(decoder->GetFrameCount(&n),"repeated count");
        if(n!=2)throw std::runtime_error("Top-level count changed");
        for(UINT index=0;index<2;index++){
            CComPtr<IWICBitmapFrameDecode> frame;
            Require(decoder->GetFrame(index,&frame),"repeated frame");
            UINT w=0,h=0;
            Require(frame->GetSize(&w,&h),"repeated dimensions");
            if(index==0) {
                if(w!=1080||h!=1920)
                    throw std::runtime_error("Primary irot geometry changed");
                if(k==0||k==149||k==299){
                    CComPtr<IWICBitmapSource> rgb;
                    Require(WICConvertBitmapSource(
                        GUID_WICPixelFormat24bppRGB,frame,&rgb),
                        "convert primary to RGB");
                    const size_t bytes=size_t(w)*h*3;
                    std::vector<BYTE> actual(bytes);
                    Require(rgb->CopyPixels(nullptr,w*3,UINT(bytes),
                            actual.data()),"read RGB after enumeration");
                    if(reference.empty())reference=std::move(actual);
                    else if(actual!=reference)
                        throw std::runtime_error("Repeated frame RGB changed");
                }
            } else if(w!=auxW||h!=auxH) {
                throw std::runtime_error("Secondary dimensions changed");
            }
        }
    }
    printf("WIC_HEIF_IMMUTABLE_ENUMERATION_300_PASS frames=2 "
           "primary=1080x1920 aux_stable=1 rgb_equal=1\n");
}
} // namespace WICDecoderFrameEnumerationProbe
