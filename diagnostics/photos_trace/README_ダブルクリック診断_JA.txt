IMG_4425 サムネイル診断（Windows 10 / ダブルクリック版 v3）
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
V3では、2つのサムネイルをメモリ内で64×64へ縮小して数値比較します。
rgb_mae_64 は画素の平均絶対差（0に近いほど似ている）、
rgb_mae_if_180deg_64 は一方を180度回転した場合の平均差です。
pixels_gt20_percent_64 は差の比較的大きい画素の割合です。
ハッシュ不一致はJPEG等の圧縮や色処理の違いでも発生するため、
それだけでキャッシュ破損や方向不一致とは判定しません。
画像の画素や縮小画像をファイル・診断ログに保存しません。
管理者権限、MSIの更新、コーデックの登録変更は不要です。
Windows 10実機での動作は未検証です。

関連資料（Microsoft公式）
https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nf-thumbcache-ithumbnailcache-getthumbnail
https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/ne-thumbcache-wts_flags
https://learn.microsoft.com/ja-jp/powershell/module/microsoft.powershell.core/about/about_powershell_exe?view=powershell-5.1
