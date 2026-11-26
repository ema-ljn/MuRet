#pragma once

#include "Core/Expected.h"
#include "Network/SocketError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// A client TCP socket that never waits, except resolving the name: the loop polls it once per frame on its thread
// (D-C5) (03-red-y-cifrado.md, "Sockets"). It can reconnect after Close(). SocketError::Closed covers both the other
// side closing and this one not being connected; the Connection calls Receive, Send and SendQueue::Flush only when
// Connected, so there a Closed always means the other side closed.
namespace Client::Network
{
enum class ConnectProgress : std::uint8_t
{
    InProgress,
    Connected,
};

class ISocket
{
public:
    virtual ~ISocket() = default;

    // Closes any previous socket first. InProgress almost always; Connected or Refused if the system already knows.
    [[nodiscard]] virtual Core::Expected<ConnectProgress, SocketError> StartConnect(std::string_view host,
                                                                                    std::uint16_t port) = 0;

    // SocketApi::CheckConnect. SO_ERROR clears when read, so the final result is kept: after Connected or an error it
    // gives the same without looking at the socket again. Without a StartConnect since the last Close, Closed.
    [[nodiscard]] virtual Core::Expected<ConnectProgress, SocketError> PollConnect() = 0;

    // Up to buffer.size(); 0 if nothing arrived. An empty buffer gives 0 without calling recv, which would give 0 and
    // read as a close. Before Connected, Closed.
    [[nodiscard]] virtual Core::Expected<std::size_t, SocketError> Receive(std::span<std::byte> buffer) = 0;

    // What the system took, 0 to bytes.size(). Before Connected, Closed.
    [[nodiscard]] virtual Core::Expected<std::size_t, SocketError> Send(std::span<const std::byte> bytes) = 0;

    virtual void Close() = 0;

    // From StartConnect (unless it did not resolve) to Close.
    [[nodiscard]] virtual bool IsOpen() const = 0;

    // The errno or WSAGetLastError of the last Failed, for the log.
    [[nodiscard]] virtual int LastSystemError() const = 0;

protected:
    // Like IObfuscation: copied and moved only from a derived class (slicing).
    ISocket() = default;
    ISocket(const ISocket&) = default;
    ISocket& operator=(const ISocket&) = default;
    ISocket(ISocket&&) = default;
    ISocket& operator=(ISocket&&) = default;
};
} // namespace Client::Network
