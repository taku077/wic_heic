# 2026-10-21: in-process AVCodec GPU HEVC 初回サムネイル高速化研究

## このブランチの位置づけ
- 親：`experiment/heic-gpu-d3d11va-firstframe` の成功コミット `e950937a`。原本保護、HEIFメタデータ照合、ネイティブAnnex B抽出（8件）、悪意あるNAL拒否等は維持。
- 新研究：`experiment/heic-gpu-inprocess-avcodec`。従来の研究用 `ffmpeg.exe` 外部プロセスを各画像で起動する形から、同一C++プロセスで `libavcodec` を呼び出す方式へ移行する検証コード。
- Windows標準WIC、Photos、HEIC Manager、既存MSIのDLL／COM登録を変更しない。GPU経路は製品から無効のまま。

## 新規コード
1. `wictest/isolated_shell/HeicInprocessGpuAB.cpp`：
   - FFmpegライブラリのHEVCソフトウェアdecoderと、条件付きの D3D11VA HEVC decoderを直接呼ぶ。
   - FFmpegのHW decoderの構成で `AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX` および `AV_PIX_FMT_D3D11` を要求し、 `get_format` ではソフトウェアへ黙って戻さず拒否。
   - 実GPUテクスチャを得た場合だけ `av_hwframe_transfer_data` でNV12/P010をCPUへ読み戻してBGRAへ変換。GPU失敗ならGPU成功と記録せず、不採用。
   - HW deviceは1回のプログラム呼び出しで2画像間再利用。ソフトウェア基準と同じ、先行CIで主画像メタデータが安全判定されたMain10の`04.heic`と`fixture-irot3-exif6-noaux.heic`に限定。
   - `RUNNER_TEMP/heic_native_bridge/` の事前検証済みAnnex Bのみを読み、他のHEIC原本に触れない。出力は新規`RUNNER_TEMP/heic_inprocess_software`/ `heic_inprocess_gpu` にしか保存しない。
   - GPU初期化、ストリームread、frame decode＋BGRA、バッチ総経過時間、CPU時間、プライベートメモリを分離測定。未回転BGRA画像を保存してFFmpegで復号した同一Annex Bからの参照とSHA256比較。
2. `wictest/isolated_shell/HeicInprocessGpuAB.vcxproj`：独立実行ファイル。FFmpeg の avcodec, avutil, swscale をDLLで使用。**MSI/WICライブラリにはリンクさせない**。
3. `diagnostics/thumbnail_warmup/Prepare_FFmpeg_Inprocess_GPU_SDK_CI.ps1`：過去の成功済 `Prepare_FFmpeg_SDK_CI.ps1` の固定SHA-256 FFmpeg 8.1 LGPL shared SDKを転用し、swscale.lib も生成する。
4. `wictest/isolated_shell/D3D11HevcCaps.cpp`：従来4032x3024のHEVC Main10判定だけでは2MP級HEICを不必要に除外しうるので、`04.heic` 2048x2048 と *未回転のHEVC coded surface* 1920x1080 のdecoder configurationを追加照会（HEIFの表示寸法1080x1920はirot適用後）。**同一アダプタが両方対応した場合のみGPU A/Bを試行**。
5. `.github/workflows/heic-gpu-path.yml`：native Annex Bと回転照合の後に、1プロセスのAVCodec経路をコンパイル。ソフトウェア経路は2件完全一致必須。実GPUハードウェアがないCIでは `NOT_PROVEN`。GPUでの画素差・不一致は報告だけで、製品採用しない。
6. `.github/workflows/heic-inprocess-fast-guards.yml`：コードの安全ガードやFFmpeg準備スクリプトの構文を確認する軽量CI。

## 検証状況と未解決
- `e950937a` の先行研究CI2本成功は確定。先行GPU試行ではWindows CIに実HWアダプタなし、GPU復号は未実証。
- **新C++の総合CIは新ブランチで実行して結果を確認するまで未認定**。コードのGitHubコミット完了とWindowsコンパイル成功、画素一致を混同しない。
- CPU baselineのunrotated BGRAとFFmpeg CLIの参照が厳密一致するかはWindows CIの結果次第。10bit変換時の色マトリクス差は確認してから扱う。
- 実GPUでAV_PIX_FMT_D3D11 frameが得られ、hwframe transferが成功し、出力の回転、ICC/NCLX、10bit色変換、alpha対応、256pxサムネイルの画質が再現できるまではユーザー配布なし。
- 2件の研究A/Bで初回Explorer高速化は証明できない。将来、10～30件の同一条件ABBA（プロセス初期化時間込み）とGPU対SWのCPU/メモリ/画質を確認。最大30%以上等の明確な改善の目途が立つまでMSIを作らない。

## 2026-10-22 継続調査：Windows実測で発見した色再現性差分
- `fd6443a` のWindows CI：固定版FFmpeg SDK取得成功、MSVCで`HeicInprocessGpuAB.cpp`コンパイル成功。
- `04.heic` は元Annex BをCLIでBGRA復号した画像と、libavcodecプロセス内復号BGRAが **SHA256およびバイト数で完全一致**。
- `fixture-irot3-exif6-noaux.heic` はHEVCフレーム復号には成功したが、BGRAのSHA256が参照値と不一致（8,294,400byteの寸法一致のみ）。
- 計測値はCi上の研究用wall時間：04 HEVC約31〜37ms、もう1枚約16〜21ms。これはExplorer表示時間でも第7世代Intel実機の速度でもない。
- CI失敗は意図した画素一致ゲートの正常動作。2枚目を一致とみなしたり、GPU高速化ができたと主張しない。
- `d23c682` でHEVC AVFrameのYCbCr matrix・量子化rangeに基づきlibswscaleの`sws_setColorspaceDetails`を呼び、BT709/BT2020/601等既知の行列だけを受理。未対応行列はfail-closed。
- `874ee16` までに、色変換前の10bit `YUV420P10LE` を診断専用に保存し、CLIの同一Annex Bから復号した10bit YUVをSHA256比較するCIを追加。これで2枚目の差がHEVC復号由来かYCbCr→BGRA変換由来かを独立に識別する。
- **この新規色変換変更と10bit YUV raw parityのCI合格は未確認**。該当SHAのActionsステータスが確認できてから合格と認定すること。
- 過去のCIでGPU実ハードウェア未搭載、D3D11VA実復号未実証。製品WIC/Explorer、MSI、Photos、HEIC Managerは一切変更していない。
- 通常プッシュ時の総合ネイティブ再構築は抑え、絞ったMain10プロセス内復号CI + 軽量ガードCIによりフィードバックを得る構成へ変更。

- `e08a93d`／`4e18e10`：iRot変換後の表示寸法（1080x1920）とHEVCデコード面（1920x1080）の混同を修正。D3D11 Main10構成照会は表示寸法ではなく圧縮フレームのcoded寸法で実行する。この修正のGPU実機性能は未測定。

## 2026-10-22：実測で到達した段階と、初回サムネイルの改善可能性

### 今回確定したCI成功
- [Windows in-process Main10 2件の全BGRA一致 #39625796537](https://github.com/taku077/wic_heic/actions/runs/39625796537)。04.heic（2048×2048、10bit、MPEG limited）とfixture-irot3-exif6-noaux.heic（HEVC coded frame 1920×1080、10bit、JPEG full range）、ともにCLIとプロセス内AVCodecのBGRA全バイトSHA256が一致。フルレンジをlimitedと誤解していたことが従前の2枚目BGRA不一致原因。
- [raw YUV10とBGRAの独立完全一致 #39625968928](https://github.com/taku077/wic_heic/actions/runs/39625968928) および [#39625979425](https://github.com/taku077/wic_heic/actions/runs/39625979425)。2件ともHEVCデコーダー直後の planar YUV420P10LE と BGRA 各SHA256が一致。
- [フェーズ別タイミングのビルド＋品質CI #39626009667](https://github.com/taku077/wic_heic/actions/runs/39626009667) が成功。Windowsホスト上の1回の値（複数回中央値ではない、GPU使用なし）：
  - 04.heic: HEVC codec open 0.412ms／decode 19.241ms／BGRA color 10.248ms／計29.902ms。
  - 縦長irotサンプル：codec open 0.197ms／decode 10.360ms／BGRA color 5.365ms／計15.923ms。
  - Annex B本体の読み込みは0.056msと0.100ms、処理単体の計測とは別。
  - この2枚は小さい／圧縮率が高い固定素材。12MP実写HEIC、Explorerの初回表示、Intel GPUの値に外挿しない。

### 初回表示速度の仮説モデル（**予測実測ではない**）
現実の1枚約1秒はユーザーの体感値。12MP人工素材の過去別CIで復号400～500ms程度。これらは別計測なので合算はできない。説明用に仮に表示計1000msのうち置換対象450ms／その他550msとした場合：
- GPU+readback+BGRAの総時間400ms：表示950ms、5%減。
- 250ms：表示800ms、20%減。
- 150ms：表示700ms、30%減。
- 100ms：表示650ms、35%減。
純粋なHEVC GPUデコードが高速でも、読み戻し・色変換・Fant相当の縮小・Shell起動/キャッシュで利得が減る。ハードウェアデコード成功前に「実機30%改善」と断定しない。対応HEICのみGPU経路を使用、未知の色空間・alpha・ICC・irot/imir/clapは従来経路へfail-closed。

### 研究の段階（段階の完了はテストの成否で認定）
1. HEICの主画像／メタデータ／ネイティブHEVC Annex Bの可逆抽出／表示行列ガード：**完了（別CIで7/7、8/8）**。
2. 外部プロセスなしのHEVC Main10 C++ソフトウェア復号と画素基準の独立検証：**2件で完了**（7件全般、ICC/HDR、実写真未カバー）。
3. 実Intel GPUでD3D11VA Main10の`AV_PIX_FMT_D3D11`フレームを取得、`av_hwframe_transfer_data`でCPUへ読み戻し、画素/色/回転を検査：**コードあり・未実証**。GitHubホストCIにはハードウェアGPUなし。
4. フル12MPのGPU初期化、復号、P010転送、BGRA色変換、WIC Fant縮小の同一マシン測定／10～30件並列比較：**未実施**。
5. WIC thumbnail handlerへ私的に導入、原本・Photos・HEIC Manager非干渉、実機初回表示30%以上などの確認、MSIへの移行：**未実施**。

### 将来の性能試験で求める合格条件
- 一枚の初回表示：ユーザーPCの同一写真・同一表示サイズ・キャッシュ状態・同一CPU/GPU環境でswとGPUの実測中央値。
- 実ハードウェアデコードの証拠（DXGI adapterとD3D11 texture、readback成功）なしに速度比較しない。
- 256px thumbnail画像の向き・サイズ・画質・PBGRA・ICC/色、元HEIC SHA256不変、alpha/10bit/HDR/合成変換fallback、並列10～30件のCPU/メモリ上限。
- GPU初期化の使い回しによる安全性と、単体の高速化が消える並列過剰負荷に注意。

## 2026-10-22：人工12MP Main10で直接計測（今回の最新実装）
- `wictest/isolated_shell/HeicInprocessGpuAB.cpp` に隔離モード `software12` を実装。HEIC原本は使わず、GitHub CIの一時領域で `testsrc2`→`libx265` の10bit HEVC Annex B 1フレームを生成し、同じ入力からFFmpeg CLIとC++インプロセスAVCodecを復号してSHA256比較。
- [CI #39626938390](https://github.com/taku077/wic_heic/actions/runs/39626938390)：成功。4032x3024 10bit（48,771,072 BGRA bytes）が完全一致。HEVC入力は460,190 bytesでかなり圧縮容易な人工画像。
- 1回だけのタイミング：codec open約0.374ms、HEVC decode約70.857ms、BGRA変換約26.704ms、合計約97.938ms。プロセス全体バッチのwall約136.657ms。GPU使用なし。Explorer初回表示やオリジナルiPhone実写の測定ではない。
- **過去の人工12MP（HEVC抽出約4.65MB）の約400–500msと比較して「4x高速」と認定しない**。圧縮内容・素材・CPU負荷条件が異なり、改善率として無効。
- GPU実ハードウェアに関する試験はまだ0件。第7世代CoreのHEVCハードウェア対応は公式仕様から可能性があるものの、対象PCでのD3D11VA Main10デコード・P010転送・色管理は未確認。
- 次の研究で重要なのは、高複雑度12MPの同一フレームでSW/GPUのdecode + readback + BGRA + WIC Fant thumbnailまで含む同一PC実測、iPhone実写真への適用可能性、10～30件並列時のCPU/メモリ。

## 2026-10-22：WIC Fantと10bit YUV直接縮小を分離した12MP A/B研究
- Windows標準WICの`WICBitmapInterpolationModeFant`（4032x3024の全BGRA → 256x192）を参照。別研究器`HeicThumbnailScaleAB.cpp`で、元HEVCから復号済みの同一BGRA/YUV420P10LEを使用。CIテストは研究ソフトだけでありWIC製品DLL未変更、MSIなし。
- [CI #39628670883](https://github.com/taku077/wic_heic/actions/runs/39628670883)：Fant中央値25.128ms、YUV→BGRA直接Area縮小4.013ms。縮小工程のみ約84%短縮だが、RGB MAE2.625787、PSNR27.556dB、最大誤差119で**不採用**。
- [CI #39629229826](https://github.com/taku077/wic_heic/actions/runs/39629229826)：フルBGRA→swscale Area はMAE0.010844、PSNR67.541dB、最大誤差4。したがって大差の主因はFantとAreaの縮小フィルタ差ではなく、縮小をRGBへの色変換／クリッピングより前に実施する計算順序。
- [CI #39629683463](https://github.com/taku077/wic_heic/actions/runs/39629683463)：同じ人工12MP Main10でYUV→中間BGRA→WIC Fantに分割すると、512px→256は中央値6.246ms、MAE0.478509／PSNR45.800dB／最大13。1024px→256は10.294ms、MAE0.323778／PSNR49.967dB／最大12。2048px→256は24.216ms、MAE0.311822／PSNR51.770dB／最大10。比較時のWIC Fant中央値27.811ms。いずれも画素完全一致ではないが、1024px経由が品質・時間のバランスがよく、**製品不採用の研究候補**。
- 同CIの単発12MP HEVCデコード88.163ms、全BGRA変換43.723ms、デコーダ初期化0.441ms。WIC Fant 27.811msを合算した画像処理時間約160msと、同じ復号88.163ms／初期化0.441ms／1024ハイブリッド10.294msを合算した約99msを比較すると、**この素材の画像処理部分のみ約38%削減する可能性**。別プログラム間の独立測定の加算であり厳密な一体測定でも、エクスプローラー初回表示時間でもない。GPU未使用。
- ハイブリッドの「約38%」をユーザー実機の初回サムネイル表示の改善率として使わない。HEIC解析、サムネイルCOM起動、回転、ICC、PBGRA、Fant、メモリ転送、保存などは別検証。
- 高解像度でも全BGRA約49MBを回避できる設計上の可能性あり。ただし今回のA/B計測器は比較用にフルBGRAとYUVの双方をメモリへ保持するので、プロセスメモリ削減を証明していない。
- 詳細ノイズを加えた別の4032x3024 Main10人工フレームで同じA/Bを行う`software12detail` CIを追加。**この別素材の結果はCIを確認するまでは未確定**。

## 2026-10-22：第7世代Intel内蔵GPUの実復号確認用パッケージを完成（ただしGPU結果は未取得）
- 研究コード：`wictest/isolated_shell/HeicD3D11PortableProbe.cpp`。`AV_PIX_FMT_D3D11` フレームを得たときだけD3D11VAハードウェア復号と認識し、`av_hwframe_transfer_data`でCPU側へ読み出し、ソフトウェアと色/画素を比較。暫定的に`max_error<=4`かつ`MAE_RGBA<=0.25`でなければ成功判定しない（製品画質判定ではない）。
- 仮想GPUやIntel以外のアダプタはハードウェア成功扱いしない。FFmpegのD3D11デバイスを画像間で使い回す。システムコーデック登録やMSIなし。
- **12MPを実際に復号する試験を追加**：2048x2048 Main10、1920x1080 Main10（irot後は縦長）、高精細4032x3024 Main10（人工HEVC約4,296,647バイト、SHA-256 `157D80CEFAAA41C7BA89F62DE0A719DF894EBB6E07DAA43DE08623CB64D8E339`）の固定3サンプルを同梱。小さいフレーム成功は12MP成功を保証しない。
- [本番コード非変更、実行用CMDまで通したActions #39634317181](https://github.com/taku077/wic_heic/actions/runs/39634317181) は**success**。C++実行ファイル/MSVC静的ランタイム生成、固定FFmpeg LGPL SDK SHA-256検証、必要な4DLLのみ同梱、外部FFmpeg/VCToolsをPATHから隠したままCPU 3/3デコード成功、D3D11 caps情報表示、実際のWindows CMD実行、ユーザー診断ログの一意な保存まで成功。
- 同CI上ではDXGI index0/1がMicrosoft Basic Render Driverで実GPUはなし。GPU Main10ハードウェア復号・速度は**未実証**。誤ってGPU成功と表示しないことを確認。
- 検証済み成果物：Actions artifact名`HEIC_GPU_ReadOnly_Probe_x64_Research`、Run `39634317181`、artifact id `12360632681`、ZIP `49,551,139`バイト（2026-11-04 期限）。ユーザーの元HEICには触れず、固定3ファイル以外を読まず、コーデックと関連付けを変更しない。署名なしのexeのため配布先とコードの確認が必要。
- 実機確認が成立した場合のみ、Intel GPU初期化時間/HEVC復号/CPU転送/色変換を評価し、12MPのWIC/FantやExif/ICC/回転と結合する。50%以上のGPUハードウェア単体加速を確認しても、初回Explorer表示の改善率とは別に扱う。
- 高負荷人工12MPのCPU参考：過去の別CIでHEVCソフト復号約421.6ms、BGRA約24.5ms、Fant約18.1ms。**同じ第7世代Intel実機の測定ではないため、GPU改善率を断定しない**。

## 2026-10-22：Intel HD Graphics 620実機における初めての12MP Main10 GPU復号成功

提供された研究用診断テキストを手元で閲覧し、**生ログやパソコンのファイルパスはGitHubへ掲載しない**。以下は技術的集計のみに限定する。

- DXGI 実アダプター `vendor=8086 device=5916`、Intel HD Graphics 620（非WARP）。HEVC Main 10/DXGI P010の4032x3024 D3D11デコーダー設定数5。2048x2048と1920x1080でもP010 decoder_configが存在。
- 実GPUで4032x3024 10bit HEVC Main 10固定人工画像を`AV_PIX_FMT_D3D11`として取得できた。P010 CPU readback成立、ソフトウェア復号とのBGRA全48,771,072バイト完全一致、MAE 0.000、最大誤差 0。初回の実Intel GPUハードウェア成功確定（画像品質はこの1枚のみ）。
- 同一試行のCPU全画像処理806.409ms（HEVCデコード754.168ms、BGRA52.241ms）に対してGPU経路367.036ms（デコード36.126ms、readback同期/転送268.289ms、BGRA62.620ms）。`1-367.036/806.409 = 54.5%`短縮（**単発・この固定サンプルの処理工程のみ**）。GPUデバイス初期化42.429msを含めると参考値409.465msで約49.2%減。HEICメタデータ解析、Fantサムネイル縮小、Explorer初回表示時間は含まれない。
- 2048x2048と1920x1080の別固定サンプルはCPU復号成功だが、実GPUではFFmpegの`Failed setup for format d3d11: hwaccel initialisation returned error`により失敗。外部のffprobeでビットストリームを再調査したところ、両者のHEVC general_profile_idcは`4 = RExt`（Range Extensions）。成功した12MPは`2 = Main 10`。**10bit=Main 10プロファイルとは限らない**。サイズが原因と断定した従前の見立ては棄却。
- RExt自体のどの制約（SPS・OSドライバー・FFmpeg D3D11VAプロファイル対応など）が初期化不可の直接原因かは未確定。今回のIntel GPU向け研究ではRExtをソフトウェアへfail-closedとする。
- `HevcAnnexBSpsProfile.h`追加：HEVC SPSの`general_profile_idc`を元データを変更せず事前読取りし、`AVCodecContext.profile`と照合。CIの3件：RExt=4, RExt=4, Main10=2を確認。GPU判定はMain10のみ候補として許可し、D3D11実フレーム＋画素を追加検証。
- 別の大きなボトルネックはGPU→CPU P010の同期／転送操作で約268ms。これは転送待ちに含まれるGPU実復号完了の同期待ちを分離したものではない。単純なメモリ転送帯域値と誤解しない。今後はGPU側で縮小・色変換を済ませて小さい結果だけをreadbackするD3D11 VideoProcessor方式を研究する。ただしWIC Fantとの画質一致はまだ不明。
- 3回CPU/GPUを繰り返して中央値を出す研究版も追加し、単発54.5%という数字の再現性を次の実機診断で確認する。配布前にCIで元ZIPのHEVC固定3件と自己テストを再検証する。
- **実写真の初回サムネイル30%以上の高速化はまだ未実証**。写真のHEVCプロファイル・ICC・HDR・aux alpha・回転などによる候補割合は未測定。

## 2026-10-22：12MP P010 GPU直接縮小→256px BGRA最小readback（研究実装）

- Intel HD620実機の高負荷Main10 GPU復号成功を踏まえ、全P010（12MP 10bit 4:2:0約36.6MB）を`av_hwframe_transfer_data`でCPUへ戻す前にD3D11 VideoProcessorで縮小・色変換する研究経路を追加。`HeicD3D11GpuThumbnail.h`がFFmpeg `AV_PIX_FMT_D3D11` のGPUテクスチャと配列スライス位置を取得し、同じFFmpegデバイスの`ID3D11VideoDevice/ID3D11VideoContext`を用いてP010から256x192のBGRAターゲットを生成して、192KiB（196,608バイト）のstagingだけ読み戻す。画素数と転送対象バイト数は約186分の1だが、実機転送時間が186分の1になると**断定できない**（デコード同期待ち・VideoProcessorの処理待ちを含む）。
- `VideoProcessorBlt`のデバイス/フォーマット能力チェック・DXGI配列スライス検証・FFmpeg D3D11ロックを追加。特殊色変換・HDR・フルレンジ・解像度違いの本研究は拒否、失敗時にはGPU小縮小を未実証と記録、既存のフルP010 readback+BGRA処理を維持。
- `HeicThumbFantReference.h`でWindows WIC `WICBitmapInterpolationModeFant`の4032×3024 BGRA→256×192を**RAM上で**同時比較。MAE、PSNR、最大RGB誤差、不透明画素数をログ記録。`quality_approved=0`を固定し、研究結果だけで画質承認・Explorer製品経路切替は行わない。
- 新しい引数は`HeicD3D11PortableProbe.exe thumb:0`（アダプターindex 0）。通常の`0`等と分離。`Run_GPU_Thumbnail_Experimental.cmd` は3固定HEVCだけを読み、テキスト`GPU_Thumbnail_Experimental_乱数_乱数.txt`を新規作成する。管理者権限・インストール・既存WIC変更・個人HEIC画像取得なし。
- GPU側の`setup_ms`、`video_blt_submit_ms`、`small_256px_readback_ms`、`gpu_thumb_total_ms`を分離する。3回反復中央値で比較し、既存フルP010 readback時間も残す。
- **重要**：実GPUハードウェア上でのVideoProcessor成功、真のreadback速度、WIC Fantとの画質差は未測定。Windows GitHub ActionsはソフトウェアGPUのみで、コンパイルとソフトウェアアダプター拒否／固定3HEVC自己テストしか証明できない。実環境で16:1縮小が処理できないドライバーもありうる。
- 研究用EXEは比較のため従来のフルP010 CPU readbackも後から行うので、プロセス全体の経過時間やメモリ使用量は現在の性能目標の直接測定ではない。将来の製品候補は**GPU縮小のみを採用可能か**の画質と能力確認後に決める。

## 2026-10-22：実機「GPU Thumbnail Experimental」第1回ログ（匿名化した性能・品質数値のみ）

ユーザーが送付したテストZIPに \`GPU_Thumbnail_Experimental_21120_6299.txt\` があり、搭載ファイル13件の SHA256 は全件マニフェスト一致。オリジナルZIP、マシン固有のフルパス、デバイス識別用ポインタ、生ログはGitHubへ公開しない。

物理アダプター：Intel HD Graphics 620（vendor 8086, device 5916）、非Intelの Microsoft Basic Render は除外。人工4032x3024 HEVC Main10だけGPU復号と縮小成功。2048x2048・1920x1080 は RExt (profile idc 4) でソフトウェアへ戻る。

**3回の中央値（ms; 単一固定人工12MP入力）**：
- software HEVC復号＋BGRA：741.100
- GPU HEVC復号＋フルP010 readback＋BGRA：341.922、フルP010 readback：267.055
- 新D3D11 VideoProcessorの12MP P010→256x192 BGRA縮小/readback：207.498（内GPU処理準備117.525、readback約78.353）；GPU復号23.503 + VP thumbnail処理207.498＝231.001（別系列中央値の合計であり、ペアごとの合計時間中央値ではない）
- 直接縮小パスは従来341.922msと比較して短時間だが**WIC Fant画質不合格**。正式採用不可。
- VP→WIC Fant 256pxのRGB MAE=6.529792, PSNR=25.774dB, RGB max error=148, 3回同一; Alpha all opaque. 元解像度のGPU vs software BGRAは3/3完全一致MAE=0。原因はGPU縮小/色変換/補間差分側。
- setup=159.433/117.525/106.329ms と大きい。これに対し小画像読み戻しは45.885/81.902/78.353ms。小画像readbackにもVideoProcessor GPU同期待ちが含まれ、転送帯域単体の計測値ではない。
- いずれも初回Explorerでの実時間、WIC Shell統合性能は未測定。新方式で30%以上初回サムネイル改善を達成したとは言えない。

**対策**：D3D11 VideoProcessor のEnumerator、Processor、256px BGRA出力、stagingテクスチャを複数フレーム間で再利用し、高額な準備初期化を毎回行わないよう改修。\`HEIC_GPU_THUMB_TIMES ... reused_processor=0/1\` を実機計測で検証予定。画質不一致が改善しない限り、速度が向上してもExplorerへ導入しない。

## 2026-10-22：HD620第2回実機ログと品質優先方式の変更

ユーザーが提出したZIP内の同梱ファイル13件すべてでSHA256マニフェスト一致。生ログ、フルパス、ユーザー個別情報はGitHubへ公開しない。物理GPU Intel HD Graphics 620、固定人工12MP HEVC Main10プロファイル (4032x3024)。

実機3回の中央値（単位ms）：
- CPU HEVC復号→フルBGRA：833.529
- D3D11VA GPU復号→フルP010読み戻し→フルBGRA：316.505（この工程で約62.0%短縮）
- GPU復号の中央値24.288 + VideoProcessor 256x192縮小・小型読み戻し中央値187.458 = 211.746（独立中央値の加算であり厳密な各回総時間中央値ではない）
- VP Processor準備時間：初回139.592、再利用後0.023/0.027。キャッシュが実Intel GPUで成功したことを実証。
- ただしVP出力のWIC FantとのRGB MAE：初回6.529792、その後6.905884が2回。RGB最大誤差148〜152、PSNR25.634〜25.774dB。画質基準不合格を維持。
- WIC LinearとのRGB MAE：初回1.939412、後2回は3.213386。CubicとのRGB MAE：初回2.202901、後2回は3.353841。Linear/Fant自身のRGB MAEは6.405796。GPU VPの画質差は縮小フィルターの違いが有力だが、色変換・サンプル位相の寄与は未確定。
- フルBGRAにおけるGPUとCPUの画素完全一致は3/3。フルP010読み戻し時間の中央値243.606ms。GPU小画像readbackは97.311msだが、GPU演算との同期待ちを含む。PCIe転送のみの時間ではない。

### 画質優先の研究本命：GPU HEVC + 元どおりのWindows WIC Fant

高品質の目的には、失敗しているGPU直接256px縮小を製品に採用しない。すでに3/3画素完全一致したGPU HEVC→フルP010 CPU readback→フルBGRAへ切り替える**復号だけのGPU化**を優先。最終縮小には元のWindows WIC Fantをそのまま使用し、従来CPU BGRA→WIC Fant出力とバイト完全一致するか実測する。

研究ソース HeicD3D11PortableProbe.cpp に3反復HEIC_GPU_FANT_END_TO_END / HEIC_GPU_FANT_3PASS_MEDIANを追加。CPU/GPUのHEVC復号+BGRA変換+WIC Fantの全工程時間と、最終256x192 BGRA画像196,608バイトの一致を検査する。完全一致しない場合はfail closed。Intel実機の新テスト結果はまだ取得前で、Explorer初回表示30%以上の短縮も引き続き未実証。管理者権限・写真の書換え・WIC COM/Photosへのインストールを要しない。

## 2026-10-22：第3回HD620実機結果：GPU復号＋WIC Fantの「最終サムネイル完全一致」

ユーザーからの最新研究用ZIPはファイル13件すべてが同梱SHA256マニフェストと一致。生ログ、デバイスポインタ等は公開せず、以下の集計値のみ残す。

- 実GPU：Intel HD Graphics 620, 8086:5916、D3D11VA実ハードウェアフレームを3回取得。**人工12MP HEVC Main10 4032x3024** が対象。
- CPUフル画像BGRAとGPU復号→フルP010 readback→BGRAは3回とも全画素完全一致（MAE=0, max=0）。その画像をWindows WIC Fant 256x192 BGRAで縮小した**最終196,608バイト**も3/3バイト完全一致した。
- 画像復号+BGRA+WIC Fant最終縮小までの3回計測（単位ms）：
  - CPU 754.284 / 786.059 / 749.868；中央値754.284
  - D3D11 GPU 397.476 / 353.172 / 387.443；中央値387.443
  - 中央値差366.841ms、短縮48.634%。これは同一人工12MP入力についてWindows Explorer/UI時間を含めず、アダプター初期化46.209msも含めない。
- CPU側WIC Fant縮小単体39.633/43.686/52.568ms、GPU側38.260/40.213/51.662ms。品質を落とす縮小最適化は不要な方向性が強い。
- 一方D3D11 VP直接縮小256pxは最初Fant MAE6.529792、反復後6.905884、品質不合格。GPU VP再利用自体は成功（setup146.786→0.029→0.026ms）、品質改善には寄与していない。製品導入禁止。
- HEIC由来の同梱2画像は両方HEVC RExt（general_profile_idc=4）で、Main10 GPUドライバーでは初期化できずCPUに戻る。**現時点で実HEIC由来画像のGPU高速化候補は2枚中0枚**。Main10 12MPは人工HEVC AnnexBであり、実HEICコンテナーからのGPU復号と同一視できない。
- Windows 10の既存HEIC WICデコーダーは、10bit HEICに対して48bppRGB（アルファ付き64bppRGBA）を公開する。人工Main10のBGRA8bit画像でWIC Fantが完全一致したことは、**WICの16bit出力／ICC・色管理の完全互換を証明していない**。10bit HEICを8bitに落とすような製品経路は許さない。

### 次段階：8bit Main/NV12を別ルートで検証

人工HEVC 12MP 8bit Main（general_profile_idc=1）をCIで追加生成。Intel D3D11 Mainを使ってGPU復号→NV12フル画像readback→BGRA→元のWIC Fantへ入力し、CPU基準と最終BGRA 256x192の全バイト一致・3回中央値を記録。Main10とMain8の成功件数は個別に出力し、2件の実HEIC由来RExtは引き続き拒否する。GPU直接縮小VPはP010限定のため8bitでは起動しない。

この研究は実機用の人工4番目の固定HEVCを追加しただけで、実HEIC画像の変換・上書き・私有写真の読込みはない。追加Main8研究でも本物のHEICサムネイル30%短縮はまだ未証明。現行WIC・フォト・HEIC ManagerとWindows Shellのインストールを変更しない。

## 2026-10-22：第4回実機ログ：8bit HEVC MainはGPU復号したがBGRAに不一致

受領した実機ZIPのマニフェスト記載の**13ファイルはSHA256全件一致**。生ログ、個人のファイルパス等は公開しない。

- 物理 GPU は Intel HD Graphics 620 (vendor 8086, device 5916)。CPU用ソフトウェアデコーダーの Main8 出力形式は YUV420P、D3D11VAハードウェアの Main8 出力形式は NV12。人工12MP HEVC Main8 1枚は実GPUハードウェア復号に成功。
- 人工12MP Main8 の1回目CPU HEVC復号→BGRA: 715.476ms。GPU復号→フルNV12 readback→BGRA: 209.132ms。しかしRGB完全一致しないため採用不可。**MAE_RGBA=1.104363、PSNR=38.898dB、最大チャネル誤差=82**。Main8 GPUを `proven_main8=0` とし、従来ソフトウェアへフォールバック。現段階でMain8の高速化率は製品成果として数えない。
- 人工12MP Main10は引き続き復号後BGRA完全一致3/3、Windows WIC Fant 256x192の最終196,608バイト完全一致3/3。3回中央値CPU=812.622ms、GPU=368.162ms、画像処理工程 **54.695%短縮**。アダプター初期化46.206ms、実HEICコンテナ処理、ICC/Exif/irot、Explorer初回表示を含まない。
- 2枚のHEIC由来RExt画像は未だCPU経路のみ。実HEICでGPU加速が成立した例はゼロである。

**次の対策**：BGRA不一致はNV12→RGB色変換の最適化経路差によるものか、Intel D3D11VAが返すNV12のY/U/Vピクセルが元々違っているのか未確定。研究用 `HeicMain8NativeYuvAudit.h` でCPU側YUV420PとGPU側NV12をそれぞれ **同一 planar YUV420P配列に正規化**し、Y/U/V成分別の不一致画素数・平均誤差・最大誤差・画像上の誤差発生矩形をログ記録。RGB変換前に差があればハードウェア復号側、YUVが一致しRGBだけ違えば変換／色属性側を疑う。生の画像画素はディスク保存しない。18MBの診断コピーは復号時間・変換時間の計測区間から外し、CPUの自己テストには単一画素をわざと変更して検出できることも確認する（故意変更はRAM内のコピーだけ）。

**安全条件**：画質不一致のままMain8に成功判定を与えない。非対応RExt/カラー／サムネイル品質不合格パスも製品へ組み込まない。MSIを作らない。最新人工サンプル結果から本物のExplorerの30%短縮を主張しない。

## 2026-10-22：第5回ユーザー実機ログ・8bit色変換問題の原因確定範囲

受領した研究用ZIPはマニフェスト記載14ファイルすべてSHA256一致。実GPUはIntel HD Graphics 620、D3D11VA 8086:5916。

実機の人工12MP HEVC Main8:
- GPUから回収したNV12とCPU復号したYUV420Pを共通planar YUVへ並び替え、Y（12,192,768画素）、U（3,048,192画素）、V（3,048,192画素）の**全てが完全一致**。3成分それぞれ差分画素数0、MAE0、最大差0。従って今回の8bit失敗はGPU/CPUのHEVC復号画素差ではない。
- しかしRGB化後のBGRAはMAE_RGBA=1.104363、最大誤差82、PSNR=38.898dB。両画像のcolor matrix=2、range=1と診断された。swscaleへ渡すソース画素形式がNV12（GPU）とYUV420P（CPU）で異なるため、色変換内部経路／丸め／色差補間が不一致の最重要候補。
- ハードウェアMain8の最初の工程時間：CPU HEVC→BGRA 633.000ms、GPU HEVC→フルNV12 readback→BGRA 196.345ms。ただしBGRA不一致なのでこの速度差を製品成果として計上しない。
- 人工12MP HEVC Main10はGPU復号→CPUフルP010→BGRA→Windows WIC Fant 256px出力が3/3バイト完全一致。CPU中央値697.905ms、GPU中央値341.870ms、51.015%短縮。依然人工HEVCだけの結果、GPUアダプター初期化44.535msやExplorer初回処理は含まない。
- GPU上のVideoProcessor直接縮小はFantとの差MAE6.53/6.91程度で画質不合格、引き続き不採用。

### CIだけで進める次段階とユーザー検証の頻度

ユーザーから「毎回ダウンロードと診断提出を要求されるのは負担。開発側で可能な自動テスト・原因特定・品質検証をまとめ、重要な節目にだけ実機テストを依頼する」という明確な方針変更があった。**ここから先は細かな修正版ZIPを都度依頼しない。**

Main8に対して、GPUから戻したNV12のY（輝度）をそのまま参照し、UV色差のみを別々のplanar U/Vへインターリーブ解除して、CPUと同じswscale YUV420P→BGRA経路へ渡す修正を研究用portable EXEに実装。画質を低下させるGPUビデオ縮小経路は使用しない。色差用の一時メモリはW×H/2=6,096,384バイトのみであり、以前の全18,289,152バイトYUVコピーより少ない。

さらに実物GPUがないWindows CIでも固定12MP CPU YUV420Pデータから人工NV12をメモリに作り、**新しいUV解除を通したBGRA出力を元のCPU出力と全48,771,072バイト完全一致**で確認できる正負テストを追加。元のNV12→BGRAの色差は同じデータから測定するが、直接NV12経路が必ず不一致という仮定は置かない。

あとはヘッダ/主CPPコードのGitHub Actions Windowsビルド、自己テスト、プロファイル検証、ソフトウェアアダプター誤検知拒否、読取専用コマンド生成、全変更のコード監査まで開発側で実施。実GPUがなければハードウェア経路の最終成功は断定せず、必要な最終節目だけユーザーに依頼する。

## 2026-10-22：HEICコンテナー7件すべてRExt判明／事前判定の修正とCI完了

- [Windows CI #39723178570](https://github.com/taku077/wic_heic/actions/runs/39723178570) はGitHubリポジトリ管理下の固定7 HEICファイルを読み取り専用でFFmpegにより主画像HEVCビットストリームへコピー抽出し、FFprobeの実際の `profile` と `pix_fmt` を照合した。7/7件が **HEVC Range Extensions (Rext, profile_idc=4)** であり、Main8=0件、Main10=0件。入力SHA256は全件前後一致。03/04/fixture2件は10bit YUV420、05は8bit YUV444、06は10bit YUV444、07は12bit YUV444。圧縮ビットストリームのプロファイルだけの集計であり写真全体のGPU対応率を推定できない。
- これまでの `HeifPrimaryTransforms.cpp` は、HEIF主画像が10bitかつアルファなし・ICCなし等なら **実SPSがRExtでも仮のMain10候補**と判定できる欠陥を持っていた。今回 `hvcC` のプライマリitem `ipma` 紐付けを厳密に解析し、hvcCのgeneral_profile_idcとVPS/SPS/PPS中の実SPSのprofile_idcが一致し、Main8(idc=1)かMain10(idc=2)で、かつ他の従来条件も満たす場合だけ暫定候補とした。RExt(idc=4)は10bit YUV420であってもCPUへフォールバックする。
- 最初はlibheifの一般 `heif_item_get_property_raw_size` APIでhvcCを読もうとしたが、固定ライブラリは既知のhvcCプロパティにcode=5を返しデータを公開しない。このため既存 `HeifNativeAnnexB.cpp` の実績があるISO BMFF `meta/iprp/ipco/ipma/hvcC` 解釈を、個別の境界チェック付き独立ヘッダー `HeicPrimaryConfigBoxes.h` に実装した。
- `HeicHvccProfileGuard.h` が1～64配列のhvcC内SPSの実ヘッダーを走査し、hvcC General HEVC Profileと比較。不正な長さ・未知プロファイル・複数の不整合SPS・プロファイル不一致・曖昧な主画像プロパティではfail-closedする。テスト用のMain8/Main10/RExt・不正長等を含む正負テストにも合格。
- `HevcAnnexBSpsProfile.h` に4byte `00 00 00 01` と重なった3byte開始コード `00 00 01` を両方認識してSPS1個を2個に数えるバグがあり修正。修正後、HEIC7件すべて hvcC general_profile_idc=4, SPS profile_idc=4, SPS count=1。
- [Windows CI #39724843223](https://github.com/taku077/wic_heic/actions/runs/39724843223)：独立libheifネイティブ分析器をWindows MSVCでビルド成功、7件の一次画像プロパティ紐付け確認、hvcC/SPS正負自己テスト通過、7件すべて `candidate=0`・CPUフォールバック・元画像SHA256不変を通過。以前の暫定Main10候補2件を期待する旧CIのチェックも7件CPUフォールバックへ整合修正した。
- 実機Intel HD620 GPUで完全画素一致の人工HEVC Main10は実証済み。ただし本テスト群の**実HEICは全件RExt**なので現行GPUパイプラインが高速化する実HEICは7件中0件。GPU経路単体の人工画像約50%短縮をWindows 10 Explorer全HEICの高速化と混同しない。
- 次の開発で本当に優先すべきは、(1) 現行プライベートWIC/libheif/libde265のソフトウェアRExtフル画像デコードとShell起動経路、(2) Windows 10 Explorer初回表示での効果、(3) 高品質で正しい48bpp/64bpp、ICC、Exif/irot処理。既存研究では埋込サムネイルは固定7件中256pxへ適用0件、libde265 AVX2限定カーネル効果は±0.5%、CPU FFmpeg decoder差は約-1.5%、直接IThumbnailProvider試作は人工HEICで2.1～2.6倍（Explorer実表示は未測定）。
- **ユーザーの手間削減**：研究途中の小刻みな診断ZIPやMSIを毎回要求しない。可能な解析・ベンチ・CIを先にまとめて実施し、十分な品質と速度改善が見込める段階にだけ実機検証を依頼する。Windows標準フォト、WIC登録、HEIC Manager、実ユーザーHEICファイルは変更しない。

## 2026-10-22：RExt7件の未登録プライベートWIC vs Shell Provider厳密A/Bと10bit復号スレッド数

目的：GPU非対応と実測で判明した固定7 HEIC（すべてRExt）に対して、GPUへの誤投入を避けた状態で初回サムネイルの改善候補を、ユーザーへの繰返しの診断提出を頼まず独立Windows CIで評価する。

### 7件×4回のCOM画像品質と時間

- `ShellEmbeddedDirect.exe --rext-corpus` と `HeicRextDirectCorpus.h` を研究追加。実験用 `WicHeicFastPreview.dll` の `DllGetClassObject → IThumbnailProvider → IInitializeWithStream → GetThumbnail(256)` と、**別にロードする未登録の `WicHeicPrivateCodec.dll` を通じたIWICBitmapDecoder→WIC Fant→32bppPBGRA** の結果を比較。どちらも同一のプライベートWICデコーダー、ユーザーのWICグローバル登録不要。実ファイルは固定7つ、各4回ABBA順に実行、計28回のWIC縮小後PBGRAを**全バイト一致**確認。アルファ付き/不透明、8bit/10bit/12bit、irot画像を含み、メタデータ/画素は読取専用、元の7 HEICのSHA256は前後不変。
- [成功CI #39729634583](https://github.com/taku077/wic_heic/actions/runs/39729634583) の例：03 HEIC Provider=139.481ms, WIC直接=141.024ms、04 HEIC Provider=130.989ms, WIC直接=131.047ms、05=2.810/2.749ms、06=3.651/3.661ms、07=3.559/3.507ms、irot noaux=83.365/83.665ms、irot aux=159.252/158.864ms。同一API間は誤差程度の差であり、**Provider呼び出しだけがHEVCデコードを速めたという主張は棄却**。古い「WIC自動コンポーネント選択 vs 直接Provider」の2.1～2.6倍差は、今回のWIC private decoder直呼び比較とは対象・経路が違い直接矛盾しない。元のExplorer初回表示は依然未実測。

### 10bit RExt libde265 1/2/4スレッドABBA

- `HeifRextThreadsAB.cpp` / .vcxprojを実装。プライベート未登録libheifが、固定されたアルファなしRExt `04.heic`（2048x2048）・`fixture-irot3-exif6-noaux.heic`（表示1080x1920、HEVC coded1920x1080）のRGB24主画像をワーカー1/2/4スレッドの順序交差で各4回、1ファイル当たり12回復号。**全デコードのRGB全画素ハッシュが一致**していることと元HEIC不変を検証。研究対象はRGB24の完全一致でありWIC 48bpp出力全体の互換性の証明ではない。
- [成功CI #39730090152](https://github.com/taku077/wic_heic/actions/runs/39730090152)：04画像 1スレッド93.222ms、2スレッド73.511ms、4スレッド69.935ms（2→4のwall 4.863%短縮）。irot noaux画像 1スレッド50.699ms、2スレッド37.362ms、4スレッド34.850ms（2→4のwall 6.724%短縮）。04のCPU使用時間は2スレッド109.375ms→4スレッド132.812ms、irot noauxでは70.312ms→78.125ms。**CPU時間増に対して2→4スレッドwall利得は小さい**。第7世代Core i5/Windows10の実機負荷・複数枚同時処理ではさらに違う可能性があり、10bit/RExtの製品版スレッド数増を採用しない。
- 同CIで、7 HEICのプライベートWIC/Providerの4回A/B全28テストを再実行し、完全な画素一致、ソースSHA256不変、システムCOM非登録も再確認。

### 技術判断

(1) Intel GPU Main/Main10人工12MPの一部成功（約50%処理時間減）は維持、しかし固定7 HEICはRExtにつき現行GPU不可。(2) 全画像に対するProviderだけの速度利得は今回の未登録WIC直接比較では存在しない。(3) RExtの4スレッド化はこの2画像の内部復号だけ4.9～6.7%程度で、CPU負荷増を伴う。(4) 既存研究ではFFmpeg CPU decoder変更1.5%遅く、AVX2限定改善は±0.5%、大きな埋込サムネイルが無ければ埋込枝は適用できない。よって既存コードを細かく切り替えるMSIを作らない。

**次の本命：12MPの典型的なRExt実HEIC（現在の固定7枚は最大2048x2048）を含む高解像度・画像特性の多様性で、実HEICのWIC CopyPixels内部処理（HEVC復号、色管理・回転、Fantリサイズ、Shell生成）を精密計測し、エクスプローラー初回表示の根本ボトルネックを特定する。** この検証のためにユーザーへ今すぐ画像や診断ZIPの再提出を求めない。画像を公開GitHubへアップロードしない、未登録研究コードでのみ進める。

## 2026-10-22：RExt HEICでWIC `CopyPixels`を工程分解（CI専用、全画素一致）

検証：[Windows CI #39737874856](https://github.com/taku077/wic_heic/actions/runs/39737874856) は**成功**。固定HEIC7画像、`IThumbnailProvider`と未登録プライベートWICの32bppPBGRA 256px全画像は各4回完全一致。元HEIC SHA256不変。

元画像はRExt、10bit/12bit/8bit、アルファ、回転混在。研究用 `HeicBitmapFrameDecodeProfileCI.cpp` をGitHub Actionsの**一時チェックアウト内だけ**で製品デコーダーソースの代わりに差し込み、`WIC_HEIC_REXT_TIMING_CI` を定義してプライベートDLLをコンパイル。ソースはビルド後 `git restore` で戻し、SHA256一致を要求。研究用以外の製品ソースは従来と**バイト単位で同一**（元の `wic_heic/HeicBitmapFrameDecode.cpp` のGit blob SHAは `c8b08d7adbb47880cdb1d176720d89efbce2ee48`）。インストーラー・Windows WIC/Photos/HEIC Manager・レジストリは変更しない。

比較条件：独立Windows Server 2022仮想CIランナー、各サンプル4回中央値、計測中はQPCにより内部区間を分割するため、数値には計測オーバーヘッドがあり、過去の非計測ビルド数値やWin10実機と直接同一視しない。

| 固定HEIC | 総WIC `CopyPixels` 中央値ms | libheif HEVC復号＋RGB生成ms | WIC生画素コピー／10・12bit→16bit値正規化ms | WIC Fant＋PBGRAほか下流ms |
|---|---:|---:|---:|---:|
| 03.heic (1920x1080, 10bit+alpha) | 132.545 | 69.090 | 15.339 | 48.116 |
| 04.heic (2048x2048, 10bit) | 122.729 | 82.555 | 22.587 | 17.532 |
| 05.heic (100x100, 8bit+alpha) | 1.412 | 0.878 | 0.000* | 0.534 |
| 06.heic (100x100, 10bit+alpha/ICC) | 2.341 | 1.032 | 0.000* | 1.306 |
| 07.heic (100x100, 12bit+alpha/ICC) | 2.325 | 1.072 | 0.000* | 1.255 |
| fixture-irot3-exif6-noaux.heic | 76.585 | 54.233 | 11.551 | 10.777 |
| fixture-irot3-exif6.heic (aux alpha) | 145.141 | 80.470 | 15.409 | 49.253 |

注：各工程の「中央値」を合計しても必ずしも総中央値に等しくならない。* 小型画像の生画素コピーが0.000ms表示なのはマイクロ秒単位への整数丸めにより短時間の読み出しを表現できていない可能性があるためで、処理が実際に0時間という意味ではない。WIC内部処理で `CopyPixels` は1920x1080サンプルに1920回、100x100では100回呼ばれるが、**libheifの本体復号は1回だけ**ということも検証。

現状の洞察：高解像度10bit例（04）ではHEVC復号＋RGB生成がWIC合計約67%、正規化コピーが約18%、Fant+PBGRAほか約14%。アルファ＋向き条件のある03およびfixture(母画像)では下流処理約36%・34%を占める。よって全画像を同じ最適化だけで30%以上速められるとは考えない。

次のCI用研究：10bit/12bit画像のWIC 16bit値変換を**元の整数計算と数学的に同一なルックアップテーブル**で行い、2つの別DLL（スカラー計算/表参照）を実際にMSBuildして4回ABBA・全256px出力バイト完全一致・WIC生画素コピー時間とサムネイル完成全体時間を比較。画素差が1バイトでもあれば不採用。10bit/12bit色深度を落とさず、16bit WIC表示の仕様も変えない。効果が小さければ製品化しない。

## 2026-10-22：RExtの10/12bit→WIC16bit精密正規化をLUT化したA/B、2回の独立CIで品質維持と短縮

- **実装**：元の16bit正規化式 `(sample * 65535 + sourceMax/2) / sourceMax` の結果を、10bit=1024件／12bit=4096件の事前計算テーブルに置き換えた。12bitも切り捨てずWIC16bit精度を維持する。研究コピー `wictest/isolated_shell/HeicBitmapFrameDecodeProfileCI.cpp` の `WIC_HEIC_REXT_NBIT_LUT_CI` 定義時だけ有効。元の `wic_heic/HeicBitmapFrameDecode.cpp` はバイト同一で元に戻してあり、Windows WIC標準、フォト、自作HEIC Manager、MSIは全く変更しない。
- **比較方式**：Windows Actionsの一時チェックアウトで、通常スカラー式とLUT式を**別々に完全再ビルド**したプライベートWIC DLLとして採取（SHA256が異なること、およびエクスポートされた `WicHeicRextLookupEnabled` 値がbaseline=0/LUT=1を確認）。未登録 `IWICBitmapDecoder` のWIC Fant 256px→32bppPBGRAを固定7 HEICに4回ずつABBA順で行う。**28比較すべてで256px画像が全バイト完全一致**。WICの元解像度の24/32/48/64bpp画像も7ファイル全バイト完全一致（例：04 HEICの48bpp=25,165,824バイト、alpha付03 HEICの64bpp=16,588,800バイト）。オリジナルHEICは一切変更せず、SHA256も不変。
- [第1 CI #39738953122](https://github.com/taku077/wic_heic/actions/runs/39738953122) は成功。04 (2048x2048/10bit)のWIC生画素コピー22.581→10.344ms（54.2%短縮）、Fant完成含む未登録WIC全工程124.035→112.192ms（**9.5%短縮**）。03 (1920x1080/10bit alpha)133.673→124.029ms（**7.2%短縮**）、irot noaux 77.968→71.468ms（**8.3%短縮**）、irot aux145.643→137.620ms（**5.5%短縮**）。小型100pxサンプルは差が微小で有意な高速化とは言えない。元画素全解像度の厳密一致テストは次のCIで追加。
- [第2 CI #39739459910](https://github.com/taku077/wic_heic/actions/runs/39739459910) も成功。04の生画素コピー20.541→8.268ms（59.7%短縮）、WIC全工程102.744→92.254ms（**10.2%短縮**）、03 WIC全工程110.689→103.222ms（6.7%短縮）、irot noaux 62.453→57.679ms（7.6%短縮）、irot aux121.928→114.869ms（5.8%短縮）。**7つの元解像度WIC全画素一致**を追加で検証成功し、48bpp/64bppネイティブ色深度・alphaを変えない。
- **保護**：本計測系は研究ブランチとWindows Server 2022の隔離CI内のみ。CIのビルド後には元製品ソースのSHA256を厳密確認して復元する。メモリ・CPU負荷、真のWindows 10 Explorer初回描画時間、実ユーザー写真のICC/HDR/Exif忠実性やサイズ別体感は未実証。今回の実HEICは小型～4.2MP（12MPの実RExt HEICは未検証）なので**ユーザーの期待する初回表示30%以上短縮を達成したとは判断しない**。MSIを作成しない。
- **技術結論**：WIC生画素コピー工程は正しい色深度を保って約半分に短縮できたが、HEVC復号＋RGB生成が依然支配的で、サムネイル完成までの短縮は概ね5～10%。次はRExtフルデコードを維持しつつ、復号エンジン／画像構成や高解像度入力の根本ボトルネックを追い、開発側でCI検証を集約してから大きな区切りにのみ実機テストをお願いする。

## 2026-10-22：RExt実HEICのRGB48/RGBA64とYCbCr420出力差、および7枚同時サムネイルの並列化

### 10bit・RExt色形式による復号負荷の差（CIのみ）

`HeifRextColorStageAB.cpp` と .vcxprojを研究ブランチに新設。固定4 HEICを未登録の静的libheifで、RGB16 interleaved（48bpp RGB／64bpp RGBA）とYCbCr420p10（アルファありの場合はAlpha平面も含む）へ独立復号。2/4スレッド、各形式4回ABBA、全画素の正規化済表示ストライド部分ハッシュを方式内で照合（RGB↔YCbCrの画素値同一性は主張しない）。4件の元HEICの前後SHA256も不変。

[成功CI #39748524129](https://github.com/taku077/wic_heic/actions/runs/39748524129)（アルファなし2画像）及び[成功CI #39748890858](https://github.com/taku077/wic_heic/actions/runs/39748890858)（アルファ有2画像を追加）：

| 固定10bit RExt・2ワーカー | RGB48/64生成 | YCbCr420生成 | 注意 |
|---|---:|---:|---|
| 04.heic 2048x2048 / 不透明 | 77.176ms | 86.825ms | **YCbCrの方が遅い** |
| fixture-irot3-exif6-noaux / 不透明 | 51.621ms | 33.777ms | YCbCrの方が速い |
| 03.heic / アルファ | 55.210ms | 41.112ms | YCbCrの方が速い |
| fixture-irot3-exif6 / アルファ | 76.745ms | 60.620ms | YCbCrの方が速い |

別ランナーCIでは04 RGB48=40.138ms、YCbCr=50.488ms。絶対時間はVM負荷に依存する一方、04では複数回**YCbCrの方が低速**である。YCbCr生成とRGB生成はlibheif内部の異なる色変換／出力バッファ／アルファ結合等を含み、時間差を単独のRGB色変換時間と認定できない。YCbCrの生データをそのままWIC Fantへ渡してもRGB48/64のバイト完全一致が保証されるわけではない。従って**YCbCrに変えるだけという製品最適化案を棄却**。今後の候補は最終WIC16bitカラー／ICC／Exif／アルファ／Fantまでの完全一致が必須。

### 7枚の未登録プライベートShell Providerの一括処理を並列化

研究ヘッダ `HeicRextBatchAB.h` を追加し、`ShellEmbeddedDirect.exe --rext-batch-corpus` に繋いだ。実HEIC固定7枚だけを一括で処理し、最初に別々の通常Provider生成物を7枚分PBGRAとしてRAMに保持、以後1/2/4独立MTA COMワーカーへ割り振ってリードオンリーの生成を実行。**すべての出力が基準PBGRA全バイト完全一致、元HEIC SHA256不変、コーデックグローバル登録・システム改変0**。

[成功CI #39749548804](https://github.com/taku077/wic_heic/actions/runs/39749548804)（各方式4回、順次実行）：
- 逐次=482.899ms / 2並列=285.964ms（40.8%短縮） / 4並列=216.837ms（55.1%短縮）。ただし方式の実行順が固定でありCPUブースト・ファイルキャッシュ熱の時間差バイアスが残る。

測定順序を**1,2,4,4,2,1,1,4,2,2,4,1** の交互スケジュールへ修正。加えてプロセスCPU時間と測定終了時の private committed memory (ProcessMemoryCountersEx.PrivateUsage) を記録。このメモリ値は**処理中のメモリ使用量ピークではなく、終了直後のサンプル値**であり、ピークメモリと混同しない。

[成功CI #39750157834](https://github.com/taku077/wic_heic/actions/runs/39750157834)（バイアス緩和、4回中央値）：
- 1並列：全7枚514.0ms、CPU523.4ms、終了時private commit約21.78MiB。
- 2並列：334.9ms（**34.8%短縮**）、CPU593.8ms（逐次比**13.4%増**）、終了時約22.15MiB。
- 4並列：224.2ms（**56.4%短縮**）、CPU726.6ms（逐次比**38.8%増**）、終了時約20.76MiB。

**今回の重要な区分：これはWindows Server 2022のGitHub VMでのファイルキャッシュが温まった後の「7枚一括Provider作業時間」であり、Windows 10の実Explorer初回サムネイル完成・1枚目の表示時間を測っていない。** Explorer自身が同じ複数並列のサムネイル生成を既に行っている場合、この結果を別プロセスの高速化効果として上乗せできない。ユーザー実機（Core i5 7th GPUなし、低負荷重視）のモデルでは4並列はCPU時間増による操作レスポンス悪化懸念があり、2並列＋優先度抑制／停止／キュー制御を研究候補とするに留める。誤って常駐バックグラウンド処理・自動プレロードを許可なしで導入しない。

**当面の方針**：Main/Main10の人工GPU成功で全HEICを一般化せず、実RExtのCPU復号や未登録WIC Fantを画素単位で守る。変更のたびにMSIや手動テストを求めず、CIで候補をまとめ、Explorerへの実機確認が必要な明確な成果・重要な節目だけ検証を依頼する。

### 再現性チェック・最終CI

[Windows CI #39750205948](https://github.com/taku077/wic_heic/actions/runs/39750205948)ではCIにCPU時間・終了時 private commitの必須項目を追加して、前回と独立したWindows仮想ランナー上で12回の交互順序ベンチを再検証。全画像PBGRA一致。

- 逐次 483.190ms、プロセスCPU 507.812ms。
- 2並列 307.864ms、逐次比 **36.3%短縮**、プロセスCPU 546.875ms（+7.7%）。
- 4並列 224.801ms、逐次比 **53.5%短縮**、プロセスCPU 726.562ms（+43.1%）。
- 終了時 private commit はおおむね21MiB、ただし処理中のピークではない。

初回の順序交互化ランと続く再実行の両方で、4ワーカーは50%以上、2ワーカーも30%以上の**暖機済み7枚一括生成時間**の短縮を示した。一方で2→4に増やすとCPUコスト上昇が大きい。PCが第7世代Core i5である点・UI応答性を優先する点から、今後の製品候補は最大2ワーカーのキャンセル可能な低優先度プレロード案を**研究対象**とし、標準Explorerが既に並列で処理しているか及びキャッシュ登録動作は未確認のため現行版に導入しない。

警告：計測値は個々のサムネイル1枚における30%短縮ではなく、**7枚全体の待ち時間**。Explorer初回の表示改善を保証するものではない。実機へインストール・ユーザーによる再試験は今回求めない。

## 2026-10-22：個別最適化を実際に組み合わせて評価、1枚・7枚・CPU負荷を区別

ユーザーから「個々は小さな削減でも組み合わせれば30%を超え得る。組み合わせ試験と開発限界・余地を評価すること」という指摘を受け、従来の**個別LUTテスト＋個別並列テストだけでは不足**と認定し、研究CIを拡張。

### 同じ7件の実RExt HEICで「通常スカラー16bit変換／正確LUT変換」×「1／2／4並列」を直接比較

[Windows CI #39751785078](https://github.com/taku077/wic_heic/actions/runs/39751785078) **成功**。CIで研究コピーから製品DLLとは隔離したscalarおよびLUTの2種類を本当に独立して再ビルド。Windowsが同じDLLベース名を再利用し測定を汚染することを防ぐため、**別プロセス**ごとに一時ディレクトリ内の `WicHeicPrivateCodec.dll` を置換して起動するA–B–B–A。各プロセスで1・2・4ワーカーを交互に各4回、一条件合計8回分。7件のWIC Fant 256px PBGRA出力はプロセス内で原画素全バイト一致、プロセス間で全7件の寸法・バイト長・画素ハッシュ一致。別の独立CIではスカラーvs LUTの全元解像度WIC24/32/48/64bppも全バイト一致を証明済み。元HEIC SHA256不変、WIC/Photos/HEIC Managerの登録変更なし。

- scalar/1逐次：全7枚578.886ms、プロセスCPU609.375ms。
- LUT/1逐次：545.836ms（**5.7%削減**）、CPU566.406ms。
- scalar/2並列：350.596ms（**39.4%削減**）、CPU648.437ms。
- **LUT/2並列：328.241ms（43.3%削減）、CPU574.219ms。**
- scalar/4並列：260.584ms（55.0%削減）、CPU808.594ms。
- LUT/4並列：248.459ms（57.1%削減）、CPU777.344ms。

結論：個別最適化を単純加算した予想ではなく**本当に複合実行して30%以上の7枚一括生成短縮を確認**。LUTと2並列の組み合わせは逐次scalarよりCPU使用時間自体も減少するCI結果。4並列は壁時計時間はより短いがCPU総仕事量が大幅に増え、ユーザーの低負荷方針に不利。

### 最初の1枚と4MP画像の準備完了まで測定

`HeicRextBatchAB.h` に、各画像の独立IThumbnailProvider完了時刻を原子キューの固有インデックスに記録して、バッチ最初の完了と第2項目 `04.heic`（2048×2048、10bit RExt）の完了を新たに算出。

[Windows CI #39752323810](https://github.com/taku077/wic_heic/actions/runs/39752323810) **全工程成功**。再度プロセス隔離A–B–B–A、全7枚全バイト一致：

| 構成 | 7枚全完了ms | 1枚目に準備できた画像ms | 2048²の04画像の完了ms | CPU時間ms |
|---|---:|---:|---:|---:|
| scalar/1 | 429.412 | 114.866 | 221.920 | 437.500 |
| LUT/1 | 401.320 | 107.486 | 205.536 | 425.781 |
| scalar/2 | 260.510 | 113.506 | 113.506 | 468.750 |
| **LUT/2** | **247.383** | **109.669** | **109.669** | **457.031** |
| scalar/4 | 192.471 | 6.761 | 160.061 | 621.094 |
| LUT/4 | 179.400 | 6.853 | 144.714 | 562.500 |

- **7枚全完了**：LUT/2はscalar/1比42.4%短縮、LUT/4は58.2%短縮。
- **最初の1枚**：scalar/1→LUT/2の変化は114.866→109.669ms、**4.5%短縮に留まる**。4並列の「最初の1枚6.9ms」はキュー内の**100×100など小型画像が大きな画像より先に完了した結果**であり、注目している大画像を6.9msで描画できるわけではない。
- **04の4MP画像の準備時刻**：scalar/1の221.920→LUT/2の109.669ms（50.6%短縮、先行する03 HEICとの並列による待ち時間削減）。LUT/4だと144.714msになり**LUT/2より遅い**。4並列はCPU競合が大きく、初期表示したい大画像の体感改善に反する可能性。
- CPU負荷：LUT/2は437.500→457.031ms（**約4.5%増**）で42.4%の一括壁時計短縮。LUT/4は562.500ms（約28.6%増）で58.2%の一括壁時計短縮。作業中ピークメモリは未計測、終了時 private commitは約21～23MiB。
- **解釈注意**：Windows Server 2022 CI／ファイルキャッシュ暖機済み、独立Providerを意図的に並列で呼び出すテストであり、**Windows 10 Explorerの実表示処理がこの順序・並列数で動く保証はない**。Explorer自体の既存並列処理、キャッシュやフォルダー画面描画の待ちを省いた時間なので、Explorer初回表示30%以上短縮達成とは記載しない。

### 今後の開発の限界と余地

**単独サムネイルの限界：** `04.heic` の以前の未最適化WIC `CopyPixels` 工程中央値は、HEVC復号＋RGB生成82.555/122.729ms（約67.3%）、WIC生画素コピー・16bit正規化22.587ms（約18.4%）、Fantなど17.532ms（約14.3%）。画素コピー工程を**理想的にゼロ**にしてもその時点の全時間は最大約18.4%しか短縮できず、単一画像30%短縮のためには大きなHEVC復号または後段工程の改善が必須。この比率は4MP／1画像／CI測定であり12MPに外挿しない。現実のLUTによる全工程短縮はおおむね5～10%。

**7枚一括の余地：** 重い大画像が同時に進むことによる待ち行列短縮が大きい。2並列＋LUTは研究CIで40%以上短縮と画素完全一致を複数の独立ランで確認。しかし実Explorerがすでに同じ並列化をしていれば追加利得は小さくなる。スレッド4本は大きな04画像の完了を遅らせ、CPU負荷を増すことも確認。

**次の優先順位：** (1) Explorer実行経路における「最初の1枚」と「フォルダーの7枚」計測を区別、既存Explorerの並列数とキャッシュを研究し、WIC/HEIC Managerを競合させない、(2) 単一HEVC RExtの復号工程（約67%）の真の高速化候補を検証、(3) 2ワーカーの**優先度／停止／低CPU負荷／サイズ別キュー**の動作とピークメモリを自動テストしてから判断、(4) 12MP・10/12bit・ICC/Exif/irotの実コンテナー代表サンプルが不足している限界を保持。GPU Main10人工成功は実RExtへ外挿しない。

**ユーザー負担**：この段階のCI研究に際し新しいMSI、ユーザー実機検証、写真の提出を求めない。製品WICフレームデコーダーのソースSHAを維持する。

## 2026-10-22：v0.4.0の2/4スレッドと今回の並列化を区別、現実的な初回表示改善上限を再評価

**v0.4.0**：`WicHeicFastPreview v0.4.0` にすでに実装された「1 HEIC画像内のHEVC復号ワーカー数」（6MP以上・8bit・アルファなし。1画像単独で4スレッド、同一プロセス内で同時復号なら各2スレッド、4論理CPU未満なら上限2）。固定人工12MPのCI同条件ではHEVC 2スレッド417.86ms、3スレッド342.23ms、4スレッド283.39ms。**10bit RExt・12bit/alphaには適用されない**。ドキュメント `FAST_PREVIEW_MSI_JA.md` と研究用 `HeicBitmapFrameDecodeProfileCI.cpp`（本製品オリジナルと同一のv0.4経路）で確認。

**2026-10-22の新しい2/4並列**：これは「異なる7枚のHEICを、独立した2/4個の`IThumbnailProvider`インスタンスで同時生成」する別種の並列化。1画像内部のHEVCデコードスレッド数を増やすのとは別。GitHub Windows VMで暖機済み7枚の全完成時間がscalar1=578.9ms→LUT+2枚並列328.2ms（43.3%減）、LUT+4枚並列248.5ms（57.1%減）。実Explorerはすでに複数枚を並行処理している可能性があるので、製品へ重複導入しない。

### 新たに実施：v0.4同種の画像内HEVCスレッド調整 × LUTの真の1枚サムネイル複合評価

`HeicRextSingleMatrix.h` を追加し、既存研究用未登録private WIC DLL（scalar版と完全等価10/12bit LUT版）について、GitHub ActionsのCI専用エクスポートを介して単一HEIC復号ワーカーを1/2/3/4に制御。プライベートDLL単位の通常WIC bitmap decoder初期化→1枚のWIC Fant→PBGRA完成までを、3枚の固定実RExt 10bit HEICに対し、ワーカー×色変換6/8パターンそれぞれ4回、ABBA順に測定、結果のバイト列を全パターンで照合した。元写真を変更せず、Windows WIC登録/フォト/HEIC Manager/MSIを一切操作せず、製品用デコーダーはGit blob `c8b08d7adbb47880cdb1d176720d89efbce2ee48` のまま。

[成功CI #39754507671](https://github.com/taku077/wic_heic/actions/runs/39754507671) ：
| 実HEIC | scalar1基準WIC Fant完成ms | LUT/2 threads 完成ms | LUT/4 threads 完成ms | 4 threads短縮 |
|---|---:|---:|---:|---:|
| 04.heic 2048×2048 10bit | 125.049 | 92.456 | 88.134 | **29.520%** |
| fixture-irot3-exif6-noaux 1080×1920 | 78.085 | 63.054 | 56.322 | **27.871%** |
| 03.heic 1920×1080 10bit+alpha | 133.336 | 107.521 | 104.601 | **21.551%** |

[成功再測定CI #39755061819](https://github.com/taku077/wic_heic/actions/runs/39755061819) では**3スレッド**も追加し、全条件PBGRAバイト完全一致。3スレッド＋LUTは順に24.781%、22.366%、18.014%短縮と4スレッドを下回った。4スレッド＋LUTは順に **29.498%、28.097%、21.014%**短縮。2つの独立CIで4MPの29.5%改善が再現された一方、いずれも30%を超えず、他の実HEICでは改善幅が小さい。特に一部の4ワーカー設定では総プロセスCPU時間が1ワーカー時の1.25～1.40倍になる。

**進捗評価（誇張しない）**：
- 4MP/10bit RExt1枚の未登録WIC Fant完成計算に限れば30%短縮へ約0.5ポイント差で、追加の改良が達成可能な余地は現実にある。ただし数値の揺れを考慮すると30.1%の一回限りの測定で完成扱いしない。
- 2MPアルファ付きは21%程度にとどまる。全画像共通で30%以上を実現するには、これまでの細かなLUT・スレッド・AVX2/FFmpeg切り替えを繰り返すだけでは難しく、libheif内部HEVC復号（条件によりWIC全体の約60〜70%）や回転/alpha/Fantの追加処理を抜本的に削る研究が必要。Intel HD620のRExtハードウェア加速は未確認であり、対応プロファイル外で強制GPU化しない。
- 人工12MP 8bit Mainにおけるv0.4の大幅高速化と、ここでの小型4MP 10bit RExtを混同しない。**同じ効果がiPhone実写12MP・Windows 10 Explorerで得られるという実証はない**。既存のWindows 10実機試験ではShell事前生成や再利用は動作したが、Explorer描画時刻・真の初回表示時間は測定できていない。Explorer本体が既に複数枚のサムネイルを並行生成している可能性も残る。
- 期限や「あと何日で実用化できる」といった予告を根拠なくしない。個別改善が重なった場合の**全工程の合算時間を実測**し、1枚/複数枚/CPU総仕事量/ピークメモリ/最終表示を別の合否項目として段階判断する。
- **開発の継続条件**：元画像不変、Fant/PBGRAネイティブ完全一致を守り、研究CIで再現性のある速度短縮が判明すればまとめて実機検証候補へ。1枚の未登録WIC Fantで30%超が複数条件で安定、または複数枚の全工程で30%以上を保持した安全な組込み候補が成立した時点に限り、ユーザーへ段階的なWindows10実機の検証を依頼する。現時点で新MSIを製作・配布しない。
