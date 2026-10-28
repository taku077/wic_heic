# IMG_4425: Windows 10 フォト実機の WIC 保存経路・正常系ベースライン

検証日: 2026-10-28（日本時間）

## 根拠の範囲

ユーザー提供の生ログ `WIC_Photos_Trace_20261028_215338_218.txt`（227行、数値イベント）は会話で解析済み。個人画像、ファイルパス、EXIF内容、画像バイト列はリポジトリにアップロードしない。以下は匿名化した結果だけである。過去のShell診断では正常時にExplorerのサムネイルが自動更新することを実機確認済み。

## WIC保存結果

| 回 | 現地時刻 | WriteSource入力 | 内部認識 | Commit | 遅延 |
|---|---|---|---|---|---|
| 1 | 21:53:50.000～50.553 | opaque-raster、4032x3024 | PhotosPureRotateLossless transform=1（CW90） | lossless、S_OK | 546ms |
| 2 | 21:53:53.344～53.982 | opaque-raster、3024x4032 | PhotosPureRotateLossless transform=3（CCW90） | lossless、S_OK | 625ms |
| 3 | 21:54:00.385～00.387 | self-wic-original、3024x4032 | WriteSourceOriginalExact、unchanged-original-heif | lossless、S_OK | 0ms（記録値） |

全3回：EncoderCommitEnter / EncoderCommitExit 対応あり。EncoderCommitLossless 3件、EncoderCommitRecompressed 0件、EncoderCommitRejected 0件、EncoderCommitOverlap 0件。

**重要：** 1回目と2回目の `FrameInitializeOptions transform=0,compression=0` はフォトから明示のWIC回転オプションが渡されていないことを示す。代わりに回転済み RGB 全画素の厳密比較から自作WICが `transform=1` / `3` と判定した。3回目は画像の無変更コピーであり、**3回連続90度回転ではない**。したがって、開始/終了HEIC SHA-256が同じでも不自然ではない。

## 途中の非致命的な未対応応答

各回 `EncoderSetThumbnail` 1件、`SetThumbnail` 2件が `0x88982F81 WINCODEC_ERR_UNSUPPORTEDOPERATION` を返している（全9件）。各回 `MetadataWriterUnavailable` が `0x80004001 E_NOTIMPL` を返す（全3件）。**実際の3回では後続のWriteSourceとCommitに成功**。これらをそのまま「保存エラーが発生した」とみなしてはならない。

WIC公式:
- https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframeencode-setthumbnail
- https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapencoder
- https://learn.microsoft.com/en-us/windows/win32/wic/-wic-codec-error-codes
- https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapencoder-commit

Windows WIC実装ガイドはエンコーダーのコンテナレベルSetThumbnail非対応に `WINCODEC_ERR_CODECNOTHUMBNAIL` を勧めているが、現コードは `WINCODEC_ERR_UNSUPPORTEDOPERATION` を返している。**この違いは今後の互換性試験候補だが、今回の3回の成功を説明できない故障原因として断定しない**。

## 依然として未確定

- 過去の「サムネイル消失 → 保存エラー」は正常系の今回のWICログでは再現せず、根本原因は未確定。
- `EncoderCommit S_OK` はアプリ側の最終ファイル置換・キャッシュ反映を単独では証明しない。
- metadata-only lossless変換は設計上オリジナルのHEIF graphを保持するが、実際の出力の全メタデータ、ICC、Exif、gain mapをこの数値ログだけで直接比較したものではない。
- 個々のPIDのプロセス名や、インストール済みMSIのコミットIDまではログで認証できない。
- `PhotosPureRotationBridge::TryRotation` の失敗経路では現在「候補なし」「TTL/eviction」「全画素不一致」「ICC不一致」「曖昧一致」「コンテナ拒否」等の**明細が数値トレースに出ない**。次に失敗再現を捕捉するときに有用な診断不足。

## 次に行うこと（新MSIを急いで配布しない）

1. この3回を正常系ベースラインに固定する。失敗していない同じ画像を無目的に回転させ続けない。
2. 失敗時に `EncoderCommitRejected` / `EncoderCommitExit` が出るか、または Commitに達する前に `SetThumbnail` / metadata query の未対応応答で停止するかを分類する。
3. 診断が必要になれば、原本のHEIFバイトやファイル名を記録しない条件で `TryRotation` の失敗理由コードを限定的に追加し、人工HEICで CI検証した上で実機投入を判断する。
4. SetThumbnailのコンテナレベル戻り値変更は別ブランチで対照試験し、既存成功経路を壊さないことを確認する。
5. 本番WICの変更・MSI配布は、原因候補を検証して実機試験の価値が明確になった段階に限定する。
