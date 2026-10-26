# Windows 10 Photos HEIC表示負荷・JPEG/PNG比較の検証仕様（2026-10-27）

## 出発点（実機症状）

- ユーザー環境：Windows 10 Pro 22H2、Core i5第7世代、専用GPUなし。現行WicHeic MSI 1.1.27を維持。
- HEICはJPEG/PNGと比較して、表示・回転・拡大縮小・保存の全体が明らかに重いと感じるとの実機報告。
- 同一人工HEICの録画では「縮小→高速回転→最初の表示崩れ→僅かに拡大→横長表示への固着」、画像だけ6–7秒消えてPhotos UIと寸法表示が残る事例があった。
- 観察したHEIC状態変更はirot/Exifだけで、圧縮HEVC画素や追加画像は全バイト保全。画面描画とファイル保存の時刻は同じとは限らない。
- **仮説：** HEVC/libheifの初回フルサイズ復号＋新規WICフレーム発行、Windows Photosの非同期描画・拡大縮小・回転要求が競合すると、画面更新順序を取り違える可能性。現時点では**時間的因果は未証明**。

## コード照合（重複開発禁止）

1. `wic_heic/HeicBitmapFrameDecode.cpp`の`DecodeImage()`は`m_Decoded`原子フラグ＋ミューテックスにより、同じCOMフレーム内では**初回に一度だけ**libheif復号。2回目以降の`CopyPixels`はRGBプレーンからmemcpy。既存の一度だけ復号キャッシュを重複実装しない。
2. `GetSize`はHEIFハンドルの表示寸法を返すだけでフルRGB復号を強制しない。JPEG/PNGとの比較も「デコーダー新規取得」「初回CopyPixels」「warm CopyPixels」「縮小」「回転」を分解する。
3. `CHeicBitmapDecoder::GetPreview()`はWINCODEC_ERR_UNSUPPORTEDOPERATION。画像内に適切なサムネイルがなければ`GetThumbnail()`はCODECNOTHUMBNAIL。WICの`IWICBitmapSourceTransform`は現行フレームのQueryInterfaceで公開されていない。このためフルサイズ画素を必要とする経路があり、Photosの小倍率表示でも高速部分復号ができると決めつけない。
4. **注意:** Microsoft WIC資料ではプレビュー／サムネイル不足時に大画像全体を復号して縮小するためコストが高いとされるが、PhotosがどのAPIをどの順で使用するかは未確認。サムネイルのない公開人工画像で観察した遅延を、すべてのiPhoneのHEICに一般化しない。

## 2026-10-27 GitHub実装

- `wic_heic/HeicBitmapFrameDecode.cpp`に`DecoderFullDecode`イベントを追加。実際の`decode_image()`呼出しを`GetTickCount64`で計測し、既存の**明示的オプトイン診断**に`elapsed_ms`、WIC-visible寸法、固定ラベル`libheif-cold`だけを記録。**ファイル名、パス、写真画素、EXIF、ユーザー情報なし。** COMフレーム1つに1回だけ、通常はOFF。既存`DecoderGetFrame`／`DecoderFrameGetSize`／`DecoderCopyPixels`と時刻で照合できる。
- `wictest/WICFormatLatencyProbe.h`を追加。同一公開人工HEICの表示RGBを使い、WIC標準エンコーダーで比較用JPEG/PNGをCI専用の**毎回新しいフォルダ**へ書き出し。同一1080×1920画像をHEIC/PNG/JPEGとして、**open＋GetFrame＋GetSize＋24BGR正規化、最初のフル画素CopyPixels、同一フレームの後続3回の平均CopyPixels、Fantで1/4縮小、90°回転**に分けてQueryPerformanceCounterで時間測定。各失敗はHRESULTで落とす。計測値だけで合否を決めない。
- `wictest/wictest.cpp`へ別モード`--perf-compare`を登録、`.github/workflows/repair-x64-build.yml`へ比較試験とプライバシー限定`DecoderFullDecode`発行の必須検査を追加。既存の50回回転、並列、Exif/ICC保持、縮小、MSI隔離導入・完全復元試験は変更しない。
- この比較対象は**人工グラデーション画像**であり、JPEGは非可逆圧縮。画像内容、同時バックグラウンド負荷、ディスクキャッシュ、CPU等に依存するので、CI runnerの数値をユーザーのWin10 PC実速度と断定しない。
- **実機への導入について：** 新しい1.1.27 MSIは配布しない。実機導入済み1.1.27のコードは変更しない。テストがすべて通ってから、必要なタイミングで進歩のあるMSIとして提供する。ローカル診断ツールv0.5.1は現状のまま使用可能。

## 優先する判断条件

- HEICの初回CopyPixelsだけが突出して遅い → 復号負荷、libde265スレッド制御、並列競合、サムネイル／プレビュー利用を調査。ただし既存高速化機能は重複開発禁止。
- WIC GetFrame/Initializeも突出 → HEIFメタ解析やストリームI/O、別デコーダー反復生成を調査。
- 初回は重いが同一COMフレームwarmは軽い → WICキャッシュは有効。Photosがフレームを毎回作り直しているかをログで確認。
- warm CopyPixels、Fant縮小、FlipRotatorが突出 → RGB転送、再サンプリング、rotation/viewport表示更新の処理を検査。
- WICの各段階が短いのにPhotosだけ数秒真っ黒 → Photosの非同期表示、UIレイアウト、画像サーフェス更新等を優先。ただしCIと実機の環境差を考慮。

## 参照文献（縦断）

- Microsoft: WIC BitmapSourceTransform https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapsourcetransform
- Microsoft: Bitmap scaler https://learn.microsoft.com/en-us/windows/win32/wic/-wic-bitmapsources-howto-scale
- Microsoft: Preview / Thumbnail no full-image decode https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapframeencode
- Microsoft: image decode performance https://learn.microsoft.com/en-us/windows/apps/develop/performance/optimize-animations-and-media
- Microsoft: WIC bitmap cache https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicimagingfactory-createbitmapfromsource
- libheif project & codecs https://github.com/strukturag/libheif
- libheif community performance case https://github.com/strukturag/libheif/issues/1787

## Windows CIでの段階別実測（2026-10-27、人工1080×1920のみ）

**Windows Actions #40356131847 SUCCESS**、既存テストとMSI隔離導入・復旧も合格。単発測定、同一人工画素をPNG/JPEGへ別名新規変換。下記数値はGitHub-hosted Windows Serverの一測定例であり、ユーザーのCore i5 Windows10に直接当てはめない。JPEGは非可逆である。

| 処理段階（ms） | HEIC | PNG | JPEG |
|---|---:|---:|---:|
| 新規デコーダー／フレーム準備 | 4.830 | 0.145 | 0.127 |
| うちCreateDecoderFromFilename | 4.789 | 0.136 | 0.121 |
| うちGetFrame(0) | 0.018 | 0.005 | 0.003 |
| うちGetSize | 0.004 | 約0.000 | 約0.000 |
| 初回の全画素CopyPixels | 59.344 | 13.140 | 6.661 |
| 同一フレームのwarm CopyPixels平均 | 2.290 | 12.259 | 4.917 |
| 1/4 Fant縮小 | 2.614 | 18.110 | 2.311 |
| 90度FlipRotator | 32.306 | 18.708 | 9.216 |

**結論:** WICの同一フレームRGBキャッシュは有効で、同一フレームの後続画素要求はHEICが速い例もある。実機で特に影響しそうな段階は**デコーダー作り直し／初回HEVC復号／回転レンダリング**。Photosの連続表示更新と直接つなげる因果は未確定。縮小がHEIC最速だった測定例もあり、「全工程が常にHEICだけ重い」と一般化できない。

**重要な計測の落とし穴:** 以前のCIでWIC内部診断ログをONにした場合、HEIC新規オープンが約144msと非常に遅く出たが、ログOFFでは4.830msになった。WICの診断器はWindowsのCreateFile/LockFileExを多数回呼び、HEICだけに追加のログI/Oコストが乗る。診断ONの数字をJPEG/PNGと同列に比べて性能回帰と扱わない。比較実験中は内部WIC診断をOFF、診断イベントの機能テストは別工程とする。

**診断ログの信頼性:** 既存の上限2MiBに到達してCommitEnter/CommitExitが欠けるCI事例を確認。#40356131847は偶然一致したが、上限到達間際で不安定。WICコードを変更して、**1.5MiB以降は通常イベントを抑止し、最後の512KiBを重要なCommit開始/終了/結果用に予約**。総上限2MiBは変更しない。さらにCIで普通イベント枠を埋めてから人工50回保存し、ペアが消えないテストを追加。結果が未確認の段階ではこれを完成と扱わない。

この診断追加コードは**GitHubの開発ブランチのみ**で、ユーザー実機のWicHeic v1.1.27にはまだ導入していない。新MSIをむやみに配布しない。

- 結果: https://github.com/taku077/wic_heic/actions/runs/40356131847
- Microsoft CreateBitmapFromSource: https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicimagingfactory-createbitmapfromsource
- Microsoft LockFileEx: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex

## 診断ログ上限の改修結果（CI確定）

- Windows Actions [#40357301514](https://github.com/taku077/wic_heic/actions/runs/40357301514) **SUCCESS**。通常負荷シナリオではCommitEnter/Exit = **192/192**件、総ログ1,631,988バイト、硬い上限2,097,152バイトを超えなかった。
- **意図的な飽和条件:** 1,572,824バイトの匿名ダミーファイルを先に置き、通常イベント用の1.5MiB枠をほぼ消費させてから50回の人工同一パス保存を実行。保存開始/終了ログが**50/50**件記録され、総ログ1,605,097バイトに収まり成功（WIC_TRACE_RESERVED_COMMIT_50_PASS）。元の人工画像と各世代のバックアップも保全。
- 保存処理の結果ログを含め**残り512KiBの保存優先枠**を確保しても、ユーザーの画像データ・パス・EXIFは記録しない。記録を延々と続けて最終的な2MiB上限までCommitが埋まる場合は無限の保証はないため、実際の監視は20分期限・2MiB上限を維持し、長時間実機ログでは上限到達を明示する。
- 関連するCIがChocolateyのNASM一時的504で失敗したため、依存バイナリの既存キャッシュがある場合に外部パッケージ取得を**回避**するCI修正も追加。機能・画像処理には影響しない。

この状態は**CI検証済みの開発ブランチ**であり、実機の1.1.27に反映されたわけではない。新MSI配布や端末の設定変更はまだ不要。

## 2026-10-27: 開発の現実的な到達目標と優先順位（ユーザー合意）

「JPEG/PNGと同等の体感速度」は達成保証できないため、ベンチマークの判定条件にしない。実機に現れる6〜7秒の画像消失や表示崩れは、HEICの復号が重いだけでは因果関係が未証明。まず初回表示のボトルネックを減らし、その後、Photos GUIで表示異常の発生が減るかを**独立**に判定する。

1. **第1段階（努力目標）:** 現行WicHeic 1.1.27比で、同一人工HEIC・同一CPU・同一経路の**初回全画素取得時間を10〜20％短縮**。CIの参考値59.3msなら約47〜53msに該当する。ただしGitHub-hosted CIのホスト差・測定ばらつきがあるため、別々のランナーの1回の計測結果を比較して達成と呼ばない。30％は伸びしろが確認できた場合だけの上位目標とし、実現を約束しない。
2. 実機体感に直接効く総所要時間（フォトで新たに開いた瞬間から正しく画像が表示されるまで）を別の指標にする。WIC初回復号20％短縮≠画面の総初回表示20％短縮。
3. 高速化の品質ゲート：回転を何度も繰り返した際の縦横比、RGB完全一致を要求する無再圧縮変換、ICC/Exif/全付属フレーム保持、既存原本不変更、MSI隔離・登録復元などに回帰が無いこと。
4. **表示不具合の合否も別判定:** 縮小・連続回転・表示乱れ・僅かな拡大・横長固着、画像だけの6〜7秒間消失の頻度と再現条件を録画で検証。単に数十ミリ秒の改善で表示不具合が解消されたと主張しない。
5. MSIは区切りのよい改善でのみ配布。現行1.1.27の端末環境・HEIC Manager・ユーザー原本に干渉しない。

### 実装済み機能の重複回避と新発見

- 旧v0.4.0時代に1枚のHEVC復号内2/4ワーカーの調整を実施。現行1.1.27のWICフレーム自体もm_Decoded + mutexで同一フレームの1回だけ復号が実装済み。これらの再実装はしない。
- 同梱libde265の固定コミット **4d45a6b36767318237771e1b3faf41773c4af4ad**（2026-06-29）は、libde265 **v1.1.1（2026-06-03）の後続2コミット**に位置する。公式が公表した「x86 で平均約8％、HEICタイルの起動約3％高速化」は**現行固定コードへ既に含まれている**。8％を新たに得られるという前提で旧版を更新し直さない。
- libde265 v1.1.3 は2026-09-14の公開最新版で、高ビット深度とRange Extensionsの修正、v1.1.2のセキュリティ修正を含む。ABI/API互換をうたうが、HEICの回転・表示・10/12ビット・CI・Photosの互換性テストが完了する前にsubmoduleを勝手に更新しない。**アップグレードによる8％上乗せ**など根拠のない速度目標を掲げない。
- 「PhotosとCIテストホストで使用可能な回転ソースの提供先」と「一般ホスト（Explorerその他）」を事前に判定する改修を行った。以前は一般ホストで全HEIFをIStreamから読み直した**後**にPhotosPureRotationBridge::Offerが拒否していた。今後はPhotos以外では無駄な全ファイル再読込を省略。Photosの回転保存経路は変更しない。実速度改善率は未測定。
- wictest/WICFormatLatencyProbe.hは1回だけの計測ではノイズに弱いため、5回×3形式を交互に新しいWICデコーダーで開き、初回画素取得／準備／90度回転の**中央値**を出力する機能を追加。各回の比較実験では診断ログによる余分なディスクI/OをOFF。少量の人工画像だけでは、一般の大容量iPhone HEICに一般化できない。
- 次の候補はWICのIWICBitmapSourceTransform、HEIF埋込サムネイル/プレビューの適切な利用だが、**現在の人工画像にプレビューがなければ、フルHEVC復号が不可避な部分がある**。PhotosのAPI呼出しを確認し、画質の安全性と十分な速度短縮が見込める場合のみ実装。

外部資料:
- libde265 公式Release一覧（v1.1.1, v1.1.2, v1.1.3）: https://github.com/strukturag/libde265/releases
- libde265 公式SIMDとマルチスレッド: https://github.com/strukturag/libde265
- Microsoft WICのTransformインターフェイス: https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapsourcetransform
- Microsoft WICサムネイル未提供時のフルデコードコスト: https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapframeencode

## 2026-10-27: 不要な元ファイル再読込を排除・再現性のある性能計測へ

- `wic_heic/HeicBitmapDecoder.cpp`のGetFrame()では、Photos専用の無再圧縮回転ソース候補としてIStreamからHEIFファイル全体を読み直してから、対象プロセスかどうかを調べる旧経路があった。この処理はPhotosとCIハーネス以外（Explorerその他）では結局候補不採用になるため、**事前にCaptureAllowedHost()を判定して対象外ホストでは元ファイル再読込を省略**した。Photos側の候補選択と安全な回転保存は変更しない。初回表示全体・復号そのものを10％改善したという意味ではなく、効果は未測定。
- `CHeicBitmapDecoder::Initialize`内の`pIStream->Stat(STATFLAG_DEFAULT)`は、実際のパーサーでは使用しないファイル名を余分に取得し、割り当てたSTATSTG.pwcsNameの開放もなかった。すでにCHeifStreamReaderが`STATFLAG_NONAME`を使ってサイズを取得するため、**重複Statを排除**。コンテナー解析時間の`DecoderContainerParsed`イベントを既存のオプトイン匿名診断へ追加。パス・EXIF・画像内容は記録しない。これも速度改善率未測定。
- 1回測定の性能揺らぎを減らすため、人工HEIC/PNG/JPEGの`--perf-compare`を**各5回、新しいWICデコーダーで交互に実行し中央値を出す**よう変更。全画素取得と90°回転、デコーダー準備を別々に報告する。
- Windows Actions [#40372545516](https://github.com/taku077/wic_heic/actions/runs/40372545516) と [#40372659287](https://github.com/taku077/wic_heic/actions/runs/40372659287) は中央値計測・既存機能テスト・MSI登録復旧をすべて通過。人工HEICの初回全画素取得の中央値は約51ms／53ms。ただし**以前の約59ms（別CI・1回測定）との差を改良の効果と断定できない**。同じテスト群でもコンテナーオープン準備は以前の約4.8msから別の5回中央値約138msへ変動した。実行環境／コーデック初期化／診断設定の影響をさらに分離して比較する必要がある。
- 最優先の判断は**正しいファイルを壊さず、同じWindows 10・同じ人工HEIC・同じ操作経路で改善すること**。初回画素取得10〜20％短縮は検証すべき努力目標であって、CIの別ランナーの一測定で達成認定しない。改善が小さい案は不用意に統合しない。


## 追加CI完了（速度値の誤認防止）

- Windows Actions [#40373544936](https://github.com/taku077/wic_heic/actions/runs/40373544936) **SUCCESS**。COM/WIC本体のビルド、人工HEICの5回中央値ベンチマーク、50回高速回転・Exif・ICC・複数フレームの保存、重要Commitのペア記録50/50件、MSI隔離導入/完全登録復元を確認。Source変更コミットa9a97f56、CIイベント要求コミット766809f7を含む。通常診断は明示Opt-in／既定OFF。
- 今回の中央値（ms）：HEIC 初回画素 43.346・90度回転21.538・準備139.261、PNG 初回8.240・回転8.974、JPEG 初回4.768・回転5.499。前回別CIではHEIC 初回51.122・回転25.703、PNG 初回9.651・回転10.652、JPEG 初回5.602・回転6.796。
- **重要:** 変化率はHEICだけでなくPNG/JPEGも近い割合で速くなっている。したがって**HEICだけの15％短縮を達成したとは言えない**。同一ランナーで変更前／変更後のペア測定を行い、HEICに固有の優位性があるか判定するまで速度効果は「未確定」。見かけの改善率を成果としてMSIをユーザーに試させない。
- 検査からはコンテナー準備約139msの持続が確認されており、この工程の内訳もDecoderContainerParsed等で確認する方針。これらは測定のための補助であり、Photosの表示破綻を解決した証拠ではない。

## 2026-10-27：小規模改善を積み上げるための開発ルールと実装台帳

ユーザーの方針：**一つ一つの改善幅が小さくても、積み重ねと組合せで10〜20％に達する可能性を評価する**。個別パッチの効果が小さいことだけを理由に直ちに破棄しない。一方、重複処理、予期しないレイアウト変化、メタデータ消失、体感悪化、過負荷、診断漏れがあれば無理に統合しない。

### 改善項目と対象領域

| 項目 | 技術上の削減 | 実装 | 速度向上の認定 |
|---|---|---|---|
| 1. 不要なStat取得 | 使われないファイル名のSTATと未解放メモリ | 2026-10-27既存済み | 未測定 |
| 2. 非Photosホスト向け余分な全HEIF読み直しを省く | GetFrame中のCopyOriginalBytes/Offer不採用 | 2026-10-27既存済み | 未測定。Photos保存経路は対象外 |
| 3. HEIFストリームのReadごとのSeek(CUR,0)を省く | libheif多数の小Readごとに追加されるCOM呼出し | `wic_heic/HeifStreamReader.cpp` commit `78bdbbc1d024` | CI安全性結果待ち・時間効果未測定 |
| 4. トップレベル画像ID列挙を1回に集約 | GetFrameCount/GetFrameでのlibheif再列挙・std::vector再確保 | `wic_heic/HeicBitmapDecoder.h/.cpp` commit `7d9dd12c4444` | CI安全性結果待ち・時間効果未測定 |
| 5. スレッド復号・16bit LUT最適化の再確認 | 研究ブランチに既存成果。既存の2/4スレッドやSSE実装と重複禁止 | 一部既存／研究のみ | Windows10 Photosの初回表示に実装済みとは限らない |

- Microsoft ISequentialStream::Readは**実際に読んだバイト数だけシークポインターを進める**契約。従来は每回Read成功後にSeek(STREAM_SEEK_CUR,0)を呼び戻り値でm_LastPosをセットしていたが、成功時は`m_LastPos += nRead`と等価。エラー時は従来同様-1を返し、ポインターを増やさない。sizeがULONGより大きい場合は安全拒否。外部の並行アクセスによるストリームポインター変更にはもともと対応しておらず、IStream/readerの単一所有を維持する。
- HEIFコンテナーはInitialize終了後にimmutable。画像一覧を一度だけ取得し、GetFrameCount/GetFrameで共有。2画像付き人工HEICを300回読み直す回帰テスト `wictest/WICDecoderFrameEnumerationProbe.h` を追加。プライマリー1080x1920、セカンダリー寸法、RGBの同一性を検査。写真原本には触れない。
- **速度判定ルール：** 各候補の一次結果は人工画像のCI合否、時間測定。組合せ効果は以前と同じPC/OS/fixture/電源状態で、旧バージョンと候補を前後比較し、複数回の中央値とばらつきで判断。別CIランナー間の差を「10％達成」と呼ばない。初回フル画素時間10〜20％短縮の目標を維持。コードの静的削減が確認できた場合も、「実機速度改善」とは区別する。
- **異常改善は別の合否：** Zoom→回転→表示崩れ→僅かなZoom→固着、数秒画像消失を解決したと推測しない。v1.1.27の安全な元バージョンを温存、Windows10 Photos実機GUI未確認の修正MSIは安易に提供しない。

資料：
- Microsoft ISequentialStream::Read: https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-isequentialstream-read
- Microsoft IStream::Seek: https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-istream-seek
- libheif: https://github.com/strukturag/libheif
- Microsoft WIC transforms: https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapsourcetransform

### 累積最適化の安全性CIと追加項目

- 現行作業ブランチの累積変更のうち、HEIF ReaderのRead→Seek(CUR,0)削減、画像IDリストの保持、300回列挙RGB検証は **Actions #40376025882 SUCCESS**。人工2フレームを300回取得し`WIC_HEIF_IMMUTABLE_ENUMERATION_300_PASS`、各50回保存、画像・Exif・ICC・登録復元を合格。
- プロセスの実行ファイル名は生存中不変であるため、`PhotosPureRotationBridge::CaptureAllowedHost()`の判定を**初回1回だけ評価してキャッシュ**した。従来はGetFrameとOfferでGetModuleFileNameW／複数の名前比較を実行していた。初回の許可結果と対象ホストは一切変更しない。コード変更コミット`d884da814fee`。これも速度効果はまだ未計測、WindowsCIは結果に従って更新する。
- 小改善の価値は各パッチの削減作業量だけでなく、実際のPhotos表示の時間と原本保全を基準に判定する。変更同士が同じボトルネックを取り除く場合、削減率を単純加算しない。
