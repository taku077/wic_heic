# 2026-10-25 HEIC共有irotによる無再圧縮回転 — 実験とWIC実装

期限：2026-10-27 20:00 JST。研究範囲を増やさず、実機不具合を解決する根拠として保持。

## 1. HEIC回転の原理
HEIFの`irot`は90度単位の反時計回りの変換属性。値0,1,2,3は0,90,180,270度。
例えば現在`irot=3`の画像を時計回り90度回す場合、`irot=2`に変えればよい。HEVCビットストリームの再エンコードは不要。Windows標準フォトの表示・保存経路が属性を使用することは、別途確認する。

参考資料：
- Nokia HEIF Technical Information: https://nokiatech.github.io/heif/technical.html
- Nokia HEIF Reader/Writer irot examples: https://github.com/nokiatech/heif/wiki/II.-HEIF-Writer-Executable-and-Configuration-Examples
- libheif tiled-image transformations: https://github.com/strukturag/libheif/wiki/Reading-and-Writing-Tiled-Images
- HEIF rotation vs EXIF pitfalls: https://github.com/strukturag/libheif/issues/227
- Microsoft IWICBitmapFlipRotator: https://learn.microsoft.com/en-us/windows/desktop/api/wincodec/nn-wincodec-iwicbitmapfliprotator

## 2. 読取専用解析による実証（使用者が試験を明示承諾した提供済みHEIC）
※ユーザー画像のバイト列・Exif・画像データをGitHubへアップロードしない。元画像は不変。
- 元画像のHEIF構成：primaryグリッド（6×8の512pxタイル）、共有irot=3、サムネイル及びHDRゲインマップ補助画像も同じirotを参照。
- 保存失敗後の画像ではタイルが単一HEVC画像に再構成され、irot属性と元のExifデータは失われた。
- 実験用の別コピーだけについて`irot:3→2`、Exif Orientation`6→1`を修正した。ファイル長は全く同じ。差分は **2バイトのみ**（irotの1バイトとExif値の1バイト）。元のHEVC圧縮画像データは不変。
- 独立した`heif-info`で表示寸法`3024×4032→4032×3024`、サムネイル`240×320→320×240`、タイル構造維持。
- 独立した`heif-convert`で両者PNG復号し、元画像の90度時計回りピクセル配列と結果が**全画素完全一致**。
- これらはコンテナ変換による無再圧縮回転の成立証明。Windows標準フォトUIの保存成功ではない。

## 3. Windows標準フォトの保存経路で確認できたことと未確定事項
既存のユーザー診断ログ（1.1.21）で、EncoderInitialize、CreateNewFrame、FrameInitializeOptions(transform=0,compression=0)、SetSize(4032,3024)等が確認された。
しかしDecoderCopyPixels出力が1.5万件を越えて2MiBログ上限を埋めたため、`WriteSource*`及び`EncoderCommit*`イベントは取得できなかった。最終保存画像の実寸は3024×4032だった。このため**標準フォトが編集後の実ラスターだけを渡したのか、当該WICエンコーダー経路が実際の出力ファイルを確定したのかは未証明**。数値一致だけで仮定しない。
ログ出力の間引きはcommit `a0b163ee88085611891bfb87e336bced83f78924`に適用済、Actions #40172549482 成功。

## 4. 既存コードの改善（独立、保守的）
`wic_heic/HeicLosslessNative.h`の`Transform`は従来、サムネイル/HDR補助画像を持つすべてのHEIFを拒否していた。今回、次の**限定条件が機械的に満たされる場合のみ**、共有irotを1箇所変更して複数の関連画像を同時回転するように改良。
- `thmb`/`auxl`の関連画像は**すべて元primaryと同一irotプロパティ**を明示的に共有。
- 各関連画像の`ipma`登録は正確に一つで、独立irot/imir/clapを持たない。
- unrelated画像とirotの共有がない。関連画像に独立Exif回転がない。
- 不明なレイアウトは安全拒否。元HEVCの変更なし。元ファイル直接上書き禁止。
修正コミット：`31fcf67c2ab5182dc5707397cde9ba1fa45bd088`。
インメモリで関連画像が同じirotを持つ場合は許可し、関連画像にirotがない場合や無関係画像に共有される場合は拒否する最小回帰テスト：`wictest/NativeLosslessProbe.h`。

**重要:** WICの入力が単なる加工済み`IWICBitmapSource`なら、元HEVC圧縮データを取得できると推定してメタ回転するのは禁止。Windows Photosがその入力のみを渡す場合、この変更だけでは標準フォトの回転保存は直らない。

## 5. 次の検証
- 標準フォトの完了した`EncoderCommitLossless`/`EncoderCommitRecompressed`ログを含むトレースを取得（削減済みログが実機コーデックに入った後。新MSI導入には復元条件の確認が必要）。
- 元の保存と実機出力の寸法/Exif/ICC/HEVC領域を比較し、どの経路かを確定する。
- 既存無再圧縮コード以外の別実装や回転属性修正で、Photosが加工済みラスタを渡す制約を回避できたとは断言しない。
