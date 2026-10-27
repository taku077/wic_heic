# 1.1.28 実機用：フォト表示異常／WIC復号時刻の対照記録

**開発用・Windows 10標準フォトでの動作未確認。** GitHub ActionsのWindows Server 2022実行と、Windows 10実機で標準フォトの描画を監視できることは別問題。正しい動作を確認できるまで、通常の写真を使わず、公開人工HEICの作業コピーでのみ試す。

## 対象症状・再現条件
- フォトの画像表示は最初からウィンドウフィットの縮小状態。
- その状態からさらに縮小し、90°回転するとPNG/HEICの両方で横長に固定されることがある。
- HEICでは画像消失6～7秒や表示領域極小化が目立ち、PNGは横長症状からの復旧が比較的速い。
- 目的は、フォトの**手動で見えた異常の時刻**と、既存WicHeic 1.1.28が記録する**デコード・画素取得**の時刻を照合すること。

## できること・できないこと
- `Photos_UI_Timeline_Capture.ps1` は起動後90秒で終了する。最大180秒まで。既定で約200ms間隔で、**現在操作中のウィンドウの外枠サイズ、クライアント領域サイズ、PID、CPU累積ミリ秒、メモリ量、UTC FILETIME、Windows起動後の単調増加tick**だけをCSVへ保存する。
- 異常が画面に見えた瞬間、**Windows標準フォトの画面でF9を短く押す**と`MARK_F9`を記録する。キーボードフックや他のキー入力の収集はせず、F9ホットキーの通知のみを記録する。旧版では200ms間隔の確認により短い押下を見逃していた可能性がある。改良版はWindowsのグローバルホットキー（`RegisterHotKey`）を一時登録し、`WM_HOTKEY`メッセージをキューから読み出すため、**フォトが前面でも短いF9押下を記録できる設計**。記録中F9は診断ツールに横取りされ、フォト自身には届かない可能性がある。既存アプリと競合して登録に失敗した場合は明示的に中止し、無音で記録漏れを起こさない。
- `Photos_UI_Timeline_Correlate.ps1` は直近10秒前後に記録されたWICの数値イベントを一覧化し、`DecoderFullDecode`の終了時刻と記録済み所要時間から**推定開始時刻**を示す。録画・画面キャプチャ・ウィンドウタイトル・写真名・EXIF・位置情報は記録しない。
- **測れるのはフォトのウィンドウサイズであり、画面内の画像表示矩形ではない。** クライアント領域サイズが一定でも画像だけ横長・消失・極小化する可能性は残る。この診断だけで画像の実描画領域やGPU遅延を直接測定したとは主張しない。
- フォトのウィンドウPIDはWICが動くプロセスのPIDと一致しない可能性がある。記録されないイベントはゼロだったとは限らない。F9マークとWICイベントが近接しても**因果関係の証明ではない**。

## 実行方法（導入・再起動・管理者権限不要）

GitHub内の`diagnostics/photos_trace`を**全ファイルまとめて**Cドライブ等のローカルフォルダに展開。Windows10標準のPowerShell 5.1を使用する。

1. **`01_Photos_UI_Timeline.cmd` をダブルクリック**。このコマンドは既存の`PhotosTrace_Start.ps1`を実行して、ユーザーのHKCUに2つのWIC診断値を最大20分だけ一時設定する。既存の値がある場合は安全のため開始を拒否する。**インストール済みWIC、Windowsフォト、Windowsのデコーダー優先順位は変更しない**。
2. ウィンドウが開いたらWindowsフォトへ切り替え、**公開人工画像の作業コピー**で「フィット→追加縮小→回転」を再現する。現象を目で確認した瞬間だけF9を押す（必須ではないが、マークがないと相関分析不可）。
3. 90秒後に自動で観測終了。F9ホットキー登録は記録終了時に`finally`で解除し、**さらにfinallyによって既存の`PhotosTrace_Stop.ps1`を実行**し、このフォルダ自身が作ったHKCU設定だけを消す。強制終了した場合は同じフォルダの`PhotosTrace_Stop.ps1`で停止を確認。記録自体も最大20分で有効期限切れとなる。
4. 同じフォルダの`Photos_UI_Timeline_*.csv`（表示窓の数値）、`WIC_Photos_Trace_*.txt`（WIC数値ログ）、`Photos_UI_Correlation_*.txt`（時刻照合結果）を確認する。解析プログラムが自身以外のファイルを上書きすることはない。

手動実行も可能：`Photos_UI_Timeline_Capture.ps1` はレジストリに一切書き込まない。WICのログと合わせたい場合のみ従来の`PhotosTrace_Start.ps1`→`Photos_UI_Timeline_Capture.ps1`→`PhotosTrace_Stop.ps1`→`Photos_UI_Timeline_Correlate.ps1`を実行する。

## 安全に関する補足
- 最長180秒、200ms数値サンプリングの既定値で、Windows上で常駐しない。F9は数値サンプリング間に押して離してもWindowsのホットキーメッセージに記録が残るため、旧方式より取り逃しにくい。ただしF9自体が他アプリに占有されている場合は実行開始時に失敗する。コマンド画面を開いたまま使い、手動でフォトに切り替える。
- CSVや集計にはPIDやPC内の負荷統計・ウィンドウ寸法が入る。他人へアップロードする場合は中身を確認する。
- WICログは既存の上限2MiB・数値のみの記録・サンプル化が適用される。AppContainerのアクセス制限によってログが空になる場合もある。
- `PhotosTrace_Start.ps1`と`PhotosTrace_Stop.ps1`は既存の安全検証済みスクリプトを再利用。新設のスクリプトはユーザーのHEIC本体・保存先・画像プレビューに一切触れない。

## 参考
- Windowsの時計の基準: https://learn.microsoft.com/en-us/windows/win32/sysinfo/time-functions
- Win32 foreground/window bounds: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect
- Add-TypeネイティブAPI: https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.utility/add-type?view=powershell-5.1
- 既存WIC診断仕様: README_JA.md

## 2026-10-27 検証と配布上の位置付け

- 新規キャプチャスクリプトと照合スクリプトは、Windows Server 2022上の**Windows PowerShell 5.1**で起動・終了、CSV数値記録、人工WICイベントとの時刻相関を検証済み。独立CIの記録は [#40484727909](https://github.com/taku077/wic_heic/actions/runs/40484727909)。
- 一括起動`Photos_UI_Timeline_Run.ps1`の短縮5秒試験では、既存WICログ有効化と停止・HKCU`TraceFile`が残らないことを検査。GitHub Actions[#40484774291](https://github.com/taku077/wic_heic/actions/runs/40484774291)。
- **Windows10標準フォト実機での使用・PNG/HEIC表示崩れ時の時刻相関は未検証。** CIの画面は標準フォトそのものではない。
- このキットはMSIを含まない。現行インストール済みのWicHeic 1.1.28は維持する。
- ファイル一式ZIPは、専用`diagnostics(release):`コミットで全CI・MSI安全検証を通過した場合だけGitHub Actionsアーティファクトにアップロードする。誤って再ビルドした1.1.28のMSIを配布することはない。

## 2026-10-27 F9未記録への改良

1.1.28導入後の実機記録で、F9を押したとの報告にもかかわらず`MARK_F9`が0件だった。旧実装の`GetAsyncKeyState`は200msごとの瞬間押下判定であり、**フォトが前面だからコマンドプロンプトにキーが届かなかった**と断定するのは誤り。短いキー入力を確認間隔の間に押して離した可能性がある。

新実装は`RegisterHotKey(NULL, ID, MOD_NOREPEAT, VK_F9)`を診断中だけ設定する。通知はスレッドの`WM_HOTKEY`メッセージキューに蓄積され、200msのサンプリング期間内の短い押下も保持する。使用後に`UnregisterHotKey`を実行。**キーを押してからCSV書込までのタイムスタンプはキュー取り出し時刻であり、最大およそサンプリング間隔ぶん遅れる可能性がある**（完全な入力時刻ではない）。

外部資料：<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate>、<https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey>、<https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-hotkey>。

実機側はZIP内の7ファイルを同じディレクトリの新版に揃え、旧版と混在させない。1.1.28 MSIの再導入は不要。
