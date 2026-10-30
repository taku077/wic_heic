# WicHeic 1.1.31 — Shellサムネイル登録・復元のリリースゲート（2026-10-30）

**位置付け**：試験環境でのみ適用する実装方針。**製品MSIへのShellEx登録実装済みを意味しない**。現在の `Installer/Package.wxs` は v1.1.30 であり、試験用 `HeicShellThumbnailLab.dll` はMSI非同梱。Windows10実機への導入・レジストリ改変は行わない。

## 試験で確認したこと

- 分離実行を想定した `IThumbnailProvider + IInitializeWithStream` のCI専用COMクラスは、Shell `IThumbnailCache` による実呼出しが可能。埋込 `thmb` のある人工8bit HEICでは主画像復号を回避（[#40931425282](https://github.com/taku077/wic_heic/actions/runs/40931425282)）。
- Shell CIの一時登録は元のキーが存在すれば上書きを拒否。HKCUのみで動作しない場合に限り、使い捨てrunnerの未知でない新規CLSIDにHKLM登録を追加し、事後に所有者・値を比較して削除。登録復元をCIで確認。
- `diagnostics/shell_registry_ci/ShellRegistrationPolicyTrial.ps1` は**実ShellExではない一時的なHKCU疑似レジストリ**を使い、初回導入、既知Microsoftハンドラーの復元、アップグレード、第三者ハンドラー維持、インストール後の変更維持、ロールバック、孤立登録の拒否を検証（[#40929059759](https://github.com/taku077/wic_heic/actions/runs/40929059759)）。
- 2026-10-30の画素比較CIでは、実在するExif Orientation=6 + HEIF irot=3の2素材と、ICC・埋込サムネイルなしの人工素材について、従来Shell経路との256px表示寸法・64×64 RGBサンプル比較が0～0.40 MAE（[#41006933392](https://github.com/taku077/wic_heic/actions/runs/41006933392)）。この結果を寸法完全一致・RGB MAE≦1.0の厳格CIに昇格した後続コミットは別途結果を確認する。

## 実際の1.1.31 MSIで必要な設計（未実装）

1. **初期状態取得をトランザクションの前に行う**。対象は少なくとも `.heic\ShellEx\{E357...}` および `SystemFileAssociations\.heic\ShellEx\{E357...}`、COM CLSIDの双方。HKCU/HKLMと32/64bitビューを混同しない。存在/非存在、REG_SZ型、値、所有者、ACLを記録するが第三者を無条件変更しない。
2. **不明な既存ハンドラーが一つでも優先される場合はShell最適化の登録をスキップ**。製品WICの既存回転・保存機能は維持する。自分が安全に所有できるパスだけを変更する。
3. **スナップショットをMSIのアップグレード・修復・アンインストールまで永続化**。連続アップグレード時に元の既定ハンドラー情報を新しい自分の値で上書きしない。
4. **MSI deferred/rollback/commitの整合性**。失敗時に登録した自分の値だけを比較復元する。未実行のカスタムアクションを実行済みと扱わない。所有権が変わった場合は他者変更を尊重し、復元を強行しない。
5. **実MSIのクリーン導入→修復→アップグレード→アンインストール→意図的途中失敗**を、使い捨てWindows runnerで独立検証。インストール前後のレジストリスナップショット、Explorer/Photos関連付け、旧MSI復旧、第三者ハンドラー維持を確認するまで実機に出さない。
6. COMサーバーが `IInitializeWithStream` を実装し、`DisableProcessIsolation=1` を設定しないこと。PhotosのWICデコーダー自体のCOM登録・保存経路を置換しない。

## 性能・画質のリリース条件

- **安全性**：Exif/irot/ICC/埋込なしの実サンプル比較と、grid/10/12bit/alpha/HDR・関連画像の安全側フォールバック。型を偽装したメタデータ変異は「検査器の拒否試験」であり**実在する正常な画像の回帰試験の代わりにしない**。
- **速度**：同じCIで8ファイル×2経路の中央値は103→12ms（約88%短縮）。対象は**埋込HEVC thumbnailのある単純な人工HEIC**で、実iPhoneの全画像に一般化できない。Windows10での埋込thumbnail保有率とフォルダ全体の体感を確認して初めて配布判断する。
- **操作**：ユーザーの未加工原本、IMG_4074を使用しない。IMG_4425も必要ならコピーのみ。新しいWPRや人手の大量反復は求めない。Windows10 22H2実機で検証可能になっても、ロールバック可能なMSIを有意な区切りで一度だけ提示。
- **計測**：初回画像・大量画像バースト・CPU-ms・Photos操作の応答・画像向き/ICC/HDR/alphaを別々に判定する。30%の理想達成を追って安全性と時間を犠牲にしない。

## 独立資料

- Microsoft IThumbnailProvider: https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nn-thumbcache-ithumbnailprovider
- Building Thumbnail Handlers: https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers
- Windows Installer rollback: https://learn.microsoft.com/en-us/windows/win32/msi/rollback-installation
- libheif: https://github.com/strukturag/libheif
- libheif grid encoding: https://github.com/strukturag/libheif/wiki/Reading-and-Writing-Tiled-Images
- darktable HEIF orientation example: https://fossies.org/linux/darktable/src/imageio/imageio_heif.c
- KDE kimageformats HEIF rotation+mirror cases: https://invent.kde.org/frameworks/kimageformats
