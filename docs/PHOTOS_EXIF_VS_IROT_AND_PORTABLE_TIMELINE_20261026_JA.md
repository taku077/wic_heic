# 2026-10-26 実機連続回転：Exif Orientation と HEIF irot、独立診断の次の安全ゲート

**対象:** Windows 10 標準フォト向け `taku077/wic_heic`。現ユーザー実機版 1.1.27 は維持。写真原本／IMG_4074／HEIC Manager は不使用・非干渉。

## 1. 前回までの根拠

- 公開人工2フレームHEICは WicHeic 1.1.26 で保存不可、1.1.27ではWindows 10標準フォト実機で回転保存操作が成功。
- 実機で保存された公開人工HEICは10,597バイトのまま**Exif Orientation 6→1だけ変更**。HEIF `irot=3`とHEVC圧縮画像は変更なし。このため「フォトで保存できた」と「90度の画素回転／表示方向変更が確定した」を分ける。
- 50回逐次保存、8件並行×合計50保存、表示用の縮小・画素縮小×回転の組合せ6条件、同一の人工作業ファイルへの50回置換、全世代バックアップがCI成功：#40273356433。
- ただし、Photos GUIにおける連打・非同期保存・サムネイル反映と、CIで順序よく終了する`IWICBitmapEncoder::Commit`の動作は別。Photosのファイル置換の具体的APIも未特定。

## 2. Exifだけ変更された場合は回転完了と同一視しない

`wictest/WICPhotosExifOnlyProbe.h` に、公開人工HEICだけを用いた一種の否定対照テストを追加。

- 元画像（固定10,597バイト、人工HEIFメタ構成）が持つ `irot=3 / Exif Orientation=6` を読取専用で開く。
- メモリ内でExifの既知の1バイトだけ`6→1`へ変え、新しい人工ファイル名として保存。元画像は一切更新・削除しない。
- **同じWICデコーダー**で元と変更後の主画像の全RGB画素、縦横寸法、2フレーム数が一致するかを検証し、Exifのみの正規化を「回転の成功」の判定に使用しない。
- この2つのオフセットは公開人工画像の専用テストとしてのみ固定。任意の実写真・未知HEIFには適用しない。
- Windows CIの試験に組み込み、実機表示との一致は別途調査。すでに取得した実機保存の事実自体は否定しない。

## 3. 独立型 HEIC_SaveTiming_Portable_v0_3

v0.2（同梱したSHA256で固定した人工HEICをWindows共通フォルダーダイアログから新規セッションへコピー）を拡張。配布ZIPはChatGPT成果物から取得する方式とし、GitHub Actions MSIとは別管理。

**基本条件:** Windows PowerShell 5.1、管理者権限なし、レジストリ設定変更なし、MSI・再起動不要。選択したフォルダーの配下で毎回一意な新セッションを作り、その中の`work/synthetic-working.heic` **1ファイルだけ**を定期的に読み取る。画像を他フォルダーで探さない。既存のHEIC、ユーザー写真へのアクセス・上書き・削除なし。元人工画像はSHA-256 `D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95` で固定。

| 出力 | 内容 |
|---|---|
| `HEIC_SaveTiming_Events.csv` | FileSystemWatcher通知（重複・取りこぼしあり） |
| `HEIC_SaveTiming_States.csv` | 人工作業ファイルのハッシュ・容量・操作途中の一時的な欠落／読取不能・`irot`／Exif Orientation |
| `HEIC_SaveTiming_Markers.csv` | 手動R：回転操作後、T：サムネイル変化確認時。観察者の入力時刻であり真のクリック時刻ではない |
| `HEIC_SaveTiming_Result.txt` | 作業コピーと元公開人工HEICのハッシュ、結果件数、既知の測定限界 |

- **約250ms**ごとに人工画像を確認、状態変化があった場合のみ記録。元公開画像のレイアウトがHEIFヘッダー・全バイト（Exifとirotの2バイトを除く）と一致する場合だけ回転フィールド値を表示。サイズや他フィールドが変わる場合は`UNRECOGNIZED_LAYOUT`として空欄を維持。未知形式に推測のオフセットを適用しない。
- FileSystemWatcherがイベントを多重発火したり欠落させたりするため、**ハッシュ変化は確認できてもファイルへの保存処理の終端時刻を断定しない**。
- 写真のサムネイルはプログラムから取得しない。R/Tはオプション。詳細なファイル名やパスを結果CSVには含めない。
- 読取ファイルは12MiB以下に制限し、危険なreparse pointを拒否。新セッションとログは自動削除しない。
- Linux環境でZIP CRC・人工画像SHA256・既知Exif 6→1変化例との一致は検証したが、Windows 10/PowerShell実機の動作は未検証。

## 4. 多方面の参照文献

- [Microsoft WIC回転オプション](https://learn.microsoft.com/en-us/windows/win32/api/wincodec/ne-wincodec-wicbitmaptransformoptions)：Rotate90は時計回り90度。pixel操作とファイル保存を区別する。
- [Microsoft WICソース変換](https://learn.microsoft.com/en-us/windows/desktop/wic/-wic-imp-iwicbitmapsourcetransform)：変換適用順序はscale→crop→rotate。
- [libavif Exif/Irot/Imir対応コード](https://aomedia.googlesource.com/libavif/+/06799082864459d6bb31ae3612dc753035c61ce1/src/exif.c)：Exifとの変換表を持ち、両者の扱いを区別。
- [PhotoPrism HEIF orientation](https://docs.photoprism.app/developer-guide/metadata/orientation/)：HEIFコンテナレベルの`irot`/`imir`とExif Orientationは役割が異なる。
- [Microsoft FileSystemWatcher](https://learn.microsoft.com/ja-jp/dotnet/api/system.io.filesystemwatcher)：多重イベント・欠落・バッファ上限あり。

## 5. 続行の優先順位

1. WIC直接のExifだけ正規化した画像では主画像RGBが変わらないという否定対照をCIで固定する。
2. 1.1.27を実機基準に、独立v0.3で人工HEICのファイルハッシュ／Exif／irotの時系列を取得し、目視サムネイル更新と区別する。
3. ソースコードからPhotos側の実ファイル置換経路を推測だけで断定しない。必要時にはプロセス単位の独立監視を追加し、元ファイルが未保存の状態で次のUI回転が行われた際の相関を確かめる。
4. ユーザーPCで実写真の上書き試験はしない。安全性と既知不具合の区別が確立してから次のMSI候補を発行する。期限2026-10-27 20:00 JST維持。

## 2026-10-26 Windows CIの確定結果

- **Actions [#40274979429](https://github.com/taku077/wic_heic/actions/runs/40274979429) SUCCESS**。WICを直接使った否定対照 `WIC_PHOTOS_EXIF_ONLY_NOT_ACTUAL_90_ROTATION_PASS size=10597 irot=3 exif=6_to_1 same_geometry=1 same_WIC_RGB=1` を複数の試験経路で確認。
- 1.1.27でユーザーがPhotosの「回転保存できた」と報告した公開人工HEICの保存ファイルがExifだけ変更される場合、少なくともWIC直接復号では縦横と主画像画素は**変更前と同じ**。フォトUI上で所望の回転が目視できるかは独立の判定対象。
- 上記CIには`--stress50`（50回逐次）、`--burst50`（8並列/50操作）、`--replace50`（同じ人工作業ファイルを50回入替）、`--scale-matrix`（縮小・回転6条件）を含み、いずれも合格。
- 保存の開始・終端トレース185組を確認。MSIの隔離インストール・アンインストール後のWIC登録状態復元比較も成功。
- 開発ブランチ内の新しいコードを使ったWindows CI成功は、ユーザーが使用中の1.1.27 MSIの内容変更やPhotos GUI実機での問題解消を意味しない。
