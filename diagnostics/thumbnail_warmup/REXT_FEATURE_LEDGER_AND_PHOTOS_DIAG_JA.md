# HEIC機能台帳／Windows 10フォト警告 ― 研究ブランチの確定・未確定を分離
日付: 2026-10-22
対象: taku077/wic_heic / experiment/heic-gpu-inprocess-avcodec

## 目的と絶対条件
- 本番v0.4.0と各研究枝の機能を取り違えず再利用するための索引。
- 目標はWindows 10 Explorer の **HEIC初回サムネイル実描画30%以上短縮**。CIのwarm cache、未登録WIC・シェルProvider・Photos表示時間を目標値に混ぜない。
- 既存製品ソース wic_heic/HeicBitmapFrameDecode.cpp (Git blob c8b08d7adbb47880cdb1d176720d89efbce2ee48) は研究中変更禁止。main/master、レジストリ、MSI、HEIC Manager、本物のHEICを変更しない。

## 版／経路別の機能（必ずスコープを記載）
| 対象 | 技術／実証 | 本番か／条件 |
|---|---|---|
| 以前のv0.3.0 | 初回表示の旧比較ベース | 実機数値は別の試験ログに依存 |
| 現在使用中v0.4.0 | **同一画像のHEVC復号ワーカー2/4**の適応制御。主に8bit・6MP以上・alphaなしで適用。複数画像デコードが同時なら2等に調整 | 製品側実装が存在。適用条件以外では改善を主張しない |
| 研究GPU Main/Main10 | Intel HD620+ D3D11VA による人工12MPの約48～55%短縮、元CPU WIC Fant完全一致の一部試験 | **人工画像だけ**、実RExt 7件はGPU適格外。製品無効 |
| 研究HEVC RExt分類 | 7/7のhvcC/SPSがRExt profile_idc=4 | この7件だけの事実。個人写真を全件RExtと推定しない |
| 研究10/12bit LUT | 厳密な16bit値正規化の整数式と全画素一致。04.heicの総256px WIC Fant約9.5～10.2%改善 | 未登録・CI研究コピーだけ |
| 研究1/2/4 **画像間**並列 | 7枚暖機済み一括Provider生成でLUT+2並列が約42～43%短縮。単一先頭画像は約4.5% | 製品未導入、既存Explorer並列化と重複のおそれ。CPU時間増 |
| 研究HEVCワーカー1/2/3/4 × LUT | 単一10bit RExt実HEICで04: **29.498/29.520%**、回転約28%、alpha約21%まで | 未登録WIC CIだけ。4スレッドのCPU仕事量増。Explorer未達 |
| 追加研究 commit 1513cc9a | 各単一画像のHEVC+RGB、WIC生画素コピー、下流Fant等の残り、CPU時間、終了時private commitを相互比較する計測。全画素一致ガード維持 | **この計測機能自体のWindows CI確認待ち**。終了時メモリをピーク値と呼ばない |
| HEIC Manager | 独立プロジェクト、Viewerや独自Explorerサムネイル | この研究ブランチから変更しない |
| Windows標準Photosの編集 | 過去にHEIC回転保存・幾何学的歪みの症状あり、診断専用ブランチとWIC呼出し調査あり | 改善未確定、**既存画像の回転を試験しない** |

## v0.4.0使用中の新症状と仮説（2026-10-22）
- スクリーンショット IMG_3915.HEIC、3024×4032、フォトに「完全な解像度で表示するにはHEVC ビデオ拡張機能が必要」と表示。編集ボタン無効／回転不能との申告。
- この表示はPhotos自身の**フル解像度用のHEVCデコード経路が使用可能とは認識されていない**状況と整合的だが、WICデコーダーの動作不良、フォトのアップデート、AppX/Codec登録、Media Foundation HEVC MFTやライセンスのいずれかをこの画像だけで特定できない。
- Microsoft公式サポートは、Windows 10の標準フォトでHEICを扱う場合にHEIFとHEVC両方の拡張機能を要するケースがあると明記。独自WICデコーダーがExplorerで動いても、Photosの編集経路の要件を満たす証明にはならない。
- 現行の `diagnostics/photos_trace/WIC_Preflight_ReadOnly.ps1` に**現在ユーザーのHEIF/HEVC関連AppX名称・バージョン・状態**の読取専用棚卸しを追加。従来のWIC DLL SHA・CLSID・MSI・OS・Photosバージョンの棚卸しを保持。
- **注意**: AppXがない=Media Foundation MFTがない、と断定不可。AppXがある=Photosから有効/ライセンス確認済みとも断定不可。前後WIC比較スクリプトのSchema=1と既存照合を維持する。
- 実機のPhotos APIロード経路調査はまだ未実施。既存の診断フレーム `diagnostics/photos_trace/` は未承認のMSI差替えを禁止、人工HEICと復元可能性・ロールバック・原本不変を必須とする。
- **現在の機能を壊さない改善方針**: WICコンポーネントとPhotosのMedia Foundation/HEVC要件を切り分け、読取専用棚卸しを先に行う。正体が不明のHEVC拡張パッケージや非公式配布物は導入しない。購入も前提にしない。写真の一括再変換・回転による検証はしない。

## 開発の次の判断点
1. 上記単一画像ステージ分解のCIで、4ワーカー×LUTの残る制約がHEVC/RGBか、コピーか、Fant/alphaかを判定する。ステージ毎の中央値は合計しても総中央値に一致しない。
2. 30%達成を目的とした**新しいアルゴリズム候補がある工程だけ**を追加研究し、独立CI・全画素一致・CPU増・メモリ増をセットで比較する。ノイズだけの30%超を採用しない。
3. 画像間並列はExplorer自身の並列化確認までは製品へ移植しない。
4. Windows 10実機テストはCIの重要な節目に一度だけ計画し、復元・原本保護と明確な合格ゲートを持たせる。

## 根拠
- 研究主台帳: diagnostics/thumbnail_warmup/GPU_INPROCESS_AVCODEC_RESEARCH_JA.md
- CI: https://github.com/taku077/wic_heic/actions/runs/39755061819
- 安全性: https://github.com/taku077/wic_heic/actions/runs/39755711692
- Photos UI/HEVC: https://support.microsoft.com/ja-jp/windows/apps/photos/photos-app-video-editor-error-can-t-view-this-file-type
- WIC HEIF codec: https://learn.microsoft.com/ja-jp/windows/win32/wic/heif-codec
- Windows 10 Photos診断: diagnostics/photos_trace/WINDOWS10_PHOTOS_SAFETY_GATE.md
