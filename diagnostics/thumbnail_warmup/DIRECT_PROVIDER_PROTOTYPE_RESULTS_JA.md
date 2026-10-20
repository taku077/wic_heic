# IThumbnailProvider 直接指定型HEICサムネイルの試験結果

最終目標：Windows 10標準のExplorer・フォトでHEICをJPEGと同等に扱う。高速化によって画像情報・10bit・ICC/EXIF/XMP・無再圧縮回転が破損しないこと。

## 変更内容

- `wictest/DirectThumbnailProviderProbe.h` に `IThumbnailProvider` と `IInitializeWithStream` の**テスト用COM実装**を追加。
- WICの`CreateDecoder(GUID_ContainerFormatHEIC, GUID_VendorPrsyahmi)`を使い、既知のHEICデコーダーを直接選択する。
- 受け取ったIStreamを読み取り専用として扱い、256pxの縦横比を保持して32bppPBGRAのトップダウンHBITMAPを生成する。
- COM初期化の二重呼び出し拒否、生成HBITMAPの読み戻し、アルファフラグと同サイズWIC自動選択生成画像との**画素完全一致**を検査する。
- 試験オブジェクトは**Shellに登録しない**。OSのレジストリ、関連付け、既存ハンドラー、HEIC Managerには変更なし。

## 実測（2026-10-21 / GitHub Actions windows-2022）

各モード10回ずつ、5個の試験環境反復の中央値の範囲。

| 項目 | 従来のWIC自動選択 | 試作IThumbnailProvider直接指定 |
|---|---:|---:|
| 256pxサムネイル完了 | 約214～266ms | 約81～124ms |

- 人工HEIC、画素値完全一致 `pixels_equal=1`、アルファ判定 `alpha_valid=1`、Windows自動試験は成功。
- 比は同じ反復内で約2.1～2.6倍。ただし、WIC/Windows 10 Explorerの実描画速度とは異なる。
- 先行するWIC直接初期化試験でも自動経路約135～140ms、直接経路0.5ms前後。両経路のデコーダーCLSIDは10/10件で一致。
- 小容量HEICのlibheifメモリ入力A/B試験はストリーム読み込みと同等時間で、追加メモリ負荷に見合う効果なしとして棄却済み。

## 制約・今後の検査

1. 現段階のテストは同一プロセス内の擬似Shell呼び出し。Explorerが`IThumbnailProvider`を孤立プロセスで実際に呼ぶ試験は**未実施**。
2. 実際のWindows 10で既存のPhotoThumbnailProviderやHEIC Managerと登録が競合しないことを、導入・復旧できる検証環境で確認する必要あり。現時点ではShell登録・MSI配布しない。
3. 実写真の8bit/10bit、アルファ、画像向き、ICC、HDR、埋め込みサムネイル、キャッシュ永続性、大量画像のメモリ/GDI使用量は、今回の1種類の人工HEICだけでは未確認。
4. 初回未キャッシュ表示時間とJPEG実機対照はこれから。負荷を制限し、効果のないマルチスレッドや常駐サービスは導入しない。
5. JPEG同等の全機能（10bit保存、EXIF/XMP/ICC完全保持、無再圧縮回転）については引き続き別の回帰試験で判定する。

Microsoft公式資料: https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nn-thumbcache-ithumbnailprovider
Microsoft公式資料: https://learn.microsoft.com/en-us/windows/win32/shell/building-thumbnail-providers