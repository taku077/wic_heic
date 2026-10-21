# HEIC/HEVC初回サムネイル：安全なGPU経路の事前審査

## 実装・進捗（2026-10-21）

研究ブランチ：`experiment/heic-gpu-decode-probe`。MSI未作成。既存のWindows/HEIC Managerの関連付けには触れない。

- libheifの`heif_item_get_transformation_properties`でHEIC主画像の順序付き`irot`/`imir`/`clap`を取得し、FFprobeが出す**正規の3×3 Display Matrix**と照合。両者の変換方向が一致する場合だけ将来のハードウェア復号の候補とし、鏡像・切り抜き・その他不明な変換、相互不一致では従来経路にフォールバックする。
- [CI 39607185057](https://github.com/taku077/wic_heic/actions/runs/39607185057)：既存7 HEICの相互照合が全合格（identity 5件、時計回り90度2件）。元ファイル不変。
- 同じCIで、libheifが返すHEIC主画像の圧縮NALと`hvcC`を使う**ネイティブHEVC Annex B抽出**を検証。既存7枚＋人工12MP画像1枚、計8枚すべて復号後のBGRA画素完全一致。元画像再圧縮・上書きなし。
- [CI 39602846177](https://github.com/taku077/wic_heic/actions/runs/39602846177)：表示行列の鏡像・傾き・拡大縮小・平行移動・重複・不整合は拒否する単体試験を通過。FFmpeg回転を利用した画素一致も7/7。
- 次段階のC++診断に、8bit、非アルファ、raw ICCなし、実装可能な画像寸法、対応済み回転という**保守的な暫定チェック**を追加。10/12bit Main10/HDR・ICC・アルファ・不明なHEIF変換は現時点でGPU候補と断定しない。NCLX色管理も今後検証が必要。
- 診断環境のGitHub WindowsランナーはMicrosoft Basic Render Driverのみで、実Intel GPUのHEVC復号速度は計測不可。D3D11のプロファイル列挙のみを実施し、実画像のGPUデコードは**未実装・未検証**。

## 次の決定条件

Intel第7世代でHEVC Main 8bitと必要に応じてMain10が利用できるかを、実機の読み取り専用なAPI診断で確認する必要がある。ただし、ユーザーの負荷を増やすため小さな診断成果物を都度渡さない。HEVCビットストリームをGPUへ送る実装、初回呼び出しの初期化オーバーヘッド、GPU→WICへの画素コピーと回転・ICC処理、CPU使用率、同時複数画像をまとめて測定する。安全条件と十分な速度改善がそろった節目だけMSIを出す。
