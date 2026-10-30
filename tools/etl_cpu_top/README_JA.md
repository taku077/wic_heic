# 既存WPRから全プロセスCPU負荷を調べる（読み取り専用）

**最優先課題：** WicHeic 1.1.33でWindowsフォトを開くとPC全体CPU100%に張り付く問題の原因特定。これまでのCSVはPhotos・dllhost・Explorerの一部しか採取していない。録画済みの `CPU_Stacks.etl` に入っている全プロセスCPUサンプルを利用する。

## 操作

1. 配布ZIPを**すべて同じフォルダーへ展開**。
2. `Run_Existing_ETL_CPU_Report.cmd` をダブルクリックし、既存の `CPU_Stacks.etl` を選択する。例えば従来の `COM_CPU_WPR_Manual_20261030_203013_576` フォルダー内のファイル。現在の1.1.33のWPRを**もう一度録画する必要はない**。
3. 同じフォルダーに作成される `CPU_AllProcesses_日時.txt` を開いてレビューし、問題なければその**テキストレポートだけ**共有する。生ETLやフルパスは共有不要。

## 内容と制限

- Microsoft公式OSS `Microsoft.Diagnostics.Tracing.TraceEvent`（NuGet 3.2.8）で、CPUのETW `PerfInfoSample` とプロセス／スレッド情報を読み取る。
- 全プロセスのCPUサンプル数と、10秒区間で多くのCPU仕事を使ったプロセス上位を**ファイル名だけ**で記録する。タスクマネージャーのCPU100%と同じ計算式ではない。特に「割合」は実行中サンプルに占める比率であり、PC全体CPU使用率ではない。
- 写真原本、ETL原本、レジストリは一切変更せず、外部通信しない。結果ファイルも既存ファイルは**絶対に上書きしない**。
- ETL内には個人ファイルパスやOSの機密情報が含まれる可能性があるため、生ETLやパスはGitHubなどへ公開しない。集計レポートにもアプリ名が含まれる可能性があるので共有前に確認。
- Windows10 64bit、.NET Framework 4.8を利用。Windows CIでコンパイルとファイル上書き防止を検査するが、**実際のユーザーETLでの動作はまだ検証されていない**。
- Windows 10本体のCPU100%を修正するツールではなく、**どのプロセス／時間帯へ修正を集中すべきか特定する**研究用補助ツール。

参考：
- https://learn.microsoft.com/en-us/troubleshoot/windows-server/support-tools/support-tools-xperf-wpa-wpr
- https://github.com/microsoft/perfview/blob/main/documentation/TraceEvent/TraceEventProgrammersGuide.md
