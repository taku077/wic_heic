#pragma once
// Direct, in-process Windows WIC lossless transformation of a synthetic HEIC.
// This is separate from Windows Photos UI; private user files never enter CI.
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace WICDirectLosslessProbe {
inline void Check(HRESULT hr,const char* label) {
    if(FAILED(hr)) {
        printf("WIC_LOSSLESS_COM_FAILURE %s 0x%08X\n",label,(unsigned)hr);
        throw std::runtime_error(label);
    }
}
inline std::vector<uint8_t> Read(const wchar_t* file) {
    std::ifstream s(file,std::ios::binary);
    if(!s)throw std::runtime_error("WIC lossless file missing");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(s),std::istreambuf_iterator<char>());
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,(void**)&factory),"create factory");
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(L"samples\\fixture-irot3-exif6.heic",
        nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),"create source decoder");
    CComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,&source),"source frame");

    CComPtr<IWICBitmapEncoder> enc;
    Check(factory->CreateEncoder(GUID_ContainerFormatHEIC,&GUID_VendorPrsyahmi,&enc),"create encoder");
    CComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream),"create output stream");
    Check(stream->InitializeFromFilename(L"samples\\fixture-wic-direct-lossless.heic",
        GENERIC_WRITE),"output filename");
    Check(enc->Initialize(stream,WICBitmapEncoderNoCache),"initialize encoder");
    CComPtr<IWICBitmapFrameEncode> frame;
    CComPtr<IPropertyBag2> options;
    Check(enc->CreateNewFrame(&frame,&options),"create options frame");
    if(!options)throw std::runtime_error("Frame IPropertyBag2 unavailable");

    PROPBAG2 keys[2]{};
    keys[0].pstrName=const_cast<LPOLESTR>(L"BitmapTransform");
    keys[1].pstrName=const_cast<LPOLESTR>(L"HeifCompressionMethod");
    VARIANT values[2]{};
    values[0].vt=VT_UI1;values[0].bVal=1; // clockwise 90
    values[1].vt=VT_UI1;values[1].bVal=1; // never re-encode
    Check(options->Write(2,keys,values),"request lossless rotation");
    Check(frame->Initialize(options),"initialize lossless frame");
    Check(frame->SetSize(1920,1080),"set expected rotated size");
    Check(frame->WriteSource(source,nullptr),"lossless WriteSource");
    Check(frame->Commit(),"lossless frame commit");
    Check(enc->Commit(),"lossless encoder commit");
    frame.Release();enc.Release();stream.Release();options.Release();

    const auto expected=Read(L"samples\\fixture-lossless-cw90.heic");
    const auto actual=Read(L"samples\\fixture-wic-direct-lossless.heic");
    printf("WIC_LOSSLESS_EXPECTED_BYTES=%zu ACTUAL_BYTES=%zu\n",
        expected.size(),actual.size());
    if(actual!=expected)
        throw std::runtime_error("WIC output differs from metadata-only expected HEIC bytes");

    CComPtr<IWICBitmapDecoder> verify;
    Check(factory->CreateDecoderFromFilename(L"samples\\fixture-wic-direct-lossless.heic",
        nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&verify),"verify saved HEIC");
    CComPtr<IWICBitmapFrameDecode> verifyFrame;
    Check(verify->GetFrame(0,&verifyFrame),"verify frame");
    UINT w=0,h=0;
    Check(verifyFrame->GetSize(&w,&h),"verify geometry");
    if(w!=1920||h!=1080)throw std::runtime_error("Lossless rotation geometry failed");
    printf("WIC_NATIVE_DIRECT_LOSSLESS_HEIC_PIXEL_PRESERVATION_PASSED\n");
}
}
