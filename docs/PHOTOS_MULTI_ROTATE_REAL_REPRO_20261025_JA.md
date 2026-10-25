# Windows 10 Photos/HEIC 連続回転 v0.4 — 実機ログ・差分・再現条件（2026-10-25）

## 0. 守る条件
提出期限：**2026-10-27（火）20:00 JST**。既存の1.1.23は連続回転時の容量増大/表示の不整合に対応済みと扱わない。ユーザーの写真やHEIC本体・ユーザーの実ファイル名/個人パス/生ログはGitHubに公開しない。
データ：利用者が意図的に提供した`HEIC_Rotation_MultiRotate_Diagnostic_v0_4(1).zip`の**2つの診断セッション**。原本は読み取り専用で調査し、元画像の自動変更・削除なし。

## 1. ユーザーが確認した対照的な現象
通常利用時：回転を続けると標準フォトが一時的に横方向へ伸びる、表示領域が細長くなる、画像が見切れる。回転保存でファイル容量が元2.07MBから6.28MBまで増加した。
診断中：同じ実機の標準フォトでも**回転を繰り返す間、目視での引き伸ばし/見切れが発生しない**。これは重要なA/B条件差であり、単に「診断すると正常」によってコーデックの描画保存問題が解決したと判断しない。

診断キットの相違点を実スクリプトで確認：
- 原本は選択した場所に残し、新しい`Sessions/HEIC_Test_...`配下の**毎回異なるフォルダ**にある`work/rotate-working.heic`を開く。`Start-Process -FilePath $working`で関連付け起動。**普段の同名ファイルで繰り返し起動する操作・Explorerサムネイルキャッシュの条件とは異なる**。
- 画像の初期表示を目視で確認した後、診断ログを開始。**回転後は保存完了を待ってOK→WIC寸法照会・SHA計算・コピー保存→ダイアログ→次の回転**という長い介在がある。普段の速い連続押下とはイベントの時間間隔が異なる。測定時点にもWICファイル再読込が発生する。
- このため「表示バグなし」は間隔/フォーカス/一時フォルダ/キャッシュ/ファイル置換の差が候補。どれかを原因と断定はしない。

## 2. 診断ZIPから直接判明したファイル変化
### 実機セッション A（画面操作記録1チェックポイント）
- 原本2,983,661バイト、WIC 3024×4032、主HEIC 6×8 grid、irot=3。
- チェックポイント1＝8,311,055バイト、WIC **3024×4032（まだ縦長）**。開始時より178.55%増。
- フォルダー監視で**HEICテスト対象のリネーム・置換6件**、途中7,472,575→8,291,801→8,442,340→8,311,055バイトなどに推移。
- WICログは`EncoderInitialize=7`、`EncoderCommitRecompressed=4`、`EncoderCommitLossless=2`、`PhotosPureRotateLossless=0`。最終のエンコーダー初期化は完了ログなし。
- 表示上の伸縮はユーザーにより「出ない」と報告されたが、出力容量と方向は大きく変化。

### 実機セッション B（操作記録2チェックポイント）
- 原本2,068,851バイト、WIC 3024×4032、grid、irot=3。
- 第1チェックポイント：6,198,860バイト、3024×4032 **縦長**。
- 第2チェックポイント：6,288,651バイト、4032×3024 **横長**、記録時刻 **2026-10-25 09:50:19.941Z**。
- 診断終了時：**6,024,772バイト、3024×4032 縦長**。ディスク上のファイル内容・寸法が第2チェックポイントと異なる。
- 監視ログに **2026-10-25 09:50:24.550Z**（第2チェックポイントから約4.6秒後）、実験用HEICの新たな置換イベントが記録されている。
- WICログ：`EncoderInitialize=6`、`EncoderCommitRecompressed=3`、`EncoderCommitLossless=3`、`PhotosPureRotateLossless=1`。無再圧縮成功と再圧縮が**同一診断期間に混在**。
- 検証時の作業コピーと原本・保全コピーは別。原本と保全コピーのSHA一致、いずれも変更なし。

### TMPイベントについて
- 作業フォルダで一時`.tmp`ファイル出現と消滅を複数回観測。FileSystemWatcherの作成/削除イベント数は複数発火し得るため、そのまま「作成回数」とはみなさない。
- 連続する置換時に`.tmp`の生成・消滅が近接する。しかし**生成元PIDは不明**。自作WICの`EncoderCommit`はIStreamへ書き込むだけでファイル名を作成するコードではない。Photosあるいは別処理が提供した保存ストリーム・一時ファイルを、アプリ側が置換している可能性。直接の発生元はまだ特定できない。

## 3. 解釈 — 事実と仮説
**確定した事実**：標準フォトによるとみられる操作中、WICが同じ画像処理期間に複数回呼び出され、実HEICファイルの置換が複数回起きる。再圧縮と無再圧縮が混ざる。チェックポイント記録後にも置換が継続した事例がある。表示に変形が出なくてもファイルのサイズ/方向は変わった。
**第一候補の仮説**：保存済みファイルの置換・WIC更新・フォトの描画レイアウト/画像キャッシュの状態反映が非同期で、速い連続回転では古い画素サイズと新しい縦横比が一時的に混在し、引き伸ばしや見切れとなる。診断時の操作間隔/モーダルダイアログが競合を回避している可能性。
**第二候補**：毎回新しいパスを開くため、普段の既存パス画像に残るサムネイル・ビューアキャッシュの挙動が違う。
**第三候補**：被験写真の元のHEIFタイル構造/irot/Exif/埋込thumbnailなどと、再エンコード後の単一HEVC画像に差があり、途中の再読み込みでgeometryが変わる。
`WICBitmapFlipRotator`のピクセル・スキャンラインアクセスの違いはMicrosoftも注意しているが、これだけで寸法変形が発生したとまでは立証できない。

## 4. コードの既知制約 — 追加研究ではなく修正対象
- `PhotosPureRotationBridge.h`の自動無再圧縮振分けは**時計回り90度のみ**、**一度使うと元データを消費**、**30秒で候補期限切れ**。連続回転後に画像の向きが180度/270度となると一致せず通常再エンコードへ落ちやすい。
- `HeicBitmapEncoder.cpp`の不一致フォールバック品質は**92**、実測における2MB→6MB級容量増と整合する。ただしその割合だけで画質差は測れない。
- ログは無再圧縮成功を`PhotosPureRotateLossless`で報告するが、不一致による振分け失敗理由は記録しない。

## 5. 対応優先度
1. 再現条件が揃っているので、**この診断資料を起点に開発**。同じ確認を無制限に繰り返さない。
2. 一時元データが存在する範囲で、連続して回転した後の**180度/270度**も画像全画素一致が成立すれば無再圧縮に接続できるようにする。書き込み原本を変更せず、Exif/HDR/タイルは既存のfail-closed安全ゲートに従う。実編集は通常保存へフォールバック。
3. 保存時のWICジオメトリー一致検査、出力Exif/ICC、元ファイル保護を維持する。
4. Explorerサムネイル経路と自作HEIC Managerの設定は変更しない。
5. 2026-10-27 20:00 JSTまでの新MSIは今回の結果と未解決事項を明記して提供する。1.1.23の成功で期限達成扱いにはしない。

## 横断資料
- Microsoft WIC flip-rotator per-pixel/scaline behavior: https://learn.microsoft.com/en-us/windows/desktop/api/wincodec/nn-wincodec-iwicbitmapfliprotator
- Microsoft WIC source transform: https://learn.microsoft.com/en-us/windows/desktop/wic/-wic-imp-iwicbitmapsourcetransform
- libheif tiled image geometry/transforms: https://github.com/strukturag/libheif/wiki/Reading-and-Writing-Tiled-Images
- libheif Exif rotation interaction: https://github.com/strukturag/libheif/issues/227
- ExifTool QuickTime/HEIC irot/ispe docs: https://exiftool.sourceforge.net/TagNames/QuickTime.html
- Windows FileSystemWatcher duplicate event behavior: https://learn.microsoft.com/en-us/dotnet/api/system.io.filesystemwatcher
