> **2026-10-24 重要訂正：実際の旧MSIのソース位置**  
> 従来この記録で「旧v1.1.18のソース」をコミット `f5b66fe89fa203c89967b46399ed6af61a6161e8` と呼んでいたが、旧MSIを取得しているActions [#39433397223](https://github.com/taku077/wic_heic/actions/runs/39433397223)の **実際のhead_shaは `a18b9ab146ca4d6f7b46a86507d7f0d0d882dc87`**。MSIのSHA256が一致していても、そのビルド後に作成された `f5b66` のソースから生成されたことにはならない。a18→f5には59コミットあり、WICフレームデコードやICC APIが変更されている。今後は旧MSIの挙動を比較するソースに必ずa18を使用し、f5は中間の「後続改良版」と呼ぶ。これまでの**旧MSIの実行結果**（0x15等）は有効だが、それに対するソース上の原因説明はa18との再照合を要する。特に旧MSIは10bit非アルファ分岐内にWIC 48bppRGBとm_Bpp=32の不整合があるが、その**後に常に `// No HDR support for now` でm_chroma・WIC形式・m_BppをRGB24/24bitに上書きする**。したがって旧MSIの実際のWICフレームは10bit HEICもRGB24へ強制降格するので、「48bit宣言で32bitを書き込む経路が実行されていた」と説明するのは誤り。後続f5以降はその強制8bit化を撤去し、10bitをWIC RGB48 LEで取り扱う。旧版の画像変形が知られているため、この旧コードへ戻して互換性を得たと主張してはならない。実機PhotosのHEVC警告・編集可否の因果は依然未確定。

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

## 登録デコーダー画素形式の不足と8並列テストの入力寸法（2026-10-24、CI最終判定待ち）

**ソース上の事実**：研究版デコーダーの`CHeicBitmapFrameDecode::GetPixelFormat`は8bit RGB／RGBAと10/12/16bit RGB／RGBAをそれぞれ`GUID_WICPixelFormat24bppRGB`、`32bppRGBA`、`48bppRGB`、`64bppRGBA`で返せる。しかし`wic_heic/dllmain.cpp`のWICデコーダー登録は従来`24bppRGB`のみ。`wictest/HighBitDepthProbe.h`が既に10bitの公開人工HEICから48bpp RGBを読めることを検証している。これはWIC能力列挙と実フレーム形式の**登録不一致**であり、Photos編集警告の原因と同一視してはいけない。

- テストコミット`45ce0404123ad4200a6848f574574596b50c7c4d`：`IWICBitmapDecoderInfo::GetPixelFormats`で4形式を確認する試験を追加。GUID実値はMicrosoft WinSDK `wincodec.h`で確認済み：https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/wincodec.h
- ソース修正`2f4b4042e87ff76c840f797d5fe38f8e7998c7b5`：**デコーダー登録のみ**に32bppRGBA、48bppRGB、64bppRGBAを追加。実際に10bitへHEVC再圧縮できないバックエンドがあるため、**エンコーダーへ未確認の48/64bpp対応を申告していない**。導入先PCへのレジストリ変更なし、CIの使い捨てWindows環境のみで登録を検証。
- この試験以前から同ブランチで追加された「同一WICフレームを8スレッドから一斉に`CopyPixels`」の試験が、初回はラムダ式のMSVCキャプチャでビルド失敗。実行後も「8スレッドの画素不一致」で失敗したが、**元のstrideは24bpp（`width*3`）なのに人工HEICは48bpp**であり、テスト自体のバッファ指定ミスだった可能性が高い。ステージング人工HEICのWICフレーム形式を確認し`width*6`で再計測する修正`174a1a79a4051e754ea4a0cb4e982babf845df31`。診断用に**人工画素のみ**各ワーカのHRESULTとFNV64を出力（`60d83c200d2704aa46fa9e24b46d59eba22e177c`）。ユーザー写真、ウィンドウ内容、端末上の画像は取得しない。
- CI依存性を見分けるため、`HighBitDepthProbe::Run`を並列コピー試験より前に実行するよう変更`edb907feb5f864ef99900a5d8257844aad52ddfb`。**並列テスト修正の最終CI結果と登録形式の最終CI結果は、この節を記録した時点で未確定。**
- **保留事項**：本修正はWICランタイム・登録能力の齟齬を修正するものであり、現ユーザーWindows10 PhotosのHEVC拡張警告／編集／回転保存が直った証拠ではない。旧v1.1.18の誤ったサムネイル／SameEncoder能力申告を復活させず、研究版MSI配布禁止を維持。

## 同一HEICフレームのMTA並列CopyPixelsと登録画素形式（2026-10-24）

**検証対象**：Windows 10 PhotosのWIC経由フル画素読取り・10/12bitサンプルの能力検出を安全にする。いずれも公開合成人工HEICのみ。旧版v1.1.18ではGUI回転保存に別の未解決問題がある。現行ユーザー実機は変更しない。

- `CHeicBitmapFrameDecode::DecodeImage`では`m_Decoded`、`m_Image`、`m_Stride`、`m_PlaneInterleaved`の初期化に同期がなく、同一フレームの複数`CopyPixels`でデータ競合があり得た。各フレームに`std::mutex m_DecodeMutex`を置き**初回復号と出版だけ排他**、別フレーム・別画像の既存並列復号は触らず、完了した画像の同時読み取りを可能にする修正`a23d5eb1bece6e917f8e6dfd4dfa5435d7bc654d`、`acef5c546c83fc32d288a7765b0a9746c640f24a`。
- Windows MTA上で1個の人工HEIC WIC frameに対する8スレッド同時`CopyPixels`を追加。CI試験の最初の失敗は**プロダクト競合の証拠ではない**。対象HEICは高ビット深度`48bppRGB`だが、テストが8bit用`24bppRGB`の行サイズを渡していたため、WICから不正なバッファ要求として拒否されていた。各スレッドの戻り値/合成画像ハッシュ診断と行サイズを是正（`5fa5a80cbdf9704bf0fd70e760e78b056ff215ca`、`3f576c61d625173b9ae7d534a2429d9ebc44a94a`、`dd2b66c21e22398d4ed545648b8ec85d09c141cc`、`922a5eef213d1796cae73099accbde8aaa69c7e2`、`60d83c200d2704aa46fa9e24b46d59eba22e177c`、`174a1a79a4051e754ea4a0cb4e982babf845df31`）。
- WIC decoderの`GetPixelFormat`は24bpp RGB、32bpp RGBA、48bpp RGB、64bpp RGBAを返せるのに、従来の`RegisterDecoder`には24bpp RGBしか記録されていなかった。`IWICBitmapDecoderInfo::GetPixelFormats`を先に調べるアプリは正しい画素形式を発見できない。合成人工10bit WICテスト`45ce0404123ad4200a6848f574574596b50c7c4d`、製品レジストリ登録修正`2f4b4042e87ff76c840f797d5fe38f8e7998c7b5`。**Decoder登録のみ**であり、実際に高ビット深度HEICをエンコードできる保証がないEncoder側には広げない。
- [**隔離CI #39995711285 — SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/39995711285)：WIC decoder登録画素形式`count=4 rgb24=1 rgba32=1 rgb48=1 rgba64=1`、8個の同時`CopyPixels`が全件`S_OK`でハッシュ`90C01259A9A8D128`一致、4回90度の全画素一致、旧版A/B、ICC保持、COMインストール/削除独立性の既存試験も合格。**実Windows 10 Photos GUIの警告解除・編集ボタン・安全保存が復旧したわけではない。**
- **続きの検証**：Encoder側の32bpp RGBA8対応はWIC APIでの受付だけでは認定しない。人工RGBAを`WritePixels`してHEICへCommitした後、フレームを開き直して画素形式とアルファの変化を確認する回帰試験を追加（`b142490ea9788ca26966544353792d1bb8db9553`）。この追試が成功するまではEncoderの登録形式は変更しない。研究版のMSI配布ゲートは閉鎖を継続。

### 上記の画素形式登録・MTA検証の最終CI結果（2026-10-24）

[**CI #39995711285**](https://github.com/taku077/wic_heic/actions/runs/39995711285)：**全工程SUCCESS**。Microsoft WinSDKの実GUIDをWIC decoder `Formats`へ追加し、標準WICコンポーネント情報から`WIC_HEIC_DECODER_REGISTERED_PIXEL_FORMATS count=4 rgb24=1 rgba32=1 rgb48=1 rgba64=1`を取得。直接10bit人工HEIC読込と4形式のデコーダー登録が整合した。

- 同じ**10bit人工HEIC**の同一frameへ8並列`CopyPixels`：8workerとも`HRESULT=0x00000000`、FNV64=`90C01259A9A8D128`で完全一致。`WIC_PHOTOS_SHARED_FRAME_MTA_COPYPIXELS_8WAY_IDENTICAL_PASS`。従前の「MTA race」失敗は24bpp用stride指定という**テスト不備が影響していた**。この1件の合格はすべての並列/長時間/アプリ側のアクセス安全性を保証するものではない。
- `WIC_PRIMARY_SWITCH_LOSSLESS_WIC_COPY_BYTE_EQUAL_PASS`、ICC 3144bytes正確な往復、主画像認識、完全バイト同一の無再圧縮処理、4回90度の`6220800`RGBバイト完全一致、旧1.1.18とのWIC合成画像A/B、フォト用MSI配布ブロックいずれも維持。
- 最新コード修正`2f4b4042e87ff76c840f797d5fe38f8e7998c7b5`、テスト入力stride修正`174a1a79a4051e754ea4a0cb4e982babf845df31`。今回ユーザーPCで旧1.1.18の入替・再導入・画像保存はしていない。
- **Windows 10 Photos GUIのHEVC警告/編集可否は未合格**。登録画素形式の不足は確実に修正されたが、フォト内部のHEVC利用権限とは別であり、因果を断定しない。元進捗42%からの増分はあくまで保守的な暫定評価とする。

## Encoderの32bpp RGBA8画素形式登録：実エンコード＋アルファ保持で能力を実証（2026-10-24）

**背景**：Decoderの32/48/64bpp追加と区別する。Encoderの`SetPixelFormat(GUID_WICPixelFormat32bppRGBA)`が`S_OK`を返すことだけでWindowsへのWIC Encoder Formats登録を増やしてはいけない。HEVCエンコーダーの対応力と戻り値に食い違いがあり得るため。

- 公開の人工64x64 RGBA配列（左透明・右不透明）を`WritePixels`し、`IWICBitmapFrameEncode::Commit`→`IWICBitmapEncoder::Commit`→同じメモリストリームをWICで再読込する試験`b142490ea9788ca26966544353792d1bb8db9553`。[**CI #39996238521 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/39996238521)：`WIC_HEIC_RGBA8_ENCODER_COMMIT_HRESULT=0x00000000`、画素形式`GUID_WICPixelFormat32bppRGBA`保持、透明～不透明アルファの変化保持。`WIC_HEIC_RGBA8_ENCODER_AND_ALPHA_READBACK_PASS`。
- WIC Encoder登録へ**32bppRGBAだけ**を追加し、他は既存の24bppRGBを維持（`5bdefdb3ca20859c13065a1bf1547d002dc14bdd`、`112fdc4547935c9f76b7f40f7b794631d627289c`）。同時更新で重複した`Formats`登録を`ffa8a424e5a3466c193671ebf11ec583b2f9141c`で1つに整理。Encoder側のWIC登録はDecoderと異なり、**48bpp/64bppを追加しない**。10bit HEVC再圧縮バックエンドのサポートは未保証。
- [**CI #39996917726 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/39996917726)：Encoder登録は`WIC_HEIC_ENCODER_REGISTERED_RGBA8 count=2 rgb24=1 rgba32=1 rgb48=0 rgba64=0`。`WIC_HEIC_ENCODER_REGISTERED_RGBA8_AND_REJECTS_UNPROVEN_HIGHBIT_PASS`、実HEIC→WICアルファ再読込、8並列10bit HEIC `CopyPixels`、Decoder登録4形式、EXIF/ICC／ロスレス4回90度画素一致、旧版1.1.18とのA/B、MSI隔離試験すべて成功。
- 本実装はHEICのアルファ付き人工RGB画像を新規保存できることと**WICコンポーネント情報の整合性**の実証であって、Photos 2026.11090.22001.0の警告・編集ボタン・回転保存を実機で直した証明ではない。HEIC Manager非干渉、旧WIC v1.1.18の画像変形問題継続、ユーザー画像・レジストリ未操作、MSI配布禁止を維持。

## RGBA8エンコーダーWIC登録の整合性を確認（2026-10-24 続き）

**前提**：デコーダーのRGB/RGBA24/32/48/64bppの実能力・登録は前節のCIで検証済み。しかしエンコーダーは`RegisterEncoder`に24bppRGBしか広告しておらず、コード上では32bppRGBAを`SetPixelFormat`で受け付けた。**受け付けるだけでは保存可能とは断定しない**ことが必須。

- `wictest/EncoderGuardProbe.h`に公開合成人工64×64 RGBA（左半分透明、右半分不透明）の**実際の32bppRGBA WritePixels → HEIC Commit → メモリ上のWIC復号 → RGBA出力・アルファ変化**を試すプローブを追加：`b142490ea9788ca26966544353792d1bb8db9553`。[**CI #39996238521 成功**](https://github.com/taku077/wic_heic/actions/runs/39996238521)：`WIC_HEIC_RGBA8_ENCODER_COMMIT_HRESULT=0x00000000`、`WIC_HEIC_RGBA8_ENCODER_AND_ALPHA_READBACK_PASS`。既存写真は使わない。
- 保存と読戻しが実証できた32bppRGBAのみ、エンコーダーのWIC component registryに追記（`5bdefdb3ca20859c13065a1bf1547d002dc14bdd`、`112fdc4547935c9f76b7f40f7b794631d627289c`）。二重コミットが重なったため`ffa8a424e5a3466c193671ebf11ec583b2f9141c`で重複キー作成を1回に統合。回帰試験も二重登録情報照会と同名変数二重宣言を`072cb8f2b2256bb596d7e45c638d5b2b66ea537f`で整理。最終テストの基準は1回の真の能力調査に集約。
- [**CI #39996917726 — SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/39996917726)：`WIC_HEIC_ENCODER_REGISTERED_RGBA8 count=2 rgb24=1 rgba32=1 rgb48=0 rgba64=0`、`WIC_HEIC_ENCODER_REGISTERED_RGBA8_AND_REJECTS_UNPROVEN_HIGHBIT_PASS`、`WIC_HEIC_RGBA8_ENCODER_AND_ALPHA_READBACK_PASS`。同じCIでデコーダー4形式の登録、8MTAスレッドの1フレーム同時画素読みが全件一致、ICC、人工HEICを4回90度回転させた全画素一致、旧1.1.18とのWIC A/B、WICとサムネイルv0.4独立インストール・ロールバックが合格。通常の製品MSIは**配布禁止のまま**。
- 研究版のバイナリはユーザーの実機Windows 10標準フォト2026.11090.22001.0で**未確認**。今回の正しい画素形式登録やRGBA書戻し成功を「HEVC拡張警告が消えた」「編集・回転ボタンが有効になった」「標準フォトから無再圧縮保存できた」とは扱わない。現時点のP0阻害要因はPhotos本体のHEVC依存判定・内部編集経路／メタデータ保存。既存個人写真、HEIC Manager、ユーザーPCの登録状態には触れない。

**再開点**：最新研究ブランチのコードとCI結果を読み取り専用で再確認してから、今回の24/32/48/64デコーダー広告・24/32エンコーダー広告・スレッド同期を**重複実装しない**こと。次にPhotos GUIが求める編集・保存API経路とHEVC MFT/Store条件を一次データで区別する。CIの成功だけでMSIを提示しない。暫定完成率42%からの進展はWIC契約の範囲に限られ、GUI合格は依然ゼロ。

## Windows 10 Photos向けWICメタデータCOM公開・途中書込み保護（2026-10-24）

**新たに実機ではなくWindows Server隔離CIで実証した3つの問題と修正。これらをPhotos GUI復旧と取り違えないこと。**

1. **HEICフレームのCOMメタデータ列挙**。既存`CHeicBitmapFrameDecode`の`GetMetadataQueryReader`はEXIFのクエリ取得ができるが、`QueryInterface(IID_IWICMetadataBlockReader)`を返さなかった。メタデータを持つフレームで必要となるWICインターフェース。合成人工EXIF HEICだけを用いたテスト`6661020aa4d1d92614703c600253b0012e6b1a83`、SDKヘッダー追加`9cce99c5a9790324494cc420c88bff2180a9c063`、[**赤CI #40001302968**](https://github.com/taku077/wic_heic/actions/runs/40001302968)：`WIC_HEIC_FRAME_METADATA_BLOCK_QI_HRESULT=0x80004002`。製品修正`f8294f7e03924571af0325cc5f4dbd9c57f52537`、`3955d62c793943dbe6ca7c2f54b12f8278fdb4ce`：フレーム自身が`IWICMetadataBlockReader`を公開し、**同一`IUnknown`識別子**で既存EXIF/XMPブロック列挙ロジックへ委譲。実ブロック数、インデックス取得、`IEnumUnknown`列挙、Orientationクエリを人工HEICで確認。[**緑CI #40002299677**](https://github.com/taku077/wic_heic/actions/runs/40002299677)：`WIC_HEIC_FRAME_BLOCK_READER_COM_IDENTITY_AND_EXIF_PASS`。
2. **ロスレスHEIC全体の一部書込み**。`CHeicBitmapEncoder::Commit`は既存非空ファイルへの書込みを拒否するが、空の`IStream`に対して短い`S_OK`書込みが発生すると、破損したHEICプレフィックスが残っていた。故障注入用の人工`IStream`（ユーザーの実ファイルは不使用）テスト`80c442350f4a677b93ca52f4a26b4ff9b0e1cdd0`、[**赤CI #40001983500**](https://github.com/taku077/wic_heic/actions/runs/40001983500)で再現。修正`9d969116b8d5b60fca9c1be1c4a1d4a8c319f188`：短い書込みを`STG_E_WRITEFAULT`として拒否し、最初から空だった出力について`SetSize(0)`と位置0への復旧をベストエフォートで行う。[**緑CI #40002299677**](https://github.com/taku077/wic_heic/actions/runs/40002299677)：`WIC_LOSSLESS_PARTIAL_WRITE_REJECT_HRESULT=0x8003001D`、残存0バイト、`WIC_LOSSLESS_PARTIAL_WRITE_ROLLBACK_PASS`。
3. **HEVC再圧縮中の一部書込み**。別の人工RGB→HEIC出力で故障注入を実施`96f29041db316a7b50e8479698664fcfdc98cbdd`。[**赤CI #40002672209**](https://github.com/taku077/wic_heic/actions/runs/40002672209)：失敗を返すが**830バイトの壊れた出力が残る**。修正`b950474400b8c0df7dcb71e887c58a1ff0bdbe3b`：開始時に空だったストリームへのHEIF raster書込みが失敗した場合にも復旧を試す。[**緑CI #40002977203**](https://github.com/taku077/wic_heic/actions/runs/40002977203)：`WIC_RASTER_PARTIAL_WRITE_REJECT_HRESULT=0x80070057`、残存0バイト、`WIC_RASTER_PARTIAL_WRITE_ROLLBACK_PASS`。
4. 同じ最終CI #40002977203 は、メタデータCOM同一性、ロスレス・通常エンコード両方の短い出力への復旧、ICC 3144 bytes保持、4回90度の6,220,800バイトRGB一致、プライマリHEIFの完全バイトコピー、旧v1.1.18との人工HEIC A/B、研究MSI配布ブロックを維持。ユーザーのIMG_4074などの元写真、PCレジストリ、HEIC Managerには触れていない。

**重要な制約**：`IStream::SetSize`をサポートしない出力では復旧を保証できない。過去に非空だったファイルを自動的に切り詰める機能ではない。WIC APIの妥当性が増したこととWindows 10 PhotosのHEVC拡張依存・編集ボタンの挙動は別である。ユーザーPCで新MSIを導入せず、**HEVC警告、編集・回転、無変形保存のP0機能ゲートは未達、配布保留**。

**次の優先事項**：既存の`PhotosWicTrace`／Media Foundation診断資料を再利用し、可視Photos UIの判定・実保存経路を実機で区別する。古い1.1.18を正常動作版と誤表記しない。旧1.1.18の`QueryCapability=0x15`の誤ったSameEncoder/Thumbnail申告を無条件に復活させない。

## HEIC出力ストリーム部分書込み・再試行の安全性：人工IStreamでの赤→緑（2026-10-24）

**目的**：Windows 10 Photos GUIの編集警告は未解決だが、WIC保存エラー発生時の不完全なHEIC残留は写真保護に直結する。既存の`nonempty-target`拒否は「保存開始前に出力先が空であること」を確認するだけで、空のストリームへHEICを**書込み開始した後**の部分書込みを巻き戻さない。

**限定した人工試験**：
- `wictest/EncoderGuardProbe.h`に、メモリ上の標準`IStream`をラップし、最初の`Write`を**成功HRESULT＋要求バイト数の半分だけ書込み**とする`ShortWriteStream`を追加。既存合成人工HEICの無再圧縮コピーと、明示的生成192×320 RGBのHEVC再エンコード両方で使う。ユーザー写真・実ファイル・レジストリの書換えはしない。テストコミット`80c442350f4a677b93ca52f4a26b4ff9b0e1cdd0`、ビルド用ヘッダー修正`3957fcc425c050f08672705d3568777fb33c97c8`、ラスターテスト`96f29041db316a7b50e8479698664fcfdc98cbdd`。
- **RED①** [CI #40001983500（失敗）](https://github.com/taku077/wic_heic/actions/runs/40001983500)：無再圧縮`Commit`は`STG_E_WRITEFAULT`を返すが`WIC_LOSSLESS_PARTIAL_WRITE_RESIDUAL_BYTES=5298`、半端なHEICが残った。**GREEN①** 製品修正`9d969116b8d5b60fca9c1be1c4a1d4a8c319f188`：書込み失敗時`IStream::SetSize(0)`と`Seek(0)`を**可能な場合に**試行。[CI #40002299677（成功）](https://github.com/taku077/wic_heic/actions/runs/40002299677)：残留0。
- **RED②** [CI #40002672209（失敗）](https://github.com/taku077/wic_heic/actions/runs/40002672209)：新規人工RGBのHEVC再エンコード中にストリームが部分書込みすると、`Commit`は失敗するが`WIC_RASTER_PARTIAL_WRITE_RESIDUAL_BYTES=830`。**GREEN②** 製品修正`b950474400b8c0df7dcb71e887c58a1ff0bdbe3b`：例外経路で出力が部分的に存在する場合に空へ復元。[CI #40002977203（成功）](https://github.com/taku077/wic_heic/actions/runs/40002977203)：残留0。
- **RED③** [CI #40003366880（失敗）](https://github.com/taku077/wic_heic/actions/runs/40003366880)：無再圧縮の最初のCommitはエラー、ストリーム0バイトに復元されたが、**同じエンコーダーへの2回目のCommitが`S_OK`**。HEVC経路では例外までにlibheif内部`m_Context`にフレームが蓄積され得るため、内部が不明な状態でリトライすべきではない。テストコミット`43f07ac6324f03f8b9394538b992a974e3e5dd65`。
- **GREEN③** `m_OutputAttempted`を親エンコーダーへ追加（`f450824ac973cf9a3b0d7143ce98e07667c8a388`、`428aff2f274c98ece6e8157f65753fb2066d7edd`）。必要な前提条件を確認した後で最初の出力を試みた時点で再利用不可とし、失敗しても`Commit`、フレーム追加、ICC変更の再実行を`WINCODEC_ERR_WRONGSTATE`で拒否。[**CI #40003964073（全工程SUCCESS）**](https://github.com/taku077/wic_heic/actions/runs/40003964073)：コピー/ラスタの再試行どちらも`0x88982F04`、元の部分書込み失敗は引き続き失敗HRESULT、残留0バイト。`WIC_FOUR_TURNS_EXACT_RGB_PIXEL_IDENTITY_PASS`、旧1.1.18の公開人工HEIC WIC A/B、メタデータCOM列挙、HEIF画像読戻し試験もPASS。

**安全保証の正確な範囲**：これは**CIで用意した空の、SetSize対応IStream**における不完全な出力除去であり、すべての外部ストリームでトランザクション的保存を保証するものではない。`SetSize(0)`が拒否されるデバイスや、呼出し側が開く時点で既存ファイルをtruncateするケースをWIC内部では復元できない。常に元写真と別の空のファイル名で保存することが前提。ユーザーの既存写真を実機における保存試験へ使わない。

**Photos P0は引続き未達**：Windows 10 22H2の標準フォトでHEVC拡張警告が消えた・編集ボタンが有効になった・JPEG同等の回転保存が実際にできたという証拠はない。旧1.1.18を正常版として扱わず、ユーザーPCへの研究版MSI導入も保留。今回の成果をGUI完成率の根拠として大幅上積みしない。

### HEICの途中書込み失敗後は同一エンコーダーを再利用しない（2026-10-24）

- 人工ロスレス／通常圧縮の短い`IStream::Write`による失敗後、`IStream`を空に戻せても、内部の`heif::Context`が既に更新されている可能性がある。同じ`IWICBitmapEncoder::Commit`を**もう1回**試すと、修正前はロスレスで`S_OK`になり不正な再発行を許した。テスト`43f07ac6324f03f8b9394538b992a974e3e5dd65`、[赤CI #40003366880](https://github.com/taku077/wic_heic/actions/runs/40003366880)：`WIC_ENCODER_FAILED_COMMIT_RETRY_LOSSLESS=0x00000000`。
- 製品修正`f450824ac973cf9a3b0d7143ce98e07667c8a388`、`428aff2f274c98ece6e8157f65753fb2066d7edd`：出力開始を`m_OutputAttempted`に記録し、**成功したCommitだけでなく出力着手後の失敗**も、そのエンコーダーでの再試行やフレーム操作の対象外にする。開始前の引数エラー・フレーム未確定とは区別する。
- [**CI #40003964073 成功**](https://github.com/taku077/wic_heic/actions/runs/40003964073)：ロスレス失敗後／通常圧縮失敗後の再試行は両方`0x88982F04`（`WINCODEC_ERR_WRONGSTATE`）、元の出力復旧後は残存0バイト。`WIC_ENCODER_FAILED_COMMIT_RETRY_LOSSLESS_BLOCKED_PASS`、`WIC_ENCODER_FAILED_COMMIT_RETRY_RASTER_BLOCKED_PASS`。同じCIで`WIC_HEIC_FRAME_BLOCK_READER_COM_IDENTITY_AND_EXIF_PASS`、両経路の短い出力の復旧、ICC 3144 bytes保存、4回90度全RGB画素一致、旧v1.1.18とのWIC合成HEIC A/Bを再確認。
- **残る制限**：これは隔離CIの人工ストリームによるWICエンコーダー安全性の改善である。出力先が`SetSize(0)`に非対応の場合は失敗後の切り詰め成功を保証できない。Windows10 PhotosのHEVC拡張警告や編集ボタンが有効になったという証拠ではない。旧1.1.18の保存失敗・画像変形は未解決。ユーザーPC、原本画像、HEIC Managerには変更なし。研究MSI配布ゲートは閉鎖を維持。


## 2026-10-24 続行：未知の IWICBitmapSource ラッパーに隠されたHEIC回転のメタデータ喪失対策（赤→緑）

**新しい事実**：既に対策済みの標準WIC FlipRotator/FormatConverter/Scaler/Clipper/IWICBitmapとは異なり、元のHEICを90°回転した画素を中継するだけの独自 `IWICBitmapSource` COMオブジェクトが、列挙済み危険インターフェースと私有 `IHeicRawSource` のすべてを隠せる。製品側 `WriteSource` はこの「由来不明の画素ソース」を既定(DontCare)で受理し、元コンテナーにあったEXIF、MakerNote、ICC、HDR/auxiliary等を再圧縮出力へ継承する証拠なく `S_OK` を返す危険が残っていた。

- 試験コミット `6e96594d6908bb319a57bf269c2b0909cb9d1805`、ヘッダー修正 `ae1489602e741787e24daf8b8c5ac8dc02bf2bd8`。`OpaqueSyntheticWicSource` は公開人工HEICのWIC回転画素のみを委譲し、外部へ `IUnknown` と `IWICBitmapSource` 以外を公開しない。入力は公開人工サンプル、出力は空のメモリIStream。ユーザー写真・PC・登録は不使用。
- **RED** [CI #40006588536](https://github.com/taku077/wic_heic/actions/runs/40006588536)：`WIC_OPAQUE_HEIC_SOURCE_REJECT compression=0 hr=0x00000000`、`Opaque HEIC raster silently accepted despite unknown provenance` で意図通りFAIL。先行CI #40006304432 は新規試験コードのヘッダー不足で失敗しており、製品不具合のRED証拠として扱わない。
- 製品修正 `c9644276aebfed417cace20908cae36f5855b012`：生の元HEIC情報が確認できる無再圧縮経路は維持。標準WICラッパーも従来通り個別に拒否し、それ以外の未知の `IWICBitmapSource` については、明示HEVC圧縮指定があっても `WriteSource` ラスター変換を禁止し `WINCODEC_ERR_UNSUPPORTEDOPERATION` を返す。写真の来歴をピクセルだけから推測しない。
- 緑修正の最初のCI #40006976862 は新しい拒否2件が `0x88982F81` でPASSしたが、従来の人工PNG/GIF→HEIC総合テストが暗黙の `WriteSource` 転送を使っていたため失敗。そのため **テスト専用の人工PNG/GIF変換だけを明示的 `WritePixels` に移行**したコミット `e6e7d76444b91e196424be1d2354d0375a426ca9`。これは元HEICの損失許容編集ではなく、新規の人工画素入力によるHEIC作成。
- **GREEN** [CI #40007576374](https://github.com/taku077/wic_heic/actions/runs/40007576374)：全工程 SUCCESS。`WIC_OPAQUE_HEIC_SOURCE_REJECT compression=0 hr=0x88982F81` / `compression=2 hr=0x88982F81`、`WIC_OPAQUE_BITMAPSOURCE_HEIC_PROVENANCE_GUARD_PASS`。また `WIC_FOUR_TURNS_EXACT_RGB_PIXEL_IDENTITY_PASS`（6220800バイト）、フレームメタデータCOM、Exif/ICC、色形式登録4/2、MTA8並列 `CopyPixels`、短いIStream書込みの双方0バイト復旧／Commit再試行禁止、人工PNG/GIF画素エンコード・読戻し、旧1.1.18とのWIC A/Bが合格。ユーザーPC未実施、MSI配布なし。

**重要なトレードオフ**：これによって、元情報を検証できない任意の `IWICBitmapSource` を `WriteSource` でHEICへ新規再圧縮する一般的な互換性も制限される。画素のみの新規ファイル生成は `WritePixels` で可能。ただし、実フォトが未知のラッパーを使うなら安全拒否となり、回転保存を可能にする修正ではない。出自追跡・メタデータ転送が可能な専用編集経路の開発が引き続き必要。Microsoft WIC仕様は `WriteSource` に一般の `IWICBitmapSource` を許しており、この実装の制限は写真保護を優先した研究版ポリシーである。https://learn.microsoft.com/ja-jp/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframeencode-writesource

**標準フォトP0は未達**：HEVC警告解消、編集ボタン有効化、無変形な実GUI回転保存、全メタデータ保持のWindows 10実機試験は成功していない。旧v1.1.18はUI比較基準だけで、変形・保存エラーのある未完成版。HEIC Manager/ユーザー元画像に変更なし。未完成MSI配布ゲートを維持。

## 2026-10-24 WIC登録でAVIF/AV1までHEIC対応と誤認：赤→緑（Photos編集は未解決）

**対象**：`wic_heic/dllmain.cpp` の `RegisterDecoder` パターン登録。製品DLLはlibde265/HEVCを復号に使うHEICコーデックであるが、旧版以来、AV1形式の `ftypavif` も自分のWIC対応形式と登録していた。アプリによる `QueryCapability` の前段で、実際には扱えないAVIFまでWICデコーダー候補に該当させる不正確な登録である。Microsoft一次資料：https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapdecoder / https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nn-wincodec-iwicbitmapdecoderinfo 。

**再現**：
- 公開人工ヘッダー `ftypheic` / `ftypavif` を新規のメモリ `IStream` にのみ構築。WICがHKLMに登録されたデコーダー情報の `IWICBitmapDecoderInfo::MatchesPattern` を使用する。実際のAVIFファイルやユーザーの写真は不要。
- テストコミット `2eccf5f6ac55ffa2138b5b9c6a7bd3562e4a2b94`、[**RED CI #40008770773**](https://github.com/taku077/wic_heic/actions/runs/40008770773)：`WIC_HEIC_DECODER_PATTERN brand=heic match=1` に加え `brand=avif match=1`、期待どおりテスト失敗。
- 製品修正 `7398325f45a3d3e8cb45a47a77f4727e776b01b4`：登録用配列から `ftypavif` **だけを除外**。HEIC/HEIX/HEVC/その他既存HEIFブランドの識別は変更しない。一般のWindows/ユーザー実機レジストリは変更せず、使い捨てWindowsランナー内の登録だけテスト。
- [**GREEN CI #40009079759（全工程SUCCESS）**](https://github.com/taku077/wic_heic/actions/runs/40009079759)：`brand=heic match=1`、`brand=avif match=0`、`WIC_HEIC_NOT_AVIF_REGISTRY_ARBITRATION_PASS`。加えて人工HEICでの4×90度回転後RGB 6,220,800バイト完全一致、メタデータCOM、ICC/EXIF、MTA8同時CopyPixels、short-write両経路の出力復旧とリトライ拒否、未知のソースのメタデータ喪失拒否、独立v0.4.0の隔離導入・元WIC復元の回帰テストが成功。旧1.1.18と研究版双方の `QueryCapability` 既知差 `0x15` 対 `0x0C` はそのまま。

**因果関係の範囲**：旧1.1.18も同じAVIF誤登録を含むので、これは研究版だけで標準フォトのHEVC拡張警告が復活した原因ではない。Windows10 Photosでの実際のHEIC編集・回転ボタン復活、正しい回転保存はなお未達。今回の改善をP0 GUI実証済として進捗率を上げない。

**安全上の制約**：ユーザーPCの旧v1.1.18、未導入の別v0.4.0、HEIC Manager、ユーザー写真には一切アクセス・変更していない。研究版MSI配布禁止、元画像自動上書き・削除禁止。次の課題はPhotos UIからのWIC/MF API呼出し実測と、過去A-B-Aでの編集可否の因果特定。既に実施済みのプロセスDLL/SHAスナップショットを繰り返さない。

## 2026-10-24：実旧MSIのビルド元を復元して8bit強制処理を直接検証

この記録内で「歴史的1.1.18のソース」を `f5b66fe89fa203c89967b46399ed6af61a6161e8` と扱っていた箇所は、**旧MSIそのもののソース参照としては誤り**。実際にCI比較で使用しているSHA-256 `4FE1809A98C0B7EB7E7A505A6105BE57A34052DE74D79A13DB0853A9EC2AACE6` のMSIは [Actions #39433397223](https://github.com/taku077/wic_heic/actions/runs/39433397223)（head SHA `a18b9ab146ca4d6f7b46a86507d7f0d0d882dc87`）で生成された。a18→f5間は59コミット。GitHub Actionsの現行WIC A/Bは過去と同じMSIを使用し、今回そのワークフロー実行のHEADが a18 であることまで GitHub API で自動検証するゲートを追加（`WIC_118_SOURCE_COMMIT_PROVENANCE_PINNED_PASS`）。

### 10bitデコードの実際の旧ソース構造
- a18 `wic_heic/HeicBitmapFrameDecode.cpp` のコンストラクターには `bpc==10` で `GUID_WICPixelFormat48bppRGB` を設定するコードがある。一部の古い枝では `m_Bpp=32` と48bit間の不整合が見られるが、**直後の `// No HDR support for now` が常に `m_chroma=heif_chroma_interleaved_RGB`, `m_PixelFormat=GUID_WICPixelFormat24bppRGB`, `m_Bpp=24` に上書きする**。ゆえに当該MSIでこの不整合のある48bitコピー経路が走ったと断定してはいけない。旧版は10/12bit入力であっても8bit WIC出力に降格する。
- 後続f5および研究版は強制RGB24処理を廃止し、10bitソースをWIC 48bpp little-endian、全16bitレンジへ正規化する。これにより旧版とのRGB24ハッシュ差を生み得るが、UIエラーの因果は未確定。
- 旧MSIの `IWICBitmapFrameDecode::GetColorContexts` は `WINCODEC_ERR_UNSUPPORTEDOPERATION`、f5以降のソースに存在するICC対応処理は旧MSIのバイナリ機能に含まれない。旧バイナリの実測とf5ソース比較を混同しない。

### 人工HEICで実行確認（CI）
- A/B計測 `wictest/WICDecoderCapabilityProbe.h`：人工10bit HEICのWIC frame0 `GetPixelFormat` と、16×8領域に対する元ピクセル形式の `CopyPixels` を使用。バッファのデータ書込みが宣言したバイト幅どおりか、書き込み前の合成センチネルが末尾に残らないかを測る。ユーザー写真・画像ファイルへの書込みなし。
- ソース検証の途中で、旧版で48bppが実行されると誤認したテストを作成し [CI #40010897316](https://github.com/taku077/wic_heic/actions/runs/40010897316) で不合格。「旧v1.1.18はRGB48ではない」とログを採取し、旧版コンストラクターの最終的なRGB24強制書換えを発見。修正したテストは **旧24bpp／新48bpp** を個別に正当な仕様として照合する。
- [**CI #40011973420 全工程SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40011973420)：`WIC_118_SOURCE_COMMIT_PROVENANCE_PINNED_PASS`、`WIC_PHOTOS_NATIVE_HEIC_WIC_BPP=24`（旧版）、`=48`（研究版）、両方の小領域8行が実際に更新され、`WIC_ACTUAL_118_FORCES_RGB24_WHILE_RESEARCH_PRESERVES_RGB48_PASS`。このCIでRGB24フル画像の旧版・研究版FNV64は依然不一致 `WIC_PHOTOS_AB_FULLRES_RGB_FINGERPRINT_MATCH=0`。加えて研究版の4×90度回転（RGB 6,220,800バイト完全一致）、無圧縮保存、ICC/EXIF、部分書込み0バイト復旧、旧MSI復元・プライベートv0.4.0共存ゲートはすべて再合格。
- 実行結果の限界：CIはWindows Server 2022でWIC APIを検証したものであり、Windows10標準Photosの「HEVC拡張警告」消失、編集ボタン有効化、回転保存の成功や劣化無しを証明しない。ユーザーPCのv1.1.18・写真・HEIC Managerへ変更なし、新MSI配布禁止を継続。

**開発判断**：旧GUIでボタンが有効だったという理由で旧RGB24強制処理を研究版へ無条件で戻すと、10bit画像の精度低下となる。研究版の48bpp保持を守りつつ、**8bitソースを使った旧MSI／研究版のPhotos編集可否比較**と、Photos GUIのWIC/MF経路を別途切り分ける。GUIで警告が復活する原因が10bit処理だと決めつけない。

## 2026-10-24：8bit HEICで旧1.1.18と研究版の全RGB画素が一致（10bit差を除外したA/B）

**目的**：これまでに旧1.1.18（正しいMSIビルド源 `a18b9ab146ca4d6f7b46a86507d7f0d0d882dc87`）と研究版で10bit HEICのフルRGBハッシュが不一致だった。しかし旧版は10bit入力でも強制RGB24に降格し、研究版はRGB48を保持するため、bit depth以外の「通常の8bit HEICに共通する編集不能の原因」と同一視できなかった。8bit HEICの旧新版を同一ファイル・同一CIで直接比較する。

**実施（GitHubのみ、旧コードに巻き戻さない）**：
- テスト `wictest/WICDecoderCapabilityProbe.h::SurveyExistingEightBitGeneratedFixture` 追加（コミット `5440ee2642af9b598f3000d3d7fc3a39e5c70320`）。先行する既存CIのWIC実体試験で新規生成した2ファイル `samples/out-01.heic`（PNGからの人工8bit新規エンコード・512×512）および `samples/icc-roundtrip.heic`（合成8bit RGB・64×64、Windows標準sRGB ICC含む）を、歴史的MSIと研究MSIのそれぞれに読み取り専用で渡す。
- それぞれ `GetFrame(0)`、`GetPixelFormat`、`CopyPixels`、バイト数、FNV64、各チャンネルの総和と隣接値変化数を採取。合成入力以外の写真の使用なし。旧版・研究版共に別プロセス2回のRGBハッシュ安定性を確認。
- 統合ワークフロー比較 `c090d27490860d13de233784c9e270c5cb2056bd`、PowerShell集計変数の初期化構文修正 `921400aef69351ed64ce10e2954792e96d87f099`。[**CI #40013264441 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40013264441)。通常の旧1.1.18 WIC A/B、4回90度無再圧縮回転画素完全一致、ICC/EXIF、シミュレーション部分書込み復旧、v0.4.0との隔離共存も全工程合格。

| 実測 | 旧1.1.18 | 研究版 |
|---|---|---|
| 8bit人工HEIC、ICCなし | 512×512・WIC RGB24・CopyPixels成功 | 同一 |
| ICCなしRGB24バイト数 | 786,432 | 786,432 |
| ICCなしFNV64 | `D0E71AB91FB01ACA` | `D0E71AB91FB01ACA` |
| ICCなしRGBチャネル総和 | `23472228,34605484,53461087` | 同一 |
| ICCあり人工HEIC | 64×64・WIC RGB24・CopyPixels成功 | 同一 |
| ICCありFNV64 | `2C37F79435939325` | `2C37F79435939325` |
| 別プロセスでの画素ハッシュ | 2回とも一致 | 2回とも一致 |
| 元10bit HEICのWICネイティブ形式（従来CI） | RGB24に強制変換 | RGB48で高精度保持 |

**解釈**：テスト対象となった2種の8bit HEICについて、RGBの全バイト一致が成立。10bitのRGB差を、そのまま通常8bitの写真に一般化できない。Photosの編集ボタン無効・HEVC拡張警告が「8bitでのRGB画素の相違」だけに起因するという説は弱まった。ただし、これは *Windows Server 2022 WICによる人工ファイル2件の結果* で、Windows10 PhotosのGUI内経路、8bit HEIC全般の復号精度、特有のICC/EXIFの採用、Media Foundation HEVC判定までは測らない。旧1.1.18がGUIで編集有効でも、回転画像破損・保存不良の不具合版であることを変更しない。

**次の調査対象**：bit-depth非依存のWIC `QueryCapability`／利用できるメタデータ／エンコーダー選択の差、および標準PhotosのWindows10実GUI固有の編集可否判定。既存診断のプロセス・SHA照合と旧v1.1.18の再インストールを反復しない。元画像・HEIC Manager・Windows10環境は無変更。新MSI配布保留。

## 2026-10-24：Media Foundation HEVC登録への影響なし／8bit WIC編集事前条件のA/B確定

**目的**：旧WIC v1.1.18で標準フォトの編集・回転ボタンが押せた一方、研究版WICではHEVC拡張警告が出て無効になった理由を、画素一致後も残るWIC能力申告とMedia Foundation MFT可視性の差に分離する。Windows10実GUIを動かせない隔離CIの制約を守り、過去のMF読取専用ツール `diagnostics/photos_trace/HevcMfReadOnlyProbe.cpp/.vcxproj` を再利用し、列挙ツールを重複開発しない。

**コード／CI**：
- `wictest/WICDecoderCapabilityProbe.h` へ、先のA/BでRGB24全バイト一致済みの人工8bit HEIC 2種類（512×512の通常RGB／64×64のsRGB ICC付き）の `QueryCapability`、デコーダー／フレームのサムネイル取得、ICC個数・HRESULTを追加（`5572b2cf1606d7e61c3daced957d190628a2c076`）。元ストリームの読み込み位置が書き換わらないことも検査。既存の人工ファイルだけを読取り、保存や原本変更はしない。
- `.github/workflows/photos-wic-postfail-arbitration-ci.yml` に、Media Foundation **未導入／実際の1.1.18 MSI導入／研究版MSI導入** の同一Windows Server 2022上の `MFTEnumEx` 観測を追加。同じABIと同じ読取専用プログラムを使い、HEVC decoder/encoder・field-of-use込decoder・対照H264 decoderの登録数を比較する。Windows環境への変更は使い捨てCIランナーのWIC MSI導入／削除のみ。アプリ起動、HEVC MFTの活性化、ライセンス操作なし。
- CI設定の最初の編集で、PowerShell正規表現 `$'` がJavaScript文字列置換の特殊シーケンスと衝突しワークフローが重複。これは製品DLLの不具合ではない。直後に**元の検証済みCIファイルから再構築し、単一ジョブ・単一P0リリース停止ゲートを検証**（`2429abf646b9b7ddeeb5fd51a21c2f38e6bd35b7`）。
- [**CI #40069180176 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40069180176)。WICの通常回帰テスト全合格、4回90°無再圧縮RGB 6,220,800バイト一致、部分書込み失敗回復、ICC/EXIF、独立v0.4.0インストール分離も維持。`WIC_PHOTOS_8BIT_CAPABILITY_THUMBNAIL_ICC_OBSERVATION_PASS`、`WIC_PHOTOS_HEVC_MFT_INVENTORY_UNCHANGED_ACROSS_BOTH_WIC_MSIS_PASS` 合格。

**Media Foundationの実測**（Windows Server 2022の使い捨てCI VM、同一ランナー・同一ユーザー文脈）：

| MFT登録 | WICなし | 旧v1.1.18導入 | 研究版導入 |
|---|---:|---:|---:|
| HEVC decoder（通常MFT） | 0 | 0 | 0 |
| HEVC decoder（field-of-useも列挙） | 0 | 0 | 0 |
| HEVC encoder（通常MFT） | 0 | 0 | 0 |
| H264 decoder（対照） | 1 | 1 | 1 |

**人工8bit HEIC 2種類のWIC機能申告実測**（旧／研究版）：
- `QueryCapability`：ICCなし・ICCありの両方で **`0x15`／`0x0C`**。旧MSIは実際に存在しないサムネイル機能を広告する。両版のデコーダー／フレーム `GetThumbnail` は `WINCODEC_ERR_CODECNOTHUMBNAIL=0x88982F44`。
- ICCなし：旧版のコンテナー／フレームの `GetColorContexts(0,nullptr,...)` はともに `WINCODEC_ERR_UNSUPPORTEDOPERATION=0x88982F81`、研究版はともに `S_OK`／0個。
- ICCあり：旧版は同じ `WINCODEC_ERR_UNSUPPORTEDOPERATION`／0個。研究版はコンテナー／フレームとも `S_OK`／1個。前ターンでCI合格した全RGB画素一致と両立している。
- Microsoft仕様：`WICBitmapDecoderCapabilitySameEncoder` は**同一ベンダーのエンコーダーで生成されたことを認識できる場合だけ**申告。画像に存在しないサムネイルを申告することも不正。https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapdecoder
- MicrosoftのWindows10フォトは静止画だけでもHEIFとHEVCの両拡張を要求する場合がある。https://support.microsoft.com/ja-jp/windows/apps/photos/photos-app-video-editor-error-can-t-view-this-file-type

**限定された結論**：両MSIは少なくともCIランナーにHEVC Media Foundation MFTを導入・登録しない。旧WICで標準フォトUIが有効化されたユーザー観測を「HEVC MFTの登録が増えたから」と解釈する根拠は見当たらない。WICの機能申告とICC APIは異なるが、**どれがPhotosのGUI状態を決めたかは未確定**であり、旧版の誤った`SameEncoder`／`Thumbnail`宣言をコピーしても正しい修正にならない。

**P0継続**：CIのWindows Server 2022はWindows10 Photos 2026.11090.22001.0のGUIを起動せず、HEVCのStore AppXライセンス／MFT実活性化も検証していない。ユーザーPCでのHEVC AppXなし／MF 0件の実測は別の過去診断で確認済みだが、過去のA-B-Aの全時点に同じMFT状態だったことは証明されない。実機Photos編集ボタン・変形なし回転保存は未達。試験WIC MSIを再提供せず、ユーザー写真／HEIC Manager／グローバルWIC登録は変更していない。

## 2026-10-24：Microsoft純正HEIFとのコーデック競合／コンテナーGUID不一致を検証

### 調査の必要性
Windows Server 2022の従来CIでは、旧v1.1.18と研究版それぞれの自作WICデコーダーを既定で選べることは確認済み。しかし、ユーザーのWindows10にはMicrosoft HEIF拡張が存在し、標準フォトがMicrosoftネイティブのHEIFデコーダーやエンコーダーへ直接到達する可能性を排除できない。WIC登録パターン、COM生成の可否、WICデコーダーの自動選択、エンコーダー形式GUIDを分けて検証。

### 実行した検証と根拠
1. COM WICの登録全デコーダーに公開人工HEICのパターン照合を実施（試験32f221e8ffdbc713a01a3c46236f7f2d091e6759、CIゲート87d1117f1a700f9d5d3f9045c985258a825073a3）。[CI #40069701380 全工程SUCCESS](https://github.com/taku077/wic_heic/actions/runs/40069701380)。登録全15件のうち、**2件**が同一HEICパターンに一致：Microsoft純正 {E9A4A80A-44FE-4DE4-8971-7150B10A5199} と自作 {6215667C-74CA-4076-A769-3F834C3F25F5}。この候補数は実旧MSI・研究版のどちらを入れても同じ。WIC自動選択では両版とも自作デコーダーが選択された。
2. Microsoft純正デコーダーを自動選択によらず COM IWICBitmapDecoderInfo::CreateInstance で直接呼び出し、成否を観測（試験3ed15a7c8e9459dcc2b1e9d3a42a03a9c72d23a4、CIゲート2f6640894a52e9ab82ab1b64168c39f635b5aa45）。[CI #40070030479 全工程SUCCESS](https://github.com/taku077/wic_heic/actions/runs/40070030479)。**旧／研究版とも HRESULT 0x88982F8B：WINCODEC_ERR_COMPONENTINITIALIZEFAILURE**。公式エラー表：https://learn.microsoft.com/ja-jp/windows/win32/wic/-wic-codec-error-codes 。純正HEIFの登録は存在するが、このServer環境ではCOMインスタンス生成が失敗し、フレーム読込に到達しない。MFのHEVC MFT数は両版で0件。これは**Windows10実機での失敗を証明するものではない**。
3. 純正HEIFと自作HEICのコンテナーGUIDを比較し、どちらの形式でWICエンコーダーを探すか別々に観測（試験4d38cea235e3533edcbbf2983bcc388fb6c3013a、CIゲート56fe6d603a5db1ff1c8cc23a51919b72c2a6a2fc）。[CI #40070235237 全工程SUCCESS](https://github.com/taku077/wic_heic/actions/runs/40070235237)。公式GUIDは {E1E62521-6787-405B-A339-500715B5763F}、自作登録のGUIDは {98CE31DD-5435-41BD-A1AF-29C387C20135}。両版とも異なる値で、GetContainerFormatは自作GUIDを返す。公式GUIDを指定したエンコーダー検索・自作ベンダー指定検索・デコーダー検索はいずれも当該CI環境で 0x88982F8B（自作エンコーダーが標準GUIDにより選択されたとの証拠はない）。Microsoft公式ID資料：https://learn.microsoft.com/ja-jp/previous-versions/windows/desktop/legacy/mt846532(v=vs.85) 。
4. WICのデコーダー調停は複数の登録パターン候補の間で QueryCapability を評価する。レジストリ ArbitrationPriority は旧・研究版とも0。公式情報：https://learn.microsoft.com/en-us/windows/win32/wic/-wic-howwicworks ／ https://learn.microsoft.com/en-us/windows/win32/wic/-wic-generalregentries 。旧版の能力申告0x15（実在しないサムネイルと根拠のないSameEncoder）を研究版へ戻すとWIC仕様違反になるため変更しない。

### 解釈と禁止事項
- **Microsoft純正デコーダーの登録があるだけでは実際の利用能力を保証しない**ことをCIで再現できた。ただしServerのStore拡張・MF HEVC環境はユーザーWindows10と異なる。
- 独自のコンテナーGUIDは両版に共通であり、旧1.1.18で編集ボタンが押せたのに研究版では無効という退行の単独原因にはならない。標準フォトが実際にどのGUIDでエンコーダーを探索するかは未確認。
- 登録GUIDを強制的にMicrosoftへ差し替えると既存WICの登録衝突・互換性破壊が発生する可能性があるため、研究版DLLに変更を加えていない。レジストリ、ユーザーPC、ユーザー写真、HEIC Managerに変更なし。MSIの完成版配布は禁止を継続。
- 画像の無再圧縮4回90度の全画素一致、ICC/EXIF、部分書込み拒否とv0.4.0独立性は最終CIで再合格。Windows 10 Photos編集・回転保存のGUIテストは0件で未達。次は純正／自作コーデックのWindows10実環境での選択経路と、Media Foundation/Store HEVC使用可否を一度の安全な診断で区別できるよう準備する。

## 2026-10-24：Win10の純正HEIF COM活性化状態を画像なしで測る単発診断をCI完了

**背景／対象**：旧v1.1.18と研究版のWindows Server 2022隔離CIではMicrosoft純正WIC HEIFが登録されていても COM生成で WINCODEC_ERR_COMPONENTINITIALIZEFAILURE 0x88982F8B となり、MF HEVC decoder/encoder=0だった。ただしユーザーWin10にはMicrosoft HEIF画像拡張が入っているので、その実機に本当に同じ初期化失敗があると断定してはならない。既存の `HevcMfReadOnlyProbe.exe` と `Run_Photos_HEVC_ReadOnly.cmd` を増強し、未解決P0の*実機固有の分岐条件*を取れるようにした。新MSIやユーザー写真は用意しない。

- Nativeコード `diagnostics/photos_trace/HevcMfReadOnlyProbe.cpp`（主修正 `88dfd798d40f324e169e7d595dd36d4e3e69e7a9`、SDK `windowscodecs.lib` リンク `7f99de67ce294e8faad769d487d2f0bc04d59d23`）。従来のMFTEnumEx 4種に加え、純正HEIF decoder／自作WIC HEIC decoder／自作HEIC encoderのWICコンポーネント登録とコンテナーGUIDを取得。引数なしはCOMコーデックインスタンスを作らない**受動照会**。明示引数 `--wic-activate-no-image` はWIC `CreateInstance` と公式HEIF GUID `CreateEncoder` の結果を追加採取する。ユーザー画像ファイル、Photosアプリ、レジストリ変更、MFT活性化、Storeライセンス操作はコード中に存在しない。
- 起動用 `diagnostics/photos_trace/Run_Photos_HEVC_ReadOnly.cmd` は上記の引数付き1回の画像なしCOM診断と従来のWIC/AppX/Photos情報の読取専用PowerShellの両方を実行。片方が失敗した場合でも終了コード0と誤報していた問題を `67fd94b0f28e6f9a715b195e683dc2b5108bbf2c` で解消。レポート／JSONは従来どおり新規ファイルに保存、原本を変更せず、元写真を読まない。**ただし外部WIC COMインスタンスを生成するので、そのDLL内部の初期化・OSキャッシュの副作用まで保証するものではない**と説明した。
- 独立CI `.github/workflows/photos-hevc-mf-readonly.yml` を作業ブランチでも自動実行するように変更（`63e58929ee8ee9fc1b7aafd2ca310aa121324c43`）。受動照会、明示COM活性化、未知の引数の拒否を検査する。最初の CI #40071187133 は検査上*意図的に返したエラーコード2*がpwshのシェル終了コードに残ったためFAILED。検査後にその意図的エラーコードをクリアした修正 `9308caaba83577fe773f3c61c6cb47b7a9634ed8` で回復。実行プログラム本体の不具合ではない。
- [**最終CI #40071379364 全工程SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40071379364)、コードヘッド `a7dc49125760166a482453fc144f62752808f773`：`HEVC_WIC_PASSIVE_METADATA_CI_PASS`、`HEVC_WIC_ACTIVE_NO_IMAGE_CI_PASS`、`HEVC_WIC_UNKNOWN_ARGUMENT_REJECTED_PASS`、`HEVC_MF_INVENTORY_ISOLATED_WINDOWS_CI_PASS`、`HEVC_MF_SINGLE_READONLY_DIAG_BUNDLE_PASS` がすべてPASS。Microsoft HEIF登録情報から公式コンテナーGUIDを取得、純正HEIF CreateInstance は例のCI VMでは 0x88982F8B、HEVC MF decoder/encoderとも0件。独自のWICはこの診断ワークフローではMSIを導入しないため未登録（0x88982F50）；ユーザー実機でも未登録とは解釈しない。
- 最終CIはダウンロード可能な診断用アーティファクト `Photos_HEVC_ReadOnly_Win10_x64`（artifact ID `12551053893`）を保存した。保持期限 **2026-11-07 UTC**。ワンクリックCMD、上記診断用EXE、既存PowerShell、更新済みREADMEの4点。**完成MSIではない**。
- 次の実機判定で欲しい差は「Microsoft HEIFの登録があるがCOM生成が失敗するか／Windows10では生成成功するか」と「自作WICは存在しCOM生成できるか」。これを現行HEVC AppX/MFT状態と結びつけ、既にWICで画像を読めるのにPhotosで編集できないという不一致の原因追跡に使用する。Windows10 PhotosのGUI操作／回転保存成功はまだ0件。**実機未実施を明記し、完成進捗45％を安易に上げない**。

## 2026-10-24：Windows10実機 HEIF/MF/WIC 診断到着・純正COM成功（Serverとの差を確定）

**出所**：ユーザーが実機で既存 `Photos_HEVC_ReadOnly_Win10_x64` 診断を実行し、レポートと `WIC_Preflight` JSONを提供。診断日時は2026-10-24、Windows10 Pro 22H2 build19045、Photos 2026.11090.22001.0。ZIP内JSONと別添JSONは全バイト一致。レポートでは native MF/WIC診断およびPowerShell事前確認のプロセス終了コードがともに0。実機のユーザー名、絶対パス、製品のGUIDキー、インストールDLLのハッシュやJSON原本をこのリポジトリへアップロードしない。

**個人情報を除いた実測結果**：

| 呼出し・状態 | Windows10実機（旧v1.1.18導入） | GitHub Windows Server 2022（旧／研究版両状態） |
|---|---|---|
| Microsoft純正WIC HEIF decoder: CreateComponentInfo | S_OK | S_OK |
| Microsoft純正WIC HEIF decoder: CreateInstance | **S_OK** | **0x88982F8B** |
| 自作WIC HEIC decoder: CreateComponentInfo / CreateInstance | S_OK / S_OK | 自作導入時利用可 |
| 自作WIC HEIC encoder: CreateComponentInfo / CreateInstance | S_OK / S_OK | 自作導入時利用可 |
| Microsoft公式HEIF形式GUID CreateEncoder | **S_OK** | **0x88982F8B** |
| MF HEVC decoder / encoder列挙（通常・field-of-use） | **0 / 0（追加decoder 0）** | **0 / 0** |
| MF H264 decoder列挙（対照） | 1 | 1 |
| AppX HEIF Image Extension（現在のユーザー） | **インストール済み・Status=Ok** | 同じAppX環境とは限らない |
| AppX HEVC拡張（現在のユーザー一覧） | **列挙なし** | 未比較 |
| 自作WICの導入版 | **旧 v1.1.18** | 旧v1.1.18と研究版のA/B（研究版のWin10実測ではない） |

**確定できること**：
- **MF HEVC decoderの列挙数0だけでは、Microsoft純正HEIFのWICインスタンスが作れないと予測できない。** Windows10ではMicrosoft純正HEIF decoderがCOM生成でS_OKを返すため、直前の「CIで純正のCOM生成が失敗する」という観測は実機に外挿できない。
- 純正HEIF形式のEncoderもCOM生成でS_OK。以前のWindows Server CIにおける純正形式のCreateEncoder失敗は**実機の保存経路が利用不能という根拠ではない**。
- 自作デコーダー・エンコーダーもCOM生成S_OKであり、DLL未登録・COMクラスが見つからないという単純な故障説は**現在の旧1.1.18導入状態では支持されない**。
- **COM生成S_OK、WICの登録、MFのMFT列挙0、AppX HEVC表示なしは相互に矛盾しない**。各層は別API。WICインスタンス生成はHEIF画像の実復号／回転保存／HEVCライセンス／Photos UI編集有効を証明しない。
- この実機診断は**旧v1.1.18がインストールされた状態の一時点**のみ。研究版WICを実機へ入れ替えたA/Bではないため、研究版でPhotosの警告・編集不可になる理由をこのログだけでは確定できない。
- HEVC AppXは現在ユーザーの列挙に出ていないが、他ユーザー／システムコンポーネント／HEVC機能全般の不存在までは証明しない。

**原因仮説の更新（優先順位）**：前ターンのCIから生じた「純正HEIFが実機でも初期化失敗してフォトを止めた」という仮説は棄却。次はWindows10上の**実際のHEIC画素処理／エンコーダー選択**とPhotosの編集ボタン判定に着目。旧版0x15／研究版0x0CのWIC能力申告、公式HEIF形式GUIDと自作HEIC形式GUIDが異なる事実はCI検証済みだが、ボタン無効化との因果はまだ未確定。誤ったSameEncoder/埋込みサムネイルの申告を戻さない。旧v1.1.18も回転後画像破損や保存エラーがある**不具合版**であり、正常版扱い禁止。

**次回の対照試験の設計**：人工HEICのみ使用、ユーザー写真・原本を開かない。実機が今の旧WICのままでも安全に比較できる技術的ステップとして、Windows10側のMicrosoft純正／自作WICが合成HEICを本当に復号できるか（COM生成ではなく`Initialize/GetFrame/CopyPixels`）と、同一合成画像で選択されるエンコーダー情報を区別できる**単発の自動検証**を検討する。ただし同じMFT/COM診断の反復、レジストリ変更、新MSIの配布を行わない。GUI回転保存のP0未達、研究版Win10実機未検証、完成率45％は変更しない。

## 2026-10-24：実機COM成功を受けた人工HEIC限定・ハッシュ認証済み実復号診断

Windows10実機のユーザー提供診断では、Microsoft純正HEIF decoder・純正HEIF encoder・自作HEIC decoder/encoder のCOM生成が**すべてS_OK**で、Media FoundationのHEVC decoder/encoder登録数は0。一方、旧WIC v1.1.18での診断だったため、研究版のWindows10 GUI編集可否とは直結しない。実機レポートとユーザー環境のパス、DLLハッシュ、MSI製品キーの原本はGitHubへコミットせず、先の節に技術的事実のみ匿名化して記録。

次の実証段階として、既存の `diagnostics/photos_trace/HevcMfReadOnlyProbe.cpp` に**独立オプトイン** `--synthetic-wic-test` を追加（コミット `e040771547f03378e5215cf5c8a2793d52febad0`）。実行ファイルの隣にある固定の公開人工HEIC `fixture-irot3-exif6-noaux.heic` だけを読む。10,597バイトかつGit blob SHA1 `126b24780e8c36a0e1521df982067ab12f971470` をWindows CryptoAPIで検証し、任意入力・リパースポイント・ハッシュ差を拒否。純正／自作WICをCLSID指定で別々にインスタンス化し、`Initialize`、`GetFrame`、WIC RGB24変換、メモリへの16×8画素`CopyPixels`のHRESULTと合成データの指紋を報告。ファイルや写真の上書き、レジストリ更新、Photos起動、MSIインストールは一切しない。元の画像なし診断の引数なし・`--wic-activate-no-image` は維持。

別CMD `diagnostics/photos_trace/Run_Photos_WIC_Synthetic_Only.cmd` も追加（`22382a4b89f46ddc6f10e7d3f9fb5734028abac7`）。元の診断CMDから人工HEIC試験を自動実行することはない。ベンダーのDLLは実行プロセス内にロードされることがあるため、ユーザーが明示的に選択するモードに限定。

[**専用CI #40072408037 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40072408037)：Windows x64 C++ビルド、通常の画像なし診断、人工HEICのみの試験の起動・SHA検証、使い捨てコピーを1バイト変更した場合にエラー8で拒否する検証、6ファイルの成果物組立て、アップロードまで完了。旧v1.1.18や研究版はこの隔離診断用VMには導入していないため、**自作WICの`CopyPixels`成功は現時点で確認できていない**。MSI提供とは無関係。

実WIC A/B統合CIへ当該人工復号テストを増設した一時試験（`c1f497cb39ee25243a5525d99a411424eb2bbf05`）はジョブ生成前の失敗となり原因未確定。既存WIC製品の不具合と解釈せず、成功済みの従来ワークフローの全内容に復元（`e10a178f3f6252e70102b8e7c5ad35741a7c2f52`）。[**復旧後の統合CI #40072685441 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40072685441)。既存WICの保護機能、人工HEIC回転テスト、WIC旧研究A/Bおよび独立サムネイルMSI試験は変えていない。

**判定限界と次**：この新コードの実機人工画像復号はユーザーがまだ実行していない。実行結果でMicrosoft純正HEIFが実際にHEICを復号できるかを把握できるが、それでもPhotosの編集ボタン判定や回転後保存までは実証できない。古い1.1.18の画像変形・保存エラーは依然あり、正常版扱いしない。既存写真・HEIC Managerに変更なし。完成率45%を据え置く。

## 2026-10-24：実機で純正HEIFの画素展開失敗／自作WIC成功を実証

**入力と出所**：ユーザーが追加提出した診断ZIP `Photos_HEVC_ReadOnly_Win10_x64(2).zip` に、Windows10 Pro 22H2で `Run_Photos_WIC_Synthetic_Only.cmd` を実行した `Photos_WIC_Synthetic_26335_12625.txt` が同梱。ユーザーの実写真・パス・レジストリ・DLLコピーはGitHubには保存しない。

**人工ファイルの完全性**：`fixture-irot3-exif6-noaux.heic` は**10,597バイト**、Git blob SHA1 **`126b24780e8c36a0e1521df982067ab12f971470`** が既存の公開合成fixtureと一致。SHA256 **`d874948c2e6f218a45c740a270fe5e10f0344c8f3a525b59fd44fdce898dcc95`**。任意の実写真を使用していない。

**明示的WICデコーダー選択の実測**：

| WIC API段階 | Microsoft純正HEIF | 旧v1.1.18自作WIC |
|---|---|---|
| COM ComponentInfo | S_OK | S_OK |
| COM CreateInstance | S_OK | S_OK |
| Initialize（HEICストリーム） | S_OK | S_OK |
| GetFrame(0) | S_OK | S_OK |
| WIC RGB24コンバーター初期化 | S_OK | S_OK |
| **16×8 CopyPixels** | **0xC00D5212 失敗** | **S_OK 成功** |
| 合成RGB24 ROI FNV64 | 算出できない | **2C46D5C491E58AE4** |

この`0xC00D5212`はMedia Foundationの `MF_E_TOPO_CODEC_NOT_FOUND`（コーデックが見つからない）として知られるHRESULT。報告済みのHEVC Media Foundationデコーダー列挙0件、ユーザー向けHEVC AppX表示なしと整合する。Microsoft公式はWindows10のフォト利用についてHEIF画像拡張とHEVCビデオ拡張の両方を、静止画だけの場合でも必要とする旨を案内：https://support.microsoft.com/ja-jp/windows/apps/photos/photos-app-video-editor-error-can-t-view-this-file-type 。Microsoft HEIF WIC文書もHEVCエンコーダーの存在確認に MFTEnumEx を挙げる：https://learn.microsoft.com/ja-jp/windows/win32/wic/heif-codec 。

**ここまでで実証した因果・非因果の区別**：
- 純正HEIFのコンテナー検出・COM生成・フレーム参照・RGB形式交渉は成功し、その**後段の復号段階**で失敗している。単純なDLL登録エラーや壊れた画像では説明しづらい。
- **同一バイトの人工HEIC**が旧v1.1.18自作WICで正常にRGB展開できたため、OS純正HEIFの失敗はそのファイルのHEICコーデック非対応（特にHEVCデコーダー不在）と高い整合性を持つ。
- **Photosの実際のデコーダー選択・HEVCライセンス判定・編集ボタン非活性の直接原因までは未証明**。この診断は純正／自作をそれぞれCLSIDで明示選択しているので、フォトが純正を選んだかは未確定。旧v1.1.18は回転で画像を変形・保存失敗する不具合を持ち、正常版扱い禁止。
- HEVCの有料拡張をユーザーに買わせる／ライセンス検査を偽装する／WICの不正な`SameEncoder`・サムネイル機能申告を戻す手段は採用しない。Windows10純正フォトの独自の拡張機能チェックがある場合、WIC DLL改善だけでは解決できない可能性が高い。

**この証拠を基にした追加実装**：固定人工HEICの`CreateDecoderFromStream`（ベンダー指定なし）による**Windows WICの自動選択と選択デコーダーCLSID・16×8 CopyPixels**を別途ログする処理を `diagnostics/photos_trace/HevcMfReadOnlyProbe.cpp` に追加（コード `bc18229a6a736445cd870a208c956526d70d9c80`）。固定ファイルの厳格ハッシュ確認や任意ファイル拒否は変更しない。 [CI #40074946545 SUCCESS](https://github.com/taku077/wic_heic/actions/runs/40074946545) で新ステージ実行、安全性試験、人工ファイル改変時の拒否に合格。**CIランナーでは純正WIC生成エラーで自動選択自体が失敗（0x88982F8B）**、Windows10でどちらが自動選択されるかは引き続き実機確認が必要。

**次に必要な実証**：追加した自動選択測定をWindows10の同じ人工HEICに対して一度だけ行い、もし純正デコーダーが選ばれて`CopyPixels=0xC00D5212`となるならWICの通常経路の選択問題を優先。自作WICが選ばれて正常にCopyPixelsできるのにPhotos編集不可なら、標準フォト固有のHEVC拡張判定または別のエンコーダー／編集経路に重点を移す。製品機能の最終修正はなお未完成。実機で試していない研究版MSIの提供・インストール、ユーザー写真、HEIC Managerの変更は実施しない。

## 2026-10-24：自動WIC経路・全画素測定と二重ハッシュ検証をCI合格

**位置付け**：Windows10ユーザーの人工10,597B HEICで、Microsoft純正HEIF `CopyPixels=0xC00D5212`、旧自作WIC `CopyPixels=S_OK` まで確認済み。この結果はPhotosの自動選択を示さないため、単なるCOM登録比較を打ち切り、WIC自動選択と画素全体の再現性を人工画像のみで調査する。

1. `HevcMfReadOnlyProbe.cpp` へ任意モード `--synthetic-wic-test` 内の `IWICImagingFactory::CreateDecoderFromStream`（ベンダー指定なし）を追加。選ばれたCLSIDがMicrosoft純正か自作か、`GetFrame`／RGB24変換／`CopyPixels`を直接記録。通常の画像なし診断には影響なし。コミット `bc18229a6a736445cd870a208c956526d70d9c80`。
2. 同じ合成人工HEICのフレーム寸法、および**全画素RGB24のFNV64指紋**を追加。最大64MiBに制限し、合成画素や画像ファイルは保存しない。16×8の成功だけでは検出できない、画像後半のコピー失敗・幾何異常や旧研究の全画素差を後から調べられる。コミット `f065c976c58e4a72ef105c1c3364d8f54c5f6cf1`、`b0003348949a95963ff555d33097f5382b666a7f`。
3. SHA-1 Git blob ハッシュ（`126b...`）に加えて、人工HEICコンテンツの**独立SHA-256 `D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95`**をWindows CryptoAPIで検証するようにした。不一致なら復号拒否。コミット `0d9f0c7afa475ddcc5ade7014fe2bd8bed17dcc1`、ワークフロー `190d94fc5556a028622dc1f11d41c6d48e597a9d`。
4. [**CI #40075430836 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40075430836) でWindowsビルド、SHA-256照合、WIC自動選択実行、安全な改ざん拒否、通常の画像なし診断、診断パッケージ6点の梱包すべて合格。人工画像のハッシュを変えるとS_OKで先へ進まない。生成物 `Photos_HEVC_ReadOnly_Win10_x64`、artifact ID `12552782202`、保持期限2026-11-07 UTC。既存のユーザー写真やOSに変更はない。
5. **CIランナーの解釈に限界**：自作MSIがないWindows Server 2022では純正HEIFのCOMが0x88982F8Bで生成不能となり、自動選択も0x88982F8B。Windows10実機は純正HEIFも自作WICもCOM生成できるため、CIの結果を実機へ外挿しない。実機で人工HEICの自動選択CLSIDとCopyPixelsを採取すれば、通常のWIC経路が自作を選んでいるかを検証可能。
6. Windows10の純正HEIF側 `0xC00D5212` とHEVC MFT列挙0件は公式MicrosoftのHEIF/HEVC拡張依存と整合し、**WIC decoder単独の修正で標準フォト固有の拡張確認が無効化されるとは主張できない**。Photos GUI編集ボタンと保存品質の実機成功はまだ未確認。1.1.18を「正常版」と扱わず、未検証MSIを配布しない。

**進捗メトリクスの改定方針**：従来の45％は、Windows10で純正と自作のどちらが画素展開に失敗するかが不明だった暫定値。今回、**ユーザー実機で明示デコーダーの正否が再現可能**になり、自動選択と全画素比較の実機検証プログラムまでCI成功したため、調査とエンジニアリングの進捗に限って暫定50％へ評価し直すことはできる。ただし**完成機能が50％利用可能という意味ではない**。本来のP0基準（Photos警告除去、編集・回転のGUI、元写真保護と全メタ保存）は未達。主観的数値より、実機動作確認で次の大きな進捗を判定する。

## 2026-10-24：実機診断を小出しにしない『1回実行・ZIP1個』方式へ移行

**ユーザー要望**：個々の診断を追加するたびに実機試験を繰り返す負荷が高い。以降はCIで可能な検証を先に実施し、実機はまとまった成果・修正を評価する場合に一度のセッションで済ませる。旧1.1.18は画素変形／保存エラーを含むため「正常版」ではない。MSIを毎回配らない。

**今回ユーザーが提供した `Photos_HEVC_ReadOnly_Win10_x64(3).zip`** は画像なし診断の再実行結果で、純正／自作WIC COM生成S_OK、HEVC MFT=0、HEIF Image Extension導入済み、旧1.1.18導入状態。以前のZIP/JSONと技術的に同じため、**新たなP0進捗として二重に数えない**。当該ユーザー実機JSON・DLLの絶対パス・MSI製品キーをGitHubへ保存しない。

**実装**：
- 新規 `Run_Photos_Batch_ReadOnly.cmd` → `Run_Photos_Batch_ReadOnly.ps1`。実機ダブルクリックを一度行うだけで、Media Foundation/WIC登録とCOM生成、固定合成HEICの純正／自作WIC `CopyPixels`、WIC自動デコーダー選択とフレーム寸法・全画素指紋、Photos／HEIF／HEVC AppX／自作WIC MSIのインベントリを一括収集する。
- 入力は事前にGit blob SHA-1／内容SHA-256で固定した公開人工HEICのみ。画像上書き、実写真入力、MSI変更、レジストリ変更、Photos UI起動はしない。詳細JSONは従来スクリプトで別に作られるが、自動削除・上書きせず、**提出用ZIPには同梱しない**。
- 完了後にユーザーが送るファイルは **`WIC_Batch_*.zip` 1個**。内部はユーザー名／DLLファイルパス／レジストリ製品キーなしの技術的診断値を記載した `WIC_Batch_Sanitized_Report.txt` **1個だけ**。`BATCH_COLLECTION_STATUS=COMPLETE` は診断収集の完了に限り、標準フォト編集・回転保存成功の意味ではない。
- 実機テスト依頼はこの先、**Windows 10標準フォトの警告・編集・人工画像の90/180/270度・4×90度・保存・メタデータ・サムネイル向き整合・復旧**を、一つの候補と一回の試験でまとめて評価できる場合に限定する。実機でユーザー写真は使わない。作業方針は `diagnostics/photos_trace/WIC_PHOTOS_ONE_BATCH_POLICY_20261024_JA.md` に文書化。

**CIエビデンス**：先行 #40076800037 SUCCESS、**最終 [#40076956160 SUCCESS](https://github.com/taku077/wic_heic/actions/runs/40076956160)**。Windows Server 2022でネイティブEXEのビルド、既存の画像なし診断、SHA確認済み人工HEIC、改ざん拒否、**PowerShell 5.1での一括実行・プライバシー監査・ZIP1個のみの生成**を成功確認。GitHub artifactは単一の `Run_Photos_Batch_ReadOnly.cmd` と統合ランチャー／日本語運用文書を含み、旧2種類の個別CMDを同梱しない（最終アーティファクト名 `Photos_WIC_OneBatch_Win10_x64`、artifact ID `12553970556`、2026-11-07 UTCまで）。

**重要**：これは**試験回数を減らすためのツール改善であり、製品WICがPhotos GUIのHEVC警告や編集・回転保存に対応したとの成果ではない**。暫定の50％は完成基準の実績とは言えず、CIツール追加だけで数字を引き上げない。次の作業はPhotosの編集可否と保存結果を変える安全なコード上の改善に集中し、小刻みな実機試験要求を差し控える。

## 2026-10-24：1回の実機一括診断によりWIC自動選択と全画素成功を確定・保存コードを改良

**一括実機資料**：ユーザー提出 `WIC_Batch_20261024_172255_928_394d8a028b8a4f95b80129ec61135b9b.zip` の唯一のファイル `WIC_Batch_Sanitized_Report.txt` を読み込み、`BATCH_COLLECTION_STATUS=COMPLETE` を確認。ユーザーの私有情報・元ZIPはリポジトリへ保存しない。

- Windows10 Pro 22H2 build19045、Photos 2026.11090.22001.0、HEIF Image Extension 1.2.48.0、旧自作WicHeic Repair Test v1.1.18導入状態（※画像変形／保存エラーを持つので**正常版ではない**）。
- MFT HEVC decoder/common=0、encoder/common=0、decoder including field-of-use=0、H264 control decoder=1。
- Microsoft純正HEIF：COM/Initialize/GetFrame/GetSize/RGB24交渉すべてS_OK。**16×8 CopyPixels=0xC00D5212**、全フレームハッシュを算出できず。
- 自作旧v1.1.18：同じ検証済み10,597B人工HEICで16×8 CopyPixels=S_OK、ROI RGB24 FNV64=`2C46D5C491E58AE4`、**1080×1920の6,220,800バイト全フレーム CopyPixels=S_OK、FNV64=`BBE3A110699D854C`**。
- WIC `IWICImagingFactory::CreateDecoderFromStream`（ベンダー未指定）の**自動選択は自作WIC**：`custom=1,microsoft=0`、RGB24 ROI／全画素コピーともS_OK、FNV64が明示自作WICと完全一致。**「WIC自動選択で純正を選ぶから画像を表示できない」仮説はこの実機人工画像では否定**。
- 一方、Photosのアプリ固有HEVC拡張チェック、編集ボタン、エンコーダー選択はこの診断で一切テストしていない。Microsoft純正WICの復号失敗はHEVC拡張不足と整合するが、Photosのボタン非活性の直接因果は依然不明。新MSIを実機に入れたわけではなく、**旧／研究版のWin10比較ではない**。
- **以後、実機診断は小出しにしない**：同一バージョンの追加診断を依頼せず、ソース修正とGitHub CIで事前検証し、区切り良い候補のみ1セッションでまとめて実機検証する。診断レポートや写真再提出を重複要求しない。

**製品コードを進める修正**：`wic_heic/HeicLosslessNative.h` で、従来 `iref` 参照ボックスがなくても構造上完全なHEICの**単一・primary `hvc1` 画像**が、無再圧縮回転を一律拒否される問題を修正。今回の新実装 `NoReferenceSinglePrimary` は、iinfが単一かつそのitem IDがprimary、タイプ `hvc1`、ipmaにほかの画像・Exif・補助要素がないことを確認して、**その限定条件だけ**元コンテナのirotを安全に更新する。曖昧な構成／複数画像／Exif／HDR／サムネイルを容認しない。構造レベルの人工BMFF回帰テスト `NativeLosslessProbe::VerifyReferenceFreePrimary` で、単一画像を許容し複数・未知種別を拒否する。既存の完全人工HEICを使った4回90°・全RGB一致などの総合試験も継続。コード `b22836a1f732a868da7cfa3eb81aa39afa5bd42c`、試験 `2cb465f98336c9d2c2ce0d0526543981043395a8`。[**製品WIC総合CI #40078197911 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40078197911)。`NATIVE_HEIC_REFERENCE_FREE_SINGLE_PRIMARY_SAFE_ROTATION_PASS`、`NATIVE_LOSSLESS_HEIF_MATCHES_PYTHON_AND_ROTATION_TEST_PASSED`、Media Foundation MFT旧版/新版非干渉テストを含み合格。**これは一部のHEICの無再圧縮回転対応範囲を広げる修正であり、Windows10標準PhotosのHEVC警告や編集保存を直したとの証拠ではない**。Windows10実機MSIは未導入。新しい実機テストは現時点で不要。

## 2026-10-24：WIC WriteSource拒否が次の正常保存を汚染するバグを再現・修正

**優先P0（Windows10標準Photos編集／保存）の製品コード側前進**。ユーザーが希望する通り、個別の実機診断を追加せず、既存の1.1.18 vs 研究版隔離CIと公開人工HEICだけで保存失敗の実際の原因を一つ修正した。

**公式仕様により却下した対症療法**：
- 旧1.1.18で `QueryCapability` が `0x15`（SameEncoder + SomeImages + Thumbnail）、研究版で `0x0C`（SomeImages + Metadata）を返す差は既知。しかし `WICBitmapDecoderCapabilitySameEncoder` は「同じHEIC形式のエンコーダーがインストールされている」意味ではなく「**この画像が同じベンダーのエンコーダーで作成されたと認識している**」というMicrosoft仕様。従ってiPhone由来HEICへ安易にSameEncoderを復活させることは不正確で採用しない。
- Microsoft WIC公式：https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapdecoder および https://learn.microsoft.com/en-us/windows/win32/api/wincodec/ne-wincodec-wicbitmapdecodercapabilities 。これだけでPhotosボタンが無効になる因果も未特定。

**再現テスト**：
1. 自作 `IWICBitmapFrameEncode` を未設定の新規フレームで初期化する。
2. WICで生成した64×48人工RGBビットマップの `WriteSource` を呼ぶ。出自不明なので `WINCODEC_ERR_UNSUPPORTEDOPERATION=0x88982F81` で**意図通り安全拒否**。
3. 同じフレームで別寸法の公開人工HEIC `fixture-irot3-exif6-noaux.heic`（1080×1920）を `WriteSource`、`Frame.Commit`、`Encoder.Commit` で保存し直す。
4. 修正前のCI [**#40078786772 FAIL（狙い通り）**](https://github.com/taku077/wic_heic/actions/runs/40078786772) では2段階目の拒否時に自動取得した64×48の`m_Width,m_Height`と画素形式が残り、後続の正規HEIC保存が `0x88982F81` で失敗。レッドテスト追加コミット `25f59f1a0a7d8bbbd36cc7f0bd324f82c27ee1d1`。
5. 製品コード `wic_heic/HeicBitmapFrameEncode.cpp` に `ScopedWicSourceSetterRollback` を導入（修正コミット `bc76ef8f3c9a30da835fc15b6c7f3174ff5c5373`）。呼出し前の自動サイズ・色形式・クロマ・精度を記録し、保存を受け入れた時だけ確定。**拒否・エラー・例外なら元の設定へロールバック**。不明なラスターを通すことはしない。元HEICコンテナ・ファイル・レジストリ・Photosには一切手を加えない。
6. テストでは `IStream::Stat` で実際の長さを確認し、メモリから読み戻したHEICを公開fixtureと**全バイト一致**させる（`GetHGlobalFromStream/GlobalSize` の割当て余剰長を避ける）。コミット `181e18eebf294e862f410490eb02fa46db7fb709`。
7. 修正後の製品CI [**#40078985290 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40078985290)：`WIC_WRITE_SOURCE_AUTOSETTER_REJECT_HRESULT=0x88982F81` に続いて `WIC_REJECTED_WRITE_SOURCE_DID_NOT_POISON_RETRY_AND_EXACT_HEIC_BYTES_PASS` を実証。既存 `NATIVE_LOSSLESS_HEIF_MATCHES_PYTHON_AND_ROTATION_TEST_PASSED`、旧研究WIC比較、HEVC MFT非干渉もPASS。旧マスク0x15／研究版0x0Cは維持（不正フラグ復活なし）。

**境界**：これは「出自不明な入力を拒否した後に同じフレームで正常保存を再試行できる」**製品の保存エンジンの不具合修正**。Photos 2026.11090.22001.0の独自HEVC拡張警告／編集・回転ボタン／GUIからのHEIC保存を実機で直したわけではない。ユーザーへ新しいMSIや細切れの実機テストは提供・要求しない。旧1.1.18を正常扱いもしない。

## 2026-10-24：部分HEIC書込みの復旧経路を拡張・SetSize非対応ストリームとの互換性を維持

**実ユーザー写真を使わずWindows GitHub隔離CIのみで検証。Photos UI未検証。** HEIC出力 `IStream` の一部は `SetSize` を実装せず、既存エンコーダーはHEICデータの一部だけを書いた後に `IStream::Write` が短い成功／失敗を返した場合、復旧用の `SetSize(0)` が拒否され、不完全な出力が残り得る（元写真が破損した実測ではない）。

1. **不採用案と戻し**：最初に `Commit` 前の `SetSize(0)` を全出力先へ必須とする案を検証した。赤テスト [CI #40079649535 FAILED](https://github.com/taku077/wic_heic/actions/runs/40079649535) に対してこの制限を製品コードへ実装（`bb3b81a8c07c7e2c40b2cdef37b4fc169358d134`）したところ、実際に合法なWindows WICの12MP人工HEIC生成まで `0x88982F81` で失敗した [CI #40079796882 FAILED](https://github.com/taku077/wic_heic/actions/runs/40079796882)。**新たな保存障害を生むため、即座にこの制約を戻した**（`59e1aaafe9be722b82abf1503095747b3804969b`）。`SetSize` 非対応でも全バイトを正常に書ける出力先を拒否しない。
2. `wictest/EncoderGuardProbe.h` に、`SetSize` 非対応・全Write成功の人工 `IStream` でも、元合成HEICのExif等を含むバイト列が**すべて等しい**という互換性試験を追加（`d100cf9cfe96d4b9fee5ec8c9ae6bd9b9a084e88`）。[CI #40079984332 SUCCESS](https://github.com/taku077/wic_heic/actions/runs/40079984332)：`WIC_HEIC_NO_SETSIZE_COMPLETE_OUTPUT_EXACT_BYTES_PASS` と既存の正常・失敗保存保護がPASS。
3. **実際に追加した安全策**：`SetSize(0)` での部分書込みロールバックが失敗したとき、`IStream::Revert()` がサポートされる**トランザクション型ストリームに限って**、最後のコミット前の状態へ復旧を試みる。元画像に触らず、人工の短い成功Writeを発生させ、`SetSize` を拒否するが `Revert` は動作する人工トランザクションストリームで赤テスト [CI #40080244318 FAILED](https://github.com/taku077/wic_heic/actions/runs/40080244318) を先に再現。修正 `wic_heic/HeicBitmapEncoder.cpp` `28c25e578c508b578a2c028eb23607d04c5dd2dd` で、HEIC生バイトのlossless出力とlibheifのrasterエンコード出力の**両方**の部分失敗時に `Revert()` を追加。[CI #40080397751 SUCCESS](https://github.com/taku077/wic_heic/actions/runs/40080397751) で `WIC_HEIC_PARTIAL_WRITE_TRANSACTION_REVERT_FALLBACK_PASS` と通常ストリームの回帰テストPASS。
4. **誤った復旧成功表示を防止**：Microsoft `IStream::Revert` 公式仕様では、トランザクションを使わない**直接モードではS_OKを返しても何も変更しない**場合がある。従って単なる `SUCCEEDED(Revert)` を復旧成功と扱わず、呼出し後の `Stat` で出力長が0と再確認できた場合だけ `stream-revert-restored` を診断出力に記録する補強 `f37905c1041e506dd221bebab2401486fb8f7a96` を追加。戻せなければ `stream-rollback-unavailable` のまま。失敗した保存のHRESULTを成功に置き換えない。公式：https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-istream-revert
5. **最終総合CI [#40080568410 SUCCESS](https://github.com/taku077/wic_heic/actions/runs/40080568410)**。製品WICのWindowsビルド、完全合成HEICの正確な復号・無再圧縮保存・回転、部分Write拒否、トランザクション復旧、SetSize無し正常出力、旧v1.1.18と研究版の回帰比較、MSIとプライベートサムネイルの独立性を検証。

**保証の限界**：非トランザクション型かつ `SetSize` 不可の出力先で書込みが途中失敗した場合、WICコーデック内部からその外部ファイルの全消去や原子的上書き防止を保証することはできない。エンコーダーは失敗を返し、復旧できたと偽装しない。呼出元アプリが元画像を事前に上書き／切詰める場合もこのWIC DLLだけでは保護できないため、実機の標準Photos試験は**人工HEICと独立コピーのみ**という不変条件を維持。

**進捗評価**：今回は保存処理の事故復旧の一部が拡張された製品コード修正であり、標準Photosの編集ボタン有効化・HEVC拡張警告の解消・回転保存成功の直接検証ではない。暫定52％は維持し、新規MSIや細切れの実機検査を依頼しない。

## 2026-10-24：標準WIC HEIC直接画素回転IF試作と互換性ゲート

**目的**：Win10標準PhotosのHEIC編集ボタンが無効となる問題に近い、標準WICの回転経路を改善する。ユーザーの実機・個人写真・v1.1.18インストール状態には触れず、公開人工HEICとWindows Server 2022の隔離CIのみで検証。

**仕様確認**：
- Microsoftの公式WIC文書は、`IWICBitmapSourceTransform` をHEIFフレームで用いることを示し、実装を推奨している。回転/ROI/画素形式/サイズをデコーダー側で処理できればWICが外部ラッパーで行う処理を減らせる：https://learn.microsoft.com/en-us/windows/win32/wic/-wic-imp-iwicbitmapsourcetransform
- ただし、同じコンテナー形式で編集・保存できることや、標準PhotosのHEVC拡張チェックを迂回できることは意味しない。**フレーム画素の回転機能とHEICの無再圧縮回転保存は別機能**。
- `IWICBitmapDecoder::GetMetadataQueryReader` がHEICコンテナーで未対応なのは、JPEG等のWIC単一フレーム形式では通常フレーム側からExif等を取得する、というMicrosoft公式の契約に沿うため、GUIボタンを狙って偽のコンテナーメタデータを申告する変更はしない：https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapdecoder-getmetadataqueryreader

**試験的実装（本流には**残さず差し戻し済み**）**：
- `CHeicBitmapFrameDecode` に `IWICBitmapSourceTransform` を追加し、元10/12/16bit精度を保持して0/90/180/270度の画素転置・ROIを処理。拡大縮小や反転・未知画素形式は拒否。COM `IUnknown` 同一性も担保する。
- 最初の回帰テストは `WIC_BITMAP_24bppRGB` と誤想定し、実際の公開fixtureが **10bit/48bppRGB（1画素6バイト）** だったため、標準WIC rotatorが `0x88982F8C`（出力バッファ不足）となった。**このエラーだけを新IFの互換性不具合と断定したのは誤り**。画素形式を正した試験で、[CI #40082957064](https://github.com/taku077/wic_heic/actions/runs/40082957064) にて `WIC_HEIC_SOURCE_TRANSFORM_ROTATION_0/1/2/3_CROP_PIXEL_EQUAL_PASS` と COM ID、安全な引数拒否のPASSを確認。
- **別の統合回帰が残った**：同じCIの後続 `GeometryProbe` で、Windowsの標準 `IWICBitmapFlipRotator` から `IWICImagingFactory::CreateBitmapFromSource` で8bit合成HEICをラスター化する既存経路が失敗（`materialize synthetic reference raster`）。従来通過していた既存アプリ機能を壊すため、この段階では**直接回転IFを公開しない**判断とした。試作コードの本流残存禁止。
- 研究差し戻し：`wic_heic/HeicBitmapFrameDecode.h` / `.cpp` / `wictest/wictest.cpp` を最後の成功済み版へ戻し、実験用 `WICSourceTransformProbe.h` を削除。最終的な製品コードは前回CI成功済みバージョンと同一。
- **将来の保護ゲートを追加**：`wictest/PhotosNavigationZoomProbe.h` に `Verify10BitWicClipRotateMaterialization` を新設。公開10bit HEICの原画素を取得した後、標準 `IWICBitmapClipper` → `IWICBitmapFlipRotator` → `CreateBitmapFromSource` → `CopyPixels` について0/90/180/270度と全6バイトRGBチャンネルの完全一致を検証する。人工画像読み取り専用、保存ファイル生成なし。
- [**最終CI #40083397893 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40083397893)：`WIC_HEIC_10BIT_STANDARD_CLIP_ROTATE_0/1/2/3_MATERIALIZE_PASS`、`WIC_HEIC_10BIT_STANDARD_CLIP_ROTATE_MATERIALIZE_PASS`、既存の無再圧縮HEIC90/180/270度・Exif一致・旧研究WIC比較・Media Foundation非干渉もすべてPASS。

**成果の解釈**：既存のHEIC閲覧・ラスター化回転機能を保持したまま、新しい10bit標準WIC経路の回帰保護を追加できた。**直接回転IFの実装自体は未採用**であり、研究版の標準Photos GUI編集・保存・HEVC警告の修正達成は**未確認**。主観的な52％をこの回帰テストだけで引き上げない。無根拠なMSI配布を行わず、実機での個別テストも依頼しない。

## 2026-10-24：再圧縮HEICの部分書き込みもトランザクション復旧を検証

GitHub現行ブランチで過去のNative SourceTransform対応実験はWindows標準WIC bitmap materialization／10bit角度変換と非互換が出たため巻戻し済み。Photos GUIの編集ボタン問題は未解決。今回はユーザーの写真やWindows10実機ではなく、CIの人工RGB→HEICエンコードの故障注入試験のみを拡充。

- `wictest/EncoderGuardProbe.h` の `VerifyPartialRasterStreamWriteRollback` にトランザクション型の人工 `IStream` を追加。通常の `SetSize` 成功の場合と、`SetSize` を拒否するが `Revert` は有効な場合を**同一の人工RGB生成・短い書き込み注入**条件で評価する。後者でも `Encoder.Commit` は失敗、保存先の長さは0に回復、二度目の `Commit` は `WINCODEC_ERR_WRONGSTATE`、不完全なHEICコンテナを成功扱いしない。
- 修正コミット `3a0b9a0dc9c53761796dce77772cca766adbe358`。製品DLLは今回変更せず、前回実装済みの `IStream::Revert` fallbackの再圧縮保存経路が動くことをCIで確認。
- [**GitHub Windows総合CI #40086467361 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40086467361)。`WIC_RASTER_PARTIAL_WRITE_TRANSACTION_REVERT_PASS`、`WIC_RASTER_PARTIAL_WRITE_ROLLBACK_PASS`、`WIC_HEIC_PARTIAL_WRITE_TRANSACTION_REVERT_FALLBACK_PASS`、旧版／研究版WICのHEVC MFT非干渉確認を含む。使用データは合成RGBと既存公開HEICのみ。
- **進捗率は暫定52％のまま**。再圧縮時の失敗復旧が検証されたが、標準フォトのHEVC拡張警告解消、編集・回転ボタン、GUIからの保存を実機で直した証拠ではない。実機テストの小出し要求、新MSI提供、ユーザー写真利用はしない。

## 2026-10-24：明示HEVC保存の不要な拒否を修正（人工HEIC 赤→緑、標準Photos P0は未合格）

**発見した製品コードの互換性不備**：`wic_heic/HeicBitmapFrameEncode.cpp::WriteSource` は元の自作HEICフレームに確実な `IHeicRawSource` があり、切抜き・幾何変更・ICC/画素形式の変更がない場合にも、`HeifCompressionMethod=2`（明示HEVC）を無条件で「元コンテナーの無再圧縮コピー」経路から除外していた。後段では元HEICのメタ情報を落とすラスター再エンコードを安全拒否するため、保存要求が失敗した。**この条件はWindows10 Photosの編集ボタンの原因と断定されていない。** 実証対象は公開人工HEICをWICから直接渡す経路のみ。

1. **赤テスト** [コミット `1055998fe31a`](https://github.com/taku077/wic_heic/commit/1055998fe31a296f674e7afc2e74092eba4d3193)：`wictest/WICDirectLosslessProbe.h::PreserveOriginalHevcWithExplicitHevcOption` を追加。Exif等を含む公開人工HEICを明示HEVCオプション付きで `WriteSource` へ渡し、保存後にメモリストリームと原HEICの全バイト一致を要求。[**CI #40090039615**](https://github.com/taku077/wic_heic/actions/runs/40090039615) は `WIC_LOSSLESS_COM_FAILURE explicit HEVC raw source copy 0x88982F81` により期待どおりFAIL。
2. **製品コードの最小修正** [コミット `43cd7a2b8e40`](https://github.com/taku077/wic_heic/commit/43cd7a2b8e40d080988a6acb2ec2cc465a0a2021)：元HEICを変更せずコピーできる経路について明示HEVC指定を許可。ただし指定時はHEIFの `ftyp` と `meta/iinf/infe` を解析し、primaryおよび他の符号化画像項目がHEVC `hvc1` と検証できる場合に限る。未認識構造・AV1等は拒否。コピー可能な場合は**HEVC再エンコードを実施せず、元コンテナー全体をバイト単位で保持**する。
3. **旧安全テストの条件修正**：初回の修正CI [#40090372114](https://github.com/taku077/wic_heic/actions/runs/40090372114) は、新機能を証明する `WIC_EXPLICIT_HEVC_VERIFIED_SOURCE_CONTAINER_BYTE_EXACT_PASS` には合格したが、旧テストが「変更なしの明示HEVC保存も必ず拒否」という旧契約のままで FAIL。安全性を弱めず、**明示HEVC＋寸法変更**と**既定オプション＋切り抜き**のいずれも `0x88982F81` で拒否すべきことを保持するテストへ改訂した（[コミット `2328f1be19e6`](https://github.com/taku077/wic_heic/commit/2328f1be19e6b98b05c26346a3c917d4308d5a2d)）。
4. [**最終Windows総合CI #40090686956 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40090686956)：`WIC_EXPLICIT_HEVC_VERIFIED_SOURCE_CONTAINER_BYTE_EXACT_PASS`、`WIC_RAW_HEIC_EXPLICIT_HEVC_GEOMETRY_AND_CROP_FAIL_CLOSED_PASS`、既存90/180/270度回転と元メタ情報の保持、部分書込み時のRevert、プライベートサムネイル共存、旧1.1.18との比較、Media Foundation HEVC MFT非干渉を含む総合CIを確認。ユーザーのPC・写真・レジストリ・HEIC Managerには一切変更なし。

**依然未証明**：Windows10 Photos 2026.11090.22001.0 が実際にこの保存APIオプションを選択するか、HEVC拡張警告が消えるか、編集・回転ボタンが有効になるか、Photos GUIからの安全な回転保存は未検証。Photos GUIと同等の最終合格ではない。実機テストの小出し・MSI提示を行わず、**進捗率は暫定52％のまま**とする。

## 2026-10-24：WIC 全範囲 WICRect の安全なロスレス保存（赤→緑、Photos P0 GUIは未合格）

**根拠**：Microsoft公式 `IWICBitmapFrameEncode::WriteSource` は省略可能な `WICRect* prc` でソース領域を指定できる。特に `SetSize` 未指定時は渡された矩形の寸法を使用する。元画素の全領域を `{0,0,幅,高さ}` と明示した場合、実画素編集は行っていないため、`prc==nullptr` の場合と同じ無再圧縮保存を可能にすべき。一方、部分領域・座標ずれ・元画素の不明なラッパーは圧縮データとメタデータを正しく保全できないため危険な成功を防ぐ。

- [**赤テストコミット `31e822c45f8778cb5bbe3f03c4d9aecfab1e4a38`**](https://github.com/taku077/wic_heic/commit/31e822c45f8778cb5bbe3f03c4d9aecfab1e4a38) を `wictest/WICDirectLosslessProbe.h` に追加。公開人工 `fixture-irot3-exif6-noaux.heic` に対して、`WICRect{0,0,1080,1920}` を指定し、(1) 明示HEVCの完全コンテナーコピーと (2) 無再圧縮90度回転をメモリストリームのみで実施し、既存の基準コンテナーと全バイト一致を要求。
- [**赤CI #40091296410（意図どおりFAIL）**](https://github.com/taku077/wic_heic/actions/runs/40091296410)：最初の全範囲コピーで `WIC_LOSSLESS_COM_FAILURE full ROI lossless WriteSource 0x88982F81`。元HEICの内容を変更していないのに `prc` 非NULLを理由に無再圧縮経路から排除していたことを再現。
- [**製品修正コミット `eaab1287e32dcc31125d57daa19a5a2f21cc2035`**](https://github.com/taku077/wic_heic/commit/eaab1287e32dcc31125d57daa19a5a2f21cc2035)：`HeicBitmapFrameEncode.cpp::WriteSource` で入力元 `IWICBitmapSource::GetSize` を取得し、矩形が原点(0,0)から元画像の幅・高さ**完全一致**の場合のみ `sourceRectangleIsWholeImage=true` と判定。従来の `prc==nullptr` およびこの真正な全範囲矩形だけにロスレスコピー／回転処理を許可。変形済みWICラッパー、未検証出自、部分クロップ、出力寸法変更、明示ICC変更の拒否は解除しない。
- [**緑CI #40091487604 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40091487604)。実ログ `WIC_FULL_FRAME_ROI_LOSSLESS_COPY_CONTAINER_BYTE_EXACT_PASS`、`WIC_FULL_FRAME_ROI_LOSSLESS_ROTATE90_CONTAINER_BYTE_EXACT_PASS`。従来 `WIC_EXPLICIT_HEVC_VERIFIED_SOURCE_CONTAINER_BYTE_EXACT_PASS`、`WIC_RAW_HEIC_EXPLICIT_HEVC_GEOMETRY_AND_CROP_FAIL_CLOSED_PASS`、90/180/270度回転、10bit標準WICクリップ・回転・画像化、再圧縮部分書込みのトランザクション復旧、旧1.1.18の比較、HEVC MFT非干渉、独立サムネイル共存も総合テスト合格。

**限界と次のP0**：この試験はWindows Server 2022のWIC APIと人工HEICであり、Windows10 Photos 2026.11090.22001.0 が現実に `WriteSource` へ全範囲 `WICRect` を渡すかは**未観測**。HEVC拡張警告・編集ボタン無効・Photos GUI保存成功は依然として未達。Microsoft公式はWindows10 PhotosのHEIF画像でHEVC拡張を要求する場合があると説明しているため、WIC DLLのみで確実に解決可能とはしない。ユーザー写真・HEIC Manager・既存WIC登録・Windows10実機には変更なし。新MSI配布・細切れ実機テストはしない。**暫定進捗52％維持**。

WIC仕様: https://learn.microsoft.com/ja-jp/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframeencode-writesource

## 2026-10-24：WIC エンコーダーのプロパティ別読取失敗で回転指定が消える不具合

**P0保存品質に直結する赤→緑修正。ただしWindows10標準PhotosのGUI操作・HEVC拡張警告は未合格。**

`CHeicBitmapFrameEncode::Initialize` は `IPropertyBag2::Read` の全体HRESULTが `S_OK` であれば、`BitmapTransform` と `HeifCompressionMethod` の各要素 `HRESULT errors[2]` が失敗していても、値を既定値（回転0／圧縮DontCare）として採用し、フレーム初期化を成功させていた。Photos等のWICクライアントから渡された回転要求が読取に失敗した場合に、失敗を通知せず未回転画像として保存するリスクがある。今回このエラー経路を公開人工COMのみで再現した。**Photos自身がこの異常なPropertyBagを返したという証拠ではない。**

- [赤テストcommit `20c81f12eadfa2a4f91ae3aba87287860a351191`](https://github.com/taku077/wic_heic/commit/20c81f12eadfa2a4f91ae3aba87287860a351191)：`wictest/EncoderGuardProbe.h` に `FailingWicOptionReadBag` と `VerifyPerOptionReadFailureCannotSilentlyLoseRotation` を追加。正式なWICオプションバッグに90度回転を設定した後、`IPropertyBag2::Read` は全体 `S_OK` だが `BitmapTransform` の個別結果だけ `E_ACCESSDENIED` とする。実行はメモリ内のエンコーダー／ストリームだけで行い、ユーザー画像・レジストリ・実機には触れない。
- [赤CI #40092291739（意図どおりFAIL）](https://github.com/taku077/wic_heic/actions/runs/40092291739)：`WIC_FRAME_PARTIAL_OPTIONS_READ_FAILED_HRESULT=0x00000000`、`Failed WIC BitmapTransform read silently used Rotate0`。この時点で既定値への黙示的フォールバックを証明。
- [製品修正commit `36fa2a754740ea3c4f3fe79f4298e76a66a0a590`](https://github.com/taku077/wic_heic/commit/36fa2a754740ea3c4f3fe79f4298e76a66a0a590)：`HeicBitmapFrameEncode.cpp::Initialize` が各要素のHRESULTを検査し、アクセス拒否等の明確な失敗を呼出元へ返す。非指定として許される `E_INVALIDARG` / `DISP_E_MEMBERNOTFOUND` かつ `VT_EMPTY` の場合に限り既定値を維持。失敗時に `VARIANT` を解放し、不完全な初期化状態へ遷移しない。
- [緑CI #40092481457 SUCCESS](https://github.com/taku077/wic_heic/actions/runs/40092481457)：`WIC_FRAME_PARTIAL_OPTIONS_READ_FAILED_HRESULT=0x80070005`、`WIC_FRAME_PROPERTY_READ_ERROR_CANNOT_SILENTLY_DROP_ROTATION_PASS` を確認。正しいバッグによる同じフレームの再初期化も成功し、失敗前後の出力ストリームは空を保持。既存 `WIC_FULL_FRAME_ROI_LOSSLESS_COPY_CONTAINER_BYTE_EXACT_PASS`、`WIC_FULL_FRAME_ROI_LOSSLESS_ROTATE90_CONTAINER_BYTE_EXACT_PASS`、`WIC_EXPLICIT_HEVC_VERIFIED_SOURCE_CONTAINER_BYTE_EXACT_PASS`、`WIC_RASTER_PARTIAL_WRITE_TRANSACTION_REVERT_PASS`、90/180/270度回転、独立サムネイル共存、MF非干渉を再確認した。

**P0残課題**：正常な標準Photosが実際にこの読取エラーを出すとは限らず、UIのHEVC警告・編集/回転ボタン無効化の原因は依然未特定。実機Photosからの回転保存、Exif/ICC/HDRの網羅保持は未合格。ユーザー環境でのインストールや実写真へのアクセスはしない。研究版MSIの提示は保留し、暫定進捗は52％を維持する。

Microsoft WICエンコーダー仕様: https://learn.microsoft.com/en-us/windows/desktop/wic/-wic-imp-iwicbitmapframeencode

## 2026-10-24：標準HEIF／自作WICの保存可能性を実機一括診断に追加（CIのみ成功）

**対象P0**：旧v1.1.18ではPhotos編集ボタンが有効化されたが、研究版WICではHEVC拡張警告／編集不可に退行。実機診断で自作WICの人工HEIC全画素復号成功、純正HEIFの `CopyPixels=0xC00D5212` を確定済み。しかしPhotos GUIが使用する**エンコーダー選択／編集保存**は直接の証拠がない。今回これを区別するための「メモリ内保存」観測を従来の**一括診断**へ組み込み、ユーザー実機には触れずにWindows x64 CIを検証した。

**GitHub実装**：
- `diagnostics/photos_trace/HevcMfReadOnlyProbe.cpp`：コミット `233df1fc3c78f3f794d7bfb7970250dbf84ef548`、診断ログ改行修正 `e2a0b4d73794b4dd010adfe5154eed6c10af4933`。固定SHA-256認証済み公開人工HEICの**自動WICデコードフレーム**を同時条件のMicrosoft純正HEIF／自作HEIC WICエンコーダーへ与える。`CreateEncoder`／`Initialize`／`CreateNewFrame`／`GetMetadataQueryWriter`／`WriteSource`／`Frame.Commit`／`Encoder.Commit`／メモリ内読戻しの結果を段階別に記録する。出力は新規 `CreateStreamOnHGlobal` メモリストリームだけで、実写真、HEIC元ファイル、レジストリ、Store・MSIに変更しない。完了後の全バイト一致を検査するが、WIC出力成功はPhotos GUI保存成功・回転保存・メタ保持完全性と同義ではない。
- `Run_Photos_Batch_ReadOnly.ps1`：コミット `61ba874bbc4cc6ff0bc35e35d6f7e5a8f7966676` と `b384bc7dfb8ceb4c40665abc2fd46f70cb25b31f`。上記のネイティブ／自作保存APIのHRESULTと完全一致フラグを、既存の匿名化済み単一ZIPテキストに収録。個別実機試験や新しいMSIは追加しない。
- `.github/workflows/photos-hevc-mf-readonly.yml`：コミット `e2a2812099914b9cded33b14d7af6917324a56de`。出力レポートに新しい情報が存在することをチェックする。
- [**診断CI #40093401870 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40093401870)：Windows Server 2022上でC++ビルド、合成HEICのSHA認証／改ざん拒否、PowerShell 5.1一括ZIP生成・サニタイズ、新フィールドを検証。この使い捨てランナーでは、ベンダー指定なしのWIC `CreateDecoderFromStream` が `0x88982F8B` で失敗し、旧版・研究版のどちらも導入していないため、**保存のA/B結果自体は計測されていない**。
- 既存の旧版／研究版MSIインストールA/B統合CIに接続しようとしたが、ワークフロー定義が不正になったため中止。試行 `6a272975221ddbaf7145ec2c9356ee87bdcb88cc` と `b2bc064471422b9e1ac2ae5075f9bb43ed78f76e` はActionsのジョブを開始できず**不採用**。成功済みワークフローのblob `44e477970fa02bf67281f5b11993925ebe4769d6` をコミット `ebdba5e5976ddf790e55fac13b3b06b25f8f9a47` で全バイト一致復旧。これらの失敗をWIC本体・Photos UIの失敗やA/B実測と誤認しない。

**判定**：標準Photos警告解除、編集／回転ボタンの復活、標準Photosからの変形・メタデータ損失なし保存は依然として未達。ユーザー実機の既存旧v1.1.18を正常版扱いせず、新MSI配布／個別診断も行わない。暫定進捗 **52％維持**。今回の開発成果は安全な一括診断の計測能力拡張のみ。

## 2026-10-24：旧1.1.18対研究版の人工HEICメモリ保存A/B実証

**WIC同一公開人工HEICのWindows Server 2022 CI実測（Windows10 Photos GUIではない）**

旧1.1.18が画像を編集・回転できるUIを復活させた一方、実保存時に画像変形・エラーを起こす不具合版だったことに留意。これまで自動WIC候補/QueryCapability/CopyPixelsはA/B済みであったが、元フレームからの`WriteSource`→`Frame.Commit`→`Encoder.Commit`→コンテナー全バイト一致という**同条件実保存**の旧／研究版比較は実施できていなかった。今回、既存の独立CIに短い呼出し1行だけ足し、CI専用 `diagnostics/photos_trace/Compare_WIC_Encoder_RAM.ps1` で検証する方式へ整理。固定公開人工HEIC 10,597B、SHA-256 `D874948C2E6F218A45C740A270FE5E10F0344C8F3A525B59FD44FDCE898DCC95` に限定し、HEIC出力は`CreateStreamOnHGlobal`を通じた**メモリのみ**。別の画像、原本、ユーザーPC、HEIC Managerやレジストリの変更は行わない。

- [**最終統合CI #40095659394 SUCCESS**](https://github.com/taku077/wic_heic/actions/runs/40095659394)。旧MSIはソース基準`a18b9ab146ca4d6f7b46a86507d7f0d0d882dc87`／既存のSHA-256照合済みActions MSIを使用し、研究版MSIを同じ使い捨てランナーで入れ替え。Media Foundation HEVC MFT数は導入前後で不変、既存90/180/270度回転・RGB/ICC・全画素・Explorerプレビューv0.4.0共存・アンインストール復旧も通過。
- **両版とも固定人工HEICの自動デコーダーは自作WIC**（`custom=1,microsoft=0`）。純正HEIFデコーダー生成は当該Server CIではいずれも`0x88982F8B`で失敗。これをWindows10へ外挿しない。
- **CLSID指定の自作WICフレーム→自作WICエンコーダー、メモリ内無編集保存**：旧1.1.18は`CreateEncoder=S_OK`、`Frame.Initialize=S_OK`、`WriteSource=0x88982F04`（`WINCODEC_ERR_WRONGSTATE`）、`Commit`未到達。研究版は`WriteSource=S_OK`、両`Commit=S_OK`、読戻しサイズ **10,597B**、元コンテナーと**全バイト一致**、`Exact=1`。
- Windows Server 2022の純正HEIFエンコーダーは両版とも`CreateEncoder=0x88982F8B`、以降未到達。**ユーザーWindows10実機では過去診断で純正HEIFエンコーダーのCOM生成S_OK**なのでServer側の失敗を実機の可否として扱わない。
- 途中の赤CIには`#40094848720`（自動選択に関する過剰な前提）、`#40095203041`（比較元デコーダーの区別とエラー）、`#40095453124`（PowerShell CRLFと行末アンカー不一致による結果抽出ミス）がある。製品WICの保存結果を誤解しないため、デコーダーの自動調停とCLSID固定の保存元を分離、診断HRESULT段階を匿名化して表示し、最後に行末をLF正規化することで全合格。誤ったCI定義を本流へ残さない。

**ここから言えること**：研究版の無編集・メタデータ保持型HEIC保存経路は旧版より明確に改善した。少なくともこの公開人工HEICでは、研究版が`WriteSource`不能だからPhotosの編集ボタンが無効化された、という単純な説明は支持されない。**しかしPhotos自身がこのAPI経路を使った事実はなく、警告解除・編集ボタン有効化・人工HEICのGUI回転保存の3つは依然未合格。** MF HEVC／純正HEIFのアプリ依存も残る。元写真・MSI・WIC登録・HEIC Managerに手を加えていない。進捗は**暫定52％維持**。次のP0は標準Photos固有のHEVC条件と編集保存APIの実経路の隔離であり、MSIを小出しにしない。

## 2026-10-24：進捗率52％を廃止しP0実機受入れ0/3へ移行

2026-10-24以後、過去の「50％」「52％」は**完成見込みや機能達成率として失効**。本原因記録に過去の数値が存在する場合も、当時の経緯であり現行評価に転用しない。

現在の**Photos実機での完成判定は0/3**。①HEVC警告なし②編集・回転ボタン有効③画像変形・再圧縮・対応メタ情報損失のない安全な回転保存と元画像保護、の3条件はいずれも研究版で未合格。すでに成功したWIC直接パススルー・API回転・部分書込み安全対策・A/Bテストは機能台帳として保存するが、GUIで検証するまでは受入れ項目に加点しない。

現行唯一の優先順位・中止条件・一次資料と安全ゲートは [P0_PHOTOS_CRITICAL_PATH_DECISION_20261024_JA.md](P0_PHOTOS_CRITICAL_PATH_DECISION_20261024_JA.md) を参照。Microsoft公式はWindows10 Photosで写真のみ利用でもHEIFとHEVC両拡張が必要と明記。一方、旧1.1.18でUI復活した同一ユーザー実機A-B-Aの事実は独立して保持し、単純な「絶対不可能」結論にしない。まずUIの実証に効くWIC契約差分、またはPhotosのHEVC依存を確定し、未確認のWIC微修正を積み上げる作業は打ち切る。

実機／インストールを伴う次の試験は、安全ゲートと具体的候補が成立した場合に限り**まとめて一度**行う。新たなコード変更・CIが無い場合は、実装が進んだように表現しない。
