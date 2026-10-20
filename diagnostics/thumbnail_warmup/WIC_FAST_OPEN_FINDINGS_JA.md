# Windows WIC HEIC 初回サムネイル高速化：根拠と採用判断

対象：taku077/wic_heic、ブランチ diagnostic/photos-wic-calltrace。Windows Server 2022（GitHub Actions）、人工HEIC10枚相当の試行をモードごとに10回、CI反復5回。

## 実測

| 対象 | Windows CIでの値（中央値の範囲） |
|---|---:|
| WIC CreateDecoderFromFilename によるHEIC開封 | 約135～140ms |
| HEIC WICデコーダーを直接指定して初期化 | 約0.38～0.55ms |
| 256pxサムネイル完成まで（自動選択） | 約207～233ms |
| 256pxサムネイル完成まで（直接指定） | 約67～94ms |

直接指定と自動選択の両方式は同じ当該HEICデコーダーCLSIDを選択（10/10回、各反復）。完成サムネイルの画素値も一致（pixels_equal=1）。
この試験の速度改善は約2.5～3.1倍（完成までの時間）であって、HEVC復号速度そのものが250倍になったのではない。

## 棄却した実験

HEICをlibheifへメモリ経由で渡す方式も、ストリーム読み取り方式と同じ人工HEICで10回ずつ比較した。開封時間は約133ms前後で双方ほぼ同一、画素一致。追加メモリに見合う効果がなく、既定経路に採用しない。試験用のメモリ読込オプションも実験後にコードから取り除いた。

## 根本原因の切り分け

1. WIC自動判定経路の負荷が大きい（同じ当該デコーダーの直接初期化では0.5ms前後）。
2. 元HEICのHEVC復号・縮小にも時間が必要で、完全なJPEG同等速度には未達。
3. Windows WICのArbitrationPriorityは小さい値ほど優先度が高い。既存の自作デコーダーは0であり、この数値を下げるだけで解決する案はない。
4. エクスプローラーのサムネイル抽出では、現在の汎用Shell/WIC経路を使う可能性があるが、CI上のWIC単体比較はWindows 10でのShell実測の代わりにはならない。

## 次の開発課題

専用のIThumbnailProvider（必要に応じてIInitializeWithStream）でHEICデコーダーを直接利用できるか、**既存のShellハンドラーを変更しない独立テスト**から着手する。人工HEICで生成結果の画素一致、10bit原本保全、EXIF不変、CPU/メモリ/ディスク負荷を検証する。
デフォルト関連付けの変更、HEIC Managerとの競合が発生する登録、常駐の設定は行わない。導入が必要になった場合、登録前の状態の保存・ロールバック・完全なアンインストール試験が必須。

最終目標はHEICのJPEG同等：初回未キャッシュ表示、写真の回転・保存、無再圧縮、10bit、EXIF/ICC/XMP/HDRの完全保持。初回サムネイル改善だけで完成とはしない。

参考：Microsoft Learn『How the Windows Imaging Component works』『General Registry Entries』。