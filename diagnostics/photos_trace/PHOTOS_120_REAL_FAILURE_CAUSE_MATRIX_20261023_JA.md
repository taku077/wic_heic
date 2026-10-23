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
