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

## 実証済みの開発成果（2026-10-22）
- 機能追加: HEVC MF登録列挙・WIC/AppX診断（フォト実機挙動とは別）。
- **CI #39763691103 成功**: 計測用EXE x64ビルドとMedia Foundation MFTEnumExの安全な呼び出し。Windows Server 2022ランナーではHEVC decoder=0、HEVC encoder=0、H264 decoder=1（ユーザー実機の結果ではない）。
- **CI #39764370659 成功**: 読取専用のワンクリックCMD+EXE+PS1+説明文の4点を揃え、任意の一度だけの実機調査向けGitHub Actions artifact `Photos_HEVC_ReadOnly_Win10_x64` を出力。14日でアーティファクト期限切れ。
- 診断時の実機ファイル・レジストリ・MSI・写真・Photos/HEIC Managerは変更しない。ただしユーザーの端末上に新規診断テキストとJSONを生成する。
- まだ標準フォトの回転/編集は復旧していない。**「診断のCI合格」と「ユーザーの不具合解消」を混同しない**。

### 読取専用結果を受け取った後の分岐
| 端末の結果 | 次の調査・安全方針 |
|---|---|
| HEVC AppX登録なし、MF HEVC decoder 0 | HEVC拡張不足が有力。まずWindowsの既存ライセンス/メーカー配布権利などの正規な無料利用可能性を調査。WICの再インストールでPhotos編集が直ると約束しない。 |
| HEVC AppXあり、MFでHEVCが0 | MFTがユーザー文脈から列挙できない、パッケージ登録・ライセンスや環境の制限をさらに調査。AppXが存在するだけで利用可と推測しない。 |
| HEVC MFTが1以上、標準フォトは依然警告 | MFTインスタンス起動／ライセンス/Photosのプロファイル非対応を切り分ける。**MFT数>0でも利用可能とは証明できない**。|
| フォトで編集はできるが回転保存だけ失敗 | `PhotosWicTrace` の診断条件が安全に揃った後、公開人工HEICの複製でWICの`WriteSource`/EncoderCommit/BitmapTransformの呼出しを調べる。画像所有者の原本は試験しない。 |
| 編集・回転できるが画素が変形 | 正しい寸法だけでは判定せず、非対称タイル画像のJPEG/HEIC幾何学比較を採用。再圧縮・EXIF・ICC・HDR等の保持も評価する。 |

**技術的な限界**: MicrosoftのWindows 10フォト自身が正規HEVC拡張の利用可能性を条件に編集を制限しているケースでは、第三者のWICサムネイル用DLL単独ではその制限を解除できない可能性が高い。非公式パッケージや偽装を採用しない。

## v0.4.0 MSIのコード実査（重要な根本的切り分け）
`Installer/FastPreview.wxs` と `diagnostics/thumbnail_warmup/FAST_PREVIEW_MSI_JA.md` の実装を確認した。
- `WicHeicFastPreview v0.4.0` は `WicHeicFastPreview.dll` のShellサムネイルProvider CLSID `{D973196C-165C-4FCE-8838-17C37037D384}` をHKLMに登録し、HKCUのHEIC ShellExをProviderへ向ける。
- 同梱の `WicHeicPrivateCodec.dll` はサムネイル生成の**内部デコーダー**であり、Windows全体へのHEIC WIC codec登録もWindowsフォトのMedia Foundation HEVC codec提供もしない。
- これは意図的な非干渉設計であり、v0.4.0のサムネイル改善で回転編集も自動改善するという推測は誤り。
- 一方、過去に別途導入したグローバルWIC `wic_heic.dll` は`Installer/WicHeic.wxs` の**別MSI/登録経路**。使用中のグローバルWICが残っているかは、ユーザー実機のWIC CLSID/ DLL SHA-256で判定する必要がある。
- 今回のWindowsフォトでHEVC不足警告が出た理由が「グローバルWIC消失」か「HEVC拡張不足」か「両方」かは、このソース監査だけでは決まらない。
- **対策として現在のv0.4.0を削除しない**。まず読取専用診断で登録差分を分離し、もし製品WICを修復するなら原本復旧可能なMSI/別Windows環境での検証とユーザー承認が必要。

## 2026-10-22: 過去と現在のWIC実機スナップショットを比較したP0原因調査
10月20日23:28（日本時間）の以前のログでは、**グローバルWIC HEIC Repair Test v1.1.18の64bit decoder/encoderが両方登録されていた**。10月22日20:00の新ログでは、その12個の検索位置すべてでdecoder/encoderが未登録になり、MSI登録は**Fast Thumbnail Preview v0.4.0のみ**になっている。両時点で標準Photosのバージョンは **2026.11090.22001.0**、Windows 10 Pro 22H2も同じ。

新ログ：HEIF Image Extension v1.2.48.0 は正常、HEVC AppXは現ユーザーの一覧に存在せず、Media Foundation列挙でHEVC decoder/encoderとも0件、対照のH.264 decoderは1件。調査プログラムは正常終了。これらの値はMicrosoft Photosの実活性化/ライセンスの判定ではない。

- **確定**：10/20→10/22にグローバルWICの登録が消失。旧v1.1.18とv0.4.0は設計上は別MSI・別CLSIDだが、導入順などの因果は未特定。
- **未確定**：旧v1.1.18をアンインストールしたかどうか、10/20時点にHEVC拡張が存在したか、グローバルWIC単独復元で現行Photosの編集ボタンが復活するか。
- **重要**：旧1.1.18には標準フォトでHEICの回転保存を実施した際、再エンコード・色深度低下・EXIF消失の既知の不具合がある。単純な無条件復元を完成済み修復と見なさない。ユーザーの元画像には触れず、現行v0.4.0を維持する。
- 今後、同じWindows隔離CI上でシステムWICとv0.4.0の両方の導入順序・COM DLL登録の独立性・安全なアンインストール後の登録復旧を検証した後、人工画像のみでWindows 10 Photos編集可能性を確認する。
- 本節はプライバシー保護のため、個人のディレクトリ・氏名・実写真・原本診断ZIP・具体的なファイルパスをGitHubに転載しない。
