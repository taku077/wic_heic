# HEIC初回サムネイル：GPU復号に向けた表示方向・HEVC抽出の研究状況（2026-10-21）

## 研究の目的と今後の配布方針
Windows 10 x64／第7世代Core i5環境で、**埋め込みサムネイルのないHEIC**の初回Explorer表示を大幅に短縮する。GPUのハードウェアHEVCデコード候補に必要なデータ経路を検証する。現行v0.3.0/v0.4.0のインストールは変更しない。**今回MSIを作成・配布しない。**

## HEIC→HEVC Annex Bビットストリームの無再圧縮抽出
人工4032×3024 HEICで、FFmpegの `hevc_mp4toannexb` によるHEVCビットストリーム抽出が成功。再復号後の全BGRA画素SHA256が、HEICから直接復号した画像と一致。

CI: [39598532294](https://github.com/taku077/wic_heic/actions/runs/39598532294)（成功、初回ビルド）、[39598959006](https://github.com/taku077/wic_heic/actions/runs/39598959006)（GPU判定修正版成功）。

## HEIC固定サンプル7件の画素再現
回転／色プロファイル／bit深度を含む既存リポジトリ固定HEICサンプル7件について、元HEIC直接デコード vs HEVC Annex B再復号を比較。

- 補正なしで全BGRA画素が一致：5/7件。
- 残り2件は `fixture-irot3-exif6-noaux.heic` と `fixture-irot3-exif6.heic`。無補正では異なり、**時計回り90度を適用すると両方とも全画素が一致**。
- `ffprobe`はこの2件に**rotation = -90**と3×3 Display Matrixを報告。ほか5件には回転行列がない。
- FFmpegの`-noautorotate`で直接HEIC復号した場合は、回転のないHEVC再復号の画素と一致。HEICの表示変換が差の原因であることを裏づける。
- CI：[39599621239](https://github.com/taku077/wic_heic/actions/runs/39599621239)（7件中5件直接一致）、[39600004712](https://github.com/taku077/wic_heic/actions/runs/39600004712)（2件の回転候補比較成功）。

## メタデータに応じた変換ルート（自動・失敗時は保守的に拒否）
`diagnostics/thumbnail_warmup/HeicOrientationPolicy.ps1` がFFprobeの**角度と3×3行列を照合**し、純粋な0／90／180／270度の回転以外は受け付けない。鏡像、平行移動、拡大縮小、せん断、表示行列重複、旧`rotate`タグの競合などを明示的に拒否する。

`CI_HeicOrientationPolicyTests.ps1`が純粋な回転5例の受理と、不正行列／矛盾したメタデータ6例の拒否を検証。`CI_HeicMetadataDirectedParity.ps1`はFFprobeメタデータで選択した補正を適用し、**7/7件のBGRA出力が完全一致**することを確認した。

CI：[39602341020](https://github.com/taku077/wic_heic/actions/runs/39602341020)（成功、厳密な表示行列チェック＋7件パリティ）。

## HEIF正式APIからの独立した検証
別ブランチ`experiment/heic-gpu-decode-probe`で、`HeifPrimaryTransforms.cpp`を実装。`heif_context_get_primary_image_ID`と`heif_item_get_transformation_properties`を利用し、プロパティの**関連付けと適用順序**を読み取る。画像データの復号やファイル更新は行わない。

7件中、5件に回転プロパティなし。問題の2件に `irot` `ccw=270`（時計回り90度）が1個ずつ存在。`imir`（鏡像）・`clap`（切り抜き）はこの7件では0。FFprobeの−90度と整合する。

CI：[39602004524](https://github.com/taku077/wic_heic/actions/runs/39602004524)（成功）。

## GPU対応判定の限界
D3D11の `ID3D11VideoDevice`、HEVC Main/Main10、NV12/P010、4032×3024でのデコーダー構成を問い合わせる**読み取り専用のプローブ**がコンパイル・実行成功。Microsoft Basic Render DriverのVendor 0x1414 Device 0x008cをソフトウェアとして認識し、ハードウェア対応を誤判定しないよう修正した。

CIではハードウェアアダプター0・12MP対応HEVCプロファイル0。**GitHubの仮想ランナーの結果であり、ユーザー所有PCのIntel内蔵GPUが非対応という結論ではない。** CIではGPUにビットストリームを実際に投入して復号する段階には未到達。

## 残課題／採用不可条件
1. 実際にIntel内蔵GPU上でD3D11 HEVCデコードが動くか、初回ハードウェア初期化のコストを含めて何msになるかは**未検証**。
2. 今回のFFmpegソフトウェア復号の全画素一致は、**GPUデコード後の色変換・向き・クロッピングが一致する保証ではない**。
3. 鏡像（`imir`）、切り抜き（`clap`）、両者の複合と複数プロパティ順序、アルファ、ICC／NCLX／HDR、10/12bit、タイル画像は未検証。これらを強制的に高速経路へ送らず従来復号へ戻す。
4. HEVCへのFFmpegコマンドライン抽出は研究用。高速な本番Shell実装には、**外部プロセスを起動せずにHEIC主画像のビットストリームを取り出す方法**の実装・検証が必要。
5. ユーザー所有のHEIC原本は上書き・削除しない。WIC/Photos/HEIC Managerの登録を変更しない。十分な初回速度改善・画質維持が測定されるまでMSIの配布は行わない。
