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

inline void SingleRotation(IWICImagingFactory* factory,
                           BYTE transform,
                           const wchar_t* path,
                           const wchar_t* expectedPath) {
    // Only synthetic public samples are used. Each output is a unique file.
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",
        nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
        "open synthetic original");
    CComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,&source),"original frame");

    CComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,&encoder),"create encoder");
    CComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream),"create output stream");
    Check(stream->InitializeFromFilename(path,GENERIC_WRITE),
        "open synthetic output");
    Check(encoder->Initialize(stream,WICBitmapEncoderNoCache),"initialize HEIC encoder");
    CComPtr<IWICBitmapFrameEncode> frame;
    CComPtr<IPropertyBag2> options;
    Check(encoder->CreateNewFrame(&frame,&options),"obtain WIC options");
    if(!options)throw std::runtime_error("HEIC options missing");
    PROPBAG2 keys[2]{};
    keys[0].pstrName=const_cast<LPOLESTR>(L"BitmapTransform");
    keys[1].pstrName=const_cast<LPOLESTR>(L"HeifCompressionMethod");
    VARIANT values[2]{};
    values[0].vt=VT_UI1;values[0].bVal=transform;
    values[1].vt=VT_UI1;values[1].bVal=1; // WICHeifCompressionNone
    Check(options->Write(2,keys,values),"specify lossless transform");
    Check(frame->Initialize(options),"initialize source-preserving encoder");
    UINT expectedW=(transform&1)?1920:1080;
    UINT expectedH=(transform&1)?1080:1920;
    Check(frame->SetSize(expectedW,expectedH),"set rotated size");
    Check(frame->WriteSource(source,nullptr),"preserve original HEVC source");
    Check(frame->Commit(),"frame.Commit");
    Check(encoder->Commit(),"encoder.Commit");
    frame.Release();encoder.Release();stream.Release();options.Release();

    const auto wanted=Read(expectedPath),produced=Read(path);
    printf("WIC_LOSSLESS_%u expected_bytes=%zu actual_bytes=%zu\n",
        (unsigned)transform,wanted.size(),produced.size());
    if(wanted!=produced)throw std::runtime_error(
        "WIC lossless output differs byte-for-byte from Python baseline");
    CComPtr<IWICBitmapDecoder> outputDecoder;
    Check(factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&outputDecoder),"reopen HEIC");
    CComPtr<IWICBitmapFrameDecode> result;
    Check(outputDecoder->GetFrame(0,&result),"get saved HEIC frame");
    UINT w=0,h=0;
    Check(result->GetSize(&w,&h),"get saved HEIC dimensions");
    if(w!=expectedW||h!=expectedH)throw std::runtime_error(
        "HEIC rotated dimensions incorrect");
}
inline void RejectAuxiliaryImage(IWICImagingFactory* factory) {
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6.heic",
        nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
        "open linked-auxiliary fixture");
    CComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,&source),"linked-image source");
    CComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,&encoder),"linked-image encoder");
    CComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream),"linked-image stream");
    Check(stream->InitializeFromFilename(
        L"samples\\fixture-wic-linked-rejected.heic",GENERIC_WRITE),
        "linked-image output");
    Check(encoder->Initialize(stream,WICBitmapEncoderNoCache),"init linked encoder");
    CComPtr<IWICBitmapFrameEncode> frame;
    CComPtr<IPropertyBag2> options;
    Check(encoder->CreateNewFrame(&frame,&options),"linked-image options");
    PROPBAG2 prop{};
    prop.pstrName=const_cast<LPOLESTR>(L"BitmapTransform");
    VARIANT val{};
    val.vt=VT_UI1;val.bVal=1;
    Check(options->Write(1,&prop,&val),"link transform options");
    Check(frame->Initialize(options),"init linked frame");
    Check(frame->SetSize(1920,1080),"linked geometry");
    HRESULT hr=frame->WriteSource(source,nullptr);
    printf("WIC_LINKED_AUXILIARY_ROTATION_REJECTION=0x%08X\n",(unsigned)hr);
    if(hr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("Unsafe linked auxiliary image was rotated");
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,(void**)&factory),
        "WIC factory");
    const auto original=Read(L"samples\\fixture-irot3-exif6-noaux.heic");
    SingleRotation(factory,1,L"samples\\fixture-wic-direct-lossless.heic",
        L"samples\\fixture-lossless-cw90.heic");
    SingleRotation(factory,2,L"samples\\fixture-wic-direct-lossless-180.heic",
        L"samples\\fixture-lossless-cw180.heic");
    SingleRotation(factory,3,L"samples\\fixture-wic-direct-lossless-270.heic",
        L"samples\\fixture-lossless-cw270.heic");
    if(Read(L"samples\\fixture-irot3-exif6-noaux.heic")!=original)
        throw std::runtime_error("Original synthetic HEIC was modified");
    RejectAuxiliaryImage(factory);
    printf("WIC_NATIVE_LOSSLESS_90_180_270_EXACT_BYTES_AND_SAFETY_PASSED\n");
}
}
