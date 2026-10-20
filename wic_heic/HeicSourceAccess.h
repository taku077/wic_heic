#pragma once
// Internal-only COM capability within wic_heic.dll.
// Never exposes a pointer to the source file for modification.
// External WIC bitmap sources cannot implement this private interface.
#include <vector>
#include <cstdint>

static const GUID IID_IHeicRawSource = {
    0xf40ad7af,0xd631,0x45ba,{0x98,0x18,0x8b,0x3c,0xca,0x03,0x1b,0x66}
};
struct IHeicRawSource : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ReadFullContent(
        std::vector<uint8_t>* content) = 0;
};
