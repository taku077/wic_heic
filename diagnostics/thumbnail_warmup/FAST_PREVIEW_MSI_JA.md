# HEIC Fast Thumbnail Preview MSI v0.1
Per-user MSI installs actual Shell IThumbnailProvider for .heic, with experimental WTSAT_ARGB and direct WIC HEIC decoder activation.
Requires preinstalled WicHeic WIC decoder. No WIC decoder or Photos update.
Safeguard: refuses to overwrite an existing per-user .heic Shell thumbnail association.
Uninstall removes own HKCU registrations and DLL; prior HKLM handlers remain.
Use COPY of real photos in a new folder, not originals. Windows 10 Explorer first paint and metadata/HDR accuracy not yet confirmed.
No .heif association in v0.1.