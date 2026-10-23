# Windows 10 フォト HEIC「HEVC拡張」警告復活：v1.1.18 vs v1.1.20 原因調査・修正記録
2026-10-23・研究ブランチ：`investigate/photos-wic-arbitration-after-120-failure`

## 実機の確定事項
- Windows 10 Pro 22H2 build19045 / Photos 2026.11090.22001.0 / HEIF Image Extension 1.2.48.0 / 有料HEVC拡張なし。
- 実機v1.1.20 (WIC Repair Test) のデコーダーとエンコーダーがx64 COMに正常登録、DLL SHA-256がMSIの期待値に一致。よって本症状は **MSIインストール失敗ではない**。
- フォトがHEICを簡易表示するが「完全な解像度にはHEVCビデオ拡張機能が必要」と警告し、編集・回転ボタンが無効。2026-10-23 19:14 JSTの本人実機結果で **v1.1.20のフォト互換はFAIL**。
- 同日19:17 JSTの復旧ZIPに含まれる7時系列JSONとMSIログで、v1.1.20アンインストール→歴史的v1.1.18 MSI再インストール→x64 decoder/encoder DLL SHA-256 `9E9D2F17038C03A5BF8840A8BD43BC4FDA19C91E947C4CD5904A8209051F93D4` 一致を確認。復元成功。元画像の撮影データやファイル内容は必要なく使用しない。
- 独立サムネイルv0.4.0は現在アンインストール済み。今回の症状の原因と推測して再インストール／削除しない。

## ソース差分から確定したこと
歴史的v1.1.18コミット `f5b66fe89fa203c89967b46399ed6af61a6161e8` と研究ブランチを比較。
- `wic_heic/dllmain.cpp` のHEICデコーダー／エンコーダーCLSID・ContainerFormat・ArbitrationPriority・ファイル拡張子は同じ。**MSIの登録が変わったから**という説明は不適切。
- `wic_heic/HeicBitmapFrameDecode.cpp` の10/12/16-bitを48/64bppへ報告する基本処理は両版で同じ。**この処理変更が1.1.20だけ編集ボタンを壊した**という説も成立しない。
- `CHeicBitmapDecoder::QueryCapability()` は異なる。歴史的v1.1.18は常に `SameEncoder|CanDecodeSomeImages|CanDecodeThumbnail` (`0x15`) を返していた。v1.1.20は実際のストリームを検査し、確認できた `CanDecodeSomeImages`／`CanEnumerateMetadata`／`CanDecodeThumbnail` のみ宣言。画像ごとに能力が異なる。
- MicrosoftのWIC仕様では、`SameEncoder` は「その画像が同じベンダーのエンコーダーで作られたと認識する」ときのみ許容。iPhone等のHEICに無条件に付けるのは不正確。旧版の一律宣言を闇雲に戻す修正はしない。必ずWIC選択とPhotos GUIの双方を分けて検証する。
- `QueryCapability` には実際のコード上のバグがあった：**主画像を認識できても、任意のEXIF一覧またはサムネイル処理で例外が起きると、外側のcatchが能力マスク全体をゼロにする**。研究ブランチ `3421e5f48c5d2de8f66bb6034be050dd019dc3d9` で主画像の確認済みビットを保持し、任意部分だけ安全に無効化するよう修正。これは実在する誤判定バグの修正であって、Photos警告消失の実機証明ではない。

## 調査・修正の切り分け
1. Win Server 2022隔離環境で、元v1.1.18の完全一致MSIと研究版WICを順にインストールし、**同一の公開人工HEIC**でWIC `QueryCapability` の出力ビットと自動デコーダー選択をA/B比較する。MSIの旧版復元を確認、利用者向けMSI成果物は作らない。GitHub Actions: `photos-wic-postfail-arbitration-ci.yml`。
2. WICデコーダーをフォトが実際にロードしているか、写真を読まずに**プロセスに読み込まれたDLLのベース名だけ**を測る `Photos_WIC_Process_ReadOnly.ps1` を開発。アプリのサンドボックス等で取得できない場合は「不明」であり、読取失敗を根拠に推測しない。
3. Microsoft公式はWindows 10標準フォトで写真にもHEIF/HEVC拡張が必要になることがあると記載：https://support.microsoft.com/ja-jp/windows/apps/photos/photos-app-video-editor-error-can-t-view-this-file-type 。フォトがWICを利用するかHEVC拡張の有効性を別に検査するかは、実機トレースなしに断定できない。
4. 旧v1.1.18で一時的に警告が消えたユーザー実機報告は維持する。しかし**旧版復元直後のGUI状態と、1.1.20導入中のGUI状態が同一条件で比較されていない**ため、今回の差分が `SameEncoder` の有無に由来するとまだ証明されていない。
5. **新しいMSIの実機導入・手動のMicrosoft登録改変・Store拡張購入・原本HEICの回転／保存をしない。** WIC実験CI合格後でもPhotosのフル解像度＋編集＋回転＋安全な保存が実機で証明されるまでは完成としない。

## 合格条件
- 合法的・仕様準拠のWIC能力宣言が維持でき、実HEIC/人工HEICの主画像が誤ってデコード不能と報告されない。
- フォトがHEVC警告なく表示・編集・回転できること（Windows 10実機）。ここが未合格なら対策は完了していない。
- 作業コピー限定の回転保存で画素形状・元HEICとEXIF/ICC/HDR等を毀損しない。プライベート画像で試さない。
- 旧WICの完全一致復旧が保証される。HEIC Manager、元写真、v0.4.0（未導入）に干渉しない。
