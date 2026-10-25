#pragma once
// Conservative in-process bridge for Windows Photos' default/no-options WIC
// raster save path. Never writes to input streams or the file system.
// No image data, file names, EXIF, or paths in trace output.
//
// A decoder may offer one short-lived original HEIF. EncoderCommit can use it
// ONLY if ALL RGB pixels of the output are the clockwise 90-degree rotation
// of that decoded HEIF. Other edits, 180/270 rotations, ambiguity, missing
// metadata and unsupported aux structures all FALL BACK to normal raster encode.
// There is deliberately no external protocol, registry, or disk cache.
#include "HeicLosslessNative.h"
#include "PhotosWicTrace.h"
#include <vector>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace PhotosPureRotationBridge {
struct Candidate {
    std::mutex gate;
    std::vector<uint8_t> original;
    UINT width=0,height=0;
    std::chrono::steady_clock::time_point at{};
    bool ambiguous=false;
};
inline Candidate& Current() {static Candidate item;return item;}

inline bool IsValidHEIF(const std::vector<uint8_t>& bytes) {
    return bytes.size()>=12 && bytes.size()<=64u*1024u*1024u &&
        bytes[4]=='f' && bytes[5]=='t' && bytes[6]=='y' && bytes[7]=='p';
}
inline void Offer(const std::vector<uint8_t>& bytes,UINT w,UINT h) {
    if(!IsValidHEIF(bytes) || !w || !h || w>20000 || h>20000)return;
    auto& state=Current();
    std::lock_guard<std::mutex> lock(state.gate);
    const auto now=std::chrono::steady_clock::now();
    if(now-state.at>std::chrono::seconds(30)) {
        state.original.clear();
        state.ambiguous=false;
    }
    if(state.ambiguous){state.at=now;return;}
    if(!state.original.empty() &&
       (state.width!=w || state.height!=h || state.original!=bytes)) {
        // More than one distinct photo decoded in this Photos process.
        // Never guess which source an opaque edited raster came from.
        state.original.clear();
        state.ambiguous=true;
    } else {
        state.original=bytes;
        state.width=w;
        state.height=h;
    }
    state.at=now;
}
inline bool TryCW90(heif::Image& edited,UINT width,UINT height,
                    const std::vector<uint8_t>& explicitICC,
                    HeicLosslessNative::Bytes& output) {
    output.clear();
    if(!width || !height || width>20000 || height>20000)return false;
    std::vector<uint8_t> source;
    UINT w=0,h=0;
    {
        auto& state=Current();
        std::lock_guard<std::mutex> lock(state.gate);
        if(state.ambiguous || state.original.empty() ||
           std::chrono::steady_clock::now()-state.at>std::chrono::seconds(30) ||
           state.width!=height || state.height!=width)return false;
        source.swap(state.original); // one-shot, no stale source replay
        w=state.width;h=state.height;
    }
    try {
        heif::Context context;
        context.read_from_memory(source.data(),source.size());
        auto original=context.get_primary_image_handle();
        if(original.get_width()!=int(w) || original.get_height()!=int(h) ||
           original.has_alpha_channel())return false;
        // An explicitly changed color profile is an edit, not a pure rotate.
        if(!explicitICC.empty()){
            const auto* raw=original.get_raw_image_handle();
            const auto n=heif_image_handle_get_raw_color_profile_size(raw);
            if(n!=explicitICC.size())return false;
            std::vector<uint8_t> icc(n);
            const auto err=heif_image_handle_get_raw_color_profile(raw,icc.data());
            if(err.code!=heif_error_Ok || icc!=explicitICC)return false;
        }
        auto rgb=original.decode_image(heif_colorspace_RGB,
                                       heif_chroma_interleaved_RGB);
        size_t sourceStride=0;
        int targetStride=0;
        auto* before=rgb.get_plane2(heif_channel_interleaved,&sourceStride);
        auto* after=edited.get_plane(heif_channel_interleaved,&targetStride);
        if(!before || !after || sourceStride<size_t(w)*3 ||
           targetStride<int(width)*3)return false;
        // Exact full-frame comparison. A crop/brightness/extra edit cannot
        // accidentally be discarded in favour of a metadata-only rotation.
        for(UINT y=0;y<height;++y){
            const uint8_t* outRow=after+size_t(y)*targetStride;
            for(UINT x=0;x<width;++x){
                const uint8_t* in=before+size_t(h-1-x)*sourceStride+
                                  size_t(y)*3;
                if(std::memcmp(in,outRow+size_t(x)*3,3)!=0)
                    return false;
            }
        }
        auto rotated=HeicLosslessNative::Transform(source,1);
        if(rotated.size()!=source.size())return false;
        output.swap(rotated);
        WicHeicDiagnostic::Emit("PhotosPureRotateLossless",width,height,
                                 1,1,"exact-rgb-cw90");
        return true;
    } catch(...) {
        // Unsupported HEIF, thumbnail, EXIF or HDR layout: no partial output.
        output.clear();
        return false;
    }
}
} // namespace PhotosPureRotationBridge
