#pragma once
// Fixed safety bounds for short-lived, in-process Windows Photos HEIF
// rotation-source candidates. No paths, user metadata, file I/O or image
// decoders are exposed by this policy header.
#include <chrono>
#include <cstddef>

namespace PhotosPureRotationBridge {
constexpr auto kSourceTtl=std::chrono::minutes(10);
constexpr size_t kMaxSourceCount=8;
constexpr size_t kMaxSourceBytes=96u*1024u*1024u;
// The Windows Photos HEIC save path often supplies only a final RGB bitmap.
// If the authentic source HEIF cannot be proven, re-encoding that bitmap can
// silently discard HDR auxiliary items and thumbnails. Reject instead.
// Other WIC hosts retain normal raster encoding, and an authenticated exact
// rotation remains lossless. This deliberately disallows uncertain Photos
// raster edits, trading availability for source-image integrity.
inline bool RefuseUnverifiedPhotosRasterSave(
    bool windowsPhotosHost,bool authenticatedLosslessSource) {
    return windowsPhotosHost && !authenticatedLosslessSource;
}
}
