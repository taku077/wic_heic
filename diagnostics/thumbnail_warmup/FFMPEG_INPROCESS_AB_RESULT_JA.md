# HEIC初回サムネイル高速化：FFmpeg と libde265 の**内部復号A/B**（2026-10-21）

## 結論
**固定の人工4032×3024 HEICでは、FFmpegに大幅な速度上の利点なし。CPU版FFmpegへの置き換えは不採用。** MSIは一切作成・インストールしていない。実機v0.3.0/v0.4.0を変更する必要はない。

- 成功した実行：[GitHub Actions 39596523347](https://github.com/taku077/wic_heic/actions/runs/39596523347)
- 実験ブランチ：`experiment/hevc-ffmpeg-libheif-inprocess`
- Windows Server 2022の同一ランナー・**同一libheifライブラリ内部**で、C APIの`heif_decoding_options::decoder_id`を`libde265`／`ffmpeg`に切り替えて試験。FFmpegプラグインはlibde265より優先度が低いため、**明示的にdecoder_idを指定**しない比較は無効。
- 人工HEIC 1枚、フル解像度RGB24復号、スレッド設定2、ABBA順で各4回。両方式ともプロセス起動・Shellキャッシュを除外。
- `libde265`中央値 **404.017ms**、CPU中央値 **750.000ms**。
- `ffmpeg`中央値 **410.171ms**、CPU中央値 **757.812ms**。差は**FFmpegが約1.52%遅い**。
- RGB24出力は全36,578,304バイト（4032×3024×3）にわたって**完全一致**：FNV-1a 64bit = `F43EEC26C78BB08C`、MAE 0、最大誤差0。
- 入力人工HEICのSHA256は不変。**高画質の実写真・10bit/HDR・アルファ・向き・ICC・4枚同時処理は未検証**なので、この1枚の測定から全ケースの優劣は断定しない。
- 比較を可能にした実験実装：`wictest/isolated_shell/HeifBackendAB.cpp`／`HeifBackendFFmpegAB.vcxproj`。
- FFmpegのWindows用LGPL shared SDK（FFmpeg 8.1系列）はGitHubリリースのzipをSHA256で検証後、CI一時フォルダに展開してMSVC用インポートlibを生成。固定SHA256：`3A584C6017CD9C43716D13BB244E77CD7C89A71E1395BB244F7751A405E3B3FC`。
- ffmpeg/avcodecとlibde265をともに扱える静的libheifのビルドが成功。WindowsのWICレジストリ登録・MSI配布・元画像上書きはなし。

## CLI方式との区別
[39593895480](https://github.com/taku077/wic_heic/actions/runs/39593895480) のFFmpeg CLI計測では1/2/4スレッドの中央値611/621/651ms。[39595325337](https://github.com/taku077/wic_heic/actions/runs/39595325337)では約470/459/452ms。**別ランナーやプロセス起動の変動が大きい**ため、CLIの数値だけでエンジン性能を判断してはいけない。今回の本命である同一プロセス・同一画像の直接A/Bでほぼ互角となった。

## 今後の開発優先順
1. CPUバックエンドの小変更（AVX2、ソフトウェアFFmpeg）はいずれも大幅改善なし。繰り返しこの方向でMSIを作らない。
2. 無料・ローカル完結かつ実機のCore i5第7世代でのハードウェアHEVCデコード経路（Intel Quick Sync、D3D11VA、Media Foundation）の技術的成立性を調べる。ただしユーザー環境はIntel QSV **decode可／encode不可** と報告済みで、decode可の意味・対象コーデックやHEIC静止画の投入可否は未確認。
3. HEICの`hvcC`／NAL単位、HEIF主画像の変換・色・向き・HDRを維持できるか、GPUデコード導入時の初期化遅延/CPU負荷/メモリ/キャッシュを検証。HEVC拡張の購入を前提にしない。
4. 埋め込みサムネイルが十分大きい写真だけは直接抽出する枝が残るが、既存7例中256px表示に適用できるものはゼロだったため補助手段に留める。
5. Windows 10実機で有意な成果が見込めるまで**MSIの提示をしない**。

## 安全性
GitHub Actions内だけでテストし、ユーザー写真のアップロード、元画像再エンコード、関連付けの変更、ShellグローバルCOM登録をしていない。ソースは実験ブランチに隔離している。
