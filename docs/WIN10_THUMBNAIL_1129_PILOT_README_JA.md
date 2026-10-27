# WicHeic 1.1.29 — Windows 10 初回サムネイル試験（候補版・未実機検証）

**注意：本書はWindows10へのインストールをまだ承認したものではありません。配布MSIのGitHub Actions全工程SUCCESS、SHA256、1.1.28復元MSI確保を確認してから試験すること。**

## 変更点（1.1.28から）

- 標準Windows 10フォトのHEICプレビュー・回転/保存・オリジナルコンテナ保護は既存コードを維持。
- 追加するのは、大型で不透明な**8bit HEICの復号ワーカー自動選択**。6,000,000画素以上・8bit luma/chroma・アルファ無し・論理CPU数2以上の場合だけ、単独実行に4（論理CPU数4以上）、同時実行が別にあれば2、過密時は従来方式へ。
- **6MP未満のHEIC、10/12bit、alpha付き**は従来方式。PNG/JPEGには新しい処理が入らない。
- v0.4.0専用IThumbnailProvider、Codecレジストリ優先順位変更、Windowsフォト設定の変更は導入しない。
- 旧研究はWindows Server 2022で人工12MPの256px初回サムネイルが約59～60%短縮、混在3ファイルの1回バッチで約34%短縮。**Windows10 Explorer実機でこの数値が出る保証はない**。

## インストール前の必須確認

1. **実際に現在使っているWicHeic 1.1.28 MSIをローカルに確保**し、開けることを確認する。もし確保できない場合は、この1.1.29候補を入れない。
2. 現行1.1.28で、公開人工HEICの*コピー*をWindows10標準フォトで開き、90°回転の表示、警告の有無、保存後の縦横比を確認。過去の横長表示崩れは未解決であり、1.1.29で直ると期待しない。
3. ベースラインとして、6MP以上のHEICを少なくとも5枚、**Explorerで最初にフォルダを開いた際のサムネイルが出揃う時間**を動画等で記録。Windowsサムネイルキャッシュがあると2回目以降は結果が変わるため、条件を揃える。
4. 元の写真には何も保存しない。必要な試験データは人工HEICか作業コピーのみ。写真の作成日時やExifを変更する操作は避ける。

## 1.1.29をインストールした後の検査順序

1. Windowsの「インストールされているアプリ」等で **1.1.29** と表示されるか確認（1.1.28のままならテストを中止）。
2. 1.1.28の時と同条件の独立したコピー・別テストフォルダでExplorer初回サムネイルの表示完了時間を計測。同じフォルダの2回目の表示やキャッシュ済みサムネイルだけで速さを評価しない。
3. PNG/JPEGの表示とフォトのHEIC表示/編集/回転UI（HEVC警告の有無、コピーの回転保存、向き、メタデータ）を確認。
4. 6MP未満のHEICや10bitファイルは高速化対象外で、現行相当の表示になることを確認。
5. 異常、保存失敗、サムネイルの向き変化、遅延悪化、システム不安定があればそこで試験を打ち切る。

## 復元方法

- 1.1.29をWindowsのアプリ管理から**アンインストール**。旧版の上書きダウングレードは想定しない。写真ファイルは削除しない。
- 保管しておいた**元の1.1.28 MSIを再インストール**。
- 以前の標準フォト表示、回転保存、Explorerサムネイル表示を、人工画像コピーで再度確認。
- 両MSIはx64版。Windowsを無理に再起動したり、Explorerを強制終了してキャッシュ全削除・レジストリ全リセットしたりしない。

## 保護・測定上の注意

Windowsの`IThumbnailCache::GetThumbnail`にはキャッシュを無視して毎回再抽出する`WTS_FORCEEXTRACTION`が存在する。ただし、この計測と実際のExplorer画面に全サムネイルが並ぶ時間は別指標。勝手にシステムの全サムネイルキャッシュを削除しない。

- https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/ne-thumbcache-wts_flags
- https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nn-thumbcache-ithumbnailcache
- https://github.com/strukturag/libde265
- https://github.com/strukturag/libheif
- https://github.com/taku077/wic_heic/tree/candidate/thumb-adaptive-1129-win10-pilot
