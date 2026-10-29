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

## 2026-10-30: Actual isolated Shell provider succeeds with HKLM COM registration

- Commit `6989c685f7f05433526c7e5f8858660e7fb63440`, Actions #40910367554: **SUCCESS, 42 steps**.
- Direct COM activation with HKCU registered class succeeded, but isolated `IThumbnailCache` extraction failed (`SHELL_USER_CLASS_ONLY_RESULT success=False`).
- With a **fresh test-only HKLM SOFTWARE\\Classes\\CLSID** entry for exactly the same COM DLL and a new disposable fixture filename, `SHELL_MACHINE_CLASS_RESULT success=True`.
- Actual Shell invocation recorded `SHELL_REGISTERED_LAB_RESULT provider=1 embedded=1 full_12mp=0`, followed by `SHELL_REGISTERED_EMBEDDED_HEVC_FAST_PATH_PASS` and `SHELL_REGISTERED_LAB_REGISTRY_RESTORED_PASS`.
- On that Windows CI run, existing default Shell extraction was 206ms and registered embedded path 33ms. **One observation per path, differing cache state; don't claim a stable 6x speedup or Win10 result.**
- This shows that SYSTEM-LEVEL CLASS REGISTRATION was sufficient on the disposable Windows Server 2022 runner; does **not** prove HKCU is inherently unsupported on Windows 10, nor that HKLM alone solves every system variant.
- No user system changes, no new MSI. Next required work: ensure **no-embedded fallback and high-resolution 1024px fallback**, EXIF/irot/color/ICC/HDR/alpha/grid handling, proper one-time 1.1.31 MSI machine-class registration with rollback, and sustained performance A/B.
- A separate focused Windows workflow branch `experiment/shell-com-focused-20261030` is being prepared to iterate without repeating full MSI/rotation verification each edit; full 42-step CI remains a final release gate.
- Microsoft registration documentation: https://learn.microsoft.com/en-us/windows/win32/shell/reg-shell-exts and https://learn.microsoft.com/en-us/windows/win32/shell/thumbnail-providers

## 2026-10-30 next safety gate: full-frame fallback and fast focused CI

- New focused workflow: `experiment/shell-com-focused-20261030`, `.github/workflows/shell-com-focused-ci.yml`. Earlier baseline run #40910945419 SUCCESS (Shell and registration compare), without rerunning the entire MSI/rotation CI.
- Prototype commit `a843f59579577b5f04ea41b1eac2969c47dd775c` implements WIC full-frame fallback if no embedded HEVC thumbnail exists or if the requested thumbnail is larger than the embedded image, with 4032x3024 no-thumb synthetic generation and 1024px/256px test entry points.
- Focused workflow now tests: (a) 1024x768 from a 1024px request despite only a 320x240 embedded preview, (b) 256x192 from an HEIC with no thumbnail, (c) exactly two intentional full-primary decode events, then a registered Shell HKLM/HKCU comparison and strict restoration.
- New workflow success status is required before claiming either fallback working. Full CI #40910367554 SUCCESS is for the preceding standalone embedded-only code, **not proof of the newer fallback code**.
- Next after fallback gates: add orientation and ICC/HDR/alpha/grid safety (never silently use a stale/wrong embedded thumbnail); compare CPU and latency over multiple samples, then package product MSI with reversible HKLM COM registration, no other WIC priorities changed.
- Installer must preserve current-user custom file associations and system thumbnail provider values, and must never leave lab CLSIDs in real installations. Existing 1.1.30 MSI stays installed until a safely reversible release candidate is ready.

## 2026-10-30: 8+8 Shell A/B latency and conservative safety path

- CI workflow `experiment/shell-com-focused-20261030` at commit `0016353cb8e0b0e152db631805acf5cd0a864764`, run [#40913663447](https://github.com/taku077/wic_heic/actions/runs/40913663447) **SUCCESS**.
- Fresh synthetic copy/forced extraction, eight files per path on the same Windows Server 2022 runner: standard WIC Shell extraction median **82ms**, registered embedded thumbnail handler median **11ms**, ratio **0.134** (86.6% shorter for this artificial workload). It is not a Windows 10 real-folder prediction; operation order and cache warmth differ, and no CPU sampled stacks are collected in this CI.
- New explicit 15%-median-speedup release gate passed (`SHELL_8_FILE_MEDIAN_SPEEDUP_GATE_PASS`).
- Integration trace `SHELL_REGISTERED_LAB_RESULT provider=1 embedded=1 full_12mp=0` confirmed no full decode for the embedded case and COM registration was restored afterward.
- New conservatism at commit `c8aa4ba14ed43c1df3d9f4e1cc5bc1699e79870d`: main-frame vs embedded aspect discrepancy >0.015, missing color-context read, or ICC on main frame -> full-WIC fallback rather than trusting an embedded image blindly. Existing no-thumbnail and 1024px quality fallbacks still pass. **This does NOT yet validate actual orientation/EXIF/irot, ICC fixture, alpha, HDR, grid, or gain maps.**
- Important MSI assessment: the current 1.1.30 installer uses WiX `File SelfRegCost=1`. Existing `DllRegisterServer` writes `SystemFileAssociations\\.heic\\ShellEx\\{e357...}` to Microsoft's photo-thumbnail provider, but existing `DllUnregisterServer` removes only codec CLSID entries. **This is insufficient for safely replacing/restoring another user's Shell thumbnail provider.** Do not ship the lab handler in an MSI by copying this self-reg pattern. New installer must snapshot/compare-and-restore affected values and test install/upgrade/uninstall under different prior handler settings.
- Candidate release still blocked on (a) production COM registration without clobbering third-party association, (b) 8-bit/rotated/HDR/ICC/alpha/grid quality correctness or conservative fallback, and (c) whole product MSI and Photos rotation-save regression in a disposable Windows environment.
- Current 1.1.30 remains the real-device version. No extra end-user WPR capture is needed yet. Optimize safely rather than meeting a theoretical end-state or distributing every minor checkpoint.

References: https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nn-thumbcache-ithumbnailprovider ; https://github.com/libvips/libvips/blob/master/libvips/foreign/heifload.c ; https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers .

## 2026-10-30: ICC-bearing embedded HEIC is safely forced onto old WIC path

- Added genuine 4032x3024 HEVC primary carrying the repository's synthetic sRGB ICC file, with its normal embedded 320x240 HEVC preview. Generator verifies the primary actually retains an ICC after encoding; no private image data involved.
- CI run [#40914759428](https://github.com/taku077/wic_heic/actions/runs/40914759428), commit `a6ff4830299d9c72436bf1d2b4de1c1057b52e11`: **SUCCESS**. Three direct native fallback cases: 1024px request, no embedded preview, ICC-bearing primary. All produced intended outputs and exactly three primary full decodes.
- CI run [#40915571966](https://github.com/taku077/wic_heic/actions/runs/40915571966), commit `649674a44316a73008995ac888f8f87f9d5a9ed3`: **SUCCESS**. Actual HKLM-registered isolated Shell IThumbnailCache also fetched a genuine ICC-bearing HEIC and logged `SHELL_REGISTERED_ICC_FULL_WIC_FALLBACK_PASS` (one primary full decode + one `icc-primary` fallback).
- Same CI's eight/ eight synthetic forced thumbnail timing medians: **68ms existing WIC**, **8ms registered handler**, ratio **0.118** (88.2% lower synthetic per-file latency). This is a single run, still order/cache influenced, and **not** an observed Windows 10 Explorer folder improvement.
- HKLM temporary COM class and ShellEx registration rollback gate again passed (`SHELL_REGISTERED_LAB_REGISTRY_RESTORED_PASS`); no actual product MSI change.
- Safety caveat: confirmed fallback for ICC does not itself verify ICC color transform precision. It preserves the existing 1.1.30 WIC source path rather than introducing a new shortcut.
- **Release blockers remain**: controlled EXIF/irot orientation correctness in both primary/embedded HEVC, 8/10/12bit and alpha/grid/HDR/gain-map handling or conservative fallback, and separate safely reversible product MSI registry upgrade/uninstall. The current installer uses `SelfRegCost=1`; a new ShellEx value cannot simply overwrite other handlers and be deleted on uninstall.
- Keep production WicHeic 1.1.30 until explicit Windows 10 pilot. Do not ask the user to repeat the earlier WPR ETL capture.
