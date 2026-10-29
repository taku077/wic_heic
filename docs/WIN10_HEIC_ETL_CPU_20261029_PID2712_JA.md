# Windows 10 / WicHeic 1.1.30 ETL CPU attribution (2026-10-29)

## Confirmed result, no additional user capture

A preexisting Windows 10 WPR trace (1,907,359,744 bytes, SHA256 **EE0E8E2D695540D700EF11E99653165C219A11FBE165F96B8554B20AC8B971DF**) was analyzed **offline on Linux**, without uploading the user's ETL or any filenames/image content to GitHub.

- All **1,819** uncompressed 1-MiB ETL buffers parsed with no invalid event boundaries.
- PerfInfo SampledProfile (opcode **46**): **539,601** CPU instruction-pointer samples.
- Matched to explicit kernel StackWalk (group 0x18/opcode 32) **PID+TID+timestamp**: **530,474** samples (98.31% of all).
- COM Surrogate `dllhost.exe` **PID 2712**: **153,543** attributable samples.
- IP in module range explicitly recorded for **PID 2712**:
  - `wic_heic.dll`: **113,221**, or **73.739%** of that process's samples;
  - `ntdll.dll`: **5,040**;
  - `WindowsCodecs.dll`: **1,710**;
  - `msheif_store.dll`: **1,174**;
  - `icm32.dll`: **1,029**.
- **29,862** PID-2712 samples could not be assigned to a process-local module image range, and were **not** silently attributed to WIC.
- `wic_heic.dll` 4-KiB RVA regions with the most samples:
  `0x120000`: 18,369; `0x12d000`: 14,487; `0x12e000`: 12,250;
  `0x135000`: 9,286; `0x105000`: 7,098; `0x145000`: 5,845.

### Interpretation and important limitation

These are **CPU instruction-pointer sampling counts**, not CPU cycles or elapsed time. They establish that the primary **active CPU execution** in the busy Shell surrogate is **within `wic_heic.dll`**, rather than merely that the DLL was loaded.

**But** the DLL may statically contain libheif/libde265; this sampling cannot yet distinguish the source WIC's own functions from the static HEVC decoder without a matching PDB/linker map. Do not claim an exact `libde265` percentage or blame Microsoft HEIF based solely on a module load.

The existing WIC trace observed **141 full-resolution 12MP decodes** in PID 2712. Independent re-count of the 2,260 WIC events for that PID: `DecoderGetFrame=141`, `DecoderFullDecode=141`, `DecoderThumbnailEmbedded=0`, `FrameThumbnailEmbedded=0`, `DecoderThumbnailUnavailable=0`, `FrameThumbnailUnavailable=0`. Thus **neither successful nor absent GetThumbnail was logged** in the observed Shell surrogate path. Scanline `CopyPixels` calls broadly match one complete row traversal per image; count alone does not prove redundant decoding.

### Development gate for 1.1.31

1. Map hot WIC RVAs to matching **1.1.30 build PDB** if available, without requiring the user to record anything again.
2. Investigate the **Explorer/Shell thumbnail path**, not only Photos' 2-worker budget. WIC `GetThumbnail` already works for synthetic embedded-HEVC fixtures, yet Shell's current path does not seem to call it in the high-load process.
3. Research an isolated `IThumbnailProvider + IInitializeWithStream` experiment, **not registered by the product MSI** until tested. Correct image orientation, ICC/color, existing cache, thumbnail dimension requirements, alpha/HDR, and safe fallback must be verified. No unverified replacement of a full-resolution Photos decoder.
4. Only after objective A/B performance and rotation-save safety gates, consider an **1.1.31** MSI. Avoid another user recording until justified.

No private raw ETL, module paths, or photos are committed here.

## Source references

- https://learn.microsoft.com/en-us/windows/win32/etw/sampledprofile
- https://learn.microsoft.com/en-us/windows/win32/etw/image-load
- https://learn.microsoft.com/en-us/windows/win32/etw/thread-v2-typegroup1
- https://learn.microsoft.com/en-us/windows/win32/wic/-wic-integrationregentries
- https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers

## 2026-10-30: Shell IThumbnailProvider integration experiment and release decision

- Verified existing Shell WIC-only path on disposable 4032x3024 HEVC with 320x240 embedded thumbnail: `IThumbnailCache.GetThumbnail(WTS_FORCEEXTRACTION)` produced 256x192 but recorded `DecoderOpen=1`, `DecoderFullDecode=1`, embedded `GetThumbnail=0`. Actions #40898893355 SUCCESS.
- Direct standalone `IThumbnailProvider + IInitializeWithStream` prototype produced the expected 256x192 DIB from native embedded 320x240 HEVC; trace confirmed no 12MP decode. Actions #40901044715 SUCCESS.
- Added a CI-only DLL/COM class factory and HKCU .heic ShellEx temporary registration, with initial SDK name-linkage fix (commit `f8711c35`), then `ThreadingModel=Apartment` and merged-HKCR/direct CoCreateInstance checks. Commit `aa09e85f`, Actions #40906973978: **HKCR path and threading value matched, direct COM activation succeeded, but the isolated Shell GetThumbnail returned 0x80040154**.
- Applied required `SHChangeNotify(SHCNE_ASSOCCHANGED)` before Shell request and after cleanup, plus new synthetic HEIC pathname. Commit `b3d2594d`, Actions #40907733973: **same Shell 0x80040154**. Source DLL and direct COM activation are functional; isolated Shell/provider selection remains unresolved.
- No production MSI registration or rollback state changed. WicHeic 1.1.30 is the last real-device version.
- Next safe investigation: ascertain whether per-user COM class registration is invisible to the isolated Shell surrogate; compare on a *disposable runner* with careful HKLM registration/restore and/or a fresh non-conflicting test file extension. Preserve Shell process isolation; never use `DisableProcessIsolation=1` to force a pass.
- Do **not** release 1.1.31 merely because direct prototype runs. Product gate: actual isolated Shell activation, CPU/full-decode A/B, 256/1024 size and embedded/no-embedded fallback, EXIF/irot direction, ICC/HDR/alpha/grid, and safe restoration of all Shell registration keys after MSI uninstall.
- For future iteration, split the focused Shell lab from the 40-step full regression CI; the latter should be rerun as a final pre-release gate instead of on each shell registry experiment.

Cross-check references: https://learn.microsoft.com/en-us/windows/win32/shell/reg-shell-exts ; https://learn.microsoft.com/en-us/windows/win32/shell/thumbnail-providers ; https://github.com/libvips/libvips/blob/master/libvips/foreign/heifload.c .
