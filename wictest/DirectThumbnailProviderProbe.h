#pragma once
// EXPERIMENT ONLY: in-process IThumbnailProvider + IInitializeWithStream.
// Never registered as a Shell extension, never changes thumbnail associations.
// The provider receives a stream and uses the known HEIC WIC CLSID directly,
// bypassing WIC's slow automatic decoder enumeration. Synthetic files only.
#include <thumbcache.h>
#include <shobjidl.h>
#include <atomic>
#include <chrono>
#include <vector>
#include <limits>
#pragma comment(lib,"gdi32.lib")

namespace DirectThumbnailProviderProbe {
using Clock=std::chrono::steady_clock;
inline double Ms(Clock::time_point start,Clock::time_point end) {
    return std::chrono::duration<double,std::milli>(end-start).count();
}
inline void Checked(HRESULT hr,const char* where) {
    if(FAILED(hr)) {
        printf("THUMB_PROVIDER_ERROR stage=%s hresult=0x%08X\n",where,unsigned(hr));
        throw std::runtime_error(where);
    }
}
class DirectProvider final : public IInitializeWithStream, public IThumbnailProvider {
    std::atomic<ULONG> refs_{1};
    CComPtr<IStream> stream_;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,void** out) override {
        if(!out) return E_POINTER;
        *out=nullptr;
        if(IsEqualIID(riid,IID_IUnknown) || IsEqualIID(riid,__uuidof(IInitializeWithStream)))
            *out=static_cast<IInitializeWithStream*>(this);
        else if(IsEqualIID(riid,__uuidof(IThumbnailProvider)))
            *out=static_cast<IThumbnailProvider*>(this);
        if(!*out)return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count=--refs_;
        if(!count)delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IStream* input,DWORD) override {
        if(!input)return E_INVALIDARG;
        if(stream_)return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        stream_=input;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetThumbnail(UINT target,HBITMAP* out,WTS_ALPHATYPE* alpha) override {
        if(!out || !alpha)return E_POINTER;
        *out=nullptr;*alpha=WTSAT_UNKNOWN;
        if(!stream_)return E_UNEXPECTED;
        if(!target || target>512)return E_INVALIDARG;
        CComPtr<IWICImagingFactory> factory;
        HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                                    IID_IWICImagingFactory,(void**)&factory);
        if(FAILED(hr))return hr;
        CComPtr<IWICBitmapDecoder> decoder;
        hr=factory->CreateDecoder(GUID_ContainerFormatHEIC,&GUID_VendorPrsyahmi,&decoder);
        if(FAILED(hr))return hr;
        LARGE_INTEGER zero{};
        hr=stream_->Seek(zero,STREAM_SEEK_SET,nullptr);
        if(FAILED(hr))return hr;
        hr=decoder->Initialize(stream_,WICDecodeMetadataCacheOnDemand);
        if(FAILED(hr))return hr;
        CComPtr<IWICBitmapFrameDecode> frame;
        hr=decoder->GetFrame(0,&frame);
        if(FAILED(hr))return hr;
        UINT w=0,h=0;
        hr=frame->GetSize(&w,&h);
        if(FAILED(hr))return hr;
        if(!w || !h || w>10000 || h>10000)return E_INVALIDARG;
        const UINT maxSide=(w>h)?w:h;
        const UINT targetWidth=(std::max)(1u,UINT(uint64_t(w)*target/maxSide));
        const UINT targetHeight=(std::max)(1u,UINT(uint64_t(h)*target/maxSide));
        const uint64_t stride64=uint64_t(targetWidth)*4u;
        if(stride64>UINT_MAX || stride64*targetHeight>4ull*512*512)return E_INVALIDARG;
        CComPtr<IWICBitmapScaler> scaler;
        hr=factory->CreateBitmapScaler(&scaler);
        if(FAILED(hr))return hr;
        hr=scaler->Initialize(frame,targetWidth,targetHeight,
                              WICBitmapInterpolationModeFant);
        if(FAILED(hr))return hr;
        CComPtr<IWICFormatConverter> converter;
        hr=factory->CreateFormatConverter(&converter);
        if(FAILED(hr))return hr;
        hr=converter->Initialize(scaler,GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
        if(FAILED(hr))return hr;
        BITMAPINFO info{};
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=LONG(targetWidth);
        info.bmiHeader.biHeight=-LONG(targetHeight); // Top-down, as WIC pixels.
        info.bmiHeader.biPlanes=1;
        info.bmiHeader.biBitCount=32;
        info.bmiHeader.biCompression=BI_RGB;
        void* bits=nullptr;
        HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        if(!bitmap || !bits) {
            if(bitmap)DeleteObject(bitmap);
            return E_OUTOFMEMORY;
        }
        hr=converter->CopyPixels(nullptr,UINT(stride64),
                        UINT(stride64*targetHeight),static_cast<BYTE*>(bits));
        if(FAILED(hr)){DeleteObject(bitmap);return hr;}
        GUID nativeFormat{};
        hr=frame->GetPixelFormat(&nativeFormat);
        if(FAILED(hr)){DeleteObject(bitmap);return hr;}
        const bool containsAlpha=
          IsEqualGUID(nativeFormat,GUID_WICPixelFormat32bppRGBA) ||
          IsEqualGUID(nativeFormat,GUID_WICPixelFormat64bppRGBA);
        *alpha=containsAlpha?WTSAT_ARGB:WTSAT_RGB;
        *out=bitmap;
        return S_OK;
    }
};
inline void CheckWicOutput(IWICImagingFactory* factory,const wchar_t* filename,
                           UINT thumbSize,std::vector<BYTE>& pixels,
                           UINT& width,UINT& height) {
    CComPtr<IWICBitmapDecoder> decoder;
    Checked(factory->CreateDecoderFromFilename(filename,nullptr,GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,&decoder),"provider baseline automatic codec");
    CComPtr<IWICBitmapFrameDecode> frame;
    Checked(decoder->GetFrame(0,&frame),"provider baseline frame");
    UINT sourceW=0,sourceH=0;
    Checked(frame->GetSize(&sourceW,&sourceH),"provider baseline dimensions");
    const UINT maxSide=(sourceW>sourceH)?sourceW:sourceH;
    if(!maxSide || maxSide>10000)throw std::runtime_error("provider input bounds");
    width=(std::max)(1u,UINT(uint64_t(sourceW)*thumbSize/maxSide));
    height=(std::max)(1u,UINT(uint64_t(sourceH)*thumbSize/maxSide));
    CComPtr<IWICBitmapScaler> scaler;
    Checked(factory->CreateBitmapScaler(&scaler),"provider baseline scaler");
    Checked(scaler->Initialize(frame,width,height,WICBitmapInterpolationModeFant),
            "provider baseline initialize scaler");
    CComPtr<IWICFormatConverter> converter;
    Checked(factory->CreateFormatConverter(&converter),"provider baseline converter");
    Checked(converter->Initialize(scaler,GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),
            "provider baseline init converter");
    pixels.resize(size_t(width)*height*4u);
    Checked(converter->CopyPixels(nullptr,width*4u,UINT(pixels.size()),pixels.data()),
            "provider baseline pixels");
}
inline void Run() {
    CComPtr<IWICImagingFactory> factory;
    Checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,(void**)&factory),
        "provider test WIC factory");
    // One known synthetic 10-bit HEIC. No private user images involved.
    constexpr wchar_t filename[]=L"samples\\fixture-irot3-exif6-noaux.heic";
    constexpr UINT size=256;
    std::vector<double> autoTime,providerTime;
    constexpr int loops=10;
    for(int i=0;i<loops;++i) {
        CComPtr<IWICStream> input;
        Checked(factory->CreateStream(&input),"provider create stream");
        Checked(input->InitializeFromFilename(filename,GENERIC_READ),
                "provider init stream");
        CComPtr<IInitializeWithStream> init;
        init.Attach(new DirectProvider());
        CComPtr<IThumbnailProvider> thumb;
        Checked(init->QueryInterface(IID_PPV_ARGS(&thumb)),
                "provider query interface");
        Checked(init->Initialize(input,STGM_READ),"provider initialize");
        if(init->Initialize(input,STGM_READ)!=HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED))
            throw std::runtime_error("provider double initialization accepted");
        const auto begin=Clock::now();
        HBITMAP bmp=nullptr;
        WTS_ALPHATYPE alpha=WTSAT_UNKNOWN;
        Checked(thumb->GetThumbnail(size,&bmp,&alpha),"provider GetThumbnail");
        const auto done=Clock::now();
        if(!bmp)throw std::runtime_error("provider bitmap missing");
        DIBSECTION dib{};
        const int objLen=GetObjectW(bmp,sizeof(dib),&dib);
        if(objLen!=sizeof(dib) || !dib.dsBm.bmBits) {
            DeleteObject(bmp);
            throw std::runtime_error("provider bitmap readback unavailable");
        }
        std::vector<BYTE> raw(size_t(dib.dsBm.bmWidthBytes)*dib.dsBm.bmHeight);
        memcpy(raw.data(),dib.dsBm.bmBits,raw.size());
        const UINT producedWidth=UINT(dib.dsBm.bmWidth);
        const UINT producedHeight=UINT(dib.dsBm.bmHeight);
        DeleteObject(bmp);
        if(alpha!=WTSAT_RGB)throw std::runtime_error("synthetic alpha flag");
        std::vector<BYTE> baseline;
        UINT width=0,height=0;
        const auto autoBegin=Clock::now();
        CheckWicOutput(factory,filename,size,baseline,width,height);
        const auto autoEnd=Clock::now();
        if(width!=producedWidth || height!=producedHeight || baseline!=raw)
            throw std::runtime_error("IThumbnailProvider and WIC thumbnails differ");
        providerTime.push_back(Ms(begin,done));
        autoTime.push_back(Ms(autoBegin,autoEnd));
    }
    std::sort(providerTime.begin(),providerTime.end());
    std::sort(autoTime.begin(),autoTime.end());
    printf("HEIC_IThumbnailProvider_DIRECT_AB samples=%d auto_total_median_ms=%.3f provider_total_median_ms=%.3f pixels_equal=1 alpha_valid=1 no_shell_registration=1\n",
           loops,autoTime[loops/2],providerTime[loops/2]);
    printf("HEIC_IThumbnailProvider_EXPERIMENT_ONLY_NOT_REGISTERED\n");
}
}
