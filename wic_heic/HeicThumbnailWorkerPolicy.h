#pragma once
// Conservative HEVC codec-thread *hint* for isolated thumbnail research.
// Does not mutate libheif global settings, WIC registration, image metadata,
// Windows Photos transforms or Explorer's thumbnail cache.
#include <cstdint>

namespace HeicThumbnailWorkerPolicy {
inline unsigned Select(unsigned width, unsigned height, int lumaBits,
                       int chromaBits, bool hasAlpha, unsigned logicalCpus,
                       unsigned alreadyDecoding) noexcept {
    // Unknown bit depth, 10/12bit, alpha and smaller images always use
    // the existing decoder path without a codec-thread override.
    if (!width || !height || lumaBits != 8 || chromaBits != 8 || hasAlpha ||
        uint64_t(width) * uint64_t(height) < 6000000ull ||
        logicalCpus < 2u) return 0u;
    // No oversubscription from requesting 4 workers per concurrent image.
    // On small CPUs never request 4 codec workers.
    if (alreadyDecoding == 0u) return logicalCpus >= 4u ? 4u : 2u;
    if (alreadyDecoding == 1u && logicalCpus >= 4u) return 2u;
    return 0u;
}
} // namespace HeicThumbnailWorkerPolicy
