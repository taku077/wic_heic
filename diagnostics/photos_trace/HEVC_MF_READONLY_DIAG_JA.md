# Windows 10 フォトの「HEVC拡張が必要」警告・編集/回転不能：優先調査

## 確定したことと未確定
2026-10-22のユーザー実機スクリーンショットでは、`IMG_3915.HEIC` (3024×4032) はプレビューが表示される一方、Windowsフォトが「完全な解像度にはHEVCビデオ拡張が必要」と通知し、編集・回転が使用できないとの報告あり。**画像表示の成功はフォト編集経路の成功を意味しない。**

Microsoft公式: Windows 10のフォトはHEIFとHEVC両方の拡張が必要な場合があり、Microsoft独自WIC/HEIF機能と、フォト側のMedia FoundationやHEVC拡張・アプリ使用権限を同一視できない。
- https://support.microsoft.com/ja-jp/windows/apps/photos/photos-app-video-editor-error-can-t-view-this-file-type
- https://learn.microsoft.com/ja-jp/windows/win32/wic/heif-codec
- https://learn.microsoft.com/ja-jp/windows/win32/api/mfapi/nf-mfapi-mftenumex
- https://learn.microsoft.com/en-us/troubleshoot/microsoft-edge/development/video-playback-issues

## 調査コード
1. `WIC_Preflight_ReadOnly.ps1`：ユーザーの既存WIC DLL/CLSID/Photos/MSIに加え、現ユーザーのHEIF/HEVC関連AppXの名称・バージョン・状態を読取専用調査する。既存ファイルとJSONだけを扱いWindows側の設定を変更しない。
2. `HevcMfReadOnlyProbe.cpp` + 同名`.vcxproj`：**画像入力やレジストリ書き換えなし**で、Windows Media FoundationのHEVC decoder/encoder とH264 decoderを`MFTEnumEx`で列挙する。通常経路と、field-of-use登録を含む経路を区別する。
3. `.github/workflows/photos-hevc-mf-readonly.yml`：Windows Server 2022隔離VMでx64ビルド・列挙の基本動作・読取専用ガードを確認する。**Windows 10フォト実機の編集成功判定ではない**。

**MFT数の解釈の制約**：登録MFT数が0でも全てのHEVC解決策がないとは断定しない。1以上でもライセンス有効・デコード可能・フォトからの到達性は保証されない。HEVC MFTが見つかった場合の次の観測対象はMFT実活性化/エラー・Photosのバージョン差・画像種別・WIC経路の切り分け（安全な公開人工HEICで実施）。

## 改修の合否
- 画像原本の削除・上書き・タイムスタンプ変更をしない。
- 事前承認なしに有料HEVC購入、パッケージインストール、Microsoft Photos修復/リセット、WIC再登録・アンインストールをしない。
- Microsoft Photosを既存のWICプライベートDLLに「接続したから修正」と主張しない。
- 問題箇所を絞り、**Windows10標準フォトの編集/90°回転保存（人工画像作業コピーのみ）**の実際の復旧を最終合否にする。
- ロールバック策のないテスト用MSIの導入は禁止。ユーザーの実HEIC原本は診断に使用しない。

## 今後の診断順序
1. 既存診断スクリプトをCIで検証、追加Native probeのコンパイルと診断出力を検証。
2. 読取専用の実機診断1回でPhotos/WIC/AppX/MFの情報を揃える（必要時だけ実施を促す）。
3. 不足が確定した場合もWindows標準フォトの無料・安全な正式解決策か、既存ライセンス復旧可能性をまず調べる。購入強要なし。
4. 未確認の互換性を装いデコーダーを無理に入れ替えない。技術上Photosが要求する外部コンポーネントなしで編集できない場合は制約と代替策を明記する。

## 重要障害としての実機テスト準備
Windows CIでビルド・実行後、必要になった**1回の診断用**にだけGitHub Actions artifactに「Photos_HEVC_ReadOnly_Win10_x64」を保存する。ユーザーへ連続してZIPやMSIを試させることはしない。
- 含まれる4点は `Run_Photos_HEVC_ReadOnly.cmd`、`HevcMfReadOnlyProbe.exe`、`WIC_Preflight_ReadOnly.ps1`、このREADME。
- cmdの役割は読取専用の2診断を順次呼び出し、**同じフォルダに新規の**テキストレポートを残すだけ。既存の写真、フォト、WIC、レジストリを一切編集しない。
- `WIC_Preflight_ReadOnly.ps1` は個別JSONも新規作成する。WICのDLLパスにユーザー名等が含まれ得るのでアップロード前に確認。
- 標準フォトがHEVCのライセンスチェックに失敗する場合は、このMFT登録数だけでは判断できない。
- ユーザー実機での実行を了承なしに強制しない。未実施なら「原因未確定」。
