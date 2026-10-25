#pragma once
// Conservative in-process bridge for Windows Photos' default/no-options WIC
// raster save path. Never writes to input streams or the file system.
// No image data, file names, EXIF, or paths in trace output.
//
// A decoder may offer one short-lived original HEIF. EncoderCommit can use it
// ONLY if ALL RGB pixels match EXACTLY ONE 90/180/270 degree rotation of
// that decoded HEIF. Other edits, ambiguity, missing metadata and unsupported
// aux structures all FALL BACK to normal raster encode.
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

inline bool CaptureAllowedHost() {
    // Avoid extra HEIC reads on every Explorer thumbnail request. Photos
    // executable names vary between Win10/Win11 and legacy/modern packages.
    // The artificial CI harness is also permitted, never Explorer.exe.
    wchar_t name[MAX_PATH]{};
    const DWORD n=GetModuleFileNameW(nullptr,name,MAX_PATH);
    if(!n || n>=MAX_PATH)return false;
    const wchar_t* base=wcsrchr(name,L'\\');
    base=base?base+1:name;
    return _wcsicmp(base,L"Microsoft.Photos.exe")==0 ||
           _wcsicmp(base,L"Photos.exe")==0 ||
           _wcsicmp(base,L"PhotosApp.exe")==0 ||
           _wcsicmp(base,L"PhotoApp.exe")==0 ||
           _wcsicmp(base,L"wictest.exe")==0;
}


inline bool IsValidHEIF(const std::vector<uint8_t>& bytes) {
    return bytes.size()>=12 && bytes.size()<=64u*1024u*1024u &&
        bytes[4]=='f' && bytes[5]=='t' && bytes[6]=='y' && bytes[7]=='p';
}
inline void Offer(const std::vector<uint8_t>& bytes,UINT w,UINT h) {
    if(!CaptureAllowedHost())return;
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
// Compare full decoded RGB pixel planes against the visible source image.
// 1 = clockwise 90; 2 = 180; 3 = clockwise 270. No interpolation, crop,
// colour adjustment or metadata edit is allowed in this shortcut.
inline bool TryRotation(heif::Image& edited,UINT width,UINT height,
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
           std::chrono::steady_clock::now()-state.at>std::chrono::seconds(30))
            return false;
        w=state.width;h=state.height;
        const bool swapped=(width==h && height==w);
        const bool normal=(width==w && height==h);
        if(!swapped && !normal)return false;
        // A failed preliminary save must not permanently use up the
        // original HEIF before a later, valid rotated save can match it.
        source=state.original;
    }
    try {
        heif::Context context;
        context.read_from_memory(source.data(),source.size());
        auto original=context.get_primary_image_handle();
        if(original.get_width()!=int(w) || original.get_height()!=int(h) ||
           original.has_alpha_channel())return false;
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

        uint8_t confirmed=0;
        for(uint8_t transform=1;transform<=3;transform++){
            if((transform&1)?(width!=h || height!=w):
                             (width!=w || height!=h))continue;
            bool equal=true;
            for(UINT y=0;y<height && equal;y++){
                const uint8_t* outRow=after+size_t(y)*targetStride;
                for(UINT x=0;x<width;x++){
                    UINT sx=0,sy=0;
                    if(transform==1){ sx=y; sy=h-1-x; }
                    else if(transform==2){ sx=w-1-x; sy=h-1-y; }
                    else { sx=w-1-y; sy=x; }
                    const uint8_t* in=before+size_t(sy)*sourceStride+
                                      size_t(sx)*3;
                    if(std::memcmp(in,outRow+size_t(x)*3,3)!=0){
                        equal=false;
                        break;
                    }
                }
            }
            if(equal){
                // Symmetrical images can match more than one angle; guessing
                // which 90/180/270 transform the user requested is unsafe.
                if(confirmed)return false;
                confirmed=transform;
            }
        }
        if(!confirmed)return false;
        auto rotated=HeicLosslessNative::Transform(source,confirmed);
        if(rotated.size()!=source.size())return false;
        {
            auto& state=Current();
            std::lock_guard<std::mutex> lock(state.gate);
            // Do not invalidate a different candidate installed while
            // decode/compare was in progress.
            if(!state.ambiguous && state.original==source)
                state.original.clear();
        }
        output.swap(rotated);
        WicHeicDiagnostic::Emit("PhotosPureRotateLossless",
            width,height,confirmed,1,"exact-rgb-rotation");
        return true;
    } catch(...) {
        output.clear();
        return false;
    }
}

} // namespace PhotosPureRotationBridge
