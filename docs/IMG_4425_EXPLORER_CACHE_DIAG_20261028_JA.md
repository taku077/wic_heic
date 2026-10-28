# IMG_4425: Explorerサムネイルキャッシュ／再抽出 A/B診断

対象：Windows 10、Windows PowerShell 5.1。新しいMSIやWICの再インストールは不要。Windows 10実機でのPhotos/Explorer操作については未検証。GitHub ActionsはWindows Server 2022で人工PNGによるShell API動作を確認した段階。

## 目的と根拠

同一IMG_4425.HEICが正常に回転保存でき、Explorerのサムネイルが自動更新されなかったが、フォルダーを再読み込み（F5）すると正しい向きになった。そこで以下を区別する。

1. Explorer画面表示・変更通知の更新遅延。
2. Windows Shellのサムネイルキャッシュが古い。
3. WICによるサムネイル抽出そのものが一時的に失敗。
4. Photosの回転保存そのものが失敗（上記3項目とは必ずしも同一原因ではない）。

MicrosoftのIThumbnailCacheには、キャッシュ済みだけを取得するWTS_INCACHEONLY=0x01と、キャッシュを無視して再抽出するWTS_FORCEEXTRACTION=0x04がある。後者はキャッシュを書き換え得るため、**明示的な -ForceReextractAfterSnapshot 指定時に限り実行**する。WTS_EXTRACTDONOTCACHE=0x20はキャッシュへ新規登録しない指定であって、既存キャッシュを無視する保証がない。実際に人工PNGのCIでWTS_CACHED=0x02が返されたため、このフラグを「新鮮な画像」の判定には使用しない。

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
4. **既定ではCACHE_ONLYの読み取り専用スナップショットだけ**を記録し、再抽出やキャッシュ上書きはしない。画像名・パス・EXIF・画像データは出力しない。
5. 実際に新しく抽出した画像と比較する必要がある場合に限り、先のコマンドに -ForceReextractAfterSnapshot を追加する。これは**Windowsのサムネイルキャッシュを更新し得る操作**であり、先にExplorerの表示状態を記録したうえで実施する。画像ファイル自体は変更しない。
6. 診断の後にF5を押し、表示が正しい向きになるかを確認する。

## 結果の読み取り

- 既定のCOMPARE=NOT_PERFORMED_CACHE_SNAPSHOT_ONLY：キャッシュ内の状態だけを取得した。新規抽出の画像と一致したとは判定できない。
- -ForceReextractAfterSnapshot指定時、キャッシュ画像と新規抽出画像のハッシュ・寸法が異なる：キャッシュ不一致。古いキャッシュが疑われるが、異なる抽出経路・スケーリングの影響もあり得る。
- 同指定時、両方OKで一致してもExplorer画面だけが古い：Explorerの表示・通知・再描画処理の問題が有力。ただし要求サイズなどの差に注意。
- CACHE_ONLYが失敗しFORCE_REEXTRACTがOK：キャッシュ未作成などがあり得る。キャッシュ破損とは断定不可。
- FORCE_REEXTRACTが失敗：Shellの抽出エラー。HRESULTで復号・タイムアウト・プロバイダー側問題を追加検討。Photosの保存失敗とは別。
- INPUT_LENGTH_AND_MTIME_UNCHANGED=False：試験中に元の作業コピーが変化しており比較無効。

## 安全性・限界

画像ファイルの書換え・レジストリの変更・MSIの導入・ExplorerへのSHChangeNotify注入・キャッシュ削除は行わない。**明示的に再抽出を選んだ場合だけShellのサムネイルキャッシュは更新され得る**。既定のスナップショットにおけるOSの付随的なキャッシュ管理までは否定できない。ファイルサイズと更新日時の前後一致を判定するが、ツール単独でHEICの全バイト不変を証明するものではない。出力の画素ハッシュはサムネイル画像由来であり、HEIC元データのSHA-256ではない。

## CI

- #40654170435：人工PNGのキャッシュ未登録時の非キャッシュ抽出に成功。
- #40654405796：人工PNGでキャッシュ有り/非登録抽出を比較したところ両者ともWTS_CACHED=0x02を返した。このため誤ったA/B判定を修正し、確実な新規抽出には明示的なWTS_FORCEEXTRACTION=0x04を使う。
- 上記の成功はWindows 10 Photos/Explorer、またはIMG_4425.HEICの回転保存不具合の解消を意味しない。

## 引継ぎ時の注意

古い仕様書をそのまま次チャットへ渡さず、移行直前にGitHubの対象ブランチHEAD・最新CI・今回の実機結果・未解決課題を必ず照合して更新する。今回の新たな主眼はPhotos保存処理の問題とExplorerサムネイル表示更新の問題の分離。
