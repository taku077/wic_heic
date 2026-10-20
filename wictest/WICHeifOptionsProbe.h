#pragma once
// WIC HEIF standard option negotiation tests. Lossless source transfer is not
// implemented yet: rejecting it is intentional and protects metadata.
namespace WICHeifOptionsProbe {
inline void H(HRESULT hr,const char* what) {
    if(FAILED(hr)) {
        printf("HEIF_OPTIONS_ERROR %s 0x%08X\n",what,(unsigned)hr);
        throw std::runtime_error(what);
    }
}
inline void Run() {
    CComPtr<IWICImagingFactory> fac;
    H(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory,(void**)&fac),"WIC factory");
    CComPtr<IWICBitmapEncoder> enc;
    H(fac->CreateEncoder(GUID_ContainerFormatHEIC,&GUID_VendorPrsyahmi,&enc),
        "create HEIC encoder");
    CComPtr<IWICStream> stream;
    H(fac->CreateStream(&stream),"create stream");
    H(stream->InitializeFromFilename(L"samples\\heif-options-test-output.heic",
        GENERIC_WRITE),"open isolated test output");
    H(enc->Initialize(stream,WICBitmapEncoderNoCache),"initialize encoder");
    CComPtr<IWICBitmapFrameEncode> frame;
    CComPtr<IPropertyBag2> bag;
    H(enc->CreateNewFrame(&frame,&bag),"create frame+options");
    if(!bag)throw std::runtime_error("HEIF option bag missing");
    ULONG count=0;
    H(bag->CountProperties(&count),"count options");
    if(count!=2)throw std::runtime_error("Unexpected HEIF option count");
    PROPBAG2 info[2]{};
    ULONG found=0;
    H(bag->GetPropertyInfo(0,2,info,&found),"GetPropertyInfo");
    bool transformFound=false,compressionFound=false;
    for(ULONG i=0;i<found;++i) {
        if(info[i].vt!=VT_UI1)throw std::runtime_error("Wrong option type");
        if(info[i].pstrName && wcscmp(info[i].pstrName,L"BitmapTransform")==0)
            transformFound=true;
        if(info[i].pstrName && wcscmp(info[i].pstrName,L"HeifCompressionMethod")==0)
            compressionFound=true;
        CoTaskMemFree(info[i].pstrName);
    }
    if(found!=2 || !transformFound || !compressionFound)
        throw std::runtime_error("Missing Microsoft HEIF option names");

    PROPBAG2 opts[2]{};
    opts[0].pstrName=const_cast<LPOLESTR>(L"BitmapTransform");
    opts[1].pstrName=const_cast<LPOLESTR>(L"HeifCompressionMethod");
    VARIANT values[2]{};
    values[0].vt=VT_UI1;values[0].bVal=1; // CW 90 degrees
    values[1].vt=VT_UI1;values[1].bVal=1; // WICHeifCompressionNone
    H(bag->Write(2,opts,values),"write rotation+no-recompression options");
    VARIANT stored[2]{};
    HRESULT errors[2]{};
    H(bag->Read(2,opts,nullptr,stored,errors),"read back options");
    if(FAILED(errors[0])||FAILED(errors[1])||stored[0].bVal!=1||
       stored[1].bVal!=1)
       throw std::runtime_error("HEIF option roundtrip mismatch");
    VariantClear(&stored[0]);VariantClear(&stored[1]);

    H(frame->Initialize(bag),"initialize frame with options");
    H(frame->SetSize(192,320),"size");
    GUID format=GUID_WICPixelFormat24bppRGB;
    H(frame->SetPixelFormat(&format),"pixel format");
    CComPtr<IWICBitmapFrameDecode> source;
    CComPtr<IWICBitmapDecoder> decoder;
    H(fac->CreateDecoderFromFilename(L"samples\\03.heic",nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"input decoder");
    H(decoder->GetFrame(0,&source),"input frame");
    HRESULT hr=frame->WriteSource(source,nullptr);
    printf("HEIF_OPTIONS_LOSSLESS_UNSUPPORTED_HRESULT=0x%08X\n",(unsigned)hr);
    if(hr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("Lossless requested but re-encoding was not blocked");
    hr=frame->Commit();
    if(hr!=WINCODEC_ERR_UNSUPPORTEDOPERATION)
        throw std::runtime_error("Lossless frame commit must fail closed");
    // Invalid option types cannot be silently accepted.
    VARIANT wrong{};wrong.vt=VT_R8;wrong.dblVal=12.0;
    hr=bag->Write(1,&opts[0],&wrong);
    if(hr!=DISP_E_TYPEMISMATCH)
        throw std::runtime_error("Invalid encoder option type accepted");
    printf("HEIF_WIC_OPTION_BAG_TEST_PASSED\n");
}
}
