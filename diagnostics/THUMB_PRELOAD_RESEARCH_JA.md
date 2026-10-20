# Windows Shell サムネイル先読み（検証段階）

## 調査元・技術の区別

- WinThumbsPreloader-V2 (Mfarooq360) は、\`IThumbnailCache::GetThumbnail\` を複数のファイルについて並列に呼ぶ方式。HEVCファイル1枚の復号を並列化する仕組みとは異なる。
- Microsoft の IThumbnailCache は、\`WTS_EXTRACT\` が未キャッシュ画像を生成した場合、\`WTS_EXTRACTDONOTCACHE\` が指定されない限りWindows Shellのキャッシュを埋める。エクスプローラーによる再利用を狙える。
- 元記事の「12～50倍」は第三者の特定ハードウェアによる報告であり、Windows 10・第7世代Core i5・外付けHDDでの保証ではない。

## 試験範囲（現時点）

\`wictest/ShellPreloadProbe.h\` は人工HEICの複製を作り、WindowsのShell APIで10枚ずつ「1ファイルずつ」と「2ワーカー」により生成要求する。各ファイルについて\`WTS_INCACHEONLY\`の再取得も試し、キャッシュが本当に充填されたかを数える。時間と結果件数だけログに書き、画像本体やパスは記録しない。試験用複製は最後に削除する。管理者権限・レジストリ登録変更は不要。Windows Server 2022 runner での試験は**Windows 10 Explorer 実機合格を意味しない**。

## 採用条件

1. Shellキャッシュに保存され、Windows 10 Explorer の再訪時に再利用される実測が必要。
2. 初回フォルダ表示の体感時間（JPEG/HEIC、キャッシュなし、10～30枚）、総所要時間、CPU・メモリ・HDD負荷を別々に測る。
3. 現在の低負荷志向を優先して、2ワーカー以上は実測で改善を確認するまで許可しない。多数のワーカーでHDDのランダムアクセスやExplorerのCOM負荷を増やさない。
4. HEIC Managerと競合する新規登録、実際の写真の書き換え、常駐サービス、Windows thumbnail cache全削除を行わない。
5. 自動先読みの開始時点・対象フォルダ・中止方法を設計し、ユーザーに個別画像の手動操作を求めない。

**重要:** 初めて開くフォルダで、事前生成を開始していない場合の初回1枚は速くならない。この方法だけでJPEG同等とは判定しない。WIC自身の復号高速化と併用検討する。
