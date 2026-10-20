# Lossless HEIC rotation feasibility (2026-10-20, diagnosis only)

## Scope
Investigation of user-reported intermittent geometric deformation when Windows 10 Photos repeatedly rotates/resizes HEIC images with this project's experimental WIC implementation. The original image, copies, and any private image bytes must **not** be uploaded to the public GitHub repository.

## Confirmed file comparisons (private supplied JPEG/HEIC A/B experiment; only aggregate measurements recorded here)
- Windows Photos saved JPEG with unchanged underlying dimensions 3024x4032 and EXIF Orientation=6. Fully decoded underlying JPEG pixels matched the pre-rotation JPEG **exactly**, RGB MAE=0.000.
- Windows Photos saved HEIC with raster dimensions 4032x3024, without HEIF orientation transformation and without original EXIF; the HEVC image was re-encoded.
- The HEIC file's final decoded RGB pixels were correctly rotated for this **specific** trial; full-resolution RGB MAE=2.831/255 against a mathematical CW90 transform of the independently decoded original. Repeated-edit distortion reported by the user has **not** been reproduced in this sample.
- Current WIC encoder `CHeicBitmapEncoder::Commit` invokes `m_Context.encode_image` for every frame, inherently re-encoding images rather than retaining original compressed items. `CreateNewFrame` does not implement an option property bag; frame metadata writing is unimplemented. This differs from the metadata-only JPEG path observed in Photos.

## Strong metadata-only feasibility proof on a PRIVATE COPY (not committed)
Original HEIC:
- HEIF primary item irot value 3 (270 degrees CCW), EXIF Orientation 6.
- Image container 4,015,189 bytes.
Perform on an isolated copy:
- Change irot to 2 (180 degrees CCW) for the additional 90 degrees CW rotation.
- Change EXIF Orientation 6 to 1 to avoid double application in a decoder that also reads EXIF.
- Exactly **two bytes** in the entire file differ; compressed HEVC image bytes are unchanged. Resulting file size is unchanged.
- Independent libheif decode yields 4032x3024 with **pixel-for-pixel identity (MAE=0, max error=0)** to a true 90-degree CW rotation of the original decoded raster.
- EXIF and color profile remain present. This is a demonstration for that source image, not a generic safe patch for all HEIF containers.

## Relevant Microsoft WIC interfaces
https://learn.microsoft.com/windows/win32/wic/heif-codec
- Microsoft's HEIF extension codec documents `BitmapTransform` with `HeifCompressionMethod=WICHeifCompressionNone` as a way to require no recompression in supported cases, with failure for unsupported transformations. This documentation describes Microsoft's codec and does NOT imply it works in this custom codec, nor that Windows 10 Photos will automatically use it.
https://learn.microsoft.com/windows/win32/api/wincodec/nn-wincodec-iwicfastmetadataencoder
- `IWICFastMetadataEncoder` supports in-place metadata updates for supported formats, but format support is conditional.

## Engineering direction and acceptance criteria (NOT YET IMPLEMENTED)
1. Identify precisely how Windows Photos calls the custom HEIC WIC frame decoder/encoder during editing: source object type, `CreateNewFrame` option bag, `Initialize`, `SetSize`, source dimensions, `CopyPixels` calls and return statuses. Do not assume Photos passes original HEVC items or a `BitmapTransform` option.
2. Implement separate image-item-level lossless orientation support (HEIF `irot` / `imir` with EXIF Orientation coherence) with parser-derived item/property references. Avoid constant offsets, blind byte patches, altering original files, or treating every HEIC as a single-frame raster. Support identity, CW90, CCW90, 180 and repeated rotations; if unsafe/unrepresentable, fail without silently re-encoding.
3. Investigate exposing WIC `BitmapTransform` / no-recompress options via `IPropertyBag2` and/or fast metadata; prove Windows Photos actually invokes the capability. If Photos only supplies a raster rather than the HEIF source, merely implementing options cannot guarantee native Photos metadata-only rotation.
4. Cropping may sometimes be expressible by HEIF clean-aperture metadata; true resizing and many edits inherently require pixel processing and may require re-encoding. They cannot all be metadata-only like rotation.
5. Independently harden the fallback re-encode path: compare source and frame dimensions, validate stride and buffer bounds, check `pConverted->CopyPixels` HRESULT, ensure format negotiation, preserve metadata and fail without corrupting originals.
6. Test original compressed item SHA equality in metadata-only saves; verify render orientation and pixel equality, thumbnails, EXIF/ICC/GPS preservation, repeated rotation 0->90->180->270->0, and non-square image geometry. Test Photos on Windows 10 separately.
7. No production patch, merge, installer, or claim of Photos parity until actual integrated behavior is proven on the target machine.

Current installed WIC should remain unchanged for diagnosis.
