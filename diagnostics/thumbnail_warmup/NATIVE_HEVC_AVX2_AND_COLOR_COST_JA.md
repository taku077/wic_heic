# HEIC初回サムネイル：MSVC AVX2および色変換の実測（2026-10-21）

## 本研究の目的・安全条件
- ユーザーの実機Windows 10 i5第7世代でHEIC画像の初回サムネイル生成が遅い。MSI v0.3は若干改善、v0.4は適応型2/4スレッドで人工HEIC約460ms程度だがユーザー実機の改善は未評価。
- Windows Server 2022 GitHub Actionsの一時環境でのみビルドとベンチマーク。**MSIを作らない。Windows WIC/Photos・HEIC ManagerのCOM登録を変更しない。**
- GitHub Actions内で生成した人工12MP HEIC（4032×3024）だけを検証。元の人工HEICも変更していない（SHA-256前後一致）。
- ベンチ測定はWindowsエクスプローラーの画面初回描画時間ではない。人工画像1種類だけで実写真一般の性能を保証しない。

## MSVCでAVX2を実験的に有効化した
ソース上の課題（固定libde265コミット `4d45a6b36767318237771e1b3faf41773c4af4ad`）:
- `CMakeLists.txt` はMSVCの場合、`SUPPORTS_SSE4_1` を有効化するが、`SUPPORTS_AVX2` は有効化しないため、通常のMSVCビルドではAVX2 kernelsが組み込まれない。
- `libde265/x86/sse.cc` のAVX2ランタイム分岐はGCCの `__builtin_cpu_supports("avx2")` だけで、MSVCの実行経路がない。
- `libde265/x86/transform-dct-tables.h` でGCCのアラインメント属性が使われ、MSVCでAVX2カーネルを有効化するとコンパイルできない。

実装：`diagnostics/thumbnail_warmup/Enable_AVX2_Libde265_CI.ps1` で**CI限定でサブモジュールの作業コピーだけ**にMSVC対応を加える。AVX2専用オブジェクトだけ `/arch:AVX2`、普段のデコード経路は通常の命令セット。 `CPUID` / `OSXSAVE` / `XGETBV` を確認し、AVX2非対応CPUでは既存のSSE4経路のままにする。研究用DLLを2種作って、同じCOMプロセス内でABBA順に交互にデコードし、出力PBGRA全画素ハッシュ一致を必須にする。

## AVX2の性能（実測）
| 試験 | 従来版WIC CopyPixels中央値 | AVX2版中央値 | 短縮率 | 結果 |
|---|---:|---:|---:|---|
| [CI run39588519754](https://github.com/taku077/wic_heic/actions/runs/39588519754) | 444.75ms | 444.06ms | +0.16% | 画素完全一致、元画像不変 |
| [CI run39589113646](https://github.com/taku077/wic_heic/actions/runs/39589113646) | 444.27ms | 446.30ms | **−0.46%** | 画素完全一致、元画像不変 |

単一の人工12MP画像、2/4スレッドの適応デコード、同一CI内交互測定6回ずつ。CPU時間の大幅な改善はなし。**AVX2の2個の逆変換カーネルだけでは今回の問題は改善しないため、次期MSIには不採用。** AVX2自体が無効ではなく、HEVC処理全体に占めるそのカーネルの比率が小さい可能性がある。追加のネイティブ最適化がない限りこの方向は優先しない。

## RGB対YCbCrデコード比較
`wictest/isolated_shell/HeifColorAB.cpp` で同一人工HEICを `heif_decode_image()` から RGB24 または YCbCr420 で取得。Windows Server 2022、交互8回（各4回）、HEVCワーカ2/4スレッドで測定。出力色空間が異なるため**RGBとYCbCrの画素ハッシュを一致と扱わない**。取得した各フォーマット内では繰返し結果を検証した。測定対象は色空間指定に応じたlibheif全体のデコード経路であり、純粋な色変換処理だけの差ではない。

| ワーカ数 | RGB経路中央値 | YCbCr420経路中央値 | 差 | 参考：RGB経路対YCbCr |
|---|---:|---:|---:|---:|
| 2スレッド | 602.52ms | 572.88ms | 29.64ms | 約4.9% |
| 4スレッド | 425.83ms | 386.73ms | 39.10ms | 約9.2% |

CI：[run39589113646](https://github.com/taku077/wic_heic/actions/runs/39589113646)。この差は、RGBを生成するための色変換・メモリ配置等に伴う全体差の目安。**サムネイルは結局RGBへの変換が必要**であり、YCbCr出力の高速化率をそのまま製品の短縮率として使うことはできない。

**判断：ソフトウェアHEVC本体の復号時間が主要な遅延要因。** 色変換経路だけを変えても30%以上の大幅改善は難しい。

## 次に検証する本命候補（まだ実装・高速化未証明）
libheifの固定バージョンには**FFmpeg `libavcodec` HEVCデコーダープラグイン**（`libheif/plugins/decoder_ffmpeg.cc`）が組み込まれており、CMakeの `WITH_FFMPEG_DECODER` を有効化すると利用候補になる。プラグイン優先度はlibde265が100、FFmpegが90のため、両者をビルドしても自動的にFFmpegを使用するわけではない。libheif API `heif_decoding_options::decoder_id` で明示選択が可能な設計である。

研究の次の手順：
1. **隔離CIのみ**でlibheif＋FFmpeg(libavcodec)の固定バージョンを用意し、2/4スレッドでlibde265とFFmpegを同じHEICに対してABBA順で比較する。Windows用のFFmpegライブラリ同梱方式・依存DLL・ライセンス条件も確認する。既存WindowsのFFmpeg実行ファイルが自動的にリンクライブラリとして使えるとは考えない。
2. HEIC内の10bit・ICC・Exif／iROT／iMIR・HDR・アルファも品質試験し、全画素一致／許容差とCPUメモリ、処理失敗時のlibde265フォールバックを検証。
3. Intel Quick Syncなどハードウェア支援の可能性は別軸。GitHubの仮想CIはユーザーの第7世代Intel iGPUと同一条件でないため、CIで動くと仮定しない。
4. **現行MSIは変更しない。** 十分な高速化と元画像・関連付け保護が確認できた成果の区切りのみでMSIの実機評価を依頼する。

## 現在の状態
AVX2ビルド・2回の厳密な速度／画素比較、およびYCbCr対RGB経路の測定は完了。FFmpegデコーダーの組み込みと速度差検証は**未実施**、結果や改善率を予想していない。
