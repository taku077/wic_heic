# WIC lossless HEIC integration — verified checkpoint

Date: 2026-10-20. Experimental branch ONLY; not a Photos-compatible release.

## Actual passing Windows CI
- **Run 39415649921**: first direct WIC Rotate90 + NoCompression produced an HEIC byte-for-byte identical to the Python orientation-only reference, 10,597 bytes, all decoded RGB differences zero.
- **Run 39418388686**: Windows WIC direct 90/180/270 produced three files each exactly 10,597 bytes, each byte-for-byte identical to its independent Python no-reencode reference. Source fixture unchanged. Linked auxiliary-image sample rejected with `WINCODEC_ERR_UNSUPPORTEDOPERATION` (0x88982F81). Normal JPEG/HEIC encoder regression and MSI temporary install/uninstall passed.
- **Run 39418868659**: added WIC flip-rotator-wrapped raster input as negative control; CI passed before and after MSI installation. Raster input does not provide original encoded HEVC, therefore no-recompress must **fail**, never silently re-encode.

All CI used PUBLIC SYNTHETIC samples. No user photographs or original source files were uploaded to GitHub. Windows GitHub runner is not Windows 10 Photos.

## Implemented
1. `HeicFrameOptions.h`: HEIF WIC `BitmapTransform` and `HeifCompressionMethod` options.
2. `HeicBitmapFrameDecode` exposes an internal read-only raw HEIF source capability only for primary image frames.
3. `HeicLosslessNative.h`: strict, fail-closed in-memory ISO-BMFF `irot` transform and EXIF orientation normalization.
4. Frame/encoder connects `WriteSource` with original compressed source access to `Commit` for exact byte-preserving output.
5. Explicit refusal of unrecognized HEIF items / unsupported `imir`, `clap`, shared transforms, linked thumbnail / auxiliary images, invalid sizes and raster-only sources when lossless rotation was requested.
6. Regression tests include pixel geometry, 90/180/270 output byte equality, source immutability, and MSI install/uninstall.

## What is NOT proven
- **Windows Photos may not request `BitmapTransform` or `HeifCompressionMethod`** and may pass only its already-rotated bitmap via `IWICBitmapFlipRotator`. If that happens, a genuine no-reencode path cannot be inferred from the pixels alone. This is the principal Photos integration blocker. Native Photos call tracing with disposable test fixtures remains necessary.
- The previous user's `IMG_4074` had an HDR gain map, embedded thumbnail, EXIF and Display P3. This branch intentionally **refuses** lossless rotation when the file contains related auxiliary visuals that cannot all be transformed consistently. Its real-world coverage is incomplete.
- Cropping and true pixel resizing remain re-encoding operations. EXIF/ICC/HDR preservation during re-encoding remains incomplete.
- The encoder cannot prevent an APPLICATION from opening/truncating the input pathname as its destination before calling the codec. A copy-only/safe-save application workflow is REQUIRED. No claim that this DLL alone prevents all original-file overwrites.
- The release build currently has `DbgLog` compiled out; no Photos-specific WIC invocation trace was collected.

## Next tasks
1. Add opt-in, privacy-minimal tracing of WIC encoder options and source-type negotiation, then check what Windows 10 Photos actually passes, **using disposable synthetic HEICs only**.
2. If Photos passes source `IWICBitmapFrameDecode` + options, validate in-place user workflow safety and EXIF/HDR parity before allowing any real image edits.
3. If Photos passes a raster-only source, don't infer original compressed item provenance. Explore a WIC fast metadata/metadata-handler route or separate app integration instead.
4. Implement coherent primary + thumbnail + HDR auxiliary transforms with independent rendering verification, or keep failing safely.
5. Create dedicated Windows 10 Photos human-visible and numerical acceptance tests, including four rotations, resize, native thumbnails, metadata, original-file hashes, and cancellation/recovery.

## Release policy
No merge to master, do not deploy the experimental MSI to user's Windows 10 yet, do not edit originals, and do not claim JPEG parity or Photos support until the above conditions are verified.
