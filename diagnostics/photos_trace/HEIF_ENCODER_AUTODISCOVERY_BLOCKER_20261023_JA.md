# 標準Photos HEIFエンコーダー自動選択障害（2026-10-23）

## Windows Server 2022 CIで実証したこと
- CI #39921260101 は標準HEIFコンテナーGUIDと明示的な独自ベンダーのWICデコーダー/エンコーダー生成、ズーム/画像移動/無再圧縮回転、v0.4.0共存に合格。
- 後続CI #39922904173 および #39923083939 は**通常のアプリが利用するベンダー未指定の CreateEncoder(GUID_ContainerFormatHeif, nullptr, ...)** を新たに検査したために失敗。未修正を完成版MSIにしない。
- HRESULT は **0x88982F8B = WINCODEC_ERR_COMPONENTINITIALIZEFAILURE**。
- CreateComponentEnumeratorでMicrosoft純正HEIFエンコーダー CLSID **{0DBECEC1-9EB3-4860-9C6F-DDBE86634575}** (Vendor GUID Data1=F0E749CA) と開発中の独自エンコーダー CLSID **{0E8AB687-73A5-455B-8C19-B93CA9AC0EEB}** (Vendor Data1=D01102C8) が共存していた。
- 前者にCoCreateInstanceすると **0x8000FFFF**、後者は **S_OK**。明示的にMicrosoftのベンダーを指定したCreateEncoderも0x88982F8B、独自ベンダーの指定では成功。**自動選択経路はMicrosoft純正の初期化失敗に引き込まれている可能性が高い**。
- これはWindows Server CIのWIC動作であり、Windows 10 Photosが実際に同じAPIを呼ぶことを直接観測した証拠ではない。

## 誤解しないこと
- 旧v1.1.18では実機Photosの編集ボタンが復活した履歴があり、HEVC拡張購入を条件にしない開発方針は維持する。
- 成功した**ベンダー指定のWIC API**だけを根拠に、ベンダーを指定しない**標準フォト**でHEIC保存可能とは言わない。
- Microsoftの標準HEIF CLSIDを上書き/削除したり、ライセンス偽装したり、ユーザーPCの登録を書き換えるMSIを未承認で配布しない。
- カスタムHEIFエンコーダー自体は正常にCOM生成されるが、Photos UIの編集/保存ルート・MetaData保全は未確認。

## 次の切り分け
1. Windows Server使い捨てCI内だけで、Microsoft純正HEIFエンコーダーの登録にある `ArbitrationPriority`（WIC公称0〜10、小さいほど優先）を一時的に1にして、独自の優先度0が選択されるか比較。必ず以前の値とRegistryValueKindを復元し、同じWindowsビルド・人工HEICのみ使用。
2. 変化する場合でもWindows 10 Photosがその選択経路を使用する証拠がなければ実機改変を勧めない。サードパーティDLL/レジストリの無承認変更なし。
3. 正常なアプリによる `CreateEncoder(...,nullptr,...)` と、Win10標準フォトGUI編集/保存/ズーム/画像移動、元画像とExif/ICC/HDR保護を全て満たすまで完成版MSIを提示しない。
4. 現有グローバルWIC v1.1.18と、別系統のFastPreview v0.4.0は勝手に置換しない。

根拠:
- https://github.com/taku077/wic_heic/actions/runs/39922904173
- https://github.com/taku077/wic_heic/actions/runs/39923083939
- https://learn.microsoft.com/en-us/windows/win32/wic/-wic-codec-error-codes
- https://learn.microsoft.com/en-us/windows/win32/wic/-wic-generalregentries
