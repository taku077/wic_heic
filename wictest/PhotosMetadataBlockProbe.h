#pragma once
// Read-only HEIC frame metadata block contract. Public disposable synthetic
// Exif fixture only; no user photos, output files, registry, or MSI actions.
namespace PhotosMetadataBlockProbe {
inline void Must(HRESULT hr,const char* what) {
    if(FAILED(hr)){printf("WIC_FRAME_BLOCK_FAILURE %s 0x%08X\n",what,unsigned(hr));
        throw std::runtime_error(what);}
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Must(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory,reinterpret_cast<void**>(&factory)),"factory");
    CComPtr<IWICBitmapDecoder> decoder;
    Must(factory->CreateDecoderFromFilename(
        L"samples\\fixture-irot3-exif6-noaux.heic",
        nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),"synthetic HEIC");
    CComPtr<IWICBitmapFrameDecode> frame;
    Must(decoder->GetFrame(0,&frame),"first frame");
    CComPtr<IWICMetadataBlockReader> block;
    HRESULT hr=frame->QueryInterface(IID_IWICMetadataBlockReader,
        reinterpret_cast<void**>(&block));
    printf("WIC_HEIC_FRAME_METADATA_BLOCK_QI_HRESULT=0x%08X\n",unsigned(hr));
    if(FAILED(hr)||!block)
        throw std::runtime_error("WIC frame does not expose required metadata block reader");
    CComPtr<IUnknown> fromFrame,fromBlocks;
    Must(frame->QueryInterface(IID_IUnknown,
        reinterpret_cast<void**>(&fromFrame)),"frame IUnknown");
    Must(block->QueryInterface(IID_IUnknown,
        reinterpret_cast<void**>(&fromBlocks)),"metadata IUnknown");
    if(fromFrame.p!=fromBlocks.p)
        throw std::runtime_error("Metadata block frame violated COM IUnknown identity");
    GUID container{};
    Must(block->GetContainerFormat(&container),"metadata container");
    if(!IsEqualGUID(container,GUID_ContainerFormatHEIC))
        throw std::runtime_error("HEIC metadata frame claims a different container");
    UINT count=0;
    Must(block->GetCount(&count),"frame metadata count");
    if(!count||count>24u)throw std::runtime_error("Synthetic EXIF block count unexpected");
    for(UINT n=0;n<count;n++){
        CComPtr<IWICMetadataReader> reader;
        Must(block->GetReaderByIndex(n,&reader),"metadata indexed reader");
        GUID type{};
        Must(reader->GetMetadataFormat(&type),"metadata format");
    }
    CComPtr<IEnumUnknown> enumeration;
    Must(block->GetEnumerator(&enumeration),"frame metadata enumerator");
    UINT seen=0;
    for(;;) {
        CComPtr<IUnknown> item;
        ULONG fetched=0;
        hr=enumeration->Next(1,&item,&fetched);
        if(hr==S_FALSE && !fetched)break;
        if(hr!=S_OK||fetched!=1||!item)
            throw std::runtime_error("HEIC metadata enumeration failed");
        CComQIPtr<IWICMetadataReader> reader(item);
        if(!reader)throw std::runtime_error("WIC enum item is not a metadata reader");
        if(++seen>count)throw std::runtime_error("Metadata enum overread");
    }
    if(seen!=count)throw std::runtime_error("Metadata enumeration count mismatch");
    CComPtr<IWICMetadataQueryReader> query;
    Must(frame->GetMetadataQueryReader(&query),"frame EXIF query");
    PROPVARIANT orient;PropVariantInit(&orient);
    hr=query->GetMetadataByName(L"/app1/ifd/{ushort=274}",&orient);
    if(FAILED(hr)){PropVariantClear(&orient);
        throw std::runtime_error("Frame EXIF Orientation query regressed");}
    PropVariantClear(&orient);
    puts("WIC_HEIC_FRAME_BLOCK_READER_COM_IDENTITY_AND_EXIF_PASS");
}
}
