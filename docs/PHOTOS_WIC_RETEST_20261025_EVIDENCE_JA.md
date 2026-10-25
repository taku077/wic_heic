# Windows 10 標準フォト HEIC 回転保存・実機再テスト検証記録 (2026-10-25)

**原本・アップロード画像・生の診断ZIP・Exif/GPS・画像ファイルのSHA256はGitHubに保管しない。** 本文はユーザー了承済みの実機実験から得た構造・統計・WICイベントのみ。User's uploaded original and working copy were inspected read-only outside repository. The old photos are not used.

## 1. 結論 — 元の寸法不一致は今回は再現せず、再圧縮とメタデータ喪失は再現した
- Test: user uploaded HEIC_Rotation_Retest_v0_2 ZIP. Working copy was rotated and saved in Windows 10 Photos, with WIC trace.
- **WIC `SetSize(4032,3024)`、`WriteSourceRaster`、`FrameCommit(raster)`、`EncoderCommitRecompressed(hevc-encoded)`まで1つのプロセスで記録した。**
- Input WIC frame dimensions 3024×4032 → output WIC frame dimensions **4032×3024**. Thus previous “SetSize was landscape but final file portrait” did not recur in this test; do not conflate two separate test inputs.
- `WriteSourceInputClass=opaque-raster`; `CreateNewFrame=no-bag`; `FrameInitializeOptions=default`, `transform=0`, `compression=0`. **Photos did not request the WIC `BitmapTransform` or no-compression property options in this observed save.** The WIC encoder cannot assume that an opaque raster is the original compressed HEIC.
- Process PID alone does not prove process executable identity, but these events are temporally contiguous with the requested save and content/geometry changes.

## 2. Read-only mechanical file checks

| Measurement | Before | After |
|---|---:|---:|
| File bytes | 2,100,886 | 1,515,195 |
| File size retained | | **72.12%** |
| HEIF mdat payload | 2,092,118 | 1,514,279 |
| HEIF meta box | 8,716 | 880 |
| HEIF main image | 3024×4032, grid 6×8 of 512px tiles | 4032×3024 single image |
| Image irot | 3 | missing |
| Exif data | 2472 bytes | missing |
| XMP data | 2543 bytes | missing |
| ICC raw color profile bytes | 536 | 536, identical SHA256 |
| Decoder color depth | 8-bit RGB | 8-bit RGB |

Metadata disappearance observed independently with installed libheif 1.19.8 `heif-info`. ICC equality checked with the `libheif` raw profile C API, not inferred from both being labelled `prof`.
- Source size minus output size: 585,691 bytes; 577,839 bytes of this difference are within `mdat`, 7,836 bytes within `meta`; an 8-bit profile remains in this test. The file shrinkage is NOT caused by reducing displayed pixel count.
- Independent `heif-convert` to 8-bit PNG and full-frame decoded pixel comparison:
  - Source rotated 90° clockwise versus saved output: same dimensions; mean absolute RGB channel error 1.5468 (scale 0–255), PSNR 41.689dB, sampled correlation about 0.999355; exact 3-channel pixels 18.024%. Nonzero differences prove the result is **not** pixel-lossless.
  - CCW90 hypothesis: MAE 69.085 and PSNR 9.702dB, so the actual save matches **CW90** orientation.
- These data prove full HEVC recompression, bitstream and metadata differences. File shrink alone does not quantify perceptual loss.

## 3. Source-level cause and constraints

In `wic_heic/HeicBitmapFrameEncode.cpp`:
- `WriteSource()` has a strictly authenticated, intact HEIF source path via internal `IID_IHeicRawSource` and, when transform options are provided, `HeicLosslessNative::Transform()`.
- Photos passed `opaque-raster`, *not* the authenticated original frame, so the lossless path was inaccessible. A plain WIC raster lacks trustworthy compressed bitstream provenance and a reliable unambiguous edit descriptor.
- `GetMetadataQueryWriter()` returns `E_NOTIMPL`, and `SetThumbnail()` rejects preview thumbnails. In this observed save, the new HEIF was written with only the explicit ICC color profile. This explains missing Exif and XMP, rather than blaming only quality defaults.
- Generic HEIF raster encoding in `wic_heic/HeicBitmapEncoder.cpp` reconstructs a primary image with new HEVC bits. 1.1.22's explicit lossy quality 92 is a *mitigation*, not the root cause fix.
- A native HEIC rotation of `irot`+Exif without recompression is known to work where authenticated source bytes are available. That does **not** demonstrate that Photos itself will submit that source/option path.

## 4. Relevant independent specification
- https://learn.microsoft.com/en-us/windows/win32/wic/heif-codec: MS HEIF specifies option bag `HeifCompressionMethod` and `BitmapTransform`; no recompression can be forced if app requests it; `Commit` fails for unsuitable raster sources.
- https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapframeencode: `WriteSource` consumes IWICBitmapSource, `Commit` writes the stream.
- https://github.com/strukturag/libheif: HEIF tiled images, transformations, metadata APIs.
- https://nokiatech.github.io/heif/technical.html: HEIF item properties and `irot`.

## 5. Concrete next engineering step / safety
1. Reproduce a **Photos-like default/no-property-bag opaque WIC raster CW90** using **public synthetic HEIC** in Windows CI. Confirm raster path and current metadata loss without putting user photos into git or tests.
2. For metadata-only rotation, investigate only *authenticated* ways to correlate incoming rotated raster with the particular source HEIF and requested transform. If not provable, do **not** swap in HEIF metadata-only edits: this could discard real edits (crop/brightness), or rotate unrelated image content.
3. Improve explicit fallback handling of Exif/XMP when actually available and safe, without promising MakerNotes/gain map or pretending output is lossless; do not undermine standard Photos ability to save.
4. Any new MSI must remain a separate version, have rollback validated for user's Windows 10 19045, and preserve HEIC Manager isolation. The deadline 2026-10-27 20:00 JST stays fixed. Latest 1.1.22 RC has not been validated via Windows 10 Photos UI; old 1.1.21 user's CI log is what this report describes.

## 6. Questions this test answers
- Is the user actually able to rotate HEIC with Windows Photos? **Yes in this tested case.**
- Does the rotated final file have wrong landscape dimensions? **No in this tested case.**
- Was it saved by our HEIC encoder through a raster-recompression path? **Yes, the WIC trace observed a completed re-encoding transaction.** Windows Photos process identity remains inference, not independently verified PID→exe.
- Are the original EXIF/XMP and bitstream preserved? **No.** ICC profile is preserved byte-for-byte in this case.
- Can increasing lossy encoder quality solve the actual metadata-only rotation? **No.** It may reduce raster generational loss but cannot restore the absent source bitstream/metadata.
