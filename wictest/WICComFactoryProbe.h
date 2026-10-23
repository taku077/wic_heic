#pragma once
// Standard COM activation contract used by Windows Photos and other WIC apps.
// Regression for a previous class factory that incorrectly selected the
// decoder or encoder by IID instead of CLSID and rejected IID_IUnknown.
namespace WICComFactoryProbe {
inline void Must(HRESULT hr,const char* label) {
    if(FAILED(hr)){
        printf("WIC_COM_FACTORY_FAILURE %s hr=0x%08X\n",label,(unsigned)hr);
        throw std::runtime_error(label);
    }
}
inline void CheckOne(const CLSID& clsid,bool decoderExpected) {
    CComPtr<IUnknown> classUnknown;
    Must(CoGetClassObject(clsid,CLSCTX_INPROC_SERVER,nullptr,IID_IUnknown,
         reinterpret_cast<void**>(&classUnknown)),"GetClassObject IUnknown");
    CComQIPtr<IClassFactory> factory(classUnknown);
    if(!factory)throw std::runtime_error("Class factory IClassFactory QI failed");
    CComPtr<IUnknown> instance;
    Must(factory->CreateInstance(nullptr,IID_IUnknown,
        reinterpret_cast<void**>(&instance)),"CreateInstance IUnknown");
    CComQIPtr<IWICBitmapDecoder> dec(instance);
    CComQIPtr<IWICBitmapEncoder> enc(instance);
    if(!!dec!=decoderExpected || !!enc==decoderExpected)
        throw std::runtime_error("WIC decoder / encoder CLSID mixed up");
    CComPtr<IUnknown> shouldStayNull;
    HRESULT hr=factory->CreateInstance(instance,IID_IUnknown,
        reinterpret_cast<void**>(&shouldStayNull));
    if(hr!=CLASS_E_NOAGGREGATION || shouldStayNull)
        throw std::runtime_error("WIC class factory mistakenly permits aggregation");
    printf("WIC_PHOTOS_IUNKNOWN_FACTORY_CLISD_BOUND_PASS kind=%s\n",
           decoderExpected?"decoder":"encoder");
}
inline void Run() {
    CheckOne(CLSID_HEICDecoder,true);
    CheckOne(CLSID_HEICEncoder,false);
    CComPtr<IWICBitmapEncoder> invalidEncoder;
    HRESULT hr=CoCreateInstance(CLSID_HEICDecoder,nullptr,
        CLSCTX_INPROC_SERVER,IID_IWICBitmapEncoder,
        reinterpret_cast<void**>(&invalidEncoder));
    if(hr!=E_NOINTERFACE || invalidEncoder)
        throw std::runtime_error("Decoder CLSID returned an encoder interface");
    CComPtr<IWICBitmapDecoder> invalidDecoder;
    hr=CoCreateInstance(CLSID_HEICEncoder,nullptr,CLSCTX_INPROC_SERVER,
        IID_IWICBitmapDecoder,reinterpret_cast<void**>(&invalidDecoder));
    if(hr!=E_NOINTERFACE || invalidDecoder)
        throw std::runtime_error("Encoder CLSID returned a decoder interface");
    puts("WIC_PHOTOS_STANDARD_COM_CLASS_FACTORY_CONTRACT_PASS");
}
}
