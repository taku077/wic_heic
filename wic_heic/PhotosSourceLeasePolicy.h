#pragma once
// Fixed safety bounds for short-lived, in-process Windows Photos HEIF
// rotation-source candidates. No paths, user metadata, file I/O or image
// decoders are exposed by this policy header.
#include <chrono>
#include <cstddef>
#include <vector>
#include <utility>

namespace PhotosPureRotationBridge {
constexpr auto kSourceTtl=std::chrono::minutes(10);
constexpr size_t kMaxSourceCount=8;
constexpr size_t kMaxSourceBytes=96u*1024u*1024u;

/* Dimension-only process-local provenance. The source candidate bytes can
   expire or be evicted while Photos is open, but associated HDR/thumbnail
   loss must still be prohibited. Never persist image paths or bytes.
   Fail closed if the bounded unique-geometry budget overflows. */
constexpr size_t kMaxLinkedSourceGeometries=128;
struct LinkedSourceGeometryGuard {
    std::vector<std::pair<unsigned,unsigned>> dimensions;
    bool overflow=false;
    void Observe(unsigned w,unsigned h) {
        if(!w || !h)return;
        for(const auto& z:dimensions)
            if(z.first==w && z.second==h)return;
        if(dimensions.size()>=kMaxLinkedSourceGeometries) {
            overflow=true;
            return;
        }
        dimensions.emplace_back(w,h);
    }
    bool PossiblyRelated(unsigned w,unsigned h) const {
        if(overflow)return true;
        for(const auto& z:dimensions)
            if((z.first==w && z.second==h) ||
               (z.first==h && z.second==w))return true;
        return false;
    }
};

}
