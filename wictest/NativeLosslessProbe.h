#pragma once
#include "../wic_heic/HeicLosslessNative.h"
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace NativeLosslessProbe {
inline HeicLosslessNative::Bytes Read(const char* file) {
    std::ifstream in(file,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot open synthetic HEIC fixture");
    return HeicLosslessNative::Bytes(std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}
inline void Run() {
    const auto initial=Read("samples\\fixture-irot3-exif6.heic");
    const auto expected=Read("samples\\fixture-lossless-cw90.heic");
    const auto actual=HeicLosslessNative::Transform(initial,1);
    if(actual!=expected)
        throw std::runtime_error("Native lossless transform does not match Python reference");
    auto current=initial;
    for(int i=0;i<4;i++) {
        auto next=HeicLosslessNative::Transform(current,1);
        if(next.size()!=initial.size())
            throw std::runtime_error("Native lossless changed container length");
        size_t changed=0;
        for(size_t k=0;k<next.size();k++)if(next[k]!=current[k])++changed;
        if(changed>2 || changed<1)
            throw std::runtime_error("Native lossless changed unrelated HEIC bytes");
        current=next;
    }
    for(auto bad : {HeicLosslessNative::Bytes{0,1,2,3},
                    HeicLosslessNative::Bytes(initial.begin(),initial.begin()+40)}) {
        try {
            auto transformed=HeicLosslessNative::Transform(bad,1);
            throw std::runtime_error("Malformed native HEIC accepted");
        } catch(const std::runtime_error& e) {
            if(std::string(e.what())=="Malformed native HEIC accepted") throw;
        }
    }
    printf("NATIVE_LOSSLESS_HEIF_MATCHES_PYTHON_AND_ROTATION_TEST_PASSED\n");
}
}
