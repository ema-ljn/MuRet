#pragma once

#include "Core/Expected.h"
#include "EngineApi.h"
#include "Network/IResolver.h"
#include "Network/ISocket.h"
#include "Network/SocketApi.h"
#include "Network/SocketError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Non-blocking from the start, with the connect as a state machine the loop polls each frame. Unlike the original:
// the connect does not wait (0x4e8570's does, with the socket still blocking), 127.0.0.1 is accepted (0x4e8570 rejects
// it), and closing is a normal close, without 0x4e8380's SO_LINGER {1, 0} that cuts with a RST. Needs a live
// SocketApi::NetworkInit while it has a socket.
namespace Client::Network
{
class ENGINE_API SystemSocket final : public ISocket
{
public:
    // The resolver must outlive the socket. A temporary does not compile.
    explicit SystemSocket(IResolver& resolver);
    SystemSocket(IResolver&&) = delete;

    // A moved-from one is left closed: the system socket belongs to the other.
    SystemSocket(SystemSocket&& other) noexcept;
    SystemSocket& operator=(SystemSocket&& other) noexcept;
    SystemSocket(const SystemSocket&) = delete;
    SystemSocket& operator=(const SystemSocket&) = delete;
    ~SystemSocket() override = default;

    [[nodiscard]] Core::Expected<ConnectProgress, SocketError> StartConnect(std::string_view host,
                                                                            std::uint16_t port) override;
    [[nodiscard]] Core::Expected<ConnectProgress, SocketError> PollConnect() override;
    [[nodiscard]] Core::Expected<std::size_t, SocketError> Receive(std::span<std::byte> buffer) override;
    [[nodiscard]] Core::Expected<std::size_t, SocketError> Send(std::span<const std::byte> bytes) override;
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    [[nodiscard]] int LastSystemError() const override;

private:
    enum class State : std::uint8_t
    {
        Idle,       // no StartConnect since the last Close
        Connecting, // waiting for CheckConnect to say how it ended
        Connected,
        Failed, // the connect ended with m_failure
    };

    // The final result stays saved.
    Core::Expected<ConnectProgress, SocketError> Settle(const SocketApi::Result& result);
    Core::Expected<ConnectProgress, SocketError> FailConnect(SocketError error, int systemError);
    Core::Expected<std::size_t, SocketError> Transferred(const SocketApi::Result& result);

    // Never null: a pointer, so the socket can be moved.
    IResolver* m_resolver;
    SocketApi::UniqueSocket m_socket;
    State m_state = State::Idle;
    SocketError m_failure = SocketError::Closed; // the connect's, with m_state at Failed
    int m_lastSystemError = 0;
};
} // namespace Client::Network
