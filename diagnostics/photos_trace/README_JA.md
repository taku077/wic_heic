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

## 未完了
- Windows 10フォトの実際の呼び出し確認。
- フォトからの無再圧縮保存に必要な機能の確認と実装。
- 埋め込みサムネイル・HDRゲインマップを含むHEICの一貫した回転。
- 縮小・再エンコード時のEXIF、ICC、XMP等の保全。
- 元画像の意図しない上書きを防ぐアプリケーション側の保存手順。
