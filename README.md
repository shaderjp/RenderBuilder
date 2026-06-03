# RenderBuilder

[日本語版 README](README.ja.md)

RenderBuilder is a Windows / Visual Studio 2022 shader editor prototype for runtime HLSL development.

## Screenshots

The current D3D12 preview path is being exercised with both glTF Sponza-style scenes and the Amazon Lumberyard Bistro FBX data.

![Large scene preview](<images/RenderBuilder Shader Editor - _Untitled 2026_05_25 22_05_21.png>)

![Bistro / Sponza material preview](<images/RenderBuilder Shader Editor - _Untitled 2026_05_25 22_07_33.png>)

![Bistro / Sponza LookDev preview](<images/RenderBuilder Shader Editor - _Untitled 2026_05_25 22_08_25.png>)

## Documentation

- [Scene files and controls (Japanese)](Docs/SceneAndControls.ja.md)
- [Scene examples and screenshots (Japanese)](Docs/SceneExamples.ja.md)
- [Custom shaders and HLSL ABI (Japanese)](Docs/CustomShaders.ja.md)
- [AI Chat setup and local Gemma workflow (Japanese)](Docs/AiChat.ja.md)
- [RenderBuilder MCP Bridge](Tools/RenderBuilderMcp/README.md)

## Current MVP

- Direct3D 12 first renderer using DirectX 12 Agility SDK 1.619.3.
- Runtime DXC compilation through Microsoft.Direct3D.DXC 1.9.2602.17.
- HLSL Shader Model 6.9 default targets: `vs_6_9` and `ps_6_9`.
- Dear ImGui docking UI with Viewport, Shader Editor, Material Inspector, Scene / Asset Browser, Compile Diagnostics, and Renderer Stats panels.
- Fixed shader ABI in `Shaders/RenderBuilderShaderABI.hlsli`.
- Successful shader compiles rebuild the D3D12 PSO; failed compiles keep the last valid PSO and show diagnostics.
- Shader Set Manager with duplicate/delete, compile-all, profile editing, per-material assignment overview, and last-good PSO status.
- Built-in preview cube plus Assimp scene import for glTF/GLB, FBX, and OBJ preview meshes.
- DirectXTex material texture slot upload for DDS/TGA/HDR/WIC images, with a checker fallback.
- Imported materials are rendered through per-material draw routing and shader set assignment.
- LookDev PBR preview with HDRI/SkyColor background, sun light, tone mapping, display modes, turntable, snapshots, and Project JSON persistence.
- Bistro-style packed ORM support: `Specular` DDS maps can be evaluated as `R=AO`, `G=Roughness`, `B=Metallic`.
- Local AI Chat panel backed by `llama-server` and a GGUF Gemma model, with model readiness tracking before prompts are sent.
- Optional local MCP control bridge for semantic LookDev/material commands through `\\.\pipe\RenderBuilder.Control`.

## LookDev Roadmap

- Improve large-scene responsiveness for Sponza/Bistro-scale assets with progressive texture upload and clearer load diagnostics.
- Add richer material debug views for texture slots, packed maps, tangent basis, alpha, and emissive contribution.
- Keep Vulkan/backend-neutral project data aligned with the D3D12 LookDev workflow.

## Planned Milestones

- M2: LookDev PBR/IBL workflow, large-scene polish, richer material diagnostics, and snapshot/export polish.
- M3: Vulkan backend parity using Vulkan SDK DXC for SPIR-V.
- M4: meshoptimizer meshlet cache plus D3D12/Vulkan Mesh Shader preview.
- M5: DXR/Vulkan Ray Tracing experiments.

## Setup

```powershell
git submodule update --init --recursive
```

Open `RenderBuilder.sln` in Visual Studio 2022 and build `x64` Debug or Release.
The first build generates local static Assimp and DirectXTex libraries under ignored `ThirdParty` build output folders.

Command-line build:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" RenderBuilder.sln /restore /p:Platform=x64 /p:Configuration=Debug
```

`GrapicsSample` is intentionally ignored and kept as reference material only.

## Local MCP Control

Start RenderBuilder with local control enabled:

```powershell
Bin\x64\Debug\RenderBuilder.exe --enable-local-control
```

Then build and run the stdio MCP bridge:

```powershell
cd Tools\RenderBuilderMcp
npm install
npm run build
node dist\index.js
```
