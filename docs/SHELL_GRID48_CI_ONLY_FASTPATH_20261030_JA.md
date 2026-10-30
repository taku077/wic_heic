# 48タイル HEIC のCI限定高速経路：実装結果と安全性ゲート（2026-10-30）

## 結論

**初の正規48タイルgrid HEIC高速経路を研究用の`IThumbnailProvider`へ接続し、人工画像の256pxサムネイルを主画像の追加フル復号なしで取得した。Windows Server 2022での隔離CI成功。ユーザーWindows 10向けMSI/DLLには未統合であり、CPU100%問題は未解決。**

- 実装ブランチ：`experiment/shell-com-focused-20261030`。
- **最初の高速経路CI成功**： https://github.com/taku077/wic_heic/actions/runs/41077017350 （SUCCESS）。
- メタデータ判定：`wictest/WICShellOrientationGate.h` の `ResearchGrid48PreviewSafe` と `ResearchGrid48FromStream`。
- CI限定プロバイダー：`wictest/WICShellThumbnailProviderPilot.h` の `Provider::GetThumbnail`、明示的に`GITHUB_ACTIONS=true` **かつ** `WIC_HEIC_GRID48_RESEARCH=1`、要求サイズ`<=256`の場合のみ。既定では従来の全WIC経路で、公開MSIには含めない。
- 合成対象：libheif v1.23.7で作った主4032×3024、8×6の48タイル各504×504、別HEVCで作った320×240サムネイル、8bit SDR、ICC/Exif/alpha/PQ/HLG無しの単純人工図形のみ。
- **CIで観測した復号件数**：`CI_GRID48_SHADOW_WIC_TRACE_COUNTS_PASS full_primary=1 embedded=2 pilot=1`。主画像フル復号1回は**画質比較の基準画像用**。二度の埋込サムネイル復号のうち一回が**CI限定プロバイダーでの表示用**で、その際の追加フル主画像復号は0。実行セッションのtrace範囲を分離しなければこの主張は成り立たない。
- 画質比較：`CI_GRID48_DIRECT_PROVIDER_RGB_MAE=0.038`、`CI_GRID48_EMBEDDED_VS_MASTER_SHADOW rgb_mae_64=0.038 pixels_gt20_percent_64=0.000`。64×64のサンプルRGB平均誤差、0～255階調。全画素比較・自然画像の高周波成分・HDRの忠実性を保証するものではない。
- 正規メタデータの許可ゲートと拒否テスト：`CI_GRID48_STRICT_METADATA_GATE_PASS`、`CI_GRID48_NEGATIVE_RELATION_BITDEPTH_ROTATION_COLOR_PASS`。参照を書き換えた`auxl`/`tmap`、10bit hvcC、`irot`等をメモリ内だけで模擬し、誤受理を防ぐ。
- **登録済みShellでのgridは依然安全な通常WICフォールバック**。研究用のenv flagは通常のユーザー／CIの隔離Shellには付与せず、従来の`SHELL_REGISTERED_GRID48_FULL_WIC_FALLBACK_PASS`を維持する。ドライバー・登録・原本・ファイル関連付けは変更しない。

## 厳格な候補ゲートの具体条件

- `iinf`で正確に50 item（1 grid + 48 hvc1 tile + 1 hvc1 thumbnail）、各ID重複なし。
- `iref`は主grid→48tileの`dimg`参照が一つ、縮小画像→主gridの`thmb`参照が一つだけ。補助画像やHDR/Exif関係は未知として拒否。
- `iprp/ipma`は50itemの一意なproperties対応。すべてのhvc1に8bit `hvcC`、`ispe`、存在する`pixi`も8bit。利用するpropertyはispe/hvcC/pixi/colrだけ。回転やトリミングの`irot/imir/clap/pasp`等を拒否。
- `colr`が存在する場合は、ICC（prof/rICC）を拒否し、標準SDR寄りの限られたnclx値だけを許可する。未知のitem type、XMP、Exif、auxl、tmap等は拒否する。
- **限定的なゲートの欠点**：現時点では`grid` item payloadから実際の行列数とタイル座標を直接検証していない。**このため許可条件は「8×6を必ず証明」ではなく「有効な48タイルへの参照を確認」**が正確。実装はCI限定で、本番／実機へ無条件展開しない。埋込サムネイルと主画像の内容一致を元ファイルのメタデータだけから暗号学的に保証できるわけでもない。
- 色・向き・HDR・gain map・Exif・アルファが実際に入るHEICを、これまでの個別回帰CIで安全側にフォールバックさせている。追加の真性素材拒否試験も同ブランチに実装。**実HEIC拒否を明示した最終コードCI： [Actions #41077558646](https://github.com/taku077/wic_heic/actions/runs/41077558646) SUCCESS。ログに `CI_GRID48_RESEARCH_REAL_ICC_IROT_ALPHA_PQ_HLG_REJECTION_PASS`、`CI_GRID48_CI_ONLY_DIRECT_PROVIDER_EMBEDDED_PASS`、`CI_GRID48_SHADOW_WIC_TRACE_COUNTS_PASS full_primary=1 embedded=2 pilot=1`、`SHELL_REGISTERED_GRID48_FULL_WIC_FALLBACK_PASS` と `SHELL_REGISTERED_LAB_REGISTRY_RESTORED_PASS` を確認。**

## 前回からの進捗と再実施不要の研究

前回までの`GetThumbnail`単独の高速化、単純HEICの登録済みShell 8件中央値96ms→13ms、48タイルgridの影画質比較MAE0.038、64KiB早期拒否・ロールバックは既に検証済み。**今回初めて、複合構造であるgridを研究用プロバイダーに合格・表示させ、フル主画像復号回避をログで実証した。** ただし本番ユーザー画像へ適用していない。

## 追加で必要な研究（効果がある場合のみ実機へ）

1. 48タイルのgrid item payload（行数列数・基礎画像寸法・非矩形／端数タイル）をlibheifの定義に基づき明示的にパースし、metadata偽装を拒否する。
2. 自然画像に近い様々な人工図形・グラデーション・高周波パターンで画質比較を多数回実施。256px以下、元のカラー管理/方向との一致を確認する。
3. ICC/Exif/irot/gain map/PQ/HLGを含む有効な複合grid素材の誤受理がないことを確認。広い適格率を示せない場合は、無理に一般iPhone HEICへ高速経路を拡大しない。
4. 第三者ShellExハンドラーの保護、インストール／アンインストール復旧、ユーザーWindows10でのCPU100%・表示品質・操作感の検証を満たした場合のみMSI候補にする。実機テストが必要なら過去ツールや診断素材は当方が添付。古いETL/ZIPをユーザーが長期保管しているとは想定しない。

## 技術資料（一次情報中心）

- libheifの正規grid生成・タイル追加： https://github.com/strukturag/libheif/wiki/Reading-and-Writing-Tiled-Images
- libheif実装での縮小画像から親への`thmb`参照： https://github.com/strukturag/libheif/blob/v1.23.7/libheif/context.cc
- libvipsのHEIF色・方向処理： https://github.com/libvips/libvips/blob/master/libvips/foreign/heifload.c
- Microsoftの隔離Shellハンドラー： https://learn.microsoft.com/ja-jp/windows/win32/shell/building-thumbnail-providers

**生のiPhone写真・個人メタデータ・ETL・パスはGitHubへ公開していない。**
