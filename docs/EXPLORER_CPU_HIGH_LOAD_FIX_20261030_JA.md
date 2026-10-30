# Windows10 Explorer HEIC CPU高負荷対策（2026-10-30）

## 最優先のユーザー要求

**最重要課題は高いCPU負荷の改善。** HEICサムネイルの表示時間短縮は副次的な指標であり、表示が速いだけでCPU使用率が高い状態を改善したと評価してはいけない。GPU性能なし・2物理4論理CPUのWindows10を対象として、既存フォト機能・画像の無改変・既存ShellExハンドラー復元を守る。

## 実機で検証済みの根拠（既存WPR/ETLを再利用）

- 2026-10-29に記録した実機WPR/ETL（個人ファイル名を含み得るためGitHubにアップロードしない）のCOM Surrogate PID2712のCPU sampled IP 153,543件のうち、113,221件（73.739%）は静的libheif/libde265を含む自作 wic_heic.dll の命令アドレス内だった。
- 141件の12MP主画像フル復号が同じプロセスで観測され、埋込サムネイルGetThumbnail使用は観測できない。Scanline CopyPixelsの多さだけで重複復号と断定しない。
- 4論理CPUを持つ実機で、既存のHEVC codec 4then0制御は1枚目の高解像度画像に4ワーカーを指定する。0は無処理やゼロCPUではなく、libheif/libde265既定値。libheif v1.23.7のlibde265 pluginでは、0指定が実効1 codec workerへ解決される実装。
- grid式HEICではHEVC workerとは別にlibheifのタイルデコード並列がある。libheif 1.23.3以前で報告された並列grid問題のアドバイザリでは、default max tile threads=4とされる。1.23.7ではその既知脆弱性の修正は存在するが、CPU負荷を減らすという観点では並列度はなお独立に考える必要がある。

## 今回作成した専用研究ブランチ

experiment/explorer-cpu-budget-20261030

- GitHub Actionsでのみ使用する環境変数 WIC_HEIC_THUMB_LAB_WORKERS に新たに「1」を受理する機能を追加。**製品版のワーカーポリシーは変更しない**。
- 人工4032x3024・8bit HEICを0,1,2,4,autoの5条件で、同一CI runnerで交互に5試行ずつ計測。取得するのは復号から256pxサムネイルを作る壁時計時間と、GetProcessTimesによるプロセスUser+Kernel CPU-ms。画素は全解像度・サムネイルともexact RGB parityを維持。
- GitHub Actionsの専用workflow explorer-cpu-budget-ci.ymlがMSIなし／人の写真なし／使い捨てWindows runnerのみに限定して試験する。
- 計測値が出るまで「1 workerでCPU消費減」「2 workerは改善確定」と主張しない。2つ以上のCPUコアで動けばプロセスCPU-msは壁時計時間より大きくなりうる。
- **重要**：hvc1単純画像のA/Bだけで、実iPhoneのgrid/gain map/ICC構成のCOM Surrogate負荷低減を証明したことにはならない。gridのタイル並列も別途CPU・出力一致の検証対象にする。

## 必須リリースゲート

1. 実CPU-ms・ピーク負荷・負荷継続時間の改善を実機2C/4T条件で確認すること。壁時計時間の大幅悪化やWindowsフォトUIの停滞は不採用。
2. CPUを制限した場合もExif/irot/ICC、grid/alpha/HDR/10/12bit、サムネイル向き、保存動作が変化しないこと。
3. COM Surrogateのスローダウンが原因でExplorerの重複リクエストが増えていないこと。独立WICフレーム同時復号、Photosとdllhostのプロセス間競合は区別する。
4. 他社ShellEx・WIC登録を破壊せず、導入/修復/ロールバック/アンインストールで前状態に復元可能にすること。
5. 実機ファイル・回収済IMG_4074原本は絶対に上書きしない。CIは人工HEICのみ。追加WPRは不要。
6. CPU改善と表示時間・ユーザー体感の双方を満たして初めて1.1.31 MSI候補。小刻みなMSI乱発は避ける。

## 参考一次資料

- Microsoft GetProcessTimes: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes
- libheif v1.23.7: https://github.com/strukturag/libheif
- libheif tile threads: https://github.com/strukturag/libheif/security/advisories/GHSA-prgh-72vc-3xmc
- libde265: https://github.com/strukturag/libde265
- 実機CPU IP分析: [WIN10_HEIC_ETL_CPU_20261029_PID2712_JA.md](WIN10_HEIC_ETL_CPU_20261029_PID2712_JA.md)
