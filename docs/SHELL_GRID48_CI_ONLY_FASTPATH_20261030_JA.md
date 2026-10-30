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
- **2026-10-31更新：grid payloadの行列・サイズ検証を実装済み。** 詳細は末尾の「第21節相当」追記。現時点のゲートは **実際の6行×8列、4032×3024のgrid header＋各504×504タイル／320×240のサムネイルispe** を要求し、未対応の`iloc`構成や不正な範囲は拒否。**ただし各HEVCタイルの符号化画素とサムネイルの全内容が一致すること、ICC/HDR/Exif等の複合iPhone写真の高速化は保証しない。**
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

## 2026-10-31：grid本体と50itemの寸法を検証するゲートを追加（CI検証成功）

- **コードコミット**：`9df1b8011fd7d6119c0c8a56f3e2ee0e34975f6a`、安全性変異試験 `230efc7b8a46be30af292aebd68c78b7b87b0475`。
- **最終CI**： https://github.com/taku077/wic_heic/actions/runs/41078596089 **SUCCESS**。実行ログの `CI_GRID48_STRICT_METADATA_GATE_PASS`、`CI_GRID48_GRID_PAYLOAD_GEOMETRY_MUTATIONS_REJECTED_PASS`、`CI_GRID48_CI_ONLY_DIRECT_PROVIDER_EMBEDDED_PASS`、`CI_GRID48_SHADOW_WIC_TRACE_COUNTS_PASS full_primary=1 embedded=2 pilot=1`、`CI_GRID48_RESEARCH_REAL_ICC_IROT_ALPHA_PQ_HLG_REJECTION_PASS`、`SHELL_REGISTERED_GRID48_FULL_WIC_FALLBACK_PASS`、`SHELL_REGISTERED_LAB_REGISTRY_RESTORED_PASS`を確認。
- **新規実装**：`wictest/WICShellOrientationGate.h` に`ResearchGridPayloadRange`／`ResearchGrid48PayloadMatches` を追加。`meta/iloc` でgridのprimary itemを見つけ、`construction_method=1`の1つのextentが`meta/idat`のどのバイト範囲を参照するかを境界検証付きで算出。ilocバージョン1/2、offset/length 4/8 byte、base 0/4/8 byteの範囲に厳格限定し、未対応のデータ参照方式・index・複数extentは許可しない。
- **実grid payloadの8/12 byte**：バージョン0、予約flagsは0/1のみ、`rows_minus_one=5`、`columns_minus_one=7`、幅4032、高さ3024を明示検証。HEIFの`grid`項目は参照件数だけでは配置・出力寸法を証明できないため、位置を任意のFourCC文字列検索で推測せず、正規iloc/idatから確定する。
- **各itemの画像寸法**：主gridの`ispe`は4032×3024、48枚のhvc1 tileは504×504、埋込thumbnailは320×240だけを許可。これにより端数タイル・部分クロップ・縦横反転・異なるタイル寸法を現研究ゲートではすべて不適格とする。
- **CIの不正変異（人工データをメモリ上だけで変更）**：grid payloadのバージョン、flags、行数、列数、幅を改変して全て拒否。tile`ispe`の504幅を503に改変して拒否。生のユーザーHEICを変更しない。
- **全体の結果**：合法な人工48タイル＋埋込サムネイルは候補として受理し、研究用`IThumbnailProvider`で主画像を余分にフル復号せず256px表示。色・回転・ICC・HDR・アルファに対する従来の安全側フォールバックを維持。CIのRGBサンプル画質試験（MAE0.038/255）も継続成功。
- **適用範囲の厳密な限界**：これは意図的に狭い **人工8bit SDR／48タイル固定のCI専用実験**。タイルの符号化画素の整合性、外部からの悪意ある全てのBMFF構成、実際のiPhone写真の適格割合、Windows10のCPU使用率低下、Photos回転保存動作は未実証。本番MSIの配布・Shell登録の変更はしない。
- **参考した別系統の実装と文献**：libheif v1.23.7 `image-items/grid.cc` のヘッダー`rows/columns/output_size`解析、`box.cc` の`Box_iloc::parse`と`construction_method=1`、Nokia HEIFの`itemlocationbox.hpp`、libheif公式tiled image説明。https://github.com/strukturag/libheif/wiki/Reading-and-Writing-Tiled-Images

**次は**人工写真相当の高周波・グラデーション画質差の調査と、本経路が実際のiPhone系gridで何割利用可能か（複合カラー／Exif／HDRの頻度）の調査。実利が乏しければ、過度に一般化せずPhotos／Explorer側の別のフル復号削減策へ切り替える。
