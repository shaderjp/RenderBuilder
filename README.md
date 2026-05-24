# RenderBuilder

[日本語版 README](README.ja.md)

RenderBuilder is a Windows / Visual Studio 2022 shader editor prototype for runtime HLSL development.

## Current MVP

- Direct3D 12 first renderer using DirectX 12 Agility SDK 1.619.3.
- Runtime DXC compilation through Microsoft.Direct3D.DXC 1.9.2602.17.
- HLSL Shader Model 6.9 default targets: `vs_6_9` and `ps_6_9`.
- Dear ImGui docking UI with Viewport, Shader Editor, Material Inspector, Scene / Asset Browser, Compile Diagnostics, and Renderer Stats panels.
- Fixed shader ABI in `Shaders/RenderBuilderShaderABI.hlsli`.
- Successful shader compiles rebuild the D3D12 PSO; failed compiles keep the last valid PSO and show diagnostics.
- Built-in preview cube plus Assimp scene import for glTF/GLB, FBX, and OBJ preview meshes.
- DirectXTex base-color texture upload for DDS/TGA/HDR/WIC images, with a checker fallback.
- Imported materials are rendered through per-material draw routing and shader set assignment.

## LookDev Roadmap

- D3D12-first PBR preview using a dedicated `LookDevPBR.hlsl` shader.
- HDRI + Sun environment controls, tone mapping, debug display modes, and turntable preview.
- Project JSON persists scene, material slots, shader assignments, camera, sky, environment, and view settings.

## Planned Milestones

- M2: LookDev PBR/IBL workflow, richer shader set management, and snapshot/export polish.
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
