# WicHeic 1.1.30 CPU負荷総合診断：実HEICフォルダ・手動終了

**人工HEICは不要、60秒・90秒の制限なし。** 既存画像・フォルダの中身を診断ツールは変更しません。WicHeic 1.1.30のMSIも変更しません。

1. ZIPをCドライブなどのローカルフォルダへ展開してください（ZIPから直接起動しない）。
2. `01_Start_CPU_Manual.cmd` をダブルクリックし、Windowsの管理者権限（UAC）を承認。
3. 「RECORDING」と出たら、普段のHEICフォルダをExplorerで開き、サムネイル生成・スクロール・フォト起動・画像切替・拡大縮小等、重い操作をそのまま再現してください。**原本に回転保存などの編集はしない**でください。
4. 終了したい時にコマンド画面へ戻ってEnterキーを押すとWPRが停止し、結果ZIPが生成されます。操作後は早めに終了してください（WPRメモリ循環バッファでは古いイベントが上書きされます）。
5. `COM_CPU_WPR_Manual_日時.zip` には解析用のETL、CPUサンプル、WIC数値イベント、要約、SHA-256記録が入ります。ETLを保管すれば解析やシンボル追加のたびに再採取する必要はありません。

**必要情報を一度に取得**：CPUスタックから使用モジュール/関数候補、プロセス別CPU、スレッド切替・待機、WICフル復号とPID/UTC時刻の相関。関数名が不明な場合はETLのシンボルを後から解決します。原因と対策の実機改善まで1回で保証するものではありません。

**注意**：WPR CPUスタックの記録には管理者権限が必要。ほかのWPR収集があれば中止します。WPRはメモリモードで自動終了なし、古いイベントは上書きされ得ます。WICログは悪用防止のため12時間で自動失効し、2MiBを超えません。共有するETLには**写真・ファイルのパス、アプリの実行情報など機微な情報**が含まれる場合があります。提出前に共有範囲をご判断ください。

- Microsoft WPR: https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options
- WPR memory logging: https://learn.microsoft.com/en-us/windows-hardware/test/wpt/logging-mode
- WPR CPU stack analysis: https://learn.microsoft.com/en-us/troubleshoot/windows-server/support-tools/support-tools-xperf-wpa-wpr
