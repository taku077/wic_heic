# HEIC / WIC 長期開発・常時更新引継ぎ台帳（正本）

**台帳作成日:** 2026-10-30 JST  
**対象シリーズ:** ChatGPT「HEIC表示方式比較 1～15」および今後の続編  
**正本の固定URL:** https://github.com/taku077/wic_heic/blob/master/HEIC_PROJECT_LIVE_HANDOFF_JA.md  
**リポジトリ:** https://github.com/taku077/wic_heic

> **このファイルがチャット移行用の唯一の入口。** 新チャットではまず本ファイルを取得し、**ここに書いたSHAやCIはスナップショットなので、その時点でGitHubの各ブランチHEAD・CI・直近の変更を再取得**する。既存研究・失敗を繰り返さない。作業中も重要な決定・実装・CI・実機評価のたびにこのファイルを更新する。更新不能時は「更新できなかった」と明示する。過去15スレッドの全メッセージ・添付がこのファイルだけで完全復元できるとは主張しない。

## 【2026-10-30 最新・最優先】大容量ETLはユーザーPCに残っていない

**ユーザーから明示確認：以前の `CPU_Stacks.etl` は削除済みで、ローカルに存在しない。存在する前提で再選択を求めたり、長期保存を要求したりしない。一方、必要性がある実機検証は通常どおり実施可能。過去の診断ツールが必要なら、その都度こちらが使える形で再提供し、文中にダウンロードリンクを添付する。**

- 以前の提案「既存ETLを選んで集計」はユーザーの保管事情を確認せずに決めた誤り。ユーザーには多GBの診断原本を保管し続ける義務はない。将来使うか不明のファイルを保存している前提を置くことも禁止。
- 既に取得した診断結果（v1.1.33 CPU100%、dllhost中心の復号71回中65回、過去ETLのdllhost CPU命令サンプルの自作WIC範囲73.739%、新並列ポリシー適用等）を再利用し、コード・CI・人工HEICで先に進む。実機で未判明のCPU帰属は**不明**とし、元ETLがないのに「解析できる」と言わない。
- **必要性がある場合はユーザーWindows10実機で検証を実施してよい。** 過去のツールが必要な場合は、現在入手可能なビルドを再検証・必要に応じ再生成し、使えるダウンロードリンクを説明文にその都度添付する。ユーザーに過去のZIPや実行ファイルを保存し続けることを求めない。追加測定では目的・想定生成量・終了後の削除/保持方針を説明。小容量の匿名化済みレポートを優先するが、必要な場合の短時間WPRや詳細診断を一律禁止しない。
- WindowsのCPU分析で必要に応じ、OSのプロセスCPUカウンター／`GetProcessTimes`等を使う短時間・低容量のその場集計を優先。ただしCPU％のみではHEVC関数別・DPC/ISR等の根本原因までは特定できない。結果がなければ仮説を事実として扱わない。
- 本文の過去ログに残る「既存ETLを選ぶ」という記述は**歴史的経緯であり現在の作業指示ではない**。この最優先節が上書きする。

**運用上の訂正：実機テストが必要になれば実施する。以前使った解析プログラム／ツールは、その時点でこちらが再配布・ダウンロードリンクを添付する。** 不要な永続保存をユーザーに依頼しない。再提供可能な過去のツールと、既に消去された元のETLデータは区別し、後者を復元可能だと断定しない。


> **2026-10-30 22:43 JST 更新（以下の過去記述より優先）:** 先にユーザーが提出した2026-10-30 20:30採取のWPR診断ZIPはChatGPTライブラリで再発見・取得済み。実機で生成されたテキスト報告 `CPU_AllProcesses_20261030_224307_303.txt` も提出済み・受領確認済み。ユーザーに旧ファイルの長期保存・再録画・再提出を求めない。現在、報告の64.31%が `(unknown)`、うち54.72%がPID不明のため、**全プロセスCPU帰属が確定したとは言わない**。次工程は**解析コードのプロセス／スレッド帰属の調査**であり、ユーザーの提出方法は正常。古い「元ETLがないので解析できない」は失効。

> **2026-10-30 最終追記：** Windows10実機から提出済みの `CPU_AllProcesses_20261030_224307_303.txt` では全スレッド付きサンプル456,016件中293,247件（64.31%）が名前不明。既存ETLを再取得して（追加録画不要）解析方法を見直し、研究ブランチ `investigate/win10-100cpu-after-1133-20261030` HEAD `60c55f718343fbe7661fa6f64edb4a6e284a95a0` で**ETL終了後にrundownで遅延帰属する保守的修正**、CI #41067460713 SUCCESS。**同じ実機ETLでの改善度は未確認、CPU100%も未解決**。次の検証に使う過去のETL ZIPと新しい解析ツールZIPを必要に応じてこちらが再提供する。ユーザーに古いファイルの保存継続を要求しない。

> **2026-10-30 23時台の最新状態（過去記述に優先）:** 新しい実機ETLレポート CPU_AllProcesses_20261030_230208_049.txt は正常。PID不明は249,525件（54.72%）→9,888件（2.17%）へ改善したが、プロセス名不明は293,247件（64.31%）で不変。既存提出ZIPのProcess_CPU_1s.csvとの照合により PID936=explorer 70,212件、PID10084=Photos 5,034件を識別。未識別は同一PID再利用なしの前提付きで218,001件（47.81%）。PID11664の87,383件（19.16%）は依然不明。ProcessEnd/DCStopからの名前収集を追加、Actions #41069110425 SUCCESS。**実機での追加改善率は未確認、CPU100%は未解決、新規MSIは保留。**

> **2026-10-30 23時台のShell改善成果（第18節を優先確認）:** CPU全体100%は未解決。実験ブランチ `experiment/shell-com-focused-20261030` 最新HEAD `fba2d3e92d8157187ac58664a493d3c22362f4ec`。複雑HEICの事前判定を64KiBまでで早期拒否する研究コードを追加し、過去のiPhone系HEIC（読み取り専用のメタ構造調査）でgrid/dimg/auxlがファイル先頭にあることを確認。人工131072Bで先頭65536Bだけの読込みをCI検証。**新たな正規8×6=48タイル+埋込HEVCサムネイルの人工HEICを生成**し、登録済みShellハンドラーでフルWICフォールバック、RGB MAE0.00、元の関連付け復元を検証。Actions #41071529559 SUCCESS。単純人工HEICの埋込経路では既定93.5ms→12.0ms（各8枚の中央値）を維持。**この速度値は複雑gridへの適用効果ではない**。製品MSIには未統合。詳細は第18節、個人画像／ETLは公開していない。

> **2026-10-30 23:50 JST 最新研究結果（第19節）**：研究ブランチ `experiment/shell-com-focused-20261030` HEAD `aeeba5d31beead1aab4a9660f7e62709da674382`。**正規8×6=48タイルgrid+埋込320×240サムネイル**を人工生成し、自作WICでサムネイルを先に復号してから基準の4032×3024主画像を復号、256×192 RGBサンプル比較MAE=**0.038/255**（20階調超0%）。**WIC診断トレースから埋込復号1／フル主画像復号1**を確認。単回CI計測はembedded=0ms、full=109msだが0msはタイマー分解能に依存、Windows10実機速度やCPU100%解消の主張ではない。Actions **#41075151932 SUCCESS**。**まだgridを高速経路へ許可していない、製品MSI変更なし。** リポジトリのlibheifはv1.23.7（CPU枯渇のセキュリティ修正済み）。次はICC/HDR/Exif/alpha等を誤って高速化しない厳密なgrid資格判定、さらに人工複合HEICの画質一致を検証。第19節を参照。

## 0. 新しいチャット用の最短開始指示

「GitHub `taku077/wic_heic` の標準ブランチ `master` にある `HEIC_PROJECT_LIVE_HANDOFF_JA.md` を読み、記載の本線・研究ブランチの最新HEAD/CI/変更点と照合して『HEIC表示方式比較』開発を中断地点から継続。過去の研究を繰り返さず、Windows10実機のCPU100%問題を最優先。進捗と検証結果をこの台帳へ随時更新。Microsoft Learn以外のGitHub・libheif・libde265・libvips・FFmpeg・GPAC・ExifTool・Exiv2・Intel等も横断調査。私の写真原本・個人ログは公開しない。」

## 1. 目的・判定基準・禁止事項（固定）

- **最優先：** Windows 10標準「フォト」を開くとPC全体CPU100%が続き、操作が重くなる問題の軽減。Explorer/COM Surrogateとの競合も対象。**単一デコードの速度だけ上がってもCPU問題が残れば未解決**。
- **併走目標：** Windows標準ExplorerのHEIC初回サムネイルとフォト表示を実用的な速度に近づけ、回転・保存・ICC/Exif/アルファ/HDR/グリッド画像を破壊しない。JPEG並みが理想だが理論値だけ追い続けず現実的な妥協点を探す。
- **試験環境：** Windows 10 Pro 22H2 x64／第7世代Core i5系（既存調査ではi5-7200U、2C/4T）／Intel HD Graphics 620／RAM16GB／約1,894枚混在フォルダ。Windows Server 2022 Actionsでの成功はWin10実機成功と区別。
- **原本保護：** ユーザーのHEIC/JPEG/PNG等の原本を**書換え・削除・上書きしない**。編集・保存の検証は人工HEICまたは専用コピーのみ。過去に復元した `IMG_4074` は**再実験禁止**。`IMG_4425`も原本での変更禁止。
- WPR/ETL、写真、ユーザーPCのファイルパス、加工前生ログ、個人情報は**公開GitHubへ投稿しない**。台帳には匿名化・集計済み結果と検証済みURLのみ。
- 既存WIC/ShellEx登録、他社ハンドラー（CopyTrans等）の優先順位を強引に変更・無効化せず、MSIのアンインストール/復旧は破壊なし・元状態へのロールバックを必須とする。
- 毎回MSIを発行しない。**コード変更→赤/緑回帰試験→CI全PASS→意味ある性能/CPU改善→慎重なMSI候補→ユーザー実機評価**。小刻みな見かけ改善だけでは公開しない。
- ベンチは**同じ入力・同じ条件**でCPU-ms/画像、CPU使用率の持続、wall-ms、フル復号回数、表示画質、フォト応答、PC全体CPUを区別。異なるセッションの単純比較は因果効果と呼ばない。

## 2. 2026-10-30 最終確認した現状・GitHub HEAD（更新時には再照合）

|役割|ブランチ／記録時のHEAD|意味|
|---|---|---|
|安全性本線|`fix/grid-rotation-lossless-safety-20261028` / `724d0f44c7d42556b959a6364092be17fd8e4e2f`|無再圧縮回転・未認証保存拒否・ICC等の基準を保持。Actions #40856125988 SUCCESS。|
|Shell埋込thumb研究|`experiment/shell-com-focused-20261030` / `0027cfcec69d8b1966bc349cde624a85d04d363a`|登録済Shell経路、Exif/irot/ICC/alpha/PQ/HLG、画質閾値、第三者ハンドラー保護を人工CI検証。**本番MSIには未統合**。|
|Explorer grid CPU研究|`experiment/shell-cpu-work-reduction-20261030` / `2c26f94b912b5038d4b2ddc740348d4a0df2f735`|Shell CPU計測専用実験。|
|v1.1.32比較候補|`candidate/explorer-grid-cpu-1132-win10` / `2c7878a2671e9616dd32ae72a707c36b99d8fcca`|COM Surrogateでタイル2/codec1の評価。|
|**v1.1.33実機評価候補**|`candidate/photos-grid-cpu-1133-win10` / `0580b959a684cc63a305aa5c24478ddb2a7359a9`|Photosの48タイルgridを4/2→2/1に制限。Actions #41046215352 SUCCESS。**実機CPU100%は未解決**。|
|**直近作業の先端**|`investigate/win10-100cpu-after-1133-20261030` / `9afd5576890c545cddbd732ce89485d6a5c81d73`|既存WPR ETLから全プロセス・同名プロセスPID別のCPUサンプルを集計するオフライン診断ツール。Actions #41059547959 **SUCCESS**。PID別集計に加えて10秒ごとの全サンプル・ThreadID不明数を表示。実ユーザーETLでの解析結果は**未取得／未確認**。|

**重要**：メインの作業先端は過去の `fix/photos-118-selfwic-incremental` や10月29日の引継ぎ記載SHAではない。研究ブランチと安全性本線を混同しない。新チャットでは `compare` と最新CIを確認してから変更を提案する。

## 3. 最新ユーザー実機報告：v1.1.33でもCPU100%（未解決）

出典（匿名化技術記録）：
https://github.com/taku077/wic_heic/blob/investigate/win10-100cpu-after-1133-20261030/docs/WIN10_1133_CPU100_NOT_RESOLVED_20261030_JA.md

- **2026-10-30 20:30～20:33 JSTの1.1.33実機WPR**は収集完了（WPR exit=0、ETL約1.50GB）。前の1.1.32 WPRは同日18:04～18:08。
- 1.1.33でPhotosの `DecoderWorkerSelect source=photos operation=1`、`DecoderGridWorkerBudget source=grid operation=1` 等を確認。**修正コード自体は有効化されていた**。旧版が読み込まれたと決めつけない。
- 同一セッション内のログ計数：1.1.33のWIC全解像度復号71回、そのうちPhotos6、dllhost65。旧1.1.32 WPRは総178（Photos24、dllhost154）。**収録時間・操作・画像・キャッシュが異なるため減少をバージョン改善率としない**。
- 1.1.33の1秒CPUサンプラー（限定3プロセス）ではPhotos最大62%、dllhost合計45%、Explorer最大27%、この3系統の同一秒合計最大77%。**サンプラーはSystemやその他のプロセス等を網羅していない**のでTask ManagerのPC全体100%との不一致を矛盾としない。CPU指標の定義も同一でない。
- 今後の軸は(1)既存ETLの全プロセス・スタックCPU帰属 (2)不要なShell全解像度復号の削減 (3)可能ならIntel GPUでのHEVC HW復号の**読み取り専用実現性評価**。CPUデコードの並列数だけをいじり続けない。
- **再度の長時間WPR録画をユーザーへ要求しない。** 既に提供されたデータを最優先で再利用。生ETLをGitHubやCIへ送信しない。
- 現状**v1.1.34 MSIリリースは保留**。

## 4. 既存研究成果の不変台帳（同じ研究をやり直さない）

### 保存・回転・品質

- v1.1.18はHEVC警告解除・回転UI操作が可能な旧比較対象だが、回転保存で画像崩れがあり「正常版」ではない。
- Exif OrientationとHEIF `irot` の同時存在（例：IMG_3916、Exif=3・irot=2）は複合処理が重要。人工HEICで無再圧縮90/180/270度と4連続回転の画素一致試験あり。ICC/Exif保持、未認証RGB由来の安全な保存拒否は維持する。
- `IMG_4425` の回転後サムネイル消失／再度回転保存失敗の根本原因は未確定。48タイルgrid＋gain map＋ICC＋Exif＋埋込thumbなど複合構造。libheif 1.23.7導入は安全性改善だが特定実機不具合の因果証明ではない。
- 1894枚混在フォルダでの縮小・回転連打によるPhotos横長固着／一時画像消失・向きの更新遅延の再現報告あり。フォト再起動で一時回復する例もあり、コーデック単独の断定は禁物。

### CPU解析済み事項

- 2026-10-29 WPRオフラインETL CPU IP分析済み：dllhost PID2712の153,543 CPUサンプル中 `wic_heic.dll` のアドレス領域113,221（73.739%）。dllhost高負荷とWIC経路は根拠がある。ただし静的リンクされたlibheif/libde265と自作ラッパー関数の内訳は別途PDB等の検証が必要。
- 同PIDで `DecoderGetFrame=141`・`DecoderFullDecode=141`、埋込thumb取得イベント0。**CopyPixels行単位呼び出し多数を重複フル復号と誤認しない**。
- 4論理CPUの従来4then0（別画像を同プロセスで並行する場合はcodec既定0）の実装済み。**codec worker=0 は停止ではない**。libheifのgrid tile thread=0とは別。
- 固定2 codec workerの単純な案は単一画像の遅延が大きく本線不採用。条件付きPhotos/Shell制限やgridタイル制限とは別の試験。
- Windows Server 2022人工12MP/48tileにおけるv1.1.33条件の安定化した7組×8枚比較：Photos tile4/codec2 **296.88 CPU-ms/画像、110.49 wall-ms** → tile2/codec1 **207.03 CPU-ms、120.85 wall-ms**。約30.3% CPU削減・9.4%遅延。**実機CPU100%改善は実証していない**。
- 1.1.31→1.1.32の異なる実機セッションのdllhost平均CPU43.05%→21.51%、最大87%→46%という測定あり。ただし表示画像／処理速度／キャッシュ／録画時間が異なり、単純な改善率とはしない。

### Shellサムネイル高速経路（先行成果を再利用）

- 既存WIC `GetThumbnail` では人工12MP + 埋込320×240が主画像フル復号0で取得可能。しかし標準 `IThumbnailCache::GetThumbnail` はこのメソッドを呼ばず全解像度復号する場合があり、**GetThumbnail単独改善で解決とはならない**。
- `IThumbnailProvider + IInitializeWithStream` のShell独立試作で、単純人工12MP/埋込320×240の場合、登録済Shell経路8件中央値が従来約103ms→試作約12ms、フル復号なし（Windows Server 2022 CI）。性能・画質の一例であり実画像・Windows10では未証明。
- 2026-10-30のShell研究ではICC、Exif/irot、alpha、PQ/HLG nclx、>8bit、grid/gain map、非適格画像を**安全な通常WICフォールバック**へ。合成パターンの縮小RGB MAE0.04/255、強い差分0%のゲートも合格。ただし複合iPhone画像には高速経路が適用できない可能性が高く、全フォルダの適格率未測定。
- 他社CopyTrans等の登録値保護・復元はCIの模擬/隔離試験あり。本番MSIへのShellEx切替は未完了。**他社ハンドラーを壊してまで採用しない**。
- Shell高速thumb研究とPhotos CPU制御は別経路。直近ユーザーのWindows10では本番Shell高速ハンドラーは未搭載。

### 他の非採用・重複禁止

- WIC登録優先順位の強制変更、他社デコーダーの安易な無効化、キャッシュ全削除での性能偽装、毎回のMSI生成、単一スレッド/2ワーカー固定の無条件採用を繰り返さない。
- 旧v0.4.0初回HEICサムネイルの2/4 **codec内部ワーカー**と複数ファイルのShell画像並列は別。古いCI warm-cacheの一括速度を実Explorer初回表示の証明としない。
- HEIC Manager（独立WPFビューアー／サムネイル）の別スレッド開発とWindows標準フォトWIC開発を混同しない。競合するシェル拡張登録に注意。

## 5. 現在の成果物・未完了ゲート（新チャット開始時に確認）

**実験先端**：`investigate/win10-100cpu-after-1133-20261030`（上記SHA参照）に `tools/etl_cpu_top/` の **既存ETL読み取り専用 全プロセス＋PID別CPU集計ツール**を追加済み。Windows .NET Framework 4.8／TraceEventを利用。専用Actions #41057776963 **SUCCESS**（短いCI合成WPR ETLを読み、同名 `dllhost.exe` の2 PIDを区別する回帰試験、上書き禁止、プライバシー・基本動作を確認）。**ユーザーの2026-10-30 ETLそのものに対する実行結果はまだない**。

ツール案内（実験ブランチ）:
https://github.com/taku077/wic_heic/blob/investigate/win10-100cpu-after-1133-20261030/tools/etl_cpu_top/README_JA.md

- この解析はCPU100%を治すものではなく、未集計プロセス・10秒区間を特定して次の改修対象を絞る。実ETL実行未確認の段階で成功と説明しない。
- まず既存レポートと現在のCI成果物、同じETLの再解析可能性を調べる。ユーザーの操作は最小限にし、新たな大容量WPR収録は行わない。
- 次にShell全解像度復号の削減が実複合grid/ICC/HDR画像で安全に可能か検討。ハードウェアHEVC復号はIntel HD620の機能があるが、libheif/libde265のソフトウェア経路に自動適用されない。Windows Media FoundationハードウェアMFT列挙・人工画像で安全に可否を調べ、製品実装を先走らない。
- 何を試しても実機体感・PC全体CPUが改善しない場合は、費用対効果の観点で採用保留／旧版維持とする。

## 6. 一次資料と横断調査・採用基準

**一サイトだけを検索しない。** 新規仮説ごとに、Microsoft Learn、`strukturag/libheif` のリリース・実コード・issue、`strukturag/libde265`、`libvips/libvips` のHEIFロード/サムネイル実装、FFmpeg、GPAC MP4Box、ExifTool、Exiv2、Intelハードウェア資料、他のWIC Shell OSS、GitHub issue/PR、必要に応じStack Overflow等を突合する。GitHub・実装・公的仕様を優先。資料の単なる列挙ではなく **対象コード箇所・対応バージョン・採否・性能測定・未検証点**に反映する。

- libheif v1.23.7: https://github.com/strukturag/libheif/releases/tag/v1.23.7 （2026-10-26のセキュリティ/CPU使い切り・グリッド並列の不具合修正。通常のフォトCPU100%の直接原因という証拠ではない）
- libvipsのembedded HEIF thumbnail活用方針: https://github.com/libvips/libvips/blob/master/libvips/resample/thumbnail.c
- WICとExplorer thumbnail統合: https://learn.microsoft.com/en-us/windows/win32/wic/-wic-integrationregentries
- Shell thumbnail provider: https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers
- WPR sampled CPU: https://learn.microsoft.com/en-us/troubleshoot/windows-server/support-tools/support-tools-xperf-wpa-wpr
- Intel 7th gen HD620 HEVC HW機能: https://www.intel.com/content/www/us/en/developer/articles/technical/encode-and-decode-capabilities-for-7th-generation-intel-core-processors-and-newer.html
- Windows MFT HW enum: https://learn.microsoft.com/en-us/windows/win32/api/mfapi/nf-mfapi-mftenumex
- libde265: https://github.com/strukturag/libde265
- GPAC: https://github.com/gpac/gpac
- FFmpeg: https://ffmpeg.org/
- ExifTool: https://exiftool.org/
- Exiv2: https://exiv2.org/

## 7. 詳細エビデンスへの道筋（本台帳だけで省略した詳細を辿る）

- 2026-10-29旧総合引継ぎ：過去のユーザーの添付 `HEIC_WIC_COMPLETE_HANDOFF_20261029_JA.md`（ChatGPTライブラリ）。**当時のHEADは古い**ので本台帳が優先。
- 2026-10-30 1～14差分：過去の添付 `HEIC_WIC_HANDOFF_SERIES_1-14_UPDATED_20261030.md`。Shell研究初期を含む。**現在の研究ブランチより古い**。
- v1.1.33未解決：`docs/WIN10_1133_CPU100_NOT_RESOLVED_20261030_JA.md`（`investigate/win10-100cpu-after-1133-20261030`）。
- v1.1.33 CIベンチ：`docs/WIN10_PHOTOS_GRID_CPU_1133_RC_JA.md`（同ブランチ）。
- v1.1.32実機ベンチ：`docs/WIN10_HEIC_CPU_WPR_20261030_180447_1132_JA.md`（同ブランチ）。
- 初回CPU IP帰属：`docs/WIN10_HEIC_ETL_CPU_20261029_PID2712_JA.md`（同ブランチ）。
- Shell埋込経路：`docs/EXPLORER_EMBEDDED_HEVC_PROVIDER_FEASIBILITY_20261030_JA.md`（Shell研究・調査ブランチ）。
- インストーラー復元：`docs/SHELL_1131_INSTALLER_RESTORE_GATE_20261030_JA.md`（Shell研究ブランチ）。
- 関連CI runは各文書内のリンクから確認。CI成功が機能の本番保証であると扱わない。

## 8. 台帳運用ルール（強制）

1. **作業開始時**：本台帳取得 → GitHub現HEAD/コミット差分/Actions・実装照合 → 最新ユーザー報告の再確認。古いSHAのまま開発しない。
2. **作業の節目ごと**：新しいコミット/CI URL・成功失敗・実測値/対象素材・安全性・新たな不具合・棄却案をこのファイルに上書き更新。**「いつか移行するときにまとめて」は禁止**。
3. 研究が失敗した場合も「理由と再実施不要条件」を書き、将来の再研究を抑制。
4. 利用者の「体感上変わらない」「CPU100%」は実機評価として優先し、CIの改善値だけで打ち消さない。
5. **masterにはこの台帳など文書のみを変更し、製品コードは用途別ブランチで作業。** 台帳がGitHubコミットを動かしてもコードブランチのHEADは動かさない。
6. 各更新後、実際に `master` からこのファイルを再読して保存を確認。更新不能ならその場で報告し、次回の取得前提にしない。
7. GitHub接続に失敗した場合、黙って古いスナップショットで続行せず、取得不能な点を明示して安全側に停止。
8. **本台帳はチャット全文・個人ETLの保管庫ではない。** 実機データは会話添付・ライブラリ等で厳密に管理し、GitHubには匿名化要約のみ。

---

**最終台帳編集:** 2026-10-30 JST。最終技術確認は上記ブランチHEAD / CI。次回は必ず現在値に更新する。

## 9. 2026-10-30 続編の実作業と横断調査の追加成果

### GitHubへ実際に反映した変更

- **固定台帳を `master` に新規作成：** 初回コミット `74f55bd5872b5118369e88ab25ba9b32211017f9`。以降は本ファイルを最新版に更新する。既存の作業コードブランチとは独立。
- **ETL CPU解析をPID別に詳細化：** `tools/etl_cpu_top/Program.cs` で「同じ名前の実行ファイルでもPIDごとにCPUサンプルを分ける」集計を追加。`dllhost.exe`、複数Windowsフォト、サービスホストの負荷発生元を調べやすくする。プロセス名の集計も残す。
- **回帰テスト：** `--self-test` が同名 `dllhost.exe` のPID 111と222を3サンプルで正しく別集計することを確認。既存WPRを使った小型のCI ETL読み取り、レポート上書き拒否、GUIスクリプトの構文検査も維持。
- **直近コードHEAD:** `1b3cf460436ad23e7a7cc39cff1c8fadef4b31a4`（`investigate/win10-100cpu-after-1133-20261030`）。
- **CI:** https://github.com/taku077/wic_heic/actions/runs/41057776963 **SUCCESS**（2026-10-30 21:46 JST頃）。これは **Windows Server 2022で合成短時間WPR ETLを処理できる**ことの確認であり、個人ETL解析と実機CPU100%解決の証拠ではない。
- **注意：** PIDは一つの記録で有効な一時的識別子であり、プロセス終了後は再利用される場合がある。同じPIDの一意なプロセス生涯を厳密に分離する機能は未実装。新しい解析ツールはCPUサンプル割合のみで、タスクマネージャーCPU Utilityや全CPU-msそのものではない。写真・ファイルパス・コマンドラインを出力しない。

### 今回の横断確認（実装判断に反映）

- MicrosoftのWPR CPU sampledの説明では、プロセス別／スレッド別集計を行う必要があり、サンプル数だけでは短いISR/DPCやCPU Utilityを完全に再現できない： https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis
- GitHub上のMicrosoft PerfView TraceEvent実装も `PerfInfoSample` とプロセス生涯／スレッド対応を参照： https://github.com/microsoft/perfview/blob/main/src/TraceEvent/Computers/TraceProcess.cs
- Intel 第7世代GPUにHEVCデコード機能は存在するが、現在のlibheif/libde265ソフト復号が自動でそれを使う証拠ではない： https://www.intel.com/content/www/us/en/support/articles/000055741/graphics/processor-graphics.html
- libvips HEIFは埋込サムネイルを利用する際も寸法・比率検査と元画像メタデータを考慮する： https://github.com/libvips/libvips/blob/master/libvips/foreign/heifload.c
- libheifのグリッドタイル並列とコーデック内部並列は別のCPU調整対象であり、0指定の意味も異なる： https://github.com/strukturag/libheif/security/advisories/GHSA-prgh-72vc-3xmc

**次の作業開始点：** 既存の2026-10-30 20:30 WPR `CPU_Stacks.etl` に対してPID別/10秒区間の解析結果を得て、高負荷がPhotos／dllhost／System／その他のどこに集中するかを特定する。**新しい長時間録画を要求しない。** 利用できない場合は未確認を維持し、同じ仮説の無限反復や無意味なMSI発行はしない。CPU100%問題は引き続き**未解決**。

## 10. 2026-10-30 22時台：ETLサンプル帰属の正確性改善とテスト結果（今回の新しい作業）

**作業ブランチ**：`investigate/win10-100cpu-after-1133-20261030`  
**HEAD**：`9afd5576890c545cddbd732ce89485d6a5c81d73`  
**CI**： https://github.com/taku077/wic_heic/actions/runs/41059547959 **SUCCESS** （Windows Server 2022、人工短時間WPR ETL）  
**製品コード・MSI**：変更なし。CPU100%は**実機未解決**。

### 今回の修正

- 旧解析ツールは `PerfInfoSample` のThreadID<=0サンプルを10秒窓の集計前に捨てていた。このため、サンプルがどれだけ帰属不能だったか分からなかった。**全受信サンプルを先に時間窓へ収容し、ThreadID付きサンプル/ThreadID不明または0のサンプルの双方を出力**するよう変更。
- プロセスごとの比率は「**ThreadID付きCPUサンプルの中での割合**」であって、タスクマネージャーの「PC全体CPU％」とは違う。**ThreadID=0の件数を全アイドル時間の直接測定と呼ばない**。ETWの短いDPC/ISR／欠落イベントの制約も明記。
- 先に導入済みのPID別区別と報告ファイル上書き防止は維持。 `--self-test` では2つの同名dllhost PIDとThreadID不明イベントの件数を検証。
- CIでは出力ラベルを `NonIdle` から `WithThreadID` へ変更した際、**初回コミット `a172b0c` の旧テストが期待ラベルを取り違えてFAIL**。次のコミット `c931848` で新フォーマットの必須項目を判定するテストに修正し、最新コミットのCIでSUCCESS。これは**ETLパーサの解析失敗ではなくテストの期待出力不一致**。同じ失敗を再調査・再実装しない。
- 合成CI検証の成果物（有効期限7日のGitHub Actions artifact）：`WicHeic-Offline-Existing-ETL-CPU-Reader-ReadOnly-UNVERIFIED`。ZIPは36ファイル、ZIP CRC PASS。ダウンロード時SHA-256：`31d97330e1de39cc14ba17fd618c7fd7ee6aa7e1061e71198faab6316405e0e8`。
- **実際のWin10 1.1.33の1.5GB ETLをこのツールで解析したという事実はない**。ユーザーが既存 `CPU_Stacks.etl` を選択すると、同じフォルダーに新規 `CPU_AllProcesses_*.txt` を作り、元ETLや画像は一切変更しない。報告TXTも共有前レビューを勧める。新しいWPR録画は不要。

### 今回追加で照合した一次資料

- Microsoft Windows Performance Analyzer CPU Usage (Sampled) はCPUサンプルのウェイト・プロセスとスレッド帰属を表示する一方、短いDPC/ISRはサンプリングで十分表現されないことがある: https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis
- Microsoft PerfView は `PerfInfoSample` のThreadID0を除外する例とプロセス生涯の追跡を持つ: https://github.com/microsoft/perfview/blob/main/src/TraceEvent/Computers/TraceProcess.cs
- TraceEventプログラマーガイド、ETWTraceEventSource とカーネルイベント解析: https://github.com/microsoft/perfview/blob/main/documentation/TraceEvent/TraceEventProgrammersGuide.md
- WindowsのWPR/WPAで利用できるプロセス・スレッド／割り込み負荷分類: https://github.com/MicrosoftDocs/SupportArticles-docs/blob/main/support/windows-server/support-tools/support-tools-xperf-wpa-wpr.md

**次の本質的なタスク**：ユーザーの2026-10-30 20:30 ETLを再録画せずローカル集計し、CPU負荷の内訳（Photos各PID、複数dllhost、System、その他、ThreadID不明）を確認。その結果を起点にWIC/libheif側のフル解像度デコード削減・GPU利用可否の研究優先順位を決める。単純なworker数変更とMSI乱発は継続して避ける。

## 11. 2026-10-30：CPU解析ツールの取得不能報告と代替配布

- ユーザーがChatGPT内のCPU解析ツールZIPリンクからダウンロードできないと報告。**原因（リンク期限切れ・ブラウザ制御・プラットフォーム側など）は未確定**。破損と決めつけない。
- ローカルに存在したCI成果物 `WicHeic_ExistingETL_CPU_ReadOnly_CI_20261030.zip` は3,410,411 bytes、36エントリ、ZIP CRCすべてPASS、SHA256 `31d97330e1de39cc14ba17fd618c7fd7ee6aa7e1061e71198faab6316405e0e8`。
- 同一バイナリを専用フォルダー `WicHeic_CPU_Analyzer/` に再梱包し、`START_HERE_JA.txt` を追加した `WicHeic_CPU_Analyzer_Win10_20261030_REPACK.zip`（3,338,438 bytes、37エントリ、CRC PASS、SHA256 `ca704b55d31be9bc4360187e68c9beb926ac498b8faf934f260e9dbddcb4c3a5`）をチャットに提供。**sandboxリンクはチャットのダウンロードUI依存なのでGitHub上に置いた恒久ファイルではない。**
- GitHub Actionsの公式な代替取得先： https://github.com/taku077/wic_heic/actions/runs/41059547959 の `Artifacts` > `WicHeic-Offline-Existing-ETL-CPU-Reader-ReadOnly-UNVERIFIED`。GitHubログインとリポジトリ読取り権限が必要。**workflowで保持期間7日**が設定されており長期配布先には不適切。
- 今後「リンクがダウンロードできない」ときは、その場で同一CIバイナリを再梱包してCRC検証し、ChatGPTダウンロードとGitHub Actions両方を示す。GitHub Releasesへの恒久配布はアップロード実行手段とユーザーが望む公開範囲を確認してから検討。リンクが直ったと断定しない。
- このCPU解析ツールのWindows10実機ETL動作およびCPU100%解決は**引き続き未検証**。新しいWPR録画は不要。

## 12. 2026-10-30：Windows PowerShell 5.1での日本語PS1起動不能を修正

**ユーザー実機で確認された不具合**：`WicHeic_CPU_Analyzer_Win10_20261030_REPACK` の `Run_Existing_ETL_CPU_Report.ps1` 8行目・24行目などで `MissingEndParenthesisInMethodCall`、文字化けした日本語（例 `CPU險ｺ譁...`）、引用符・括弧・閉じ波括弧の構文エラーが連鎖し、ファイル選択ダイアログさえ開かなかった。CPU解析ロジックや既存ETLが壊れたことを意味しない。

**根本原因を実配布ZIPのバイト列で確認**：日本語文字列を含む `.ps1` が**UTF-8 BOMなし**（先頭 `EF BB BF` なし）。Windows PowerShell 5.1はBOMなしファイルをANSIコードページとして解釈し得る。従来のCIはPowerShell 7 (`pwsh`) のParser.ParseFileでしか検証しておらず、Windows10で実際に起動される5.1のパーサを未検証。Microsoft一次根拠：https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_Character_Encoding?view=powershell-5.1

**修正**：
- 研究ブランチ `investigate/win10-100cpu-after-1133-20261030` の `tools/etl_cpu_top/Run_Existing_ETL_CPU_Report.ps1` をUTF-8 BOM付き（バイト列 `EF BB BF`）へ更新、日本語画面は維持。GUI表示せず解析EXEの内蔵テストを実行する `-SelfTest` を追加。コミット `3c36528162b50ee9c5ac93d8d369764a54b09854`。
- `.github/workflows/offline-etl-cpu-top-ci.yml` に**Windows PowerShell 5.1本体 `powershell.exe -File ... -SelfTest` を実行**する検査を追加、配布対象PS1のBOM `EF BB BF` をバイト列で判定。コミット `ea043f87ff5e773611b8609cc9c8df66d820a157`。
- [GitHub Actions #41061935218](https://github.com/taku077/wic_heic/actions/runs/41061935218) **SUCCESS**。CIログに `ETL_WINDOWS_POWERSHELL_51_JAPANESE_LAUNCHER_PASS`、人工短時間WPR ETLで `REPORT_CREATED` と記録。旧CIのPowerShell7構文検査のみの問題を解消。**ユーザーWindows10のGUI実機テストは未完了**。
- 旧CI生成バイナリは維持し、修正PS1と日本語案内を入れ直した37エントリCRC合格ZIP `WicHeic_CPU_Analyzer_Win10_PS51_Fixed_20261030.zip` を同チャットに提供。SHA256 `372a2f49c86f1a75eff83ad6fa00b9b6621c7d81160b2079569a0e410db05f24`。チャットリンクは恒久的URLではないため、取得不能時の代替はActions #41061935218のArtifacts（通常ログイン・保存期間7日）。
- **再発防止ルール**：`powershell.exe` 経由で起動する、日本語を含む全`.ps1`はUTF8 BOMを要求。コード生成・ZIP再梱包後もBOMのバイト列・ZIP CRC・**実際のWindows PowerShell 5.1での動作**を検証する。PowerShell7のみの検査を合格としない。

**この時点の作業指示は失効**：ユーザーの大容量 `CPU_Stacks.etl` は既に削除されている。修正版ZIPを実機で動かす必要はなく、ユーザーに再取得・復元・保存を求めない。CPU100%自体は依然未解決。続きは本台帳冒頭の【最優先】節に従う。

## 13. 2026-10-30：ETLがユーザーPCに既にないと判明（工程の是正）

ユーザーから「以前保存した `CPU_Stacks.etl` はもうない。次回使うか不明のGB単位の診断ファイルを長期保管できない」と指摘された。解析EXEの5.1起動対応をした後も「既存ETLを選んでください」と案内したのは、**保存の確認なしに前提とした計画ミス**。これにより、今回の実機診断ツールは即時の役割を果たせない。実ファイルがない以上、追加改善したETLツールではCPU100%原因を明確化できない。ユーザー側の不備ではない。

**是正方針**：
1. 既存WPR由来の集計済み証拠とGitHub記録だけで再評価し、問題箇所（とくにdllhostのフルサイズ復号）をコード上で絞り、人工画像CIを使って実験する。
2. 必要な新しい実機負荷チェックは、OSカウンターを短時間集計して小さいテキストのみ残す方式を原則とする。**新しい大容量WPR/ETLを作るツールを無断で配布・依頼しない**。
3. 後で再利用しそうというだけでユーザーに大型資料の保存を要求しない。次の研究に必要な情報は、提出された時点で必要な集計・匿名化・記録を済ませる。
4. 根本原因の切り分けに不足する情報がある場合は、不足点を明示する。**既存ログが全プロセスのCPU帰属を証明したと誤って主張しない**。

確認に使用した一次資料：
- Microsoft Windows Performance Recorderのメモリ／ファイルモードと容量制御: https://learn.microsoft.com/en-us/windows-hardware/test/wpt/logging-mode
- Microsoft PDH CPUカウンター収集、プロセス名の重複とPID誤帰属への注意: https://learn.microsoft.com/en-us/windows/win32/perfctrs/collecting-performance-data
- WindowsのWPRプロファイル開始／停止: https://learn.microsoft.com/ja-jp/windows-hardware/test/wpt/wpr-command-line-options

**状態：** 大容量ETLの再解析作業は中止。CPU100%は未解決。製品本体/MSIは今回変更していない。

## 14. 2026-10-30：ユーザーによる実機診断・過去ツール再配布の運用明確化（最優先の訂正）

ユーザーは**「実機作業自体を避けたい」とは言っていない**。CPU100%解決にどうしても実機検証が必要なら、適切なタイミングでユーザーに依頼してよい。その際、以前の診断ツール等を使う必要があれば、**ユーザーが旧ZIPやプログラムを保持している前提を置かず、こちらが最新の安全な版を準備して、依頼する回答本文にダウンロードリンクを別途添付する**。既に渡したツールの「保存しているはず」を前提にしない。再配布には必要に応じて実ビルド・CI・ZIP整合性・Windows PowerShell 5.1とユーザーの実行環境での適合性を確認する。サンドボックスの過去チャットリンクは再利用可能と見なさず、その時点で実在確認できるリンクを使う。

**区別**：
- **過去の診断ツール／アプリ**：ソースとビルド条件がGitHubに残っているものは再生成・再配布を試みられる。提供時にはファイル・起動手順・成果物の用途と実機未検証範囲を明記する。
- **ユーザーが削除した元の大型ETL**：ツールを再配布しても自動的には戻らない。必要なら新たに計測するか、既存の匿名化済み集計で進む。ユーザーに旧ETLがあると決めつけない。
- **今後の診断**：実機検証を禁止しない。CPUの持続負荷や保存操作の安全性に効く場合、具体的な目的と収集データ、サイズ・削除時点を説明し、可能なら実行時に小さなレポートを生成する。やむを得ず一時的に大きなETLを作る際も、解析・要約後の削除を前提にし、ユーザーに永続保管を求めない。
- **判断は性能・安全性優先**：診断やソース調査が必要なだけで長く足踏みせず、過去の失敗を繰り返さずに実際のCPU100%改善へ向かう。

参考：Microsoft WPRはメモリ記録モードも提供しており、ファイル記録は容量が膨らみ得る（https://learn.microsoft.com/ja-jp/windows-hardware/test/wpt/logging-mode）。GitHub Actions成果物は有効期限があるため、必要時に都度提供できる形を選ぶ（https://docs.github.com/ja/actions/how-tos/manage-workflow-runs/download-workflow-artifacts）。

**この第14節と冒頭の訂正は、過去の「実機診断は避ける」「旧解析ツールは今後不要」などの過度に限定的な書き方に優先する。**

## 15. 2026-10-30 22:43 JST：実機CPU解析レポート受領と次の原因調査

- 受領レポート：`CPU_AllProcesses_20261030_224307_303.txt`（匿名化済みテキスト）。対応ETL記録時間2026-10-30 11:30:14〜11:33:18 UTC、論理CPU4、サンプル全461551／スレッドIDあり456016／不明または0が5535。
- **サンプル内訳**：プロセス名不明293247（64.31%）、dllhost 92114（20.20%）、Photos 62848（13.78%）。これらは**スレッドID付きサンプル内の構成比**であり、タスクマネージャーのCPU使用率でもプロセスCPU使用率でもない。
- **重大な未解決課題**：293247サンプルがプロセス名不明。そのうちPIDも不明249525サンプル（54.72%）。`Unnamed=0` は実際には名前が完全取得できたという意味ではなく、文字列 `(unknown)` が既に代入されているためカウンタが増えない可能性がある。GitHubの`tools/etl_cpu_top/Program.cs` における `e.ProcessID` と `ThreadStartGroup` 由来PIDマッピング、および`e.ProcessName` によるフォールバックの精度を検証する。
- **判断（当時の状態）**：提出されたレポートは正常に生成・提出されている。過去の提出ZIP・レポートはこちらで再取得して利用する。主要CPU寄与の全容は現レポートだけでは未確定。実機で報告されたCPU100%未解決。なお次の第16節で改善版の実機再解析が必要となり、元ZIPもツールもこちらから改めて提供することにした。
- **参考実装**：Microsoft PerfView のProcess/Thread lifetime tracking https://github.com/microsoft/perfview/blob/main/src/TraceEvent/Computers/TraceProcess.cs 、Microsoft ETW Process rundown https://learn.microsoft.com/en-us/windows/win32/etw/process 、Microsoft WPA CPU sampled https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis 。

## 16. 2026-10-30：提出済みETLレポート(unknown)64.31%の保守的修復研究

**実装先端**：`investigate/win10-100cpu-after-1133-20261030` / `60c55f718343fbe7661fa6f64edb4a6e284a95a0`。  
**Windows CI**：https://github.com/taku077/wic_heic/actions/runs/41067460713 **SUCCESS**。  
**製品コード**：変更なし。ユーザー実機のCPU100%問題は**依然未解決**。

### 旧レポートと元ファイルの確認

- ユーザー提出 `CPU_AllProcesses_20261030_224307_303.txt`：取得サンプル461,551、ThreadID付き456,016、ThreadIDなし5,535。
- ThreadID付きサンプルの割合：`(unknown)` 293,247 (64.31%)、`dllhost` 92,114 (20.20%)、`Photos` 62,848 (13.78%)。**これらはCPU使用率%ではない**。
- 不明293,247のうち249,525件はPID自体が不明。さらに `(unknown) [PID 936]` 31,350件は、**同じ提出済みZIP内の`Process_CPU_1s.csv`でPID936が`explorer`と確認できる**。これは旧レポートのプロセス名帰属が不完全である確かな一例。ただし単純に同PIDを全て自動再帰属した数値は新レポートが出るまで断定しない。
- 元ZIP `COM_CPU_WPR_Manual_20261030_203013_576.zip`（213,993,795 bytes、CRC PASS）をユーザー過去アップロードから再取得済み。中の `CPU_Stacks.etl` は1,500,512,256 bytes。**このデータをGitHubに公開していない。**
- 同じZIP内の `CPU_WIC_Summary.txt` ではフル復号71回、dllhost PID3284のフル復号64回、同PIDのCPUサンプラー平均16.76%、最大45%（特定プロセスサンプラー値）。相関だけでWIC関数のCPU100%原因と断定しない。

### 新実装（無駄な再測定を避ける）

- `tools/etl_cpu_top/Program.cs` を更新。旧ツールはサンプル到着時にプロセス名が不明だと即`(unknown)`へ固定していた。
- 新版はCPUサンプルを`PID・TID・10秒区間`単位でメモリ上だけに集計し、**ETLの全プロセス/スレッドrundownを読み終えた後**にまとめて帰属。ETL原本の上書きも新たなETLファイル生成もしない。
- `KernelImageFileName` を利用してプロセス名取得を改善。ThreadIDが後段に記録され、**当該TIDが全記録中でただ一つのPIDへ対応する場合に限り**後から復元。別PIDに再利用されたTIDは**復元禁止**、不明のまま残す。
- 新レポートに`AttributionRawPID`、`DeferredUniqueTIDRecovered`、`PIDUnresolved`、`ThreadIDsAmbiguous`を追加。旧`Unnamed=0`の過小計数も修正。
- 人工短時間WPR ETL、同名dllhost PID別集計、Windows PowerShell 5.1日本語起動、ファイル上書き防止をCIで検査。GitHub Actions #41067460713 SUCCESS。
- **注意**：ThreadIDが全体で再利用されたケースは帰属しないので`unknown`が残る場合がある。ETW採取時点で欠落した情報は、ソフト側だけで正しく復元できない。`(unknown)`の64.31%が全て解消するとは主張しない。
- CPUサンプルの比率はWPA CPU utilizationやTask Manager CPU%と同義でない。実機で新報告を受け取るまではv2の効果未証明。

### 再配布済み成果物・次のゲート

- CI由来のv2解析ツールを一フォルダにまとめた `WicHeic_CPU_Attribution_V2_PS51_20261030.zip`（3,340,715 bytes、CRC PASS、PowerShell 5.1起動PS1のUTF-8 BOM検証、SHA-256 `04bb68965c0ad8a09ac308e928d23220399138031c94671c6965adf0a72cb813`）。これはChatGPT添付であり、**将来のチャットでは今のsandboxリンクを推測して再利用しない**。
- 先に提出済みETLの元ZIPもこちらが用意。必要な時は2ファイルのダウンロードリンクを**同じ回答文で再提供する**。1.5GBの一時展開スペースが必要であることを明示。
- 次の有意なゲートは、**同じ2026-10-30 ETLでv2の小さいCPU_AllProcessesテキストを生成**し、不明・PID復元・dllhost/Photos/Explorerの増減を比較すること。新WPR録画は不要。匿名化されたTXTのみ共有し、元ETLは処理後に削除可能。
- 失敗時は新ETLを要求せず、ツールの障害を直す。Windows10実機合格までは「実機動作確認済み」と扱わない。重大なCPU負荷改善がないなら新MSIは配布しない。

### 横断調査した一次資料

- Microsoft ETW ThreadイベントとDCStart/DCStop、ヘッダーPID/TIDが異なる場合の注意: https://learn.microsoft.com/ja-jp/windows/win32/etw/thread
- Microsoft WPA sampled CPUの意味・DPC/ISRの限界: https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis
- Microsoft PerfView のPID lifetimeとCPUサンプル帰属実装: https://github.com/microsoft/perfview/blob/main/src/TraceEvent/Computers/TraceProcess.cs
- PerfView TraceEvent programmer guide: https://github.com/microsoft/perfview/blob/main/documentation/TraceEvent/TraceEventProgrammersGuide.md
- Microsoft Xperfプロセス/スレッドCPU集計にはLOADER+PROC_THREAD+PROFILE必要: https://learn.microsoft.com/en-us/previous-versions/windows/desktop/xperf/cpu-sampling-by-process

## 17. 2026-10-30 23時台：実機v2提出の分析、過去ZIPとの交差照合、ProcessEnd側の追加改善

- **受領したユーザー実機レポート**: CPU_AllProcesses_20261030_230208_049.txt。既存の元ZIPはChatGPTライブラリから回収できている。追加録画も、過去の大容量ファイル長期保存もユーザーに要求しない。
- スレッドID付き456,016件。RawPID206,491件、UniqueTIDからの遅延PID復元239,637件、なおPID不明9,888件（2.17%）。前版のPID不明249,525件（54.72%）に比べ大幅減少。しかし**名前不明293,247件（64.31%）は変わらない**。
- 内訳の大きいPIDは、不明PID11664=87,383件（19.16%）、不明PID936=70,212件（15.40%）、dllhost PID3284=89,373件（19.60%）。Photos全体62,848件（13.78%）、dllhost全体92,114件（20.20%）。これは**CPUサンプルの構成比**でありPC全体CPU%ではない。
- 元ZIP内のProcess_CPU_1s.csvを当方で直接読み直し、PID936がexplorer（228行、11:30:17〜11:33:35 UTC）、PID10084がPhotos（31行、11:30:17〜11:30:43 UTC）と同じ採取セッションで確認。v2の不明名70,212+5,034=75,246件は別記録で識別でき、条件付きの未識別は293,247-75,246=218,001件（47.81%）。ただし**ETL内部の名前解決が直ったわけではない**。PID11664は同梱CPU CSVに現れず、実行名不明。PID4もこの独立CSVでは未確認。
- 元のCPU_WIC_Summary.txtには全フルサイズ復号71件、dllhost PID3284で64件との記載。負荷候補として有力だが、WICだけがCPU100%の全原因とは断定しない。
- **GitHub実装**: 調査ブランチ investigate/win10-100cpu-after-1133-20261030 の解析コードを更新し、ProcessStart/DCStartに加えてProcessEnd/DCEndからも実行名を収集。同じPIDに相異なる名前がある場合は名前割当てを拒否。ProcessStartOrDCStartEvents、ProcessEndOrDCEndEvents、ConflictingNamePIDs、UniqueNamedPIDsをレポートに追加。
- 初回CI #41068927840は自己テストの引数不足（CS7036）でFAIL。修正コミット5926c39eb8efee519dd9fdcf153815c57e643be2に対し**CI #41069110425 SUCCESS**。このCIは人工ETLの成功であり、実際の元ETLで名前不明が減ったとはまだ証明していない。
- 詳細（匿名化済み）: https://github.com/taku077/wic_heic/blob/investigate/win10-100cpu-after-1133-20261030/docs/WIN10_1133_ETL_V2_CROSSCHECK_20261030_JA.md
- 外部根拠: Microsoft PerfViewプロセス生涯 https://github.com/microsoft/perfview/blob/main/src/TraceEvent/Computers/TraceProcess.cs ; TraceEvent parser https://github.com/microsoft/perfview/blob/main/src/TraceEvent/Parsers/kerneltraceeventparser.cs.base ; WPA CPU解釈 https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis
- **次に進む条件**: 無限に診断をやり直さず、既存のShell/dllhostフルサイズ復号削減の実装研究へも進む。追加実機検証が必要となったときだけ、過去提出ZIPと最新ツールを**当方から再添付**する。新MSIはCPU改善と安全性が確認できるまで作らない。

## 18. 2026-10-30 23時台：Shellフル復号コスト削減研究の再開と正規48タイルgridゲート

**重要：** 前のCPUレポート解析だけで足踏みせず、HEIC復号負荷を直接減らすためShell高速経路の研究コードを改良した。CIは成功したがユーザーWindows10の本番WIC/MSIには反映しておらず、**CPU100%は未解決**。ユーザーの大容量ETL・写真を再提出させる必要はない。

- **研究ブランチ：** `experiment/shell-com-focused-20261030` / 記録時HEAD `fba2d3e92d8157187ac58664a493d3c22362f4ec`。実装／テスト：`wictest/WICShellOrientationGate.h`、`wictest/WICShellThumbnailProviderPilot.h`、`wictest/isolated_shell/SyntheticEmbeddedHevcThumb.cpp`、`.github/workflows/shell-com-focused-ci.yml`。
- **実装1：** `IThumbnailProvider` が起動する前に、`FromStream` は従来最大24MiBの全HEICを確保・読み込んでいた。新コードはまず64KiBだけ確保・読込み、`grid`・`auxl`・`dimg`・`tmap` という複合HEIF関係の印が見つかった場合に**早期に通常フルWICへ安全に戻る**。この4文字列は**拒否だけに使用**し、許可に使わない。該当しない場合は以前の完全なBMFF構文・色・Exif検査を維持する。
- **既存写真のメタデータだけを読む私的調査：** 過去提出のiPhone系HEIC（元1,721,813 bytes）の先頭64KiBでgrid=1173 byte、dimg=1550、auxl=1708、ICC prof=1770を確認。画像画素の復号／書換えは行わず、ファイル名や個人メタデータはGitHub未公開。64KiB早期拒否により、**この事前判定に限りファイルコピー・メモリ確保が設計上約96.2%削減**される。WICフル復号そのものが減る効果ではなく、実機CPU改善率でもない。
- **試験1：** 人工・無効gridマーカー入りメモリストリーム131072Bを作り、65536B読んだ時点で拒否することをCI確認（`CI_SHELL_COMPLEX_EARLY_REJECT_PASS`）。最初のα素材による「実ファイルの読込み削減」検証は素材全体が37087Bしかなく、成立しなかった。この初期試験の誤ったサイズ前提を訂正済み。
- **実装2（既存研究の不足を解消）：** 有効な8×6=48タイル（4032×3024px、各504×504）と320×240埋込HEVCサムネイルを備えた**正規の人工grid HEIC**をPinned libheif APIで実際に生成。以前のFourCCだけ書換えた無効grid変異テストとは別。生成マーカー `CI_VALID_GRID_48_TILES_EMBEDDED_THUMB_CREATED_PASS`。
- **試験2：** Windows Server 2022で隔離Shellハンドラーを実際に登録し `IThumbnailCache` 経由で生成した正規gridを取得。安全側にフルWICフォールバックし、登録前と出力256x192、64×64RGB比較MAE 0.00、強い差分0%。`SHELL_REGISTERED_GRID48_FULL_WIC_FALLBACK_PASS`。ICC、Exif/irot、alpha、PQ/HLG等の既存安全ゲートと関連付け復元もPASS。
- **既存高速経路の維持：** 単純な人工HEIC（正規gridとは別）では8件中央値93.50ms（既定）→12.00ms（埋込thumb経路）、比0.128、元主画像のフル12MP復号0。**この数値を実iPhone複合gridの高速化達成と表現しない**。
- **実装CI：** [Actions #41070865266](https://github.com/taku077/wic_heic/actions/runs/41070865266) SUCCESS（初期軽量化）、[Actions #41071529559](https://github.com/taku077/wic_heic/actions/runs/41071529559) SUCCESS（正規grid、画質、登録復旧、既存一式）。
- **詳細な再利用可能資料：** https://github.com/taku077/wic_heic/blob/experiment/shell-com-focused-20261030/docs/SHELL_GRID48_EMBEDDED_THUMB_PRELIGHT_20261030_JA.md
- **今後の本質的ゲート：** 実際のiPhoneのgrid+ICC/HDR/gain map+Exifを安全に埋込thumbから表示できるか、まず人工複合素材で**色・向き・HDRの基準画像比較**を検証。安全性を示せない場合は高速パスを拡大しない。Shell専用経路を本番に持ち込む場合は第三者ハンドラー保護、インストール／アンインストール復元、Win10実機のCPU100%・体感の改善を必須とする。**無意味な小刻みMSIを出さない**。
- **参考：** libheif tiled official API https://github.com/strukturag/libheif/wiki/Reading-and-Writing-Tiled-Images 、Microsoft Shell handlers https://learn.microsoft.com/ja-jp/windows/win32/shell/building-thumbnail-providers 、libvips thumbnail path https://github.com/libvips/libvips/blob/master/libvips/resample/thumbnail.c 、WIC thumbnail fallback https://learn.microsoft.com/en-us/windows/desktop/wic/-wic-imp-iwicbitmapframeencode 。

## 19. 2026-10-30 23:50 JST：正規48tile gridの埋込サムネイル復号可能性を確認（AI開発の継続）

**目的**：繰り返しETL診断を作らず、Shell/COM Surrogateでの大規模HEICフル復号を減らす次の実装の根拠を得る。ユーザー提供の画像原本・ETL・レジストリに手を加えない。

- **研究ブランチ**：`experiment/shell-com-focused-20261030`／記録時HEAD `aeeba5d31beead1aab4a9660f7e62709da674382`。今回の主な変更：`wictest/WICShellThumbnailProviderPilot.h` の `RunGridEmbeddedShadow`、`wictest/wictest.cpp` のCI専用 `--shell-grid-shadow`、`.github/workflows/shell-com-focused-ci.yml` の合成grid生成、画質比較、WICトレース確認。
- **実際に検証した内容**：libheifで生成する正規8x6=48 tile・主4032x3024・サムネイル320x240（人工の四色ブロック素材）のコンテナを自作WICで開き、`IWICBitmapDecoder::GetThumbnail`からサムネイルを取得、同じWIC Fant→BGRA経路で256x192にする。その後フル主画像を基準として復号し、同じ寸法・補間でRGBサンプル比較。
- **画質**：64x64点のRGB MAE `0.038` /255、20階調超の差分率0.000%。試験済みgridはICCなし。**全画素厳密一致・異なる画素内容・複雑なiPhone HDR/gain map/Exifの表示品質を証明しない**。
- **WIC診断トレース：** `CI_GRID48_SHADOW_WIC_TRACE_COUNTS_PASS full_primary=1 embedded=1`。検証テストでは`embedded`を先に、基準用`full_primary`を後で復号。プロセス横断のCPU使用率・Shell高速経路への本番適用を示すものではない。
- **参考測定（単回CIのみ）**：`CI_GRID48_WIC_RENDER_TIMING_MS embedded=0 full_master=109 single_sample=1`。0msはGetTickCount64の分解能でゼロと表示されたにすぎず、完全な処理時間ゼロではない。順序・キャッシュ・cold/warm区別なし、推定高速化倍率として使用禁止。
- **CI成功**：[Actions #41074570057](https://github.com/taku077/wic_heic/actions/runs/41074570057)（画質比較と従来フォールバック分離）、[Actions #41075151932](https://github.com/taku077/wic_heic/actions/runs/41075151932) **SUCCESS**（実際のWIC埋込/フル復号件数、所要時間を記録、既存Shell登録CI、ICC・irot・alpha・PQ・HLG・GRID48の安全フォールバック、関連付け復元）。
- **失敗と対処**：最初のCI #41074281935は、影テストで基準用フル復号を1回追加したため、旧フォールバック用トレースの`full=3`固定期待値が`full=4`になってFAIL。画質差・本体不具合ではなくテストの計測範囲混同。計測ログを分けてCI合格。今後、実装を変えた試験の復号イベント数は従来試験と混ぜない。
- **意味**：単純な正規gridでも、埋込thumbを取得することは原理的に可能。従来の厳格`WicShellLabSafety::FromStream` はgridを依然拒否するため、本番・研究Shellのgrid高速許可**未実装**。次は**8bit SDR・方向/色が安全なgridのみを対象とする検証可能な資格判定**、および実画像構造に近い人工ICC/HDR/Exif/gain mapによる誤受理拒否を設計。これらの安全性とWindows10実機のCPU/体感効果が確かめられるまでMSI配布なし。
- **外部の新情報**：2026-10-26公開のlibheifセキュリティ勧告 https://github.com/strukturag/libheif/security/advisories/GHSA-48p4-gj45-w9v6 は悪意ある派生HEIFでのCPU枯渇を報告、`v1.23.7`で修正。研究ブランチのlibheif submodule SHA `12a259a5882dd99d45ec5fc77e296a3b620d1e2e` はv1.23.7リリース。**ユーザーのCPU100%はこの攻撃が原因だったとは言えない**。
- **再利用用技術資料**： https://github.com/taku077/wic_heic/blob/experiment/shell-com-focused-20261030/docs/SHELL_GRID48_EMBEDDED_THUMB_PRELIGHT_20261030_JA.md
- **再発防止**：過去実験を再実施する前に台帳第18～19節とCIを読む。個人画像/ETLを公開GitHubにアップロードしない。大容量ログの手元保存は前提としない。実機で過去ファイルが必要ならChatGPT Libraryを先に検索し、必要なZIP・ツールを依頼文に同梱または別添する。
