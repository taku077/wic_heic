# Windows 10実機での人工HEICサムネイル先読み検証（2026-10-21）

この文書はユーザー提供の診断ZIP内テキストログの技術的要約であり、画像・ユーザーファイルのパス・識別情報を含まない。

## 試験結果（実機）
- Windows 10: 10.0.19045
- 検証画像: 固定SHA256の人工HEICを10枚複製
- Explorerテストフォルダの認識: True
- Shell先読み結果: folders=1 generated=4 reused=6 errors=0
- コピーと人工HEIC原本: SHA256不変
- Explorer上での実描画時間・JPEGとの速度差: 測定していない
- CPU・メモリ・ディスクIO負荷: 測定していない

## 終了処理の未解決事項
- 子監視プロセスの標準出力には WARMUP_WATCH_COMPLETED があり、処理完了は記録されている。
- 親側の WATCH_AUTO_EXIT は False。制限時間内に子プロセスが終了しなかったと判定された。
- そのため、今回の実機試験を完全合格とは扱わない。
- 子プロセスの明示的終了・終了状態と完了マーカーの別判定を実装し、Windows 10実機再確認が必要。

## 現時点の結論
Windows 10でShellサムネイルキャッシュへの生成・取得経路は動作し、人工画像のファイル改変は検出されなかった。
初回HEIC表示のJPEG同等速度と、検証ツールの自然終了は未確認。
10bit、ICC/EXIF/XMP等の完全保持、無再圧縮回転という最終目標は変更しない。

## 2回目の実機テスト（03:25の診断ZIP）
- OS: Windows 10 10.0.19045.0
- EXPLORER_FOLDER_DETECTED=True
- WATCH_AUTO_EXIT=True、WATCH_COMPLETION_MARKER=True、WATCH_EXIT_CODE=UNAVAILABLE
- 人工HEIC10枚のShell経路: generated=3、reused=7、errors=0
- cache_query_ms=54.48（10件の合計）
- generation_ms=1945.49（生成3件の合計、平均約648.50ms/枚）
- folder_elapsed_ms=2544（フォルダ1件全体）
- COPIES_HASH_UNCHANGED=True、FIXTURE_HASH_UNCHANGED=True
- 標準エラーは空。

### 解釈の注意
再利用7件は、試験開始前から登録されていたことを意味しない。新規の人工HEICフォルダを開いたExplorerが先に生成し、その後先読み処理が参照した可能性がある。先読み自体の速度向上量の測定にはなっていない。
また、画面の実描画速度は未計測。JPEG同等の初回速度を達成したとは判定しない。
今回の実機では監視プロセスが終了したことは確認できたが、PowerShellのExitCode取得は不可で、終了コード0は直接確認できていない。

### 次の検証
2026-10-21以降の開発コードでは、Explorer起動前のキャッシュをWTS_INCACHEONLYのみで読み、先読み後と比較する CacheOnly モードを追加。独立に実行してもWindowsのサムネイルを生成しない設計。
実機での真の初回表示改善を判断するには、未キャッシュの別画像セットとJPEG対照、描画時間およびCPU/メモリ/ディスク負荷が必要。
