#pragma once
// Only normalize the metadata *exposed through WIC*; the source HEIC is never modified.
// libheif has already applied a 90/270 degree HEIF item transform to the pixels
// when its display dimensions are swapped relative to the original ispe dimensions.
// Exposing an additional EXIF 90-degree rotation to Photos can rotate the image twice.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace WicHeicOrientation {
inline uint16_t Get16(const std::vector<uint8_t>& b, size_t p, bool le) {
    return le ? uint16_t(b[p] | (uint16_t(b[p+1]) << 8))
              : uint16_t((uint16_t(b[p]) << 8) | b[p+1]);
}
inline uint32_t Get32(const std::vector<uint8_t>& b, size_t p, bool le) {
    return le ? (uint32_t(Get16(b,p,true)) | (uint32_t(Get16(b,p+2,true)) << 16))
              : ((uint32_t(Get16(b,p,false)) << 16) | Get16(b,p+2,false));
}

// libheif's Exif item starts with a four-byte TIFF offset that the existing
// metadata reader strips; TIFF starts at byte 4 in the data returned by libheif.
// Modify only Orientation=6 or 8, SHORT/count=1, in IFD0; preserve all other data.
// This does NOT flatten pixels or affect the HEIC file or its embedded metadata.
inline bool NormalizeExifForRotatedPixels(std::vector<uint8_t>& b, bool dimensionsSwapped) {
    const size_t base = 4;
    if (!dimensionsSwapped || b.size() < base + 8) return false;
    bool le;
    if (b[base]=='I' && b[base+1]=='I') le=true;
    else if (b[base]=='M' && b[base+1]=='M') le=false;
    else return false;
    if (Get16(b,base+2,le)!=42) return false;
    const uint32_t ifdOffset=Get32(b,base+4,le);
    if (ifdOffset < 8 || ifdOffset > b.size()-base ||
        (b.size()-base-ifdOffset) < 2) return false;
    const size_t dir=base+size_t(ifdOffset);
    const uint16_t n=Get16(b,dir,le);
    const size_t available=(b.size()-dir-2)/12;
    if (n>available) return false;
    for (size_t i=0;i<n;++i) {
        const size_t entry=dir+2+i*12;
        if (Get16(b,entry,le)!=274) continue; // EXIF Orientation
        if (Get16(b,entry+2,le)!=3 || Get32(b,entry+4,le)!=1) return false;
        const uint16_t orientation=Get16(b,entry+8,le);
        if (orientation!=6 && orientation!=8) return false;
        if (le) { b[entry+8]=1; b[entry+9]=0; }
        else { b[entry+8]=0; b[entry+9]=1; }
        return true;
    }
    return false;
}
} // namespace WicHeicOrientation
