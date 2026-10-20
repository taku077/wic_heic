# HEICサムネイル専用Shellハンドラー：登録経路切り分けとCI実測

## 最終目標（変更不可）
Windows 10標準Explorerとフォトにおいて、HEICをJPEG同等の速度・操作性・画質・メタデータ保持で扱う。10bit、EXIF/ICC/XMP/HDR、無再圧縮回転も必須。サムネイル試験のみでは完成としない。

## 2026-10-21の開発成果

分離した試験用COM DLL：`wictest/isolated_shell/HeicTrialThumbnail.cpp`、独立プロジェクト `HeicShellTrial.vcxproj`。`IThumbnailProvider + IInitializeWithStream`を実装し、既知のHEIC WICデコーダーを直接指定する。**実際の.heic/.heifにはShell登録しない。** 合成画像を`.wictrial`という試験専用の拡張子にコピーする。

GitHub Actions（Windows Server 2022）での結果：

| 登録/試験 | 結果 |
|---|---|
| COM DLLの直接生成（HKCU） | 成功（HRESULT 0） |
| Shellから取得（HKCU、Both） | `0x80040154` |
| Shellから取得（HKCU、Apartment） | `0x80040154` |
| Shellから取得（HKLM、Apartment） | 成功 |
| テスト用DLLへの呼び出し | 成功・10/10回 |
| 10枚の新規生成 | 10/10回 |
| 10枚のShellキャッシュのみ再取得 | 10/10回成功、再生成0回 |
| Shell APIの生成時間の中央値 | 89.09ms |
| 生成時間の90パーセンタイル目安 | 108.23ms |
| 同一人工HEIC原本・コピーのSHA256 | 不変 |
| 所有する試験用レジストリの完全削除 | 合格 |

10枚は**同一の固定人工HEICの複製**であり、異なる実写真やビット深度・画質条件をカバーしない。各画像は別の一意なファイル名で作成し、サムネイルキャッシュの初回生成を測定。

成功ログ：`https://github.com/taku077/wic_heic/actions/runs/39489030955`
単体1枚成功ログ：`https://github.com/taku077/wic_heic/actions/runs/39488542010`

## 技術的な切り分け

Shell取得失敗時の直接COM生成は成功していたが、`BHID_ThumbnailHandler`による取得と`IThumbnailCache`抽出はいずれも`0x80040154`。クラス登録先を一時的にHKLMへ変更すると、同一DLLがShellから実際に呼び出された。これは**CIランナーの構成における確認結果**であり、Windows 10すべてでHKCU登録が不可能と断言するものではない。

Microsoft Shell拡張の推奨に従い、独立試験用DLLの`ThreadingModel`は`Apartment`を使用する。

## 安全性

- HKLM登録は使い捨てGitHub ActionsのWindowsランナー上に限定。スクリプトは`GITHUB_ACTIONS=true`等がなければMachineモードを拒否する。
- 登録前にCLSIDs、ProgID、拡張子の存在を検査し、既存登録があれば停止。`finally`内で試験用キーのみ解除する。
- テスト中、HEIC Managerの登録・関連付け・ソフトウェア実体は変更しない。Windows 10ユーザー実機への導入・インストール・自動常駐はしていない。
- テスト用COM DLLは`DllRegisterServer`と`DllUnregisterServer`を意図的に持たない。CIの専用スクリプトが登録を制御する。

## 未合格・次に必要な検証

1. Windows 10 Pro 22H2実機でShell登録・Explorer上の初回未キャッシュ表示速度とJPEG対照。
2. 異なる人工HEIC（サイズ・8bit/10bit・向き・ICC/EXIF・アルファ）の完全画素一致とメタデータ原本不変。
3. 大量画像、長時間表示、外付けHDD使用時のCPU/メモリ/GDIオブジェクト/ディスクI/Oの負荷とリーク。
4. Shell登録と旧ハンドラーの共存・登録解除・元状態の完全な復元（実機導入前の必須条件）。
5. Windowsフォトの回転・保存、10bit・EXIF/ICC/XMP/HDR保持、無再圧縮処理の別系統試験。

現状判定：**隔離Windows CIでの実Shell連携と10枚キャッシュ試験は合格。Windows 10実機のJPEG同等速度は未検証。**