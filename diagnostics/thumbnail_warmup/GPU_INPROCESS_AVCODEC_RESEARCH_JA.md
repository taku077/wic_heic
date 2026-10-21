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
