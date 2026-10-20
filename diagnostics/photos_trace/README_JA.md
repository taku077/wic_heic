# Windows 10 フォト / WIC 呼び出し診断（開発用、未配布）

**注意：これは標準フォト互換の完成版ではありません。** GitHub の \`diagnostic/photos-wic-calltrace\` ブランチ用の安全確認キットです。Windows 10標準フォト実機では、まだ実行・検証していません。

## 目的
HEICをフォトで90度回転する際に実際に使われる経路を、根拠を持って特定します。

1. このWICデコーダーがフォトから呼ばれているか。
2. このWICエンコーダーが呼ばれているか。
3. \`BitmapTransform\` の回転指定が渡されるか。
4. \`HeifCompressionMethod\` で無再圧縮が指定されるか。
5. 入力が元のHEICビットストリーム付きフレームか、加工済みピクセルのみか。
6. 再圧縮されるか、無再圧縮で保存されるか、拒否されるか。
7. メタデータ書き込みが要求されたか（\`SetColorContexts\`・\`SetThumbnail\`・\`GetMetadataQueryWriter\`）。

## 重要な安全条件
- **IMG_4074および実際の写真で検証しない。** 公開済みの小さな人工HEICの一時コピーだけ使用する。
- 診断用WIC MSIの導入は、別途、実機バックアップ・復元手順とリリース判定が揃うまで実施しない。
- 診断スクリプトはWICのインストール・アンインストール、元画像の移動・改名・上書き・EXIF変更を一切しない。
- \`PhotosTrace_Start.ps1\` は **HKCU\Software\WicHeic** の \`TraceFile\` と \`TraceUntilFileTime\` だけを設定する。既存のトレース設定があるときは拒否する。
- ログはスクリプトと同じローカルフォルダ内に作成する。設定を残しても20分後に自動的に記録が停止する。
- \`PhotosTrace_Stop.ps1\` は自分で作成した設定だけ解除し、ログを削除しない。実行が終われば必ず停止。
- 出力は固定イベント名・数字・PIDのみ。ファイル名、フォルダ名、画像本体、撮影日時、位置情報、EXIF内容、コマンド引数は記録しない。
- ログの上限は2 MiB。ログは既存ファイルへの追記のみ、画像への書き込みはしない。

## 動作の判定
\`PhotosTrace_Analyze.ps1\` は同じフォルダのテキストログを分析し、イベント別件数とPID別件数を別のテキストに保存します。

- \`DecoderOpen\` があるが \`EncoderInitialize\` がない：対象プロセスではこのWICエンコーダーの実行を確認できない（フォトが使ったものの特定が必要）。
- \`FrameInitializeOptions transform=1\`：時計回り90度指定がフォト側から届いた可能性がある。ただし PID と操作時刻の照合が必要。
- \`WriteSourceLossless\` と \`EncoderCommitLossless\`：圧縮データをコピーする経路を通った。
- \`WriteSourceRaster\` と \`EncoderCommitRecompressed\`：画素を再エンコードした。
- \`WriteSourceRejected source=raster-only\`：圧縮元がないため、再圧縮禁止の条件で拒否した。
- \`WriteSourceRejected source=unsupported-heif\`：補助画像／HEIF構造などに未対応。

**ログが空でも、フォトがWICを利用していないと即断しない。** 既存のWIC v2には診断機能がない。フォトが対象DLLをロードしたことと、診断が有効であることを機械的に確認する必要がある。

## 開発者向け自動試験
Windows GitHub Actionsでは環境変数 \`WIC_HEIC_TRACE_FILE\` を明示的に設定し、公開の人工HEICだけをWICに渡す。ログの必須イベントおよびファイルパス・画像メタデータの不記録を判定する。開始／停止スクリプトのレジストリ変更と解除もチェックする。

## 追加の安全対策と復旧準備（2026-10-20）
- `PhotosTrace_Start.ps1` は既存の `TraceFile` **または** `TraceUntilFileTime` が1つでもあれば開始を拒否する。期限値とログパスをセッション記録に保持し、他者の設定を変更した場合はStopも削除を拒否する。
- ログは排他制御して追記し、正確に **2 MiB以下**とする。書き込み競合時にはイベントを記録できない場合があり、ログ欠落を「呼び出しがなかった証拠」とは扱わない。
- `WIC_Preflight_ReadOnly.ps1` はレジストリを一切変更せず、32/64bitのWIC CLSID・DLLパス・DLL SHA-256・WicHeic MSI情報・Windowsビルド・フォトのバージョンをこのフォルダのJSONへ保存する（DLLパスにはユーザー名等の環境情報が含まれ得るため、外部共有する前に確認する）。
- `WIC_Preflight_Compare.ps1 -Before <導入前JSON> -After <復元後JSON>` はレジストリの登録先とDLL SHA-256、WicHeic MSI情報を比較する。**この比較はWindows標準フォトの正常動作や、元のMSIによる復元の代わりにはならない。**
- GitHub Actionsの隔離Windows環境では、診断開始/停止・設定の非破壊性・2MiB上限・MSI導入削除前後のスナップショット比較が成功している。
- このスナップショットツールは**読取専用**だが、MSI導入・アンインストールを行わない。復元用の旧MSIと手順が実機と一致すると確認できるまでは、ユーザーPCへの実験MSI導入は禁止。

## 人工HEIC作業コピーの保全
- PhotosSynthetic_Prepare.ps1 は固定された公開人工HEICのGit Blob SHA-1と内容を照合し、一意なPhotosSynthetic_GUIDフォルダにread-only保全コピーと作業コピーを作る。既存の写真を入力として受け取らない。ファイルを上書き・削除しない。
- Windowsフォトで開くのは work/synthetic-working.heic のみ。保全コピーや実際の写真を開かない。
- PhotosSynthetic_Verify.ps1 -SessionDirectory (準備時のフォルダ) は参照用ファイルのSHA-256と作業コピーの変更状況を読み取り専用で確認する。変化した事実と編集成功は別であり、画質・EXIF・HDRの保全を証明するものではない。
- シンボリックリンク・ジャンクションを拒否。作ったファイルを自動削除しない。これらは本番Windows 10フォトで未検証。
- GitHub Actionsの人工ファイルの検査が成功しても、元のWICへ復元可能なことを確認するまでは実験MSIを導入しない。

- GitHub Actionsでは環境変数を使わずHKCU診断設定を有効化し、人工HEICのWIC読み書きが実際にログへ記録されること、期限切れ後に追記されないことも検証する。これは通常プロセスのCI試験であり、Windows 10フォトのパッケージ権限やAppContainer内でログが書けることの証明ではない。

## 未完了
- Windows 10フォトの実際の呼び出し確認。
- フォトからの無再圧縮保存に必要な機能の確認と実装。
- 埋め込みサムネイル・HDRゲインマップを含むHEICの一貫した回転。
- 縮小・再エンコード時のEXIF、ICC、XMP等の保全。
- 元画像の意図しない上書きを防ぐアプリケーション側の保存手順。
