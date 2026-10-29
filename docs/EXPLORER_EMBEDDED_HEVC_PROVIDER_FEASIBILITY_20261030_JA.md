# Explorer HEIC CPU削減：Shellサムネイル経路の独立実験（2026-10-30）

## 実機上の事実（これまでの測定を再利用）
- Windows 10 1.1.30の既存ETL解析で、負荷の大きなCOM Surrogate PID 2712におけるCPU命令ポインタサンプル153,543件中、113,221件（73.739%）が自作 `wic_heic.dll` のアドレス領域にあった。
- 同じPIDで `DecoderGetFrame=141`、`DecoderFullDecode=141`。埋込サムネイル取得・不在のイベントは0件。全ての画像が実際に埋込サムネイルを持っていたかはまだ未確認。
- `wic_heic.dll` はlibheif/libde265等を静的リンクしているため、DLL内部の負荷をWICラッパーとHEVC復号ライブラリに分割するには同じビルドのPDB/マップが必要。

## CIで新たに再現できたこと
- 人工4032x3024 HEVC（320x240の埋込サムネイルあり）は、自作WICの `IWICBitmapDecoder::GetThumbnail` と `IWICBitmapFrameDecode::GetThumbnail` から正常に取得できた。主画像を復号していない。
- Windows Shell `IThumbnailCache::GetThumbnail` の `WTS_FORCEEXTRACTION` を用い、同じ人工HEICを256x192として取り出すことにも成功した。
- そのShell呼び出しでは**埋込GetThumbnailイベント0、フル12MP復号1、DecoderOpen1**を確認。既存WICだけを修正してもShellがそのメソッドを呼ばなければ利益がない。比較時の抽出時間は215ms（人工HEICの単一測定でありユーザー実機の速度ではない）。
- Actions #40898893355 SUCCESS: https://github.com/taku077/wic_heic/actions/runs/40898893355

## 独立試作の目的
`IThumbnailProvider` と `IInitializeWithStream` を使い、Shell専用に埋込HEVCを読む経路を実装する。まずテストプログラム内でこのインターフェイスを直接起動してHEVC埋込サムネイルをWICから読み取り、GDIのHBITMAPへ変換する。**この段階では、MSIに登録しない。**

- 埋込サムネイルが使える・要求が256px等で十分な大きさの場合、軽量HEVCのみ復号して返す。
- 埋込がない場合、未対応の画像、複合HDR/ICC/向き不整合などでは**安全なフォールバック**が必要。未検証の埋込画像を全て強制使用しない。
- 1024pxの要求に対して320pxを大きく拡大して返すのは画質劣化。必要に応じて元画像の正規復号経路へ戻す。
- サムネイル色やEXIF/irotの扱いは主画像と一致させる必要がある。特に高ビット深度/HDR/gain-map/グリッド画像が対象。
- Shellハンドラーを後から登録する場合、WindowsのCOM隔離を無効にしない。 `IInitializeWithStream` を使用し、レジストリ既存値の保護・復元を担保する。
- Shellデコーダーでは既存WICを再利用し、Windows 10フォトの無再圧縮回転・保存経路に影響させない。

## 次のゲート（ユーザーへ再診断を依頼しない）
1. CIだけの `IThumbnailProvider` 直接呼び出しで色・画素内容・寸法・フル12MP非復号を確認。
2. disposable Windows runner上でのみShellへ登録し、実際の `IThumbnailCache` から**そのハンドラーが呼ばれること**を検証。失敗時の登録復元を最優先。
3. 256/1024・横/縦回転・8/10/12bit・alpha・grid・gain map・ICC・埋込なしで正しさとフォールバックを確認。
4. フルサイズ復号回数、CPUサンプル、ファイル単位の所要時間を同条件で比較する。根拠のないMSI配布はしない。

## 一次・横断資料
- Microsoft Shell IThumbnailProvider: https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nn-thumbcache-ithumbnailprovider
- Microsoft IThumbnailCache: https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nf-thumbcache-ithumbnailcache-getthumbnail
- Microsoft Building Thumbnail Handlers: https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers
- Microsoft WIC SetThumbnail: https://learn.microsoft.com/en-us/windows/desktop/wic/-wic-imp-iwicbitmapframeencode
- libvips embedded HEIF thumbnail implementation: https://github.com/libvips/libvips/blob/master/libvips/foreign/heifload.c
- libvips source-selected size gate: https://github.com/libvips/libvips/blob/master/libvips/resample/thumbnail.c
- libheif: https://github.com/strukturag/libheif

## 開発方針
目標はCPU使用率だけでなく、Explorerの初回サムネイル時間・フォト操作感を含めた改善。性能改善が実機で認められない場合は実験を製品版に取り込まない。実機で問題が残る既知のロスレス回転/サムネイル消失バグを、性能改良によって悪化させない。


## 2026-10-30追加検証：登録済みShell経路の安全なフォールバック

**実装コミット**：[af901cf0](https://github.com/taku077/wic_heic/commit/af901cf0c424ea48ea2beae5b0c7f6e3708d67b7)。
**Windows Server 2022 CI**：[ #40931425282 ](https://github.com/taku077/wic_heic/actions/runs/40931425282) SUCCESS。

既存の直接COMテストを繰り返す代わりに、実際の IThumbnailCache::GetThumbnail とテスト登録済み IThumbnailProvider を通す回帰ゲートを増設した。

- **埋込サムネイルなし**：通常WICへのフォールバックをShell呼出ログで確認（SHELL_REGISTERED_NO_EMBEDDED_FULL_WIC_FALLBACK_PASS）。
- **実在する有効なHEIC試験素材（irot=3 + Exif Orientation=6）**：補助画像あり・なしの2種類で、埋込経路を通らず通常WICへのフォールバックに成功。
- **ICC付き人工HEIC**：従来の登録済みShell経路試験でも主画像フォールバックを維持。
- 上記追加試験は、試験用コピーを作って実行し、抽出の前後でコピーの **SHA-256が不変**であることを検査した。
- シンプルな人工12MP HEICで、Shellハンドラーが呼ばれ埋込320×240を取得、主画像の12MP復号は0件。8ファイルずつの強制再抽出の中央値は従来 **103 ms**、試作経路 **12 ms**（同一CIジョブ、約88.3%短縮）。この数値は**Windows 10実機や1,894枚のフォルダ全体を表さない**。
- 事前登録を尊重し、CIの一時HKCU/HKLMテスト登録を復元する検査もPASS。**本番MSIのShellEx登録は未変更**。

**残る検証ゲート**：通常フォールバック後のExif/irot画像について、Windows標準の表示向き・色・画素が基準と一致するかを直接比較すること。GetThumbnail成功は方向・色の一致の証明ではない。8/10/12bit、alpha、grid、HDR/gain map、ICCおよび既存他社ハンドラーを含む動作の保証はまだない。実機で埋込thmbを持たない場合は今回の高速化が効かない可能性が高い。1.1.31 MSIは配布しない。

**参照・横断資料**：
- [Microsoft IThumbnailProvider / IInitializeWithStream](https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nn-thumbcache-ithumbnailprovider)
- [Microsoft Shell registration](https://learn.microsoft.com/en-us/windows/win32/shell/reg-shell-exts)
- [libvips HEIF thumbnail](https://www.libvips.org/API/current/ctor.Image.heifload.html)
- [libheif v1.23.7 release/security changes](https://github.com/strukturag/libheif/releases/tag/v1.23.7)
