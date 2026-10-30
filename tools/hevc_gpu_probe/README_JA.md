# HEVC GPU read-only capability gate (Windows 10 / WIC research)

## 目的と現在の結論

Windows 10フォトでHEIC表示中にCPU全体100%が継続する実機問題の、**新しい改善経路候補**を調べる。ユーザー環境は第7世代Core i5内蔵Intel HD Graphics 620（専用GPUなし）。**内蔵GPUにもHEVC専用デコード回路はある**が、現行自作WICの`libheif/libde265`ソフト復号がその回路を自動で使うとは限らない。

`HevcGpuReadOnly.exe` は HEIC原本・サムネイル・レジストリ・インストーラー・GPU設定を一切変更しない**能力列挙だけの調査プログラム**。Media FoundationのハードウェアMFT列挙と、D3D11ハードウェアデバイス上のHEVC Main (NV12)・Main10 (P010) 出力形式サポートを**独立に照会**する。

|出力|意味|
|---|---|
|`D3D11_HARDWARE_DEVICE=AVAILABLE`|実行PCでD3D11ハードウェアデバイス生成成功。GPU HEVC復号成功の証明ではない|
|`D3D11_HEVC_MAIN_NV12=YES`|HEVC Main向けデコーダープロファイルとNV12形式のサポートあり|
|`D3D11_HEVC_MAIN10_P010=YES`|HEVC Main10向けデコーダープロファイルとP010形式のサポートあり|
|`MF_HEVC_HARDWARE_MFT_COUNT=N`|Media Foundationが現在列挙できるHEVCハードウェアMFT数。0でもD3D11VA等別経路まで不可能とは断定できない|
|`UNKNOWN`|API失敗など。対応なしのNOと混同禁止|

**列挙でYESが出ても、HEIC grid 48タイルのビットストリーム／色域／ICC／Exif／回転のデコードの正確性・CPU削減・GPU使用率は未検証。MFTEnumExではActivateObjectを呼ばない。**

## Windows CIの目的と限界

`.github/workflows/hevc-gpu-probe-ci.yml` で、MSVCビルド、`--self-test`、ヘッドレスCI上での例外なく安全な能力列挙を確認。**CIのGPU能力値をユーザーのWindows 10/Intel HD620の値と扱わない**。人工HEICですら実復号を行わないためWindows 10での製品高速化の採否はまだ判断できない。

今後の進行：①実環境での列挙結果（必要時のみ）→②ハードウェア経路を別研究として人工HEICのみでDecode/CPU-ms・GPUエンジン・画素一致を比べる→③色・向き・回転保存・ICC/Exif・MSIロールバックを非退行確認→④実機でCPU100%持続と体感が改善する場合に限り本線導入。**YESだけでMSIを配布しない**。

## 横断した一次ソース

- Intel 第7世代GPU HEVC decode matrix: https://www.intel.com/content/www/us/en/developer/articles/technical/encode-and-decode-capabilities-for-7th-generation-intel-core-processors-and-newer.html
- Microsoft MFTEnumEx: https://learn.microsoft.com/en-us/windows/win32/api/mfapi/nf-mfapi-mftenumex
- Microsoft D3D11 CheckVideoDecoderFormat: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11videodevice-checkvideodecoderformat
- Chromium D3D HEVC profile & NV12/P010 handling: https://chromium.googlesource.com/chromium/src.git/+/refs/heads/main/media/gpu/windows/d3d_decoder_configurator.cc
- FFmpeg D3D11VA/DXVA2 acceleration documentation: https://ffmpeg.org/ffmpeg.html
- libheif plugin notes: https://github.com/strukturag/libheif
- libde265 software decoder: https://github.com/strukturag/libde265

**注意**: このツールの実機配布はユーザーの同意なくWindows上に自動導入しない。GitHub CI成果物は評価用であり、`wic_heic.dll` のGPU実装ではない。個人のETL・画像・パスをGitHubに保存しない。
