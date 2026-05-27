#include "D3D12Backend.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <DirectXTex.h>
#include <d3dcompiler.h>
#include <wincodec.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

extern "C" __declspec(dllexport) const UINT D3D12SDKVersion = 619;
extern "C" __declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
constexpr UINT SceneSrvDescriptorIndex = 0;
constexpr UINT MaterialTextureSlotCount = static_cast<UINT>(rb::TextureSlot::Count);
constexpr UINT TextureSlotBaseColor = static_cast<UINT>(rb::TextureSlot::BaseColor);
constexpr UINT TextureSlotNormal = static_cast<UINT>(rb::TextureSlot::Normal);
constexpr UINT TextureSlotRoughness = static_cast<UINT>(rb::TextureSlot::Roughness);
constexpr UINT TextureSlotMetallic = static_cast<UINT>(rb::TextureSlot::Metallic);
constexpr UINT TextureSlotOcclusion = static_cast<UINT>(rb::TextureSlot::Occlusion);
constexpr UINT TextureSlotEmissive = static_cast<UINT>(rb::TextureSlot::Emissive);
constexpr UINT MaterialSrvDescriptorStart = 1;
constexpr UINT MaxMaterialCount = 1024;
constexpr UINT EnvironmentSrvDescriptorIndex = MaterialSrvDescriptorStart + MaxMaterialCount * MaterialTextureSlotCount;
constexpr UINT ShadowSrvDescriptorIndex = EnvironmentSrvDescriptorIndex + 1;
constexpr UINT ImGuiSrvDescriptorStart = ShadowSrvDescriptorIndex + 1;
constexpr UINT SrvDescriptorCapacity = ImGuiSrvDescriptorStart + 512;
constexpr UINT MaterialTextureBaseColorBit = 1u << TextureSlotBaseColor;
constexpr UINT MaterialTextureNormalBit = 1u << TextureSlotNormal;
constexpr UINT MaterialTextureRoughnessBit = 1u << TextureSlotRoughness;
constexpr UINT MaterialTextureMetallicBit = 1u << TextureSlotMetallic;
constexpr UINT MaterialTextureOcclusionBit = 1u << TextureSlotOcclusion;
constexpr UINT MaterialTextureEmissiveBit = 1u << TextureSlotEmissive;
constexpr DWORD GpuWaitTimeoutMs = 5000;
constexpr DWORD ResizeGpuWaitTimeoutMs = 100;
constexpr DXGI_FORMAT ShadowMapFormat = DXGI_FORMAT_R32_TYPELESS;
constexpr DXGI_FORMAT ShadowDsvFormat = DXGI_FORMAT_D32_FLOAT;
constexpr DXGI_FORMAT ShadowSrvFormat = DXGI_FORMAT_R32_FLOAT;

UINT NormalizeShadowResolution(std::uint32_t resolution)
{
    if (resolution <= 1024)
    {
        return 1024;
    }
    if (resolution <= 2048)
    {
        return 2048;
    }
    return 4096;
}

D3D12_RESOURCE_DESC BufferDesc(UINT64 size)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES props = {};
    props.Type = type;
    props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    props.CreationNodeMask = 1;
    props.VisibleNodeMask = 1;
    return props;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

D3D12_RENDER_TARGET_BLEND_DESC DefaultBlend()
{
    D3D12_RENDER_TARGET_BLEND_DESC desc = {};
    desc.BlendEnable = FALSE;
    desc.LogicOpEnable = FALSE;
    desc.SrcBlend = D3D12_BLEND_ONE;
    desc.DestBlend = D3D12_BLEND_ZERO;
    desc.BlendOp = D3D12_BLEND_OP_ADD;
    desc.SrcBlendAlpha = D3D12_BLEND_ONE;
    desc.DestBlendAlpha = D3D12_BLEND_ZERO;
    desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    desc.LogicOp = D3D12_LOGIC_OP_NOOP;
    desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return desc;
}

std::string WideAdapterName(const DXGI_ADAPTER_DESC1& desc)
{
    char buffer[128] = {};
    WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, buffer, static_cast<int>(sizeof(buffer)), nullptr, nullptr);
    return buffer;
}

std::wstring LowerExtension(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return extension;
}

std::wstring TextureCacheKey(const std::wstring& path)
{
    std::filesystem::path normalized = std::filesystem::absolute(std::filesystem::path(path)).lexically_normal();
    std::wstring key = normalized.generic_wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return key;
}

std::string HResultMessage(HRESULT hr)
{
    std::ostringstream message;
    message << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
    return message.str();
}

void TraceRenderBuilder(const std::string& message)
{
    const std::string debugLine = "RenderBuilder: " + message + "\n";
    OutputDebugStringA(debugLine.c_str());

    try
    {
        std::filesystem::create_directories("Bin/Logs");
        std::ofstream log("Bin/Logs/RenderBuilder.log", std::ios::app);
        log << GetTickCount64() << " " << message << "\n";
    }
    catch (...)
    {
    }
}

float ClampFloat(float value, float minimum, float maximum)
{
    return std::max(minimum, std::min(maximum, value));
}
}

namespace rb
{
D3D12Backend::~D3D12Backend()
{
    Shutdown();
}

void D3D12Backend::ThrowIfFailed(HRESULT hr, const char* message) const
{
    if (FAILED(hr))
    {
        throw std::runtime_error(message);
    }
}

void D3D12Backend::Initialize(HWND hwnd, UINT width, UINT height)
{
    m_hwnd = hwnd;
    m_width = std::max(width, 1u);
    m_height = std::max(height, 1u);
    CreateDeviceObjects(hwnd, m_width, m_height);
    CreateRootSignature();
    CreateSkyPipelineState();
    CreateShadowPipelineState();
    CreateGeometry();
    CreateConstantBuffer();
    CreateSceneTarget();
    CreateShadowResources();
    CreateDefaultMaterialResources();
    InitializeImGui(hwnd);
}

void D3D12Backend::Shutdown()
{
    if (m_device)
    {
        WaitForGpu();
    }

    if (m_imguiInitialized)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_imguiInitialized = false;
    }

    if (m_constantBuffer)
    {
        m_constantBuffer->Unmap(0, nullptr);
        m_constantBufferMapped = nullptr;
    }

    ReleaseRenderTargets();
    m_sceneTarget.Reset();
    m_sceneDepth.Reset();
    m_shadowMap.Reset();
    m_fallbackTexture.Reset();
    m_environmentTexture.Reset();
    m_materialTextures.clear();
    m_materials.clear();
    m_draws.clear();
    m_pipelineState.Reset();
    m_skyPipelineState.Reset();
    m_shadowPipelineState.Reset();
    m_pipelineStates.clear();
    m_rootSignature.Reset();
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_constantBuffer.Reset();
    m_swapChain.Reset();
    m_commandList.Reset();
    for (auto& allocator : m_commandAllocators)
    {
        allocator.Reset();
    }
    m_commandQueue.Reset();
    m_rtvHeap.Reset();
    m_dsvHeap.Reset();
    m_srvHeap.Reset();
    m_fence.Reset();
    m_device.Reset();
    m_factory.Reset();

    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

void D3D12Backend::CreateDeviceObjects(HWND hwnd, UINT width, UINT height)
{
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)), "CreateDXGIFactory2 failed.");

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT adapterIndex = 0; m_factory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc = {};
        adapter->GetDesc1(&desc);
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device))))
        {
            m_capabilities.adapterName = WideAdapterName(desc);
            break;
        }
    }

    if (!m_device)
    {
        ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)), "D3D12CreateDevice failed.");
        m_capabilities.adapterName = "Default D3D12 Adapter";
    }

    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_9 };
    m_capabilities.shaderModel69 = SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))) && shaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_9;

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
    {
        m_capabilities.meshShader = options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
    }

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
    {
        m_capabilities.rayTracing = options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)), "CreateCommandQueue failed.");

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = m_backBufferFormat;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain), "CreateSwapChainForHwnd failed.");
    ThrowIfFailed(m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation failed.");
    ThrowIfFailed(swapChain.As(&m_swapChain), "Query IDXGISwapChain3 failed.");
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount + 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "CreateDescriptorHeap(RTV) failed.");
    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 2;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)), "CreateDescriptorHeap(DSV) failed.");
    m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = SrvDescriptorCapacity;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap(SRV) failed.");
    m_srvAllocator.cpuStart = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    m_srvAllocator.gpuStart = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    m_srvAllocator.descriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_srvAllocator.capacity = SrvDescriptorCapacity;
    m_srvAllocator.used = ImGuiSrvDescriptorStart;
    m_sceneSrvCpu = m_srvAllocator.cpuStart;
    m_sceneSrvGpu = m_srvAllocator.gpuStart;

    for (UINT i = 0; i < FrameCount; ++i)
    {
        ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i])), "CreateCommandAllocator failed.");
    }
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList)), "CreateCommandList failed.");
    ThrowIfFailed(m_commandList->Close(), "Initial command list close failed.");

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "CreateFence failed.");
    m_fenceValues.fill(0);
    m_nextFenceValue = 1;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent)
    {
        throw std::runtime_error("CreateEvent failed.");
    }

    CreateRenderTargets();
}

void D3D12Backend::ReleaseRenderTargets()
{
    for (auto& renderTarget : m_renderTargets)
    {
        renderTarget.Reset();
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Backend::RtvHandle(UINT index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * m_rtvDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Backend::DsvHandle(UINT index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * m_dsvDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Backend::SrvCpuHandle(UINT index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvAllocator.cpuStart;
    handle.ptr += static_cast<SIZE_T>(index) * m_srvAllocator.descriptorSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE D3D12Backend::SrvGpuHandle(UINT index) const
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_srvAllocator.gpuStart;
    handle.ptr += static_cast<UINT64>(index) * m_srvAllocator.descriptorSize;
    return handle;
}

void D3D12Backend::CreateRenderTargets()
{
    for (UINT i = 0; i < FrameCount; ++i)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i])), "GetBuffer failed.");
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, RtvHandle(i));
    }
}

void D3D12Backend::CreateSceneTarget()
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = m_sceneWidth;
    desc.Height = m_sceneHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = m_sceneFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = m_sceneFormat;
    clearValue.Color[0] = m_skyConstants.horizonColor.x;
    clearValue.Color[1] = m_skyConstants.horizonColor.y;
    clearValue.Color[2] = m_skyConstants.horizonColor.z;
    clearValue.Color[3] = 1.0f;

    const D3D12_HEAP_PROPERTIES heapProps = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&m_sceneTarget)), "Create scene render target failed.");
    m_sceneTargetState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_device->CreateRenderTargetView(m_sceneTarget.Get(), nullptr, RtvHandle(FrameCount));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = m_sceneFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_sceneTarget.Get(), &srvDesc, m_sceneSrvCpu);

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = m_sceneWidth;
    depthDesc.Height = m_sceneHeight;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = m_sceneDepthFormat;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = m_sceneDepthFormat;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    ThrowIfFailed(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(&m_sceneDepth)), "Create scene depth target failed.");
    m_device->CreateDepthStencilView(m_sceneDepth.Get(), nullptr, DsvHandle(0));
}

void D3D12Backend::CreateShadowResources()
{
    if (!m_device)
    {
        return;
    }

    m_shadowResolution = NormalizeShadowResolution(m_lookDevShadowSettings.resolution);

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = m_shadowResolution;
    desc.Height = m_shadowResolution;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = ShadowMapFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = ShadowDsvFormat;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    const D3D12_HEAP_PROPERTIES heapProps = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    m_shadowMap.Reset();
    ThrowIfFailed(m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&m_shadowMap)),
        "Create shadow map failed.");
    m_shadowMapState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = ShadowDsvFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_device->CreateDepthStencilView(m_shadowMap.Get(), &dsvDesc, DsvHandle(1));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = ShadowSrvFormat;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_shadowMap.Get(), &srvDesc, SrvCpuHandle(ShadowSrvDescriptorIndex));

    std::ostringstream status;
    status << "Sun shadow " << (m_lookDevShadowSettings.enabled ? "enabled" : "disabled")
           << ": " << m_shadowResolution << ".";
    m_shadowStatus = status.str();
}

void D3D12Backend::CreateRootSignature()
{
    D3D12_DESCRIPTOR_RANGE textureRange = {};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = MaterialTextureSlotCount;
    textureRange.BaseShaderRegister = 0;
    textureRange.RegisterSpace = 0;
    textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE environmentRange = {};
    environmentRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    environmentRange.NumDescriptors = 2;
    environmentRange.BaseShaderRegister = MaterialTextureSlotCount;
    environmentRange.RegisterSpace = 0;
    environmentRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[5] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &textureRange;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[2].Constants.Num32BitValues = sizeof(MaterialConstants) / sizeof(std::uint32_t);
    rootParameters[2].Constants.ShaderRegister = 1;
    rootParameters[2].Constants.RegisterSpace = 0;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[3].Constants.Num32BitValues = sizeof(LookDevConstants) / sizeof(std::uint32_t);
    rootParameters[3].Constants.ShaderRegister = 2;
    rootParameters[3].Constants.RegisterSpace = 0;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges = &environmentRange;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].MipLODBias = 0.0f;
    samplers[0].MaxAnisotropy = 1;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[0].MinLOD = 0.0f;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].MipLODBias = 0.0f;
    samplers[1].MaxAnisotropy = 1;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].MinLOD = 0.0f;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParameters);
    rootDesc.pParameters = rootParameters;
    rootDesc.NumStaticSamplers = _countof(samplers);
    rootDesc.pStaticSamplers = samplers;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    const HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    if (FAILED(hr))
    {
        std::string message = "D3D12SerializeRootSignature failed.";
        if (error)
        {
            message.append("\n");
            message.append(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        }
        throw std::runtime_error(message);
    }
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)), "CreateRootSignature failed.");
}

void D3D12Backend::CreateGeometry()
{
    const std::array<SceneVertex, 24> vertices =
    {{
        {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{-1.0f, -1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{-1.0f,  1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{-1.0f, -1.0f,  1.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{-1.0f, -1.0f,  1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
    }};

    const std::array<std::uint32_t, 36> indices =
    {{
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23,
    }};

    m_boundsMin = XMFLOAT3(-1.0f, -1.0f, -1.0f);
    m_boundsMax = XMFLOAT3(1.0f, 1.0f, 1.0f);
    m_loadedSceneMesh = false;
    CreateMeshBuffers(std::vector<SceneVertex>(vertices.begin(), vertices.end()), std::vector<std::uint32_t>(indices.begin(), indices.end()));
    m_draws = { { static_cast<std::uint32_t>(indices.size()), 0, 0, 0 } };
    ResetCameraToScene();
}

void D3D12Backend::CreateMeshBuffers(const std::vector<SceneVertex>& vertices, const std::vector<std::uint32_t>& indices)
{
    if (vertices.empty() || indices.empty())
    {
        throw std::runtime_error("Cannot create mesh buffers with empty geometry.");
    }

    WaitForGpu();

    const UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(SceneVertex));
    const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
    const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

    D3D12_RESOURCE_DESC vertexDesc = BufferDesc(vertexBufferSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer)), "Create vertex buffer failed.");
    void* mapped = nullptr;
    ThrowIfFailed(vertexBuffer->Map(0, nullptr, &mapped), "Map vertex buffer failed.");
    std::memcpy(mapped, vertices.data(), vertexBufferSize);
    vertexBuffer->Unmap(0, nullptr);
    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = vertexBufferSize;
    vertexBufferView.StrideInBytes = sizeof(SceneVertex);

    D3D12_RESOURCE_DESC indexDesc = BufferDesc(indexBufferSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &indexDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBuffer)), "Create index buffer failed.");
    ThrowIfFailed(indexBuffer->Map(0, nullptr, &mapped), "Map index buffer failed.");
    std::memcpy(mapped, indices.data(), indexBufferSize);
    indexBuffer->Unmap(0, nullptr);
    indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    indexBufferView.SizeInBytes = indexBufferSize;

    m_vertexBuffer = vertexBuffer;
    m_indexBuffer = indexBuffer;
    m_vertexBufferView = vertexBufferView;
    m_indexBufferView = indexBufferView;
    m_vertexCount = static_cast<UINT>(vertices.size());
    m_indexCount = static_cast<UINT>(indices.size());
}

void D3D12Backend::UploadTextureSubresources(ID3D12Resource* texture, const std::vector<D3D12_SUBRESOURCE_DATA>& subresources)
{
    if (!texture || subresources.empty())
    {
        throw std::runtime_error("Texture upload requested with no texture data.");
    }

    WaitForGpu();

    const D3D12_RESOURCE_DESC textureDesc = texture->GetDesc();
    const UINT subresourceCount = static_cast<UINT>(subresources.size());
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresourceCount);
    std::vector<UINT> rowCounts(subresourceCount);
    std::vector<UINT64> rowSizes(subresourceCount);
    UINT64 uploadSize = 0;
    m_device->GetCopyableFootprints(&textureDesc, 0, subresourceCount, 0, layouts.data(), rowCounts.data(), rowSizes.data(), &uploadSize);

    ComPtr<ID3D12Resource> uploadBuffer;
    const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC uploadDesc = BufferDesc(uploadSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)), "Create texture upload buffer failed.");

    std::uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = {};
    ThrowIfFailed(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped)), "Map texture upload buffer failed.");
    for (UINT subresourceIndex = 0; subresourceIndex < subresourceCount; ++subresourceIndex)
    {
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout = layouts[subresourceIndex];
        const D3D12_SUBRESOURCE_DATA& source = subresources[subresourceIndex];
        const UINT rows = rowCounts[subresourceIndex];
        const UINT64 rowSize = rowSizes[subresourceIndex];
        const UINT depth = layout.Footprint.Depth;

        for (UINT z = 0; z < depth; ++z)
        {
            std::uint8_t* destinationSlice = mapped + layout.Offset + static_cast<SIZE_T>(layout.Footprint.RowPitch) * rows * z;
            const std::uint8_t* sourceSlice = static_cast<const std::uint8_t*>(source.pData) + source.SlicePitch * z;
            for (UINT y = 0; y < rows; ++y)
            {
                std::memcpy(destinationSlice + static_cast<SIZE_T>(layout.Footprint.RowPitch) * y, sourceSlice + source.RowPitch * y, static_cast<std::size_t>(rowSize));
            }
        }
    }
    uploadBuffer->Unmap(0, nullptr);

    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset(), "Texture upload command allocator reset failed.");
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr), "Texture upload command list reset failed.");

    D3D12_RESOURCE_BARRIER toCopy = Transition(texture, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    m_commandList->ResourceBarrier(1, &toCopy);
    for (UINT subresourceIndex = 0; subresourceIndex < subresourceCount; ++subresourceIndex)
    {
        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = texture;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = subresourceIndex;

        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = uploadBuffer.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = layouts[subresourceIndex];

        m_commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }
    D3D12_RESOURCE_BARRIER toShaderResource = Transition(texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_commandList->ResourceBarrier(1, &toShaderResource);

    ThrowIfFailed(m_commandList->Close(), "Texture upload command list close failed.");
    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, commandLists);
    WaitForGpu();
}

void D3D12Backend::CreateFallbackTexture()
{
    if (m_fallbackTexture)
    {
        return;
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 2;
    desc.Height = 2;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_fallbackTexture)), "Create fallback texture failed.");

    const std::array<std::uint8_t, 16> pixels =
    {{
        32, 96, 178, 255, 222, 135, 46, 255,
        222, 135, 46, 255, 32, 96, 178, 255,
    }};
    D3D12_SUBRESOURCE_DATA subresource = {};
    subresource.pData = pixels.data();
    subresource.RowPitch = 2 * 4;
    subresource.SlicePitch = pixels.size();
    UploadTextureSubresources(m_fallbackTexture.Get(), { subresource });
}

void D3D12Backend::CreateFallbackSrv(UINT descriptorIndex)
{
    CreateFallbackTexture();
    m_device->CreateShaderResourceView(m_fallbackTexture.Get(), nullptr, SrvCpuHandle(descriptorIndex));
}

void D3D12Backend::CreateDefaultMaterialResources()
{
    for (UINT textureSlot = 0; textureSlot < MaterialTextureSlotCount; ++textureSlot)
    {
        CreateFallbackSrv(MaterialSrvDescriptorStart + textureSlot);
    }
    if (!m_hasEnvironmentTexture)
    {
        CreateFallbackSrv(EnvironmentSrvDescriptorIndex);
    }
    m_materialTextures.clear();
    m_materials.clear();

    RenderMaterial material = {};
    material.name = "Default Material";
    material.shaderSetName = "LookDev PBR";
    material.constants.baseColorFactor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    material.constants.textureMask = 0;
    material.textureTableGpu = SrvGpuHandle(MaterialSrvDescriptorStart);
    m_materialTextures.resize(1);
    m_materials.push_back(material);
}

bool D3D12Backend::CreateMaterialTexture(const std::wstring& path, UINT descriptorIndex, ComPtr<ID3D12Resource>& texture, std::string& diagnostics)
{
    return CreateTextureFromFile(path, descriptorIndex, texture, diagnostics);
}

bool D3D12Backend::CreateTextureFromFile(const std::wstring& path, UINT descriptorIndex, ComPtr<ID3D12Resource>& texture, std::string& diagnostics)
{
    try
    {
        DirectX::TexMetadata metadata = {};
        DirectX::ScratchImage scratchImage;
        const std::wstring extension = LowerExtension(path);

        HRESULT hr = E_FAIL;
        if (extension == L".dds")
        {
            hr = DirectX::LoadFromDDSFile(path.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, scratchImage);
        }
        else if (extension == L".tga")
        {
            hr = DirectX::LoadFromTGAFile(path.c_str(), DirectX::TGA_FLAGS_NONE, &metadata, scratchImage);
        }
        else if (extension == L".hdr")
        {
            hr = DirectX::LoadFromHDRFile(path.c_str(), &metadata, scratchImage);
        }
        else
        {
            hr = DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, &metadata, scratchImage);
        }

        if (FAILED(hr))
        {
            diagnostics = "DirectXTex failed to load " + std::filesystem::path(path).filename().string() + ": " + HResultMessage(hr);
            return false;
        }

        if (!DirectX::IsSupportedTexture(m_device.Get(), metadata))
        {
            const DXGI_FORMAT fallbackFormat = metadata.format == DXGI_FORMAT_R32G32B32_FLOAT
                ? DXGI_FORMAT_R32G32B32A32_FLOAT
                : DXGI_FORMAT_R8G8B8A8_UNORM;
            DirectX::ScratchImage convertedImage;
            hr = DirectX::Convert(scratchImage.GetImages(), scratchImage.GetImageCount(), metadata, fallbackFormat, DirectX::TEX_FILTER_DEFAULT, 0.0f, convertedImage);
            if (SUCCEEDED(hr) && DirectX::IsSupportedTexture(m_device.Get(), convertedImage.GetMetadata()))
            {
                scratchImage = std::move(convertedImage);
                metadata = scratchImage.GetMetadata();
            }
        }

        if (!DirectX::IsSupportedTexture(m_device.Get(), metadata))
        {
            diagnostics = "Texture format is not supported by the active D3D12 device: " + std::filesystem::path(path).filename().string();
            return false;
        }

        if (metadata.mipLevels <= 1 && metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE2D && metadata.width > 1 && metadata.height > 1)
        {
            DirectX::ScratchImage mipChain;
            hr = DirectX::GenerateMipMaps(scratchImage.GetImages(), scratchImage.GetImageCount(), metadata, DirectX::TEX_FILTER_DEFAULT, 0, mipChain);
            if (SUCCEEDED(hr))
            {
                scratchImage = std::move(mipChain);
                metadata = scratchImage.GetMetadata();
            }
        }

        hr = DirectX::CreateTexture(m_device.Get(), metadata, texture.GetAddressOf());
        if (FAILED(hr))
        {
            diagnostics = "DirectXTex failed to create texture " + std::filesystem::path(path).filename().string() + ": " + HResultMessage(hr);
            return false;
        }

        std::vector<D3D12_SUBRESOURCE_DATA> subresources;
        hr = DirectX::PrepareUpload(m_device.Get(), scratchImage.GetImages(), scratchImage.GetImageCount(), metadata, subresources);
        if (FAILED(hr))
        {
            diagnostics = "DirectXTex failed to prepare upload data for " + std::filesystem::path(path).filename().string() + ": " + HResultMessage(hr);
            texture.Reset();
            return false;
        }

        UploadTextureSubresources(texture.Get(), subresources);
        m_device->CreateShaderResourceView(texture.Get(), nullptr, SrvCpuHandle(descriptorIndex));
        diagnostics = "Loaded texture " + std::filesystem::path(path).filename().string();
        return true;
    }
    catch (const std::exception& ex)
    {
        diagnostics = ex.what();
        texture.Reset();
        return false;
    }
}

bool D3D12Backend::LoadSceneMesh(const ImportedScene& scene, std::string& diagnostics)
{
    try
    {
        if (scene.vertices.empty() || scene.indices.empty())
        {
            diagnostics = "Imported scene has no renderable mesh buffers.";
            return false;
        }

        CreateMeshBuffers(scene.vertices, scene.indices);
        m_boundsMin = scene.boundsMin;
        m_boundsMax = scene.boundsMax;
        m_loadedSceneMesh = true;
        ResetCameraToScene();

        m_draws = scene.draws;
        if (m_draws.empty())
        {
            m_draws.push_back({ static_cast<std::uint32_t>(scene.indices.size()), 0, 0, 0 });
        }

        CreateFallbackTexture();
        m_materialTextures.clear();
        m_materials.clear();

        const std::size_t requestedMaterialCount = std::max<std::size_t>(scene.materials.size(), 1);
        const std::size_t materialCount = std::min<std::size_t>(requestedMaterialCount, MaxMaterialCount);
        m_materialTextures.resize(materialCount);

        std::ostringstream output;
        output << "D3D12 mesh buffers updated from imported scene.";
        std::array<std::size_t, MaterialTextureSlotCount> loadedTextureCounts = {};
        std::unordered_map<std::wstring, ComPtr<ID3D12Resource>> textureCache;
        const std::array<const char*, MaterialTextureSlotCount> textureSlotNames =
        {
            "base color",
            "normal",
            "roughness",
            "metallic",
            "occlusion",
            "emissive",
        };
        const std::array<UINT, MaterialTextureSlotCount> textureSlotBits =
        {
            MaterialTextureBaseColorBit,
            MaterialTextureNormalBit,
            MaterialTextureRoughnessBit,
            MaterialTextureMetallicBit,
            MaterialTextureOcclusionBit,
            MaterialTextureEmissiveBit,
        };
        for (std::size_t materialIndex = 0; materialIndex < materialCount; ++materialIndex)
        {
            const UINT descriptorBase = MaterialSrvDescriptorStart + static_cast<UINT>(materialIndex) * MaterialTextureSlotCount;
            for (UINT textureSlot = 0; textureSlot < MaterialTextureSlotCount; ++textureSlot)
            {
                CreateFallbackSrv(descriptorBase + textureSlot);
            }

            RenderMaterial renderMaterial = {};
            if (materialIndex < scene.materials.size())
            {
                const SceneMaterial& sceneMaterial = scene.materials[materialIndex];
                renderMaterial.name = sceneMaterial.assignment.materialName;
                renderMaterial.shaderSetName = sceneMaterial.assignment.shaderSetName;
                renderMaterial.texturePaths[TextureSlotBaseColor] = sceneMaterial.baseColorTexturePath;
                renderMaterial.texturePaths[TextureSlotNormal] = sceneMaterial.normalTexturePath;
                renderMaterial.texturePaths[TextureSlotRoughness] = sceneMaterial.roughnessTexturePath;
                renderMaterial.texturePaths[TextureSlotMetallic] = sceneMaterial.metallicTexturePath;
                renderMaterial.texturePaths[TextureSlotOcclusion] = sceneMaterial.occlusionTexturePath;
                renderMaterial.texturePaths[TextureSlotEmissive] = sceneMaterial.emissiveTexturePath;
                renderMaterial.constants.baseColorFactor = sceneMaterial.baseColorFactor;
                renderMaterial.constants.emissiveFactor = sceneMaterial.emissiveFactor;
                renderMaterial.constants.roughnessFactor = sceneMaterial.assignment.roughnessFactor;
                renderMaterial.constants.metallicFactor = sceneMaterial.assignment.metallicFactor;
                renderMaterial.constants.occlusionStrength = sceneMaterial.assignment.occlusionStrength;
                renderMaterial.constants.alphaCutoff = sceneMaterial.assignment.alphaCutoff;
                renderMaterial.constants.alphaMode = static_cast<float>(sceneMaterial.assignment.alphaMode);
                renderMaterial.constants.packedOcclusionRoughnessMetallic = sceneMaterial.assignment.packedOcclusionRoughnessMetallic ? 1.0f : 0.0f;
            }
            else
            {
                renderMaterial.name = "Default Material";
                renderMaterial.shaderSetName = "LookDev PBR";
                renderMaterial.constants.baseColorFactor = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            }
            renderMaterial.textureTableGpu = SrvGpuHandle(descriptorBase);
            renderMaterial.constants.textureMask = 0;

            for (UINT textureSlot = 0; textureSlot < MaterialTextureSlotCount; ++textureSlot)
            {
                const std::wstring& texturePath = renderMaterial.texturePaths[textureSlot];
                if (texturePath.empty())
                {
                    continue;
                }

                const std::wstring textureKey = TextureCacheKey(texturePath);
                const auto cachedTexture = textureCache.find(textureKey);
                if (cachedTexture != textureCache.end())
                {
                    m_materialTextures[materialIndex][textureSlot] = cachedTexture->second;
                    m_device->CreateShaderResourceView(cachedTexture->second.Get(), nullptr, SrvCpuHandle(descriptorBase + textureSlot));
                    renderMaterial.constants.textureMask |= textureSlotBits[textureSlot];
                    ++loadedTextureCounts[textureSlot];
                    continue;
                }

                std::string textureDiagnostics;
                if (CreateMaterialTexture(texturePath, descriptorBase + textureSlot, m_materialTextures[materialIndex][textureSlot], textureDiagnostics))
                {
                    textureCache[textureKey] = m_materialTextures[materialIndex][textureSlot];
                    renderMaterial.constants.textureMask |= textureSlotBits[textureSlot];
                    ++loadedTextureCounts[textureSlot];
                }
                else
                {
                    output << "\n" << renderMaterial.name << " " << textureSlotNames[textureSlot]
                           << ": " << textureDiagnostics << " Falling back to checker texture.";
                }
            }
            m_materials.push_back(renderMaterial);
        }
        if (requestedMaterialCount > materialCount)
        {
            output << "\nOnly the first " << MaxMaterialCount << " materials received texture descriptors; later materials use material 0.";
        }
        output << "\nLoaded textures for " << m_materials.size() << " materials: "
               << loadedTextureCounts[TextureSlotBaseColor] << " base color, "
               << loadedTextureCounts[TextureSlotNormal] << " normal, "
               << loadedTextureCounts[TextureSlotRoughness] << " roughness, "
               << loadedTextureCounts[TextureSlotMetallic] << " metallic, "
               << loadedTextureCounts[TextureSlotOcclusion] << " occlusion, "
               << loadedTextureCounts[TextureSlotEmissive] << " emissive."
               << "\nUnique uploaded material textures: " << textureCache.size() << ".";
        diagnostics = output.str();
        return true;
    }
    catch (const std::exception& ex)
    {
        diagnostics = ex.what();
        return false;
    }
}

void D3D12Backend::ResetPreviewScene()
{
    CreateGeometry();
    CreateDefaultMaterialResources();
}

void D3D12Backend::CreateConstantBuffer()
{
    const UINT bufferSize = (sizeof(SceneConstants) + 255u) & ~255u;
    const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC desc = BufferDesc(bufferSize);
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantBuffer)), "Create constant buffer failed.");
    D3D12_RANGE readRange = {};
    ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_constantBufferMapped)), "Map constant buffer failed.");
}

void D3D12Backend::CreatePipelineState(const std::vector<std::uint8_t>& vertexShader, const std::vector<std::uint8_t>& pixelShader, ComPtr<ID3D12PipelineState>& outPipelineState)
{
    if (vertexShader.empty() || pixelShader.empty())
    {
        throw std::runtime_error("Cannot create a PSO with empty shader bytecode.");
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vertexShader.data(), vertexShader.size() };
    psoDesc.PS = { pixelShader.data(), pixelShader.size() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0] = DefaultBlend();
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_sceneFormat;
    psoDesc.DSVFormat = m_sceneDepthFormat;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outPipelineState)), "CreateGraphicsPipelineState failed.");
}

void D3D12Backend::CreateSkyPipelineState()
{
    const char* skyShader = R"(
struct SkyVsOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer RenderBuilderSky : register(b1)
{
    float4 gSkyTopColor;
    float4 gSkyHorizonColor;
};

cbuffer RenderBuilderScene : register(b0)
{
    float4x4 gModelViewProjection;
    float4x4 gModel;
    float4x4 gViewProjectionInverse;
    float4 gCameraPositionTime;
    float4 gLightDirectionIntensity;
};

cbuffer RenderBuilderLookDev : register(b2)
{
    float4 gSunColorIntensity;
    float4 gEnvironmentOptions;
    float4 gViewOptions;
    float4 gIblOptions;
    float4 gLookDevSkyTopColor;
    float4 gLookDevSkyHorizonColor;
};

Texture2D gEnvironmentTexture : register(t6);
SamplerState gLinearWrapSampler : register(s0);

SkyVsOut VSMain(uint vertexId : SV_VertexID)
{
    const float2 positions[3] =
    {
        float2(-1.0, -1.0),
        float2(-1.0,  3.0),
        float2( 3.0, -1.0),
    };

    SkyVsOut output;
    const float2 position = positions[vertexId];
    output.position = float4(position, 0.0, 1.0);
    output.uv = float2(position.x * 0.5 + 0.5, -position.y * 0.5 + 0.5);
    return output;
}

float4 PSMain(SkyVsOut input) : SV_Target0
{
    const float2 clip = input.uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    const float4 farPoint = mul(float4(clip, 1.0, 1.0), gViewProjectionInverse);
    float3 direction = normalize(farPoint.xyz / max(abs(farPoint.w), 1.0e-6) - gCameraPositionTime.xyz);

    const float yaw = gEnvironmentOptions.x;
    const float c = cos(yaw);
    const float s = sin(yaw);
    direction = float3(c * direction.x - s * direction.z, direction.y, s * direction.x + c * direction.z);

    const uint backgroundMode = (uint)round(gEnvironmentOptions.z);
    const bool hasEnvironment = gEnvironmentOptions.w > 0.5;
    float3 color;
    if (backgroundMode == 2)
    {
        const float checker = (fmod(floor(input.uv.x * 32.0) + floor(input.uv.y * 32.0), 2.0) == 0.0) ? 0.18 : 0.32;
        color = float3(checker, checker, checker);
    }
    else if (backgroundMode == 1 && hasEnvironment)
    {
        const float2 envUv = float2(atan2(direction.x, direction.z) * 0.159154943 + 0.5, acos(clamp(direction.y, -1.0, 1.0)) * 0.318309886);
        color = gEnvironmentTexture.SampleLevel(gLinearWrapSampler, envUv, 0.0).rgb * gEnvironmentOptions.y;
    }
    else
    {
        const float t = saturate(input.uv.y);
        color = lerp(gLookDevSkyTopColor.rgb, gLookDevSkyHorizonColor.rgb, t);
    }

    color *= exp2(gViewOptions.x);
    const uint toneMapper = (uint)round(gViewOptions.z);
    if (toneMapper == 1)
    {
        color = color / (1.0 + color);
    }
    else if (toneMapper == 2)
    {
        color = saturate((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14));
    }
    color = pow(max(color, 0.0), 1.0 / max(gViewOptions.y, 0.01));
    return float4(color, 1.0);
}
)";

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> error;
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    HRESULT hr = D3DCompile(skyShader, std::strlen(skyShader), "RenderBuilderSky", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &error);
    if (FAILED(hr))
    {
        std::string message = "Sky vertex shader compile failed.";
        if (error)
        {
            message.append("\n");
            message.append(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        }
        throw std::runtime_error(message);
    }

    error.Reset();
    hr = D3DCompile(skyShader, std::strlen(skyShader), "RenderBuilderSky", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &error);
    if (FAILED(hr))
    {
        std::string message = "Sky pixel shader compile failed.";
        if (error)
        {
            message.append("\n");
            message.append(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        }
        throw std::runtime_error(message);
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0] = DefaultBlend();
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_sceneFormat;
    psoDesc.DSVFormat = m_sceneDepthFormat;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_skyPipelineState)), "Create sky pipeline state failed.");
}

void D3D12Backend::CreateShadowPipelineState()
{
    const char* shadowShader = R"(
cbuffer RenderBuilderScene : register(b0)
{
    float4x4 gModelViewProjection;
    float4x4 gModel;
    float4x4 gViewProjectionInverse;
    float4 gCameraPositionTime;
    float4 gLightDirectionIntensity;
    float4x4 gShadowViewProjection;
};

cbuffer RenderBuilderMaterial : register(b1)
{
    float4 gBaseColorFactor;
    uint gMaterialTextureMask;
    float gNormalStrength;
    float gNormalGreenScale;
    float gRoughnessFactor;
    float gMetallicFactor;
    float gOcclusionStrength;
    float gAlphaCutoff;
    float gAlphaMode;
    float4 gEmissiveFactor;
    float gPackedOcclusionRoughnessMetallic;
};

Texture2D gBaseColorTexture : register(t0);
SamplerState gLinearWrapSampler : register(s0);

struct ShadowVsIn
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
    float4 tangent : TANGENT;
};

struct ShadowVsOut
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

ShadowVsOut VSMain(ShadowVsIn input)
{
    ShadowVsOut output;
    const float4 worldPosition = mul(float4(input.position, 1.0), gModel);
    output.position = mul(worldPosition, gShadowViewProjection);
    output.texcoord = input.texcoord;
    return output;
}

void PSMain(ShadowVsOut input)
{
    const uint RB_TEXTURE_BASE_COLOR = 1u << 0;
    if ((uint)round(gAlphaMode) == 1u)
    {
        float alpha = gBaseColorFactor.a;
        if ((gMaterialTextureMask & RB_TEXTURE_BASE_COLOR) != 0)
        {
            alpha *= gBaseColorTexture.Sample(gLinearWrapSampler, input.texcoord).a;
        }
        if (alpha < gAlphaCutoff)
        {
            discard;
        }
    }
}
)";

    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;
    ComPtr<ID3DBlob> error;
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    HRESULT hr = D3DCompile(shadowShader, std::strlen(shadowShader), "RenderBuilderShadow", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &error);
    if (FAILED(hr))
    {
        std::string message = "Shadow vertex shader compile failed.";
        if (error)
        {
            message.append("\n");
            message.append(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        }
        throw std::runtime_error(message);
    }

    error.Reset();
    hr = D3DCompile(shadowShader, std::strlen(shadowShader), "RenderBuilderShadow", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &error);
    if (FAILED(hr))
    {
        std::string message = "Shadow pixel shader compile failed.";
        if (error)
        {
            message.append("\n");
            message.append(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        }
        throw std::runtime_error(message);
    }

    D3D12_INPUT_ELEMENT_DESC inputLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = 1000;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 2.0f;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;
    psoDesc.DSVFormat = ShadowDsvFormat;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_shadowPipelineState)), "Create shadow pipeline state failed.");
}

bool D3D12Backend::TryApplyShaders(const std::vector<std::uint8_t>& vertexShader, const std::vector<std::uint8_t>& pixelShader, std::string& diagnostics)
{
    return TryApplyShaders("Default Raster Shader", vertexShader, pixelShader, diagnostics);
}

bool D3D12Backend::TryApplyShaders(const std::string& shaderSetName, const std::vector<std::uint8_t>& vertexShader, const std::vector<std::uint8_t>& pixelShader, std::string& diagnostics)
{
    try
    {
        ComPtr<ID3D12PipelineState> newPipelineState;
        CreatePipelineState(vertexShader, pixelShader, newPipelineState);
        m_pipelineState = newPipelineState;
        m_pipelineStates[shaderSetName] = newPipelineState;
        diagnostics = "D3D12 PSO rebuilt successfully for shader set '" + shaderSetName + "'.";
        return true;
    }
    catch (const std::exception& ex)
    {
        diagnostics = ex.what();
        return false;
    }
}

void D3D12Backend::RenameShaderSetPipeline(const std::string& previousName, const std::string& newName)
{
    if (previousName == newName)
    {
        return;
    }

    const auto existingPipeline = m_pipelineStates.find(previousName);
    if (existingPipeline == m_pipelineStates.end())
    {
        return;
    }

    m_pipelineStates[newName] = existingPipeline->second;
    m_pipelineStates.erase(existingPipeline);
}

void D3D12Backend::RemoveShaderSetPipeline(const std::string& name)
{
    m_pipelineStates.erase(name);
}

void D3D12Backend::InitializeImGui(HWND hwnd)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo initInfo;
    initInfo.Device = m_device.Get();
    initInfo.CommandQueue = m_commandQueue.Get();
    initInfo.NumFramesInFlight = FrameCount;
    initInfo.RTVFormat = m_backBufferFormat;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = m_srvHeap.Get();
    initInfo.UserData = &m_srvAllocator;
    initInfo.SrvDescriptorAllocFn = &D3D12Backend::ImGuiSrvAllocate;
    initInfo.SrvDescriptorFreeFn = &D3D12Backend::ImGuiSrvFree;
    ImGui_ImplDX12_Init(&initInfo);
    m_imguiInitialized = true;
}

void D3D12Backend::ImGuiSrvAllocate(::ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
{
    auto* allocator = static_cast<SrvAllocator*>(info->UserData);
    IM_ASSERT(allocator != nullptr);
    IM_ASSERT(allocator->used < allocator->capacity);
    const UINT index = allocator->used++;
    cpu->ptr = allocator->cpuStart.ptr + static_cast<SIZE_T>(index) * allocator->descriptorSize;
    gpu->ptr = allocator->gpuStart.ptr + static_cast<UINT64>(index) * allocator->descriptorSize;
}

void D3D12Backend::ImGuiSrvFree(::ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)
{
}

bool D3D12Backend::Resize(UINT width, UINT height)
{
    if (!m_swapChain || width == 0 || height == 0)
    {
        return true;
    }
    if (width == m_width && height == m_height)
    {
        return true;
    }

    {
        std::ostringstream message;
        message << "Resize begin backbuffer " << m_width << "x" << m_height << " -> " << width << "x" << height;
        TraceRenderBuilder(message.str());
    }

    if (!TryWaitForGpu("Resize backbuffer", ResizeGpuWaitTimeoutMs))
    {
        TraceRenderBuilder("Resize deferred: GPU did not become idle before ResizeBuffers.");
        return false;
    }

    TraceRenderBuilder("Resize releasing render targets.");
    ReleaseRenderTargets();
    m_width = width;
    m_height = height;
    TraceRenderBuilder("ResizeBuffers begin.");
    ThrowIfFailed(m_swapChain->ResizeBuffers(FrameCount, width, height, m_backBufferFormat, 0), "ResizeBuffers failed.");
    TraceRenderBuilder("ResizeBuffers end.");
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    CreateRenderTargets();
    m_traceRenderFrames = 4;
    TraceRenderBuilder("Resize end.");
    return true;
}

bool D3D12Backend::ResizeSceneTarget(UINT width, UINT height)
{
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (!m_device || (width == m_sceneWidth && height == m_sceneHeight))
    {
        return true;
    }

    {
        std::ostringstream message;
        message << "ResizeSceneTarget begin " << m_sceneWidth << "x" << m_sceneHeight << " -> " << width << "x" << height;
        TraceRenderBuilder(message.str());
    }

    if (!TryWaitForGpu("Resize scene target", ResizeGpuWaitTimeoutMs))
    {
        TraceRenderBuilder("ResizeSceneTarget deferred: GPU did not become idle before scene target recreate.");
        return false;
    }

    m_sceneTarget.Reset();
    m_sceneDepth.Reset();
    m_sceneWidth = width;
    m_sceneHeight = height;
    CreateSceneTarget();
    m_traceRenderFrames = 4;
    TraceRenderBuilder("ResizeSceneTarget end.");
    return true;
}

void D3D12Backend::UpdateConstants(float deltaSeconds)
{
    m_elapsedSeconds += deltaSeconds;

    const float radius = SceneRadius();
    const XMVECTOR at = XMLoadFloat3(&m_cameraTarget);
    const float cosPitch = std::cos(m_cameraPitch);
    const XMVECTOR cameraOffset = XMVectorSet(
        std::sin(m_cameraYaw) * cosPitch,
        std::sin(m_cameraPitch),
        -std::cos(m_cameraYaw) * cosPitch,
        0.0f) * m_cameraDistance;
    const XMVECTOR eye = at + cameraOffset;
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    const float nearPlane = std::max(0.01f, std::min(radius, m_cameraDistance) * 0.001f);
    const float farPlane = std::max(100.0f, (m_cameraDistance + radius) * 4.0f);
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(55.0f), static_cast<float>(m_sceneWidth) / static_cast<float>(m_sceneHeight), nearPlane, farPlane);
    const XMMATRIX viewProjection = view * projection;
    const XMMATRIX model = m_loadedSceneMesh ? XMMatrixIdentity() : XMMatrixRotationY(m_elapsedSeconds * 0.65f) * XMMatrixRotationX(m_elapsedSeconds * 0.23f);

    SceneConstants constants = {};
    XMStoreFloat4x4(&constants.modelViewProjection, XMMatrixTranspose(model * viewProjection));
    XMStoreFloat4x4(&constants.model, XMMatrixTranspose(model));
    XMStoreFloat4x4(&constants.viewProjectionInverse, XMMatrixTranspose(XMMatrixInverse(nullptr, viewProjection)));
    XMFLOAT3 cameraPosition = {};
    XMStoreFloat3(&cameraPosition, eye);
    constants.cameraPositionTime = XMFLOAT4(cameraPosition.x, cameraPosition.y, cameraPosition.z, m_elapsedSeconds);
    constants.lightDirectionIntensity = XMFLOAT4(
        m_lookDevEnvironment.sunDirection[0],
        m_lookDevEnvironment.sunDirection[1],
        m_lookDevEnvironment.sunDirection[2],
        m_lookDevEnvironment.sunIntensity);
    XMStoreFloat4x4(&constants.shadowViewProjection, XMMatrixTranspose(ComputeShadowViewProjection()));

    std::memcpy(m_constantBufferMapped, &constants, sizeof(constants));
}

XMMATRIX D3D12Backend::ComputeShadowViewProjection() const
{
    const XMVECTOR minBounds = XMLoadFloat3(&m_boundsMin);
    const XMVECTOR maxBounds = XMLoadFloat3(&m_boundsMax);
    const XMVECTOR center = (minBounds + maxBounds) * 0.5f;
    const float radius = SceneRadius() * m_lookDevShadowSettings.fitScale;
    XMVECTOR lightDirection = XMVectorSet(
        m_lookDevEnvironment.sunDirection[0],
        m_lookDevEnvironment.sunDirection[1],
        m_lookDevEnvironment.sunDirection[2],
        0.0f);
    if (XMVectorGetX(XMVector3LengthSq(lightDirection)) < 1.0e-6f)
    {
        lightDirection = XMVectorSet(-0.35f, -0.75f, 0.55f, 0.0f);
    }
    lightDirection = XMVector3Normalize(lightDirection);

    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (std::abs(XMVectorGetX(XMVector3Dot(lightDirection, up))) > 0.95f)
    {
        up = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    }

    const XMVECTOR eye = center - lightDirection * (radius * 2.0f);
    const XMMATRIX view = XMMatrixLookAtLH(eye, center, up);
    const XMMATRIX projection = XMMatrixOrthographicLH(radius * 2.0f, radius * 2.0f, 0.01f, radius * 4.0f);
    return view * projection;
}

float D3D12Backend::SceneRadius() const
{
    const XMVECTOR minBounds = XMLoadFloat3(&m_boundsMin);
    const XMVECTOR maxBounds = XMLoadFloat3(&m_boundsMax);
    const XMVECTOR center = (minBounds + maxBounds) * 0.5f;
    const XMVECTOR extents = XMVectorMax(maxBounds - center, XMVectorReplicate(0.75f));
    return std::max(0.75f, XMVectorGetX(XMVector3Length(extents)));
}

void D3D12Backend::CameraBasis(XMVECTOR& forward, XMVECTOR& right, XMVECTOR& up) const
{
    const float cosPitch = std::cos(m_cameraPitch);
    const XMVECTOR eyeOffset = XMVectorSet(
        std::sin(m_cameraYaw) * cosPitch,
        std::sin(m_cameraPitch),
        -std::cos(m_cameraYaw) * cosPitch,
        0.0f) * m_cameraDistance;
    forward = XMVector3Normalize(XMVectorNegate(eyeOffset));
    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    up = XMVector3Normalize(XMVector3Cross(forward, right));
}

void D3D12Backend::ResetCameraToScene()
{
    const XMVECTOR minBounds = XMLoadFloat3(&m_boundsMin);
    const XMVECTOR maxBounds = XMLoadFloat3(&m_boundsMax);
    const XMVECTOR center = (minBounds + maxBounds) * 0.5f;
    XMStoreFloat3(&m_cameraTarget, center);

    const float radius = SceneRadius();
    m_cameraMoveScale = radius;
    m_cameraDistance = std::max(0.1f, radius * 2.75f);
    m_cameraYaw = 0.0f;
    m_cameraPitch = 0.12f;
}

ViewportCamera D3D12Backend::CameraState() const
{
    ViewportCamera camera;
    camera.target = { m_cameraTarget.x, m_cameraTarget.y, m_cameraTarget.z };
    camera.yaw = m_cameraYaw;
    camera.pitch = m_cameraPitch;
    camera.distance = m_cameraDistance;
    return camera;
}

void D3D12Backend::SetCameraState(const ViewportCamera& camera)
{
    m_cameraTarget = XMFLOAT3(camera.target[0], camera.target[1], camera.target[2]);
    m_cameraYaw = camera.yaw;
    m_cameraPitch = ClampFloat(camera.pitch, -1.45f, 1.45f);
    m_cameraDistance = std::max(0.01f, camera.distance);
}

void D3D12Backend::OrbitCamera(float yawDeltaRadians, float pitchDeltaRadians)
{
    m_cameraYaw += yawDeltaRadians;
    m_cameraPitch = ClampFloat(m_cameraPitch + pitchDeltaRadians, -1.45f, 1.45f);
}

void D3D12Backend::PanCamera(float rightDelta, float upDelta)
{
    XMVECTOR forward;
    XMVECTOR right;
    XMVECTOR up;
    CameraBasis(forward, right, up);
    XMVECTOR target = XMLoadFloat3(&m_cameraTarget);
    target += (-right * rightDelta + up * upDelta) * m_cameraDistance;
    XMStoreFloat3(&m_cameraTarget, target);
}

void D3D12Backend::DollyCamera(float wheelDelta)
{
    m_cameraDistance = std::max(0.01f, m_cameraDistance * std::pow(1.12f, -wheelDelta));
}

void D3D12Backend::MoveCamera(float forwardDelta, float rightDelta, float upDelta)
{
    XMVECTOR forward;
    XMVECTOR right;
    XMVECTOR up;
    CameraBasis(forward, right, up);
    XMVECTOR target = XMLoadFloat3(&m_cameraTarget);
    target += (forward * forwardDelta + right * rightDelta + up * upDelta) * m_cameraMoveScale;
    XMStoreFloat3(&m_cameraTarget, target);
}

void D3D12Backend::SetMaterialAssignments(const std::vector<MaterialAssignment>& assignments)
{
    for (const MaterialAssignment& assignment : assignments)
    {
        for (RenderMaterial& material : m_materials)
        {
            if (material.name == assignment.materialName)
            {
                material.shaderSetName = assignment.shaderSetName;
                material.constants.baseColorFactor = XMFLOAT4(
                    assignment.baseColorFactor[0],
                    assignment.baseColorFactor[1],
                    assignment.baseColorFactor[2],
                    assignment.baseColorFactor[3]);
                material.constants.emissiveFactor = XMFLOAT4(
                    assignment.emissiveFactor[0],
                    assignment.emissiveFactor[1],
                    assignment.emissiveFactor[2],
                    assignment.emissiveFactor[3]);
                material.constants.roughnessFactor = assignment.roughnessFactor;
                material.constants.metallicFactor = assignment.metallicFactor;
                material.constants.normalStrength = assignment.normalStrength;
                material.constants.normalGreenScale = assignment.flipNormalGreen ? -1.0f : 1.0f;
                material.constants.occlusionStrength = assignment.occlusionStrength;
                material.constants.alphaCutoff = assignment.alphaCutoff;
                material.constants.alphaMode = static_cast<float>(assignment.alphaMode);
                material.constants.packedOcclusionRoughnessMetallic = assignment.packedOcclusionRoughnessMetallic ? 1.0f : 0.0f;
                break;
            }
        }
    }
}

bool D3D12Backend::UpdateMaterialTextureSlot(const std::string& materialName, std::uint32_t textureSlot, const std::wstring& path, std::string& diagnostics)
{
    if (textureSlot >= MaterialTextureSlotCount)
    {
        diagnostics = "Invalid material texture slot.";
        return false;
    }

    for (std::size_t materialIndex = 0; materialIndex < m_materials.size(); ++materialIndex)
    {
        RenderMaterial& material = m_materials[materialIndex];
        if (material.name != materialName)
        {
            continue;
        }

        const UINT descriptorIndex = MaterialSrvDescriptorStart + static_cast<UINT>(materialIndex) * MaterialTextureSlotCount + textureSlot;
        const UINT textureBit = 1u << textureSlot;
        if (path.empty())
        {
            WaitForGpu();
            CreateFallbackSrv(descriptorIndex);
            if (materialIndex < m_materialTextures.size())
            {
                m_materialTextures[materialIndex][textureSlot].Reset();
            }
            material.texturePaths[textureSlot].clear();
            material.constants.textureMask &= ~textureBit;
            diagnostics = "Cleared texture slot for material '" + materialName + "'.";
            return true;
        }

        ComPtr<ID3D12Resource> newTexture;
        std::string textureDiagnostics;
        if (!CreateMaterialTexture(path, descriptorIndex, newTexture, textureDiagnostics))
        {
            diagnostics = textureDiagnostics;
            return false;
        }

        if (materialIndex >= m_materialTextures.size())
        {
            m_materialTextures.resize(materialIndex + 1);
        }
        m_materialTextures[materialIndex][textureSlot] = newTexture;
        material.texturePaths[textureSlot] = path;
        material.constants.textureMask |= textureBit;
        diagnostics = textureDiagnostics;
        return true;
    }

    diagnostics = "Material was not found: " + materialName;
    return false;
}

void D3D12Backend::SetSkyColors(const std::array<float, 4>& topColor, const std::array<float, 4>& horizonColor)
{
    m_skyConstants.topColor = XMFLOAT4(topColor[0], topColor[1], topColor[2], 1.0f);
    m_skyConstants.horizonColor = XMFLOAT4(horizonColor[0], horizonColor[1], horizonColor[2], 1.0f);
    m_lookDevConstants.skyTopColor = m_skyConstants.topColor;
    m_lookDevConstants.skyHorizonColor = m_skyConstants.horizonColor;
}

void D3D12Backend::SetLookDevEnvironment(const LookDevEnvironment& environment)
{
    m_lookDevEnvironment = environment;
    m_lookDevConstants.sunColorIntensity = XMFLOAT4(
        environment.sunColor[0],
        environment.sunColor[1],
        environment.sunColor[2],
        environment.sunIntensity);
    m_lookDevConstants.environmentOptions = XMFLOAT4(
        environment.rotationYaw,
        environment.intensity,
        static_cast<float>(environment.backgroundMode),
        m_hasEnvironmentTexture ? 1.0f : 0.0f);
    m_lookDevConstants.iblOptions.x = static_cast<float>(std::max<UINT>(m_environmentMipLevels, 1u) - 1u);
    m_lookDevConstants.iblOptions.y = 1.0f;
    m_lookDevConstants.iblOptions.z = 1.0f;
    m_lookDevConstants.iblOptions.w = 1.0f;
}

void D3D12Backend::SetLookDevViewSettings(const LookDevViewSettings& viewSettings)
{
    m_lookDevViewSettings = viewSettings;
    m_lookDevConstants.viewOptions = XMFLOAT4(
        viewSettings.exposure,
        viewSettings.gamma,
        static_cast<float>(viewSettings.toneMapper),
        static_cast<float>(viewSettings.displayMode));
}

void D3D12Backend::SetLookDevShadowSettings(const LookDevShadowSettings& shadowSettings)
{
    const UINT previousResolution = m_shadowResolution;
    m_lookDevShadowSettings = shadowSettings;
    m_lookDevShadowSettings.resolution = NormalizeShadowResolution(shadowSettings.resolution);
    m_lookDevShadowSettings.strength = ClampFloat(m_lookDevShadowSettings.strength, 0.0f, 1.0f);
    m_lookDevShadowSettings.bias = ClampFloat(m_lookDevShadowSettings.bias, 0.0f, 0.05f);
    m_lookDevShadowSettings.softness = ClampFloat(m_lookDevShadowSettings.softness, 0.0f, 8.0f);
    m_lookDevShadowSettings.fitScale = ClampFloat(m_lookDevShadowSettings.fitScale, 1.0f, 4.0f);
    m_lookDevConstants.shadowOptions = XMFLOAT4(
        m_lookDevShadowSettings.enabled ? 1.0f : 0.0f,
        m_lookDevShadowSettings.strength,
        m_lookDevShadowSettings.bias,
        m_lookDevShadowSettings.softness / static_cast<float>(m_lookDevShadowSettings.resolution));

    if (m_device && (!m_shadowMap || previousResolution != m_lookDevShadowSettings.resolution))
    {
        WaitForGpu();
        CreateShadowResources();
    }
    else
    {
        std::ostringstream status;
        status << "Sun shadow " << (m_lookDevShadowSettings.enabled ? "enabled" : "disabled")
               << ": " << m_lookDevShadowSettings.resolution << ".";
        m_shadowStatus = status.str();
    }
}

void D3D12Backend::SetDebugViewMode(LookDevDisplayMode displayMode)
{
    m_lookDevViewSettings.displayMode = displayMode;
    m_lookDevConstants.viewOptions.w = static_cast<float>(displayMode);
}

bool D3D12Backend::UpdateEnvironmentTexture(const std::wstring& path, std::string& diagnostics)
{
    if (path.empty())
    {
        WaitForGpu();
        CreateFallbackSrv(EnvironmentSrvDescriptorIndex);
        m_environmentTexture.Reset();
        m_hasEnvironmentTexture = false;
        m_environmentMipLevels = 1;
        m_lookDevEnvironment.environmentPath.clear();
        m_lookDevConstants.environmentOptions.w = 0.0f;
        m_lookDevConstants.iblOptions.x = 0.0f;
        m_environmentStatus = "Environment texture cleared. Using SkyColor/default fallback.";
        diagnostics = m_environmentStatus;
        return true;
    }

    ComPtr<ID3D12Resource> newTexture;
    std::string textureDiagnostics;
    if (!CreateTextureFromFile(path, EnvironmentSrvDescriptorIndex, newTexture, textureDiagnostics))
    {
        diagnostics = textureDiagnostics + " Keeping the last valid environment.";
        return false;
    }

    m_environmentTexture = newTexture;
    m_hasEnvironmentTexture = true;
    m_environmentMipLevels = std::max<UINT>(newTexture->GetDesc().MipLevels, 1u);
    m_lookDevEnvironment.environmentPath = path;
    m_lookDevConstants.environmentOptions.w = 1.0f;
    m_lookDevConstants.iblOptions.x = static_cast<float>(m_environmentMipLevels - 1u);
    m_environmentStatus = "Loaded environment " + std::filesystem::path(path).filename().string();
    diagnostics = m_environmentStatus + "\n" + textureDiagnostics;
    return true;
}

bool D3D12Backend::SaveSceneSnapshot(const std::wstring& path, std::string& diagnostics)
{
    if (!m_sceneTarget)
    {
        diagnostics = "Scene target is not available.";
        return false;
    }

    try
    {
        WaitForGpu();
        DirectX::ScratchImage captured;
        HRESULT hr = DirectX::CaptureTexture(
            m_commandQueue.Get(),
            m_sceneTarget.Get(),
            false,
            captured,
            m_sceneTargetState,
            m_sceneTargetState);
        if (FAILED(hr))
        {
            diagnostics = "DirectXTex failed to capture viewport: " + HResultMessage(hr);
            return false;
        }

        const DirectX::Image* capturedImage = captured.GetImage(0, 0, 0);
        if (!capturedImage)
        {
            diagnostics = "Captured viewport image was empty.";
            return false;
        }

        DirectX::ScratchImage ldrImage;
        hr = DirectX::Convert(*capturedImage, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, 0.0f, ldrImage);
        if (FAILED(hr))
        {
            diagnostics = "DirectXTex failed to convert viewport image: " + HResultMessage(hr);
            return false;
        }

        const DirectX::Image* outputImage = ldrImage.GetImage(0, 0, 0);
        if (!outputImage)
        {
            diagnostics = "Converted viewport image was empty.";
            return false;
        }

        hr = DirectX::SaveToWICFile(*outputImage, DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, path.c_str());
        if (FAILED(hr))
        {
            diagnostics = "DirectXTex failed to write snapshot: " + HResultMessage(hr);
            return false;
        }

        diagnostics = "Saved viewport snapshot to " + std::filesystem::path(path).string();
        return true;
    }
    catch (const std::exception& ex)
    {
        diagnostics = ex.what();
        return false;
    }
}

ID3D12PipelineState* D3D12Backend::PipelineForMaterial(const RenderMaterial& material) const
{
    const auto it = m_pipelineStates.find(material.shaderSetName);
    if (it != m_pipelineStates.end())
    {
        return it->second.Get();
    }
    return m_pipelineState.Get();
}

const D3D12Backend::RenderMaterial& D3D12Backend::MaterialForDraw(const SceneDraw& draw) const
{
    if (!m_materials.empty() && draw.materialIndex < m_materials.size())
    {
        return m_materials[draw.materialIndex];
    }
    return m_materials.front();
}

void D3D12Backend::DrawSky()
{
    if (!m_skyPipelineState)
    {
        return;
    }

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_skyPipelineState.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRoot32BitConstants(3, sizeof(LookDevConstants) / sizeof(std::uint32_t), &m_lookDevConstants, 0);
    m_commandList->SetGraphicsRootDescriptorTable(4, SrvGpuHandle(EnvironmentSrvDescriptorIndex));
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 0, nullptr);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->DrawInstanced(3, 1, 0, 0);
}

void D3D12Backend::RenderShadowMap()
{
    if (!m_lookDevShadowSettings.enabled || !m_shadowPipelineState || !m_shadowMap || m_draws.empty())
    {
        return;
    }

    if (m_shadowMapState != D3D12_RESOURCE_STATE_DEPTH_WRITE)
    {
        D3D12_RESOURCE_BARRIER barrier = Transition(m_shadowMap.Get(), m_shadowMapState, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        m_commandList->ResourceBarrier(1, &barrier);
        m_shadowMapState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv = DsvHandle(1);
    m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);
    m_commandList->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT shadowViewport = { 0.0f, 0.0f, static_cast<float>(m_shadowResolution), static_cast<float>(m_shadowResolution), 0.0f, 1.0f };
    D3D12_RECT shadowScissor = { 0, 0, static_cast<LONG>(m_shadowResolution), static_cast<LONG>(m_shadowResolution) };
    m_commandList->RSSetViewports(1, &shadowViewport);
    m_commandList->RSSetScissorRects(1, &shadowScissor);

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_shadowPipelineState.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);

    for (const SceneDraw& draw : m_draws)
    {
        if (draw.indexCount == 0)
        {
            continue;
        }

        const RenderMaterial& material = MaterialForDraw(draw);
        if (material.constants.alphaMode > 1.5f)
        {
            continue;
        }

        m_commandList->SetGraphicsRootDescriptorTable(1, material.textureTableGpu);
        m_commandList->SetGraphicsRoot32BitConstants(2, sizeof(MaterialConstants) / sizeof(std::uint32_t), &material.constants, 0);
        m_commandList->DrawIndexedInstanced(draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
    }

    D3D12_RESOURCE_BARRIER barrier = Transition(m_shadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_commandList->ResourceBarrier(1, &barrier);
    m_shadowMapState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
}

void D3D12Backend::Render(float deltaSeconds, const std::vector<std::uint8_t>&, const std::vector<std::uint8_t>&)
{
    if (!m_pipelineState || m_materials.empty())
    {
        return;
    }

    const bool traceRender = m_traceRenderFrames > 0;
    if (traceRender)
    {
        std::ostringstream message;
        message << "Render begin frame=" << m_frameNumber
                << " frameIndex=" << m_frameIndex
                << " backbuffer=" << m_width << "x" << m_height
                << " scene=" << m_sceneWidth << "x" << m_sceneHeight;
        TraceRenderBuilder(message.str());
    }

    const UINT64 frameFenceValue = m_fenceValues[m_frameIndex];
    if (frameFenceValue != 0 && m_fence && m_fence->GetCompletedValue() < frameFenceValue)
    {
        if (traceRender)
        {
            std::ostringstream message;
            message << "Render skipped: frame allocator is still busy"
                    << " frameIndex=" << m_frameIndex
                    << " waitFence=" << frameFenceValue
                    << " completed=" << m_fence->GetCompletedValue();
            TraceRenderBuilder(message.str());
        }
        ImGui::Render();
        return;
    }

    const auto startTime = std::chrono::high_resolution_clock::now();
    UpdateConstants(deltaSeconds);

    if (traceRender) { TraceRenderBuilder("Render reset command list."); }
    ThrowIfFailed(m_commandAllocators[m_frameIndex]->Reset(), "Command allocator reset failed.");
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr), "Command list reset failed.");

    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    RenderShadowMap();

    if (m_sceneTargetState != D3D12_RESOURCE_STATE_RENDER_TARGET)
    {
        D3D12_RESOURCE_BARRIER barrier = Transition(m_sceneTarget.Get(), m_sceneTargetState, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_commandList->ResourceBarrier(1, &barrier);
        m_sceneTargetState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = RtvHandle(FrameCount);
    D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv = DsvHandle(0);
    m_commandList->OMSetRenderTargets(1, &sceneRtv, FALSE, &sceneDsv);
    const float fallbackClear[] = { m_skyConstants.horizonColor.x, m_skyConstants.horizonColor.y, m_skyConstants.horizonColor.z, 1.0f };
    m_commandList->ClearRenderTargetView(sceneRtv, fallbackClear, 0, nullptr);
    m_commandList->ClearDepthStencilView(sceneDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    D3D12_VIEWPORT sceneViewport = { 0.0f, 0.0f, static_cast<float>(m_sceneWidth), static_cast<float>(m_sceneHeight), 0.0f, 1.0f };
    D3D12_RECT sceneScissor = { 0, 0, static_cast<LONG>(m_sceneWidth), static_cast<LONG>(m_sceneHeight) };
    m_commandList->RSSetViewports(1, &sceneViewport);
    m_commandList->RSSetScissorRects(1, &sceneScissor);
    DrawSky();
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRoot32BitConstants(3, sizeof(LookDevConstants) / sizeof(std::uint32_t), &m_lookDevConstants, 0);
    m_commandList->SetGraphicsRootDescriptorTable(4, SrvGpuHandle(EnvironmentSrvDescriptorIndex));
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->IASetIndexBuffer(&m_indexBufferView);

    ID3D12PipelineState* currentPipeline = nullptr;
    for (const SceneDraw& draw : m_draws)
    {
        if (draw.indexCount == 0)
        {
            continue;
        }

        const RenderMaterial& material = MaterialForDraw(draw);
        ID3D12PipelineState* pipeline = PipelineForMaterial(material);
        if (!pipeline)
        {
            continue;
        }
        if (pipeline != currentPipeline)
        {
            m_commandList->SetPipelineState(pipeline);
            currentPipeline = pipeline;
        }
        m_commandList->SetGraphicsRootDescriptorTable(1, material.textureTableGpu);
        m_commandList->SetGraphicsRoot32BitConstants(2, sizeof(MaterialConstants) / sizeof(std::uint32_t), &material.constants, 0);
        m_commandList->DrawIndexedInstanced(draw.indexCount, 1, draw.startIndex, draw.baseVertex, 0);
    }

    D3D12_RESOURCE_BARRIER sceneToSrv = Transition(m_sceneTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_commandList->ResourceBarrier(1, &sceneToSrv);
    m_sceneTargetState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    if (traceRender) { TraceRenderBuilder("Render scene target complete."); }
    D3D12_RESOURCE_BARRIER backBufferToRtv = Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &backBufferToRtv);

    const float backClear[] = { 0.015f, 0.017f, 0.02f, 1.0f };
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv = RtvHandle(m_frameIndex);
    m_commandList->OMSetRenderTargets(1, &backBufferRtv, FALSE, nullptr);
    m_commandList->ClearRenderTargetView(backBufferRtv, backClear, 0, nullptr);

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());

    D3D12_RESOURCE_BARRIER backBufferToPresent = Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &backBufferToPresent);
    if (traceRender) { TraceRenderBuilder("Render close command list."); }
    ThrowIfFailed(m_commandList->Close(), "Command list close failed.");

    ID3D12CommandList* commandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, commandLists);
    if (traceRender) { TraceRenderBuilder("Render present begin."); }
    ThrowIfFailed(m_swapChain->Present(1, 0), "Present failed.");
    if (traceRender) { TraceRenderBuilder("Render present end."); }
    if (traceRender) { TraceRenderBuilder("Render move next frame begin."); }
    MoveToNextFrame();
    if (traceRender) { TraceRenderBuilder("Render move next frame end."); }

    const auto endTime = std::chrono::high_resolution_clock::now();
    m_lastFrameMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    ++m_frameNumber;
    if (m_traceRenderFrames > 0)
    {
        --m_traceRenderFrames;
    }
}

void D3D12Backend::WaitForGpu()
{
    if (!TryWaitForGpu("GPU wait", GpuWaitTimeoutMs))
    {
        throw std::runtime_error("GPU wait timed out. See Bin/Logs/RenderBuilder.log for the last D3D12 operation.");
    }
}

bool D3D12Backend::TryWaitForGpu(const char* reason, DWORD timeoutMs)
{
    if (!m_commandQueue || !m_fence)
    {
        return true;
    }

    const auto startTime = std::chrono::high_resolution_clock::now();
    const UINT64 fenceValue = m_nextFenceValue++;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fenceValue), "Fence signal failed.");

    if (m_fence->GetCompletedValue() < fenceValue)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent), "SetEventOnCompletion failed.");
        const DWORD waitResult = WaitForSingleObjectEx(m_fenceEvent, timeoutMs, FALSE);
        if (waitResult == WAIT_TIMEOUT)
        {
            std::ostringstream message;
            message << "GPU wait timeout"
                    << " reason='" << (reason ? reason : "unknown") << "'"
                    << " timeoutMs=" << timeoutMs
                    << " frameIndex=" << m_frameIndex
                    << " fenceValue=" << fenceValue
                    << " completed=" << m_fence->GetCompletedValue();
            if (m_device)
            {
                const HRESULT removedReason = m_device->GetDeviceRemovedReason();
                if (FAILED(removedReason))
                {
                    message << " deviceRemoved=" << HResultMessage(removedReason);
                }
            }
            TraceRenderBuilder(message.str());
            return false;
        }
        if (waitResult != WAIT_OBJECT_0)
        {
            std::ostringstream message;
            message << "GPU wait failed"
                    << " reason='" << (reason ? reason : "unknown") << "'"
                    << " waitResult=" << waitResult
                    << " lastError=" << GetLastError();
            TraceRenderBuilder(message.str());
            throw std::runtime_error("GPU wait failed.");
        }
    }

    m_fenceValues.fill(0);
    const auto endTime = std::chrono::high_resolution_clock::now();
    const double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    if (elapsedMs > 50.0)
    {
        std::ostringstream message;
        message << "GPU wait slow"
                << " reason='" << (reason ? reason : "unknown") << "'"
                << " elapsedMs=" << elapsedMs
                << " frameIndex=" << m_frameIndex
                << " fenceValue=" << fenceValue;
        TraceRenderBuilder(message.str());
    }
    return true;
}

void D3D12Backend::MoveToNextFrame()
{
    const UINT submittedFrameIndex = m_frameIndex;
    const UINT64 fenceValue = m_nextFenceValue++;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fenceValue), "Fence signal failed.");
    m_fenceValues[submittedFrameIndex] = fenceValue;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}
}
