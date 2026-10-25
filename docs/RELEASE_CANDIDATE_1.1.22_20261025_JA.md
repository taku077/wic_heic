# 1.1.22 Release Candidate — Windows 10 フォトの回転保存後のファイル縮小に対する修正
作成日: 2026-10-25 JST
締切: 2026-10-27（火）20:00 JST
状態: Windows 10標準フォトUIでは未検証。人工HEICのCIのみ。

## この修正の根拠となる実機結果（2026-10-25）
利用者が自作WIC 1.1.21 MSIを導入してWindows 10標準フォトで確認：
- HEVC拡張が必要という注意文は消失。
- 回転操作と保存が可能。
- **回転前約1.6MBだった画像が保存後約575KBに縮小した。**
- 元と保存後のピクセル寸法、Exif、色深度、視覚画質は未比較。ファイルサイズだけで画像の劣化量・原因は確定できない。
- 今後はユーザーの実写真ではなく人工HEICで確認する。HEIC ManagerとIMG_4074に触れない。

## 今回の修正
- `wic_heic/HeicBitmapEncoder.cpp`: `heif::Encoder(heif_compression_HEVC)` のラスタ保存に `encoder.set_lossy_quality(92)` を明示し、未指定の既定圧縮品質による世代劣化を低減。
- 元ビットストリームがWICから直接渡された時の既存の無再圧縮パスは変更なし。
- 正体不明の加工済みラスタが渡される場合は、元HEVCを証明できないため、暗黙の無再圧縮回転を装わない。
- `Installer/Package.wxs` を **1.1.22** に上げ、利用者が試した1.1.21と区別する。
- `.github/workflows/repair-x64-build.yml` のMSIのバージョン確認・候補名も1.1.22に変更。

## 重要な制約（今回で完成を宣言しない）
- 高品質の**再エンコード**とHEICの**無再圧縮回転**は異なる。92の設定はファイルサイズや画質一致を保証しない。
- 旧版のデコーダーは最終的にWICへ24bppRGBを返すため、10/12bitやHDRの情報が8bitに失われる場合がある。この候補ではまだ改善していない。
- 標準フォトが加工済みWICラスタを渡す経路では、Exif/MakerNote/XMP等の自動保持ができない場合がある。これも未解決。
- 標準フォトの保存経路は端末で確認したログがまだない。ファイル容量の大幅な減少は再圧縮経路の存在と整合するが、決定的証拠ではない。
- 画像の縦横ピクセルが縮んだ場合は別の根本原因で、品質92だけでは修正できない。
- ユーザーのWindows 10環境における**1.1.21→1.1.22更新/旧1.1.18への復元**の安全性は未確認。Windows Server 2022 CIによるクリーン導入・削除は実機復元の代わりにならない。**導入を促さない**。

## 受入判定（新規の大量試験追加はしない）
既存のWindows GitHub Actions CIのMSI x64ビルド、人工HEIC WIC保存、隔離環境でのインストール→アンインストール→WIC登録状態復元が全てSUCCESSとなったコミットを候補にする。Actionsのartifact内にMSIとSHA-256を残す。正常な実機Photos回転保存は、ユーザーが安全な導入経路を確認した後の別判定とする。

## 期限
機能凍結は2026-10-26 18:00 JST。MSI提出は2026-10-27 20:00 JSTのまま。予防的な機能研究や試験を増やして期限を延長しない。

## 照合資料
- libheif encoder quality API: https://github.com/strukturag/libheif
- `heif::Encoder::set_lossy_quality`: https://sources.debian.org/src/libheif/1.3.2-2~deb10u1/libheif/heif_cxx.h/
- WIC WriteSource: https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframeencode-writesource
- バージョン前MSI成功: GitHub Actions #40124209047
- 保存用計画: `docs/PHOTOS_118_MSI_RELEASE_PLAN_20261027_JA.md`
