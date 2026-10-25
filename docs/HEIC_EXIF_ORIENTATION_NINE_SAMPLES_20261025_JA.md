# 2026-10-25：正常に保存できるHEIC 8枚と失敗するIMG_3916のExif Orientation比較

**重要**：原本は読み取り専用で解析。ユーザーの画像本体・Exif/GPS・フルハッシュ・個人パスをGitHubには保管しない。対象の9枚はユーザーが2026-10-25に提供したIMG_3931.zip中のHEIC。保存可否はユーザーの実機報告に基づく。解析は独立したlibheif`heif-info`と、libheif C APIで主画像に関連付いたExifデータを取得してTIFF IFD0のタグ0x0112 (Orientation)を生バイトから直接読み取った。

| 画像 | byte size | HEIF-visible size | TIFF IFD0 Orientation | primary `irot` (CCW) | embedded thumbnail | Win10 Photos回転保存 |
|---|---:|---|---:|---:|---|---|
| IMG_3916 | 1,738,419 | 4032×3024 | **3** | **2 = 180°** | 320×240 | **失敗** |
| IMG_3917 | 3,824,310 | 3024×4032 | **Exifなし** | なし | なし | 成功（ユーザー報告） |
| IMG_3919 | 2,978,891 | 3024×4032 | **6** | 3 = CCW270° | 240×320 | 成功 |
| IMG_3920 | 2,100,886 | 3024×4032 | **6** | 3 = CCW270° | なし | 成功 |
| IMG_3921 | 1,294,883 | 3024×4032 | **Exifなし** | なし | なし | 成功 |
| IMG_3922 | 6,587,162 | 3024×4032 | **Exifなし** | なし | なし | 成功 |
| IMG_3923 | 2,068,851 | 3024×4032 | **1** | 3 = CCW270° | なし | 成功 |
| IMG_3931 | 2,889,083 | 4032×3024 | **1** | 0 = CCW0° | 320×240 | 成功 |
| IMG_3932 | 2,567,528 | 4032×3024 | **1** | 0 = CCW0° | 320×240 | 成功 |

比較結果：失敗画像だけがOrientation=3、primary irot=2。ほかの正常画像のOrientationは1（3枚）、6（2枚）、Exif自体なし（3枚）。**単にExifとirotが共存することが失敗条件ではない**。Orientation6+irot3でも成功している。

IMG_3916・IMG_3931・IMG_3932ではいずれも4032×3024、8×6 grid、320×240 embedded thumbnail、Apple gain-map auxがある。これらの項目は失敗を区別しない。
IMG_3916とIMG_3923は過去に個別提出されたものと今回ZIP内でSHA256が完全一致した。

## 自作WICソースと一致する2つの既知不足
1. `wic_heic/ExifOrientationFix.h::NormalizeExifForRotatedPixels`は`dimensionsSwapped`が偽なら即座に処理終了し、Orientation=6/8のみを1へ正規化する。180°は幅/高さを交換しないため、IMG_3916の主画像がlibheifでiROT180適用済みでもExif=3をWindows Photosへ露出したままになる可能性が高い。Photosで表示中に自動で180°回転する目視症状と符合。
2. `wic_heic/HeicLosslessNative.h::NormalizeExif`はOrientation=3を`Bad("Unsupported Exif Orientation")`で拒否する。IMG_3916を無再圧縮のHEIF `irot`更新経路へ通せない具体的根拠。Exif=1や旧iROTが奇数の場合のExif=6/8とは処理が異なる。
3. ただし**Photosの保存UIエラーが上記単独で確定したとは言わない**。元ソース圧縮バイトが取得できない`opaque-raster`保存ではHEVC再エンコードへフォールバックし得る。最終`HRESULT`のWICログを取得できれば保存エラー位置を確定できる。

## 実験済みの事実（個人画像ファイルをコミットせず）
IMG_3916の実験コピーだけでExif3→1、irot2→1の計2バイトを変更し、元HEVCデータ・HDRゲインマップ・ICC/サムネイルを完全に維持。独立libheifで時計回り90°の全RGB画素が完全一致。HEIFメタ回転の技術的実現可能性は実証済み。Windows Photos実機でのWIC保存成功ではない。

## 修正上の注意
- 出力済みピクセルにHEIF irotが適用されていることをコンテナーから確認して**180°のExif3のみ**正規化すべき。Exif Orientation3単独でHEIFのirotが存在しない画像を自動的にOrientation1へ変えてはいけない。
- 90/270の既存正常経路とExifを持たないWIC産出HEICを退行させない。
- フォトで保存失敗した実際の`HRESULT`は未収集。画像資料は十分揃っているので写真追加提出を求めず、必要なら一回の失敗時WICトレースのみ。

## 横断資料
- HEIFコンテナー回転が正でExif Orientationは表示に再適用しない：https://pillow-heif.readthedocs.io/en/stable/workaround-orientation.html
- libheifにおけるExif回転二重適用の問題：https://github.com/strukturag/libheif/issues/227
- Exif Orientation=3が180°に対応する定義・相互運用マッピング：https://aomedia.googlesource.com/libavif/+/f4948db213b7cd0ff7488b1ac5567f29cdd01539/src/exif.c
- Microsoft WIC Exif メタデータ読取：https://learn.microsoft.com/en-us/windows/win32/wic/-wic-codec-readingwritingmetadata
