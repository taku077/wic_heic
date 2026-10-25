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


// Separate internal COM contract for an authentic non-primary decoder frame.
// Explicit frame index/count lets the encoder verify that a multi-image
// HEIF container is being copied in full, not substituted for one bitmap.
// The primary IHeicRawSource contract and its vtable remain unchanged.
static const GUID IID_IHeicAuxiliaryContainerSource = {
    0x7c143fa2,0x91a8,0x4dab,{0x80,0x38,0x78,0x72,0x1d,0x2f,0x4d,0x51}
};
struct IHeicAuxiliaryContainerSource : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ReadContainerAndIndex(
        std::vector<uint8_t>* container,UINT* topLevelIndex,
        UINT* topLevelCount) = 0;
};
