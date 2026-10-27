# Windows 10 HEIC初回サムネイル高速化 — 12MP HEVC 2/4ワーカー研究（2026-10-27）

## 判定

**4ワーカーの候補は同一Windows Server 2022 CIで約59%高速。製品1.1.28には未導入。**

WicHeic 1.1.28の既存Photos編集・回転保存とWIC登録を維持するため、完全隔離したブランチ `experiment/thumb-hevc-workers-1128-isolated` だけで試験。v0.4.0のコード・方針は「参考」として再利用し、`IThumbnailProvider`や関連付けの一括移植は行っていない。

## 比較したコード

- `wic_heic/HeicBitmapFrameDecode.cpp` の既存RGB復号を基本（労働数指定なし）。CIで`GITHUB_ACTIONS=true`かつビルド時`WIC_HEIC_THUMB_WORKERS_LAB`が定義され、さらに `WIC_HEIC_THUMB_LAB_WORKERS=2`か`4`を明示した場合だけ、`heif_decoding_options::num_codec_threads`を使い復号する。**マクロを持たない正式1.1.28バイナリの実行経路は変更なし**。
- `wictest/isolated_shell/Synthetic12MpHeic.cpp` は旧v0.4.0研究から持ち込んだ**パス保護付き人工画像生成器**。GitHub Actions`RUNNER_TEMP`外へ画像を書けず、既存ファイルを上書きしない。
- `wictest/WICThumbnailWorkerLabProbe.h` は同じランナーで標準・2・4モードを5回ずつ交互に測る。出力サイズを256×192に揃えたWIC Fant scalerの初回全画素取得まで計測。**元画像4032×3024 RGBと縮小後256×192 RGBを両方バイト単位で全一致検証**する。
- 診断専用実験ブランチのRelease x64ビルドでのみ専用マクロを有効化。**正式1.1.28のMSIは変更・配布しない**。

## 実測1：12MP全画素取得

[CI #40522554451](https://github.com/taku077/wic_heic/actions/runs/40522554451) : 全工程 **SUCCESS**。4032×3024 24bit RGB、各5回中央値。同一ランナー／同一バイナリ内でworker環境値だけ切り替え。

| モード | 最初の全画素 CopyPixels（フルRGB）中央値 | 現行標準比 |
|---|---:|---:|
| 現行標準（codec threads指定なし） | 1389.101ms | 基準 |
| 2ワーカー | 765.924ms | 約44.9%削減 |
| 4ワーカー | 563.763ms | 約59.4%削減 |

15回すべてRGB全画素一致。一般的なWindows10初回Explorer実測ではない。

## 実測2：12MPから256×192pxのWICサムネイルを初回生成

[CI #40524707311](https://github.com/taku077/wic_heic/actions/runs/40524707311) : 全工程 **SUCCESS**。各5回中央値、WIC Fant scaler初期化＋256×192への`CopyPixels`、完了後のフルRGB検証は**時間計測に含まない**。

| モード | 256px初回生成中央値 | 現行標準比 |
|---|---:|---:|
| 現行標準（codec threads指定なし） | 1397.345ms | 基準 |
| 2ワーカー | 822.885ms | 約41.1%削減 |
| 4ワーカー | 571.343ms | 約59.1%削減 |

**15回すべて元画像の全画素と縮小後の全画素一致**。元のHEICを編集・書き換えない。`WICThumbnailWorkerLabProbe.h`内の画素一致失敗はCI失敗になる。

## 解釈／適用できない範囲

- **現行Windows 10実機1.1.28の初回サムネイル速度が59%改善したという主張は不成立**。実機へ導入した新バイナリはなく、今回の同一ランナー測定ではWindows Shellのフォーマット判定、`IThumbnailProvider`呼び出し、Thumbnail Cache、フォルダ内多枚数並行実行、I/Oキャッシュ、COMプロセス境界を計測していない。
- 人工12MP画像の8bit RGBの例だけで、実機ユーザー画像や全種類のHEVC bitstream、10/12bit、Alpha、grid、Exif Orientation、ICC、large multi-imageや非WPP画像の結果を代表させない。libde265ワーカースレッドはWPP/タイル対応bitstreamで有益性が変化する。
- 低画素のHEICへ4ワーカーを無条件適用するとワーカー作成・同期の負荷や多数画像のCPU過剰競合があり得るため、旧v0.4.0と同様に**大きな8bit不透明画像に限定**する選択を検討する。
- JPEG/PNGの復号器は独立しており、以前の人工1080×1920のWIC自動生成HEIC約141ms対PNG約0.12msという測定は別のコスト。4ワーカーだけではWIC decoder arbitrationの遅延をなくせない。

## 次の統合・判定条件

1. **同じブランチ内で低解像度・高ビット深度・alpha付き画像のフォールバックを試験**。適応型閾値は例として600万画素以上、8bit、非alpha、CPUスレッド数2以上、多枚数処理時はスレッド数抑制。これは旧v0.4.0由来の設計候補であって正式版への採用決定ではない。
2. 空WIC pixel format、回転Exif/ICC、90/180/270°、同一COMフレーム並列CopyPixels、50回回転保存、Shellレジストリ復元のCIがすべて通ること。
3. **追加のMSIはまだ作らない**。初回Shellサムネイル生成の実機比較（同一ファイル群・キャッシュなし・同じExplorer設定・before/after）で効能を確認できる段階まで現行1.1.28を維持。

## 複数サイトからの参照

- libde265公式：WPP/tiles/SSE/AVX最適化と内部worker threads — https://github.com/strukturag/libde265
- libde265 APIの`de265_start_worker_threads`注意書き — https://github.com/strukturag/libde265/blob/master/libde265/de265.h
- libheifのタイル並列とコーデック並列は別レイヤー — https://docs.rs/libheif-sys/latest/libheif_sys/fn.heif_context_set_max_decoding_threads.html
- libvips HEIF loader の埋込サムネイル切り替え・寸法検証 — https://github.com/libvips/libvips/blob/master/libvips/foreign/heifload.c
- Microsoft WIC codec lifetime／多数画像同時作成時の制約 — https://learn.microsoft.com/en-us/windows/win32/wic/-wic-howwicworks


## 2026-10-28：適応型ポリシー・ビット深度除外試験

実験ブランチに`wic_heic/HeicThumbnailWorkerPolicy.h`を追加。固定4ワーカーを製品用に無条件採用するのではなく、**6,000,000画素以上、luma/chroma両方8bit、アルファなし、論理CPU数と並行実行数**に基づく選択を試作した。未知ビット深度・10/12bit・アルファ付き・小画像は**元のlibheif経路**（コーデックスレッド数を明示指定しない）へ戻す。4論理CPU以上で単独復号中は4、既に別の対象画像が1枚復号中なら2、2件以上なら0（元経路）とする。論理CPUが2なら単独復号で2に制限。

ソースでは`WIC_HEIC_THUMB_WORKERS_LAB`と`GITHUB_ACTIONS=true`かつラボ変数`WIC_HEIC_THUMB_LAB_WORKERS=a`が揃ったときだけ動作。共有`atomic`のアクティブデコード数はRAIIで例外時にも減らす。**生産1.1.28は変えず、この分岐のRelease x64だけテスト。** この制御は同一DLL／プロセス内のみで、異なるプロセスの復号競合まで管理できるわけではない。

[CI #40529006247](https://github.com/taku077/wic_heic/actions/runs/40529006247) **SUCCESS**。同一Windows Server 2022、同一4032×3024人工8bit HEIC、5回中央値・サムネイル256×192 Fant。すべて元画像RGBと縮小後RGBの厳密一致：

| 方式 | 初回サムネイル生成中央値 | 標準比 |
|---|---:|---:|
| 標準 | 1472.420ms | 基準 |
| 2スレッド固定 | 887.905ms | 約39.7%削減 |
| 4スレッド固定 | 641.188ms | 約56.5%削減 |
| 適応型auto | 642.065ms | 約56.4%削減 |

実行時`THUMB_WORKERS_POLICY_DECISION requested=a selected=4 width=4032 height=3024 luma_bits=8 chroma_bits=8 alpha=0 concurrent_before=0`が**5/5回**。適応型の選択が単なる理論値ではなく実際に動作した。合計20回の8bit 12MPサムネイル画素一致に加え、**14個の純粋ポリシー判定ケースPASS**（小画像、CPU2/4、並行実行数、6MP境界、10/12bit、アルファ、ビット深度未知）。別の公開人工`1080×1920`の`fixture-irot3-exif6-noaux.heic`は実際に**luma/chroma 10bit**と報告され、`requested=a selected=0`で元方式に戻り、標準側の同じ256px画素と一致。

**未確認**：本ユーザー実機のCPUでの相対速度、Windows10 Explorer初回表示、キャッシュミスの多数枚数処理、異なるHEIC bitstream、8bit+alpha/10/12bitの大きな実写真全体、フォトの回転・保存UI。このCI数値を完成版の高速化と称しない。今後、ラボ専用に異なるCOMフレーム2枚の同時サムネイル復号で競合を確認する。ただし、新しいMSIのインストールはまだ依頼しない。

研究資料：libde265のWPP/tiles並列 https://github.com/strukturag/libde265 、libheifの独立タイル並列とcodec内部並列 https://docs.rs/libheif-sys/latest/libheif_sys/fn.heif_context_set_max_decoding_threads.html 、ビット深度・alphaの事前判定 https://codegraph.jelmer.uk/libheif/1.23.1-1/libheif/api/libheif/heif_image_handle.h 。
