#pragma once
// JPEG/HEIC WIC geometry A/B test. Does NOT interact with Windows Photos.
// Validates actual RGB pixels at asymmetric grid positions, not just width/height.
#include <vector>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace GeometryProbe {
static constexpr UINT SW=192, SH=320;
static const GUID& HEIC() { return GUID_ContainerFormatHEIC; }
static const GUID& JPEG() { return GUID_ContainerFormatJpeg; }

inline void Must(HRESULT hr,const char* step) {
    if(FAILED(hr)) {
        printf("GEOMETRY FAILED %s HRESULT=0x%08X\n",step,(unsigned)hr);
        throw std::runtime_error(step);
    }
}
inline void Save(IWICImagingFactory* factory, IWICBitmapSource* source,
                 const GUID& container, const GUID* vendor, const wchar_t* path) {
    CComPtr<IWICBitmapEncoder> encoder;
    CComPtr<IWICBitmapFrameEncode> frame;
    CComPtr<IWICStream> stream;
    Must(factory->CreateEncoder(container,vendor,&encoder),"CreateEncoder");
    Must(factory->CreateStream(&stream),"CreateStream");
    Must(stream->InitializeFromFilename(path,GENERIC_WRITE),"InitializeFromFilename");
    Must(encoder->Initialize(stream,WICBitmapEncoderNoCache),"encoder.Initialize");
    Must(encoder->CreateNewFrame(&frame,nullptr),"CreateNewFrame");
    Must(frame->Initialize(nullptr),"frame.Initialize");
    UINT width=0,height=0;
    Must(source->GetSize(&width,&height),"source.GetSize");
    Must(frame->SetSize(width,height),"frame.SetSize");
    Must(frame->SetResolution(96.0,96.0),"frame.SetResolution");
    GUID pixelFormat=GUID_WICPixelFormat24bppRGB;
    Must(frame->SetPixelFormat(&pixelFormat),"frame.SetPixelFormat");
    CComPtr<IWICBitmapSource> toWrite;
    if (IsEqualGUID(pixelFormat,GUID_WICPixelFormat24bppRGB)) {
        toWrite=source;
    } else {
        Must(WICConvertBitmapSource(pixelFormat,source,&toWrite),"ConvertBitmapSource for encoder");
    }
    Must(frame->WriteSource(toWrite,nullptr),"frame.WriteSource");
    Must(frame->Commit(),"frame.Commit");
    Must(encoder->Commit(),"encoder.Commit");
}

inline CComPtr<IWICBitmapFrameDecode> Read(IWICImagingFactory* f,const wchar_t* filename) {
    CComPtr<IWICBitmapDecoder> decoder;
    CComPtr<IWICBitmapFrameDecode> frame;
    Must(f->CreateDecoderFromFilename(filename,nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"CreateDecoderFromFilename");
    Must(decoder->GetFrame(0,&frame),"decoder.GetFrame");
    return frame;
}
struct Pixels {
    UINT w=0,h=0;
    std::vector<BYTE> rgb;
};
inline Pixels ToRGB(IWICBitmapSource* source) {
    Pixels px;
    Must(source->GetSize(&px.w,&px.h),"GetSize for pixel comparison");
    if(px.w>10000 || px.h>10000 || px.w==0 || px.h==0) throw std::runtime_error("bad geometry");
    CComPtr<IWICBitmapSource> converted;
    Must(WICConvertBitmapSource(GUID_WICPixelFormat24bppRGB,source,&converted),"ToRgb24");
    px.rgb.resize(size_t(px.w)*px.h*3);
    Must(converted->CopyPixels(nullptr,px.w*3,static_cast<UINT>(px.rgb.size()),px.rgb.data()),"CopyPixels RGB");
    return px;
}
inline double GridError(const Pixels& a,const Pixels& b) {
    if(a.w!=b.w || a.h!=b.h) {
        printf("GEOMETRY DIMENSION MISMATCH %ux%u vs %ux%u\n",a.w,a.h,b.w,b.h);
        throw std::runtime_error("geometry pixel dimension mismatch");
    }
    // The original 8x10 colored tile grid becomes a 10x8 grid when rotated.
    double sum=0.0;size_t count=0;
    for(unsigned row=0;row<8;row++)for(unsigned col=0;col<10;col++) {
        const UINT x=(col*32)+16, y=(row*24)+12;
        for(int dy=-2;dy<=2;dy++) for(int dx=-2;dx<=2;dx++) {
            size_t off=(size_t(y+dy)*a.w+(x+dx))*3;
            for(int channel=0;channel<3;channel++) {
                sum+=std::abs(int(a.rgb[off+channel])-int(b.rgb[off+channel]));
                ++count;
            }
        }
    }
    return sum/count;
}
inline void CheckSample(const char* label,const Pixels& ideal,const Pixels& actual) {
    double error=GridError(ideal,actual);
    printf("GEOMETRY %s size=%ux%u GRID_RGB_MAE=%.3f\n",label,actual.w,actual.h,error);
    if(error>36.0) throw std::runtime_error("geometry distortion above conservative threshold");
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Must(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory,(void**)&factory),"CoCreate WIC factory");
    std::vector<BYTE> input(size_t(SW)*SH*3);
    for(UINT y=0;y<SH;y++)for(UINT x=0;x<SW;x++) {
        const UINT cx=x/24, cy=y/32;
        size_t off=(size_t(y)*SW+x)*3;
        input[off+0]=BYTE((cx*51+cy*23+41)%256);
        input[off+1]=BYTE((cx*19+cy*63+77)%256);
        input[off+2]=BYTE((cx*79+cy*37+29)%256);
    }
    CComPtr<IWICBitmap> source;
    Must(factory->CreateBitmapFromMemory(SW,SH,GUID_WICPixelFormat24bppRGB,
        SW*3,static_cast<UINT>(input.size()),input.data(),&source),"CreateBitmapFromMemory");
    Save(factory,source,JPEG(),nullptr,L"samples\\geometry-original-jpeg.jpg");
    Save(factory,source,HEIC(),&GUID_VendorPrsyahmi,L"samples\\geometry-original-heic.heic");
    CComPtr<IWICBitmapFlipRotator> expectedRotation;
    Must(factory->CreateBitmapFlipRotator(&expectedRotation),"CreateBitmapFlipRotator");
    Must(expectedRotation->Initialize(source,WICBitmapTransformRotate90),"rotator.Initialize");
    Pixels reference=ToRGB(expectedRotation);
    if(reference.w!=SH || reference.h!=SW) throw std::runtime_error("rotation dimensions wrong");
    auto jpegSource=Read(factory,L"samples\\geometry-original-jpeg.jpg");
    auto heicSource=Read(factory,L"samples\\geometry-original-heic.heic");
    CComPtr<IWICBitmapFlipRotator> jRot,hRot;
    Must(factory->CreateBitmapFlipRotator(&jRot),"JPEG rotator");
    Must(factory->CreateBitmapFlipRotator(&hRot),"HEIC rotator");
    Must(jRot->Initialize(jpegSource,WICBitmapTransformRotate90),"JPEG rotate90");
    Must(hRot->Initialize(heicSource,WICBitmapTransformRotate90),"HEIC rotate90");
    Save(factory,jRot,JPEG(),nullptr,L"samples\\geometry-rotated-jpeg.jpg");
    // Pixel geometry reference only: deliberately materialize a synthetic
    // rotated WIC raster before HEIC encoding. A direct flip-rotator
    // WriteSource is rejected because it loses original HEIC provenance.
    // NEVER use this rasterization path to rotate a user's actual HEIC.
    CComPtr<IWICBitmap> syntheticRaster;
    Must(factory->CreateBitmapFromSource(hRot,WICBitmapCacheOnLoad,
        &syntheticRaster),"materialize synthetic reference raster");
    Save(factory,syntheticRaster,HEIC(),&GUID_VendorPrsyahmi,
        L"samples\\geometry-rotated-heic.heic");
    auto jOut=Read(factory,L"samples\\geometry-rotated-jpeg.jpg");
    auto hOut=Read(factory,L"samples\\geometry-rotated-heic.heic");
    Pixels jpegPixels=ToRGB(jOut),heicPixels=ToRGB(hOut);
    CheckSample("JPEG_ROTATED",reference,jpegPixels);
    CheckSample("HEIC_ROTATED",reference,heicPixels);
    printf("GEOMETRY JPEG_VS_HEIC_GRID_MAE=%.3f\n",GridError(jpegPixels,heicPixels));
    // A deliberately horizontally stretched raster (same nominal dimensions).
    // This MUST be rejected to prove the geometry test is not a size-only check.
    Pixels stretched=reference;
    for(UINT y=0;y<stretched.h;y++)for(UINT x=0;x<stretched.w;x++) {
        const UINT srcX=((stretched.w-1 < (x*3)/4) ? stretched.w-1 : (x*3)/4);
        for(int c=0;c<3;c++)stretched.rgb[(size_t(y)*stretched.w+x)*3+c]=
            reference.rgb[(size_t(y)*stretched.w+srcX)*3+c];
    }
    const double badError=GridError(reference,stretched);
    printf("GEOMETRY NEGATIVE_CONTROL_HORIZONTALLY_STRETCHED_GRID_MAE=%.3f\n",badError);
    if(badError<=36.0) throw std::runtime_error("geometry negative control failed to detect stretching");
    printf("GEOMETRY_PIXEL_AB_TEST_PASSED\n");
}
} // namespace GeometryProbe
