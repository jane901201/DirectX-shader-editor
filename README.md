# DirectX-shader-editor
嘗試用 DirectX 製作 shader 編輯器

# 版本
- DirectX12
- HLSL
- Messagepack
- C++20
- C++20 Coroutines
- RxCpp
- cppcoro
- Boost.DI(?
- assimp
- cmake

  # 架構
  Logger
  .dll 相關建置的東西 cmake 自動化
  DirectX と Vulkan 両対応の Shader Editor（＝同じノード/同じコード資産を、DX12 と Vulkan に出せる設計）を作るなら、最初に「どこを共通化して、どこを分岐させるか」を決めるのが一番重要です。実務的には以下の注意点が効きます。

1) 共通の中間表現をどうするか（最重要）

現実的なルートはだいたい3つです。

HLSL を正（ソース）にして、DXIL と SPIR-V を両方生成

Vulkan 側は DXC の SPIR-V backend が公式に使える、という立て付け。

注意：HLSL の「仕様の曖昧さ」や実装差が移植性問題を生む、という話がまさに最近も議論されています。

SPIR-V を正にして、必要に応じて HLSL へ変換

SPIRV-Cross は SPIR-V の reflection と各言語（HLSL/GLSL/MSL 等）への変換が主用途。

DX 側で最終的に DXIL が欲しいなら「SPIR-V→HLSL→DXC で DXIL」みたいな二段階になりがち（落とし穴も増える）。

Slang / 独自 IR を正にする

例として Slang は SPIR-V ターゲット固有の属性（vk::binding など）も整理されている。

“エディタの表現力”と“バックエンド差分吸収”を両立しやすい。

結論：**最短距離は「HLSL を正にして DXC で DXIL と SPIR-V を吐く」**構成が多いです。

2) “Binding/Resource モデル差”を UI 設計に織り込む

DX12 と Vulkan の差は、結局 リソースの結び付け方に出ます。

Vulkan

シェーダは descriptor set + binding でリソースに接続される（SPIR-V 側の decoration）。

D3D12

HLSL は t/b/u/s レジスタ + space（仮想レジスタ空間）で束ね、root signature がそれをマップする。

エディタ側のおすすめ実装

ノード/リソースに「論理ID（Texture_Albedo 等）」を付ける

その論理IDを、

Vulkan: (set, binding)

D3D12: (register, space)
に バックエンド別に割り当てる “レイアウト解決” 層を作る

3) Stage I/O（頂点→ピクセル等）の接続ルールを固定する

Vulkan の stage link は Location（SPIR-V の Location decoration）ベース。

DX は語彙として semantic（TEXCOORD0 等）が強いけど、結局あなたのエディタは **「同一のフィールド集合」**を定義して両方へ落とす必要があります。

おすすめ：

エディタ内部で **“struct 的な I/O 定義”**を持ち、

DX: semantic を生成

Vulkan: location を生成
のように 片側の文化に寄せない。

4) Reflection を前提にしたパイプライン生成

両対応は「コンパイルできた」で終わらず、実行時に正しいパイプラインレイアウトを組めることが本番です。

SPIR-V 側：descriptor set/binding/location/push constant 等だけ見ればよい、という実装指針が分かりやすいです。

変換や解析には SPIRV-Cross の reflection が定番。

5) “対応しない機能”を最初に決める（移植性の地雷除去）

エディタが自由すぎると、バックエンド差で破綻します。最初は例えば：

wave/subgroup 系、bindless 周り、独自拡張

レイトレ/DXR と Vulkan RT の差分

64-bit 演算や精度差、NaN/denorm の扱い

…みたいな“地雷カテゴリ”を スコープ外にするのが安定です（後で段階的に増やす）。

6) 実装の型（おすすめの全体アーキテクチャ）

Frontend: ノードグラフ → 自前 AST（型情報つき）

Middle: 共通最適化（定数畳み込み、デッドコード除去、共通部分式…）

Backend:

DX12: AST → HLSL → DXC → DXIL

Vulkan: AST → HLSL → DXC(SPIR-V) → SPIR-V

反射: SPIR-V は SPIRV-Cross、DXIL は D3D の reflection（または同等情報を自前で保持）

DXC の “HLSL→SPIR-V” 自体は Vulkan ガイドでも明確に推奨される形です。

ShaderEditor/
Core/
Graph/                # ノード・ピン・接続・サブグラフ
Types/                # 型、暗黙変換、オーバーロード
IR/                   # AST/SSAなど（あなたが選ぶ）
Diagnostics/          # エラー、ソースマップ
Serialization/        # .json / .asset 等
CodeGen/
Shared/               # 共通のコード生成ユーティリティ
HLSL/                 # HLSL 生成（共通）
Templates/            # エントリポイント、I/O構造体テンプレ
Backends/
D3D12/
Layout/             # register/space, root signature metadata
Compiler/           # DXC呼び出し（DXIL）
Reflection/         # DXIL 反射 or メタ保持
Vulkan/
Layout/             # set/binding, push constants
Compiler/           # DXC呼び出し（SPIR-V）
Reflection/         # SPIR-V 反射（SPIRV-Cross等）
Runtime/
MaterialSystem/       # グラフ->実行用マテリアル、バリアント管理
PipelineCache/        # PSO/パイプラインキャッシュ
ResourceBinding/      # 描画時のバインド更新
Tools/
Tests/
CLI/                  # headless compile & validate

SIMD Programming
https://youtu.be/vIRjSdTCIEU?si=RehS6J_dIUwefYq_

# 有時間
兼容 Vulkan 版本

# 進一步
WebGPU Three.js 之類的