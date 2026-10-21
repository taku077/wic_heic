# WicHeic Fast Thumbnail Preview v0.2.0 (Windows 10 x64 trial)

This experimental MSI installs two DLLs together: WicHeicFastPreview.dll (the actual .heic Shell IThumbnailProvider) and WicHeicPrivateCodec.dll (a pinned existing WIC HEIC decoder, loaded privately by the handler).

**A separately installed WicHeic decoder is NOT REQUIRED.** The v0.1.1/v0.1.2 prerequisite-registry check was invalid for PCs with other HEIC software or a differently registered codec; the v0.2.0 MSI removes that condition and privately loads its included decoder.

Important safety boundaries:
- The private decoder is NEVER registered as a system WIC codec. The MSI leaves existing Windows Photos, HEIC Manager, and installed WIC registrations untouched.
- The provider's own COM class is registered in HKLM; the .heic thumbnail override is recorded under HKCU for the installing account. No .heif association.
- If an HKCU .heic handler already exists, installation refuses to overwrite it. Do not manually delete registry values to bypass this.
- The original Windows-wide HEIC association remains; uninstall removes only the preview's own DLLs and registry entries.
- Test COPIES of real HEIC photos, not originals. Do not rotate/save HEIC using Photos while testing.
- Windows Server 2022 Shell synthetic CI results do not guarantee Windows 10 GUI paint speed or parity across iPhone HEIC orientations, ICC, HDR, or embedded metadata.
