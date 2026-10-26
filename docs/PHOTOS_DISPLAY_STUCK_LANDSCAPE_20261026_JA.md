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
