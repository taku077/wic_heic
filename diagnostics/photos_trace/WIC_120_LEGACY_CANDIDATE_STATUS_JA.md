# Windows 10 標準フォト・旧形式WIC v1.1.20候補の実装／試験状態

更新日: 2026-10-23

## ゴール
Windows 10 Pro 22H2 build 19045、Microsoft Photos 2026.11090.22001.0、HEIF拡張あり、**有料HEVC拡張なし**で、HEICをJPEGのように標準フォトで開き、ズーム・画像送り・回転・編集保存する。HEIC Manager／独立したサムネイルv0.4.0は変更しない。実写真IMG_4074/IMG_3915は再試験に使わない。

## 旧WICルートを選ぶ理由
- 旧WicHeic Repair Test v1.1.18 (DLL SHA-256 `9E9D2F17038C03A5BF8840A8BD43BC4FDA19C91E947C4CD5904A8209051F93D4`) は、2026-10-20に一時回転保存に成功、2026-10-22には再導入でHEVC警告・編集ボタン無効が一時解消。保存時のEXIF消失・再圧縮、編集・回転エラーは未解決だった。
- Microsoftネイティブ `GUID_ContainerFormatHeif` 採用版は明示的ベンダーならWIC API合格。しかし**ベンダー未指定のCreateEncoder()** はWindows Server 2022上で Microsoftネイティブエンコーダーを選択後 `WINCODEC_ERR_COMPONENTINITIALIZEFAILURE=0x88982F8B`。独自エンコーダー自身のCOM生成は成功。これを実機Photosで解決したとは扱わない。
- ネイティブエンコーダーのHKLM優先度書き換えはアクセス拒否、HKCUの優先度オーバーレイでは失敗が改善せず、実験後削除。ユーザーPCにコーデックの優先順位変更を導入しない。
- 現候補は2026-10-22の実機フォトでUI成功実績のある**旧プロジェクト専用HEICコンテナーGUID** `98ce31dd-5435-41bd-a1af-29c387c20135` を維持し、標準COMクラスファクトリー、正しいWICのQueryCapability、PixelCopy領域境界、高ビット深度、およびロスレス回転回帰を改善する。

## 検証済み
- [WIC統合CI #39925852369](https://github.com/taku077/wic_heic/actions/runs/39925852369): PASS。全解像度・zoom ROI・画像A→B→Aの独立デコード、正常なCOM IUnknown CLSID分離、PNG誤入力がBADIMAGE、lossless 90/180/270回転と元HEIC不変、preview v0.4.0共存・独立アンインストール。
- [MSI v1.1.20候補CI #39926012224](https://github.com/taku077/wic_heic/actions/runs/39926012224): PASS。実MSI v1.1.18→v1.1.20のMajorUpgrade、v1.1.20アンインストール、v1.1.18の正確な再インストールでDLLハッシュ同一、COM登録残留なし。
- 旧復旧MSIのSHA-256 `4FE1809A98C0B7EB7E7A505A6105BE57A34052DE74D79A13DB0853A9EC2AACE6`、復元DLLはユーザーの過去実機診断と一致。
- v1.1.20候補MSIのSHA-256 `5B7AB53837051FB4DCB79749D9464F522343B2A84C253F2E4F379023E2E47097`。GitHub Actions artifactのZIPダイジェストとは異なる。

## 未検証・完成ゲート
1. 上記は**Windows Server 2022のWICとMSI**試験で、ユーザーのWindows 10標準「フォト」GUIは自動化試験対象にできない。
2. Windows 10フォトの警告消失／編集・回転ボタン有効化／拡大縮小・左右移動／新規作業コピーの保存、元画像とEXIF/ICC/HDR等の保全は、実機で未確認。特にPhotosのラスター変換保存は無再圧縮回転を経由する保証がない。
3. 公開CI artifactは候補MSIとハッシュのみで、**実機導入用の完全な復元キットではない**。旧WICがインストールされているユーザーに直接インストールするよう促さない。既存の`Install_Experimental_WIC.ps1`は旧WICがあれば安全上ブロックする仕様。
4. 将来の実機試験に進む場合は、必ず**旧v1.1.18の完全一致MSI**と現在のCOM登録／DLLハッシュを保全したうえで、明示的に選択した一度限りの試験に限定し、人工HEICの作業コピーだけで結果を取得する。新旧コードの同梱・二重インストール・第三者コーデックの無断変更なし。

## 現段階の判定
**CIビルド／旧版復元検証済み、Windows 10 Photos完成ではない**。新規の独立MSIを乱発せず、1回の意味のある実機検証に向けて残る安全条件とGUI条件を解消する。
