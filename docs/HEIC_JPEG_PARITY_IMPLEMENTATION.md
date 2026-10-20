# HEIC / JPEG editing parity — lossless rotation implementation

Status: **phase 1 implementation tested, not integrated with Windows Photos**.

User requirement: HEIC rotation should behave like JPEG's demonstrated metadata-only rotation. Do not change or replace the original photo. Preserve EXIF, ICC / Display P3, HDR gain maps, XMP, MakerNote, thumbnails and unrelated HEIF items. A return status indicating success must never conceal malformed frame geometry, dropped metadata or unintended recompression.

## Source findings
- The current custom WIC \`CHeicBitmapEncoder::Commit\` always calls \`m_Context.encode_image\`, producing new HEVC data even for a 90-degree rotation. Thus Photos can lose EXIF/ICC/HDR on re-encode.
- The normal Windows Photos JPEG path observed in the A/B trials kept the JPEG compressed raster identical and updated EXIF Orientation. This is an observed behavior in those trials, not a claim that every JPEG edit is metadata-only.
- Microsoft's WIC HEIF encoder documents the encoder options \`BitmapTransform\` (VT_UI1) and \`HeifCompressionMethod\` (VT_UI1), with \`WICHeifCompressionNone\` to require no recompression, and \`WINCODEC_ERR_UNSUPPORTEDOPERATION\` for edits that cannot be performed without recompression. https://learn.microsoft.com/windows/win32/wic/heif-codec
- The Microsoft HEIF WIC implementation is **not** identical to this open-source codec. Supporting the properties in this plugin is necessary but not sufficient to prove Photos will use the lossless path.

## Phase 1: metadata-only rotation engine — implemented
See \`tools/lossless_heic_rotate_experimental.py\` and \`tools/test_lossless_heic_rotate.py\`.

- Detect HEIF BMFF primary item via \`pitm\`, \`iprp/ipco/ipma\`, and existing \`irot\` association.
- Rotate clockwise 90 degrees by decrementing HEIF's counterclockwise quarter-turn index modulo four.
- Follow \`iinf\` / \`iref\` / \`iloc\` to primary EXIF item and normalize redundant Orientation 6/8 to 1 when metadata is already rotated.
- No HEVC bitstream encoding, no change to ICC, thumbnails, HDR gain maps, other metadata items, or container length. Only the located orientation byte(s) may be changed.
- Reject unknown transforms (\`imir\` or \`clap\`), missing/ambiguous \`irot\`, ambiguous EXIF orientation, unexpected layout, corrupt bounds.
- Always create an exclusive new file; refuse existing destinations and identical input/output. Never overwrite source.
- Four 90-degree cycles and malformed input rejection are CI-tested on a **synthetic public fixture**. Sensitive user files are never pushed to GitHub.
- The real privately supplied restoration candidate was also tested offline: rotation altered exactly two bytes, maintained all other bytes, HEIC decoded to exact 90-degree rotation with full-resolution RGB pixel MAE = 0.0 (not an estimate), and the source SHA remained unchanged. The HDR and ICC bytes were preserved by exhaustive byte diff.

## Phase 2: WIC integration — NOT DONE
1. Implement and test an actual \`IPropertyBag2\` for \`CreateNewFrame\` with the documented \`BitmapTransform\` and \`HeifCompressionMethod\` options and strict explicit rejection when an operation cannot be made lossless.
2. Establish whether Windows Photos supplies the original HEIC bitstream or only decoded pixels to the encoder, and whether it writes \`BitmapTransform\`. A generic \`IWICBitmapSource\` raster alone does not contain enough information to re-use the original HEVC bitstream.
3. Introduce a verifiable source bitstream transfer only when source identity is established, and issue output atomically to a **different path** until full non-destructive Photos integration has been proven.
4. Support the no-\`irot\` HEIF case (may require safely rebuilding BMFF offsets), EXIF missing/varied forms, multi-picture and transformed files, preserving ICC/HDR subimages.
5. Preserve metadata and color context in the re-encoding fallback required for resizing, and test repeated edits and both WIC \`WriteSource\` and \`WritePixels\` pathways.
6. Verify Windows 10 Photos end-to-end using disposable test copies and logs. Do not install a replacement MSI on the user's actual machine until both automated and manual acceptance gates pass.

## Release gate
No production merge and no claim of full JPEG parity until:
- lossless CW90/CCW90/180 repeat tests, SHA of all compressed items unchanged, EXIF/ICC/XMP/HDR/thumbnail retained;
- no overwrite of originals under any workflow; temporary output failures never replace source;
- pixel geometry, displayed orientation, Explorer thumbnails, Photos opening view and file properties are verified on Windows 10 22H2;
- crop/resize fallback never silently corrupts or drops metadata, and unrepresentable lossless operations fail explicitly.

**No changes to HEIC Manager, user files, or an installed WIC codec from this branch.**
