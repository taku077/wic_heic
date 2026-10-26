#pragma once
// WIC pixel resize/rotate MATRIX using checked-in PUBLIC synthetic HEIC only.
// These are explicit NEW single-frame raster EXPORTS, not an in-place HEIF edit.
// Lossy resize is NOT equivalent to metadata-only rotation and may discard EXIF.
// The original file is opened READ-ONLY; each output path is NEW and unique.
#include <string>
#include <vector>
#include <stdexcept>
#include <cmath>
#include <cstdio>

namespace WICPhotosScaleMatrixProbe {
inline void Require(HRESULT hr,const char* label) {
    if(FAILED(hr)) {
        printf("WIC_SCALE_MATRIX_FAILURE=%s HRESULT=0x%08X\n",
               label,(unsigned)hr);
        throw std::runtime_error(label);
    }
}
inline std::wstring NewFolder() {
    const std::wstring root=L"samples\\synthetic_scale_matrix_"+
        std::to_wstring(GetCurrentProcessId())+L"_"+
        std::to_wstring(GetTickCount64());
    if(!CreateDirectoryW(root.c_str(),nullptr))
        throw std::runtime_error("Scale matrix: could not make fresh folder");
    return root;
}
inline void CheckOutput(IWICImagingFactory* factory,const wchar_t* output,
                        IWICBitmapSource* reference,UINT w,UINT h) {
    CComPtr<IWICBitmapDecoder> decoder;
    Require(factory->CreateDecoderFromFilename(output,nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"resize output readback");
    UINT count=0;
    Require(decoder->GetFrameCount(&count),"resize output frame count");
    if(count!=1)throw std::runtime_error("Raster export produced unexpected frame count");
    CComPtr<IWICBitmapFrameDecode> frame;
    Require(decoder->GetFrame(0,&frame),"resize output WIC primary");
    UINT ow=0,oh=0;
    Require(frame->GetSize(&ow,&oh),"resize output WIC geometry");
    if(ow!=w || oh!=h)throw std::runtime_error("RESIZE_STRETCH_OR_WRONG_GEOMETRY");
    CComPtr<IWICBitmapSource> refRgb,outRgb;
    Require(WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,
            reference,&refRgb),"resize reference RGB");
    Require(WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,
            frame,&outRgb),"resize decoded RGB");
    const size_t size=size_t(w)*h*3;
    if(!w||!h||size>UINT_MAX)throw std::runtime_error("Invalid matrix RGB size");
    std::vector<BYTE> before(size),after(size);
    Require(refRgb->CopyPixels(nullptr,w*3,UINT(size),before.data()),
            "resize reference pixels");
    Require(outRgb->CopyPixels(nullptr,w*3,UINT(size),after.data()),
            "resize saved pixels");
    double mse=0;
    for(size_t i=0;i<size;i++) {
        const int diff=int(before[i])-int(after[i]);
        mse+=double(diff)*diff;
    }
    mse/=size;
    const double psnr=mse==0?99.0:10.0*std::log10((255.0*255.0)/mse);
    printf("WIC_SCALE_MATRIX_PSNR=%0.2f dims=%ux%u\n",psnr,w,h);
    // A lossy HEVC re-encode cannot preserve exact RGB bytes. This checks
    // catastrophic mismatch or shifted/stretched content; NOT losslessness.
    if(psnr<25.0)throw std::runtime_error("Resize/rotate pixel correspondence too poor");
}
inline void RunCase(IWICImagingFactory* factory,IWICBitmapFrameDecode* source,
                    const std::wstring& root,int index,bool scaleFirst,
                    UINT reducedW,UINT reducedH,
                    WICBitmapInterpolationMode filter,
                    WICBitmapTransformOptions direction) {
    CComPtr<IWICBitmapSource> transformed;
    if(scaleFirst) {
        CComPtr<IWICBitmapScaler> scaler;
        Require(factory->CreateBitmapScaler(&scaler),"scale-first create");
        Require(scaler->Initialize(source,reducedW,reducedH,filter),
                "scale-first initialize");
        CComPtr<IWICBitmapFlipRotator> rotator;
        Require(factory->CreateBitmapFlipRotator(&rotator),
                "scale-first rotator create");
        Require(rotator->Initialize(scaler,direction),"scale-first rotate");
        transformed=rotator;
    } else {
        CComPtr<IWICBitmapFlipRotator> rotator;
        Require(factory->CreateBitmapFlipRotator(&rotator),
                "rotate-first rotator create");
        Require(rotator->Initialize(source,direction),"rotate-first initialize");
        CComPtr<IWICBitmapScaler> scaler;
        Require(factory->CreateBitmapScaler(&scaler),"rotate-first scaler");
        Require(scaler->Initialize(rotator,reducedW,reducedH,filter),
                "rotate-first initialize scaling");
        transformed=scaler;
    }
    UINT w=0,h=0;
    Require(transformed->GetSize(&w,&h),"matrix source geometry");
    const bool quarter=(direction==WICBitmapTransformRotate90 ||
                        direction==WICBitmapTransformRotate270);
    const UINT expectedW=scaleFirst&&quarter?reducedH:reducedW;
    const UINT expectedH=scaleFirst&&quarter?reducedW:reducedH;
    if(w!=expectedW || h!=expectedH)
        throw std::runtime_error("Matrix WIC transform source geometry mismatch");
    CComPtr<IWICBitmapSource> rgb;
    Require(WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,
            transformed,&rgb),"matrix convert to 24bpp RGB");
    CComPtr<IWICBitmap> opaque;
    Require(factory->CreateBitmapFromSource(rgb,WICBitmapCacheOnLoad,&opaque),
            "matrix cache transformed pixels");
    const std::wstring path=root+L"\\case_"+std::to_wstring(index)+L".heic";
    if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)
        throw std::runtime_error("Matrix refused existing destination");
    CComPtr<IWICBitmapEncoder> encoder;
    Require(factory->CreateEncoder(GUID_ContainerFormatHEIC,
            &GUID_VendorPrsyahmi,&encoder),"matrix export encoder");
    CComPtr<IWICStream> stream;
    Require(factory->CreateStream(&stream),"matrix output stream");
    Require(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),
            "matrix output create NEW file");
    Require(encoder->Initialize(stream,WICBitmapEncoderNoCache),
            "matrix encoder init");
    CComPtr<IWICBitmapFrameEncode> destination;
    Require(encoder->CreateNewFrame(&destination,nullptr),
            "matrix export create frame");
    Require(destination->Initialize(nullptr),"matrix export init frame");
    Require(destination->SetSize(w,h),"matrix export SetSize");
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppRGB;
    Require(destination->SetPixelFormat(&format),"matrix export SetPixelFormat");
    if(!IsEqualGUID(format,GUID_WICPixelFormat24bppRGB))
        throw std::runtime_error("Matrix export unexpected format");
    Require(destination->WriteSource(opaque,nullptr),"matrix export WriteSource");
    Require(destination->Commit(),"matrix export frame Commit");
    Require(encoder->Commit(),"matrix export encoder Commit");
    destination.Release();encoder.Release();stream.Release();
    CheckOutput(factory,path.c_str(),opaque,w,h);
    printf("WIC_SCALE_MATRIX_CASE_%d_PASS scaleFirst=%d w=%u h=%u\n",
           index,scaleFirst?1:0,w,h);
}
inline void RequireMultiFrameResizeFailsClosed(IWICImagingFactory* factory,
        IWICBitmapDecoder* decoder,const std::wstring& root) {
    CComPtr<IWICBitmapFrameDecode> first,second;
    Require(decoder->GetFrame(0,&first),"fail-close source first");
    Require(decoder->GetFrame(1,&second),"fail-close source second");
    CComPtr<IWICBitmapScaler> scaler;
    Require(factory->CreateBitmapScaler(&scaler),"fail-close scaler");
    Require(scaler->Initialize(first,540,960,WICBitmapInterpolationModeFant),
            "fail-close shrink");
    CComPtr<IWICBitmapFlipRotator> rotated;
    Require(factory->CreateBitmapFlipRotator(&rotated),"fail-close rotator");
    Require(rotated->Initialize(scaler,WICBitmapTransformRotate90),
            "fail-close rotate");
    CComPtr<IWICBitmap> opaque;
    Require(factory->CreateBitmapFromSource(rotated,WICBitmapCacheOnLoad,&opaque),
            "fail-close opaque");
    UINT auxW=0,auxH=0;
    Require(second->GetSize(&auxW,&auxH),"fail-close aux size");
    const std::wstring path=root+L"\\unsafe-inplace-scaled-primary.heic";
    if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)
        throw std::runtime_error("Fail-close existing destination");
    CComPtr<IWICBitmapEncoder> encoder;
    Require(factory->CreateEncoder(GUID_ContainerFormatHEIC,
            &GUID_VendorPrsyahmi,&encoder),"fail-close encoder");
    CComPtr<IWICStream> stream;
    Require(factory->CreateStream(&stream),"fail-close stream");
    Require(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),
            "fail-close empty file");
    Require(encoder->Initialize(stream,WICBitmapEncoderNoCache),
            "fail-close encoder Initialize");
    for(int index=0;index<2;index++){
        CComPtr<IWICBitmapFrameEncode> frame;
        Require(encoder->CreateNewFrame(&frame,nullptr),"fail-close frame");
        Require(frame->Initialize(nullptr),"fail-close frame init");
        Require(frame->SetSize(index==0?960:auxW,index==0?540:auxH),
                "fail-close SetSize");
        WICPixelFormatGUID fmt=GUID_WICPixelFormat24bppRGB;
        Require(frame->SetPixelFormat(&fmt),"fail-close SetPixelFormat");
        Require(frame->WriteSource(index==0?
                static_cast<IWICBitmapSource*>(opaque.p):
                static_cast<IWICBitmapSource*>(second.p),nullptr),
                "fail-close WriteSource");
        Require(frame->Commit(),"fail-close frame Commit");
    }
    const HRESULT hr=encoder->Commit();
    if(SUCCEEDED(hr))
        throw std::runtime_error("DANGEROUS: scaled multi-frame edit silently discarded original HEIF metadata");
    encoder.Release();stream.Release();
    const auto bytes=WICPhotosOpaqueRotationProbe::Read(path.c_str());
    if(!bytes.empty())
        throw std::runtime_error("Fail-close scaled multi-frame save emitted partial output");
    printf("WIC_SCALE_ROTATE_MULTI_FRAME_INPLACE_SAFE_REJECT_PASS hr=0x%08X\n",
           unsigned(hr));
}
inline void Run(){
    CComPtr<IWICImagingFactory> factory;
    Require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
            CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"matrix WIC factory");
    CComPtr<IWICBitmapDecoder> decoder;
    Require(factory->CreateDecoderFromFilename(
            L"samples\\fixture-irot3-exif6-noaux.heic",nullptr,
            GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
            "matrix public synthetic HEIC");
    UINT count=0;
    Require(decoder->GetFrameCount(&count),"matrix count");
    if(count!=2) throw std::runtime_error("Matrix fixture must have two top-level images");
    CComPtr<IWICBitmapFrameDecode> source;
    Require(decoder->GetFrame(0,&source),"matrix primary");
    UINT w=0,h=0;
    Require(source->GetSize(&w,&h),"matrix source dims");
    if(w!=1080 || h!=1920)
        throw std::runtime_error("Matrix source geometry has changed");
    const std::wstring root=NewFolder();
    RunCase(factory,source,root,1,true,540,960,
        WICBitmapInterpolationModeFant,WICBitmapTransformRotate90);
    RunCase(factory,source,root,2,false,960,540,
        WICBitmapInterpolationModeFant,WICBitmapTransformRotate90);
    RunCase(factory,source,root,3,true,540,960,
        WICBitmapInterpolationModeLinear,WICBitmapTransformRotate270);
    RunCase(factory,source,root,4,false,960,540,
        WICBitmapInterpolationModeLinear,WICBitmapTransformRotate270);
    RunCase(factory,source,root,5,true,541,959,
        WICBitmapInterpolationModeFant,WICBitmapTransformRotate90);
    RunCase(factory,source,root,6,false,959,541,
        WICBitmapInterpolationModeFant,WICBitmapTransformRotate90);
    RequireMultiFrameResizeFailsClosed(factory,decoder,root);
    printf("WIC_SCALE_ROTATE_SIX_CASE_MATRIX_PASS\n");
}
} // namespace WICPhotosScaleMatrixProbe
