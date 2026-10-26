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
