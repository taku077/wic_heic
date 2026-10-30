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
    // Pragmatic compromise: preserve the fast 4-worker cold thumbnail when
    // it is the only active eligible decode, but avoid injecting an additional
    // two worker threads while it is still running on a 4-logical-CPU PC.
    // Returning zero means libheif's DEFAULT codec behavior, NOT zero CPU.
    if (logicalCpus == 4u)
        return alreadyDecoding == 0u ? 4u : 0u;
    if (alreadyDecoding == 0u) return logicalCpus >= 4u ? 4u : 2u;
    if (alreadyDecoding == 1u && logicalCpus >= 6u) return 2u;
    return 0u;
}
// Explorer COM Surrogate CPU-pressure compromise for 2-core / 4-thread PCs.
// A 4-worker cold decode occupied ~3.46 logical cores in artificial CI;
// 2 workers occupied ~1.88 while consuming fewer total CPU milliseconds.
// Preserve every image-format safety gate and other process policies.
inline unsigned SelectForShellCpuBudget(unsigned w,unsigned h,int luma,
                                        int chroma,bool alpha,unsigned cpus,
                                        unsigned active) noexcept {
    const unsigned selected=Select(w,h,luma,chroma,alpha,cpus,active);
    return cpus==4u && selected==4u ? 2u : selected;
}
// Photos' UI needs CPU headroom. Preserve Explorer's 4-worker fast path.
// Zero means the unmodified libheif default, not zero codec threads.
inline unsigned SelectForPhotos(unsigned w,unsigned h,int luma,int chroma,
                                bool alpha,unsigned cpus,unsigned active) noexcept {
    if(cpus<4u || active!=0u) return 0u;
    return Select(w,h,luma,chroma,alpha,cpus,active)==4u ? 2u : 0u;
}
} // namespace HeicThumbnailWorkerPolicy
