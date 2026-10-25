# Windows 10標準フォト 1.1.24 — 一部HEICのみ保存失敗、1894枚フォルダーの表示揺れ（2026-10-25）

## 重大な実機事実
- 標準フォトで多くのHEICは回転保存可能だが、一枚だけ失敗。
- その他の写真では回転直後に画像が引き伸ばされる／画像領域が極端に細長くなり見切れる症状が残る。数秒後には正しい表示に戻る。
- ユーザーの通常テストフォルダーにはHEICが**約1894枚**。診断v0.4は元と別の作業フォルダーへ画像をコピーし、同じ実機フォトでも表示が崩れなかった。連打と縮小を交えても診断時は崩れない。**操作速度だけでは説明できない。**
- サムネイルの方向も、Photosの回転操作とほぼ同じ時期に更新される。1894枚分の列挙/表示処理、キャッシュ無効化、旧寸法を使った表示更新などが候補。**実際に1894枚全件を再デコードしている証拠はない。**

## 失敗画像のローカル読み取り専用解析（写真データはGitHubに含めない）
- user-provided sample ZIP is valid, one HEIC, 1,738,419 bytes.
- Independent libheif 1.19.8 `heif-info` and `heif-convert` both decode primary image successfully. View geometry 4032×3024, HEVC tiles 512×512 grid 8×6, 8-bit.
- HEIF primary `irot=2` (180° CCW), **TIFF Exif Orientation=3** (180°). Both present: double-transform is a compatibility risk; which interpretation Photos uses must be measured, not assumed.
- Embedded thumbnail 320×240; ICC profile. Apple HDR auxiliary gain map exists, with own grid, and `auxC = urn:com:apple:photo:2020:aux:hdrgainmap`. Related primary, thumbnail and gain-map `ipma` entries share same `irot` property.
- Additional independent auxiliary Exif `cdsc` reference: metadata item 64 → gain map item 63. Primary Exif metadata item 65 → primary item 49. This is important and is not merely a heuristic based on file size.

## Additional likely performance amplification at fallback saves
- `wic_heic/HeicBitmapFrameDecode.cpp::GetThumbnail` **does** read HEIC embedded thumbnails when present (this failing image has 320×240).
- `wic_heic/HeicBitmapFrameEncode.cpp::SetThumbnail` and `HeicBitmapEncoder.cpp::SetThumbnail` currently return `WINCODEC_ERR_UNSUPPORTEDOPERATION`, and the ordinary opaque-raster HEIC encoder produces a newly re-encoded primary item with no preserved embedded thumbnail. This is a potential **feedback loop**: rotate-save takes lossless fallback -> discards thumbnail -> thumbnail request now decodes much larger primary image.
- Microsoft explicitly warns that without a thumbnail at container/frame level, WIC must decode the full image and scale it, with large performance cost. https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapframeencode
- This **could** amplify CPU/RAM pressure in a folder with 1,894 HEIC images and repeated rotation/re-save, but no trace yet shows exactly how many thumbnails were invalidated or which PID does the work. Do not claim whole-folder re-decoding.

## Confirmed 1.1.24 source-level unsupported conditions, not yet final Photos save-failure cause
In `wic_heic/HeicLosslessNative.h`:
- `Transform` uses `LinkedVisualItems` to collect thumb/aux and then after `ExifIDs` explicitly rejects any `HasCdsc(exifId,relatedVisual)`: `Bad("Related image has independent Exif orientation")`. This supplied sample has exactly that situation (auxiliary gain-map Exif), so **the native metadata-only transform rejects it by design**.
- `NormalizeExif` also rejects TIFF Orientation 3: only 1 or 6/8 when old HEIF irot angle is odd are accepted. This sample has HEIF `irot=2` and EXIF=3. Even without gain map independent metadata, present code would reject Orientation=3.
- Thus current lossless path cannot rotate this file. **However this does not by itself prove why Photos' entire save fails**, since default raster re-encoding is supposed to be a fallback. Need the specific HRESULT/final `EncoderCommit` from failing save.
- Preserve fail-closed logic until gain-map Exif and primary Exif can both be updated without corrupting geometry, colors, metadata; do not replace user file or strip HDR just to get green tests.

## Concrete discriminating test, no new MSI or personal-photo GitHub upload
1. Create a **new file with a new name** in a small empty folder from a disposable copy of the failed HEIC. Do not modify original. Use Windows Photos 1.1.24 to try exactly one 90° rotate-save. Record precise Photos error message or screenshot **without private photo content** (crop screenshot to message). This checks whether save failure follows the file structure or 1894-file folder context.
2. For one other already-known-to-save HEIC, use two copies of the **same bytes**: one test copy in the 1894-item folder and a second in a newly created mostly empty folder. Same MSI/Photos conditions; compare whether visible distortions occur. No need to upload either full photo.
3. To discriminate the save-failure HRESULT from unsupported native lossy fallback, capture **one optional WIC trace** and small `00_Check_Installed_WIC.cmd` read-only status. Do not ask user to upload all 1894 photos or repeatedly install new MSI.
4. If repeated failures only within big folder, profile Windows Photos CPU/WorkingSet and thumbnail activities. Sysinternals Process Monitor can identify exactly which process creates/deletes `.tmp` and what file reads happen, but only after filtering to a test folder and scrubbing image paths, and only if necessary; default tool trace is cheaper.

## Cross-site evidence
- Microsoft Photos engineers' discussion: UWP vs Win32 folder enumeration and large-photo collections: https://blogs.windows.com/windowsdeveloper/2024/06/03/microsoft-photos-migrating-from-uwp-to-windows-app-sdk/
- Microsoft Shell thumbnail caching changes when file modified: https://learn.microsoft.com/en-us/windows/win32/shell/thumbnail-providers
- WIC thumbnail behaviour and full-frame decode fallback: https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapframeencode
- libheif support of aux images/tiles and grids: https://github.com/strukturag/libheif
- Apple HDR gain maps and auxiliary data: https://developer.apple.com/documentation/appkit/applying-apple-hdr-effect-to-your-photos
- HEIF+Exif double orientation risk: https://github.com/strukturag/libheif/issues/227

## Release obligation
10/27/2026 Tue 20:00 JST deadline unchanged. Previous 1.1.24 CI success is not proof that Windows 10 Photos rotation UI / save / thumbnail and HDR metadata problems are resolved.
