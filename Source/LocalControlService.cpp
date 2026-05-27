#include "LocalControlService.h"

#include <chrono>
#include <sstream>
#include <vector>

namespace rb
{
namespace
{
std::string Win32ErrorMessage(DWORD error)
{
    std::ostringstream message;
    message << "Win32 error " << error;
    return message.str();
}

std::string TimeoutResponse()
{
    return "{\"id\":null,\"ok\":false,\"error\":{\"code\":\"Timeout\",\"message\":\"RenderBuilder did not process the local control request before timeout.\"}}";
}

std::string StoppedResponse()
{
    return "{\"id\":null,\"ok\":false,\"error\":{\"code\":\"Stopped\",\"message\":\"RenderBuilder local control service stopped before the request completed.\"}}";
}
}

LocalControlService::~LocalControlService()
{
    Stop();
}

bool LocalControlService::Start()
{
    if (m_running)
    {
        return true;
    }

    m_stopRequested = false;
    try
    {
        m_serverThread = std::thread(&LocalControlService::ServerLoop, this);
        m_running = true;
        return true;
    }
    catch (const std::exception& ex)
    {
        SetLastTransportError(ex.what());
        m_running = false;
        return false;
    }
}

void LocalControlService::Stop()
{
    if (!m_running && !m_serverThread.joinable())
    {
        return;
    }

    m_stopRequested = true;
    WakeServer();
    if (m_serverThread.joinable())
    {
        m_serverThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (const std::shared_ptr<LocalControlRequest>& request : m_requests)
        {
            std::lock_guard<std::mutex> requestLock(request->mutex);
            request->response = StoppedResponse();
            request->cancelled = true;
            request->completed = true;
            request->completedCondition.notify_all();
        }
        m_requests.clear();
    }

    m_connectedClientCount = 0;
    m_running = false;
}

bool LocalControlService::IsRunning() const
{
    return m_running && !m_stopRequested;
}

LocalControlStatus LocalControlService::Status() const
{
    LocalControlStatus status;
    status.enabled = IsRunning();
    status.connectedClientCount = m_connectedClientCount.load();
    status.pipeName = "\\\\.\\pipe\\RenderBuilder.Control";
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        status.lastTransportError = m_lastTransportError;
    }
    return status;
}

std::shared_ptr<LocalControlRequest> LocalControlService::TryPopRequest()
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    while (!m_requests.empty())
    {
        std::shared_ptr<LocalControlRequest> request = m_requests.front();
        m_requests.pop_front();
        std::lock_guard<std::mutex> requestLock(request->mutex);
        if (!request->cancelled)
        {
            return request;
        }
    }
    return {};
}

void LocalControlService::CompleteRequest(const std::shared_ptr<LocalControlRequest>& request, const std::string& response)
{
    if (!request)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(request->mutex);
        if (request->cancelled)
        {
            return;
        }
        request->response = response;
        request->completed = true;
    }
    request->completedCondition.notify_all();
}

void LocalControlService::ServerLoop()
{
    while (!m_stopRequested)
    {
        HANDLE pipe = CreateNamedPipeW(
            PipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT,
            1,
            64 * 1024,
            64 * 1024,
            0,
            nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            SetLastTransportError(Win32ErrorMessage(GetLastError()));
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        bool connected = false;
        while (!m_stopRequested)
        {
            if (ConnectNamedPipe(pipe, nullptr))
            {
                connected = true;
                break;
            }

            const DWORD error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED)
            {
                connected = true;
                break;
            }
            if (error != ERROR_PIPE_LISTENING && error != ERROR_NO_DATA)
            {
                SetLastTransportError(Win32ErrorMessage(error));
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        if (!connected)
        {
            CloseHandle(pipe);
            continue;
        }

        if (m_stopRequested)
        {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            break;
        }

        m_connectedClientCount = 1;
        ClientLoop(pipe);
        m_connectedClientCount = 0;

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    m_running = false;
}

void LocalControlService::ClientLoop(HANDLE pipe)
{
    std::string buffer;
    std::vector<char> chunk(4096);

    while (!m_stopRequested)
    {
        DWORD bytesRead = 0;
        if (!ReadFile(pipe, chunk.data(), static_cast<DWORD>(chunk.size()), &bytesRead, nullptr) || bytesRead == 0)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_NO_DATA)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            if (error != ERROR_BROKEN_PIPE && error != ERROR_PIPE_NOT_CONNECTED)
            {
                SetLastTransportError(Win32ErrorMessage(error));
            }
            break;
        }

        buffer.append(chunk.data(), chunk.data() + bytesRead);
        std::size_t newline = std::string::npos;
        while ((newline = buffer.find('\n')) != std::string::npos)
        {
            std::string line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line.empty())
            {
                continue;
            }
            if (!QueueRequestAndWait(pipe, line))
            {
                return;
            }
        }
    }
}

bool LocalControlService::QueueRequestAndWait(HANDLE pipe, const std::string& line)
{
    if (line.size() > 1024 * 1024)
    {
        const std::string response = "{\"id\":null,\"ok\":false,\"error\":{\"code\":\"PayloadTooLarge\",\"message\":\"Local control request exceeded 1 MiB.\"}}\n";
        DWORD bytesWritten = 0;
        return WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &bytesWritten, nullptr) != FALSE;
    }

    std::shared_ptr<LocalControlRequest> request = std::make_shared<LocalControlRequest>();
    request->sequence = m_nextSequence.fetch_add(1);
    request->text = line;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_requests.push_back(request);
    }

    std::unique_lock<std::mutex> lock(request->mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!request->completed && !m_stopRequested && std::chrono::steady_clock::now() < deadline)
    {
        request->completedCondition.wait_for(lock, std::chrono::milliseconds(50));
    }

    std::string response;
    if (request->completed)
    {
        response = request->response;
    }
    else if (m_stopRequested)
    {
        request->cancelled = true;
        response = StoppedResponse();
    }
    else
    {
        request->cancelled = true;
        response = TimeoutResponse();
    }
    response.push_back('\n');
    lock.unlock();

    DWORD bytesWritten = 0;
    if (!WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()), &bytesWritten, nullptr))
    {
        SetLastTransportError(Win32ErrorMessage(GetLastError()));
        return false;
    }
    return true;
}

void LocalControlService::WakeServer()
{
    HANDLE pipe = CreateFileW(PipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(pipe);
    }
}

void LocalControlService::SetLastTransportError(const std::string& error)
{
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_lastTransportError = error;
}
}
