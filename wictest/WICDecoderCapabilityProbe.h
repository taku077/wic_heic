#pragma once
#include <array>
#include "../wic_heic/HeicSourceAccess.h"
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
    // A/B against the ACTUAL historical MSI built by workflow run 39433397223
    // at a18b9ab (not the later f5b66 source tree). That old decoder
    // advertises WIC 48bppRGB for a 10-bit HEIC but sets m_Bpp=32 and
    // CopyPixels copies only four bytes per pixel. Detect the untouched
    // trailing bytes of each six-byte-per-pixel WIC output row.
    // This is a read-only public synthetic HEIC and a stack buffer: no
    // metadata, pixels, filenames or paths are written to the log.
    WICPixelFormatGUID originalFormat{};
    hr=pixelFrame->GetPixelFormat(&originalFormat);
    if(FAILED(hr))throw std::runtime_error("Synthetic frame WIC format missing");
    if(!IsEqualGUID(originalFormat,GUID_WICPixelFormat48bppRGB))
        throw std::runtime_error("Synthetic 10-bit fixture is not WIC 48bpp RGB");
    constexpr UINT testW=16, testH=8, testStride=testW*6;
    std::array<BYTE,testStride*testH> originalSentinel{};
    for(size_t i=0;i<originalSentinel.size();++i)
        originalSentinel[i]=BYTE((i*73u+19u)&255u);
    auto rawRead=originalSentinel;
    WICRect rectangle{0,0,INT(testW),INT(testH)};
    const HRESULT rawHr=pixelFrame->CopyPixels(&rectangle,testStride,
        UINT(rawRead.size()),rawRead.data());
    printf("WIC_PHOTOS_10BIT_RAW48_ROI_HRESULT=0x%08X\\n",unsigned(rawHr));
    if(FAILED(rawHr))
        throw std::runtime_error("Synthetic WIC raw48 source CopyPixels failed");
    unsigned untouchedTailRows=0;
    unsigned changedHeadRows=0;
    for(UINT y=0;y<testH;++y) {
        const size_t row=size_t(y)*testStride;
        bool tailOriginal=true;
        bool headChanged=false;
        for(UINT x=0;x<testStride;++x) {
            if(x<testW*4 && rawRead[row+x]!=originalSentinel[row+x])
                headChanged=true;
            if(x>=testW*4 && rawRead[row+x]!=originalSentinel[row+x])
                tailOriginal=false;
        }
        if(tailOriginal)++untouchedTailRows;
        if(headChanged)++changedHeadRows;
    }
    printf("WIC_PHOTOS_10BIT_RAW48_TAIL_UNTOUCHED_ROWS=%u\\n",untouchedTailRows);
    printf("WIC_PHOTOS_10BIT_RAW48_HEAD_CHANGED_ROWS=%u\\n",changedHeadRows);
    puts("WIC_PHOTOS_10BIT_RAW48_STRIDE_AB_SURVEY_COMPLETE_PASS");
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
    // Both versions returned DIFFERENT RGB fingerprints for WIC frame 0.
    // This synthetic HEIC contains TWO image frames. Old WIC picked the
    // first item ID, whereas research WIC intentionally maps pitm primary
    // to index 0. Compare *both* frames, so differing source frames are not
    // misdiagnosed as differing HEVC decoders or RGB color conversion.
    if(frameCount==2) {
        CComPtr<IWICBitmapFrameDecode> secondary;
        const HRESULT get=automatic->GetFrame(1,&secondary);
        if(FAILED(get)||!secondary)
            throw std::runtime_error("HEIC second frame unavailable for source comparison");
        UINT sw=0,sh=0;
        if(FAILED(secondary->GetSize(&sw,&sh)) || !sw || !sh)
            throw std::runtime_error("HEIC second frame size unavailable");
        const uint64_t secondBytes=uint64_t(sw)*sh*3ull;
        if(secondBytes>64ull*1024*1024 || !secondBytes)
            throw std::runtime_error("HEIC second frame out of bounded observation");
        CComPtr<IWICBitmapSource> convertedSecond;
        if(FAILED(WICConvertBitmapSource(
            GUID_WICPixelFormat24bppRGB,secondary,&convertedSecond)))
            throw std::runtime_error("HEIC second frame RGB conversion failed");
        std::vector<BYTE> other(static_cast<size_t>(secondBytes));
        if(FAILED(convertedSecond->CopyPixels(nullptr,sw*3,
            UINT(secondBytes),other.data())))
            throw std::runtime_error("HEIC second frame full RGB read failed");
        uint64_t secondHash=14695981039346656037ull;
        for(BYTE value:other) {
            secondHash^=value;
            secondHash*=1099511628211ull;
        }
        printf("WIC_PHOTOS_PIXEL_FRAME1_SIZE=%ux%u\\n",sw,sh);
        printf("WIC_PHOTOS_PIXEL_FRAME1_FNV64=%016llX\\n",
            static_cast<unsigned long long>(secondHash));
    }
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
        // Exercise the legal WIC lifecycle on both historical and
        // research implementations. Earlier A/B accidentally called
        // CreateNewFrame BEFORE Initialize: old 1.1.18 accepted this
        // invalid order, while corrected research rejects it correctly.
        // The observation here is about GUI-relevant *valid* WIC APIs.
        CComPtr<IStream> surveyOutput;
        hr=CreateStreamOnHGlobal(nullptr,TRUE,&surveyOutput);
        if(FAILED(hr)||!surveyOutput)
            throw std::runtime_error("Survey encoder memory stream missing");
        hr=vendorEncoder->Initialize(surveyOutput,WICBitmapEncoderNoCache);
        if(FAILED(hr))
            throw std::runtime_error("Survey encoder initialization failed");
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
    // Actual WIC encoder integration, not only a private RAW-source probe.
    // With pitm=2, copying selected frame zero must retain the ENTIRE
    // original container, including its new primary association, exactly.
    // A secondary frame MUST NOT silently produce the unrelated primary
    // container. Both outputs are memory streams; no files are touched.
    auto verifyWrite=[&](IWICBitmapFrameDecode* selected,bool primary) {
        CComPtr<IWICBitmapEncoder> encoder;
        if(FAILED(factory->CreateEncoder(GUID_ContainerFormatHEIC,
            &GUID_VendorPrsyahmi,&encoder)))
            throw std::runtime_error("Primary-switch WIC encoder missing");
        CComPtr<IStream> memory;
        if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&memory)))
            throw std::runtime_error("Primary-switch WIC output memory missing");
        if(FAILED(encoder->Initialize(memory,WICBitmapEncoderNoCache)))
            throw std::runtime_error("Primary-switch WIC encoder init failed");
        CComPtr<IWICBitmapFrameEncode> outputFrame;
        if(FAILED(encoder->CreateNewFrame(&outputFrame,nullptr)) ||
           FAILED(outputFrame->Initialize(nullptr)))
            throw std::runtime_error("Primary-switch WIC output frame init failed");
        UINT sw=0,sh=0;
        if(FAILED(selected->GetSize(&sw,&sh)) ||
           FAILED(outputFrame->SetSize(sw,sh)))
            throw std::runtime_error("Primary-switch output dimensions failed");
        const HRESULT writeHr=outputFrame->WriteSource(selected,nullptr);
        if(!primary) {
            if(writeHr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
                throw std::runtime_error("Secondary HEIC frame was copied as full primary container");
            STATSTG st{};
            if(FAILED(memory->Stat(&st,STATFLAG_NONAME)) ||
               st.cbSize.QuadPart!=0)
                throw std::runtime_error("Rejected secondary wrote output bytes");
            puts("WIC_PRIMARY_SWITCH_SECONDARY_LOSSLESS_COPY_REJECT_PASS");
            return;
        }
        if(FAILED(writeHr) || FAILED(outputFrame->Commit()) ||
           FAILED(encoder->Commit()))
            throw std::runtime_error("Primary-switch lossless WIC copy failed");
        STATSTG st{};
        if(FAILED(memory->Stat(&st,STATFLAG_NONAME)) ||
           st.cbSize.QuadPart!=bytes.size())
            throw std::runtime_error("Primary-switch lossless output byte length changed");
        LARGE_INTEGER zero{};
        if(FAILED(memory->Seek(zero,STREAM_SEEK_SET,nullptr)))
            throw std::runtime_error("Primary-switch output rewind failed");
        std::vector<BYTE> copiedResult(bytes.size());
        ULONG n=0;
        if(FAILED(memory->Read(copiedResult.data(),ULONG(copiedResult.size()),&n)) ||
           n!=copiedResult.size() || copiedResult!=bytes)
            throw std::runtime_error("Primary-switch HEIC lossless output lost source bytes");
        puts("WIC_PRIMARY_SWITCH_LOSSLESS_WIC_COPY_BYTE_EQUAL_PASS");
    };
    verifyWrite(first,true);
    verifyWrite(second,false);
    puts("WIC_PRIMARY_HEIC_FRAME_ZERO_INVARIANT_PASS");
}
// The WIC specification says CanDecodeThumbnail is ONLY for real embedded
// global/frame thumbnails. When none exists, WIC callers can scale the
// decoded primary themselves. Do NOT copy historical 1.1.18's dishonest
// advertised thumbnail flag or force a costly synthesized thumbnail into
// IWICBitmapDecoder::GetThumbnail. Measure the standards-compliant fallback
// against a public HEIC with no embedded thumbnail.
inline void VerifyWicScaleFallbackWithoutEmbeddedThumbnail(IWICImagingFactory* factory) {
    CComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr=factory->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder);
    if(FAILED(hr)||!decoder)
        throw std::runtime_error("WIC nonembedded thumbnail source missing");
    CComPtr<IWICBitmapFrameDecode> primary;
    if(FAILED(decoder->GetFrame(0,&primary)))
        throw std::runtime_error("WIC nonembedded thumbnail primary missing");
    CComPtr<IWICBitmapSource> absent;
    HRESULT containerHr=decoder->GetThumbnail(&absent);
    const bool containerMissing=containerHr==WINCODEC_ERR_CODECNOTHUMBNAIL && !absent;
    HRESULT frameHr=primary->GetThumbnail(&absent);
    const bool frameMissing=frameHr==WINCODEC_ERR_CODECNOTHUMBNAIL && !absent;
    if(!containerMissing||!frameMissing)
        throw std::runtime_error("Absent HEIC thumbnail was falsely returned as embedded");
    CComPtr<IWICStream> stream;
    if(FAILED(factory->CreateStream(&stream)) ||
       FAILED(stream->InitializeFromFilename(
          L"samples\\fixture-irot3-exif6-noaux.heic",GENERIC_READ)))
        throw std::runtime_error("WIC thumbnail capability stream failed");
    CComPtr<IWICBitmapDecoder> queryDecoder;
    if(FAILED(factory->CreateDecoder(GUID_ContainerFormatHEIC,
       &GUID_VendorPrsyahmi,&queryDecoder)))
        throw std::runtime_error("WIC thumbnail capability codec missing");
    DWORD flags=0;
    if(FAILED(queryDecoder->QueryCapability(stream,&flags)) ||
       (flags&WICBitmapDecoderCapabilityCanDecodeThumbnail))
        throw std::runtime_error("WIC claimed an embedded thumbnail when absent");
    UINT fw=0,fh=0;
    if(FAILED(primary->GetSize(&fw,&fh))||fw<2||fh<2)
        throw std::runtime_error("WIC primary dimensions invalid");
    const UINT longSide=(fw>fh?fw:fh);
    const UINT tw=UINT((uint64_t(fw)*256ull+longSide/2)/longSide);
    const UINT th=UINT((uint64_t(fh)*256ull+longSide/2)/longSide);
    if(!tw||!th||tw>256||th>256)
        throw std::runtime_error("WIC scaler fallback geometry overflow");
    CComPtr<IWICBitmapScaler> scaled;
    if(FAILED(factory->CreateBitmapScaler(&scaled)) ||
       FAILED(scaled->Initialize(primary,tw,th,WICBitmapInterpolationModeFant)))
        throw std::runtime_error("WIC primary fallback scaling failed");
    UINT w=0,h=0;
    if(FAILED(scaled->GetSize(&w,&h))||w!=tw||h!=th)
        throw std::runtime_error("WIC fallback scaler returned wrong geometry");
    CComPtr<IWICBitmapSource> rgb;
    if(FAILED(WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,scaled,&rgb)))
        throw std::runtime_error("WIC scaled HEIC RGB conversion failed");
    std::vector<BYTE> pixels(size_t(tw)*th*3);
    if(FAILED(rgb->CopyPixels(nullptr,tw*3,UINT(pixels.size()),pixels.data())))
        throw std::runtime_error("WIC scaled HEIC readback failed");
    BYTE lo=255,hi=0;
    for(BYTE v:pixels) {if(v<lo)lo=v;if(v>hi)hi=v;}
    if(lo==hi)throw std::runtime_error("HEIC primary thumbnail produced no pixel variation");
    // A scaled raster must not masquerade as the original HEIF bitstream.
    IHeicRawSource* raw=nullptr;
    HRESULT rawHr=scaled->QueryInterface(
        IID_IHeicRawSource,reinterpret_cast<void**>(&raw));
    if(raw)raw->Release();
    if(SUCCEEDED(rawHr))
        throw std::runtime_error("Scaled HEIC preview leaked raw-source provenance");
    printf("WIC_PHOTOS_SPEC_COMPLIANT_NO_EMBEDDED_THUMBNAIL_AND_RGB_FALLBACK_PASS size=%ux%u mask=0x%X\\n",
        tw,th,unsigned(flags));
}
// Photos/WIC arbitration reads decoder REGISTRY patterns before asking
// QueryCapability. HEIC is HEVC-based, but AVIF uses AV1: this private
// libde265/HEVC build has no AV1 decoder. Do not advertise AVIF merely
// because libheif understands ISO BMFF container boxes. Both byte arrays
// below are synthetic headers created in memory, never user files.
inline void VerifyRegisteredHeicNotAvifSignature(IWICImagingFactory* factory) {
    CComPtr<IWICComponentInfo> component;
    if(FAILED(factory->CreateComponentInfo(CLSID_HEICDecoder,&component)))
        throw std::runtime_error("HEIC decoder component info missing");
    CComPtr<IWICBitmapDecoderInfo> info;
    if(FAILED(component->QueryInterface(IID_IWICBitmapDecoderInfo,
        reinterpret_cast<void**>(&info))) || !info)
        throw std::runtime_error("HEIC decoder registration metadata missing");
    for(const char* brand : {"heic","avif"}) {
        BYTE header[24]{};
        header[3]=24;
        memcpy(header+4,"ftyp",4);
        memcpy(header+8,brand,4);
        memcpy(header+16,"mif1",4);
        CComPtr<IStream> stream;
        if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)))
            throw std::runtime_error("Synthetic brand memory stream unavailable");
        ULONG written=0;
        if(FAILED(stream->Write(header,24,&written))||written!=24)
            throw std::runtime_error("Synthetic brand stream write failed");
        LARGE_INTEGER zero{};
        if(FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)))
            throw std::runtime_error("Synthetic brand seek failed");
        BOOL matched=FALSE;
        HRESULT hr=info->MatchesPattern(stream,&matched);
        if(FAILED(hr))throw std::runtime_error("WIC decoder pattern query failed");
        printf("WIC_HEIC_DECODER_PATTERN brand=%s match=%u\\n",
               brand,unsigned(!!matched));
        if((strcmp(brand,"heic")==0)!=!!matched)
            throw std::runtime_error("HEIC decoder advertised unsupported AVIF or missed HEIC");
    }
    puts("WIC_HEIC_NOT_AVIF_REGISTRY_ARBITRATION_PASS");
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,
        (void**)&factory)))throw std::runtime_error("Capability factory unavailable");
    VerifyRegisteredHeicNotAvifSignature(factory);
    VerifyPrimarySelectionWhenPitmChanges(factory);
    VerifyWicScaleFallbackWithoutEmbeddedThumbnail(factory);
    Verify(factory,L"samples\\fixture-irot3-exif6.heic",true,true);
    Verify(factory,L"samples\\fixture-irot3-exif6-noaux.heic",true,true);
    Verify(factory,L"samples\\01.png",false,false);
    // The codec is explicitly constructed here to ensure malformed formats
    // return BADIMAGE rather than an invented out-of-memory condition.
    CComPtr<IWICBitmapDecoder> invalid;
    if(FAILED(CoCreateInstance(CLSID_HEICDecoder,nullptr,CLSCTX_INPROC_SERVER,
          IID_IWICBitmapDecoder,(void**)&invalid)))
        throw std::runtime_error("Malformed-HEIF decoder activation failed");
    // Decoder capabilities can be queried before Initialize, but actual
    // frame access cannot. Return WRONGSTATE rather than false S_OK with
    // zero frames, or FRAMEMISSING for a stream never opened.
    UINT unopenedCount=999u;
    HRESULT unopenedCountHr=invalid->GetFrameCount(&unopenedCount);
    CComPtr<IWICBitmapFrameDecode> unopenedFrame;
    HRESULT unopenedFrameHr=invalid->GetFrame(0,&unopenedFrame);
    printf("WIC_DECODER_UNOPENED_FRAME_QUERIES count_hr=0x%08X count=%u frame_hr=0x%08X\\n",
        unsigned(unopenedCountHr),unopenedCount,unsigned(unopenedFrameHr));
    if(unopenedCountHr!=WINCODEC_ERR_WRONGSTATE || unopenedCount!=0 ||
       unopenedFrameHr!=WINCODEC_ERR_WRONGSTATE || unopenedFrame)
        throw std::runtime_error("Decoder exposed frames before successful Initialize");
    CComPtr<IWICStream> png;
    if(FAILED(factory->CreateStream(&png)) ||
       FAILED(png->InitializeFromFilename(L"samples\\01.png",GENERIC_READ)))
        throw std::runtime_error("Malformed HEIF fixture open failed");
    HRESULT bad=invalid->Initialize(png,WICDecodeMetadataCacheOnDemand);
    if(bad!=WINCODEC_ERR_BADIMAGE)
        throw std::runtime_error("Malformed HEIF must return WINCODEC_ERR_BADIMAGE");
    UINT failedCount=999u;
    HRESULT failedCountHr=invalid->GetFrameCount(&failedCount);
    CComPtr<IWICBitmapFrameDecode> failedFrame;
    HRESULT failedFrameHr=invalid->GetFrame(0,&failedFrame);
    printf("WIC_DECODER_FAILED_INIT_FRAME_QUERIES count_hr=0x%08X count=%u frame_hr=0x%08X\\n",
        unsigned(failedCountHr),failedCount,unsigned(failedFrameHr));
    if(failedCountHr!=WINCODEC_ERR_WRONGSTATE || failedCount!=0 ||
       failedFrameHr!=WINCODEC_ERR_WRONGSTATE || failedFrame)
        throw std::runtime_error("Failed HEIF parse left accessible fake frames");
    puts("WIC_DECODER_FRAME_QUERIES_REQUIRE_SUCCESSFUL_INITIALIZATION_PASS");
    puts("WIC_PHOTOS_MALFORMED_HEIF_REPORTS_BADIMAGE_NOT_OOM_PASS");
    printf("WIC_PHOTOS_DECODER_ARBITRATION_CONTRACT_PASS\n");
}
}
