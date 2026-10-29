# WicHeic 1.1.30 — Windows10標準フォト負荷低減候補

本MSIはWindows Server 2022上の人工HEIC CI検証用ビルドであり、Windows10実機でのフォト体感改善は**未検証**。製品版の恒久的な安定性を保証しない。

## 変更点
- Windows標準フォト（既存のPhotosホスト許可リスト）では、6MP以上の8bit不透明HEICで復号codec worker数を最大2に抑え、同時復号があれば既定方式へ戻す。4論理CPU未満でも既定方式。PhotosのCPU/UI競合を軽減する狙い。
- Explorerでは既存の単独4-worker高速化を維持。ビット深度不明・10/12bit・alpha付き・小画像には適用しない。
- 既存の無再圧縮回転・EXIF/ICC・保存拒否・フォルダの大量画像回帰試験を維持。
- 人工12MP HEVC埋込320x240サムネイル経路と、フルサイズ復号が発生しないことの回帰試験を追加。

## インストール前の要件
1. **現在のWicHeic旧バージョンMSIを手元に確保**。このファイルを復元できない場合は実機へインストールしない。
2. 人工HEICの作業コピーのみで、フォトを開く時間、CPU使用率、ズーム、回転50回と保存、画質・向き・警告を比較する。写真の原本は触らない。
3. CIの全試験 SUCCESS、MSI ProductVersion=1.1.30、DLL SHA256照合、MSIを導入/削除した後の登録復元を確認してから使用する。

## 異常が出た場合
Windowsのアプリと機能で1.1.30をアンインストールし、保管してある旧バージョンのMSIを再インストールする。MajorUpgradeの自動ダウングレードは期待しない。Explorerキャッシュを一括削除せず、レジストリを不用意に変更しない。

## 未検証事項
Windows10実機のフォト操作応答、iPhone実HEICの全形式、IMG_4425の48タイルgridとHDR gain map、全フォト保存操作の安全性、すべてのCPU構成の改善。CI人工HEICでの2-worker RGB一致は実機GUI合格の証明ではない。

## 関連仕様
- https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapdecoder
- https://github.com/strukturag/libheif
- https://github.com/strukturag/libde265
