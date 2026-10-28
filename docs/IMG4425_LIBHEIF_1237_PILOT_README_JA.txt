IMG_4425 WIC/HEIC pilot MSI - libheif 1.23.7
==================================================
Release candidate only. Windows 10 Photos compatibility unverified.
Source branch: candidate/img4425-libheif-safe-photos-fallback-20261029.

Changes:
1. libheif 1.23.7 security and multithreaded-grid decoding fixes.
2. Exact proven lossless rotations preserve the original HEIF container,
   including its EXIF, ICC, HDR auxiliary images and embedded thumbnails.
3. If Windows Photos submits a default-option RGB bitmap and an authentic
   original HEIC cannot be matched, this codec deliberately refuses that
   Photos save instead of silently re-encoding to a new single-frame HEVC
   image and losing possible HDR/thumbnail/metadata.
4. Numeric WIC trace reports a specific reason for this refusal.

IMPORTANT TRADEOFF:
This strict Photos-only guard may also refuse legitimate non-rotation pixel
edits of HEIC, including brightness/color/crop changes. Those edits cannot
currently preserve an unknown original HEIF graph. Other WIC applications
and explicit raster conversions continue working as before.
If WIC never sees the original HEIC, the guard cannot reconstruct the
original file; it prevents a risky Photos default raster save.
This is NOT proof that all historic IMG_4425 defects were fixed.

Installation/testing:
- Keep the previously installed MSI/rollback method.
- Close Photos/Explorer windows, then install WicHeic-HEIC-x64.msi.
- Test on a COPY of IMG_4425.HEIC only. Never overwrite the original.
- First test open and Explorer thumbnail; then rotate CW90, save,
  reopen, reverse rotation and compare metadata/HDR/embedded thumbnail.
- A save failure may be intentional if origin was not authenticated.
  Keep both the error text and WIC numeric log for diagnosis.
- If any corruption, blank thumbnail, HEVC warning or regression occurs,
  stop testing and restore the prior MSI.

Version: WIC test MSI 1.1.29 (x64); libheif 1.23.7.
Provenance: IMG4425_PILOT_SHA256.txt generated in the same build run.
No Windows 10 Photos real-device test has yet occurred for this MSI.
