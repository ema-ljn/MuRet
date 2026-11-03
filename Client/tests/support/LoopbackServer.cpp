#include "support/LoopbackServer.h"

#include "Network/SocketApi.h"
#include "support/NetworkPrinters.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace Client::Tests
{
namespace SocketApi = Network::SocketApi;

bool RetryUntil(const std::function<bool()>& attempt)
{
    const auto deadline = std::chrono::steady_clock::now() + LoopbackTimeout;
    while (!attempt())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

LoopbackServer::LoopbackServer()
{
    SocketApi::Opened listener = SocketApi::ListenOnLoopback();
    if (!listener.socket.IsOpen())
    {
        ADD_FAILURE() << "no se pudo escuchar en 127.0.0.1: error " << listener.result.systemError;
        return;
    }
    m_listener = std::move(listener.socket);
    m_port = SocketApi::LocalPort(m_listener.Get());
    EXPECT_NE(m_port, 0) << "getsockname no dio el puerto";
}

std::uint16_t LoopbackServer::Port() const
{
    return m_port;
}

bool LoopbackServer::Accept()
{
    if (!m_listener.IsOpen())
    {
        return false;
    }
    SocketApi::Opened accepted = SocketApi::Accept(m_listener.Get());
    if (accepted.result.status == SocketApi::Status::Failed)
    {
        ADD_FAILURE() << "accept dio el error " << accepted.result.systemError;
    }
    if (!accepted.socket.IsOpen())
    {
        return false;
    }
    m_client = std::move(accepted.socket);
    m_clientClosed = false;
    return true;
}

std::vector<std::byte> LoopbackServer::Read()
{
    constexpr std::size_t ChunkSize = 4096;
    std::vector<std::byte> received;
    while (m_client.IsOpen() && !m_clientClosed)
    {
        std::array<std::byte, ChunkSize> chunk{};
        const SocketApi::Result result = SocketApi::Receive(m_client.Get(), chunk);
        if (result.status == SocketApi::Status::Done)
        {
            received.insert(received.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(result.bytes));
            continue;
        }
        if (result.status == SocketApi::Status::PeerClosed)
        {
            m_clientClosed = true;
        }
        else if (result.status != SocketApi::Status::TryLater)
        {
            ADD_FAILURE() << "recv del servidor dio el error " << result.systemError;
        }
        break;
    }
    return received;
}

bool LoopbackServer::ClientClosed() const
{
    return m_clientClosed;
}

void LoopbackServer::Write(std::span<const std::byte> bytes)
{
    const SocketApi::Result result = SocketApi::Send(m_client.Get(), bytes);
    EXPECT_EQ(result.status, SocketApi::Status::Done) << "send del servidor: error " << result.systemError;
    EXPECT_EQ(result.bytes, bytes.size()) << "el sistema no tomó todo lo que mandó el servidor";
}

void LoopbackServer::CloseClient()
{
    m_client.Reset();
}

void LoopbackServer::ResetClient()
{
    SocketApi::CloseWithReset(m_client);
}
} // namespace Client::Tests
