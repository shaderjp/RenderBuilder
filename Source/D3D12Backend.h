#pragma once

#include "EditorTypes.h"
#include "SceneTypes.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct ImGui_ImplDX12_InitInfo;

namespace rb
{
class D3D12Backend
{
public:
    static constexpr UINT FrameCount = 3;

    D3D12Backend() = default;
    ~D3D12Backend();

    D3D12Backend(const D3D12Backend&) = delete;
    D3D12Backend& operator=(const D3D12Backend&) = delete;

    void Initialize(HWND hwnd, UINT width, UINT height);
    void Shutdown();
    void Resize(UINT width, UINT height);
    void Render(float deltaSeconds, const std::vector<std::uint8_t>& vertexShader, const std::vector<std::uint8_t>& pixelShader);
    bool TryApplyShaders(const std::vector<std::uint8_t>& vertexShader, const std::vector<std::uint8_t>& pixelShader, std::string& diagnostics);
    bool TryApplyShaders(const std::string& shaderSetName, const std::vector<std::uint8_t>& vertexShader, const std::vector<std::uint8_t>& pixelShader, std::string& diagnostics);
    bool LoadSceneMesh(const ImportedScene& scene, std::string& diagnostics);
    void SetMaterialAssignments(const std::vector<MaterialAssignment>& assignments);
    bool UpdateMaterialTextureSlot(const std::string& materialName, std::uint32_t textureSlot, const std::wstring& path, std::string& diagnostics);
    void ResizeSceneTarget(UINT width, UINT height);
    void SetSkyColors(const std::array<float, 4>& topColor, const std::array<float, 4>& horizonColor);
    void SetLookDevEnvironment(const LookDevEnvironment& environment);
    void SetLookDevViewSettings(const LookDevViewSettings& viewSettings);
    bool UpdateEnvironmentTexture(const std::wstring& path, std::string& diagnostics);
    void SetDebugViewMode(LookDevDisplayMode displayMode);
    bool SaveSceneSnapshot(const std::wstring& path, std::string& diagnostics);
    void ResetPreviewScene();
    void ResetCameraToScene();
    ViewportCamera CameraState() const;
    void SetCameraState(const ViewportCamera& camera);
    void OrbitCamera(float yawDeltaRadians, float pitchDeltaRadians);
    void PanCamera(float rightDelta, float upDelta);
    void DollyCamera(float wheelDelta);
    void MoveCamera(float forwardDelta, float rightDelta, float upDelta);

    ID3D12Device* Device() const { return m_device.Get(); }
    ID3D12CommandQueue* CommandQueue() const { return m_commandQueue.Get(); }
    ID3D12DescriptorHeap* SrvHeap() const { return m_srvHeap.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE SceneSrvGpu() const { return m_sceneSrvGpu; }
    D3D12_CPU_DESCRIPTOR_HANDLE SceneSrvCpu() const { return m_sceneSrvCpu; }
    UINT SceneWidth() const { return m_sceneWidth; }
    UINT SceneHeight() const { return m_sceneHeight; }
    BackendCapabilities Capabilities() const { return m_capabilities; }
    double LastFrameMs() const { return m_lastFrameMs; }
    UINT64 FrameNumber() const { return m_frameNumber; }
    UINT IndexCount() const { return m_indexCount; }
    UINT VertexCount() const { return m_vertexCount; }
    bool HasValidPipeline() const { return m_pipelineState != nullptr; }
    bool HasEnvironmentTexture() const { return m_hasEnvironmentTexture; }
    std::string EnvironmentStatus() const { return m_environmentStatus; }

private:
    struct SceneConstants
    {
        DirectX::XMFLOAT4X4 modelViewProjection;
        DirectX::XMFLOAT4X4 model;
        DirectX::XMFLOAT4X4 viewProjectionInverse;
        DirectX::XMFLOAT4 cameraPositionTime;
        DirectX::XMFLOAT4 lightDirectionIntensity;
    };

    struct MaterialConstants
    {
        DirectX::XMFLOAT4 baseColorFactor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        UINT textureMask = 0;
        float normalStrength = 1.0f;
        float normalGreenScale = 1.0f;
        float roughnessFactor = 0.48f;
        float metallicFactor = 0.0f;
        float occlusionStrength = 1.0f;
        float alphaCutoff = 0.5f;
        float alphaMode = 0.0f;
        DirectX::XMFLOAT4 emissiveFactor = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
    };

    struct LookDevConstants
    {
        DirectX::XMFLOAT4 sunColorIntensity = DirectX::XMFLOAT4(1.0f, 0.96f, 0.88f, 1.2f);
        DirectX::XMFLOAT4 environmentOptions = DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, 0.0f);
        DirectX::XMFLOAT4 viewOptions = DirectX::XMFLOAT4(0.0f, 2.2f, 2.0f, 0.0f);
        DirectX::XMFLOAT4 iblOptions = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        DirectX::XMFLOAT4 skyTopColor = DirectX::XMFLOAT4(0.12f, 0.22f, 0.36f, 1.0f);
        DirectX::XMFLOAT4 skyHorizonColor = DirectX::XMFLOAT4(0.035f, 0.045f, 0.055f, 1.0f);
    };

    struct SkyConstants
    {
        DirectX::XMFLOAT4 topColor = DirectX::XMFLOAT4(0.12f, 0.22f, 0.36f, 1.0f);
        DirectX::XMFLOAT4 horizonColor = DirectX::XMFLOAT4(0.035f, 0.045f, 0.055f, 1.0f);
    };

    struct RenderMaterial
    {
        std::string name = "Default Material";
        std::string shaderSetName = "Default Raster Shader";
        std::array<std::wstring, static_cast<std::size_t>(TextureSlot::Count)> texturePaths;
        MaterialConstants constants;
        D3D12_GPU_DESCRIPTOR_HANDLE textureTableGpu = {};
    };

    struct SrvAllocator
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = {};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {};
        UINT descriptorSize = 0;
        UINT capacity = 0;
        UINT used = 0;
    };

    static void ImGuiSrvAllocate(::ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu);
    static void ImGuiSrvFree(::ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu);

    void CreateDeviceObjects(HWND hwnd, UINT width, UINT height);
    void CreateRenderTargets();
    void CreateSceneTarget();
    void CreateRootSignature();
    void CreateGeometry();
    void CreateMeshBuffers(const std::vector<SceneVertex>& vertices, const std::vector<std::uint32_t>& indices);
    void CreateDefaultMaterialResources();
    bool CreateMaterialTexture(const std::wstring& path, UINT descriptorIndex, Microsoft::WRL::ComPtr<ID3D12Resource>& texture, std::string& diagnostics);
    bool CreateTextureFromFile(const std::wstring& path, UINT descriptorIndex, Microsoft::WRL::ComPtr<ID3D12Resource>& texture, std::string& diagnostics);
    void CreateFallbackTexture();
    void CreateFallbackSrv(UINT descriptorIndex);
    void UploadTextureSubresources(ID3D12Resource* texture, const std::vector<D3D12_SUBRESOURCE_DATA>& subresources);
    void CreateConstantBuffer();
    void CreatePipelineState(const std::vector<std::uint8_t>& vertexShader, const std::vector<std::uint8_t>& pixelShader, Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPipelineState);
    void CreateSkyPipelineState();
    void DrawSky();
    void InitializeImGui(HWND hwnd);
    void ReleaseRenderTargets();
    void WaitForGpu();
    void MoveToNextFrame();
    void UpdateConstants(float deltaSeconds);
    void CameraBasis(DirectX::XMVECTOR& forward, DirectX::XMVECTOR& right, DirectX::XMVECTOR& up) const;
    float SceneRadius() const;
    ID3D12PipelineState* PipelineForMaterial(const RenderMaterial& material) const;
    const RenderMaterial& MaterialForDraw(const SceneDraw& draw) const;
    D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle(UINT index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE SrvCpuHandle(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE SrvGpuHandle(UINT index) const;
    void ThrowIfFailed(HRESULT hr, const char* message) const;

    HWND m_hwnd = nullptr;
    UINT m_width = 0;
    UINT m_height = 0;
    UINT m_sceneWidth = 1280;
    UINT m_sceneHeight = 720;
    DXGI_FORMAT m_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT m_sceneFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    DXGI_FORMAT m_sceneDepthFormat = DXGI_FORMAT_D32_FLOAT;

    Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, FrameCount> m_commandAllocators;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FrameCount> m_renderTargets;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    std::array<UINT64, FrameCount> m_fenceValues = {};
    UINT m_frameIndex = 0;
    UINT m_rtvDescriptorSize = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineState;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_skyPipelineState;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> m_pipelineStates;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneTarget;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneDepth;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_fallbackTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_environmentTexture;
    std::vector<std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, static_cast<std::size_t>(TextureSlot::Count)>> m_materialTextures;
    std::vector<RenderMaterial> m_materials;
    std::vector<SceneDraw> m_draws;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW m_indexBufferView = {};
    UINT m_vertexCount = 0;
    UINT m_indexCount = 0;
    std::uint8_t* m_constantBufferMapped = nullptr;
    D3D12_RESOURCE_STATES m_sceneTargetState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_CPU_DESCRIPTOR_HANDLE m_sceneSrvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_sceneSrvGpu = {};
    SrvAllocator m_srvAllocator;

    BackendCapabilities m_capabilities;
    float m_elapsedSeconds = 0.0f;
    DirectX::XMFLOAT3 m_boundsMin = DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f);
    DirectX::XMFLOAT3 m_boundsMax = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
    DirectX::XMFLOAT3 m_cameraTarget = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    float m_cameraYaw = 0.0f;
    float m_cameraPitch = 0.12f;
    float m_cameraDistance = 4.0f;
    float m_cameraMoveScale = 1.0f;
    bool m_loadedSceneMesh = false;
    double m_lastFrameMs = 0.0;
    UINT64 m_frameNumber = 0;
    bool m_imguiInitialized = false;
    SkyConstants m_skyConstants;
    LookDevEnvironment m_lookDevEnvironment;
    LookDevViewSettings m_lookDevViewSettings;
    LookDevConstants m_lookDevConstants;
    bool m_hasEnvironmentTexture = false;
    UINT m_environmentMipLevels = 1;
    std::string m_environmentStatus = "Using SkyColor background.";
};
}
