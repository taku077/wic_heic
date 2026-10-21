// CI-only: compare a real Shell IThumbnailCache bitmap against a separately
// rendered WIC baseline. Never registers handlers or opens personal photos.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <thumbcache.h>
#include <shobjidl.h>
#include <shlobj_core.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "../../wic_heic/wic_heic.h"

using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT kSize=256;
const CLSID kCacheClsid={0x50ef4544,0xac9f,0x4a8e,{0xb2,0x1b,0x8a,0x26,0x18,0x0d,0xb1,0x3f}};

void Checked(HRESULT hr,const char* stage) {
    if(FAILED(hr)) {
        std::printf("SHELL_PIXEL_ERROR stage=%s hr=0x%08X\n",stage,UINT(hr));
        throw std::runtime_error(stage);
    }
}
void Required(bool ok,const char* stage) {
    if(!ok) {
        std::printf("SHELL_PIXEL_ERROR stage=%s\n",stage);
        throw std::runtime_error(stage);
    }
}
struct CoScope {
    CoScope(){Checked(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"CoInitializeEx");}
    ~CoScope(){CoUninitialize();}
};
std::wstring Env(const wchar_t* name) {
    DWORD n=GetEnvironmentVariableW(name,nullptr,0);
    if(!n || n>32767)return {};
    std::wstring value(n,L'\0');
    DWORD got=GetEnvironmentVariableW(name,&value[0],n);
    if(!got || got>=n)return {};
    value.resize(got);
    return value;
}
std::wstring FullPath(const std::wstring& value) {
    DWORD n=GetFullPathNameW(value.c_str(),0,nullptr,nullptr);
    Required(n>0 && n<32767,"bad path");
    std::wstring result(n,L'\0');
    DWORD got=GetFullPathNameW(value.c_str(),n,&result[0],nullptr);
    Required(got>0 && got<n,"full path failed");
    result.resize(got);
    while(!result.empty() && (result.back()==L'\\' || result.back()==L'/'))result.pop_back();
    return result;
}
void GuardCiFolder(const std::wstring& folder) {
    Required(Env(L"GITHUB_ACTIONS")==L"true","CI-only guard");
    const auto root=FullPath(Env(L"RUNNER_TEMP"));
    const auto target=FullPath(folder);
    Required(!root.empty() && target.size()>root.size()+16,"fixture path too short");
    Required(_wcsnicmp(target.c_str(),root.c_str(),root.size())==0 &&
             target[root.size()]==L'\\',"not in runner temp");
    auto leaf=target.substr(root.size()+1);
    Required(leaf.rfind(L"wic_shell_trial_",0)==0 &&
             leaf.find_first_of(L"\\/")==std::wstring::npos,
             "unexpected synthetic directory");
    DWORD attributes=GetFileAttributesW(target.c_str());
    Required(attributes!=INVALID_FILE_ATTRIBUTES &&
             (attributes&FILE_ATTRIBUTE_DIRECTORY)!=0 &&
             (attributes&FILE_ATTRIBUTE_REPARSE_POINT)==0,
             "invalid synthetic folder");
}
struct Pixels {
    UINT width=0,height=0;
    std::vector<BYTE> bytes;
};
Pixels WicReference(const std::wstring& filename,IWICImagingFactory* factory) {
    ComPtr<IWICStream> stream;
    Checked(factory->CreateStream(stream.GetAddressOf()),"wic stream");
    Checked(stream->InitializeFromFilename(filename.c_str(),GENERIC_READ),"wic stream open");
    // An explicit format and vendor, as in the synthetic provider. The
    // independent WIC render below has its own decoder/scaler/converter.
    ComPtr<IWICBitmapDecoder> decoder;
    Checked(factory->CreateDecoder(GUID_ContainerFormatHEIC,
        &GUID_VendorPrsyahmi,decoder.GetAddressOf()),"wic decoder");
    Checked(decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand),
            "wic decoder initialize");
    ComPtr<IWICBitmapFrameDecode> frame;
    Checked(decoder->GetFrame(0,frame.GetAddressOf()),"wic frame");
    UINT width=0,height=0;
    Checked(frame->GetSize(&width,&height),"wic dimensions");
    Required(width>0 && height>0 && width<=10000 && height<=10000,"wic bounds");
    const UINT maxSide=(std::max)(width,height);
    Pixels pixels;
    pixels.width=(std::max)(1u,UINT(uint64_t(width)*kSize/maxSide));
    pixels.height=(std::max)(1u,UINT(uint64_t(height)*kSize/maxSide));
    const auto count=uint64_t(pixels.width)*pixels.height*4;
    Required(count<=16*1024*1024 && count<=UINT_MAX,"wic output too large");
    ComPtr<IWICBitmapScaler> scaler;
    Checked(factory->CreateBitmapScaler(scaler.GetAddressOf()),"wic scaler");
    Checked(scaler->Initialize(frame.Get(),pixels.width,pixels.height,
        WICBitmapInterpolationModeFant),"wic scale");
    ComPtr<IWICFormatConverter> converter;
    Checked(factory->CreateFormatConverter(converter.GetAddressOf()),"wic converter");
    Checked(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),
        "wic conversion");
    pixels.bytes.resize(size_t(count));
    Checked(converter->CopyPixels(nullptr,pixels.width*4u,UINT(count),
            pixels.bytes.data()),"wic CopyPixels");
    return pixels;
}
Pixels ShellCached(const std::wstring& filename,IThumbnailCache* cache) {
    ComPtr<IShellItem> item;
    Checked(SHCreateItemFromParsingName(filename.c_str(),nullptr,
            IID_PPV_ARGS(item.GetAddressOf())),"shell item");
    ComPtr<ISharedBitmap> shared;
    WTS_CACHEFLAGS flags{};
    WTS_THUMBNAILID id{};
    Checked(cache->GetThumbnail(item.Get(),kSize,WTS_INCACHEONLY,
            shared.GetAddressOf(),&flags,&id),"cache-only thumbnail");
    Required(shared!=nullptr,"no Shell bitmap");
    HBITMAP bitmap=nullptr;
    Checked(shared->GetSharedBitmap(&bitmap),"shared HBITMAP");
    Required(bitmap!=nullptr,"null HBITMAP");
    BITMAP bm{};
    Required(GetObjectW(bitmap,sizeof(bm),&bm)==sizeof(bm),"bitmap metadata");
    Required(bm.bmWidth>0 && bm.bmHeight>0 && bm.bmWidth<=1024 &&
            bm.bmHeight<=1024,"Shell bitmap bounds");
    Pixels pixels;
    pixels.width=UINT(bm.bmWidth);
    pixels.height=UINT(bm.bmHeight);
    pixels.bytes.resize(size_t(pixels.width)*pixels.height*4u);

    // Require top-down pixel ordering, 32bpp BGRA. A DIB section gives us
    // its true alpha bytes without GDI converting or discarding alpha.
    DIBSECTION dib{};
    Required(GetObjectW(bitmap,sizeof(dib),&dib)==sizeof(dib) &&
             dib.dsBm.bmBits && dib.dsBm.bmBitsPixel==32,
             "Shell bitmap is not a readable 32bpp DIB");
    Required(dib.dsBm.bmWidthBytes>=LONG(pixels.width*4u),
             "Shell bitmap stride");
    const BYTE* source=static_cast<const BYTE*>(dib.dsBm.bmBits);
    const size_t stride=size_t(dib.dsBm.bmWidthBytes);
    for(UINT y=0;y<pixels.height;++y) {
        const size_t srcRow=(dib.dsBmih.biHeight<0)?y:(pixels.height-1-y);
        std::memcpy(pixels.bytes.data()+size_t(y)*pixels.width*4,
                    source+srcRow*stride,size_t(pixels.width)*4u);
    }
    return pixels;
}
} // namespace

int wmain(int argc,wchar_t** argv) {
    try {
        Required(argc==2,"usage: ShellPixelParity.exe <CI synthetic work folder>");
        const std::wstring root=FullPath(argv[1]);
        GuardCiFolder(root);
        CoScope scope;
        ComPtr<IWICImagingFactory> factory;
        Checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(factory.GetAddressOf())),"wic factory");
        ComPtr<IThumbnailCache> cache;
        Checked(CoCreateInstance(kCacheClsid,nullptr,CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(cache.GetAddressOf())),"Shell cache");
        uint64_t compared=0;
        UINT parityFailures=0;
        for(int i=0;i<10;++i) {
            wchar_t filename[64]{};
            swprintf_s(filename,L"synthetic_%02d.wictrial",i);
            const std::wstring path=root+L"\\"+filename;
            Required(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES,
                     "missing synthetic file");
            Pixels shell=ShellCached(path,cache.Get());
            Pixels reference=WicReference(path,factory.Get());
            if(shell.width!=reference.width || shell.height!=reference.height) {
                std::printf("SHELL_PIXEL_DIMENSIONS_MISMATCH index=%d shell=%ux%u wic=%ux%u\n",
                    i,shell.width,shell.height,reference.width,reference.height);
                throw std::runtime_error("Shell dimensions do not match WIC");
            }
            const auto mismatch=std::mismatch(shell.bytes.begin(),shell.bytes.end(),
                                               reference.bytes.begin());
            if(mismatch.first!=shell.bytes.end()) {
                const auto at=size_t(mismatch.first-shell.bytes.begin());
                // Diagnose bitmap row order / Shell transform without logging any
                // synthetic image content. Count full BGRA pixels only.
                uint64_t equalDirect=0,equalH=0,equalV=0,equalHV=0,equalRB=0;
                uint64_t diffDirect=0,diffH=0,diffV=0,diffHV=0,diffRB=0;
                uint64_t alphaMismatch=0;
                for(UINT y=0;y<shell.height;++y) for(UINT x=0;x<shell.width;++x) {
                    const auto offset=(size_t(y)*shell.width+x)*4;
                    auto equals=[&](UINT sx,UINT sy) {
                        const auto base=(size_t(sy)*shell.width+sx)*4;
                        return std::memcmp(shell.bytes.data()+base,
                                reference.bytes.data()+offset,4)==0;
                    };
                    equalDirect+=equals(x,y);
                    equalH+=equals(shell.width-1-x,y);
                    equalV+=equals(x,shell.height-1-y);
                    equalHV+=equals(shell.width-1-x,shell.height-1-y);
                    const BYTE* p=shell.bytes.data()+offset;
                    const BYTE* q=reference.bytes.data()+offset;
                    equalRB+=(p[0]==q[2] && p[1]==q[1] &&
                              p[2]==q[0] && p[3]==q[3]);
                    auto delta=[](BYTE a,BYTE b)->uint64_t {
                        return (a>b)?uint64_t(a-b):uint64_t(b-a);
                    };
                    auto measure=[&](UINT sx,UINT sy)->uint64_t {
                        const BYTE* c=shell.bytes.data()+
                           (size_t(sy)*shell.width+sx)*4;
                        return delta(c[0],q[0])+delta(c[1],q[1])+delta(c[2],q[2]);
                    };
                    diffDirect+=measure(x,y);
                    diffH+=measure(shell.width-1-x,y);
                    diffV+=measure(x,shell.height-1-y);
                    diffHV+=measure(shell.width-1-x,shell.height-1-y);
                    diffRB+=delta(p[0],q[2])+delta(p[1],q[1])+delta(p[2],q[0]);
                    alphaMismatch+=(p[3]!=q[3]);
                }
                std::printf("SHELL_PIXEL_DIAGNOSTIC index=%d total=%llu same=%llu hflip=%llu vflip=%llu rotated180=%llu swap_rb=%llu\n",
                    i,static_cast<unsigned long long>(shell.width)*shell.height,
                    static_cast<unsigned long long>(equalDirect),
                    static_cast<unsigned long long>(equalH),
                    static_cast<unsigned long long>(equalV),
                    static_cast<unsigned long long>(equalHV),
                    static_cast<unsigned long long>(equalRB));
                const uint64_t denom=uint64_t(shell.width)*shell.height*3u;
                std::printf("SHELL_PIXEL_MAE_X1000 index=%d direct=%llu hflip=%llu vflip=%llu rotate180=%llu swap_rb=%llu alpha_mismatch=%llu\n",
                    i,static_cast<unsigned long long>(diffDirect*1000/denom),
                    static_cast<unsigned long long>(diffH*1000/denom),
                    static_cast<unsigned long long>(diffV*1000/denom),
                    static_cast<unsigned long long>(diffHV*1000/denom),
                    static_cast<unsigned long long>(diffRB*1000/denom),
                    static_cast<unsigned long long>(alphaMismatch));
                std::printf("SHELL_PIXEL_BYTES_MISMATCH index=%d byte=%zu shell=%u wic=%u\n",
                   i,at,unsigned(*mismatch.first),unsigned(*mismatch.second));
                ++parityFailures;
                continue;
            }
            compared+=shell.bytes.size();
            std::printf("SHELL_PIXEL_PARITY_FILE_PASS index=%d pixels=%u dimensions=%ux%u\n",
                i,shell.width*shell.height,shell.width,shell.height);
        }
        if(parityFailures) {
            std::printf("SHELL_PIXEL_PARITY_SUMMARY cases=10 exact=%u mismatch=%u compared_exact_bytes=%llu\n",
                10u-parityFailures,parityFailures,
                static_cast<unsigned long long>(compared));
            return 1;
        }
        std::printf("SHELL_PIXEL_PARITY_PASS cases=10 compared_bytes=%llu\n",
                    static_cast<unsigned long long>(compared));
        return 0;
    } catch(const std::exception& e) {
        std::printf("SHELL_PIXEL_PARITY_FAILED stage=%s\n",e.what());
        return 1;
    }
}