# 既存WPRから全プロセスCPU負荷を調べる（読み取り専用）

**最優先課題：** WicHeic 1.1.33でWindowsフォトを開くとPC全体CPU100%に張り付く問題の原因特定。これまでのCSVはPhotos・dllhost・Explorerの一部しか採取していない。録画済みの `CPU_Stacks.etl` に入っている全プロセスCPUサンプルを利用する。

## 操作

1. 配布ZIPを**すべて同じフォルダーへ展開**。
2. `Run_Existing_ETL_CPU_Report.cmd` をダブルクリックし、既存の `CPU_Stacks.etl` を選択する。例えば従来の `COM_CPU_WPR_Manual_20261030_203013_576` フォルダー内のファイル。現在の1.1.33のWPRを**もう一度録画する必要はない**。
3. 同じフォルダーに作成される `CPU_AllProcesses_日時.txt` を開いてレビューし、問題なければその**テキストレポートだけ**共有する。生ETLやフルパスは共有不要。

## 2026-10-30の実機提出結果と新しい解析方法

ユーザー提出の `CPU_AllProcesses_20261030_224307_303.txt` は正しい形式で生成されていたが、同レポートのプロセス名不明は **293,247/456,016サンプル（64.31%）** だった。うち **249,525件（54.72%）** はPIDも不明。別の提出済み `Process_CPU_1s.csv` を照合すると、レポートで不明とされたPID936はExplorerだと確認できる。アプリのCPU100%問題の原因が64.31%を占めるという意味ではない。

このバージョンでは、**CPUイベントの受信時に名前が不明でも、即座にunknownへ固定しない**。ETL内のプロセス／スレッドのrundownを最後まで読んだ後で帰属を行う。スレッドIDが複数のPIDに使われたときは再利用と判断し、誤った推測による復元を拒否する。新レポートの `AttributionRawPID`、`DeferredUniqueTIDRecovered`、`PIDUnresolved`、`ThreadIDsAmbiguous` で、どの程度復元できたか検証する。なお、実ユーザーETLでの改善率は **未確認**。

**同じ診断をもう一度録画する必要はない**。過去の提出ZIPはChatGPTのファイルライブラリから回収できている。必要であれば、ツールと元ZIPはその都度こちらで再添付する。生ETLを公開GitHub・CIへアップロードしない。

## 内容と制限

- Microsoft公式OSS `Microsoft.Diagnostics.Tracing.TraceEvent`（NuGet 3.2.8）で、CPUのETW `PerfInfoSample` とプロセス／スレッド情報を読み取る。
- 全プロセスのCPUサンプル数と、10秒区間で多くのCPU仕事を使ったプロセス上位を**実行ファイル名だけ**で記録する。さらに同名の `dllhost.exe` / フォトなどの**PID別集計**を含む。PIDは一回の録画の中だけで意味があり、プロセス終了後に再利用される場合がある。タスクマネージャーのCPU100%と同じ計算式ではない。特に「割合」は**スレッドID付きのCPUサンプル**に占める比率であり、PC全体CPU使用率ではない。
- 10秒区間ごとに `withThreadID`（プロセス帰属集計可能）、`allReceived`（取得された全サンプル）、`noThreadID`（スレッドID不明・0）を併記。**スレッドIDが0のサンプル数を「CPUの全アイドル時間」と読み替えない**。ETWのサンプリング対象、欠落、DPC/ISRを考慮し、PC全体100%を直接推算しない。最初/最後の10秒区間は部分区間の場合あり。
- 写真原本、ETL原本、レジストリは一切変更せず、外部通信しない。結果ファイルも既存ファイルは**絶対に上書きしない**。
- ETL内には個人ファイルパスやOSの機密情報が含まれる可能性があるため、生ETLやパスはGitHubなどへ公開しない。集計レポートにもアプリ名が含まれる可能性があるので共有前に確認。
- Windows10 64bit、.NET Framework 4.8を利用。Windows CIでコンパイルとファイル上書き防止を検査するが、**実際のユーザーETLでの動作はまだ検証されていない**。
- Windows 10本体のCPU100%を修正するツールではなく、**どのプロセス／時間帯へ修正を集中すべきか特定する**研究用補助ツール。

参考：
- https://learn.microsoft.com/en-us/troubleshoot/windows-server/support-tools/support-tools-xperf-wpa-wpr
- https://github.com/microsoft/perfview/blob/main/documentation/TraceEvent/TraceEventProgrammersGuide.md
