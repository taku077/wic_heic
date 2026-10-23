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
    // A/B NEW: check the same synthetic HEIC's full-resolution RGB frame,
    // not merely the codec CLSID or its advertised capability mask. WIC
    // arbitration can succeed while GetSize/CopyPixels subsequently fail.
    // We compute a fingerprint in memory; no pixels, filenames, metadata,
    // or paths are written into CI output. Both versions use this exact probe.
    UINT frameCount=0;
    hr=automatic->GetFrameCount(&frameCount);
    printf("WIC_PHOTOS_PIXEL_FRAMECOUNT_HRESULT=0x%08X\n",(unsigned)hr);
    printf("WIC_PHOTOS_PIXEL_FRAMECOUNT=%u\n",frameCount);
    if(FAILED(hr)||frameCount==0)
        throw std::runtime_error("Synthetic HEIC has no WIC frames");
    CComPtr<IWICBitmapFrameDecode> pixelFrame;
    hr=automatic->GetFrame(0,&pixelFrame);
    printf("WIC_PHOTOS_PIXEL_GETFRAME_HRESULT=0x%08X\n",(unsigned)hr);
    if(FAILED(hr)||!pixelFrame)
        throw std::runtime_error("Synthetic HEIC frame cannot be obtained");
    UINT frameWidth=0,frameHeight=0;
    hr=pixelFrame->GetSize(&frameWidth,&frameHeight);
    printf("WIC_PHOTOS_PIXEL_GETSIZE_HRESULT=0x%08X\n",(unsigned)hr);
    printf("WIC_PHOTOS_PIXEL_SIZE=%ux%u\n",frameWidth,frameHeight);
    if(FAILED(hr)||frameWidth==0||frameHeight==0)
        throw std::runtime_error("Synthetic HEIC frame geometry missing");
    CComPtr<IWICBitmapSource> rgb;
    hr=WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,pixelFrame,&rgb);
    printf("WIC_PHOTOS_PIXEL_RGB24_CONVERT_HRESULT=0x%08X\n",(unsigned)hr);
    if(FAILED(hr)||!rgb)
        throw std::runtime_error("Synthetic HEIC full-frame RGB24 conversion failed");
    const uint64_t strideBytes=uint64_t(frameWidth)*3ull;
    const uint64_t fullBytes=strideBytes*frameHeight;
    if(!strideBytes||fullBytes>64ull*1024*1024 ||
       strideBytes>0xffffffffull)
        throw std::runtime_error("Synthetic HEIC readback exceeds bounded buffer");
    std::vector<BYTE> readback(static_cast<size_t>(fullBytes));
    hr=rgb->CopyPixels(nullptr,static_cast<UINT>(strideBytes),
                       static_cast<UINT>(fullBytes),readback.data());
    printf("WIC_PHOTOS_PIXEL_FULLRES_COPY_HRESULT=0x%08X\n",(unsigned)hr);
    if(FAILED(hr))
        throw std::runtime_error("Synthetic HEIC full-resolution CopyPixels failed");
    uint64_t fingerprint=14695981039346656037ull; // FNV-1a 64
    for(BYTE sample:readback) {
        fingerprint^=sample;
        fingerprint*=1099511628211ull;
    }
    printf("WIC_PHOTOS_PIXEL_FULLRES_BYTES=%llu\n",
        static_cast<unsigned long long>(fullBytes));
    printf("WIC_PHOTOS_PIXEL_READBACK_FNV64=%016llX\n",
        static_cast<unsigned long long>(fingerprint));
    // Synthetic-only aggregate diagnostics, not image bytes. The preceding
    // A/B showed deterministic but different full-frame RGB hashes. A color
    // histogram is invariant to pixel positions; matching histogram with
    // differing full-frame hash means rearrangement, not channel change.
    uint64_t quadrantTotals[4][3]{};
    uint32_t histogram[3][256]{};
    for(UINT y=0;y<frameHeight;++y) {
        const size_t row=static_cast<size_t>(y)*static_cast<size_t>(strideBytes);
        for(UINT col=0;col<frameWidth;++col) {
            const unsigned quadrant=(y>=frameHeight/2?2u:0u)+
                                    (col>=frameWidth/2?1u:0u);
            const size_t pixel=row+static_cast<size_t>(col)*3u;
            for(unsigned channel=0;channel<3;++channel) {
                const unsigned value=readback[pixel+channel];
                quadrantTotals[quadrant][channel]+=value;
                ++histogram[channel][value];
            }
        }
    }
    uint64_t histogramFingerprint=14695981039346656037ull;
    for(unsigned channel=0;channel<3;++channel)
        for(unsigned value=0;value<256;++value) {
            uint32_t count=histogram[channel][value];
            for(unsigned shift=0;shift<32;shift+=8) {
                histogramFingerprint^=static_cast<BYTE>(count>>shift);
                histogramFingerprint*=1099511628211ull;
            }
        }
    uint64_t channels[3]{};
    for(unsigned q=0;q<4;++q)
        for(unsigned c=0;c<3;++c)channels[c]+=quadrantTotals[q][c];
    printf("WIC_PHOTOS_PIXEL_CHANNEL_TOTALS=%llu,%llu,%llu\n",
        (unsigned long long)channels[0],(unsigned long long)channels[1],
        (unsigned long long)channels[2]);
    printf("WIC_PHOTOS_PIXEL_HISTOGRAM_FNV64=%016llX\n",
        (unsigned long long)histogramFingerprint);
    for(unsigned q=0;q<4;++q)
        printf("WIC_PHOTOS_PIXEL_QUADRANT_%u_CHANNEL_TOTALS=%llu,%llu,%llu\n",
            q,(unsigned long long)quadrantTotals[q][0],
            (unsigned long long)quadrantTotals[q][1],
            (unsigned long long)quadrantTotals[q][2]);
    puts("WIC_PHOTOS_SYNTHETIC_FULLRES_RGB_READBACK_PASS");
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
// Synthetic two-image HEIC: change only the primary item identifier in a
// local MEMORY COPY, never the fixture itself. This tests the WIC invariant
// that GetFrame(0) corresponds to HEIF's pitm primary image even when the
// primary is NOT the first item in the top-level ID list. The primary-only
// IHeicRawSource contract gives a stronger oracle than geometry alone.
inline void VerifyPrimarySelectionWhenPitmChanges(IWICImagingFactory* factory) {
    std::ifstream input("samples\\fixture-irot3-exif6-noaux.heic",std::ios::binary);
    if(!input)throw std::runtime_error("Synthetic primary-switch fixture unavailable");
    std::vector<BYTE> bytes((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    if(bytes.size()<512 || bytes.size()>65536)
        throw std::runtime_error("Unexpected synthetic primary-switch size");
    size_t pitmOffset=0;
    unsigned matches=0;
    for(size_t i=4;i+10<=bytes.size();++i) {
        if(bytes[i]=='p' && bytes[i+1]=='i' && bytes[i+2]=='t' &&
           bytes[i+3]=='m' && bytes[i-1]==14 &&
           bytes[i-2]==0 && bytes[i-3]==0 && bytes[i-4]==0 &&
           bytes[i+4]==0 && bytes[i+5]==0 &&
           bytes[i+6]==0 && bytes[i+7]==0 &&
           bytes[i+8]==0 && bytes[i+9]==1) {
            pitmOffset=i+9;
            ++matches;
        }
    }
    if(matches!=1)
        throw std::runtime_error("Synthetic pitm primary-id=1 not located uniquely");
    bytes[pitmOffset]=2; // HEIF primary item now image 2 instead of image 1
    CComPtr<IWICStream> stream;
    if(FAILED(factory->CreateStream(&stream)) ||
       FAILED(stream->InitializeFromMemory(bytes.data(),UINT(bytes.size()))))
        throw std::runtime_error("Primary-switch memory stream unavailable");
    CComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr=factory->CreateDecoderFromStream(stream,
        &GUID_VendorPrsyahmi,WICDecodeMetadataCacheOnDemand,&decoder);
    if(FAILED(hr)||!decoder)
        throw std::runtime_error("Primary-switch synthetic decoder unavailable");
    UINT count=0;
    if(FAILED(decoder->GetFrameCount(&count))||count<2)
        throw std::runtime_error("Primary-switch two-image container not recognized");
    CComPtr<IWICBitmapFrameDecode> first;
    if(FAILED(decoder->GetFrame(0,&first))||!first)
        throw std::runtime_error("Primary-switch frame zero missing");
    IHeicRawSource* raw=nullptr;
    hr=first->QueryInterface(IID_IHeicRawSource,reinterpret_cast<void**>(&raw));
    if(FAILED(hr)||!raw)
        throw std::runtime_error("Primary-switch WIC frame lacks source-provenance capability");
    std::vector<uint8_t> copied;
    const HRESULT readHr=raw->ReadFullContent(&copied);
    raw->Release();
    printf("WIC_PRIMARY_SWITCH_FRAME_ZERO_RAW_HRESULT=0x%08X\n",unsigned(readHr));
    if(FAILED(readHr) || copied!=bytes)
        throw std::runtime_error("WIC GetFrame(0) does not point to HEIF pitm primary");
    CComPtr<IWICBitmapFrameDecode> second;
    if(FAILED(decoder->GetFrame(1,&second))||!second)
        throw std::runtime_error("Primary-switch secondary frame missing");
    raw=nullptr;
    hr=second->QueryInterface(IID_IHeicRawSource,reinterpret_cast<void**>(&raw));
    if(FAILED(hr)||!raw)
        throw std::runtime_error("Primary-switch secondary provenance probe unavailable");
    copied.clear();
    const HRESULT secondary=raw->ReadFullContent(&copied);
    raw->Release();
    if(secondary!=WINCODEC_ERR_UNSUPPORTEDOPERATION || !copied.empty())
        throw std::runtime_error("WIC secondary frame exposed the primary's HEIF bytes");
    puts("WIC_PRIMARY_HEIC_FRAME_ZERO_INVARIANT_PASS");
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
        (void**)&factory)))throw std::runtime_error("Capability factory unavailable");
    VerifyPrimarySelectionWhenPitmChanges(factory);
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
