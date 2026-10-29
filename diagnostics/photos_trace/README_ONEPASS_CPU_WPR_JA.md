# WicHeic 1.1.30 CPU重さ総合診断（1回採取版）

**目的**：CPU使用率と自作WICの復号履歴に加え、Windows標準WPRのCPUスタックを**一回の60秒観測で同時収集**する。CPU競合・実際にCPUを使用したプロセスやモジュールと、復号の時間帯を対応づける。

## 使い方
1. このZIPをローカルドライブの通常フォルダに展開する（ZIP内から直接実行しない）。
2. `01_Start_OnePass_CPU_Diagnostic.cmd` をダブルクリックする。UACの管理者権限確認が出る（WPRのCPUスタック収集に必要）。権限を許可できない場合は中止する。
3. コマンド画面に記録開始が出たら、Windows 10のフォトで**使い捨ての人工HEIC**を開く。重さが現れる操作を60秒以内に実施する。
4. 終了後、同じフォルダに生成される `COM_CPU_WPR_Result_日時.zip` **1個を**提出する。元のETLも結果フォルダに残るので、後から追加解析する際に録画し直す必要はない。
5. 進行中はコンソールを閉じない。WPRを既に使用中の場合は開始を拒否する。他のWPR記録は停止しない。

## 記録内容と制約
- `CPU_Stacks.etl`：WPR CPU.Verbose + GeneralProfile.Verbose。CPUサンプリングとスタック／スケジューラ情報。**ユーザー名・パス・コマンドライン・ほかのプロセス情報が含まれる場合がある**ので提出前に内容や共有範囲を判断すること。
- `Process_CPU_1s.csv`：dllhost、Photos、ExplorerのCPU割合（全論理CPUを100%とした値）、PID、UTC、メモリ、スレッド数。
- `WIC_Decode_Events.txt`：WicHeic 1.1.30に既存のオプトインイベント。記録が空でもWICが動かなかった証明ではない。
- `CPU_WIC_Summary.txt`：PID/時刻が一致した復号とCPU計測の照合。
- `Capture_Manifest.txt`：ETLのSHA256、時間帯、計測条件。

**WPRはメモリモード**で60秒に制限（無制限ファイルモードは使用しない）。出力先の空き容量1GiBを確認する。WPRに失敗した場合は「成功した」と偽らずエラー終了。正常時はWPR停止とWIC診断レジストリの解除を実行する。強制停止でWPR記録が残った場合、自分の記録であることを確認してから管理者コマンドで`wpr -stop`または`wpr -cancel`を実施する。既存の別のWPRセッションは触らないこと。

## この採取で判断できること
- `dllhost.exe`とPhotos本体のどちらに高CPUが集中しているか。
- CPUが高い時間帯に`wic_heic.dll`や`msheif.dll`など、どのモジュールのスタックが多いか。
- 同時WIC復号の回数・タイミングとCPUピークの関係、CPU競合・待機の可能性。
- 将来、解析ツールやシンボルを追加しても**同じETLを再利用**できる。

## 判断できないこと・補完の扱い
- 一回の計測だけで、原因の全てや修正版が何%速くなるかは保証しない。
- OSやドライバー内部の関数名、静的リンクされたコーデックの関数名には対応するシンボルが必要なことがある。**シンボル取得・解釈は開発側の作業**であり、原則として再採取を依頼しない。
- 標準フォトの画面が崩れる原因を、CPUスタックだけで確定するものではない。
- ETLには個人情報が入る可能性があるため、WICテキストログと異なり、自動的な匿名化は保証できない。無理に提出せず、懸念があれば事前に共有範囲を検討する。

CI成功はWindows Server 2022/PowerShell 5.1の動作検証を意味し、Windows 10のGUIを実際に操作した検証ではない。

## 公開資料
- Microsoft WPR CLI: https://learn.microsoft.com/ja-jp/windows-hardware/test/wpt/wpr-command-line-options
- Microsoft CPU Sampling: https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis
- Microsoft Windows Support WPR: https://jpwinsup.github.io/blog/2024/01/05/Performance/SystemResource/Whatswpr/
