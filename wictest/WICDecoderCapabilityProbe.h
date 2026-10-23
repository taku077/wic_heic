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
// Additional A/B only for a PUBLIC SYNTHETIC HEIC with an embedded ICC.
// Earlier probes observed ONLY an ICC-absent fixture, so they could not
// distinguish "the API changed in general" from "only zero-ICC differs".
// This performs no save, no register writes, and never reads user photography.
// The historical codec is allowed to return errors; record rather than mask.
inline void SurveySyntheticIccForPhotosEditor(IWICImagingFactory* factory) {
    CComPtr<IWICBitmapDecoder> decoder;
    const HRESULT created=factory->CreateDecoderFromFilename(
        L"samples\\icc-roundtrip.heic",nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder);
    printf("WIC_PHOTOS_ICC_PRESENT_DECODER_HRESULT=0x%08X\n",(unsigned)created);
    if(FAILED(created)||!decoder)
        throw std::runtime_error("Synthetic ICC HEIC cannot be opened");
    UINT containerCount=0;
    const HRESULT containerHr=decoder->GetColorContexts(0,nullptr,&containerCount);
    printf("WIC_PHOTOS_ICC_PRESENT_CONTAINER_HRESULT=0x%08X\n",(unsigned)containerHr);
    printf("WIC_PHOTOS_ICC_PRESENT_CONTAINER_COUNT=%u\n",containerCount);
    CComPtr<IWICBitmapFrameDecode> frame;
    const HRESULT frameHr=decoder->GetFrame(0,&frame);
    if(FAILED(frameHr)||!frame)
        throw std::runtime_error("Synthetic ICC HEIC frame missing");
    UINT frameCount=0;
    const HRESULT frameColors=frame->GetColorContexts(0,nullptr,&frameCount);
    printf("WIC_PHOTOS_ICC_PRESENT_FRAME_HRESULT=0x%08X\n",(unsigned)frameColors);
    printf("WIC_PHOTOS_ICC_PRESENT_FRAME_COUNT=%u\n",frameCount);
    if(SUCCEEDED(frameColors) && frameCount>0) {
        IWICColorContext* profile=nullptr;
        UINT returned=0;
        HRESULT fetch=frame->GetColorContexts(1,&profile,&returned);
        CComPtr<IWICColorContext> holder;
        holder.Attach(profile);
        printf("WIC_PHOTOS_ICC_PRESENT_PROFILE_FETCH_HRESULT=0x%08X\n",(unsigned)fetch);
        printf("WIC_PHOTOS_ICC_PRESENT_PROFILE_RETURNED=%u\n",returned);
        if(SUCCEEDED(fetch) && profile) {
            WICColorContextType type=WICColorContextUninitialized;
            HRESULT typeHr=profile->GetType(&type);
            printf("WIC_PHOTOS_ICC_PRESENT_PROFILE_TYPE_HRESULT=0x%08X\n",(unsigned)typeHr);
            printf("WIC_PHOTOS_ICC_PRESENT_PROFILE_TYPE=%u\n",(unsigned)type);
        }
    }
    puts("WIC_PHOTOS_ICC_PRESENT_AB_OBSERVATION_COMPLETE_PASS");
}
// Read-only capability survey. It is deliberately compatible with the
// historical v1.1.18 DLL, which violates current arbitration invariants;
// no correctness assertions are made inside this observer. Used ONLY on
// public synthetic HEIC in isolated CI, never user photography.
inline void Survey() {
    CComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
        (void**)&factory)))throw std::runtime_error("Survey WIC factory failed");
    CComPtr<IWICStream> source;
    if(FAILED(factory->CreateStream(&source)) ||
       FAILED(source->InitializeFromFilename(
          L"samples\\fixture-irot3-exif6-noaux.heic",GENERIC_READ)))
        throw std::runtime_error("Survey synthetic HEIC stream failed");
    CComPtr<IWICBitmapDecoder> custom;
    HRESULT hr=factory->CreateDecoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi, &custom);
    if(FAILED(hr) || !custom)
        throw std::runtime_error("Survey explicit-vendor decoder failed");
    DWORD mask=0xFFFFFFFFu;
    hr=custom->QueryCapability(source,&mask);
    if(FAILED(hr))throw std::runtime_error("Survey QueryCapability failed");
    printf("WIC_PHOTOS_CAPABILITY_SURVEY_MASK=0x%X\n",unsigned(mask));
    printf("WIC_PHOTOS_CAPABILITY_SURVEY_SAME_ENCODER=%u\n",
         !!(mask & WICBitmapDecoderCapabilitySameEncoder));
    printf("WIC_PHOTOS_CAPABILITY_SURVEY_ANY_PRIMARY=%u\n",
         !!(mask & (WICBitmapDecoderCapabilityCanDecodeAllImages |
                    WICBitmapDecoderCapabilityCanDecodeSomeImages)));
    CComPtr<IWICBitmapDecoder> automatic;
    hr=factory->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&automatic);
    if(FAILED(hr)||!automatic)
        throw std::runtime_error("Survey default WIC selection failed");
    CComPtr<IWICBitmapDecoderInfo> info;
    if(FAILED(automatic->GetDecoderInfo(&info)) || !info)
        throw std::runtime_error("Survey decoder info failed");
    CLSID chosen{};
    if(FAILED(info->GetCLSID(&chosen)))
        throw std::runtime_error("Survey chosen CLSID failed");
    printf("WIC_PHOTOS_CAPABILITY_SURVEY_DEFAULT_IS_CUSTOM=%u\n",
        !!IsEqualGUID(chosen,CLSID_HEICDecoder));
    // Observation-only editor preflight matrix. These probes use only an
    // already-present synthetic HEIC and transient COM objects; no WriteSource,
    // file creation, photo edits, registry modification, or MSI installation.
    // A/B records HRESULTs without assuming that any one HRESULT explains
    // Microsoft Photos' editor UI, which cannot run in Windows Server CI.
    GUID selectedContainer{};
    hr=automatic->GetContainerFormat(&selectedContainer);
    if(FAILED(hr))throw std::runtime_error("Survey container GUID failed");
    printf("WIC_PHOTOS_EDITOR_CONTAINER_MATCHES_PRIVATE=%u\n",
        !!IsEqualGUID(selectedContainer,GUID_ContainerFormatHEIC));

    CComPtr<IWICBitmapSource> preview;
    hr=automatic->GetPreview(&preview);
    printf("WIC_PHOTOS_EDITOR_DECODER_PREVIEW_HRESULT=0x%08X\n",unsigned(hr));
    CComPtr<IWICMetadataQueryReader> containerMeta;
    hr=automatic->GetMetadataQueryReader(&containerMeta);
    printf("WIC_PHOTOS_EDITOR_CONTAINER_METADATA_HRESULT=0x%08X\n",unsigned(hr));

    UINT containerContexts=0;
    hr=automatic->GetColorContexts(0,nullptr,&containerContexts);
    printf("WIC_PHOTOS_EDITOR_CONTAINER_COLORCONTEXTS_HRESULT=0x%08X\n",unsigned(hr));
    printf("WIC_PHOTOS_EDITOR_CONTAINER_COLORCONTEXTS_COUNT=%u\n",containerContexts);

    CComPtr<IWICBitmapFrameDecode> firstFrame;
    hr=automatic->GetFrame(0,&firstFrame);
    if(FAILED(hr)||!firstFrame)throw std::runtime_error("Survey primary frame failed");
    WICPixelFormatGUID pix{};
    hr=firstFrame->GetPixelFormat(&pix);
    if(FAILED(hr))throw std::runtime_error("Survey primary pixel format failed");
    UINT frameContexts=0;
    hr=firstFrame->GetColorContexts(0,nullptr,&frameContexts);
    printf("WIC_PHOTOS_EDITOR_FRAME_COLORCONTEXTS_HRESULT=0x%08X\n",unsigned(hr));
    printf("WIC_PHOTOS_EDITOR_FRAME_COLORCONTEXTS_COUNT=%u\n",frameContexts);
    CComPtr<IWICMetadataQueryReader> frameMeta;
    hr=firstFrame->GetMetadataQueryReader(&frameMeta);
    printf("WIC_PHOTOS_EDITOR_FRAME_METADATA_HRESULT=0x%08X\n",unsigned(hr));

    CComPtr<IWICBitmapEncoder> automaticEncoder;
    hr=factory->CreateEncoder(GUID_ContainerFormatHEIC,nullptr,&automaticEncoder);
    printf("WIC_PHOTOS_EDITOR_DEFAULT_ENCODER_HRESULT=0x%08X\n",unsigned(hr));
    if(SUCCEEDED(hr)&&automaticEncoder){
        CComPtr<IWICMetadataQueryWriter> writer;
        hr=automaticEncoder->GetMetadataQueryWriter(&writer);
        printf("WIC_PHOTOS_EDITOR_DEFAULT_ENCODER_METADATA_WRITER_HRESULT=0x%08X\n",unsigned(hr));
    }
    CComPtr<IWICBitmapEncoder> vendorEncoder;
    hr=factory->CreateEncoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,&vendorEncoder);
    printf("WIC_PHOTOS_EDITOR_VENDOR_ENCODER_HRESULT=0x%08X\n",unsigned(hr));
    if(SUCCEEDED(hr)&&vendorEncoder){
        CComPtr<IWICMetadataQueryWriter> writer;
        hr=vendorEncoder->GetMetadataQueryWriter(&writer);
        printf("WIC_PHOTOS_EDITOR_VENDOR_ENCODER_METADATA_WRITER_HRESULT=0x%08X\n",unsigned(hr));
        CComPtr<IWICBitmapFrameEncode> newFrame;
        hr=vendorEncoder->CreateNewFrame(&newFrame,nullptr);
        printf("WIC_PHOTOS_EDITOR_CREATE_FRAME_HRESULT=0x%08X\n",unsigned(hr));
        if(SUCCEEDED(hr)&&newFrame){
            CComPtr<IWICMetadataQueryWriter> frameWriter;
            hr=newFrame->GetMetadataQueryWriter(&frameWriter);
            printf("WIC_PHOTOS_EDITOR_FRAME_METADATA_WRITER_HRESULT=0x%08X\n",unsigned(hr));
        }
    }
    // Reproduce the dangerous Photos-style WIC raster rotation in MEMORY.
    // The old 1.1.18 codec enabled rotation UI but could silently re-encode
    // a rotated HEIC without preserving Exif/ICC/HDR. This A/B observes
    // WriteSource before Commit. No file or original photo is modified.
    CComPtr<IWICBitmapFlipRotator> rasterRotation;
    hr=factory->CreateBitmapFlipRotator(&rasterRotation);
    if(FAILED(hr))throw std::runtime_error("Survey memory-only rotate unavailable");
    hr=rasterRotation->Initialize(firstFrame,WICBitmapTransformRotate90);
    if(FAILED(hr))throw std::runtime_error("Survey memory-only rotate init failed");
    UINT rotatedWidth=0,rotatedHeight=0;
    hr=rasterRotation->GetSize(&rotatedWidth,&rotatedHeight);
    if(FAILED(hr))throw std::runtime_error("Survey rotated dimensions unavailable");
    for(unsigned method : {0u,2u}) {
        CComPtr<IWICBitmapEncoder> enc;
        hr=factory->CreateEncoder(GUID_ContainerFormatHEIC,
            &GUID_VendorPrsyahmi,&enc);
        if(FAILED(hr)||!enc)throw std::runtime_error("Survey raster encoder missing");
        CComPtr<IStream> memoryOnly;
        hr=CreateStreamOnHGlobal(nullptr,TRUE,&memoryOnly);
        if(FAILED(hr)||!memoryOnly)throw std::runtime_error("Survey memory IStream failed");
        hr=enc->Initialize(memoryOnly,WICBitmapEncoderNoCache);
        if(FAILED(hr))throw std::runtime_error("Survey raster encoder init failed");
        CComPtr<IWICBitmapFrameEncode> frameToWrite;
        CComPtr<IPropertyBag2> bag;
        hr=enc->CreateNewFrame(&frameToWrite,&bag);
        if(FAILED(hr)||!frameToWrite)throw std::runtime_error("Survey raster frame failed");
        if(method==2) {
            if(!bag)throw std::runtime_error("Survey raster options missing");
            PROPBAG2 key{};key.pstrName=const_cast<LPOLESTR>(
                L"HeifCompressionMethod");
            VARIANT v{};v.vt=VT_UI1;v.bVal=2;
            hr=bag->Write(1,&key,&v);
            if(FAILED(hr))throw std::runtime_error("Survey explicit raster option failed");
        }
        hr=frameToWrite->Initialize(bag);
        if(FAILED(hr))throw std::runtime_error("Survey raster frame initialize failed");
        hr=frameToWrite->SetSize(rotatedWidth,rotatedHeight);
        if(FAILED(hr))throw std::runtime_error("Survey raster frame size failed");
        GUID format=GUID_WICPixelFormat24bppRGB;
        hr=frameToWrite->SetPixelFormat(&format);
        if(FAILED(hr))throw std::runtime_error("Survey raster pixel format failed");
        hr=frameToWrite->WriteSource(rasterRotation,nullptr);
        printf("WIC_PHOTOS_EDITOR_MEMONLY_ROTATOR_WRITESOURCE_METHOD_%u=0x%08X\n",
            method,unsigned(hr));
        // No frame.Commit or encoder.Commit. A baseline may have accepted a
        // lossy raster; neither test writes a HEIC file or user content.
        STATSTG st{};
        if(FAILED(memoryOnly->Stat(&st,STATFLAG_NONAME)) ||
            st.cbSize.QuadPart!=0)
            throw std::runtime_error("Survey unexpectedly wrote memory stream bytes");
    }
    puts("WIC_PHOTOS_EDITOR_MEMONLY_ROTATOR_AB_SURVEY_COMPLETE_PASS");
    SurveySyntheticIccForPhotosEditor(factory);
    puts("WIC_PHOTOS_EDITOR_COM_API_SURVEY_COMPLETE_PASS");
    printf("WIC_PHOTOS_CAPABILITY_SURVEY_COMPLETE_PASS\n");
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
        (void**)&factory)))throw std::runtime_error("Capability factory unavailable");
    Verify(factory,L"samples\\fixture-irot3-exif6.heic",true,true);
    Verify(factory,L"samples\\fixture-irot3-exif6-noaux.heic",true,true);
    Verify(factory,L"samples\\01.png",false,false);
    // The codec is explicitly constructed here to ensure malformed formats
    // return BADIMAGE rather than an invented out-of-memory condition.
    CComPtr<IWICBitmapDecoder> invalid;
    if(FAILED(CoCreateInstance(CLSID_HEICDecoder,nullptr,CLSCTX_INPROC_SERVER,
          IID_IWICBitmapDecoder,(void**)&invalid)))
        throw std::runtime_error("Malformed-HEIF decoder activation failed");
    CComPtr<IWICStream> png;
    if(FAILED(factory->CreateStream(&png)) ||
       FAILED(png->InitializeFromFilename(L"samples\\01.png",GENERIC_READ)))
        throw std::runtime_error("Malformed HEIF fixture open failed");
    HRESULT bad=invalid->Initialize(png,WICDecodeMetadataCacheOnDemand);
    if(bad!=WINCODEC_ERR_BADIMAGE)
        throw std::runtime_error("Malformed HEIF must return WINCODEC_ERR_BADIMAGE");
    puts("WIC_PHOTOS_MALFORMED_HEIF_REPORTS_BADIMAGE_NOT_OOM_PASS");
    printf("WIC_PHOTOS_DECODER_ARBITRATION_CONTRACT_PASS\n");
}
}
