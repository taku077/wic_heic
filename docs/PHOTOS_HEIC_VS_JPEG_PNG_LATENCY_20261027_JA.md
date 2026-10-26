# Windows 10 Photos HEIC表示負荷・JPEG/PNG比較の検証仕様（2026-10-27）

## 出発点（実機症状）

- ユーザー環境：Windows 10 Pro 22H2、Core i5第7世代、専用GPUなし。現行WicHeic MSI 1.1.27を維持。
- HEICはJPEG/PNGと比較して、表示・回転・拡大縮小・保存の全体が明らかに重いと感じるとの実機報告。
- 同一人工HEICの録画では「縮小→高速回転→最初の表示崩れ→僅かに拡大→横長表示への固着」、画像だけ6–7秒消えてPhotos UIと寸法表示が残る事例があった。
- 観察したHEIC状態変更はirot/Exifだけで、圧縮HEVC画素や追加画像は全バイト保全。画面描画とファイル保存の時刻は同じとは限らない。
- **仮説：** HEVC/libheifの初回フルサイズ復号＋新規WICフレーム発行、Windows Photosの非同期描画・拡大縮小・回転要求が競合すると、画面更新順序を取り違える可能性。現時点では**時間的因果は未証明**。

## コード照合（重複開発禁止）

1. `wic_heic/HeicBitmapFrameDecode.cpp`の`DecodeImage()`は`m_Decoded`原子フラグ＋ミューテックスにより、同じCOMフレーム内では**初回に一度だけ**libheif復号。2回目以降の`CopyPixels`はRGBプレーンからmemcpy。既存の一度だけ復号キャッシュを重複実装しない。
2. `GetSize`はHEIFハンドルの表示寸法を返すだけでフルRGB復号を強制しない。JPEG/PNGとの比較も「デコーダー新規取得」「初回CopyPixels」「warm CopyPixels」「縮小」「回転」を分解する。
3. `CHeicBitmapDecoder::GetPreview()`はWINCODEC_ERR_UNSUPPORTEDOPERATION。画像内に適切なサムネイルがなければ`GetThumbnail()`はCODECNOTHUMBNAIL。WICの`IWICBitmapSourceTransform`は現行フレームのQueryInterfaceで公開されていない。このためフルサイズ画素を必要とする経路があり、Photosの小倍率表示でも高速部分復号ができると決めつけない。
4. **注意:** Microsoft WIC資料ではプレビュー／サムネイル不足時に大画像全体を復号して縮小するためコストが高いとされるが、PhotosがどのAPIをどの順で使用するかは未確認。サムネイルのない公開人工画像で観察した遅延を、すべてのiPhoneのHEICに一般化しない。

## 2026-10-27 GitHub実装

- `wic_heic/HeicBitmapFrameDecode.cpp`に`DecoderFullDecode`イベントを追加。実際の`decode_image()`呼出しを`GetTickCount64`で計測し、既存の**明示的オプトイン診断**に`elapsed_ms`、WIC-visible寸法、固定ラベル`libheif-cold`だけを記録。**ファイル名、パス、写真画素、EXIF、ユーザー情報なし。** COMフレーム1つに1回だけ、通常はOFF。既存`DecoderGetFrame`／`DecoderFrameGetSize`／`DecoderCopyPixels`と時刻で照合できる。
- `wictest/WICFormatLatencyProbe.h`を追加。同一公開人工HEICの表示RGBを使い、WIC標準エンコーダーで比較用JPEG/PNGをCI専用の**毎回新しいフォルダ**へ書き出し。同一1080×1920画像をHEIC/PNG/JPEGとして、**open＋GetFrame＋GetSize＋24BGR正規化、最初のフル画素CopyPixels、同一フレームの後続3回の平均CopyPixels、Fantで1/4縮小、90°回転**に分けてQueryPerformanceCounterで時間測定。各失敗はHRESULTで落とす。計測値だけで合否を決めない。
- `wictest/wictest.cpp`へ別モード`--perf-compare`を登録、`.github/workflows/repair-x64-build.yml`へ比較試験とプライバシー限定`DecoderFullDecode`発行の必須検査を追加。既存の50回回転、並列、Exif/ICC保持、縮小、MSI隔離導入・完全復元試験は変更しない。
- この比較対象は**人工グラデーション画像**であり、JPEGは非可逆圧縮。画像内容、同時バックグラウンド負荷、ディスクキャッシュ、CPU等に依存するので、CI runnerの数値をユーザーのWin10 PC実速度と断定しない。
- **実機への導入について：** 新しい1.1.27 MSIは配布しない。実機導入済み1.1.27のコードは変更しない。テストがすべて通ってから、必要なタイミングで進歩のあるMSIとして提供する。ローカル診断ツールv0.5.1は現状のまま使用可能。

## 優先する判断条件

- HEICの初回CopyPixelsだけが突出して遅い → 復号負荷、libde265スレッド制御、並列競合、サムネイル／プレビュー利用を調査。ただし既存高速化機能は重複開発禁止。
- WIC GetFrame/Initializeも突出 → HEIFメタ解析やストリームI/O、別デコーダー反復生成を調査。
- 初回は重いが同一COMフレームwarmは軽い → WICキャッシュは有効。Photosがフレームを毎回作り直しているかをログで確認。
- warm CopyPixels、Fant縮小、FlipRotatorが突出 → RGB転送、再サンプリング、rotation/viewport表示更新の処理を検査。
- WICの各段階が短いのにPhotosだけ数秒真っ黒 → Photosの非同期表示、UIレイアウト、画像サーフェス更新等を優先。ただしCIと実機の環境差を考慮。

## 参照文献（縦断）

- Microsoft: WIC BitmapSourceTransform https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapsourcetransform
- Microsoft: Bitmap scaler https://learn.microsoft.com/en-us/windows/win32/wic/-wic-bitmapsources-howto-scale
- Microsoft: Preview / Thumbnail no full-image decode https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapframeencode
- Microsoft: image decode performance https://learn.microsoft.com/en-us/windows/apps/develop/performance/optimize-animations-and-media
- Microsoft: WIC bitmap cache https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicimagingfactory-createbitmapfromsource
- libheif project & codecs https://github.com/strukturag/libheif
- libheif community performance case https://github.com/strukturag/libheif/issues/1787
