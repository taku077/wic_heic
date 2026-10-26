# Windows 10 Photos: 連続回転すると横長表示に固着する（v1.1.27実機）

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
