# RenderBuilder

[English README](README.md)

RenderBuilder は、ランタイム HLSL 開発のための Windows / Visual Studio 2022 向けシェーダエディタのプロトタイプです。

## 現在の MVP

- DirectX 12 Agility SDK 1.619.3 を使う Direct3D 12 first のレンダラー。
- Microsoft.Direct3D.DXC 1.9.2602.17 によるランタイム DXC コンパイル。
- HLSL Shader Model 6.9 を既定ターゲットとして使用: `vs_6_9` / `ps_6_9`。
- Dear ImGui docking UI による Viewport、Shader Editor、Material Inspector、Scene / Asset Browser、Compile Diagnostics、Renderer Stats パネル。
- `Shaders/RenderBuilderShaderABI.hlsli` による固定シェーダ ABI。
- シェーダのコンパイル成功時は D3D12 PSO を再構築し、失敗時は最後に成功した PSO を維持して diagnostics にエラーを表示。
- 組み込みプレビュー cube と、Assimp による glTF/GLB、FBX、OBJ の scene import。
- DirectXTex による DDS/TGA/HDR/WIC 画像の texture upload と checker fallback。
- imported material を material ごとの draw routing と shader set assignment で描画。

## LookDev ロードマップ

- 専用の `LookDevPBR.hlsl` shader による D3D12 first の PBR preview。
- HDRI + Sun environment control、tone mapping、debug display mode、turntable preview。
- Project JSON に scene、material slot、shader assignment、camera、sky、environment、view setting を保存。

## 今後のマイルストーン

- M2: LookDev PBR/IBL workflow、より豊富な shader set 管理、snapshot/export polish。
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
