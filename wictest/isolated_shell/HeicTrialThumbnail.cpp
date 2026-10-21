// Shell IThumbnailProvider: .wictrial synthetic CI or .heic preview MSI.
// The .heic version loads a private decoder and does not alter global WIC.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <thumbcache.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <new>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include "../../wic_heic/wic_heic.h"

#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(linker,"/export:DllGetClassObject,PRIVATE")
#pragma comment(linker,"/export:DllCanUnloadNow,PRIVATE")

using Microsoft::WRL::ComPtr;
namespace {
#ifdef WIC_HEIC_FAST_PREVIEW
const CLSID kTrialClsid =
    {0xd973196c,0x165c,0x4fce,{0x88,0x38,0x17,0xc3,0x70,0x37,0xd3,0x84}};
const wchar_t kCounterKey[] =
    L"Software\\WicHeicFastPreviewCI\\{D973196C-165C-4FCE-8838-17C37037D384}";
#else
const CLSID kTrialClsid =
    {0x94da5cb1,0xf0e4,0x42c2,{0xb5,0xd8,0xb1,0xf3,0xce,0x6d,0x78,0x2b}};
const wchar_t kCounterKey[] =
    L"Software\\WicHeicSyntheticTrial\\{94DA5CB1-F0E4-42C2-B5D8-B1F3CE6D782B}";
#endif
std::atomic<long> g_locks{0},g_objects{0};

void CountTestMarker(const wchar_t* name) noexcept {
    // Only an ephemeral harness-created HKCU marker enables writes.
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,kCounterKey,0,
                     KEY_QUERY_VALUE|KEY_SET_VALUE,&key)!=ERROR_SUCCESS)return;
    DWORD enabled=0,bytes=sizeof(enabled),type=0;
    const LSTATUS status=RegQueryValueExW(key,L"Enable",nullptr,&type,
                                          reinterpret_cast<BYTE*>(&enabled),&bytes);
    if(status==ERROR_SUCCESS && type==REG_DWORD && enabled==1) {
        DWORD count=0;
        type=0;bytes=sizeof(count);
        if(RegQueryValueExW(key,name,nullptr,&type,
            reinterpret_cast<BYTE*>(&count),&bytes)!=ERROR_SUCCESS)count=0;
        if(count<10000) {
            ++count;
            RegSetValueExW(key,name,0,REG_DWORD,
                           reinterpret_cast<const BYTE*>(&count),sizeof(count));
        }
    }
    RegCloseKey(key);
}

bool ForceArgbForCiTrial() noexcept {
#ifdef WIC_HEIC_FAST_PREVIEW
    return true; // CI-proven ARGB PBGRA cache presentation.
#else
    // CI test option lives only under the synthetic per-run HKCU marker.
    // It has no effect without a script-created Enable=1 entry.
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_CURRENT_USER,kCounterKey,0,KEY_QUERY_VALUE,&key)
           !=ERROR_SUCCESS)return false;
    DWORD enabled=0,force=0,bytes=sizeof(DWORD),type=0;
    bool valid=RegQueryValueExW(key,L"Enable",nullptr,&type,
             reinterpret_cast<BYTE*>(&enabled),&bytes)==ERROR_SUCCESS &&
             type==REG_DWORD && enabled==1;
    type=0;bytes=sizeof(DWORD);
    bool requested=RegQueryValueExW(key,L"ForceArgb",nullptr,&type,
             reinterpret_cast<BYTE*>(&force),&bytes)==ERROR_SUCCESS &&
             type==REG_DWORD && force==1;
    RegCloseKey(key);
    return valid && requested;
#endif
}

#ifdef WIC_HEIC_FAST_PREVIEW
// Private WIC HEIC decoder, copied into the same installation directory.
// Do not register this codec globally: leave Windows Photos, HEIC Manager,
// and the user's installed WIC codec registrations entirely unchanged.
class ScopedPrivateCodec final {
    HMODULE module_ = nullptr;
public:
    ScopedPrivateCodec() = default;
    ScopedPrivateCodec(const ScopedPrivateCodec&) = delete;
    ScopedPrivateCodec& operator=(const ScopedPrivateCodec&) = delete;
    ~ScopedPrivateCodec() { if(module_) FreeLibrary(module_); }

    HRESULT CreateDecoder(IWICBitmapDecoder** result) noexcept {
        if(!result) return E_POINTER;
        *result = nullptr;
        HMODULE self = nullptr;
        const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        if(!GetModuleHandleExW(flags,
                reinterpret_cast<LPCWSTR>(&g_objects), &self))
            return HRESULT_FROM_WIN32(GetLastError());

        wchar_t path[MAX_PATH] = {};
        const DWORD size = GetModuleFileNameW(self, path, MAX_PATH);
        if(!size || size >= MAX_PATH)
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        std::wstring filepath(path, size);
        const auto slash = filepath.find_last_of(L"\\/");
        if(slash == std::wstring::npos) return E_UNEXPECTED;
        filepath.erase(slash + 1);
        filepath += L"WicHeicPrivateCodec.dll";
        module_ = LoadLibraryExW(filepath.c_str(), nullptr,
                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module_) return HRESULT_FROM_WIN32(GetLastError());
        using GetFactory = HRESULT (STDAPICALLTYPE *)(REFCLSID, REFIID, LPVOID*);
        const auto entry = reinterpret_cast<GetFactory>(
                 GetProcAddress(module_, "DllGetClassObject"));
        if(!entry) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        ComPtr<IClassFactory> factory;
        const HRESULT hr = entry(CLSID_HEICDecoder, IID_IClassFactory,
                                 reinterpret_cast<void**>(factory.GetAddressOf()));
        if(FAILED(hr)) return hr;
        return factory->CreateInstance(nullptr, IID_PPV_ARGS(result));
    }
};
#endif

class Provider final : public IThumbnailProvider, public IInitializeWithStream {
    std::atomic<ULONG> refs_{1};
    ComPtr<IStream> stream_;
public:
    Provider() {++g_objects;CountTestMarker(L"Activated");}
    ~Provider() {--g_objects;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** result) override {
        if(!result)return E_POINTER;
        *result=nullptr;
        if(IsEqualIID(iid,IID_IUnknown) || IsEqualIID(iid,__uuidof(IThumbnailProvider)))
            *result=static_cast<IThumbnailProvider*>(this);
        else if(IsEqualIID(iid,__uuidof(IInitializeWithStream)))
            *result=static_cast<IInitializeWithStream*>(this);
        if(!*result)return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG value=--refs_;if(!value)delete this;return value;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IStream* stream,DWORD) override {
        if(!stream)return E_INVALIDARG;
        if(stream_)return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        stream_=stream;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetThumbnail(UINT cx,HBITMAP* output,
                                           WTS_ALPHATYPE* alpha) override {
        if(!output || !alpha)return E_POINTER;
        *output=nullptr;*alpha=WTSAT_UNKNOWN;
        if(!stream_)return E_UNEXPECTED;
        if(cx==0 || cx>1024)return E_INVALIDARG;
        CountTestMarker(L"Attempts");
        ComPtr<IWICImagingFactory> factory;
        HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,
            CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
        if(FAILED(hr))return hr;
        // The preview MSI carries its own *unregistered* decoder.  The
        // synthetic .wictrial CI variant still uses the original WIC path.
#ifdef WIC_HEIC_FAST_PREVIEW
        ScopedPrivateCodec privateCodec; // must outlive all WIC decode objects
#endif
        ComPtr<IWICBitmapDecoder> decoder;
#ifdef WIC_HEIC_FAST_PREVIEW
        hr=privateCodec.CreateDecoder(decoder.GetAddressOf());
#else
        hr=factory->CreateDecoder(GUID_ContainerFormatHEIC,
            &GUID_VendorPrsyahmi,decoder.GetAddressOf());
#endif
        if(FAILED(hr))return hr;
        LARGE_INTEGER start{};
        hr=stream_->Seek(start,STREAM_SEEK_SET,nullptr);
        if(FAILED(hr))return hr;
        hr=decoder->Initialize(stream_.Get(),WICDecodeMetadataCacheOnDemand);
        if(FAILED(hr))return hr;
        ComPtr<IWICBitmapFrameDecode> frame;
        hr=decoder->GetFrame(0,frame.GetAddressOf());
        if(FAILED(hr))return hr;
        UINT width=0,height=0;
        hr=frame->GetSize(&width,&height);
        if(FAILED(hr))return hr;
        if(!width || !height || width>10000 || height>10000)return E_INVALIDARG;
        const UINT maxSide=(std::max)(width,height);
        const UINT tw=(std::max)(1u,UINT(uint64_t(width)*cx/maxSide));
        const UINT th=(std::max)(1u,UINT(uint64_t(height)*cx/maxSide));
        const uint64_t bytes=uint64_t(tw)*th*4u;
        if(bytes>16ull*1024*1024 || bytes>UINT_MAX)return E_INVALIDARG;
        ComPtr<IWICBitmapScaler> scaler;
        hr=factory->CreateBitmapScaler(scaler.GetAddressOf());
        if(FAILED(hr))return hr;
        hr=scaler->Initialize(frame.Get(),tw,th,WICBitmapInterpolationModeFant);
        if(FAILED(hr))return hr;
        ComPtr<IWICFormatConverter> converter;
        hr=factory->CreateFormatConverter(converter.GetAddressOf());
        if(FAILED(hr))return hr;
        hr=converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
        if(FAILED(hr))return hr;
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth=LONG(tw);
        bitmapInfo.bmiHeader.biHeight=-LONG(th);
        bitmapInfo.bmiHeader.biPlanes=1;
        bitmapInfo.bmiHeader.biBitCount=32;
        bitmapInfo.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;
        HBITMAP dib=CreateDIBSection(nullptr,&bitmapInfo,DIB_RGB_COLORS,
                                     &pixels,nullptr,0);
        if(!dib || !pixels) {
            if(dib)DeleteObject(dib);
            return E_OUTOFMEMORY;
        }
        hr=converter->CopyPixels(nullptr,tw*4u,UINT(bytes),
                                  static_cast<BYTE*>(pixels));
        if(FAILED(hr)){DeleteObject(dib);return hr;}
        WICPixelFormatGUID sourceFormat{};
        hr=frame->GetPixelFormat(&sourceFormat);
        if(FAILED(hr)){DeleteObject(dib);return hr;}
        const bool hasAlpha =
            IsEqualGUID(sourceFormat,GUID_WICPixelFormat32bppRGBA) ||
            IsEqualGUID(sourceFormat,GUID_WICPixelFormat64bppRGBA);
        // The PBGRA conversion supplies a valid alpha byte even for opaque
        // RGB fixtures. Experimental ARGB mode can test whether Shell uses a
        // different cache representation for alpha-labelled thumbnails.
        *alpha=(hasAlpha || ForceArgbForCiTrial())?WTSAT_ARGB:WTSAT_RGB;
        *output=dib;
        CountTestMarker(L"Calls");
        return S_OK;
    }
};

class Factory final : public IClassFactory {
    std::atomic<ULONG> refs_{1};
public:
    Factory(){++g_objects;}
    ~Factory(){--g_objects;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** result) override {
        if(!result)return E_POINTER;
        *result=nullptr;
        if(IsEqualIID(iid,IID_IUnknown)||IsEqualIID(iid,IID_IClassFactory))
            *result=static_cast<IClassFactory*>(this);
        if(!*result)return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG value=--refs_;if(!value)delete this;return value;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,
                                              void** output) override {
        if(!output)return E_POINTER;
        *output=nullptr;
        if(outer)return CLASS_E_NOAGGREGATION;
        Provider* object=new(std::nothrow) Provider();
        if(!object)return E_OUTOFMEMORY;
        const HRESULT hr=object->QueryInterface(iid,output);
        object->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if(lock)++g_locks;else --g_locks;
        return S_OK;
    }
};
}

STDAPI DllGetClassObject(REFCLSID clsid,REFIID iid,LPVOID* output) {
    if(!output)return E_POINTER;
    *output=nullptr;
    if(!IsEqualCLSID(clsid,kTrialClsid))return CLASS_E_CLASSNOTAVAILABLE;
    Factory* factory=new(std::nothrow) Factory();
    if(!factory)return E_OUTOFMEMORY;
    const HRESULT hr=factory->QueryInterface(iid,output);
    factory->Release();
    return hr;
}
STDAPI DllCanUnloadNow() {
    return g_objects.load()==0 && g_locks.load()==0 ? S_OK : S_FALSE;
}
// No self-registration: MSI owns the preview CLSID and HKCU .heic ShellEx;
// CI's synthetic .wictrial variant is separate and unchanged.
