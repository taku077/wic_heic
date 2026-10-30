# Shell HEIC高速化：64KiB安全事前判定と正規48タイルgrid回帰試験（2026-10-30）

## 結論

**既存のShell埋込サムネイル試作が利用する判定を、複雑なHEICで軽量化。安全性CI合格。ただしWindows10実機に配布していない。**

CI: https://github.com/taku077/wic_heic/actions/runs/41070865266 （64KiB事前判定）／ https://github.com/taku077/wic_heic/actions/runs/41071529559 （正規48タイルgrid・登録済Shell・画質・復元）ともに **SUCCESS**。

- 従来の `WicShellLabSafety::FromStream` は先に最大24MiBの全バイトをメモリに確保／読み取ってから、grid・補助画像・色・Exifの非適合性を判定していた。
- 新版は**最初の64KiBだけ確保・読込み**、この範囲に `grid`／`auxl`／`dimg`／`tmap` という複合構造のFourCCが存在する場合、**安全な通常WICへ戻すため即座に拒否**。それ以外は従来の厳密な全BMFF解析を維持。文字列ヒットは不適合の**否定的ヒントのみ**として使用し、適合判定には絶対使わない。誤検出しても高速経路から通常WICへ戻るだけで画像は変更しない。
- 旧コードで最初に全ファイル容量分の `Bytes` を確保する問題も修正。複合構造の早期除外では不要なメモリ確保・メディアデータ読込みを回避。
- **過去に提供された1枚のiPhone系HEICを画像原本非変更・解像度復号なしの先頭メタデータのみ検査**。ファイルは1,721,813 bytes、`grid`最初の1173バイト、`dimg`1550、`auxl`1708、`prof`1770を確認。今回の64KiB検査ではこの例で全ファイル読込みを回避可能で、**事前判定読込み／確保量は設計上96.19%減**。実Win10でのCPU効果を測定した数値ではない。個人写真・ファイルパス・EXIF・画像データはGitHub未公開。
- CI人工メモリ内の異常gridマーカー例（131,072 bytes）では `CI_SHELL_COMPLEX_EARLY_REJECT_PASS bytes_read=65536 file_bytes=131072 artificial=1`。**これは無効な合成ビットストリームであり、有効な48tile HEIFではない**ので区別する。
- 別の生成器で、**合法的な8×6=48タイル（4032×3024、504×504 tile）＋埋込320×240 HEVC thumbnail**の人工HEICも実際に生成。CI `CI_VALID_GRID_48_TILES_EMBEDDED_THUMB_CREATED_PASS` を確認。以前のFourCC書換えだけの疑似gridテストとは別。
- この**正規grid人工HEIC**を登録済み隔離Shell `IThumbnailProvider`で抽出し、fast path拒否→WICフル画像フォールバック、画質同一（64×64サンプル RGB MAE 0.00/255、20階調超の差分0%）、出力256×192を確認。CI `SHELL_REGISTERED_GRID48_FULL_WIC_FALLBACK_PASS`、`SHELL_PARITY_GRID48_GEOMETRY_RGB_PASS`。
- 同一CIの単純人工HEIC（gridではない）では、登録済みShell比較が既定93.50ms、埋込サムネイル経路12.00ms（各8画像の中央値、比0.128）。**この速度改善は正規gridのものではない**。gridは依然としてフルWIC復号へ戻る。
- 既存のICC・Exif/irot・alpha・PQ/HLG・埋込なしに対する安全な通常WICフォールバック、RGB差分／向き／Shell登録復元ゲートも継続PASS。
- CIの初期版では「正規alpha 4032×3024が必ず64KiBを超え、auxlを先頭に含む」とする不適切な期待値で失敗。実生成α素材は **37,087 bytes** であったため、全読込して当然。後で合成131,072Bのgridマーカー入りストリームで「読込み64KiB」の検証に変更し、正規alphaの安全なフォールバックは独立して検証した。**この失敗を繰り返さない。**

## いま進めるべき次の変更

- 実機のCPU100%に効かせるには、正規iPhone系の**grid+ICC/HDR/gain map＋Exif**の埋込thmbを、色・向き・品質を維持してフル復号せず出せるかという安全性が次の本質的なゲート。今回の正常grid+thmb人工素材でもまだ高速経路を許可していない。
- 次の研究では正規grid人工素材を起点に、必要なカラープロファイル／補助画像／回転の組合せを人工生成し、**既存フルWIC出力と同一画面における比較**と複雑画像の適格率を証拠に採否判断する。形だけの早期解除は禁止。
- 本番WIC DLLやMSIへのShell登録はまだ行わない。CopyTrans等の第三者ハンドラーを保護し、アンインストール／元登録復旧・Photos回転保存品質を確認できるまで実機試験候補としない。
- CPU100%問題は未解決。Photos側ワーカー数の既研究を繰り返さない。

## 横断参考

- libheif 48タイルgridの公式API、タイル形式一貫性要件: https://github.com/strukturag/libheif/wiki/Reading-and-Writing-Tiled-Images
- libheif v1.23.7 `heif_tiling.h`: https://github.com/strukturag/libheif/blob/v1.23.7/libheif/api/libheif/heif_tiling.h
- libvipsのthumbnail選択（小さい埋込ソースのみ利用）: https://github.com/libvips/libvips/blob/master/libvips/resample/thumbnail.c
- Microsoft Shell `IThumbnailProvider` と `IInitializeWithStream`: https://learn.microsoft.com/ja-jp/windows/win32/shell/building-thumbnail-providers
- Microsoft WICフル画像復号のコスト: https://learn.microsoft.com/en-us/windows/desktop/wic/-wic-imp-iwicbitmapframeencode

**本資料は合格したGitHub実装・CIを記録する目的のものであり、個人画像やETLを含まない。**
