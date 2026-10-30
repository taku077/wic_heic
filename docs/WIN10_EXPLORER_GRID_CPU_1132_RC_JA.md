# WicHeic 1.1.32 — Explorer CPU高負荷対策・Windows10実機評価候補

## 結論

この候補は **COM Surrogate（dllhost.exe）のCPU高負荷を下げるための修正版**。表示速度の高速化は主目的ではない。前回の1.1.31でCPU最大87%、記録区間平均43.05%が観測され、高CPU問題が未解決だったことを受けて開発した。

## 主な変更

- Windows10 2コア/4スレッドの `dllhost.exe` に限り、libheifの独立したgridタイル並列数を4→**2**へ制限。
- 画像が実際に複数タイルのgridで、既存の6MP以上/8bit不透明/単独復号条件も満たす場合のみ、HEVC内部ワーカー数を2→**1**に制限。通常の単純HEICのHEVC 2ワーカーポリシーは維持。
- **Windowsフォト/他のWICホスト/高ビット深度・アルファ画像の従来のコーデック選択を変更しない**。
- libheifの**公開C API**を利用する小規模なコンテキスト管理ラッパーを新設し、HEIF内容や画像バイトを変更せず、画像ごとのタイル並列上限を設定。既存のImageHandle、ICC、Exif、回転保存などのコードを流用。
- フォトやExplorerの既定アプリ、他社Shell拡張ハンドラー、既存の画像ファイルには手を加えない。

## Windows Server 2022 CIのCPU測定

- [実際のWIC経由の48タイル12MP試験 #41026999004](https://github.com/taku077/wic_heic/actions/runs/41026999004) SUCCESS。4096×3072の人工grid HEIC、各設定5回ずつ中央値。
- 従来グリッドタイル4 / HEVC4：**CPU 343.75ms、復号126.29ms**。
- 改善後グリッドタイル2 / HEVC1：**CPU 234.38ms、復号142.48ms**。
- CPU時間 **約31.8%減**、個別復号時間 **約12.8%増**。全解像度RGB画素**完全一致**。数値ログで実際の`tile2/codec1`選択を確認。
- 同じCIの通常12MP HEICではワーカー2/4の画素一致、従来Photosとその他ホストのポリシーを維持。
- これらの数値は**Windows10実機のCPU使用率を保証せず、実機87%から何%まで下がるかは未測定**。

## バージョン安全性・導入前の確認

- 候補MSIはProductVersion 1.1.32。1.1.31のMSIを保管してから導入すること。旧版へ戻す必要があれば、新版をアンインストール後、旧版を再インストールする。
- ユーザーの原本には一切編集・上書き保存を行わない。IMG_4074は試験に使わない。回転保存試験は人工HEICのみ。
- Windows10 Pro 22H2 x64向け。CIのWindows Server 2022でMSIインストール/アンインストール・WIC登録復元・EXIF/ICC/回転保存/画像読取の回帰がすべてSUCCESSにならない限り配布しない。
- 以前のWPR（2026-10-29および10-30）を使い、長時間WPR/ETLの追加採取を何度も要求しない。ユーザーが実機で気軽に確認する場合は、タスクマネージャーでCOM Surrogateの最大CPU、40%以上など高負荷が続く時間、Explorerのサムネイル消失やPhotosの応答・回転保存を確認する。

## 配布ファイル（CI合格後）

- **MSI本体単体**と**MSI・本書・SHA-256のみの軽量ZIP**を同時配布。チェックサム・ZIP CRCを検証する。大容量人工HEICや生ETLは軽量ZIPへ含めない。
- ChatGPT内のダウンロードに失敗した場合のためGitHub Actions ArtifactsダウンロードURLを必ず併記。
- GitHub Actions CI合格をもって「Windows10実機で高負荷問題解決済み」とは表示しない。

## 参考

- [libheif tile-worker C API](https://github.com/strukturag/libheif/blob/v1.23.7/libheif/api/libheif/heif_decoding.h)
- [libheif tiling API](https://github.com/strukturag/libheif/blob/v1.23.7/libheif/api/libheif/heif_tiling.h)
- [Microsoft WIC Decoder](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapdecoder)
- [従来の実機CPU高負荷解析](WIN10_HEIC_CPU_WPR_20261030_165304_JA.md)
