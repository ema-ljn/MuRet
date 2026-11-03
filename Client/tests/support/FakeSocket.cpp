#include "support/FakeSocket.h"

#include "App/ServerAddress.h"
#include "Core/Expected.h"
#include "Network/ISocket.h"
#include "Network/SocketError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Client::Tests
{
using Network::ConnectProgress;
using Network::SocketError;

Core::Expected<ConnectProgress, SocketError> FakeSocket::StartConnect(std::string_view host, std::uint16_t port)
{
    if (onStartConnect)
    {
        onStartConnect();
    }
    if (m_open)
    {
        Close();
    }
    connects.push_back(App::ServerAddress{.host = std::string(host), .port = port});
    // Como en SystemSocket, un nombre que no resuelve no llega a crear el socket.
    if (connectResult == SocketError::ResolveFailed)
    {
        return Finish();
    }
    m_open = true;
    m_state = State::Connecting;
    m_pollsLeft = pollsUntilConnected;
    if (m_pollsLeft > 0)
    {
        return ConnectProgress::InProgress;
    }
    return Finish();
}

Core::Expected<ConnectProgress, SocketError> FakeSocket::PollConnect()
{
    switch (m_state)
    {
    case State::Idle:
        return Core::Unexpected(SocketError::Closed);
    case State::Connecting:
        if (m_pollsLeft > 0)
        {
            --m_pollsLeft;
            return ConnectProgress::InProgress;
        }
        return Finish();
    case State::Connected:
        return ConnectProgress::Connected;
    case State::Failed:
        break;
    }
    return Core::Unexpected(m_failure.value_or(SocketError::Failed));
}

Core::Expected<std::size_t, SocketError> FakeSocket::Receive(std::span<std::byte> buffer)
{
    if (m_state != State::Connected)
    {
        return Core::Unexpected(SocketError::Closed);
    }
    if (buffer.empty())
    {
        return std::size_t{0};
    }
    if (reads.empty())
    {
        if (peerClosed)
        {
            return Core::Unexpected(SocketError::Closed);
        }
        return std::size_t{0};
    }
    std::vector<std::byte>& next = reads.front();
    const std::size_t count = std::min(buffer.size(), next.size());
    const auto end = next.begin() + static_cast<std::ptrdiff_t>(count);
    std::copy(next.begin(), end, buffer.begin());
    next.erase(next.begin(), end);
    if (next.empty())
    {
        reads.pop_front();
    }
    return count;
}

Core::Expected<std::size_t, SocketError> FakeSocket::Send(std::span<const std::byte> bytes)
{
    if (m_state != State::Connected)
    {
        return Core::Unexpected(SocketError::Closed);
    }
    if (sendError)
    {
        const SocketError error = *sendError;
        sendError.reset();
        return Core::Unexpected(error);
    }
    std::size_t taken = bytes.size();
    if (!sendLimits.empty())
    {
        const SendStep step = sendLimits.front();
        sendLimits.pop_front();
        if (step.error)
        {
            return Core::Unexpected(*step.error);
        }
        taken = std::min(taken, step.taken);
    }
    sent.insert(sent.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(taken));
    return taken;
}

void FakeSocket::Close()
{
    ++closes;
    m_open = false;
    m_state = State::Idle;
    m_failure.reset();
}

bool FakeSocket::IsOpen() const
{
    return m_open;
}

int FakeSocket::LastSystemError() const
{
    return systemError;
}

// El connect termina como dice el guion, y el resultado queda guardado.
Core::Expected<ConnectProgress, SocketError> FakeSocket::Finish()
{
    if (connectResult)
    {
        m_state = State::Failed;
        m_failure = connectResult;
        return Core::Unexpected(*connectResult);
    }
    m_state = State::Connected;
    return ConnectProgress::Connected;
}
} // namespace Client::Tests
