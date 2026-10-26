# Windows 10 Photos: 連続回転すると横長表示に固着する（v1.1.27実機）

## 重要訂正（ユーザーの追加説明・2026-10-26）

この不具合は「正常に回転し続けた後、突然横長の表示で固着した」という単独症状ではない。

**実際の操作と症状の連鎖（ユーザー目視報告）：**
1. 人工縦長HEICを標準Photosで開き、表示倍率を縮小。
2. **縮小表示のまま回転操作を繰り返したところ、まず画像の表示が崩れた**。この時点は「横向き固着」ではなく、表示画像の変形・乱れ。
3. その縮小状態から**表示倍率を少し拡大したところ、横長表示への固着が発生**し、回転を続けても横長の表示状態から戻らなくなった。
4. 表示が先に崩れていたため、固着が何回目の回転か、固着直前に本来縦長／横長のどちらだったか、利用者には**判断不可能**。ユーザーに連続50回の各クリックの前の正しい向きを申告してもらうことを診断要件にしてはならない。
5. **「縮小・少し拡大」はPhotos画面上の表示倍率の変更**として記録し、HEIC本体の画素寸法を書き換えるリサイズ保存とは混同しない。

**従来解析の誤りと修正：** v0.4診断でFマーカーの時点のディスク上のirot=0（横長）だったという事実自体は正しい。しかし、**保存されたファイルの向きが横長というだけでは、Photosの表示が正しい角度であることも、回転操作が正常に反映されたことも立証されない**。ユーザーがその前に表示崩れを経験し本来の向きを追えなくなった事情を無視して、「ディスクも横長だから表示も一致」と解釈してはならない。Fは「Photosが横長に固着したとユーザーが確信した時刻」であり、正しい回転順序の監視値ではない。

**新たな優先仮説：** 表示倍率の変更と回転で、表示用のビットマップ寸法・縦横比・描画矩形・倍率キャッシュが不整合となり、最初は伸長／見切れとして現れ、続く少量のズームで横長のレイアウトに固着した可能性。旧WICフレームの保持／Photos再描画と、非同期保存の重なりも残す。WIC直結CIで単純な回転・リサイズが成功しても、Photosの**表示倍率＋回転＋さらに倍率変更**の実際の操作順序を再現したことにはならない。

**今後の試験設計:** ユーザーに1回ごとの向きをメモさせず、人工画像で「表示を縮小→高速回転→表示崩れ発生→僅かに拡大→表示固着」という連鎖を一つの再現シナリオとして記録。独立診断はHEICのSHA256・irot・Exifと、任意の「最初の表示崩れ」「拡大操作」「表示固着」の各時点を紐付ける。マーカーは参考・任意で、迅速な連続操作を妨げない。必要なら人工画像だけを映す画面の短い録画を利用者承認で検討するが、プライバシー情報収集やPhotosへのフック、レジストリ変更、MSI導入は勝手に行わない。

**関連資料:** Microsoft WICの変換順序はscale→crop→rotate（https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapsourcetransform）、WIC scalerはCopyPixelsごとにリサンプリングを行い得る（https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nn-wincodec-iwicbitmapscaler）、FlipRotatorはソースに適切なバッファがないと重い（https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nn-wincodec-iwicbitmapfliprotator）。ただしPhotosの「表示ズーム」が必ずIWICBitmapScalerとして実装されているとは限らない。これらは調査上の比較基準であり、原因を断言する根拠ではない。

---

**報告・解析日:** 2026-10-26。**実機版:** WicHeic 1.1.27。**診断:** 独立型HEIC_SaveTiming_Portable_v0_3。
ユーザーが人工の縦長HEICを繰り返し回転すると、Photos画面が**横長のまま**になり、さらに回転操作を続けても横長表示から切り替わらない事例を報告。
開発上の比較対象はGitHubで公開している10,597バイトの人工HEICコピーのみ。ユーザーの私的写真・ファイル名・アカウント・ローカルパス・診断ZIP全体をGitHubへアップロードしない。

## 実機診断ログから直接確認できたこと

- sessionに11件の状態記録（初期＋10回の内容変更）を記録。ユーザーの目視は「横長表示のまま」。一方で監視対象の人工HEICファイル自体は変化を続けた。
- HEIFの回転プロパティ \`irot\`（値0,1,2,3）の記録順序：\`3,2,3,2,1,0,3,2,1,0,3\`。Exif Orientation：初期だけ6、以降は1のまま。
- **全11状態のSHA-256を再構築して照合成功**。すべて元の公開人工HEICと同じ10,597バイトで、HEIF中の既知の\`irot\`1バイトとExif Orientation1バイト以外に差がない。HEVC圧縮データと他のHEIFアイテムはバイト一致・無再圧縮。
- 観測した中で\`irot=3\`はlibheifにより**1080×1920の縦長**として認識。ユーザーが提供した最終作業HEICも\`irot=3,Exif=1\`で、元画像との差はExif Orientationの1バイト\`6→1\`だけ。データ上は縦長なのに画面では横長だったとの報告。
- \`irot=0,2\`ではlibheifが1920×1080横長、\`irot=1,3\`では1080×1920縦長と認識。**縦横が交互に切り替わる**ことを独立デコーダーでも確認。
- 2→3→2と逆転する箇所がある。常に同方向にクリックしたという証拠はないため、保存結果の逆順競合だとは断定しない。
- FileSystemWatcherは\`Created,other-file\`を10件記録するが他ファイル名はプライバシー保護のため収集せず、ファイル一時名・Photos PID・実ファイル置換順序は不明。別ファイルが何か特定できない。
- R=回転操作直後/T=サムネイル更新の**手動マーカー0件**。ログにUI操作時間と画面の縦横の状態そのものは含まれていない。よって「どの更新時点でPhotosが固着したか」を断定できない。
- FileSystemWatcherの通知の重複・取りこぼしがあるため「状態10変化＝利用者が10回だけ操作した」とは断定しない。初期から最後までの瞬間的な全変更が完全に記録されている保証もない。

## 原因候補（根拠と未確定点）

1. **Photosの表示・読み込みキャッシュ不整合（最有力候補）**：実ファイルの表示方向を決める\`irot\`は切り替わり、最終は縦長。それでも実機GUIは横長との報告。Photosが古い画面寸法やWIC IWICBitmapSourceを保持し、再読み込み/レイアウトの再計算が遅れた可能性がある。Photos内ビューアーとExplorerサムネイルキャッシュは区別し、混同しない。
2. **WICが最新ファイルを開くときの寸法通知・フォーマット交渉誤り**：保存バイトが正しくても特定シーケンスでGetSize/CopyPixels/サムネイルに古い寸法が返るかは別問題。GitHub WIC直接試験で、実機ログと同じ人工\`irot\`11状態を個別に開き、戻り値と画像RGBを検証。
3. **Exifとirotの解釈の差**：Exifは最初から1へ正規化され続け、その後の回転は\`irot\`のみ変化。HEIF規格上は\`irot\`が表示方向を規定し、Exifは説明的。Photos等のアプリの混合対応があるため実機画面との照合が必要。
4. **保存完了前の連続クリックと非同期ファイル置換**：一時的に古い版が戻ったり画面更新が取りこぼされる可能性。ほぼ同時の内容変化が複数あるが、UIマーカー・PID・ファイル名が無く未証明。

## 独立調査／実機での次の確認

1. **元画像・Photosを変更せず**、表示が横長に固着したあとPhotosだけを閉じ、新しく同じ人工作業HEICを開いたときに縦長へ戻るかを確認。戻るならPhotosセッション内のキャッシュ／表示更新が強く疑われる。戻らない場合はWIC GetSize/Thumbnail/表示側の処理をさらに切り分ける。
2. 独立診断v0.3のR/Tマーカーは任意。操作時刻と画面確認時刻を示す目視情報を併せると因果関係の解明が進む。**診断ON/OFF、MSI交換やWindows再起動は不要**。
3. Windows CIで\`wictest/WICPhotosExifOnlyProbe.h\`に\`RunObservedIrotSequence\`を追加。11状態の人工ファイルは毎回新規作成し、元画像は読取専用。WIC \`GetSize\`とRGBを読み直し、\`irot\`奇数で1080×1920、偶数で1920×1080であることを要求。Photos GUIと同一ではないことに注意。
4. GUI固着の実機再現を自動CIで直接保証するのはWindows ServerのWICテストだけでは不可能。重複修正・根拠のないレジストリ操作・サムネイルキャッシュ全消去を避ける。
5. WicHeic v1.1.27実機の元バイナリを勝手に更新しない。新MSIは一連の安全検査と実機復旧用MSI確保後にユーザー承認のうえ提供。

## 公開参照資料

- Microsoft WICの90/180/270度の規定: https://learn.microsoft.com/en-us/windows/win32/api/wincodec/ne-wincodec-wicbitmaptransformoptions
- Microsoftサムネイルキャッシュの更新と最終更新日時: https://learn.microsoft.com/en-us/windows/win32/shell/thumbnail-providers
- Microsoft SHChangeNotify（SHCNE_UPDATEITEM）: https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shchangenotify
- Microsoft FileSystemWatcher: https://learn.microsoft.com/en-us/dotnet/api/system.io.filesystemwatcher
- pillow-heifのExifとirot差分: https://pillow-heif.readthedocs.io/en/latest/workaround-orientation.html
- PhotoPrismのExif OrientationとHEIF Rotation: https://docs.photoprism.app/developer-guide/metadata/orientation/
- ExifToolタグ表: https://exiftool.org/TagNames/

## Windows CIの最新検証結果

- **Actions [#40277179361](https://github.com/taku077/wic_heic/actions/runs/40277179361) SUCCESS** （コミット `912870131ca660fdc236a5c99b9e67560c0f36c5`）。ログ中の人工HEIC `irot`11状態をWIC直接呼び出しで再現し、縦横がすべて一致：`WIC_PHOTOS_OBSERVED_11_IROT_STATES_MATCH_GEOMETRY_PASS last_irot=3 last_dims=1080x1920`。
- Exifだけ`6→1`へ変えた場合はWICの画像寸法・RGBとも変更なし：`WIC_PHOTOS_EXIF_ONLY_NOT_ACTUAL_90_ROTATION_PASS`。
- 50回の同名人工作業ファイル入替、8並行×50件、縮小回転試験、MSI隔離導入・削除・登録復旧も回帰試験合格。ユーザー実機のPhotos GUIは未検証。
- これにより**独立デコーダーlibheifと、WIC直接呼び出しはこの11状態について正しい縦横を返す**と確認。異常がPhotosのアプリ内表示キャッシュ/再読込経路にある可能性が上がる。ただしPhotosが読み出す実経路・古いWICフレームを保持する経路・更新通知の障害は未確認なので、Photos固有と断言しない。

## 2026-10-26 実機再オープン検査：縦長に復旧（ユーザー確認）

**事実:** 連続回転の後、標準Photos画面が横長から変化しなくなった状態で、Photosを一度閉じて、**同一の** `synthetic-working.heic` を改めて開いたところ、ユーザー実機で**正しい縦長の表示へ復旧した**。

**組み合わせた証拠:** 人工HEIC最終状態は `irot=3`, Exif Orientation=1, 1080×1920。HEVCデータ保持。独立libheifもWIC直接呼び出し（CI #40277179361）も当該11状態を正しい縦横寸法で復号。再オープンではPhotosも縦長表示。

**解釈:** 同じファイルを新規に開いて正常表示へ戻るため、ファイル破損・回転情報欠落・恒常的なWIC復号失敗の仮説は後退。**Photosの開いている間の表示データ／寸法キャッシュ／保存後の再読み込み・レイアウト更新不足**が最も有力。標準Photosが保持しているIWICBitmapFrameDecodeなどの古いフレーム、非同期保存、同一ファイル置換通知、ビュー内ビットマップ更新のどこで stale となったかは未特定であり、Photos側の実装を証拠なしで断言しない。

**WIC側の制約:** WICの `IWICBitmapDecoder::Initialize` は初期化された入力ストリームに結び付いており、外部アプリが保持中の既存デコーダーをコーデック側が勝手に再オープン・再初期化できるとは限らない。保存の `IWICBitmapEncoder::Commit` 成功は、Photos UIのファイル置換と描画更新完了を意味しない。 `SHChangeNotify` はShell通知であってPhotosの表示キャッシュ更新保証ではない。現状ではコーデックへ根拠なくShell通知・ファイル監視・強制キャッシュ破棄の処理を追加しない。

**今後の焦点:** 必要に応じて人工HEICのみで、Photosを開いたまま別画像へ移動して戻る時（ビュー内再選択）と、Photosを終了して再オープンする時（アプリ側再生成）を区別する。操作と表示更新の時刻を独立v0.3のR/Tマーカーへ記録可能。PhotosプロセスまたはShellへの干渉・レジストリやMSI更新・キャッシュ全消去は行わない。

公開仕様:
- Microsoft IWICBitmapDecoder::Initialize: https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapdecoder-initialize
- Microsoft WIC decoder implementation: https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapdecoder
- Microsoft SHChangeNotify: https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shchangenotify

## 2026-10-26: 保持中の旧WICビットマップと最新デコーダーの対照試験

Microsoft公式のIWICImagingFactory::CreateBitmapFromSource仕様では、WICBitmapCacheOnLoadは呼び出し時点のビットマップをメモリへ複製する。元ファイルが変更されたらそのコピーも自動で更新されるという仕様ではない。
https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicimagingfactory-createbitmapfromsource

- wictest/WICPhotosReplace50Probe.hに否定対照を追加。公開人工HEICのメモリスナップショットから古いWICデコーダー／キャッシュ済みビットマップを作り、テスト終了まで保持。旧WICビットマップの入力はメモリストリームとし、古いファイルハンドルがWindowsのReplaceFileWを妨げる条件を排除。
- 別の人工試験専用フォルダーのsynthetic-working.heicを、WICで90度回転し新しい候補ファイルに保存、バックアップを残してReplaceFileWで同一パスへ置換、再度新規WICデコーダーで読み込む試験を50回実行。
- 結果：50回すべて、最新ファイルの再オープンは新しい寸法・内容を返す一方、保持中の旧メモリビットマップは最初の1080×1920の寸法とRGB画素から変化しない。
- Windows Actions #40299312212 SUCCESS。ログ WIC_PHOTOS_HELD_VIEWER_BITMAP_STAYS_OLD_FRESH_DECODER_UPDATES_PASS old_dims=1080x1920 new_decoder_checked=50 を確認。既存の11回転、Exif-only、50回逐次／並列、縮小回転、MSI隔離導入・削除・登録復元も合格。
- 重要：これはWICの正常なメモリキャッシュ動作を模倣した人工実験であり、Photosが内部で同じ方式のキャッシュを使用している証拠ではない。Photosのセッション表示更新不良という仮説を技術的に説明できるが、原因断定ではない。WIC DLLから他アプリの既存メモリビットマップを強制更新する改造は行わない。

## 独立型HEIC SaveTiming Portable v0.4（Windows 10実機は未検証）

- v0.3の専用人工HEIC（ハッシュ・容量・irot・Exif）監視に加え、Photosと一致するプロセス名に限定したPID件数とMainWindowHandle有無を約1秒ごとに観察し、変化時だけHEIC_SaveTiming_PhotosProcesses.csvへ保存する。
- 追加マーカー：F=横長に固着、C=Photosを閉じた、O=再度開いた、V=縦長に復旧。R/Tは従来どおり。操作者が監視コンソールを前面にしてキーを押した時刻であり、アプリの操作そのものを正確に示す時刻ではない。
- Windows 10のPhotosプロセス名は版によって異なり、対象PID=0やMainWindowHandle=0でもアプリが存在しないとは断定しない。PID以外のユーザー情報、写真、パス、window titleは収集しない。
- インストール／再起動／レジストリ変更不要。毎回新しい人工画像の作業コピーだけを記録。ZIP CRC、同梱人工HEICのSHA256一致は確認済み。PowerShell 5.1実機での動作は未確認。
- 次の切り分けでは、横長に固着したあとPhotosを終了させず別の人工画像に移動して戻った場合と、Photosを閉じて開き直した場合を比較し、表示の再生成条件を調べる。

## 2026-10-26 20:43–20:45 JST：独立診断v0.4の追加実機証拠（匿名化要約）

ユーザーがv0.4で人工HEICの高速回転・表示固着・再オープン復旧を検証。GitHubには**診断ZIP自体、Photos PID、ローカルパス、ユーザー画像を一切アップロードせず**、以下の事実だけ記録する。

- v0.4 CSV：初期＋6状態遷移、人工画像10,597Bは全状態で固定。irot `3→0→3→0→3→0→3`、Exif `6→1` の後は1のまま。最終HEICは元公開人工fixtureとの差がExif1バイトだけで、元HEVC・追加画像も保持。reference unchanged。
- マーカー F（横長表示固着の申告）が20:44:13と20:44:28頃に記録。その両時点で**直前に確認できるディスク上のirotは0（横長）**。つまりこのセッションのFマーカー自体は「ファイルが縦長なのにPhotosだけ横長」の直接証拠ではない。固着までに利用者がどれだけ高速クリックしたか、クリック時点の向きはログ未収集。
- Photosウィンドウの検出数は20:45:19頃に1→0、20:45:28頃に0→1へ変化。プロセス数は0にならず、バックグラウンドが残った可能性がある。これは**Photosアプリが完全終了した証拠ではない**。
- 20:45:33頃、**作業HEIC自体がirot=0（横長）からirot=3（縦長）へ変更**。その約9秒後の20:45:42頃に V（縦長に復旧の申告）が記録される。
- **重要な交絡:** 今回は「Photosを閉じて開き直したことのみ」で縦長に戻ったとは断定できない。再オープンと近接して**ファイル自体も横→縦へ更新**されたため、データ変更・保存待ち結果・UIキャッシュ再生成のいずれが直接効いたか切り分けられていない。以前の「Photosセッションキャッシュが最有力」という判断は仮説として保持し、確定と扱わない。
- 観察7件中、FileSystemWatcherは7個の `Created,other-file` を記録。プロバイダーイベントは重複・欠落や一時ファイルを含み得るため、枚数・保存完了時点・原因となるプロセスはこのログだけでは不明。

**次の分離試験:** 同じ人工作業HEICで横長固着時に独立監視を続け、まず操作を止めてディスクのirotとSHA256が一定であることを確認したうえで、Photosを閉じずに別の**人工画像**へ移動→元に戻る試験、続いてPhotosを閉じて再オープンする試験を区別する。ただしユーザー写真や元画像の上書きは禁止。マーカーF/C/O/Vだけでなく実際の操作方向をR記録できると順序解析が改善。端末に新MSI導入／レジストリ編集／強制キャッシュ削除は不要。

Microsoft根拠: https://learn.microsoft.com/en-us/dotnet/api/system.io.filesystemwatcher ; https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicimagingfactory-createbitmapfromsource


## 2026-10-26 21:49〜21:51 実機録画＋独立v0.4診断の照合（匿名化結果のみ）

ユーザーからOBS録画（H.264 1280×720／30fps／約134.23秒）と独立v0.4診断ZIPを受領。**録画原本、ログZIP、スクリーンショット、Photos PID、Windowsパス等をGitHubへアップロードしない**。以下は、映像を直接確認し、診断ログと突き合わせた匿名化所見。

### 明確に見えたPhotos GUIの現象

1. 録画開始から約48秒：Photos左下の画像情報は **1080×1920（縦長）**、倍率10%。画面中央の実描画画像は**横長**。同一のPhotos UI内で表示寸法の記述と描画結果の縦横が食い違う。
2. 約79秒：画像情報は再び **1080×1920**、倍率36%、しかし実描画は**横長**。
3. 約119.25〜128秒：Photosの上部/下部UIと画像情報 **1920×1080**、倍率20%は残る一方、画像領域は**全面真っ黒で画像が見えない**状態が約9秒持続。約128.25秒以降に画像が再表示（その前後で倍率操作がある）。録画による直接証拠。
4. 映像の途中には拡大時の画像の一部見切れと、表示縮小・回転・ズーム変化が複数回現れる。倍率が大きいだけで生じる正常な見切れと、上記の情報/描画食い違い・長時間の画像消失を混同しない。
5. **重要な解釈制限：** フォトのプレビューでは、ディスクの保存前に描画方向だけ変わることもあり得る。「画面の寸法表記が縦長・プレビューが横長」という単一フレームだけからWICの保存バグ/Photosキャッシュバグを確定できない。しかし映像に矛盾した表示状態と9秒間の画像消失が実在する事実は、以前の目視報告のみより強い証拠。

### 独立診断と録画の時間範囲・重要な不一致

- v0.4診断開始は約21:48:13 JST、**180秒で21:51:13頃に終了**。OBS動画はファイル名の時刻に基づく推定で21:49:17開始〜21:51:31終了。したがって**最後の約18秒の映像は診断の観測範囲外**。約119〜128秒の黒画面状態はこの未監視区間に入る。録画ファイル名を基準とする時刻対応には数秒の不確実さあり。
- 診断CSVは初期＋7回の状態変化（計8状態）を記録。観測中のHEICは全回10,597バイトで元の人工画像と\`irot\`／Exif Orientationの指定位置を除いて一致。 \`irot: 3→1→0→1→3→1→0→3\`、Exif: 初回6→1、以降1。再圧縮・付属HEIFアイテム損失はこれらの観測状態に見られない。
- **監視終了時のResult.txtは作業画像SHA-256 \`A680DF74DCFE858EFAA5C29BFB39658628D1DF1C30891759375069BD893A314B\`（irot3/Exif1）を記録**。しかし後からZIPに収められた\`synthetic-working.heic\`は **SHA-256 \`8DC7DC76D5A27BFA13FAAAC40E45A9CAD52DA183FB3989A87B33130BA4CC370A\`（irot1/Exif1）**。これらは同一時刻の状態ではない。監視終了後に編集が進んだ可能性、または別時点でZIP化された可能性があるため、監視終了時の状態として後者を採用しない。元の人工HEICは保護されている。
- 画像が見えない約9秒の時点のHEIC回転情報・SHA-256は**記録されていない**。録画された黒画面を特定のirot値に紐付けたと断言しない。
- 手動マーカーは今回0。操作ごとに切り替えてR/F/Tを押させる方式は高速連続試験の流れを阻害するため必須にしない。

### 今後の開発・調査の優先順位

1. **今回の録画はWICの「回転情報を書けない」ことよりもPhotosの画面描画・倍率変更・回転の同期を調べる根拠。** ただしPhotosとWICの境界にある表示ソース/寸法交渉/キャッシュはまだ未特定。憶測でコーデックの再圧縮処理を変更しない。
2. 独立診断v0.5は既存のWindows標準フォルダ選択・人工画像限定を維持し、標準時間を360秒（Qで停止可能）に延長。**診断終了時の人工HEICを別名で一度だけ新規保存し、そのSHAをResultに記録**。後からZIPへ入れた画像との差を防ぐ。Windows10/PS5.1での実機動作は未検証。
3. OBS録画を開始する前に独立監視を開始し、録画終了後にQで独立監視を終了する運用を推奨。映像とファイルログは共通のUTCを直接共有していないため、録画ファイル名由来の開始時刻対応は推定として扱う。
4. もし再現が必要ならPhotosだけ、人工画像だけを対象に、同一表示セッションの「縮小→高速回転→表示崩れ→僅かに拡大→画像固着/消失」の一連を撮影。各クリックの向きはユーザーに申告させない。実写真、IMG_4074、HEIC Manager、WIC登録へ触れない。現行実機MSI 1.1.27維持。

関連Microsoft公式資料:
- WIC FlipRotator とバッファリング: https://learn.microsoft.com/en-us/windows/desktop/api/wincodec/nn-wincodec-iwicbitmapfliprotator
- WICのフレームデコード: https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nn-wincodec-iwicbitmapframedecode
- FileSystemWatcherの重複・取りこぼし: https://learn.microsoft.com/en-us/dotnet/api/system.io.filesystemwatcher


## 2026-10-27 00:34–00:37 JST：v0.5.1＋OBS第2回録画の匿名化照合

**使用版:** 実機WicHeic 1.1.27、HEIC_SaveTiming Portable v0.5.1。ユーザーが公開人工2フレームHEICで録画・状態監視を実施。**個人情報、元録画、診断ZIP、画像キャプチャ、Photos PID、ローカルフォルダ名はGitHubに転載しない。**

### 監視記録と保全

- 監視開始 00:33:56.96 JST、停止 00:37:25.55 JST（約208.6秒）。200秒の録画ファイル名「00-34-08」による推定開始／終了は約00:34:08–00:37:28。映像の最後約3秒だけ監視範囲外。MP4内部に絶対開始日時メタデータはないため、相関は**概算**。
- 状態CSVは初期＋10回の変更＝計11状態。HEICの11状態はすべて10,597バイトで、公開人工原本の**irotバイト（offset567）とExif Orientationバイト（offset10589）だけの変更**で各SHA-256を**完全再現**。改変／再圧縮／付属画像消失の兆候なし。
- \`irot\`（初期→最終）=\`3,2,1,0,1,0,3,2,1,0,1\`、Exif Orientation初期6→以後1。
- 停止時スナップショットのSHA256=\`8DC7DC76D5A27BFA13FAAAC40E45A9CAD52DA183FB3989A87B33130BA4CC370A\`。ZIP同梱のwork/synthetic-working.heicと全バイト一致。停止時\`irot=1, Exif=1\`。referenceのSHA=\`D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95\`、保全確認。v0.5で起きた同梱人工HEIC参照パスエラーはv0.5.1で解消して実機ログ生成に成功。
- 操作マーカーは0件。Photosのプロセス検出は実装依存。FileSystemWatcherイベントは12件。イベント数≠保存処理回数。

### 直接映像で確認できた異常

- 録画経過約25.5〜31.0秒と34.0〜41.0秒（0.5秒間隔で人工グラデーション領域の可視性を解析）に、**Photosのボタン・画像情報・ズーム率が残るのに、中央画像だけ消えて真っ黒**となる状態を記録。代表画像：30秒はUIの1920×1080／倍率20%表示、38秒も1920×1080／40%表示。最後のPhotosウィンドウ消失後の黒画面（約186〜200秒）は画像描画異常に数えない。
- 約50秒の1920×1080表示は横長画像が画面の上に偏っており、同じ中央倍率でも91秒の縦長画像は中央付近。ただし、拡大・縮小のカーソルアンカー／スクロール仕様が原因である可能性もあるため、**上寄りだけで不具合確定しない**。
- 画像の表示形状は録画の約90秒台などで横長／縦長に切り替わる。ファイルの\`irot=0\`が約00:35:16.44～00:36:08.78の約52.3秒間変わらない区間と重なるため、**Photosプレビューの方向切替と物理HEIFの更新は1操作1保存で同期しているとはいえない**。ただし動画開始時刻をファイル名から推定しており、個々の操作と保存の正確な対応は未証明。
- 本事例で\`irot\`が正しく保持されたことは、描画エラーがWICに無関係と証明するものではない。Photos表示キャッシュ、UIレイアウト更新、WIC GetSize/CopyPixelsの順序、描画用サーフェス、ファイル置換後の同期、Zoom anchorなどを引き続き比較する。

### 開発への具体的示唆

1. 保存コーデックの無再圧縮回転をむやみに再修正しない。今回の11状態のHEVCデータは保持されている。
2. Windows 10標準Photosで「**倍率変更と回転の途中で画像だけ数秒間非表示になる**」ことを新しい必須再現条件とする。WIC直接の回転50回試験で検出されない**UIの保持フレームと描画状態の問題**を対象に分ける。
3. 独立診断の画像ファイル監視は既に使用可能。次のバージョンで必要なら「動画の開始時刻をOS時計に結びつけるタイムシンク機能」を優先。ユーザーにクリックごとのマーカーを強制せず、プレビューの連打を妨げない。
4. Photosの画面消失と監視プロセス0を混同しない。WIC/MSIの更新を伴わず、人工HEIC・録画・ログの時系列から引き続き調べる。

参照:
- https://learn.microsoft.com/en-us/windows/desktop/api/wincodec/nn-wincodec-iwicbitmapfliprotator
- https://learn.microsoft.com/en-us/dotnet/api/system.io.filesystemwatcher
