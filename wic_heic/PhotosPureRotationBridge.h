#pragma once
// Conservative in-process bridge for Windows Photos' default/no-options WIC
// raster save path. Never writes to input streams or the file system.
// No image data, file names, EXIF, or paths in trace output.
//
// A decoder may offer one short-lived original HEIF. EncoderCommit can use it
// ONLY if ALL RGB pixels match EXACTLY ONE 0/90/180/270 degree rotation of
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
#include <utility>

namespace PhotosPureRotationBridge {
// Photos can preload nearby images. One unrelated decoder must not permanently
// disable lossless saving for the active photo. Keep a bounded, short-lived set
// and accept a save only when ONE source and ONE angle match every RGB pixel.
// The store contains no file names, disk paths or persistent data.
struct Candidate {
    std::vector<uint8_t> original;
    UINT width=0,height=0;
    std::chrono::steady_clock::time_point at{};
};
struct Store {
    std::mutex gate;
    std::vector<Candidate> candidates;
    uint64_t revision=0;
};
inline Store& Current() {static Store store;return store;}
inline void Prune(Store& state,std::chrono::steady_clock::time_point now) {
    const auto oldSize=state.candidates.size();
    state.candidates.erase(
        std::remove_if(state.candidates.begin(),state.candidates.end(),
            [&](const Candidate& c) {
                return now-c.at>std::chrono::seconds(30);
            }),
        state.candidates.end());
    if(state.candidates.size()!=oldSize)++state.revision;
}
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
    Prune(state,now);
    for(auto& c:state.candidates) {
        if(c.width==w && c.height==h && c.original==bytes) {
            c.at=now; // same HEIF re-opened, not a competing candidate
            return;
        }
    }
    // Bound memory/CPU under Photos folder preloading. If the active source
    // is evicted, lossless matching simply declines and raster save proceeds.
    state.candidates.push_back(Candidate{bytes,w,h,now});
    ++state.revision;
    size_t total=0;
    for(const auto& c:state.candidates)total+=c.original.size();
    constexpr size_t maxBytes=64u*1024u*1024u;
    constexpr size_t maxCount=4;
    while(state.candidates.size()>maxCount || total>maxBytes) {
        total-=state.candidates.front().original.size();
        state.candidates.erase(state.candidates.begin());
        ++state.revision;
    }
}
// Compare full decoded RGB pixel planes against the visible source image.
// 0 = unchanged after 360 degrees; 1 = clockwise 90; 2 = 180; 3 = CW270.
// No interpolation, crop, colour adjustment or metadata edit is permitted.
// For 0 degrees return byte-identical HEIF; never encode a noop raster.
inline bool TryRotation(heif::Image& edited,UINT width,UINT height,
                        const std::vector<uint8_t>& explicitICC,
                        HeicLosslessNative::Bytes& output) {
    output.clear();
    if(!width || !height || width>20000 || height>20000)return false;
    std::vector<Candidate> possible;
    uint64_t revision=0;
    {
        auto& state=Current();
        std::lock_guard<std::mutex> lock(state.gate);
        Prune(state,std::chrono::steady_clock::now());
        revision=state.revision;
        for(const auto& c:state.candidates){
            const bool swapped=(width==c.height && height==c.width);
            const bool normal=(width==c.width && height==c.height);
            if(swapped || normal)possible.push_back(c);
        }
    }
    if(possible.empty())return false;
    try {
        int targetStride=0;
        auto* after=edited.get_plane(heif_channel_interleaved,&targetStride);
        if(!after || targetStride<int(width)*3)return false;
        bool matched=false;
        uint8_t confirmed=255;
        std::vector<uint8_t> source;
        for(const auto& candidate:possible) {
            const UINT w=candidate.width,h=candidate.height;
            heif::Context context;
            context.read_from_memory(candidate.original.data(),
                                     candidate.original.size());
            auto original=context.get_primary_image_handle();
            if(original.get_width()!=int(w) || original.get_height()!=int(h) ||
               original.has_alpha_channel())return false; // fail closed
            if(!explicitICC.empty()){
                const auto* raw=original.get_raw_image_handle();
                const auto n=heif_image_handle_get_raw_color_profile_size(raw);
                if(n!=explicitICC.size())continue;
                std::vector<uint8_t> icc(n);
                const auto err=heif_image_handle_get_raw_color_profile(raw,
                                                                        icc.data());
                if(err.code!=heif_error_Ok || icc!=explicitICC)continue;
            }
            auto rgb=original.decode_image(heif_colorspace_RGB,
                                           heif_chroma_interleaved_RGB);
            size_t sourceStride=0;
            auto* before=rgb.get_plane2(heif_channel_interleaved,
                                        &sourceStride);
            if(!before || sourceStride<size_t(w)*3)return false;
            for(uint8_t transform=0;transform<=3;transform++){
                if((transform&1)?(width!=h || height!=w):
                                 (width!=w || height!=h))continue;
                bool equal=true;
                for(UINT y=0;y<height && equal;y++){
                    const uint8_t* outRow=after+size_t(y)*targetStride;
                    for(UINT x=0;x<width;x++){
                        UINT sx=0,sy=0;
                        if(transform==0){sx=x;sy=y;}
                        else if(transform==1){sx=y;sy=h-1-x;}
                        else if(transform==2){sx=w-1-x;sy=h-1-y;}
                        else {sx=w-1-y;sy=x;}
                        const uint8_t* in=before+size_t(sy)*sourceStride+
                                          size_t(sx)*3;
                        if(std::memcmp(in,outRow+size_t(x)*3,3)!=0){
                            equal=false;
                            break;
                        }
                    }
                }
                if(equal){
                    // Same pixel result from different HEIF metadata (or a
                    // symmetric image/angle) is ambiguous: never guess.
                    if(matched)return false;
                    matched=true;
                    confirmed=transform;
                    source=candidate.original;
                }
            }
        }
        if(!matched)return false;
        auto result=(confirmed==0)?source:
            HeicLosslessNative::Transform(source,confirmed);
        if(result.size()!=source.size())return false;
        {
            auto& state=Current();
            std::lock_guard<std::mutex> lock(state.gate);
            // Another image may have been offered while comparing. In that
            // case the complete uniqueness proof is stale: fail closed.
            if(state.revision!=revision)return false;
            const auto it=std::find_if(state.candidates.begin(),
                                      state.candidates.end(),
                [&](const Candidate& c){return c.original==source;});
            if(it==state.candidates.end())return false;
            state.candidates.erase(it);
            ++state.revision;
        }
        output.swap(result);
        WicHeicDiagnostic::Emit("PhotosPureRotateLossless",
            width,height,confirmed,1,
            confirmed==0?"exact-rgb-identity":"exact-rgb-rotation");
        return true;
    } catch(...) {
        output.clear();
        return false;
    }
}

} // namespace PhotosPureRotationBridge
