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

## Follow-up: initial display/Explorer thumbnail appears sideways after user's CW90 test

User reports that an originally portrait HEIC subsequently appears landscape in Explorer thumbnails and immediately upon opening in Photos.

Cross-check of BOTH privately provided 2026-10-20 experiment ZIPs (only properties/hashes reported; NO private image bytes committed):
- In each experiment `HEIC_REFERENCE_DO_NOT_EDIT.heic` SHA-256 exactly matches the pre-test `OriginalHeicSHA256` in `PREPARED_STATE.json`. The prepared reference copy was NOT changed by the rotation test.
- The respective `HEIC_ROTATE_IN_PHOTOS.heic` SHA differs from the pre-test working SHA; these edited working files were changed.
- The source HEIC displays at 3024x4032 and has primary HEIF rotation `irot=3`, plus an embedded thumbnail and Exif metadata.
- The edited HEIC displays at 4032x3024, has no `irot`, no embedded thumbnail and no Exif metadata. It stores the landscape appearance in its re-encoded pixels. Both independent libheif and ffprobe indicate 4032x3024.
- Hence a landscape thumbnail/initial display **of the edited working copy** agrees with the saved raster; this alone is NOT evidence of a broken thumbnail or incorrect orientation. JPEG test working copy still stores 3024x4032 and EXIF Orientation=6, likewise requesting landscape presentation without recoding pixels.
- We CANNOT infer from these ZIPs whether the user's untouched source file elsewhere on their PC, the prepared reference copy shown in Explorer, or a different image unexpectedly changed orientation. That must be established by the exact filename, SHA-256 and location of the affected file.
- The edited HEIC lost its embedded thumbnail; Explorer may generate one. A cache mismatch is possible in principle but NOT established by these samples.

Additional acceptance criteria:
1. Distinguish reference/original vs edited working copy in the test UI and logs; never compare an intentionally rotated output to an unrotated display expectation.
2. Check HEIF item primary orientation, EXIF Orientation, WIC decoded dimensions, embedded thumbnail orientation and Shell thumbnail/cache separately before touching decoder code.
3. For metadata-only rotation, output should appear landscape after CW90 while original compressed raster and pre-test original remain bit-identical; rendering orientation is changed without recompression.
4. Four successive clockwise 90-degree edits on copies must restore original upright portrait view, compressed item hashes unchanged, no thumbnail staleness or EXIF double rotation, and no loss of unrelated metadata.
5. Do not install/release another MSI to 'correct' an edited copy's expected landscape display without a failing invariant.
