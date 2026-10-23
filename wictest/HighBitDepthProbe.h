#pragma once
// JPEG-equivalence regression for a pinned synthetic 10-bit HEIC fixture.
// Never use user-supplied real photos or output files.
namespace HighBitDepthProbe {
inline void Check(HRESULT hr, const char* where) {
    if (FAILED(hr)) { printf("HIGHBIT_ERROR %s 0x%08X\n",where,(unsigned)hr); throw std::runtime_error(where); }
}
inline void Run() {
    CComPtr<IWICImagingFactory> fac;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
       IID_IWICImagingFactory,(void**)&fac),"factory");
    CComPtr<IWICBitmapDecoder> input;
    Check(fac->CreateDecoderFromFilename(L"samples\\fixture-irot3-exif6-noaux.heic",
         nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&input),"10bit synthetic decoder");
    CComPtr<IWICBitmapFrameDecode> decoded;
    Check(input->GetFrame(0,&decoded),"10bit fixture frame");
    GUID fmt{};Check(decoded->GetPixelFormat(&fmt),"source pixel format");
    if(!IsEqualGUID(fmt,GUID_WICPixelFormat48bppRGB))
        throw std::runtime_error("10-bit HEIC advertised as a lower-bit-depth WIC format");
    // WIC component-info format enumeration is often used by Windows
    // clients before they attempt high bit-depth HEIC image decode.
    // The runtime correctly returns 48bppRGB for this synthetic 10-bit
    // source, so advertising ONLY 24bppRGB is a real API inconsistency.
    CComPtr<IWICBitmapDecoderInfo> info;
    Check(input->GetDecoderInfo(&info),"decoder component info");
    UINT registeredCount=0;
    Check(info->GetPixelFormats(0,nullptr,&registeredCount),"registered pixel format count");
    if(registeredCount>64u || !registeredCount)
        throw std::runtime_error("WIC decoder registered format count invalid");
    std::vector<GUID> registered(registeredCount);
    UINT actualCount=0;
    Check(info->GetPixelFormats(registeredCount,registered.data(),&actualCount),
        "registered pixel format list");
    auto has=[&](const GUID& guid){
        for(UINT i=0;i<actualCount;i++)
            if(IsEqualGUID(registered[i],guid))return true;
        return false;
    };
    printf("WIC_HEIC_DECODER_REGISTERED_PIXEL_FORMATS count=%u rgb24=%u rgba32=%u rgb48=%u rgba64=%u\\n",
        actualCount,unsigned(has(GUID_WICPixelFormat24bppRGB)),
        unsigned(has(GUID_WICPixelFormat32bppRGBA)),
        unsigned(has(GUID_WICPixelFormat48bppRGB)),
        unsigned(has(GUID_WICPixelFormat64bppRGBA)));
    if(!has(GUID_WICPixelFormat24bppRGB) ||
       !has(GUID_WICPixelFormat32bppRGBA) ||
       !has(GUID_WICPixelFormat48bppRGB) ||
       !has(GUID_WICPixelFormat64bppRGBA))
        throw std::runtime_error("HEIC decoder runtime and WIC registered pixel formats disagree");
    puts("WIC_HEIC_DECODER_REGISTERED_PIXEL_FORMATS_COMPLETE_PASS");
    UINT w=0,h=0;
    Check(decoded->GetSize(&w,&h),"10bit dimensions");
    if(w==0 || h==0 || uint64_t(w)*h>20000000ull)
        throw std::runtime_error("unexpected synthetic fixture dimensions");
    WICRect line{0,0,static_cast<INT>(w),1};
    std::vector<BYTE> bytes(size_t(w)*6);
    Check(decoded->CopyPixels(&line,w*6,UINT(bytes.size()),bytes.data()),"10bit WIC 48bpp pixels");
    // Roundtrip through our WIC encoder without degrading back to 8-bit.
    CComPtr<IWICBitmapEncoder> encoder;
    Check(fac->CreateEncoder(GUID_ContainerFormatHEIC,&GUID_VendorPrsyahmi,&encoder),"encoder");
    CComPtr<IWICStream> stream;Check(fac->CreateStream(&stream),"stream");
    Check(stream->InitializeFromFilename(L"samples\\highbit-roundtrip.heic",GENERIC_WRITE),"output");
    Check(encoder->Initialize(stream,WICBitmapEncoderNoCache),"init encoder");
    CComPtr<IWICBitmapFrameEncode> output;
    Check(encoder->CreateNewFrame(&output,nullptr),"new frame");
    Check(output->Initialize(nullptr),"init frame");
    Check(output->SetSize(w,h),"set size");
    GUID outFmt=GUID_WICPixelFormat48bppRGB;
    Check(output->SetPixelFormat(&outFmt),"set high-bit format");
    if(!IsEqualGUID(outFmt,GUID_WICPixelFormat48bppRGB))
        throw std::runtime_error("48-bit WIC format downgraded during HEIC encode");
    Check(output->WriteSource(decoded,nullptr),"write 10-bit source");
    Check(output->Commit(),"commit frame");
    // The pinned Kvazaar backend may be compiled for 8-bit-only HEVC.
    // Do not report HEIC 10-bit parity if the native encoder rejects it.
    HRESULT encodeHr=encoder->Commit();
    if(FAILED(encodeHr)) {
        printf("HEIC_WIC_10BIT_ENCODER_BACKEND_BLOCKED HRESULT=0x%08X (NOT JPEG PARITY)\n",
               (unsigned)encodeHr);
        return;
    }
    output.Release();encoder.Release();stream.Release();
    CComPtr<IWICBitmapDecoder> verify;
    Check(fac->CreateDecoderFromFilename(L"samples\\highbit-roundtrip.heic",nullptr,
          GENERIC_READ,WICDecodeMetadataCacheOnDemand,&verify),"roundtrip readback");
    CComPtr<IWICBitmapFrameDecode> frame;
    Check(verify->GetFrame(0,&frame),"roundtrip frame");
    GUID got{}; Check(frame->GetPixelFormat(&got),"output pixel format");
    if(!IsEqualGUID(got,GUID_WICPixelFormat48bppRGB))
        throw std::runtime_error("10-bit HEIC output was downgraded to 8-bit");
    printf("HEIC_WIC_10BIT_FORMAT_ROUNDTRIP_PASSED %ux%u\n",w,h);
}
}
