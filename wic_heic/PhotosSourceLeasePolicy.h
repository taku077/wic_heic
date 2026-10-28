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
}
