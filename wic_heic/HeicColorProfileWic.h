#pragma once
// WIC raw ICC profile handling for JPEG-equivalent HEIC color preservation.
// ExifColorSpace and NCLX are NOT byte-identical ICC profiles; do not silently
// substitute or synthesize one. Caller gets an explicit unsupported HRESULT.
namespace HeicColorProfileWic {
constexpr UINT kMaxProfileBytes = 16u * 1024u * 1024u;

inline HRESULT Read(IWICColorContext* source, std::vector<uint8_t>* bytes) {
    if (!source || !bytes) return E_POINTER;
    WICColorContextType type = WICColorContextUninitialized;
    HRESULT hr = source->GetType(&type);
    if (FAILED(hr)) return hr;
    if (type != WICColorContextProfile) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    UINT needed = 0;
    hr = source->GetProfileBytes(0, nullptr, &needed);
    if (FAILED(hr)) return hr;
    if (!needed || needed > kMaxProfileBytes) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    std::vector<uint8_t> result(needed);
    UINT actual = 0;
    hr = source->GetProfileBytes(needed, result.data(), &actual);
    if (FAILED(hr)) return hr;
    if (actual != needed) return WINCODEC_ERR_BADIMAGE;
    bytes->swap(result);
    return S_OK;
}

inline HRESULT ReadArray(UINT count, IWICColorContext** sources,
                         std::vector<uint8_t>* bytes) {
    if (!bytes) return E_POINTER;
    if (count == 0) {
        bytes->clear();
        return S_OK;
    }
    if (count != 1) return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    if (!sources || !sources[0]) return E_POINTER;
    return Read(sources[0], bytes);
}
}
