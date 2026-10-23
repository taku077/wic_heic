# Windows 10標準フォト HEVC編集・回転の復旧に向けた調査ルート

## 古い成功と現障害を区別する

2026-10-20のユーザー実機では、グローバルWIC v1.1.18を介してWindows標準フォトのHEIC回転保存に成功した。これはCI成功ではなく実機記録。ただし WriteSourceRaster -> EncoderCommitRecompressed による再圧縮、10bit→8bit、EXIF消失があったため、JPEG相当の無劣化保存とは言えない。

2026-10-23 02:08 JSTの既存実機診断はWindows 10 Pro build 19045、Microsoft Photos 2026.11090.22001.0、HEIF拡張1.2.48.0、HEVC decoder MFT 0件、グローバルWIC v1.1.18登録あり。同じWICを入れ直すだけで標準フォトのHEVC警告・編集無効が解消しなかった実機経過がある。

## 原因切り分けの優先順位

1. **Microsoft Photos側のHEVC利用可能状態を先に回復する。** Microsoft公式には、Windows 10 PhotosのHEIC写真についてもHEIF/HEVCの両拡張が必要になることがあると記載されている。WICでプレビューできることはPhotos編集機能の有効化と同義ではない。
2. 既に権利があるMicrosoft Storeの公式HEVC拡張、またはPCメーカーの正規提供版を検討する。公式メーカー向け製品ページ https://apps.microsoft.com/detail/9n4wgh0z6vhq は存在するが、すべてのユーザーが無料取得できるとは限らない。自動インストール、非公式AppX再配布、ライセンス偽装は行わない。
3. HEVCが正規に利用できる条件で、**公開された人工HEICの作業コピーだけ**を用いフォトGUIの編集・回転有効化を確認する。
4. その後、Photos→WICの呼び出し方式と、無再圧縮保存時のEXIF/ICC/HDR/MakerNote/XMP/サムネイルを含む情報保全を検証・修正する。

## 追加した独立した読取専用情報

Photos_HEVC_AppXScope_ReadOnly.ps1 は **現在ユーザー / 他ユーザー / Windowsプロビジョニング** の3登録範囲だけを調べる。これは前回の read_only_diagnostic.json に含まれないスコープ情報であり、WIC / MFT / HEIC画像の過去の調査は繰り返さない。

取得失敗や閲覧権限不足は「不明」と扱う。Windowsの設定、ライセンス、写真、MSI、フォトアプリ、WIC、v0.4.0やHEIC Managerのどれも変更しない。結果は新しいJSONファイルとして保存し、ユーザーの実写真を一切読み込まない。AppX存在の確認はStoreでの購入権利・フォトからの実際の利用可否を証明しない。

必要になった場合だけ一回実行するための独立スクリプトであり、本体MSIに同梱しない。CIが合格してもWindows 10実機でのPhotos復旧を証明したことにはならない。

## 完成条件

Windows 10標準フォトGUIで表示・編集・回転・保存が可能なこと、人工HEICの原本が不変なこと、無再圧縮と画像・メタデータ保全、WICとv0.4.0の共存を満たして初めて完成版MSIを提示する。

Sources:
- https://support.microsoft.com/ja-jp/windows/apps/photos/photos-app-video-editor-error-can-t-view-this-file-type
- https://learn.microsoft.com/en-us/windows/win32/wic/heif-codec
- https://apps.microsoft.com/detail/9n4wgh0z6vhq
