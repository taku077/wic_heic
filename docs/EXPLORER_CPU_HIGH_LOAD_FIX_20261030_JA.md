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


## 2026-10-30 CIで確認した実装・結果

**CPU研究実装**：Shellプロセス専用2ワーカー選択、従来Photos・他ホストと分離。4論理CPUのdllhost.exeのみに適用。Exif/ICC/高ビット深度/アルファなど従来Selectの適格条件を維持し、同時復号中の2枚目以降は旧0(default)に戻る。

**GitHub Actions [#41011035938](https://github.com/taku077/wic_heic/actions/runs/41011035938) SUCCESS**。人工12MP 8bit画像、各5試行中央値：

| codec workers | wall ms | process CPU ms | CPUms/wall |
| --- | ---: | ---: | ---: |
| 0（codec default） | 1432.736 | 1421.875 | 0.992 |
| 1 | 1436.221 | 1437.500 | 1.001 |
| 2 | **859.691** | **1609.375** | **1.872** |
| 4（旧Shell選択） | **616.639** | **2140.625** | **3.471** |
| auto（旧Shell選択） | 616.367 | 2140.625 | 3.473 |

単純12MPで4→2へ変更したとき、**プロセスCPU時間約24.8%削減、平均CPU同時稼働コア相当約46.1%削減、処理時間約39.4%増加**。これはWindows10実機CPU最大62%が何%に下がるかの測定ではない。Photos側2ワーカーと従来他ホスト4ワーカーのまま。CLI人工25画像のWIC全画素と256px RGB厳密一致も確認。

**CIマーカー**：SHELL_LOW_CPU_WORKER_POLICY_PASS（7ケース）、SHELL_LOW_CPU_RGB_PARITY_PASS、PHOTOS_UI_2WORKER_RGB_PARITY_PASS。自動選択ログでも workers=2/photos=0/shell=1、workers=2/photos=1/shell=0、workers=4/photos=0/shell=0。

**未証明**：ユーザーの本物のdllhost.exeでのCPU使用率低下、gridでのlibheif独立4タイル並列による高CPU、複数フレーム同時の実機体感、Windows10 2C/4TとServer 2022 runnerの差。実機配布は保留し、他形式への影響、MSIロールバックを別途確認する。

CI前段で混在画像fixture不足のため一度失敗したのでCPU専用テストを分離し、既存の混在形式の回帰試験は維持。製品不具合の修正ではなくテスト環境不足の分離。


## 2026-10-30 新規：grid画像の独立CPU計測（実画像原本不使用）

- libheif v1.23.7にはHEVC内部codec workerとは独立した、コンテキスト単位のheif_context_set_max_decoding_threads APIが存在する。0はタイルを呼び出しスレッドで順次復号、1は別背景スレッド1本。既定は4。これはcodec workerの0とは異なる。
- wictest/isolated_shell/SyntheticGridCpuAB.cpp にWindows CI専用の48タイルHEVC grid合成器を作成。画像は使い捨てrunnerのRUNNER_TEMPにのみ生成され、試験後削除。C APIで完全に独立したコンテキストを作成し、タイル並列0/1/2/4およびHEVC codec worker1/2/4を交互に比較。全てのテストでRGB完全一致。
- libheif v1.23.7のheif_tiling.hのencode_grid引数名の行・列順と実装heif_tiling.cc側の実際の解釈が逆であることに注意し、生成したgridの主画像サイズを読み戻して確認する。
- [#41012547990](https://github.com/taku077/wic_heic/actions/runs/41012547990)：3072×2304の48タイル、codec worker=1でgrid4→2、CPU時間62.50→78.12msと増えた。小さい人工画像では「並列数を減らせば必ずCPU時間削減」は成立しない。
- [#41012784412](https://github.com/taku077/wic_heic/actions/runs/41012784412)：4096×3072の12.6MP・48タイル、codec worker=1でgrid4→2、CPU時間218.75→187.50ms（約14.3%減）、壁時計時間90.92→106.33ms（約16.9%増）、RGB厳密一致。各3回中央値。
- [#41013310048](https://github.com/taku077/wic_heic/actions/runs/41013310048)：12.6MP gridのコンビネーションを各7回。grid4/codec4はwall118.06ms・CPU312.50ms、grid4/codec2は114.09ms・296.88ms、grid2/codec2は119.27ms・296.88ms、grid2/codec1は126.35ms・203.12ms、grid1/codec2は148.43ms・203.12ms。すべてRGB厳密一致。grid2/codec1はこの1回のCIではgrid4/codec4よりCPU時間約35%減少、壁時計約7%増加した。ただし単一runnerの小規模試験であり、別実行との数値の振れもあるため、採用確定ではない。

**重要な実装上の制限**：現在の自作WICはlibheif C++ラッパーheif::Contextを使用し、このラッパーにはCのheif_context_set_max_decoding_threadsに対応する公開setterがない。コンテキストのprivate shared_ptrを不正に取得したり、libheifのグローバル設定を無差別に変更したりしない。C APIでのコンテキスト管理への安全な移行または検証済みの追加ラッパーがない限り、grid上限を**製品経路へ組み込んだとは主張しない**。

次の判断：単純12MPのHEVC codec 4→2は既に実装・CI合格。grid2＋codec1は有望な研究候補だが、追加A/Bと実際のWIC/Shell上で画素・プロファイル・回転・CPU・時間が妥当と確認できるまで試験専用とする。1.1.31 MSIはリリースしない。
