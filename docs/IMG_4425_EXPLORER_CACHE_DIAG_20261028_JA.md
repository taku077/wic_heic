# IMG_4425: Explorerサムネイルキャッシュ／再抽出 A/B診断

対象：Windows 10、Windows PowerShell 5.1。新しいMSIやWICの再インストールは不要。Windows 10実機でのPhotos/Explorer操作については未検証。GitHub ActionsはWindows Server 2022で人工PNGによるShell API動作を確認した段階。

## 目的と根拠

同一IMG_4425.HEICが正常に回転保存でき、Explorerのサムネイルが自動更新されなかったが、フォルダーを再読み込み（F5）すると正しい向きになった。そこで以下を区別する。

1. Explorer画面表示・変更通知の更新遅延。
2. Windows Shellのサムネイルキャッシュが古い。
3. WICによるサムネイル抽出そのものが一時的に失敗。
4. Photosの回転保存そのものが失敗（上記3項目とは必ずしも同一原因ではない）。

MicrosoftのIThumbnailCacheにはキャッシュ済みのみ取得するWTS_INCACHEONLY=0x01と、抽出するがキャッシュへ追加しないWTS_EXTRACTDONOTCACHE=0x20がある。後者と前者のサムネイル画素を比較する。キャッシュを強制書き換えするWTS_FORCEEXTRACTION=0x04は使わない。

資料：
- https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nf-thumbcache-ithumbnailcache-getthumbnail
- https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/ne-thumbcache-wts_flags
- https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shchangenotify

## 実機での操作（コピーのみ使用）

1. IMG_4425.HEICの原本は変更せず、作業コピーだけをPhotosで開く。現在のWICやMSIは変更しない。
2. 回転保存後、Explorerのサムネイルが更新されない場合、F5の前に診断する。
3. diagnostics/photos_trace/HEIC_ThumbnailCache_AB_ReadOnly.ps1 をC:などローカルドライブへ置き、Windows PowerShell 5.1で次のいずれかを実行。管理者権限不要。実行ポリシー変更はそのプロセス内に限定する。
   - 対話選択：powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\HEIC_ThumbnailCache_AB_ReadOnly.ps1
   - コピーのパス指定：powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\HEIC_ThumbnailCache_AB_ReadOnly.ps1 -ImagePath "C:\Test\IMG_4425_copy.HEIC"
4. CACHE_ONLY と EXTRACT_NO_CACHE のHRESULT、ビットマップ寸法、画素ハッシュの先頭20桁、所要時間を記録する。画像名・パス・EXIF・画像データは出力しない。
5. 診断の後にF5を押し、表示が正しい向きになるかを確認する。

## 結果の読み取り

- 両方OKで画素ハッシュ・寸法が異なる：キャッシュ済み画像と再抽出画像の不一致。古いキャッシュが疑われるが、抽出経路やスケーリングの違いでもあり得る。
- 両方OKで一致してもExplorerの画面だけが古い：Explorerの表示・通知・再描画処理の問題が有力。Explorerと本ツールのサムネイル要求サイズやプロバイダーの差に注意。
- CACHE_ONLYが失敗しEXTRACT_NO_CACHEがOK：指定サイズでキャッシュ未作成の可能性。キャッシュ破損とは断定不可。
- EXTRACT_NO_CACHEが失敗：Shellの抽出エラー。HRESULTで復号・タイムアウト・プロバイダー側問題を追加検討。Photosの保存失敗と混同しない。
- INPUT_LENGTH_AND_MTIME_UNCHANGED=False：試験中に元の作業コピーが変化しており比較無効。

## 安全性・限界

画像ファイルの書換え・レジストリの変更・MSIの導入・ExplorerへのSHChangeNotify注入・キャッシュ削除は行わない。ただしShell内部の付随的なキャッシュ管理までは否定できない。ファイルサイズと更新日時の前後一致を判定するが、ツール単独でHEICの全バイト不変を証明するものではない。出力の画素ハッシュはサムネイル画像由来であり、HEIC元データのSHA-256ではない。

## CI

- #40654170435：2026-10-28、Windows Server 2022 + PowerShell 5.1で人工PNGのキャッシュ非登録抽出に成功。人工PNGの全バイトSHA256の前後一致をCIで確認。
- 上記の成功はWindows 10 Photos/Explorer、またはIMG_4425.HEICの回転保存不具合の解消を意味しない。

## 引継ぎ時の注意

古い仕様書をそのまま次チャットへ渡さず、移行直前にGitHubの対象ブランチHEAD・最新CI・今回の実機結果・未解決課題を必ず照合して更新する。今回の新たな主眼はPhotos保存処理の問題とExplorerサムネイル表示更新の問題の分離。
