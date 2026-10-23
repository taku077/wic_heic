# Windows 10 標準「フォト」HEIC/JPEG操作パリティ 受け入れ仕様

目標: 標準Microsoft Photos 2026.11090.22001.0 を使用し、HEICをJPEGと同じ操作系で扱う。**自作WPFビューアやHEIC Managerを標準フォトに差し替えない。** Windows 10 Pro 22H2 build 19045、ユーザーのHEVC有料拡張なし、HEIF拡張ありの実機条件が基準。

## JPEGとの操作比較
|操作|実装責任|受け入れ試験|状況|
|---|---|---|---|
|エクスプローラーからダブルクリック|Windowsの既定の「フォト」関連付け + WIC|HEICもフォトで開き警告なし|旧v1.1.18実機で一時成功、現行未確認|
|全解像度表示|独立したグローバルHEIC WICデコーダー|原寸全画素の取得、色/向き、JPEGと同等の見え方|WIC CIは一部確認、実機UI未確認|
|ホイール・ボタンによる拡大/縮小|Photos UI + WIC GetFrame/CopyPixels|50/100/200%以上、ジャギー/画像歪み/操作停止後ぼけの有無|新規zoom ROI CI追加、UI未確認|
|フォルダ内の前後画像へ移動|Photos UI・Explorerファイル関連付け|JPEGとHEIC混在で←→移動、画像切替後にも正しい向き・ズーム|独立ファイルの複数回Open/Decodeテストを新設、UI未確認|
|90/180/270度回転|Photos UI + WIC encoder HEIF lossless|元画像保護、再圧縮なし、EXIF/ICC/XMP/MakerNote等完全保持、90度4回と幾何学・色検証|直接WIC APIでは一部合格、Photosの保存経路未確認|
|その他の画像編集|Photos UI + WIC encoder|切り取り・色補正等の保存成功、入力の保護、劣化/未対応の明示|未解決。画素を編集すれば本質的に再圧縮が必要な操作もある|
|サムネイル|既存FastPreview v0.4.0（独立）|Explorer初回・キャッシュ・別機能に不干渉|過去実機確認済の機能を破壊しない|

## 既存実機で確定した重要事実
- 2026-10-16 メーカー向け無料HEVC拡張のStore ID 9n4wgh0z6vhq は「このアプリはお使いのデバイスでは動作しません」で失敗。提案し直さない。
- 2026-10-20 旧WIC v1.1.18では標準フォトで一度回転保存ができたが、ラスター再圧縮とEXIF消失があった。
- 2026-10-22 旧WIC v1.1.18再導入でHEVC警告が消え、編集・回転ボタンが一時復活。保存エラーと向き不一致が残った。Paid HEVC拡張は購入していない。
- 一連の実写IMG_4074・IMG_3915は読み取りと試験にも使用しない。CIテストには生成済み人工HEICだけを使用する。

## 今回の変更
- `wic_heic/HeicBitmapDecoder.cpp::QueryCapability`: Windows WIC公式仕様どおり実ストリームを検査し、無条件の「同じエンコーダー/サムネイルあり」申告を修正。読取後のIStream位置を復元。
- `wic_heic/HeicBitmapFrameDecode.cpp::CopyPixels`: ズーム用ROI・stride・buffer境界を検査してから全画素HEVC復号を実行。不正要求でCPU/メモリを余分に消費しない。
- `wictest/WICDecoderCapabilityProbe.h`: HEIC/PNGの正常な判定、実サムネイルの有無、カーソル不変性を検査。
- `wictest/PhotosNavigationZoomProbe.h`: HEIC A→B→Aの個別デコード・原寸／切り出し領域の画素完全一致を検査。
- GitHub Actionsで隔離Windows環境を使用する。フォトGUIはWindows Serverランナーでは操作できないため、ビルド合格と実機フォト合格を区別する。

## 必須の最終ゲート
* Windows10ユーザー環境の標準フォトの実動作確認までは「完成」「Photos編集対応済み」「MSI提示可能」としない。
* 実機試験には公開人工HEICを新規生成しコピーを利用。元画像上書き禁止、既存テストデータ自動削除禁止、アンインストール/復旧手順の検証。
* 画像の無再圧縮保存・Exif/ICC/HDR等の情報保持と、Photosによる一時的な編集ボタン復活とは別項目。
* HEIC Manager、FastPreview v0.4.0、購入済み/未購入のHEVC拡張を無断変更しない。
