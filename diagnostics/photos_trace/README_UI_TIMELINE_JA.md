# 1.1.28 実機用：フォト表示異常／WIC復号時刻の対照記録

**開発用・Windows 10標準フォトでの動作未確認。** GitHub ActionsのWindows Server 2022実行と、Windows 10実機で標準フォトの描画を監視できることは別問題。正しい動作を確認できるまで、通常の写真を使わず、公開人工HEICの作業コピーでのみ試す。

## 対象症状・再現条件
- フォトの画像表示は最初からウィンドウフィットの縮小状態。
- その状態からさらに縮小し、90°回転するとPNG/HEICの両方で横長に固定されることがある。
- HEICでは画像消失6～7秒や表示領域極小化が目立ち、PNGは横長症状からの復旧が比較的速い。
- 目的は、フォトの**手動で見えた異常の時刻**と、既存WicHeic 1.1.28が記録する**デコード・画素取得**の時刻を照合すること。

## できること・できないこと
- `Photos_UI_Timeline_Capture.ps1` は起動後90秒で終了する。最大180秒まで。既定で約200ms間隔で、**現在操作中のウィンドウの外枠サイズ、クライアント領域サイズ、PID、CPU累積ミリ秒、メモリ量、UTC FILETIME、Windows起動後の単調増加tick**だけをCSVへ保存する。
- 異常が画面に見えた瞬間、**Windows標準フォトの画面でF9を短く押す**と`MARK_F9`を記録する。入力フックやキーの内容収集は一切せず、F9の押下状態だけを確認する。200ms間隔のため、ごく短い押下は拾えないことがある。F9がフォト側で別の動作を起こす場合は押さない。
- `Photos_UI_Timeline_Correlate.ps1` は直近10秒前後に記録されたWICの数値イベントを一覧化し、`DecoderFullDecode`の終了時刻と記録済み所要時間から**推定開始時刻**を示す。録画・画面キャプチャ・ウィンドウタイトル・写真名・EXIF・位置情報は記録しない。
- **測れるのはフォトのウィンドウサイズであり、画面内の画像表示矩形ではない。** クライアント領域サイズが一定でも画像だけ横長・消失・極小化する可能性は残る。この診断だけで画像の実描画領域やGPU遅延を直接測定したとは主張しない。
- フォトのウィンドウPIDはWICが動くプロセスのPIDと一致しない可能性がある。記録されないイベントはゼロだったとは限らない。F9マークとWICイベントが近接しても**因果関係の証明ではない**。

## 実行方法（導入・再起動・管理者権限不要）

GitHub内の`diagnostics/photos_trace`を**全ファイルまとめて**Cドライブ等のローカルフォルダに展開。Windows10標準のPowerShell 5.1を使用する。

1. **`01_Photos_UI_Timeline.cmd` をダブルクリック**。このコマンドは既存の`PhotosTrace_Start.ps1`を実行して、ユーザーのHKCUに2つのWIC診断値を最大20分だけ一時設定する。既存の値がある場合は安全のため開始を拒否する。**インストール済みWIC、Windowsフォト、Windowsのデコーダー優先順位は変更しない**。
2. ウィンドウが開いたらWindowsフォトへ切り替え、**公開人工画像の作業コピー**で「フィット→追加縮小→回転」を再現する。現象を目で確認した瞬間だけF9を押す（必須ではないが、マークがないと相関分析不可）。
3. 90秒後に自動で観測終了。**finallyによって既存の`PhotosTrace_Stop.ps1`を実行**し、このフォルダ自身が作ったHKCU設定だけを消す。強制終了した場合は同じフォルダの`PhotosTrace_Stop.ps1`で停止を確認。記録自体も最大20分で有効期限切れとなる。
4. 同じフォルダの`Photos_UI_Timeline_*.csv`（表示窓の数値）、`WIC_Photos_Trace_*.txt`（WIC数値ログ）、`Photos_UI_Correlation_*.txt`（時刻照合結果）を確認する。解析プログラムが自身以外のファイルを上書きすることはない。

手動実行も可能：`Photos_UI_Timeline_Capture.ps1` はレジストリに一切書き込まない。WICのログと合わせたい場合のみ従来の`PhotosTrace_Start.ps1`→`Photos_UI_Timeline_Capture.ps1`→`PhotosTrace_Stop.ps1`→`Photos_UI_Timeline_Correlate.ps1`を実行する。

## 安全に関する補足
- 最長180秒、200msサンプリングの既定値で、Windows上で常駐しない。コマンド画面を開いたまま使い、手動でフォトに切り替える。
- CSVや集計にはPIDやPC内の負荷統計・ウィンドウ寸法が入る。他人へアップロードする場合は中身を確認する。
- WICログは既存の上限2MiB・数値のみの記録・サンプル化が適用される。AppContainerのアクセス制限によってログが空になる場合もある。
- `PhotosTrace_Start.ps1`と`PhotosTrace_Stop.ps1`は既存の安全検証済みスクリプトを再利用。新設のスクリプトはユーザーのHEIC本体・保存先・画像プレビューに一切触れない。

## 参考
- Windowsの時計の基準: https://learn.microsoft.com/en-us/windows/win32/sysinfo/time-functions
- Win32 foreground/window bounds: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowrect
- Add-TypeネイティブAPI: https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.utility/add-type?view=powershell-5.1
- 既存WIC診断仕様: README_JA.md
