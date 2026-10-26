# 2026-10-26 HEIC/WIC開発: 複数サイト横断調査と開発優先順位

**開発対象:** Windows 10 Explorer / Windows 10標準Photos向け自作WICのみ。
**実機基準版:** ユーザーがインストールして2フレームの人工HEICを回転保存できたWicHeic 1.1.27。
**重要:** このブランチの追加コードはCI確認中であり、配布済み1.1.27 MSIへ自動反映されない。CI成功≠Windows 10 GUI実機成功。
**安全条件:** IMG_4074、ユーザー原写真、HEIC Manager、既存関連付け/キャッシュに手を触れない。人工HEICだけ。元ファイルの上書き・自動削除はしない。MSIは改善がまとまったときだけ。

## 1. 検索対象と参考サイト・得られた論点

| 提供元 | 参考資料 | 今回の技術的示唆 |
|---|---|---|
| Microsoft WIC | https://learn.microsoft.com/en-us/windows/desktop/api/wincodec/nn-wincodec-iwicbitmapfliprotator | FlipRotatorは画素単位の要求により走査線デコーダーにn²型の負荷を生むことがあり、IWICBitmap等によるバッファリングを推奨。既存WICはDecodeImageでRGB平面をメモリキャッシュするため、同じ画像を行ごとに復号する設計ではない。一方、同じCOMフレームへの同時初回アクセスの同期を調べる価値がある。 |
| Microsoft WIC | https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapsourcetransform | scale→crop→rotateが正規順序。これとWIC SetSize/CopyPixelsの寸法・strideの整合を検証する。 |
| Microsoft WIC | https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframeencode-writesource | SetSize/WriteSourceの寸法はWIC仕様で条件があり、現在の安全側rejectは「予期せぬ引き伸ばしを防ぐ」ための意図的な制限。一般のWIC要件とPhotos実パスを比較する。 |
| pillow-heif | https://pillow-heif.readthedocs.io/en/latest/workaround-orientation.html | HEIFではExif向きは原則記述情報で、表示はirot/imirに従う。古いビューアーにはExifと二重適用の事例あり。メタデータの正規化と「目視で90°回ったこと」を混同してはいけない。 |
| libavif | https://aomedia.googlesource.com/libavif/+/06799082864459d6bb31ae3612dc753035c61ce1/src/exif.c | Exif方向とHEIF irot/imirの変換表・回転符号・鏡像変換を参照。画素回転を前提に安易にExifのみ書き換えない。 |
| ImageMagick | https://github.com/ImageMagick/ImageMagick/issues/1232 | HEICデコーダーが保持するExif向きと実画素とのズレでauto-orientが二重補正する問題。 |
| ImageMagick | https://github.com/ImageMagick/ImageMagick/issues/5647 | HEIC再生成時irotが失われてExifだけ保持され、結果が横向きになる報告。 |
| ImageMagick | https://imagemagick.org/command-line-options/ | \`-orient\` は画素を回転せずメタデータを設定。回転保存成功と画素/表示方向の一致は別の確認が必要。 |
| PhotoPrism | https://docs.photoprism.app/developer-guide/metadata/orientation/ | HEIF/HEICのコンテナレベルirot/imirとExif orientationを区別し、実際のデコーダーに合わせるべき。 |
| XnView | https://newsgroup.xnview.com/viewtopic.php?t=50119 | 2026年にもHEIFのirot/imirとExifの混在表示が報告・再現されている。Photos固有問題と断定しない。 |
| libheif | https://github.com/strukturag/libheif/security/advisories/GHSA-8fmq-r4pf-7m57 | 新しいlibheifで悪意ある補助画像参照の循環によるハングが報告。既存プロジェクトはピン留めした古い依存であるため直接の脆弱性該当とは断定せず、形式検証と依存の見直しに反映する。 |

## 2. 既存コードとの照合（重複実装禁止）

- \`wic_heic/HeicBitmapFrameDecode.cpp::CopyPixels\` は既にWICRect/stride/bufferを検証し、\`DecodeImage()\`後のRGB面サイズ一致・行コピーを行う。**拡大縮小/回転を同時に追加するのではなく既存実装を再利用**。
- 従来\`DecodeImage\`は\`m_Decoded\`が非同期保護されておらず、同一フレームの並列\`CopyPixels\`時に複数の\`m_Image\`書換えやRGBポインタの寿命競合が起こり得た。今回\`m_DecodeMutex\`で初回復号+公開だけを排他し、既に復号済みの場合は通常の行コピーを並列に継続する。
- \`wictest/WICDecoderConcurrentProbe.h\` は1つの人工HEICフレームにCOM MTA内4ワーカーが同時に初回\`CopyPixels\`を要求し、RGB全バイトの一致と、後続の矩形+パディング保全を検証する。Photos GUIでの再現成功を意味しない。
- \`wic_heic/HeicLosslessNative.h\` は主HEIF irot更新・Exif orientation正規化・関連画像共有回転・全コンテナの安全判定を既に提供する。別方式の新しい回転実装を重複追加しない。
- \`wic_heic/PhotosPureRotationBridge.h\` は短時間最大4候補の圧縮HEIFと全RGBの0/90/180/270厳密一致を検査。候補再復号コストは保存時の遅延に寄与し得るが、現時点の実機測定がなく原因断定しない。バッファリング・候補数調整は測定後。
- 1.1.27実機成功例の保存後HEICで **Exif Orientation 6→1のみ** が変わり、HEIF\`irot=3\`に変化がなかった記録がある。ユーザーは保存UI成功を確認したが、バイナリ上での90°回転の適用とは区別する。実機で表示方向が所望どおりに変わったかは、次の人工試験で検証が必要。

## 3. 今後の優先順位

1. 今回の並行初回復号のrace修正とMTA同一COMフレーム試験がWindows CIで成功すること。
2. 1.1.27の人工2フレーム保存・色プロファイル/EXIF/全HEIF保持の既存回帰が引き続きPASSであること。
3. **同一人工HEICの連続90°×4回**について、HEVC再圧縮の有無、元へ戻った表示画素、各保存のWIC/ファイル置換/サムネイル更新タイミングを分けた回帰試験を強化する。
4. WICのLogを常時有効化して重くするのではなく、独立診断ツールを優先。内部原因調査時だけオプトイントレースを利用する。
5. HDR/多重画像・ICC/Exif/XMPは全項目を保持できる場合のロスレスを優先。編集ラスタの再圧縮やメタ情報損失は未解決のまま分類。
6. 新MSIは区切りの良い結果かつ必要性が生じるまで発行しない。1.1.27を実機基準として維持し、旧版復旧手段を保持。

