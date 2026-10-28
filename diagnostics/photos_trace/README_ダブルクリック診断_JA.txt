IMG_4425 サムネイル診断（Windows 10 / ダブルクリック版）
==========================================================

【操作はこれだけです】
1. このZIPの中身をすべて1つのフォルダーに展開します。
2. 「01_診断開始_ダブルクリック.cmd」をダブルクリックします。
3. ファイル選択画面で、IMG_4425.HEICの「コピー」を選択します。
4. 実行方法の確認画面が表示されます。通常は「いいえ」を選んでください。
5. 診断結果はメモ帳で開き、テキストファイルとして自動保存されます。

【重要：サムネイルの向きが更新されないとき】
先にエクスプローラーでF5を押さず、古い向きのまま診断してください。
「いいえ」：既存キャッシュの確認だけ（推奨、キャッシュ強制更新なし）。
「はい」：キャッシュを記録した後に強制再抽出も実施します。
           Windowsのサムネイルキャッシュが更新される可能性があります。
「キャンセル」：何も調べず終了します。

結果は通常、ドキュメントの HEIC_Thumbnail_Diagnostics フォルダーに保存。
そこに保存できない環境ではTEMPフォルダーに保存します。
診断結果には画像ファイル名や写真のEXIF情報、元画像の画素は記録しません。
記録するのは寸法・状態・短い画素ハッシュ・経過時間・エラーコード等です。

【注意】
・必ず元画像を別に保管し、HEICのコピーを選択してください。
・画像ファイルの保存・回転・更新は行いません。
・診断にはWindows標準のPowerShell 5.1を利用します。
・管理者権限、MSIの変更、コーデック登録変更は不要です。
・「はい」の強制再抽出はWindowsのサムネイルキャッシュに影響します。
・ダブルクリックした際はコマンドウィンドウも表示されます。
  診断が完了してメモ帳が表示されたら、閉じて構いません。
・診断結果の実機対応は未確認です。人工画像での自動テスト済みです。

関連資料（Microsoft公式）
https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/nf-thumbcache-ithumbnailcache-getthumbnail
https://learn.microsoft.com/en-us/windows/win32/api/thumbcache/ne-thumbcache-wts_flags
https://learn.microsoft.com/ja-jp/powershell/module/microsoft.powershell.core/about/about_powershell_exe?view=powershell-5.1
