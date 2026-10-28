#pragma once
// Guard regression tests against malformed frame size, incomplete WritePixels,
// and false BGR pixel-format negotiation. Uses temporary sample outputs only.
namespace EncoderGuardProbe {
inline void Check(HRESULT hr,const char* step) {
    if (FAILED(hr)) { printf("GUARD ERROR %s: 0x%08X\n",step,(unsigned)hr); throw std::runtime_error(step); }
}
inline void Run() {
    CComPtr<IWICImagingFactory> fac;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory,(void**)&fac),"CoCreate WIC");

    const UINT W=192,H=320;
    std::vector<BYTE> pixels(size_t(W)*H*3);
    for(UINT y=0;y<H;y++)for(UINT x=0;x<W;x++) {
        size_t off=(size_t(y)*W+x)*3;
        pixels[off]=BYTE(x%256); pixels[off+1]=BYTE(y%256); pixels[off+2]=BYTE((x+2*y)%256);
    }
    CComPtr<IWICBitmap> source;
    Check(fac->CreateBitmapFromMemory(W,H,GUID_WICPixelFormat24bppRGB,
        W*3,static_cast<UINT>(pixels.size()),pixels.data(),&source),"Create source");

    CComPtr<IWICBitmapEncoder> enc;
    CComPtr<IWICStream> stream;
    CComPtr<IWICBitmapFrameEncode> frame;
    Check(fac->CreateEncoder(GUID_ContainerFormatHEIC,&GUID_VendorPrsyahmi,&enc),"Create HEIC encoder");
    HRESULT badInit=enc->Initialize(nullptr,WICBitmapEncoderNoCache);
    if(badInit!=E_INVALIDARG)throw std::runtime_error("guard: null stream not rejected");
    Check(fac->CreateStream(&stream),"Create stream");
    Check(stream->InitializeFromFilename(L"samples\\guard-writepixels.heic",GENERIC_WRITE),"Open test output");
    Check(enc->Initialize(stream,WICBitmapEncoderNoCache),"Init encoder");
    if(enc->SetThumbnail(source)!=WINCODEC_ERR_CODECNOTHUMBNAIL)
        throw std::runtime_error("guard: container-level unsupported thumbnail HRESULT must conform to WIC contract");
    if(enc->SetColorContexts(0,nullptr)!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("guard: top-level color contexts unexpectedly accepted");
    CComPtr<IWICMetadataQueryWriter> unavailableWriter;
    if(enc->GetMetadataQueryWriter(&unavailableWriter)!=WINCODEC_ERR_UNSUPPORTEDOPERATION || unavailableWriter)
        throw std::runtime_error("guard: top-level metadata writer unexpectedly available");
    HRESULT doubleInit=enc->Initialize(stream,WICBitmapEncoderNoCache);
    if(doubleInit!=WINCODEC_ERR_WRONGSTATE)
        throw std::runtime_error("guard: repeated encoder init accepted");
    Check(enc->CreateNewFrame(&frame,nullptr),"Create frame");
    Check(frame->Initialize(nullptr),"Init frame");
    HRESULT thumbnailHr=frame->SetThumbnail(source);
    if(thumbnailHr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("guard: ignored thumbnail reported as saved");
    Check(frame->SetSize(H,W),"Set intentionally wrong size");
    GUID bgr=GUID_WICPixelFormat24bppBGR;
    Check(frame->SetPixelFormat(&bgr),"Set BGR pixel format");
    if(!IsEqualGUID(bgr,GUID_WICPixelFormat24bppRGB))
        throw std::runtime_error("guard: BGR negotiation must report RGB");
    HRESULT hr=frame->WriteSource(source,nullptr);
    printf("GUARD WRONG_SIZE_WriteSource=0x%08X (expected E_INVALIDARG)\n",(unsigned)hr);
    if(hr!=E_INVALIDARG) throw std::runtime_error("guard: dimensions must be rejected");
    hr=frame->Commit();
    if(hr!=WINCODEC_ERR_WRONGSTATE) throw std::runtime_error("guard: incomplete frame committed");

    // WritePixels should reject partial or too-small buffers rather than
    // copying incomplete rows into a full-size output HEIC image.
    const UINT srcStride=H*3;
    std::vector<BYTE> widePixels(size_t(srcStride)*W,77);
    hr=frame->WritePixels(W,srcStride,static_cast<UINT>(widePixels.size()-1),widePixels.data());
    if(hr!=E_INVALIDARG) throw std::runtime_error("guard: short buffer not rejected");
    Check(frame->WritePixels(W,srcStride,static_cast<UINT>(widePixels.size()),widePixels.data()),
        "WritePixels valid");
    hr=frame->WritePixels(1,srcStride,srcStride,widePixels.data());
    if(hr!=WINCODEC_ERR_WRONGSTATE && hr!=E_INVALIDARG)
        throw std::runtime_error("guard: writing after full frame accepted");
    Check(frame->Commit(),"frame.Commit");
    Check(enc->Commit(),"encoder.Commit");
    frame.Release(); enc.Release(); stream.Release();
    CComPtr<IWICBitmapDecoder> readback;
    Check(fac->CreateDecoderFromFilename(L"samples\\guard-writepixels.heic",nullptr,
        GENERIC_READ,WICDecodeMetadataCacheOnDemand,&readback),"readback decoder");
    CComPtr<IWICBitmapFrameDecode> decoded;
    Check(readback->GetFrame(0,&decoded),"readback frame");
    UINT actualW=0,actualH=0;
    Check(decoded->GetSize(&actualW,&actualH),"readback size");
    if(actualW!=H || actualH!=W) throw std::runtime_error("guard: pixels size incorrect");
    // Fully prepared raster output must not replace existing stream bytes.
    CComPtr<IStream> protectedStream;
    Check(CreateStreamOnHGlobal(nullptr,TRUE,&protectedStream),"Create sentinel stream");
    const BYTE marker[]={0x53,0x41,0x46,0x45};
    ULONG n=0;
    Check(protectedStream->Write(marker,sizeof(marker),&n),"Write sentinel");
    if(n!=sizeof(marker))throw std::runtime_error("guard: write sentinel failed");
    LARGE_INTEGER zero{};
    Check(protectedStream->Seek(zero,STREAM_SEEK_SET,nullptr),"Rewind sentinel");
    CComPtr<IWICBitmapEncoder> protectedEnc;
    Check(fac->CreateEncoder(GUID_ContainerFormatHEIC,&GUID_VendorPrsyahmi,
        &protectedEnc),"Create protected encoder");
    Check(protectedEnc->Initialize(protectedStream,WICBitmapEncoderNoCache),
        "Initialize protected encoder");
    CComPtr<IWICBitmapFrameEncode> protectedFrame;
    Check(protectedEnc->CreateNewFrame(&protectedFrame,nullptr),"Create protected frame");
    Check(protectedFrame->Initialize(nullptr),"Initialize protected frame");
    Check(protectedFrame->SetSize(W,H),"Protected dimensions");
    GUID rgb=GUID_WICPixelFormat24bppRGB;
    Check(protectedFrame->SetPixelFormat(&rgb),"Protected format");
    Check(protectedFrame->WriteSource(source,nullptr),"Protected source");
    Check(protectedFrame->Commit(),"Protected frame commit");
    HRESULT protectHr=protectedEnc->Commit();
    if(protectHr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("guard: existing output not rejected");
    STATSTG stat{};
    Check(protectedStream->Stat(&stat,STATFLAG_NONAME),"Stat protected stream");
    if(stat.cbSize.QuadPart!=sizeof(marker))
        throw std::runtime_error("guard: existing stream length changed");
    Check(protectedStream->Seek(zero,STREAM_SEEK_SET,nullptr),"Rewind protected stream");
    BYTE got[sizeof(marker)]{};
    n=0;
    Check(protectedStream->Read(got,sizeof(got),&n),"Read protected stream");
    if(n!=sizeof(marker) || memcmp(got,marker,sizeof(marker))!=0)
        throw std::runtime_error("guard: existing output bytes changed");
    printf("ENCODER_GEOMETRY_FAILSAFE_TEST_PASSED\n");
    printf("ENCODER_NONEMPTY_STREAM_AND_THUMBNAIL_FAIL_CLOSED_PASSED\n");
}
} // namespace EncoderGuardProbe
