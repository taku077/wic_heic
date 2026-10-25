# Windows 10 HEIC / 標準フォト向け 1.1.24 技術報告（2026-10-25）

**提出期限：2026-10-27（火）20:00 JST。1.1.21～1.1.23では要求された最終提出を済ませた扱いにしない。** この文書と人工CIは、Windows 10標準フォトの実機での表示崩れ解消を証明しない。画像本体・個人の写真・識別子・プライベートパスはGitHubに含めない。

## 1. 操作条件の訂正（最優先）
ユーザー実機の v0.4 診断セッションAでは **回転ボタン4回を速く連打し、縮小も行った** が表示変形なし。セッションBは **比較的ゆっくり4回回転**で表示変形なし。通常利用では連打と関係なく伸長・細い表示枠・見切れが報告される。したがって「診断中の待機時間のおかげでバグを回避」は棄却。さらに診断の `RotationPressesRecorded` は確認画面で採取したチェックポイント数で、実際の回転ボタン回数ではない。旧解釈で複数保存ログとクリック数を比較しない。

確実な診断／通常差分は、新しい実験用パス/ファイル名、シェル関連付け経由の起動、WICの追加ログ・読み取り、キャッシュの新規状態、そして利用画像の内部構造。Windowsの`System.ThumbnailCacheId`はファイル名・ボリューム・変更データで変わる（Microsoft公式）。この事実をWindows Photos固有キャッシュの機能と取り違えない。

## 2. 画像が引き延ばされたり細長くなるコード上の危険
`wic_heic/HeicBitmapFrameDecode.cpp::CopyPixels` は従来：
- `m_Handle.get_width()/get_height()`を画素メモリの寸法だと仮定し、`DecodeImage()`後の **`m_Image.get_width()/get_height()` の一致を検証しない**。HEIFの`grid`/`irot`/`clap`/タイル余白が絡む際、宣言寸法と復号寸法がずれると行の始まり位置が誤り、画像が伸長・ずれ・見切れる形になり得る。
- 呼び出し元の`WICRect`に負値や画像外の範囲が指定されても、ポインタへそのまま加算。
- `cbStride`が要求行バイト数未満でも拒否しておらず、`rc.Height*cbStride`も32-bit演算のまま扱っていた。

**修正：** 元画像/復号画素の縦横を両方検査して不一致なら`WINCODEC_ERR_BADIMAGE`を返す。矩形の負値・画像外の座標・無効幅高さ、コピー先行幅・バッファ量を64-bitで事前検証する。拒否理由`DecoderCopyPixelsRejected`を数値のみでトレース。元画像の上書きは一切しない。

技術的な裏付け：
- Microsoft: `IWICBitmapSource::CopyPixels` が矩形・stride・buffer size を受けて実際の画素を生成する (https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapsource-copypixels)
- Microsoft: `IWICBitmapSourceTransform`、正規のscale→crop→rotate順、コピー先の変換後stride (https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapsourcetransform)
- libheif Wiki: 回転されたタイルは一部画像領域外に位置し、余白/オフセットによる境界の調整が必要 (https://github.com/strukturag/libheif/wiki/Reading-and-Writing-Tiled-Images)
- libheif #1820: handleの宣言寸法と実際のdecoder出力寸法が異なるために、行コピーによるシアー・見切れ症状が発生した実例 (https://github.com/strukturag/libheif/issues/1820)
同じ機構が利用者のWindows Photosのバグを起こしたという直接の証拠は**まだない**。

## 3. 連続回転時の容量増加を減らすコード上の対策
`PhotosPureRotationBridge.h`: 標準フォトはWICに無再圧縮変換のフラグではなく`opaque-raster`を渡すため、元HEIFと出力RGB全画素が **90/180/270度のいずれか1種類**に一致する場合に限り既存HEIFメタ回転を実行。Exif/ICC/HDR/補助画像の安全な構造を保つ。
- 従来のCW90だけでなく180/270も実装。加工済みラスタを勝手に「元写真と同じ」とみなさない。対象画素の完全一致に限定。
- 保存試行に一致しなかった場合は、元ソースを一度で消費せず、時間制限内の後続の保存に再利用可能。成功時のみ候補を破棄。写真が混在して曖昧なときは安全に通常再エンコードへ戻す。
- 縮小/トリミング/明るさ変更などの画素加工は同一画素ではなくなるので従来保存を保持。画素が完全に同じメタデータのみの編集の扱いには制約が残り、純粋な回転以外は実機未保証。
- 両経路の既存フレームストリームの位置検査、元データへの非上書き、ICC比較を保持。

## 4. 人工HEIC Windows CI
- CI #40184176310 **SUCCESS**: `WIC_DECODER_ROTATED_RECT_BOUNDS_AND_STRIDE_PASS`、WICフル画像との矩形一致、無効矩形10例拒否、出力行パディング不変。
- CI #40184565711 **SUCCESS**: `WIC_PHOTOS_OPAQUE_CW90_LOSSLESS_EQUAL_PASS`、`CW180`、`CW270`、それぞれ**10,597バイトの期待HEIF完全一致**。`WIC_PHOTOS_OPAQUE_ONE_PIXEL_EDIT_SAFE_REENCODE_PASS`。MSI導入・アンインストール後の登録状態復元もSUCCESS。
- GitHub人工CIはWindows Server 2022を使用。ユーザーWindows10標準フォトのUI描画、キャッシュ、連続回転は未検証。Photosがすでに別ルートで描画を行う場合は効果が限定的。

## 5. 新版のリリースと残る課題
MSIバージョン **1.1.24**。新バージョンの成功CIに基づく候補のみ提出する。旧1.1.18は正常版扱いしない。HEIC Managerとは別物。
- 本当のWindows 10 Photos表示伸長の解消は未検証。
- 自動変換は時計回り90、180、270。4回目の360度/0度の自動無再圧縮選択は未対応。メタデータのみの編集を区別できない場合は安全な保守的選択を維持。
- `set_lossy_quality(92)`は無再圧縮不成立時にHEVCサイズを大幅に増加させ得る。容量増大が完全に防げたという宣言はしない。
- Exif/XMPが取得不能なラスタ再エンコードでは欠落が残る。全画像での情報完全保持は未完成。
- 1.1.23→1.1.24や1.1.21→1.1.24のユーザーWindows10上書き導入／ロールバックは保証しない。過去Explorer黒画面問題を踏まえ復旧キットの適用条件を事前確認、明示的承認があるまでインストールしない。

## 6. 1.1.24候補の再現可能な出荷ゲート（Windows CI）
コード固定対象：`HeicBitmapFrameDecode.cpp`の境界・実寸検査、`PhotosPureRotationBridge.h`の90/180/270一致、`HeicBitmapEncoder.cpp`の正しい保存分岐、`wictest/WICDecoderRectProbe.h`と`wictest/WICPhotosOpaqueRotationProbe.h`の人工回帰。
- Actions #40184176310 **SUCCESS**：回転画素範囲検査10例。
- Actions #40184565711 **SUCCESS**：CW90/CW180/CW270が期待HEIF全10,597バイト一致、加工1画素は再圧縮、MSI登録状態復元。
- Actions #40184899588 **SUCCESS**：1.1.24バージョンのx64 Release、WIC回帰、MSIインストール・アンインストール・登録状態復元。
- 今回のコミットを明示的な`release(candidate)`として公開し、**再実行に成功した場合に限り**バイナリーMSI+SHA256をGitHub Actionsにアップロードする。実機フォトUIは未検証。
