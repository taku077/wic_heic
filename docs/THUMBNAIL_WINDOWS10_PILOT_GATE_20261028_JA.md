# 初回サムネイル高速化：Windows 10実機MSI試験の判定ゲート（2026-10-28）

## 目的と現在の扱い

ユーザー要望：**研究だけを長く続けず、適度な区切りで実機に導入してExplorerの初回サムネイルを比較したい。** ただし頻繁なMSI再インストールは不要。現時点の導入済み版は**WicHeic 1.1.28**。フォト編集・回転保存が可能な現行版を守り、元写真を無変更にする。

研究ブランチ`experiment/thumb-hevc-workers-1128-isolated`はCIだけで`GITHUB_ACTIONS=true`かつ専用マクロがある場合に適応型選択を動かす。**このDLLやこのブランチで作ったMSIをそのまま実機へインストールしても、高速化が有効になるわけではない。** 現在は実機配布禁止。

## 実機試験候補を一度だけ作る条件

1. 12MP人工8bit不透明HEICの256pxサムネイルを標準と適応型で5回以上同一ランナーA/B測定し、**30％以上短縮**、元RGBおよび256px RGB全画素一致。従来研究の約60％はWindows Server 2022人工画像のみ。
2. 小画像、10/12bit、alpha付き、異常bit depthは従来経路へ戻す。8bit alphaは**実際にalpha補助情報を含むことをファイルの生HEIF情報から検証**し、単なる「RGBAでエンコードを要求した」だけでは合格にしない。
3. 異なるHEICコンテナが同時に読み込まれる混在3画像（人工12MP 8bit不透明／人工12MP 8bit alpha／公開人工10bit）のWIC RGBの一致とクラッシュなしを確認する。**WIC現行1.1.28は一部HDR/alphaを24bit RGBに平坦化する仕様**のため、この試験をアルファ精度保持や高ビット深度完全対応と誤表示しない。
4. 専用WICコード・Photos編集/回転保存・ICC/Exif・並列CopyPixels・強制停止時のCOM後始末・既存の全回帰試験とMSI登録復元が成功。
5. Windows10実機へのMSI候補は**1.1.28と判別できる新しいバージョン番号**を用意し、既存の正常動作を壊した場合に**元の1.1.28へ戻せる導入手順と元版の回収先、SHA-256**を付属。新MSIのインストール前に旧MSIの入手・復元方法を確認する。現在の1.1.28と同バージョン番号で上書きしない。
6. 実機Photosでの警告表示・編集/回転・無再圧縮保存・サムネイル方向の回帰確認を一回の導入で実施。不正なら直ちに旧版へ戻す。画像は公開人工HEICの作業コピーに限定。

## 実機計測の具体案

- Windows 10 Explorerの初回表示について、同じ人工HEIC群を同一ローカルフォルダ・同じ表示サイズ・同じWindowsユーザー・同じ電源設定で比較する。記録は初回表示、20枚単位の表示完了時間、CPU/メモリ、エラー枚数。フォルダの元画像のExif・作成日時を更新しない。
- **Windowsのサムネイルキャッシュを無条件で全削除しない。** 必要なら`IThumbnailCache::GetThumbnail(...,WTS_FORCEEXTRACTION,...)`を使い、キャッシュを無視した強制再抽出の時間を再現性ある独立指標として測る。ただし強制再抽出のAPI呼出時間とExplorerが複数枚を表示し終える時間は別々の指標と明記する。
- Shellの標準WICサムネイルプロバイダー・レジストリ優先順位・Windowsフォト関連付けを、速度測定だけのために変更しない。実機試験キットはShellとの統合が確認できる最小の読み取り中心設計とし、強制Explorer終了やレジストリ全消去を要しない。
- MSIはGitHub Actions成果物のリンクをそのまま配布しない。既存指示どおり、**こちらの作業環境へ取得→展開→ファイル検証→再ZIP化してsandboxから配布**する。MSIの毎回配布禁止。

## 達成値と未解決事項

- CI #40530246040：12MP人工HEICの256px縮小は標準1427.335ms、適応型567.721ms、約60.2%短縮。同一ランナー・各5回、画像一致。異なるCOMフレーム2枚の同時復号では自動選択4+2。
- 実機Windows10 Explorerの初回表示改善値：**未検証**。3形式混在・12bit人工HEIC素材も全条件は未検証。公開したシステムAPIの結果から数秒改善したとは断定しない。
- libde265のcodec worker並列効果はHEVC bitstreamのWPP/tiles構造とCPUスレッド数・同時画像数で変化する。ユーザーのPC（2C/4T）でも同等に速くなる保証なし。

## 一次資料と他社実装

- Microsoft Shell `IThumbnailCache::GetThumbnail`：https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nf-thumbcache-ithumbnailcache-getthumbnail
- `WTS_FORCEEXTRACTION` がキャッシュを無視して抽出：https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/ne-thumbcache-wts_flags
- WICと標準Shellサムネイルプロバイダーの関連付け：https://learn.microsoft.com/en-us/windows/win32/wic/-wic-integrationregentries
- libde265 HEVC codec workers：https://github.com/strukturag/libde265
- libvips HEIF埋込サムネイル等：https://github.com/libvips/libvips/blob/master/libvips/foreign/heifload.c
- libheifのHEVC/alpha/10bit対応はバックエンドと版依存：https://github.com/strukturag/libheif

## 1.1.29実機試験用MSIの段階

- 専用候補ブランチ：`candidate/thumb-adaptive-1129-win10-pilot`。**WicHeic 1.1.29**。x64 Releaseで`WIC_HEIC_THUMB_ADAPTIVE_PILOT`を有効化し、実機Windows10では研究用環境変数なしでも、`>=6MP`・luma/chromaとも8bit・alphaなしに適応型復号を適用。対象外のHEICは従来復号経路、例外時のワーカー数はRAIIで復元。Windows PhotosのUI操作と回転/保存を壊さないことをCI/実機双方で必須確認。
- `WIC_HEIC_THUMB_WORKERS_LAB`は研究CIの標準/2/4/auto比較のためだけに併存。`GITHUB_ACTIONS=true`で明示されたモード以外、通常のWindows10では製品自動選択が有効。独立専用`IThumbnailProvider`・レジストリ優先順位の変更は一切含めない。
- WIC DLLの**非CI・無環境変数**の自動選択経路を人工12MPサムネイルで試験し、標準復号の256px RGBと完全一致すること、CIログに`THUMB_PILOT_PRODUCTION_AUTO_SELECTED workers=4`と`THUMB_PILOT_NOENV_RGB_PARITY_PASS`が出ることをrelease gateとする。
- **旧版1.1.28のMSIを手元に保存できていることを実機導入前に確認**。失敗した場合はWindowsアプリ設定から1.1.29をアンインストールしてから、手元の1.1.28 MSIを再実行（`MajorUpgrade`が旧版を新版本体から上書きダウングレードするとは仮定しない）。アンインストール時に保護された元写真・Exif・ICCが変わらないことも確認。古いMSIが不明の場合は新版を導入しない。
- 新しいMSIはGitHub Action成功後、こちら側で取得・展開・ファイルごとのSHA確認・再ZIP化し、最初の1回だけユーザーへ配布。**配布時点では実機Windows10で未検証**であり、必ず1.1.28へ戻す条件を示す。
