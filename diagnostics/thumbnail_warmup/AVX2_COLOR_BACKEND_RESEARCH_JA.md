# HEIC初回サムネイル高速化：MSVC AVX2／色変換の研究結果（2026-10-21）

## 研究目的・範囲
埋め込みHEICサムネイルがない写真でも、フルサイズHEVC画像からの初回サムネイル生成を大幅に短縮したい。Windows 10 x64／第7世代Core i5での利用を念頭に、CPU SIMD（AVX2）とlibheifの色空間変換コストを隔離CIで検証する。MSIを作らず、ユーザーの実写真やWindows WICの登録は変更しない。

## AVX2の実験実装
- libde265上流の**固定したサブモジュールバージョン**に対して、CIランナーの一時作業ツリーだけにパッチを当てる。実際に配布するDLLやライブラリへは適用しない。
- MSVCの `SUPPORTS_AVX2` 有効化、16x16／32x32逆変換カーネルのAVX2専用コンパイル、CPUID／OSXSAVE／XGETBVによるランタイムCPU判定、MSVC 64byte配列整列互換性を実装した。
- Baselineのlibde265とAVX2版を別々のプライベートWIC DLLとして同一CIランナーに組み込み、12MPの人工4032×3024 HEICで6対6のABBA順 `CopyPixels` 比較。
- 対応CPUであること、AVX2カーネルがビルドされたこと、異なるDLLができたこと、両経路の32bpp PBGRAハッシュ完全一致、元の人工HEICのSHA256維持を検証。

| CI | Baseline中央値 | AVX2中央値 | 差 | 判定 |
|---|---:|---:|---:|---|
| [39589113646](https://github.com/taku077/wic_heic/actions/runs/39589113646) | 444.27ms | 446.30ms | **0.46%遅い** | 成功・改善なし |
| [39589825252](https://github.com/taku077/wic_heic/actions/runs/39589825252) | 507.92ms | 505.99ms | **0.38%速い** | 成功・ほぼ同じ |

いずれもWindows Server 2022の合成12MP画像、CPUと物理メモリ条件は実機と一致しない。CPU時間は概ね同程度。**MSVC AVX2に実用的な改善はないため採用・MSI配布しない。** 先行したCI失敗は実験のソース変更検知が2ファイル前提のまま3ファイルへ拡張されたことが原因で、検知条件を修正して再試験成功。

## libheif内部：YCbCrとRGB経路の比較
[39589113646](https://github.com/taku077/wic_heic/actions/runs/39589113646) と [39589825252](https://github.com/taku077/wic_heic/actions/runs/39589825252) で、同じ人工HEICについてlibheifからYCbCr420出力する場合とRGB24出力する場合を比較した。

| CI | HEVCスレッド | RGB24中央値 | YCbCr420中央値 | 差 |
|---|---:|---:|---:|---:|
| 39589113646 | 2 | 602.52ms | 572.88ms | 29.64ms |
| 39589113646 | 4 | 425.83ms | 386.73ms | 39.10ms |
| 39589825252 | 2 | 628.43ms | 578.16ms | 50.27ms |
| 39589825252 | 4 | 483.49ms | 446.01ms | 37.48ms |

**これらは別の色空間・画素形式の出力時間であり、WICサムネイルの画素完全一致を確認した実装ではない。** RGB変換省略は数十ミリ秒の改善候補だが、現行の全体約0.4～0.5秒を大幅に変える単独の突破口ではない。YCbCr420をそのまま表示に使うこともできない。

## 次の研究：別HEVC復号エンジン
固定バージョンのlibheifには公式の `libheif/plugins/decoder_ffmpeg.cc` が含まれ、FFmpegのHEVC（`AV_CODEC_ID_HEVC`）復号に対応する。libheif CMakeの `WITH_FFMPEG_DECODER` は既定でOFF。FFmpegの`avcodec`／`avutil`をリンクしてプライベートlibheifコーデック内で試す候補がある。

研究には、固定版FFmpegのWindowsライブラリ入手・ビルド、両HEVCバックエンドを分離した比較、向き/ICC/10bit/alphaなどの差分、4枚同時処理・CPU/メモリ、Windows 10非干渉を順次試す必要がある。libheifのプラグイン名に「HW accelerated」と書いてあっても、実際にIntel GPUのハードウェアHEVCデコードが選択されるとは限らず、その保証はない。Win10実機のQuick Sync可用性については別途安全な診断が必要。

## 不変条件
HEICの原本を変更・削除・再エンコードしない。隔離CIだけで試す。HEIC Managerや「フォト」への関連付けを操作しない。色・画素・向きが合格しない最適化は実機MSIとして公開しない。MSIは十分な改善が出た区切りのみ配布する。
