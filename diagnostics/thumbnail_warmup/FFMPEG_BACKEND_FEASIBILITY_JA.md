# HEIC高速化：FFmpeg HEVC復号エンジン実装可能性の調査（2026-10-21）

- 埋め込みサムネイルなしのHEICにも効く高速化を目標とする。現行Windows向けv0.3.0/v0.4.0（独立WICサムネイルハンドラー）を変更しない。
- 固定版libheifに公式プラグイン `libheif/plugins/decoder_ffmpeg.cc` があり、FFmpegの `AV_CODEC_ID_HEVC` でHEVCのプライマリ画像をデコードできる構成が用意されている。CMakeの `WITH_FFMPEG_DECODER` は既定でOFFで、avcodec／avutilヘッダーとリンクライブラリが必要。
- **コマンドラインのffmpeg.exeがHEICを扱えることと、libheifのFFmpeg復号プラグインを利用できることは別条件**。今回のCIは入口としてコマンドライン版のHEIC読み取りと12MP→256px縮小の可用性・ウォールタイムを計測する探索で、WIC DLLへの統合試験でも、画素完全一致の証明でもない。
- `.github/workflows/hevc-ffmpeg-feasibility.yml`: GitHub Actions Windows一時ランナーのみで既存の12MP人工HEICを生成し、ffmpeg.exeで1/2/4スレッドのHEIC単画像復号と256pxの縮小を試す。FFmpeg未導入ならChocolateyでCI一時環境にだけ導入する。ユーザーの写真は扱わず、HEICのSHA256不変を確認。
- FFmpegプロセスの起動時間・入出力処理を含むため、CLI数値をそのままShell/WICの実用表示時間と比較してはいけない。カラー変換、EXIF/irot、ICC/HDR、アルファ、Win10の依存ライブラリ配布／自動CPU選択を検証するまで配布不可。
- 先行する [AVX2試験](https://github.com/taku077/wic_heic/blob/experiment/hevc-msvc-avx2-cpu-dispatch/diagnostics/thumbnail_warmup/AVX2_COLOR_BACKEND_RESEARCH_JA.md) ではMSVC AVX2の差は±0.5%程度で、採用しない。
- MSIの作成はしない。実機の旧版を削除・上書きしない。
