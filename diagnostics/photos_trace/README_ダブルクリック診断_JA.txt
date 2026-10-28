IMG_4425 サムネイル診断（Windows 10 / ダブルクリック版 v2）
==========================================================

【使い方】
1. ZIPの中身をすべて展開します。
2. 「01_診断開始_ダブルクリック.cmd」をダブルクリックします。
3. 調べたいHEICを指定します。元画像を選んでも構いません。
診断後は、結果フォルダーとメモ帳が自動で開きます。

【結果の保存先】
原則、起動したCMDと同じフォルダーの中の「診断結果」です。
そこへ書き込めないときは「ドキュメント\診断結果」、さらに不可ならTEMPへ保存します。
完了画面には実際のテキストのフルパスも表示します。

【選択画面を廃止】
「はい／いいえ」選択はなくなりました。
古いキャッシュを先に記録し、その後で強制再抽出して自動で比較します。
画像ファイル本体は変更しませんが、Windowsのサムネイルキャッシュは更新され得ます。

【注意】
Explorerでサムネイルの向きがおかしくなった際は、F5を押す前に診断してください。
元HEICを直接診断できます。ただし、Photosで回転保存を試す場合はコピーを使用してください。
結果には画像のファイル名、パス、EXIFや元画素は保存しません。
管理者権限、MSIの更新、コーデックの登録変更は不要です。
Windows 10実機での動作は未検証です。

関連資料（Microsoft公式）
https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nf-thumbcache-ithumbnailcache-getthumbnail
https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/ne-thumbcache-wts_flags
https://learn.microsoft.com/ja-jp/powershell/module/microsoft.powershell.core/about/about_powershell_exe?view=powershell-5.1
