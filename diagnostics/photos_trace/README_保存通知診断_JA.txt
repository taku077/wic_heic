Windows 10 保存通知とサムネイル更新の切り分け・独立診断

1. ZIPファイルを「すべて展開」します。
2. 「02_保存通知診断_ダブルクリック.cmd」をダブルクリックします。
3. ファイル選択ダイアログで、監視したいHEICを選択します。
4. 観測画面を開いたままWindows標準フォトで回転保存してください。
   約90秒後に自動終了します。途中終了は「終了して結果を保存」。
5. 起動ファイルと同じフォルダー内に「診断結果」を作り、
   テキストが自動保存され、フォルダーとメモ帳が開きます。

実際に回転保存を行う場合は、元画像を守るため作業コピーを使用してください。
本診断ツール自体はHEICの読み取りしか行いません。
写真本体・サムネイルキャッシュ・レジストリ・WIC登録を一切変更しません。
Shell変更通知はWindowsから受信するだけで送信・強制更新はしません。

表示内容は FileSystemWatcher のファイル通知と
SHChangeNotifyRegister のShell通知を時刻付きで記録したものです。
対象フォルダー内の一時ファイルから生じるイベントもありますが、
ファイル名やディレクトリのフルパスはログに残しません。
対象/フォルダー/その他という分類とイベントの種類だけ記録します。
Photosでファイルを回転保存すれば前後のSHA256が変化するのは正常です。
保存エラーが出た時刻や、Explorerの向きが変わったかもメモしてください。

【重要な制限】
・本ツールが受信したShell通知はExplorerが表示を更新したことの証明ではありません。
・通知が記録されなくても、Explorerが通知を受け取っていない証明ではありません。
・Windowsは個別ファイル通知をフォルダー通知へまとめる場合があります。
・FileSystemWatcherの通知は重複することや失われることがあります。
・一度も保存操作しなければ、変更イベントが出ないのは正常です。
・写真を読み込むだけなら元画像を選んでよいですが、
  Photosで保存操作する場合は必ず作業コピーをお使いください。
・インストール済みWICのEncoderCommit結果は本ツールでは直接記録しません。
  フォト保存失敗の原因確定には、別途WIC側の数値ログとの照合が必要です。

参考
https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shchangenotifyregister
https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shchangenotification_lock
https://learn.microsoft.com/en-us/dotnet/api/system.io.filesystemwatcher
