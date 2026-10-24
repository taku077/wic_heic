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

## 2026-10-22：安全な一時復旧キット完成（Windows CIのみ合格、フォト実機は未確認）

**P0 標準フォトの編集・回転不能の復旧候補が、Windows Server 2022隔離CIのインストール・切り戻し・画像処理試験を通過した。**
- [CI #39770708757](https://github.com/taku077/wic_heic/actions/runs/39770708757)：グローバルWIC→プレビューv0.4.0と、**プレビューv0.4.0→グローバルWIC**の両順序で、decoder/encoder 64bit COM登録・独立Shell Provider・DLL SHA-256・相互アンインストール後のハッシュ一致に成功。
- [CI #39771550007](https://github.com/taku077/wic_heic/actions/runs/39771550007)：v0.4.0登録中、実WICの画像デコード・エンコード、synthetic HEIC無再圧縮回転90/180/270、画素完全一致、ICC同一、Exif Orientation、WIC読み戻し検証に成功。
- [CI #39773274867](https://github.com/taku077/wic_heic/actions/runs/39773274867)：既存v0.4.0を残したまま、`Install_Experimental_WIC.ps1` の既存WIC衝突チェック（v0.4だけは許容）、WICの一時導入、画像試験、`Uninstall_WIC_Trial_RestorePreview.ps1` の試験用WICだけの削除、v0.4 COM関連付けとサーバー/プライベートDLL SHA一致を**実行して合格**。単一の人工12MP 8bit Main HEICを含むオプションの実機テストキットも生成。
- オプションキット名 `WIC-Photos-P0-TemporaryGlobalWIC-Win10-UNVERIFIED`、artifact ID `12423925765`。**これは配布候補であり正式版・解決確定ではない。** ユーザーの明示的同意なしにWindows 10 PCへはインストールしない。実写真・特に過去復旧済の画像は編集しない。

### 現時点で絶対に主張しない事項
- CIはWindows Server 2022で、Windows 10のMicrosoft Photos 2026.11090.22001.0を起動・操作していない。**編集ボタン有効化・回転・保存の実機成功はまだ0件**。
- Microsoft公式ではWindows10フォトにHEIFとHEVCの両拡張が必要な場合がある。新しい読取専用実機診断ではHEVC AppXとMedia Foundation HEVC decoderが検出されず、WICを足すだけで警告が消える保証はない。
- 初回Explorerサムネイル30%以上短縮もWindows10実描画では未確認。プレビューv0.4.0 COMとDLLの保持は確認できたが、導入中のExplorer初回時間・CPUピークは未計測。
- JPEG同等の完全な色・全Exif・MakerNote/ICC・XMP/HDR/補助画像保護、PhotosがWIC native lossless経路を実際に選択することは未確認。**既存実写HEICをWindowsフォトで上書き保存させてはいけない**。
- 将来の実機試験は公開人工HEIC作業コピー、編集・回転有効性、寸法・ハッシュ、復旧スクリプト実行、v0.4サムネイル維持の一度限りのゲートで実施する。

## 2026-10-22 21時頃：Windows 10フォト実機で復旧候補の失敗が確定（P0 継続）

実機ユーザー報告：CI #39773274867で合格したグローバルWICの試験導入後も「HEVC拡張機能が必要」警告が**消えず**、標準フォトの編集・回転が**使用不可**。人工HEIC回転成功の報告はなく、旧版の「正常表示」を実現できなかった。

### ソース監査で確定した構成問題
- `Installer/Package.wxs` の `<Package Id="prsyahmi.WicHeic" ... Version="1.1.18">` は、以前の旧WIC Repair Test 1.1.18のコミット `f5b66fe89fa203c89967b46399ed6af61a6161e8` と今回の研究ブランチで**内容もblob SHAも同一**。新版試験でコードが変わっても識別子・製品バージョンを変えなかった。製品ライフサイクルを正しく管理しておらず、今後のin-place/repair/upgradeの正確性を主張できない。これは**独立した設計不備**であり、今回のPhotos失敗の原因と即断しない。
- `Installer/WicHeic.wxs` のSelfRegCost DLL成分GUID、`wic_heic/dllmain.cpp` のWIC decoder/encoder CLSID登録は旧版と**ソース同一**。変更は主に `HeicBitmapFrameDecode.cpp` の**サムネイル用の条件付きプライベート最適化**で、通常のグローバルWICビルドに同じ最適化が適用されたと考えない。
- 10月22日20時の実機診断では**グローバルWICの全登録が空**であり、導入用スクリプトは既存グローバルWICの上書きを拒否する仕様だった。この実機試行を「旧1.1.18の上書きによる不具合」と説明する証拠はない。v0.4.0は別製品識別子・別Shell Providerで保持。
- 10月20日23時の旧WIC登録は確認できる一方、**当時HEVC拡張AppX/MFデコーダーが利用できたかは記録されていない**。Photos版番号が同じでもMicrosoft HEIF/HEVC依存の状態変化は否定できない。
- Microsoft公式（https://support.microsoft.com/ja-jp/windows/apps/photos/photos-app-video-editor-error-can-t-view-this-file-type）はWindows10フォトで写真だけでもHEIFとHEVC両拡張が必要な場合を明記。10月22日現ユーザー診断でHEVC AppXなし・MF HEVC decoder=0。これは現状の警告と整合するが、旧正常状態から変化した理由は未特定。
- v0.4.0のHKCU ShellExへのサムネイルProvider登録がフォトに影響したかどうかは標準フォト実機UIでのA/Bが存在せず、原因と断定しない。

### 開発判断・安全措置
1. **今のMSI復旧候補は実機不合格。製品ではなく不採用**。ユーザーへの繰り返し実機検証・アンインストール・新規MSI導入依頼はしない。
2. Windows CIのWIC direct decode/encodeや回転・ICCの成功をフォトの成功に読み替えない。今後、UI編集/回転の互換性が証明されるまで `photos-wic-preview-coexistence.yml` からの実験MSI artifactの自動配布を**停止**。
3. 同じPackage識別子＋同じVersionで異なるバイナリをリリースしない。将来の修正版は専用のインストール・完全削除・復元方式と、重複MSI/サムネイルProvider/権限チェックで確実な衝突防止を要求。
4. 既にユーザーに導入された試験WICはPhotosの問題を解決していない。実機の状況を変えるときは**試験WICだけを切り戻す既存のチェック付きスクリプト**を一度だけ使う方針。v0.4.0と原本は変更しない。
5. Windows10標準フォト自身のHEVC要件を正規かつ安全に満たす方法がなければ、WIC改修で解決したと偽装しない。旧WICが編集できたとのユーザー実績は実在する報告として扱い、断定的に否定しない。

### 高速化の技術的範囲
v0.4.0 Explorer初回表示の目標30%以上改善研究は**別トラックで継続**。安全性と同時に実測のある高速化だけ製品候補とする。

## 2026-10-24 追加：純正HEIFと自作HEICの登録・COM生成を画像なしで切り分ける

前回までのWindows Server 2022 CIでは、WICに Microsoft純正HEIFデコーダー（CLSID E9A4A80A-44FE-4DE4-8971-7150B10A5199）と自作WIC（CLSID 6215667C-74CA-4076-A769-3F834C3F25F5）が共存していた。純正HEIFのCOM生成は同CIで 0x88982F8B（コンポーネント初期化失敗）。しかしこれは**HEIF拡張が導入済みのユーザーWindows10で同じ結果になる証明ではない**ため、実機診断で画像を一切使わず取得できるようにした。

### 診断機能と操作範囲
- 既存の `HevcMfReadOnlyProbe.exe` を拡張。引数なしではこれまで通りMedia Foundation HEVC/H264 MFT列挙に加え、WICコンポーネント**登録情報だけ**を照会する（3件：Microsoft純正HEIF decoder、自作WIC HEIC decoder/encoder）。各COM CLSIDの登録と公開するコンテナーGUID（公式HEIF＝E1E62521-6787-405B-A339-500715B5763F、独自HEIC＝98CE31DD-5435-41BD-A1AF-29C387C20135）を区別。
- 明示引数 `--wic-activate-no-image` は、登録情報だけでは判別できない「実際にインスタンス生成できるか」を調べる。純正／自作WICの `CreateInstance` と、公式HEIFコンテナーGUIDでのエンコーダー `CreateEncoder` のHRESULTを記録する。**画像ストリーム、実写真、ファイル名、画像データは渡さない。** 
- ワンクリック `Run_Photos_HEVC_ReadOnly.cmd` は今回の画像なしCOM生成モードを実行する。既存の `WIC_Preflight_ReadOnly.ps1` による登録・AppX・MSI確認も継続。診断レポートとJSONは**新規ファイルとして**診断フォルダ内に作成される。
- **「読み取り専用」の限界**：診断コードは画像を開かず、レジストリの書込み・アプリパッケージ追加削除・MSI導入・Photos操作を指示しない。ただし `CreateInstance` によって既存の外部WIC DLLが診断プロセス内に読み込まれ、DLL固有の初期化やOS内部キャッシュが実行される可能性まで否定できない。あくまでもWIC COMの生成可否であり、HEVC復号成功、Storeの有効ライセンス、標準Photosの編集可否を証明しない。
- 入力引数は「なし」か上記専用引数のみを許可。画像パス、Windows登録変更、メタデータ書込みのオプションは存在しない。
- **元写真、過去復元した写真、HEIC Manager、既存MSIは触らない**。診断レポートにはパスやソフト状態が含まれ得るため、ユーザーはアップロード前に内容を確認する。

### 使いどころ
今回の追加は、CI隔離ランナーで失敗するMicrosoft純正HEIF COM生成がWindows10実機では成功するのか、その結果が既存HEVC AppX/MFTの欠如と両立するのかを同時に判別するためのもの。以前の診断でWindows10のHEVC MFT=0は確認済みであり、**同じ値を再び収集するだけの無目的な診断を要求しない**。自作WICの編集・回転保存が改善したと証明できるまでは試験MSI配布を再開しない。

## 2026-10-24追加：任意の人工HEIC実復号テスト（画像なし診断とは分離）

**きっかけ**：Windows10実機から届いた読取専用レポートでは、Microsoft純正HEIFデコーダー・純正HEIFコンテナーエンコーダー・自作WICデコーダー／エンコーダーのCOM生成が**いずれもS_OK**。一方、MF HEVCデコーダー／エンコーダー列挙は0。この結果は「WICオブジェクトの生成可否」と「そのコーデックがHEICのビットストリームを実際に読めるか」を分けない限り解釈できない。

**新しいテスト（任意、既存診断では自動実行しない）**：
- 配布キット中の `Run_Photos_WIC_Synthetic_Only.cmd` を明示的に実行したときだけ、`HevcMfReadOnlyProbe.exe --synthetic-wic-test` が実行される。
- 受け付ける入力は、実行EXEと**同じディレクトリにある固定名** `fixture-irot3-exif6-noaux.heic` のみ。サイズ10597バイトと、Gitのblob SHA-1 `126b24780e8c36a0e1521df982067ab12f971470`（`blob 10597\0` を付けた内容SHA1）を両方検証し、不一致・欠損・リパースポイントなら**復号せず拒否（終了コード8）**。任意の画像ファイル名や写真のパスを指定する引数は存在しない。
- SHA確認後、Microsoft純正HEIF decoderと自作WIC HEIC decoderそれぞれをCLSIDで明示選択し、固定人工画像の `Initialize` → `GetFrame(0)` → `GetSize` → WIC RGB24変換 → **16×8画素のみCopyPixels**を実行。各段階のHRESULTと読み取り成功時だけ合成画素のFNV64を記録。入力画像への書込み、JPEG/HEICファイルの保存、Photos起動、MSI/レジストリ変更はしない。ただし既存のWICコーデックDLLが診断プロセスにロードされ得る。
- 従来の `Run_Photos_HEVC_ReadOnly.cmd` は引き続き画像を**一切開かない**。新しい人工画像テストと分離。両方を繰り返しても進捗とは扱わない。
- 任意の人工画像テストは標準フォトGUIのボタン／回転保存やHEVC使用許諾を直接テストしたものではない。Windows10におけるMicrosoft純正・自作WICの**実復号可能性の差**を調べる限定的な検証。
- [CI #40072408037 SUCCESS](https://github.com/taku077/wic_heic/actions/runs/40072408037)：本体ビルド、既存画像なしテスト、人工HEICのSHA整合性・サイズ判定、画像のみ試験が実行可能であること、意図的に一時コピーの1バイトを変えても復号に進まず拒否すること、旧ワンクリック診断への非干渉、計6ファイルを診断アーティファクトへ梱包することを確認。CI runnerでは自作WIC MSIが入っていないため、**自作WICの16×8 CopyPixels成功まではこのCIだけでは確認できない**。
- 旧版・研究版MSIがインストールされた状態の総合CIへ診断を追加しようとした実験 #40072518664 はジョブ開始前に失敗。**直ちに以前の成功済みワークフローへ戻し**（コミット `e10a178f3f6252e70102b8e7c5ad35741a7c2f52`）。製品DLLや画像保護策に変更はない。

**安全条件**：ユーザーの実写真、過去に復元した画像、HEIC Managerは対象外。人工HEIC以外の入力を受け付けず、実機に新MSIの導入を求めない。結果の数字だけでは標準フォトの編集・回転保存を達成したとは言わない。
