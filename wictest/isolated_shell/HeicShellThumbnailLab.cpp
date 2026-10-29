// CI-only, never shipped, minimal COM class factory wrapping the already-tested
// IThumbnailProvider stream implementation. Registration is performed/undone
// solely by the dedicated GitHub Actions workflow, never by DllRegisterServer.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <new>
#include "../../wic_heic/wic_heic.h"
#include "../WICShellThumbnailProviderPilot.h"
constexpr CLSID kShellLabClsid=
  {0x98e34b24,0xdafa,0x44c7,{0x89,0xa4,0x5f,0xb3,0x9f,0x72,0xb7,0x91}};
class LabFactory final: public IClassFactory {
    volatile LONG count_{1};
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** output) override {
        if(!output)return E_POINTER;
        *output=nullptr;
        if(iid==IID_IUnknown||iid==IID_IClassFactory){
            *output=static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return InterlockedIncrement(&count_);}
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining=InterlockedDecrement(&count_);
        if(!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** p) override {
        if(!p)return E_POINTER;
        *p=nullptr;
        if(outer)return CLASS_E_NOAGGREGATION;
        auto* instance=new(std::nothrow) WICShellThumbnailProviderPilot::Provider();
        if(!instance)return E_OUTOFMEMORY;
        HRESULT hr=instance->QueryInterface(iid,p);
        instance->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override {return S_OK;}
};
extern "C" __declspec(dllexport) HRESULT __stdcall DllGetClassObject(
    REFCLSID clsid,REFIID iid,void** output){
    if(!output)return E_POINTER;
    *output=nullptr;
    if(clsid!=kShellLabClsid)return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory=new(std::nothrow) LabFactory();
    if(!factory)return E_OUTOFMEMORY;
    HRESULT hr=factory->QueryInterface(iid,output);
    factory->Release();
    return hr;
}
extern "C" __declspec(dllexport) HRESULT __stdcall DllCanUnloadNow(){
    // CI lab keeps code loaded until Shell's isolated process terminates.
    return S_FALSE;
}