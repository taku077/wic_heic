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
inline void PreserveCompleteHEICOnDefaultCopy(IWICImagingFactory* factory) {
    // Linked EXIF/ICC/thumbnail/HDR fixture must be copied byte-for-byte.
    // The default HEIF encoder option must never discard the source metadata.
    const wchar_t* sourceFile=L"samples\\fixture-irot3-exif6.heic";
    const wchar_t* destFile=L"samples\\fixture-wic-default-copy-exact.heic";
    const auto original=Read(sourceFile);
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(sourceFile,nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"exact-copy source decoder");
    CComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,&source),"exact-copy source frame");
    UINT w=0,h=0;
    Check(source->GetSize(&w,&h),"exact-copy dimensions");
    CComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,&encoder),"exact-copy encoder");
    CComPtr<IWICStream> output;
    Check(factory->CreateStream(&output),"exact-copy output stream");
    Check(output->InitializeFromFilename(destFile,GENERIC_WRITE),"exact-copy output");
    Check(encoder->Initialize(output,WICBitmapEncoderNoCache),"exact-copy encoder init");
    CComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame,nullptr),"exact-copy new frame");
    Check(frame->Initialize(nullptr),"exact-copy default options");
    Check(frame->SetSize(w,h),"exact-copy set dimensions");
    Check(frame->WriteSource(source,nullptr),"exact-copy preserve original container");
    Check(frame->Commit(),"exact-copy frame commit");
    Check(encoder->Commit(),"exact-copy encoder commit");
    frame.Release();encoder.Release();output.Release();
    const auto actual=Read(destFile);
    if(actual!=original)throw std::runtime_error(
        "Default WIC HEIC copy lost EXIF/ICC/HDR/thumbnail bytes");
    if(Read(sourceFile)!=original)throw std::runtime_error("WIC modified original input");
    printf("WIC_DEFAULT_HEIC_COPY_EXIF_ICC_HDR_FULL_BYTE_EQUALITY_PASS bytes=%zu\\n",actual.size());
}

inline void PreserveExplicitIccOnDefaultCopy(IWICImagingFactory* factory) {
    const wchar_t* sourceFile=L"samples\\icc-roundtrip.heic";
    const wchar_t* destFile=L"samples\\icc-exact-default-copy.heic";
    const auto original=Read(sourceFile);
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(sourceFile,nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"ICC passthrough decoder");
    CComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,&source),"ICC passthrough source");
    UINT w=0,h=0;
    Check(source->GetSize(&w,&h),"ICC passthrough dimensions");
    IWICColorContext* profile=nullptr;
    UINT count=0;
    Check(source->GetColorContexts(1,&profile,&count),"ICC passthrough profile");
    if(count!=1 || !profile)throw std::runtime_error("ICC source profile missing");
    CComPtr<IWICColorContext> profileHolder;
    profileHolder.Attach(profile);
    CComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,&encoder),"ICC passthrough encoder");
    IWICColorContext* values[]={profileHolder};
    Check(encoder->SetColorContexts(1,values),"ICC passthrough write context");
    CComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream),"ICC passthrough stream");
    Check(stream->InitializeFromFilename(destFile,GENERIC_WRITE),"ICC passthrough destination");
    Check(encoder->Initialize(stream,WICBitmapEncoderNoCache),"ICC passthrough encoder init");
    CComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame,nullptr),"ICC passthrough new frame");
    Check(frame->Initialize(nullptr),"ICC passthrough frame init");
    Check(frame->SetSize(w,h),"ICC passthrough size");
    Check(frame->WriteSource(source,nullptr),"ICC passthrough preserve source bytes");
    Check(frame->Commit(),"ICC passthrough frame commit");
    Check(encoder->Commit(),"ICC passthrough encoder commit");
    frame.Release();encoder.Release();stream.Release();
    if(Read(destFile)!=original)
        throw std::runtime_error("Explicitly matching ICC profile caused source container loss");
    printf("WIC_EXPLICIT_SAME_ICC_FULL_HEIC_BYTE_EQUALITY_PASS size=%zu\\n",original.size());
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
inline void RejectRasterOnlyRotation(IWICImagingFactory* factory) {
    // A WIC flip-rotator exposes pixels but no original HEVC bitstream.
    // This resembles one possible Photos editing pipeline. It must NEVER
    // be silently re-encoded when NoCompression is explicitly requested.
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",
        nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
        "open raster-transform fixture");
    CComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,&source),"raster-transform original");
    CComPtr<IWICBitmapFlipRotator> rotator;
    Check(factory->CreateBitmapFlipRotator(&rotator),"raster-transform rotator");
    Check(rotator->Initialize(source,WICBitmapTransformRotate90),
        "wrap source in WIC pixel rotator");
    CComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,&encoder),"raster-transform encoder");
    CComPtr<IWICStream> output;
    Check(factory->CreateStream(&output),"raster-transform output");
    Check(output->InitializeFromFilename(
        L"samples\\fixture-wic-raster-rejected.heic",GENERIC_WRITE),
        "raster-transform destination");
    Check(encoder->Initialize(output,WICBitmapEncoderNoCache),"raster-transform encoder init");
    CComPtr<IWICBitmapFrameEncode> frame;
    CComPtr<IPropertyBag2> options;
    Check(encoder->CreateNewFrame(&frame,&options),"raster-transform options");
    PROPBAG2 method{};method.pstrName=const_cast<LPOLESTR>(L"HeifCompressionMethod");
    VARIANT noCompression{};noCompression.vt=VT_UI1;noCompression.bVal=1;
    Check(options->Write(1,&method,&noCompression),"raster-transform no-recompress option");
    Check(frame->Initialize(options),"raster-transform frame init");
    Check(frame->SetSize(1920,1080),"raster-transform output dimensions");
    HRESULT hr=frame->WriteSource(rotator,nullptr);
    printf("WIC_LOSSLESS_RASTER_WRAPPER_REJECTION=0x%08X\n",(unsigned)hr);
    if(hr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("WIC raster wrapper was incorrectly treated as lossless source");
}

inline void RejectImplicitOrExplicitWicRotateRasterWithoutProvenance(IWICImagingFactory* factory) {
    // The original 1.1.18 could enable the Photos edit/rotate buttons but
    // could lose metadata, stretch the image, or fail while saving. This
    // test covers a specific unsafe WriteSource route, not Photos itself.
    const wchar_t* sourcePath=L"samples\\fixture-irot3-exif6-noaux.heic";
    const auto original=Read(sourcePath);
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(sourcePath,nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"rotation-safety decoder");
    CComPtr<IWICBitmapFrameDecode> input;
    Check(decoder->GetFrame(0,&input),"rotation-safety source");
    CComPtr<IWICBitmapFlipRotator> rotate;
    Check(factory->CreateBitmapFlipRotator(&rotate),"rotation-safety rotator");
    Check(rotate->Initialize(input,WICBitmapTransformRotate90),
        "rotation-safety rotator initialize");
    UINT w=0,h=0;
    Check(rotate->GetSize(&w,&h),"rotation-safety dimensions");
    // First case is the implicit "DontCare" HEVC encoder option, which
    // previously permitted unannounced lossy conversion of a HEIC raster.
    // Second case explicitly requests HEVC but still lacks original
    // metadata/auxiliary provenance.
    for(int mode=0;mode<2;++mode) {
        const wchar_t* out=mode==0?
            L"samples\\fixture-wic-rotator-default-rejected.heic":
            L"samples\\fixture-wic-rotator-explicit-rejected.heic";
        CComPtr<IWICBitmapEncoder> encoder;
        Check(factory->CreateEncoder(GUID_ContainerFormatHEIC,
            &GUID_VendorPrsyahmi,&encoder),"rotation-safety encoder");
        CComPtr<IWICStream> stream;
        Check(factory->CreateStream(&stream),"rotation-safety output stream");
        Check(stream->InitializeFromFilename(out,GENERIC_WRITE),
            "rotation-safety unique synthetic destination");
        Check(encoder->Initialize(stream,WICBitmapEncoderNoCache),
            "rotation-safety encoder initialize");
        CComPtr<IWICBitmapFrameEncode> frame;
        CComPtr<IPropertyBag2> bag;
        Check(encoder->CreateNewFrame(&frame,&bag),"rotation-safety frame");
        if(mode==1) {
            if(!bag)throw std::runtime_error("No frame option bag");
            PROPBAG2 prop{}; prop.pstrName=const_cast<LPOLESTR>(
                L"HeifCompressionMethod");
            VARIANT value{};value.vt=VT_UI1;value.bVal=2;
            Check(bag->Write(1,&prop,&value),"rotation-safety explicit HEVC option");
        }
        Check(frame->Initialize(bag),"rotation-safety frame initialize");
        Check(frame->SetSize(w,h),"rotation-safety size");
        GUID pf=GUID_WICPixelFormat24bppRGB;
        Check(frame->SetPixelFormat(&pf),"rotation-safety pixel format");
        const HRESULT hr=frame->WriteSource(rotate,nullptr);
        printf("WIC_PHOTOS_RASTER_ROTATOR_SAVE_REJECT mode=%d hr=0x%08X\n",
            mode,unsigned(hr));
        if(hr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
            throw std::runtime_error("Rotated HEIC raster lost provenance but was accepted");
        STATSTG outputStat{};
        Check(stream->Stat(&outputStat,STATFLAG_NONAME),
            "rotation-safety destination status");
        if(outputStat.cbSize.QuadPart!=0)
            throw std::runtime_error("Rejected HEIC rotation wrote output bytes");
    }
    if(Read(sourcePath)!=original)
        throw std::runtime_error("Rejected rotation changed synthetic original");
    puts("WIC_PHOTOS_RASTER_ROTATION_METADATA_LOSS_GUARD_PASS");
}

inline void RejectUnsafeOriginalHeicRasterFallback(IWICImagingFactory* factory) {
    // Regression for a metadata-loss loophole: explicit HEVC and cropped
    // WriteSource previously bypassed the exact-source safeguard and
    // silently re-encoded an original HEIC with missing EXIF/HDR assets.
    const wchar_t* sourceFile=L"samples\\fixture-irot3-exif6.heic";
    const auto original=Read(sourceFile);
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(sourceFile,nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"unsafe-raster original");
    CComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,&source),"unsafe-raster frame");
    UINT w=0,h=0;
    Check(source->GetSize(&w,&h),"unsafe-raster dimensions");
    if(w<2 || h<2)throw std::runtime_error("Unexpected HEIC fixture size");

    for (int test=0;test<2;++test) {
        const wchar_t* outputFile=test==0?
            L"samples\\fixture-wic-raw-explicit-hevc-rejected.heic":
            L"samples\\fixture-wic-raw-crop-rejected.heic";
        CComPtr<IWICBitmapEncoder> encoder;
        Check(factory->CreateEncoder(GUID_ContainerFormatHEIC,
            &GUID_VendorPrsyahmi,&encoder),"unsafe-raster encoder");
        CComPtr<IWICStream> stream;
        Check(factory->CreateStream(&stream),"unsafe-raster stream");
        Check(stream->InitializeFromFilename(outputFile,GENERIC_WRITE),
            "unsafe-raster destination");
        Check(encoder->Initialize(stream,WICBitmapEncoderNoCache),
            "unsafe-raster encoder init");
        CComPtr<IWICBitmapFrameEncode> frame;
        CComPtr<IPropertyBag2> options;
        Check(encoder->CreateNewFrame(&frame,&options),
            "unsafe-raster encoder options");
        if(!options)throw std::runtime_error("Missing HEIC frame options");
        if(test==0) {
            PROPBAG2 method{};
            method.pstrName=const_cast<LPOLESTR>(L"HeifCompressionMethod");
            VARIANT compression{};
            compression.vt=VT_UI1; compression.bVal=2; // explicit HEVC
            Check(options->Write(1,&method,&compression),
                "unsafe-raster explicit HEVC");
        }
        Check(frame->Initialize(options),"unsafe-raster init frame");
        Check(frame->SetSize(test==0?w:w-1,h),"unsafe-raster output size");
        WICRect crop{0,0,int(w-1),int(h)};
        const HRESULT hr=frame->WriteSource(source,test==0?nullptr:&crop);
        printf("WIC_RAW_HEIC_METADATA_SAFE_REJECTION_%d=0x%08X\n",
            test,(unsigned)hr);
        if(hr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
            throw std::runtime_error("Original HEIC silently entered lossy raster path");
        frame.Release();encoder.Release();stream.Release();options.Release();
        if(!Read(outputFile).empty())
            throw std::runtime_error("Failed HEIC write produced unexpected bytes");
    }
    if(Read(sourceFile)!=original)
        throw std::runtime_error("HEIC raster rejection modified input");
    printf("WIC_RAW_HEIC_EXPLICIT_HEVC_AND_CROP_FAIL_CLOSED_PASS\n");
}

inline void RejectMutationsAfterLosslessCapture(IWICImagingFactory* factory) {
    // Previously several late setters returned S_OK despite EncoderCommit
    // writing the already-captured original HEIC container unchanged. That
    // silently discarded requested ICC, geometry and pixel modifications.
    const wchar_t* sourceFile=L"samples\\fixture-irot3-exif6.heic";
    const wchar_t* outputFile=L"samples\\fixture-wic-lossless-state-locked.heic";
    const auto original=Read(sourceFile);
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(sourceFile,nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"state-lock source decoder");
    CComPtr<IWICBitmapFrameDecode> source;
    Check(decoder->GetFrame(0,&source),"state-lock source frame");
    UINT w=0,h=0;
    Check(source->GetSize(&w,&h),"state-lock source size");
    CComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,&encoder),"state-lock encoder");
    CComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream),"state-lock stream");
    Check(stream->InitializeFromFilename(outputFile,GENERIC_WRITE),
        "state-lock output");
    Check(encoder->Initialize(stream,WICBitmapEncoderNoCache),
        "state-lock encoder initialize");
    CComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame,nullptr),"state-lock create frame");
    Check(frame->Initialize(nullptr),"state-lock initialize frame");
    Check(frame->SetSize(w,h),"state-lock set initial geometry");
    Check(frame->WriteSource(source,nullptr),"state-lock capture source bytes");
    auto reject=[&](HRESULT hr,const char* name) {
        if(hr!=WINCODEC_ERR_WRONGSTATE) {
            printf("WIC_LOSSLESS_STATE_UNLOCKED %s actual=0x%08X\n",
                name,(unsigned)hr);
            throw std::runtime_error("Captured source mutated");
        }
    };
    reject(frame->SetSize(w+1,h),"late SetSize");
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppRGB;
    reject(frame->SetPixelFormat(&format),"late SetPixelFormat");
    reject(frame->SetColorContexts(0,nullptr),"late frame SetColorContexts");
    reject(encoder->SetColorContexts(0,nullptr),"late encoder SetColorContexts");
    reject(frame->WriteSource(source,nullptr),"second WriteSource");
    BYTE pixels[3]={0,0,0};
    reject(frame->WritePixels(1,3,3,pixels),"late WritePixels");
    Check(frame->Commit(),"state-lock frame commit");
    Check(encoder->Commit(),"state-lock encoder commit");
    frame.Release();encoder.Release();stream.Release();
    if(Read(outputFile)!=original)
        throw std::runtime_error("State-lock output differs from original HEIC");
    if(Read(sourceFile)!=original)
        throw std::runtime_error("State-lock modified original HEIC");
    printf("WIC_LOSSLESS_CAPTURE_IMMUTABLE_AND_SOURCE_BYTES_PRESERVED_PASS\n");
}

// End-to-end WIC integration: four consecutive 90-degree turns using
// ONLY in-memory IStreams and the synthetic repository fixture. No file is
// created, modified, truncated, renamed, or deleted. Each step must preserve
// the HEVC bitstream and all HEIF sidecar items byte-for-byte except the
// intentional irot/Exif Orientation patch from the proven native reference.
inline void FourTurnsOnlyInMemory(IWICImagingFactory* factory) {
    const auto original=Read(L"samples\\fixture-irot3-exif6-noaux.heic");
    if(original.empty() || original.size()>16u*1024u*1024u)
        throw std::runtime_error("Synthetic HEIC fixture size out of bounds");
    auto current=original;
    UINT initialW=0,initialH=0;
    for(unsigned turn=0;turn<4;++turn) {
        CComPtr<IStream> input;
        Check(CreateStreamOnHGlobal(nullptr,TRUE,&input),
            "four-turn input memory stream");
        ULONG n=0;
        Check(input->Write(current.data(),ULONG(current.size()),&n),
            "four-turn write input memory bytes");
        if(n!=current.size())throw std::runtime_error("Short memory input");
        LARGE_INTEGER zero{};
        Check(input->Seek(zero,STREAM_SEEK_SET,nullptr),"four-turn rewind input");

        CComPtr<IWICBitmapDecoder> decoder;
        Check(factory->CreateDecoderFromStream(input,nullptr,
            WICDecodeMetadataCacheOnDemand,&decoder),
            "four-turn WIC decode HEIC in memory");
        CComPtr<IWICBitmapFrameDecode> source;
        Check(decoder->GetFrame(0,&source),"four-turn source frame");
        UINT sw=0,sh=0;
        Check(source->GetSize(&sw,&sh),"four-turn size");
        if(turn==0) {initialW=sw;initialH=sh;}
        if((turn&1)==0 && (sw!=initialW || sh!=initialH))
            throw std::runtime_error("Four-turn input geometry inconsistent");
        if((turn&1)!=0 && (sw!=initialH || sh!=initialW))
            throw std::runtime_error("Four-turn input geometry stretched");

        CComPtr<IStream> output;
        Check(CreateStreamOnHGlobal(nullptr,TRUE,&output),
            "four-turn output memory stream");
        CComPtr<IWICBitmapEncoder> encoder;
        Check(factory->CreateEncoder(GUID_ContainerFormatHEIC,
            &GUID_VendorPrsyahmi,&encoder),"four-turn WIC encoder");
        Check(encoder->Initialize(output,WICBitmapEncoderNoCache),
            "four-turn encoder initialize");
        CComPtr<IWICBitmapFrameEncode> frame;
        CComPtr<IPropertyBag2> options;
        Check(encoder->CreateNewFrame(&frame,&options),"four-turn frame and bag");
        if(!options)throw std::runtime_error("Lossless option bag missing");
        PROPBAG2 keys[2]{};
        keys[0].pstrName=const_cast<LPOLESTR>(L"BitmapTransform");
        keys[1].pstrName=const_cast<LPOLESTR>(L"HeifCompressionMethod");
        VARIANT values[2]{};
        values[0].vt=VT_UI1;values[0].bVal=1;
        values[1].vt=VT_UI1;values[1].bVal=1;
        Check(options->Write(2,keys,values),"four-turn lossless option");
        Check(frame->Initialize(options),"four-turn frame initialize");
        Check(frame->SetSize(sh,sw),"four-turn swapped dimensions");
        Check(frame->WriteSource(source,nullptr),"four-turn original HEVC transfer");
        Check(frame->Commit(),"four-turn frame commit");
        Check(encoder->Commit(),"four-turn encoder commit");

        STATSTG st{};
        Check(output->Stat(&st,STATFLAG_NONAME),"four-turn output stream stat");
        if(st.cbSize.QuadPart!=current.size())
            throw std::runtime_error("Four-turn output changed HEIC length");
        Check(output->Seek(zero,STREAM_SEEK_SET,nullptr),
            "four-turn output rewind");
        std::vector<uint8_t> next(current.size());
        ULONG got=0;
        Check(output->Read(next.data(),ULONG(next.size()),&got),
            "four-turn output read");
        if(got!=next.size())throw std::runtime_error("Four-turn short memory output");
        const auto expected=HeicLosslessNative::Transform(current,1);
        if(next!=expected)
            throw std::runtime_error("WIC lossless source differs from HEIF reference");
        // The bytes in the original source stream must not have changed.
        Check(input->Seek(zero,STREAM_SEEK_SET,nullptr),
            "four-turn original rewind");
        std::vector<uint8_t> before(current.size());
        Check(input->Read(before.data(),ULONG(before.size()),&got),
            "four-turn check original input");
        if(got!=before.size() || before!=current)
            throw std::runtime_error("Four-turn source memory was modified");
        current=std::move(next);
        printf("WIC_FOUR_90_TURNS_IN_MEMORY_STEP=%u SIZE=%ux%u BYTES=%zu\n",
            turn+1,sh,sw,current.size());
    }
    // The first transform normalizes the ambiguous irot+Exif6 orientation.
    // Thus the canonical four-turn output may not equal the *uncanonical*
    // original byte-for-byte; the changes must be confined to orientation.
    const auto canonical=HeicLosslessNative::Transform(original,1);
    const auto undo=HeicLosslessNative::Transform(canonical,3);
    if(current!=undo)
        throw std::runtime_error("Four consecutive HEIC rotations were not canonical");
    puts("WIC_FOUR_TURNS_MEMORY_ONLY_EXIF_CANONICAL_HEVC_BYTES_PRESERVED_PASS");
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
    PreserveCompleteHEICOnDefaultCopy(factory);
    PreserveExplicitIccOnDefaultCopy(factory);
    RejectAuxiliaryImage(factory);
    RejectRasterOnlyRotation(factory);
    RejectImplicitOrExplicitWicRotateRasterWithoutProvenance(factory);
    RejectUnsafeOriginalHeicRasterFallback(factory);
    RejectMutationsAfterLosslessCapture(factory);
    FourTurnsOnlyInMemory(factory);
    printf("WIC_NATIVE_LOSSLESS_90_180_270_EXACT_BYTES_AND_SAFETY_PASSED\n");
}
}
