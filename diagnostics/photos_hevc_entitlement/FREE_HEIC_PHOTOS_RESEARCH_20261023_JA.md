# Windows 10標準フォトを有料HEVC拡張なしで復旧するための公開情報調査
調査日: 2026-10-23 JST

## 前提と実機での既知結果（同じ方法を繰り返さない）
- Windows 10 Pro 22H2 build 19045, Photos 2026.11090.22001.0, HEIF 1.2.48.0
- 有料HEVC拡張は購入しない。メーカー向け無料版Store ID 9n4wgh0z6vhq は2026-10-16「このアプリはお使いのデバイスでは動作しません」で実機失敗。
- 2026-10-20 旧WicHeic Repair Test 1.1.18では回転保存が短期間成功したが、ラスター再圧縮・Exif欠落。2026-10-22の1.1.18再導入でHEVC警告が消えて編集・回転ボタンが復活したユーザー実機報告あり。保存は失敗。
- 2026-10-23既存のMFT列挙はHEVC decoder=0、WIC 1.1.18登録あり。ただしフォトのその時点のボタン状態は未観測。
- 実写原本保護、HEIC Manager及びv0.4.0非干渉。未確認のMSIは配布しない。

## 調査1：既存WIC/libheifを介してWindows Photosを有効化する（最優先）
- https://github.com/prsyahmi/wic_heic : 無料WIC-HEIC、libheif経由でWindowsアプリにHEIC復号を提供。ユーザー実機で編集ボタン復活を確認したのは同系統のWIC v1.1.18。
- https://github.com/prsyahmi/wic_heic/issues/7 : 別ユーザーのWin10 22H2でWIC登録後もフォトが低解像度サムネイルのみになる報告。WIC対応=フォト完全対応ではない。
- https://learn.microsoft.com/en-us/windows/win32/wic/heif-codec : WICのHeifCompressionMethod、無再圧縮回転/トリミングについて公式記述あり。ただしPhotosが独自WIC実装のロスレスオプションを渡す保証はない。
- 優先実装: 旧実機成功版のWIC COM登録/フォト編集有効化をビルド・注册の観点から差分解析。人工HEICで実Photos呼出しを調べ、ソースデータのメタ情報/ビット深度/画素等価を保護。環境を弄る前にCI上で旧版/候補版のAPI契約・メタデータ・アンインストール独立性を比べる。

## 調査2：無料のオープンソースHEVC Media Foundation Transform（新規研究候補）
- https://github.com/asafk/FFmpegMFT : FFmpegを使うMFT HEVC/H.265デコーダー (Main/Main10, NV12/YV12/YUY2)。LICENSEはLGPL-3.0。初期リポジトリ最終pushは2020-02-09、2026年現在 GitHub Releaseなし。
- 上流 FFmpegMFT/FFmpegMFT/dllmain.cpp の DllRegisterServer は COM登録後に MFTRegister(CLSID_FFmpegMFT, MFT_CATEGORY_VIDEO_DECODER, ..., MFT_ENUM_FLAG_SYNCMFT, ...) を呼ぶ。VLC/LAV等のアプリ内デコーダーとは違い、システムMFデコーダーカテゴリで列挙可能にする設計。
- https://learn.microsoft.com/en-us/windows/win32/medfound/registering-and-enumerating-mfts : MFTRegister/MFTEnumEx。実際にユーザーのWin10 Photos（パッケージアプリ）が任意の外部MFTを採用するかは**未実証**。HEVC拡張のStore資格チェックがあれば独立MFTだけでは警告は解除できない。
- 厳密要件: Windows隔離環境で安全にコンパイル → クローズドな登録スナップショット保存 → MF列挙/活性化/正しい出力試験 → 元に戻す。ユーザーPCに旧い未整備DLLやregsvr32を直導入しない。DLL依存の古いFFmpeg/セキュリティ、ライセンス、配布特許を調査してから。MFT列挙の成功をフォトGUI有効化に読み替えない。

## 調査3：Windows 10対応HEVC AppxBundleオフライン入手（技術候補、現状推奨しない）
- https://www.free-codecs.com/hevc-video-extensions-from-device-manufacturer_download.htm : 第三者ミラーでは Windows 10 用 Microsoft.HEVCVideoExtension 2.4.109.0/2.4.95.0 のAppxBundleが掲示されている。Microsoft公式Storeのメーカー向けID 9n4wgh0z6vhq のウェブ取得失敗とは**別の入手経路**ではある。
- https://learn.microsoft.com/ja-jp/troubleshoot/microsoft-edge/development/video-playback-issues : Microsoft公式はサードパーティ経由でHEVC拡張をインストールした場合、HEVC拡張のライセンス無効によって decoder instance error 0xC00DB3B3 が生じうると明記。パッケージにMicrosoftの署名があることと、そのPCで合法的・技術的に使えるライセンスを持つことは別。
- そのためサードパーティのMSIX/APPXをユーザーへインストールするよう指示せず、正式な配布の正当性・ハッシュ・署名・権利を検証できるまでは研究候補に留める。第三者ミラーの「誰でも無料」宣伝を証拠扱いしない。

## 調査4：Microsoft Photos Legacyを別経路として比較（無料、原本変更なし）
- https://support.microsoft.com/en-us/windows/apps/photos/what-is-photos-legacy : Microsoft公式は現在のPhotosとPhotos Legacyを同時使用できること、設定からPhotos Legacyを開けることを説明。最新版の標準フォトを削除・修復・初期化せずに代替呼出し経路の有無を調べられる。
- ただしHEIC編集への対応やHEVC拡張不要は公式に保証されていない。ユーザー実機の編集復旧と評価する前に人工HEICで確認が必要。

## 適合しない方法・誤情報
- VLC、LAV Filters、K-Liteなどは自前再生またはDirectShow等の経路が主であり、Windows PhotosのHEVC MF/HEIF WICを必ず満たすわけではない。「K-Liteを入れればPhotos編集が直る」は出典で実証されていない。
- CopyTrans HEICはユーザー実機でExplorerの異常負荷の既往があるため再提案しない。IrfanView、XnView MP、GIMP、OneDrive/iPhone JPEG変換は無料で画像を扱えても「Windows 10標準フォトでHEICのままJPEG相当の回転保存」という要件を満たさない。
- 2026-10-15のPhotos新HEIC/HDRエディターはWindows 11 Insider向け開始（https://blogs.windows.com/windowsexperience/2026/10/15/meet-the-new-photos-app-on-windows-11/）、Windows 10向け無料解決策とは扱わない。
- Intel第7世代にはHEVCハードウェア復号対応がある（https://www.intel.com/content/www/us/en/support/articles/000037112/graphics.html）が、ハードウェア対応自体はPhotosのMFT登録/利用権利を提供しない。
- 複数Microsoft公式資料: https://support.microsoft.com/ja-jp/windows/apps/photos/photos-app-video-editor-error-can-t-view-this-file-type 、 https://learn.microsoft.com/en-us/windows/win32/medfound/registering-and-enumerating-mfts 。

## 優先順位
1. 旧1.1.18実機成功のWIC COM経路との差分特定と修正（最優先、インストール変更前にソース/CIで検証）。
2. 独立したPhotos Legacy（存在する場合）の動作経路を人工画像で識別。現用Photos/原本変更不要。
3. OSS FFmpegMFTの技術成立性を隔離Windows CIで調査。ユーザーPC登録や導入は提案しない。
4. 第三者配布OEM旧Appxはライセンス/安全性の問題が解消するまで導入不可。

解決未確認を隠さない。公開情報探索結果であり、Windows 10 Photos GUIの実機成功を証明するものではない。
