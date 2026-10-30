# Windows10 WicHeic 1.1.33 — CPU100%未解決（2026-10-30 20:30 実機）

## 最優先課題／結論

ユーザーの実機では **Windowsフォトを開くとタスクマネージャーのPC全体CPU使用率が100%に張り付き、体感の重さは1.1.33で改善していない。** これを最重要かつ未解決の事実として扱う。人工HEICのCIで「CPU-msが約30%減少」は実機でPC全体の問題が解決したことの証拠ではない。**1.1.33はCPU100%問題の解決済み版ではない。**

### 根拠とプライバシー

ユーザーから提供された非公開WPR診断ZIP・WICイベント・子プロセス終了記録を解析した。**ETL生ファイル・写真・ファイルパス・生WICログはGitHubへ追加しない。**
- 1.1.32：2026-10-30 18:04:47～18:08:51 JST、WPR約243.54秒、4論理CPU。
- 1.1.33：2026-10-30 20:30:14～20:33:15 JST、WPR約181.08秒、4論理CPU、WPR exit=0 COMPLETE、ZIP CRC PASS、ETL約1.50GB。
- 収集マニフェストには以前の診断キット由来の「1.1.30」見出しが残るが、実行WICが旧版という意味ではない。**新しいフォト向け `DecoderWorkerSelect source=photos operation=1` と `DecoderGridWorkerBudget source=grid operation=1` が実機で確認でき、1.1.33の処理が稼働したことを示す。**

### 再計算した診断結果（時系列内でPIDごとCPUを合算）

| 観測値 | 1.1.32 | 1.1.33 |
|---|---:|---:|
| 1秒サンプラーでのPhotos最大CPU | 74% | 62% |
| dllhost合計最大CPU | 46% | 45% |
| Explorer最大CPU | 36% | 27% |
| 3系統の同一秒における合計最大CPU | 94% | **77%** |
| WICの全解像度復号（全PID） | 178 | 71 |
| うちPhotosの全解像度復号 | 24 | **6** |
| うちdllhostの全解像度復号 | 154 | **65** |
| WICフォトのHEVC選択（new grid2/codec1） | 0/24 | **5/6** |
| WICフォトの旧codec2選択 | 24/24 | 1/6 |
| WICで新Shell codec1選択 | 153 | 64 |

**重要な解釈**：記録時間、開いた画像、操作、キャッシュ、システム負荷が違うので、各列の減少を1.1.33の実力値として使わない。大半のPhotos復号で1.1.33の新しい制限が実際に有効だったにもかかわらず、ユーザーがPC全体CPU100%を観測した。コードが未適用だから重い、という説明は不適切。

**最大の診断の穴**：`Process_CPU_1s.csv`はPhotos, dllhost, explorerの3名前を主に採取し、**PC上のすべてのCPU消費プロセス／System／割込みを合計していない**。そのため記録の最大77%とユーザーのTask Manager全体100%は矛盾しない。またTask ManagerはCPU Utility、サンプラーはProcess.GetProcessTimesを基にするため指標定義に差がある。Windows Performance Toolkitでは [CPU Usage (Sampled)](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis) で全プロセス・スレッドの寄与を判定可能。ただし**生ETLを公開リポジトリや外部CIへアップロードしない**。

### 次の修正優先順位（速度改善を目的としない）

1. **残ったCPU100%を動かす全プロセス／CPUスタックを、既存1.1.33のETLで特定する。** 新たに何GBものWPR録画をユーザーへ求めない。候補はPhotosのソフトウェア HEVC復号、Explorer COM Surrogateの連続全解像度復号、Windows画像関連の他の処理、System／割込み、ハードウェア処理不可である可能性。決めつけない。
2. **CPU計算量そのものを削減する。** 1.1.33でのWIC全解像度復号71件中dllhost65件（約92%）でShell専用embedded fast pathはMSI未搭載。Shell COMの人工単純HEICでは126.95→1.95 CPU-ms（CI #41040091508）だが**grid/ICC/HDR付き実画像では安全性上フォールバック**。HDRトーンマップ／EXIF向き／ICC完全一致を人工grid/HDR素材で検証して、元画像を使わず安全に対象を広げる。
3. **ハードウェアHEVC復号の適用可能性を調べる。** 第7世代Intel HD Graphics 620等はHEVC 8/10bit 4:2:0 decodeのハードウェア機能をIntelが確認。WIC/libheif/libde265の現在のデコードが自動でこれを使うわけではない。Windows Media Foundation `MFTEnumEx(MFT_ENUM_FLAG_HARDWARE)`等を使った利用可否のReadOnly判定→人工HEIC（48タイル）でピクセル／CPU-ms／GPUエンジン使用を比較。**WICコンポーネントへのGPU経路移植はまだ実装していない。** 非対応・ドライバ制限・精度不一致ならCPU経路へ戻す。
4. 「ワーカーをさらに1→0へ下げる」だけで達成したと主張しない。総CPU時間・PC全体CPU100%持続・フォトの応答・サムネイル表示と画像保存の回帰を総合評価する。1.1.34 MSIの量産は禁止。1.1.33は未解決として維持し、意味のある製品コード修正とCIを経た時点でのみ再配布。

### 公開根拠

- [Intel 第7世代CPU media codec表](https://www.intel.com/content/www/us/en/developer/articles/technical/encode-and-decode-capabilities-for-7th-generation-intel-core-processors-and-newer.html)
- [Intel oneVPL ハードウェア機能表](https://www.intel.com/content/www/us/en/docs/onevpl/developer-reference-media-intel-hardware/1-0/features-and-formats.html)
- [Microsoft MFTEnumExのハードウェアMFT列挙](https://learn.microsoft.com/en-us/windows/win32/api/mfapi/nf-mfapi-mftenumex)
- [Microsoft Task Manager CPU Utility とProcessor Timeの差](https://github.com/MicrosoftDocs/SupportArticles-docs/blob/main/support/windows-client/performance/cpu-usage-exceeds-100.md)
