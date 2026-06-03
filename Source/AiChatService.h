#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rb
{
struct AiChatMessage
{
    std::string role;
    std::string content;
};

struct AiChatConfig
{
    std::filesystem::path serverExecutable;
    std::filesystem::path modelPath;
    std::string host = "127.0.0.1";
    std::uint16_t port = 18080;
    int contextTokens = 8192;
    int maxTokens = 1024;
    int gpuLayers = 0;
    int threads = 0;
    float temperature = 1.0f;
    float topP = 0.95f;
    int topK = 64;
    bool useJinja = true;
    bool startServer = true;
};

struct AiChatEvent
{
    enum class Kind
    {
        Status,
        Response,
        Error
    };

    Kind kind = Kind::Status;
    std::string text;
};

struct AiChatRuntimeStatus
{
    bool serverStartedByApp = false;
    bool busy = false;
    DWORD processId = 0;
    std::filesystem::path modelPath;
    std::string lastError;
};

class AiChatService
{
public:
    AiChatService() = default;
    ~AiChatService();

    AiChatService(const AiChatService&) = delete;
    AiChatService& operator=(const AiChatService&) = delete;

    bool Start(const AiChatConfig& config);
    void Stop();
    bool Submit(const std::vector<AiChatMessage>& messages);
    std::vector<AiChatEvent> DrainEvents();
    AiChatRuntimeStatus Status() const;

private:
    void WorkerMain(std::vector<AiChatMessage> messages);
    std::string SendChatCompletion(const std::vector<AiChatMessage>& messages, std::string& error) const;
    void PushEvent(AiChatEvent::Kind kind, const std::string& text);
    void SetLastError(const std::string& error);
    bool IsProcessAlive() const;

    AiChatConfig m_config;
    PROCESS_INFORMATION m_processInfo = {};
    bool m_hasProcess = false;

    mutable std::mutex m_mutex;
    std::deque<AiChatEvent> m_events;
    std::string m_lastError;

    std::thread m_worker;
    std::atomic<bool> m_busy = false;
    std::atomic<bool> m_stopRequested = false;
};
}
