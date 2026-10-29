#pragma once
// CI-ONLY prototype for IThumbnailProvider + IInitializeWithStream.
// NOT a registered COM server and NOT included in a production MSI.
// Proves that embedded HEVC thmb can become an opaque HBITMAP via native WIC
// without decoding its 12MP master image or writing source files.
#include <windows.h>
#include <wincodec.h>
#include <thumbcache.h>
#include <shobjidl.h>
#include <atlbase.h>
#include <atomic>
#include <new>
#include <memory>
#include <cstdio>
#include <stdexcept>
#include <string>
#include "../wic_heic/PhotosWicTrace.h"
namespace WICShellThumbnailProviderPilot {
class Provider final : public IInitializeWithStream, public IThumbnailProvider {
    std::atomic<ULONG> references_{1};
    CComPtr<IStream> input_;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** p) override {
        if(!p)return E_POINTER;
        *p=nullptr;
        if(riid==IID_IUnknown || riid==IID_IInitializeWithStream)
            *p=static_cast<IInitializeWithStream*>(this);
        else if(riid==IID_IThumbnailProvider)
            *p=static_cast<IThumbnailProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++references_;}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n=--references_;
        if(!n)delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IStream* stream,DWORD access) override {
        if(!stream)return E_POINTER;
        if(input_)return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        // A thumbnail provider is read-only even when Shell supplies flags.
        input_=stream;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetThumbnail(UINT requested,HBITMAP* output,
                                            WTS_ALPHATYPE* alpha) override {
        if(!output || !alpha)return E_POINTER;
        *output=nullptr;
        *alpha=WTSAT_UNKNOWN;
        if(!input_)return E_UNEXPECTED;
        if(requested==0 || requested>1024)return E_INVALIDARG;
        WicHeicDiagnostic::Emit("ShellLabProviderEnter",requested,0,0,0,"test-only");
        CComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr=CoCreateInstance(CLSID_HEICDecoder,nullptr,CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&decoder));
        if(FAILED(hr))return hr;
        LARGE_INTEGER zero{};
        hr=input_->Seek(zero,STREAM_SEEK_SET,nullptr);
        if(FAILED(hr))return hr;
        hr=decoder->Initialize(input_,WICDecodeMetadataCacheOnDemand);
        if(FAILED(hr))return hr;
        CComPtr<IWICBitmapSource> thumb;
        hr=decoder->GetThumbnail(&thumb);
        if(hr==WINCODEC_ERR_CODECNOTHUMBNAIL){
            CComPtr<IWICBitmapFrameDecode> frame;
            hr=decoder->GetFrame(0,&frame);
            if(FAILED(hr))return hr;
            hr=frame->GetThumbnail(&thumb);
        }
        // Absence must be explicit. Do NOT silently decompress 12MP here;
        // such fallback must be designed and benchmarked separately.
        if(FAILED(hr)||!thumb)return FAILED(hr)?hr:WINCODEC_ERR_CODECNOTHUMBNAIL;
        UINT width=0,height=0;
        hr=thumb->GetSize(&width,&height);
        if(FAILED(hr))return hr;
        if(!width||!height||width>16384||height>16384)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        // Never upscale an embedded 320x240 preview to 1024px. Production
        // fallback for high requested sizes will need a separate quality gate.
        CComPtr<IWICBitmapSource> render=thumb;
        if(width>requested || height>requested){
            const double ratio=double(requested)/double(max(width,height));
            const UINT scaledW=max(1u,UINT(width*ratio+0.5));
            const UINT scaledH=max(1u,UINT(height*ratio+0.5));
            CComPtr<IWICImagingFactory> factory;
            hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory));
            if(FAILED(hr))return hr;
            CComPtr<IWICBitmapScaler> scaler;
            hr=factory->CreateBitmapScaler(&scaler);
            if(FAILED(hr))return hr;
            hr=scaler->Initialize(render,scaledW,scaledH,WICBitmapInterpolationModeFant);
            if(FAILED(hr))return hr;
            render=scaler;
            width=scaledW;height=scaledH;
        }
        CComPtr<IWICImagingFactory> factory;
        hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&factory));
        if(FAILED(hr))return hr;
        CComPtr<IWICFormatConverter> rgba;
        hr=factory->CreateFormatConverter(&rgba);
        if(FAILED(hr))return hr;
        hr=rgba->Initialize(render,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,
                            nullptr,0,WICBitmapPaletteTypeCustom);
        if(FAILED(hr))return hr;
        if(width>UINT32_MAX/4u || height>UINT32_MAX/(width*4u))
            return E_INVALIDARG;
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth=LONG(width);
        bmi.bmiHeader.biHeight=-LONG(height);
        bmi.bmiHeader.biPlanes=1;
        bmi.bmiHeader.biBitCount=32;
        bmi.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;
        HBITMAP dib=CreateDIBSection(nullptr,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!dib||!bits)return HRESULT_FROM_WIN32(GetLastError());
        hr=rgba->CopyPixels(nullptr,width*4,width*height*4,static_cast<BYTE*>(bits));
        if(FAILED(hr)){DeleteObject(dib);return hr;}
        *output=dib;
        *alpha=WTSAT_RGB;
        return S_OK;
    }
};
inline void Need(bool okay,const char* description) {
    if(!okay)throw std::runtime_error(description);
}
inline void Run() {
    wchar_t temp[32768]{};
    const auto n=GetEnvironmentVariableW(L"RUNNER_TEMP",temp,32768);
    Need(n>0&&n<32768,"CI runner temp missing");
    wchar_t actions[16]{};
    Need(GetEnvironmentVariableW(L"GITHUB_ACTIONS",actions,16)==4 &&
         wcscmp(actions,L"true")==0,"Not allowed outside GitHub CI");
    const std::wstring path=std::wstring(temp,n)+L"\\wicfast_embedded_4032x3024.heic";
    Need(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES,"Missing embedded HEIC");
    CComPtr<IWICImagingFactory> factory;
    Need(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
         CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"No WIC factory");
    CComPtr<IWICStream> stream;
    Need(SUCCEEDED(factory->CreateStream(&stream)),"Create stream failed");
    Need(SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_READ)),
         "Open HEIC failed");
    CComPtr<IThumbnailProvider> thumbnail;
    {
        Provider* object=new Provider();
        thumbnail.Attach(object);
    }
    CComPtr<IInitializeWithStream> initializer;
    Need(SUCCEEDED(thumbnail->QueryInterface(IID_PPV_ARGS(&initializer))),
         "IInitializeWithStream not supported");
    Need(SUCCEEDED(initializer->Initialize(stream,STGM_READ)),"Stream initialization failed");
    HBITMAP bitmap=nullptr;
    WTS_ALPHATYPE alpha=WTSAT_UNKNOWN;
    const HRESULT hr=thumbnail->GetThumbnail(256,&bitmap,&alpha);
    Need(SUCCEEDED(hr)&&bitmap!=nullptr&&alpha==WTSAT_RGB,"Provider GetThumbnail failed");
    BITMAP info{};
    Need(GetObjectW(bitmap,sizeof(info),&info)==sizeof(info),"DIB object invalid");
    Need(info.bmWidth==256&&abs(info.bmHeight)==192&&info.bmBitsPixel==32,
         "Thumbnail 256x192 geometry/color mismatch");
    DIBSECTION sec{};
    Need(GetObjectW(bitmap,sizeof(sec),&sec)==sizeof(sec) &&
         sec.dsBm.bmBits!=nullptr,"DIB pixels missing");
    auto* px=static_cast<const BYTE*>(sec.dsBm.bmBits);
    const auto rgb=[&](int x,int y)->unsigned {
       const auto* a=px+(size_t(y)*256+x)*4;
       return (unsigned(a[2])<<16)|(unsigned(a[1])<<8)|unsigned(a[0]);
    };
    Need(rgb(32,32)!=rgb(224,32) && rgb(32,32)!=rgb(32,160),
         "Synthetic thumbnail quadrants collapsed");
    DeleteObject(bitmap);
    bitmap=nullptr;
    std::printf("CI_SHELL_THUMB_PROVIDER_RGB_PASS dims=256x192 opaque=1 "
                "using_embedded_HEVC=1 original_12MP_not_requested=1\n");
}
} // namespace
