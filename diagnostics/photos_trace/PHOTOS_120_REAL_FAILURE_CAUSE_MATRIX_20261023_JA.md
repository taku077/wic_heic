# Windows 10 フォト HEIC「HEVC拡張」警告復活：v1.1.18 vs v1.1.20 原因調査・修正記録
2026-10-23・研究ブランチ：`investigate/photos-wic-arbitration-after-120-failure`

## 実機の確定事項
- Windows 10 Pro 22H2 build19045 / Photos 2026.11090.22001.0 / HEIF Image Extension 1.2.48.0 / 有料HEVC拡張なし。
- 実機v1.1.20 (WIC Repair Test) のデコーダーとエンコーダーがx64 COMに正常登録、DLL SHA-256がMSIの期待値に一致。よって本症状は **MSIインストール失敗ではない**。
- フォトがHEICを簡易表示するが「完全な解像度にはHEVCビデオ拡張機能が必要」と警告し、編集・回転ボタンが無効。2026-10-23 19:14 JSTの本人実機結果で **v1.1.20のフォト互換はFAIL**。
- 同日19:17 JSTの復旧ZIPに含まれる7時系列JSONとMSIログで、v1.1.20アンインストール→歴史的v1.1.18 MSI再インストール→x64 decoder/encoder DLL SHA-256 `9E9D2F17038C03A5BF8840A8BD43BC4FDA19C91E947C4CD5904A8209051F93D4` 一致を確認。復元成功。元画像の撮影データやファイル内容は必要なく使用しない。
- 独立サムネイルv0.4.0は現在アンインストール済み。今回の症状の原因と推測して再インストール／削除しない。

## ソース差分から確定したこと
歴史的v1.1.18コミット `f5b66fe89fa203c89967b46399ed6af61a6161e8` と研究ブランチを比較。
- `wic_heic/dllmain.cpp` のHEICデコーダー／エンコーダーCLSID・ContainerFormat・ArbitrationPriority・ファイル拡張子は同じ。**MSIの登録が変わったから**という説明は不適切。
- `wic_heic/HeicBitmapFrameDecode.cpp` の10/12/16-bitを48/64bppへ報告する基本処理は両版で同じ。**この処理変更が1.1.20だけ編集ボタンを壊した**という説も成立しない。
- `CHeicBitmapDecoder::QueryCapability()` は異なる。歴史的v1.1.18は常に `SameEncoder|CanDecodeSomeImages|CanDecodeThumbnail` (`0x15`) を返していた。v1.1.20は実際のストリームを検査し、確認できた `CanDecodeSomeImages`／`CanEnumerateMetadata`／`CanDecodeThumbnail` のみ宣言。画像ごとに能力が異なる。
- MicrosoftのWIC仕様では、`SameEncoder` は「その画像が同じベンダーのエンコーダーで作られたと認識する」ときのみ許容。iPhone等のHEICに無条件に付けるのは不正確。旧版の一律宣言を闇雲に戻す修正はしない。必ずWIC選択とPhotos GUIの双方を分けて検証する。
- `QueryCapability` には実際のコード上のバグがあった：**主画像を認識できても、任意のEXIF一覧またはサムネイル処理で例外が起きると、外側のcatchが能力マスク全体をゼロにする**。研究ブランチ `3421e5f48c5d2de8f66bb6034be050dd019dc3d9` で主画像の確認済みビットを保持し、任意部分だけ安全に無効化するよう修正。これは実在する誤判定バグの修正であって、Photos警告消失の実機証明ではない。

## 調査・修正の切り分け
1. Win Server 2022隔離環境で、元v1.1.18の完全一致MSIと研究版WICを順にインストールし、**同一の公開人工HEIC**でWIC `QueryCapability` の出力ビットと自動デコーダー選択をA/B比較する。MSIの旧版復元を確認、利用者向けMSI成果物は作らない。GitHub Actions: `photos-wic-postfail-arbitration-ci.yml`。
2. WICデコーダーをフォトが実際にロードしているか、写真を読まずに**プロセスに読み込まれたDLLのベース名だけ**を測る `Photos_WIC_Process_ReadOnly.ps1` を開発。アプリのサンドボックス等で取得できない場合は「不明」であり、読取失敗を根拠に推測しない。
3. Microsoft公式はWindows 10標準フォトで写真にもHEIF/HEVC拡張が必要になることがあると記載：https://support.microsoft.com/ja-jp/windows/apps/photos/photos-app-video-editor-error-can-t-view-this-file-type 。フォトがWICを利用するかHEVC拡張の有効性を別に検査するかは、実機トレースなしに断定できない。
4. 旧v1.1.18で一時的に警告が消えたユーザー実機報告は維持する。しかし**旧版復元直後のGUI状態と、1.1.20導入中のGUI状態が同一条件で比較されていない**ため、今回の差分が `SameEncoder` の有無に由来するとまだ証明されていない。
5. **新しいMSIの実機導入・手動のMicrosoft登録改変・Store拡張購入・原本HEICの回転／保存をしない。** WIC実験CI合格後でもPhotosのフル解像度＋編集＋回転＋安全な保存が実機で証明されるまでは完成としない。

## 合格条件
- 合法的・仕様準拠のWIC能力宣言が維持でき、実HEIC/人工HEICの主画像が誤ってデコード不能と報告されない。
- フォトがHEVC警告なく表示・編集・回転できること（Windows 10実機）。ここが未合格なら対策は完了していない。
- 作業コピー限定の回転保存で画素形状・元HEICとEXIF/ICC/HDR等を毀損しない。プライベート画像で試さない。
- 旧WICの完全一致復旧が保証される。HEIC Manager、元写真、v0.4.0（未導入）に干渉しない。

## CI追試確定：歴史的v1.1.18と修正版の同一人工HEIC比較（2026-10-23）
[CI #39939819747](https://github.com/taku077/wic_heic/actions/runs/39939819747)：Windows Server 2022で**PASS**。元v1.1.18と研究修正版を同じCI環境で順にMSI導入し、同じ公開人工HEICをWICに読み込ませた。

| 測定 | 元v1.1.18 | 研究修正版 |
|---|---|---|
| QueryCapabilityマスク | `0x15` | `0x0C` |
| SameEncoder ビット | 1 | 0 |
| CanDecodeSomeImages | 1 | 1 |
| CanEnumerateMetadata | 0 | 1 |
| CanDecodeThumbnail（その人工HEIC） | 1（旧版の無条件申告） | 0（埋め込みなし） |
| ベンダー指定なしの WIC CreateDecoderFromFilename が自作CLSIDを選択 | **1** | **1** |

**決定的な限定事項**：少なくともこの人工HEICと隔離Windowsでは、修正版WICは登録されているだけでなくWindowsによる実際の自動選択にも成功。従って「v1.1.20はWICの自動選択が常に失敗するためフォトで編集できない」という一般的な原因説は否定された。一方、Windows 10 Photos内でのデコード・編集機能条件はこれでは測れない。**HEVC警告との因果関係は引き続き未確定**。

CIは公開人工HEICだけで実行し、元画像にもユーザーPCにもアクセスしていない。実機不合格版MSIは再配布せず、Artifact出力はゼロ（リリースゲート有効）。研究修正は `3421e5f48c5d2de8f66bb6034be050dd019dc3d9`、旧版と修正版の測定用プログラムは `wictest/WICDecoderCapabilityProbe.h` に保存した。

## 同一ユーザー実機でのA→B→Aによる重要な追試（2026-10-23 19:49 JST）
- A：v1.1.18の状態ではHEVC警告がなく、編集・回転ボタンが操作できるとユーザーが報告している（保存成功は別件、未確認）。
- B：v1.1.20へMSI更新して同じWindows10 Photosを使うとHEVC警告が復活し、編集・回転ボタンが無効化。DLL一致・COM登録成功は事前診断で確認。
- A：ユーザーが試験キットでv1.1.18へ復元。MSIとCOM/DLL復元の完全一致を診断で確認した後、**HEVC警告が消え、編集・回転が使える状態に戻った**と本人から明示報告。
- このA-B-Aにより、PC側のHEVC拡張未購入という**不変条件のみ**を原因とする説明は否定される。**WIC実装差による回帰、またはそれに伴うPhotosのコーデック選択/アプリ状態差**の可能性が強まった。
- ただし変化したのはWICの複数API実装箇所であり、QueryCapability `SameEncoder` 1→0だけが原因だと確定しない。少なくとも公開人工HEICでは旧版／修正版ともにWindows WICの自動デコーダー選択に成功した。
- **標準フォト上でJPEG同等の無劣化回転保存・EXIF/ICC/HDR保持は未検証である。** UIのボタン復活と画像保全の完成を混同しない。
- 実機には現在v1.1.18が復元されている。これを保持し、必要性が固まるまで別MSIを配布・導入しない。ユーザーの写真に触れない。v0.4.0は現時点でアンインストール済み。

## 追加A/B：編集・保存API計測で差をさらに限定（2026-10-23）
[CI #39942508191](https://github.com/taku077/wic_heic/actions/runs/39942508191)：歴史的v1.1.18と修正版で同じ公開人工HEICを使い、以下のWIC APIを読取専用で照会した。両方のMSIは使い捨てWindows Server 2022ランナーでのみ導入し、ユーザーへMSIを配布していない。

| API | 旧v1.1.18 | 修正版 |
|---|---|---|
| WICの自作デコーダー自動選択 | 成功 | 成功 |
| HEICコンテナーGUID | 同じ | 同じ |
| Decoder.GetPreview | `0x88982F81` (非対応) | 同じ |
| Decoder.GetMetadataQueryReader | `0x88982F81` (非対応) | 同じ |
| **Decoder.GetColorContexts（プロファイル0件）** | **`0x88982F81`** | **`0x00000000`** |
| **Frame.GetColorContexts（プロファイル0件）** | **`0x88982F81`** | **`0x00000000`** |
| Frame.GetMetadataQueryReader | `S_OK` | 同じ |
| WICエンコーダー自動選択・明示選択 | `S_OK` | 同じ |
| Encoder.GetMetadataQueryWriter | `0x88982F81` (非対応) | 同じ |
| Encoder.CreateNewFrame | `S_OK` | 同じ |
| FrameEncode.GetMetadataQueryWriter | `0x80004001` (`E_NOTIMPL`) | 同じ |

加えて既知の差分は `QueryCapability` が `0x15`→`0x0C`。**フォトに関係する今回観測したAPIのうち、実際に応答が異なるのは能力値と空のカラープロファイル列挙の成否**。これらのどちらがPhotos警告・編集ボタンを制御するかは未判明。

留意点：旧版の `SameEncoder` は他社のiPhone HEICに無条件で立ててよいものではない。旧版に合わせて偽装する「修正」は仕様違反となるため行わない。フレームの `GetMetadataQueryWriter` が両版で未実装であることは、**UIの復活とは別の保存品質・EXIF/ICC保持の問題**。これを理由にして旧版もJPEG品質の保存が完成したと説明しない。

実機でPhotosのHEVC警告を消すには、旧v1.1.18のA-B-A確認に加えて**本物のWindows 10標準フォトが、どのDLL・WIC APIを呼び、どのエラーで編集を制限しているか**の記録が必要。Windows ServerのCIにはPhotos 2026の実UIがないため、今回のA/Bデータだけで完成を主張しない。

## 2026-10-23 20:21 JST 旧v1.1.18が正常動作するPhotosプロセスの読み取り専用スナップショット

- 実機でWindows 10 build19045、Photos編集/回転ボタンが使える **旧WIC v1.1.18状態** で、公開人工HEICを標準フォトで開き、`Photos_WIC_Process_ReadOnly.ps1` の読み取り専用診断を実施した。
- **Photosのプロセス1件に `windowscodecs.dll` および `wic_heic.dll` が読み込まれている**（プロセスの `Modules` 列挙成功）。別のPhotosプロセス1件とApplicationFrameHostには `windowscodecs.dll` だけが観測された。DLLの存在しないプロセスでも別のタイミングに呼び出されていた可能性は排除しない。
- 同じ診断で、HKLM x64 WIC decoder/encoderの **登録先DLLのSHA-256** が両方とも歴史的v1.1.18の `9E9D2F17038C03A5BF8840A8BD43BC4FDA19C91E947C4CD5904A8209051F93D4` と一致。
- **厳密な限界**：プロセス側はモジュールの *basenameのみ* を保存し、ロードされた実ファイルのパスやSHA-256までは収集していない。したがって「Photosが `wic_heic.dll` という名のDLLをロードしていた」は直接の観測だが、「実際にロードされたファイルのハッシュが登録先とバイト単位で一致」まではこの診断だけでは断定できない。また、モジュールのロードは、個別WIC APIの呼び出し・コーデック選択・編集ボタンの内部条件を証明するものではない。
- プライバシー保護として**プロセスPIDと元JSONファイル、画像、画像パスは公開GitHubに保存していない**。個人の写真・EXIF・コンテンツへアクセスしていない。
- このデータにより「フォトが自作WIC DLLをロードする経路が存在しない」という仮説は否定された。次に必要なのは、既に正常動作中の実機WICを保護したまま **新版との機能/API差分の原因をコードと人工画像CIで削減すること**。不具合版v1.1.20を再配布・再導入しない。

Microsoft WIC公式仕様: https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapdecoder （`QueryCapability` は実ストリームに基づき各フレーム/メタデータについて正直に申告し、ストリーム位置を戻す必要がある）

## UI有効化と回転保存は別ゲート（2026-10-23 ユーザーの再明示）

ユーザーは明確に「**v1.1.18は回転は出来るがバグる**」と訂正・再確認した。以下は以後の開発・リリースで絶対に混同しない。

| 機能 | v1.1.18実機状態 | 開発合格条件 |
|---|---|---|
| フォトでHEVC警告が消える | 報告済み | v1.1.18互換を維持 |
| 編集/回転ボタンが押せる | 報告済み | 同上 |
| 回転後に正しい向きと寸法・縦横比で表示 | **不合格：歪み・表示崩れの報告** | 人工非対称格子で全4方位・4×90度一致 |
| Explorerサムネイルと標準フォトが同角度 | **不合格：角度不一致の報告** | 実GUIとWIC/Shellの照合 |
| 回転保存 | **不合格：保存エラーの報告** | 人工HEICの作業コピーの保存に成功 |
| EXIF/ICC/10bit/HDR/MakerNote/XMP保持 | **不合格または未実証：既知のEXIF喪失と再圧縮、未対応構造の存在** | JPEG同等の意味論・画素/全メタデータ同一性 |
| 実ユーザー写真安全性 | 元写真は試験禁止 | 写真を開かず人工HEICのみ、元ファイル上書き禁止 |

従って「v1.1.18正常」「正常動作版」「完成版」という説明は誤り。**v1.1.18はGUI有効化経路が実機で確認された、回転保存については既知不具合が残る比較基準**である。

## 回転保存の危険なWICピクセル経路の実装修正（研究版、実機未検証）

`wic_heic/HeicBitmapFrameEncode.cpp` の `WriteSource` で、`IWICBitmapFlipRotator` を受け取る場合、元の圧縮HEVC・EXIF・HDR・サムネイル等との関係を確認できないため、ユーザーに見えない再エンコードへ進む前に拒否するガードを追加。標準「DontCare」でも明示HEVC再圧縮でも拒否。**画像保全を優先する暫定措置であり回転保存成功の修正ではない**。

`wictest/WICDirectLosslessProbe.h` へ、公開人工HEICから作ったWIC回転ラッパーの保存拒否、出力0バイト・原本完全一致を検査する試験を追加。
`wictest/GeometryProbe.h` と `wictest/wictest.cpp` の人工画像の画素比較用経路だけは、検証目的を明示して一時WICビットマップへ画素を物質化する。**この明示的な損失許容経路を実ユーザーHEICに使わない**。

ただし`WICFormatConverter`等でラッパーがさらに包まれた場合、`IWICBitmapFlipRotator` という識別子を保持する保証はなく、上記ガード単独で全てのメタデータ破壊経路を塞ぐことは**未確認**。MSI配布・実HEIC保存は不可。さらにPhotosがHEICをどのAPIで上書きするかの実機経路を別に確認する必要がある。

## 2026-10-23：旧版v1.1.18の危険な回転保存経路を同じ人工HEICで実測

[CI #39947927210](https://github.com/taku077/wic_heic/actions/runs/39947927210) **成功**。Windows Server 2022隔離環境で、同じ公開人工HEICをWICの`IWICBitmapFlipRotator` で90度回転した画素ソースとして渡し、`CHeicBitmapFrameEncode::WriteSource` の返答を比較した。メモリ上の`IStream`のみ、`Frame.Commit`や`Encoder.Commit`は呼ばず、**ファイルを作成・編集しない**比較。

| WriteSource設定 | 過去実機とハッシュ一致の旧v1.1.18 | 研究版（回転メタデータ喪失ガード追加） |
|---|---|---|
| HEVC compression = DontCare（既定） | `S_OK = 0x00000000` **受付** | `WINCODEC_ERR_UNSUPPORTEDOPERATION = 0x88982F81` **拒否** |
| HEVC compression = HEVC（明示） | `S_OK = 0x00000000` **受付** | `WINCODEC_ERR_UNSUPPORTEDOPERATION = 0x88982F81` **拒否** |

**確定したバグの実装経路**：旧v1.1.18は、元HEICのExif/ICC/HDR/サムネイルなどを取り出せないWIC画素回転ラッパーからの画像書き込みを成功として受理する。このAPIは元HEICの正確な圧縮データ／メタデータを引き継がず、非可逆再エンコードや付帯情報消失につながり得る。**実フォトUIがこの経路を使用したかはまだ観測していない**ので、過去の全保存不具合の唯一の根本原因であるとは断定しない。

**研究版の進歩と制約**：当該経路の`WriteSource`を明示拒否し、事前の実HEICコンテンツを自動的に画素再圧縮して壊さない。人工HEICの原本ハッシュ維持・出力0バイト・WIC試験・幾何学比較はCI合格。ただし、追加の`WICFormatConverter`やApp側の早期ファイル切詰めはこのガードだけでは防げない。**編集が成功する修正ではなく、失敗時の破損軽減**に留まる。新しいMSIの実機導入は不可。

古いv1.1.18を「正常動作版」等と誤認してはならない。**GUIの編集/回転操作の有効化のみの比較基準**で、回転結果の寸法、方向、保存成否、EXIF/ICCその他の維持は依然不合格。

## 無劣化回転の実装上の境界：WICの元画像情報

Microsoft公式 `IWICBitmapFlipRotator` は `IWICBitmapSource` を継承し、独自の追加公開APIは `Initialize(source,transform)` のみで、元の `IWICBitmapSource` を呼び出し側が後から取得する公開メソッドを持たない。従って、汎用FlipRotatorの画素だけがHEICエンコーダーへ渡された場合、エンコーダーは元HEVC・EXIF・ICC・MakerNote・HDR・サムネイルの整合関係を取得できず、**「画素一致」だけで非劣化のコンテナー回転を保証することは不可能**。公式: https://learn.microsoft.com/en-us/windows/desktop/api/wincodec/nn-wincodec-iwicbitmapfliprotator

公式 `IWICBitmapSourceTransform` はデコーダー側で回転／拡縮／色形式変換を直接処理できる高速化APIだが、WICラッパーから外部エンコーダーへHEIC元データの出所を提供するAPIではない。**デコーダーでこのAPIを実装するだけでは、Photosに任意のメタデータ保存機能が生まれない**。公式: https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nn-wincodec-iwicbitmapsourcetransform

堅牢な実装の必要条件：`IHeicRawSource` を保持する**元の自作HEICデコードフレーム**と、`IPropertyBag2::BitmapTransform` の**正式な回転角指定**が、同一のエンコーダー処理で明示的に渡されること。既存の研究版ではこの条件下のサポート済み人工HEICで `HeicLosslessNative::Transform` を利用し、再圧縮せずHEIFコンテナーの `irot` とEXIF整合値を変更する。一方、サムネイル・HDR関連物を同時に正しい向きへ更新できない特殊な画像は安全に拒否する。標準Photos GUIがこの公式角度指定と元フレームの組合せを実際に渡すかどうかは未確認。

2026-10-23以降、**既知の制限**として、WICのFlipRotator、Scaler、Clipper、FormatConverter等の加工後ソースから元のHEICメタデータの完全性を推定して自動再エンコードしない。標準Photosが独自内部経路で回転する場合は別途、Windows 10実機・人工HEICに限定して通過APIの証拠を集めること。

## 4回90度連続のWIC保存・再読込（合格した範囲）
[CI #39949604806](https://github.com/taku077/wic_heic/actions/runs/39949604806) **成功**：人工HEICを元に `IWICBitmapDecoder` → `IWICBitmapEncoder` → `IPropertyBag2(BitmapTransform=90,HeifCompressionMethod=NoCompression)` → 新しいメモリ内`IStream` を4回連続実行し、各回のHEICコンテナー長は10,597バイトのまま。WICで再読込する寸法は `1920×1080 → 1080×1920 → 1920×1080 → 1080×1920` と交互に変化。HEICバイト列は各回で`HeicLosslessNative::Transform`の期待結果と一致、元のメモリ入力は不変、EXIF Orientationは矛盾を除去して正規化。

`Exif=6`と`irot=3`を併用した人工HEICは、1回目にEXIF Orientationが1へ正規化されるため、4回後のファイル全バイトは**元の曖昧なOrientationのバイト列とは異なり得る**。そのため「4回回して元画像と全バイト一致すべき」とは判定しない。フル解像度の復号画素が4回後に等しいかは別の独立試験を追加中。

このCIが証明するのは自作WICへの**元HEICフレーム＋明示回転指定**の条件だけであり、Windows 10標準Photosのボタンがこの同じ経路を使うことも、実GUIで回転・保存が成功することも証明しない。新しいMSIは作成・配布しない。

## 4回90度の全画素再検証（2026-10-23）

[CI #39950063821](https://github.com/taku077/wic_heic/actions/runs/39950063821) **成功**。前述のDirect WICの4回連続メモリHEIC回転に追加して、元の人工HEICと4回回転した結果を**別々にWICでデコード**し、RGB24形式の全画素を配列比較。

- 幅 `1080`、高さ `1920`（両側一致）。
- 元のRGBバイト数 `6,220,800`、回転4回後も `6,220,800`。
- **全6,220,800バイトの完全一致**：`WIC_FOUR_TURNS_EXACT_RGB_PIXEL_IDENTITY_PASS`。
- 出力はメモリ内ストリームのみに保持。ユーザー画像、ユーザーPCのファイル、レジストリは使用していない。人工HEICの元バイト列も一切変更しない。
- 元HEICの `Exif Orientation=6` は正規化されるため、「画像とHEVCが同一」と「全コンテナーバイト列が完全一致」は区別する。

**検証対象の明確な境界**：WICの`IPropertyBag2`経由で`BitmapTransform`が明示され、入力が`IHeicRawSource`を持つ元の自作デコードフレームである場合のみ。本試験は標準Photos 2026の回転ボタンや保存機能の正常動作を証明していない。ヘッダ/メタ構成の異なるHDR・埋め込みサムネイルを含むHEICは別途安全ゲートが必要。実機でv1.1.18を完成・正常版とみなさない。新MSIは未配布。

## Microsoft公式WIC WriteSource省略規約への修正（2026-10-23）

公式一次資料 https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframeencode-writesource によると、`IWICBitmapFrameEncode::WriteSource`は、`SetSize`未呼出時には入力`IWICBitmapSource`あるいは指定`WICRect`の寸法を採用し、`SetPixelFormat`未呼出時には入力画像の画素形式を採用しなければならない。Microsoft自身のHEIFサンプル https://learn.microsoft.com/en-us/windows/win32/wic/heif-codec の `RotateAndCropSource` もこれらの呼び出しを省略している。

**再現（修正前）**：[CI #39951239948](https://github.com/taku077/wic_heic/actions/runs/39951239948) にて、公開人工HEICの無回転・`SetSize`および`SetPixelFormat`省略の正規`WriteSource`は `0x88982F81` で不合格。この失敗は、Photosが実際にその保存経路を使用している証明ではないが、WIC公開仕様との実在する非互換の証拠。

**修正**：`wic_heic/HeicBitmapFrameEncode.cpp` の`WriteSource`で不足している出力寸法・画素形式のみを入力ソースの`GetSize`/`GetPixelFormat`から安全に設定する。90°/270°の出力寸法は入れ替え、`WICRect`指定は事前に範囲検証。すでに`SetSize`や`SetPixelFormat`が明示された場合は尊重する。ソースの出所を推測せず、HEICロスレスコピー／角度だけを変更するメタデータ保持経路、圧縮不可の拒否条件を温存する。

**修正後**：[CI #39951609128](https://github.com/taku077/wic_heic/actions/runs/39951609128) **全工程成功**。一時的なメモリ`IStream`のみ使用した公式形式の`WriteSource`で、`SetSize`/`SetPixelFormat`なしの無回転コピーと90°ロスレス回転双方が成功し、出力コンテナー全バイトが期待値に一致（10,597バイト）。4回連続回転のRGB全画素一致、危険なラスター回転の拒否、旧版とのA/B API試験も維持。

**次の検証**：EXIF/ICC/サムネイル等を含む異なる人工HEICでも、省略形の`WriteSource`が全バイト保持するか確認。標準フォト実UIの編集・回転・保存とHEVC警告は引き続き **未合格**。安全な復元キットとGUI実機検証が完了するまで、新版MSIを配布しない。v1.1.18も画像変形・保存不具合がある未完成版のまま。

## 省略形WriteSourceでEXIF/ICC/関連画像の全バイト保持を確認（2026-10-23）

[GitHub Actions #39952256445](https://github.com/taku077/wic_heic/actions/runs/39952256445) **PASS**。Windows Server 2022の隔離WICを利用し、Microsoft WICの公式と同様に `SetSize`、`SetPixelFormat`、`SetResolution` を**省略**した `WriteSource`→`Frame.Commit`→`Encoder.Commit` を、メモリ`IStream`上で実行。

- 人工HEIC（EXIF/irot, no auxiliary）無回転コピー：**10,597バイト完全一致**。
- 同人工HEIC 90°ロスレス回転：**10,597バイト、ネイティブ回転の期待値と完全一致**。
- 人工HEIC（EXIFと関連データあり）無回転コピー：**10,597バイト完全一致**。
- 人工HEIC（ICCプロファイルあり）無回転コピー：**3,740バイト完全一致**。
- 4回90°回転後RGB全画素 `6,220,800` バイト完全一致と危険な画像ラスター回転の拒否も、同じワークフローで再合格。

この結果は **WICの正規的な保存方法との適合性、および対応する合成HEICの完全コンテナーコピー** を裏付ける。あくまで **Windows ServerのWIC単体試験**であり、ユーザーのWindows 10 Photos 2026 UI、HEVC未購入環境の編集ボタン、Photos独自の保存経路は未検証。旧v1.1.18のUIのみの合格と回転保存不具合を混同しない。

リリース条件：Photos GUI警告解除・編集ボタン・人工HEICコピーの回転・安全な保存と再読込・Exif/ICC/サムネイル整合をWindows 10実機で証明し、旧MSI復元条件を再度確定。**未達のためMSI配布は保留する。**

## ICC変更要求が無劣化保存で無視されるバグの再現と修正（2026-10-23）

**実在する不具合を赤→緑で再現・修正した**。以前の`WriteSource`では、以下の要求をエラーにせず`S_OK`で受理した。元HEICの色プロファイルや圧縮データをそのまま返すため、アプリから見ると保存できたように見えて**要求したICC削除・置換だけが反映されない**危険があった。

- 元ICCを含む人工HEICに対し、エンコーダー側で`SetColorContexts(0,nullptr)`を指定し無回転コピー。修正前`S_OK=0x00000000`で無視：[CI #39954064206](https://github.com/taku077/wic_heic/actions/runs/39954064206) **再現成功／テストは意図どおりFAIL**。
- 元ICCを含む人工HEICに対し、フレーム側で`SetColorContexts(0,nullptr)`を指定し無回転コピー。
- ICC無し・回転対応の人工HEICに対し、別人工HEICから取得したICCをフレーム側で指定し、90°の`BitmapTransform`を要求。

**修正内容**：`wic_heic/HeicBitmapFrameEncode.h/.cpp`と`HeicBitmapEncoder.h/.cpp`の4ファイルをコミット`53c2d6a7ae42a33b7ad96109c7d9ca34b41a0c8b`で修正。単なる未設定と明示`SetColorContexts(0)`を区別する`m_IccExplicitlySet`フラグを追加。元HEICデコーダーフレームの`GetColorContexts`から取得したICCと、指定ICCを**バイト単位で照合**。確認できない・要求が異なる場合は`WINCODEC_ERR_UNSUPPORTEDOPERATION`を返し、`m_LosslessReady`と出力ストリームを変更しない。これをデフォルトの元HEIC完全コピー経路だけでなく、`BitmapTransform`を使用するロスレス回転経路にも適用した。

**[修正後CI #39954599445](https://github.com/taku077/wic_heic/actions/runs/39954599445) 全工程PASS**。

| 人工HEIC ICC条件 | 旧動作 | 修正後 |
|---|---|---|
| Encoder側で明示的ICC削除 | 成功と誤報 | `0x88982F81` 安全拒否 |
| Frame側で明示的ICC削除 | 成功と誤報するリスク | `0x88982F81` 安全拒否 |
| ロスレス90°回転中に別ICCを指定 | 指定ICCを無視するリスク | `0x88982F81` 安全拒否 |
| 元と完全に同じICCを明示指定 | 完全コピー成功 | 完全コピー成功（3,740バイト一致） |
| ICC変更指定なし | 元HEICを完全に保持 | 全バイト保持を継続 |
| HEIC 4×90°ロスレス・復号RGB比較 | 合格 | RGB 6,220,800バイト完全一致を継続 |

**留意**：この修正は「ICC編集機能を実装した」わけではない。元HEICのICCを勝手に破棄するのではなく、実装できない変更要求を安全に拒否する。元ソースが別の`IWICBitmapSource`ラッパーを通過して出自が消えた場合の安全性は別途検証が必要。あわせてPhotos GUIのHEVC警告解除／編集・回転ボタン／保存成否は未解決。v1.1.18もこれらの保存要件を満たさない既知不具合版。MSIは配布しない。

## 明示したWIC画素形式がロスレス保存で無視されるバグ（2026-10-23）

HEICに対して`IWICBitmapFrameEncode::SetPixelFormat`で元WICデコーダーが返す画素形式と異なるフォーマット（試験ではRGBA等）を**明示指定**しても、ロスレスHEIC`WriteSource`が元のHEVC圧縮内容をそのままコピーし`S_OK`を返していた。これでは、元の色チャネル構成・ビット深度を変えたい呼び出し側に、変更に成功したと誤解させる。

[CI #39955364781](https://github.com/taku077/wic_heic/actions/runs/39955364781) の人工HEICメモリIStream試験で、**無回転`WriteSource`が`0x00000000`を返して誤成功となる現象を再現**し、意図した赤テストとしてFAILした。

修正コミット：`e017ed871635afbd1dd1bb07f5e95c5bb16057d5`。次の仕様を`HeicBitmapFrameEncode`に実装した。
- 依頼者からの明示的な`SetPixelFormat`と、`WriteSource`内でWIC規約に従い自動設定した画素形式とを`m_PixelFormatExplicitlySet`で区別する。
- 元のHEIC圧縮データを直接維持する経路で、**明示した画素形式がソースWICフレームのものと違う場合**、変更要求を隠蔽して成功を返さず、`WINCODEC_ERR_UNSUPPORTEDOPERATION`を返す。通常の再圧縮エンコーダーの意図的な画素形式変更まで禁止するものではない。
- 未指定の場合は既存の正規`WriteSource`自動設定を維持する。元データへの入力書き込みは行わない。ICC差分・EXIF・HDR保護も維持する。

**[修正後CI #39956148293](https://github.com/taku077/wic_heic/actions/runs/39956148293) 全工程PASS**。
- 異なるWICピクセル形式の明示要求・無回転：`0x88982F81`、出力0バイト。
- 同要求・90°ロスレス回転：`0x88982F81`、出力0バイト。
- `SetSize`/`SetPixelFormat`省略の正常な無回転コピー・90°回転：PASS。
- EXIF/ICCを含む人工HEICのコンテナー全バイト保持、ICC不一致の拒否、4回90°回転とRGB全6,220,800バイト一致、旧v1.1.18とのA/B比較：再合格。

**注意**：これらは人工HEICの自作WICエンコーダー試験であり、Photos 2026の編集UI・HEVC警告・保存の実機障害が完全解決したことを意味しない。v1.1.18を正常完成版と扱わない。標準フォトの実機ゲートに未到達のため、MSI発行を保留する。

## 追加実証：WICのラッパーに隠れたHEIC回転画素の誤受理（2026-10-23、今回の引継ぎ後）

- **再現（赤）**：コミット `f7e5003ea3b0112ff0efe4fc5f44149d0e986add`、[Actions #39959369514](https://github.com/taku077/wic_heic/actions/runs/39959369514) は意図どおり FAIL。人工HEICのフレームを90° WIC FlipRotatorで回転し、その出力を `IWICFormatConverter` に包んで `WriteSource` に渡すと、メタデータの出所が見えなくなったにもかかわらず **`S_OK (0x00000000)`** で受理した。旧実装が認識できた直接FlipRotatorだけのガードでは不十分だったことを具体的に再現した。
- **修正（緑）**：コミット `4a349127b3e4b1db6182764b6562bb92c7d2fca8`、[Actions #39959780026](https://github.com/taku077/wic_heic/actions/runs/39959780026) **全工程成功**。`HeicBitmapFrameEncode.cpp` の安全拒否対象へ `IWICFormatConverter`、`IWICBitmapScaler`、`IWICBitmapClipper` を追加。`IWICBitmapFlipRotator` ガードも保持し、既定(DontCare)と明示HEVCの双方で拒否する。元の `IHeicRawSource` の検証済み無再圧縮経路は変更しない。
- `wictest/WICDirectLosslessProbe.h` の人工HEICで 3 ラッパー×2圧縮条件=**6/6 件**、`WriteSource=0x88982F81`、出力メモリストリームは0バイト、入力人工ファイルのバイト列不変。`WIC_WRAPPED_RASTER_ROTATION_METADATA_LOSS_GUARD_PASS` を確認。
- 同じ緑CIで `WIC_FOUR_TURNS_EXACT_RGB_PIXEL_IDENTITY_PASS` (6,220,800バイト)、`WIC_CANONICAL_IMPLICIT_EXIF_ICC_AUXILIARY_BYTE_PRESERVATION_PASS`、`WIC_LOSSLESS_EXPLICIT_ICC_REMOVAL_AND_REPLACEMENT_GUARD_PASS`、`WIC_LOSSLESS_EXPLICIT_PIXEL_FORMAT_CHANGE_GUARD_PASS` を再確認。
- **限界**：これは既知の3種類のラッパー越しの画素誤受理を塞いだものであり、`IWICBitmap` へ実体化された後の出自まで識別できるわけではない。Windows10 Photosがこの保存経路を実際に使うことも確認できていない。PhotosのHEVC警告・ボタン無効は **未解決**。保存成功や画像保護の全面的な証明でもない。回転編集可能な完成MSIは **配布禁止を維持**。
- **優先する次の検証**：ユーザーPCの現状（v1.1.18復元済み）に手を触れず、v1.1.18と研究版の `QueryCapability` マスクと空ICC `GetColorContexts` の挙動がPhotos GUIへ影響するかを切り分ける。公式WIC規約に反する `SameEncoder` 申告は戻さない。Windows Server CIのWIC呼び出し成功をPhotos GUI合格と誤認しない。

## 引継ぎ後の追加検証：ICCを持つ人工HEICでの旧v1.1.18対研究版A/B（2026-10-23）

**既存調査との差分**：従来のA/BはICCプロファイルが0件の人工HEICに対する `GetColorContexts(0,nullptr,&count)` だった。ICCが実際に存在する画像の挙動は未確認であり、同じ試験を繰り返さず `wictest/WICDecoderCapabilityProbe.h` に `SurveySyntheticIccForPhotosEditor` を追加（commit `6cba2e5cfde183478c8a57ea058845f73c9c3a72`）。以前からCIで生成されている `samples/icc-roundtrip.heic`（公開人工HEIC）を再利用し、旧MSI v1.1.18と研究版を同一使い捨てWindows Server 2022で順に観測した。

[GitHub Actions #39962636837](https://github.com/taku077/wic_heic/actions/runs/39962636837) **全工程SUCCESS**（旧MSI SHA照合、WIC A/B、既存ロスレス回転・メタデータ保護回帰、配布禁止ゲート）。

| ICC入り人工HEIC・WIC API | 旧v1.1.18 | 研究版 |
|---|---|---|
| HEICデコーダー作成 | `0x00000000` | `0x00000000` |
| Decoder.GetColorContexts(0) | `0x88982F81`（0件） | `0x00000000`（1件） |
| Frame.GetColorContexts(0) | `0x88982F81`（0件） | `0x00000000`（1件） |
| Frame.GetColorContexts(1)実プロファイル取得 | 不可 | `0x00000000`（1件） |
| WICColorContextの型取得 | 不可 | `0x00000000`、型1（プロファイル） |

**確定したこと**：旧v1.1.18はプロファイル無しのケースだけでなく、実際にICCプロファイルを含む合成人工HEICでもICC APIが非対応。研究版は当該人工ICCを取得できる。したがって「旧版でGUIボタンが有効なのだから、空ICC応答を含め `GetColorContexts` を旧版の失敗応答へ戻す」という変更は画質・色管理後退であり、少なくとも現状では採用できない。Microsoft公式 `IWICBitmapFrameDecode::GetColorContexts` は `0,nullptr` で件数を照会する仕様: https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframedecode-getcolorcontexts 。

**未確定**：`GetColorContexts` 応答差がWindows 10 Photos 2026.11090.22001.0のHEVC警告・編集ボタン無効化を引き起こす因果関係。Windows Server WICテストはPhotos内部のMedia Foundation／HEVC拡張検査や編集フローを観測しない。旧版の `QueryCapability=0x15` を理由なく復活させない。Microsoft WIC `SameEncoder` は同一ベンダー画像のみ: https://learn.microsoft.com/en-us/windows/win32/api/wincodec/ne-wincodec-wicbitmapdecodercapabilities 。

**開発上の次アクション**：旧MSI導入・復旧をユーザーに再要求せず、WIC応答の非破壊的な比較とPhotos実機GUI／Media Foundation依存の証拠取得を区別する。現行v1.1.18は表示ボタンが有効でも画像変形／回転保存不具合があり完成版ではない。新MSI配布ゲートは引き続き閉鎖。

## 引継ぎ後の追加調査：標準フォトUIの前段となるWICフル解像度画素の旧版比較（2026-10-23）

**背景・新規性**：既存CIは旧v1.1.18／研究版のコーデック自動選択とWIC API HRESULT、研究版のロスレス4回転の画素一致を調べていた。しかし、**旧v1.1.18と研究版で同じ人工HEICの「フル解像度RGB画素そのもの」が一致するか**は未測定だった。旧版の表示乱れを正解値とみなさないためにも、別の比較軸が必要となった。

- 研究コミット `3f50c580d7d6aaf8d8f035b70e27d05545e27810`：人工HEICをWIC `CreateDecoderFromFilename` → `GetFrame(0)` → `WICConvertBitmapSource(RGB24)` → `CopyPixels(全フレーム)` で読み、フレーム数・寸法・HRESULT・RGB全バイト数・FNV64を出す観測専用テストを追加。元画像や画素値のログは収集しない。試験自体はユーザー写真を参照しない。
- 観測ログ整形とCI A/B比較：`484f5e45fb5153784a07cdec51c50982f7213de4`、`67738940430e49cbc057b424f58ac7c2a2b7986f`、`e9657c46ae5c151a44fa3492d09af7915e1c841f`。[Actions #39964503469](https://github.com/taku077/wic_heic/actions/runs/39964503469) **SUCCESS**。
- 同一版をそれぞれ別プロセスで2回再測定したCIコミット `099390d581b889e15851e31b20d72c825e75db7e`、[Actions #39965381543](https://github.com/taku077/wic_heic/actions/runs/39965381543) **SUCCESS**。旧版同士・研究版同士のFNV64は各々2回で一致し、版間だけ不一致。少なくとも今回の試験では偶発的不安定値ではない。
- 分析コミット `7eec5d482ab3dde3338be6a4d55c2a35b0fad659`、[Actions #39965964279](https://github.com/taku077/wic_heic/actions/runs/39965964279) **SUCCESS**。空間4領域のRGB和と位置に依存しないRGBヒストグラムFNV64を計測。生画素は一切ログに書かない。

| 同一公開人工HEIC、同一Windows Server 2022ワークフロー | 旧v1.1.18 | 研究版 |
|---|---|---|
| 自動選択されたデコーダー | 自作CLSID | 自作CLSID |
| WICフレーム数 | 2 | 2 |
| 第0フレーム寸法 | 1080×1920 | 1080×1920 |
| WIC RGB24変換、フルCopyPixels | 成功 | 成功 |
| RGBバイト数 | 6,220,800 | 6,220,800 |
| RGB全体FNV64 | `BBE3A110699D854C` | `77003DAE9142F975` |
| RGBヒストグラムFNV64（位置非依存） | `DEBFF6F820D6DEBE` | `B0CAA540D9075006` |
| R成分の全画素合計 | 262,888,691 | 263,975,780 |
| G成分の全画素合計 | 264,637,349 | 263,811,317 |
| B成分の全画素合計 | 263,260,083 | 264,627,625 |

**新たな確定事項**：
1. 両版でWIC自動選択のみならず、人工HEICのフル解像度RGB24変換と全画素読込が `S_OK`。この人工HEICについて「研究版はフル解像度のWIC読込ができない」という説は否定された。
2. 同一版の別プロセス再測定では画素ハッシュが安定する一方、旧版／研究版の画素ハッシュは相違。RGBヒストグラムまで相違し、**単なる画素配列順序の入れ替えのみでは説明できない**。版間の全画素RGBデータには色成分値の差もある。
3. ソース静的比較では、通常入力に対する `HeicBitmapFrameDecode::CopyPixels` の1行ごとの主なコピー・10bit値換算処理は実質同じで、Gitのsubmodule SHAも libheif / libde265 / kvazaar の3件で旧版・研究版が一致。**原因はなお未確定**。 historical MSIとCI研究ビルドは異なる生成物であり、ビルド構成・間接依存・初期化状態・色管理処理などを切り分ける前に特定ソースの不具合と断定しない。
4. 旧版の回転表示の破綻が実機報告されているので、RGB完全一致を理由に研究版を旧版へ近づける修正を行うのは危険。独立の期待画像・色空間基準でどちらが正しいかを評価する必要がある。

**未解決・リリースゲート**：本結果がPhotos 2026.11090.22001.0のHEVC警告／編集ボタン制御と因果関係を持つ証拠はない。Windows10実機GUIのフル表示、回転保存、実ファイル保護も未達。追加MSI配布は保留。次の調査はこの実測差分の原因解析と、標準Photosの実際のコーデック選択・UI判定経路の把握を区別して進める。

## 主画像IDが先頭でないHEICのWICフレーム取り違え：赤→緑の実証と修正（2026-10-23）

**実際に発見したバグ**：`CHeicBitmapDecoder::GetFrame(0)` はHEIF `pitm` 主画像を使わず `get_list_of_top_level_image_IDs()[0]` を採用していた。`QueryCapability` も先頭項目から機能を判断していた。主画像IDとトップレベル項目配列の並びが異なるファイルで、WIC 0番フレームが**非主画像**になり、標準フォトや保存の対象画像取り違えにつながり得る。

**異なる試験条件（既存試験の繰り返しではない）**：
- Git管理済みの公開人工HEIC `fixture-irot3-exif6-noaux.heic` は1080×1920の画像を含み、`pitm` の主画像ID=1、画像項目ID=1と2、Exif項目ID=3を持つ。ローカルメモリ複製の`pitm`のIDを1→2と**2バイトフィールドの1バイトのみ**変更し、元の人工ファイルを一切変更しない。
- 改変した人工コンテナーからWIC `GetFrame(0)` を取得、内部`IHeicRawSource::ReadFullContent`でそれが現在の主画像を示すかを試験。`GetFrame(1)` は非主画像として原本転送を拒否すべき。メモリ内だけで完結する。テスト `wictest/WICDecoderCapabilityProbe.h::VerifyPrimarySelectionWhenPitmChanges`。
- 試験追加 `d6e825691152eba8a93c06492cd63e78f11b9ea6`、専用テストヘッダー修正 `fd15695e5a89469f42551576fc244607a7b6fdf9`。[失敗CI #39969145167](https://github.com/taku077/wic_heic/actions/runs/39969145167)：`WIC_PRIMARY_SWITCH_FRAME_ZERO_RAW_HRESULT=0x88982F81`、「WIC GetFrame(0) does not point to HEIF pitm primary」。これが**製品コードにある再現可能なバグ**である。
- 修正 `71158e42ece7d224eda307050a443ae59361cb72`：`GetFrame(0)`で`get_primary_image_handle()`を選択し、2番以降にその他の画像を重複なく割り当てる。`QueryCapability`でも主画像を基準に判断する。[成功CI #39969507523](https://github.com/taku077/wic_heic/actions/runs/39969507523)：`WIC_PRIMARY_SWITCH_FRAME_ZERO_RAW_HRESULT=0x00000000`、`WIC_PRIMARY_HEIC_FRAME_ZERO_INVARIANT_PASS`。他の4回転・画素一致、変換ラッパーの損失防止、ICC、WICとv0.4.0隔離共存、歴史的v1.1.18とのA/BテストもPASS。
- 追加保存検証 `308a6941b3744c9d05c97d44142bb2b8c0554bf9`：`pitm=2`になった合成人工HEICについて、WICエンコーダーの実際の`WriteSource/Commit`が元コンテナーと**全バイト一致**すること、非主画像は`WINCODEC_ERR_UNSUPPORTEDOPERATION`で拒否され、出力ストリームが0バイトに留まることを検証する。[成功CI #39969954772](https://github.com/taku077/wic_heic/actions/runs/39969954772) **全工程SUCCESS**。ログ上で `WIC_PRIMARY_SWITCH_LOSSLESS_WIC_COPY_BYTE_EQUAL_PASS` と `WIC_PRIMARY_SWITCH_SECONDARY_LOSSLESS_COPY_REJECT_PASS` を同時確認。合成人工HEICの全バイト一致転送・非主画像拒否・出力0バイトを確認。Windows10 Photos GUIは未検証。

**重要な限定**：この不整合は「トップレベル画像の列挙順が主画像順と違う」HEICに特有であり、ユーザー実機で報告されたHEVC拡張警告・編集ボタン無効化や全ての回転バグの単一原因だと断定しない。Win10 Photos GUIの実機確認は未実施。歴史的v1.1.18は引き続き画像変形と保存エラーを抱える比較版。未確認MSIの配布は禁止。次に必要なのは実際のフォトからどのWIC/HEVC経路が選択されるかの切り分け。

## MATERIALIZED_BITMAP_GUARD_VERIFIED：WICのマテリアライズで回転元HEICの由来を失う保存経路（2026-10-23）

**新しい実バグ・赤→緑**：以前の回転保存安全ガードは `IWICBitmapFlipRotator`、`IWICFormatConverter`、`IWICBitmapScaler`、`IWICBitmapClipper` を拒否していた。しかしPhotosのようなWIC呼び出し元が `CreateBitmapFromSource(rotator,WICBitmapCacheOnLoad)` で回転画素を `IWICBitmap` に具体化すると、これらのラッパーQIが通らない一方で `IHeicRawSource` もないため、`CHeicBitmapFrameEncode::WriteSource` が裸のRGB画像を**原HEICのEXIF・ICC・XMP・MakerNote・HDR/サムネイルの引継ぎなしで再エンコード対象として受け付ける**実装だった。

1. 再現テスト `3a929ec46cac8caec5438072341ca204672bcf32`：公開人工HEICだけを90度回転し、`IWICBitmap` にメモリ内で具象化。既定圧縮と明示HEVCの両方で `WriteSource` 拒否・出力ストリーム0バイト・原人工HEIC不変を要求。[失敗CI #39971011380](https://github.com/taku077/wic_heic/actions/runs/39971011380)：既存3ラッパーは拒否するが、新しい `view=bitmap compression=0` が `hr=0x00000000` となり危険な再圧縮を**受け付けることを実証**。
2. 製品修正 `019fe705193cfec49bbdfdb88bbd5496f4ee93a4`：由来を証明できない `IWICBitmap` を `WriteSource` の損失危険入力の対象に追加。`IHeicRawSource` を維持した真のHEICデコードフレームの無再圧縮転送は従来どおり優先し、直接入力ではない曖昧なビットマップだけ拒否する。
3. 一般の**新規人工RGB画像生成**まで壊さないため、ローカル人工画像試験は、元HEICとの同一性を主張しない`WritePixels`へ明示的に切替。対象：`wictest/GeometryProbe.h` `8ae3a8ffa08b3310ee5f87daeaca817cf247aa63`、`wictest/ICCProfileProbe.h` `adad321e01db2c7a4bc19a3ef0d13c83924282d8`、`wictest/EncoderGuardProbe.h` `36dc2ff1528dcbe931f6612a4fe1d9eddc661b92`、誤った同一HRESULT期待値を修正 `3bb5007b4f5bbc8c74787adfd1be3db46a21442e`、`wictest/wictest.cpp` の人工回転スモーク試験 `5199b693e7ed0b315be040b055856c853d757d3a`。
4. [成功CI #39972199395](https://github.com/taku077/wic_heic/actions/runs/39972199395)：**SUCCESS**。`view=bitmap` は既定圧縮と明示HEVCの両方で `0x88982F81`、従来3ラッパーも全件 `0x88982F81`。原本不変・拒否時出力0バイト。主画像ID変更時のHEIC完全コピー、安全拒否、`GEOMETRY_PIXEL_AB_TEST_PASSED`、`HEIC_WIC_ICC_EXACT_ROUNDTRIP_PASSED`、`WIC_FOUR_TURNS_EXACT_RGB_PIXEL_IDENTITY_PASS`、旧1.1.18とのWIC A/B、WIC/サムネイル専用MSI隔離共存がすべて成功。利用者向けMSIは配布禁止。

**性能・互換性上のトレードオフ**：元HEIC由来でなく純粋に新規生成された `IWICBitmap` も、`WriteSource` だけでは区別できないため安全側に倒して拒否する。新規画像作成を明示する `WritePixels` を使うWIC呼び出し元はエンコード可能。これは既存の一般WICクライアントの互換性に影響し得るので、すべてのWindowsアプリの成功を主張しない。

**限界／未解決**：このガードは `WriteSource` に渡された**識別可能なWICラッパーとIWICBitmap**だけに効く。`WritePixels` が既存HEIC由来のピクセルである場合は原HEICとの対応を識別できず、元画像のメタデータ喪失防止を一般には保証しない。Photosが保存先ファイルをエンコーダー呼び出し前に切り詰めるケースも防げない。そもそもWindows 10 PhotosのHEVC警告・編集ボタン無効は現時点で未解決であり、実機GUIと無劣化回転保存のリリースゲートは閉じたまま。

## HEIC WIC `SetResolution` 成功偽装によるDPI編集黙殺を修正（2026-10-23、CI赤→緑）

**新しく確認した不具合**：`wic_heic/HeicBitmapFrameEncode.cpp::SetResolution(double dpiX,double dpiY)` は引数を読み取らず常に `S_OK` を返していた。WIC APIの定義ではこれは**物理解像度メタデータを設定する操作**であり、既存HEIC原本バイト列の無再圧縮転送の後にDPI 300を指定しても「成功」とする実装は、指定された編集を破棄したまま保存成功と誤報する。Microsoft公式：https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframeencode-setresolution

- **再現テスト追加** `7df6dd061ece86b2994dd69658e1f4807efd4a70`：`wictest/WICDirectLosslessProbe.h` に、公開人工HEICの原本を `WriteSource` でロスレス保持した後 `SetResolution(300,300)` が `WINCODEC_ERR_WRONGSTATE` を返すこと、フレーム初期化直後の非既定DPIは `WINCODEC_ERR_UNSUPPORTEDOPERATION`、正常な96dpiは `S_OK`、負のDPIは `E_INVALIDARG`、未完成出力が0バイトのままであることを確認する検証を追加。
- [**失敗CI #39973464774**](https://github.com/taku077/wic_heic/actions/runs/39973464774)：`WIC_LOSSLESS_STATE_UNLOCKED late SetResolution actual=0x00000000` と実測。原本バイト確定後でも更新APIが成功してしまう製品上の欠陥を確認。
- **製品修正** `d01b81cd92fbc74d304df49861b6541f7fbbabd9`：`m_LosslessReady`／フレーム作成済み／画素書込み済みは遅延DPI設定を `WINCODEC_ERR_WRONGSTATE` で拒否。有限でない、0以下のDPIは `E_INVALIDARG`、96dpi以外の物理解像度変更は現行エンコーダーが書き戻せないため `WINCODEC_ERR_UNSUPPORTEDOPERATION` で明示拒否。96dpiはWICデコーダーが提供する既定値として許可する。
- この変更により、従来の`wictest/wictest.cpp`の**公開サンプル変換だけ**で入力96.012001dpiの忠実転記要求が失敗するようになった。これは機能未実装を顕在化したもので、安全性ガードを緩めない。試験専用の人工画像トランスコードに限り、96dpi以外を「DPI保全は試験対象外」と明示ログに記録して `SetResolution` を要求せず、画像生成・WIC読み戻しを継続する（`d2c3abec41738e27bac035649ddfb31e6e4386b1`）。**ユーザー写真・一般Photos保存のためのDPI黙殺フォールバックではない**。
- [**成功CI #39974578063**](https://github.com/taku077/wic_heic/actions/runs/39974578063)：`WIC_EXPLICIT_DPI_CHANGE_MUST_NOT_BE_SILENT_PASS`、`WIC_LOSSLESS_CAPTURE_IMMUTABLE_AND_SOURCE_BYTES_PRESERVED_PASS`、`GEOMETRY_PIXEL_AB_TEST_PASSED`、`HEIC_WIC_ICC_EXACT_ROUNDTRIP_PASSED`、`WIC_PRIMARY_SWITCH_LOSSLESS_WIC_COPY_BYTE_EQUAL_PASS`、`WIC_FOUR_TURNS_EXACT_RGB_PIXEL_IDENTITY_PASS`、歴史的v1.1.18とのWIC選択A/B、WIC/サムネイル別MSI分離試験が全てSUCCESS。ログに`SYNTHETIC_SMOKE_DPI_METADATA_NOT_TRANSFERRED original_x=96.012001 original_y=96.012001` を明示。

**残る機能不足**：任意DPIの実際のHEIF/EXIFメタデータへの書込みは未実装。この段階では安全側に**拒否**するだけであり、ユーザーが求めるPhotos編集・回転保存が成功することを意味しない。96dpi以外の画像を再エンコードしたいWICアプリからは、以前よりエラーが増え得る。一般アプリ互換性上のリスクも残り、修正版を完成MSIとして配布しない。最優先のWindows 10標準フォトのHEVC警告／編集無効化は未解決、実機GUI試験は未実施。既知の旧v1.1.18回転バグは修復済みと見なさない。

## Windows 10 フォトが複数プロセスで動く場合のWIC接続：可視窓とDLL読込みの相関（2026-10-24）

**新たに過去の実機JSONを確認**：2026-10-23の`Photos_WIC_Process_ReadOnly_20261023_202155_143_9224.json`には、同時刻の`Photos.exe`が**2プロセス**あり、一方に`wic_heic.dll`がロードされ、他方には`windowscodecs.dll`しか検出されていなかった。ApplicationFrameHostは別プロセス。**DLLがロードされたフォトと、HEVC警告を表示する可視フォトが同一PIDとは限らない。** 旧版にも画面表示バグがあるため、グローバルWICを再インストールするだけの未検証対策は繰り返さない。安全のため、GitHub文書にはユーザー端末の実PID・端末固有DLLパスを転記しない。

**読取専用診断の改修・CI**：
- `diagnostics/photos_trace/Photos_WIC_Process_ReadOnly.ps1`：USER32の`EnumWindows`・`EnumChildWindows`・`GetWindowThreadProcessId`・`IsWindowVisible`を介して、可視トップレベル画面とApplicationFrameHost等の子ウィンドウの所属PIDを**ウィンドウタイトル・画像・文字列・座標を読まずに**観測。既存PhotosプロセスのWIC DLLマップに`VisibleTopLevelWindowCount`、`VisibleHostedChildWindowCount`、`WindowScan`を追加。既存JSON項目`PhotosProcessModuleSnapshot`は維持。読み取れない場合は`WindowScan=UNAVAILABLE`と明示。システム設定変更、MSI、HEIC原本操作、ウィンドウ内容の取得は禁止。
- 追加の`VisiblePhotosWicCorrelation`は`NO_VISIBLE_PHOTOS_PROCESS_OBSERVED`、`WINDOW_SCAN_UNAVAILABLE`、`MIXED_VISIBLE_PHOTOS_PROCESSES`、`VISIBLE_PHOTOS_PROCESS_HAS_CUSTOM_WIC_MODULE`、`VISIBLE_PHOTOS_PROCESS_WITHOUT_CUSTOM_WIC_MODULE`のいずれか。**可視PIDとDLLロードの瞬間的な相関であり、写真の閲覧・編集処理でどのWIC APIが呼ばれたかを証明しない。**
- 新コード `4b32a67a3de97f4b3c044b7f842e53547b67ff4c`、`719493c52103b058d8b9db3157226450e016daa8`、`078204ec95b0926b535115d981f594cc3ca2b090`、`87c6bcb3399e669528b2b2ae200f9b7766a41a7c`、`2ec47abf9983f22be7172dfe18bde10661ba5250`、`8289557e4b791d671b4ce4e30ec80aef575f798e`、最終の構文修正`ba0e65c702c47742e117b72ec10ec94669d5f796`。
- [Windows PowerShell 5.1隔離CI #39979117146](https://github.com/taku077/wic_heic/actions/runs/39979117146)：**SUCCESS**。構文、読取専用、プライバシーガード、ワンクリックCMD、4ファイル（合成人工HEIC・CMD・PS1・README）キットSHA検証とartifact公開を確認。`WindowScan=OK`、`VisiblePhotosWicCorrelation=NO_VISIBLE_PHOTOS_PROCESS_OBSERVED`。Windows Server 2022 CIには`Photos.exe`がなく、実Windows10フォトを紐付けた結果**ではない**。実機側のユーザー操作が必要な診断を何度も要求せず、必要となった時に公開人工HEICをフォトで開いた状態の単発実行だけを行う。試験用MSI導入は不要。
- CIアーティファクト名`Photos-WIC-ReadOnly-NoAdmin-WithSyntheticHEIC-4Files`、artifact ID `12513275266`。ユーザーに報告を依頼する前に、該当CIの成功とZIPのファイル構成を確認する。

**この試験を優先する理由**：旧v1.1.18が「フォトの警告・編集ボタンが一時的に正常」でも「保存に失敗・画像変形」、研究版は編集ボタン無効という複数経路の問題を、**可視UIに対応する実プロセスへ絞らずにWICとHEVCのコードだけ直しても原因究明は不十分**だから。Windows 10標準フォト実機の編集有効化・無再圧縮保存・原本保護は依然未達。MSIリリース禁止を維持する。

## 旧WICと研究版の2フレームRGB・サムネイル能力追加検証（2026-10-24）

**重要な仕様確認**：Microsoft WIC公式「Implementing IWICBitmapDecoder」によれば、実際のグローバル/Frame 0の埋め込みサムネイルが存在しない場合は `WICBitmapDecoderCapabilityCanDecodeThumbnail` を申告してはならず、WICの呼び出し元が主画像を縮小する。旧v1.1.18の`QueryCapability=0x15`（`SameEncoder`とサムネイルを無条件申告）は不正確であり、戻して「互換性を改善」としない。参考：https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapdecoder

- 最初の実験テスト `79a1abfdcf6c4119b90153438248ba45d121fc49` は、埋め込みサムネイルのない公開HEICでも`GetThumbnail`で自動生成を要求した。[CI #39977565875](https://github.com/taku077/wic_heic/actions/runs/39977565875)は`container=0x88982F44, frame=0x88982F44`で失敗したが、**これは仕様違反のテスト条件だったため、製品コードのバグとは扱わない**。
- 仕様に沿うWICスケーラー代替検証に修正（`1468cd8b9e62a0c800617053a0a9a2f2ad961b6a`）。埋め込みなしの人工HEICでGlobal/Frame `GetThumbnail`が不在を正しく返し、能力マスクが`0x0C`で誤ったサムネイルフラグなしでも、`IWICBitmapScaler`で主画像から**144×256**の向き・比率が正しいRGB画像を作れることを実証。ロスレス原本データへプレビューがなりすますことも防止。
- さらに、過去の旧版と研究版のフルRGBハッシュ不一致は「0番と1番の別画像を比較したため」とも考えられたので、両版の2フレーム全体のFNV64を測定（`384f1c9106101588c6c8577f1439337e6d5c76f8`）。[CI #39978165477](https://github.com/taku077/wic_heic/actions/runs/39978165477)：**SUCCESS**。

| WIC/人工HEIC検証 | 旧v1.1.18 | 研究版 |
|---|---|---|
| 自動WIC Frame 0 寸法 | 1080×1920 | 1080×1920 |
| Frame 0 RGB FNV64 | `BBE3A110699D854C` | `77003DAE9142F975` |
| 自動WIC Frame 1 寸法 | 1920×1080 | 1920×1080 |
| Frame 1 RGB FNV64 | `00DE34F36CF9C3E5` | `A04BF4B5648503C9` |
| `QueryCapability` mask | `0x15`（サムネイル無条件） | `0x0C`（実装に即した申告） |

**推論の制約**：両フレームとも旧版と研究版でハッシュ不一致。**単純に0番と1番を取り違えただけでは今回のRGB差は説明できない**。ただし、旧MSIと研究CIビルドのネイティブライブラリ差・色管理・ソース選択など他要因は未除外。「原因特定済み」と扱わない。サムネイルAPIのフル画像スケールが可能でも、PhotosのHEVC警告・編集ボタンが修復したことの証明にはならない。実機GUIの承認ゲートとMSI配布禁止を維持する。

## Windows 10実機の可視Photos PIDとWIC DLLの相関：Schema 2の実測 → SHA検証追加（2026-10-24）

2026-10-24 00:39 JST頃、ユーザー提供の`Photos_WIC_Process_ReadOnly` **Schema 2 JSON** を検証した。Windows 10 build19045、`WindowScan=OK`、`VisiblePhotosWicCorrelation=VISIBLE_PHOTOS_PROCESS_HAS_CUSTOM_WIC_MODULE`。可視トップレベル/子ウィンドウのある`Photos.exe`に `windowscodecs.dll` と `wic_heic.dll` が同時にマップされ、もう1つのウィンドウなし`Photos.exe`は`windowscodecs.dll`のみ、`ApplicationFrameHost`は`windowscodecs.dll`のみが検出された。対象の元JSONは`FileContentsOrPhotosAccessed=false`および`PersistentSystemChanges=false`。**個々のPID、実パス、ユーザー環境のSHA256、画像ファイル名、画像内容はGitHubに転載しない。**

今回の反証：`wic_heic.dll`が見えたのは表示と無関係なバックグラウンドPhotosだけ、という仮説は当該時点では支持されない。実際の**可視Photosプロセス**が自作DLLをロードしている。なおDllロードの事実だけでは、全画面画像読込、編集可否判定、回転保存、HEVC MFTの有無、サムネイル／プレビュー／フル画像のどの経路でDLLが使われたかは証明できない。以前からある旧`v1.1.18`の画像変形・回転保存失敗を「正常」と判断してはならない。未検証WIC MSIはユーザーPCに導入しない。

不足した観測：Schema 2ではWICレジストリ登録**先**のDLLはSHA256を取得するが、Photosの`Process.Modules`側はbasenameしか収集していない。DLLが以前ロードされたままか／別のファイルパスから同名DLLがロードされたかは未判明。対応する**ディスク上のDLLファイル**同士の照合（メモリ中のバイト列ではない）を読み取り専用で追加した。プロセス記録の`MappedCustomWicDllRegisteredFileMatch`に、`MATCHES_REGISTERED_ON_DISK_SHA256`／`DIFFERS_FROM_REGISTERED_ON_DISK_SHA256`／`MODULE_FILE_HASH_UNAVAILABLE`／`MODULE_NOT_OBSERVED`のいずれかを追記し、パス・プロセス毎のハッシュ値は記録しない。一般の画像・タイトル・ウィンドウ内容を読まない。Windows 10 Photosへの**モジュール実在とAPI呼出しは別**という解釈は維持。

- コード・修正コミット `e3c41ea0e2f98c040399cf719bcae7ccec146298`、`0c6f674d31d30be6b369eea16275773a057e7e6a`、`fc0b9bd3f071224be07c5f33cf6db667a716bf97`。当初はPowerShell構文破損をCI検出したため**配布せず**、既知のCI合格スクリプトから安全に再構成：`2397c4f00bf8562223977dd2683e2948a5b0f661`。
- [隔離CI #39980980119](https://github.com/taku077/wic_heic/actions/runs/39980980119) **SUCCESS**：PS5.1実行・構文、`PHOTOS_WIC_MAPPED_DLL_SHA256_ONLY_PRIVACY_GUARD_PASS`、ウィンドウPID相関、安全なCMDランチャー、公開人工HEICを含む4ファイルZIP（SHA検証）を合格。アーティファクト`Photos-WIC-ReadOnly-NoAdmin-WithSyntheticHEIC-4Files`、ID`12514160076`。Windows Server 2022 CI上にPhotosアプリはなく、**実ユーザー環境でのDLL照合結果は未取得**。
- 新観測で「登録ファイルと読込みファイルのSHA不一致」が出た場合は、再インストールより先に競合・旧DLL残存・プロセスキャッシュを疑う。「一致」であればその仮説は弱まり、より本質的なWIC機能申告・Photos内部のHEVC/編集経路へ重点を移す。モジュールのディスク照合「一致」も、Photosから個々のWIC APIを呼んだ事実や画質・保存の証明にはならない。

**完成率**：この実機情報と診断機能自体は有益だが、元のP0達成条件である「HEVC警告を研究版で消す」「Photos GUI編集と安全保存を実機で成功させる」はまだ未達。自動CIと実機機能達成率を混同せず、過去の39%から数値を無条件に増やさない。

## Schema 3実機照合: MATCHES_REGISTERED_ON_DISK_SHA256（2026-10-24）

**実機ユーザー提供JSONの確定事項（GitHubには匿名化した判定だけ記録）**：
- Windows 10 build 19045、`WindowScan=OK`、`VisiblePhotosWicCorrelation=VISIBLE_PHOTOS_PROCESS_HAS_CUSTOM_WIC_MODULE`。
- 可視トップレベル画面とホストの可視子ウィンドウを持つ`Photos.exe`が`windowscodecs.dll`と`wic_heic.dll`をロードしている。`ApplicationFrameHost`には`windowscodecs.dll`、ほかの非可視`Photos.exe`にも`windowscodecs.dll`のみが検出された。
- **重要な追加観測**：当該可視`Photos.exe`の`MappedCustomWicDllRegisteredFileMatch=MATCHES_REGISTERED_ON_DISK_SHA256`。`HKLM`側のデコーダーとエンコーダー登録先は同一ハッシュ。これにより**可視フォトが、登録WICと異なるディスク上のDLLファイルをロードしている**という個別原因仮説は支持されない。
- 実機個別PID、完全なDLLパス、登録ハッシュや日時の秒以下等はGitHub上では掲載しない。ファイル内容・画像・個人情報・ウィンドウタイトルを読んでいない。
- **観測の限界**：一致したのはプロセスモジュールが指す**ディスク上のファイル**と現在のCOM登録ファイルのSHA-256。プロセス内メモリのコードが同一か、どのWIC APIが実際に編集判定に使われたか、HEVCのMF/Store拡張経路が利用可能かは示さない。観測時点で稼働中の比較ベースは旧`1.1.18`（表示・ボタン有効、ただし回転変形・保存失敗あり）であり、研究版を実機で検証した証拠ではない。
- したがって、**更なる読み取り専用のプロセス/DLL名・ハッシュ照合を繰り返さない**。主な未解決事項は(1)Photos UIからの実際のWIC/Media Foundation呼出しとHRESULTの根拠(2)旧v1.1.18で警告が消えた状態と研究版で再発する際のアプリ内条件(3)専用人工HEICでのWindows 10 GUIの編集・ロスレス保存、および導入前の復元ゲートである。GUI実測を伴わないWindows Server CIを完成条件に代用しない。

Microsoft公式サポートではWindows 10フォトにHEIFとHEVCの両拡張が必要となる場合を記載している：https://support.microsoft.com/en-us/windows/apps/photos/photos-app-video-editor-error-can-t-view-this-file-type 。**第三者WICコーデックを登録するだけで標準フォト独自のHEVC依存・使用許諾確認を満たすと断定しない**。旧`v1.1.18`の誤った`QueryCapability=0x15`を無条件復帰しない：`SameEncoder`は同ベンダーによる符号化を実際に認識した場合の値であり、`CanDecodeThumbnail`は現に取得できるサムネイルを要する。公式：https://learn.microsoft.com/en-us/windows/win32/api/wincodec/ne-wincodec-wicbitmapdecodercapabilities 。

**進捗表示**：39％据え置き。これは原因候補の排除・実機診断の前進であるが、ユーザーが求める標準フォト機能ゲートの解決はまだ達成していない。

## WICエンコーダーの二段階Commit実装不備・初期化順序の検証と修正（2026-10-24）

**背景**：標準Photos GUIの保存経路は未観測だが、研究版`CHeicBitmapEncoder`と`CHeicBitmapFrameEncode`のWIC公開API呼出し順序に独立した不具合がある。Microsoft公式は`IWICBitmapFrameEncode::Commit`で各フレームを確定した後でのみ`IWICBitmapEncoder::Commit`を行い、成功したフレームは再編集・再初期化できず、エンコーダー保存成功後も新しいフレームを作成できないと定める。参照：
https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapencoder-commit
https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframeencode-commit
https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapencoder-createnewframe

1. **最初の不具合 RED**：公開人工RGB 192x320を`WritePixels`ですべて渡しただけの状態（`frame.Commit()`未実施）で`encoder.Commit()`が **`S_OK`**、出力HEICをコミットしてしまう。[失敗CI #39986448240](https://github.com/taku077/wic_heic/actions/runs/39986448240) `WIC_ENCODER_COMMIT_BEFORE_FRAME_COMMIT_HRESULT=0x00000000`。入力原画像を使わず既存生成グリッドと専用CIテスト出力だけに限定。
2. **修正**：フレームに`m_Committed`を導入、フレーム`Commit`が成功した場合のみ真にし、重複フレーム`Commit`と確定後のプロパティ編集を拒否。親のエンコーダーは全フレームの`m_Committed`を確認するまで元ストリームに1バイトも書かない。コード`89237e7de82410c912fffdcaa7f421263debdf0d`、`4ef7bccf8800998b0cc1dbca064deb0f5589c899`、`69b9b14a017afb238997fb46bed829d659735bea`。[成功CI #39986838426](https://github.com/taku077/wic_heic/actions/runs/39986838426)：早期Commitは`0x88982F04`（WRONGSTATE）、出力0バイト、正規順序なら通常保存成功。4回転画素一致、ICC、HEIC主画像完全コピー、旧1.1.18とのA/B継続試験もPASS。
3. **次の不具合 RED**：`encoder.Initialize`より前に`CreateNewFrame`を実行すると不正にも`S_OK`。コミット`aa9f3fb94cfee057f65354b462a94bf3342b5b3e`、[失敗CI #39987345759](https://github.com/taku077/wic_heic/actions/runs/39987345759) `WIC_ENCODER_PREINIT_CREATE_FRAME_HRESULT=0x00000000`。さらに保存完了後の新規フレームや前フレーム未確定での重複作成も許していた。
4. **修正**：エンコーダーに`m_Committed`を追加。`CreateNewFrame`はエンコーダー未初期化・確定済み・前フレーム未確定なら`WINCODEC_ERR_WRONGSTATE`、保存完了後のICC変更も拒否。成功した親`Commit`でのみ確定状態にする。コード`dd9134de2863059f06a20a21fe604e313c95f855`、`0749c1115f48c2590e61a0556b55ca01cce5a472`。
5. **過去A/B試験の誤った前提を是正**：旧1.1.18と研究版の「13 API安定比較」自体がエンコーダー`Initialize`を省略した後に`CreateNewFrame`を行っていた。この旧版の不正受理を研究版へ強制するのは不合理なため、比較プローブで公開人工WICメモリストリームを明示的に`Initialize`した後に作成（`a04ad2a87b397277f97878b6112b026121015afc`）。新しい仕様違反を無効化するのではなく、**公式のWIC順序で旧・新の両方を比較**。[成功CI #39988330685](https://github.com/taku077/wic_heic/actions/runs/39988330685)：`WIC_ENCODER_PREINIT_CREATE_FRAME_HRESULT=0x88982F04`、`WIC_ENCODER_OVERLAPPING_FRAME_HRESULT=0x88982F04`、`WIC_ENCODER_POSTCOMMIT_CREATE_FRAME_HRESULT=0x88982F04`、エンコーダー早期Commit拒否、正規保存・ICC・4回転全RGB画素一致、旧版との合成人工HEIC A/BすべてPASS。
6. **既知の制限**：これらはWIC公開契約とメタデータ保存の安全性を改善したもので、標準フォトのHEVC拡張警告・編集ボタン無効化が解消したという証明ではない。さらにPhotosが指定するWIC frame のメタデータ更新を完全にサポートしたわけでもない。Windows10 Photos GUIとJPEG同等の無再圧縮回転は未合格、**ユーザー向けMSI配布禁止継続**。

## 未初期化／読込失敗したデコーダーからの架空フレーム照会：赤→緑（2026-10-24）

**確認した実バグ**：`CHeicBitmapDecoder::GetFrameCount`は`Initialize`を一度も成功させていなくても、空の`m_Context`からフレーム数0を返し`S_OK`としていた。同じ状態で`GetFrame(0)`は`WINCODEC_ERR_FRAMEMISSING`を返し、API間の状態矛盾がある。また元実装では`m_Reader`が解析前に確保されるため、malformed PNG/HEIFで`Initialize`が`WINCODEC_ERR_BADIMAGE`に失敗した場合にも`m_Reader`が非NULLに残り、プロファイル・サムネイルの条件確認が実際の初期化成功を意味していなかった。

- テスト`62460f661cf6ad584661f388485e4f48e22c4fe6`、[**失敗CI #39988937212**](https://github.com/taku077/wic_heic/actions/runs/39988937212)：`WIC_DECODER_UNOPENED_FRAME_QUERIES count_hr=0x00000000 count=0 frame_hr=0x88982F62`。WIC初期化なしなのにGetFrameCountが成功することを実証。
- 製品修正`b1c0de561851aebe22b51434dc96985e8040e48f`、`8c80675575eb17b71c78896f75dc4d3befbe3af4`：`m_Initialized`を導入し、HEIF解析が最後まで成功した場合のみ真。GetFrameCount/GetFrame/GetColorContexts/GetThumbnail/CopyOriginalBytesで解析成功確認。フレーム数は失敗時も0に初期化。
- [**成功CI #39989287630**](https://github.com/taku077/wic_heic/actions/runs/39989287630)：`WIC_DECODER_UNOPENED_FRAME_QUERIES count_hr=0x88982F04 count=0 frame_hr=0x88982F04`、`WIC_DECODER_FAILED_INIT_FRAME_QUERIES count_hr=0x88982F04 count=0 frame_hr=0x88982F04`、`WIC_DECODER_FRAME_QUERIES_REQUIRE_SUCCESSFUL_INITIALIZATION_PASS`。従来のHEIC画像・主画像の完全コピー・ICC・ロスレス4回90度画素一致、旧v1.1.18とのWIC A/Bも全工程PASS。
- **範囲**：これは画像の不正入力後の状態矛盾を直すもので、Windows 10 PhotosのHEVC警告／編集ボタンの修復が実機で確認されたわけではない。ユーザー写真はテスト対象外、ユーザーPCの設定は変更せず、研究版MSIの配布禁止を維持。

## フレームInitialize前の無条件Setter成功：WIC frame lifecycleを修正（2026-10-24）

**新規RED検証**：`IWICBitmapFrameEncode::Initialize`前に、研究版`CHeicBitmapFrameEncode`が`SetSize`、`SetPixelFormat`、`SetColorContexts(0)`をすべて`S_OK`で受理した。Microsoft公式はフレーム`Initialize`がオプション確定の最初の手順であり、後続の出力形状・ICC変更に先行すると定める。https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapframeencode 。公開人工RGBだけで検査。テストコミット`9c55ef4aeeb43618c3b4b7ef3f3904f49198f365`、[**赤CI #39989860749**](https://github.com/taku077/wic_heic/actions/runs/39989860749)：`WIC_FRAME_PREINIT_SETTERS_HRESULT size=0x00000000 format=0x00000000 icc=0x00000000`。
- 製品修正 `7e664a5b41e9e8b25ea2f21435e4339650f39254`、DPI追加漏れを修正`cfff80507dbca54eac295e4d5784a502814e835a`。`m_OptionsInitialized`が真でない限り`SetSize`、`SetPixelFormat`、`SetColorContexts`、`SetResolution`、`WriteSource`、`WritePixels`、`Frame.Commit`を`WINCODEC_ERR_WRONGSTATE`で拒否。
- [**緑CI #39990292766**](https://github.com/taku077/wic_heic/actions/runs/39990292766)：`WIC_FRAME_PREINIT_SETTERS_HRESULT size=0x88982F04 format=0x88982F04 icc=0x88982F04`。旧・新WIC比較、EXIF/ICC、HEIC主画像、失敗時出力不変、4回90度の6,220,800バイトRGB一致、公開人工HEIC生成・読戻し試験全てPASS。ユーザーPCへMSI導入・レジストリ変更なし。
- Windows10 PhotosのHEVC警告と編集ボタンは未解決。WIC内部契約に合致したことをGUI動作の証明とは扱わない。

## HEICルートメタデータ形式のJPEG誤申告：EXIF読取256回を保持して修正（2026-10-24）

**発見した実不整合**：研究版および旧版由来の`CHeicMetadataBlockReader::GetContainerFormat`は、`GUID_ContainerFormatHEIC`ではなく`GUID_ContainerFormatJpeg`を返していた。EXIFアイテムのWIC読取りに`GUID_MetadataFormatApp1`を指定してJPEG APP1のブロック構造を用いること自体は意図的な互換実装だが、**外側の画像コンテナーまでJPEGと偽るのは別問題**。Microsoft WICの`IWICMetadataBlockReader::GetContainerFormat`仕様はデコーダーの画像コンテナーを返すもの：https://learn.microsoft.com/en-us/windows/win32/api/wincodecsdk/nf-wincodecsdk-iwicmetadatablockreader-getcontainerformat 。

- 再現テスト`a81b8ef3de9417b2eb5b87bd013cfc351669461d`に、人工HEICをWICで開き、`IWICMetadataQueryReader::GetContainerFormat`がHEICに一致することを追加。既存の`/app1/ifd/{ushort=274}`のEXIF Orientation 1読戻しおよび256回連続のreader生成・参照解放試験は削除しない。[**赤CI #39991082574**](https://github.com/taku077/wic_heic/actions/runs/39991082574) ：`WIC_HEIC_METADATA_ROOT_CONTAINER_HEIC=0`、JPEGコンテナーを返していた。
- 製品修正`745b2c9e020119465831198dcaed71ff99c93d78`：`CHeicMetadataBlockReader::GetContainerFormat`は`GUID_ContainerFormatHEIC`を返す。内部のEXIF `GUID_MetadataFormatApp1`読取りやEXIF Orientation正規化、XMPなどメタデータブロックの解析手順は変更しない。
- [**緑CI #39991396516**](https://github.com/taku077/wic_heic/actions/runs/39991396516)：`WIC_HEIC_METADATA_ROOT_CONTAINER_HEIC=1`、`WIC_HEIC_METADATA_ROOT_CONTAINER_HEIC_PASS`、`EXIF Orientation returned by WIC = 1 (type 18)`、`WIC_METADATA_READER_256_CREATE_QUERY_RELEASE_PASS`、`ROTATED_HEIC_METADATA_AND_PIXELS_PASSED`がすべて成功。加えて旧v1.1.18とのA/B、主画像、ICC、4回転全6,220,800バイトRGB一致、既存WICエンコーダー／デコーダー状態保護、別MSI隔離共存ゲートも全SUCCESS。**旧版との内部メタデータ・コンテナーGUID差は意図的な仕様修正であり、13 APIの全てが文字列一致するという意味ではない**。
- **明確な未達条件**：Windows 10 Photos 2026.11090.22001.0 の実UIに研究版を導入した結果ではないため、HEVC拡張警告、編集／回転ボタンの有効化、安全な保存についての合格証拠はない。Windows Server 2022 CIにはWindows10フォトUIがない。旧1.1.18の「ボタンがある」状態も回転表示破綻／保存失敗を伴う未完成状態。HEVC拡張を購入せずWIC以外のMedia Foundation経路が使用できるかは引き続き未確認。新MSIの配布ゲートは閉じたまま。
 
## 2026-10-24 一括開発の総括・未達ゲート

この応答内で**赤→緑**を確認した5つの不具合：
1. フレーム未コミットでも親エンコーダーが`S_OK`でHEICを発行する → 修正・早期Commit出力0バイト。
2. エンコーダーのInitialize前フレーム作成、前フレーム未確定での重複作成、保存後の追加作成／ICC変更が受理される → 状態ガード。
3. HEIF未初期化／解析失敗のままフレーム数`S_OK=0`および誤ったFrameMissing → 解析成功フラグとWRONGSTATE。
4. フレームInitialize前のSetSize/SetPixelFormat/SetColorContexts成功 → 初期化時点の契約を保持。
5. EXIF APP1とHEIC本体を混同してメタデータルートをJPEGと申告 → HEIC形式で返し、EXIF Orientation読取と256回連続生成を維持。

これらはWIC実装の規約／データ保護を改善するが、**Windows10標準フォトの警告・編集可能・ロスレス回転保存というP0機能の解決に直結したと断言できない**。ユーザーPCのv1.1.18はそのまま、HEIC Manager/サムネイル専用MSIも変更なし、原本の写真を試験に使わない。正しい研究版MSI完成率は保守的な暫定見積りで**39％→42％（+3ポイント）**、ただし実GUIで警告が消えない場合は機能合格率には反映されない。リリースは引き続きブロック。

次の技術上の重要ゲート：同じ人工HEICでWindows10標準フォトが**可視プロセスのWICデコーダーのどのAPI能力／Media Foundation HEVC MFT／Store拡張権限を用いて編集可能判定を行うのか**の証拠を確保すること。既存実機の可視Photosで登録済み旧1.1.18のWIC DLLロードとディスクハッシュ一致は確認済みであり、同一モジュール照合は繰り返さない。無根拠に`SameEncoder`や`CanDecodeThumbnail`を旧版の誤申告値へ戻さない。Win10 GUIに代わるWindows Server CI成功を完成保証しない。
