#include "RenderBuilderApp.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <CommCtrl.h>
#include <commdlg.h>
#include <Objbase.h>

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

namespace
{
constexpr std::size_t ShaderBufferSize = 256 * 1024;
constexpr UINT_PTR ResizeMoveTimerId = 1;
constexpr std::size_t MaterialTextureSlotCount = static_cast<std::size_t>(rb::TextureSlot::Count);
constexpr const char* TextureSlotLabels[MaterialTextureSlotCount] =
{
    "Base Color",
    "Normal",
    "Roughness",
    "Metallic",
    "Occlusion",
    "Emissive",
};
constexpr const char* TextureSlotJsonNames[MaterialTextureSlotCount] =
{
    "baseColor",
    "normal",
    "roughness",
    "metallic",
    "occlusion",
    "emissive",
};
constexpr const wchar_t* TextureFileFilter = L"Texture Files\0*.dds;*.tga;*.hdr;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0All Files\0*.*\0";
constexpr const wchar_t* EnvironmentFileFilter = L"Environment Files\0*.hdr;*.dds;*.exr;*.png;*.jpg;*.jpeg\0All Files\0*.*\0";
constexpr const char* LookDevShaderSetName = "LookDev PBR";
constexpr const char* DefaultRasterShaderSetName = "Default Raster Shader";
constexpr const char* CustomLookDevPresetName = "Custom";

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::string ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open text file: " + path.string());
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to write text file: " + path.string());
    }
    file << text;
}

std::string EscapeJson(const std::string& text)
{
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (const char ch : text)
    {
        switch (ch)
        {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

std::string TextureFileName(const std::wstring& path)
{
    if (path.empty())
    {
        return "<none>";
    }
    return WideToUtf8(std::filesystem::path(path).filename().wstring());
}

std::wstring SceneTexturePath(const rb::SceneMaterial& material, std::size_t textureSlot)
{
    switch (textureSlot)
    {
    case 0: return material.baseColorTexturePath;
    case 1: return material.normalTexturePath;
    case 2: return material.roughnessTexturePath;
    case 3: return material.metallicTexturePath;
    case 4: return material.occlusionTexturePath;
    case 5: return material.emissiveTexturePath;
    default: return {};
    }
}

const char* AlphaModeName(rb::AlphaMode mode)
{
    switch (mode)
    {
    case rb::AlphaMode::Opaque: return "Opaque";
    case rb::AlphaMode::Mask: return "Mask";
    case rb::AlphaMode::Blend: return "Blend";
    default: return "Opaque";
    }
}

const char* BackgroundModeName(rb::LookDevBackgroundMode mode)
{
    switch (mode)
    {
    case rb::LookDevBackgroundMode::SkyColor: return "SkyColor";
    case rb::LookDevBackgroundMode::Hdri: return "HDRI Background";
    case rb::LookDevBackgroundMode::TransparentChecker: return "Transparent Checker";
    default: return "SkyColor";
    }
}

const char* ToneMapperName(rb::ToneMapper toneMapper)
{
    switch (toneMapper)
    {
    case rb::ToneMapper::None: return "None";
    case rb::ToneMapper::Reinhard: return "Reinhard";
    case rb::ToneMapper::Aces: return "ACES";
    default: return "ACES";
    }
}

const char* DisplayModeName(rb::LookDevDisplayMode mode)
{
    switch (mode)
    {
    case rb::LookDevDisplayMode::Beauty: return "Beauty";
    case rb::LookDevDisplayMode::BaseColor: return "BaseColor";
    case rb::LookDevDisplayMode::Normal: return "Normal";
    case rb::LookDevDisplayMode::Roughness: return "Roughness";
    case rb::LookDevDisplayMode::Metallic: return "Metallic";
    case rb::LookDevDisplayMode::AmbientOcclusion: return "AO";
    case rb::LookDevDisplayMode::Emissive: return "Emissive";
    case rb::LookDevDisplayMode::LightingOnly: return "LightingOnly";
    default: return "Beauty";
    }
}

std::string AlphaModeJsonName(rb::AlphaMode mode)
{
    switch (mode)
    {
    case rb::AlphaMode::Mask: return "Mask";
    case rb::AlphaMode::Blend: return "Blend";
    case rb::AlphaMode::Opaque:
    default:
        return "Opaque";
    }
}

std::string BackgroundModeJsonName(rb::LookDevBackgroundMode mode)
{
    switch (mode)
    {
    case rb::LookDevBackgroundMode::Hdri: return "Hdri";
    case rb::LookDevBackgroundMode::TransparentChecker: return "TransparentChecker";
    case rb::LookDevBackgroundMode::SkyColor:
    default:
        return "SkyColor";
    }
}

std::string ToneMapperJsonName(rb::ToneMapper toneMapper)
{
    switch (toneMapper)
    {
    case rb::ToneMapper::None: return "None";
    case rb::ToneMapper::Reinhard: return "Reinhard";
    case rb::ToneMapper::Aces:
    default:
        return "Aces";
    }
}

std::string DisplayModeJsonName(rb::LookDevDisplayMode mode)
{
    switch (mode)
    {
    case rb::LookDevDisplayMode::BaseColor: return "BaseColor";
    case rb::LookDevDisplayMode::Normal: return "Normal";
    case rb::LookDevDisplayMode::Roughness: return "Roughness";
    case rb::LookDevDisplayMode::Metallic: return "Metallic";
    case rb::LookDevDisplayMode::AmbientOcclusion: return "AmbientOcclusion";
    case rb::LookDevDisplayMode::Emissive: return "Emissive";
    case rb::LookDevDisplayMode::LightingOnly: return "LightingOnly";
    case rb::LookDevDisplayMode::Beauty:
    default:
        return "Beauty";
    }
}

rb::AlphaMode AlphaModeFromJson(const std::string& text, rb::AlphaMode fallback)
{
    if (text == "Mask") { return rb::AlphaMode::Mask; }
    if (text == "Blend") { return rb::AlphaMode::Blend; }
    if (text == "Opaque") { return rb::AlphaMode::Opaque; }
    return fallback;
}

rb::LookDevBackgroundMode BackgroundModeFromJson(const std::string& text, rb::LookDevBackgroundMode fallback)
{
    if (text == "Hdri" || text == "HDRI") { return rb::LookDevBackgroundMode::Hdri; }
    if (text == "TransparentChecker") { return rb::LookDevBackgroundMode::TransparentChecker; }
    if (text == "SkyColor") { return rb::LookDevBackgroundMode::SkyColor; }
    return fallback;
}

rb::ToneMapper ToneMapperFromJson(const std::string& text, rb::ToneMapper fallback)
{
    if (text == "None") { return rb::ToneMapper::None; }
    if (text == "Reinhard") { return rb::ToneMapper::Reinhard; }
    if (text == "Aces" || text == "ACES") { return rb::ToneMapper::Aces; }
    return fallback;
}

rb::LookDevDisplayMode DisplayModeFromJson(const std::string& text, rb::LookDevDisplayMode fallback)
{
    if (text == "BaseColor") { return rb::LookDevDisplayMode::BaseColor; }
    if (text == "Normal") { return rb::LookDevDisplayMode::Normal; }
    if (text == "Roughness") { return rb::LookDevDisplayMode::Roughness; }
    if (text == "Metallic") { return rb::LookDevDisplayMode::Metallic; }
    if (text == "AmbientOcclusion" || text == "AO") { return rb::LookDevDisplayMode::AmbientOcclusion; }
    if (text == "Emissive") { return rb::LookDevDisplayMode::Emissive; }
    if (text == "LightingOnly") { return rb::LookDevDisplayMode::LightingOnly; }
    if (text == "Beauty") { return rb::LookDevDisplayMode::Beauty; }
    return fallback;
}

rb::LookDevPreset MakeLookDevPreset(
    const char* name,
    const std::array<float, 4>& skyTopColor,
    const std::array<float, 4>& skyHorizonColor,
    rb::LookDevBackgroundMode backgroundMode,
    const std::array<float, 3>& sunDirection,
    const std::array<float, 3>& sunColor,
    float sunIntensity,
    float environmentIntensity,
    float exposure,
    rb::ToneMapper toneMapper,
    rb::LookDevDisplayMode displayMode,
    bool preserveEnvironmentPath)
{
    rb::LookDevPreset preset;
    preset.name = name;
    preset.skyTopColor = skyTopColor;
    preset.skyHorizonColor = skyHorizonColor;
    preset.environment.backgroundMode = backgroundMode;
    preset.environment.sunDirection = sunDirection;
    preset.environment.sunColor = sunColor;
    preset.environment.sunIntensity = sunIntensity;
    preset.environment.intensity = environmentIntensity;
    preset.viewSettings.exposure = exposure;
    preset.viewSettings.toneMapper = toneMapper;
    preset.viewSettings.displayMode = displayMode;
    preset.preserveEnvironmentPath = preserveEnvironmentPath;
    return preset;
}

std::vector<rb::LookDevPreset> BuiltInLookDevPresets()
{
    return
    {
        MakeLookDevPreset(
            "Default Studio",
            { 0.12f, 0.22f, 0.36f, 1.0f },
            { 0.035f, 0.045f, 0.055f, 1.0f },
            rb::LookDevBackgroundMode::SkyColor,
            { -0.35f, -0.75f, 0.55f },
            { 1.0f, 0.96f, 0.88f },
            1.2f,
            1.0f,
            0.0f,
            rb::ToneMapper::Aces,
            rb::LookDevDisplayMode::Beauty,
            false),
        MakeLookDevPreset(
            "Neutral Gray",
            { 0.42f, 0.42f, 0.42f, 1.0f },
            { 0.18f, 0.18f, 0.18f, 1.0f },
            rb::LookDevBackgroundMode::SkyColor,
            { -0.20f, -0.80f, 0.48f },
            { 1.0f, 1.0f, 1.0f },
            0.85f,
            0.0f,
            0.0f,
            rb::ToneMapper::Aces,
            rb::LookDevDisplayMode::Beauty,
            false),
        MakeLookDevPreset(
            "Outdoor HDRI",
            { 0.36f, 0.54f, 0.76f, 1.0f },
            { 0.72f, 0.80f, 0.88f, 1.0f },
            rb::LookDevBackgroundMode::Hdri,
            { -0.52f, -0.62f, 0.32f },
            { 1.0f, 0.93f, 0.82f },
            2.5f,
            1.35f,
            -0.25f,
            rb::ToneMapper::Aces,
            rb::LookDevDisplayMode::Beauty,
            true),
    };
}

bool PathExists(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return false;
    }

    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::filesystem::path AbsoluteLexicalPath(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return {};
    }

    std::error_code ec;
    std::filesystem::path absolutePath = path.is_absolute() ? path : std::filesystem::absolute(path, ec);
    if (ec)
    {
        absolutePath = path;
    }
    return absolutePath.lexically_normal();
}

std::filesystem::path ResolveProjectPath(const std::string& jsonPath, const std::filesystem::path& projectDirectory)
{
    if (jsonPath.empty())
    {
        return {};
    }

    std::filesystem::path path(Utf8ToWide(jsonPath));
    if (path.is_relative() && !projectDirectory.empty())
    {
        path = projectDirectory / path;
    }
    return path.lexically_normal();
}

std::wstring PathForProjectJson(const std::wstring& storedPath, const std::filesystem::path& projectDirectory)
{
    if (storedPath.empty())
    {
        return {};
    }

    std::filesystem::path path(storedPath);
    if (!path.is_absolute() && !projectDirectory.empty())
    {
        path = projectDirectory / path;
    }
    path = path.lexically_normal();

    if (!projectDirectory.empty())
    {
        const std::filesystem::path base = projectDirectory.lexically_normal();
        const std::filesystem::path relativePath = path.lexically_relative(base);
        if (!relativePath.empty())
        {
            return relativePath.generic_wstring();
        }
    }
    return path.generic_wstring();
}

std::string JsonPathString(const std::wstring& storedPath, const std::filesystem::path& projectDirectory)
{
    return WideToUtf8(PathForProjectJson(storedPath, projectDirectory));
}

void AppendMissingAssetDiagnostic(std::ostringstream& diagnostics, const std::string& label, const std::filesystem::path& path)
{
    if (path.empty() || PathExists(path))
    {
        return;
    }

    diagnostics << "\nMissing " << label << ": " << path.string();
}

struct JsonValue
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::unordered_map<std::string, JsonValue> object;
};

class JsonParser
{
public:
    explicit JsonParser(const std::string& text) : m_text(text) {}

    JsonValue Parse()
    {
        JsonValue value = ParseValue();
        SkipWhitespace();
        if (m_position != m_text.size())
        {
            throw std::runtime_error("Unexpected trailing characters in project JSON.");
        }
        return value;
    }

private:
    JsonValue ParseValue()
    {
        SkipWhitespace();
        if (m_position >= m_text.size())
        {
            throw std::runtime_error("Unexpected end of project JSON.");
        }

        switch (m_text[m_position])
        {
        case '{': return ParseObject();
        case '[': return ParseArray();
        case '"':
        {
            JsonValue value;
            value.type = JsonValue::Type::String;
            value.string = ParseString();
            return value;
        }
        case 't': return ParseLiteral("true", JsonValue::Type::Bool, true);
        case 'f': return ParseLiteral("false", JsonValue::Type::Bool, false);
        case 'n': return ParseLiteral("null", JsonValue::Type::Null, false);
        default:
            if (m_text[m_position] == '-' || std::isdigit(static_cast<unsigned char>(m_text[m_position])))
            {
                return ParseNumber();
            }
            throw std::runtime_error("Invalid token in project JSON.");
        }
    }

    JsonValue ParseObject()
    {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        Expect('{');
        SkipWhitespace();
        if (TryConsume('}'))
        {
            return value;
        }

        while (true)
        {
            SkipWhitespace();
            const std::string key = ParseString();
            SkipWhitespace();
            Expect(':');
            value.object[key] = ParseValue();
            SkipWhitespace();
            if (TryConsume('}'))
            {
                break;
            }
            Expect(',');
        }
        return value;
    }

    JsonValue ParseArray()
    {
        JsonValue value;
        value.type = JsonValue::Type::Array;
        Expect('[');
        SkipWhitespace();
        if (TryConsume(']'))
        {
            return value;
        }

        while (true)
        {
            value.array.push_back(ParseValue());
            SkipWhitespace();
            if (TryConsume(']'))
            {
                break;
            }
            Expect(',');
        }
        return value;
    }

    JsonValue ParseLiteral(const char* literal, JsonValue::Type type, bool boolean)
    {
        const std::size_t length = std::strlen(literal);
        if (m_text.compare(m_position, length, literal) != 0)
        {
            throw std::runtime_error("Invalid literal in project JSON.");
        }
        m_position += length;

        JsonValue value;
        value.type = type;
        value.boolean = boolean;
        return value;
    }

    JsonValue ParseNumber()
    {
        const std::size_t begin = m_position;
        if (m_text[m_position] == '-')
        {
            ++m_position;
        }
        ConsumeDigits();
        if (m_position < m_text.size() && m_text[m_position] == '.')
        {
            ++m_position;
            ConsumeDigits();
        }
        if (m_position < m_text.size() && (m_text[m_position] == 'e' || m_text[m_position] == 'E'))
        {
            ++m_position;
            if (m_position < m_text.size() && (m_text[m_position] == '+' || m_text[m_position] == '-'))
            {
                ++m_position;
            }
            ConsumeDigits();
        }

        JsonValue value;
        value.type = JsonValue::Type::Number;
        value.number = std::stod(m_text.substr(begin, m_position - begin));
        return value;
    }

    std::string ParseString()
    {
        Expect('"');
        std::string result;
        while (m_position < m_text.size())
        {
            const char ch = m_text[m_position++];
            if (ch == '"')
            {
                return result;
            }
            if (ch != '\\')
            {
                result.push_back(ch);
                continue;
            }

            if (m_position >= m_text.size())
            {
                throw std::runtime_error("Unterminated escape in project JSON string.");
            }
            const char escaped = m_text[m_position++];
            switch (escaped)
            {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u':
                for (int i = 0; i < 4; ++i)
                {
                    if (m_position >= m_text.size() || !std::isxdigit(static_cast<unsigned char>(m_text[m_position])))
                    {
                        throw std::runtime_error("Invalid unicode escape in project JSON string.");
                    }
                    ++m_position;
                }
                result.push_back('?');
                break;
            default:
                throw std::runtime_error("Invalid escape in project JSON string.");
            }
        }
        throw std::runtime_error("Unterminated string in project JSON.");
    }

    void ConsumeDigits()
    {
        bool consumed = false;
        while (m_position < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_position])))
        {
            consumed = true;
            ++m_position;
        }
        if (!consumed)
        {
            throw std::runtime_error("Invalid number in project JSON.");
        }
    }

    void SkipWhitespace()
    {
        while (m_position < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_position])))
        {
            ++m_position;
        }
    }

    void Expect(char expected)
    {
        if (m_position >= m_text.size() || m_text[m_position] != expected)
        {
            throw std::runtime_error("Unexpected character in project JSON.");
        }
        ++m_position;
    }

    bool TryConsume(char expected)
    {
        if (m_position < m_text.size() && m_text[m_position] == expected)
        {
            ++m_position;
            return true;
        }
        return false;
    }

    const std::string& m_text;
    std::size_t m_position = 0;
};

const JsonValue* FindMember(const JsonValue& value, const char* name)
{
    if (value.type != JsonValue::Type::Object)
    {
        return nullptr;
    }
    const auto it = value.object.find(name);
    return it != value.object.end() ? &it->second : nullptr;
}

std::string JsonStringOr(const JsonValue& value, const char* name, const std::string& fallback = {})
{
    const JsonValue* member = FindMember(value, name);
    if (!member || member->type != JsonValue::Type::String)
    {
        return fallback;
    }
    return member->string;
}

double JsonNumberOr(const JsonValue& value, const char* name, double fallback)
{
    const JsonValue* member = FindMember(value, name);
    if (!member || member->type != JsonValue::Type::Number)
    {
        return fallback;
    }
    return member->number;
}

bool JsonBoolOr(const JsonValue& value, const char* name, bool fallback)
{
    const JsonValue* member = FindMember(value, name);
    if (!member || member->type != JsonValue::Type::Bool)
    {
        return fallback;
    }
    return member->boolean;
}

std::array<float, 4> JsonFloat4Or(const JsonValue& value, const char* name, const std::array<float, 4>& fallback)
{
    const JsonValue* member = FindMember(value, name);
    if (!member || member->type != JsonValue::Type::Array || member->array.size() < 4)
    {
        return fallback;
    }

    std::array<float, 4> result = fallback;
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        if (member->array[i].type != JsonValue::Type::Number)
        {
            return fallback;
        }
        result[i] = static_cast<float>(member->array[i].number);
    }
    return result;
}

std::array<float, 3> JsonFloat3Or(const JsonValue& value, const char* name, const std::array<float, 3>& fallback)
{
    const JsonValue* member = FindMember(value, name);
    if (!member || member->type != JsonValue::Type::Array || member->array.size() < 3)
    {
        return fallback;
    }

    std::array<float, 3> result = fallback;
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        if (member->array[i].type != JsonValue::Type::Number)
        {
            return fallback;
        }
        result[i] = static_cast<float>(member->array[i].number);
    }
    return result;
}

rb::LookDevEnvironment JsonLookDevEnvironmentOr(
    const JsonValue& value,
    const std::filesystem::path& projectDirectory,
    const rb::LookDevEnvironment& fallback)
{
    rb::LookDevEnvironment environment = fallback;
    const std::filesystem::path environmentPath = ResolveProjectPath(JsonStringOr(value, "environmentPath"), projectDirectory);
    environment.environmentPath = environmentPath.wstring();
    environment.rotationYaw = static_cast<float>(JsonNumberOr(value, "rotationYaw", environment.rotationYaw));
    environment.intensity = static_cast<float>(JsonNumberOr(value, "intensity", environment.intensity));
    environment.backgroundMode = BackgroundModeFromJson(JsonStringOr(value, "backgroundMode"), environment.backgroundMode);
    environment.sunDirection = JsonFloat3Or(value, "sunDirection", environment.sunDirection);
    environment.sunColor = JsonFloat3Or(value, "sunColor", environment.sunColor);
    environment.sunIntensity = static_cast<float>(JsonNumberOr(value, "sunIntensity", environment.sunIntensity));
    return environment;
}

rb::LookDevViewSettings JsonLookDevViewSettingsOr(const JsonValue& value, const rb::LookDevViewSettings& fallback)
{
    rb::LookDevViewSettings viewSettings = fallback;
    viewSettings.exposure = static_cast<float>(JsonNumberOr(value, "exposure", viewSettings.exposure));
    viewSettings.toneMapper = ToneMapperFromJson(JsonStringOr(value, "toneMapper"), viewSettings.toneMapper);
    viewSettings.gamma = static_cast<float>(JsonNumberOr(value, "gamma", viewSettings.gamma));
    viewSettings.displayMode = DisplayModeFromJson(JsonStringOr(value, "displayMode"), viewSettings.displayMode);
    viewSettings.turntableEnabled = JsonBoolOr(value, "turntableEnabled", viewSettings.turntableEnabled);
    viewSettings.turntableSpeed = static_cast<float>(JsonNumberOr(value, "turntableSpeed", viewSettings.turntableSpeed));
    return viewSettings;
}
}

namespace rb
{
int RenderBuilderApp::Run(HINSTANCE instance, int showCommand)
{
    try
    {
        Initialize(instance, showCommand);
        MSG message = {};
        while (m_running)
        {
            while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    m_running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessage(&message);
            }

            if (m_running)
            {
                Tick();
            }
        }
        m_backend.Shutdown();
        if (m_comInitialized)
        {
            CoUninitialize();
            m_comInitialized = false;
        }
        return 0;
    }
    catch (const std::exception& ex)
    {
        m_backend.Shutdown();
        if (m_comInitialized)
        {
            CoUninitialize();
            m_comInitialized = false;
        }
        MessageBoxA(nullptr, ex.what(), "RenderBuilder fatal error", MB_ICONERROR | MB_OK);
        return 1;
    }
}

void RenderBuilderApp::Initialize(HINSTANCE instance, int showCommand)
{
    const HRESULT coInitializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(coInitializeResult))
    {
        m_comInitialized = true;
    }
    else if (coInitializeResult != RPC_E_CHANGED_MODE)
    {
        throw std::runtime_error("CoInitializeEx failed.");
    }

    m_rootDirectory = FindRootDirectory();
    LoadRecentProjects();
    m_shaderTextBuffer.resize(ShaderBufferSize);

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = RenderBuilderApp::WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"RenderBuilderWindowClass";
    RegisterClassExW(&windowClass);

    RECT windowRect = { 0, 0, static_cast<LONG>(m_windowWidth), static_cast<LONG>(m_windowHeight) };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);
    m_hwnd = CreateWindowExW(
        0,
        windowClass.lpszClassName,
        L"RenderBuilder Shader Editor",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        instance,
        this);
    if (!m_hwnd)
    {
        throw std::runtime_error("CreateWindowEx failed.");
    }

    ShowWindow(m_hwnd, showCommand);
    UpdateWindow(m_hwnd);

    m_backend.Initialize(m_hwnd, m_windowWidth, m_windowHeight);
    m_shaderCompiler = std::make_unique<DxcShaderCompiler>();
    LoadDefaultShader();
    CompileActiveShader();
    SetProjectDirty(false);
    m_lastTick = std::chrono::high_resolution_clock::now();
}

std::filesystem::path RenderBuilderApp::FindRootDirectory() const
{
    std::filesystem::path current = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i)
    {
        if (std::filesystem::exists(current / "Shaders" / "DefaultRaster.hlsl"))
        {
            return current;
        }
        if (!current.has_parent_path())
        {
            break;
        }
        current = current.parent_path();
    }

    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    current = std::filesystem::path(modulePath).parent_path();
    for (int i = 0; i < 8; ++i)
    {
        if (std::filesystem::exists(current / "Shaders" / "DefaultRaster.hlsl"))
        {
            return current;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path();
}

void RenderBuilderApp::LoadDefaultShader()
{
    const std::filesystem::path shaderPath = m_rootDirectory / "Shaders" / "LookDevPBR.hlsl";
    LoadShaderFromDisk(shaderPath);
    m_activeShaderSet.name = LookDevShaderSetName;
    m_activeShaderSet.vertexEntry = L"VSMain";
    m_activeShaderSet.pixelEntry = L"PSMain";
    m_activeShaderSet.vertexProfile = L"vs_6_9";
    m_activeShaderSet.pixelProfile = L"ps_6_9";

    ShaderSet defaultRasterSet;
    defaultRasterSet.name = DefaultRasterShaderSetName;
    defaultRasterSet.sourcePath = (m_rootDirectory / "Shaders" / "DefaultRaster.hlsl").wstring();
    defaultRasterSet.sourceText = ReadTextFile(defaultRasterSet.sourcePath);
    defaultRasterSet.vertexEntry = L"VSMain";
    defaultRasterSet.pixelEntry = L"PSMain";
    defaultRasterSet.vertexProfile = L"vs_6_9";
    defaultRasterSet.pixelProfile = L"ps_6_9";

    m_project.shaderSets = { m_activeShaderSet, defaultRasterSet };
    m_project.materialAssignments = { { "Default Material", m_activeShaderSet.name } };
    m_project.lookDevPresets = BuiltInLookDevPresets();
    m_project.activeLookDevPresetName = "Default Studio";
    m_activeShaderSetIndex = 0;
    m_shaderSetSerial = 3;
    m_backend.SetSkyColors(m_project.skyTopColor, m_project.skyHorizonColor);
    m_backend.SetLookDevEnvironment(m_project.lookDevEnvironment);
    m_backend.SetLookDevViewSettings(m_project.lookDevViewSettings);
}

void RenderBuilderApp::UpdateWindowTitle() const
{
    if (!m_hwnd)
    {
        return;
    }

    std::wstring title = L"RenderBuilder Shader Editor";
    if (!m_project.path.empty())
    {
        title += L" - ";
        if (m_projectDirty)
        {
            title += L"*";
        }
        title += std::filesystem::path(m_project.path).filename().wstring();
    }
    else if (m_projectDirty)
    {
        title += L" - *Untitled";
    }
    SetWindowTextW(m_hwnd, title.c_str());
}

void RenderBuilderApp::SetProjectDirty(bool dirty)
{
    if (m_projectDirty == dirty)
    {
        UpdateWindowTitle();
        return;
    }
    m_projectDirty = dirty;
    UpdateWindowTitle();
}

void RenderBuilderApp::MarkProjectDirty()
{
    SetProjectDirty(true);
}

void RenderBuilderApp::ApplyLookDevSettings()
{
    m_project.skyTopColor[3] = 1.0f;
    m_project.skyHorizonColor[3] = 1.0f;
    m_backend.SetSkyColors(m_project.skyTopColor, m_project.skyHorizonColor);
    m_backend.SetLookDevEnvironment(m_project.lookDevEnvironment);
    m_backend.SetLookDevViewSettings(m_project.lookDevViewSettings);
}

void RenderBuilderApp::EnsureLookDevPresets()
{
    const std::vector<LookDevPreset> builtInPresets = BuiltInLookDevPresets();
    for (const LookDevPreset& builtInPreset : builtInPresets)
    {
        const auto existing = std::find_if(
            m_project.lookDevPresets.begin(),
            m_project.lookDevPresets.end(),
            [&](const LookDevPreset& preset) { return preset.name == builtInPreset.name; });
        if (existing == m_project.lookDevPresets.end())
        {
            m_project.lookDevPresets.push_back(builtInPreset);
        }
    }

    if (m_project.activeLookDevPresetName.empty())
    {
        m_project.activeLookDevPresetName = m_project.lookDevPresets.empty() ? CustomLookDevPresetName : m_project.lookDevPresets.front().name;
    }
}

LookDevPreset RenderBuilderApp::CaptureCurrentLookDevPreset(const std::string& name) const
{
    LookDevPreset preset;
    preset.name = name.empty() ? CustomLookDevPresetName : name;
    preset.skyTopColor = m_project.skyTopColor;
    preset.skyHorizonColor = m_project.skyHorizonColor;
    preset.environment = m_project.lookDevEnvironment;
    preset.viewSettings = m_project.lookDevViewSettings;
    preset.preserveEnvironmentPath = false;
    return preset;
}

void RenderBuilderApp::UpsertLookDevPreset(const LookDevPreset& preset)
{
    const auto existing = std::find_if(
        m_project.lookDevPresets.begin(),
        m_project.lookDevPresets.end(),
        [&](const LookDevPreset& candidate) { return candidate.name == preset.name; });
    if (existing != m_project.lookDevPresets.end())
    {
        *existing = preset;
    }
    else
    {
        m_project.lookDevPresets.push_back(preset);
    }
}

void RenderBuilderApp::ApplyLookDevPreset(std::size_t index)
{
    EnsureLookDevPresets();
    if (index >= m_project.lookDevPresets.size())
    {
        return;
    }

    const LookDevPreset preset = m_project.lookDevPresets[index];
    const std::wstring previousEnvironmentPath = m_project.lookDevEnvironment.environmentPath;
    m_project.skyTopColor = preset.skyTopColor;
    m_project.skyHorizonColor = preset.skyHorizonColor;
    m_project.lookDevEnvironment = preset.environment;
    m_project.lookDevViewSettings = preset.viewSettings;
    m_project.activeLookDevPresetName = preset.name;

    if (preset.preserveEnvironmentPath && m_project.lookDevEnvironment.environmentPath.empty())
    {
        m_project.lookDevEnvironment.environmentPath = previousEnvironmentPath;
    }

    std::string environmentDiagnostics;
    if (m_project.lookDevEnvironment.environmentPath.empty())
    {
        m_backend.UpdateEnvironmentTexture({}, environmentDiagnostics);
    }
    else if (PathExists(m_project.lookDevEnvironment.environmentPath))
    {
        if (!m_backend.UpdateEnvironmentTexture(m_project.lookDevEnvironment.environmentPath, environmentDiagnostics))
        {
            environmentDiagnostics = "Preset environment load failed: " + environmentDiagnostics;
        }
    }
    else
    {
        m_backend.UpdateEnvironmentTexture({}, environmentDiagnostics);
        environmentDiagnostics = "Preset environment missing: " + WideToUtf8(m_project.lookDevEnvironment.environmentPath);
    }

    ApplyLookDevSettings();
    m_sceneDiagnostics = "Applied LookDev preset '" + preset.name + "'.";
    if (!environmentDiagnostics.empty())
    {
        m_sceneDiagnostics += "\n" + environmentDiagnostics;
    }
    MarkProjectDirty();
}

void RenderBuilderApp::MarkLookDevCustom()
{
    m_project.activeLookDevPresetName = CustomLookDevPresetName;
}

void RenderBuilderApp::LoadShaderFromDisk(const std::filesystem::path& path)
{
    m_activeShaderSet.sourcePath = path.wstring();
    m_activeShaderSet.sourceText = ReadTextFile(path);
    std::fill(m_shaderTextBuffer.begin(), m_shaderTextBuffer.end(), '\0');
    const std::size_t copySize = std::min(m_activeShaderSet.sourceText.size(), m_shaderTextBuffer.size() - 1);
    std::memcpy(m_shaderTextBuffer.data(), m_activeShaderSet.sourceText.data(), copySize);
    m_shaderDirty = false;
}

void RenderBuilderApp::SynchronizeActiveShaderSet()
{
    m_activeShaderSet.sourceText.assign(m_shaderTextBuffer.data());
    if (m_project.shaderSets.empty())
    {
        m_project.shaderSets.push_back(m_activeShaderSet);
        m_activeShaderSetIndex = 0;
    }
    if (m_activeShaderSetIndex >= m_project.shaderSets.size())
    {
        m_activeShaderSetIndex = 0;
    }
    m_project.shaderSets[m_activeShaderSetIndex] = m_activeShaderSet;
}

void RenderBuilderApp::SelectShaderSet(std::size_t index)
{
    if (index >= m_project.shaderSets.size() || index == m_activeShaderSetIndex)
    {
        return;
    }

    SynchronizeActiveShaderSet();
    m_activeShaderSetIndex = index;
    m_activeShaderSet = m_project.shaderSets[m_activeShaderSetIndex];
    std::fill(m_shaderTextBuffer.begin(), m_shaderTextBuffer.end(), '\0');
    const std::size_t copySize = std::min(m_activeShaderSet.sourceText.size(), m_shaderTextBuffer.size() - 1);
    std::memcpy(m_shaderTextBuffer.data(), m_activeShaderSet.sourceText.data(), copySize);
    m_shaderDirty = false;
}

void RenderBuilderApp::CreateShaderSetFromActive()
{
    SynchronizeActiveShaderSet();

    ShaderSet shaderSet = m_activeShaderSet;
    char name[64] = {};
    std::snprintf(name, sizeof(name), "Shader Set %u", m_shaderSetSerial++);
    shaderSet.name = name;
    shaderSet.sourcePath.clear();
    m_project.shaderSets.push_back(shaderSet);
    SelectShaderSet(m_project.shaderSets.size() - 1);
    m_shaderDirty = true;
    MarkProjectDirty();
}

void RenderBuilderApp::Tick()
{
    const auto now = std::chrono::high_resolution_clock::now();
    const float deltaSeconds = std::chrono::duration<float>(now - m_lastTick).count();
    m_deltaSeconds = deltaSeconds;
    m_lastTick = now;

    if (m_minimized)
    {
        Sleep(16);
        return;
    }

    if (m_inSizeMove)
    {
        Sleep(16);
        return;
    }

    if (!ApplyPendingResize() || !ApplyPendingSceneTargetResize())
    {
        Sleep(1);
        return;
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        SaveProject();
    }
    if (m_project.lookDevViewSettings.turntableEnabled)
    {
        m_backend.OrbitCamera(m_project.lookDevViewSettings.turntableSpeed * deltaSeconds, 0.0f);
        m_project.viewportCamera = m_backend.CameraState();
        m_project.hasViewportCamera = true;
    }
    DrawUi();
    m_backend.Render(deltaSeconds, m_activeVertexShader, m_activePixelShader);
}

void RenderBuilderApp::RequestResize(UINT width, UINT height)
{
    m_pendingResizeWidth = std::max(width, 1u);
    m_pendingResizeHeight = std::max(height, 1u);
    m_pendingResize = true;
}

bool RenderBuilderApp::ApplyPendingResize()
{
    if (!m_pendingResize || !m_backend.Device())
    {
        return true;
    }

    if (!m_backend.Resize(m_pendingResizeWidth, m_pendingResizeHeight))
    {
        ++m_resizeDeferFrames;
        if (m_resizeDeferFrames == 1 || (m_resizeDeferFrames % 60) == 0)
        {
            m_sceneDiagnostics = "Window resize is waiting for the GPU. See Bin/Logs/RenderBuilder.log if it does not recover.";
        }
        return false;
    }

    m_pendingResize = false;
    m_resizeDeferFrames = 0;
    return true;
}

void RenderBuilderApp::RequestSceneTargetResize(UINT width, UINT height)
{
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (width == m_backend.SceneWidth() && height == m_backend.SceneHeight())
    {
        m_pendingSceneTargetResize = false;
        return;
    }

    m_pendingSceneTargetWidth = width;
    m_pendingSceneTargetHeight = height;
    m_pendingSceneTargetResize = true;
}

bool RenderBuilderApp::ApplyPendingSceneTargetResize()
{
    if (!m_pendingSceneTargetResize || !m_backend.Device())
    {
        return true;
    }

    const UINT width = m_pendingSceneTargetWidth;
    const UINT height = m_pendingSceneTargetHeight;
    if (!m_backend.ResizeSceneTarget(width, height))
    {
        ++m_sceneTargetResizeDeferFrames;
        if (m_sceneTargetResizeDeferFrames == 1 || (m_sceneTargetResizeDeferFrames % 60) == 0)
        {
            m_sceneDiagnostics = "Viewport target resize is waiting for the GPU. See Bin/Logs/RenderBuilder.log if it does not recover.";
        }
        return false;
    }

    m_pendingSceneTargetResize = false;
    m_sceneTargetResizeDeferFrames = 0;
    return true;
}

void RenderBuilderApp::DrawUi()
{
    DrawDockspace();
    DrawViewportPanel();
    DrawShaderEditorPanel();
    DrawMaterialInspectorPanel();
    DrawAssetBrowserPanel();
    DrawDiagnosticsPanel();
    DrawStatsPanel();
}

void RenderBuilderApp::DrawDockspace()
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("RenderBuilderDockspace", nullptr, windowFlags);
    ImGui::PopStyleVar(2);

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open Shader..."))
            {
                const auto path = OpenFileDialog(L"HLSL Files\0*.hlsl;*.hlsli\0All Files\0*.*\0");
                if (!path.empty())
                {
                    LoadShaderFromDisk(path);
                    CompileActiveShader();
                    MarkProjectDirty();
                }
            }
            if (ImGui::MenuItem("Open Scene..."))
            {
                const auto path = OpenFileDialog(L"Model Files\0*.gltf;*.glb;*.fbx;*.obj\0All Files\0*.*\0");
                if (!path.empty())
                {
                    LoadScenePath(path.wstring());
                }
            }
            if (ImGui::MenuItem("Open Project..."))
            {
                LoadProject();
            }
            if (ImGui::MenuItem("Save Project", "Ctrl+S"))
            {
                SaveProject();
            }
            if (ImGui::MenuItem("Save Project As..."))
            {
                SaveProjectAs();
            }
            if (ImGui::BeginMenu("Recent Projects", !m_recentProjects.empty()))
            {
                for (const std::filesystem::path& recentPath : m_recentProjects)
                {
                    const std::string label = recentPath.string();
                    if (ImGui::MenuItem(label.c_str()))
                    {
                        LoadProjectFromDisk(recentPath);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Exit"))
            {
                PostQuitMessage(0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Build"))
        {
            if (ImGui::MenuItem("Compile Shader", "Ctrl+Enter"))
            {
                CompileActiveShader();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    const ImGuiID dockspaceId = ImGui::GetID("RenderBuilderDockspaceId");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();
}

void RenderBuilderApp::DrawViewportPanel()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Viewport");
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x < 1.0f || available.y < 1.0f)
    {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    ImVec2 imageSize = available;
    const UINT targetWidth = static_cast<UINT>(std::max(imageSize.x, 1.0f));
    const UINT targetHeight = static_cast<UINT>(std::max(imageSize.y, 1.0f));
    RequestSceneTargetResize(targetWidth, targetHeight);
    const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_backend.SceneSrvGpu();
    const ImTextureID textureId = static_cast<ImTextureID>(gpuHandle.ptr);
    const ImVec2 imageTopLeft = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(
        "ViewportCanvas",
        imageSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 imageBottomRight(imageTopLeft.x + imageSize.x, imageTopLeft.y + imageSize.y);
    ImGui::GetWindowDrawList()->AddImage(textureId, imageTopLeft, imageBottomRight);
    HandleViewportCameraControls();
    ImGui::End();
    ImGui::PopStyleVar();
}

void RenderBuilderApp::HandleViewportCameraControls()
{
    ImGuiIO& io = ImGui::GetIO();
    const bool viewportHovered = ImGui::IsItemHovered();
    const bool viewportActive = ImGui::IsItemActive();
    const bool viewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    bool cameraChanged = false;

    if (viewportHovered)
    {
        if (io.MouseWheel != 0.0f)
        {
            m_backend.DollyCamera(io.MouseWheel);
            cameraChanged = true;
        }

        if (viewportActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            m_backend.OrbitCamera(io.MouseDelta.x * 0.008f, -io.MouseDelta.y * 0.008f);
            cameraChanged = true;
        }
        else if (viewportActive && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
        {
            m_backend.PanCamera(io.MouseDelta.x * 0.002f, io.MouseDelta.y * 0.002f);
            cameraChanged = true;
        }
    }

    if (viewportHovered || viewportActive || viewportFocused)
    {
        const float speed = (io.KeyShift ? 1.8f : 0.65f) * m_deltaSeconds;
        float forward = 0.0f;
        float right = 0.0f;
        float up = 0.0f;

        if (ImGui::IsKeyDown(ImGuiKey_W)) { forward += speed; }
        if (ImGui::IsKeyDown(ImGuiKey_S)) { forward -= speed; }
        if (ImGui::IsKeyDown(ImGuiKey_D)) { right += speed; }
        if (ImGui::IsKeyDown(ImGuiKey_A)) { right -= speed; }
        if (ImGui::IsKeyDown(ImGuiKey_E)) { up += speed; }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) { up -= speed; }
        if (forward != 0.0f || right != 0.0f || up != 0.0f)
        {
            m_backend.MoveCamera(forward, right, up);
            cameraChanged = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home))
        {
            m_backend.ResetCameraToScene();
            cameraChanged = true;
        }
    }

    if (cameraChanged)
    {
        m_project.viewportCamera = m_backend.CameraState();
        m_project.hasViewportCamera = true;
        MarkProjectDirty();
    }
}

void RenderBuilderApp::DrawShaderEditorPanel()
{
    ImGui::Begin("Shader Editor");
    if (ImGui::BeginCombo("Shader Set", m_activeShaderSet.name.c_str()))
    {
        for (std::size_t i = 0; i < m_project.shaderSets.size(); ++i)
        {
            const bool selected = i == m_activeShaderSetIndex;
            if (ImGui::Selectable(m_project.shaderSets[i].name.c_str(), selected))
            {
                SelectShaderSet(i);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("New"))
    {
        CreateShaderSetFromActive();
    }

    char shaderName[128] = {};
    const std::string previousName = m_activeShaderSet.name;
    strncpy_s(shaderName, m_activeShaderSet.name.c_str(), _TRUNCATE);
    if (ImGui::InputText("Name", shaderName, sizeof(shaderName)))
    {
        if (shaderName[0] != '\0')
        {
            m_activeShaderSet.name = shaderName;
            for (MaterialAssignment& assignment : m_project.materialAssignments)
            {
                if (assignment.shaderSetName == previousName)
                {
                    assignment.shaderSetName = m_activeShaderSet.name;
                }
            }
            m_backend.SetMaterialAssignments(m_project.materialAssignments);
            m_shaderDirty = true;
            SynchronizeActiveShaderSet();
            MarkProjectDirty();
        }
    }

    ImGui::Text("Source: %s", m_activeShaderSet.sourcePath.empty() ? "<memory>" : WideToUtf8(m_activeShaderSet.sourcePath).c_str());
    ImGui::SameLine();
    if (m_shaderDirty)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "modified");
    }
    else
    {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "compiled");
    }

    ImGui::PushItemWidth(140.0f);
    char vsEntry[64] = {};
    char psEntry[64] = {};
    const std::string vsEntryText = WideToUtf8(m_activeShaderSet.vertexEntry);
    const std::string psEntryText = WideToUtf8(m_activeShaderSet.pixelEntry);
    strncpy_s(vsEntry, vsEntryText.c_str(), _TRUNCATE);
    strncpy_s(psEntry, psEntryText.c_str(), _TRUNCATE);
    if (ImGui::InputText("VS Entry", vsEntry, sizeof(vsEntry)))
    {
        m_activeShaderSet.vertexEntry = Utf8ToWide(vsEntry);
        m_shaderDirty = true;
        MarkProjectDirty();
    }
    ImGui::SameLine();
    if (ImGui::InputText("PS Entry", psEntry, sizeof(psEntry)))
    {
        m_activeShaderSet.pixelEntry = Utf8ToWide(psEntry);
        m_shaderDirty = true;
        MarkProjectDirty();
    }
    ImGui::PopItemWidth();

    if (ImGui::Button("Compile"))
    {
        CompileActiveShader();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload Default"))
    {
        const std::string activeName = m_activeShaderSet.name;
        LoadShaderFromDisk(m_rootDirectory / "Shaders" / "DefaultRaster.hlsl");
        m_activeShaderSet.name = activeName;
        m_activeShaderSet.vertexEntry = L"VSMain";
        m_activeShaderSet.pixelEntry = L"PSMain";
        m_activeShaderSet.vertexProfile = L"vs_6_9";
        m_activeShaderSet.pixelProfile = L"ps_6_9";
        SynchronizeActiveShaderSet();
        CompileActiveShader();
        MarkProjectDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload LookDev PBR"))
    {
        const std::string activeName = m_activeShaderSet.name;
        LoadShaderFromDisk(m_rootDirectory / "Shaders" / "LookDevPBR.hlsl");
        m_activeShaderSet.name = activeName;
        m_activeShaderSet.vertexEntry = L"VSMain";
        m_activeShaderSet.pixelEntry = L"PSMain";
        m_activeShaderSet.vertexProfile = L"vs_6_9";
        m_activeShaderSet.pixelProfile = L"ps_6_9";
        SynchronizeActiveShaderSet();
        CompileActiveShader();
        MarkProjectDirty();
    }

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
    if (ImGui::InputTextMultiline("##ShaderSource", m_shaderTextBuffer.data(), m_shaderTextBuffer.size(), ImVec2(-FLT_MIN, -FLT_MIN), flags))
    {
        m_shaderDirty = true;
        MarkProjectDirty();
    }
    ImGui::End();
}

void RenderBuilderApp::DrawMaterialInspectorPanel()
{
    ImGui::Begin("Material Inspector");
    if (m_project.materialAssignments.empty())
    {
        ImGui::TextUnformatted("No imported materials yet.");
    }
    for (MaterialAssignment& assignment : m_project.materialAssignments)
    {
        ImGui::PushID(assignment.materialName.c_str());
        ImGui::SeparatorText(assignment.materialName.c_str());
        bool assignmentChanged = false;
        if (ImGui::BeginCombo("Shader Set", assignment.shaderSetName.c_str()))
        {
            for (const ShaderSet& shaderSet : m_project.shaderSets)
            {
                const bool selected = assignment.shaderSetName == shaderSet.name;
                if (ImGui::Selectable(shaderSet.name.c_str(), selected))
                {
                    assignment.shaderSetName = shaderSet.name;
                    assignmentChanged = true;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        const SceneMaterial* sceneMaterial = FindSceneMaterial(assignment.materialName);

        if (ImGui::ColorEdit4("Base Color Factor", assignment.baseColorFactor.data()))
        {
            assignmentChanged = true;
        }
        ImGui::PushItemWidth(180.0f);
        if (ImGui::SliderFloat("Roughness Factor", &assignment.roughnessFactor, 0.0f, 1.0f, "%.2f"))
        {
            assignmentChanged = true;
        }
        if (ImGui::SliderFloat("Metallic Factor", &assignment.metallicFactor, 0.0f, 1.0f, "%.2f"))
        {
            assignmentChanged = true;
        }
        if (ImGui::SliderFloat("Occlusion Strength", &assignment.occlusionStrength, 0.0f, 1.0f, "%.2f"))
        {
            assignmentChanged = true;
        }
        ImGui::PopItemWidth();
        if (ImGui::ColorEdit3("Emissive Color", assignment.emissiveFactor.data()))
        {
            assignmentChanged = true;
        }
        ImGui::PushItemWidth(180.0f);
        if (ImGui::SliderFloat("Emissive Intensity", &assignment.emissiveFactor[3], 0.0f, 20.0f, "%.2f"))
        {
            assignmentChanged = true;
        }
        const char* alphaModes[] = { "Opaque", "Mask", "Blend" };
        int alphaMode = static_cast<int>(assignment.alphaMode);
        if (ImGui::Combo("Alpha Mode", &alphaMode, alphaModes, _countof(alphaModes)))
        {
            assignment.alphaMode = static_cast<AlphaMode>(alphaMode);
            assignmentChanged = true;
        }
        if (assignment.alphaMode == AlphaMode::Mask && ImGui::SliderFloat("Alpha Cutoff", &assignment.alphaCutoff, 0.0f, 1.0f, "%.2f"))
        {
            assignmentChanged = true;
        }
        ImGui::PopItemWidth();

        ImGui::SeparatorText("Textures");
        for (std::size_t textureSlot = 0; textureSlot < MaterialTextureSlotCount; ++textureSlot)
        {
            ImGui::PushID(static_cast<int>(textureSlot));
            const std::wstring importedPath = sceneMaterial ? SceneTexturePath(*sceneMaterial, textureSlot) : std::wstring();
            const std::wstring effectivePath = EffectiveTexturePath(assignment, textureSlot);
            const char* sourceLabel = assignment.textureOverrideEnabled[textureSlot]
                ? (effectivePath.empty() ? "cleared" : "override")
                : (importedPath.empty() ? "none" : "imported");

            ImGui::Text("%s: %s", TextureSlotLabels[textureSlot], TextureFileName(effectivePath).c_str());
            if (ImGui::IsItemHovered() && !effectivePath.empty())
            {
                ImGui::SetTooltip("%s", WideToUtf8(effectivePath).c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", sourceLabel);
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
            {
                const auto path = OpenFileDialog(TextureFileFilter);
                if (!path.empty())
                {
                    assignment.textureOverrideEnabled[textureSlot] = true;
                    assignment.textureOverrides[textureSlot] = path.wstring();
                    ApplyMaterialTextureSlot(assignment, textureSlot);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear"))
            {
                assignment.textureOverrideEnabled[textureSlot] = true;
                assignment.textureOverrides[textureSlot].clear();
                ApplyMaterialTextureSlot(assignment, textureSlot);
            }
            if (assignment.textureOverrideEnabled[textureSlot])
            {
                ImGui::SameLine();
                if (ImGui::Button("Use Imported"))
                {
                    assignment.textureOverrideEnabled[textureSlot] = false;
                    assignment.textureOverrides[textureSlot].clear();
                    ApplyMaterialTextureSlot(assignment, textureSlot);
                }
            }
            ImGui::PopID();
        }

        ImGui::PushItemWidth(180.0f);
        if (ImGui::SliderFloat("Normal Strength", &assignment.normalStrength, 0.0f, 2.0f, "%.2f"))
        {
            assignmentChanged = true;
        }
        ImGui::PopItemWidth();
        if (ImGui::Checkbox("Flip Normal Green", &assignment.flipNormalGreen))
        {
            assignmentChanged = true;
        }
        if (assignmentChanged)
        {
            m_backend.SetMaterialAssignments(m_project.materialAssignments);
            MarkProjectDirty();
        }
        ImGui::TextUnformatted("Pipeline: Raster VS/PS");
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Mesh Shader and Ray Tracing assignment slots are present in the data model and disabled until their backend milestones land.");
    ImGui::End();
}

void RenderBuilderApp::DrawAssetBrowserPanel()
{
    ImGui::Begin("Scene / Asset Browser");
    ImGui::Text("Root: %s", m_rootDirectory.string().c_str());
    ImGui::Text("Project: %s%s",
        m_projectDirty ? "*" : "",
        m_project.path.empty() ? "<untitled>" : WideToUtf8(m_project.path).c_str());
    ImGui::Text("Scene: %s", m_project.scenePath.empty() ? "<built-in cube>" : WideToUtf8(m_project.scenePath).c_str());
    ImGui::Text("Geometry: %zu vertices, %zu indices, %zu draws", m_sceneVertexCount, m_sceneIndexCount, m_sceneDrawCount);
    bool skyChanged = false;
    skyChanged |= ImGui::ColorEdit3("Sky Top", m_project.skyTopColor.data());
    skyChanged |= ImGui::ColorEdit3("Sky Horizon", m_project.skyHorizonColor.data());
    if (skyChanged)
    {
        MarkLookDevCustom();
        ApplyLookDevSettings();
        MarkProjectDirty();
    }
    ImGui::SeparatorText("Environment");
    EnsureLookDevPresets();
    const auto activePreset = std::find_if(
        m_project.lookDevPresets.begin(),
        m_project.lookDevPresets.end(),
        [&](const LookDevPreset& preset) { return preset.name == m_project.activeLookDevPresetName; });
    const std::string presetPreview = activePreset == m_project.lookDevPresets.end() ? CustomLookDevPresetName : activePreset->name;
    if (ImGui::BeginCombo("LookDev Preset", presetPreview.c_str()))
    {
        for (std::size_t presetIndex = 0; presetIndex < m_project.lookDevPresets.size(); ++presetIndex)
        {
            const LookDevPreset& preset = m_project.lookDevPresets[presetIndex];
            const bool selected = preset.name == m_project.activeLookDevPresetName;
            if (ImGui::Selectable(preset.name.c_str(), selected))
            {
                ApplyLookDevPreset(presetIndex);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        if (activePreset == m_project.lookDevPresets.end())
        {
            ImGui::Separator();
            ImGui::TextDisabled("%s", CustomLookDevPresetName);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Current Preset"))
    {
        const std::string baseName = presetPreview == CustomLookDevPresetName ? "Custom Preset" : presetPreview + " Copy";
        std::snprintf(m_lookDevPresetNameBuffer, sizeof(m_lookDevPresetNameBuffer), "%s", baseName.c_str());
        ImGui::OpenPopup("Save LookDev Preset");
    }
    if (ImGui::BeginPopupModal("Save LookDev Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Name", m_lookDevPresetNameBuffer, sizeof(m_lookDevPresetNameBuffer));
        if (ImGui::Button("Save"))
        {
            std::string presetName = m_lookDevPresetNameBuffer;
            if (presetName.empty())
            {
                presetName = "Custom Preset";
            }
            UpsertLookDevPreset(CaptureCurrentLookDevPreset(presetName));
            m_project.activeLookDevPresetName = presetName;
            m_sceneDiagnostics = "Saved LookDev preset '" + presetName + "'.";
            MarkProjectDirty();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Text("HDRI: %s", m_project.lookDevEnvironment.environmentPath.empty() ? "<none>" : TextureFileName(m_project.lookDevEnvironment.environmentPath).c_str());
    if (ImGui::IsItemHovered() && !m_project.lookDevEnvironment.environmentPath.empty())
    {
        ImGui::SetTooltip("%s", WideToUtf8(m_project.lookDevEnvironment.environmentPath).c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Load HDRI..."))
    {
        const auto path = OpenFileDialog(EnvironmentFileFilter);
        if (!path.empty())
        {
            std::string diagnostics;
            if (m_backend.UpdateEnvironmentTexture(path.wstring(), diagnostics))
            {
                MarkLookDevCustom();
                m_project.lookDevEnvironment.environmentPath = path.wstring();
                if (m_project.lookDevEnvironment.backgroundMode == LookDevBackgroundMode::SkyColor)
                {
                    m_project.lookDevEnvironment.backgroundMode = LookDevBackgroundMode::Hdri;
                }
                ApplyLookDevSettings();
                m_sceneDiagnostics = diagnostics;
                MarkProjectDirty();
            }
            else
            {
                m_sceneDiagnostics = "Environment load failed: " + diagnostics;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear HDRI"))
    {
        std::string diagnostics;
        m_backend.UpdateEnvironmentTexture({}, diagnostics);
        MarkLookDevCustom();
        m_project.lookDevEnvironment.environmentPath.clear();
        ApplyLookDevSettings();
        m_sceneDiagnostics = diagnostics;
        MarkProjectDirty();
    }

    const char* backgroundModes[] = { "SkyColor", "HDRI Background", "Transparent Checker" };
    int backgroundMode = static_cast<int>(m_project.lookDevEnvironment.backgroundMode);
    bool lookDevChanged = false;
    if (ImGui::Combo("Background", &backgroundMode, backgroundModes, _countof(backgroundModes)))
    {
        m_project.lookDevEnvironment.backgroundMode = static_cast<LookDevBackgroundMode>(backgroundMode);
        lookDevChanged = true;
    }
    ImGui::PushItemWidth(180.0f);
    lookDevChanged |= ImGui::SliderAngle("HDRI Rotation", &m_project.lookDevEnvironment.rotationYaw, -180.0f, 180.0f);
    lookDevChanged |= ImGui::SliderFloat("Environment Intensity", &m_project.lookDevEnvironment.intensity, 0.0f, 8.0f, "%.2f");
    lookDevChanged |= ImGui::SliderFloat3("Sun Direction", m_project.lookDevEnvironment.sunDirection.data(), -1.0f, 1.0f, "%.2f");
    lookDevChanged |= ImGui::ColorEdit3("Sun Color", m_project.lookDevEnvironment.sunColor.data());
    lookDevChanged |= ImGui::SliderFloat("Sun Intensity", &m_project.lookDevEnvironment.sunIntensity, 0.0f, 10.0f, "%.2f");
    lookDevChanged |= ImGui::SliderFloat("Exposure", &m_project.lookDevViewSettings.exposure, -8.0f, 8.0f, "%.2f EV");
    lookDevChanged |= ImGui::SliderFloat("Gamma", &m_project.lookDevViewSettings.gamma, 1.0f, 3.0f, "%.2f");
    ImGui::PopItemWidth();

    const char* toneMappers[] = { "None", "Reinhard", "ACES" };
    int toneMapper = static_cast<int>(m_project.lookDevViewSettings.toneMapper);
    if (ImGui::Combo("Tone Mapper", &toneMapper, toneMappers, _countof(toneMappers)))
    {
        m_project.lookDevViewSettings.toneMapper = static_cast<ToneMapper>(toneMapper);
        lookDevChanged = true;
    }
    const char* displayModes[] = { "Beauty", "BaseColor", "Normal", "Roughness", "Metallic", "AO", "Emissive", "LightingOnly" };
    int displayMode = static_cast<int>(m_project.lookDevViewSettings.displayMode);
    if (ImGui::Combo("Display Mode", &displayMode, displayModes, _countof(displayModes)))
    {
        m_project.lookDevViewSettings.displayMode = static_cast<LookDevDisplayMode>(displayMode);
        m_backend.SetDebugViewMode(m_project.lookDevViewSettings.displayMode);
        lookDevChanged = true;
    }
    if (ImGui::Checkbox("Turntable", &m_project.lookDevViewSettings.turntableEnabled))
    {
        lookDevChanged = true;
    }
    ImGui::SameLine();
    ImGui::PushItemWidth(140.0f);
    if (ImGui::SliderFloat("Speed", &m_project.lookDevViewSettings.turntableSpeed, -2.0f, 2.0f, "%.2f"))
    {
        lookDevChanged = true;
    }
    ImGui::PopItemWidth();
    if (lookDevChanged)
    {
        MarkLookDevCustom();
        ApplyLookDevSettings();
        MarkProjectDirty();
    }

    if (ImGui::Button("Load Scene..."))
    {
        const auto path = OpenFileDialog(L"Model Files\0*.gltf;*.glb;*.fbx;*.obj\0All Files\0*.*\0");
        if (!path.empty())
        {
            LoadScenePath(path.wstring());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Project..."))
    {
        LoadProject();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Project"))
    {
        SaveProject();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As..."))
    {
        SaveProjectAs();
    }
    ImGui::SameLine();
    if (ImGui::Button("Snapshot..."))
    {
        SaveViewportSnapshot();
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", m_sceneDiagnostics.c_str());
    ImGui::End();
}

void RenderBuilderApp::DrawDiagnosticsPanel()
{
    ImGui::Begin("Compile Diagnostics");
    if (m_lastCompileSucceeded)
    {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "Last compile succeeded.");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f), "Last compile failed. Keeping last valid PSO.");
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", m_compileDiagnostics.c_str());
    ImGui::End();
}

void RenderBuilderApp::DrawStatsPanel()
{
    const BackendCapabilities caps = m_backend.Capabilities();
    ImGui::Begin("Renderer Stats");
    ImGui::Text("Backend: Direct3D 12");
    ImGui::Text("Adapter: %s", caps.adapterName.c_str());
    ImGui::Text("Frame: %llu", static_cast<unsigned long long>(m_backend.FrameNumber()));
    ImGui::Text("CPU frame: %.3f ms", m_backend.LastFrameMs());
    ImGui::Text("Preview target: %u x %u", m_backend.SceneWidth(), m_backend.SceneHeight());
    ImGui::Text("Vertex count: %u", m_backend.VertexCount());
    ImGui::Text("Index count: %u", m_backend.IndexCount());
    ImGui::Text("Environment: %s", m_backend.HasEnvironmentTexture() ? "HDRI loaded" : "fallback");
    ImGui::Text("Background: %s", BackgroundModeName(m_project.lookDevEnvironment.backgroundMode));
    ImGui::Text("Exposure: %.2f EV", m_project.lookDevViewSettings.exposure);
    ImGui::Text("Display: %s", DisplayModeName(m_project.lookDevViewSettings.displayMode));
    ImGui::Separator();
    ImGui::Text("SM 6.9: %s", caps.shaderModel69 ? "available" : "not reported");
    ImGui::Text("Mesh Shader: %s", caps.meshShader ? "available" : "disabled");
    ImGui::Text("Ray Tracing: %s", caps.rayTracing ? "available" : "disabled");
    ImGui::Text("Vulkan Backend: planned");
    ImGui::End();
}

void RenderBuilderApp::CompileActiveShader()
{
    SynchronizeActiveShaderSet();

    std::string diagnostics;
    const bool succeeded = CompileShaderSet(m_project.shaderSets[m_activeShaderSetIndex], &m_activeVertexShader, &m_activePixelShader, diagnostics);
    m_activeShaderSet = m_project.shaderSets[m_activeShaderSetIndex];
    m_lastCompileSucceeded = succeeded;
    if (succeeded)
    {
        m_shaderDirty = false;
    }
    m_compileDiagnostics = diagnostics;
}

bool RenderBuilderApp::CompileShaderSet(ShaderSet& shaderSet, std::vector<std::uint8_t>* vertexShader, std::vector<std::uint8_t>* pixelShader, std::string& diagnostics)
{
    const std::wstring includeDirectory = (m_rootDirectory / "Shaders").wstring();

    ShaderCompileRequest vsRequest;
    vsRequest.sourceName = shaderSet.sourcePath.empty() ? L"ShaderEditor.hlsl" : shaderSet.sourcePath;
    vsRequest.source = shaderSet.sourceText;
    vsRequest.includeDirectory = includeDirectory;
    vsRequest.entryPoint = shaderSet.vertexEntry;
    vsRequest.profile = shaderSet.vertexProfile;

    ShaderCompileRequest psRequest = vsRequest;
    psRequest.entryPoint = shaderSet.pixelEntry;
    psRequest.profile = shaderSet.pixelProfile;

    const ShaderCompileResult vs = m_shaderCompiler->Compile(vsRequest);
    const ShaderCompileResult ps = m_shaderCompiler->Compile(psRequest);

    std::ostringstream output;
    output << "[" << shaderSet.name << "]\n";
    output << "[Vertex Shader]\n" << vs.diagnostics << "\n\n[Pixel Shader]\n" << ps.diagnostics << "\n";

    if (vs.succeeded && ps.succeeded)
    {
        std::string psoDiagnostics;
        if (m_backend.TryApplyShaders(shaderSet.name, vs.bytecode, ps.bytecode, psoDiagnostics))
        {
            if (vertexShader)
            {
                *vertexShader = vs.bytecode;
            }
            if (pixelShader)
            {
                *pixelShader = ps.bytecode;
            }
            output << "\n[D3D12]\n" << psoDiagnostics;
            diagnostics = output.str();
            return true;
        }
        output << "\n[D3D12]\n" << psoDiagnostics;
    }

    diagnostics = output.str();
    return false;
}

bool RenderBuilderApp::LoadScenePath(const std::wstring& path, bool markDirty)
{
    SceneImportResult result = m_sceneImporter.ImportScene(path);
    if (!result.succeeded)
    {
        m_sceneDiagnostics = result.diagnostics;
        return false;
    }

    std::string backendDiagnostics;
    if (!m_backend.LoadSceneMesh(result.scene, backendDiagnostics))
    {
        m_sceneDiagnostics = result.diagnostics + "\n" + backendDiagnostics;
        return false;
    }

    m_project.scenePath = path;
    m_sceneMaterials = result.scene.materials;
    if (!result.scene.materials.empty())
    {
        m_project.materialAssignments.clear();
        m_project.materialAssignments.reserve(result.scene.materials.size());
        for (const SceneMaterial& material : result.scene.materials)
        {
            m_project.materialAssignments.push_back(material.assignment);
        }
        m_backend.SetMaterialAssignments(m_project.materialAssignments);
    }
    m_sceneVertexCount = result.scene.vertices.size();
    m_sceneIndexCount = result.scene.indices.size();
    m_sceneDrawCount = result.scene.draws.size();
    m_sceneDiagnostics = result.diagnostics + "\n" + backendDiagnostics;
    if (markDirty)
    {
        MarkProjectDirty();
    }
    return true;
}

void RenderBuilderApp::UseDefaultScenePreview()
{
    m_backend.ResetPreviewScene();
    m_sceneMaterials.clear();
    m_sceneVertexCount = 24;
    m_sceneIndexCount = 36;
    m_sceneDrawCount = 1;
}

const SceneMaterial* RenderBuilderApp::FindSceneMaterial(const std::string& materialName) const
{
    const auto it = std::find_if(
        m_sceneMaterials.begin(),
        m_sceneMaterials.end(),
        [&materialName](const SceneMaterial& material)
        {
            return material.assignment.materialName == materialName;
        });
    return it != m_sceneMaterials.end() ? &(*it) : nullptr;
}

std::wstring RenderBuilderApp::ImportedTexturePath(const std::string& materialName, std::size_t textureSlot) const
{
    const SceneMaterial* material = FindSceneMaterial(materialName);
    return material ? SceneTexturePath(*material, textureSlot) : std::wstring();
}

std::wstring RenderBuilderApp::EffectiveTexturePath(const MaterialAssignment& assignment, std::size_t textureSlot) const
{
    if (textureSlot >= MaterialTextureSlotCount)
    {
        return {};
    }
    if (assignment.textureOverrideEnabled[textureSlot])
    {
        return assignment.textureOverrides[textureSlot];
    }
    return ImportedTexturePath(assignment.materialName, textureSlot);
}

void RenderBuilderApp::ApplyMaterialTextureSlot(const MaterialAssignment& assignment, std::size_t textureSlot)
{
    std::string diagnostics;
    const std::wstring effectivePath = EffectiveTexturePath(assignment, textureSlot);
    if (m_backend.UpdateMaterialTextureSlot(assignment.materialName, static_cast<std::uint32_t>(textureSlot), effectivePath, diagnostics))
    {
        m_sceneDiagnostics = std::string(TextureSlotLabels[textureSlot]) + " texture updated for " + assignment.materialName + ".\n" + diagnostics;
    }
    else
    {
        m_sceneDiagnostics = std::string(TextureSlotLabels[textureSlot]) + " texture update failed for " + assignment.materialName + ".\n" + diagnostics;
    }
    MarkProjectDirty();
}

std::string RenderBuilderApp::ApplyMaterialTextureOverrides(const std::vector<MaterialAssignment>& assignments)
{
    std::ostringstream diagnostics;
    for (const MaterialAssignment& assignment : assignments)
    {
        for (std::size_t textureSlot = 0; textureSlot < MaterialTextureSlotCount; ++textureSlot)
        {
            if (!assignment.textureOverrideEnabled[textureSlot])
            {
                continue;
            }

            std::string textureDiagnostics;
            const std::wstring effectivePath = EffectiveTexturePath(assignment, textureSlot);
            if (m_backend.UpdateMaterialTextureSlot(assignment.materialName, static_cast<std::uint32_t>(textureSlot), effectivePath, textureDiagnostics))
            {
                diagnostics << "\n" << assignment.materialName << " " << TextureSlotLabels[textureSlot] << ": " << textureDiagnostics;
            }
            else
            {
                diagnostics << "\n" << assignment.materialName << " " << TextureSlotLabels[textureSlot] << " failed: " << textureDiagnostics;
            }
        }
    }
    return diagnostics.str();
}

void RenderBuilderApp::SaveProject()
{
    if (m_project.path.empty())
    {
        SaveProjectAs();
        return;
    }
    SaveProjectToDisk(std::filesystem::path(m_project.path));
}

void RenderBuilderApp::SaveProjectAs()
{
    const auto path = SaveFileDialog(L"RenderBuilder Project\0*.renderbuilder.json;*.json\0All Files\0*.*\0", L"renderbuilder.json");
    if (path.empty())
    {
        return;
    }
    SaveProjectToDisk(path);
}

void RenderBuilderApp::SaveViewportSnapshot()
{
    const auto path = SaveFileDialog(L"PNG Image\0*.png\0All Files\0*.*\0", L"png");
    if (path.empty())
    {
        return;
    }

    std::string diagnostics;
    if (m_backend.SaveSceneSnapshot(path.wstring(), diagnostics))
    {
        m_sceneDiagnostics = diagnostics;
    }
    else
    {
        m_sceneDiagnostics = "Snapshot export failed: " + diagnostics;
    }
}

bool RenderBuilderApp::SaveProjectToDisk(const std::filesystem::path& requestedPath)
{
    try
    {
        SynchronizeActiveShaderSet();
        std::filesystem::path path = requestedPath;
        if (path.extension().empty())
        {
            path += L".renderbuilder.json";
        }
        path = AbsoluteLexicalPath(path);
        const std::filesystem::path projectDirectory = path.parent_path();
        m_project.viewportCamera = m_backend.CameraState();
        m_project.hasViewportCamera = true;
        EnsureLookDevPresets();
        const auto activePreset = std::find_if(
            m_project.lookDevPresets.begin(),
            m_project.lookDevPresets.end(),
            [&](const LookDevPreset& preset) { return preset.name == m_project.activeLookDevPresetName; });
        if (activePreset == m_project.lookDevPresets.end() || m_project.activeLookDevPresetName == CustomLookDevPresetName)
        {
            UpsertLookDevPreset(CaptureCurrentLookDevPreset(m_project.activeLookDevPresetName.empty() ? CustomLookDevPresetName : m_project.activeLookDevPresetName));
        }

        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty())
        {
            std::filesystem::create_directories(parent);
        }

        std::ostringstream json;
        json << "{\n";
        json << "  \"backend\": \"D3D12\",\n";
        json << "  \"scenePath\": \"" << EscapeJson(JsonPathString(m_project.scenePath, projectDirectory)) << "\",\n";
        json << "  \"skyTopColor\": ["
             << m_project.skyTopColor[0] << ", "
             << m_project.skyTopColor[1] << ", "
             << m_project.skyTopColor[2] << ", "
             << m_project.skyTopColor[3] << "],\n";
        json << "  \"skyHorizonColor\": ["
             << m_project.skyHorizonColor[0] << ", "
             << m_project.skyHorizonColor[1] << ", "
             << m_project.skyHorizonColor[2] << ", "
             << m_project.skyHorizonColor[3] << "],\n";
        json << "  \"viewportCamera\": { "
             << "\"target\": ["
             << m_project.viewportCamera.target[0] << ", "
             << m_project.viewportCamera.target[1] << ", "
             << m_project.viewportCamera.target[2] << "], "
             << "\"yaw\": " << m_project.viewportCamera.yaw << ", "
             << "\"pitch\": " << m_project.viewportCamera.pitch << ", "
             << "\"distance\": " << m_project.viewportCamera.distance << " },\n";
        json << "  \"lookDevEnvironment\": { "
             << "\"environmentPath\": \"" << EscapeJson(JsonPathString(m_project.lookDevEnvironment.environmentPath, projectDirectory)) << "\", "
             << "\"rotationYaw\": " << m_project.lookDevEnvironment.rotationYaw << ", "
             << "\"intensity\": " << m_project.lookDevEnvironment.intensity << ", "
             << "\"backgroundMode\": \"" << EscapeJson(BackgroundModeJsonName(m_project.lookDevEnvironment.backgroundMode)) << "\", "
             << "\"sunDirection\": ["
             << m_project.lookDevEnvironment.sunDirection[0] << ", "
             << m_project.lookDevEnvironment.sunDirection[1] << ", "
             << m_project.lookDevEnvironment.sunDirection[2] << "], "
             << "\"sunColor\": ["
             << m_project.lookDevEnvironment.sunColor[0] << ", "
             << m_project.lookDevEnvironment.sunColor[1] << ", "
             << m_project.lookDevEnvironment.sunColor[2] << "], "
             << "\"sunIntensity\": " << m_project.lookDevEnvironment.sunIntensity << " },\n";
        json << "  \"lookDevViewSettings\": { "
             << "\"exposure\": " << m_project.lookDevViewSettings.exposure << ", "
             << "\"toneMapper\": \"" << EscapeJson(ToneMapperJsonName(m_project.lookDevViewSettings.toneMapper)) << "\", "
             << "\"gamma\": " << m_project.lookDevViewSettings.gamma << ", "
             << "\"displayMode\": \"" << EscapeJson(DisplayModeJsonName(m_project.lookDevViewSettings.displayMode)) << "\", "
             << "\"turntableEnabled\": " << (m_project.lookDevViewSettings.turntableEnabled ? "true" : "false") << ", "
             << "\"turntableSpeed\": " << m_project.lookDevViewSettings.turntableSpeed << " },\n";
        json << "  \"activeLookDevPreset\": \"" << EscapeJson(m_project.activeLookDevPresetName) << "\",\n";
        json << "  \"lookDevPresets\": [\n";
        for (std::size_t i = 0; i < m_project.lookDevPresets.size(); ++i)
        {
            const LookDevPreset& preset = m_project.lookDevPresets[i];
            json << "    { "
                 << "\"name\": \"" << EscapeJson(preset.name) << "\", "
                 << "\"skyTopColor\": ["
                 << preset.skyTopColor[0] << ", "
                 << preset.skyTopColor[1] << ", "
                 << preset.skyTopColor[2] << ", "
                 << preset.skyTopColor[3] << "], "
                 << "\"skyHorizonColor\": ["
                 << preset.skyHorizonColor[0] << ", "
                 << preset.skyHorizonColor[1] << ", "
                 << preset.skyHorizonColor[2] << ", "
                 << preset.skyHorizonColor[3] << "], "
                 << "\"preserveEnvironmentPath\": " << (preset.preserveEnvironmentPath ? "true" : "false") << ", "
                 << "\"environment\": { "
                 << "\"environmentPath\": \"" << EscapeJson(JsonPathString(preset.environment.environmentPath, projectDirectory)) << "\", "
                 << "\"rotationYaw\": " << preset.environment.rotationYaw << ", "
                 << "\"intensity\": " << preset.environment.intensity << ", "
                 << "\"backgroundMode\": \"" << EscapeJson(BackgroundModeJsonName(preset.environment.backgroundMode)) << "\", "
                 << "\"sunDirection\": ["
                 << preset.environment.sunDirection[0] << ", "
                 << preset.environment.sunDirection[1] << ", "
                 << preset.environment.sunDirection[2] << "], "
                 << "\"sunColor\": ["
                 << preset.environment.sunColor[0] << ", "
                 << preset.environment.sunColor[1] << ", "
                 << preset.environment.sunColor[2] << "], "
                 << "\"sunIntensity\": " << preset.environment.sunIntensity << " }, "
                 << "\"viewSettings\": { "
                 << "\"exposure\": " << preset.viewSettings.exposure << ", "
                 << "\"toneMapper\": \"" << EscapeJson(ToneMapperJsonName(preset.viewSettings.toneMapper)) << "\", "
                 << "\"gamma\": " << preset.viewSettings.gamma << ", "
                 << "\"displayMode\": \"" << EscapeJson(DisplayModeJsonName(preset.viewSettings.displayMode)) << "\", "
                 << "\"turntableEnabled\": " << (preset.viewSettings.turntableEnabled ? "true" : "false") << ", "
                 << "\"turntableSpeed\": " << preset.viewSettings.turntableSpeed << " } }";
            json << (i + 1 < m_project.lookDevPresets.size() ? "," : "") << "\n";
        }
        json << "  ],\n";
        json << "  \"shaderSourcePath\": \"" << EscapeJson(JsonPathString(m_activeShaderSet.sourcePath, projectDirectory)) << "\",\n";
        json << "  \"vertexEntry\": \"" << EscapeJson(WideToUtf8(m_activeShaderSet.vertexEntry)) << "\",\n";
        json << "  \"pixelEntry\": \"" << EscapeJson(WideToUtf8(m_activeShaderSet.pixelEntry)) << "\",\n";
        json << "  \"pipelineKind\": \"RasterVSPS\",\n";
        json << "  \"activeShaderSet\": \"" << EscapeJson(m_activeShaderSet.name) << "\",\n";
        json << "  \"shaderSets\": [\n";
        for (std::size_t i = 0; i < m_project.shaderSets.size(); ++i)
        {
            const ShaderSet& shaderSet = m_project.shaderSets[i];
            json << "    { "
                 << "\"name\": \"" << EscapeJson(shaderSet.name) << "\", "
                 << "\"sourcePath\": \"" << EscapeJson(JsonPathString(shaderSet.sourcePath, projectDirectory)) << "\", "
                 << "\"sourceText\": \"" << EscapeJson(shaderSet.sourceText) << "\", "
                 << "\"vertexEntry\": \"" << EscapeJson(WideToUtf8(shaderSet.vertexEntry)) << "\", "
                 << "\"pixelEntry\": \"" << EscapeJson(WideToUtf8(shaderSet.pixelEntry)) << "\", "
                 << "\"vertexProfile\": \"" << EscapeJson(WideToUtf8(shaderSet.vertexProfile)) << "\", "
                 << "\"pixelProfile\": \"" << EscapeJson(WideToUtf8(shaderSet.pixelProfile)) << "\", "
                 << "\"pipelineKind\": \"RasterVSPS\" }";
            json << (i + 1 < m_project.shaderSets.size() ? "," : "") << "\n";
        }
        json << "  ],\n";
        json << "  \"materials\": [\n";
        for (std::size_t i = 0; i < m_project.materialAssignments.size(); ++i)
        {
            const MaterialAssignment& material = m_project.materialAssignments[i];
            json << "    { "
                 << "\"name\": \"" << EscapeJson(material.materialName) << "\", "
                 << "\"shaderSet\": \"" << EscapeJson(material.shaderSetName) << "\", "
                 << "\"baseColorFactor\": ["
                 << material.baseColorFactor[0] << ", "
                 << material.baseColorFactor[1] << ", "
                 << material.baseColorFactor[2] << ", "
                 << material.baseColorFactor[3] << "], "
                 << "\"emissiveFactor\": ["
                 << material.emissiveFactor[0] << ", "
                 << material.emissiveFactor[1] << ", "
                 << material.emissiveFactor[2] << ", "
                 << material.emissiveFactor[3] << "], "
                 << "\"roughnessFactor\": " << material.roughnessFactor << ", "
                 << "\"metallicFactor\": " << material.metallicFactor << ", "
                 << "\"normalStrength\": " << material.normalStrength << ", "
                 << "\"occlusionStrength\": " << material.occlusionStrength << ", "
                 << "\"alphaMode\": \"" << EscapeJson(AlphaModeJsonName(material.alphaMode)) << "\", "
                 << "\"alphaCutoff\": " << material.alphaCutoff << ", "
                 << "\"flipNormalGreen\": " << (material.flipNormalGreen ? "true" : "false") << ", "
                 << "\"textures\": { ";
            for (std::size_t textureSlot = 0; textureSlot < MaterialTextureSlotCount; ++textureSlot)
            {
                json << "\"" << TextureSlotJsonNames[textureSlot] << "\": { "
                     << "\"override\": " << (material.textureOverrideEnabled[textureSlot] ? "true" : "false") << ", "
                     << "\"path\": \"" << EscapeJson(JsonPathString(material.textureOverrides[textureSlot], projectDirectory)) << "\" }";
                json << (textureSlot + 1 < MaterialTextureSlotCount ? ", " : "");
            }
            json << " } }";
            json << (i + 1 < m_project.materialAssignments.size() ? "," : "") << "\n";
        }
        json << "  ]\n";
        json << "}\n";

        WriteTextFile(path, json.str());
        m_project.path = path.wstring();
        AddRecentProject(path);
        SetProjectDirty(false);
        m_sceneDiagnostics = "Saved project to " + path.string();
        return true;
    }
    catch (const std::exception& ex)
    {
        m_sceneDiagnostics = "Project save failed: " + std::string(ex.what());
        return false;
    }
}

void RenderBuilderApp::LoadRecentProjects()
{
    m_recentProjects.clear();
    try
    {
        const std::filesystem::path recentPath = m_rootDirectory / "Assets" / "RecentProjects.txt";
        if (!std::filesystem::exists(recentPath))
        {
            return;
        }

        std::istringstream input(ReadTextFile(recentPath));
        std::string line;
        while (std::getline(input, line))
        {
            if (line.empty())
            {
                continue;
            }
            std::filesystem::path path(Utf8ToWide(line));
            if (std::find(m_recentProjects.begin(), m_recentProjects.end(), path) == m_recentProjects.end())
            {
                m_recentProjects.push_back(path);
            }
            if (m_recentProjects.size() >= 8)
            {
                break;
            }
        }
    }
    catch (...)
    {
    }
}

void RenderBuilderApp::SaveRecentProjects() const
{
    try
    {
        const std::filesystem::path recentPath = m_rootDirectory / "Assets" / "RecentProjects.txt";
        std::filesystem::create_directories(recentPath.parent_path());
        std::ostringstream output;
        for (const std::filesystem::path& path : m_recentProjects)
        {
            output << WideToUtf8(path.wstring()) << "\n";
        }
        WriteTextFile(recentPath, output.str());
    }
    catch (...)
    {
    }
}

void RenderBuilderApp::AddRecentProject(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return;
    }

    const std::filesystem::path normalized = std::filesystem::absolute(path).lexically_normal();
    m_recentProjects.erase(
        std::remove(m_recentProjects.begin(), m_recentProjects.end(), normalized),
        m_recentProjects.end());
    m_recentProjects.insert(m_recentProjects.begin(), normalized);
    if (m_recentProjects.size() > 8)
    {
        m_recentProjects.resize(8);
    }
    SaveRecentProjects();
}

void RenderBuilderApp::LoadProject()
{
    const auto path = OpenFileDialog(L"RenderBuilder Project\0*.renderbuilder.json;*.json\0All Files\0*.*\0");
    if (!path.empty())
    {
        LoadProjectFromDisk(path);
    }
}

void RenderBuilderApp::LoadProjectFromDisk(const std::filesystem::path& path)
{
    try
    {
        const std::filesystem::path projectPath = AbsoluteLexicalPath(path);
        const std::filesystem::path projectDirectory = projectPath.parent_path();
        const JsonValue root = JsonParser(ReadTextFile(projectPath)).Parse();
        if (root.type != JsonValue::Type::Object)
        {
            throw std::runtime_error("Project JSON root must be an object.");
        }

        std::ostringstream assetDiagnostics;
        ProjectFile loadedProject;
        loadedProject.path = projectPath.wstring();
        const std::filesystem::path scenePath = ResolveProjectPath(JsonStringOr(root, "scenePath"), projectDirectory);
        loadedProject.scenePath = scenePath.wstring();
        AppendMissingAssetDiagnostic(assetDiagnostics, "scene", scenePath);
        const std::array<float, 4> legacyClearColor = JsonFloat4Or(root, "viewportClearColor", loadedProject.skyHorizonColor);
        loadedProject.skyTopColor = JsonFloat4Or(root, "skyTopColor", loadedProject.skyTopColor);
        loadedProject.skyHorizonColor = JsonFloat4Or(root, "skyHorizonColor", legacyClearColor);
        const JsonValue* viewportCamera = FindMember(root, "viewportCamera");
        if (viewportCamera && viewportCamera->type == JsonValue::Type::Object)
        {
            loadedProject.viewportCamera.target = JsonFloat3Or(*viewportCamera, "target", loadedProject.viewportCamera.target);
            loadedProject.viewportCamera.yaw = static_cast<float>(JsonNumberOr(*viewportCamera, "yaw", loadedProject.viewportCamera.yaw));
            loadedProject.viewportCamera.pitch = static_cast<float>(JsonNumberOr(*viewportCamera, "pitch", loadedProject.viewportCamera.pitch));
            loadedProject.viewportCamera.distance = static_cast<float>(JsonNumberOr(*viewportCamera, "distance", loadedProject.viewportCamera.distance));
            loadedProject.hasViewportCamera = true;
        }
        const JsonValue* lookDevEnvironment = FindMember(root, "lookDevEnvironment");
        if (lookDevEnvironment && lookDevEnvironment->type == JsonValue::Type::Object)
        {
            loadedProject.lookDevEnvironment = JsonLookDevEnvironmentOr(*lookDevEnvironment, projectDirectory, loadedProject.lookDevEnvironment);
            AppendMissingAssetDiagnostic(assetDiagnostics, "environment", loadedProject.lookDevEnvironment.environmentPath);
        }
        const JsonValue* lookDevViewSettings = FindMember(root, "lookDevViewSettings");
        if (lookDevViewSettings && lookDevViewSettings->type == JsonValue::Type::Object)
        {
            loadedProject.lookDevViewSettings = JsonLookDevViewSettingsOr(*lookDevViewSettings, loadedProject.lookDevViewSettings);
        }
        loadedProject.activeLookDevPresetName = JsonStringOr(root, "activeLookDevPreset", loadedProject.activeLookDevPresetName);
        const JsonValue* lookDevPresets = FindMember(root, "lookDevPresets");
        if (lookDevPresets && lookDevPresets->type == JsonValue::Type::Array)
        {
            for (const JsonValue& presetValue : lookDevPresets->array)
            {
                if (presetValue.type != JsonValue::Type::Object)
                {
                    continue;
                }

                LookDevPreset preset;
                preset.name = JsonStringOr(presetValue, "name", "LookDev Preset");
                preset.skyTopColor = JsonFloat4Or(presetValue, "skyTopColor", preset.skyTopColor);
                preset.skyHorizonColor = JsonFloat4Or(presetValue, "skyHorizonColor", preset.skyHorizonColor);
                preset.preserveEnvironmentPath = JsonBoolOr(presetValue, "preserveEnvironmentPath", preset.preserveEnvironmentPath);
                const JsonValue* presetEnvironment = FindMember(presetValue, "environment");
                if (presetEnvironment && presetEnvironment->type == JsonValue::Type::Object)
                {
                    preset.environment = JsonLookDevEnvironmentOr(*presetEnvironment, projectDirectory, preset.environment);
                    AppendMissingAssetDiagnostic(assetDiagnostics, "preset environment '" + preset.name + "'", preset.environment.environmentPath);
                }
                const JsonValue* presetViewSettings = FindMember(presetValue, "viewSettings");
                if (presetViewSettings && presetViewSettings->type == JsonValue::Type::Object)
                {
                    preset.viewSettings = JsonLookDevViewSettingsOr(*presetViewSettings, preset.viewSettings);
                }
                loadedProject.lookDevPresets.push_back(preset);
            }
        }
        const std::string activeShaderSetName = JsonStringOr(root, "activeShaderSet", LookDevShaderSetName);

        const JsonValue* shaderSets = FindMember(root, "shaderSets");
        if (shaderSets && shaderSets->type == JsonValue::Type::Array)
        {
            for (const JsonValue& shaderSetValue : shaderSets->array)
            {
                if (shaderSetValue.type != JsonValue::Type::Object)
                {
                    continue;
                }

                ShaderSet shaderSet;
                shaderSet.name = JsonStringOr(shaderSetValue, "name", "Shader Set");
                const std::filesystem::path shaderSourcePath = ResolveProjectPath(JsonStringOr(shaderSetValue, "sourcePath"), projectDirectory);
                shaderSet.sourcePath = shaderSourcePath.wstring();
                shaderSet.sourceText = JsonStringOr(shaderSetValue, "sourceText");
                shaderSet.vertexEntry = Utf8ToWide(JsonStringOr(shaderSetValue, "vertexEntry", "VSMain"));
                shaderSet.pixelEntry = Utf8ToWide(JsonStringOr(shaderSetValue, "pixelEntry", "PSMain"));
                shaderSet.vertexProfile = Utf8ToWide(JsonStringOr(shaderSetValue, "vertexProfile", "vs_6_9"));
                shaderSet.pixelProfile = Utf8ToWide(JsonStringOr(shaderSetValue, "pixelProfile", "ps_6_9"));
                if (!shaderSourcePath.empty() && PathExists(shaderSourcePath))
                {
                    if (shaderSet.sourceText.empty())
                    {
                        shaderSet.sourceText = ReadTextFile(shaderSourcePath);
                    }
                }
                else
                {
                    AppendMissingAssetDiagnostic(assetDiagnostics, "shader source '" + shaderSet.name + "'", shaderSourcePath);
                }
                loadedProject.shaderSets.push_back(shaderSet);
            }
        }

        if (loadedProject.shaderSets.empty())
        {
            ShaderSet shaderSet;
            shaderSet.name = activeShaderSetName.empty() ? LookDevShaderSetName : activeShaderSetName;
            const std::filesystem::path shaderSourcePath = ResolveProjectPath(JsonStringOr(root, "shaderSourcePath"), projectDirectory);
            shaderSet.sourcePath = shaderSourcePath.wstring();
            shaderSet.vertexEntry = Utf8ToWide(JsonStringOr(root, "vertexEntry", "VSMain"));
            shaderSet.pixelEntry = Utf8ToWide(JsonStringOr(root, "pixelEntry", "PSMain"));
            shaderSet.vertexProfile = L"vs_6_9";
            shaderSet.pixelProfile = L"ps_6_9";
            if (!shaderSourcePath.empty() && PathExists(shaderSourcePath))
            {
                shaderSet.sourceText = ReadTextFile(shaderSourcePath);
            }
            else
            {
                AppendMissingAssetDiagnostic(assetDiagnostics, "shader source '" + shaderSet.name + "'", shaderSourcePath);
            }
            if (shaderSet.sourceText.empty())
            {
                shaderSet.sourcePath = (m_rootDirectory / "Shaders" / "LookDevPBR.hlsl").wstring();
                shaderSet.sourceText = ReadTextFile(shaderSet.sourcePath);
            }
            loadedProject.shaderSets.push_back(shaderSet);
        }

        const JsonValue* materials = FindMember(root, "materials");
        if (materials && materials->type == JsonValue::Type::Array)
        {
            for (const JsonValue& materialValue : materials->array)
            {
                if (materialValue.type != JsonValue::Type::Object)
                {
                    continue;
                }

                MaterialAssignment assignment;
                assignment.materialName = JsonStringOr(materialValue, "name");
                assignment.shaderSetName = JsonStringOr(materialValue, "shaderSet", loadedProject.shaderSets.front().name);
                assignment.baseColorFactor = JsonFloat4Or(materialValue, "baseColorFactor", assignment.baseColorFactor);
                assignment.emissiveFactor = JsonFloat4Or(materialValue, "emissiveFactor", assignment.emissiveFactor);
                assignment.roughnessFactor = static_cast<float>(JsonNumberOr(materialValue, "roughnessFactor", assignment.roughnessFactor));
                assignment.metallicFactor = static_cast<float>(JsonNumberOr(materialValue, "metallicFactor", assignment.metallicFactor));
                assignment.normalStrength = static_cast<float>(JsonNumberOr(materialValue, "normalStrength", assignment.normalStrength));
                assignment.occlusionStrength = static_cast<float>(JsonNumberOr(materialValue, "occlusionStrength", assignment.occlusionStrength));
                assignment.alphaMode = AlphaModeFromJson(JsonStringOr(materialValue, "alphaMode"), assignment.alphaMode);
                assignment.alphaCutoff = static_cast<float>(JsonNumberOr(materialValue, "alphaCutoff", assignment.alphaCutoff));
                assignment.flipNormalGreen = JsonBoolOr(materialValue, "flipNormalGreen", assignment.flipNormalGreen);
                const JsonValue* textures = FindMember(materialValue, "textures");
                if (textures && textures->type == JsonValue::Type::Object)
                {
                    for (std::size_t textureSlot = 0; textureSlot < MaterialTextureSlotCount; ++textureSlot)
                    {
                        const JsonValue* textureValue = FindMember(*textures, TextureSlotJsonNames[textureSlot]);
                        if (textureValue && textureValue->type == JsonValue::Type::Object)
                        {
                            assignment.textureOverrideEnabled[textureSlot] = JsonBoolOr(*textureValue, "override", false);
                            const std::filesystem::path texturePath = ResolveProjectPath(JsonStringOr(*textureValue, "path"), projectDirectory);
                            assignment.textureOverrides[textureSlot] = texturePath.wstring();
                            if (assignment.textureOverrideEnabled[textureSlot] && !texturePath.empty())
                            {
                                AppendMissingAssetDiagnostic(
                                    assetDiagnostics,
                                    "texture override '" + assignment.materialName + " / " + TextureSlotLabels[textureSlot] + "'",
                                    texturePath);
                            }
                        }
                    }
                }
                if (!assignment.materialName.empty())
                {
                    loadedProject.materialAssignments.push_back(assignment);
                }
            }
        }

        std::string sceneLoadDiagnostics;
        std::vector<MaterialAssignment> importedAssignments;
        if (!loadedProject.scenePath.empty())
        {
            if (PathExists(loadedProject.scenePath))
            {
                const bool sceneLoaded = LoadScenePath(loadedProject.scenePath, false);
                sceneLoadDiagnostics = m_sceneDiagnostics;
                if (sceneLoaded)
                {
                    importedAssignments = m_project.materialAssignments;
                }
                else
                {
                    UseDefaultScenePreview();
                }
            }
            else
            {
                UseDefaultScenePreview();
                sceneLoadDiagnostics = "Scene file was not found. Using built-in preview cube.";
            }
        }
        else
        {
            UseDefaultScenePreview();
            sceneLoadDiagnostics = "Using built-in preview cube.";
        }

        m_project = loadedProject;
        EnsureLookDevPresets();
        if (m_project.materialAssignments.empty())
        {
            m_project.materialAssignments = importedAssignments;
        }
        if (m_project.materialAssignments.empty())
        {
            m_project.materialAssignments.push_back({ "Default Material", m_project.shaderSets.front().name });
        }
        std::string environmentDiagnostics;
        if (!m_project.lookDevEnvironment.environmentPath.empty() && PathExists(m_project.lookDevEnvironment.environmentPath))
        {
            if (!m_backend.UpdateEnvironmentTexture(m_project.lookDevEnvironment.environmentPath, environmentDiagnostics))
            {
                environmentDiagnostics = "Environment load failed: " + environmentDiagnostics;
            }
        }
        else
        {
            m_backend.UpdateEnvironmentTexture({}, environmentDiagnostics);
        }
        ApplyLookDevSettings();
        m_backend.SetMaterialAssignments(m_project.materialAssignments);
        const std::string textureOverrideDiagnostics = ApplyMaterialTextureOverrides(m_project.materialAssignments);
        if (m_project.hasViewportCamera)
        {
            m_backend.SetCameraState(m_project.viewportCamera);
        }

        m_activeShaderSetIndex = 0;
        for (std::size_t i = 0; i < m_project.shaderSets.size(); ++i)
        {
            if (m_project.shaderSets[i].name == activeShaderSetName)
            {
                m_activeShaderSetIndex = i;
                break;
            }
        }
        m_activeShaderSet = m_project.shaderSets[m_activeShaderSetIndex];
        std::fill(m_shaderTextBuffer.begin(), m_shaderTextBuffer.end(), '\0');
        const std::size_t copySize = std::min(m_activeShaderSet.sourceText.size(), m_shaderTextBuffer.size() - 1);
        std::memcpy(m_shaderTextBuffer.data(), m_activeShaderSet.sourceText.data(), copySize);
        m_shaderDirty = false;
        m_shaderSetSerial = static_cast<std::uint32_t>(m_project.shaderSets.size() + 1);

        std::ostringstream diagnostics;
        bool activeCompileSucceeded = false;
        for (std::size_t i = 0; i < m_project.shaderSets.size(); ++i)
        {
            if (i == m_activeShaderSetIndex)
            {
                continue;
            }

            std::string shaderDiagnostics;
            CompileShaderSet(m_project.shaderSets[i], nullptr, nullptr, shaderDiagnostics);
            diagnostics << shaderDiagnostics << "\n\n";
        }

        std::string activeDiagnostics;
        activeCompileSucceeded = CompileShaderSet(m_project.shaderSets[m_activeShaderSetIndex], &m_activeVertexShader, &m_activePixelShader, activeDiagnostics);
        diagnostics << activeDiagnostics;

        m_lastCompileSucceeded = activeCompileSucceeded;
        m_compileDiagnostics = diagnostics.str();
        m_sceneDiagnostics = "Loaded project from " + projectPath.string();
        if (!sceneLoadDiagnostics.empty())
        {
            m_sceneDiagnostics += "\n" + sceneLoadDiagnostics;
        }
        const std::string missingAssetDiagnostics = assetDiagnostics.str();
        if (!missingAssetDiagnostics.empty())
        {
            m_sceneDiagnostics += "\nAsset diagnostics:" + missingAssetDiagnostics;
        }
        if (!textureOverrideDiagnostics.empty())
        {
            m_sceneDiagnostics += textureOverrideDiagnostics;
        }
        if (!environmentDiagnostics.empty())
        {
            m_sceneDiagnostics += "\n" + environmentDiagnostics;
        }
        AddRecentProject(projectPath);
        SetProjectDirty(false);
    }
    catch (const std::exception& ex)
    {
        m_sceneDiagnostics = "Project load failed: " + std::string(ex.what());
    }
}

std::filesystem::path RenderBuilderApp::OpenFileDialog(const wchar_t* filter) const
{
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW openFile = {};
    openFile.lStructSize = sizeof(openFile);
    openFile.hwndOwner = m_hwnd;
    openFile.lpstrFilter = filter;
    openFile.lpstrFile = fileName;
    openFile.nMaxFile = MAX_PATH;
    openFile.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&openFile))
    {
        return std::filesystem::path(fileName);
    }
    return {};
}

std::filesystem::path RenderBuilderApp::SaveFileDialog(const wchar_t* filter, const wchar_t* defaultExtension) const
{
    wchar_t fileName[MAX_PATH] = {};
    if (!m_project.path.empty())
    {
        wcsncpy_s(fileName, std::filesystem::path(m_project.path).filename().wstring().c_str(), _TRUNCATE);
    }
    else
    {
        wcsncpy_s(fileName, L"Untitled.renderbuilder.json", _TRUNCATE);
    }

    OPENFILENAMEW saveFile = {};
    saveFile.lStructSize = sizeof(saveFile);
    saveFile.hwndOwner = m_hwnd;
    saveFile.lpstrFilter = filter;
    saveFile.lpstrFile = fileName;
    saveFile.nMaxFile = MAX_PATH;
    saveFile.lpstrDefExt = defaultExtension;
    saveFile.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameW(&saveFile))
    {
        return std::filesystem::path(fileName);
    }
    return {};
}

LRESULT CALLBACK RenderBuilderApp::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam))
    {
        return true;
    }

    RenderBuilderApp* app = nullptr;
    if (message == WM_NCCREATE)
    {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = static_cast<RenderBuilderApp*>(createStruct->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    else
    {
        app = reinterpret_cast<RenderBuilderApp*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (app)
    {
        return app->HandleMessage(hwnd, message, wparam, lparam);
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

LRESULT RenderBuilderApp::HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_ENTERSIZEMOVE:
        m_inSizeMove = true;
        SetTimer(hwnd, ResizeMoveTimerId, 16, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
        m_inSizeMove = false;
        KillTimer(hwnd, ResizeMoveTimerId);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SIZE:
        m_windowWidth = LOWORD(lparam);
        m_windowHeight = HIWORD(lparam);
        if (wparam == SIZE_MINIMIZED)
        {
            m_minimized = true;
            return 0;
        }
        m_minimized = false;
        if (m_windowWidth > 0 && m_windowHeight > 0)
        {
            RequestResize(m_windowWidth, m_windowHeight);
        }
        return 0;
    case WM_TIMER:
        if (wparam == ResizeMoveTimerId && m_inSizeMove && !m_minimized)
        {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, ResizeMoveTimerId);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, message, wparam, lparam);
    }
}
}
