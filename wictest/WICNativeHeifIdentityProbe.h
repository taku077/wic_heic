#pragma once
// Experimental native-Windows HEIF WIC identity test. Only synthetic images.
// This test MUST fail on the legacy private-container-GUID branch, as intended.
namespace WICNativeHeifIdentityProbe {
inline void Check(HRESULT hr,const char* msg) {
    if(FAILED(hr)) {
        printf("WIC_NATIVE_HEIF_ID_ERROR %s hr=0x%08X\n",msg,(unsigned)hr);
        throw std::runtime_error(msg);
    }
}
inline void Run() {
    if(!IsEqualGUID(GUID_ContainerFormatHEIC,GUID_ContainerFormatHeif))
        throw std::runtime_error("Project HEIC GUID is not native Windows HEIF");
    CComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory,(void**)&factory),"Create factory");
    // Explicit-vendor queries avoid claiming Windows Photos will choose our
    // codec without it. They verify that Windows-native HEIF calls can reach
    // this WIC implementation, unlike with the legacy private format GUID.
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoder(GUID_ContainerFormatHeif,
        &GUID_VendorPrsyahmi,&decoder),"Resolve vendor under native HEIF");
    GUID returned{};
    Check(decoder->GetContainerFormat(&returned),"Native decoder identity");
    if(!IsEqualGUID(returned,GUID_ContainerFormatHeif))
        throw std::runtime_error("Decoder exposed legacy private HEIC identity");
    CComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatHeif,
        &GUID_VendorPrsyahmi,&encoder),"Resolve vendor native HEIF encoder");
    Check(encoder->GetContainerFormat(&returned),"Native encoder identity");
    if(!IsEqualGUID(returned,GUID_ContainerFormatHeif))
        throw std::runtime_error("Encoder exposed legacy private HEIC identity");
    // A Photos caller normally does not specify our vendor GUID. The
    // explicit-vendor tests above cannot establish default WIC arbitration.
    // Validate decoder + encoder discovery WITHOUT a vendor on Windows CI.
    CComPtr<IWICBitmapEncoder> autoEncoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatHeif,
        nullptr,&autoEncoder),"Resolve default native HEIF encoder");
    CComPtr<IWICBitmapEncoderInfo> autoEncoderInfo;
    Check(autoEncoder->GetEncoderInfo(&autoEncoderInfo),
        "Default HEIF encoder information");
    CLSID encoderClsid{};
    Check(autoEncoderInfo->GetCLSID(&encoderClsid),
        "Default HEIF encoder CLSID");
    if(!IsEqualGUID(encoderClsid,CLSID_HEICEncoder))
        throw std::runtime_error("Default HEIF encoder does not select our WIC encoder");
    CComPtr<IWICBitmapDecoder> autoDecoder;
    Check(factory->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&autoDecoder),
        "Resolve default HEIC from filename");
    CComPtr<IWICBitmapDecoderInfo> autoDecoderInfo;
    Check(autoDecoder->GetDecoderInfo(&autoDecoderInfo),
        "Default HEIC decoder information");
    CLSID decoderClsid{};
    Check(autoDecoderInfo->GetCLSID(&decoderClsid),
        "Default HEIC decoder CLSID");
    printf("WIC_NATIVE_HEIF_DEFAULT_SELECTS_OWN_DECODER=%u\n",
        (unsigned)IsEqualGUID(decoderClsid,CLSID_HEICDecoder));
    if(!IsEqualGUID(decoderClsid,CLSID_HEICDecoder))
        throw std::runtime_error("Default HEIC decoder is not our verified full-size codec");
    puts("WIC_NATIVE_HEIF_NO_VENDOR_AUTODISCOVERY_PASS");
    puts("WIC_NATIVE_HEIF_GUID_VENDOR_DECODER_ENCODER_PASS");
}
}
