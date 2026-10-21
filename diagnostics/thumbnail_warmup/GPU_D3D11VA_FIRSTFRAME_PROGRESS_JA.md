# HEIC GPU復号：D3D11VA 1フレーム読み戻し研究（2026-10-21）

## 保持する確定成果
- `experiment/heic-gpu-caps-ci` の厳密なDisplay Matrix拒否規則＋7件BGRA完全一致：Actions `39602846177` SUCCESS。
- `experiment/heic-gpu-decode-probe` のlibheif主画像変換調査＋C++のネイティブAnnex B抽出8件バイト一致：Actions `39608974924` SUCCESS。
- GPUでの実復号やiPhone実機での初回サムネイル改善が成功したわけではない。
- decode-probeブランチには `HeicOrientationPolicy.ps1` と `CI_HeifCrosscheckRotation.ps1` が既にあるため、別ブランチから内容を再移植・二重定義しない。

## 今回の実装
研究専用ブランチ：`experiment/heic-gpu-d3d11va-firstframe`。
- `diagnostics/thumbnail_warmup/CI_HeicD3d11vaFirstFrame.ps1` は既存の `HeifPrimaryTransforms.exe`、`D3D11HevcCaps.exe`、`HeifNativeAnnexB.exe` の成果を接続する。固定HEICサンプルのlibheif主画像メタデータが安全と認定したものだけを対象とし、D3D11 Main8/Main10プロファイルと実DXGIアダプタ番号を対応させる。
- FFmpeg D3D11VAを `-init_hw_device d3d11va=hw:<adapter>`、`-hwaccel d3d11va`、`-hwaccel_output_format d3d11` で明示。実D3D11 surfaceの `hwdownload,format=nv12|p010le,format=bgra` を要求するため、SW画像を暗黙に使った処理成功をハードウェア復号成功と誤認しない。
- raw HEVCへのソフトウェア復号とGPU経路を同じAnnex B入力に対して比較。BGRA全画素SHA256・バイト数、wall時間、プロセスCPU時間、ピークワーキングセットを記録。GPU側の画素不一致・HW入力失敗は `FALLBACK` と記録し、製品採用を認定しない。
- 実Intel GPUを含まないActions環境では `HEIC_GPU_FIRSTFRAME_NOT_PROVEN` を出し、正常な「非実証／フォールバック」結果として扱う。GPUがないことをユーザー環境の非対応と誤認しない。
- `.github/workflows/heic-gpu-path.yml` を新研究ブランチでのみ拡張し、既存ネイティブ抽出と回転クロスチェックの後に試行。元HEICのSHA256を監視し、研究専用のRUNNER_TEMP以外に出力しない。

## 現時点で未確認
1. この研究ブランチの新しいCI実行ログはまだ取得できていない。ワークフローの編集/コミット成功と、Actionsの完走/成功は区別すること。既存2 CIの成功を今回のCI成功と誤表記しない。
2. Windows CIのソフトウェアDXGIアダプタだけでは、Intel GPUで実HW surfaceが出たことを確認できない。第7世代i5のGPU型番、対応するMain/Main10、4032×3024のdecoder configurationは未測定。
3. Main10のP010読み戻しやFFmpegのGPU色変換がデバイス別に期待どおりか、実GPUがないため未検証。画素不一致ならフォールバック。
4. raw BGRA比較は「HEVC未回転1フレームの一致」の確認に限定。HEIF側の回転・ICC/NCLX・HDR・alpha・縮小補間は、既存の独立metadata検証に加え、実GPU後のサムネイル画像最終比較が必要。
5. 現実のエクスプローラー高速化にはプロセス起動コスト削減が不可欠。研究FFmpeg CLIを製品のthumbnail handlerにそのまま導入しない。最終的にはin-processのAVCodec/D3D11VA経路等で同一出力品質・低CPU・並列実験を行う。

## 継続の判断
- 研究のGPU復号経路で1画像でも実HW readbackと画素再現が確認された場合に、init/device再利用・GPU→CPU転送・BGRA色変換・256px縮小のコストを分離し、10～30件の並列負荷試験へ進む。
- 原本保護、未知画像fail-closed、既存WIC/Photos/HEIC Manager非干渉は解除しない。MSIは作らない。
