# Windows Shell人工HEIC画素比較：2026-10-21時点

## 範囲と重要な制限
本資料は GitHub Actions の一時的な Windows Server 2022 x64 ランナーで行った、`wic_heic` 実験用Shell COMサムネイルプロバイダーとWindows `IThumbnailCache` の検証記録である。利用するのは公開リポジトリ内の試験HEICのみ。**Windows 10実機、実写真、通常の.heic/.heif関連付け、HEIC Managerの機能は検証も変更もしていない。**

プロバイダーは実験用 `.wictrial` 拡張子に限定し、MSIには含めない。COMレジストリは使い捨てランナーの試験中だけ登録し、試験終了時に復元する。

## 検証実装
- `wictest/isolated_shell/HeicTrialThumbnail.cpp`: `IWICBitmapFrameDecode` → WIC Fant補間 → 32bpp PBGRA → Shell HBITMAP。通常は `WTSAT_RGB`、CI限定の `ForceArgb` マーカーでは `WTSAT_ARGB`。
- `wictest/isolated_shell/ShellPixelParity.cpp`: Shell `IThumbnailCache` から取得した `ISharedBitmap` をGDI `GetDIBits` で読み出し、同じ人工HEICをWIC経由で別途描画した画素バイト列と比較する。GDIの上下方向規則を踏まえる。
- `diagnostics/thumbnail_warmup/CI_IsolatedShellTrial.ps1`: 10個のコピーのSHA-256、Shellハンドラー呼び出し件数、キャッシュ再訪、画素比較、レジストリ復元を検証する。
- `.github/workflows/synthetic-shell-com-integration.yml`: RGB → ARGB → ARGB → RGB のABBA順序で4回、それぞれ新しい作業フォルダを作り、続けてARGBの128px・512px要求を検証する。

## 実証したこと
1. RGB指定は10件中6件しかWIC基準と1バイト単位で一致しなかった。残り4件はGDIで正しく読み出してもRGB値に軽微な差があり、最大のチャンネル差は5階調。
2. 同じ実験プロバイダーでARGB指定にすると、256px要求の10件すべてがWIC参照画素と完全一致した。再現性をABBA試験のARGB2回でも確認した。
3. ABBA試験（CI: https://github.com/taku077/wic_heic/actions/runs/39557327523 ）における中央値はRGBが93.59ms/74.51ms、ARGBが74.25ms/77.27ms。p90はRGBが126.37ms/129.33ms、ARGBが121.32ms/139.20ms。これらは少数・同一ランナーの参考値であり性能優位の証明ではない。
4. サイズ別CI（https://github.com/taku077/wic_heic/actions/runs/39557885440 ）では、128px要求→256pxの画像、256px要求→256px、512px要求→768pxというShellサイズ選択が観測された。**要求サイズと戻り値のサイズを混同せず、実際の返却寸法でWIC参照を生成**して比較すると、どの要求サイズでも人工HEIC10件すべてが1バイト単位で一致した。
5. Shellキャッシュへの再訪は再生成を必要とせず、試験用キーは終了後に復元された。

## まだ実証していないこと・合格扱いにしない項目
- Windows 10の実際のExplorerでの初回HEIC/JPEGサムネイル表示の比較。今回の計測はShell API試験であり、Explorer UIの表示遅延やスクロール速度ではない。
- 同じ構図・解像度・画質のJPEGとの公平な速度ベンチマーク、複数回の統計、CPU・メモリ・SSD/HDDへの負荷、Shellキャッシュの容量増。
- 実写真の10bit/HDR、ICC、EXIF、XMP、Apple MakerNote等の保存・復元。現在の画素比較は**サムネイル向け8bit PBGRAへの投影**のみであり、元ファイルの高ビット深度やHDR・メタデータの完全保全を証明しない。
- HEICの回転保存をJPEGと同等の無再圧縮・メタデータ保持で行えるかの実機確認。
- ShellのARGB指定が長時間・大量画像・実端末でも安定して最善であること。

## 次の開発ゲート
A. JPEGとHEICの同一画素由来の人工画像で、コールドキャッシュ・ウォームキャッシュの比較を取る（サイズバケットと生成時間を区別）。
B. 実機配布を一切せずに、人工HEICにICC・アルファ・10bit/HDRの差を加えたWIC出力とメタデータの検証を増やす。
C. Windows 10の実写真テストは、破壊・上書きを避ける復元手順と明示的な許可が得られるまで保留する。

**結論：隔離ShellのARGB画質完全一致は有望な実証済み成果。ただしWindows 10標準JPEG同等の実用性能は未達・未検証。**

## 2026-10-21追補：同一WIC画素源からJPEGを生成した速度比較と遅延の内訳

隔離CI専用の `wictest/isolated_shell/ShellJpegComparison.cpp` を追加した。
人工HEICのWICデコード画素を24bpp BGRに変換し、Windows WICの標準JPEGエンコーダーで `.jpg` を作成する。各ペアは同じデコード元画像に由来するが、JPEGは非可逆圧縮されるためファイル内容が同一という意味ではない。JPEG生成の時間はサムネイル表示時間に含めない。

異なる新規パスのJPEGと実験用 `.wictrial` HEICを交互にWindows `IThumbnailCache` へ渡し、初回取得と `WTS_INCACHEONLY` 再訪を計測した。HEIC側は実験用直接WIC Shellプロバイダー、JPEG側はWindows標準のJPEG処理経路なので、純粋な形式だけの差ではなく**現在の実装の処理経路比較**である。

小さい元画像ではJPEG側が100×100のサムネイルを返す一方、HEIC実験ハンドラー側が256×256へ拡大して返す。処理画素数の異なる4ペアを公正な速度比較から除外し、同じ寸法を返した6ペアのみで集計した。

- CI https://github.com/taku077/wic_heic/actions/runs/39559855974
- 1回目の初回Shell取得中央値：HEIC **132.89ms**、JPEG **8.50ms**（6ペア）。
- 2回目の初回Shell取得中央値：HEIC **132.60ms**、JPEG **8.32ms**（6ペア）。
- 同じ6ペアのキャッシュ再訪中央値はHEIC約0.62ms、JPEG約0.39～0.44ms。
- Windows Server 2022の測定であり、**Windows 10 Explorerの画面表示時間、コールドOS/SSD/HDDキャッシュ測定とは区別**する。

さらに同じ6ペアで、Shell計測をすべて終えた**後**に、独立したインプロセスWIC描画経路の各段階を測定した。

- CI https://github.com/taku077/wic_heic/actions/runs/39560343574
- 直接WICのデコーダー生成、初期化・フレーム取得、Scaler/Converter初期化は、いずれもHEICで概ね0.4ms以内。
- 32bpp PBGRAへの縮小付き `IWICFormatConverter::CopyPixels` 中央値：HEIC **122.20ms／122.23ms**、JPEG **2.70ms／2.89ms**。
- `CopyPixels` は遅延の支配的な段階であり、**この段階にはHEVCの実画像デコード・画素変換・縮小の実処理が含まれる**。これだけでは内部のHEVCデコード単独時間とは断定できない。
- HEICのWICデコーダーを明示的に選択する起動高速化だけでは、この `CopyPixels` 遅延は解消されない。

`wic_heic/HeicBitmapFrameDecode.cpp` の現行コードを点検した結果、初回 `CopyPixels` → `DecodeImage()` → `m_Handle.decode_image(heif_colorspace_RGB,m_chroma)` で実画像が遅延デコードされる構造を確認した。現在のShell試作ハンドラーは `GetFrame` → WIC Fant scalerを用いる。WIC側には `GetThumbnail` メソッドが実装されているが、埋め込みサムネイルがある場合に活用して高速化できるか、向き・HDR/ICC・画質の差が許容できるかは未試験。

**次の優先順位:** 
1. 公開人工HEICで埋め込みサムネイルの有無、解像度、デコード時間、主画像との向き・色差を検査する。
2. 埋め込みサムネイルが必要サイズを満たすときだけ試験的に選択し、実行時間と画素／ICC／EXIFの同一性をチェックする。差がある場合、画素完全一致とは評価しない。
3. フルHEVCデコードと縮小／色変換の時間内訳を、人工画像・隔離CI・CPU負荷制限付きで分解する。マルチスレッドを無条件に増やさない。
