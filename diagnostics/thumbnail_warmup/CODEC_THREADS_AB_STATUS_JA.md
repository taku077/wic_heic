# HEIC初回サムネイル高速化：HEVCデコードスレッド数A/B試験

## 範囲
実験ブランチ：`experiment/12mp-codec-thread-ab`。配布中の `WicHeic Fast Thumbnail Preview v0.2.0` は変更しない。実験用WIC DLLの再ビルドはGitHub Actionsの一時的なWindows Server 2022でのみ行い、ユーザーPCにはインストールしない。

## 確定した遅延
1,200万画素の人工HEIC（4032×3024）の4件で、v0.2.0に相当するデコード経路のサムネイル描画中央値 `1054.63ms`、大半が HEIFデコード+WIC縮小の `CopyPixels`。比較基準CI：`https://github.com/taku077/wic_heic/actions/runs/39569741001`。

## A/Bテストの構成
- `wic_heic/HeicBitmapFrameDecode.cpp` の **CI専用コンパイル定義** `WIC_HEIC_PRIVATE_THREAD_EXPERIMENT` が有効な場合に限り、libheifの `heif_decoding_options::num_codec_threads` を切り替える。0は従来のC++ラッパーデフォルト経路そのまま。
- 1、2、4は明示的に指定し、順序 0→1→2→4→4→2→1→0 で計8回測定する。画像1枚ごとにWICデコーダーとストリームを新規生成する。
- 同一の人工12MP HEICから生成された256px・32bpp PBGRA縮小サムネイルの**全ピクセルハッシュ一致を必須**にする。異なれば失敗として不採用。
- 各サンプルの `CopyPixels` 時計時間、プロセスCPU時間、プライベートメモリを測る。最後にファイルSHA-256の不変を確認する。
- 既存のWIC登録、Explorerや「フォト」、HEIC Managerは変更しない。
- **各モード2回は予備調査で、厳密な速度改善の証明ではない**。改善候補があれば独立ランナー・ABBA再現試験、画像種類の追加、メモリ上限を通過後に配布MSIへ検討する。
- libde265内で既定スレッド数が1であることを確認。i5第7世代ユーザーPCでの最適スレッド数は未確定。

## 結果
最新のCI：`https://github.com/taku077/wic_heic/actions/workflows/codec-threads-ab.yml`。

数値はCI完了・ログ精査後に追記する。標準より遅い・CPU増が大きい・画素不一致が出る場合は高速版として配布しない。
