#pragma once
// JPEG-parity regression: a WIC ICC profile written through the HEIC encoder
// must be recoverable byte-for-byte from the HEIC decoder. Synthetic pixels only.
namespace ICCProfileProbe {
inline void Must(HRESULT hr, const char* what) {
    if (FAILED(hr)) { printf("ICC_ERROR %s 0x%08X\n", what, (unsigned)hr); throw std::runtime_error(what); }
}
inline void Run() {
    WCHAR systemDir[MAX_PATH]{};
    if (!GetSystemDirectoryW(systemDir, MAX_PATH)) throw std::runtime_error("ICC GetSystemDirectory");
    std::wstring profilePath(systemDir);
    profilePath += L"\\spool\\drivers\\color\\sRGB Color Space Profile.icm";
    CComPtr<IWICImagingFactory> factory;
    Must(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IWICImagingFactory, (void**)&factory), "Create factory");
    CComPtr<IWICColorContext> inputContext;
    Must(factory->CreateColorContext(&inputContext), "Create input ICC context");
    Must(inputContext->InitializeFromFilename(profilePath.c_str()), "Load standard Windows sRGB ICC");
    UINT inputSize=0;
    Must(inputContext->GetProfileBytes(0,nullptr,&inputSize),"Get input ICC size");
    if (!inputSize || inputSize > 16u*1024u*1024u) throw std::runtime_error("ICC input size");
    std::vector<BYTE> inputICC(inputSize);
    UINT got=0;
    Must(inputContext->GetProfileBytes(inputSize,inputICC.data(),&got),"Read ICC");
    if (got!=inputSize) throw std::runtime_error("ICC input bytes truncated");
    const UINT w=64, h=64;
    std::vector<BYTE> pixels(size_t(w)*h*3,127);
    CComPtr<IWICBitmap> bitmap;
    Must(factory->CreateBitmapFromMemory(w,h,GUID_WICPixelFormat24bppRGB,
                                         w*3,UINT(pixels.size()),pixels.data(),&bitmap),"Create synthetic bitmap");
    CComPtr<IWICBitmapEncoder> encoder;
    Must(factory->CreateEncoder(GUID_ContainerFormatHEIC,&GUID_VendorPrsyahmi,&encoder),"Create HEIC encoder");
    CComPtr<IWICStream> stream;
    Must(factory->CreateStream(&stream),"Create stream");
    Must(stream->InitializeFromFilename(L"samples\\icc-roundtrip.heic",GENERIC_WRITE),"Open ICC output");
    Must(encoder->Initialize(stream,WICBitmapEncoderNoCache),"Init encoder");
    IWICColorContext* contexts[]={inputContext};
    Must(encoder->SetColorContexts(1,contexts),"Encoder SetColorContexts");
    CComPtr<IWICBitmapFrameEncode> frame;
    Must(encoder->CreateNewFrame(&frame,nullptr),"Create frame");
    Must(frame->Initialize(nullptr),"Init frame");
    Must(frame->SetSize(w,h),"Set size");
    GUID rgb=GUID_WICPixelFormat24bppRGB;
    Must(frame->SetPixelFormat(&rgb),"Set pixel format");
    Must(frame->SetColorContexts(1,contexts),"Frame SetColorContexts");
    Must(frame->WriteSource(bitmap,nullptr),"Write synthetic source");
    Must(frame->Commit(),"Commit frame");
    Must(encoder->Commit(),"Commit HEIC");
    frame.Release(); encoder.Release(); stream.Release();
    CComPtr<IWICBitmapDecoder> decoder;
    Must(factory->CreateDecoderFromFilename(L"samples\\icc-roundtrip.heic",nullptr,
          GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),"HEIC readback");
    CComPtr<IWICBitmapFrameDecode> decoded;
    Must(decoder->GetFrame(0,&decoded),"Get decoded frame");
    UINT count=0;
    Must(decoded->GetColorContexts(0,nullptr,&count),"Query color context count");
    if(count!=1)throw std::runtime_error("ICC not exposed on decoded HEIC");
    CComPtr<IWICColorContext> outputContext;
    IWICColorContext* received[]={nullptr};
    Must(decoded->GetColorContexts(1,received,&count),"Get decoded color context");
    outputContext.Attach(received[0]);
    if(count!=1 || !outputContext)throw std::runtime_error("ICC context absent");
    UINT outputSize=0;
    Must(outputContext->GetProfileBytes(0,nullptr,&outputSize),"Get output ICC size");
    if(outputSize!=inputSize)throw std::runtime_error("ICC length not preserved");
    std::vector<BYTE> outputICC(outputSize);
    Must(outputContext->GetProfileBytes(outputSize,outputICC.data(),&got),"Read output ICC");
    if(got!=outputSize || outputICC!=inputICC)throw std::runtime_error("ICC content changed");
    printf("HEIC_WIC_ICC_EXACT_ROUNDTRIP_PASSED size=%u\n",inputSize);
}
}
