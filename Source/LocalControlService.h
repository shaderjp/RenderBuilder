#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace rb
{
struct LocalControlRequest
{
    std::uint64_t sequence = 0;
    std::string text;
    std::string response;
    bool completed = false;
    bool cancelled = false;
    std::mutex mutex;
    std::condition_variable completedCondition;
};

struct LocalControlStatus
{
    bool enabled = false;
    std::uint32_t connectedClientCount = 0;
    std::string pipeName;
    std::string lastTransportError;
};

class LocalControlService
{
public:
    static constexpr const wchar_t* PipeName = L"\\\\.\\pipe\\RenderBuilder.Control";

    LocalControlService() = default;
    ~LocalControlService();

    LocalControlService(const LocalControlService&) = delete;
    LocalControlService& operator=(const LocalControlService&) = delete;

    bool Start();
    void Stop();
    bool IsRunning() const;
    LocalControlStatus Status() const;

    std::shared_ptr<LocalControlRequest> TryPopRequest();
    void CompleteRequest(const std::shared_ptr<LocalControlRequest>& request, const std::string& response);

private:
    void ServerLoop();
    void ClientLoop(HANDLE pipe);
    bool QueueRequestAndWait(HANDLE pipe, const std::string& line);
    void WakeServer();
    void SetLastTransportError(const std::string& error);

    std::atomic<bool> m_stopRequested = false;
    std::atomic<bool> m_running = false;
    std::atomic<std::uint32_t> m_connectedClientCount = 0;
    std::atomic<std::uint64_t> m_nextSequence = 1;
    std::thread m_serverThread;

    mutable std::mutex m_queueMutex;
    std::deque<std::shared_ptr<LocalControlRequest>> m_requests;

    mutable std::mutex m_statusMutex;
    std::string m_lastTransportError;
};
}
