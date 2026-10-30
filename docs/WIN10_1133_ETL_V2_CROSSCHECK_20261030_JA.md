# Windows10 WicHeic 1.1.33 CPU100 — ETL v2レポート横断照合（2026-10-30）

## まとめ：PID解決は成功、名前解決は未達

**この文書は匿名化済みの研究メモであり、原本ETL・個人ファイルパス・コマンドラインを含まない。** ユーザー提出 `CPU_AllProcesses_20261030_230208_049.txt` と、以前に提出された同一WPR採取ZIP内の `Process_CPU_1s.csv` / `CPU_WIC_Summary.txt` を対照した。追加の録画や原本更新は実施していない。

| 数値・発見 | 今回の測定・確認 | 読み方 |
|---|---:|---|
| 全ETW CPUサンプル | 461,551 | ThreadID 0／欠落含む受信数 |
| ThreadID付き | 456,016 | 集計割合の分母、**PC全体CPU使用率ではない** |
| ThreadIDなし・0 | 5,535 | すべてがCPUアイドルを意味するわけではない |
| PIDがもともと明確 | 206,491 | v2内訳 |
| 後段rundownから保守的に復元したPID | 239,637 | ETL記録自体を再作成していない |
| PID最後まで不明 | 9,888（2.17%） | v1の249,525（54.72%）から大幅改善 |
| **プロセス名不明** | **293,247（64.31%）** | v1と同じ。**PID解決と名前解決は別** |
| dllhost.exe合計 | 92,114（20.20%） | 中心PID3284は89,373 |
| Photos.exe合計 | 62,848（13.78%） | 複数PIDに分散 |

### 同じ提出ZIP内の独立CPUサンプラーとの照合

同梱 `Process_CPU_1s.csv` が列挙したPIDと名前を当該WPR時間帯で突合：

- `PID 936` は `explorer`（228行、記録11:30:17〜11:33:35 UTC）、v2レポートでは `(unknown) [PID 936]` に70,212サンプル（15.40%）。**名前が欠落しているが、既存の別記録では識別できている例**。
- `PID 10084` は `Photos`（31行、11:30:17〜11:30:43 UTC）、v2レポートでは `(unknown) [PID 10084]` に5,034サンプル（1.10%）。
- この2件を**同一セッションでPID再利用なしという前提付き**で計上すると、プロセス名不明は `293,247 - 70,212 - 5,034 = 218,001`（47.81%）に相当。Photosは `62,848 + 5,034 = 67,882`（14.89%）、explorerは最低70,212（15.40%）。**補正は外部CSVに基づく解析上の注記であり、v2 ETL内での名前取得が直ったとは主張しない**。
- `PID 11664` が87,383サンプル（19.16%）と重いが、同梱 `Process_CPU_1s.csv` はPhotos / dllhost / explorerの監視対象に限るため、当該PIDの名前は**不明**。名前を推測して割り当てない。
- `PID 4` に17,260件（3.78%）あり、Windowsの通常のSystem PID4との関係は追加検証対象だが、**今回の独立CSVでは直接名前を確認できないので本表の補正では未識別のまま**。

### CPU100%問題との関係

- 同梱 `CPU_WIC_Summary.txt` の `WicFullDecodes=71`、`dllhost PID3284` に64回が集中。Windows10実機の高負荷候補として有力なまま。サンプル割合は関数別CPU時間の証明ではない。
- ETLの`dllhost.exe PID3284`は89,373サンプル（19.60%）、他Photos複数PID。最重要の未識別プロセス `PID11664` とexplorer欠落名が解決されるまで、CPU100%が自作WICだけに由来すると断定しない。
- **不要な追加のユーザー録画はしない**。過去提出ZIPがChatGPTライブラリにあり、関連テキストを当方で再確認可能。

## 今回の次工程

1. ETL解析コードでは `ProcessStartGroup`（Start/DCStart）だけでなく `ProcessEndGroup`（Stop/DCEnd）も読んで、プロセス名の復元可能性を増やす。PID再利用で異なる名前が出た場合は、誤った1対1対応を拒否する。
2. 名前関連の失敗理由（start/end件数、名前衝突PID数）を匿名化集計で出し、改善版CIを実行。
3. それでも`PID11664`の名前を復元できない場合、OS全体CPU帰属は限定的なまま評価し、**無限に診断を引き延ばさない**。残存のWIC由来dllhostフルサイズ復号削減の実装研究を優先する。

## 参考資料

- Microsoft PerfViewのプロセス／スレッド生涯管理（StartGroupとEndGroupを両方使用）：https://github.com/microsoft/perfview/blob/main/src/TraceEvent/Computers/TraceProcess.cs
- TraceEvent kernel parser / process DC start・end API：https://github.com/microsoft/perfview/blob/main/src/TraceEvent/Parsers/kerneltraceeventparser.cs.base
- Microsoft sampled CPU解釈：https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis
- Microsoft ETW必要プロバイダ（Loader+ProcThread+Profile）：https://learn.microsoft.com/en-us/previous-versions/windows/desktop/xperf/cpu-sampling-by-process
