#pragma once
// HEIF WIC encoder options. Exposes the standard names and VT_UI1 values used
// by Microsoft's HEIF WIC codec, without claiming lossless rotation support.
// Fail closed in FrameEncode until the original compressed HEVC item is available.
#include <atomic>
#include <cwchar>
#include <new>

class CHeicFrameOptions final : public IPropertyBag2 {
    std::atomic<ULONG> m_Refs{1};
    BYTE m_Transform=0;  // WICBitmapTransformRotate0
    BYTE m_Compression=0; // WICHeifCompressionDontCare

    static int Index(LPCOLESTR name) {
        if (!name) return -1;
        if (wcscmp(name,L"BitmapTransform")==0) return 0;
        if (wcscmp(name,L"HeifCompressionMethod")==0) return 1;
        return -1;
    }
    static const wchar_t* Name(int i) {
        return i==0?L"BitmapTransform":L"HeifCompressionMethod";
    }
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out) return E_POINTER;
        *out=nullptr;
        if(IsEqualIID(iid,IID_IUnknown) || IsEqualIID(iid,IID_IPropertyBag2))
            *out=static_cast<IPropertyBag2*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_Refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n=--m_Refs;
        if(!n)delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE Read(ULONG n,PROPBAG2* props,IErrorLog*,VARIANT* values,HRESULT* errors) override {
        if(n && (!props||!values||!errors))return E_POINTER;
        for(ULONG i=0;i<n;i++){
            VariantInit(&values[i]);
            const int k=Index(props[i].pstrName);
            if(k<0){errors[i]=E_INVALIDARG;continue;}
            values[i].vt=VT_UI1;
            values[i].bVal=k==0?m_Transform:m_Compression;
            errors[i]=S_OK;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Write(ULONG n,PROPBAG2* props,VARIANT* values) override {
        if(n && (!props||!values))return E_POINTER;
        BYTE transform=m_Transform, compression=m_Compression;
        for(ULONG i=0;i<n;i++){
            const int k=Index(props[i].pstrName);
            // Unknown generic codec options are ignored for interoperability.
            if(k<0)continue;
            if(values[i].vt!=VT_UI1)return DISP_E_TYPEMISMATCH;
            const BYTE v=values[i].bVal;
            if(k==0){
                // Only ordinary 0/90/180/270 rotation (no flip/transpose).
                if(v>3)return WINCODEC_ERR_UNSUPPORTEDOPERATION;
                transform=v;
            } else {
                // 0=DontCare, 1=NoCompression, 2=HEVC.
                if(v>2)return WINCODEC_ERR_UNSUPPORTEDOPERATION;
                compression=v;
            }
        }
        m_Transform=transform;m_Compression=compression;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CountProperties(ULONG* count) override {
        if(!count)return E_POINTER;
        *count=2;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyInfo(ULONG first,ULONG count,PROPBAG2* props,ULONG* got) override {
        if(!got)return E_POINTER;
        *got=0;
        if(count && !props)return E_POINTER;
        if(first>2)return E_INVALIDARG;
        for(ULONG i=first;i<2 && *got<count;i++){
            PROPBAG2 item{};
            const wchar_t* name=Name(int(i));
            const size_t len=wcslen(name)+1;
            item.pstrName=static_cast<LPOLESTR>(CoTaskMemAlloc(len*sizeof(wchar_t)));
            if(!item.pstrName){
                for(ULONG j=0;j<*got;j++)CoTaskMemFree(props[j].pstrName);
                *got=0;
                return E_OUTOFMEMORY;
            }
            wcscpy_s(item.pstrName,len,name);
            item.dwType=PROPBAG2_TYPE_DATA;
            item.vt=VT_UI1;
            props[*got]=item;
            ++*got;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE LoadObject(LPCOLESTR,DWORD,IUnknown*,IErrorLog*) override {
        return E_NOTIMPL;
    }
};
