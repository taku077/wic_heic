# WicHeic 1.1.26 x64 Windows 10 Photos — MSI候補（2026-10-26）

**提出期限:** 2026-10-27（火）20:00 JST。期限変更なし。

**注意:** この文書を作成した時点では、1.1.26 Windows 10標準フォト実機は**未検証**。
Windows CIでのインストール／アンインストール成功はWindows 10実機のロールバック保証ではない。
1.1.25はユーザー実機でIMG_3916を含む回転保存成功確認済み。表示伸長、見切れ、保存遅延は未解決。

## 1. 1.1.25からの差分

- `wic_heic/PhotosPureRotationBridge.h`：Photos型opaque-rasterの全画素完全一致について、CW90/180/270に加え0°（四回転後の向き）を厳密判定。0°では元HEIFをバイト同一で使用し、編集した1画素でも再圧縮へ戻す。全角度・複数画像で判定が曖昧な場合にはロスレスを拒否。
- 同ファイル：Photosの隣接HEIC先読みを想定し、一つのグローバル候補で恒久的にambiguousとなる旧仕様を、短時間最大4件・合計64 MiBの候補と一意一致判定へ変更。誤紐付け回避のため保存中に候補集合が変化すれば安全側に拒否。
- `wic_heic/PhotosWicTrace.h`／`HeicBitmapEncoder.cpp`：オプトインWICログへ保存操作番号、UTC時刻、スレッドID、WIC内部所要時間、同時Commitの存在を追加。ログ上限2MiB／20分自動停止／写真やパス非記録を維持。**WIC Commit終端はPhotosのファイル置換やサムネイル更新完了を意味しない**。
- `diagnostics/photos_trace/PhotosTrace_Analyze.ps1`：Windows PowerShell 5.1で旧ログとの互換を保ち、保存所要時間と同時処理数を集計。
- `wictest/WICPhotosOpaqueRotationProbe.h`：0度、1画素編集、別HEIC先読みの人工テストを追加。回転メタデータ、EXIF3限定の正規化、寸法/stride検査、元画像保護などは旧実装をそのまま使用し再実装しない。

## 2. テスト判定とバイナリー出荷

- **リリース候補の発行条件:** 該当 `release(candidate):` push の Windows Actions `compile-x64` が成功し、x64 DLL・既存人工HEIC保存試験・MSI導入／削除／COM復元比較がすべて成功した場合だけ自動でMSIとSHA256をアップロードする。
- MSI ProductVersion: **1.1.26**。WiX既存MajorUpgradeの構造を維持する。利用者PCでの旧1.1.25→新版導入・ロールバックは未保証。
- アーティファクト名：`WicHeic-1.1.26-x64-Win10-Photos-UNVERIFIED-RC`
- アーティファクト収録：`WicHeic-HEIC-x64.msi`、`MSI_SHA256_AND_PROVENANCE.txt`。SHA256値はGitHub Actionsのビルドから記録し、文書中に値を推測で書かない。
- 1.1.26は**実験候補**であり、Windows 10実機で表示バグ解消済み／Exif・XMPを全保存したとは表記しない。

## 3. 現時点の既知未解決

- Photosで素早く・遅く回転を繰り返した場合の引き伸ばし／細長い描画枠／見切れ。ユーザー観察では回転操作とサムネイル更新にタイムラグがある。保存処理／後続ファイル置換／サムネイルの順番関係は未立証。
- 実HEICでの再圧縮による容量増。全画素一致以外の編集は意図的に再圧縮へフォールバック（品質92の既存設定を維持）。再圧縮編集でEXIF/XMP等が欠ける問題も残る。
- HDR/埋め込みサムネイル/主画像がHEIFプロパティを共有しないケースなど、無再圧縮安全ゲート外の形式。
- `.tmp`作成・ファイル置換の主体PIDの未特定。ファイル監視イベントは重複する可能性がある。
- 今回の診断ログに時間を追加しただけではPhotos表示バグの原因を確定できない。

## 4. Windows 10 実機導入前の必須安全ゲート

- **ユーザーが明示承認するまで1.1.26をPCへインストールしない。** 正常保存できる1.1.25のMSI保管とWIC DLLハッシュ・WIC CLSID等の現状読取専用スナップショットを取得する。
- 旧版復旧用の1.1.25 MSIと手順を実際に用意できない場合は導入を勧めない。事前診断と復旧後比較は `diagnostics/photos_trace/WIC_Preflight_ReadOnly.ps1` および `WIC_Preflight_Compare.ps1` を使う。
- テストは新しい空フォルダー、公開の人工HEICとその参照コピーのみに限定。ユーザー実写真原本を上書き、削除、自動変換しない。IMG_4074不使用。
- 自作HEIC Manager、WindowsのHEIF/HEVC拡張購入、Explorerサムネイルキャッシュ／関連付けを変更しない。
- ログを利用する場合は、対象MSIのCIと復元確認後に `PhotosTrace_Start.ps1` / `PhotosTrace_Stop.ps1` を使用。画面操作、Encoder Commit、実ファイル置換、サムネイル表示の各時点を別に評価する。

## 5. 外部参照

- Microsoft WIC `IWICBitmapEncoder::Commit`: https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapencoder-commit
- Microsoft WIC codec Commit実装: https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapencoder
- libheif tiled images: https://github.com/strukturag/libheif/wiki/Reading-and-Writing-Tiled-Images
- pillow-heif orientation: https://pillow-heif.readthedocs.io/en/stable/workaround-orientation.html
- libavif EXIF+irot handling: https://aomedia.googlesource.com/libavif/+/999fcf5481c77e78e9a63a4cbd26b30a5c02a94c/apps/shared/avifjpeg.c
