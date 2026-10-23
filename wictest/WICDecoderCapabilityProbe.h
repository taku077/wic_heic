#pragma once
// Synthetic-only WIC arbitration regression for Windows Photos full-size
// decoding and browsing. Tests the decoder contract, NOT Photos GUI controls.
namespace WICDecoderCapabilityProbe {
inline void Verify(IWICImagingFactory* factory, const wchar_t* path,
                   bool isHeic, bool checkMetadata) {
    CComPtr<IWICStream> source;
    if(FAILED(factory->CreateStream(&source)) ||
       FAILED(source->InitializeFromFilename(path, GENERIC_READ)))
        throw std::runtime_error("Capability stream open failed");
    CComPtr<IWICBitmapDecoder> decoder;
    if(FAILED(factory->CreateDecoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi, &decoder)))
        throw std::runtime_error("Capability decoder creation failed");
    LARGE_INTEGER offset{};offset.QuadPart=7;
    ULARGE_INTEGER before{},after{};
    if(FAILED(source->Seek(offset, STREAM_SEEK_SET, &before)))
        throw std::runtime_error("Capability seek failed");
    DWORD capabilities=0xFFFFFFFFu;
    HRESULT hr=decoder->QueryCapability(source,&capabilities);
    LARGE_INTEGER zero{};
    if(FAILED(source->Seek(zero,STREAM_SEEK_CUR,&after)) ||
       after.QuadPart!=before.QuadPart)
        throw std::runtime_error("QueryCapability modified stream cursor");
    if(FAILED(hr))throw std::runtime_error("QueryCapability failed");
    if(capabilities & WICBitmapDecoderCapabilitySameEncoder)
        throw std::runtime_error("Third-party HEIC falsely claimed SameEncoder");
    if(isHeic) {
        if(!(capabilities & WICBitmapDecoderCapabilityCanDecodeSomeImages))
            throw std::runtime_error("Valid HEIC not discoverable by Photos WIC");
        if(checkMetadata && !(capabilities & WICBitmapDecoderCapabilityCanEnumerateMetadata))
            throw std::runtime_error("HEIC EXIF enumeration not advertised");
        // Discover the embedded thumbnail through the actual IWIC decoder,
        // not from a hard-coded filename or a stale registry capability.
        if(FAILED(source->Seek(zero,STREAM_SEEK_SET,nullptr)))
            throw std::runtime_error("Rewind before thumbnail check failed");
        CComPtr<IWICBitmapDecoder> live;
        if(FAILED(factory->CreateDecoderFromStream(source,nullptr,
            WICDecodeMetadataCacheOnDemand,&live)))
            throw std::runtime_error("Live HEIC decoder unavailable");
        CComPtr<IWICBitmapSource> thumb;
        CComPtr<IWICBitmapFrameDecode> frame;
        if(FAILED(live->GetFrame(0,&frame)))
            throw std::runtime_error("Live HEIC frame unavailable");
        hr=frame->GetThumbnail(&thumb);
        const bool thumbnailAvailable=SUCCEEDED(hr) && !!thumb;
        if(thumbnailAvailable != !!(capabilities &
             WICBitmapDecoderCapabilityCanDecodeThumbnail))
            throw std::runtime_error("HEIC decoder falsely advertised thumbnail");
    } else if(capabilities != 0) {
        throw std::runtime_error("HEIC codec claimed ability to open PNG");
    }
    printf("WIC_CAPABILITY_STREAM_VERIFIED_CURSOR_RESTORED format=%s mask=0x%X\n",
           isHeic?"HEIC":"PNG",(unsigned)capabilities);
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
        (void**)&factory)))throw std::runtime_error("Capability factory unavailable");
    Verify(factory,L"samples\\fixture-irot3-exif6.heic",true,true);
    Verify(factory,L"samples\\fixture-irot3-exif6-noaux.heic",true,true);
    Verify(factory,L"samples\\01.png",false,false);
    printf("WIC_PHOTOS_DECODER_ARBITRATION_CONTRACT_PASS\n");
}
}
