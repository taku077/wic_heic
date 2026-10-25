# Windows 10 標準フォト HEIC 回転保存候補 1.1.23（2026-10-25）

状態：**Windows 10標準フォト実機では、この新バージョンは未検証**。Windows GitHub Actions CIの人工HEIC検証は成功。提出期限は**2026-10-27（火）20:00 JSTのまま**。以前のMSI候補を期限の最終提出物として扱わない。

## 1. 変更の理由（実機で確認済み）
Windows 10標準フォト・自作WIC 1.1.21の実機WICトレースでは、回転済み不透明ラスタが `WriteSource` に渡され、`CreateNewFrame=no-bag`, `FrameInitializeOptions=default`, `WriteSourceInputClass=opaque-raster`, `EncoderCommitRecompressed` まで記録された。サイズは約27.9%減少し、Exif/XMPが欠落した。向きは正しく90度回転できた。

## 2. 修正 — 標準フォト型の不透明ラスタ保存を無再圧縮回転へ限定的に接続
- `PhotosPureRotationBridge.h`を追加：自作HEICデコーダーが読み込んだ**元の圧縮HEIFコンテナ**を、ファイルへの書込みなしで、同一プロセス内・30秒以内・最大64MiB・原則一画像に限定して一時保持。異なる2枚が同時に候補になれば無再圧縮経路は無効化。ユーザー画像・パス・Exifをログに記録しない。
- 写真の保存時、**WIC側の不透明RGB24ラスターが、同一プロセスで直前に読み込まれた元画像を時計回り90度回したものと全画素一致する場合に限定**して既存の`HeicLosslessNative::Transform`を呼ぶ。
- 元HEVCビット列やタイル構造、ICC、Exif/XMPなどのHEIFデータをそのまま維持し、`irot`と必要なExif回転属性のみ更新する。
- 出力画素の不一致、別画像候補の競合、変換されたICC、切り抜き・明度変更など、元情報が不明・互換性がない条件では**以前どおり再エンコード**。ユーザーの他の編集を無断で捨てて「無再圧縮成功」としない。
- 現時点では**時計回り90度**のみを自動選択。180度・反時計回り90度への自動対応は今回の対象外であり、未対応を無期限の追加研究にしない。
- 従来の元WICフレーム直接入力と明示的`BitmapTransform`を使用した無再圧縮回転経路は維持。
- 共有`irot`を持つ主画像・サムネイル/HDR補助画像に限る属性変更の安全対応は`HeicLosslessNative.h`に反映済み。非共有・構造不明は拒否。

## 3. 既存CI上での機械的な回帰テスト（人工HEICのみ）
GitHub Actions #40178280524 **SUCCESS**：
- `WIC_PHOTOS_OPAQUE_CW90_LOSSLESS_EQUAL_PASS bytes=10597`: **フォトと同じオプションなし・不透明ラスタ入力**から保存し、作成したHEIFがメタデータのみを変更した既存Python/ネイティブ基準ファイルと全10,597バイト完全一致。
- `WIC_PHOTOS_OPAQUE_ONE_PIXEL_EDIT_SAFE_REENCODE_PASS`: 同じ人工HEICの回転済みWICラスターで1画素だけ変更すると、無再圧縮経路へ誤分類せず、従来の再エンコードを継続。
- 既存のExif向き・90/180/270回転・WIC元フレーム保存・ICC回帰が成功。
- Windows CIのMSI導入とWIC保存、MSI削除後WIC登録情報復元が成功。
- **このCIは実際のWindows 10 Photos UIを操作した結果ではない。**

## 4. 注意点と導入制限
- 実際のWindows標準フォトが出力する画素には、アプリ内部の色変換等が含まれる可能性がある。その場合は今回の全画素一致にならず、**再エンコードにフォールバックする**。ここは実機未確認。
- ピクセル変更なしでも、写真のメタデータのみをユーザーが編集したケースでは、WICが該当編集を独立に通知しないため、今回の短時間保存候補を使用する条件と混同し得る。今回のユーザー用途は**純粋な回転ボタンのみ**。その他編集の互換性が確認されるまで重大な制限として扱う。
- 新版1.1.23のユーザーWindows10実機への導入前に、現行1.1.21のバイナリー・COM登録・旧1.1.18復元への安全確認が必要。従来Explorer黒画面事故があった。必ず承認後に人工HEICの独立コピーのみ使用。IMG_4074等実画像、HEIC Managerへの変更禁止。元画像自動上書き・削除なし。
- 今回のコード修正は**メタデータライター一般機能の実装ではない**。無再圧縮が適用できない再エンコードではExif/XMP欠落が残る。

## 5. 横断した参考資料
- Microsoft WIC HEIF codec compression/BitmapTransform: https://learn.microsoft.com/en-us/windows/win32/wic/heif-codec
- Microsoft WIC metadata writer: https://learn.microsoft.com/en-us/windows/win32/wic/-wic-codec-readingwritingmetadata
- Nokia HEIF technical spec (irot/Exif/grid): https://nokiatech.github.io/heif/technical.html
- libheif source implementation and HEIC decoder: https://github.com/strukturag/libheif
- libheif grid-image rotations: https://github.com/strukturag/libheif/wiki/Reading-and-Writing-Tiled-Images
- Exiv2 HEIF/Exif support: https://github.com/Exiv2/exiv2

## 6. 期限前の最終コード差分（Explorer低負荷対策）
実機環境はCore i5第7世代・GPUなし。Windows 10 ExplorerがHEICサムネイルを生成するたびに元のHEIF全バイトを二重読込しないよう、一次画像の一時保持は実行ホストのファイル名が`Microsoft.Photos.exe`、`Photos.exe`、`PhotosApp.exe`、`PhotoApp.exe`のいずれかの場合に限定。Windows CIの`wictest.exe`も人工試験のみ許可。これら以外は以前のWIC経路を使用する。Windows 10実機のPhotos実行ファイル名がこのいずれにも当てはまらない場合、無再圧縮の自動振分けは行われず通常保存となる（未確認事項）。
コード：`wic_heic/PhotosPureRotationBridge.h`。Windows GitHub Actions #40178738765：**SUCCESS**（人工HEIC純粋回転バイト一致・1画素編集フォールバック・x64ビルド・隔離MSI導入削除）。
