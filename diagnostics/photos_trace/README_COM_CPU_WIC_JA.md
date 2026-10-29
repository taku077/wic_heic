# 1.1.30 COM CPU + WIC PID / time matching diagnostic (Windows 10)

**本ツールはインストール不要です。MSIやフォトの設定を変更しません。画像ファイルも開いたり保存したりしません。**

## 利用方法

1. ZIPをローカルドライブ（例：デスクトップ）に展開します。フォルダ名は変更しても構いません。
2. `01_Start_CPU_WIC_Diagnostic.cmd` をダブルクリックします。管理者権限は不要です。90秒間だけCPUと自作WICの数値ログを同時に収集します。
3. 記録中にWindows標準フォトで**人工HEICのコピー**を開き、重い操作を再現します。90秒後に停止します。
4. 同じフォルダに生成された `COM_CPU_WIC_Result_日時.zip` を共有してください。

## 記録するもの

- `Process_CPU_1s.csv`: Photos / dllhost / explorerのPID、UTC、全CPU比、メモリ、スレッド数。サンプリング間隔は約0.85～1秒です。
- `WIC_Decode_Events.txt`: WICのPID・イベント・画像寸法・復号所要時間・UTC。ログが空でも正常に記録できなかった可能性があります。
- `CPU_WIC_Summary.txt`: PID別CPUピークとWIC復号発生時刻の照合。**同一PID・同時刻は原因証明ではありません。**

撮影画像の中身・ファイル名・パス・EXIF・画素・ウィンドウタイトル・キー入力は採取しません。ただしプロセス名、PID、動作時刻、画像寸法等の数値は含まれるため、共有前に確認してください。

## 安全策

既存の `PhotosTrace_Start.ps1` と `PhotosTrace_Stop.ps1` をそのまま再利用し、HKCUの自分が作成した診断用設定だけを解除します。他のトレースが設定済みなら開始しません。診断を強制終了した場合、`PhotosTrace_Stop.ps1` を実行してください。記録自体は最大20分で自動的に無効になります。再起動・レジストリの優先度変更・サムネイルキャッシュ全削除は不要です。

## 解析の限界と次の段階

フォトだけでなくCOM SurrogateでCPUを消費することが既存の2026-10-29の実機ZIPで判明しています。今回のPID同時観測でWIC復号とCPU上昇が連動しているかを調べます。モジュール単位のCPU帰属が不明な場合のみ、WPRで短時間のCPUスタック採取を検討します。**WPRはこのキットから自動起動しません。**

参考資料：Microsoft WPR/WPA https://learn.microsoft.com/en-us/troubleshoot/windows-server/support-tools/support-tools-xperf-wpa-wpr 、WPR CLI https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options
