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
