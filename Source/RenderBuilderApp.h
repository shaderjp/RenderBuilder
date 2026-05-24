#pragma once

#include "D3D12Backend.h"
#include "EditorTypes.h"
#include "SceneImporter.h"
#include "ShaderCompiler.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace rb
{
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
    void ApplyPendingResize();
    void RequestSceneTargetResize(UINT width, UINT height);
    void ApplyPendingSceneTargetResize();
    void DrawUi();
    void DrawDockspace();
    void DrawViewportPanel();
    void DrawShaderEditorPanel();
    void DrawMaterialInspectorPanel();
    void DrawAssetBrowserPanel();
    void DrawDiagnosticsPanel();
    void DrawStatsPanel();
    void CompileActiveShader();
    bool CompileShaderSet(ShaderSet& shaderSet, std::vector<std::uint8_t>* vertexShader, std::vector<std::uint8_t>* pixelShader, std::string& diagnostics);
    void SynchronizeActiveShaderSet();
    void SelectShaderSet(std::size_t index);
    void CreateShaderSetFromActive();
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
    void UpdateWindowTitle() const;
    const SceneMaterial* FindSceneMaterial(const std::string& materialName) const;
    std::wstring ImportedTexturePath(const std::string& materialName, std::size_t textureSlot) const;
    std::wstring EffectiveTexturePath(const MaterialAssignment& assignment, std::size_t textureSlot) const;
    void ApplyMaterialTextureSlot(const MaterialAssignment& assignment, std::size_t textureSlot);
    std::string ApplyMaterialTextureOverrides(const std::vector<MaterialAssignment>& assignments);
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

    std::filesystem::path m_rootDirectory;
    std::unique_ptr<DxcShaderCompiler> m_shaderCompiler;
    D3D12Backend m_backend;
    SceneImporter m_sceneImporter;

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

    std::chrono::high_resolution_clock::time_point m_lastTick;
};
}
