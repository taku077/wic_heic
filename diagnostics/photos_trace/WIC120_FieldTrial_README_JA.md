# Windows 10 標準フォト HEIC：旧WIC v1.1.18 → 研究用 v1.1.20 1回限りの試験キット

**まだWindows 10標準フォトの編集・保存まで成功した完成版ではありません。ユーザーPCへの導入は、CIによるキット全体の更新・復元・元DLL保全の検証に合格し、今回の実機テストを明示的に選択した場合に限ります。**

## 目的と安全条件
- Windows 10 Pro 22H2 build 19045、Windows標準「フォト」で HEICの表示・ズーム・画像送り・回転・編集・新規保存をJPEGと同等にするための検証。
- 2026年10月20〜22日に一時成功した旧v1.1.18と同じ**独自HEIC WIC GUID**を維持した研究用v1.1.20で試す。
- 有料HEVC拡張購入、メーカー版の再試行、Windows純正HEIFコンポーネントの優先度変更は禁止。
- HEIC Manager・v0.4.0の独立サムネイル・関連付けを変更しない。ユーザー実写真（IMG_4074、IMG_3915等）では**絶対に試験しない**。本人の画像のコピーもこのキットに含まれない。
- ログ、セッション、事前・事後スナップショットはこのキットのフォルダに残す。写真を自動削除・上書きしない。

## キット内の期待ファイル（検証が終わったCIでのみ生成）
- `WicHeic-Repair-1.1.18-ORIGINAL-x64.msi`：過去実機とのハッシュ完全一致が確認された旧版の復元用。
- `WicHeic-Repair-1.1.20-TRIAL-x64.msi`：実験版の新MSI。新しい名前で本番版との誤認を防ぐ。
- `WIC120_MSI_SHA256.txt`：**同じCI実行で作った新MSI**のSHA-256。
- `WIC120_DLL_SHA256.txt`：**同じCI実行で作ったWIC DLL**のSHA-256。登録DLLの照合にも使用する。
- `WIC120_OneTimeTrial.ps1`：標準動作は読み取り専用。明示した `-Install` / `-Restore -Session` 以外ではMSIの変更を行わない。
- `WIC_Preflight_ReadOnly.ps1`、`WIC_Preflight_Compare.ps1`：Windows/COM/MSI/DLLを読取りと復元前後の完全比較。
- `Synthetic_Test_Only.heic`：公開人工HEIC。実機試験に使うときは新しい作業フォルダへ**コピー**し、原本を保持する。
- `README_JA.md`：この説明書。

## 導入前の要件
1. **現在導入済みの旧v1.1.18が、実際に実機で成功していたDLLハッシュ** `9E9D2F17038C03A5BF8840A8BD43BC4FDA19C91E947C4CD5904A8209051F93D4` **と一致**すること。既存の別バージョン、または未登録のPCには使えない（安全上停止）。
2. 復元用v1.1.18 MSIのSHA-256が `4FE1809A98C0B7EB7E7A505A6105BE57A34052DE74D79A13DB0853A9EC2AACE6` と一致すること。
3. `WIC120_OneTimeTrial.ps1` の読み取り専用モードで `WIC120_PRECHECK_OLD118_EXACT_MATCH_PASS` を確認すること。予期しない32bit・ユーザー別WIC登録やv0.4.0の別版があれば中止。
4. 復元用キットをローカルの通常フォルダに展開し、ダウンロードしたZIPとファイルを保管しておくこと。圧縮ファイル内から実行しない。
5. MSI操作は明示的な管理者権限が必要。キットは権限を自動昇格させない。

## ユーザーが将来明示的に実機試験を選んだ場合のみ
```powershell
# 通常のPowerShell 5.1。JSONを新規作成するだけ。初回はこれだけ。
.\WIC120_OneTimeTrial.ps1 -CheckOnly

# 読取専用結果・現在のPhotos状態を確認したうえで初めて、
# 管理者PowerShell 5.1から明示して実行（まだ事前承認待ち）。
.\WIC120_OneTimeTrial.ps1 -Install

# 作業コピーだけでフォトの表示/ズーム/←→移動/回転保存の可否を確認。
# キットに表示された固有のセッションファイルを使い管理者PowerShellから復元。
.\WIC120_OneTimeTrial.ps1 -Restore -Session '.\WIC120_Session_<固有ID>.json'
```

復元で `WIC120_USER_BASELINE_118_FULL_RESTORE_PASS` となるまで、「復旧完了」と呼ばない。途中エラーが出た場合は追加MSI導入、手動COM削除、レジストリ書換えをしない。診断ログを保管し、作業を中断する。旧版MSIが同梱されているからといって、無条件で上書き・自動復旧できるわけではない。

## 実機フォト合格判定
人工HEICの**作業コピー**を使用して、標準フォトで警告なしに開く→マウスホイール/ズーム→フォルダ内の前後画像へ移動→作業コピーだけ90度回転と別名保存。原本のSHAを確認し、保存画像の画素形状、向き、EXIF/ICC/HDR/サムネイル保持を調べる。表示や保存に異常があれば作業を止める。

**CIのWindows Server 2022にMicrosoft PhotosのWindows 10 GUIが存在するわけではないため、最終確認は実機のみ。** 回転ボタン復活と無再圧縮保存・メタデータ保全は別の合格項目。

GitHub: https://github.com/taku077/wic_heic/tree/feature/photos-120-one-time-restore-kit
