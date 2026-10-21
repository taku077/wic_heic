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
