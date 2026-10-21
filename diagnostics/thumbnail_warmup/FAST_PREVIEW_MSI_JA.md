# HEIC Fast Thumbnail Preview MSI v0.1.2 (Windows 10 x64 trial)

- This version **installs the actual .heic Shell thumbnail provider**, not merely the old WIC decoder.
- Version 0.1.2 fixes the prerequisite registry check: the WIC HEIC **decoder** CLSID is {6215667C-74CA-4076-A769-3F834C3F25F5}; v0.1.1 incorrectly checked the **encoder** CLSID {0E8AB687-73A5-455B-8C19-B93CA9AC0EEB}.
- If the user already has a per-user .heic Shell thumbnail handler installed, the MSI refuses to replace it. Do not delete any registry values or uninstall HEIC Manager just to satisfy the check. See the read-only preflight report tool.
- The MSI installs a separate native Shell thumbnail COM server machine-wide and a per-user .heic override (HKCU); it requires an installed WIC HEIC decoder and must be installed by the current Windows account.
- Uninstall removes the preview-owned override and DLL (verified on Windows Server 2022 synthetic samples). The pre-existing machine-wide Shell handler remains.
- Test ONLY copies of actual HEIC photos, never rotate/save original HEIC with Photos during this experiment. GUI initial paint on user's Windows 10 remains unverified.
- Only .heic is handled; .heif not included. No codec or Photos replacement.
