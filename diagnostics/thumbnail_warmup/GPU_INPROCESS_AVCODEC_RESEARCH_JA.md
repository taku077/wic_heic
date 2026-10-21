# 2026-10-21: in-process AVCodec GPU HEVC 初回サムネイル高速化研究

## このブランチの位置づけ
- 親：`experiment/heic-gpu-d3d11va-firstframe` の成功コミット `e950937a`。原本保護、HEIFメタデータ照合、ネイティブAnnex B抽出（8件）、悪意あるNAL拒否等は維持。
- 新研究：`experiment/heic-gpu-inprocess-avcodec`。従来の研究用 `ffmpeg.exe` 外部プロセスを各画像で起動する形から、同一C++プロセスで `libavcodec` を呼び出す方式へ移行する検証コード。
- Windows標準WIC、Photos、HEIC Manager、既存MSIのDLL／COM登録を変更しない。GPU経路は製品から無効のまま。

## 新規コード
1. `wictest/isolated_shell/HeicInprocessGpuAB.cpp`：
   - FFmpegライブラリのHEVCソフトウェアdecoderと、条件付きの D3D11VA HEVC decoderを直接呼ぶ。
   - FFmpegのHW decoderの構成で `AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX` および `AV_PIX_FMT_D3D11` を要求し、 `get_format` ではソフトウェアへ黙って戻さず拒否。
   - 実GPUテクスチャを得た場合だけ `av_hwframe_transfer_data` でNV12/P010をCPUへ読み戻してBGRAへ変換。GPU失敗ならGPU成功と記録せず、不採用。
   - HW deviceは1回のプログラム呼び出しで2画像間再利用。ソフトウェア基準と同じ、先行CIで主画像メタデータが安全判定されたMain10の`04.heic`と`fixture-irot3-exif6-noaux.heic`に限定。
   - `RUNNER_TEMP/heic_native_bridge/` の事前検証済みAnnex Bのみを読み、他のHEIC原本に触れない。出力は新規`RUNNER_TEMP/heic_inprocess_software`/ `heic_inprocess_gpu` にしか保存しない。
   - GPU初期化、ストリームread、frame decode＋BGRA、バッチ総経過時間、CPU時間、プライベートメモリを分離測定。未回転BGRA画像を保存してFFmpegで復号した同一Annex Bからの参照とSHA256比較。
2. `wictest/isolated_shell/HeicInprocessGpuAB.vcxproj`：独立実行ファイル。FFmpeg の avcodec, avutil, swscale をDLLで使用。**MSI/WICライブラリにはリンクさせない**。
3. `diagnostics/thumbnail_warmup/Prepare_FFmpeg_Inprocess_GPU_SDK_CI.ps1`：過去の成功済 `Prepare_FFmpeg_SDK_CI.ps1` の固定SHA-256 FFmpeg 8.1 LGPL shared SDKを転用し、swscale.lib も生成する。
4. `wictest/isolated_shell/D3D11HevcCaps.cpp`：従来4032x3024のHEVC Main10判定だけでは2MP級HEICを不必要に除外しうるので、`04.heic` 2048x2048 と縦長 1080x1920 のdecoder configurationを追加照会。**同一アダプタが両方対応した場合のみGPU A/Bを試行**。
5. `.github/workflows/heic-gpu-path.yml`：native Annex Bと回転照合の後に、1プロセスのAVCodec経路をコンパイル。ソフトウェア経路は2件完全一致必須。実GPUハードウェアがないCIでは `NOT_PROVEN`。GPUでの画素差・不一致は報告だけで、製品採用しない。
6. `.github/workflows/heic-inprocess-fast-guards.yml`：コードの安全ガードやFFmpeg準備スクリプトの構文を確認する軽量CI。

## 検証状況と未解決
- `e950937a` の先行研究CI2本成功は確定。先行GPU試行ではWindows CIに実HWアダプタなし、GPU復号は未実証。
- **新C++の総合CIは新ブランチで実行して結果を確認するまで未認定**。コードのGitHubコミット完了とWindowsコンパイル成功、画素一致を混同しない。
- CPU baselineのunrotated BGRAとFFmpeg CLIの参照が厳密一致するかはWindows CIの結果次第。10bit変換時の色マトリクス差は確認してから扱う。
- 実GPUでAV_PIX_FMT_D3D11 frameが得られ、hwframe transferが成功し、出力の回転、ICC/NCLX、10bit色変換、alpha対応、256pxサムネイルの画質が再現できるまではユーザー配布なし。
- 2件の研究A/Bで初回Explorer高速化は証明できない。将来、10～30件の同一条件ABBA（プロセス初期化時間込み）とGPU対SWのCPU/メモリ/画質を確認。最大30%以上等の明確な改善の目途が立つまでMSIを作らない。

## 2026-10-22 継続調査：Windows実測で発見した色再現性差分
- `fd6443a` のWindows CI：固定版FFmpeg SDK取得成功、MSVCで`HeicInprocessGpuAB.cpp`コンパイル成功。
- `04.heic` は元Annex BをCLIでBGRA復号した画像と、libavcodecプロセス内復号BGRAが **SHA256およびバイト数で完全一致**。
- `fixture-irot3-exif6-noaux.heic` はHEVCフレーム復号には成功したが、BGRAのSHA256が参照値と不一致（8,294,400byteの寸法一致のみ）。
- 計測値はCi上の研究用wall時間：04 HEVC約31〜37ms、もう1枚約16〜21ms。これはExplorer表示時間でも第7世代Intel実機の速度でもない。
- CI失敗は意図した画素一致ゲートの正常動作。2枚目を一致とみなしたり、GPU高速化ができたと主張しない。
- `d23c682` でHEVC AVFrameのYCbCr matrix・量子化rangeに基づきlibswscaleの`sws_setColorspaceDetails`を呼び、BT709/BT2020/601等既知の行列だけを受理。未対応行列はfail-closed。
- `874ee16` までに、色変換前の10bit `YUV420P10LE` を診断専用に保存し、CLIの同一Annex Bから復号した10bit YUVをSHA256比較するCIを追加。これで2枚目の差がHEVC復号由来かYCbCr→BGRA変換由来かを独立に識別する。
- **この新規色変換変更と10bit YUV raw parityのCI合格は未確認**。該当SHAのActionsステータスが確認できてから合格と認定すること。
- 過去のCIでGPU実ハードウェア未搭載、D3D11VA実復号未実証。製品WIC/Explorer、MSI、Photos、HEIC Managerは一切変更していない。
- 通常プッシュ時の総合ネイティブ再構築は抑え、絞ったMain10プロセス内復号CI + 軽量ガードCIによりフィードバックを得る構成へ変更。
