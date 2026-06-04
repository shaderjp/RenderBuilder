#pragma once

#include "AiChatService.h"
#include "D3D12Backend.h"
#include "EditorTypes.h"
#include "LocalControlService.h"
#include "SceneImporter.h"
#include "ShaderCompiler.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rb
{
enum class AssetKind
{
    Scene,
    Texture,
    Environment,
    Shader,
    Project,
    Other
};

struct AssetBrowserItem
{
    AssetKind kind = AssetKind::Other;
    std::filesystem::path path;
    std::string source;
    bool referenced = false;
    bool missing = false;
};

struct ShaderSetRuntimeStatus
{
    bool compileAttempted = false;
    bool lastCompileSucceeded = false;
    bool hasLastGoodPso = false;
    std::string lastDiagnostics;
};

struct AiChatTranscriptEntry
{
    std::string role;
    std::string text;
};

struct AiPendingControlAction
{
    std::string method;
    std::string paramsJson;
    std::string label;
};

class RenderBuilderApp
{
public:
    RenderBuilderApp() = default;
    int Run(HINSTANCE instance, int showCommand);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    void Initialize(HINSTANCE instance, int showCommand);
    void Tick();
    void RequestResize(UINT width, UINT height);
    bool ApplyPendingResize();
    void RequestSceneTargetResize(UINT width, UINT height);
    bool ApplyPendingSceneTargetResize();
    void DrawUi();
    void DrawDockspace();
    void DrawWindowMenu();
    void DrawViewportPanel();
    void DrawShaderEditorPanel();
    void DrawMaterialInspectorPanel();
    void DrawAssetBrowserPanel();
    void DrawAssetCatalogPanel();
    void DrawShaderSetManagement();
    void DrawMaterialShaderAssignmentOverview();
    void DrawAutomationPanel();
    void DrawAiChatPanel();
    void DrawDiagnosticsPanel();
    void DrawStatsPanel();
    void ProcessLocalControlRequests();
    void PollAiChatEvents();
    std::string HandleLocalControlRequest(const std::string& requestText);
    void SetLocalControlEnabled(bool enabled);
    void InitializeAiChatDefaults();
    void SubmitAiChatPrompt();
    void ProcessAiAssistantResponse(const std::string& assistantText);
    void ApplyPendingAiActions();
    std::string BuildAiSystemPrompt() const;
    std::string BuildAiUserPrompt(const std::string& prompt) const;
    std::string BuildAiMaterialSummaryJson() const;
    void CompileActiveShader();
    void CompileAllShaderSets();
    bool CompileShaderSet(ShaderSet& shaderSet, std::vector<std::uint8_t>* vertexShader, std::vector<std::uint8_t>* pixelShader, std::string& diagnostics);
    void SynchronizeActiveShaderSet();
    void SelectShaderSet(std::size_t index);
    void CreateShaderSetFromActive();
    void DuplicateActiveShaderSet();
    void DeleteActiveShaderSet();
    void RenameActiveShaderSet(const std::string& newName);
    bool ShaderSetNameExists(const std::string& name, std::size_t excludeIndex = static_cast<std::size_t>(-1)) const;
    std::string UniqueShaderSetName(const std::string& baseName) const;
    std::size_t ShaderSetUsageCount(const std::string& name) const;
    const ShaderSetRuntimeStatus* ShaderStatusFor(const std::string& name) const;
    void HandleViewportCameraControls();
    void LoadDefaultShader();
    void LoadShaderFromDisk(const std::filesystem::path& path);
    void SaveProject();
    void SaveProjectAs();
    bool SaveProjectToDisk(const std::filesystem::path& path);
    void SaveViewportSnapshot();
    void LoadProject();
    void LoadProjectFromDisk(const std::filesystem::path& path);
    void LoadRecentProjects();
    void SaveRecentProjects() const;
    void AddRecentProject(const std::filesystem::path& path);
    bool LoadScenePath(const std::wstring& path, bool markDirty = true);
    void UseDefaultScenePreview();
    void MarkProjectDirty();
    void SetProjectDirty(bool dirty);
    void ApplyLookDevSettings();
    void EnsureLookDevPresets();
    LookDevPreset CaptureCurrentLookDevPreset(const std::string& name) const;
    void UpsertLookDevPreset(const LookDevPreset& preset);
    void ApplyLookDevPreset(std::size_t index);
    void MarkLookDevCustom();
    void UpdateWindowTitle() const;
    void RefreshAssetCatalog();
    void AddAssetCatalogItem(AssetKind kind, const std::filesystem::path& path, const std::string& source, bool referenced);
    void AddReferencedAsset(AssetKind kind, const std::filesystem::path& path, const std::string& source);
    AssetKind ClassifyAssetPath(const std::filesystem::path& path) const;
    bool AssetMatchesFilter(const AssetBrowserItem& item) const;
    void LoadSelectedAsset();
    void UseSelectedAssetAsEnvironment();
    void AssignSelectedTextureToMaterialSlot();
    const SceneMaterial* FindSceneMaterial(const std::string& materialName) const;
    std::wstring ImportedTexturePath(const std::string& materialName, std::size_t textureSlot) const;
    std::wstring EffectiveTexturePath(const MaterialAssignment& assignment, std::size_t textureSlot) const;
    void ApplyMaterialTextureSlot(const MaterialAssignment& assignment, std::size_t textureSlot);
    std::string ApplyMaterialTextureOverrides(const std::vector<MaterialAssignment>& assignments);
    std::string BuildControlStateJson() const;
    std::string BuildControlMaterialsJson() const;
    std::string BuildControlDiagnosticsJson() const;
    std::filesystem::path FindRootDirectory() const;
    std::filesystem::path OpenFileDialog(const wchar_t* filter) const;
    std::filesystem::path SaveFileDialog(const wchar_t* filter, const wchar_t* defaultExtension) const;

    HWND m_hwnd = nullptr;
    UINT m_windowWidth = 1600;
    UINT m_windowHeight = 960;
    bool m_running = true;
    bool m_comInitialized = false;
    bool m_minimized = false;
    bool m_inSizeMove = false;
    bool m_pendingResize = false;
    UINT m_pendingResizeWidth = 0;
    UINT m_pendingResizeHeight = 0;
    bool m_pendingSceneTargetResize = false;
    UINT m_pendingSceneTargetWidth = 0;
    UINT m_pendingSceneTargetHeight = 0;
    std::uint32_t m_resizeDeferFrames = 0;
    std::uint32_t m_sceneTargetResizeDeferFrames = 0;
    bool m_showViewportPanel = true;
    bool m_showShaderEditorPanel = true;
    bool m_showMaterialInspectorPanel = true;
    bool m_showAssetBrowserPanel = true;
    bool m_showAutomationPanel = true;
    bool m_showAiChatPanel = true;
    bool m_showDiagnosticsPanel = true;
    bool m_showStatsPanel = true;

    std::filesystem::path m_rootDirectory;
    std::unique_ptr<DxcShaderCompiler> m_shaderCompiler;
    D3D12Backend m_backend;
    SceneImporter m_sceneImporter;
    LocalControlService m_localControlService;
    AiChatService m_aiChatService;

    ProjectFile m_project;
    std::vector<SceneMaterial> m_sceneMaterials;
    ShaderSet m_activeShaderSet;
    std::vector<std::uint8_t> m_activeVertexShader;
    std::vector<std::uint8_t> m_activePixelShader;
    std::vector<char> m_shaderTextBuffer;
    std::string m_compileDiagnostics;
    std::string m_sceneDiagnostics = "Using built-in preview cube.";
    std::size_t m_sceneVertexCount = 24;
    std::size_t m_sceneIndexCount = 36;
    std::size_t m_sceneDrawCount = 1;
    std::size_t m_activeShaderSetIndex = 0;
    std::uint32_t m_shaderSetSerial = 2;
    float m_deltaSeconds = 0.0f;
    bool m_lastCompileSucceeded = false;
    bool m_shaderDirty = false;
    bool m_projectDirty = false;
    std::vector<std::filesystem::path> m_recentProjects;
    char m_lookDevPresetNameBuffer[64] = "Custom Preset";
    std::vector<AssetBrowserItem> m_assetCatalog;
    std::filesystem::path m_selectedAssetPath;
    int m_assetKindFilter = 0;
    std::size_t m_assetMaterialIndex = 0;
    std::size_t m_assetTextureSlot = 0;
    char m_assetSearchBuffer[128] = {};
    bool m_assetCatalogDirty = true;
    std::unordered_map<std::string, ShaderSetRuntimeStatus> m_shaderSetStatus;
    bool m_localControlEnabled = false;
    std::uint64_t m_controlStateVersion = 1;
    std::string m_controlLastCommand = "<none>";
    std::string m_controlLastError;
    char m_aiPromptBuffer[4096] = {};
    char m_aiModelPathBuffer[1024] = {};
    char m_aiServerPathBuffer[1024] = {};
    int m_aiServerPort = 18080;
    int m_aiContextTokens = 4096;
    int m_aiMaxTokens = 512;
    int m_aiGpuLayerMode = 0;
    int m_aiGpuLayers = 99;
    int m_aiThreads = 0;
    float m_aiTemperature = 1.0f;
    float m_aiTopP = 0.95f;
    int m_aiTopK = 64;
    bool m_aiUseJinja = true;
    bool m_aiAutoApply = false;
    std::uint64_t m_aiRequestSerial = 1;
    std::string m_aiStatus = "AI model is not loaded.";
    std::vector<AiChatTranscriptEntry> m_aiTranscript;
    std::vector<AiPendingControlAction> m_aiPendingActions;

    std::chrono::high_resolution_clock::time_point m_lastTick;
};
}
