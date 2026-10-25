# WicHeic 1.1.25 — Windows 10 Photosでの irot=2 / Exif Orientation=3 修正候補（2026-10-25）

**状態：人工HEICのWindows CIで表示Exif・90度回転保存とも成功。ユーザーWindows 10標準フォトの実機では未確認。** 2026-10-27（火）20:00 JSTのMSI提出期限は維持し、以前のMSIを達成済扱いにしない。

## 発端
ユーザーのHEIC9枚を比較した結果、標準フォトで保存失敗し開くと180度回転して見えるIMG_3916のみ、主画像の**HEIF irot=2（180度）＋TIFF Exif Orientation=3（180度）**が同時に存在した。他8枚はExif1、Exif6、あるいはExif項目自体がなかった。画像容量やHDRゲインマップだけでは失敗を説明できない。
検査は私的な読み取り専用コピーに限定。本人のHEIC、Exif/GPS、実ファイルパス、PNG等をこのリポジトリに保存しない。

## 修正の要点
1. `ExifOrientationFix.h`に、既にHEIF irotで180度回転済みの表示画素について**WICが公開するExif Orientation=3のみ1へ正規化**する分岐を追加。HEIC原本やファイル内部Exifは変更しない。Exif3だという情報だけで無条件に変更しない。
2. `HeicLosslessNative.h::PrimaryHasIrot180`はHEIFの`meta/iprp/ipco/ipma`から**主画像の実在するirot=2を認証**。 `HeicBitmapFrameDecode.cpp::GetMetadataQueryReader`は主画像でExif3を検出した時に限り圧縮HEIFを読み取り、認証に成功した場合のみ正規化フラグをメタデータリーダーへ渡す。通常のExif1/6、Exifなしの画像ではHEIF再読込を追加しない。
3. `HeicLosslessNative.h::NormalizeExif`で、旧primary irot=2かつExif3の場合に限りExif3→1を許可。既存irotを更新して主画像/関連サムネイル/HDRの圧縮データ・ICC/XMP・Exifの他フィールドは変更しない。不明/不整合はfail-closed。
4. 既存の90/180/270、Exif1/6/8安全分岐、ソース不明のラスタは通常再エンコードの安全条件を維持する。

## Windows GitHub Actionsでの人工HEIC検査
- 公開済み人工HEICから一時的に`irot2+Exif3`の人工テスト入力を生成。`tools/test_lossless_heic_rotate.py`。ユーザーの写真は未使用。
- `WIC_HALF_TURN_IROT2_EXIF3_NORMALIZED_TO_1_PASS`：WIC queryでExif1と見える。
- `NATIVE_IROT2_EXIF3_BYTE_EXACT_ROTATE_AND_FAILCLOSED_PASS`：期待出力と全バイト一致、irot2でないExif3は安全拒否。
- `WIC_PHOTOS_OPAQUE_IROT2_EXIF3_CW90_LOSSLESS_EQUAL_PASS bytes=10597`：Photos型の`CreateNewFrame`オプションなし、ラスタ入力、エンコーダーcommitまでの保存経路が期待HEICの全10,597バイトと完全一致。
- 従来の90/180/270回転、1画素変更時の再圧縮、安全な矩形・stride、MSIの隔離インストールとアンインストール後WIC登録状態復元も回帰試験成功。
- Windows CI：#40195086971 **SUCCESS**。これはWindows 10標準フォトGUI実機の再現とは別。

## 実機テストへの指示
- **旧1.1.24を手動削除・WindowsのHEVC拡張を購入・HEIC Managerを書き換える等は不要。**
- 1.1.25導入前に最新版の専用復元キット`00_Check_Installed_WIC.cmd`で現状1.1.24のDLLハッシュを確認。旧バージョンからのMSI `MajorUpgrade`対応はWindows CIでのみ確認、ユーザーPCでの復旧動作は未検証。
- IMG_3916 **元ファイルでなく試験用コピー**を小さい空フォルダーに置き、最初の表示時180度回転が出るか確認し、90度回転保存できるかを確認。保存できたらサイズ・見た目とExif/irot維持を調べる。
- 1.1.25でも保存失敗なら、診断ツール`HEIC_Rotation_MultiRotate_Diagnostic_v0_4`などで原本をコピーして一度だけ再現し、**WIC_Trace.txt と Diagnosis_Report.json** を共有。個人HEIC原本を再アップロードする必要はない。
- 原因断定は「その条件だけの修正で同じ実機・同じ写真の不具合が解消」と確認できれば非常に強くなるが、追加の表示キャッシュ問題などが併存する可能性は残す。

## 参照資料
- pillow-heif HEIFとExifの表示方向：https://pillow-heif.readthedocs.io/en/stable/workaround-orientation.html
- libheif二重回転の報告：https://github.com/strukturag/libheif/issues/227
- Microsoft WIC metadata：https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-metadata
- Microsoft WIC HEIFオプション：https://learn.microsoft.com/en-us/windows/win32/wic/heif-codec

## 2026-10-25 実機での原因切り分け：Exif Orientationだけの修正で保存成功

**ユーザーが報告した実機確認結果（新1.1.25 MSIの導入成功報告ではない）:**
- IMG_3916の原本は未変更。原画像の複製でExif Orientation **3→1だけを1バイト変更**したHEICを、ユーザーのWindows 10標準フォトで開いて回転保存したところ、**正常に保存できた**。
- 修正前後の HEIF `irot=2`、画像サイズ1,738,419バイト、HEVC圧縮データ、HDR補助画像、埋込サムネイル、その他Exif/ICCは変更していない。従来のIMG_3916は同じフォト環境で回転保存失敗と報告。
- したがってExif Orientation 3の重複・不整合は**保存失敗の主要因である可能性がきわめて高い**。ただしこの成功試験のみで保存内部のWIC HRESULTや画素の完全保全を証明したとはしない。表示中の自動180度回転が消えたかは今回の報告には含まれない。
- **追加添付** `HEIC_Rotation_MultiRotate_Diagnostic_v0_4(2).zip` を解析：2セッションとも、`reference/original-reference.heic`および`work/rotate-working.heic` は**修正前のIMG_3916（Exif Orientation=3、1,738,419バイト、同一SHA256）**だった。修正版Exif1はZIP内に存在しない。
- 1セッションに診断JSONがあるが、チェックポイント後も原本/作業コピーのSHA256に変化なし、ファイル監視イベント0件、WICログの`EncoderCommitLossless`/`EncoderCommitRecompressed` 0件。両セッションで`EncoderSetThumbnail hr=0x88982F81`（未対応）と`MetadataWriterUnavailable hr=0x80004001`（未実装）を観測したが、**これらだけが回転保存UIエラーの直接原因と断定しない**。今回の診断ZIPはExif修正版の保存成功を機械的に追跡したログではない。
- 特に**Exifだけ変えて正常保存した事実**と、今回の**修正前データの途中までしか記録していないログ**は混同しない。先行リリース1.1.24と提案候補1.1.25の動作実績も混同しない。
- 次は本試験結果をもとに1.1.25の改善範囲を絞り、画像・自作HEIC Manager・Explorer設定やキャッシュを不用意に変更しない。締切は2026-10-27 20:00 JSTから延長しない。

## 2026-10-25 実機投入前の追加安全試験（画像処理実装変更なし）

ユーザーは1.1.25 MSIを**まだダウンロード・インストールしていない**。現行インストールは1.1.24と報告。Exif3を1へ変えたIMG_3916コピーは1.1.24でも正常保存できたが、元のExif3のIMG_3916で1.1.25が正常動作するかは未確認。

**判断:** 1.1.25を小さな空フォルダーの**元Exif3を持つIMG_3916の新規コピー**で試す価値が高い。新1.1.25は元HEIFを直接書換えるのではなく、WICのメタデータ公開と圧縮データに対する保存時の既存無再圧縮回転を修正。もう一枚、正常画像IMG_3923のコピーで回帰確認。実画像原本を絶対上書きしない。00_Check_Installed_WIC.cmdの正常判定と旧1.1.24 MSIの保全を導入前提とする。Microsoft Photosを閉じて旧版MSIの手動アンインストールをしない。

安全なnegative-control回帰も追加：
- 公開人工`irot=2+Exif Orientation3`と、その`irot`の1バイトだけを2→0へ変えた`irot=0+Exif Orientation3`を作成。写真本体のアップロードなし。
- **後者ではWICがOrientation=3をそのまま公開**することを自動試験。Exif=3だけで無条件に1へ変更する実装ではない。
- CI #40199504651 **SUCCESS**：
  `WIC_HALF_TURN_IROT2_EXIF3_NORMALIZED_TO_1_PASS`、
  `WIC_NO_HALF_TURN_EXIF3_PRESERVED_AS_3_PASS`、
  `WIC_PHOTOS_OPAQUE_IROT2_EXIF3_CW90_LOSSLESS_EQUAL_PASS bytes=10597`、
  `WIC_RESTORE_COMPARISON=PASS`。
- **今回の2コミットはCI人工テストの追加のみ**で、先に発行した1.1.25 MSIのバイナリー本体は変更していない。GUI実機試験はユーザーが実施するまで未検証。

引き続き未解決：Windows Photosの大量フォルダーでの数秒遅れの表示変形、Exif/XMPのラスタ再エンコード時欠落、品質92の再圧縮による容量増、連続回転の4回目における再エンコード、`.tmp`作成主体の未特定。これらはIMG_3916のExif3修正成功だけで解消済としない。

参考：
- WIC HEIF no-recompression options: https://learn.microsoft.com/en-us/windows/win32/wic/heif-codec
- pillow-heif Exif orientation: https://pillow-heif.readthedocs.io/en/stable/workaround-orientation.html
- Microsoft Photos Windows App SDK transition: https://blogs.windows.com/windowsdeveloper/2024/06/03/microsoft-photos-migrating-from-uwp-to-windows-app-sdk/
