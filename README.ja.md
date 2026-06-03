# RenderBuilder

[English README](README.md)

RenderBuilder は、ランタイム HLSL 開発のための Windows / Visual Studio 2022 向けシェーダエディタのプロトタイプです。

## スクリーンショット

現在の D3D12 preview path は、glTF の Sponza 系 scene と Amazon Lumberyard Bistro の FBX data で確認しています。

![大型 scene preview](<images/RenderBuilder Shader Editor - _Untitled 2026_05_25 22_05_21.png>)

![Bistro / Sponza material preview](<images/RenderBuilder Shader Editor - _Untitled 2026_05_25 22_07_33.png>)

![Bistro / Sponza LookDev preview](<images/RenderBuilder Shader Editor - _Untitled 2026_05_25 22_08_25.png>)

## ドキュメント

- [シーンファイルと基本操作](Docs/SceneAndControls.ja.md)
- [シーン例とスクリーンショット](Docs/SceneExamples.ja.md)
- [独自シェーダの作り方](Docs/CustomShaders.ja.md)
- [AI Chat のセットアップ](Docs/AiChat.ja.md)
- [RenderBuilder MCP Bridge](Tools/RenderBuilderMcp/README.md)

## 現在の MVP

- DirectX 12 Agility SDK 1.619.3 を使う Direct3D 12 first のレンダラー。
- Microsoft.Direct3D.DXC 1.9.2602.17 によるランタイム DXC コンパイル。
- HLSL Shader Model 6.9 を既定ターゲットとして使用: `vs_6_9` / `ps_6_9`。
- Dear ImGui docking UI による Viewport、Shader Editor、Material Inspector、Scene / Asset Browser、Compile Diagnostics、Renderer Stats パネル。
- `Shaders/RenderBuilderShaderABI.hlsli` による固定シェーダ ABI。
- シェーダのコンパイル成功時は D3D12 PSO を再構築し、失敗時は最後に成功した PSO を維持して diagnostics にエラーを表示。
- Shader Set Manager による duplicate/delete、compile all、profile 編集、material assignment overview、last-good PSO status 表示。
- 組み込みプレビュー cube と、Assimp による glTF/GLB、FBX、OBJ の scene import。
- DirectXTex による DDS/TGA/HDR/WIC 画像の material texture slot upload と checker fallback。
- imported material を material ごとの draw routing と shader set assignment で描画。
- HDRI/SkyColor background、sun light、tone mapping、display mode、turntable、snapshot、Project JSON 復元を含む LookDev PBR preview。
- Bistro 系の packed ORM に対応。`Specular` DDS を `R=AO`、`G=Roughness`、`B=Metallic` として評価できます。
- `llama-server` と GGUF Gemma model を使う local AI Chat panel。モデルが `Ready` になるまで送信を抑止します。
- `\\.\pipe\RenderBuilder.Control` 経由で LookDev / material 設定を操作する local MCP control bridge。

## LookDev ロードマップ

- Sponza / Bistro 規模の大型 asset での応答性を上げるため、progressive texture upload と load diagnostics を強化。
- texture slot、packed map、tangent basis、alpha、emissive contribution を確認する material debug view を追加。
- Vulkan / backend 非依存の project data を D3D12 LookDev workflow と揃える。

## 今後のマイルストーン

- M2: LookDev PBR/IBL workflow、大型 scene polish、material diagnostics、snapshot/export polish。
- M3: Vulkan SDK の DXC for SPIR-V を使った Vulkan backend parity。
- M4: meshoptimizer meshlet cache と D3D12/Vulkan Mesh Shader preview。
- M5: DXR/Vulkan Ray Tracing の実験。

## セットアップ

```powershell
git submodule update --init --recursive
```

Visual Studio 2022 で `RenderBuilder.sln` を開き、`x64` の Debug または Release でビルドしてください。
初回ビルド時に、local static Assimp / DirectXTex library が git 管理対象外の `ThirdParty` build output folder に生成されます。

コマンドラインビルド:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" RenderBuilder.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

`GrapicsSample` は意図的に git 管理対象外にしており、参照資料としてのみ保持します。

## Local MCP Control

RenderBuilder を local control 有効で起動します。

```powershell
Bin\x64\Debug\RenderBuilder.exe --enable-local-control
```

stdio MCP bridge は次のように build / run します。

```powershell
cd Tools\RenderBuilderMcp
npm install
npm run build
node dist\index.js
```
