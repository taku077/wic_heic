# HEIC表示方式比較1～7 統合再開点（2026-10-24 JST）

## 目的とスコープ
Windows 10 Pro 22H2 build 19045 の標準「フォト」（2026.11090.22001.0）で、HEVC有料拡張なしに HEIC をフル解像度表示・編集・回転し、再圧縮なしで安全に保存できるようにする。JPEG同様の向き／寸法・EXIF・ICC・その他の対応済み付帯データの保全。研究CI成功だけでPhotos実機合格としない。
この文書は、過去チャット「HEIC表示方式比較1～7」での既存作業を再実装しないための短い索引。詳しい赤緑証拠と制約は同フォルダの PHOTOS_120_REAL_FAILURE_CAUSE_MATRIX_20261023_JA.md、WINDOWS10_PHOTOS_SAFETY_GATE.md に保存されている。

## 2026-10-24 再開時のGitHub照合（read-only調査）
- 作業ブランチ: investigate/photos-wic-arbitration-after-120-failure
- コード安全修正HEAD: 428aff2f274c98ece6e8157f65753fb2066d7edd （出力試行後の同一encoder再使用を禁止）
- 照合時点のドキュメントHEAD: 25f7b2504fa6e040d651fe39e64265cab312ca21
- 直近実行CI: https://github.com/taku077/wic_heic/actions/runs/40003964073 SUCCESS（コード修正428aff2を検証）。25f7b25自体は文書コミットで別CIは観測されていない。
- 本文追加でHEADが更新されるため、再開時には **必ず再度 git branch HEAD/コミット/Actions を読み取り専用で確認** すること。古いSHAへ強制上書きしない。

## ユーザー実機で確定した現状
- HEIF Image Extension 1.2.48.0 は導入済、HEVC有料拡張なし。以前のMF診断でHEVC decoder/encoder=0だった（これはフォト独自の権限検査までは測らない）。
- 実機で A(v1.1.18)→B(v1.1.20)→A(v1.1.18) の比較結果あり。v1.1.18ではHEVC警告が消え、編集/回転UIが有効。v1.1.20では警告が再出現、UI無効。元版に戻すとUI有効に戻った。インストールとDLL登録の一致は診断済。
- v1.1.18は **正常動作版ではない**。実機で回転後の画像変形、Explorer/フォトの向き不一致、保存エラー・メタデータ欠損問題が既知。比較可能なのはUIが有効になる事実だけ。
- 旧1.1.18のDLLがPhotosプロセスへロードされたことは読み取り専用診断で観測済み。ただしAPI呼び出しや保存経路の実証ではない。
- 既存v1.1.18を保持。サムネイル専用WicHeicFastPreview v0.4.0は現在アンインストール済み。自作HEIC Managerとは別系統で干渉しない。

## 既に完了: 重複実装しない
- 旧v1.1.18との同じ公開人工HEICを使ったWIC自動デコーダー選択・QueryCapability 0x15 vs 0x0C・GetColorContexts空ICC/実ICC比較。旧SameEncoder/Thumbnailの無条件広告は不正確。切り戻してPhotos動作改善と断定しない。CI #39939819747, #39942508191, #39962636837。
- QueryCapabilityの任意メタ/サムネイル例外で主画像ビットまで消失するバグ修正。取得色プロファイル改善。旧版のWIC登録値/CLSID相違の調査。Photos DLLロード読取専用診断の作成・実施。
- 元の自作HEICフレーム + 正式な IPropertyBag2(BitmapTransform, HeifCompressionMethod=None) の直接WIC無再圧縮90度×4回転・RGB 6,220,800バイト一致、EXIF向き正規化、対応する人工メタ情報全バイト維持。CI #39950063821。
- WIC WriteSourceのSetSize/SetPixelFormat省略に対する公式規約対応。ICC削除・別プロファイル設定・明示的な画素形式変更を黙って無視しないガード。CI #39951609128, #39954599445, #39956148293。
- WIC FlipRotator / FormatConverter / Scaler / Clipper など元HEIC出自が失われるラスターを安全拒否。拒否は写真破損予防で、Photos GUIの編集保存成功ではない。CI #39959780026。
- 主画像・フレーム・メタデータ列挙状態、WIC EXIF/HEICルート、フレームCOM IWICMetadataBlockReaderとCOM同一性。CI #39991396516, #40002299677。
- Decoder WIC登録4形式 (RGB24/RGBA32/RGB48/RGBA64)、Encoder登録2形式 (RGB24/RGBA32だけ)、RGBA実HEICエンコード&アルファ読戻し、MTA8同時CopyPixels一致。CI #39995711285, #39996917726。
- 人工IStream short Writeを使う lossless/raster 両方の部分書込み残留0復旧（SetSize(0)が可能な空のIStream限定）。同じエンコーダーでCommit再試行/フレーム追加/ICC変更を拒否。赤 #40001983500, #40002672209, #40003366880; 緑 #40002299677, #40002977203, **#40003964073**。
- HEVC MF/AppX読取専用診断、Windows Server CIでのMSI共存・復元テスト、人工HEIC準備/照合、WICトレースの事前実装。新しい診断キットを理由なく重複作成しない。

## 未達（最優先P0はここ）
1. Windows 10 Photos実UIで、研究版の「HEVC拡張警告が消える」「編集/回転ボタン有効」が **実証ゼロ**。A-B-AからはWICの何らかの差またはそれに連動するPhotos状態の差が重要だが、QueryCapability単体が原因とは確定しない。
2. PhotosがどのWIC APIやMedia Foundation HEVC経路で編集可否を判定し、回転保存へどのIWICBitmapSource/オプション/Encoderを渡すか未同定。Windows Server 2022 CIだけでは検証できない。
3. Photos実機で人工HEIC作業コピーの無変形90/180/270度・保存後再読込・サムネイル角度一致・EXIF/ICC/特殊HEICの整合未確認。旧1.1.18は不合格。
4. HDR/gain map、サムネイル・XMP/MakerNoteなど特殊HEIF関連データ全般を無条件に回転・保存できるという保証はない。
5. 外部IStreamがSetSize(0)非対応の場合の原子的保存は未保証。呼出し側による原本の先行truncateはCodecから復元できない。

## 次に作業する順序（重複を避けるゲート）
1. 本ブランチHEADとActions、原因表・既存 PhotosWicTrace/HEVC MF/プロセス診断を読み取り専用で確認する。最終CI #40003964073を失敗扱いして再着手しない。
2. P0の根本原因へ直接繋がる **実機PhotosのAPI/編集判定経路の証拠** を既存診断能力で収集できるかをまず検討。旧版の反復インストールや同一診断を繰返さない。新コード修正は再現試験で具体的な差が実証された箇所に絞る。
3. 研究版のみのWindows Server WIC試験合格をフォト実UI成功と宣言しない。再現不可能な課題は「Windows10フォト側の要追加証拠」として明記。
4. 実機試験可能な候補ができても、正確な旧MSI/登録/DLLの復元、隔離Windows10での復元試験、ユーザー承認が揃う前に新版MSIを導入・配布しない。IMG_4074を含むユーザー写真は試験に使わず、公開人工HEICの新規作業コピーだけを使用する。

## 確認済み一次資料
- https://learn.microsoft.com/ja-jp/windows/win32/wic/heif-codec (BitmapTransform, HeifCompressionMethod=None)
- https://learn.microsoft.com/en-us/windows/win32/wic/-wic-howwicworks (WIC codec arbitration、デコード・メタデータ)
- https://support.microsoft.com/ja-jp/windows/apps/photos/photos-app-video-editor-error-can-t-view-this-file-type (Windows10 Photos HEIF/HEVC)
