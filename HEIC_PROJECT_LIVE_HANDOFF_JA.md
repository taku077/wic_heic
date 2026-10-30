# HEIC / WIC 長期開発・常時更新引継ぎ台帳（正本）

**台帳作成日:** 2026-10-30 JST  
**対象シリーズ:** ChatGPT「HEIC表示方式比較 1～15」および今後の続編  
**正本の固定URL:** https://github.com/taku077/wic_heic/blob/master/HEIC_PROJECT_LIVE_HANDOFF_JA.md  
**リポジトリ:** https://github.com/taku077/wic_heic

> **このファイルがチャット移行用の唯一の入口。** 新チャットではまず本ファイルを取得し、**ここに書いたSHAやCIはスナップショットなので、その時点でGitHubの各ブランチHEAD・CI・直近の変更を再取得**する。既存研究・失敗を繰り返さない。作業中も重要な決定・実装・CI・実機評価のたびにこのファイルを更新する。更新不能時は「更新できなかった」と明示する。過去15スレッドの全メッセージ・添付がこのファイルだけで完全復元できるとは主張しない。

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
