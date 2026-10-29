# WicHeic CPU diagnostic — standalone download

This branch exists only to provide a permanent GitHub source-archive ZIP for the Windows 10 WPR 19041 manual-stop diagnostic. It has **no MSI, photos, or unrelated repository content**.

Start by extracting the ZIP, then run `01_Start_CPU_Manual.cmd` from the extracted folder. Keep the six files together. Read `README_CPU_WPR_MANUAL_JA.md` first.

- Upstream, Windows CI validated source: `e6ff0ede99a57cef8158b6aaf5a8c2d1b93f49ae`
- CI validation: https://github.com/taku077/wic_heic/actions/runs/40889903222
- GitHub archive (commit-pinned preferred): links supplied in chat
- No GitHub Actions artifact retention restriction for code archives.
- This is a CPU diagnostic tool, **not** WicHeic installer.

Do not change or save original HEIC images during the capture. The ETL trace may contain private paths/process details.

## Included SHA-1 Git blob IDs

- `01_Start_CPU_Manual.cmd`: `3c8439eade1f76ffb32ec7cc90c75439aa547cbe`
- `COM_CPU_WPR_Manual.ps1`: `82c7eb45fb7c32581ca2c1d58dfa2023ae25d0cf`
- `COM_CPU_WIC_Analyze.ps1`: `423a49c67e667c89dcbf344427ab3533e1260a80`
- `PhotosTrace_Start.ps1`: `a85288aeaa8c676b4741e21c881e3ddac2a1cf33`
- `PhotosTrace_Stop.ps1`: `970992eda4ec1a71a7057e7ddfc66e39a639d12a`
- `README_CPU_WPR_MANUAL_JA.md`: `ee9c52a6aca4dbe35e802914297b9e930e7af616`
