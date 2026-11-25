#include "Network/SystemSocket.h"

#include "Core/Expected.h"
#include "Network/IResolver.h"
#include "Network/ISocket.h"
#include "Network/SocketApi.h"
#include "Network/SocketError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace Client::Network
{
SystemSocket::SystemSocket(IResolver& resolver) : m_resolver(&resolver) {}

SystemSocket::SystemSocket(SystemSocket&& other) noexcept
    : m_resolver(other.m_resolver), m_socket(std::move(other.m_socket)),
      m_state(std::exchange(other.m_state, State::Idle)), m_failure(other.m_failure),
      m_lastSystemError(other.m_lastSystemError)
{
}

SystemSocket& SystemSocket::operator=(SystemSocket&& other) noexcept
{
    if (this != &other)
    {
        m_resolver = other.m_resolver;
        m_socket = std::move(other.m_socket);
        m_state = std::exchange(other.m_state, State::Idle);
        m_failure = other.m_failure;
        m_lastSystemError = other.m_lastSystemError;
    }
    return *this;
}

Core::Expected<ConnectProgress, SocketError> SystemSocket::StartConnect(std::string_view host, std::uint16_t port)
{
    Close();
    const Core::Expected<Ipv4Address, SocketError> ip = m_resolver->Resolve(host);
    if (!ip.HasValue())
    {
        return FailConnect(ip.Error(), 0);
    }
    SocketApi::Opened opened = SocketApi::OpenTcp();
    if (!opened.socket.IsOpen())
    {
        return FailConnect(SocketError::Failed, opened.result.systemError);
    }
    m_socket = std::move(opened.socket);
    m_state = State::Connecting;
    return Settle(SocketApi::Connect(m_socket.Get(), ip.Value(), port));
}

Core::Expected<ConnectProgress, SocketError> SystemSocket::PollConnect()
{
    switch (m_state)
    {
    case State::Idle:
        return Core::Unexpected(SocketError::Closed);
    case State::Connecting:
        return Settle(SocketApi::CheckConnect(m_socket.Get()));
    case State::Connected:
        return ConnectProgress::Connected;
    case State::Failed:
        return Core::Unexpected(m_failure);
    }
    return Core::Unexpected(SocketError::Failed);
}

Core::Expected<std::size_t, SocketError> SystemSocket::Receive(std::span<std::byte> buffer)
{
    if (m_state != State::Connected)
    {
        return Core::Unexpected(SocketError::Closed);
    }
    return Transferred(SocketApi::Receive(m_socket.Get(), buffer));
}

Core::Expected<std::size_t, SocketError> SystemSocket::Send(std::span<const std::byte> bytes)
{
    if (m_state != State::Connected)
    {
        return Core::Unexpected(SocketError::Closed);
    }
    return Transferred(SocketApi::Send(m_socket.Get(), bytes));
}

void SystemSocket::Close()
{
    m_socket.Reset();
    m_state = State::Idle;
}

bool SystemSocket::IsOpen() const
{
    return m_socket.IsOpen();
}

int SystemSocket::LastSystemError() const
{
    return m_lastSystemError;
}

// Solo el connect pasa por acá: Done es Connected, y un error queda como el resultado final.
Core::Expected<ConnectProgress, SocketError> SystemSocket::Settle(const SocketApi::Result& result)
{
    switch (result.status)
    {
    case SocketApi::Status::Done:
        m_state = State::Connected;
        return ConnectProgress::Connected;
    case SocketApi::Status::InProgress:
    case SocketApi::Status::TryLater:
        return ConnectProgress::InProgress;
    case SocketApi::Status::Refused:
        return FailConnect(SocketError::Refused, result.systemError);
    case SocketApi::Status::PeerClosed:
        return FailConnect(SocketError::Closed, result.systemError);
    case SocketApi::Status::Failed:
        break;
    }
    return FailConnect(SocketError::Failed, result.systemError);
}

Core::Expected<ConnectProgress, SocketError> SystemSocket::FailConnect(SocketError error, int systemError)
{
    m_state = State::Failed;
    m_failure = error;
    if (error == SocketError::Failed)
    {
        m_lastSystemError = systemError;
    }
    return Core::Unexpected(error);
}

Core::Expected<std::size_t, SocketError> SystemSocket::Transferred(const SocketApi::Result& result)
{
    switch (result.status)
    {
    case SocketApi::Status::Done:
        return result.bytes;
    case SocketApi::Status::TryLater:
        return std::size_t{0};
    case SocketApi::Status::PeerClosed:
        return Core::Unexpected(SocketError::Closed);
    case SocketApi::Status::InProgress:
    case SocketApi::Status::Refused:
    case SocketApi::Status::Failed:
        break;
    }
    m_lastSystemError = result.systemError;
    return Core::Unexpected(SocketError::Failed);
}
} // namespace Client::Network
