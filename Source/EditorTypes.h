#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rb
{
enum class PipelineKind
{
    RasterVSPS,
    MeshShader,
    RayTracing
};

enum class RenderBackendKind
{
    D3D12,
    Vulkan
};

struct ShaderCompileRequest
{
    std::wstring sourceName;
    std::wstring entryPoint;
    std::wstring profile;
    std::wstring includeDirectory;
    std::string source;
    bool debug = true;
    bool spirv = false;
};

struct ShaderCompileResult
{
    bool succeeded = false;
    std::string diagnostics;
    std::vector<std::uint8_t> bytecode;
};

struct ShaderSet
{
    std::string name = "Default Raster Shader";
    std::wstring sourcePath;
    std::string sourceText;
    std::wstring vertexEntry = L"VSMain";
    std::wstring pixelEntry = L"PSMain";
    std::wstring vertexProfile = L"vs_6_9";
    std::wstring pixelProfile = L"ps_6_9";
    PipelineKind pipelineKind = PipelineKind::RasterVSPS;
};

struct MaterialAssignment
{
    std::string materialName = "Default Material";
    std::string shaderSetName = "Default Raster Shader";
    std::array<std::wstring, 4> textureOverrides;
    std::array<bool, 4> textureOverrideEnabled = {};
    std::array<float, 4> baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float roughnessFactor = 0.48f;
    float metallicFactor = 0.0f;
    float normalStrength = 1.0f;
    bool flipNormalGreen = false;
};

struct ProjectFile
{
    std::wstring path;
    std::wstring scenePath;
    std::array<float, 4> skyTopColor = { 0.12f, 0.22f, 0.36f, 1.0f };
    std::array<float, 4> skyHorizonColor = { 0.035f, 0.045f, 0.055f, 1.0f };
    std::vector<ShaderSet> shaderSets;
    std::vector<MaterialAssignment> materialAssignments;
};

struct BackendCapabilities
{
    bool rasterVSPS = true;
    bool meshShader = false;
    bool rayTracing = false;
    bool shaderModel69 = false;
    std::string adapterName;
};
}
