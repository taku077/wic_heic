# HEIC/WIC 開発の横断調査方針（継続指示）
更新日: 2026-10-25
対象: Windows 10 標準フォト / 自作 WIC
主作業ブランチ: fix/photos-118-selfwic-incremental

## この指示は次回以降の作業でも必ず引き継ぐ
技術調査を Microsoft Learn のみに偏らせない。必要性のある調査は GitHub にある複数の独立した実装、libheif、GPAC/MP4Box、FFmpeg、ExifTool、Exiv2、libvips、Nokia HEIF、ImageMagick、digiKam/KDE、PhotoStructure およびその他の関連技術資料・Issue・変更履歴・実コードを横断して確認する。

深い技術調査では、原則10件以上の「独立した有用な出典・プロジェクト」を比較する。検索結果の数を埋めるために類似ページ、転載、無関係な情報を水増ししない。問題が狭い場合は最も直接的な一次ソースを優先し、調査そのものを目的化しない。

各出典について「(1)該当する実装・事実、(2)現在の不具合への関係、(3)既存コードで実装済みか、(4)採用する変更とテスト、または採用しない理由」を記録する。基準コミットと現行ブランチの HEAD、CI、既存テストを先に確認し、すでに実装済みの機能を再開発しない。研究版を一括マージしない。

## 固定された優先順位
1. 旧1.1.18互換の標準フォト動作（HEVC警告なし、回転・編集UI有効）を壊さない
2. 標準フォトでの回転済み画像の保存経路を正常化し、Exif/ICC/10・12bit、回転方向と寸法を可能な限り正確に守る
3. 原本・既存ファイルの非上書き、人工HEICだけを用いるCI、Explorer停止を防ぐ導入・復旧ガードを守る

「CIから WIC APIを叩いた成功」と「Windows 10 標準フォトの実機で成功」は別の証拠である。WICラスタ経由の出自不明画像を元HEVCと決めつけて無圧縮回転しない。実機の復元確認がないままMSI導入を促さない。個人写真・IMG_4074・HEIC Managerには触れない。

## 2026-10-25 横断調査の参照先（背景にすぎず、今後の調査先を制限しない）
- Nokia HEIF technical: https://nokiatech.github.io/heif/technical.html （irot/ispe/pixi/colr/メタアイテムの区別）
- GPAC/MP4Box: https://wiki.gpac.io/Howtos/heif/heif-import/ （HEIFアイテムのプロパティ）
- libheif: https://github.com/strukturag/libheif （HEIFデコード・メタデータAPI）
- libvips heifload.c: https://github.com/libvips/libvips/blob/master/libvips/foreign/heifload.c （Exif先頭4Bオフセット処理、回転の二重適用防止）
- libvips heifsave.c: https://github.com/libvips/libvips/blob/master/libvips/foreign/heifsave.c （Exif/XMPを画像ハンドルに関連付けて再保存）
- ImageMagick: https://github.com/ImageMagick/ImageMagick/issues/1232 （HEIC回転情報とExif向きの不一致）
- Exiv2: https://exiv2.org/manpage.html （BMFF/HEIFの読み書き対応範囲に注意）
- ExifTool: https://exiftool.org/ （保存前後のExif/XMP比較）
- FFmpeg: https://ffmpeg.org/ （HEIF/HEVCビットストリーム解析）
- KDE digiKam: https://bugs.kde.org/show_bug.cgi?id=498076 （ICC消失の実例）
- PhotoStructure: https://forum.photostructure.com/t/incorrectly-rotated-heic-images/34 （二重回転バグの事例）
- WIC API: https://learn.microsoft.com/en-us/windows/desktop/wic/-wic-imp-iwicbitmapframeencode （APIの契約確認）

## 実装済み台帳への追記
- HEAD 8074ecc715397d6204ef3f50fab85ec9a25013f5: 自作WIC FrameDecodeのICC公開、人工ラスタ90度回転でICC 588B保持、CI #40120058119 SUCCESS（標準フォト実機では未確認）
- 後続修正: HeicMetadataBlockReader の参照カウント初期化と短いExifアイテム拒否、Exif query readerの寿命を人工HEICで検証。CIの合否は該当 HEAD の実結果によって記録すること。

最終目標は研究ノートの充実ではなく、再現できるユーザー不具合の解消と安全な動作である。
