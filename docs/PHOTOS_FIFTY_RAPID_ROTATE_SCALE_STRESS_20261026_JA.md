# Windows 10 Photos/WIC: 50回の高速回転、縮小・回転および同時保存の回帰試験

**開発日:** 2026-10-26。**実機基準:** WicHeic 1.1.27。**MSI:** 現行実機1.1.27には今回の新規コードは含まれない。不要なMSI再配布をしない。
**目標:** ユーザー実機では少なくとも50回、間隔を置かず回転操作する。表示ズーム縮小と画像そのもののリサイズ後の回転も区別して検証する。
**安全:** GitHub内の公開人工HEICのみ、出力は毎回別の新規パス。原本上書き・自動削除・IMG_4074・HEIC Managerへの干渉は禁止。

## 仕様別の自動テスト

| モード | コマンド（CI内部） | 合格基準 | 残る制限 |
|---|---|---|---|
| 既存4連続回転 | `wictest.exe` | 4回それぞれ無再圧縮・全HEIFバイト一致、最後のRGB画素復元 | 高速操作や実機GUIの競合は試せない |
| 50回連続90°回転保存 | `wictest.exe --stress50` | **待機なし**で前の保存結果を次回へ使用。50回すべて期待HEIFと全バイト一致、サイズ一定、4回ごとに元RGBへ復帰、50回目のRGBは初期画像180°と完全一致 | 直前のCommit成功後に次を開始する逐次実行。Photos側の同一ファイル置換／サムネイル更新は対象外 |
| 画面表示のみ縮小→回転 | `--stress50` に統合 | 各ターンでIWICBitmapScalerを使って元画像の半分の寸法の表示用データを生成。原画像のGetSize不変、保存は元解像度で回転 | あくまでWIC表示側のシミュレーションでPhotos内部のUI倍率キャッシュまでは再現しない |
| 実画素縮小→回転／回転→縮小 | `wictest.exe --scale-matrix` | 6条件。約50%縮小、541×959等の端数、Fant／Linear、90／270度、変換順序を組合せ。出力寸法がWICの期待と一致し、再符号化出力RGBのPSNR≧25 dB | これらは原ファイルとは別の**単一フレーム出力**。再圧縮は起きる。Exif・XMP・追加画像全保持を保証する試験ではない |
| 2フレームHEIFで危険な実画素縮小・回転 | `--scale-matrix` | 原コンテナのメタデータ・追加画像を証明できない場合、Encoder::Commitを失敗させ、出力ストリーム0バイトを保持 | この状態は「正常な縮小保存をサポート」ではなく、安全に拒否している状態 |
| 8件同時Commit × 50操作 | `wictest.exe --burst50` | 8件の保存操作を準備して同時にCommitさせ、合計50件すべての出力HEIFが期待全バイト一致 | 出力先は50件すべて別ファイル。ユーザーの**同一ファイル**へ50回高速クリックする実操作との完全一致ではない |

## 赤テストと修正

1. 1.1.26で実際に失敗した2フレーム人工HEICを自動再現した前例の応用として、50件同時保存を追加。
2. 最初の赤テスト[CI #40271224313](https://github.com/taku077/wic_heic/actions/runs/40271224313)で8並行Commitの複数が`0x88982F81`、画像保存失敗を再現。逐次50回だけでは検出されなかった。
3. 原因: `PhotosPureRotationBridge::TryRotation` が認証済みの2フレーム元HEIFでも共有候補キャッシュを消費し、後続の同時保存が元候補を見失う競合。
4. 修正: **私的COM契約から出自を検証できる2フレームのHEIFコンテナ**が渡された場合だけ、候補キャッシュに依存せずそのバイト列からRGBを復号して厳密に0/90/180/270度を判定し、既存の`HeicLosslessNative::Transform`で無再圧縮回転。同じ画像の未認証1フレームWICラスターは従来通りグローバル候補の一意一致とrevision安全判定を維持。
5. [CI #40271469656](https://github.com/taku077/wic_heic/actions/runs/40271469656)では50件同時Commitの結果が`WIC_PHOTOS_FIFTY_OVERLAPPING_COMMIT_BYTE_EXACT_PASS`となった。ただしCI全体は**失敗**：高並列により、従来トレースファイルの失敗即時返却式ロックがCommitEnter/Exit行の一部を取りこぼした。
6. `PhotosWicTrace.h`：CommitEnter/Exitだけログのファイルロックを**待機型**にして整合性を維持。他の大量イベントはこれまでどおり非ブロック方式。診断OFFのときはファイル書込みを行わない。
7. **Windows Actions #40271751986 SUCCESS:** 逐次50回（各HEIF期待バイト一致、50回目180°のRGB一致）、8同時×計50件（全HEIF期待バイト一致）、縮小／回転6条件と安全拒否、診断Commit始終の対応 **142組**、MSI隔離導入・削除後の登録状態復元、既存の4回連続・同一フレーム並列復号がすべて合格。これはWindows 10標準フォトGUIの高速連続クリック・同一ファイルへの保存完了を検証したものではない。
   - https://github.com/taku077/wic_heic/actions/runs/40271751986
   - 診断ログが有効な状態ではCommit境界の追跡精度を優先し、同時保存時の一部のログ書込みが待機する。診断OFF時の保存性能については旧機能と区別し、実機で評価する。


## 広く照会した技術資料

- Microsoft: [IWICBitmapSourceTransformの規定順序](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapsourcetransform) はscale→crop→rotate。補間・座標空間を違えると縦横の不一致や見切れを招く。
- Microsoft: [IWICBitmapScaler](https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapscaler-initialize) は生成後の再初期化不可。寸法と補間方式を区別する。
- Microsoft: [IWICBitmapFlipRotator](https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nn-wincodec-iwicbitmapfliprotator) はバッファなしで走査線再復号の負荷が増える。ビットマップキャッシュ利用を維持。
- Microsoft: [WICの堅牢性とスレッド安全性](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-rawguidelines-reliability-security) は多様な大きさ・操作順序・並行実行での継続負荷試験を推奨。
- pillow-heif: [pillow_heif HEIF編集とExif/XMPの扱い](https://github.com/bigcat88/pillow_heif/blob/master/docs/pillow-plugin.rst) は多画像ファイルでの主画像・各画像のメタデータ指定に注意が必要。
- libheif: [HEIFコンテナの複数画像・補助画像・Exif/XMP対応](https://github.com/strukturag/libheif) を参照。縮小と単純な`irot`変更は異なる操作。

## 次に残る、実機の重要な検証

- **高速クリックが保存完了前に積み重なる場合**、Photos側の非同期ファイル置換・読み込み済み旧画像・サムネイル更新がどう相互作用するか。複数の別ファイルへの同時WIC Commit成功だけでは解決とみなさない。
- **縮小を伴う実写真の回転編集**については、Exif/XMP/ICC/補助画像を保持した上で新しい主画像を符号化できる経路を安全に実装する必要がある。現状は多画像で安全拒否。
- 非破壊の人工画像だけを使った、サイズ変化・Exif/irot・RGB比較・表示伸長・画像見切れ・処理時間の実機テストが必要。
- 製品MSI更新は安全性の区切りが整うまで見送る。配布する場合はGitHub Actions成功、SHA-256照合の上、ChatGPTで受取可能なZIPに再梱包する。


## 2026-10-26追加：同じ作業ファイルの50回更新とWindows公式ファイル置換仕様

ユーザーの実機では同じ画像に対して短時間で50回程度の回転を実施するため、従来の別ファイル出力50回とは別のテストを追加。

- `wictest/WICPhotosReplace50Probe.h`、実行オプション `--replace50`。
- 原本 `samples/fixture-irot3-exif6-noaux.heic` は読取専用。毎回 **新規の人工試験専用フォルダ** に `synthetic-working.heic` としてコピーして開始。
- 各回は `synthetic-working.heic` の2枚フレームを読み直し、主画像をWICで90度回転して **新規の`candidate-N.heic`** へ保存。保存ファイルと元HEIFから導出した期待結果が全バイト一致することを必須とする。
- Windows `ReplaceFileW` で、保存済み `candidate-N.heic` を同じファイル名 `synthetic-working.heic` へ昇格すると同時に、前の状態を別名の **`backup-before-N.heic`** に保存。50世代すべて保持。原本は一度も置き換えず、終了時に元人工HEICの全バイト一致も確認。ファイル削除・後片付けは自動で行わない。
- 各回のWICデコーダー・エンコーダーは完全に解放してから置換し、同じ作業ファイル名で新しくデコーダーを作り、寸法・回転状態が期待どおり更新されたかを照合。
- 50回目は初期HEIFから180度回転した状態と全バイト一致を確認。
- **Windows CI #40273356433 SUCCESS**。ログ `WIC_PHOTOS_REPLACE_SAME_WORKING_COPY_FIFTY_PRESERVED_PASS operations=50 retainedBackups=50` を確認。従来の逐次50回、8件同時・計50回、縮小/回転6条件、WIC保存イベント182組の開始・終了一致、MSI隔離導入/削除/COM復元も合格。
- コードHEAD確認時: `a72b87f2c6450e7a0960ce248bfcc0eb44683898`。CI: https://github.com/taku077/wic_heic/actions/runs/40273356433

**重要な限界:** この試験はWindows CIにおける逐次 `Encode→Commit→ReplaceFileW→再オープン`。実際のWindows 10 Photosで、旧状態から開始した複数の回転操作が同じ出力先で競争する「操作を保存完了前に連打」までは再現しない。後者はWIC内のエンコード成功だけで直るとは断定できない。画像の作成日時・更新日時・サムネイルの表示タイミングについてもPhotos実機との一致は未検証。

### 追加の外部調査

- Microsoft `ReplaceFileW`：https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew 。置換時に任意のバックアップ名を指定可能。元ファイルの作成日時・アクセス権等を保持するが、**実際のPhotosがこのAPIを使用しているとは断定しない**。エラー時はファイル名と実体が複数パターンになり得るため、試験中に任意の本物写真へ適用しない。
- Microsoft `IWICBitmapEncoder::Commit`：https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapencoder-commit 。フレームCommit後にエンコーダーCommitを呼ぶ必要がある。これはアプリ側の同一ファイル名置換完了、サムネイル反映、連続UIクリックの逐次化を意味しない。
- pillow-heif `Workarounds`：https://pillow-heif.readthedocs.io/en/latest/workaround-orientation.html 。HEIFの規定表示方向は`irot`/`imir`であり、Exif Orientationとの二重適用は実装間の互換性問題を生む。以前の実機結果でExifだけ `6→1` に変化し`irot=3`が不変の例があるため、保存が成功しても要求した90度回転が表示に反映されたかは別の検査が必要。
- libavif Exif/irot/imir対応表：https://aomedia.googlesource.com/libavif/+/af93d6670e923ac8acac739b23ac1ec179418701/src/exif.c 。画素編集を伴う縮小や鏡像は`irot`のみの書換えとして扱わない。

### 次の実機重点

今後、人工HEICで「回転ボタンを連打→保存ダイアログ→サムネイルの向き変化→ファイル置換」の4時点を独立監視し、更新前のExif/irotと更新後のExif/irotと表示画素を比較する。レジストリ・MSIの更新が診断に必須とは限らないため、ファイル更新と向きの比較は独立ツールを優先。元写真原本は使用しない。
