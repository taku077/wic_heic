# Windows 10 Photos/WIC integration safety gate (required before real-machine testing)

Status 2026-10-20: Experimental Photos call-trace source compiles and CI tests pass on windows-2022 GitHub Actions. **Windows 10 Photos itself has NOT been tested.** The previous user's valuable IMG_4074 HEIC must never be used in further trials.

## Preconditions for any user-machine installation
- User explicitly approves a **temporary** replacement of the WIC component. A GitHub Actions success alone is NOT consent to install or edit.
- Independently verify and preserve current installed WIC DLL SHA-256, the registered WIC encoder/decoder CLSIDs, MSI/uninstall details, Photos version, and Windows 10 build.
- Verify a restorable exact MSI matching their current installed codec is locally available and test the reinstall/rollback procedures on a disposable virtual Windows environment first.
- Do not touch, uninstall, or change custom HEIC Manager components.
- Create a new empty local folder dedicated to disposable test images. No links, junctions or symlinks into the photo library.
- Copy ONLY a public synthetic HEIC into that folder. Save a bit-identical reference outside it. Use exclusive new paths, never overwrite existing photos.
- Images and metadata used in GitHub Actions must be public synthetic fixtures, never privately submitted actual photographs.

## Privacy-limited trace deployment
- Diagnostic tracing defaults to OFF. A separate PowerShell start script enables HKCU settings for at most 20 minutes; stop script removes only settings created for that session.
- The trace is capped at 2 MiB and contains only event names, PID, widths/heights, transform/compression numeric values, source classification and HRESULT. NO input/output paths, file names, content, EXIF, GPS or ICC values.
- Log and summary are saved next to the scripts, not in AppData or hidden directories.
- The trace mechanism may not have access to a packaged Photos process or its file-system sandbox. Verify with an explicit probe before concluding that Photos did not use this WIC component.
- No background monitoring, persistent hooks or startup tasks. No operations on IMG_4074.

## Procedural guard
1. Confirm exact WIC snapshot and tested rollback package exist.
2. Stop Windows Photos/Explorer photo browsing, install the diagnostic WIC only after approval, verify dll hash/registration, then reboot if required.
3. Start local trace. Test with an artificial HEIC file ONLY, copying it to a new disposable working path.
4. Open the synthetic copy in Windows Photos; rotate 90 degrees and save its **working copy**. Stop trace.
5. Verify untouched synthetic reference SHA, Photos-saved output, HEIF metadata and log events. Distinguish events by PID, not just timestamp.
6. Immediately restore originally installed WIC, compare registration and file hashes, reboot; do not alter HEIC Manager.
7. If no event is captured, diagnose trace applicability before trying again. Do not claim Photos never used WIC from an empty file.

## Release gates
1. JPEG-equivalent lossless orientation must actually work in Windows 10 Photos UI, not only a native test executable.
2. Four 90-degree rotations must restore original displayed orientation and preserve all compressed image/metadata payloads (including HDR gain maps, thumbnails, EXIF, MakerNote, ICC, XMP) unless the image format cannot support the edit and the operation is explicitly refused.
3. Native resizing/cropping which requires re-encoding must retain metadata and correct geometry. Lossless rotation and resize should not be conflated.
4. Failing a test must NEVER silently report success. User's original and recovery copies must never be overwritten.
5. A full Windows 10 Photos AND Explorer, CPU/memory, install/uninstall, and recovery/rollback test must succeed.
6. No experimental MSI should be labeled "complete" or recommended for production before the above.

Source: https://github.com/taku077/wic_heic/actions/runs/39421885041
