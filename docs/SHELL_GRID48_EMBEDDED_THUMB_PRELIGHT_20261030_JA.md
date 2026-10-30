# Shell HEIC高速化：64KiB安全事前判定と正規48タイルgrid回帰試験（2026-10-30）

## 結論

**既存のShell埋込サムネイル試作が利用する判定を、複雑なHEICで軽量化。安全性CI合格。ただしWindows10実機に配布していない。**

CI: https://github.com/taku077/wic_heic/actions/runs/41070865266 （64KiB事前判定）／ https://github.com/taku077/wic_heic/actions/runs/41071529559 （正規48タイルgrid・登録済Shell・画質・復元）ともに **SUCCESS**。

- 従来の `WicShellLabSafety::FromStream` は先に最大24MiBの全バイトをメモリに確保／読み取ってから、grid・補助画像・色・Exifの非適合性を判定していた。
- 新版は**最初の64KiBだけ確保・読込み**、この範囲に `grid`／`auxl`／`dimg`／`tmap` という複合構造のFourCCが存在する場合、**安全な通常WICへ戻すため即座に拒否**。それ以外は従来の厳密な全BMFF解析を維持。文字列ヒットは不適合の**否定的ヒントのみ**として使用し、適合判定には絶対使わない。誤検出しても高速経路から通常WICへ戻るだけで画像は変更しない。
- 旧コードで最初に全ファイル容量分の `Bytes` を確保する問題も修正。複合構造の早期除外では不要なメモリ確保・メディアデータ読込みを回避。
- **過去に提供された1枚のiPhone系HEICを画像原本非変更・解像度復号なしの先頭メタデータのみ検査**。ファイルは1,721,813 bytes、`grid`最初の1173バイト、`dimg`1550、`auxl`1708、`prof`1770を確認。今回の64KiB検査ではこの例で全ファイル読込みを回避可能で、**事前判定読込み／確保量は設計上96.19%減**。実Win10でのCPU効果を測定した数値ではない。個人写真・ファイルパス・EXIF・画像データはGitHub未公開。
- CI人工メモリ内の異常gridマーカー例（131,072 bytes）では `CI_SHELL_COMPLEX_EARLY_REJECT_PASS bytes_read=65536 file_bytes=131072 artificial=1`。**これは無効な合成ビットストリームであり、有効な48tile HEIFではない**ので区別する。
- 別の生成器で、**合法的な8×6=48タイル（4032×3024、504×504 tile）＋埋込320×240 HEVC thumbnail**の人工HEICも実際に生成。CI `CI_VALID_GRID_48_TILES_EMBEDDED_THUMB_CREATED_PASS` を確認。以前のFourCC書換えだけの疑似gridテストとは別。
- この**正規grid人工HEIC**を登録済み隔離Shell `IThumbnailProvider`で抽出し、fast path拒否→WICフル画像フォールバック、画質同一（64×64サンプル RGB MAE 0.00/255、20階調超の差分0%）、出力256×192を確認。CI `SHELL_REGISTERED_GRID48_FULL_WIC_FALLBACK_PASS`、`SHELL_PARITY_GRID48_GEOMETRY_RGB_PASS`。
- 同一CIの単純人工HEIC（gridではない）では、登録済みShell比較が既定93.50ms、埋込サムネイル経路12.00ms（各8画像の中央値、比0.128）。**この速度改善は正規gridのものではない**。gridは依然としてフルWIC復号へ戻る。
- 既存のICC・Exif/irot・alpha・PQ/HLG・埋込なしに対する安全な通常WICフォールバック、RGB差分／向き／Shell登録復元ゲートも継続PASS。
- CIの初期版では「正規alpha 4032×3024が必ず64KiBを超え、auxlを先頭に含む」とする不適切な期待値で失敗。実生成α素材は **37,087 bytes** であったため、全読込して当然。後で合成131,072Bのgridマーカー入りストリームで「読込み64KiB」の検証に変更し、正規alphaの安全なフォールバックは独立して検証した。**この失敗を繰り返さない。**

## いま進めるべき次の変更

- 実機のCPU100%に効かせるには、正規iPhone系の**grid+ICC/HDR/gain map＋Exif**の埋込thmbを、色・向き・品質を維持してフル復号せず出せるかという安全性が次の本質的なゲート。今回の正常grid+thmb人工素材でもまだ高速経路を許可していない。
- 次の研究では正規grid人工素材を起点に、必要なカラープロファイル／補助画像／回転の組合せを人工生成し、**既存フルWIC出力と同一画面における比較**と複雑画像の適格率を証拠に採否判断する。形だけの早期解除は禁止。
- 本番WIC DLLやMSIへのShell登録はまだ行わない。CopyTrans等の第三者ハンドラーを保護し、アンインストール／元登録復旧・Photos回転保存品質を確認できるまで実機試験候補としない。
- CPU100%問題は未解決。Photos側ワーカー数の既研究を繰り返さない。

## 横断参考

- libheif 48タイルgridの公式API、タイル形式一貫性要件: https://github.com/strukturag/libheif/wiki/Reading-and-Writing-Tiled-Images
- libheif v1.23.7 `heif_tiling.h`: https://github.com/strukturag/libheif/blob/v1.23.7/libheif/api/libheif/heif_tiling.h
- libvipsのthumbnail選択（小さい埋込ソースのみ利用）: https://github.com/libvips/libvips/blob/master/libvips/resample/thumbnail.c
- Microsoft Shell `IThumbnailProvider` と `IInitializeWithStream`: https://learn.microsoft.com/ja-jp/windows/win32/shell/building-thumbnail-providers
- Microsoft WICフル画像復号のコスト: https://learn.microsoft.com/en-us/windows/desktop/wic/-wic-imp-iwicbitmapframeencode

**本資料は合格したGitHub実装・CIを記録する目的のものであり、個人画像やETLを含まない。**

## 2026-10-30 23:44以降：正規48タイルの埋込thumb対フルWICの直接画質比較

**CI #41074570057 SUCCESS**: https://github.com/taku077/wic_heic/actions/runs/41074570057

- `wictest/WICShellThumbnailProviderPilot.h` にCI専用`RunGridEmbeddedShadow()`を追加。GitHub Actions上で生成する**正規8x6=48タイルgrid+320x240埋込thumb**を自作WICで開き、`IWICBitmapDecoder::GetThumbnail`で小さいサムネイルを先に復号。その後`GetFrame(0)`からフル4032x3024を復号し、**両者ともWIC Fant→32bit BGRA→256x192**に縮小して比較。ユーザー画像は未使用。
- 64x64点のRGBチャンネル平均絶対誤差 **MAE=0.038/255**、20階調超の差分 **0.000%**。主画像にはICCがないことも検査。厳密な全画素比較、写実的な高周波テクスチャ、HDRや実iPhone複合構成を保証する数値ではない。
- `WicShellLabSafety::FromStream`は今回も**gridの高速経路を拒否するまま**。この検証結果だけで本番gridを許可する実装変更はしていない。構造だけを理由にgridサムネイルが使えないわけではない、という肯定的な**技術可能性**が確認できた。
- CI初回 #41074281935 では、基準画像のフル復号が既存3回のフォールバック回数に加算され `full=4` となり失敗。これは**画質比較失敗でも復号エラーでもない**。基準画像比較と通常フォールバックのWICイベント記録を分離して #41074570057 SUCCESS。
- 次の研究では、WICトレースで埋込／フル復号件数を区別し、両経路の所要時間を測る。その後、Exif/ICC/HDR/alphaなしの**厳密な安全性ゲートに該当するgridだけ**を研究用シェルハンドラーで試す。混在iPhoneフォルダーへの一律の許可はしない。
- 直近の重要な外部セキュリティ情報：libheif v1.23.7で複雑な派生画像のCPU枯渇脆弱性が修正されている（https://github.com/strukturag/libheif/security/advisories/GHSA-48p4-gj45-w9v6）。この研究ブランチのsubmoduleはリリース`v1.23.7`に対応する`12a259a5882dd99d45ec5fc77e296a3b620d1e2e`を指しているとGitHubで確認。これをユーザーのCPU100%の原因だと断定しない。

## 2026-10-30 最終計測：WICトレースで復号経路を分離（CI成功）

- **Windows Actions #41075151932 SUCCESS**: https://github.com/taku077/wic_heic/actions/runs/41075151932 （研究コードコミット `245124e66c9b2c48a4845d1904ed3a70533ff97b`）。正規48タイルgridについて別ファイルのWIC診断トレースを取得し、`CI_GRID48_SHADOW_WIC_TRACE_COUNTS_PASS full_primary=1 embedded=1`を確認。**試験は埋込thumbを1回復号し、その後品質基準用のフル主画像を1回復号する**構成であり、単純な主画像の復号回避だけを計測したテストではない。
- **画質**：64x64 RGBサンプル比較 `MAE=0.038/255`、20階調超の差分率0%、正規grid4032x3024/埋込320x240の両方を256x192に縮小。
- **参考時間**：単回のCI計測で`embedded=0ms`、`full_master=109ms`。0msはWindowsミリ秒タイマーの分解能以下を意味し**処理時間が文字通りゼロではない**。別レイヤーのWIC初期化や再訪キャッシュの影響を排除しておらず、速度比やWindows10実機の性能改善率として扱ってはならない。**多数回・順序入替え・cold/warm分離の比較が次ゲート**。
- 既存の登録済みShellプロバイダ試験も全PASS（本来の単純HEICの8ファイル中央値は既定96.00ms、試験ハンドラー13.00ms、比0.135）。**この高速化はgrid画像ではなく、既存の単純HEICでの成果**。
- **失敗→修正履歴**：#41074281935で独立grid影テストのフル復号1回が既存フォールバック3回に加算されたため`full=4`となった。ログ記録の開始位置を分け、#41074570057 SUCCESS。さらに実際のWIC traceで`full_primary=1 embedded=1`を確認する専用ゲートを追加、#41075151932 SUCCESS。
- **製品コード／レジストリ／MSIは未変更**。gridの高速サムネイル経路はまだ「合格した人工画像の技術可能性」の段階。ICC/Exif/HDR/alpha/auxlや実iPhone画像の色・向き・輝度の同等性を検証するまでは、一般gridを開放しない。
