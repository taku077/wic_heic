# WicHeic 1.1.27 x64 / Windows 10 Photos: 複数フレーム回転保存の修正候補

**状態:** Windows 10 Photos実機では1.1.27は未確認。ユーザーPCで確認済みの1.1.26を自動交換しない。**提出期限 2026-10-27（火）20:00 JST** は不変。HEIC Managerには非干渉。

## 実機で確認された再現条件

- 1.1.26でユーザーは人工\`fixture-irot3-exif6-noaux.heic\`の回転保存失敗を報告。ユーザー私的写真ではなく公開CI fixtureのみを使用。
- このHEICは名称\`noaux\`だがトップレベル画像として認識される画像2個を含む。追加の画像はHEIFの一般的な\`auxl\`ではなく特殊な参照\`zzzz\`を持つ。
- WICの匿名化診断では5回の保存試行それぞれで最初の\`WriteSourceRaster\`と\`FrameCommit\`が通過。2枚目の\`WriteSource\`が\`self-wic-original\`、\`DecoderFrameGetSize source=auxiliary\`の直後で停止し、\`EncoderCommitEnter=0\`。
- 追加画像をトップレベルに列挙しないようにした1バイト差の比較用人工HEICは、同じ実機・同じ1.1.26で回転保存に成功。保存されたHEIFは全長10,597バイトで変更は\`irot\`とExif方向の2バイトのみ。HEVC圧縮データは維持。
- 上記は特殊な人工HEICの条件であり、一般的な単一写真や正規のマルチフレームHEIFすべてを代表しない。

## 原因をCIで赤テスト化

- \`wictest/WICPhotosOpaqueRotationProbe.h::RunTwoFramePhotosSave\`は実機と同じ2枚のWICフレームの\`CreateNewFrame\`→\`WriteSource\`→\`FrameCommit\`→\`EncoderCommit\`を公開人工HEICだけで再現。
- 修正前Windows Actions [#40219776843](https://github.com/taku077/wic_heic/actions/runs/40219776843)では、2枚目\`WriteSource\`が\`0x88982F81\`で失敗しており、1.1.26実機の症状と一致。
- 従来CIが合格していたのは単一WICフレームを模した保存経路のみであり、Photosが2フレームを列挙してコピーするパターンをテストしていなかったため。

## 1.1.27の修正（出自を証明できる場合だけHEIF全体を再利用）

1. \`HeicSourceAccess.h\`に既存\`IHeicRawSource\`のABIを変更せず、**非主画像**であることとトップレベルindex/countを示す別の内部COMインターフェースを追加。
2. \`HeicBitmapFrameDecode\`はトップレベルの実indexを保持し、サムネイルデコーダーは対象外とする。補助画像由来の元コンテナは読み取るだけで画像やファイルへ変更を加えない。
3. \`HeicBitmapFrameEncode::WriteSource\`は元WICフレームの出自が認証できた場合、2枚目の内容を独立HEVC画像として再生成せず、元HEIF全体とindex/countだけを結び付けて保持。\`FrameCommit\`は内容を持つ状態を確認して成功。
4. \`HeicBitmapEncoder::Commit\`は二つのWICフレームが同一の元HEIFから来たと立証し、元HEIFが**ちょうど2つのトップレベル画像**を持ち、index=0が主画像・index=1が非主画像、寸法・編集条件が整合する場合だけ通過。
5. 主画像が未編集なら全バイト一致コピー。90/180/270/0度の純粋な回転なら、元HEIFと**全RGB画素が一致する厳密照合**および既存\`HeicLosslessNative::Transform\`の安全ゲートを通したHEIF全体を出力。他画像・Exif/ICC/付属画像は元のコンテナから保持。
6. 主画像の1画素でも異なる、別HEIF由来、切り抜き、編集メタデータの不整合などで同一性を立証できない場合は**無再圧縮を拒否**。元コンテナやユーザー元画像を破壊しない。

## CI追加検査

- 既存の1枚画像版の0/90/180/270度ロスレス、Exif方向の正規化、画像形式/矩形、MSIの新規インストール・削除と登録状態復元比較を継続。
- 公開2画像fixtureの**90度回転**で期待HEIFと完全バイト一致。
- 同fixtureの**1画素改変**でEncoder Commit拒否かつ出力ストリームに部分的な画像を書かないこと。
- 同fixtureの**無回転コピー**で元HEIFと全バイト一致。
- 合格とビルドが確認できても、Photos Windows 10の実機UI互換・速度・保存結果の完全保証にはならない。

## 配布・復旧ゲート

- \`Installer/Package.wxs\`のProductVersionは **1.1.27**。旧版1.1.26実機導入済みの環境へ勝手にMSI導入しない。
- \`release(candidate):\`コミットをWindows Actionsで最後まで検査し成功した場合だけ、\`WicHeic-1.1.27-x64-Win10-Photos-UNVERIFIED-RC\`としてMSIとSHA256をアップロード。
- ユーザーへの実配布はGitHubアーティファクトを取得してSHA256照合・ZIP再梱包し、ChatGPTのサンドボックスリンクで提供する方式とする。直接GitHub MSIリンクを成果物DL用に要求しない。
- ユーザーPC試験前に既存1.1.26と1.1.25のMSI、インストール済DLLハッシュ、WIC CLSID、読取専用スナップショットを保存。旧版への復旧が確実でない場合は導入を勧めない。
- **実写真・IMG_4074・HEIC Managerの設定・DLL・レジストリに手を触れない。** Windows 10標準フォト実機では使い捨ての人工HEICのみを使用。
- 既存の表示伸長・見切れ、Photos内の保存遅延、再圧縮時のExif/XMP保持の未解決事項は今回解決済みとしない。

## 参考資料

- Microsoft WIC frame/encoder Commit: https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapencoder
- Microsoft WIC CreateNewFrame: https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapencoder-createnewframe
- libheif top-level image enumeration: https://github.com/strukturag/libheif/blob/master/go/heif/heif.go
- Library of Congress HEIF items, auxiliary and HDR: https://wwws.loc.gov/preservation/digital/formats/fdd/fdd000526.shtml


## 2026-10-26 Windows 10標準フォト実機結果：1.1.27で回転保存成功（ユーザー報告）

- **ユーザーがWicHeic 1.1.27を導入し、1.1.26で保存失敗した公開人工2フレームHEICを標準フォトで回転保存できたと報告。** 新版による保存不能の改善を実機で確認。なおフォトのボタン操作、画像の最終的な画面方向については今回の添付のみでは機械判定しない。
- 保存結果と考えられる添付 `fixture-irot3-exif6-noaux(3).zip` のHEICはZIP CRC整合性正常、10,597バイト、SHA-256 `A680DF74DCFE858EFAA5C29BFB39658628D1DF1C30891759375069BD893A314B`。
- 参照元の公開人工HEICは10,597バイト、SHA-256 `D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95`。
- **両HEICの差は1バイトのみ**（offset 10589、Exif Orientationの値 `6→1`）。HEIF primary `irot=3`は不変。HEVC圧縮画像、2画像の画素データ、HEIFその他の各バイトは同一。heif-infoで両ファイルに主画像1080×1920（10-bit）と追加画像1920×1080（10-bit）の存在を確認。
- これは**無再圧縮・追加画像保持の証拠**である。ただしExif正規化のみで保存されており、回転が何度適用されたか・画面の見た目との方向一致はこの差分のみで断言しない。ユーザー報告の「保存できた」と、バイナリで立証した「Exif1バイトのみ変化」を区別する。
- Windows CI #40220970052 SUCCESS と実機の保存成功を確認。一方、一般写真の全形式対応、繰り返し回転の表示崩れ/見切れ、再圧縮時のExif/XMP保全、ファイル置換/サムネイル遅延は引き続き未解決。
- 今回の報告で新たなMSI変更・強制再インストールは行わない。個人写真のアップロード・GitHubへの取り込みは不要。
