# HEIC vs JPEG rotation geometry investigation (no production fix yet)

Date: 2026-10-20

## Observed on user's Windows 10 Photos
- The previous HEIC display-rotation and rotate-save errors were resolved by the v2 experimental build.
- New issue: rotating portrait HEIC to landscape in Windows Photos can horizontally stretch content or change its proportions.
- The user explicitly requires machine comparison with JPEG, not speculative code changes.
- Avoid changes to the user's installed WIC, HEIC Manager or original photographs until actual Photos saved outputs are measured.

## Source audit
- The earlier native WIC smoke test checked frame count and dimensions but did NOT compare image pixels or aspect geometry.
- `CHeicBitmapFrameEncode::WriteSource` does not cross-check source dimensions against previously negotiated `SetSize`; in its converted-format branch, it does not examine the return value of `pConverted->CopyPixels`. These are implementation weaknesses but NOT established as the cause of the Photos bug. Do not patch until isolated.
- Windows Photos can use a different WIC call sequence than the test harness. There is currently no trace of the Photos call parameters.

## Machine-verified baseline
GitHub Actions run: https://github.com/taku077/wic_heic/actions/runs/39380880738

The `wictest/GeometryProbe.h` test uses a non-square asymmetric tiled RGB image (192x320), creates identical JPEG and HEIC versions via WIC, rotates each 90 degrees, saves through the corresponding WIC encoder, decodes both results, and compares RGB pixels at grid centers against the exact rotated reference.

Results:
- JPEG 90-degree output 320x192, grid RGB mean absolute error (MAE) 0.815 / 255.
- HEIC 90-degree output 320x192, grid RGB MAE 0.464 / 255.
- JPEG vs HEIC MAE 0.785 / 255.
- Deliberately horizontally stretched negative control: MAE 68.108 / 255, proving that the test detects distortion despite correct outer dimensions.
- The test passed both before and after the MSI installation in the GitHub Windows-2022 runner.
- Production source was NOT altered on this diagnostic branch; only tests and CI selection were added.

These findings rule out unconditional deformation along the tested direct WIC rotate-save path, NOT Windows Photos-specific deformation.

## Next acceptance gate
Collect measurements of the user's actual Windows Photos saved outputs:
1. From one affected ORIGINAL HEIC, make a matching JPEG baseline using Windows WIC's displayed raster. Preserve immutable references.
2. Rotate HEIC and JPEG working copies in Windows Photos in exactly the same direction.
3. For both saved outputs, measure WIC dimensions, Orientation, same-direction grid RGB MAE against the rotated JPEG baseline, and SHA256.
4. Compare the errors and dimensions. If HEIC departs sharply from JPEG, examine the WIC SetSize, pixel format, source dimensions, stride and CopyPixels return value in the Photos-specific save path.
5. Do not conclude that EXIF, ICC, pixel format or a particular WIC function is at fault until evidence selects it.

The local read-only Photos A/B kit is distributed to the user separately; unlike the native CI harness, that PowerShell/WPF kit has NOT yet been validated on their Windows 10 environment.

Never change `master` or merge these diagnostic changes as an assumed repair.
