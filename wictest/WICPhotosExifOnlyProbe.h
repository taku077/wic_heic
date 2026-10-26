#pragma once
// Verify that Photos-style Exif normalization ALONE is not a 90-degree
// bitmap rotation. This narrow test uses only the SHA-pinned artificial
// two-frame HEIC and one in-memory Exif byte adjustment; no user files or
// output paths, no false claim that "save succeeded" means "rotated".
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cstdio>

namespace WICPhotosExifOnlyProbe {
inline void Run() {
    const auto source=WICPhotosOpaqueRotationProbe::Read(
        L"samples\\fixture-irot3-exif6-noaux.heic");
    // This pair of offsets applies ONLY to the pinned 10,597-byte fixture.
    // Do NOT use it as a general HEIF parser. Both were independently
    // confirmed against the actual synthetic Photos 1.1.27 saved output.
    constexpr size_t irotAngleAt=567, exifOrientationAt=10589;
    if(source.size()!=10597 || source[irotAngleAt]!=3 ||
       source[exifOrientationAt]!=6)
        throw std::runtime_error("Unexpected synthetic fixture EXIF/irot layout");
    auto exifOnly=source;
    exifOnly[exifOrientationAt]=1;  // observed user Photos save mutation
    heif::Context originalContext,normalizedContext;
    originalContext.read_from_memory(source.data(),source.size());
    normalizedContext.read_from_memory(exifOnly.data(),exifOnly.size());
    if(originalContext.get_number_of_top_level_images()!=2 ||
       normalizedContext.get_number_of_top_level_images()!=2)
        throw std::runtime_error("Exif-only edit altered HEIF image count");
    auto before=originalContext.get_primary_image_handle();
    auto after=normalizedContext.get_primary_image_handle();
    if(before.get_width()!=after.get_width() ||
       before.get_height()!=after.get_height())
        throw std::runtime_error("Exif-only normalization changed primary geometry");
    auto rgbA=before.decode_image(heif_colorspace_RGB,
                                heif_chroma_interleaved_RGB);
    auto rgbB=after.decode_image(heif_colorspace_RGB,
                               heif_chroma_interleaved_RGB);
    size_t strideA=0,strideB=0;
    const auto* a=rgbA.get_plane2(heif_channel_interleaved,&strideA);
    const auto* b=rgbB.get_plane2(heif_channel_interleaved,&strideB);
    if(!a || !b || strideA<size_t(before.get_width())*3 ||
       strideB<size_t(after.get_width())*3)
        throw std::runtime_error("Exif-only primary RGB not available");
    for(int y=0;y<before.get_height();y++)
        if(std::memcmp(a+size_t(y)*strideA,
                       b+size_t(y)*strideB,
                       size_t(before.get_width())*3)!=0)
            throw std::runtime_error("Exif-only rewrite unexpectedly changed pixels");
    if(source[irotAngleAt]!=exifOnly[irotAngleAt])
        throw std::runtime_error("Unexpected irot change in Exif-only comparison");
    printf("WIC_PHOTOS_EXIF_ONLY_NOT_90_DEGREE_ROTATION_PASS bytes=%zu "
           "irot=3 exif=6_to_1 same_size=1 same_primary_rgb=1\n",source.size());
}
} // namespace WICPhotosExifOnlyProbe
