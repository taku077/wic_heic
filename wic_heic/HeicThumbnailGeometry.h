#pragma once
// Avoid returning an embedded thumbnail whose display orientation or aspect
// ratio is inconsistent with the primary HEIC. Such thumbnails can otherwise
// appear rotated relative to Windows Photos and the full decoded picture.
// Conservative fallback: WIC may render the primary frame instead.
#include <algorithm>
#include <cstdint>

namespace HeicThumbnailGeometry {
inline bool Compatible(int primaryW,int primaryH,int thumbW,int thumbH) noexcept {
    if(primaryW<=0 || primaryH<=0 || thumbW<=0 || thumbH<=0) return false;
    const uint64_t lhs=uint64_t(primaryW)*uint64_t(thumbH);
    const uint64_t rhs=uint64_t(primaryH)*uint64_t(thumbW);
    const uint64_t high=std::max(lhs,rhs);
    const uint64_t low=std::min(lhs,rhs);
    // Permit less than 2 percent discrepancy for rounded thumbnail sizes.
    return 100*(high-low)<=2*high;
}
}
