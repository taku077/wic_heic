#pragma once
// WIC metadata writer staging for lossless HEIC source transfer only.
// A metadata query write may be accepted before WriteSource, but it is
// committed only if the ORIGINAL frame has the exact same typed value.
// Any actual edit requires a separate provenance-preserving HEIF writer;
// never acknowledge data loss as a successful Photos edit.
#include <propidl.h>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>

struct HeicGuardedMetadataEntry final {
    std::wstring name;
    PROPVARIANT value;
    explicit HeicGuardedMetadataEntry(const std::wstring& n) : name(n) {
        PropVariantInit(&value);
    }
    ~HeicGuardedMetadataEntry() { PropVariantClear(&value); }
    HeicGuardedMetadataEntry(const HeicGuardedMetadataEntry&) = delete;
    HeicGuardedMetadataEntry& operator=(const HeicGuardedMetadataEntry&) = delete;
};
struct HeicGuardedMetadataState final {
    std::mutex mutex;
    bool closed=false;
    std::vector<std::unique_ptr<HeicGuardedMetadataEntry>> entries;
};

inline bool HeicWicMetadataValueSame(const PROPVARIANT& a,const PROPVARIANT& b) noexcept {
    if(a.vt!=b.vt) return false;
    switch(a.vt) {
    case VT_EMPTY: return true;
    case VT_UI1: return a.bVal==b.bVal;
    case VT_UI2: return a.uiVal==b.uiVal;
    case VT_UI4: return a.ulVal==b.ulVal;
    case VT_UI8: return a.uhVal.QuadPart==b.uhVal.QuadPart;
    case VT_I1: return a.cVal==b.cVal;
    case VT_I2: return a.iVal==b.iVal;
    case VT_I4: return a.lVal==b.lVal;
    case VT_I8: return a.hVal.QuadPart==b.hVal.QuadPart;
    case VT_LPWSTR: return a.pwszVal && b.pwszVal &&
        wcscmp(a.pwszVal,b.pwszVal)==0;
    case VT_BLOB: return a.blob.cbSize==b.blob.cbSize &&
        (a.blob.cbSize==0 || (a.blob.pBlobData && b.blob.pBlobData &&
           memcmp(a.blob.pBlobData,b.blob.pBlobData,a.blob.cbSize)==0));
    default: return false; // unknown metadata formats MUST NOT be skipped
    }
}

inline HRESULT HeicVerifyUnchangedMetadata(
    const std::shared_ptr<HeicGuardedMetadataState>& state,
    IWICBitmapSource* bitmap) {
    if(!state) return S_OK;
    std::lock_guard<std::mutex> lock(state->mutex);
    if(state->entries.empty())return S_OK;
    if(!bitmap)return E_POINTER;
    IWICBitmapFrameDecode* frame=nullptr;
    HRESULT hr=bitmap->QueryInterface(IID_IWICBitmapFrameDecode,
        reinterpret_cast<void**>(&frame));
    if(FAILED(hr)||!frame)return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    IWICMetadataQueryReader* query=nullptr;
    hr=frame->GetMetadataQueryReader(&query);
    frame->Release();
    if(FAILED(hr)||!query)return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    for(const auto& entry:state->entries) {
        PROPVARIANT actual;
        PropVariantInit(&actual);
        hr=query->GetMetadataByName(entry->name.c_str(),&actual);
        const bool match=SUCCEEDED(hr) &&
            HeicWicMetadataValueSame(actual,entry->value);
        PropVariantClear(&actual);
        if(!match) {
            query->Release();
            return WINCODEC_ERR_UNSUPPORTEDOPERATION;
        }
    }
    query->Release();
    return S_OK;
}

class CHeicGuardedMetadataQueryWriter final : public IWICMetadataQueryWriter {
    std::atomic<ULONG> m_refs{1};
    std::shared_ptr<HeicGuardedMetadataState> m_state;
public:
    explicit CHeicGuardedMetadataQueryWriter(
        std::shared_ptr<HeicGuardedMetadataState> state)
        :m_state(std::move(state)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(iid==IID_IUnknown||iid==IID_IWICMetadataQueryReader||
           iid==IID_IWICMetadataQueryWriter) {
            *out=static_cast<IWICMetadataQueryWriter*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++m_refs;}
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n=--m_refs;
        if(!n)delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetContainerFormat(GUID* format) override {
        if(!format)return E_POINTER;
        *format=GUID_ContainerFormatHEIC;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetLocation(UINT max,WCHAR* location,
        UINT* actual) override {
        if(!actual)return E_POINTER;
        *actual=2; // WCHAR count includes the terminating null
        if(!max)return S_OK;
        if(!location)return E_POINTER;
        if(max<2)return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        location[0]=L'/';location[1]=L'\0';
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMetadataByName(LPCWSTR name,
        PROPVARIANT* value) override {
        if(!name||!value)return E_POINTER;
        PropVariantInit(value);
        std::lock_guard<std::mutex> lock(m_state->mutex);
        for(const auto& entry:m_state->entries)
            if(entry->name==name)return PropVariantCopy(value,&entry->value);
        return WINCODEC_ERR_PROPERTYNOTFOUND;
    }
    HRESULT STDMETHODCALLTYPE GetEnumerator(IEnumString** enumerator) override {
        if(!enumerator)return E_POINTER;
        *enumerator=nullptr;
        // Do not invent nonexistent metadata from a source not yet supplied.
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
    HRESULT STDMETHODCALLTYPE SetMetadataByName(LPCWSTR name,
        const PROPVARIANT* value) override {
        if(!name||!value)return E_POINTER;
        try {
            const size_t n=wcsnlen_s(name,1025);
            if(!n||n>1024)return E_INVALIDARG;
            // Only types that can be compared strictly against source
            // metadata can be accepted. No arbitrary serialization.
            if(!HeicWicMetadataValueSame(*value,*value))
                return WINCODEC_ERR_UNSUPPORTEDOPERATION;
            std::lock_guard<std::mutex> lock(m_state->mutex);
            if(m_state->closed)return WINCODEC_ERR_WRONGSTATE;
            for(auto& entry:m_state->entries) {
                if(entry->name!=name)continue;
                PROPVARIANT replacement;
                PropVariantInit(&replacement);
                HRESULT hr=PropVariantCopy(&replacement,value);
                if(FAILED(hr))return hr;
                PropVariantClear(&entry->value);
                entry->value=replacement;
                return S_OK;
            }
            if(m_state->entries.size()>=64)
                return WINCODEC_ERR_UNSUPPORTEDOPERATION;
            auto entry=std::make_unique<HeicGuardedMetadataEntry>(name);
            HRESULT hr=PropVariantCopy(&entry->value,value);
            if(FAILED(hr))return hr;
            m_state->entries.push_back(std::move(entry));
            return S_OK;
        } catch(const std::bad_alloc&) {return E_OUTOFMEMORY;}
          catch(...) {return E_INVALIDARG;}
    }
    HRESULT STDMETHODCALLTYPE RemoveMetadataByName(LPCWSTR name) override {
        if(!name)return E_POINTER;
        // Removing source metadata is deliberately not a lossless copy.
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
};
