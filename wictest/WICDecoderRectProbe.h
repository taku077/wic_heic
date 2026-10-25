#pragma once
// WIC pixel-memory bounds regression using checked-in public synthetic HEIC.
// Test real irot/grid geometry, region copies, and malformed caller WICRects.
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <cstring>

namespace WICDecoderRectProbe {
inline void Check(HRESULT hr,const char* step){
    if(FAILED(hr)){
        printf("WIC_RECT_FAILURE %s HRESULT=0x%08X\n",step,unsigned(hr));
        throw std::runtime_error(step);
    }
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
          CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"factory");
    CComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromFilename(
          L"samples\\fixture-irot3-exif6-noaux.heic",
          nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),
          "open synthetic rotation source");
    CComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0,&frame),"get rotated frame");
    UINT w=0,h=0;
    Check(frame->GetSize(&w,&h),"rotated dimensions");
    if(w!=1080 || h!=1920)
        throw std::runtime_error("Unexpected WIC test fixture geometry");
    WICPixelFormatGUID format{};
    Check(frame->GetPixelFormat(&format),"format");
    if(!IsEqualGUID(format,GUID_WICPixelFormat24bppRGB))
        throw std::runtime_error("WIC decoder test expects 24bpp RGB");

    const UINT pitch=w*3;
    std::vector<BYTE> all(size_t(pitch)*h);
    Check(frame->CopyPixels(nullptr,pitch,UINT(all.size()),all.data()),
          "full-frame read");

    WICRect r{17,23,51,29};
    const UINT rStride=r.Width*3+7;
    std::vector<BYTE> crop(size_t(rStride)*r.Height,0xA7);
    Check(frame->CopyPixels(&r,rStride,UINT(crop.size()),crop.data()),
          "valid rectangle copy");
    for(int y=0;y<r.Height;y++) {
        const BYTE* want=all.data()+size_t(r.Y+y)*pitch+size_t(r.X)*3;
        const BYTE* got=crop.data()+size_t(y)*rStride;
        if(memcmp(want,got,size_t(r.Width)*3)!=0)
            throw std::runtime_error("WIC rotated region does not match full image");
        for(UINT i=UINT(r.Width*3);i<rStride;i++)
            if(got[i]!=0xA7)
                throw std::runtime_error("WIC wrote into destination row padding");
    }
    // Caller-supplied geometry MUST be rejected rather than reading memory
    // before/after the plane. Input buffers are deliberately tiny.
    struct Case {WICRect rect;UINT stride;UINT size;};
    const std::vector<Case> bad={
        {{-1,0,8,8},24,192},
        {{0,-1,8,8},24,192},
        {{1079,0,2,1},6,6},
        {{0,1919,1,2},3,6},
        {{0,0,0,1},3,3},
        {{0,0,1,0},3,3},
        {{INT_MAX,0,INT_MAX,1},3,3},
        {{0,0,8,8},23,192}, // row stride too short
        {{0,0,8,8},24,191}, // dest one byte too short
        {{0,0,1,1},0,3}
    };
    for(const auto& one:bad){
        std::vector<BYTE> result(one.size?one.size:1,0xC6);
        HRESULT hr=frame->CopyPixels(&one.rect,one.stride,one.size,result.data());
        if(SUCCEEDED(hr))
            throw std::runtime_error("WIC accepted unsafe rectangle/stride");
        for(BYTE b:result)if(b!=0xC6)
            throw std::runtime_error("Unsafe WIC call modified destination");
    }
    // WIC permits tight allocation of the LAST row (no padding required).
    const WICRect edge{w?int(w-1):0,h?int(h-1):0,1,1};
    BYTE last[3]{};
    Check(frame->CopyPixels(&edge,3,3,last),"bottom-right pixel");
    const BYTE* expected=all.data()+size_t(h-1)*pitch+size_t(w-1)*3;
    if(memcmp(last,expected,3)!=0)
        throw std::runtime_error("Bottom-right pixel mismatch");
    printf("WIC_DECODER_ROTATED_RECT_BOUNDS_AND_STRIDE_PASS\n");
}
} // namespace WICDecoderRectProbe
