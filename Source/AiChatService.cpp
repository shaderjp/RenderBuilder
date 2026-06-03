#include "AiChatService.h"

#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

#pragma comment(lib, "winhttp.lib")

namespace rb
{
namespace
{
std::wstring Utf8ToWideLocal(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
    {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string Win32ErrorMessage(DWORD error)
{
    std::ostringstream message;
    message << "Win32 error " << error;
    return message.str();
}

std::string JsonEscapeLocal(const std::string& text)
{
    std::ostringstream escaped;
    escaped << std::hex << std::setfill('0');
    for (const unsigned char ch : text)
    {
        switch (ch)
        {
        case '\\': escaped << "\\\\"; break;
        case '"': escaped << "\\\""; break;
        case '\n': escaped << "\\n"; break;
        case '\r': escaped << "\\r"; break;
        case '\t': escaped << "\\t"; break;
        default:
            if (ch < 0x20)
            {
                escaped << "\\u" << std::setw(4) << static_cast<int>(ch);
            }
            else
            {
                escaped << static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped.str();
}

void AppendUtf8(std::string& output, std::uint32_t codePoint)
{
    if (codePoint <= 0x7f)
    {
        output.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint <= 0x7ff)
    {
        output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
    else if (codePoint <= 0xffff)
    {
        output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
    else
    {
        output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
}

bool ReadHex4(const std::string& text, std::size_t offset, std::uint32_t& value)
{
    if (offset + 4 > text.size())
    {
        return false;
    }

    value = 0;
    for (std::size_t i = 0; i < 4; ++i)
    {
        const char ch = text[offset + i];
        value <<= 4;
        if (ch >= '0' && ch <= '9')
        {
            value |= static_cast<std::uint32_t>(ch - '0');
        }
        else if (ch >= 'a' && ch <= 'f')
        {
            value |= static_cast<std::uint32_t>(ch - 'a' + 10);
        }
        else if (ch >= 'A' && ch <= 'F')
        {
            value |= static_cast<std::uint32_t>(ch - 'A' + 10);
        }
        else
        {
            return false;
        }
    }
    return true;
}

bool ParseJsonStringAt(const std::string& text, std::size_t offset, std::string& output, std::size_t& nextOffset)
{
    if (offset >= text.size() || text[offset] != '"')
    {
        return false;
    }

    output.clear();
    std::size_t i = offset + 1;
    while (i < text.size())
    {
        const char ch = text[i++];
        if (ch == '"')
        {
            nextOffset = i;
            return true;
        }
        if (ch != '\\')
        {
            output.push_back(ch);
            continue;
        }
        if (i >= text.size())
        {
            return false;
        }

        const char escaped = text[i++];
        switch (escaped)
        {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'u':
        {
            std::uint32_t codePoint = 0;
            if (!ReadHex4(text, i, codePoint))
            {
                return false;
            }
            i += 4;
            if (codePoint >= 0xd800 && codePoint <= 0xdbff && i + 6 <= text.size() && text[i] == '\\' && text[i + 1] == 'u')
            {
                std::uint32_t low = 0;
                if (ReadHex4(text, i + 2, low) && low >= 0xdc00 && low <= 0xdfff)
                {
                    codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
                    i += 6;
                }
            }
            AppendUtf8(output, codePoint);
            break;
        }
        default:
            return false;
        }
    }
    return false;
}

bool FindJsonStringProperty(const std::string& json, const std::string& property, std::string& value)
{
    for (std::size_t i = 0; i < json.size(); ++i)
    {
        if (json[i] != '"')
        {
            continue;
        }

        std::string key;
        std::size_t next = 0;
        if (!ParseJsonStringAt(json, i, key, next))
        {
            continue;
        }
        i = next;
        if (key != property)
        {
            continue;
        }

        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i])))
        {
            ++i;
        }
        if (i >= json.size() || json[i] != ':')
        {
            continue;
        }
        ++i;
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i])))
        {
            ++i;
        }
        if (ParseJsonStringAt(json, i, value, next))
        {
            return true;
        }
    }
    return false;
}

std::string BuildChatCompletionBody(const AiChatConfig& config, const std::vector<AiChatMessage>& messages)
{
    std::ostringstream body;
    body << "{\"model\":\"local-gemma-4-E4B-it\",\"stream\":false"
         << ",\"temperature\":" << config.temperature
         << ",\"top_p\":" << config.topP
         << ",\"top_k\":" << config.topK
         << ",\"max_tokens\":" << config.maxTokens
         << ",\"messages\":[";
    for (std::size_t i = 0; i < messages.size(); ++i)
    {
        body << "{\"role\":\"" << JsonEscapeLocal(messages[i].role)
             << "\",\"content\":\"" << JsonEscapeLocal(messages[i].content) << "\"}";
        if (i + 1 < messages.size())
        {
            body << ",";
        }
    }
    body << "]}";
    return body.str();
}

struct HttpResponse
{
    DWORD statusCode = 0;
    std::string body;
};

std::string HttpResponseMessage(const HttpResponse& response)
{
    std::string message;
    if (!FindJsonStringProperty(response.body, "message", message))
    {
        message = response.body;
    }
    return message;
}

std::string ToLowerAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool IsModelLoadingResponse(const HttpResponse& response)
{
    if (response.statusCode != 503)
    {
        return false;
    }

    const std::string message = ToLowerAscii(HttpResponseMessage(response));
    return message.find("loading model") != std::string::npos;
}

bool PostJson(
    const std::string& host,
    std::uint16_t port,
    const wchar_t* path,
    const std::string& body,
    HttpResponse& response,
    std::string& error)
{
    HINTERNET session = WinHttpOpen(
        L"RenderBuilder AI Chat/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session)
    {
        error = Win32ErrorMessage(GetLastError());
        return false;
    }

    const std::wstring wideHost = Utf8ToWideLocal(host);
    HINTERNET connect = WinHttpConnect(session, wideHost.c_str(), port, 0);
    if (!connect)
    {
        error = Win32ErrorMessage(GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connect, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request)
    {
        error = Win32ErrorMessage(GetLastError());
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD timeoutMs = 120000;
    WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    const wchar_t* headers = L"Content-Type: application/json\r\n";
    if (!WinHttpSendRequest(
            request,
            headers,
            static_cast<DWORD>(-1L),
            const_cast<char*>(body.data()),
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()),
            0))
    {
        error = Win32ErrorMessage(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    if (!WinHttpReceiveResponse(request, nullptr))
    {
        error = Win32ErrorMessage(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD statusSize = sizeof(response.statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &response.statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);

    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available > 0)
    {
        std::string chunk(available, '\0');
        DWORD downloaded = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &downloaded))
        {
            error = Win32ErrorMessage(GetLastError());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }
        chunk.resize(downloaded);
        response.body += chunk;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return true;
}

bool GetJson(
    const std::string& host,
    std::uint16_t port,
    const wchar_t* path,
    HttpResponse& response,
    std::string& error)
{
    HINTERNET session = WinHttpOpen(
        L"RenderBuilder AI Chat/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session)
    {
        error = Win32ErrorMessage(GetLastError());
        return false;
    }

    const std::wstring wideHost = Utf8ToWideLocal(host);
    HINTERNET connect = WinHttpConnect(session, wideHost.c_str(), port, 0);
    if (!connect)
    {
        error = Win32ErrorMessage(GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request)
    {
        error = Win32ErrorMessage(GetLastError());
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD timeoutMs = 3000;
    WinHttpSetOption(request, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

    if (!WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0))
    {
        error = Win32ErrorMessage(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    if (!WinHttpReceiveResponse(request, nullptr))
    {
        error = Win32ErrorMessage(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD statusSize = sizeof(response.statusCode);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &response.statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);

    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available > 0)
    {
        std::string chunk(available, '\0');
        DWORD downloaded = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &downloaded))
        {
            error = Win32ErrorMessage(GetLastError());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }
        chunk.resize(downloaded);
        response.body += chunk;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return true;
}
}

AiChatService::~AiChatService()
{
    Stop();
}

bool AiChatService::Start(const AiChatConfig& config)
{
    Stop();

    m_config = config;
    m_stopRequested = false;
    SetLastError({});
    ResetReadyDiagnostics();
    SetModelState(AiChatModelState::Starting, "Starting");

    if (!config.startServer)
    {
        SetModelState(AiChatModelState::Loading, "Checking external endpoint");
        PushEvent(AiChatEvent::Kind::Status, "Using external llama-server endpoint.");
        m_readyWorker = std::thread(&AiChatService::ReadyMonitorMain, this);
        return true;
    }

    if (config.serverExecutable.empty() || !std::filesystem::exists(config.serverExecutable))
    {
        SetLastError("llama-server executable was not found.");
        SetModelState(AiChatModelState::Failed, "Failed");
        return false;
    }
    if (config.modelPath.empty() || !std::filesystem::exists(config.modelPath))
    {
        SetLastError("GGUF model file was not found.");
        SetModelState(AiChatModelState::Failed, "Failed");
        return false;
    }

    std::wostringstream command;
    command << L"\"" << config.serverExecutable.wstring() << L"\""
            << L" -m \"" << config.modelPath.wstring() << L"\""
            << L" --host " << Utf8ToWideLocal(config.host)
            << L" --port " << config.port
            << L" -c " << config.contextTokens
            << L" -ngl " << Utf8ToWideLocal(config.gpuLayers)
            << L" --temp " << config.temperature
            << L" --top-p " << config.topP
            << L" --top-k " << config.topK
            << L" --reasoning off"
            << L" --reasoning-budget 0";
    if (config.threads > 0)
    {
        command << L" -t " << config.threads;
    }
    if (config.useJinja)
    {
        command << L" --jinja";
    }

    std::wstring mutableCommand = command.str();
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo = {};
    if (!CreateProcessW(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            config.serverExecutable.parent_path().wstring().c_str(),
            &startupInfo,
            &processInfo))
    {
        SetLastError("Failed to start llama-server: " + Win32ErrorMessage(GetLastError()));
        SetModelState(AiChatModelState::Failed, "Failed");
        return false;
    }

    m_processInfo = processInfo;
    m_hasProcess = true;
    SetModelState(AiChatModelState::Loading, "Loading model");
    PushEvent(AiChatEvent::Kind::Status, "Started llama-server. Loading local model...");
    m_readyWorker = std::thread(&AiChatService::ReadyMonitorMain, this);
    return true;
}

void AiChatService::Stop()
{
    m_stopRequested = true;
    if (m_worker.joinable())
    {
        m_worker.join();
    }
    if (m_readyWorker.joinable())
    {
        m_readyWorker.join();
    }
    m_busy = false;

    if (m_hasProcess)
    {
        if (IsProcessAlive())
        {
            TerminateProcess(m_processInfo.hProcess, 0);
            WaitForSingleObject(m_processInfo.hProcess, 5000);
        }
        CloseHandle(m_processInfo.hThread);
        CloseHandle(m_processInfo.hProcess);
        m_processInfo = {};
        m_hasProcess = false;
    }
    SetModelState(AiChatModelState::Stopped, "Stopped");
}

bool AiChatService::Submit(const std::vector<AiChatMessage>& messages)
{
    if (m_busy)
    {
        SetLastError("AI chat is already generating a response.");
        return false;
    }
    if (messages.empty())
    {
        SetLastError("AI chat request had no messages.");
        return false;
    }
    if (ModelState() != AiChatModelState::Ready)
    {
        SetLastError("AI model is not ready yet.");
        return false;
    }
    if (m_worker.joinable())
    {
        m_worker.join();
    }

    m_busy = true;
    m_stopRequested = false;
    m_worker = std::thread(&AiChatService::WorkerMain, this, messages);
    return true;
}

std::vector<AiChatEvent> AiChatService::DrainEvents()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AiChatEvent> events(m_events.begin(), m_events.end());
    m_events.clear();
    return events;
}

AiChatRuntimeStatus AiChatService::Status() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    AiChatRuntimeStatus status;
    status.serverStartedByApp = m_hasProcess;
    status.busy = m_busy;
    status.processId = m_hasProcess ? m_processInfo.dwProcessId : 0;
    status.modelPath = m_config.modelPath;
    status.lastError = m_lastError;
    status.modelState = m_modelState;
    status.modelStateText = m_modelStateText;
    status.modelLoadSeconds = m_modelLoadSeconds;
    if (m_modelLoadStartTime != std::chrono::steady_clock::time_point{} &&
        (m_modelState == AiChatModelState::Starting || m_modelState == AiChatModelState::Loading))
    {
        status.modelLoadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_modelLoadStartTime).count();
    }
    status.readyCheckCount = m_readyCheckCount;
    status.lastReadyHttpStatus = m_lastReadyHttpStatus;
    status.lastReadyError = m_lastReadyError;
    return status;
}

void AiChatService::ReadyMonitorMain()
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    std::string lastStatus;

    while (!m_stopRequested)
    {
        if (m_hasProcess && !IsProcessAlive())
        {
            SetModelState(AiChatModelState::Failed, "Failed");
            SetLastError("llama-server stopped before the model became ready.");
            PushEvent(AiChatEvent::Kind::Error, "llama-server stopped before the model became ready.");
            return;
        }

        HttpResponse response;
        std::string requestError;
        std::string nextStatus;
        AiChatModelState nextState = AiChatModelState::Starting;
        if (GetJson(m_config.host, m_config.port, L"/v1/models", response, requestError))
        {
            RecordReadyProbe(response.statusCode, response.statusCode == 200 ? std::string{} : HttpResponseMessage(response));
            if (response.statusCode == 200)
            {
                SetModelState(AiChatModelState::Ready, "Ready");
                SetLastError({});
                PushEvent(AiChatEvent::Kind::Status, "AI model ready.");
                return;
            }

            nextState = AiChatModelState::Loading;
            nextStatus = "Loading model";
            if (response.statusCode >= 400 && !IsModelLoadingResponse(response))
            {
                nextStatus = "Waiting for model endpoint";
            }
        }
        else
        {
            RecordReadyProbe(0, requestError);
            nextState = AiChatModelState::Starting;
            nextStatus = "Starting llama-server";
        }

        if (nextStatus != lastStatus)
        {
            SetModelState(nextState, nextStatus);
            PushEvent(AiChatEvent::Kind::Status, nextStatus + "...");
            lastStatus = nextStatus;
        }

        if (std::chrono::steady_clock::now() >= deadline)
        {
            SetModelState(AiChatModelState::Failed, "Failed");
            SetLastError("AI model did not become ready within 5 minutes.");
            PushEvent(AiChatEvent::Kind::Error, "AI model did not become ready within 5 minutes.");
            return;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void AiChatService::WorkerMain(std::vector<AiChatMessage> messages)
{
    PushEvent(AiChatEvent::Kind::Status, "Sending prompt to local model...");
    std::string error;
    const std::string text = SendChatCompletion(messages, error);
    if (!error.empty())
    {
        SetLastError(error);
        PushEvent(AiChatEvent::Kind::Error, error);
    }
    else if (!m_stopRequested)
    {
        PushEvent(AiChatEvent::Kind::Response, text);
    }
    m_busy = false;
}

std::string AiChatService::SendChatCompletion(const std::vector<AiChatMessage>& messages, std::string& error)
{
    const std::string body = BuildChatCompletionBody(m_config, messages);
    HttpResponse response;
    bool notifiedModelLoading = false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    do
    {
        std::string requestError;
        response = {};
        if (PostJson(m_config.host, m_config.port, L"/v1/chat/completions", body, response, requestError))
        {
            if (IsModelLoadingResponse(response))
            {
                if (!notifiedModelLoading)
                {
                    PushEvent(AiChatEvent::Kind::Status, "The local model is still loading. The request will be retried automatically.");
                    notifiedModelLoading = true;
                }
                if (m_stopRequested)
                {
                    error = "AI chat request was stopped.";
                    return {};
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            break;
        }
        if (m_stopRequested)
        {
            error = "AI chat request was stopped.";
            return {};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        error = requestError;
    }
    while (std::chrono::steady_clock::now() < deadline);

    if (response.statusCode == 0)
    {
        if (error.empty())
        {
            error = "llama-server did not respond.";
        }
        return {};
    }

    if (response.statusCode >= 400)
    {
        const std::string message = HttpResponseMessage(response);
        std::ostringstream stream;
        stream << "llama-server HTTP " << response.statusCode << ": " << message;
        error = stream.str();
        return {};
    }

    std::string content;
    if (!FindJsonStringProperty(response.body, "content", content))
    {
        error = "llama-server response did not contain an assistant message.";
        return {};
    }

    error.clear();
    return content;
}

void AiChatService::PushEvent(AiChatEvent::Kind kind, const std::string& text)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back({ kind, text });
}

void AiChatService::SetLastError(const std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastError = error;
}

void AiChatService::SetModelState(AiChatModelState state, const std::string& text)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if ((state == AiChatModelState::Ready || state == AiChatModelState::Failed || state == AiChatModelState::Stopped) &&
        m_modelLoadStartTime != std::chrono::steady_clock::time_point{})
    {
        m_modelLoadSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_modelLoadStartTime).count();
    }
    m_modelState = state;
    m_modelStateText = text;
}

void AiChatService::ResetReadyDiagnostics()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_modelLoadStartTime = std::chrono::steady_clock::now();
    m_modelLoadSeconds = 0.0;
    m_readyCheckCount = 0;
    m_lastReadyHttpStatus = 0;
    m_lastReadyError.clear();
}

void AiChatService::RecordReadyProbe(DWORD httpStatus, const std::string& error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_readyCheckCount;
    m_lastReadyHttpStatus = httpStatus;
    m_lastReadyError = error;
}

AiChatModelState AiChatService::ModelState() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_modelState;
}

bool AiChatService::IsProcessAlive() const
{
    if (!m_hasProcess || !m_processInfo.hProcess)
    {
        return false;
    }

    DWORD exitCode = 0;
    return GetExitCodeProcess(m_processInfo.hProcess, &exitCode) && exitCode == STILL_ACTIVE;
}
}
